;;  Machine description the Motorola MCore
;;  Copyright (C) 1993, 1999, 2000, 2004, 2005
;;  Free Software Foundation, Inc.
;;  Contributed by Motorola.

;; This file is part of GCC.

;; GCC is free software; you can redistribute it and/or modify
;; it under the terms of the GNU General Public License as published by
;; the Free Software Foundation; either version 2, or (at your option)
;; any later version.

;; GCC is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU General Public License for more details.

;; You should have received a copy of the GNU General Public License
;; along with GCC; see the file COPYING.  If not, write to
;; the Free Software Foundation, 51 Franklin Street, Fifth Floor,
;; Boston, MA 02110-1301, USA.

;;- See file "rtl.def" for documentation on define_insn, match_*, et. al.



;; -------------------------------------------------------------------------
;; Attributes
;; -------------------------------------------------------------------------

; Target CPU.

(define_attr "type" "brcond,branch,jmp,load,store,move,alu,shift"
  (const_string "alu"))

;; If a branch destination is within -2048..2047 bytes away from the
;; instruction it can be 2 bytes long.  All other conditional branches
;; are 10 bytes long, and all other unconditional branches are 8 bytes.
;;
;; the assembler handles the long-branch span case for us if we use
;; the "jb*" mnemonics for jumps/branches. This pushes the span
;; calculations and the literal table placement into the assembler,
;; where their interactions can be managed in a single place.

;; All MCORE instructions are two bytes long.

(define_attr "length" "" (const_int 2))

;; Scheduling.  We only model a simple load latency.
(define_insn_reservation "any_insn" 1
			 (eq_attr "type" "!load")
			 "nothing")
(define_insn_reservation "memory" 2
			 (eq_attr "type" "load")
			 "nothing")

(include "predicates.md")

;; -------------------------------------------------------------------------
;; Test and bit test
;; -------------------------------------------------------------------------

(define_insn ""
  [(set (reg:SI 17) 
        (sign_extract:SI (match_operand:SI 0 "mcore_arith_reg_operand" "r")
                         (const_int 1)
                         (match_operand:SI 1 "mcore_literal_K_operand" "K")))]
  ""
  "btsti	%0,%1"
  [(set_attr "type" "shift")])

(define_insn ""
  [(set (reg:SI 17) 
        (zero_extract:SI (match_operand:SI 0 "mcore_arith_reg_operand" "r")
                         (const_int 1)
                         (match_operand:SI 1 "mcore_literal_K_operand" "K")))]
  ""
  "btsti	%0,%1"
  [(set_attr "type" "shift")])

;;; This is created by combine.
(define_insn ""
  [(set (reg:CC 17)
	(ne:CC (zero_extract:SI (match_operand:SI 0 "mcore_arith_reg_operand" "r")
				(const_int 1)
				(match_operand:SI 1 "mcore_literal_K_operand" "K"))
	       (const_int 0)))]
  ""
  "btsti	%0,%1"
  [(set_attr "type" "shift")])


;; Created by combine from conditional patterns below (see sextb/btsti rx,31)

(define_insn ""
  [(set (reg:CC 17)
        (ne:CC (lshiftrt:SI (match_operand:SI 0 "mcore_arith_reg_operand" "r")
                            (const_int 7))
               (const_int 0)))]
  "GET_CODE(operands[0]) == SUBREG && 
      GET_MODE(SUBREG_REG(operands[0])) == QImode"
  "btsti	%0,7"
  [(set_attr "type" "shift")])

(define_insn ""
  [(set (reg:CC 17)
        (ne:CC (lshiftrt:SI (match_operand:SI 0 "mcore_arith_reg_operand" "r")
                            (const_int 15))
               (const_int 0)))]
  "GET_CODE(operands[0]) == SUBREG && 
      GET_MODE(SUBREG_REG(operands[0])) == HImode"
  "btsti	%0,15"
  [(set_attr "type" "shift")])

(define_split
  [(set (pc)
	(if_then_else (ne (eq:CC (zero_extract:SI
				  (match_operand:SI 0 "mcore_arith_reg_operand" "")
				  (const_int 1)
				  (match_operand:SI 1 "mcore_literal_K_operand" ""))
				 (const_int 0))
			  (const_int 0))
		      (label_ref (match_operand 2 "" ""))
		      (pc)))]
  ""
  [(set (reg:CC 17)
	(zero_extract:SI (match_dup 0) (const_int 1) (match_dup 1)))
   (set (pc) (if_then_else (eq (reg:CC 17) (const_int 0))
			   (label_ref (match_dup 2))
			   (pc)))]
  "")

(define_split
  [(set (pc)
	(if_then_else (eq (ne:CC (zero_extract:SI
				  (match_operand:SI 0 "mcore_arith_reg_operand" "")
				  (const_int 1)
				  (match_operand:SI 1 "mcore_literal_K_operand" ""))
				 (const_int 0))
			  (const_int 0))
		      (label_ref (match_operand 2 "" ""))
		      (pc)))]
  ""
  [(set (reg:CC 17)
	(zero_extract:SI (match_dup 0) (const_int 1) (match_dup 1)))
   (set (pc) (if_then_else (eq (reg:CC 17) (const_int 0))
			   (label_ref (match_dup 2))
			   (pc)))]
  "")

;; XXX - disabled by nickc because it fails on libiberty/fnmatch.c
;;
;; ; Experimental - relax immediates for and, andn, or, and tst to allow
;; ;    any immediate value (or an immediate at all -- or, andn, & tst).  
;; ;    This is done to allow bit field masks to fold together in combine.
;; ;    The reload phase will force the immediate into a register at the
;; ;    very end.  This helps in some cases, but hurts in others: we'd
;; ;    really like to cse these immediates.  However, there is a phase
;; ;    ordering problem here.  cse picks up individual masks and cse's
;; ;    those, but not folded masks (cse happens before combine).  It's
;; ;    not clear what the best solution is because we really want cse
;; ;    before combine (leaving the bit field masks alone).   To pick up
;; ;    relaxed immediates use -mrelax-immediates.  It might take some
;; ;    experimenting to see which does better (i.e. regular imms vs.
;; ;    arbitrary imms) for a particular code.   BRC
;; 
;; (define_insn ""
;;   [(set (reg:CC 17)
;; 	(ne:CC (and:SI (match_operand:SI 0 "mcore_arith_reg_operand" "r")
;; 		       (match_operand:SI 1 "mcore_arith_any_imm_operand" "rI"))
;; 	       (const_int 0)))]
;;   "TARGET_RELAX_IMM"
;;   "tst	%0,%1")
;; 
;; (define_insn ""
;;   [(set (reg:CC 17)
;; 	(ne:CC (and:SI (match_operand:SI 0 "mcore_arith_reg_operand" "r")
;; 		       (match_operand:SI 1 "mcore_arith_M_operand" "r"))
;; 	       (const_int 0)))]
;;   "!TARGET_RELAX_IMM"
;;   "tst	%0,%1")

(define_insn ""
  [(set (reg:CC 17)
	(ne:CC (and:SI (match_operand:SI 0 "mcore_arith_reg_operand" "r")
		       (match_operand:SI 1 "mcore_arith_M_operand" "r"))
	       (const_int 0)))]
  ""
  "tst	%0,%1")


(define_split 
  [(parallel[
      (set (reg:CC 17)
           (ne:CC (ne:SI (leu:CC (match_operand:SI 0 "mcore_arith_reg_operand" "r")
                                 (match_operand:SI 1 "mcore_arith_reg_operand" "r"))
                         (const_int 0))
                  (const_int 0)))
      (clobber (match_operand:CC 2 "mcore_arith_reg_operand" "=r"))])]
  ""
  [(set (reg:CC 17) (ne:SI (match_dup 0) (const_int 0)))
   (set (reg:CC 17) (leu:CC (match_dup 0) (match_dup 1)))])

;; -------------------------------------------------------------------------
;; SImode signed integer comparisons
;; -------------------------------------------------------------------------

(define_insn "decne_t"
  [(set (reg:CC 17) (ne:CC (plus:SI (match_operand:SI 0 "mcore_arith_reg_operand" "+r")
				    (const_int -1))		  
			   (const_int 0)))
   (set (match_dup 0)
	(plus:SI (match_dup 0)
		 (const_int -1)))]
  ""
  "decne	%0")

;; The combiner seems to prefer the following to the former.
;;
(define_insn ""
  [(set (reg:CC 17) (ne:CC (match_operand:SI 0 "mcore_arith_reg_operand" "+r")
			   (const_int 1)))
   (set (match_dup 0)
	(plus:SI (match_dup 0)
		 (const_int -1)))]
  ""
  "decne	%0")

(define_insn "cmpnesi_t"
  [(set (reg:CC 17) (ne:CC (match_operand:SI 0 "mcore_arith_reg_operand" "r")
			   (match_operand:SI 1 "mcore_arith_reg_operand" "r")))]
  ""
  "cmpne	%0,%1")

(define_insn "cmpneisi_t"
  [(set (reg:CC 17) (ne:CC (match_operand:SI 0 "mcore_arith_reg_operand" "r")
			   (match_operand:SI 1 "mcore_arith_K_operand" "K")))]
  ""
  "cmpnei	%0,%1")

(define_insn "cmpgtsi_t"
  [(set (reg:CC 17) (gt:CC (match_operand:SI 0 "mcore_arith_reg_operand" "r")
			   (match_operand:SI 1 "mcore_arith_reg_operand" "r")))]
  ""
  "cmplt	%1,%0")

(define_insn ""
  [(set (reg:CC 17) (gt:CC (plus:SI
			    (match_operand:SI 0 "mcore_arith_reg_operand" "+r")
			    (const_int -1))
			   (const_int 0)))
   (set (match_dup 0) (plus:SI (match_dup 0) (const_int -1)))]
  ""
  "decgt	%0")

(define_insn "cmpltsi_t"
  [(set (reg:CC 17) (lt:CC (match_operand:SI 0 "mcore_arith_reg_operand" "r")
			   (match_operand:SI 1 "mcore_arith_reg_operand" "r")))]
  ""
  "cmplt	%0,%1")

; cmplti is 1-32
(define_insn "cmpltisi_t"
  [(set (reg:CC 17) (lt:CC (match_operand:SI 0 "mcore_arith_reg_operand" "r")
			   (match_operand:SI 1 "mcore_arith_J_operand" "J")))]
  ""
  "cmplti	%0,%1")

; covers cmplti x,0
(define_insn ""
  [(set (reg:CC 17) (lt:CC (match_operand:SI 0 "mcore_arith_reg_operand" "r")
                         (const_int 0)))]
  ""
  "btsti	%0,31")

(define_insn ""
  [(set (reg:CC 17) (lt:CC (plus:SI
			    (match_operand:SI 0 "mcore_arith_reg_operand" "+r")
			    (const_int -1))
			   (const_int 0)))
   (set (match_dup 0) (plus:SI (match_dup 0) (const_int -1)))]
  ""
  "declt	%0")

;; -------------------------------------------------------------------------
;; SImode unsigned integer comparisons
;; -------------------------------------------------------------------------

(define_insn "cmpgeusi_t"
  [(set (reg:CC 17) (geu:CC (match_operand:SI 0 "mcore_arith_reg_operand" "r")
			    (match_operand:SI 1 "mcore_arith_reg_operand" "r")))]
  ""
  "cmphs	%0,%1")

(define_insn "cmpgeusi_0"
  [(set (reg:CC 17) (geu:CC (match_operand:SI 0 "mcore_arith_reg_operand" "r")
			    (const_int 0)))]
  ""
  "cmpnei	%0, 0")

(define_insn "cmpleusi_t"
  [(set (reg:CC 17) (leu:CC (match_operand:SI 0 "mcore_arith_reg_operand" "r")
			    (match_operand:SI 1 "mcore_arith_reg_operand" "r")))]
  ""
  "cmphs	%1,%0")

;; We save the compare operands in the cmpxx patterns and use them when
;; we generate the branch.

;; We accept constants here, in case we can modify them to ones which
;; are more efficient to load.  E.g. change 'x <= 62' to 'x < 63'.

(define_expand "cmpsi"
  [(set (reg:CC 17) (compare:CC (match_operand:SI 0 "mcore_compare_operand" "")
				(match_operand:SI 1 "nonmemory_operand" "")))]
  ""
  "
{ arch_compare_op0 = operands[0];
  arch_compare_op1 = operands[1];
  DONE;
}")

;; -------------------------------------------------------------------------
;; Logical operations
;; -------------------------------------------------------------------------

;; Logical AND clearing a single bit.  andsi3 knows that we have this
;; pattern and allows the constant literal pass through.
;;

;; RBE 2/97: don't need this pattern any longer...
;; RBE: I don't think we need both "S" and exact_log2() clauses.
;;(define_insn ""
;;  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
;;	(and:SI (match_operand:SI 1 "mcore_arith_reg_operand" "%0")
;;		(match_operand:SI 2 "const_int_operand" "S")))]
;;  "mcore_arith_S_operand (operands[2])"
;;  "bclri	%0,%Q2")
;;

(define_insn "andnsi3"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
	(and:SI (not:SI (match_operand:SI 1 "mcore_arith_reg_operand" "r"))
		(match_operand:SI 2 "mcore_arith_reg_operand" "0")))]
  ""
  "andn	%0,%1")

(define_expand "andsi3"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "")
	(and:SI (match_operand:SI 1 "mcore_arith_reg_operand" "")
		(match_operand:SI 2 "nonmemory_operand" "")))]
  ""
  "
{
  if (GET_CODE (operands[2]) == CONST_INT && INTVAL (operands[2]) < 0
      && ! mcore_arith_S_operand (operands[2]))
    {
      int not_value = ~ INTVAL (operands[2]);
      if (   CONST_OK_FOR_I (not_value)
          || CONST_OK_FOR_M (not_value)
	  || CONST_OK_FOR_N (not_value))
	{
	  operands[2] = copy_to_mode_reg (SImode, GEN_INT (not_value));
	  emit_insn (gen_andnsi3 (operands[0], operands[2], operands[1]));
	  DONE;
	}
    }

  if (! mcore_arith_K_S_operand (operands[2], SImode))
    operands[2] = copy_to_mode_reg (SImode, operands[2]);
}")

(define_insn ""
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r,r,r,r")
	(and:SI (match_operand:SI 1 "mcore_arith_reg_operand" "0,0,r,0")
		(match_operand:SI 2 "mcore_arith_any_imm_operand" "r,K,0,S")))]
  "TARGET_RELAX_IMM"
  "*
{
   switch (which_alternative)
     {
     case 0: return \"and	%0,%2\";
     case 1: return \"andi	%0,%2\";
     case 2: return \"and	%0,%1\";
     /* case -1: return \"bclri	%0,%Q2\";	 will not happen */
     case 3: return mcore_output_bclri (operands[0], INTVAL (operands[2]));
     default: gcc_unreachable ();
     }
}")

;; This was the old "S" which was "!(2^n)" */
;; case -1: return \"bclri	%0,%Q2\";	 will not happen */

(define_insn ""
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r,r,r,r")
	(and:SI (match_operand:SI 1 "mcore_arith_reg_operand" "0,0,r,0")
		(match_operand:SI 2 "mcore_arith_K_S_operand" "r,K,0,S")))]
  "!TARGET_RELAX_IMM"
  "*
{
   switch (which_alternative)
     {
     case 0: return \"and	%0,%2\";
     case 1: return \"andi	%0,%2\";
     case 2: return \"and	%0,%1\";
     case 3: return mcore_output_bclri (operands[0], INTVAL (operands[2]));
     default: gcc_unreachable ();
     }
}")

;(define_insn "iorsi3"
;  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
;	(ior:SI (match_operand:SI 1 "mcore_arith_reg_operand" "%0")
;		(match_operand:SI 2 "mcore_arith_reg_operand" "r")))]
;  ""
;  "or	%0,%2")

