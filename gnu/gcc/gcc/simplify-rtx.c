/* RTL simplification functions for GNU compiler.
   Copyright (C) 1987, 1988, 1989, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007
   Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */


#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "tree.h"
#include "tm_p.h"
#include "regs.h"
#include "hard-reg-set.h"
#include "flags.h"
#include "real.h"
#include "insn-config.h"
#include "recog.h"
#include "function.h"
#include "expr.h"
#include "toplev.h"
#include "output.h"
#include "ggc.h"
#include "target.h"

/* Simplification and canonicalization of RTL.  */

/* Much code operates on (low, high) pairs; the low value is an
   unsigned wide int, the high value a signed wide int.  We
   occasionally need to sign extend from low to high as if low were a
   signed wide int.  */
#define HWI_SIGN_EXTEND(low) \
 ((((HOST_WIDE_INT) low) < 0) ? ((HOST_WIDE_INT) -1) : ((HOST_WIDE_INT) 0))

static rtx neg_const_int (enum machine_mode, rtx);
static bool plus_minus_operand_p (rtx);
static int simplify_plus_minus_op_data_cmp (const void *, const void *);
static rtx simplify_plus_minus (enum rtx_code, enum machine_mode, rtx, rtx);
static rtx simplify_immed_subreg (enum machine_mode, rtx, enum machine_mode,
				  unsigned int);
static rtx simplify_associative_operation (enum rtx_code, enum machine_mode,
					   rtx, rtx);
static rtx simplify_relational_operation_1 (enum rtx_code, enum machine_mode,
					    enum machine_mode, rtx, rtx);
static rtx simplify_unary_operation_1 (enum rtx_code, enum machine_mode, rtx);
static rtx simplify_binary_operation_1 (enum rtx_code, enum machine_mode,
					rtx, rtx, rtx, rtx);

/* Negate a CONST_INT rtx, truncating (because a conversion from a
   maximally negative number can overflow).  */
static rtx
neg_const_int (enum machine_mode mode, rtx i)
{
  return gen_int_mode (- INTVAL (i), mode);
}

/* Test whether expression, X, is an immediate constant that represents
   the most significant bit of machine mode MODE.  */

bool
mode_signbit_p (enum machine_mode mode, rtx x)
{
  unsigned HOST_WIDE_INT val;
  unsigned int width;

  if (GET_MODE_CLASS (mode) != MODE_INT)
    return false;

  width = GET_MODE_BITSIZE (mode);
  if (width == 0)
    return false;
  
  if (width <= HOST_BITS_PER_WIDE_INT
      && GET_CODE (x) == CONST_INT)
    val = INTVAL (x);
  else if (width <= 2 * HOST_BITS_PER_WIDE_INT
	   && GET_CODE (x) == CONST_DOUBLE
	   && CONST_DOUBLE_LOW (x) == 0)
    {
      val = CONST_DOUBLE_HIGH (x);
      width -= HOST_BITS_PER_WIDE_INT;
    }
  else
    return false;

  if (width < HOST_BITS_PER_WIDE_INT)
    val &= ((unsigned HOST_WIDE_INT) 1 << width) - 1;
  return val == ((unsigned HOST_WIDE_INT) 1 << (width - 1));
}

/* Make a binary operation by properly ordering the operands and
   seeing if the expression folds.  */

rtx
simplify_gen_binary (enum rtx_code code, enum machine_mode mode, rtx op0,
		     rtx op1)
{
  rtx tem;

  /* If this simplifies, do it.  */
  tem = simplify_binary_operation (code, mode, op0, op1);
  if (tem)
    return tem;

  /* Put complex operands first and constants second if commutative.  */
  if (GET_RTX_CLASS (code) == RTX_COMM_ARITH
      && swap_commutative_operands_p (op0, op1))
    tem = op0, op0 = op1, op1 = tem;

  return gen_rtx_fmt_ee (code, mode, op0, op1);
}

/* If X is a MEM referencing the constant pool, return the real value.
   Otherwise return X.  */
rtx
avoid_constant_pool_reference (rtx x)
{
  rtx c, tmp, addr;
  enum machine_mode cmode;
  HOST_WIDE_INT offset = 0;

  switch (GET_CODE (x))
    {
    case MEM:
      break;

    case FLOAT_EXTEND:
      /* Handle float extensions of constant pool references.  */
      tmp = XEXP (x, 0);
      c = avoid_constant_pool_reference (tmp);
      if (c != tmp && GET_CODE (c) == CONST_DOUBLE)
	{
	  REAL_VALUE_TYPE d;

	  REAL_VALUE_FROM_CONST_DOUBLE (d, c);
	  return CONST_DOUBLE_FROM_REAL_VALUE (d, GET_MODE (x));
	}
      return x;

    default:
      return x;
    }

  addr = XEXP (x, 0);

  /* Call target hook to avoid the effects of -fpic etc....  */
  addr = targetm.delegitimize_address (addr);

  /* Split the address into a base and integer offset.  */
  if (GET_CODE (addr) == CONST
      && GET_CODE (XEXP (addr, 0)) == PLUS
      && GET_CODE (XEXP (XEXP (addr, 0), 1)) == CONST_INT)
    {
      offset = INTVAL (XEXP (XEXP (addr, 0), 1));
      addr = XEXP (XEXP (addr, 0), 0);
    }

  if (GET_CODE (addr) == LO_SUM)
    addr = XEXP (addr, 1);

  /* If this is a constant pool reference, we can turn it into its
     constant and hope that simplifications happen.  */
  if (GET_CODE (addr) == SYMBOL_REF
      && CONSTANT_POOL_ADDRESS_P (addr))
    {
      c = get_pool_constant (addr);
      cmode = get_pool_mode (addr);

      /* If we're accessing the constant in a different mode than it was
         originally stored, attempt to fix that up via subreg simplifications.
         If that fails we have no choice but to return the original memory.  */
      if (offset != 0 || cmode != GET_MODE (x))
        {
          rtx tem = simplify_subreg (GET_MODE (x), c, cmode, offset);
          if (tem && CONSTANT_P (tem))
            return tem;
        }
      else
        return c;
    }

  return x;
}

/* Return true if X is a MEM referencing the constant pool.  */

bool
constant_pool_reference_p (rtx x)
{
  return avoid_constant_pool_reference (x) != x;
}

/* Make a unary operation by first seeing if it folds and otherwise making
   the specified operation.  */

rtx
simplify_gen_unary (enum rtx_code code, enum machine_mode mode, rtx op,
		    enum machine_mode op_mode)
{
  rtx tem;

  /* If this simplifies, use it.  */
  if ((tem = simplify_unary_operation (code, mode, op, op_mode)) != 0)
    return tem;

  return gen_rtx_fmt_e (code, mode, op);
}

/* Likewise for ternary operations.  */

rtx
simplify_gen_ternary (enum rtx_code code, enum machine_mode mode,
		      enum machine_mode op0_mode, rtx op0, rtx op1, rtx op2)
{
  rtx tem;

  /* If this simplifies, use it.  */
  if (0 != (tem = simplify_ternary_operation (code, mode, op0_mode,
					      op0, op1, op2)))
    return tem;

  return gen_rtx_fmt_eee (code, mode, op0, op1, op2);
}

/* Likewise, for relational operations.
   CMP_MODE specifies mode comparison is done in.  */

rtx
simplify_gen_relational (enum rtx_code code, enum machine_mode mode,
			 enum machine_mode cmp_mode, rtx op0, rtx op1)
{
  rtx tem;

  if (0 != (tem = simplify_relational_operation (code, mode, cmp_mode,
						 op0, op1)))
    return tem;

  return gen_rtx_fmt_ee (code, mode, op0, op1);
}

/* Replace all occurrences of OLD_RTX in X with NEW_RTX and try to simplify the
   resulting RTX.  Return a new RTX which is as simplified as possible.  */

rtx
simplify_replace_rtx (rtx x, rtx old_rtx, rtx new_rtx)
{
  enum rtx_code code = GET_CODE (x);
  enum machine_mode mode = GET_MODE (x);
  enum machine_mode op_mode;
  rtx op0, op1, op2;

  /* If X is OLD_RTX, return NEW_RTX.  Otherwise, if this is an expression, try
     to build a new expression substituting recursively.  If we can't do
     anything, return our input.  */

  if (x == old_rtx)
    return new_rtx;

  switch (GET_RTX_CLASS (code))
    {
    case RTX_UNARY:
      op0 = XEXP (x, 0);
      op_mode = GET_MODE (op0);
      op0 = simplify_replace_rtx (op0, old_rtx, new_rtx);
      if (op0 == XEXP (x, 0))
	return x;
      return simplify_gen_unary (code, mode, op0, op_mode);

    case RTX_BIN_ARITH:
    case RTX_COMM_ARITH:
      op0 = simplify_replace_rtx (XEXP (x, 0), old_rtx, new_rtx);
      op1 = simplify_replace_rtx (XEXP (x, 1), old_rtx, new_rtx);
      if (op0 == XEXP (x, 0) && op1 == XEXP (x, 1))
	return x;
      return simplify_gen_binary (code, mode, op0, op1);

    case RTX_COMPARE:
    case RTX_COMM_COMPARE:
      op0 = XEXP (x, 0);
      op1 = XEXP (x, 1);
      op_mode = GET_MODE (op0) != VOIDmode ? GET_MODE (op0) : GET_MODE (op1);
      op0 = simplify_replace_rtx (op0, old_rtx, new_rtx);
      op1 = simplify_replace_rtx (op1, old_rtx, new_rtx);
      if (op0 == XEXP (x, 0) && op1 == XEXP (x, 1))
	return x;
      return simplify_gen_relational (code, mode, op_mode, op0, op1);

    case RTX_TERNARY:
    case RTX_BITFIELD_OPS:
      op0 = XEXP (x, 0);
      op_mode = GET_MODE (op0);
      op0 = simplify_replace_rtx (op0, old_rtx, new_rtx);
      op1 = simplify_replace_rtx (XEXP (x, 1), old_rtx, new_rtx);
      op2 = simplify_replace_rtx (XEXP (x, 2), old_rtx, new_rtx);
      if (op0 == XEXP (x, 0) && op1 == XEXP (x, 1) && op2 == XEXP (x, 2))
	return x;
      if (op_mode == VOIDmode)
	op_mode = GET_MODE (op0);
      return simplify_gen_ternary (code, mode, op_mode, op0, op1, op2);

    case RTX_EXTRA:
      /* The only case we try to handle is a SUBREG.  */
      if (code == SUBREG)
	{
	  op0 = simplify_replace_rtx (SUBREG_REG (x), old_rtx, new_rtx);
	  if (op0 == SUBREG_REG (x))
	    return x;
	  op0 = simplify_gen_subreg (GET_MODE (x), op0,
				     GET_MODE (SUBREG_REG (x)),
				     SUBREG_BYTE (x));
	  return op0 ? op0 : x;
	}
      break;

    case RTX_OBJ:
      if (code == MEM)
	{
	  op0 = simplify_replace_rtx (XEXP (x, 0), old_rtx, new_rtx);
	  if (op0 == XEXP (x, 0))
	    return x;
	  return replace_equiv_address_nv (x, op0);
	}
      else if (code == LO_SUM)
	{
	  op0 = simplify_replace_rtx (XEXP (x, 0), old_rtx, new_rtx);
	  op1 = simplify_replace_rtx (XEXP (x, 1), old_rtx, new_rtx);

	  /* (lo_sum (high x) x) -> x  */
	  if (GET_CODE (op0) == HIGH && rtx_equal_p (XEXP (op0, 0), op1))
	    return op1;

	  if (op0 == XEXP (x, 0) && op1 == XEXP (x, 1))
	    return x;
	  return gen_rtx_LO_SUM (mode, op0, op1);
	}
      else if (code == REG)
	{
	  if (rtx_equal_p (x, old_rtx))
	    return new_rtx;
	}
      break;

    default:
      break;
    }
  return x;
}

/* Try to simplify a unary operation CODE whose output mode is to be
   MODE with input operand OP whose mode was originally OP_MODE.
   Return zero if no simplification can be made.  */
rtx
simplify_unary_operation (enum rtx_code code, enum machine_mode mode,
			  rtx op, enum machine_mode op_mode)
{
  rtx trueop, tem;

  if (GET_CODE (op) == CONST)
    op = XEXP (op, 0);

  trueop = avoid_constant_pool_reference (op);

  tem = simplify_const_unary_operation (code, mode, trueop, op_mode);
  if (tem)
    return tem;

  return simplify_unary_operation_1 (code, mode, op);
}

/* Perform some simplifications we can do even if the operands
   aren't constant.  */
static rtx
simplify_unary_operation_1 (enum rtx_code code, enum machine_mode mode, rtx op)
{
  enum rtx_code reversed;
  rtx temp;

  switch (code)
    {
    case NOT:
      /* (not (not X)) == X.  */
      if (GET_CODE (op) == NOT)
	return XEXP (op, 0);

      /* (not (eq X Y)) == (ne X Y), etc. if BImode or the result of the
	 comparison is all ones.   */
      if (COMPARISON_P (op)
	  && (mode == BImode || STORE_FLAG_VALUE == -1)
	  && ((reversed = reversed_comparison_code (op, NULL_RTX)) != UNKNOWN))
	return simplify_gen_relational (reversed, mode, VOIDmode,
					XEXP (op, 0), XEXP (op, 1));

      /* (not (plus X -1)) can become (neg X).  */
      if (GET_CODE (op) == PLUS
	  && XEXP (op, 1) == constm1_rtx)
	return simplify_gen_unary (NEG, mode, XEXP (op, 0), mode);

      /* Similarly, (not (neg X)) is (plus X -1).  */
      if (GET_CODE (op) == NEG)
	return plus_constant (XEXP (op, 0), -1);

      /* (not (xor X C)) for C constant is (xor X D) with D = ~C.  */
      if (GET_CODE (op) == XOR
	  && GET_CODE (XEXP (op, 1)) == CONST_INT
	  && (temp = simplify_unary_operation (NOT, mode,
					       XEXP (op, 1), mode)) != 0)
	return simplify_gen_binary (XOR, mode, XEXP (op, 0), temp);

      /* (not (plus X C)) for signbit C is (xor X D) with D = ~C.  */
      if (GET_CODE (op) == PLUS
	  && GET_CODE (XEXP (op, 1)) == CONST_INT
	  && mode_signbit_p (mode, XEXP (op, 1))
	  && (temp = simplify_unary_operation (NOT, mode,
					       XEXP (op, 1), mode)) != 0)
	return simplify_gen_binary (XOR, mode, XEXP (op, 0), temp);


      /* (not (ashift 1 X)) is (rotate ~1 X).  We used to do this for
	 operands other than 1, but that is not valid.  We could do a
	 similar simplification for (not (lshiftrt C X)) where C is
	 just the sign bit, but this doesn't seem common enough to
	 bother with.  */
      if (GET_CODE (op) == ASHIFT
	  && XEXP (op, 0) == const1_rtx)
	{
	  temp = simplify_gen_unary (NOT, mode, const1_rtx, mode);
	  return simplify_gen_binary (ROTATE, mode, temp, XEXP (op, 1));
	}

      /* (not (ashiftrt foo C)) where C is the number of bits in FOO
	 minus 1 is (ge foo (const_int 0)) if STORE_FLAG_VALUE is -1,
	 so we can perform the above simplification.  */
 
      if (STORE_FLAG_VALUE == -1
	  && GET_CODE (op) == ASHIFTRT
	  && GET_CODE (XEXP (op, 1)) == CONST_INT
	  && INTVAL (XEXP (op, 1)) == GET_MODE_BITSIZE (mode) - 1)
	return simplify_gen_relational (GE, mode, VOIDmode,
					XEXP (op, 0), const0_rtx);


      if (GET_CODE (op) == SUBREG
	  && subreg_lowpart_p (op)
	  && (GET_MODE_SIZE (GET_MODE (op))
	      < GET_MODE_SIZE (GET_MODE (SUBREG_REG (op))))
	  && GET_CODE (SUBREG_REG (op)) == ASHIFT
	  && XEXP (SUBREG_REG (op), 0) == const1_rtx)
	{
	  enum machine_mode inner_mode = GET_MODE (SUBREG_REG (op));
	  rtx x;

	  x = gen_rtx_ROTATE (inner_mode,
			      simplify_gen_unary (NOT, inner_mode, const1_rtx,
						  inner_mode),
			      XEXP (SUBREG_REG (op), 1));
	  return rtl_hooks.gen_lowpart_no_emit (mode, x);
	}

      /* Apply De Morgan's laws to reduce number of patterns for machines
	 with negating logical insns (and-not, nand, etc.).  If result has
	 only one NOT, put it first, since that is how the patterns are
	 coded.  */

      if (GET_CODE (op) == IOR || GET_CODE (op) == AND)
	{
	  rtx in1 = XEXP (op, 0), in2 = XEXP (op, 1);
	  enum machine_mode op_mode;

	  op_mode = GET_MODE (in1);
	  in1 = simplify_gen_unary (NOT, op_mode, in1, op_mode);

	  op_mode = GET_MODE (in2);
	  if (op_mode == VOIDmode)
	    op_mode = mode;
	  in2 = simplify_gen_unary (NOT, op_mode, in2, op_mode);

	  if (GET_CODE (in2) == NOT && GET_CODE (in1) != NOT)
	    {
	      rtx tem = in2;
	      in2 = in1; in1 = tem;
	    }

	  return gen_rtx_fmt_ee (GET_CODE (op) == IOR ? AND : IOR,
				 mode, in1, in2);
	}
      break;

    case NEG:
      /* (neg (neg X)) == X.  */
      if (GET_CODE (op) == NEG)
	return XEXP (op, 0);

      /* (neg (plus X 1)) can become (not X).  */
      if (GET_CODE (op) == PLUS
	  && XEXP (op, 1) == const1_rtx)
	return simplify_gen_unary (NOT, mode, XEXP (op, 0), mode);
      
      /* Similarly, (neg (not X)) is (plus X 1).  */
      if (GET_CODE (op) == NOT)
	return plus_constant (XEXP (op, 0), 1);
      
      /* (neg (minus X Y)) can become (minus Y X).  This transformation
	 isn't safe for modes with signed zeros, since if X and Y are
	 both +0, (minus Y X) is the same as (minus X Y).  If the
	 rounding mode is towards +infinity (or -infinity) then the two
	 expressions will be rounded differently.  */
      if (GET_CODE (op) == MINUS
	  && !HONOR_SIGNED_ZEROS (mode)
	  && !HONOR_SIGN_DEPENDENT_ROUNDING (mode))
	return simplify_gen_binary (MINUS, mode, XEXP (op, 1), XEXP (op, 0));
      
      if (GET_CODE (op) == PLUS
	  && !HONOR_SIGNED_ZEROS (mode)
	  && !HONOR_SIGN_DEPENDENT_ROUNDING (mode))
	{
	  /* (neg (plus A C)) is simplified to (minus -C A).  */
	  if (GET_CODE (XEXP (op, 1)) == CONST_INT
	      || GET_CODE (XEXP (op, 1)) == CONST_DOUBLE)
	    {
	      temp = simplify_unary_operation (NEG, mode, XEXP (op, 1), mode);
	      if (temp)
		return simplify_gen_binary (MINUS, mode, temp, XEXP (op, 0));
	    }

	  /* (neg (plus A B)) is canonicalized to (minus (neg A) B).  */
	  temp = simplify_gen_unary (NEG, mode, XEXP (op, 0), mode);
	  return simplify_gen_binary (MINUS, mode, temp, XEXP (op, 1));
	}

      /* (neg (mult A B)) becomes (mult (neg A) B).
	 This works even for floating-point values.  */
      if (GET_CODE (op) == MULT
	  && !HONOR_SIGN_DEPENDENT_ROUNDING (mode))
	{
	  temp = simplify_gen_unary (NEG, mode, XEXP (op, 0), mode);
	  return simplify_gen_binary (MULT, mode, temp, XEXP (op, 1));
	}

      /* NEG commutes with ASHIFT since it is multiplication.  Only do
	 this if we can then eliminate the NEG (e.g., if the operand
	 is a constant).  */
      if (GET_CODE (op) == ASHIFT)
	{
	  temp = simplify_unary_operation (NEG, mode, XEXP (op, 0), mode);
	  if (temp)
	    return simplify_gen_binary (ASHIFT, mode, temp, XEXP (op, 1));
	}

      /* (neg (ashiftrt X C)) can be replaced by (lshiftrt X C) when
	 C is equal to the width of MODE minus 1.  */
      if (GET_CODE (op) == ASHIFTRT
	  && GET_CODE (XEXP (op, 1)) == CONST_INT
	  && INTVAL (XEXP (op, 1)) == GET_MODE_BITSIZE (mode) - 1)
	return simplify_gen_binary (LSHIFTRT, mode,
				    XEXP (op, 0), XEXP (op, 1));

      /* (neg (lshiftrt X C)) can be replaced by (ashiftrt X C) when
	 C is equal to the width of MODE minus 1.  */
      if (GET_CODE (op) == LSHIFTRT
	  && GET_CODE (XEXP (op, 1)) == CONST_INT
	  && INTVAL (XEXP (op, 1)) == GET_MODE_BITSIZE (mode) - 1)
	return simplify_gen_binary (ASHIFTRT, mode,
				    XEXP (op, 0), XEXP (op, 1));
      
      /* (neg (xor A 1)) is (plus A -1) if A is known to be either 0 or 1.  */
      if (GET_CODE (op) == XOR
	  && XEXP (op, 1) == const1_rtx
	  && nonzero_bits (XEXP (op, 0), mode) == 1)
	return plus_constant (XEXP (op, 0), -1);

      /* (neg (lt x 0)) is (ashiftrt X C) if STORE_FLAG_VALUE is 1.  */
      /* (neg (lt x 0)) is (lshiftrt X C) if STORE_FLAG_VALUE is -1.  */
      if (GET_CODE (op) == LT
	  && XEXP (op, 1) == const0_rtx)
	{
	  enum machine_mode inner = GET_MODE (XEXP (op, 0));
	  int isize = GET_MODE_BITSIZE (inner);
	  if (STORE_FLAG_VALUE == 1)
	    {
	      temp = simplify_gen_binary (ASHIFTRT, inner, XEXP (op, 0),
					  GEN_INT (isize - 1));
	      if (mode == inner)
		return temp;
	      if (GET_MODE_BITSIZE (mode) > isize)
		return simplify_gen_unary (SIGN_EXTEND, mode, temp, inner);
	      return simplify_gen_unary (TRUNCATE, mode, temp, inner);
	    }
	  else if (STORE_FLAG_VALUE == -1)
	    {
	      temp = simplify_gen_binary (LSHIFTRT, inner, XEXP (op, 0),
					  GEN_INT (isize - 1));
	      if (mode == inner)
		return temp;
	      if (GET_MODE_BITSIZE (mode) > isize)
		return simplify_gen_unary (ZERO_EXTEND, mode, temp, inner);
	      return simplify_gen_unary (TRUNCATE, mode, temp, inner);
	    }
	}
      break;

    case TRUNCATE:
      /* We can't handle truncation to a partial integer mode here
         because we don't know the real bitsize of the partial
         integer mode.  */
      if (GET_MODE_CLASS (mode) == MODE_PARTIAL_INT)
        break;

      /* (truncate:SI ({sign,zero}_extend:DI foo:SI)) == foo:SI.  */
      if ((GET_CODE (op) == SIGN_EXTEND
	   || GET_CODE (op) == ZERO_EXTEND)
	  && GET_MODE (XEXP (op, 0)) == mode)
	return XEXP (op, 0);

      /* (truncate:SI (OP:DI ({sign,zero}_extend:DI foo:SI))) is
	 (OP:SI foo:SI) if OP is NEG or ABS.  */
      if ((GET_CODE (op) == ABS
	   || GET_CODE (op) == NEG)
	  && (GET_CODE (XEXP (op, 0)) == SIGN_EXTEND
	      || GET_CODE (XEXP (op, 0)) == ZERO_EXTEND)
	  && GET_MODE (XEXP (XEXP (op, 0), 0)) == mode)
	return simplify_gen_unary (GET_CODE (op), mode,
				   XEXP (XEXP (op, 0), 0), mode);

      /* (truncate:A (subreg:B (truncate:C X) 0)) is
	 (truncate:A X).  */
      if (GET_CODE (op) == SUBREG
	  && GET_CODE (SUBREG_REG (op)) == TRUNCATE
	  && subreg_lowpart_p (op))
	return simplify_gen_unary (TRUNCATE, mode, XEXP (SUBREG_REG (op), 0),
				   GET_MODE (XEXP (SUBREG_REG (op), 0)));

      /* If we know that the value is already truncated, we can
         replace the TRUNCATE with a SUBREG.  Note that this is also
         valid if TRULY_NOOP_TRUNCATION is false for the corresponding
         modes we just have to apply a different definition for
         truncation.  But don't do this for an (LSHIFTRT (MULT ...)) 
         since this will cause problems with the umulXi3_highpart
         patterns.  */
      if ((TRULY_NOOP_TRUNCATION (GET_MODE_BITSIZE (mode),
				 GET_MODE_BITSIZE (GET_MODE (op)))
	   ? (num_sign_bit_copies (op, GET_MODE (op))
	      > (unsigned int) (GET_MODE_BITSIZE (GET_MODE (op))
				- GET_MODE_BITSIZE (mode)))
	   : truncated_to_mode (mode, op))
	  && ! (GET_CODE (op) == LSHIFTRT
		&& GET_CODE (XEXP (op, 0)) == MULT))
	return rtl_hooks.gen_lowpart_no_emit (mode, op);

      /* A truncate of a comparison can be replaced with a subreg if
         STORE_FLAG_VALUE permits.  This is like the previous test,
         but it works even if the comparison is done in a mode larger
         than HOST_BITS_PER_WIDE_INT.  */
      if (GET_MODE_BITSIZE (mode) <= HOST_BITS_PER_WIDE_INT
	  && COMPARISON_P (op)
	  && ((HOST_WIDE_INT) STORE_FLAG_VALUE & ~GET_MODE_MASK (mode)) == 0)
	return rtl_hooks.gen_lowpart_no_emit (mode, op);
      break;

    case FLOAT_TRUNCATE:
      if (DECIMAL_FLOAT_MODE_P (mode))
	break;

      /* (float_truncate:SF (float_extend:DF foo:SF)) = foo:SF.  */
      if (GET_CODE (op) == FLOAT_EXTEND
	  && GET_MODE (XEXP (op, 0)) == mode)
	return XEXP (op, 0);

      /* (float_truncate:SF (float_truncate:DF foo:XF))
         = (float_truncate:SF foo:XF).
	 This may eliminate double rounding, so it is unsafe.

         (float_truncate:SF (float_extend:XF foo:DF))
         = (float_truncate:SF foo:DF).

         (float_truncate:DF (float_extend:XF foo:SF))
         = (float_extend:SF foo:DF).  */
      if ((GET_CODE (op) == FLOAT_TRUNCATE
	   && flag_unsafe_math_optimizations)
	  || GET_CODE (op) == FLOAT_EXTEND)
	return simplify_gen_unary (GET_MODE_SIZE (GET_MODE (XEXP (op,
							    0)))
				   > GET_MODE_SIZE (mode)
				   ? FLOAT_TRUNCATE : FLOAT_EXTEND,
				   mode,
				   XEXP (op, 0), mode);

      /*  (float_truncate (float x)) is (float x)  */
      if (GET_CODE (op) == FLOAT
	  && (flag_unsafe_math_optimizations
	      || ((unsigned)significand_size (GET_MODE (op))
		  >= (GET_MODE_BITSIZE (GET_MODE (XEXP (op, 0)))
		      - num_sign_bit_copies (XEXP (op, 0),
					     GET_MODE (XEXP (op, 0)))))))
	return simplify_gen_unary (FLOAT, mode,
				   XEXP (op, 0),
				   GET_MODE (XEXP (op, 0)));

      /* (float_truncate:SF (OP:DF (float_extend:DF foo:sf))) is
	 (OP:SF foo:SF) if OP is NEG or ABS.  */
      if ((GET_CODE (op) == ABS
	   || GET_CODE (op) == NEG)
	  && GET_CODE (XEXP (op, 0)) == FLOAT_EXTEND
	  && GET_MODE (XEXP (XEXP (op, 0), 0)) == mode)
	return simplify_gen_unary (GET_CODE (op), mode,
				   XEXP (XEXP (op, 0), 0), mode);

      /* (float_truncate:SF (subreg:DF (float_truncate:SF X) 0))
	 is (float_truncate:SF x).  */
      if (GET_CODE (op) == SUBREG
	  && subreg_lowpart_p (op)
	  && GET_CODE (SUBREG_REG (op)) == FLOAT_TRUNCATE)
	return SUBREG_REG (op);
      break;

    case FLOAT_EXTEND:
      if (DECIMAL_FLOAT_MODE_P (mode))
	break;

      /*  (float_extend (float_extend x)) is (float_extend x)

	  (float_extend (float x)) is (float x) assuming that double
	  rounding can't happen.
          */
      if (GET_CODE (op) == FLOAT_EXTEND
	  || (GET_CODE (op) == FLOAT
	      && ((unsigned)significand_size (GET_MODE (op))
		  >= (GET_MODE_BITSIZE (GET_MODE (XEXP (op, 0)))
		      - num_sign_bit_copies (XEXP (op, 0),
					     GET_MODE (XEXP (op, 0)))))))
	return simplify_gen_unary (GET_CODE (op), mode,
				   XEXP (op, 0),
				   GET_MODE (XEXP (op, 0)));

      break;

    case ABS:
      /* (abs (neg <foo>)) -> (abs <foo>) */
      if (GET_CODE (op) == NEG)
	return simplify_gen_unary (ABS, mode, XEXP (op, 0),
				   GET_MODE (XEXP (op, 0)));

      /* If the mode of the operand is VOIDmode (i.e. if it is ASM_OPERANDS),
         do nothing.  */
      if (GET_MODE (op) == VOIDmode)
	break;

      /* If operand is something known to be positive, ignore the ABS.  */
      if (GET_CODE (op) == FFS || GET_CODE (op) == ABS
	  || ((GET_MODE_BITSIZE (GET_MODE (op))
	       <= HOST_BITS_PER_WIDE_INT)
	      && ((nonzero_bits (op, GET_MODE (op))
		   & ((HOST_WIDE_INT) 1
		      << (GET_MODE_BITSIZE (GET_MODE (op)) - 1)))
		  == 0)))
	return op;

      /* If operand is known to be only -1 or 0, convert ABS to NEG.  */
      if (num_sign_bit_copies (op, mode) == GET_MODE_BITSIZE (mode))
	return gen_rtx_NEG (mode, op);

      break;

    case FFS:
      /* (ffs (*_extend <X>)) = (ffs <X>) */
      if (GET_CODE (op) == SIGN_EXTEND
	  || GET_CODE (op) == ZERO_EXTEND)
	return simplify_gen_unary (FFS, mode, XEXP (op, 0),
				   GET_MODE (XEXP (op, 0)));
      break;

    case POPCOUNT:
    case PARITY:
      /* (pop* (zero_extend <X>)) = (pop* <X>) */
      if (GET_CODE (op) == ZERO_EXTEND)
	return simplify_gen_unary (code, mode, XEXP (op, 0),
				   GET_MODE (XEXP (op, 0)));
      break;

    case FLOAT:
      /* (float (sign_extend <X>)) = (float <X>).  */
      if (GET_CODE (op) == SIGN_EXTEND)
	return simplify_gen_unary (FLOAT, mode, XEXP (op, 0),
				   GET_MODE (XEXP (op, 0)));
      break;

    case SIGN_EXTEND:
      /* (sign_extend (truncate (minus (label_ref L1) (label_ref L2))))
	 becomes just the MINUS if its mode is MODE.  This allows
	 folding switch statements on machines using casesi (such as
	 the VAX).  */
      if (GET_CODE (op) == TRUNCATE
	  && GET_MODE (XEXP (op, 0)) == mode
	  && GET_CODE (XEXP (op, 0)) == MINUS
	  && GET_CODE (XEXP (XEXP (op, 0), 0)) == LABEL_REF
	  && GET_CODE (XEXP (XEXP (op, 0), 1)) == LABEL_REF)
	return XEXP (op, 0);

      /* Check for a sign extension of a subreg of a promoted
	 variable, where the promotion is sign-extended, and the
	 target mode is the same as the variable's promotion.  */
      if (GET_CODE (op) == SUBREG
	  && SUBREG_PROMOTED_VAR_P (op)
	  && ! SUBREG_PROMOTED_UNSIGNED_P (op)
	  && GET_MODE (XEXP (op, 0)) == mode)
	return XEXP (op, 0);

#if defined(POINTERS_EXTEND_UNSIGNED) && !defined(HAVE_ptr_extend)
      if (! POINTERS_EXTEND_UNSIGNED
	  && mode == Pmode && GET_MODE (op) == ptr_mode
	  && (CONSTANT_P (op)
	      || (GET_CODE (op) == SUBREG
		  && REG_P (SUBREG_REG (op))
		  && REG_POINTER (SUBREG_REG (op))
		  && GET_MODE (SUBREG_REG (op)) == Pmode)))
	return convert_memory_address (Pmode, op);
#endif
      break;

    case ZERO_EXTEND:
      /* Check for a zero extension of a subreg of a promoted
	 variable, where the promotion is zero-extended, and the
	 target mode is the same as the variable's promotion.  */
      if (GET_CODE (op) == SUBREG
	  && SUBREG_PROMOTED_VAR_P (op)
	  && SUBREG_PROMOTED_UNSIGNED_P (op) > 0
	  && GET_MODE (XEXP (op, 0)) == mode)
	return XEXP (op, 0);

#if defined(POINTERS_EXTEND_UNSIGNED) && !defined(HAVE_ptr_extend)
      if (POINTERS_EXTEND_UNSIGNED > 0
	  && mode == Pmode && GET_MODE (op) == ptr_mode
	  && (CONSTANT_P (op)
	      || (GET_CODE (op) == SUBREG
		  && REG_P (SUBREG_REG (op))
		  && REG_POINTER (SUBREG_REG (op))
		  && GET_MODE (SUBREG_REG (op)) == Pmode)))
	return convert_memory_address (Pmode, op);
#endif
      break;

    default:
      break;
    }
  
  return 0;
}

