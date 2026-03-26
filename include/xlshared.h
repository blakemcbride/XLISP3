/* xlshared.h - shared bytecode pool for cross-thread code sharing */
/*      Copyright (c) 1984-2002, by David Michael Betz
        All Rights Reserved
        See the included file 'license.txt' for the full license.
*/

#ifndef __XLSHARED_H__
#define __XLSHARED_H__

#include "xlisp.h"

#ifdef XLISP_USE_CONTEXT

/* shared literal descriptor types */
#define xlSL_NIL       0
#define xlSL_TRUE      1
#define xlSL_FIXNUM    2
#define xlSL_FLONUM    3
#define xlSL_CHAR      4
#define xlSL_STRING    5
#define xlSL_SYMBOL    6
#define xlSL_CODE      7

/* a shared literal descriptor */
typedef struct xlSharedLiteral {
    int type;
    union {
        xlFIXTYPE fixnum;
        xlFLOTYPE flonum;
        xlCHRTYPE character;
        struct { char *data; xlFIXTYPE len; } string;
        struct { char *name; char *package; } symbol;
        int codeIndex;       /* index into templates array for nested code */
    } val;
} xlSharedLiteral;

/* a shared code template */
typedef struct xlSharedCodeTemplate {
    unsigned char *bytecode;     /* shared bytecodes (malloc'd, permanent) */
    xlFIXTYPE bytecodeLen;
    char *name;                  /* function name as C string, or NULL */
    int nlits;                   /* total code vector size */
    xlSharedLiteral *lits;       /* literal descriptors for indices xlFIRSTLIT..nlits-1 */
} xlSharedCodeTemplate;

/* a top-level binding: symbol name -> template index */
typedef struct xlSharedBinding {
    char *name;
    char *package;
    int templateIndex;
} xlSharedBinding;

/* the global shared code pool */
typedef struct xlSharedPool {
    int nTemplates;
    int templateCapacity;
    xlSharedCodeTemplate *templates;
    int nBindings;
    int bindingCapacity;
    xlSharedBinding *bindings;
} xlSharedPool;

/* public API */
int xlHasSharedCode(void);
void xlLinkSharedCode(void);

/* Lisp-level built-in functions */
xlValue xsharefunction(void);
xlValue xsharedcodep(void);

#endif /* XLISP_USE_CONTEXT */

#endif /* __XLSHARED_H__ */
