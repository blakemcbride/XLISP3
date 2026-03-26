/* xlshared.c - shared bytecode pool for cross-thread code sharing */
/*      Copyright (c) 1984-2002, by David Michael Betz
        All Rights Reserved
        See the included file 'license.txt' for the full license.
*/

#include "xlisp.h"

#ifdef XLISP_USE_CONTEXT

#include "xlshared.h"
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

/* the global shared pool (process-wide, not per-context) */
static xlSharedPool *sharedPool = NULL;

/* mutex for pool access */
#ifdef _WIN32
static CRITICAL_SECTION poolMutex;
static int poolMutexReady = 0;
static void ensurePoolMutex(void) {
    if (!poolMutexReady) {
        InitializeCriticalSection(&poolMutex);
        poolMutexReady = 1;
    }
}
#define POOL_LOCK()   do { ensurePoolMutex(); EnterCriticalSection(&poolMutex); } while(0)
#define POOL_UNLOCK() LeaveCriticalSection(&poolMutex)
#else
static pthread_mutex_t poolMutex = PTHREAD_MUTEX_INITIALIZER;
#define POOL_LOCK()   pthread_mutex_lock(&poolMutex)
#define POOL_UNLOCK() pthread_mutex_unlock(&poolMutex)
#endif

/* forward declarations */
static int publishCodeObject(xlValue code);
static xlValue instantiateTemplate(int templateIndex);
static char *copyString(const char *s);
static void ensurePool(void);
static void ensureTemplateCapacity(void);
static void ensureBindingCapacity(void);

/* ====================================================================
 * Pool management
 * ==================================================================== */

static void ensurePool(void)
{
    if (sharedPool == NULL) {
        sharedPool = (xlSharedPool *)malloc(sizeof(xlSharedPool));
        if (sharedPool == NULL)
            xlFmtAbort("shared pool: out of memory");
        memset(sharedPool, 0, sizeof(xlSharedPool));
    }
}

static void ensureTemplateCapacity(void)
{
    ensurePool();
    if (sharedPool->nTemplates >= sharedPool->templateCapacity) {
        int newCap = sharedPool->templateCapacity == 0 ? 64 : sharedPool->templateCapacity * 2;
        xlSharedCodeTemplate *newArr = (xlSharedCodeTemplate *)realloc(
            sharedPool->templates, newCap * sizeof(xlSharedCodeTemplate));
        if (newArr == NULL)
            xlFmtAbort("shared pool: out of memory");
        sharedPool->templates = newArr;
        sharedPool->templateCapacity = newCap;
    }
}

static void ensureBindingCapacity(void)
{
    ensurePool();
    if (sharedPool->nBindings >= sharedPool->bindingCapacity) {
        int newCap = sharedPool->bindingCapacity == 0 ? 64 : sharedPool->bindingCapacity * 2;
        xlSharedBinding *newArr = (xlSharedBinding *)realloc(
            sharedPool->bindings, newCap * sizeof(xlSharedBinding));
        if (newArr == NULL)
            xlFmtAbort("shared pool: out of memory");
        sharedPool->bindings = newArr;
        sharedPool->bindingCapacity = newCap;
    }
}

static char *copyString(const char *s)
{
    char *copy;
    if (s == NULL)
        return NULL;
    copy = (char *)malloc(strlen(s) + 1);
    if (copy == NULL)
        xlFmtAbort("shared pool: out of memory");
    strcpy(copy, s);
    return copy;
}

/* ====================================================================
 * Publishing: convert a code object into a shared template
 * ==================================================================== */

/* get the primary name of a package as a C string */
static const char *packageName(xlValue pkg)
{
    xlValue names;
    if (!xlPackageP(pkg))
        return NULL;
    names = xlGetNames(pkg);
    if (xlConsP(names) && xlStringP(xlCar(names)))
        return xlGetString(xlCar(names));
    return NULL;
}

