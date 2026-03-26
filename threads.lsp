;; threads.lsp -- High-level threading utilities for XLISP
;;
;; Requires: make THREADS=1
;; Load with: (load "threads.lsp")
;;
;; Provides:
;;   with-mutex    -- safe lock/unlock with cleanup
;;   future/await  -- spawn computation, retrieve result
;;   future?       -- type predicate
;;   pcall         -- parallel call, collect results in order
;;   pmap          -- parallel map over a list
;;   thread-pool-create   -- create a pool of worker threads
;;   thread-pool-submit   -- submit work to a pool (returns future)
;;   thread-pool-destroy  -- shut down a pool
;;   thread-pool?         -- type predicate

;; ============================================================
;; 1. with-mutex
;; ============================================================

(define-macro (with-mutex mtx &body body)
  (let ((result (gensym))
        (m (gensym)))
    `(let ((,m ,mtx))
       (mutex-lock ,m)
       (let ((,result (unwind-protect
                        (begin ,@body)
                        (mutex-unlock ,m))))
         ,result))))

;; ============================================================
;; 2. future / await / future?
;; ============================================================

;; Internal counter for unique channel names
(define %future-counter 0)

(define (%future-next-name)
  (set! %future-counter (+ %future-counter 1))
  (format #f "__future-~A" %future-counter))

(define (%make-future thread-handle channel name)
  (list '%future thread-handle channel name))

(define (future? obj)
  (and (pair? obj)
       (eq? (car obj) '%future)))

(define (%future-thread f) (list-ref f 1))
(define (%future-channel f) (list-ref f 2))
(define (%future-name f) (list-ref f 3))

(define (future expr-string . args)
  (let* ((init-file (if (pair? args) (car args) #f))
         (ch-name (%future-next-name))
         (ch (channel-create ch-name))
         ;; Build the child expression that evaluates expr-string,
         ;; converts the result to a string, and sends it on the channel.
         (wrapper (string-append
                    "(catch '%future-exit"
                    " (begin"
                    "  (define %ch (channel-lookup \"" ch-name "\"))"
                    "  (define %error-sent #f)"
                    "  (set! *error-handler*"
                    "    (lambda (fn env cont)"
                    "      (if (not %error-sent)"
                    "        (begin"
                    "          (set! %error-sent #t)"
                    "          (channel-send %ch \"__FUTURE-ERROR__\")"
                    "          (channel-close %ch)))"
                    "      (throw '%future-exit #f)))"
                    "  (define %result"
                    "    (let ((v " expr-string "))"
                    "      (if (string? v) v"
                    "        (format #f \"~A\" v))))"
                    "  (channel-send %ch %result)"
                    "  (channel-close %ch)))"))
         (h (thread-create wrapper init-file)))
    (%make-future h ch ch-name)))

(define (await f)
  (let* ((ch (%future-channel f))
         (h (%future-thread f))
         (msg (channel-receive ch)))
    (thread-join h)
    (channel-destroy ch)
    ;; Invalidate the future
    (set-car! f #f)
    (if (equal? msg "__FUTURE-ERROR__")
      (error "future terminated with an error")
      msg)))

;; ============================================================
;; 3. pcall
;; ============================================================

(define (pcall . expr-strings)
  (let* ((futures (map (lambda (e) (future e)) expr-strings))
         (results (map await futures)))
    results))

;; ============================================================
;; 4. thread-pool
;; ============================================================

(define %pool-counter 0)

(define (%pool-next-name)
  (set! %pool-counter (+ %pool-counter 1))
  (format #f "__pool-~A" %pool-counter))

(define (%make-pool task-channel handles name)
  (list '%thread-pool task-channel handles name))

(define (thread-pool? obj)
  (and (pair? obj)
       (eq? (car obj) '%thread-pool)))

(define (%pool-task-channel p) (list-ref p 1))
(define (%pool-handles p) (list-ref p 2))
(define (%pool-name p) (list-ref p 3))

(define (thread-pool-create n . args)
  (let* ((init-file (if (pair? args) (car args) #f))
         (pool-name (%pool-next-name))
         (task-ch-name (string-append pool-name "-tasks"))
         (task-ch (channel-create task-ch-name))
         ;; Worker loop: receive "result-ch-name\texpr", evaluate, send result
         (worker-expr (string-append
                        "(begin"
                        " (define %tch (channel-lookup \"" task-ch-name "\"))"
                        " (define %current-rch #f)"
                        " (set! *error-handler*"
                        "   (lambda (fn env cont)"
                        "     (if %current-rch"
                        "       (begin"
                        "         (channel-send %current-rch \"__FUTURE-ERROR__\")"
                        "         (channel-close %current-rch)"
                        "         (set! %current-rch #f)))"
                        "     (throw '%worker-error #f)))"
                        " (let %loop ()"
                        "   (let ((%msg (channel-receive %tch)))"
                        "     (if %msg"
                        "       (begin"
                        "         (catch '%worker-error"
                        "           (let* ((%tab (string-search \"\\t\" %msg))"
                        "                  (%rname (substring %msg 0 %tab))"
                        "                  (%expr (substring %msg (+ %tab 1) (string-length %msg)))"
                        "                  (%rch (channel-lookup %rname)))"
                        "             (set! %current-rch %rch)"
                        "             (let ((%v (eval (read (make-string-input-stream %expr)))))"
                        "               (if %rch"
                        "                 (begin"
                        "                   (channel-send %rch"
                        "                     (if (string? %v) %v (format #f \"~A\" %v)))"
                        "                   (channel-close %rch))))"
                        "             (set! %current-rch #f)))"
                        "         (%loop))))))"
                        ))
         (handles (let loop ((i 0) (acc '()))
                    (if (>= i n)
                      (reverse acc)
                      (loop (+ i 1)
                            (cons (thread-create worker-expr init-file) acc))))))
    (%make-pool task-ch handles pool-name)))

(define (thread-pool-submit pool expr-string)
  (let* ((ch-name (%future-next-name))
         (ch (channel-create ch-name))
         (task-ch (%pool-task-channel pool))
         (msg (string-append ch-name "\t" expr-string)))
    (channel-send task-ch msg)
    ;; Return a future-like object (no thread handle — pool owns the threads)
    (%make-future #f ch ch-name)))

(define (thread-pool-destroy pool)
  (let ((task-ch (%pool-task-channel pool))
        (handles (%pool-handles pool)))
    ;; Close the task channel — workers will exit their loops
    (channel-close task-ch)
    ;; Join all workers
    (for-each thread-join handles)
    ;; Cleanup
    (channel-destroy task-ch)
    ;; Invalidate
    (set-car! (cdr pool) #f)
    (set-car! (cddr pool) #f)
    #t))

;; Override await for pool futures (no thread handle to join)
(let ((%original-await await))
  (set! await
    (lambda (f)
      (let* ((ch (%future-channel f))
             (h (%future-thread f))
             (msg (channel-receive ch)))
        ;; Join the thread (catch errors from failed threads)
        (if h (catch 'error (thread-join h)))
        (channel-destroy ch)
        ;; Invalidate the future
        (set-car! f #f)
        (if (equal? msg "__FUTURE-ERROR__")
          (error "future terminated with an error")
          msg)))))

;; ============================================================
;; 5. pmap
;; ============================================================

(define (pmap template values . args)
  (let ((pool (if (pair? args) (car args) #f)))
    (if pool
      ;; Use thread pool
      (let* ((futures (map (lambda (v)
                             (thread-pool-submit pool
                               (string-append
                                 (let loop ((s template) (done ""))
                                   (let ((pos (string-search "~a" s)))
                                     (if pos
                                       (string-append done
                                         (substring s 0 pos)
                                         v
                                         (substring s (+ pos 2) (string-length s)))
                                       (string-append done s))))
                                 "")))
                           values))
             (results (map await futures)))
        results)
      ;; Use individual futures
      (let* ((futures (map (lambda (v)
                             (future
                               (let loop ((s template) (done ""))
                                 (let ((pos (string-search "~a" s)))
                                   (if pos
                                     (string-append done
                                       (substring s 0 pos)
                                       v
                                       (substring s (+ pos 2) (string-length s)))
                                     (string-append done s))))))
                           values))
             (results (map await futures)))
        results))))
