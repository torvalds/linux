;; Predicate definitions for Motorola MCore.
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

;; Nonzero if OP is a normal arithmetic register.

(define_predicate "mcore_arith_reg_operand"
  (match_code "reg,subreg")
{
  if (! register_operand (op, mode))
    return 0;

  if (GET_CODE (op) == SUBREG)
    op = SUBREG_REG (op);

  if (GET_CODE (op) == REG)
    return REGNO (op) != CC_REG;

  return 1;
})

;; Nonzero if OP can be source of a simple move operation.

(define_predicate "mcore_general_movsrc_operand"
  (match_code "mem,const_int,reg,subreg,symbol_ref,label_ref")
{
  /* Any (MEM LABEL_REF) is OK.  That is a pc-relative load.  */
  if (GET_CODE (op) == MEM && GET_CODE (XEXP (op, 0)) == LABEL_REF)
    return 1;

  return general_operand (op, mode);
})

;; Nonzero if OP can be destination of a simple move operation.

(define_predicate "mcore_general_movdst_operand"
  (match_code "mem,const_int,reg,subreg")
{
  if (GET_CODE (op) == REG && REGNO (op) == CC_REG)
    return 0;

  return general_operand (op, mode);
})

;; Nonzero if OP should be recognized during reload for an ixh/ixw
;; operand.  See the ixh/ixw patterns.

(define_predicate "mcore_reload_operand"
  (match_code "mem,reg,subreg")
{
  if (mcore_arith_reg_operand (op, mode))
    return 1;

  if (! reload_in_progress)
    return 0;

  return GET_CODE (op) == MEM;
})

;; Nonzero if OP is a valid source operand for an arithmetic insn.

(define_predicate "mcore_arith_J_operand"
  (match_code "const_int,reg,subreg")
{
  if (register_operand (op, mode))
    return 1;

  if (GET_CODE (op) == CONST_INT && CONST_OK_FOR_J (INTVAL (op)))
    return 1;

  return 0;
})

;; Nonzero if OP is a valid source operand for an arithmetic insn.

(define_predicate "mcore_arith_K_operand"
  (match_code "const_int,reg,subreg")
{
  if (register_operand (op, mode))
    return 1;

  if (GET_CODE (op) == CONST_INT && CONST_OK_FOR_K (INTVAL (op)))
    return 1;

  return 0;
})

;; Nonzero if OP is a valid source operand for a shift or rotate insn.

(define_predicate "mcore_arith_K_operand_not_0"
  (match_code "const_int,reg,subreg")
{
  if (register_operand (op, mode))
    return 1;

  if (   GET_CODE (op) == CONST_INT
      && CONST_OK_FOR_K (INTVAL (op))
      && INTVAL (op) != 0)
    return 1;

  return 0;
})

;; TODO: Add a comment here.

(define_predicate "mcore_arith_M_operand"
  (match_code "const_int,reg,subreg")
{
  if (register_operand (op, mode))
    return 1;

  if (GET_CODE (op) == CONST_INT && CONST_OK_FOR_M (INTVAL (op)))
    return 1;

  return 0;
})

;; TODO: Add a comment here.

(define_predicate "mcore_arith_K_S_operand"
  (match_code "const_int,reg,subreg")
{
  if (register_operand (op, mode))
    return 1;

  if (GET_CODE (op) == CONST_INT)
    {
      if (CONST_OK_FOR_K (INTVAL (op)) || CONST_OK_FOR_M (~INTVAL (op)))
	return 1;
    }

  return 0;
})

;; Nonzero if OP is a valid source operand for a cmov with two consts
;; +/- 1.

(define_predicate "mcore_arith_O_operand"
  (match_code "const_int,reg,subreg")
{
  if (register_operand (op, mode))
    return 1;

  if (GET_CODE (op) == CONST_INT && CONST_OK_FOR_O (INTVAL (op)))
    return 1;

  return 0;
})

;; Nonzero if OP is a valid source operand for loading.

(define_predicate "mcore_arith_imm_operand"
  (match_code "const_int,reg,subreg")
{
  if (register_operand (op, mode))
    return 1;

  if (GET_CODE (op) == CONST_INT && const_ok_for_mcore (INTVAL (op)))
    return 1;

  return 0;
})

;; TODO: Add a comment here.

(define_predicate "mcore_arith_any_imm_operand"
  (match_code "const_int,reg,subreg")
{
  if (register_operand (op, mode))
    return 1;

  if (GET_CODE (op) == CONST_INT)
    return 1;

  return 0;
})

;; Nonzero if OP is a valid source operand for a btsti.

(define_predicate "mcore_literal_K_operand"
  (match_code "const_int")
{
  if (GET_CODE (op) == CONST_INT && CONST_OK_FOR_K (INTVAL (op)))
    return 1;

  return 0;
})

;; Nonzero if OP is a valid source operand for an add/sub insn.