/* publish a single code object, returning its template index */
static int publishCodeObject(xlValue code)
{
    xlSharedCodeTemplate tmpl;
    xlValue bcode;
    xlFIXTYPE bcLen, nlits, i, numUserLits;
    const unsigned char *bcData;

    /* extract bytecodes */
    bcode = xlGetBCode(code);
    bcLen = xlGetSLength(bcode);
    bcData = xlGetCodeStr(code);

    /* copy bytecodes to shared memory */
    tmpl.bytecode = (unsigned char *)malloc(bcLen);
    if (tmpl.bytecode == NULL)
        xlFmtAbort("shared pool: out of memory");
    memcpy(tmpl.bytecode, bcData, bcLen);
    tmpl.bytecodeLen = bcLen;

    /* extract function name */
    {
        xlValue nameVal = xlGetCName(code);
        if (xlSymbolP(nameVal))
            tmpl.name = copyString(xlGetString(xlGetPName(nameVal)));
        else
            tmpl.name = NULL;
    }

    /* process literals */
    nlits = xlGetSize(code);
    tmpl.nlits = nlits;
    numUserLits = nlits - xlFIRSTLIT;

    if (numUserLits > 0) {
        tmpl.lits = (xlSharedLiteral *)calloc(numUserLits, sizeof(xlSharedLiteral));
        if (tmpl.lits == NULL)
            xlFmtAbort("shared pool: out of memory");

        for (i = 0; i < numUserLits; i++) {
            xlValue lit = xlGetElement(code, xlFIRSTLIT + i);
            xlSharedLiteral *sl = &tmpl.lits[i];

            if (xlNullP(lit)) {
                sl->type = xlSL_NIL;
            }
            else if (lit == xlTrue) {
                sl->type = xlSL_TRUE;
            }
            else if (xlFixnumP(lit)) {
                sl->type = xlSL_FIXNUM;
                sl->val.fixnum = xlGetFixnum(lit);
            }
            else if (xlFlonumP(lit)) {
                sl->type = xlSL_FLONUM;
                sl->val.flonum = xlGetFlonum(lit);
            }
            else if (xlCharacterP(lit)) {
                sl->type = xlSL_CHAR;
                sl->val.character = xlGetChCode(lit);
            }
            else if (xlStringP(lit)) {
                xlFIXTYPE slen = xlGetSLength(lit);
                sl->type = xlSL_STRING;
                sl->val.string.len = slen;
                sl->val.string.data = (char *)malloc(slen + 1);
                if (sl->val.string.data == NULL)
                    xlFmtAbort("shared pool: out of memory");
                memcpy(sl->val.string.data, xlGetString(lit), slen);
                sl->val.string.data[slen] = '\0';
            }
            else if (xlSymbolP(lit)) {
                xlValue pkg;
                sl->type = xlSL_SYMBOL;
                sl->val.symbol.name = copyString(xlGetString(xlGetPName(lit)));
                pkg = xlGetPackage(lit);
                sl->val.symbol.package = copyString(packageName(pkg));
            }
            else if (xlCodeP(lit)) {
                /* nested code object - publish recursively */
                sl->type = xlSL_CODE;
                sl->val.codeIndex = publishCodeObject(lit);
            }
            else {
                /* unsupported literal type - store as NIL */
                sl->type = xlSL_NIL;
            }
        }
    }
    else {
        tmpl.lits = NULL;
    }

    /* add template to pool */
    ensureTemplateCapacity();
    {
        int idx = sharedPool->nTemplates;
        sharedPool->templates[idx] = tmpl;
        sharedPool->nTemplates++;
        return idx;
    }
}

/* ====================================================================
 * Instantiation: create local code objects from a shared template
 * ==================================================================== */

