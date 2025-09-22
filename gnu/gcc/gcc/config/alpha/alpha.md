;; Machine description for DEC Alpha for GNU C compiler
;; Copyright (C) 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999,
;; 2000, 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
;; Contributed by Richard Kenner (kenner@vlsi1.ultra.nyu.edu)
;;
;; This file is part of GCC.
;;
;; GCC is free software; you can redistribute it and/or modify
;; it under the terms of the GNU General Public License as published by
;; the Free Software Foundation; either version 2, or (at your option)
;; any later version.
;;
;; GCC is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU General Public License for more details.
;;
;; You should have received a copy of the GNU General Public License
;; along with GCC; see the file COPYING.  If not, write to
;; the Free Software Foundation, 51 Franklin Street, Fifth Floor,
;; Boston, MA 02110-1301, USA.

;;- See file "rtl.def" for documentation on define_insn, match_*, et. al.

;; Uses of UNSPEC in this file:

(define_constants
  [(UNSPEC_ARG_HOME	0)
   (UNSPEC_INSXH	2)
   (UNSPEC_MSKXH	3)
   (UNSPEC_CVTQL	4)
   (UNSPEC_CVTLQ	5)
   (UNSPEC_UMK_LAUM	6)
   (UNSPEC_UMK_LALM	7)
   (UNSPEC_UMK_LAL	8)
   (UNSPEC_UMK_LOAD_CIW	9)
   (UNSPEC_LDGP2	10)
   (UNSPEC_LITERAL	11)
   (UNSPEC_LITUSE	12)
   (UNSPEC_SIBCALL	13)
   (UNSPEC_SYMBOL	14)

   ;; TLS Support
   (UNSPEC_TLSGD_CALL	15)
   (UNSPEC_TLSLDM_CALL	16)
   (UNSPEC_TLSGD	17)
   (UNSPEC_TLSLDM	18)
   (UNSPEC_DTPREL	19)
   (UNSPEC_TPREL	20)
   (UNSPEC_TP		21)

   ;; Builtins
   (UNSPEC_CMPBGE	22)
   (UNSPEC_ZAP		23)
   (UNSPEC_AMASK	24)
   (UNSPEC_IMPLVER	25)
   (UNSPEC_PERR		26)
   (UNSPEC_COPYSIGN     27)

   ;; Atomic operations
   (UNSPEC_MB		28)
   (UNSPEC_ATOMIC	31)
   (UNSPEC_CMPXCHG	32)
   (UNSPEC_XCHG		33)
  ])

;; UNSPEC_VOLATILE:

(define_constants
  [(UNSPECV_IMB		0)
   (UNSPECV_BLOCKAGE	1)
   (UNSPECV_SETJMPR	2)	; builtin_setjmp_receiver
   (UNSPECV_LONGJMP	3)	; builtin_longjmp
   (UNSPECV_TRAPB	4)
   (UNSPECV_PSPL	5)	; prologue_stack_probe_loop
   (UNSPECV_REALIGN	6)
   (UNSPECV_EHR		7)	; exception_receiver
   (UNSPECV_MCOUNT	8)
   (UNSPECV_FORCE_MOV	9)
   (UNSPECV_LDGP1	10)
   (UNSPECV_PLDGP2	11)	; prologue ldgp
   (UNSPECV_SET_TP	12)
   (UNSPECV_RPCC	13)
   (UNSPECV_SETJMPR_ER	14)	; builtin_setjmp_receiver fragment
   (UNSPECV_LL		15)	; load-locked
   (UNSPECV_SC		16)	; store-conditional
  ])

;; Where necessary, the suffixes _le and _be are used to distinguish between
;; little-endian and big-endian patterns.
;;
;; Note that the Unicos/Mk assembler does not support the following
;; opcodes: mov, fmov, nop, fnop, unop.

;; Processor type -- this attribute must exactly match the processor_type
;; enumeration in alpha.h.

(define_attr "tune" "ev4,ev5,ev6"
  (const (symbol_ref "alpha_tune")))

;; Define an insn type attribute.  This is used in function unit delay
;; computations, among other purposes.  For the most part, we use the names
;; defined in the EV4 documentation, but add a few that we have to know about
;; separately.

(define_attr "type"
  "ild,fld,ldsym,ist,fst,ibr,callpal,fbr,jsr,iadd,ilog,shift,icmov,fcmov,
   icmp,imul,fadd,fmul,fcpys,fdiv,fsqrt,misc,mvi,ftoi,itof,mb,ld_l,st_c,
   multi,none"
  (const_string "iadd"))

;; Describe a user's asm statement.
(define_asm_attributes
  [(set_attr "type" "multi")])

;; Define the operand size an insn operates on.  Used primarily by mul
;; and div operations that have size dependent timings.

(define_attr "opsize" "si,di,udi"
  (const_string "di"))

;; The TRAP attribute marks instructions that may generate traps
;; (which are imprecise and may need a trapb if software completion
;; is desired).

(define_attr "trap" "no,yes"
  (const_string "no"))

;; The ROUND_SUFFIX attribute marks which instructions require a
;; rounding-mode suffix.  The value NONE indicates no suffix,
;; the value NORMAL indicates a suffix controlled by alpha_fprm.

(define_attr "round_suffix" "none,normal,c"
  (const_string "none"))

;; The TRAP_SUFFIX attribute marks instructions requiring a trap-mode suffix:
;;   NONE	no suffix
;;   SU		accepts only /su (cmpt et al)
;;   SUI	accepts only /sui (cvtqt and cvtqs)
;;   V_SV	accepts /v and /sv (cvtql only)
;;   V_SV_SVI	accepts /v, /sv and /svi (cvttq only)
;;   U_SU_SUI	accepts /u, /su and /sui (most fp instructions)
;;
;; The actual suffix emitted is controlled by alpha_fptm.

(define_attr "trap_suffix" "none,su,sui,v_sv,v_sv_svi,u_su_sui"
  (const_string "none"))

;; The length of an instruction sequence in bytes.

(define_attr "length" ""
  (const_int 4))

;; The USEGP attribute marks instructions that have relocations that use
;; the GP.

(define_attr "usegp" "no,yes"
  (cond [(eq_attr "type" "ldsym,jsr")
	   (const_string "yes")
	 (eq_attr "type" "ild,fld,ist,fst")
	   (symbol_ref "alpha_find_lo_sum_using_gp(insn)")
	]
	(const_string "no")))

;; The CANNOT_COPY attribute marks instructions with relocations that
;; cannot easily be duplicated.  This includes insns with gpdisp relocs
;; since they have to stay in 1-1 correspondence with one another.  This
;; also includes jsr insns, since they must stay in correspondence with
;; the immediately following gpdisp instructions.

(define_attr "cannot_copy" "false,true"
  (const_string "false"))

;; Include scheduling descriptions.
  
(include "ev4.md")
(include "ev5.md")
(include "ev6.md")


;; Include predicate definitions

(include "predicates.md")


;; First define the arithmetic insns.  Note that the 32-bit forms also
;; sign-extend.

;; Handle 32-64 bit extension from memory to a floating point register
;; specially, since this occurs frequently in int->double conversions.
;;
;; Note that while we must retain the =f case in the insn for reload's
;; benefit, it should be eliminated after reload, so we should never emit
;; code for that case.  But we don't reject the possibility.

(define_expand "extendsidi2"
  [(set (match_operand:DI 0 "register_operand" "")
	(sign_extend:DI (match_operand:SI 1 "nonimmediate_operand" "")))]
  ""
  "")

(define_insn "*cvtlq"
  [(set (match_operand:DI 0 "register_operand" "=f")
	(unspec:DI [(match_operand:SF 1 "reg_or_0_operand" "fG")]
		   UNSPEC_CVTLQ))]
  ""
  "cvtlq %1,%0"
  [(set_attr "type" "fadd")])

(define_insn "*extendsidi2_1"
  [(set (match_operand:DI 0 "register_operand" "=r,r,!*f")
	(sign_extend:DI
	  (match_operand:SI 1 "nonimmediate_operand" "r,m,m")))]
  ""
  "@
   addl $31,%1,%0
   ldl %0,%1
   lds %0,%1\;cvtlq %0,%0"
  [(set_attr "type" "iadd,ild,fld")
   (set_attr "length" "*,*,8")])

(define_split
  [(set (match_operand:DI 0 "hard_fp_register_operand" "")
	(sign_extend:DI (match_operand:SI 1 "memory_operand" "")))]
  "reload_completed"
  [(set (match_dup 2) (match_dup 1))
   (set (match_dup 0) (unspec:DI [(match_dup 2)] UNSPEC_CVTLQ))]
{
  operands[1] = adjust_address (operands[1], SFmode, 0);
  operands[2] = gen_rtx_REG (SFmode, REGNO (operands[0]));
})

;; Optimize sign-extension of SImode loads.  This shows up in the wake of
;; reload when converting fp->int.

(define_peephole2
  [(set (match_operand:SI 0 "hard_int_register_operand" "")
        (match_operand:SI 1 "memory_operand" ""))
   (set (match_operand:DI 2 "hard_int_register_operand" "")
        (sign_extend:DI (match_dup 0)))]
  "true_regnum (operands[0]) == true_regnum (operands[2])
   || peep2_reg_dead_p (2, operands[0])"
  [(set (match_dup 2)
	(sign_extend:DI (match_dup 1)))]
  "")

;; Don't say we have addsi3 if optimizing.  This generates better code.  We
;; have the anonymous addsi3 pattern below in case combine wants to make it.
(define_expand "addsi3"
  [(set (match_operand:SI 0 "register_operand" "")
	(plus:SI (match_operand:SI 1 "reg_or_0_operand" "")
		 (match_operand:SI 2 "add_operand" "")))]
  "! optimize"
  "")

(define_insn "*addsi_internal"
  [(set (match_operand:SI 0 "register_operand" "=r,r,r,r")
	(plus:SI (match_operand:SI 1 "reg_or_0_operand" "%rJ,rJ,rJ,rJ")
		 (match_operand:SI 2 "add_operand" "rI,O,K,L")))]
  ""
  "@
   addl %r1,%2,%0
   subl %r1,%n2,%0
   lda %0,%2(%r1)
   ldah %0,%h2(%r1)")

(define_split
  [(set (match_operand:SI 0 "register_operand" "")
	(plus:SI (match_operand:SI 1 "register_operand" "")
		 (match_operand:SI 2 "const_int_operand" "")))]
  "! add_operand (operands[2], SImode)"
  [(set (match_dup 0) (plus:SI (match_dup 1) (match_dup 3)))
   (set (match_dup 0) (plus:SI (match_dup 0) (match_dup 4)))]
{
  HOST_WIDE_INT val = INTVAL (operands[2]);
  HOST_WIDE_INT low = (val & 0xffff) - 2 * (val & 0x8000);
  HOST_WIDE_INT rest = val - low;

  operands[3] = GEN_INT (rest);
  operands[4] = GEN_INT (low);
})

(define_insn "*addsi_se"
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(sign_extend:DI
	 (plus:SI (match_operand:SI 1 "reg_or_0_operand" "%rJ,rJ")
		  (match_operand:SI 2 "sext_add_operand" "rI,O"))))]
  ""
  "@
   addl %r1,%2,%0
   subl %r1,%n2,%0")

(define_insn "*addsi_se2"
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(sign_extend:DI
	 (subreg:SI (plus:DI (match_operand:DI 1 "reg_or_0_operand" "%rJ,rJ")
			     (match_operand:DI 2 "sext_add_operand" "rI,O"))
		    0)))]
  ""
  "@
   addl %r1,%2,%0
   subl %r1,%n2,%0")

(define_split
  [(set (match_operand:DI 0 "register_operand" "")
	(sign_extend:DI
	 (plus:SI (match_operand:SI 1 "reg_not_elim_operand" "")
		  (match_operand:SI 2 "const_int_operand" ""))))
   (clobber (match_operand:SI 3 "reg_not_elim_operand" ""))]
  "! sext_add_operand (operands[2], SImode) && INTVAL (operands[2]) > 0
   && INTVAL (operands[2]) % 4 == 0"
  [(set (match_dup 3) (match_dup 4))
   (set (match_dup 0) (sign_extend:DI (plus:SI (mult:SI (match_dup 3)
							(match_dup 5))
					       (match_dup 1))))]
{
  HOST_WIDE_INT val = INTVAL (operands[2]) / 4;
  int mult = 4;

  if (val % 2 == 0)
    val /= 2, mult = 8;

  operands[4] = GEN_INT (val);
  operands[5] = GEN_INT (mult);
})

(define_split
  [(set (match_operand:DI 0 "register_operand" "")
	(sign_extend:DI
	 (plus:SI (match_operator:SI 1 "comparison_operator"
				     [(match_operand 2 "" "")
				      (match_operand 3 "" "")])
		  (match_operand:SI 4 "add_operand" ""))))
   (clobber (match_operand:DI 5 "register_operand" ""))]
  ""
  [(set (match_dup 5) (match_dup 6))
   (set (match_dup 0) (sign_extend:DI (plus:SI (match_dup 7) (match_dup 4))))]
{
  operands[6] = gen_rtx_fmt_ee (GET_CODE (operands[1]), DImode,
				operands[2], operands[3]);
  operands[7] = gen_lowpart (SImode, operands[5]);
})

(define_insn "addvsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(plus:SI (match_operand:SI 1 "reg_or_0_operand" "%rJ,rJ")
		 (match_operand:SI 2 "sext_add_operand" "rI,O")))
   (trap_if (ne (plus:DI (sign_extend:DI (match_dup 1))
			 (sign_extend:DI (match_dup 2)))
		(sign_extend:DI (plus:SI (match_dup 1)
					 (match_dup 2))))
	    (const_int 0))]
  ""
  "@
   addlv %r1,%2,%0
   sublv %r1,%n2,%0")

(define_expand "adddi3"
  [(set (match_operand:DI 0 "register_operand" "")
	(plus:DI (match_operand:DI 1 "register_operand" "")
		 (match_operand:DI 2 "add_operand" "")))]
  ""
  "")

(define_insn "*adddi_er_lo16_dtp"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(lo_sum:DI (match_operand:DI 1 "register_operand" "r")
		   (match_operand:DI 2 "dtp16_symbolic_operand" "")))]
  "HAVE_AS_TLS"
  "lda %0,%2(%1)\t\t!dtprel")

(define_insn "*adddi_er_hi32_dtp"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(plus:DI (match_operand:DI 1 "register_operand" "r")
		 (high:DI (match_operand:DI 2 "dtp32_symbolic_operand" ""))))]
  "HAVE_AS_TLS"
  "ldah %0,%2(%1)\t\t!dtprelhi")

(define_insn "*adddi_er_lo32_dtp"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(lo_sum:DI (match_operand:DI 1 "register_operand" "r")
		   (match_operand:DI 2 "dtp32_symbolic_operand" "")))]
  "HAVE_AS_TLS"
  "lda %0,%2(%1)\t\t!dtprello")

(define_insn "*adddi_er_lo16_tp"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(lo_sum:DI (match_operand:DI 1 "register_operand" "r")
		   (match_operand:DI 2 "tp16_symbolic_operand" "")))]
  "HAVE_AS_TLS"
  "lda %0,%2(%1)\t\t!tprel")

(define_insn "*adddi_er_hi32_tp"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(plus:DI (match_operand:DI 1 "register_operand" "r")
		 (high:DI (match_operand:DI 2 "tp32_symbolic_operand" ""))))]
  "HAVE_AS_TLS"
  "ldah %0,%2(%1)\t\t!tprelhi")

(define_insn "*adddi_er_lo32_tp"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(lo_sum:DI (match_operand:DI 1 "register_operand" "r")
		   (match_operand:DI 2 "tp32_symbolic_operand" "")))]
  "HAVE_AS_TLS"
  "lda %0,%2(%1)\t\t!tprello")

(define_insn "*adddi_er_high_l"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(plus:DI (match_operand:DI 1 "register_operand" "r")
		 (high:DI (match_operand:DI 2 "local_symbolic_operand" ""))))]
  "TARGET_EXPLICIT_RELOCS && reload_completed"
  "ldah %0,%2(%1)\t\t!gprelhigh"
  [(set_attr "usegp" "yes")])

(define_split
  [(set (match_operand:DI 0 "register_operand" "")
        (high:DI (match_operand:DI 1 "local_symbolic_operand" "")))]
  "TARGET_EXPLICIT_RELOCS && reload_completed"
  [(set (match_dup 0)
	(plus:DI (match_dup 2) (high:DI (match_dup 1))))]
  "operands[2] = pic_offset_table_rtx;")

;; We used to expend quite a lot of effort choosing addq/subq/lda.
;; With complications like
;;
;;   The NT stack unwind code can't handle a subq to adjust the stack
;;   (that's a bug, but not one we can do anything about).  As of NT4.0 SP3,
;;   the exception handling code will loop if a subq is used and an
;;   exception occurs.
;;
;;   The 19980616 change to emit prologues as RTL also confused some
;;   versions of GDB, which also interprets prologues.  This has been
;;   fixed as of GDB 4.18, but it does not harm to unconditionally
;;   use lda here.
;;
;; and the fact that the three insns schedule exactly the same, it's
;; just not worth the effort.

(define_insn "*adddi_internal"
  [(set (match_operand:DI 0 "register_operand" "=r,r,r")
	(plus:DI (match_operand:DI 1 "register_operand" "%r,r,r")
		 (match_operand:DI 2 "add_operand" "r,K,L")))]
  ""
  "@
   addq %1,%2,%0
   lda %0,%2(%1)
   ldah %0,%h2(%1)")

;; ??? Allow large constants when basing off the frame pointer or some
;; virtual register that may eliminate to the frame pointer.  This is
;; done because register elimination offsets will change the hi/lo split,
;; and if we split before reload, we will require additional instructions.

(define_insn "*adddi_fp_hack"
  [(set (match_operand:DI 0 "register_operand" "=r,r,r")
        (plus:DI (match_operand:DI 1 "reg_no_subreg_operand" "r,r,r")
		 (match_operand:DI 2 "const_int_operand" "K,L,n")))]
  "NONSTRICT_REG_OK_FP_BASE_P (operands[1])
   && INTVAL (operands[2]) >= 0
   /* This is the largest constant an lda+ldah pair can add, minus
      an upper bound on the displacement between SP and AP during
      register elimination.  See INITIAL_ELIMINATION_OFFSET.  */
   && INTVAL (operands[2])
	< (0x7fff8000
	   - FIRST_PSEUDO_REGISTER * UNITS_PER_WORD
	   - ALPHA_ROUND(current_function_outgoing_args_size)
	   - (ALPHA_ROUND (get_frame_size ()
			   + max_reg_num () * UNITS_PER_WORD
			   + current_function_pretend_args_size)
	      - current_function_pretend_args_size))"
  "@
   lda %0,%2(%1)
   ldah %0,%h2(%1)
   #")

;; Don't do this if we are adjusting SP since we don't want to do it
;; in two steps.  Don't split FP sources for the reason listed above.
(define_split
  [(set (match_operand:DI 0 "register_operand" "")
	(plus:DI (match_operand:DI 1 "register_operand" "")
		 (match_operand:DI 2 "const_int_operand" "")))]
  "! add_operand (operands[2], DImode)
   && operands[0] != stack_pointer_rtx
   && operands[1] != frame_pointer_rtx
   && operands[1] != arg_pointer_rtx"
  [(set (match_dup 0) (plus:DI (match_dup 1) (match_dup 3)))
   (set (match_dup 0) (plus:DI (match_dup 0) (match_dup 4)))]
{
  HOST_WIDE_INT val = INTVAL (operands[2]);
  HOST_WIDE_INT low = (val & 0xffff) - 2 * (val & 0x8000);
  HOST_WIDE_INT rest = val - low;

  operands[4] = GEN_INT (low);
  if (CONST_OK_FOR_LETTER_P (rest, 'L'))
    operands[3] = GEN_INT (rest);
  else if (! no_new_pseudos)
    {
      operands[3] = gen_reg_rtx (DImode);
      emit_move_insn (operands[3], operands[2]);
      emit_insn (gen_adddi3 (operands[0], operands[1], operands[3]));
      DONE;
    }
  else
    FAIL;
})

(define_insn "*saddl"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(plus:SI (mult:SI (match_operand:SI 1 "reg_not_elim_operand" "r,r")
			  (match_operand:SI 2 "const48_operand" "I,I"))
		 (match_operand:SI 3 "sext_add_operand" "rI,O")))]
  ""
  "@
   s%2addl %1,%3,%0
   s%2subl %1,%n3,%0")

(define_insn "*saddl_se"
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(sign_extend:DI
	 (plus:SI (mult:SI (match_operand:SI 1 "reg_not_elim_operand" "r,r")
			   (match_operand:SI 2 "const48_operand" "I,I"))
		  (match_operand:SI 3 "sext_add_operand" "rI,O"))))]
  ""
  "@
   s%2addl %1,%3,%0
   s%2subl %1,%n3,%0")

(define_split
  [(set (match_operand:DI 0 "register_operand" "")
	(sign_extend:DI
	 (plus:SI (mult:SI (match_operator:SI 1 "comparison_operator"
					      [(match_operand 2 "" "")
					       (match_operand 3 "" "")])
			   (match_operand:SI 4 "const48_operand" ""))
		  (match_operand:SI 5 "sext_add_operand" ""))))
   (clobber (match_operand:DI 6 "reg_not_elim_operand" ""))]
  ""
  [(set (match_dup 6) (match_dup 7))
   (set (match_dup 0)
	(sign_extend:DI (plus:SI (mult:SI (match_dup 8) (match_dup 4))
				 (match_dup 5))))]
{
  operands[7] = gen_rtx_fmt_ee (GET_CODE (operands[1]), DImode,
				operands[2], operands[3]);
  operands[8] = gen_lowpart (SImode, operands[6]);
})

(define_insn "*saddq"
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(plus:DI (mult:DI (match_operand:DI 1 "reg_not_elim_operand" "r,r")
			  (match_operand:DI 2 "const48_operand" "I,I"))
		 (match_operand:DI 3 "sext_add_operand" "rI,O")))]
  ""
  "@
   s%2addq %1,%3,%0
   s%2subq %1,%n3,%0")

(define_insn "addvdi3"
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(plus:DI (match_operand:DI 1 "reg_or_0_operand" "%rJ,rJ")
		 (match_operand:DI 2 "sext_add_operand" "rI,O")))
   (trap_if (ne (plus:TI (sign_extend:TI (match_dup 1))
			 (sign_extend:TI (match_dup 2)))
		(sign_extend:TI (plus:DI (match_dup 1)
					 (match_dup 2))))
	    (const_int 0))]
  ""
  "@
   addqv %r1,%2,%0
   subqv %r1,%n2,%0")

(define_insn "negsi2"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(neg:SI (match_operand:SI 1 "reg_or_8bit_operand" "rI")))]
  ""
  "subl $31,%1,%0")

(define_insn "*negsi_se"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(sign_extend:DI (neg:SI
			 (match_operand:SI 1 "reg_or_8bit_operand" "rI"))))]
  ""
  "subl $31,%1,%0")

(define_insn "negvsi2"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(neg:SI (match_operand:SI 1 "register_operand" "r")))
   (trap_if (ne (neg:DI (sign_extend:DI (match_dup 1)))
		(sign_extend:DI (neg:SI (match_dup 1))))
	    (const_int 0))]
  ""
  "sublv $31,%1,%0")

(define_insn "negdi2"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(neg:DI (match_operand:DI 1 "reg_or_8bit_operand" "rI")))]
  ""
  "subq $31,%1,%0")

(define_insn "negvdi2"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(neg:DI (match_operand:DI 1 "register_operand" "r")))
   (trap_if (ne (neg:TI (sign_extend:TI (match_dup 1)))
		(sign_extend:TI (neg:DI (match_dup 1))))
	    (const_int 0))]
  ""
  "subqv $31,%1,%0")

(define_expand "subsi3"
  [(set (match_operand:SI 0 "register_operand" "")
	(minus:SI (match_operand:SI 1 "reg_or_0_operand" "")
		  (match_operand:SI 2 "reg_or_8bit_operand" "")))]
  "! optimize"
  "")

(define_insn "*subsi_internal"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(minus:SI (match_operand:SI 1 "reg_or_0_operand" "rJ")
		  (match_operand:SI 2 "reg_or_8bit_operand" "rI")))]
  ""
  "subl %r1,%2,%0")

(define_insn "*subsi_se"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(sign_extend:DI (minus:SI (match_operand:SI 1 "reg_or_0_operand" "rJ")
				  (match_operand:SI 2 "reg_or_8bit_operand" "rI"))))]
  ""
  "subl %r1,%2,%0")

(define_insn "*subsi_se2"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(sign_extend:DI
	 (subreg:SI (minus:DI (match_operand:DI 1 "reg_or_0_operand" "rJ")
			      (match_operand:DI 2 "reg_or_8bit_operand" "rI"))
		    0)))]
  ""
  "subl %r1,%2,%0")

(define_insn "subvsi3"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(minus:SI (match_operand:SI 1 "reg_or_0_operand" "rJ")
		  (match_operand:SI 2 "reg_or_8bit_operand" "rI")))
   (trap_if (ne (minus:DI (sign_extend:DI (match_dup 1))
			  (sign_extend:DI (match_dup 2)))
		(sign_extend:DI (minus:SI (match_dup 1)
					  (match_dup 2))))
	    (const_int 0))]
  ""
  "sublv %r1,%2,%0")

(define_insn "subdi3"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(minus:DI (match_operand:DI 1 "reg_or_0_operand" "rJ")
		  (match_operand:DI 2 "reg_or_8bit_operand" "rI")))]
  ""
  "subq %r1,%2,%0")

(define_insn "*ssubl"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(minus:SI (mult:SI (match_operand:SI 1 "reg_not_elim_operand" "r")
			   (match_operand:SI 2 "const48_operand" "I"))
		  (match_operand:SI 3 "reg_or_8bit_operand" "rI")))]
  ""
  "s%2subl %1,%3,%0")

(define_insn "*ssubl_se"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(sign_extend:DI
	 (minus:SI (mult:SI (match_operand:SI 1 "reg_not_elim_operand" "r")
			    (match_operand:SI 2 "const48_operand" "I"))
		   (match_operand:SI 3 "reg_or_8bit_operand" "rI"))))]
  ""
  "s%2subl %1,%3,%0")

(define_insn "*ssubq"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(minus:DI (mult:DI (match_operand:DI 1 "reg_not_elim_operand" "r")
			   (match_operand:DI 2 "const48_operand" "I"))
		  (match_operand:DI 3 "reg_or_8bit_operand" "rI")))]
  ""
  "s%2subq %1,%3,%0")

(define_insn "subvdi3"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(minus:DI (match_operand:DI 1 "reg_or_0_operand" "rJ")
		  (match_operand:DI 2 "reg_or_8bit_operand" "rI")))
   (trap_if (ne (minus:TI (sign_extend:TI (match_dup 1))
			  (sign_extend:TI (match_dup 2)))
		(sign_extend:TI (minus:DI (match_dup 1)
					  (match_dup 2))))
	    (const_int 0))]
  ""
  "subqv %r1,%2,%0")

;; The Unicos/Mk assembler doesn't support mull.

(define_insn "mulsi3"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(mult:SI (match_operand:SI 1 "reg_or_0_operand" "%rJ")
		 (match_operand:SI 2 "reg_or_8bit_operand" "rI")))]
  "!TARGET_ABI_UNICOSMK"
  "mull %r1,%2,%0"
  [(set_attr "type" "imul")
   (set_attr "opsize" "si")])

(define_insn "*mulsi_se"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(sign_extend:DI
	  (mult:SI (match_operand:SI 1 "reg_or_0_operand" "%rJ")
		   (match_operand:SI 2 "reg_or_8bit_operand" "rI"))))]
  "!TARGET_ABI_UNICOSMK"
  "mull %r1,%2,%0"
  [(set_attr "type" "imul")
   (set_attr "opsize" "si")])

(define_insn "mulvsi3"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(mult:SI (match_operand:SI 1 "reg_or_0_operand" "%rJ")
		 (match_operand:SI 2 "reg_or_8bit_operand" "rI")))
   (trap_if (ne (mult:DI (sign_extend:DI (match_dup 1))
			 (sign_extend:DI (match_dup 2)))
		(sign_extend:DI (mult:SI (match_dup 1)
					 (match_dup 2))))
	    (const_int 0))]
  "!TARGET_ABI_UNICOSMK"
  "mullv %r1,%2,%0"
  [(set_attr "type" "imul")
   (set_attr "opsize" "si")])

(define_insn "muldi3"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(mult:DI (match_operand:DI 1 "reg_or_0_operand" "%rJ")
		 (match_operand:DI 2 "reg_or_8bit_operand" "rI")))]
  ""
  "mulq %r1,%2,%0"
  [(set_attr "type" "imul")])

(define_insn "mulvdi3"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(mult:DI (match_operand:DI 1 "reg_or_0_operand" "%rJ")
		 (match_operand:DI 2 "reg_or_8bit_operand" "rI")))
   (trap_if (ne (mult:TI (sign_extend:TI (match_dup 1))
			 (sign_extend:TI (match_dup 2)))
		(sign_extend:TI (mult:DI (match_dup 1)
					 (match_dup 2))))
	    (const_int 0))]
  ""
  "mulqv %r1,%2,%0"
  [(set_attr "type" "imul")])

(define_expand "umuldi3_highpart"
  [(set (match_operand:DI 0 "register_operand" "")
	(truncate:DI
	 (lshiftrt:TI
	  (mult:TI (zero_extend:TI
		     (match_operand:DI 1 "register_operand" ""))
		   (match_operand:DI 2 "reg_or_8bit_operand" ""))
	  (const_int 64))))]
  ""
{
  if (REG_P (operands[2]))
    operands[2] = gen_rtx_ZERO_EXTEND (TImode, operands[2]);
})

(define_insn "*umuldi3_highpart_reg"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(truncate:DI
	 (lshiftrt:TI
	  (mult:TI (zero_extend:TI
		     (match_operand:DI 1 "register_operand" "r"))
		   (zero_extend:TI
		     (match_operand:DI 2 "register_operand" "r")))
	  (const_int 64))))]
  ""
  "umulh %1,%2,%0"
  [(set_attr "type" "imul")
   (set_attr "opsize" "udi")])

(define_insn "*umuldi3_highpart_const"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(truncate:DI
	 (lshiftrt:TI
	  (mult:TI (zero_extend:TI (match_operand:DI 1 "register_operand" "r"))
		   (match_operand:TI 2 "cint8_operand" "I"))
	  (const_int 64))))]
  ""
  "umulh %1,%2,%0"
  [(set_attr "type" "imul")
   (set_attr "opsize" "udi")])

;; The divide and remainder operations take their inputs from r24 and
;; r25, put their output in r27, and clobber r23 and r28 on all
;; systems except Unicos/Mk. On Unicos, the standard library provides
;; subroutines which use the standard calling convention and work on
;; DImode operands.

;; ??? Force sign-extension here because some versions of OSF/1 and
;; Interix/NT don't do the right thing if the inputs are not properly
;; sign-extended.  But Linux, for instance, does not have this
;; problem.  Is it worth the complication here to eliminate the sign
;; extension?

(define_expand "divsi3"
  [(set (match_dup 3)
	(sign_extend:DI (match_operand:SI 1 "nonimmediate_operand" "")))
   (set (match_dup 4)
	(sign_extend:DI (match_operand:SI 2 "nonimmediate_operand" "")))
   (parallel [(set (match_dup 5)
		   (sign_extend:DI (div:SI (match_dup 3) (match_dup 4))))
	      (clobber (reg:DI 23))
	      (clobber (reg:DI 28))])
   (set (match_operand:SI 0 "nonimmediate_operand" "")
	(subreg:SI (match_dup 5) 0))]
  "! TARGET_ABI_OPEN_VMS && ! TARGET_ABI_UNICOSMK"
{
  operands[3] = gen_reg_rtx (DImode);
  operands[4] = gen_reg_rtx (DImode);
  operands[5] = gen_reg_rtx (DImode);
})

(define_expand "udivsi3"
  [(set (match_dup 3)
	(sign_extend:DI (match_operand:SI 1 "nonimmediate_operand" "")))
   (set (match_dup 4)
	(sign_extend:DI (match_operand:SI 2 "nonimmediate_operand" "")))
   (parallel [(set (match_dup 5)
		   (sign_extend:DI (udiv:SI (match_dup 3) (match_dup 4))))
	      (clobber (reg:DI 23))
	      (clobber (reg:DI 28))])
   (set (match_operand:SI 0 "nonimmediate_operand" "")
	(subreg:SI (match_dup 5) 0))]
  "! TARGET_ABI_OPEN_VMS && ! TARGET_ABI_UNICOSMK"
{
  operands[3] = gen_reg_rtx (DImode);
  operands[4] = gen_reg_rtx (DImode);
  operands[5] = gen_reg_rtx (DImode);
})

(define_expand "modsi3"
  [(set (match_dup 3)
	(sign_extend:DI (match_operand:SI 1 "nonimmediate_operand" "")))
   (set (match_dup 4)
	(sign_extend:DI (match_operand:SI 2 "nonimmediate_operand" "")))
   (parallel [(set (match_dup 5)
		   (sign_extend:DI (mod:SI (match_dup 3) (match_dup 4))))
	      (clobber (reg:DI 23))
	      (clobber (reg:DI 28))])
   (set (match_operand:SI 0 "nonimmediate_operand" "")
	(subreg:SI (match_dup 5) 0))]
  "! TARGET_ABI_OPEN_VMS && ! TARGET_ABI_UNICOSMK"
{
  operands[3] = gen_reg_rtx (DImode);
  operands[4] = gen_reg_rtx (DImode);
  operands[5] = gen_reg_rtx (DImode);
})

(define_expand "umodsi3"
  [(set (match_dup 3)
	(sign_extend:DI (match_operand:SI 1 "nonimmediate_operand" "")))
   (set (match_dup 4)
	(sign_extend:DI (match_operand:SI 2 "nonimmediate_operand" "")))
   (parallel [(set (match_dup 5)
		   (sign_extend:DI (umod:SI (match_dup 3) (match_dup 4))))
	      (clobber (reg:DI 23))
	      (clobber (reg:DI 28))])
   (set (match_operand:SI 0 "nonimmediate_operand" "")
	(subreg:SI (match_dup 5) 0))]
  "! TARGET_ABI_OPEN_VMS && ! TARGET_ABI_UNICOSMK"
{
  operands[3] = gen_reg_rtx (DImode);
  operands[4] = gen_reg_rtx (DImode);
  operands[5] = gen_reg_rtx (DImode);
})

(define_expand "divdi3"
  [(parallel [(set (match_operand:DI 0 "register_operand" "")
		   (div:DI (match_operand:DI 1 "register_operand" "")
			   (match_operand:DI 2 "register_operand" "")))
	      (clobber (reg:DI 23))
	      (clobber (reg:DI 28))])]
  "! TARGET_ABI_OPEN_VMS && ! TARGET_ABI_UNICOSMK"
  "")

(define_expand "udivdi3"
  [(parallel [(set (match_operand:DI 0 "register_operand" "")
		   (udiv:DI (match_operand:DI 1 "register_operand" "")
			    (match_operand:DI 2 "register_operand" "")))
	      (clobber (reg:DI 23))
	      (clobber (reg:DI 28))])]
  "! TARGET_ABI_OPEN_VMS && ! TARGET_ABI_UNICOSMK"
  "")

(define_expand "moddi3"
  [(use (match_operand:DI 0 "register_operand" ""))
   (use (match_operand:DI 1 "register_operand" ""))
   (use (match_operand:DI 2 "register_operand" ""))]
  "!TARGET_ABI_OPEN_VMS"
{
  if (TARGET_ABI_UNICOSMK)
    emit_insn (gen_moddi3_umk (operands[0], operands[1], operands[2]));
  else
    emit_insn (gen_moddi3_dft (operands[0], operands[1], operands[2]));
  DONE;
})

(define_expand "moddi3_dft"
  [(parallel [(set (match_operand:DI 0 "register_operand" "")
		   (mod:DI (match_operand:DI 1 "register_operand" "")
			   (match_operand:DI 2 "register_operand" "")))
	      (clobber (reg:DI 23))
	      (clobber (reg:DI 28))])]
  "! TARGET_ABI_OPEN_VMS && ! TARGET_ABI_UNICOSMK"
  "")

;; On Unicos/Mk, we do as the system's C compiler does:
;; compute the quotient, multiply and subtract.

(define_expand "moddi3_umk"
  [(use (match_operand:DI 0 "register_operand" ""))
   (use (match_operand:DI 1 "register_operand" ""))
   (use (match_operand:DI 2 "register_operand" ""))]
  "TARGET_ABI_UNICOSMK"
{
  rtx div, mul = gen_reg_rtx (DImode);

  div = expand_binop (DImode, sdiv_optab, operands[1], operands[2],
		      NULL_RTX, 0, OPTAB_LIB);
  div = force_reg (DImode, div);
  emit_insn (gen_muldi3 (mul, operands[2], div));
  emit_insn (gen_subdi3 (operands[0], operands[1], mul));
  DONE;
})

(define_expand "umoddi3"
  [(use (match_operand:DI 0 "register_operand" ""))
   (use (match_operand:DI 1 "register_operand" ""))
   (use (match_operand:DI 2 "register_operand" ""))]
  "! TARGET_ABI_OPEN_VMS"
{
  if (TARGET_ABI_UNICOSMK)
    emit_insn (gen_umoddi3_umk (operands[0], operands[1], operands[2]));
  else
    emit_insn (gen_umoddi3_dft (operands[0], operands[1], operands[2]));
  DONE;
})

(define_expand "umoddi3_dft"
  [(parallel [(set (match_operand:DI 0 "register_operand" "")
		   (umod:DI (match_operand:DI 1 "register_operand" "")
			    (match_operand:DI 2 "register_operand" "")))
	      (clobber (reg:DI 23))
	      (clobber (reg:DI 28))])]
  "! TARGET_ABI_OPEN_VMS && ! TARGET_ABI_UNICOSMK"
  "")

(define_expand "umoddi3_umk"
  [(use (match_operand:DI 0 "register_operand" ""))
   (use (match_operand:DI 1 "register_operand" ""))
   (use (match_operand:DI 2 "register_operand" ""))]
  "TARGET_ABI_UNICOSMK"
{
  rtx div, mul = gen_reg_rtx (DImode);

  div = expand_binop (DImode, udiv_optab, operands[1], operands[2],
		      NULL_RTX, 1, OPTAB_LIB);
  div = force_reg (DImode, div);
  emit_insn (gen_muldi3 (mul, operands[2], div));
  emit_insn (gen_subdi3 (operands[0], operands[1], mul));
  DONE;
})

;; Lengths of 8 for ldq $t12,__divq($gp); jsr $t9,($t12),__divq as
;; expanded by the assembler.

(define_insn_and_split "*divmodsi_internal_er"
  [(set (match_operand:DI 0 "register_operand" "=c")
	(sign_extend:DI (match_operator:SI 3 "divmod_operator"
			[(match_operand:DI 1 "register_operand" "a")
			 (match_operand:DI 2 "register_operand" "b")])))
   (clobber (reg:DI 23))
   (clobber (reg:DI 28))]
  "TARGET_EXPLICIT_RELOCS && ! TARGET_ABI_OPEN_VMS"
  "#"
  "&& reload_completed"
  [(parallel [(set (match_dup 0)
		   (sign_extend:DI (match_dup 3)))
	      (use (match_dup 0))
	      (use (match_dup 4))
	      (clobber (reg:DI 23))
	      (clobber (reg:DI 28))])]
{
  const char *str;
  switch (GET_CODE (operands[3]))
    {
    case DIV: 
      str = "__divl";
      break; 
    case UDIV:
      str = "__divlu";
      break;
    case MOD:
      str = "__reml";
      break;
    case UMOD:
      str = "__remlu";
      break;
    default:
      gcc_unreachable ();
    }
  operands[4] = GEN_INT (alpha_next_sequence_number++);
  emit_insn (gen_movdi_er_high_g (operands[0], pic_offset_table_rtx,
				  gen_rtx_SYMBOL_REF (DImode, str),
				  operands[4]));
}
  [(set_attr "type" "jsr")
   (set_attr "length" "8")])

(define_insn "*divmodsi_internal_er_1"
  [(set (match_operand:DI 0 "register_operand" "=c")
	(sign_extend:DI (match_operator:SI 3 "divmod_operator"
                        [(match_operand:DI 1 "register_operand" "a")
                         (match_operand:DI 2 "register_operand" "b")])))
   (use (match_operand:DI 4 "register_operand" "c"))
   (use (match_operand 5 "const_int_operand" ""))
   (clobber (reg:DI 23))
   (clobber (reg:DI 28))]
  "TARGET_EXPLICIT_RELOCS && ! TARGET_ABI_OPEN_VMS"
  "jsr $23,($27),__%E3%j5"
  [(set_attr "type" "jsr")
   (set_attr "length" "4")])

(define_insn "*divmodsi_internal"
  [(set (match_operand:DI 0 "register_operand" "=c")
	(sign_extend:DI (match_operator:SI 3 "divmod_operator"
			[(match_operand:DI 1 "register_operand" "a")
			 (match_operand:DI 2 "register_operand" "b")])))
   (clobber (reg:DI 23))
   (clobber (reg:DI 28))]
  "! TARGET_ABI_OPEN_VMS && ! TARGET_ABI_UNICOSMK"
  "%E3 %1,%2,%0"
  [(set_attr "type" "jsr")
   (set_attr "length" "8")])

(define_insn_and_split "*divmoddi_internal_er"
  [(set (match_operand:DI 0 "register_operand" "=c")
	(match_operator:DI 3 "divmod_operator"
			[(match_operand:DI 1 "register_operand" "a")
			 (match_operand:DI 2 "register_operand" "b")]))
   (clobber (reg:DI 23))
   (clobber (reg:DI 28))]
  "TARGET_EXPLICIT_RELOCS && ! TARGET_ABI_OPEN_VMS"
  "#"
  "&& reload_completed"
  [(parallel [(set (match_dup 0) (match_dup 3))
	      (use (match_dup 0))
	      (use (match_dup 4))
	      (clobber (reg:DI 23))
	      (clobber (reg:DI 28))])]
{
  const char *str;
  switch (GET_CODE (operands[3]))
    {
    case DIV: 
      str = "__divq";
      break; 
    case UDIV:
      str = "__divqu";
      break;
    case MOD:
      str = "__remq";
      break;
    case UMOD:
      str = "__remqu";
      break;
    default:
      gcc_unreachable ();
    }
  operands[4] = GEN_INT (alpha_next_sequence_number++);
  emit_insn (gen_movdi_er_high_g (operands[0], pic_offset_table_rtx,
				  gen_rtx_SYMBOL_REF (DImode, str),
				  operands[4]));
}
  [(set_attr "type" "jsr")
   (set_attr "length" "8")])

(define_insn "*divmoddi_internal_er_1"
  [(set (match_operand:DI 0 "register_operand" "=c")
	(match_operator:DI 3 "divmod_operator"
                        [(match_operand:DI 1 "register_operand" "a")
                         (match_operand:DI 2 "register_operand" "b")]))
   (use (match_operand:DI 4 "register_operand" "c"))
   (use (match_operand 5 "const_int_operand" ""))
   (clobber (reg:DI 23))
   (clobber (reg:DI 28))]
  "TARGET_EXPLICIT_RELOCS && ! TARGET_ABI_OPEN_VMS"
  "jsr $23,($27),__%E3%j5"
  [(set_attr "type" "jsr")
   (set_attr "length" "4")])

(define_insn "*divmoddi_internal"
  [(set (match_operand:DI 0 "register_operand" "=c")
	(match_operator:DI 3 "divmod_operator"
			[(match_operand:DI 1 "register_operand" "a")
			 (match_operand:DI 2 "register_operand" "b")]))
   (clobber (reg:DI 23))
   (clobber (reg:DI 28))]
  "! TARGET_ABI_OPEN_VMS && ! TARGET_ABI_UNICOSMK"
  "%E3 %1,%2,%0"
  [(set_attr "type" "jsr")
   (set_attr "length" "8")])

;; Next are the basic logical operations.  We only expose the DImode operations
;; to the rtl expanders, but SImode versions exist for combine as well as for
;; the atomic operation splitters.

(define_insn "*andsi_internal"
  [(set (match_operand:SI 0 "register_operand" "=r,r,r")
	(and:SI (match_operand:SI 1 "reg_or_0_operand" "%rJ,rJ,rJ")
		(match_operand:SI 2 "and_operand" "rI,N,MH")))]
  ""
  "@
   and %r1,%2,%0
   bic %r1,%N2,%0
   zapnot %r1,%m2,%0"
  [(set_attr "type" "ilog,ilog,shift")])

(define_insn "anddi3"
  [(set (match_operand:DI 0 "register_operand" "=r,r,r")
	(and:DI (match_operand:DI 1 "reg_or_0_operand" "%rJ,rJ,rJ")
		(match_operand:DI 2 "and_operand" "rI,N,MH")))]
  ""
  "@
   and %r1,%2,%0
   bic %r1,%N2,%0
   zapnot %r1,%m2,%0"
  [(set_attr "type" "ilog,ilog,shift")])

;; There are times when we can split an AND into two AND insns.  This occurs
;; when we can first clear any bytes and then clear anything else.  For
;; example "I & 0xffff07" is "(I & 0xffffff) & 0xffffffffffffff07".
;; Only do this when running on 64-bit host since the computations are
;; too messy otherwise.

(define_split
  [(set (match_operand:DI 0 "register_operand" "")
	(and:DI (match_operand:DI 1 "register_operand" "")
		(match_operand:DI 2 "const_int_operand" "")))]
  "HOST_BITS_PER_WIDE_INT == 64 && ! and_operand (operands[2], DImode)"
  [(set (match_dup 0) (and:DI (match_dup 1) (match_dup 3)))
   (set (match_dup 0) (and:DI (match_dup 0) (match_dup 4)))]
{
  unsigned HOST_WIDE_INT mask1 = INTVAL (operands[2]);
  unsigned HOST_WIDE_INT mask2 = mask1;
  int i;

  /* For each byte that isn't all zeros, make it all ones.  */
  for (i = 0; i < 64; i += 8)
    if ((mask1 & ((HOST_WIDE_INT) 0xff << i)) != 0)
      mask1 |= (HOST_WIDE_INT) 0xff << i;

  /* Now turn on any bits we've just turned off.  */
  mask2 |= ~ mask1;

  operands[3] = GEN_INT (mask1);
  operands[4] = GEN_INT (mask2);
})

(define_expand "zero_extendqihi2"
  [(set (match_operand:HI 0 "register_operand" "")
	(zero_extend:HI (match_operand:QI 1 "nonimmediate_operand" "")))]
  ""
{
  if (! TARGET_BWX)
    operands[1] = force_reg (QImode, operands[1]);
})

(define_insn "*zero_extendqihi2_bwx"
  [(set (match_operand:HI 0 "register_operand" "=r,r")
	(zero_extend:HI (match_operand:QI 1 "nonimmediate_operand" "r,m")))]
  "TARGET_BWX"
  "@
   and %1,0xff,%0
   ldbu %0,%1"
  [(set_attr "type" "ilog,ild")])

(define_insn "*zero_extendqihi2_nobwx"
  [(set (match_operand:HI 0 "register_operand" "=r")
	(zero_extend:HI (match_operand:QI 1 "register_operand" "r")))]
  "! TARGET_BWX"
  "and %1,0xff,%0"
  [(set_attr "type" "ilog")])

(define_expand "zero_extendqisi2"
  [(set (match_operand:SI 0 "register_operand" "")
	(zero_extend:SI (match_operand:QI 1 "nonimmediate_operand" "")))]
  ""
{
  if (! TARGET_BWX)
    operands[1] = force_reg (QImode, operands[1]);
})

(define_insn "*zero_extendqisi2_bwx"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(zero_extend:SI (match_operand:QI 1 "nonimmediate_operand" "r,m")))]
  "TARGET_BWX"
  "@
   and %1,0xff,%0
   ldbu %0,%1"
  [(set_attr "type" "ilog,ild")])

(define_insn "*zero_extendqisi2_nobwx"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(zero_extend:SI (match_operand:QI 1 "register_operand" "r")))]
  "! TARGET_BWX"
  "and %1,0xff,%0"
  [(set_attr "type" "ilog")])

(define_expand "zero_extendqidi2"
  [(set (match_operand:DI 0 "register_operand" "")
	(zero_extend:DI (match_operand:QI 1 "nonimmediate_operand" "")))]
  ""
{
  if (! TARGET_BWX)
    operands[1] = force_reg (QImode, operands[1]);
})

(define_insn "*zero_extendqidi2_bwx"
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(zero_extend:DI (match_operand:QI 1 "nonimmediate_operand" "r,m")))]
  "TARGET_BWX"
  "@
   and %1,0xff,%0
   ldbu %0,%1"
  [(set_attr "type" "ilog,ild")])

(define_insn "*zero_extendqidi2_nobwx"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(zero_extend:DI (match_operand:QI 1 "register_operand" "r")))]
  "! TARGET_BWX"
  "and %1,0xff,%0"
  [(set_attr "type" "ilog")])

(define_expand "zero_extendhisi2"
  [(set (match_operand:SI 0 "register_operand" "")
	(zero_extend:SI (match_operand:HI 1 "nonimmediate_operand" "")))]
  ""
{
  if (! TARGET_BWX)
    operands[1] = force_reg (HImode, operands[1]);
})

(define_insn "*zero_extendhisi2_bwx"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(zero_extend:SI (match_operand:HI 1 "nonimmediate_operand" "r,m")))]
  "TARGET_BWX"
  "@
   zapnot %1,3,%0
   ldwu %0,%1"
  [(set_attr "type" "shift,ild")])

(define_insn "*zero_extendhisi2_nobwx"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(zero_extend:SI (match_operand:HI 1 "register_operand" "r")))]
  "! TARGET_BWX"
  "zapnot %1,3,%0"
  [(set_attr "type" "shift")])

(define_expand "zero_extendhidi2"
  [(set (match_operand:DI 0 "register_operand" "")
	(zero_extend:DI (match_operand:HI 1 "nonimmediate_operand" "")))]
  ""
{
  if (! TARGET_BWX)
    operands[1] = force_reg (HImode, operands[1]);
})

(define_insn "*zero_extendhidi2_bwx"
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(zero_extend:DI (match_operand:HI 1 "nonimmediate_operand" "r,m")))]
  "TARGET_BWX"
  "@
   zapnot %1,3,%0
   ldwu %0,%1"
  [(set_attr "type" "shift,ild")])

(define_insn "*zero_extendhidi2_nobwx"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(zero_extend:DI (match_operand:HI 1 "register_operand" "r")))]
  ""
  "zapnot %1,3,%0"
  [(set_attr "type" "shift")])

(define_insn "zero_extendsidi2"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(zero_extend:DI (match_operand:SI 1 "register_operand" "r")))]
  ""
  "zapnot %1,15,%0"
  [(set_attr "type" "shift")])

(define_insn "*andnotsi3"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(and:SI (not:SI (match_operand:SI 1 "reg_or_8bit_operand" "rI"))
		(match_operand:SI 2 "reg_or_0_operand" "rJ")))]
  ""
  "bic %r2,%1,%0"
  [(set_attr "type" "ilog")])

(define_insn "andnotdi3"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(and:DI (not:DI (match_operand:DI 1 "reg_or_8bit_operand" "rI"))
		(match_operand:DI 2 "reg_or_0_operand" "rJ")))]
  ""
  "bic %r2,%1,%0"
  [(set_attr "type" "ilog")])

(define_insn "*iorsi_internal"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(ior:SI (match_operand:SI 1 "reg_or_0_operand" "%rJ,rJ")
		(match_operand:SI 2 "or_operand" "rI,N")))]
  ""
  "@
   bis %r1,%2,%0
   ornot %r1,%N2,%0"
  [(set_attr "type" "ilog")])

(define_insn "iordi3"
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(ior:DI (match_operand:DI 1 "reg_or_0_operand" "%rJ,rJ")
		(match_operand:DI 2 "or_operand" "rI,N")))]
  ""
  "@
   bis %r1,%2,%0
   ornot %r1,%N2,%0"
  [(set_attr "type" "ilog")])

(define_insn "*one_cmplsi_internal"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(not:SI (match_operand:SI 1 "reg_or_8bit_operand" "rI")))]
  ""
  "ornot $31,%1,%0"
  [(set_attr "type" "ilog")])

(define_insn "one_cmpldi2"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(not:DI (match_operand:DI 1 "reg_or_8bit_operand" "rI")))]
  ""
  "ornot $31,%1,%0"
  [(set_attr "type" "ilog")])

(define_insn "*iornotsi3"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(ior:SI (not:SI (match_operand:SI 1 "reg_or_8bit_operand" "rI"))
		(match_operand:SI 2 "reg_or_0_operand" "rJ")))]
  ""
  "ornot %r2,%1,%0"
  [(set_attr "type" "ilog")])

(define_insn "*iornotdi3"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(ior:DI (not:DI (match_operand:DI 1 "reg_or_8bit_operand" "rI"))
		(match_operand:DI 2 "reg_or_0_operand" "rJ")))]
  ""
  "ornot %r2,%1,%0"
  [(set_attr "type" "ilog")])

(define_insn "*xorsi_internal"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(xor:SI (match_operand:SI 1 "reg_or_0_operand" "%rJ,rJ")
		(match_operand:SI 2 "or_operand" "rI,N")))]
  ""
  "@
   xor %r1,%2,%0
   eqv %r1,%N2,%0"
  [(set_attr "type" "ilog")])

(define_insn "xordi3"
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(xor:DI (match_operand:DI 1 "reg_or_0_operand" "%rJ,rJ")
		(match_operand:DI 2 "or_operand" "rI,N")))]
  ""
  "@
   xor %r1,%2,%0
   eqv %r1,%N2,%0"
  [(set_attr "type" "ilog")])

(define_insn "*xornotsi3"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(not:SI (xor:SI (match_operand:SI 1 "register_operand" "%rJ")
			(match_operand:SI 2 "register_operand" "rI"))))]
  ""
  "eqv %r1,%2,%0"
  [(set_attr "type" "ilog")])

(define_insn "*xornotdi3"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(not:DI (xor:DI (match_operand:DI 1 "register_operand" "%rJ")
			(match_operand:DI 2 "register_operand" "rI"))))]
  ""
  "eqv %r1,%2,%0"
  [(set_attr "type" "ilog")])

;; Handle FFS and related insns iff we support CIX.

(define_expand "ffsdi2"
  [(set (match_dup 2)
	(ctz:DI (match_operand:DI 1 "register_operand" "")))
   (set (match_dup 3)
	(plus:DI (match_dup 2) (const_int 1)))
   (set (match_operand:DI 0 "register_operand" "")
	(if_then_else:DI (eq (match_dup 1) (const_int 0))
			 (const_int 0) (match_dup 3)))]
  "TARGET_CIX"
{
  operands[2] = gen_reg_rtx (DImode);
  operands[3] = gen_reg_rtx (DImode);
})

(define_insn "clzdi2"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(clz:DI (match_operand:DI 1 "register_operand" "r")))]
  "TARGET_CIX"
  "ctlz %1,%0"
  [(set_attr "type" "mvi")])

(define_insn "ctzdi2"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(ctz:DI (match_operand:DI 1 "register_operand" "r")))]
  "TARGET_CIX"
  "cttz %1,%0"
  [(set_attr "type" "mvi")])

(define_insn "popcountdi2"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(popcount:DI (match_operand:DI 1 "register_operand" "r")))]
  "TARGET_CIX"
  "ctpop %1,%0"
  [(set_attr "type" "mvi")])

;; Next come the shifts and the various extract and insert operations.

(define_insn "ashldi3"
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(ashift:DI (match_operand:DI 1 "reg_or_0_operand" "rJ,rJ")
		   (match_operand:DI 2 "reg_or_6bit_operand" "P,rS")))]
  ""
{
  switch (which_alternative)
    {
    case 0:
      if (operands[2] == const1_rtx)
	return "addq %r1,%r1,%0";
      else
	return "s%P2addq %r1,0,%0";
    case 1:
      return "sll %r1,%2,%0";
    default:
      gcc_unreachable ();
    }
}
  [(set_attr "type" "iadd,shift")])

(define_insn "*ashldi_se"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(sign_extend:DI
	 (subreg:SI (ashift:DI (match_operand:DI 1 "reg_or_0_operand" "rJ")
			       (match_operand:DI 2 "const_int_operand" "P"))
		    0)))]
  "INTVAL (operands[2]) >= 1 && INTVAL (operands[2]) <= 3"
{
  if (operands[2] == const1_rtx)
    return "addl %r1,%r1,%0";
  else
    return "s%P2addl %r1,0,%0";
}
  [(set_attr "type" "iadd")])

(define_insn "lshrdi3"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(lshiftrt:DI (match_operand:DI 1 "reg_or_0_operand" "rJ")
		     (match_operand:DI 2 "reg_or_6bit_operand" "rS")))]
  ""
  "srl %r1,%2,%0"
  [(set_attr "type" "shift")])

(define_insn "ashrdi3"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(ashiftrt:DI (match_operand:DI 1 "reg_or_0_operand" "rJ")
		     (match_operand:DI 2 "reg_or_6bit_operand" "rS")))]
  ""
  "sra %r1,%2,%0"
  [(set_attr "type" "shift")])

(define_expand "extendqihi2"
  [(set (match_dup 2)
	(ashift:DI (match_operand:QI 1 "some_operand" "")
		   (const_int 56)))
   (set (match_operand:HI 0 "register_operand" "")
	(ashiftrt:DI (match_dup 2)
		     (const_int 56)))]
  ""
{
  if (TARGET_BWX)
    {
      emit_insn (gen_extendqihi2x (operands[0],
				   force_reg (QImode, operands[1])));
      DONE;
    }

 /* If we have an unaligned MEM, extend to DImode (which we do
     specially) and then copy to the result.  */
  if (unaligned_memory_operand (operands[1], HImode))
    {
      rtx temp = gen_reg_rtx (DImode);

      emit_insn (gen_extendqidi2 (temp, operands[1]));
      emit_move_insn (operands[0], gen_lowpart (HImode, temp));
      DONE;
    }

  operands[0] = gen_lowpart (DImode, operands[0]);
  operands[1] = gen_lowpart (DImode, force_reg (QImode, operands[1]));
  operands[2] = gen_reg_rtx (DImode);
})

(define_insn "extendqidi2x"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(sign_extend:DI (match_operand:QI 1 "register_operand" "r")))]
  "TARGET_BWX"
  "sextb %1,%0"
  [(set_attr "type" "shift")])

(define_insn "extendhidi2x"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(sign_extend:DI (match_operand:HI 1 "register_operand" "r")))]
  "TARGET_BWX"
  "sextw %1,%0"
  [(set_attr "type" "shift")])

(define_insn "extendqisi2x"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(sign_extend:SI (match_operand:QI 1 "register_operand" "r")))]
  "TARGET_BWX"
  "sextb %1,%0"
  [(set_attr "type" "shift")])

(define_insn "extendhisi2x"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(sign_extend:SI (match_operand:HI 1 "register_operand" "r")))]
  "TARGET_BWX"
  "sextw %1,%0"
  [(set_attr "type" "shift")])

(define_insn "extendqihi2x"
  [(set (match_operand:HI 0 "register_operand" "=r")
	(sign_extend:HI (match_operand:QI 1 "register_operand" "r")))]
  "TARGET_BWX"
  "sextb %1,%0"
  [(set_attr "type" "shift")])

(define_expand "extendqisi2"
  [(set (match_dup 2)
	(ashift:DI (match_operand:QI 1 "some_operand" "")
		   (const_int 56)))
   (set (match_operand:SI 0 "register_operand" "")
	(ashiftrt:DI (match_dup 2)
		     (const_int 56)))]
  ""
{
  if (TARGET_BWX)
    {
      emit_insn (gen_extendqisi2x (operands[0],
				   force_reg (QImode, operands[1])));
      DONE;
    }

  /* If we have an unaligned MEM, extend to a DImode form of
     the result (which we do specially).  */
  if (unaligned_memory_operand (operands[1], QImode))
    {
      rtx temp = gen_reg_rtx (DImode);

      emit_insn (gen_extendqidi2 (temp, operands[1]));
      emit_move_insn (operands[0], gen_lowpart (SImode, temp));
      DONE;
    }

  operands[0] = gen_lowpart (DImode, operands[0]);
  operands[1] = gen_lowpart (DImode, force_reg (QImode, operands[1]));
  operands[2] = gen_reg_rtx (DImode);
})

(define_expand "extendqidi2"
  [(set (match_dup 2)
	(ashift:DI (match_operand:QI 1 "some_operand" "")
		   (const_int 56)))
   (set (match_operand:DI 0 "register_operand" "")
	(ashiftrt:DI (match_dup 2)
		     (const_int 56)))]
  ""
{
  if (TARGET_BWX)
    {
      emit_insn (gen_extendqidi2x (operands[0],
				   force_reg (QImode, operands[1])));
      DONE;
    }

  if (unaligned_memory_operand (operands[1], QImode))
    {
      rtx seq = gen_unaligned_extendqidi (operands[0], XEXP (operands[1], 0));
      alpha_set_memflags (seq, operands[1]);
      emit_insn (seq);
      DONE;
    }

  operands[1] = gen_lowpart (DImode, force_reg (QImode, operands[1]));
  operands[2] = gen_reg_rtx (DImode);
})

(define_expand "extendhisi2"
  [(set (match_dup 2)
	(ashift:DI (match_operand:HI 1 "some_operand" "")
		   (const_int 48)))
   (set (match_operand:SI 0 "register_operand" "")
	(ashiftrt:DI (match_dup 2)
		     (const_int 48)))]
  ""
{
  if (TARGET_BWX)
    {
      emit_insn (gen_extendhisi2x (operands[0],
				   force_reg (HImode, operands[1])));
      DONE;
    }

  /* If we have an unaligned MEM, extend to a DImode form of
     the result (which we do specially).  */
  if (unaligned_memory_operand (operands[1], HImode))
    {
      rtx temp = gen_reg_rtx (DImode);

      emit_insn (gen_extendhidi2 (temp, operands[1]));
      emit_move_insn (operands[0], gen_lowpart (SImode, temp));
      DONE;
    }

  operands[0] = gen_lowpart (DImode, operands[0]);
  operands[1] = gen_lowpart (DImode, force_reg (HImode, operands[1]));
  operands[2] = gen_reg_rtx (DImode);
})

(define_expand "extendhidi2"
  [(set (match_dup 2)
	(ashift:DI (match_operand:HI 1 "some_operand" "")
		   (const_int 48)))
   (set (match_operand:DI 0 "register_operand" "")
	(ashiftrt:DI (match_dup 2)
		     (const_int 48)))]
  ""
{
  if (TARGET_BWX)
    {
      emit_insn (gen_extendhidi2x (operands[0],
				   force_reg (HImode, operands[1])));
      DONE;
    }

  if (unaligned_memory_operand (operands[1], HImode))
    {
      rtx seq = gen_unaligned_extendhidi (operands[0], XEXP (operands[1], 0));

      alpha_set_memflags (seq, operands[1]);
      emit_insn (seq);
      DONE;
    }

  operands[1] = gen_lowpart (DImode, force_reg (HImode, operands[1]));
  operands[2] = gen_reg_rtx (DImode);
})

;; Here's how we sign extend an unaligned byte and halfword.  Doing this
;; as a pattern saves one instruction.  The code is similar to that for
;; the unaligned loads (see below).
;;
;; Operand 1 is the address, operand 0 is the result.
(define_expand "unaligned_extendqidi"
  [(use (match_operand:QI 0 "register_operand" ""))
   (use (match_operand:DI 1 "address_operand" ""))]
  ""
{
  operands[0] = gen_lowpart (DImode, operands[0]);
  if (WORDS_BIG_ENDIAN)
    emit_insn (gen_unaligned_extendqidi_be (operands[0], operands[1]));
  else
    emit_insn (gen_unaligned_extendqidi_le (operands[0], operands[1]));
  DONE;
})

(define_expand "unaligned_extendqidi_le"
  [(set (match_dup 3)
	(mem:DI (and:DI (match_operand:DI 1 "" "") (const_int -8))))
   (set (match_dup 4)
	(ashift:DI (match_dup 3)
		   (minus:DI (const_int 56)
			     (ashift:DI
			      (and:DI (plus:DI (match_dup 2) (const_int -1))
				      (const_int 7))
			      (const_int 3)))))
   (set (match_operand:DI 0 "register_operand" "")
	(ashiftrt:DI (match_dup 4) (const_int 56)))]
  "! WORDS_BIG_ENDIAN"
{
  operands[2] = get_unaligned_offset (operands[1], 1);
  operands[3] = gen_reg_rtx (DImode);
  operands[4] = gen_reg_rtx (DImode);
})

(define_expand "unaligned_extendqidi_be"
  [(set (match_dup 3)
	(mem:DI (and:DI (match_operand:DI 1 "" "") (const_int -8))))
   (set (match_dup 4)
	(ashift:DI (match_dup 3)
		   (ashift:DI
		     (and:DI
		       (plus:DI (match_dup 2) (const_int 1))
		       (const_int 7))
		     (const_int 3))))
   (set (match_operand:DI 0 "register_operand" "")
	(ashiftrt:DI (match_dup 4) (const_int 56)))]
  "WORDS_BIG_ENDIAN"
{
  operands[2] = get_unaligned_offset (operands[1], -1);
  operands[3] = gen_reg_rtx (DImode);
  operands[4] = gen_reg_rtx (DImode);
})

(define_expand "unaligned_extendhidi"
  [(use (match_operand:QI 0 "register_operand" ""))
   (use (match_operand:DI 1 "address_operand" ""))]
  ""
{
  operands[0] = gen_lowpart (DImode, operands[0]);
  if (WORDS_BIG_ENDIAN)
    emit_insn (gen_unaligned_extendhidi_be (operands[0], operands[1]));
  else
    emit_insn (gen_unaligned_extendhidi_le (operands[0], operands[1]));
  DONE;
})

(define_expand "unaligned_extendhidi_le"
  [(set (match_dup 3)
	(mem:DI (and:DI (match_operand:DI 1 "" "") (const_int -8))))
   (set (match_dup 4)
	(ashift:DI (match_dup 3)
		   (minus:DI (const_int 56)
			     (ashift:DI
			      (and:DI (plus:DI (match_dup 2) (const_int -1))
				      (const_int 7))
			      (const_int 3)))))
   (set (match_operand:DI 0 "register_operand" "")
	(ashiftrt:DI (match_dup 4) (const_int 48)))]
  "! WORDS_BIG_ENDIAN"
{
  operands[2] = get_unaligned_offset (operands[1], 2);
  operands[3] = gen_reg_rtx (DImode);
  operands[4] = gen_reg_rtx (DImode);
})

(define_expand "unaligned_extendhidi_be"
  [(set (match_dup 3)
	(mem:DI (and:DI (match_operand:DI 1 "" "") (const_int -8))))
   (set (match_dup 4)
	(ashift:DI (match_dup 3)
		   (ashift:DI
		     (and:DI
		       (plus:DI (match_dup 2) (const_int 1))
		       (const_int 7))
		     (const_int 3))))
   (set (match_operand:DI 0 "register_operand" "")
	(ashiftrt:DI (match_dup 4) (const_int 48)))]
  "WORDS_BIG_ENDIAN"
{
  operands[2] = get_unaligned_offset (operands[1], -1);
  operands[3] = gen_reg_rtx (DImode);
  operands[4] = gen_reg_rtx (DImode);
})

(define_insn "*extxl_const"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(zero_extract:DI (match_operand:DI 1 "reg_or_0_operand" "rJ")
			 (match_operand:DI 2 "mode_width_operand" "n")
			 (match_operand:DI 3 "mul8_operand" "I")))]
  ""
  "ext%M2l %r1,%s3,%0"
  [(set_attr "type" "shift")])

(define_insn "extxl_le"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(zero_extract:DI (match_operand:DI 1 "reg_or_0_operand" "rJ")
			 (match_operand:DI 2 "mode_width_operand" "n")
			 (ashift:DI (match_operand:DI 3 "reg_or_8bit_operand" "rI")
				    (const_int 3))))]
  "! WORDS_BIG_ENDIAN"
  "ext%M2l %r1,%3,%0"
  [(set_attr "type" "shift")])

(define_insn "extxl_be"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(zero_extract:DI (match_operand:DI 1 "reg_or_0_operand" "rJ")
			 (match_operand:DI 2 "mode_width_operand" "n")
			 (minus:DI
			   (const_int 56)
			   (ashift:DI
			     (match_operand:DI 3 "reg_or_8bit_operand" "rI")
			     (const_int 3)))))]
  "WORDS_BIG_ENDIAN"
  "ext%M2l %r1,%3,%0"
  [(set_attr "type" "shift")])

;; Combine has some strange notion of preserving existing undefined behavior
;; in shifts larger than a word size.  So capture these patterns that it
;; should have turned into zero_extracts.

(define_insn "*extxl_1_le"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(and:DI (lshiftrt:DI (match_operand:DI 1 "reg_or_0_operand" "rJ")
		  (ashift:DI (match_operand:DI 2 "reg_or_8bit_operand" "rI")
			     (const_int 3)))
	     (match_operand:DI 3 "mode_mask_operand" "n")))]
  "! WORDS_BIG_ENDIAN"
  "ext%U3l %1,%2,%0"
  [(set_attr "type" "shift")])

(define_insn "*extxl_1_be"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(and:DI (lshiftrt:DI
		  (match_operand:DI 1 "reg_or_0_operand" "rJ")
		  (minus:DI (const_int 56)
		    (ashift:DI (match_operand:DI 2 "reg_or_8bit_operand" "rI")
			       (const_int 3))))
		(match_operand:DI 3 "mode_mask_operand" "n")))]
  "WORDS_BIG_ENDIAN"
  "ext%U3l %1,%2,%0"
  [(set_attr "type" "shift")])

(define_insn "*extql_2_le"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(lshiftrt:DI (match_operand:DI 1 "reg_or_0_operand" "rJ")
	  (ashift:DI (match_operand:DI 2 "reg_or_8bit_operand" "rI")
		     (const_int 3))))]
  "! WORDS_BIG_ENDIAN"
  "extql %1,%2,%0"
  [(set_attr "type" "shift")])

(define_insn "*extql_2_be"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(lshiftrt:DI
	  (match_operand:DI 1 "reg_or_0_operand" "rJ")
	  (minus:DI (const_int 56)
		    (ashift:DI
		      (match_operand:DI 2 "reg_or_8bit_operand" "rI")
		      (const_int 3)))))]
  "WORDS_BIG_ENDIAN"
  "extql %1,%2,%0"
  [(set_attr "type" "shift")])

(define_insn "extqh_le"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(ashift:DI
	 (match_operand:DI 1 "reg_or_0_operand" "rJ")
	  (minus:DI (const_int 56)
		    (ashift:DI
		     (and:DI
		      (plus:DI (match_operand:DI 2 "reg_or_8bit_operand" "rI")
			       (const_int -1))
		      (const_int 7))
		     (const_int 3)))))]
  "! WORDS_BIG_ENDIAN"
  "extqh %r1,%2,%0"
  [(set_attr "type" "shift")])

(define_insn "extqh_be"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(ashift:DI
	  (match_operand:DI 1 "reg_or_0_operand" "rJ")
	  (ashift:DI
	    (and:DI
	      (plus:DI (match_operand:DI 2 "reg_or_8bit_operand" "rI")
		       (const_int 1))
	      (const_int 7))
	    (const_int 3))))]
  "WORDS_BIG_ENDIAN"
  "extqh %r1,%2,%0"
  [(set_attr "type" "shift")])

(define_insn "extlh_le"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(ashift:DI
	 (and:DI (match_operand:DI 1 "reg_or_0_operand" "rJ")
		 (const_int 2147483647))
	 (minus:DI (const_int 56)
		    (ashift:DI
		     (and:DI
		      (plus:DI (match_operand:DI 2 "reg_or_8bit_operand" "rI")
			       (const_int -1))
		      (const_int 7))
		     (const_int 3)))))]
  "! WORDS_BIG_ENDIAN"
  "extlh %r1,%2,%0"
  [(set_attr "type" "shift")])

(define_insn "extlh_be"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(and:DI
	  (ashift:DI
	    (match_operand:DI 1 "reg_or_0_operand" "rJ")
	    (ashift:DI
	      (and:DI
		(plus:DI
		  (match_operand:DI 2 "reg_or_8bit_operand" "rI")
		  (const_int 1))
		(const_int 7))
	      (const_int 3)))
	  (const_int 2147483647)))]
  "WORDS_BIG_ENDIAN"
  "extlh %r1,%2,%0"
  [(set_attr "type" "shift")])

(define_insn "extwh_le"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(ashift:DI
	 (and:DI (match_operand:DI 1 "reg_or_0_operand" "rJ")
		 (const_int 65535))
	 (minus:DI (const_int 56)
		    (ashift:DI
		     (and:DI
		      (plus:DI (match_operand:DI 2 "reg_or_8bit_operand" "rI")
			       (const_int -1))
		      (const_int 7))
		     (const_int 3)))))]
  "! WORDS_BIG_ENDIAN"
  "extwh %r1,%2,%0"
  [(set_attr "type" "shift")])

(define_insn "extwh_be"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(and:DI
	  (ashift:DI (match_operand:DI 1 "reg_or_0_operand" "rJ")
		     (ashift:DI
		       (and:DI
			 (plus:DI
			   (match_operand:DI 2 "reg_or_8bit_operand" "rI")
			   (const_int 1))
			 (const_int 7))
		       (const_int 3)))
	  (const_int 65535)))]
  "WORDS_BIG_ENDIAN"
  "extwh %r1,%2,%0"
  [(set_attr "type" "shift")])

;; This converts an extXl into an extXh with an appropriate adjustment
;; to the address calculation.

;;(define_split
;;  [(set (match_operand:DI 0 "register_operand" "")
;;	(ashift:DI (zero_extract:DI (match_operand:DI 1 "register_operand" "")
;;				    (match_operand:DI 2 "mode_width_operand" "")
;;				    (ashift:DI (match_operand:DI 3 "" "")
;;					       (const_int 3)))
;;		   (match_operand:DI 4 "const_int_operand" "")))
;;   (clobber (match_operand:DI 5 "register_operand" ""))]
;;  "INTVAL (operands[4]) == 64 - INTVAL (operands[2])"
;;  [(set (match_dup 5) (match_dup 6))
;;   (set (match_dup 0)
;;	(ashift:DI (zero_extract:DI (match_dup 1) (match_dup 2)
;;				    (ashift:DI (plus:DI (match_dup 5)
;;							(match_dup 7))
;;					       (const_int 3)))
;;		   (match_dup 4)))]
;;  "
;;{
;;  operands[6] = plus_constant (operands[3],
;;			       INTVAL (operands[2]) / BITS_PER_UNIT);
;;  operands[7] = GEN_INT (- INTVAL (operands[2]) / BITS_PER_UNIT);
;;}")

(define_insn "*insbl_const"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(ashift:DI (zero_extend:DI (match_operand:QI 1 "register_operand" "r"))
		   (match_operand:DI 2 "mul8_operand" "I")))]
  ""
  "insbl %1,%s2,%0"
  [(set_attr "type" "shift")])

(define_insn "*inswl_const"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(ashift:DI (zero_extend:DI (match_operand:HI 1 "register_operand" "r"))
		   (match_operand:DI 2 "mul8_operand" "I")))]
  ""
  "inswl %1,%s2,%0"
  [(set_attr "type" "shift")])

(define_insn "*insll_const"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(ashift:DI (zero_extend:DI (match_operand:SI 1 "register_operand" "r"))
		   (match_operand:DI 2 "mul8_operand" "I")))]
  ""
  "insll %1,%s2,%0"
  [(set_attr "type" "shift")])

(define_insn "insbl_le"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(ashift:DI (zero_extend:DI (match_operand:QI 1 "register_operand" "r"))
		   (ashift:DI (match_operand:DI 2 "reg_or_8bit_operand" "rI")
			      (const_int 3))))]
  "! WORDS_BIG_ENDIAN"
  "insbl %1,%2,%0"
  [(set_attr "type" "shift")])

(define_insn "insbl_be"
 [(set (match_operand:DI 0 "register_operand" "=r")
       (ashift:DI (zero_extend:DI (match_operand:QI 1 "register_operand" "r"))
	 (minus:DI (const_int 56)
	   (ashift:DI (match_operand:DI 2 "reg_or_8bit_operand" "rI")
		      (const_int 3)))))]
  "WORDS_BIG_ENDIAN"
  "insbl %1,%2,%0"
  [(set_attr "type" "shift")])

(define_insn "inswl_le"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(ashift:DI (zero_extend:DI (match_operand:HI 1 "register_operand" "r"))
		   (ashift:DI (match_operand:DI 2 "reg_or_8bit_operand" "rI")
			      (const_int 3))))]
  "! WORDS_BIG_ENDIAN"
  "inswl %1,%2,%0"
  [(set_attr "type" "shift")])

(define_insn "inswl_be"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(ashift:DI (zero_extend:DI (match_operand:HI 1 "register_operand" "r"))
	  (minus:DI (const_int 56)
	    (ashift:DI (match_operand:DI 2 "reg_or_8bit_operand" "rI")
		       (const_int 3)))))]
  "WORDS_BIG_ENDIAN"
  "inswl %1,%2,%0"
  [(set_attr "type" "shift")])

(define_insn "insll_le"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(ashift:DI (zero_extend:DI (match_operand:SI 1 "register_operand" "r"))
		   (ashift:DI (match_operand:DI 2 "reg_or_8bit_operand" "rI")
			      (const_int 3))))]
  "! WORDS_BIG_ENDIAN"
  "insll %1,%2,%0"
  [(set_attr "type" "shift")])

(define_insn "insll_be"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(ashift:DI (zero_extend:DI (match_operand:SI 1 "register_operand" "r"))
	  (minus:DI (const_int 56)
	    (ashift:DI (match_operand:DI 2 "reg_or_8bit_operand" "rI")
		       (const_int 3)))))]
  "WORDS_BIG_ENDIAN"
  "insll %1,%2,%0"
  [(set_attr "type" "shift")])

(define_insn "insql_le"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(ashift:DI (match_operand:DI 1 "register_operand" "r")
		   (ashift:DI (match_operand:DI 2 "reg_or_8bit_operand" "rI")
			      (const_int 3))))]
  "! WORDS_BIG_ENDIAN"
  "insql %1,%2,%0"
  [(set_attr "type" "shift")])

(define_insn "insql_be"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(ashift:DI (match_operand:DI 1 "register_operand" "r")
	  (minus:DI (const_int 56)
	    (ashift:DI (match_operand:DI 2 "reg_or_8bit_operand" "rI")
		       (const_int 3)))))]
  "WORDS_BIG_ENDIAN"
  "insql %1,%2,%0"
  [(set_attr "type" "shift")])

;; Combine has this sometimes habit of moving the and outside of the
;; shift, making life more interesting.

(define_insn "*insxl"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(and:DI (ashift:DI (match_operand:DI 1 "register_operand" "r")
		   	   (match_operand:DI 2 "mul8_operand" "I"))
		(match_operand:DI 3 "immediate_operand" "i")))]
  "HOST_BITS_PER_WIDE_INT == 64
   && GET_CODE (operands[3]) == CONST_INT
   && (((unsigned HOST_WIDE_INT) 0xff << INTVAL (operands[2])
        == (unsigned HOST_WIDE_INT) INTVAL (operands[3]))
       || ((unsigned HOST_WIDE_INT) 0xffff << INTVAL (operands[2])
        == (unsigned HOST_WIDE_INT) INTVAL (operands[3]))
       || ((unsigned HOST_WIDE_INT) 0xffffffff << INTVAL (operands[2])
        == (unsigned HOST_WIDE_INT) INTVAL (operands[3])))"
{
#if HOST_BITS_PER_WIDE_INT == 64
  if ((unsigned HOST_WIDE_INT) 0xff << INTVAL (operands[2])
      == (unsigned HOST_WIDE_INT) INTVAL (operands[3]))
    return "insbl %1,%s2,%0";
  if ((unsigned HOST_WIDE_INT) 0xffff << INTVAL (operands[2])
      == (unsigned HOST_WIDE_INT) INTVAL (operands[3]))
    return "inswl %1,%s2,%0";
  if ((unsigned HOST_WIDE_INT) 0xffffffff << INTVAL (operands[2])
      == (unsigned HOST_WIDE_INT) INTVAL (operands[3]))
    return "insll %1,%s2,%0";
#endif
  gcc_unreachable ();
}
  [(set_attr "type" "shift")])

;; We do not include the insXh insns because they are complex to express
;; and it does not appear that we would ever want to generate them.
;;
;; Since we need them for block moves, though, cop out and use unspec.

(define_insn "insxh"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(unspec:DI [(match_operand:DI 1 "register_operand" "r")
		    (match_operand:DI 2 "mode_width_operand" "n")
		    (match_operand:DI 3 "reg_or_8bit_operand" "rI")]
		   UNSPEC_INSXH))]
  ""
  "ins%M2h %1,%3,%0"
  [(set_attr "type" "shift")])

(define_insn "mskxl_le"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(and:DI (not:DI (ashift:DI
			 (match_operand:DI 2 "mode_mask_operand" "n")
			 (ashift:DI
			  (match_operand:DI 3 "reg_or_8bit_operand" "rI")
			  (const_int 3))))
		(match_operand:DI 1 "reg_or_0_operand" "rJ")))]
  "! WORDS_BIG_ENDIAN"
  "msk%U2l %r1,%3,%0"
  [(set_attr "type" "shift")])

(define_insn "mskxl_be"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(and:DI (not:DI (ashift:DI
			  (match_operand:DI 2 "mode_mask_operand" "n")
			  (minus:DI (const_int 56)
			    (ashift:DI
			      (match_operand:DI 3 "reg_or_8bit_operand" "rI")
			      (const_int 3)))))
		(match_operand:DI 1 "reg_or_0_operand" "rJ")))]
  "WORDS_BIG_ENDIAN"
  "msk%U2l %r1,%3,%0"
  [(set_attr "type" "shift")])

;; We do not include the mskXh insns because it does not appear we would
;; ever generate one.
;;
;; Again, we do for block moves and we use unspec again.

(define_insn "mskxh"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(unspec:DI [(match_operand:DI 1 "register_operand" "r")
		    (match_operand:DI 2 "mode_width_operand" "n")
		    (match_operand:DI 3 "reg_or_8bit_operand" "rI")]
		   UNSPEC_MSKXH))]
  ""
  "msk%M2h %1,%3,%0"
  [(set_attr "type" "shift")])

;; Prefer AND + NE over LSHIFTRT + AND.

(define_insn_and_split "*ze_and_ne"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(zero_extract:DI (match_operand:DI 1 "reg_or_0_operand" "rJ")
			 (const_int 1)
			 (match_operand 2 "const_int_operand" "I")))]
  "(unsigned HOST_WIDE_INT) INTVAL (operands[2]) < 8"
  "#"
  "(unsigned HOST_WIDE_INT) INTVAL (operands[2]) < 8"
  [(set (match_dup 0)
	(and:DI (match_dup 1) (match_dup 3)))
   (set (match_dup 0)
	(ne:DI (match_dup 0) (const_int 0)))]
  "operands[3] = GEN_INT (1 << INTVAL (operands[2]));")

;; Floating-point operations.  All the double-precision insns can extend
;; from single, so indicate that.  The exception are the ones that simply
;; play with the sign bits; it's not clear what to do there.

(define_insn "abssf2"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(abs:SF (match_operand:SF 1 "reg_or_0_operand" "fG")))]
  "TARGET_FP"
  "cpys $f31,%R1,%0"
  [(set_attr "type" "fcpys")])

(define_insn "*nabssf2"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(neg:SF (abs:SF (match_operand:SF 1 "reg_or_0_operand" "fG"))))]
  "TARGET_FP"
  "cpysn $f31,%R1,%0"
  [(set_attr "type" "fadd")])

(define_insn "absdf2"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(abs:DF (match_operand:DF 1 "reg_or_0_operand" "fG")))]
  "TARGET_FP"
  "cpys $f31,%R1,%0"
  [(set_attr "type" "fcpys")])

(define_insn "*nabsdf2"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(neg:DF (abs:DF (match_operand:DF 1 "reg_or_0_operand" "fG"))))]
  "TARGET_FP"
  "cpysn $f31,%R1,%0"
  [(set_attr "type" "fadd")])

(define_expand "abstf2"
  [(parallel [(set (match_operand:TF 0 "register_operand" "")
		   (abs:TF (match_operand:TF 1 "reg_or_0_operand" "")))
	      (use (match_dup 2))])]
  "TARGET_HAS_XFLOATING_LIBS"
{
#if HOST_BITS_PER_WIDE_INT >= 64
  operands[2] = force_reg (DImode, GEN_INT ((HOST_WIDE_INT) 1 << 63));
#else
  operands[2] = force_reg (DImode, immed_double_const (0, 0x80000000, DImode));
#endif
})

(define_insn_and_split "*abstf_internal"
  [(set (match_operand:TF 0 "register_operand" "=r")
	(abs:TF (match_operand:TF 1 "reg_or_0_operand" "rG")))
   (use (match_operand:DI 2 "register_operand" "r"))]
  "TARGET_HAS_XFLOATING_LIBS"
  "#"
  "&& reload_completed"
  [(const_int 0)]
  "alpha_split_tfmode_frobsign (operands, gen_andnotdi3); DONE;")

(define_insn "negsf2"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(neg:SF (match_operand:SF 1 "reg_or_0_operand" "fG")))]
  "TARGET_FP"
  "cpysn %R1,%R1,%0"
  [(set_attr "type" "fadd")])

(define_insn "negdf2"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(neg:DF (match_operand:DF 1 "reg_or_0_operand" "fG")))]
  "TARGET_FP"
  "cpysn %R1,%R1,%0"
  [(set_attr "type" "fadd")])

(define_expand "negtf2"
  [(parallel [(set (match_operand:TF 0 "register_operand" "")
		   (neg:TF (match_operand:TF 1 "reg_or_0_operand" "")))
	      (use (match_dup 2))])]
  "TARGET_HAS_XFLOATING_LIBS"
{
#if HOST_BITS_PER_WIDE_INT >= 64
  operands[2] = force_reg (DImode, GEN_INT ((HOST_WIDE_INT) 1 << 63));
#else
  operands[2] = force_reg (DImode, immed_double_const (0, 0x80000000, DImode));
#endif
})

(define_insn_and_split "*negtf_internal"
  [(set (match_operand:TF 0 "register_operand" "=r")
	(neg:TF (match_operand:TF 1 "reg_or_0_operand" "rG")))
   (use (match_operand:DI 2 "register_operand" "r"))]
  "TARGET_HAS_XFLOATING_LIBS"
  "#"
  "&& reload_completed"
  [(const_int 0)]
  "alpha_split_tfmode_frobsign (operands, gen_xordi3); DONE;")

(define_insn "copysignsf3"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(unspec:SF [(match_operand:SF 1 "reg_or_0_operand" "fG")
		    (match_operand:SF 2 "reg_or_0_operand" "fG")]
		   UNSPEC_COPYSIGN))]
  "TARGET_FP"
  "cpys %R2,%R1,%0"
  [(set_attr "type" "fadd")])

(define_insn "*ncopysignsf3"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(neg:SF (unspec:SF [(match_operand:SF 1 "reg_or_0_operand" "fG")
			    (match_operand:SF 2 "reg_or_0_operand" "fG")]
			   UNSPEC_COPYSIGN)))]
  "TARGET_FP"
  "cpysn %R2,%R1,%0"
  [(set_attr "type" "fadd")])

(define_insn "copysigndf3"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(unspec:DF [(match_operand:DF 1 "reg_or_0_operand" "fG")
		    (match_operand:DF 2 "reg_or_0_operand" "fG")]
		   UNSPEC_COPYSIGN))]
  "TARGET_FP"
  "cpys %R2,%R1,%0"
  [(set_attr "type" "fadd")])

(define_insn "*ncopysigndf3"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(neg:DF (unspec:DF [(match_operand:DF 1 "reg_or_0_operand" "fG")
			    (match_operand:DF 2 "reg_or_0_operand" "fG")]
			   UNSPEC_COPYSIGN)))]
  "TARGET_FP"
  "cpysn %R2,%R1,%0"
  [(set_attr "type" "fadd")])

(define_insn "*addsf_ieee"
  [(set (match_operand:SF 0 "register_operand" "=&f")
	(plus:SF (match_operand:SF 1 "reg_or_0_operand" "%fG")
		 (match_operand:SF 2 "reg_or_0_operand" "fG")))]
  "TARGET_FP && alpha_fptm >= ALPHA_FPTM_SU"
  "add%,%/ %R1,%R2,%0"
  [(set_attr "type" "fadd")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "normal")
   (set_attr "trap_suffix" "u_su_sui")])

(define_insn "addsf3"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(plus:SF (match_operand:SF 1 "reg_or_0_operand" "%fG")
		 (match_operand:SF 2 "reg_or_0_operand" "fG")))]
  "TARGET_FP"
  "add%,%/ %R1,%R2,%0"
  [(set_attr "type" "fadd")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "normal")
   (set_attr "trap_suffix" "u_su_sui")])

(define_insn "*adddf_ieee"
  [(set (match_operand:DF 0 "register_operand" "=&f")
	(plus:DF (match_operand:DF 1 "reg_or_0_operand" "%fG")
		 (match_operand:DF 2 "reg_or_0_operand" "fG")))]
  "TARGET_FP && alpha_fptm >= ALPHA_FPTM_SU"
  "add%-%/ %R1,%R2,%0"
  [(set_attr "type" "fadd")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "normal")
   (set_attr "trap_suffix" "u_su_sui")])

(define_insn "adddf3"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(plus:DF (match_operand:DF 1 "reg_or_0_operand" "%fG")
		 (match_operand:DF 2 "reg_or_0_operand" "fG")))]
  "TARGET_FP"
  "add%-%/ %R1,%R2,%0"
  [(set_attr "type" "fadd")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "normal")
   (set_attr "trap_suffix" "u_su_sui")])

(define_insn "*adddf_ext1"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(plus:DF (float_extend:DF
		  (match_operand:SF 1 "reg_or_0_operand" "fG"))
		 (match_operand:DF 2 "reg_or_0_operand" "fG")))]
  "TARGET_FP && alpha_fptm < ALPHA_FPTM_SU"
  "add%-%/ %R1,%R2,%0"
  [(set_attr "type" "fadd")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "normal")
   (set_attr "trap_suffix" "u_su_sui")])

(define_insn "*adddf_ext2"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(plus:DF (float_extend:DF
		  (match_operand:SF 1 "reg_or_0_operand" "%fG"))
		 (float_extend:DF
		  (match_operand:SF 2 "reg_or_0_operand" "fG"))))]
  "TARGET_FP && alpha_fptm < ALPHA_FPTM_SU"
  "add%-%/ %R1,%R2,%0"
  [(set_attr "type" "fadd")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "normal")
   (set_attr "trap_suffix" "u_su_sui")])

(define_expand "addtf3"
  [(use (match_operand 0 "register_operand" ""))
   (use (match_operand 1 "general_operand" ""))
   (use (match_operand 2 "general_operand" ""))]
  "TARGET_HAS_XFLOATING_LIBS"
  "alpha_emit_xfloating_arith (PLUS, operands); DONE;")

;; Define conversion operators between DFmode and SImode, using the cvtql
;; instruction.  To allow combine et al to do useful things, we keep the
;; operation as a unit until after reload, at which point we split the
;; instructions.
;;
;; Note that we (attempt to) only consider this optimization when the
;; ultimate destination is memory.  If we will be doing further integer
;; processing, it is cheaper to do the truncation in the int regs.

(define_insn "*cvtql"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(unspec:SF [(match_operand:DI 1 "reg_or_0_operand" "fG")]
		   UNSPEC_CVTQL))]
  "TARGET_FP"
  "cvtql%/ %R1,%0"
  [(set_attr "type" "fadd")
   (set_attr "trap" "yes")
   (set_attr "trap_suffix" "v_sv")])

(define_insn_and_split "*fix_truncdfsi_ieee"
  [(set (match_operand:SI 0 "memory_operand" "=m")
	(subreg:SI
	  (match_operator:DI 4 "fix_operator" 
	    [(match_operand:DF 1 "reg_or_0_operand" "fG")]) 0))
   (clobber (match_scratch:DI 2 "=&f"))
   (clobber (match_scratch:SF 3 "=&f"))]
  "TARGET_FP && alpha_fptm >= ALPHA_FPTM_SU"
  "#"
  "&& reload_completed"
  [(set (match_dup 2) (match_op_dup 4 [(match_dup 1)]))
   (set (match_dup 3) (unspec:SF [(match_dup 2)] UNSPEC_CVTQL))
   (set (match_dup 5) (match_dup 3))]
{
  operands[5] = adjust_address (operands[0], SFmode, 0);
}
  [(set_attr "type" "fadd")
   (set_attr "trap" "yes")])

(define_insn_and_split "*fix_truncdfsi_internal"
  [(set (match_operand:SI 0 "memory_operand" "=m")
	(subreg:SI
	  (match_operator:DI 3 "fix_operator" 
	    [(match_operand:DF 1 "reg_or_0_operand" "fG")]) 0))
   (clobber (match_scratch:DI 2 "=f"))]
  "TARGET_FP && alpha_fptm < ALPHA_FPTM_SU"
  "#"
  "&& reload_completed"
  [(set (match_dup 2) (match_op_dup 3 [(match_dup 1)]))
   (set (match_dup 4) (unspec:SF [(match_dup 2)] UNSPEC_CVTQL))
   (set (match_dup 5) (match_dup 4))]
{
  operands[4] = gen_rtx_REG (SFmode, REGNO (operands[2]));
  operands[5] = adjust_address (operands[0], SFmode, 0);
}
  [(set_attr "type" "fadd")
   (set_attr "trap" "yes")])

(define_insn "*fix_truncdfdi_ieee"
  [(set (match_operand:DI 0 "reg_no_subreg_operand" "=&f")
	(match_operator:DI 2 "fix_operator" 
	  [(match_operand:DF 1 "reg_or_0_operand" "fG")]))]
  "TARGET_FP && alpha_fptm >= ALPHA_FPTM_SU"
  "cvt%-q%/ %R1,%0"
  [(set_attr "type" "fadd")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "c")
   (set_attr "trap_suffix" "v_sv_svi")])

(define_insn "*fix_truncdfdi2"
  [(set (match_operand:DI 0 "reg_no_subreg_operand" "=f")
	(match_operator:DI 2 "fix_operator" 
	  [(match_operand:DF 1 "reg_or_0_operand" "fG")]))]
  "TARGET_FP"
  "cvt%-q%/ %R1,%0"
  [(set_attr "type" "fadd")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "c")
   (set_attr "trap_suffix" "v_sv_svi")])

(define_expand "fix_truncdfdi2"
  [(set (match_operand:DI 0 "reg_no_subreg_operand" "")
	(fix:DI (match_operand:DF 1 "reg_or_0_operand" "")))]
  "TARGET_FP"
  "")

(define_expand "fixuns_truncdfdi2"
  [(set (match_operand:DI 0 "reg_no_subreg_operand" "")
	(unsigned_fix:DI (match_operand:DF 1 "reg_or_0_operand" "")))]
  "TARGET_FP"
  "")

;; Likewise between SFmode and SImode.

(define_insn_and_split "*fix_truncsfsi_ieee"
  [(set (match_operand:SI 0 "memory_operand" "=m")
	(subreg:SI
	  (match_operator:DI 4 "fix_operator" 
	    [(float_extend:DF
	       (match_operand:SF 1 "reg_or_0_operand" "fG"))]) 0))
   (clobber (match_scratch:DI 2 "=&f"))
   (clobber (match_scratch:SF 3 "=&f"))]
  "TARGET_FP && alpha_fptm >= ALPHA_FPTM_SU"
  "#"
  "&& reload_completed"
  [(set (match_dup 2) (match_op_dup 4 [(float_extend:DF (match_dup 1))]))
   (set (match_dup 3) (unspec:SF [(match_dup 2)] UNSPEC_CVTQL))
   (set (match_dup 5) (match_dup 3))]
{
  operands[5] = adjust_address (operands[0], SFmode, 0);
}
  [(set_attr "type" "fadd")
   (set_attr "trap" "yes")])

(define_insn_and_split "*fix_truncsfsi_internal"
  [(set (match_operand:SI 0 "memory_operand" "=m")
	(subreg:SI
	  (match_operator:DI 3 "fix_operator" 
	    [(float_extend:DF
	       (match_operand:SF 1 "reg_or_0_operand" "fG"))]) 0))
   (clobber (match_scratch:DI 2 "=f"))]
  "TARGET_FP && alpha_fptm < ALPHA_FPTM_SU"
  "#"
  "&& reload_completed"
  [(set (match_dup 2) (match_op_dup 3 [(float_extend:DF (match_dup 1))]))
   (set (match_dup 4) (unspec:SF [(match_dup 2)] UNSPEC_CVTQL))
   (set (match_dup 5) (match_dup 4))]
{
  operands[4] = gen_rtx_REG (SFmode, REGNO (operands[2]));
  operands[5] = adjust_address (operands[0], SFmode, 0);
}
  [(set_attr "type" "fadd")
   (set_attr "trap" "yes")])

(define_insn "*fix_truncsfdi_ieee"
  [(set (match_operand:DI 0 "reg_no_subreg_operand" "=&f")
	(match_operator:DI 2 "fix_operator" 
	  [(float_extend:DF (match_operand:SF 1 "reg_or_0_operand" "fG"))]))]
  "TARGET_FP && alpha_fptm >= ALPHA_FPTM_SU"
  "cvt%-q%/ %R1,%0"
  [(set_attr "type" "fadd")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "c")
   (set_attr "trap_suffix" "v_sv_svi")])

(define_insn "*fix_truncsfdi2"
  [(set (match_operand:DI 0 "reg_no_subreg_operand" "=f")
	(match_operator:DI 2 "fix_operator" 
	  [(float_extend:DF (match_operand:SF 1 "reg_or_0_operand" "fG"))]))]
  "TARGET_FP"
  "cvt%-q%/ %R1,%0"
  [(set_attr "type" "fadd")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "c")
   (set_attr "trap_suffix" "v_sv_svi")])

(define_expand "fix_truncsfdi2"
  [(set (match_operand:DI 0 "reg_no_subreg_operand" "")
	(fix:DI (float_extend:DF (match_operand:SF 1 "reg_or_0_operand" ""))))]
  "TARGET_FP"
  "")

(define_expand "fixuns_truncsfdi2"
  [(set (match_operand:DI 0 "reg_no_subreg_operand" "")
	(unsigned_fix:DI
	  (float_extend:DF (match_operand:SF 1 "reg_or_0_operand" ""))))]
  "TARGET_FP"
  "")

(define_expand "fix_trunctfdi2"
  [(use (match_operand:DI 0 "register_operand" ""))
   (use (match_operand:TF 1 "general_operand" ""))]
  "TARGET_HAS_XFLOATING_LIBS"
  "alpha_emit_xfloating_cvt (FIX, operands); DONE;")

(define_expand "fixuns_trunctfdi2"
  [(use (match_operand:DI 0 "register_operand" ""))
   (use (match_operand:TF 1 "general_operand" ""))]
  "TARGET_HAS_XFLOATING_LIBS"
  "alpha_emit_xfloating_cvt (UNSIGNED_FIX, operands); DONE;")

(define_insn "*floatdisf_ieee"
  [(set (match_operand:SF 0 "register_operand" "=&f")
	(float:SF (match_operand:DI 1 "reg_no_subreg_operand" "f")))]
  "TARGET_FP && alpha_fptm >= ALPHA_FPTM_SU"
  "cvtq%,%/ %1,%0"
  [(set_attr "type" "fadd")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "normal")
   (set_attr "trap_suffix" "sui")])

(define_insn "floatdisf2"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(float:SF (match_operand:DI 1 "reg_no_subreg_operand" "f")))]
  "TARGET_FP"
  "cvtq%,%/ %1,%0"
  [(set_attr "type" "fadd")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "normal")
   (set_attr "trap_suffix" "sui")])

(define_insn_and_split "*floatsisf2_ieee"
  [(set (match_operand:SF 0 "register_operand" "=&f")
	(float:SF (match_operand:SI 1 "memory_operand" "m")))
   (clobber (match_scratch:DI 2 "=&f"))
   (clobber (match_scratch:SF 3 "=&f"))]
  "TARGET_FP && alpha_fptm >= ALPHA_FPTM_SU"
  "#"
  "&& reload_completed"
  [(set (match_dup 3) (match_dup 1))
   (set (match_dup 2) (unspec:DI [(match_dup 3)] UNSPEC_CVTLQ))
   (set (match_dup 0) (float:SF (match_dup 2)))]
{
  operands[1] = adjust_address (operands[1], SFmode, 0);
})

(define_insn_and_split "*floatsisf2"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(float:SF (match_operand:SI 1 "memory_operand" "m")))]
  "TARGET_FP"
  "#"
  "&& reload_completed"
  [(set (match_dup 0) (match_dup 1))
   (set (match_dup 2) (unspec:DI [(match_dup 0)] UNSPEC_CVTLQ))
   (set (match_dup 0) (float:SF (match_dup 2)))]
{
  operands[1] = adjust_address (operands[1], SFmode, 0);
  operands[2] = gen_rtx_REG (DImode, REGNO (operands[0]));
})

(define_insn "*floatdidf_ieee"
  [(set (match_operand:DF 0 "register_operand" "=&f")
	(float:DF (match_operand:DI 1 "reg_no_subreg_operand" "f")))]
  "TARGET_FP && alpha_fptm >= ALPHA_FPTM_SU"
  "cvtq%-%/ %1,%0"
  [(set_attr "type" "fadd")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "normal")
   (set_attr "trap_suffix" "sui")])

(define_insn "floatdidf2"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(float:DF (match_operand:DI 1 "reg_no_subreg_operand" "f")))]
  "TARGET_FP"
  "cvtq%-%/ %1,%0"
  [(set_attr "type" "fadd")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "normal")
   (set_attr "trap_suffix" "sui")])

(define_insn_and_split "*floatsidf2_ieee"
  [(set (match_operand:DF 0 "register_operand" "=&f")
	(float:DF (match_operand:SI 1 "memory_operand" "m")))
   (clobber (match_scratch:DI 2 "=&f"))
   (clobber (match_scratch:SF 3 "=&f"))]
  "TARGET_FP && alpha_fptm >= ALPHA_FPTM_SU"
  "#"
  "&& reload_completed"
  [(set (match_dup 3) (match_dup 1))
   (set (match_dup 2) (unspec:DI [(match_dup 3)] UNSPEC_CVTLQ))
   (set (match_dup 0) (float:DF (match_dup 2)))]
{
  operands[1] = adjust_address (operands[1], SFmode, 0);
})

(define_insn_and_split "*floatsidf2"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(float:DF (match_operand:SI 1 "memory_operand" "m")))]
  "TARGET_FP"
  "#"
  "&& reload_completed"
  [(set (match_dup 3) (match_dup 1))
   (set (match_dup 2) (unspec:DI [(match_dup 3)] UNSPEC_CVTLQ))
   (set (match_dup 0) (float:DF (match_dup 2)))]
{
  operands[1] = adjust_address (operands[1], SFmode, 0);
  operands[2] = gen_rtx_REG (DImode, REGNO (operands[0]));
  operands[3] = gen_rtx_REG (SFmode, REGNO (operands[0]));
})

(define_expand "floatditf2"
  [(use (match_operand:TF 0 "register_operand" ""))
   (use (match_operand:DI 1 "general_operand" ""))]
  "TARGET_HAS_XFLOATING_LIBS"
  "alpha_emit_xfloating_cvt (FLOAT, operands); DONE;")

(define_expand "floatunsdisf2"
  [(use (match_operand:SF 0 "register_operand" ""))
   (use (match_operand:DI 1 "register_operand" ""))]
  "TARGET_FP"
  "alpha_emit_floatuns (operands); DONE;")

(define_expand "floatunsdidf2"
  [(use (match_operand:DF 0 "register_operand" ""))
   (use (match_operand:DI 1 "register_operand" ""))]
  "TARGET_FP"
  "alpha_emit_floatuns (operands); DONE;")

(define_expand "floatunsditf2"
  [(use (match_operand:TF 0 "register_operand" ""))
   (use (match_operand:DI 1 "general_operand" ""))]
  "TARGET_HAS_XFLOATING_LIBS"
  "alpha_emit_xfloating_cvt (UNSIGNED_FLOAT, operands); DONE;")

(define_expand "extendsfdf2"
  [(set (match_operand:DF 0 "register_operand" "")
	(float_extend:DF (match_operand:SF 1 "nonimmediate_operand" "")))]
  "TARGET_FP"
{
  if (alpha_fptm >= ALPHA_FPTM_SU)
    operands[1] = force_reg (SFmode, operands[1]);
})

;; The Unicos/Mk assembler doesn't support cvtst, but we've already
;; asserted that alpha_fptm == ALPHA_FPTM_N.

(define_insn "*extendsfdf2_ieee"
  [(set (match_operand:DF 0 "register_operand" "=&f")
	(float_extend:DF (match_operand:SF 1 "register_operand" "f")))]
  "TARGET_FP && alpha_fptm >= ALPHA_FPTM_SU"
  "cvtsts %1,%0"
  [(set_attr "type" "fadd")
   (set_attr "trap" "yes")])

(define_insn "*extendsfdf2_internal"
  [(set (match_operand:DF 0 "register_operand" "=f,f,m")
	(float_extend:DF (match_operand:SF 1 "nonimmediate_operand" "f,m,f")))]
  "TARGET_FP && alpha_fptm < ALPHA_FPTM_SU"
  "@
   cpys %1,%1,%0
   ld%, %0,%1
   st%- %1,%0"
  [(set_attr "type" "fcpys,fld,fst")])

;; Use register_operand for operand 1 to prevent compress_float_constant
;; from doing something silly.  When optimizing we'll put things back 
;; together anyway.
(define_expand "extendsftf2"
  [(use (match_operand:TF 0 "register_operand" ""))
   (use (match_operand:SF 1 "register_operand" ""))]
  "TARGET_HAS_XFLOATING_LIBS"
{
  rtx tmp = gen_reg_rtx (DFmode);
  emit_insn (gen_extendsfdf2 (tmp, operands[1]));
  emit_insn (gen_extenddftf2 (operands[0], tmp));
  DONE;
})

(define_expand "extenddftf2"
  [(use (match_operand:TF 0 "register_operand" ""))
   (use (match_operand:DF 1 "register_operand" ""))]
  "TARGET_HAS_XFLOATING_LIBS"
  "alpha_emit_xfloating_cvt (FLOAT_EXTEND, operands); DONE;")

(define_insn "*truncdfsf2_ieee"
  [(set (match_operand:SF 0 "register_operand" "=&f")
	(float_truncate:SF (match_operand:DF 1 "reg_or_0_operand" "fG")))]
  "TARGET_FP && alpha_fptm >= ALPHA_FPTM_SU"
  "cvt%-%,%/ %R1,%0"
  [(set_attr "type" "fadd")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "normal")
   (set_attr "trap_suffix" "u_su_sui")])

(define_insn "truncdfsf2"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(float_truncate:SF (match_operand:DF 1 "reg_or_0_operand" "fG")))]
  "TARGET_FP"
  "cvt%-%,%/ %R1,%0"
  [(set_attr "type" "fadd")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "normal")
   (set_attr "trap_suffix" "u_su_sui")])

(define_expand "trunctfdf2"
  [(use (match_operand:DF 0 "register_operand" ""))
   (use (match_operand:TF 1 "general_operand" ""))]
  "TARGET_HAS_XFLOATING_LIBS"
  "alpha_emit_xfloating_cvt (FLOAT_TRUNCATE, operands); DONE;")

(define_expand "trunctfsf2"
  [(use (match_operand:SF 0 "register_operand" ""))
   (use (match_operand:TF 1 "general_operand" ""))]
  "TARGET_FP && TARGET_HAS_XFLOATING_LIBS"
{
  rtx tmpf, sticky, arg, lo, hi;

  tmpf = gen_reg_rtx (DFmode);
  sticky = gen_reg_rtx (DImode);
  arg = copy_to_mode_reg (TFmode, operands[1]);
  lo = gen_lowpart (DImode, arg);
  hi = gen_highpart (DImode, arg);

  /* Convert the low word of the TFmode value into a sticky rounding bit,
     then or it into the low bit of the high word.  This leaves the sticky
     bit at bit 48 of the fraction, which is representable in DFmode,
     which prevents rounding error in the final conversion to SFmode.  */

  emit_insn (gen_rtx_SET (VOIDmode, sticky,
			  gen_rtx_NE (DImode, lo, const0_rtx)));
  emit_insn (gen_iordi3 (hi, hi, sticky));
  emit_insn (gen_trunctfdf2 (tmpf, arg));
  emit_insn (gen_truncdfsf2 (operands[0], tmpf));
  DONE;
})

(define_insn "*divsf3_ieee"
  [(set (match_operand:SF 0 "register_operand" "=&f")
	(div:SF (match_operand:SF 1 "reg_or_0_operand" "fG")
		(match_operand:SF 2 "reg_or_0_operand" "fG")))]
  "TARGET_FP && alpha_fptm >= ALPHA_FPTM_SU"
  "div%,%/ %R1,%R2,%0"
  [(set_attr "type" "fdiv")
   (set_attr "opsize" "si")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "normal")
   (set_attr "trap_suffix" "u_su_sui")])

(define_insn "divsf3"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(div:SF (match_operand:SF 1 "reg_or_0_operand" "fG")
		(match_operand:SF 2 "reg_or_0_operand" "fG")))]
  "TARGET_FP"
  "div%,%/ %R1,%R2,%0"
  [(set_attr "type" "fdiv")
   (set_attr "opsize" "si")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "normal")
   (set_attr "trap_suffix" "u_su_sui")])

(define_insn "*divdf3_ieee"
  [(set (match_operand:DF 0 "register_operand" "=&f")
	(div:DF (match_operand:DF 1 "reg_or_0_operand" "fG")
		(match_operand:DF 2 "reg_or_0_operand" "fG")))]
  "TARGET_FP && alpha_fptm >= ALPHA_FPTM_SU"
  "div%-%/ %R1,%R2,%0"
  [(set_attr "type" "fdiv")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "normal")
   (set_attr "trap_suffix" "u_su_sui")])

(define_insn "divdf3"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(div:DF (match_operand:DF 1 "reg_or_0_operand" "fG")
		(match_operand:DF 2 "reg_or_0_operand" "fG")))]
  "TARGET_FP"
  "div%-%/ %R1,%R2,%0"
  [(set_attr "type" "fdiv")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "normal")
   (set_attr "trap_suffix" "u_su_sui")])

(define_insn "*divdf_ext1"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(div:DF (float_extend:DF (match_operand:SF 1 "reg_or_0_operand" "fG"))
		(match_operand:DF 2 "reg_or_0_operand" "fG")))]
  "TARGET_FP && alpha_fptm < ALPHA_FPTM_SU"
  "div%-%/ %R1,%R2,%0"
  [(set_attr "type" "fdiv")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "normal")
   (set_attr "trap_suffix" "u_su_sui")])

(define_insn "*divdf_ext2"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(div:DF (match_operand:DF 1 "reg_or_0_operand" "fG")
		(float_extend:DF
		 (match_operand:SF 2 "reg_or_0_operand" "fG"))))]
  "TARGET_FP && alpha_fptm < ALPHA_FPTM_SU"
  "div%-%/ %R1,%R2,%0"
  [(set_attr "type" "fdiv")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "normal")
   (set_attr "trap_suffix" "u_su_sui")])

(define_insn "*divdf_ext3"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(div:DF (float_extend:DF (match_operand:SF 1 "reg_or_0_operand" "fG"))
		(float_extend:DF (match_operand:SF 2 "reg_or_0_operand" "fG"))))]
  "TARGET_FP && alpha_fptm < ALPHA_FPTM_SU"
  "div%-%/ %R1,%R2,%0"
  [(set_attr "type" "fdiv")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "normal")
   (set_attr "trap_suffix" "u_su_sui")])

(define_expand "divtf3"
  [(use (match_operand 0 "register_operand" ""))
   (use (match_operand 1 "general_operand" ""))
   (use (match_operand 2 "general_operand" ""))]
  "TARGET_HAS_XFLOATING_LIBS"
  "alpha_emit_xfloating_arith (DIV, operands); DONE;")

(define_insn "*mulsf3_ieee"
  [(set (match_operand:SF 0 "register_operand" "=&f")
	(mult:SF (match_operand:SF 1 "reg_or_0_operand" "%fG")
		 (match_operand:SF 2 "reg_or_0_operand" "fG")))]
  "TARGET_FP && alpha_fptm >= ALPHA_FPTM_SU"
  "mul%,%/ %R1,%R2,%0"
  [(set_attr "type" "fmul")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "normal")
   (set_attr "trap_suffix" "u_su_sui")])

(define_insn "mulsf3"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(mult:SF (match_operand:SF 1 "reg_or_0_operand" "%fG")
		 (match_operand:SF 2 "reg_or_0_operand" "fG")))]
  "TARGET_FP"
  "mul%,%/ %R1,%R2,%0"
  [(set_attr "type" "fmul")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "normal")
   (set_attr "trap_suffix" "u_su_sui")])

(define_insn "*muldf3_ieee"
  [(set (match_operand:DF 0 "register_operand" "=&f")
	(mult:DF (match_operand:DF 1 "reg_or_0_operand" "%fG")
		 (match_operand:DF 2 "reg_or_0_operand" "fG")))]
  "TARGET_FP && alpha_fptm >= ALPHA_FPTM_SU"
  "mul%-%/ %R1,%R2,%0"
  [(set_attr "type" "fmul")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "normal")
   (set_attr "trap_suffix" "u_su_sui")])

(define_insn "muldf3"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(mult:DF (match_operand:DF 1 "reg_or_0_operand" "%fG")
		 (match_operand:DF 2 "reg_or_0_operand" "fG")))]
  "TARGET_FP"
  "mul%-%/ %R1,%R2,%0"
  [(set_attr "type" "fmul")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "normal")
   (set_attr "trap_suffix" "u_su_sui")])

(define_insn "*muldf_ext1"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(mult:DF (float_extend:DF
		  (match_operand:SF 1 "reg_or_0_operand" "fG"))
		 (match_operand:DF 2 "reg_or_0_operand" "fG")))]
  "TARGET_FP && alpha_fptm < ALPHA_FPTM_SU"
  "mul%-%/ %R1,%R2,%0"
  [(set_attr "type" "fmul")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "normal")
   (set_attr "trap_suffix" "u_su_sui")])

(define_insn "*muldf_ext2"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(mult:DF (float_extend:DF
		  (match_operand:SF 1 "reg_or_0_operand" "%fG"))
		 (float_extend:DF
		  (match_operand:SF 2 "reg_or_0_operand" "fG"))))]
  "TARGET_FP && alpha_fptm < ALPHA_FPTM_SU"
  "mul%-%/ %R1,%R2,%0"
  [(set_attr "type" "fmul")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "normal")
   (set_attr "trap_suffix" "u_su_sui")])

(define_expand "multf3"
  [(use (match_operand 0 "register_operand" ""))
   (use (match_operand 1 "general_operand" ""))
   (use (match_operand 2 "general_operand" ""))]
  "TARGET_HAS_XFLOATING_LIBS"
  "alpha_emit_xfloating_arith (MULT, operands); DONE;")

(define_insn "*subsf3_ieee"
  [(set (match_operand:SF 0 "register_operand" "=&f")
	(minus:SF (match_operand:SF 1 "reg_or_0_operand" "fG")
		  (match_operand:SF 2 "reg_or_0_operand" "fG")))]
  "TARGET_FP && alpha_fptm >= ALPHA_FPTM_SU"
  "sub%,%/ %R1,%R2,%0"
  [(set_attr "type" "fadd")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "normal")
   (set_attr "trap_suffix" "u_su_sui")])

(define_insn "subsf3"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(minus:SF (match_operand:SF 1 "reg_or_0_operand" "fG")
		  (match_operand:SF 2 "reg_or_0_operand" "fG")))]
  "TARGET_FP"
  "sub%,%/ %R1,%R2,%0"
  [(set_attr "type" "fadd")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "normal")
   (set_attr "trap_suffix" "u_su_sui")])

(define_insn "*subdf3_ieee"
  [(set (match_operand:DF 0 "register_operand" "=&f")
	(minus:DF (match_operand:DF 1 "reg_or_0_operand" "fG")
		  (match_operand:DF 2 "reg_or_0_operand" "fG")))]
  "TARGET_FP && alpha_fptm >= ALPHA_FPTM_SU"
  "sub%-%/ %R1,%R2,%0"
  [(set_attr "type" "fadd")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "normal")
   (set_attr "trap_suffix" "u_su_sui")])

(define_insn "subdf3"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(minus:DF (match_operand:DF 1 "reg_or_0_operand" "fG")
		  (match_operand:DF 2 "reg_or_0_operand" "fG")))]
  "TARGET_FP"
  "sub%-%/ %R1,%R2,%0"
  [(set_attr "type" "fadd")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "normal")
   (set_attr "trap_suffix" "u_su_sui")])

(define_insn "*subdf_ext1"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(minus:DF (float_extend:DF
		   (match_operand:SF 1 "reg_or_0_operand" "fG"))
		  (match_operand:DF 2 "reg_or_0_operand" "fG")))]
  "TARGET_FP && alpha_fptm < ALPHA_FPTM_SU"
  "sub%-%/ %R1,%R2,%0"
  [(set_attr "type" "fadd")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "normal")
   (set_attr "trap_suffix" "u_su_sui")])

(define_insn "*subdf_ext2"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(minus:DF (match_operand:DF 1 "reg_or_0_operand" "fG")
		  (float_extend:DF
		   (match_operand:SF 2 "reg_or_0_operand" "fG"))))]
  "TARGET_FP && alpha_fptm < ALPHA_FPTM_SU"
  "sub%-%/ %R1,%R2,%0"
  [(set_attr "type" "fadd")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "normal")
   (set_attr "trap_suffix" "u_su_sui")])

(define_insn "*subdf_ext3"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(minus:DF (float_extend:DF
		   (match_operand:SF 1 "reg_or_0_operand" "fG"))
		  (float_extend:DF
		   (match_operand:SF 2 "reg_or_0_operand" "fG"))))]
  "TARGET_FP && alpha_fptm < ALPHA_FPTM_SU"
  "sub%-%/ %R1,%R2,%0"
  [(set_attr "type" "fadd")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "normal")
   (set_attr "trap_suffix" "u_su_sui")])

(define_expand "subtf3"
  [(use (match_operand 0 "register_operand" ""))
   (use (match_operand 1 "general_operand" ""))
   (use (match_operand 2 "general_operand" ""))]
  "TARGET_HAS_XFLOATING_LIBS"
  "alpha_emit_xfloating_arith (MINUS, operands); DONE;")

(define_insn "*sqrtsf2_ieee"
  [(set (match_operand:SF 0 "register_operand" "=&f")
	(sqrt:SF (match_operand:SF 1 "reg_or_0_operand" "fG")))]
  "TARGET_FP && TARGET_FIX && alpha_fptm >= ALPHA_FPTM_SU"
  "sqrt%,%/ %R1,%0"
  [(set_attr "type" "fsqrt")
   (set_attr "opsize" "si")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "normal")
   (set_attr "trap_suffix" "u_su_sui")])

(define_insn "sqrtsf2"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(sqrt:SF (match_operand:SF 1 "reg_or_0_operand" "fG")))]
  "TARGET_FP && TARGET_FIX"
  "sqrt%,%/ %R1,%0"
  [(set_attr "type" "fsqrt")
   (set_attr "opsize" "si")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "normal")
   (set_attr "trap_suffix" "u_su_sui")])

(define_insn "*sqrtdf2_ieee"
  [(set (match_operand:DF 0 "register_operand" "=&f")
	(sqrt:DF (match_operand:DF 1 "reg_or_0_operand" "fG")))]
  "TARGET_FP && TARGET_FIX && alpha_fptm >= ALPHA_FPTM_SU"
  "sqrt%-%/ %R1,%0"
  [(set_attr "type" "fsqrt")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "normal")
   (set_attr "trap_suffix" "u_su_sui")])

(define_insn "sqrtdf2"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(sqrt:DF (match_operand:DF 1 "reg_or_0_operand" "fG")))]
  "TARGET_FP && TARGET_FIX"
  "sqrt%-%/ %R1,%0"
  [(set_attr "type" "fsqrt")
   (set_attr "trap" "yes")
   (set_attr "round_suffix" "normal")
   (set_attr "trap_suffix" "u_su_sui")])

;; Next are all the integer comparisons, and conditional moves and branches
;; and some of the related define_expand's and define_split's.

(define_insn "*setcc_internal"
  [(set (match_operand 0 "register_operand" "=r")
	(match_operator 1 "alpha_comparison_operator"
			   [(match_operand:DI 2 "register_operand" "r")
			    (match_operand:DI 3 "reg_or_8bit_operand" "rI")]))]
  "GET_MODE_CLASS (GET_MODE (operands[0])) == MODE_INT
   && GET_MODE_SIZE (GET_MODE (operands[0])) <= 8
   && GET_MODE (operands[0]) == GET_MODE (operands[1])"
  "cmp%C1 %2,%3,%0"
  [(set_attr "type" "icmp")])

;; Yes, we can technically support reg_or_8bit_operand in operand 2,
;; but that's non-canonical rtl and allowing that causes inefficiencies
;; from cse on.
(define_insn "*setcc_swapped_internal"
  [(set (match_operand 0 "register_operand" "=r")
        (match_operator 1 "alpha_swapped_comparison_operator"
			   [(match_operand:DI 2 "register_operand" "r")
			    (match_operand:DI 3 "reg_or_0_operand" "rJ")]))]
  "GET_MODE_CLASS (GET_MODE (operands[0])) == MODE_INT
   && GET_MODE_SIZE (GET_MODE (operands[0])) <= 8
   && GET_MODE (operands[0]) == GET_MODE (operands[1])"
  "cmp%c1 %r3,%2,%0"
  [(set_attr "type" "icmp")])

;; Use match_operator rather than ne directly so that we can match
;; multiple integer modes.
(define_insn "*setne_internal"
  [(set (match_operand 0 "register_operand" "=r")
	(match_operator 1 "signed_comparison_operator"
			  [(match_operand:DI 2 "register_operand" "r")
			   (const_int 0)]))]
  "GET_MODE_CLASS (GET_MODE (operands[0])) == MODE_INT
   && GET_MODE_SIZE (GET_MODE (operands[0])) <= 8
   && GET_CODE (operands[1]) == NE
   && GET_MODE (operands[0]) == GET_MODE (operands[1])"
  "cmpult $31,%2,%0"
  [(set_attr "type" "icmp")])

;; The mode folding trick can't be used with const_int operands, since
;; reload needs to know the proper mode.
;;
;; Use add_operand instead of the more seemingly natural reg_or_8bit_operand
;; in order to create more pairs of constants.  As long as we're allowing
;; two constants at the same time, and will have to reload one of them...

(define_insn "*movqicc_internal"
  [(set (match_operand:QI 0 "register_operand" "=r,r,r,r")
	(if_then_else:QI
	 (match_operator 2 "signed_comparison_operator"
			 [(match_operand:DI 3 "reg_or_0_operand" "rJ,rJ,J,J")
			  (match_operand:DI 4 "reg_or_0_operand" "J,J,rJ,rJ")])
	 (match_operand:QI 1 "add_operand" "rI,0,rI,0")
	 (match_operand:QI 5 "add_operand" "0,rI,0,rI")))]
  "(operands[3] == const0_rtx) ^ (operands[4] == const0_rtx)"
  "@
   cmov%C2 %r3,%1,%0
   cmov%D2 %r3,%5,%0
   cmov%c2 %r4,%1,%0
   cmov%d2 %r4,%5,%0"
  [(set_attr "type" "icmov")])

(define_insn "*movhicc_internal"
  [(set (match_operand:HI 0 "register_operand" "=r,r,r,r")
	(if_then_else:HI
	 (match_operator 2 "signed_comparison_operator"
			 [(match_operand:DI 3 "reg_or_0_operand" "rJ,rJ,J,J")
			  (match_operand:DI 4 "reg_or_0_operand" "J,J,rJ,rJ")])
	 (match_operand:HI 1 "add_operand" "rI,0,rI,0")
	 (match_operand:HI 5 "add_operand" "0,rI,0,rI")))]
  "(operands[3] == const0_rtx) ^ (operands[4] == const0_rtx)"
  "@
   cmov%C2 %r3,%1,%0
   cmov%D2 %r3,%5,%0
   cmov%c2 %r4,%1,%0
   cmov%d2 %r4,%5,%0"
  [(set_attr "type" "icmov")])

(define_insn "*movsicc_internal"
  [(set (match_operand:SI 0 "register_operand" "=r,r,r,r")
	(if_then_else:SI
	 (match_operator 2 "signed_comparison_operator"
			 [(match_operand:DI 3 "reg_or_0_operand" "rJ,rJ,J,J")
			  (match_operand:DI 4 "reg_or_0_operand" "J,J,rJ,rJ")])
	 (match_operand:SI 1 "add_operand" "rI,0,rI,0")
	 (match_operand:SI 5 "add_operand" "0,rI,0,rI")))]
  "(operands[3] == const0_rtx) ^ (operands[4] == const0_rtx)"
  "@
   cmov%C2 %r3,%1,%0
   cmov%D2 %r3,%5,%0
   cmov%c2 %r4,%1,%0
   cmov%d2 %r4,%5,%0"
  [(set_attr "type" "icmov")])

(define_insn "*movdicc_internal"
  [(set (match_operand:DI 0 "register_operand" "=r,r,r,r")
	(if_then_else:DI
	 (match_operator 2 "signed_comparison_operator"
			 [(match_operand:DI 3 "reg_or_0_operand" "rJ,rJ,J,J")
			  (match_operand:DI 4 "reg_or_0_operand" "J,J,rJ,rJ")])
	 (match_operand:DI 1 "add_operand" "rI,0,rI,0")
	 (match_operand:DI 5 "add_operand" "0,rI,0,rI")))]
  "(operands[3] == const0_rtx) ^ (operands[4] == const0_rtx)"
  "@
   cmov%C2 %r3,%1,%0
   cmov%D2 %r3,%5,%0
   cmov%c2 %r4,%1,%0
   cmov%d2 %r4,%5,%0"
  [(set_attr "type" "icmov")])

(define_insn "*movqicc_lbc"
  [(set (match_operand:QI 0 "register_operand" "=r,r")
	(if_then_else:QI
	 (eq (zero_extract:DI (match_operand:DI 2 "reg_or_0_operand" "rJ,rJ")
			      (const_int 1)
			      (const_int 0))
	     (const_int 0))
	 (match_operand:QI 1 "reg_or_8bit_operand" "rI,0")
	 (match_operand:QI 3 "reg_or_8bit_operand" "0,rI")))]
  ""
  "@
   cmovlbc %r2,%1,%0
   cmovlbs %r2,%3,%0"
  [(set_attr "type" "icmov")])

(define_insn "*movhicc_lbc"
  [(set (match_operand:HI 0 "register_operand" "=r,r")
	(if_then_else:HI
	 (eq (zero_extract:DI (match_operand:DI 2 "reg_or_0_operand" "rJ,rJ")
			      (const_int 1)
			      (const_int 0))
	     (const_int 0))
	 (match_operand:HI 1 "reg_or_8bit_operand" "rI,0")
	 (match_operand:HI 3 "reg_or_8bit_operand" "0,rI")))]
  ""
  "@
   cmovlbc %r2,%1,%0
   cmovlbs %r2,%3,%0"
  [(set_attr "type" "icmov")])

(define_insn "*movsicc_lbc"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(if_then_else:SI
	 (eq (zero_extract:DI (match_operand:DI 2 "reg_or_0_operand" "rJ,rJ")
			      (const_int 1)
			      (const_int 0))
	     (const_int 0))
	 (match_operand:SI 1 "reg_or_8bit_operand" "rI,0")
	 (match_operand:SI 3 "reg_or_8bit_operand" "0,rI")))]
  ""
  "@
   cmovlbc %r2,%1,%0
   cmovlbs %r2,%3,%0"
  [(set_attr "type" "icmov")])

(define_insn "*movdicc_lbc"
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(if_then_else:DI
	 (eq (zero_extract:DI (match_operand:DI 2 "reg_or_0_operand" "rJ,rJ")
			      (const_int 1)
			      (const_int 0))
	     (const_int 0))
	 (match_operand:DI 1 "reg_or_8bit_operand" "rI,0")
	 (match_operand:DI 3 "reg_or_8bit_operand" "0,rI")))]
  ""
  "@
   cmovlbc %r2,%1,%0
   cmovlbs %r2,%3,%0"
  [(set_attr "type" "icmov")])

(define_insn "*movqicc_lbs"
  [(set (match_operand:QI 0 "register_operand" "=r,r")
	(if_then_else:QI
	 (ne (zero_extract:DI (match_operand:DI 2 "reg_or_0_operand" "rJ,rJ")
			      (const_int 1)
			      (const_int 0))
	     (const_int 0))
	 (match_operand:QI 1 "reg_or_8bit_operand" "rI,0")
	 (match_operand:QI 3 "reg_or_8bit_operand" "0,rI")))]
  ""
  "@
   cmovlbs %r2,%1,%0
   cmovlbc %r2,%3,%0"
  [(set_attr "type" "icmov")])

(define_insn "*movhicc_lbs"
  [(set (match_operand:HI 0 "register_operand" "=r,r")
	(if_then_else:HI
	 (ne (zero_extract:DI (match_operand:DI 2 "reg_or_0_operand" "rJ,rJ")
			      (const_int 1)
			      (const_int 0))
	     (const_int 0))
	 (match_operand:HI 1 "reg_or_8bit_operand" "rI,0")
	 (match_operand:HI 3 "reg_or_8bit_operand" "0,rI")))]
  ""
  "@
   cmovlbs %r2,%1,%0
   cmovlbc %r2,%3,%0"
  [(set_attr "type" "icmov")])

(define_insn "*movsicc_lbs"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(if_then_else:SI
	 (ne (zero_extract:DI (match_operand:DI 2 "reg_or_0_operand" "rJ,rJ")
			      (const_int 1)
			      (const_int 0))
	     (const_int 0))
	 (match_operand:SI 1 "reg_or_8bit_operand" "rI,0")
	 (match_operand:SI 3 "reg_or_8bit_operand" "0,rI")))]
  ""
  "@
   cmovlbs %r2,%1,%0
   cmovlbc %r2,%3,%0"
  [(set_attr "type" "icmov")])

(define_insn "*movdicc_lbs"
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(if_then_else:DI
	 (ne (zero_extract:DI (match_operand:DI 2 "reg_or_0_operand" "rJ,rJ")
			      (const_int 1)
			      (const_int 0))
	     (const_int 0))
	 (match_operand:DI 1 "reg_or_8bit_operand" "rI,0")
	 (match_operand:DI 3 "reg_or_8bit_operand" "0,rI")))]
  ""
  "@
   cmovlbs %r2,%1,%0
   cmovlbc %r2,%3,%0"
  [(set_attr "type" "icmov")])

;; For ABS, we have two choices, depending on whether the input and output
;; registers are the same or not.
(define_expand "absdi2"
  [(set (match_operand:DI 0 "register_operand" "")
	(abs:DI (match_operand:DI 1 "register_operand" "")))]
  ""
{
  if (rtx_equal_p (operands[0], operands[1]))
    emit_insn (gen_absdi2_same (operands[0], gen_reg_rtx (DImode)));
  else
    emit_insn (gen_absdi2_diff (operands[0], operands[1]));
  DONE;
})

(define_expand "absdi2_same"
  [(set (match_operand:DI 1 "register_operand" "")
	(neg:DI (match_operand:DI 0 "register_operand" "")))
   (set (match_dup 0)
	(if_then_else:DI (ge (match_dup 0) (const_int 0))
			 (match_dup 0)
			 (match_dup 1)))]
  ""
  "")

(define_expand "absdi2_diff"
  [(set (match_operand:DI 0 "register_operand" "")
	(neg:DI (match_operand:DI 1 "register_operand" "")))
   (set (match_dup 0)
	(if_then_else:DI (lt (match_dup 1) (const_int 0))
			 (match_dup 0)
			 (match_dup 1)))]
  ""
  "")

(define_split
  [(set (match_operand:DI 0 "register_operand" "")
	(abs:DI (match_dup 0)))
   (clobber (match_operand:DI 1 "register_operand" ""))]
  ""
  [(set (match_dup 1) (neg:DI (match_dup 0)))
   (set (match_dup 0) (if_then_else:DI (ge (match_dup 0) (const_int 0))
				       (match_dup 0) (match_dup 1)))]
  "")

(define_split
  [(set (match_operand:DI 0 "register_operand" "")
	(abs:DI (match_operand:DI 1 "register_operand" "")))]
  "! rtx_equal_p (operands[0], operands[1])"
  [(set (match_dup 0) (neg:DI (match_dup 1)))
   (set (match_dup 0) (if_then_else:DI (lt (match_dup 1) (const_int 0))
				       (match_dup 0) (match_dup 1)))]
  "")

(define_split
  [(set (match_operand:DI 0 "register_operand" "")
	(neg:DI (abs:DI (match_dup 0))))
   (clobber (match_operand:DI 1 "register_operand" ""))]
  ""
  [(set (match_dup 1) (neg:DI (match_dup 0)))
   (set (match_dup 0) (if_then_else:DI (le (match_dup 0) (const_int 0))
				       (match_dup 0) (match_dup 1)))]
  "")

(define_split
  [(set (match_operand:DI 0 "register_operand" "")
	(neg:DI (abs:DI (match_operand:DI 1 "register_operand" ""))))]
  "! rtx_equal_p (operands[0], operands[1])"
  [(set (match_dup 0) (neg:DI (match_dup 1)))
   (set (match_dup 0) (if_then_else:DI (gt (match_dup 1) (const_int 0))
				       (match_dup 0) (match_dup 1)))]
  "")

(define_insn "sminqi3"
  [(set (match_operand:QI 0 "register_operand" "=r")
	(smin:QI (match_operand:QI 1 "reg_or_0_operand" "%rJ")
		 (match_operand:QI 2 "reg_or_8bit_operand" "rI")))]
  "TARGET_MAX"
  "minsb8 %r1,%2,%0"
  [(set_attr "type" "mvi")])

(define_insn "uminqi3"
  [(set (match_operand:QI 0 "register_operand" "=r")
	(umin:QI (match_operand:QI 1 "reg_or_0_operand" "%rJ")
		 (match_operand:QI 2 "reg_or_8bit_operand" "rI")))]
  "TARGET_MAX"
  "minub8 %r1,%2,%0"
  [(set_attr "type" "mvi")])

(define_insn "smaxqi3"
  [(set (match_operand:QI 0 "register_operand" "=r")
	(smax:QI (match_operand:QI 1 "reg_or_0_operand" "%rJ")
		 (match_operand:QI 2 "reg_or_8bit_operand" "rI")))]
  "TARGET_MAX"
  "maxsb8 %r1,%2,%0"
  [(set_attr "type" "mvi")])

(define_insn "umaxqi3"
  [(set (match_operand:QI 0 "register_operand" "=r")
	(umax:QI (match_operand:QI 1 "reg_or_0_operand" "%rJ")
		 (match_operand:QI 2 "reg_or_8bit_operand" "rI")))]
  "TARGET_MAX"
  "maxub8 %r1,%2,%0"
  [(set_attr "type" "mvi")])

(define_insn "sminhi3"
  [(set (match_operand:HI 0 "register_operand" "=r")
	(smin:HI (match_operand:HI 1 "reg_or_0_operand" "%rJ")
		 (match_operand:HI 2 "reg_or_8bit_operand" "rI")))]
  "TARGET_MAX"
  "minsw4 %r1,%2,%0"
  [(set_attr "type" "mvi")])

(define_insn "uminhi3"
  [(set (match_operand:HI 0 "register_operand" "=r")
	(umin:HI (match_operand:HI 1 "reg_or_0_operand" "%rJ")
		 (match_operand:HI 2 "reg_or_8bit_operand" "rI")))]
  "TARGET_MAX"
  "minuw4 %r1,%2,%0"
  [(set_attr "type" "mvi")])

(define_insn "smaxhi3"
  [(set (match_operand:HI 0 "register_operand" "=r")
	(smax:HI (match_operand:HI 1 "reg_or_0_operand" "%rJ")
		 (match_operand:HI 2 "reg_or_8bit_operand" "rI")))]
  "TARGET_MAX"
  "maxsw4 %r1,%2,%0"
  [(set_attr "type" "mvi")])

(define_insn "umaxhi3"
  [(set (match_operand:HI 0 "register_operand" "=r")
	(umax:HI (match_operand:HI 1 "reg_or_0_operand" "%rJ")
		 (match_operand:HI 2 "reg_or_8bit_operand" "rI")))]
  "TARGET_MAX"
  "maxuw4 %r1,%2,%0"
  [(set_attr "type" "mvi")])

(define_expand "smaxdi3"
  [(set (match_dup 3)
	(le:DI (match_operand:DI 1 "reg_or_0_operand" "")
	       (match_operand:DI 2 "reg_or_8bit_operand" "")))
   (set (match_operand:DI 0 "register_operand" "")
	(if_then_else:DI (eq (match_dup 3) (const_int 0))
			 (match_dup 1) (match_dup 2)))]
  ""
  { operands[3] = gen_reg_rtx (DImode); })

(define_split
  [(set (match_operand:DI 0 "register_operand" "")
	(smax:DI (match_operand:DI 1 "reg_or_0_operand" "")
		 (match_operand:DI 2 "reg_or_8bit_operand" "")))
   (clobber (match_operand:DI 3 "register_operand" ""))]
  "operands[2] != const0_rtx"
  [(set (match_dup 3) (le:DI (match_dup 1) (match_dup 2)))
   (set (match_dup 0) (if_then_else:DI (eq (match_dup 3) (const_int 0))
				       (match_dup 1) (match_dup 2)))]
  "")

(define_insn "*smax_const0"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(smax:DI (match_operand:DI 1 "register_operand" "0")
		 (const_int 0)))]
  ""
  "cmovlt %0,0,%0"
  [(set_attr "type" "icmov")])

(define_expand "smindi3"
  [(set (match_dup 3)
	(lt:DI (match_operand:DI 1 "reg_or_0_operand" "")
	       (match_operand:DI 2 "reg_or_8bit_operand" "")))
   (set (match_operand:DI 0 "register_operand" "")
	(if_then_else:DI (ne (match_dup 3) (const_int 0))
			 (match_dup 1) (match_dup 2)))]
  ""
  { operands[3] = gen_reg_rtx (DImode); })

(define_split
  [(set (match_operand:DI 0 "register_operand" "")
	(smin:DI (match_operand:DI 1 "reg_or_0_operand" "")
		 (match_operand:DI 2 "reg_or_8bit_operand" "")))
   (clobber (match_operand:DI 3 "register_operand" ""))]
  "operands[2] != const0_rtx"
  [(set (match_dup 3) (lt:DI (match_dup 1) (match_dup 2)))
   (set (match_dup 0) (if_then_else:DI (ne (match_dup 3) (const_int 0))
				       (match_dup 1) (match_dup 2)))]
  "")

(define_insn "*smin_const0"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(smin:DI (match_operand:DI 1 "register_operand" "0")
		 (const_int 0)))]
  ""
  "cmovgt %0,0,%0"
  [(set_attr "type" "icmov")])

(define_expand "umaxdi3"
  [(set (match_dup 3)
	(leu:DI (match_operand:DI 1 "reg_or_0_operand" "")
		(match_operand:DI 2 "reg_or_8bit_operand" "")))
   (set (match_operand:DI 0 "register_operand" "")
	(if_then_else:DI (eq (match_dup 3) (const_int 0))
			 (match_dup 1) (match_dup 2)))]
  ""
  "operands[3] = gen_reg_rtx (DImode);")

(define_split
  [(set (match_operand:DI 0 "register_operand" "")
	(umax:DI (match_operand:DI 1 "reg_or_0_operand" "")
		 (match_operand:DI 2 "reg_or_8bit_operand" "")))
   (clobber (match_operand:DI 3 "register_operand" ""))]
  "operands[2] != const0_rtx"
  [(set (match_dup 3) (leu:DI (match_dup 1) (match_dup 2)))
   (set (match_dup 0) (if_then_else:DI (eq (match_dup 3) (const_int 0))
				       (match_dup 1) (match_dup 2)))]
  "")

(define_expand "umindi3"
  [(set (match_dup 3)
	(ltu:DI (match_operand:DI 1 "reg_or_0_operand" "")
		(match_operand:DI 2 "reg_or_8bit_operand" "")))
   (set (match_operand:DI 0 "register_operand" "")
	(if_then_else:DI (ne (match_dup 3) (const_int 0))
			 (match_dup 1) (match_dup 2)))]
  ""
  "operands[3] = gen_reg_rtx (DImode);")

(define_split
  [(set (match_operand:DI 0 "register_operand" "")
	(umin:DI (match_operand:DI 1 "reg_or_0_operand" "")
		 (match_operand:DI 2 "reg_or_8bit_operand" "")))
   (clobber (match_operand:DI 3 "register_operand" ""))]
  "operands[2] != const0_rtx"
  [(set (match_dup 3) (ltu:DI (match_dup 1) (match_dup 2)))
   (set (match_dup 0) (if_then_else:DI (ne (match_dup 3) (const_int 0))
				       (match_dup 1) (match_dup 2)))]
  "")

(define_insn "*bcc_normal"
  [(set (pc)
	(if_then_else
	 (match_operator 1 "signed_comparison_operator"
			 [(match_operand:DI 2 "reg_or_0_operand" "rJ")
			  (const_int 0)])
	 (label_ref (match_operand 0 "" ""))
	 (pc)))]
  ""
  "b%C1 %r2,%0"
  [(set_attr "type" "ibr")])

(define_insn "*bcc_reverse"
  [(set (pc)
	(if_then_else
	 (match_operator 1 "signed_comparison_operator"
			 [(match_operand:DI 2 "register_operand" "r")
			  (const_int 0)])

	 (pc)
	 (label_ref (match_operand 0 "" ""))))]
  ""
  "b%c1 %2,%0"
  [(set_attr "type" "ibr")])

(define_insn "*blbs_normal"
  [(set (pc)
	(if_then_else
	 (ne (zero_extract:DI (match_operand:DI 1 "reg_or_0_operand" "rJ")
			      (const_int 1)
			      (const_int 0))
	     (const_int 0))
	 (label_ref (match_operand 0 "" ""))
	 (pc)))]
  ""
  "blbs %r1,%0"
  [(set_attr "type" "ibr")])

(define_insn "*blbc_normal"
  [(set (pc)
	(if_then_else
	 (eq (zero_extract:DI (match_operand:DI 1 "reg_or_0_operand" "rJ")
			      (const_int 1)
			      (const_int 0))
	     (const_int 0))
	 (label_ref (match_operand 0 "" ""))
	 (pc)))]
  ""
  "blbc %r1,%0"
  [(set_attr "type" "ibr")])

(define_split
  [(parallel
    [(set (pc)
	  (if_then_else
	   (match_operator 1 "comparison_operator"
			   [(zero_extract:DI (match_operand:DI 2 "register_operand" "")
					     (const_int 1)
					     (match_operand:DI 3 "const_int_operand" ""))
			    (const_int 0)])
	   (label_ref (match_operand 0 "" ""))
	   (pc)))
     (clobber (match_operand:DI 4 "register_operand" ""))])]
  "INTVAL (operands[3]) != 0"
  [(set (match_dup 4)
	(lshiftrt:DI (match_dup 2) (match_dup 3)))
   (set (pc)
	(if_then_else (match_op_dup 1
				    [(zero_extract:DI (match_dup 4)
						      (const_int 1)
						      (const_int 0))
				     (const_int 0)])
		      (label_ref (match_dup 0))
		      (pc)))]
  "")

;; The following are the corresponding floating-point insns.  Recall
;; we need to have variants that expand the arguments from SFmode
;; to DFmode.

(define_insn "*cmpdf_ieee"
  [(set (match_operand:DF 0 "register_operand" "=&f")
	(match_operator:DF 1 "alpha_fp_comparison_operator"
			   [(match_operand:DF 2 "reg_or_0_operand" "fG")
			    (match_operand:DF 3 "reg_or_0_operand" "fG")]))]
  "TARGET_FP && alpha_fptm >= ALPHA_FPTM_SU"
  "cmp%-%C1%/ %R2,%R3,%0"
  [(set_attr "type" "fadd")
   (set_attr "trap" "yes")
   (set_attr "trap_suffix" "su")])

(define_insn "*cmpdf_internal"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(match_operator:DF 1 "alpha_fp_comparison_operator"
			   [(match_operand:DF 2 "reg_or_0_operand" "fG")
			    (match_operand:DF 3 "reg_or_0_operand" "fG")]))]
  "TARGET_FP && alpha_fptm < ALPHA_FPTM_SU"
  "cmp%-%C1%/ %R2,%R3,%0"
  [(set_attr "type" "fadd")
   (set_attr "trap" "yes")
   (set_attr "trap_suffix" "su")])

(define_insn "*cmpdf_ieee_ext1"
  [(set (match_operand:DF 0 "register_operand" "=&f")
	(match_operator:DF 1 "alpha_fp_comparison_operator"
			   [(float_extend:DF
			     (match_operand:SF 2 "reg_or_0_operand" "fG"))
			    (match_operand:DF 3 "reg_or_0_operand" "fG")]))]
  "TARGET_FP && alpha_fptm >= ALPHA_FPTM_SU"
  "cmp%-%C1%/ %R2,%R3,%0"
  [(set_attr "type" "fadd")
   (set_attr "trap" "yes")
   (set_attr "trap_suffix" "su")])

(define_insn "*cmpdf_ext1"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(match_operator:DF 1 "alpha_fp_comparison_operator"
			   [(float_extend:DF
			     (match_operand:SF 2 "reg_or_0_operand" "fG"))
			    (match_operand:DF 3 "reg_or_0_operand" "fG")]))]
  "TARGET_FP && alpha_fptm < ALPHA_FPTM_SU"
  "cmp%-%C1%/ %R2,%R3,%0"
  [(set_attr "type" "fadd")
   (set_attr "trap" "yes")
   (set_attr "trap_suffix" "su")])

(define_insn "*cmpdf_ieee_ext2"
  [(set (match_operand:DF 0 "register_operand" "=&f")
	(match_operator:DF 1 "alpha_fp_comparison_operator"
			   [(match_operand:DF 2 "reg_or_0_operand" "fG")
			    (float_extend:DF
			     (match_operand:SF 3 "reg_or_0_operand" "fG"))]))]
  "TARGET_FP && alpha_fptm >= ALPHA_FPTM_SU"
  "cmp%-%C1%/ %R2,%R3,%0"
  [(set_attr "type" "fadd")
   (set_attr "trap" "yes")
   (set_attr "trap_suffix" "su")])

(define_insn "*cmpdf_ext2"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(match_operator:DF 1 "alpha_fp_comparison_operator"
			   [(match_operand:DF 2 "reg_or_0_operand" "fG")
			    (float_extend:DF
			     (match_operand:SF 3 "reg_or_0_operand" "fG"))]))]
  "TARGET_FP && alpha_fptm < ALPHA_FPTM_SU"
  "cmp%-%C1%/ %R2,%R3,%0"
  [(set_attr "type" "fadd")
   (set_attr "trap" "yes")
   (set_attr "trap_suffix" "su")])

(define_insn "*cmpdf_ieee_ext3"
  [(set (match_operand:DF 0 "register_operand" "=&f")
	(match_operator:DF 1 "alpha_fp_comparison_operator"
			   [(float_extend:DF
			     (match_operand:SF 2 "reg_or_0_operand" "fG"))
			    (float_extend:DF
			     (match_operand:SF 3 "reg_or_0_operand" "fG"))]))]
  "TARGET_FP && alpha_fptm >= ALPHA_FPTM_SU"
  "cmp%-%C1%/ %R2,%R3,%0"
  [(set_attr "type" "fadd")
   (set_attr "trap" "yes")
   (set_attr "trap_suffix" "su")])

(define_insn "*cmpdf_ext3"
  [(set (match_operand:DF 0 "register_operand" "=f")
	(match_operator:DF 1 "alpha_fp_comparison_operator"
			   [(float_extend:DF
			     (match_operand:SF 2 "reg_or_0_operand" "fG"))
			    (float_extend:DF
			     (match_operand:SF 3 "reg_or_0_operand" "fG"))]))]
  "TARGET_FP && alpha_fptm < ALPHA_FPTM_SU"
  "cmp%-%C1%/ %R2,%R3,%0"
  [(set_attr "type" "fadd")
   (set_attr "trap" "yes")
   (set_attr "trap_suffix" "su")])

(define_insn "*movdfcc_internal"
  [(set (match_operand:DF 0 "register_operand" "=f,f")
	(if_then_else:DF
	 (match_operator 3 "signed_comparison_operator"
			 [(match_operand:DF 4 "reg_or_0_operand" "fG,fG")
			  (match_operand:DF 2 "const0_operand" "G,G")])
	 (match_operand:DF 1 "reg_or_0_operand" "fG,0")
	 (match_operand:DF 5 "reg_or_0_operand" "0,fG")))]
  "TARGET_FP"
  "@
   fcmov%C3 %R4,%R1,%0
   fcmov%D3 %R4,%R5,%0"
  [(set_attr "type" "fcmov")])

(define_insn "*movsfcc_internal"
  [(set (match_operand:SF 0 "register_operand" "=f,f")
	(if_then_else:SF
	 (match_operator 3 "signed_comparison_operator"
			 [(match_operand:DF 4 "reg_or_0_operand" "fG,fG")
			  (match_operand:DF 2 "const0_operand" "G,G")])
	 (match_operand:SF 1 "reg_or_0_operand" "fG,0")
	 (match_operand:SF 5 "reg_or_0_operand" "0,fG")))]
  "TARGET_FP"
  "@
   fcmov%C3 %R4,%R1,%0
   fcmov%D3 %R4,%R5,%0"
  [(set_attr "type" "fcmov")])

(define_insn "*movdfcc_ext1"
  [(set (match_operand:DF 0 "register_operand" "=f,f")
	(if_then_else:DF
	 (match_operator 3 "signed_comparison_operator"
			 [(match_operand:DF 4 "reg_or_0_operand" "fG,fG")
			  (match_operand:DF 2 "const0_operand" "G,G")])
	 (float_extend:DF (match_operand:SF 1 "reg_or_0_operand" "fG,0"))
	 (match_operand:DF 5 "reg_or_0_operand" "0,fG")))]
  "TARGET_FP"
  "@
   fcmov%C3 %R4,%R1,%0
   fcmov%D3 %R4,%R5,%0"
  [(set_attr "type" "fcmov")])

(define_insn "*movdfcc_ext2"
  [(set (match_operand:DF 0 "register_operand" "=f,f")
	(if_then_else:DF
	 (match_operator 3 "signed_comparison_operator"
			 [(float_extend:DF
			   (match_operand:SF 4 "reg_or_0_operand" "fG,fG"))
			  (match_operand:DF 2 "const0_operand" "G,G")])
	 (match_operand:DF 1 "reg_or_0_operand" "fG,0")
	 (match_operand:DF 5 "reg_or_0_operand" "0,fG")))]
  "TARGET_FP"
  "@
   fcmov%C3 %R4,%R1,%0
   fcmov%D3 %R4,%R5,%0"
  [(set_attr "type" "fcmov")])

(define_insn "*movdfcc_ext3"
  [(set (match_operand:SF 0 "register_operand" "=f,f")
	(if_then_else:SF
	 (match_operator 3 "signed_comparison_operator"
			 [(float_extend:DF
			   (match_operand:SF 4 "reg_or_0_operand" "fG,fG"))
			  (match_operand:DF 2 "const0_operand" "G,G")])
	 (match_operand:SF 1 "reg_or_0_operand" "fG,0")
	 (match_operand:SF 5 "reg_or_0_operand" "0,fG")))]
  "TARGET_FP"
  "@
   fcmov%C3 %R4,%R1,%0
   fcmov%D3 %R4,%R5,%0"
  [(set_attr "type" "fcmov")])

(define_insn "*movdfcc_ext4"
  [(set (match_operand:DF 0 "register_operand" "=f,f")
	(if_then_else:DF
	 (match_operator 3 "signed_comparison_operator"
			 [(float_extend:DF
			   (match_operand:SF 4 "reg_or_0_operand" "fG,fG"))
			  (match_operand:DF 2 "const0_operand" "G,G")])
	 (float_extend:DF (match_operand:SF 1 "reg_or_0_operand" "fG,0"))
	 (match_operand:DF 5 "reg_or_0_operand" "0,fG")))]
  "TARGET_FP"
  "@
   fcmov%C3 %R4,%R1,%0
   fcmov%D3 %R4,%R5,%0"
  [(set_attr "type" "fcmov")])

(define_expand "smaxdf3"
  [(set (match_dup 3)
	(le:DF (match_operand:DF 1 "reg_or_0_operand" "")
	       (match_operand:DF 2 "reg_or_0_operand" "")))
   (set (match_operand:DF 0 "register_operand" "")
	(if_then_else:DF (eq (match_dup 3) (match_dup 4))
			 (match_dup 1) (match_dup 2)))]
  "TARGET_FP"
{
  operands[3] = gen_reg_rtx (DFmode);
  operands[4] = CONST0_RTX (DFmode);
})

(define_expand "smindf3"
  [(set (match_dup 3)
	(lt:DF (match_operand:DF 1 "reg_or_0_operand" "")
	       (match_operand:DF 2 "reg_or_0_operand" "")))
   (set (match_operand:DF 0 "register_operand" "")
	(if_then_else:DF (ne (match_dup 3) (match_dup 4))
			 (match_dup 1) (match_dup 2)))]
  "TARGET_FP"
{
  operands[3] = gen_reg_rtx (DFmode);
  operands[4] = CONST0_RTX (DFmode);
})

(define_expand "smaxsf3"
  [(set (match_dup 3)
	(le:DF (float_extend:DF (match_operand:SF 1 "reg_or_0_operand" ""))
	       (float_extend:DF (match_operand:SF 2 "reg_or_0_operand" ""))))
   (set (match_operand:SF 0 "register_operand" "")
	(if_then_else:SF (eq (match_dup 3) (match_dup 4))
			 (match_dup 1) (match_dup 2)))]
  "TARGET_FP"
{
  operands[3] = gen_reg_rtx (DFmode);
  operands[4] = CONST0_RTX (DFmode);
})

(define_expand "sminsf3"
  [(set (match_dup 3)
	(lt:DF (float_extend:DF (match_operand:SF 1 "reg_or_0_operand" ""))
	       (float_extend:DF (match_operand:SF 2 "reg_or_0_operand" ""))))
   (set (match_operand:SF 0 "register_operand" "")
	(if_then_else:SF (ne (match_dup 3) (match_dup 4))
		      (match_dup 1) (match_dup 2)))]
  "TARGET_FP"
{
  operands[3] = gen_reg_rtx (DFmode);
  operands[4] = CONST0_RTX (DFmode);
})

(define_insn "*fbcc_normal"
  [(set (pc)
	(if_then_else
	 (match_operator 1 "signed_comparison_operator"
			 [(match_operand:DF 2 "reg_or_0_operand" "fG")
			  (match_operand:DF 3 "const0_operand" "G")])
	 (label_ref (match_operand 0 "" ""))
	 (pc)))]
  "TARGET_FP"
  "fb%C1 %R2,%0"
  [(set_attr "type" "fbr")])

(define_insn "*fbcc_ext_normal"
  [(set (pc)
	(if_then_else
	 (match_operator 1 "signed_comparison_operator"
			 [(float_extend:DF
			   (match_operand:SF 2 "reg_or_0_operand" "fG"))
			  (match_operand:DF 3 "const0_operand" "G")])
	 (label_ref (match_operand 0 "" ""))
	 (pc)))]
  "TARGET_FP"
  "fb%C1 %R2,%0"
  [(set_attr "type" "fbr")])

;; These are the main define_expand's used to make conditional branches
;; and compares.

(define_expand "cmpdf"
  [(set (cc0) (compare (match_operand:DF 0 "reg_or_0_operand" "")
		       (match_operand:DF 1 "reg_or_0_operand" "")))]
  "TARGET_FP"
{
  alpha_compare.op0 = operands[0];
  alpha_compare.op1 = operands[1];
  alpha_compare.fp_p = 1;
  DONE;
})

(define_expand "cmptf"
  [(set (cc0) (compare (match_operand:TF 0 "general_operand" "")
		       (match_operand:TF 1 "general_operand" "")))]
  "TARGET_HAS_XFLOATING_LIBS"
{
  alpha_compare.op0 = operands[0];
  alpha_compare.op1 = operands[1];
  alpha_compare.fp_p = 1;
  DONE;
})

(define_expand "cmpdi"
  [(set (cc0) (compare (match_operand:DI 0 "some_operand" "")
		       (match_operand:DI 1 "some_operand" "")))]
  ""
{
  alpha_compare.op0 = operands[0];
  alpha_compare.op1 = operands[1];
  alpha_compare.fp_p = 0;
  DONE;
})

(define_expand "beq"
  [(set (pc)
	(if_then_else (match_dup 1)
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "{ operands[1] = alpha_emit_conditional_branch (EQ); }")

(define_expand "bne"
  [(set (pc)
	(if_then_else (match_dup 1)
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "{ operands[1] = alpha_emit_conditional_branch (NE); }")

(define_expand "blt"
  [(set (pc)
	(if_then_else (match_dup 1)
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "{ operands[1] = alpha_emit_conditional_branch (LT); }")

(define_expand "ble"
  [(set (pc)
	(if_then_else (match_dup 1)
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "{ operands[1] = alpha_emit_conditional_branch (LE); }")

(define_expand "bgt"
  [(set (pc)
	(if_then_else (match_dup 1)
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "{ operands[1] = alpha_emit_conditional_branch (GT); }")

(define_expand "bge"
  [(set (pc)
	(if_then_else (match_dup 1)
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "{ operands[1] = alpha_emit_conditional_branch (GE); }")

(define_expand "bltu"
  [(set (pc)
	(if_then_else (match_dup 1)
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "{ operands[1] = alpha_emit_conditional_branch (LTU); }")

(define_expand "bleu"
  [(set (pc)
	(if_then_else (match_dup 1)
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "{ operands[1] = alpha_emit_conditional_branch (LEU); }")

(define_expand "bgtu"
  [(set (pc)
	(if_then_else (match_dup 1)
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "{ operands[1] = alpha_emit_conditional_branch (GTU); }")

(define_expand "bgeu"
  [(set (pc)
	(if_then_else (match_dup 1)
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "{ operands[1] = alpha_emit_conditional_branch (GEU); }")

(define_expand "bunordered"
  [(set (pc)
	(if_then_else (match_dup 1)
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "{ operands[1] = alpha_emit_conditional_branch (UNORDERED); }")

(define_expand "bordered"
  [(set (pc)
	(if_then_else (match_dup 1)
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
  "{ operands[1] = alpha_emit_conditional_branch (ORDERED); }")

(define_expand "seq"
  [(set (match_operand:DI 0 "register_operand" "")
	(match_dup 1))]
  ""
  "{ if ((operands[1] = alpha_emit_setcc (EQ)) == NULL_RTX) FAIL; }")

(define_expand "sne"
  [(set (match_operand:DI 0 "register_operand" "")
	(match_dup 1))]
  ""
  "{ if ((operands[1] = alpha_emit_setcc (NE)) == NULL_RTX) FAIL; }")

(define_expand "slt"
  [(set (match_operand:DI 0 "register_operand" "")
	(match_dup 1))]
  ""
  "{ if ((operands[1] = alpha_emit_setcc (LT)) == NULL_RTX) FAIL; }")

(define_expand "sle"
  [(set (match_operand:DI 0 "register_operand" "")
	(match_dup 1))]
  ""
  "{ if ((operands[1] = alpha_emit_setcc (LE)) == NULL_RTX) FAIL; }")

(define_expand "sgt"
  [(set (match_operand:DI 0 "register_operand" "")
	(match_dup 1))]
  ""
  "{ if ((operands[1] = alpha_emit_setcc (GT)) == NULL_RTX) FAIL; }")

(define_expand "sge"
  [(set (match_operand:DI 0 "register_operand" "")
	(match_dup 1))]
  ""
  "{ if ((operands[1] = alpha_emit_setcc (GE)) == NULL_RTX) FAIL; }")

(define_expand "sltu"
  [(set (match_operand:DI 0 "register_operand" "")
	(match_dup 1))]
  ""
  "{ if ((operands[1] = alpha_emit_setcc (LTU)) == NULL_RTX) FAIL; }")

(define_expand "sleu"
  [(set (match_operand:DI 0 "register_operand" "")
	(match_dup 1))]
  ""
  "{ if ((operands[1] = alpha_emit_setcc (LEU)) == NULL_RTX) FAIL; }")

(define_expand "sgtu"
  [(set (match_operand:DI 0 "register_operand" "")
	(match_dup 1))]
  ""
  "{ if ((operands[1] = alpha_emit_setcc (GTU)) == NULL_RTX) FAIL; }")

(define_expand "sgeu"
  [(set (match_operand:DI 0 "register_operand" "")
	(match_dup 1))]
  ""
  "{ if ((operands[1] = alpha_emit_setcc (GEU)) == NULL_RTX) FAIL; }")

(define_expand "sunordered"
  [(set (match_operand:DI 0 "register_operand" "")
	(match_dup 1))]
  ""
  "{ if ((operands[1] = alpha_emit_setcc (UNORDERED)) == NULL_RTX) FAIL; }")

(define_expand "sordered"
  [(set (match_operand:DI 0 "register_operand" "")
	(match_dup 1))]
  ""
  "{ if ((operands[1] = alpha_emit_setcc (ORDERED)) == NULL_RTX) FAIL; }")

;; These are the main define_expand's used to make conditional moves.

(define_expand "movsicc"
  [(set (match_operand:SI 0 "register_operand" "")
	(if_then_else:SI (match_operand 1 "comparison_operator" "")
			 (match_operand:SI 2 "reg_or_8bit_operand" "")
			 (match_operand:SI 3 "reg_or_8bit_operand" "")))]
  ""
{
  if ((operands[1] = alpha_emit_conditional_move (operands[1], SImode)) == 0)
    FAIL;
})

(define_expand "movdicc"
  [(set (match_operand:DI 0 "register_operand" "")
	(if_then_else:DI (match_operand 1 "comparison_operator" "")
			 (match_operand:DI 2 "reg_or_8bit_operand" "")
			 (match_operand:DI 3 "reg_or_8bit_operand" "")))]
  ""
{
  if ((operands[1] = alpha_emit_conditional_move (operands[1], DImode)) == 0)
    FAIL;
})

(define_expand "movsfcc"
  [(set (match_operand:SF 0 "register_operand" "")
	(if_then_else:SF (match_operand 1 "comparison_operator" "")
			 (match_operand:SF 2 "reg_or_8bit_operand" "")
			 (match_operand:SF 3 "reg_or_8bit_operand" "")))]
  ""
{
  if ((operands[1] = alpha_emit_conditional_move (operands[1], SFmode)) == 0)
    FAIL;
})

(define_expand "movdfcc"
  [(set (match_operand:DF 0 "register_operand" "")
	(if_then_else:DF (match_operand 1 "comparison_operator" "")
			 (match_operand:DF 2 "reg_or_8bit_operand" "")
			 (match_operand:DF 3 "reg_or_8bit_operand" "")))]
  ""
{
  if ((operands[1] = alpha_emit_conditional_move (operands[1], DFmode)) == 0)
    FAIL;
})

;; These define_split definitions are used in cases when comparisons have
;; not be stated in the correct way and we need to reverse the second
;; comparison.  For example, x >= 7 has to be done as x < 6 with the
;; comparison that tests the result being reversed.  We have one define_split
;; for each use of a comparison.  They do not match valid insns and need
;; not generate valid insns.
;;
;; We can also handle equality comparisons (and inequality comparisons in
;; cases where the resulting add cannot overflow) by doing an add followed by
;; a comparison with zero.  This is faster since the addition takes one
;; less cycle than a compare when feeding into a conditional move.
;; For this case, we also have an SImode pattern since we can merge the add
;; and sign extend and the order doesn't matter.
;;
;; We do not do this for floating-point, since it isn't clear how the "wrong"
;; operation could have been generated.

(define_split
  [(set (match_operand:DI 0 "register_operand" "")
	(if_then_else:DI
	 (match_operator 1 "comparison_operator"
			 [(match_operand:DI 2 "reg_or_0_operand" "")
			  (match_operand:DI 3 "reg_or_cint_operand" "")])
	 (match_operand:DI 4 "reg_or_cint_operand" "")
	 (match_operand:DI 5 "reg_or_cint_operand" "")))
   (clobber (match_operand:DI 6 "register_operand" ""))]
  "operands[3] != const0_rtx"
  [(set (match_dup 6) (match_dup 7))
   (set (match_dup 0)
	(if_then_else:DI (match_dup 8) (match_dup 4) (match_dup 5)))]
{
  enum rtx_code code = GET_CODE (operands[1]);
  int unsignedp = (code == GEU || code == LEU || code == GTU || code == LTU);

  /* If we are comparing for equality with a constant and that constant
     appears in the arm when the register equals the constant, use the
     register since that is more likely to match (and to produce better code
     if both would).  */

  if (code == EQ && GET_CODE (operands[3]) == CONST_INT
      && rtx_equal_p (operands[4], operands[3]))
    operands[4] = operands[2];

  else if (code == NE && GET_CODE (operands[3]) == CONST_INT
	   && rtx_equal_p (operands[5], operands[3]))
    operands[5] = operands[2];

  if (code == NE || code == EQ
      || (extended_count (operands[2], DImode, unsignedp) >= 1
	  && extended_count (operands[3], DImode, unsignedp) >= 1))
    {
      if (GET_CODE (operands[3]) == CONST_INT)
	operands[7] = gen_rtx_PLUS (DImode, operands[2],
				    GEN_INT (- INTVAL (operands[3])));
      else
	operands[7] = gen_rtx_MINUS (DImode, operands[2], operands[3]);

      operands[8] = gen_rtx_fmt_ee (code, VOIDmode, operands[6], const0_rtx);
    }

  else if (code == EQ || code == LE || code == LT
	   || code == LEU || code == LTU)
    {
      operands[7] = gen_rtx_fmt_ee (code, DImode, operands[2], operands[3]);
      operands[8] = gen_rtx_NE (VOIDmode, operands[6], const0_rtx);
    }
  else
    {
      operands[7] = gen_rtx_fmt_ee (reverse_condition (code), DImode,
				    operands[2], operands[3]);
      operands[8] = gen_rtx_EQ (VOIDmode, operands[6], const0_rtx);
    }
})

(define_split
  [(set (match_operand:DI 0 "register_operand" "")
	(if_then_else:DI
	 (match_operator 1 "comparison_operator"
			 [(match_operand:SI 2 "reg_or_0_operand" "")
			  (match_operand:SI 3 "reg_or_cint_operand" "")])
	 (match_operand:DI 4 "reg_or_8bit_operand" "")
	 (match_operand:DI 5 "reg_or_8bit_operand" "")))
   (clobber (match_operand:DI 6 "register_operand" ""))]
  "operands[3] != const0_rtx
   && (GET_CODE (operands[1]) == EQ || GET_CODE (operands[1]) == NE)"
  [(set (match_dup 6) (match_dup 7))
   (set (match_dup 0)
	(if_then_else:DI (match_dup 8) (match_dup 4) (match_dup 5)))]
{
  enum rtx_code code = GET_CODE (operands[1]);
  int unsignedp = (code == GEU || code == LEU || code == GTU || code == LTU);
  rtx tem;

  if ((code != NE && code != EQ
       && ! (extended_count (operands[2], DImode, unsignedp) >= 1
	     && extended_count (operands[3], DImode, unsignedp) >= 1)))
    FAIL;

  if (GET_CODE (operands[3]) == CONST_INT)
    tem = gen_rtx_PLUS (SImode, operands[2],
			GEN_INT (- INTVAL (operands[3])));
  else
    tem = gen_rtx_MINUS (SImode, operands[2], operands[3]);

  operands[7] = gen_rtx_SIGN_EXTEND (DImode, tem);
  operands[8] = gen_rtx_fmt_ee (GET_CODE (operands[1]), VOIDmode,
				operands[6], const0_rtx);
})

;; Prefer to use cmp and arithmetic when possible instead of a cmove.

(define_split
  [(set (match_operand 0 "register_operand" "")
	(if_then_else (match_operator 1 "signed_comparison_operator"
			   [(match_operand:DI 2 "reg_or_0_operand" "")
			    (const_int 0)])
	  (match_operand 3 "const_int_operand" "")
	  (match_operand 4 "const_int_operand" "")))]
  ""
  [(const_int 0)]
{
  if (alpha_split_conditional_move (GET_CODE (operands[1]), operands[0],
				    operands[2], operands[3], operands[4]))
    DONE;
  else
    FAIL;
})

;; ??? Why combine is allowed to create such non-canonical rtl, I don't know.
;; Oh well, we match it in movcc, so it must be partially our fault.
(define_split
  [(set (match_operand 0 "register_operand" "")
	(if_then_else (match_operator 1 "signed_comparison_operator"
			   [(const_int 0)
			    (match_operand:DI 2 "reg_or_0_operand" "")])
	  (match_operand 3 "const_int_operand" "")
	  (match_operand 4 "const_int_operand" "")))]
  ""
  [(const_int 0)]
{
  if (alpha_split_conditional_move (swap_condition (GET_CODE (operands[1])),
				    operands[0], operands[2], operands[3],
				    operands[4]))
    DONE;
  else
    FAIL;
})

(define_insn_and_split "*cmp_sadd_di"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(plus:DI (if_then_else:DI
		   (match_operator 1 "alpha_zero_comparison_operator"
		     [(match_operand:DI 2 "reg_or_0_operand" "rJ")
		      (const_int 0)])
		   (match_operand:DI 3 "const48_operand" "I")
		   (const_int 0))
	         (match_operand:DI 4 "sext_add_operand" "rIO")))
   (clobber (match_scratch:DI 5 "=r"))]
  ""
  "#"
  "! no_new_pseudos || reload_completed"
  [(set (match_dup 5)
	(match_op_dup:DI 1 [(match_dup 2) (const_int 0)]))
   (set (match_dup 0)
	(plus:DI (mult:DI (match_dup 5) (match_dup 3))
		 (match_dup 4)))]
{
  if (! no_new_pseudos)
    operands[5] = gen_reg_rtx (DImode);
  else if (reg_overlap_mentioned_p (operands[5], operands[4]))
    operands[5] = operands[0];
})

(define_insn_and_split "*cmp_sadd_si"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(plus:SI (if_then_else:SI
		   (match_operator 1 "alpha_zero_comparison_operator"
		     [(match_operand:DI 2 "reg_or_0_operand" "rJ")
		      (const_int 0)])
		   (match_operand:SI 3 "const48_operand" "I")
		   (const_int 0))
	         (match_operand:SI 4 "sext_add_operand" "rIO")))
   (clobber (match_scratch:SI 5 "=r"))]
  ""
  "#"
  "! no_new_pseudos || reload_completed"
  [(set (match_dup 5)
	(match_op_dup:SI 1 [(match_dup 2) (const_int 0)]))
   (set (match_dup 0)
	(plus:SI (mult:SI (match_dup 5) (match_dup 3))
		 (match_dup 4)))]
{
  if (! no_new_pseudos)
    operands[5] = gen_reg_rtx (DImode);
  else if (reg_overlap_mentioned_p (operands[5], operands[4]))
    operands[5] = operands[0];
})

(define_insn_and_split "*cmp_sadd_sidi"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(sign_extend:DI
	  (plus:SI (if_then_else:SI
		     (match_operator 1 "alpha_zero_comparison_operator"
		       [(match_operand:DI 2 "reg_or_0_operand" "rJ")
		        (const_int 0)])
		     (match_operand:SI 3 "const48_operand" "I")
		     (const_int 0))
	           (match_operand:SI 4 "sext_add_operand" "rIO"))))
   (clobber (match_scratch:SI 5 "=r"))]
  ""
  "#"
  "! no_new_pseudos || reload_completed"
  [(set (match_dup 5)
	(match_op_dup:SI 1 [(match_dup 2) (const_int 0)]))
   (set (match_dup 0)
	(sign_extend:DI (plus:SI (mult:SI (match_dup 5) (match_dup 3))
				 (match_dup 4))))]
{
  if (! no_new_pseudos)
    operands[5] = gen_reg_rtx (DImode);
  else if (reg_overlap_mentioned_p (operands[5], operands[4]))
    operands[5] = operands[0];
})

(define_insn_and_split "*cmp_ssub_di"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(minus:DI (if_then_else:DI
		    (match_operator 1 "alpha_zero_comparison_operator"
		      [(match_operand:DI 2 "reg_or_0_operand" "rJ")
		       (const_int 0)])
		    (match_operand:DI 3 "const48_operand" "I")
		    (const_int 0))
	          (match_operand:DI 4 "reg_or_8bit_operand" "rI")))
   (clobber (match_scratch:DI 5 "=r"))]
  ""
  "#"
  "! no_new_pseudos || reload_completed"
  [(set (match_dup 5)
	(match_op_dup:DI 1 [(match_dup 2) (const_int 0)]))
   (set (match_dup 0)
	(minus:DI (mult:DI (match_dup 5) (match_dup 3))
		  (match_dup 4)))]
{
  if (! no_new_pseudos)
    operands[5] = gen_reg_rtx (DImode);
  else if (reg_overlap_mentioned_p (operands[5], operands[4]))
    operands[5] = operands[0];
})

(define_insn_and_split "*cmp_ssub_si"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(minus:SI (if_then_else:SI
		    (match_operator 1 "alpha_zero_comparison_operator"
		      [(match_operand:DI 2 "reg_or_0_operand" "rJ")
		       (const_int 0)])
		    (match_operand:SI 3 "const48_operand" "I")
		    (const_int 0))
	          (match_operand:SI 4 "reg_or_8bit_operand" "rI")))
   (clobber (match_scratch:SI 5 "=r"))]
  ""
  "#"
  "! no_new_pseudos || reload_completed"
  [(set (match_dup 5)
	(match_op_dup:SI 1 [(match_dup 2) (const_int 0)]))
   (set (match_dup 0)
	(minus:SI (mult:SI (match_dup 5) (match_dup 3))
		 (match_dup 4)))]
{
  if (! no_new_pseudos)
    operands[5] = gen_reg_rtx (DImode);
  else if (reg_overlap_mentioned_p (operands[5], operands[4]))
    operands[5] = operands[0];
})

(define_insn_and_split "*cmp_ssub_sidi"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(sign_extend:DI
	  (minus:SI (if_then_else:SI
		      (match_operator 1 "alpha_zero_comparison_operator"
		        [(match_operand:DI 2 "reg_or_0_operand" "rJ")
		         (const_int 0)])
		      (match_operand:SI 3 "const48_operand" "I")
		      (const_int 0))
	            (match_operand:SI 4 "reg_or_8bit_operand" "rI"))))
   (clobber (match_scratch:SI 5 "=r"))]
  ""
  "#"
  "! no_new_pseudos || reload_completed"
  [(set (match_dup 5)
	(match_op_dup:SI 1 [(match_dup 2) (const_int 0)]))
   (set (match_dup 0)
	(sign_extend:DI (minus:SI (mult:SI (match_dup 5) (match_dup 3))
				  (match_dup 4))))]
{
  if (! no_new_pseudos)
    operands[5] = gen_reg_rtx (DImode);
  else if (reg_overlap_mentioned_p (operands[5], operands[4]))
    operands[5] = operands[0];
})

;; Here are the CALL and unconditional branch insns.  Calls on NT and OSF
;; work differently, so we have different patterns for each.

;; On Unicos/Mk a call information word (CIW) must be generated for each
;; call. The CIW contains information about arguments passed in registers
;; and is stored in the caller's SSIB. Its offset relative to the beginning
;; of the SSIB is passed in $25. Handling this properly is quite complicated
;; in the presence of inlining since the CIWs for calls performed by the
;; inlined function must be stored in the SSIB of the function it is inlined
;; into as well. We encode the CIW in an unspec and append it to the list
;; of the CIWs for the current function only when the instruction for loading
;; $25 is generated.

(define_expand "call"
  [(use (match_operand:DI 0 "" ""))
   (use (match_operand 1 "" ""))
   (use (match_operand 2 "" ""))
   (use (match_operand 3 "" ""))]
  ""
{
  if (TARGET_ABI_WINDOWS_NT)
    emit_call_insn (gen_call_nt (operands[0], operands[1]));
  else if (TARGET_ABI_OPEN_VMS)
    emit_call_insn (gen_call_vms (operands[0], operands[2]));
  else if (TARGET_ABI_UNICOSMK)
    emit_call_insn (gen_call_umk (operands[0], operands[2]));
  else
    emit_call_insn (gen_call_osf (operands[0], operands[1]));
  DONE;
})

(define_expand "sibcall"
  [(parallel [(call (mem:DI (match_operand 0 "" ""))
			    (match_operand 1 "" ""))
	      (unspec [(reg:DI 29)] UNSPEC_SIBCALL)])]
  "TARGET_ABI_OSF"
{
  gcc_assert (GET_CODE (operands[0]) == MEM);
  operands[0] = XEXP (operands[0], 0);
})

(define_expand "call_osf"
  [(parallel [(call (mem:DI (match_operand 0 "" ""))
		    (match_operand 1 "" ""))
	      (use (reg:DI 29))
	      (clobber (reg:DI 26))])]
  ""
{
  gcc_assert (GET_CODE (operands[0]) == MEM);

  operands[0] = XEXP (operands[0], 0);
  if (! call_operand (operands[0], Pmode))
    operands[0] = copy_to_mode_reg (Pmode, operands[0]);
})

(define_expand "call_nt"
  [(parallel [(call (mem:DI (match_operand 0 "" ""))
		    (match_operand 1 "" ""))
	      (clobber (reg:DI 26))])]
  ""
{
  gcc_assert (GET_CODE (operands[0]) == MEM);

  operands[0] = XEXP (operands[0], 0);
  if (GET_CODE (operands[0]) != SYMBOL_REF && GET_CODE (operands[0]) != REG)
    operands[0] = force_reg (DImode, operands[0]);
})

;; Calls on Unicos/Mk are always indirect.
;; op 0: symbol ref for called function
;; op 1: CIW for $25 represented by an unspec

(define_expand "call_umk"
   [(parallel [(call (mem:DI (match_operand 0 "" ""))
		     (match_operand 1 "" ""))
	       (use (reg:DI 25))
	       (clobber (reg:DI 26))])]
   ""
{
  gcc_assert (GET_CODE (operands[0]) == MEM);

  /* Always load the address of the called function into a register;
     load the CIW in $25.  */

  operands[0] = XEXP (operands[0], 0);
  if (GET_CODE (operands[0]) != REG)
    operands[0] = force_reg (DImode, operands[0]);

  emit_move_insn (gen_rtx_REG (DImode, 25), operands[1]);
})

;;
;; call openvms/alpha
;; op 0: symbol ref for called function
;; op 1: next_arg_reg (argument information value for R25)
;;
(define_expand "call_vms"
  [(parallel [(call (mem:DI (match_operand 0 "" ""))
		    (match_operand 1 "" ""))
	      (use (match_dup 2))
	      (use (reg:DI 25))
	      (use (reg:DI 26))
	      (clobber (reg:DI 27))])]
  ""
{
  gcc_assert (GET_CODE (operands[0]) == MEM);

  operands[0] = XEXP (operands[0], 0);

  /* Always load AI with argument information, then handle symbolic and
     indirect call differently.  Load RA and set operands[2] to PV in
     both cases.  */

  emit_move_insn (gen_rtx_REG (DImode, 25), operands[1]);
  if (GET_CODE (operands[0]) == SYMBOL_REF)
    {
      alpha_need_linkage (XSTR (operands[0], 0), 0);

      operands[2] = const0_rtx;
    }
  else
    {
      emit_move_insn (gen_rtx_REG (Pmode, 26),
		      gen_rtx_MEM (Pmode, plus_constant (operands[0], 8)));
      operands[2] = operands[0];
    }

})

(define_expand "call_value"
  [(use (match_operand 0 "" ""))
   (use (match_operand:DI 1 "" ""))
   (use (match_operand 2 "" ""))
   (use (match_operand 3 "" ""))
   (use (match_operand 4 "" ""))]
  ""
{
  if (TARGET_ABI_WINDOWS_NT)
    emit_call_insn (gen_call_value_nt (operands[0], operands[1], operands[2]));
  else if (TARGET_ABI_OPEN_VMS)
    emit_call_insn (gen_call_value_vms (operands[0], operands[1],
					operands[3]));
  else if (TARGET_ABI_UNICOSMK)
    emit_call_insn (gen_call_value_umk (operands[0], operands[1],
					operands[3]));
  else
    emit_call_insn (gen_call_value_osf (operands[0], operands[1],
					operands[2]));
  DONE;
})

(define_expand "sibcall_value"
  [(parallel [(set (match_operand 0 "" "")
		   (call (mem:DI (match_operand 1 "" ""))
		         (match_operand 2 "" "")))
	      (unspec [(reg:DI 29)] UNSPEC_SIBCALL)])]
  "TARGET_ABI_OSF"
{
  gcc_assert (GET_CODE (operands[1]) == MEM);
  operands[1] = XEXP (operands[1], 0);
})

(define_expand "call_value_osf"
  [(parallel [(set (match_operand 0 "" "")
		   (call (mem:DI (match_operand 1 "" ""))
			 (match_operand 2 "" "")))
	      (use (reg:DI 29))
	      (clobber (reg:DI 26))])]
  ""
{
  gcc_assert (GET_CODE (operands[1]) == MEM);

  operands[1] = XEXP (operands[1], 0);
  if (! call_operand (operands[1], Pmode))
    operands[1] = copy_to_mode_reg (Pmode, operands[1]);
})

(define_expand "call_value_nt"
  [(parallel [(set (match_operand 0 "" "")
		   (call (mem:DI (match_operand 1 "" ""))
			 (match_operand 2 "" "")))
	      (clobber (reg:DI 26))])]
  ""
{
  gcc_assert (GET_CODE (operands[1]) == MEM);

  operands[1] = XEXP (operands[1], 0);
  if (GET_CODE (operands[1]) != SYMBOL_REF && GET_CODE (operands[1]) != REG)
    operands[1] = force_reg (DImode, operands[1]);
})

(define_expand "call_value_vms"
  [(parallel [(set (match_operand 0 "" "")
		   (call (mem:DI (match_operand:DI 1 "" ""))
			 (match_operand 2 "" "")))
	      (use (match_dup 3))
	      (use (reg:DI 25))
	      (use (reg:DI 26))
	      (clobber (reg:DI 27))])]
  ""
{
  gcc_assert (GET_CODE (operands[1]) == MEM);

  operands[1] = XEXP (operands[1], 0);

  /* Always load AI with argument information, then handle symbolic and
     indirect call differently.  Load RA and set operands[3] to PV in
     both cases.  */

  emit_move_insn (gen_rtx_REG (DImode, 25), operands[2]);
  if (GET_CODE (operands[1]) == SYMBOL_REF)
    {
      alpha_need_linkage (XSTR (operands[1], 0), 0);

      operands[3] = const0_rtx;
    }
  else
    {
      emit_move_insn (gen_rtx_REG (Pmode, 26),
		      gen_rtx_MEM (Pmode, plus_constant (operands[1], 8)));
      operands[3] = operands[1];
    }
})

(define_expand "call_value_umk"
  [(parallel [(set (match_operand 0 "" "")
		   (call (mem:DI (match_operand 1 "" ""))
			 (match_operand 2 "" "")))
	      (use (reg:DI 25))
	      (clobber (reg:DI 26))])]
  ""
{
  gcc_assert (GET_CODE (operands[1]) == MEM);

  operands[1] = XEXP (operands[1], 0);
  if (GET_CODE (operands[1]) != REG)
    operands[1] = force_reg (DImode, operands[1]);

  emit_move_insn (gen_rtx_REG (DImode, 25), operands[2]);
})

(define_insn "*call_osf_1_er"
  [(call (mem:DI (match_operand:DI 0 "call_operand" "c,R,s"))
	 (match_operand 1 "" ""))
   (use (reg:DI 29))
   (clobber (reg:DI 26))]
  "TARGET_EXPLICIT_RELOCS && TARGET_ABI_OSF"
  "@
   jsr $26,(%0),0\;ldah $29,0($26)\t\t!gpdisp!%*\;lda $29,0($29)\t\t!gpdisp!%*
   bsr $26,%0\t\t!samegp
   ldq $27,%0($29)\t\t!literal!%#\;jsr $26,($27),%0\t\t!lituse_jsr!%#\;ldah $29,0($26)\t\t!gpdisp!%*\;lda $29,0($29)\t\t!gpdisp!%*"
  [(set_attr "type" "jsr")
   (set_attr "length" "12,*,16")])

;; We must use peep2 instead of a split because we need accurate life
;; information for $gp.  Consider the case of { bar(); while (1); }.
(define_peephole2
  [(parallel [(call (mem:DI (match_operand:DI 0 "call_operand" ""))
		    (match_operand 1 "" ""))
	      (use (reg:DI 29))
	      (clobber (reg:DI 26))])]
  "TARGET_EXPLICIT_RELOCS && TARGET_ABI_OSF && reload_completed
   && ! samegp_function_operand (operands[0], Pmode)
   && (peep2_regno_dead_p (1, 29)
       || find_reg_note (insn, REG_NORETURN, NULL_RTX))"
  [(parallel [(call (mem:DI (match_dup 2))
		    (match_dup 1))
	      (set (reg:DI 26) (plus:DI (pc) (const_int 4)))
	      (unspec_volatile [(reg:DI 29)] UNSPECV_BLOCKAGE)
	      (use (match_dup 0))
	      (use (match_dup 3))])]
{
  if (CONSTANT_P (operands[0]))
    {
      operands[2] = gen_rtx_REG (Pmode, 27);
      operands[3] = GEN_INT (alpha_next_sequence_number++);
      emit_insn (gen_movdi_er_high_g (operands[2], pic_offset_table_rtx,
				      operands[0], operands[3]));
    }
  else
    {
      operands[2] = operands[0];
      operands[0] = const0_rtx;
      operands[3] = const0_rtx;
    }
})

(define_peephole2
  [(parallel [(call (mem:DI (match_operand:DI 0 "call_operand" ""))
		    (match_operand 1 "" ""))
	      (use (reg:DI 29))
	      (clobber (reg:DI 26))])]
  "TARGET_EXPLICIT_RELOCS && TARGET_ABI_OSF && reload_completed
   && ! samegp_function_operand (operands[0], Pmode)
   && ! (peep2_regno_dead_p (1, 29)
         || find_reg_note (insn, REG_NORETURN, NULL_RTX))"
  [(parallel [(call (mem:DI (match_dup 2))
		    (match_dup 1))
	      (set (reg:DI 26) (plus:DI (pc) (const_int 4)))
	      (unspec_volatile [(reg:DI 29)] UNSPECV_BLOCKAGE)
	      (use (match_dup 0))
	      (use (match_dup 4))])
   (set (reg:DI 29)
	(unspec_volatile:DI [(reg:DI 26) (match_dup 3)] UNSPECV_LDGP1))
   (set (reg:DI 29)
	(unspec:DI [(reg:DI 29) (match_dup 3)] UNSPEC_LDGP2))]
{
  if (CONSTANT_P (operands[0]))
    {
      operands[2] = gen_rtx_REG (Pmode, 27);
      operands[4] = GEN_INT (alpha_next_sequence_number++);
      emit_insn (gen_movdi_er_high_g (operands[2], pic_offset_table_rtx,
				      operands[0], operands[4]));
    }
  else
    {
      operands[2] = operands[0];
      operands[0] = const0_rtx;
      operands[4] = const0_rtx;
    }
  operands[3] = GEN_INT (alpha_next_sequence_number++);
})

;; We add a blockage unspec_volatile to prevent insns from moving down
;; from above the call to in between the call and the ldah gpdisp.

(define_insn "*call_osf_2_er"
  [(call (mem:DI (match_operand:DI 0 "register_operand" "c"))
	 (match_operand 1 "" ""))
   (set (reg:DI 26) (plus:DI (pc) (const_int 4)))
   (unspec_volatile [(reg:DI 29)] UNSPECV_BLOCKAGE)
   (use (match_operand 2 "" ""))
   (use (match_operand 3 "const_int_operand" ""))]
  "TARGET_EXPLICIT_RELOCS && TARGET_ABI_OSF"
  "jsr $26,(%0),%2%J3"
  [(set_attr "type" "jsr")
   (set_attr "cannot_copy" "true")])

;; We output a nop after noreturn calls at the very end of the function to
;; ensure that the return address always remains in the caller's code range,
;; as not doing so might confuse unwinding engines.
;;
;; The potential change in insn length is not reflected in the length
;; attributes at this stage. Since the extra space is only actually added at
;; the very end of the compilation process (via final/print_operand), it
;; really seems harmless and not worth the trouble of some extra computation
;; cost and complexity.

(define_insn "*call_osf_1_noreturn"
  [(call (mem:DI (match_operand:DI 0 "call_operand" "c,R,s"))
	 (match_operand 1 "" ""))
   (use (reg:DI 29))
   (clobber (reg:DI 26))]
  "! TARGET_EXPLICIT_RELOCS && TARGET_ABI_OSF
   && find_reg_note (insn, REG_NORETURN, NULL_RTX)"
  "@
   jsr $26,($27),0%+
   bsr $26,$%0..ng%+
   jsr $26,%0%+"
  [(set_attr "type" "jsr")
   (set_attr "length" "*,*,8")])

(define_insn "*call_osf_1"
  [(call (mem:DI (match_operand:DI 0 "call_operand" "c,R,s"))
	 (match_operand 1 "" ""))
   (use (reg:DI 29))
   (clobber (reg:DI 26))]
  "! TARGET_EXPLICIT_RELOCS && TARGET_ABI_OSF"
  "@
   jsr $26,($27),0\;ldgp $29,0($26)
   bsr $26,$%0..ng
   jsr $26,%0\;ldgp $29,0($26)"
  [(set_attr "type" "jsr")
   (set_attr "length" "12,*,16")])

;; Note that the DEC assembler expands "jmp foo" with $at, which
;; doesn't do what we want.
(define_insn "*sibcall_osf_1_er"
  [(call (mem:DI (match_operand:DI 0 "symbolic_operand" "R,s"))
	 (match_operand 1 "" ""))
   (unspec [(reg:DI 29)] UNSPEC_SIBCALL)]
  "TARGET_EXPLICIT_RELOCS && TARGET_ABI_OSF"
  "@
   br $31,%0\t\t!samegp
   ldq $27,%0($29)\t\t!literal!%#\;jmp $31,($27),%0\t\t!lituse_jsr!%#"
  [(set_attr "type" "jsr")
   (set_attr "length" "*,8")])

(define_insn "*sibcall_osf_1"
  [(call (mem:DI (match_operand:DI 0 "symbolic_operand" "R,s"))
	 (match_operand 1 "" ""))
   (unspec [(reg:DI 29)] UNSPEC_SIBCALL)]
  "! TARGET_EXPLICIT_RELOCS && TARGET_ABI_OSF"
  "@
   br $31,$%0..ng
   lda $27,%0\;jmp $31,($27),%0"
  [(set_attr "type" "jsr")
   (set_attr "length" "*,8")])

(define_insn "*call_nt_1"
  [(call (mem:DI (match_operand:DI 0 "call_operand" "r,R,s"))
	 (match_operand 1 "" ""))
   (clobber (reg:DI 26))]
  "TARGET_ABI_WINDOWS_NT"
  "@
   jsr $26,(%0)
   bsr $26,%0
   jsr $26,%0"
  [(set_attr "type" "jsr")
   (set_attr "length" "*,*,12")])

; GAS relies on the order and position of instructions output below in order
; to generate relocs for VMS link to potentially optimize the call.
; Please do not molest.
(define_insn "*call_vms_1"
  [(call (mem:DI (match_operand:DI 0 "call_operand" "r,s"))
	 (match_operand 1 "" ""))
   (use (match_operand:DI 2 "nonmemory_operand" "r,n"))
   (use (reg:DI 25))
   (use (reg:DI 26))
   (clobber (reg:DI 27))]
  "TARGET_ABI_OPEN_VMS"
{
  switch (which_alternative)
    {
    case 0:
   	return "mov %2,$27\;jsr $26,0\;ldq $27,0($29)";
    case 1:
	operands [2] = alpha_use_linkage (operands [0], cfun->decl, 1, 0);
	operands [3] = alpha_use_linkage (operands [0], cfun->decl, 0, 0);
   	return "ldq $26,%3\;ldq $27,%2\;jsr $26,%0\;ldq $27,0($29)";
    default:
      gcc_unreachable ();
    }
}
  [(set_attr "type" "jsr")
   (set_attr "length" "12,16")])

(define_insn "*call_umk_1"
  [(call (mem:DI (match_operand:DI 0 "call_operand" "r"))
	 (match_operand 1 "" ""))
   (use (reg:DI 25))
   (clobber (reg:DI 26))]
  "TARGET_ABI_UNICOSMK"
  "jsr $26,(%0)"
  [(set_attr "type" "jsr")])

;; Call subroutine returning any type.

(define_expand "untyped_call"
  [(parallel [(call (match_operand 0 "" "")
		    (const_int 0))
	      (match_operand 1 "" "")
	      (match_operand 2 "" "")])]
  ""
{
  int i;

  emit_call_insn (GEN_CALL (operands[0], const0_rtx, NULL, const0_rtx));

  for (i = 0; i < XVECLEN (operands[2], 0); i++)
    {
      rtx set = XVECEXP (operands[2], 0, i);
      emit_move_insn (SET_DEST (set), SET_SRC (set));
    }

  /* The optimizer does not know that the call sets the function value
     registers we stored in the result block.  We avoid problems by
     claiming that all hard registers are used and clobbered at this
     point.  */
  emit_insn (gen_blockage ());

  DONE;
})

;; UNSPEC_VOLATILE is considered to use and clobber all hard registers and
;; all of memory.  This blocks insns from being moved across this point.

(define_insn "blockage"
  [(unspec_volatile [(const_int 0)] UNSPECV_BLOCKAGE)]
  ""
  ""
  [(set_attr "length" "0")
   (set_attr "type" "none")])

(define_insn "jump"
  [(set (pc)
	(label_ref (match_operand 0 "" "")))]
  ""
  "br $31,%l0"
  [(set_attr "type" "ibr")])

(define_expand "return"
  [(return)]
  "direct_return ()"
  "")

(define_insn "*return_internal"
  [(return)]
  "reload_completed"
  "ret $31,($26),1"
  [(set_attr "type" "ibr")])

(define_insn "indirect_jump"
  [(set (pc) (match_operand:DI 0 "register_operand" "r"))]
  ""
  "jmp $31,(%0),0"
  [(set_attr "type" "ibr")])

(define_expand "tablejump"
  [(parallel [(set (pc)
		   (match_operand 0 "register_operand" ""))
	      (use (label_ref:DI (match_operand 1 "" "")))])]
  ""
{
  if (TARGET_ABI_WINDOWS_NT)
    {
      rtx dest = gen_reg_rtx (DImode);
      emit_insn (gen_extendsidi2 (dest, operands[0]));
      operands[0] = dest;
    }
  else if (TARGET_ABI_OSF)
    {
      rtx dest = gen_reg_rtx (DImode);
      emit_insn (gen_extendsidi2 (dest, operands[0]));
      emit_insn (gen_adddi3 (dest, pic_offset_table_rtx, dest));	
      operands[0] = dest;
    }
})

(define_insn "*tablejump_osf_nt_internal"
  [(set (pc)
	(match_operand:DI 0 "register_operand" "r"))
   (use (label_ref:DI (match_operand 1 "" "")))]
  "(TARGET_ABI_OSF || TARGET_ABI_WINDOWS_NT)
   && alpha_tablejump_addr_vec (insn)"
{
  operands[2] = alpha_tablejump_best_label (insn);
  return "jmp $31,(%0),%2";
}
  [(set_attr "type" "ibr")])

(define_insn "*tablejump_internal"
  [(set (pc)
	(match_operand:DI 0 "register_operand" "r"))
   (use (label_ref (match_operand 1 "" "")))]
  ""
  "jmp $31,(%0),0"
  [(set_attr "type" "ibr")])

;; Cache flush.  Used by INITIALIZE_TRAMPOLINE.  0x86 is PAL_imb, but we don't
;; want to have to include pal.h in our .s file.
;;
;; Technically the type for call_pal is jsr, but we use that for determining
;; if we need a GP.  Use ibr instead since it has the same EV5 scheduling
;; characteristics.
(define_insn "imb"
  [(unspec_volatile [(const_int 0)] UNSPECV_IMB)]
  ""
  "call_pal 0x86"
  [(set_attr "type" "callpal")])

;; BUGCHK is documented common to OSF/1 and VMS PALcode.
;; NT does not document anything at 0x81 -- presumably it would generate
;; the equivalent of SIGILL, but this isn't that important.
;; ??? Presuming unicosmk uses either OSF/1 or VMS PALcode.
(define_insn "trap"
  [(trap_if (const_int 1) (const_int 0))]
  "!TARGET_ABI_WINDOWS_NT"
  "call_pal 0x81"
  [(set_attr "type" "callpal")])

;; For userland, we load the thread pointer from the TCB.
;; For the kernel, we load the per-cpu private value.

(define_insn "load_tp"
  [(set (match_operand:DI 0 "register_operand" "=v")
	(unspec:DI [(const_int 0)] UNSPEC_TP))]
  "TARGET_ABI_OSF"
{
  if (TARGET_TLS_KERNEL)
    return "call_pal 0x32";
  else
    return "call_pal 0x9e";
}
  [(set_attr "type" "callpal")])

;; For completeness, and possibly a __builtin function, here's how to
;; set the thread pointer.  Since we don't describe enough of this
;; quantity for CSE, we have to use a volatile unspec, and then there's
;; not much point in creating an R16_REG register class.

(define_expand "set_tp"
  [(set (reg:DI 16) (match_operand:DI 0 "input_operand" ""))
   (unspec_volatile [(reg:DI 16)] UNSPECV_SET_TP)]
  "TARGET_ABI_OSF"
  "")

(define_insn "*set_tp"
  [(unspec_volatile [(reg:DI 16)] UNSPECV_SET_TP)]
  "TARGET_ABI_OSF"
{
  if (TARGET_TLS_KERNEL)
    return "call_pal 0x31";
  else
    return "call_pal 0x9f";
}
  [(set_attr "type" "callpal")])

;; Finally, we have the basic data motion insns.  The byte and word insns
;; are done via define_expand.  Start with the floating-point insns, since
;; they are simpler.

(define_insn "*movsf_nofix"
  [(set (match_operand:SF 0 "nonimmediate_operand" "=f,f,*r,*r,m,m")
	(match_operand:SF 1 "input_operand" "fG,m,*rG,m,fG,*r"))]
  "TARGET_FPREGS && ! TARGET_FIX
   && (register_operand (operands[0], SFmode)
       || reg_or_0_operand (operands[1], SFmode))"
  "@
   cpys %R1,%R1,%0
   ld%, %0,%1
   bis $31,%r1,%0
   ldl %0,%1
   st%, %R1,%0
   stl %r1,%0"
  [(set_attr "type" "fcpys,fld,ilog,ild,fst,ist")])

(define_insn "*movsf_fix"
  [(set (match_operand:SF 0 "nonimmediate_operand" "=f,f,*r,*r,m,m,f,*r")
	(match_operand:SF 1 "input_operand" "fG,m,*rG,m,fG,*r,*r,f"))]
  "TARGET_FPREGS && TARGET_FIX
   && (register_operand (operands[0], SFmode)
       || reg_or_0_operand (operands[1], SFmode))"
  "@
   cpys %R1,%R1,%0
   ld%, %0,%1
   bis $31,%r1,%0
   ldl %0,%1
   st%, %R1,%0
   stl %r1,%0
   itofs %1,%0
   ftois %1,%0"
  [(set_attr "type" "fcpys,fld,ilog,ild,fst,ist,itof,ftoi")])

(define_insn "*movsf_nofp"
  [(set (match_operand:SF 0 "nonimmediate_operand" "=r,r,m")
	(match_operand:SF 1 "input_operand" "rG,m,r"))]
  "! TARGET_FPREGS
   && (register_operand (operands[0], SFmode)
       || reg_or_0_operand (operands[1], SFmode))"
  "@
   bis $31,%r1,%0
   ldl %0,%1
   stl %r1,%0"
  [(set_attr "type" "ilog,ild,ist")])

(define_insn "*movdf_nofix"
  [(set (match_operand:DF 0 "nonimmediate_operand" "=f,f,*r,*r,m,m")
	(match_operand:DF 1 "input_operand" "fG,m,*rG,m,fG,*r"))]
  "TARGET_FPREGS && ! TARGET_FIX
   && (register_operand (operands[0], DFmode)
       || reg_or_0_operand (operands[1], DFmode))"
  "@
   cpys %R1,%R1,%0
   ld%- %0,%1
   bis $31,%r1,%0
   ldq %0,%1
   st%- %R1,%0
   stq %r1,%0"
  [(set_attr "type" "fcpys,fld,ilog,ild,fst,ist")])

(define_insn "*movdf_fix"
  [(set (match_operand:DF 0 "nonimmediate_operand" "=f,f,*r,*r,m,m,f,*r")
	(match_operand:DF 1 "input_operand" "fG,m,*rG,m,fG,*r,*r,f"))]
  "TARGET_FPREGS && TARGET_FIX
   && (register_operand (operands[0], DFmode)
       || reg_or_0_operand (operands[1], DFmode))"
  "@
   cpys %R1,%R1,%0
   ld%- %0,%1
   bis $31,%r1,%0
   ldq %0,%1
   st%- %R1,%0
   stq %r1,%0
   itoft %1,%0
   ftoit %1,%0"
  [(set_attr "type" "fcpys,fld,ilog,ild,fst,ist,itof,ftoi")])

(define_insn "*movdf_nofp"
  [(set (match_operand:DF 0 "nonimmediate_operand" "=r,r,m")
	(match_operand:DF 1 "input_operand" "rG,m,r"))]
  "! TARGET_FPREGS
   && (register_operand (operands[0], DFmode)
       || reg_or_0_operand (operands[1], DFmode))"
  "@
   bis $31,%r1,%0
   ldq %0,%1
   stq %r1,%0"
  [(set_attr "type" "ilog,ild,ist")])

;; Subregs suck for register allocation.  Pretend we can move TFmode
;; data between general registers until after reload.

(define_insn_and_split "*movtf_internal"
  [(set (match_operand:TF 0 "nonimmediate_operand" "=r,o")
	(match_operand:TF 1 "input_operand" "roG,rG"))]
  "register_operand (operands[0], TFmode)
   || reg_or_0_operand (operands[1], TFmode)"
  "#"
  "reload_completed"
  [(set (match_dup 0) (match_dup 2))
   (set (match_dup 1) (match_dup 3))]
{
  alpha_split_tmode_pair (operands, TFmode, true); 
})

(define_expand "movsf"
  [(set (match_operand:SF 0 "nonimmediate_operand" "")
	(match_operand:SF 1 "general_operand" ""))]
  ""
{
  if (GET_CODE (operands[0]) == MEM
      && ! reg_or_0_operand (operands[1], SFmode))
    operands[1] = force_reg (SFmode, operands[1]);
})

(define_expand "movdf"
  [(set (match_operand:DF 0 "nonimmediate_operand" "")
	(match_operand:DF 1 "general_operand" ""))]
  ""
{
  if (GET_CODE (operands[0]) == MEM
      && ! reg_or_0_operand (operands[1], DFmode))
    operands[1] = force_reg (DFmode, operands[1]);
})

(define_expand "movtf"
  [(set (match_operand:TF 0 "nonimmediate_operand" "")
	(match_operand:TF 1 "general_operand" ""))]
  ""
{
  if (GET_CODE (operands[0]) == MEM
      && ! reg_or_0_operand (operands[1], TFmode))
    operands[1] = force_reg (TFmode, operands[1]);
})

(define_insn "*movsi"
  [(set (match_operand:SI 0 "nonimmediate_operand" "=r,r,r,r,r,m")
	(match_operand:SI 1 "input_operand" "rJ,K,L,n,m,rJ"))]
  "(TARGET_ABI_OSF || TARGET_ABI_UNICOSMK)
   && (register_operand (operands[0], SImode)
       || reg_or_0_operand (operands[1], SImode))"
  "@
   bis $31,%r1,%0
   lda %0,%1($31)
   ldah %0,%h1($31)
   #
   ldl %0,%1
   stl %r1,%0"
  [(set_attr "type" "ilog,iadd,iadd,multi,ild,ist")])

(define_insn "*movsi_nt_vms"
  [(set (match_operand:SI 0 "nonimmediate_operand" "=r,r,r,r,r,r,m")
	(match_operand:SI 1 "input_operand" "rJ,K,L,s,n,m,rJ"))]
  "(TARGET_ABI_WINDOWS_NT || TARGET_ABI_OPEN_VMS)
    && (register_operand (operands[0], SImode)
        || reg_or_0_operand (operands[1], SImode))"
  "@
   bis $31,%1,%0
   lda %0,%1
   ldah %0,%h1
   lda %0,%1
   #
   ldl %0,%1
   stl %r1,%0"
  [(set_attr "type" "ilog,iadd,iadd,ldsym,multi,ild,ist")])

(define_insn "*movhi_nobwx"
  [(set (match_operand:HI 0 "register_operand" "=r,r")
	(match_operand:HI 1 "input_operand" "rJ,n"))]
  "! TARGET_BWX
   && (register_operand (operands[0], HImode)
       || register_operand (operands[1], HImode))"
  "@
   bis $31,%r1,%0
   lda %0,%L1($31)"
  [(set_attr "type" "ilog,iadd")])

(define_insn "*movhi_bwx"
  [(set (match_operand:HI 0 "nonimmediate_operand" "=r,r,r,m")
	(match_operand:HI 1 "input_operand" "rJ,n,m,rJ"))]
  "TARGET_BWX
   && (register_operand (operands[0], HImode)
       || reg_or_0_operand (operands[1], HImode))"
  "@
   bis $31,%r1,%0
   lda %0,%L1($31)
   ldwu %0,%1
   stw %r1,%0"
  [(set_attr "type" "ilog,iadd,ild,ist")])

(define_insn "*movqi_nobwx"
  [(set (match_operand:QI 0 "register_operand" "=r,r")
	(match_operand:QI 1 "input_operand" "rJ,n"))]
  "! TARGET_BWX
   && (register_operand (operands[0], QImode)
       || register_operand (operands[1], QImode))"
  "@
   bis $31,%r1,%0
   lda %0,%L1($31)"
  [(set_attr "type" "ilog,iadd")])

(define_insn "*movqi_bwx"
  [(set (match_operand:QI 0 "nonimmediate_operand" "=r,r,r,m")
	(match_operand:QI 1 "input_operand" "rJ,n,m,rJ"))]
  "TARGET_BWX
   && (register_operand (operands[0], QImode)
       || reg_or_0_operand (operands[1], QImode))"
  "@
   bis $31,%r1,%0
   lda %0,%L1($31)
   ldbu %0,%1
   stb %r1,%0"
  [(set_attr "type" "ilog,iadd,ild,ist")])

;; We do two major things here: handle mem->mem and construct long
;; constants.

(define_expand "movsi"
  [(set (match_operand:SI 0 "nonimmediate_operand" "")
	(match_operand:SI 1 "general_operand" ""))]
  ""
{
  if (alpha_expand_mov (SImode, operands))
    DONE;
})

;; Split a load of a large constant into the appropriate two-insn
;; sequence.

(define_split
  [(set (match_operand:SI 0 "register_operand" "")
	(match_operand:SI 1 "non_add_const_operand" ""))]
  ""
  [(const_int 0)]
{
  if (alpha_split_const_mov (SImode, operands))
    DONE;
  else
    FAIL;
})

;; Split the load of an address into a four-insn sequence on Unicos/Mk.
;; Always generate a REG_EQUAL note for the last instruction to facilitate
;; optimizations. If the symbolic operand is a label_ref, generate REG_LABEL
;; notes and update LABEL_NUSES because this is not done automatically.
;; Labels may be incorrectly deleted if we don't do this.
;;
;; Describing what the individual instructions do correctly is too complicated
;; so use UNSPECs for each of the three parts of an address.

(define_split
  [(set (match_operand:DI 0 "register_operand" "")
	(match_operand:DI 1 "symbolic_operand" ""))]
  "TARGET_ABI_UNICOSMK && reload_completed"
  [(const_int 0)]
{
  rtx insn1, insn2, insn3;

  insn1 = emit_insn (gen_umk_laum (operands[0], operands[1]));
  emit_insn (gen_ashldi3 (operands[0], operands[0], GEN_INT (32)));
  insn2 = emit_insn (gen_umk_lalm (operands[0], operands[0], operands[1]));
  insn3 = emit_insn (gen_umk_lal (operands[0], operands[0], operands[1]));
  REG_NOTES (insn3) = gen_rtx_EXPR_LIST (REG_EQUAL, operands[1],
					 REG_NOTES (insn3));
  if (GET_CODE (operands[1]) == LABEL_REF)
    {
      rtx label;

      label = XEXP (operands[1], 0);
      REG_NOTES (insn1) = gen_rtx_EXPR_LIST (REG_LABEL, label,
					     REG_NOTES (insn1));
      REG_NOTES (insn2) = gen_rtx_EXPR_LIST (REG_LABEL, label,
					     REG_NOTES (insn2));
      REG_NOTES (insn3) = gen_rtx_EXPR_LIST (REG_LABEL, label,
					     REG_NOTES (insn3));
      LABEL_NUSES (label) += 3;
    }
  DONE;
})

;; Instructions for loading the three parts of an address on Unicos/Mk.

(define_insn "umk_laum"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(unspec:DI [(match_operand:DI 1 "symbolic_operand" "")]
		   UNSPEC_UMK_LAUM))]
  "TARGET_ABI_UNICOSMK"
  "laum %r0,%t1($31)"
  [(set_attr "type" "iadd")])

(define_insn "umk_lalm"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(plus:DI (match_operand:DI 1 "register_operand" "r")
		 (unspec:DI [(match_operand:DI 2 "symbolic_operand" "")]
			    UNSPEC_UMK_LALM)))] 
  "TARGET_ABI_UNICOSMK"
  "lalm %r0,%t2(%r1)"
  [(set_attr "type" "iadd")])

(define_insn "umk_lal"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(plus:DI (match_operand:DI 1 "register_operand" "r")
		 (unspec:DI [(match_operand:DI 2 "symbolic_operand" "")]
			    UNSPEC_UMK_LAL)))]
  "TARGET_ABI_UNICOSMK"
  "lal %r0,%t2(%r1)"
  [(set_attr "type" "iadd")])

;; Add a new call information word to the current function's list of CIWs
;; and load its index into $25. Doing it here ensures that the CIW will be
;; associated with the correct function even in the presence of inlining.

(define_insn "*umk_load_ciw"
  [(set (reg:DI 25)
	(unspec:DI [(match_operand 0 "" "")] UNSPEC_UMK_LOAD_CIW))]
  "TARGET_ABI_UNICOSMK"
{
  operands[0] = unicosmk_add_call_info_word (operands[0]);
  return "lda $25,%0";
}
  [(set_attr "type" "iadd")])

(define_insn "*movdi_er_low_l"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(lo_sum:DI (match_operand:DI 1 "register_operand" "r")
		   (match_operand:DI 2 "local_symbolic_operand" "")))]
  "TARGET_EXPLICIT_RELOCS"
{
  if (true_regnum (operands[1]) == 29)
    return "lda %0,%2(%1)\t\t!gprel";
  else
    return "lda %0,%2(%1)\t\t!gprellow";
}
  [(set_attr "usegp" "yes")])

(define_split
  [(set (match_operand:DI 0 "register_operand" "")
	(match_operand:DI 1 "small_symbolic_operand" ""))]
  "TARGET_EXPLICIT_RELOCS && reload_completed"
  [(set (match_dup 0)
	(lo_sum:DI (match_dup 2) (match_dup 1)))]
  "operands[2] = pic_offset_table_rtx;")

(define_split
  [(set (match_operand:DI 0 "register_operand" "")
	(match_operand:DI 1 "local_symbolic_operand" ""))]
  "TARGET_EXPLICIT_RELOCS && reload_completed"
  [(set (match_dup 0)
	(plus:DI (match_dup 2) (high:DI (match_dup 1))))
   (set (match_dup 0)
	(lo_sum:DI (match_dup 0) (match_dup 1)))]
  "operands[2] = pic_offset_table_rtx;")

(define_split
  [(match_operand 0 "some_small_symbolic_operand" "")]
  ""
  [(match_dup 0)]
  "operands[0] = split_small_symbolic_operand (operands[0]);")

;; Accepts any symbolic, not just global, since function calls that
;; don't go via bsr still use !literal in hopes of linker relaxation.
(define_insn "movdi_er_high_g"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(unspec:DI [(match_operand:DI 1 "register_operand" "r")
		    (match_operand:DI 2 "symbolic_operand" "")
		    (match_operand 3 "const_int_operand" "")]
		   UNSPEC_LITERAL))]
  "TARGET_EXPLICIT_RELOCS"
{
  if (INTVAL (operands[3]) == 0)
    return "ldq %0,%2(%1)\t\t!literal";
  else
    return "ldq %0,%2(%1)\t\t!literal!%3";
}
  [(set_attr "type" "ldsym")])

(define_split
  [(set (match_operand:DI 0 "register_operand" "")
	(match_operand:DI 1 "global_symbolic_operand" ""))]
  "TARGET_EXPLICIT_RELOCS && reload_completed"
  [(set (match_dup 0)
	(unspec:DI [(match_dup 2)
		    (match_dup 1)
		    (const_int 0)] UNSPEC_LITERAL))]
  "operands[2] = pic_offset_table_rtx;")

(define_insn "movdi_er_tlsgd"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(unspec:DI [(match_operand:DI 1 "register_operand" "r")
		    (match_operand:DI 2 "symbolic_operand" "")
		    (match_operand 3 "const_int_operand" "")]
		   UNSPEC_TLSGD))]
  "HAVE_AS_TLS"
{
  if (INTVAL (operands[3]) == 0)
    return "lda %0,%2(%1)\t\t!tlsgd";
  else
    return "lda %0,%2(%1)\t\t!tlsgd!%3";
})

(define_insn "movdi_er_tlsldm"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(unspec:DI [(match_operand:DI 1 "register_operand" "r")
		    (match_operand 2 "const_int_operand" "")]
		   UNSPEC_TLSLDM))]
  "HAVE_AS_TLS"
{
  if (INTVAL (operands[2]) == 0)
    return "lda %0,%&(%1)\t\t!tlsldm";
  else
    return "lda %0,%&(%1)\t\t!tlsldm!%2";
})

(define_insn "*movdi_er_gotdtp"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(unspec:DI [(match_operand:DI 1 "register_operand" "r")
		    (match_operand:DI 2 "symbolic_operand" "")]
		   UNSPEC_DTPREL))]
  "HAVE_AS_TLS"
  "ldq %0,%2(%1)\t\t!gotdtprel"
  [(set_attr "type" "ild")
   (set_attr "usegp" "yes")])

(define_split
  [(set (match_operand:DI 0 "register_operand" "")
	(match_operand:DI 1 "gotdtp_symbolic_operand" ""))]
  "HAVE_AS_TLS && reload_completed"
  [(set (match_dup 0)
	(unspec:DI [(match_dup 2)
		    (match_dup 1)] UNSPEC_DTPREL))]
{
  operands[1] = XVECEXP (XEXP (operands[1], 0), 0, 0);
  operands[2] = pic_offset_table_rtx;
})

(define_insn "*movdi_er_gottp"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(unspec:DI [(match_operand:DI 1 "register_operand" "r")
		    (match_operand:DI 2 "symbolic_operand" "")]
		   UNSPEC_TPREL))]
  "HAVE_AS_TLS"
  "ldq %0,%2(%1)\t\t!gottprel"
  [(set_attr "type" "ild")
   (set_attr "usegp" "yes")])

(define_split
  [(set (match_operand:DI 0 "register_operand" "")
	(match_operand:DI 1 "gottp_symbolic_operand" ""))]
  "HAVE_AS_TLS && reload_completed"
  [(set (match_dup 0)
	(unspec:DI [(match_dup 2)
		    (match_dup 1)] UNSPEC_TPREL))]
{
  operands[1] = XVECEXP (XEXP (operands[1], 0), 0, 0);
  operands[2] = pic_offset_table_rtx;
})

(define_insn "*movdi_er_nofix"
  [(set (match_operand:DI 0 "nonimmediate_operand" "=r,r,r,r,r,r,r,m,*f,*f,Q")
	(match_operand:DI 1 "input_operand" "rJ,K,L,T,s,n,m,rJ,*fJ,Q,*f"))]
  "TARGET_EXPLICIT_RELOCS && ! TARGET_FIX
   && (register_operand (operands[0], DImode)
       || reg_or_0_operand (operands[1], DImode))"
  "@
   mov %r1,%0
   lda %0,%1($31)
   ldah %0,%h1($31)
   #
   #
   #
   ldq%A1 %0,%1
   stq%A0 %r1,%0
   fmov %R1,%0
   ldt %0,%1
   stt %R1,%0"
  [(set_attr "type" "ilog,iadd,iadd,iadd,ldsym,multi,ild,ist,fcpys,fld,fst")
   (set_attr "usegp" "*,*,*,yes,*,*,*,*,*,*,*")])

;; The 'U' constraint matches symbolic operands on Unicos/Mk. Those should
;; have been split up by the rules above but we shouldn't reject the
;; possibility of them getting through.

(define_insn "*movdi_nofix"
  [(set (match_operand:DI 0 "nonimmediate_operand" "=r,r,r,r,r,r,r,m,*f,*f,Q")
	(match_operand:DI 1 "input_operand" "rJ,K,L,U,s,n,m,rJ,*fJ,Q,*f"))]
  "! TARGET_FIX
   && (register_operand (operands[0], DImode)
       || reg_or_0_operand (operands[1], DImode))"
  "@
   bis $31,%r1,%0
   lda %0,%1($31)
   ldah %0,%h1($31)
   laum %0,%t1($31)\;sll %0,32,%0\;lalm %0,%t1(%0)\;lal %0,%t1(%0)
   lda %0,%1
   #
   ldq%A1 %0,%1
   stq%A0 %r1,%0
   cpys %R1,%R1,%0
   ldt %0,%1
   stt %R1,%0"
  [(set_attr "type" "ilog,iadd,iadd,ldsym,ldsym,multi,ild,ist,fcpys,fld,fst")
   (set_attr "length" "*,*,*,16,*,*,*,*,*,*,*")])

(define_insn "*movdi_er_fix"
  [(set (match_operand:DI 0 "nonimmediate_operand"
				"=r,r,r,r,r,r,r, m, *f,*f, Q, r,*f")
	(match_operand:DI 1 "input_operand"
				"rJ,K,L,T,s,n,m,rJ,*fJ, Q,*f,*f, r"))]
  "TARGET_EXPLICIT_RELOCS && TARGET_FIX
   && (register_operand (operands[0], DImode)
       || reg_or_0_operand (operands[1], DImode))"
  "@
   mov %r1,%0
   lda %0,%1($31)
   ldah %0,%h1($31)
   #
   #
   #
   ldq%A1 %0,%1
   stq%A0 %r1,%0
   fmov %R1,%0
   ldt %0,%1
   stt %R1,%0
   ftoit %1,%0
   itoft %1,%0"
  [(set_attr "type" "ilog,iadd,iadd,iadd,ldsym,multi,ild,ist,fcpys,fld,fst,ftoi,itof")
   (set_attr "usegp" "*,*,*,yes,*,*,*,*,*,*,*,*,*")])

(define_insn "*movdi_fix"
  [(set (match_operand:DI 0 "nonimmediate_operand" "=r,r,r,r,r,r,m,*f,*f,Q,r,*f")
	(match_operand:DI 1 "input_operand" "rJ,K,L,s,n,m,rJ,*fJ,Q,*f,*f,r"))]
  "! TARGET_EXPLICIT_RELOCS && TARGET_FIX
   && (register_operand (operands[0], DImode)
       || reg_or_0_operand (operands[1], DImode))"
  "@
   bis $31,%r1,%0
   lda %0,%1($31)
   ldah %0,%h1($31)
   lda %0,%1
   #
   ldq%A1 %0,%1
   stq%A0 %r1,%0
   cpys %R1,%R1,%0
   ldt %0,%1
   stt %R1,%0
   ftoit %1,%0
   itoft %1,%0"
  [(set_attr "type" "ilog,iadd,iadd,ldsym,multi,ild,ist,fcpys,fld,fst,ftoi,itof")])

;; VMS needs to set up "vms_base_regno" for unwinding.  This move
;; often appears dead to the life analysis code, at which point we
;; die for emitting dead prologue instructions.  Force this live.

(define_insn "force_movdi"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(unspec_volatile:DI [(match_operand:DI 1 "register_operand" "r")]
			    UNSPECV_FORCE_MOV))]
  ""
  "mov %1,%0"
  [(set_attr "type" "ilog")])

;; We do three major things here: handle mem->mem, put 64-bit constants in
;; memory, and construct long 32-bit constants.

(define_expand "movdi"
  [(set (match_operand:DI 0 "nonimmediate_operand" "")
	(match_operand:DI 1 "general_operand" ""))]
  ""
{
  if (alpha_expand_mov (DImode, operands))
    DONE;
})

;; Split a load of a large constant into the appropriate two-insn
;; sequence.

(define_split
  [(set (match_operand:DI 0 "register_operand" "")
	(match_operand:DI 1 "non_add_const_operand" ""))]
  ""
  [(const_int 0)]
{
  if (alpha_split_const_mov (DImode, operands))
    DONE;
  else
    FAIL;
})

;; We need to prevent reload from splitting TImode moves, because it
;; might decide to overwrite a pointer with the value it points to.
;; In that case we have to do the loads in the appropriate order so
;; that the pointer is not destroyed too early.

(define_insn_and_split "*movti_internal"
  [(set (match_operand:TI 0 "nonimmediate_operand" "=r,o")
        (match_operand:TI 1 "input_operand" "roJ,rJ"))]
  "(register_operand (operands[0], TImode)
    /* Prevent rematerialization of constants.  */
    && ! CONSTANT_P (operands[1]))
   || reg_or_0_operand (operands[1], TImode)"
  "#"
  "reload_completed"
  [(set (match_dup 0) (match_dup 2))
   (set (match_dup 1) (match_dup 3))]
{
  alpha_split_tmode_pair (operands, TImode, true);
})

(define_expand "movti"
  [(set (match_operand:TI 0 "nonimmediate_operand" "")
        (match_operand:TI 1 "general_operand" ""))]
  ""
{
  if (GET_CODE (operands[0]) == MEM
      && ! reg_or_0_operand (operands[1], TImode))
    operands[1] = force_reg (TImode, operands[1]);

  if (operands[1] == const0_rtx)
    ;
  /* We must put 64-bit constants in memory.  We could keep the
     32-bit constants in TImode and rely on the splitter, but
     this doesn't seem to be worth the pain.  */
  else if (GET_CODE (operands[1]) == CONST_INT
	   || GET_CODE (operands[1]) == CONST_DOUBLE)
    {
      rtx in[2], out[2], target;

      gcc_assert (!no_new_pseudos);

      split_double (operands[1], &in[0], &in[1]);

      if (in[0] == const0_rtx)
	out[0] = const0_rtx;
      else
	{
	  out[0] = gen_reg_rtx (DImode);
	  emit_insn (gen_movdi (out[0], in[0]));
	}

      if (in[1] == const0_rtx)
	out[1] = const0_rtx;
      else
	{
	  out[1] = gen_reg_rtx (DImode);
	  emit_insn (gen_movdi (out[1], in[1]));
	}

      if (GET_CODE (operands[0]) != REG)
	target = gen_reg_rtx (TImode);
      else
	target = operands[0];

      emit_insn (gen_movdi (gen_rtx_SUBREG (DImode, target, 0), out[0]));
      emit_insn (gen_movdi (gen_rtx_SUBREG (DImode, target, 8), out[1]));

      if (target != operands[0])
	emit_insn (gen_rtx_SET (VOIDmode, operands[0], target));

      DONE;
    }
})

;; These are the partial-word cases.
;;
;; First we have the code to load an aligned word.  Operand 0 is the register
;; in which to place the result.  It's mode is QImode or HImode.  Operand 1
;; is an SImode MEM at the low-order byte of the proper word.  Operand 2 is the
;; number of bits within the word that the value is.  Operand 3 is an SImode
;; scratch register.  If operand 0 is a hard register, operand 3 may be the
;; same register.  It is allowed to conflict with operand 1 as well.

(define_expand "aligned_loadqi"
  [(set (match_operand:SI 3 "register_operand" "")
	(match_operand:SI 1 "memory_operand" ""))
   (set (match_operand:DI 0 "register_operand" "")
	(zero_extract:DI (subreg:DI (match_dup 3) 0)
			 (const_int 8)
			 (match_operand:DI 2 "const_int_operand" "")))]

  ""
  "")

(define_expand "aligned_loadhi"
  [(set (match_operand:SI 3 "register_operand" "")
	(match_operand:SI 1 "memory_operand" ""))
   (set (match_operand:DI 0 "register_operand" "")
	(zero_extract:DI (subreg:DI (match_dup 3) 0)
			 (const_int 16)
			 (match_operand:DI 2 "const_int_operand" "")))]

  ""
  "")

;; Similar for unaligned loads, where we use the sequence from the
;; Alpha Architecture manual. We have to distinguish between little-endian
;; and big-endian systems as the sequences are different.
;;
;; Operand 1 is the address.  Operands 2 and 3 are temporaries, where
;; operand 3 can overlap the input and output registers.

(define_expand "unaligned_loadqi"
  [(use (match_operand:DI 0 "register_operand" ""))
   (use (match_operand:DI 1 "address_operand" ""))
   (use (match_operand:DI 2 "register_operand" ""))
   (use (match_operand:DI 3 "register_operand" ""))]
  ""
{
  if (WORDS_BIG_ENDIAN)
    emit_insn (gen_unaligned_loadqi_be (operands[0], operands[1],
					operands[2], operands[3]));
  else
    emit_insn (gen_unaligned_loadqi_le (operands[0], operands[1],
					operands[2], operands[3]));
  DONE;
})

(define_expand "unaligned_loadqi_le"
  [(set (match_operand:DI 2 "register_operand" "")
	(mem:DI (and:DI (match_operand:DI 1 "address_operand" "")
			(const_int -8))))
   (set (match_operand:DI 3 "register_operand" "")
	(match_dup 1))
   (set (match_operand:DI 0 "register_operand" "")
	(zero_extract:DI (match_dup 2)
			 (const_int 8)
			 (ashift:DI (match_dup 3) (const_int 3))))]
  "! WORDS_BIG_ENDIAN"
  "")

(define_expand "unaligned_loadqi_be"
  [(set (match_operand:DI 2 "register_operand" "")
	(mem:DI (and:DI (match_operand:DI 1 "address_operand" "")
			(const_int -8))))
   (set (match_operand:DI 3 "register_operand" "")
	(match_dup 1))
   (set (match_operand:DI 0 "register_operand" "")
	(zero_extract:DI (match_dup 2)
			 (const_int 8)
			 (minus:DI
			   (const_int 56)
			   (ashift:DI (match_dup 3) (const_int 3)))))]
  "WORDS_BIG_ENDIAN"
  "")

(define_expand "unaligned_loadhi"
  [(use (match_operand:DI 0 "register_operand" ""))
   (use (match_operand:DI 1 "address_operand" ""))
   (use (match_operand:DI 2 "register_operand" ""))
   (use (match_operand:DI 3 "register_operand" ""))]
  ""
{
  if (WORDS_BIG_ENDIAN)
    emit_insn (gen_unaligned_loadhi_be (operands[0], operands[1],
					operands[2], operands[3]));
  else
    emit_insn (gen_unaligned_loadhi_le (operands[0], operands[1],
					operands[2], operands[3]));
  DONE;
})

(define_expand "unaligned_loadhi_le"
  [(set (match_operand:DI 2 "register_operand" "")
	(mem:DI (and:DI (match_operand:DI 1 "address_operand" "")
			(const_int -8))))
   (set (match_operand:DI 3 "register_operand" "")
	(match_dup 1))
   (set (match_operand:DI 0 "register_operand" "")
	(zero_extract:DI (match_dup 2)
			 (const_int 16)
			 (ashift:DI (match_dup 3) (const_int 3))))]
  "! WORDS_BIG_ENDIAN"
  "")

(define_expand "unaligned_loadhi_be"
  [(set (match_operand:DI 2 "register_operand" "")
	(mem:DI (and:DI (match_operand:DI 1 "address_operand" "")
			(const_int -8))))
   (set (match_operand:DI 3 "register_operand" "")
	(plus:DI (match_dup 1) (const_int 1)))
   (set (match_operand:DI 0 "register_operand" "")
	(zero_extract:DI (match_dup 2)
			 (const_int 16)
			 (minus:DI
			   (const_int 56)
			   (ashift:DI (match_dup 3) (const_int 3)))))]
  "WORDS_BIG_ENDIAN"
  "")

;; Storing an aligned byte or word requires two temporaries.  Operand 0 is the
;; aligned SImode MEM.  Operand 1 is the register containing the
;; byte or word to store.  Operand 2 is the number of bits within the word that
;; the value should be placed.  Operands 3 and 4 are SImode temporaries.

(define_expand "aligned_store"
  [(set (match_operand:SI 3 "register_operand" "")
	(match_operand:SI 0 "memory_operand" ""))
   (set (subreg:DI (match_dup 3) 0)
	(and:DI (subreg:DI (match_dup 3) 0) (match_dup 5)))
   (set (subreg:DI (match_operand:SI 4 "register_operand" "") 0)
	(ashift:DI (zero_extend:DI (match_operand 1 "register_operand" ""))
		   (match_operand:DI 2 "const_int_operand" "")))
   (set (subreg:DI (match_dup 4) 0)
	(ior:DI (subreg:DI (match_dup 4) 0) (subreg:DI (match_dup 3) 0)))
   (set (match_dup 0) (match_dup 4))]
  ""
{
  operands[5] = GEN_INT (~ (GET_MODE_MASK (GET_MODE (operands[1]))
			    << INTVAL (operands[2])));
})

;; For the unaligned byte and halfword cases, we use code similar to that
;; in the ;; Architecture book, but reordered to lower the number of registers
;; required.  Operand 0 is the address.  Operand 1 is the data to store.
;; Operands 2, 3, and 4 are DImode temporaries, where operands 2 and 4 may
;; be the same temporary, if desired.  If the address is in a register,
;; operand 2 can be that register.

(define_expand "unaligned_storeqi"
  [(use (match_operand:DI 0 "address_operand" ""))
   (use (match_operand:QI 1 "register_operand" ""))
   (use (match_operand:DI 2 "register_operand" ""))
   (use (match_operand:DI 3 "register_operand" ""))
   (use (match_operand:DI 4 "register_operand" ""))]
  ""
{
  if (WORDS_BIG_ENDIAN)
    emit_insn (gen_unaligned_storeqi_be (operands[0], operands[1],
					 operands[2], operands[3],
					 operands[4]));
  else
    emit_insn (gen_unaligned_storeqi_le (operands[0], operands[1],
					 operands[2], operands[3],
					 operands[4]));
  DONE;
})

(define_expand "unaligned_storeqi_le"
  [(set (match_operand:DI 3 "register_operand" "")
	(mem:DI (and:DI (match_operand:DI 0 "address_operand" "")
			(const_int -8))))
   (set (match_operand:DI 2 "register_operand" "")
	(match_dup 0))
   (set (match_dup 3)
	(and:DI (not:DI (ashift:DI (const_int 255)
				   (ashift:DI (match_dup 2) (const_int 3))))
		(match_dup 3)))
   (set (match_operand:DI 4 "register_operand" "")
	(ashift:DI (zero_extend:DI (match_operand:QI 1 "register_operand" ""))
		   (ashift:DI (match_dup 2) (const_int 3))))
   (set (match_dup 4) (ior:DI (match_dup 4) (match_dup 3)))
   (set (mem:DI (and:DI (match_dup 0) (const_int -8)))
	(match_dup 4))]
  "! WORDS_BIG_ENDIAN"
  "")

(define_expand "unaligned_storeqi_be"
  [(set (match_operand:DI 3 "register_operand" "")
	(mem:DI (and:DI (match_operand:DI 0 "address_operand" "")
			(const_int -8))))
   (set (match_operand:DI 2 "register_operand" "")
	(match_dup 0))
   (set (match_dup 3)
	(and:DI (not:DI (ashift:DI (const_int 255)
			  (minus:DI (const_int 56)
				    (ashift:DI (match_dup 2) (const_int 3)))))
		(match_dup 3)))
   (set (match_operand:DI 4 "register_operand" "")
	(ashift:DI (zero_extend:DI (match_operand:QI 1 "register_operand" ""))
		   (minus:DI (const_int 56)
		     (ashift:DI (match_dup 2) (const_int 3)))))
   (set (match_dup 4) (ior:DI (match_dup 4) (match_dup 3)))
   (set (mem:DI (and:DI (match_dup 0) (const_int -8)))
	(match_dup 4))]
  "WORDS_BIG_ENDIAN"
  "")

(define_expand "unaligned_storehi"
  [(use (match_operand:DI 0 "address_operand" ""))
   (use (match_operand:HI 1 "register_operand" ""))
   (use (match_operand:DI 2 "register_operand" ""))
   (use (match_operand:DI 3 "register_operand" ""))
   (use (match_operand:DI 4 "register_operand" ""))]
  ""
{
  if (WORDS_BIG_ENDIAN)
    emit_insn (gen_unaligned_storehi_be (operands[0], operands[1],
					 operands[2], operands[3],
					 operands[4]));
  else
    emit_insn (gen_unaligned_storehi_le (operands[0], operands[1],
					 operands[2], operands[3],
					 operands[4]));
  DONE;
})

(define_expand "unaligned_storehi_le"
  [(set (match_operand:DI 3 "register_operand" "")
	(mem:DI (and:DI (match_operand:DI 0 "address_operand" "")
			(const_int -8))))
   (set (match_operand:DI 2 "register_operand" "")
	(match_dup 0))
   (set (match_dup 3)
	(and:DI (not:DI (ashift:DI (const_int 65535)
				   (ashift:DI (match_dup 2) (const_int 3))))
		(match_dup 3)))
   (set (match_operand:DI 4 "register_operand" "")
	(ashift:DI (zero_extend:DI (match_operand:HI 1 "register_operand" ""))
		   (ashift:DI (match_dup 2) (const_int 3))))
   (set (match_dup 4) (ior:DI (match_dup 4) (match_dup 3)))
   (set (mem:DI (and:DI (match_dup 0) (const_int -8)))
	(match_dup 4))]
  "! WORDS_BIG_ENDIAN"
  "")

(define_expand "unaligned_storehi_be"
  [(set (match_operand:DI 3 "register_operand" "")
	(mem:DI (and:DI (match_operand:DI 0 "address_operand" "")
			(const_int -8))))
   (set (match_operand:DI 2 "register_operand" "")
	(plus:DI (match_dup 0) (const_int 1)))
   (set (match_dup 3)
	(and:DI (not:DI (ashift:DI
			  (const_int 65535)
			  (minus:DI (const_int 56)
				    (ashift:DI (match_dup 2) (const_int 3)))))
		(match_dup 3)))
   (set (match_operand:DI 4 "register_operand" "")
	(ashift:DI (zero_extend:DI (match_operand:HI 1 "register_operand" ""))
		   (minus:DI (const_int 56)
			     (ashift:DI (match_dup 2) (const_int 3)))))
   (set (match_dup 4) (ior:DI (match_dup 4) (match_dup 3)))
   (set (mem:DI (and:DI (match_dup 0) (const_int -8)))
	(match_dup 4))]
  "WORDS_BIG_ENDIAN"
  "")

;; Here are the define_expand's for QI and HI moves that use the above
;; patterns.  We have the normal sets, plus the ones that need scratch
;; registers for reload.

(define_expand "movqi"
  [(set (match_operand:QI 0 "nonimmediate_operand" "")
	(match_operand:QI 1 "general_operand" ""))]
  ""
{
  if (TARGET_BWX
      ? alpha_expand_mov (QImode, operands)
      : alpha_expand_mov_nobwx (QImode, operands))
    DONE;
})

(define_expand "movhi"
  [(set (match_operand:HI 0 "nonimmediate_operand" "")
	(match_operand:HI 1 "general_operand" ""))]
  ""
{
  if (TARGET_BWX
      ? alpha_expand_mov (HImode, operands)
      : alpha_expand_mov_nobwx (HImode, operands))
    DONE;
})

;; Here are the versions for reload.  Note that in the unaligned cases
;; we know that the operand must not be a pseudo-register because stack
;; slots are always aligned references.

(define_expand "reload_inqi"
  [(parallel [(match_operand:QI 0 "register_operand" "=r")
	      (match_operand:QI 1 "any_memory_operand" "m")
	      (match_operand:TI 2 "register_operand" "=&r")])]
  "! TARGET_BWX"
{
  rtx scratch, seq;

  if (aligned_memory_operand (operands[1], QImode))
    {
      seq = gen_reload_inqi_help (operands[0], operands[1],
				  gen_rtx_REG (SImode, REGNO (operands[2])));
    }
  else
    {
      rtx addr;

      /* It is possible that one of the registers we got for operands[2]
	 might coincide with that of operands[0] (which is why we made
	 it TImode).  Pick the other one to use as our scratch.  */
      if (REGNO (operands[0]) == REGNO (operands[2]))
	scratch = gen_rtx_REG (DImode, REGNO (operands[2]) + 1);
      else
	scratch = gen_rtx_REG (DImode, REGNO (operands[2]));

      addr = get_unaligned_address (operands[1]);
      operands[0] = gen_rtx_REG (DImode, REGNO (operands[0]));
      seq = gen_unaligned_loadqi (operands[0], addr, scratch, operands[0]);
      alpha_set_memflags (seq, operands[1]);
    }
  emit_insn (seq);
  DONE;
})

(define_expand "reload_inhi"
  [(parallel [(match_operand:HI 0 "register_operand" "=r")
	      (match_operand:HI 1 "any_memory_operand" "m")
	      (match_operand:TI 2 "register_operand" "=&r")])]
  "! TARGET_BWX"
{
  rtx scratch, seq;

  if (aligned_memory_operand (operands[1], HImode))
    {
      seq = gen_reload_inhi_help (operands[0], operands[1],
				  gen_rtx_REG (SImode, REGNO (operands[2])));
    }
  else
    {
      rtx addr;

      /* It is possible that one of the registers we got for operands[2]
	 might coincide with that of operands[0] (which is why we made
	 it TImode).  Pick the other one to use as our scratch.  */
      if (REGNO (operands[0]) == REGNO (operands[2]))
	scratch = gen_rtx_REG (DImode, REGNO (operands[2]) + 1);
      else
	scratch = gen_rtx_REG (DImode, REGNO (operands[2]));

      addr = get_unaligned_address (operands[1]);
      operands[0] = gen_rtx_REG (DImode, REGNO (operands[0]));
      seq = gen_unaligned_loadhi (operands[0], addr, scratch, operands[0]);
      alpha_set_memflags (seq, operands[1]);
    }
  emit_insn (seq);
  DONE;
})

(define_expand "reload_outqi"
  [(parallel [(match_operand:QI 0 "any_memory_operand" "=m")
	      (match_operand:QI 1 "register_operand" "r")
	      (match_operand:TI 2 "register_operand" "=&r")])]
  "! TARGET_BWX"
{
  if (aligned_memory_operand (operands[0], QImode))
    {
      emit_insn (gen_reload_outqi_help
		 (operands[0], operands[1],
		  gen_rtx_REG (SImode, REGNO (operands[2])),
		  gen_rtx_REG (SImode, REGNO (operands[2]) + 1)));
    }
  else
    {
      rtx addr = get_unaligned_address (operands[0]);
      rtx scratch1 = gen_rtx_REG (DImode, REGNO (operands[2]));
      rtx scratch2 = gen_rtx_REG (DImode, REGNO (operands[2]) + 1);
      rtx scratch3 = scratch1;
      rtx seq;

      if (GET_CODE (addr) == REG)
	scratch1 = addr;

      seq = gen_unaligned_storeqi (addr, operands[1], scratch1,
				   scratch2, scratch3);
      alpha_set_memflags (seq, operands[0]);
      emit_insn (seq);
    }
  DONE;
})

(define_expand "reload_outhi"
  [(parallel [(match_operand:HI 0 "any_memory_operand" "=m")
	      (match_operand:HI 1 "register_operand" "r")
	      (match_operand:TI 2 "register_operand" "=&r")])]
  "! TARGET_BWX"
{
  if (aligned_memory_operand (operands[0], HImode))
    {
      emit_insn (gen_reload_outhi_help
		 (operands[0], operands[1],
		  gen_rtx_REG (SImode, REGNO (operands[2])),
		  gen_rtx_REG (SImode, REGNO (operands[2]) + 1)));
    }
  else
    {
      rtx addr = get_unaligned_address (operands[0]);
      rtx scratch1 = gen_rtx_REG (DImode, REGNO (operands[2]));
      rtx scratch2 = gen_rtx_REG (DImode, REGNO (operands[2]) + 1);
      rtx scratch3 = scratch1;
      rtx seq;

      if (GET_CODE (addr) == REG)
	scratch1 = addr;

      seq = gen_unaligned_storehi (addr, operands[1], scratch1,
				   scratch2, scratch3);
      alpha_set_memflags (seq, operands[0]);
      emit_insn (seq);
    }
  DONE;
})

;; Helpers for the above.  The way reload is structured, we can't
;; always get a proper address for a stack slot during reload_foo
;; expansion, so we must delay our address manipulations until after.

(define_insn_and_split "reload_inqi_help"
  [(set (match_operand:QI 0 "register_operand" "=r")
        (match_operand:QI 1 "memory_operand" "m"))
   (clobber (match_operand:SI 2 "register_operand" "=r"))]
  "! TARGET_BWX && (reload_in_progress || reload_completed)"
  "#"
  "! TARGET_BWX && reload_completed"
  [(const_int 0)]
{
  rtx aligned_mem, bitnum;
  get_aligned_mem (operands[1], &aligned_mem, &bitnum);
  operands[0] = gen_lowpart (DImode, operands[0]);
  emit_insn (gen_aligned_loadqi (operands[0], aligned_mem, bitnum,
				 operands[2]));
  DONE;
})

(define_insn_and_split "reload_inhi_help"
  [(set (match_operand:HI 0 "register_operand" "=r")
        (match_operand:HI 1 "memory_operand" "m"))
   (clobber (match_operand:SI 2 "register_operand" "=r"))]
  "! TARGET_BWX && (reload_in_progress || reload_completed)"
  "#"
  "! TARGET_BWX && reload_completed"
  [(const_int 0)]
{
  rtx aligned_mem, bitnum;
  get_aligned_mem (operands[1], &aligned_mem, &bitnum);
  operands[0] = gen_lowpart (DImode, operands[0]);
  emit_insn (gen_aligned_loadhi (operands[0], aligned_mem, bitnum,
				 operands[2]));
  DONE;
})

(define_insn_and_split "reload_outqi_help"
  [(set (match_operand:QI 0 "memory_operand" "=m")
        (match_operand:QI 1 "register_operand" "r"))
   (clobber (match_operand:SI 2 "register_operand" "=r"))
   (clobber (match_operand:SI 3 "register_operand" "=r"))]
  "! TARGET_BWX && (reload_in_progress || reload_completed)"
  "#"
  "! TARGET_BWX && reload_completed"
  [(const_int 0)]
{
  rtx aligned_mem, bitnum;
  get_aligned_mem (operands[0], &aligned_mem, &bitnum);
  emit_insn (gen_aligned_store (aligned_mem, operands[1], bitnum,
				operands[2], operands[3]));
  DONE;
})

(define_insn_and_split "reload_outhi_help"
  [(set (match_operand:HI 0 "memory_operand" "=m")
        (match_operand:HI 1 "register_operand" "r"))
   (clobber (match_operand:SI 2 "register_operand" "=r"))
   (clobber (match_operand:SI 3 "register_operand" "=r"))]
  "! TARGET_BWX && (reload_in_progress || reload_completed)"
  "#"
  "! TARGET_BWX && reload_completed"
  [(const_int 0)]
{
  rtx aligned_mem, bitnum;
  get_aligned_mem (operands[0], &aligned_mem, &bitnum);
  emit_insn (gen_aligned_store (aligned_mem, operands[1], bitnum,
				operands[2], operands[3]));
  DONE;
})

;; Vector operations

(define_mode_macro VEC [V8QI V4HI V2SI])

(define_expand "mov<mode>"
  [(set (match_operand:VEC 0 "nonimmediate_operand" "")
        (match_operand:VEC 1 "general_operand" ""))]
  ""
{
  if (alpha_expand_mov (<MODE>mode, operands))
    DONE;
})

(define_split
  [(set (match_operand:VEC 0 "register_operand" "")
	(match_operand:VEC 1 "non_zero_const_operand" ""))]
  ""
  [(const_int 0)]
{
  if (alpha_split_const_mov (<MODE>mode, operands))
    DONE;
  else
    FAIL;
})


(define_expand "movmisalign<mode>"
  [(set (match_operand:VEC 0 "nonimmediate_operand" "")
        (match_operand:VEC 1 "general_operand" ""))]
  ""
{
  alpha_expand_movmisalign (<MODE>mode, operands);
  DONE;
})

(define_insn "*mov<mode>_fix"
  [(set (match_operand:VEC 0 "nonimmediate_operand" "=r,r,r,m,*f,*f,m,r,*f")
	(match_operand:VEC 1 "input_operand" "rW,i,m,rW,*fW,m,*f,*f,r"))]
  "TARGET_FIX
   && (register_operand (operands[0], <MODE>mode)
       || reg_or_0_operand (operands[1], <MODE>mode))"
  "@
   bis $31,%r1,%0
   #
   ldq %0,%1
   stq %r1,%0
   cpys %R1,%R1,%0
   ldt %0,%1
   stt %R1,%0
   ftoit %1,%0
   itoft %1,%0"
  [(set_attr "type" "ilog,multi,ild,ist,fcpys,fld,fst,ftoi,itof")])

(define_insn "*mov<mode>_nofix"
  [(set (match_operand:VEC 0 "nonimmediate_operand" "=r,r,r,m,*f,*f,m")
	(match_operand:VEC 1 "input_operand" "rW,i,m,rW,*fW,m,*f"))]
  "! TARGET_FIX
   && (register_operand (operands[0], <MODE>mode)
       || reg_or_0_operand (operands[1], <MODE>mode))"
  "@
   bis $31,%r1,%0
   #
   ldq %0,%1
   stq %r1,%0
   cpys %R1,%R1,%0
   ldt %0,%1
   stt %R1,%0"
  [(set_attr "type" "ilog,multi,ild,ist,fcpys,fld,fst")])

(define_insn "uminv8qi3"
  [(set (match_operand:V8QI 0 "register_operand" "=r")
	(umin:V8QI (match_operand:V8QI 1 "reg_or_0_operand" "rW")
		   (match_operand:V8QI 2 "reg_or_0_operand" "rW")))]
  "TARGET_MAX"
  "minub8 %r1,%r2,%0"
  [(set_attr "type" "mvi")])

(define_insn "sminv8qi3"
  [(set (match_operand:V8QI 0 "register_operand" "=r")
	(smin:V8QI (match_operand:V8QI 1 "reg_or_0_operand" "rW")
		   (match_operand:V8QI 2 "reg_or_0_operand" "rW")))]
  "TARGET_MAX"
  "minsb8 %r1,%r2,%0"
  [(set_attr "type" "mvi")])

(define_insn "uminv4hi3"
  [(set (match_operand:V4HI 0 "register_operand" "=r")
	(umin:V4HI (match_operand:V4HI 1 "reg_or_0_operand" "rW")
		   (match_operand:V4HI 2 "reg_or_0_operand" "rW")))]
  "TARGET_MAX"
  "minuw4 %r1,%r2,%0"
  [(set_attr "type" "mvi")])

(define_insn "sminv4hi3"
  [(set (match_operand:V4HI 0 "register_operand" "=r")
	(smin:V4HI (match_operand:V4HI 1 "reg_or_0_operand" "rW")
		   (match_operand:V4HI 2 "reg_or_0_operand" "rW")))]
  "TARGET_MAX"
  "minsw4 %r1,%r2,%0"
  [(set_attr "type" "mvi")])

(define_insn "umaxv8qi3"
  [(set (match_operand:V8QI 0 "register_operand" "=r")
	(umax:V8QI (match_operand:V8QI 1 "reg_or_0_operand" "rW")
		   (match_operand:V8QI 2 "reg_or_0_operand" "rW")))]
  "TARGET_MAX"
  "maxub8 %r1,%r2,%0"
  [(set_attr "type" "mvi")])

(define_insn "smaxv8qi3"
  [(set (match_operand:V8QI 0 "register_operand" "=r")
	(smax:V8QI (match_operand:V8QI 1 "reg_or_0_operand" "rW")
		   (match_operand:V8QI 2 "reg_or_0_operand" "rW")))]
  "TARGET_MAX"
  "maxsb8 %r1,%r2,%0"
  [(set_attr "type" "mvi")])

(define_insn "umaxv4hi3"
  [(set (match_operand:V4HI 0 "register_operand" "=r")
	(umax:V4HI (match_operand:V4HI 1 "reg_or_0_operand" "rW")
		   (match_operand:V4HI 2 "reg_or_0_operand" "rW")))]
  "TARGET_MAX"
  "maxuw4 %r1,%r2,%0"
  [(set_attr "type" "mvi")])

(define_insn "smaxv4hi3"
  [(set (match_operand:V4HI 0 "register_operand" "=r")
	(smax:V4HI (match_operand:V4HI 1 "reg_or_0_operand" "rW")
		   (match_operand:V4HI 2 "reg_or_0_operand" "rW")))]
  "TARGET_MAX"
  "maxsw4 %r1,%r2,%0"
  [(set_attr "type" "mvi")])

(define_insn "one_cmpl<mode>2"
  [(set (match_operand:VEC 0 "register_operand" "=r")
	(not:VEC (match_operand:VEC 1 "register_operand" "r")))]
  ""
  "ornot $31,%1,%0"
  [(set_attr "type" "ilog")])

(define_insn "and<mode>3"
  [(set (match_operand:VEC 0 "register_operand" "=r")
	(and:VEC (match_operand:VEC 1 "register_operand" "r")
		 (match_operand:VEC 2 "register_operand" "r")))]
  ""
  "and %1,%2,%0"
  [(set_attr "type" "ilog")])

(define_insn "*andnot<mode>3"
  [(set (match_operand:VEC 0 "register_operand" "=r")
	(and:VEC (not:VEC (match_operand:VEC 1 "register_operand" "r"))
		 (match_operand:VEC 2 "register_operand" "r")))]
  ""
  "bic %2,%1,%0"
  [(set_attr "type" "ilog")])

(define_insn "ior<mode>3"
  [(set (match_operand:VEC 0 "register_operand" "=r")
	(ior:VEC (match_operand:VEC 1 "register_operand" "r")
		 (match_operand:VEC 2 "register_operand" "r")))]
  ""
  "bis %1,%2,%0"
  [(set_attr "type" "ilog")])

(define_insn "*iornot<mode>3"
  [(set (match_operand:VEC 0 "register_operand" "=r")
	(ior:VEC (not:DI (match_operand:VEC 1 "register_operand" "r"))
		 (match_operand:VEC 2 "register_operand" "r")))]
  ""
  "ornot %2,%1,%0"
  [(set_attr "type" "ilog")])

(define_insn "xor<mode>3"
  [(set (match_operand:VEC 0 "register_operand" "=r")
	(xor:VEC (match_operand:VEC 1 "register_operand" "r")
		 (match_operand:VEC 2 "register_operand" "r")))]
  ""
  "xor %1,%2,%0"
  [(set_attr "type" "ilog")])

(define_insn "*xornot<mode>3"
  [(set (match_operand:VEC 0 "register_operand" "=r")
	(not:VEC (xor:VEC (match_operand:VEC 1 "register_operand" "r")
			  (match_operand:VEC 2 "register_operand" "r"))))]
  ""
  "eqv %1,%2,%0"
  [(set_attr "type" "ilog")])

(define_expand "vec_shl_<mode>"
  [(set (match_operand:VEC 0 "register_operand" "")
	(ashift:DI (match_operand:VEC 1 "register_operand" "")
		   (match_operand:DI 2 "reg_or_6bit_operand" "")))]
  ""
{
  operands[0] = gen_lowpart (DImode, operands[0]);
  operands[1] = gen_lowpart (DImode, operands[1]);
})

(define_expand "vec_shr_<mode>"
  [(set (match_operand:VEC 0 "register_operand" "")
        (lshiftrt:DI (match_operand:VEC 1 "register_operand" "")
                     (match_operand:DI 2 "reg_or_6bit_operand" "")))]
  ""
{
  operands[0] = gen_lowpart (DImode, operands[0]);
  operands[1] = gen_lowpart (DImode, operands[1]);
})

;; Bit field extract patterns which use ext[wlq][lh]

(define_expand "extv"
  [(set (match_operand:DI 0 "register_operand" "")
	(sign_extract:DI (match_operand:QI 1 "memory_operand" "")
			 (match_operand:DI 2 "immediate_operand" "")
			 (match_operand:DI 3 "immediate_operand" "")))]
  ""
{
  int ofs;

  /* We can do 16, 32 and 64 bit fields, if aligned on byte boundaries.  */
  if (INTVAL (operands[3]) % 8 != 0
      || (INTVAL (operands[2]) != 16
	  && INTVAL (operands[2]) != 32
	  && INTVAL (operands[2]) != 64))
    FAIL;

  /* From mips.md: extract_bit_field doesn't verify that our source
     matches the predicate, so we force it to be a MEM here.  */
  if (GET_CODE (operands[1]) != MEM)
    FAIL;

  /* The bit number is relative to the mode of operand 1 which is
     usually QImode (this might actually be a bug in expmed.c). Note 
     that the bit number is negative in big-endian mode in this case.
     We have to convert that to the offset.  */
  if (WORDS_BIG_ENDIAN)
    ofs = GET_MODE_BITSIZE (GET_MODE (operands[1]))
          - INTVAL (operands[2]) - INTVAL (operands[3]);
  else
    ofs = INTVAL (operands[3]);

  ofs = ofs / 8;

  alpha_expand_unaligned_load (operands[0], operands[1],
			       INTVAL (operands[2]) / 8,
			       ofs, 1);
  DONE;
})

(define_expand "extzv"
  [(set (match_operand:DI 0 "register_operand" "")
	(zero_extract:DI (match_operand:DI 1 "nonimmediate_operand" "")
			 (match_operand:DI 2 "immediate_operand" "")
			 (match_operand:DI 3 "immediate_operand" "")))]
  ""
{
  /* We can do 8, 16, 32 and 64 bit fields, if aligned on byte boundaries.  */
  if (INTVAL (operands[3]) % 8 != 0
      || (INTVAL (operands[2]) != 8
	  && INTVAL (operands[2]) != 16
	  && INTVAL (operands[2]) != 32
	  && INTVAL (operands[2]) != 64))
    FAIL;

  if (GET_CODE (operands[1]) == MEM)
    {
      int ofs;

      /* Fail 8 bit fields, falling back on a simple byte load.  */
      if (INTVAL (operands[2]) == 8)
	FAIL;

      /* The bit number is relative to the mode of operand 1 which is
	 usually QImode (this might actually be a bug in expmed.c). Note 
	 that the bit number is negative in big-endian mode in this case.
	 We have to convert that to the offset.  */
      if (WORDS_BIG_ENDIAN)
	ofs = GET_MODE_BITSIZE (GET_MODE (operands[1]))
	      - INTVAL (operands[2]) - INTVAL (operands[3]);
      else
	ofs = INTVAL (operands[3]);

      ofs = ofs / 8;

      alpha_expand_unaligned_load (operands[0], operands[1],
			           INTVAL (operands[2]) / 8,
				   ofs, 0);
      DONE;
    }
})

(define_expand "insv"
  [(set (zero_extract:DI (match_operand:QI 0 "memory_operand" "")
			 (match_operand:DI 1 "immediate_operand" "")
			 (match_operand:DI 2 "immediate_operand" ""))
	(match_operand:DI 3 "register_operand" ""))]
  ""
{
  int ofs;

  /* We can do 16, 32 and 64 bit fields, if aligned on byte boundaries.  */
  if (INTVAL (operands[2]) % 8 != 0
      || (INTVAL (operands[1]) != 16
	  && INTVAL (operands[1]) != 32
	  && INTVAL (operands[1]) != 64))
    FAIL;

  /* From mips.md: store_bit_field doesn't verify that our source
     matches the predicate, so we force it to be a MEM here.  */
  if (GET_CODE (operands[0]) != MEM)
    FAIL;

  /* The bit number is relative to the mode of operand 1 which is
     usually QImode (this might actually be a bug in expmed.c). Note 
     that the bit number is negative in big-endian mode in this case.
     We have to convert that to the offset.  */
  if (WORDS_BIG_ENDIAN)
    ofs = GET_MODE_BITSIZE (GET_MODE (operands[0]))
          - INTVAL (operands[1]) - INTVAL (operands[2]);
  else
    ofs = INTVAL (operands[2]);

  ofs = ofs / 8;

  alpha_expand_unaligned_store (operands[0], operands[3],
			        INTVAL (operands[1]) / 8, ofs);
  DONE;
})

;; Block move/clear, see alpha.c for more details.
;; Argument 0 is the destination
;; Argument 1 is the source
;; Argument 2 is the length
;; Argument 3 is the alignment

(define_expand "movmemqi"
  [(parallel [(set (match_operand:BLK 0 "memory_operand" "")
		   (match_operand:BLK 1 "memory_operand" ""))
	      (use (match_operand:DI 2 "immediate_operand" ""))
	      (use (match_operand:DI 3 "immediate_operand" ""))])]
  ""
{
  if (alpha_expand_block_move (operands))
    DONE;
  else
    FAIL;
})

(define_expand "movmemdi"
  [(parallel [(set (match_operand:BLK 0 "memory_operand" "")
		   (match_operand:BLK 1 "memory_operand" ""))
	      (use (match_operand:DI 2 "immediate_operand" ""))
	      (use (match_operand:DI 3 "immediate_operand" ""))
	      (use (match_dup 4))
	      (clobber (reg:DI 25))
	      (clobber (reg:DI 16))
	      (clobber (reg:DI 17))
	      (clobber (reg:DI 18))
	      (clobber (reg:DI 19))
	      (clobber (reg:DI 20))
	      (clobber (reg:DI 26))
	      (clobber (reg:DI 27))])]
  "TARGET_ABI_OPEN_VMS"
{
  operands[4] = gen_rtx_SYMBOL_REF (Pmode, "OTS$MOVE");
  alpha_need_linkage (XSTR (operands[4], 0), 0);
})

(define_insn "*movmemdi_1"
  [(set (match_operand:BLK 0 "memory_operand" "=m,=m")
	(match_operand:BLK 1 "memory_operand" "m,m"))
   (use (match_operand:DI 2 "nonmemory_operand" "r,i"))
   (use (match_operand:DI 3 "immediate_operand" ""))
   (use (match_operand:DI 4 "call_operand" "i,i"))
   (clobber (reg:DI 25))
   (clobber (reg:DI 16))
   (clobber (reg:DI 17))
   (clobber (reg:DI 18))
   (clobber (reg:DI 19))
   (clobber (reg:DI 20))
   (clobber (reg:DI 26))
   (clobber (reg:DI 27))]
  "TARGET_ABI_OPEN_VMS"
{
  operands [5] = alpha_use_linkage (operands [4], cfun->decl, 0, 1);
  switch (which_alternative)
    {
    case 0:
	return "lda $16,%0\;bis $31,%2,$17\;lda $18,%1\;ldq $26,%5\;lda $25,3($31)\;jsr $26,%4\;ldq $27,0($29)";
    case 1:
	return "lda $16,%0\;lda $17,%2($31)\;lda $18,%1\;ldq $26,%5\;lda $25,3($31)\;jsr $26,%4\;ldq $27,0($29)";
    default:
      gcc_unreachable ();
    }
}
  [(set_attr "type" "multi")
   (set_attr "length" "28")])

(define_expand "setmemqi"
  [(parallel [(set (match_operand:BLK 0 "memory_operand" "")
		   (match_operand 2 "const_int_operand" ""))
	      (use (match_operand:DI 1 "immediate_operand" ""))
	      (use (match_operand:DI 3 "immediate_operand" ""))])]
  ""
{
  /* If value to set is not zero, use the library routine.  */
  if (operands[2] != const0_rtx)
    FAIL;

  if (alpha_expand_block_clear (operands))
    DONE;
  else
    FAIL;
})

(define_expand "setmemdi"
  [(parallel [(set (match_operand:BLK 0 "memory_operand" "")
		   (match_operand 2 "const_int_operand" ""))
	      (use (match_operand:DI 1 "immediate_operand" ""))
	      (use (match_operand:DI 3 "immediate_operand" ""))
	      (use (match_dup 4))
	      (clobber (reg:DI 25))
	      (clobber (reg:DI 16))
	      (clobber (reg:DI 17))
	      (clobber (reg:DI 26))
	      (clobber (reg:DI 27))])]
  "TARGET_ABI_OPEN_VMS"
{
  /* If value to set is not zero, use the library routine.  */
  if (operands[2] != const0_rtx)
    FAIL;

  operands[4] = gen_rtx_SYMBOL_REF (Pmode, "OTS$ZERO");
  alpha_need_linkage (XSTR (operands[4], 0), 0);
})

(define_insn "*clrmemdi_1"
  [(set (match_operand:BLK 0 "memory_operand" "=m,=m")
		   (const_int 0))
   (use (match_operand:DI 1 "nonmemory_operand" "r,i"))
   (use (match_operand:DI 2 "immediate_operand" ""))
   (use (match_operand:DI 3 "call_operand" "i,i"))
   (clobber (reg:DI 25))
   (clobber (reg:DI 16))
   (clobber (reg:DI 17))
   (clobber (reg:DI 26))
   (clobber (reg:DI 27))]
  "TARGET_ABI_OPEN_VMS"
{
  operands [4] = alpha_use_linkage (operands [3], cfun->decl, 0, 1);
  switch (which_alternative)
    {
    case 0:
	return "lda $16,%0\;bis $31,%1,$17\;ldq $26,%4\;lda $25,2($31)\;jsr $26,%3\;ldq $27,0($29)";
    case 1:
	return "lda $16,%0\;lda $17,%1($31)\;ldq $26,%4\;lda $25,2($31)\;jsr $26,%3\;ldq $27,0($29)";
    default:
      gcc_unreachable ();
    }
}
  [(set_attr "type" "multi")
   (set_attr "length" "24")])


;; Subroutine of stack space allocation.  Perform a stack probe.
(define_expand "probe_stack"
  [(set (match_dup 1) (match_operand:DI 0 "const_int_operand" ""))]
  ""
{
  operands[1] = gen_rtx_MEM (DImode, plus_constant (stack_pointer_rtx,
						    INTVAL (operands[0])));
  MEM_VOLATILE_P (operands[1]) = 1;

  operands[0] = const0_rtx;
})

;; This is how we allocate stack space.  If we are allocating a
;; constant amount of space and we know it is less than 4096
;; bytes, we need do nothing.
;;
;; If it is more than 4096 bytes, we need to probe the stack
;; periodically.
(define_expand "allocate_stack"
  [(set (reg:DI 30)
	(plus:DI (reg:DI 30)
		 (match_operand:DI 1 "reg_or_cint_operand" "")))
   (set (match_operand:DI 0 "register_operand" "=r")
	(match_dup 2))]
  ""
{
  if (GET_CODE (operands[1]) == CONST_INT
      && INTVAL (operands[1]) < 32768)
    {
      if (INTVAL (operands[1]) >= 4096
	  && (flag_stack_check || STACK_CHECK_BUILTIN))
	{
	  /* We do this the same way as in the prologue and generate explicit
	     probes.  Then we update the stack by the constant.  */

	  int probed = 4096;

	  emit_insn (gen_probe_stack (GEN_INT (- probed)));
	  while (probed + 8192 < INTVAL (operands[1]))
	    emit_insn (gen_probe_stack (GEN_INT (- (probed += 8192))));

	  if (probed + 4096 < INTVAL (operands[1]))
	    emit_insn (gen_probe_stack (GEN_INT (- INTVAL(operands[1]))));
	}

      operands[1] = GEN_INT (- INTVAL (operands[1]));
      operands[2] = virtual_stack_dynamic_rtx;
    }
  else
    {
      rtx out_label = 0;
      rtx loop_label = gen_label_rtx ();
      rtx want = gen_reg_rtx (Pmode);
      rtx tmp = gen_reg_rtx (Pmode);
      rtx memref;

      emit_insn (gen_subdi3 (want, stack_pointer_rtx,
			     force_reg (Pmode, operands[1])));
      emit_insn (gen_adddi3 (tmp, stack_pointer_rtx, GEN_INT (-4096)));

      if (GET_CODE (operands[1]) != CONST_INT)
	{
	  out_label = gen_label_rtx ();
	  emit_insn (gen_cmpdi (want, tmp));
	  emit_jump_insn (gen_bgeu (out_label));
	}

      emit_label (loop_label);
      memref = gen_rtx_MEM (DImode, tmp);
      MEM_VOLATILE_P (memref) = 1;
      emit_move_insn (memref, const0_rtx);
      emit_insn (gen_adddi3 (tmp, tmp, GEN_INT(-8192)));
      emit_insn (gen_cmpdi (tmp, want));
      emit_jump_insn (gen_bgtu (loop_label));

      memref = gen_rtx_MEM (DImode, want);
      MEM_VOLATILE_P (memref) = 1;
      emit_move_insn (memref, const0_rtx);

      if (out_label)
	emit_label (out_label);

      emit_move_insn (stack_pointer_rtx, want);
      emit_move_insn (operands[0], virtual_stack_dynamic_rtx);
      DONE;
    }
})

;; This is used by alpha_expand_prolog to do the same thing as above,
;; except we cannot at that time generate new basic blocks, so we hide
;; the loop in this one insn.

(define_insn "prologue_stack_probe_loop"
  [(unspec_volatile [(match_operand:DI 0 "register_operand" "r")
		     (match_operand:DI 1 "register_operand" "r")]
		    UNSPECV_PSPL)]
  ""
{
  operands[2] = gen_label_rtx ();
  (*targetm.asm_out.internal_label) (asm_out_file, "L",
			     CODE_LABEL_NUMBER (operands[2]));

  return "stq $31,-8192(%1)\;subq %0,1,%0\;lda %1,-8192(%1)\;bne %0,%l2";
}
  [(set_attr "length" "16")
   (set_attr "type" "multi")])

(define_expand "prologue"
  [(clobber (const_int 0))]
  ""
{
  alpha_expand_prologue ();
  DONE;
})

;; These take care of emitting the ldgp insn in the prologue. This will be
;; an lda/ldah pair and we want to align them properly.  So we have two
;; unspec_volatile insns, the first of which emits the ldgp assembler macro
;; and the second of which emits nothing.  However, both are marked as type
;; IADD (the default) so the alignment code in alpha.c does the right thing
;; with them.

(define_expand "prologue_ldgp"
  [(set (match_dup 0)
	(unspec_volatile:DI [(match_dup 1) (match_dup 2)] UNSPECV_LDGP1))
   (set (match_dup 0)
	(unspec_volatile:DI [(match_dup 0) (match_dup 2)] UNSPECV_PLDGP2))]
  ""
{
  operands[0] = pic_offset_table_rtx;
  operands[1] = gen_rtx_REG (Pmode, 27);
  operands[2] = (TARGET_EXPLICIT_RELOCS
		 ? GEN_INT (alpha_next_sequence_number++)
		 : const0_rtx);
})

(define_insn "*ldgp_er_1"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(unspec_volatile:DI [(match_operand:DI 1 "register_operand" "r")
			     (match_operand 2 "const_int_operand" "")]
			    UNSPECV_LDGP1))]
  "TARGET_EXPLICIT_RELOCS && TARGET_ABI_OSF"
  "ldah %0,0(%1)\t\t!gpdisp!%2"
  [(set_attr "cannot_copy" "true")])

(define_insn "*ldgp_er_2"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(unspec:DI [(match_operand:DI 1 "register_operand" "r")
		    (match_operand 2 "const_int_operand" "")]
		   UNSPEC_LDGP2))]
  "TARGET_EXPLICIT_RELOCS && TARGET_ABI_OSF"
  "lda %0,0(%1)\t\t!gpdisp!%2"
  [(set_attr "cannot_copy" "true")])

(define_insn "*prologue_ldgp_er_2"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(unspec_volatile:DI [(match_operand:DI 1 "register_operand" "r")
			     (match_operand 2 "const_int_operand" "")]
		   	    UNSPECV_PLDGP2))]
  "TARGET_EXPLICIT_RELOCS && TARGET_ABI_OSF"
  "lda %0,0(%1)\t\t!gpdisp!%2\n$%~..ng:"
  [(set_attr "cannot_copy" "true")])

(define_insn "*prologue_ldgp_1"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(unspec_volatile:DI [(match_operand:DI 1 "register_operand" "r")
			     (match_operand 2 "const_int_operand" "")]
			    UNSPECV_LDGP1))]
  ""
  "ldgp %0,0(%1)\n$%~..ng:"
  [(set_attr "cannot_copy" "true")])

(define_insn "*prologue_ldgp_2"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(unspec_volatile:DI [(match_operand:DI 1 "register_operand" "r")
			     (match_operand 2 "const_int_operand" "")]
		   	    UNSPECV_PLDGP2))]
  ""
  "")

;; The _mcount profiling hook has special calling conventions, and
;; does not clobber all the registers that a normal call would.  So
;; hide the fact this is a call at all.

(define_insn "prologue_mcount"
  [(unspec_volatile [(const_int 0)] UNSPECV_MCOUNT)]
  ""
{
  if (TARGET_EXPLICIT_RELOCS)
    /* Note that we cannot use a lituse_jsr reloc, since _mcount
       cannot be called via the PLT.  */
    return "ldq $28,_mcount($29)\t\t!literal\;jsr $28,($28),_mcount";
  else
    return "lda $28,_mcount\;jsr $28,($28),_mcount";
}
  [(set_attr "type" "multi")
   (set_attr "length" "8")])

(define_insn "init_fp"
  [(set (match_operand:DI 0 "register_operand" "=r")
        (match_operand:DI 1 "register_operand" "r"))
   (clobber (mem:BLK (match_operand:DI 2 "register_operand" "=r")))]
  ""
  "bis $31,%1,%0")

(define_expand "epilogue"
  [(return)]
  ""
{
  alpha_expand_epilogue ();
})

(define_expand "sibcall_epilogue"
  [(return)]
  "TARGET_ABI_OSF"
{
  alpha_expand_epilogue ();
  DONE;
})

(define_expand "builtin_longjmp"
  [(use (match_operand:DI 0 "register_operand" "r"))]
  "TARGET_ABI_OSF"
{
  /* The elements of the buffer are, in order:  */
  rtx fp = gen_rtx_MEM (Pmode, operands[0]);
  rtx lab = gen_rtx_MEM (Pmode, plus_constant (operands[0], 8));
  rtx stack = gen_rtx_MEM (Pmode, plus_constant (operands[0], 16));
  rtx pv = gen_rtx_REG (Pmode, 27);

  /* This bit is the same as expand_builtin_longjmp.  */
  emit_move_insn (hard_frame_pointer_rtx, fp);
  emit_move_insn (pv, lab);
  emit_stack_restore (SAVE_NONLOCAL, stack, NULL_RTX);
  emit_insn (gen_rtx_USE (VOIDmode, hard_frame_pointer_rtx));
  emit_insn (gen_rtx_USE (VOIDmode, stack_pointer_rtx));

  /* Load the label we are jumping through into $27 so that we know
     where to look for it when we get back to setjmp's function for
     restoring the gp.  */
  emit_jump_insn (gen_builtin_longjmp_internal (pv));
  emit_barrier ();
  DONE;
})

;; This is effectively a copy of indirect_jump, but constrained such
;; that register renaming cannot foil our cunning plan with $27.
(define_insn "builtin_longjmp_internal"
  [(set (pc)
	(unspec_volatile [(match_operand:DI 0 "register_operand" "c")]
			 UNSPECV_LONGJMP))]
  ""
  "jmp $31,(%0),0"
  [(set_attr "type" "ibr")])

(define_expand "builtin_setjmp_receiver"
  [(unspec_volatile [(label_ref (match_operand 0 "" ""))] UNSPECV_SETJMPR)]
  "TARGET_ABI_OSF"
  "")

(define_insn_and_split "*builtin_setjmp_receiver_1"
  [(unspec_volatile [(match_operand 0 "" "")] UNSPECV_SETJMPR)]
  "TARGET_ABI_OSF"
{
  if (TARGET_EXPLICIT_RELOCS)
    return "#";
  else
    return "br $27,$LSJ%=\n$LSJ%=:\;ldgp $29,0($27)";
}
  "&& TARGET_EXPLICIT_RELOCS && reload_completed"
  [(set (match_dup 1)
	(unspec_volatile:DI [(match_dup 2) (match_dup 3)] UNSPECV_LDGP1))
   (set (match_dup 1)
	(unspec:DI [(match_dup 1) (match_dup 3)] UNSPEC_LDGP2))]
{
  if (prev_nonnote_insn (curr_insn) != XEXP (operands[0], 0))
    emit_insn (gen_rtx_UNSPEC_VOLATILE (VOIDmode, gen_rtvec (1, operands[0]),
					UNSPECV_SETJMPR_ER));
  operands[1] = pic_offset_table_rtx;
  operands[2] = gen_rtx_REG (Pmode, 27);
  operands[3] = GEN_INT (alpha_next_sequence_number++);
}
  [(set_attr "length" "12")
   (set_attr "type" "multi")])

(define_insn "*builtin_setjmp_receiver_er_sl_1"
  [(unspec_volatile [(match_operand 0 "" "")] UNSPECV_SETJMPR_ER)]
  "TARGET_ABI_OSF && TARGET_EXPLICIT_RELOCS && TARGET_AS_CAN_SUBTRACT_LABELS"
  "lda $27,$LSJ%=-%l0($27)\n$LSJ%=:")
  
(define_insn "*builtin_setjmp_receiver_er_1"
  [(unspec_volatile [(match_operand 0 "" "")] UNSPECV_SETJMPR_ER)]
  "TARGET_ABI_OSF && TARGET_EXPLICIT_RELOCS"
  "br $27,$LSJ%=\n$LSJ%=:"
  [(set_attr "type" "ibr")])

(define_expand "exception_receiver"
  [(unspec_volatile [(match_dup 0)] UNSPECV_EHR)]
  "TARGET_ABI_OSF"
{
  if (TARGET_LD_BUGGY_LDGP)
    operands[0] = alpha_gp_save_rtx ();
  else
    operands[0] = const0_rtx;
})

(define_insn "*exception_receiver_2"
  [(unspec_volatile [(match_operand:DI 0 "memory_operand" "m")] UNSPECV_EHR)]
  "TARGET_ABI_OSF && TARGET_LD_BUGGY_LDGP"
  "ldq $29,%0"
  [(set_attr "type" "ild")])

(define_insn_and_split "*exception_receiver_1"
  [(unspec_volatile [(const_int 0)] UNSPECV_EHR)]
  "TARGET_ABI_OSF"
{
  if (TARGET_EXPLICIT_RELOCS)
    return "ldah $29,0($26)\t\t!gpdisp!%*\;lda $29,0($29)\t\t!gpdisp!%*";
  else
    return "ldgp $29,0($26)";
}
  "&& TARGET_EXPLICIT_RELOCS && reload_completed"
  [(set (match_dup 0)
	(unspec_volatile:DI [(match_dup 1) (match_dup 2)] UNSPECV_LDGP1))
   (set (match_dup 0)
	(unspec:DI [(match_dup 0) (match_dup 2)] UNSPEC_LDGP2))]
{
  operands[0] = pic_offset_table_rtx;
  operands[1] = gen_rtx_REG (Pmode, 26);
  operands[2] = GEN_INT (alpha_next_sequence_number++);
}
  [(set_attr "length" "8")
   (set_attr "type" "multi")])

(define_expand "nonlocal_goto_receiver"
  [(unspec_volatile [(const_int 0)] UNSPECV_BLOCKAGE)
   (set (reg:DI 27) (mem:DI (reg:DI 29)))
   (unspec_volatile [(const_int 0)] UNSPECV_BLOCKAGE)
   (use (reg:DI 27))]
  "TARGET_ABI_OPEN_VMS"
  "")

(define_insn "arg_home"
  [(unspec [(const_int 0)] UNSPEC_ARG_HOME)
   (use (reg:DI 1))
   (use (reg:DI 25))
   (use (reg:DI 16))
   (use (reg:DI 17))
   (use (reg:DI 18))
   (use (reg:DI 19))
   (use (reg:DI 20))
   (use (reg:DI 21))
   (use (reg:DI 48))
   (use (reg:DI 49))
   (use (reg:DI 50))
   (use (reg:DI 51))
   (use (reg:DI 52))
   (use (reg:DI 53))
   (clobber (mem:BLK (const_int 0)))
   (clobber (reg:DI 24))
   (clobber (reg:DI 25))
   (clobber (reg:DI 0))]
  "TARGET_ABI_OPEN_VMS"
  "lda $0,OTS$HOME_ARGS\;ldq $0,8($0)\;jsr $0,OTS$HOME_ARGS"
  [(set_attr "length" "16")
   (set_attr "type" "multi")])

;; Load the CIW into r2 for calling __T3E_MISMATCH

(define_expand "umk_mismatch_args"
  [(set:DI (match_dup 1) (mem:DI (plus:DI (reg:DI 15) (const_int -16))))
   (set:DI (match_dup 2) (mem:DI (plus:DI (match_dup 1) (const_int -32))))
   (set:DI (reg:DI 1) (match_operand:DI 0 "const_int_operand" ""))
   (set:DI (match_dup 3) (plus:DI (mult:DI (reg:DI 25)
					   (const_int 8))
				  (match_dup 2)))
   (set:DI (reg:DI 2) (mem:DI (match_dup 3)))]
  "TARGET_ABI_UNICOSMK"
{
  operands[1] = gen_reg_rtx (DImode);
  operands[2] = gen_reg_rtx (DImode);
  operands[3] = gen_reg_rtx (DImode);
})

(define_insn "arg_home_umk"
  [(unspec [(const_int 0)] UNSPEC_ARG_HOME)
   (use (reg:DI 1))
   (use (reg:DI 2))
   (use (reg:DI 16))
   (use (reg:DI 17))
   (use (reg:DI 18))
   (use (reg:DI 19))
   (use (reg:DI 20))
   (use (reg:DI 21))
   (use (reg:DI 48))
   (use (reg:DI 49))
   (use (reg:DI 50))
   (use (reg:DI 51))
   (use (reg:DI 52))
   (use (reg:DI 53))
   (clobber (mem:BLK (const_int 0)))
   (parallel [
   (clobber (reg:DI 22))
   (clobber (reg:DI 23))
   (clobber (reg:DI 24))
   (clobber (reg:DI 0))
   (clobber (reg:DI 1))
   (clobber (reg:DI 2))
   (clobber (reg:DI 3))
   (clobber (reg:DI 4))
   (clobber (reg:DI 5))
   (clobber (reg:DI 6))
   (clobber (reg:DI 7))
   (clobber (reg:DI 8))])]
  "TARGET_ABI_UNICOSMK"
  "laum $4,__T3E_MISMATCH($31)\;sll $4,32,$4\;lalm $4,__T3E_MISMATCH($4)\;lal $4,__T3E_MISMATCH($4)\;jsr $3,($4)"
  [(set_attr "length" "16")
   (set_attr "type" "multi")])

;; Prefetch data.  
;;
;; On EV4, these instructions are nops -- no load occurs.
;;
;; On EV5, these instructions act as a normal load, and thus can trap
;; if the address is invalid.  The OS may (or may not) handle this in
;; the entMM fault handler and suppress the fault.  If so, then this
;; has the effect of a read prefetch instruction.
;;
;; On EV6, these become official prefetch instructions.

(define_insn "prefetch"
  [(prefetch (match_operand:DI 0 "address_operand" "p")
	     (match_operand:DI 1 "const_int_operand" "n")
	     (match_operand:DI 2 "const_int_operand" "n"))]
  "TARGET_FIXUP_EV5_PREFETCH || alpha_cpu == PROCESSOR_EV6"
{
  /* Interpret "no temporal locality" as this data should be evicted once
     it is used.  The "evict next" alternatives load the data into the cache
     and leave the LRU eviction counter pointing to that block.  */
  static const char * const alt[2][2] = {
    { 
      "ldq $31,%a0",		/* read, evict next */
      "ldl $31,%a0",		/* read, evict last */
    },
    {
      "ldt $f31,%a0",		/* write, evict next */
      "lds $f31,%a0",		/* write, evict last */
    }
  };

  bool write = INTVAL (operands[1]) != 0;
  bool lru = INTVAL (operands[2]) != 0;

  return alt[write][lru];
}
  [(set_attr "type" "ild")])

;; Close the trap shadow of preceding instructions.  This is generated
;; by alpha_reorg.

(define_insn "trapb"
  [(unspec_volatile [(const_int 0)] UNSPECV_TRAPB)]
  ""
  "trapb"
  [(set_attr "type" "misc")])

;; No-op instructions used by machine-dependent reorg to preserve
;; alignment for instruction issue.
;; The Unicos/Mk assembler does not support these opcodes.

(define_insn "nop"
  [(const_int 0)]
  ""
  "bis $31,$31,$31"
  [(set_attr "type" "ilog")])

(define_insn "fnop"
  [(const_int 1)]
  "TARGET_FP"
  "cpys $f31,$f31,$f31"
  [(set_attr "type" "fcpys")])

(define_insn "unop"
  [(const_int 2)]
  ""
  "ldq_u $31,0($30)")

;; On Unicos/Mk we use a macro for aligning code.

(define_insn "realign"
  [(unspec_volatile [(match_operand 0 "immediate_operand" "i")]
		    UNSPECV_REALIGN)]
  ""
{
  if (TARGET_ABI_UNICOSMK)
    return "gcc@code@align %0";
  else
    return ".align %0 #realign";
})

;; Instructions to be emitted from __builtins.

(define_insn "builtin_cmpbge"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(unspec:DI [(match_operand:DI 1 "reg_or_0_operand" "rJ")
		    (match_operand:DI 2 "reg_or_8bit_operand" "rI")]
		   UNSPEC_CMPBGE))]
  ""
  "cmpbge %r1,%2,%0"
  ;; The EV6 data sheets list this as ILOG.  OTOH, EV6 doesn't 
  ;; actually differentiate between ILOG and ICMP in the schedule.
  [(set_attr "type" "icmp")])

(define_expand "builtin_extbl"
  [(match_operand:DI 0 "register_operand" "")
   (match_operand:DI 1 "reg_or_0_operand" "")
   (match_operand:DI 2 "reg_or_8bit_operand" "")]
  ""
{
  rtx (*gen) (rtx, rtx, rtx, rtx);
  if (WORDS_BIG_ENDIAN)
    gen = gen_extxl_be;
  else
    gen = gen_extxl_le;
  emit_insn ((*gen) (operands[0], operands[1], GEN_INT (8), operands[2]));
  DONE;
})

(define_expand "builtin_extwl"
  [(match_operand:DI 0 "register_operand" "")
   (match_operand:DI 1 "reg_or_0_operand" "")
   (match_operand:DI 2 "reg_or_8bit_operand" "")]
  ""
{
  rtx (*gen) (rtx, rtx, rtx, rtx);
  if (WORDS_BIG_ENDIAN)
    gen = gen_extxl_be;
  else
    gen = gen_extxl_le;
  emit_insn ((*gen) (operands[0], operands[1], GEN_INT (16), operands[2]));
  DONE;
})

(define_expand "builtin_extll"
  [(match_operand:DI 0 "register_operand" "")
   (match_operand:DI 1 "reg_or_0_operand" "")
   (match_operand:DI 2 "reg_or_8bit_operand" "")]
  ""
{
  rtx (*gen) (rtx, rtx, rtx, rtx);
  if (WORDS_BIG_ENDIAN)
    gen = gen_extxl_be;
  else
    gen = gen_extxl_le;
  emit_insn ((*gen) (operands[0], operands[1], GEN_INT (32), operands[2]));
  DONE;
})

(define_expand "builtin_extql"
  [(match_operand:DI 0 "register_operand" "")
   (match_operand:DI 1 "reg_or_0_operand" "")
   (match_operand:DI 2 "reg_or_8bit_operand" "")]
  ""
{
  rtx (*gen) (rtx, rtx, rtx, rtx);
  if (WORDS_BIG_ENDIAN)
    gen = gen_extxl_be;
  else
    gen = gen_extxl_le;
  emit_insn ((*gen) (operands[0], operands[1], GEN_INT (64), operands[2]));
  DONE;
})

(define_expand "builtin_extwh"
  [(match_operand:DI 0 "register_operand" "")
   (match_operand:DI 1 "reg_or_0_operand" "")
   (match_operand:DI 2 "reg_or_8bit_operand" "")]
  ""
{
  rtx (*gen) (rtx, rtx, rtx);
  if (WORDS_BIG_ENDIAN)
    gen = gen_extwh_be;
  else
    gen = gen_extwh_le;
  emit_insn ((*gen) (operands[0], operands[1], operands[2]));
  DONE;
})

(define_expand "builtin_extlh"
  [(match_operand:DI 0 "register_operand" "")
   (match_operand:DI 1 "reg_or_0_operand" "")
   (match_operand:DI 2 "reg_or_8bit_operand" "")]
  ""
{
  rtx (*gen) (rtx, rtx, rtx);
  if (WORDS_BIG_ENDIAN)
    gen = gen_extlh_be;
  else
    gen = gen_extlh_le;
  emit_insn ((*gen) (operands[0], operands[1], operands[2]));
  DONE;
})

(define_expand "builtin_extqh"
  [(match_operand:DI 0 "register_operand" "")
   (match_operand:DI 1 "reg_or_0_operand" "")
   (match_operand:DI 2 "reg_or_8bit_operand" "")]
  ""
{
  rtx (*gen) (rtx, rtx, rtx);
  if (WORDS_BIG_ENDIAN)
    gen = gen_extqh_be;
  else
    gen = gen_extqh_le;
  emit_insn ((*gen) (operands[0], operands[1], operands[2]));
  DONE;
})

(define_expand "builtin_insbl"
  [(match_operand:DI 0 "register_operand" "")
   (match_operand:DI 1 "register_operand" "")
   (match_operand:DI 2 "reg_or_8bit_operand" "")]
  ""
{
  rtx (*gen) (rtx, rtx, rtx);
  if (WORDS_BIG_ENDIAN)
    gen = gen_insbl_be;
  else
    gen = gen_insbl_le;
  operands[1] = gen_lowpart (QImode, operands[1]);
  emit_insn ((*gen) (operands[0], operands[1], operands[2]));
  DONE;
})

(define_expand "builtin_inswl"
  [(match_operand:DI 0 "register_operand" "")
   (match_operand:DI 1 "register_operand" "")
   (match_operand:DI 2 "reg_or_8bit_operand" "")]
  ""
{
  rtx (*gen) (rtx, rtx, rtx);
  if (WORDS_BIG_ENDIAN)
    gen = gen_inswl_be;
  else
    gen = gen_inswl_le;
  operands[1] = gen_lowpart (HImode, operands[1]);
  emit_insn ((*gen) (operands[0], operands[1], operands[2]));
  DONE;
})

(define_expand "builtin_insll"
  [(match_operand:DI 0 "register_operand" "")
   (match_operand:DI 1 "register_operand" "")
   (match_operand:DI 2 "reg_or_8bit_operand" "")]
  ""
{
  rtx (*gen) (rtx, rtx, rtx);
  if (WORDS_BIG_ENDIAN)
    gen = gen_insll_be;
  else
    gen = gen_insll_le;
  operands[1] = gen_lowpart (SImode, operands[1]);
  emit_insn ((*gen) (operands[0], operands[1], operands[2]));
  emit_insn ((*gen) (operands[0], operands[1], operands[2]));
  DONE;
})

(define_expand "builtin_insql"
  [(match_operand:DI 0 "register_operand" "")
   (match_operand:DI 1 "reg_or_0_operand" "")
   (match_operand:DI 2 "reg_or_8bit_operand" "")]
  ""
{
  rtx (*gen) (rtx, rtx, rtx);
  if (WORDS_BIG_ENDIAN)
    gen = gen_insql_be;
  else
    gen = gen_insql_le;
  emit_insn ((*gen) (operands[0], operands[1], operands[2]));
  DONE;
})

(define_expand "builtin_inswh"
  [(match_operand:DI 0 "register_operand" "")
   (match_operand:DI 1 "register_operand" "")
   (match_operand:DI 2 "reg_or_8bit_operand" "")]
  ""
{
  emit_insn (gen_insxh (operands[0], operands[1], GEN_INT (16), operands[2]));
  DONE;
})

(define_expand "builtin_inslh"
  [(match_operand:DI 0 "register_operand" "")
   (match_operand:DI 1 "register_operand" "")
   (match_operand:DI 2 "reg_or_8bit_operand" "")]
  ""
{
  emit_insn (gen_insxh (operands[0], operands[1], GEN_INT (32), operands[2]));
  DONE;
})

(define_expand "builtin_insqh"
  [(match_operand:DI 0 "register_operand" "")
   (match_operand:DI 1 "register_operand" "")
   (match_operand:DI 2 "reg_or_8bit_operand" "")]
  ""
{
  emit_insn (gen_insxh (operands[0], operands[1], GEN_INT (64), operands[2]));
  DONE;
})

(define_expand "builtin_mskbl"
  [(match_operand:DI 0 "register_operand" "")
   (match_operand:DI 1 "reg_or_0_operand" "")
   (match_operand:DI 2 "reg_or_8bit_operand" "")]
  ""
{
  rtx (*gen) (rtx, rtx, rtx, rtx);
  rtx mask;
  if (WORDS_BIG_ENDIAN)
    gen = gen_mskxl_be;
  else
    gen = gen_mskxl_le;
  mask = GEN_INT (0xff);
  emit_insn ((*gen) (operands[0], operands[1], mask, operands[2]));
  DONE;
})

(define_expand "builtin_mskwl"
  [(match_operand:DI 0 "register_operand" "")
   (match_operand:DI 1 "reg_or_0_operand" "")
   (match_operand:DI 2 "reg_or_8bit_operand" "")]
  ""
{
  rtx (*gen) (rtx, rtx, rtx, rtx);
  rtx mask;
  if (WORDS_BIG_ENDIAN)
    gen = gen_mskxl_be;
  else
    gen = gen_mskxl_le;
  mask = GEN_INT (0xffff);
  emit_insn ((*gen) (operands[0], operands[1], mask, operands[2]));
  DONE;
})

(define_expand "builtin_mskll"
  [(match_operand:DI 0 "register_operand" "")
   (match_operand:DI 1 "reg_or_0_operand" "")
   (match_operand:DI 2 "reg_or_8bit_operand" "")]
  ""
{
  rtx (*gen) (rtx, rtx, rtx, rtx);
  rtx mask;
  if (WORDS_BIG_ENDIAN)
    gen = gen_mskxl_be;
  else
    gen = gen_mskxl_le;
  mask = immed_double_const (0xffffffff, 0, DImode);
  emit_insn ((*gen) (operands[0], operands[1], mask, operands[2]));
  DONE;
})

(define_expand "builtin_mskql"
  [(match_operand:DI 0 "register_operand" "")
   (match_operand:DI 1 "reg_or_0_operand" "")
   (match_operand:DI 2 "reg_or_8bit_operand" "")]
  ""
{
  rtx (*gen) (rtx, rtx, rtx, rtx);
  rtx mask;
  if (WORDS_BIG_ENDIAN)
    gen = gen_mskxl_be;
  else
    gen = gen_mskxl_le;
  mask = constm1_rtx;
  emit_insn ((*gen) (operands[0], operands[1], mask, operands[2]));
  DONE;
})

(define_expand "builtin_mskwh"
  [(match_operand:DI 0 "register_operand" "")
   (match_operand:DI 1 "register_operand" "")
   (match_operand:DI 2 "reg_or_8bit_operand" "")]
  ""
{
  emit_insn (gen_mskxh (operands[0], operands[1], GEN_INT (16), operands[2]));
  DONE;
})

(define_expand "builtin_msklh"
  [(match_operand:DI 0 "register_operand" "")
   (match_operand:DI 1 "register_operand" "")
   (match_operand:DI 2 "reg_or_8bit_operand" "")]
  ""
{
  emit_insn (gen_mskxh (operands[0], operands[1], GEN_INT (32), operands[2]));
  DONE;
})

(define_expand "builtin_mskqh"
  [(match_operand:DI 0 "register_operand" "")
   (match_operand:DI 1 "register_operand" "")
   (match_operand:DI 2 "reg_or_8bit_operand" "")]
  ""
{
  emit_insn (gen_mskxh (operands[0], operands[1], GEN_INT (64), operands[2]));
  DONE;
})

(define_expand "builtin_zap"
  [(set (match_operand:DI 0 "register_operand" "")
	(and:DI (unspec:DI
		  [(match_operand:DI 2 "reg_or_cint_operand" "")]
		  UNSPEC_ZAP)
		(match_operand:DI 1 "reg_or_cint_operand" "")))]
  ""
{
  if (GET_CODE (operands[2]) == CONST_INT)
    {
      rtx mask = alpha_expand_zap_mask (INTVAL (operands[2]));

      if (mask == const0_rtx)
	{
	  emit_move_insn (operands[0], const0_rtx);
	  DONE;
	}
      if (mask == constm1_rtx)
	{
	  emit_move_insn (operands[0], operands[1]);
	  DONE;
	}

      operands[1] = force_reg (DImode, operands[1]);
      emit_insn (gen_anddi3 (operands[0], operands[1], mask));
      DONE;
    }

  operands[1] = force_reg (DImode, operands[1]);
  operands[2] = gen_lowpart (QImode, operands[2]);
})

(define_insn "*builtin_zap_1"
  [(set (match_operand:DI 0 "register_operand" "=r,r,r,r")
	(and:DI (unspec:DI
		  [(match_operand:QI 2 "reg_or_cint_operand" "n,n,r,r")]
		  UNSPEC_ZAP)
		(match_operand:DI 1 "reg_or_cint_operand" "n,r,J,r")))]
  ""
  "@
   #
   #
   bis $31,$31,%0
   zap %r1,%2,%0"
  [(set_attr "type" "shift,shift,ilog,shift")])

(define_split
  [(set (match_operand:DI 0 "register_operand" "")
	(and:DI (unspec:DI
		  [(match_operand:QI 2 "const_int_operand" "")]
		  UNSPEC_ZAP)
		(match_operand:DI 1 "const_int_operand" "")))]
  ""
  [(const_int 0)]
{
  rtx mask = alpha_expand_zap_mask (INTVAL (operands[2]));
  if (HOST_BITS_PER_WIDE_INT >= 64 || GET_CODE (mask) == CONST_INT)
    operands[1] = gen_int_mode (INTVAL (operands[1]) & INTVAL (mask), DImode);
  else
    {
      HOST_WIDE_INT c_lo = INTVAL (operands[1]);
      HOST_WIDE_INT c_hi = (c_lo < 0 ? -1 : 0);
      operands[1] = immed_double_const (c_lo & CONST_DOUBLE_LOW (mask),
					c_hi & CONST_DOUBLE_HIGH (mask),
					DImode);
    }
  emit_move_insn (operands[0], operands[1]);
  DONE;
})

(define_split
  [(set (match_operand:DI 0 "register_operand" "")
	(and:DI (unspec:DI
		  [(match_operand:QI 2 "const_int_operand" "")]
		  UNSPEC_ZAP)
		(match_operand:DI 1 "register_operand" "")))]
  ""
  [(set (match_dup 0)
	(and:DI (match_dup 1) (match_dup 2)))]
{
  operands[2] = alpha_expand_zap_mask (INTVAL (operands[2]));
  if (operands[2] == const0_rtx)
    {
      emit_move_insn (operands[0], const0_rtx);
      DONE;
    }
  if (operands[2] == constm1_rtx)
    {
      emit_move_insn (operands[0], operands[1]);
      DONE;
    }
})

(define_expand "builtin_zapnot"
  [(set (match_operand:DI 0 "register_operand" "")
	(and:DI (unspec:DI
		  [(not:QI (match_operand:DI 2 "reg_or_cint_operand" ""))]
		  UNSPEC_ZAP)
		(match_operand:DI 1 "reg_or_cint_operand" "")))]
  ""
{
  if (GET_CODE (operands[2]) == CONST_INT)
    {
      rtx mask = alpha_expand_zap_mask (~ INTVAL (operands[2]));

      if (mask == const0_rtx)
	{
	  emit_move_insn (operands[0], const0_rtx);
	  DONE;
	}
      if (mask == constm1_rtx)
	{
	  emit_move_insn (operands[0], operands[1]);
	  DONE;
	}

      operands[1] = force_reg (DImode, operands[1]);
      emit_insn (gen_anddi3 (operands[0], operands[1], mask));
      DONE;
    }

  operands[1] = force_reg (DImode, operands[1]);
  operands[2] = gen_lowpart (QImode, operands[2]);
})

(define_insn "*builtin_zapnot_1"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(and:DI (unspec:DI
                  [(not:QI (match_operand:QI 2 "register_operand" "r"))]
                  UNSPEC_ZAP)
		(match_operand:DI 1 "reg_or_0_operand" "rJ")))]
  ""
  "zapnot %r1,%2,%0"
  [(set_attr "type" "shift")])

(define_insn "builtin_amask"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(unspec:DI [(match_operand:DI 1 "reg_or_8bit_operand" "rI")]
		   UNSPEC_AMASK))]
  ""
  "amask %1,%0"
  [(set_attr "type" "ilog")])

(define_insn "builtin_implver"
  [(set (match_operand:DI 0 "register_operand" "=r")
  	(unspec:DI [(const_int 0)] UNSPEC_IMPLVER))]
  ""
  "implver %0"
  [(set_attr "type" "ilog")])

(define_insn "builtin_rpcc"
  [(set (match_operand:DI 0 "register_operand" "=r")
  	(unspec_volatile:DI [(const_int 0)] UNSPECV_RPCC))]
  ""
  "rpcc %0"
  [(set_attr "type" "ilog")])

(define_expand "builtin_minub8"
  [(match_operand:DI 0 "register_operand" "")
   (match_operand:DI 1 "reg_or_0_operand" "")
   (match_operand:DI 2 "reg_or_0_operand" "")]
  "TARGET_MAX"
{
  alpha_expand_builtin_vector_binop (gen_uminv8qi3, V8QImode, operands[0],
				     operands[1], operands[2]);
  DONE;
})

(define_expand "builtin_minsb8"
  [(match_operand:DI 0 "register_operand" "")
   (match_operand:DI 1 "reg_or_0_operand" "")
   (match_operand:DI 2 "reg_or_0_operand" "")]
  "TARGET_MAX"
{
  alpha_expand_builtin_vector_binop (gen_sminv8qi3, V8QImode, operands[0],
				     operands[1], operands[2]);
  DONE;
})

(define_expand "builtin_minuw4"
  [(match_operand:DI 0 "register_operand" "")
   (match_operand:DI 1 "reg_or_0_operand" "")
   (match_operand:DI 2 "reg_or_0_operand" "")]
  "TARGET_MAX"
{
  alpha_expand_builtin_vector_binop (gen_uminv4hi3, V4HImode, operands[0],
				     operands[1], operands[2]);
  DONE;
})

(define_expand "builtin_minsw4"
  [(match_operand:DI 0 "register_operand" "")
   (match_operand:DI 1 "reg_or_0_operand" "")
   (match_operand:DI 2 "reg_or_0_operand" "")]
  "TARGET_MAX"
{
  alpha_expand_builtin_vector_binop (gen_sminv4hi3, V4HImode, operands[0],
				     operands[1], operands[2]);
  DONE;
})

(define_expand "builtin_maxub8"
  [(match_operand:DI 0 "register_operand" "")
   (match_operand:DI 1 "reg_or_0_operand" "")
   (match_operand:DI 2 "reg_or_0_operand" "")]
  "TARGET_MAX"
{
  alpha_expand_builtin_vector_binop (gen_umaxv8qi3, V8QImode, operands[0],
				     operands[1], operands[2]);
  DONE;
})

(define_expand "builtin_maxsb8"
  [(match_operand:DI 0 "register_operand" "")
   (match_operand:DI 1 "reg_or_0_operand" "")
   (match_operand:DI 2 "reg_or_0_operand" "")]
  "TARGET_MAX"
{
  alpha_expand_builtin_vector_binop (gen_smaxv8qi3, V8QImode, operands[0],
				     operands[1], operands[2]);
  DONE;
})

(define_expand "builtin_maxuw4"
  [(match_operand:DI 0 "register_operand" "")
   (match_operand:DI 1 "reg_or_0_operand" "")
   (match_operand:DI 2 "reg_or_0_operand" "")]
  "TARGET_MAX"
{
  alpha_expand_builtin_vector_binop (gen_umaxv4hi3, V4HImode, operands[0],
				     operands[1], operands[2]);
  DONE;
})

(define_expand "builtin_maxsw4"
  [(match_operand:DI 0 "register_operand" "")
   (match_operand:DI 1 "reg_or_0_operand" "")
   (match_operand:DI 2 "reg_or_0_operand" "")]
  "TARGET_MAX"
{
  alpha_expand_builtin_vector_binop (gen_smaxv4hi3, V4HImode, operands[0],
				     operands[1], operands[2]);
  DONE;
})

(define_insn "builtin_perr"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(unspec:DI [(match_operand:DI 1 "reg_or_0_operand" "%rJ")
		    (match_operand:DI 2 "reg_or_8bit_operand" "rJ")]
		   UNSPEC_PERR))]
  "TARGET_MAX"
  "perr %r1,%r2,%0"
  [(set_attr "type" "mvi")])

(define_expand "builtin_pklb"
  [(set (match_operand:DI 0 "register_operand" "")
	(vec_concat:V8QI
	  (vec_concat:V4QI
	    (truncate:V2QI (match_operand:DI 1 "register_operand" ""))
	    (match_dup 2))
	  (match_dup 3)))]
  "TARGET_MAX"
{
  operands[0] = gen_lowpart (V8QImode, operands[0]);
  operands[1] = gen_lowpart (V2SImode, operands[1]);
  operands[2] = CONST0_RTX (V2QImode);
  operands[3] = CONST0_RTX (V4QImode);
})

(define_insn "*pklb"
  [(set (match_operand:V8QI 0 "register_operand" "=r")
	(vec_concat:V8QI
	  (vec_concat:V4QI
	    (truncate:V2QI (match_operand:V2SI 1 "register_operand" "r"))
	    (match_operand:V2QI 2 "const0_operand" ""))
	  (match_operand:V4QI 3 "const0_operand" "")))]
  "TARGET_MAX"
  "pklb %r1,%0"
  [(set_attr "type" "mvi")])

(define_expand "builtin_pkwb"
  [(set (match_operand:DI 0 "register_operand" "")
	(vec_concat:V8QI
	  (truncate:V4QI (match_operand:DI 1 "register_operand" ""))
	  (match_dup 2)))]
  "TARGET_MAX"
{
  operands[0] = gen_lowpart (V8QImode, operands[0]);
  operands[1] = gen_lowpart (V4HImode, operands[1]);
  operands[2] = CONST0_RTX (V4QImode);
})

(define_insn "*pkwb"
  [(set (match_operand:V8QI 0 "register_operand" "=r")
	(vec_concat:V8QI
	  (truncate:V4QI (match_operand:V4HI 1 "register_operand" "r"))
	  (match_operand:V4QI 2 "const0_operand" "")))]
  "TARGET_MAX"
  "pkwb %r1,%0"
  [(set_attr "type" "mvi")])

(define_expand "builtin_unpkbl"
  [(set (match_operand:DI 0 "register_operand" "")
	(zero_extend:V2SI
	  (vec_select:V2QI (match_operand:DI 1 "register_operand" "")
			   (parallel [(const_int 0) (const_int 1)]))))]
  "TARGET_MAX"
{
  operands[0] = gen_lowpart (V2SImode, operands[0]);
  operands[1] = gen_lowpart (V8QImode, operands[1]);
})

(define_insn "*unpkbl"
  [(set (match_operand:V2SI 0 "register_operand" "=r")
	(zero_extend:V2SI
	  (vec_select:V2QI (match_operand:V8QI 1 "reg_or_0_operand" "rW")
			   (parallel [(const_int 0) (const_int 1)]))))]
  "TARGET_MAX"
  "unpkbl %r1,%0"
  [(set_attr "type" "mvi")])

(define_expand "builtin_unpkbw"
  [(set (match_operand:DI 0 "register_operand" "")
	(zero_extend:V4HI
	  (vec_select:V4QI (match_operand:DI 1 "register_operand" "")
			   (parallel [(const_int 0)
				      (const_int 1)
				      (const_int 2)
				      (const_int 3)]))))]
  "TARGET_MAX"
{
  operands[0] = gen_lowpart (V4HImode, operands[0]);
  operands[1] = gen_lowpart (V8QImode, operands[1]);
})

(define_insn "*unpkbw"
  [(set (match_operand:V4HI 0 "register_operand" "=r")
	(zero_extend:V4HI
	  (vec_select:V4QI (match_operand:V8QI 1 "reg_or_0_operand" "rW")
			   (parallel [(const_int 0)
				      (const_int 1)
				      (const_int 2)
				      (const_int 3)]))))]
  "TARGET_MAX"
  "unpkbw %r1,%0"
  [(set_attr "type" "mvi")])

(include "sync.md")

;; The call patterns are at the end of the file because their
;; wildcard operand0 interferes with nice recognition.

(define_insn "*call_value_osf_1_er"
  [(set (match_operand 0 "" "")
	(call (mem:DI (match_operand:DI 1 "call_operand" "c,R,s"))
	      (match_operand 2 "" "")))
   (use (reg:DI 29))
   (clobber (reg:DI 26))]
  "TARGET_EXPLICIT_RELOCS && TARGET_ABI_OSF"
  "@
   jsr $26,(%1),0\;ldah $29,0($26)\t\t!gpdisp!%*\;lda $29,0($29)\t\t!gpdisp!%*
   bsr $26,%1\t\t!samegp
   ldq $27,%1($29)\t\t!literal!%#\;jsr $26,($27),0\t\t!lituse_jsr!%#\;ldah $29,0($26)\t\t!gpdisp!%*\;lda $29,0($29)\t\t!gpdisp!%*"
  [(set_attr "type" "jsr")
   (set_attr "length" "12,*,16")])

;; We must use peep2 instead of a split because we need accurate life
;; information for $gp.  Consider the case of { bar(); while (1); }.
(define_peephole2
  [(parallel [(set (match_operand 0 "" "")
		   (call (mem:DI (match_operand:DI 1 "call_operand" ""))
		         (match_operand 2 "" "")))
	      (use (reg:DI 29))
	      (clobber (reg:DI 26))])]
  "TARGET_EXPLICIT_RELOCS && TARGET_ABI_OSF && reload_completed
   && ! samegp_function_operand (operands[1], Pmode)
   && (peep2_regno_dead_p (1, 29)
       || find_reg_note (insn, REG_NORETURN, NULL_RTX))"
  [(parallel [(set (match_dup 0)
		   (call (mem:DI (match_dup 3))
			 (match_dup 2)))
	      (set (reg:DI 26) (plus:DI (pc) (const_int 4)))
	      (unspec_volatile [(reg:DI 29)] UNSPECV_BLOCKAGE)
	      (use (match_dup 1))
	      (use (match_dup 4))])]
{
  if (CONSTANT_P (operands[1]))
    {
      operands[3] = gen_rtx_REG (Pmode, 27);
      operands[4] = GEN_INT (alpha_next_sequence_number++);
      emit_insn (gen_movdi_er_high_g (operands[3], pic_offset_table_rtx,
				      operands[1], operands[4]));
    }
  else
    {
      operands[3] = operands[1];
      operands[1] = const0_rtx;
      operands[4] = const0_rtx;
    }
})

(define_peephole2
  [(parallel [(set (match_operand 0 "" "")
		   (call (mem:DI (match_operand:DI 1 "call_operand" ""))
		         (match_operand 2 "" "")))
	      (use (reg:DI 29))
	      (clobber (reg:DI 26))])]
  "TARGET_EXPLICIT_RELOCS && TARGET_ABI_OSF && reload_completed
   && ! samegp_function_operand (operands[1], Pmode)
   && ! (peep2_regno_dead_p (1, 29)
         || find_reg_note (insn, REG_NORETURN, NULL_RTX))"
  [(parallel [(set (match_dup 0)
		   (call (mem:DI (match_dup 3))
			 (match_dup 2)))
	      (set (reg:DI 26) (plus:DI (pc) (const_int 4)))
	      (unspec_volatile [(reg:DI 29)] UNSPECV_BLOCKAGE)
	      (use (match_dup 1))
	      (use (match_dup 5))])
   (set (reg:DI 29)
	(unspec_volatile:DI [(reg:DI 26) (match_dup 4)] UNSPECV_LDGP1))
   (set (reg:DI 29)
	(unspec:DI [(reg:DI 29) (match_dup 4)] UNSPEC_LDGP2))]
{
  if (CONSTANT_P (operands[1]))
    {
      operands[3] = gen_rtx_REG (Pmode, 27);
      operands[5] = GEN_INT (alpha_next_sequence_number++);
      emit_insn (gen_movdi_er_high_g (operands[3], pic_offset_table_rtx,
				      operands[1], operands[5]));
    }
  else
    {
      operands[3] = operands[1];
      operands[1] = const0_rtx;
      operands[5] = const0_rtx;
    }
  operands[4] = GEN_INT (alpha_next_sequence_number++);
})

;; We add a blockage unspec_volatile to prevent insns from moving down
;; from above the call to in between the call and the ldah gpdisp.
(define_insn "*call_value_osf_2_er"
  [(set (match_operand 0 "" "")
	(call (mem:DI (match_operand:DI 1 "register_operand" "c"))
	      (match_operand 2 "" "")))
   (set (reg:DI 26)
	(plus:DI (pc) (const_int 4)))
   (unspec_volatile [(reg:DI 29)] UNSPECV_BLOCKAGE)
   (use (match_operand 3 "" ""))
   (use (match_operand 4 "" ""))]
  "TARGET_EXPLICIT_RELOCS && TARGET_ABI_OSF"
  "jsr $26,(%1),%3%J4"
  [(set_attr "type" "jsr")
   (set_attr "cannot_copy" "true")])

(define_insn "*call_value_osf_1_noreturn"
  [(set (match_operand 0 "" "")
	(call (mem:DI (match_operand:DI 1 "call_operand" "c,R,s"))
	      (match_operand 2 "" "")))
   (use (reg:DI 29))
   (clobber (reg:DI 26))]
  "! TARGET_EXPLICIT_RELOCS && TARGET_ABI_OSF
   && find_reg_note (insn, REG_NORETURN, NULL_RTX)"
  "@
   jsr $26,($27),0%+
   bsr $26,$%1..ng%+
   jsr $26,%1%+"
  [(set_attr "type" "jsr")
   (set_attr "length" "*,*,8")])

(define_insn_and_split "call_value_osf_tlsgd"
  [(set (match_operand 0 "" "")
	(call (mem:DI (match_operand:DI 1 "symbolic_operand" ""))
	      (const_int 0)))
   (unspec [(match_operand:DI 2 "const_int_operand" "")] UNSPEC_TLSGD_CALL)
   (use (reg:DI 29))
   (clobber (reg:DI 26))]
  "HAVE_AS_TLS"
  "#"
  "&& reload_completed"
  [(set (match_dup 3)
	(unspec:DI [(match_dup 5)
		    (match_dup 1)
		    (match_dup 2)] UNSPEC_LITERAL))
   (parallel [(set (match_dup 0)
		   (call (mem:DI (match_dup 3))
			 (const_int 0)))
	      (set (reg:DI 26) (plus:DI (pc) (const_int 4)))
	      (unspec_volatile [(match_dup 5)] UNSPECV_BLOCKAGE)
	      (use (match_dup 1))
	      (use (unspec [(match_dup 2)] UNSPEC_TLSGD_CALL))])
   (set (match_dup 5)
	(unspec_volatile:DI [(reg:DI 26) (match_dup 4)] UNSPECV_LDGP1))
   (set (match_dup 5)
	(unspec:DI [(match_dup 5) (match_dup 4)] UNSPEC_LDGP2))]
{
  operands[3] = gen_rtx_REG (Pmode, 27);
  operands[4] = GEN_INT (alpha_next_sequence_number++);
  operands[5] = pic_offset_table_rtx;
}
  [(set_attr "type" "multi")])

(define_insn_and_split "call_value_osf_tlsldm"
  [(set (match_operand 0 "" "")
	(call (mem:DI (match_operand:DI 1 "symbolic_operand" ""))
	      (const_int 0)))
   (unspec [(match_operand:DI 2 "const_int_operand" "")] UNSPEC_TLSLDM_CALL)
   (use (reg:DI 29))
   (clobber (reg:DI 26))]
  "HAVE_AS_TLS"
  "#"
  "&& reload_completed"
  [(set (match_dup 3)
	(unspec:DI [(match_dup 5)
		    (match_dup 1)
		    (match_dup 2)] UNSPEC_LITERAL))
   (parallel [(set (match_dup 0)
		   (call (mem:DI (match_dup 3))
			 (const_int 0)))
	      (set (reg:DI 26) (plus:DI (pc) (const_int 4)))
	      (unspec_volatile [(match_dup 5)] UNSPECV_BLOCKAGE)
	      (use (match_dup 1))
	      (use (unspec [(match_dup 2)] UNSPEC_TLSLDM_CALL))])
   (set (reg:DI 29)
	(unspec_volatile:DI [(reg:DI 26) (match_dup 4)] UNSPECV_LDGP1))
   (set (reg:DI 29)
	(unspec:DI [(reg:DI 29) (match_dup 4)] UNSPEC_LDGP2))]
{
  operands[3] = gen_rtx_REG (Pmode, 27);
  operands[4] = GEN_INT (alpha_next_sequence_number++);
  operands[5] = pic_offset_table_rtx;
}
  [(set_attr "type" "multi")])

(define_insn "*call_value_osf_1"
  [(set (match_operand 0 "" "")
	(call (mem:DI (match_operand:DI 1 "call_operand" "c,R,s"))
	      (match_operand 2 "" "")))
   (use (reg:DI 29))
   (clobber (reg:DI 26))]
  "! TARGET_EXPLICIT_RELOCS && TARGET_ABI_OSF"
  "@
   jsr $26,($27),0\;ldgp $29,0($26)
   bsr $26,$%1..ng
   jsr $26,%1\;ldgp $29,0($26)"
  [(set_attr "type" "jsr")
   (set_attr "length" "12,*,16")])

(define_insn "*sibcall_value_osf_1_er"
  [(set (match_operand 0 "" "")
	(call (mem:DI (match_operand:DI 1 "symbolic_operand" "R,s"))
	      (match_operand 2 "" "")))
   (unspec [(reg:DI 29)] UNSPEC_SIBCALL)]
  "TARGET_EXPLICIT_RELOCS && TARGET_ABI_OSF"
  "@
   br $31,%1\t\t!samegp
   ldq $27,%1($29)\t\t!literal!%#\;jmp $31,($27),%1\t\t!lituse_jsr!%#"
  [(set_attr "type" "jsr")
   (set_attr "length" "*,8")])

(define_insn "*sibcall_value_osf_1"
  [(set (match_operand 0 "" "")
	(call (mem:DI (match_operand:DI 1 "symbolic_operand" "R,s"))
	      (match_operand 2 "" "")))
   (unspec [(reg:DI 29)] UNSPEC_SIBCALL)]
  "! TARGET_EXPLICIT_RELOCS && TARGET_ABI_OSF"
  "@
   br $31,$%1..ng
   lda $27,%1\;jmp $31,($27),%1"
  [(set_attr "type" "jsr")
   (set_attr "length" "*,8")])

(define_insn "*call_value_nt_1"
  [(set (match_operand 0 "" "")
	(call (mem:DI (match_operand:DI 1 "call_operand" "r,R,s"))
	      (match_operand 2 "" "")))
   (clobber (reg:DI 26))]
  "TARGET_ABI_WINDOWS_NT"
  "@
   jsr $26,(%1)
   bsr $26,%1
   jsr $26,%1"
  [(set_attr "type" "jsr")
   (set_attr "length" "*,*,12")])

; GAS relies on the order and position of instructions output below in order
; to generate relocs for VMS link to potentially optimize the call.
; Please do not molest.
(define_insn "*call_value_vms_1"
  [(set (match_operand 0 "" "")
	(call (mem:DI (match_operand:DI 1 "call_operand" "r,s"))
	      (match_operand 2 "" "")))
   (use (match_operand:DI 3 "nonmemory_operand" "r,n"))
   (use (reg:DI 25))
   (use (reg:DI 26))
   (clobber (reg:DI 27))]
  "TARGET_ABI_OPEN_VMS"
{
  switch (which_alternative)
    {
    case 0:
   	return "mov %3,$27\;jsr $26,0\;ldq $27,0($29)";
    case 1:
	operands [3] = alpha_use_linkage (operands [1], cfun->decl, 1, 0);
	operands [4] = alpha_use_linkage (operands [1], cfun->decl, 0, 0);
   	return "ldq $26,%4\;ldq $27,%3\;jsr $26,%1\;ldq $27,0($29)";
    default:
      gcc_unreachable ();
    }
}
  [(set_attr "type" "jsr")
   (set_attr "length" "12,16")])

(define_insn "*call_value_umk"
  [(set (match_operand 0 "" "")
	(call (mem:DI (match_operand:DI 1 "call_operand" "r"))
	      (match_operand 2 "" "")))
   (use (reg:DI 25))
   (clobber (reg:DI 26))]
  "TARGET_ABI_UNICOSMK"
  "jsr $26,(%1)"
  [(set_attr "type" "jsr")])