(define_predicate "mcore_addsub_operand"
  (match_code "const_int,reg,subreg")
{
  if (register_operand (op, mode))
    return 1;

  if (GET_CODE (op) == CONST_INT)
    {
      return 1;

      /* The following is removed because it precludes large constants from being
	 returned as valid source operands for and add/sub insn.  While large
	 constants may not directly be used in an add/sub, they may if first loaded
	 into a register.  Thus, this predicate should indicate that they are valid,
	 and the constraint in mcore.md should control whether an additional load to
	 register is needed. (see mcore.md, addsi). -- DAC 4/2/1998  */
      /*
	if (CONST_OK_FOR_J(INTVAL(op)) || CONST_OK_FOR_L(INTVAL(op)))
          return 1;
      */
    }

  return 0;
})

;; Nonzero if OP is a valid source operand for a compare operation.

(define_predicate "mcore_compare_operand"
  (match_code "const_int,reg,subreg")
{
  if (register_operand (op, mode))
    return 1;

  if (GET_CODE (op) == CONST_INT && INTVAL (op) == 0)
    return 1;

  return 0;
})

;; Return 1 if OP is a load multiple operation.  It is known to be a
;; PARALLEL and the first section will be tested.

(define_predicate "mcore_load_multiple_operation"
  (match_code "parallel")
{
  int count = XVECLEN (op, 0);
  int dest_regno;
  rtx src_addr;
  int i;

  /* Perform a quick check so we don't blow up below.  */
  if (count <= 1
      || GET_CODE (XVECEXP (op, 0, 0)) != SET
      || GET_CODE (SET_DEST (XVECEXP (op, 0, 0))) != REG
      || GET_CODE (SET_SRC (XVECEXP (op, 0, 0))) != MEM)
    return 0;

  dest_regno = REGNO (SET_DEST (XVECEXP (op, 0, 0)));
  src_addr = XEXP (SET_SRC (XVECEXP (op, 0, 0)), 0);

  for (i = 1; i < count; i++)
    {
      rtx elt = XVECEXP (op, 0, i);

      if (GET_CODE (elt) != SET
	  || GET_CODE (SET_DEST (elt)) != REG
	  || GET_MODE (SET_DEST (elt)) != SImode
	  || REGNO (SET_DEST (elt))    != (unsigned) (dest_regno + i)
	  || GET_CODE (SET_SRC (elt))  != MEM
	  || GET_MODE (SET_SRC (elt))  != SImode
	  || GET_CODE (XEXP (SET_SRC (elt), 0)) != PLUS
	  || ! rtx_equal_p (XEXP (XEXP (SET_SRC (elt), 0), 0), src_addr)
	  || GET_CODE (XEXP (XEXP (SET_SRC (elt), 0), 1)) != CONST_INT
	  || INTVAL (XEXP (XEXP (SET_SRC (elt), 0), 1)) != i * 4)
	return 0;
    }

  return 1;
})

;; Similar, but tests for store multiple.

(define_predicate "mcore_store_multiple_operation"
  (match_code "parallel")
{
  int count = XVECLEN (op, 0);
  int src_regno;
  rtx dest_addr;
  int i;

  /* Perform a quick check so we don't blow up below.  */
  if (count <= 1
      || GET_CODE (XVECEXP (op, 0, 0)) != SET
      || GET_CODE (SET_DEST (XVECEXP (op, 0, 0))) != MEM
      || GET_CODE (SET_SRC (XVECEXP (op, 0, 0))) != REG)
    return 0;

  src_regno = REGNO (SET_SRC (XVECEXP (op, 0, 0)));
  dest_addr = XEXP (SET_DEST (XVECEXP (op, 0, 0)), 0);

  for (i = 1; i < count; i++)
    {
      rtx elt = XVECEXP (op, 0, i);

      if (GET_CODE (elt) != SET
	  || GET_CODE (SET_SRC (elt)) != REG
	  || GET_MODE (SET_SRC (elt)) != SImode
	  || REGNO (SET_SRC (elt)) != (unsigned) (src_regno + i)
	  || GET_CODE (SET_DEST (elt)) != MEM
	  || GET_MODE (SET_DEST (elt)) != SImode
	  || GET_CODE (XEXP (SET_DEST (elt), 0)) != PLUS
	  || ! rtx_equal_p (XEXP (XEXP (SET_DEST (elt), 0), 0), dest_addr)
	  || GET_CODE (XEXP (XEXP (SET_DEST (elt), 0), 1)) != CONST_INT
	  || INTVAL (XEXP (XEXP (SET_DEST (elt), 0), 1)) != i * 4)
	return 0;
    }

  return 1;
})

;; TODO: Add a comment here.

(define_predicate "mcore_call_address_operand"
  (match_code "reg,subreg,const_int,symbol_ref")
{
  return register_operand (op, mode) || CONSTANT_P (op);
})
