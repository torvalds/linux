;; Predicate definitions for SPARC.
;; Copyright (C) 2005 Free Software Foundation, Inc.
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

;; Predicates for numerical constants.

;; Return true if OP is the zero constant for MODE.
(define_predicate "const_zero_operand"
  (and (match_code "const_int,const_double,const_vector")
       (match_test "op == CONST0_RTX (mode)")))

;; Return true if OP is the one constant for MODE.
(define_predicate "const_one_operand"
  (and (match_code "const_int,const_double,const_vector")
       (match_test "op == CONST1_RTX (mode)")))

;; Return true if OP is the integer constant 4096.
(define_predicate "const_4096_operand"
  (and (match_code "const_int")
       (match_test "INTVAL (op) == 4096")))

;; Return true if OP is a constant that is representable by a 13-bit
;; signed field.  This is an acceptable immediate operand for most
;; 3-address instructions.
(define_predicate "small_int_operand"
  (and (match_code "const_int")
       (match_test "SPARC_SIMM13_P (INTVAL (op))")))

;; Return true if OP is a constant operand for the umul instruction.  That
;; instruction sign-extends immediate values just like all other SPARC
;; instructions, but interprets the extended result as an unsigned number.
(define_predicate "uns_small_int_operand"
  (match_code "const_int,const_double")
{
#if HOST_BITS_PER_WIDE_INT == 32
  return ((GET_CODE (op) == CONST_INT && (unsigned) INTVAL (op) < 0x1000)
	  || (GET_CODE (op) == CONST_DOUBLE
	      && CONST_DOUBLE_HIGH (op) == 0
	      && (unsigned) CONST_DOUBLE_LOW (op) - 0xFFFFF000 < 0x1000));
#else
  return (GET_CODE (op) == CONST_INT
	  && ((INTVAL (op) >= 0 && INTVAL (op) < 0x1000)
	      || (INTVAL (op) >= 0xFFFFF000
                  && INTVAL (op) <= 0xFFFFFFFF)));
#endif
})

;; Return true if OP is a constant that can be loaded by the sethi instruction.
;; The first test avoids emitting sethi to load zero for example.
(define_predicate "const_high_operand"
  (and (match_code "const_int")
       (and (not (match_operand 0 "small_int_operand"))
            (match_test "SPARC_SETHI_P (INTVAL (op) & GET_MODE_MASK (mode))"))))

;; Return true if OP is a constant whose 1's complement can be loaded by the
;; sethi instruction.
(define_predicate "const_compl_high_operand"
  (and (match_code "const_int")
       (and (not (match_operand 0 "small_int_operand"))
            (match_test "SPARC_SETHI_P (~INTVAL (op) & GET_MODE_MASK (mode))"))))

;; Return true if OP is a FP constant that needs to be loaded by the sethi/losum
;; pair of instructions.
(define_predicate "fp_const_high_losum_operand"
  (match_operand 0 "const_double_operand")
{
  gcc_assert (mode == SFmode);
  return fp_high_losum_p (op);
})


;; Predicates for symbolic constants.

;; Return true if OP is either a symbol reference or a sum of a symbol
;; reference and a constant.
(define_predicate "symbolic_operand"
  (match_code "symbol_ref,label_ref,const")
{
  enum machine_mode omode = GET_MODE (op);

  if (omode != mode && omode != VOIDmode && mode != VOIDmode)
    return false;

  switch (GET_CODE (op))
    {
    case SYMBOL_REF:
      return !SYMBOL_REF_TLS_MODEL (op);

    case LABEL_REF:
      return true;

    case CONST:
      op = XEXP (op, 0);
      return (((GET_CODE (XEXP (op, 0)) == SYMBOL_REF
		&& !SYMBOL_REF_TLS_MODEL (XEXP (op, 0)))
	       || GET_CODE (XEXP (op, 0)) == LABEL_REF)
	      && GET_CODE (XEXP (op, 1)) == CONST_INT);

    default:
      gcc_unreachable ();
    }
})

;; Return true if OP is a symbolic operand for the TLS Global Dynamic model.
(define_predicate "tgd_symbolic_operand"
  (and (match_code "symbol_ref")
       (match_test "SYMBOL_REF_TLS_MODEL (op) == TLS_MODEL_GLOBAL_DYNAMIC")))

;; Return true if OP is a symbolic operand for the TLS Local Dynamic model.
(define_predicate "tld_symbolic_operand"
  (and (match_code "symbol_ref")
       (match_test "SYMBOL_REF_TLS_MODEL (op) == TLS_MODEL_LOCAL_DYNAMIC")))

