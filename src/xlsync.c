/* xlsync.c - synchronization primitives for xlisp threads */
/*      Copyright (c) 1984-2002, by David Michael Betz
        All Rights Reserved
        See the included file 'license.txt' for the full license.
*/

#include "xlisp.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

#include <stdlib.h>
#include <string.h>

/* ====================================================================
 * Type tags for foreign pointer discrimination
 * ==================================================================== */

/*
 * We use static xlCClass structs as type tags.  Only their addresses
 * matter -- no fields are used.  This lets mutex?, condition?, channel?
 * distinguish their handles from other foreign pointers.
 */
static xlCClass mutexTag  = { NULL, NULL, NULL };
static xlCClass condTag   = { NULL, NULL, NULL };

/* ====================================================================
 * Named-object registry
 *
 * Synchronization objects can be shared across threads by name.
 * A creating thread calls (mutex-create "name"), and a child thread
 * calls (mutex-lookup "name") to obtain a handle to the same OS object.
 *
 * The registry itself is protected by its own mutex.
 * ==================================================================== */

typedef struct xlSyncEntry {
    char *name;                 /* registry name (malloc'd) */
    void *obj;                  /* pointer to the handle struct */
    xlCClass *tag;              /* &mutexTag or &condTag */
    int refCount;               /* number of live references */
    struct xlSyncEntry *next;
} xlSyncEntry;

#ifdef XLISP_USE_CONTEXT

static xlSyncEntry *registryHead = NULL;

#ifdef _WIN32
static CRITICAL_SECTION registryLock;
static int registryLockReady = 0;
static void ensureRegistryLock(void) {
    if (!registryLockReady) {
        InitializeCriticalSection(&registryLock);
        registryLockReady = 1;
    }
}
#define REG_LOCK()   do { ensureRegistryLock(); EnterCriticalSection(&registryLock); } while(0)
#define REG_UNLOCK() LeaveCriticalSection(&registryLock)
#else
static pthread_mutex_t registryLock = PTHREAD_MUTEX_INITIALIZER;
#define REG_LOCK()   pthread_mutex_lock(&registryLock)
#define REG_UNLOCK() pthread_mutex_unlock(&registryLock)
#endif

/* register a named object; returns 1 on success, 0 if name already taken */
static int registryAdd(const char *name, void *obj, xlCClass *tag)
{
    xlSyncEntry *e;
    REG_LOCK();
    for (e = registryHead; e != NULL; e = e->next) {
        if (strcmp(e->name, name) == 0) {
            REG_UNLOCK();
            return 0; /* name already exists */
        }
    }
    e = (xlSyncEntry *)malloc(sizeof(xlSyncEntry));
    if (e == NULL) { REG_UNLOCK(); return 0; }
    e->name = (char *)malloc(strlen(name) + 1);
    if (e->name == NULL) { free(e); REG_UNLOCK(); return 0; }
    strcpy(e->name, name);
    e->obj = obj;
    e->tag = tag;
    e->refCount = 1;
    e->next = registryHead;
    registryHead = e;
    REG_UNLOCK();
    return 1;
}

/* look up a named object; increments refCount if found */
static void *registryLookup(const char *name, xlCClass *tag)
{
    xlSyncEntry *e;
    void *result = NULL;
    REG_LOCK();
    for (e = registryHead; e != NULL; e = e->next) {
        if (e->tag == tag && strcmp(e->name, name) == 0) {
            e->refCount++;
            result = e->obj;
            break;
        }
    }
    REG_UNLOCK();
    return result;
}

/* decrement refCount for an object; returns new refCount */
static int registryRelease(void *obj, xlCClass *tag)
{
    xlSyncEntry *e, **pp;
    int rc = 0;
    REG_LOCK();
    for (pp = &registryHead; *pp != NULL; pp = &(*pp)->next) {
        e = *pp;
        if (e->obj == obj && e->tag == tag) {
            e->refCount--;
            rc = e->refCount;
            if (rc <= 0) {
                *pp = e->next;
                free(e->name);
                free(e);
            }
            break;
        }
    }
    REG_UNLOCK();
    return rc;
}

#endif /* XLISP_USE_CONTEXT */


/* ====================================================================
 * Mutex implementation
 * ==================================================================== */

#ifdef XLISP_USE_CONTEXT

typedef struct xlMutexHandle {
#ifdef _WIN32
    CRITICAL_SECTION cs;
#else
    pthread_mutex_t mutex;
#endif
    int destroyed;
} xlMutexHandle;