; need an expand to resolve ambiguity betw. the two iors below.
(define_expand "iorsi3"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "")
	(ior:SI (match_operand:SI 1 "mcore_arith_reg_operand" "")
		(match_operand:SI 2 "nonmemory_operand" "")))]
  ""
  "
{
   if (! mcore_arith_M_operand (operands[2], SImode))
      operands[2] = copy_to_mode_reg (SImode, operands[2]);
}")

(define_insn ""
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r,r,r")
	(ior:SI (match_operand:SI 1 "mcore_arith_reg_operand" "%0,0,0")
		(match_operand:SI 2 "mcore_arith_any_imm_operand" "r,M,T")))]
  "TARGET_RELAX_IMM"
  "*
{
   switch (which_alternative)
     {
     case 0: return \"or	%0,%2\";
     case 1: return \"bseti	%0,%P2\";
     case 2: return mcore_output_bseti (operands[0], INTVAL (operands[2]));
     default: gcc_unreachable ();
     }
}")

(define_insn ""
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r,r,r")
	(ior:SI (match_operand:SI 1 "mcore_arith_reg_operand" "%0,0,0")
		(match_operand:SI 2 "mcore_arith_M_operand" "r,M,T")))]
  "!TARGET_RELAX_IMM"
  "*
{
   switch (which_alternative)
     {
     case 0: return \"or	%0,%2\";
     case 1: return \"bseti	%0,%P2\";
     case 2: return mcore_output_bseti (operands[0], INTVAL (operands[2]));
     default: gcc_unreachable ();
     }
}")

;(define_insn ""
;  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
;	(ior:SI (match_operand:SI 1 "mcore_arith_reg_operand" "0")
;		(match_operand:SI 2 "const_int_operand" "M")))]
;  "exact_log2 (INTVAL (operands[2])) >= 0"
;  "bseti	%0,%P2")

;(define_insn ""
;  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
;	(ior:SI (match_operand:SI 1 "mcore_arith_reg_operand" "0")
;		(match_operand:SI 2 "const_int_operand" "i")))]
;  "mcore_num_ones (INTVAL (operands[2])) < 3"
;  "* return mcore_output_bseti (operands[0], INTVAL (operands[2]));")

(define_insn "xorsi3"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
	(xor:SI (match_operand:SI 1 "mcore_arith_reg_operand" "%0")
		(match_operand:SI 2 "mcore_arith_reg_operand" "r")))]
  ""
  "xor	%0,%2")

; these patterns give better code then gcc invents if
; left to its own devices

(define_insn "anddi3"
  [(set (match_operand:DI 0 "mcore_arith_reg_operand" "=r")
	(and:DI (match_operand:DI 1 "mcore_arith_reg_operand" "%0")
		(match_operand:DI 2 "mcore_arith_reg_operand" "r")))]
  ""
  "and	%0,%2\;and	%R0,%R2"
  [(set_attr "length" "4")])

(define_insn "iordi3"
  [(set (match_operand:DI 0 "mcore_arith_reg_operand" "=r")
	(ior:DI (match_operand:DI 1 "mcore_arith_reg_operand" "%0")
		(match_operand:DI 2 "mcore_arith_reg_operand" "r")))]
  ""
  "or	%0,%2\;or	%R0,%R2"
  [(set_attr "length" "4")])

(define_insn "xordi3"
  [(set (match_operand:DI 0 "mcore_arith_reg_operand" "=r")
	(xor:DI (match_operand:DI 1 "mcore_arith_reg_operand" "%0")
		(match_operand:DI 2 "mcore_arith_reg_operand" "r")))]
  ""
  "xor	%0,%2\;xor	%R0,%R2"
  [(set_attr "length" "4")])

;; -------------------------------------------------------------------------
;; Shifts and rotates
;; -------------------------------------------------------------------------

;; Only allow these if the shift count is a convenient constant.
(define_expand "rotlsi3"
  [(set (match_operand:SI            0 "mcore_arith_reg_operand" "")
	(rotate:SI (match_operand:SI 1 "mcore_arith_reg_operand" "")
		   (match_operand:SI 2 "nonmemory_operand" "")))]
  ""
  "if (! mcore_literal_K_operand (operands[2], SImode))
	 FAIL;
  ")

;; We can only do constant rotates, which is what this pattern provides.
;; The combiner will put it together for us when we do:
;;	(x << N) | (x >> (32 - N))
(define_insn ""
  [(set (match_operand:SI              0 "mcore_arith_reg_operand" "=r")
	(rotate:SI (match_operand:SI   1 "mcore_arith_reg_operand"  "0")
		     (match_operand:SI 2 "mcore_literal_K_operand"  "K")))]
  ""
  "rotli	%0,%2"
  [(set_attr "type" "shift")])

(define_insn "ashlsi3"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r,r")
	(ashift:SI (match_operand:SI 1 "mcore_arith_reg_operand" "0,0")
		   (match_operand:SI 2 "mcore_arith_K_operand_not_0" "r,K")))]
  ""
  "@
	lsl	%0,%2
	lsli	%0,%2"
  [(set_attr "type" "shift")])

(define_insn ""
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
	(ashift:SI (const_int 1)
		   (match_operand:SI 1 "mcore_arith_reg_operand" "r")))]
  ""
  "bgenr	%0,%1"
  [(set_attr "type" "shift")])

(define_insn "ashrsi3"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r,r")
	(ashiftrt:SI (match_operand:SI 1 "mcore_arith_reg_operand" "0,0")
		     (match_operand:SI 2 "mcore_arith_K_operand_not_0" "r,K")))]
  ""
  "@
	asr	%0,%2
	asri	%0,%2"
  [(set_attr "type" "shift")])

(define_insn "lshrsi3"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r,r")
	(lshiftrt:SI (match_operand:SI 1 "mcore_arith_reg_operand" "0,0")
		     (match_operand:SI 2 "mcore_arith_K_operand_not_0" "r,K")))]
  ""
  "@
	lsr	%0,%2
	lsri	%0,%2"
  [(set_attr "type" "shift")])

;(define_expand "ashldi3"
;  [(parallel[(set (match_operand:DI 0 "mcore_arith_reg_operand" "")
;		  (ashift:DI (match_operand:DI 1 "mcore_arith_reg_operand" "")
;			     (match_operand:DI 2 "immediate_operand" "")))
;
;	     (clobber (reg:CC 17))])]
;	    
;  ""
;  "
;{
;  if (GET_CODE (operands[2]) != CONST_INT
;      || INTVAL (operands[2]) != 1)
;    FAIL;
;}")
;
;(define_insn ""
;  [(set (match_operand:DI 0 "mcore_arith_reg_operand" "=r")
;	(ashift:DI (match_operand:DI 1 "mcore_arith_reg_operand" "0")
;		     (const_int 1)))
;   (clobber (reg:CC 17))]
;  ""
;  "lsli	%R0,0\;rotli	%0,0"
;  [(set_attr "length" "4") (set_attr "type" "shift")])

;; -------------------------------------------------------------------------
;; Index instructions
;; -------------------------------------------------------------------------
;; The second of each set of patterns is borrowed from the alpha.md file.
;; These variants of the above insns can occur if the second operand
;; is the frame pointer.  This is a kludge, but there doesn't
;; seem to be a way around it.  Only recognize them while reloading.

;; We must use reload_operand for some operands in case frame pointer
;; elimination put a MEM with invalid address there.  Otherwise,
;; the result of the substitution will not match this pattern, and reload
;; will not be able to correctly fix the result.

;; indexing longlongs or doubles (8 bytes)

(define_insn "indexdi_t"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
	(plus:SI (mult:SI (match_operand:SI 1 "mcore_arith_reg_operand" "r")
			  (const_int 8))
		 (match_operand:SI 2 "mcore_arith_reg_operand" "0")))]
  ""
  "*
    if (! mcore_is_same_reg (operands[1], operands[2]))
      {
        output_asm_insn (\"ixw\\t%0,%1\", operands);
        output_asm_insn (\"ixw\\t%0,%1\", operands);
      }
    else
      {
        output_asm_insn (\"ixh\\t%0,%1\", operands);
        output_asm_insn (\"ixh\\t%0,%1\", operands);
      }
    return \"\";
  "
;; if operands[1] == operands[2], the first option above is wrong! -- dac
;; was this... -- dac
;; ixw	%0,%1\;ixw	%0,%1"

  [(set_attr "length" "4")])

(define_insn ""
  [(set (match_operand:SI 0 "mcore_reload_operand" "=r,r,r")
	(plus:SI (plus:SI (mult:SI (match_operand:SI 1 "mcore_reload_operand" "r,r,r")
				   (const_int 8))
			  (match_operand:SI 2 "mcore_arith_reg_operand" "0,0,0"))
		 (match_operand:SI 3 "mcore_addsub_operand" "r,J,L")))]
  "reload_in_progress"
  "@
	ixw	%0,%1\;ixw	%0,%1\;addu	%0,%3
	ixw	%0,%1\;ixw	%0,%1\;addi	%0,%3
	ixw	%0,%1\;ixw	%0,%1\;subi	%0,%M3"
  [(set_attr "length" "6")])

;; indexing longs (4 bytes)

(define_insn "indexsi_t"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
	(plus:SI (mult:SI (match_operand:SI 1 "mcore_arith_reg_operand" "r")
			  (const_int 4))
		 (match_operand:SI 2 "mcore_arith_reg_operand" "0")))]
  ""
  "ixw	%0,%1")

(define_insn ""
  [(set (match_operand:SI 0 "mcore_reload_operand" "=r,r,r")
	(plus:SI (plus:SI (mult:SI (match_operand:SI 1 "mcore_reload_operand" "r,r,r")
				   (const_int 4))
			  (match_operand:SI 2 "mcore_arith_reg_operand" "0,0,0"))
		 (match_operand:SI 3 "mcore_addsub_operand" "r,J,L")))]
  "reload_in_progress"
  "@
	ixw	%0,%1\;addu	%0,%3
	ixw	%0,%1\;addi	%0,%3
	ixw	%0,%1\;subi	%0,%M3"
  [(set_attr "length" "4")])

;; indexing shorts (2 bytes)

(define_insn "indexhi_t"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
	(plus:SI (mult:SI (match_operand:SI 1 "mcore_arith_reg_operand" "r")
			  (const_int 2))
		 (match_operand:SI 2 "mcore_arith_reg_operand" "0")))]
  ""
  "ixh	%0,%1")

(define_insn ""
  [(set (match_operand:SI 0 "mcore_reload_operand" "=r,r,r")
	(plus:SI (plus:SI (mult:SI (match_operand:SI 1 "mcore_reload_operand" "r,r,r")
				   (const_int 2))
			  (match_operand:SI 2 "mcore_arith_reg_operand" "0,0,0"))
		 (match_operand:SI 3 "mcore_addsub_operand" "r,J,L")))]
  "reload_in_progress"
  "@
	ixh	%0,%1\;addu	%0,%3
	ixh	%0,%1\;addi	%0,%3
	ixh	%0,%1\;subi	%0,%M3"
  [(set_attr "length" "4")])

;;
;; Other sizes may be handy for indexing. 
;; the tradeoffs to consider when adding these are
;;	code size, execution time [vs. mul it is easy to win],
;;	and register pressure -- these patterns don't use an extra
;;	register to build the offset from the base
;;	and whether the compiler will not come up with some other idiom.
;;

;; -------------------------------------------------------------------------
;; Addition, Subtraction instructions
;; -------------------------------------------------------------------------

(define_expand "addsi3"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "")
	(plus:SI (match_operand:SI 1 "mcore_arith_reg_operand" "")
		 (match_operand:SI 2 "nonmemory_operand" "")))]
  ""
  "
{
  extern int flag_omit_frame_pointer;

  /* If this is an add to the frame pointer, then accept it as is so
     that we can later fold in the fp/sp offset from frame pointer
     elimination.  */
  if (flag_omit_frame_pointer
      && GET_CODE (operands[1]) == REG
      && (REGNO (operands[1]) == VIRTUAL_STACK_VARS_REGNUM
	  || REGNO (operands[1]) == FRAME_POINTER_REGNUM))
    {
      emit_insn (gen_addsi3_fp (operands[0], operands[1], operands[2]));
      DONE;
    }

  /* Convert adds to subtracts if this makes loading the constant cheaper.
     But only if we are allowed to generate new pseudos.  */
  if (! (reload_in_progress || reload_completed)
      && GET_CODE (operands[2]) == CONST_INT && INTVAL (operands[2]) < -32)
    {
      int neg_value = - INTVAL (operands[2]);
      if (   CONST_OK_FOR_I (neg_value)
	  || CONST_OK_FOR_M (neg_value)
	  || CONST_OK_FOR_N (neg_value))
	{
	  operands[2] = copy_to_mode_reg (SImode, GEN_INT (neg_value));
	  emit_insn (gen_subsi3 (operands[0], operands[1], operands[2]));
	  DONE;
	}
    } 

  if (! mcore_addsub_operand (operands[2], SImode))
    operands[2] = copy_to_mode_reg (SImode, operands[2]);
}")
 
;; RBE: for some constants which are not in the range which allows
;; us to do a single operation, we will try a paired addi/addi instead
;; of a movi/addi. This relieves some register pressure at the expense
;; of giving away some potential constant reuse.
;;
;; RBE 6/17/97: this didn't buy us anything, but I keep the pattern
;; for later reference
;; 
;; (define_insn "addsi3_i2"
;;   [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
;;      (plus:SI (match_operand:SI 1 "mcore_arith_reg_operand" "%0")
;;               (match_operand:SI 2 "const_int_operand" "g")))]
;;   "GET_CODE(operands[2]) == CONST_INT
;;    && ((INTVAL (operands[2]) > 32 && INTVAL(operands[2]) <= 64)
;;        || (INTVAL (operands[2]) < -32 && INTVAL(operands[2]) >= -64))"
;;   "*
;; {
;;    int n = INTVAL(operands[2]);
;;    if (n > 0)
;;      {
;;        operands[2] = GEN_INT(n - 32);
;;        return \"addi\\t%0,32\;addi\\t%0,%2\";
;;      }
;;    else
;;      {
;;        n = (-n);
;;        operands[2] = GEN_INT(n - 32);
;;        return \"subi\\t%0,32\;subi\\t%0,%2\";
;;      }
;; }"
;;  [(set_attr "length" "4")])

(define_insn "addsi3_i"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r,r,r")
	(plus:SI (match_operand:SI 1 "mcore_arith_reg_operand" "%0,0,0")
		 (match_operand:SI 2 "mcore_addsub_operand" "r,J,L")))]
  ""
  "@
	addu	%0,%2
	addi	%0,%2
	subi	%0,%M2")

;; This exists so that address computations based on the frame pointer
;; can be folded in when frame pointer elimination occurs.  Ordinarily
;; this would be bad because it allows insns which would require reloading,
;; but without it, we get multiple adds where one would do.

(define_insn "addsi3_fp"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r,r,r")
	(plus:SI (match_operand:SI 1 "mcore_arith_reg_operand" "%0,0,0")
		 (match_operand:SI 2 "immediate_operand" "r,J,L")))]
  "flag_omit_frame_pointer
   && (reload_in_progress || reload_completed || REGNO (operands[1]) == FRAME_POINTER_REGNUM)"
  "@
	addu	%0,%2
	addi	%0,%2
	subi	%0,%M2")

