# XLISP Threading Support

## Overview

XLISP supports native multi-threading through per-thread interpreter
contexts. Each thread gets a completely independent interpreter instance
with its own stacks, heap, symbol tables, garbage collector, and VM
registers. Lisp data cannot be shared between threads directly.

Threads coordinate through synchronization primitives (mutexes and
condition variables) that are shared at the C level via a named registry.
Data is exchanged between threads as strings.

Threading requires a reentrant build (`make THREADS=1`).

## Building

```bash
make clean
make THREADS=1
```

This defines `XLISP_USE_CONTEXT`, which enables thread-local storage for
the interpreter state. To verify:

```bash
nm lib/libxlisp.a | grep xlCreateContext
```

You should see `T xlCreateContext` in the output.

## Architecture

### Thread-Local Interpreter Contexts

All interpreter state that was previously stored in global variables is
encapsulated in an `xlContext` structure (defined in
`include/xlcontext.h`). This includes:

- VM registers (`fun`, `env`, `val`, `argc`, `next`)
- Value and control stacks
- Bytecode interpreter state (`pc`, `pcBase`, error targets, catch frames)
- Memory management (node segments, vector segments, free lists, GC state)
- Singleton values (`#t`, `#f`, unbound, default, eof)
- Package system (package list, LISP/XLISP/KEYWORD packages)
- Cached symbols (~40 frequently-used symbols)
- Reader, printer, and I/O state
- Compiler state (code buffer, compiler info)
- Initialization flag and command line

### Thread-Local Storage

A thread-local pointer to the current context is maintained using:

- `__thread` keyword on GCC/Clang (Linux, macOS)
- `__declspec(thread)` on MSVC (Windows)
- `pthread_getspecific` as a fallback

### Compatibility Macros

The header `include/xlcompat.h` provides macros that redirect all former
global variable accesses through the context pointer. For example:

```c
#define xlVal       (xlCtx()->val)
#define xlSP        (xlCtx()->sp)
#define xlTrue      (xlCtx()->vTrue)
#define s_quote     (xlCtx()->sym.quote)
```

This allows all existing code to work unchanged. File-static variables in
`xlint.c` (bytecode interpreter) and `xlcom.c` (compiler) are similarly
redirected when `XLISP_USE_CONTEXT` is defined.

### Dispatch Table

The bytecode `optab[256]` dispatch table is shared and immutable. It is
initialized once (protected by an `optabInitialized` flag) and read-only
thereafter, making it safe for concurrent access.

## Lisp-Level Threading API

### Thread Creation

```lisp
(THREAD-CREATE expr-string [init-file])
```

Creates a new native OS thread that evaluates *expr-string* (a string
containing a Lisp expression) in its own interpreter context.

- *init-file*: optional string naming an initialization file to load
  before evaluating the expression. Default is `"xlisp.lsp"`. Pass `#f`
  to skip loading any init file.
- Returns a thread handle (foreign pointer).

**Important:** `THREAD-CREATE` evaluates only the **first** expression
in the string. To execute multiple statements, wrap them in `(begin ...)`:

```lisp
(define h (thread-create
  "(begin
     (define (fib n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))
     (fib 30))"
  #f))
```

### Thread Join

```lisp
(THREAD-JOIN handle)
```

Waits for the thread to complete. Returns `#t` on success. Signals an
error if the thread terminated with an error or was already joined. Each
handle must be joined exactly once.

### Thread Predicate

```lisp
(THREAD? obj)
```

Returns `#t` if *obj* is a live (not yet joined) thread handle.

## Synchronization Primitives

Synchronization objects are shared across threads through a global
**named registry**. The creating thread registers an object by name; child
threads look it up by the same name. Both threads' Lisp-level handles
point to the same underlying C structure. Reference counting prevents
use-after-free.

### Mutexes

