;; Predicate definitions for HP PA-RISC.
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

;; Return nonzero only if OP is a register of mode MODE, or
;; CONST0_RTX.

(define_predicate "reg_or_0_operand"
  (match_code "subreg,reg,const_int,const_double")
{
  return (op == CONST0_RTX (mode) || register_operand (op, mode));
})

;; Return nonzero if OP is suitable for use in a call to a named
;; function.
;;
;; For 2.5 try to eliminate either call_operand_address or
;; function_label_operand, they perform very similar functions.

(define_predicate "call_operand_address"
  (match_code "label_ref,symbol_ref,const_int,const_double,const,high")
{
  return (GET_MODE (op) == word_mode
	  && CONSTANT_P (op) && ! TARGET_PORTABLE_RUNTIME);
})

;; Return 1 iff OP is an indexed memory operand.

(define_predicate "indexed_memory_operand"
  (match_code "subreg,mem")
{
  if (GET_MODE (op) != mode)
    return 0;

  /* Before reload, a (SUBREG (MEM...)) forces reloading into a register.  */
  if (reload_completed && GET_CODE (op) == SUBREG)
    op = SUBREG_REG (op);

  if (GET_CODE (op) != MEM || symbolic_memory_operand (op, mode))
    return 0;

  op = XEXP (op, 0);

  return (memory_address_p (mode, op) && IS_INDEX_ADDR_P (op));
})

;; Return 1 iff OP is a symbolic operand.
;; Note: an inline copy of this code is present in pa_secondary_reload.

(define_predicate "symbolic_operand"
  (match_code "symbol_ref,label_ref,const")
{
  switch (GET_CODE (op))
    {
    case SYMBOL_REF:
      return !SYMBOL_REF_TLS_MODEL (op);
    case LABEL_REF:
      return 1;
    case CONST:
      op = XEXP (op, 0);
      return (((GET_CODE (XEXP (op, 0)) == SYMBOL_REF
                && !SYMBOL_REF_TLS_MODEL (XEXP (op, 0)))
	       || GET_CODE (XEXP (op, 0)) == LABEL_REF)
	      && GET_CODE (XEXP (op, 1)) == CONST_INT);
    default:
      return 0;
    }
})

;; Return truth value of statement that OP is a symbolic memory
;; operand of mode MODE.