static xlMutexHandle *allocMutex(void)
{
    xlMutexHandle *m = (xlMutexHandle *)malloc(sizeof(xlMutexHandle));
    if (m == NULL) return NULL;
    memset(m, 0, sizeof(xlMutexHandle));
#ifdef _WIN32
    InitializeCriticalSection(&m->cs);
#else
    pthread_mutex_init(&m->mutex, NULL);
#endif
    m->destroyed = 0;
    return m;
}

static void freeMutex(xlMutexHandle *m)
{
    if (m == NULL) return;
#ifdef _WIN32
    DeleteCriticalSection(&m->cs);
#else
    pthread_mutex_destroy(&m->mutex);
#endif
    free(m);
}

#endif /* XLISP_USE_CONTEXT */


/* xmutexcreate - (mutex-create [name]) => mutex handle */
xlValue xmutexcreate(void)
{
#ifdef XLISP_USE_CONTEXT
    xlValue nameArg = xlNil;
    const char *name = NULL;
    xlMutexHandle *m;
    xlValue handle;

    /* optional name argument */
    if (xlMoreArgsP()) {
        nameArg = xlGetArgString();
        name = xlGetString(nameArg);
    }
    xlLastArg();

    m = allocMutex();
    if (m == NULL)
        xlFmtError("mutex-create: out of memory");

    handle = xlMakeForeignPtr(&mutexTag, m);

    if (name != NULL) {
        if (!registryAdd(name, m, &mutexTag)) {
            freeMutex(m);
            xlSetFPtr(handle, NULL);
            xlFmtError("mutex-create: name already in use");
        }
    } else {
        /* anonymous: add to registry with empty name for refcounting */
        /* Actually, for anonymous objects we just track them without registry */
    }

    return handle;
#else
    if (xlMoreArgsP()) xlGetArgString();
    xlLastArg();
    xlFmtError("mutex-create: requires threaded build (THREADS=1)");
    return xlNil;
#endif
}

/* xmutexlock - (mutex-lock mutex) => #t */
xlValue xmutexlock(void)
{
#ifdef XLISP_USE_CONTEXT
    xlValue arg;
    xlMutexHandle *m;

    arg = xlGetArgForeignPtr();
    xlLastArg();

    if (xlGetFPType(arg) != &mutexTag)
        xlFmtError("mutex-lock: not a mutex");
    m = (xlMutexHandle *)xlGetFPtr(arg);
    if (m == NULL || m->destroyed)
        xlFmtError("mutex-lock: mutex has been destroyed");

#ifdef _WIN32
    EnterCriticalSection(&m->cs);
#else
    pthread_mutex_lock(&m->mutex);
#endif
    return xlTrue;
#else
    xlGetArgForeignPtr();
    xlLastArg();
    xlFmtError("mutex-lock: requires threaded build (THREADS=1)");
    return xlNil;
#endif
}

/* xmutexunlock - (mutex-unlock mutex) => #t */
xlValue xmutexunlock(void)
{
#ifdef XLISP_USE_CONTEXT
    xlValue arg;
    xlMutexHandle *m;

    arg = xlGetArgForeignPtr();
    xlLastArg();

    if (xlGetFPType(arg) != &mutexTag)
        xlFmtError("mutex-unlock: not a mutex");
    m = (xlMutexHandle *)xlGetFPtr(arg);
    if (m == NULL || m->destroyed)
        xlFmtError("mutex-unlock: mutex has been destroyed");

#ifdef _WIN32
    LeaveCriticalSection(&m->cs);
#else
    pthread_mutex_unlock(&m->mutex);
#endif
    return xlTrue;
#else
    xlGetArgForeignPtr();
    xlLastArg();
    xlFmtError("mutex-unlock: requires threaded build (THREADS=1)");
    return xlNil;
#endif
}

/* xmutexdestroy - (mutex-destroy mutex) => #t */
xlValue xmutexdestroy(void)
{
#ifdef XLISP_USE_CONTEXT
    xlValue arg;
    xlMutexHandle *m;

    arg = xlGetArgForeignPtr();
    xlLastArg();

    if (xlGetFPType(arg) != &mutexTag)
        xlFmtError("mutex-destroy: not a mutex");
    m = (xlMutexHandle *)xlGetFPtr(arg);
    if (m == NULL || m->destroyed)
        xlFmtError("mutex-destroy: already destroyed");

    m->destroyed = 1;

    /* check refcount via registry; if not registered just free */
    if (registryRelease(m, &mutexTag) <= 0)
        freeMutex(m);

    xlSetFPtr(arg, NULL);
    return xlTrue;
#else
    xlGetArgForeignPtr();
    xlLastArg();
    xlFmtError("mutex-destroy: requires threaded build (THREADS=1)");
    return xlNil;
#endif
}

