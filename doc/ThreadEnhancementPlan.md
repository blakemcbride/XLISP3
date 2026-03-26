# Thread Enhancement Plan

## Status: Complete

All planned threading features have been implemented and tested:

### Low-Level Primitives (C)

- **Threads** (`thread-create`, `thread-join`, `thread?`) -- `src/xlnthread.c`
- **Mutexes** (`mutex-create`, `mutex-lock`, `mutex-unlock`, `mutex-destroy`, `mutex-lookup`, `mutex?`) -- `src/xlsync.c`
- **Condition variables** (`condition-create`, `condition-wait`, `condition-signal`, `condition-broadcast`, `condition-destroy`, `condition-lookup`, `condition?`) -- `src/xlsync.c`
- **Channels** (`channel-create`, `channel-send`, `channel-receive`, `channel-close`, `channel-destroy`, `channel-lookup`, `channel-open?`, `channel?`) -- `src/xlsync.c`

Total: 24 threading/synchronization functions.

All primitives share a common named-registry pattern for cross-thread
access, use `xlCClass` type tags for foreign pointer discrimination, and
support both POSIX and Windows platforms.

### High-Level Utilities (Lisp)

- **with-mutex** -- Safe lock/unlock macro with unwind-protect cleanup
- **future / await / future?** -- Spawn computation, retrieve result later
- **pcall** -- Run N expressions concurrently, collect results in order
- **thread-pool** (`thread-pool-create`, `thread-pool-submit`, `thread-pool-destroy`, `thread-pool?`) -- Persistent worker pool
- **pmap** -- Parallel map with template substitution

All defined in `threads.lsp`.

## Test Results

- `tests/test_sync.lsp` -- 25 tests (mutexes, condvars, cross-thread)
- `tests/test_channel.lsp` -- 38 tests (channels, bounded, multi-producer, blocking)
- `tests/test_threads.lsp` -- 47 tests (with-mutex, future/await, pcall, thread-pool, pmap, stress, error handling)
- Stress tested: 0 errors in 100+ iterations per suite

## Documentation

- `doc/Threads.md` -- Complete threading reference
- `doc/ThreadEnhancements.md` -- High-level utilities implementation plan
- `doc/xlisp.md` sections 49-52 -- Lisp-level API reference
