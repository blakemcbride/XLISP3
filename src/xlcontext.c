/* xlcontext.c - xlisp interpreter context management */
/*      Copyright (c) 1984-2002, by David Michael Betz
        All Rights Reserved
        See the included file 'license.txt' for the full license.
*/

/*
 * This file must be compiled WITHOUT the compatibility macros,
 * since it implements the actual context management.
 */
#define XLISP_CONTEXT_IMPL 1

#include "xlisp.h"
#include "xlcontext.h"

#include <string.h>
#include <setjmp.h>

/* ====================================================================
 * Thread-Local Storage
 * ==================================================================== */

#ifdef XLISP_TLS_NATIVE
/* Native thread-local storage */
XLISP_TLS xlContext *xl_current_context = NULL;

#else
/* Pthread-based thread-local storage */
#include <pthread.h>

static pthread_key_t xl_context_key;
static pthread_once_t xl_context_key_once = PTHREAD_ONCE_INIT;

static void xl_create_key(void) {
    pthread_key_create(&xl_context_key, NULL);
}

xlEXPORT xlContext *xlGetCurrentContext(void) {
    pthread_once(&xl_context_key_once, xl_create_key);
    return (xlContext *)pthread_getspecific(xl_context_key);
}

static void xl_set_context_pthread(xlContext *ctx) {
    pthread_once(&xl_context_key_once, xl_create_key);
    pthread_setspecific(xl_context_key, ctx);
}
#endif


/* ====================================================================
 * Context Creation and Destruction
 * ==================================================================== */

/*
 * xlCreateContext - Allocate a new interpreter context
 */
xlEXPORT xlContext *xlCreateContext(void) {
    xlContext *ctx;

    /* Allocate the context structure */
    ctx = (xlContext *)malloc(sizeof(xlContext));
    if (ctx == NULL)
        return NULL;

    /* Zero-initialize the entire structure */
    memset(ctx, 0, sizeof(xlContext));

    /* Set default sizes */
    ctx->nsSize = xlNSSIZE;
    ctx->vsSize = xlVSSIZE;

    /* Initialize printer limits */
    ctx->prBreadth = -1;
    ctx->prDepth = -1;

    return ctx;
}

/*
 * xlDestroyContext - Free an interpreter context
 */
xlEXPORT void xlDestroyContext(xlContext *ctx) {
    xlNodeSegment *nseg, *next_nseg;
    xlVectorSegment *vseg, *next_vseg;
    xlProtectedPtrBlk *ppb, *next_ppb;

    if (ctx == NULL)
        return;

    /* Free node segments */
    for (nseg = ctx->nSegments; nseg != NULL; nseg = next_nseg) {
        next_nseg = nseg->ns_next;
        free(nseg);
    }

    /* Free vector segments */
    for (vseg = ctx->vSegments; vseg != NULL; vseg = next_vseg) {
        next_vseg = vseg->vs_next;
        free(vseg);
    }

    /* Free protected pointer blocks */
    for (ppb = ctx->pPointers; ppb != NULL; ppb = next_ppb) {
        next_ppb = ppb->next;
        free(ppb);
    }

    /* Free the stack */
    if (ctx->stkBase != NULL)
        free(ctx->stkBase);

    /* Free the context structure itself */
    free(ctx);
}


/* ====================================================================
 * Context Selection
 * ==================================================================== */

/*
 * xlSetCurrentContext - Set the current thread's context
 */
xlEXPORT void xlSetCurrentContext(xlContext *ctx) {
#ifdef XLISP_TLS_NATIVE
    xl_current_context = ctx;
#else
    xl_set_context_pthread(ctx);
#endif
}


/* ====================================================================
 * Context Initialization
 * ==================================================================== */

/*
 * xlContextInitMemory - Initialize memory management for a context
 *
 * This allocates the stack and prepares the memory allocator.
 * It does NOT allocate node/vector segments yet - that happens on demand.
 */
void xlContextInitMemory(xlContext *ctx, xlFIXTYPE stackSize) {
    xlFIXTYPE n;

    /* Initialize basic values */
    ctx->vTrue = NULL;
    ctx->vFalse = NULL;
    ctx->unboundObject = NULL;
    ctx->defaultObject = NULL;
    ctx->eofObject = NULL;
    ctx->packages = NULL;

    /* Initialize VM registers */
    ctx->fun = NULL;
    ctx->env = NULL;
    ctx->val = NULL;
    ctx->argc = 0;
    ctx->next = NULL;

    /* Initialize statistics */
    ctx->gcCalls = 0;
    ctx->total = 0;

    /* Initialize node space */
    ctx->nSegments = NULL;
    ctx->nsLast = NULL;
    ctx->nsCount = 0;
    ctx->nNodes = 0;
    ctx->nFree = 0;
    ctx->fNodes = NULL;

    /* Initialize vector space */
    ctx->vSegments = NULL;
    ctx->vsCurrent = NULL;
    ctx->vsCount = 0;
    ctx->vFree = NULL;
    ctx->vTop = NULL;

    /* Initialize protected pointers */
    ctx->pPointers = NULL;

    /* Allocate the stack */
    n = stackSize * sizeof(xlValue);
    ctx->stkBase = (xlValue *)malloc(n);
    if (ctx->stkBase == NULL) {
        /* Caller should check for initialization failure */
        return;
    }
    ctx->total += n;

    /* Initialize stack pointers */
    ctx->stkTop = ctx->stkBase + stackSize;
    ctx->sp = ctx->stkTop;      /* value stack starts at top, grows down */
    ctx->csp = ctx->stkBase;    /* control stack starts at base, grows up */
}

