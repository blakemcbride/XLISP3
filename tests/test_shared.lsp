;; Exhaustive tests for the shared bytecode pool feature
;; Requires: make THREADS=1
;; Run with: bin/xlisp tests/test_shared.lsp

(define pass-count 0)
(define fail-count 0)

(define (check name result)
  (if result
    (begin (set! pass-count (+ pass-count 1))
           (display "  PASS: ") (display name) (newline))
    (begin (set! fail-count (+ fail-count 1))
           (display "  FAIL: ") (display name) (newline))))

;; ============================================================
(display "=== 1. shared-code? predicate (empty pool) ===") (newline)
;; ============================================================

(check "shared-code? initially #f" (not (shared-code?)))
(check "shared-code? returns boolean" (eq? #f (shared-code?)))

;; ============================================================
(display "=== 2. share-function basic operations ===") (newline)
;; ============================================================

;; Simple function
(define (add1 n) (+ n 1))
(check "share-function returns #t" (eq? #t (share-function 'add1)))
(check "shared-code? after share" (shared-code?))

;; Original function still works in main thread
(check "original add1 still works" (= 6 (add1 5)))

;; Share another function
(define (double n) (* 2 n))
(share-function 'double)
(check "double still works after sharing" (= 20 (double 10)))

;; ============================================================
(display "=== 3. share-function error handling ===") (newline)
;; ============================================================

;; Sharing a non-closure should signal an error
(define not-a-closure 42)
(define share-error-caught #f)
(catch 'error
  (share-function 'not-a-closure))
(set! share-error-caught #t)
(check "share-function rejects non-closure" share-error-caught)

;; Sharing an unbound symbol should signal an error
(define share-unbound-error #f)
(catch 'error
  (share-function 'this-symbol-is-not-bound-anywhere))
(set! share-unbound-error #t)
(check "share-function rejects unbound symbol" share-unbound-error)

;; ============================================================
(display "=== 4. Basic thread with shared code ===") (newline)
;; ============================================================

;; Child thread should have add1 available without defining it
(define h1 (thread-create "(number->string (add1 99))" #f))
(check "thread with shared add1 joins" (thread-join h1))

;; Child thread should have double available
(define h2 (thread-create "(number->string (double 21))" #f))
(check "thread with shared double joins" (thread-join h2))

;; ============================================================
(display "=== 5. Verify shared code produces correct results ===") (newline)
;; ============================================================

;; Use a channel to get the result back from the child thread
(define ch1 (channel-create "result1"))

(define h3 (thread-create
  "(begin (define ch (channel-lookup \"result1\")) (channel-send ch (number->string (add1 41))))"
  #f))
(define r3 (channel-receive ch1))
(check "shared add1 returns correct value" (equal? "42" r3))
(thread-join h3)
(channel-destroy ch1)

(define ch2 (channel-create "result2"))
(define h4 (thread-create
  "(begin (define ch (channel-lookup \"result2\")) (channel-send ch (number->string (double 25))))"
  #f))
(define r4 (channel-receive ch2))
(check "shared double returns correct value" (equal? "50" r4))
(thread-join h4)
(channel-destroy ch2)

;; ============================================================
(display "=== 6. Shared function with multiple arguments ===") (newline)
;; ============================================================

(define (add a b) (+ a b))
(share-function 'add)

(define ch3 (channel-create "result3"))
(define h5 (thread-create
  "(begin (define ch (channel-lookup \"result3\")) (channel-send ch (number->string (add 17 25))))"
  #f))
(check "shared multi-arg: correct result" (equal? "42" (channel-receive ch3)))
(thread-join h5)
(channel-destroy ch3)

;; ============================================================
(display "=== 7. Shared function with various literal types ===") (newline)
;; ============================================================

;; Function that uses string literals
(define (greet name) (string-append "Hello, " name "!"))
(share-function 'greet)

(define ch4 (channel-create "result4"))
(define h6 (thread-create
  "(begin (define ch (channel-lookup \"result4\")) (channel-send ch (greet \"World\")))"
  #f))
(check "shared fn with string literal" (equal? "Hello, World!" (channel-receive ch4)))
(thread-join h6)
(channel-destroy ch4)

;; Function that uses fixnum literals
(define (add-hundred n) (+ n 100))
(share-function 'add-hundred)

(define ch5 (channel-create "result5"))
(define h7 (thread-create
  "(begin (define ch (channel-lookup \"result5\")) (channel-send ch (number->string (add-hundred 23))))"
  #f))
(check "shared fn with fixnum literal" (equal? "123" (channel-receive ch5)))
(thread-join h7)
(channel-destroy ch5)

;; Function that uses flonum literals
(define (add-pi n) (+ n 3.14159))
(share-function 'add-pi)

(define ch6 (channel-create "result6"))
(define h8 (thread-create
  "(begin (define ch (channel-lookup \"result6\")) (channel-send ch (number->string (add-pi 0.0))))"
  #f))
(define r6 (channel-receive ch6))
(check "shared fn with flonum literal" (equal? "3.14159" r6))
(thread-join h8)
(channel-destroy ch6)

;; Function that uses character literals
(define (char-test) (string #\A #\B #\C))
(share-function 'char-test)

(define ch6b (channel-create "result6b"))
(define h8b (thread-create
  "(begin (define ch (channel-lookup \"result6b\")) (channel-send ch (char-test)))"
  #f))
(check "shared fn with character literals" (equal? "ABC" (channel-receive ch6b)))
(thread-join h8b)
(channel-destroy ch6b)

;; Function that uses boolean literals
(define (bool-test x) (if (> x 0) #t #f))
(share-function 'bool-test)

(define ch6c (channel-create "result6c"))
(define h8c (thread-create
  "(begin (define ch (channel-lookup \"result6c\")) (channel-send ch (if (bool-test 5) \"yes\" \"no\")))"
  #f))
(check "shared fn with #t literal" (equal? "yes" (channel-receive ch6c)))
(thread-join h8c)
(channel-destroy ch6c)

(define ch6d (channel-create "result6d"))
(define h8d (thread-create
  "(begin (define ch (channel-lookup \"result6d\")) (channel-send ch (if (bool-test -1) \"yes\" \"no\")))"
  #f))
(check "shared fn with #f result" (equal? "no" (channel-receive ch6d)))
(thread-join h8d)
(channel-destroy ch6d)

;; Function that uses nil
(define (nil-test) (if (null? '()) "nil" "not-nil"))
(share-function 'nil-test)

(define ch6e (channel-create "result6e"))
(define h8e (thread-create
  "(begin (define ch (channel-lookup \"result6e\")) (channel-send ch (nil-test)))"
  #f))
(check "shared fn with nil literal" (equal? "nil" (channel-receive ch6e)))
(thread-join h8e)
(channel-destroy ch6e)

;; ============================================================
(display "=== 8. Recursive shared function ===") (newline)
;; ============================================================

(define (fact n) (if (< n 2) 1 (* n (fact (- n 1)))))
(share-function 'fact)

(define ch7 (channel-create "result7"))
(define h9 (thread-create
  "(begin (define ch (channel-lookup \"result7\")) (channel-send ch (number->string (fact 10))))"
  #f))
(check "shared recursive fn" (equal? "3628800" (channel-receive ch7)))
(thread-join h9)
(channel-destroy ch7)

;; Fibonacci
(define (fib n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))
(share-function 'fib)

(define ch8 (channel-create "result8"))
(define h10 (thread-create
  "(begin (define ch (channel-lookup \"result8\")) (channel-send ch (number->string (fib 20))))"
  #f))
(check "shared fibonacci fn" (equal? "6765" (channel-receive ch8)))
(thread-join h10)
(channel-destroy ch8)

;; ============================================================
(display "=== 9. Shared function with nested code (lambda) ===") (newline)
;; ============================================================

(define (make-adder n) (lambda (x) (+ n x)))
(share-function 'make-adder)

(define ch9 (channel-create "result9"))
(define h11 (thread-create
  "(begin (define ch (channel-lookup \"result9\")) (define add5 (make-adder 5)) (channel-send ch (number->string (add5 37))))"
  #f))
(check "shared fn returning closure" (equal? "42" (channel-receive ch9)))
(thread-join h11)
(channel-destroy ch9)

;; ============================================================
(display "=== 10. Shared function calling another shared function ===") (newline)
;; ============================================================

;; add1 and double are already shared
(define (add1-then-double n) (double (add1 n)))
(share-function 'add1-then-double)

(define ch10 (channel-create "result10"))
(define h12 (thread-create
  "(begin (define ch (channel-lookup \"result10\")) (channel-send ch (number->string (add1-then-double 20))))"
  #f))
(check "shared fn calls other shared fn" (equal? "42" (channel-receive ch10)))
(thread-join h12)
(channel-destroy ch10)

;; ============================================================
(display "=== 11. Multiple threads using same shared code ===") (newline)
;; ============================================================

(define ch11a (channel-create "result11a"))
(define ch11b (channel-create "result11b"))
(define ch11c (channel-create "result11c"))

(define h13a (thread-create
  "(begin (define ch (channel-lookup \"result11a\")) (channel-send ch (number->string (fact 8))))"
  #f))
(define h13b (thread-create
  "(begin (define ch (channel-lookup \"result11b\")) (channel-send ch (number->string (fact 9))))"
  #f))
(define h13c (thread-create
  "(begin (define ch (channel-lookup \"result11c\")) (channel-send ch (number->string (fact 10))))"
  #f))

(check "concurrent thread A using shared fn" (equal? "40320" (channel-receive ch11a)))
(check "concurrent thread B using shared fn" (equal? "362880" (channel-receive ch11b)))
(check "concurrent thread C using shared fn" (equal? "3628800" (channel-receive ch11c)))

(thread-join h13a)
(thread-join h13b)
(thread-join h13c)
(channel-destroy ch11a)
(channel-destroy ch11b)
(channel-destroy ch11c)

;; ============================================================
(display "=== 12. Shared code with list argument (thread-create) ===") (newline)
;; ============================================================

;; thread-create also accepts a list form
(define ch12 (channel-create "result12"))
(define h14 (thread-create
  '(begin (define ch (channel-lookup "result12")) (channel-send ch (number->string (add1 99))))
  #f))
(check "list-form thread-create with shared fn" (equal? "100" (channel-receive ch12)))
(thread-join h14)
(channel-destroy ch12)

;; ============================================================
(display "=== 13. Shared higher-order function ===") (newline)
;; ============================================================

(define (apply-twice f x) (f (f x)))
(share-function 'apply-twice)

(define ch13 (channel-create "result13"))
(define h15 (thread-create
  "(begin (define ch (channel-lookup \"result13\")) (channel-send ch (number->string (apply-twice add1 5))))"
  #f))
(check "shared higher-order fn" (equal? "7" (channel-receive ch13)))
(thread-join h15)
(channel-destroy ch13)

;; ============================================================
(display "=== 14. Shared function with conditional logic ===") (newline)
;; ============================================================

(define (classify n)
  (cond ((< n 0) "negative")
        ((= n 0) "zero")
        ((< n 10) "small")
        ((< n 100) "medium")
        (else "large")))
(share-function 'classify)

(define ch14a (channel-create "result14a"))
(define ch14b (channel-create "result14b"))
(define ch14c (channel-create "result14c"))
(define ch14d (channel-create "result14d"))
(define ch14e (channel-create "result14e"))

(define h16a (thread-create
  "(begin (define ch (channel-lookup \"result14a\")) (channel-send ch (classify -5)))" #f))
(define h16b (thread-create
  "(begin (define ch (channel-lookup \"result14b\")) (channel-send ch (classify 0)))" #f))
(define h16c (thread-create
  "(begin (define ch (channel-lookup \"result14c\")) (channel-send ch (classify 7)))" #f))
(define h16d (thread-create
  "(begin (define ch (channel-lookup \"result14d\")) (channel-send ch (classify 42)))" #f))
(define h16e (thread-create
  "(begin (define ch (channel-lookup \"result14e\")) (channel-send ch (classify 999)))" #f))

(check "classify negative" (equal? "negative" (channel-receive ch14a)))
(check "classify zero" (equal? "zero" (channel-receive ch14b)))
(check "classify small" (equal? "small" (channel-receive ch14c)))
(check "classify medium" (equal? "medium" (channel-receive ch14d)))
(check "classify large" (equal? "large" (channel-receive ch14e)))

(thread-join h16a) (thread-join h16b) (thread-join h16c)
(thread-join h16d) (thread-join h16e)
(channel-destroy ch14a) (channel-destroy ch14b) (channel-destroy ch14c)
(channel-destroy ch14d) (channel-destroy ch14e)

;; ============================================================
(display "=== 15. Shared function with let/let* bindings ===") (newline)
;; ============================================================

(define (hypotenuse a b)
  (let ((a2 (* a a))
        (b2 (* b b)))
    (sqrt (+ a2 b2))))
(share-function 'hypotenuse)

(define ch15 (channel-create "result15"))
(define h17 (thread-create
  "(begin (define ch (channel-lookup \"result15\")) (channel-send ch (number->string (hypotenuse 3.0 4.0))))"
  #f))
(check "shared fn with let bindings" (equal? "5" (channel-receive ch15)))
(thread-join h17)
(channel-destroy ch15)

;; ============================================================
(display "=== 16. Shared function with string operations ===") (newline)
;; ============================================================

(define (shout s)
  (string-append "*** " s " ***"))
(share-function 'shout)

(define ch16 (channel-create "result16"))
(define h18 (thread-create
  "(begin (define ch (channel-lookup \"result16\")) (channel-send ch (shout \"HELLO\")))"
  #f))
(check "shared fn with string ops" (equal? "*** HELLO ***" (channel-receive ch16)))
(thread-join h18)
(channel-destroy ch16)

;; ============================================================
(display "=== 17. Many shared functions ===") (newline)
;; ============================================================

;; Share a batch of simple functions
(define (sf-add a b) (+ a b))
(define (sf-sub a b) (- a b))
(define (sf-mul a b) (* a b))
(define (sf-neg n) (- 0 n))
(define (sf-abs n) (if (< n 0) (- 0 n) n))
(define (sf-max a b) (if (> a b) a b))
(define (sf-min a b) (if (< a b) a b))
(define (sf-square n) (* n n))
(define (sf-cube n) (* n n n))
(define (sf-zero? n) (= n 0))

(share-function 'sf-add)
(share-function 'sf-sub)
(share-function 'sf-mul)
(share-function 'sf-neg)
(share-function 'sf-abs)
(share-function 'sf-max)
(share-function 'sf-min)
(share-function 'sf-square)
(share-function 'sf-cube)
(share-function 'sf-zero?)

(define ch17 (channel-create "result17"))
(define h19 (thread-create
  "(begin
     (define ch (channel-lookup \"result17\"))
     (channel-send ch (number->string (sf-add 10 20)))
     (channel-send ch (number->string (sf-sub 50 8)))
     (channel-send ch (number->string (sf-mul 6 7)))
     (channel-send ch (number->string (sf-neg 42)))
     (channel-send ch (number->string (sf-abs -99)))
     (channel-send ch (number->string (sf-max 10 20)))
     (channel-send ch (number->string (sf-min 10 20)))
     (channel-send ch (number->string (sf-square 9)))
     (channel-send ch (number->string (sf-cube 3)))
     (channel-send ch (if (sf-zero? 0) \"yes\" \"no\")))"
  #f))

(check "many shared: add" (equal? "30" (channel-receive ch17)))
(check "many shared: sub" (equal? "42" (channel-receive ch17)))
(check "many shared: mul" (equal? "42" (channel-receive ch17)))
(check "many shared: neg" (equal? "-42" (channel-receive ch17)))
(check "many shared: abs" (equal? "99" (channel-receive ch17)))
(check "many shared: max" (equal? "20" (channel-receive ch17)))
(check "many shared: min" (equal? "10" (channel-receive ch17)))
(check "many shared: square" (equal? "81" (channel-receive ch17)))
(check "many shared: cube" (equal? "27" (channel-receive ch17)))
(check "many shared: zero?" (equal? "yes" (channel-receive ch17)))

(thread-join h19)
(channel-destroy ch17)

;; ============================================================
(display "=== 18. Stress: many threads sharing same code ===") (newline)
;; ============================================================

(define stress-ch (channel-create "stress"))
(define stress-handles '())

;; Spawn 10 threads that all use shared fact
(let loop ((i 1))
  (if (<= i 10)
    (begin
      (let ((h (thread-create
                 (string-append "(begin (define ch (channel-lookup \"stress\")) (channel-send ch (number->string (fact " (number->string i) "))))")
                 #f)))
        (set! stress-handles (cons h stress-handles)))
      (loop (+ i 1)))))

;; Collect results (order not guaranteed, so just count correct ones)
(define stress-expected '())
(let loop ((i 1))
  (if (<= i 10)
    (begin
      (set! stress-expected (cons (number->string (fact i)) stress-expected))
      (loop (+ i 1)))))

(define stress-results '())
(let loop ((i 0))
  (if (< i 10)
    (begin
      (set! stress-results (cons (channel-receive stress-ch) stress-results))
      (loop (+ i 1)))))

;; Check that we got all expected values (sort both and compare)
(define (sort-strings lst)
  (if (or (null? lst) (null? (cdr lst)))
    lst
    (let* ((pivot (car lst))
           (rest (cdr lst))
           (less '())
           (greater '()))
      (let loop ((r rest))
        (if (null? r) #f
          (begin
            (if (string<? (car r) pivot)
              (set! less (cons (car r) less))
              (set! greater (cons (car r) greater)))
            (loop (cdr r)))))
      (append (sort-strings less) (list pivot) (sort-strings greater)))))

(check "stress: all 10 results received" (= 10 (length stress-results)))
(check "stress: results match expected"
  (equal? (sort-strings stress-expected) (sort-strings stress-results)))

;; Join all threads
(let loop ((handles stress-handles))
  (if (not (null? handles))
    (begin (thread-join (car handles)) (loop (cdr handles)))))

(channel-destroy stress-ch)

;; ============================================================
(display "=== 19. Shared code still works after threads finish ===") (newline)
;; ============================================================

;; Main thread should still be able to use the shared functions
(check "add1 after threads" (= 101 (add1 100)))
(check "double after threads" (= 200 (double 100)))
(check "fact after threads" (= 120 (fact 5)))
(check "fib after threads" (= 55 (fib 10)))

;; New threads should still get shared code
(define ch19 (channel-create "result19"))
(define h20 (thread-create
  "(begin (define ch (channel-lookup \"result19\")) (channel-send ch (number->string (fib 15))))"
  #f))
(check "new thread still gets shared code" (equal? "610" (channel-receive ch19)))
(thread-join h20)
(channel-destroy ch19)

;; ============================================================
(display "=== 20. Shared function with do loop ===") (newline)
;; ============================================================

(define (sum-to n)
  (do ((i 0 (+ i 1))
       (s 0 (+ s i)))
      ((> i n) s)))
(share-function 'sum-to)

(define ch20 (channel-create "result20"))
(define h21 (thread-create
  "(begin (define ch (channel-lookup \"result20\")) (channel-send ch (number->string (sum-to 100))))"
  #f))
(check "shared fn with do loop" (equal? "5050" (channel-receive ch20)))
(thread-join h21)
(channel-destroy ch20)

;; ============================================================
(display "=== 21. Shared function with list processing ===") (newline)
;; ============================================================

(define (my-length lst)
  (if (null? lst) 0 (+ 1 (my-length (cdr lst)))))
(share-function 'my-length)

(define ch21 (channel-create "result21"))
(define h22 (thread-create
  "(begin (define ch (channel-lookup \"result21\")) (channel-send ch (number->string (my-length '(a b c d e)))))"
  #f))
(check "shared fn with list processing" (equal? "5" (channel-receive ch21)))
(thread-join h22)
(channel-destroy ch21)

;; ============================================================
(display "=== 22. Shared mutually-dependent functions ===") (newline)
;; ============================================================

;; Two functions that call each other (via shared bindings)
(define (my-even? n)
  (if (= n 0) #t (my-odd? (- n 1))))
(define (my-odd? n)
  (if (= n 0) #f (my-even? (- n 1))))
(share-function 'my-even?)
(share-function 'my-odd?)

(define ch22 (channel-create "result22"))
(define h23 (thread-create
  "(begin
     (define ch (channel-lookup \"result22\"))
     (channel-send ch (if (my-even? 10) \"yes\" \"no\"))
     (channel-send ch (if (my-odd? 10) \"yes\" \"no\"))
     (channel-send ch (if (my-even? 7) \"yes\" \"no\"))
     (channel-send ch (if (my-odd? 7) \"yes\" \"no\")))"
  #f))

(check "mutual recursion: even? 10" (equal? "yes" (channel-receive ch22)))
(check "mutual recursion: odd? 10" (equal? "no" (channel-receive ch22)))
(check "mutual recursion: even? 7" (equal? "no" (channel-receive ch22)))
(check "mutual recursion: odd? 7" (equal? "yes" (channel-receive ch22)))

(thread-join h23)
(channel-destroy ch22)

;; ============================================================
(display "=== 23. Share function that uses apply ===") (newline)
;; ============================================================

(define (sum-list lst)
  (apply + lst))
(share-function 'sum-list)

(define ch23 (channel-create "result23"))
(define h24 (thread-create
  "(begin (define ch (channel-lookup \"result23\")) (channel-send ch (number->string (sum-list '(1 2 3 4 5)))))"
  #f))
(check "shared fn using apply" (equal? "15" (channel-receive ch23)))
(thread-join h24)
(channel-destroy ch23)

;; ============================================================
(display "=== 24. Shared function with tail recursion ===") (newline)
;; ============================================================

(define (tail-sum n acc)
  (if (= n 0) acc (tail-sum (- n 1) (+ acc n))))
(share-function 'tail-sum)

(define ch24 (channel-create "result24"))
(define h25 (thread-create
  "(begin (define ch (channel-lookup \"result24\")) (channel-send ch (number->string (tail-sum 1000 0))))"
  #f))
(check "shared tail-recursive fn" (equal? "500500" (channel-receive ch24)))
(thread-join h25)
(channel-destroy ch24)

;; ============================================================
;; Summary
;; ============================================================
(newline)
(display "=== Results: ")
(display pass-count) (display " passed, ")
(display fail-count) (display " failed ===") (newline)

(exit)