```lisp
(MUTEX-CREATE [name])       ; create a mutex, optionally named
(MUTEX-LOCK mutex)          ; acquire the mutex (blocking)
(MUTEX-UNLOCK mutex)        ; release the mutex
(MUTEX-DESTROY mutex)       ; destroy the mutex
(MUTEX-LOOKUP name)         ; look up a named mutex => handle or #f
(MUTEX? obj)                ; type predicate
```

Cross-thread example:

```lisp
;; Main thread
(define m (mutex-create "my-lock"))
(mutex-lock m)

;; Child thread (all in one expression)
(define h (thread-create
  "(begin
     (define m (mutex-lookup \"my-lock\"))
     (mutex-lock m)
     ;; ... critical section ...
     (mutex-unlock m))"
  #f))

(mutex-unlock m)
(thread-join h)
(mutex-destroy m)
```

### Condition Variables

```lisp
(CONDITION-CREATE [name])          ; create a condition variable
(CONDITION-WAIT cond mutex)        ; atomically unlock mutex and wait
(CONDITION-SIGNAL cond)            ; wake one waiting thread
(CONDITION-BROADCAST cond)         ; wake all waiting threads
(CONDITION-DESTROY cond)           ; destroy the condition variable
(CONDITION-LOOKUP name)            ; look up by name => handle or #f
(CONDITION? obj)                   ; type predicate
```

Signal/wait example:

```lisp
(define m (mutex-create "m"))
(define cv (condition-create "cv"))

;; Main locks before spawning child, ensuring child blocks on mutex-lock
;; until main enters condition-wait (which releases the mutex).
(mutex-lock m)

(define h (thread-create
  "(begin
     (define m (mutex-lookup \"m\"))
     (define c (condition-lookup \"cv\"))
     (mutex-lock m)
     (condition-signal c)
     (mutex-unlock m))"
  #f))

(condition-wait cv m)   ; releases m, blocks, reacquires m on wake
(mutex-unlock m)
(thread-join h)
```

Broadcast with ready-signaling:

```lisp
(define m (mutex-create "m"))
(define bc (condition-create "bc"))
(define r1 (condition-create "r1"))
(define r2 (condition-create "r2"))

(mutex-lock m)

;; Child 1: signal ready, then wait for broadcast
(define h1 (thread-create
  "(begin
     (define m (mutex-lookup \"m\"))
     (define bc (condition-lookup \"bc\"))
     (define r (condition-lookup \"r1\"))
     (mutex-lock m)
     (condition-signal r)
     (condition-wait bc m)
     (mutex-unlock m))" #f))
(condition-wait r1 m)   ; wait until child 1 is ready

;; Child 2: same pattern
(define h2 (thread-create
  "(begin
     (define m (mutex-lookup \"m\"))
     (define bc (condition-lookup \"bc\"))
     (define r (condition-lookup \"r2\"))
     (mutex-lock m)
     (condition-signal r)
     (condition-wait bc m)
     (mutex-unlock m))" #f))
(condition-wait r2 m)   ; wait until child 2 is ready

;; Both children are now in condition-wait on bc
(condition-broadcast bc)
(mutex-unlock m)

(thread-join h1)
(thread-join h2)
```

### Type Discrimination

Mutexes, condition variables, and thread handles are all foreign pointers
but are tagged with distinct `xlCClass` type markers. The predicates
`mutex?`, `condition?`, and `thread?` correctly distinguish between them:

```lisp
(define m (mutex-create))
(define cv (condition-create))
(mutex? m)        ; => #t
(mutex? cv)       ; => #f
(condition? cv)   ; => #t
(condition? m)    ; => #f
```

## C-Level API

### Headers

```c
#include "xlisp.h"
#include "xlcontext.h"
```

### Context Management

```c
xlContext *xlCreateContext(void);
int xlInitContext(xlContext *ctx, xlCallbacks *callbacks,
                 int argc, const char *argv[], const char *workspace);
void xlSetCurrentContext(xlContext *ctx);
xlContext *xlGetCurrentContext(void);
void xlDestroyContext(xlContext *ctx);
```

