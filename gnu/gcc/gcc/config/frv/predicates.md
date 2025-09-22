;; Predicate definitions for Frv.
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

;; Return true if operand is a GPR register.

(define_predicate "integer_register_operand"
  (match_code "reg,subreg")
{
  if (GET_MODE (op) != mode && mode != VOIDmode)
    return FALSE;

  if (GET_CODE (op) == SUBREG)
    {
      if (GET_CODE (SUBREG_REG (op)) != REG)
	return register_operand (op, mode);

      op = SUBREG_REG (op);
    }

  if (GET_CODE (op) != REG)
    return FALSE;

  return GPR_AP_OR_PSEUDO_P (REGNO (op));
})

;; Return 1 is OP is a memory operand, or will be turned into one by
;; reload.

(define_predicate "frv_load_operand"
  (match_code "reg,subreg,mem")
{
  if (GET_MODE (op) != mode && mode != VOIDmode)
    return FALSE;

  if (reload_in_progress)
    {
      rtx tmp = op;
      if (GET_CODE (tmp) == SUBREG)
	tmp = SUBREG_REG (tmp);
      if (GET_CODE (tmp) == REG
	  && REGNO (tmp) >= FIRST_PSEUDO_REGISTER)
	op = reg_equiv_memory_loc[REGNO (tmp)];
    }

  return op && memory_operand (op, mode);
})

;; Return true if operand is a GPR register.  Do not allow SUBREG's
;; here, in order to prevent a combine bug.

(define_predicate "gpr_no_subreg_operand"
  (match_code "reg")
{
  if (GET_MODE (op) != mode && mode != VOIDmode)
    return FALSE;

  if (GET_CODE (op) != REG)
    return FALSE;

  return GPR_OR_PSEUDO_P (REGNO (op));
})

;; Return 1 if operand is a GPR register or a FPR register.

(define_predicate "gpr_or_fpr_operand"
  (match_code "reg,subreg")
{
  int regno;

  if (GET_MODE (op) != mode && mode != VOIDmode)
    return FALSE;

  if (GET_CODE (op) == SUBREG)
    {
      if (GET_CODE (SUBREG_REG (op)) != REG)
	return register_operand (op, mode);

      op = SUBREG_REG (op);
    }

  if (GET_CODE (op) != REG)
    return FALSE;

  regno = REGNO (op);
  if (GPR_P (regno) || FPR_P (regno) || regno >= FIRST_PSEUDO_REGISTER)
    return TRUE;

  return FALSE;
})

;; Return 1 if operand is a GPR register or 12 bit signed immediate.

(define_predicate "gpr_or_int12_operand"
  (match_code "reg,subreg,const_int,const")
{
  if (GET_CODE (op) == CONST_INT)
    return IN_RANGE_P (INTVAL (op), -2048, 2047);

  if (got12_operand (op, mode))
    return true;

  if (GET_MODE (op) != mode && mode != VOIDmode)
    return FALSE;

  if (GET_CODE (op) == SUBREG)
    {
      if (GET_CODE (SUBREG_REG (op)) != REG)
	return register_operand (op, mode);

      op = SUBREG_REG (op);
    }

  if (GET_CODE (op) != REG)
    return FALSE;

  return GPR_OR_PSEUDO_P (REGNO (op));
})

;; Return 1 if operand is a GPR register, or a FPR register, or a 12
;; bit signed immediate.

(define_predicate "gpr_fpr_or_int12_operand"
  (match_code "reg,subreg,const_int")
{
  int regno;

  if (GET_CODE (op) == CONST_INT)
    return IN_RANGE_P (INTVAL (op), -2048, 2047);

  if (GET_MODE (op) != mode && mode != VOIDmode)
    return FALSE;

  if (GET_CODE (op) == SUBREG)
    {
      if (GET_CODE (SUBREG_REG (op)) != REG)
	return register_operand (op, mode);

      op = SUBREG_REG (op);
    }

  if (GET_CODE (op) != REG)
    return FALSE;

  regno = REGNO (op);
  if (GPR_P (regno) || FPR_P (regno) || regno >= FIRST_PSEUDO_REGISTER)
    return TRUE;

  return FALSE;
})

;; Return 1 if operand is a register or 10 bit signed immediate.

(define_predicate "gpr_or_int10_operand"
  (match_code "reg,subreg,const_int")
{
  if (GET_CODE (op) == CONST_INT)
    return IN_RANGE_P (INTVAL (op), -512, 511);

  if (GET_MODE (op) != mode && mode != VOIDmode)
    return FALSE;

  if (GET_CODE (op) == SUBREG)
    {
      if (GET_CODE (SUBREG_REG (op)) != REG)
	return register_operand (op, mode);

      op = SUBREG_REG (op);
    }

  if (GET_CODE (op) != REG)
    return FALSE;

  return GPR_OR_PSEUDO_P (REGNO (op));
})

;; Return 1 if operand is a register or an integer immediate.

