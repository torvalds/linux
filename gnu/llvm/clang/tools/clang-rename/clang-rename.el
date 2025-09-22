;;; clang-rename.el --- Renames every occurrence of a symbol found at <offset>.  -*- lexical-binding: t; -*-

;; Version: 0.1.0
;; Keywords: tools, c

;;; Commentary:

;; To install clang-rename.el make sure the directory of this file is in your
;; `load-path' and add
;;
;;   (require 'clang-rename)
;;
;; to your .emacs configuration.

;;; Code:

(defgroup clang-rename nil
  "Integration with clang-rename"
  :group 'c)

(defcustom clang-rename-binary "clang-rename"
  "Path to clang-rename executable."
  :type '(file :must-match t)
  :group 'clang-rename)

;;;###autoload
(defun clang-rename (new-name)
  "Rename all instances of the symbol at point to NEW-NAME using clang-rename."
  (interactive "sEnter a new name: ")
  (save-some-buffers :all)
  ;; clang-rename should not be combined with other operations when undoing.
  (undo-boundary)
  (let ((output-buffer (get-buffer-create "*clang-rename*")))
    (with-current-buffer output-buffer (erase-buffer))
    (let ((exit-code (call-process
                      clang-rename-binary nil output-buffer nil
                      (format "-offset=%d"
                              ;; clang-rename wants file (byte) offsets, not
                              ;; buffer (character) positions.
                              (clang-rename--bufferpos-to-filepos
                               ;; Emacs treats one character after a symbol as
                               ;; part of the symbol, but clang-rename doesn’t.
                               ;; Use the beginning of the current symbol, if
                               ;; available, to resolve the inconsistency.
                               (or (car (bounds-of-thing-at-point 'symbol))
                                   (point))
                               'exact))
                      (format "-new-name=%s" new-name)
                      "-i" (buffer-file-name))))
      (if (and (integerp exit-code) (zerop exit-code))
          ;; Success; revert current buffer so it gets the modifications.
          (progn
            (kill-buffer output-buffer)
            (revert-buffer :ignore-auto :noconfirm :preserve-modes))
        ;; Failure; append exit code to output buffer and display it.
        (let ((message (clang-rename--format-message
                        "clang-rename failed with %s %s"
                        (if (integerp exit-code) "exit status" "signal")
                        exit-code)))
          (with-current-buffer output-buffer
            (insert ?\n message ?\n))
          (message "%s" message)
          (display-buffer output-buffer))))))

(defalias 'clang-rename--bufferpos-to-filepos
  (if (fboundp 'bufferpos-to-filepos)
      'bufferpos-to-filepos
    ;; Emacs 24 doesn’t have ‘bufferpos-to-filepos’, simulate it using
    ;; ‘position-bytes’.
    (lambda (position &optional _quality _coding-system)
      (1- (position-bytes position)))))

;; ‘format-message’ is new in Emacs 25.1.  Provide a fallback for older
;; versions.
(defalias 'clang-rename--format-message
  (if (fboundp 'format-message) 'format-message 'format))

(provide 'clang-rename)

;;; clang-rename.el ends here
