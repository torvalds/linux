;;; This function switches C-mode so that it indents almost everything
;;; as specified in FreeBSD's style(9). Tested with emacs-19.34 and
;;; xemacs-20.4.
;;;
;;; Use "M-x bsd" in a C mode buffer to activate it.
;;;
;;; The only problem I found is top-level indenting:
;;;
;;; We want function definitions with the function name at the beginning
;;; of a second line after the return type specification in the first:
;;; > int
;;; > foo(int bla)
;;; But emacs c-mode can't treat this differently from other multiple-line
;;; toplevel constructs:
;;; > const char *const bar =
;;; > "sometext";
;;; which means the second line must be indented by hand.
;;;
;;; To make this the default, use a line like this, but you can't easily
;;; switch back to default GNU style, since the old state isn't saved.
;;; (add-hook 'c-mode-common-hook 'bsd)
;;; As long as you don't have this in the c-mode hook you can edit GNU
;;; and BSD style C sources within one emacs session with no problem.
;;;
;;; Please report problems and additions directly to cracauer@freebsd.org

(defun bsd () (interactive)
  (c-set-style "bsd")
  (setq indent-tabs-mode t)
  ;; Use C-c C-s at points of source code so see which
  ;; c-set-offset is in effect for this situation
  (c-set-offset 'defun-block-intro 8)
  (c-set-offset 'statement-block-intro 8)
  (c-set-offset 'statement-case-intro 8)
  (c-set-offset 'substatement-open 4)
  (c-set-offset 'substatement 8)
  (c-set-offset 'arglist-cont-nonempty 4)
  (c-set-offset 'inclass 8)
  (c-set-offset 'knr-argdecl-intro 8)
  )