(define_predicate "gpr_or_int_operand"
  (match_code "reg,subreg,const_int")
{
  if (GET_CODE (op) == CONST_INT)
    return TRUE;

  if (GET_MODE (op) != mode && mode != VOIDmode)
    return FALSE;

  if (GET_CODE (op) == SUBREG)
    {
      if (GET_CODE (SUBREG_REG (op)) != REG)
	return register_operand (op, mode);

      op = SUBREG_REG (op);
    }

  if (GET_CODE (op) != REG)
    return FALSE;

  return GPR_OR_PSEUDO_P (REGNO (op));
})

;; Return true if operand is something that can be an input for a move
;; operation.

(define_predicate "move_source_operand"
  (match_code "reg,subreg,const_int,mem,const_double,const,symbol_ref,label_ref")
{
  rtx subreg;
  enum rtx_code code;

  switch (GET_CODE (op))
    {
    default:
      break;

    case CONST_INT:
    case CONST_DOUBLE:
      return immediate_operand (op, mode);

    case SUBREG:
      if (GET_MODE (op) != mode && mode != VOIDmode)
        return FALSE;

      subreg = SUBREG_REG (op);
      code = GET_CODE (subreg);
      if (code == MEM)
	return frv_legitimate_address_p (mode, XEXP (subreg, 0),
					 reload_completed, FALSE, FALSE);

      return (code == REG);

    case REG:
      if (GET_MODE (op) != mode && mode != VOIDmode)
        return FALSE;

      return TRUE;

    case MEM:
      return frv_legitimate_memory_operand (op, mode, FALSE);
    }

  return FALSE;
})

;; Return true if operand is something that can be an output for a
;; move operation.

(define_predicate "move_destination_operand"
  (match_code "reg,subreg,mem")
{
  rtx subreg;
  enum rtx_code code;

  switch (GET_CODE (op))
    {
    default:
      break;

    case SUBREG:
      if (GET_MODE (op) != mode && mode != VOIDmode)
        return FALSE;

      subreg = SUBREG_REG (op);
      code = GET_CODE (subreg);
      if (code == MEM)
	return frv_legitimate_address_p (mode, XEXP (subreg, 0),
					 reload_completed, FALSE, FALSE);

      return (code == REG);

    case REG:
      if (GET_MODE (op) != mode && mode != VOIDmode)
        return FALSE;

      return TRUE;

    case MEM:
      return frv_legitimate_memory_operand (op, mode, FALSE);
    }

  return FALSE;
})

;; Return true if we the operand is a valid destination for a movcc_fp
;; instruction.  This means rejecting fcc_operands, since we need
;; scratch registers to write to them.

(define_predicate "movcc_fp_destination_operand"
  (match_code "reg,subreg,mem")
{
  if (fcc_operand (op, mode))
    return FALSE;

  return move_destination_operand (op, mode);
})

;; Return true if operand is something that can be an input for a
;; conditional move operation.

(define_predicate "condexec_source_operand"
  (match_code "reg,subreg,const_int,mem,const_double")
{
  rtx subreg;
  enum rtx_code code;

  switch (GET_CODE (op))
    {
    default:
      break;

    case CONST_INT:
    case CONST_DOUBLE:
      return ZERO_P (op);

    case SUBREG:
      if (GET_MODE (op) != mode && mode != VOIDmode)
        return FALSE;

      subreg = SUBREG_REG (op);
      code = GET_CODE (subreg);
      if (code == MEM)
	return frv_legitimate_address_p (mode, XEXP (subreg, 0),
					 reload_completed, TRUE, FALSE);

      return (code == REG);

    case REG:
      if (GET_MODE (op) != mode && mode != VOIDmode)
        return FALSE;

      return TRUE;

    case MEM:
      return frv_legitimate_memory_operand (op, mode, TRUE);
    }

  return FALSE;
})

;; Return true if operand is something that can be an output for a
;; conditional move operation.

(define_predicate "condexec_dest_operand"
  (match_code "reg,subreg,mem")
{
  rtx subreg;
  enum rtx_code code;

  switch (GET_CODE (op))
    {
    default:
      break;

    case SUBREG:
      if (GET_MODE (op) != mode && mode != VOIDmode)
        return FALSE;

      subreg = SUBREG_REG (op);
      code = GET_CODE (subreg);
      if (code == MEM)
	return frv_legitimate_address_p (mode, XEXP (subreg, 0),
					 reload_completed, TRUE, FALSE);

      return (code == REG);

    case REG:
      if (GET_MODE (op) != mode && mode != VOIDmode)
        return FALSE;

      return TRUE;

    case MEM:
      return frv_legitimate_memory_operand (op, mode, TRUE);
    }

  return FALSE;
})

;; Return true if operand is a register of any flavor or a 0 of the
;; appropriate type.