;; RBE: for some constants which are not in the range which allows
;; us to do a single operation, we will try a paired addi/addi instead
;; of a movi/addi. This relieves some register pressure at the expense
;; of giving away some potential constant reuse.
;;
;; RBE 6/17/97: this didn't buy us anything, but I keep the pattern
;; for later reference
;; 
;; (define_insn "subsi3_i2"
;;   [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
;;      (plus:SI (match_operand:SI 1 "mcore_arith_reg_operand" "%0")
;;               (match_operand:SI 2 "const_int_operand" "g")))]
;;   "TARGET_RBETEST && GET_CODE(operands[2]) == CONST_INT
;;    && ((INTVAL (operands[2]) > 32 && INTVAL(operands[2]) <= 64)
;;        || (INTVAL (operands[2]) < -32 && INTVAL(operands[2]) >= -64))"
;;   "*
;; {
;;    int n = INTVAL(operands[2]);
;;    if ( n > 0)
;;      {
;;        operands[2] = GEN_INT( n - 32);
;;        return \"subi\\t%0,32\;subi\\t%0,%2\";
;;      }
;;    else
;;      {
;;        n = (-n);
;;        operands[2] = GEN_INT(n - 32);
;;        return \"addi\\t%0,32\;addi\\t%0,%2\";
;;      }
;; }"
;;   [(set_attr "length" "4")])

;(define_insn "subsi3"
;  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r,r,r,r")
;	(minus:SI (match_operand:SI 1 "mcore_arith_K_operand" "0,0,r,K")
;		  (match_operand:SI 2 "mcore_arith_J_operand" "r,J,0,0")))]
;  ""
;  "@
;	sub	%0,%2
;	subi	%0,%2
;	rsub	%0,%1
;	rsubi	%0,%1")

(define_insn "subsi3"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r,r,r")
        (minus:SI (match_operand:SI 1 "mcore_arith_reg_operand" "0,0,r")
                  (match_operand:SI 2 "mcore_arith_J_operand" "r,J,0")))]
  ""
  "@
	subu	%0,%2
	subi	%0,%2
	rsub	%0,%1")

(define_insn ""
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
        (minus:SI (match_operand:SI 1 "mcore_literal_K_operand" "K")
                  (match_operand:SI 2 "mcore_arith_reg_operand" "0")))]
  ""
  "rsubi	%0,%1")

(define_insn "adddi3"
  [(set (match_operand:DI 0 "mcore_arith_reg_operand" "=&r")
	(plus:DI (match_operand:DI 1 "mcore_arith_reg_operand" "%0")
		 (match_operand:DI 2 "mcore_arith_reg_operand" "r")))
   (clobber (reg:CC 17))]
  ""
  "*
  {
    if (TARGET_LITTLE_END)
      return \"cmplt	%0,%0\;addc	%0,%2\;addc	%R0,%R2\";
    return \"cmplt	%R0,%R0\;addc	%R0,%R2\;addc	%0,%2\";
  }"
  [(set_attr "length" "6")])

;; special case for "longlong += 1"
(define_insn ""
  [(set (match_operand:DI 0 "mcore_arith_reg_operand" "=&r")
	(plus:DI (match_operand:DI 1 "mcore_arith_reg_operand" "0")
		 (const_int 1)))
   (clobber (reg:CC 17))]
  ""
  "*
  {
   if (TARGET_LITTLE_END)
      return \"addi	%0,1\;cmpnei %0,0\;incf	%R0\";
    return \"addi	%R0,1\;cmpnei %R0,0\;incf	%0\";
  }"
  [(set_attr "length" "6")])

;; special case for "longlong -= 1"
(define_insn ""
  [(set (match_operand:DI 0 "mcore_arith_reg_operand" "=&r")
	(plus:DI (match_operand:DI 1 "mcore_arith_reg_operand" "0")
		 (const_int -1)))
   (clobber (reg:CC 17))]
  ""
  "*
  {
    if (TARGET_LITTLE_END)
       return \"cmpnei %0,0\;decf	%R0\;subi	%0,1\";
    return \"cmpnei %R0,0\;decf	%0\;subi	%R0,1\";
  }"
  [(set_attr "length" "6")])

;; special case for "longlong += const_int"
;; we have to use a register for the const_int because we don't
;; have an unsigned compare immediate... only +/- 1 get to
;; play the no-extra register game because they compare with 0.
;; This winds up working out for any literal that is synthesized
;; with a single instruction. The more complicated ones look
;; like the get broken into subreg's to get initialized too soon
;; for us to catch here. -- RBE 4/25/96
;; only allow for-sure positive values.

(define_insn ""
  [(set (match_operand:DI 0 "mcore_arith_reg_operand" "=&r")
	(plus:DI (match_operand:DI 1 "mcore_arith_reg_operand" "0")
		 (match_operand:SI 2 "const_int_operand" "r")))
   (clobber (reg:CC 17))]
  "GET_CODE (operands[2]) == CONST_INT
   && INTVAL (operands[2]) > 0 && ! (INTVAL (operands[2]) & 0x80000000)"
  "*
{
  gcc_assert (GET_MODE (operands[2]) == SImode);
  if (TARGET_LITTLE_END)
    return \"addu	%0,%2\;cmphs	%0,%2\;incf	%R0\";
  return \"addu	%R0,%2\;cmphs	%R0,%2\;incf	%0\";
}"
  [(set_attr "length" "6")])

;; optimize "long long" + "unsigned long"
;; won't trigger because of how the extension is expanded upstream.
;; (define_insn ""
;;   [(set (match_operand:DI 0 "mcore_arith_reg_operand" "=&r")
;; 	(plus:DI (match_operand:DI 1 "mcore_arith_reg_operand" "%0")
;; 		 (zero_extend:DI (match_operand:SI 2 "mcore_arith_reg_operand" "r"))))
;;    (clobber (reg:CC 17))]
;;   "0"
;;   "cmplt	%R0,%R0\;addc	%R0,%2\;inct	%0"
;;   [(set_attr "length" "6")])

;; optimize "long long" + "signed long"
;; won't trigger because of how the extension is expanded upstream.
;; (define_insn ""
;;   [(set (match_operand:DI 0 "mcore_arith_reg_operand" "=&r")
;; 	(plus:DI (match_operand:DI 1 "mcore_arith_reg_operand" "%0")
;; 		 (sign_extend:DI (match_operand:SI 2 "mcore_arith_reg_operand" "r"))))
;;    (clobber (reg:CC 17))]
;;   "0"
;;   "cmplt	%R0,%R0\;addc	%R0,%2\;inct	%0\;btsti	%2,31\;dect	%0"
;;   [(set_attr "length" "6")])

(define_insn "subdi3"
  [(set (match_operand:DI 0 "mcore_arith_reg_operand" "=&r")
	(minus:DI (match_operand:DI 1 "mcore_arith_reg_operand" "0")
		  (match_operand:DI 2 "mcore_arith_reg_operand" "r")))
   (clobber (reg:CC 17))]
  ""
  "*
  {
    if (TARGET_LITTLE_END)
      return \"cmphs	%0,%0\;subc	%0,%2\;subc	%R0,%R2\";
    return \"cmphs	%R0,%R0\;subc	%R0,%R2\;subc	%0,%2\";
  }"
  [(set_attr "length" "6")])

;; -------------------------------------------------------------------------
;; Multiplication instructions
;; -------------------------------------------------------------------------

(define_insn "mulsi3"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
	(mult:SI (match_operand:SI 1 "mcore_arith_reg_operand" "%0")
		 (match_operand:SI 2 "mcore_arith_reg_operand" "r")))]
  ""
  "mult	%0,%2")

;;
;; 32/32 signed division -- added to the MCORE instruction set spring 1997
;;
;; Different constraints based on the architecture revision...
;;
(define_expand "divsi3"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "")
        (div:SI (match_operand:SI 1 "mcore_arith_reg_operand" "")
                (match_operand:SI 2 "mcore_arith_reg_operand" "")))]
  "TARGET_DIV"
  "")
 
;; MCORE Revision 1.50: restricts the divisor to be in r1. (6/97)
;;
(define_insn ""
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
        (div:SI (match_operand:SI 1 "mcore_arith_reg_operand" "0")
                (match_operand:SI 2 "mcore_arith_reg_operand" "b")))]
  "TARGET_DIV"
  "divs %0,%2")

;;
;; 32/32 signed division -- added to the MCORE instruction set spring 1997
;;
;; Different constraints based on the architecture revision...
;;
(define_expand "udivsi3"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "")
        (udiv:SI (match_operand:SI 1 "mcore_arith_reg_operand" "")
                 (match_operand:SI 2 "mcore_arith_reg_operand" "")))]
  "TARGET_DIV"
  "")
 
;; MCORE Revision 1.50: restricts the divisor to be in r1. (6/97)
(define_insn ""
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
        (udiv:SI (match_operand:SI 1 "mcore_arith_reg_operand" "0")
                 (match_operand:SI 2 "mcore_arith_reg_operand" "b")))]
  "TARGET_DIV"
  "divu %0,%2")
 
;; -------------------------------------------------------------------------
;; Unary arithmetic
;; -------------------------------------------------------------------------

(define_insn "negsi2"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
	(neg:SI (match_operand:SI 1 "mcore_arith_reg_operand" "0")))]
  ""
  "*
{
   return \"rsubi	%0,0\";
}")


(define_insn "abssi2"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
	(abs:SI (match_operand:SI 1 "mcore_arith_reg_operand" "0")))]
  ""
  "abs	%0")
	     
(define_insn "negdi2"
  [(set (match_operand:DI 0 "mcore_arith_reg_operand" "=&r")
	(neg:DI (match_operand:DI 1 "mcore_arith_reg_operand" "0")))
   (clobber (reg:CC 17))]
  ""
  "*
{
   if (TARGET_LITTLE_END)
     return \"cmpnei	%0,0\\n\\trsubi	%0,0\\n\\tnot	%R0\\n\\tincf	%R0\";
   return \"cmpnei	%R0,0\\n\\trsubi	%R0,0\\n\\tnot	%0\\n\\tincf	%0\";
}"
  [(set_attr "length" "8")])

(define_insn "one_cmplsi2"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
	(not:SI (match_operand:SI 1 "mcore_arith_reg_operand" "0")))]
  ""
  "not	%0")

;; -------------------------------------------------------------------------
;; Zero extension instructions
;; -------------------------------------------------------------------------

(define_expand "zero_extendhisi2"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "")
	(zero_extend:SI (match_operand:HI 1 "mcore_arith_reg_operand" "")))]
  ""
  "")

(define_insn ""
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r,r")
	(zero_extend:SI (match_operand:HI 1 "general_operand" "0,m")))]
  ""
  "@
	zexth	%0
	ld.h	%0,%1"
  [(set_attr "type" "shift,load")])

;; ldh gives us a free zero-extension. The combiner picks up on this.
(define_insn ""
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
	(zero_extend:SI (mem:HI (match_operand:SI 1 "mcore_arith_reg_operand" "r"))))]
  ""
  "ld.h	%0,(%1)"
  [(set_attr "type" "load")])

(define_insn ""
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
	(zero_extend:SI (mem:HI (plus:SI (match_operand:SI 1 "mcore_arith_reg_operand" "r")
				         (match_operand:SI 2 "const_int_operand" "")))))]
  "(INTVAL (operands[2]) >= 0) &&
   (INTVAL (operands[2]) < 32) &&
   ((INTVAL (operands[2])&1) == 0)"
  "ld.h	%0,(%1,%2)"
  [(set_attr "type" "load")])

(define_expand "zero_extendqisi2"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "")
	(zero_extend:SI (match_operand:QI 1 "general_operand" "")))]
  ""
  "") 

;; RBE: XXX: we don't recognize that the xtrb3 kills the CC register.
(define_insn ""
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r,b,r")
	(zero_extend:SI (match_operand:QI 1 "general_operand" "0,r,m")))]
  ""
  "@
	zextb	%0
	xtrb3	%0,%1
	ld.b	%0,%1"
  [(set_attr "type" "shift,shift,load")])

;; ldb gives us a free zero-extension. The combiner picks up on this.
(define_insn ""
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
	(zero_extend:SI (mem:QI (match_operand:SI 1 "mcore_arith_reg_operand" "r"))))]
  ""
  "ld.b	%0,(%1)"
  [(set_attr "type" "load")])

(define_insn ""
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
	(zero_extend:SI (mem:QI (plus:SI (match_operand:SI 1 "mcore_arith_reg_operand" "r")
				         (match_operand:SI 2 "const_int_operand" "")))))]
  "(INTVAL (operands[2]) >= 0) &&
   (INTVAL (operands[2]) < 16)"
  "ld.b	%0,(%1,%2)"
  [(set_attr "type" "load")])

(define_expand "zero_extendqihi2"
  [(set (match_operand:HI 0 "mcore_arith_reg_operand" "")
	(zero_extend:HI (match_operand:QI 1 "general_operand" "")))]
  ""
  "") 

;; RBE: XXX: we don't recognize that the xtrb3 kills the CC register.
(define_insn ""
  [(set (match_operand:HI 0 "mcore_arith_reg_operand" "=r,b,r")
	(zero_extend:HI (match_operand:QI 1 "general_operand" "0,r,m")))]
  ""
  "@
	zextb	%0
	xtrb3	%0,%1
	ld.b	%0,%1"
  [(set_attr "type" "shift,shift,load")])

;; ldb gives us a free zero-extension. The combiner picks up on this.
;; this doesn't catch references that are into a structure.
;; note that normally the compiler uses the above insn, unless it turns
;; out that we're dealing with a volatile...
(define_insn ""
  [(set (match_operand:HI 0 "mcore_arith_reg_operand" "=r")
	(zero_extend:HI (mem:QI (match_operand:SI 1 "mcore_arith_reg_operand" "r"))))]
  ""
  "ld.b	%0,(%1)"
  [(set_attr "type" "load")])

(define_insn ""
  [(set (match_operand:HI 0 "mcore_arith_reg_operand" "=r")
	(zero_extend:HI (mem:QI (plus:SI (match_operand:SI 1 "mcore_arith_reg_operand" "r")
				         (match_operand:SI 2 "const_int_operand" "")))))]
  "(INTVAL (operands[2]) >= 0) &&
   (INTVAL (operands[2]) < 16)"
  "ld.b	%0,(%1,%2)"
  [(set_attr "type" "load")])


;; -------------------------------------------------------------------------
;; Sign extension instructions
;; -------------------------------------------------------------------------

(define_expand "extendsidi2"
  [(set (match_operand:DI 0 "mcore_arith_reg_operand" "=r") 
	(match_operand:SI 1 "mcore_arith_reg_operand" "r"))]
  ""
  "
  {
    int low, high;

    if (TARGET_LITTLE_END)
      low = 0, high = 4;
    else
      low = 4, high = 0;
    
    emit_insn (gen_rtx_SET (VOIDmode, gen_rtx_SUBREG (SImode, operands[0], low),
	      operands[1]));
    emit_insn (gen_rtx_SET (VOIDmode, gen_rtx_SUBREG (SImode, operands[0], high),
	      gen_rtx_ASHIFTRT (SImode,
			       gen_rtx_SUBREG (SImode, operands[0], low),
			       GEN_INT (31))));
    DONE;
  }"
)

(define_insn "extendhisi2"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
	(sign_extend:SI (match_operand:HI 1 "mcore_arith_reg_operand" "0")))]
  ""
  "sexth	%0")

(define_insn "extendqisi2"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
	(sign_extend:SI (match_operand:QI 1 "mcore_arith_reg_operand" "0")))]
  ""
  "sextb	%0")

(define_insn "extendqihi2"
  [(set (match_operand:HI 0 "mcore_arith_reg_operand" "=r")
	(sign_extend:HI (match_operand:QI 1 "mcore_arith_reg_operand" "0")))]
  ""
  "sextb	%0")

;; -------------------------------------------------------------------------
;; Move instructions
;; -------------------------------------------------------------------------

;; SImode

