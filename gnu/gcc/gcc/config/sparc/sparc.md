;; Machine description for SPARC chip for GCC
;;  Copyright (C) 1987, 1988, 1989, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
;;  1999, 2000, 2001, 2002, 2003, 2004, 2005,2006 Free Software Foundation, Inc.
;;  Contributed by Michael Tiemann (tiemann@cygnus.com)
;;  64-bit SPARC-V9 support by Michael Tiemann, Jim Wilson, and Doug Evans,
;;  at Cygnus Support.

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

(define_constants
  [(UNSPEC_MOVE_PIC		0)
   (UNSPEC_UPDATE_RETURN	1)
   (UNSPEC_LOAD_PCREL_SYM	2)
   (UNSPEC_MOVE_PIC_LABEL	5)
   (UNSPEC_SETH44		6)
   (UNSPEC_SETM44		7)
   (UNSPEC_SETHH		9)
   (UNSPEC_SETLM		10)
   (UNSPEC_EMB_HISUM		11)
   (UNSPEC_EMB_TEXTUHI		13)
   (UNSPEC_EMB_TEXTHI		14)
   (UNSPEC_EMB_TEXTULO		15)
   (UNSPEC_EMB_SETHM		18)

   (UNSPEC_TLSGD		30)
   (UNSPEC_TLSLDM		31)
   (UNSPEC_TLSLDO		32)
   (UNSPEC_TLSIE		33)
   (UNSPEC_TLSLE		34)
   (UNSPEC_TLSLD_BASE		35)

   (UNSPEC_FPACK16	 	40)
   (UNSPEC_FPACK32		41)
   (UNSPEC_FPACKFIX		42)
   (UNSPEC_FEXPAND		43)
   (UNSPEC_FPMERGE		44)
   (UNSPEC_MUL16AL		45)
   (UNSPEC_MUL8UL		46)
   (UNSPEC_MULDUL		47)
   (UNSPEC_ALIGNDATA		48)
   (UNSPEC_ALIGNADDR		49)
   (UNSPEC_PDIST		50)

   (UNSPEC_SP_SET		60)
   (UNSPEC_SP_TEST		61)
  ])

(define_constants
  [(UNSPECV_BLOCKAGE		0)
   (UNSPECV_FLUSHW		1)
   (UNSPECV_GOTO		2)
   (UNSPECV_FLUSH		4)
   (UNSPECV_SETJMP		5)
   (UNSPECV_SAVEW		6)
   (UNSPECV_MEMBAR		7)
   (UNSPECV_CAS			8)
   (UNSPECV_SWAP		9)
   (UNSPECV_LDSTUB		10)
  ])

;; The upper 32 fp regs on the v9 can't hold SFmode values.  To deal with this
;; a second register class, EXTRA_FP_REGS, exists for the v9 chip.  The name
;; is a bit of a misnomer as it covers all 64 fp regs.  The corresponding
;; constraint letter is 'e'.  To avoid any confusion, 'e' is used instead of
;; 'f' for all DF/TFmode values, including those that are specific to the v8.


;; Attribute for cpu type.
;; These must match the values for enum processor_type in sparc.h.
(define_attr "cpu"
  "v7,
   cypress,
   v8,
   supersparc,
   sparclite,f930,f934,
   hypersparc,sparclite86x,
   sparclet,tsc701,
   v9,
   ultrasparc,
   ultrasparc3,
   niagara"
  (const (symbol_ref "sparc_cpu_attr")))

;; Attribute for the instruction set.
;; At present we only need to distinguish v9/!v9, but for clarity we
;; test TARGET_V8 too.
(define_attr "isa" "v7,v8,v9,sparclet"
 (const
  (cond [(symbol_ref "TARGET_V9") (const_string "v9")
	 (symbol_ref "TARGET_V8") (const_string "v8")
	 (symbol_ref "TARGET_SPARCLET") (const_string "sparclet")]
	(const_string "v7"))))

;; Insn type.
(define_attr "type"
  "ialu,compare,shift,
   load,sload,store,
   uncond_branch,branch,call,sibcall,call_no_delay_slot,return,
   imul,idiv,
   fpload,fpstore,
   fp,fpmove,
   fpcmove,fpcrmove,
   fpcmp,
   fpmul,fpdivs,fpdivd,
   fpsqrts,fpsqrtd,
   fga,fgm_pack,fgm_mul,fgm_pdist,fgm_cmp,
   cmove,
   ialuX,
   multi,savew,flushw,iflush,trap"
  (const_string "ialu"))

;; True if branch/call has empty delay slot and will emit a nop in it
(define_attr "empty_delay_slot" "false,true"
  (symbol_ref "empty_delay_slot (insn)"))

(define_attr "branch_type" "none,icc,fcc,reg"
  (const_string "none"))

(define_attr "pic" "false,true"
  (symbol_ref "flag_pic != 0"))

(define_attr "calls_alloca" "false,true"
  (symbol_ref "current_function_calls_alloca != 0"))

(define_attr "calls_eh_return" "false,true"
   (symbol_ref "current_function_calls_eh_return !=0 "))
   
(define_attr "leaf_function" "false,true"
  (symbol_ref "current_function_uses_only_leaf_regs != 0"))

(define_attr "delayed_branch" "false,true"
  (symbol_ref "flag_delayed_branch != 0"))

;; Length (in # of insns).
;; Beware that setting a length greater or equal to 3 for conditional branches
;; has a side-effect (see output_cbranch and output_v9branch).
(define_attr "length" ""
  (cond [(eq_attr "type" "uncond_branch,call")
	   (if_then_else (eq_attr "empty_delay_slot" "true")
	     (const_int 2)
	     (const_int 1))
	 (eq_attr "type" "sibcall")
	   (if_then_else (eq_attr "leaf_function" "true")
	     (if_then_else (eq_attr "empty_delay_slot" "true")
	       (const_int 3)
	       (const_int 2))
	     (if_then_else (eq_attr "empty_delay_slot" "true")
	       (const_int 2)
	       (const_int 1)))
	 (eq_attr "branch_type" "icc")
	   (if_then_else (match_operand 0 "noov_compare64_operator" "")
	     (if_then_else (lt (pc) (match_dup 1))
	       (if_then_else (lt (minus (match_dup 1) (pc)) (const_int 260000))
		 (if_then_else (eq_attr "empty_delay_slot" "true")
		   (const_int 2)
		   (const_int 1))
		 (if_then_else (eq_attr "empty_delay_slot" "true")
		   (const_int 4)
		   (const_int 3)))
	       (if_then_else (lt (minus (pc) (match_dup 1)) (const_int 260000))
		 (if_then_else (eq_attr "empty_delay_slot" "true")
		   (const_int 2)
		   (const_int 1))
		 (if_then_else (eq_attr "empty_delay_slot" "true")
		   (const_int 4)
		   (const_int 3))))
	     (if_then_else (eq_attr "empty_delay_slot" "true")
	       (const_int 2)
	       (const_int 1)))
	 (eq_attr "branch_type" "fcc")
	   (if_then_else (match_operand 0 "fcc0_register_operand" "")
	     (if_then_else (eq_attr "empty_delay_slot" "true")
	       (if_then_else (eq (symbol_ref "TARGET_V9") (const_int 0))
		 (const_int 3)
		 (const_int 2))
	       (if_then_else (eq (symbol_ref "TARGET_V9") (const_int 0))
		 (const_int 2)
		 (const_int 1)))
	     (if_then_else (lt (pc) (match_dup 2))
	       (if_then_else (lt (minus (match_dup 2) (pc)) (const_int 260000))
		 (if_then_else (eq_attr "empty_delay_slot" "true")
		   (const_int 2)
		   (const_int 1))
		 (if_then_else (eq_attr "empty_delay_slot" "true")
		   (const_int 4)
		   (const_int 3)))
	       (if_then_else (lt (minus (pc) (match_dup 2)) (const_int 260000))
		 (if_then_else (eq_attr "empty_delay_slot" "true")
		   (const_int 2)
		   (const_int 1))
		 (if_then_else (eq_attr "empty_delay_slot" "true")
		   (const_int 4)
		   (const_int 3)))))
	 (eq_attr "branch_type" "reg")
	   (if_then_else (lt (pc) (match_dup 2))
	     (if_then_else (lt (minus (match_dup 2) (pc)) (const_int 32000))
	       (if_then_else (eq_attr "empty_delay_slot" "true")
		 (const_int 2)
		 (const_int 1))
	       (if_then_else (eq_attr "empty_delay_slot" "true")
		 (const_int 4)
		 (const_int 3)))
	     (if_then_else (lt (minus (pc) (match_dup 2)) (const_int 32000))
	       (if_then_else (eq_attr "empty_delay_slot" "true")
		 (const_int 2)
		 (const_int 1))
	       (if_then_else (eq_attr "empty_delay_slot" "true")
		 (const_int 4)
		 (const_int 3))))
	 ] (const_int 1)))

;; FP precision.
(define_attr "fptype" "single,double"
  (const_string "single"))

;; UltraSPARC-III integer load type.
(define_attr "us3load_type" "2cycle,3cycle"
  (const_string "2cycle"))

(define_asm_attributes
  [(set_attr "length" "2")
   (set_attr "type" "multi")])

;; Attributes for instruction and branch scheduling
(define_attr "tls_call_delay" "false,true"
  (symbol_ref "tls_call_delay (insn)"))

(define_attr "in_call_delay" "false,true"
  (cond [(eq_attr "type" "uncond_branch,branch,call,sibcall,call_no_delay_slot,multi")
	 	(const_string "false")
	 (eq_attr "type" "load,fpload,store,fpstore")
	 	(if_then_else (eq_attr "length" "1")
			      (const_string "true")
			      (const_string "false"))]
	(if_then_else (and (eq_attr "length" "1")
			   (eq_attr "tls_call_delay" "true"))
		      (const_string "true")
		      (const_string "false"))))

(define_attr "eligible_for_sibcall_delay" "false,true"
  (symbol_ref "eligible_for_sibcall_delay (insn)"))

(define_attr "eligible_for_return_delay" "false,true"
  (symbol_ref "eligible_for_return_delay (insn)"))

;; ??? !v9: Should implement the notion of predelay slots for floating-point
;; branches.  This would allow us to remove the nop always inserted before
;; a floating point branch.

;; ??? It is OK for fill_simple_delay_slots to put load/store instructions
;; in a delay slot, but it is not OK for fill_eager_delay_slots to do so.
;; This is because doing so will add several pipeline stalls to the path
;; that the load/store did not come from.  Unfortunately, there is no way
;; to prevent fill_eager_delay_slots from using load/store without completely
;; disabling them.  For the SPEC benchmark set, this is a serious lose,
;; because it prevents us from moving back the final store of inner loops.

(define_attr "in_branch_delay" "false,true"
  (if_then_else (and (eq_attr "type" "!uncond_branch,branch,call,sibcall,call_no_delay_slot,multi")
		     (eq_attr "length" "1"))
		(const_string "true")
		(const_string "false")))

(define_attr "in_uncond_branch_delay" "false,true"
  (if_then_else (and (eq_attr "type" "!uncond_branch,branch,call,sibcall,call_no_delay_slot,multi")
		     (eq_attr "length" "1"))
		(const_string "true")
		(const_string "false")))

(define_attr "in_annul_branch_delay" "false,true"
  (if_then_else (and (eq_attr "type" "!uncond_branch,branch,call,sibcall,call_no_delay_slot,multi")
		     (eq_attr "length" "1"))
		(const_string "true")
		(const_string "false")))

(define_delay (eq_attr "type" "call")
  [(eq_attr "in_call_delay" "true") (nil) (nil)])

(define_delay (eq_attr "type" "sibcall")
  [(eq_attr "eligible_for_sibcall_delay" "true") (nil) (nil)])

(define_delay (eq_attr "type" "branch")
  [(eq_attr "in_branch_delay" "true")
   (nil) (eq_attr "in_annul_branch_delay" "true")])

(define_delay (eq_attr "type" "uncond_branch")
  [(eq_attr "in_uncond_branch_delay" "true")
   (nil) (nil)])

(define_delay (eq_attr "type" "return")
  [(eq_attr "eligible_for_return_delay" "true") (nil) (nil)])


;; Include SPARC DFA schedulers

(include "cypress.md")
(include "supersparc.md")
(include "hypersparc.md")
(include "sparclet.md")
(include "ultra1_2.md")
(include "ultra3.md")
(include "niagara.md")


;; Operand and operator predicates.

(include "predicates.md")


;; Compare instructions.

;; We generate RTL for comparisons and branches by having the cmpxx 
;; patterns store away the operands.  Then, the scc and bcc patterns
;; emit RTL for both the compare and the branch.
;;
;; We do this because we want to generate different code for an sne and
;; seq insn.  In those cases, if the second operand of the compare is not
;; const0_rtx, we want to compute the xor of the two operands and test
;; it against zero.
;;
;; We start with the DEFINE_EXPANDs, then the DEFINE_INSNs to match
;; the patterns.  Finally, we have the DEFINE_SPLITs for some of the scc
;; insns that actually require more than one machine instruction.

(define_expand "cmpsi"
  [(set (reg:CC 100)
	(compare:CC (match_operand:SI 0 "compare_operand" "")
		    (match_operand:SI 1 "arith_operand" "")))]
  ""
{
  if (GET_CODE (operands[0]) == ZERO_EXTRACT && operands[1] != const0_rtx)
    operands[0] = force_reg (SImode, operands[0]);

  sparc_compare_op0 = operands[0];
  sparc_compare_op1 = operands[1];
  DONE;
})

(define_expand "cmpdi"
  [(set (reg:CCX 100)
	(compare:CCX (match_operand:DI 0 "compare_operand" "")
		     (match_operand:DI 1 "arith_operand" "")))]
  "TARGET_ARCH64"
{
  if (GET_CODE (operands[0]) == ZERO_EXTRACT && operands[1] != const0_rtx)
    operands[0] = force_reg (DImode, operands[0]);

  sparc_compare_op0 = operands[0];
  sparc_compare_op1 = operands[1];
  DONE;
})

(define_expand "cmpsf"
  ;; The 96 here isn't ever used by anyone.
  [(set (reg:CCFP 96)
	(compare:CCFP (match_operand:SF 0 "register_operand" "")
		      (match_operand:SF 1 "register_operand" "")))]
  "TARGET_FPU"
{
  sparc_compare_op0 = operands[0];
  sparc_compare_op1 = operands[1];
  DONE;
})

(define_expand "cmpdf"
  ;; The 96 here isn't ever used by anyone.
  [(set (reg:CCFP 96)
	(compare:CCFP (match_operand:DF 0 "register_operand" "")
		      (match_operand:DF 1 "register_operand" "")))]
  "TARGET_FPU"
{
  sparc_compare_op0 = operands[0];
  sparc_compare_op1 = operands[1];
  DONE;
})

(define_expand "cmptf"
  ;; The 96 here isn't ever used by anyone.
  [(set (reg:CCFP 96)
	(compare:CCFP (match_operand:TF 0 "register_operand" "")
		      (match_operand:TF 1 "register_operand" "")))]
  "TARGET_FPU"
{
  sparc_compare_op0 = operands[0];
  sparc_compare_op1 = operands[1];
  DONE;
})

;; Now the compare DEFINE_INSNs.

(define_insn "*cmpsi_insn"
  [(set (reg:CC 100)
	(compare:CC (match_operand:SI 0 "register_operand" "r")
		    (match_operand:SI 1 "arith_operand" "rI")))]
  ""
  "cmp\t%0, %1"
  [(set_attr "type" "compare")])

(define_insn "*cmpdi_sp64"
  [(set (reg:CCX 100)
	(compare:CCX (match_operand:DI 0 "register_operand" "r")
		     (match_operand:DI 1 "arith_operand" "rI")))]
  "TARGET_ARCH64"
  "cmp\t%0, %1"
  [(set_attr "type" "compare")])

(define_insn "*cmpsf_fpe"
  [(set (match_operand:CCFPE 0 "fcc_register_operand" "=c")
	(compare:CCFPE (match_operand:SF 1 "register_operand" "f")
		       (match_operand:SF 2 "register_operand" "f")))]
  "TARGET_FPU"
{
  if (TARGET_V9)
    return "fcmpes\t%0, %1, %2";
  return "fcmpes\t%1, %2";
}
  [(set_attr "type" "fpcmp")])

(define_insn "*cmpdf_fpe"
  [(set (match_operand:CCFPE 0 "fcc_register_operand" "=c")
	(compare:CCFPE (match_operand:DF 1 "register_operand" "e")
		       (match_operand:DF 2 "register_operand" "e")))]
  "TARGET_FPU"
{
  if (TARGET_V9)
    return "fcmped\t%0, %1, %2";
  return "fcmped\t%1, %2";
}
  [(set_attr "type" "fpcmp")
   (set_attr "fptype" "double")])

(define_insn "*cmptf_fpe"
  [(set (match_operand:CCFPE 0 "fcc_register_operand" "=c")
	(compare:CCFPE (match_operand:TF 1 "register_operand" "e")
		       (match_operand:TF 2 "register_operand" "e")))]
  "TARGET_FPU && TARGET_HARD_QUAD"
{
  if (TARGET_V9)
    return "fcmpeq\t%0, %1, %2";
  return "fcmpeq\t%1, %2";
}
  [(set_attr "type" "fpcmp")])

(define_insn "*cmpsf_fp"
  [(set (match_operand:CCFP 0 "fcc_register_operand" "=c")
	(compare:CCFP (match_operand:SF 1 "register_operand" "f")
		      (match_operand:SF 2 "register_operand" "f")))]
  "TARGET_FPU"
{
  if (TARGET_V9)
    return "fcmps\t%0, %1, %2";
  return "fcmps\t%1, %2";
}
  [(set_attr "type" "fpcmp")])

(define_insn "*cmpdf_fp"
  [(set (match_operand:CCFP 0 "fcc_register_operand" "=c")
	(compare:CCFP (match_operand:DF 1 "register_operand" "e")
		      (match_operand:DF 2 "register_operand" "e")))]
  "TARGET_FPU"
{
  if (TARGET_V9)
    return "fcmpd\t%0, %1, %2";
  return "fcmpd\t%1, %2";
}
  [(set_attr "type" "fpcmp")
   (set_attr "fptype" "double")])

(define_insn "*cmptf_fp"
  [(set (match_operand:CCFP 0 "fcc_register_operand" "=c")
	(compare:CCFP (match_operand:TF 1 "register_operand" "e")
		      (match_operand:TF 2 "register_operand" "e")))]
  "TARGET_FPU && TARGET_HARD_QUAD"
{
  if (TARGET_V9)
    return "fcmpq\t%0, %1, %2";
  return "fcmpq\t%1, %2";
}
  [(set_attr "type" "fpcmp")])

;; Next come the scc insns.  For seq, sne, sgeu, and sltu, we can do this
;; without jumps using the addx/subx instructions.  For seq/sne on v9 we use
;; the same code as v8 (the addx/subx method has more applications).  The
;; exception to this is "reg != 0" which can be done in one instruction on v9
;; (so we do it).  For the rest, on v9 we use conditional moves; on v8, we do
;; branches.

;; Seq_special[_xxx] and sne_special[_xxx] clobber the CC reg, because they
;; generate addcc/subcc instructions.

(define_expand "seqsi_special"
  [(set (match_dup 3)
	(xor:SI (match_operand:SI 1 "register_operand" "")
		(match_operand:SI 2 "register_operand" "")))
   (parallel [(set (match_operand:SI 0 "register_operand" "")
		   (eq:SI (match_dup 3) (const_int 0)))
	      (clobber (reg:CC 100))])]
  ""
  { operands[3] = gen_reg_rtx (SImode); })

(define_expand "seqdi_special"
  [(set (match_dup 3)
	(xor:DI (match_operand:DI 1 "register_operand" "")
		(match_operand:DI 2 "register_operand" "")))
   (set (match_operand:DI 0 "register_operand" "")
	(eq:DI (match_dup 3) (const_int 0)))]
  "TARGET_ARCH64"
  { operands[3] = gen_reg_rtx (DImode); })

(define_expand "snesi_special"
  [(set (match_dup 3)
	(xor:SI (match_operand:SI 1 "register_operand" "")
		(match_operand:SI 2 "register_operand" "")))
   (parallel [(set (match_operand:SI 0 "register_operand" "")
		   (ne:SI (match_dup 3) (const_int 0)))
	      (clobber (reg:CC 100))])]
  ""
  { operands[3] = gen_reg_rtx (SImode); })

(define_expand "snedi_special"
  [(set (match_dup 3)
	(xor:DI (match_operand:DI 1 "register_operand" "")
		(match_operand:DI 2 "register_operand" "")))
   (set (match_operand:DI 0 "register_operand" "")
	(ne:DI (match_dup 3) (const_int 0)))]
  "TARGET_ARCH64"
  { operands[3] = gen_reg_rtx (DImode); })

(define_expand "seqdi_special_trunc"
  [(set (match_dup 3)
	(xor:DI (match_operand:DI 1 "register_operand" "")
		(match_operand:DI 2 "register_operand" "")))
   (set (match_operand:SI 0 "register_operand" "")
	(eq:SI (match_dup 3) (const_int 0)))]
  "TARGET_ARCH64"
  { operands[3] = gen_reg_rtx (DImode); })

(define_expand "snedi_special_trunc"
  [(set (match_dup 3)
	(xor:DI (match_operand:DI 1 "register_operand" "")
		(match_operand:DI 2 "register_operand" "")))
   (set (match_operand:SI 0 "register_operand" "")
	(ne:SI (match_dup 3) (const_int 0)))]
  "TARGET_ARCH64"
  { operands[3] = gen_reg_rtx (DImode); })

(define_expand "seqsi_special_extend"
  [(set (match_dup 3)
	(xor:SI (match_operand:SI 1 "register_operand" "")
		(match_operand:SI 2 "register_operand" "")))
   (parallel [(set (match_operand:DI 0 "register_operand" "")
		   (eq:DI (match_dup 3) (const_int 0)))
	      (clobber (reg:CC 100))])]
  "TARGET_ARCH64"
  { operands[3] = gen_reg_rtx (SImode); })

(define_expand "snesi_special_extend"
  [(set (match_dup 3)
	(xor:SI (match_operand:SI 1 "register_operand" "")
		(match_operand:SI 2 "register_operand" "")))
   (parallel [(set (match_operand:DI 0 "register_operand" "")
		   (ne:DI (match_dup 3) (const_int 0)))
	      (clobber (reg:CC 100))])]
  "TARGET_ARCH64"
  { operands[3] = gen_reg_rtx (SImode); })

;; ??? v9: Operand 0 needs a mode, so SImode was chosen.
;; However, the code handles both SImode and DImode.
(define_expand "seq"
  [(set (match_operand:SI 0 "int_register_operand" "")
	(eq:SI (match_dup 1) (const_int 0)))]
  ""
{
  if (GET_MODE (sparc_compare_op0) == SImode)
    {
      rtx pat;

      if (GET_MODE (operands[0]) == SImode)
	pat = gen_seqsi_special (operands[0], sparc_compare_op0,
				 sparc_compare_op1);
      else if (! TARGET_ARCH64)
	FAIL;
      else
	pat = gen_seqsi_special_extend (operands[0], sparc_compare_op0,
					sparc_compare_op1);
      emit_insn (pat);
      DONE;
    }
  else if (GET_MODE (sparc_compare_op0) == DImode)
    {
      rtx pat;

      if (! TARGET_ARCH64)
	FAIL;
      else if (GET_MODE (operands[0]) == SImode)
	pat = gen_seqdi_special_trunc (operands[0], sparc_compare_op0,
				       sparc_compare_op1);
      else
	pat = gen_seqdi_special (operands[0], sparc_compare_op0,
				 sparc_compare_op1);
      emit_insn (pat);
      DONE;
    }
  else if (GET_MODE (sparc_compare_op0) == TFmode && ! TARGET_HARD_QUAD)
    {
      sparc_emit_float_lib_cmp (sparc_compare_op0, sparc_compare_op1, EQ);
      emit_jump_insn (gen_sne (operands[0]));
      DONE;
    }
  else if (TARGET_V9)
    {
      if (gen_v9_scc (EQ, operands))
	DONE;
      /* fall through */
    }
  FAIL;
})

;; ??? v9: Operand 0 needs a mode, so SImode was chosen.
;; However, the code handles both SImode and DImode.
(define_expand "sne"
  [(set (match_operand:SI 0 "int_register_operand" "")
	(ne:SI (match_dup 1) (const_int 0)))]
  ""
{
  if (GET_MODE (sparc_compare_op0) == SImode)
    {
      rtx pat;

      if (GET_MODE (operands[0]) == SImode)
	pat = gen_snesi_special (operands[0], sparc_compare_op0,
				 sparc_compare_op1);
      else if (! TARGET_ARCH64)
	FAIL;
      else
	pat = gen_snesi_special_extend (operands[0], sparc_compare_op0,
					sparc_compare_op1);
      emit_insn (pat);
      DONE;
    }
  else if (GET_MODE (sparc_compare_op0) == DImode)
    {
      rtx pat;

      if (! TARGET_ARCH64)
	FAIL;
      else if (GET_MODE (operands[0]) == SImode)
	pat = gen_snedi_special_trunc (operands[0], sparc_compare_op0,
				       sparc_compare_op1);
      else
	pat = gen_snedi_special (operands[0], sparc_compare_op0,
				 sparc_compare_op1);
      emit_insn (pat);
      DONE;
    }
  else if (GET_MODE (sparc_compare_op0) == TFmode && ! TARGET_HARD_QUAD)
    {
      sparc_emit_float_lib_cmp (sparc_compare_op0, sparc_compare_op1, NE);
      emit_jump_insn (gen_sne (operands[0]));
      DONE;
    }
  else if (TARGET_V9)
    {
      if (gen_v9_scc (NE, operands))
	DONE;
      /* fall through */
    }
  FAIL;
})

(define_expand "sgt"
  [(set (match_operand:SI 0 "int_register_operand" "")
	(gt:SI (match_dup 1) (const_int 0)))]
  ""
{
  if (GET_MODE (sparc_compare_op0) == TFmode && ! TARGET_HARD_QUAD)
    {
      sparc_emit_float_lib_cmp (sparc_compare_op0, sparc_compare_op1, GT);
      emit_jump_insn (gen_sne (operands[0]));
      DONE;
    }
  else if (TARGET_V9)
    {
      if (gen_v9_scc (GT, operands))
	DONE;
      /* fall through */
    }
  FAIL;
})

(define_expand "slt"
  [(set (match_operand:SI 0 "int_register_operand" "")
	(lt:SI (match_dup 1) (const_int 0)))]
  ""
{
  if (GET_MODE (sparc_compare_op0) == TFmode && ! TARGET_HARD_QUAD)
    {
      sparc_emit_float_lib_cmp (sparc_compare_op0, sparc_compare_op1, LT);
      emit_jump_insn (gen_sne (operands[0]));
      DONE;
    }
  else if (TARGET_V9)
    {
      if (gen_v9_scc (LT, operands))
	DONE;
      /* fall through */
    }
  FAIL;
})

(define_expand "sge"
  [(set (match_operand:SI 0 "int_register_operand" "")
	(ge:SI (match_dup 1) (const_int 0)))]
  ""
{
  if (GET_MODE (sparc_compare_op0) == TFmode && ! TARGET_HARD_QUAD)
    {
      sparc_emit_float_lib_cmp (sparc_compare_op0, sparc_compare_op1, GE);
      emit_jump_insn (gen_sne (operands[0]));
      DONE;
    }
  else if (TARGET_V9)
    {
      if (gen_v9_scc (GE, operands))
	DONE;
      /* fall through */
    }
  FAIL;
})

(define_expand "sle"
  [(set (match_operand:SI 0 "int_register_operand" "")
	(le:SI (match_dup 1) (const_int 0)))]
  ""
{
  if (GET_MODE (sparc_compare_op0) == TFmode && ! TARGET_HARD_QUAD)
    {
      sparc_emit_float_lib_cmp (sparc_compare_op0, sparc_compare_op1, LE);
      emit_jump_insn (gen_sne (operands[0]));
      DONE;
    }
  else if (TARGET_V9)
    {
      if (gen_v9_scc (LE, operands))
	DONE;
      /* fall through */
    }
  FAIL;
})

(define_expand "sgtu"
  [(set (match_operand:SI 0 "int_register_operand" "")
	(gtu:SI (match_dup 1) (const_int 0)))]
  ""
{
  if (! TARGET_V9)
    {
      rtx tem, pat;

      /* We can do ltu easily, so if both operands are registers, swap them and
	 do a LTU.  */
      if ((GET_CODE (sparc_compare_op0) == REG
	   || GET_CODE (sparc_compare_op0) == SUBREG)
	  && (GET_CODE (sparc_compare_op1) == REG
	      || GET_CODE (sparc_compare_op1) == SUBREG))
	{
	  tem = sparc_compare_op0;
	  sparc_compare_op0 = sparc_compare_op1;
	  sparc_compare_op1 = tem;
	  pat = gen_sltu (operands[0]);
          if (pat == NULL_RTX)
            FAIL;
          emit_insn (pat);
	  DONE;
	}
    }
  else
    {
      if (gen_v9_scc (GTU, operands))
	DONE;
    }
  FAIL;
})

(define_expand "sltu"
  [(set (match_operand:SI 0 "int_register_operand" "")
	(ltu:SI (match_dup 1) (const_int 0)))]
  ""
{
  if (TARGET_V9)
    {
      if (gen_v9_scc (LTU, operands))
	DONE;
    }
  operands[1] = gen_compare_reg (LTU);
})

(define_expand "sgeu"
  [(set (match_operand:SI 0 "int_register_operand" "")
	(geu:SI (match_dup 1) (const_int 0)))]
  ""
{
  if (TARGET_V9)
    {
      if (gen_v9_scc (GEU, operands))
	DONE;
    }
  operands[1] = gen_compare_reg (GEU);
})

(define_expand "sleu"
  [(set (match_operand:SI 0 "int_register_operand" "")
	(leu:SI (match_dup 1) (const_int 0)))]
  ""
{
  if (! TARGET_V9)
    {
      rtx tem, pat;

      /* We can do geu easily, so if both operands are registers, swap them and
	 do a GEU.  */
      if ((GET_CODE (sparc_compare_op0) == REG
	   || GET_CODE (sparc_compare_op0) == SUBREG)
	  && (GET_CODE (sparc_compare_op1) == REG
	      || GET_CODE (sparc_compare_op1) == SUBREG))
	{
	  tem = sparc_compare_op0;
	  sparc_compare_op0 = sparc_compare_op1;
	  sparc_compare_op1 = tem;
	  pat = gen_sgeu (operands[0]);
          if (pat == NULL_RTX)
            FAIL;
          emit_insn (pat);
	  DONE;
	}
    }
  else
    {
      if (gen_v9_scc (LEU, operands))
	DONE;
    }
  FAIL;
})

;; Now the DEFINE_INSNs for the scc cases.

;; The SEQ and SNE patterns are special because they can be done
;; without any branching and do not involve a COMPARE.  We want
;; them to always use the splits below so the results can be
;; scheduled.

(define_insn_and_split "*snesi_zero"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(ne:SI (match_operand:SI 1 "register_operand" "r")
	       (const_int 0)))
   (clobber (reg:CC 100))]
  ""
  "#"
  ""
  [(set (reg:CC_NOOV 100) (compare:CC_NOOV (neg:SI (match_dup 1))
					   (const_int 0)))
   (set (match_dup 0) (ltu:SI (reg:CC 100) (const_int 0)))]
  ""
  [(set_attr "length" "2")])

(define_insn_and_split "*neg_snesi_zero"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(neg:SI (ne:SI (match_operand:SI 1 "register_operand" "r")
		       (const_int 0))))
   (clobber (reg:CC 100))]
  ""
  "#"
  ""
  [(set (reg:CC_NOOV 100) (compare:CC_NOOV (neg:SI (match_dup 1))
					   (const_int 0)))
   (set (match_dup 0) (neg:SI (ltu:SI (reg:CC 100) (const_int 0))))]
  ""
  [(set_attr "length" "2")])

(define_insn_and_split "*snesi_zero_extend"
  [(set (match_operand:DI 0 "register_operand" "=r")
        (ne:DI (match_operand:SI 1 "register_operand" "r")
               (const_int 0)))
   (clobber (reg:CC 100))]
  "TARGET_ARCH64"
  "#"
  "&& 1"
  [(set (reg:CC_NOOV 100) (compare:CC_NOOV (minus:SI (const_int 0)
                                                     (match_dup 1))
                                           (const_int 0)))
   (set (match_dup 0) (zero_extend:DI (plus:SI (plus:SI (const_int 0)
                                                        (const_int 0))
                                               (ltu:SI (reg:CC_NOOV 100)
                                                       (const_int 0)))))]
  ""
  [(set_attr "length" "2")])

(define_insn_and_split "*snedi_zero"
  [(set (match_operand:DI 0 "register_operand" "=&r")
        (ne:DI (match_operand:DI 1 "register_operand" "r")
               (const_int 0)))]
  "TARGET_ARCH64"
  "#"
  "&& ! reg_overlap_mentioned_p (operands[1], operands[0])"
  [(set (match_dup 0) (const_int 0))
   (set (match_dup 0) (if_then_else:DI (ne:DI (match_dup 1)
                                              (const_int 0))
                                       (const_int 1)
                                       (match_dup 0)))]
  ""
  [(set_attr "length" "2")])

(define_insn_and_split "*neg_snedi_zero"
  [(set (match_operand:DI 0 "register_operand" "=&r")
        (neg:DI (ne:DI (match_operand:DI 1 "register_operand" "r")
                       (const_int 0))))]
  "TARGET_ARCH64"
  "#"
  "&& ! reg_overlap_mentioned_p (operands[1], operands[0])"
  [(set (match_dup 0) (const_int 0))
   (set (match_dup 0) (if_then_else:DI (ne:DI (match_dup 1)
                                              (const_int 0))
                                       (const_int -1)
                                       (match_dup 0)))]
  ""
  [(set_attr "length" "2")])

(define_insn_and_split "*snedi_zero_trunc"
  [(set (match_operand:SI 0 "register_operand" "=&r")
        (ne:SI (match_operand:DI 1 "register_operand" "r")
               (const_int 0)))]
  "TARGET_ARCH64"
  "#"
  "&& ! reg_overlap_mentioned_p (operands[1], operands[0])"
  [(set (match_dup 0) (const_int 0))
   (set (match_dup 0) (if_then_else:SI (ne:DI (match_dup 1)
                                              (const_int 0))
                                       (const_int 1)
                                       (match_dup 0)))]
  ""
  [(set_attr "length" "2")])

(define_insn_and_split "*seqsi_zero"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(eq:SI (match_operand:SI 1 "register_operand" "r")
	       (const_int 0)))
   (clobber (reg:CC 100))]
  ""
  "#"
  ""
  [(set (reg:CC_NOOV 100) (compare:CC_NOOV (neg:SI (match_dup 1))
					   (const_int 0)))
   (set (match_dup 0) (geu:SI (reg:CC 100) (const_int 0)))]
  ""
  [(set_attr "length" "2")])

(define_insn_and_split "*neg_seqsi_zero"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(neg:SI (eq:SI (match_operand:SI 1 "register_operand" "r")
		       (const_int 0))))
   (clobber (reg:CC 100))]
  ""
  "#"
  ""
  [(set (reg:CC_NOOV 100) (compare:CC_NOOV (neg:SI (match_dup 1))
					   (const_int 0)))
   (set (match_dup 0) (neg:SI (geu:SI (reg:CC 100) (const_int 0))))]
  ""
  [(set_attr "length" "2")])

(define_insn_and_split "*seqsi_zero_extend"
  [(set (match_operand:DI 0 "register_operand" "=r")
        (eq:DI (match_operand:SI 1 "register_operand" "r")
               (const_int 0)))
   (clobber (reg:CC 100))]
  "TARGET_ARCH64"
  "#"
  "&& 1"
  [(set (reg:CC_NOOV 100) (compare:CC_NOOV (minus:SI (const_int 0)
                                                     (match_dup 1))
                                           (const_int 0)))
   (set (match_dup 0) (zero_extend:DI (minus:SI (minus:SI (const_int 0)
                                                          (const_int -1))
                                                (ltu:SI (reg:CC_NOOV 100)
                                                        (const_int 0)))))]
  ""
  [(set_attr "length" "2")])

(define_insn_and_split "*seqdi_zero"
  [(set (match_operand:DI 0 "register_operand" "=&r")
        (eq:DI (match_operand:DI 1 "register_operand" "r")
               (const_int 0)))]
  "TARGET_ARCH64"
  "#"
  "&& ! reg_overlap_mentioned_p (operands[1], operands[0])"
  [(set (match_dup 0) (const_int 0))
   (set (match_dup 0) (if_then_else:DI (eq:DI (match_dup 1)
                                              (const_int 0))
                                       (const_int 1)
                                       (match_dup 0)))]
  ""
  [(set_attr "length" "2")])

(define_insn_and_split "*neg_seqdi_zero"
  [(set (match_operand:DI 0 "register_operand" "=&r")
        (neg:DI (eq:DI (match_operand:DI 1 "register_operand" "r")
                       (const_int 0))))]
  "TARGET_ARCH64"
  "#"
  "&& ! reg_overlap_mentioned_p (operands[1], operands[0])"
  [(set (match_dup 0) (const_int 0))
   (set (match_dup 0) (if_then_else:DI (eq:DI (match_dup 1)
                                              (const_int 0))
                                       (const_int -1)
                                       (match_dup 0)))]
  ""
  [(set_attr "length" "2")]) 

(define_insn_and_split "*seqdi_zero_trunc"
  [(set (match_operand:SI 0 "register_operand" "=&r")
        (eq:SI (match_operand:DI 1 "register_operand" "r")
               (const_int 0)))]
  "TARGET_ARCH64"
  "#"
  "&& ! reg_overlap_mentioned_p (operands[1], operands[0])"
  [(set (match_dup 0) (const_int 0))
   (set (match_dup 0) (if_then_else:SI (eq:DI (match_dup 1)
                                              (const_int 0))
                                       (const_int 1)
                                       (match_dup 0)))]
  ""
  [(set_attr "length" "2")])

;; We can also do (x + (i == 0)) and related, so put them in.
;; ??? The addx/subx insns use the 32 bit carry flag so there are no DImode
;; versions for v9.

(define_insn_and_split "*x_plus_i_ne_0"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(plus:SI (ne:SI (match_operand:SI 1 "register_operand" "r")
			(const_int 0))
		 (match_operand:SI 2 "register_operand" "r")))
   (clobber (reg:CC 100))]
  ""
  "#"
  ""
  [(set (reg:CC_NOOV 100) (compare:CC_NOOV (neg:SI (match_dup 1))
					   (const_int 0)))
   (set (match_dup 0) (plus:SI (ltu:SI (reg:CC 100) (const_int 0))
			       (match_dup 2)))]
  ""
  [(set_attr "length" "2")])

(define_insn_and_split "*x_minus_i_ne_0"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(minus:SI (match_operand:SI 2 "register_operand" "r")
		  (ne:SI (match_operand:SI 1 "register_operand" "r")
			 (const_int 0))))
   (clobber (reg:CC 100))]
  ""
  "#"
  ""
  [(set (reg:CC_NOOV 100) (compare:CC_NOOV (neg:SI (match_dup 1))
					   (const_int 0)))
   (set (match_dup 0) (minus:SI (match_dup 2)
				(ltu:SI (reg:CC 100) (const_int 0))))]
  ""
  [(set_attr "length" "2")])

(define_insn_and_split "*x_plus_i_eq_0"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(plus:SI (eq:SI (match_operand:SI 1 "register_operand" "r")
			(const_int 0))
		 (match_operand:SI 2 "register_operand" "r")))
   (clobber (reg:CC 100))]
  ""
  "#"
  ""
  [(set (reg:CC_NOOV 100) (compare:CC_NOOV (neg:SI (match_dup 1))
					   (const_int 0)))
   (set (match_dup 0) (plus:SI (geu:SI (reg:CC 100) (const_int 0))
			       (match_dup 2)))]
  ""
  [(set_attr "length" "2")])

(define_insn_and_split "*x_minus_i_eq_0"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(minus:SI (match_operand:SI 2 "register_operand" "r")
		  (eq:SI (match_operand:SI 1 "register_operand" "r")
			 (const_int 0))))
   (clobber (reg:CC 100))]
  ""
  "#"
  ""
  [(set (reg:CC_NOOV 100) (compare:CC_NOOV (neg:SI (match_dup 1))
					   (const_int 0)))
   (set (match_dup 0) (minus:SI (match_dup 2)
				(geu:SI (reg:CC 100) (const_int 0))))]
  ""
  [(set_attr "length" "2")])

;; We can also do GEU and LTU directly, but these operate after a compare.
;; ??? The addx/subx insns use the 32 bit carry flag so there are no DImode
;; versions for v9.

(define_insn "*sltu_insn"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(ltu:SI (reg:CC 100) (const_int 0)))]
  ""
  "addx\t%%g0, 0, %0"
  [(set_attr "type" "ialuX")])

(define_insn "*neg_sltu_insn"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(neg:SI (ltu:SI (reg:CC 100) (const_int 0))))]
  ""
  "subx\t%%g0, 0, %0"
  [(set_attr "type" "ialuX")])

;; ??? Combine should canonicalize these next two to the same pattern.
(define_insn "*neg_sltu_minus_x"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(minus:SI (neg:SI (ltu:SI (reg:CC 100) (const_int 0)))
		  (match_operand:SI 1 "arith_operand" "rI")))]
  ""
  "subx\t%%g0, %1, %0"
  [(set_attr "type" "ialuX")])

(define_insn "*neg_sltu_plus_x"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(neg:SI (plus:SI (ltu:SI (reg:CC 100) (const_int 0))
			 (match_operand:SI 1 "arith_operand" "rI"))))]
  ""
  "subx\t%%g0, %1, %0"
  [(set_attr "type" "ialuX")])

(define_insn "*sgeu_insn"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(geu:SI (reg:CC 100) (const_int 0)))]
  ""
  "subx\t%%g0, -1, %0"
  [(set_attr "type" "ialuX")])

(define_insn "*neg_sgeu_insn"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(neg:SI (geu:SI (reg:CC 100) (const_int 0))))]
  ""
  "addx\t%%g0, -1, %0"
  [(set_attr "type" "ialuX")])

;; We can also do (x + ((unsigned) i >= 0)) and related, so put them in.
;; ??? The addx/subx insns use the 32 bit carry flag so there are no DImode
;; versions for v9.

(define_insn "*sltu_plus_x"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(plus:SI (ltu:SI (reg:CC 100) (const_int 0))
		 (match_operand:SI 1 "arith_operand" "rI")))]
  ""
  "addx\t%%g0, %1, %0"
  [(set_attr "type" "ialuX")])

(define_insn "*sltu_plus_x_plus_y"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(plus:SI (ltu:SI (reg:CC 100) (const_int 0))
		 (plus:SI (match_operand:SI 1 "arith_operand" "%r")
			  (match_operand:SI 2 "arith_operand" "rI"))))]
  ""
  "addx\t%1, %2, %0"
  [(set_attr "type" "ialuX")])

(define_insn "*x_minus_sltu"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(minus:SI (match_operand:SI 1 "register_operand" "r")
		  (ltu:SI (reg:CC 100) (const_int 0))))]
  ""
  "subx\t%1, 0, %0"
  [(set_attr "type" "ialuX")])

;; ??? Combine should canonicalize these next two to the same pattern.
(define_insn "*x_minus_y_minus_sltu"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(minus:SI (minus:SI (match_operand:SI 1 "register_or_zero_operand" "rJ")
			    (match_operand:SI 2 "arith_operand" "rI"))
		  (ltu:SI (reg:CC 100) (const_int 0))))]
  ""
  "subx\t%r1, %2, %0"
  [(set_attr "type" "ialuX")])

(define_insn "*x_minus_sltu_plus_y"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(minus:SI (match_operand:SI 1 "register_or_zero_operand" "rJ")
		  (plus:SI (ltu:SI (reg:CC 100) (const_int 0))
			   (match_operand:SI 2 "arith_operand" "rI"))))]
  ""
  "subx\t%r1, %2, %0"
  [(set_attr "type" "ialuX")])

(define_insn "*sgeu_plus_x"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(plus:SI (geu:SI (reg:CC 100) (const_int 0))
		 (match_operand:SI 1 "register_operand" "r")))]
  ""
  "subx\t%1, -1, %0"
  [(set_attr "type" "ialuX")])

(define_insn "*x_minus_sgeu"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(minus:SI (match_operand:SI 1 "register_operand" "r")
		  (geu:SI (reg:CC 100) (const_int 0))))]
  ""
  "addx\t%1, -1, %0"
  [(set_attr "type" "ialuX")])

(define_split
  [(set (match_operand:SI 0 "register_operand" "")
	(match_operator:SI 2 "noov_compare_operator"
			   [(match_operand 1 "icc_or_fcc_register_operand" "")
			    (const_int 0)]))]
  "TARGET_V9
   && REGNO (operands[1]) == SPARC_ICC_REG
   && (GET_MODE (operands[1]) == CCXmode
       /* 32 bit LTU/GEU are better implemented using addx/subx.  */
       || (GET_CODE (operands[2]) != LTU && GET_CODE (operands[2]) != GEU))"
  [(set (match_dup 0) (const_int 0))
   (set (match_dup 0)
	(if_then_else:SI (match_op_dup:SI 2 [(match_dup 1) (const_int 0)])
			 (const_int 1)
			 (match_dup 0)))]
  "")


;; These control RTL generation for conditional jump insns

;; The quad-word fp compare library routines all return nonzero to indicate
;; true, which is different from the equivalent libgcc routines, so we must
;; handle them specially here.

(define_expand "beq"
  [(set (pc)
	(if_then_else (eq (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
{
  if (TARGET_ARCH64 && sparc_compare_op1 == const0_rtx
      && GET_CODE (sparc_compare_op0) == REG
      && GET_MODE (sparc_compare_op0) == DImode)
    {
      emit_v9_brxx_insn (EQ, sparc_compare_op0, operands[0]);
      DONE;
    }
  else if (GET_MODE (sparc_compare_op0) == TFmode && ! TARGET_HARD_QUAD)
    {
      sparc_emit_float_lib_cmp (sparc_compare_op0, sparc_compare_op1, EQ);
      emit_jump_insn (gen_bne (operands[0]));
      DONE;
    }
  operands[1] = gen_compare_reg (EQ);
})

(define_expand "bne"
  [(set (pc)
	(if_then_else (ne (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
{
  if (TARGET_ARCH64 && sparc_compare_op1 == const0_rtx
      && GET_CODE (sparc_compare_op0) == REG
      && GET_MODE (sparc_compare_op0) == DImode)
    {
      emit_v9_brxx_insn (NE, sparc_compare_op0, operands[0]);
      DONE;
    }
  else if (GET_MODE (sparc_compare_op0) == TFmode && ! TARGET_HARD_QUAD)
    {
      sparc_emit_float_lib_cmp (sparc_compare_op0, sparc_compare_op1, NE);
      emit_jump_insn (gen_bne (operands[0]));
      DONE;
    }
  operands[1] = gen_compare_reg (NE);
})

(define_expand "bgt"
  [(set (pc)
	(if_then_else (gt (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
{
  if (TARGET_ARCH64 && sparc_compare_op1 == const0_rtx
      && GET_CODE (sparc_compare_op0) == REG
      && GET_MODE (sparc_compare_op0) == DImode)
    {
      emit_v9_brxx_insn (GT, sparc_compare_op0, operands[0]);
      DONE;
    }
  else if (GET_MODE (sparc_compare_op0) == TFmode && ! TARGET_HARD_QUAD)
    {
      sparc_emit_float_lib_cmp (sparc_compare_op0, sparc_compare_op1, GT);
      emit_jump_insn (gen_bne (operands[0]));
      DONE;
    }
  operands[1] = gen_compare_reg (GT);
})

(define_expand "bgtu"
  [(set (pc)
	(if_then_else (gtu (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
{
  operands[1] = gen_compare_reg (GTU);
})

(define_expand "blt"
  [(set (pc)
	(if_then_else (lt (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
{
  if (TARGET_ARCH64 && sparc_compare_op1 == const0_rtx
      && GET_CODE (sparc_compare_op0) == REG
      && GET_MODE (sparc_compare_op0) == DImode)
    {
      emit_v9_brxx_insn (LT, sparc_compare_op0, operands[0]);
      DONE;
    }
  else if (GET_MODE (sparc_compare_op0) == TFmode && ! TARGET_HARD_QUAD)
    {
      sparc_emit_float_lib_cmp (sparc_compare_op0, sparc_compare_op1, LT);
      emit_jump_insn (gen_bne (operands[0]));
      DONE;
    }
  operands[1] = gen_compare_reg (LT);
})

(define_expand "bltu"
  [(set (pc)
	(if_then_else (ltu (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
{
  operands[1] = gen_compare_reg (LTU);
})

(define_expand "bge"
  [(set (pc)
	(if_then_else (ge (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
{
  if (TARGET_ARCH64 && sparc_compare_op1 == const0_rtx
      && GET_CODE (sparc_compare_op0) == REG
      && GET_MODE (sparc_compare_op0) == DImode)
    {
      emit_v9_brxx_insn (GE, sparc_compare_op0, operands[0]);
      DONE;
    }
  else if (GET_MODE (sparc_compare_op0) == TFmode && ! TARGET_HARD_QUAD)
    {
      sparc_emit_float_lib_cmp (sparc_compare_op0, sparc_compare_op1, GE);
      emit_jump_insn (gen_bne (operands[0]));
      DONE;
    }
  operands[1] = gen_compare_reg (GE);
})

(define_expand "bgeu"
  [(set (pc)
	(if_then_else (geu (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
{
  operands[1] = gen_compare_reg (GEU);
})

(define_expand "ble"
  [(set (pc)
	(if_then_else (le (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
{
  if (TARGET_ARCH64 && sparc_compare_op1 == const0_rtx
      && GET_CODE (sparc_compare_op0) == REG
      && GET_MODE (sparc_compare_op0) == DImode)
    {
      emit_v9_brxx_insn (LE, sparc_compare_op0, operands[0]);
      DONE;
    }
  else if (GET_MODE (sparc_compare_op0) == TFmode && ! TARGET_HARD_QUAD)
    {
      sparc_emit_float_lib_cmp (sparc_compare_op0, sparc_compare_op1, LE);
      emit_jump_insn (gen_bne (operands[0]));
      DONE;
    }
  operands[1] = gen_compare_reg (LE);
})

(define_expand "bleu"
  [(set (pc)
	(if_then_else (leu (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
{
  operands[1] = gen_compare_reg (LEU);
})

(define_expand "bunordered"
  [(set (pc)
	(if_then_else (unordered (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
{
  if (GET_MODE (sparc_compare_op0) == TFmode && ! TARGET_HARD_QUAD)
    {
      sparc_emit_float_lib_cmp (sparc_compare_op0, sparc_compare_op1,
				UNORDERED);
      emit_jump_insn (gen_beq (operands[0]));
      DONE;
    }
  operands[1] = gen_compare_reg (UNORDERED);
})

(define_expand "bordered"
  [(set (pc)
	(if_then_else (ordered (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
{
  if (GET_MODE (sparc_compare_op0) == TFmode && ! TARGET_HARD_QUAD)
    {
      sparc_emit_float_lib_cmp (sparc_compare_op0, sparc_compare_op1, ORDERED);
      emit_jump_insn (gen_bne (operands[0]));
      DONE;
    }
  operands[1] = gen_compare_reg (ORDERED);
})

(define_expand "bungt"
  [(set (pc)
	(if_then_else (ungt (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
{
  if (GET_MODE (sparc_compare_op0) == TFmode && ! TARGET_HARD_QUAD)
    {
      sparc_emit_float_lib_cmp (sparc_compare_op0, sparc_compare_op1, UNGT);
      emit_jump_insn (gen_bgt (operands[0]));
      DONE;
    }
  operands[1] = gen_compare_reg (UNGT);
})

(define_expand "bunlt"
  [(set (pc)
	(if_then_else (unlt (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
{
  if (GET_MODE (sparc_compare_op0) == TFmode && ! TARGET_HARD_QUAD)
    {
      sparc_emit_float_lib_cmp (sparc_compare_op0, sparc_compare_op1, UNLT);
      emit_jump_insn (gen_bne (operands[0]));
      DONE;
    }
  operands[1] = gen_compare_reg (UNLT);
})

(define_expand "buneq"
  [(set (pc)
	(if_then_else (uneq (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
{
  if (GET_MODE (sparc_compare_op0) == TFmode && ! TARGET_HARD_QUAD)
    {
      sparc_emit_float_lib_cmp (sparc_compare_op0, sparc_compare_op1, UNEQ);
      emit_jump_insn (gen_beq (operands[0]));
      DONE;
    }
  operands[1] = gen_compare_reg (UNEQ);
})

(define_expand "bunge"
  [(set (pc)
	(if_then_else (unge (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
{
  if (GET_MODE (sparc_compare_op0) == TFmode && ! TARGET_HARD_QUAD)
    {
      sparc_emit_float_lib_cmp (sparc_compare_op0, sparc_compare_op1, UNGE);
      emit_jump_insn (gen_bne (operands[0]));
      DONE;
    }
  operands[1] = gen_compare_reg (UNGE);
})

(define_expand "bunle"
  [(set (pc)
	(if_then_else (unle (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
{
  if (GET_MODE (sparc_compare_op0) == TFmode && ! TARGET_HARD_QUAD)
    {
      sparc_emit_float_lib_cmp (sparc_compare_op0, sparc_compare_op1, UNLE);
      emit_jump_insn (gen_bne (operands[0]));
      DONE;
    }
  operands[1] = gen_compare_reg (UNLE);
})

(define_expand "bltgt"
  [(set (pc)
	(if_then_else (ltgt (match_dup 1) (const_int 0))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))]
  ""
{
  if (GET_MODE (sparc_compare_op0) == TFmode && ! TARGET_HARD_QUAD)
    {
      sparc_emit_float_lib_cmp (sparc_compare_op0, sparc_compare_op1, LTGT);
      emit_jump_insn (gen_bne (operands[0]));
      DONE;
    }
  operands[1] = gen_compare_reg (LTGT);
})

;; Now match both normal and inverted jump.

;; XXX fpcmp nop braindamage
(define_insn "*normal_branch"
  [(set (pc)
	(if_then_else (match_operator 0 "noov_compare_operator"
				      [(reg 100) (const_int 0)])
		      (label_ref (match_operand 1 "" ""))
		      (pc)))]
  ""
{
  return output_cbranch (operands[0], operands[1], 1, 0,
			 final_sequence && INSN_ANNULLED_BRANCH_P (insn),
			 insn);
}
  [(set_attr "type" "branch")
   (set_attr "branch_type" "icc")])

;; XXX fpcmp nop braindamage
(define_insn "*inverted_branch"
  [(set (pc)
	(if_then_else (match_operator 0 "noov_compare_operator"
				      [(reg 100) (const_int 0)])
		      (pc)
		      (label_ref (match_operand 1 "" ""))))]
  ""
{
  return output_cbranch (operands[0], operands[1], 1, 1,
			 final_sequence && INSN_ANNULLED_BRANCH_P (insn),
			 insn);
}
  [(set_attr "type" "branch")
   (set_attr "branch_type" "icc")])

;; XXX fpcmp nop braindamage
(define_insn "*normal_fp_branch"
  [(set (pc)
	(if_then_else (match_operator 1 "comparison_operator"
				      [(match_operand:CCFP 0 "fcc_register_operand" "c")
				       (const_int 0)])
		      (label_ref (match_operand 2 "" ""))
		      (pc)))]
  ""
{
  return output_cbranch (operands[1], operands[2], 2, 0,
			 final_sequence && INSN_ANNULLED_BRANCH_P (insn),
			 insn);
}
  [(set_attr "type" "branch")
   (set_attr "branch_type" "fcc")])

;; XXX fpcmp nop braindamage
(define_insn "*inverted_fp_branch"
  [(set (pc)
	(if_then_else (match_operator 1 "comparison_operator"
				      [(match_operand:CCFP 0 "fcc_register_operand" "c")
				       (const_int 0)])
		      (pc)
		      (label_ref (match_operand 2 "" ""))))]
  ""
{
  return output_cbranch (operands[1], operands[2], 2, 1,
			 final_sequence && INSN_ANNULLED_BRANCH_P (insn),
			 insn);
}
  [(set_attr "type" "branch")
   (set_attr "branch_type" "fcc")])

;; XXX fpcmp nop braindamage
(define_insn "*normal_fpe_branch"
  [(set (pc)
	(if_then_else (match_operator 1 "comparison_operator"
				      [(match_operand:CCFPE 0 "fcc_register_operand" "c")
				       (const_int 0)])
		      (label_ref (match_operand 2 "" ""))
		      (pc)))]
  ""
{
  return output_cbranch (operands[1], operands[2], 2, 0,
			 final_sequence && INSN_ANNULLED_BRANCH_P (insn),
			 insn);
}
  [(set_attr "type" "branch")
   (set_attr "branch_type" "fcc")])

;; XXX fpcmp nop braindamage
(define_insn "*inverted_fpe_branch"
  [(set (pc)
	(if_then_else (match_operator 1 "comparison_operator"
				      [(match_operand:CCFPE 0 "fcc_register_operand" "c")
				       (const_int 0)])
		      (pc)
		      (label_ref (match_operand 2 "" ""))))]
  ""
{
  return output_cbranch (operands[1], operands[2], 2, 1,
			 final_sequence && INSN_ANNULLED_BRANCH_P (insn),
			 insn);
}
  [(set_attr "type" "branch")
   (set_attr "branch_type" "fcc")])

;; SPARC V9-specific jump insns.  None of these are guaranteed to be
;; in the architecture.

;; There are no 32 bit brreg insns.

;; XXX
(define_insn "*normal_int_branch_sp64"
  [(set (pc)
	(if_then_else (match_operator 0 "v9_register_compare_operator"
				      [(match_operand:DI 1 "register_operand" "r")
				       (const_int 0)])
		      (label_ref (match_operand 2 "" ""))
		      (pc)))]
  "TARGET_ARCH64"
{
  return output_v9branch (operands[0], operands[2], 1, 2, 0,
			  final_sequence && INSN_ANNULLED_BRANCH_P (insn),
			  insn);
}
  [(set_attr "type" "branch")
   (set_attr "branch_type" "reg")])

;; XXX
(define_insn "*inverted_int_branch_sp64"
  [(set (pc)
	(if_then_else (match_operator 0 "v9_register_compare_operator"
				      [(match_operand:DI 1 "register_operand" "r")
				       (const_int 0)])
		      (pc)
		      (label_ref (match_operand 2 "" ""))))]
  "TARGET_ARCH64"
{
  return output_v9branch (operands[0], operands[2], 1, 2, 1,
			  final_sequence && INSN_ANNULLED_BRANCH_P (insn),
			  insn);
}
  [(set_attr "type" "branch")
   (set_attr "branch_type" "reg")])


(define_mode_macro P [(SI "Pmode == SImode") (DI "Pmode == DImode")])

;; Load in operand 0 the (absolute) address of operand 1, which is a symbolic
;; value subject to a PC-relative relocation.  Operand 2 is a helper function
;; that adds the PC value at the call point to operand 0.

(define_insn "load_pcrel_sym<P:mode>"
  [(set (match_operand:P 0 "register_operand" "=r")
	(unspec:P [(match_operand:P 1 "symbolic_operand" "")
		   (match_operand:P 2 "call_address_operand" "")] UNSPEC_LOAD_PCREL_SYM))
   (clobber (reg:P 15))]
  ""
{
  if (flag_delayed_branch)
    return "sethi\t%%hi(%a1-4), %0\n\tcall\t%a2\n\t add\t%0, %%lo(%a1+4), %0";
  else
    return "sethi\t%%hi(%a1-8), %0\n\tadd\t%0, %%lo(%a1-4), %0\n\tcall\t%a2\n\t nop";
}
  [(set (attr "type") (const_string "multi"))
   (set (attr "length")
	(if_then_else (eq_attr "delayed_branch" "true")
		      (const_int 3)
		      (const_int 4)))])


;; Integer move instructions

(define_expand "movqi"
  [(set (match_operand:QI 0 "nonimmediate_operand" "")
	(match_operand:QI 1 "general_operand" ""))]
  ""
{
  if (sparc_expand_move (QImode, operands))
    DONE;
})

(define_insn "*movqi_insn"
  [(set (match_operand:QI 0 "nonimmediate_operand" "=r,r,m")
	(match_operand:QI 1 "input_operand"   "rI,m,rJ"))]
  "(register_operand (operands[0], QImode)
    || register_or_zero_operand (operands[1], QImode))"
  "@
   mov\t%1, %0
   ldub\t%1, %0
   stb\t%r1, %0"
  [(set_attr "type" "*,load,store")
   (set_attr "us3load_type" "*,3cycle,*")])

(define_expand "movhi"
  [(set (match_operand:HI 0 "nonimmediate_operand" "")
	(match_operand:HI 1 "general_operand" ""))]
  ""
{
  if (sparc_expand_move (HImode, operands))
    DONE;
})

(define_insn "*movhi_insn"
  [(set (match_operand:HI 0 "nonimmediate_operand" "=r,r,r,m")
	(match_operand:HI 1 "input_operand"   "rI,K,m,rJ"))]
  "(register_operand (operands[0], HImode)
    || register_or_zero_operand (operands[1], HImode))"
  "@
   mov\t%1, %0
   sethi\t%%hi(%a1), %0
   lduh\t%1, %0
   sth\t%r1, %0"
  [(set_attr "type" "*,*,load,store")
   (set_attr "us3load_type" "*,*,3cycle,*")])

;; We always work with constants here.
(define_insn "*movhi_lo_sum"
  [(set (match_operand:HI 0 "register_operand" "=r")
	(ior:HI (match_operand:HI 1 "register_operand" "%r")
                (match_operand:HI 2 "small_int_operand" "I")))]
  ""
  "or\t%1, %2, %0")

(define_expand "movsi"
  [(set (match_operand:SI 0 "nonimmediate_operand" "")
	(match_operand:SI 1 "general_operand" ""))]
  ""
{
  if (sparc_expand_move (SImode, operands))
    DONE;
})

(define_insn "*movsi_insn"
  [(set (match_operand:SI 0 "nonimmediate_operand" "=r,r,r,m,!f,!f,!m,d")
	(match_operand:SI 1 "input_operand"   "rI,K,m,rJ,f,m,f,J"))]
  "(register_operand (operands[0], SImode)
    || register_or_zero_operand (operands[1], SImode))"
  "@
   mov\t%1, %0
   sethi\t%%hi(%a1), %0
   ld\t%1, %0
   st\t%r1, %0
   fmovs\t%1, %0
   ld\t%1, %0
   st\t%1, %0
   fzeros\t%0"
  [(set_attr "type" "*,*,load,store,fpmove,fpload,fpstore,fga")])

(define_insn "*movsi_lo_sum"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(lo_sum:SI (match_operand:SI 1 "register_operand" "r")
                   (match_operand:SI 2 "immediate_operand" "in")))]
  ""
  "or\t%1, %%lo(%a2), %0")

(define_insn "*movsi_high"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(high:SI (match_operand:SI 1 "immediate_operand" "in")))]
  ""
  "sethi\t%%hi(%a1), %0")

;; The next two patterns must wrap the SYMBOL_REF in an UNSPEC
;; so that CSE won't optimize the address computation away.
(define_insn "movsi_lo_sum_pic"
  [(set (match_operand:SI 0 "register_operand" "=r")
        (lo_sum:SI (match_operand:SI 1 "register_operand" "r")
                   (unspec:SI [(match_operand:SI 2 "immediate_operand" "in")] UNSPEC_MOVE_PIC)))]
  "flag_pic"
  "or\t%1, %%lo(%a2), %0")

(define_insn "movsi_high_pic"
  [(set (match_operand:SI 0 "register_operand" "=r")
        (high:SI (unspec:SI [(match_operand 1 "" "")] UNSPEC_MOVE_PIC)))]
  "flag_pic && check_pic (1)"
  "sethi\t%%hi(%a1), %0")

(define_expand "movsi_pic_label_ref"
  [(set (match_dup 3) (high:SI
     (unspec:SI [(match_operand:SI 1 "label_ref_operand" "")
		 (match_dup 2)] UNSPEC_MOVE_PIC_LABEL)))
   (set (match_dup 4) (lo_sum:SI (match_dup 3)
     (unspec:SI [(match_dup 1) (match_dup 2)] UNSPEC_MOVE_PIC_LABEL)))
   (set (match_operand:SI 0 "register_operand" "=r")
	(minus:SI (match_dup 5) (match_dup 4)))]
  "flag_pic"
{
  current_function_uses_pic_offset_table = 1;
  operands[2] = gen_rtx_SYMBOL_REF (Pmode, "_GLOBAL_OFFSET_TABLE_");
  if (no_new_pseudos)
    {
      operands[3] = operands[0];
      operands[4] = operands[0];
    }
  else
    {
      operands[3] = gen_reg_rtx (SImode);
      operands[4] = gen_reg_rtx (SImode);
    }
  operands[5] = pic_offset_table_rtx;
})

(define_insn "*movsi_high_pic_label_ref"
  [(set (match_operand:SI 0 "register_operand" "=r")
      (high:SI
        (unspec:SI [(match_operand:SI 1 "label_ref_operand" "")
		    (match_operand:SI 2 "" "")] UNSPEC_MOVE_PIC_LABEL)))]
  "flag_pic"
  "sethi\t%%hi(%a2-(%a1-.)), %0")

(define_insn "*movsi_lo_sum_pic_label_ref"
  [(set (match_operand:SI 0 "register_operand" "=r")
      (lo_sum:SI (match_operand:SI 1 "register_operand" "r")
        (unspec:SI [(match_operand:SI 2 "label_ref_operand" "")
		    (match_operand:SI 3 "" "")] UNSPEC_MOVE_PIC_LABEL)))]
  "flag_pic"
  "or\t%1, %%lo(%a3-(%a2-.)), %0")

(define_expand "movdi"
  [(set (match_operand:DI 0 "nonimmediate_operand" "")
	(match_operand:DI 1 "general_operand" ""))]
  ""
{
  if (sparc_expand_move (DImode, operands))
    DONE;
})

;; Be careful, fmovd does not exist when !v9.
;; We match MEM moves directly when we have correct even
;; numbered registers, but fall into splits otherwise.
;; The constraint ordering here is really important to
;; avoid insane problems in reload, especially for patterns
;; of the form:
;;
;; (set (mem:DI (plus:SI (reg:SI 30 %fp)
;;                       (const_int -5016)))
;;      (reg:DI 2 %g2))
;;

(define_insn "*movdi_insn_sp32"
  [(set (match_operand:DI 0 "nonimmediate_operand"
				"=o,T,U,o,r,r,r,?T,?f,?f,?o,?f")
        (match_operand:DI 1 "input_operand"
				" J,U,T,r,o,i,r, f, T, o, f, f"))]
  "! TARGET_V9
   && (register_operand (operands[0], DImode)
       || register_or_zero_operand (operands[1], DImode))"
  "@
   #
   std\t%1, %0
   ldd\t%1, %0
   #
   #
   #
   #
   std\t%1, %0
   ldd\t%1, %0
   #
   #
   #"
  [(set_attr "type" "store,store,load,*,*,*,*,fpstore,fpload,*,*,*")
   (set_attr "length" "2,*,*,2,2,2,2,*,*,2,2,2")])

(define_insn "*movdi_insn_sp32_v9"
  [(set (match_operand:DI 0 "nonimmediate_operand"
					"=T,o,T,U,o,r,r,r,?T,?f,?f,?o,?e,?e,?W")
        (match_operand:DI 1 "input_operand"
					" J,J,U,T,r,o,i,r, f, T, o, f, e, W, e"))]
  "! TARGET_ARCH64
   && TARGET_V9
   && (register_operand (operands[0], DImode)
       || register_or_zero_operand (operands[1], DImode))"
  "@
   stx\t%%g0, %0
   #
   std\t%1, %0
   ldd\t%1, %0
   #
   #
   #
   #
   std\t%1, %0
   ldd\t%1, %0
   #
   #
   fmovd\\t%1, %0
   ldd\\t%1, %0
   std\\t%1, %0"
  [(set_attr "type" "store,store,store,load,*,*,*,*,fpstore,fpload,*,*,fpmove,fpload,fpstore")
   (set_attr "length" "*,2,*,*,2,2,2,2,*,*,2,2,*,*,*")
   (set_attr "fptype" "*,*,*,*,*,*,*,*,*,*,*,*,double,*,*")])

(define_insn "*movdi_insn_sp64"
  [(set (match_operand:DI 0 "nonimmediate_operand" "=r,r,r,m,?e,?e,?W,b")
        (match_operand:DI 1 "input_operand"   "rI,N,m,rJ,e,W,e,J"))]
  "TARGET_ARCH64
   && (register_operand (operands[0], DImode)
       || register_or_zero_operand (operands[1], DImode))"
  "@
   mov\t%1, %0
   sethi\t%%hi(%a1), %0
   ldx\t%1, %0
   stx\t%r1, %0
   fmovd\t%1, %0
   ldd\t%1, %0
   std\t%1, %0
   fzero\t%0"
  [(set_attr "type" "*,*,load,store,fpmove,fpload,fpstore,fga")
   (set_attr "fptype" "*,*,*,*,double,*,*,double")])

(define_expand "movdi_pic_label_ref"
  [(set (match_dup 3) (high:DI
     (unspec:DI [(match_operand:DI 1 "label_ref_operand" "")
                 (match_dup 2)] UNSPEC_MOVE_PIC_LABEL)))
   (set (match_dup 4) (lo_sum:DI (match_dup 3)
     (unspec:DI [(match_dup 1) (match_dup 2)] UNSPEC_MOVE_PIC_LABEL)))
   (set (match_operand:DI 0 "register_operand" "=r")
        (minus:DI (match_dup 5) (match_dup 4)))]
  "TARGET_ARCH64 && flag_pic"
{
  current_function_uses_pic_offset_table = 1;
  operands[2] = gen_rtx_SYMBOL_REF (Pmode, "_GLOBAL_OFFSET_TABLE_");
  if (no_new_pseudos)
    {
      operands[3] = operands[0];
      operands[4] = operands[0];
    }
  else
    {
      operands[3] = gen_reg_rtx (DImode);
      operands[4] = gen_reg_rtx (DImode);
    }
  operands[5] = pic_offset_table_rtx;
})

(define_insn "*movdi_high_pic_label_ref"
  [(set (match_operand:DI 0 "register_operand" "=r")
        (high:DI
          (unspec:DI [(match_operand:DI 1 "label_ref_operand" "")
                      (match_operand:DI 2 "" "")] UNSPEC_MOVE_PIC_LABEL)))]
  "TARGET_ARCH64 && flag_pic"
  "sethi\t%%hi(%a2-(%a1-.)), %0")

(define_insn "*movdi_lo_sum_pic_label_ref"
  [(set (match_operand:DI 0 "register_operand" "=r")
      (lo_sum:DI (match_operand:DI 1 "register_operand" "r")
        (unspec:DI [(match_operand:DI 2 "label_ref_operand" "")
                    (match_operand:DI 3 "" "")] UNSPEC_MOVE_PIC_LABEL)))]
  "TARGET_ARCH64 && flag_pic"
  "or\t%1, %%lo(%a3-(%a2-.)), %0")

;; SPARC-v9 code model support insns.  See sparc_emit_set_symbolic_const64
;; in sparc.c to see what is going on here... PIC stuff comes first.

(define_insn "movdi_lo_sum_pic"
  [(set (match_operand:DI 0 "register_operand" "=r")
        (lo_sum:DI (match_operand:DI 1 "register_operand" "r")
                   (unspec:DI [(match_operand:DI 2 "immediate_operand" "in")] UNSPEC_MOVE_PIC)))]
  "TARGET_ARCH64 && flag_pic"
  "or\t%1, %%lo(%a2), %0")

(define_insn "movdi_high_pic"
  [(set (match_operand:DI 0 "register_operand" "=r")
        (high:DI (unspec:DI [(match_operand 1 "" "")] UNSPEC_MOVE_PIC)))]
  "TARGET_ARCH64 && flag_pic && check_pic (1)"
  "sethi\t%%hi(%a1), %0")

(define_insn "*sethi_di_medlow_embmedany_pic"
  [(set (match_operand:DI 0 "register_operand" "=r")
        (high:DI (match_operand:DI 1 "medium_pic_operand" "")))]
  "(TARGET_CM_MEDLOW || TARGET_CM_EMBMEDANY) && check_pic (1)"
  "sethi\t%%hi(%a1), %0")

(define_insn "*sethi_di_medlow"
  [(set (match_operand:DI 0 "register_operand" "=r")
        (high:DI (match_operand:DI 1 "symbolic_operand" "")))]
  "TARGET_CM_MEDLOW && check_pic (1)"
  "sethi\t%%hi(%a1), %0")

(define_insn "*losum_di_medlow"
  [(set (match_operand:DI 0 "register_operand" "=r")
        (lo_sum:DI (match_operand:DI 1 "register_operand" "r")
                   (match_operand:DI 2 "symbolic_operand" "")))]
  "TARGET_CM_MEDLOW"
  "or\t%1, %%lo(%a2), %0")

(define_insn "seth44"
  [(set (match_operand:DI 0 "register_operand" "=r")
        (high:DI (unspec:DI [(match_operand:DI 1 "symbolic_operand" "")] UNSPEC_SETH44)))]
  "TARGET_CM_MEDMID"
  "sethi\t%%h44(%a1), %0")

(define_insn "setm44"
  [(set (match_operand:DI 0 "register_operand" "=r")
        (lo_sum:DI (match_operand:DI 1 "register_operand" "r")
                   (unspec:DI [(match_operand:DI 2 "symbolic_operand" "")] UNSPEC_SETM44)))]
  "TARGET_CM_MEDMID"
  "or\t%1, %%m44(%a2), %0")

(define_insn "setl44"
  [(set (match_operand:DI 0 "register_operand" "=r")
        (lo_sum:DI (match_operand:DI 1 "register_operand" "r")
                   (match_operand:DI 2 "symbolic_operand" "")))]
  "TARGET_CM_MEDMID"
  "or\t%1, %%l44(%a2), %0")

(define_insn "sethh"
  [(set (match_operand:DI 0 "register_operand" "=r")
        (high:DI (unspec:DI [(match_operand:DI 1 "symbolic_operand" "")] UNSPEC_SETHH)))]
  "TARGET_CM_MEDANY"
  "sethi\t%%hh(%a1), %0")

(define_insn "setlm"
  [(set (match_operand:DI 0 "register_operand" "=r")
        (high:DI (unspec:DI [(match_operand:DI 1 "symbolic_operand" "")] UNSPEC_SETLM)))]
  "TARGET_CM_MEDANY"
  "sethi\t%%lm(%a1), %0")

(define_insn "sethm"
  [(set (match_operand:DI 0 "register_operand" "=r")
        (lo_sum:DI (match_operand:DI 1 "register_operand" "r")
                   (unspec:DI [(match_operand:DI 2 "symbolic_operand" "")] UNSPEC_EMB_SETHM)))]
  "TARGET_CM_MEDANY"
  "or\t%1, %%hm(%a2), %0")

(define_insn "setlo"
  [(set (match_operand:DI 0 "register_operand" "=r")
        (lo_sum:DI (match_operand:DI 1 "register_operand" "r")
                   (match_operand:DI 2 "symbolic_operand" "")))]
  "TARGET_CM_MEDANY"
  "or\t%1, %%lo(%a2), %0")

(define_insn "embmedany_sethi"
  [(set (match_operand:DI 0 "register_operand" "=r")
        (high:DI (unspec:DI [(match_operand:DI 1 "data_segment_operand" "")] UNSPEC_EMB_HISUM)))]
  "TARGET_CM_EMBMEDANY && check_pic (1)"
  "sethi\t%%hi(%a1), %0")

(define_insn "embmedany_losum"
  [(set (match_operand:DI 0 "register_operand" "=r")
        (lo_sum:DI (match_operand:DI 1 "register_operand" "r")
                   (match_operand:DI 2 "data_segment_operand" "")))]
  "TARGET_CM_EMBMEDANY"
  "add\t%1, %%lo(%a2), %0")

(define_insn "embmedany_brsum"
  [(set (match_operand:DI 0 "register_operand" "=r")
        (unspec:DI [(match_operand:DI 1 "register_operand" "r")] UNSPEC_EMB_HISUM))]
  "TARGET_CM_EMBMEDANY"
  "add\t%1, %_, %0")

(define_insn "embmedany_textuhi"
  [(set (match_operand:DI 0 "register_operand" "=r")
        (high:DI (unspec:DI [(match_operand:DI 1 "text_segment_operand" "")] UNSPEC_EMB_TEXTUHI)))]
  "TARGET_CM_EMBMEDANY && check_pic (1)"
  "sethi\t%%uhi(%a1), %0")

(define_insn "embmedany_texthi"
  [(set (match_operand:DI 0 "register_operand" "=r")
        (high:DI (unspec:DI [(match_operand:DI 1 "text_segment_operand" "")] UNSPEC_EMB_TEXTHI)))]
  "TARGET_CM_EMBMEDANY && check_pic (1)"
  "sethi\t%%hi(%a1), %0")

(define_insn "embmedany_textulo"
  [(set (match_operand:DI 0 "register_operand" "=r")
        (lo_sum:DI (match_operand:DI 1 "register_operand" "r")
                   (unspec:DI [(match_operand:DI 2 "text_segment_operand" "")] UNSPEC_EMB_TEXTULO)))]
  "TARGET_CM_EMBMEDANY"
  "or\t%1, %%ulo(%a2), %0")

(define_insn "embmedany_textlo"
  [(set (match_operand:DI 0 "register_operand" "=r")
        (lo_sum:DI (match_operand:DI 1 "register_operand" "r")
                   (match_operand:DI 2 "text_segment_operand" "")))]
  "TARGET_CM_EMBMEDANY"
  "or\t%1, %%lo(%a2), %0")

;; Now some patterns to help reload out a bit.
(define_expand "reload_indi"
  [(parallel [(match_operand:DI 0 "register_operand" "=r")
              (match_operand:DI 1 "immediate_operand" "")
              (match_operand:TI 2 "register_operand" "=&r")])]
  "(TARGET_CM_MEDANY
    || TARGET_CM_EMBMEDANY)
   && ! flag_pic"
{
  sparc_emit_set_symbolic_const64 (operands[0], operands[1], operands[2]);
  DONE;
})

(define_expand "reload_outdi"
  [(parallel [(match_operand:DI 0 "register_operand" "=r")
              (match_operand:DI 1 "immediate_operand" "")
              (match_operand:TI 2 "register_operand" "=&r")])]
  "(TARGET_CM_MEDANY
    || TARGET_CM_EMBMEDANY)
   && ! flag_pic"
{
  sparc_emit_set_symbolic_const64 (operands[0], operands[1], operands[2]);
  DONE;
})

;; Split up putting CONSTs and REGs into DI regs when !arch64
(define_split
  [(set (match_operand:DI 0 "register_operand" "")
        (match_operand:DI 1 "const_int_operand" ""))]
  "! TARGET_ARCH64 && reload_completed"
  [(clobber (const_int 0))]
{
#if HOST_BITS_PER_WIDE_INT == 32
  emit_insn (gen_movsi (gen_highpart (SImode, operands[0]),
			(INTVAL (operands[1]) < 0) ?
			constm1_rtx :
			const0_rtx));
  emit_insn (gen_movsi (gen_lowpart (SImode, operands[0]),
			operands[1]));
#else
  unsigned int low, high;

  low = trunc_int_for_mode (INTVAL (operands[1]), SImode);
  high = trunc_int_for_mode (INTVAL (operands[1]) >> 32, SImode);
  emit_insn (gen_movsi (gen_highpart (SImode, operands[0]), GEN_INT (high)));

  /* Slick... but this trick loses if this subreg constant part
     can be done in one insn.  */
  if (low == high
      && ! SPARC_SETHI32_P (high)
      && ! SPARC_SIMM13_P (high))
    emit_insn (gen_movsi (gen_lowpart (SImode, operands[0]),
			  gen_highpart (SImode, operands[0])));
  else
    emit_insn (gen_movsi (gen_lowpart (SImode, operands[0]), GEN_INT (low)));
#endif
  DONE;
})

(define_split
  [(set (match_operand:DI 0 "register_operand" "")
        (match_operand:DI 1 "const_double_operand" ""))]
  "reload_completed
   && (! TARGET_V9
       || (! TARGET_ARCH64
           && ((GET_CODE (operands[0]) == REG
                && REGNO (operands[0]) < 32)
               || (GET_CODE (operands[0]) == SUBREG
                   && GET_CODE (SUBREG_REG (operands[0])) == REG
                   && REGNO (SUBREG_REG (operands[0])) < 32))))"
  [(clobber (const_int 0))]
{
  emit_insn (gen_movsi (gen_highpart (SImode, operands[0]),
			GEN_INT (CONST_DOUBLE_HIGH (operands[1]))));

  /* Slick... but this trick loses if this subreg constant part
     can be done in one insn.  */
  if (CONST_DOUBLE_LOW (operands[1]) == CONST_DOUBLE_HIGH (operands[1])
      && ! SPARC_SETHI32_P (CONST_DOUBLE_HIGH (operands[1]))
      && ! SPARC_SIMM13_P (CONST_DOUBLE_HIGH (operands[1])))
    {
      emit_insn (gen_movsi (gen_lowpart (SImode, operands[0]),
			    gen_highpart (SImode, operands[0])));
    }
  else
    {
      emit_insn (gen_movsi (gen_lowpart (SImode, operands[0]),
			    GEN_INT (CONST_DOUBLE_LOW (operands[1]))));
    }
  DONE;
})

(define_split
  [(set (match_operand:DI 0 "register_operand" "")
        (match_operand:DI 1 "register_operand" ""))]
  "reload_completed
   && (! TARGET_V9
       || (! TARGET_ARCH64
           && ((GET_CODE (operands[0]) == REG
                && REGNO (operands[0]) < 32)
               || (GET_CODE (operands[0]) == SUBREG
                   && GET_CODE (SUBREG_REG (operands[0])) == REG
                   && REGNO (SUBREG_REG (operands[0])) < 32))))"
  [(clobber (const_int 0))]
{
  rtx set_dest = operands[0];
  rtx set_src = operands[1];
  rtx dest1, dest2;
  rtx src1, src2;

  dest1 = gen_highpart (SImode, set_dest);
  dest2 = gen_lowpart (SImode, set_dest);
  src1 = gen_highpart (SImode, set_src);
  src2 = gen_lowpart (SImode, set_src);

  /* Now emit using the real source and destination we found, swapping
     the order if we detect overlap.  */
  if (reg_overlap_mentioned_p (dest1, src2))
    {
      emit_insn (gen_movsi (dest2, src2));
      emit_insn (gen_movsi (dest1, src1));
    }
  else
    {
      emit_insn (gen_movsi (dest1, src1));
      emit_insn (gen_movsi (dest2, src2));
    }
  DONE;
})

;; Now handle the cases of memory moves from/to non-even
;; DI mode register pairs.
(define_split
  [(set (match_operand:DI 0 "register_operand" "")
        (match_operand:DI 1 "memory_operand" ""))]
  "(! TARGET_ARCH64
    && reload_completed
    && sparc_splitdi_legitimate (operands[0], operands[1]))"
  [(clobber (const_int 0))]
{
  rtx word0 = adjust_address (operands[1], SImode, 0);
  rtx word1 = adjust_address (operands[1], SImode, 4);
  rtx high_part = gen_highpart (SImode, operands[0]);
  rtx low_part = gen_lowpart (SImode, operands[0]);

  if (reg_overlap_mentioned_p (high_part, word1))
    {
      emit_insn (gen_movsi (low_part, word1));
      emit_insn (gen_movsi (high_part, word0));
    }
  else
    {
      emit_insn (gen_movsi (high_part, word0));
      emit_insn (gen_movsi (low_part, word1));
    }
  DONE;
})

(define_split
  [(set (match_operand:DI 0 "memory_operand" "")
        (match_operand:DI 1 "register_operand" ""))]
  "(! TARGET_ARCH64
    && reload_completed
    && sparc_splitdi_legitimate (operands[1], operands[0]))"
  [(clobber (const_int 0))]
{
  emit_insn (gen_movsi (adjust_address (operands[0], SImode, 0),
			gen_highpart (SImode, operands[1])));
  emit_insn (gen_movsi (adjust_address (operands[0], SImode, 4),
			gen_lowpart (SImode, operands[1])));
  DONE;
})

(define_split
  [(set (match_operand:DI 0 "memory_operand" "")
        (match_operand:DI 1 "const_zero_operand" ""))]
  "reload_completed
   && (! TARGET_V9
       || (! TARGET_ARCH64
	   && ! mem_min_alignment (operands[0], 8)))
   && offsettable_memref_p (operands[0])"
  [(clobber (const_int 0))]
{
  emit_insn (gen_movsi (adjust_address (operands[0], SImode, 0), const0_rtx));
  emit_insn (gen_movsi (adjust_address (operands[0], SImode, 4), const0_rtx));
  DONE;
})


;; Floating point and vector move instructions

;; We don't define V1SI because SI should work just fine.
(define_mode_macro V32 [SF V2HI V4QI])

;; Yes, you guessed it right, the former movsf expander.
(define_expand "mov<V32:mode>"
  [(set (match_operand:V32 0 "nonimmediate_operand" "")
	(match_operand:V32 1 "general_operand" ""))]
  "<V32:MODE>mode == SFmode || TARGET_VIS"
{
  if (sparc_expand_move (<V32:MODE>mode, operands))
    DONE;
})

(define_insn "*movsf_insn"
  [(set (match_operand:V32 0 "nonimmediate_operand" "=d,f,*r,*r,*r,f,*r,m,m")
	(match_operand:V32 1 "input_operand"        "GY,f,*rRY,Q,S,m,m,f,*rGY"))]
  "TARGET_FPU
   && (register_operand (operands[0], <V32:MODE>mode)
       || register_or_zero_operand (operands[1], <V32:MODE>mode))"
{
  if (GET_CODE (operands[1]) == CONST_DOUBLE
      && (which_alternative == 2
          || which_alternative == 3
          || which_alternative == 4))
    {
      REAL_VALUE_TYPE r;
      long i;

      REAL_VALUE_FROM_CONST_DOUBLE (r, operands[1]);
      REAL_VALUE_TO_TARGET_SINGLE (r, i);
      operands[1] = GEN_INT (i);
    }

  switch (which_alternative)
    {
    case 0:
      return "fzeros\t%0";
    case 1:
      return "fmovs\t%1, %0";
    case 2:
      return "mov\t%1, %0";
    case 3:
      return "sethi\t%%hi(%a1), %0";
    case 4:
      return "#";
    case 5:
    case 6:
      return "ld\t%1, %0";
    case 7:
    case 8:
      return "st\t%r1, %0";
    default:
      gcc_unreachable ();
    }
}
  [(set_attr "type" "fga,fpmove,*,*,*,fpload,load,fpstore,store")])

;; Exactly the same as above, except that all `f' cases are deleted.
;; This is necessary to prevent reload from ever trying to use a `f' reg
;; when -mno-fpu.

(define_insn "*movsf_insn_no_fpu"
  [(set (match_operand:SF 0 "nonimmediate_operand" "=r,r,r,r,m")
	(match_operand:SF 1 "input_operand"    "rR,Q,S,m,rG"))]
  "! TARGET_FPU
   && (register_operand (operands[0], SFmode)
       || register_or_zero_operand (operands[1], SFmode))"
{
  if (GET_CODE (operands[1]) == CONST_DOUBLE
      && (which_alternative == 0
          || which_alternative == 1
          || which_alternative == 2))
    {
      REAL_VALUE_TYPE r;
      long i;

      REAL_VALUE_FROM_CONST_DOUBLE (r, operands[1]);
      REAL_VALUE_TO_TARGET_SINGLE (r, i);
      operands[1] = GEN_INT (i);
    }

  switch (which_alternative)
    {
    case 0:
      return "mov\t%1, %0";
    case 1:
      return "sethi\t%%hi(%a1), %0";
    case 2:
      return "#";
    case 3:
      return "ld\t%1, %0";
    case 4:
      return "st\t%r1, %0";
    default:
      gcc_unreachable ();
    }
}
  [(set_attr "type" "*,*,*,load,store")])

;; The following 3 patterns build SFmode constants in integer registers.

(define_insn "*movsf_lo_sum"
  [(set (match_operand:SF 0 "register_operand" "=r")
        (lo_sum:SF (match_operand:SF 1 "register_operand" "r")
                   (match_operand:SF 2 "fp_const_high_losum_operand" "S")))]
  ""
{
  REAL_VALUE_TYPE r;
  long i;

  REAL_VALUE_FROM_CONST_DOUBLE (r, operands[2]);
  REAL_VALUE_TO_TARGET_SINGLE (r, i);
  operands[2] = GEN_INT (i);
  return "or\t%1, %%lo(%a2), %0";
})

(define_insn "*movsf_high"
  [(set (match_operand:SF 0 "register_operand" "=r")
        (high:SF (match_operand:SF 1 "fp_const_high_losum_operand" "S")))]
  ""
{
  REAL_VALUE_TYPE r;
  long i;

  REAL_VALUE_FROM_CONST_DOUBLE (r, operands[1]);
  REAL_VALUE_TO_TARGET_SINGLE (r, i);
  operands[1] = GEN_INT (i);
  return "sethi\t%%hi(%1), %0";
})

(define_split
  [(set (match_operand:SF 0 "register_operand" "")
        (match_operand:SF 1 "fp_const_high_losum_operand" ""))]
  "REG_P (operands[0]) && REGNO (operands[0]) < 32"
  [(set (match_dup 0) (high:SF (match_dup 1)))
   (set (match_dup 0) (lo_sum:SF (match_dup 0) (match_dup 1)))])

(define_mode_macro V64 [DF V2SI V4HI V8QI])

;; Yes, you again guessed it right, the former movdf expander.
(define_expand "mov<V64:mode>"
  [(set (match_operand:V64 0 "nonimmediate_operand" "")
	(match_operand:V64 1 "general_operand" ""))]
  "<V64:MODE>mode == DFmode || TARGET_VIS"
{
  if (sparc_expand_move (<V64:MODE>mode, operands))
    DONE;
})

;; Be careful, fmovd does not exist when !v9.
(define_insn "*movdf_insn_sp32"
  [(set (match_operand:DF 0 "nonimmediate_operand" "=e,W,U,T,o,e,*r,o,e,o")
	(match_operand:DF 1 "input_operand"    "W#F,e,T,U,G,e,*rFo,*r,o#F,e"))]
  "TARGET_FPU
   && ! TARGET_V9
   && (register_operand (operands[0], DFmode)
       || register_or_zero_operand (operands[1], DFmode))"
  "@
  ldd\t%1, %0
  std\t%1, %0
  ldd\t%1, %0
  std\t%1, %0
  #
  #
  #
  #
  #
  #"
 [(set_attr "type" "fpload,fpstore,load,store,*,*,*,*,*,*")
  (set_attr "length" "*,*,*,*,2,2,2,2,2,2")])

(define_insn "*movdf_insn_sp32_no_fpu"
  [(set (match_operand:DF 0 "nonimmediate_operand" "=U,T,o,r,o")
	(match_operand:DF 1 "input_operand"    "T,U,G,ro,r"))]
  "! TARGET_FPU
   && ! TARGET_V9
   && (register_operand (operands[0], DFmode)
       || register_or_zero_operand (operands[1], DFmode))"
  "@
  ldd\t%1, %0
  std\t%1, %0
  #
  #
  #"
  [(set_attr "type" "load,store,*,*,*")
   (set_attr "length" "*,*,2,2,2")])

;; We have available v9 double floats but not 64-bit integer registers.
(define_insn "*movdf_insn_sp32_v9"
  [(set (match_operand:V64 0 "nonimmediate_operand" "=b,e,e,T,W,U,T,f,*r,o")
        (match_operand:V64 1 "input_operand" "GY,e,W#F,GY,e,T,U,o#F,*roGYF,*rGYf"))]
  "TARGET_FPU
   && TARGET_V9
   && ! TARGET_ARCH64
   && (register_operand (operands[0], <V64:MODE>mode)
       || register_or_zero_operand (operands[1], <V64:MODE>mode))"
  "@
  fzero\t%0
  fmovd\t%1, %0
  ldd\t%1, %0
  stx\t%r1, %0
  std\t%1, %0
  ldd\t%1, %0
  std\t%1, %0
  #
  #
  #"
  [(set_attr "type" "fga,fpmove,load,store,store,load,store,*,*,*")
   (set_attr "length" "*,*,*,*,*,*,*,2,2,2")
   (set_attr "fptype" "double,double,*,*,*,*,*,*,*,*")])

(define_insn "*movdf_insn_sp32_v9_no_fpu"
  [(set (match_operand:DF 0 "nonimmediate_operand" "=U,T,T,r,o")
	(match_operand:DF 1 "input_operand"    "T,U,G,ro,rG"))]
  "! TARGET_FPU
   && TARGET_V9
   && ! TARGET_ARCH64
   && (register_operand (operands[0], DFmode)
       || register_or_zero_operand (operands[1], DFmode))"
  "@
  ldd\t%1, %0
  std\t%1, %0
  stx\t%r1, %0
  #
  #"
  [(set_attr "type" "load,store,store,*,*")
   (set_attr "length" "*,*,*,2,2")])

;; We have available both v9 double floats and 64-bit integer registers.
(define_insn "*movdf_insn_sp64"
  [(set (match_operand:V64 0 "nonimmediate_operand" "=b,e,e,W,*r,*r,m,*r")
        (match_operand:V64 1 "input_operand"    "GY,e,W#F,e,*rGY,m,*rGY,F"))]
  "TARGET_FPU
   && TARGET_ARCH64
   && (register_operand (operands[0], <V64:MODE>mode)
       || register_or_zero_operand (operands[1], <V64:MODE>mode))"
  "@
  fzero\t%0
  fmovd\t%1, %0
  ldd\t%1, %0
  std\t%1, %0
  mov\t%r1, %0
  ldx\t%1, %0
  stx\t%r1, %0
  #"
  [(set_attr "type" "fga,fpmove,load,store,*,load,store,*")
   (set_attr "length" "*,*,*,*,*,*,*,2")
   (set_attr "fptype" "double,double,*,*,*,*,*,*")])

(define_insn "*movdf_insn_sp64_no_fpu"
  [(set (match_operand:DF 0 "nonimmediate_operand" "=r,r,m")
        (match_operand:DF 1 "input_operand"    "r,m,rG"))]
  "! TARGET_FPU
   && TARGET_ARCH64
   && (register_operand (operands[0], DFmode)
       || register_or_zero_operand (operands[1], DFmode))"
  "@
  mov\t%1, %0
  ldx\t%1, %0
  stx\t%r1, %0"
  [(set_attr "type" "*,load,store")])

;; This pattern build DFmode constants in integer registers.
(define_split
  [(set (match_operand:DF 0 "register_operand" "")
        (match_operand:DF 1 "const_double_operand" ""))]
  "TARGET_FPU
   && (GET_CODE (operands[0]) == REG
       && REGNO (operands[0]) < 32)
   && ! const_zero_operand(operands[1], DFmode)
   && reload_completed"
  [(clobber (const_int 0))]
{
  REAL_VALUE_TYPE r;
  long l[2];

  REAL_VALUE_FROM_CONST_DOUBLE (r, operands[1]);
  REAL_VALUE_TO_TARGET_DOUBLE (r, l);
  operands[0] = gen_rtx_raw_REG (DImode, REGNO (operands[0]));

  if (TARGET_ARCH64)
    {
#if HOST_BITS_PER_WIDE_INT == 32
      gcc_unreachable ();
#else
      HOST_WIDE_INT val;

      val = ((HOST_WIDE_INT)(unsigned long)l[1] |
             ((HOST_WIDE_INT)(unsigned long)l[0] << 32));
      emit_insn (gen_movdi (operands[0], gen_int_mode (val, DImode)));
#endif
    }
  else
    {
      emit_insn (gen_movsi (gen_highpart (SImode, operands[0]),
			    gen_int_mode (l[0], SImode)));

      /* Slick... but this trick loses if this subreg constant part
         can be done in one insn.  */
      if (l[1] == l[0]
	  && ! SPARC_SETHI32_P (l[0])
	  && ! SPARC_SIMM13_P (l[0]))
        {
          emit_insn (gen_movsi (gen_lowpart (SImode, operands[0]),
			        gen_highpart (SImode, operands[0])));
        }
      else
        {
          emit_insn (gen_movsi (gen_lowpart (SImode, operands[0]),
			        gen_int_mode (l[1], SImode)));
        }
    }
  DONE;
})

;; Ok, now the splits to handle all the multi insn and
;; mis-aligned memory address cases.
;; In these splits please take note that we must be
;; careful when V9 but not ARCH64 because the integer
;; register DFmode cases must be handled.
(define_split
  [(set (match_operand:V64 0 "register_operand" "")
        (match_operand:V64 1 "register_operand" ""))]
  "(! TARGET_V9
    || (! TARGET_ARCH64
        && ((GET_CODE (operands[0]) == REG
             && REGNO (operands[0]) < 32)
            || (GET_CODE (operands[0]) == SUBREG
                && GET_CODE (SUBREG_REG (operands[0])) == REG
                && REGNO (SUBREG_REG (operands[0])) < 32))))
   && reload_completed"
  [(clobber (const_int 0))]
{
  rtx set_dest = operands[0];
  rtx set_src = operands[1];
  rtx dest1, dest2;
  rtx src1, src2;
  enum machine_mode half_mode;

  /* We can be expanded for DFmode or integral vector modes.  */
  if (<V64:MODE>mode == DFmode)
    half_mode = SFmode;
  else
    half_mode = SImode;
  
  dest1 = gen_highpart (half_mode, set_dest);
  dest2 = gen_lowpart (half_mode, set_dest);
  src1 = gen_highpart (half_mode, set_src);
  src2 = gen_lowpart (half_mode, set_src);

  /* Now emit using the real source and destination we found, swapping
     the order if we detect overlap.  */
  if (reg_overlap_mentioned_p (dest1, src2))
    {
      emit_move_insn_1 (dest2, src2);
      emit_move_insn_1 (dest1, src1);
    }
  else
    {
      emit_move_insn_1 (dest1, src1);
      emit_move_insn_1 (dest2, src2);
    }
  DONE;
})

(define_split
  [(set (match_operand:V64 0 "register_operand" "")
	(match_operand:V64 1 "memory_operand" ""))]
  "reload_completed
   && ! TARGET_ARCH64
   && (((REGNO (operands[0]) % 2) != 0)
       || ! mem_min_alignment (operands[1], 8))
   && offsettable_memref_p (operands[1])"
  [(clobber (const_int 0))]
{
  enum machine_mode half_mode;
  rtx word0, word1;

  /* We can be expanded for DFmode or integral vector modes.  */
  if (<V64:MODE>mode == DFmode)
    half_mode = SFmode;
  else
    half_mode = SImode;

  word0 = adjust_address (operands[1], half_mode, 0);
  word1 = adjust_address (operands[1], half_mode, 4);

  if (reg_overlap_mentioned_p (gen_highpart (half_mode, operands[0]), word1))
    {
      emit_move_insn_1 (gen_lowpart (half_mode, operands[0]), word1);
      emit_move_insn_1 (gen_highpart (half_mode, operands[0]), word0);
    }
  else
    {
      emit_move_insn_1 (gen_highpart (half_mode, operands[0]), word0);
      emit_move_insn_1 (gen_lowpart (half_mode, operands[0]), word1);
    }
  DONE;
})

(define_split
  [(set (match_operand:V64 0 "memory_operand" "")
	(match_operand:V64 1 "register_operand" ""))]
  "reload_completed
   && ! TARGET_ARCH64
   && (((REGNO (operands[1]) % 2) != 0)
       || ! mem_min_alignment (operands[0], 8))
   && offsettable_memref_p (operands[0])"
  [(clobber (const_int 0))]
{
  enum machine_mode half_mode;
  rtx word0, word1;

  /* We can be expanded for DFmode or integral vector modes.  */
  if (<V64:MODE>mode == DFmode)
    half_mode = SFmode;
  else
    half_mode = SImode;

  word0 = adjust_address (operands[0], half_mode, 0);
  word1 = adjust_address (operands[0], half_mode, 4);

  emit_move_insn_1 (word0, gen_highpart (half_mode, operands[1]));
  emit_move_insn_1 (word1, gen_lowpart (half_mode, operands[1]));
  DONE;
})

(define_split
  [(set (match_operand:V64 0 "memory_operand" "")
        (match_operand:V64 1 "const_zero_operand" ""))]
  "reload_completed
   && (! TARGET_V9
       || (! TARGET_ARCH64
	   && ! mem_min_alignment (operands[0], 8)))
   && offsettable_memref_p (operands[0])"
  [(clobber (const_int 0))]
{
  enum machine_mode half_mode;
  rtx dest1, dest2;

  /* We can be expanded for DFmode or integral vector modes.  */
  if (<V64:MODE>mode == DFmode)
    half_mode = SFmode;
  else
    half_mode = SImode;

  dest1 = adjust_address (operands[0], half_mode, 0);
  dest2 = adjust_address (operands[0], half_mode, 4);

  emit_move_insn_1 (dest1, CONST0_RTX (half_mode));
  emit_move_insn_1 (dest2, CONST0_RTX (half_mode));
  DONE;
})

(define_split
  [(set (match_operand:V64 0 "register_operand" "")
        (match_operand:V64 1 "const_zero_operand" ""))]
  "reload_completed
   && ! TARGET_ARCH64
   && ((GET_CODE (operands[0]) == REG
	&& REGNO (operands[0]) < 32)
       || (GET_CODE (operands[0]) == SUBREG
	   && GET_CODE (SUBREG_REG (operands[0])) == REG
	   && REGNO (SUBREG_REG (operands[0])) < 32))"
  [(clobber (const_int 0))]
{
  enum machine_mode half_mode;
  rtx set_dest = operands[0];
  rtx dest1, dest2;

  /* We can be expanded for DFmode or integral vector modes.  */
  if (<V64:MODE>mode == DFmode)
    half_mode = SFmode;
  else
    half_mode = SImode;

  dest1 = gen_highpart (half_mode, set_dest);
  dest2 = gen_lowpart (half_mode, set_dest);
  emit_move_insn_1 (dest1, CONST0_RTX (half_mode));
  emit_move_insn_1 (dest2, CONST0_RTX (half_mode));
  DONE;
})

(define_expand "movtf"
  [(set (match_operand:TF 0 "nonimmediate_operand" "")
	(match_operand:TF 1 "general_operand" ""))]
  ""
{
  if (sparc_expand_move (TFmode, operands))
    DONE;
})

(define_insn "*movtf_insn_sp32"
  [(set (match_operand:TF 0 "nonimmediate_operand" "=b,e,o,U,r")
	(match_operand:TF 1 "input_operand"    "G,oe,GeUr,o,roG"))]
  "TARGET_FPU
   && ! TARGET_ARCH64
   && (register_operand (operands[0], TFmode)
       || register_or_zero_operand (operands[1], TFmode))"
  "#"
  [(set_attr "length" "4")])

;; Exactly the same as above, except that all `e' cases are deleted.
;; This is necessary to prevent reload from ever trying to use a `e' reg
;; when -mno-fpu.

(define_insn "*movtf_insn_sp32_no_fpu"
  [(set (match_operand:TF 0 "nonimmediate_operand" "=o,U,o,r,o")
	(match_operand:TF 1 "input_operand"    "G,o,U,roG,r"))]
  "! TARGET_FPU
   && ! TARGET_ARCH64
   && (register_operand (operands[0], TFmode)
       || register_or_zero_operand (operands[1], TFmode))"
  "#"
  [(set_attr "length" "4")])

(define_insn "*movtf_insn_sp64"
  [(set (match_operand:TF 0 "nonimmediate_operand" "=b,e,o,r")
        (match_operand:TF 1 "input_operand"    "G,oe,Ger,roG"))]
  "TARGET_FPU
   && TARGET_ARCH64
   && ! TARGET_HARD_QUAD
   && (register_operand (operands[0], TFmode)
       || register_or_zero_operand (operands[1], TFmode))"
  "#"
  [(set_attr "length" "2")])

(define_insn "*movtf_insn_sp64_hq"
  [(set (match_operand:TF 0 "nonimmediate_operand" "=b,e,e,m,o,r")
        (match_operand:TF 1 "input_operand"    "G,e,m,e,rG,roG"))]
  "TARGET_FPU
   && TARGET_ARCH64
   && TARGET_HARD_QUAD
   && (register_operand (operands[0], TFmode)
       || register_or_zero_operand (operands[1], TFmode))"
  "@
  #
  fmovq\t%1, %0
  ldq\t%1, %0
  stq\t%1, %0
  #
  #"
  [(set_attr "type" "*,fpmove,fpload,fpstore,*,*")
   (set_attr "length" "2,*,*,*,2,2")])

(define_insn "*movtf_insn_sp64_no_fpu"
  [(set (match_operand:TF 0 "nonimmediate_operand" "=r,o")
        (match_operand:TF 1 "input_operand"    "orG,rG"))]
  "! TARGET_FPU
   && TARGET_ARCH64
   && (register_operand (operands[0], TFmode)
       || register_or_zero_operand (operands[1], TFmode))"
  "#"
  [(set_attr "length" "2")])

;; Now all the splits to handle multi-insn TF mode moves.
(define_split
  [(set (match_operand:TF 0 "register_operand" "")
        (match_operand:TF 1 "register_operand" ""))]
  "reload_completed
   && (! TARGET_ARCH64
       || (TARGET_FPU
           && ! TARGET_HARD_QUAD)
       || ! fp_register_operand (operands[0], TFmode))"
  [(clobber (const_int 0))]
{
  rtx set_dest = operands[0];
  rtx set_src = operands[1];
  rtx dest1, dest2;
  rtx src1, src2;

  dest1 = gen_df_reg (set_dest, 0);
  dest2 = gen_df_reg (set_dest, 1);
  src1 = gen_df_reg (set_src, 0);
  src2 = gen_df_reg (set_src, 1);

  /* Now emit using the real source and destination we found, swapping
     the order if we detect overlap.  */
  if (reg_overlap_mentioned_p (dest1, src2))
    {
      emit_insn (gen_movdf (dest2, src2));
      emit_insn (gen_movdf (dest1, src1));
    }
  else
    {
      emit_insn (gen_movdf (dest1, src1));
      emit_insn (gen_movdf (dest2, src2));
    }
  DONE;
})

(define_split
  [(set (match_operand:TF 0 "nonimmediate_operand" "")
        (match_operand:TF 1 "const_zero_operand" ""))]
  "reload_completed"
  [(clobber (const_int 0))]
{
  rtx set_dest = operands[0];
  rtx dest1, dest2;

  switch (GET_CODE (set_dest))
    {
    case REG:
      dest1 = gen_df_reg (set_dest, 0);
      dest2 = gen_df_reg (set_dest, 1);
      break;
    case MEM:
      dest1 = adjust_address (set_dest, DFmode, 0);
      dest2 = adjust_address (set_dest, DFmode, 8);
      break;
    default:
      gcc_unreachable ();      
    }

  emit_insn (gen_movdf (dest1, CONST0_RTX (DFmode)));
  emit_insn (gen_movdf (dest2, CONST0_RTX (DFmode)));
  DONE;
})

(define_split
  [(set (match_operand:TF 0 "register_operand" "")
        (match_operand:TF 1 "memory_operand" ""))]
  "(reload_completed
    && offsettable_memref_p (operands[1])
    && (! TARGET_ARCH64
	|| ! TARGET_HARD_QUAD
	|| ! fp_register_operand (operands[0], TFmode)))"
  [(clobber (const_int 0))]
{
  rtx word0 = adjust_address (operands[1], DFmode, 0);
  rtx word1 = adjust_address (operands[1], DFmode, 8);
  rtx set_dest, dest1, dest2;

  set_dest = operands[0];

  dest1 = gen_df_reg (set_dest, 0);
  dest2 = gen_df_reg (set_dest, 1);

  /* Now output, ordering such that we don't clobber any registers
     mentioned in the address.  */
  if (reg_overlap_mentioned_p (dest1, word1))

    {
      emit_insn (gen_movdf (dest2, word1));
      emit_insn (gen_movdf (dest1, word0));
    }
  else
   {
      emit_insn (gen_movdf (dest1, word0));
      emit_insn (gen_movdf (dest2, word1));
   }
  DONE;
})

(define_split
  [(set (match_operand:TF 0 "memory_operand" "")
	(match_operand:TF 1 "register_operand" ""))]
  "(reload_completed
    && offsettable_memref_p (operands[0])
    && (! TARGET_ARCH64
	|| ! TARGET_HARD_QUAD
	|| ! fp_register_operand (operands[1], TFmode)))"
  [(clobber (const_int 0))]
{
  rtx set_src = operands[1];

  emit_insn (gen_movdf (adjust_address (operands[0], DFmode, 0),
			gen_df_reg (set_src, 0)));
  emit_insn (gen_movdf (adjust_address (operands[0], DFmode, 8),
			gen_df_reg (set_src, 1)));
  DONE;
})


;; SPARC-V9 conditional move instructions.

;; We can handle larger constants here for some flavors, but for now we keep
;; it simple and only allow those constants supported by all flavors.
;; Note that emit_conditional_move canonicalizes operands 2,3 so that operand
;; 3 contains the constant if one is present, but we handle either for
;; generality (sparc.c puts a constant in operand 2).

(define_expand "movqicc"
  [(set (match_operand:QI 0 "register_operand" "")
	(if_then_else:QI (match_operand 1 "comparison_operator" "")
			 (match_operand:QI 2 "arith10_operand" "")
			 (match_operand:QI 3 "arith10_operand" "")))]
  "TARGET_V9"
{
  enum rtx_code code = GET_CODE (operands[1]);

  if (GET_MODE (sparc_compare_op0) == DImode
      && ! TARGET_ARCH64)
    FAIL;

  if (sparc_compare_op1 == const0_rtx
      && GET_CODE (sparc_compare_op0) == REG
      && GET_MODE (sparc_compare_op0) == DImode
      && v9_regcmp_p (code))
    {
      operands[1] = gen_rtx_fmt_ee (code, DImode,
			     sparc_compare_op0, sparc_compare_op1);
    }
  else
    {
      rtx cc_reg = gen_compare_reg (code);
      operands[1] = gen_rtx_fmt_ee (code, GET_MODE (cc_reg), cc_reg, const0_rtx);
    }
})

(define_expand "movhicc"
  [(set (match_operand:HI 0 "register_operand" "")
	(if_then_else:HI (match_operand 1 "comparison_operator" "")
			 (match_operand:HI 2 "arith10_operand" "")
			 (match_operand:HI 3 "arith10_operand" "")))]
  "TARGET_V9"
{
  enum rtx_code code = GET_CODE (operands[1]);

  if (GET_MODE (sparc_compare_op0) == DImode
      && ! TARGET_ARCH64)
    FAIL;

  if (sparc_compare_op1 == const0_rtx
      && GET_CODE (sparc_compare_op0) == REG
      && GET_MODE (sparc_compare_op0) == DImode
      && v9_regcmp_p (code))
    {
      operands[1] = gen_rtx_fmt_ee (code, DImode,
			     sparc_compare_op0, sparc_compare_op1);
    }
  else
    {
      rtx cc_reg = gen_compare_reg (code);
      operands[1] = gen_rtx_fmt_ee (code, GET_MODE (cc_reg), cc_reg, const0_rtx);
    }
})

(define_expand "movsicc"
  [(set (match_operand:SI 0 "register_operand" "")
	(if_then_else:SI (match_operand 1 "comparison_operator" "")
			 (match_operand:SI 2 "arith10_operand" "")
			 (match_operand:SI 3 "arith10_operand" "")))]
  "TARGET_V9"
{
  enum rtx_code code = GET_CODE (operands[1]);
  enum machine_mode op0_mode = GET_MODE (sparc_compare_op0);

  if (sparc_compare_op1 == const0_rtx
      && GET_CODE (sparc_compare_op0) == REG
      && (TARGET_ARCH64 && op0_mode == DImode && v9_regcmp_p (code)))
    {
      operands[1] = gen_rtx_fmt_ee (code, op0_mode,
			     sparc_compare_op0, sparc_compare_op1);
    }
  else
    {
      rtx cc_reg = gen_compare_reg (code);
      operands[1] = gen_rtx_fmt_ee (code, GET_MODE (cc_reg),
				    cc_reg, const0_rtx);
    }
})

(define_expand "movdicc"
  [(set (match_operand:DI 0 "register_operand" "")
	(if_then_else:DI (match_operand 1 "comparison_operator" "")
			 (match_operand:DI 2 "arith10_operand" "")
			 (match_operand:DI 3 "arith10_operand" "")))]
  "TARGET_ARCH64"
{
  enum rtx_code code = GET_CODE (operands[1]);

  if (sparc_compare_op1 == const0_rtx
      && GET_CODE (sparc_compare_op0) == REG
      && GET_MODE (sparc_compare_op0) == DImode
      && v9_regcmp_p (code))
    {
      operands[1] = gen_rtx_fmt_ee (code, DImode,
			     sparc_compare_op0, sparc_compare_op1);
    }
  else
    {
      rtx cc_reg = gen_compare_reg (code);
      operands[1] = gen_rtx_fmt_ee (code, GET_MODE (cc_reg),
				    cc_reg, const0_rtx);
    }
})

(define_expand "movsfcc"
  [(set (match_operand:SF 0 "register_operand" "")
	(if_then_else:SF (match_operand 1 "comparison_operator" "")
			 (match_operand:SF 2 "register_operand" "")
			 (match_operand:SF 3 "register_operand" "")))]
  "TARGET_V9 && TARGET_FPU"
{
  enum rtx_code code = GET_CODE (operands[1]);

  if (GET_MODE (sparc_compare_op0) == DImode
      && ! TARGET_ARCH64)
    FAIL;

  if (sparc_compare_op1 == const0_rtx
      && GET_CODE (sparc_compare_op0) == REG
      && GET_MODE (sparc_compare_op0) == DImode
      && v9_regcmp_p (code))
    {
      operands[1] = gen_rtx_fmt_ee (code, DImode,
			     sparc_compare_op0, sparc_compare_op1);
    }
  else
    {
      rtx cc_reg = gen_compare_reg (code);
      operands[1] = gen_rtx_fmt_ee (code, GET_MODE (cc_reg), cc_reg, const0_rtx);
    }
})

(define_expand "movdfcc"
  [(set (match_operand:DF 0 "register_operand" "")
	(if_then_else:DF (match_operand 1 "comparison_operator" "")
			 (match_operand:DF 2 "register_operand" "")
			 (match_operand:DF 3 "register_operand" "")))]
  "TARGET_V9 && TARGET_FPU"
{
  enum rtx_code code = GET_CODE (operands[1]);

  if (GET_MODE (sparc_compare_op0) == DImode
      && ! TARGET_ARCH64)
    FAIL;

  if (sparc_compare_op1 == const0_rtx
      && GET_CODE (sparc_compare_op0) == REG
      && GET_MODE (sparc_compare_op0) == DImode
      && v9_regcmp_p (code))
    {
      operands[1] = gen_rtx_fmt_ee (code, DImode,
			     sparc_compare_op0, sparc_compare_op1);
    }
  else
    {
      rtx cc_reg = gen_compare_reg (code);
      operands[1] = gen_rtx_fmt_ee (code, GET_MODE (cc_reg), cc_reg, const0_rtx);
    }
})

(define_expand "movtfcc"
  [(set (match_operand:TF 0 "register_operand" "")
	(if_then_else:TF (match_operand 1 "comparison_operator" "")
			 (match_operand:TF 2 "register_operand" "")
			 (match_operand:TF 3 "register_operand" "")))]
  "TARGET_V9 && TARGET_FPU"
{
  enum rtx_code code = GET_CODE (operands[1]);

  if (GET_MODE (sparc_compare_op0) == DImode
      && ! TARGET_ARCH64)
    FAIL;

  if (sparc_compare_op1 == const0_rtx
      && GET_CODE (sparc_compare_op0) == REG
      && GET_MODE (sparc_compare_op0) == DImode
      && v9_regcmp_p (code))
    {
      operands[1] = gen_rtx_fmt_ee (code, DImode,
			     sparc_compare_op0, sparc_compare_op1);
    }
  else
    {
      rtx cc_reg = gen_compare_reg (code);
      operands[1] = gen_rtx_fmt_ee (code, GET_MODE (cc_reg), cc_reg, const0_rtx);
    }
})

;; Conditional move define_insns.

(define_insn "*movqi_cc_sp64"
  [(set (match_operand:QI 0 "register_operand" "=r,r")
	(if_then_else:QI (match_operator 1 "comparison_operator"
				[(match_operand 2 "icc_or_fcc_register_operand" "X,X")
				 (const_int 0)])
                         (match_operand:QI 3 "arith11_operand" "rL,0")
                         (match_operand:QI 4 "arith11_operand" "0,rL")))]
  "TARGET_V9"
  "@
   mov%C1\t%x2, %3, %0
   mov%c1\t%x2, %4, %0"
  [(set_attr "type" "cmove")])

(define_insn "*movhi_cc_sp64"
  [(set (match_operand:HI 0 "register_operand" "=r,r")
	(if_then_else:HI (match_operator 1 "comparison_operator"
				[(match_operand 2 "icc_or_fcc_register_operand" "X,X")
				 (const_int 0)])
                         (match_operand:HI 3 "arith11_operand" "rL,0")
                         (match_operand:HI 4 "arith11_operand" "0,rL")))]
  "TARGET_V9"
  "@
   mov%C1\t%x2, %3, %0
   mov%c1\t%x2, %4, %0"
  [(set_attr "type" "cmove")])

(define_insn "*movsi_cc_sp64"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(if_then_else:SI (match_operator 1 "comparison_operator"
				[(match_operand 2 "icc_or_fcc_register_operand" "X,X")
				 (const_int 0)])
                         (match_operand:SI 3 "arith11_operand" "rL,0")
                         (match_operand:SI 4 "arith11_operand" "0,rL")))]
  "TARGET_V9"
  "@
   mov%C1\t%x2, %3, %0
   mov%c1\t%x2, %4, %0"
  [(set_attr "type" "cmove")])

(define_insn "*movdi_cc_sp64"
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(if_then_else:DI (match_operator 1 "comparison_operator"
				[(match_operand 2 "icc_or_fcc_register_operand" "X,X")
				 (const_int 0)])
                         (match_operand:DI 3 "arith11_operand" "rL,0")
                         (match_operand:DI 4 "arith11_operand" "0,rL")))]
  "TARGET_ARCH64"
  "@
   mov%C1\t%x2, %3, %0
   mov%c1\t%x2, %4, %0"
  [(set_attr "type" "cmove")])

(define_insn "*movdi_cc_sp64_trunc"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(if_then_else:SI (match_operator 1 "comparison_operator"
				[(match_operand 2 "icc_or_fcc_register_operand" "X,X")
				 (const_int 0)])
                         (match_operand:SI 3 "arith11_operand" "rL,0")
                         (match_operand:SI 4 "arith11_operand" "0,rL")))]
  "TARGET_ARCH64"
  "@
   mov%C1\t%x2, %3, %0
   mov%c1\t%x2, %4, %0"
  [(set_attr "type" "cmove")])

(define_insn "*movsf_cc_sp64"
  [(set (match_operand:SF 0 "register_operand" "=f,f")
	(if_then_else:SF (match_operator 1 "comparison_operator"
				[(match_operand 2 "icc_or_fcc_register_operand" "X,X")
				 (const_int 0)])
                         (match_operand:SF 3 "register_operand" "f,0")
                         (match_operand:SF 4 "register_operand" "0,f")))]
  "TARGET_V9 && TARGET_FPU"
  "@
   fmovs%C1\t%x2, %3, %0
   fmovs%c1\t%x2, %4, %0"
  [(set_attr "type" "fpcmove")])

(define_insn "movdf_cc_sp64"
  [(set (match_operand:DF 0 "register_operand" "=e,e")
	(if_then_else:DF (match_operator 1 "comparison_operator"
				[(match_operand 2 "icc_or_fcc_register_operand" "X,X")
				 (const_int 0)])
                         (match_operand:DF 3 "register_operand" "e,0")
                         (match_operand:DF 4 "register_operand" "0,e")))]
  "TARGET_V9 && TARGET_FPU"
  "@
   fmovd%C1\t%x2, %3, %0
   fmovd%c1\t%x2, %4, %0"
  [(set_attr "type" "fpcmove")
   (set_attr "fptype" "double")])

(define_insn "*movtf_cc_hq_sp64"
  [(set (match_operand:TF 0 "register_operand" "=e,e")
	(if_then_else:TF (match_operator 1 "comparison_operator"
				[(match_operand 2 "icc_or_fcc_register_operand" "X,X")
				 (const_int 0)])
                         (match_operand:TF 3 "register_operand" "e,0")
                         (match_operand:TF 4 "register_operand" "0,e")))]
  "TARGET_V9 && TARGET_FPU && TARGET_HARD_QUAD"
  "@
   fmovq%C1\t%x2, %3, %0
   fmovq%c1\t%x2, %4, %0"
  [(set_attr "type" "fpcmove")])

(define_insn_and_split "*movtf_cc_sp64"
  [(set (match_operand:TF 0 "register_operand" "=e,e")
	(if_then_else:TF (match_operator 1 "comparison_operator"
			    [(match_operand 2 "icc_or_fcc_register_operand" "X,X")
			     (const_int 0)])
                         (match_operand:TF 3 "register_operand" "e,0")
                         (match_operand:TF 4 "register_operand" "0,e")))]
  "TARGET_V9 && TARGET_FPU && !TARGET_HARD_QUAD"
  "#"
  "&& reload_completed"
  [(clobber (const_int 0))]
{
  rtx set_dest = operands[0];
  rtx set_srca = operands[3];
  rtx set_srcb = operands[4];
  int third = rtx_equal_p (set_dest, set_srca);
  rtx dest1, dest2;
  rtx srca1, srca2, srcb1, srcb2;

  dest1 = gen_df_reg (set_dest, 0);
  dest2 = gen_df_reg (set_dest, 1);
  srca1 = gen_df_reg (set_srca, 0);
  srca2 = gen_df_reg (set_srca, 1);
  srcb1 = gen_df_reg (set_srcb, 0);
  srcb2 = gen_df_reg (set_srcb, 1);

  /* Now emit using the real source and destination we found, swapping
     the order if we detect overlap.  */
  if ((third && reg_overlap_mentioned_p (dest1, srcb2))
      || (!third && reg_overlap_mentioned_p (dest1, srca2)))
    {
      emit_insn (gen_movdf_cc_sp64 (dest2, operands[1], operands[2], srca2, srcb2));
      emit_insn (gen_movdf_cc_sp64 (dest1, operands[1], operands[2], srca1, srcb1));
    }
  else
    {
      emit_insn (gen_movdf_cc_sp64 (dest1, operands[1], operands[2], srca1, srcb1));
      emit_insn (gen_movdf_cc_sp64 (dest2, operands[1], operands[2], srca2, srcb2));
    }
  DONE;
}
  [(set_attr "length" "2")])

(define_insn "*movqi_cc_reg_sp64"
  [(set (match_operand:QI 0 "register_operand" "=r,r")
	(if_then_else:QI (match_operator 1 "v9_register_compare_operator"
				[(match_operand:DI 2 "register_operand" "r,r")
				 (const_int 0)])
                         (match_operand:QI 3 "arith10_operand" "rM,0")
                         (match_operand:QI 4 "arith10_operand" "0,rM")))]
  "TARGET_ARCH64"
  "@
   movr%D1\t%2, %r3, %0
   movr%d1\t%2, %r4, %0"
  [(set_attr "type" "cmove")])

(define_insn "*movhi_cc_reg_sp64"
  [(set (match_operand:HI 0 "register_operand" "=r,r")
	(if_then_else:HI (match_operator 1 "v9_register_compare_operator"
				[(match_operand:DI 2 "register_operand" "r,r")
				 (const_int 0)])
                         (match_operand:HI 3 "arith10_operand" "rM,0")
                         (match_operand:HI 4 "arith10_operand" "0,rM")))]
  "TARGET_ARCH64"
  "@
   movr%D1\t%2, %r3, %0
   movr%d1\t%2, %r4, %0"
  [(set_attr "type" "cmove")])

(define_insn "*movsi_cc_reg_sp64"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(if_then_else:SI (match_operator 1 "v9_register_compare_operator"
				[(match_operand:DI 2 "register_operand" "r,r")
				 (const_int 0)])
                         (match_operand:SI 3 "arith10_operand" "rM,0")
                         (match_operand:SI 4 "arith10_operand" "0,rM")))]
  "TARGET_ARCH64"
  "@
   movr%D1\t%2, %r3, %0
   movr%d1\t%2, %r4, %0"
  [(set_attr "type" "cmove")])

(define_insn "*movdi_cc_reg_sp64"
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(if_then_else:DI (match_operator 1 "v9_register_compare_operator"
				[(match_operand:DI 2 "register_operand" "r,r")
				 (const_int 0)])
                         (match_operand:DI 3 "arith10_operand" "rM,0")
                         (match_operand:DI 4 "arith10_operand" "0,rM")))]
  "TARGET_ARCH64"
  "@
   movr%D1\t%2, %r3, %0
   movr%d1\t%2, %r4, %0"
  [(set_attr "type" "cmove")])

(define_insn "*movsf_cc_reg_sp64"
  [(set (match_operand:SF 0 "register_operand" "=f,f")
	(if_then_else:SF (match_operator 1 "v9_register_compare_operator"
				[(match_operand:DI 2 "register_operand" "r,r")
				 (const_int 0)])
                         (match_operand:SF 3 "register_operand" "f,0")
                         (match_operand:SF 4 "register_operand" "0,f")))]
  "TARGET_ARCH64 && TARGET_FPU"
  "@
   fmovrs%D1\t%2, %3, %0
   fmovrs%d1\t%2, %4, %0"
  [(set_attr "type" "fpcrmove")])

(define_insn "movdf_cc_reg_sp64"
  [(set (match_operand:DF 0 "register_operand" "=e,e")
	(if_then_else:DF (match_operator 1 "v9_register_compare_operator"
				[(match_operand:DI 2 "register_operand" "r,r")
				 (const_int 0)])
                         (match_operand:DF 3 "register_operand" "e,0")
                         (match_operand:DF 4 "register_operand" "0,e")))]
  "TARGET_ARCH64 && TARGET_FPU"
  "@
   fmovrd%D1\t%2, %3, %0
   fmovrd%d1\t%2, %4, %0"
  [(set_attr "type" "fpcrmove")
   (set_attr "fptype" "double")])

(define_insn "*movtf_cc_reg_hq_sp64"
  [(set (match_operand:TF 0 "register_operand" "=e,e")
	(if_then_else:TF (match_operator 1 "v9_register_compare_operator"
				[(match_operand:DI 2 "register_operand" "r,r")
				 (const_int 0)])
                         (match_operand:TF 3 "register_operand" "e,0")
                         (match_operand:TF 4 "register_operand" "0,e")))]
  "TARGET_ARCH64 && TARGET_FPU && TARGET_HARD_QUAD"
  "@
   fmovrq%D1\t%2, %3, %0
   fmovrq%d1\t%2, %4, %0"
  [(set_attr "type" "fpcrmove")])

(define_insn_and_split "*movtf_cc_reg_sp64"
  [(set (match_operand:TF 0 "register_operand" "=e,e")
	(if_then_else:TF (match_operator 1 "v9_register_compare_operator"
				[(match_operand:DI 2 "register_operand" "r,r")
				 (const_int 0)])
                         (match_operand:TF 3 "register_operand" "e,0")
                         (match_operand:TF 4 "register_operand" "0,e")))]
  "TARGET_ARCH64 && TARGET_FPU && ! TARGET_HARD_QUAD"
  "#"
  "&& reload_completed"
  [(clobber (const_int 0))]
{
  rtx set_dest = operands[0];
  rtx set_srca = operands[3];
  rtx set_srcb = operands[4];
  int third = rtx_equal_p (set_dest, set_srca);
  rtx dest1, dest2;
  rtx srca1, srca2, srcb1, srcb2;

  dest1 = gen_df_reg (set_dest, 0);
  dest2 = gen_df_reg (set_dest, 1);
  srca1 = gen_df_reg (set_srca, 0);
  srca2 = gen_df_reg (set_srca, 1);
  srcb1 = gen_df_reg (set_srcb, 0);
  srcb2 = gen_df_reg (set_srcb, 1);

  /* Now emit using the real source and destination we found, swapping
     the order if we detect overlap.  */
  if ((third && reg_overlap_mentioned_p (dest1, srcb2))
      || (!third && reg_overlap_mentioned_p (dest1, srca2)))
    {
      emit_insn (gen_movdf_cc_reg_sp64 (dest2, operands[1], operands[2], srca2, srcb2));
      emit_insn (gen_movdf_cc_reg_sp64 (dest1, operands[1], operands[2], srca1, srcb1));
    }
  else
    {
      emit_insn (gen_movdf_cc_reg_sp64 (dest1, operands[1], operands[2], srca1, srcb1));
      emit_insn (gen_movdf_cc_reg_sp64 (dest2, operands[1], operands[2], srca2, srcb2));
    }
  DONE;
}
  [(set_attr "length" "2")])


;; Zero-extension instructions

;; These patterns originally accepted general_operands, however, slightly
;; better code is generated by only accepting register_operands, and then
;; letting combine generate the ldu[hb] insns.

(define_expand "zero_extendhisi2"
  [(set (match_operand:SI 0 "register_operand" "")
	(zero_extend:SI (match_operand:HI 1 "register_operand" "")))]
  ""
{
  rtx temp = gen_reg_rtx (SImode);
  rtx shift_16 = GEN_INT (16);
  int op1_subbyte = 0;

  if (GET_CODE (operand1) == SUBREG)
    {
      op1_subbyte = SUBREG_BYTE (operand1);
      op1_subbyte /= GET_MODE_SIZE (SImode);
      op1_subbyte *= GET_MODE_SIZE (SImode);
      operand1 = XEXP (operand1, 0);
    }

  emit_insn (gen_ashlsi3 (temp, gen_rtx_SUBREG (SImode, operand1, op1_subbyte),
			  shift_16));
  emit_insn (gen_lshrsi3 (operand0, temp, shift_16));
  DONE;
})

(define_insn "*zero_extendhisi2_insn"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(zero_extend:SI (match_operand:HI 1 "memory_operand" "m")))]
  ""
  "lduh\t%1, %0"
  [(set_attr "type" "load")
   (set_attr "us3load_type" "3cycle")])

(define_expand "zero_extendqihi2"
  [(set (match_operand:HI 0 "register_operand" "")
	(zero_extend:HI (match_operand:QI 1 "register_operand" "")))]
  ""
  "")

(define_insn "*zero_extendqihi2_insn"
  [(set (match_operand:HI 0 "register_operand" "=r,r")
	(zero_extend:HI (match_operand:QI 1 "input_operand" "r,m")))]
  "GET_CODE (operands[1]) != CONST_INT"
  "@
   and\t%1, 0xff, %0
   ldub\t%1, %0"
  [(set_attr "type" "*,load")
   (set_attr "us3load_type" "*,3cycle")])

(define_expand "zero_extendqisi2"
  [(set (match_operand:SI 0 "register_operand" "")
	(zero_extend:SI (match_operand:QI 1 "register_operand" "")))]
  ""
  "")

(define_insn "*zero_extendqisi2_insn"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(zero_extend:SI (match_operand:QI 1 "input_operand" "r,m")))]
  "GET_CODE (operands[1]) != CONST_INT"
  "@
   and\t%1, 0xff, %0
   ldub\t%1, %0"
  [(set_attr "type" "*,load")
   (set_attr "us3load_type" "*,3cycle")])

(define_expand "zero_extendqidi2"
  [(set (match_operand:DI 0 "register_operand" "")
	(zero_extend:DI (match_operand:QI 1 "register_operand" "")))]
  "TARGET_ARCH64"
  "")

(define_insn "*zero_extendqidi2_insn"
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(zero_extend:DI (match_operand:QI 1 "input_operand" "r,m")))]
  "TARGET_ARCH64 && GET_CODE (operands[1]) != CONST_INT"
  "@
   and\t%1, 0xff, %0
   ldub\t%1, %0"
  [(set_attr "type" "*,load")
   (set_attr "us3load_type" "*,3cycle")])

(define_expand "zero_extendhidi2"
  [(set (match_operand:DI 0 "register_operand" "")
	(zero_extend:DI (match_operand:HI 1 "register_operand" "")))]
  "TARGET_ARCH64"
{
  rtx temp = gen_reg_rtx (DImode);
  rtx shift_48 = GEN_INT (48);
  int op1_subbyte = 0;

  if (GET_CODE (operand1) == SUBREG)
    {
      op1_subbyte = SUBREG_BYTE (operand1);
      op1_subbyte /= GET_MODE_SIZE (DImode);
      op1_subbyte *= GET_MODE_SIZE (DImode);
      operand1 = XEXP (operand1, 0);
    }

  emit_insn (gen_ashldi3 (temp, gen_rtx_SUBREG (DImode, operand1, op1_subbyte),
			  shift_48));
  emit_insn (gen_lshrdi3 (operand0, temp, shift_48));
  DONE;
})

(define_insn "*zero_extendhidi2_insn"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(zero_extend:DI (match_operand:HI 1 "memory_operand" "m")))]
  "TARGET_ARCH64"
  "lduh\t%1, %0"
  [(set_attr "type" "load")
   (set_attr "us3load_type" "3cycle")])

;; ??? Write truncdisi pattern using sra?

(define_expand "zero_extendsidi2"
  [(set (match_operand:DI 0 "register_operand" "")
	(zero_extend:DI (match_operand:SI 1 "register_operand" "")))]
  ""
  "")

(define_insn "*zero_extendsidi2_insn_sp64"
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(zero_extend:DI (match_operand:SI 1 "input_operand" "r,m")))]
  "TARGET_ARCH64 && GET_CODE (operands[1]) != CONST_INT"
  "@
   srl\t%1, 0, %0
   lduw\t%1, %0"
  [(set_attr "type" "shift,load")])

(define_insn_and_split "*zero_extendsidi2_insn_sp32"
  [(set (match_operand:DI 0 "register_operand" "=r")
        (zero_extend:DI (match_operand:SI 1 "register_operand" "r")))]
  "! TARGET_ARCH64"
  "#"
  "&& reload_completed"
  [(set (match_dup 2) (match_dup 3))
   (set (match_dup 4) (match_dup 5))]
{
  rtx dest1, dest2;

  dest1 = gen_highpart (SImode, operands[0]);
  dest2 = gen_lowpart (SImode, operands[0]);

  /* Swap the order in case of overlap.  */
  if (REGNO (dest1) == REGNO (operands[1]))
    {
      operands[2] = dest2;
      operands[3] = operands[1];
      operands[4] = dest1;
      operands[5] = const0_rtx;
    }
  else
    {
      operands[2] = dest1;
      operands[3] = const0_rtx;
      operands[4] = dest2;
      operands[5] = operands[1];
    }
}
  [(set_attr "length" "2")])

;; Simplify comparisons of extended values.

(define_insn "*cmp_zero_extendqisi2"
  [(set (reg:CC 100)
	(compare:CC (zero_extend:SI (match_operand:QI 0 "register_operand" "r"))
		    (const_int 0)))]
  ""
  "andcc\t%0, 0xff, %%g0"
  [(set_attr "type" "compare")])

(define_insn "*cmp_zero_qi"
  [(set (reg:CC 100)
	(compare:CC (match_operand:QI 0 "register_operand" "r")
		    (const_int 0)))]
  ""
  "andcc\t%0, 0xff, %%g0"
  [(set_attr "type" "compare")])

(define_insn "*cmp_zero_extendqisi2_set"
  [(set (reg:CC 100)
	(compare:CC (zero_extend:SI (match_operand:QI 1 "register_operand" "r"))
		    (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=r")
	(zero_extend:SI (match_dup 1)))]
  ""
  "andcc\t%1, 0xff, %0"
  [(set_attr "type" "compare")])

(define_insn "*cmp_zero_extendqisi2_andcc_set"
  [(set (reg:CC 100)
	(compare:CC (and:SI (match_operand:SI 1 "register_operand" "r")
			    (const_int 255))
		    (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=r")
	(zero_extend:SI (subreg:QI (match_dup 1) 0)))]
  ""
  "andcc\t%1, 0xff, %0"
  [(set_attr "type" "compare")])

(define_insn "*cmp_zero_extendqidi2"
  [(set (reg:CCX 100)
	(compare:CCX (zero_extend:DI (match_operand:QI 0 "register_operand" "r"))
		     (const_int 0)))]
  "TARGET_ARCH64"
  "andcc\t%0, 0xff, %%g0"
  [(set_attr "type" "compare")])

(define_insn "*cmp_zero_qi_sp64"
  [(set (reg:CCX 100)
	(compare:CCX (match_operand:QI 0 "register_operand" "r")
		     (const_int 0)))]
  "TARGET_ARCH64"
  "andcc\t%0, 0xff, %%g0"
  [(set_attr "type" "compare")])

(define_insn "*cmp_zero_extendqidi2_set"
  [(set (reg:CCX 100)
	(compare:CCX (zero_extend:DI (match_operand:QI 1 "register_operand" "r"))
		     (const_int 0)))
   (set (match_operand:DI 0 "register_operand" "=r")
	(zero_extend:DI (match_dup 1)))]
  "TARGET_ARCH64"
  "andcc\t%1, 0xff, %0"
  [(set_attr "type" "compare")])

(define_insn "*cmp_zero_extendqidi2_andcc_set"
  [(set (reg:CCX 100)
	(compare:CCX (and:DI (match_operand:DI 1 "register_operand" "r")
			     (const_int 255))
		     (const_int 0)))
   (set (match_operand:DI 0 "register_operand" "=r")
	(zero_extend:DI (subreg:QI (match_dup 1) 0)))]
  "TARGET_ARCH64"
  "andcc\t%1, 0xff, %0"
  [(set_attr "type" "compare")])

;; Similarly, handle {SI,DI}->QI mode truncation followed by a compare.

(define_insn "*cmp_siqi_trunc"
  [(set (reg:CC 100)
	(compare:CC (subreg:QI (match_operand:SI 0 "register_operand" "r") 3)
		    (const_int 0)))]
  ""
  "andcc\t%0, 0xff, %%g0"
  [(set_attr "type" "compare")])

(define_insn "*cmp_siqi_trunc_set"
  [(set (reg:CC 100)
	(compare:CC (subreg:QI (match_operand:SI 1 "register_operand" "r") 3)
		    (const_int 0)))
   (set (match_operand:QI 0 "register_operand" "=r")
	(subreg:QI (match_dup 1) 3))]
  ""
  "andcc\t%1, 0xff, %0"
  [(set_attr "type" "compare")])

(define_insn "*cmp_diqi_trunc"
  [(set (reg:CC 100)
	(compare:CC (subreg:QI (match_operand:DI 0 "register_operand" "r") 7)
		    (const_int 0)))]
  "TARGET_ARCH64"
  "andcc\t%0, 0xff, %%g0"
  [(set_attr "type" "compare")])

(define_insn "*cmp_diqi_trunc_set"
  [(set (reg:CC 100)
	(compare:CC (subreg:QI (match_operand:DI 1 "register_operand" "r") 7)
		    (const_int 0)))
   (set (match_operand:QI 0 "register_operand" "=r")
	(subreg:QI (match_dup 1) 7))]
  "TARGET_ARCH64"
  "andcc\t%1, 0xff, %0"
  [(set_attr "type" "compare")])


;; Sign-extension instructions

;; These patterns originally accepted general_operands, however, slightly
;; better code is generated by only accepting register_operands, and then
;; letting combine generate the lds[hb] insns.

(define_expand "extendhisi2"
  [(set (match_operand:SI 0 "register_operand" "")
	(sign_extend:SI (match_operand:HI 1 "register_operand" "")))]
  ""
{
  rtx temp = gen_reg_rtx (SImode);
  rtx shift_16 = GEN_INT (16);
  int op1_subbyte = 0;

  if (GET_CODE (operand1) == SUBREG)
    {
      op1_subbyte = SUBREG_BYTE (operand1);
      op1_subbyte /= GET_MODE_SIZE (SImode);
      op1_subbyte *= GET_MODE_SIZE (SImode);
      operand1 = XEXP (operand1, 0);
    }

  emit_insn (gen_ashlsi3 (temp, gen_rtx_SUBREG (SImode, operand1, op1_subbyte),
			  shift_16));
  emit_insn (gen_ashrsi3 (operand0, temp, shift_16));
  DONE;
})

(define_insn "*sign_extendhisi2_insn"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(sign_extend:SI (match_operand:HI 1 "memory_operand" "m")))]
  ""
  "ldsh\t%1, %0"
  [(set_attr "type" "sload")
   (set_attr "us3load_type" "3cycle")])

(define_expand "extendqihi2"
  [(set (match_operand:HI 0 "register_operand" "")
	(sign_extend:HI (match_operand:QI 1 "register_operand" "")))]
  ""
{
  rtx temp = gen_reg_rtx (SImode);
  rtx shift_24 = GEN_INT (24);
  int op1_subbyte = 0;
  int op0_subbyte = 0;

  if (GET_CODE (operand1) == SUBREG)
    {
      op1_subbyte = SUBREG_BYTE (operand1);
      op1_subbyte /= GET_MODE_SIZE (SImode);
      op1_subbyte *= GET_MODE_SIZE (SImode);
      operand1 = XEXP (operand1, 0);
    }
  if (GET_CODE (operand0) == SUBREG)
    {
      op0_subbyte = SUBREG_BYTE (operand0);
      op0_subbyte /= GET_MODE_SIZE (SImode);
      op0_subbyte *= GET_MODE_SIZE (SImode);
      operand0 = XEXP (operand0, 0);
    }
  emit_insn (gen_ashlsi3 (temp, gen_rtx_SUBREG (SImode, operand1, op1_subbyte),
			  shift_24));
  if (GET_MODE (operand0) != SImode)
    operand0 = gen_rtx_SUBREG (SImode, operand0, op0_subbyte);
  emit_insn (gen_ashrsi3 (operand0, temp, shift_24));
  DONE;
})

(define_insn "*sign_extendqihi2_insn"
  [(set (match_operand:HI 0 "register_operand" "=r")
	(sign_extend:HI (match_operand:QI 1 "memory_operand" "m")))]
  ""
  "ldsb\t%1, %0"
  [(set_attr "type" "sload")
   (set_attr "us3load_type" "3cycle")])

(define_expand "extendqisi2"
  [(set (match_operand:SI 0 "register_operand" "")
	(sign_extend:SI (match_operand:QI 1 "register_operand" "")))]
  ""
{
  rtx temp = gen_reg_rtx (SImode);
  rtx shift_24 = GEN_INT (24);
  int op1_subbyte = 0;

  if (GET_CODE (operand1) == SUBREG)
    {
      op1_subbyte = SUBREG_BYTE (operand1);
      op1_subbyte /= GET_MODE_SIZE (SImode);
      op1_subbyte *= GET_MODE_SIZE (SImode);
      operand1 = XEXP (operand1, 0);
    }

  emit_insn (gen_ashlsi3 (temp, gen_rtx_SUBREG (SImode, operand1, op1_subbyte),
			  shift_24));
  emit_insn (gen_ashrsi3 (operand0, temp, shift_24));
  DONE;
})

(define_insn "*sign_extendqisi2_insn"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(sign_extend:SI (match_operand:QI 1 "memory_operand" "m")))]
  ""
  "ldsb\t%1, %0"
  [(set_attr "type" "sload")
   (set_attr "us3load_type" "3cycle")])

(define_expand "extendqidi2"
  [(set (match_operand:DI 0 "register_operand" "")
	(sign_extend:DI (match_operand:QI 1 "register_operand" "")))]
  "TARGET_ARCH64"
{
  rtx temp = gen_reg_rtx (DImode);
  rtx shift_56 = GEN_INT (56);
  int op1_subbyte = 0;

  if (GET_CODE (operand1) == SUBREG)
    {
      op1_subbyte = SUBREG_BYTE (operand1);
      op1_subbyte /= GET_MODE_SIZE (DImode);
      op1_subbyte *= GET_MODE_SIZE (DImode);
      operand1 = XEXP (operand1, 0);
    }

  emit_insn (gen_ashldi3 (temp, gen_rtx_SUBREG (DImode, operand1, op1_subbyte),
			  shift_56));
  emit_insn (gen_ashrdi3 (operand0, temp, shift_56));
  DONE;
})

(define_insn "*sign_extendqidi2_insn"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(sign_extend:DI (match_operand:QI 1 "memory_operand" "m")))]
  "TARGET_ARCH64"
  "ldsb\t%1, %0"
  [(set_attr "type" "sload")
   (set_attr "us3load_type" "3cycle")])

(define_expand "extendhidi2"
  [(set (match_operand:DI 0 "register_operand" "")
	(sign_extend:DI (match_operand:HI 1 "register_operand" "")))]
  "TARGET_ARCH64"
{
  rtx temp = gen_reg_rtx (DImode);
  rtx shift_48 = GEN_INT (48);
  int op1_subbyte = 0;

  if (GET_CODE (operand1) == SUBREG)
    {
      op1_subbyte = SUBREG_BYTE (operand1);
      op1_subbyte /= GET_MODE_SIZE (DImode);
      op1_subbyte *= GET_MODE_SIZE (DImode);
      operand1 = XEXP (operand1, 0);
    }

  emit_insn (gen_ashldi3 (temp, gen_rtx_SUBREG (DImode, operand1, op1_subbyte),
			  shift_48));
  emit_insn (gen_ashrdi3 (operand0, temp, shift_48));
  DONE;
})

(define_insn "*sign_extendhidi2_insn"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(sign_extend:DI (match_operand:HI 1 "memory_operand" "m")))]
  "TARGET_ARCH64"
  "ldsh\t%1, %0"
  [(set_attr "type" "sload")
   (set_attr "us3load_type" "3cycle")])

(define_expand "extendsidi2"
  [(set (match_operand:DI 0 "register_operand" "")
	(sign_extend:DI (match_operand:SI 1 "register_operand" "")))]
  "TARGET_ARCH64"
  "")

(define_insn "*sign_extendsidi2_insn"
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(sign_extend:DI (match_operand:SI 1 "input_operand" "r,m")))]
  "TARGET_ARCH64"
  "@
  sra\t%1, 0, %0
  ldsw\t%1, %0"
  [(set_attr "type" "shift,sload")
   (set_attr "us3load_type" "*,3cycle")])


;; Special pattern for optimizing bit-field compares.  This is needed
;; because combine uses this as a canonical form.

(define_insn "*cmp_zero_extract"
  [(set (reg:CC 100)
	(compare:CC
	 (zero_extract:SI (match_operand:SI 0 "register_operand" "r")
			  (match_operand:SI 1 "small_int_operand" "I")
			  (match_operand:SI 2 "small_int_operand" "I"))
	 (const_int 0)))]
  "INTVAL (operands[2]) > 19"
{
  int len = INTVAL (operands[1]);
  int pos = 32 - INTVAL (operands[2]) - len;
  HOST_WIDE_INT mask = ((1 << len) - 1) << pos;
  operands[1] = GEN_INT (mask);
  return "andcc\t%0, %1, %%g0";
}
  [(set_attr "type" "compare")])

(define_insn "*cmp_zero_extract_sp64"
  [(set (reg:CCX 100)
	(compare:CCX
	 (zero_extract:DI (match_operand:DI 0 "register_operand" "r")
			  (match_operand:SI 1 "small_int_operand" "I")
			  (match_operand:SI 2 "small_int_operand" "I"))
	 (const_int 0)))]
  "TARGET_ARCH64 && INTVAL (operands[2]) > 51"
{
  int len = INTVAL (operands[1]);
  int pos = 64 - INTVAL (operands[2]) - len;
  HOST_WIDE_INT mask = (((unsigned HOST_WIDE_INT) 1 << len) - 1) << pos;
  operands[1] = GEN_INT (mask);
  return "andcc\t%0, %1, %%g0";
}
  [(set_attr "type" "compare")])


;; Conversions between float, double and long double.

(define_insn "extendsfdf2"
  [(set (match_operand:DF 0 "register_operand" "=e")
	(float_extend:DF
	 (match_operand:SF 1 "register_operand" "f")))]
  "TARGET_FPU"
  "fstod\t%1, %0"
  [(set_attr "type" "fp")
   (set_attr "fptype" "double")])

(define_expand "extendsftf2"
  [(set (match_operand:TF 0 "nonimmediate_operand" "")
	(float_extend:TF
	 (match_operand:SF 1 "register_operand" "")))]
  "TARGET_FPU && (TARGET_HARD_QUAD || TARGET_ARCH64)"
  "emit_tfmode_cvt (FLOAT_EXTEND, operands); DONE;")

(define_insn "*extendsftf2_hq"
  [(set (match_operand:TF 0 "register_operand" "=e")
	(float_extend:TF
	 (match_operand:SF 1 "register_operand" "f")))]
  "TARGET_FPU && TARGET_HARD_QUAD"
  "fstoq\t%1, %0"
  [(set_attr "type" "fp")])

(define_expand "extenddftf2"
  [(set (match_operand:TF 0 "nonimmediate_operand" "")
	(float_extend:TF
	 (match_operand:DF 1 "register_operand" "")))]
  "TARGET_FPU && (TARGET_HARD_QUAD || TARGET_ARCH64)"
  "emit_tfmode_cvt (FLOAT_EXTEND, operands); DONE;")

(define_insn "*extenddftf2_hq"
  [(set (match_operand:TF 0 "register_operand" "=e")
	(float_extend:TF
	 (match_operand:DF 1 "register_operand" "e")))]
  "TARGET_FPU && TARGET_HARD_QUAD"
  "fdtoq\t%1, %0"
  [(set_attr "type" "fp")])

(define_insn "truncdfsf2"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(float_truncate:SF
	 (match_operand:DF 1 "register_operand" "e")))]
  "TARGET_FPU"
  "fdtos\t%1, %0"
  [(set_attr "type" "fp")
   (set_attr "fptype" "double")])

(define_expand "trunctfsf2"
  [(set (match_operand:SF 0 "register_operand" "")
	(float_truncate:SF
	 (match_operand:TF 1 "general_operand" "")))]
  "TARGET_FPU && (TARGET_HARD_QUAD || TARGET_ARCH64)"
  "emit_tfmode_cvt (FLOAT_TRUNCATE, operands); DONE;")

(define_insn "*trunctfsf2_hq"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(float_truncate:SF
	 (match_operand:TF 1 "register_operand" "e")))]
  "TARGET_FPU && TARGET_HARD_QUAD"
  "fqtos\t%1, %0"
  [(set_attr "type" "fp")])

(define_expand "trunctfdf2"
  [(set (match_operand:DF 0 "register_operand" "")
	(float_truncate:DF
	 (match_operand:TF 1 "general_operand" "")))]
  "TARGET_FPU && (TARGET_HARD_QUAD || TARGET_ARCH64)"
  "emit_tfmode_cvt (FLOAT_TRUNCATE, operands); DONE;")

(define_insn "*trunctfdf2_hq"
  [(set (match_operand:DF 0 "register_operand" "=e")
	(float_truncate:DF
	 (match_operand:TF 1 "register_operand" "e")))]
  "TARGET_FPU && TARGET_HARD_QUAD"
  "fqtod\t%1, %0"
  [(set_attr "type" "fp")])


;; Conversion between fixed point and floating point.

(define_insn "floatsisf2"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(float:SF (match_operand:SI 1 "register_operand" "f")))]
  "TARGET_FPU"
  "fitos\t%1, %0"
  [(set_attr "type" "fp")
   (set_attr "fptype" "double")])

(define_insn "floatsidf2"
  [(set (match_operand:DF 0 "register_operand" "=e")
	(float:DF (match_operand:SI 1 "register_operand" "f")))]
  "TARGET_FPU"
  "fitod\t%1, %0"
  [(set_attr "type" "fp")
   (set_attr "fptype" "double")])

(define_expand "floatsitf2"
  [(set (match_operand:TF 0 "nonimmediate_operand" "")
	(float:TF (match_operand:SI 1 "register_operand" "")))]
  "TARGET_FPU && (TARGET_HARD_QUAD || TARGET_ARCH64)"
  "emit_tfmode_cvt (FLOAT, operands); DONE;")

(define_insn "*floatsitf2_hq"
  [(set (match_operand:TF 0 "register_operand" "=e")
	(float:TF (match_operand:SI 1 "register_operand" "f")))]
  "TARGET_FPU && TARGET_HARD_QUAD"
  "fitoq\t%1, %0"
  [(set_attr "type" "fp")])

(define_expand "floatunssitf2"
  [(set (match_operand:TF 0 "nonimmediate_operand" "")
	(unsigned_float:TF (match_operand:SI 1 "register_operand" "")))]
  "TARGET_FPU && TARGET_ARCH64 && ! TARGET_HARD_QUAD"
  "emit_tfmode_cvt (UNSIGNED_FLOAT, operands); DONE;")

;; Now the same for 64 bit sources.

(define_insn "floatdisf2"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(float:SF (match_operand:DI 1 "register_operand" "e")))]
  "TARGET_V9 && TARGET_FPU"
  "fxtos\t%1, %0"
  [(set_attr "type" "fp")
   (set_attr "fptype" "double")])

(define_expand "floatunsdisf2"
  [(use (match_operand:SF 0 "register_operand" ""))
   (use (match_operand:DI 1 "general_operand" ""))]
  "TARGET_ARCH64 && TARGET_FPU"
  "sparc_emit_floatunsdi (operands, SFmode); DONE;")

(define_insn "floatdidf2"
  [(set (match_operand:DF 0 "register_operand" "=e")
	(float:DF (match_operand:DI 1 "register_operand" "e")))]
  "TARGET_V9 && TARGET_FPU"
  "fxtod\t%1, %0"
  [(set_attr "type" "fp")
   (set_attr "fptype" "double")])

(define_expand "floatunsdidf2"
  [(use (match_operand:DF 0 "register_operand" ""))
   (use (match_operand:DI 1 "general_operand" ""))]
  "TARGET_ARCH64 && TARGET_FPU"
  "sparc_emit_floatunsdi (operands, DFmode); DONE;")

(define_expand "floatditf2"
  [(set (match_operand:TF 0 "nonimmediate_operand" "")
	(float:TF (match_operand:DI 1 "register_operand" "")))]
  "TARGET_FPU && TARGET_V9 && (TARGET_HARD_QUAD || TARGET_ARCH64)"
  "emit_tfmode_cvt (FLOAT, operands); DONE;")

(define_insn "*floatditf2_hq"
  [(set (match_operand:TF 0 "register_operand" "=e")
	(float:TF (match_operand:DI 1 "register_operand" "e")))]
  "TARGET_V9 && TARGET_FPU && TARGET_HARD_QUAD"
  "fxtoq\t%1, %0"
  [(set_attr "type" "fp")])

(define_expand "floatunsditf2"
  [(set (match_operand:TF 0 "nonimmediate_operand" "")
	(unsigned_float:TF (match_operand:DI 1 "register_operand" "")))]
  "TARGET_FPU && TARGET_ARCH64 && ! TARGET_HARD_QUAD"
  "emit_tfmode_cvt (UNSIGNED_FLOAT, operands); DONE;")

;; Convert a float to an actual integer.
;; Truncation is performed as part of the conversion.

(define_insn "fix_truncsfsi2"
  [(set (match_operand:SI 0 "register_operand" "=f")
	(fix:SI (fix:SF (match_operand:SF 1 "register_operand" "f"))))]
  "TARGET_FPU"
  "fstoi\t%1, %0"
  [(set_attr "type" "fp")
   (set_attr "fptype" "double")])

(define_insn "fix_truncdfsi2"
  [(set (match_operand:SI 0 "register_operand" "=f")
	(fix:SI (fix:DF (match_operand:DF 1 "register_operand" "e"))))]
  "TARGET_FPU"
  "fdtoi\t%1, %0"
  [(set_attr "type" "fp")
   (set_attr "fptype" "double")])

(define_expand "fix_trunctfsi2"
  [(set (match_operand:SI 0 "register_operand" "")
	(fix:SI (match_operand:TF 1 "general_operand" "")))]
  "TARGET_FPU && (TARGET_HARD_QUAD || TARGET_ARCH64)"
  "emit_tfmode_cvt (FIX, operands); DONE;")

(define_insn "*fix_trunctfsi2_hq"
  [(set (match_operand:SI 0 "register_operand" "=f")
	(fix:SI (match_operand:TF 1 "register_operand" "e")))]
  "TARGET_FPU && TARGET_HARD_QUAD"
  "fqtoi\t%1, %0"
  [(set_attr "type" "fp")])

(define_expand "fixuns_trunctfsi2"
  [(set (match_operand:SI 0 "register_operand" "")
	(unsigned_fix:SI (match_operand:TF 1 "general_operand" "")))]
  "TARGET_FPU && TARGET_ARCH64 && ! TARGET_HARD_QUAD"
  "emit_tfmode_cvt (UNSIGNED_FIX, operands); DONE;")

;; Now the same, for V9 targets

(define_insn "fix_truncsfdi2"
  [(set (match_operand:DI 0 "register_operand" "=e")
	(fix:DI (fix:SF (match_operand:SF 1 "register_operand" "f"))))]
  "TARGET_V9 && TARGET_FPU"
  "fstox\t%1, %0"
  [(set_attr "type" "fp")
   (set_attr "fptype" "double")])

(define_expand "fixuns_truncsfdi2"
  [(use (match_operand:DI 0 "register_operand" ""))
   (use (match_operand:SF 1 "general_operand" ""))]
  "TARGET_ARCH64 && TARGET_FPU"
  "sparc_emit_fixunsdi (operands, SFmode); DONE;")

(define_insn "fix_truncdfdi2"
  [(set (match_operand:DI 0 "register_operand" "=e")
	(fix:DI (fix:DF (match_operand:DF 1 "register_operand" "e"))))]
  "TARGET_V9 && TARGET_FPU"
  "fdtox\t%1, %0"
  [(set_attr "type" "fp")
   (set_attr "fptype" "double")])

(define_expand "fixuns_truncdfdi2"
  [(use (match_operand:DI 0 "register_operand" ""))
   (use (match_operand:DF 1 "general_operand" ""))]
  "TARGET_ARCH64 && TARGET_FPU"
  "sparc_emit_fixunsdi (operands, DFmode); DONE;")

(define_expand "fix_trunctfdi2"
  [(set (match_operand:DI 0 "register_operand" "")
	(fix:DI (match_operand:TF 1 "general_operand" "")))]
  "TARGET_V9 && TARGET_FPU && (TARGET_HARD_QUAD || TARGET_ARCH64)"
  "emit_tfmode_cvt (FIX, operands); DONE;")

(define_insn "*fix_trunctfdi2_hq"
  [(set (match_operand:DI 0 "register_operand" "=e")
	(fix:DI (match_operand:TF 1 "register_operand" "e")))]
  "TARGET_V9 && TARGET_FPU && TARGET_HARD_QUAD"
  "fqtox\t%1, %0"
  [(set_attr "type" "fp")])

(define_expand "fixuns_trunctfdi2"
  [(set (match_operand:DI 0 "register_operand" "")
	(unsigned_fix:DI (match_operand:TF 1 "general_operand" "")))]
  "TARGET_FPU && TARGET_ARCH64 && ! TARGET_HARD_QUAD"
  "emit_tfmode_cvt (UNSIGNED_FIX, operands); DONE;")


;; Integer addition/subtraction instructions.

(define_expand "adddi3"
  [(set (match_operand:DI 0 "register_operand" "")
	(plus:DI (match_operand:DI 1 "register_operand" "")
		 (match_operand:DI 2 "arith_double_add_operand" "")))]
  ""
{
  if (! TARGET_ARCH64)
    {
      emit_insn (gen_rtx_PARALLEL (VOIDmode, gen_rtvec (2,
			  gen_rtx_SET (VOIDmode, operands[0],
				   gen_rtx_PLUS (DImode, operands[1],
						 operands[2])),
			  gen_rtx_CLOBBER (VOIDmode,
				   gen_rtx_REG (CCmode, SPARC_ICC_REG)))));
      DONE;
    }
})

(define_insn_and_split "adddi3_insn_sp32"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(plus:DI (match_operand:DI 1 "arith_double_operand" "%r")
		 (match_operand:DI 2 "arith_double_operand" "rHI")))
   (clobber (reg:CC 100))]
  "! TARGET_ARCH64"
  "#"
  "&& reload_completed"
  [(parallel [(set (reg:CC_NOOV 100)
		   (compare:CC_NOOV (plus:SI (match_dup 4)
					     (match_dup 5))
				    (const_int 0)))
	      (set (match_dup 3)
		   (plus:SI (match_dup 4) (match_dup 5)))])
   (set (match_dup 6)
	(plus:SI (plus:SI (match_dup 7)
			  (match_dup 8))
		 (ltu:SI (reg:CC_NOOV 100) (const_int 0))))]
{
  operands[3] = gen_lowpart (SImode, operands[0]);
  operands[4] = gen_lowpart (SImode, operands[1]);
  operands[5] = gen_lowpart (SImode, operands[2]);
  operands[6] = gen_highpart (SImode, operands[0]);
  operands[7] = gen_highpart_mode (SImode, DImode, operands[1]);
#if HOST_BITS_PER_WIDE_INT == 32
  if (GET_CODE (operands[2]) == CONST_INT)
    {
      if (INTVAL (operands[2]) < 0)
	operands[8] = constm1_rtx;
      else
	operands[8] = const0_rtx;
    }
  else
#endif
    operands[8] = gen_highpart_mode (SImode, DImode, operands[2]);
}
  [(set_attr "length" "2")])

;; LTU here means "carry set"
(define_insn "addx"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(plus:SI (plus:SI (match_operand:SI 1 "arith_operand" "%r")
			  (match_operand:SI 2 "arith_operand" "rI"))
		 (ltu:SI (reg:CC_NOOV 100) (const_int 0))))]
  ""
  "addx\t%1, %2, %0"
  [(set_attr "type" "ialuX")])

(define_insn_and_split "*addx_extend_sp32"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(zero_extend:DI (plus:SI (plus:SI
                                  (match_operand:SI 1 "register_or_zero_operand" "%rJ")
                                  (match_operand:SI 2 "arith_operand" "rI"))
                                 (ltu:SI (reg:CC_NOOV 100) (const_int 0)))))]
  "! TARGET_ARCH64"
  "#"
  "&& reload_completed"
  [(set (match_dup 3) (plus:SI (plus:SI (match_dup 1) (match_dup 2))
                               (ltu:SI (reg:CC_NOOV 100) (const_int 0))))
   (set (match_dup 4) (const_int 0))]
  "operands[3] = gen_lowpart (SImode, operands[0]);
   operands[4] = gen_highpart_mode (SImode, DImode, operands[1]);"
  [(set_attr "length" "2")])

(define_insn "*addx_extend_sp64"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(zero_extend:DI (plus:SI (plus:SI (match_operand:SI 1 "register_or_zero_operand" "%rJ")
                                          (match_operand:SI 2 "arith_operand" "rI"))
                                 (ltu:SI (reg:CC_NOOV 100) (const_int 0)))))]
  "TARGET_ARCH64"
  "addx\t%r1, %2, %0"
  [(set_attr "type" "ialuX")])

(define_insn_and_split ""
  [(set (match_operand:DI 0 "register_operand" "=r")
        (plus:DI (zero_extend:DI (match_operand:SI 1 "register_operand" "r"))
                 (match_operand:DI 2 "register_operand" "r")))
   (clobber (reg:CC 100))]
  "! TARGET_ARCH64"
  "#"
  "&& reload_completed"
  [(parallel [(set (reg:CC_NOOV 100)
                   (compare:CC_NOOV (plus:SI (match_dup 3) (match_dup 1))
                                    (const_int 0)))
              (set (match_dup 5) (plus:SI (match_dup 3) (match_dup 1)))])
   (set (match_dup 6)
        (plus:SI (plus:SI (match_dup 4) (const_int 0))
                 (ltu:SI (reg:CC_NOOV 100) (const_int 0))))]
  "operands[3] = gen_lowpart (SImode, operands[2]);
   operands[4] = gen_highpart (SImode, operands[2]);
   operands[5] = gen_lowpart (SImode, operands[0]);
   operands[6] = gen_highpart (SImode, operands[0]);"
  [(set_attr "length" "2")])

(define_insn "*adddi3_sp64"
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(plus:DI (match_operand:DI 1 "register_operand" "%r,r")
		 (match_operand:DI 2 "arith_add_operand" "rI,O")))]
  "TARGET_ARCH64"
  "@
   add\t%1, %2, %0
   sub\t%1, -%2, %0")

(define_insn "addsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r,d")
	(plus:SI (match_operand:SI 1 "register_operand" "%r,r,d")
		 (match_operand:SI 2 "arith_add_operand" "rI,O,d")))]
  ""
  "@
   add\t%1, %2, %0
   sub\t%1, -%2, %0
   fpadd32s\t%1, %2, %0"
  [(set_attr "type" "*,*,fga")
   (set_attr "fptype" "*,*,single")])

(define_insn "*cmp_cc_plus"
  [(set (reg:CC_NOOV 100)
	(compare:CC_NOOV (plus:SI (match_operand:SI 0 "arith_operand" "%r")
				  (match_operand:SI 1 "arith_operand" "rI"))
			 (const_int 0)))]
  ""
  "addcc\t%0, %1, %%g0"
  [(set_attr "type" "compare")])

(define_insn "*cmp_ccx_plus"
  [(set (reg:CCX_NOOV 100)
	(compare:CCX_NOOV (plus:DI (match_operand:DI 0 "arith_operand" "%r")
				   (match_operand:DI 1 "arith_operand" "rI"))
			  (const_int 0)))]
  "TARGET_ARCH64"
  "addcc\t%0, %1, %%g0"
  [(set_attr "type" "compare")])

(define_insn "*cmp_cc_plus_set"
  [(set (reg:CC_NOOV 100)
	(compare:CC_NOOV (plus:SI (match_operand:SI 1 "arith_operand" "%r")
				  (match_operand:SI 2 "arith_operand" "rI"))
			 (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=r")
	(plus:SI (match_dup 1) (match_dup 2)))]
  ""
  "addcc\t%1, %2, %0"
  [(set_attr "type" "compare")])

(define_insn "*cmp_ccx_plus_set"
  [(set (reg:CCX_NOOV 100)
	(compare:CCX_NOOV (plus:DI (match_operand:DI 1 "arith_operand" "%r")
				   (match_operand:DI 2 "arith_operand" "rI"))
			  (const_int 0)))
   (set (match_operand:DI 0 "register_operand" "=r")
	(plus:DI (match_dup 1) (match_dup 2)))]
  "TARGET_ARCH64"
  "addcc\t%1, %2, %0"
  [(set_attr "type" "compare")])

(define_expand "subdi3"
  [(set (match_operand:DI 0 "register_operand" "")
	(minus:DI (match_operand:DI 1 "register_operand" "")
		  (match_operand:DI 2 "arith_double_add_operand" "")))]
  ""
{
  if (! TARGET_ARCH64)
    {
      emit_insn (gen_rtx_PARALLEL (VOIDmode, gen_rtvec (2,
			  gen_rtx_SET (VOIDmode, operands[0],
				   gen_rtx_MINUS (DImode, operands[1],
						  operands[2])),
			  gen_rtx_CLOBBER (VOIDmode,
				   gen_rtx_REG (CCmode, SPARC_ICC_REG)))));
      DONE;
    }
})

(define_insn_and_split "subdi3_insn_sp32"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(minus:DI (match_operand:DI 1 "register_operand" "r")
		  (match_operand:DI 2 "arith_double_operand" "rHI")))
   (clobber (reg:CC 100))]
  "! TARGET_ARCH64"
  "#"
  "&& reload_completed"
  [(parallel [(set (reg:CC_NOOV 100)
		   (compare:CC_NOOV (minus:SI (match_dup 4)
					      (match_dup 5))
				    (const_int 0)))
	      (set (match_dup 3)
		   (minus:SI (match_dup 4) (match_dup 5)))])
   (set (match_dup 6)
	(minus:SI (minus:SI (match_dup 7)
			    (match_dup 8))
		  (ltu:SI (reg:CC_NOOV 100) (const_int 0))))]
{
  operands[3] = gen_lowpart (SImode, operands[0]);
  operands[4] = gen_lowpart (SImode, operands[1]);
  operands[5] = gen_lowpart (SImode, operands[2]);
  operands[6] = gen_highpart (SImode, operands[0]);
  operands[7] = gen_highpart (SImode, operands[1]);
#if HOST_BITS_PER_WIDE_INT == 32
  if (GET_CODE (operands[2]) == CONST_INT)
    {
      if (INTVAL (operands[2]) < 0)
	operands[8] = constm1_rtx;
      else
	operands[8] = const0_rtx;
    }
  else
#endif
    operands[8] = gen_highpart_mode (SImode, DImode, operands[2]);
}
  [(set_attr "length" "2")])

;; LTU here means "carry set"
(define_insn "subx"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(minus:SI (minus:SI (match_operand:SI 1 "register_or_zero_operand" "rJ")
			    (match_operand:SI 2 "arith_operand" "rI"))
		  (ltu:SI (reg:CC_NOOV 100) (const_int 0))))]
  ""
  "subx\t%r1, %2, %0"
  [(set_attr "type" "ialuX")])

(define_insn "*subx_extend_sp64"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(zero_extend:DI (minus:SI (minus:SI (match_operand:SI 1 "register_or_zero_operand" "rJ")
                                            (match_operand:SI 2 "arith_operand" "rI"))
                                  (ltu:SI (reg:CC_NOOV 100) (const_int 0)))))]
  "TARGET_ARCH64"
  "subx\t%r1, %2, %0"
  [(set_attr "type" "ialuX")])

(define_insn_and_split "*subx_extend"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(zero_extend:DI (minus:SI (minus:SI (match_operand:SI 1 "register_or_zero_operand" "rJ")
                                            (match_operand:SI 2 "arith_operand" "rI"))
                                  (ltu:SI (reg:CC_NOOV 100) (const_int 0)))))]
  "! TARGET_ARCH64"
  "#"
  "&& reload_completed"
  [(set (match_dup 3) (minus:SI (minus:SI (match_dup 1) (match_dup 2))
                                (ltu:SI (reg:CC_NOOV 100) (const_int 0))))
   (set (match_dup 4) (const_int 0))]
  "operands[3] = gen_lowpart (SImode, operands[0]);
   operands[4] = gen_highpart (SImode, operands[0]);"
  [(set_attr "length" "2")])

(define_insn_and_split ""
  [(set (match_operand:DI 0 "register_operand" "=r")
      (minus:DI (match_operand:DI 1 "register_operand" "r")
                (zero_extend:DI (match_operand:SI 2 "register_operand" "r"))))
   (clobber (reg:CC 100))]
  "! TARGET_ARCH64"
  "#"
  "&& reload_completed"
  [(parallel [(set (reg:CC_NOOV 100)
                   (compare:CC_NOOV (minus:SI (match_dup 3) (match_dup 2))
                                    (const_int 0)))
              (set (match_dup 5) (minus:SI (match_dup 3) (match_dup 2)))])
   (set (match_dup 6)
        (minus:SI (minus:SI (match_dup 4) (const_int 0))
                  (ltu:SI (reg:CC_NOOV 100) (const_int 0))))]
  "operands[3] = gen_lowpart (SImode, operands[1]);
   operands[4] = gen_highpart (SImode, operands[1]);
   operands[5] = gen_lowpart (SImode, operands[0]);
   operands[6] = gen_highpart (SImode, operands[0]);"
  [(set_attr "length" "2")])

(define_insn "*subdi3_sp64"
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(minus:DI (match_operand:DI 1 "register_operand" "r,r")
		  (match_operand:DI 2 "arith_add_operand" "rI,O")))]
  "TARGET_ARCH64"
  "@
   sub\t%1, %2, %0
   add\t%1, -%2, %0")

(define_insn "subsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r,d")
	(minus:SI (match_operand:SI 1 "register_operand" "r,r,d")
		  (match_operand:SI 2 "arith_add_operand" "rI,O,d")))]
  ""
  "@
   sub\t%1, %2, %0
   add\t%1, -%2, %0
   fpsub32s\t%1, %2, %0"
  [(set_attr "type" "*,*,fga")
   (set_attr "fptype" "*,*,single")])

(define_insn "*cmp_minus_cc"
  [(set (reg:CC_NOOV 100)
	(compare:CC_NOOV (minus:SI (match_operand:SI 0 "register_or_zero_operand" "rJ")
				   (match_operand:SI 1 "arith_operand" "rI"))
			 (const_int 0)))]
  ""
  "subcc\t%r0, %1, %%g0"
  [(set_attr "type" "compare")])

(define_insn "*cmp_minus_ccx"
  [(set (reg:CCX_NOOV 100)
	(compare:CCX_NOOV (minus:DI (match_operand:DI 0 "register_operand" "r")
				    (match_operand:DI 1 "arith_operand" "rI"))
			  (const_int 0)))]
  "TARGET_ARCH64"
  "subcc\t%0, %1, %%g0"
  [(set_attr "type" "compare")])

(define_insn "cmp_minus_cc_set"
  [(set (reg:CC_NOOV 100)
	(compare:CC_NOOV (minus:SI (match_operand:SI 1 "register_or_zero_operand" "rJ")
				   (match_operand:SI 2 "arith_operand" "rI"))
			 (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=r")
	(minus:SI (match_dup 1) (match_dup 2)))]
  ""
  "subcc\t%r1, %2, %0"
  [(set_attr "type" "compare")])

(define_insn "*cmp_minus_ccx_set"
  [(set (reg:CCX_NOOV 100)
	(compare:CCX_NOOV (minus:DI (match_operand:DI 1 "register_operand" "r")
				    (match_operand:DI 2 "arith_operand" "rI"))
			  (const_int 0)))
   (set (match_operand:DI 0 "register_operand" "=r")
	(minus:DI (match_dup 1) (match_dup 2)))]
  "TARGET_ARCH64"
  "subcc\t%1, %2, %0"
  [(set_attr "type" "compare")])


;; Integer multiply/divide instructions.

;; The 32 bit multiply/divide instructions are deprecated on v9, but at
;; least in UltraSPARC I, II and IIi it is a win tick-wise.

(define_insn "mulsi3"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(mult:SI (match_operand:SI 1 "arith_operand" "%r")
		 (match_operand:SI 2 "arith_operand" "rI")))]
  "TARGET_HARD_MUL"
  "smul\t%1, %2, %0"
  [(set_attr "type" "imul")])

(define_expand "muldi3"
  [(set (match_operand:DI 0 "register_operand" "")
	(mult:DI (match_operand:DI 1 "arith_operand" "")
		 (match_operand:DI 2 "arith_operand" "")))]
  "TARGET_ARCH64 || TARGET_V8PLUS"
{
  if (TARGET_V8PLUS)
    {
      emit_insn (gen_muldi3_v8plus (operands[0], operands[1], operands[2]));
      DONE;
    }
})

(define_insn "*muldi3_sp64"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(mult:DI (match_operand:DI 1 "arith_operand" "%r")
		 (match_operand:DI 2 "arith_operand" "rI")))]
  "TARGET_ARCH64"
  "mulx\t%1, %2, %0"
  [(set_attr "type" "imul")])

;; V8plus wide multiply.
;; XXX
(define_insn "muldi3_v8plus"
  [(set (match_operand:DI 0 "register_operand" "=r,h")
	(mult:DI (match_operand:DI 1 "arith_operand" "%r,0")
		 (match_operand:DI 2 "arith_operand" "rI,rI")))
   (clobber (match_scratch:SI 3 "=&h,X"))
   (clobber (match_scratch:SI 4 "=&h,X"))]
  "TARGET_V8PLUS"
{
  if (sparc_check_64 (operands[1], insn) <= 0)
    output_asm_insn ("srl\t%L1, 0, %L1", operands);
  if (which_alternative == 1)
    output_asm_insn ("sllx\t%H1, 32, %H1", operands);
  if (GET_CODE (operands[2]) == CONST_INT)
    {
      if (which_alternative == 1)
	return "or\t%L1, %H1, %H1\n\tmulx\t%H1, %2, %L0\;srlx\t%L0, 32, %H0";
      else
	return "sllx\t%H1, 32, %3\n\tor\t%L1, %3, %3\n\tmulx\t%3, %2, %3\n\tsrlx\t%3, 32, %H0\n\tmov\t%3, %L0";
    }
  else if (rtx_equal_p (operands[1], operands[2]))
    {
      if (which_alternative == 1)
	return "or\t%L1, %H1, %H1\n\tmulx\t%H1, %H1, %L0\;srlx\t%L0, 32, %H0";
      else
	return "sllx\t%H1, 32, %3\n\tor\t%L1, %3, %3\n\tmulx\t%3, %3, %3\n\tsrlx\t%3, 32, %H0\n\tmov\t%3, %L0";
    }
  if (sparc_check_64 (operands[2], insn) <= 0)
    output_asm_insn ("srl\t%L2, 0, %L2", operands);
  if (which_alternative == 1)
    return "or\t%L1, %H1, %H1\n\tsllx\t%H2, 32, %L1\n\tor\t%L2, %L1, %L1\n\tmulx\t%H1, %L1, %L0\;srlx\t%L0, 32, %H0";
  else
    return "sllx\t%H1, 32, %3\n\tsllx\t%H2, 32, %4\n\tor\t%L1, %3, %3\n\tor\t%L2, %4, %4\n\tmulx\t%3, %4, %3\n\tsrlx\t%3, 32, %H0\n\tmov\t%3, %L0";
}
  [(set_attr "type" "multi")
   (set_attr "length" "9,8")])

(define_insn "*cmp_mul_set"
  [(set (reg:CC 100)
	(compare:CC (mult:SI (match_operand:SI 1 "arith_operand" "%r")
		    (match_operand:SI 2 "arith_operand" "rI"))
		    (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=r")
	(mult:SI (match_dup 1) (match_dup 2)))]
  "TARGET_V8 || TARGET_SPARCLITE || TARGET_DEPRECATED_V8_INSNS"
  "smulcc\t%1, %2, %0"
  [(set_attr "type" "imul")])

(define_expand "mulsidi3"
  [(set (match_operand:DI 0 "register_operand" "")
	(mult:DI (sign_extend:DI (match_operand:SI 1 "register_operand" ""))
		 (sign_extend:DI (match_operand:SI 2 "arith_operand" ""))))]
  "TARGET_HARD_MUL"
{
  if (CONSTANT_P (operands[2]))
    {
      if (TARGET_V8PLUS)
	emit_insn (gen_const_mulsidi3_v8plus (operands[0], operands[1],
					      operands[2]));
      else if (TARGET_ARCH32)
	emit_insn (gen_const_mulsidi3_sp32 (operands[0], operands[1],
					    operands[2]));
      else 
	emit_insn (gen_const_mulsidi3_sp64 (operands[0], operands[1],
					    operands[2]));
      DONE;
    }
  if (TARGET_V8PLUS)
    {
      emit_insn (gen_mulsidi3_v8plus (operands[0], operands[1], operands[2]));
      DONE;
    }
})

;; V9 puts the 64 bit product in a 64 bit register.  Only out or global
;; registers can hold 64 bit values in the V8plus environment.
;; XXX
(define_insn "mulsidi3_v8plus"
  [(set (match_operand:DI 0 "register_operand" "=h,r")
	(mult:DI (sign_extend:DI (match_operand:SI 1 "register_operand" "r,r"))
		 (sign_extend:DI (match_operand:SI 2 "register_operand" "r,r"))))
   (clobber (match_scratch:SI 3 "=X,&h"))]
  "TARGET_V8PLUS"
  "@
   smul\t%1, %2, %L0\n\tsrlx\t%L0, 32, %H0
   smul\t%1, %2, %3\n\tsrlx\t%3, 32, %H0\n\tmov\t%3, %L0"
  [(set_attr "type" "multi")
   (set_attr "length" "2,3")])

;; XXX
(define_insn "const_mulsidi3_v8plus"
  [(set (match_operand:DI 0 "register_operand" "=h,r")
	(mult:DI (sign_extend:DI (match_operand:SI 1 "register_operand" "r,r"))
		 (match_operand:DI 2 "small_int_operand" "I,I")))
   (clobber (match_scratch:SI 3 "=X,&h"))]
  "TARGET_V8PLUS"
  "@
   smul\t%1, %2, %L0\n\tsrlx\t%L0, 32, %H0
   smul\t%1, %2, %3\n\tsrlx\t%3, 32, %H0\n\tmov\t%3, %L0"
  [(set_attr "type" "multi")
   (set_attr "length" "2,3")])

;; XXX
(define_insn "*mulsidi3_sp32"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(mult:DI (sign_extend:DI (match_operand:SI 1 "register_operand" "r"))
		 (sign_extend:DI (match_operand:SI 2 "register_operand" "r"))))]
  "TARGET_HARD_MUL32"
{
  return TARGET_SPARCLET
         ? "smuld\t%1, %2, %L0"
         : "smul\t%1, %2, %L0\n\trd\t%%y, %H0";
}
  [(set (attr "type")
	(if_then_else (eq_attr "isa" "sparclet")
		      (const_string "imul") (const_string "multi")))
   (set (attr "length")
	(if_then_else (eq_attr "isa" "sparclet")
		      (const_int 1) (const_int 2)))])

(define_insn "*mulsidi3_sp64"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(mult:DI (sign_extend:DI (match_operand:SI 1 "register_operand" "r"))
		 (sign_extend:DI (match_operand:SI 2 "register_operand" "r"))))]
  "TARGET_DEPRECATED_V8_INSNS && TARGET_ARCH64"
  "smul\t%1, %2, %0"
  [(set_attr "type" "imul")])

;; Extra pattern, because sign_extend of a constant isn't valid.

;; XXX
(define_insn "const_mulsidi3_sp32"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(mult:DI (sign_extend:DI (match_operand:SI 1 "register_operand" "r"))
		 (match_operand:DI 2 "small_int_operand" "I")))]
  "TARGET_HARD_MUL32"
{
  return TARGET_SPARCLET
         ? "smuld\t%1, %2, %L0"
         : "smul\t%1, %2, %L0\n\trd\t%%y, %H0";
}
  [(set (attr "type")
	(if_then_else (eq_attr "isa" "sparclet")
		      (const_string "imul") (const_string "multi")))
   (set (attr "length")
	(if_then_else (eq_attr "isa" "sparclet")
		      (const_int 1) (const_int 2)))])

(define_insn "const_mulsidi3_sp64"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(mult:DI (sign_extend:DI (match_operand:SI 1 "register_operand" "r"))
		 (match_operand:DI 2 "small_int_operand" "I")))]
  "TARGET_DEPRECATED_V8_INSNS && TARGET_ARCH64"
  "smul\t%1, %2, %0"
  [(set_attr "type" "imul")])

(define_expand "smulsi3_highpart"
  [(set (match_operand:SI 0 "register_operand" "")
	(truncate:SI
	 (lshiftrt:DI (mult:DI (sign_extend:DI (match_operand:SI 1 "register_operand" ""))
			       (sign_extend:DI (match_operand:SI 2 "arith_operand" "")))
		      (const_int 32))))]
  "TARGET_HARD_MUL && TARGET_ARCH32"
{
  if (CONSTANT_P (operands[2]))
    {
      if (TARGET_V8PLUS)
	{
	  emit_insn (gen_const_smulsi3_highpart_v8plus (operands[0],
							operands[1],
							operands[2],
							GEN_INT (32)));
	  DONE;
	}
      emit_insn (gen_const_smulsi3_highpart (operands[0], operands[1], operands[2]));
      DONE;
    }
  if (TARGET_V8PLUS)
    {
      emit_insn (gen_smulsi3_highpart_v8plus (operands[0], operands[1],
					      operands[2], GEN_INT (32)));
      DONE;
    }
})

;; XXX
(define_insn "smulsi3_highpart_v8plus"
  [(set (match_operand:SI 0 "register_operand" "=h,r")
	(truncate:SI
	 (lshiftrt:DI (mult:DI (sign_extend:DI (match_operand:SI 1 "register_operand" "r,r"))
			       (sign_extend:DI (match_operand:SI 2 "register_operand" "r,r")))
		      (match_operand:SI 3 "small_int_operand" "I,I"))))
   (clobber (match_scratch:SI 4 "=X,&h"))]
  "TARGET_V8PLUS"
  "@
   smul\t%1, %2, %0\;srlx\t%0, %3, %0
   smul\t%1, %2, %4\;srlx\t%4, %3, %0"
  [(set_attr "type" "multi")
   (set_attr "length" "2")])

;; The combiner changes TRUNCATE in the previous pattern to SUBREG.
;; XXX
(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=h,r")
	(subreg:SI
	 (lshiftrt:DI
	  (mult:DI (sign_extend:DI (match_operand:SI 1 "register_operand" "r,r"))
		   (sign_extend:DI (match_operand:SI 2 "register_operand" "r,r")))
	  (match_operand:SI 3 "small_int_operand" "I,I"))
	 4))
   (clobber (match_scratch:SI 4 "=X,&h"))]
  "TARGET_V8PLUS"
  "@
   smul\t%1, %2, %0\n\tsrlx\t%0, %3, %0
   smul\t%1, %2, %4\n\tsrlx\t%4, %3, %0"
  [(set_attr "type" "multi")
   (set_attr "length" "2")])

;; XXX
(define_insn "const_smulsi3_highpart_v8plus"
  [(set (match_operand:SI 0 "register_operand" "=h,r")
	(truncate:SI
	 (lshiftrt:DI (mult:DI (sign_extend:DI (match_operand:SI 1 "register_operand" "r,r"))
			       (match_operand:DI 2 "small_int_operand" "I,I"))
		      (match_operand:SI 3 "small_int_operand" "I,I"))))
   (clobber (match_scratch:SI 4 "=X,&h"))]
  "TARGET_V8PLUS"
  "@
   smul\t%1, %2, %0\n\tsrlx\t%0, %3, %0
   smul\t%1, %2, %4\n\tsrlx\t%4, %3, %0"
  [(set_attr "type" "multi")
   (set_attr "length" "2")])

;; XXX
(define_insn "*smulsi3_highpart_sp32"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(truncate:SI
	 (lshiftrt:DI (mult:DI (sign_extend:DI (match_operand:SI 1 "register_operand" "r"))
			       (sign_extend:DI (match_operand:SI 2 "register_operand" "r")))
		      (const_int 32))))]
  "TARGET_HARD_MUL32"
  "smul\t%1, %2, %%g0\n\trd\t%%y, %0"
  [(set_attr "type" "multi")
   (set_attr "length" "2")])

;; XXX
(define_insn "const_smulsi3_highpart"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(truncate:SI
	 (lshiftrt:DI (mult:DI (sign_extend:DI (match_operand:SI 1 "register_operand" "r"))
			       (match_operand:DI 2 "small_int_operand" "i"))
		      (const_int 32))))]
  "TARGET_HARD_MUL32"
  "smul\t%1, %2, %%g0\n\trd\t%%y, %0"
  [(set_attr "type" "multi")
   (set_attr "length" "2")])

(define_expand "umulsidi3"
  [(set (match_operand:DI 0 "register_operand" "")
	(mult:DI (zero_extend:DI (match_operand:SI 1 "register_operand" ""))
		 (zero_extend:DI (match_operand:SI 2 "uns_arith_operand" ""))))]
  "TARGET_HARD_MUL"
{
  if (CONSTANT_P (operands[2]))
    {
      if (TARGET_V8PLUS)
	emit_insn (gen_const_umulsidi3_v8plus (operands[0], operands[1],
					       operands[2]));
      else if (TARGET_ARCH32)
	emit_insn (gen_const_umulsidi3_sp32 (operands[0], operands[1],
					     operands[2]));
      else 
	emit_insn (gen_const_umulsidi3_sp64 (operands[0], operands[1],
					     operands[2]));
      DONE;
    }
  if (TARGET_V8PLUS)
    {
      emit_insn (gen_umulsidi3_v8plus (operands[0], operands[1], operands[2]));
      DONE;
    }
})

;; XXX
(define_insn "umulsidi3_v8plus"
  [(set (match_operand:DI 0 "register_operand" "=h,r")
	(mult:DI (zero_extend:DI (match_operand:SI 1 "register_operand" "r,r"))
		 (zero_extend:DI (match_operand:SI 2 "register_operand" "r,r"))))
   (clobber (match_scratch:SI 3 "=X,&h"))]
  "TARGET_V8PLUS"
  "@
   umul\t%1, %2, %L0\n\tsrlx\t%L0, 32, %H0
   umul\t%1, %2, %3\n\tsrlx\t%3, 32, %H0\n\tmov\t%3, %L0"
  [(set_attr "type" "multi")
   (set_attr "length" "2,3")])

;; XXX
(define_insn "*umulsidi3_sp32"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(mult:DI (zero_extend:DI (match_operand:SI 1 "register_operand" "r"))
		 (zero_extend:DI (match_operand:SI 2 "register_operand" "r"))))]
  "TARGET_HARD_MUL32"
{
  return TARGET_SPARCLET
         ? "umuld\t%1, %2, %L0"
         : "umul\t%1, %2, %L0\n\trd\t%%y, %H0";
}
  [(set (attr "type")
        (if_then_else (eq_attr "isa" "sparclet")
                      (const_string "imul") (const_string "multi")))
   (set (attr "length")
	(if_then_else (eq_attr "isa" "sparclet")
		      (const_int 1) (const_int 2)))])

(define_insn "*umulsidi3_sp64"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(mult:DI (zero_extend:DI (match_operand:SI 1 "register_operand" "r"))
		 (zero_extend:DI (match_operand:SI 2 "register_operand" "r"))))]
  "TARGET_DEPRECATED_V8_INSNS && TARGET_ARCH64"
  "umul\t%1, %2, %0"
  [(set_attr "type" "imul")])

;; Extra pattern, because sign_extend of a constant isn't valid.

;; XXX
(define_insn "const_umulsidi3_sp32"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(mult:DI (zero_extend:DI (match_operand:SI 1 "register_operand" "r"))
		 (match_operand:DI 2 "uns_small_int_operand" "")))]
  "TARGET_HARD_MUL32"
{
  return TARGET_SPARCLET
         ? "umuld\t%1, %s2, %L0"
         : "umul\t%1, %s2, %L0\n\trd\t%%y, %H0";
}
  [(set (attr "type")
	(if_then_else (eq_attr "isa" "sparclet")
		      (const_string "imul") (const_string "multi")))
   (set (attr "length")
	(if_then_else (eq_attr "isa" "sparclet")
		      (const_int 1) (const_int 2)))])

(define_insn "const_umulsidi3_sp64"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(mult:DI (zero_extend:DI (match_operand:SI 1 "register_operand" "r"))
		 (match_operand:DI 2 "uns_small_int_operand" "")))]
  "TARGET_DEPRECATED_V8_INSNS && TARGET_ARCH64"
  "umul\t%1, %s2, %0"
  [(set_attr "type" "imul")])

;; XXX
(define_insn "const_umulsidi3_v8plus"
  [(set (match_operand:DI 0 "register_operand" "=h,r")
	(mult:DI (zero_extend:DI (match_operand:SI 1 "register_operand" "r,r"))
		 (match_operand:DI 2 "uns_small_int_operand" "")))
   (clobber (match_scratch:SI 3 "=X,h"))]
  "TARGET_V8PLUS"
  "@
   umul\t%1, %s2, %L0\n\tsrlx\t%L0, 32, %H0
   umul\t%1, %s2, %3\n\tsrlx\t%3, 32, %H0\n\tmov\t%3, %L0"
  [(set_attr "type" "multi")
   (set_attr "length" "2,3")])

(define_expand "umulsi3_highpart"
  [(set (match_operand:SI 0 "register_operand" "")
	(truncate:SI
	 (lshiftrt:DI (mult:DI (zero_extend:DI (match_operand:SI 1 "register_operand" ""))
			       (zero_extend:DI (match_operand:SI 2 "uns_arith_operand" "")))
		      (const_int 32))))]
  "TARGET_HARD_MUL && TARGET_ARCH32"
{
  if (CONSTANT_P (operands[2]))
    {
      if (TARGET_V8PLUS)
	{
	  emit_insn (gen_const_umulsi3_highpart_v8plus (operands[0],
							operands[1],
							operands[2],
							GEN_INT (32)));
	  DONE;
	}
      emit_insn (gen_const_umulsi3_highpart (operands[0], operands[1], operands[2]));
      DONE;
    }
  if (TARGET_V8PLUS)
    {
      emit_insn (gen_umulsi3_highpart_v8plus (operands[0], operands[1],
					      operands[2], GEN_INT (32)));
      DONE;
    }
})

;; XXX
(define_insn "umulsi3_highpart_v8plus"
  [(set (match_operand:SI 0 "register_operand" "=h,r")
	(truncate:SI
	 (lshiftrt:DI (mult:DI (zero_extend:DI (match_operand:SI 1 "register_operand" "r,r"))
			       (zero_extend:DI (match_operand:SI 2 "register_operand" "r,r")))
		      (match_operand:SI 3 "small_int_operand" "I,I"))))
   (clobber (match_scratch:SI 4 "=X,h"))]
  "TARGET_V8PLUS"
  "@
   umul\t%1, %2, %0\n\tsrlx\t%0, %3, %0
   umul\t%1, %2, %4\n\tsrlx\t%4, %3, %0"
  [(set_attr "type" "multi")
   (set_attr "length" "2")])

;; XXX
(define_insn "const_umulsi3_highpart_v8plus"
  [(set (match_operand:SI 0 "register_operand" "=h,r")
	(truncate:SI
	 (lshiftrt:DI (mult:DI (zero_extend:DI (match_operand:SI 1 "register_operand" "r,r"))
			       (match_operand:DI 2 "uns_small_int_operand" ""))
		      (match_operand:SI 3 "small_int_operand" "I,I"))))
   (clobber (match_scratch:SI 4 "=X,h"))]
  "TARGET_V8PLUS"
  "@
   umul\t%1, %s2, %0\n\tsrlx\t%0, %3, %0
   umul\t%1, %s2, %4\n\tsrlx\t%4, %3, %0"
  [(set_attr "type" "multi")
   (set_attr "length" "2")])

;; XXX
(define_insn "*umulsi3_highpart_sp32"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(truncate:SI
	 (lshiftrt:DI (mult:DI (zero_extend:DI (match_operand:SI 1 "register_operand" "r"))
			       (zero_extend:DI (match_operand:SI 2 "register_operand" "r")))
		      (const_int 32))))]
  "TARGET_HARD_MUL32"
  "umul\t%1, %2, %%g0\n\trd\t%%y, %0"
  [(set_attr "type" "multi")
   (set_attr "length" "2")])

;; XXX
(define_insn "const_umulsi3_highpart"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(truncate:SI
	 (lshiftrt:DI (mult:DI (zero_extend:DI (match_operand:SI 1 "register_operand" "r"))
			       (match_operand:DI 2 "uns_small_int_operand" ""))
		      (const_int 32))))]
  "TARGET_HARD_MUL32"
  "umul\t%1, %s2, %%g0\n\trd\t%%y, %0"
  [(set_attr "type" "multi")
   (set_attr "length" "2")])

;; The V8 architecture specifies that there must be 3 instructions between
;; a Y register write and a use of it for correct results.

(define_expand "divsi3"
  [(parallel [(set (match_operand:SI 0 "register_operand" "=r,r")
		   (div:SI (match_operand:SI 1 "register_operand" "r,r")
			   (match_operand:SI 2 "input_operand" "rI,m")))
	      (clobber (match_scratch:SI 3 "=&r,&r"))])]
  "TARGET_V8 || TARGET_DEPRECATED_V8_INSNS"
{
  if (TARGET_ARCH64)
    {
      operands[3] = gen_reg_rtx(SImode);
      emit_insn (gen_ashrsi3 (operands[3], operands[1], GEN_INT (31)));
      emit_insn (gen_divsi3_sp64 (operands[0], operands[1], operands[2],
				  operands[3]));
      DONE;
    }
})

(define_insn "divsi3_sp32"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(div:SI (match_operand:SI 1 "register_operand" "r,r")
		(match_operand:SI 2 "input_operand" "rI,m")))
   (clobber (match_scratch:SI 3 "=&r,&r"))]
  "(TARGET_V8 || TARGET_DEPRECATED_V8_INSNS)
   && TARGET_ARCH32"
{
  if (which_alternative == 0)
    if (TARGET_V9)
      return "sra\t%1, 31, %3\n\twr\t%3, 0, %%y\n\tsdiv\t%1, %2, %0";
    else
      return "sra\t%1, 31, %3\n\twr\t%3, 0, %%y\n\tnop\n\tnop\n\tnop\n\tsdiv\t%1, %2, %0";
  else
    if (TARGET_V9)
      return "sra\t%1, 31, %3\n\twr\t%3, 0, %%y\n\tld\t%2, %3\n\tsdiv\t%1, %3, %0";
    else
      return "sra\t%1, 31, %3\n\twr\t%3, 0, %%y\n\tld\t%2, %3\n\tnop\n\tnop\n\tsdiv\t%1, %3, %0";
}
  [(set_attr "type" "multi")
   (set (attr "length")
	(if_then_else (eq_attr "isa" "v9")
		      (const_int 4) (const_int 6)))])

(define_insn "divsi3_sp64"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(div:SI (match_operand:SI 1 "register_operand" "r")
		(match_operand:SI 2 "input_operand" "rI")))
   (use (match_operand:SI 3 "register_operand" "r"))]
  "TARGET_DEPRECATED_V8_INSNS && TARGET_ARCH64"
  "wr\t%%g0, %3, %%y\n\tsdiv\t%1, %2, %0"
  [(set_attr "type" "multi")
   (set_attr "length" "2")])

(define_insn "divdi3"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(div:DI (match_operand:DI 1 "register_operand" "r")
		(match_operand:DI 2 "arith_operand" "rI")))]
  "TARGET_ARCH64"
  "sdivx\t%1, %2, %0"
  [(set_attr "type" "idiv")])

(define_insn "*cmp_sdiv_cc_set"
  [(set (reg:CC 100)
	(compare:CC (div:SI (match_operand:SI 1 "register_operand" "r")
			    (match_operand:SI 2 "arith_operand" "rI"))
		    (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=r")
	(div:SI (match_dup 1) (match_dup 2)))
   (clobber (match_scratch:SI 3 "=&r"))]
  "TARGET_V8 || TARGET_DEPRECATED_V8_INSNS"
{
  if (TARGET_V9)
    return "sra\t%1, 31, %3\n\twr\t%3, 0, %%y\n\tsdivcc\t%1, %2, %0";
  else
    return "sra\t%1, 31, %3\n\twr\t%3, 0, %%y\n\tnop\n\tnop\n\tnop\n\tsdivcc\t%1, %2, %0";
}
  [(set_attr "type" "multi")
   (set (attr "length")
	(if_then_else (eq_attr "isa" "v9")
		      (const_int 3) (const_int 6)))])

;; XXX
(define_expand "udivsi3"
  [(set (match_operand:SI 0 "register_operand" "")
	(udiv:SI (match_operand:SI 1 "nonimmediate_operand" "")
		 (match_operand:SI 2 "input_operand" "")))]
  "TARGET_V8 || TARGET_DEPRECATED_V8_INSNS"
  "")

;; The V8 architecture specifies that there must be 3 instructions between
;; a Y register write and a use of it for correct results.

(define_insn "udivsi3_sp32"
  [(set (match_operand:SI 0 "register_operand" "=r,&r,&r")
	(udiv:SI (match_operand:SI 1 "nonimmediate_operand" "r,r,m")
		 (match_operand:SI 2 "input_operand" "rI,m,r")))]
  "(TARGET_V8 || TARGET_DEPRECATED_V8_INSNS)
   && TARGET_ARCH32"
{
  output_asm_insn ("wr\t%%g0, %%g0, %%y", operands);
  switch (which_alternative)
    {
    default:
      return "nop\n\tnop\n\tnop\n\tudiv\t%1, %2, %0";
    case 1:
      return "ld\t%2, %0\n\tnop\n\tnop\n\tudiv\t%1, %0, %0";
    case 2:
      return "ld\t%1, %0\n\tnop\n\tnop\n\tudiv\t%0, %2, %0";
    }
}
  [(set_attr "type" "multi")
   (set_attr "length" "5")])

(define_insn "udivsi3_sp64"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(udiv:SI (match_operand:SI 1 "nonimmediate_operand" "r")
		 (match_operand:SI 2 "input_operand" "rI")))]
  "TARGET_DEPRECATED_V8_INSNS && TARGET_ARCH64"
  "wr\t%%g0, 0, %%y\n\tudiv\t%1, %2, %0"
  [(set_attr "type" "multi")
   (set_attr "length" "2")])

(define_insn "udivdi3"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(udiv:DI (match_operand:DI 1 "register_operand" "r")
		 (match_operand:DI 2 "arith_operand" "rI")))]
  "TARGET_ARCH64"
  "udivx\t%1, %2, %0"
  [(set_attr "type" "idiv")])

(define_insn "*cmp_udiv_cc_set"
  [(set (reg:CC 100)
	(compare:CC (udiv:SI (match_operand:SI 1 "register_operand" "r")
			     (match_operand:SI 2 "arith_operand" "rI"))
		    (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=r")
	(udiv:SI (match_dup 1) (match_dup 2)))]
  "TARGET_V8
   || TARGET_DEPRECATED_V8_INSNS"
{
  if (TARGET_V9)
    return "wr\t%%g0, %%g0, %%y\n\tudivcc\t%1, %2, %0";
  else
    return "wr\t%%g0, %%g0, %%y\n\tnop\n\tnop\n\tnop\n\tudivcc\t%1, %2, %0";
}
  [(set_attr "type" "multi")
   (set (attr "length")
	(if_then_else (eq_attr "isa" "v9")
		      (const_int 2) (const_int 5)))])

; sparclet multiply/accumulate insns

(define_insn "*smacsi"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(plus:SI (mult:SI (match_operand:SI 1 "register_operand" "%r")
			  (match_operand:SI 2 "arith_operand" "rI"))
		 (match_operand:SI 3 "register_operand" "0")))]
  "TARGET_SPARCLET"
  "smac\t%1, %2, %0"
  [(set_attr "type" "imul")])

(define_insn "*smacdi"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(plus:DI (mult:DI (sign_extend:DI
			   (match_operand:SI 1 "register_operand" "%r"))
			  (sign_extend:DI
			   (match_operand:SI 2 "register_operand" "r")))
		 (match_operand:DI 3 "register_operand" "0")))]
  "TARGET_SPARCLET"
  "smacd\t%1, %2, %L0"
  [(set_attr "type" "imul")])

(define_insn "*umacdi"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(plus:DI (mult:DI (zero_extend:DI
			   (match_operand:SI 1 "register_operand" "%r"))
			  (zero_extend:DI
			   (match_operand:SI 2 "register_operand" "r")))
		 (match_operand:DI 3 "register_operand" "0")))]
  "TARGET_SPARCLET"
  "umacd\t%1, %2, %L0"
  [(set_attr "type" "imul")])


;; Boolean instructions.

;; We define DImode `and' so with DImode `not' we can get
;; DImode `andn'.  Other combinations are possible.

(define_mode_macro V64I [DI V2SI V4HI V8QI])
(define_mode_macro V32I [SI V2HI V4QI])

(define_expand "and<V64I:mode>3"
  [(set (match_operand:V64I 0 "register_operand" "")
	(and:V64I (match_operand:V64I 1 "arith_double_operand" "")
		  (match_operand:V64I 2 "arith_double_operand" "")))]
  ""
  "")

(define_insn "*and<V64I:mode>3_sp32"
  [(set (match_operand:V64I 0 "register_operand" "=r,b")
	(and:V64I (match_operand:V64I 1 "arith_double_operand" "%r,b")
		  (match_operand:V64I 2 "arith_double_operand" "rHI,b")))]
  "! TARGET_ARCH64"
  "@
  #
  fand\t%1, %2, %0"
  [(set_attr "type" "*,fga")
   (set_attr "length" "2,*")
   (set_attr "fptype" "*,double")])

(define_insn "*and<V64I:mode>3_sp64"
  [(set (match_operand:V64I 0 "register_operand" "=r,b")
	(and:V64I (match_operand:V64I 1 "arith_operand" "%r,b")
		  (match_operand:V64I 2 "arith_operand" "rI,b")))]
  "TARGET_ARCH64"
  "@
   and\t%1, %2, %0
   fand\t%1, %2, %0"
  [(set_attr "type" "*,fga")
   (set_attr "fptype" "*,double")])

(define_insn "and<V32I:mode>3"
  [(set (match_operand:V32I 0 "register_operand" "=r,d")
	(and:V32I (match_operand:V32I 1 "arith_operand" "%r,d")
		  (match_operand:V32I 2 "arith_operand" "rI,d")))]
  ""
  "@
   and\t%1, %2, %0
   fands\t%1, %2, %0"
  [(set_attr "type" "*,fga")
   (set_attr "fptype" "*,single")])

(define_split
  [(set (match_operand:SI 0 "register_operand" "")
	(and:SI (match_operand:SI 1 "register_operand" "")
		(match_operand:SI 2 "const_compl_high_operand" "")))
   (clobber (match_operand:SI 3 "register_operand" ""))]
  ""
  [(set (match_dup 3) (match_dup 4))
   (set (match_dup 0) (and:SI (not:SI (match_dup 3)) (match_dup 1)))]
{
  operands[4] = GEN_INT (~INTVAL (operands[2]));
})

(define_insn_and_split "*and_not_<V64I:mode>_sp32"
  [(set (match_operand:V64I 0 "register_operand" "=r,b")
	(and:V64I (not:V64I (match_operand:V64I 1 "register_operand" "%r,b"))
		  (match_operand:V64I 2 "register_operand" "r,b")))]
  "! TARGET_ARCH64"
  "@
   #
   fandnot1\t%1, %2, %0"
  "&& reload_completed
   && ((GET_CODE (operands[0]) == REG
        && REGNO (operands[0]) < 32)
       || (GET_CODE (operands[0]) == SUBREG
           && GET_CODE (SUBREG_REG (operands[0])) == REG
           && REGNO (SUBREG_REG (operands[0])) < 32))"
  [(set (match_dup 3) (and:SI (not:SI (match_dup 4)) (match_dup 5)))
   (set (match_dup 6) (and:SI (not:SI (match_dup 7)) (match_dup 8)))]
  "operands[3] = gen_highpart (SImode, operands[0]);
   operands[4] = gen_highpart (SImode, operands[1]);
   operands[5] = gen_highpart (SImode, operands[2]);
   operands[6] = gen_lowpart (SImode, operands[0]);
   operands[7] = gen_lowpart (SImode, operands[1]);
   operands[8] = gen_lowpart (SImode, operands[2]);"
  [(set_attr "type" "*,fga")
   (set_attr "length" "2,*")
   (set_attr "fptype" "*,double")])

(define_insn "*and_not_<V64I:mode>_sp64"
  [(set (match_operand:V64I 0 "register_operand" "=r,b")
	(and:V64I (not:V64I (match_operand:V64I 1 "register_operand" "%r,b"))
		  (match_operand:V64I 2 "register_operand" "r,b")))]
  "TARGET_ARCH64"
  "@
   andn\t%2, %1, %0
   fandnot1\t%1, %2, %0"
  [(set_attr "type" "*,fga")
   (set_attr "fptype" "*,double")])

(define_insn "*and_not_<V32I:mode>"
  [(set (match_operand:V32I 0 "register_operand" "=r,d")
	(and:V32I (not:V32I (match_operand:V32I 1 "register_operand" "%r,d"))
		  (match_operand:V32I 2 "register_operand" "r,d")))]
  ""
  "@
   andn\t%2, %1, %0
   fandnot1s\t%1, %2, %0"
  [(set_attr "type" "*,fga")
   (set_attr "fptype" "*,single")])

(define_expand "ior<V64I:mode>3"
  [(set (match_operand:V64I 0 "register_operand" "")
	(ior:V64I (match_operand:V64I 1 "arith_double_operand" "")
		  (match_operand:V64I 2 "arith_double_operand" "")))]
  ""
  "")

(define_insn "*ior<V64I:mode>3_sp32"
  [(set (match_operand:V64I 0 "register_operand" "=r,b")
	(ior:V64I (match_operand:V64I 1 "arith_double_operand" "%r,b")
		  (match_operand:V64I 2 "arith_double_operand" "rHI,b")))]
  "! TARGET_ARCH64"
  "@
  #
  for\t%1, %2, %0"
  [(set_attr "type" "*,fga")
   (set_attr "length" "2,*")
   (set_attr "fptype" "*,double")])

(define_insn "*ior<V64I:mode>3_sp64"
  [(set (match_operand:V64I 0 "register_operand" "=r,b")
	(ior:V64I (match_operand:V64I 1 "arith_operand" "%r,b")
		  (match_operand:V64I 2 "arith_operand" "rI,b")))]
  "TARGET_ARCH64"
  "@
  or\t%1, %2, %0
  for\t%1, %2, %0"
  [(set_attr "type" "*,fga")
   (set_attr "fptype" "*,double")])

(define_insn "ior<V32I:mode>3"
  [(set (match_operand:V32I 0 "register_operand" "=r,d")
	(ior:V32I (match_operand:V32I 1 "arith_operand" "%r,d")
		  (match_operand:V32I 2 "arith_operand" "rI,d")))]
  ""
  "@
   or\t%1, %2, %0
   fors\t%1, %2, %0"
  [(set_attr "type" "*,fga")
   (set_attr "fptype" "*,single")])

(define_split
  [(set (match_operand:SI 0 "register_operand" "")
	(ior:SI (match_operand:SI 1 "register_operand" "")
		(match_operand:SI 2 "const_compl_high_operand" "")))
   (clobber (match_operand:SI 3 "register_operand" ""))]
  ""
  [(set (match_dup 3) (match_dup 4))
   (set (match_dup 0) (ior:SI (not:SI (match_dup 3)) (match_dup 1)))]
{
  operands[4] = GEN_INT (~INTVAL (operands[2]));
})

(define_insn_and_split "*or_not_<V64I:mode>_sp32"
  [(set (match_operand:V64I 0 "register_operand" "=r,b")
	(ior:V64I (not:V64I (match_operand:V64I 1 "register_operand" "r,b"))
		  (match_operand:V64I 2 "register_operand" "r,b")))]
  "! TARGET_ARCH64"
  "@
   #
   fornot1\t%1, %2, %0"
  "&& reload_completed
   && ((GET_CODE (operands[0]) == REG
        && REGNO (operands[0]) < 32)
       || (GET_CODE (operands[0]) == SUBREG
           && GET_CODE (SUBREG_REG (operands[0])) == REG
           && REGNO (SUBREG_REG (operands[0])) < 32))"
  [(set (match_dup 3) (ior:SI (not:SI (match_dup 4)) (match_dup 5)))
   (set (match_dup 6) (ior:SI (not:SI (match_dup 7)) (match_dup 8)))]
  "operands[3] = gen_highpart (SImode, operands[0]);
   operands[4] = gen_highpart (SImode, operands[1]);
   operands[5] = gen_highpart (SImode, operands[2]);
   operands[6] = gen_lowpart (SImode, operands[0]);
   operands[7] = gen_lowpart (SImode, operands[1]);
   operands[8] = gen_lowpart (SImode, operands[2]);"
  [(set_attr "type" "*,fga")
   (set_attr "length" "2,*")
   (set_attr "fptype" "*,double")])

(define_insn "*or_not_<V64I:mode>_sp64"
  [(set (match_operand:V64I 0 "register_operand" "=r,b")
	(ior:V64I (not:V64I (match_operand:V64I 1 "register_operand" "r,b"))
		  (match_operand:V64I 2 "register_operand" "r,b")))]
  "TARGET_ARCH64"
  "@
  orn\t%2, %1, %0
  fornot1\t%1, %2, %0"
  [(set_attr "type" "*,fga")
   (set_attr "fptype" "*,double")])

(define_insn "*or_not_<V32I:mode>"
  [(set (match_operand:V32I 0 "register_operand" "=r,d")
	(ior:V32I (not:V32I (match_operand:V32I 1 "register_operand" "r,d"))
		  (match_operand:V32I 2 "register_operand" "r,d")))]
  ""
  "@
   orn\t%2, %1, %0
   fornot1s\t%1, %2, %0"
  [(set_attr "type" "*,fga")
   (set_attr "fptype" "*,single")])

(define_expand "xor<V64I:mode>3"
  [(set (match_operand:V64I 0 "register_operand" "")
	(xor:V64I (match_operand:V64I 1 "arith_double_operand" "")
		  (match_operand:V64I 2 "arith_double_operand" "")))]
  ""
  "")

(define_insn "*xor<V64I:mode>3_sp32"
  [(set (match_operand:V64I 0 "register_operand" "=r,b")
	(xor:V64I (match_operand:V64I 1 "arith_double_operand" "%r,b")
		  (match_operand:V64I 2 "arith_double_operand" "rHI,b")))]
  "! TARGET_ARCH64"
  "@
  #
  fxor\t%1, %2, %0"
  [(set_attr "type" "*,fga")
   (set_attr "length" "2,*")
   (set_attr "fptype" "*,double")])

(define_insn "*xor<V64I:mode>3_sp64"
  [(set (match_operand:V64I 0 "register_operand" "=r,b")
	(xor:V64I (match_operand:V64I 1 "arith_operand" "%rJ,b")
		  (match_operand:V64I 2 "arith_operand" "rI,b")))]
  "TARGET_ARCH64"
  "@
  xor\t%r1, %2, %0
  fxor\t%1, %2, %0"
  [(set_attr "type" "*,fga")
   (set_attr "fptype" "*,double")])

(define_insn "xor<V32I:mode>3"
  [(set (match_operand:V32I 0 "register_operand" "=r,d")
	(xor:V32I (match_operand:V32I 1 "arith_operand" "%rJ,d")
		  (match_operand:V32I 2 "arith_operand" "rI,d")))]
  ""
  "@
   xor\t%r1, %2, %0
   fxors\t%1, %2, %0"
  [(set_attr "type" "*,fga")
   (set_attr "fptype" "*,single")])

(define_split
  [(set (match_operand:SI 0 "register_operand" "")
	(xor:SI (match_operand:SI 1 "register_operand" "")
		(match_operand:SI 2 "const_compl_high_operand" "")))
   (clobber (match_operand:SI 3 "register_operand" ""))]
   ""
  [(set (match_dup 3) (match_dup 4))
   (set (match_dup 0) (not:SI (xor:SI (match_dup 3) (match_dup 1))))]
{
  operands[4] = GEN_INT (~INTVAL (operands[2]));
})

(define_split
  [(set (match_operand:SI 0 "register_operand" "")
	(not:SI (xor:SI (match_operand:SI 1 "register_operand" "")
			(match_operand:SI 2 "const_compl_high_operand" ""))))
   (clobber (match_operand:SI 3 "register_operand" ""))]
  ""
  [(set (match_dup 3) (match_dup 4))
   (set (match_dup 0) (xor:SI (match_dup 3) (match_dup 1)))]
{
  operands[4] = GEN_INT (~INTVAL (operands[2]));
})

;; Split DImode logical operations requiring two instructions.
(define_split
  [(set (match_operand:V64I 0 "register_operand" "")
	(match_operator:V64I 1 "cc_arith_operator"	; AND, IOR, XOR
			   [(match_operand:V64I 2 "register_operand" "")
			    (match_operand:V64I 3 "arith_double_operand" "")]))]
  "! TARGET_ARCH64
   && reload_completed
   && ((GET_CODE (operands[0]) == REG
        && REGNO (operands[0]) < 32)
       || (GET_CODE (operands[0]) == SUBREG
           && GET_CODE (SUBREG_REG (operands[0])) == REG
           && REGNO (SUBREG_REG (operands[0])) < 32))"
  [(set (match_dup 4) (match_op_dup:SI 1 [(match_dup 6) (match_dup 8)]))
   (set (match_dup 5) (match_op_dup:SI 1 [(match_dup 7) (match_dup 9)]))]
{
  operands[4] = gen_highpart (SImode, operands[0]);
  operands[5] = gen_lowpart (SImode, operands[0]);
  operands[6] = gen_highpart (SImode, operands[2]);
  operands[7] = gen_lowpart (SImode, operands[2]);
#if HOST_BITS_PER_WIDE_INT == 32
  if (GET_CODE (operands[3]) == CONST_INT && <V64I:MODE>mode == DImode)
    {
      if (INTVAL (operands[3]) < 0)
	operands[8] = constm1_rtx;
      else
	operands[8] = const0_rtx;
    }
  else
#endif
    operands[8] = gen_highpart_mode (SImode, <V64I:MODE>mode, operands[3]);
  operands[9] = gen_lowpart (SImode, operands[3]);
})

;; xnor patterns.  Note that (a ^ ~b) == (~a ^ b) == ~(a ^ b).
;; Combine now canonicalizes to the rightmost expression.
(define_insn_and_split "*xor_not_<V64I:mode>_sp32"
  [(set (match_operand:V64I 0 "register_operand" "=r,b")
	(not:V64I (xor:V64I (match_operand:V64I 1 "register_operand" "r,b")
			    (match_operand:V64I 2 "register_operand" "r,b"))))]
  "! TARGET_ARCH64"
  "@
   #
   fxnor\t%1, %2, %0"
  "&& reload_completed
   && ((GET_CODE (operands[0]) == REG
        && REGNO (operands[0]) < 32)
       || (GET_CODE (operands[0]) == SUBREG
           && GET_CODE (SUBREG_REG (operands[0])) == REG
           && REGNO (SUBREG_REG (operands[0])) < 32))"
  [(set (match_dup 3) (not:SI (xor:SI (match_dup 4) (match_dup 5))))
   (set (match_dup 6) (not:SI (xor:SI (match_dup 7) (match_dup 8))))]
  "operands[3] = gen_highpart (SImode, operands[0]);
   operands[4] = gen_highpart (SImode, operands[1]);
   operands[5] = gen_highpart (SImode, operands[2]);
   operands[6] = gen_lowpart (SImode, operands[0]);
   operands[7] = gen_lowpart (SImode, operands[1]);
   operands[8] = gen_lowpart (SImode, operands[2]);"
  [(set_attr "type" "*,fga")
   (set_attr "length" "2,*")
   (set_attr "fptype" "*,double")])

(define_insn "*xor_not_<V64I:mode>_sp64"
  [(set (match_operand:V64I 0 "register_operand" "=r,b")
	(not:V64I (xor:V64I (match_operand:V64I 1 "register_or_zero_operand" "rJ,b")
			    (match_operand:V64I 2 "arith_operand" "rI,b"))))]
  "TARGET_ARCH64"
  "@
  xnor\t%r1, %2, %0
  fxnor\t%1, %2, %0"
  [(set_attr "type" "*,fga")
   (set_attr "fptype" "*,double")])

(define_insn "*xor_not_<V32I:mode>"
  [(set (match_operand:V32I 0 "register_operand" "=r,d")
	(not:V32I (xor:V32I (match_operand:V32I 1 "register_or_zero_operand" "rJ,d")
			    (match_operand:V32I 2 "arith_operand" "rI,d"))))]
  ""
  "@
   xnor\t%r1, %2, %0
   fxnors\t%1, %2, %0"
  [(set_attr "type" "*,fga")
   (set_attr "fptype" "*,single")])

;; These correspond to the above in the case where we also (or only)
;; want to set the condition code.  

(define_insn "*cmp_cc_arith_op"
  [(set (reg:CC 100)
	(compare:CC
	 (match_operator:SI 2 "cc_arith_operator"
			    [(match_operand:SI 0 "arith_operand" "%r")
			     (match_operand:SI 1 "arith_operand" "rI")])
	 (const_int 0)))]
  ""
  "%A2cc\t%0, %1, %%g0"
  [(set_attr "type" "compare")])

(define_insn "*cmp_ccx_arith_op"
  [(set (reg:CCX 100)
	(compare:CCX
	 (match_operator:DI 2 "cc_arith_operator"
			    [(match_operand:DI 0 "arith_operand" "%r")
			     (match_operand:DI 1 "arith_operand" "rI")])
	 (const_int 0)))]
  "TARGET_ARCH64"
  "%A2cc\t%0, %1, %%g0"
  [(set_attr "type" "compare")])

(define_insn "*cmp_cc_arith_op_set"
  [(set (reg:CC 100)
	(compare:CC
	 (match_operator:SI 3 "cc_arith_operator"
			    [(match_operand:SI 1 "arith_operand" "%r")
			     (match_operand:SI 2 "arith_operand" "rI")])
	 (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=r")
	(match_operator:SI 4 "cc_arith_operator" [(match_dup 1) (match_dup 2)]))]
  "GET_CODE (operands[3]) == GET_CODE (operands[4])"
  "%A3cc\t%1, %2, %0"
  [(set_attr "type" "compare")])

(define_insn "*cmp_ccx_arith_op_set"
  [(set (reg:CCX 100)
	(compare:CCX
	 (match_operator:DI 3 "cc_arith_operator"
			    [(match_operand:DI 1 "arith_operand" "%r")
			     (match_operand:DI 2 "arith_operand" "rI")])
	 (const_int 0)))
   (set (match_operand:DI 0 "register_operand" "=r")
	(match_operator:DI 4 "cc_arith_operator" [(match_dup 1) (match_dup 2)]))]
  "TARGET_ARCH64 && GET_CODE (operands[3]) == GET_CODE (operands[4])"
  "%A3cc\t%1, %2, %0"
  [(set_attr "type" "compare")])

(define_insn "*cmp_cc_xor_not"
  [(set (reg:CC 100)
	(compare:CC
	 (not:SI (xor:SI (match_operand:SI 0 "register_or_zero_operand" "%rJ")
			 (match_operand:SI 1 "arith_operand" "rI")))
	 (const_int 0)))]
  ""
  "xnorcc\t%r0, %1, %%g0"
  [(set_attr "type" "compare")])

(define_insn "*cmp_ccx_xor_not"
  [(set (reg:CCX 100)
	(compare:CCX
	 (not:DI (xor:DI (match_operand:DI 0 "register_or_zero_operand" "%rJ")
			 (match_operand:DI 1 "arith_operand" "rI")))
	 (const_int 0)))]
  "TARGET_ARCH64"
  "xnorcc\t%r0, %1, %%g0"
  [(set_attr "type" "compare")])

(define_insn "*cmp_cc_xor_not_set"
  [(set (reg:CC 100)
	(compare:CC
	 (not:SI (xor:SI (match_operand:SI 1 "register_or_zero_operand" "%rJ")
			 (match_operand:SI 2 "arith_operand" "rI")))
	 (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=r")
	(not:SI (xor:SI (match_dup 1) (match_dup 2))))]
  ""
  "xnorcc\t%r1, %2, %0"
  [(set_attr "type" "compare")])

(define_insn "*cmp_ccx_xor_not_set"
  [(set (reg:CCX 100)
	(compare:CCX
	 (not:DI (xor:DI (match_operand:DI 1 "register_or_zero_operand" "%rJ")
			 (match_operand:DI 2 "arith_operand" "rI")))
	 (const_int 0)))
   (set (match_operand:DI 0 "register_operand" "=r")
	(not:DI (xor:DI (match_dup 1) (match_dup 2))))]
  "TARGET_ARCH64"
  "xnorcc\t%r1, %2, %0"
  [(set_attr "type" "compare")])

(define_insn "*cmp_cc_arith_op_not"
  [(set (reg:CC 100)
	(compare:CC
	 (match_operator:SI 2 "cc_arith_not_operator"
			    [(not:SI (match_operand:SI 0 "arith_operand" "rI"))
			     (match_operand:SI 1 "register_or_zero_operand" "rJ")])
	 (const_int 0)))]
  ""
  "%B2cc\t%r1, %0, %%g0"
  [(set_attr "type" "compare")])

(define_insn "*cmp_ccx_arith_op_not"
  [(set (reg:CCX 100)
	(compare:CCX
	 (match_operator:DI 2 "cc_arith_not_operator"
			    [(not:DI (match_operand:DI 0 "arith_operand" "rI"))
			     (match_operand:DI 1 "register_or_zero_operand" "rJ")])
	 (const_int 0)))]
  "TARGET_ARCH64"
  "%B2cc\t%r1, %0, %%g0"
  [(set_attr "type" "compare")])

(define_insn "*cmp_cc_arith_op_not_set"
  [(set (reg:CC 100)
	(compare:CC
	 (match_operator:SI 3 "cc_arith_not_operator"
			    [(not:SI (match_operand:SI 1 "arith_operand" "rI"))
			     (match_operand:SI 2 "register_or_zero_operand" "rJ")])
	 (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=r")
	(match_operator:SI 4 "cc_arith_not_operator"
			    [(not:SI (match_dup 1)) (match_dup 2)]))]
  "GET_CODE (operands[3]) == GET_CODE (operands[4])"
  "%B3cc\t%r2, %1, %0"
  [(set_attr "type" "compare")])

(define_insn "*cmp_ccx_arith_op_not_set"
  [(set (reg:CCX 100)
	(compare:CCX
	 (match_operator:DI 3 "cc_arith_not_operator"
			    [(not:DI (match_operand:DI 1 "arith_operand" "rI"))
			     (match_operand:DI 2 "register_or_zero_operand" "rJ")])
	 (const_int 0)))
   (set (match_operand:DI 0 "register_operand" "=r")
	(match_operator:DI 4 "cc_arith_not_operator"
			    [(not:DI (match_dup 1)) (match_dup 2)]))]
  "TARGET_ARCH64 && GET_CODE (operands[3]) == GET_CODE (operands[4])"
  "%B3cc\t%r2, %1, %0"
  [(set_attr "type" "compare")])

;; We cannot use the "neg" pseudo insn because the Sun assembler
;; does not know how to make it work for constants.

(define_expand "negdi2"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(neg:DI (match_operand:DI 1 "register_operand" "r")))]
  ""
{
  if (! TARGET_ARCH64)
    {
      emit_insn (gen_rtx_PARALLEL
		 (VOIDmode,
		  gen_rtvec (2,
			     gen_rtx_SET (VOIDmode, operand0,
					  gen_rtx_NEG (DImode, operand1)),
			     gen_rtx_CLOBBER (VOIDmode,
					      gen_rtx_REG (CCmode,
							   SPARC_ICC_REG)))));
      DONE;
    }
})

(define_insn_and_split "*negdi2_sp32"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(neg:DI (match_operand:DI 1 "register_operand" "r")))
   (clobber (reg:CC 100))]
  "TARGET_ARCH32"
  "#"
  "&& reload_completed"
  [(parallel [(set (reg:CC_NOOV 100)
                   (compare:CC_NOOV (minus:SI (const_int 0) (match_dup 5))
                                    (const_int 0)))
              (set (match_dup 4) (minus:SI (const_int 0) (match_dup 5)))])
   (set (match_dup 2) (minus:SI (minus:SI (const_int 0) (match_dup 3))
                                (ltu:SI (reg:CC 100) (const_int 0))))]
  "operands[2] = gen_highpart (SImode, operands[0]);
   operands[3] = gen_highpart (SImode, operands[1]);
   operands[4] = gen_lowpart (SImode, operands[0]);
   operands[5] = gen_lowpart (SImode, operands[1]);"
  [(set_attr "length" "2")])

(define_insn "*negdi2_sp64"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(neg:DI (match_operand:DI 1 "register_operand" "r")))]
  "TARGET_ARCH64"
  "sub\t%%g0, %1, %0")

(define_insn "negsi2"
  [(set (match_operand:SI 0 "register_operand" "=r")
        (neg:SI (match_operand:SI 1 "arith_operand" "rI")))]
  ""
  "sub\t%%g0, %1, %0")

(define_insn "*cmp_cc_neg"
  [(set (reg:CC_NOOV 100)
	(compare:CC_NOOV (neg:SI (match_operand:SI 0 "arith_operand" "rI"))
			 (const_int 0)))]
  ""
  "subcc\t%%g0, %0, %%g0"
  [(set_attr "type" "compare")])

(define_insn "*cmp_ccx_neg"
  [(set (reg:CCX_NOOV 100)
	(compare:CCX_NOOV (neg:DI (match_operand:DI 0 "arith_operand" "rI"))
			  (const_int 0)))]
  "TARGET_ARCH64"
  "subcc\t%%g0, %0, %%g0"
  [(set_attr "type" "compare")])

(define_insn "*cmp_cc_set_neg"
  [(set (reg:CC_NOOV 100)
	(compare:CC_NOOV (neg:SI (match_operand:SI 1 "arith_operand" "rI"))
			 (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=r")
	(neg:SI (match_dup 1)))]
  ""
  "subcc\t%%g0, %1, %0"
  [(set_attr "type" "compare")])

(define_insn "*cmp_ccx_set_neg"
  [(set (reg:CCX_NOOV 100)
	(compare:CCX_NOOV (neg:DI (match_operand:DI 1 "arith_operand" "rI"))
			  (const_int 0)))
   (set (match_operand:DI 0 "register_operand" "=r")
	(neg:DI (match_dup 1)))]
  "TARGET_ARCH64"
  "subcc\t%%g0, %1, %0"
  [(set_attr "type" "compare")])

;; We cannot use the "not" pseudo insn because the Sun assembler
;; does not know how to make it work for constants.
(define_expand "one_cmpl<V64I:mode>2"
  [(set (match_operand:V64I 0 "register_operand" "")
	(not:V64I (match_operand:V64I 1 "register_operand" "")))]
  ""
  "")

(define_insn_and_split "*one_cmpl<V64I:mode>2_sp32"
  [(set (match_operand:V64I 0 "register_operand" "=r,b")
	(not:V64I (match_operand:V64I 1 "register_operand" "r,b")))]
  "! TARGET_ARCH64"
  "@
   #
   fnot1\t%1, %0"
  "&& reload_completed
   && ((GET_CODE (operands[0]) == REG
        && REGNO (operands[0]) < 32)
       || (GET_CODE (operands[0]) == SUBREG
           && GET_CODE (SUBREG_REG (operands[0])) == REG
           && REGNO (SUBREG_REG (operands[0])) < 32))"
  [(set (match_dup 2) (not:SI (xor:SI (match_dup 3) (const_int 0))))
   (set (match_dup 4) (not:SI (xor:SI (match_dup 5) (const_int 0))))]
  "operands[2] = gen_highpart (SImode, operands[0]);
   operands[3] = gen_highpart (SImode, operands[1]);
   operands[4] = gen_lowpart (SImode, operands[0]);
   operands[5] = gen_lowpart (SImode, operands[1]);"
  [(set_attr "type" "*,fga")
   (set_attr "length" "2,*")
   (set_attr "fptype" "*,double")])

(define_insn "*one_cmpl<V64I:mode>2_sp64"
  [(set (match_operand:V64I 0 "register_operand" "=r,b")
	(not:V64I (match_operand:V64I 1 "arith_operand" "rI,b")))]
  "TARGET_ARCH64"
  "@
   xnor\t%%g0, %1, %0
   fnot1\t%1, %0"
  [(set_attr "type" "*,fga")
   (set_attr "fptype" "*,double")])

(define_insn "one_cmpl<V32I:mode>2"
  [(set (match_operand:V32I 0 "register_operand" "=r,d")
	(not:V32I (match_operand:V32I 1 "arith_operand" "rI,d")))]
  ""
  "@
  xnor\t%%g0, %1, %0
  fnot1s\t%1, %0"
  [(set_attr "type" "*,fga")
   (set_attr "fptype" "*,single")])

(define_insn "*cmp_cc_not"
  [(set (reg:CC 100)
	(compare:CC (not:SI (match_operand:SI 0 "arith_operand" "rI"))
		    (const_int 0)))]
  ""
  "xnorcc\t%%g0, %0, %%g0"
  [(set_attr "type" "compare")])

(define_insn "*cmp_ccx_not"
  [(set (reg:CCX 100)
	(compare:CCX (not:DI (match_operand:DI 0 "arith_operand" "rI"))
		     (const_int 0)))]
  "TARGET_ARCH64"
  "xnorcc\t%%g0, %0, %%g0"
  [(set_attr "type" "compare")])

(define_insn "*cmp_cc_set_not"
  [(set (reg:CC 100)
	(compare:CC (not:SI (match_operand:SI 1 "arith_operand" "rI"))
		    (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=r")
	(not:SI (match_dup 1)))]
  ""
  "xnorcc\t%%g0, %1, %0"
  [(set_attr "type" "compare")])

(define_insn "*cmp_ccx_set_not"
  [(set (reg:CCX 100)
	(compare:CCX (not:DI (match_operand:DI 1 "arith_operand" "rI"))
		    (const_int 0)))
   (set (match_operand:DI 0 "register_operand" "=r")
	(not:DI (match_dup 1)))]
  "TARGET_ARCH64"
  "xnorcc\t%%g0, %1, %0"
  [(set_attr "type" "compare")])

(define_insn "*cmp_cc_set"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(match_operand:SI 1 "register_operand" "r"))
   (set (reg:CC 100)
	(compare:CC (match_dup 1)
		    (const_int 0)))]
  ""
  "orcc\t%1, 0, %0"
  [(set_attr "type" "compare")])

(define_insn "*cmp_ccx_set64"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(match_operand:DI 1 "register_operand" "r"))
   (set (reg:CCX 100)
	(compare:CCX (match_dup 1)
		     (const_int 0)))]
  "TARGET_ARCH64"
  "orcc\t%1, 0, %0"
   [(set_attr "type" "compare")])


;; Floating point arithmetic instructions.

(define_expand "addtf3"
  [(set (match_operand:TF 0 "nonimmediate_operand" "")
	(plus:TF (match_operand:TF 1 "general_operand" "")
		 (match_operand:TF 2 "general_operand" "")))]
  "TARGET_FPU && (TARGET_HARD_QUAD || TARGET_ARCH64)"
  "emit_tfmode_binop (PLUS, operands); DONE;")

(define_insn "*addtf3_hq"
  [(set (match_operand:TF 0 "register_operand" "=e")
	(plus:TF (match_operand:TF 1 "register_operand" "e")
		 (match_operand:TF 2 "register_operand" "e")))]
  "TARGET_FPU && TARGET_HARD_QUAD"
  "faddq\t%1, %2, %0"
  [(set_attr "type" "fp")])

(define_insn "adddf3"
  [(set (match_operand:DF 0 "register_operand" "=e")
	(plus:DF (match_operand:DF 1 "register_operand" "e")
		 (match_operand:DF 2 "register_operand" "e")))]
  "TARGET_FPU"
  "faddd\t%1, %2, %0"
  [(set_attr "type" "fp")
   (set_attr "fptype" "double")])

(define_insn "addsf3"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(plus:SF (match_operand:SF 1 "register_operand" "f")
		 (match_operand:SF 2 "register_operand" "f")))]
  "TARGET_FPU"
  "fadds\t%1, %2, %0"
  [(set_attr "type" "fp")])

(define_expand "subtf3"
  [(set (match_operand:TF 0 "nonimmediate_operand" "")
	(minus:TF (match_operand:TF 1 "general_operand" "")
		  (match_operand:TF 2 "general_operand" "")))]
  "TARGET_FPU && (TARGET_HARD_QUAD || TARGET_ARCH64)"
  "emit_tfmode_binop (MINUS, operands); DONE;")

(define_insn "*subtf3_hq"
  [(set (match_operand:TF 0 "register_operand" "=e")
	(minus:TF (match_operand:TF 1 "register_operand" "e")
		  (match_operand:TF 2 "register_operand" "e")))]
  "TARGET_FPU && TARGET_HARD_QUAD"
  "fsubq\t%1, %2, %0"
  [(set_attr "type" "fp")])

(define_insn "subdf3"
  [(set (match_operand:DF 0 "register_operand" "=e")
	(minus:DF (match_operand:DF 1 "register_operand" "e")
		  (match_operand:DF 2 "register_operand" "e")))]
  "TARGET_FPU"
  "fsubd\t%1, %2, %0"
  [(set_attr "type" "fp")
   (set_attr "fptype" "double")])

(define_insn "subsf3"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(minus:SF (match_operand:SF 1 "register_operand" "f")
		  (match_operand:SF 2 "register_operand" "f")))]
  "TARGET_FPU"
  "fsubs\t%1, %2, %0"
  [(set_attr "type" "fp")])

(define_expand "multf3"
  [(set (match_operand:TF 0 "nonimmediate_operand" "")
	(mult:TF (match_operand:TF 1 "general_operand" "")
		 (match_operand:TF 2 "general_operand" "")))]
  "TARGET_FPU && (TARGET_HARD_QUAD || TARGET_ARCH64)"
  "emit_tfmode_binop (MULT, operands); DONE;")

(define_insn "*multf3_hq"
  [(set (match_operand:TF 0 "register_operand" "=e")
	(mult:TF (match_operand:TF 1 "register_operand" "e")
		 (match_operand:TF 2 "register_operand" "e")))]
  "TARGET_FPU && TARGET_HARD_QUAD"
  "fmulq\t%1, %2, %0"
  [(set_attr "type" "fpmul")])

(define_insn "muldf3"
  [(set (match_operand:DF 0 "register_operand" "=e")
	(mult:DF (match_operand:DF 1 "register_operand" "e")
		 (match_operand:DF 2 "register_operand" "e")))]
  "TARGET_FPU"
  "fmuld\t%1, %2, %0"
  [(set_attr "type" "fpmul")
   (set_attr "fptype" "double")])

(define_insn "mulsf3"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(mult:SF (match_operand:SF 1 "register_operand" "f")
		 (match_operand:SF 2 "register_operand" "f")))]
  "TARGET_FPU"
  "fmuls\t%1, %2, %0"
  [(set_attr "type" "fpmul")])

(define_insn "*muldf3_extend"
  [(set (match_operand:DF 0 "register_operand" "=e")
	(mult:DF (float_extend:DF (match_operand:SF 1 "register_operand" "f"))
		 (float_extend:DF (match_operand:SF 2 "register_operand" "f"))))]
  "(TARGET_V8 || TARGET_V9) && TARGET_FPU"
  "fsmuld\t%1, %2, %0"
  [(set_attr "type" "fpmul")
   (set_attr "fptype" "double")])

(define_insn "*multf3_extend"
  [(set (match_operand:TF 0 "register_operand" "=e")
	(mult:TF (float_extend:TF (match_operand:DF 1 "register_operand" "e"))
		 (float_extend:TF (match_operand:DF 2 "register_operand" "e"))))]
  "(TARGET_V8 || TARGET_V9) && TARGET_FPU && TARGET_HARD_QUAD"
  "fdmulq\t%1, %2, %0"
  [(set_attr "type" "fpmul")])

(define_expand "divtf3"
  [(set (match_operand:TF 0 "nonimmediate_operand" "")
	(div:TF (match_operand:TF 1 "general_operand" "")
		(match_operand:TF 2 "general_operand" "")))]
  "TARGET_FPU && (TARGET_HARD_QUAD || TARGET_ARCH64)"
  "emit_tfmode_binop (DIV, operands); DONE;")

;; don't have timing for quad-prec. divide.
(define_insn "*divtf3_hq"
  [(set (match_operand:TF 0 "register_operand" "=e")
	(div:TF (match_operand:TF 1 "register_operand" "e")
		(match_operand:TF 2 "register_operand" "e")))]
  "TARGET_FPU && TARGET_HARD_QUAD"
  "fdivq\t%1, %2, %0"
  [(set_attr "type" "fpdivd")])

(define_insn "divdf3"
  [(set (match_operand:DF 0 "register_operand" "=e")
	(div:DF (match_operand:DF 1 "register_operand" "e")
		(match_operand:DF 2 "register_operand" "e")))]
  "TARGET_FPU"
  "fdivd\t%1, %2, %0"
  [(set_attr "type" "fpdivd")
   (set_attr "fptype" "double")])

(define_insn "divsf3"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(div:SF (match_operand:SF 1 "register_operand" "f")
		(match_operand:SF 2 "register_operand" "f")))]
  "TARGET_FPU"
  "fdivs\t%1, %2, %0"
  [(set_attr "type" "fpdivs")])

(define_expand "negtf2"
  [(set (match_operand:TF 0 "register_operand" "=e,e")
	(neg:TF (match_operand:TF 1 "register_operand" "0,e")))]
  "TARGET_FPU"
  "")

(define_insn_and_split "*negtf2_notv9"
  [(set (match_operand:TF 0 "register_operand" "=e,e")
	(neg:TF (match_operand:TF 1 "register_operand" "0,e")))]
  ; We don't use quad float insns here so we don't need TARGET_HARD_QUAD.
  "TARGET_FPU
   && ! TARGET_V9"
  "@
  fnegs\t%0, %0
  #"
  "&& reload_completed
   && sparc_absnegfloat_split_legitimate (operands[0], operands[1])"
  [(set (match_dup 2) (neg:SF (match_dup 3)))
   (set (match_dup 4) (match_dup 5))
   (set (match_dup 6) (match_dup 7))]
  "operands[2] = gen_rtx_raw_REG (SFmode, REGNO (operands[0]));
   operands[3] = gen_rtx_raw_REG (SFmode, REGNO (operands[1]));
   operands[4] = gen_rtx_raw_REG (SFmode, REGNO (operands[0]) + 1);
   operands[5] = gen_rtx_raw_REG (SFmode, REGNO (operands[1]) + 1);
   operands[6] = gen_rtx_raw_REG (DFmode, REGNO (operands[0]) + 2);
   operands[7] = gen_rtx_raw_REG (DFmode, REGNO (operands[1]) + 2);"
  [(set_attr "type" "fpmove,*")
   (set_attr "length" "*,2")])

(define_insn_and_split "*negtf2_v9"
  [(set (match_operand:TF 0 "register_operand" "=e,e")
	(neg:TF (match_operand:TF 1 "register_operand" "0,e")))]
  ; We don't use quad float insns here so we don't need TARGET_HARD_QUAD.
  "TARGET_FPU && TARGET_V9"
  "@
  fnegd\t%0, %0
  #"
  "&& reload_completed
   && sparc_absnegfloat_split_legitimate (operands[0], operands[1])"
  [(set (match_dup 2) (neg:DF (match_dup 3)))
   (set (match_dup 4) (match_dup 5))]
  "operands[2] = gen_rtx_raw_REG (DFmode, REGNO (operands[0]));
   operands[3] = gen_rtx_raw_REG (DFmode, REGNO (operands[1]));
   operands[4] = gen_rtx_raw_REG (DFmode, REGNO (operands[0]) + 2);
   operands[5] = gen_rtx_raw_REG (DFmode, REGNO (operands[1]) + 2);"
  [(set_attr "type" "fpmove,*")
   (set_attr "length" "*,2")
   (set_attr "fptype" "double")])

(define_expand "negdf2"
  [(set (match_operand:DF 0 "register_operand" "")
	(neg:DF (match_operand:DF 1 "register_operand" "")))]
  "TARGET_FPU"
  "")

(define_insn_and_split "*negdf2_notv9"
  [(set (match_operand:DF 0 "register_operand" "=e,e")
	(neg:DF (match_operand:DF 1 "register_operand" "0,e")))]
  "TARGET_FPU && ! TARGET_V9"
  "@
  fnegs\t%0, %0
  #"
  "&& reload_completed
   && sparc_absnegfloat_split_legitimate (operands[0], operands[1])"
  [(set (match_dup 2) (neg:SF (match_dup 3)))
   (set (match_dup 4) (match_dup 5))]
  "operands[2] = gen_rtx_raw_REG (SFmode, REGNO (operands[0]));
   operands[3] = gen_rtx_raw_REG (SFmode, REGNO (operands[1]));
   operands[4] = gen_rtx_raw_REG (SFmode, REGNO (operands[0]) + 1);
   operands[5] = gen_rtx_raw_REG (SFmode, REGNO (operands[1]) + 1);"
  [(set_attr "type" "fpmove,*")
   (set_attr "length" "*,2")])

(define_insn "*negdf2_v9"
  [(set (match_operand:DF 0 "register_operand" "=e")
	(neg:DF (match_operand:DF 1 "register_operand" "e")))]
  "TARGET_FPU && TARGET_V9"
  "fnegd\t%1, %0"
  [(set_attr "type" "fpmove")
   (set_attr "fptype" "double")])

(define_insn "negsf2"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(neg:SF (match_operand:SF 1 "register_operand" "f")))]
  "TARGET_FPU"
  "fnegs\t%1, %0"
  [(set_attr "type" "fpmove")])

(define_expand "abstf2"
  [(set (match_operand:TF 0 "register_operand" "")
	(abs:TF (match_operand:TF 1 "register_operand" "")))]
  "TARGET_FPU"
  "")

(define_insn_and_split "*abstf2_notv9"
  [(set (match_operand:TF 0 "register_operand" "=e,e")
	(abs:TF (match_operand:TF 1 "register_operand" "0,e")))]
  ; We don't use quad float insns here so we don't need TARGET_HARD_QUAD.
  "TARGET_FPU && ! TARGET_V9"
  "@
  fabss\t%0, %0
  #"
  "&& reload_completed
   && sparc_absnegfloat_split_legitimate (operands[0], operands[1])"
  [(set (match_dup 2) (abs:SF (match_dup 3)))
   (set (match_dup 4) (match_dup 5))
   (set (match_dup 6) (match_dup 7))]
  "operands[2] = gen_rtx_raw_REG (SFmode, REGNO (operands[0]));
   operands[3] = gen_rtx_raw_REG (SFmode, REGNO (operands[1]));
   operands[4] = gen_rtx_raw_REG (SFmode, REGNO (operands[0]) + 1);
   operands[5] = gen_rtx_raw_REG (SFmode, REGNO (operands[1]) + 1);
   operands[6] = gen_rtx_raw_REG (DFmode, REGNO (operands[0]) + 2);
   operands[7] = gen_rtx_raw_REG (DFmode, REGNO (operands[1]) + 2);"
  [(set_attr "type" "fpmove,*")
   (set_attr "length" "*,2")])

(define_insn "*abstf2_hq_v9"
  [(set (match_operand:TF 0 "register_operand" "=e,e")
	(abs:TF (match_operand:TF 1 "register_operand" "0,e")))]
  "TARGET_FPU && TARGET_V9 && TARGET_HARD_QUAD"
  "@
  fabsd\t%0, %0
  fabsq\t%1, %0"
  [(set_attr "type" "fpmove")
   (set_attr "fptype" "double,*")])

(define_insn_and_split "*abstf2_v9"
  [(set (match_operand:TF 0 "register_operand" "=e,e")
	(abs:TF (match_operand:TF 1 "register_operand" "0,e")))]
  "TARGET_FPU && TARGET_V9 && !TARGET_HARD_QUAD"
  "@
  fabsd\t%0, %0
  #"
  "&& reload_completed
   && sparc_absnegfloat_split_legitimate (operands[0], operands[1])"
  [(set (match_dup 2) (abs:DF (match_dup 3)))
   (set (match_dup 4) (match_dup 5))]
  "operands[2] = gen_rtx_raw_REG (DFmode, REGNO (operands[0]));
   operands[3] = gen_rtx_raw_REG (DFmode, REGNO (operands[1]));
   operands[4] = gen_rtx_raw_REG (DFmode, REGNO (operands[0]) + 2);
   operands[5] = gen_rtx_raw_REG (DFmode, REGNO (operands[1]) + 2);"
  [(set_attr "type" "fpmove,*")
   (set_attr "length" "*,2")
   (set_attr "fptype" "double,*")])

(define_expand "absdf2"
  [(set (match_operand:DF 0 "register_operand" "")
	(abs:DF (match_operand:DF 1 "register_operand" "")))]
  "TARGET_FPU"
  "")

(define_insn_and_split "*absdf2_notv9"
  [(set (match_operand:DF 0 "register_operand" "=e,e")
	(abs:DF (match_operand:DF 1 "register_operand" "0,e")))]
  "TARGET_FPU && ! TARGET_V9"
  "@
  fabss\t%0, %0
  #"
  "&& reload_completed
   && sparc_absnegfloat_split_legitimate (operands[0], operands[1])"
  [(set (match_dup 2) (abs:SF (match_dup 3)))
   (set (match_dup 4) (match_dup 5))]
  "operands[2] = gen_rtx_raw_REG (SFmode, REGNO (operands[0]));
   operands[3] = gen_rtx_raw_REG (SFmode, REGNO (operands[1]));
   operands[4] = gen_rtx_raw_REG (SFmode, REGNO (operands[0]) + 1);
   operands[5] = gen_rtx_raw_REG (SFmode, REGNO (operands[1]) + 1);"
  [(set_attr "type" "fpmove,*")
   (set_attr "length" "*,2")])

(define_insn "*absdf2_v9"
  [(set (match_operand:DF 0 "register_operand" "=e")
	(abs:DF (match_operand:DF 1 "register_operand" "e")))]
  "TARGET_FPU && TARGET_V9"
  "fabsd\t%1, %0"
  [(set_attr "type" "fpmove")
   (set_attr "fptype" "double")])

(define_insn "abssf2"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(abs:SF (match_operand:SF 1 "register_operand" "f")))]
  "TARGET_FPU"
  "fabss\t%1, %0"
  [(set_attr "type" "fpmove")])

(define_expand "sqrttf2"
  [(set (match_operand:TF 0 "nonimmediate_operand" "")
	(sqrt:TF (match_operand:TF 1 "general_operand" "")))]
  "TARGET_FPU && (TARGET_HARD_QUAD || TARGET_ARCH64)"
  "emit_tfmode_unop (SQRT, operands); DONE;")

(define_insn "*sqrttf2_hq"
  [(set (match_operand:TF 0 "register_operand" "=e")
	(sqrt:TF (match_operand:TF 1 "register_operand" "e")))]
  "TARGET_FPU && TARGET_HARD_QUAD"
  "fsqrtq\t%1, %0"
  [(set_attr "type" "fpsqrtd")])

(define_insn "sqrtdf2"
  [(set (match_operand:DF 0 "register_operand" "=e")
	(sqrt:DF (match_operand:DF 1 "register_operand" "e")))]
  "TARGET_FPU"
  "fsqrtd\t%1, %0"
  [(set_attr "type" "fpsqrtd")
   (set_attr "fptype" "double")])

(define_insn "sqrtsf2"
  [(set (match_operand:SF 0 "register_operand" "=f")
	(sqrt:SF (match_operand:SF 1 "register_operand" "f")))]
  "TARGET_FPU"
  "fsqrts\t%1, %0"
  [(set_attr "type" "fpsqrts")])


;; Arithmetic shift instructions.

(define_insn "ashlsi3"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(ashift:SI (match_operand:SI 1 "register_operand" "r")
		   (match_operand:SI 2 "arith_operand" "rI")))]
  ""
{
  if (GET_CODE (operands[2]) == CONST_INT)
    operands[2] = GEN_INT (INTVAL (operands[2]) & 0x1f);
  return "sll\t%1, %2, %0";
}
  [(set (attr "type")
	(if_then_else (match_operand 2 "const_one_operand" "")
		      (const_string "ialu") (const_string "shift")))])

(define_expand "ashldi3"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(ashift:DI (match_operand:DI 1 "register_operand" "r")
		   (match_operand:SI 2 "arith_operand" "rI")))]
  "TARGET_ARCH64 || TARGET_V8PLUS"
{
  if (! TARGET_ARCH64)
    {
      if (GET_CODE (operands[2]) == CONST_INT)
	FAIL;
      emit_insn (gen_ashldi3_v8plus (operands[0], operands[1], operands[2]));
      DONE;
    }
})

(define_insn "*ashldi3_sp64"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(ashift:DI (match_operand:DI 1 "register_operand" "r")
		   (match_operand:SI 2 "arith_operand" "rI")))]
  "TARGET_ARCH64"
{
  if (GET_CODE (operands[2]) == CONST_INT)
    operands[2] = GEN_INT (INTVAL (operands[2]) & 0x3f);
  return "sllx\t%1, %2, %0";
}
  [(set (attr "type")
	(if_then_else (match_operand 2 "const_one_operand" "")
		      (const_string "ialu") (const_string "shift")))])

;; XXX UGH!
(define_insn "ashldi3_v8plus"
  [(set (match_operand:DI 0 "register_operand" "=&h,&h,r")
	(ashift:DI (match_operand:DI 1 "arith_operand" "rI,0,rI")
		   (match_operand:SI 2 "arith_operand" "rI,rI,rI")))
   (clobber (match_scratch:SI 3 "=X,X,&h"))]
  "TARGET_V8PLUS"
  "* return output_v8plus_shift (operands, insn, \"sllx\");"
  [(set_attr "type" "multi")
   (set_attr "length" "5,5,6")])

;; Optimize (1LL<<x)-1
;; XXX this also needs to be fixed to handle equal subregs
;; XXX first before we could re-enable it.
;(define_insn ""
;  [(set (match_operand:DI 0 "register_operand" "=h")
;	(plus:DI (ashift:DI (const_int 1)
;			    (match_operand:SI 1 "arith_operand" "rI"))
;		 (const_int -1)))]
;  "0 && TARGET_V8PLUS"
;{
;  if (GET_CODE (operands[1]) == REG && REGNO (operands[1]) == REGNO (operands[0]))
;    return "mov\t1, %L0\;sllx\t%L0, %1, %L0\;sub\t%L0, 1, %L0\;srlx\t%L0, 32, %H0";
;  return "mov\t1, %H0\;sllx\t%H0, %1, %L0\;sub\t%L0, 1, %L0\;srlx\t%L0, 32, %H0";
;}
;  [(set_attr "type" "multi")
;   (set_attr "length" "4")])

(define_insn "*cmp_cc_ashift_1"
  [(set (reg:CC_NOOV 100)
	(compare:CC_NOOV (ashift:SI (match_operand:SI 0 "register_operand" "r")
				    (const_int 1))
			 (const_int 0)))]
  ""
  "addcc\t%0, %0, %%g0"
  [(set_attr "type" "compare")])

(define_insn "*cmp_cc_set_ashift_1"
  [(set (reg:CC_NOOV 100)
	(compare:CC_NOOV (ashift:SI (match_operand:SI 1 "register_operand" "r")
				    (const_int 1))
			 (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=r")
	(ashift:SI (match_dup 1) (const_int 1)))]
  ""
  "addcc\t%1, %1, %0"
  [(set_attr "type" "compare")])

(define_insn "ashrsi3"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(ashiftrt:SI (match_operand:SI 1 "register_operand" "r")
		     (match_operand:SI 2 "arith_operand" "rI")))]
  ""
  {
     if (GET_CODE (operands[2]) == CONST_INT)
       operands[2] = GEN_INT (INTVAL (operands[2]) & 0x1f);
     return "sra\t%1, %2, %0";
  }
  [(set_attr "type" "shift")])

(define_insn "*ashrsi3_extend"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(sign_extend:DI (ashiftrt:SI (match_operand:SI 1 "register_operand" "r")
				     (match_operand:SI 2 "arith_operand" "r"))))]
  "TARGET_ARCH64"
  "sra\t%1, %2, %0"
  [(set_attr "type" "shift")])

;; This handles the case as above, but with constant shift instead of
;; register. Combiner "simplifies" it for us a little bit though.
(define_insn "*ashrsi3_extend2"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(ashiftrt:DI (ashift:DI (subreg:DI (match_operand:SI 1 "register_operand" "r") 0)
				(const_int 32))
		     (match_operand:SI 2 "small_int_operand" "I")))]
  "TARGET_ARCH64 && INTVAL (operands[2]) >= 32 && INTVAL (operands[2]) < 64"
{
  operands[2] = GEN_INT (INTVAL (operands[2]) - 32);
  return "sra\t%1, %2, %0";
}
  [(set_attr "type" "shift")])

(define_expand "ashrdi3"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(ashiftrt:DI (match_operand:DI 1 "register_operand" "r")
		     (match_operand:SI 2 "arith_operand" "rI")))]
  "TARGET_ARCH64 || TARGET_V8PLUS"
{
  if (! TARGET_ARCH64)
    {
      if (GET_CODE (operands[2]) == CONST_INT)
        FAIL;	/* prefer generic code in this case */
      emit_insn (gen_ashrdi3_v8plus (operands[0], operands[1], operands[2]));
      DONE;
    }
})

(define_insn "*ashrdi3_sp64"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(ashiftrt:DI (match_operand:DI 1 "register_operand" "r")
		     (match_operand:SI 2 "arith_operand" "rI")))]
  "TARGET_ARCH64"
  
  {
    if (GET_CODE (operands[2]) == CONST_INT)
      operands[2] = GEN_INT (INTVAL (operands[2]) & 0x3f);
    return "srax\t%1, %2, %0";
  }
  [(set_attr "type" "shift")])

;; XXX
(define_insn "ashrdi3_v8plus"
  [(set (match_operand:DI 0 "register_operand" "=&h,&h,r")
	(ashiftrt:DI (match_operand:DI 1 "arith_operand" "rI,0,rI")
		     (match_operand:SI 2 "arith_operand" "rI,rI,rI")))
   (clobber (match_scratch:SI 3 "=X,X,&h"))]
  "TARGET_V8PLUS"
  "* return output_v8plus_shift (operands, insn, \"srax\");"
  [(set_attr "type" "multi")
   (set_attr "length" "5,5,6")])

(define_insn "lshrsi3"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(lshiftrt:SI (match_operand:SI 1 "register_operand" "r")
		     (match_operand:SI 2 "arith_operand" "rI")))]
  ""
  {
    if (GET_CODE (operands[2]) == CONST_INT)
      operands[2] = GEN_INT (INTVAL (operands[2]) & 0x1f);
    return "srl\t%1, %2, %0";
  }
  [(set_attr "type" "shift")])

;; This handles the case where
;; (zero_extend:DI (lshiftrt:SI (match_operand:SI) (match_operand:SI))),
;; but combiner "simplifies" it for us.
(define_insn "*lshrsi3_extend"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(and:DI (subreg:DI (lshiftrt:SI (match_operand:SI 1 "register_operand" "r")
			   (match_operand:SI 2 "arith_operand" "r")) 0)
		(match_operand 3 "const_int_operand" "")))]
  "TARGET_ARCH64 && (unsigned HOST_WIDE_INT) INTVAL (operands[3]) == 0xffffffff"
  "srl\t%1, %2, %0"
  [(set_attr "type" "shift")])

;; This handles the case where
;; (lshiftrt:DI (zero_extend:DI (match_operand:SI)) (const_int >=0 < 32))
;; but combiner "simplifies" it for us.
(define_insn "*lshrsi3_extend2"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(zero_extract:DI (subreg:DI (match_operand:SI 1 "register_operand" "r") 0)
			 (match_operand 2 "small_int_operand" "I")
			 (const_int 32)))]
  "TARGET_ARCH64 && (unsigned HOST_WIDE_INT) INTVAL (operands[2]) < 32"
{
  operands[2] = GEN_INT (32 - INTVAL (operands[2]));
  return "srl\t%1, %2, %0";
}
  [(set_attr "type" "shift")])

(define_expand "lshrdi3"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(lshiftrt:DI (match_operand:DI 1 "register_operand" "r")
		     (match_operand:SI 2 "arith_operand" "rI")))]
  "TARGET_ARCH64 || TARGET_V8PLUS"
{
  if (! TARGET_ARCH64)
    {
      if (GET_CODE (operands[2]) == CONST_INT)
        FAIL;
      emit_insn (gen_lshrdi3_v8plus (operands[0], operands[1], operands[2]));
      DONE;
    }
})

(define_insn "*lshrdi3_sp64"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(lshiftrt:DI (match_operand:DI 1 "register_operand" "r")
		     (match_operand:SI 2 "arith_operand" "rI")))]
  "TARGET_ARCH64"
  {
    if (GET_CODE (operands[2]) == CONST_INT)
      operands[2] = GEN_INT (INTVAL (operands[2]) & 0x3f);
    return "srlx\t%1, %2, %0";
  }
  [(set_attr "type" "shift")])

;; XXX
(define_insn "lshrdi3_v8plus"
  [(set (match_operand:DI 0 "register_operand" "=&h,&h,r")
	(lshiftrt:DI (match_operand:DI 1 "arith_operand" "rI,0,rI")
		     (match_operand:SI 2 "arith_operand" "rI,rI,rI")))
   (clobber (match_scratch:SI 3 "=X,X,&h"))]
  "TARGET_V8PLUS"
  "* return output_v8plus_shift (operands, insn, \"srlx\");"
  [(set_attr "type" "multi")
   (set_attr "length" "5,5,6")])

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r")
	(ashiftrt:SI (subreg:SI (lshiftrt:DI (match_operand:DI 1 "register_operand" "r")
					     (const_int 32)) 4)
		     (match_operand:SI 2 "small_int_operand" "I")))]
  "TARGET_ARCH64 && (unsigned HOST_WIDE_INT) INTVAL (operands[2]) < 32"
{
  operands[2] = GEN_INT (INTVAL (operands[2]) + 32);
  return "srax\t%1, %2, %0";
}
  [(set_attr "type" "shift")])

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r")
	(lshiftrt:SI (subreg:SI (ashiftrt:DI (match_operand:DI 1 "register_operand" "r")
					     (const_int 32)) 4)
		     (match_operand:SI 2 "small_int_operand" "I")))]
  "TARGET_ARCH64 && (unsigned HOST_WIDE_INT) INTVAL (operands[2]) < 32"
{
  operands[2] = GEN_INT (INTVAL (operands[2]) + 32);
  return "srlx\t%1, %2, %0";
}
  [(set_attr "type" "shift")])

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r")
	(ashiftrt:SI (subreg:SI (ashiftrt:DI (match_operand:DI 1 "register_operand" "r")
					     (match_operand:SI 2 "small_int_operand" "I")) 4)
		     (match_operand:SI 3 "small_int_operand" "I")))]
  "TARGET_ARCH64
   && (unsigned HOST_WIDE_INT) INTVAL (operands[2]) >= 32
   && (unsigned HOST_WIDE_INT) INTVAL (operands[3]) < 32
   && (unsigned HOST_WIDE_INT) (INTVAL (operands[2]) + INTVAL (operands[3])) < 64"
{
  operands[2] = GEN_INT (INTVAL (operands[2]) + INTVAL (operands[3]));

  return "srax\t%1, %2, %0";
}
  [(set_attr "type" "shift")])

(define_insn ""
  [(set (match_operand:SI 0 "register_operand" "=r")
	(lshiftrt:SI (subreg:SI (lshiftrt:DI (match_operand:DI 1 "register_operand" "r")
					     (match_operand:SI 2 "small_int_operand" "I")) 4)
		     (match_operand:SI 3 "small_int_operand" "I")))]
  "TARGET_ARCH64
   && (unsigned HOST_WIDE_INT) INTVAL (operands[2]) >= 32
   && (unsigned HOST_WIDE_INT) INTVAL (operands[3]) < 32
   && (unsigned HOST_WIDE_INT) (INTVAL (operands[2]) + INTVAL (operands[3])) < 64"
{
  operands[2] = GEN_INT (INTVAL (operands[2]) + INTVAL (operands[3]));

  return "srlx\t%1, %2, %0";
}
  [(set_attr "type" "shift")])


;; Unconditional and other jump instructions.

(define_insn "jump"
  [(set (pc) (label_ref (match_operand 0 "" "")))]
  ""
  "* return output_ubranch (operands[0], 0, insn);"
  [(set_attr "type" "uncond_branch")])

(define_expand "tablejump"
  [(parallel [(set (pc) (match_operand 0 "register_operand" "r"))
	      (use (label_ref (match_operand 1 "" "")))])]
  ""
{
  gcc_assert (GET_MODE (operands[0]) == CASE_VECTOR_MODE);

  /* In pic mode, our address differences are against the base of the
     table.  Add that base value back in; CSE ought to be able to combine
     the two address loads.  */
  if (flag_pic)
    {
      rtx tmp, tmp2;
      tmp = gen_rtx_LABEL_REF (Pmode, operands[1]);
      tmp2 = operands[0];
      if (CASE_VECTOR_MODE != Pmode)
        tmp2 = gen_rtx_SIGN_EXTEND (Pmode, tmp2);
      tmp = gen_rtx_PLUS (Pmode, tmp2, tmp);
      operands[0] = memory_address (Pmode, tmp);
    }
})

(define_insn "*tablejump_sp32"
  [(set (pc) (match_operand:SI 0 "address_operand" "p"))
   (use (label_ref (match_operand 1 "" "")))]
  "! TARGET_ARCH64"
  "jmp\t%a0%#"
  [(set_attr "type" "uncond_branch")])

(define_insn "*tablejump_sp64"
  [(set (pc) (match_operand:DI 0 "address_operand" "p"))
   (use (label_ref (match_operand 1 "" "")))]
  "TARGET_ARCH64"
  "jmp\t%a0%#"
  [(set_attr "type" "uncond_branch")])


;; Jump to subroutine instructions.

(define_expand "call"
  ;; Note that this expression is not used for generating RTL.
  ;; All the RTL is generated explicitly below.
  [(call (match_operand 0 "call_operand" "")
	 (match_operand 3 "" "i"))]
  ;; operands[2] is next_arg_register
  ;; operands[3] is struct_value_size_rtx.
  ""
{
  rtx fn_rtx;

  gcc_assert (GET_MODE (operands[0]) == FUNCTION_MODE);

  gcc_assert (GET_CODE (operands[3]) == CONST_INT);

  if (GET_CODE (XEXP (operands[0], 0)) == LABEL_REF)
    {
      /* This is really a PIC sequence.  We want to represent
	 it as a funny jump so its delay slots can be filled. 

	 ??? But if this really *is* a CALL, will not it clobber the
	 call-clobbered registers?  We lose this if it is a JUMP_INSN.
	 Why cannot we have delay slots filled if it were a CALL?  */

      /* We accept negative sizes for untyped calls.  */
      if (! TARGET_ARCH64 && INTVAL (operands[3]) != 0)
	emit_jump_insn
	  (gen_rtx_PARALLEL
	   (VOIDmode,
	    gen_rtvec (3,
		       gen_rtx_SET (VOIDmode, pc_rtx, XEXP (operands[0], 0)),
		       operands[3],
		       gen_rtx_CLOBBER (VOIDmode, gen_rtx_REG (Pmode, 15)))));
      else
	emit_jump_insn
	  (gen_rtx_PARALLEL
	   (VOIDmode,
	    gen_rtvec (2,
		       gen_rtx_SET (VOIDmode, pc_rtx, XEXP (operands[0], 0)),
		       gen_rtx_CLOBBER (VOIDmode, gen_rtx_REG (Pmode, 15)))));
      goto finish_call;
    }

  fn_rtx = operands[0];

  /* We accept negative sizes for untyped calls.  */
  if (! TARGET_ARCH64 && INTVAL (operands[3]) != 0)
    emit_call_insn
      (gen_rtx_PARALLEL
       (VOIDmode,
	gen_rtvec (3, gen_rtx_CALL (VOIDmode, fn_rtx, const0_rtx),
		   operands[3],
		   gen_rtx_CLOBBER (VOIDmode, gen_rtx_REG (Pmode, 15)))));
  else
    emit_call_insn
      (gen_rtx_PARALLEL
       (VOIDmode,
	gen_rtvec (2, gen_rtx_CALL (VOIDmode, fn_rtx, const0_rtx),
		   gen_rtx_CLOBBER (VOIDmode, gen_rtx_REG (Pmode, 15)))));

 finish_call:

  DONE;
})

;; We can't use the same pattern for these two insns, because then registers
;; in the address may not be properly reloaded.

(define_insn "*call_address_sp32"
  [(call (mem:SI (match_operand:SI 0 "address_operand" "p"))
	 (match_operand 1 "" ""))
   (clobber (reg:SI 15))]
  ;;- Do not use operand 1 for most machines.
  "! TARGET_ARCH64"
  "call\t%a0, %1%#"
  [(set_attr "type" "call")])

(define_insn "*call_symbolic_sp32"
  [(call (mem:SI (match_operand:SI 0 "symbolic_operand" "s"))
	 (match_operand 1 "" ""))
   (clobber (reg:SI 15))]
  ;;- Do not use operand 1 for most machines.
  "! TARGET_ARCH64"
  "call\t%a0, %1%#"
  [(set_attr "type" "call")])

(define_insn "*call_address_sp64"
  [(call (mem:DI (match_operand:DI 0 "address_operand" "p"))
	 (match_operand 1 "" ""))
   (clobber (reg:DI 15))]
  ;;- Do not use operand 1 for most machines.
  "TARGET_ARCH64"
  "call\t%a0, %1%#"
  [(set_attr "type" "call")])

(define_insn "*call_symbolic_sp64"
  [(call (mem:DI (match_operand:DI 0 "symbolic_operand" "s"))
	 (match_operand 1 "" ""))
   (clobber (reg:DI 15))]
  ;;- Do not use operand 1 for most machines.
  "TARGET_ARCH64"
  "call\t%a0, %1%#"
  [(set_attr "type" "call")])

;; This is a call that wants a structure value.
;; There is no such critter for v9 (??? we may need one anyway).
(define_insn "*call_address_struct_value_sp32"
  [(call (mem:SI (match_operand:SI 0 "address_operand" "p"))
	 (match_operand 1 "" ""))
   (match_operand 2 "immediate_operand" "")
   (clobber (reg:SI 15))]
  ;;- Do not use operand 1 for most machines.
  "! TARGET_ARCH64 && GET_CODE (operands[2]) == CONST_INT && INTVAL (operands[2]) > 0"
{
  operands[2] = GEN_INT (INTVAL (operands[2]) & 0xfff);
  return "call\t%a0, %1\n\t nop\n\tunimp\t%2";
}
  [(set_attr "type" "call_no_delay_slot")
   (set_attr "length" "3")])

;; This is a call that wants a structure value.
;; There is no such critter for v9 (??? we may need one anyway).
(define_insn "*call_symbolic_struct_value_sp32"
  [(call (mem:SI (match_operand:SI 0 "symbolic_operand" "s"))
	 (match_operand 1 "" ""))
   (match_operand 2 "immediate_operand" "")
   (clobber (reg:SI 15))]
  ;;- Do not use operand 1 for most machines.
  "! TARGET_ARCH64 && GET_CODE (operands[2]) == CONST_INT && INTVAL (operands[2]) > 0"
{
  operands[2] = GEN_INT (INTVAL (operands[2]) & 0xfff);
  return "call\t%a0, %1\n\t nop\n\tunimp\t%2";
}
  [(set_attr "type" "call_no_delay_slot")
   (set_attr "length" "3")])

;; This is a call that may want a structure value.  This is used for
;; untyped_calls.
(define_insn "*call_address_untyped_struct_value_sp32"
  [(call (mem:SI (match_operand:SI 0 "address_operand" "p"))
	 (match_operand 1 "" ""))
   (match_operand 2 "immediate_operand" "")
   (clobber (reg:SI 15))]
  ;;- Do not use operand 1 for most machines.
  "! TARGET_ARCH64 && GET_CODE (operands[2]) == CONST_INT && INTVAL (operands[2]) < 0"
  "call\t%a0, %1\n\t nop\n\tnop"
  [(set_attr "type" "call_no_delay_slot")
   (set_attr "length" "3")])

;; This is a call that may want a structure value.  This is used for
;; untyped_calls.
(define_insn "*call_symbolic_untyped_struct_value_sp32"
  [(call (mem:SI (match_operand:SI 0 "symbolic_operand" "s"))
	 (match_operand 1 "" ""))
   (match_operand 2 "immediate_operand" "")
   (clobber (reg:SI 15))]
  ;;- Do not use operand 1 for most machines.
  "! TARGET_ARCH64 && GET_CODE (operands[2]) == CONST_INT && INTVAL (operands[2]) < 0"
  "call\t%a0, %1\n\t nop\n\tnop"
  [(set_attr "type" "call_no_delay_slot")
   (set_attr "length" "3")])

(define_expand "call_value"
  ;; Note that this expression is not used for generating RTL.
  ;; All the RTL is generated explicitly below.
  [(set (match_operand 0 "register_operand" "=rf")
	(call (match_operand 1 "" "")
	      (match_operand 4 "" "")))]
  ;; operand 2 is stack_size_rtx
  ;; operand 3 is next_arg_register
  ""
{
  rtx fn_rtx;
  rtvec vec;

  gcc_assert (GET_MODE (operands[1]) == FUNCTION_MODE);

  fn_rtx = operands[1];

  vec = gen_rtvec (2,
		   gen_rtx_SET (VOIDmode, operands[0],
				gen_rtx_CALL (VOIDmode, fn_rtx, const0_rtx)),
		   gen_rtx_CLOBBER (VOIDmode, gen_rtx_REG (Pmode, 15)));

  emit_call_insn (gen_rtx_PARALLEL (VOIDmode, vec));

  DONE;
})

(define_insn "*call_value_address_sp32"
  [(set (match_operand 0 "" "=rf")
	(call (mem:SI (match_operand:SI 1 "address_operand" "p"))
	      (match_operand 2 "" "")))
   (clobber (reg:SI 15))]
  ;;- Do not use operand 2 for most machines.
  "! TARGET_ARCH64"
  "call\t%a1, %2%#"
  [(set_attr "type" "call")])

(define_insn "*call_value_symbolic_sp32"
  [(set (match_operand 0 "" "=rf")
	(call (mem:SI (match_operand:SI 1 "symbolic_operand" "s"))
	      (match_operand 2 "" "")))
   (clobber (reg:SI 15))]
  ;;- Do not use operand 2 for most machines.
  "! TARGET_ARCH64"
  "call\t%a1, %2%#"
  [(set_attr "type" "call")])

(define_insn "*call_value_address_sp64"
  [(set (match_operand 0 "" "")
	(call (mem:DI (match_operand:DI 1 "address_operand" "p"))
	      (match_operand 2 "" "")))
   (clobber (reg:DI 15))]
  ;;- Do not use operand 2 for most machines.
  "TARGET_ARCH64"
  "call\t%a1, %2%#"
  [(set_attr "type" "call")])

(define_insn "*call_value_symbolic_sp64"
  [(set (match_operand 0 "" "")
	(call (mem:DI (match_operand:DI 1 "symbolic_operand" "s"))
	      (match_operand 2 "" "")))
   (clobber (reg:DI 15))]
  ;;- Do not use operand 2 for most machines.
  "TARGET_ARCH64"
  "call\t%a1, %2%#"
  [(set_attr "type" "call")])

(define_expand "untyped_call"
  [(parallel [(call (match_operand 0 "" "")
		    (const_int 0))
	      (match_operand:BLK 1 "memory_operand" "")
	      (match_operand 2 "" "")])]
  ""
{
  rtx valreg1 = gen_rtx_REG (DImode, 8);
  rtx valreg2 = gen_rtx_REG (TARGET_ARCH64 ? TFmode : DFmode, 32);
  rtx result = operands[1];

  /* Pass constm1 to indicate that it may expect a structure value, but
     we don't know what size it is.  */
  emit_call_insn (GEN_CALL (operands[0], const0_rtx, NULL, constm1_rtx));

  /* Save the function value registers.  */
  emit_move_insn (adjust_address (result, DImode, 0), valreg1);
  emit_move_insn (adjust_address (result, TARGET_ARCH64 ? TFmode : DFmode, 8),
				  valreg2);

  /* The optimizer does not know that the call sets the function value
     registers we stored in the result block.  We avoid problems by
     claiming that all hard registers are used and clobbered at this
     point.  */
  emit_insn (gen_blockage ());

  DONE;
})

;;  Tail call instructions.

(define_expand "sibcall"
  [(parallel [(call (match_operand 0 "call_operand" "") (const_int 0))
	      (return)])]
  ""
  "")

(define_insn "*sibcall_symbolic_sp32"
  [(call (mem:SI (match_operand:SI 0 "symbolic_operand" "s"))
	 (match_operand 1 "" ""))
   (return)]
  "! TARGET_ARCH64"
  "* return output_sibcall(insn, operands[0]);"
  [(set_attr "type" "sibcall")])

(define_insn "*sibcall_symbolic_sp64"
  [(call (mem:DI (match_operand:DI 0 "symbolic_operand" "s"))
	 (match_operand 1 "" ""))
   (return)]
  "TARGET_ARCH64"
  "* return output_sibcall(insn, operands[0]);"
  [(set_attr "type" "sibcall")])

(define_expand "sibcall_value"
  [(parallel [(set (match_operand 0 "register_operand" "=rf")
		(call (match_operand 1 "" "") (const_int 0)))
	      (return)])]
  ""
  "")

(define_insn "*sibcall_value_symbolic_sp32"
  [(set (match_operand 0 "" "=rf")
	(call (mem:SI (match_operand:SI 1 "symbolic_operand" "s"))
	      (match_operand 2 "" "")))
   (return)]
  "! TARGET_ARCH64"
  "* return output_sibcall(insn, operands[1]);"
  [(set_attr "type" "sibcall")])

(define_insn "*sibcall_value_symbolic_sp64"
  [(set (match_operand 0 "" "")
	(call (mem:DI (match_operand:DI 1 "symbolic_operand" "s"))
	      (match_operand 2 "" "")))
   (return)]
  "TARGET_ARCH64"
  "* return output_sibcall(insn, operands[1]);"
  [(set_attr "type" "sibcall")])


;; Special instructions.

(define_expand "prologue"
  [(const_int 0)]
  ""
{
  sparc_expand_prologue ();
  DONE;
})

;; The "save register window" insn is modelled as follows so that the DWARF-2
;; backend automatically emits the required call frame debugging information
;; while it is parsing it.  Therefore, the pattern should not be modified
;; without first studying the impact of the changes on the debug info.
;; [(set (%fp) (%sp))
;;  (set (%sp) (unspec_volatile [(%sp) (-frame_size)] UNSPECV_SAVEW))
;;  (set (%i7) (%o7))]

(define_insn "save_register_window<P:mode>"
  [(set (reg:P 30) (reg:P 14))
   (set (reg:P 14) (unspec_volatile:P [(reg:P 14)
				       (match_operand:P 0 "arith_operand" "rI")] UNSPECV_SAVEW))
   (set (reg:P 31) (reg:P 15))]
  ""
  "save\t%%sp, %0, %%sp"
  [(set_attr "type" "savew")])

(define_expand "epilogue"
  [(return)]
  ""
{
  sparc_expand_epilogue ();
})

(define_expand "sibcall_epilogue"
  [(return)]
  ""
{
  sparc_expand_epilogue ();
  DONE;
})

(define_expand "return"
  [(return)]
  "sparc_can_use_return_insn_p ()"
  "")

(define_insn "*return_internal"
  [(return)]
  ""
  "* return output_return (insn);"
  [(set_attr "type" "return")
   (set (attr "length")
	(cond [(eq_attr "leaf_function" "true")
		 (if_then_else (eq_attr "empty_delay_slot" "true")
			       (const_int 2)
			       (const_int 1))
	       (eq_attr "calls_eh_return" "true")
		 (if_then_else (eq_attr "delayed_branch" "true")
			       (if_then_else (eq_attr "isa" "v9")
					     (const_int 2)
					     (const_int 3))
			       (if_then_else (eq_attr "isa" "v9")
					     (const_int 3)
					     (const_int 4)))
	       (eq_attr "empty_delay_slot" "true")
		 (if_then_else (eq_attr "delayed_branch" "true")
			       (const_int 2)
			       (const_int 3))
	      ] (const_int 1)))])

;; UNSPEC_VOLATILE is considered to use and clobber all hard registers and
;; all of memory.  This blocks insns from being moved across this point.

(define_insn "blockage"
  [(unspec_volatile [(const_int 0)] UNSPECV_BLOCKAGE)]
  ""
  ""
  [(set_attr "length" "0")])

;; Prepare to return any type including a structure value.

(define_expand "untyped_return"
  [(match_operand:BLK 0 "memory_operand" "")
   (match_operand 1 "" "")]
  ""
{
  rtx valreg1 = gen_rtx_REG (DImode, 24);
  rtx valreg2 = gen_rtx_REG (TARGET_ARCH64 ? TFmode : DFmode, 32);
  rtx result = operands[0];

  if (! TARGET_ARCH64)
    {
      rtx rtnreg = gen_rtx_REG (SImode, (current_function_uses_only_leaf_regs
					 ? 15 : 31));
      rtx value = gen_reg_rtx (SImode);

      /* Fetch the instruction where we will return to and see if it's an unimp
	 instruction (the most significant 10 bits will be zero).  If so,
	 update the return address to skip the unimp instruction.  */
      emit_move_insn (value,
		      gen_rtx_MEM (SImode, plus_constant (rtnreg, 8)));
      emit_insn (gen_lshrsi3 (value, value, GEN_INT (22)));
      emit_insn (gen_update_return (rtnreg, value));
    }

  /* Reload the function value registers.  */
  emit_move_insn (valreg1, adjust_address (result, DImode, 0));
  emit_move_insn (valreg2,
		  adjust_address (result, TARGET_ARCH64 ? TFmode : DFmode, 8));

  /* Put USE insns before the return.  */
  emit_insn (gen_rtx_USE (VOIDmode, valreg1));
  emit_insn (gen_rtx_USE (VOIDmode, valreg2));

  /* Construct the return.  */
  expand_naked_return ();

  DONE;
})

;; Adjust the return address conditionally. If the value of op1 is equal
;; to all zero then adjust the return address i.e. op0 = op0 + 4.
;; This is technically *half* the check required by the 32-bit SPARC
;; psABI. This check only ensures that an "unimp" insn was written by
;; the caller, but doesn't check to see if the expected size matches
;; (this is encoded in the 12 lower bits). This check is obsolete and
;; only used by the above code "untyped_return".

(define_insn "update_return"
  [(unspec:SI [(match_operand:SI 0 "register_operand" "r")
	       (match_operand:SI 1 "register_operand" "r")] UNSPEC_UPDATE_RETURN)]
  "! TARGET_ARCH64"
{
  if (flag_delayed_branch)
    return "cmp\t%1, 0\n\tbe,a\t.+8\n\t add\t%0, 4, %0";
  else
    return "cmp\t%1, 0\n\tbne\t.+12\n\t nop\n\tadd\t%0, 4, %0";
}
  [(set (attr "type") (const_string "multi"))
   (set (attr "length")
	(if_then_else (eq_attr "delayed_branch" "true")
		      (const_int 3)
		      (const_int 4)))])

(define_insn "nop"
  [(const_int 0)]
  ""
  "nop")

(define_expand "indirect_jump"
  [(set (pc) (match_operand 0 "address_operand" "p"))]
  ""
  "")

(define_insn "*branch_sp32"
  [(set (pc) (match_operand:SI 0 "address_operand" "p"))]
  "! TARGET_ARCH64"
 "jmp\t%a0%#"
 [(set_attr "type" "uncond_branch")])
 
(define_insn "*branch_sp64"
  [(set (pc) (match_operand:DI 0 "address_operand" "p"))]
  "TARGET_ARCH64"
  "jmp\t%a0%#"
  [(set_attr "type" "uncond_branch")])

(define_expand "nonlocal_goto"
  [(match_operand:SI 0 "general_operand" "")
   (match_operand:SI 1 "general_operand" "")
   (match_operand:SI 2 "general_operand" "")
   (match_operand:SI 3 "" "")]
  ""
{
  rtx lab = operands[1];
  rtx stack = operands[2];
  rtx fp = operands[3];
  rtx labreg;

  /* Trap instruction to flush all the register windows.  */
  emit_insn (gen_flush_register_windows ());

  /* Load the fp value for the containing fn into %fp.  This is needed
     because STACK refers to %fp.  Note that virtual register instantiation
     fails if the virtual %fp isn't set from a register.  */
  if (GET_CODE (fp) != REG)
    fp = force_reg (Pmode, fp);
  emit_move_insn (virtual_stack_vars_rtx, fp);

  /* Find the containing function's current nonlocal goto handler,
     which will do any cleanups and then jump to the label.  */
  labreg = gen_rtx_REG (Pmode, 8);
  emit_move_insn (labreg, lab);

  /* Restore %fp from stack pointer value for containing function.
     The restore insn that follows will move this to %sp,
     and reload the appropriate value into %fp.  */
  emit_move_insn (hard_frame_pointer_rtx, stack);

  emit_insn (gen_rtx_USE (VOIDmode, stack_pointer_rtx));
  emit_insn (gen_rtx_USE (VOIDmode, static_chain_rtx));

  /* ??? The V9-specific version was disabled in rev 1.65.  */
  emit_jump_insn (gen_goto_handler_and_restore (labreg));
  emit_barrier ();
  DONE;
})

;; Special trap insn to flush register windows.
(define_insn "flush_register_windows"
  [(unspec_volatile [(const_int 0)] UNSPECV_FLUSHW)]
  ""
  { return TARGET_V9 ? "flushw" : "ta\t3"; }
  [(set_attr "type" "flushw")])

(define_insn "goto_handler_and_restore"
  [(unspec_volatile [(match_operand 0 "register_operand" "=r")] UNSPECV_GOTO)]
  "GET_MODE (operands[0]) == Pmode"
{
  if (flag_delayed_branch)
    return "jmp\t%0\n\t restore";
  else
    return "mov\t%0,%%g1\n\trestore\n\tjmp\t%%g1\n\t nop";
}
  [(set (attr "type") (const_string "multi"))
   (set (attr "length")
	(if_then_else (eq_attr "delayed_branch" "true")
		      (const_int 2)
		      (const_int 4)))])

;; For __builtin_setjmp we need to flush register windows iff the function
;; calls alloca as well, because otherwise the register window might be
;; saved after %sp adjustment and thus setjmp would crash
(define_expand "builtin_setjmp_setup"
  [(match_operand 0 "register_operand" "r")]
  ""
{
  emit_insn (gen_do_builtin_setjmp_setup ());
  DONE;
})

(define_insn "do_builtin_setjmp_setup"
  [(unspec_volatile [(const_int 0)] UNSPECV_SETJMP)]
  ""
{
  if (! current_function_calls_alloca)
    return "";
  if (! TARGET_V9)
    return "\tta\t3\n";
  fputs ("\tflushw\n", asm_out_file);
  if (flag_pic)
    fprintf (asm_out_file, "\tst%c\t%%l7, [%%sp+%d]\n",
	     TARGET_ARCH64 ? 'x' : 'w',
	     SPARC_STACK_BIAS + 7 * UNITS_PER_WORD);
  fprintf (asm_out_file, "\tst%c\t%%fp, [%%sp+%d]\n",
	   TARGET_ARCH64 ? 'x' : 'w',
	   SPARC_STACK_BIAS + 14 * UNITS_PER_WORD);
  fprintf (asm_out_file, "\tst%c\t%%i7, [%%sp+%d]\n",
	   TARGET_ARCH64 ? 'x' : 'w',
	   SPARC_STACK_BIAS + 15 * UNITS_PER_WORD);
  return "";
}
  [(set_attr "type" "multi")
   (set (attr "length")
        (cond [(eq_attr "calls_alloca" "false")
                 (const_int 0)
               (eq_attr "isa" "!v9")
                 (const_int 1)
               (eq_attr "pic" "true")
                 (const_int 4)] (const_int 3)))])

;; Pattern for use after a setjmp to store FP and the return register
;; into the stack area.

(define_expand "setjmp"
  [(const_int 0)]
  ""
{
  rtx mem;
  
  mem = gen_rtx_MEM (Pmode,
		     plus_constant (stack_pointer_rtx,
				    SPARC_STACK_BIAS + 14 * UNITS_PER_WORD));
  emit_insn (gen_rtx_SET (VOIDmode, mem, frame_pointer_rtx));

  mem = gen_rtx_MEM (Pmode,
		     plus_constant (stack_pointer_rtx,
				    SPARC_STACK_BIAS + 15 * UNITS_PER_WORD));
  emit_insn (gen_rtx_SET (VOIDmode, mem, gen_rtx_REG (Pmode, 31)));
  DONE;
})

;; Special pattern for the FLUSH instruction.

; We do SImode and DImode versions of this to quiet down genrecog's complaints
; of the define_insn otherwise missing a mode.  We make "flush", aka
; gen_flush, the default one since sparc_initialize_trampoline uses
; it on SImode mem values.

(define_insn "flush"
  [(unspec_volatile [(match_operand:SI 0 "memory_operand" "m")] UNSPECV_FLUSH)]
  ""
  { return TARGET_V9 ? "flush\t%f0" : "iflush\t%f0"; }
  [(set_attr "type" "iflush")])

(define_insn "flushdi"
  [(unspec_volatile [(match_operand:DI 0 "memory_operand" "m")] UNSPECV_FLUSH)]
  ""
  { return TARGET_V9 ? "flush\t%f0" : "iflush\t%f0"; }
  [(set_attr "type" "iflush")])


;; Find first set instructions.

;; The scan instruction searches from the most significant bit while ffs
;; searches from the least significant bit.  The bit index and treatment of
;; zero also differ.  It takes at least 7 instructions to get the proper
;; result.  Here is an obvious 8 instruction sequence.

;; XXX
(define_insn "ffssi2"
  [(set (match_operand:SI 0 "register_operand" "=&r")
	(ffs:SI (match_operand:SI 1 "register_operand" "r")))
   (clobber (match_scratch:SI 2 "=&r"))]
  "TARGET_SPARCLITE || TARGET_SPARCLET"
{
  return "sub\t%%g0, %1, %0\;and\t%0, %1, %0\;scan\t%0, 0, %0\;mov\t32, %2\;sub\t%2, %0, %0\;sra\t%0, 31, %2\;and\t%2, 31, %2\;add\t%2, %0, %0";
}
  [(set_attr "type" "multi")
   (set_attr "length" "8")])

;; ??? This should be a define expand, so that the extra instruction have
;; a chance of being optimized away.

;; Disabled because none of the UltraSPARCs implement popc.  The HAL R1
;; does, but no one uses that and we don't have a switch for it.
;
;(define_insn "ffsdi2"
;  [(set (match_operand:DI 0 "register_operand" "=&r")
;	(ffs:DI (match_operand:DI 1 "register_operand" "r")))
;   (clobber (match_scratch:DI 2 "=&r"))]
;  "TARGET_ARCH64"
;  "neg\t%1, %2\;xnor\t%1, %2, %2\;popc\t%2, %0\;movzr\t%1, 0, %0"
;  [(set_attr "type" "multi")
;   (set_attr "length" "4")])



;; Peepholes go at the end.

;; Optimize consecutive loads or stores into ldd and std when possible.
;; The conditions in which we do this are very restricted and are 
;; explained in the code for {registers,memory}_ok_for_ldd functions.

(define_peephole2
  [(set (match_operand:SI 0 "memory_operand" "")
      (const_int 0))
   (set (match_operand:SI 1 "memory_operand" "")
      (const_int 0))]
  "TARGET_V9
   && mems_ok_for_ldd_peep (operands[0], operands[1], NULL_RTX)"
  [(set (match_dup 0)
       (const_int 0))]
  "operands[0] = widen_memory_access (operands[0], DImode, 0);")

(define_peephole2
  [(set (match_operand:SI 0 "memory_operand" "")
      (const_int 0))
   (set (match_operand:SI 1 "memory_operand" "")
      (const_int 0))]
  "TARGET_V9
   && mems_ok_for_ldd_peep (operands[1], operands[0], NULL_RTX)"
  [(set (match_dup 1)
       (const_int 0))]
  "operands[1] = widen_memory_access (operands[1], DImode, 0);")

(define_peephole2
  [(set (match_operand:SI 0 "register_operand" "")
        (match_operand:SI 1 "memory_operand" ""))
   (set (match_operand:SI 2 "register_operand" "")
        (match_operand:SI 3 "memory_operand" ""))]
  "registers_ok_for_ldd_peep (operands[0], operands[2]) 
   && mems_ok_for_ldd_peep (operands[1], operands[3], operands[0])" 
  [(set (match_dup 0)
	(match_dup 1))]
  "operands[1] = widen_memory_access (operands[1], DImode, 0);
   operands[0] = gen_rtx_REG (DImode, REGNO (operands[0]));")

(define_peephole2
  [(set (match_operand:SI 0 "memory_operand" "")
        (match_operand:SI 1 "register_operand" ""))
   (set (match_operand:SI 2 "memory_operand" "")
        (match_operand:SI 3 "register_operand" ""))]
  "registers_ok_for_ldd_peep (operands[1], operands[3]) 
   && mems_ok_for_ldd_peep (operands[0], operands[2], NULL_RTX)"
  [(set (match_dup 0)
	(match_dup 1))]
  "operands[0] = widen_memory_access (operands[0], DImode, 0);
   operands[1] = gen_rtx_REG (DImode, REGNO (operands[1]));")

(define_peephole2
  [(set (match_operand:SF 0 "register_operand" "")
        (match_operand:SF 1 "memory_operand" ""))
   (set (match_operand:SF 2 "register_operand" "")
        (match_operand:SF 3 "memory_operand" ""))]
  "registers_ok_for_ldd_peep (operands[0], operands[2]) 
   && mems_ok_for_ldd_peep (operands[1], operands[3], operands[0])"
  [(set (match_dup 0)
	(match_dup 1))]
  "operands[1] = widen_memory_access (operands[1], DFmode, 0);
   operands[0] = gen_rtx_REG (DFmode, REGNO (operands[0]));")

(define_peephole2
  [(set (match_operand:SF 0 "memory_operand" "")
        (match_operand:SF 1 "register_operand" ""))
   (set (match_operand:SF 2 "memory_operand" "")
        (match_operand:SF 3 "register_operand" ""))]
  "registers_ok_for_ldd_peep (operands[1], operands[3]) 
  && mems_ok_for_ldd_peep (operands[0], operands[2], NULL_RTX)"
  [(set (match_dup 0)
	(match_dup 1))]
  "operands[0] = widen_memory_access (operands[0], DFmode, 0);
   operands[1] = gen_rtx_REG (DFmode, REGNO (operands[1]));")

(define_peephole2
  [(set (match_operand:SI 0 "register_operand" "")
        (match_operand:SI 1 "memory_operand" ""))
   (set (match_operand:SI 2 "register_operand" "")
        (match_operand:SI 3 "memory_operand" ""))]
  "registers_ok_for_ldd_peep (operands[2], operands[0]) 
  && mems_ok_for_ldd_peep (operands[3], operands[1], operands[0])"
  [(set (match_dup 2)
	(match_dup 3))]
   "operands[3] = widen_memory_access (operands[3], DImode, 0);
    operands[2] = gen_rtx_REG (DImode, REGNO (operands[2]));")

(define_peephole2
  [(set (match_operand:SI 0 "memory_operand" "")
        (match_operand:SI 1 "register_operand" ""))
   (set (match_operand:SI 2 "memory_operand" "")
        (match_operand:SI 3 "register_operand" ""))]
  "registers_ok_for_ldd_peep (operands[3], operands[1]) 
  && mems_ok_for_ldd_peep (operands[2], operands[0], NULL_RTX)" 
  [(set (match_dup 2)
	(match_dup 3))]
  "operands[2] = widen_memory_access (operands[2], DImode, 0);
   operands[3] = gen_rtx_REG (DImode, REGNO (operands[3]));
   ")
 
(define_peephole2
  [(set (match_operand:SF 0 "register_operand" "")
        (match_operand:SF 1 "memory_operand" ""))
   (set (match_operand:SF 2 "register_operand" "")
        (match_operand:SF 3 "memory_operand" ""))]
  "registers_ok_for_ldd_peep (operands[2], operands[0]) 
  && mems_ok_for_ldd_peep (operands[3], operands[1], operands[0])"
  [(set (match_dup 2)
	(match_dup 3))]
  "operands[3] = widen_memory_access (operands[3], DFmode, 0);
   operands[2] = gen_rtx_REG (DFmode, REGNO (operands[2]));")

(define_peephole2
  [(set (match_operand:SF 0 "memory_operand" "")
        (match_operand:SF 1 "register_operand" ""))
   (set (match_operand:SF 2 "memory_operand" "")
        (match_operand:SF 3 "register_operand" ""))]
  "registers_ok_for_ldd_peep (operands[3], operands[1]) 
  && mems_ok_for_ldd_peep (operands[2], operands[0], NULL_RTX)"
  [(set (match_dup 2)
	(match_dup 3))]
  "operands[2] = widen_memory_access (operands[2], DFmode, 0);
   operands[3] = gen_rtx_REG (DFmode, REGNO (operands[3]));")
 
;; Optimize the case of following a reg-reg move with a test
;; of reg just moved.  Don't allow floating point regs for operand 0 or 1.
;; This can result from a float to fix conversion.

(define_peephole2
  [(set (match_operand:SI 0 "register_operand" "")
	(match_operand:SI 1 "register_operand" ""))
   (set (reg:CC 100)
	(compare:CC (match_operand:SI 2 "register_operand" "")
		    (const_int 0)))]
  "(rtx_equal_p (operands[2], operands[0])
    || rtx_equal_p (operands[2], operands[1]))
    && ! SPARC_FP_REG_P (REGNO (operands[0]))
    && ! SPARC_FP_REG_P (REGNO (operands[1]))"
  [(parallel [(set (match_dup 0) (match_dup 1))
	      (set (reg:CC 100)
		   (compare:CC (match_dup 1) (const_int 0)))])]
  "")

(define_peephole2
  [(set (match_operand:DI 0 "register_operand" "")
	(match_operand:DI 1 "register_operand" ""))
   (set (reg:CCX 100)
	(compare:CCX (match_operand:DI 2 "register_operand" "")
		    (const_int 0)))]
  "TARGET_ARCH64
   && (rtx_equal_p (operands[2], operands[0])
       || rtx_equal_p (operands[2], operands[1]))
   && ! SPARC_FP_REG_P (REGNO (operands[0]))
   && ! SPARC_FP_REG_P (REGNO (operands[1]))"
  [(parallel [(set (match_dup 0) (match_dup 1))
	      (set (reg:CCX 100)
		   (compare:CCX (match_dup 1) (const_int 0)))])]
  "")


;; Prefetch instructions.

;; ??? UltraSPARC-III note: A memory operation loading into the floating point register
;; ??? file, if it hits the prefetch cache, has a chance to dual-issue with other memory
;; ??? operations.  With DFA we might be able to model this, but it requires a lot of
;; ??? state.
(define_expand "prefetch"
  [(match_operand 0 "address_operand" "")
   (match_operand 1 "const_int_operand" "")
   (match_operand 2 "const_int_operand" "")]
  "TARGET_V9"
{
  if (TARGET_ARCH64)
    emit_insn (gen_prefetch_64 (operands[0], operands[1], operands[2]));
  else
    emit_insn (gen_prefetch_32 (operands[0], operands[1], operands[2]));
  DONE;
})

(define_insn "prefetch_64"
  [(prefetch (match_operand:DI 0 "address_operand" "p")
	     (match_operand:DI 1 "const_int_operand" "n")
	     (match_operand:DI 2 "const_int_operand" "n"))]
  ""
{
  static const char * const prefetch_instr[2][2] = {
    {
      "prefetch\t[%a0], 1", /* no locality: prefetch for one read */
      "prefetch\t[%a0], 0", /* medium to high locality: prefetch for several reads */
    },
    {
      "prefetch\t[%a0], 3", /* no locality: prefetch for one write */
      "prefetch\t[%a0], 2", /* medium to high locality: prefetch for several writes */
    }
  };
  int read_or_write = INTVAL (operands[1]);
  int locality = INTVAL (operands[2]);

  gcc_assert (read_or_write == 0 || read_or_write == 1);
  gcc_assert (locality >= 0 && locality < 4);
  return prefetch_instr [read_or_write][locality == 0 ? 0 : 1];
}
  [(set_attr "type" "load")])

(define_insn "prefetch_32"
  [(prefetch (match_operand:SI 0 "address_operand" "p")
	     (match_operand:SI 1 "const_int_operand" "n")
	     (match_operand:SI 2 "const_int_operand" "n"))]
  ""
{
  static const char * const prefetch_instr[2][2] = {
    {
      "prefetch\t[%a0], 1", /* no locality: prefetch for one read */
      "prefetch\t[%a0], 0", /* medium to high locality: prefetch for several reads */
    },
    {
      "prefetch\t[%a0], 3", /* no locality: prefetch for one write */
      "prefetch\t[%a0], 2", /* medium to high locality: prefetch for several writes */
    }
  };
  int read_or_write = INTVAL (operands[1]);
  int locality = INTVAL (operands[2]);

  gcc_assert (read_or_write == 0 || read_or_write == 1);
  gcc_assert (locality >= 0 && locality < 4);
  return prefetch_instr [read_or_write][locality == 0 ? 0 : 1];
}
  [(set_attr "type" "load")])


;; Trap instructions.

(define_insn "trap"
  [(trap_if (const_int 1) (const_int 5))]
  ""
  "ta\t5"
  [(set_attr "type" "trap")])

(define_expand "conditional_trap"
  [(trap_if (match_operator 0 "noov_compare_operator" [(match_dup 2) (match_dup 3)])
	    (match_operand:SI 1 "arith_operand" ""))]
  ""
  "operands[2] = gen_compare_reg (GET_CODE (operands[0]));
   if (GET_MODE (operands[2]) != CCmode && GET_MODE (operands[2]) != CCXmode)
     FAIL;
   operands[3] = const0_rtx;")

(define_insn ""
  [(trap_if (match_operator 0 "noov_compare_operator" [(reg:CC 100) (const_int 0)])
	    (match_operand:SI 1 "arith_operand" "rM"))]
  ""
{
  if (TARGET_V9)
    return "t%C0\t%%icc, %1";
  else
    return "t%C0\t%1";
}
  [(set_attr "type" "trap")])

(define_insn ""
  [(trap_if (match_operator 0 "noov_compare_operator" [(reg:CCX 100) (const_int 0)])
	    (match_operand:SI 1 "arith_operand" "rM"))]
  "TARGET_V9"
  "t%C0\t%%xcc, %1"
  [(set_attr "type" "trap")])


;; TLS support instructions.

(define_insn "tgd_hi22"
  [(set (match_operand:SI 0 "register_operand" "=r")
        (high:SI (unspec:SI [(match_operand 1 "tgd_symbolic_operand" "")]
			    UNSPEC_TLSGD)))]
  "TARGET_TLS"
  "sethi\\t%%tgd_hi22(%a1), %0")

(define_insn "tgd_lo10"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(lo_sum:SI (match_operand:SI 1 "register_operand" "r")
		   (unspec:SI [(match_operand 2 "tgd_symbolic_operand" "")]
			      UNSPEC_TLSGD)))]
  "TARGET_TLS"
  "add\\t%1, %%tgd_lo10(%a2), %0")

(define_insn "tgd_add32"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(plus:SI (match_operand:SI 1 "register_operand" "r")
		 (unspec:SI [(match_operand:SI 2 "register_operand" "r")
			     (match_operand 3 "tgd_symbolic_operand" "")]
			    UNSPEC_TLSGD)))]
  "TARGET_TLS && TARGET_ARCH32"
  "add\\t%1, %2, %0, %%tgd_add(%a3)")

(define_insn "tgd_add64"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(plus:DI (match_operand:DI 1 "register_operand" "r")
		 (unspec:DI [(match_operand:SI 2 "register_operand" "r")
			     (match_operand 3 "tgd_symbolic_operand" "")]
			    UNSPEC_TLSGD)))]
  "TARGET_TLS && TARGET_ARCH64"
  "add\\t%1, %2, %0, %%tgd_add(%a3)")

(define_insn "tgd_call32"
  [(set (match_operand 0 "register_operand" "=r")
	(call (mem:SI (unspec:SI [(match_operand:SI 1 "symbolic_operand" "s")
				  (match_operand 2 "tgd_symbolic_operand" "")]
				 UNSPEC_TLSGD))
	      (match_operand 3 "" "")))
   (clobber (reg:SI 15))]
  "TARGET_TLS && TARGET_ARCH32"
  "call\t%a1, %%tgd_call(%a2)%#"
  [(set_attr "type" "call")])

(define_insn "tgd_call64"
  [(set (match_operand 0 "register_operand" "=r")
	(call (mem:DI (unspec:DI [(match_operand:DI 1 "symbolic_operand" "s")
				  (match_operand 2 "tgd_symbolic_operand" "")]
				 UNSPEC_TLSGD))
	      (match_operand 3 "" "")))
   (clobber (reg:DI 15))]
  "TARGET_TLS && TARGET_ARCH64"
  "call\t%a1, %%tgd_call(%a2)%#"
  [(set_attr "type" "call")])

(define_insn "tldm_hi22"
  [(set (match_operand:SI 0 "register_operand" "=r")
        (high:SI (unspec:SI [(const_int 0)] UNSPEC_TLSLDM)))]
  "TARGET_TLS"
  "sethi\\t%%tldm_hi22(%&), %0")

(define_insn "tldm_lo10"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(lo_sum:SI (match_operand:SI 1 "register_operand" "r")
		    (unspec:SI [(const_int 0)] UNSPEC_TLSLDM)))]
  "TARGET_TLS"
  "add\\t%1, %%tldm_lo10(%&), %0")

(define_insn "tldm_add32"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(plus:SI (match_operand:SI 1 "register_operand" "r")
		 (unspec:SI [(match_operand:SI 2 "register_operand" "r")]
			    UNSPEC_TLSLDM)))]
  "TARGET_TLS && TARGET_ARCH32"
  "add\\t%1, %2, %0, %%tldm_add(%&)")

(define_insn "tldm_add64"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(plus:DI (match_operand:DI 1 "register_operand" "r")
		 (unspec:DI [(match_operand:SI 2 "register_operand" "r")]
			    UNSPEC_TLSLDM)))]
  "TARGET_TLS && TARGET_ARCH64"
  "add\\t%1, %2, %0, %%tldm_add(%&)")

(define_insn "tldm_call32"
  [(set (match_operand 0 "register_operand" "=r")
	(call (mem:SI (unspec:SI [(match_operand:SI 1 "symbolic_operand" "s")]
				 UNSPEC_TLSLDM))
	      (match_operand 2 "" "")))
   (clobber (reg:SI 15))]
  "TARGET_TLS && TARGET_ARCH32"
  "call\t%a1, %%tldm_call(%&)%#"
  [(set_attr "type" "call")])

(define_insn "tldm_call64"
  [(set (match_operand 0 "register_operand" "=r")
	(call (mem:DI (unspec:DI [(match_operand:DI 1 "symbolic_operand" "s")]
				 UNSPEC_TLSLDM))
	      (match_operand 2 "" "")))
   (clobber (reg:DI 15))]
  "TARGET_TLS && TARGET_ARCH64"
  "call\t%a1, %%tldm_call(%&)%#"
  [(set_attr "type" "call")])

(define_insn "tldo_hix22"
  [(set (match_operand:SI 0 "register_operand" "=r")
        (high:SI (unspec:SI [(match_operand 1 "tld_symbolic_operand" "")]
			    UNSPEC_TLSLDO)))]
  "TARGET_TLS"
  "sethi\\t%%tldo_hix22(%a1), %0")

(define_insn "tldo_lox10"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(lo_sum:SI (match_operand:SI 1 "register_operand" "r")
		   (unspec:SI [(match_operand 2 "tld_symbolic_operand" "")]
			      UNSPEC_TLSLDO)))]
  "TARGET_TLS"
  "xor\\t%1, %%tldo_lox10(%a2), %0")

(define_insn "tldo_add32"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(plus:SI (match_operand:SI 1 "register_operand" "r")
		 (unspec:SI [(match_operand:SI 2 "register_operand" "r")
			     (match_operand 3 "tld_symbolic_operand" "")]
			    UNSPEC_TLSLDO)))]
  "TARGET_TLS && TARGET_ARCH32"
  "add\\t%1, %2, %0, %%tldo_add(%a3)")

(define_insn "tldo_add64"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(plus:DI (match_operand:DI 1 "register_operand" "r")
		 (unspec:DI [(match_operand:SI 2 "register_operand" "r")
			     (match_operand 3 "tld_symbolic_operand" "")]
			    UNSPEC_TLSLDO)))]
  "TARGET_TLS && TARGET_ARCH64"
  "add\\t%1, %2, %0, %%tldo_add(%a3)")

(define_insn "tie_hi22"
  [(set (match_operand:SI 0 "register_operand" "=r")
        (high:SI (unspec:SI [(match_operand 1 "tie_symbolic_operand" "")]
			    UNSPEC_TLSIE)))]
  "TARGET_TLS"
  "sethi\\t%%tie_hi22(%a1), %0")

(define_insn "tie_lo10"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(lo_sum:SI (match_operand:SI 1 "register_operand" "r")
		   (unspec:SI [(match_operand 2 "tie_symbolic_operand" "")]
			      UNSPEC_TLSIE)))]
  "TARGET_TLS"
  "add\\t%1, %%tie_lo10(%a2), %0")

(define_insn "tie_ld32"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(unspec:SI [(match_operand:SI 1 "register_operand" "r")
		    (match_operand:SI 2 "register_operand" "r")
		    (match_operand 3 "tie_symbolic_operand" "")]
		   UNSPEC_TLSIE))]
  "TARGET_TLS && TARGET_ARCH32"
  "ld\\t[%1 + %2], %0, %%tie_ld(%a3)"
  [(set_attr "type" "load")])

(define_insn "tie_ld64"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(unspec:DI [(match_operand:DI 1 "register_operand" "r")
		    (match_operand:SI 2 "register_operand" "r")
		    (match_operand 3 "tie_symbolic_operand" "")]
		   UNSPEC_TLSIE))]
  "TARGET_TLS && TARGET_ARCH64"
  "ldx\\t[%1 + %2], %0, %%tie_ldx(%a3)"
  [(set_attr "type" "load")])

(define_insn "tie_add32"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(plus:SI (match_operand:SI 1 "register_operand" "r")
		 (unspec:SI [(match_operand:SI 2 "register_operand" "r")
			     (match_operand 3 "tie_symbolic_operand" "")]
			    UNSPEC_TLSIE)))]
  "TARGET_SUN_TLS && TARGET_ARCH32"
  "add\\t%1, %2, %0, %%tie_add(%a3)")

(define_insn "tie_add64"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(plus:DI (match_operand:DI 1 "register_operand" "r")
		 (unspec:DI [(match_operand:DI 2 "register_operand" "r")
			     (match_operand 3 "tie_symbolic_operand" "")]
			    UNSPEC_TLSIE)))]
  "TARGET_SUN_TLS && TARGET_ARCH64"
  "add\\t%1, %2, %0, %%tie_add(%a3)")

(define_insn "tle_hix22_sp32"
  [(set (match_operand:SI 0 "register_operand" "=r")
        (high:SI (unspec:SI [(match_operand 1 "tle_symbolic_operand" "")]
			    UNSPEC_TLSLE)))]
  "TARGET_TLS && TARGET_ARCH32"
  "sethi\\t%%tle_hix22(%a1), %0")

(define_insn "tle_lox10_sp32"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(lo_sum:SI (match_operand:SI 1 "register_operand" "r")
		   (unspec:SI [(match_operand 2 "tle_symbolic_operand" "")]
			      UNSPEC_TLSLE)))]
  "TARGET_TLS && TARGET_ARCH32"
  "xor\\t%1, %%tle_lox10(%a2), %0")

(define_insn "tle_hix22_sp64"
  [(set (match_operand:DI 0 "register_operand" "=r")
        (high:DI (unspec:DI [(match_operand 1 "tle_symbolic_operand" "")]
			    UNSPEC_TLSLE)))]
  "TARGET_TLS && TARGET_ARCH64"
  "sethi\\t%%tle_hix22(%a1), %0")

(define_insn "tle_lox10_sp64"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(lo_sum:DI (match_operand:DI 1 "register_operand" "r")
		   (unspec:DI [(match_operand 2 "tle_symbolic_operand" "")]
			      UNSPEC_TLSLE)))]
  "TARGET_TLS && TARGET_ARCH64"
  "xor\\t%1, %%tle_lox10(%a2), %0")

;; Now patterns combining tldo_add{32,64} with some integer loads or stores
(define_insn "*tldo_ldub_sp32"
  [(set (match_operand:QI 0 "register_operand" "=r")
	(mem:QI (plus:SI (unspec:SI [(match_operand:SI 2 "register_operand" "r")
				     (match_operand 3 "tld_symbolic_operand" "")]
				    UNSPEC_TLSLDO)
			 (match_operand:SI 1 "register_operand" "r"))))]
  "TARGET_TLS && TARGET_ARCH32"
  "ldub\t[%1 + %2], %0, %%tldo_add(%3)"
  [(set_attr "type" "load")
   (set_attr "us3load_type" "3cycle")])

(define_insn "*tldo_ldub1_sp32"
  [(set (match_operand:HI 0 "register_operand" "=r")
	(zero_extend:HI (mem:QI (plus:SI (unspec:SI [(match_operand:SI 2 "register_operand" "r")
						     (match_operand 3 "tld_symbolic_operand" "")]
						    UNSPEC_TLSLDO)
					 (match_operand:SI 1 "register_operand" "r")))))]
  "TARGET_TLS && TARGET_ARCH32"
  "ldub\t[%1 + %2], %0, %%tldo_add(%3)"
  [(set_attr "type" "load")
   (set_attr "us3load_type" "3cycle")])

(define_insn "*tldo_ldub2_sp32"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(zero_extend:SI (mem:QI (plus:SI (unspec:SI [(match_operand:SI 2 "register_operand" "r")
						     (match_operand 3 "tld_symbolic_operand" "")]
						    UNSPEC_TLSLDO)
					 (match_operand:SI 1 "register_operand" "r")))))]
  "TARGET_TLS && TARGET_ARCH32"
  "ldub\t[%1 + %2], %0, %%tldo_add(%3)"
  [(set_attr "type" "load")
   (set_attr "us3load_type" "3cycle")])

(define_insn "*tldo_ldsb1_sp32"
  [(set (match_operand:HI 0 "register_operand" "=r")
	(sign_extend:HI (mem:QI (plus:SI (unspec:SI [(match_operand:SI 2 "register_operand" "r")
						     (match_operand 3 "tld_symbolic_operand" "")]
						    UNSPEC_TLSLDO)
					 (match_operand:SI 1 "register_operand" "r")))))]
  "TARGET_TLS && TARGET_ARCH32"
  "ldsb\t[%1 + %2], %0, %%tldo_add(%3)"
  [(set_attr "type" "sload")
   (set_attr "us3load_type" "3cycle")])

(define_insn "*tldo_ldsb2_sp32"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(sign_extend:SI (mem:QI (plus:SI (unspec:SI [(match_operand:SI 2 "register_operand" "r")
						     (match_operand 3 "tld_symbolic_operand" "")]
						    UNSPEC_TLSLDO)
					 (match_operand:SI 1 "register_operand" "r")))))]
  "TARGET_TLS && TARGET_ARCH32"
  "ldsb\t[%1 + %2], %0, %%tldo_add(%3)"
  [(set_attr "type" "sload")
   (set_attr "us3load_type" "3cycle")])

(define_insn "*tldo_ldub_sp64"
  [(set (match_operand:QI 0 "register_operand" "=r")
	(mem:QI (plus:DI (unspec:DI [(match_operand:SI 2 "register_operand" "r")
				     (match_operand 3 "tld_symbolic_operand" "")]
				    UNSPEC_TLSLDO)
			 (match_operand:DI 1 "register_operand" "r"))))]
  "TARGET_TLS && TARGET_ARCH64"
  "ldub\t[%1 + %2], %0, %%tldo_add(%3)"
  [(set_attr "type" "load")
   (set_attr "us3load_type" "3cycle")])

(define_insn "*tldo_ldub1_sp64"
  [(set (match_operand:HI 0 "register_operand" "=r")
	(zero_extend:HI (mem:QI (plus:DI (unspec:DI [(match_operand:SI 2 "register_operand" "r")
						     (match_operand 3 "tld_symbolic_operand" "")]
						    UNSPEC_TLSLDO)
					 (match_operand:DI 1 "register_operand" "r")))))]
  "TARGET_TLS && TARGET_ARCH64"
  "ldub\t[%1 + %2], %0, %%tldo_add(%3)"
  [(set_attr "type" "load")
   (set_attr "us3load_type" "3cycle")])

(define_insn "*tldo_ldub2_sp64"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(zero_extend:SI (mem:QI (plus:DI (unspec:DI [(match_operand:SI 2 "register_operand" "r")
						     (match_operand 3 "tld_symbolic_operand" "")]
						    UNSPEC_TLSLDO)
					 (match_operand:DI 1 "register_operand" "r")))))]
  "TARGET_TLS && TARGET_ARCH64"
  "ldub\t[%1 + %2], %0, %%tldo_add(%3)"
  [(set_attr "type" "load")
   (set_attr "us3load_type" "3cycle")])

(define_insn "*tldo_ldub3_sp64"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(zero_extend:DI (mem:QI (plus:DI (unspec:DI [(match_operand:SI 2 "register_operand" "r")
						     (match_operand 3 "tld_symbolic_operand" "")]
						    UNSPEC_TLSLDO)
					 (match_operand:DI 1 "register_operand" "r")))))]
  "TARGET_TLS && TARGET_ARCH64"
  "ldub\t[%1 + %2], %0, %%tldo_add(%3)"
  [(set_attr "type" "load")
   (set_attr "us3load_type" "3cycle")])

(define_insn "*tldo_ldsb1_sp64"
  [(set (match_operand:HI 0 "register_operand" "=r")
	(sign_extend:HI (mem:QI (plus:DI (unspec:DI [(match_operand:SI 2 "register_operand" "r")
						     (match_operand 3 "tld_symbolic_operand" "")]
						    UNSPEC_TLSLDO)
					 (match_operand:DI 1 "register_operand" "r")))))]
  "TARGET_TLS && TARGET_ARCH64"
  "ldsb\t[%1 + %2], %0, %%tldo_add(%3)"
  [(set_attr "type" "sload")
   (set_attr "us3load_type" "3cycle")])

(define_insn "*tldo_ldsb2_sp64"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(sign_extend:SI (mem:QI (plus:DI (unspec:DI [(match_operand:SI 2 "register_operand" "r")
						     (match_operand 3 "tld_symbolic_operand" "")]
						    UNSPEC_TLSLDO)
					 (match_operand:DI 1 "register_operand" "r")))))]
  "TARGET_TLS && TARGET_ARCH64"
  "ldsb\t[%1 + %2], %0, %%tldo_add(%3)"
  [(set_attr "type" "sload")
   (set_attr "us3load_type" "3cycle")])

(define_insn "*tldo_ldsb3_sp64"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(sign_extend:DI (mem:QI (plus:DI (unspec:DI [(match_operand:SI 2 "register_operand" "r")
						     (match_operand 3 "tld_symbolic_operand" "")]
						    UNSPEC_TLSLDO)
					 (match_operand:DI 1 "register_operand" "r")))))]
  "TARGET_TLS && TARGET_ARCH64"
  "ldsb\t[%1 + %2], %0, %%tldo_add(%3)"
  [(set_attr "type" "sload")
   (set_attr "us3load_type" "3cycle")])

(define_insn "*tldo_lduh_sp32"
  [(set (match_operand:HI 0 "register_operand" "=r")
	(mem:HI (plus:SI (unspec:SI [(match_operand:SI 2 "register_operand" "r")
				     (match_operand 3 "tld_symbolic_operand" "")]
				    UNSPEC_TLSLDO)
			 (match_operand:SI 1 "register_operand" "r"))))]
  "TARGET_TLS && TARGET_ARCH32"
  "lduh\t[%1 + %2], %0, %%tldo_add(%3)"
  [(set_attr "type" "load")
   (set_attr "us3load_type" "3cycle")])

(define_insn "*tldo_lduh1_sp32"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(zero_extend:SI (mem:HI (plus:SI (unspec:SI [(match_operand:SI 2 "register_operand" "r")
						     (match_operand 3 "tld_symbolic_operand" "")]
						    UNSPEC_TLSLDO)
					 (match_operand:SI 1 "register_operand" "r")))))]
  "TARGET_TLS && TARGET_ARCH32"
  "lduh\t[%1 + %2], %0, %%tldo_add(%3)"
  [(set_attr "type" "load")
   (set_attr "us3load_type" "3cycle")])

(define_insn "*tldo_ldsh1_sp32"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(sign_extend:SI (mem:HI (plus:SI (unspec:SI [(match_operand:SI 2 "register_operand" "r")
						     (match_operand 3 "tld_symbolic_operand" "")]
						    UNSPEC_TLSLDO)
					 (match_operand:SI 1 "register_operand" "r")))))]
  "TARGET_TLS && TARGET_ARCH32"
  "ldsh\t[%1 + %2], %0, %%tldo_add(%3)"
  [(set_attr "type" "sload")
   (set_attr "us3load_type" "3cycle")])

(define_insn "*tldo_lduh_sp64"
  [(set (match_operand:HI 0 "register_operand" "=r")
	(mem:HI (plus:DI (unspec:DI [(match_operand:SI 2 "register_operand" "r")
				     (match_operand 3 "tld_symbolic_operand" "")]
				    UNSPEC_TLSLDO)
			 (match_operand:DI 1 "register_operand" "r"))))]
  "TARGET_TLS && TARGET_ARCH64"
  "lduh\t[%1 + %2], %0, %%tldo_add(%3)"
  [(set_attr "type" "load")
   (set_attr "us3load_type" "3cycle")])

(define_insn "*tldo_lduh1_sp64"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(zero_extend:SI (mem:HI (plus:DI (unspec:DI [(match_operand:SI 2 "register_operand" "r")
						     (match_operand 3 "tld_symbolic_operand" "")]
						    UNSPEC_TLSLDO)
					 (match_operand:DI 1 "register_operand" "r")))))]
  "TARGET_TLS && TARGET_ARCH64"
  "lduh\t[%1 + %2], %0, %%tldo_add(%3)"
  [(set_attr "type" "load")
   (set_attr "us3load_type" "3cycle")])

(define_insn "*tldo_lduh2_sp64"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(zero_extend:DI (mem:HI (plus:DI (unspec:DI [(match_operand:SI 2 "register_operand" "r")
						     (match_operand 3 "tld_symbolic_operand" "")]
						    UNSPEC_TLSLDO)
					 (match_operand:DI 1 "register_operand" "r")))))]
  "TARGET_TLS && TARGET_ARCH64"
  "lduh\t[%1 + %2], %0, %%tldo_add(%3)"
  [(set_attr "type" "load")
   (set_attr "us3load_type" "3cycle")])

(define_insn "*tldo_ldsh1_sp64"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(sign_extend:SI (mem:HI (plus:DI (unspec:DI [(match_operand:SI 2 "register_operand" "r")
						     (match_operand 3 "tld_symbolic_operand" "")]
						    UNSPEC_TLSLDO)
					 (match_operand:DI 1 "register_operand" "r")))))]
  "TARGET_TLS && TARGET_ARCH64"
  "ldsh\t[%1 + %2], %0, %%tldo_add(%3)"
  [(set_attr "type" "sload")
   (set_attr "us3load_type" "3cycle")])

(define_insn "*tldo_ldsh2_sp64"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(sign_extend:DI (mem:HI (plus:DI (unspec:DI [(match_operand:SI 2 "register_operand" "r")
						     (match_operand 3 "tld_symbolic_operand" "")]
						    UNSPEC_TLSLDO)
					 (match_operand:DI 1 "register_operand" "r")))))]
  "TARGET_TLS && TARGET_ARCH64"
  "ldsh\t[%1 + %2], %0, %%tldo_add(%3)"
  [(set_attr "type" "sload")
   (set_attr "us3load_type" "3cycle")])

(define_insn "*tldo_lduw_sp32"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(mem:SI (plus:SI (unspec:SI [(match_operand:SI 2 "register_operand" "r")
				     (match_operand 3 "tld_symbolic_operand" "")]
				    UNSPEC_TLSLDO)
			 (match_operand:SI 1 "register_operand" "r"))))]
  "TARGET_TLS && TARGET_ARCH32"
  "ld\t[%1 + %2], %0, %%tldo_add(%3)"
  [(set_attr "type" "load")])

(define_insn "*tldo_lduw_sp64"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(mem:SI (plus:DI (unspec:DI [(match_operand:SI 2 "register_operand" "r")
				     (match_operand 3 "tld_symbolic_operand" "")]
				    UNSPEC_TLSLDO)
			 (match_operand:DI 1 "register_operand" "r"))))]
  "TARGET_TLS && TARGET_ARCH64"
  "lduw\t[%1 + %2], %0, %%tldo_add(%3)"
  [(set_attr "type" "load")])

(define_insn "*tldo_lduw1_sp64"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(zero_extend:DI (mem:SI (plus:DI (unspec:DI [(match_operand:SI 2 "register_operand" "r")
						     (match_operand 3 "tld_symbolic_operand" "")]
						    UNSPEC_TLSLDO)
					 (match_operand:DI 1 "register_operand" "r")))))]
  "TARGET_TLS && TARGET_ARCH64"
  "lduw\t[%1 + %2], %0, %%tldo_add(%3)"
  [(set_attr "type" "load")])

(define_insn "*tldo_ldsw1_sp64"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(sign_extend:DI (mem:SI (plus:DI (unspec:DI [(match_operand:SI 2 "register_operand" "r")
						     (match_operand 3 "tld_symbolic_operand" "")]
						    UNSPEC_TLSLDO)
					 (match_operand:DI 1 "register_operand" "r")))))]
  "TARGET_TLS && TARGET_ARCH64"
  "ldsw\t[%1 + %2], %0, %%tldo_add(%3)"
  [(set_attr "type" "sload")
   (set_attr "us3load_type" "3cycle")])

(define_insn "*tldo_ldx_sp64"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(mem:DI (plus:DI (unspec:DI [(match_operand:SI 2 "register_operand" "r")
				     (match_operand 3 "tld_symbolic_operand" "")]
				    UNSPEC_TLSLDO)
			 (match_operand:DI 1 "register_operand" "r"))))]
  "TARGET_TLS && TARGET_ARCH64"
  "ldx\t[%1 + %2], %0, %%tldo_add(%3)"
  [(set_attr "type" "load")])

(define_insn "*tldo_stb_sp32"
  [(set (mem:QI (plus:SI (unspec:SI [(match_operand:SI 2 "register_operand" "r")
				     (match_operand 3 "tld_symbolic_operand" "")]
				    UNSPEC_TLSLDO)
			 (match_operand:SI 1 "register_operand" "r")))
	(match_operand:QI 0 "register_operand" "=r"))]
  "TARGET_TLS && TARGET_ARCH32"
  "stb\t%0, [%1 + %2], %%tldo_add(%3)"
  [(set_attr "type" "store")])

(define_insn "*tldo_stb_sp64"
  [(set (mem:QI (plus:DI (unspec:DI [(match_operand:SI 2 "register_operand" "r")
				     (match_operand 3 "tld_symbolic_operand" "")]
				    UNSPEC_TLSLDO)
			 (match_operand:DI 1 "register_operand" "r")))
	(match_operand:QI 0 "register_operand" "=r"))]
  "TARGET_TLS && TARGET_ARCH64"
  "stb\t%0, [%1 + %2], %%tldo_add(%3)"
  [(set_attr "type" "store")])

(define_insn "*tldo_sth_sp32"
  [(set (mem:HI (plus:SI (unspec:SI [(match_operand:SI 2 "register_operand" "r")
				     (match_operand 3 "tld_symbolic_operand" "")]
				    UNSPEC_TLSLDO)
			 (match_operand:SI 1 "register_operand" "r")))
	(match_operand:HI 0 "register_operand" "=r"))]
  "TARGET_TLS && TARGET_ARCH32"
  "sth\t%0, [%1 + %2], %%tldo_add(%3)"
  [(set_attr "type" "store")])

(define_insn "*tldo_sth_sp64"
  [(set (mem:HI (plus:DI (unspec:DI [(match_operand:SI 2 "register_operand" "r")
				     (match_operand 3 "tld_symbolic_operand" "")]
				    UNSPEC_TLSLDO)
			 (match_operand:DI 1 "register_operand" "r")))
	(match_operand:HI 0 "register_operand" "=r"))]
  "TARGET_TLS && TARGET_ARCH64"
  "sth\t%0, [%1 + %2], %%tldo_add(%3)"
  [(set_attr "type" "store")])

(define_insn "*tldo_stw_sp32"
  [(set (mem:SI (plus:SI (unspec:SI [(match_operand:SI 2 "register_operand" "r")
				     (match_operand 3 "tld_symbolic_operand" "")]
				    UNSPEC_TLSLDO)
			 (match_operand:SI 1 "register_operand" "r")))
	(match_operand:SI 0 "register_operand" "=r"))]
  "TARGET_TLS && TARGET_ARCH32"
  "st\t%0, [%1 + %2], %%tldo_add(%3)"
  [(set_attr "type" "store")])

(define_insn "*tldo_stw_sp64"
  [(set (mem:SI (plus:DI (unspec:DI [(match_operand:SI 2 "register_operand" "r")
				     (match_operand 3 "tld_symbolic_operand" "")]
				    UNSPEC_TLSLDO)
			 (match_operand:DI 1 "register_operand" "r")))
	(match_operand:SI 0 "register_operand" "=r"))]
  "TARGET_TLS && TARGET_ARCH64"
  "stw\t%0, [%1 + %2], %%tldo_add(%3)"
  [(set_attr "type" "store")])

(define_insn "*tldo_stx_sp64"
  [(set (mem:DI (plus:DI (unspec:DI [(match_operand:SI 2 "register_operand" "r")
				     (match_operand 3 "tld_symbolic_operand" "")]
				    UNSPEC_TLSLDO)
			 (match_operand:DI 1 "register_operand" "r")))
	(match_operand:DI 0 "register_operand" "=r"))]
  "TARGET_TLS && TARGET_ARCH64"
  "stx\t%0, [%1 + %2], %%tldo_add(%3)"
  [(set_attr "type" "store")])


;; Stack protector instructions.

(define_expand "stack_protect_set"
  [(match_operand 0 "memory_operand" "")
   (match_operand 1 "memory_operand" "")]
  ""
{
#ifdef TARGET_THREAD_SSP_OFFSET
  rtx tlsreg = gen_rtx_REG (Pmode, 7);
  rtx addr = gen_rtx_PLUS (Pmode, tlsreg, GEN_INT (TARGET_THREAD_SSP_OFFSET));
  operands[1] = gen_rtx_MEM (Pmode, addr);
#endif
  if (TARGET_ARCH64)
    emit_insn (gen_stack_protect_setdi (operands[0], operands[1]));
  else
    emit_insn (gen_stack_protect_setsi (operands[0], operands[1]));
  DONE;
})

(define_insn "stack_protect_setsi"
  [(set (match_operand:SI 0 "memory_operand" "=m")
	(unspec:SI [(match_operand:SI 1 "memory_operand" "m")] UNSPEC_SP_SET))
   (set (match_scratch:SI 2 "=&r") (const_int 0))]
  "TARGET_ARCH32"
  "ld\t%1, %2\;st\t%2, %0\;mov\t0, %2"
  [(set_attr "type" "multi")
   (set_attr "length" "3")])

(define_insn "stack_protect_setdi"
  [(set (match_operand:DI 0 "memory_operand" "=m")
	(unspec:DI [(match_operand:DI 1 "memory_operand" "m")] UNSPEC_SP_SET))
   (set (match_scratch:DI 2 "=&r") (const_int 0))]
  "TARGET_ARCH64"
  "ldx\t%1, %2\;stx\t%2, %0\;mov\t0, %2"
  [(set_attr "type" "multi")
   (set_attr "length" "3")])

(define_expand "stack_protect_test"
  [(match_operand 0 "memory_operand" "")
   (match_operand 1 "memory_operand" "")
   (match_operand 2 "" "")]
  ""
{
#ifdef TARGET_THREAD_SSP_OFFSET
  rtx tlsreg = gen_rtx_REG (Pmode, 7);
  rtx addr = gen_rtx_PLUS (Pmode, tlsreg, GEN_INT (TARGET_THREAD_SSP_OFFSET));
  operands[1] = gen_rtx_MEM (Pmode, addr);
#endif
  if (TARGET_ARCH64)
    {
      rtx temp = gen_reg_rtx (Pmode);
      emit_insn (gen_stack_protect_testdi (temp, operands[0], operands[1]));
      sparc_compare_op0 = temp;
      sparc_compare_op1 = const0_rtx;
    }
  else
    {
      emit_insn (gen_stack_protect_testsi (operands[0], operands[1]));
      sparc_compare_op0 = operands[0];
      sparc_compare_op1 = operands[1];
      sparc_compare_emitted = gen_rtx_REG (CCmode, SPARC_ICC_REG);
    }
  emit_jump_insn (gen_beq (operands[2]));
  DONE;
})

(define_insn "stack_protect_testsi"
  [(set (reg:CC 100)
	(unspec:CC [(match_operand:SI 0 "memory_operand" "m")
		    (match_operand:SI 1 "memory_operand" "m")]
		   UNSPEC_SP_TEST))
   (set (match_scratch:SI 3 "=r") (const_int 0))
   (clobber (match_scratch:SI 2 "=&r"))]
  "TARGET_ARCH32"
  "ld\t%0, %2\;ld\t%1, %3\;xorcc\t%2, %3, %2\;mov\t0, %3"
  [(set_attr "type" "multi")
   (set_attr "length" "4")])

(define_insn "stack_protect_testdi"
  [(set (match_operand:DI 0 "register_operand" "=&r")
	(unspec:DI [(match_operand:DI 1 "memory_operand" "m")
		    (match_operand:DI 2 "memory_operand" "m")]
		   UNSPEC_SP_TEST))
   (set (match_scratch:DI 3 "=r") (const_int 0))]
  "TARGET_ARCH64"
  "ldx\t%1, %0\;ldx\t%2, %3\;xor\t%0, %3, %0\;mov\t0, %3"
  [(set_attr "type" "multi")
   (set_attr "length" "4")])


;; Vector instructions.

(define_insn "addv2si3"
  [(set (match_operand:V2SI 0 "register_operand" "=e")
	(plus:V2SI (match_operand:V2SI 1 "register_operand" "e")
		   (match_operand:V2SI 2 "register_operand" "e")))]
  "TARGET_VIS"
  "fpadd32\t%1, %2, %0"
  [(set_attr "type" "fga")
   (set_attr "fptype" "double")])

(define_insn "addv4hi3"
  [(set (match_operand:V4HI 0 "register_operand" "=e")
	 (plus:V4HI (match_operand:V4HI 1 "register_operand" "e")
		    (match_operand:V4HI 2 "register_operand" "e")))]
  "TARGET_VIS"
  "fpadd16\t%1, %2, %0"
  [(set_attr "type" "fga")
   (set_attr "fptype" "double")])

;; fpadd32s is emitted by the addsi3 pattern.

(define_insn "addv2hi3"
  [(set (match_operand:V2HI 0 "register_operand" "=f")
	(plus:V2HI (match_operand:V2HI 1 "register_operand" "f")
		   (match_operand:V2HI 2 "register_operand" "f")))]
  "TARGET_VIS"
  "fpadd16s\t%1, %2, %0"
  [(set_attr "type" "fga")
   (set_attr "fptype" "single")])

(define_insn "subv2si3"
  [(set (match_operand:V2SI 0 "register_operand" "=e")
	(minus:V2SI (match_operand:V2SI 1 "register_operand" "e")
		    (match_operand:V2SI 2 "register_operand" "e")))]
  "TARGET_VIS"
  "fpsub32\t%1, %2, %0"
  [(set_attr "type" "fga")
   (set_attr "fptype" "double")])

(define_insn "subv4hi3"
  [(set (match_operand:V4HI 0 "register_operand" "=e")
	(minus:V4HI (match_operand:V4HI 1 "register_operand" "e")
		    (match_operand:V4HI 2 "register_operand" "e")))]
  "TARGET_VIS"
  "fpsub16\t%1, %2, %0"
  [(set_attr "type" "fga")
   (set_attr "fptype" "double")])

;; fpsub32s is emitted by the subsi3 pattern.

(define_insn "subv2hi3"
  [(set (match_operand:V2HI 0 "register_operand" "=f")
	(minus:V2HI (match_operand:V2HI 1 "register_operand" "f")
		    (match_operand:V2HI 2 "register_operand" "f")))]
  "TARGET_VIS"
  "fpsub16s\t%1, %2, %0"
  [(set_attr "type" "fga")
   (set_attr "fptype" "single")])

;; All other logical instructions have integer equivalents so they
;; are defined together.

;; (ior (not (op1)) (not (op2))) is the canonical form of NAND.

(define_insn "*nand<V64mode>_vis"
  [(set (match_operand:V64 0 "register_operand" "=e")
	(ior:V64 (not:V64 (match_operand:V64 1 "register_operand" "e"))
		 (not:V64 (match_operand:V64 2 "register_operand" "e"))))]
  "TARGET_VIS"
  "fnand\t%1, %2, %0"
  [(set_attr "type" "fga")
   (set_attr "fptype" "double")])

(define_insn "*nand<V32mode>_vis"
  [(set (match_operand:V32 0 "register_operand" "=f")
	 (ior:V32 (not:V32 (match_operand:V32 1 "register_operand" "f"))
		  (not:V32 (match_operand:V32 2 "register_operand" "f"))))]
  "TARGET_VIS"
  "fnands\t%1, %2, %0"
  [(set_attr "type" "fga")
   (set_attr "fptype" "single")])

;; Hard to generate VIS instructions.  We have builtins for these.

(define_insn "fpack16_vis"
  [(set (match_operand:V4QI 0 "register_operand" "=f")
        (unspec:V4QI [(match_operand:V4HI 1 "register_operand" "e")]
		      UNSPEC_FPACK16))]
  "TARGET_VIS"
  "fpack16\t%1, %0"
  [(set_attr "type" "fga")
   (set_attr "fptype" "double")])

(define_insn "fpackfix_vis"
  [(set (match_operand:V2HI 0 "register_operand" "=f")
        (unspec:V2HI [(match_operand:V2SI 1 "register_operand" "e")]
		      UNSPEC_FPACKFIX))]
  "TARGET_VIS"
  "fpackfix\t%1, %0"
  [(set_attr "type" "fga")
   (set_attr "fptype" "double")])

(define_insn "fpack32_vis"
  [(set (match_operand:V8QI 0 "register_operand" "=e")
        (unspec:V8QI [(match_operand:V2SI 1 "register_operand" "e")
        	      (match_operand:V8QI 2 "register_operand" "e")]
                     UNSPEC_FPACK32))]
  "TARGET_VIS"
  "fpack32\t%1, %2, %0"
  [(set_attr "type" "fga")
   (set_attr "fptype" "double")])

(define_insn "fexpand_vis"
  [(set (match_operand:V4HI 0 "register_operand" "=e")
        (unspec:V4HI [(match_operand:V4QI 1 "register_operand" "f")]
         UNSPEC_FEXPAND))]
 "TARGET_VIS"
 "fexpand\t%1, %0"
 [(set_attr "type" "fga")
  (set_attr "fptype" "double")])

;; It may be possible to describe this operation as (1 indexed):
;; (vec_select (vec_duplicate (vec_duplicate (vec_concat 1 2)))
;;  1,5,10,14,19,23,28,32)
;; Note that (vec_merge:V8QI [(V4QI) (V4QI)] (10101010 = 170) doesn't work
;; because vec_merge expects all the operands to be of the same type.
(define_insn "fpmerge_vis"
  [(set (match_operand:V8QI 0 "register_operand" "=e")
        (unspec:V8QI [(match_operand:V4QI 1 "register_operand" "f")
                      (match_operand:V4QI 2 "register_operand" "f")]
         UNSPEC_FPMERGE))]
 "TARGET_VIS"
 "fpmerge\t%1, %2, %0"
 [(set_attr "type" "fga")
  (set_attr "fptype" "double")])

;; Partitioned multiply instructions
(define_insn "fmul8x16_vis"
  [(set (match_operand:V4HI 0 "register_operand" "=e")
        (mult:V4HI (match_operand:V4QI 1 "register_operand" "f")
                   (match_operand:V4HI 2 "register_operand" "e")))]
  "TARGET_VIS"
  "fmul8x16\t%1, %2, %0"
  [(set_attr "type" "fpmul")
   (set_attr "fptype" "double")])

;; Only one of the following two insns can be a multiply.
(define_insn "fmul8x16au_vis"
  [(set (match_operand:V4HI 0 "register_operand" "=e")
        (mult:V4HI (match_operand:V4QI 1 "register_operand" "f")
                   (match_operand:V2HI 2 "register_operand" "f")))]
  "TARGET_VIS"
  "fmul8x16au\t%1, %2, %0"
  [(set_attr "type" "fpmul")
   (set_attr "fptype" "double")])

(define_insn "fmul8x16al_vis"
  [(set (match_operand:V4HI 0 "register_operand" "=e")
        (unspec:V4HI [(match_operand:V4QI 1 "register_operand" "f")
                      (match_operand:V2HI 2 "register_operand" "f")]
         UNSPEC_MUL16AL))]
  "TARGET_VIS"
  "fmul8x16al\t%1, %2, %0"
  [(set_attr "type" "fpmul")
   (set_attr "fptype" "double")])

;; Only one of the following two insns can be a multiply.
(define_insn "fmul8sux16_vis"
  [(set (match_operand:V4HI 0 "register_operand" "=e")
        (mult:V4HI (match_operand:V8QI 1 "register_operand" "e")
                   (match_operand:V4HI 2 "register_operand" "e")))]
  "TARGET_VIS"
  "fmul8sux16\t%1, %2, %0"
  [(set_attr "type" "fpmul")
   (set_attr "fptype" "double")])

(define_insn "fmul8ulx16_vis"
  [(set (match_operand:V4HI 0 "register_operand" "=e")
        (unspec:V4HI [(match_operand:V8QI 1 "register_operand" "e")
                      (match_operand:V4HI 2 "register_operand" "e")]
         UNSPEC_MUL8UL))]
  "TARGET_VIS"
  "fmul8ulx16\t%1, %2, %0"
  [(set_attr "type" "fpmul")
   (set_attr "fptype" "double")])

;; Only one of the following two insns can be a multiply.
(define_insn "fmuld8sux16_vis"
  [(set (match_operand:V2SI 0 "register_operand" "=e")
        (mult:V2SI (match_operand:V4QI 1 "register_operand" "f")
                   (match_operand:V2HI 2 "register_operand" "f")))]
  "TARGET_VIS"
  "fmuld8sux16\t%1, %2, %0"
  [(set_attr "type" "fpmul")
   (set_attr "fptype" "double")])

(define_insn "fmuld8ulx16_vis"
  [(set (match_operand:V2SI 0 "register_operand" "=e")
        (unspec:V2SI [(match_operand:V4QI 1 "register_operand" "f")
                      (match_operand:V2HI 2 "register_operand" "f")]
         UNSPEC_MULDUL))]
  "TARGET_VIS"
  "fmuld8ulx16\t%1, %2, %0"
  [(set_attr "type" "fpmul")
   (set_attr "fptype" "double")])

;; Using faligndata only makes sense after an alignaddr since the choice of
;; bytes to take out of each operand is dependent on the results of the last
;; alignaddr.
(define_insn "faligndata<V64I:mode>_vis"
  [(set (match_operand:V64I 0 "register_operand" "=e")
        (unspec:V64I [(match_operand:V64I 1 "register_operand" "e")
                      (match_operand:V64I 2 "register_operand" "e")]
         UNSPEC_ALIGNDATA))]
  "TARGET_VIS"
  "faligndata\t%1, %2, %0"
  [(set_attr "type" "fga")
   (set_attr "fptype" "double")])

(define_insn "alignaddr<P:mode>_vis"
  [(set (match_operand:P 0 "register_operand" "=r")
        (unspec:P [(match_operand:P 1 "register_or_zero_operand" "rJ")
                   (match_operand:P 2 "register_or_zero_operand" "rJ")]
         UNSPEC_ALIGNADDR))]
  "TARGET_VIS"
  "alignaddr\t%r1, %r2, %0")

(define_insn "pdist_vis"
  [(set (match_operand:DI 0 "register_operand" "=e")
        (unspec:DI [(match_operand:V8QI 1 "register_operand" "e")
                    (match_operand:V8QI 2 "register_operand" "e")
                    (match_operand:DI 3 "register_operand" "0")]
         UNSPEC_PDIST))]
  "TARGET_VIS"
  "pdist\t%1, %2, %0"
  [(set_attr "type" "fga")
   (set_attr "fptype" "double")])

(include "sync.md")
