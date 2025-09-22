;;; llvm-mode.el --- Major mode for the LLVM assembler language.

;; Maintainer:  The LLVM team, http://llvm.org/
;; Version: 1.0

;;; Commentary:

;; Major mode for editing LLVM IR files.

;;; Code:

(defvar llvm-mode-syntax-table
  (let ((table (make-syntax-table)))
    (modify-syntax-entry ?% "_" table)
    (modify-syntax-entry ?. "_" table)
    (modify-syntax-entry ?\; "< " table)
    (modify-syntax-entry ?\n "> " table)
    table)
  "Syntax table used while in LLVM mode.")

(defconst llvm-mode-primitive-type-regexp
  (concat
   "\\(i[0-9]+\\|"
   (regexp-opt
    '("void" "half" "bfloat" "float" "double" "fp128" "x86_fp80" "ppc_fp128"
      "x86_mmx" "x86_amx" "ptr" "type" "label" "opaque" "token") t)
   "\\)"))

(defvar llvm-font-lock-keywords
  (list
   ;; Attributes
   `(,(regexp-opt
       '("alwaysinline" "argmemonly" "allocsize" "builtin" "cold" "convergent" "dereferenceable" "dereferenceable_or_null" "hot" "immarg" "inaccessiblememonly"
         "inaccessiblemem_or_argmemonly" "inalloca" "inlinehint" "jumptable" "minsize" "mustprogress" "naked" "nobuiltin" "nonnull" "nocapture"
         "nocallback" "nocf_check" "noduplicate" "nofree" "noimplicitfloat" "noinline" "nomerge" "nonlazybind" "noprofile" "noredzone" "noreturn"
         "norecurse" "nosync" "noundef" "nounwind" "nosanitize_bounds" "nosanitize_coverage" "null_pointer_is_valid" "optdebug" "optforfuzzing" "optnone" "optsize" "preallocated" "readnone" "readonly" "returned" "returns_twice"
         "shadowcallstack" "signext" "speculatable" "speculative_load_hardening" "ssp" "sspreq" "sspstrong" "safestack" "sanitize_address" "sanitize_hwaddress" "sanitize_memtag"
         "sanitize_thread" "sanitize_memory" "strictfp" "swifterror" "uwtable" "vscale_range" "willreturn" "writeonly" "zeroext") 'symbols) . font-lock-constant-face)
   ;; Variables
   '("%[-a-zA-Z$._][-a-zA-Z$._0-9]*" . font-lock-variable-name-face)
   ;; Labels
   '("[-a-zA-Z$._0-9]+:" . font-lock-variable-name-face)
   ;; Unnamed variable slots
   '("%[-]?[0-9]+" . font-lock-variable-name-face)
   ;; Function names
   '("@[-a-zA-Z$._][-a-zA-Z$._0-9]*" . font-lock-function-name-face)
   ;; Fixed vector types
   `(,(concat "<[ \t]*\\([0-9]+\\)[ \t]*x[ \t]+"
	      llvm-mode-primitive-type-regexp
	      "[ \t]*>")
     (1 'font-lock-type-face)
     (2 'font-lock-type-face))
   ;; Scalable vector types
   `(,(concat "<[ \t]*\\(vscale[ \t]\\)+x[ \t]+\\([0-9]+\\)[ \t]*x[ \t]+"
	      llvm-mode-primitive-type-regexp
	      "[ \t]*>")
     (1 'font-lock-keyword-face)
     (2 'font-lock-type-face)
     (3 'font-lock-type-face))
   ;; Primitive types
   `(,(concat "\\<" llvm-mode-primitive-type-regexp "\\>") . font-lock-type-face)
   ;; Integer literals
   '("\\b[-]?[0-9]+\\b" . font-lock-preprocessor-face)
   ;; Values that can appear in a vec
   '("\\b\\(true\\|false\\|null\\|undef\\|poison\\|none\\)\\b" . font-lock-keyword-face)
   ;; Floating point constants
   '("\\b[-+]?[0-9]+.[0-9]*\\([eE][-+]?[0-9]+\\)?\\b" . font-lock-preprocessor-face)
   ;; Hex constants
   '("\\b[us]?0x[0-9A-Fa-f]+\\b" . font-lock-preprocessor-face)
   ;; Keywords
   `(,(regexp-opt
       '(;; Toplevel entities
         "declare" "define" "module" "target" "source_filename" "global" "constant" "const" "alias" "ifunc" "comdat"
         "attributes" "uselistorder" "uselistorder_bb"
         ;; Linkage types
         "private" "internal" "weak" "weak_odr" "linkonce" "linkonce_odr" "available_externally" "appending" "common" "extern_weak" "external"
         "uninitialized" "implementation" "..."
         ;; Values
         "zeroinitializer" "c" "asm" "blockaddress"

         ;; Calling conventions
         "ccc" "fastcc" "coldcc" "anyregcc" "preserve_mostcc" "preserve_allcc"
         "cxx_fast_tlscc" "swiftcc" "tailcc" "swifttailcc" "cfguard_checkcc"
         ;; Visibility styles
         "default" "hidden" "protected"
         ;; DLL storages
         "dllimport" "dllexport"
         ;; Thread local
         "thread_local" "localdynamic" "initialexec" "localexec"
         ;; Runtime preemption specifiers
         "dso_preemptable" "dso_local" "dso_local_equivalent"

         "gc" "atomic" "no_cfi" "volatile" "personality" "prologue" "section") 'symbols) . font-lock-keyword-face)
   ;; Arithmetic and Logical Operators
   `(,(regexp-opt '("add" "sub" "mul" "sdiv" "udiv" "urem" "srem" "and" "or" "xor"
                    "setne" "seteq" "setlt" "setgt" "setle" "setge") 'symbols) . font-lock-keyword-face)
   ;; Floating-point operators
   `(,(regexp-opt '("fadd" "fsub" "fneg" "fmul" "fdiv" "frem") 'symbols) . font-lock-keyword-face)
   ;; Special instructions
   `(,(regexp-opt '("phi" "tail" "call" "select" "to" "shl" "lshr" "ashr" "fcmp" "icmp" "va_arg" "landingpad" "freeze") 'symbols) . font-lock-keyword-face)
   ;; Control instructions
   `(,(regexp-opt '("ret" "br" "switch" "invoke" "resume" "unwind" "unreachable" "indirectbr" "callbr") 'symbols) . font-lock-keyword-face)
   ;; Memory operators
   `(,(regexp-opt '("malloc" "alloca" "free" "load" "store" "getelementptr" "fence" "cmpxchg" "atomicrmw") 'symbols) . font-lock-keyword-face)
   ;; Casts
   `(,(regexp-opt '("bitcast" "inttoptr" "ptrtoint" "trunc" "zext" "sext" "fptrunc" "fpext" "fptoui" "fptosi" "uitofp" "sitofp" "addrspacecast") 'symbols) . font-lock-keyword-face)
   ;; Vector ops
   `(,(regexp-opt '("extractelement" "insertelement" "shufflevector") 'symbols) . font-lock-keyword-face)
   ;; Aggregate ops
   `(,(regexp-opt '("extractvalue" "insertvalue") 'symbols) . font-lock-keyword-face)
   ;; Metadata types
   `(,(regexp-opt '("distinct") 'symbols) . font-lock-keyword-face)
   ;; Debug records
   `(,(concat "#" (regexp-opt '("dbg_assign" "dbg_declare" "dbg_label" "dbg_value") 'symbols)) . font-lock-keyword-face)
   ;; Atomic memory ordering constraints
   `(,(regexp-opt '("unordered" "monotonic" "acquire" "release" "acq_rel" "seq_cst") 'symbols) . font-lock-keyword-face)
   ;; Fast-math flags
   `(,(regexp-opt '("nnan" "ninf" "nsz" "arcp" "contract" "afn" "reassoc" "fast") 'symbols) . font-lock-keyword-face)
   ;; Use-list order directives
   `(,(regexp-opt '("uselistorder" "uselistorder_bb") 'symbols) . font-lock-keyword-face))
  "Syntax highlighting for LLVM.")

(defun llvm-current-defun-name ()
  "The `add-log-current-defun' function in LLVM mode."
  (save-excursion
    (end-of-line)
    (if (re-search-backward "^[ \t]*define[ \t]+.+[ \t]+@\\(.+\\)(.*)" nil t)
	(match-string-no-properties 1))))

;;;###autoload
(define-derived-mode llvm-mode prog-mode "LLVM"
  "Major mode for editing LLVM source files.
\\{llvm-mode-map}
  Runs `llvm-mode-hook' on startup."
  (setq font-lock-defaults `(llvm-font-lock-keywords))
  (setq-local defun-prompt-regexp "^[ \t]*define[ \t]+.+[ \t]+@.+(.*).+")
  (setq-local add-log-current-defun-function #'llvm-current-defun-name)
  (setq-local comment-start ";"))

;; Associate .ll files with llvm-mode
;;;###autoload
(add-to-list 'auto-mode-alist (cons "\\.ll\\'" 'llvm-mode))

(provide 'llvm-mode)

;;; llvm-mode.el ends here
