/* xlnthread.c - native thread creation for xlisp */
/*      Copyright (c) 1984-2002, by David Michael Betz
        All Rights Reserved
        See the included file 'license.txt' for the full license.
*/

#include "xlisp.h"

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <pthread.h>
#endif

#include <string.h>

/* thread info structure - tracks a spawned thread */
typedef struct xlThreadInfo {
    char *expr;             /* expression to evaluate (C string copy) */
    char *initFile;         /* initialization file to load, or NULL */
    int status;             /* 0=running, 1=success, -1=error */
    int joined;             /* whether thread-join has been called */
#ifdef _WIN32
    HANDLE thread;
#else
    pthread_t thread;
    int threadValid;        /* whether pthread_t has been set */
#endif
} xlThreadInfo;

#ifdef XLISP_USE_CONTEXT

/*
 * Mutex to serialize context initialization.
 * Some underlying OS callbacks (osparsepath, etc.) use static buffers
 * that are not thread-safe. We serialize initialization and file loading
 * to avoid corruption.
 */
#ifdef _WIN32
static CRITICAL_SECTION initMutex;
static int initMutexReady = 0;
static void ensureInitMutex(void) {
    if (!initMutexReady) {
        InitializeCriticalSection(&initMutex);
        initMutexReady = 1;
    }
}
#define INIT_LOCK()   do { ensureInitMutex(); EnterCriticalSection(&initMutex); } while(0)
#define INIT_UNLOCK() LeaveCriticalSection(&initMutex)
#else
static pthread_mutex_t initMutex = PTHREAD_MUTEX_INITIALIZER;
#define INIT_LOCK()   pthread_mutex_lock(&initMutex)
#define INIT_UNLOCK() pthread_mutex_unlock(&initMutex)
#endif