/* Try to compute the value of a unary operation CODE whose output mode is to
   be MODE with input operand OP whose mode was originally OP_MODE.
   Return zero if the value cannot be computed.  */
rtx
simplify_const_unary_operation (enum rtx_code code, enum machine_mode mode,
				rtx op, enum machine_mode op_mode)
{
  unsigned int width = GET_MODE_BITSIZE (mode);

  if (code == VEC_DUPLICATE)
    {
      gcc_assert (VECTOR_MODE_P (mode));
      if (GET_MODE (op) != VOIDmode)
      {
	if (!VECTOR_MODE_P (GET_MODE (op)))
	  gcc_assert (GET_MODE_INNER (mode) == GET_MODE (op));
	else
	  gcc_assert (GET_MODE_INNER (mode) == GET_MODE_INNER
						(GET_MODE (op)));
      }
      if (GET_CODE (op) == CONST_INT || GET_CODE (op) == CONST_DOUBLE
	  || GET_CODE (op) == CONST_VECTOR)
	{
          int elt_size = GET_MODE_SIZE (GET_MODE_INNER (mode));
          unsigned n_elts = (GET_MODE_SIZE (mode) / elt_size);
	  rtvec v = rtvec_alloc (n_elts);
	  unsigned int i;

	  if (GET_CODE (op) != CONST_VECTOR)
	    for (i = 0; i < n_elts; i++)
	      RTVEC_ELT (v, i) = op;
	  else
	    {
	      enum machine_mode inmode = GET_MODE (op);
              int in_elt_size = GET_MODE_SIZE (GET_MODE_INNER (inmode));
              unsigned in_n_elts = (GET_MODE_SIZE (inmode) / in_elt_size);

	      gcc_assert (in_n_elts < n_elts);
	      gcc_assert ((n_elts % in_n_elts) == 0);
	      for (i = 0; i < n_elts; i++)
	        RTVEC_ELT (v, i) = CONST_VECTOR_ELT (op, i % in_n_elts);
	    }
	  return gen_rtx_CONST_VECTOR (mode, v);
	}
    }

  if (VECTOR_MODE_P (mode) && GET_CODE (op) == CONST_VECTOR)
    {
      int elt_size = GET_MODE_SIZE (GET_MODE_INNER (mode));
      unsigned n_elts = (GET_MODE_SIZE (mode) / elt_size);
      enum machine_mode opmode = GET_MODE (op);
      int op_elt_size = GET_MODE_SIZE (GET_MODE_INNER (opmode));
      unsigned op_n_elts = (GET_MODE_SIZE (opmode) / op_elt_size);
      rtvec v = rtvec_alloc (n_elts);
      unsigned int i;

      gcc_assert (op_n_elts == n_elts);
      for (i = 0; i < n_elts; i++)
	{
	  rtx x = simplify_unary_operation (code, GET_MODE_INNER (mode),
					    CONST_VECTOR_ELT (op, i),
					    GET_MODE_INNER (opmode));
	  if (!x)
	    return 0;
	  RTVEC_ELT (v, i) = x;
	}
      return gen_rtx_CONST_VECTOR (mode, v);
    }

  /* The order of these tests is critical so that, for example, we don't
     check the wrong mode (input vs. output) for a conversion operation,
     such as FIX.  At some point, this should be simplified.  */

  if (code == FLOAT && GET_MODE (op) == VOIDmode
      && (GET_CODE (op) == CONST_DOUBLE || GET_CODE (op) == CONST_INT))
    {
      HOST_WIDE_INT hv, lv;
      REAL_VALUE_TYPE d;

      if (GET_CODE (op) == CONST_INT)
	lv = INTVAL (op), hv = HWI_SIGN_EXTEND (lv);
      else
	lv = CONST_DOUBLE_LOW (op),  hv = CONST_DOUBLE_HIGH (op);

      REAL_VALUE_FROM_INT (d, lv, hv, mode);
      d = real_value_truncate (mode, d);
      return CONST_DOUBLE_FROM_REAL_VALUE (d, mode);
    }
  else if (code == UNSIGNED_FLOAT && GET_MODE (op) == VOIDmode
	   && (GET_CODE (op) == CONST_DOUBLE
	       || GET_CODE (op) == CONST_INT))
    {
      HOST_WIDE_INT hv, lv;
      REAL_VALUE_TYPE d;

      if (GET_CODE (op) == CONST_INT)
	lv = INTVAL (op), hv = HWI_SIGN_EXTEND (lv);
      else
	lv = CONST_DOUBLE_LOW (op),  hv = CONST_DOUBLE_HIGH (op);

      if (op_mode == VOIDmode)
	{
	  /* We don't know how to interpret negative-looking numbers in
	     this case, so don't try to fold those.  */
	  if (hv < 0)
	    return 0;
	}
      else if (GET_MODE_BITSIZE (op_mode) >= HOST_BITS_PER_WIDE_INT * 2)
	;
      else
	hv = 0, lv &= GET_MODE_MASK (op_mode);

      REAL_VALUE_FROM_UNSIGNED_INT (d, lv, hv, mode);
      d = real_value_truncate (mode, d);
      return CONST_DOUBLE_FROM_REAL_VALUE (d, mode);
    }

  if (GET_CODE (op) == CONST_INT
      && width <= HOST_BITS_PER_WIDE_INT && width > 0)
    {
      HOST_WIDE_INT arg0 = INTVAL (op);
      HOST_WIDE_INT val;

      switch (code)
	{
	case NOT:
	  val = ~ arg0;
	  break;

	case NEG:
	  val = - arg0;
	  break;

	case ABS:
	  val = (arg0 >= 0 ? arg0 : - arg0);
	  break;

	case FFS:
	  /* Don't use ffs here.  Instead, get low order bit and then its
	     number.  If arg0 is zero, this will return 0, as desired.  */
	  arg0 &= GET_MODE_MASK (mode);
	  val = exact_log2 (arg0 & (- arg0)) + 1;
	  break;

	case CLZ:
	  arg0 &= GET_MODE_MASK (mode);
	  if (arg0 == 0 && CLZ_DEFINED_VALUE_AT_ZERO (mode, val))
	    ;
	  else
	    val = GET_MODE_BITSIZE (mode) - floor_log2 (arg0) - 1;
	  break;

	case CTZ:
	  arg0 &= GET_MODE_MASK (mode);
	  if (arg0 == 0)
	    {
	      /* Even if the value at zero is undefined, we have to come
		 up with some replacement.  Seems good enough.  */
	      if (! CTZ_DEFINED_VALUE_AT_ZERO (mode, val))
		val = GET_MODE_BITSIZE (mode);
	    }
	  else
	    val = exact_log2 (arg0 & -arg0);
	  break;

	case POPCOUNT:
	  arg0 &= GET_MODE_MASK (mode);
	  val = 0;
	  while (arg0)
	    val++, arg0 &= arg0 - 1;
	  break;

	case PARITY:
	  arg0 &= GET_MODE_MASK (mode);
	  val = 0;
	  while (arg0)
	    val++, arg0 &= arg0 - 1;
	  val &= 1;
	  break;

	case TRUNCATE:
	  val = arg0;
	  break;

	case ZERO_EXTEND:
	  /* When zero-extending a CONST_INT, we need to know its
             original mode.  */
	  gcc_assert (op_mode != VOIDmode);
	  if (GET_MODE_BITSIZE (op_mode) == HOST_BITS_PER_WIDE_INT)
	    {
	      /* If we were really extending the mode,
		 we would have to distinguish between zero-extension
		 and sign-extension.  */
	      gcc_assert (width == GET_MODE_BITSIZE (op_mode));
	      val = arg0;
	    }
	  else if (GET_MODE_BITSIZE (op_mode) < HOST_BITS_PER_WIDE_INT)
	    val = arg0 & ~((HOST_WIDE_INT) (-1) << GET_MODE_BITSIZE (op_mode));
	  else
	    return 0;
	  break;

	case SIGN_EXTEND:
	  if (op_mode == VOIDmode)
	    op_mode = mode;
	  if (GET_MODE_BITSIZE (op_mode) == HOST_BITS_PER_WIDE_INT)
	    {
	      /* If we were really extending the mode,
		 we would have to distinguish between zero-extension
		 and sign-extension.  */
	      gcc_assert (width == GET_MODE_BITSIZE (op_mode));
	      val = arg0;
	    }
	  else if (GET_MODE_BITSIZE (op_mode) < HOST_BITS_PER_WIDE_INT)
	    {
	      val
		= arg0 & ~((HOST_WIDE_INT) (-1) << GET_MODE_BITSIZE (op_mode));
	      if (val
		  & ((HOST_WIDE_INT) 1 << (GET_MODE_BITSIZE (op_mode) - 1)))
		val -= (HOST_WIDE_INT) 1 << GET_MODE_BITSIZE (op_mode);
	    }
	  else
	    return 0;
	  break;

	case SQRT:
	case FLOAT_EXTEND:
	case FLOAT_TRUNCATE:
	case SS_TRUNCATE:
	case US_TRUNCATE:
	case SS_NEG:
	  return 0;

	default:
	  gcc_unreachable ();
	}

      return gen_int_mode (val, mode);
    }

  /* We can do some operations on integer CONST_DOUBLEs.  Also allow
     for a DImode operation on a CONST_INT.  */
  else if (GET_MODE (op) == VOIDmode
	   && width <= HOST_BITS_PER_WIDE_INT * 2
	   && (GET_CODE (op) == CONST_DOUBLE
	       || GET_CODE (op) == CONST_INT))
    {
      unsigned HOST_WIDE_INT l1, lv;
      HOST_WIDE_INT h1, hv;

      if (GET_CODE (op) == CONST_DOUBLE)
	l1 = CONST_DOUBLE_LOW (op), h1 = CONST_DOUBLE_HIGH (op);
      else
	l1 = INTVAL (op), h1 = HWI_SIGN_EXTEND (l1);

      switch (code)
	{
	case NOT:
	  lv = ~ l1;
	  hv = ~ h1;
	  break;

	case NEG:
	  neg_double (l1, h1, &lv, &hv);
	  break;

	case ABS:
	  if (h1 < 0)
	    neg_double (l1, h1, &lv, &hv);
	  else
	    lv = l1, hv = h1;
	  break;

	case FFS:
	  hv = 0;
	  if (l1 == 0)
	    {
	      if (h1 == 0)
		lv = 0;
	      else
		lv = HOST_BITS_PER_WIDE_INT + exact_log2 (h1 & -h1) + 1;
	    }
	  else
	    lv = exact_log2 (l1 & -l1) + 1;
	  break;

	case CLZ:
	  hv = 0;
	  if (h1 != 0)
	    lv = GET_MODE_BITSIZE (mode) - floor_log2 (h1) - 1
	      - HOST_BITS_PER_WIDE_INT;
	  else if (l1 != 0)
	    lv = GET_MODE_BITSIZE (mode) - floor_log2 (l1) - 1;
	  else if (! CLZ_DEFINED_VALUE_AT_ZERO (mode, lv))
	    lv = GET_MODE_BITSIZE (mode);
	  break;

	case CTZ:
	  hv = 0;
	  if (l1 != 0)
	    lv = exact_log2 (l1 & -l1);
	  else if (h1 != 0)
	    lv = HOST_BITS_PER_WIDE_INT + exact_log2 (h1 & -h1);
	  else if (! CTZ_DEFINED_VALUE_AT_ZERO (mode, lv))
	    lv = GET_MODE_BITSIZE (mode);
	  break;

	case POPCOUNT:
	  hv = 0;
	  lv = 0;
	  while (l1)
	    lv++, l1 &= l1 - 1;
	  while (h1)
	    lv++, h1 &= h1 - 1;
	  break;

	case PARITY:
	  hv = 0;
	  lv = 0;
	  while (l1)
	    lv++, l1 &= l1 - 1;
	  while (h1)
	    lv++, h1 &= h1 - 1;
	  lv &= 1;
	  break;

	case TRUNCATE:
	  /* This is just a change-of-mode, so do nothing.  */
	  lv = l1, hv = h1;
	  break;

	case ZERO_EXTEND:
	  gcc_assert (op_mode != VOIDmode);

	  if (GET_MODE_BITSIZE (op_mode) > HOST_BITS_PER_WIDE_INT)
	    return 0;

	  hv = 0;
	  lv = l1 & GET_MODE_MASK (op_mode);
	  break;

	case SIGN_EXTEND:
	  if (op_mode == VOIDmode
	      || GET_MODE_BITSIZE (op_mode) > HOST_BITS_PER_WIDE_INT)
	    return 0;
	  else
	    {
	      lv = l1 & GET_MODE_MASK (op_mode);
	      if (GET_MODE_BITSIZE (op_mode) < HOST_BITS_PER_WIDE_INT
		  && (lv & ((HOST_WIDE_INT) 1
			    << (GET_MODE_BITSIZE (op_mode) - 1))) != 0)
		lv -= (HOST_WIDE_INT) 1 << GET_MODE_BITSIZE (op_mode);

	      hv = HWI_SIGN_EXTEND (lv);
	    }
	  break;

	case SQRT:
	  return 0;

	default:
	  return 0;
	}

      return immed_double_const (lv, hv, mode);
    }

  else if (GET_CODE (op) == CONST_DOUBLE
	   && SCALAR_FLOAT_MODE_P (mode))
    {
      REAL_VALUE_TYPE d, t;
      REAL_VALUE_FROM_CONST_DOUBLE (d, op);

      switch (code)
	{
	case SQRT:
	  if (HONOR_SNANS (mode) && real_isnan (&d))
	    return 0;
	  real_sqrt (&t, mode, &d);
	  d = t;
	  break;
	case ABS:
	  d = REAL_VALUE_ABS (d);
	  break;
	case NEG:
	  d = REAL_VALUE_NEGATE (d);
	  break;
	case FLOAT_TRUNCATE:
	  d = real_value_truncate (mode, d);
	  break;
	case FLOAT_EXTEND:
	  /* All this does is change the mode.  */
	  break;
	case FIX:
	  real_arithmetic (&d, FIX_TRUNC_EXPR, &d, NULL);
	  break;
	case NOT:
	  {
	    long tmp[4];
	    int i;

	    real_to_target (tmp, &d, GET_MODE (op));
	    for (i = 0; i < 4; i++)
	      tmp[i] = ~tmp[i];
	    real_from_target (&d, tmp, mode);
	    break;
	  }
	default:
	  gcc_unreachable ();
	}
      return CONST_DOUBLE_FROM_REAL_VALUE (d, mode);
    }

  else if (GET_CODE (op) == CONST_DOUBLE
	   && SCALAR_FLOAT_MODE_P (GET_MODE (op))
	   && GET_MODE_CLASS (mode) == MODE_INT
	   && width <= 2*HOST_BITS_PER_WIDE_INT && width > 0)
    {
      /* Although the overflow semantics of RTL's FIX and UNSIGNED_FIX
	 operators are intentionally left unspecified (to ease implementation
	 by target backends), for consistency, this routine implements the
	 same semantics for constant folding as used by the middle-end.  */

      /* This was formerly used only for non-IEEE float.
	 eggert@twinsun.com says it is safe for IEEE also.  */
      HOST_WIDE_INT xh, xl, th, tl;
      REAL_VALUE_TYPE x, t;
      REAL_VALUE_FROM_CONST_DOUBLE (x, op);
      switch (code)
	{
	case FIX:
	  if (REAL_VALUE_ISNAN (x))
	    return const0_rtx;

	  /* Test against the signed upper bound.  */
	  if (width > HOST_BITS_PER_WIDE_INT)
	    {
	      th = ((unsigned HOST_WIDE_INT) 1
		    << (width - HOST_BITS_PER_WIDE_INT - 1)) - 1;
	      tl = -1;
	    }
	  else
	    {
	      th = 0;
	      tl = ((unsigned HOST_WIDE_INT) 1 << (width - 1)) - 1;
	    }
	  real_from_integer (&t, VOIDmode, tl, th, 0);
	  if (REAL_VALUES_LESS (t, x))
	    {
	      xh = th;
	      xl = tl;
	      break;
	    }

	  /* Test against the signed lower bound.  */
	  if (width > HOST_BITS_PER_WIDE_INT)
	    {
	      th = (HOST_WIDE_INT) -1 << (width - HOST_BITS_PER_WIDE_INT - 1);
	      tl = 0;
	    }
	  else
	    {
	      th = -1;
	      tl = (HOST_WIDE_INT) -1 << (width - 1);
	    }
	  real_from_integer (&t, VOIDmode, tl, th, 0);
	  if (REAL_VALUES_LESS (x, t))
	    {
	      xh = th;
	      xl = tl;
	      break;
	    }
	  REAL_VALUE_TO_INT (&xl, &xh, x);
	  break;

	case UNSIGNED_FIX:
	  if (REAL_VALUE_ISNAN (x) || REAL_VALUE_NEGATIVE (x))
	    return const0_rtx;

	  /* Test against the unsigned upper bound.  */
	  if (width == 2*HOST_BITS_PER_WIDE_INT)
	    {
	      th = -1;
	      tl = -1;
	    }
	  else if (width >= HOST_BITS_PER_WIDE_INT)
	    {
	      th = ((unsigned HOST_WIDE_INT) 1
		    << (width - HOST_BITS_PER_WIDE_INT)) - 1;
	      tl = -1;
	    }
	  else
	    {
	      th = 0;
	      tl = ((unsigned HOST_WIDE_INT) 1 << width) - 1;
	    }
	  real_from_integer (&t, VOIDmode, tl, th, 1);
	  if (REAL_VALUES_LESS (t, x))
	    {
	      xh = th;
	      xl = tl;
	      break;
	    }

	  REAL_VALUE_TO_INT (&xl, &xh, x);
	  break;

	default:
	  gcc_unreachable ();
	}
      return immed_double_const (xl, xh, mode);
    }

  return NULL_RTX;
}