(define_predicate "reg_or_0_operand"
  (match_code "reg,subreg,const_int,const_double")
{
  switch (GET_CODE (op))
    {
    default:
      break;

    case REG:
    case SUBREG:
      if (GET_MODE (op) != mode && mode != VOIDmode)
	return FALSE;

      return register_operand (op, mode);

    case CONST_INT:
    case CONST_DOUBLE:
      return ZERO_P (op);
    }

  return FALSE;
})

;; Return true if operand is the link register.

(define_predicate "lr_operand"
  (match_code "reg")
{
  if (GET_CODE (op) != REG)
    return FALSE;

  if (GET_MODE (op) != mode && mode != VOIDmode)
    return FALSE;

  if (REGNO (op) != LR_REGNO && REGNO (op) < FIRST_PSEUDO_REGISTER)
    return FALSE;

  return TRUE;
})

;; Return true if operand is a gpr register or a valid memory operand.

(define_predicate "gpr_or_memory_operand"
  (match_code "reg,subreg,mem")
{
  return (integer_register_operand (op, mode)
	  || frv_legitimate_memory_operand (op, mode, FALSE));
})

;; Return true if operand is a gpr register, a valid memory operand,
;; or a memory operand that can be made valid using an additional gpr
;; register.

(define_predicate "gpr_or_memory_operand_with_scratch"
  (match_code "reg,subreg,mem")
{
  rtx addr;

  if (gpr_or_memory_operand (op, mode))
    return TRUE;

  if (GET_CODE (op) != MEM)
    return FALSE;

  if (GET_MODE (op) != mode)
    return FALSE;

  addr = XEXP (op, 0);

  if (GET_CODE (addr) != PLUS)
    return FALSE;
      
  if (!integer_register_operand (XEXP (addr, 0), Pmode))
    return FALSE;

  if (GET_CODE (XEXP (addr, 1)) != CONST_INT)
    return FALSE;

  return TRUE;
})

;; Return true if operand is a fpr register or a valid memory
;; operation.

(define_predicate "fpr_or_memory_operand"
  (match_code "reg,subreg,mem")
{
  return (fpr_operand (op, mode)
	  || frv_legitimate_memory_operand (op, mode, FALSE));
})

;; Return 1 if operand is a 12 bit signed immediate.

(define_predicate "int12_operand"
  (match_code "const_int")
{
  if (GET_CODE (op) != CONST_INT)
    return FALSE;

  return IN_RANGE_P (INTVAL (op), -2048, 2047);
})

;; Return 1 if operand is an integer constant that takes 2
;; instructions to load up and can be split into sethi/setlo
;; instructions..

(define_predicate "int_2word_operand"
  (match_code "const_int,const_double,symbol_ref,label_ref,const")
{
  HOST_WIDE_INT value;
  REAL_VALUE_TYPE rv;
  long l;

  switch (GET_CODE (op))
    {
    default:
      break;

    case LABEL_REF:
      if (TARGET_FDPIC)
	return FALSE;
      
      return (flag_pic == 0);

    case CONST:
      if (flag_pic || TARGET_FDPIC)
	return FALSE;

      op = XEXP (op, 0);
      if (GET_CODE (op) == PLUS && GET_CODE (XEXP (op, 1)) == CONST_INT)
	op = XEXP (op, 0);
      return GET_CODE (op) == SYMBOL_REF || GET_CODE (op) == LABEL_REF;

    case SYMBOL_REF:
      if (TARGET_FDPIC)
	return FALSE;
      
      /* small data references are already 1 word */
      return (flag_pic == 0) && (! SYMBOL_REF_SMALL_P (op));

    case CONST_INT:
      return ! IN_RANGE_P (INTVAL (op), -32768, 32767);

    case CONST_DOUBLE:
      if (GET_MODE (op) == SFmode)
	{
	  REAL_VALUE_FROM_CONST_DOUBLE (rv, op);
	  REAL_VALUE_TO_TARGET_SINGLE (rv, l);
	  value = l;
	  return ! IN_RANGE_P (value, -32768, 32767);
	}
      else if (GET_MODE (op) == VOIDmode)
	{
	  value = CONST_DOUBLE_LOW (op);
	  return ! IN_RANGE_P (value, -32768, 32767);
	}
      break;
    }

  return FALSE;
})

;; Return true if operand is the uClinux PIC register.

(define_predicate "fdpic_operand"
  (match_code "reg")
{
  if (!TARGET_FDPIC)
    return FALSE;

  if (GET_CODE (op) != REG)
    return FALSE;

  if (GET_MODE (op) != mode && mode != VOIDmode)
    return FALSE;

  if (REGNO (op) != FDPIC_REGNO && REGNO (op) < FIRST_PSEUDO_REGISTER)
    return FALSE;

  return TRUE;
})

;; TODO: Add a comment here.

(define_predicate "fdpic_fptr_operand"
  (match_code "reg")
{
  if (GET_MODE (op) != mode && mode != VOIDmode)
    return FALSE;
  if (GET_CODE (op) != REG)
    return FALSE;
  if (REGNO (op) != FDPIC_FPTR_REGNO && REGNO (op) < FIRST_PSEUDO_REGISTER)
    return FALSE;
  return TRUE;
})