(define_predicate "symbolic_memory_operand"
  (match_code "subreg,mem")
{
  if (GET_CODE (op) == SUBREG)
    op = SUBREG_REG (op);
  if (GET_CODE (op) != MEM)
    return 0;
  op = XEXP (op, 0);
  return ((GET_CODE (op) == SYMBOL_REF && !SYMBOL_REF_TLS_MODEL (op))
  	 || GET_CODE (op) == CONST || GET_CODE (op) == HIGH 
	 || GET_CODE (op) == LABEL_REF);
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


;; Return 1 if the operand is a register operand or a non-symbolic
;; memory operand after reload.  This predicate is used for branch
;; patterns that internally handle register reloading.  We need to
;; accept non-symbolic memory operands after reload to ensure that the
;; pattern is still valid if reload didn't find a hard register for
;; the operand.

(define_predicate "reg_before_reload_operand"
  (match_code "reg,mem")
{
  /* Don't accept a SUBREG since it will need a reload.  */
  if (GET_CODE (op) == SUBREG)
    return 0;

  if (register_operand (op, mode))
    return 1;

  if (reload_completed
      && memory_operand (op, mode)
      && !symbolic_memory_operand (op, mode))
    return 1;

  return 0;
})

;; Return 1 if the operand is either a register, zero, or a memory
;; operand that is not symbolic.

(define_predicate "reg_or_0_or_nonsymb_mem_operand"
  (match_code "subreg,reg,mem,const_int,const_double")
{
  if (register_operand (op, mode))
    return 1;

  if (op == CONST0_RTX (mode))
    return 1;

  if (GET_CODE (op) == SUBREG)
    op = SUBREG_REG (op);

  if (GET_CODE (op) != MEM)
    return 0;

  /* Until problems with management of the REG_POINTER flag are resolved,
     we need to delay creating move insns with unscaled indexed addresses
     until CSE is not expected.  */
  if (!TARGET_NO_SPACE_REGS
      && !cse_not_expected
      && GET_CODE (XEXP (op, 0)) == PLUS
      && REG_P (XEXP (XEXP (op, 0), 0))
      && REG_P (XEXP (XEXP (op, 0), 1)))
    return 0;

  return (!symbolic_memory_operand (op, mode)
	  && memory_address_p (mode, XEXP (op, 0)));
})

;; Accept anything that can be used as a destination operand for a
;; move instruction.  We don't accept indexed memory operands since
;; they are supported only for floating point stores.

(define_predicate "move_dest_operand"
  (match_code "subreg,reg,mem")
{
  if (register_operand (op, mode))
    return 1;

  if (GET_MODE (op) != mode)
    return 0;

  if (GET_CODE (op) == SUBREG)
    op = SUBREG_REG (op);

  if (GET_CODE (op) != MEM || symbolic_memory_operand (op, mode))
    return 0;

  op = XEXP (op, 0);

  return (memory_address_p (mode, op)
	  && !IS_INDEX_ADDR_P (op)
	  && !IS_LO_SUM_DLT_ADDR_P (op));
})

;; Accept anything that can be used as a source operand for a move
;; instruction.

(define_predicate "move_src_operand"
  (match_code "subreg,reg,const_int,const_double,mem")
{
  if (register_operand (op, mode))
    return 1;

  if (op == CONST0_RTX (mode))
    return 1;

  if (GET_CODE (op) == CONST_INT)
    return cint_ok_for_move (INTVAL (op));

  if (GET_MODE (op) != mode)
    return 0;

  if (GET_CODE (op) == SUBREG)
    op = SUBREG_REG (op);

  if (GET_CODE (op) != MEM)
    return 0;

  /* Until problems with management of the REG_POINTER flag are resolved,
     we need to delay creating move insns with unscaled indexed addresses
     until CSE is not expected.  */
  if (!TARGET_NO_SPACE_REGS
      && !cse_not_expected
      && GET_CODE (XEXP (op, 0)) == PLUS
      && REG_P (XEXP (XEXP (op, 0), 0))
      && REG_P (XEXP (XEXP (op, 0), 1)))
    return 0;

  return memory_address_p (mode, XEXP (op, 0));
})

;; Accept anything that can be used as the source operand for a
;; prefetch instruction with a cache-control completer.

(define_predicate "prefetch_cc_operand"
  (match_code "mem")
{
  if (GET_CODE (op) != MEM)
    return 0;

  op = XEXP (op, 0);

  /* We must reject virtual registers as we don't allow REG+D.  */
  if (op == virtual_incoming_args_rtx
      || op == virtual_stack_vars_rtx
      || op == virtual_stack_dynamic_rtx
      || op == virtual_outgoing_args_rtx
      || op == virtual_cfa_rtx)
    return 0;

  if (!REG_P (op) && !IS_INDEX_ADDR_P (op))
    return 0;

  /* Until problems with management of the REG_POINTER flag are resolved,
     we need to delay creating prefetch insns with unscaled indexed addresses
     until CSE is not expected.  */
  if (!TARGET_NO_SPACE_REGS
      && !cse_not_expected
      && GET_CODE (op) == PLUS
      && REG_P (XEXP (op, 0)))
    return 0;

  return memory_address_p (mode, op);
})

;; Accept anything that can be used as the source operand for a
;; prefetch instruction with no cache-control completer.

(define_predicate "prefetch_nocc_operand"
  (match_code "mem")
{
  if (GET_CODE (op) != MEM)
    return 0;

  op = XEXP (op, 0);

  /* Until problems with management of the REG_POINTER flag are resolved,
     we need to delay creating prefetch insns with unscaled indexed addresses
     until CSE is not expected.  */
  if (!TARGET_NO_SPACE_REGS
      && !cse_not_expected
      && GET_CODE (op) == PLUS
      && REG_P (XEXP (op, 0))
      && REG_P (XEXP (op, 1)))
    return 0;

  return memory_address_p (mode, op);
})

;; Accept REG and any CONST_INT that can be moved in one instruction
;; into a general register.

(define_predicate "reg_or_cint_move_operand"
  (match_code "subreg,reg,const_int")
{
  if (register_operand (op, mode))
    return 1;

  return (GET_CODE (op) == CONST_INT && cint_ok_for_move (INTVAL (op)));
})

;; TODO: Add a comment here.

(define_predicate "pic_label_operand"
  (match_code "label_ref,const")
{
  if (!flag_pic)
    return 0;

  switch (GET_CODE (op))
    {
    case LABEL_REF:
      return 1;
    case CONST:
      op = XEXP (op, 0);
      return (GET_CODE (XEXP (op, 0)) == LABEL_REF
	      && GET_CODE (XEXP (op, 1)) == CONST_INT);
    default:
      return 0;
    }
})

;; TODO: Add a comment here.

(define_predicate "fp_reg_operand"
  (match_code "reg")
{
  return reg_renumber && FP_REG_P (op);
})

;; Return truth value of whether OP can be used as an operand in a
;; three operand arithmetic insn that accepts registers of mode MODE
;; or 14-bit signed integers.

(define_predicate "arith_operand"
  (match_code "subreg,reg,const_int")
{
  return (register_operand (op, mode)
	  || (GET_CODE (op) == CONST_INT && INT_14_BITS (op)));
})

;; Return truth value of whether OP can be used as an operand in a
;; three operand arithmetic insn that accepts registers of mode MODE
;; or 11-bit signed integers.

(define_predicate "arith11_operand"
  (match_code "subreg,reg,const_int")
{
  return (register_operand (op, mode)
	  || (GET_CODE (op) == CONST_INT && INT_11_BITS (op)));
})

;; A constant integer suitable for use in a PRE_MODIFY memory
;; reference.

(define_predicate "pre_cint_operand"
  (match_code "const_int")
{
  return (GET_CODE (op) == CONST_INT
	  && INTVAL (op) >= -0x2000 && INTVAL (op) < 0x10);
})

;; A constant integer suitable for use in a POST_MODIFY memory
;; reference.

(define_predicate "post_cint_operand"
  (match_code "const_int")
{
  return (GET_CODE (op) == CONST_INT
	  && INTVAL (op) < 0x2000 && INTVAL (op) >= -0x10);
})

;; TODO: Add a comment here.

(define_predicate "arith_double_operand"
  (match_code "subreg,reg,const_double")
{
  return (register_operand (op, mode)
	  || (GET_CODE (op) == CONST_DOUBLE
	      && GET_MODE (op) == mode
	      && VAL_14_BITS_P (CONST_DOUBLE_LOW (op))
	      && ((CONST_DOUBLE_HIGH (op) >= 0)
		  == ((CONST_DOUBLE_LOW (op) & 0x1000) == 0))));
})

;; Return truth value of whether OP is an integer which fits the range
;; constraining immediate operands in three-address insns, or is an
;; integer register.

(define_predicate "ireg_or_int5_operand"
  (match_code "const_int,reg")
{
  return ((GET_CODE (op) == CONST_INT && INT_5_BITS (op))
	  || (GET_CODE (op) == REG && REGNO (op) > 0 && REGNO (op) < 32));
})

;; Return truth value of whether OP is an integer which fits the range
;; constraining immediate operands in three-address insns.

(define_predicate "int5_operand"
  (match_code "const_int")
{
  return (GET_CODE (op) == CONST_INT && INT_5_BITS (op));
})

;; Return truth value of whether OP is an integer which fits the range
;; constraining immediate operands in three-address insns.

(define_predicate "uint5_operand"
  (match_code "const_int")
{
  return (GET_CODE (op) == CONST_INT && INT_U5_BITS (op));
})

;; Return truth value of whether OP is an integer which fits the range
;; constraining immediate operands in three-address insns.

(define_predicate "int11_operand"
  (match_code "const_int")
{
  return (GET_CODE (op) == CONST_INT && INT_11_BITS (op));
})

;; Return truth value of whether OP is an integer which fits the range
;; constraining immediate operands in three-address insns.

(define_predicate "uint32_operand"
  (match_code "const_int,const_double")
{
#if HOST_BITS_PER_WIDE_INT > 32
  /* All allowed constants will fit a CONST_INT.  */
  return (GET_CODE (op) == CONST_INT
	  && (INTVAL (op) >= 0 && INTVAL (op) < (HOST_WIDE_INT) 1 << 32));
#else
  return (GET_CODE (op) == CONST_INT
	  || (GET_CODE (op) == CONST_DOUBLE
	      && CONST_DOUBLE_HIGH (op) == 0));
#endif
})

;; Return truth value of whether OP is an integer which fits the range
;; constraining immediate operands in three-address insns.

(define_predicate "arith5_operand"
  (match_code "subreg,reg,const_int")
{
  return register_operand (op, mode) || int5_operand (op, mode);
})

;; True iff depi or extru can be used to compute (reg & OP).

(define_predicate "and_operand"
  (match_code "subreg,reg,const_int")
{
  return (register_operand (op, mode)
	  || (GET_CODE (op) == CONST_INT && and_mask_p (INTVAL (op))));
})

;; True iff depi can be used to compute (reg | OP).

(define_predicate "ior_operand"
  (match_code "const_int")
{
  return (GET_CODE (op) == CONST_INT && ior_mask_p (INTVAL (op)));
})

;; True iff OP is a CONST_INT of the forms 0...0xxxx or
;; 0...01...1xxxx. Such values can be the left hand side x in (x <<
;; r), using the zvdepi instruction.

(define_predicate "lhs_lshift_cint_operand"
  (match_code "const_int")
{
  unsigned HOST_WIDE_INT x;
  if (GET_CODE (op) != CONST_INT)
    return 0;
  x = INTVAL (op) >> 4;
  return (x & (x + 1)) == 0;
})

;; TODO: Add a comment here.

(define_predicate "lhs_lshift_operand"
  (match_code "subreg,reg,const_int")
{
  return register_operand (op, mode) || lhs_lshift_cint_operand (op, mode);
})

;; TODO: Add a comment here.

(define_predicate "arith32_operand"
  (match_code "subreg,reg,const_int")
{
  return register_operand (op, mode) || GET_CODE (op) == CONST_INT;
})

;; TODO: Add a comment here.

(define_predicate "pc_or_label_operand"
  (match_code "pc,label_ref")
{
  return (GET_CODE (op) == PC || GET_CODE (op) == LABEL_REF);
})

;; TODO: Add a comment here.

(define_predicate "plus_xor_ior_operator"
  (match_code "plus,xor,ior")
{
  return (GET_CODE (op) == PLUS || GET_CODE (op) == XOR
	  || GET_CODE (op) == IOR);
})

;; Return 1 if OP is a CONST_INT with the value 2, 4, or 8.  These are
;; the valid constant for shadd instructions.

(define_predicate "shadd_operand"
  (match_code "const_int")
{
  return (GET_CODE (op) == CONST_INT && shadd_constant_p (INTVAL (op)));
})

;; TODO: Add a comment here.

(define_predicate "div_operand"
  (match_code "reg,const_int")
{
  return (mode == SImode
	  && ((GET_CODE (op) == REG && REGNO (op) == 25)
	      || (GET_CODE (op) == CONST_INT && INTVAL (op) > 0
		  && INTVAL (op) < 16 && magic_milli[INTVAL (op)])));
})

;; Return nonzero if OP is an integer register, else return zero.

(define_predicate "ireg_operand"
  (match_code "reg")
{
  return (GET_CODE (op) == REG && REGNO (op) > 0 && REGNO (op) < 32);
})

;; Return 1 if this is a comparison operator.  This allows the use of
;; MATCH_OPERATOR to recognize all the branch insns.

(define_predicate "cmpib_comparison_operator"
  (match_code "eq,ne,lt,le,leu,gt,gtu,ge")
{
  return ((mode == VOIDmode || GET_MODE (op) == mode)
          && (GET_CODE (op) == EQ
	      || GET_CODE (op) == NE
	      || GET_CODE (op) == GT
	      || GET_CODE (op) == GTU
	      || GET_CODE (op) == GE
	      || GET_CODE (op) == LT
	      || GET_CODE (op) == LE
	      || GET_CODE (op) == LEU));
})

;; Return 1 if OP is an operator suitable for use in a movb
;; instruction.

(define_predicate "movb_comparison_operator"
  (match_code "eq,ne,lt,ge")
{
  return (GET_CODE (op) == EQ || GET_CODE (op) == NE
	  || GET_CODE (op) == LT || GET_CODE (op) == GE);
})