### Single-Thread Usage

For single-threaded use, the standard `xlInit()` works as before. In
reentrant mode it automatically creates a default context:

```c
int main(int argc, char *argv[]) {
    xlCallbacks *callbacks = xlDefaultCallbacks(argc, argv);
    if (!xlInit(callbacks, argc, argv, NULL)) {
        fprintf(stderr, "Failed to initialize XLISP\n");
        return 1;
    }
    xlCallFunctionByName(NULL, 0, "*TOPLEVEL*", 0);
    return 0;
}
```

### Multi-Thread Usage

Each thread must create and initialize its own context:

```c
#include <pthread.h>
#include "xlisp.h"

void *worker_thread(void *arg) {
    xlContext *ctx = xlCreateContext();
    xlCallbacks *callbacks = xlDefaultCallbacks(NULL);

    if (xlInitContext(ctx, callbacks, 0, NULL, NULL) != 0) {
        xlDestroyContext(ctx);
        return NULL;
    }

    xlLoadFile("worker.lsp");
    xlCallFunctionByName(NULL, 0, "DO-WORK", 0);

    xlDestroyContext(ctx);
    return NULL;
}
```

### Multiple Contexts in One Thread

A single thread can manage multiple contexts:

```c
xlContext *ctx1 = xlCreateContext();
xlContext *ctx2 = xlCreateContext();

xlInitContext(ctx1, callbacks, 0, NULL, NULL);
xlLoadFile("program1.lsp");

xlInitContext(ctx2, callbacks, 0, NULL, NULL);
xlLoadFile("program2.lsp");

xlSetCurrentContext(ctx1);
xlCallFunctionByName(NULL, 0, "FUNC1", 0);

xlSetCurrentContext(ctx2);
xlCallFunctionByName(NULL, 0, "FUNC2", 0);

xlDestroyContext(ctx1);
xlDestroyContext(ctx2);
```

## Implementation Files

| File | Role |
|------|------|
| `include/xlcontext.h` | Context structure and TLS configuration |
| `include/xlcompat.h` | Compatibility macros redirecting globals to context |
| `include/xlthread.h` | Thread-safe API declarations |
| `src/xlcontext.c` | Context creation, destruction, initialization |
| `src/xlnthread.c` | `thread-create`, `thread-join`, `thread?` |
| `src/xlsync.c` | Mutexes, condition variables, named registry |

## Limitations

1. **No data sharing.** Lisp values cannot be passed between contexts.
   Each context has its own heap; pointers are not valid across contexts.

2. **String-only communication.** Data crosses thread boundaries as C
   strings (used by `thread-create` and the named registry).

3. **Memory overhead.** Each context has a full interpreter instance.
   Memory scales linearly with context count.

4. **Initialization serialization.** Context initialization uses OS
   callbacks with static buffers that are not thread-safe. A global mutex
   serializes `xlInitContext` and `xlLoadFile` calls in child threads.

5. **Known thread-unsafe statics (low risk).** Several file-static
   variables remain shared but are low-risk:
   - `gsprefix`/`gsnumber` in `xlfun1.c` (gensym counter)
   - `buf[200]` in `xlprint.c` (number formatting)
   - `lposition` in `unstuff.c` (console output tracking)
   - `rseed` in `xlansi.c` (random number seed)

   These affect output formatting and gensym uniqueness but do not cause
   crashes.

## Platform Support

| Component | POSIX | Windows |
|-----------|-------|---------|
| TLS | `__thread` | `__declspec(thread)` |
| Thread creation | `pthread_create` | `_beginthreadex` |
| Mutex | `pthread_mutex_t` | `CRITICAL_SECTION` |
| Condition variable | `pthread_cond_t` | `CONDITION_VARIABLE` (Vista+) |