;; An address operand that may use a pair of registers, an addressing
;; mode that we reject in general.

(define_predicate "ldd_address_operand"
  (match_code "reg,subreg,plus")
{
  if (GET_MODE (op) != mode && GET_MODE (op) != VOIDmode)
    return FALSE;

  return frv_legitimate_address_p (DImode, op, reload_completed, FALSE, TRUE);
})

;; TODO: Add a comment here.

(define_predicate "got12_operand"
  (match_code "const")
{
  struct frv_unspec unspec;

  if (frv_const_unspec_p (op, &unspec))
    switch (unspec.reloc)
      {
      case R_FRV_GOT12:
      case R_FRV_GOTOFF12:
      case R_FRV_FUNCDESC_GOT12:
      case R_FRV_FUNCDESC_GOTOFF12:
      case R_FRV_GPREL12:
      case R_FRV_TLSMOFF12:
	return true;
      }
  return false;
})

;; Return true if OP is a valid const-unspec expression.

(define_predicate "const_unspec_operand"
  (match_code "const")
{
  struct frv_unspec unspec;

  return frv_const_unspec_p (op, &unspec);
})

;; Return true if operand is an icc register.

(define_predicate "icc_operand"
  (match_code "reg")
{
  int regno;

  if (GET_MODE (op) != mode && mode != VOIDmode)
    return FALSE;

  if (GET_CODE (op) != REG)
    return FALSE;

  regno = REGNO (op);
  return ICC_OR_PSEUDO_P (regno);
})

;; Return true if operand is an fcc register.

(define_predicate "fcc_operand"
  (match_code "reg")
{
  int regno;

  if (GET_MODE (op) != mode && mode != VOIDmode)
    return FALSE;

  if (GET_CODE (op) != REG)
    return FALSE;

  regno = REGNO (op);
  return FCC_OR_PSEUDO_P (regno);
})

;; Return true if operand is either an fcc or icc register.

(define_predicate "cc_operand"
  (match_code "reg")
{
  int regno;

  if (GET_MODE (op) != mode && mode != VOIDmode)
    return FALSE;

  if (GET_CODE (op) != REG)
    return FALSE;

  regno = REGNO (op);
  if (CC_OR_PSEUDO_P (regno))
    return TRUE;

  return FALSE;
})

;; Return true if operand is an integer CCR register.

(define_predicate "icr_operand"
  (match_code "reg")
{
  int regno;

  if (GET_MODE (op) != mode && mode != VOIDmode)
    return FALSE;

  if (GET_CODE (op) != REG)
    return FALSE;

  regno = REGNO (op);
  return ICR_OR_PSEUDO_P (regno);
})

;; Return true if operand is an fcc register.

(define_predicate "fcr_operand"
  (match_code "reg")
{
  int regno;

  if (GET_MODE (op) != mode && mode != VOIDmode)
    return FALSE;

  if (GET_CODE (op) != REG)
    return FALSE;

  regno = REGNO (op);
  return FCR_OR_PSEUDO_P (regno);
})

;; Return true if operand is either an fcc or icc register.

(define_predicate "cr_operand"
  (match_code "reg")
{
  int regno;

  if (GET_MODE (op) != mode && mode != VOIDmode)
    return FALSE;

  if (GET_CODE (op) != REG)
    return FALSE;

  regno = REGNO (op);
  if (CR_OR_PSEUDO_P (regno))
    return TRUE;

  return FALSE;
})

;; Return true if operand is a FPR register.

(define_predicate "fpr_operand"
  (match_code "reg,subreg")
{
  if (GET_MODE (op) != mode && mode != VOIDmode)
    return FALSE;

  if (GET_CODE (op) == SUBREG)
    {
      if (GET_CODE (SUBREG_REG (op)) != REG)
        return register_operand (op, mode);

      op = SUBREG_REG (op);
    }

  if (GET_CODE (op) != REG)
    return FALSE;

  return FPR_OR_PSEUDO_P (REGNO (op));
})

;; Return true if operand is an even GPR or FPR register.

(define_predicate "even_reg_operand"
  (match_code "reg,subreg")
{
  int regno;

  if (GET_MODE (op) != mode && mode != VOIDmode)
    return FALSE;

  if (GET_CODE (op) == SUBREG)
    {
      if (GET_CODE (SUBREG_REG (op)) != REG)
        return register_operand (op, mode);

      op = SUBREG_REG (op);
    }

  if (GET_CODE (op) != REG)
    return FALSE;

  regno = REGNO (op);
  if (regno >= FIRST_PSEUDO_REGISTER)
    return TRUE;

  if (GPR_P (regno))
    return (((regno - GPR_FIRST) & 1) == 0);

  if (FPR_P (regno))
    return (((regno - FPR_FIRST) & 1) == 0);

  return FALSE;
})

