;; Predicate definitions for NEC V850.
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

;; Return true if OP is either a register or 0.

(define_predicate "reg_or_0_operand"
  (match_code "reg,subreg,const_int,const_double")
{
  if (GET_CODE (op) == CONST_INT)
    return INTVAL (op) == 0;

  else if (GET_CODE (op) == CONST_DOUBLE)
    return CONST_DOUBLE_OK_FOR_G (op);

  else
    return register_operand (op, mode);
})

;; Return true if OP is either a register or a signed five bit
;; integer.

(define_predicate "reg_or_int5_operand"
  (match_code "reg,subreg,const_int")
{
  if (GET_CODE (op) == CONST_INT)
    return CONST_OK_FOR_J (INTVAL (op));

  else
    return register_operand (op, mode);
})

;; Return true if OP is either a register or a signed nine bit
;; integer.

(define_predicate "reg_or_int9_operand"
  (match_code "reg,subreg,const_int")
{
  if (GET_CODE (op) == CONST_INT)
    return CONST_OK_FOR_O (INTVAL (op));

  return register_operand (op, mode);
})

;; Return true if OP is either a register or a const integer.

(define_predicate "reg_or_const_operand"
  (match_code "reg,const_int")
{
  if (GET_CODE (op) == CONST_INT)
    return TRUE;

  return register_operand (op, mode);
})

;; Return true if OP is a valid call operand.

(define_predicate "call_address_operand"
  (match_code "reg,symbol_ref")
{
  /* Only registers are valid call operands if TARGET_LONG_CALLS.  */
  if (TARGET_LONG_CALLS)
    return GET_CODE (op) == REG;
  return (GET_CODE (op) == SYMBOL_REF || GET_CODE (op) == REG);
})

;; TODO: Add a comment here.

(define_predicate "movsi_source_operand"
  (match_code "label_ref,symbol_ref,const_int,const_double,const,high,mem,reg,subreg")
{
  /* Some constants, as well as symbolic operands
     must be done with HIGH & LO_SUM patterns.  */
  if (CONSTANT_P (op)
      && GET_CODE (op) != HIGH
      && !(GET_CODE (op) == CONST_INT
           && (CONST_OK_FOR_J (INTVAL (op))
               || CONST_OK_FOR_K (INTVAL (op))
               || CONST_OK_FOR_L (INTVAL (op)))))
    return special_symbolref_operand (op, mode);
  else
    return general_operand (op, mode);
})

;; TODO: Add a comment here.

(define_predicate "special_symbolref_operand"
  (match_code "symbol_ref")
{
  if (GET_CODE (op) == CONST
      && GET_CODE (XEXP (op, 0)) == PLUS
      && GET_CODE (XEXP (XEXP (op, 0), 1)) == CONST_INT
      && CONST_OK_FOR_K (INTVAL (XEXP (XEXP (op, 0), 1))))
    op = XEXP (XEXP (op, 0), 0);

  if (GET_CODE (op) == SYMBOL_REF)
    return (SYMBOL_REF_FLAGS (op)
	    & (SYMBOL_FLAG_ZDA | SYMBOL_FLAG_TDA | SYMBOL_FLAG_SDA)) != 0;

  return FALSE;
})

;; TODO: Add a comment here.

(define_predicate "power_of_two_operand"
  (match_code "const_int")
{
  if (GET_CODE (op) != CONST_INT)
    return 0;

  if (exact_log2 (INTVAL (op)) == -1)
    return 0;
  return 1;
})

;; Return nonzero if the given RTX is suitable for collapsing into a
;; jump to a function prologue.

