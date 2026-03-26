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
static xlCClass mutexTag   = { NULL, NULL, NULL };
static xlCClass condTag    = { NULL, NULL, NULL };
static xlCClass channelTag = { NULL, NULL, NULL };

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
    xlCClass *tag;              /* &mutexTag, &condTag, or &channelTag */
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


/* ====================================================================
 * Channel implementation
 *
 * A channel is a thread-safe message queue that carries C strings.
 * It has its own internal lock and condition variables for blocking
 * send (when bounded and full) and blocking receive (when empty).
 * ==================================================================== */

#ifdef XLISP_USE_CONTEXT

typedef struct xlChannelNode {
    char *data;                     /* C string (malloc'd copy) */
    struct xlChannelNode *next;
} xlChannelNode;

typedef struct xlChannelHandle {
#ifdef _WIN32
    CRITICAL_SECTION lock;
    CONDITION_VARIABLE notEmpty;
    CONDITION_VARIABLE notFull;
#else
    pthread_mutex_t lock;
    pthread_cond_t notEmpty;
    pthread_cond_t notFull;
#endif
    xlChannelNode *head;            /* dequeue from head */
    xlChannelNode *tail;            /* enqueue at tail */
    int count;                      /* current messages in queue */
    int capacity;                   /* max messages (0 = unbounded) */
    int closed;                     /* non-zero after channel-close */
    int destroyed;
} xlChannelHandle;

static xlChannelHandle *allocChannel(int capacity)
{
    xlChannelHandle *ch = (xlChannelHandle *)malloc(sizeof(xlChannelHandle));
    if (ch == NULL) return NULL;
    memset(ch, 0, sizeof(xlChannelHandle));
#ifdef _WIN32
    InitializeCriticalSection(&ch->lock);
    InitializeConditionVariable(&ch->notEmpty);
    InitializeConditionVariable(&ch->notFull);
#else
    pthread_mutex_init(&ch->lock, NULL);
    pthread_cond_init(&ch->notEmpty, NULL);
    pthread_cond_init(&ch->notFull, NULL);
#endif
    ch->head = NULL;
    ch->tail = NULL;
    ch->count = 0;
    ch->capacity = capacity;
    ch->closed = 0;
    ch->destroyed = 0;
    return ch;
}

static void freeChannel(xlChannelHandle *ch)
{
    xlChannelNode *n, *next;
    if (ch == NULL) return;
    /* free any remaining messages */
    for (n = ch->head; n != NULL; n = next) {
        next = n->next;
        free(n->data);
        free(n);
    }
#ifdef _WIN32
    DeleteCriticalSection(&ch->lock);
#else
    pthread_cond_destroy(&ch->notFull);
    pthread_cond_destroy(&ch->notEmpty);
    pthread_mutex_destroy(&ch->lock);
#endif
    free(ch);
}

#endif /* XLISP_USE_CONTEXT */


/* xchannelcreate - (channel-create [name] [capacity]) => channel handle */
xlValue xchannelcreate(void)
{
#ifdef XLISP_USE_CONTEXT
    const char *name = NULL;
    int capacity = 0;
    xlChannelHandle *ch;
    xlValue handle;

    /* optional name argument */
    if (xlMoreArgsP()) {
        xlValue arg = xlGetArg();
        if (xlStringP(arg)) {
            name = xlGetString(arg);
            /* optional capacity after name */
            if (xlMoreArgsP()) {
                xlValue capArg = xlGetArgFixnum();
                capacity = (int)xlGetFixnum(capArg);
                if (capacity < 0) capacity = 0;
            }
        } else if (xlFixnumP(arg)) {
            /* (channel-create capacity) with no name */
            capacity = (int)xlGetFixnum(arg);
            if (capacity < 0) capacity = 0;
        } else {
            xlBadType(arg);
        }
    }
    xlLastArg();

    ch = allocChannel(capacity);
    if (ch == NULL)
        xlFmtError("channel-create: out of memory");

    handle = xlMakeForeignPtr(&channelTag, ch);

    if (name != NULL) {
        if (!registryAdd(name, ch, &channelTag)) {
            freeChannel(ch);
            xlSetFPtr(handle, NULL);
            xlFmtError("channel-create: name already in use");
        }
    }

    return handle;
#else
    if (xlMoreArgsP()) xlGetArg();
    if (xlMoreArgsP()) xlGetArg();
    xlLastArg();
    xlFmtError("channel-create: requires threaded build (THREADS=1)");
    return xlNil;
#endif
}

