;; GCC machine description for CRX.
;; Copyright (C) 1988, 1994, 1995, 1996, 1997, 1998, 1999, 2000,
;; 2001, 2002, 2003, 2004
;; Free Software Foundation, Inc.
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
;; Boston, MA 02110-1301, USA.  */

;;  Register numbers

(define_constants
  [(SP_REGNUM 15)	; Stack pointer
   (RA_REGNUM 14)	; Return address
   (LO_REGNUM 16)	; LO register
   (HI_REGNUM 17)	; HI register
   (CC_REGNUM 18)	; Condition code register
  ]
)

(define_attr "length" "" ( const_int 6 ))

(define_asm_attributes
  [(set_attr "length" "6")]
)

;;  Predicates

(define_predicate "u4bits_operand"
  (match_code "const_int,const_double")
  {
    if (GET_CODE (op) == CONST_DOUBLE)
      return crx_const_double_ok (op);
    return (UNSIGNED_INT_FITS_N_BITS(INTVAL(op), 4)) ? 1 : 0;
  }
)

(define_predicate "cst4_operand"
  (and (match_code "const_int")
       (match_test "INT_CST4(INTVAL(op))")))

(define_predicate "reg_or_u4bits_operand"
  (ior (match_operand 0 "u4bits_operand")
       (match_operand 0 "register_operand")))

(define_predicate "reg_or_cst4_operand"
  (ior (match_operand 0 "cst4_operand")
       (match_operand 0 "register_operand")))

(define_predicate "reg_or_sym_operand"
  (ior (match_code "symbol_ref")
       (match_operand 0 "register_operand")))

(define_predicate "nosp_reg_operand"
  (and (match_operand 0 "register_operand")
       (match_test "REGNO (op) != SP_REGNUM")))

(define_predicate "store_operand"
  (and (match_operand 0 "memory_operand")
       (not (match_operand 0 "push_operand"))))

;;  Mode Macro Definitions

(define_mode_macro ALLMT [QI HI SI SF DI DF])
(define_mode_macro CRXMM [QI HI SI SF])
(define_mode_macro CRXIM [QI HI SI])
(define_mode_macro DIDFM [DI DF])
(define_mode_macro SISFM [SI SF])
(define_mode_macro SHORT [QI HI])

(define_mode_attr tIsa [(QI "b") (HI "w") (SI "d") (SF "d")])
(define_mode_attr lImmArith [(QI "4") (HI "4") (SI "6")])
(define_mode_attr lImmRotl [(QI "2") (HI "2") (SI "4")])
(define_mode_attr IJK [(QI "I") (HI "J") (SI "K")])
(define_mode_attr iF [(QI "i") (HI "i") (SI "i") (DI "i") (SF "F") (DF "F")])
(define_mode_attr JG [(QI "J") (HI "J") (SI "J") (DI "J") (SF "G") (DF "G")])
;   In HI or QI mode we push 4 bytes.
(define_mode_attr pushCnstr [(QI "X") (HI "X") (SI "<") (SF "<") (DI "<") (DF "<")])
(define_mode_attr tpush [(QI "") (HI "") (SI "") (SF "") (DI "sp, ") (DF "sp, ")])
(define_mode_attr lpush [(QI "2") (HI "2") (SI "2") (SF "2") (DI "4") (DF "4")])


;;  Code Macro Definitions

(define_code_macro sz_xtnd [sign_extend zero_extend])
(define_code_attr sIsa [(sign_extend "") (zero_extend "u")])
(define_code_attr sPat [(sign_extend "s") (zero_extend "u")])
(define_code_attr szPat [(sign_extend "") (zero_extend "zero_")])
(define_code_attr szIsa [(sign_extend "s") (zero_extend "z")])

(define_code_macro sh_oprnd [ashift ashiftrt lshiftrt])
(define_code_attr shIsa [(ashift "ll") (ashiftrt "ra") (lshiftrt "rl")])
(define_code_attr shPat [(ashift "ashl") (ashiftrt "ashr") (lshiftrt "lshr")])