;; Return true if OP is a symbolic operand for the TLS Initial Exec model.
(define_predicate "tie_symbolic_operand"
  (and (match_code "symbol_ref")
       (match_test "SYMBOL_REF_TLS_MODEL (op) == TLS_MODEL_INITIAL_EXEC")))

;; Return true if OP is a symbolic operand for the TLS Local Exec model.
(define_predicate "tle_symbolic_operand"
  (and (match_code "symbol_ref")
       (match_test "SYMBOL_REF_TLS_MODEL (op) == TLS_MODEL_LOCAL_EXEC")))

;; Return true if the operand is an argument used in generating PIC references
;; in either the medium/low or embedded medium/anywhere code models on V9.
;; Check for (const (minus (symbol_ref:GOT)
;;                         (const (minus (label) (pc)))))
(define_predicate "medium_pic_operand"
  (match_code "const")
{
  /* Check for (const (minus (symbol_ref:GOT)
                             (const (minus (label) (pc))))).  */
  op = XEXP (op, 0);
  return GET_CODE (op) == MINUS
         && GET_CODE (XEXP (op, 0)) == SYMBOL_REF
         && GET_CODE (XEXP (op, 1)) == CONST
         && GET_CODE (XEXP (XEXP (op, 1), 0)) == MINUS;
})

;; Return true if OP is a LABEL_REF of mode MODE.
(define_predicate "label_ref_operand"
  (and (match_code "label_ref")
       (match_test "GET_MODE (op) == mode")))

;; Return true if OP is a data segment reference.  This includes the readonly
;; data segment or, in other words, anything but the text segment.
;; This is needed in the embedded medium/anywhere code model on V9.  These
;; values are accessed with EMBMEDANY_BASE_REG.  */
(define_predicate "data_segment_operand"
  (match_code "symbol_ref,plus,const")
{
  switch (GET_CODE (op))
    {
    case SYMBOL_REF :
      return ! SYMBOL_REF_FUNCTION_P (op);
    case PLUS :
      /* Assume canonical format of symbol + constant.
	 Fall through.  */
    case CONST :
      return data_segment_operand (XEXP (op, 0), VOIDmode);
    default :
      gcc_unreachable ();
    }
})

;; Return true if OP is a text segment reference.
;; This is needed in the embedded medium/anywhere code model on V9.
(define_predicate "text_segment_operand"
  (match_code "label_ref,symbol_ref,plus,const")
{
  switch (GET_CODE (op))
    {
    case LABEL_REF :
      return true;
    case SYMBOL_REF :
      return SYMBOL_REF_FUNCTION_P (op);
    case PLUS :
      /* Assume canonical format of symbol + constant.
	 Fall through.  */
    case CONST :
      return text_segment_operand (XEXP (op, 0), VOIDmode);
    default :
      gcc_unreachable ();
    }
})


;; Predicates for registers.

;; Return true if OP is either the zero constant or a register.
(define_predicate "register_or_zero_operand"
  (ior (match_operand 0 "register_operand")
       (match_operand 0 "const_zero_operand")))

;; Return true if OP is a register operand in a floating point register.
(define_predicate "fp_register_operand"
  (match_operand 0 "register_operand")
{
  if (GET_CODE (op) == SUBREG)
    op = SUBREG_REG (op); /* Possibly a MEM */
  return REG_P (op) && SPARC_FP_REG_P (REGNO (op));
})

;; Return true if OP is an integer register.
(define_special_predicate "int_register_operand"
  (ior (match_test "register_operand (op, SImode)")
       (match_test "TARGET_ARCH64 && register_operand (op, DImode)")))

;; Return true if OP is a floating point condition code register.
(define_predicate "fcc_register_operand"
  (match_code "reg")
{
  if (mode != VOIDmode && mode != GET_MODE (op))
    return false;
  if (mode == VOIDmode
      && (GET_MODE (op) != CCFPmode && GET_MODE (op) != CCFPEmode))
    return false;

#if 0 /* ??? 1 when %fcc0-3 are pseudos first.  See gen_compare_reg().  */
  if (reg_renumber == 0)
    return REGNO (op) >= FIRST_PSEUDO_REGISTER;
  return REGNO_OK_FOR_CCFP_P (REGNO (op));
#else
  return ((unsigned) REGNO (op) - SPARC_FIRST_V9_FCC_REG) < 4;
#endif
})

