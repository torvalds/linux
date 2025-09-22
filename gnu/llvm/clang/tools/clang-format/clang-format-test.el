;;; clang-format-test.el --- unit tests for clang-format.el  -*- lexical-binding: t; -*-

;; Copyright (C) 2017  Google Inc.

;; Author: Philipp Stephani <phst@google.com>

;; Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
;; See https://llvm.org/LICENSE.txt for license information.
;; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

;;; Commentary:

;; Unit tests for clang-format.el. Not run by lit, run as:
;; emacs -Q -batch -l clang/tools/clang-format/clang-format.el -l clang/tools/clang-format/clang-format-test.el -f ert-run-tests-batch-and-exit

;;; Code:

(require 'clang-format)

(require 'cl-lib)
(require 'ert)
(require 'pcase)

(ert-deftest clang-format-buffer--buffer-encoding ()
  "Tests that encoded text is handled properly."
  (cl-letf* ((call-process-args nil)
             ((symbol-function 'call-process-region)
              (lambda (&rest args)
                (push args call-process-args)
                (pcase-exhaustive args
                  (`(,_start ,_end ,_program ,_delete (,stdout ,_stderr)
                             ,_display . ,_args)
                   (with-current-buffer stdout
                     (insert "<?xml version='1.0'?>
<replacements xml:space='preserve' incomplete_format='false'>
<replacement offset='4' length='0'> </replacement>
<replacement offset='10' length='0'> </replacement>
</replacements>
"))
                   0)))))
    (with-temp-buffer
      (let ((buffer-file-name "foo.cpp")
            (buffer-file-coding-system 'utf-8-with-signature-dos)
            (default-process-coding-system 'latin-1-unix))
        (insert "ä =ö;\nü= ß;\n")
        (goto-char (point-min))
        (end-of-line)
        (clang-format-buffer))
      (should (equal (buffer-string) "ä = ö;\nü = ß;\n"))
      (should (eolp))
      (should (equal (buffer-substring (point) (point-max))
                     "\nü = ß;\n")))
    (should-not (cdr call-process-args))
    (pcase-exhaustive call-process-args
      (`((,start ,end ,_program ,delete (,_stdout ,_stderr) ,display . ,args))
       (should-not start)
       (should-not end)
       (should-not delete)
       (should-not display)
       (should (equal args
                      '("-output-replacements-xml" "-assume-filename" "foo.cpp"
                        "-fallback-style" "none"
                        ;; Beginning of buffer, no byte-order mark.
                        "-offset" "0"
                        ;; We have two lines with 2×2 bytes for the umlauts,
                        ;; 1 byte for the line ending, and 3 bytes for the
                        ;; other ASCII characters each.
                        "-length" "16"
                        ;; Length of a single line (without line ending).
                        "-cursor" "7")))))))

(ert-deftest clang-format-buffer--process-encoding ()
  "Tests that text is sent to the clang-format process in the
right encoding."
  (cl-letf* ((hexdump (executable-find "hexdump"))
             (original-call-process-region
              (symbol-function 'call-process-region))
             (call-process-inputs nil)
             ;; We redirect the input to hexdump so that we have guaranteed
             ;; ASCII output.
             ((symbol-function 'call-process-region)
              (lambda (&rest args)
                (pcase-exhaustive args
                  (`(,start ,end ,_program ,_delete (,stdout ,_stderr)
                            ,_display . ,_args)
                   (with-current-buffer stdout
                     (insert "<?xml version='1.0'?>
<replacements xml:space='preserve' incomplete_format='false'>
</replacements>
"))
                   (let ((stdin (current-buffer)))
                     (with-temp-buffer
                       (prog1
                           (let ((stdout (current-buffer)))
                             (with-current-buffer stdin
                               (funcall original-call-process-region
                                        start end hexdump nil stdout nil
                                        "-v" "-e" "/1 \"%02x \"")))
                         (push (buffer-string) call-process-inputs)))))))))
    (skip-unless hexdump)
    (with-temp-buffer
      (let ((buffer-file-name "foo.cpp")
            (buffer-file-coding-system 'utf-8-with-signature-dos)
            (default-process-coding-system 'latin-1-unix))
        (insert "ä\n")
        (clang-format-buffer))
      (should (equal (buffer-string) "ä\n"))
      (should (eobp)))
    (should (equal call-process-inputs '("c3 a4 0a ")))))

(ert-deftest clang-format-buffer--end-to-end ()
  "End-to-end test for ‘clang-format-buffer’.
Actually calls the clang-format binary."
  (skip-unless (file-executable-p clang-format-executable))
  (with-temp-buffer
    (let ((buffer-file-name "foo.cpp")
          (buffer-file-coding-system 'utf-8-with-signature-dos)
          (default-process-coding-system 'latin-1-unix))
      (insert "ä =ö;\nü= ß;\n")
      (goto-char (point-min))
      (end-of-line)
      (clang-format-buffer))
    (should (equal (buffer-string) "ä = ö;\nü = ß;\n"))
    (should (eolp))
    (should (equal (buffer-substring (point) (point-max))
                   "\nü = ß;\n"))))

;;; clang-format-test.el ends here