(define_code_macro mima_oprnd [smax umax smin umin])
(define_code_attr mimaIsa [(smax "maxs") (umax "maxu") (smin "mins") (umin "minu")])

(define_code_macro any_cond [eq ne gt gtu lt ltu ge geu le leu])

;;  Addition Instructions

(define_insn "adddi3"
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(plus:DI (match_operand:DI 1 "register_operand" "%0,0")
		 (match_operand:DI 2 "nonmemory_operand" "r,i")))
   (clobber (reg:CC CC_REGNUM))]
  ""
  "addd\t%L2, %L1\;addcd\t%H2, %H1"
  [(set_attr "length" "4,12")]
)

(define_insn "add<mode>3"
  [(set (match_operand:CRXIM 0 "register_operand" "=r,r")
	(plus:CRXIM (match_operand:CRXIM 1 "register_operand" "%0,0")
		    (match_operand:CRXIM 2 "nonmemory_operand" "r,i")))
   (clobber (reg:CC CC_REGNUM))]
  ""
  "add<tIsa>\t%2, %0"
  [(set_attr "length" "2,<lImmArith>")]
)

;;  Subtract Instructions

(define_insn "subdi3"
  [(set (match_operand:DI 0 "register_operand" "=r,r")
	(minus:DI (match_operand:DI 1 "register_operand" "0,0")
		  (match_operand:DI 2 "nonmemory_operand" "r,i")))
   (clobber (reg:CC CC_REGNUM))]
  ""
  "subd\t%L2, %L1\;subcd\t%H2, %H1"
  [(set_attr "length" "4,12")]
)

(define_insn "sub<mode>3"
  [(set (match_operand:CRXIM 0 "register_operand" "=r,r")
	(minus:CRXIM (match_operand:CRXIM 1 "register_operand" "0,0")
		     (match_operand:CRXIM 2 "nonmemory_operand" "r,i")))
   (clobber (reg:CC CC_REGNUM))]
  ""
  "sub<tIsa>\t%2, %0"
  [(set_attr "length" "2,<lImmArith>")]
)

;;  Multiply Instructions

(define_insn "mul<mode>3"
  [(set (match_operand:CRXIM 0 "register_operand" "=r,r")
	(mult:CRXIM (match_operand:CRXIM 1 "register_operand" "%0,0")
		    (match_operand:CRXIM 2 "nonmemory_operand" "r,i")))
   (clobber (reg:CC CC_REGNUM))]
  ""
  "mul<tIsa>\t%2, %0"
  [(set_attr "length" "2,<lImmArith>")]
)

;;  Widening-multiplication Instructions

(define_insn "<sIsa>mulsidi3"
  [(set (match_operand:DI 0 "register_operand" "=k")
	(mult:DI (sz_xtnd:DI (match_operand:SI 1 "register_operand" "%r"))
		 (sz_xtnd:DI (match_operand:SI 2 "register_operand" "r"))))
   (clobber (reg:CC CC_REGNUM))]
  ""
  "mull<sPat>d\t%2, %1"
  [(set_attr "length" "4")]
)

(define_insn "<sIsa>mulhisi3"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(mult:SI (sz_xtnd:SI (match_operand:HI 1 "register_operand" "%0"))
		 (sz_xtnd:SI (match_operand:HI 2 "register_operand" "r"))))
   (clobber (reg:CC CC_REGNUM))]
  ""
  "mul<sPat>wd\t%2, %0"
  [(set_attr "length" "4")]
)

(define_insn "<sIsa>mulqihi3"
  [(set (match_operand:HI 0 "register_operand" "=r")
	(mult:HI (sz_xtnd:HI (match_operand:QI 1 "register_operand" "%0"))
		 (sz_xtnd:HI (match_operand:QI 2 "register_operand" "r"))))
   (clobber (reg:CC CC_REGNUM))]
  ""
  "mul<sPat>bw\t%2, %0"
  [(set_attr "length" "4")]
)

;;  Logical Instructions - and

