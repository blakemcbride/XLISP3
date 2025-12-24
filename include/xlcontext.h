/* xlcontext.h - xlisp interpreter context for multi-threading support */
/*      Copyright (c) 1984-2002, by David Michael Betz
        All Rights Reserved
        See the included file 'license.txt' for the full license.
*/

#ifndef __XLCONTEXT_H__
#define __XLCONTEXT_H__

#include <stdio.h>  /* for FILE* */

/*
 * Forward declarations - only if xlisp.h hasn't been included yet.
 * If xlisp.h is included first, these types are already defined.
 */
#ifndef __XLISP_H__
typedef struct xlNode xlNode, *xlValue;
typedef struct xlNodeSegment xlNodeSegment;
typedef struct xlVectorSegment xlVectorSegment;
typedef struct xlProtectedPtrBlk xlProtectedPtrBlk;
typedef struct xlErrorTarget xlErrorTarget;
typedef struct xlCClass xlCClass;
typedef struct xlCallbacks xlCallbacks;
#endif

/* Type definitions matching xlisp.h defaults */
#ifndef xlFIXTYPE
#define xlFIXTYPE long
#endif

#ifndef xlOFFTYPE
#define xlOFFTYPE long
#endif

/*
 * xlContext - Per-thread interpreter state
 *
 * This structure contains all state that was previously stored in global
 * variables. Each thread that uses XLISP must have its own context.
 *
 * Usage:
 *   xlContext *ctx = xlCreateContext();
 *   xlInitContext(ctx, callbacks, argc, argv, workspace);
 *   xlSetCurrentContext(ctx);
 *   // ... use XLISP API ...
 *   xlDestroyContext(ctx);
 */
typedef struct xlContext {

    /* ================================================================
     * VM Registers
     * ================================================================ */
    xlValue fun;                /* current function being executed */
    xlValue env;                /* current lexical environment */
    xlValue val;                /* value of most recent instruction */
    int argc;                   /* argument count for current call */
    void (*next)(void);         /* next function to call (xlApply or NULL) */

    /* ================================================================
     * Value and Control Stacks
     * ================================================================ */
    xlValue *sp;                /* value stack pointer (grows down) */
    xlValue *csp;               /* control stack pointer (grows up) */
    xlValue *stkBase;           /* base of stack allocation */
    xlValue *stkTop;            /* top of stack allocation */

    /* ================================================================
     * Bytecode Interpreter State
     * ================================================================ */
    unsigned char *pc;          /* program counter */
    unsigned char *pcBase;      /* base of current code object */
    xlErrorTarget *errTarget;   /* error/abort target chain */
    xlValue *catchFrame;        /* current catch frame pointer */
    int traceBytecodes;         /* bytecode tracing enabled */
    int sample;                 /* control character sample counter */

    /* ================================================================
     * Memory Management - Node Space
     * ================================================================ */
    xlNodeSegment *nSegments;   /* list of node segments */
    xlNodeSegment *nsLast;      /* last node segment (for appending) */
    xlValue fNodes;             /* head of free node list */
    xlFIXTYPE nsSize;           /* default nodes per segment */
    xlFIXTYPE nNodes;           /* total number of nodes allocated */
    xlFIXTYPE nFree;            /* number of nodes in free list */
    int nsCount;                /* number of node segments */

    /* ================================================================
     * Memory Management - Vector Space
     * ================================================================ */
    xlVectorSegment *vSegments; /* list of vector segments */
    xlVectorSegment *vsCurrent; /* current vector segment */
    xlValue *vFree;             /* next free location in vector space */
    xlValue *vTop;              /* top of current vector segment */
    xlFIXTYPE vsSize;           /* default size of vector segments */
    int vsCount;                /* number of vector segments */

    /* ================================================================
     * Memory Management - Protected Pointers
     * ================================================================ */
    xlProtectedPtrBlk *pPointers;   /* protected pointer blocks */

    /* ================================================================
     * Memory Management - Statistics
     * ================================================================ */
    xlFIXTYPE total;            /* total bytes of memory in use */
    xlFIXTYPE gcCalls;          /* number of GC invocations */

    /* ================================================================
     * Important Singleton Values
     * ================================================================ */
    xlValue vTrue;              /* #t */
    xlValue vFalse;             /* #f */
    xlValue unboundObject;      /* marker for unbound variables */
    xlValue defaultObject;      /* default object for methods */
    xlValue eofObject;          /* end-of-file object */

    /* ================================================================
     * Package System
     * ================================================================ */
    xlValue packages;           /* list of all packages */
    xlValue lispPackage;        /* the LISP package */
    xlValue xlispPackage;       /* the XLISP package */
    xlValue keywordPackage;     /* the KEYWORD package */

    /* ================================================================
     * Cached Symbols - Frequently Used
     * ================================================================ */
    struct {
        /* Special forms and core */
        xlValue quote;
        xlValue function;
        xlValue quasiquote;
        xlValue unquote;
        xlValue unquoteSplicing;
        xlValue dot;

        /* System symbols */
        xlValue package;
        xlValue eval;
        xlValue load;
        xlValue print;
        xlValue printCase;
        xlValue eql;
        xlValue error;
        xlValue stackPointer;
        xlValue backtrace;
        xlValue unassigned;

        /* Standard streams */
        xlValue stdin_;
        xlValue stdout_;
        xlValue stderr_;

        /* Format strings */
        xlValue fixfmt;
        xlValue hexfmt;
        xlValue flofmt;
        xlValue freeptr;

        /* Lambda list keywords */
        xlValue lk_optional;
        xlValue lk_rest;
        xlValue lk_key;
        xlValue lk_aux;
        xlValue lk_allow_other_keys;

        /* Scheme-style lambda keywords */
        xlValue slk_optional;
        xlValue slk_rest;

        /* Keyword symbols for functions */
        xlValue k_upcase;
        xlValue k_downcase;
        xlValue k_internal;
        xlValue k_external;
        xlValue k_inherited;
        xlValue k_key;
        xlValue k_uses;
        xlValue k_test;
        xlValue k_testnot;
        xlValue k_start;
        xlValue k_end;
        xlValue k_1start;
        xlValue k_1end;
        xlValue k_2start;
        xlValue k_2end;
        xlValue k_count;
        xlValue k_fromend;

    } sym;

    /* ================================================================
     * Reader State
     * ================================================================ */
    xlValue symReadTable;       /* read table symbol */
    xlValue symNMacro;          /* non-terminating macro */
    xlValue symTMacro;          /* terminating macro */
    xlValue symWSpace;          /* whitespace */
    xlValue symConst;           /* constituent */
    xlValue symSEscape;         /* single escape */
    xlValue symMEscape;         /* multiple escape */

    /* ================================================================
     * Printer State
     * ================================================================ */
    int prBreadth;              /* print breadth limit (-1 = unlimited) */
    int prDepth;                /* print depth limit (-1 = unlimited) */

    /* ================================================================
     * I/O State
     * ================================================================ */
    FILE *transcriptFP;         /* transcript file pointer */

    /* ================================================================
     * Callbacks
     * ================================================================ */
    xlCallbacks *callbacks;     /* host application callbacks */

    /* ================================================================
     * Compiler State
     * ================================================================ */
    int debugModeP;             /* true to turn off tail recursion */

    /* ================================================================
     * Initialization State
     * ================================================================ */
    int initialized;            /* non-zero if fully initialized */

    /* ================================================================
     * Command Line
     * ================================================================ */
    int cmdLineArgC;            /* argument count */
    const char **cmdLineArgV;   /* argument vector */

    /* ================================================================
     * C Class Registry
     * ================================================================ */
    xlCClass *cClasses;         /* linked list of C classes */

    /* ================================================================
     * Object System
     * ================================================================ */
    xlValue c_class;            /* the Class class */
    xlValue c_object;           /* the Object class */
    xlValue k_initialize;       /* :initialize keyword */

} xlContext;


