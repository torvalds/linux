;;; llvm-mir-mode.el --- Major mode for LLVM Machine IR

;; Maintainer:  The LLVM team, http://llvm.org/
;; Version: 1.0

;;; Commentary:

;; Major mode for editing LLVM MIR files.

;;; Code:

(require 'llvm-mode)

(defvar llvm-mir-mode-map
  (let ((map (make-sparse-keymap)))
    map)
  "Keymap for `llvm-mir-mode'.")

(defvar llvm-mir-mode-syntax-table
  (let ((st (make-syntax-table)))
    (modify-syntax-entry ?% "_" st)
    (modify-syntax-entry ?$ "_" st)
    (modify-syntax-entry ?. "_" st)
    (modify-syntax-entry ?# "< " st)
    (modify-syntax-entry ?\; "< " st)
    (modify-syntax-entry ?\n "> " st)
    st)
  "Syntax table for `llvm-mir-mode'.")

(defvar llvm-mir-font-lock-keywords
  (append
   (list
    ; YAML Attributes
    '("^name: +\\([a-zA-Z._][-a-zA-Z._0-9]*\\)"
      1 font-lock-function-name-face)
    '("^body: +|" . font-lock-keyword-face)
    '("^[a-zA-Z_.][-a-zA-Z._0-9]*:" . font-lock-keyword-face)
    `(,(regexp-opt '("true" "false")) . font-lock-constant-face)
    ; YAML separators
    '("^\\(---\\( |\\)?\\|\\.\\.\\.\\)$" . font-lock-comment-face)
    ; Registers
    '("%[a-zA-Z_.][-a-zA-Z._0-9]*" . font-lock-variable-name-face)
    '("%[0-9]+\\(\\.[a-zA-Z._0-9]+\\)?" . font-lock-variable-name-face)
    '("$[a-zA-Z_.][-a-zA-Z._0-9]*" . font-lock-constant-face)
    ; Register classes
    `(,(concat
        "%\\([a-zA-Z_.][-a-zA-Z._0-9]*\\|[0-9]+\\(\\.[a-zA-Z._0-9]+\\)?\\)"
        "\\(:[a-zA-Z_.][-a-zA-Z._0-9]*\\)")
      3 font-lock-type-face)
    '("class: \\([a-zA-Z_.][-a-zA-Z._0-9]*\\)" 1 font-lock-type-face)
    ; MO Register flags
    `(,(regexp-opt '("dead" "debug-use" "def" "early-clobber" "implicit"
                     "implicit-def" "internal" "killed" "renamable" "undef")
                   'symbols)
      . font-lock-keyword-face))
   llvm-font-lock-keywords)
  "Keyword highlighting specification for `llvm-mir-mode'.")

;;;###autoload
(define-derived-mode llvm-mir-mode prog-mode "LLVM MIR"
  "A major mode for editing LLVM MIR files."
  (setq-local comment-start "; ")
  (setq-local font-lock-defaults `(llvm-mir-font-lock-keywords)))

;;;###autoload
(add-to-list 'auto-mode-alist (cons "\\.mir\\'" 'llvm-mir-mode))

(provide 'llvm-mir-mode)

;;; llvm-mir-mode.el ends here