(define_insn "and<mode>3"
  [(set (match_operand:CRXIM 0 "register_operand" "=r,r")
	(and:CRXIM (match_operand:CRXIM 1 "register_operand" "%0,0")
		   (match_operand:CRXIM 2 "nonmemory_operand" "r,i")))
   (clobber (reg:CC CC_REGNUM))]
  ""
  "and<tIsa>\t%2, %0"
  [(set_attr "length" "2,<lImmArith>")]
)

;;  Logical Instructions - or

(define_insn "ior<mode>3"
  [(set (match_operand:CRXIM 0 "register_operand" "=r,r")
	(ior:CRXIM (match_operand:CRXIM 1 "register_operand" "%0,0")
		   (match_operand:CRXIM 2 "nonmemory_operand" "r,i")))
   (clobber (reg:CC CC_REGNUM))]
  ""
  "or<tIsa>\t%2, %0"
  [(set_attr "length" "2,<lImmArith>")]
)

;;  Logical Instructions - xor

(define_insn "xor<mode>3"
  [(set (match_operand:CRXIM 0 "register_operand" "=r,r")
	(xor:CRXIM (match_operand:CRXIM 1 "register_operand" "%0,0")
		   (match_operand:CRXIM 2 "nonmemory_operand" "r,i")))
   (clobber (reg:CC CC_REGNUM))]
  ""
  "xor<tIsa>\t%2, %0"
  [(set_attr "length" "2,<lImmArith>")]
)

;;  Sign and Zero Extend Instructions

(define_insn "<szPat>extendhisi2"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(sz_xtnd:SI (match_operand:HI 1 "register_operand" "r")))
   (clobber (reg:CC CC_REGNUM))]
  ""
  "<szIsa>extwd\t%1, %0"
  [(set_attr "length" "4")]
)

(define_insn "<szPat>extendqisi2"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(sz_xtnd:SI (match_operand:QI 1 "register_operand" "r")))
   (clobber (reg:CC CC_REGNUM))]
  ""
  "<szIsa>extbd\t%1, %0"
  [(set_attr "length" "4")]
)

(define_insn "<szPat>extendqihi2"
  [(set (match_operand:HI 0 "register_operand" "=r")
	(sz_xtnd:HI (match_operand:QI 1 "register_operand" "r")))
   (clobber (reg:CC CC_REGNUM))]
  ""
  "<szIsa>extbw\t%1, %0"
  [(set_attr "length" "4")]
)

;;  Negation Instructions

(define_insn "neg<mode>2"
  [(set (match_operand:CRXIM 0 "register_operand" "=r")
	(neg:CRXIM (match_operand:CRXIM 1 "register_operand" "r")))
   (clobber (reg:CC CC_REGNUM))]
  ""
  "neg<tIsa>\t%1, %0"
  [(set_attr "length" "4")]
)

;;  Absolute Instructions

(define_insn "abs<mode>2"
  [(set (match_operand:CRXIM 0 "register_operand" "=r")
	(abs:CRXIM (match_operand:CRXIM 1 "register_operand" "r")))
   (clobber (reg:CC CC_REGNUM))]
  ""
  "abs<tIsa>\t%1, %0"
  [(set_attr "length" "4")]
)

;;  Max and Min Instructions

(define_insn "<code><mode>3"
  [(set (match_operand:CRXIM 0 "register_operand" "=r")
	(mima_oprnd:CRXIM (match_operand:CRXIM 1 "register_operand"  "%0")
			  (match_operand:CRXIM 2 "register_operand"  "r")))]
  ""
  "<mimaIsa><tIsa>\t%2, %0"
  [(set_attr "length" "4")]
)

;;  One's Complement

(define_insn "one_cmpl<mode>2"
  [(set (match_operand:CRXIM 0 "register_operand" "=r")
	(not:CRXIM (match_operand:CRXIM 1 "register_operand" "0")))
   (clobber (reg:CC CC_REGNUM))]
  ""
  "xor<tIsa>\t$-1, %0"
  [(set_attr "length" "2")]
)

;;  Rotate Instructions