;; Return true if operand is an odd GPR register.

(define_predicate "odd_reg_operand"
  (match_code "reg,subreg")
{
  int regno;

  if (GET_MODE (op) != mode && mode != VOIDmode)
    return FALSE;

  if (GET_CODE (op) == SUBREG)
    {
      if (GET_CODE (SUBREG_REG (op)) != REG)
        return register_operand (op, mode);

      op = SUBREG_REG (op);
    }

  if (GET_CODE (op) != REG)
    return FALSE;

  regno = REGNO (op);
  /* Assume that reload will give us an even register.  */
  if (regno >= FIRST_PSEUDO_REGISTER)
    return FALSE;

  if (GPR_P (regno))
    return (((regno - GPR_FIRST) & 1) != 0);

  if (FPR_P (regno))
    return (((regno - FPR_FIRST) & 1) != 0);

  return FALSE;
})

;; Return true if operand is an even GPR register.

(define_predicate "even_gpr_operand"
  (match_code "reg,subreg")
{
  int regno;

  if (GET_MODE (op) != mode && mode != VOIDmode)
    return FALSE;

  if (GET_CODE (op) == SUBREG)
    {
      if (GET_CODE (SUBREG_REG (op)) != REG)
        return register_operand (op, mode);

      op = SUBREG_REG (op);
    }

  if (GET_CODE (op) != REG)
    return FALSE;

  regno = REGNO (op);
  if (regno >= FIRST_PSEUDO_REGISTER)
    return TRUE;

  if (! GPR_P (regno))
    return FALSE;

  return (((regno - GPR_FIRST) & 1) == 0);
})

;; Return true if operand is an odd GPR register.

(define_predicate "odd_gpr_operand"
  (match_code "reg,subreg")
{
  int regno;

  if (GET_MODE (op) != mode && mode != VOIDmode)
    return FALSE;

  if (GET_CODE (op) == SUBREG)
    {
      if (GET_CODE (SUBREG_REG (op)) != REG)
        return register_operand (op, mode);

      op = SUBREG_REG (op);
    }

  if (GET_CODE (op) != REG)
    return FALSE;

  regno = REGNO (op);
  /* Assume that reload will give us an even register.  */
  if (regno >= FIRST_PSEUDO_REGISTER)
    return FALSE;

  if (! GPR_P (regno))
    return FALSE;

  return (((regno - GPR_FIRST) & 1) != 0);
})

;; Return true if operand is a quad aligned FPR register.

(define_predicate "quad_fpr_operand"
  (match_code "reg,subreg")
{
  int regno;

  if (GET_MODE (op) != mode && mode != VOIDmode)
    return FALSE;

  if (GET_CODE (op) == SUBREG)
    {
      if (GET_CODE (SUBREG_REG (op)) != REG)
        return register_operand (op, mode);

      op = SUBREG_REG (op);
    }

  if (GET_CODE (op) != REG)
    return FALSE;

  regno = REGNO (op);
  if (regno >= FIRST_PSEUDO_REGISTER)
    return TRUE;

  if (! FPR_P (regno))
    return FALSE;

  return (((regno - FPR_FIRST) & 3) == 0);
})

;; Return true if operand is an even FPR register.

(define_predicate "even_fpr_operand"
  (match_code "reg,subreg")
{
  int regno;

  if (GET_MODE (op) != mode && mode != VOIDmode)
    return FALSE;

  if (GET_CODE (op) == SUBREG)
    {
      if (GET_CODE (SUBREG_REG (op)) != REG)
        return register_operand (op, mode);

      op = SUBREG_REG (op);
    }

  if (GET_CODE (op) != REG)
    return FALSE;

  regno = REGNO (op);
  if (regno >= FIRST_PSEUDO_REGISTER)
    return TRUE;

  if (! FPR_P (regno))
    return FALSE;

  return (((regno - FPR_FIRST) & 1) == 0);
})

;; Return true if operand is an odd FPR register.

(define_predicate "odd_fpr_operand"
  (match_code "reg,subreg")
{
  int regno;

  if (GET_MODE (op) != mode && mode != VOIDmode)
    return FALSE;

  if (GET_CODE (op) == SUBREG)
    {
      if (GET_CODE (SUBREG_REG (op)) != REG)
        return register_operand (op, mode);

      op = SUBREG_REG (op);
    }

  if (GET_CODE (op) != REG)
    return FALSE;

  regno = REGNO (op);
  /* Assume that reload will give us an even register.  */
  if (regno >= FIRST_PSEUDO_REGISTER)
    return FALSE;

  if (! FPR_P (regno))
    return FALSE;

  return (((regno - FPR_FIRST) & 1) != 0);
})