/* worker thread function */
#ifdef _WIN32
static unsigned __stdcall threadWorker(void *arg)
#else
static void *threadWorker(void *arg)
#endif
{
    xlThreadInfo *info = (xlThreadInfo *)arg;
    xlContext *ctx = NULL;
    xlCallbacks *callbacks = NULL;
    xlValue result;

    /* create a new interpreter context for this thread */
    ctx = xlCreateContext();
    if (ctx == NULL) {
        info->status = -1;
#ifdef _WIN32
        return 1;
#else
        return NULL;
#endif
    }

    /*
     * Serialize initialization: xlInitContext and xlLoadFile use
     * OS callbacks with static buffers that aren't thread-safe.
     */
    INIT_LOCK();

    /* get default callbacks and initialize the context */
    callbacks = xlDefaultCallbacks(NULL);
    if (xlInitContext(ctx, callbacks, 0, NULL, NULL) != 0) {
        INIT_UNLOCK();
        xlDestroyContext(ctx);
        info->status = -1;
#ifdef _WIN32
        return 1;
#else
        return NULL;
#endif
    }

    /* load the initialization file if specified */
    if (info->initFile != NULL)
        xlLoadFile(info->initFile);

    INIT_UNLOCK();

    /* evaluate the expression */
    if (xlEvaluateCString(&result, 1, info->expr) >= 0)
        info->status = 1;
    else
        info->status = -1;

    /* clean up the context */
    xlDestroyContext(ctx);

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

#endif /* XLISP_USE_CONTEXT */

/* xthreadcreate - built-in function 'thread-create' */
/* (thread-create expr-string [init-file]) => thread-handle */
xlValue xthreadcreate(void)
{
#ifdef XLISP_USE_CONTEXT
    xlValue strval, initval;
    const char *str, *initStr;
    xlThreadInfo *info;
    xlValue handle;

    /* get the expression string argument */
    strval = xlGetArgString();

    /* get optional init file argument (default: "xlisp.lsp") */
    if (xlMoreArgsP()) {
        initval = xlGetArg();
        if (initval == xlNil || initval == xlFalse)
            initStr = NULL;  /* #f or nil means no init file */
        else if (xlStringP(initval))
            initStr = xlGetString(initval);
        else {
            xlBadType(initval);
            return xlNil; /* not reached */
        }
    }
    else {
        initStr = "xlisp.lsp";  /* default: load standard init */
    }

    xlLastArg();

    str = xlGetString(strval);

    /* allocate thread info */
    info = (xlThreadInfo *)malloc(sizeof(xlThreadInfo));
    if (info == NULL)
        xlFmtError("thread-create: out of memory");

    memset(info, 0, sizeof(xlThreadInfo));

    /* copy the expression string (it may be GC'd in the calling thread) */
    info->expr = (char *)malloc(strlen(str) + 1);
    if (info->expr == NULL) {
        free(info);
        xlFmtError("thread-create: out of memory");
    }
    strcpy(info->expr, str);

    /* copy the init file name if specified */
    if (initStr != NULL) {
        info->initFile = (char *)malloc(strlen(initStr) + 1);
        if (info->initFile == NULL) {
            free(info->expr);
            free(info);
            xlFmtError("thread-create: out of memory");
        }
        strcpy(info->initFile, initStr);
    }
    else {
        info->initFile = NULL;
    }

    info->status = 0;   /* running */
    info->joined = 0;

    /* create the thread handle as a foreign pointer */
    handle = xlMakeForeignPtr(NULL, info);

    /* create the OS thread */
#ifdef _WIN32
    info->thread = (HANDLE)_beginthreadex(NULL, 0, threadWorker, info, 0, NULL);
    if (info->thread == 0) {
        free(info->expr);
        free(info);
        xlFmtError("thread-create: failed to create thread");
    }
#else
    {
        int rc = pthread_create(&info->thread, NULL, threadWorker, info);
        if (rc != 0) {
            free(info->expr);
            free(info);
            xlFmtError("thread-create: failed to create thread");
        }
        info->threadValid = 1;
    }
#endif

    return handle;
#else
    xlGetArgString();
    if (xlMoreArgsP()) xlGetArg();
    xlLastArg();
    xlFmtError("thread-create: requires threaded build (THREADS=1)");
    return xlNil; /* not reached */
#endif
}

/* xthreadjoin - built-in function 'thread-join' */
/* (thread-join handle) => #t on success, signals error on failure */
xlValue xthreadjoin(void)
{
#ifdef XLISP_USE_CONTEXT
    xlValue arg;
    xlThreadInfo *info;

    /* get the thread handle argument */
    arg = xlGetArgForeignPtr();
    xlLastArg();

    info = (xlThreadInfo *)xlGetFPtr(arg);
    if (info == NULL)
        xlFmtError("thread-join: invalid thread handle");

    if (info->joined)
        xlFmtError("thread-join: thread already joined");

    /* wait for the thread to finish */
#ifdef _WIN32
    WaitForSingleObject(info->thread, INFINITE);
    CloseHandle(info->thread);
    info->thread = NULL;
#else
    if (info->threadValid) {
        pthread_join(info->thread, NULL);
        info->threadValid = 0;
    }
#endif

    info->joined = 1;

    /* free the expression and init file strings */
    if (info->expr != NULL) {
        free(info->expr);
        info->expr = NULL;
    }
    if (info->initFile != NULL) {
        free(info->initFile);
        info->initFile = NULL;
    }

    /* check status */
    if (info->status == 1) {
        /* clean up the info structure */
        free(info);
        xlSetFPtr(arg, NULL);
        return xlTrue;
    }
    else {
        free(info);
        xlSetFPtr(arg, NULL);
        xlFmtError("thread-join: thread terminated with error");
        return xlNil; /* not reached */
    }
#else
    xlGetArgForeignPtr();
    xlLastArg();
    xlFmtError("thread-join: requires threaded build (THREADS=1)");
    return xlNil; /* not reached */
#endif
}

/* xthreadp - built-in function 'thread?' */
/* (thread? obj) => #t if obj is a thread handle */
xlValue xthreadp(void)
{
    xlValue arg;
    arg = xlGetArg();
    xlLastArg();

    /* a thread handle is a foreign pointer with non-null data */
    if (xlForeignPtrP(arg) && xlGetFPtr(arg) != NULL)
        return xlTrue;
    return xlFalse;
}