/* xmutexlookup - (mutex-lookup name) => mutex handle or #f */
xlValue xmutexlookup(void)
{
#ifdef XLISP_USE_CONTEXT
    xlValue nameArg;
    const char *name;
    xlMutexHandle *m;

    nameArg = xlGetArgString();
    xlLastArg();
    name = xlGetString(nameArg);

    m = (xlMutexHandle *)registryLookup(name, &mutexTag);
    if (m == NULL || m->destroyed)
        return xlFalse;

    return xlMakeForeignPtr(&mutexTag, m);
#else
    xlGetArgString();
    xlLastArg();
    xlFmtError("mutex-lookup: requires threaded build (THREADS=1)");
    return xlNil;
#endif
}

/* xmutexp - (mutex? obj) => #t / #f */
xlValue xmutexp(void)
{
    xlValue arg;
    arg = xlGetArg();
    xlLastArg();
    if (xlForeignPtrP(arg) && xlGetFPType(arg) == &mutexTag && xlGetFPtr(arg) != NULL)
        return xlTrue;
    return xlFalse;
}


/* ====================================================================
 * Condition variable implementation
 * ==================================================================== */

#ifdef XLISP_USE_CONTEXT

typedef struct xlCondHandle {
#ifdef _WIN32
    CONDITION_VARIABLE cond;
#else
    pthread_cond_t cond;
#endif
    int destroyed;
} xlCondHandle;

static xlCondHandle *allocCond(void)
{
    xlCondHandle *c = (xlCondHandle *)malloc(sizeof(xlCondHandle));
    if (c == NULL) return NULL;
    memset(c, 0, sizeof(xlCondHandle));
#ifdef _WIN32
    InitializeConditionVariable(&c->cond);
#else
    pthread_cond_init(&c->cond, NULL);
#endif
    c->destroyed = 0;
    return c;
}

static void freeCond(xlCondHandle *c)
{
    if (c == NULL) return;
#ifdef _WIN32
    /* Windows CONDITION_VARIABLE doesn't need explicit destruction */
#else
    pthread_cond_destroy(&c->cond);
#endif
    free(c);
}

#endif /* XLISP_USE_CONTEXT */


/* xcondcreate - (condition-create [name]) => condition handle */
xlValue xcondcreate(void)
{
#ifdef XLISP_USE_CONTEXT
    xlValue nameArg = xlNil;
    const char *name = NULL;
    xlCondHandle *c;
    xlValue handle;

    if (xlMoreArgsP()) {
        nameArg = xlGetArgString();
        name = xlGetString(nameArg);
    }
    xlLastArg();

    c = allocCond();
    if (c == NULL)
        xlFmtError("condition-create: out of memory");

    handle = xlMakeForeignPtr(&condTag, c);

    if (name != NULL) {
        if (!registryAdd(name, c, &condTag)) {
            freeCond(c);
            xlSetFPtr(handle, NULL);
            xlFmtError("condition-create: name already in use");
        }
    }

    return handle;
#else
    if (xlMoreArgsP()) xlGetArgString();
    xlLastArg();
    xlFmtError("condition-create: requires threaded build (THREADS=1)");
    return xlNil;
#endif
}

/* xcondwait - (condition-wait cond mutex) => #t */
xlValue xcondwait(void)
{
#ifdef XLISP_USE_CONTEXT
    xlValue condArg, mutexArg;
    xlCondHandle *c;
    xlMutexHandle *m;

    condArg = xlGetArgForeignPtr();
    mutexArg = xlGetArgForeignPtr();
    xlLastArg();

    if (xlGetFPType(condArg) != &condTag)
        xlFmtError("condition-wait: first argument is not a condition variable");
    if (xlGetFPType(mutexArg) != &mutexTag)
        xlFmtError("condition-wait: second argument is not a mutex");

    c = (xlCondHandle *)xlGetFPtr(condArg);
    m = (xlMutexHandle *)xlGetFPtr(mutexArg);

    if (c == NULL || c->destroyed)
        xlFmtError("condition-wait: condition has been destroyed");
    if (m == NULL || m->destroyed)
        xlFmtError("condition-wait: mutex has been destroyed");

#ifdef _WIN32
    SleepConditionVariableCS(&c->cond, &m->cs, INFINITE);
#else
    pthread_cond_wait(&c->cond, &m->mutex);
#endif
    return xlTrue;
#else
    xlGetArgForeignPtr();
    xlGetArgForeignPtr();
    xlLastArg();
    xlFmtError("condition-wait: requires threaded build (THREADS=1)");
    return xlNil;
#endif
}