;; Return true if operand is a 2 word memory address that can be
;; loaded in one instruction to load or store.  We assume the stack
;; and frame pointers are suitably aligned, and variables in the small
;; data area.  FIXME -- at some we should recognize other globals and
;; statics. We can't assume that any old pointer is aligned, given
;; that arguments could be passed on an odd word on the stack and the
;; address taken and passed through to another function.

(define_predicate "dbl_memory_one_insn_operand"
  (match_code "mem")
{
  rtx addr;
  rtx addr_reg;

  if (! TARGET_DWORD)
    return FALSE;

  if (GET_CODE (op) != MEM)
    return FALSE;

  if (mode != VOIDmode && GET_MODE_SIZE (mode) != 2*UNITS_PER_WORD)
    return FALSE;

  addr = XEXP (op, 0);
  if (GET_CODE (addr) == REG)
    addr_reg = addr;

  else if (GET_CODE (addr) == PLUS)
    {
      rtx addr0 = XEXP (addr, 0);
      rtx addr1 = XEXP (addr, 1);

      if (GET_CODE (addr0) != REG)
	return FALSE;

      if (got12_operand (addr1, VOIDmode))
	return TRUE;

      if (GET_CODE (addr1) != CONST_INT)
	return FALSE;

      if ((INTVAL (addr1) & 7) != 0)
	return FALSE;

      addr_reg = addr0;
    }

  else
    return FALSE;

  if (addr_reg == frame_pointer_rtx || addr_reg == stack_pointer_rtx)
    return TRUE;

  return FALSE;
})

;; Return true if operand is a 2 word memory address that needs to use
;; two instructions to load or store.

(define_predicate "dbl_memory_two_insn_operand"
  (match_code "mem")
{
  if (GET_CODE (op) != MEM)
    return FALSE;

  if (mode != VOIDmode && GET_MODE_SIZE (mode) != 2*UNITS_PER_WORD)
    return FALSE;

  if (! TARGET_DWORD)
    return TRUE;

  return ! dbl_memory_one_insn_operand (op, mode);
})

;; Return true if operand is a memory reference suitable for a call.

(define_predicate "call_operand"
  (match_code "reg,subreg,const_int,const,symbol_ref")
{
  if (GET_MODE (op) != mode && mode != VOIDmode && GET_CODE (op) != CONST_INT)
    return FALSE;

  if (GET_CODE (op) == SYMBOL_REF)
    return !TARGET_LONG_CALLS || SYMBOL_REF_LOCAL_P (op);

  /* Note this doesn't allow reg+reg or reg+imm12 addressing (which should
     never occur anyway), but prevents reload from not handling the case
     properly of a call through a pointer on a function that calls
     vfork/setjmp, etc. due to the need to flush all of the registers to stack.  */
  return gpr_or_int12_operand (op, mode);
})

;; Return true if operand is a memory reference suitable for a
;; sibcall.

(define_predicate "sibcall_operand"
  (match_code "reg,subreg,const_int,const")
{
  if (GET_MODE (op) != mode && mode != VOIDmode && GET_CODE (op) != CONST_INT)
    return FALSE;

  /* Note this doesn't allow reg+reg or reg+imm12 addressing (which should
     never occur anyway), but prevents reload from not handling the case
     properly of a call through a pointer on a function that calls
     vfork/setjmp, etc. due to the need to flush all of the registers to stack.  */
  return gpr_or_int12_operand (op, mode);
})

;; Return 1 if operand is an integer constant with the bottom 16 bits
;; clear.

(define_predicate "upper_int16_operand"
  (match_code "const_int")
{
  if (GET_CODE (op) != CONST_INT)
    return FALSE;

  return ((INTVAL (op) & 0xffff) == 0);
})

;; Return 1 if operand is a 16 bit unsigned immediate.

(define_predicate "uint16_operand"
  (match_code "const_int")
{
  if (GET_CODE (op) != CONST_INT)
    return FALSE;

  return IN_RANGE_P (INTVAL (op), 0, 0xffff);
})

;; Returns 1 if OP is either a SYMBOL_REF or a constant.

(define_predicate "symbolic_operand"
  (match_code "symbol_ref,const_int")
{
  enum rtx_code c = GET_CODE (op);

  if (c == CONST)
    {
      /* Allow (const:SI (plus:SI (symbol_ref) (const_int))).  */
      return GET_MODE (op) == SImode
	&& GET_CODE (XEXP (op, 0)) == PLUS
	&& GET_CODE (XEXP (XEXP (op, 0), 0)) == SYMBOL_REF
	&& GET_CODE (XEXP (XEXP (op, 0), 1)) == CONST_INT;
    }

  return c == SYMBOL_REF || c == CONST_INT;
})

;; Return true if operator is a kind of relational operator.

(define_predicate "relational_operator"
  (match_code "eq,ne,le,lt,ge,gt,leu,ltu,geu,gtu")
{
  return (integer_relational_operator (op, mode)
	  || float_relational_operator (op, mode));
})

;; Return true if OP is a relational operator suitable for CCmode,
;; CC_UNSmode or CC_NZmode.