/* ====================================================================
 * Thread-Local Storage Configuration
 * ==================================================================== */

#if defined(_WIN32) || defined(_WIN64)
    /* Windows: use __declspec(thread) */
    #define XLISP_TLS __declspec(thread)
    #define XLISP_TLS_NATIVE 1
#elif defined(__GNUC__) || defined(__clang__)
    /* GCC/Clang: use __thread */
    #define XLISP_TLS __thread
    #define XLISP_TLS_NATIVE 1
#else
    /* Fallback: use pthread_getspecific */
    #define XLISP_TLS
    #define XLISP_TLS_PTHREAD 1
#endif


/* ====================================================================
 * Context Access
 * ==================================================================== */

#ifdef XLISP_TLS_NATIVE
    /* Fast path: native thread-local storage */
    extern XLISP_TLS xlContext *xl_current_context;
    #define xlCtx() xl_current_context
#else
    /* Slow path: pthread TLS */
    xlContext *xlGetCurrentContext(void);
    #define xlCtx() xlGetCurrentContext()
#endif


/* ====================================================================
 * Context Management API
 * ==================================================================== */

#ifndef xlEXPORT
#define xlEXPORT
#endif

/*
 * xlCreateContext - Allocate a new interpreter context
 *
 * Returns a newly allocated context structure, or NULL on failure.
 * The context is not initialized; call xlInitContext() before use.
 */
xlEXPORT xlContext *xlCreateContext(void);

/*
 * xlDestroyContext - Free an interpreter context
 *
 * Releases all memory associated with the context, including:
 * - Node segments
 * - Vector segments
 * - Stack space
 * - Protected pointer blocks
 *
 * The context must not be in use by any thread when destroyed.
 */
xlEXPORT void xlDestroyContext(xlContext *ctx);

/*
 * xlSetCurrentContext - Set the current thread's context
 *
 * This must be called before using any XLISP functions.
 * Each thread should have its own context.
 */
xlEXPORT void xlSetCurrentContext(xlContext *ctx);

/*
 * xlGetCurrentContext - Get the current thread's context
 *
 * Returns the context set by xlSetCurrentContext(), or NULL if none.
 */
#ifndef XLISP_TLS_NATIVE
xlEXPORT xlContext *xlGetCurrentContext(void);
#else
#define xlGetCurrentContext() xl_current_context
#endif

/*
 * xlInitContext - Initialize an interpreter context
 *
 * This performs the same initialization as xlInit(), but for a
 * specific context. The context must have been created with
 * xlCreateContext().
 *
 * Parameters:
 *   ctx       - Context to initialize
 *   callbacks - Host application callbacks (or NULL for defaults)
 *   argc      - Command line argument count
 *   argv      - Command line argument vector
 *   workspace - Workspace file to load (or NULL)
 *
 * Returns 0 on success, non-zero on failure.
 */
xlEXPORT int xlInitContext(
    xlContext *ctx,
    xlCallbacks *callbacks,
    int argc,
    const char *argv[],
    const char *workspace
);

/*
 * xlContextInitMemory - Initialize memory management for a context
 *
 * Called internally by xlInitContext(). Allocates the initial
 * stack space and prepares the memory allocator.
 */
void xlContextInitMemory(xlContext *ctx, xlFIXTYPE stackSize);

/*
 * xlContextInitSymbols - Initialize symbols for a context
 *
 * Called internally by xlInitContext(). Creates the initial
 * packages and interns the standard symbols.
 */
void xlContextInitSymbols(xlContext *ctx);

#endif /* __XLCONTEXT_H__ */
