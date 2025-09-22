;; Machine Descriptions for R8C/M16C/M32C
;; Copyright (C) 2006
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

;; various block move instructions

;; R8C:
;;  SMOVB - while (r3--) { *a1-- = *r1ha0--; } - memcpy
;;  SMOVF - while (r3--) { *a1++ = *r1ha0++; } - memcpy
;;  SSTR  - while (r3--) { *a1++ = [r0l,r0]; } - memset

;; M32CM:
;;  SCMPU - while (*a0 && *a0 != *a1) { a0++; a1++; } - strcmp
;;  SIN   - while (r3--) { *a1++ = *a0; }
;;  SMOVB - while (r3--) { *a1-- = *a0--; } - memcpy
;;  SMOVF - while (r3--) { *a1++ = *a0++; } - memcpy
;;  SMOVU - while (*a1++ = *a0++) ; - strcpy
;;  SOUT  - while (r3--) { *a1 = *a0++; }
;;  SSTR  - while (r3--) { *a1++ = [r0l,r0]; } - memset



;; 0 = destination (mem:BLK ...)
;; 1 = source (mem:BLK ...)
;; 2 = count
;; 3 = alignment
(define_expand "movmemhi"
  [(match_operand 0 "ap_operand" "")
   (match_operand 1 "ap_operand" "")
   (match_operand 2 "m32c_r3_operand" "")
   (match_operand 3 "" "")
   ]
  ""
  "if (m32c_expand_movmemhi(operands)) DONE; FAIL;"
  )

;; We can't use mode macros for these because M16C uses r1h to extend
;; the source address, for copying data from ROM to RAM.  We don't yet
;; support that, but we need to zero our r1h, so the patterns differ.

;; 0 = dest (out)
;; 1 = src (out)
;; 2 = count (out)
;; 3 = dest (in)
;; 4 = src (in)
;; 5 = count (in)
(define_insn "movmemhi_bhi_op"
  [(set (mem:QI (match_operand:HI 3 "ap_operand" "0"))
	(mem:QI (match_operand:HI 4 "ap_operand" "1")))
   (set (match_operand:HI 2 "m32c_r3_operand" "=R3w")
	(const_int 0))
   (set (match_operand:HI 0 "ap_operand" "=Ra1")
	(plus:HI (match_dup 3)
		  (zero_extend:HI (match_operand:HI 5 "m32c_r3_operand" "2"))))
   (set (match_operand:HI 1 "ap_operand" "=Ra0")
	(plus:HI (match_dup 4)
		  (zero_extend:HI (match_dup 5))))
   (use (reg:HI R1_REGNO))]
  "TARGET_A16"
  "mov.b:q\t#0,r1h\n\tsmovf.b\t; %0[0..%2-1]=r1h%1[]"
  )
(define_insn "movmemhi_bpsi_op"
  [(set (mem:QI (match_operand:PSI 3 "ap_operand" "0"))
	(mem:QI (match_operand:PSI 4 "ap_operand" "1")))
   (set (match_operand:HI 2 "m32c_r3_operand" "=R3w")
	(const_int 0))
   (set (match_operand:PSI 0 "ap_operand" "=Ra1")
	(plus:PSI (match_dup 3)
		  (zero_extend:PSI (match_operand:HI 5 "m32c_r3_operand" "2"))))
   (set (match_operand:PSI 1 "ap_operand" "=Ra0")
	(plus:PSI (match_dup 4)
		  (zero_extend:PSI (match_dup 5))))]
  "TARGET_A24"
  "smovf.b\t; %0[0..%2-1]=%1[]"
  )
(define_insn "movmemhi_whi_op"
  [(set (mem:HI (match_operand:HI 3 "ap_operand" "0"))
	(mem:HI (match_operand:HI 4 "ap_operand" "1")))
   (set (match_operand:HI 2 "m32c_r3_operand" "=R3w")
	(const_int 0))
   (set (match_operand:HI 0 "ap_operand" "=Ra1")
	(plus:HI (match_dup 3)
		  (zero_extend:HI (match_operand:HI 5 "m32c_r3_operand" "2"))))
   (set (match_operand:HI 1 "ap_operand" "=Ra0")
	(plus:HI (match_dup 4)
		  (zero_extend:HI (match_dup 5))))
   (use (reg:HI R1_REGNO))]
  "TARGET_A16"
  "mov.b:q\t#0,r1h\n\tsmovf.w\t; %0[0..%2-1]=r1h%1[]"
  )
(define_insn "movmemhi_wpsi_op"
  [(set (mem:HI (match_operand:PSI 3 "ap_operand" "0"))
	(mem:HI (match_operand:PSI 4 "ap_operand" "1")))
   (set (match_operand:HI 2 "m32c_r3_operand" "=R3w")
	(const_int 0))
   (set (match_operand:PSI 0 "ap_operand" "=Ra1")
	(plus:PSI (match_dup 3)
		  (zero_extend:PSI (match_operand:HI 5 "m32c_r3_operand" "2"))))
   (set (match_operand:PSI 1 "ap_operand" "=Ra0")
	(plus:PSI (match_dup 4)
		  (zero_extend:PSI (match_dup 5))))]
  "TARGET_A24"
  "smovf.w\t; %0[0..%2-1]=%1[]"
  )