(define_insn "rotl<mode>3"
  [(set (match_operand:CRXIM 0 "register_operand" "=r,r")
	(rotate:CRXIM (match_operand:CRXIM 1 "register_operand" "0,0")
		      (match_operand:CRXIM 2 "nonmemory_operand" "r,<IJK>")))
   (clobber (reg:CC CC_REGNUM))]
  ""
  "@
  rotl<tIsa>\t%2, %0
  rot<tIsa>\t%2, %0"
  [(set_attr "length" "4,<lImmRotl>")]
)

(define_insn "rotr<mode>3"
  [(set (match_operand:CRXIM 0 "register_operand" "=r")
	(rotatert:CRXIM (match_operand:CRXIM 1 "register_operand" "0")
			(match_operand:CRXIM 2 "register_operand" "r")))
   (clobber (reg:CC CC_REGNUM))]
  ""
  "rotr<tIsa>\t%2, %0"
  [(set_attr "length" "4")]
)

;;  Arithmetic Left and Right Shift Instructions

(define_insn "<shPat><mode>3"
  [(set (match_operand:CRXIM 0 "register_operand" "=r,r")
	(sh_oprnd:CRXIM (match_operand:CRXIM 1 "register_operand" "0,0")
			(match_operand:QI 2 "nonmemory_operand" "r,<IJK>")))
   (clobber (reg:CC CC_REGNUM))]
  ""
  "s<shIsa><tIsa>\t%2, %0"
  [(set_attr "length" "2,2")]
)

;;  Bit Set Instructions

(define_insn "extv"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(sign_extract:SI (match_operand:SI 1 "register_operand" "r")
			 (match_operand:SI 2 "const_int_operand" "n")
			 (match_operand:SI 3 "const_int_operand" "n")))]
  ""
  {
    static char buf[100];
    int strpntr;
    int size = INTVAL (operands[2]);
    int pos = INTVAL (operands[3]);
    strpntr = sprintf (buf, "ram\t$%d, $31, $%d, %%1, %%0\;",
	      BITS_PER_WORD - (size + pos), BITS_PER_WORD - size);
    sprintf (buf + strpntr, "srad\t$%d, %%0", BITS_PER_WORD - size);
    return buf;
  }
  [(set_attr "length" "6")]
)

(define_insn "extzv"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(zero_extract:SI (match_operand:SI 1 "register_operand" "r")
			 (match_operand:SI 2 "const_int_operand" "n")
			 (match_operand:SI 3 "const_int_operand" "n")))]
  ""
  {
    static char buf[40];
    int size = INTVAL (operands[2]);
    int pos = INTVAL (operands[3]);
    sprintf (buf, "ram\t$%d, $%d, $0, %%1, %%0",
	   (BITS_PER_WORD - pos) % BITS_PER_WORD, size - 1);
    return buf;
  }
  [(set_attr "length" "4")]
)

(define_insn "insv"
  [(set (zero_extract:SI (match_operand:SI 0 "register_operand" "+r")
			 (match_operand:SI 1 "const_int_operand" "n")
			 (match_operand:SI 2 "const_int_operand" "n"))
	(match_operand:SI 3 "register_operand" "r"))]
  ""
  {
    static char buf[40];
    int size = INTVAL (operands[1]);
    int pos = INTVAL (operands[2]);
    sprintf (buf, "rim\t$%d, $%d, $%d, %%3, %%0",
	    pos, size + pos - 1, pos);
    return buf;
  }
  [(set_attr "length" "4")]
)

;;  Move Instructions

(define_expand "mov<mode>"
  [(set (match_operand:ALLMT 0 "nonimmediate_operand" "")
	(match_operand:ALLMT 1 "general_operand" ""))]
  ""
  {
    if (!(reload_in_progress || reload_completed))
      {
	if (!register_operand (operands[0], <MODE>mode))
	  {
	    if (push_operand (operands[0], <MODE>mode) ?
		!nosp_reg_operand (operands[1], <MODE>mode) :
		!reg_or_u4bits_operand (operands[1], <MODE>mode))
	      {
		operands[1] = copy_to_mode_reg (<MODE>mode, operands[1]);
	      }
	  }
      }
  }
)

