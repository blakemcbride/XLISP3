# XLISP Codebase Analysis

## Overview

**XLISP** is an object-oriented LISP interpreter/compiler (v10.0.0) by David Michael Betz (1983-2017). Licensed under MIT. It compiles Lisp to bytecodes rather than interpreting directly, making it faster than traditional interpreters. Supports native multi-threading with per-thread interpreter contexts when built in reentrant mode.

## Project Structure

```
xlisp/
├── src/           # Core C source (29 files, ~19.5K lines)
├── include/       # Header files (xlisp.h, xlcontext.h, xlcompat.h, xlthread.h)
├── xlisp/         # REPL executable source
├── ext/           # Extension module
├── doc/           # Documentation (Markdown)
├── tests/         # Lisp test scripts
├── *.lsp          # 14 Lisp initialization/library files
├── CMakeLists.txt # Modern build system
└── Makefile       # Legacy build
```

## Key Components

| Module | Purpose |
|--------|---------|
| `xlcom.c` | Bytecode compiler |
| `xlint.c` | Bytecode interpreter/VM |
| `xldmem.c` | Memory management & GC |
| `xlobj.c` | Object-oriented system |
| `xlread.c` / `xlprint.c` | Reader/printer |
| `xlapi.c` | C embedding API |
| `xlfun1.c`, `xlfun2.c`, `xlfun3.c` | Built-in functions |
| `xlftab.c` | Function table registry |
| `xlmath.c` | Mathematical functions |
| `xlsym.c` | Symbol and package management |
| `xlimage.c` | Memory image/workspace management |
| `xlfasl.c` | Fast loading (compiled bytecode) |
| `xldbg.c` | Debugging support |
| `xlcontext.c` | Per-thread interpreter context management |
| `xlnthread.c` | Native thread creation/join |
| `xlsync.c` | Synchronization primitives (mutexes, condition variables, channels) |
| `msstuff.c` | Windows-specific code |
| `unstuff.c` | Unix-specific code |

## Lisp Library Files

| File | Purpose |
|------|---------|
| `xlisp.lsp` | Main initialization |
| `xlinit.lsp` | System initialization |
| `macros.lsp` | Macro system |
| `objects.lsp` | Object system utilities |
| `qquote.lsp` | Quasiquote/unquote support |
| `clisp.lsp` | Common Lisp compatibility |
| `pp.lsp` | Pretty printer |
| `math.lsp` | Math utilities |
| `compile.lsp` | Compilation utilities |
| `fasl.lsp` | Fast loading utilities |
| `crec.lsp` | C records (FFI) |

## Architecture

### Execution Flow

```
Lisp Source Code
       ↓
   Reader (xlread.c)
       ↓
  S-Expressions
       ↓
 Compiler (xlcom.c)
       ↓
    Bytecodes
       ↓
 Interpreter (xlint.c)
       ↓
 Execution Results
```

### Memory Layout

- **Node Space**: Lisp values with free list and protected pointers
- **Vector Space**: Strings and arrays with automatic expansion
- **Generational garbage collection**

### VM Registers

- `xlFun` - current function
- `xlEnv` - environment
- `xlVal` - last value
- `xlSP` - value stack pointer
- `xlCSP` - control stack pointer

### Threading Architecture

When built with `THREADS=1`:

- All global interpreter state is encapsulated in an `xlContext` structure
  (`include/xlcontext.h`)
- A thread-local pointer (`xl_current_context`) tracks the active context
- Compatibility macros in `include/xlcompat.h` redirect global variable
  accesses through the context pointer (e.g., `#define xlVal (xlCtx()->val)`)
- Each thread gets independent stacks, heap, GC, symbols, and packages
- The bytecode dispatch table (`optab`) is shared and immutable
- Synchronization objects (mutexes, condition variables, and message channels)
  are shared at the C level via a named registry with reference counting

## Notable Features

- **Bytecode compilation** - not direct interpretation
- **Generational garbage collection** with protected pointers
- **Class-based OOP** with inheritance and method dispatch
- **Scheme influences** - lexical scoping, proper tail recursion
- **Common Lisp elements** - packages, keywords, multiple values
- **Native threading** - per-thread interpreter contexts with mutexes,
  condition variables, message channels, and high-level utilities
  (futures, thread pools, parallel map)
- **Cross-platform** (Windows, Linux, macOS) via ANSI C
- **Extensible** via C API and extension modules
- **FASL support** - fast loading of pre-compiled bytecode

## Build System

### CMake (Recommended)

```bash
mkdir build && cd build
cmake ..
make
make install  # or make package
```

Requires CMake 3.14+. Supports link-time optimization (IPO).

### Build Targets

1. **xlisp** (library) - Core interpreter/compiler
2. **ext** (shared library) - Extension module
3. **xlisp-repl** (executable) - Interactive REPL

### Reentrant/Threading Build

```bash
make clean
make THREADS=1
```

Defines `XLISP_USE_CONTEXT` and links with `-lpthread` (on Unix).
Enables `thread-create`, `thread-join`, mutexes, condition variables, and channels.

### Legacy Support

- `Makefile` - Traditional make-based build
- `.dsp/.dsw` - Historical Visual Studio project files

## Type System

- `xlValue` - Universal Lisp value pointer (node-based)
- `xlFIXTYPE` - Fixed-point integers (long)
- `xlFLOTYPE` - Floating-point (double)
- `xlCHRTYPE` - Character (int)

## C API Patterns

```c
// Argument fetching
xlGetArg()
xlGetArgFixnum()
xlLastArg()  // Detects too many args

// Return values
xlMakeFixnum()
xlCons()

// Class definition
xlClass()
```

## Platform Abstraction

- `msstuff.c` - Windows-specific implementation
- `unstuff.c` - Unix-specific implementation
- ANSI C for maximum portability
- Thread-local storage: `__thread` (GCC/Clang), `__declspec(thread)` (MSVC), pthread keys (fallback)
- Threading: pthreads (Unix), Win32 threads (Windows)
