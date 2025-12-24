/* xlosint.c - operating system interface routines */
/*      Copyright (c) 1984-2002, by David Michael Betz
        All Rights Reserved
        See the included file 'license.txt' for the full license.
*/

#include "xlisp.h"

#ifndef XLISP_USE_CONTEXT
/* global variables - only in legacy mode */
static xlCallbacks *callbacks = NULL;
#endif

/* xlSetCallbacks - initialize xlisp */
void xlSetCallbacks(xlCallbacks *cb)
{
#ifdef XLISP_USE_CONTEXT
    /* In context mode, store in the current context */
    xlCtx()->callbacks = cb;
#else
    /* save the pointer to the callbacks */
    callbacks = cb;
#endif
}

/* Helper to get callbacks pointer */
static xlCallbacks *getCallbacks(void)
{
#ifdef XLISP_USE_CONTEXT
    return xlCtx()->callbacks;
#else
    return callbacks;
#endif
}

/* xlosLoadPath - return the load path */
xlEXPORT const char *xlosLoadPath(void)
{
    xlCallbacks *cb = getCallbacks();
    return cb && cb->loadPath ? (*cb->loadPath)() : NULL;
}

/* xlosParsePath - return the load path */
xlEXPORT const char *xlosParsePath(const char **pp)
{
    xlCallbacks *cb = getCallbacks();
    return cb && cb->parsePath ? (*cb->parsePath)(pp) : NULL;
}

/* xlosDirectorySeparator - return the directory separator character */
xlEXPORT int xlosDirectorySeparator(void)
{
    xlCallbacks *cb = getCallbacks();
    return cb && cb->directorySeparator ? (*cb->directorySeparator)() : '\\';
}

/* xlosEnter - enter o/s specific functions */
void xlosEnter(void)
{
    xlSubrDef *sdp;
    xlXSubrDef *xsdp;
    for (sdp = xlosSubrTab; sdp->name != NULL; ++sdp)
        xlSubr(sdp->name,sdp->subr);
    for (xsdp = xlosXSubrTab; xsdp->name != NULL; ++xsdp)
        xlXSubr(xsdp->name,xsdp->subr);
}

/* xlosFindSubr - find an os specific function */
xlEXPORT xlValue (*xlosFindSubr(const char *name))(void)
{
    xlSubrDef *sdp;
    xlXSubrDef *xsdp;

    /* find the built-in function */
    for (sdp = xlosSubrTab; sdp->name != NULL; ++sdp)
        if (strcmp(sdp->name,name) == 0)
            return sdp->subr;
    for (xsdp = xlosXSubrTab; xsdp->name != NULL; ++xsdp)
        if (strcmp(xsdp->name,name) == 0)
            return (xlValue (*)(void))xsdp->subr;

    /* call the user handler */
    xlCallbacks *cb = getCallbacks();
    return cb && cb->findSubr ? (*cb->findSubr)(name) : NULL;
}

/* xlosError - print an error message */
xlEXPORT void xlosError(const char *msg)
{
    xlCallbacks *cb = getCallbacks();
    if (cb && cb->error)
        (*cb->error)(msg);
}

/* xlosFileModTime - return the modification time of a file */
xlEXPORT int xlosFileModTime(const char *fname,xlFIXTYPE *pModTime)
{
    xlCallbacks *cb = getCallbacks();
    return cb && cb->fileModTime ? (*cb->fileModTime)(fname,pModTime) : FALSE;
}

/* xlosConsoleGetC - get a character from the terminal */
xlEXPORT int xlosConsoleGetC(void)
{
    xlCallbacks *cb = getCallbacks();
    int ch;

    /* get the next character */
    ch = cb && cb->consoleGetC ? (*cb->consoleGetC)() : EOF;

    /* output the character to the transcript file */
    if (xlTranscriptFP && ch != EOF)
        putc(ch,xlTranscriptFP);

    /* return the character */
    return ch;
}

/* xlosConsolePutC - put a character to the terminal */
xlEXPORT void xlosConsolePutC(int ch)
{
    xlCallbacks *cb = getCallbacks();

    /* check for control characters */
    xlosCheck();

    /* output the character */
    if (cb && cb->consolePutC)
        (*cb->consolePutC)(ch);

    /* output the character to the transcript file */
    if (xlTranscriptFP)
        putc(ch,xlTranscriptFP);
}

/* xlosConsolePutS - output a string to the terminal */
xlEXPORT void xlosConsolePutS(const char *str)
{
    while (*str)
        xlosConsolePutC(*str++);
}

/* xlosConsoleAtBOLP - are we at the beginning of a line? */
xlEXPORT int xlosConsoleAtBOLP(void)
{
    xlCallbacks *cb = getCallbacks();
    return cb && cb->consoleAtBOLP ? (*cb->consoleAtBOLP)() : FALSE;
}

/* xlosConsoleFlush - flush the terminal input buffer */
xlEXPORT void xlosConsoleFlush(void)
{
    xlCallbacks *cb = getCallbacks();
    if (cb && cb->consoleFlushInput)
        (*cb->consoleFlushInput)();
}

/* xlosConsoleCheck - check for control characters during execution */
xlEXPORT int xlosConsoleCheck(void)
{
    xlCallbacks *cb = getCallbacks();
    return cb && cb->consoleCheck ? (*cb->consoleCheck)() : 0;
}

/* xlosFlushOutput - flush the output buffer */
xlEXPORT void xlosFlushOutput(void)
{
    xlCallbacks *cb = getCallbacks();
    if (cb && cb->consoleFlushOutput)
        (*cb->consoleFlushOutput)();
}

/* xlosExit - exit from XLISP */
xlEXPORT void xlosExit(int sts)
{
    xlCallbacks *cb = getCallbacks();
    if (cb && cb->exit)
        (*cb->exit)(sts);
}