/* xcondsignal - (condition-signal cond) => #t */
xlValue xcondsignal(void)
{
#ifdef XLISP_USE_CONTEXT
    xlValue arg;
    xlCondHandle *c;

    arg = xlGetArgForeignPtr();
    xlLastArg();

    if (xlGetFPType(arg) != &condTag)
        xlFmtError("condition-signal: not a condition variable");
    c = (xlCondHandle *)xlGetFPtr(arg);
    if (c == NULL || c->destroyed)
        xlFmtError("condition-signal: condition has been destroyed");

#ifdef _WIN32
    WakeConditionVariable(&c->cond);
#else
    pthread_cond_signal(&c->cond);
#endif
    return xlTrue;
#else
    xlGetArgForeignPtr();
    xlLastArg();
    xlFmtError("condition-signal: requires threaded build (THREADS=1)");
    return xlNil;
#endif
}

/* xcondbroadcast - (condition-broadcast cond) => #t */
xlValue xcondbroadcast(void)
{
#ifdef XLISP_USE_CONTEXT
    xlValue arg;
    xlCondHandle *c;

    arg = xlGetArgForeignPtr();
    xlLastArg();

    if (xlGetFPType(arg) != &condTag)
        xlFmtError("condition-broadcast: not a condition variable");
    c = (xlCondHandle *)xlGetFPtr(arg);
    if (c == NULL || c->destroyed)
        xlFmtError("condition-broadcast: condition has been destroyed");

#ifdef _WIN32
    WakeAllConditionVariable(&c->cond);
#else
    pthread_cond_broadcast(&c->cond);
#endif
    return xlTrue;
#else
    xlGetArgForeignPtr();
    xlLastArg();
    xlFmtError("condition-broadcast: requires threaded build (THREADS=1)");
    return xlNil;
#endif
}

/* xconddestroy - (condition-destroy cond) => #t */
xlValue xconddestroy(void)
{
#ifdef XLISP_USE_CONTEXT
    xlValue arg;
    xlCondHandle *c;

    arg = xlGetArgForeignPtr();
    xlLastArg();

    if (xlGetFPType(arg) != &condTag)
        xlFmtError("condition-destroy: not a condition variable");
    c = (xlCondHandle *)xlGetFPtr(arg);
    if (c == NULL || c->destroyed)
        xlFmtError("condition-destroy: already destroyed");

    c->destroyed = 1;

    if (registryRelease(c, &condTag) <= 0)
        freeCond(c);

    xlSetFPtr(arg, NULL);
    return xlTrue;
#else
    xlGetArgForeignPtr();
    xlLastArg();
    xlFmtError("condition-destroy: requires threaded build (THREADS=1)");
    return xlNil;
#endif
}

/* xcondlookup - (condition-lookup name) => condition handle or #f */
xlValue xcondlookup(void)
{
#ifdef XLISP_USE_CONTEXT
    xlValue nameArg;
    const char *name;
    xlCondHandle *c;

    nameArg = xlGetArgString();
    xlLastArg();
    name = xlGetString(nameArg);

    c = (xlCondHandle *)registryLookup(name, &condTag);
    if (c == NULL || c->destroyed)
        return xlFalse;

    return xlMakeForeignPtr(&condTag, c);
#else
    xlGetArgString();
    xlLastArg();
    xlFmtError("condition-lookup: requires threaded build (THREADS=1)");
    return xlNil;
#endif
}

/* xcondp - (condition? obj) => #t / #f */
xlValue xcondp(void)
{
    xlValue arg;
    arg = xlGetArg();
    xlLastArg();
    if (xlForeignPtrP(arg) && xlGetFPType(arg) == &condTag && xlGetFPtr(arg) != NULL)
        return xlTrue;
    return xlFalse;
}