/* xchannelsend - (channel-send channel string) => #t */
xlValue xchannelsend(void)
{
#ifdef XLISP_USE_CONTEXT
    xlValue chArg, strArg;
    xlChannelHandle *ch;
    const char *str;
    xlChannelNode *node;

    chArg = xlGetArgForeignPtr();
    strArg = xlGetArgString();
    xlLastArg();

    if (xlGetFPType(chArg) != &channelTag)
        xlFmtError("channel-send: not a channel");
    ch = (xlChannelHandle *)xlGetFPtr(chArg);
    if (ch == NULL || ch->destroyed)
        xlFmtError("channel-send: channel has been destroyed");

    str = xlGetString(strArg);

    /* allocate the message node */
    node = (xlChannelNode *)malloc(sizeof(xlChannelNode));
    if (node == NULL)
        xlFmtError("channel-send: out of memory");
    node->data = (char *)malloc(strlen(str) + 1);
    if (node->data == NULL) {
        free(node);
        xlFmtError("channel-send: out of memory");
    }
    strcpy(node->data, str);
    node->next = NULL;

    /* enqueue with blocking if bounded and full */
#ifdef _WIN32
    EnterCriticalSection(&ch->lock);
    while (ch->capacity > 0 && ch->count >= ch->capacity && !ch->closed)
        SleepConditionVariableCS(&ch->notFull, &ch->lock, INFINITE);
    if (ch->closed) {
        LeaveCriticalSection(&ch->lock);
        free(node->data);
        free(node);
        xlFmtError("channel-send: channel is closed");
    }
    if (ch->tail == NULL) {
        ch->head = ch->tail = node;
    } else {
        ch->tail->next = node;
        ch->tail = node;
    }
    ch->count++;
    WakeConditionVariable(&ch->notEmpty);
    LeaveCriticalSection(&ch->lock);
#else
    pthread_mutex_lock(&ch->lock);
    while (ch->capacity > 0 && ch->count >= ch->capacity && !ch->closed)
        pthread_cond_wait(&ch->notFull, &ch->lock);
    if (ch->closed) {
        pthread_mutex_unlock(&ch->lock);
        free(node->data);
        free(node);
        xlFmtError("channel-send: channel is closed");
    }
    if (ch->tail == NULL) {
        ch->head = ch->tail = node;
    } else {
        ch->tail->next = node;
        ch->tail = node;
    }
    ch->count++;
    pthread_cond_signal(&ch->notEmpty);
    pthread_mutex_unlock(&ch->lock);
#endif

    return xlTrue;
#else
    xlGetArgForeignPtr();
    xlGetArgString();
    xlLastArg();
    xlFmtError("channel-send: requires threaded build (THREADS=1)");
    return xlNil;
#endif
}

/* xchannelreceive - (channel-receive channel) => string or #f */
xlValue xchannelreceive(void)
{
#ifdef XLISP_USE_CONTEXT
    xlValue chArg;
    xlChannelHandle *ch;
    xlChannelNode *node;
    char *data;
    xlValue result;

    chArg = xlGetArgForeignPtr();
    xlLastArg();

    if (xlGetFPType(chArg) != &channelTag)
        xlFmtError("channel-receive: not a channel");
    ch = (xlChannelHandle *)xlGetFPtr(chArg);
    if (ch == NULL || ch->destroyed)
        xlFmtError("channel-receive: channel has been destroyed");

    /* dequeue with blocking if empty */
#ifdef _WIN32
    EnterCriticalSection(&ch->lock);
    while (ch->head == NULL && !ch->closed)
        SleepConditionVariableCS(&ch->notEmpty, &ch->lock, INFINITE);
    if (ch->head == NULL) {
        /* closed and empty */
        LeaveCriticalSection(&ch->lock);
        return xlFalse;
    }
    node = ch->head;
    ch->head = node->next;
    if (ch->head == NULL) ch->tail = NULL;
    ch->count--;
    WakeConditionVariable(&ch->notFull);
    LeaveCriticalSection(&ch->lock);
#else
    pthread_mutex_lock(&ch->lock);
    while (ch->head == NULL && !ch->closed)
        pthread_cond_wait(&ch->notEmpty, &ch->lock);
    if (ch->head == NULL) {
        /* closed and empty */
        pthread_mutex_unlock(&ch->lock);
        return xlFalse;
    }
    node = ch->head;
    ch->head = node->next;
    if (ch->head == NULL) ch->tail = NULL;
    ch->count--;
    pthread_cond_signal(&ch->notFull);
    pthread_mutex_unlock(&ch->lock);
#endif

    /* convert C string to Lisp string */
    data = node->data;
    result = xlMakeCString(data);
    free(data);
    free(node);
    return result;
#else
    xlGetArgForeignPtr();
    xlLastArg();
    xlFmtError("channel-receive: requires threaded build (THREADS=1)");
    return xlNil;
#endif
}

