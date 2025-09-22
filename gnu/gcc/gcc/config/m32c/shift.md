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

;; bit shifting

; Shifts are unusual for m32c.  We only support shifting in one
; "direction" but the shift count is signed.  Also, immediate shift
; counts have a limited range, and variable shift counts have to be in
; $r1h which GCC normally doesn't even know about.

; Other than compensating for the above, the patterns below are pretty
; straightforward.

(define_insn "ashlqi3_i"
  [(set (match_operand:QI 0 "mra_operand" "=RqiSd*Rmm,RqiSd*Rmm")
	(ashift:QI (match_operand:QI 1 "mra_operand" "0,0")
		   (match_operand:QI 2 "mrai_operand" "In4,RqiSd")))
   (clobber (match_scratch:HI 3 "=X,R1w"))]
  ""
  "@
   sha.b\t%2,%0
   mov.b\t%2,r1h\n\tsha.b\tr1h,%0"
  [(set_attr "flags" "oszc,oszc")]
  )

(define_insn "ashrqi3_i"
  [(set (match_operand:QI 0 "mra_operand" "=RqiSd*Rmm,RqiSd*Rmm")
	(ashiftrt:QI (match_operand:QI 1 "mra_operand" "0,0")
		     (neg:QI (match_operand:QI 2 "mrai_operand" "In4,RqiSd"))))
   (clobber (match_scratch:HI 3 "=X,R1w"))]
  ""
  "@
   sha.b\t%2,%0
   mov.b\t%2,r1h\n\tsha.b\tr1h,%0"
  [(set_attr "flags" "oszc,oszc")]
  )

(define_insn "lshrqi3_i"
  [(set (match_operand:QI 0 "mra_operand" "=RqiSd*Rmm,RqiSd*Rmm")
	(lshiftrt:QI (match_operand:QI 1 "mra_operand" "0,0")
		     (neg:QI (match_operand:QI 2 "mrai_operand" "In4,RqiSd"))))
   (clobber (match_scratch:HI 3 "=X,R1w"))]
  ""
  "@
   shl.b\t%2,%0
   mov.b\t%2,r1h\n\tshl.b\tr1h,%0"
  [(set_attr "flags" "szc,szc")]
  )


(define_expand "ashlqi3"
  [(parallel [(set (match_operand:QI 0 "mra_operand" "")
	(ashift:QI (match_operand:QI 1 "mra_operand" "")
		   (match_operand:QI 2 "general_operand" "")))
   (clobber (match_scratch:HI 3 ""))])]
  ""
  "if (m32c_prepare_shift (operands, 1, ASHIFT))
     DONE;"
  )

(define_expand "ashrqi3"
  [(parallel [(set (match_operand:QI 0 "mra_operand" "")
	(ashiftrt:QI (match_operand:QI 1 "mra_operand" "")
		     (neg:QI (match_operand:QI 2 "general_operand" ""))))
   (clobber (match_scratch:HI 3 ""))])]
  ""
  "if (m32c_prepare_shift (operands, -1, ASHIFTRT))
     DONE;"
  )

(define_expand "lshrqi3"
  [(parallel [(set (match_operand:QI 0 "mra_operand" "")
		   (lshiftrt:QI (match_operand:QI 1 "mra_operand" "")
				(neg:QI (match_operand:QI 2 "general_operand" ""))))
	      (clobber (match_scratch:HI 3 ""))])]
  ""
  "if (m32c_prepare_shift (operands, -1, LSHIFTRT))
     DONE;"
  )

; - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

(define_insn "ashlhi3_i"
  [(set (match_operand:HI 0 "mra_operand" "=SdRhi*Rmm,SdRhi*Rmm")
	(ashift:HI (match_operand:HI 1 "mra_operand" "0,0")
		   (match_operand:QI 2 "mrai_operand" "In4,RqiSd")))
   (clobber (match_scratch:HI 3 "=X,R1w"))]
  ""
  "@
   sha.w\t%2,%0
   mov.b\t%2,r1h\n\tsha.w\tr1h,%0"
  [(set_attr "flags" "oszc,oszc")]
  )

(define_insn "ashrhi3_i"
  [(set (match_operand:HI 0 "mra_operand" "=SdRhi*Rmm,SdRhi*Rmm")
	(ashiftrt:HI (match_operand:HI 1 "mra_operand" "0,0")
		     (neg:QI (match_operand:QI 2 "mrai_operand" "In4,RqiSd"))))
   (clobber (match_scratch:HI 3 "=X,R1w"))]
  ""
  "@
   sha.w\t%2,%0
   mov.b\t%2,r1h\n\tsha.w\tr1h,%0"
  [(set_attr "flags" "oszc,oszc")]
  )

