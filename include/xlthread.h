/* xlthread.h - thread-safe XLISP API */
/*      Copyright (c) 1984-2002, by David Michael Betz
        All Rights Reserved
        See the included file 'license.txt' for the full license.
*/

#ifndef __XLTHREAD_H__
#define __XLTHREAD_H__

#include "xlcontext.h"

/*
 * Thread-Safe XLISP API
 *
 * This header provides the public API for using XLISP in a multi-threaded
 * application. Each thread that needs to use XLISP must have its own
 * interpreter context.
 *
 * Basic usage pattern:
 *
 *   void *worker_thread(void *arg) {
 *       xlContext *ctx;
 *       xlValue result;
 *
 *       // Create and initialize context for this thread
 *       ctx = xlCreateContext();
 *       if (!ctx) return NULL;
 *
 *       if (xlInitContext(ctx, xlDefaultCallbacks(NULL), 0, NULL, NULL)) {
 *           xlDestroyContext(ctx);
 *           return NULL;
 *       }
 *
 *       // Set as current context for this thread
 *       xlSetCurrentContext(ctx);
 *
 *       // Now standard XLISP API can be used
 *       xlEvaluateCString(&result, 1, "(+ 1 2)");
 *
 *       // Or use explicit context versions
 *       xlEvaluateCStringCtx(ctx, &result, 1, "(* 6 7)");
 *
 *       // Cleanup when done
 *       xlDestroyContext(ctx);
 *       return NULL;
 *   }
 *
 * IMPORTANT NOTES:
 *
 * 1. Each thread MUST have its own context. Contexts are NOT thread-safe
 *    and must not be shared between threads.
 *
 * 2. Lisp objects (xlValue) belong to their context's heap. Do NOT pass
 *    xlValue objects between threads - they will become invalid or cause
 *    memory corruption.
 *
 * 3. For inter-thread communication, serialize Lisp data to strings or
 *    use C-level data structures with your own synchronization.
 *
 * 4. Each context has its own garbage collector. GC in one thread does
 *    not affect other threads.
 */


/* ====================================================================
 * Context Management (re-exported from xlcontext.h)
 * ==================================================================== */

/* These are declared in xlcontext.h but re-listed here for convenience */

/*
 * xlCreateContext - Create a new interpreter context
 *
 * Allocates and returns a new context structure. The context is not
 * initialized; you must call xlInitContext() before use.
 *
 * Returns NULL on allocation failure.
 */
/* xlEXPORT xlContext *xlCreateContext(void); */

/*
 * xlDestroyContext - Destroy an interpreter context
 *
 * Frees all memory associated with the context. The context must not
 * be in use when destroyed. If this is the current context, you should
 * call xlSetCurrentContext(NULL) first.
 */
/* xlEXPORT void xlDestroyContext(xlContext *ctx); */

/*
 * xlSetCurrentContext - Set the current thread's context
 *
 * Sets the context that will be used by all standard XLISP API calls
 * in the current thread. Pass NULL to clear the current context.
 */
/* xlEXPORT void xlSetCurrentContext(xlContext *ctx); */

/*
 * xlGetCurrentContext - Get the current thread's context
 *
 * Returns the context set by xlSetCurrentContext(), or NULL if none.
 */
/* xlEXPORT xlContext *xlGetCurrentContext(void); */

/*
 * xlInitContext - Initialize an interpreter context
 *
 * Initializes a context for use. This sets up memory management,
 * creates the initial packages and symbols, and optionally loads
 * a workspace image.
 *
 * Parameters:
 *   ctx       - Context created by xlCreateContext()
 *   callbacks - Application callbacks, or NULL for defaults
 *   argc      - Command line argument count
 *   argv      - Command line arguments
 *   workspace - Workspace file to load, or NULL
 *
 * Returns 0 on success, non-zero on failure.
 */
/* xlEXPORT int xlInitContext(xlContext *ctx, xlCallbacks *callbacks,
                              int argc, const char *argv[],
                              const char *workspace); */