static xlValue instantiateTemplate(int templateIndex)
{
    xlSharedCodeTemplate *tmpl;
    xlValue code;
    xlFIXTYPE nlits, i, numUserLits;

    if (sharedPool == NULL || templateIndex < 0 || templateIndex >= sharedPool->nTemplates)
        xlFmtError("shared pool: invalid template index");

    tmpl = &sharedPool->templates[templateIndex];
    nlits = tmpl->nlits;
    numUserLits = nlits - xlFIRSTLIT;

    /* create a new code object in the local heap */
    code = xlNewCode(nlits);
    xlCPush(code);

    /* create a bytecode string node pointing to shared data */
    {
        xlValue bstr = xlMakeExternalString(tmpl->bytecode, tmpl->bytecodeLen);
        xlSetBCode(code, bstr);
    }

    /* set function name */
    if (tmpl->name != NULL)
        xlSetCName(code, xlEnter(tmpl->name));

    /* set vnames to nil (informational only, used for debugging) */
    xlSetVNames(code, xlNil);

    /* resolve each literal in the local context */
    for (i = 0; i < numUserLits; i++) {
        xlSharedLiteral *sl = &tmpl->lits[i];
        xlValue val = xlNil;

        switch (sl->type) {
        case xlSL_NIL:
            val = xlNil;
            break;
        case xlSL_TRUE:
            val = xlTrue;
            break;
        case xlSL_FIXNUM:
            val = xlMakeFixnum(sl->val.fixnum);
            break;
        case xlSL_FLONUM:
            val = xlMakeFlonum(sl->val.flonum);
            break;
        case xlSL_CHAR:
            val = xlMakeChar(sl->val.character);
            break;
        case xlSL_STRING:
            val = xlMakeString(sl->val.string.data, sl->val.string.len);
            break;
        case xlSL_SYMBOL: {
            xlValue pkg = xlNil;
            xlValue key;
            if (sl->val.symbol.package != NULL)
                pkg = xlFindPackage(sl->val.symbol.package);
            if (xlNullP(pkg))
                pkg = xlLispPackage;
            val = xlInternCString(sl->val.symbol.name, pkg, &key);
            break;
        }
        case xlSL_CODE:
            /* nested code object - instantiate recursively */
            val = instantiateTemplate(sl->val.codeIndex);
            break;
        }

        xlSetElement(code, xlFIRSTLIT + i, val);
    }

    return xlPop();
}

/* ====================================================================
 * Linking: install all shared bindings into the current context
 * ==================================================================== */

/* xlHasSharedCode - check if any shared code is available */
int xlHasSharedCode(void)
{
    return sharedPool != NULL && sharedPool->nBindings > 0;
}

/* xlLinkSharedCode - instantiate all shared templates and bind them */
void xlLinkSharedCode(void)
{
    int i;

    if (!xlHasSharedCode())
        return;

    for (i = 0; i < sharedPool->nBindings; i++) {
        xlSharedBinding *binding = &sharedPool->bindings[i];
        xlValue code, closure, sym, pkg, key;

        /* instantiate the code template */
        code = instantiateTemplate(binding->templateIndex);
        xlCPush(code);

        /* create a closure with the global environment */
        closure = xlMakeClosure(code, xlNil);
        xlCPush(closure);

        /* find or create the symbol and bind it */
        pkg = xlNil;
        if (binding->package != NULL)
            pkg = xlFindPackage(binding->package);
        if (xlNullP(pkg))
            pkg = xlLispPackage;
        sym = xlInternCString(binding->name, pkg, &key);
        xlSetValue(sym, closure);

        xlDrop(2);
    }
}

/* ====================================================================
 * Lisp-level built-in functions
 * ==================================================================== */

/* xsharefunction - built-in function 'share-function' */
/* (share-function sym) => #t */
xlValue xsharefunction(void)
{
#ifdef XLISP_USE_CONTEXT
    xlValue sym, val, code;
    const char *symName;
    const char *pkgName;
    xlValue pkg;
    int templateIndex;

    /* get the symbol argument */
    sym = xlGetArgSymbol();
    xlLastArg();

    /* get the symbol's value - must be a closure */
    val = xlGetValue(sym);
    if (!xlClosureP(val))
        xlError("not a closure", val);

    /* extract the code object from the closure */
    code = xlGetCode(val);
    if (!xlCodeP(code))
        xlError("closure does not contain a code object", val);

    /* publish the code object to the shared pool */
    POOL_LOCK();

    templateIndex = publishCodeObject(code);

    /* record the binding */
    symName = xlGetString(xlGetPName(sym));
    pkg = xlGetPackage(sym);
    pkgName = packageName(pkg);

    ensureBindingCapacity();
    {
        xlSharedBinding *b = &sharedPool->bindings[sharedPool->nBindings];
        b->name = copyString(symName);
        b->package = copyString(pkgName);
        b->templateIndex = templateIndex;
        sharedPool->nBindings++;
    }

    POOL_UNLOCK();

    return xlTrue;
#else
    xlFmtError("share-function requires a threaded build (THREADS=1)");
    return xlNil;
#endif
}

/* xsharedcodep - built-in function 'shared-code?' */
/* (shared-code?) => #t or #f */
xlValue xsharedcodep(void)
{
    xlLastArg();
#ifdef XLISP_USE_CONTEXT
    return xlHasSharedCode() ? xlTrue : xlNil;
#else
    return xlNil;
#endif
}

#endif /* XLISP_USE_CONTEXT */