;; 0 = destination (mem:BLK ...)
;; 1 = number of bytes
;; 2 = value to store
;; 3 = alignment
(define_expand "setmemhi"
  [(match_operand 0 "ap_operand" "")
   (match_operand 1 "m32c_r3_operand" "")
   (match_operand 2 "m32c_r0_operand" "")
   (match_operand 3 "" "")
   ]
  "TARGET_A24"
  "if (m32c_expand_setmemhi(operands)) DONE; FAIL;"
  )

;; 0 = address (out)
;; 1 = count (out)
;; 2 = value (in)
;; 3 = address (in)
;; 4 = count (in)
(define_insn "setmemhi_b<mode>_op"
  [(set (mem:QI (match_operand:HPSI 3 "ap_operand" "0"))
	(match_operand:QI 2 "m32c_r0_operand" "R0w"))
   (set (match_operand:HI 1 "m32c_r3_operand" "=R3w")
	(const_int 0))
   (set (match_operand:HPSI 0 "ap_operand" "=Ra1")
	(plus:HPSI (match_dup 3)
		  (zero_extend:HPSI (match_operand:HI 4 "m32c_r3_operand" "1"))))]
  "TARGET_A24"
  "sstr.b\t; %0[0..%1-1]=%2"
  )

(define_insn "setmemhi_w<mode>_op"
  [(set (mem:HI (match_operand:HPSI 3 "ap_operand" "0"))
	(match_operand:HI 2 "m32c_r0_operand" "R0w"))
   (set (match_operand:HI 1 "m32c_r3_operand" "=R3w")
	(const_int 0))
   (set (match_operand:HPSI 0 "ap_operand" "=Ra1")
	(plus:HPSI (match_dup 3)
		  (zero_extend:HPSI (match_operand:HI 4 "m32c_r3_operand" "1"))))]
  "TARGET_A24"
  "sstr.w\t; %0[0..%1-1]=%2"
  )


;; SCMPU sets the flags according to the result of the string
;; comparison.  GCC wants the result to be a signed value reflecting
;; the result, which it then compares to zero.  Hopefully we can
;; optimize that later (see peephole in cond.md).  Meanwhile, the
;; strcmp builtin is expanded to a SCMPU followed by a flags-to-int
;; pattern in cond.md.

;; 0 = result:HI
;; 1 = destination (mem:BLK ...)
;; 2 = source (mem:BLK ...)
;; 3 = alignment

(define_expand "cmpstrsi"
  [(match_operand:HI 0 "" "")
   (match_operand 1 "ap_operand" "")
   (match_operand 2 "ap_operand" "")
   (match_operand 3 "" "")
   ]
  "TARGET_A24"
  "if (m32c_expand_cmpstr(operands)) DONE; FAIL;"
  )

;; 0 = string1
;; 1 = string2

(define_insn "cmpstrhi_op"
  [(set (reg:CC FLG_REGNO)
	(compare:CC (mem:BLK (match_operand:PSI 0 "ap_operand" "Ra0"))
		    (mem:BLK (match_operand:PSI 1 "ap_operand" "Ra1"))))
   (clobber (match_operand:PSI 2 "ap_operand" "=0"))
   (clobber (match_operand:PSI 3 "ap_operand" "=1"))]
  "TARGET_A24"
  "scmpu.b\t; flags := strcmp(*%0,*%1)"
  [(set_attr "flags" "oszc")]
  )



;; Note that SMOVU leaves the address registers pointing *after*
;; the NUL at the end of the string.  This is not what gcc expects; it
;; expects the address registers to point *at* the NUL.  The expander
;; must emit a suitable add insn.

;; 0 = target: set to &NUL in dest
;; 1 = destination (mem:BLK ...)
;; 2 = source (mem:BLK ...)

(define_expand "movstr"
  [(match_operand 0 "" "")
   (match_operand 1 "ap_operand" "")
   (match_operand 2 "ap_operand" "")
   ]
  "TARGET_A24"
  "if (m32c_expand_movstr(operands)) DONE; FAIL;"
  )

;; 0 = dest (out)
;; 1 = src (out) (clobbered)
;; 2 = dest (in)
;; 3 = src (in)
(define_insn "movstr_op"
  [(set (mem:BLK (match_operand:PSI 2 "ap_operand" "0"))
	(mem:BLK (match_operand:PSI 3 "ap_operand" "1")))
   (set (match_operand:PSI 0 "ap_operand" "=Ra1")
	(plus:PSI (match_dup 2)
		  (unspec:PSI [(const_int 0)] UNS_SMOVU)))
   (set (match_operand:PSI 1 "ap_operand" "=Ra0")
	(plus:PSI (match_dup 3)
		  (unspec:PSI [(const_int 0)] UNS_SMOVU)))]
  "TARGET_A24"
  "smovu.b\t; while (*%2++ := *%3++) != 0"
  [(set_attr "flags" "*")]
  )
  
