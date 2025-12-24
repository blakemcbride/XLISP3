# xlisp
## The API

### Building

Standard build:
```bash
make
```

Reentrant/thread-safe build:
```bash
make REENTRANT=1
```

The reentrant build enables thread-local interpreter contexts, allowing XLISP to be safely called from multiple threads.

### Basic Usage

Here is the basic form of a C program that uses `xlisp.dll`.

```cpp
#include "xlisp.h"

/* main - the main routine */
void main(int argc,char *argv[])
{
    xlCallbacks *callbacks = NULL ;

    /* get the default callback structure */
    callbacks = xlDefaultCallbacks(argc,argv);

    /* initialize xlisp */
    xlInit(callbacks,argc,argv,NULL);

    /* add your functions here */

    /* display the banner */
    xlInfo("%s\\n",xlBanner());

    /* load the initialization file */
    xlLoadFile("xlisp.ini");

    /* call the read/eval/print loop */
    xlCallFunctionByName(NULL,0,"*TOPLEVEL*",0);
}
```

### Multi-threaded Usage

When built with `REENTRANT=1`, each thread must create and initialize its own interpreter context. Contexts are completely independent - no Lisp data is shared between threads.

```cpp
#include "xlisp.h"
#include "xlthread.h"

void *thread_func(void *arg)
{
    xlCallbacks *callbacks;
    xlContext *ctx;

    /* create a new interpreter context for this thread */
    ctx = xlCreateContext();
    if (ctx == NULL)
        return NULL;

    /* get default callbacks and initialize the context */
    callbacks = xlDefaultCallbacks(0, NULL);
    if (xlInitContext(ctx, callbacks, 0, NULL, NULL) != 0) {
        xlDestroyContext(ctx);
        return NULL;
    }

    /* use the interpreter */
    xlLoadFile("mycode.lsp");
    xlCallFunctionByName(NULL, 0, "MY-FUNCTION", 0);

    /* clean up when done */
    xlDestroyContext(ctx);
    return NULL;
}
```

#### Context API Functions

```cpp
xlContext *xlCreateContext(void)
```
Allocates a new interpreter context. Returns NULL on failure.

```cpp
int xlInitContext(xlContext *ctx, xlCallbacks *callbacks,
                  int argc, const char *argv[], const char *workspace)
```
Initializes a context for use. Returns 0 on success, -1 on failure.
- `ctx` - context created by `xlCreateContext()`
- `callbacks` - callback structure (use `xlDefaultCallbacks()`)
- `argc`, `argv` - command line arguments (can be 0, NULL)
- `workspace` - workspace image file to restore (or NULL)

```cpp
void xlSetCurrentContext(xlContext *ctx)
```
Sets the current thread's active context. Called automatically by `xlInitContext()`.

```cpp
void xlDestroyContext(xlContext *ctx)
```
Frees all memory associated with a context. Call when the thread is done with the interpreter.

```cpp
xlContext *xlGetCurrentContext(void)
```
Returns the current thread's active context.

### Defining External Functions

External functions should be declared as functions taking no arguments and returning an xlValue which is the result. Arguments should be fetched by using the routines below.

For functions that take optional arguments, call the predicate `xlMoreArgsP()` to determine if more arguments are present before
calling the argument fetching function.

When you have fetched all of the arguments, call `xlLastArg()` 
to detect calls with too many arguments.

Your new function should have the following form:
```cpp
xlValue myadd(void)
{
xlFIXTYPE a,b;

xlVal = xlGetArgFixnum(); 
a = xlGetFixnum(xlVal);

xlVal = xlGetArgFixnum(); 
b = xlGetFixnum(xlVal);

xlLastArg();
    return xlMakeFixnum(a + b);
}
```
After writing your function, add it to xlisp by using the `xlSubr()` 
function:
```lisp
xlSubr("myadd",myadd);
```
Argument list parsing macros:
```cpp
xlGetArg()
xlLastArg()
xlMoreArgsP()
```
Macros to get arguments of a particular type:
```cpp
xlGetArgCons()
xlGetArgList()
xlGetArgSymbol()
xlGetArgString()
xlGetArgFixnum()
xlGetArgNumber()
xlGetArgChar()
xlGetArgVector()
xlGetArgPort()
xlGetArgInputPort()
xlGetArgOutputPort()
xlGetArgUnnamedStream()
xlGetArgClosure()
xlGetArgEnv()
```
All of the argument fetching functions return their result as an `xlValue`. 

If you want to get the fixnum or flonum in a form that C can understand, use the following macros. To return a fixnum result, use
the function `xlMakeFixnum()` passing it a C long. To return a flonum result, use the function `xlMakeFlonum()` passing it a C double.

Types:
```cpp
xlFIXTYPE
xlFLOTYPE
```
Fixnum/flonum/character access macros:
```cpp
xlGetFixnum(x)
xlGetFlonum(x)
xlGetChCode(x)
```