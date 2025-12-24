# XLISP Threading Support Design

## Overview

This document describes the design for adding multi-threading support to XLISP using **thread-local interpreter instances**. Each thread gets its own complete interpreter state with no sharing of Lisp data between threads.

## Architecture

### Current Problem: Global State

The current implementation uses extensive global variables:

```c
// VM Registers (xldmem.c)
xlValue xlFun;          // current function
xlValue xlEnv;          // current environment
xlValue xlVal;          // value of most recent instruction
xlValue *xlSP;          // value stack pointer
xlValue *xlCSP;         // control stack pointer
int xlArgC;             // argument count

// Stack (xldmem.c)
xlValue *xlStkBase;     // stack base
xlValue *xlStkTop;      // stack top

// Memory Management (xldmem.c)
xlNodeSegment *xlNSegments;
xlVectorSegment *xlVSegments;
xlValue *xlVFree, *xlVTop;
xlValue xlFNodes;
xlFIXTYPE xlNFree, xlNNodes, xlTotal, xlGCCalls;
xlProtectedPtrBlk *xlPPointers;

// Interpreter State (xlint.c)
xlErrorTarget *xlerrtarget;
xlValue *xlcatch;
int xlTraceBytecodes;
void (*xlNext)(void);
static unsigned char *base, *pc;  // bytecode pointers

// Important Values
xlValue xlTrue, xlFalse, xlPackages;
xlValue xlUnboundObject, xlDefaultObject, xlEofObject;

// Symbols (xlinit.c) - ~30+ cached symbols
xlValue s_quote, s_function, s_package, ...

// I/O
FILE *xlTranscriptFP;
xlCallbacks *callbacks;
```

### Solution: Interpreter Context Structure

All global state is encapsulated into a single context structure:

```c
/* include/xlcontext.h */

#ifndef __XLCONTEXT_H__
#define __XLCONTEXT_H__

#include "xlisp.h"

/* Interpreter context - contains all per-thread state */
typedef struct xlContext {

    /* === VM Registers === */
    xlValue fun;            /* current function */
    xlValue env;            /* current environment */
    xlValue val;            /* value of most recent instruction */
    int argc;               /* argument count */
    void (*next)(void);     /* next function to call */

    /* === Stacks === */
    xlValue *sp;            /* value stack pointer */
    xlValue *csp;           /* control stack pointer */
    xlValue *stkBase;       /* stack base */
    xlValue *stkTop;        /* stack top */

    /* === Bytecode Interpreter === */
    unsigned char *pc;      /* program counter */
    unsigned char *pcBase;  /* code base pointer */
    xlErrorTarget *errTarget;
    xlValue *catchFrame;
    int traceBytecodes;
    int sample;             /* control char sample counter */

    /* === Memory: Node Space === */
    xlNodeSegment *nSegments;
    xlNodeSegment *nsLast;
    xlValue fNodes;         /* free node list */
    xlFIXTYPE nsSize;
    xlFIXTYPE nNodes;
    xlFIXTYPE nFree;
    int nsCount;

    /* === Memory: Vector Space === */
    xlVectorSegment *vSegments;
    xlVectorSegment *vsCurrent;
    xlValue *vFree;
    xlValue *vTop;
    xlFIXTYPE vsSize;
    int vsCount;

    /* === Memory: Protected Pointers === */
    xlProtectedPtrBlk *pPointers;

    /* === Memory: Statistics === */
    xlFIXTYPE total;
    xlFIXTYPE gcCalls;

    /* === Important Values === */
    xlValue vTrue;
    xlValue vFalse;
    xlValue packages;
    xlValue unboundObject;
    xlValue defaultObject;
    xlValue eofObject;

    /* === Cached Symbols === */
    struct {
        xlValue quote, function, quasiquote, unquote, unquoteSplicing;
        xlValue dot, package, eval, load;
        xlValue print, printCase, eql;
        xlValue stdin_, stdout_, stderr_;
        xlValue stackPointer, error;
        xlValue fixfmt, hexfmt, flofmt, freeptr, backtrace;
        /* Lambda list keywords */
        xlValue lk_optional, lk_rest, lk_key, lk_aux, lk_allow_other_keys;
        xlValue slk_optional, slk_rest;
        /* Keyword symbols */
        xlValue k_upcase, k_downcase;
        xlValue k_internal, k_external, k_inherited;
        xlValue k_key, k_uses, k_test, k_testnot;
        xlValue k_start, k_end, k_1start, k_1end, k_2start, k_2end;
        xlValue k_count, k_fromend;
    } sym;

    /* === Packages === */
    xlValue lispPackage;
    xlValue xlispPackage;
    xlValue keywordPackage;

    /* === Reader State === */
    xlValue symReadTable;
    xlValue symNMacro, symTMacro, symWSpace;
    xlValue symConst, symSEscape, symMEscape;

    /* === Printer State === */
    int prBreadth;
    int prDepth;

    /* === I/O === */
    FILE *transcriptFP;

    /* === Callbacks === */
    xlCallbacks *callbacks;

    /* === Initialization Flag === */
    int initialized;

    /* === Command Line === */
    int cmdLineArgC;
    const char **cmdLineArgV;

    /* === C Classes === */
    xlCClass *cClasses;

} xlContext;

/* Thread-local context access */
#if defined(_WIN32)
    #define XLISP_TLS __declspec(thread)
#elif defined(__GNUC__)
    #define XLISP_TLS __thread
#else
    /* Fall back to pthread_getspecific */
    #define XLISP_USE_PTHREAD_TLS 1
#endif

#ifndef XLISP_USE_PTHREAD_TLS
    extern XLISP_TLS xlContext *xlCurrentContext;
    #define xlCtx() xlCurrentContext
#else
    xlContext *xlCtx(void);
#endif

/* Context management API */
xlContext *xlCreateContext(void);
void xlDestroyContext(xlContext *ctx);
void xlSetCurrentContext(xlContext *ctx);
xlContext *xlGetCurrentContext(void);

/* Initialize a context */
int xlInitContext(xlContext *ctx, xlCallbacks *callbacks,
                  int argc, const char *argv[], const char *workspace);

#endif /* __XLCONTEXT_H__ */
```