/* Subroutine of simplify_binary_operation to simplify a commutative,
   associative binary operation CODE with result mode MODE, operating
   on OP0 and OP1.  CODE is currently one of PLUS, MULT, AND, IOR, XOR,
   SMIN, SMAX, UMIN or UMAX.  Return zero if no simplification or
   canonicalization is possible.  */

static rtx
simplify_associative_operation (enum rtx_code code, enum machine_mode mode,
				rtx op0, rtx op1)
{
  rtx tem;

  /* Linearize the operator to the left.  */
  if (GET_CODE (op1) == code)
    {
      /* "(a op b) op (c op d)" becomes "((a op b) op c) op d)".  */
      if (GET_CODE (op0) == code)
	{
	  tem = simplify_gen_binary (code, mode, op0, XEXP (op1, 0));
	  return simplify_gen_binary (code, mode, tem, XEXP (op1, 1));
	}

      /* "a op (b op c)" becomes "(b op c) op a".  */
      if (! swap_commutative_operands_p (op1, op0))
	return simplify_gen_binary (code, mode, op1, op0);

      tem = op0;
      op0 = op1;
      op1 = tem;
    }

  if (GET_CODE (op0) == code)
    {
      /* Canonicalize "(x op c) op y" as "(x op y) op c".  */
      if (swap_commutative_operands_p (XEXP (op0, 1), op1))
	{
	  tem = simplify_gen_binary (code, mode, XEXP (op0, 0), op1);
	  return simplify_gen_binary (code, mode, tem, XEXP (op0, 1));
	}

      /* Attempt to simplify "(a op b) op c" as "a op (b op c)".  */
      tem = swap_commutative_operands_p (XEXP (op0, 1), op1)
	    ? simplify_binary_operation (code, mode, op1, XEXP (op0, 1))
	    : simplify_binary_operation (code, mode, XEXP (op0, 1), op1);
      if (tem != 0)
        return simplify_gen_binary (code, mode, XEXP (op0, 0), tem);

      /* Attempt to simplify "(a op b) op c" as "(a op c) op b".  */
      tem = swap_commutative_operands_p (XEXP (op0, 0), op1)
	    ? simplify_binary_operation (code, mode, op1, XEXP (op0, 0))
	    : simplify_binary_operation (code, mode, XEXP (op0, 0), op1);
      if (tem != 0)
        return simplify_gen_binary (code, mode, tem, XEXP (op0, 1));
    }

  return 0;
}


/* Simplify a binary operation CODE with result mode MODE, operating on OP0
   and OP1.  Return 0 if no simplification is possible.

   Don't use this for relational operations such as EQ or LT.
   Use simplify_relational_operation instead.  */
rtx
simplify_binary_operation (enum rtx_code code, enum machine_mode mode,
			   rtx op0, rtx op1)
{
  rtx trueop0, trueop1;
  rtx tem;

  /* Relational operations don't work here.  We must know the mode
     of the operands in order to do the comparison correctly.
     Assuming a full word can give incorrect results.
     Consider comparing 128 with -128 in QImode.  */
  gcc_assert (GET_RTX_CLASS (code) != RTX_COMPARE);
  gcc_assert (GET_RTX_CLASS (code) != RTX_COMM_COMPARE);

  /* Make sure the constant is second.  */
  if (GET_RTX_CLASS (code) == RTX_COMM_ARITH
      && swap_commutative_operands_p (op0, op1))
    {
      tem = op0, op0 = op1, op1 = tem;
    }

  trueop0 = avoid_constant_pool_reference (op0);
  trueop1 = avoid_constant_pool_reference (op1);

  tem = simplify_const_binary_operation (code, mode, trueop0, trueop1);
  if (tem)
    return tem;
  return simplify_binary_operation_1 (code, mode, op0, op1, trueop0, trueop1);
}

/* Subroutine of simplify_binary_operation.  Simplify a binary operation
   CODE with result mode MODE, operating on OP0 and OP1.  If OP0 and/or
   OP1 are constant pool references, TRUEOP0 and TRUEOP1 represent the
   actual constants.  */