(define_expand "movsi"
  [(set (match_operand:SI 0 "general_operand" "")
	(match_operand:SI 1 "general_operand" ""))]
  ""
  "
{
  if (GET_CODE (operands[0]) == MEM)
    operands[1] = force_reg (SImode, operands[1]);
}")

(define_insn ""
  [(set (match_operand:SI 0 "mcore_general_movdst_operand" "=r,r,a,r,a,r,m")
	(match_operand:SI 1 "mcore_general_movsrc_operand"  "r,P,i,c,R,m,r"))]
  "(register_operand (operands[0], SImode)
    || register_operand (operands[1], SImode))"
  "* return mcore_output_move (insn, operands, SImode);"
  [(set_attr "type" "move,move,move,move,load,load,store")])

;;
;; HImode
;;

(define_expand "movhi"
  [(set (match_operand:HI 0 "general_operand" "")
	(match_operand:HI 1 "general_operand"  ""))]
  ""
  "
{
  if (GET_CODE (operands[0]) == MEM)
    operands[1] = force_reg (HImode, operands[1]);
  else if (CONSTANT_P (operands[1])
	   && (GET_CODE (operands[1]) != CONST_INT
	       || (! CONST_OK_FOR_I (INTVAL (operands[1]))
		   && ! CONST_OK_FOR_M (INTVAL (operands[1]))
		   && ! CONST_OK_FOR_N (INTVAL (operands[1]))))
	   && ! reload_completed && ! reload_in_progress)
    {
      rtx reg = gen_reg_rtx (SImode);
      emit_insn (gen_movsi (reg, operands[1]));
      operands[1] = gen_lowpart (HImode, reg);
    }
}")
  
(define_insn ""
  [(set (match_operand:HI 0 "mcore_general_movdst_operand" "=r,r,a,r,r,m")
	(match_operand:HI 1 "mcore_general_movsrc_operand"  "r,P,i,c,m,r"))]
  "(register_operand (operands[0], HImode)
    || register_operand (operands[1], HImode))"
  "* return mcore_output_move (insn, operands, HImode);"
  [(set_attr "type" "move,move,move,move,load,store")])

;;
;; QImode
;;

(define_expand "movqi"
  [(set (match_operand:QI 0 "general_operand" "")
	(match_operand:QI 1 "general_operand"  ""))]
  ""
  "
{
  if (GET_CODE (operands[0]) == MEM)
    operands[1] = force_reg (QImode, operands[1]);
  else if (CONSTANT_P (operands[1])
	   && (GET_CODE (operands[1]) != CONST_INT
	       || (! CONST_OK_FOR_I (INTVAL (operands[1]))
		   && ! CONST_OK_FOR_M (INTVAL (operands[1]))
		   && ! CONST_OK_FOR_N (INTVAL (operands[1]))))
	   && ! reload_completed && ! reload_in_progress)
    {
      rtx reg = gen_reg_rtx (SImode);
      emit_insn (gen_movsi (reg, operands[1]));
      operands[1] = gen_lowpart (QImode, reg);
    }
}")
  
(define_insn ""
  [(set (match_operand:QI 0 "mcore_general_movdst_operand" "=r,r,a,r,r,m")
	(match_operand:QI 1 "mcore_general_movsrc_operand"  "r,P,i,c,m,r"))]
  "(register_operand (operands[0], QImode)
    || register_operand (operands[1], QImode))"
  "* return mcore_output_move (insn, operands, QImode);"
   [(set_attr "type" "move,move,move,move,load,store")])


;; DImode

(define_expand "movdi"
  [(set (match_operand:DI 0 "general_operand" "")
	(match_operand:DI 1 "general_operand" ""))]
  ""
  "
{
  if (GET_CODE (operands[0]) == MEM)
    operands[1] = force_reg (DImode, operands[1]);
  else if (GET_CODE (operands[1]) == CONST_INT
           && ! CONST_OK_FOR_I (INTVAL (operands[1]))
	   && ! CONST_OK_FOR_M (INTVAL (operands[1]))
	   && ! CONST_OK_FOR_N (INTVAL (operands[1])))
    {
      int i;
      for (i = 0; i < UNITS_PER_WORD * 2; i += UNITS_PER_WORD)
        emit_move_insn (simplify_gen_subreg (SImode, operands[0], DImode, i),
		        simplify_gen_subreg (SImode, operands[1], DImode, i));
      DONE;
    }
}")

(define_insn "movdi_i"
  [(set (match_operand:DI 0 "general_operand" "=r,r,r,r,a,r,m")
	(match_operand:DI 1 "mcore_general_movsrc_operand" "I,M,N,r,R,m,r"))]
  ""
  "* return mcore_output_movedouble (operands, DImode);"
  [(set_attr "length" "4") (set_attr "type" "move,move,move,move,load,load,store")])

;; SFmode

(define_expand "movsf"
  [(set (match_operand:SF 0 "general_operand" "")
	(match_operand:SF 1 "general_operand" ""))]
  ""
  "
{
  if (GET_CODE (operands[0]) == MEM)
    operands[1] = force_reg (SFmode, operands[1]);
}")

(define_insn "movsf_i"
  [(set (match_operand:SF 0 "general_operand" "=r,r,m")
	(match_operand:SF 1 "general_operand"  "r,m,r"))]
  ""
  "@
	mov	%0,%1
	ld.w	%0,%1
	st.w	%1,%0"
  [(set_attr "type" "move,load,store")])

;; DFmode

(define_expand "movdf"
  [(set (match_operand:DF 0 "general_operand" "")
	(match_operand:DF 1 "general_operand" ""))]
  ""
  "
{
  if (GET_CODE (operands[0]) == MEM)
    operands[1] = force_reg (DFmode, operands[1]);
}")

(define_insn "movdf_k"
  [(set (match_operand:DF 0 "general_operand" "=r,r,m")
	(match_operand:DF 1 "general_operand" "r,m,r"))]
  ""
  "* return mcore_output_movedouble (operands, DFmode);"
  [(set_attr "length" "4") (set_attr "type" "move,load,store")])


;; Load/store multiple

;; ??? This is not currently used.
(define_insn "ldm"
  [(set (match_operand:TI 0 "mcore_arith_reg_operand" "=r")
	(mem:TI (match_operand:SI 1 "mcore_arith_reg_operand" "r")))]
  ""
  "ldq	%U0,(%1)")

;; ??? This is not currently used.
(define_insn "stm"
  [(set (mem:TI (match_operand:SI 0 "mcore_arith_reg_operand" "r"))
	(match_operand:TI 1 "mcore_arith_reg_operand" "r"))]
  ""
  "stq	%U1,(%0)")

(define_expand "load_multiple"
  [(match_par_dup 3 [(set (match_operand:SI 0 "" "")
			  (match_operand:SI 1 "" ""))
		     (use (match_operand:SI 2 "" ""))])]
  ""
  "
{
  int regno, count, i;

  /* Support only loading a constant number of registers from memory and
     only if at least two registers.  The last register must be r15.  */
  if (GET_CODE (operands[2]) != CONST_INT
      || INTVAL (operands[2]) < 2
      || GET_CODE (operands[1]) != MEM
      || XEXP (operands[1], 0) != stack_pointer_rtx
      || GET_CODE (operands[0]) != REG
      || REGNO (operands[0]) + INTVAL (operands[2]) != 16)
    FAIL;

  count = INTVAL (operands[2]);
  regno = REGNO (operands[0]);

  operands[3] = gen_rtx_PARALLEL (VOIDmode, rtvec_alloc (count));

  for (i = 0; i < count; i++)
    XVECEXP (operands[3], 0, i)
      = gen_rtx_SET (VOIDmode,
		 gen_rtx_REG (SImode, regno + i),
		 gen_rtx_MEM (SImode, plus_constant (stack_pointer_rtx,
						      i * 4)));
}")

(define_insn ""
  [(match_parallel 0 "mcore_load_multiple_operation"
		   [(set (match_operand:SI 1 "mcore_arith_reg_operand" "=r")
			 (mem:SI (match_operand:SI 2 "register_operand" "r")))])]
  "GET_CODE (operands[2]) == REG && REGNO (operands[2]) == STACK_POINTER_REGNUM"
  "ldm	%1-r15,(%2)")

(define_expand "store_multiple"
  [(match_par_dup 3 [(set (match_operand:SI 0 "" "")
			  (match_operand:SI 1 "" ""))
		     (use (match_operand:SI 2 "" ""))])]
  ""
  "
{
  int regno, count, i;

  /* Support only storing a constant number of registers to memory and
     only if at least two registers.  The last register must be r15.  */
  if (GET_CODE (operands[2]) != CONST_INT
      || INTVAL (operands[2]) < 2
      || GET_CODE (operands[0]) != MEM
      || XEXP (operands[0], 0) != stack_pointer_rtx
      || GET_CODE (operands[1]) != REG
      || REGNO (operands[1]) + INTVAL (operands[2]) != 16)
    FAIL;

  count = INTVAL (operands[2]);
  regno = REGNO (operands[1]);

  operands[3] = gen_rtx_PARALLEL (VOIDmode, rtvec_alloc (count));

  for (i = 0; i < count; i++)
    XVECEXP (operands[3], 0, i)
      = gen_rtx_SET (VOIDmode,
		 gen_rtx_MEM (SImode, plus_constant (stack_pointer_rtx,
						      i * 4)),
		 gen_rtx_REG (SImode, regno + i));
}")

(define_insn ""
  [(match_parallel 0 "mcore_store_multiple_operation"
		   [(set (mem:SI (match_operand:SI 2 "register_operand" "r"))
			 (match_operand:SI 1 "mcore_arith_reg_operand" "r"))])]
  "GET_CODE (operands[2]) == REG && REGNO (operands[2]) == STACK_POINTER_REGNUM"
  "stm	%1-r15,(%2)")

;; ------------------------------------------------------------------------
;; Define the real conditional branch instructions.
;; ------------------------------------------------------------------------

(define_insn "branch_true"
  [(set (pc) (if_then_else (ne (reg:CC 17) (const_int 0))
			   (label_ref (match_operand 0 "" ""))
			   (pc)))]
  ""
  "jbt	%l0"
  [(set_attr "type" "brcond")])

(define_insn "branch_false"
  [(set (pc) (if_then_else (eq (reg:CC 17) (const_int 0))
			   (label_ref (match_operand 0 "" ""))
			   (pc)))]
  ""
  "jbf	%l0"
  [(set_attr "type" "brcond")])

(define_insn "inverse_branch_true"
  [(set (pc) (if_then_else (ne (reg:CC 17) (const_int 0))
			   (pc)
			   (label_ref (match_operand 0 "" ""))))]
  ""
  "jbf	%l0"
  [(set_attr "type" "brcond")])

(define_insn "inverse_branch_false"
  [(set (pc) (if_then_else (eq (reg:CC 17) (const_int 0))
   			   (pc)
			   (label_ref (match_operand 0 "" ""))))]
  ""
  "jbt	%l0"
  [(set_attr "type" "brcond")])

;; Conditional branch insns

;; At top-level, condition test are eq/ne, because we
;; are comparing against the condition register (which
;; has the result of the true relational test

; There is no beq compare, so we reverse the branch arms.

(define_expand "beq"
  [(set (pc) (if_then_else (ne (match_dup 1) (const_int 0))
			   (pc)
			   (label_ref (match_operand 0 "" ""))))]
  ""
  "
{
  operands[1] = mcore_gen_compare_reg (EQ);
}")

(define_expand "bne"
  [(set (pc) (if_then_else (ne (match_dup 1) (const_int 0))
			   (label_ref (match_operand 0 "" ""))
			   (pc)))]
  ""
  "
{
  operands[1] = mcore_gen_compare_reg (NE);
}")

; check whether (GT A imm) can become (LE A imm) with the branch reversed.  
; if so, emit a (LT A imm + 1) in place of the (LE A imm).  BRC

(define_expand "bgt"
  [(set (pc) (if_then_else (ne (match_dup 1) (const_int 0))
			   (label_ref (match_operand 0 "" ""))
			   (pc)))]
  ""
  "
{
  if (mcore_modify_comparison (LE))
    {
      emit_jump_insn (gen_reverse_blt (operands[0]));
      DONE;
    }
  operands[1] = mcore_gen_compare_reg (GT);
}")

; There is no ble compare, so we reverse the branch arms.
; reversed the condition and branch arms for ble -- the check_dbra_loop()
; transformation assumes that ble uses a branch-true with the label as
; as the target. BRC

; check whether (LE A imm) can become (LT A imm + 1).

(define_expand "ble"
  [(set (pc) (if_then_else (eq (match_dup 1) (const_int 0))
			   (label_ref (match_operand 0 "" ""))
                           (pc)))]
  ""
  "
{
  if (mcore_modify_comparison (LE))
    {
      emit_jump_insn (gen_blt (operands[0]));
      DONE;
    }
  operands[1] = mcore_gen_compare_reg (LE);
}")

; make generating a reversed blt simple
(define_expand "reverse_blt"
  [(set (pc) (if_then_else (ne (match_dup 1) (const_int 0))
                           (pc)
                           (label_ref (match_operand 0 "" ""))))]
  ""
  "
{
  operands[1] = mcore_gen_compare_reg (LT);
}")

(define_expand "blt"
  [(set (pc) (if_then_else (ne (match_dup 1) (const_int 0))
			   (label_ref (match_operand 0 "" ""))
			   (pc)))]
  ""
  "
{
  operands[1] = mcore_gen_compare_reg (LT);
}")

; There is no bge compare, so we reverse the branch arms.

(define_expand "bge"
  [(set (pc) (if_then_else (ne (match_dup 1) (const_int 0))
			   (pc)
			   (label_ref (match_operand 0 "" ""))))]
  ""
  "
{
  operands[1] = mcore_gen_compare_reg (GE);
}")

; There is no gtu compare, so we reverse the branch arms

;(define_expand "bgtu"
;  [(set (pc) (if_then_else (ne (match_dup 1) (const_int 0))
;			   (pc)
;			   (label_ref (match_operand 0 "" ""))))]
;  ""
;  "
;{
;  if (GET_CODE (arch_compare_op1) == CONST_INT
;      && INTVAL (arch_compare_op1) == 0)
;    operands[1] = mcore_gen_compare_reg (NE);
;  else 
;    { if (mcore_modify_comparison (GTU))
;	{
;	  emit_jump_insn (gen_bgeu (operands[0]));
;	  DONE;
;	}
;      operands[1] = mcore_gen_compare_reg (LEU);
;    }
;}")

(define_expand "bgtu"
  [(set (pc) (if_then_else (ne (match_dup 1) (const_int 0))
			   (pc)
			   (label_ref (match_operand 0 "" ""))))]
  ""
  "
{
  if (GET_CODE (arch_compare_op1) == CONST_INT
      && INTVAL (arch_compare_op1) == 0)
    {
      /* The inverse of '> 0' for an unsigned test is
	 '== 0' but we do not have such an instruction available.
	 Instead we must reverse the branch (back to the normal
	 ordering) and test '!= 0'.  */
	 
      operands[1] = mcore_gen_compare_reg (NE);
      
      emit_jump_insn (gen_rtx_SET (VOIDmode,
	pc_rtx,
	gen_rtx_IF_THEN_ELSE (VOIDmode,
	gen_rtx_NE (VOIDmode,
	operands[1],
	const0_rtx),
	gen_rtx_LABEL_REF (VOIDmode,operands[0]),
	pc_rtx)));
      DONE;	      
    }
  operands[1] = mcore_gen_compare_reg (GTU);
}")


(define_expand "bleu"
  [(set (pc) (if_then_else (ne (match_dup 1) (const_int 0))
			   (label_ref (match_operand 0 "" ""))
			   (pc)))]
  ""
  "
{
  operands[1] = mcore_gen_compare_reg (LEU);
}")

; There is no bltu compare, so we reverse the branch arms
(define_expand "bltu"
  [(set (pc) (if_then_else (ne (match_dup 1) (const_int 0))
			   (pc)
			   (label_ref (match_operand 0 "" ""))))]
  ""
  "
{
  operands[1] = mcore_gen_compare_reg (LTU);
}")

(define_expand "bgeu"
  [(set (pc) (if_then_else (ne (match_dup 1) (const_int 0))
			   (label_ref (match_operand 0 "" ""))
			   (pc)))]
  ""
  "
{

  operands[1] = mcore_gen_compare_reg (GEU);
}")

;; ------------------------------------------------------------------------
;; Jump and linkage insns
;; ------------------------------------------------------------------------

(define_insn "jump_real"
  [(set (pc)
	(label_ref (match_operand 0 "" "")))]
  ""
  "jbr	%l0"
  [(set_attr "type" "branch")])

(define_expand "jump"
 [(set (pc) (label_ref (match_operand 0 "" "")))]
 ""
 "
{
  emit_jump_insn (gen_jump_real (operand0));
  DONE;
}
")

(define_insn "indirect_jump"
  [(set (pc)
	(match_operand:SI 0 "mcore_arith_reg_operand" "r"))]
  ""
  "jmp	%0"
  [(set_attr "type" "jmp")])

(define_expand "call"
  [(parallel[(call (match_operand:SI 0 "" "")
		   (match_operand 1 "" ""))
	     (clobber (reg:SI 15))])]
  ""
  "
{
  if (GET_CODE (operands[0]) == MEM
      && ! register_operand (XEXP (operands[0], 0), SImode)
      && ! mcore_symbolic_address_p (XEXP (operands[0], 0)))
    operands[0] = gen_rtx_MEM (GET_MODE (operands[0]),
			   force_reg (Pmode, XEXP (operands[0], 0)));
}")

(define_insn "call_internal"
  [(call (mem:SI (match_operand:SI 0 "mcore_call_address_operand" "riR"))
	 (match_operand 1 "" ""))
   (clobber (reg:SI 15))]
  ""
  "* return mcore_output_call (operands, 0);")

(define_expand "call_value"
  [(parallel[(set (match_operand 0 "register_operand" "")
		  (call (match_operand:SI 1 "" "")
			(match_operand 2 "" "")))
	     (clobber (reg:SI 15))])]
  ""
  "
{
  if (GET_CODE (operands[0]) == MEM
      && ! register_operand (XEXP (operands[0], 0), SImode)
      && ! mcore_symbolic_address_p (XEXP (operands[0], 0)))
    operands[1] = gen_rtx_MEM (GET_MODE (operands[1]),
			   force_reg (Pmode, XEXP (operands[1], 0)));
}")

(define_insn "call_value_internal"
  [(set (match_operand 0 "register_operand" "=r")
	(call (mem:SI (match_operand:SI 1 "mcore_call_address_operand" "riR"))
	      (match_operand 2 "" "")))
   (clobber (reg:SI 15))]
  ""
  "* return mcore_output_call (operands, 1);")

(define_insn "call_value_struct"
  [(parallel [(set (match_parallel 0 ""
	             [(expr_list (match_operand 3 "register_operand" "") (match_operand 4 "immediate_operand" ""))
		      (expr_list (match_operand 5 "register_operand" "") (match_operand 6 "immediate_operand" ""))])
		  (call (match_operand:SI 1 "" "")
			(match_operand 2 "" "")))
	     (clobber (reg:SI 15))])]
  ""
  "* return mcore_output_call (operands, 1);"
)


;; ------------------------------------------------------------------------
;; Misc insns
;; ------------------------------------------------------------------------

(define_insn "nop"
  [(const_int 0)]
  ""
  "or	r0,r0")

(define_insn "tablejump"
  [(set (pc)
	(match_operand:SI 0 "mcore_arith_reg_operand" "r"))
   (use (label_ref (match_operand 1 "" "")))]
  ""
  "jmp	%0"
  [(set_attr "type" "jmp")])

(define_insn "*return"
 [(return)]
 "reload_completed && ! mcore_naked_function_p ()"
 "jmp	r15"
 [(set_attr "type" "jmp")])

(define_insn "*no_return"
 [(return)]
 "reload_completed && mcore_naked_function_p ()"
 ""
 [(set_attr "length" "0")]
)

(define_expand "prologue"
  [(const_int 0)]
  ""
  "mcore_expand_prolog (); DONE;")

(define_expand "epilogue"
  [(return)]
  ""
  "mcore_expand_epilog ();")

;; ------------------------------------------------------------------------
;; Scc instructions
;; ------------------------------------------------------------------------

(define_insn "mvc"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
	(ne:SI (reg:CC 17) (const_int 0)))]
  ""
  "mvc	%0"
  [(set_attr "type" "move")])

(define_insn "mvcv"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
	(eq:SI (reg:CC 17) (const_int 0)))]
  ""
  "mvcv	%0"
  [(set_attr "type" "move")])

; in 0.97 use (LE 0) with (LT 1) and complement c.  BRC
(define_split 
  [(parallel[
     (set (match_operand:SI 0 "mcore_arith_reg_operand" "")
          (ne:SI (gt:CC (match_operand:SI 1 "mcore_arith_reg_operand" "")
                        (const_int 0))
                 (const_int 0)))
     (clobber (reg:SI 17))])]
  ""
  [(set (reg:CC 17)
        (lt:CC (match_dup 1) (const_int 1)))
   (set (match_dup 0) (eq:SI (reg:CC 17) (const_int 0)))])
     

(define_expand "seq"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "")
	(eq:SI (match_dup 1) (const_int 0)))]
  ""
  "
{
  operands[1] = mcore_gen_compare_reg (NE);
}")

(define_expand "sne"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "")
	(ne:SI (match_dup 1) (const_int 0)))]
  ""
  "
{
  operands[1] = mcore_gen_compare_reg (NE);
}")

(define_expand "slt"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "")
	(ne:SI (match_dup 1) (const_int 0)))]
  ""
  "
{
  operands[1] = mcore_gen_compare_reg (LT);
}")

; make generating a LT with the comparison reversed easy.  BRC
(define_expand "reverse_slt"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "")
        (eq:SI (match_dup 1) (const_int 0)))]
  ""
  "
{
  operands[1] = mcore_gen_compare_reg (LT);
}")

(define_expand "sge"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "")
	(eq:SI (match_dup 1) (const_int 0)))]
  ""
  "
{
  operands[1] = mcore_gen_compare_reg (LT);
}")

; check whether (GT A imm) can become (LE A imm) with the comparison
; reversed.  if so, emit a (LT A imm + 1) in place of the (LE A imm).  BRC

(define_expand "sgt"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "")
	(ne:SI (match_dup 1) (const_int 0)))]
  ""
  "
{
  if (mcore_modify_comparison (LE))
    {
      emit_insn (gen_reverse_slt (operands[0]));
      DONE;
    }
  
  operands[1] = mcore_gen_compare_reg (GT);
}")

(define_expand "sle"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "")
	(eq:SI (match_dup 1) (const_int 0)))]
  ""
  "
{
  if (mcore_modify_comparison (LE))
    {
      emit_insn (gen_slt (operands[0]));
      DONE;
    }
  operands[1] = mcore_gen_compare_reg (GT);
}")

(define_expand "sltu"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "")
	(eq:SI (match_dup 1) (const_int 0)))]
  ""
  "
{
  operands[1] = mcore_gen_compare_reg (GEU);
}")

(define_expand "sgeu"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "")
	(ne:SI (match_dup 1) (const_int 0)))]
  ""
  "
{
  operands[1] = mcore_gen_compare_reg (GEU);
}")

