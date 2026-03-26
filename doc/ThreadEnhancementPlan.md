# Thread Enhancement Plan: Message Channels

## Status

Threads, mutexes, and condition variables are implemented and tested.
This document covers the remaining planned enhancement: message channels
for inter-thread communication.

## Motivation

Since threads have independent Lisp heaps, they cannot share Lisp objects
directly. Mutexes and condition variables provide synchronization but not
data exchange. Channels provide a thread-safe queue for passing
string-serialized data between threads.

## Channel API

| Function | Signature | Returns |
|---|---|---|
| `CHANNEL-CREATE` | `(channel-create [name] [capacity])` | channel handle |
| `CHANNEL-SEND` | `(channel-send channel string)` | `#t` |
| `CHANNEL-RECEIVE` | `(channel-receive channel)` | string, or `#f` on closed empty channel |
| `CHANNEL-CLOSE` | `(channel-close channel)` | `#t` |
| `CHANNEL-DESTROY` | `(channel-destroy channel)` | `#t` |
| `CHANNEL-LOOKUP` | `(channel-lookup name)` | channel handle or `#f` |
| `CHANNEL-OPEN?` | `(channel-open? channel)` | `#t` / `#f` |
| `CHANNEL?` | `(channel? obj)` | `#t` / `#f` |

Total: 8 new built-in functions.

### Semantics

- **`channel-create`**: Creates a channel. An optional *name* registers
  it globally for cross-thread lookup. An optional *capacity* (integer)
  limits the queue size; 0 or omitted means unbounded.

- **`channel-send`**: Enqueues a string. Blocks if the channel is
  bounded and full. Signals an error if the channel is closed.

- **`channel-receive`**: Dequeues a string. Blocks if the channel is
  empty and still open. Returns `#f` if the channel is closed and empty
  (no more data will arrive).

- **`channel-close`**: Marks the channel as closed. Pending messages can
  still be received. Wakes all blocked receivers so they can see the
  closed state.

- **`channel-destroy`**: Frees the underlying resources. Decrements the
  reference count; actual cleanup occurs when all references are released.

- **`channel-lookup`**: Finds a named channel in the global registry.
  Returns `#f` if not found.

## C Data Structure

```c
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
```

Tagged with a static `xlCClass channelTag` sentinel for type
discrimination via `channel?`, consistent with the existing mutex and
condition variable tagging.

## Cross-Thread Sharing

Channels use the same named-registry pattern already implemented for
mutexes and condition variables:

1. Creating thread calls `(channel-create "name")` — allocates the OS
   objects, registers globally by name.
2. Child thread calls `(channel-lookup "name")` — finds the C struct in
   the registry, wraps it in a new foreign pointer in the child's heap.
3. Both threads' foreign pointers point to the **same** underlying
   `xlChannelHandle`.
4. Reference counting prevents use-after-free.

## Usage Examples

### Simple Producer/Consumer

```lisp
(define ch (channel-create "work-queue"))

;; Producer thread
(define h (thread-create
  "(begin
     (define ch (channel-lookup \"work-queue\"))
     (channel-send ch \"hello from thread\")
     (channel-send ch \"another message\")
     (channel-close ch))"
  #f))

;; Consumer (main thread)
(display (channel-receive ch))    ; => "hello from thread"
(display (channel-receive ch))    ; => "another message"
(display (channel-receive ch))    ; => #f (closed, empty)

(thread-join h)
(channel-destroy ch)
```

### Bounded Channel (Backpressure)

```lisp
(define ch (channel-create "bounded" 10))  ; max 10 pending messages

;; Fast producer blocks when queue is full
(define h (thread-create
  "(begin
     (define ch (channel-lookup \"bounded\"))
     (define (produce n)
       (if (<= n 100)
         (begin
           (channel-send ch (number->string n))
           (produce (+ n 1)))))
     (produce 1)
     (channel-close ch))"
  #f))

;; Slow consumer
(define (consume)
  (let ((msg (channel-receive ch)))
    (if msg
      (begin (display msg) (newline) (consume)))))
(consume)

(thread-join h)
(channel-destroy ch)
```

### Multiple Producers

```lisp
(define ch (channel-create "results"))

(define h1 (thread-create
  "(begin
     (define ch (channel-lookup \"results\"))
     (channel-send ch \"result-from-1\"))" #f))

(define h2 (thread-create
  "(begin
     (define ch (channel-lookup \"results\"))
     (channel-send ch \"result-from-2\"))" #f))

(display (channel-receive ch)) (newline)
(display (channel-receive ch)) (newline)

(thread-join h1)
(thread-join h2)
(channel-destroy ch)
```

## File Changes

All changes are in existing files — no new source files needed.

| File | Change |
|------|--------|
| `src/xlsync.c` | Add channel implementation (8 functions) |
| `include/xlisp.h` | Add 8 function declarations |
| `src/xlftab.c` | Add 8 entries to `subrtab` |
| `doc/xlisp.md` | Add channel documentation section |
| `doc/Threads.md` | Update with channel information |

## Implementation Notes

- Channels are implemented entirely in `src/xlsync.c`, extending the
  existing synchronization module.
- The named registry, reference counting, and type-tagging infrastructure
  already exists and will be reused.
- The `notEmpty` / `notFull` condition variables are internal to the
  channel (not exposed to Lisp). They implement blocking send/receive
  without requiring the user to manage separate mutexes and condvars.
- All send/receive operations copy data as C strings (`malloc`/`free`),
  since the sender's and receiver's Lisp heaps are independent.

## Error Handling

Follows existing XLISP conventions:

- Wrong argument type: `xlBadType(arg)`
- Wrong argument count: `xlLastArg()` / `xlGetArg*()` macros
- Operational errors: `xlFmtError("channel-xxx: description")`
- Non-reentrant build: `xlFmtError("channel-xxx: requires reentrant build (THREADS=1)")`
- Use after destroy: `xlFmtError("channel-xxx: channel has been destroyed")`
- Send to closed channel: `xlFmtError("channel-send: channel is closed")`

## Testing Plan

- Single-producer, single-consumer
- Multiple producers, single consumer
- Single producer, multiple consumers
- Bounded channel with backpressure
- Close while receivers are blocked (should unblock and return `#f`)
- Close while senders are blocked on full bounded channel (should error)
- Destroy while other threads hold references (refcount prevents crash)
- 50+ iteration stress runs