static rtx
simplify_binary_operation_1 (enum rtx_code code, enum machine_mode mode,
			     rtx op0, rtx op1, rtx trueop0, rtx trueop1)
{
  rtx tem, reversed, opleft, opright;
  HOST_WIDE_INT val;
  unsigned int width = GET_MODE_BITSIZE (mode);

  /* Even if we can't compute a constant result,
     there are some cases worth simplifying.  */

  switch (code)
    {
    case PLUS:
      /* Maybe simplify x + 0 to x.  The two expressions are equivalent
	 when x is NaN, infinite, or finite and nonzero.  They aren't
	 when x is -0 and the rounding mode is not towards -infinity,
	 since (-0) + 0 is then 0.  */
      if (!HONOR_SIGNED_ZEROS (mode) && trueop1 == CONST0_RTX (mode))
	return op0;

      /* ((-a) + b) -> (b - a) and similarly for (a + (-b)).  These
	 transformations are safe even for IEEE.  */
      if (GET_CODE (op0) == NEG)
	return simplify_gen_binary (MINUS, mode, op1, XEXP (op0, 0));
      else if (GET_CODE (op1) == NEG)
	return simplify_gen_binary (MINUS, mode, op0, XEXP (op1, 0));

      /* (~a) + 1 -> -a */
      if (INTEGRAL_MODE_P (mode)
	  && GET_CODE (op0) == NOT
	  && trueop1 == const1_rtx)
	return simplify_gen_unary (NEG, mode, XEXP (op0, 0), mode);

      /* Handle both-operands-constant cases.  We can only add
	 CONST_INTs to constants since the sum of relocatable symbols
	 can't be handled by most assemblers.  Don't add CONST_INT
	 to CONST_INT since overflow won't be computed properly if wider
	 than HOST_BITS_PER_WIDE_INT.  */

      if (CONSTANT_P (op0) && GET_MODE (op0) != VOIDmode
	  && GET_CODE (op1) == CONST_INT)
	return plus_constant (op0, INTVAL (op1));
      else if (CONSTANT_P (op1) && GET_MODE (op1) != VOIDmode
	       && GET_CODE (op0) == CONST_INT)
	return plus_constant (op1, INTVAL (op0));

      /* See if this is something like X * C - X or vice versa or
	 if the multiplication is written as a shift.  If so, we can
	 distribute and make a new multiply, shift, or maybe just
	 have X (if C is 2 in the example above).  But don't make
	 something more expensive than we had before.  */

      if (SCALAR_INT_MODE_P (mode))
	{
	  HOST_WIDE_INT coeff0h = 0, coeff1h = 0;
	  unsigned HOST_WIDE_INT coeff0l = 1, coeff1l = 1;
	  rtx lhs = op0, rhs = op1;

	  if (GET_CODE (lhs) == NEG)
	    {
	      coeff0l = -1;
	      coeff0h = -1;
	      lhs = XEXP (lhs, 0);
	    }
	  else if (GET_CODE (lhs) == MULT
		   && GET_CODE (XEXP (lhs, 1)) == CONST_INT)
	    {
	      coeff0l = INTVAL (XEXP (lhs, 1));
	      coeff0h = INTVAL (XEXP (lhs, 1)) < 0 ? -1 : 0;
	      lhs = XEXP (lhs, 0);
	    }
	  else if (GET_CODE (lhs) == ASHIFT
		   && GET_CODE (XEXP (lhs, 1)) == CONST_INT
		   && INTVAL (XEXP (lhs, 1)) >= 0
		   && INTVAL (XEXP (lhs, 1)) < HOST_BITS_PER_WIDE_INT)
	    {
	      coeff0l = ((HOST_WIDE_INT) 1) << INTVAL (XEXP (lhs, 1));
	      coeff0h = 0;
	      lhs = XEXP (lhs, 0);
	    }

	  if (GET_CODE (rhs) == NEG)
	    {
	      coeff1l = -1;
	      coeff1h = -1;
	      rhs = XEXP (rhs, 0);
	    }
	  else if (GET_CODE (rhs) == MULT
		   && GET_CODE (XEXP (rhs, 1)) == CONST_INT)
	    {
	      coeff1l = INTVAL (XEXP (rhs, 1));
	      coeff1h = INTVAL (XEXP (rhs, 1)) < 0 ? -1 : 0;
	      rhs = XEXP (rhs, 0);
	    }
	  else if (GET_CODE (rhs) == ASHIFT
		   && GET_CODE (XEXP (rhs, 1)) == CONST_INT
		   && INTVAL (XEXP (rhs, 1)) >= 0
		   && INTVAL (XEXP (rhs, 1)) < HOST_BITS_PER_WIDE_INT)
	    {
	      coeff1l = ((HOST_WIDE_INT) 1) << INTVAL (XEXP (rhs, 1));
	      coeff1h = 0;
	      rhs = XEXP (rhs, 0);
	    }

	  if (rtx_equal_p (lhs, rhs))
	    {
	      rtx orig = gen_rtx_PLUS (mode, op0, op1);
	      rtx coeff;
	      unsigned HOST_WIDE_INT l;
	      HOST_WIDE_INT h;

	      add_double (coeff0l, coeff0h, coeff1l, coeff1h, &l, &h);
	      coeff = immed_double_const (l, h, mode);

	      tem = simplify_gen_binary (MULT, mode, lhs, coeff);
	      return rtx_cost (tem, SET) <= rtx_cost (orig, SET)
		? tem : 0;
	    }
	}

      /* (plus (xor X C1) C2) is (xor X (C1^C2)) if C2 is signbit.  */
      if ((GET_CODE (op1) == CONST_INT
	   || GET_CODE (op1) == CONST_DOUBLE)
	  && GET_CODE (op0) == XOR
	  && (GET_CODE (XEXP (op0, 1)) == CONST_INT
	      || GET_CODE (XEXP (op0, 1)) == CONST_DOUBLE)
	  && mode_signbit_p (mode, op1))
	return simplify_gen_binary (XOR, mode, XEXP (op0, 0),
				    simplify_gen_binary (XOR, mode, op1,
							 XEXP (op0, 1)));

      /* Canonicalize (plus (mult (neg B) C) A) to (minus A (mult B C)).  */
      if (GET_CODE (op0) == MULT
	  && GET_CODE (XEXP (op0, 0)) == NEG)
	{
	  rtx in1, in2;

	  in1 = XEXP (XEXP (op0, 0), 0);
	  in2 = XEXP (op0, 1);
	  return simplify_gen_binary (MINUS, mode, op1,
				      simplify_gen_binary (MULT, mode,
							   in1, in2));
	}

      /* (plus (comparison A B) C) can become (neg (rev-comp A B)) if
	 C is 1 and STORE_FLAG_VALUE is -1 or if C is -1 and STORE_FLAG_VALUE
	 is 1.  */
      if (COMPARISON_P (op0)
	  && ((STORE_FLAG_VALUE == -1 && trueop1 == const1_rtx)
	      || (STORE_FLAG_VALUE == 1 && trueop1 == constm1_rtx))
	  && (reversed = reversed_comparison (op0, mode)))
	return
	  simplify_gen_unary (NEG, mode, reversed, mode);

      /* If one of the operands is a PLUS or a MINUS, see if we can
	 simplify this by the associative law.
	 Don't use the associative law for floating point.
	 The inaccuracy makes it nonassociative,
	 and subtle programs can break if operations are associated.  */

      if (INTEGRAL_MODE_P (mode)
	  && (plus_minus_operand_p (op0)
	      || plus_minus_operand_p (op1))
	  && (tem = simplify_plus_minus (code, mode, op0, op1)) != 0)
	return tem;

      /* Reassociate floating point addition only when the user
	 specifies unsafe math optimizations.  */
      if (FLOAT_MODE_P (mode)
	  && flag_unsafe_math_optimizations)
	{
	  tem = simplify_associative_operation (code, mode, op0, op1);
	  if (tem)
	    return tem;
	}
      break;

    case COMPARE:
#ifdef HAVE_cc0
      /* Convert (compare FOO (const_int 0)) to FOO unless we aren't
	 using cc0, in which case we want to leave it as a COMPARE
	 so we can distinguish it from a register-register-copy.

	 In IEEE floating point, x-0 is not the same as x.  */

      if ((TARGET_FLOAT_FORMAT != IEEE_FLOAT_FORMAT
	   || ! FLOAT_MODE_P (mode) || flag_unsafe_math_optimizations)
	  && trueop1 == CONST0_RTX (mode))
	return op0;
#endif

      /* Convert (compare (gt (flags) 0) (lt (flags) 0)) to (flags).  */
      if (((GET_CODE (op0) == GT && GET_CODE (op1) == LT)
	   || (GET_CODE (op0) == GTU && GET_CODE (op1) == LTU))
	  && XEXP (op0, 1) == const0_rtx && XEXP (op1, 1) == const0_rtx)
	{
	  rtx xop00 = XEXP (op0, 0);
	  rtx xop10 = XEXP (op1, 0);

#ifdef HAVE_cc0
	  if (GET_CODE (xop00) == CC0 && GET_CODE (xop10) == CC0)
#else
	    if (REG_P (xop00) && REG_P (xop10)
		&& GET_MODE (xop00) == GET_MODE (xop10)
		&& REGNO (xop00) == REGNO (xop10)
		&& GET_MODE_CLASS (GET_MODE (xop00)) == MODE_CC
		&& GET_MODE_CLASS (GET_MODE (xop10)) == MODE_CC)
#endif
	      return xop00;
	}
      break;

    case MINUS:
      /* We can't assume x-x is 0 even with non-IEEE floating point,
	 but since it is zero except in very strange circumstances, we
	 will treat it as zero with -funsafe-math-optimizations.  */
      if (rtx_equal_p (trueop0, trueop1)
	  && ! side_effects_p (op0)
	  && (! FLOAT_MODE_P (mode) || flag_unsafe_math_optimizations))
	return CONST0_RTX (mode);

      /* Change subtraction from zero into negation.  (0 - x) is the
	 same as -x when x is NaN, infinite, or finite and nonzero.
	 But if the mode has signed zeros, and does not round towards
	 -infinity, then 0 - 0 is 0, not -0.  */
      if (!HONOR_SIGNED_ZEROS (mode) && trueop0 == CONST0_RTX (mode))
	return simplify_gen_unary (NEG, mode, op1, mode);

      /* (-1 - a) is ~a.  */
      if (trueop0 == constm1_rtx)
	return simplify_gen_unary (NOT, mode, op1, mode);

      /* Subtracting 0 has no effect unless the mode has signed zeros
	 and supports rounding towards -infinity.  In such a case,
	 0 - 0 is -0.  */
      if (!(HONOR_SIGNED_ZEROS (mode)
	    && HONOR_SIGN_DEPENDENT_ROUNDING (mode))
	  && trueop1 == CONST0_RTX (mode))
	return op0;

      /* See if this is something like X * C - X or vice versa or
	 if the multiplication is written as a shift.  If so, we can
	 distribute and make a new multiply, shift, or maybe just
	 have X (if C is 2 in the example above).  But don't make
	 something more expensive than we had before.  */

      if (SCALAR_INT_MODE_P (mode))
	{
	  HOST_WIDE_INT coeff0h = 0, negcoeff1h = -1;
	  unsigned HOST_WIDE_INT coeff0l = 1, negcoeff1l = -1;
	  rtx lhs = op0, rhs = op1;

	  if (GET_CODE (lhs) == NEG)
	    {
	      coeff0l = -1;
	      coeff0h = -1;
	      lhs = XEXP (lhs, 0);
	    }
	  else if (GET_CODE (lhs) == MULT
		   && GET_CODE (XEXP (lhs, 1)) == CONST_INT)
	    {
	      coeff0l = INTVAL (XEXP (lhs, 1));
	      coeff0h = INTVAL (XEXP (lhs, 1)) < 0 ? -1 : 0;
	      lhs = XEXP (lhs, 0);
	    }
	  else if (GET_CODE (lhs) == ASHIFT
		   && GET_CODE (XEXP (lhs, 1)) == CONST_INT
		   && INTVAL (XEXP (lhs, 1)) >= 0
		   && INTVAL (XEXP (lhs, 1)) < HOST_BITS_PER_WIDE_INT)
	    {
	      coeff0l = ((HOST_WIDE_INT) 1) << INTVAL (XEXP (lhs, 1));
	      coeff0h = 0;
	      lhs = XEXP (lhs, 0);
	    }

	  if (GET_CODE (rhs) == NEG)
	    {
	      negcoeff1l = 1;
	      negcoeff1h = 0;
	      rhs = XEXP (rhs, 0);
	    }
	  else if (GET_CODE (rhs) == MULT
		   && GET_CODE (XEXP (rhs, 1)) == CONST_INT)
	    {
	      negcoeff1l = -INTVAL (XEXP (rhs, 1));
	      negcoeff1h = INTVAL (XEXP (rhs, 1)) <= 0 ? 0 : -1;
	      rhs = XEXP (rhs, 0);
	    }
	  else if (GET_CODE (rhs) == ASHIFT
		   && GET_CODE (XEXP (rhs, 1)) == CONST_INT
		   && INTVAL (XEXP (rhs, 1)) >= 0
		   && INTVAL (XEXP (rhs, 1)) < HOST_BITS_PER_WIDE_INT)
	    {
	      negcoeff1l = -(((HOST_WIDE_INT) 1) << INTVAL (XEXP (rhs, 1)));
	      negcoeff1h = -1;
	      rhs = XEXP (rhs, 0);
	    }

	  if (rtx_equal_p (lhs, rhs))
	    {
	      rtx orig = gen_rtx_MINUS (mode, op0, op1);
	      rtx coeff;
	      unsigned HOST_WIDE_INT l;
	      HOST_WIDE_INT h;

	      add_double (coeff0l, coeff0h, negcoeff1l, negcoeff1h, &l, &h);
	      coeff = immed_double_const (l, h, mode);

	      tem = simplify_gen_binary (MULT, mode, lhs, coeff);
	      return rtx_cost (tem, SET) <= rtx_cost (orig, SET)
		? tem : 0;
	    }
	}

      /* (a - (-b)) -> (a + b).  True even for IEEE.  */
      if (GET_CODE (op1) == NEG)
	return simplify_gen_binary (PLUS, mode, op0, XEXP (op1, 0));

      /* (-x - c) may be simplified as (-c - x).  */
      if (GET_CODE (op0) == NEG
	  && (GET_CODE (op1) == CONST_INT
	      || GET_CODE (op1) == CONST_DOUBLE))
	{
	  tem = simplify_unary_operation (NEG, mode, op1, mode);
	  if (tem)
	    return simplify_gen_binary (MINUS, mode, tem, XEXP (op0, 0));
	}

      /* Don't let a relocatable value get a negative coeff.  */
      if (GET_CODE (op1) == CONST_INT && GET_MODE (op0) != VOIDmode)
	return simplify_gen_binary (PLUS, mode,
				    op0,
				    neg_const_int (mode, op1));

      /* (x - (x & y)) -> (x & ~y) */
      if (GET_CODE (op1) == AND)
	{
	  if (rtx_equal_p (op0, XEXP (op1, 0)))
	    {
	      tem = simplify_gen_unary (NOT, mode, XEXP (op1, 1),
					GET_MODE (XEXP (op1, 1)));
	      return simplify_gen_binary (AND, mode, op0, tem);
	    }
	  if (rtx_equal_p (op0, XEXP (op1, 1)))
	    {
	      tem = simplify_gen_unary (NOT, mode, XEXP (op1, 0),
					GET_MODE (XEXP (op1, 0)));
	      return simplify_gen_binary (AND, mode, op0, tem);
	    }
	}

      /* If STORE_FLAG_VALUE is 1, (minus 1 (comparison foo bar)) can be done
	 by reversing the comparison code if valid.  */
      if (STORE_FLAG_VALUE == 1
	  && trueop0 == const1_rtx
	  && COMPARISON_P (op1)
	  && (reversed = reversed_comparison (op1, mode)))
	return reversed;

      /* Canonicalize (minus A (mult (neg B) C)) to (plus (mult B C) A).  */
      if (GET_CODE (op1) == MULT
	  && GET_CODE (XEXP (op1, 0)) == NEG)
	{
	  rtx in1, in2;

	  in1 = XEXP (XEXP (op1, 0), 0);
	  in2 = XEXP (op1, 1);
	  return simplify_gen_binary (PLUS, mode,
				      simplify_gen_binary (MULT, mode,
							   in1, in2),
				      op0);
	}

      /* Canonicalize (minus (neg A) (mult B C)) to
	 (minus (mult (neg B) C) A).  */
      if (GET_CODE (op1) == MULT
	  && GET_CODE (op0) == NEG)
	{
	  rtx in1, in2;

	  in1 = simplify_gen_unary (NEG, mode, XEXP (op1, 0), mode);
	  in2 = XEXP (op1, 1);
	  return simplify_gen_binary (MINUS, mode,
				      simplify_gen_binary (MULT, mode,
							   in1, in2),
				      XEXP (op0, 0));
	}

      /* If one of the operands is a PLUS or a MINUS, see if we can
	 simplify this by the associative law.  This will, for example,
         canonicalize (minus A (plus B C)) to (minus (minus A B) C).
	 Don't use the associative law for floating point.
	 The inaccuracy makes it nonassociative,
	 and subtle programs can break if operations are associated.  */

      if (INTEGRAL_MODE_P (mode)
	  && (plus_minus_operand_p (op0)
	      || plus_minus_operand_p (op1))
	  && (tem = simplify_plus_minus (code, mode, op0, op1)) != 0)
	return tem;
      break;

    case MULT:
      if (trueop1 == constm1_rtx)
	return simplify_gen_unary (NEG, mode, op0, mode);

      /* Maybe simplify x * 0 to 0.  The reduction is not valid if
	 x is NaN, since x * 0 is then also NaN.  Nor is it valid
	 when the mode has signed zeros, since multiplying a negative
	 number by 0 will give -0, not 0.  */
      if (!HONOR_NANS (mode)
	  && !HONOR_SIGNED_ZEROS (mode)
	  && trueop1 == CONST0_RTX (mode)
	  && ! side_effects_p (op0))
	return op1;

      /* In IEEE floating point, x*1 is not equivalent to x for
	 signalling NaNs.  */
      if (!HONOR_SNANS (mode)
	  && trueop1 == CONST1_RTX (mode))
	return op0;

      /* Convert multiply by constant power of two into shift unless
	 we are still generating RTL.  This test is a kludge.  */
      if (GET_CODE (trueop1) == CONST_INT
	  && (val = exact_log2 (INTVAL (trueop1))) >= 0
	  /* If the mode is larger than the host word size, and the
	     uppermost bit is set, then this isn't a power of two due
	     to implicit sign extension.  */
	  && (width <= HOST_BITS_PER_WIDE_INT
	      || val != HOST_BITS_PER_WIDE_INT - 1))
	return simplify_gen_binary (ASHIFT, mode, op0, GEN_INT (val));

      /* Likewise for multipliers wider than a word.  */
      if (GET_CODE (trueop1) == CONST_DOUBLE
	  && (GET_MODE (trueop1) == VOIDmode
	      || GET_MODE_CLASS (GET_MODE (trueop1)) == MODE_INT)
	  && GET_MODE (op0) == mode
	  && CONST_DOUBLE_LOW (trueop1) == 0
	  && (val = exact_log2 (CONST_DOUBLE_HIGH (trueop1))) >= 0)
	return simplify_gen_binary (ASHIFT, mode, op0,
				    GEN_INT (val + HOST_BITS_PER_WIDE_INT));

      /* x*2 is x+x and x*(-1) is -x */
      if (GET_CODE (trueop1) == CONST_DOUBLE
	  && SCALAR_FLOAT_MODE_P (GET_MODE (trueop1))
	  && GET_MODE (op0) == mode)
	{
	  REAL_VALUE_TYPE d;
	  REAL_VALUE_FROM_CONST_DOUBLE (d, trueop1);

	  if (REAL_VALUES_EQUAL (d, dconst2))
	    return simplify_gen_binary (PLUS, mode, op0, copy_rtx (op0));

	  if (!HONOR_SNANS (mode)
	      && REAL_VALUES_EQUAL (d, dconstm1))
	    return simplify_gen_unary (NEG, mode, op0, mode);
	}

      /* Optimize -x * -x as x * x.  */
      if (FLOAT_MODE_P (mode)
	  && GET_CODE (op0) == NEG
	  && GET_CODE (op1) == NEG
	  && rtx_equal_p (XEXP (op0, 0), XEXP (op1, 0))
	  && !side_effects_p (XEXP (op0, 0)))
	return simplify_gen_binary (MULT, mode, XEXP (op0, 0), XEXP (op1, 0));

      /* Likewise, optimize abs(x) * abs(x) as x * x.  */
      if (SCALAR_FLOAT_MODE_P (mode)
	  && GET_CODE (op0) == ABS
	  && GET_CODE (op1) == ABS
	  && rtx_equal_p (XEXP (op0, 0), XEXP (op1, 0))
	  && !side_effects_p (XEXP (op0, 0)))
	return simplify_gen_binary (MULT, mode, XEXP (op0, 0), XEXP (op1, 0));

      /* Reassociate multiplication, but for floating point MULTs
	 only when the user specifies unsafe math optimizations.  */
      if (! FLOAT_MODE_P (mode)
	  || flag_unsafe_math_optimizations)
	{
	  tem = simplify_associative_operation (code, mode, op0, op1);
	  if (tem)
	    return tem;
	}
      break;

    case IOR:
      if (trueop1 == const0_rtx)
	return op0;
      if (GET_CODE (trueop1) == CONST_INT
	  && ((INTVAL (trueop1) & GET_MODE_MASK (mode))
	      == GET_MODE_MASK (mode)))
	return op1;
      if (rtx_equal_p (trueop0, trueop1) && ! side_effects_p (op0))
	return op0;
      /* A | (~A) -> -1 */
      if (((GET_CODE (op0) == NOT && rtx_equal_p (XEXP (op0, 0), op1))
	   || (GET_CODE (op1) == NOT && rtx_equal_p (XEXP (op1, 0), op0)))
	  && ! side_effects_p (op0)
	  && SCALAR_INT_MODE_P (mode))
	return constm1_rtx;

      /* (ior A C) is C if all bits of A that might be nonzero are on in C.  */
      if (GET_CODE (op1) == CONST_INT
	  && GET_MODE_BITSIZE (mode) <= HOST_BITS_PER_WIDE_INT
	  && (nonzero_bits (op0, mode) & ~INTVAL (op1)) == 0)
	return op1;
 
      /* Convert (A & B) | A to A.  */
      if (GET_CODE (op0) == AND
	  && (rtx_equal_p (XEXP (op0, 0), op1)
	      || rtx_equal_p (XEXP (op0, 1), op1))
	  && ! side_effects_p (XEXP (op0, 0))
	  && ! side_effects_p (XEXP (op0, 1)))
	return op1;

      /* Convert (ior (ashift A CX) (lshiftrt A CY)) where CX+CY equals the
         mode size to (rotate A CX).  */

      if (GET_CODE (op1) == ASHIFT
          || GET_CODE (op1) == SUBREG)
        {
	  opleft = op1;
	  opright = op0;
	}
      else
        {
	  opright = op1;
	  opleft = op0;
	}

      if (GET_CODE (opleft) == ASHIFT && GET_CODE (opright) == LSHIFTRT
          && rtx_equal_p (XEXP (opleft, 0), XEXP (opright, 0))
          && GET_CODE (XEXP (opleft, 1)) == CONST_INT
          && GET_CODE (XEXP (opright, 1)) == CONST_INT
          && (INTVAL (XEXP (opleft, 1)) + INTVAL (XEXP (opright, 1))
              == GET_MODE_BITSIZE (mode)))
        return gen_rtx_ROTATE (mode, XEXP (opright, 0), XEXP (opleft, 1));

      /* Same, but for ashift that has been "simplified" to a wider mode
        by simplify_shift_const.  */

      if (GET_CODE (opleft) == SUBREG
          && GET_CODE (SUBREG_REG (opleft)) == ASHIFT
          && GET_CODE (opright) == LSHIFTRT
          && GET_CODE (XEXP (opright, 0)) == SUBREG
          && GET_MODE (opleft) == GET_MODE (XEXP (opright, 0))
          && SUBREG_BYTE (opleft) == SUBREG_BYTE (XEXP (opright, 0))
          && (GET_MODE_SIZE (GET_MODE (opleft))
              < GET_MODE_SIZE (GET_MODE (SUBREG_REG (opleft))))
          && rtx_equal_p (XEXP (SUBREG_REG (opleft), 0),
                          SUBREG_REG (XEXP (opright, 0)))
          && GET_CODE (XEXP (SUBREG_REG (opleft), 1)) == CONST_INT
          && GET_CODE (XEXP (opright, 1)) == CONST_INT
          && (INTVAL (XEXP (SUBREG_REG (opleft), 1)) + INTVAL (XEXP (opright, 1))
              == GET_MODE_BITSIZE (mode)))
        return gen_rtx_ROTATE (mode, XEXP (opright, 0),
                               XEXP (SUBREG_REG (opleft), 1));

      /* If we have (ior (and (X C1) C2)), simplify this by making
	 C1 as small as possible if C1 actually changes.  */
      if (GET_CODE (op1) == CONST_INT
	  && (GET_MODE_BITSIZE (mode) <= HOST_BITS_PER_WIDE_INT
	      || INTVAL (op1) > 0)
	  && GET_CODE (op0) == AND
	  && GET_CODE (XEXP (op0, 1)) == CONST_INT
	  && GET_CODE (op1) == CONST_INT
	  && (INTVAL (XEXP (op0, 1)) & INTVAL (op1)) != 0)
	return simplify_gen_binary (IOR, mode,
				    simplify_gen_binary
					  (AND, mode, XEXP (op0, 0),
					   GEN_INT (INTVAL (XEXP (op0, 1))
						    & ~INTVAL (op1))),
				    op1);

      /* If OP0 is (ashiftrt (plus ...) C), it might actually be
         a (sign_extend (plus ...)).  Then check if OP1 is a CONST_INT and
	 the PLUS does not affect any of the bits in OP1: then we can do
	 the IOR as a PLUS and we can associate.  This is valid if OP1
         can be safely shifted left C bits.  */
      if (GET_CODE (trueop1) == CONST_INT && GET_CODE (op0) == ASHIFTRT
          && GET_CODE (XEXP (op0, 0)) == PLUS
          && GET_CODE (XEXP (XEXP (op0, 0), 1)) == CONST_INT
          && GET_CODE (XEXP (op0, 1)) == CONST_INT
          && INTVAL (XEXP (op0, 1)) < HOST_BITS_PER_WIDE_INT)
        {
          int count = INTVAL (XEXP (op0, 1));
          HOST_WIDE_INT mask = INTVAL (trueop1) << count;

          if (mask >> count == INTVAL (trueop1)
              && (mask & nonzero_bits (XEXP (op0, 0), mode)) == 0)
	    return simplify_gen_binary (ASHIFTRT, mode,
					plus_constant (XEXP (op0, 0), mask),
					XEXP (op0, 1));
        }

      tem = simplify_associative_operation (code, mode, op0, op1);
      if (tem)
	return tem;
      break;

    case XOR:
      if (trueop1 == const0_rtx)
	return op0;
      if (GET_CODE (trueop1) == CONST_INT
	  && ((INTVAL (trueop1) & GET_MODE_MASK (mode))
	      == GET_MODE_MASK (mode)))
	return simplify_gen_unary (NOT, mode, op0, mode);
      if (rtx_equal_p (trueop0, trueop1)
	  && ! side_effects_p (op0)
	  && GET_MODE_CLASS (mode) != MODE_CC)
	 return CONST0_RTX (mode);

      /* Canonicalize XOR of the most significant bit to PLUS.  */
      if ((GET_CODE (op1) == CONST_INT
	   || GET_CODE (op1) == CONST_DOUBLE)
	  && mode_signbit_p (mode, op1))
	return simplify_gen_binary (PLUS, mode, op0, op1);
      /* (xor (plus X C1) C2) is (xor X (C1^C2)) if C1 is signbit.  */
      if ((GET_CODE (op1) == CONST_INT
	   || GET_CODE (op1) == CONST_DOUBLE)
	  && GET_CODE (op0) == PLUS
	  && (GET_CODE (XEXP (op0, 1)) == CONST_INT
	      || GET_CODE (XEXP (op0, 1)) == CONST_DOUBLE)
	  && mode_signbit_p (mode, XEXP (op0, 1)))
	return simplify_gen_binary (XOR, mode, XEXP (op0, 0),
				    simplify_gen_binary (XOR, mode, op1,
							 XEXP (op0, 1)));

      /* If we are XORing two things that have no bits in common,
	 convert them into an IOR.  This helps to detect rotation encoded
	 using those methods and possibly other simplifications.  */

      if (GET_MODE_BITSIZE (mode) <= HOST_BITS_PER_WIDE_INT
	  && (nonzero_bits (op0, mode)
	      & nonzero_bits (op1, mode)) == 0)
	return (simplify_gen_binary (IOR, mode, op0, op1));

      /* Convert (XOR (NOT x) (NOT y)) to (XOR x y).
	 Also convert (XOR (NOT x) y) to (NOT (XOR x y)), similarly for
	 (NOT y).  */
      {
	int num_negated = 0;

	if (GET_CODE (op0) == NOT)
	  num_negated++, op0 = XEXP (op0, 0);
	if (GET_CODE (op1) == NOT)
	  num_negated++, op1 = XEXP (op1, 0);

	if (num_negated == 2)
	  return simplify_gen_binary (XOR, mode, op0, op1);
	else if (num_negated == 1)
	  return simplify_gen_unary (NOT, mode,
				     simplify_gen_binary (XOR, mode, op0, op1),
				     mode);
      }

      /* Convert (xor (and A B) B) to (and (not A) B).  The latter may
	 correspond to a machine insn or result in further simplifications
	 if B is a constant.  */

      if (GET_CODE (op0) == AND
	  && rtx_equal_p (XEXP (op0, 1), op1)
	  && ! side_effects_p (op1))
	return simplify_gen_binary (AND, mode,
				    simplify_gen_unary (NOT, mode,
							XEXP (op0, 0), mode),
				    op1);

      else if (GET_CODE (op0) == AND
	       && rtx_equal_p (XEXP (op0, 0), op1)
	       && ! side_effects_p (op1))
	return simplify_gen_binary (AND, mode,
				    simplify_gen_unary (NOT, mode,
							XEXP (op0, 1), mode),
				    op1);

      /* (xor (comparison foo bar) (const_int 1)) can become the reversed
	 comparison if STORE_FLAG_VALUE is 1.  */
      if (STORE_FLAG_VALUE == 1
	  && trueop1 == const1_rtx
	  && COMPARISON_P (op0)
	  && (reversed = reversed_comparison (op0, mode)))
	return reversed;

      /* (lshiftrt foo C) where C is the number of bits in FOO minus 1
	 is (lt foo (const_int 0)), so we can perform the above
	 simplification if STORE_FLAG_VALUE is 1.  */

      if (STORE_FLAG_VALUE == 1
	  && trueop1 == const1_rtx
	  && GET_CODE (op0) == LSHIFTRT
	  && GET_CODE (XEXP (op0, 1)) == CONST_INT
	  && INTVAL (XEXP (op0, 1)) == GET_MODE_BITSIZE (mode) - 1)
	return gen_rtx_GE (mode, XEXP (op0, 0), const0_rtx);

      /* (xor (comparison foo bar) (const_int sign-bit))
	 when STORE_FLAG_VALUE is the sign bit.  */
      if (GET_MODE_BITSIZE (mode) <= HOST_BITS_PER_WIDE_INT
	  && ((STORE_FLAG_VALUE & GET_MODE_MASK (mode))
	      == (unsigned HOST_WIDE_INT) 1 << (GET_MODE_BITSIZE (mode) - 1))
	  && trueop1 == const_true_rtx
	  && COMPARISON_P (op0)
	  && (reversed = reversed_comparison (op0, mode)))
	return reversed;

      break;
      
      tem = simplify_associative_operation (code, mode, op0, op1);
      if (tem)
	return tem;
      break;

    case AND:
      if (trueop1 == CONST0_RTX (mode) && ! side_effects_p (op0))
	return trueop1;
      /* If we are turning off bits already known off in OP0, we need
	 not do an AND.  */
      if (GET_CODE (trueop1) == CONST_INT
	  && GET_MODE_BITSIZE (mode) <= HOST_BITS_PER_WIDE_INT
	  && (nonzero_bits (trueop0, mode) & ~INTVAL (trueop1)) == 0)
	return op0;
      if (rtx_equal_p (trueop0, trueop1) && ! side_effects_p (op0)
	  && GET_MODE_CLASS (mode) != MODE_CC)
	return op0;
      /* A & (~A) -> 0 */
      if (((GET_CODE (op0) == NOT && rtx_equal_p (XEXP (op0, 0), op1))
	   || (GET_CODE (op1) == NOT && rtx_equal_p (XEXP (op1, 0), op0)))
	  && ! side_effects_p (op0)
	  && GET_MODE_CLASS (mode) != MODE_CC)
	return CONST0_RTX (mode);

      /* Transform (and (extend X) C) into (zero_extend (and X C)) if
	 there are no nonzero bits of C outside of X's mode.  */
      if ((GET_CODE (op0) == SIGN_EXTEND
	   || GET_CODE (op0) == ZERO_EXTEND)
	  && GET_CODE (trueop1) == CONST_INT
	  && GET_MODE_BITSIZE (mode) <= HOST_BITS_PER_WIDE_INT
	  && (~GET_MODE_MASK (GET_MODE (XEXP (op0, 0)))
	      & INTVAL (trueop1)) == 0)
	{
	  enum machine_mode imode = GET_MODE (XEXP (op0, 0));
	  tem = simplify_gen_binary (AND, imode, XEXP (op0, 0),
				     gen_int_mode (INTVAL (trueop1),
						   imode));
	  return simplify_gen_unary (ZERO_EXTEND, mode, tem, imode);
	}

      /* Convert (A ^ B) & A to A & (~B) since the latter is often a single
	 insn (and may simplify more).  */
      if (GET_CODE (op0) == XOR
	  && rtx_equal_p (XEXP (op0, 0), op1)
	  && ! side_effects_p (op1))
	return simplify_gen_binary (AND, mode,
				    simplify_gen_unary (NOT, mode,
							XEXP (op0, 1), mode),
				    op1);

      if (GET_CODE (op0) == XOR
	  && rtx_equal_p (XEXP (op0, 1), op1)
	  && ! side_effects_p (op1))
	return simplify_gen_binary (AND, mode,
				    simplify_gen_unary (NOT, mode,
							XEXP (op0, 0), mode),
				    op1);

      /* Similarly for (~(A ^ B)) & A.  */
      if (GET_CODE (op0) == NOT
	  && GET_CODE (XEXP (op0, 0)) == XOR
	  && rtx_equal_p (XEXP (XEXP (op0, 0), 0), op1)
	  && ! side_effects_p (op1))
	return simplify_gen_binary (AND, mode, XEXP (XEXP (op0, 0), 1), op1);

      if (GET_CODE (op0) == NOT
	  && GET_CODE (XEXP (op0, 0)) == XOR
	  && rtx_equal_p (XEXP (XEXP (op0, 0), 1), op1)
	  && ! side_effects_p (op1))
	return simplify_gen_binary (AND, mode, XEXP (XEXP (op0, 0), 0), op1);

      /* Convert (A | B) & A to A.  */
      if (GET_CODE (op0) == IOR
	  && (rtx_equal_p (XEXP (op0, 0), op1)
	      || rtx_equal_p (XEXP (op0, 1), op1))
	  && ! side_effects_p (XEXP (op0, 0))
	  && ! side_effects_p (XEXP (op0, 1)))
	return op1;

      /* For constants M and N, if M == (1LL << cst) - 1 && (N & M) == M,
	 ((A & N) + B) & M -> (A + B) & M
	 Similarly if (N & M) == 0,
	 ((A | N) + B) & M -> (A + B) & M
	 and for - instead of + and/or ^ instead of |.  */
      if (GET_CODE (trueop1) == CONST_INT
	  && GET_MODE_BITSIZE (mode) <= HOST_BITS_PER_WIDE_INT
	  && ~INTVAL (trueop1)
	  && (INTVAL (trueop1) & (INTVAL (trueop1) + 1)) == 0
	  && (GET_CODE (op0) == PLUS || GET_CODE (op0) == MINUS))
	{
	  rtx pmop[2];
	  int which;

	  pmop[0] = XEXP (op0, 0);
	  pmop[1] = XEXP (op0, 1);

	  for (which = 0; which < 2; which++)
	    {
	      tem = pmop[which];
	      switch (GET_CODE (tem))
		{
		case AND:
		  if (GET_CODE (XEXP (tem, 1)) == CONST_INT
		      && (INTVAL (XEXP (tem, 1)) & INTVAL (trueop1))
		      == INTVAL (trueop1))
		    pmop[which] = XEXP (tem, 0);
		  break;
		case IOR:
		case XOR:
		  if (GET_CODE (XEXP (tem, 1)) == CONST_INT
		      && (INTVAL (XEXP (tem, 1)) & INTVAL (trueop1)) == 0)
		    pmop[which] = XEXP (tem, 0);
		  break;
		default:
		  break;
		}
	    }

	  if (pmop[0] != XEXP (op0, 0) || pmop[1] != XEXP (op0, 1))
	    {
	      tem = simplify_gen_binary (GET_CODE (op0), mode,
					 pmop[0], pmop[1]);
	      return simplify_gen_binary (code, mode, tem, op1);
	    }
	}
      tem = simplify_associative_operation (code, mode, op0, op1);
      if (tem)
	return tem;
      break;

    case UDIV:
      /* 0/x is 0 (or x&0 if x has side-effects).  */
      if (trueop0 == CONST0_RTX (mode))
	{
	  if (side_effects_p (op1))
	    return simplify_gen_binary (AND, mode, op1, trueop0);
	  return trueop0;
	}
      /* x/1 is x.  */
      if (trueop1 == CONST1_RTX (mode))
	return rtl_hooks.gen_lowpart_no_emit (mode, op0);
      /* Convert divide by power of two into shift.  */
      if (GET_CODE (trueop1) == CONST_INT
	  && (val = exact_log2 (INTVAL (trueop1))) > 0)
	return simplify_gen_binary (LSHIFTRT, mode, op0, GEN_INT (val));
      break;

    case DIV:
      /* Handle floating point and integers separately.  */
      if (SCALAR_FLOAT_MODE_P (mode))
	{
	  /* Maybe change 0.0 / x to 0.0.  This transformation isn't
	     safe for modes with NaNs, since 0.0 / 0.0 will then be
	     NaN rather than 0.0.  Nor is it safe for modes with signed
	     zeros, since dividing 0 by a negative number gives -0.0  */
	  if (trueop0 == CONST0_RTX (mode)
	      && !HONOR_NANS (mode)
	      && !HONOR_SIGNED_ZEROS (mode)
	      && ! side_effects_p (op1))
	    return op0;
	  /* x/1.0 is x.  */
	  if (trueop1 == CONST1_RTX (mode)
	      && !HONOR_SNANS (mode))
	    return op0;

	  if (GET_CODE (trueop1) == CONST_DOUBLE
	      && trueop1 != CONST0_RTX (mode))
	    {
	      REAL_VALUE_TYPE d;
	      REAL_VALUE_FROM_CONST_DOUBLE (d, trueop1);

	      /* x/-1.0 is -x.  */
	      if (REAL_VALUES_EQUAL (d, dconstm1)
		  && !HONOR_SNANS (mode))
		return simplify_gen_unary (NEG, mode, op0, mode);

	      /* Change FP division by a constant into multiplication.
		 Only do this with -funsafe-math-optimizations.  */
	      if (flag_unsafe_math_optimizations
		  && !REAL_VALUES_EQUAL (d, dconst0))
		{
		  REAL_ARITHMETIC (d, RDIV_EXPR, dconst1, d);
		  tem = CONST_DOUBLE_FROM_REAL_VALUE (d, mode);
		  return simplify_gen_binary (MULT, mode, op0, tem);
		}
	    }
	}
      else
	{
	  /* 0/x is 0 (or x&0 if x has side-effects).  */
	  if (trueop0 == CONST0_RTX (mode))
	    {
	      if (side_effects_p (op1))
		return simplify_gen_binary (AND, mode, op1, trueop0);
	      return trueop0;
	    }
	  /* x/1 is x.  */
	  if (trueop1 == CONST1_RTX (mode))
	    return rtl_hooks.gen_lowpart_no_emit (mode, op0);
	  /* x/-1 is -x.  */
	  if (trueop1 == constm1_rtx)
	    {
	      rtx x = rtl_hooks.gen_lowpart_no_emit (mode, op0);
	      return simplify_gen_unary (NEG, mode, x, mode);
	    }
	}
      break;

    case UMOD:
      /* 0%x is 0 (or x&0 if x has side-effects).  */
      if (trueop0 == CONST0_RTX (mode))
	{
	  if (side_effects_p (op1))
	    return simplify_gen_binary (AND, mode, op1, trueop0);
	  return trueop0;
	}
      /* x%1 is 0 (of x&0 if x has side-effects).  */
      if (trueop1 == CONST1_RTX (mode))
	{
	  if (side_effects_p (op0))
	    return simplify_gen_binary (AND, mode, op0, CONST0_RTX (mode));
	  return CONST0_RTX (mode);
	}
      /* Implement modulus by power of two as AND.  */
      if (GET_CODE (trueop1) == CONST_INT
	  && exact_log2 (INTVAL (trueop1)) > 0)
	return simplify_gen_binary (AND, mode, op0,
				    GEN_INT (INTVAL (op1) - 1));
      break;

    case MOD:
      /* 0%x is 0 (or x&0 if x has side-effects).  */
      if (trueop0 == CONST0_RTX (mode))
	{
	  if (side_effects_p (op1))
	    return simplify_gen_binary (AND, mode, op1, trueop0);
	  return trueop0;
	}
      /* x%1 and x%-1 is 0 (or x&0 if x has side-effects).  */
      if (trueop1 == CONST1_RTX (mode) || trueop1 == constm1_rtx)
	{
	  if (side_effects_p (op0))
	    return simplify_gen_binary (AND, mode, op0, CONST0_RTX (mode));
	  return CONST0_RTX (mode);
	}
      break;

    case ROTATERT:
    case ROTATE:
    case ASHIFTRT:
      if (trueop1 == CONST0_RTX (mode))
	return op0;
      if (trueop0 == CONST0_RTX (mode) && ! side_effects_p (op1))
	return op0;
      /* Rotating ~0 always results in ~0.  */
      if (GET_CODE (trueop0) == CONST_INT && width <= HOST_BITS_PER_WIDE_INT
	  && (unsigned HOST_WIDE_INT) INTVAL (trueop0) == GET_MODE_MASK (mode)
	  && ! side_effects_p (op1))
	return op0;
      break;

    case ASHIFT:
    case SS_ASHIFT:
      if (trueop1 == CONST0_RTX (mode))
	return op0;
      if (trueop0 == CONST0_RTX (mode) && ! side_effects_p (op1))
	return op0;
      break;

    case LSHIFTRT:
      if (trueop1 == CONST0_RTX (mode))
	return op0;
      if (trueop0 == CONST0_RTX (mode) && ! side_effects_p (op1))
	return op0;
      /* Optimize (lshiftrt (clz X) C) as (eq X 0).  */
      if (GET_CODE (op0) == CLZ
	  && GET_CODE (trueop1) == CONST_INT
	  && STORE_FLAG_VALUE == 1
	  && INTVAL (trueop1) < (HOST_WIDE_INT)width)
	{
	  enum machine_mode imode = GET_MODE (XEXP (op0, 0));
	  unsigned HOST_WIDE_INT zero_val = 0;

	  if (CLZ_DEFINED_VALUE_AT_ZERO (imode, zero_val)
	      && zero_val == GET_MODE_BITSIZE (imode)
	      && INTVAL (trueop1) == exact_log2 (zero_val))
	    return simplify_gen_relational (EQ, mode, imode,
					    XEXP (op0, 0), const0_rtx);
	}
      break;

    case SMIN:
      if (width <= HOST_BITS_PER_WIDE_INT
	  && GET_CODE (trueop1) == CONST_INT
	  && INTVAL (trueop1) == (HOST_WIDE_INT) 1 << (width -1)
	  && ! side_effects_p (op0))
	return op1;
      if (rtx_equal_p (trueop0, trueop1) && ! side_effects_p (op0))
	return op0;
      tem = simplify_associative_operation (code, mode, op0, op1);
      if (tem)
	return tem;
      break;

    case SMAX:
      if (width <= HOST_BITS_PER_WIDE_INT
	  && GET_CODE (trueop1) == CONST_INT
	  && ((unsigned HOST_WIDE_INT) INTVAL (trueop1)
	      == (unsigned HOST_WIDE_INT) GET_MODE_MASK (mode) >> 1)
	  && ! side_effects_p (op0))
	return op1;
      if (rtx_equal_p (trueop0, trueop1) && ! side_effects_p (op0))
	return op0;
      tem = simplify_associative_operation (code, mode, op0, op1);
      if (tem)
	return tem;
      break;

    case UMIN:
      if (trueop1 == CONST0_RTX (mode) && ! side_effects_p (op0))
	return op1;
      if (rtx_equal_p (trueop0, trueop1) && ! side_effects_p (op0))
	return op0;
      tem = simplify_associative_operation (code, mode, op0, op1);
      if (tem)
	return tem;
      break;

    case UMAX:
      if (trueop1 == constm1_rtx && ! side_effects_p (op0))
	return op1;
      if (rtx_equal_p (trueop0, trueop1) && ! side_effects_p (op0))
	return op0;
      tem = simplify_associative_operation (code, mode, op0, op1);
      if (tem)
	return tem;
      break;

    case SS_PLUS:
    case US_PLUS:
    case SS_MINUS:
    case US_MINUS:
      /* ??? There are simplifications that can be done.  */
      return 0;

    case VEC_SELECT:
      if (!VECTOR_MODE_P (mode))
	{
	  gcc_assert (VECTOR_MODE_P (GET_MODE (trueop0)));
	  gcc_assert (mode == GET_MODE_INNER (GET_MODE (trueop0)));
	  gcc_assert (GET_CODE (trueop1) == PARALLEL);
	  gcc_assert (XVECLEN (trueop1, 0) == 1);
	  gcc_assert (GET_CODE (XVECEXP (trueop1, 0, 0)) == CONST_INT);

	  if (GET_CODE (trueop0) == CONST_VECTOR)
	    return CONST_VECTOR_ELT (trueop0, INTVAL (XVECEXP
						      (trueop1, 0, 0)));
	}
      else
	{
	  gcc_assert (VECTOR_MODE_P (GET_MODE (trueop0)));
	  gcc_assert (GET_MODE_INNER (mode)
		      == GET_MODE_INNER (GET_MODE (trueop0)));
	  gcc_assert (GET_CODE (trueop1) == PARALLEL);

	  if (GET_CODE (trueop0) == CONST_VECTOR)
	    {
	      int elt_size = GET_MODE_SIZE (GET_MODE_INNER (mode));
	      unsigned n_elts = (GET_MODE_SIZE (mode) / elt_size);
	      rtvec v = rtvec_alloc (n_elts);
	      unsigned int i;

	      gcc_assert (XVECLEN (trueop1, 0) == (int) n_elts);
	      for (i = 0; i < n_elts; i++)
		{
		  rtx x = XVECEXP (trueop1, 0, i);

		  gcc_assert (GET_CODE (x) == CONST_INT);
		  RTVEC_ELT (v, i) = CONST_VECTOR_ELT (trueop0,
						       INTVAL (x));
		}

	      return gen_rtx_CONST_VECTOR (mode, v);
	    }
	}

      if (XVECLEN (trueop1, 0) == 1
	  && GET_CODE (XVECEXP (trueop1, 0, 0)) == CONST_INT
	  && GET_CODE (trueop0) == VEC_CONCAT)
	{
	  rtx vec = trueop0;
	  int offset = INTVAL (XVECEXP (trueop1, 0, 0)) * GET_MODE_SIZE (mode);

	  /* Try to find the element in the VEC_CONCAT.  */
	  while (GET_MODE (vec) != mode
		 && GET_CODE (vec) == VEC_CONCAT)
	    {
	      HOST_WIDE_INT vec_size = GET_MODE_SIZE (GET_MODE (XEXP (vec, 0)));
	      if (offset < vec_size)
		vec = XEXP (vec, 0);
	      else
		{
		  offset -= vec_size;
		  vec = XEXP (vec, 1);
		}
	      vec = avoid_constant_pool_reference (vec);
	    }

	  if (GET_MODE (vec) == mode)
	    return vec;
	}

      return 0;
    case VEC_CONCAT:
      {
	enum machine_mode op0_mode = (GET_MODE (trueop0) != VOIDmode
				      ? GET_MODE (trueop0)
				      : GET_MODE_INNER (mode));
	enum machine_mode op1_mode = (GET_MODE (trueop1) != VOIDmode
				      ? GET_MODE (trueop1)
				      : GET_MODE_INNER (mode));

	gcc_assert (VECTOR_MODE_P (mode));
	gcc_assert (GET_MODE_SIZE (op0_mode) + GET_MODE_SIZE (op1_mode)
		    == GET_MODE_SIZE (mode));

	if (VECTOR_MODE_P (op0_mode))
	  gcc_assert (GET_MODE_INNER (mode)
		      == GET_MODE_INNER (op0_mode));
	else
	  gcc_assert (GET_MODE_INNER (mode) == op0_mode);

	if (VECTOR_MODE_P (op1_mode))
	  gcc_assert (GET_MODE_INNER (mode)
		      == GET_MODE_INNER (op1_mode));
	else
	  gcc_assert (GET_MODE_INNER (mode) == op1_mode);

	if ((GET_CODE (trueop0) == CONST_VECTOR
	     || GET_CODE (trueop0) == CONST_INT
	     || GET_CODE (trueop0) == CONST_DOUBLE)
	    && (GET_CODE (trueop1) == CONST_VECTOR
		|| GET_CODE (trueop1) == CONST_INT
		|| GET_CODE (trueop1) == CONST_DOUBLE))
	  {
	    int elt_size = GET_MODE_SIZE (GET_MODE_INNER (mode));
	    unsigned n_elts = (GET_MODE_SIZE (mode) / elt_size);
	    rtvec v = rtvec_alloc (n_elts);
	    unsigned int i;
	    unsigned in_n_elts = 1;

	    if (VECTOR_MODE_P (op0_mode))
	      in_n_elts = (GET_MODE_SIZE (op0_mode) / elt_size);
	    for (i = 0; i < n_elts; i++)
	      {
		if (i < in_n_elts)
		  {
		    if (!VECTOR_MODE_P (op0_mode))
		      RTVEC_ELT (v, i) = trueop0;
		    else
		      RTVEC_ELT (v, i) = CONST_VECTOR_ELT (trueop0, i);
		  }
		else
		  {
		    if (!VECTOR_MODE_P (op1_mode))
		      RTVEC_ELT (v, i) = trueop1;
		    else
		      RTVEC_ELT (v, i) = CONST_VECTOR_ELT (trueop1,
							   i - in_n_elts);
		  }
	      }

	    return gen_rtx_CONST_VECTOR (mode, v);
	  }
      }
      return 0;

    default:
      gcc_unreachable ();
    }

  return 0;
}