(define_expand "sgtu"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "")
	(eq:SI (match_dup 1) (const_int 0)))]
  ""
  "
{
  operands[1] = mcore_gen_compare_reg (LEU);
}")

(define_expand "sleu"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "")
	(ne:SI (match_dup 1) (const_int 0)))]
  ""
  "
{
  operands[1] = mcore_gen_compare_reg (LEU);
}")

(define_insn "incscc"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
	(plus:SI (ne (reg:CC 17) (const_int 0))
		 (match_operand:SI 1 "mcore_arith_reg_operand" "0")))]
  ""
  "inct	%0")

(define_insn "incscc_false"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
	(plus:SI (eq (reg:CC 17) (const_int 0))
		 (match_operand:SI 1 "mcore_arith_reg_operand" "0")))]
  ""
  "incf	%0")

(define_insn "decscc"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
	(minus:SI (match_operand:SI 1 "mcore_arith_reg_operand" "0")
		  (ne (reg:CC 17) (const_int 0))))]
  ""
  "dect	%0")

(define_insn "decscc_false"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
	(minus:SI (match_operand:SI 1 "mcore_arith_reg_operand" "0")
		  (eq (reg:CC 17) (const_int 0))))]
  ""
  "decf	%0")

;; ------------------------------------------------------------------------
;; Conditional move patterns.
;; ------------------------------------------------------------------------

(define_expand "smaxsi3"
  [(set (reg:CC 17)
	(lt:CC (match_operand:SI 1 "mcore_arith_reg_operand" "")
	       (match_operand:SI 2 "mcore_arith_reg_operand" "")))
   (set (match_operand:SI 0 "mcore_arith_reg_operand" "")
	(if_then_else:SI (eq (reg:CC 17) (const_int 0))
			 (match_dup 1) (match_dup 2)))]
  ""
  "")
	       
(define_split
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "")
	(smax:SI (match_operand:SI 1 "mcore_arith_reg_operand" "")
		 (match_operand:SI 2 "mcore_arith_reg_operand" "")))]
  ""
  [(set (reg:CC 17)
	(lt:SI (match_dup 1) (match_dup 2)))
   (set (match_dup 0)
	(if_then_else:SI (eq (reg:CC 17) (const_int 0))
			 (match_dup 1) (match_dup 2)))]
  "")

; no tstgt in 0.97, so just use cmplti (btsti x,31) and reverse move 
; condition  BRC
(define_split
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "")
        (smax:SI (match_operand:SI 1 "mcore_arith_reg_operand" "")
                 (const_int 0)))]
  ""
  [(set (reg:CC 17)
        (lt:CC (match_dup 1) (const_int 0)))
   (set (match_dup 0)
        (if_then_else:SI (eq (reg:CC 17) (const_int 0))
                         (match_dup 1) (const_int 0)))]
  "")

(define_expand "sminsi3"
  [(set (reg:CC 17)
	(lt:CC (match_operand:SI 1 "mcore_arith_reg_operand" "")
	       (match_operand:SI 2 "mcore_arith_reg_operand" "")))
   (set (match_operand:SI 0 "mcore_arith_reg_operand" "")
	(if_then_else:SI (ne (reg:CC 17) (const_int 0))
			 (match_dup 1) (match_dup 2)))]
  ""
  "")

(define_split
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "")
	(smin:SI (match_operand:SI 1 "mcore_arith_reg_operand" "")
		 (match_operand:SI 2 "mcore_arith_reg_operand" "")))]
  ""
  [(set (reg:CC 17)
	(lt:SI (match_dup 1) (match_dup 2)))
   (set (match_dup 0)
	(if_then_else:SI (ne (reg:CC 17) (const_int 0))
			 (match_dup 1) (match_dup 2)))]
  "")

;(define_split
;  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "")
;        (smin:SI (match_operand:SI 1 "mcore_arith_reg_operand" "")
;                 (const_int 0)))]
;  ""
;  [(set (reg:CC 17)
;        (gt:CC (match_dup 1) (const_int 0)))
;   (set (match_dup 0)
;        (if_then_else:SI (eq (reg:CC 17) (const_int 0))
;                         (match_dup 1) (const_int 0)))]
;  "")

; changed these unsigned patterns to use geu instead of ltu.  it appears
; that the c-torture & ssrl test suites didn't catch these!  only showed
; up in friedman's clib work.   BRC 7/7/95

(define_expand "umaxsi3"
  [(set (reg:CC 17)
	(geu:CC (match_operand:SI 1 "mcore_arith_reg_operand" "")
		(match_operand:SI 2 "mcore_arith_reg_operand" "")))
   (set (match_operand:SI 0 "mcore_arith_reg_operand" "")
	(if_then_else:SI (eq (reg:CC 17) (const_int 0))
			 (match_dup 2) (match_dup 1)))]
  ""
  "")
	       
(define_split
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "")
	(umax:SI (match_operand:SI 1 "mcore_arith_reg_operand" "")
		 (match_operand:SI 2 "mcore_arith_reg_operand" "")))]
  ""
  [(set (reg:CC 17)
	(geu:SI (match_dup 1) (match_dup 2)))
   (set (match_dup 0)
	(if_then_else:SI (eq (reg:CC 17) (const_int 0))
			 (match_dup 2) (match_dup 1)))]
  "")

(define_expand "uminsi3"
  [(set (reg:CC 17)
	(geu:CC (match_operand:SI 1 "mcore_arith_reg_operand" "")
		(match_operand:SI 2 "mcore_arith_reg_operand" "")))
   (set (match_operand:SI 0 "mcore_arith_reg_operand" "")
	(if_then_else:SI (ne (reg:CC 17) (const_int 0))
			 (match_dup 2) (match_dup 1)))]
  ""
  "")

(define_split
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "")
	(umin:SI (match_operand:SI 1 "mcore_arith_reg_operand" "")
		 (match_operand:SI 2 "mcore_arith_reg_operand" "")))]
  ""
  [(set (reg:CC 17)
	(geu:SI (match_dup 1) (match_dup 2)))
   (set (match_dup 0)
	(if_then_else:SI (ne (reg:CC 17) (const_int 0))
			 (match_dup 2) (match_dup 1)))]
  "")

;; ------------------------------------------------------------------------
;; conditional move patterns really start here
;; ------------------------------------------------------------------------

;; the "movtK" patterns are experimental.  they are intended to account for
;; gcc's mucking on code such as:
;;
;;            free_ent = ((block_compress) ? 257 : 256 );
;;
;; these patterns help to get a tstne/bgeni/inct (or equivalent) sequence
;; when both arms have constants that are +/- 1 of each other.
;;
;; note in the following patterns that the "movtK" ones should be the first
;; one defined in each sequence.  this is because the general pattern also
;; matches, so use ordering to determine priority (it's easier this way than
;; adding conditions to the general patterns).   BRC
;;
;; the U and Q constraints are necessary to ensure that reload does the
;; 'right thing'.  U constrains the operand to 0 and Q to 1 for use in the
;; clrt & clrf and clrt/inct & clrf/incf patterns.    BRC 6/26
;;
;; ??? there appears to be some problems with these movtK patterns for ops
;; other than eq & ne.  need to fix.  6/30 BRC

;; ------------------------------------------------------------------------
;; ne 
;; ------------------------------------------------------------------------

; experimental conditional move with two constants +/- 1  BRC

(define_insn "movtK_1"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
        (if_then_else:SI
            (ne (reg:CC 17) (const_int 0))
          (match_operand:SI 1 "mcore_arith_O_operand" "O")
          (match_operand:SI 2 "mcore_arith_O_operand" "O")))]
  "  GET_CODE (operands[1]) == CONST_INT
  && GET_CODE (operands[2]) == CONST_INT
  && (   (INTVAL (operands[1]) - INTVAL (operands[2]) == 1)
      || (INTVAL (operands[2]) - INTVAL (operands[1]) == 1))"
  "* return mcore_output_cmov (operands, 1, NULL);"
  [(set_attr "length" "4")])

