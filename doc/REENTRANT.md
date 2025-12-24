# XLISP Reentrant/Thread-Safe Mode

## Overview

XLISP can be built in a reentrant mode that allows it to be safely called from multiple threads. Each thread gets its own independent interpreter context with completely separate:

- Value and control stacks
- Heap memory (nodes and vectors)
- Symbol tables and packages
- Garbage collector state
- VM registers

**Important:** Lisp data cannot be shared between threads. Each context is a fully isolated interpreter instance.

## Building

To build XLISP with reentrant support:

```bash
make clean
make REENTRANT=1
```

This defines `XLISP_USE_CONTEXT` which enables thread-local storage for the interpreter state.

To verify the build has reentrant support:

```bash
nm lib/libxlisp.a | grep xlCreateContext
```

You should see `T xlCreateContext` in the output.

## Thread-Local Storage

The reentrant build uses thread-local storage (TLS) to maintain a per-thread interpreter context pointer. The implementation uses:

- `__thread` keyword on GCC/Clang (Linux, macOS)
- `__declspec(thread)` on MSVC (Windows)
- pthread keys as a fallback

## API

### Headers

```c
#include "xlisp.h"
#include "xlthread.h"
```

### Functions

#### xlCreateContext

```c
xlContext *xlCreateContext(void)
```

Allocates a new interpreter context. Returns NULL on failure.

The context is allocated but not initialized. You must call `xlInitContext()` before using it.

#### xlInitContext

```c
int xlInitContext(xlContext *ctx, xlCallbacks *callbacks,
                  int argc, const char *argv[], const char *workspace)
```

Initializes a context for use. Returns 0 on success, -1 on failure.

Parameters:
- `ctx` - Context created by `xlCreateContext()`
- `callbacks` - Callback structure from `xlDefaultCallbacks()`
- `argc`, `argv` - Command line arguments (can be 0, NULL)
- `workspace` - Workspace image file to restore, or NULL for fresh start

This function:
1. Sets the context as the current thread's active context
2. Initializes memory management (stack, heap)
3. Creates the standard packages and symbols
4. Optionally restores a workspace image

#### xlSetCurrentContext

```c
void xlSetCurrentContext(xlContext *ctx)
```

Sets the current thread's active context. This is called automatically by `xlInitContext()`, but can be used to switch between multiple contexts in the same thread.

#### xlGetCurrentContext

```c
xlContext *xlGetCurrentContext(void)
```

Returns the current thread's active context, or NULL if none is set.

#### xlDestroyContext

```c
void xlDestroyContext(xlContext *ctx)
```

Frees all memory associated with a context:
- Node segments
- Vector segments
- Stack
- Protected pointer blocks
- The context structure itself

Call this when a thread is finished using the interpreter.

## Usage Examples

### Single Thread (Main Program)

For single-threaded use, the standard `xlInit()` function works as before. In reentrant mode, it automatically creates and initializes a default context:

```c
#include "xlisp.h"

int main(int argc, char *argv[])
{
    xlCallbacks *callbacks = xlDefaultCallbacks(argc, argv);

    if (!xlInit(callbacks, argc, argv, NULL)) {
        fprintf(stderr, "Failed to initialize XLISP\n");
        return 1;
    }

    xlInfo("%s\n", xlBanner());
    xlCallFunctionByName(NULL, 0, "*TOPLEVEL*", 0);
    return 0;
}
```

### Multiple Threads

Each thread must create and initialize its own context:

```c
#include <pthread.h>
#include "xlisp.h"
#include "xlthread.h"

void *worker_thread(void *arg)
{
    xlContext *ctx;
    xlCallbacks *callbacks;

    /* Create a new context for this thread */
    ctx = xlCreateContext();
    if (ctx == NULL) {
        fprintf(stderr, "Failed to create context\n");
        return NULL;
    }

    /* Initialize the context */
    callbacks = xlDefaultCallbacks(0, NULL);
    if (xlInitContext(ctx, callbacks, 0, NULL, NULL) != 0) {
        fprintf(stderr, "Failed to initialize context\n");
        xlDestroyContext(ctx);
        return NULL;
    }

    /* Now use the interpreter */
    xlLoadFile("worker.lsp");
    xlCallFunctionByName(NULL, 0, "DO-WORK", 0);

    /* Clean up */
    xlDestroyContext(ctx);
    return NULL;
}

int main(int argc, char *argv[])
{
    pthread_t threads[4];
    int i;

    for (i = 0; i < 4; i++)
        pthread_create(&threads[i], NULL, worker_thread, NULL);

    for (i = 0; i < 4; i++)
        pthread_join(threads[i], NULL);

    return 0;
}
```

### Multiple Contexts in One Thread

A single thread can manage multiple contexts by switching between them:

```c
#include "xlisp.h"
#include "xlthread.h"

int main(void)
{
    xlContext *ctx1, *ctx2;
    xlCallbacks *callbacks = xlDefaultCallbacks(0, NULL);

    /* Create two contexts */
    ctx1 = xlCreateContext();
    ctx2 = xlCreateContext();

    /* Initialize first context */
    xlInitContext(ctx1, callbacks, 0, NULL, NULL);
    xlLoadFile("program1.lsp");

    /* Initialize second context */
    xlInitContext(ctx2, callbacks, 0, NULL, NULL);
    xlLoadFile("program2.lsp");

    /* Switch between them */
    xlSetCurrentContext(ctx1);
    xlCallFunctionByName(NULL, 0, "FUNC1", 0);

    xlSetCurrentContext(ctx2);
    xlCallFunctionByName(NULL, 0, "FUNC2", 0);

    /* Clean up */
    xlDestroyContext(ctx1);
    xlDestroyContext(ctx2);

    return 0;
}
```

## Limitations

1. **No data sharing:** Lisp values cannot be passed between contexts. Each context has its own heap, so pointers are not valid across contexts.

2. **No cross-thread calls:** You cannot call a function in one context from another thread. Each thread must use its own context.

3. **Callbacks:** The callback structure can be shared between contexts (it contains function pointers, not Lisp data), but be careful with any state in your callback implementations.

4. **Memory overhead:** Each context has its own complete interpreter state, including separate heaps. Memory usage scales linearly with the number of contexts.

5. **Initialization time:** Creating and initializing a context takes time (building symbol tables, etc.). For best performance, create contexts once and reuse them.

## Implementation Details

The reentrant mode works by:

1. Moving all global variables into an `xlContext` structure
2. Using a thread-local pointer (`xl_current_context`) to the current context
3. Providing compatibility macros that redirect global variable access through the context pointer

For example, the global `xlVal` becomes:
```c
#define xlVal (xlCtx()->val)
```

Where `xlCtx()` returns the current thread's context pointer.

The context structure is defined in `include/xlcontext.h` and contains all interpreter state including VM registers, stack pointers, memory management state, symbol caches, and package pointers.