;; Return true if OP is the floating point condition code register fcc0.
(define_predicate "fcc0_register_operand"
  (match_code "reg")
{
  if (mode != VOIDmode && mode != GET_MODE (op))
    return false;
  if (mode == VOIDmode
      && (GET_MODE (op) != CCFPmode && GET_MODE (op) != CCFPEmode))
    return false;

  return REGNO (op) == SPARC_FCC_REG;
})

;; Return true if OP is an integer or floating point condition code register.
(define_predicate "icc_or_fcc_register_operand"
  (match_code "reg")
{
  if (REGNO (op) == SPARC_ICC_REG)
    {
      if (mode != VOIDmode && mode != GET_MODE (op))
	return false;
      if (mode == VOIDmode
	  && GET_MODE (op) != CCmode && GET_MODE (op) != CCXmode)
	return false;

      return true;
    }

  return fcc_register_operand (op, mode);
})


;; Predicates for arithmetic instructions.

;; Return true if OP is a register, or is a constant that is representable
;; by a 13-bit signed field.  This is an acceptable operand for most
;; 3-address instructions.
(define_predicate "arith_operand"
  (ior (match_operand 0 "register_operand")
       (match_operand 0 "small_int_operand")))

;; 64-bit: Same as above.
;; 32-bit: Return true if OP is a register, or is a constant that is 
;; representable by a couple of 13-bit signed fields.  This is an
;; acceptable operand for most 3-address splitters.
(define_predicate "arith_double_operand"
  (match_code "const_int,const_double,reg,subreg")
{
  bool arith_simple_operand = arith_operand (op, mode);
  HOST_WIDE_INT m1, m2;

  if (TARGET_ARCH64 || arith_simple_operand)
    return arith_simple_operand;

#if HOST_BITS_PER_WIDE_INT == 32
  if (GET_CODE (op) != CONST_DOUBLE)
    return false;
  m1 = CONST_DOUBLE_LOW (op);
  m2 = CONST_DOUBLE_HIGH (op);
#else
  if (GET_CODE (op) != CONST_INT)
    return false;
  m1 = trunc_int_for_mode (INTVAL (op), SImode);
  m2 = trunc_int_for_mode (INTVAL (op) >> 32, SImode);
#endif

  return SPARC_SIMM13_P (m1) && SPARC_SIMM13_P (m2);
})

;; Return true if OP is suitable as second operand for add/sub.
(define_predicate "arith_add_operand"
  (ior (match_operand 0 "arith_operand")
       (match_operand 0 "const_4096_operand")))
       
;; Return true if OP is suitable as second double operand for add/sub.
(define_predicate "arith_double_add_operand"
  (match_code "const_int,const_double,reg,subreg")
{
  bool _arith_double_operand = arith_double_operand (op, mode);

  if (_arith_double_operand)
    return true;

  return TARGET_ARCH64 && const_4096_operand (op, mode);
})

;; Return true if OP is a register, or is a CONST_INT that can fit in a
;; signed 10-bit immediate field.  This is an acceptable SImode operand for
;; the movrcc instructions.
(define_predicate "arith10_operand"
  (ior (match_operand 0 "register_operand")
       (and (match_code "const_int")
            (match_test "SPARC_SIMM10_P (INTVAL (op))"))))

;; Return true if OP is a register, or is a CONST_INT that can fit in a
;; signed 11-bit immediate field.  This is an acceptable SImode operand for
;; the movcc instructions.
(define_predicate "arith11_operand"
  (ior (match_operand 0 "register_operand")
       (and (match_code "const_int")
            (match_test "SPARC_SIMM11_P (INTVAL (op))"))))

;; Return true if OP is a register or a constant for the umul instruction.
(define_predicate "uns_arith_operand"
  (ior (match_operand 0 "register_operand")
       (match_operand 0 "uns_small_int_operand")))


;; Predicates for miscellaneous instructions.

;; Return true if OP is valid for the lhs of a comparison insn.
(define_predicate "compare_operand"
  (match_code "reg,subreg,zero_extract")
{
  if (GET_CODE (op) == ZERO_EXTRACT)
    return (register_operand (XEXP (op, 0), mode)
	    && small_int_operand (XEXP (op, 1), mode)
	    && small_int_operand (XEXP (op, 2), mode)
	    /* This matches cmp_zero_extract.  */
	    && ((mode == SImode
		 && INTVAL (XEXP (op, 2)) > 19)
		/* This matches cmp_zero_extract_sp64.  */
		|| (TARGET_ARCH64
		    && mode == DImode
		    && INTVAL (XEXP (op, 2)) > 51)));
  else
    return register_operand (op, mode);
})