(define_insn "movt0"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r,r,r,r")
	(if_then_else:SI
	 (ne (reg:CC 17) (const_int 0))
	 (match_operand:SI 1 "mcore_arith_imm_operand" "r,0,U,0")
	 (match_operand:SI 2 "mcore_arith_imm_operand" "0,r,0,U")))]
  ""
  "@
    movt	%0,%1
    movf	%0,%2
    clrt	%0
    clrf	%0")

;; ------------------------------------------------------------------------
;; eq
;; ------------------------------------------------------------------------

; experimental conditional move with two constants +/- 1  BRC
(define_insn "movtK_2"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
        (if_then_else:SI
            (eq (reg:CC 17) (const_int 0))
          (match_operand:SI 1 "mcore_arith_O_operand" "O")
          (match_operand:SI 2 "mcore_arith_O_operand" "O")))]
  "  GET_CODE (operands[1]) == CONST_INT
  && GET_CODE (operands[2]) == CONST_INT
  && (   (INTVAL (operands[1]) - INTVAL (operands[2]) == 1)
      || (INTVAL (operands[2]) - INTVAL (operands[1]) == 1))"
  "* return mcore_output_cmov (operands, 0, NULL);"
  [(set_attr "length" "4")])

(define_insn "movf0"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r,r,r,r")
	(if_then_else:SI
	 (eq (reg:CC 17) (const_int 0))
	 (match_operand:SI 1 "mcore_arith_imm_operand" "r,0,U,0")
	 (match_operand:SI 2 "mcore_arith_imm_operand" "0,r,0,U")))]
  ""
  "@
    movf	%0,%1
    movt	%0,%2
    clrf	%0
    clrt	%0")

; turns lsli rx,imm/btsti rx,31 into btsti rx,imm.  not done by a peephole
; because the instructions are not adjacent (peepholes are related by posn -
; not by dataflow).   BRC

(define_insn ""
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r,r,r,r")
        (if_then_else:SI (eq (zero_extract:SI 
                              (match_operand:SI 1 "mcore_arith_reg_operand" "r,r,r,r")
                              (const_int 1)
                              (match_operand:SI 2 "mcore_literal_K_operand" "K,K,K,K"))
                             (const_int 0))
                         (match_operand:SI 3 "mcore_arith_imm_operand" "r,0,U,0")
                         (match_operand:SI 4 "mcore_arith_imm_operand" "0,r,0,U")))]
  ""
  "@
    btsti	%1,%2\;movf	%0,%3
    btsti	%1,%2\;movt	%0,%4
    btsti	%1,%2\;clrf	%0
    btsti	%1,%2\;clrt	%0"
  [(set_attr "length" "4")])

; turns sextb rx/btsti rx,31 into btsti rx,7.  must be QImode to be safe.  BRC

(define_insn ""
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r,r,r,r")
        (if_then_else:SI (eq (lshiftrt:SI 
                              (match_operand:SI 1 "mcore_arith_reg_operand" "r,r,r,r")
                              (const_int 7))
                             (const_int 0))
                         (match_operand:SI 2 "mcore_arith_imm_operand" "r,0,U,0")
                         (match_operand:SI 3 "mcore_arith_imm_operand" "0,r,0,U")))]
  "GET_CODE (operands[1]) == SUBREG && 
      GET_MODE (SUBREG_REG (operands[1])) == QImode"
  "@
    btsti	%1,7\;movf	%0,%2
    btsti	%1,7\;movt	%0,%3
    btsti	%1,7\;clrf	%0
    btsti	%1,7\;clrt	%0"
  [(set_attr "length" "4")])


;; ------------------------------------------------------------------------
;; ne
;; ------------------------------------------------------------------------

;; Combine creates this from an andn instruction in a scc sequence.
;; We must recognize it to get conditional moves generated.

; experimental conditional move with two constants +/- 1  BRC
(define_insn "movtK_3"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
        (if_then_else:SI
            (ne (match_operand:SI 1 "mcore_arith_reg_operand" "r") 
                (const_int 0))
          (match_operand:SI 2 "mcore_arith_O_operand" "O")
          (match_operand:SI 3 "mcore_arith_O_operand" "O")))]
  "  GET_CODE (operands[2]) == CONST_INT
  && GET_CODE (operands[3]) == CONST_INT
  && (   (INTVAL (operands[2]) - INTVAL (operands[3]) == 1)
      || (INTVAL (operands[3]) - INTVAL (operands[2]) == 1))"
  "*
{
  rtx out_operands[4];
  out_operands[0] = operands[0];
  out_operands[1] = operands[2];
  out_operands[2] = operands[3];
  out_operands[3] = operands[1];

  return mcore_output_cmov (out_operands, 1, \"cmpnei	%3,0\");

}"
  [(set_attr "length" "6")])

(define_insn "movt2"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r,r,r,r")
	(if_then_else:SI (ne (match_operand:SI 1 "mcore_arith_reg_operand" "r,r,r,r")
			     (const_int 0))
			 (match_operand:SI 2 "mcore_arith_imm_operand" "r,0,U,0")
			 (match_operand:SI 3 "mcore_arith_imm_operand" "0,r,0,U")))]
  ""      
  "@
    cmpnei	%1,0\;movt	%0,%2
    cmpnei	%1,0\;movf	%0,%3
    cmpnei	%1,0\;clrt	%0
    cmpnei	%1,0\;clrf	%0"
  [(set_attr "length" "4")])

; turns lsli rx,imm/btsti rx,31 into btsti rx,imm.  not done by a peephole
; because the instructions are not adjacent (peepholes are related by posn -
; not by dataflow).   BRC

(define_insn ""
 [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r,r,r,r")
        (if_then_else:SI (ne (zero_extract:SI 
                              (match_operand:SI 1 "mcore_arith_reg_operand" "r,r,r,r")
                              (const_int 1)
                              (match_operand:SI 2 "mcore_literal_K_operand" "K,K,K,K"))
                             (const_int 0))
                         (match_operand:SI 3 "mcore_arith_imm_operand" "r,0,U,0")
                         (match_operand:SI 4 "mcore_arith_imm_operand" "0,r,0,U")))]
  ""
  "@
    btsti	%1,%2\;movt	%0,%3
    btsti	%1,%2\;movf	%0,%4
    btsti	%1,%2\;clrt	%0
    btsti	%1,%2\;clrf	%0"
  [(set_attr "length" "4")])

; turns sextb rx/btsti rx,31 into btsti rx,7.  must be QImode to be safe.  BRC

(define_insn ""
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r,r,r,r")
        (if_then_else:SI (ne (lshiftrt:SI 
                              (match_operand:SI 1 "mcore_arith_reg_operand" "r,r,r,r")
                              (const_int 7))
                             (const_int 0))
                         (match_operand:SI 2 "mcore_arith_imm_operand" "r,0,U,0")
                         (match_operand:SI 3 "mcore_arith_imm_operand" "0,r,0,U")))]
  "GET_CODE (operands[1]) == SUBREG && 
      GET_MODE (SUBREG_REG (operands[1])) == QImode"
  "@
    btsti	%1,7\;movt	%0,%2
    btsti	%1,7\;movf	%0,%3
    btsti	%1,7\;clrt	%0
    btsti	%1,7\;clrf	%0"
  [(set_attr "length" "4")])

;; ------------------------------------------------------------------------
;; eq/eq
;; ------------------------------------------------------------------------

; experimental conditional move with two constants +/- 1  BRC
(define_insn "movtK_4"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
        (if_then_else:SI
            (eq (eq:SI (reg:CC 17) (const_int 0)) (const_int 0))
          (match_operand:SI 1 "mcore_arith_O_operand" "O")
          (match_operand:SI 2 "mcore_arith_O_operand" "O")))]
  "GET_CODE (operands[1]) == CONST_INT &&
   GET_CODE (operands[2]) == CONST_INT &&
   ((INTVAL (operands[1]) - INTVAL (operands[2]) == 1) ||
   (INTVAL (operands[2]) - INTVAL (operands[1]) == 1))"
  "* return mcore_output_cmov(operands, 1, NULL);"
  [(set_attr "length" "4")])

(define_insn "movt3"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r,r,r,r")
	(if_then_else:SI
	 (eq (eq:SI (reg:CC 17) (const_int 0)) (const_int 0))
	 (match_operand:SI 1 "mcore_arith_imm_operand" "r,0,U,0")
	 (match_operand:SI 2 "mcore_arith_imm_operand" "0,r,0,U")))]
  ""
  "@
    movt	%0,%1
    movf	%0,%2
    clrt	%0
    clrf	%0")

;; ------------------------------------------------------------------------
;; eq/ne
;; ------------------------------------------------------------------------

; experimental conditional move with two constants +/- 1  BRC
(define_insn "movtK_5"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
        (if_then_else:SI
            (eq (ne:SI (reg:CC 17) (const_int 0)) (const_int 0))
          (match_operand:SI 1 "mcore_arith_O_operand" "O")
          (match_operand:SI 2 "mcore_arith_O_operand" "O")))]
  "GET_CODE (operands[1]) == CONST_INT &&
   GET_CODE (operands[2]) == CONST_INT &&
   ((INTVAL (operands[1]) - INTVAL (operands[2]) == 1) ||
    (INTVAL (operands[2]) - INTVAL (operands[1]) == 1))"
  "* return mcore_output_cmov (operands, 0, NULL);"
  [(set_attr "length" "4")])

(define_insn "movf1"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r,r,r,r")
	(if_then_else:SI
	 (eq (ne:SI (reg:CC 17) (const_int 0)) (const_int 0))
	 (match_operand:SI 1 "mcore_arith_imm_operand" "r,0,U,0")
	 (match_operand:SI 2 "mcore_arith_imm_operand" "0,r,0,U")))]
  ""
  "@
    movf	%0,%1
    movt	%0,%2
    clrf	%0
    clrt	%0")

;; ------------------------------------------------------------------------
;; eq
;; ------------------------------------------------------------------------

;; Combine creates this from an andn instruction in a scc sequence.
;; We must recognize it to get conditional moves generated.

; experimental conditional move with two constants +/- 1  BRC

(define_insn "movtK_6"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
        (if_then_else:SI
            (eq (match_operand:SI 1 "mcore_arith_reg_operand" "r") 
                (const_int 0))
          (match_operand:SI 2 "mcore_arith_O_operand" "O")
          (match_operand:SI 3 "mcore_arith_O_operand" "O")))]
  "GET_CODE (operands[1]) == CONST_INT &&
   GET_CODE (operands[2]) == CONST_INT &&
   ((INTVAL (operands[2]) - INTVAL (operands[3]) == 1) ||
    (INTVAL (operands[3]) - INTVAL (operands[2]) == 1))"
  "* 
{
   rtx out_operands[4];
   out_operands[0] = operands[0];
   out_operands[1] = operands[2];
   out_operands[2] = operands[3];
   out_operands[3] = operands[1];

   return mcore_output_cmov (out_operands, 0, \"cmpnei	%3,0\");
}"
  [(set_attr "length" "6")])

(define_insn "movf3"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r,r,r,r")
	(if_then_else:SI (eq (match_operand:SI 1 "mcore_arith_reg_operand" "r,r,r,r")
			     (const_int 0))
			 (match_operand:SI 2 "mcore_arith_imm_operand" "r,0,U,0")
			 (match_operand:SI 3 "mcore_arith_imm_operand" "0,r,0,U")))]
  ""
  "@
    cmpnei	%1,0\;movf	%0,%2
    cmpnei	%1,0\;movt	%0,%3
    cmpnei	%1,0\;clrf	%0
    cmpnei	%1,0\;clrt	%0"
  [(set_attr "length" "4")])

;; ------------------------------------------------------------------------
;; ne/eq
;; ------------------------------------------------------------------------

; experimental conditional move with two constants +/- 1  BRC
(define_insn "movtK_7"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
        (if_then_else:SI
            (ne (eq:SI (reg:CC 17) (const_int 0)) (const_int 0))
          (match_operand:SI 1 "mcore_arith_O_operand" "O")
          (match_operand:SI 2 "mcore_arith_O_operand" "O")))]
  "GET_CODE (operands[1]) == CONST_INT &&
   GET_CODE (operands[2]) == CONST_INT &&
   ((INTVAL (operands[1]) - INTVAL (operands[2]) == 1) ||
    (INTVAL (operands[2]) - INTVAL (operands[1]) == 1))"
  "* return mcore_output_cmov (operands, 0, NULL);"
  [(set_attr "length" "4")])

(define_insn "movf4"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r,r,r,r")
	(if_then_else:SI
	 (ne (eq:SI (reg:CC 17) (const_int 0)) (const_int 0))
	 (match_operand:SI 1 "mcore_arith_imm_operand" "r,0,U,0")
	 (match_operand:SI 2 "mcore_arith_imm_operand" "0,r,0,U")))]
  ""
  "@
    movf	%0,%1
    movt	%0,%2
    clrf	%0
    clrt	%0")

;; ------------------------------------------------------------------------
;; ne/ne
;; ------------------------------------------------------------------------

; experimental conditional move with two constants +/- 1  BRC
(define_insn "movtK_8"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
        (if_then_else:SI
            (ne (ne:SI (reg:CC 17) (const_int 0)) (const_int 0))
          (match_operand:SI 1 "mcore_arith_O_operand" "O")
          (match_operand:SI 2 "mcore_arith_O_operand" "O")))]
  "GET_CODE (operands[1]) == CONST_INT &&
   GET_CODE (operands[2]) == CONST_INT &&
   ((INTVAL (operands[1]) - INTVAL (operands[2]) == 1) ||
    (INTVAL (operands[2]) - INTVAL (operands[1]) == 1))"
  "* return mcore_output_cmov (operands, 1, NULL);"
  [(set_attr "length" "4")])

(define_insn "movt4"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r,r,r,r")
	(if_then_else:SI
	 (ne (ne:SI (reg:CC 17) (const_int 0)) (const_int 0))
	 (match_operand:SI 1 "mcore_arith_imm_operand" "r,0,U,0")
	 (match_operand:SI 2 "mcore_arith_imm_operand" "0,r,0,U")))]
  ""
  "@
    movt	%0,%1
    movf	%0,%2
    clrt	%0
    clrf	%0")

;; Also need patterns to recognize lt/ge, since otherwise the compiler will
;; try to output not/asri/tstne/movf.

;; ------------------------------------------------------------------------
;; lt
;; ------------------------------------------------------------------------

; experimental conditional move with two constants +/- 1  BRC
(define_insn "movtK_9"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
        (if_then_else:SI
            (lt (match_operand:SI 1 "mcore_arith_reg_operand" "r") 
                (const_int 0))
          (match_operand:SI 2 "mcore_arith_O_operand" "O")
          (match_operand:SI 3 "mcore_arith_O_operand" "O")))]
  "GET_CODE (operands[2]) == CONST_INT &&
   GET_CODE (operands[3]) == CONST_INT &&
   ((INTVAL (operands[2]) - INTVAL (operands[3]) == 1) ||
    (INTVAL (operands[3]) - INTVAL (operands[2]) == 1))"
  "*
{
   rtx out_operands[4];
   out_operands[0] = operands[0];
   out_operands[1] = operands[2];
   out_operands[2] = operands[3];
   out_operands[3] = operands[1];

   return mcore_output_cmov (out_operands, 1, \"btsti	%3,31\");
}"
  [(set_attr "length" "6")])

(define_insn "movt5"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r,r,r,r")
	(if_then_else:SI (lt (match_operand:SI 1 "mcore_arith_reg_operand" "r,r,r,r")
			     (const_int 0))
			 (match_operand:SI 2 "mcore_arith_imm_operand" "r,0,U,0")
			 (match_operand:SI 3 "mcore_arith_imm_operand" "0,r,0,U")))]
  ""
  "@
    btsti	%1,31\;movt	%0,%2
    btsti	%1,31\;movf	%0,%3
    btsti	%1,31\;clrt	%0
    btsti	%1,31\;clrf	%0"
  [(set_attr "length" "4")])


