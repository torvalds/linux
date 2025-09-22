;; Predicate definitions for TMS320C[34]x.
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

;; Nonzero if OP is a floating point value with value 0.0.

(define_predicate "fp_zero_operand"
  (match_code "const_double")
{
  REAL_VALUE_TYPE r;

  if (GET_CODE (op) != CONST_DOUBLE)
    return 0;
  REAL_VALUE_FROM_CONST_DOUBLE (r, op);
  return REAL_VALUES_EQUAL (r, dconst0);
})

;; TODO: Add a comment here.

(define_predicate "const_operand"
  (match_code "const_int,const_double")
{
  switch (mode)
    {
    case QFmode:
    case HFmode:
      if (GET_CODE (op) != CONST_DOUBLE
	  || GET_MODE (op) != mode
	  || GET_MODE_CLASS (mode) != MODE_FLOAT)
	return 0;

      return c4x_immed_float_p (op);

#if Pmode != QImode
    case Pmode:
#endif
    case QImode:
      if (GET_CODE (op) != CONST_INT
	  || (GET_MODE (op) != VOIDmode && GET_MODE (op) != mode)
	  || GET_MODE_CLASS (mode) != MODE_INT)
	return 0;

      return IS_HIGH_CONST (INTVAL (op)) || IS_INT16_CONST (INTVAL (op));

    case HImode:
      return 0;

    default:
      return 0;
    }
})

;; TODO: Add a comment here.

(define_predicate "stik_const_operand"
  (match_code "const_int")
{
  return c4x_K_constant (op);
})

;; TODO: Add a comment here.

(define_predicate "not_const_operand"
  (match_code "const_int")
{
  return c4x_N_constant (op);
})

;; TODO: Add a comment here.

(define_predicate "reg_operand"
  (match_code "reg,subreg")
{
  if (GET_CODE (op) == SUBREG
      && GET_MODE (op) == QFmode)
    return 0;
  return register_operand (op, mode);
})

;; TODO: Add a comment here.

(define_predicate "reg_or_const_operand"
  (match_code "reg,subreg,const_int,const_double")
{
  return reg_operand (op, mode) || const_operand (op, mode);
})

;; Extended precision register R0-R1.

(define_predicate "r0r1_reg_operand"
  (match_code "reg,subreg")
{
  if (! reg_operand (op, mode))
    return 0;
  if (GET_CODE (op) == SUBREG)
    op = SUBREG_REG (op);
  return REG_P (op) && IS_R0R1_OR_PSEUDO_REG (op);
})

;; Extended precision register R2-R3.

(define_predicate "r2r3_reg_operand"
  (match_code "reg,subreg")
{
  if (! reg_operand (op, mode))
    return 0;
  if (GET_CODE (op) == SUBREG)
    op = SUBREG_REG (op);
  return REG_P (op) && IS_R2R3_OR_PSEUDO_REG (op);
})

;; Low extended precision register R0-R7.

(define_predicate "ext_low_reg_operand"
  (match_code "reg,subreg")
{
  if (! reg_operand (op, mode))
    return 0;
  if (GET_CODE (op) == SUBREG)
    op = SUBREG_REG (op);
  return REG_P (op) && IS_EXT_LOW_OR_PSEUDO_REG (op);
})

;; Extended precision register.

(define_predicate "ext_reg_operand"
  (match_code "reg,subreg")
{
  if (! reg_operand (op, mode))
    return 0;
  if (GET_CODE (op) == SUBREG)
    op = SUBREG_REG (op);
  if (! REG_P (op))
    return 0;
  return IS_EXT_OR_PSEUDO_REG (op);
})

;; Standard precision register.

(define_predicate "std_reg_operand"
  (match_code "reg,subreg")
{
  if (! reg_operand (op, mode))
    return 0;
  if (GET_CODE (op) == SUBREG)
    op = SUBREG_REG (op);
  return REG_P (op) && IS_STD_OR_PSEUDO_REG (op);
})

;; Standard precision or normal register.

(define_predicate "std_or_reg_operand"
  (match_code "reg,subreg")
{
  if (reload_in_progress)
    return std_reg_operand (op, mode);
  return reg_operand (op, mode);
})

;; Address register.

(define_predicate "addr_reg_operand"
  (match_code "reg,subreg")
{
  if (! reg_operand (op, mode))
    return 0;
  return c4x_a_register (op);
})

;; Index register.

(define_predicate "index_reg_operand"
  (match_code "reg,subreg")
{
  if (! reg_operand (op, mode))
    return 0;
  if (GET_CODE (op) == SUBREG)
    op = SUBREG_REG (op);
  return c4x_x_register (op);
})

;; DP register.

(define_predicate "dp_reg_operand"
  (match_code "reg")
{
  return REG_P (op) && IS_DP_OR_PSEUDO_REG (op);
})

;; SP register.

(define_predicate "sp_reg_operand"
  (match_code "reg")
{
  return REG_P (op) && IS_SP_OR_PSEUDO_REG (op);
})

;; ST register.

(define_predicate "st_reg_operand"
  (match_code "reg")
{
  return REG_P (op) && IS_ST_OR_PSEUDO_REG (op);
})

;; RC register.

