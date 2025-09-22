;;; clang-format.el --- Format code using clang-format  -*- lexical-binding: t; -*-

;; Version: 0.1.0
;; Keywords: tools, c
;; Package-Requires: ((cl-lib "0.3"))
;; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

;;; Commentary:

;; This package allows to filter code through clang-format to fix its formatting.
;; clang-format is a tool that formats C/C++/Obj-C code according to a set of
;; style options, see <http://clang.llvm.org/docs/ClangFormatStyleOptions.html>.
;; Note that clang-format 3.4 or newer is required.

;; clang-format.el is available via MELPA and can be installed via
;;
;;   M-x package-install clang-format
;;
;; when ("melpa" . "http://melpa.org/packages/") is included in
;; `package-archives'.  Alternatively, ensure the directory of this
;; file is in your `load-path' and add
;;
;;   (require 'clang-format)
;;
;; to your .emacs configuration.

;; You may also want to bind `clang-format-region' to a key:
;;
;;   (global-set-key [C-M-tab] 'clang-format-region)

;;; Code:

(require 'cl-lib)
(require 'xml)

(defgroup clang-format nil
  "Format code using clang-format."
  :group 'tools)

(defcustom clang-format-executable
  (or (executable-find "clang-format")
      "clang-format")
  "Location of the clang-format executable.

A string containing the name or the full path of the executable."
  :group 'clang-format
  :type '(file :must-match t)
  :risky t)

(defcustom clang-format-style nil
  "Style argument to pass to clang-format.

By default clang-format will load the style configuration from
a file named .clang-format located in one of the parent directories
of the buffer."
  :group 'clang-format
  :type '(choice (string) (const nil))
  :safe #'stringp)
(make-variable-buffer-local 'clang-format-style)

(defcustom clang-format-fallback-style "none"
  "Fallback style to pass to clang-format.

This style will be used if clang-format-style is set to \"file\"
and no .clang-format is found in the directory of the buffer or
one of parent directories. Set to \"none\" to disable formatting
in such buffers."
  :group 'clang-format
  :type 'string
  :safe #'stringp)
(make-variable-buffer-local 'clang-format-fallback-style)

(defun clang-format--extract (xml-node)
  "Extract replacements and cursor information from XML-NODE."
  (unless (and (listp xml-node) (eq (xml-node-name xml-node) 'replacements))
    (error "Expected <replacements> node"))
  (let ((nodes (xml-node-children xml-node))
        (incomplete-format (xml-get-attribute xml-node 'incomplete_format))
        replacements
        cursor)
    (dolist (node nodes)
      (when (listp node)
        (let* ((children (xml-node-children node))
               (text (car children)))
          (cl-case (xml-node-name node)
            (replacement
             (let* ((offset (xml-get-attribute-or-nil node 'offset))
                    (length (xml-get-attribute-or-nil node 'length)))
               (when (or (null offset) (null length))
                 (error "<replacement> node does not have offset and length attributes"))
               (when (cdr children)
                 (error "More than one child node in <replacement> node"))

               (setq offset (string-to-number offset))
               (setq length (string-to-number length))
               (push (list offset length text) replacements)))
            (cursor
             (setq cursor (string-to-number text)))))))

    ;; Sort by decreasing offset, length.
    (setq replacements (sort (delq nil replacements)
                             (lambda (a b)
                               (or (> (car a) (car b))
                                   (and (= (car a) (car b))
                                        (> (cadr a) (cadr b)))))))

    (list replacements cursor (string= incomplete-format "true"))))

(defun clang-format--replace (offset length &optional text)
  "Replace the region defined by OFFSET and LENGTH with TEXT.
OFFSET and LENGTH are measured in bytes, not characters.  OFFSET
is a zero-based file offset, assuming ‘utf-8-unix’ coding."
  (let ((start (clang-format--filepos-to-bufferpos offset 'exact 'utf-8-unix))
        (end (clang-format--filepos-to-bufferpos (+ offset length) 'exact
                                                 'utf-8-unix)))
    (goto-char start)
    (delete-region start end)
    (when text
      (insert text))))

;; ‘bufferpos-to-filepos’ and ‘filepos-to-bufferpos’ are new in Emacs 25.1.
;; Provide fallbacks for older versions.
(defalias 'clang-format--bufferpos-to-filepos
  (if (fboundp 'bufferpos-to-filepos)
      'bufferpos-to-filepos
    (lambda (position &optional _quality _coding-system)
      (1- (position-bytes position)))))

(defalias 'clang-format--filepos-to-bufferpos
  (if (fboundp 'filepos-to-bufferpos)
      'filepos-to-bufferpos
    (lambda (byte &optional _quality _coding-system)
      (byte-to-position (1+ byte)))))

;;;###autoload
(defun clang-format-region (start end &optional style assume-file-name)
  "Use clang-format to format the code between START and END according to STYLE.
If called interactively uses the region or the current statement if there is no
no active region. If no STYLE is given uses `clang-format-style'. Use
ASSUME-FILE-NAME to locate a style config file, if no ASSUME-FILE-NAME is given
uses the function `buffer-file-name'."
  (interactive
   (if (use-region-p)
       (list (region-beginning) (region-end))
     (list (point) (point))))

  (unless style
    (setq style clang-format-style))

  (unless assume-file-name
    (setq assume-file-name (buffer-file-name (buffer-base-buffer))))

  (let ((file-start (clang-format--bufferpos-to-filepos start 'approximate
                                                        'utf-8-unix))
        (file-end (clang-format--bufferpos-to-filepos end 'approximate
                                                      'utf-8-unix))
        (cursor (clang-format--bufferpos-to-filepos (point) 'exact 'utf-8-unix))
        (temp-buffer (generate-new-buffer " *clang-format-temp*"))
        (temp-file (make-temp-file "clang-format"))
        ;; Output is XML, which is always UTF-8.  Input encoding should match
        ;; the encoding used to convert between buffer and file positions,
        ;; otherwise the offsets calculated above are off.  For simplicity, we
        ;; always use ‘utf-8-unix’ and ignore the buffer coding system.
        (default-process-coding-system '(utf-8-unix . utf-8-unix)))
    (unwind-protect
        (let ((status (apply #'call-process-region
                             nil nil clang-format-executable
                             nil `(,temp-buffer ,temp-file) nil
                             `("-output-replacements-xml"
                               ;; Guard against a nil assume-file-name.
                               ;; If the clang-format option -assume-filename
                               ;; is given a blank string it will crash as per
                               ;; the following bug report
                               ;; https://bugs.llvm.org/show_bug.cgi?id=34667
                               ,@(and assume-file-name
                                      (list "-assume-filename" assume-file-name))
                               ,@(and style (list "-style" style))
                               "-fallback-style" ,clang-format-fallback-style
                               "-offset" ,(number-to-string file-start)
                               "-length" ,(number-to-string (- file-end file-start))
                               "-cursor" ,(number-to-string cursor))))
              (stderr (with-temp-buffer
                        (unless (zerop (cadr (insert-file-contents temp-file)))
                          (insert ": "))
                        (buffer-substring-no-properties
                         (point-min) (line-end-position)))))
          (cond
           ((stringp status)
            (error "(clang-format killed by signal %s%s)" status stderr))
           ((not (zerop status))
            (error "(clang-format failed with code %d%s)" status stderr)))

          (cl-destructuring-bind (replacements cursor incomplete-format)
              (with-current-buffer temp-buffer
                (clang-format--extract (car (xml-parse-region))))
            (save-excursion
              (dolist (rpl replacements)
                (apply #'clang-format--replace rpl)))
            (when cursor
              (goto-char (clang-format--filepos-to-bufferpos cursor 'exact
                                                             'utf-8-unix)))
            (if incomplete-format
                (message "(clang-format: incomplete (syntax errors)%s)" stderr)
              (message "(clang-format: success%s)" stderr))))
      (delete-file temp-file)
      (when (buffer-name temp-buffer) (kill-buffer temp-buffer)))))

;;;###autoload
(defun clang-format-buffer (&optional style assume-file-name)
  "Use clang-format to format the current buffer according to STYLE.
If no STYLE is given uses `clang-format-style'. Use ASSUME-FILE-NAME
to locate a style config file. If no ASSUME-FILE-NAME is given uses
the function `buffer-file-name'."
  (interactive)
  (clang-format-region (point-min) (point-max) style assume-file-name))

;;;###autoload
(defalias 'clang-format 'clang-format-region)

(provide 'clang-format)
;;; clang-format.el ends here