(define_insn "push<mode>_internal"
  [(set (match_operand:ALLMT 0 "push_operand" "=<pushCnstr>")
	(match_operand:ALLMT 1 "nosp_reg_operand" "b"))]
  ""
  "push\t<tpush>%p1"
  [(set_attr "length" "<lpush>")]
)

(define_insn "mov<mode>_regs"
  [(set (match_operand:SISFM 0 "register_operand" "=r, r, r, k")
	(match_operand:SISFM 1 "nonmemory_operand" "r, <iF>, k, r"))]
  ""
  "@
  movd\t%1, %0
  movd\t%1, %0
  mfpr\t%1, %0
  mtpr\t%1, %0"
  [(set_attr "length" "2,6,4,4")]
)

(define_insn "mov<mode>_regs"
  [(set (match_operand:DIDFM 0 "register_operand" "=r, r, r, k")
	(match_operand:DIDFM 1 "nonmemory_operand" "r, <iF>, k, r"))]
  ""
  {
    switch (which_alternative)
      {
      case 0: if (REGNO (operands[0]) > REGNO (operands[1]))
	        return "movd\t%H1, %H0\;movd\t%L1, %L0";
	      else
	        return "movd\t%L1, %L0\;movd\t%H1, %H0";
      case 1: return "movd\t%H1, %H0\;movd\t%L1, %L0";
      case 2: return "mfpr\t%H1, %H0\;mfpr\t%L1, %L0";
      case 3: return "mtpr\t%H1, %H0\;mtpr\t%L1, %L0";
      default: gcc_unreachable ();
      }
  }
  [(set_attr "length" "4,12,8,8")]
)

(define_insn "mov<mode>_regs" ; no HI/QI mode in HILO regs
  [(set (match_operand:SHORT 0 "register_operand" "=r, r")
	(match_operand:SHORT 1 "nonmemory_operand" "r, i"))]
  ""
  "mov<tIsa>\t%1, %0"
  [(set_attr "length" "2,<lImmArith>")]
)

(define_insn "mov<mode>_load"
  [(set (match_operand:CRXMM 0 "register_operand" "=r")
	(match_operand:CRXMM 1 "memory_operand" "m"))]
  ""
  "load<tIsa>\t%1, %0"
  [(set_attr "length" "6")]
)

(define_insn "mov<mode>_load"
  [(set (match_operand:DIDFM 0 "register_operand" "=r")
	(match_operand:DIDFM 1 "memory_operand" "m"))]
  ""
  {
    rtx first_dest_reg = gen_rtx_REG (SImode, REGNO (operands[0]));
    if (reg_overlap_mentioned_p (first_dest_reg, operands[1]))
      return "loadd\t%H1, %H0\;loadd\t%L1, %L0";
    return "loadd\t%L1, %L0\;loadd\t%H1, %H0";
  }
  [(set_attr "length" "12")]
)

(define_insn "mov<mode>_store"
  [(set (match_operand:CRXMM 0 "store_operand" "=m, m")
	(match_operand:CRXMM 1 "reg_or_u4bits_operand" "r, <JG>"))]
  ""
  "stor<tIsa>\t%1, %0"
  [(set_attr "length" "6")]
)

(define_insn "mov<mode>_store"
  [(set (match_operand:DIDFM 0 "store_operand" "=m, m")
	(match_operand:DIDFM 1 "reg_or_u4bits_operand" "r, <JG>"))]
  ""
  "stord\t%H1, %H0\;stord\t%L1, %L0"
  [(set_attr "length" "12")]
)

;;  Movmem Instruction

(define_expand "movmemsi"
  [(use (match_operand:BLK 0 "memory_operand" ""))
   (use (match_operand:BLK 1 "memory_operand" ""))
   (use (match_operand:SI 2 "nonmemory_operand" ""))
   (use (match_operand:SI 3 "const_int_operand" ""))]
  ""
  {
    if (crx_expand_movmem (operands[0], operands[1], operands[2], operands[3]))
      DONE;
    else
      FAIL;
  }
)