(define_predicate "integer_relational_operator"
  (match_code "eq,ne,le,lt,ge,gt,leu,ltu,geu,gtu")
{
  if (mode != VOIDmode && mode != GET_MODE (op))
    return FALSE;

  /* The allowable relations depend on the mode of the ICC register.  */
  switch (GET_CODE (op))
    {
    default:
      return FALSE;

    case EQ:
    case NE:
    case LT:
    case GE:
      return (GET_MODE (XEXP (op, 0)) == CC_NZmode
	      || GET_MODE (XEXP (op, 0)) == CCmode);

    case LE:
    case GT:
      return GET_MODE (XEXP (op, 0)) == CCmode;

    case GTU:
    case GEU:
    case LTU:
    case LEU:
      return (GET_MODE (XEXP (op, 0)) == CC_NZmode
	      || GET_MODE (XEXP (op, 0)) == CC_UNSmode);
    }
})

;; Return true if operator is a floating point relational operator.

(define_predicate "float_relational_operator"
  (match_code "eq,ne,le,lt,ge,gt")
{
  if (mode != VOIDmode && mode != GET_MODE (op))
    return FALSE;

  switch (GET_CODE (op))
    {
    default:
      return FALSE;

    case EQ: case NE:
    case LE: case LT:
    case GE: case GT:
#if 0
    case UEQ: case UNE:
    case ULE: case ULT:
    case UGE: case UGT:
    case ORDERED:
    case UNORDERED:
#endif
      return GET_MODE (XEXP (op, 0)) == CC_FPmode;
    }
})

;; Return true if operator is EQ/NE of a conditional execution
;; register.

(define_predicate "ccr_eqne_operator"
  (match_code "eq,ne")
{
  enum machine_mode op_mode = GET_MODE (op);
  rtx op0;
  rtx op1;
  int regno;

  if (mode != VOIDmode && op_mode != mode)
    return FALSE;

  switch (GET_CODE (op))
    {
    default:
      return FALSE;

    case EQ:
    case NE:
      break;
    }

  op1 = XEXP (op, 1);
  if (op1 != const0_rtx)
    return FALSE;

  op0 = XEXP (op, 0);
  if (GET_CODE (op0) != REG)
    return FALSE;

  regno = REGNO (op0);
  if (op_mode == CC_CCRmode && CR_OR_PSEUDO_P (regno))
    return TRUE;

  return FALSE;
})

;; Return true if operator is a minimum or maximum operator (both
;; signed and unsigned).

(define_predicate "minmax_operator"
  (match_code "smin,smax,umin,umax")
{
  if (mode != VOIDmode && mode != GET_MODE (op))
    return FALSE;

  switch (GET_CODE (op))
    {
    default:
      return FALSE;

    case SMIN:
    case SMAX:
    case UMIN:
    case UMAX:
      break;
    }

  if (! integer_register_operand (XEXP (op, 0), mode))
    return FALSE;

  if (! gpr_or_int10_operand (XEXP (op, 1), mode))
    return FALSE;

  return TRUE;
})

;; Return true if operator is an integer binary operator that can
;; executed conditionally and takes 1 cycle.

(define_predicate "condexec_si_binary_operator"
  (match_code "plus,minus,and,ior,xor,ashift,ashiftrt,lshiftrt")
{
  enum machine_mode op_mode = GET_MODE (op);

  if (mode != VOIDmode && op_mode != mode)
    return FALSE;

  switch (GET_CODE (op))
    {
    default:
      return FALSE;

    case PLUS:
    case MINUS:
    case AND:
    case IOR:
    case XOR:
    case ASHIFT:
    case ASHIFTRT:
    case LSHIFTRT:
      return TRUE;
    }
})

;; Return true if operator is an integer binary operator that can be
;; executed conditionally by a media instruction.

(define_predicate "condexec_si_media_operator"
  (match_code "and,ior,xor")
{
  enum machine_mode op_mode = GET_MODE (op);

  if (mode != VOIDmode && op_mode != mode)
    return FALSE;

  switch (GET_CODE (op))
    {
    default:
      return FALSE;

    case AND:
    case IOR:
    case XOR:
      return TRUE;
    }
})

;; Return true if operator is an integer division operator that can
;; executed conditionally.

(define_predicate "condexec_si_divide_operator"
  (match_code "div,udiv")
{
  enum machine_mode op_mode = GET_MODE (op);

  if (mode != VOIDmode && op_mode != mode)
    return FALSE;

  switch (GET_CODE (op))
    {
    default:
      return FALSE;

    case DIV:
    case UDIV:
      return TRUE;
    }
})

;; Return true if operator is an integer unary operator that can
;; executed conditionally.

(define_predicate "condexec_si_unary_operator"
  (match_code "not,neg")
{
  enum machine_mode op_mode = GET_MODE (op);

  if (mode != VOIDmode && op_mode != mode)
    return FALSE;

  switch (GET_CODE (op))
    {
    default:
      return FALSE;

    case NEG:
    case NOT:
      return TRUE;
    }
})