rtx
simplify_const_binary_operation (enum rtx_code code, enum machine_mode mode,
				 rtx op0, rtx op1)
{
  HOST_WIDE_INT arg0, arg1, arg0s, arg1s;
  HOST_WIDE_INT val;
  unsigned int width = GET_MODE_BITSIZE (mode);

  if (VECTOR_MODE_P (mode)
      && code != VEC_CONCAT
      && GET_CODE (op0) == CONST_VECTOR
      && GET_CODE (op1) == CONST_VECTOR)
    {
      unsigned n_elts = GET_MODE_NUNITS (mode);
      enum machine_mode op0mode = GET_MODE (op0);
      unsigned op0_n_elts = GET_MODE_NUNITS (op0mode);
      enum machine_mode op1mode = GET_MODE (op1);
      unsigned op1_n_elts = GET_MODE_NUNITS (op1mode);
      rtvec v = rtvec_alloc (n_elts);
      unsigned int i;

      gcc_assert (op0_n_elts == n_elts);
      gcc_assert (op1_n_elts == n_elts);
      for (i = 0; i < n_elts; i++)
	{
	  rtx x = simplify_binary_operation (code, GET_MODE_INNER (mode),
					     CONST_VECTOR_ELT (op0, i),
					     CONST_VECTOR_ELT (op1, i));
	  if (!x)
	    return 0;
	  RTVEC_ELT (v, i) = x;
	}

      return gen_rtx_CONST_VECTOR (mode, v);
    }

  if (VECTOR_MODE_P (mode)
      && code == VEC_CONCAT
      && CONSTANT_P (op0) && CONSTANT_P (op1))
    {
      unsigned n_elts = GET_MODE_NUNITS (mode);
      rtvec v = rtvec_alloc (n_elts);

      gcc_assert (n_elts >= 2);
      if (n_elts == 2)
	{
	  gcc_assert (GET_CODE (op0) != CONST_VECTOR);
	  gcc_assert (GET_CODE (op1) != CONST_VECTOR);

	  RTVEC_ELT (v, 0) = op0;
	  RTVEC_ELT (v, 1) = op1;
	}
      else
	{
	  unsigned op0_n_elts = GET_MODE_NUNITS (GET_MODE (op0));
	  unsigned op1_n_elts = GET_MODE_NUNITS (GET_MODE (op1));
	  unsigned i;

	  gcc_assert (GET_CODE (op0) == CONST_VECTOR);
	  gcc_assert (GET_CODE (op1) == CONST_VECTOR);
	  gcc_assert (op0_n_elts + op1_n_elts == n_elts);

	  for (i = 0; i < op0_n_elts; ++i)
	    RTVEC_ELT (v, i) = XVECEXP (op0, 0, i);
	  for (i = 0; i < op1_n_elts; ++i)
	    RTVEC_ELT (v, op0_n_elts+i) = XVECEXP (op1, 0, i);
	}

      return gen_rtx_CONST_VECTOR (mode, v);
    }

  if (SCALAR_FLOAT_MODE_P (mode)
      && GET_CODE (op0) == CONST_DOUBLE
      && GET_CODE (op1) == CONST_DOUBLE
      && mode == GET_MODE (op0) && mode == GET_MODE (op1))
    {
      if (code == AND
	  || code == IOR
	  || code == XOR)
	{
	  long tmp0[4];
	  long tmp1[4];
	  REAL_VALUE_TYPE r;
	  int i;

	  real_to_target (tmp0, CONST_DOUBLE_REAL_VALUE (op0),
			  GET_MODE (op0));
	  real_to_target (tmp1, CONST_DOUBLE_REAL_VALUE (op1),
			  GET_MODE (op1));
	  for (i = 0; i < 4; i++)
	    {
	      switch (code)
	      {
	      case AND:
		tmp0[i] &= tmp1[i];
		break;
	      case IOR:
		tmp0[i] |= tmp1[i];
		break;
	      case XOR:
		tmp0[i] ^= tmp1[i];
		break;
	      default:
		gcc_unreachable ();
	      }
	    }
	   real_from_target (&r, tmp0, mode);
	   return CONST_DOUBLE_FROM_REAL_VALUE (r, mode);
	}
      else
	{
	  REAL_VALUE_TYPE f0, f1, value, result;
	  bool inexact;

	  REAL_VALUE_FROM_CONST_DOUBLE (f0, op0);
	  REAL_VALUE_FROM_CONST_DOUBLE (f1, op1);
	  real_convert (&f0, mode, &f0);
	  real_convert (&f1, mode, &f1);

	  if (HONOR_SNANS (mode)
	      && (REAL_VALUE_ISNAN (f0) || REAL_VALUE_ISNAN (f1)))
	    return 0;

	  if (code == DIV
	      && REAL_VALUES_EQUAL (f1, dconst0)
	      && (flag_trapping_math || ! MODE_HAS_INFINITIES (mode)))
	    return 0;

	  if (MODE_HAS_INFINITIES (mode) && HONOR_NANS (mode)
	      && flag_trapping_math
	      && REAL_VALUE_ISINF (f0) && REAL_VALUE_ISINF (f1))
	    {
	      int s0 = REAL_VALUE_NEGATIVE (f0);
	      int s1 = REAL_VALUE_NEGATIVE (f1);

	      switch (code)
		{
		case PLUS:
		  /* Inf + -Inf = NaN plus exception.  */
		  if (s0 != s1)
		    return 0;
		  break;
		case MINUS:
		  /* Inf - Inf = NaN plus exception.  */
		  if (s0 == s1)
		    return 0;
		  break;
		case DIV:
		  /* Inf / Inf = NaN plus exception.  */
		  return 0;
		default:
		  break;
		}
	    }

	  if (code == MULT && MODE_HAS_INFINITIES (mode) && HONOR_NANS (mode)
	      && flag_trapping_math
	      && ((REAL_VALUE_ISINF (f0) && REAL_VALUES_EQUAL (f1, dconst0))
		  || (REAL_VALUE_ISINF (f1)
		      && REAL_VALUES_EQUAL (f0, dconst0))))
	    /* Inf * 0 = NaN plus exception.  */
	    return 0;

	  inexact = real_arithmetic (&value, rtx_to_tree_code (code),
				     &f0, &f1);
	  real_convert (&result, mode, &value);

	  /* Don't constant fold this floating point operation if
	     the result has overflowed and flag_trapping_math.  */

	  if (flag_trapping_math
	      && MODE_HAS_INFINITIES (mode)
	      && REAL_VALUE_ISINF (result)
	      && !REAL_VALUE_ISINF (f0)
	      && !REAL_VALUE_ISINF (f1))
	    /* Overflow plus exception.  */
	    return 0;

	  /* Don't constant fold this floating point operation if the
	     result may dependent upon the run-time rounding mode and
	     flag_rounding_math is set, or if GCC's software emulation
	     is unable to accurately represent the result.  */

	  if ((flag_rounding_math
	       || (REAL_MODE_FORMAT_COMPOSITE_P (mode)
		   && !flag_unsafe_math_optimizations))
	      && (inexact || !real_identical (&result, &value)))
	    return NULL_RTX;

	  return CONST_DOUBLE_FROM_REAL_VALUE (result, mode);
	}
    }

  /* We can fold some multi-word operations.  */
  if (GET_MODE_CLASS (mode) == MODE_INT
      && width == HOST_BITS_PER_WIDE_INT * 2
      && (GET_CODE (op0) == CONST_DOUBLE || GET_CODE (op0) == CONST_INT)
      && (GET_CODE (op1) == CONST_DOUBLE || GET_CODE (op1) == CONST_INT))
    {
      unsigned HOST_WIDE_INT l1, l2, lv, lt;
      HOST_WIDE_INT h1, h2, hv, ht;

      if (GET_CODE (op0) == CONST_DOUBLE)
	l1 = CONST_DOUBLE_LOW (op0), h1 = CONST_DOUBLE_HIGH (op0);
      else
	l1 = INTVAL (op0), h1 = HWI_SIGN_EXTEND (l1);

      if (GET_CODE (op1) == CONST_DOUBLE)
	l2 = CONST_DOUBLE_LOW (op1), h2 = CONST_DOUBLE_HIGH (op1);
      else
	l2 = INTVAL (op1), h2 = HWI_SIGN_EXTEND (l2);

      switch (code)
	{
	case MINUS:
	  /* A - B == A + (-B).  */
	  neg_double (l2, h2, &lv, &hv);
	  l2 = lv, h2 = hv;

	  /* Fall through....  */

	case PLUS:
	  add_double (l1, h1, l2, h2, &lv, &hv);
	  break;

	case MULT:
	  mul_double (l1, h1, l2, h2, &lv, &hv);
	  break;

	case DIV:
	  if (div_and_round_double (TRUNC_DIV_EXPR, 0, l1, h1, l2, h2,
				    &lv, &hv, &lt, &ht))
	    return 0;
	  break;

	case MOD:
	  if (div_and_round_double (TRUNC_DIV_EXPR, 0, l1, h1, l2, h2,
				    &lt, &ht, &lv, &hv))
	    return 0;
	  break;

	case UDIV:
	  if (div_and_round_double (TRUNC_DIV_EXPR, 1, l1, h1, l2, h2,
				    &lv, &hv, &lt, &ht))
	    return 0;
	  break;

	case UMOD:
	  if (div_and_round_double (TRUNC_DIV_EXPR, 1, l1, h1, l2, h2,
				    &lt, &ht, &lv, &hv))
	    return 0;
	  break;

	case AND:
	  lv = l1 & l2, hv = h1 & h2;
	  break;

	case IOR:
	  lv = l1 | l2, hv = h1 | h2;
	  break;

	case XOR:
	  lv = l1 ^ l2, hv = h1 ^ h2;
	  break;

	case SMIN:
	  if (h1 < h2
	      || (h1 == h2
		  && ((unsigned HOST_WIDE_INT) l1
		      < (unsigned HOST_WIDE_INT) l2)))
	    lv = l1, hv = h1;
	  else
	    lv = l2, hv = h2;
	  break;

	case SMAX:
	  if (h1 > h2
	      || (h1 == h2
		  && ((unsigned HOST_WIDE_INT) l1
		      > (unsigned HOST_WIDE_INT) l2)))
	    lv = l1, hv = h1;
	  else
	    lv = l2, hv = h2;
	  break;

	case UMIN:
	  if ((unsigned HOST_WIDE_INT) h1 < (unsigned HOST_WIDE_INT) h2
	      || (h1 == h2
		  && ((unsigned HOST_WIDE_INT) l1
		      < (unsigned HOST_WIDE_INT) l2)))
	    lv = l1, hv = h1;
	  else
	    lv = l2, hv = h2;
	  break;

	case UMAX:
	  if ((unsigned HOST_WIDE_INT) h1 > (unsigned HOST_WIDE_INT) h2
	      || (h1 == h2
		  && ((unsigned HOST_WIDE_INT) l1
		      > (unsigned HOST_WIDE_INT) l2)))
	    lv = l1, hv = h1;
	  else
	    lv = l2, hv = h2;
	  break;

	case LSHIFTRT:   case ASHIFTRT:
	case ASHIFT:
	case ROTATE:     case ROTATERT:
	  if (SHIFT_COUNT_TRUNCATED)
	    l2 &= (GET_MODE_BITSIZE (mode) - 1), h2 = 0;

	  if (h2 != 0 || l2 >= GET_MODE_BITSIZE (mode))
	    return 0;

	  if (code == LSHIFTRT || code == ASHIFTRT)
	    rshift_double (l1, h1, l2, GET_MODE_BITSIZE (mode), &lv, &hv,
			   code == ASHIFTRT);
	  else if (code == ASHIFT)
	    lshift_double (l1, h1, l2, GET_MODE_BITSIZE (mode), &lv, &hv, 1);
	  else if (code == ROTATE)
	    lrotate_double (l1, h1, l2, GET_MODE_BITSIZE (mode), &lv, &hv);
	  else /* code == ROTATERT */
	    rrotate_double (l1, h1, l2, GET_MODE_BITSIZE (mode), &lv, &hv);
	  break;

	default:
	  return 0;
	}

      return immed_double_const (lv, hv, mode);
    }

  if (GET_CODE (op0) == CONST_INT && GET_CODE (op1) == CONST_INT
      && width <= HOST_BITS_PER_WIDE_INT && width != 0)
    {
      /* Get the integer argument values in two forms:
         zero-extended in ARG0, ARG1 and sign-extended in ARG0S, ARG1S.  */

      arg0 = INTVAL (op0);
      arg1 = INTVAL (op1);

      if (width < HOST_BITS_PER_WIDE_INT)
        {
          arg0 &= ((HOST_WIDE_INT) 1 << width) - 1;
          arg1 &= ((HOST_WIDE_INT) 1 << width) - 1;

          arg0s = arg0;
          if (arg0s & ((HOST_WIDE_INT) 1 << (width - 1)))
	    arg0s |= ((HOST_WIDE_INT) (-1) << width);

	  arg1s = arg1;
	  if (arg1s & ((HOST_WIDE_INT) 1 << (width - 1)))
	    arg1s |= ((HOST_WIDE_INT) (-1) << width);
	}
      else
	{
	  arg0s = arg0;
	  arg1s = arg1;
	}
      
      /* Compute the value of the arithmetic.  */
      
      switch (code)
	{
	case PLUS:
	  val = arg0s + arg1s;
	  break;
	  
	case MINUS:
	  val = arg0s - arg1s;
	  break;
	  
	case MULT:
	  val = arg0s * arg1s;
	  break;
	  
	case DIV:
	  if (arg1s == 0
	      || (arg0s == (HOST_WIDE_INT) 1 << (HOST_BITS_PER_WIDE_INT - 1)
		  && arg1s == -1))
	    return 0;
	  val = arg0s / arg1s;
	  break;
	  
	case MOD:
	  if (arg1s == 0
	      || (arg0s == (HOST_WIDE_INT) 1 << (HOST_BITS_PER_WIDE_INT - 1)
		  && arg1s == -1))
	    return 0;
	  val = arg0s % arg1s;
	  break;
	  
	case UDIV:
	  if (arg1 == 0
	      || (arg0s == (HOST_WIDE_INT) 1 << (HOST_BITS_PER_WIDE_INT - 1)
		  && arg1s == -1))
	    return 0;
	  val = (unsigned HOST_WIDE_INT) arg0 / arg1;
	  break;
	  
	case UMOD:
	  if (arg1 == 0
	      || (arg0s == (HOST_WIDE_INT) 1 << (HOST_BITS_PER_WIDE_INT - 1)
		  && arg1s == -1))
	    return 0;
	  val = (unsigned HOST_WIDE_INT) arg0 % arg1;
	  break;
	  
	case AND:
	  val = arg0 & arg1;
	  break;
	  
	case IOR:
	  val = arg0 | arg1;
	  break;
	  
	case XOR:
	  val = arg0 ^ arg1;
	  break;
	  
	case LSHIFTRT:
	case ASHIFT:
	case ASHIFTRT:
	  /* Truncate the shift if SHIFT_COUNT_TRUNCATED, otherwise make sure
	     the value is in range.  We can't return any old value for
	     out-of-range arguments because either the middle-end (via
	     shift_truncation_mask) or the back-end might be relying on
	     target-specific knowledge.  Nor can we rely on
	     shift_truncation_mask, since the shift might not be part of an
	     ashlM3, lshrM3 or ashrM3 instruction.  */
	  if (SHIFT_COUNT_TRUNCATED)
	    arg1 = (unsigned HOST_WIDE_INT) arg1 % width;
	  else if (arg1 < 0 || arg1 >= GET_MODE_BITSIZE (mode))
	    return 0;
	  
	  val = (code == ASHIFT
		 ? ((unsigned HOST_WIDE_INT) arg0) << arg1
		 : ((unsigned HOST_WIDE_INT) arg0) >> arg1);
	  
	  /* Sign-extend the result for arithmetic right shifts.  */
	  if (code == ASHIFTRT && arg0s < 0 && arg1 > 0)
	    val |= ((HOST_WIDE_INT) -1) << (width - arg1);
	  break;
	  
	case ROTATERT:
	  if (arg1 < 0)
	    return 0;
	  
	  arg1 %= width;
	  val = ((((unsigned HOST_WIDE_INT) arg0) << (width - arg1))
		 | (((unsigned HOST_WIDE_INT) arg0) >> arg1));
	  break;
	  
	case ROTATE:
	  if (arg1 < 0)
	    return 0;
	  
	  arg1 %= width;
	  val = ((((unsigned HOST_WIDE_INT) arg0) << arg1)
		 | (((unsigned HOST_WIDE_INT) arg0) >> (width - arg1)));
	  break;
	  
	case COMPARE:
	  /* Do nothing here.  */
	  return 0;
	  
	case SMIN:
	  val = arg0s <= arg1s ? arg0s : arg1s;
	  break;
	  
	case UMIN:
	  val = ((unsigned HOST_WIDE_INT) arg0
		 <= (unsigned HOST_WIDE_INT) arg1 ? arg0 : arg1);
	  break;
	  
	case SMAX:
	  val = arg0s > arg1s ? arg0s : arg1s;
	  break;
	  
	case UMAX:
	  val = ((unsigned HOST_WIDE_INT) arg0
		 > (unsigned HOST_WIDE_INT) arg1 ? arg0 : arg1);
	  break;
	  
	case SS_PLUS:
	case US_PLUS:
	case SS_MINUS:
	case US_MINUS:
	case SS_ASHIFT:
	  /* ??? There are simplifications that can be done.  */
	  return 0;
	  
	default:
	  gcc_unreachable ();
	}

      return gen_int_mode (val, mode);
    }

  return NULL_RTX;
}



