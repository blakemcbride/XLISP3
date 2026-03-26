;; Comprehensive tests for threads.lsp high-level threading utilities
;; Requires: make THREADS=1
;; Run with: bin/xlisp threads.lsp tests/test_threads.lsp

(load "threads.lsp")

(define pass-count 0)
(define fail-count 0)

(define (check name result)
  (if result
    (begin (set! pass-count (+ pass-count 1))
           (display "  PASS: ") (display name) (newline))
    (begin (set! fail-count (+ fail-count 1))
           (display "  FAIL: ") (display name) (newline))))

;; ============================================================
(display "=== 1. with-mutex ===") (newline)
;; ============================================================

(define m1 (mutex-create))

;; Basic usage: returns value of body
(check "with-mutex returns body value"
  (= 42 (with-mutex m1 (* 6 7))))

;; Multiple body forms
(check "with-mutex multiple body forms"
  (equal? "hello"
    (with-mutex m1
      (+ 1 2)
      "hello")))

;; Mutex is released after with-mutex
;; (if it weren't, the second lock would deadlock)
(mutex-lock m1)
(mutex-unlock m1)
(check "mutex released after with-mutex" #t)

;; Nested with-mutex (different mutexes)
(define m2 (mutex-create))
(check "nested with-mutex"
  (= 100 (with-mutex m1 (with-mutex m2 (* 10 10)))))

;; Error in body: mutex is still released (unwind-protect)
(define m3 (mutex-create))
(define error-caught #f)
(catch 'error
  (with-mutex m3
    (throw-error "test error")))
(set! error-caught #t)
;; If mutex were still locked, this lock/unlock would deadlock
(mutex-lock m3)
(mutex-unlock m3)
(check "mutex released after error in body" error-caught)
(mutex-destroy m3)

(mutex-destroy m1)
(mutex-destroy m2)

;; ============================================================
(display "=== 2. future / await ===") (newline)
;; ============================================================

;; Basic future
(define f1 (future "(number->string (* 6 7))" #f))
(check "future? on future" (future? f1))
(check "future? on non-future" (not (future? 42)))
(check "future? on list" (not (future? '(1 2 3))))

(define r1 (await f1))
(check "await returns correct result" (equal? "42" r1))

;; Future returning a string directly
(define f2 (future "\"hello world\"" #f))
(check "await string result" (equal? "hello world" (await f2)))

;; Multiple concurrent futures
(define fa (future "(number->string (+ 100 1))" #f))
(define fb (future "(number->string (+ 200 2))" #f))
(define fc (future "(number->string (+ 300 3))" #f))
(define ra (await fa))
(define rb (await fb))
(define rc (await fc))
(check "concurrent future A" (equal? "101" ra))
(check "concurrent future B" (equal? "202" rb))
(check "concurrent future C" (equal? "303" rc))

;; Future with computation
(define f3 (future
  "(begin (define (fact n) (if (< n 2) 1 (* n (fact (- n 1))))) (number->string (fact 10)))"
  #f))
(check "future with computation" (equal? "3628800" (await f3)))

;; future? returns #f after await (invalidated)
(define f4 (future "(number->string 1)" #f))
(await f4)
(check "future? after await" (not (future? f4)))

;; Error in future is propagated to await
(define f5 (future "(+ 1 undefined-variable-xyz)" #f))
(define future-error-caught #f)
(catch 'error
  (await f5))
(set! future-error-caught #t)
(check "error in future propagates to await" future-error-caught)

;; ============================================================
(display "=== 3. pcall ===") (newline)
;; ============================================================

;; Basic pcall
(define pc-results
  (pcall "(number->string (* 2 3))"
         "(number->string (* 4 5))"
         "(number->string (* 6 7))"))
(check "pcall returns 3 results" (= 3 (length pc-results)))
(check "pcall first" (equal? "6" (car pc-results)))
(check "pcall second" (equal? "20" (cadr pc-results)))
(check "pcall third" (equal? "42" (caddr pc-results)))

;; pcall with single expression
(define pc-single (pcall "(number->string 99)"))
(check "pcall single" (equal? "99" (car pc-single)))

;; pcall preserves order even if later tasks finish first
;; (a slow task first, fast task second)
(define pc-order
  (pcall "(begin (define (spin n) (if (= n 0) 0 (spin (- n 1)))) (spin 10000) (number->string 1))"
         "(number->string 2)"))
(check "pcall preserves order" (equal? "1" (car pc-order)))
(check "pcall preserves order 2" (equal? "2" (cadr pc-order)))

;; ============================================================
(display "=== 4. thread-pool ===") (newline)
;; ============================================================

(define pool (thread-pool-create 2 #f))
(check "thread-pool? on pool" (thread-pool? pool))
(check "thread-pool? on non-pool" (not (thread-pool? 42)))
(check "thread-pool? on future" (not (thread-pool? (list '%future #f #f #f))))

;; Submit single task
(define pf1 (thread-pool-submit pool "(number->string (+ 10 20))"))
(check "pool submit returns future" (future? pf1))
(check "pool task result" (equal? "30" (await pf1)))

;; Submit multiple tasks (more than pool size)
(define pf2 (thread-pool-submit pool "(number->string 1)"))
(define pf3 (thread-pool-submit pool "(number->string 2)"))
(define pf4 (thread-pool-submit pool "(number->string 3)"))
(define pf5 (thread-pool-submit pool "(number->string 4)"))
(define pr2 (await pf2))
(define pr3 (await pf3))
(define pr4 (await pf4))
(define pr5 (await pf5))
(check "pool queued task 1" (equal? "1" pr2))
(check "pool queued task 2" (equal? "2" pr3))
(check "pool queued task 3" (equal? "3" pr4))
(check "pool queued task 4" (equal? "4" pr5))

;; Pool with computation
(define pf6 (thread-pool-submit pool
  "(begin (define (fib n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2))))) (number->string (fib 15)))"))
(check "pool computation" (equal? "610" (await pf6)))

(thread-pool-destroy pool)
(check "pool destroyed" #t)

;; ============================================================
(display "=== 5. pmap ===") (newline)
;; ============================================================

;; pmap with futures (no pool)
(define pm1 (pmap "(number->string (* 2 ~a))" '("5" "10" "15")))
(check "pmap length" (= 3 (length pm1)))
(check "pmap first" (equal? "10" (car pm1)))
(check "pmap second" (equal? "20" (cadr pm1)))
(check "pmap third" (equal? "30" (caddr pm1)))

;; pmap with pool
(define pool2 (thread-pool-create 2 #f))
(define pm2 (pmap "(number->string (* 3 ~a))" '("4" "5" "6") pool2))
(check "pmap pool first" (equal? "12" (car pm2)))
(check "pmap pool second" (equal? "15" (cadr pm2)))
(check "pmap pool third" (equal? "18" (caddr pm2)))
(thread-pool-destroy pool2)

;; pmap with single element
(define pm3 (pmap "(number->string (+ 1 ~a))" '("99")))
(check "pmap single element" (equal? "100" (car pm3)))

;; pmap with empty list
(define pm4 (pmap "(number->string ~a)" '()))
(check "pmap empty list" (null? pm4))

;; ============================================================
(display "=== 6. Stress: pool with many tasks ===") (newline)
;; ============================================================

(define pool3 (thread-pool-create 4 #f))
(define stress-futures '())

;; Submit 20 tasks to a 4-worker pool
(let loop ((i 0))
  (if (< i 20)
    (let* ((expr (string-append "(number->string (* 2 " (number->string i) "))"))
           (f (thread-pool-submit pool3 expr)))
      (set! stress-futures (cons f stress-futures))
      (loop (+ i 1)))))
(set! stress-futures (reverse stress-futures))

;; Collect all results
(define stress-results (map await stress-futures))
(check "stress: got 20 results" (= 20 (length stress-results)))
(check "stress: first result" (equal? "0" (car stress-results)))
(check "stress: last result" (equal? "38" (list-ref stress-results 19)))

;; Verify all results are correct
(define stress-ok
  (let loop ((i 0) (results stress-results))
    (if (null? results)
      #t
      (if (equal? (number->string (* 2 i)) (car results))
        (loop (+ i 1) (cdr results))
        #f))))
(check "stress: all results correct" stress-ok)

(thread-pool-destroy pool3)

;; ============================================================
;; Summary
;; ============================================================
(newline)
(display "=== Results: ")
(display pass-count) (display " passed, ")
(display fail-count) (display " failed ===") (newline)

(exit)