/* ====================================================================
 * Thread-Safe API Functions (Explicit Context Parameter)
 *
 * These functions take an explicit context parameter instead of using
 * the thread-local current context. They are useful when you need to
 * operate on a context that is not the current thread's context.
 * ==================================================================== */

/* Forward declaration of xlValue - full definition in xlisp.h */
#ifndef __XLISP_H__
typedef struct xlNode *xlValue;
#endif

/*
 * xlCallFunctionCtx - Call a Lisp function with explicit context
 *
 * Same as xlCallFunction() but uses the specified context.
 */
xlEXPORT int xlCallFunctionCtx(
    xlContext *ctx,
    xlValue *values,
    int vmax,
    xlValue fun,
    int argc,
    ...
);

/*
 * xlCallFunctionByNameCtx - Call a named function with explicit context
 *
 * Same as xlCallFunctionByName() but uses the specified context.
 */
xlEXPORT int xlCallFunctionByNameCtx(
    xlContext *ctx,
    xlValue *values,
    int vmax,
    const char *fname,
    int argc,
    ...
);

/*
 * xlEvaluateCtx - Evaluate an expression with explicit context
 *
 * Same as xlEvaluate() but uses the specified context.
 */
xlEXPORT int xlEvaluateCtx(
    xlContext *ctx,
    xlValue *values,
    int vmax,
    xlValue expr
);

/*
 * xlEvaluateCStringCtx - Evaluate a C string with explicit context
 *
 * Same as xlEvaluateCString() but uses the specified context.
 */
xlEXPORT int xlEvaluateCStringCtx(
    xlContext *ctx,
    xlValue *values,
    int vmax,
    const char *str
);

/*
 * xlEvaluateStringCtx - Evaluate a string with explicit context
 *
 * Same as xlEvaluateString() but uses the specified context.
 */
xlEXPORT int xlEvaluateStringCtx(
    xlContext *ctx,
    xlValue *values,
    int vmax,
    const char *str,
    xlFIXTYPE len
);

/*
 * xlLoadFileCtx - Load a file with explicit context
 *
 * Same as xlLoadFile() but uses the specified context.
 */
xlEXPORT int xlLoadFileCtx(
    xlContext *ctx,
    const char *fname
);

/*
 * xlReadFromCStringCtx - Read from a C string with explicit context
 *
 * Same as xlReadFromCString() but uses the specified context.
 */
xlEXPORT int xlReadFromCStringCtx(
    xlContext *ctx,
    const char *str,
    xlValue *pval
);

/*
 * xlGCCtx - Force garbage collection with explicit context
 *
 * Triggers garbage collection for the specified context.
 */
xlEXPORT void xlGCCtx(xlContext *ctx);


/* ====================================================================
 * Utility Functions
 * ==================================================================== */

/*
 * xlContextIsInitialized - Check if a context is initialized
 *
 * Returns non-zero if the context has been successfully initialized.
 */
#define xlContextIsInitialized(ctx) ((ctx)->initialized)

/*
 * xlContextMemoryUsage - Get memory usage for a context
 *
 * Returns the total bytes of memory allocated by the context.
 */
#define xlContextMemoryUsage(ctx) ((ctx)->total)

/*
 * xlContextGCCount - Get GC count for a context
 *
 * Returns the number of garbage collections performed by the context.
 */
#define xlContextGCCount(ctx) ((ctx)->gcCalls)


/* ====================================================================
 * Thread Safety Utilities
 * ==================================================================== */

/*
 * xlWithContext - Execute code with a specific context
 *
 * This macro temporarily sets a context as current, executes the
 * given code block, and restores the previous context.
 *
 * Usage:
 *   xlWithContext(ctx) {
 *       // code using ctx as current context
 *   }
 */
#define xlWithContext(ctx) \
    for (xlContext *_xl_saved_ctx = xlGetCurrentContext(), \
                   *_xl_once = (xlSetCurrentContext(ctx), (xlContext*)1); \
         _xl_once; \
         xlSetCurrentContext(_xl_saved_ctx), _xl_once = NULL)

#endif /* __XLTHREAD_H__ */
