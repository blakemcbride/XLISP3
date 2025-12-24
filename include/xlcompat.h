/* xlcompat.h - compatibility macros for multi-threading support */
/*      Copyright (c) 1984-2002, by David Michael Betz
        All Rights Reserved
        See the included file 'license.txt' for the full license.
*/

#ifndef __XLCOMPAT_H__
#define __XLCOMPAT_H__

#include "xlcontext.h"

/*
 * Compatibility Layer for XLISP Threading
 *
 * These macros redirect all global variable accesses to the current
 * thread's context. This allows existing code to work unchanged while
 * supporting multiple interpreter instances.
 *
 * IMPORTANT: Do not use these macros in xlcontext.c where the actual
 * context management is implemented. Define XLISP_CONTEXT_IMPL before
 * including this header to disable the macros.
 */

#ifndef XLISP_CONTEXT_IMPL

/* ====================================================================
 * VM Registers
 * ==================================================================== */
#define xlFun       (xlCtx()->fun)
#define xlEnv       (xlCtx()->env)
#define xlVal       (xlCtx()->val)
#define xlArgC      (xlCtx()->argc)
#define xlNext      (xlCtx()->next)

/* ====================================================================
 * Stacks
 * ==================================================================== */
#define xlSP        (xlCtx()->sp)
#define xlCSP       (xlCtx()->csp)
#define xlStkBase   (xlCtx()->stkBase)
#define xlStkTop    (xlCtx()->stkTop)

/* ====================================================================
 * Bytecode Interpreter State
 * ==================================================================== */
#define xlerrtarget      (xlCtx()->errTarget)
#define xlcatch          (xlCtx()->catchFrame)
#define xlTraceBytecodes (xlCtx()->traceBytecodes)

/* Note: pc and pcBase are static in xlint.c, handled separately */

/* ====================================================================
 * Memory Management - Node Space
 * ==================================================================== */
#define xlNSegments (xlCtx()->nSegments)
#define xlNSLast    (xlCtx()->nsLast)
#define xlFNodes    (xlCtx()->fNodes)
#define xlNSSize    (xlCtx()->nsSize)
#define xlNNodes    (xlCtx()->nNodes)
#define xlNFree     (xlCtx()->nFree)
#define xlNSCount   (xlCtx()->nsCount)

/* ====================================================================
 * Memory Management - Vector Space
 * ==================================================================== */
#define xlVSegments (xlCtx()->vSegments)
#define xlVSCurrent (xlCtx()->vsCurrent)
#define xlVFree     (xlCtx()->vFree)
#define xlVTop      (xlCtx()->vTop)
#define xlVSSize    (xlCtx()->vsSize)
#define xlVSCount   (xlCtx()->vsCount)

/* ====================================================================
 * Memory Management - Other
 * ==================================================================== */
#define xlPPointers (xlCtx()->pPointers)
#define xlTotal     (xlCtx()->total)
#define xlGCCalls   (xlCtx()->gcCalls)

/* ====================================================================
 * Important Singleton Values
 * ==================================================================== */
#define xlTrue          (xlCtx()->vTrue)
#define xlFalse         (xlCtx()->vFalse)
#define xlUnboundObject (xlCtx()->unboundObject)
#define xlDefaultObject (xlCtx()->defaultObject)
#define xlEofObject     (xlCtx()->eofObject)

/* ====================================================================
 * Package System
 * ==================================================================== */
#define xlPackages       (xlCtx()->packages)
#define xlLispPackage    (xlCtx()->lispPackage)
#define xlXLispPackage   (xlCtx()->xlispPackage)
#define xlKeywordPackage (xlCtx()->keywordPackage)

/* ====================================================================
 * Cached Symbols - Special Forms
 * ==================================================================== */
#define s_quote           (xlCtx()->sym.quote)
#define s_function        (xlCtx()->sym.function)
#define s_quasiquote      (xlCtx()->sym.quasiquote)
#define s_unquote         (xlCtx()->sym.unquote)
#define s_unquotesplicing (xlCtx()->sym.unquoteSplicing)
#define s_dot             (xlCtx()->sym.dot)

/* ====================================================================
 * Cached Symbols - System
 * ==================================================================== */