(define_insn "lshrhi3_i"
  [(set (match_operand:HI 0 "mra_operand" "=RhiSd*Rmm,RhiSd*Rmm")
	(lshiftrt:HI (match_operand:HI 1 "mra_operand" "0,0")
		     (neg:QI (match_operand:QI 2 "mrai_operand" "In4,RqiSd"))))
   (clobber (match_scratch:HI 3 "=X,R1w"))]
  ""
  "@
   shl.w\t%2,%0
   mov.b\t%2,r1h\n\tshl.w\tr1h,%0"
  [(set_attr "flags" "szc,szc")]
  )


(define_expand "ashlhi3"
  [(parallel [(set (match_operand:HI 0 "mra_operand" "")
		   (ashift:HI (match_operand:HI 1 "mra_operand" "")
			      (match_operand:QI 2 "general_operand" "")))
	      (clobber (match_scratch:HI 3 ""))])]
  ""
  "if (m32c_prepare_shift (operands, 1, ASHIFT))
     DONE;"
  )

(define_expand "ashrhi3"
  [(parallel [(set (match_operand:HI 0 "mra_operand" "")
		   (ashiftrt:HI (match_operand:HI 1 "mra_operand" "")
				(neg:QI (match_operand:QI 2 "general_operand" ""))))
	      (clobber (match_scratch:HI 3 ""))])]
  ""
  "if (m32c_prepare_shift (operands, -1, ASHIFTRT))
     DONE;"
  )

(define_expand "lshrhi3"
  [(parallel [(set (match_operand:HI 0 "mra_operand" "")
		   (lshiftrt:HI (match_operand:HI 1 "mra_operand" "")
				(neg:QI (match_operand:QI 2 "general_operand" ""))))
	      (clobber (match_scratch:HI 3 ""))])]
  ""
  "if (m32c_prepare_shift (operands, -1, LSHIFTRT))
     DONE;"
  )




; - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -


(define_insn "ashlpsi3_i"
  [(set (match_operand:PSI 0 "mra_operand" "=R02RaaSd*Rmm,R02RaaSd*Rmm")
	(ashift:PSI (match_operand:PSI 1 "mra_operand" "0,0")
		    (match_operand:QI 2 "mrai_operand" "In4,RqiSd")))
   (clobber (match_scratch:HI 3 "=X,R1w"))]
  "TARGET_A24"
  "@
   sha.l\t%2,%0
   mov.b\t%2,r1h\n\tsha.l\tr1h,%0"
  [(set_attr "flags" "oszc,oszc")]
  )

(define_insn "ashrpsi3_i"
  [(set (match_operand:PSI 0 "mra_operand" "=R02RaaSd*Rmm,R02RaaSd*Rmm")
	(ashiftrt:PSI (match_operand:PSI 1 "mra_operand" "0,0")
		      (neg:QI (match_operand:QI 2 "mrai_operand" "In4,RqiSd"))))
   (clobber (match_scratch:HI 3 "=X,R1w"))]
  "TARGET_A24"
  "@
   sha.l\t%2,%0
   mov.b\t%2,r1h\n\tsha.l\tr1h,%0"
  [(set_attr "flags" "oszc,oszc")]
  )

(define_insn "lshrpsi3_i"
  [(set (match_operand:PSI 0 "mra_operand" "=R02RaaSd,??Rmm")
	(lshiftrt:PSI (match_operand:PSI 1 "mra_operand" "0,0")
		      (neg:QI (match_operand:QI 2 "shiftcount_operand" "In4,RqiSd"))))
   (clobber (match_scratch:HI 3 "=X,R1w"))]
  "TARGET_A24"
  "@
   shl.l\t%2,%0
   mov.b\t%2,r1h\n\tshl.l\tr1h,%0"
  [(set_attr "flags" "szc,szc")]
  )


(define_expand "ashlpsi3"
  [(parallel [(set (match_operand:PSI 0 "mra_operand" "")
		   (ashift:PSI (match_operand:PSI 1 "mra_operand" "")
			       (match_operand:QI 2 "mrai_operand" "")))
	      (clobber (match_scratch:HI 3 ""))])]
  "TARGET_A24"
  "if (m32c_prepare_shift (operands, 1, ASHIFT))
     DONE;"
  )

(define_expand "ashrpsi3"
  [(parallel [(set (match_operand:PSI 0 "mra_operand" "")
		   (ashiftrt:PSI (match_operand:PSI 1 "mra_operand" "")
				 (neg:QI (match_operand:QI 2 "mrai_operand" ""))))
	      (clobber (match_scratch:HI 3 ""))])]
  "TARGET_A24"
  "if (m32c_prepare_shift (operands, -1, ASHIFTRT))
     DONE;"
  )

(define_expand "lshrpsi3"
  [(parallel [(set (match_operand:PSI 0 "mra_operand" "")
		   (lshiftrt:PSI (match_operand:PSI 1 "mra_operand" "")
				 (neg:QI (match_operand:QI 2 "mrai_operand" ""))))
	      (clobber (match_scratch:HI 3 ""))])]
  "TARGET_A24"
  "if (m32c_prepare_shift (operands, -1, LSHIFTRT))
     DONE;"
  )

; - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