/* Simplify a PLUS or MINUS, at least one of whose operands may be another
   PLUS or MINUS.

   Rather than test for specific case, we do this by a brute-force method
   and do all possible simplifications until no more changes occur.  Then
   we rebuild the operation.  */

struct simplify_plus_minus_op_data
{
  rtx op;
  short neg;
};

static int
simplify_plus_minus_op_data_cmp (const void *p1, const void *p2)
{
  const struct simplify_plus_minus_op_data *d1 = p1;
  const struct simplify_plus_minus_op_data *d2 = p2;
  int result;

  result = (commutative_operand_precedence (d2->op)
	    - commutative_operand_precedence (d1->op));
  if (result)
    return result;

  /* Group together equal REGs to do more simplification.  */
  if (REG_P (d1->op) && REG_P (d2->op))
    return REGNO (d1->op) - REGNO (d2->op);
  else
    return 0;
}

static rtx
simplify_plus_minus (enum rtx_code code, enum machine_mode mode, rtx op0,
		     rtx op1)
{
  struct simplify_plus_minus_op_data ops[8];
  rtx result, tem;
  int n_ops = 2, input_ops = 2;
  int changed, n_constants = 0, canonicalized = 0;
  int i, j;

  memset (ops, 0, sizeof ops);

  /* Set up the two operands and then expand them until nothing has been
     changed.  If we run out of room in our array, give up; this should
     almost never happen.  */

  ops[0].op = op0;
  ops[0].neg = 0;
  ops[1].op = op1;
  ops[1].neg = (code == MINUS);

  do
    {
      changed = 0;

      for (i = 0; i < n_ops; i++)
	{
	  rtx this_op = ops[i].op;
	  int this_neg = ops[i].neg;
	  enum rtx_code this_code = GET_CODE (this_op);

	  switch (this_code)
	    {
	    case PLUS:
	    case MINUS:
	      if (n_ops == 7)
		return NULL_RTX;

	      ops[n_ops].op = XEXP (this_op, 1);
	      ops[n_ops].neg = (this_code == MINUS) ^ this_neg;
	      n_ops++;

	      ops[i].op = XEXP (this_op, 0);
	      input_ops++;
	      changed = 1;
	      canonicalized |= this_neg;
	      break;

	    case NEG:
	      ops[i].op = XEXP (this_op, 0);
	      ops[i].neg = ! this_neg;
	      changed = 1;
	      canonicalized = 1;
	      break;

	    case CONST:
	      if (n_ops < 7
		  && GET_CODE (XEXP (this_op, 0)) == PLUS
		  && CONSTANT_P (XEXP (XEXP (this_op, 0), 0))
		  && CONSTANT_P (XEXP (XEXP (this_op, 0), 1)))
		{
		  ops[i].op = XEXP (XEXP (this_op, 0), 0);
		  ops[n_ops].op = XEXP (XEXP (this_op, 0), 1);
		  ops[n_ops].neg = this_neg;
		  n_ops++;
		  changed = 1;
	          canonicalized = 1;
		}
	      break;

	    case NOT:
	      /* ~a -> (-a - 1) */
	      if (n_ops != 7)
		{
		  ops[n_ops].op = constm1_rtx;
		  ops[n_ops++].neg = this_neg;
		  ops[i].op = XEXP (this_op, 0);
		  ops[i].neg = !this_neg;
		  changed = 1;
	          canonicalized = 1;
		}
	      break;

	    case CONST_INT:
	      n_constants++;
	      if (this_neg)
		{
		  ops[i].op = neg_const_int (mode, this_op);
		  ops[i].neg = 0;
		  changed = 1;
	          canonicalized = 1;
		}
	      break;

	    default:
	      break;
	    }
	}
    }
  while (changed);

  if (n_constants > 1)
    canonicalized = 1;

  gcc_assert (n_ops >= 2);

  /* If we only have two operands, we can avoid the loops.  */
  if (n_ops == 2)
    {
      enum rtx_code code = ops[0].neg || ops[1].neg ? MINUS : PLUS;
      rtx lhs, rhs;

      /* Get the two operands.  Be careful with the order, especially for
	 the cases where code == MINUS.  */
      if (ops[0].neg && ops[1].neg)
	{
	  lhs = gen_rtx_NEG (mode, ops[0].op);
	  rhs = ops[1].op;
	}
      else if (ops[0].neg)
	{
	  lhs = ops[1].op;
	  rhs = ops[0].op;
	}
      else
	{
	  lhs = ops[0].op;
	  rhs = ops[1].op;
	}

      return simplify_const_binary_operation (code, mode, lhs, rhs);
    }

  /* Now simplify each pair of operands until nothing changes.  */
  do
    {
      /* Insertion sort is good enough for an eight-element array.  */
      for (i = 1; i < n_ops; i++)
        {
          struct simplify_plus_minus_op_data save;
          j = i - 1;
          if (simplify_plus_minus_op_data_cmp (&ops[j], &ops[i]) < 0)
	    continue;

          canonicalized = 1;
          save = ops[i];
          do
	    ops[j + 1] = ops[j];
          while (j-- && simplify_plus_minus_op_data_cmp (&ops[j], &save) > 0);
          ops[j + 1] = save;
        }

      /* This is only useful the first time through.  */
      if (!canonicalized)
        return NULL_RTX;

      changed = 0;
      for (i = n_ops - 1; i > 0; i--)
	for (j = i - 1; j >= 0; j--)
	  {
	    rtx lhs = ops[j].op, rhs = ops[i].op;
	    int lneg = ops[j].neg, rneg = ops[i].neg;

	    if (lhs != 0 && rhs != 0)
	      {
		enum rtx_code ncode = PLUS;

		if (lneg != rneg)
		  {
		    ncode = MINUS;
		    if (lneg)
		      tem = lhs, lhs = rhs, rhs = tem;
		  }
		else if (swap_commutative_operands_p (lhs, rhs))
		  tem = lhs, lhs = rhs, rhs = tem;

		if ((GET_CODE (lhs) == CONST || GET_CODE (lhs) == CONST_INT)
		    && (GET_CODE (rhs) == CONST || GET_CODE (rhs) == CONST_INT))
		  {
		    rtx tem_lhs, tem_rhs;

		    tem_lhs = GET_CODE (lhs) == CONST ? XEXP (lhs, 0) : lhs;
		    tem_rhs = GET_CODE (rhs) == CONST ? XEXP (rhs, 0) : rhs;
		    tem = simplify_binary_operation (ncode, mode, tem_lhs, tem_rhs);

		    if (tem && !CONSTANT_P (tem))
		      tem = gen_rtx_CONST (GET_MODE (tem), tem);
		  }
		else
		  tem = simplify_binary_operation (ncode, mode, lhs, rhs);
		
		/* Reject "simplifications" that just wrap the two
		   arguments in a CONST.  Failure to do so can result
		   in infinite recursion with simplify_binary_operation
		   when it calls us to simplify CONST operations.  */
		if (tem
		    && ! (GET_CODE (tem) == CONST
			  && GET_CODE (XEXP (tem, 0)) == ncode
			  && XEXP (XEXP (tem, 0), 0) == lhs
			  && XEXP (XEXP (tem, 0), 1) == rhs))
		  {
		    lneg &= rneg;
		    if (GET_CODE (tem) == NEG)
		      tem = XEXP (tem, 0), lneg = !lneg;
		    if (GET_CODE (tem) == CONST_INT && lneg)
		      tem = neg_const_int (mode, tem), lneg = 0;

		    ops[i].op = tem;
		    ops[i].neg = lneg;
		    ops[j].op = NULL_RTX;
		    changed = 1;
		  }
	      }
	  }

      /* Pack all the operands to the lower-numbered entries.  */
      for (i = 0, j = 0; j < n_ops; j++)
        if (ops[j].op)
          {
	    ops[i] = ops[j];
	    i++;
          }
      n_ops = i;
    }
  while (changed);

  /* Create (minus -C X) instead of (neg (const (plus X C))).  */
  if (n_ops == 2
      && GET_CODE (ops[1].op) == CONST_INT
      && CONSTANT_P (ops[0].op)
      && ops[0].neg)
    return gen_rtx_fmt_ee (MINUS, mode, ops[1].op, ops[0].op);
  
  /* We suppressed creation of trivial CONST expressions in the
     combination loop to avoid recursion.  Create one manually now.
     The combination loop should have ensured that there is exactly
     one CONST_INT, and the sort will have ensured that it is last
     in the array and that any other constant will be next-to-last.  */

  if (n_ops > 1
      && GET_CODE (ops[n_ops - 1].op) == CONST_INT
      && CONSTANT_P (ops[n_ops - 2].op))
    {
      rtx value = ops[n_ops - 1].op;
      if (ops[n_ops - 1].neg ^ ops[n_ops - 2].neg)
	value = neg_const_int (mode, value);
      ops[n_ops - 2].op = plus_constant (ops[n_ops - 2].op, INTVAL (value));
      n_ops--;
    }

  /* Put a non-negated operand first, if possible.  */

  for (i = 0; i < n_ops && ops[i].neg; i++)
    continue;
  if (i == n_ops)
    ops[0].op = gen_rtx_NEG (mode, ops[0].op);
  else if (i != 0)
    {
      tem = ops[0].op;
      ops[0] = ops[i];
      ops[i].op = tem;
      ops[i].neg = 1;
    }

  /* Now make the result by performing the requested operations.  */
  result = ops[0].op;
  for (i = 1; i < n_ops; i++)
    result = gen_rtx_fmt_ee (ops[i].neg ? MINUS : PLUS,
			     mode, result, ops[i].op);

  return result;
}

/* Check whether an operand is suitable for calling simplify_plus_minus.  */
static bool
plus_minus_operand_p (rtx x)
{
  return GET_CODE (x) == PLUS
         || GET_CODE (x) == MINUS
	 || (GET_CODE (x) == CONST
	     && GET_CODE (XEXP (x, 0)) == PLUS
	     && CONSTANT_P (XEXP (XEXP (x, 0), 0))
	     && CONSTANT_P (XEXP (XEXP (x, 0), 1)));
}

/* Like simplify_binary_operation except used for relational operators.
   MODE is the mode of the result. If MODE is VOIDmode, both operands must
   not also be VOIDmode.

   CMP_MODE specifies in which mode the comparison is done in, so it is
   the mode of the operands.  If CMP_MODE is VOIDmode, it is taken from
   the operands or, if both are VOIDmode, the operands are compared in
   "infinite precision".  */
rtx
simplify_relational_operation (enum rtx_code code, enum machine_mode mode,
			       enum machine_mode cmp_mode, rtx op0, rtx op1)
{
  rtx tem, trueop0, trueop1;

  if (cmp_mode == VOIDmode)
    cmp_mode = GET_MODE (op0);
  if (cmp_mode == VOIDmode)
    cmp_mode = GET_MODE (op1);

  tem = simplify_const_relational_operation (code, cmp_mode, op0, op1);
  if (tem)
    {
      if (SCALAR_FLOAT_MODE_P (mode))
	{
          if (tem == const0_rtx)
            return CONST0_RTX (mode);
#ifdef FLOAT_STORE_FLAG_VALUE
	  {
	    REAL_VALUE_TYPE val;
	    val = FLOAT_STORE_FLAG_VALUE (mode);
	    return CONST_DOUBLE_FROM_REAL_VALUE (val, mode);
	  }
#else
	  return NULL_RTX;
#endif 
	}
      if (VECTOR_MODE_P (mode))
	{
	  if (tem == const0_rtx)
	    return CONST0_RTX (mode);
#ifdef VECTOR_STORE_FLAG_VALUE
	  {
	    int i, units;
	    rtvec v;

	    rtx val = VECTOR_STORE_FLAG_VALUE (mode);
	    if (val == NULL_RTX)
	      return NULL_RTX;
	    if (val == const1_rtx)
	      return CONST1_RTX (mode);

	    units = GET_MODE_NUNITS (mode);
	    v = rtvec_alloc (units);
	    for (i = 0; i < units; i++)
	      RTVEC_ELT (v, i) = val;
	    return gen_rtx_raw_CONST_VECTOR (mode, v);
	  }
#else
	  return NULL_RTX;
#endif
	}

      return tem;
    }

  /* For the following tests, ensure const0_rtx is op1.  */
  if (swap_commutative_operands_p (op0, op1)
      || (op0 == const0_rtx && op1 != const0_rtx))
    tem = op0, op0 = op1, op1 = tem, code = swap_condition (code);

  /* If op0 is a compare, extract the comparison arguments from it.  */
  if (GET_CODE (op0) == COMPARE && op1 == const0_rtx)
    return simplify_relational_operation (code, mode, VOIDmode,
				          XEXP (op0, 0), XEXP (op0, 1));

  if (GET_MODE_CLASS (cmp_mode) == MODE_CC
      || CC0_P (op0))
    return NULL_RTX;

  trueop0 = avoid_constant_pool_reference (op0);
  trueop1 = avoid_constant_pool_reference (op1);
  return simplify_relational_operation_1 (code, mode, cmp_mode,
		  			  trueop0, trueop1);
}

/* This part of simplify_relational_operation is only used when CMP_MODE
   is not in class MODE_CC (i.e. it is a real comparison).

   MODE is the mode of the result, while CMP_MODE specifies in which
   mode the comparison is done in, so it is the mode of the operands.  */

static rtx
simplify_relational_operation_1 (enum rtx_code code, enum machine_mode mode,
				 enum machine_mode cmp_mode, rtx op0, rtx op1)
{
  enum rtx_code op0code = GET_CODE (op0);

  if (GET_CODE (op1) == CONST_INT)
    {
      if (INTVAL (op1) == 0 && COMPARISON_P (op0))
	{
	  /* If op0 is a comparison, extract the comparison arguments
	     from it.  */
	  if (code == NE)
	    {
	      if (GET_MODE (op0) == mode)
		return simplify_rtx (op0);
	      else
		return simplify_gen_relational (GET_CODE (op0), mode, VOIDmode,
					        XEXP (op0, 0), XEXP (op0, 1));
	    }
	  else if (code == EQ)
	    {
	      enum rtx_code new_code = reversed_comparison_code (op0, NULL_RTX);
	      if (new_code != UNKNOWN)
	        return simplify_gen_relational (new_code, mode, VOIDmode,
					        XEXP (op0, 0), XEXP (op0, 1));
	    }
	}
    }

  /* (eq/ne (plus x cst1) cst2) simplifies to (eq/ne x (cst2 - cst1))  */
  if ((code == EQ || code == NE)
      && (op0code == PLUS || op0code == MINUS)
      && CONSTANT_P (op1)
      && CONSTANT_P (XEXP (op0, 1))
      && (INTEGRAL_MODE_P (cmp_mode) || flag_unsafe_math_optimizations))
    {
      rtx x = XEXP (op0, 0);
      rtx c = XEXP (op0, 1);

      c = simplify_gen_binary (op0code == PLUS ? MINUS : PLUS,
			       cmp_mode, op1, c);
      return simplify_gen_relational (code, mode, cmp_mode, x, c);
    }

  /* (ne:SI (zero_extract:SI FOO (const_int 1) BAR) (const_int 0))) is
     the same as (zero_extract:SI FOO (const_int 1) BAR).  */
  if (code == NE
      && op1 == const0_rtx
      && GET_MODE_CLASS (mode) == MODE_INT
      && cmp_mode != VOIDmode
      /* ??? Work-around BImode bugs in the ia64 backend.  */
      && mode != BImode
      && cmp_mode != BImode
      && nonzero_bits (op0, cmp_mode) == 1
      && STORE_FLAG_VALUE == 1)
    return GET_MODE_SIZE (mode) > GET_MODE_SIZE (cmp_mode)
	   ? simplify_gen_unary (ZERO_EXTEND, mode, op0, cmp_mode)
	   : lowpart_subreg (mode, op0, cmp_mode);

  /* (eq/ne (xor x y) 0) simplifies to (eq/ne x y).  */
  if ((code == EQ || code == NE)
      && op1 == const0_rtx
      && op0code == XOR)
    return simplify_gen_relational (code, mode, cmp_mode,
				    XEXP (op0, 0), XEXP (op0, 1));

  /* (eq/ne (xor x y) x) simplifies to (eq/ne y 0).  */
  if ((code == EQ || code == NE)
      && op0code == XOR
      && rtx_equal_p (XEXP (op0, 0), op1)
      && !side_effects_p (XEXP (op0, 0)))
    return simplify_gen_relational (code, mode, cmp_mode,
				    XEXP (op0, 1), const0_rtx);

  /* Likewise (eq/ne (xor x y) y) simplifies to (eq/ne x 0).  */
  if ((code == EQ || code == NE)
      && op0code == XOR
      && rtx_equal_p (XEXP (op0, 1), op1)
      && !side_effects_p (XEXP (op0, 1)))
    return simplify_gen_relational (code, mode, cmp_mode,
				    XEXP (op0, 0), const0_rtx);

  /* (eq/ne (xor x C1) C2) simplifies to (eq/ne x (C1^C2)).  */
  if ((code == EQ || code == NE)
      && op0code == XOR
      && (GET_CODE (op1) == CONST_INT
	  || GET_CODE (op1) == CONST_DOUBLE)
      && (GET_CODE (XEXP (op0, 1)) == CONST_INT
	  || GET_CODE (XEXP (op0, 1)) == CONST_DOUBLE))
    return simplify_gen_relational (code, mode, cmp_mode, XEXP (op0, 0),
				    simplify_gen_binary (XOR, cmp_mode,
							 XEXP (op0, 1), op1));