;;  Compare and Branch Instructions

(define_insn "cbranch<mode>4"
  [(set (pc)
	(if_then_else (match_operator 0 "comparison_operator"
			[(match_operand:CRXIM 1 "register_operand" "r")
			 (match_operand:CRXIM 2 "reg_or_cst4_operand" "rL")])
		      (label_ref (match_operand 3 "" ""))
		      (pc)))
   (clobber (reg:CC CC_REGNUM))]
  ""
  "cmpb%d0<tIsa>\t%2, %1, %l3"
  [(set_attr "length" "6")]
)

;;  Compare Instructions

(define_expand "cmp<mode>"
  [(set (reg:CC CC_REGNUM)
	(compare:CC (match_operand:CRXIM 0 "register_operand" "")
		    (match_operand:CRXIM 1 "nonmemory_operand" "")))]
  ""
  {
    crx_compare_op0 = operands[0];
    crx_compare_op1 = operands[1];
    DONE;
  }
)

(define_insn "cmp<mode>_internal"
  [(set (reg:CC CC_REGNUM)
	(compare:CC (match_operand:CRXIM 0 "register_operand" "r,r")
		    (match_operand:CRXIM 1 "nonmemory_operand" "r,i")))]
  ""
  "cmp<tIsa>\t%1, %0"
  [(set_attr "length" "2,<lImmArith>")]
)

;;  Conditional Branch Instructions

(define_expand "b<code>"
  [(set (pc)
	(if_then_else (any_cond (reg:CC CC_REGNUM)
				(const_int 0))
		      (label_ref (match_operand 0 ""))
		      (pc)))]
  ""
  {
    crx_expand_branch (<CODE>, operands[0]);
    DONE;
  }
)

(define_insn "bCOND_internal"
  [(set (pc)
	(if_then_else (match_operator 0 "comparison_operator"
			[(reg:CC CC_REGNUM)
			 (const_int 0)])
		      (label_ref (match_operand 1 ""))
		      (pc)))]
  ""
  "b%d0\t%l1"
  [(set_attr "length" "6")]
)

;;  Scond Instructions

(define_expand "s<code>"
  [(set (match_operand:SI 0 "register_operand")
  	(any_cond:SI (reg:CC CC_REGNUM) (const_int 0)))]
  ""
  {
    crx_expand_scond (<CODE>, operands[0]);
    DONE;
  }
)

(define_insn "sCOND_internal"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(match_operator:SI 1 "comparison_operator"
	  [(reg:CC CC_REGNUM) (const_int 0)]))]
  ""
  "s%d1\t%0"
  [(set_attr "length" "2")]
)

;;  Jumps and Branches

(define_insn "indirect_jump_return"
  [(parallel
    [(set (pc)
	  (reg:SI RA_REGNUM))
     (return)])
  ]
  "reload_completed"
  "jump\tra"
  [(set_attr "length" "2")]
)

(define_insn "indirect_jump"
  [(set (pc)
	(match_operand:SI 0 "reg_or_sym_operand" "r,i"))]
  ""
  "@
  jump\t%0
  br\t%a0"
  [(set_attr "length" "2,6")]
)

(define_insn "interrupt_return"
  [(parallel
    [(unspec_volatile [(const_int 0)] 0)
     (return)])]
  ""
  {
    return crx_prepare_push_pop_string (1);
  }
  [(set_attr "length" "14")]
)

(define_insn "jump_to_imm"
  [(set (pc)
	(match_operand 0 "immediate_operand" "i"))]
  ""
  "br\t%c0"
  [(set_attr "length" "6")]
)

(define_insn "jump"
  [(set (pc)
	(label_ref (match_operand 0 "" "")))]
  ""
  "br\t%l0"
  [(set_attr "length" "6")]
)

;;  Function Prologue and Epilogue

(define_expand "prologue"
  [(const_int 0)]
  ""
  {
    crx_expand_prologue ();
    DONE;
  }
)