## Compatibility Macros

To minimize code changes, provide macros that redirect old globals to context fields:

```c
/* include/xlcompat.h - Compatibility layer for threading */

#ifndef __XLCOMPAT_H__
#define __XLCOMPAT_H__

#include "xlcontext.h"

/* VM Registers */
#define xlFun       (xlCtx()->fun)
#define xlEnv       (xlCtx()->env)
#define xlVal       (xlCtx()->val)
#define xlArgC      (xlCtx()->argc)
#define xlNext      (xlCtx()->next)

/* Stacks */
#define xlSP        (xlCtx()->sp)
#define xlCSP       (xlCtx()->csp)
#define xlStkBase   (xlCtx()->stkBase)
#define xlStkTop    (xlCtx()->stkTop)

/* Memory - Node Space */
#define xlNSegments (xlCtx()->nSegments)
#define xlNSLast    (xlCtx()->nsLast)
#define xlFNodes    (xlCtx()->fNodes)
#define xlNSSize    (xlCtx()->nsSize)
#define xlNNodes    (xlCtx()->nNodes)
#define xlNFree     (xlCtx()->nFree)
#define xlNSCount   (xlCtx()->nsCount)

/* Memory - Vector Space */
#define xlVSegments (xlCtx()->vSegments)
#define xlVSCurrent (xlCtx()->vsCurrent)
#define xlVFree     (xlCtx()->vFree)
#define xlVTop      (xlCtx()->vTop)
#define xlVSSize    (xlCtx()->vsSize)
#define xlVSCount   (xlCtx()->vsCount)

/* Memory - Other */
#define xlPPointers (xlCtx()->pPointers)
#define xlTotal     (xlCtx()->total)
#define xlGCCalls   (xlCtx()->gcCalls)

/* Important Values */
#define xlTrue          (xlCtx()->vTrue)
#define xlFalse         (xlCtx()->vFalse)
#define xlPackages      (xlCtx()->packages)
#define xlUnboundObject (xlCtx()->unboundObject)
#define xlDefaultObject (xlCtx()->defaultObject)
#define xlEofObject     (xlCtx()->eofObject)

/* Packages */
#define xlLispPackage    (xlCtx()->lispPackage)
#define xlXLispPackage   (xlCtx()->xlispPackage)
#define xlKeywordPackage (xlCtx()->keywordPackage)

/* Interpreter State */
#define xlerrtarget      (xlCtx()->errTarget)
#define xlcatch          (xlCtx()->catchFrame)
#define xlTraceBytecodes (xlCtx()->traceBytecodes)

/* I/O */
#define xlTranscriptFP   (xlCtx()->transcriptFP)

/* Symbols - accessed via xlCtx()->sym.XXX */
#define s_quote           (xlCtx()->sym.quote)
#define s_function        (xlCtx()->sym.function)
#define s_package         (xlCtx()->sym.package)
/* ... etc for all cached symbols ... */

/* Printer */
#define xlPRBreadth  (xlCtx()->prBreadth)
#define xlPRDepth    (xlCtx()->prDepth)

/* Command line */
#define xlCmdLineArgC (xlCtx()->cmdLineArgC)
#define xlCmdLineArgV (xlCtx()->cmdLineArgV)

/* Initialization */
#define xlInitializedP (xlCtx()->initialized)

#endif /* __XLCOMPAT_H__ */
```

