# XLISP Threading Support

## Overview

XLISP supports native multi-threading through per-thread interpreter
contexts. Each thread gets a completely independent interpreter instance
with its own stacks, heap, symbol tables, garbage collector, and VM
registers. Lisp data cannot be shared between threads directly.

Threads coordinate through synchronization primitives (mutexes,
condition variables, and message channels) that are shared at the C level
via a named registry. Data is exchanged between threads as strings.

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

### Per-Thread Garbage Collection and Memory

Each thread has its own heap (node segments and vector segments), its own
free lists, and its own garbage collector. The GC runs synchronously
within the thread — it is triggered inline when an allocation cannot be
satisfied, and there is no separate GC thread. Because heaps are not
shared, one thread's GC never touches another thread's memory, and there
are no stop-the-world pauses across threads.

#### GC Algorithm

The collector in `src/xldmem.c` uses a three-phase **mark-sweep-compact**
algorithm:

1. **Mark** — Starting from roots (VM registers `xlFun`, `xlEnv`, `xlVal`;
   the value and control stacks; the package list; and protected
   pointers), the collector walks all reachable objects. It uses a
   **pointer-reversal traversal** (Deutsch-Schorr-Waite algorithm) that
   flips car/cdr pointers as it descends, avoiding recursion and needing
   no auxiliary stack. Vector-like nodes (symbols, vectors, code objects,
   etc.) are marked by iterating their elements. Continuations receive
   special handling — their embedded value/control stacks are marked
   separately.

2. **Compact** — Live vectors are slid down within each vector segment to
   eliminate gaps left by dead vectors, and each vector node's data
   pointer is updated to its new location. Only vector space is
   compacted; node space is not.

3. **Sweep** — Every node in every segment is visited. Unmarked nodes are
   returned to the free list. Marked nodes have their mark bit cleared.
   Certain types receive special cleanup: file streams are closed, foreign
   pointers call their registered `free` callback, and subr name strings
   are freed.

#### Default Memory Sizes

All threads (including the main thread) use the same hardcoded defaults,
defined in `include/xlisp.h`:

| Constant | Default | Purpose |
|----------|---------|---------|
| `xlSTACKSIZE` | 65,536 | Value/control stack (in `xlValue` slots) |
| `xlNSSIZE` | 20,000 | Nodes per node segment |
| `xlVSSIZE` | 200,000 | `xlValue` slots per vector segment |

These are not hard caps — they are the size of **one segment**. Memory
grows on demand: when the GC runs and there still is not enough free
space, `findmemory()` allocates additional segments via `xlNExpand()` and
`xlVExpand()`. There is no upper bound other than what `malloc` can
provide.

#### No Per-Thread Size Configuration

There is currently no way to configure memory sizes on a per-thread
basis. The `thread-create` function does not accept sizing parameters,
and `xlCreateContext()` always sets `nsSize` and `vsSize` to the compiled
defaults. To customize sizes you would need to modify the constants in
`include/xlisp.h` and rebuild, which affects all threads equally.

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

### Channels

Channels are thread-safe message queues for exchanging string data
between threads. They have internal locking and condition variables,
so no separate mutex management is needed.

```lisp
(CHANNEL-CREATE [name] [capacity]) ; create a channel
(CHANNEL-SEND channel string)      ; enqueue (blocks if bounded and full)
(CHANNEL-RECEIVE channel)          ; dequeue (blocks if empty); #f when closed+empty
(CHANNEL-CLOSE channel)            ; mark closed; pending messages still receivable
(CHANNEL-DESTROY channel)          ; free resources (ref-counted)
(CHANNEL-LOOKUP name)              ; look up a named channel => handle or #f
(CHANNEL-OPEN? channel)            ; #t if not yet closed
(CHANNEL? obj)                     ; type predicate
```

The optional *capacity* argument limits the queue size. When full, a
bounded channel blocks the sender until the receiver drains a message.
If omitted or 0, the channel is unbounded.

Producer/consumer example:

```lisp
(define ch (channel-create "work"))

;; Producer thread
(define h (thread-create
  "(begin
     (define ch (channel-lookup \"work\"))
     (channel-send ch \"hello\")
     (channel-send ch \"world\")
     (channel-close ch))"
  #f))

;; Consumer (main thread)
(define (consume)
  (let ((msg (channel-receive ch)))
    (if msg
      (begin (display msg) (newline) (consume)))))
(consume)    ; prints "hello" then "world"

(thread-join h)
(channel-destroy ch)
```

Bounded channel with backpressure:

```lisp
(define ch (channel-create "bounded" 2))

(define h (thread-create
  "(begin
     (define ch (channel-lookup \"bounded\"))
     (channel-send ch \"a\")
     (channel-send ch \"b\")
     (channel-send ch \"c\")
     (channel-send ch \"d\")
     (channel-close ch))"
  #f))

;; Consumer drains messages; producer blocks when queue is full
(define (consume)
  (let ((msg (channel-receive ch)))
    (if msg (begin (display msg) (newline) (consume)))))
(consume)

(thread-join h)
(channel-destroy ch)
```

### Type Discrimination

Mutexes, condition variables, channels, and thread handles are all
foreign pointers but are tagged with distinct `xlCClass` type markers.
The predicates `mutex?`, `condition?`, `channel?`, and `thread?`
correctly distinguish between them:

```lisp
(define m (mutex-create))
(define cv (condition-create))
(define ch (channel-create))
(mutex? m)        ; => #t
(mutex? cv)       ; => #f
(condition? cv)   ; => #t
(channel? ch)     ; => #t
(channel? m)      ; => #f
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

## High-Level Utilities

The file `threads.lsp` provides convenient abstractions built on the
low-level primitives.  Load it with `(load "threads.lsp")`.

### with-mutex

Macro that locks a mutex, evaluates body forms, and guarantees unlock
even on error:

```lisp
(define m (mutex-create))
(with-mutex m
  ;; critical section
  (+ 1 2))   ; => 3
```

### future / await

Spawn a computation and retrieve the result later:

```lisp
(define f (future "(number->string (* 6 7))" #f))
;; ... do other work ...
(await f)   ; => "42"
```

### pcall

Run multiple expressions concurrently, collect results in order:

```lisp
(pcall "(number->string (* 2 3))"
       "(number->string (* 4 5))")
;; => ("6" "20")
```

### Thread Pool

Reuse a fixed set of worker threads instead of spawning one per task:

```lisp
(define pool (thread-pool-create 4 #f))
(define f1 (thread-pool-submit pool "(number->string (fib 30))"))
(define f2 (thread-pool-submit pool "(number->string (fib 31))"))
(display (await f1)) (newline)
(display (await f2)) (newline)
(thread-pool-destroy pool)
```

### pmap

Parallel map — apply a template expression to a list of values:

```lisp
(pmap "(number->string (* 2 ~a))" '("5" "10" "15"))
;; => ("10" "20" "30")
```

See `doc/xlisp.md` section 52 for the complete API reference.

## Implementation Files

| File | Role |
|------|------|
| `include/xlcontext.h` | Context structure and TLS configuration |
| `include/xlcompat.h` | Compatibility macros redirecting globals to context |
| `include/xlthread.h` | Thread-safe API declarations |
| `src/xlcontext.c` | Context creation, destruction, initialization |
| `src/xlnthread.c` | `thread-create`, `thread-join`, `thread?` |
| `src/xlsync.c` | Mutexes, condition variables, channels, named registry |
| `threads.lsp` | High-level utilities (with-mutex, future/await, pcall, thread-pool, pmap) |

## Limitations

1. **No data sharing.** Lisp values cannot be passed between contexts.
   Each context has its own heap; pointers are not valid across contexts.

2. **String-only communication.** Data crosses thread boundaries as C
   strings (used by `thread-create`, the named registry, and channels).

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
| Channel (internal) | `pthread_mutex_t` + `pthread_cond_t` | `CRITICAL_SECTION` + `CONDITION_VARIABLE` |