;; ------------------------------------------------------------------------
;; ge
;; ------------------------------------------------------------------------

; experimental conditional move with two constants +/- 1  BRC
(define_insn "movtK_10"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
        (if_then_else:SI
            (ge (match_operand:SI 1 "mcore_arith_reg_operand" "r") 
                (const_int 0))
          (match_operand:SI 2 "mcore_arith_O_operand" "O")
          (match_operand:SI 3 "mcore_arith_O_operand" "O")))]
  "GET_CODE (operands[2]) == CONST_INT &&
   GET_CODE (operands[3]) == CONST_INT &&
   ((INTVAL (operands[2]) - INTVAL (operands[3]) == 1) ||
    (INTVAL (operands[3]) - INTVAL (operands[2]) == 1))"
  "*
{
  rtx out_operands[4];
  out_operands[0] = operands[0];
  out_operands[1] = operands[2];
  out_operands[2] = operands[3];
  out_operands[3] = operands[1];

   return mcore_output_cmov (out_operands, 0, \"btsti	%3,31\");
}"
  [(set_attr "length" "6")])

(define_insn "movf5"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r,r,r,r")
	(if_then_else:SI (ge (match_operand:SI 1 "mcore_arith_reg_operand" "r,r,r,r")
			     (const_int 0))
			 (match_operand:SI 2 "mcore_arith_imm_operand" "r,0,U,0")
			 (match_operand:SI 3 "mcore_arith_imm_operand" "0,r,0,U")))]
  ""
  "@
    btsti	%1,31\;movf	%0,%2
    btsti	%1,31\;movt	%0,%3
    btsti	%1,31\;clrf	%0
    btsti	%1,31\;clrt	%0"
  [(set_attr "length" "4")])

;; ------------------------------------------------------------------------
;; Bitfield extract (xtrbN)
;; ------------------------------------------------------------------------

; sometimes we're better off using QI/HI mode and letting the machine indep.
; part expand insv and extv.
;
; e.g., sequences like:a	[an insertion]
;
;      ldw r8,(r6)
;      movi r7,0x00ffffff
;      and r8,r7                 r7 dead
;      stw r8,(r6)                r8 dead
;
; become:
;
;      movi r8,0
;      stb r8,(r6)              r8 dead
;
; it looks like always using SI mode is a win except in this type of code 
; (when adjacent bit fields collapse on a byte or halfword boundary).  when
; expanding with SI mode, non-adjacent bit field masks fold, but with QI/HI
; mode, they do not.  one thought is to add some peepholes to cover cases
; like the above, but this is not a general solution.
;
; -mword-bitfields expands/inserts using SI mode.  otherwise, do it with
; the smallest mode possible (using the machine indep. expansions).  BRC

;(define_expand "extv"
;  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "")
;	(sign_extract:SI (match_operand:SI 1 "mcore_arith_reg_operand" "")
;			 (match_operand:SI 2 "const_int_operand" "")
;			 (match_operand:SI 3 "const_int_operand" "")))
;   (clobber (reg:CC 17))]
;  ""
;  "
;{
;  if (INTVAL (operands[1]) != 8 || INTVAL (operands[2]) % 8 != 0)
;    {
;     if (TARGET_W_FIELD)
;       {
;        rtx lshft = GEN_INT (32 - (INTVAL (operands[2]) + INTVAL (operands[3])));
;        rtx rshft = GEN_INT (32 - INTVAL (operands[2]));
;
;        emit_insn (gen_rtx_SET (SImode, operands[0], operands[1]));
;        emit_insn (gen_rtx_SET (SImode, operands[0],
;                            gen_rtx_ASHIFT (SImode, operands[0], lshft)));
;        emit_insn (gen_rtx_SET (SImode, operands[0],
;                            gen_rtx_ASHIFTRT (SImode, operands[0], rshft)));
;        DONE;
;     }
;     else
;        FAIL;
;  }
;}")

(define_expand "extv"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "")
	(sign_extract:SI (match_operand:SI 1 "mcore_arith_reg_operand" "")
			 (match_operand:SI 2 "const_int_operand" "")
			 (match_operand:SI 3 "const_int_operand" "")))
   (clobber (reg:CC 17))]
  ""
  "
{
  if (INTVAL (operands[2]) == 8 && INTVAL (operands[3]) % 8 == 0)
    {
       /* 8 bit field, aligned properly, use the xtrb[0123]+sext sequence.  */
       /* not DONE, not FAIL, but let the RTL get generated....  */
    }
  else if (TARGET_W_FIELD)
    {
      /* Arbitrary placement; note that the tree->rtl generator will make
         something close to this if we return FAIL  */
      rtx lshft = GEN_INT (32 - (INTVAL (operands[2]) + INTVAL (operands[3])));
      rtx rshft = GEN_INT (32 - INTVAL (operands[2]));
      rtx tmp1 = gen_reg_rtx (SImode);
      rtx tmp2 = gen_reg_rtx (SImode);

      emit_insn (gen_rtx_SET (SImode, tmp1, operands[1]));
      emit_insn (gen_rtx_SET (SImode, tmp2,
                         gen_rtx_ASHIFT (SImode, tmp1, lshft)));
      emit_insn (gen_rtx_SET (SImode, operands[0],
                         gen_rtx_ASHIFTRT (SImode, tmp2, rshft)));
      DONE;
    }
  else
    {
      /* Let the caller choose an alternate sequence.  */
      FAIL;
    }
}")

(define_expand "extzv"
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "")
	(zero_extract:SI (match_operand:SI 1 "mcore_arith_reg_operand" "")
			 (match_operand:SI 2 "const_int_operand" "")
			 (match_operand:SI 3 "const_int_operand" "")))
   (clobber (reg:CC 17))]
  ""
  "
{
  if (INTVAL (operands[2]) == 8 && INTVAL (operands[3]) % 8 == 0)
    {
       /* 8 bit field, aligned properly, use the xtrb[0123] sequence.  */
       /* Let the template generate some RTL....  */
    }
  else if (CONST_OK_FOR_K ((1 << INTVAL (operands[2])) - 1))
    {
      /* A narrow bit-field (<=5 bits) means we can do a shift to put
         it in place and then use an andi to extract it.
         This is as good as a shiftleft/shiftright.  */

      rtx shifted;
      rtx mask = GEN_INT ((1 << INTVAL (operands[2])) - 1);

      if (INTVAL (operands[3]) == 0)
        {
          shifted = operands[1];
        }
      else
        {
          rtx rshft = GEN_INT (INTVAL (operands[3]));
          shifted = gen_reg_rtx (SImode);
          emit_insn (gen_rtx_SET (SImode, shifted,
                         gen_rtx_LSHIFTRT (SImode, operands[1], rshft)));
        }
     emit_insn (gen_rtx_SET (SImode, operands[0],
                       gen_rtx_AND (SImode, shifted, mask)));
     DONE;
   }
 else if (TARGET_W_FIELD)
   {
     /* Arbitrary pattern; play shift/shift games to get it. 
      * this is pretty much what the caller will do if we say FAIL */
     rtx lshft = GEN_INT (32 - (INTVAL (operands[2]) + INTVAL (operands[3])));
     rtx rshft = GEN_INT (32 - INTVAL (operands[2]));
     rtx tmp1 = gen_reg_rtx (SImode);
     rtx tmp2 = gen_reg_rtx (SImode);

     emit_insn (gen_rtx_SET (SImode, tmp1, operands[1]));
     emit_insn (gen_rtx_SET (SImode, tmp2,
                         gen_rtx_ASHIFT (SImode, tmp1, lshft)));
     emit_insn (gen_rtx_SET (SImode, operands[0],
                       gen_rtx_LSHIFTRT (SImode, tmp2, rshft)));
     DONE;
   }
 else
   {
     /* Make the compiler figure out some alternative mechanism.  */
     FAIL;
   }

 /* Emit the RTL pattern; something will match it later.  */
}")

(define_expand "insv"
  [(set (zero_extract:SI (match_operand:SI 0 "mcore_arith_reg_operand" "")
			 (match_operand:SI 1 "const_int_operand" "")
			 (match_operand:SI 2 "const_int_operand" ""))
	(match_operand:SI 3 "general_operand" ""))
   (clobber (reg:CC 17))]
  ""
  "
{
  if (mcore_expand_insv (operands))
    {
      DONE;
    }
  else
    {
      FAIL;
    }
}")

;;
;; the xtrb[0123] instructions handily get at 8-bit fields on nice boundaries.
;; but then, they do force you through r1.
;;
;; the combiner will build such patterns for us, so we'll make them available
;; for its use.
;;
;; Note that we have both SIGNED and UNSIGNED versions of these...
;;

;;
;; These no longer worry about the clobbering of CC bit; not sure this is
;; good...
;;
;; the SIGNED versions of these
;;
(define_insn ""
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r,b")
	(sign_extract:SI (match_operand:SI 1 "mcore_arith_reg_operand" "0,r") (const_int 8) (const_int 24)))]
  ""
  "@
	asri	%0,24
	xtrb0	%0,%1\;sextb	%0"
  [(set_attr "type" "shift")])

(define_insn ""
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=b")
	(sign_extract:SI (match_operand:SI 1 "mcore_arith_reg_operand" "r") (const_int 8) (const_int 16)))]
  ""
  "xtrb1	%0,%1\;sextb	%0"
  [(set_attr "type" "shift")])

(define_insn ""
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=b")
	(sign_extract:SI (match_operand:SI 1 "mcore_arith_reg_operand" "r") (const_int 8) (const_int 8)))]
  ""
  "xtrb2	%0,%1\;sextb	%0"
  [(set_attr "type" "shift")])

(define_insn ""
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
	(sign_extract:SI (match_operand:SI 1 "mcore_arith_reg_operand" "0") (const_int 8) (const_int 0)))]
  ""
  "sextb	%0"
  [(set_attr "type" "shift")])

;; the UNSIGNED uses of xtrb[0123]
;;
(define_insn ""
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r,b")
	(zero_extract:SI (match_operand:SI 1 "mcore_arith_reg_operand" "0,r") (const_int 8) (const_int 24)))]
  ""
  "@
	lsri	%0,24
	xtrb0	%0,%1"
  [(set_attr "type" "shift")])

(define_insn ""
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=b")
	(zero_extract:SI (match_operand:SI 1 "mcore_arith_reg_operand" "r") (const_int 8) (const_int 16)))]
  ""
  "xtrb1	%0,%1"
  [(set_attr "type" "shift")])

(define_insn ""
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=b")
	(zero_extract:SI (match_operand:SI 1 "mcore_arith_reg_operand" "r") (const_int 8) (const_int 8)))]
  ""
  "xtrb2	%0,%1"
  [(set_attr "type" "shift")])

;; This can be peepholed if it follows a ldb ...
(define_insn ""
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r,b")
	(zero_extract:SI (match_operand:SI 1 "mcore_arith_reg_operand" "0,r") (const_int 8) (const_int 0)))]
  ""
  "@
	zextb	%0
	xtrb3	%0,%1\;zextb	%0"
  [(set_attr "type" "shift")])


;; ------------------------------------------------------------------------
;; Block move - adapted from m88k.md
;; ------------------------------------------------------------------------

(define_expand "movmemsi"
  [(parallel [(set (mem:BLK (match_operand:BLK 0 "" ""))
		   (mem:BLK (match_operand:BLK 1 "" "")))
	      (use (match_operand:SI 2 "general_operand" ""))
	      (use (match_operand:SI 3 "immediate_operand" ""))])]
  ""
  "
{
  if (mcore_expand_block_move (operands))
    DONE;
  else
    FAIL;
}")

;; ;;; ??? These patterns are meant to be generated from expand_block_move,
;; ;;; but they currently are not.
;; 
;; (define_insn ""
;;   [(set (match_operand:QI 0 "mcore_arith_reg_operand" "=r")
;; 	(match_operand:BLK 1 "mcore_general_movsrc_operand" "m"))]
;;   ""
;;   "ld.b	%0,%1"
;;   [(set_attr "type" "load")])
;; 
;; (define_insn ""
;;   [(set (match_operand:HI 0 "mcore_arith_reg_operand" "=r")
;; 	(match_operand:BLK 1 "mcore_general_movsrc_operand" "m"))]
;;   ""
;;   "ld.h	%0,%1"
;;   [(set_attr "type" "load")])
;; 
;; (define_insn ""
;;   [(set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
;; 	(match_operand:BLK 1 "mcore_general_movsrc_operand" "m"))]
;;   ""
;;   "ld.w	%0,%1"
;;   [(set_attr "type" "load")])
;; 
;; (define_insn ""
;;   [(set (match_operand:BLK 0 "mcore_general_movdst_operand" "=m")
;; 	(match_operand:QI 1 "mcore_arith_reg_operand" "r"))]
;;   ""
;;   "st.b	%1,%0"
;;   [(set_attr "type" "store")])
;; 
;; (define_insn ""
;;   [(set (match_operand:BLK 0 "mcore_general_movdst_operand" "=m")
;; 	(match_operand:HI 1 "mcore_arith_reg_operand" "r"))]
;;   ""
;;   "st.h	%1,%0"
;;   [(set_attr "type" "store")])
;; 
;; (define_insn ""
;;   [(set (match_operand:BLK 0 "mcore_general_movdst_operand" "=m")
;; 	(match_operand:SI 1 "mcore_arith_reg_operand" "r"))]
;;   ""
;;   "st.w	%1,%0"
;;   [(set_attr "type" "store")])

;; ------------------------------------------------------------------------
;; Misc Optimizing quirks
;; ------------------------------------------------------------------------

;; pair to catch constructs like:  (int *)((p+=4)-4) which happen
;; in stdarg/varargs traversal. This changes a 3 insn sequence to a 2
;; insn sequence. -- RBE 11/30/95
(define_insn ""
  [(parallel[
      (set (match_operand:SI 0 "mcore_arith_reg_operand" "=r")
	   (match_operand:SI 1 "mcore_arith_reg_operand" "+r"))
      (set (match_dup 1) (plus:SI (match_dup 1) (match_operand 2 "mcore_arith_any_imm_operand" "")))])]
  "GET_CODE(operands[2]) == CONST_INT"
  "#"
  [(set_attr "length" "4")])

(define_split 
  [(parallel[
      (set (match_operand:SI 0 "mcore_arith_reg_operand" "")
	   (match_operand:SI 1 "mcore_arith_reg_operand" ""))
      (set (match_dup 1) (plus:SI (match_dup 1) (match_operand 2 "mcore_arith_any_imm_operand" "")))])]
  "GET_CODE(operands[2]) == CONST_INT &&
   operands[0] != operands[1]"
  [(set (match_dup 0) (match_dup 1))
   (set (match_dup 1) (plus:SI (match_dup 1) (match_dup 2)))])


;;; Peepholes

; note: in the following patterns, use mcore_is_dead() to ensure that the
; reg we may be trashing really is dead.  reload doesn't always mark
; deaths, so mcore_is_dead() (see mcore.c) scans forward to find its death.  BRC

;;; A peephole to convert the 3 instruction sequence generated by reload
;;; to load a FP-offset address into a 2 instruction sequence.
;;; ??? This probably never matches anymore.
(define_peephole
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "r")
	(match_operand:SI 1 "const_int_operand" "J"))
   (set (match_dup 0) (neg:SI (match_dup 0)))
   (set (match_dup 0)
	(plus:SI (match_dup 0)
		 (match_operand:SI 2 "mcore_arith_reg_operand" "r")))]
  "CONST_OK_FOR_J (INTVAL (operands[1]))"
  "error\;mov	%0,%2\;subi	%0,%1")

;; Moves of inlinable constants are done late, so when a 'not' is generated
;; it is never combined with the following 'and' to generate an 'andn' b/c 
;; the combiner never sees it.  use a peephole to pick up this case (happens
;; mostly with bitfields)  BRC