/* xchannelclose - (channel-close channel) => #t */
xlValue xchannelclose(void)
{
#ifdef XLISP_USE_CONTEXT
    xlValue chArg;
    xlChannelHandle *ch;

    chArg = xlGetArgForeignPtr();
    xlLastArg();

    if (xlGetFPType(chArg) != &channelTag)
        xlFmtError("channel-close: not a channel");
    ch = (xlChannelHandle *)xlGetFPtr(chArg);
    if (ch == NULL || ch->destroyed)
        xlFmtError("channel-close: channel has been destroyed");

#ifdef _WIN32
    EnterCriticalSection(&ch->lock);
    ch->closed = 1;
    WakeAllConditionVariable(&ch->notEmpty);
    WakeAllConditionVariable(&ch->notFull);
    LeaveCriticalSection(&ch->lock);
#else
    pthread_mutex_lock(&ch->lock);
    ch->closed = 1;
    pthread_cond_broadcast(&ch->notEmpty);
    pthread_cond_broadcast(&ch->notFull);
    pthread_mutex_unlock(&ch->lock);
#endif

    return xlTrue;
#else
    xlGetArgForeignPtr();
    xlLastArg();
    xlFmtError("channel-close: requires threaded build (THREADS=1)");
    return xlNil;
#endif
}

/* xchanneldestroy - (channel-destroy channel) => #t */
xlValue xchanneldestroy(void)
{
#ifdef XLISP_USE_CONTEXT
    xlValue chArg;
    xlChannelHandle *ch;

    chArg = xlGetArgForeignPtr();
    xlLastArg();

    if (xlGetFPType(chArg) != &channelTag)
        xlFmtError("channel-destroy: not a channel");
    ch = (xlChannelHandle *)xlGetFPtr(chArg);
    if (ch == NULL || ch->destroyed)
        xlFmtError("channel-destroy: already destroyed");

    ch->destroyed = 1;

    if (registryRelease(ch, &channelTag) <= 0)
        freeChannel(ch);

    xlSetFPtr(chArg, NULL);
    return xlTrue;
#else
    xlGetArgForeignPtr();
    xlLastArg();
    xlFmtError("channel-destroy: requires threaded build (THREADS=1)");
    return xlNil;
#endif
}

/* xchannellookup - (channel-lookup name) => channel handle or #f */
xlValue xchannellookup(void)
{
#ifdef XLISP_USE_CONTEXT
    xlValue nameArg;
    const char *name;
    xlChannelHandle *ch;

    nameArg = xlGetArgString();
    xlLastArg();
    name = xlGetString(nameArg);

    ch = (xlChannelHandle *)registryLookup(name, &channelTag);
    if (ch == NULL || ch->destroyed)
        return xlFalse;

    return xlMakeForeignPtr(&channelTag, ch);
#else
    xlGetArgString();
    xlLastArg();
    xlFmtError("channel-lookup: requires threaded build (THREADS=1)");
    return xlNil;
#endif
}

/* xchannelopenp - (channel-open? channel) => #t / #f */
xlValue xchannelopenp(void)
{
#ifdef XLISP_USE_CONTEXT
    xlValue chArg;
    xlChannelHandle *ch;

    chArg = xlGetArgForeignPtr();
    xlLastArg();

    if (xlGetFPType(chArg) != &channelTag)
        xlFmtError("channel-open?: not a channel");
    ch = (xlChannelHandle *)xlGetFPtr(chArg);
    if (ch == NULL || ch->destroyed)
        return xlFalse;

    return ch->closed ? xlFalse : xlTrue;
#else
    xlGetArgForeignPtr();
    xlLastArg();
    xlFmtError("channel-open?: requires threaded build (THREADS=1)");
    return xlNil;
#endif
}

/* xchannelp - (channel? obj) => #t / #f */
xlValue xchannelp(void)
{
    xlValue arg;
    arg = xlGetArg();
    xlLastArg();
    if (xlForeignPtrP(arg) && xlGetFPType(arg) == &channelTag && xlGetFPtr(arg) != NULL)
        return xlTrue;
    return xlFalse;
}
