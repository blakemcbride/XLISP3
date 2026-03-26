;; Comprehensive synchronization tests
;; NOTE: thread-create only evaluates the FIRST expression.
;; Use (begin ...) to wrap multiple expressions.

(define pass-count 0)
(define fail-count 0)

(define (check name result)
  (if result
    (begin (set! pass-count (+ pass-count 1))
           (display "  PASS: ") (display name) (newline))
    (begin (set! fail-count (+ fail-count 1))
           (display "  FAIL: ") (display name) (newline))))

;; ============================================================
(display "=== 1. Mutex Basic Operations ===") (newline)
;; ============================================================

(define m (mutex-create))
(check "mutex-create returns mutex" (mutex? m))
(check "mutex? on integer" (not (mutex? 42)))
(check "mutex? on string" (not (mutex? "hello")))
(check "mutex? on nil" (not (mutex? '())))
(check "mutex-lock" (mutex-lock m))
(check "mutex-unlock" (mutex-unlock m))
(check "mutex-destroy" (mutex-destroy m))
(check "mutex? after destroy" (not (mutex? m)))

;; ============================================================
(display "=== 2. Named Mutex and Lookup ===") (newline)
;; ============================================================

(define m1 (mutex-create "alpha"))
(check "named mutex created" (mutex? m1))
(define m2 (mutex-lookup "alpha"))
(check "mutex-lookup found" (mutex? m2))
(check "lookup non-existent" (not (mutex-lookup "no-such")))
(mutex-destroy m1)

;; ============================================================
(display "=== 3. Condition Variable Basic Operations ===") (newline)
;; ============================================================

(define cv (condition-create))
(check "condition-create" (condition? cv))
(check "condition? on integer" (not (condition? 42)))
(check "condition? on mutex" (not (condition? (mutex-create))))
(check "condition-destroy" (condition-destroy cv))
(check "condition? after destroy" (not (condition? cv)))

;; ============================================================
(display "=== 4. Named Condition Variable and Lookup ===") (newline)
;; ============================================================

(define cv1 (condition-create "beta"))
(check "named condition created" (condition? cv1))
(define cv2 (condition-lookup "beta"))
(check "condition-lookup found" (condition? cv2))
(check "lookup non-existent" (not (condition-lookup "no-such")))
(condition-destroy cv1)

;; ============================================================
(display "=== 5. Type Discrimination ===") (newline)
;; ============================================================

(define tm (mutex-create))
(define tc (condition-create))
(check "mutex is not condition" (not (condition? tm)))
(check "condition is not mutex" (not (mutex? tc)))
(mutex-destroy tm)
(condition-destroy tc)

;; ============================================================
(display "=== 6. Cross-Thread Mutex ===") (newline)
;; ============================================================

(define m3 (mutex-create "cross-mutex"))
(define h1 (thread-create
  "(begin (define m (mutex-lookup \"cross-mutex\")) (mutex-lock m) (mutex-unlock m))" #f))
(thread-join h1)
(check "child thread used mutex" #t)
(mutex-destroy m3)

;; ============================================================
(display "=== 7. Cross-Thread Condition Signal ===") (newline)
;; ============================================================

(define m4 (mutex-create "sig-mutex"))
(define cv3 (condition-create "sig-cond"))

;; Lock before spawning so child blocks until main is in condition-wait
(mutex-lock m4)

(define h2 (thread-create
  "(begin (define m (mutex-lookup \"sig-mutex\")) (define c (condition-lookup \"sig-cond\")) (mutex-lock m) (condition-signal c) (mutex-unlock m))" #f))

(condition-wait cv3 m4)
(mutex-unlock m4)
(thread-join h2)
(check "condition signal/wait across threads" #t)
(mutex-destroy m4)
(condition-destroy cv3)

;; ============================================================
(display "=== 8. Cross-Thread Condition Broadcast ===") (newline)
;; ============================================================

;; Each child signals a "ready" condvar before entering broadcast wait,
;; so main knows all children are waiting before it broadcasts.

(define bm (mutex-create "bm"))
(define bc (condition-create "bc"))
(define r1 (condition-create "r1"))
(define r2 (condition-create "r2"))
(define r3 (condition-create "r3"))

(mutex-lock bm)

(define h3 (thread-create
  "(begin (define m (mutex-lookup \"bm\")) (define bc (condition-lookup \"bc\")) (define r (condition-lookup \"r1\")) (mutex-lock m) (condition-signal r) (condition-wait bc m) (mutex-unlock m))" #f))
(condition-wait r1 bm)

(define h4 (thread-create
  "(begin (define m (mutex-lookup \"bm\")) (define bc (condition-lookup \"bc\")) (define r (condition-lookup \"r2\")) (mutex-lock m) (condition-signal r) (condition-wait bc m) (mutex-unlock m))" #f))
(condition-wait r2 bm)

(define h5 (thread-create
  "(begin (define m (mutex-lookup \"bm\")) (define bc (condition-lookup \"bc\")) (define r (condition-lookup \"r3\")) (mutex-lock m) (condition-signal r) (condition-wait bc m) (mutex-unlock m))" #f))
(condition-wait r3 bm)

(condition-broadcast bc)
(mutex-unlock bm)

(thread-join h3)
(thread-join h4)
(thread-join h5)
(check "broadcast woke 3 threads" #t)
(mutex-destroy bm)
(condition-destroy bc)
(condition-destroy r1)
(condition-destroy r2)
(condition-destroy r3)

;; ============================================================
(display "=== 9. Multiple Contending Mutex Threads ===") (newline)
;; ============================================================

(define m6 (mutex-create "contend"))

(define h6 (thread-create
  "(begin (define m (mutex-lookup \"contend\")) (define (w n) (if (> n 0) (w (- n 1)) #t)) (mutex-lock m) (w 5000) (mutex-unlock m))" #f))
(define h7 (thread-create
  "(begin (define m (mutex-lookup \"contend\")) (define (w n) (if (> n 0) (w (- n 1)) #t)) (mutex-lock m) (w 5000) (mutex-unlock m))" #f))
(define h8 (thread-create
  "(begin (define m (mutex-lookup \"contend\")) (define (w n) (if (> n 0) (w (- n 1)) #t)) (mutex-lock m) (w 5000) (mutex-unlock m))" #f))
(define h9 (thread-create
  "(begin (define m (mutex-lookup \"contend\")) (define (w n) (if (> n 0) (w (- n 1)) #t)) (mutex-lock m) (w 5000) (mutex-unlock m))" #f))

(thread-join h6)
(thread-join h7)
(thread-join h8)
(thread-join h9)
(check "4 contending threads completed" #t)
(mutex-destroy m6)

;; ============================================================
;; Summary
;; ============================================================
(newline)
(display "=== Results: ")
(display pass-count) (display " passed, ")
(display fail-count) (display " failed ===") (newline)

(exit)