(define_predicate "rc_reg_operand"
  (match_code "reg")
{
  return REG_P (op) && IS_RC_OR_PSEUDO_REG (op);
})

;; TODO: Add a comment here.

(define_predicate "call_address_operand"
  (match_code "reg,symbol_ref,label_ref,const")
{
  return (REG_P (op) || symbolic_address_operand (op, mode));
})

;; Check dst operand of a move instruction.

(define_predicate "dst_operand"
  (match_code "subreg,reg,mem")
{
  if (GET_CODE (op) == SUBREG
      && mixed_subreg_operand (op, mode))
    return 0;

  if (REG_P (op))
    return reg_operand (op, mode);

  return nonimmediate_operand (op, mode);
})

;; Check src operand of two operand arithmetic instructions.

(define_predicate "src_operand"
  (match_code "subreg,reg,mem,const_int,const_double")
{
  if (GET_CODE (op) == SUBREG
      && mixed_subreg_operand (op, mode))
    return 0;

  if (REG_P (op))
    return reg_operand (op, mode);

  if (mode == VOIDmode)
    mode = GET_MODE (op);

  if (GET_CODE (op) == CONST_INT)
    return (mode == QImode || mode == Pmode || mode == HImode)
      && c4x_I_constant (op);

  /* We don't like CONST_DOUBLE integers.  */
  if (GET_CODE (op) == CONST_DOUBLE)
    return c4x_H_constant (op);

  /* Disallow symbolic addresses.  Only the predicate
     symbolic_address_operand will match these.  */
  if (GET_CODE (op) == SYMBOL_REF
      || GET_CODE (op) == LABEL_REF
      || GET_CODE (op) == CONST)
    return 0;

  /* If TARGET_LOAD_DIRECT_MEMS is nonzero, disallow direct memory
     access to symbolic addresses.  These operands will get forced
     into a register and the movqi expander will generate a
     HIGH/LO_SUM pair if TARGET_EXPOSE_LDP is nonzero.  */
  if (GET_CODE (op) == MEM
      && ((GET_CODE (XEXP (op, 0)) == SYMBOL_REF
	   || GET_CODE (XEXP (op, 0)) == LABEL_REF
	   || GET_CODE (XEXP (op, 0)) == CONST)))
    return !TARGET_EXPOSE_LDP &&
      ! TARGET_LOAD_DIRECT_MEMS && GET_MODE (op) == mode;

  return general_operand (op, mode);
})

;; TODO: Add a comment here.

(define_predicate "src_hi_operand"
  (match_code "subreg,reg,mem,const_double")
{
  if (c4x_O_constant (op))
    return 1;
  return src_operand (op, mode);
})

;; Check src operand of two operand logical instructions.

(define_predicate "lsrc_operand"
  (match_code "subreg,reg,mem,const_int,const_double")
{
  if (mode == VOIDmode)
    mode = GET_MODE (op);

  if (mode != QImode && mode != Pmode)
    fatal_insn ("mode not QImode", op);

  if (GET_CODE (op) == CONST_INT)
    return c4x_L_constant (op) || c4x_J_constant (op);

  return src_operand (op, mode);
})

;; Check src operand of two operand tricky instructions.

(define_predicate "tsrc_operand"
  (match_code "subreg,reg,mem,const_int,const_double")
{
  if (mode == VOIDmode)
    mode = GET_MODE (op);

  if (mode != QImode && mode != Pmode)
    fatal_insn ("mode not QImode", op);

  if (GET_CODE (op) == CONST_INT)
    return c4x_L_constant (op) || c4x_N_constant (op) || c4x_J_constant (op);

  return src_operand (op, mode);
})

;; Check src operand of two operand non immediate instructions.

(define_predicate "nonimmediate_src_operand"
  (match_code "subreg,reg,mem")
{
  if (GET_CODE (op) == CONST_INT || GET_CODE (op) == CONST_DOUBLE)
    return 0;

  return src_operand (op, mode);
})

;; Check logical src operand of two operand non immediate instructions.

(define_predicate "nonimmediate_lsrc_operand"
  (match_code "subreg,reg,mem")
{
  if (GET_CODE (op) == CONST_INT || GET_CODE (op) == CONST_DOUBLE)
    return 0;

  return lsrc_operand (op, mode);
})

;; Match any operand.

(define_predicate "any_operand"
  (match_code "subreg,reg,mem,const_int,const_double")
{
  return 1;
})

;; Check for indirect operands allowable in parallel instruction.

(define_predicate "par_ind_operand"
  (match_code "mem")
{
  if (mode != VOIDmode && mode != GET_MODE (op))
    return 0;

  return c4x_S_indirect (op);
})

;; Check for operands allowable in parallel instruction.

(define_predicate "parallel_operand"
  (match_code "subreg,reg,mem")
{
  return ext_low_reg_operand (op, mode) || par_ind_operand (op, mode);
})

;; Symbolic address operand.

(define_predicate "symbolic_address_operand"
  (match_code "symbol_ref,label_ref,const")
{
  switch (GET_CODE (op))
    {
    case CONST:
    case SYMBOL_REF:
    case LABEL_REF:
      return 1;
    default:
      return 0;
    }
})