#define s_package      (xlCtx()->sym.package)
#define s_eval         (xlCtx()->sym.eval)
#define s_load         (xlCtx()->sym.load)
#define s_print        (xlCtx()->sym.print)
#define s_printcase    (xlCtx()->sym.printCase)
#define s_eql          (xlCtx()->sym.eql)
#define s_error        (xlCtx()->sym.error)
#define s_stackpointer (xlCtx()->sym.stackPointer)
#define s_backtrace    (xlCtx()->sym.backtrace)
#define s_unassigned   (xlCtx()->sym.unassigned)

/* ====================================================================
 * Cached Symbols - Standard Streams
 * ==================================================================== */
#define s_stdin  (xlCtx()->sym.stdin_)
#define s_stdout (xlCtx()->sym.stdout_)
#define s_stderr (xlCtx()->sym.stderr_)

/* ====================================================================
 * Cached Symbols - Format Strings
 * ==================================================================== */
#define s_fixfmt  (xlCtx()->sym.fixfmt)
#define s_hexfmt  (xlCtx()->sym.hexfmt)
#define s_flofmt  (xlCtx()->sym.flofmt)
#define s_freeptr (xlCtx()->sym.freeptr)

/* ====================================================================
 * Cached Symbols - Lambda List Keywords
 * ==================================================================== */
#define lk_optional        (xlCtx()->sym.lk_optional)
#define lk_rest            (xlCtx()->sym.lk_rest)
#define lk_key             (xlCtx()->sym.lk_key)
#define lk_aux             (xlCtx()->sym.lk_aux)
#define lk_allow_other_keys (xlCtx()->sym.lk_allow_other_keys)
#define slk_optional       (xlCtx()->sym.slk_optional)
#define slk_rest           (xlCtx()->sym.slk_rest)

/* ====================================================================
 * Cached Symbols - Keywords
 * ==================================================================== */
#define k_upcase    (xlCtx()->sym.k_upcase)
#define k_downcase  (xlCtx()->sym.k_downcase)
#define k_internal  (xlCtx()->sym.k_internal)
#define k_external  (xlCtx()->sym.k_external)
#define k_inherited (xlCtx()->sym.k_inherited)
#define k_key       (xlCtx()->sym.k_key)
#define k_uses      (xlCtx()->sym.k_uses)
#define k_test      (xlCtx()->sym.k_test)
#define k_testnot   (xlCtx()->sym.k_testnot)
#define k_start     (xlCtx()->sym.k_start)
#define k_end       (xlCtx()->sym.k_end)
#define k_1start    (xlCtx()->sym.k_1start)
#define k_1end      (xlCtx()->sym.k_1end)
#define k_2start    (xlCtx()->sym.k_2start)
#define k_2end      (xlCtx()->sym.k_2end)
#define k_count     (xlCtx()->sym.k_count)
#define k_fromend   (xlCtx()->sym.k_fromend)

/* ====================================================================
 * Reader State
 * ==================================================================== */
#define xlSymReadTable (xlCtx()->symReadTable)
#define xlSymNMacro    (xlCtx()->symNMacro)
#define xlSymTMacro    (xlCtx()->symTMacro)
#define xlSymWSpace    (xlCtx()->symWSpace)
#define xlSymConst     (xlCtx()->symConst)
#define xlSymSEscape   (xlCtx()->symSEscape)
#define xlSymMEscape   (xlCtx()->symMEscape)

/* ====================================================================
 * Printer State
 * ==================================================================== */
#define xlPRBreadth (xlCtx()->prBreadth)
#define xlPRDepth   (xlCtx()->prDepth)

/* ====================================================================
 * I/O State
 * ==================================================================== */
#define xlTranscriptFP (xlCtx()->transcriptFP)

/* ====================================================================
 * Initialization and Command Line
 * ==================================================================== */
#define xlInitializedP (xlCtx()->initialized)
#define xlCmdLineArgC  (xlCtx()->cmdLineArgC)
#define xlCmdLineArgV  (xlCtx()->cmdLineArgV)

/* ====================================================================
 * Compiler State
 * ==================================================================== */
#define xlDebugModeP   (xlCtx()->debugModeP)

/* ====================================================================
 * Object System
 * ==================================================================== */
#define c_class      (xlCtx()->c_class)
#define c_object     (xlCtx()->c_object)
#define k_initialize (xlCtx()->k_initialize)

#endif /* XLISP_CONTEXT_IMPL */

#endif /* __XLCOMPAT_H__ */