  return NULL_RTX;
}

/* Check if the given comparison (done in the given MODE) is actually a
   tautology or a contradiction.
   If no simplification is possible, this function returns zero.
   Otherwise, it returns either const_true_rtx or const0_rtx.  */

rtx
simplify_const_relational_operation (enum rtx_code code,
				     enum machine_mode mode,
				     rtx op0, rtx op1)
{
  int equal, op0lt, op0ltu, op1lt, op1ltu;
  rtx tem;
  rtx trueop0;
  rtx trueop1;

  gcc_assert (mode != VOIDmode
	      || (GET_MODE (op0) == VOIDmode
		  && GET_MODE (op1) == VOIDmode));

  /* If op0 is a compare, extract the comparison arguments from it.  */
  if (GET_CODE (op0) == COMPARE && op1 == const0_rtx)
    {
      op1 = XEXP (op0, 1);
      op0 = XEXP (op0, 0);

      if (GET_MODE (op0) != VOIDmode)
	mode = GET_MODE (op0);
      else if (GET_MODE (op1) != VOIDmode)
	mode = GET_MODE (op1);
      else
	return 0;
    }

  /* We can't simplify MODE_CC values since we don't know what the
     actual comparison is.  */
  if (GET_MODE_CLASS (GET_MODE (op0)) == MODE_CC || CC0_P (op0))
    return 0;

  /* Make sure the constant is second.  */
  if (swap_commutative_operands_p (op0, op1))
    {
      tem = op0, op0 = op1, op1 = tem;
      code = swap_condition (code);
    }

  trueop0 = avoid_constant_pool_reference (op0);
  trueop1 = avoid_constant_pool_reference (op1);

  /* For integer comparisons of A and B maybe we can simplify A - B and can
     then simplify a comparison of that with zero.  If A and B are both either
     a register or a CONST_INT, this can't help; testing for these cases will
     prevent infinite recursion here and speed things up.

     We can only do this for EQ and NE comparisons as otherwise we may
     lose or introduce overflow which we cannot disregard as undefined as
     we do not know the signedness of the operation on either the left or
     the right hand side of the comparison.  */

  if (INTEGRAL_MODE_P (mode) && trueop1 != const0_rtx
      && (code == EQ || code == NE)
      && ! ((REG_P (op0) || GET_CODE (trueop0) == CONST_INT)
	    && (REG_P (op1) || GET_CODE (trueop1) == CONST_INT))
      && 0 != (tem = simplify_binary_operation (MINUS, mode, op0, op1))
      /* We cannot do this if tem is a nonzero address.  */
      && ! nonzero_address_p (tem))
    return simplify_const_relational_operation (signed_condition (code),
						mode, tem, const0_rtx);

  if (! HONOR_NANS (mode) && code == ORDERED)
    return const_true_rtx;

  if (! HONOR_NANS (mode) && code == UNORDERED)
    return const0_rtx;

  /* For modes without NaNs, if the two operands are equal, we know the
     result except if they have side-effects.  */
  if (! HONOR_NANS (GET_MODE (trueop0))
      && rtx_equal_p (trueop0, trueop1)
      && ! side_effects_p (trueop0))
    equal = 1, op0lt = 0, op0ltu = 0, op1lt = 0, op1ltu = 0;

  /* If the operands are floating-point constants, see if we can fold
     the result.  */
  else if (GET_CODE (trueop0) == CONST_DOUBLE
	   && GET_CODE (trueop1) == CONST_DOUBLE
	   && SCALAR_FLOAT_MODE_P (GET_MODE (trueop0)))
    {
      REAL_VALUE_TYPE d0, d1;

      REAL_VALUE_FROM_CONST_DOUBLE (d0, trueop0);
      REAL_VALUE_FROM_CONST_DOUBLE (d1, trueop1);

      /* Comparisons are unordered iff at least one of the values is NaN.  */
      if (REAL_VALUE_ISNAN (d0) || REAL_VALUE_ISNAN (d1))
	switch (code)
	  {
	  case UNEQ:
	  case UNLT:
	  case UNGT:
	  case UNLE:
	  case UNGE:
	  case NE:
	  case UNORDERED:
	    return const_true_rtx;
	  case EQ:
	  case LT:
	  case GT:
	  case LE:
	  case GE:
	  case LTGT:
	  case ORDERED:
	    return const0_rtx;
	  default:
	    return 0;
	  }

      equal = REAL_VALUES_EQUAL (d0, d1);
      op0lt = op0ltu = REAL_VALUES_LESS (d0, d1);
      op1lt = op1ltu = REAL_VALUES_LESS (d1, d0);
    }

  /* Otherwise, see if the operands are both integers.  */
  else if ((GET_MODE_CLASS (mode) == MODE_INT || mode == VOIDmode)
	   && (GET_CODE (trueop0) == CONST_DOUBLE
	       || GET_CODE (trueop0) == CONST_INT)
	   && (GET_CODE (trueop1) == CONST_DOUBLE
	       || GET_CODE (trueop1) == CONST_INT))
    {
      int width = GET_MODE_BITSIZE (mode);
      HOST_WIDE_INT l0s, h0s, l1s, h1s;
      unsigned HOST_WIDE_INT l0u, h0u, l1u, h1u;

      /* Get the two words comprising each integer constant.  */
      if (GET_CODE (trueop0) == CONST_DOUBLE)
	{
	  l0u = l0s = CONST_DOUBLE_LOW (trueop0);
	  h0u = h0s = CONST_DOUBLE_HIGH (trueop0);
	}
      else
	{
	  l0u = l0s = INTVAL (trueop0);
	  h0u = h0s = HWI_SIGN_EXTEND (l0s);
	}

      if (GET_CODE (trueop1) == CONST_DOUBLE)
	{
	  l1u = l1s = CONST_DOUBLE_LOW (trueop1);
	  h1u = h1s = CONST_DOUBLE_HIGH (trueop1);
	}
      else
	{
	  l1u = l1s = INTVAL (trueop1);
	  h1u = h1s = HWI_SIGN_EXTEND (l1s);
	}

      /* If WIDTH is nonzero and smaller than HOST_BITS_PER_WIDE_INT,
	 we have to sign or zero-extend the values.  */
      if (width != 0 && width < HOST_BITS_PER_WIDE_INT)
	{
	  l0u &= ((HOST_WIDE_INT) 1 << width) - 1;
	  l1u &= ((HOST_WIDE_INT) 1 << width) - 1;

	  if (l0s & ((HOST_WIDE_INT) 1 << (width - 1)))
	    l0s |= ((HOST_WIDE_INT) (-1) << width);

	  if (l1s & ((HOST_WIDE_INT) 1 << (width - 1)))
	    l1s |= ((HOST_WIDE_INT) (-1) << width);
	}
      if (width != 0 && width <= HOST_BITS_PER_WIDE_INT)
	h0u = h1u = 0, h0s = HWI_SIGN_EXTEND (l0s), h1s = HWI_SIGN_EXTEND (l1s);

      equal = (h0u == h1u && l0u == l1u);
      op0lt = (h0s < h1s || (h0s == h1s && l0u < l1u));
      op1lt = (h1s < h0s || (h1s == h0s && l1u < l0u));
      op0ltu = (h0u < h1u || (h0u == h1u && l0u < l1u));
      op1ltu = (h1u < h0u || (h1u == h0u && l1u < l0u));
    }

  /* Otherwise, there are some code-specific tests we can make.  */
  else
    {
      /* Optimize comparisons with upper and lower bounds.  */
      if (SCALAR_INT_MODE_P (mode)
	  && GET_MODE_BITSIZE (mode) <= HOST_BITS_PER_WIDE_INT)
	{
	  rtx mmin, mmax;
	  int sign;

	  if (code == GEU
	      || code == LEU
	      || code == GTU
	      || code == LTU)
	    sign = 0;
	  else
	    sign = 1;

	  get_mode_bounds (mode, sign, mode, &mmin, &mmax);

	  tem = NULL_RTX;
	  switch (code)
	    {
	    case GEU:
	    case GE:
	      /* x >= min is always true.  */
	      if (rtx_equal_p (trueop1, mmin))
		tem = const_true_rtx;
	      else 
	      break;

	    case LEU:
	    case LE:
	      /* x <= max is always true.  */
	      if (rtx_equal_p (trueop1, mmax))
		tem = const_true_rtx;
	      break;

	    case GTU:
	    case GT:
	      /* x > max is always false.  */
	      if (rtx_equal_p (trueop1, mmax))
		tem = const0_rtx;
	      break;

	    case LTU:
	    case LT:
	      /* x < min is always false.  */
	      if (rtx_equal_p (trueop1, mmin))
		tem = const0_rtx;
	      break;

	    default:
	      break;
	    }
	  if (tem == const0_rtx
	      || tem == const_true_rtx)
	    return tem;
	}

      switch (code)
	{
	case EQ:
	  if (trueop1 == const0_rtx && nonzero_address_p (op0))
	    return const0_rtx;
	  break;

	case NE:
	  if (trueop1 == const0_rtx && nonzero_address_p (op0))
	    return const_true_rtx;
	  break;

	case LT:
	  /* Optimize abs(x) < 0.0.  */
	  if (trueop1 == CONST0_RTX (mode)
	      && !HONOR_SNANS (mode)
	      && (!INTEGRAL_MODE_P (mode)
		  || (!flag_wrapv && !flag_trapv && flag_strict_overflow)))
	    {
	      tem = GET_CODE (trueop0) == FLOAT_EXTEND ? XEXP (trueop0, 0)
						       : trueop0;
	      if (GET_CODE (tem) == ABS)
		{
		  if (INTEGRAL_MODE_P (mode)
		      && (issue_strict_overflow_warning
			  (WARN_STRICT_OVERFLOW_CONDITIONAL)))
		    warning (OPT_Wstrict_overflow,
			     ("assuming signed overflow does not occur when "
			      "assuming abs (x) < 0 is false"));
		  return const0_rtx;
		}
	    }
	  break;

	case GE:
	  /* Optimize abs(x) >= 0.0.  */
	  if (trueop1 == CONST0_RTX (mode)
	      && !HONOR_NANS (mode)
	      && (!INTEGRAL_MODE_P (mode)
		  || (!flag_wrapv && !flag_trapv && flag_strict_overflow)))
	    {
	      tem = GET_CODE (trueop0) == FLOAT_EXTEND ? XEXP (trueop0, 0)
						       : trueop0;
	      if (GET_CODE (tem) == ABS)
		{
		  if (INTEGRAL_MODE_P (mode)
		      && (issue_strict_overflow_warning
			  (WARN_STRICT_OVERFLOW_CONDITIONAL)))
		    warning (OPT_Wstrict_overflow,
			     ("assuming signed overflow does not occur when "
			      "assuming abs (x) >= 0 is true"));
		  return const_true_rtx;
		}
	    }
	  break;

	case UNGE:
	  /* Optimize ! (abs(x) < 0.0).  */
	  if (trueop1 == CONST0_RTX (mode))
	    {
	      tem = GET_CODE (trueop0) == FLOAT_EXTEND ? XEXP (trueop0, 0)
						       : trueop0;
	      if (GET_CODE (tem) == ABS)
		return const_true_rtx;
	    }
	  break;

	default:
	  break;
	}

      return 0;
    }

  /* If we reach here, EQUAL, OP0LT, OP0LTU, OP1LT, and OP1LTU are set
     as appropriate.  */
  switch (code)
    {
    case EQ:
    case UNEQ:
      return equal ? const_true_rtx : const0_rtx;
    case NE:
    case LTGT:
      return ! equal ? const_true_rtx : const0_rtx;
    case LT:
    case UNLT:
      return op0lt ? const_true_rtx : const0_rtx;
    case GT:
    case UNGT:
      return op1lt ? const_true_rtx : const0_rtx;
    case LTU:
      return op0ltu ? const_true_rtx : const0_rtx;
    case GTU:
      return op1ltu ? const_true_rtx : const0_rtx;
    case LE:
    case UNLE:
      return equal || op0lt ? const_true_rtx : const0_rtx;
    case GE:
    case UNGE:
      return equal || op1lt ? const_true_rtx : const0_rtx;
    case LEU:
      return equal || op0ltu ? const_true_rtx : const0_rtx;
    case GEU:
      return equal || op1ltu ? const_true_rtx : const0_rtx;
    case ORDERED:
      return const_true_rtx;
    case UNORDERED:
      return const0_rtx;
    default:
      gcc_unreachable ();
    }
}

/* Simplify CODE, an operation with result mode MODE and three operands,
   OP0, OP1, and OP2.  OP0_MODE was the mode of OP0 before it became
   a constant.  Return 0 if no simplifications is possible.  */

rtx
simplify_ternary_operation (enum rtx_code code, enum machine_mode mode,
			    enum machine_mode op0_mode, rtx op0, rtx op1,
			    rtx op2)
{
  unsigned int width = GET_MODE_BITSIZE (mode);

  /* VOIDmode means "infinite" precision.  */
  if (width == 0)
    width = HOST_BITS_PER_WIDE_INT;

  switch (code)
    {
    case SIGN_EXTRACT:
    case ZERO_EXTRACT:
      if (GET_CODE (op0) == CONST_INT
	  && GET_CODE (op1) == CONST_INT
	  && GET_CODE (op2) == CONST_INT
	  && ((unsigned) INTVAL (op1) + (unsigned) INTVAL (op2) <= width)
	  && width <= (unsigned) HOST_BITS_PER_WIDE_INT)
	{
	  /* Extracting a bit-field from a constant */
	  HOST_WIDE_INT val = INTVAL (op0);

	  if (BITS_BIG_ENDIAN)
	    val >>= (GET_MODE_BITSIZE (op0_mode)
		     - INTVAL (op2) - INTVAL (op1));
	  else
	    val >>= INTVAL (op2);

	  if (HOST_BITS_PER_WIDE_INT != INTVAL (op1))
	    {
	      /* First zero-extend.  */
	      val &= ((HOST_WIDE_INT) 1 << INTVAL (op1)) - 1;
	      /* If desired, propagate sign bit.  */
	      if (code == SIGN_EXTRACT
		  && (val & ((HOST_WIDE_INT) 1 << (INTVAL (op1) - 1))))
		val |= ~ (((HOST_WIDE_INT) 1 << INTVAL (op1)) - 1);
	    }

	  /* Clear the bits that don't belong in our mode,
	     unless they and our sign bit are all one.
	     So we get either a reasonable negative value or a reasonable
	     unsigned value for this mode.  */
	  if (width < HOST_BITS_PER_WIDE_INT
	      && ((val & ((HOST_WIDE_INT) (-1) << (width - 1)))
		  != ((HOST_WIDE_INT) (-1) << (width - 1))))
	    val &= ((HOST_WIDE_INT) 1 << width) - 1;

	  return gen_int_mode (val, mode);
	}
      break;

    case IF_THEN_ELSE:
      if (GET_CODE (op0) == CONST_INT)
	return op0 != const0_rtx ? op1 : op2;

      /* Convert c ? a : a into "a".  */
      if (rtx_equal_p (op1, op2) && ! side_effects_p (op0))
	return op1;

      /* Convert a != b ? a : b into "a".  */
      if (GET_CODE (op0) == NE
	  && ! side_effects_p (op0)
	  && ! HONOR_NANS (mode)
	  && ! HONOR_SIGNED_ZEROS (mode)
	  && ((rtx_equal_p (XEXP (op0, 0), op1)
	       && rtx_equal_p (XEXP (op0, 1), op2))
	      || (rtx_equal_p (XEXP (op0, 0), op2)
		  && rtx_equal_p (XEXP (op0, 1), op1))))
	return op1;

      /* Convert a == b ? a : b into "b".  */
      if (GET_CODE (op0) == EQ
	  && ! side_effects_p (op0)
	  && ! HONOR_NANS (mode)
	  && ! HONOR_SIGNED_ZEROS (mode)
	  && ((rtx_equal_p (XEXP (op0, 0), op1)
	       && rtx_equal_p (XEXP (op0, 1), op2))
	      || (rtx_equal_p (XEXP (op0, 0), op2)
		  && rtx_equal_p (XEXP (op0, 1), op1))))
	return op2;

      if (COMPARISON_P (op0) && ! side_effects_p (op0))
	{
	  enum machine_mode cmp_mode = (GET_MODE (XEXP (op0, 0)) == VOIDmode
					? GET_MODE (XEXP (op0, 1))
					: GET_MODE (XEXP (op0, 0)));
	  rtx temp;

	  /* Look for happy constants in op1 and op2.  */
	  if (GET_CODE (op1) == CONST_INT && GET_CODE (op2) == CONST_INT)
	    {
	      HOST_WIDE_INT t = INTVAL (op1);
	      HOST_WIDE_INT f = INTVAL (op2);

	      if (t == STORE_FLAG_VALUE && f == 0)
	        code = GET_CODE (op0);
	      else if (t == 0 && f == STORE_FLAG_VALUE)
		{
		  enum rtx_code tmp;
		  tmp = reversed_comparison_code (op0, NULL_RTX);
		  if (tmp == UNKNOWN)
		    break;
		  code = tmp;
		}
	      else
		break;

	      return simplify_gen_relational (code, mode, cmp_mode,
					      XEXP (op0, 0), XEXP (op0, 1));
	    }

	  if (cmp_mode == VOIDmode)
	    cmp_mode = op0_mode;
	  temp = simplify_relational_operation (GET_CODE (op0), op0_mode,
			  			cmp_mode, XEXP (op0, 0),
						XEXP (op0, 1));

	  /* See if any simplifications were possible.  */
	  if (temp)
	    {
	      if (GET_CODE (temp) == CONST_INT)
		return temp == const0_rtx ? op2 : op1;
	      else if (temp)
	        return gen_rtx_IF_THEN_ELSE (mode, temp, op1, op2);
	    }
	}
      break;

    case VEC_MERGE:
      gcc_assert (GET_MODE (op0) == mode);
      gcc_assert (GET_MODE (op1) == mode);
      gcc_assert (VECTOR_MODE_P (mode));
      op2 = avoid_constant_pool_reference (op2);
      if (GET_CODE (op2) == CONST_INT)
	{
          int elt_size = GET_MODE_SIZE (GET_MODE_INNER (mode));
	  unsigned n_elts = (GET_MODE_SIZE (mode) / elt_size);
	  int mask = (1 << n_elts) - 1;

	  if (!(INTVAL (op2) & mask))
	    return op1;
	  if ((INTVAL (op2) & mask) == mask)
	    return op0;

	  op0 = avoid_constant_pool_reference (op0);
	  op1 = avoid_constant_pool_reference (op1);
	  if (GET_CODE (op0) == CONST_VECTOR
	      && GET_CODE (op1) == CONST_VECTOR)
	    {
	      rtvec v = rtvec_alloc (n_elts);
	      unsigned int i;

	      for (i = 0; i < n_elts; i++)
		RTVEC_ELT (v, i) = (INTVAL (op2) & (1 << i)
				    ? CONST_VECTOR_ELT (op0, i)
				    : CONST_VECTOR_ELT (op1, i));
	      return gen_rtx_CONST_VECTOR (mode, v);
	    }
	}
      break;

    default:
      gcc_unreachable ();
    }

  return 0;
}

/* Evaluate a SUBREG of a CONST_INT or CONST_DOUBLE or CONST_VECTOR,
   returning another CONST_INT or CONST_DOUBLE or CONST_VECTOR.

   Works by unpacking OP into a collection of 8-bit values
   represented as a little-endian array of 'unsigned char', selecting by BYTE,
   and then repacking them again for OUTERMODE.  */

static rtx
simplify_immed_subreg (enum machine_mode outermode, rtx op, 
		       enum machine_mode innermode, unsigned int byte)
{
  /* We support up to 512-bit values (for V8DFmode).  */
  enum {
    max_bitsize = 512,
    value_bit = 8,
    value_mask = (1 << value_bit) - 1
  };
  unsigned char value[max_bitsize / value_bit];
  int value_start;
  int i;
  int elem;

  int num_elem;
  rtx * elems;
  int elem_bitsize;
  rtx result_s;
  rtvec result_v = NULL;
  enum mode_class outer_class;
  enum machine_mode outer_submode;

  /* Some ports misuse CCmode.  */
  if (GET_MODE_CLASS (outermode) == MODE_CC && GET_CODE (op) == CONST_INT)
    return op;

  /* We have no way to represent a complex constant at the rtl level.  */
  if (COMPLEX_MODE_P (outermode))
    return NULL_RTX;

  /* Unpack the value.  */

  if (GET_CODE (op) == CONST_VECTOR)
    {
      num_elem = CONST_VECTOR_NUNITS (op);
      elems = &CONST_VECTOR_ELT (op, 0);
      elem_bitsize = GET_MODE_BITSIZE (GET_MODE_INNER (innermode));
    }
  else
    {
      num_elem = 1;
      elems = &op;
      elem_bitsize = max_bitsize;
    }
  /* If this asserts, it is too complicated; reducing value_bit may help.  */
  gcc_assert (BITS_PER_UNIT % value_bit == 0);
  /* I don't know how to handle endianness of sub-units.  */
  gcc_assert (elem_bitsize % BITS_PER_UNIT == 0);
  
  for (elem = 0; elem < num_elem; elem++)
    {
      unsigned char * vp;
      rtx el = elems[elem];
      
      /* Vectors are kept in target memory order.  (This is probably
	 a mistake.)  */
      {
	unsigned byte = (elem * elem_bitsize) / BITS_PER_UNIT;
	unsigned ibyte = (((num_elem - 1 - elem) * elem_bitsize) 
			  / BITS_PER_UNIT);
	unsigned word_byte = WORDS_BIG_ENDIAN ? ibyte : byte;
	unsigned subword_byte = BYTES_BIG_ENDIAN ? ibyte : byte;
	unsigned bytele = (subword_byte % UNITS_PER_WORD
			 + (word_byte / UNITS_PER_WORD) * UNITS_PER_WORD);
	vp = value + (bytele * BITS_PER_UNIT) / value_bit;
      }
	
      switch (GET_CODE (el))
	{
	case CONST_INT:
	  for (i = 0;
	       i < HOST_BITS_PER_WIDE_INT && i < elem_bitsize; 
	       i += value_bit)
	    *vp++ = INTVAL (el) >> i;
	  /* CONST_INTs are always logically sign-extended.  */
	  for (; i < elem_bitsize; i += value_bit)
	    *vp++ = INTVAL (el) < 0 ? -1 : 0;
	  break;
      
	case CONST_DOUBLE:
	  if (GET_MODE (el) == VOIDmode)
	    {
	      /* If this triggers, someone should have generated a
		 CONST_INT instead.  */
	      gcc_assert (elem_bitsize > HOST_BITS_PER_WIDE_INT);

	      for (i = 0; i < HOST_BITS_PER_WIDE_INT; i += value_bit)
		*vp++ = CONST_DOUBLE_LOW (el) >> i;
	      while (i < HOST_BITS_PER_WIDE_INT * 2 && i < elem_bitsize)
		{
		  *vp++
		    = CONST_DOUBLE_HIGH (el) >> (i - HOST_BITS_PER_WIDE_INT);
		  i += value_bit;
		}
	      /* It shouldn't matter what's done here, so fill it with
		 zero.  */
	      for (; i < elem_bitsize; i += value_bit)
		*vp++ = 0;
	    }
	  else
	    {
	      long tmp[max_bitsize / 32];
	      int bitsize = GET_MODE_BITSIZE (GET_MODE (el));

	      gcc_assert (SCALAR_FLOAT_MODE_P (GET_MODE (el)));
	      gcc_assert (bitsize <= elem_bitsize);
	      gcc_assert (bitsize % value_bit == 0);

	      real_to_target (tmp, CONST_DOUBLE_REAL_VALUE (el),
			      GET_MODE (el));

	      /* real_to_target produces its result in words affected by
		 FLOAT_WORDS_BIG_ENDIAN.  However, we ignore this,
		 and use WORDS_BIG_ENDIAN instead; see the documentation
	         of SUBREG in rtl.texi.  */
	      for (i = 0; i < bitsize; i += value_bit)
		{
		  int ibase;
		  if (WORDS_BIG_ENDIAN)
		    ibase = bitsize - 1 - i;
		  else
		    ibase = i;
		  *vp++ = tmp[ibase / 32] >> i % 32;
		}
	      
	      /* It shouldn't matter what's done here, so fill it with
		 zero.  */
	      for (; i < elem_bitsize; i += value_bit)
		*vp++ = 0;
	    }
	  break;
	  
	default:
	  gcc_unreachable ();
	}
    }