(define_insn "push_for_prologue"
  [(parallel
    [(set (reg:SI SP_REGNUM)
	  (minus:SI (reg:SI SP_REGNUM)
		    (match_operand:SI 0 "immediate_operand" "i")))])]
  "reload_completed"
  {
    return crx_prepare_push_pop_string (0);
  }
  [(set_attr "length" "4")]
)

(define_expand "epilogue"
  [(return)]
  ""
  {
    crx_expand_epilogue ();
    DONE;
  }
)

(define_insn "pop_and_popret_return"
  [(parallel
    [(set (reg:SI SP_REGNUM)
	  (plus:SI (reg:SI SP_REGNUM)
		   (match_operand:SI 0 "immediate_operand" "i")))
     (use (reg:SI RA_REGNUM))
     (return)])
  ]
  "reload_completed"
  {
    return crx_prepare_push_pop_string (1);
  }
  [(set_attr "length" "4")]
)

(define_insn "popret_RA_return"
  [(parallel
    [(use (reg:SI RA_REGNUM))
     (return)])
  ]
  "reload_completed"
  "popret\tra"
  [(set_attr "length" "2")]
)

;;  Table Jump

(define_insn "tablejump"
  [(set (pc)
	(match_operand:SI 0 "register_operand" "r"))
	(use (label_ref:SI (match_operand 1 "" "" )))]
  ""
  "jump\t%0"
  [(set_attr "length" "2")]
)

;;  Call Instructions

(define_expand "call"
  [(call (match_operand:QI 0 "memory_operand" "")
	 (match_operand 1 "" ""))]
  ""
  {
    emit_call_insn (gen_crx_call (operands[0], operands[1]));
    DONE;
  }
)

(define_expand "crx_call"
  [(parallel
    [(call (match_operand:QI 0 "memory_operand" "")
	   (match_operand 1 "" ""))
     (clobber (reg:SI RA_REGNUM))])]
  ""
  ""
)

(define_insn "crx_call_insn_branch"
  [(call (mem:QI (match_operand:SI 0 "immediate_operand" "i"))
	 (match_operand 1 "" ""))
   (clobber (match_operand:SI 2 "register_operand" "+r"))]
  ""
  "bal\tra, %a0"
  [(set_attr "length" "6")]
)

(define_insn "crx_call_insn_jump"
  [(call (mem:QI (match_operand:SI 0 "register_operand" "r"))
	 (match_operand 1 "" ""))
   (clobber (match_operand:SI 2 "register_operand" "+r"))]
  ""
  "jal\t%0"
  [(set_attr "length" "2")]
)

(define_insn "crx_call_insn_jalid"
  [(call (mem:QI (mem:SI (plus:SI
			   (match_operand:SI 0 "register_operand" "r")
			   (match_operand:SI 1 "register_operand" "r"))))
	 (match_operand 2 "" ""))
   (clobber (match_operand:SI 3 "register_operand" "+r"))]
  ""
  "jalid\t%0, %1"
  [(set_attr "length" "4")]
)

;;  Call Value Instructions

(define_expand "call_value"
  [(set (match_operand 0 "general_operand" "")
	(call (match_operand:QI 1 "memory_operand" "")
	      (match_operand 2 "" "")))]
  ""
  {
    emit_call_insn (gen_crx_call_value (operands[0], operands[1], operands[2]));
    DONE;
  }
)

(define_expand "crx_call_value"
  [(parallel
    [(set (match_operand 0 "general_operand" "")
	  (call (match_operand 1 "memory_operand" "")
		(match_operand 2 "" "")))
     (clobber (reg:SI RA_REGNUM))])]
  ""
  ""
)

(define_insn "crx_call_value_insn_branch"
  [(set (match_operand 0 "" "=g")
	(call (mem:QI (match_operand:SI 1 "immediate_operand" "i"))
	      (match_operand 2 "" "")))
   (clobber (match_operand:SI 3 "register_operand" "+r"))]
  ""
  "bal\tra, %a1"
  [(set_attr "length" "6")]
)