(define_predicate "pattern_is_ok_for_prologue"
  (match_code "parallel")
{
  int count = XVECLEN (op, 0);
  int i;
  rtx vector_element;

  /* If there are no registers to save then the function prologue
     is not suitable.  */
  if (count <= 2)
    return 0;

  /* The pattern matching has already established that we are adjusting the
     stack and pushing at least one register.  We must now check that the
     remaining entries in the vector to make sure that they are also register
     pushes, except for the last entry which should be a CLOBBER of r10.

     The test below performs the C equivalent of this machine description
     pattern match:

     (set (mem:SI (plus:SI (reg:SI 3)
      (match_operand:SI 2 "immediate_operand" "i")))
      (match_operand:SI 3 "register_is_ok_for_epilogue" "r"))

     */

  for (i = 2; i < count - (TARGET_LONG_CALLS ? 2: 1); i++)
    {
      rtx dest;
      rtx src;
      rtx plus;

      vector_element = XVECEXP (op, 0, i);

      if (GET_CODE (vector_element) != SET)
	return 0;

      dest = SET_DEST (vector_element);
      src = SET_SRC (vector_element);

      if (GET_CODE (dest) != MEM
	  || GET_MODE (dest) != SImode
	  || GET_CODE (src) != REG
	  || GET_MODE (src) != SImode
	  || ! register_is_ok_for_epilogue (src, SImode))
	return 0;

      plus = XEXP (dest, 0);

      if ( GET_CODE (plus) != PLUS
	  || GET_CODE (XEXP (plus, 0)) != REG
	  || GET_MODE (XEXP (plus, 0)) != SImode
	  || REGNO (XEXP (plus, 0)) != STACK_POINTER_REGNUM
	  || GET_CODE (XEXP (plus, 1)) != CONST_INT)
	return 0;

      /* If the register is being pushed somewhere other than the stack
	 space just acquired by the first operand then abandon this quest.
	 Note: the test is <= because both values are negative.	 */
      if (INTVAL (XEXP (plus, 1))
	  <= INTVAL (XEXP (SET_SRC (XVECEXP (op, 0, 0)), 1)))
	{
	  return 0;
	}
    }

  /* Make sure that the last entries in the vector are clobbers.  */
  for (; i < count; i++)
    {
      vector_element = XVECEXP (op, 0, i);

      if (GET_CODE (vector_element) != CLOBBER
	  || GET_CODE (XEXP (vector_element, 0)) != REG
	  || !(REGNO (XEXP (vector_element, 0)) == 10
	       || (TARGET_LONG_CALLS ? (REGNO (XEXP (vector_element, 0)) == 11) : 0 )))
	return 0;
    }

  return 1;
})

;; Return nonzero if the given RTX is suitable for collapsing into
;; jump to a function epilogue.

(define_predicate "pattern_is_ok_for_epilogue"
  (match_code "parallel")
{
  int count = XVECLEN (op, 0);
  int i;

  /* If there are no registers to restore then the function epilogue
     is not suitable.  */
  if (count <= 2)
    return 0;

  /* The pattern matching has already established that we are performing a
     function epilogue and that we are popping at least one register.  We must
     now check the remaining entries in the vector to make sure that they are
     also register pops.  There is no good reason why there should ever be
     anything else in this vector, but being paranoid always helps...

     The test below performs the C equivalent of this machine description
     pattern match:

        (set (match_operand:SI n "register_is_ok_for_epilogue" "r")
	  (mem:SI (plus:SI (reg:SI 3) (match_operand:SI n "immediate_operand" "i"))))
     */

  for (i = 3; i < count; i++)
    {
      rtx vector_element = XVECEXP (op, 0, i);
      rtx dest;
      rtx src;
      rtx plus;

      if (GET_CODE (vector_element) != SET)
	return 0;

      dest = SET_DEST (vector_element);
      src = SET_SRC (vector_element);

      if (GET_CODE (dest) != REG
	  || GET_MODE (dest) != SImode
	  || ! register_is_ok_for_epilogue (dest, SImode)
	  || GET_CODE (src) != MEM
	  || GET_MODE (src) != SImode)
	return 0;

      plus = XEXP (src, 0);

      if (GET_CODE (plus) != PLUS
	  || GET_CODE (XEXP (plus, 0)) != REG
	  || GET_MODE (XEXP (plus, 0)) != SImode
	  || REGNO (XEXP (plus, 0)) != STACK_POINTER_REGNUM
	  || GET_CODE (XEXP (plus, 1)) != CONST_INT)
	return 0;
    }

  return 1;
})

;; Return true if the given RTX is a register which can be restored by
;; a function epilogue.

(define_predicate "register_is_ok_for_epilogue"
  (match_code "reg")
{
  /* The save/restore routines can only cope with registers 20 - 31.  */
  return ((GET_CODE (op) == REG)
          && (((REGNO (op) >= 20) && REGNO (op) <= 31)));
})

;; Return nonzero if the given RTX is suitable for collapsing into a
;; DISPOSE instruction.