  /* Now, pick the right byte to start with.  */
  /* Renumber BYTE so that the least-significant byte is byte 0.  A special
     case is paradoxical SUBREGs, which shouldn't be adjusted since they
     will already have offset 0.  */
  if (GET_MODE_SIZE (innermode) >= GET_MODE_SIZE (outermode))
    {
      unsigned ibyte = (GET_MODE_SIZE (innermode) - GET_MODE_SIZE (outermode) 
			- byte);
      unsigned word_byte = WORDS_BIG_ENDIAN ? ibyte : byte;
      unsigned subword_byte = BYTES_BIG_ENDIAN ? ibyte : byte;
      byte = (subword_byte % UNITS_PER_WORD
	      + (word_byte / UNITS_PER_WORD) * UNITS_PER_WORD);
    }

  /* BYTE should still be inside OP.  (Note that BYTE is unsigned,
     so if it's become negative it will instead be very large.)  */
  gcc_assert (byte < GET_MODE_SIZE (innermode));

  /* Convert from bytes to chunks of size value_bit.  */
  value_start = byte * (BITS_PER_UNIT / value_bit);

  /* Re-pack the value.  */
    
  if (VECTOR_MODE_P (outermode))
    {
      num_elem = GET_MODE_NUNITS (outermode);
      result_v = rtvec_alloc (num_elem);
      elems = &RTVEC_ELT (result_v, 0);
      outer_submode = GET_MODE_INNER (outermode);
    }
  else
    {
      num_elem = 1;
      elems = &result_s;
      outer_submode = outermode;
    }

  outer_class = GET_MODE_CLASS (outer_submode);
  elem_bitsize = GET_MODE_BITSIZE (outer_submode);

  gcc_assert (elem_bitsize % value_bit == 0);
  gcc_assert (elem_bitsize + value_start * value_bit <= max_bitsize);

  for (elem = 0; elem < num_elem; elem++)
    {
      unsigned char *vp;
      
      /* Vectors are stored in target memory order.  (This is probably
	 a mistake.)  */
      {
	unsigned byte = (elem * elem_bitsize) / BITS_PER_UNIT;
	unsigned ibyte = (((num_elem - 1 - elem) * elem_bitsize) 
			  / BITS_PER_UNIT);
	unsigned word_byte = WORDS_BIG_ENDIAN ? ibyte : byte;
	unsigned subword_byte = BYTES_BIG_ENDIAN ? ibyte : byte;
	unsigned bytele = (subword_byte % UNITS_PER_WORD
			 + (word_byte / UNITS_PER_WORD) * UNITS_PER_WORD);
	vp = value + value_start + (bytele * BITS_PER_UNIT) / value_bit;
      }

      switch (outer_class)
	{
	case MODE_INT:
	case MODE_PARTIAL_INT:
	  {
	    unsigned HOST_WIDE_INT hi = 0, lo = 0;

	    for (i = 0;
		 i < HOST_BITS_PER_WIDE_INT && i < elem_bitsize;
		 i += value_bit)
	      lo |= (HOST_WIDE_INT)(*vp++ & value_mask) << i;
	    for (; i < elem_bitsize; i += value_bit)
	      hi |= ((HOST_WIDE_INT)(*vp++ & value_mask)
		     << (i - HOST_BITS_PER_WIDE_INT));
	    
	    /* immed_double_const doesn't call trunc_int_for_mode.  I don't
	       know why.  */
	    if (elem_bitsize <= HOST_BITS_PER_WIDE_INT)
	      elems[elem] = gen_int_mode (lo, outer_submode);
	    else if (elem_bitsize <= 2 * HOST_BITS_PER_WIDE_INT)
	      elems[elem] = immed_double_const (lo, hi, outer_submode);
	    else
	      return NULL_RTX;
	  }
	  break;
      
	case MODE_FLOAT:
	case MODE_DECIMAL_FLOAT:
	  {
	    REAL_VALUE_TYPE r;
	    long tmp[max_bitsize / 32];
	    
	    /* real_from_target wants its input in words affected by
	       FLOAT_WORDS_BIG_ENDIAN.  However, we ignore this,
	       and use WORDS_BIG_ENDIAN instead; see the documentation
	       of SUBREG in rtl.texi.  */
	    for (i = 0; i < max_bitsize / 32; i++)
	      tmp[i] = 0;
	    for (i = 0; i < elem_bitsize; i += value_bit)
	      {
		int ibase;
		if (WORDS_BIG_ENDIAN)
		  ibase = elem_bitsize - 1 - i;
		else
		  ibase = i;
		tmp[ibase / 32] |= (*vp++ & value_mask) << i % 32;
	      }

	    real_from_target (&r, tmp, outer_submode);
	    elems[elem] = CONST_DOUBLE_FROM_REAL_VALUE (r, outer_submode);
	  }
	  break;
	    
	default:
	  gcc_unreachable ();
	}
    }
  if (VECTOR_MODE_P (outermode))
    return gen_rtx_CONST_VECTOR (outermode, result_v);
  else
    return result_s;
}

/* Simplify SUBREG:OUTERMODE(OP:INNERMODE, BYTE)
   Return 0 if no simplifications are possible.  */
rtx
simplify_subreg (enum machine_mode outermode, rtx op,
		 enum machine_mode innermode, unsigned int byte)
{
  /* Little bit of sanity checking.  */
  gcc_assert (innermode != VOIDmode);
  gcc_assert (outermode != VOIDmode);
  gcc_assert (innermode != BLKmode);
  gcc_assert (outermode != BLKmode);

  gcc_assert (GET_MODE (op) == innermode
	      || GET_MODE (op) == VOIDmode);

  gcc_assert ((byte % GET_MODE_SIZE (outermode)) == 0);
  gcc_assert (byte < GET_MODE_SIZE (innermode));

  if (outermode == innermode && !byte)
    return op;

  if (GET_CODE (op) == CONST_INT
      || GET_CODE (op) == CONST_DOUBLE
      || GET_CODE (op) == CONST_VECTOR)
    return simplify_immed_subreg (outermode, op, innermode, byte);

  /* Changing mode twice with SUBREG => just change it once,
     or not at all if changing back op starting mode.  */
  if (GET_CODE (op) == SUBREG)
    {
      enum machine_mode innermostmode = GET_MODE (SUBREG_REG (op));
      int final_offset = byte + SUBREG_BYTE (op);
      rtx newx;

      if (outermode == innermostmode
	  && byte == 0 && SUBREG_BYTE (op) == 0)
	return SUBREG_REG (op);

      /* The SUBREG_BYTE represents offset, as if the value were stored
	 in memory.  Irritating exception is paradoxical subreg, where
	 we define SUBREG_BYTE to be 0.  On big endian machines, this
	 value should be negative.  For a moment, undo this exception.  */
      if (byte == 0 && GET_MODE_SIZE (innermode) < GET_MODE_SIZE (outermode))
	{
	  int difference = (GET_MODE_SIZE (innermode) - GET_MODE_SIZE (outermode));
	  if (WORDS_BIG_ENDIAN)
	    final_offset += (difference / UNITS_PER_WORD) * UNITS_PER_WORD;
	  if (BYTES_BIG_ENDIAN)
	    final_offset += difference % UNITS_PER_WORD;
	}
      if (SUBREG_BYTE (op) == 0
	  && GET_MODE_SIZE (innermostmode) < GET_MODE_SIZE (innermode))
	{
	  int difference = (GET_MODE_SIZE (innermostmode) - GET_MODE_SIZE (innermode));
	  if (WORDS_BIG_ENDIAN)
	    final_offset += (difference / UNITS_PER_WORD) * UNITS_PER_WORD;
	  if (BYTES_BIG_ENDIAN)
	    final_offset += difference % UNITS_PER_WORD;
	}

      /* See whether resulting subreg will be paradoxical.  */
      if (GET_MODE_SIZE (innermostmode) > GET_MODE_SIZE (outermode))
	{
	  /* In nonparadoxical subregs we can't handle negative offsets.  */
	  if (final_offset < 0)
	    return NULL_RTX;
	  /* Bail out in case resulting subreg would be incorrect.  */
	  if (final_offset % GET_MODE_SIZE (outermode)
	      || (unsigned) final_offset >= GET_MODE_SIZE (innermostmode))
	    return NULL_RTX;
	}
      else
	{
	  int offset = 0;
	  int difference = (GET_MODE_SIZE (innermostmode) - GET_MODE_SIZE (outermode));

	  /* In paradoxical subreg, see if we are still looking on lower part.
	     If so, our SUBREG_BYTE will be 0.  */
	  if (WORDS_BIG_ENDIAN)
	    offset += (difference / UNITS_PER_WORD) * UNITS_PER_WORD;
	  if (BYTES_BIG_ENDIAN)
	    offset += difference % UNITS_PER_WORD;
	  if (offset == final_offset)
	    final_offset = 0;
	  else
	    return NULL_RTX;
	}

      /* Recurse for further possible simplifications.  */
      newx = simplify_subreg (outermode, SUBREG_REG (op), innermostmode,
			      final_offset);
      if (newx)
	return newx;
      if (validate_subreg (outermode, innermostmode,
			   SUBREG_REG (op), final_offset))
        return gen_rtx_SUBREG (outermode, SUBREG_REG (op), final_offset);
      return NULL_RTX;
    }

  /* Merge implicit and explicit truncations.  */

  if (GET_CODE (op) == TRUNCATE
      && GET_MODE_SIZE (outermode) < GET_MODE_SIZE (innermode)
      && subreg_lowpart_offset (outermode, innermode) == byte)
    return simplify_gen_unary (TRUNCATE, outermode, XEXP (op, 0),
			       GET_MODE (XEXP (op, 0)));

  /* SUBREG of a hard register => just change the register number
     and/or mode.  If the hard register is not valid in that mode,
     suppress this simplification.  If the hard register is the stack,
     frame, or argument pointer, leave this as a SUBREG.  */

  if (REG_P (op)
      && REGNO (op) < FIRST_PSEUDO_REGISTER
#ifdef CANNOT_CHANGE_MODE_CLASS
      && ! (REG_CANNOT_CHANGE_MODE_P (REGNO (op), innermode, outermode)
	    && GET_MODE_CLASS (innermode) != MODE_COMPLEX_INT
	    && GET_MODE_CLASS (innermode) != MODE_COMPLEX_FLOAT)
#endif
      && ((reload_completed && !frame_pointer_needed)
	  || (REGNO (op) != FRAME_POINTER_REGNUM
#if HARD_FRAME_POINTER_REGNUM != FRAME_POINTER_REGNUM
	      && REGNO (op) != HARD_FRAME_POINTER_REGNUM
#endif
	     ))
#if FRAME_POINTER_REGNUM != ARG_POINTER_REGNUM
      && REGNO (op) != ARG_POINTER_REGNUM
#endif
      && REGNO (op) != STACK_POINTER_REGNUM
      && subreg_offset_representable_p (REGNO (op), innermode,
					byte, outermode))
    {
      unsigned int regno = REGNO (op);
      unsigned int final_regno
	= regno + subreg_regno_offset (regno, innermode, byte, outermode);

      /* ??? We do allow it if the current REG is not valid for
	 its mode.  This is a kludge to work around how float/complex
	 arguments are passed on 32-bit SPARC and should be fixed.  */
      if (HARD_REGNO_MODE_OK (final_regno, outermode)
	  || ! HARD_REGNO_MODE_OK (regno, innermode))
	{
	  rtx x;
	  int final_offset = byte;

	  /* Adjust offset for paradoxical subregs.  */
	  if (byte == 0
	      && GET_MODE_SIZE (innermode) < GET_MODE_SIZE (outermode))
	    {
	      int difference = (GET_MODE_SIZE (innermode)
				- GET_MODE_SIZE (outermode));
	      if (WORDS_BIG_ENDIAN)
		final_offset += (difference / UNITS_PER_WORD) * UNITS_PER_WORD;
	      if (BYTES_BIG_ENDIAN)
		final_offset += difference % UNITS_PER_WORD;
	    }

	  x = gen_rtx_REG_offset (op, outermode, final_regno, final_offset);

	  /* Propagate original regno.  We don't have any way to specify
	     the offset inside original regno, so do so only for lowpart.
	     The information is used only by alias analysis that can not
	     grog partial register anyway.  */

	  if (subreg_lowpart_offset (outermode, innermode) == byte)
	    ORIGINAL_REGNO (x) = ORIGINAL_REGNO (op);
	  return x;
	}
    }

  /* If we have a SUBREG of a register that we are replacing and we are
     replacing it with a MEM, make a new MEM and try replacing the
     SUBREG with it.  Don't do this if the MEM has a mode-dependent address
     or if we would be widening it.  */

  if (MEM_P (op)
      && ! mode_dependent_address_p (XEXP (op, 0))
      /* Allow splitting of volatile memory references in case we don't
         have instruction to move the whole thing.  */
      && (! MEM_VOLATILE_P (op)
	  || ! have_insn_for (SET, innermode))
      && GET_MODE_SIZE (outermode) <= GET_MODE_SIZE (GET_MODE (op)))
    return adjust_address_nv (op, outermode, byte);

  /* Handle complex values represented as CONCAT
     of real and imaginary part.  */
  if (GET_CODE (op) == CONCAT)
    {
      unsigned int inner_size, final_offset;
      rtx part, res;

      inner_size = GET_MODE_UNIT_SIZE (innermode);
      part = byte < inner_size ? XEXP (op, 0) : XEXP (op, 1);
      final_offset = byte % inner_size;
      if (final_offset + GET_MODE_SIZE (outermode) > inner_size)
	return NULL_RTX;

      res = simplify_subreg (outermode, part, GET_MODE (part), final_offset);
      if (res)
	return res;
      if (validate_subreg (outermode, GET_MODE (part), part, final_offset))
	return gen_rtx_SUBREG (outermode, part, final_offset);
      return NULL_RTX;
    }

  /* Optimize SUBREG truncations of zero and sign extended values.  */
  if ((GET_CODE (op) == ZERO_EXTEND
       || GET_CODE (op) == SIGN_EXTEND)
      && GET_MODE_BITSIZE (outermode) < GET_MODE_BITSIZE (innermode))
    {
      unsigned int bitpos = subreg_lsb_1 (outermode, innermode, byte);

      /* If we're requesting the lowpart of a zero or sign extension,
	 there are three possibilities.  If the outermode is the same
	 as the origmode, we can omit both the extension and the subreg.
	 If the outermode is not larger than the origmode, we can apply
	 the truncation without the extension.  Finally, if the outermode
	 is larger than the origmode, but both are integer modes, we
	 can just extend to the appropriate mode.  */
      if (bitpos == 0)
	{
	  enum machine_mode origmode = GET_MODE (XEXP (op, 0));
	  if (outermode == origmode)
	    return XEXP (op, 0);
	  if (GET_MODE_BITSIZE (outermode) <= GET_MODE_BITSIZE (origmode))
	    return simplify_gen_subreg (outermode, XEXP (op, 0), origmode,
					subreg_lowpart_offset (outermode,
							       origmode));
	  if (SCALAR_INT_MODE_P (outermode))
	    return simplify_gen_unary (GET_CODE (op), outermode,
				       XEXP (op, 0), origmode);
	}

      /* A SUBREG resulting from a zero extension may fold to zero if
	 it extracts higher bits that the ZERO_EXTEND's source bits.  */
      if (GET_CODE (op) == ZERO_EXTEND
	  && bitpos >= GET_MODE_BITSIZE (GET_MODE (XEXP (op, 0))))
	return CONST0_RTX (outermode);
    }

  /* Simplify (subreg:QI (lshiftrt:SI (sign_extend:SI (x:QI)) C), 0) into
     to (ashiftrt:QI (x:QI) C), where C is a suitable small constant and
     the outer subreg is effectively a truncation to the original mode.  */
  if ((GET_CODE (op) == LSHIFTRT
       || GET_CODE (op) == ASHIFTRT)
      && SCALAR_INT_MODE_P (outermode)
      /* Ensure that OUTERMODE is at least twice as wide as the INNERMODE
	 to avoid the possibility that an outer LSHIFTRT shifts by more
	 than the sign extension's sign_bit_copies and introduces zeros
	 into the high bits of the result.  */
      && (2 * GET_MODE_BITSIZE (outermode)) <= GET_MODE_BITSIZE (innermode)
      && GET_CODE (XEXP (op, 1)) == CONST_INT
      && GET_CODE (XEXP (op, 0)) == SIGN_EXTEND
      && GET_MODE (XEXP (XEXP (op, 0), 0)) == outermode
      && INTVAL (XEXP (op, 1)) < GET_MODE_BITSIZE (outermode)
      && subreg_lsb_1 (outermode, innermode, byte) == 0)
    return simplify_gen_binary (ASHIFTRT, outermode,
				XEXP (XEXP (op, 0), 0), XEXP (op, 1));

  /* Likewise (subreg:QI (lshiftrt:SI (zero_extend:SI (x:QI)) C), 0) into
     to (lshiftrt:QI (x:QI) C), where C is a suitable small constant and
     the outer subreg is effectively a truncation to the original mode.  */
  if ((GET_CODE (op) == LSHIFTRT
       || GET_CODE (op) == ASHIFTRT)
      && SCALAR_INT_MODE_P (outermode)
      && GET_MODE_BITSIZE (outermode) < GET_MODE_BITSIZE (innermode)
      && GET_CODE (XEXP (op, 1)) == CONST_INT
      && GET_CODE (XEXP (op, 0)) == ZERO_EXTEND
      && GET_MODE (XEXP (XEXP (op, 0), 0)) == outermode
      && INTVAL (XEXP (op, 1)) < GET_MODE_BITSIZE (outermode)
      && subreg_lsb_1 (outermode, innermode, byte) == 0)
    return simplify_gen_binary (LSHIFTRT, outermode,
				XEXP (XEXP (op, 0), 0), XEXP (op, 1));

  /* Likewise (subreg:QI (ashift:SI (zero_extend:SI (x:QI)) C), 0) into
     to (ashift:QI (x:QI) C), where C is a suitable small constant and
     the outer subreg is effectively a truncation to the original mode.  */
  if (GET_CODE (op) == ASHIFT
      && SCALAR_INT_MODE_P (outermode)
      && GET_MODE_BITSIZE (outermode) < GET_MODE_BITSIZE (innermode)
      && GET_CODE (XEXP (op, 1)) == CONST_INT
      && (GET_CODE (XEXP (op, 0)) == ZERO_EXTEND
	  || GET_CODE (XEXP (op, 0)) == SIGN_EXTEND)
      && GET_MODE (XEXP (XEXP (op, 0), 0)) == outermode
      && INTVAL (XEXP (op, 1)) < GET_MODE_BITSIZE (outermode)
      && subreg_lsb_1 (outermode, innermode, byte) == 0)
    return simplify_gen_binary (ASHIFT, outermode,
				XEXP (XEXP (op, 0), 0), XEXP (op, 1));

  return NULL_RTX;
}

/* Make a SUBREG operation or equivalent if it folds.  */

rtx
simplify_gen_subreg (enum machine_mode outermode, rtx op,
		     enum machine_mode innermode, unsigned int byte)
{
  rtx newx;

  newx = simplify_subreg (outermode, op, innermode, byte);
  if (newx)
    return newx;

  if (GET_CODE (op) == SUBREG
      || GET_CODE (op) == CONCAT
      || GET_MODE (op) == VOIDmode)
    return NULL_RTX;

  if (validate_subreg (outermode, innermode, op, byte))
    return gen_rtx_SUBREG (outermode, op, byte);

  return NULL_RTX;
}

/* Simplify X, an rtx expression.

   Return the simplified expression or NULL if no simplifications
   were possible.

   This is the preferred entry point into the simplification routines;
   however, we still allow passes to call the more specific routines.

   Right now GCC has three (yes, three) major bodies of RTL simplification
   code that need to be unified.

	1. fold_rtx in cse.c.  This code uses various CSE specific
	   information to aid in RTL simplification.

	2. simplify_rtx in combine.c.  Similar to fold_rtx, except that
	   it uses combine specific information to aid in RTL
	   simplification.

	3. The routines in this file.


   Long term we want to only have one body of simplification code; to
   get to that state I recommend the following steps:

	1. Pour over fold_rtx & simplify_rtx and move any simplifications
	   which are not pass dependent state into these routines.

	2. As code is moved by #1, change fold_rtx & simplify_rtx to
	   use this routine whenever possible.

	3. Allow for pass dependent state to be provided to these
	   routines and add simplifications based on the pass dependent
	   state.  Remove code from cse.c & combine.c that becomes
	   redundant/dead.

    It will take time, but ultimately the compiler will be easier to
    maintain and improve.  It's totally silly that when we add a
    simplification that it needs to be added to 4 places (3 for RTL
    simplification and 1 for tree simplification.  */

rtx
simplify_rtx (rtx x)
{
  enum rtx_code code = GET_CODE (x);
  enum machine_mode mode = GET_MODE (x);

  switch (GET_RTX_CLASS (code))
    {
    case RTX_UNARY:
      return simplify_unary_operation (code, mode,
				       XEXP (x, 0), GET_MODE (XEXP (x, 0)));
    case RTX_COMM_ARITH:
      if (swap_commutative_operands_p (XEXP (x, 0), XEXP (x, 1)))
	return simplify_gen_binary (code, mode, XEXP (x, 1), XEXP (x, 0));

      /* Fall through....  */

    case RTX_BIN_ARITH:
      return simplify_binary_operation (code, mode, XEXP (x, 0), XEXP (x, 1));

    case RTX_TERNARY:
    case RTX_BITFIELD_OPS:
      return simplify_ternary_operation (code, mode, GET_MODE (XEXP (x, 0)),
					 XEXP (x, 0), XEXP (x, 1),
					 XEXP (x, 2));

    case RTX_COMPARE:
    case RTX_COMM_COMPARE:
      return simplify_relational_operation (code, mode,
                                            ((GET_MODE (XEXP (x, 0))
                                             != VOIDmode)
                                            ? GET_MODE (XEXP (x, 0))
                                            : GET_MODE (XEXP (x, 1))),
                                            XEXP (x, 0),
                                            XEXP (x, 1));

    case RTX_EXTRA:
      if (code == SUBREG)
	return simplify_gen_subreg (mode, SUBREG_REG (x),
				    GET_MODE (SUBREG_REG (x)),
				    SUBREG_BYTE (x));
      break;

    case RTX_OBJ:
      if (code == LO_SUM)
	{
	  /* Convert (lo_sum (high FOO) FOO) to FOO.  */
	  if (GET_CODE (XEXP (x, 0)) == HIGH
	      && rtx_equal_p (XEXP (XEXP (x, 0), 0), XEXP (x, 1)))
	  return XEXP (x, 1);
	}
      break;

    default:
      break;
    }
  return NULL;
}