/*
 * xlInitContext - Initialize an interpreter context
 *
 * This is the main initialization function that sets up a context
 * for use. It must be called after xlCreateContext() and before
 * using the context.
 *
 * This performs the same initialization as xlInit(), including:
 * - Setting up callbacks
 * - Initializing memory management
 * - Creating packages and symbols (via xlInitWorkspace)
 * - Optionally restoring a workspace image
 */
xlEXPORT int xlInitContext(
    xlContext *ctx,
    xlCallbacks *callbacks,
    int argc,
    const char *argv[],
    const char *workspace
) {
    xlContext *saved_ctx;
    xlErrorTarget target;

    if (ctx == NULL)
        return -1;

    /* Save current context and set this one as active */
    saved_ctx = xlGetCurrentContext();
    xlSetCurrentContext(ctx);

    /* Store callbacks and set them via xlSetCallbacks */
    ctx->callbacks = callbacks;
    xlSetCallbacks(callbacks);

    /* Store command line */
    ctx->cmdLineArgC = argc;
    ctx->cmdLineArgV = argv;

    /* Set default segment sizes */
    ctx->nsSize = xlNSSIZE;
    ctx->vsSize = xlVSSIZE;

    /* Setup an initialization error handler */
    xlPushTarget(&target);
    if (setjmp(target.target)) {
        xlPopTarget();
        xlSetCurrentContext(saved_ctx);
        return -1;
    }

    /*
     * Initialize the workspace. This calls xlInitMemory() which
     * sets up the stack, then creates packages and symbols.
     * Since the context is now current, all the macros (xlSP, xlEnv, etc.)
     * will access this context's fields.
     */
    if (!workspace || !xlRestoreImage(workspace))
        xlInitWorkspace(xlSTACKSIZE);

    /* Done with initialization */
    xlPopTarget();

    /* Mark as initialized */
    ctx->initialized = 1;

    /* Keep this context as current (don't restore saved) */
    /* xlSetCurrentContext(saved_ctx); */

    return 0;
}


/* ====================================================================
 * Explicit Context API Functions
 *
 * These functions operate on an explicit context parameter.
 * They temporarily set the context as current, perform the operation,
 * and restore the previous context.
 *
 * NOTE: Full implementation requires the rest of the XLISP system
 * to be integrated with the context system. For Phase 1, these
 * are stub implementations.
 * ==================================================================== */

/*
 * Helper macro to wrap operations with context switching
 */
#define WITH_CONTEXT(ctx, code) do { \
    xlContext *_saved = xlGetCurrentContext(); \
    xlSetCurrentContext(ctx); \
    code; \
    xlSetCurrentContext(_saved); \
} while (0)

/*
 * xlGCCtx - Force garbage collection for a specific context
 */
xlEXPORT void xlGCCtx(xlContext *ctx) {
    WITH_CONTEXT(ctx, {
        /* xlGC() will be called here once integrated */
        /* For now, this is a placeholder */
    });
}

/*
 * The remaining *Ctx functions are stubs for Phase 1.
 * They will be fully implemented when the interpreter
 * is integrated with the context system.
 *
 * xlCallFunctionCtx
 * xlCallFunctionByNameCtx
 * xlEvaluateCtx
 * xlEvaluateCStringCtx
 * xlEvaluateStringCtx
 * xlLoadFileCtx
 * xlReadFromCStringCtx
 */

/* Stub implementations - to be completed in later phases */

xlEXPORT int xlEvaluateCStringCtx(
    xlContext *ctx,
    xlValue *values,
    int vmax,
    const char *str
) {
    int result = -1;
    WITH_CONTEXT(ctx, {
        /* result = xlEvaluateCString(values, vmax, str); */
        /* Stub: not yet integrated */
        (void)values;
        (void)vmax;
        (void)str;
    });
    return result;
}

xlEXPORT int xlEvaluateStringCtx(
    xlContext *ctx,
    xlValue *values,
    int vmax,
    const char *str,
    xlFIXTYPE len
) {
    int result = -1;
    WITH_CONTEXT(ctx, {
        /* result = xlEvaluateString(values, vmax, str, len); */
        (void)values;
        (void)vmax;
        (void)str;
        (void)len;
    });
    return result;
}

xlEXPORT int xlEvaluateCtx(
    xlContext *ctx,
    xlValue *values,
    int vmax,
    xlValue expr
) {
    int result = -1;
    WITH_CONTEXT(ctx, {
        /* result = xlEvaluate(values, vmax, expr); */
        (void)values;
        (void)vmax;
        (void)expr;
    });
    return result;
}

xlEXPORT int xlLoadFileCtx(
    xlContext *ctx,
    const char *fname
) {
    int result = -1;
    WITH_CONTEXT(ctx, {
        /* result = xlLoadFile(fname); */
        (void)fname;
    });
    return result;
}

xlEXPORT int xlReadFromCStringCtx(
    xlContext *ctx,
    const char *str,
    xlValue *pval
) {
    int result = -1;
    WITH_CONTEXT(ctx, {
        /* result = xlReadFromCString(str, pval); */
        (void)str;
        (void)pval;
    });
    return result;
}

/*
 * Variadic functions require special handling.
 * These will be implemented using va_list versions in later phases.
 */

xlEXPORT int xlCallFunctionCtx(
    xlContext *ctx,
    xlValue *values,
    int vmax,
    xlValue fun,
    int argc,
    ...
) {
    (void)ctx;
    (void)values;
    (void)vmax;
    (void)fun;
    (void)argc;
    /* Stub - requires va_list integration */
    return -1;
}

xlEXPORT int xlCallFunctionByNameCtx(
    xlContext *ctx,
    xlValue *values,
    int vmax,
    const char *fname,
    int argc,
    ...
) {
    (void)ctx;
    (void)values;
    (void)vmax;
    (void)fname;
    (void)argc;
    /* Stub - requires va_list integration */
    return -1;
}