;; Return true if operator is an addition or subtraction
;; expression. Such expressions can be evaluated conditionally by
;; floating-point instructions.

(define_predicate "condexec_sf_add_operator"
  (match_code "plus,minus")
{
  enum machine_mode op_mode = GET_MODE (op);

  if (mode != VOIDmode && op_mode != mode)
    return FALSE;

  switch (GET_CODE (op))
    {
    default:
      return FALSE;

    case PLUS:
    case MINUS:
      return TRUE;
    }
})

;; Return true if operator is a conversion-type expression that can be
;; evaluated conditionally by floating-point instructions.

(define_predicate "condexec_sf_conv_operator"
  (match_code "abs,neg")
{
  enum machine_mode op_mode = GET_MODE (op);

  if (mode != VOIDmode && op_mode != mode)
    return FALSE;

  switch (GET_CODE (op))
    {
    default:
      return FALSE;

    case NEG:
    case ABS:
      return TRUE;
    }
})

;; Return true if OP is an integer binary operator that can be
;; combined with a (set ... (compare:CC_NZ ...)) pattern.

(define_predicate "intop_compare_operator"
  (match_code "plus,minus,and,ior,xor,ashift,ashiftrt,lshiftrt")
{
  if (mode != VOIDmode && GET_MODE (op) != mode)
    return FALSE;

  switch (GET_CODE (op))
    {
    default:
      return FALSE;

    case PLUS:
    case MINUS:
    case AND:
    case IOR:
    case XOR:
    case ASHIFTRT:
    case LSHIFTRT:
      return GET_MODE (op) == SImode;
    }
})

;; Return 1 if operand is a register or 6 bit signed immediate.

(define_predicate "fpr_or_int6_operand"
  (match_code "reg,subreg,const_int")
{
  if (GET_CODE (op) == CONST_INT)
    return IN_RANGE_P (INTVAL (op), -32, 31);

  if (GET_MODE (op) != mode && mode != VOIDmode)
    return FALSE;

  if (GET_CODE (op) == SUBREG)
    {
      if (GET_CODE (SUBREG_REG (op)) != REG)
	return register_operand (op, mode);

      op = SUBREG_REG (op);
    }

  if (GET_CODE (op) != REG)
    return FALSE;

  return FPR_OR_PSEUDO_P (REGNO (op));
})

;; Return 1 if operand is a 6 bit signed immediate.

(define_predicate "int6_operand"
  (match_code "const_int")
{
  if (GET_CODE (op) != CONST_INT)
    return FALSE;

  return IN_RANGE_P (INTVAL (op), -32, 31);
})

;; Return 1 if operand is a 5 bit signed immediate.

(define_predicate "int5_operand"
  (match_code "const_int")
{
  return GET_CODE (op) == CONST_INT && IN_RANGE_P (INTVAL (op), -16, 15);
})

;; Return 1 if operand is a 5 bit unsigned immediate.

(define_predicate "uint5_operand"
  (match_code "const_int")
{
  return GET_CODE (op) == CONST_INT && IN_RANGE_P (INTVAL (op), 0, 31);
})

;; Return 1 if operand is a 4 bit unsigned immediate.

(define_predicate "uint4_operand"
  (match_code "const_int")
{
  return GET_CODE (op) == CONST_INT && IN_RANGE_P (INTVAL (op), 0, 15);
})

;; Return 1 if operand is a 1 bit unsigned immediate (0 or 1).

(define_predicate "uint1_operand"
  (match_code "const_int")
{
  return GET_CODE (op) == CONST_INT && IN_RANGE_P (INTVAL (op), 0, 1);
})

;; Return 1 if operand is a valid ACC register number.

(define_predicate "acc_operand"
  (match_code "reg,subreg")
{
  return ((mode == VOIDmode || mode == GET_MODE (op))
	  && REG_P (op) && ACC_P (REGNO (op))
	  && ((REGNO (op) - ACC_FIRST) & ~ACC_MASK) == 0);
})

;; Return 1 if operand is a valid even ACC register number.

(define_predicate "even_acc_operand"
  (match_code "reg,subreg")
{
  return acc_operand (op, mode) && ((REGNO (op) - ACC_FIRST) & 1) == 0;
})

;; Return 1 if operand is zero or four.

(define_predicate "quad_acc_operand"
  (match_code "reg,subreg")
{
  return acc_operand (op, mode) && ((REGNO (op) - ACC_FIRST) & 3) == 0;
})

;; Return 1 if operand is a valid ACCG register number.

(define_predicate "accg_operand"
  (match_code "reg,subreg")
{
  return ((mode == VOIDmode || mode == GET_MODE (op))
	  && REG_P (op) && ACCG_P (REGNO (op))
	  && ((REGNO (op) - ACCG_FIRST) & ~ACC_MASK) == 0);
})