(define_predicate "pattern_is_ok_for_dispose"
  (match_code "parallel")
{
  int count = XVECLEN (op, 0);
  int i;

  /* If there are no registers to restore then
     the dispose instruction is not suitable.  */
  if (count <= 2)
    return 0;

  /* The pattern matching has already established that we are performing a
     function epilogue and that we are popping at least one register.  We must
     now check the remaining entries in the vector to make sure that they are
     also register pops.  There is no good reason why there should ever be
     anything else in this vector, but being paranoid always helps...

     The test below performs the C equivalent of this machine description
     pattern match:

        (set (match_operand:SI n "register_is_ok_for_epilogue" "r")
	  (mem:SI (plus:SI (reg:SI 3)
	    (match_operand:SI n "immediate_operand" "i"))))
     */

  for (i = 3; i < count; i++)
    {
      rtx vector_element = XVECEXP (op, 0, i);
      rtx dest;
      rtx src;
      rtx plus;

      if (GET_CODE (vector_element) != SET)
	return 0;

      dest = SET_DEST (vector_element);
      src  = SET_SRC (vector_element);

      if (   GET_CODE (dest) != REG
	  || GET_MODE (dest) != SImode
	  || ! register_is_ok_for_epilogue (dest, SImode)
	  || GET_CODE (src) != MEM
	  || GET_MODE (src) != SImode)
	return 0;

      plus = XEXP (src, 0);

      if (   GET_CODE (plus) != PLUS
	  || GET_CODE (XEXP (plus, 0)) != REG
	  || GET_MODE (XEXP (plus, 0)) != SImode
	  || REGNO    (XEXP (plus, 0)) != STACK_POINTER_REGNUM
	  || GET_CODE (XEXP (plus, 1)) != CONST_INT)
	return 0;
    }

  return 1;
})

;; Return nonzero if the given RTX is suitable for collapsing into a
;; PREPARE instruction.

(define_predicate "pattern_is_ok_for_prepare"
  (match_code "parallel")
{
  int count = XVECLEN (op, 0);
  int i;

  /* If there are no registers to restore then the prepare instruction
     is not suitable.  */
  if (count <= 1)
    return 0;

  /* The pattern matching has already established that we are adjusting the
     stack and pushing at least one register.  We must now check that the
     remaining entries in the vector to make sure that they are also register
     pushes.

     The test below performs the C equivalent of this machine description
     pattern match:

     (set (mem:SI (plus:SI (reg:SI 3)
       (match_operand:SI 2 "immediate_operand" "i")))
         (match_operand:SI 3 "register_is_ok_for_epilogue" "r"))

     */

  for (i = 2; i < count; i++)
    {
      rtx vector_element = XVECEXP (op, 0, i);
      rtx dest;
      rtx src;
      rtx plus;

      if (GET_CODE (vector_element) != SET)
	return 0;

      dest = SET_DEST (vector_element);
      src  = SET_SRC (vector_element);

      if (   GET_CODE (dest) != MEM
	  || GET_MODE (dest) != SImode
	  || GET_CODE (src) != REG
	  || GET_MODE (src) != SImode
	  || ! register_is_ok_for_epilogue (src, SImode)
	     )
	return 0;

      plus = XEXP (dest, 0);

      if (   GET_CODE (plus) != PLUS
	  || GET_CODE (XEXP (plus, 0)) != REG
	  || GET_MODE (XEXP (plus, 0)) != SImode
	  || REGNO    (XEXP (plus, 0)) != STACK_POINTER_REGNUM
	  || GET_CODE (XEXP (plus, 1)) != CONST_INT)
	return 0;

      /* If the register is being pushed somewhere other than the stack
	 space just acquired by the first operand then abandon this quest.
	 Note: the test is <= because both values are negative.	 */
      if (INTVAL (XEXP (plus, 1))
	  <= INTVAL (XEXP (SET_SRC (XVECEXP (op, 0, 0)), 1)))
	return 0;
    }

  return 1;
})

;; TODO: Add a comment here.

(define_predicate "not_power_of_two_operand"
  (match_code "const_int")
{
  unsigned int mask;

  if (mode == QImode)
    mask = 0xff;
  else if (mode == HImode)
    mask = 0xffff;
  else if (mode == SImode)
    mask = 0xffffffff;
  else
    return 0;

  if (GET_CODE (op) != CONST_INT)
    return 0;

  if (exact_log2 (~INTVAL (op) & mask) == -1)
    return 0;
  return 1;
})