## New Public API

```c
/* include/xlthread.h - Thread-safe API */

#ifndef __XLTHREAD_H__
#define __XLTHREAD_H__

#include "xlcontext.h"

/*
 * Thread-Safe XLISP API
 *
 * Each thread must:
 * 1. Create its own context with xlCreateContext()
 * 2. Initialize it with xlInitContext()
 * 3. Set it as current with xlSetCurrentContext()
 * 4. Use standard xl* functions (they use xlCtx() internally)
 * 5. Destroy with xlDestroyContext() when done
 */

/* Create a new interpreter context */
xlEXPORT xlContext *xlCreateContext(void);

/* Destroy an interpreter context */
xlEXPORT void xlDestroyContext(xlContext *ctx);

/* Set the current thread's context */
xlEXPORT void xlSetCurrentContext(xlContext *ctx);

/* Get the current thread's context */
xlEXPORT xlContext *xlGetCurrentContext(void);

/* Initialize a context (replaces xlInit for multi-threaded use) */
xlEXPORT int xlInitContext(
    xlContext *ctx,
    xlCallbacks *callbacks,
    int argc,
    const char *argv[],
    const char *workspace
);

/* Thread-safe versions of key API functions */
/* (These explicitly take a context parameter) */

xlEXPORT int xlCallFunctionCtx(
    xlContext *ctx,
    xlValue *values, int vmax,
    xlValue fun, int argc, ...
);

xlEXPORT int xlEvaluateCtx(
    xlContext *ctx,
    xlValue *values, int vmax,
    xlValue expr
);

xlEXPORT int xlEvaluateCStringCtx(
    xlContext *ctx,
    xlValue *values, int vmax,
    const char *str
);

#endif /* __XLTHREAD_H__ */
```

## Usage Example

```c
/* example_threaded.c - Multi-threaded XLISP usage */

#include <pthread.h>
#include "xlthread.h"

void *worker_thread(void *arg) {
    int thread_id = *(int *)arg;
    xlContext *ctx;
    xlValue result;
    char expr[256];

    /* Create and initialize context for this thread */
    ctx = xlCreateContext();
    if (!ctx) {
        fprintf(stderr, "Thread %d: Failed to create context\n", thread_id);
        return NULL;
    }

    /* Initialize with default callbacks */
    if (xlInitContext(ctx, xlDefaultCallbacks(NULL), 0, NULL, NULL) != 0) {
        fprintf(stderr, "Thread %d: Failed to initialize\n", thread_id);
        xlDestroyContext(ctx);
        return NULL;
    }

    /* Set as current context for this thread */
    xlSetCurrentContext(ctx);

    /* Now we can use standard XLISP functions */
    snprintf(expr, sizeof(expr), "(+ %d 100)", thread_id);

    if (xlEvaluateCString(&result, 1, expr) == 1) {
        printf("Thread %d: %s = %ld\n",
               thread_id, expr, xlGetFixnum(result));
    }

    /* Can also use explicit context version */
    xlEvaluateCStringCtx(ctx, &result, 1, "(* 6 7)");

    /* Cleanup */
    xlDestroyContext(ctx);
    return NULL;
}

int main(void) {
    pthread_t threads[4];
    int ids[4] = {1, 2, 3, 4};

    /* Launch worker threads */
    for (int i = 0; i < 4; i++) {
        pthread_create(&threads[i], NULL, worker_thread, &ids[i]);
    }

    /* Wait for completion */
    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }

    return 0;
}
```