; The m16c has a maximum shift count of -16..16, even when in a
; register.  It's optimal to use multiple shifts of -8..8 rather than
; loading larger constants into R1H multiple time.  The m32c can shift
; -32..32 either via immediates or in registers.  Hence, separate
; patterns.


(define_insn "ashlsi3_16"
  [(set (match_operand:SI 0 "r0123_operand" "=R03,R03")
	(ashift:SI (match_operand:SI 1 "r0123_operand" "0,0")
		   (match_operand:QI 2 "shiftcount_operand" "In4,RqiSd")))
   (clobber (match_scratch:HI 3 "=X,R1w"))]
  "TARGET_A16"
  "@
   sha.l\t%2,%0
   mov.b\t%2,r1h\n\tsha.l\tr1h,%0"
  [(set_attr "flags" "oszc,oszc")]
  )

(define_insn "ashrsi3_16"
  [(set (match_operand:SI 0 "r0123_operand" "=R03,R03")
	(ashiftrt:SI (match_operand:SI 1 "r0123_operand" "0,0")
		     (neg:QI (match_operand:QI 2 "shiftcount_operand" "In4,RqiSd"))))
   (clobber (match_scratch:HI 3 "=X,R1w"))]
  "TARGET_A16"
  "@
   sha.l\t%2,%0
   mov.b\t%2,r1h\n\tsha.l\tr1h,%0"
  [(set_attr "flags" "oszc,oszc")]
  )

(define_insn "lshrsi3_16"
  [(set (match_operand:SI 0 "r0123_operand" "=R03,R03")
	(lshiftrt:SI (match_operand:SI 1 "r0123_operand" "0,0")
		     (neg:QI (match_operand:QI 2 "shiftcount_operand" "In4,RqiSd"))))
   (clobber (match_scratch:HI 3 "=X,R1w"))]
  "TARGET_A16"
  "@
   shl.l\t%2,%0
   mov.b\t%2,r1h\n\tshl.l\tr1h,%0"
  [(set_attr "flags" "szc,szc")]
  )



(define_insn "ashlsi3_24"
  [(set (match_operand:SI 0 "r0123_operand" "=R03,R03")
	(ashift:SI (match_operand:SI 1 "r0123_operand" "0,0")
		   (match_operand:QI 2 "longshiftcount_operand" "In6,RqiSd")))
   (clobber (match_scratch:HI 3 "=X,R1w"))]
  "TARGET_A24"
  "@
   sha.l\t%2,%0
   mov.b\t%2,r1h\n\tsha.l\tr1h,%0"
  )

(define_insn "ashrsi3_24"
  [(set (match_operand:SI 0 "r0123_operand" "=R03,R03")
	(ashiftrt:SI (match_operand:SI 1 "r0123_operand" "0,0")
		     (neg:QI (match_operand:QI 2 "longshiftcount_operand" "In6,RqiSd"))))
   (clobber (match_scratch:HI 3 "=X,R1w"))]
  "TARGET_A24"
  "@
   sha.l\t%2,%0
   mov.b\t%2,r1h\n\tsha.l\tr1h,%0"
  )

(define_insn "lshrsi3_24"
  [(set (match_operand:SI 0 "r0123_operand" "=R03,R03")
	(lshiftrt:SI (match_operand:SI 1 "r0123_operand" "0,0")
		     (neg:QI (match_operand:QI 2 "longshiftcount_operand" "In6,RqiSd"))))
   (clobber (match_scratch:HI 3 "=X,R1w"))]
  "TARGET_A24"
  "@
   shl.l\t%2,%0
   mov.b\t%2,r1h\n\tshl.l\tr1h,%0"
  )




(define_expand "ashlsi3"
  [(parallel [(set (match_operand:SI 0 "r0123_operand" "")
		   (ashift:SI (match_operand:SI 1 "r0123_operand" "")
			      (match_operand:QI 2 "mrai_operand" "")))
	      (clobber (match_scratch:HI 3 ""))])]
  ""
  "if (m32c_prepare_shift (operands, 1, ASHIFT))
     DONE;"
  )

(define_expand "ashrsi3"
  [(parallel [(set (match_operand:SI 0 "r0123_operand" "")
		   (ashiftrt:SI (match_operand:SI 1 "r0123_operand" "")
				(neg:QI (match_operand:QI 2 "mrai_operand" ""))))
	      (clobber (match_scratch:HI 3 ""))])]
  ""
  "if (m32c_prepare_shift (operands, -1, ASHIFTRT))
     DONE;"
  )

(define_expand "lshrsi3"
  [(parallel [(set (match_operand:SI 0 "r0123_operand" "")
		   (lshiftrt:SI (match_operand:SI 1 "r0123_operand" "")
				(neg:QI (match_operand:QI 2 "mrai_operand" ""))))
	      (clobber (match_scratch:HI 3 ""))])]
  ""
  "if (m32c_prepare_shift (operands, -1, LSHIFTRT))
     DONE;"
  )