(define_peephole
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "r")
        (match_operand:SI 1 "const_int_operand" "i"))
   (set (match_operand:SI 2 "mcore_arith_reg_operand" "r")
        (and:SI (match_dup 2) (match_dup 0)))]
  "mcore_const_trick_uses_not (INTVAL (operands[1])) &&
 	operands[0] != operands[2] &&
        mcore_is_dead (insn, operands[0])"
  "* return mcore_output_andn (insn, operands);")

; when setting or clearing just two bits, it's cheapest to use two bseti's 
; or bclri's.  only happens when relaxing immediates.  BRC

(define_peephole
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "")
        (match_operand:SI 1 "const_int_operand" ""))
   (set (match_operand:SI 2 "mcore_arith_reg_operand" "")
        (ior:SI (match_dup 2) (match_dup 0)))]
  "TARGET_HARDLIT && mcore_num_ones (INTVAL (operands[1])) == 2 &&
       mcore_is_dead (insn, operands[0])"
  "* return mcore_output_bseti (operands[2], INTVAL (operands[1]));")

(define_peephole
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "")
        (match_operand:SI 1 "const_int_operand" ""))
   (set (match_operand:SI 2 "mcore_arith_reg_operand" "")
        (and:SI (match_dup 2) (match_dup 0)))]
  "TARGET_HARDLIT && mcore_num_zeros (INTVAL (operands[1])) == 2 &&
       mcore_is_dead (insn, operands[0])"
  "* return mcore_output_bclri (operands[2], INTVAL (operands[1]));")

; change an and with a mask that has a single cleared bit into a bclri.  this
; handles QI and HI mode values using the knowledge that the most significant
; bits don't matter.

(define_peephole
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "")
        (match_operand:SI 1 "const_int_operand" ""))
   (set (match_operand:SI 2 "mcore_arith_reg_operand" "")
        (and:SI (match_operand:SI 3 "mcore_arith_reg_operand" "")
                (match_dup 0)))]
  "GET_CODE (operands[3]) == SUBREG && 
      GET_MODE (SUBREG_REG (operands[3])) == QImode &&
      mcore_num_zeros (INTVAL (operands[1]) | 0xffffff00) == 1 &&
      mcore_is_dead (insn, operands[0])"
"*
  if (! mcore_is_same_reg (operands[2], operands[3]))
    output_asm_insn (\"mov\\t%2,%3\", operands);
  return mcore_output_bclri (operands[2], INTVAL (operands[1]) | 0xffffff00);")

/* Do not fold these together -- mode is lost at final output phase.  */

(define_peephole
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "")
        (match_operand:SI 1 "const_int_operand" ""))
   (set (match_operand:SI 2 "mcore_arith_reg_operand" "")
        (and:SI (match_operand:SI 3 "mcore_arith_reg_operand" "")
                (match_dup 0)))]
  "GET_CODE (operands[3]) == SUBREG && 
      GET_MODE (SUBREG_REG (operands[3])) == HImode &&
      mcore_num_zeros (INTVAL (operands[1]) | 0xffff0000) == 1 &&
      operands[2] == operands[3] &&
      mcore_is_dead (insn, operands[0])"
"*
  if (! mcore_is_same_reg (operands[2], operands[3]))
    output_asm_insn (\"mov\\t%2,%3\", operands);
  return mcore_output_bclri (operands[2], INTVAL (operands[1]) | 0xffff0000);")

; This peephole helps when using -mwide-bitfields to widen fields so they 
; collapse.   This, however, has the effect that a narrower mode is not used
; when desirable.  
;
; e.g., sequences like:
;
;      ldw r8,(r6)
;      movi r7,0x00ffffff
;      and r8,r7                 r7 dead
;      stw r8,(r6)                r8 dead
;
; get peepholed to become:
;
;      movi r8,0
;      stb r8,(r6)              r8 dead
;
; Do only easy addresses that have no offset.  This peephole is also applied 
; to halfwords.  We need to check that the load is non-volatile before we get
; rid of it.

(define_peephole
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "")
        (match_operand:SI 1 "memory_operand" ""))
   (set (match_operand:SI 2 "mcore_arith_reg_operand" "")
        (match_operand:SI 3 "const_int_operand" ""))
   (set (match_dup 0) (and:SI (match_dup 0) (match_dup 2)))
   (set (match_operand:SI 4 "memory_operand" "") (match_dup 0))]
  "mcore_is_dead (insn, operands[0]) &&
   ! MEM_VOLATILE_P (operands[1]) &&
   mcore_is_dead (insn, operands[2]) && 
   (mcore_byte_offset (INTVAL (operands[3])) > -1 || 
    mcore_halfword_offset (INTVAL (operands[3])) > -1) &&
   ! MEM_VOLATILE_P (operands[4]) &&
   GET_CODE (XEXP (operands[4], 0)) == REG"
"*
{
   int ofs;
   enum machine_mode mode;
   rtx base_reg = XEXP (operands[4], 0);

   if ((ofs = mcore_byte_offset (INTVAL (operands[3]))) > -1)
      mode = QImode;
   else if ((ofs = mcore_halfword_offset (INTVAL (operands[3]))) > -1)
      mode = HImode;
   else
      gcc_unreachable ();

   if (ofs > 0) 
      operands[4] = gen_rtx_MEM (mode, 
                              gen_rtx_PLUS (SImode, base_reg, GEN_INT(ofs)));
   else
      operands[4] = gen_rtx_MEM (mode, base_reg);

   if (mode == QImode)
      return \"movi	%0,0\\n\\tst.b	%0,%4\";

   return \"movi	%0,0\\n\\tst.h	%0,%4\";
}")

; from sop11. get btsti's for (LT A 0) where A is a QI or HI value

(define_peephole
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "r")
        (sign_extend:SI (match_operand:QI 1 "mcore_arith_reg_operand" "0")))
   (set (reg:CC 17)
	(lt:CC (match_dup 0)
	    (const_int 0)))]
  "mcore_is_dead (insn, operands[0])"
  "btsti	%0,7")

(define_peephole
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "r")
        (sign_extend:SI (match_operand:HI 1 "mcore_arith_reg_operand" "0")))
   (set (reg:CC 17)
	(lt:CC (match_dup 0)
	    (const_int 0)))]
  "mcore_is_dead (insn, operands[0])"
  "btsti	%0,15")

; Pick up a tst.  This combination happens because the immediate is not
; allowed to fold into one of the operands of the tst.  Does not happen
; when relaxing immediates.  BRC

(define_peephole
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "")
        (match_operand:SI 1 "mcore_arith_reg_operand" ""))
   (set (match_dup 0)
        (and:SI (match_dup 0)
                (match_operand:SI 2 "mcore_literal_K_operand" "")))
   (set (reg:CC 17) (ne:CC (match_dup 0) (const_int 0)))]
  "mcore_is_dead (insn, operands[0])"
  "movi	%0,%2\;tst	%1,%0")

(define_peephole
  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "")
        (if_then_else:SI (ne (zero_extract:SI 
                                (match_operand:SI 1 "mcore_arith_reg_operand" "")
                                (const_int 1)
	                        (match_operand:SI 2 "mcore_literal_K_operand" ""))
			     (const_int 0))
	   (match_operand:SI 3 "mcore_arith_imm_operand" "")
           (match_operand:SI 4 "mcore_arith_imm_operand" "")))
    (set (reg:CC 17) (ne:CC (match_dup 0) (const_int 0)))]
  ""
"*
{
  unsigned int op0 = REGNO (operands[0]);

  if (GET_CODE (operands[3]) == REG)
    {
     if (REGNO (operands[3]) == op0 && GET_CODE (operands[4]) == CONST_INT
	 && INTVAL (operands[4]) == 0)
        return \"btsti	%1,%2\\n\\tclrf	%0\";
     else if (GET_CODE (operands[4]) == REG)
       {
        if (REGNO (operands[4]) == op0)
   	   return \"btsti	%1,%2\\n\\tmovf	%0,%3\";
        else if (REGNO (operands[3]) == op0)
 	   return \"btsti	%1,%2\\n\\tmovt	%0,%4\";
       }

     gcc_unreachable ();
    }
  else if (GET_CODE (operands[3]) == CONST_INT
           && INTVAL (operands[3]) == 0
	   && GET_CODE (operands[4]) == REG)
     return \"btsti	%1,%2\\n\\tclrt	%0\";

  gcc_unreachable ();
}")

; experimental - do the constant folding ourselves.  note that this isn't
;   re-applied like we'd really want.  i.e., four ands collapse into two
;   instead of one.  this is because peepholes are applied as a sliding
;   window.  the peephole does not generate new rtl's, but instead slides
;   across the rtl's generating machine instructions.  it would be nice
;   if the peephole optimizer is changed to re-apply patterns and to gen
;   new rtl's.  this is more flexible.  the pattern below helps when we're
;   not using relaxed immediates.   BRC

;(define_peephole
;  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "")
;        (match_operand:SI 1 "const_int_operand" ""))
;   (set (match_operand:SI 2 "mcore_arith_reg_operand" "")
;          (and:SI (match_dup 2) (match_dup 0)))
;   (set (match_dup 0)
;        (match_operand:SI 3 "const_int_operand" ""))
;   (set (match_dup 2)
;           (and:SI (match_dup 2) (match_dup 0)))]
;  "!TARGET_RELAX_IMM && mcore_is_dead (insn, operands[0]) &&
;       mcore_const_ok_for_inline (INTVAL (operands[1]) & INTVAL (operands[3]))"
;  "*
;{
;  rtx out_operands[2];
;  out_operands[0] = operands[0];
;  out_operands[1] = GEN_INT (INTVAL (operands[1]) & INTVAL (operands[3]));
;  
;  output_inline_const (SImode, out_operands);
;
;  output_asm_insn (\"and	%2,%0\", operands);
;
;  return \"\";   
;}")

; BRC: for inlining get rid of extra test - experimental
;(define_peephole
;  [(set (match_operand:SI 0 "mcore_arith_reg_operand" "r")
;          (ne:SI (reg:CC 17) (const_int 0)))
;   (set (reg:CC 17) (ne:CC (match_dup 0) (const_int 0)))
;   (set (pc) 
;       (if_then_else (eq (reg:CC 17) (const_int 0))
;         (label_ref (match_operand 1 "" ""))
;         (pc)))]
;   ""
;   "*
;{
;  if (get_attr_length (insn) == 10)
;    {
;      output_asm_insn (\"bt	2f\\n\\tjmpi	[1f]\", operands);
;      output_asm_insn (\".align	2\\n1:\", operands);
;      output_asm_insn (\".long	%1\\n2:\", operands);
;      return \"\";
;    }
;  return \"bf	%l1\";
;}")


;;; Special patterns for dealing with the constant pool.

;;; 4 byte integer in line.

(define_insn "consttable_4"
 [(unspec_volatile [(match_operand:SI 0 "general_operand" "=g")] 0)]
 ""
 "*
{
  assemble_integer (operands[0], 4, BITS_PER_WORD, 1);
  return \"\";
}"
 [(set_attr "length" "4")])

;;; align to a four byte boundary.

(define_insn "align_4"
 [(unspec_volatile [(const_int 0)] 1)]
 ""
 ".align 2")

;;; Handle extra constant pool entries created during final pass.

(define_insn "consttable_end"
  [(unspec_volatile [(const_int 0)] 2)]
  ""
  "* return mcore_output_jump_label_table ();")

;;
;; Stack allocation -- in particular, for alloca().
;; this is *not* what we use for entry into functions.
;;
;; This is how we allocate stack space.  If we are allocating a
;; constant amount of space and we know it is less than 4096
;; bytes, we need do nothing.
;;
;; If it is more than 4096 bytes, we need to probe the stack
;; periodically. 
;;
;; operands[1], the distance is a POSITIVE number indicating that we
;; are allocating stack space
;;
(define_expand "allocate_stack"
  [(set (reg:SI 0)
	(plus:SI (reg:SI 0)
		 (match_operand:SI 1 "general_operand" "")))
   (set (match_operand:SI 0 "register_operand" "=r")
        (match_dup 2))]
  ""
  "
{
  /* If he wants no probing, just do it for him.  */
  if (mcore_stack_increment == 0)
    {
      emit_insn (gen_addsi3 (stack_pointer_rtx, stack_pointer_rtx,operands[1]));
;;      emit_move_insn (operands[0], virtual_stack_dynamic_rtx);
      DONE;
    }

  /* For small constant growth, we unroll the code.  */
  if (GET_CODE (operands[1]) == CONST_INT
      && INTVAL (operands[1]) < 8 * STACK_UNITS_MAXSTEP)
    {
      int left = INTVAL(operands[1]);

      /* If it's a long way, get close enough for a last shot.  */
      if (left >= STACK_UNITS_MAXSTEP)
	{
	  rtx tmp = gen_reg_rtx (Pmode);
	  emit_insn (gen_movsi (tmp, GEN_INT (STACK_UNITS_MAXSTEP)));
	  do
	    {
	      rtx memref = gen_rtx_MEM (SImode, stack_pointer_rtx);

              MEM_VOLATILE_P (memref) = 1;
	      emit_insn (gen_subsi3 (stack_pointer_rtx, stack_pointer_rtx, tmp));
	      emit_insn (gen_movsi (memref, stack_pointer_rtx));
	      left -= STACK_UNITS_MAXSTEP;
	    }
	  while (left > STACK_UNITS_MAXSTEP);
	}
      /* Perform the final adjustment.  */
      emit_insn (gen_addsi3 (stack_pointer_rtx,stack_pointer_rtx,GEN_INT(-left)));
;;      emit_move_insn (operands[0], virtual_stack_dynamic_rtx);
      DONE;
    }
  else
    {
      rtx out_label = 0;
      rtx loop_label = gen_label_rtx ();
      rtx step = gen_reg_rtx (Pmode);
      rtx tmp = gen_reg_rtx (Pmode);
      rtx memref;

#if 1
      emit_insn (gen_movsi (tmp, operands[1]));
      emit_insn (gen_movsi (step, GEN_INT(STACK_UNITS_MAXSTEP)));

      if (GET_CODE (operands[1]) != CONST_INT)
	{
	  out_label = gen_label_rtx ();
	  emit_insn (gen_cmpsi (step, tmp));		/* quick out */
	  emit_jump_insn (gen_bgeu (out_label));
	}

      /* Run a loop that steps it incrementally.  */
      emit_label (loop_label);

      /* Extend a step, probe, and adjust remaining count.  */
      emit_insn(gen_subsi3(stack_pointer_rtx, stack_pointer_rtx, step));
      memref = gen_rtx_MEM (SImode, stack_pointer_rtx);
      MEM_VOLATILE_P (memref) = 1;
      emit_insn(gen_movsi(memref, stack_pointer_rtx));
      emit_insn(gen_subsi3(tmp, tmp, step));

      /* Loop condition -- going back up.  */
      emit_insn (gen_cmpsi (step, tmp));
      emit_jump_insn (gen_bltu (loop_label));

      if (out_label)
	emit_label (out_label);

      /* Bump the residual.  */
      emit_insn (gen_subsi3 (stack_pointer_rtx, stack_pointer_rtx, tmp));
;;      emit_move_insn (operands[0], virtual_stack_dynamic_rtx);
      DONE;
#else
      /* simple one-shot -- ensure register and do a subtract.
       * This does NOT comply with the ABI.  */
      emit_insn (gen_movsi (tmp, operands[1]));
      emit_insn (gen_subsi3 (stack_pointer_rtx, stack_pointer_rtx, tmp));
;;      emit_move_insn (operands[0], virtual_stack_dynamic_rtx);
      DONE;
#endif
    }
}")