## Implementation Plan

### Phase 1: Create Context Structure
1. Define `xlContext` struct in new `include/xlcontext.h`
2. Add thread-local storage for current context pointer
3. Implement `xlCreateContext()`, `xlDestroyContext()`, `xlSetCurrentContext()`

### Phase 2: Add Compatibility Macros
1. Create `include/xlcompat.h` with macro redirects
2. Include xlcompat.h in xlisp.h (after xlcontext.h)
3. All existing code continues to work via macros

### Phase 3: Refactor Initialization
1. Create `xlInitContext()` that initializes a specific context
2. Modify `xlInit()` to create a default context and call `xlInitContext()`
3. Move all initialization from static to context-based

### Phase 4: Refactor Memory Management
1. Move all memory globals into context (`xldmem.c`)
2. GC now operates on context's memory segments
3. Each context has independent heap

### Phase 5: Refactor Interpreter
1. Move interpreter state into context (`xlint.c`)
2. Move bytecode state (pc, base) into context
3. Error handling uses context's error target

### Phase 6: Refactor Symbols and Packages
1. Move symbol cache into context
2. Move package list into context
3. Each context has its own symbol table

### Phase 7: Testing and Validation
1. Single-threaded regression tests
2. Multi-threaded stress tests
3. Memory leak testing with valgrind

## Files Requiring Modification

| File | Changes |
|------|---------|
| `include/xlisp.h` | Include xlcontext.h, xlcompat.h |
| `include/xlcontext.h` | **NEW** - Context structure |
| `include/xlcompat.h` | **NEW** - Compatibility macros |
| `include/xlthread.h` | **NEW** - Thread-safe API |
| `src/xldmem.c` | Remove globals, use xlCtx() |
| `src/xlint.c` | Remove globals, use xlCtx() |
| `src/xlinit.c` | Remove globals, use xlCtx() |
| `src/xlsym.c` | Remove globals, use xlCtx() |
| `src/xlmain.c` | Add context creation in xlInit() |
| `src/xlapi.c` | Add xlInitContext(), context API |
| `src/xlcom.c` | Use xlCtx() for compiler state |
| `src/xlread.c` | Use xlCtx() for reader state |
| `src/xlprint.c` | Use xlCtx() for printer state |
| `src/xlio.c` | Use xlCtx() for I/O state |
| `src/xlobj.c` | Use xlCtx() for object symbols |
| `src/xlfun1.c` | No changes (uses macros) |
| `src/xlfun2.c` | No changes (uses macros) |
| `src/xlfun3.c` | No changes (uses macros) |

## Considerations

### What This Design Does NOT Support
- Sharing Lisp objects between threads (each thread has isolated heap)
- Concurrent GC (each thread GCs independently)
- Cross-thread message passing at Lisp level

### If You Need Shared Data
For inter-thread communication, use C-level mechanisms:
- Serialize Lisp data to strings, pass via queue
- Use foreign pointers to shared C structures with your own locking
- Implement a Lisp-level channel/queue using C primitives

### Performance Notes
- Thread-local storage access is very fast (single instruction on most platforms)
- Independent heaps mean no GC coordination overhead
- Memory usage scales linearly with thread count

## Estimated Effort

| Phase | Effort |
|-------|--------|
| Phase 1: Context Structure | 1-2 days |
| Phase 2: Compatibility Macros | 1 day |
| Phase 3: Refactor Init | 2-3 days |
| Phase 4: Refactor Memory | 3-4 days |
| Phase 5: Refactor Interpreter | 2-3 days |
| Phase 6: Refactor Symbols | 2-3 days |
| Phase 7: Testing | 3-5 days |
| **Total** | **~2-3 weeks** |
