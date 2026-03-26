;; Comprehensive channel tests
;; Requires: make THREADS=1

(define pass-count 0)
(define fail-count 0)

(define (check name result)
  (if result
    (begin (set! pass-count (+ pass-count 1))
           (display "  PASS: ") (display name) (newline))
    (begin (set! fail-count (+ fail-count 1))
           (display "  FAIL: ") (display name) (newline))))

;; ============================================================
(display "=== 1. Channel Basic Operations ===") (newline)
;; ============================================================

(define ch (channel-create))
(check "channel-create returns channel" (channel? ch))
(check "channel? on integer" (not (channel? 42)))
(check "channel? on string" (not (channel? "hello")))
(check "channel? on mutex" (not (channel? (mutex-create))))
(check "channel-open? on new channel" (channel-open? ch))

;; Send and receive in same thread (unbounded channel)
(channel-send ch "hello")
(channel-send ch "world")
(check "receive first" (equal? "hello" (channel-receive ch)))
(check "receive second" (equal? "world" (channel-receive ch)))

;; Close and receive #f
(channel-close ch)
(check "channel-open? after close" (not (channel-open? ch)))
(check "receive on closed empty channel" (not (channel-receive ch)))

;; Destroy
(channel-destroy ch)
(check "channel? after destroy" (not (channel? ch)))

;; ============================================================
(display "=== 2. Named Channel and Lookup ===") (newline)
;; ============================================================

(define ch1 (channel-create "alpha"))
(check "named channel created" (channel? ch1))
(define ch2 (channel-lookup "alpha"))
(check "channel-lookup found" (channel? ch2))
(check "lookup non-existent" (not (channel-lookup "no-such")))

;; Both handles refer to the same channel
(channel-send ch1 "test")
(check "receive from looked-up handle" (equal? "test" (channel-receive ch2)))

(channel-destroy ch1)

;; ============================================================
(display "=== 3. Channel with Capacity ===") (newline)
;; ============================================================

(define ch3 (channel-create 3))
(check "bounded channel created" (channel? ch3))
(channel-send ch3 "a")
(channel-send ch3 "b")
(channel-send ch3 "c")
;; channel is now full (capacity 3)
;; can still receive
(check "receive from full" (equal? "a" (channel-receive ch3)))
(check "receive second" (equal? "b" (channel-receive ch3)))
(check "receive third" (equal? "c" (channel-receive ch3)))
(channel-destroy ch3)

;; Named + capacity
(define ch4 (channel-create "bounded" 5))
(check "named bounded channel" (channel? ch4))
(channel-destroy ch4)

;; ============================================================
(display "=== 4. Type Discrimination ===") (newline)
;; ============================================================

(define tm (mutex-create))
(define tc (condition-create))
(define tch (channel-create))

(check "channel is not mutex" (not (mutex? tch)))
(check "channel is not condition" (not (condition? tch)))
(check "mutex is not channel" (not (channel? tm)))
(check "condition is not channel" (not (channel? tc)))

(mutex-destroy tm)
(condition-destroy tc)
(channel-destroy tch)

;; ============================================================
(display "=== 5. Cross-Thread: Single Producer, Single Consumer ===") (newline)
;; ============================================================

(define ch5 (channel-create "spsc"))

