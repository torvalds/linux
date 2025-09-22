;; Machine Descriptions for R8C/M16C/M32C
;; Copyright (C) 2005
;; Free Software Foundation, Inc.
;; Contributed by Red Hat.
;;
;; This file is part of GCC.
;;
;; GCC is free software; you can redistribute it and/or modify it
;; under the terms of the GNU General Public License as published
;; by the Free Software Foundation; either version 2, or (at your
;; option) any later version.
;;
;; GCC is distributed in the hope that it will be useful, but WITHOUT
;; ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
;; or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
;; License for more details.
;;
;; You should have received a copy of the GNU General Public License
;; along with GCC; see the file COPYING.  If not, write to the Free
;; Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
;; 02110-1301, USA.

;; Prologue and epilogue patterns

(define_expand "prologue"
  [(const_int 1)]
  ""
  "m32c_emit_prologue(); DONE;"
  )

; For the next two, operands[0] is the amount of stack space we want
; to reserve.

; We assume dwarf2out will process each set in sequence.
(define_insn "prologue_enter_16"
  [(set (mem:HI (pre_dec:HI (reg:HI SP_REGNO)))
	(reg:HI FB_REGNO))
   (set (reg:HI FB_REGNO)
	(reg:HI SP_REGNO))
   (set (reg:HI SP_REGNO)
	(minus:HI (reg:HI SP_REGNO)
	           (match_operand 0 "const_int_operand" "i")))
   ]
  "TARGET_A16"
  "enter\t%0"
  [(set_attr "flags" "x")]
  )

(define_insn "prologue_enter_24"
  [(set (mem:SI (pre_dec:PSI (reg:PSI SP_REGNO)))
	(reg:SI FB_REGNO))
   (set (reg:PSI FB_REGNO)
	(reg:PSI SP_REGNO))
   (set (reg:PSI SP_REGNO)
	(minus:PSI (reg:PSI SP_REGNO)
	           (match_operand 0 "const_int_operand" "i")))
   ]
  "TARGET_A24"
  "enter\t%0"
  [(set_attr "flags" "x")]
  )

; Just a comment, for debugging the assembler output.
(define_insn "prologue_end"
  [(unspec_volatile [(const_int 0)] UNS_PROLOGUE_END)]
  ""
  "; end of prologue"
  [(set_attr "flags" "n")]
  )



(define_expand "epilogue"
  [(const_int 1)]
  ""
  "m32c_emit_epilogue(); DONE;"
  )

(define_expand "eh_return"
  [(match_operand:PSI 0 "" "")]
  ""
  "m32c_emit_eh_epilogue(operands[0]); DONE;"
  )

(define_insn "eh_epilogue"
  [(set (pc)
	(unspec_volatile [(match_operand 0 "m32c_r1_operand" "")
			  (match_operand 1 "m32c_r0_operand" "")
			  ] UNS_EH_EPILOGUE))]
  ""
  "jmp.a\t__m32c_eh_return"
  [(set_attr "flags" "x")]
  )

(define_insn "epilogue_exitd"
  [(set (reg:PSI SP_REGNO)
	(reg:PSI FB_REGNO))
   (set (reg:PSI FB_REGNO)
	(mem:PSI (reg:PSI SP_REGNO)))
   (set (reg:PSI SP_REGNO)
	(plus:PSI (reg:PSI SP_REGNO)
	      (match_operand 0 "const_int_operand" "i")))
   (return)
   ]
  ""
  "exitd"
  [(set_attr "flags" "x")]
  )

(define_insn "epilogue_reit"
  [(set (reg:PSI SP_REGNO)
	(plus:PSI (reg:PSI SP_REGNO)
	      (match_operand 0 "const_int_operand" "i")))
   (return)
   ]
  ""
  "reit"
  [(set_attr "flags" "x")]
  )

(define_insn "epilogue_rts"
  [(return)
   ]
  ""
  "rts"
  [(set_attr "flags" "x")]
  )

(define_insn "epilogue_start"
  [(unspec_volatile [(const_int 0)] UNS_EPILOGUE_START)]
  ""
  "; start of epilogue"
  [(set_attr "flags" "n")]
  )


; These are used by the prologue/epilogue code.

(define_insn "pushm"
  [(unspec [(match_operand 0 "const_int_operand" "i")] UNS_PUSHM)]
  ""
  "pushm\t%p0"
  [(set_attr "flags" "n")]
  )

(define_insn "popm"
  [(unspec [(match_operand 0 "const_int_operand" "i")] UNS_POPM)]
  ""
  "popm\t%p0"
  [(set_attr "flags" "n")]
  )