;; Return true if OP is a valid operand for the source of a move insn.
(define_predicate "input_operand"
  (match_code "const_int,const_double,const_vector,reg,subreg,mem")
{
  enum mode_class mclass;

  /* If both modes are non-void they must be the same.  */
  if (mode != VOIDmode && GET_MODE (op) != VOIDmode && mode != GET_MODE (op))
    return false;

  mclass = GET_MODE_CLASS (mode);

  /* Allow any 1-instruction integer constant.  */
  if (mclass == MODE_INT
      && (small_int_operand (op, mode) || const_high_operand (op, mode)))
    return true;

  /* If 32-bit mode and this is a DImode constant, allow it
     so that the splits can be generated.  */
  if (TARGET_ARCH32
      && mode == DImode
      && (GET_CODE (op) == CONST_DOUBLE || GET_CODE (op) == CONST_INT))
    return true;

  if ((mclass == MODE_FLOAT && GET_CODE (op) == CONST_DOUBLE)
      || (mclass == MODE_VECTOR_INT && GET_CODE (op) == CONST_VECTOR))
    return true;

  if (register_operand (op, mode))
    return true;

  /* If this is a SUBREG, look inside so that we handle paradoxical ones.  */
  if (GET_CODE (op) == SUBREG)
    op = SUBREG_REG (op);

  /* Check for valid MEM forms.  */
  if (GET_CODE (op) == MEM)
    return memory_address_p (mode, XEXP (op, 0));

  return false;
})

;; Return true if OP is an address suitable for a call insn.
;; Call insn on SPARC can take a PC-relative constant address
;; or any regular memory address.
(define_predicate "call_address_operand"
  (ior (match_operand 0 "symbolic_operand")
       (match_test "memory_address_p (Pmode, op)")))

;; Return true if OP is an operand suitable for a call insn.
(define_predicate "call_operand"
  (and (match_code "mem")
       (match_test "call_address_operand (XEXP (op, 0), mode)")))


;; Predicates for operators.

;; Return true if OP is a comparison operator.  This allows the use of
;; MATCH_OPERATOR to recognize all the branch insns.
(define_predicate "noov_compare_operator"
  (match_code "ne,eq,ge,gt,le,lt,geu,gtu,leu,ltu")
{
  enum rtx_code code = GET_CODE (op);
  if (GET_MODE (XEXP (op, 0)) == CC_NOOVmode
      || GET_MODE (XEXP (op, 0)) == CCX_NOOVmode)
    /* These are the only branches which work with CC_NOOVmode.  */
    return (code == EQ || code == NE || code == GE || code == LT);
  return true;
})

;; Return true if OP is a 64-bit comparison operator.  This allows the use of
;; MATCH_OPERATOR to recognize all the branch insns.
(define_predicate "noov_compare64_operator"
  (and (match_code "ne,eq,ge,gt,le,lt,geu,gtu,leu,ltu")
       (match_test "TARGET_V9"))
{
  enum rtx_code code = GET_CODE (op);
  if (GET_MODE (XEXP (op, 0)) == CCX_NOOVmode)
    /* These are the only branches which work with CCX_NOOVmode.  */
    return (code == EQ || code == NE || code == GE || code == LT);
  return (GET_MODE (XEXP (op, 0)) == CCXmode);
})

;; Return true if OP is a comparison operator suitable for use in V9
;; conditional move or branch on register contents instructions.
(define_predicate "v9_register_compare_operator"
  (match_code "eq,ne,ge,lt,le,gt"))

;; Return true if OP is an operator which can set the condition codes
;; explicitly.  We do not include PLUS and MINUS because these
;; require CC_NOOVmode, which we handle explicitly.
(define_predicate "cc_arith_operator"
  (match_code "and,ior,xor"))

;; Return true if OP is an operator which can bitwise complement its
;; second operand and set the condition codes explicitly.
;; XOR is not here because combine canonicalizes (xor (not ...) ...)
;; and (xor ... (not ...)) to (not (xor ...)).  */
(define_predicate "cc_arith_not_operator"
  (match_code "and,ior"))

;; Return true if OP is memory operand with just [%reg] addressing mode.
(define_predicate "memory_reg_operand"
  (and (match_code "mem")
       (and (match_operand 0 "memory_operand")
	    (match_test "REG_P (XEXP (op, 0))"))))
