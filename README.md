# XLISP 3

XLISP3 is a fork of David Betz's XLISP3.

The home for this fork is at <https://github.com/blakemcbride/XLISP3>

XLISP3 is better described as Scheme than Lisp since it more closely follows Scheme.

It has some really nice features as follows:

1. It has a byte-code compiler (to FASL files) so code runs reasonably fast.
2. It can load Lisp source files or compiled FASL files.
3. It has an object system with classes, methods, inheritance, and `super` calls.
4. The macro system is traditional Lisp-style (not Scheme's hygienic macros).
5. It can be used as an extension language embedded in C programs.
6. It can save/load workspace images.
7. It correctly handles tail recursion (proper tail call optimization).
8. It has a Common Lisp-style package system.
9. It supports multiple return values.
10. It has first-class continuations (call/cc).

## To all of this, I have added:

A. When used as an extension language, it is now reentrant and can handle multiple simultaneous threads.

To this, I would like to add native thread support at some time.

## Building

    make                 # standard build
    make REENTRANT=1     # thread-safe build
    make clean           # remove build artifacts

## The original README file is located at README2.md

Blake McBride
blake@mcbridemail.com


