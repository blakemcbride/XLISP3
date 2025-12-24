# XLISP Codebase Analysis

## Overview

**XLISP** is an object-oriented LISP interpreter/compiler (v3.3) by David Michael Betz (1983-2017). Licensed under MIT. It compiles Lisp to bytecodes rather than interpreting directly, making it faster than traditional interpreters.

## Project Structure

```
xlisp/
├── src/           # Core C source (27 files, ~18.8K lines)
├── include/       # Header files
├── xlisp/         # REPL executable source
├── ext/           # Extension module
├── doc/           # Documentation (Markdown)
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

## Notable Features

- **Bytecode compilation** - not direct interpretation
- **Generational garbage collection** with protected pointers
- **Class-based OOP** with inheritance and method dispatch
- **Scheme influences** - lexical scoping, proper tail recursion
- **Common Lisp elements** - packages, keywords, multiple values
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