;; Producer thread sends 5 messages then closes
(define h1 (thread-create
  "(begin
     (define ch (channel-lookup \"spsc\"))
     (channel-send ch \"msg-1\")
     (channel-send ch \"msg-2\")
     (channel-send ch \"msg-3\")
     (channel-send ch \"msg-4\")
     (channel-send ch \"msg-5\")
     (channel-close ch))"
  #f))

;; Consumer (main thread) receives all messages
(define msgs '())
(define (consume)
  (let ((msg (channel-receive ch5)))
    (if msg
      (begin (set! msgs (append msgs (list msg)))
             (consume)))))
(consume)

(check "received 5 messages" (= 5 (length msgs)))
(check "first message" (equal? "msg-1" (car msgs)))
(check "last message" (equal? "msg-5" (list-ref msgs 4)))

(thread-join h1)
(channel-destroy ch5)

;; ============================================================
(display "=== 6. Cross-Thread: Multiple Producers ===") (newline)
;; ============================================================

(define ch6 (channel-create "mpsc"))

(define h2 (thread-create
  "(begin
     (define ch (channel-lookup \"mpsc\"))
     (channel-send ch \"from-A\"))" #f))

(define h3 (thread-create
  "(begin
     (define ch (channel-lookup \"mpsc\"))
     (channel-send ch \"from-B\"))" #f))

(define h4 (thread-create
  "(begin
     (define ch (channel-lookup \"mpsc\"))
     (channel-send ch \"from-C\"))" #f))

;; Receive all 3
(define m1 (channel-receive ch6))
(define m2 (channel-receive ch6))
(define m3 (channel-receive ch6))

(check "got 3 messages" (and (string? m1) (string? m2) (string? m3)))

;; Verify all three sources are present (order may vary)
(define all-msgs (list m1 m2 m3))
(check "from-A present" (member "from-A" all-msgs))
(check "from-B present" (member "from-B" all-msgs))
(check "from-C present" (member "from-C" all-msgs))

(thread-join h2)
(thread-join h3)
(thread-join h4)
(channel-destroy ch6)

;; ============================================================
(display "=== 7. Cross-Thread: Consumer Blocks Until Data ===") (newline)
;; ============================================================

;; Main thread will block on receive; child sends after a delay.
(define ch7 (channel-create "blocking"))
(define mtx (mutex-create "blk-m"))
(define ready-cv (condition-create "blk-r"))

;; Lock before spawning so child blocks until main is ready
(mutex-lock mtx)

(define h5 (thread-create
  "(begin
     (define ch (channel-lookup \"blocking\"))
     (define m (mutex-lookup \"blk-m\"))
     (define r (condition-lookup \"blk-r\"))
     (mutex-lock m)
     (condition-signal r)
     (mutex-unlock m)
     (channel-send ch \"delayed-msg\"))"
  #f))

;; Wait for child to be running
(condition-wait ready-cv mtx)
(mutex-unlock mtx)

;; Now receive (child may still be about to send)
(define result (channel-receive ch7))
(check "blocking receive got message" (equal? "delayed-msg" result))

(thread-join h5)
(channel-destroy ch7)
(mutex-destroy mtx)
(condition-destroy ready-cv)

;; ============================================================
(display "=== 8. Close Unblocks Waiting Receivers ===") (newline)
;; ============================================================

(define ch8 (channel-create "close-test"))
(define mtx2 (mutex-create "cl-m"))
(define ready2 (condition-create "cl-r"))

(mutex-lock mtx2)

;; Child will close the channel after signaling ready
(define h6 (thread-create
  "(begin
     (define ch (channel-lookup \"close-test\"))
     (define m (mutex-lookup \"cl-m\"))
     (define r (condition-lookup \"cl-r\"))
     (mutex-lock m)
     (condition-signal r)
     (mutex-unlock m)
     (channel-close ch))"
  #f))

(condition-wait ready2 mtx2)
(mutex-unlock mtx2)

;; Receive should return #f because channel is/will-be closed and empty
(define close-result (channel-receive ch8))
(check "receive returns #f on close" (not close-result))

(thread-join h6)
(channel-destroy ch8)
(mutex-destroy mtx2)
(condition-destroy ready2)

;; ============================================================
(display "=== 9. Bounded Channel: Producer Blocks When Full ===") (newline)
;; ============================================================

;; Bounded channel of capacity 2.
;; Producer sends 5 messages (must block after 2 until consumer drains).
(define ch9 (channel-create "bounded-test" 2))

(define h7 (thread-create
  "(begin
     (define ch (channel-lookup \"bounded-test\"))
     (channel-send ch \"b1\")
     (channel-send ch \"b2\")
     (channel-send ch \"b3\")
     (channel-send ch \"b4\")
     (channel-send ch \"b5\")
     (channel-close ch))"
  #f))

(define bounded-msgs '())
(define (consume-bounded)
  (let ((msg (channel-receive ch9)))
    (if msg
      (begin (set! bounded-msgs (append bounded-msgs (list msg)))
             (consume-bounded)))))
(consume-bounded)

(check "bounded: received 5 messages" (= 5 (length bounded-msgs)))
(check "bounded: correct order" (equal? "b1" (car bounded-msgs)))
(check "bounded: last message" (equal? "b5" (list-ref bounded-msgs 4)))

(thread-join h7)
(channel-destroy ch9)

;; ============================================================
(display "=== 10. Receive Pending Messages After Close ===") (newline)
;; ============================================================

(define ch10 (channel-create))
(channel-send ch10 "pre-close-1")
(channel-send ch10 "pre-close-2")
(channel-close ch10)

(check "receive after close 1" (equal? "pre-close-1" (channel-receive ch10)))
(check "receive after close 2" (equal? "pre-close-2" (channel-receive ch10)))
(check "receive after close empty" (not (channel-receive ch10)))
(channel-destroy ch10)

;; ============================================================
;; Summary
;; ============================================================
(newline)
(display "=== Results: ")
(display pass-count) (display " passed, ")
(display fail-count) (display " failed ===") (newline)

(exit)