(define_insn "crx_call_value_insn_jump"
  [(set (match_operand 0 "" "=g")
	(call (mem:QI (match_operand:SI 1 "register_operand" "r"))
	      (match_operand 2 "" "")))
   (clobber (match_operand:SI 3 "register_operand" "+r"))]
  ""
  "jal\t%1"
  [(set_attr "length" "2")]
)

(define_insn "crx_call_value_insn_jalid"
  [(set (match_operand 0 "" "=g")
	(call (mem:QI (mem:SI (plus:SI
				(match_operand:SI 1 "register_operand" "r")
				(match_operand:SI 2 "register_operand" "r"))))
	      (match_operand 3 "" "")))
   (clobber (match_operand:SI 4 "register_operand" "+r"))]
  ""
  "jalid\t%0, %1"
  [(set_attr "length" "4")]
)

;;  Nop

(define_insn "nop"
  [(const_int 0)]
  ""
  ""
)

;;  Multiply and Accumulate Instructions

(define_insn "<sPat>madsidi3"
  [(set (match_operand:DI 0 "register_operand" "+k")
	(plus:DI
	  (mult:DI (sz_xtnd:DI (match_operand:SI 1 "register_operand" "%r"))
		   (sz_xtnd:DI (match_operand:SI 2 "register_operand" "r")))
	  (match_dup 0)))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_MAC"
  "mac<sPat>d\t%2, %1"
  [(set_attr "length" "4")]
)

(define_insn "<sPat>madhisi3"
  [(set (match_operand:SI 0 "register_operand" "+l")
	(plus:SI
	  (mult:SI (sz_xtnd:SI (match_operand:HI 1 "register_operand" "%r"))
		   (sz_xtnd:SI (match_operand:HI 2 "register_operand" "r")))
	  (match_dup 0)))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_MAC"
  "mac<sPat>w\t%2, %1"
  [(set_attr "length" "4")]
)

(define_insn "<sPat>madqihi3"
  [(set (match_operand:HI 0 "register_operand" "+l")
	(plus:HI
	  (mult:HI (sz_xtnd:HI (match_operand:QI 1 "register_operand" "%r"))
		   (sz_xtnd:HI (match_operand:QI 2 "register_operand" "r")))
	  (match_dup 0)))
   (clobber (reg:CC CC_REGNUM))]
  "TARGET_MAC"
  "mac<sPat>b\t%2, %1"
  [(set_attr "length" "4")]
)

;;  Loop Instructions

(define_expand "doloop_end"
  [(use (match_operand 0 "" ""))	; loop pseudo
   (use (match_operand 1 "" ""))	; iterations; zero if unknown
   (use (match_operand 2 "" ""))	; max iterations
   (use (match_operand 3 "" ""))	; loop level
   (use (match_operand 4 "" ""))]       ; label
  ""
  {
    if (INTVAL (operands[3]) > crx_loop_nesting)
      FAIL;
    switch (GET_MODE (operands[0]))
      {
      case SImode:
	emit_jump_insn (gen_doloop_end_si (operands[4], operands[0]));
	break;
      case HImode:
	emit_jump_insn (gen_doloop_end_hi (operands[4], operands[0]));
	break;
      case QImode:
	emit_jump_insn (gen_doloop_end_qi (operands[4], operands[0]));
	break;
      default:
	FAIL;
      }
    DONE;
  }
)

;   CRX dbnz[bwd] used explicitly (see above) but also by the combiner.

(define_insn "doloop_end_<mode>"
  [(set (pc)
	(if_then_else (ne (match_operand:CRXIM 1 "register_operand" "+r,!m")
			  (const_int 1))
		      (label_ref (match_operand 0 "" ""))
		      (pc)))
   (set (match_dup 1) (plus:CRXIM (match_dup 1) (const_int -1)))
   (clobber (match_scratch:CRXIM 2 "=X,r"))
   (clobber (reg:CC CC_REGNUM))]
  ""
  "@
  dbnz<tIsa>\t%1, %l0
  load<tIsa>\t%1, %2\;add<tIsa>\t$-1, %2\;stor<tIsa>\t%2, %1\;bne\t%l0"
  [(set_attr "length" "6, 12")]
)
