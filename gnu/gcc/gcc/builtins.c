/* Expand builtin functions.
   Copyright (C) 1988, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999,
   2000, 2001, 2002, 2003, 2004, 2005, 2006 Free Software Foundation, Inc.

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
#include "machmode.h"
#include "real.h"
#include "rtl.h"
#include "tree.h"
#include "tree-gimple.h"
#include "flags.h"
#include "regs.h"
#include "hard-reg-set.h"
#include "except.h"
#include "function.h"
#include "insn-config.h"
#include "expr.h"
#include "optabs.h"
#include "libfuncs.h"
#include "recog.h"
#include "output.h"
#include "typeclass.h"
#include "toplev.h"
#include "predict.h"
#include "tm_p.h"
#include "target.h"
#include "langhooks.h"
#include "basic-block.h"
#include "tree-mudflap.h"

#ifndef PAD_VARARGS_DOWN
#define PAD_VARARGS_DOWN BYTES_BIG_ENDIAN
#endif

/* Define the names of the builtin function types and codes.  */
const char *const built_in_class_names[4]
  = {"NOT_BUILT_IN", "BUILT_IN_FRONTEND", "BUILT_IN_MD", "BUILT_IN_NORMAL"};

#define DEF_BUILTIN(X, N, C, T, LT, B, F, NA, AT, IM, COND) #X,
const char * built_in_names[(int) END_BUILTINS] =
{
#include "builtins.def"
};
#undef DEF_BUILTIN

/* Setup an array of _DECL trees, make sure each element is
   initialized to NULL_TREE.  */
tree built_in_decls[(int) END_BUILTINS];
/* Declarations used when constructing the builtin implicitly in the compiler.
   It may be NULL_TREE when this is invalid (for instance runtime is not
   required to implement the function call in all cases).  */
tree implicit_built_in_decls[(int) END_BUILTINS];

static int get_pointer_alignment (tree, unsigned int);
static const char *c_getstr (tree);
static rtx c_readstr (const char *, enum machine_mode);
static int target_char_cast (tree, char *);
static rtx get_memory_rtx (tree, tree);
static int apply_args_size (void);
static int apply_result_size (void);
#if defined (HAVE_untyped_call) || defined (HAVE_untyped_return)
static rtx result_vector (int, rtx);
#endif
static void expand_builtin_update_setjmp_buf (rtx);
static void expand_builtin_prefetch (tree);
static rtx expand_builtin_apply_args (void);
static rtx expand_builtin_apply_args_1 (void);
static rtx expand_builtin_apply (rtx, rtx, rtx);
static void expand_builtin_return (rtx);
static enum type_class type_to_class (tree);
static rtx expand_builtin_classify_type (tree);
static void expand_errno_check (tree, rtx);
static rtx expand_builtin_mathfn (tree, rtx, rtx);
static rtx expand_builtin_mathfn_2 (tree, rtx, rtx);
static rtx expand_builtin_mathfn_3 (tree, rtx, rtx);
static rtx expand_builtin_sincos (tree);
static rtx expand_builtin_int_roundingfn (tree, rtx, rtx);
static rtx expand_builtin_args_info (tree);
static rtx expand_builtin_next_arg (void);
static rtx expand_builtin_va_start (tree);
static rtx expand_builtin_va_end (tree);
static rtx expand_builtin_va_copy (tree);
static rtx expand_builtin_memcmp (tree, tree, rtx, enum machine_mode);
static rtx expand_builtin_strcmp (tree, rtx, enum machine_mode);
static rtx expand_builtin_strncmp (tree, rtx, enum machine_mode);
static rtx builtin_memcpy_read_str (void *, HOST_WIDE_INT, enum machine_mode);
static rtx expand_builtin_strcat (tree, tree, rtx, enum machine_mode);
static rtx expand_builtin_strncat (tree, rtx, enum machine_mode);
static rtx expand_builtin_strspn (tree, rtx, enum machine_mode);
static rtx expand_builtin_strcspn (tree, rtx, enum machine_mode);
static rtx expand_builtin_memcpy (tree, rtx, enum machine_mode);
static rtx expand_builtin_mempcpy (tree, tree, rtx, enum machine_mode, int);
static rtx expand_builtin_memmove (tree, tree, rtx, enum machine_mode, tree);
static rtx expand_builtin_bcopy (tree);
static rtx expand_builtin_strcpy (tree, tree, rtx, enum machine_mode);
static rtx expand_builtin_stpcpy (tree, rtx, enum machine_mode);
static rtx builtin_strncpy_read_str (void *, HOST_WIDE_INT, enum machine_mode);
static rtx expand_builtin_strncpy (tree, rtx, enum machine_mode);
static rtx builtin_memset_read_str (void *, HOST_WIDE_INT, enum machine_mode);
static rtx builtin_memset_gen_str (void *, HOST_WIDE_INT, enum machine_mode);
static rtx expand_builtin_memset (tree, rtx, enum machine_mode, tree);
static rtx expand_builtin_bzero (tree);
static rtx expand_builtin_strlen (tree, rtx, enum machine_mode);
static rtx expand_builtin_strstr (tree, tree, rtx, enum machine_mode);
static rtx expand_builtin_strpbrk (tree, tree, rtx, enum machine_mode);
static rtx expand_builtin_strchr (tree, tree, rtx, enum machine_mode);
static rtx expand_builtin_strrchr (tree, tree, rtx, enum machine_mode);
static rtx expand_builtin_alloca (tree, rtx);
static rtx expand_builtin_unop (enum machine_mode, tree, rtx, rtx, optab);
static rtx expand_builtin_frame_address (tree, tree);
static rtx expand_builtin_fputs (tree, rtx, bool);
static rtx expand_builtin_printf (tree, rtx, enum machine_mode, bool);
static rtx expand_builtin_fprintf (tree, rtx, enum machine_mode, bool);
static rtx expand_builtin_sprintf (tree, rtx, enum machine_mode);
static tree stabilize_va_list (tree, int);
static rtx expand_builtin_expect (tree, rtx);
static tree fold_builtin_constant_p (tree);
static tree fold_builtin_classify_type (tree);
static tree fold_builtin_strlen (tree);
static tree fold_builtin_inf (tree, int);
static tree fold_builtin_nan (tree, tree, int);
static int validate_arglist (tree, ...);
static bool integer_valued_real_p (tree);
static tree fold_trunc_transparent_mathfn (tree, tree);
static bool readonly_data_expr (tree);
static rtx expand_builtin_fabs (tree, rtx, rtx);
static rtx expand_builtin_signbit (tree, rtx);
static tree fold_builtin_sqrt (tree, tree);
static tree fold_builtin_cbrt (tree, tree);
static tree fold_builtin_pow (tree, tree, tree);
static tree fold_builtin_powi (tree, tree, tree);
static tree fold_builtin_sin (tree);
static tree fold_builtin_cos (tree, tree, tree);
static tree fold_builtin_tan (tree);
static tree fold_builtin_atan (tree, tree);
static tree fold_builtin_trunc (tree, tree);
static tree fold_builtin_floor (tree, tree);
static tree fold_builtin_ceil (tree, tree);
static tree fold_builtin_round (tree, tree);
static tree fold_builtin_int_roundingfn (tree, tree);
static tree fold_builtin_bitop (tree, tree);
static tree fold_builtin_memory_op (tree, tree, bool, int);
static tree fold_builtin_strchr (tree, tree);
static tree fold_builtin_memcmp (tree);
static tree fold_builtin_strcmp (tree);
static tree fold_builtin_strncmp (tree);
static tree fold_builtin_signbit (tree, tree);
static tree fold_builtin_copysign (tree, tree, tree);
static tree fold_builtin_isascii (tree);
static tree fold_builtin_toascii (tree);
static tree fold_builtin_isdigit (tree);
static tree fold_builtin_fabs (tree, tree);
static tree fold_builtin_abs (tree, tree);
static tree fold_builtin_unordered_cmp (tree, tree, enum tree_code,
					enum tree_code);
static tree fold_builtin_1 (tree, tree, bool);

static tree fold_builtin_strpbrk (tree, tree);
static tree fold_builtin_strstr (tree, tree);
static tree fold_builtin_strrchr (tree, tree);
static tree fold_builtin_strcat (tree);
static tree fold_builtin_strncat (tree);
static tree fold_builtin_strspn (tree);
static tree fold_builtin_strcspn (tree);
static tree fold_builtin_sprintf (tree, int);

static rtx expand_builtin_object_size (tree);
static rtx expand_builtin_memory_chk (tree, rtx, enum machine_mode,
				      enum built_in_function);
static void maybe_emit_chk_warning (tree, enum built_in_function);
static void maybe_emit_sprintf_chk_warning (tree, enum built_in_function);
static tree fold_builtin_object_size (tree);
static tree fold_builtin_strcat_chk (tree, tree);
static tree fold_builtin_strncat_chk (tree, tree);
static tree fold_builtin_sprintf_chk (tree, enum built_in_function);
static tree fold_builtin_printf (tree, tree, bool, enum built_in_function);
static tree fold_builtin_fprintf (tree, tree, bool, enum built_in_function);
static bool init_target_chars (void);

static unsigned HOST_WIDE_INT target_newline;
static unsigned HOST_WIDE_INT target_percent;
static unsigned HOST_WIDE_INT target_c;
static unsigned HOST_WIDE_INT target_s;
static char target_percent_c[3];
static char target_percent_s[3];
static char target_percent_s_newline[4];

/* Return true if NODE should be considered for inline expansion regardless
   of the optimization level.  This means whenever a function is invoked with
   its "internal" name, which normally contains the prefix "__builtin".  */

static bool called_as_built_in (tree node)
{
  const char *name = IDENTIFIER_POINTER (DECL_NAME (node));
  if (strncmp (name, "__builtin_", 10) == 0)
    return true;
  if (strncmp (name, "__sync_", 7) == 0)
    return true;
  return false;
}

/* Return the alignment in bits of EXP, a pointer valued expression.
   But don't return more than MAX_ALIGN no matter what.
   The alignment returned is, by default, the alignment of the thing that
   EXP points to.  If it is not a POINTER_TYPE, 0 is returned.

   Otherwise, look at the expression to see if we can do better, i.e., if the
   expression is actually pointing at an object whose alignment is tighter.  */

static int
get_pointer_alignment (tree exp, unsigned int max_align)
{
  unsigned int align, inner;

  /* We rely on TER to compute accurate alignment information.  */
  if (!(optimize && flag_tree_ter))
    return 0;

  if (!POINTER_TYPE_P (TREE_TYPE (exp)))
    return 0;

  align = TYPE_ALIGN (TREE_TYPE (TREE_TYPE (exp)));
  align = MIN (align, max_align);

  while (1)
    {
      switch (TREE_CODE (exp))
	{
	case NOP_EXPR:
	case CONVERT_EXPR:
	case NON_LVALUE_EXPR:
	  exp = TREE_OPERAND (exp, 0);
	  if (! POINTER_TYPE_P (TREE_TYPE (exp)))
	    return align;

	  inner = TYPE_ALIGN (TREE_TYPE (TREE_TYPE (exp)));
	  align = MIN (inner, max_align);
	  break;

	case PLUS_EXPR:
	  /* If sum of pointer + int, restrict our maximum alignment to that
	     imposed by the integer.  If not, we can't do any better than
	     ALIGN.  */
	  if (! host_integerp (TREE_OPERAND (exp, 1), 1))
	    return align;

	  while (((tree_low_cst (TREE_OPERAND (exp, 1), 1))
		  & (max_align / BITS_PER_UNIT - 1))
		 != 0)
	    max_align >>= 1;

	  exp = TREE_OPERAND (exp, 0);
	  break;

	case ADDR_EXPR:
	  /* See what we are pointing at and look at its alignment.  */
	  exp = TREE_OPERAND (exp, 0);
	  inner = max_align;
	  if (handled_component_p (exp))
	    {
	      HOST_WIDE_INT bitsize, bitpos;
	      tree offset;
	      enum machine_mode mode; 
	      int unsignedp, volatilep;

	      exp = get_inner_reference (exp, &bitsize, &bitpos, &offset,
					 &mode, &unsignedp, &volatilep, true);
	      if (bitpos)
		inner = MIN (inner, (unsigned) (bitpos & -bitpos));
	      if (offset && TREE_CODE (offset) == PLUS_EXPR
		  && host_integerp (TREE_OPERAND (offset, 1), 1))
	        {
		  /* Any overflow in calculating offset_bits won't change
		     the alignment.  */
		  unsigned offset_bits
		    = ((unsigned) tree_low_cst (TREE_OPERAND (offset, 1), 1)
		       * BITS_PER_UNIT);

		  if (offset_bits)
		    inner = MIN (inner, (offset_bits & -offset_bits));
		  offset = TREE_OPERAND (offset, 0);
		}
	      if (offset && TREE_CODE (offset) == MULT_EXPR
		  && host_integerp (TREE_OPERAND (offset, 1), 1))
	        {
		  /* Any overflow in calculating offset_factor won't change
		     the alignment.  */
		  unsigned offset_factor
		    = ((unsigned) tree_low_cst (TREE_OPERAND (offset, 1), 1)
		       * BITS_PER_UNIT);

		  if (offset_factor)
		    inner = MIN (inner, (offset_factor & -offset_factor));
		}
	      else if (offset)
		inner = MIN (inner, BITS_PER_UNIT);
	    }
	  if (TREE_CODE (exp) == FUNCTION_DECL)
	    align = FUNCTION_BOUNDARY;
	  else if (DECL_P (exp))
	    align = MIN (inner, DECL_ALIGN (exp));
#ifdef CONSTANT_ALIGNMENT
	  else if (CONSTANT_CLASS_P (exp))
	    align = MIN (inner, (unsigned)CONSTANT_ALIGNMENT (exp, align));
#endif
	  else if (TREE_CODE (exp) == VIEW_CONVERT_EXPR
		   || TREE_CODE (exp) == INDIRECT_REF)
	    align = MIN (TYPE_ALIGN (TREE_TYPE (exp)), inner);
	  else
	    align = MIN (align, inner);
	  return MIN (align, max_align);

	default:
	  return align;
	}
    }
}

/* Compute the length of a C string.  TREE_STRING_LENGTH is not the right
   way, because it could contain a zero byte in the middle.
   TREE_STRING_LENGTH is the size of the character array, not the string.

   ONLY_VALUE should be nonzero if the result is not going to be emitted
   into the instruction stream and zero if it is going to be expanded.
   E.g. with i++ ? "foo" : "bar", if ONLY_VALUE is nonzero, constant 3
   is returned, otherwise NULL, since
   len = c_strlen (src, 1); if (len) expand_expr (len, ...); would not
   evaluate the side-effects.

   The value returned is of type `ssizetype'.

   Unfortunately, string_constant can't access the values of const char
   arrays with initializers, so neither can we do so here.  */

tree
c_strlen (tree src, int only_value)
{
  tree offset_node;
  HOST_WIDE_INT offset;
  int max;
  const char *ptr;

  STRIP_NOPS (src);
  if (TREE_CODE (src) == COND_EXPR
      && (only_value || !TREE_SIDE_EFFECTS (TREE_OPERAND (src, 0))))
    {
      tree len1, len2;

      len1 = c_strlen (TREE_OPERAND (src, 1), only_value);
      len2 = c_strlen (TREE_OPERAND (src, 2), only_value);
      if (tree_int_cst_equal (len1, len2))
	return len1;
    }

  if (TREE_CODE (src) == COMPOUND_EXPR
      && (only_value || !TREE_SIDE_EFFECTS (TREE_OPERAND (src, 0))))
    return c_strlen (TREE_OPERAND (src, 1), only_value);

  src = string_constant (src, &offset_node);
  if (src == 0)
    return 0;

  max = TREE_STRING_LENGTH (src) - 1;
  ptr = TREE_STRING_POINTER (src);

  if (offset_node && TREE_CODE (offset_node) != INTEGER_CST)
    {
      /* If the string has an internal zero byte (e.g., "foo\0bar"), we can't
	 compute the offset to the following null if we don't know where to
	 start searching for it.  */
      int i;

      for (i = 0; i < max; i++)
	if (ptr[i] == 0)
	  return 0;

      /* We don't know the starting offset, but we do know that the string
	 has no internal zero bytes.  We can assume that the offset falls
	 within the bounds of the string; otherwise, the programmer deserves
	 what he gets.  Subtract the offset from the length of the string,
	 and return that.  This would perhaps not be valid if we were dealing
	 with named arrays in addition to literal string constants.  */

      return size_diffop (size_int (max), offset_node);
    }

  /* We have a known offset into the string.  Start searching there for
     a null character if we can represent it as a single HOST_WIDE_INT.  */
  if (offset_node == 0)
    offset = 0;
  else if (! host_integerp (offset_node, 0))
    offset = -1;
  else
    offset = tree_low_cst (offset_node, 0);

  /* If the offset is known to be out of bounds, warn, and call strlen at
     runtime.  */
  if (offset < 0 || offset > max)
    {
      warning (0, "offset outside bounds of constant string");
      return 0;
    }

  /* Use strlen to search for the first zero byte.  Since any strings
     constructed with build_string will have nulls appended, we win even
     if we get handed something like (char[4])"abcd".

     Since OFFSET is our starting index into the string, no further
     calculation is needed.  */
  return ssize_int (strlen (ptr + offset));
}

/* Return a char pointer for a C string if it is a string constant
   or sum of string constant and integer constant.  */

static const char *
c_getstr (tree src)
{
  tree offset_node;

  src = string_constant (src, &offset_node);
  if (src == 0)
    return 0;

  if (offset_node == 0)
    return TREE_STRING_POINTER (src);
  else if (!host_integerp (offset_node, 1)
	   || compare_tree_int (offset_node, TREE_STRING_LENGTH (src) - 1) > 0)
    return 0;

  return TREE_STRING_POINTER (src) + tree_low_cst (offset_node, 1);
}

/* Return a CONST_INT or CONST_DOUBLE corresponding to target reading
   GET_MODE_BITSIZE (MODE) bits from string constant STR.  */

static rtx
c_readstr (const char *str, enum machine_mode mode)
{
  HOST_WIDE_INT c[2];
  HOST_WIDE_INT ch;
  unsigned int i, j;

  gcc_assert (GET_MODE_CLASS (mode) == MODE_INT);

  c[0] = 0;
  c[1] = 0;
  ch = 1;
  for (i = 0; i < GET_MODE_SIZE (mode); i++)
    {
      j = i;
      if (WORDS_BIG_ENDIAN)
	j = GET_MODE_SIZE (mode) - i - 1;
      if (BYTES_BIG_ENDIAN != WORDS_BIG_ENDIAN
	  && GET_MODE_SIZE (mode) > UNITS_PER_WORD)
	j = j + UNITS_PER_WORD - 2 * (j % UNITS_PER_WORD) - 1;
      j *= BITS_PER_UNIT;
      gcc_assert (j <= 2 * HOST_BITS_PER_WIDE_INT);

      if (ch)
	ch = (unsigned char) str[i];
      c[j / HOST_BITS_PER_WIDE_INT] |= ch << (j % HOST_BITS_PER_WIDE_INT);
    }
  return immed_double_const (c[0], c[1], mode);
}

/* Cast a target constant CST to target CHAR and if that value fits into
   host char type, return zero and put that value into variable pointed to by
   P.  */

static int
target_char_cast (tree cst, char *p)
{
  unsigned HOST_WIDE_INT val, hostval;

  if (!host_integerp (cst, 1)
      || CHAR_TYPE_SIZE > HOST_BITS_PER_WIDE_INT)
    return 1;

  val = tree_low_cst (cst, 1);
  if (CHAR_TYPE_SIZE < HOST_BITS_PER_WIDE_INT)
    val &= (((unsigned HOST_WIDE_INT) 1) << CHAR_TYPE_SIZE) - 1;

  hostval = val;
  if (HOST_BITS_PER_CHAR < HOST_BITS_PER_WIDE_INT)
    hostval &= (((unsigned HOST_WIDE_INT) 1) << HOST_BITS_PER_CHAR) - 1;

  if (val != hostval)
    return 1;

  *p = hostval;
  return 0;
}

/* Similar to save_expr, but assumes that arbitrary code is not executed
   in between the multiple evaluations.  In particular, we assume that a
   non-addressable local variable will not be modified.  */

static tree
builtin_save_expr (tree exp)
{
  if (TREE_ADDRESSABLE (exp) == 0
      && (TREE_CODE (exp) == PARM_DECL
	  || (TREE_CODE (exp) == VAR_DECL && !TREE_STATIC (exp))))
    return exp;

  return save_expr (exp);
}

/* Given TEM, a pointer to a stack frame, follow the dynamic chain COUNT
   times to get the address of either a higher stack frame, or a return
   address located within it (depending on FNDECL_CODE).  */

static rtx
expand_builtin_return_addr (enum built_in_function fndecl_code, int count)
{
  int i;

#ifdef INITIAL_FRAME_ADDRESS_RTX
  rtx tem = INITIAL_FRAME_ADDRESS_RTX;
#else
  rtx tem;

  /* For a zero count with __builtin_return_address, we don't care what
     frame address we return, because target-specific definitions will
     override us.  Therefore frame pointer elimination is OK, and using
     the soft frame pointer is OK.

     For a non-zero count, or a zero count with __builtin_frame_address,
     we require a stable offset from the current frame pointer to the
     previous one, so we must use the hard frame pointer, and
     we must disable frame pointer elimination.  */
  if (count == 0 && fndecl_code == BUILT_IN_RETURN_ADDRESS)
    tem = frame_pointer_rtx;
  else
    {
      tem = hard_frame_pointer_rtx;

      /* Tell reload not to eliminate the frame pointer.  */
      current_function_accesses_prior_frames = 1;
    }
#endif

  /* Some machines need special handling before we can access
     arbitrary frames.  For example, on the SPARC, we must first flush
     all register windows to the stack.  */
#ifdef SETUP_FRAME_ADDRESSES
  if (count > 0)
    SETUP_FRAME_ADDRESSES ();
#endif

  /* On the SPARC, the return address is not in the frame, it is in a
     register.  There is no way to access it off of the current frame
     pointer, but it can be accessed off the previous frame pointer by
     reading the value from the register window save area.  */
#ifdef RETURN_ADDR_IN_PREVIOUS_FRAME
  if (fndecl_code == BUILT_IN_RETURN_ADDRESS)
    count--;
#endif

  /* Scan back COUNT frames to the specified frame.  */
  for (i = 0; i < count; i++)
    {
      /* Assume the dynamic chain pointer is in the word that the
	 frame address points to, unless otherwise specified.  */
#ifdef DYNAMIC_CHAIN_ADDRESS
      tem = DYNAMIC_CHAIN_ADDRESS (tem);
#endif
      tem = memory_address (Pmode, tem);
      tem = gen_frame_mem (Pmode, tem);
      tem = copy_to_reg (tem);
    }

  /* For __builtin_frame_address, return what we've got.  But, on
     the SPARC for example, we may have to add a bias.  */
  if (fndecl_code == BUILT_IN_FRAME_ADDRESS)
#ifdef FRAME_ADDR_RTX
    return FRAME_ADDR_RTX (tem);
#else
    return tem;
#endif

  /* For __builtin_return_address, get the return address from that frame.  */
#ifdef RETURN_ADDR_RTX
  tem = RETURN_ADDR_RTX (count, tem);
#else
  tem = memory_address (Pmode,
			plus_constant (tem, GET_MODE_SIZE (Pmode)));
  tem = gen_frame_mem (Pmode, tem);
#endif
  return tem;
}

/* Alias set used for setjmp buffer.  */
static HOST_WIDE_INT setjmp_alias_set = -1;

/* Construct the leading half of a __builtin_setjmp call.  Control will
   return to RECEIVER_LABEL.  This is also called directly by the SJLJ
   exception handling code.  */

void
expand_builtin_setjmp_setup (rtx buf_addr, rtx receiver_label)
{
  enum machine_mode sa_mode = STACK_SAVEAREA_MODE (SAVE_NONLOCAL);
  rtx stack_save;
  rtx mem;

  if (setjmp_alias_set == -1)
    setjmp_alias_set = new_alias_set ();

  buf_addr = convert_memory_address (Pmode, buf_addr);

  buf_addr = force_reg (Pmode, force_operand (buf_addr, NULL_RTX));

  /* We store the frame pointer and the address of receiver_label in
     the buffer and use the rest of it for the stack save area, which
     is machine-dependent.  */

  mem = gen_rtx_MEM (Pmode, buf_addr);
  set_mem_alias_set (mem, setjmp_alias_set);
  emit_move_insn (mem, targetm.builtin_setjmp_frame_value ());

  mem = gen_rtx_MEM (Pmode, plus_constant (buf_addr, GET_MODE_SIZE (Pmode))),
  set_mem_alias_set (mem, setjmp_alias_set);

  emit_move_insn (validize_mem (mem),
		  force_reg (Pmode, gen_rtx_LABEL_REF (Pmode, receiver_label)));

  stack_save = gen_rtx_MEM (sa_mode,
			    plus_constant (buf_addr,
					   2 * GET_MODE_SIZE (Pmode)));
  set_mem_alias_set (stack_save, setjmp_alias_set);
  emit_stack_save (SAVE_NONLOCAL, &stack_save, NULL_RTX);

  /* If there is further processing to do, do it.  */
#ifdef HAVE_builtin_setjmp_setup
  if (HAVE_builtin_setjmp_setup)
    emit_insn (gen_builtin_setjmp_setup (buf_addr));
#endif

  /* Tell optimize_save_area_alloca that extra work is going to
     need to go on during alloca.  */
  current_function_calls_setjmp = 1;

  /* Set this so all the registers get saved in our frame; we need to be
     able to copy the saved values for any registers from frames we unwind.  */
  current_function_has_nonlocal_label = 1;
}

/* Construct the trailing part of a __builtin_setjmp call.  This is
   also called directly by the SJLJ exception handling code.  */

void
expand_builtin_setjmp_receiver (rtx receiver_label ATTRIBUTE_UNUSED)
{
  /* Clobber the FP when we get here, so we have to make sure it's
     marked as used by this function.  */
  emit_insn (gen_rtx_USE (VOIDmode, hard_frame_pointer_rtx));

  /* Mark the static chain as clobbered here so life information
     doesn't get messed up for it.  */
  emit_insn (gen_rtx_CLOBBER (VOIDmode, static_chain_rtx));

  /* Now put in the code to restore the frame pointer, and argument
     pointer, if needed.  */
#ifdef HAVE_nonlocal_goto
  if (! HAVE_nonlocal_goto)
#endif
    {
      emit_move_insn (virtual_stack_vars_rtx, hard_frame_pointer_rtx);
      /* This might change the hard frame pointer in ways that aren't
	 apparent to early optimization passes, so force a clobber.  */
      emit_insn (gen_rtx_CLOBBER (VOIDmode, hard_frame_pointer_rtx));
    }

#if ARG_POINTER_REGNUM != HARD_FRAME_POINTER_REGNUM
  if (fixed_regs[ARG_POINTER_REGNUM])
    {
#ifdef ELIMINABLE_REGS
      size_t i;
      static const struct elims {const int from, to;} elim_regs[] = ELIMINABLE_REGS;

      for (i = 0; i < ARRAY_SIZE (elim_regs); i++)
	if (elim_regs[i].from == ARG_POINTER_REGNUM
	    && elim_regs[i].to == HARD_FRAME_POINTER_REGNUM)
	  break;

      if (i == ARRAY_SIZE (elim_regs))
#endif
	{
	  /* Now restore our arg pointer from the address at which it
	     was saved in our stack frame.  */
	  emit_move_insn (virtual_incoming_args_rtx,
			  copy_to_reg (get_arg_pointer_save_area (cfun)));
	}
    }
#endif

#ifdef HAVE_builtin_setjmp_receiver
  if (HAVE_builtin_setjmp_receiver)
    emit_insn (gen_builtin_setjmp_receiver (receiver_label));
  else
#endif
#ifdef HAVE_nonlocal_goto_receiver
    if (HAVE_nonlocal_goto_receiver)
      emit_insn (gen_nonlocal_goto_receiver ());
    else
#endif
      { /* Nothing */ }

  /* @@@ This is a kludge.  Not all machine descriptions define a blockage
     insn, but we must not allow the code we just generated to be reordered
     by scheduling.  Specifically, the update of the frame pointer must
     happen immediately, not later.  So emit an ASM_INPUT to act as blockage
     insn.  */
  emit_insn (gen_rtx_ASM_INPUT (VOIDmode, ""));
}

/* __builtin_longjmp is passed a pointer to an array of five words (not
   all will be used on all machines).  It operates similarly to the C
   library function of the same name, but is more efficient.  Much of
   the code below is copied from the handling of non-local gotos.  */

static void
expand_builtin_longjmp (rtx buf_addr, rtx value)
{
  rtx fp, lab, stack, insn, last;
  enum machine_mode sa_mode = STACK_SAVEAREA_MODE (SAVE_NONLOCAL);

  if (setjmp_alias_set == -1)
    setjmp_alias_set = new_alias_set ();

  buf_addr = convert_memory_address (Pmode, buf_addr);

  buf_addr = force_reg (Pmode, buf_addr);

  /* We used to store value in static_chain_rtx, but that fails if pointers
     are smaller than integers.  We instead require that the user must pass
     a second argument of 1, because that is what builtin_setjmp will
     return.  This also makes EH slightly more efficient, since we are no
     longer copying around a value that we don't care about.  */
  gcc_assert (value == const1_rtx);

  last = get_last_insn ();
#ifdef HAVE_builtin_longjmp
  if (HAVE_builtin_longjmp)
    emit_insn (gen_builtin_longjmp (buf_addr));
  else
#endif
    {
      fp = gen_rtx_MEM (Pmode, buf_addr);
      lab = gen_rtx_MEM (Pmode, plus_constant (buf_addr,
					       GET_MODE_SIZE (Pmode)));

      stack = gen_rtx_MEM (sa_mode, plus_constant (buf_addr,
						   2 * GET_MODE_SIZE (Pmode)));
      set_mem_alias_set (fp, setjmp_alias_set);
      set_mem_alias_set (lab, setjmp_alias_set);
      set_mem_alias_set (stack, setjmp_alias_set);

      /* Pick up FP, label, and SP from the block and jump.  This code is
	 from expand_goto in stmt.c; see there for detailed comments.  */
#ifdef HAVE_nonlocal_goto
      if (HAVE_nonlocal_goto)
	/* We have to pass a value to the nonlocal_goto pattern that will
	   get copied into the static_chain pointer, but it does not matter
	   what that value is, because builtin_setjmp does not use it.  */
	emit_insn (gen_nonlocal_goto (value, lab, stack, fp));
      else
#endif
	{
	  lab = copy_to_reg (lab);

	  emit_insn (gen_rtx_CLOBBER (VOIDmode,
				      gen_rtx_MEM (BLKmode,
						   gen_rtx_SCRATCH (VOIDmode))));
	  emit_insn (gen_rtx_CLOBBER (VOIDmode,
				      gen_rtx_MEM (BLKmode,
						   hard_frame_pointer_rtx)));

	  emit_move_insn (hard_frame_pointer_rtx, fp);
	  emit_stack_restore (SAVE_NONLOCAL, stack, NULL_RTX);

	  emit_insn (gen_rtx_USE (VOIDmode, hard_frame_pointer_rtx));
	  emit_insn (gen_rtx_USE (VOIDmode, stack_pointer_rtx));
	  emit_indirect_jump (lab);
	}
    }

  /* Search backwards and mark the jump insn as a non-local goto.
     Note that this precludes the use of __builtin_longjmp to a
     __builtin_setjmp target in the same function.  However, we've
     already cautioned the user that these functions are for
     internal exception handling use only.  */
  for (insn = get_last_insn (); insn; insn = PREV_INSN (insn))
    {
      gcc_assert (insn != last);

      if (JUMP_P (insn))
	{
	  REG_NOTES (insn) = alloc_EXPR_LIST (REG_NON_LOCAL_GOTO, const0_rtx,
					      REG_NOTES (insn));
	  break;
	}
      else if (CALL_P (insn))
	break;
    }
}

/* Expand a call to __builtin_nonlocal_goto.  We're passed the target label
   and the address of the save area.  */

static rtx
expand_builtin_nonlocal_goto (tree arglist)
{
  tree t_label, t_save_area;
  rtx r_label, r_save_area, r_fp, r_sp, insn;

  if (!validate_arglist (arglist, POINTER_TYPE, POINTER_TYPE, VOID_TYPE))
    return NULL_RTX;

  t_label = TREE_VALUE (arglist);
  arglist = TREE_CHAIN (arglist);
  t_save_area = TREE_VALUE (arglist);

  r_label = expand_normal (t_label);
  r_label = convert_memory_address (Pmode, r_label);
  r_save_area = expand_normal (t_save_area);
  r_save_area = convert_memory_address (Pmode, r_save_area);
  r_fp = gen_rtx_MEM (Pmode, r_save_area);
  r_sp = gen_rtx_MEM (STACK_SAVEAREA_MODE (SAVE_NONLOCAL),
		      plus_constant (r_save_area, GET_MODE_SIZE (Pmode)));

  current_function_has_nonlocal_goto = 1;

#ifdef HAVE_nonlocal_goto
  /* ??? We no longer need to pass the static chain value, afaik.  */
  if (HAVE_nonlocal_goto)
    emit_insn (gen_nonlocal_goto (const0_rtx, r_label, r_sp, r_fp));
  else
#endif
    {
      r_label = copy_to_reg (r_label);

      emit_insn (gen_rtx_CLOBBER (VOIDmode,
				  gen_rtx_MEM (BLKmode,
					       gen_rtx_SCRATCH (VOIDmode))));

      emit_insn (gen_rtx_CLOBBER (VOIDmode,
				  gen_rtx_MEM (BLKmode,
					       hard_frame_pointer_rtx)));

      /* Restore frame pointer for containing function.
	 This sets the actual hard register used for the frame pointer
	 to the location of the function's incoming static chain info.
	 The non-local goto handler will then adjust it to contain the
	 proper value and reload the argument pointer, if needed.  */
      emit_move_insn (hard_frame_pointer_rtx, r_fp);
      emit_stack_restore (SAVE_NONLOCAL, r_sp, NULL_RTX);

      /* USE of hard_frame_pointer_rtx added for consistency;
	 not clear if really needed.  */
      emit_insn (gen_rtx_USE (VOIDmode, hard_frame_pointer_rtx));
      emit_insn (gen_rtx_USE (VOIDmode, stack_pointer_rtx));
      emit_indirect_jump (r_label);
    }

  /* Search backwards to the jump insn and mark it as a
     non-local goto.  */
  for (insn = get_last_insn (); insn; insn = PREV_INSN (insn))
    {
      if (JUMP_P (insn))
	{
	  REG_NOTES (insn) = alloc_EXPR_LIST (REG_NON_LOCAL_GOTO,
					      const0_rtx, REG_NOTES (insn));
	  break;
	}
      else if (CALL_P (insn))
	break;
    }

  return const0_rtx;
}

/* __builtin_update_setjmp_buf is passed a pointer to an array of five words
   (not all will be used on all machines) that was passed to __builtin_setjmp.
   It updates the stack pointer in that block to correspond to the current
   stack pointer.  */

static void
expand_builtin_update_setjmp_buf (rtx buf_addr)
{
  enum machine_mode sa_mode = Pmode;
  rtx stack_save;


#ifdef HAVE_save_stack_nonlocal
  if (HAVE_save_stack_nonlocal)
    sa_mode = insn_data[(int) CODE_FOR_save_stack_nonlocal].operand[0].mode;
#endif
#ifdef STACK_SAVEAREA_MODE
  sa_mode = STACK_SAVEAREA_MODE (SAVE_NONLOCAL);
#endif

  stack_save
    = gen_rtx_MEM (sa_mode,
		   memory_address
		   (sa_mode,
		    plus_constant (buf_addr, 2 * GET_MODE_SIZE (Pmode))));

#ifdef HAVE_setjmp
  if (HAVE_setjmp)
    emit_insn (gen_setjmp ());
#endif

  emit_stack_save (SAVE_NONLOCAL, &stack_save, NULL_RTX);
}

/* Expand a call to __builtin_prefetch.  For a target that does not support
   data prefetch, evaluate the memory address argument in case it has side
   effects.  */

static void
expand_builtin_prefetch (tree arglist)
{
  tree arg0, arg1, arg2;
  rtx op0, op1, op2;

  if (!validate_arglist (arglist, POINTER_TYPE, 0))
    return;

  arg0 = TREE_VALUE (arglist);
  /* Arguments 1 and 2 are optional; argument 1 (read/write) defaults to
     zero (read) and argument 2 (locality) defaults to 3 (high degree of
     locality).  */
  if (TREE_CHAIN (arglist))
    {
      arg1 = TREE_VALUE (TREE_CHAIN (arglist));
      if (TREE_CHAIN (TREE_CHAIN (arglist)))
	arg2 = TREE_VALUE (TREE_CHAIN (TREE_CHAIN (arglist)));
      else
	arg2 = build_int_cst (NULL_TREE, 3);
    }
  else
    {
      arg1 = integer_zero_node;
      arg2 = build_int_cst (NULL_TREE, 3);
    }

  /* Argument 0 is an address.  */
  op0 = expand_expr (arg0, NULL_RTX, Pmode, EXPAND_NORMAL);

  /* Argument 1 (read/write flag) must be a compile-time constant int.  */
  if (TREE_CODE (arg1) != INTEGER_CST)
    {
      error ("second argument to %<__builtin_prefetch%> must be a constant");
      arg1 = integer_zero_node;
    }
  op1 = expand_normal (arg1);
  /* Argument 1 must be either zero or one.  */
  if (INTVAL (op1) != 0 && INTVAL (op1) != 1)
    {
      warning (0, "invalid second argument to %<__builtin_prefetch%>;"
	       " using zero");
      op1 = const0_rtx;
    }

  /* Argument 2 (locality) must be a compile-time constant int.  */
  if (TREE_CODE (arg2) != INTEGER_CST)
    {
      error ("third argument to %<__builtin_prefetch%> must be a constant");
      arg2 = integer_zero_node;
    }
  op2 = expand_normal (arg2);
  /* Argument 2 must be 0, 1, 2, or 3.  */
  if (INTVAL (op2) < 0 || INTVAL (op2) > 3)
    {
      warning (0, "invalid third argument to %<__builtin_prefetch%>; using zero");
      op2 = const0_rtx;
    }

#ifdef HAVE_prefetch
  if (HAVE_prefetch)
    {
      if ((! (*insn_data[(int) CODE_FOR_prefetch].operand[0].predicate)
	     (op0,
	      insn_data[(int) CODE_FOR_prefetch].operand[0].mode))
	  || (GET_MODE (op0) != Pmode))
	{
	  op0 = convert_memory_address (Pmode, op0);
	  op0 = force_reg (Pmode, op0);
	}
      emit_insn (gen_prefetch (op0, op1, op2));
    }
#endif

  /* Don't do anything with direct references to volatile memory, but
     generate code to handle other side effects.  */
  if (!MEM_P (op0) && side_effects_p (op0))
    emit_insn (op0);
}

/* Get a MEM rtx for expression EXP which is the address of an operand
   to be used in a string instruction (cmpstrsi, movmemsi, ..).  LEN is
   the maximum length of the block of memory that might be accessed or
   NULL if unknown.  */

static rtx
get_memory_rtx (tree exp, tree len)
{
  rtx addr = expand_expr (exp, NULL_RTX, ptr_mode, EXPAND_NORMAL);
  rtx mem = gen_rtx_MEM (BLKmode, memory_address (BLKmode, addr));

  /* Get an expression we can use to find the attributes to assign to MEM.
     If it is an ADDR_EXPR, use the operand.  Otherwise, dereference it if
     we can.  First remove any nops.  */
  while ((TREE_CODE (exp) == NOP_EXPR || TREE_CODE (exp) == CONVERT_EXPR
	  || TREE_CODE (exp) == NON_LVALUE_EXPR)
	 && POINTER_TYPE_P (TREE_TYPE (TREE_OPERAND (exp, 0))))
    exp = TREE_OPERAND (exp, 0);

  if (TREE_CODE (exp) == ADDR_EXPR)
    exp = TREE_OPERAND (exp, 0);
  else if (POINTER_TYPE_P (TREE_TYPE (exp)))
    exp = build1 (INDIRECT_REF, TREE_TYPE (TREE_TYPE (exp)), exp);
  else
    exp = NULL;

  /* Honor attributes derived from exp, except for the alias set
     (as builtin stringops may alias with anything) and the size
     (as stringops may access multiple array elements).  */
  if (exp)
    {
      set_mem_attributes (mem, exp, 0);

      /* Allow the string and memory builtins to overflow from one
	 field into another, see http://gcc.gnu.org/PR23561.
	 Thus avoid COMPONENT_REFs in MEM_EXPR unless we know the whole
	 memory accessed by the string or memory builtin will fit
	 within the field.  */
      if (MEM_EXPR (mem) && TREE_CODE (MEM_EXPR (mem)) == COMPONENT_REF)
	{
	  tree mem_expr = MEM_EXPR (mem);
	  HOST_WIDE_INT offset = -1, length = -1;
	  tree inner = exp;

	  while (TREE_CODE (inner) == ARRAY_REF
		 || TREE_CODE (inner) == NOP_EXPR
		 || TREE_CODE (inner) == CONVERT_EXPR
		 || TREE_CODE (inner) == NON_LVALUE_EXPR
		 || TREE_CODE (inner) == VIEW_CONVERT_EXPR
		 || TREE_CODE (inner) == SAVE_EXPR)
	    inner = TREE_OPERAND (inner, 0);

	  gcc_assert (TREE_CODE (inner) == COMPONENT_REF);

	  if (MEM_OFFSET (mem)
	      && GET_CODE (MEM_OFFSET (mem)) == CONST_INT)
	    offset = INTVAL (MEM_OFFSET (mem));

	  if (offset >= 0 && len && host_integerp (len, 0))
	    length = tree_low_cst (len, 0);

	  while (TREE_CODE (inner) == COMPONENT_REF)
	    {
	      tree field = TREE_OPERAND (inner, 1);
	      gcc_assert (! DECL_BIT_FIELD (field));
	      gcc_assert (TREE_CODE (mem_expr) == COMPONENT_REF);
	      gcc_assert (field == TREE_OPERAND (mem_expr, 1));

	      if (length >= 0
		  && TYPE_SIZE_UNIT (TREE_TYPE (inner))
		  && host_integerp (TYPE_SIZE_UNIT (TREE_TYPE (inner)), 0))
		{
		  HOST_WIDE_INT size
		    = tree_low_cst (TYPE_SIZE_UNIT (TREE_TYPE (inner)), 0);
		  /* If we can prove the memory starting at XEXP (mem, 0)
		     and ending at XEXP (mem, 0) + LENGTH will fit into
		     this field, we can keep that COMPONENT_REF in MEM_EXPR.  */
		  if (offset <= size
		      && length <= size
		      && offset + length <= size)
		    break;
		}

	      if (offset >= 0
		  && host_integerp (DECL_FIELD_OFFSET (field), 0))
		offset += tree_low_cst (DECL_FIELD_OFFSET (field), 0)
			  + tree_low_cst (DECL_FIELD_BIT_OFFSET (field), 1)
			    / BITS_PER_UNIT;
	      else
		{
		  offset = -1;
		  length = -1;
		}

	      mem_expr = TREE_OPERAND (mem_expr, 0);
	      inner = TREE_OPERAND (inner, 0);
	    }

	  if (mem_expr == NULL)
	    offset = -1;
	  if (mem_expr != MEM_EXPR (mem))
	    {
	      set_mem_expr (mem, mem_expr);
	      set_mem_offset (mem, offset >= 0 ? GEN_INT (offset) : NULL_RTX);
	    }
	}
      set_mem_alias_set (mem, 0);
      set_mem_size (mem, NULL_RTX);
    }

  return mem;
}

/* Built-in functions to perform an untyped call and return.  */

/* For each register that may be used for calling a function, this
   gives a mode used to copy the register's value.  VOIDmode indicates
   the register is not used for calling a function.  If the machine
   has register windows, this gives only the outbound registers.
   INCOMING_REGNO gives the corresponding inbound register.  */
static enum machine_mode apply_args_mode[FIRST_PSEUDO_REGISTER];

/* For each register that may be used for returning values, this gives
   a mode used to copy the register's value.  VOIDmode indicates the
   register is not used for returning values.  If the machine has
   register windows, this gives only the outbound registers.
   INCOMING_REGNO gives the corresponding inbound register.  */
static enum machine_mode apply_result_mode[FIRST_PSEUDO_REGISTER];

/* For each register that may be used for calling a function, this
   gives the offset of that register into the block returned by
   __builtin_apply_args.  0 indicates that the register is not
   used for calling a function.  */
static int apply_args_reg_offset[FIRST_PSEUDO_REGISTER];

/* Return the size required for the block returned by __builtin_apply_args,
   and initialize apply_args_mode.  */

static int
apply_args_size (void)
{
  static int size = -1;
  int align;
  unsigned int regno;
  enum machine_mode mode;

  /* The values computed by this function never change.  */
  if (size < 0)
    {
      /* The first value is the incoming arg-pointer.  */
      size = GET_MODE_SIZE (Pmode);

      /* The second value is the structure value address unless this is
	 passed as an "invisible" first argument.  */
      if (targetm.calls.struct_value_rtx (cfun ? TREE_TYPE (cfun->decl) : 0, 0))
	size += GET_MODE_SIZE (Pmode);

      for (regno = 0; regno < FIRST_PSEUDO_REGISTER; regno++)
	if (FUNCTION_ARG_REGNO_P (regno))
	  {
	    mode = reg_raw_mode[regno];

	    gcc_assert (mode != VOIDmode);

	    align = GET_MODE_ALIGNMENT (mode) / BITS_PER_UNIT;
	    if (size % align != 0)
	      size = CEIL (size, align) * align;
	    apply_args_reg_offset[regno] = size;
	    size += GET_MODE_SIZE (mode);
	    apply_args_mode[regno] = mode;
	  }
	else
	  {
	    apply_args_mode[regno] = VOIDmode;
	    apply_args_reg_offset[regno] = 0;
	  }
    }
  return size;
}

/* Return the size required for the block returned by __builtin_apply,
   and initialize apply_result_mode.  */

static int
apply_result_size (void)
{
  static int size = -1;
  int align, regno;
  enum machine_mode mode;

  /* The values computed by this function never change.  */
  if (size < 0)
    {
      size = 0;

      for (regno = 0; regno < FIRST_PSEUDO_REGISTER; regno++)
	if (FUNCTION_VALUE_REGNO_P (regno))
	  {
	    mode = reg_raw_mode[regno];

	    gcc_assert (mode != VOIDmode);

	    align = GET_MODE_ALIGNMENT (mode) / BITS_PER_UNIT;
	    if (size % align != 0)
	      size = CEIL (size, align) * align;
	    size += GET_MODE_SIZE (mode);
	    apply_result_mode[regno] = mode;
	  }
	else
	  apply_result_mode[regno] = VOIDmode;

      /* Allow targets that use untyped_call and untyped_return to override
	 the size so that machine-specific information can be stored here.  */
#ifdef APPLY_RESULT_SIZE
      size = APPLY_RESULT_SIZE;
#endif
    }
  return size;
}

#if defined (HAVE_untyped_call) || defined (HAVE_untyped_return)
/* Create a vector describing the result block RESULT.  If SAVEP is true,
   the result block is used to save the values; otherwise it is used to
   restore the values.  */

static rtx
result_vector (int savep, rtx result)
{
  int regno, size, align, nelts;
  enum machine_mode mode;
  rtx reg, mem;
  rtx *savevec = alloca (FIRST_PSEUDO_REGISTER * sizeof (rtx));

  size = nelts = 0;
  for (regno = 0; regno < FIRST_PSEUDO_REGISTER; regno++)
    if ((mode = apply_result_mode[regno]) != VOIDmode)
      {
	align = GET_MODE_ALIGNMENT (mode) / BITS_PER_UNIT;
	if (size % align != 0)
	  size = CEIL (size, align) * align;
	reg = gen_rtx_REG (mode, savep ? regno : INCOMING_REGNO (regno));
	mem = adjust_address (result, mode, size);
	savevec[nelts++] = (savep
			    ? gen_rtx_SET (VOIDmode, mem, reg)
			    : gen_rtx_SET (VOIDmode, reg, mem));
	size += GET_MODE_SIZE (mode);
      }
  return gen_rtx_PARALLEL (VOIDmode, gen_rtvec_v (nelts, savevec));
}
#endif /* HAVE_untyped_call or HAVE_untyped_return */

/* Save the state required to perform an untyped call with the same
   arguments as were passed to the current function.  */

static rtx
expand_builtin_apply_args_1 (void)
{
  rtx registers, tem;
  int size, align, regno;
  enum machine_mode mode;
  rtx struct_incoming_value = targetm.calls.struct_value_rtx (cfun ? TREE_TYPE (cfun->decl) : 0, 1);

  /* Create a block where the arg-pointer, structure value address,
     and argument registers can be saved.  */
  registers = assign_stack_local (BLKmode, apply_args_size (), -1);

  /* Walk past the arg-pointer and structure value address.  */
  size = GET_MODE_SIZE (Pmode);
  if (targetm.calls.struct_value_rtx (cfun ? TREE_TYPE (cfun->decl) : 0, 0))
    size += GET_MODE_SIZE (Pmode);

  /* Save each register used in calling a function to the block.  */
  for (regno = 0; regno < FIRST_PSEUDO_REGISTER; regno++)
    if ((mode = apply_args_mode[regno]) != VOIDmode)
      {
	align = GET_MODE_ALIGNMENT (mode) / BITS_PER_UNIT;
	if (size % align != 0)
	  size = CEIL (size, align) * align;

	tem = gen_rtx_REG (mode, INCOMING_REGNO (regno));

	emit_move_insn (adjust_address (registers, mode, size), tem);
	size += GET_MODE_SIZE (mode);
      }

  /* Save the arg pointer to the block.  */
  tem = copy_to_reg (virtual_incoming_args_rtx);
#ifdef STACK_GROWS_DOWNWARD
  /* We need the pointer as the caller actually passed them to us, not
     as we might have pretended they were passed.  Make sure it's a valid
     operand, as emit_move_insn isn't expected to handle a PLUS.  */
  tem
    = force_operand (plus_constant (tem, current_function_pretend_args_size),
		     NULL_RTX);
#endif
  emit_move_insn (adjust_address (registers, Pmode, 0), tem);

  size = GET_MODE_SIZE (Pmode);

  /* Save the structure value address unless this is passed as an
     "invisible" first argument.  */
  if (struct_incoming_value)
    {
      emit_move_insn (adjust_address (registers, Pmode, size),
		      copy_to_reg (struct_incoming_value));
      size += GET_MODE_SIZE (Pmode);
    }

  /* Return the address of the block.  */
  return copy_addr_to_reg (XEXP (registers, 0));
}

/* __builtin_apply_args returns block of memory allocated on
   the stack into which is stored the arg pointer, structure
   value address, static chain, and all the registers that might
   possibly be used in performing a function call.  The code is
   moved to the start of the function so the incoming values are
   saved.  */

static rtx
expand_builtin_apply_args (void)
{
  /* Don't do __builtin_apply_args more than once in a function.
     Save the result of the first call and reuse it.  */
  if (apply_args_value != 0)
    return apply_args_value;
  {
    /* When this function is called, it means that registers must be
       saved on entry to this function.  So we migrate the
       call to the first insn of this function.  */
    rtx temp;
    rtx seq;

    start_sequence ();
    temp = expand_builtin_apply_args_1 ();
    seq = get_insns ();
    end_sequence ();

    apply_args_value = temp;

    /* Put the insns after the NOTE that starts the function.
       If this is inside a start_sequence, make the outer-level insn
       chain current, so the code is placed at the start of the
       function.  */
    push_topmost_sequence ();
    emit_insn_before (seq, NEXT_INSN (entry_of_function ()));
    pop_topmost_sequence ();
    return temp;
  }
}

/* Perform an untyped call and save the state required to perform an
   untyped return of whatever value was returned by the given function.  */

static rtx
expand_builtin_apply (rtx function, rtx arguments, rtx argsize)
{
  int size, align, regno;
  enum machine_mode mode;
  rtx incoming_args, result, reg, dest, src, call_insn;
  rtx old_stack_level = 0;
  rtx call_fusage = 0;
  rtx struct_value = targetm.calls.struct_value_rtx (cfun ? TREE_TYPE (cfun->decl) : 0, 0);

  arguments = convert_memory_address (Pmode, arguments);

  /* Create a block where the return registers can be saved.  */
  result = assign_stack_local (BLKmode, apply_result_size (), -1);

  /* Fetch the arg pointer from the ARGUMENTS block.  */
  incoming_args = gen_reg_rtx (Pmode);
  emit_move_insn (incoming_args, gen_rtx_MEM (Pmode, arguments));
#ifndef STACK_GROWS_DOWNWARD
  incoming_args = expand_simple_binop (Pmode, MINUS, incoming_args, argsize,
				       incoming_args, 0, OPTAB_LIB_WIDEN);
#endif

  /* Push a new argument block and copy the arguments.  Do not allow
     the (potential) memcpy call below to interfere with our stack
     manipulations.  */
  do_pending_stack_adjust ();
  NO_DEFER_POP;

  /* Save the stack with nonlocal if available.  */
#ifdef HAVE_save_stack_nonlocal
  if (HAVE_save_stack_nonlocal)
    emit_stack_save (SAVE_NONLOCAL, &old_stack_level, NULL_RTX);
  else
#endif
    emit_stack_save (SAVE_BLOCK, &old_stack_level, NULL_RTX);

  /* Allocate a block of memory onto the stack and copy the memory
     arguments to the outgoing arguments address.  */
  allocate_dynamic_stack_space (argsize, 0, BITS_PER_UNIT);
  dest = virtual_outgoing_args_rtx;
#ifndef STACK_GROWS_DOWNWARD
  if (GET_CODE (argsize) == CONST_INT)
    dest = plus_constant (dest, -INTVAL (argsize));
  else
    dest = gen_rtx_PLUS (Pmode, dest, negate_rtx (Pmode, argsize));
#endif
  dest = gen_rtx_MEM (BLKmode, dest);
  set_mem_align (dest, PARM_BOUNDARY);
  src = gen_rtx_MEM (BLKmode, incoming_args);
  set_mem_align (src, PARM_BOUNDARY);
  emit_block_move (dest, src, argsize, BLOCK_OP_NORMAL);

  /* Refer to the argument block.  */
  apply_args_size ();
  arguments = gen_rtx_MEM (BLKmode, arguments);
  set_mem_align (arguments, PARM_BOUNDARY);

  /* Walk past the arg-pointer and structure value address.  */
  size = GET_MODE_SIZE (Pmode);
  if (struct_value)
    size += GET_MODE_SIZE (Pmode);

  /* Restore each of the registers previously saved.  Make USE insns
     for each of these registers for use in making the call.  */
  for (regno = 0; regno < FIRST_PSEUDO_REGISTER; regno++)
    if ((mode = apply_args_mode[regno]) != VOIDmode)
      {
	align = GET_MODE_ALIGNMENT (mode) / BITS_PER_UNIT;
	if (size % align != 0)
	  size = CEIL (size, align) * align;
	reg = gen_rtx_REG (mode, regno);
	emit_move_insn (reg, adjust_address (arguments, mode, size));
	use_reg (&call_fusage, reg);
	size += GET_MODE_SIZE (mode);
      }

  /* Restore the structure value address unless this is passed as an
     "invisible" first argument.  */
  size = GET_MODE_SIZE (Pmode);
  if (struct_value)
    {
      rtx value = gen_reg_rtx (Pmode);
      emit_move_insn (value, adjust_address (arguments, Pmode, size));
      emit_move_insn (struct_value, value);
      if (REG_P (struct_value))
	use_reg (&call_fusage, struct_value);
      size += GET_MODE_SIZE (Pmode);
    }

  /* All arguments and registers used for the call are set up by now!  */
  function = prepare_call_address (function, NULL, &call_fusage, 0, 0);

  /* Ensure address is valid.  SYMBOL_REF is already valid, so no need,
     and we don't want to load it into a register as an optimization,
     because prepare_call_address already did it if it should be done.  */
  if (GET_CODE (function) != SYMBOL_REF)
    function = memory_address (FUNCTION_MODE, function);

  /* Generate the actual call instruction and save the return value.  */
#ifdef HAVE_untyped_call
  if (HAVE_untyped_call)
    emit_call_insn (gen_untyped_call (gen_rtx_MEM (FUNCTION_MODE, function),
				      result, result_vector (1, result)));
  else
#endif
#ifdef HAVE_call_value
  if (HAVE_call_value)
    {
      rtx valreg = 0;

      /* Locate the unique return register.  It is not possible to
	 express a call that sets more than one return register using
	 call_value; use untyped_call for that.  In fact, untyped_call
	 only needs to save the return registers in the given block.  */
      for (regno = 0; regno < FIRST_PSEUDO_REGISTER; regno++)
	if ((mode = apply_result_mode[regno]) != VOIDmode)
	  {
	    gcc_assert (!valreg); /* HAVE_untyped_call required.  */

	    valreg = gen_rtx_REG (mode, regno);
	  }

      emit_call_insn (GEN_CALL_VALUE (valreg,
				      gen_rtx_MEM (FUNCTION_MODE, function),
				      const0_rtx, NULL_RTX, const0_rtx));

      emit_move_insn (adjust_address (result, GET_MODE (valreg), 0), valreg);
    }
  else
#endif
    gcc_unreachable ();

  /* Find the CALL insn we just emitted, and attach the register usage
     information.  */
  call_insn = last_call_insn ();
  add_function_usage_to (call_insn, call_fusage);

  /* Restore the stack.  */
#ifdef HAVE_save_stack_nonlocal
  if (HAVE_save_stack_nonlocal)
    emit_stack_restore (SAVE_NONLOCAL, old_stack_level, NULL_RTX);
  else
#endif
    emit_stack_restore (SAVE_BLOCK, old_stack_level, NULL_RTX);

  OK_DEFER_POP;

  /* Return the address of the result block.  */
  result = copy_addr_to_reg (XEXP (result, 0));
  return convert_memory_address (ptr_mode, result);
}

/* Perform an untyped return.  */

static void
expand_builtin_return (rtx result)
{
  int size, align, regno;
  enum machine_mode mode;
  rtx reg;
  rtx call_fusage = 0;

  result = convert_memory_address (Pmode, result);

  apply_result_size ();
  result = gen_rtx_MEM (BLKmode, result);

#ifdef HAVE_untyped_return
  if (HAVE_untyped_return)
    {
      emit_jump_insn (gen_untyped_return (result, result_vector (0, result)));
      emit_barrier ();
      return;
    }
#endif

  /* Restore the return value and note that each value is used.  */
  size = 0;
  for (regno = 0; regno < FIRST_PSEUDO_REGISTER; regno++)
    if ((mode = apply_result_mode[regno]) != VOIDmode)
      {
	align = GET_MODE_ALIGNMENT (mode) / BITS_PER_UNIT;
	if (size % align != 0)
	  size = CEIL (size, align) * align;
	reg = gen_rtx_REG (mode, INCOMING_REGNO (regno));
	emit_move_insn (reg, adjust_address (result, mode, size));

	push_to_sequence (call_fusage);
	emit_insn (gen_rtx_USE (VOIDmode, reg));
	call_fusage = get_insns ();
	end_sequence ();
	size += GET_MODE_SIZE (mode);
      }

  /* Put the USE insns before the return.  */
  emit_insn (call_fusage);

  /* Return whatever values was restored by jumping directly to the end
     of the function.  */
  expand_naked_return ();
}

/* Used by expand_builtin_classify_type and fold_builtin_classify_type.  */

static enum type_class
type_to_class (tree type)
{
  switch (TREE_CODE (type))
    {
    case VOID_TYPE:	   return void_type_class;
    case INTEGER_TYPE:	   return integer_type_class;
    case ENUMERAL_TYPE:	   return enumeral_type_class;
    case BOOLEAN_TYPE:	   return boolean_type_class;
    case POINTER_TYPE:	   return pointer_type_class;
    case REFERENCE_TYPE:   return reference_type_class;
    case OFFSET_TYPE:	   return offset_type_class;
    case REAL_TYPE:	   return real_type_class;
    case COMPLEX_TYPE:	   return complex_type_class;
    case FUNCTION_TYPE:	   return function_type_class;
    case METHOD_TYPE:	   return method_type_class;
    case RECORD_TYPE:	   return record_type_class;
    case UNION_TYPE:
    case QUAL_UNION_TYPE:  return union_type_class;
    case ARRAY_TYPE:	   return (TYPE_STRING_FLAG (type)
				   ? string_type_class : array_type_class);
    case LANG_TYPE:	   return lang_type_class;
    default:		   return no_type_class;
    }
}

/* Expand a call to __builtin_classify_type with arguments found in
   ARGLIST.  */

static rtx
expand_builtin_classify_type (tree arglist)
{
  if (arglist != 0)
    return GEN_INT (type_to_class (TREE_TYPE (TREE_VALUE (arglist))));
  return GEN_INT (no_type_class);
}

/* This helper macro, meant to be used in mathfn_built_in below,
   determines which among a set of three builtin math functions is
   appropriate for a given type mode.  The `F' and `L' cases are
   automatically generated from the `double' case.  */
#define CASE_MATHFN(BUILT_IN_MATHFN) \
  case BUILT_IN_MATHFN: case BUILT_IN_MATHFN##F: case BUILT_IN_MATHFN##L: \
  fcode = BUILT_IN_MATHFN; fcodef = BUILT_IN_MATHFN##F ; \
  fcodel = BUILT_IN_MATHFN##L ; break;

/* Return mathematic function equivalent to FN but operating directly
   on TYPE, if available.  If we can't do the conversion, return zero.  */
tree
mathfn_built_in (tree type, enum built_in_function fn)
{
  enum built_in_function fcode, fcodef, fcodel;

  switch (fn)
    {
      CASE_MATHFN (BUILT_IN_ACOS)
      CASE_MATHFN (BUILT_IN_ACOSH)
      CASE_MATHFN (BUILT_IN_ASIN)
      CASE_MATHFN (BUILT_IN_ASINH)
      CASE_MATHFN (BUILT_IN_ATAN)
      CASE_MATHFN (BUILT_IN_ATAN2)
      CASE_MATHFN (BUILT_IN_ATANH)
      CASE_MATHFN (BUILT_IN_CBRT)
      CASE_MATHFN (BUILT_IN_CEIL)
      CASE_MATHFN (BUILT_IN_COPYSIGN)
      CASE_MATHFN (BUILT_IN_COS)
      CASE_MATHFN (BUILT_IN_COSH)
      CASE_MATHFN (BUILT_IN_DREM)
      CASE_MATHFN (BUILT_IN_ERF)
      CASE_MATHFN (BUILT_IN_ERFC)
      CASE_MATHFN (BUILT_IN_EXP)
      CASE_MATHFN (BUILT_IN_EXP10)
      CASE_MATHFN (BUILT_IN_EXP2)
      CASE_MATHFN (BUILT_IN_EXPM1)
      CASE_MATHFN (BUILT_IN_FABS)
      CASE_MATHFN (BUILT_IN_FDIM)
      CASE_MATHFN (BUILT_IN_FLOOR)
      CASE_MATHFN (BUILT_IN_FMA)
      CASE_MATHFN (BUILT_IN_FMAX)
      CASE_MATHFN (BUILT_IN_FMIN)
      CASE_MATHFN (BUILT_IN_FMOD)
      CASE_MATHFN (BUILT_IN_FREXP)
      CASE_MATHFN (BUILT_IN_GAMMA)
      CASE_MATHFN (BUILT_IN_HUGE_VAL)
      CASE_MATHFN (BUILT_IN_HYPOT)
      CASE_MATHFN (BUILT_IN_ILOGB)
      CASE_MATHFN (BUILT_IN_INF)
      CASE_MATHFN (BUILT_IN_J0)
      CASE_MATHFN (BUILT_IN_J1)
      CASE_MATHFN (BUILT_IN_JN)
      CASE_MATHFN (BUILT_IN_LCEIL)
      CASE_MATHFN (BUILT_IN_LDEXP)
      CASE_MATHFN (BUILT_IN_LFLOOR)
      CASE_MATHFN (BUILT_IN_LGAMMA)
      CASE_MATHFN (BUILT_IN_LLCEIL)
      CASE_MATHFN (BUILT_IN_LLFLOOR)
      CASE_MATHFN (BUILT_IN_LLRINT)
      CASE_MATHFN (BUILT_IN_LLROUND)
      CASE_MATHFN (BUILT_IN_LOG)
      CASE_MATHFN (BUILT_IN_LOG10)
      CASE_MATHFN (BUILT_IN_LOG1P)
      CASE_MATHFN (BUILT_IN_LOG2)
      CASE_MATHFN (BUILT_IN_LOGB)
      CASE_MATHFN (BUILT_IN_LRINT)
      CASE_MATHFN (BUILT_IN_LROUND)
      CASE_MATHFN (BUILT_IN_MODF)
      CASE_MATHFN (BUILT_IN_NAN)
      CASE_MATHFN (BUILT_IN_NANS)
      CASE_MATHFN (BUILT_IN_NEARBYINT)
      CASE_MATHFN (BUILT_IN_NEXTAFTER)
      CASE_MATHFN (BUILT_IN_NEXTTOWARD)
      CASE_MATHFN (BUILT_IN_POW)
      CASE_MATHFN (BUILT_IN_POWI)
      CASE_MATHFN (BUILT_IN_POW10)
      CASE_MATHFN (BUILT_IN_REMAINDER)
      CASE_MATHFN (BUILT_IN_REMQUO)
      CASE_MATHFN (BUILT_IN_RINT)
      CASE_MATHFN (BUILT_IN_ROUND)
      CASE_MATHFN (BUILT_IN_SCALB)
      CASE_MATHFN (BUILT_IN_SCALBLN)
      CASE_MATHFN (BUILT_IN_SCALBN)
      CASE_MATHFN (BUILT_IN_SIGNIFICAND)
      CASE_MATHFN (BUILT_IN_SIN)
      CASE_MATHFN (BUILT_IN_SINCOS)
      CASE_MATHFN (BUILT_IN_SINH)
      CASE_MATHFN (BUILT_IN_SQRT)
      CASE_MATHFN (BUILT_IN_TAN)
      CASE_MATHFN (BUILT_IN_TANH)
      CASE_MATHFN (BUILT_IN_TGAMMA)
      CASE_MATHFN (BUILT_IN_TRUNC)
      CASE_MATHFN (BUILT_IN_Y0)
      CASE_MATHFN (BUILT_IN_Y1)
      CASE_MATHFN (BUILT_IN_YN)

      default:
	return 0;
      }

  if (TYPE_MAIN_VARIANT (type) == double_type_node)
    return implicit_built_in_decls[fcode];
  else if (TYPE_MAIN_VARIANT (type) == float_type_node)
    return implicit_built_in_decls[fcodef];
  else if (TYPE_MAIN_VARIANT (type) == long_double_type_node)
    return implicit_built_in_decls[fcodel];
  else
    return 0;
}

/* If errno must be maintained, expand the RTL to check if the result,
   TARGET, of a built-in function call, EXP, is NaN, and if so set
   errno to EDOM.  */

static void
expand_errno_check (tree exp, rtx target)
{
  rtx lab = gen_label_rtx ();

  /* Test the result; if it is NaN, set errno=EDOM because
     the argument was not in the domain.  */
  emit_cmp_and_jump_insns (target, target, EQ, 0, GET_MODE (target),
			   0, lab);

#ifdef TARGET_EDOM
  /* If this built-in doesn't throw an exception, set errno directly.  */
  if (TREE_NOTHROW (TREE_OPERAND (TREE_OPERAND (exp, 0), 0)))
    {
#ifdef GEN_ERRNO_RTX
      rtx errno_rtx = GEN_ERRNO_RTX;
#else
      rtx errno_rtx
	  = gen_rtx_MEM (word_mode, gen_rtx_SYMBOL_REF (Pmode, "errno"));
#endif
      emit_move_insn (errno_rtx, GEN_INT (TARGET_EDOM));
      emit_label (lab);
      return;
    }
#endif

  /* We can't set errno=EDOM directly; let the library call do it.
     Pop the arguments right away in case the call gets deleted.  */
  NO_DEFER_POP;
  expand_call (exp, target, 0);
  OK_DEFER_POP;
  emit_label (lab);
}


/* Expand a call to one of the builtin math functions (sqrt, exp, or log).
   Return 0 if a normal call should be emitted rather than expanding the
   function in-line.  EXP is the expression that is a call to the builtin
   function; if convenient, the result should be placed in TARGET.
   SUBTARGET may be used as the target for computing one of EXP's operands.  */

static rtx
expand_builtin_mathfn (tree exp, rtx target, rtx subtarget)
{
  optab builtin_optab;
  rtx op0, insns, before_call;
  tree fndecl = get_callee_fndecl (exp);
  tree arglist = TREE_OPERAND (exp, 1);
  enum machine_mode mode;
  bool errno_set = false;
  tree arg, narg;

  if (!validate_arglist (arglist, REAL_TYPE, VOID_TYPE))
    return 0;

  arg = TREE_VALUE (arglist);

  switch (DECL_FUNCTION_CODE (fndecl))
    {
    CASE_FLT_FN (BUILT_IN_SQRT):
      errno_set = ! tree_expr_nonnegative_p (arg);
      builtin_optab = sqrt_optab;
      break;
    CASE_FLT_FN (BUILT_IN_EXP):
      errno_set = true; builtin_optab = exp_optab; break;
    CASE_FLT_FN (BUILT_IN_EXP10):
    CASE_FLT_FN (BUILT_IN_POW10):
      errno_set = true; builtin_optab = exp10_optab; break;
    CASE_FLT_FN (BUILT_IN_EXP2):
      errno_set = true; builtin_optab = exp2_optab; break;
    CASE_FLT_FN (BUILT_IN_EXPM1):
      errno_set = true; builtin_optab = expm1_optab; break;
    CASE_FLT_FN (BUILT_IN_LOGB):
      errno_set = true; builtin_optab = logb_optab; break;
    CASE_FLT_FN (BUILT_IN_ILOGB):
      errno_set = true; builtin_optab = ilogb_optab; break;
    CASE_FLT_FN (BUILT_IN_LOG):
      errno_set = true; builtin_optab = log_optab; break;
    CASE_FLT_FN (BUILT_IN_LOG10):
      errno_set = true; builtin_optab = log10_optab; break;
    CASE_FLT_FN (BUILT_IN_LOG2):
      errno_set = true; builtin_optab = log2_optab; break;
    CASE_FLT_FN (BUILT_IN_LOG1P):
      errno_set = true; builtin_optab = log1p_optab; break;
    CASE_FLT_FN (BUILT_IN_ASIN):
      builtin_optab = asin_optab; break;
    CASE_FLT_FN (BUILT_IN_ACOS):
      builtin_optab = acos_optab; break;
    CASE_FLT_FN (BUILT_IN_TAN):
      builtin_optab = tan_optab; break;
    CASE_FLT_FN (BUILT_IN_ATAN):
      builtin_optab = atan_optab; break;
    CASE_FLT_FN (BUILT_IN_FLOOR):
      builtin_optab = floor_optab; break;
    CASE_FLT_FN (BUILT_IN_CEIL):
      builtin_optab = ceil_optab; break;
    CASE_FLT_FN (BUILT_IN_TRUNC):
      builtin_optab = btrunc_optab; break;
    CASE_FLT_FN (BUILT_IN_ROUND):
      builtin_optab = round_optab; break;
    CASE_FLT_FN (BUILT_IN_NEARBYINT):
      builtin_optab = nearbyint_optab; break;
    CASE_FLT_FN (BUILT_IN_RINT):
      builtin_optab = rint_optab; break;
    CASE_FLT_FN (BUILT_IN_LRINT):
    CASE_FLT_FN (BUILT_IN_LLRINT):
      builtin_optab = lrint_optab; break;
    default:
      gcc_unreachable ();
    }

  /* Make a suitable register to place result in.  */
  mode = TYPE_MODE (TREE_TYPE (exp));

  if (! flag_errno_math || ! HONOR_NANS (mode))
    errno_set = false;

  /* Before working hard, check whether the instruction is available.  */
  if (builtin_optab->handlers[(int) mode].insn_code != CODE_FOR_nothing)
    {
      target = gen_reg_rtx (mode);

      /* Wrap the computation of the argument in a SAVE_EXPR, as we may
	 need to expand the argument again.  This way, we will not perform
	 side-effects more the once.  */
      narg = builtin_save_expr (arg);
      if (narg != arg)
	{
	  arg = narg;
	  arglist = build_tree_list (NULL_TREE, arg);
	  exp = build_function_call_expr (fndecl, arglist);
	}

      op0 = expand_expr (arg, subtarget, VOIDmode, 0);

      start_sequence ();

      /* Compute into TARGET.
	 Set TARGET to wherever the result comes back.  */
      target = expand_unop (mode, builtin_optab, op0, target, 0);

      if (target != 0)
	{
	  if (errno_set)
	    expand_errno_check (exp, target);

	  /* Output the entire sequence.  */
	  insns = get_insns ();
	  end_sequence ();
	  emit_insn (insns);
	  return target;
	}

      /* If we were unable to expand via the builtin, stop the sequence
	 (without outputting the insns) and call to the library function
	 with the stabilized argument list.  */
      end_sequence ();
    }

  before_call = get_last_insn ();

  target = expand_call (exp, target, target == const0_rtx);

  /* If this is a sqrt operation and we don't care about errno, try to
     attach a REG_EQUAL note with a SQRT rtx to the emitted libcall.
     This allows the semantics of the libcall to be visible to the RTL
     optimizers.  */
  if (builtin_optab == sqrt_optab && !errno_set)
    {
      /* Search backwards through the insns emitted by expand_call looking
	 for the instruction with the REG_RETVAL note.  */
      rtx last = get_last_insn ();
      while (last != before_call)
	{
	  if (find_reg_note (last, REG_RETVAL, NULL))
	    {
	      rtx note = find_reg_note (last, REG_EQUAL, NULL);
	      /* Check that the REQ_EQUAL note is an EXPR_LIST with
		 two elements, i.e. symbol_ref(sqrt) and the operand.  */
	      if (note
		  && GET_CODE (note) == EXPR_LIST
		  && GET_CODE (XEXP (note, 0)) == EXPR_LIST
		  && XEXP (XEXP (note, 0), 1) != NULL_RTX
		  && XEXP (XEXP (XEXP (note, 0), 1), 1) == NULL_RTX)
		{
		  rtx operand = XEXP (XEXP (XEXP (note, 0), 1), 0);
		  /* Check operand is a register with expected mode.  */
		  if (operand
		      && REG_P (operand)
		      && GET_MODE (operand) == mode)
		    {
		      /* Replace the REG_EQUAL note with a SQRT rtx.  */
		      rtx equiv = gen_rtx_SQRT (mode, operand);
		      set_unique_reg_note (last, REG_EQUAL, equiv);
		    }
		}
	      break;
	    }
	  last = PREV_INSN (last);
	}
    }

  return target;
}

/* Expand a call to the builtin binary math functions (pow and atan2).
   Return 0 if a normal call should be emitted rather than expanding the
   function in-line.  EXP is the expression that is a call to the builtin
   function; if convenient, the result should be placed in TARGET.
   SUBTARGET may be used as the target for computing one of EXP's
   operands.  */

static rtx
expand_builtin_mathfn_2 (tree exp, rtx target, rtx subtarget)
{
  optab builtin_optab;
  rtx op0, op1, insns;
  int op1_type = REAL_TYPE;
  tree fndecl = get_callee_fndecl (exp);
  tree arglist = TREE_OPERAND (exp, 1);
  tree arg0, arg1, temp, narg;
  enum machine_mode mode;
  bool errno_set = true;
  bool stable = true;

  if ((DECL_FUNCTION_CODE (fndecl) == BUILT_IN_LDEXP)
      || (DECL_FUNCTION_CODE (fndecl) == BUILT_IN_LDEXPF)
      || (DECL_FUNCTION_CODE (fndecl) == BUILT_IN_LDEXPL))
    op1_type = INTEGER_TYPE;

  if (!validate_arglist (arglist, REAL_TYPE, op1_type, VOID_TYPE))
    return 0;

  arg0 = TREE_VALUE (arglist);
  arg1 = TREE_VALUE (TREE_CHAIN (arglist));

  switch (DECL_FUNCTION_CODE (fndecl))
    {
    CASE_FLT_FN (BUILT_IN_POW):
      builtin_optab = pow_optab; break;
    CASE_FLT_FN (BUILT_IN_ATAN2):
      builtin_optab = atan2_optab; break;
    CASE_FLT_FN (BUILT_IN_LDEXP):
      builtin_optab = ldexp_optab; break;
    CASE_FLT_FN (BUILT_IN_FMOD):
      builtin_optab = fmod_optab; break;
    CASE_FLT_FN (BUILT_IN_DREM):
      builtin_optab = drem_optab; break;
    default:
      gcc_unreachable ();
    }

  /* Make a suitable register to place result in.  */
  mode = TYPE_MODE (TREE_TYPE (exp));

  /* Before working hard, check whether the instruction is available.  */
  if (builtin_optab->handlers[(int) mode].insn_code == CODE_FOR_nothing)
    return 0;

  target = gen_reg_rtx (mode);

  if (! flag_errno_math || ! HONOR_NANS (mode))
    errno_set = false;

  /* Always stabilize the argument list.  */
  narg = builtin_save_expr (arg1);
  if (narg != arg1)
    {
      arg1 = narg;
      temp = build_tree_list (NULL_TREE, narg);
      stable = false;
    }
  else
    temp = TREE_CHAIN (arglist);

  narg = builtin_save_expr (arg0);
  if (narg != arg0)
    {
      arg0 = narg;
      arglist = tree_cons (NULL_TREE, narg, temp);
      stable = false;
    }
  else if (! stable)
    arglist = tree_cons (NULL_TREE, arg0, temp);

  if (! stable)
    exp = build_function_call_expr (fndecl, arglist);

  op0 = expand_expr (arg0, subtarget, VOIDmode, EXPAND_NORMAL);
  op1 = expand_normal (arg1);

  start_sequence ();

  /* Compute into TARGET.
     Set TARGET to wherever the result comes back.  */
  target = expand_binop (mode, builtin_optab, op0, op1,
			 target, 0, OPTAB_DIRECT);

  /* If we were unable to expand via the builtin, stop the sequence
     (without outputting the insns) and call to the library function
     with the stabilized argument list.  */
  if (target == 0)
    {
      end_sequence ();
      return expand_call (exp, target, target == const0_rtx);
    }

  if (errno_set)
    expand_errno_check (exp, target);

  /* Output the entire sequence.  */
  insns = get_insns ();
  end_sequence ();
  emit_insn (insns);

  return target;
}

/* Expand a call to the builtin sin and cos math functions.
   Return 0 if a normal call should be emitted rather than expanding the
   function in-line.  EXP is the expression that is a call to the builtin
   function; if convenient, the result should be placed in TARGET.
   SUBTARGET may be used as the target for computing one of EXP's
   operands.  */

static rtx
expand_builtin_mathfn_3 (tree exp, rtx target, rtx subtarget)
{
  optab builtin_optab;
  rtx op0, insns;
  tree fndecl = get_callee_fndecl (exp);
  tree arglist = TREE_OPERAND (exp, 1);
  enum machine_mode mode;
  tree arg, narg;

  if (!validate_arglist (arglist, REAL_TYPE, VOID_TYPE))
    return 0;

  arg = TREE_VALUE (arglist);

  switch (DECL_FUNCTION_CODE (fndecl))
    {
    CASE_FLT_FN (BUILT_IN_SIN):
    CASE_FLT_FN (BUILT_IN_COS):
      builtin_optab = sincos_optab; break;
    default:
      gcc_unreachable ();
    }

  /* Make a suitable register to place result in.  */
  mode = TYPE_MODE (TREE_TYPE (exp));

  /* Check if sincos insn is available, otherwise fallback
     to sin or cos insn.  */
  if (builtin_optab->handlers[(int) mode].insn_code == CODE_FOR_nothing) {
    switch (DECL_FUNCTION_CODE (fndecl))
      {
      CASE_FLT_FN (BUILT_IN_SIN):
	builtin_optab = sin_optab; break;
      CASE_FLT_FN (BUILT_IN_COS):
	builtin_optab = cos_optab; break;
      default:
	gcc_unreachable ();
      }
  }

  /* Before working hard, check whether the instruction is available.  */
  if (builtin_optab->handlers[(int) mode].insn_code != CODE_FOR_nothing)
    {
      target = gen_reg_rtx (mode);

      /* Wrap the computation of the argument in a SAVE_EXPR, as we may
	 need to expand the argument again.  This way, we will not perform
	 side-effects more the once.  */
      narg = save_expr (arg);
      if (narg != arg)
	{
	  arg = narg;
	  arglist = build_tree_list (NULL_TREE, arg);
	  exp = build_function_call_expr (fndecl, arglist);
	}

      op0 = expand_expr (arg, subtarget, VOIDmode, 0);

      start_sequence ();

      /* Compute into TARGET.
	 Set TARGET to wherever the result comes back.  */
      if (builtin_optab == sincos_optab)
	{
	  int result;

	  switch (DECL_FUNCTION_CODE (fndecl))
	    {
	    CASE_FLT_FN (BUILT_IN_SIN):
	      result = expand_twoval_unop (builtin_optab, op0, 0, target, 0);
	      break;
	    CASE_FLT_FN (BUILT_IN_COS):
	      result = expand_twoval_unop (builtin_optab, op0, target, 0, 0);
	      break;
	    default:
	      gcc_unreachable ();
	    }
	  gcc_assert (result);
	}
      else
	{
	  target = expand_unop (mode, builtin_optab, op0, target, 0);
	}

      if (target != 0)
	{
	  /* Output the entire sequence.  */
	  insns = get_insns ();
	  end_sequence ();
	  emit_insn (insns);
	  return target;
	}

      /* If we were unable to expand via the builtin, stop the sequence
	 (without outputting the insns) and call to the library function
	 with the stabilized argument list.  */
      end_sequence ();
    }

  target = expand_call (exp, target, target == const0_rtx);

  return target;
}

/* Expand a call to the builtin sincos math function.
   Return 0 if a normal call should be emitted rather than expanding the
   function in-line.  EXP is the expression that is a call to the builtin
   function.  */

static rtx
expand_builtin_sincos (tree exp)
{
  rtx op0, op1, op2, target1, target2;
  tree arglist = TREE_OPERAND (exp, 1);
  enum machine_mode mode;
  tree arg, sinp, cosp;
  int result;

  if (!validate_arglist (arglist, REAL_TYPE,
			 POINTER_TYPE, POINTER_TYPE, VOID_TYPE))
    return 0;

  arg = TREE_VALUE (arglist);
  sinp = TREE_VALUE (TREE_CHAIN (arglist));
  cosp = TREE_VALUE (TREE_CHAIN (TREE_CHAIN (arglist)));

  /* Make a suitable register to place result in.  */
  mode = TYPE_MODE (TREE_TYPE (arg));

  /* Check if sincos insn is available, otherwise emit the call.  */
  if (sincos_optab->handlers[(int) mode].insn_code == CODE_FOR_nothing)
    return NULL_RTX;

  target1 = gen_reg_rtx (mode);
  target2 = gen_reg_rtx (mode);

  op0 = expand_normal (arg);
  op1 = expand_normal (build_fold_indirect_ref (sinp));
  op2 = expand_normal (build_fold_indirect_ref (cosp));

  /* Compute into target1 and target2.
     Set TARGET to wherever the result comes back.  */
  result = expand_twoval_unop (sincos_optab, op0, target2, target1, 0);
  gcc_assert (result);

  /* Move target1 and target2 to the memory locations indicated
     by op1 and op2.  */
  emit_move_insn (op1, target1);
  emit_move_insn (op2, target2);

  return const0_rtx;
}

/* Expand a call to one of the builtin rounding functions (lfloor).
   If expanding via optab fails, lower expression to (int)(floor(x)).
   EXP is the expression that is a call to the builtin function;
   if convenient, the result should be placed in TARGET.  SUBTARGET may
   be used as the target for computing one of EXP's operands.  */

static rtx
expand_builtin_int_roundingfn (tree exp, rtx target, rtx subtarget)
{
  optab builtin_optab;
  rtx op0, insns, tmp;
  tree fndecl = get_callee_fndecl (exp);
  tree arglist = TREE_OPERAND (exp, 1);
  enum built_in_function fallback_fn;
  tree fallback_fndecl;
  enum machine_mode mode;
  tree arg, narg;

  if (!validate_arglist (arglist, REAL_TYPE, VOID_TYPE))
    gcc_unreachable ();

  arg = TREE_VALUE (arglist);

  switch (DECL_FUNCTION_CODE (fndecl))
    {
    CASE_FLT_FN (BUILT_IN_LCEIL):
    CASE_FLT_FN (BUILT_IN_LLCEIL):
      builtin_optab = lceil_optab;
      fallback_fn = BUILT_IN_CEIL;
      break;

    CASE_FLT_FN (BUILT_IN_LFLOOR):
    CASE_FLT_FN (BUILT_IN_LLFLOOR):
      builtin_optab = lfloor_optab;
      fallback_fn = BUILT_IN_FLOOR;
      break;

    default:
      gcc_unreachable ();
    }

  /* Make a suitable register to place result in.  */
  mode = TYPE_MODE (TREE_TYPE (exp));

  /* Before working hard, check whether the instruction is available.  */
  if (builtin_optab->handlers[(int) mode].insn_code != CODE_FOR_nothing)
    {
      target = gen_reg_rtx (mode);

      /* Wrap the computation of the argument in a SAVE_EXPR, as we may
	 need to expand the argument again.  This way, we will not perform
	 side-effects more the once.  */
      narg = builtin_save_expr (arg);
      if (narg != arg)
	{
	  arg = narg;
	  arglist = build_tree_list (NULL_TREE, arg);
	  exp = build_function_call_expr (fndecl, arglist);
	}

      op0 = expand_expr (arg, subtarget, VOIDmode, 0);

      start_sequence ();

      /* Compute into TARGET.
	 Set TARGET to wherever the result comes back.  */
      target = expand_unop (mode, builtin_optab, op0, target, 0);

      if (target != 0)
	{
	  /* Output the entire sequence.  */
	  insns = get_insns ();
	  end_sequence ();
	  emit_insn (insns);
	  return target;
	}

      /* If we were unable to expand via the builtin, stop the sequence
	 (without outputting the insns).  */
      end_sequence ();
    }

  /* Fall back to floating point rounding optab.  */
  fallback_fndecl = mathfn_built_in (TREE_TYPE (arg), fallback_fn);
  /* We shouldn't get here on targets without TARGET_C99_FUNCTIONS.
     ??? Perhaps convert (int)floorf(x) into (int)floor((double)x).  */
  gcc_assert (fallback_fndecl != NULL_TREE);
  exp = build_function_call_expr (fallback_fndecl, arglist);

  tmp = expand_normal (exp);

  /* Truncate the result of floating point optab to integer
     via expand_fix ().  */
  target = gen_reg_rtx (mode);
  expand_fix (target, tmp, 0);

  return target;
}

/* To evaluate powi(x,n), the floating point value x raised to the
   constant integer exponent n, we use a hybrid algorithm that
   combines the "window method" with look-up tables.  For an
   introduction to exponentiation algorithms and "addition chains",
   see section 4.6.3, "Evaluation of Powers" of Donald E. Knuth,
   "Seminumerical Algorithms", Vol. 2, "The Art of Computer Programming",
   3rd Edition, 1998, and Daniel M. Gordon, "A Survey of Fast Exponentiation
   Methods", Journal of Algorithms, Vol. 27, pp. 129-146, 1998.  */

/* Provide a default value for POWI_MAX_MULTS, the maximum number of
   multiplications to inline before calling the system library's pow
   function.  powi(x,n) requires at worst 2*bits(n)-2 multiplications,
   so this default never requires calling pow, powf or powl.  */

#ifndef POWI_MAX_MULTS
#define POWI_MAX_MULTS  (2*HOST_BITS_PER_WIDE_INT-2)
#endif

/* The size of the "optimal power tree" lookup table.  All
   exponents less than this value are simply looked up in the
   powi_table below.  This threshold is also used to size the
   cache of pseudo registers that hold intermediate results.  */
#define POWI_TABLE_SIZE 256

/* The size, in bits of the window, used in the "window method"
   exponentiation algorithm.  This is equivalent to a radix of
   (1<<POWI_WINDOW_SIZE) in the corresponding "m-ary method".  */
#define POWI_WINDOW_SIZE 3

/* The following table is an efficient representation of an
   "optimal power tree".  For each value, i, the corresponding
   value, j, in the table states than an optimal evaluation
   sequence for calculating pow(x,i) can be found by evaluating
   pow(x,j)*pow(x,i-j).  An optimal power tree for the first
   100 integers is given in Knuth's "Seminumerical algorithms".  */

static const unsigned char powi_table[POWI_TABLE_SIZE] =
  {
      0,   1,   1,   2,   2,   3,   3,   4,  /*   0 -   7 */
      4,   6,   5,   6,   6,  10,   7,   9,  /*   8 -  15 */
      8,  16,   9,  16,  10,  12,  11,  13,  /*  16 -  23 */
     12,  17,  13,  18,  14,  24,  15,  26,  /*  24 -  31 */
     16,  17,  17,  19,  18,  33,  19,  26,  /*  32 -  39 */
     20,  25,  21,  40,  22,  27,  23,  44,  /*  40 -  47 */
     24,  32,  25,  34,  26,  29,  27,  44,  /*  48 -  55 */
     28,  31,  29,  34,  30,  60,  31,  36,  /*  56 -  63 */
     32,  64,  33,  34,  34,  46,  35,  37,  /*  64 -  71 */
     36,  65,  37,  50,  38,  48,  39,  69,  /*  72 -  79 */
     40,  49,  41,  43,  42,  51,  43,  58,  /*  80 -  87 */
     44,  64,  45,  47,  46,  59,  47,  76,  /*  88 -  95 */
     48,  65,  49,  66,  50,  67,  51,  66,  /*  96 - 103 */
     52,  70,  53,  74,  54, 104,  55,  74,  /* 104 - 111 */
     56,  64,  57,  69,  58,  78,  59,  68,  /* 112 - 119 */
     60,  61,  61,  80,  62,  75,  63,  68,  /* 120 - 127 */
     64,  65,  65, 128,  66, 129,  67,  90,  /* 128 - 135 */
     68,  73,  69, 131,  70,  94,  71,  88,  /* 136 - 143 */
     72, 128,  73,  98,  74, 132,  75, 121,  /* 144 - 151 */
     76, 102,  77, 124,  78, 132,  79, 106,  /* 152 - 159 */
     80,  97,  81, 160,  82,  99,  83, 134,  /* 160 - 167 */
     84,  86,  85,  95,  86, 160,  87, 100,  /* 168 - 175 */
     88, 113,  89,  98,  90, 107,  91, 122,  /* 176 - 183 */
     92, 111,  93, 102,  94, 126,  95, 150,  /* 184 - 191 */
     96, 128,  97, 130,  98, 133,  99, 195,  /* 192 - 199 */
    100, 128, 101, 123, 102, 164, 103, 138,  /* 200 - 207 */
    104, 145, 105, 146, 106, 109, 107, 149,  /* 208 - 215 */
    108, 200, 109, 146, 110, 170, 111, 157,  /* 216 - 223 */
    112, 128, 113, 130, 114, 182, 115, 132,  /* 224 - 231 */
    116, 200, 117, 132, 118, 158, 119, 206,  /* 232 - 239 */
    120, 240, 121, 162, 122, 147, 123, 152,  /* 240 - 247 */
    124, 166, 125, 214, 126, 138, 127, 153,  /* 248 - 255 */
  };


/* Return the number of multiplications required to calculate
   powi(x,n) where n is less than POWI_TABLE_SIZE.  This is a
   subroutine of powi_cost.  CACHE is an array indicating
   which exponents have already been calculated.  */

static int
powi_lookup_cost (unsigned HOST_WIDE_INT n, bool *cache)
{
  /* If we've already calculated this exponent, then this evaluation
     doesn't require any additional multiplications.  */
  if (cache[n])
    return 0;

  cache[n] = true;
  return powi_lookup_cost (n - powi_table[n], cache)
	 + powi_lookup_cost (powi_table[n], cache) + 1;
}

/* Return the number of multiplications required to calculate
   powi(x,n) for an arbitrary x, given the exponent N.  This
   function needs to be kept in sync with expand_powi below.  */

static int
powi_cost (HOST_WIDE_INT n)
{
  bool cache[POWI_TABLE_SIZE];
  unsigned HOST_WIDE_INT digit;
  unsigned HOST_WIDE_INT val;
  int result;

  if (n == 0)
    return 0;

  /* Ignore the reciprocal when calculating the cost.  */
  val = (n < 0) ? -n : n;

  /* Initialize the exponent cache.  */
  memset (cache, 0, POWI_TABLE_SIZE * sizeof (bool));
  cache[1] = true;

  result = 0;

  while (val >= POWI_TABLE_SIZE)
    {
      if (val & 1)
	{
	  digit = val & ((1 << POWI_WINDOW_SIZE) - 1);
	  result += powi_lookup_cost (digit, cache)
		    + POWI_WINDOW_SIZE + 1;
	  val >>= POWI_WINDOW_SIZE;
	}
      else
	{
	  val >>= 1;
	  result++;
	}
    }

  return result + powi_lookup_cost (val, cache);
}

/* Recursive subroutine of expand_powi.  This function takes the array,
   CACHE, of already calculated exponents and an exponent N and returns
   an RTX that corresponds to CACHE[1]**N, as calculated in mode MODE.  */

static rtx
expand_powi_1 (enum machine_mode mode, unsigned HOST_WIDE_INT n, rtx *cache)
{
  unsigned HOST_WIDE_INT digit;
  rtx target, result;
  rtx op0, op1;

  if (n < POWI_TABLE_SIZE)
    {
      if (cache[n])
	return cache[n];

      target = gen_reg_rtx (mode);
      cache[n] = target;

      op0 = expand_powi_1 (mode, n - powi_table[n], cache);
      op1 = expand_powi_1 (mode, powi_table[n], cache);
    }
  else if (n & 1)
    {
      target = gen_reg_rtx (mode);
      digit = n & ((1 << POWI_WINDOW_SIZE) - 1);
      op0 = expand_powi_1 (mode, n - digit, cache);
      op1 = expand_powi_1 (mode, digit, cache);
    }
  else
    {
      target = gen_reg_rtx (mode);
      op0 = expand_powi_1 (mode, n >> 1, cache);
      op1 = op0;
    }

  result = expand_mult (mode, op0, op1, target, 0);
  if (result != target)
    emit_move_insn (target, result);
  return target;
}

/* Expand the RTL to evaluate powi(x,n) in mode MODE.  X is the
   floating point operand in mode MODE, and N is the exponent.  This
   function needs to be kept in sync with powi_cost above.  */

static rtx
expand_powi (rtx x, enum machine_mode mode, HOST_WIDE_INT n)
{
  unsigned HOST_WIDE_INT val;
  rtx cache[POWI_TABLE_SIZE];
  rtx result;

  if (n == 0)
    return CONST1_RTX (mode);

  val = (n < 0) ? -n : n;

  memset (cache, 0, sizeof (cache));
  cache[1] = x;

  result = expand_powi_1 (mode, (n < 0) ? -n : n, cache);

  /* If the original exponent was negative, reciprocate the result.  */
  if (n < 0)
    result = expand_binop (mode, sdiv_optab, CONST1_RTX (mode),
			   result, NULL_RTX, 0, OPTAB_LIB_WIDEN);

  return result;
}

/* Expand a call to the pow built-in mathematical function.  Return 0 if
   a normal call should be emitted rather than expanding the function
   in-line.  EXP is the expression that is a call to the builtin
   function; if convenient, the result should be placed in TARGET.  */

static rtx
expand_builtin_pow (tree exp, rtx target, rtx subtarget)
{
  tree arglist = TREE_OPERAND (exp, 1);
  tree arg0, arg1;

  if (! validate_arglist (arglist, REAL_TYPE, REAL_TYPE, VOID_TYPE))
    return 0;

  arg0 = TREE_VALUE (arglist);
  arg1 = TREE_VALUE (TREE_CHAIN (arglist));

  if (TREE_CODE (arg1) == REAL_CST
      && ! TREE_CONSTANT_OVERFLOW (arg1))
    {
      REAL_VALUE_TYPE cint;
      REAL_VALUE_TYPE c;
      HOST_WIDE_INT n;

      c = TREE_REAL_CST (arg1);
      n = real_to_integer (&c);
      real_from_integer (&cint, VOIDmode, n, n < 0 ? -1 : 0, 0);
      if (real_identical (&c, &cint))
	{
	  /* If the exponent is -1, 0, 1 or 2, then expand_powi is exact.
	     Otherwise, check the number of multiplications required.
	     Note that pow never sets errno for an integer exponent.  */
	  if ((n >= -1 && n <= 2)
	      || (flag_unsafe_math_optimizations
		  && ! optimize_size
		  && powi_cost (n) <= POWI_MAX_MULTS))
	    {
	      enum machine_mode mode = TYPE_MODE (TREE_TYPE (exp));
	      rtx op = expand_expr (arg0, subtarget, VOIDmode, 0);
	      op = force_reg (mode, op);
	      return expand_powi (op, mode, n);
	    }
	}
    }

  if (! flag_unsafe_math_optimizations)
    return NULL_RTX;
  return expand_builtin_mathfn_2 (exp, target, subtarget);
}

/* Expand a call to the powi built-in mathematical function.  Return 0 if
   a normal call should be emitted rather than expanding the function
   in-line.  EXP is the expression that is a call to the builtin
   function; if convenient, the result should be placed in TARGET.  */

static rtx
expand_builtin_powi (tree exp, rtx target, rtx subtarget)
{
  tree arglist = TREE_OPERAND (exp, 1);
  tree arg0, arg1;
  rtx op0, op1;
  enum machine_mode mode;
  enum machine_mode mode2;

  if (! validate_arglist (arglist, REAL_TYPE, INTEGER_TYPE, VOID_TYPE))
    return 0;

  arg0 = TREE_VALUE (arglist);
  arg1 = TREE_VALUE (TREE_CHAIN (arglist));
  mode = TYPE_MODE (TREE_TYPE (exp));

  /* Handle constant power.  */

  if (TREE_CODE (arg1) == INTEGER_CST
      && ! TREE_CONSTANT_OVERFLOW (arg1))
    {
      HOST_WIDE_INT n = TREE_INT_CST_LOW (arg1);

      /* If the exponent is -1, 0, 1 or 2, then expand_powi is exact.
	 Otherwise, check the number of multiplications required.  */
      if ((TREE_INT_CST_HIGH (arg1) == 0
	   || TREE_INT_CST_HIGH (arg1) == -1)
	  && ((n >= -1 && n <= 2)
	      || (! optimize_size
		  && powi_cost (n) <= POWI_MAX_MULTS)))
	{
	  op0 = expand_expr (arg0, subtarget, VOIDmode, 0);
	  op0 = force_reg (mode, op0);
	  return expand_powi (op0, mode, n);
	}
    }

  /* Emit a libcall to libgcc.  */

  /* Mode of the 2nd argument must match that of an int. */
  mode2 = mode_for_size (INT_TYPE_SIZE, MODE_INT, 0);

  if (target == NULL_RTX)
    target = gen_reg_rtx (mode);

  op0 = expand_expr (arg0, subtarget, mode, 0);
  if (GET_MODE (op0) != mode)
    op0 = convert_to_mode (mode, op0, 0);
  op1 = expand_expr (arg1, 0, mode2, 0);
  if (GET_MODE (op1) != mode2)
    op1 = convert_to_mode (mode2, op1, 0);

  target = emit_library_call_value (powi_optab->handlers[(int) mode].libfunc,
				    target, LCT_CONST_MAKE_BLOCK, mode, 2,
				    op0, mode, op1, mode2);

  return target;
}

/* Expand expression EXP which is a call to the strlen builtin.  Return 0
   if we failed the caller should emit a normal call, otherwise
   try to get the result in TARGET, if convenient.  */

static rtx
expand_builtin_strlen (tree arglist, rtx target,
		       enum machine_mode target_mode)
{
  if (!validate_arglist (arglist, POINTER_TYPE, VOID_TYPE))
    return 0;
  else
    {
      rtx pat;
      tree len, src = TREE_VALUE (arglist);
      rtx result, src_reg, char_rtx, before_strlen;
      enum machine_mode insn_mode = target_mode, char_mode;
      enum insn_code icode = CODE_FOR_nothing;
      int align;

      /* If the length can be computed at compile-time, return it.  */
      len = c_strlen (src, 0);
      if (len)
	return expand_expr (len, target, target_mode, EXPAND_NORMAL);

      /* If the length can be computed at compile-time and is constant
	 integer, but there are side-effects in src, evaluate
	 src for side-effects, then return len.
	 E.g. x = strlen (i++ ? "xfoo" + 1 : "bar");
	 can be optimized into: i++; x = 3;  */
      len = c_strlen (src, 1);
      if (len && TREE_CODE (len) == INTEGER_CST)
	{
	  expand_expr (src, const0_rtx, VOIDmode, EXPAND_NORMAL);
	  return expand_expr (len, target, target_mode, EXPAND_NORMAL);
	}

      align = get_pointer_alignment (src, BIGGEST_ALIGNMENT) / BITS_PER_UNIT;

      /* If SRC is not a pointer type, don't do this operation inline.  */
      if (align == 0)
	return 0;

      /* Bail out if we can't compute strlen in the right mode.  */
      while (insn_mode != VOIDmode)
	{
	  icode = strlen_optab->handlers[(int) insn_mode].insn_code;
	  if (icode != CODE_FOR_nothing)
	    break;

	  insn_mode = GET_MODE_WIDER_MODE (insn_mode);
	}
      if (insn_mode == VOIDmode)
	return 0;

      /* Make a place to write the result of the instruction.  */
      result = target;
      if (! (result != 0
	     && REG_P (result)
	     && GET_MODE (result) == insn_mode
	     && REGNO (result) >= FIRST_PSEUDO_REGISTER))
	result = gen_reg_rtx (insn_mode);

      /* Make a place to hold the source address.  We will not expand
	 the actual source until we are sure that the expansion will
	 not fail -- there are trees that cannot be expanded twice.  */
      src_reg = gen_reg_rtx (Pmode);

      /* Mark the beginning of the strlen sequence so we can emit the
	 source operand later.  */
      before_strlen = get_last_insn ();

      char_rtx = const0_rtx;
      char_mode = insn_data[(int) icode].operand[2].mode;
      if (! (*insn_data[(int) icode].operand[2].predicate) (char_rtx,
							    char_mode))
	char_rtx = copy_to_mode_reg (char_mode, char_rtx);

      pat = GEN_FCN (icode) (result, gen_rtx_MEM (BLKmode, src_reg),
			     char_rtx, GEN_INT (align));
      if (! pat)
	return 0;
      emit_insn (pat);

      /* Now that we are assured of success, expand the source.  */
      start_sequence ();
      pat = expand_expr (src, src_reg, ptr_mode, EXPAND_NORMAL);
      if (pat != src_reg)
	emit_move_insn (src_reg, pat);
      pat = get_insns ();
      end_sequence ();

      if (before_strlen)
	emit_insn_after (pat, before_strlen);
      else
	emit_insn_before (pat, get_insns ());

      /* Return the value in the proper mode for this function.  */
      if (GET_MODE (result) == target_mode)
	target = result;
      else if (target != 0)
	convert_move (target, result, 0);
      else
	target = convert_to_mode (target_mode, result, 0);

      return target;
    }
}

/* Expand a call to the strstr builtin.  Return 0 if we failed the
   caller should emit a normal call, otherwise try to get the result
   in TARGET, if convenient (and in mode MODE if that's convenient).  */

static rtx
expand_builtin_strstr (tree arglist, tree type, rtx target, enum machine_mode mode)
{
  if (validate_arglist (arglist, POINTER_TYPE, POINTER_TYPE, VOID_TYPE))
    {
      tree result = fold_builtin_strstr (arglist, type);
      if (result)
	return expand_expr (result, target, mode, EXPAND_NORMAL);
    }
  return 0;
}

/* Expand a call to the strchr builtin.  Return 0 if we failed the
   caller should emit a normal call, otherwise try to get the result
   in TARGET, if convenient (and in mode MODE if that's convenient).  */

static rtx
expand_builtin_strchr (tree arglist, tree type, rtx target, enum machine_mode mode)
{
  if (validate_arglist (arglist, POINTER_TYPE, INTEGER_TYPE, VOID_TYPE))
    {
      tree result = fold_builtin_strchr (arglist, type);
      if (result)
	return expand_expr (result, target, mode, EXPAND_NORMAL);

      /* FIXME: Should use strchrM optab so that ports can optimize this.  */
    }
  return 0;
}

/* Expand a call to the strrchr builtin.  Return 0 if we failed the
   caller should emit a normal call, otherwise try to get the result
   in TARGET, if convenient (and in mode MODE if that's convenient).  */

static rtx
expand_builtin_strrchr (tree arglist, tree type, rtx target, enum machine_mode mode)
{
  if (validate_arglist (arglist, POINTER_TYPE, INTEGER_TYPE, VOID_TYPE))
    {
      tree result = fold_builtin_strrchr (arglist, type);
      if (result)
	return expand_expr (result, target, mode, EXPAND_NORMAL);
    }
  return 0;
}

/* Expand a call to the strpbrk builtin.  Return 0 if we failed the
   caller should emit a normal call, otherwise try to get the result
   in TARGET, if convenient (and in mode MODE if that's convenient).  */

static rtx
expand_builtin_strpbrk (tree arglist, tree type, rtx target, enum machine_mode mode)
{
  if (validate_arglist (arglist, POINTER_TYPE, POINTER_TYPE, VOID_TYPE))
    {
      tree result = fold_builtin_strpbrk (arglist, type);
      if (result)
	return expand_expr (result, target, mode, EXPAND_NORMAL);
    }
  return 0;
}

/* Callback routine for store_by_pieces.  Read GET_MODE_BITSIZE (MODE)
   bytes from constant string DATA + OFFSET and return it as target
   constant.  */

static rtx
builtin_memcpy_read_str (void *data, HOST_WIDE_INT offset,
			 enum machine_mode mode)
{
  const char *str = (const char *) data;

  gcc_assert (offset >= 0
	      && ((unsigned HOST_WIDE_INT) offset + GET_MODE_SIZE (mode)
		  <= strlen (str) + 1));

  return c_readstr (str + offset, mode);
}

/* Expand a call to the memcpy builtin, with arguments in ARGLIST.
   Return 0 if we failed, the caller should emit a normal call,
   otherwise try to get the result in TARGET, if convenient (and in
   mode MODE if that's convenient).  */
static rtx
expand_builtin_memcpy (tree exp, rtx target, enum machine_mode mode)
{
  tree fndecl = get_callee_fndecl (exp);
  tree arglist = TREE_OPERAND (exp, 1);
  if (!validate_arglist (arglist,
			 POINTER_TYPE, POINTER_TYPE, INTEGER_TYPE, VOID_TYPE))
    return 0;
  else
    {
      tree dest = TREE_VALUE (arglist);
      tree src = TREE_VALUE (TREE_CHAIN (arglist));
      tree len = TREE_VALUE (TREE_CHAIN (TREE_CHAIN (arglist)));
      const char *src_str;
      unsigned int src_align = get_pointer_alignment (src, BIGGEST_ALIGNMENT);
      unsigned int dest_align
	= get_pointer_alignment (dest, BIGGEST_ALIGNMENT);
      rtx dest_mem, src_mem, dest_addr, len_rtx;
      tree result = fold_builtin_memory_op (arglist, TREE_TYPE (TREE_TYPE (fndecl)),
					    false, /*endp=*/0);

      if (result)
	{
	  while (TREE_CODE (result) == COMPOUND_EXPR)
	    {
	      expand_expr (TREE_OPERAND (result, 0), const0_rtx, VOIDmode,
			   EXPAND_NORMAL);
	      result = TREE_OPERAND (result, 1);
	    }
	  return expand_expr (result, target, mode, EXPAND_NORMAL);
	}

      /* If DEST is not a pointer type, call the normal function.  */
      if (dest_align == 0)
	return 0;

      /* If either SRC is not a pointer type, don't do this
	 operation in-line.  */
      if (src_align == 0)
	return 0;

      dest_mem = get_memory_rtx (dest, len);
      set_mem_align (dest_mem, dest_align);
      len_rtx = expand_normal (len);
      src_str = c_getstr (src);

      /* If SRC is a string constant and block move would be done
	 by pieces, we can avoid loading the string from memory
	 and only stored the computed constants.  */
      if (src_str
	  && GET_CODE (len_rtx) == CONST_INT
	  && (unsigned HOST_WIDE_INT) INTVAL (len_rtx) <= strlen (src_str) + 1
	  && can_store_by_pieces (INTVAL (len_rtx), builtin_memcpy_read_str,
				  (void *) src_str, dest_align))
	{
	  dest_mem = store_by_pieces (dest_mem, INTVAL (len_rtx),
				      builtin_memcpy_read_str,
				      (void *) src_str, dest_align, 0);
	  dest_mem = force_operand (XEXP (dest_mem, 0), NULL_RTX);
	  dest_mem = convert_memory_address (ptr_mode, dest_mem);
	  return dest_mem;
	}

      src_mem = get_memory_rtx (src, len);
      set_mem_align (src_mem, src_align);

      /* Copy word part most expediently.  */
      dest_addr = emit_block_move (dest_mem, src_mem, len_rtx,
				   CALL_EXPR_TAILCALL (exp)
				   ? BLOCK_OP_TAILCALL : BLOCK_OP_NORMAL);

      if (dest_addr == 0)
	{
	  dest_addr = force_operand (XEXP (dest_mem, 0), NULL_RTX);
	  dest_addr = convert_memory_address (ptr_mode, dest_addr);
	}
      return dest_addr;
    }
}

/* Expand a call to the mempcpy builtin, with arguments in ARGLIST.
   Return 0 if we failed; the caller should emit a normal call,
   otherwise try to get the result in TARGET, if convenient (and in
   mode MODE if that's convenient).  If ENDP is 0 return the
   destination pointer, if ENDP is 1 return the end pointer ala
   mempcpy, and if ENDP is 2 return the end pointer minus one ala
   stpcpy.  */

static rtx
expand_builtin_mempcpy (tree arglist, tree type, rtx target, enum machine_mode mode,
			int endp)
{
  if (!validate_arglist (arglist,
			 POINTER_TYPE, POINTER_TYPE, INTEGER_TYPE, VOID_TYPE))
    return 0;
  /* If return value is ignored, transform mempcpy into memcpy.  */
  else if (target == const0_rtx)
    {
      tree fn = implicit_built_in_decls[BUILT_IN_MEMCPY];

      if (!fn)
	return 0;

      return expand_expr (build_function_call_expr (fn, arglist),
			  target, mode, EXPAND_NORMAL);
    }
  else
    {
      tree dest = TREE_VALUE (arglist);
      tree src = TREE_VALUE (TREE_CHAIN (arglist));
      tree len = TREE_VALUE (TREE_CHAIN (TREE_CHAIN (arglist)));
      const char *src_str;
      unsigned int src_align = get_pointer_alignment (src, BIGGEST_ALIGNMENT);
      unsigned int dest_align
	= get_pointer_alignment (dest, BIGGEST_ALIGNMENT);
      rtx dest_mem, src_mem, len_rtx;
      tree result = fold_builtin_memory_op (arglist, type, false, endp);

      if (result)
	{
	  while (TREE_CODE (result) == COMPOUND_EXPR)
	    {
	      expand_expr (TREE_OPERAND (result, 0), const0_rtx, VOIDmode,
			   EXPAND_NORMAL);
	      result = TREE_OPERAND (result, 1);
	    }
	  return expand_expr (result, target, mode, EXPAND_NORMAL);
	}

      /* If either SRC or DEST is not a pointer type, don't do this
	 operation in-line.  */
      if (dest_align == 0 || src_align == 0)
	return 0;

      /* If LEN is not constant, call the normal function.  */
      if (! host_integerp (len, 1))
	return 0;

      len_rtx = expand_normal (len);
      src_str = c_getstr (src);

      /* If SRC is a string constant and block move would be done
	 by pieces, we can avoid loading the string from memory
	 and only stored the computed constants.  */
      if (src_str
	  && GET_CODE (len_rtx) == CONST_INT
	  && (unsigned HOST_WIDE_INT) INTVAL (len_rtx) <= strlen (src_str) + 1
	  && can_store_by_pieces (INTVAL (len_rtx), builtin_memcpy_read_str,
				  (void *) src_str, dest_align))
	{
	  dest_mem = get_memory_rtx (dest, len);
	  set_mem_align (dest_mem, dest_align);
	  dest_mem = store_by_pieces (dest_mem, INTVAL (len_rtx),
				      builtin_memcpy_read_str,
				      (void *) src_str, dest_align, endp);
	  dest_mem = force_operand (XEXP (dest_mem, 0), NULL_RTX);
	  dest_mem = convert_memory_address (ptr_mode, dest_mem);
	  return dest_mem;
	}

      if (GET_CODE (len_rtx) == CONST_INT
	  && can_move_by_pieces (INTVAL (len_rtx),
				 MIN (dest_align, src_align)))
	{
	  dest_mem = get_memory_rtx (dest, len);
	  set_mem_align (dest_mem, dest_align);
	  src_mem = get_memory_rtx (src, len);
	  set_mem_align (src_mem, src_align);
	  dest_mem = move_by_pieces (dest_mem, src_mem, INTVAL (len_rtx),
				     MIN (dest_align, src_align), endp);
	  dest_mem = force_operand (XEXP (dest_mem, 0), NULL_RTX);
	  dest_mem = convert_memory_address (ptr_mode, dest_mem);
	  return dest_mem;
	}

      return 0;
    }
}

/* Expand expression EXP, which is a call to the memmove builtin.  Return 0
   if we failed; the caller should emit a normal call.  */

static rtx
expand_builtin_memmove (tree arglist, tree type, rtx target,
			enum machine_mode mode, tree orig_exp)
{
  if (!validate_arglist (arglist,
			 POINTER_TYPE, POINTER_TYPE, INTEGER_TYPE, VOID_TYPE))
    return 0;
  else
    {
      tree dest = TREE_VALUE (arglist);
      tree src = TREE_VALUE (TREE_CHAIN (arglist));
      tree len = TREE_VALUE (TREE_CHAIN (TREE_CHAIN (arglist)));

      unsigned int src_align = get_pointer_alignment (src, BIGGEST_ALIGNMENT);
      unsigned int dest_align
	= get_pointer_alignment (dest, BIGGEST_ALIGNMENT);
      tree result = fold_builtin_memory_op (arglist, type, false, /*endp=*/3);

      if (result)
	{
	  while (TREE_CODE (result) == COMPOUND_EXPR)
	    {
	      expand_expr (TREE_OPERAND (result, 0), const0_rtx, VOIDmode,
			   EXPAND_NORMAL);
	      result = TREE_OPERAND (result, 1);
	    }
	  return expand_expr (result, target, mode, EXPAND_NORMAL);
	}

      /* If DEST is not a pointer type, call the normal function.  */
      if (dest_align == 0)
	return 0;

      /* If either SRC is not a pointer type, don't do this
	 operation in-line.  */
      if (src_align == 0)
	return 0;

      /* If src is categorized for a readonly section we can use
	 normal memcpy.  */
      if (readonly_data_expr (src))
	{
	  tree fn = implicit_built_in_decls[BUILT_IN_MEMCPY];
	  if (!fn)
	    return 0;
	  fn = build_function_call_expr (fn, arglist);
	  if (TREE_CODE (fn) == CALL_EXPR)
	    CALL_EXPR_TAILCALL (fn) = CALL_EXPR_TAILCALL (orig_exp);
	  return expand_expr (fn, target, mode, EXPAND_NORMAL);
	}

      /* If length is 1 and we can expand memcpy call inline,
	 it is ok to use memcpy as well.  */
      if (integer_onep (len))
	{
#if defined(SUBWORD_ACCESS_P)
	  if (SUBWORD_ACCESS_P
	      || (src_align >= BIGGEST_ALIGNMENT
		  && dest_align >= BIGGEST_ALIGNMENT))
	    {
#endif
	      rtx ret = expand_builtin_mempcpy (arglist, type, target, mode,
						/*endp=*/0);
	      if (ret)
		return ret;
#if defined(SUBWORD_ACCESS_P)
	    }
#endif
	}

      /* Otherwise, call the normal function.  */
      return 0;
   }
}

/* Expand expression EXP, which is a call to the bcopy builtin.  Return 0
   if we failed the caller should emit a normal call.  */

static rtx
expand_builtin_bcopy (tree exp)
{
  tree arglist = TREE_OPERAND (exp, 1);
  tree type = TREE_TYPE (exp);
  tree src, dest, size, newarglist;

  if (!validate_arglist (arglist,
			 POINTER_TYPE, POINTER_TYPE, INTEGER_TYPE, VOID_TYPE))
    return NULL_RTX;

  src = TREE_VALUE (arglist);
  dest = TREE_VALUE (TREE_CHAIN (arglist));
  size = TREE_VALUE (TREE_CHAIN (TREE_CHAIN (arglist)));

  /* New argument list transforming bcopy(ptr x, ptr y, int z) to
     memmove(ptr y, ptr x, size_t z).   This is done this way
     so that if it isn't expanded inline, we fallback to
     calling bcopy instead of memmove.  */

  newarglist = build_tree_list (NULL_TREE, fold_convert (sizetype, size));
  newarglist = tree_cons (NULL_TREE, src, newarglist);
  newarglist = tree_cons (NULL_TREE, dest, newarglist);

  return expand_builtin_memmove (newarglist, type, const0_rtx, VOIDmode, exp);
}

#ifndef HAVE_movstr
# define HAVE_movstr 0
# define CODE_FOR_movstr CODE_FOR_nothing
#endif

/* Expand into a movstr instruction, if one is available.  Return 0 if
   we failed, the caller should emit a normal call, otherwise try to
   get the result in TARGET, if convenient.  If ENDP is 0 return the
   destination pointer, if ENDP is 1 return the end pointer ala
   mempcpy, and if ENDP is 2 return the end pointer minus one ala
   stpcpy.  */

static rtx
expand_movstr (tree dest, tree src, rtx target, int endp)
{
  rtx end;
  rtx dest_mem;
  rtx src_mem;
  rtx insn;
  const struct insn_data * data;

  if (!HAVE_movstr)
    return 0;

  dest_mem = get_memory_rtx (dest, NULL);
  src_mem = get_memory_rtx (src, NULL);
  if (!endp)
    {
      target = force_reg (Pmode, XEXP (dest_mem, 0));
      dest_mem = replace_equiv_address (dest_mem, target);
      end = gen_reg_rtx (Pmode);
    }
  else
    {
      if (target == 0 || target == const0_rtx)
	{
	  end = gen_reg_rtx (Pmode);
	  if (target == 0)
	    target = end;
	}
      else
	end = target;
    }

  data = insn_data + CODE_FOR_movstr;

  if (data->operand[0].mode != VOIDmode)
    end = gen_lowpart (data->operand[0].mode, end);

  insn = data->genfun (end, dest_mem, src_mem);

  gcc_assert (insn);

  emit_insn (insn);

  /* movstr is supposed to set end to the address of the NUL
     terminator.  If the caller requested a mempcpy-like return value,
     adjust it.  */
  if (endp == 1 && target != const0_rtx)
    {
      rtx tem = plus_constant (gen_lowpart (GET_MODE (target), end), 1);
      emit_move_insn (target, force_operand (tem, NULL_RTX));
    }

  return target;
}

/* Expand expression EXP, which is a call to the strcpy builtin.  Return 0
   if we failed the caller should emit a normal call, otherwise try to get
   the result in TARGET, if convenient (and in mode MODE if that's
   convenient).  */

static rtx
expand_builtin_strcpy (tree fndecl, tree arglist, rtx target, enum machine_mode mode)
{
  if (validate_arglist (arglist, POINTER_TYPE, POINTER_TYPE, VOID_TYPE))
    {
      tree result = fold_builtin_strcpy (fndecl, arglist, 0);
      if (result)
	{
	  while (TREE_CODE (result) == COMPOUND_EXPR)
	    {
	      expand_expr (TREE_OPERAND (result, 0), const0_rtx, VOIDmode,
			   EXPAND_NORMAL);
	      result = TREE_OPERAND (result, 1);
	    }
	  return expand_expr (result, target, mode, EXPAND_NORMAL);
	}

      return expand_movstr (TREE_VALUE (arglist),
			    TREE_VALUE (TREE_CHAIN (arglist)),
			    target, /*endp=*/0);
    }
  return 0;
}

/* Expand a call to the stpcpy builtin, with arguments in ARGLIST.
   Return 0 if we failed the caller should emit a normal call,
   otherwise try to get the result in TARGET, if convenient (and in
   mode MODE if that's convenient).  */

static rtx
expand_builtin_stpcpy (tree exp, rtx target, enum machine_mode mode)
{
  tree arglist = TREE_OPERAND (exp, 1);
  /* If return value is ignored, transform stpcpy into strcpy.  */
  if (target == const0_rtx)
    {
      tree fn = implicit_built_in_decls[BUILT_IN_STRCPY];
#ifndef NO_UNSAFE_BUILTINS
      if (!fn)
#endif
	return 0;

      return expand_expr (build_function_call_expr (fn, arglist),
			  target, mode, EXPAND_NORMAL);
    }

  if (!validate_arglist (arglist, POINTER_TYPE, POINTER_TYPE, VOID_TYPE))
    return 0;
  else
    {
      tree dst, src, len, lenp1;
      tree narglist;
      rtx ret;

      /* Ensure we get an actual string whose length can be evaluated at
	 compile-time, not an expression containing a string.  This is
	 because the latter will potentially produce pessimized code
	 when used to produce the return value.  */
      src = TREE_VALUE (TREE_CHAIN (arglist));
      if (! c_getstr (src) || ! (len = c_strlen (src, 0)))
	return expand_movstr (TREE_VALUE (arglist),
			      TREE_VALUE (TREE_CHAIN (arglist)),
			      target, /*endp=*/2);

      dst = TREE_VALUE (arglist);
      lenp1 = size_binop (PLUS_EXPR, len, ssize_int (1));
      narglist = build_tree_list (NULL_TREE, lenp1);
      narglist = tree_cons (NULL_TREE, src, narglist);
      narglist = tree_cons (NULL_TREE, dst, narglist);
      ret = expand_builtin_mempcpy (narglist, TREE_TYPE (exp),
				    target, mode, /*endp=*/2);

      if (ret)
	return ret;

      if (TREE_CODE (len) == INTEGER_CST)
	{
	  rtx len_rtx = expand_normal (len);

	  if (GET_CODE (len_rtx) == CONST_INT)
	    {
	      ret = expand_builtin_strcpy (get_callee_fndecl (exp),
					   arglist, target, mode);

	      if (ret)
		{
		  if (! target)
		    {
		      if (mode != VOIDmode)
			target = gen_reg_rtx (mode);
		      else
			target = gen_reg_rtx (GET_MODE (ret));
		    }
		  if (GET_MODE (target) != GET_MODE (ret))
		    ret = gen_lowpart (GET_MODE (target), ret);

		  ret = plus_constant (ret, INTVAL (len_rtx));
		  ret = emit_move_insn (target, force_operand (ret, NULL_RTX));
		  gcc_assert (ret);

		  return target;
		}
	    }
	}

      return expand_movstr (TREE_VALUE (arglist),
			    TREE_VALUE (TREE_CHAIN (arglist)),
			    target, /*endp=*/2);
    }
}

/* Callback routine for store_by_pieces.  Read GET_MODE_BITSIZE (MODE)
   bytes from constant string DATA + OFFSET and return it as target
   constant.  */

static rtx
builtin_strncpy_read_str (void *data, HOST_WIDE_INT offset,
			  enum machine_mode mode)
{
  const char *str = (const char *) data;

  if ((unsigned HOST_WIDE_INT) offset > strlen (str))
    return const0_rtx;

  return c_readstr (str + offset, mode);
}

/* Expand expression EXP, which is a call to the strncpy builtin.  Return 0
   if we failed the caller should emit a normal call.  */

static rtx
expand_builtin_strncpy (tree exp, rtx target, enum machine_mode mode)
{
  tree fndecl = get_callee_fndecl (exp);
  tree arglist = TREE_OPERAND (exp, 1);
  if (validate_arglist (arglist,
			POINTER_TYPE, POINTER_TYPE, INTEGER_TYPE, VOID_TYPE))
    {
      tree slen = c_strlen (TREE_VALUE (TREE_CHAIN (arglist)), 1);
      tree len = TREE_VALUE (TREE_CHAIN (TREE_CHAIN (arglist)));
      tree result = fold_builtin_strncpy (fndecl, arglist, slen);

      if (result)
	{
	  while (TREE_CODE (result) == COMPOUND_EXPR)
	    {
	      expand_expr (TREE_OPERAND (result, 0), const0_rtx, VOIDmode,
			   EXPAND_NORMAL);
	      result = TREE_OPERAND (result, 1);
	    }
	  return expand_expr (result, target, mode, EXPAND_NORMAL);
	}

      /* We must be passed a constant len and src parameter.  */
      if (!host_integerp (len, 1) || !slen || !host_integerp (slen, 1))
	return 0;

      slen = size_binop (PLUS_EXPR, slen, ssize_int (1));

      /* We're required to pad with trailing zeros if the requested
	 len is greater than strlen(s2)+1.  In that case try to
	 use store_by_pieces, if it fails, punt.  */
      if (tree_int_cst_lt (slen, len))
	{
	  tree dest = TREE_VALUE (arglist);
	  unsigned int dest_align
	    = get_pointer_alignment (dest, BIGGEST_ALIGNMENT);
	  const char *p = c_getstr (TREE_VALUE (TREE_CHAIN (arglist)));
	  rtx dest_mem;

	  if (!p || dest_align == 0 || !host_integerp (len, 1)
	      || !can_store_by_pieces (tree_low_cst (len, 1),
				       builtin_strncpy_read_str,
				       (void *) p, dest_align))
	    return 0;

	  dest_mem = get_memory_rtx (dest, len);
	  store_by_pieces (dest_mem, tree_low_cst (len, 1),
			   builtin_strncpy_read_str,
			   (void *) p, dest_align, 0);
	  dest_mem = force_operand (XEXP (dest_mem, 0), NULL_RTX);
	  dest_mem = convert_memory_address (ptr_mode, dest_mem);
	  return dest_mem;
	}
    }
  return 0;
}

/* Callback routine for store_by_pieces.  Read GET_MODE_BITSIZE (MODE)
   bytes from constant string DATA + OFFSET and return it as target
   constant.  */

static rtx
builtin_memset_read_str (void *data, HOST_WIDE_INT offset ATTRIBUTE_UNUSED,
			 enum machine_mode mode)
{
  const char *c = (const char *) data;
  char *p = alloca (GET_MODE_SIZE (mode));

  memset (p, *c, GET_MODE_SIZE (mode));

  return c_readstr (p, mode);
}

/* Callback routine for store_by_pieces.  Return the RTL of a register
   containing GET_MODE_SIZE (MODE) consecutive copies of the unsigned
   char value given in the RTL register data.  For example, if mode is
   4 bytes wide, return the RTL for 0x01010101*data.  */

static rtx
builtin_memset_gen_str (void *data, HOST_WIDE_INT offset ATTRIBUTE_UNUSED,
			enum machine_mode mode)
{
  rtx target, coeff;
  size_t size;
  char *p;

  size = GET_MODE_SIZE (mode);
  if (size == 1)
    return (rtx) data;

  p = alloca (size);
  memset (p, 1, size);
  coeff = c_readstr (p, mode);

  target = convert_to_mode (mode, (rtx) data, 1);
  target = expand_mult (mode, target, coeff, NULL_RTX, 1);
  return force_reg (mode, target);
}

/* Expand expression EXP, which is a call to the memset builtin.  Return 0
   if we failed the caller should emit a normal call, otherwise try to get
   the result in TARGET, if convenient (and in mode MODE if that's
   convenient).  */

static rtx
expand_builtin_memset (tree arglist, rtx target, enum machine_mode mode,
		       tree orig_exp)
{
  if (!validate_arglist (arglist,
			 POINTER_TYPE, INTEGER_TYPE, INTEGER_TYPE, VOID_TYPE))
    return 0;
  else
    {
      tree dest = TREE_VALUE (arglist);
      tree val = TREE_VALUE (TREE_CHAIN (arglist));
      tree len = TREE_VALUE (TREE_CHAIN (TREE_CHAIN (arglist)));
      tree fndecl, fn;
      enum built_in_function fcode;
      char c;
      unsigned int dest_align;
      rtx dest_mem, dest_addr, len_rtx;

      dest_align = get_pointer_alignment (dest, BIGGEST_ALIGNMENT);

      /* If DEST is not a pointer type, don't do this
	 operation in-line.  */
      if (dest_align == 0)
	return 0;

      /* If the LEN parameter is zero, return DEST.  */
      if (integer_zerop (len))
	{
	  /* Evaluate and ignore VAL in case it has side-effects.  */
	  expand_expr (val, const0_rtx, VOIDmode, EXPAND_NORMAL);
	  return expand_expr (dest, target, mode, EXPAND_NORMAL);
	}

      /* Stabilize the arguments in case we fail.  */
      dest = builtin_save_expr (dest);
      val = builtin_save_expr (val);
      len = builtin_save_expr (len);

      len_rtx = expand_normal (len);
      dest_mem = get_memory_rtx (dest, len);

      if (TREE_CODE (val) != INTEGER_CST)
	{
	  rtx val_rtx;

	  val_rtx = expand_normal (val);
	  val_rtx = convert_to_mode (TYPE_MODE (unsigned_char_type_node),
				     val_rtx, 0);

	  /* Assume that we can memset by pieces if we can store the
	   * the coefficients by pieces (in the required modes).
	   * We can't pass builtin_memset_gen_str as that emits RTL.  */
	  c = 1;
	  if (host_integerp (len, 1)
	      && !(optimize_size && tree_low_cst (len, 1) > 1)
	      && can_store_by_pieces (tree_low_cst (len, 1),
				      builtin_memset_read_str, &c, dest_align))
	    {
	      val_rtx = force_reg (TYPE_MODE (unsigned_char_type_node),
				   val_rtx);
	      store_by_pieces (dest_mem, tree_low_cst (len, 1),
			       builtin_memset_gen_str, val_rtx, dest_align, 0);
	    }
	  else if (!set_storage_via_setmem (dest_mem, len_rtx, val_rtx,
					    dest_align))
	    goto do_libcall;

	  dest_mem = force_operand (XEXP (dest_mem, 0), NULL_RTX);
	  dest_mem = convert_memory_address (ptr_mode, dest_mem);
	  return dest_mem;
	}

      if (target_char_cast (val, &c))
	goto do_libcall;

      if (c)
	{
	  if (host_integerp (len, 1)
	      && !(optimize_size && tree_low_cst (len, 1) > 1)
	      && can_store_by_pieces (tree_low_cst (len, 1),
				      builtin_memset_read_str, &c, dest_align))
	    store_by_pieces (dest_mem, tree_low_cst (len, 1),
			     builtin_memset_read_str, &c, dest_align, 0);
	  else if (!set_storage_via_setmem (dest_mem, len_rtx, GEN_INT (c),
					    dest_align))
	    goto do_libcall;

	  dest_mem = force_operand (XEXP (dest_mem, 0), NULL_RTX);
	  dest_mem = convert_memory_address (ptr_mode, dest_mem);
	  return dest_mem;
	}

      set_mem_align (dest_mem, dest_align);
      dest_addr = clear_storage (dest_mem, len_rtx,
				 CALL_EXPR_TAILCALL (orig_exp)
				 ? BLOCK_OP_TAILCALL : BLOCK_OP_NORMAL);

      if (dest_addr == 0)
	{
	  dest_addr = force_operand (XEXP (dest_mem, 0), NULL_RTX);
	  dest_addr = convert_memory_address (ptr_mode, dest_addr);
	}

      return dest_addr;

    do_libcall:
      fndecl = get_callee_fndecl (orig_exp);
      fcode = DECL_FUNCTION_CODE (fndecl);
      gcc_assert (fcode == BUILT_IN_MEMSET || fcode == BUILT_IN_BZERO);
      arglist = build_tree_list (NULL_TREE, len);
      if (fcode == BUILT_IN_MEMSET)
	arglist = tree_cons (NULL_TREE, val, arglist);
      arglist = tree_cons (NULL_TREE, dest, arglist);
      fn = build_function_call_expr (fndecl, arglist);
      if (TREE_CODE (fn) == CALL_EXPR)
	CALL_EXPR_TAILCALL (fn) = CALL_EXPR_TAILCALL (orig_exp);
      return expand_call (fn, target, target == const0_rtx);
    }
}

/* Expand expression EXP, which is a call to the bzero builtin.  Return 0
   if we failed the caller should emit a normal call.  */

static rtx
expand_builtin_bzero (tree exp)
{
  tree arglist = TREE_OPERAND (exp, 1);
  tree dest, size, newarglist;

  if (!validate_arglist (arglist, POINTER_TYPE, INTEGER_TYPE, VOID_TYPE))
    return NULL_RTX;

  dest = TREE_VALUE (arglist);
  size = TREE_VALUE (TREE_CHAIN (arglist));

  /* New argument list transforming bzero(ptr x, int y) to
     memset(ptr x, int 0, size_t y).   This is done this way
     so that if it isn't expanded inline, we fallback to
     calling bzero instead of memset.  */

  newarglist = build_tree_list (NULL_TREE, fold_convert (sizetype, size));
  newarglist = tree_cons (NULL_TREE, integer_zero_node, newarglist);
  newarglist = tree_cons (NULL_TREE, dest, newarglist);

  return expand_builtin_memset (newarglist, const0_rtx, VOIDmode, exp);
}

/* Expand expression EXP, which is a call to the memcmp built-in function.
   ARGLIST is the argument list for this call.  Return 0 if we failed and the
   caller should emit a normal call, otherwise try to get the result in
   TARGET, if convenient (and in mode MODE, if that's convenient).  */

static rtx
expand_builtin_memcmp (tree exp ATTRIBUTE_UNUSED, tree arglist, rtx target,
		       enum machine_mode mode)
{
  if (!validate_arglist (arglist,
			 POINTER_TYPE, POINTER_TYPE, INTEGER_TYPE, VOID_TYPE))
    return 0;
  else
    {
      tree result = fold_builtin_memcmp (arglist);
      if (result)
	return expand_expr (result, target, mode, EXPAND_NORMAL);
    }

#if defined HAVE_cmpmemsi || defined HAVE_cmpstrnsi
  {
    tree arg1 = TREE_VALUE (arglist);
    tree arg2 = TREE_VALUE (TREE_CHAIN (arglist));
    tree len = TREE_VALUE (TREE_CHAIN (TREE_CHAIN (arglist)));
    rtx arg1_rtx, arg2_rtx, arg3_rtx;
    rtx result;
    rtx insn;

    int arg1_align
      = get_pointer_alignment (arg1, BIGGEST_ALIGNMENT) / BITS_PER_UNIT;
    int arg2_align
      = get_pointer_alignment (arg2, BIGGEST_ALIGNMENT) / BITS_PER_UNIT;
    enum machine_mode insn_mode;

#ifdef HAVE_cmpmemsi
    if (HAVE_cmpmemsi)
      insn_mode = insn_data[(int) CODE_FOR_cmpmemsi].operand[0].mode;
    else
#endif
#ifdef HAVE_cmpstrnsi
    if (HAVE_cmpstrnsi)
      insn_mode = insn_data[(int) CODE_FOR_cmpstrnsi].operand[0].mode;
    else
#endif
      return 0;

    /* If we don't have POINTER_TYPE, call the function.  */
    if (arg1_align == 0 || arg2_align == 0)
      return 0;

    /* Make a place to write the result of the instruction.  */
    result = target;
    if (! (result != 0
	   && REG_P (result) && GET_MODE (result) == insn_mode
	   && REGNO (result) >= FIRST_PSEUDO_REGISTER))
      result = gen_reg_rtx (insn_mode);

    arg1_rtx = get_memory_rtx (arg1, len);
    arg2_rtx = get_memory_rtx (arg2, len);
    arg3_rtx = expand_normal (len);

    /* Set MEM_SIZE as appropriate.  */
    if (GET_CODE (arg3_rtx) == CONST_INT)
      {
	set_mem_size (arg1_rtx, arg3_rtx);
	set_mem_size (arg2_rtx, arg3_rtx);
      }

#ifdef HAVE_cmpmemsi
    if (HAVE_cmpmemsi)
      insn = gen_cmpmemsi (result, arg1_rtx, arg2_rtx, arg3_rtx,
			   GEN_INT (MIN (arg1_align, arg2_align)));
    else
#endif
#ifdef HAVE_cmpstrnsi
    if (HAVE_cmpstrnsi)
      insn = gen_cmpstrnsi (result, arg1_rtx, arg2_rtx, arg3_rtx,
			    GEN_INT (MIN (arg1_align, arg2_align)));
    else
#endif
      gcc_unreachable ();

    if (insn)
      emit_insn (insn);
    else
      emit_library_call_value (memcmp_libfunc, result, LCT_PURE_MAKE_BLOCK,
			       TYPE_MODE (integer_type_node), 3,
			       XEXP (arg1_rtx, 0), Pmode,
			       XEXP (arg2_rtx, 0), Pmode,
			       convert_to_mode (TYPE_MODE (sizetype), arg3_rtx,
						TYPE_UNSIGNED (sizetype)),
			       TYPE_MODE (sizetype));

    /* Return the value in the proper mode for this function.  */
    mode = TYPE_MODE (TREE_TYPE (exp));
    if (GET_MODE (result) == mode)
      return result;
    else if (target != 0)
      {
	convert_move (target, result, 0);
	return target;
      }
    else
      return convert_to_mode (mode, result, 0);
  }
#endif

  return 0;
}

/* Expand expression EXP, which is a call to the strcmp builtin.  Return 0
   if we failed the caller should emit a normal call, otherwise try to get
   the result in TARGET, if convenient.  */

static rtx
expand_builtin_strcmp (tree exp, rtx target, enum machine_mode mode)
{
  tree arglist = TREE_OPERAND (exp, 1);

  if (!validate_arglist (arglist, POINTER_TYPE, POINTER_TYPE, VOID_TYPE))
    return 0;
  else
    {
      tree result = fold_builtin_strcmp (arglist);
      if (result)
	return expand_expr (result, target, mode, EXPAND_NORMAL);
    }

#if defined HAVE_cmpstrsi || defined HAVE_cmpstrnsi
  if (cmpstr_optab[SImode] != CODE_FOR_nothing
      || cmpstrn_optab[SImode] != CODE_FOR_nothing)
    {
      rtx arg1_rtx, arg2_rtx;
      rtx result, insn = NULL_RTX;
      tree fndecl, fn;

      tree arg1 = TREE_VALUE (arglist);
      tree arg2 = TREE_VALUE (TREE_CHAIN (arglist));
      int arg1_align
	= get_pointer_alignment (arg1, BIGGEST_ALIGNMENT) / BITS_PER_UNIT;
      int arg2_align
	= get_pointer_alignment (arg2, BIGGEST_ALIGNMENT) / BITS_PER_UNIT;

      /* If we don't have POINTER_TYPE, call the function.  */
      if (arg1_align == 0 || arg2_align == 0)
	return 0;

      /* Stabilize the arguments in case gen_cmpstr(n)si fail.  */
      arg1 = builtin_save_expr (arg1);
      arg2 = builtin_save_expr (arg2);

      arg1_rtx = get_memory_rtx (arg1, NULL);
      arg2_rtx = get_memory_rtx (arg2, NULL);

#ifdef HAVE_cmpstrsi
      /* Try to call cmpstrsi.  */
      if (HAVE_cmpstrsi)
	{
	  enum machine_mode insn_mode
	    = insn_data[(int) CODE_FOR_cmpstrsi].operand[0].mode;

	  /* Make a place to write the result of the instruction.  */
	  result = target;
	  if (! (result != 0
		 && REG_P (result) && GET_MODE (result) == insn_mode
		 && REGNO (result) >= FIRST_PSEUDO_REGISTER))
	    result = gen_reg_rtx (insn_mode);

	  insn = gen_cmpstrsi (result, arg1_rtx, arg2_rtx,
			       GEN_INT (MIN (arg1_align, arg2_align)));
	}
#endif
#ifdef HAVE_cmpstrnsi
      /* Try to determine at least one length and call cmpstrnsi.  */
      if (!insn && HAVE_cmpstrnsi)
	{
	  tree len;
	  rtx arg3_rtx;

	  enum machine_mode insn_mode
	    = insn_data[(int) CODE_FOR_cmpstrnsi].operand[0].mode;
	  tree len1 = c_strlen (arg1, 1);
	  tree len2 = c_strlen (arg2, 1);

	  if (len1)
	    len1 = size_binop (PLUS_EXPR, ssize_int (1), len1);
	  if (len2)
	    len2 = size_binop (PLUS_EXPR, ssize_int (1), len2);

	  /* If we don't have a constant length for the first, use the length
	     of the second, if we know it.  We don't require a constant for
	     this case; some cost analysis could be done if both are available
	     but neither is constant.  For now, assume they're equally cheap,
	     unless one has side effects.  If both strings have constant lengths,
	     use the smaller.  */

	  if (!len1)
	    len = len2;
	  else if (!len2)
	    len = len1;
	  else if (TREE_SIDE_EFFECTS (len1))
	    len = len2;
	  else if (TREE_SIDE_EFFECTS (len2))
	    len = len1;
	  else if (TREE_CODE (len1) != INTEGER_CST)
	    len = len2;
	  else if (TREE_CODE (len2) != INTEGER_CST)
	    len = len1;
	  else if (tree_int_cst_lt (len1, len2))
	    len = len1;
	  else
	    len = len2;

	  /* If both arguments have side effects, we cannot optimize.  */
	  if (!len || TREE_SIDE_EFFECTS (len))
	    goto do_libcall;

	  arg3_rtx = expand_normal (len);

	  /* Make a place to write the result of the instruction.  */
	  result = target;
	  if (! (result != 0
		 && REG_P (result) && GET_MODE (result) == insn_mode
		 && REGNO (result) >= FIRST_PSEUDO_REGISTER))
	    result = gen_reg_rtx (insn_mode);

	  insn = gen_cmpstrnsi (result, arg1_rtx, arg2_rtx, arg3_rtx,
				GEN_INT (MIN (arg1_align, arg2_align)));
	}
#endif

      if (insn)
	{
	  emit_insn (insn);

	  /* Return the value in the proper mode for this function.  */
	  mode = TYPE_MODE (TREE_TYPE (exp));
	  if (GET_MODE (result) == mode)
	    return result;
	  if (target == 0)
	    return convert_to_mode (mode, result, 0);
	  convert_move (target, result, 0);
	  return target;
	}

      /* Expand the library call ourselves using a stabilized argument
	 list to avoid re-evaluating the function's arguments twice.  */
#ifdef HAVE_cmpstrnsi
    do_libcall:
#endif
      arglist = build_tree_list (NULL_TREE, arg2);
      arglist = tree_cons (NULL_TREE, arg1, arglist);
      fndecl = get_callee_fndecl (exp);
      fn = build_function_call_expr (fndecl, arglist);
      if (TREE_CODE (fn) == CALL_EXPR)
	CALL_EXPR_TAILCALL (fn) = CALL_EXPR_TAILCALL (exp);
      return expand_call (fn, target, target == const0_rtx);
    }
#endif
  return 0;
}

/* Expand expression EXP, which is a call to the strncmp builtin.  Return 0
   if we failed the caller should emit a normal call, otherwise try to get
   the result in TARGET, if convenient.  */

static rtx
expand_builtin_strncmp (tree exp, rtx target, enum machine_mode mode)
{
  tree arglist = TREE_OPERAND (exp, 1);

  if (!validate_arglist (arglist,
			 POINTER_TYPE, POINTER_TYPE, INTEGER_TYPE, VOID_TYPE))
    return 0;
  else
    {
      tree result = fold_builtin_strncmp (arglist);
      if (result)
	return expand_expr (result, target, mode, EXPAND_NORMAL);
    }

  /* If c_strlen can determine an expression for one of the string
     lengths, and it doesn't have side effects, then emit cmpstrnsi
     using length MIN(strlen(string)+1, arg3).  */
#ifdef HAVE_cmpstrnsi
  if (HAVE_cmpstrnsi)
  {
    tree arg1 = TREE_VALUE (arglist);
    tree arg2 = TREE_VALUE (TREE_CHAIN (arglist));
    tree arg3 = TREE_VALUE (TREE_CHAIN (TREE_CHAIN (arglist)));
    tree len, len1, len2;
    rtx arg1_rtx, arg2_rtx, arg3_rtx;
    rtx result, insn;
    tree fndecl, fn;

    int arg1_align
      = get_pointer_alignment (arg1, BIGGEST_ALIGNMENT) / BITS_PER_UNIT;
    int arg2_align
      = get_pointer_alignment (arg2, BIGGEST_ALIGNMENT) / BITS_PER_UNIT;
    enum machine_mode insn_mode
      = insn_data[(int) CODE_FOR_cmpstrnsi].operand[0].mode;

    len1 = c_strlen (arg1, 1);
    len2 = c_strlen (arg2, 1);

    if (len1)
      len1 = size_binop (PLUS_EXPR, ssize_int (1), len1);
    if (len2)
      len2 = size_binop (PLUS_EXPR, ssize_int (1), len2);

    /* If we don't have a constant length for the first, use the length
       of the second, if we know it.  We don't require a constant for
       this case; some cost analysis could be done if both are available
       but neither is constant.  For now, assume they're equally cheap,
       unless one has side effects.  If both strings have constant lengths,
       use the smaller.  */

    if (!len1)
      len = len2;
    else if (!len2)
      len = len1;
    else if (TREE_SIDE_EFFECTS (len1))
      len = len2;
    else if (TREE_SIDE_EFFECTS (len2))
      len = len1;
    else if (TREE_CODE (len1) != INTEGER_CST)
      len = len2;
    else if (TREE_CODE (len2) != INTEGER_CST)
      len = len1;
    else if (tree_int_cst_lt (len1, len2))
      len = len1;
    else
      len = len2;

    /* If both arguments have side effects, we cannot optimize.  */
    if (!len || TREE_SIDE_EFFECTS (len))
      return 0;

    /* The actual new length parameter is MIN(len,arg3).  */
    len = fold_build2 (MIN_EXPR, TREE_TYPE (len), len,
		       fold_convert (TREE_TYPE (len), arg3));

    /* If we don't have POINTER_TYPE, call the function.  */
    if (arg1_align == 0 || arg2_align == 0)
      return 0;

    /* Make a place to write the result of the instruction.  */
    result = target;
    if (! (result != 0
	   && REG_P (result) && GET_MODE (result) == insn_mode
	   && REGNO (result) >= FIRST_PSEUDO_REGISTER))
      result = gen_reg_rtx (insn_mode);

    /* Stabilize the arguments in case gen_cmpstrnsi fails.  */
    arg1 = builtin_save_expr (arg1);
    arg2 = builtin_save_expr (arg2);
    len = builtin_save_expr (len);

    arg1_rtx = get_memory_rtx (arg1, len);
    arg2_rtx = get_memory_rtx (arg2, len);
    arg3_rtx = expand_normal (len);
    insn = gen_cmpstrnsi (result, arg1_rtx, arg2_rtx, arg3_rtx,
			  GEN_INT (MIN (arg1_align, arg2_align)));
    if (insn)
      {
	emit_insn (insn);

	/* Return the value in the proper mode for this function.  */
	mode = TYPE_MODE (TREE_TYPE (exp));
	if (GET_MODE (result) == mode)
	  return result;
	if (target == 0)
	  return convert_to_mode (mode, result, 0);
	convert_move (target, result, 0);
	return target;
      }

    /* Expand the library call ourselves using a stabilized argument
       list to avoid re-evaluating the function's arguments twice.  */
    arglist = build_tree_list (NULL_TREE, len);
    arglist = tree_cons (NULL_TREE, arg2, arglist);
    arglist = tree_cons (NULL_TREE, arg1, arglist);
    fndecl = get_callee_fndecl (exp);
    fn = build_function_call_expr (fndecl, arglist);
    if (TREE_CODE (fn) == CALL_EXPR)
      CALL_EXPR_TAILCALL (fn) = CALL_EXPR_TAILCALL (exp);
    return expand_call (fn, target, target == const0_rtx);
  }
#endif
  return 0;
}

/* Expand expression EXP, which is a call to the strcat builtin.
   Return 0 if we failed the caller should emit a normal call,
   otherwise try to get the result in TARGET, if convenient.  */

static rtx
expand_builtin_strcat (tree fndecl, tree arglist, rtx target, enum machine_mode mode)
{
  if (!validate_arglist (arglist, POINTER_TYPE, POINTER_TYPE, VOID_TYPE))
    return 0;
  else
    {
      tree dst = TREE_VALUE (arglist),
      src = TREE_VALUE (TREE_CHAIN (arglist));
      const char *p = c_getstr (src);

      /* If the string length is zero, return the dst parameter.  */
      if (p && *p == '\0')
	return expand_expr (dst, target, mode, EXPAND_NORMAL);

      if (!optimize_size)
	{
	  /* See if we can store by pieces into (dst + strlen(dst)).  */
	  tree newsrc, newdst,
	    strlen_fn = implicit_built_in_decls[BUILT_IN_STRLEN];
	  rtx insns;

	  /* Stabilize the argument list.  */
	  newsrc = builtin_save_expr (src);
	  if (newsrc != src)
	    arglist = build_tree_list (NULL_TREE, newsrc);
	  else
	    arglist = TREE_CHAIN (arglist); /* Reusing arglist if safe.  */

	  dst = builtin_save_expr (dst);

	  start_sequence ();

	  /* Create strlen (dst).  */
	  newdst =
	    build_function_call_expr (strlen_fn,
				      build_tree_list (NULL_TREE, dst));
	  /* Create (dst + (cast) strlen (dst)).  */
	  newdst = fold_convert (TREE_TYPE (dst), newdst);
	  newdst = fold_build2 (PLUS_EXPR, TREE_TYPE (dst), dst, newdst);

	  newdst = builtin_save_expr (newdst);
	  arglist = tree_cons (NULL_TREE, newdst, arglist);

	  if (!expand_builtin_strcpy (fndecl, arglist, target, mode))
	    {
	      end_sequence (); /* Stop sequence.  */
	      return 0;
	    }

	  /* Output the entire sequence.  */
	  insns = get_insns ();
	  end_sequence ();
	  emit_insn (insns);

	  return expand_expr (dst, target, mode, EXPAND_NORMAL);
	}

      return 0;
    }
}

/* Expand expression EXP, which is a call to the strncat builtin.
   Return 0 if we failed the caller should emit a normal call,
   otherwise try to get the result in TARGET, if convenient.  */

static rtx
expand_builtin_strncat (tree arglist, rtx target, enum machine_mode mode)
{
  if (validate_arglist (arglist,
			POINTER_TYPE, POINTER_TYPE, INTEGER_TYPE, VOID_TYPE))
    {
      tree result = fold_builtin_strncat (arglist);
      if (result)
	return expand_expr (result, target, mode, EXPAND_NORMAL);
    }
  return 0;
}

/* Expand expression EXP, which is a call to the strspn builtin.
   Return 0 if we failed the caller should emit a normal call,
   otherwise try to get the result in TARGET, if convenient.  */

static rtx
expand_builtin_strspn (tree arglist, rtx target, enum machine_mode mode)
{
  if (validate_arglist (arglist, POINTER_TYPE, POINTER_TYPE, VOID_TYPE))
    {
      tree result = fold_builtin_strspn (arglist);
      if (result)
	return expand_expr (result, target, mode, EXPAND_NORMAL);
    }
  return 0;
}

/* Expand expression EXP, which is a call to the strcspn builtin.
   Return 0 if we failed the caller should emit a normal call,
   otherwise try to get the result in TARGET, if convenient.  */

static rtx
expand_builtin_strcspn (tree arglist, rtx target, enum machine_mode mode)
{
  if (validate_arglist (arglist, POINTER_TYPE, POINTER_TYPE, VOID_TYPE))
    {
      tree result = fold_builtin_strcspn (arglist);
      if (result)
	return expand_expr (result, target, mode, EXPAND_NORMAL);
    }
  return 0;
}

/* Expand a call to __builtin_saveregs, generating the result in TARGET,
   if that's convenient.  */

rtx
expand_builtin_saveregs (void)
{
  rtx val, seq;

  /* Don't do __builtin_saveregs more than once in a function.
     Save the result of the first call and reuse it.  */
  if (saveregs_value != 0)
    return saveregs_value;

  /* When this function is called, it means that registers must be
     saved on entry to this function.  So we migrate the call to the
     first insn of this function.  */

  start_sequence ();

  /* Do whatever the machine needs done in this case.  */
  val = targetm.calls.expand_builtin_saveregs ();

  seq = get_insns ();
  end_sequence ();

  saveregs_value = val;

  /* Put the insns after the NOTE that starts the function.  If this
     is inside a start_sequence, make the outer-level insn chain current, so
     the code is placed at the start of the function.  */
  push_topmost_sequence ();
  emit_insn_after (seq, entry_of_function ());
  pop_topmost_sequence ();

  return val;
}

/* __builtin_args_info (N) returns word N of the arg space info
   for the current function.  The number and meanings of words
   is controlled by the definition of CUMULATIVE_ARGS.  */

static rtx
expand_builtin_args_info (tree arglist)
{
  int nwords = sizeof (CUMULATIVE_ARGS) / sizeof (int);
  int *word_ptr = (int *) &current_function_args_info;

  gcc_assert (sizeof (CUMULATIVE_ARGS) % sizeof (int) == 0);

  if (arglist != 0)
    {
      if (!host_integerp (TREE_VALUE (arglist), 0))
	error ("argument of %<__builtin_args_info%> must be constant");
      else
	{
	  HOST_WIDE_INT wordnum = tree_low_cst (TREE_VALUE (arglist), 0);

	  if (wordnum < 0 || wordnum >= nwords)
	    error ("argument of %<__builtin_args_info%> out of range");
	  else
	    return GEN_INT (word_ptr[wordnum]);
	}
    }
  else
    error ("missing argument in %<__builtin_args_info%>");

  return const0_rtx;
}

/* Expand a call to __builtin_next_arg.  */

static rtx
expand_builtin_next_arg (void)
{
  /* Checking arguments is already done in fold_builtin_next_arg
     that must be called before this function.  */
  return expand_binop (Pmode, add_optab,
		       current_function_internal_arg_pointer,
		       current_function_arg_offset_rtx,
		       NULL_RTX, 0, OPTAB_LIB_WIDEN);
}

/* Make it easier for the backends by protecting the valist argument
   from multiple evaluations.  */

static tree
stabilize_va_list (tree valist, int needs_lvalue)
{
  if (TREE_CODE (va_list_type_node) == ARRAY_TYPE)
    {
      if (TREE_SIDE_EFFECTS (valist))
	valist = save_expr (valist);

      /* For this case, the backends will be expecting a pointer to
	 TREE_TYPE (va_list_type_node), but it's possible we've
	 actually been given an array (an actual va_list_type_node).
	 So fix it.  */
      if (TREE_CODE (TREE_TYPE (valist)) == ARRAY_TYPE)
	{
	  tree p1 = build_pointer_type (TREE_TYPE (va_list_type_node));
	  valist = build_fold_addr_expr_with_type (valist, p1);
	}
    }
  else
    {
      tree pt;

      if (! needs_lvalue)
	{
	  if (! TREE_SIDE_EFFECTS (valist))
	    return valist;

	  pt = build_pointer_type (va_list_type_node);
	  valist = fold_build1 (ADDR_EXPR, pt, valist);
	  TREE_SIDE_EFFECTS (valist) = 1;
	}

      if (TREE_SIDE_EFFECTS (valist))
	valist = save_expr (valist);
      valist = build_fold_indirect_ref (valist);
    }

  return valist;
}

/* The "standard" definition of va_list is void*.  */

tree
std_build_builtin_va_list (void)
{
  return ptr_type_node;
}

/* The "standard" implementation of va_start: just assign `nextarg' to
   the variable.  */

void
std_expand_builtin_va_start (tree valist, rtx nextarg)
{
  tree t;

  t = build2 (MODIFY_EXPR, TREE_TYPE (valist), valist,
	      make_tree (ptr_type_node, nextarg));
  TREE_SIDE_EFFECTS (t) = 1;

  expand_expr (t, const0_rtx, VOIDmode, EXPAND_NORMAL);
}

/* Expand ARGLIST, from a call to __builtin_va_start.  */

static rtx
expand_builtin_va_start (tree arglist)
{
  rtx nextarg;
  tree chain, valist;

  chain = TREE_CHAIN (arglist);

  if (!chain)
    {
      error ("too few arguments to function %<va_start%>");
      return const0_rtx;
    }

  if (fold_builtin_next_arg (chain))
    return const0_rtx;

  nextarg = expand_builtin_next_arg ();
  valist = stabilize_va_list (TREE_VALUE (arglist), 1);

#ifdef EXPAND_BUILTIN_VA_START
  EXPAND_BUILTIN_VA_START (valist, nextarg);
#else
  std_expand_builtin_va_start (valist, nextarg);
#endif

  return const0_rtx;
}

/* The "standard" implementation of va_arg: read the value from the
   current (padded) address and increment by the (padded) size.  */

tree
std_gimplify_va_arg_expr (tree valist, tree type, tree *pre_p, tree *post_p)
{
  tree addr, t, type_size, rounded_size, valist_tmp;
  unsigned HOST_WIDE_INT align, boundary;
  bool indirect;

#ifdef ARGS_GROW_DOWNWARD
  /* All of the alignment and movement below is for args-grow-up machines.
     As of 2004, there are only 3 ARGS_GROW_DOWNWARD targets, and they all
     implement their own specialized gimplify_va_arg_expr routines.  */
  gcc_unreachable ();
#endif

  indirect = pass_by_reference (NULL, TYPE_MODE (type), type, false);
  if (indirect)
    type = build_pointer_type (type);

  align = PARM_BOUNDARY / BITS_PER_UNIT;
  boundary = FUNCTION_ARG_BOUNDARY (TYPE_MODE (type), type) / BITS_PER_UNIT;

  /* Hoist the valist value into a temporary for the moment.  */
  valist_tmp = get_initialized_tmp_var (valist, pre_p, NULL);

  /* va_list pointer is aligned to PARM_BOUNDARY.  If argument actually
     requires greater alignment, we must perform dynamic alignment.  */
  if (boundary > align
      && !integer_zerop (TYPE_SIZE (type)))
    {
      t = fold_convert (TREE_TYPE (valist), size_int (boundary - 1));
      t = build2 (MODIFY_EXPR, TREE_TYPE (valist), valist_tmp,
		  build2 (PLUS_EXPR, TREE_TYPE (valist), valist_tmp, t));
      gimplify_and_add (t, pre_p);

      t = fold_convert (TREE_TYPE (valist), size_int (-boundary));
      t = build2 (MODIFY_EXPR, TREE_TYPE (valist), valist_tmp,
		  build2 (BIT_AND_EXPR, TREE_TYPE (valist), valist_tmp, t));
      gimplify_and_add (t, pre_p);
    }
  else
    boundary = align;

  /* If the actual alignment is less than the alignment of the type,
     adjust the type accordingly so that we don't assume strict alignment
     when deferencing the pointer.  */
  boundary *= BITS_PER_UNIT;
  if (boundary < TYPE_ALIGN (type))
    {
      type = build_variant_type_copy (type);
      TYPE_ALIGN (type) = boundary;
    }

  /* Compute the rounded size of the type.  */
  type_size = size_in_bytes (type);
  rounded_size = round_up (type_size, align);

  /* Reduce rounded_size so it's sharable with the postqueue.  */
  gimplify_expr (&rounded_size, pre_p, post_p, is_gimple_val, fb_rvalue);

  /* Get AP.  */
  addr = valist_tmp;
  if (PAD_VARARGS_DOWN && !integer_zerop (rounded_size))
    {
      /* Small args are padded downward.  */
      t = fold_build2 (GT_EXPR, sizetype, rounded_size, size_int (align));
      t = fold_build3 (COND_EXPR, sizetype, t, size_zero_node,
		       size_binop (MINUS_EXPR, rounded_size, type_size));
      t = fold_convert (TREE_TYPE (addr), t);
      addr = fold_build2 (PLUS_EXPR, TREE_TYPE (addr), addr, t);
    }

  /* Compute new value for AP.  */
  t = fold_convert (TREE_TYPE (valist), rounded_size);
  t = build2 (PLUS_EXPR, TREE_TYPE (valist), valist_tmp, t);
  t = build2 (MODIFY_EXPR, TREE_TYPE (valist), valist, t);
  gimplify_and_add (t, pre_p);

  addr = fold_convert (build_pointer_type (type), addr);

  if (indirect)
    addr = build_va_arg_indirect_ref (addr);

  return build_va_arg_indirect_ref (addr);
}

/* Build an indirect-ref expression over the given TREE, which represents a
   piece of a va_arg() expansion.  */
tree
build_va_arg_indirect_ref (tree addr)
{
  addr = build_fold_indirect_ref (addr);

  if (flag_mudflap) /* Don't instrument va_arg INDIRECT_REF.  */
    mf_mark (addr);

  return addr;
}

/* Return a dummy expression of type TYPE in order to keep going after an
   error.  */

static tree
dummy_object (tree type)
{
  tree t = build_int_cst (build_pointer_type (type), 0);
  return build1 (INDIRECT_REF, type, t);
}

/* Gimplify __builtin_va_arg, aka VA_ARG_EXPR, which is not really a
   builtin function, but a very special sort of operator.  */

enum gimplify_status
gimplify_va_arg_expr (tree *expr_p, tree *pre_p, tree *post_p)
{
  tree promoted_type, want_va_type, have_va_type;
  tree valist = TREE_OPERAND (*expr_p, 0);
  tree type = TREE_TYPE (*expr_p);
  tree t;

  /* Verify that valist is of the proper type.  */
  want_va_type = va_list_type_node;
  have_va_type = TREE_TYPE (valist);

  if (have_va_type == error_mark_node)
    return GS_ERROR;

  if (TREE_CODE (want_va_type) == ARRAY_TYPE)
    {
      /* If va_list is an array type, the argument may have decayed
	 to a pointer type, e.g. by being passed to another function.
	 In that case, unwrap both types so that we can compare the
	 underlying records.  */
      if (TREE_CODE (have_va_type) == ARRAY_TYPE
	  || POINTER_TYPE_P (have_va_type))
	{
	  want_va_type = TREE_TYPE (want_va_type);
	  have_va_type = TREE_TYPE (have_va_type);
	}
    }

  if (TYPE_MAIN_VARIANT (want_va_type) != TYPE_MAIN_VARIANT (have_va_type))
    {
      error ("first argument to %<va_arg%> not of type %<va_list%>");
      return GS_ERROR;
    }

  /* Generate a diagnostic for requesting data of a type that cannot
     be passed through `...' due to type promotion at the call site.  */
  else if ((promoted_type = lang_hooks.types.type_promotes_to (type))
	   != type)
    {
      static bool gave_help;

      /* Unfortunately, this is merely undefined, rather than a constraint
	 violation, so we cannot make this an error.  If this call is never
	 executed, the program is still strictly conforming.  */
      warning (0, "%qT is promoted to %qT when passed through %<...%>",
	       type, promoted_type);
      if (! gave_help)
	{
	  gave_help = true;
	  warning (0, "(so you should pass %qT not %qT to %<va_arg%>)",
		   promoted_type, type);
	}

      /* We can, however, treat "undefined" any way we please.
	 Call abort to encourage the user to fix the program.  */
      inform ("if this code is reached, the program will abort");
      t = build_function_call_expr (implicit_built_in_decls[BUILT_IN_TRAP],
				    NULL);
      append_to_statement_list (t, pre_p);

      /* This is dead code, but go ahead and finish so that the
	 mode of the result comes out right.  */
      *expr_p = dummy_object (type);
      return GS_ALL_DONE;
    }
  else
    {
      /* Make it easier for the backends by protecting the valist argument
	 from multiple evaluations.  */
      if (TREE_CODE (va_list_type_node) == ARRAY_TYPE)
	{
	  /* For this case, the backends will be expecting a pointer to
	     TREE_TYPE (va_list_type_node), but it's possible we've
	     actually been given an array (an actual va_list_type_node).
	     So fix it.  */
	  if (TREE_CODE (TREE_TYPE (valist)) == ARRAY_TYPE)
	    {
	      tree p1 = build_pointer_type (TREE_TYPE (va_list_type_node));
	      valist = build_fold_addr_expr_with_type (valist, p1);
	    }
	  gimplify_expr (&valist, pre_p, post_p, is_gimple_val, fb_rvalue);
	}
      else
	gimplify_expr (&valist, pre_p, post_p, is_gimple_min_lval, fb_lvalue);

      if (!targetm.gimplify_va_arg_expr)
	/* FIXME:Once most targets are converted we should merely
	   assert this is non-null.  */
	return GS_ALL_DONE;

      *expr_p = targetm.gimplify_va_arg_expr (valist, type, pre_p, post_p);
      return GS_OK;
    }
}

/* Expand ARGLIST, from a call to __builtin_va_end.  */

static rtx
expand_builtin_va_end (tree arglist)
{
  tree valist = TREE_VALUE (arglist);

  /* Evaluate for side effects, if needed.  I hate macros that don't
     do that.  */
  if (TREE_SIDE_EFFECTS (valist))
    expand_expr (valist, const0_rtx, VOIDmode, EXPAND_NORMAL);

  return const0_rtx;
}

/* Expand ARGLIST, from a call to __builtin_va_copy.  We do this as a
   builtin rather than just as an assignment in stdarg.h because of the
   nastiness of array-type va_list types.  */

static rtx
expand_builtin_va_copy (tree arglist)
{
  tree dst, src, t;

  dst = TREE_VALUE (arglist);
  src = TREE_VALUE (TREE_CHAIN (arglist));

  dst = stabilize_va_list (dst, 1);
  src = stabilize_va_list (src, 0);

  if (TREE_CODE (va_list_type_node) != ARRAY_TYPE)
    {
      t = build2 (MODIFY_EXPR, va_list_type_node, dst, src);
      TREE_SIDE_EFFECTS (t) = 1;
      expand_expr (t, const0_rtx, VOIDmode, EXPAND_NORMAL);
    }
  else
    {
      rtx dstb, srcb, size;

      /* Evaluate to pointers.  */
      dstb = expand_expr (dst, NULL_RTX, Pmode, EXPAND_NORMAL);
      srcb = expand_expr (src, NULL_RTX, Pmode, EXPAND_NORMAL);
      size = expand_expr (TYPE_SIZE_UNIT (va_list_type_node), NULL_RTX,
			  VOIDmode, EXPAND_NORMAL);

      dstb = convert_memory_address (Pmode, dstb);
      srcb = convert_memory_address (Pmode, srcb);

      /* "Dereference" to BLKmode memories.  */
      dstb = gen_rtx_MEM (BLKmode, dstb);
      set_mem_alias_set (dstb, get_alias_set (TREE_TYPE (TREE_TYPE (dst))));
      set_mem_align (dstb, TYPE_ALIGN (va_list_type_node));
      srcb = gen_rtx_MEM (BLKmode, srcb);
      set_mem_alias_set (srcb, get_alias_set (TREE_TYPE (TREE_TYPE (src))));
      set_mem_align (srcb, TYPE_ALIGN (va_list_type_node));

      /* Copy.  */
      emit_block_move (dstb, srcb, size, BLOCK_OP_NORMAL);
    }

  return const0_rtx;
}

/* Expand a call to one of the builtin functions __builtin_frame_address or
   __builtin_return_address.  */

static rtx
expand_builtin_frame_address (tree fndecl, tree arglist)
{
  /* The argument must be a nonnegative integer constant.
     It counts the number of frames to scan up the stack.
     The value is the return address saved in that frame.  */
  if (arglist == 0)
    /* Warning about missing arg was already issued.  */
    return const0_rtx;
  else if (! host_integerp (TREE_VALUE (arglist), 1))
    {
      if (DECL_FUNCTION_CODE (fndecl) == BUILT_IN_FRAME_ADDRESS)
	error ("invalid argument to %<__builtin_frame_address%>");
      else
	error ("invalid argument to %<__builtin_return_address%>");
      return const0_rtx;
    }
  else
    {
      rtx tem
	= expand_builtin_return_addr (DECL_FUNCTION_CODE (fndecl),
				      tree_low_cst (TREE_VALUE (arglist), 1));

      /* Some ports cannot access arbitrary stack frames.  */
      if (tem == NULL)
	{
	  if (DECL_FUNCTION_CODE (fndecl) == BUILT_IN_FRAME_ADDRESS)
	    warning (0, "unsupported argument to %<__builtin_frame_address%>");
	  else
	    warning (0, "unsupported argument to %<__builtin_return_address%>");
	  return const0_rtx;
	}

      /* For __builtin_frame_address, return what we've got.  */
      if (DECL_FUNCTION_CODE (fndecl) == BUILT_IN_FRAME_ADDRESS)
	return tem;

      if (!REG_P (tem)
	  && ! CONSTANT_P (tem))
	tem = copy_to_mode_reg (Pmode, tem);
      return tem;
    }
}

/* Expand a call to the alloca builtin, with arguments ARGLIST.  Return 0 if
   we failed and the caller should emit a normal call, otherwise try to get
   the result in TARGET, if convenient.  */

static rtx
expand_builtin_alloca (tree arglist, rtx target)
{
  rtx op0;
  rtx result;

  /* In -fmudflap-instrumented code, alloca() and __builtin_alloca()
     should always expand to function calls.  These can be intercepted
     in libmudflap.  */
  if (flag_mudflap)
    return 0;

  if (!validate_arglist (arglist, INTEGER_TYPE, VOID_TYPE))
    return 0;

  /* Compute the argument.  */
  op0 = expand_normal (TREE_VALUE (arglist));

  /* Allocate the desired space.  */
  result = allocate_dynamic_stack_space (op0, target, BITS_PER_UNIT);
  result = convert_memory_address (ptr_mode, result);

  return result;
}

/* Expand a call to a unary builtin.  The arguments are in ARGLIST.
   Return 0 if a normal call should be emitted rather than expanding the
   function in-line.  If convenient, the result should be placed in TARGET.
   SUBTARGET may be used as the target for computing one of EXP's operands.  */

static rtx
expand_builtin_unop (enum machine_mode target_mode, tree arglist, rtx target,
		     rtx subtarget, optab op_optab)
{
  rtx op0;
  if (!validate_arglist (arglist, INTEGER_TYPE, VOID_TYPE))
    return 0;

  /* Compute the argument.  */
  op0 = expand_expr (TREE_VALUE (arglist), subtarget, VOIDmode, 0);
  /* Compute op, into TARGET if possible.
     Set TARGET to wherever the result comes back.  */
  target = expand_unop (TYPE_MODE (TREE_TYPE (TREE_VALUE (arglist))),
			op_optab, op0, target, 1);
  gcc_assert (target);

  return convert_to_mode (target_mode, target, 0);
}

/* If the string passed to fputs is a constant and is one character
   long, we attempt to transform this call into __builtin_fputc().  */

static rtx
expand_builtin_fputs (tree arglist, rtx target, bool unlocked)
{
  /* Verify the arguments in the original call.  */
  if (validate_arglist (arglist, POINTER_TYPE, POINTER_TYPE, VOID_TYPE))
    {
      tree result = fold_builtin_fputs (arglist, (target == const0_rtx),
					unlocked, NULL_TREE);
      if (result)
	return expand_expr (result, target, VOIDmode, EXPAND_NORMAL);
    }
  return 0;
}

/* Expand a call to __builtin_expect.  We return our argument and emit a
   NOTE_INSN_EXPECTED_VALUE note.  This is the expansion of __builtin_expect in
   a non-jump context.  */

static rtx
expand_builtin_expect (tree arglist, rtx target)
{
  tree exp, c;
  rtx note, rtx_c;

  if (arglist == NULL_TREE
      || TREE_CHAIN (arglist) == NULL_TREE)
    return const0_rtx;
  exp = TREE_VALUE (arglist);
  c = TREE_VALUE (TREE_CHAIN (arglist));

  if (TREE_CODE (c) != INTEGER_CST)
    {
      error ("second argument to %<__builtin_expect%> must be a constant");
      c = integer_zero_node;
    }

  target = expand_expr (exp, target, VOIDmode, EXPAND_NORMAL);

  /* Don't bother with expected value notes for integral constants.  */
  if (flag_guess_branch_prob && GET_CODE (target) != CONST_INT)
    {
      /* We do need to force this into a register so that we can be
	 moderately sure to be able to correctly interpret the branch
	 condition later.  */
      target = force_reg (GET_MODE (target), target);

      rtx_c = expand_expr (c, NULL_RTX, GET_MODE (target), EXPAND_NORMAL);

      note = emit_note (NOTE_INSN_EXPECTED_VALUE);
      NOTE_EXPECTED_VALUE (note) = gen_rtx_EQ (VOIDmode, target, rtx_c);
    }

  return target;
}

/* Like expand_builtin_expect, except do this in a jump context.  This is
   called from do_jump if the conditional is a __builtin_expect.  Return either
   a list of insns to emit the jump or NULL if we cannot optimize
   __builtin_expect.  We need to optimize this at jump time so that machines
   like the PowerPC don't turn the test into a SCC operation, and then jump
   based on the test being 0/1.  */

rtx
expand_builtin_expect_jump (tree exp, rtx if_false_label, rtx if_true_label)
{
  tree arglist = TREE_OPERAND (exp, 1);
  tree arg0 = TREE_VALUE (arglist);
  tree arg1 = TREE_VALUE (TREE_CHAIN (arglist));
  rtx ret = NULL_RTX;

  /* Only handle __builtin_expect (test, 0) and
     __builtin_expect (test, 1).  */
  if (TREE_CODE (TREE_TYPE (arg1)) == INTEGER_TYPE
      && (integer_zerop (arg1) || integer_onep (arg1)))
    {
      rtx insn, drop_through_label, temp;

      /* Expand the jump insns.  */
      start_sequence ();
      do_jump (arg0, if_false_label, if_true_label);
      ret = get_insns ();

      drop_through_label = get_last_insn ();
      if (drop_through_label && NOTE_P (drop_through_label))
	drop_through_label = prev_nonnote_insn (drop_through_label);
      if (drop_through_label && !LABEL_P (drop_through_label))
	drop_through_label = NULL_RTX;
      end_sequence ();

      if (! if_true_label)
	if_true_label = drop_through_label;
      if (! if_false_label)
	if_false_label = drop_through_label;

      /* Go through and add the expect's to each of the conditional jumps.  */
      insn = ret;
      while (insn != NULL_RTX)
	{
	  rtx next = NEXT_INSN (insn);

	  if (JUMP_P (insn) && any_condjump_p (insn))
	    {
	      rtx ifelse = SET_SRC (pc_set (insn));
	      rtx then_dest = XEXP (ifelse, 1);
	      rtx else_dest = XEXP (ifelse, 2);
	      int taken = -1;

	      /* First check if we recognize any of the labels.  */
	      if (GET_CODE (then_dest) == LABEL_REF
		  && XEXP (then_dest, 0) == if_true_label)
		taken = 1;
	      else if (GET_CODE (then_dest) == LABEL_REF
		       && XEXP (then_dest, 0) == if_false_label)
		taken = 0;
	      else if (GET_CODE (else_dest) == LABEL_REF
		       && XEXP (else_dest, 0) == if_false_label)
		taken = 1;
	      else if (GET_CODE (else_dest) == LABEL_REF
		       && XEXP (else_dest, 0) == if_true_label)
		taken = 0;
	      /* Otherwise check where we drop through.  */
	      else if (else_dest == pc_rtx)
		{
		  if (next && NOTE_P (next))
		    next = next_nonnote_insn (next);

		  if (next && JUMP_P (next)
		      && any_uncondjump_p (next))
		    temp = XEXP (SET_SRC (pc_set (next)), 0);
		  else
		    temp = next;

		  /* TEMP is either a CODE_LABEL, NULL_RTX or something
		     else that can't possibly match either target label.  */
		  if (temp == if_false_label)
		    taken = 1;
		  else if (temp == if_true_label)
		    taken = 0;
		}
	      else if (then_dest == pc_rtx)
		{
		  if (next && NOTE_P (next))
		    next = next_nonnote_insn (next);

		  if (next && JUMP_P (next)
		      && any_uncondjump_p (next))
		    temp = XEXP (SET_SRC (pc_set (next)), 0);
		  else
		    temp = next;

		  if (temp == if_false_label)
		    taken = 0;
		  else if (temp == if_true_label)
		    taken = 1;
		}

	      if (taken != -1)
		{
		  /* If the test is expected to fail, reverse the
		     probabilities.  */
		  if (integer_zerop (arg1))
		    taken = 1 - taken;
		  predict_insn_def (insn, PRED_BUILTIN_EXPECT, taken);
		}
	    }

	  insn = next;
	}
    }

  return ret;
}

void
expand_builtin_trap (void)
{
#ifdef HAVE_trap
  if (HAVE_trap)
    emit_insn (gen_trap ());
  else
#endif
    emit_library_call (abort_libfunc, LCT_NORETURN, VOIDmode, 0);
  emit_barrier ();
}

/* Expand a call to fabs, fabsf or fabsl with arguments ARGLIST.
   Return 0 if a normal call should be emitted rather than expanding
   the function inline.  If convenient, the result should be placed
   in TARGET.  SUBTARGET may be used as the target for computing
   the operand.  */

static rtx
expand_builtin_fabs (tree arglist, rtx target, rtx subtarget)
{
  enum machine_mode mode;
  tree arg;
  rtx op0;

  if (!validate_arglist (arglist, REAL_TYPE, VOID_TYPE))
    return 0;

  arg = TREE_VALUE (arglist);
  mode = TYPE_MODE (TREE_TYPE (arg));
  op0 = expand_expr (arg, subtarget, VOIDmode, 0);
  return expand_abs (mode, op0, target, 0, safe_from_p (target, arg, 1));
}

/* Expand a call to copysign, copysignf, or copysignl with arguments ARGLIST.
   Return NULL is a normal call should be emitted rather than expanding the
   function inline.  If convenient, the result should be placed in TARGET.
   SUBTARGET may be used as the target for computing the operand.  */

static rtx
expand_builtin_copysign (tree arglist, rtx target, rtx subtarget)
{
  rtx op0, op1;
  tree arg;

  if (!validate_arglist (arglist, REAL_TYPE, REAL_TYPE, VOID_TYPE))
    return 0;

  arg = TREE_VALUE (arglist);
  op0 = expand_expr (arg, subtarget, VOIDmode, EXPAND_NORMAL);

  arg = TREE_VALUE (TREE_CHAIN (arglist));
  op1 = expand_normal (arg);

  return expand_copysign (op0, op1, target);
}

/* Create a new constant string literal and return a char* pointer to it.
   The STRING_CST value is the LEN characters at STR.  */
tree
build_string_literal (int len, const char *str)
{
  tree t, elem, index, type;

  t = build_string (len, str);
  elem = build_type_variant (char_type_node, 1, 0);
  index = build_index_type (build_int_cst (NULL_TREE, len - 1));
  type = build_array_type (elem, index);
  TREE_TYPE (t) = type;
  TREE_CONSTANT (t) = 1;
  TREE_INVARIANT (t) = 1;
  TREE_READONLY (t) = 1;
  TREE_STATIC (t) = 1;

  type = build_pointer_type (type);
  t = build1 (ADDR_EXPR, type, t);

  type = build_pointer_type (elem);
  t = build1 (NOP_EXPR, type, t);
  return t;
}

/* Expand EXP, a call to printf or printf_unlocked.
   Return 0 if a normal call should be emitted rather than transforming
   the function inline.  If convenient, the result should be placed in
   TARGET with mode MODE.  UNLOCKED indicates this is a printf_unlocked
   call.  */
static rtx
expand_builtin_printf (tree exp, rtx target, enum machine_mode mode,
		       bool unlocked)
{
  tree arglist = TREE_OPERAND (exp, 1);
  /* If we're using an unlocked function, assume the other unlocked
     functions exist explicitly.  */
  tree const fn_putchar = unlocked ? built_in_decls[BUILT_IN_PUTCHAR_UNLOCKED]
    : implicit_built_in_decls[BUILT_IN_PUTCHAR];
  tree const fn_puts = unlocked ? built_in_decls[BUILT_IN_PUTS_UNLOCKED]
    : implicit_built_in_decls[BUILT_IN_PUTS];
  const char *fmt_str;
  tree fn, fmt, arg;

  /* If the return value is used, don't do the transformation.  */
  if (target != const0_rtx)
    return 0;

  /* Verify the required arguments in the original call.  */
  if (! arglist)
    return 0;
  fmt = TREE_VALUE (arglist);
  if (! POINTER_TYPE_P (TREE_TYPE (fmt)))
    return 0;
  arglist = TREE_CHAIN (arglist);

  /* Check whether the format is a literal string constant.  */
  fmt_str = c_getstr (fmt);
  if (fmt_str == NULL)
    return 0;

  if (!init_target_chars())
    return 0;

  /* If the format specifier was "%s\n", call __builtin_puts(arg).  */
  if (strcmp (fmt_str, target_percent_s_newline) == 0)
    {
      if (! arglist
	  || ! POINTER_TYPE_P (TREE_TYPE (TREE_VALUE (arglist)))
	  || TREE_CHAIN (arglist))
	return 0;
      fn = fn_puts;
    }
  /* If the format specifier was "%c", call __builtin_putchar(arg).  */
  else if (strcmp (fmt_str, target_percent_c) == 0)
    {
      if (! arglist
	  || TREE_CODE (TREE_TYPE (TREE_VALUE (arglist))) != INTEGER_TYPE
	  || TREE_CHAIN (arglist))
	return 0;
      fn = fn_putchar;
    }
  else
    {
      /* We can't handle anything else with % args or %% ... yet.  */
      if (strchr (fmt_str, target_percent))
	return 0;

      if (arglist)
	return 0;

      /* If the format specifier was "", printf does nothing.  */
      if (fmt_str[0] == '\0')
	return const0_rtx;
      /* If the format specifier has length of 1, call putchar.  */
      if (fmt_str[1] == '\0')
	{
	  /* Given printf("c"), (where c is any one character,)
	     convert "c"[0] to an int and pass that to the replacement
	     function.  */
	  arg = build_int_cst (NULL_TREE, fmt_str[0]);
	  arglist = build_tree_list (NULL_TREE, arg);
	  fn = fn_putchar;
	}
      else
	{
	  /* If the format specifier was "string\n", call puts("string").  */
	  size_t len = strlen (fmt_str);
	  if ((unsigned char)fmt_str[len - 1] == target_newline)
	    {
	      /* Create a NUL-terminated string that's one char shorter
		 than the original, stripping off the trailing '\n'.  */
	      char *newstr = alloca (len);
	      memcpy (newstr, fmt_str, len - 1);
	      newstr[len - 1] = 0;

	      arg = build_string_literal (len, newstr);
	      arglist = build_tree_list (NULL_TREE, arg);
	      fn = fn_puts;
	    }
	  else
	    /* We'd like to arrange to call fputs(string,stdout) here,
	       but we need stdout and don't have a way to get it yet.  */
	    return 0;
	}
    }

  if (!fn)
    return 0;
  fn = build_function_call_expr (fn, arglist);
  if (TREE_CODE (fn) == CALL_EXPR)
    CALL_EXPR_TAILCALL (fn) = CALL_EXPR_TAILCALL (exp);
  return expand_expr (fn, target, mode, EXPAND_NORMAL);
}

/* Expand EXP, a call to fprintf or fprintf_unlocked.
   Return 0 if a normal call should be emitted rather than transforming
   the function inline.  If convenient, the result should be placed in
   TARGET with mode MODE.  UNLOCKED indicates this is a fprintf_unlocked
   call.  */
static rtx
expand_builtin_fprintf (tree exp, rtx target, enum machine_mode mode,
			bool unlocked)
{
  tree arglist = TREE_OPERAND (exp, 1);
  /* If we're using an unlocked function, assume the other unlocked
     functions exist explicitly.  */
  tree const fn_fputc = unlocked ? built_in_decls[BUILT_IN_FPUTC_UNLOCKED]
    : implicit_built_in_decls[BUILT_IN_FPUTC];
  tree const fn_fputs = unlocked ? built_in_decls[BUILT_IN_FPUTS_UNLOCKED]
    : implicit_built_in_decls[BUILT_IN_FPUTS];
  const char *fmt_str;
  tree fn, fmt, fp, arg;

  /* If the return value is used, don't do the transformation.  */
  if (target != const0_rtx)
    return 0;

  /* Verify the required arguments in the original call.  */
  if (! arglist)
    return 0;
  fp = TREE_VALUE (arglist);
  if (! POINTER_TYPE_P (TREE_TYPE (fp)))
    return 0;
  arglist = TREE_CHAIN (arglist);
  if (! arglist)
    return 0;
  fmt = TREE_VALUE (arglist);
  if (! POINTER_TYPE_P (TREE_TYPE (fmt)))
    return 0;
  arglist = TREE_CHAIN (arglist);

  /* Check whether the format is a literal string constant.  */
  fmt_str = c_getstr (fmt);
  if (fmt_str == NULL)
    return 0;

  if (!init_target_chars())
    return 0;

  /* If the format specifier was "%s", call __builtin_fputs(arg,fp).  */
  if (strcmp (fmt_str, target_percent_s) == 0)
    {
      if (! arglist
	  || ! POINTER_TYPE_P (TREE_TYPE (TREE_VALUE (arglist)))
	  || TREE_CHAIN (arglist))
	return 0;
      arg = TREE_VALUE (arglist);
      arglist = build_tree_list (NULL_TREE, fp);
      arglist = tree_cons (NULL_TREE, arg, arglist);
      fn = fn_fputs;
    }
  /* If the format specifier was "%c", call __builtin_fputc(arg,fp).  */
  else if (strcmp (fmt_str, target_percent_c) == 0)
    {
      if (! arglist
	  || TREE_CODE (TREE_TYPE (TREE_VALUE (arglist))) != INTEGER_TYPE
	  || TREE_CHAIN (arglist))
	return 0;
      arg = TREE_VALUE (arglist);
      arglist = build_tree_list (NULL_TREE, fp);
      arglist = tree_cons (NULL_TREE, arg, arglist);
      fn = fn_fputc;
    }
  else
    {
      /* We can't handle anything else with % args or %% ... yet.  */
      if (strchr (fmt_str, target_percent))
	return 0;

      if (arglist)
	return 0;

      /* If the format specifier was "", fprintf does nothing.  */
      if (fmt_str[0] == '\0')
	{
	  /* Evaluate and ignore FILE* argument for side-effects.  */
	  expand_expr (fp, const0_rtx, VOIDmode, EXPAND_NORMAL);
	  return const0_rtx;
	}

      /* When "string" doesn't contain %, replace all cases of
	 fprintf(stream,string) with fputs(string,stream).  The fputs
	 builtin will take care of special cases like length == 1.  */
      arglist = build_tree_list (NULL_TREE, fp);
      arglist = tree_cons (NULL_TREE, fmt, arglist);
      fn = fn_fputs;
    }

  if (!fn)
    return 0;
  fn = build_function_call_expr (fn, arglist);
  if (TREE_CODE (fn) == CALL_EXPR)
    CALL_EXPR_TAILCALL (fn) = CALL_EXPR_TAILCALL (exp);
  return expand_expr (fn, target, mode, EXPAND_NORMAL);
}

/* Expand a call to sprintf with argument list ARGLIST.  Return 0 if
   a normal call should be emitted rather than expanding the function
   inline.  If convenient, the result should be placed in TARGET with
   mode MODE.  */

static rtx
expand_builtin_sprintf (tree arglist, rtx target, enum machine_mode mode)
{
  tree orig_arglist, dest, fmt;
  const char *fmt_str;

  orig_arglist = arglist;

  /* Verify the required arguments in the original call.  */
  if (! arglist)
    return 0;
  dest = TREE_VALUE (arglist);
  if (! POINTER_TYPE_P (TREE_TYPE (dest)))
    return 0;
  arglist = TREE_CHAIN (arglist);
  if (! arglist)
    return 0;
  fmt = TREE_VALUE (arglist);
  if (! POINTER_TYPE_P (TREE_TYPE (fmt)))
    return 0;
  arglist = TREE_CHAIN (arglist);

  /* Check whether the format is a literal string constant.  */
  fmt_str = c_getstr (fmt);
  if (fmt_str == NULL)
    return 0;

  if (!init_target_chars())
    return 0;

  /* If the format doesn't contain % args or %%, use strcpy.  */
  if (strchr (fmt_str, target_percent) == 0)
    {
      tree fn = implicit_built_in_decls[BUILT_IN_STRCPY];
      tree exp;

#ifndef NO_UNSAFE_BUILTINS
      if (arglist || ! fn)
#endif
	return 0;
      expand_expr (build_function_call_expr (fn, orig_arglist),
		   const0_rtx, VOIDmode, EXPAND_NORMAL);
      if (target == const0_rtx)
	return const0_rtx;
      exp = build_int_cst (NULL_TREE, strlen (fmt_str));
      return expand_expr (exp, target, mode, EXPAND_NORMAL);
    }
  /* If the format is "%s", use strcpy if the result isn't used.  */
  else if (strcmp (fmt_str, target_percent_s) == 0)
    {
      tree fn, arg, len;
      fn = implicit_built_in_decls[BUILT_IN_STRCPY];

#ifndef NO_UNSAFE_BUILTINS
      if (! fn)
#endif
	return 0;

      if (! arglist || TREE_CHAIN (arglist))
	return 0;
      arg = TREE_VALUE (arglist);
      if (! POINTER_TYPE_P (TREE_TYPE (arg)))
	return 0;

      if (target != const0_rtx)
	{
	  len = c_strlen (arg, 1);
	  if (! len || TREE_CODE (len) != INTEGER_CST)
	    return 0;
	}
      else
	len = NULL_TREE;

      arglist = build_tree_list (NULL_TREE, arg);
      arglist = tree_cons (NULL_TREE, dest, arglist);
      expand_expr (build_function_call_expr (fn, arglist),
		   const0_rtx, VOIDmode, EXPAND_NORMAL);

      if (target == const0_rtx)
	return const0_rtx;
      return expand_expr (len, target, mode, EXPAND_NORMAL);
    }

  return 0;
}

/* Expand a call to either the entry or exit function profiler.  */

static rtx
expand_builtin_profile_func (bool exitp)
{
  rtx this, which;

  this = DECL_RTL (current_function_decl);
  gcc_assert (MEM_P (this));
  this = XEXP (this, 0);

  if (exitp)
    which = profile_function_exit_libfunc;
  else
    which = profile_function_entry_libfunc;

  emit_library_call (which, LCT_NORMAL, VOIDmode, 2, this, Pmode,
		     expand_builtin_return_addr (BUILT_IN_RETURN_ADDRESS,
						 0),
		     Pmode);

  return const0_rtx;
}

/* Given a trampoline address, make sure it satisfies TRAMPOLINE_ALIGNMENT.  */

static rtx
round_trampoline_addr (rtx tramp)
{
  rtx temp, addend, mask;

  /* If we don't need too much alignment, we'll have been guaranteed
     proper alignment by get_trampoline_type.  */
  if (TRAMPOLINE_ALIGNMENT <= STACK_BOUNDARY)
    return tramp;

  /* Round address up to desired boundary.  */
  temp = gen_reg_rtx (Pmode);
  addend = GEN_INT (TRAMPOLINE_ALIGNMENT / BITS_PER_UNIT - 1);
  mask = GEN_INT (-TRAMPOLINE_ALIGNMENT / BITS_PER_UNIT);

  temp  = expand_simple_binop (Pmode, PLUS, tramp, addend,
			       temp, 0, OPTAB_LIB_WIDEN);
  tramp = expand_simple_binop (Pmode, AND, temp, mask,
			       temp, 0, OPTAB_LIB_WIDEN);

  return tramp;
}

static rtx
expand_builtin_init_trampoline (tree arglist)
{
  tree t_tramp, t_func, t_chain;
  rtx r_tramp, r_func, r_chain;
#ifdef TRAMPOLINE_TEMPLATE
  rtx blktramp;
#endif

  if (!validate_arglist (arglist, POINTER_TYPE, POINTER_TYPE,
			 POINTER_TYPE, VOID_TYPE))
    return NULL_RTX;

  t_tramp = TREE_VALUE (arglist);
  arglist = TREE_CHAIN (arglist);
  t_func = TREE_VALUE (arglist);
  arglist = TREE_CHAIN (arglist);
  t_chain = TREE_VALUE (arglist);

  r_tramp = expand_normal (t_tramp);
  r_func = expand_normal (t_func);
  r_chain = expand_normal (t_chain);

  /* Generate insns to initialize the trampoline.  */
  r_tramp = round_trampoline_addr (r_tramp);
#ifdef TRAMPOLINE_TEMPLATE
  blktramp = gen_rtx_MEM (BLKmode, r_tramp);
  set_mem_align (blktramp, TRAMPOLINE_ALIGNMENT);
  emit_block_move (blktramp, assemble_trampoline_template (),
		   GEN_INT (TRAMPOLINE_SIZE), BLOCK_OP_NORMAL);
#endif
  trampolines_created = 1;
  INITIALIZE_TRAMPOLINE (r_tramp, r_func, r_chain);

  return const0_rtx;
}

static rtx
expand_builtin_adjust_trampoline (tree arglist)
{
  rtx tramp;

  if (!validate_arglist (arglist, POINTER_TYPE, VOID_TYPE))
    return NULL_RTX;

  tramp = expand_normal (TREE_VALUE (arglist));
  tramp = round_trampoline_addr (tramp);
#ifdef TRAMPOLINE_ADJUST_ADDRESS
  TRAMPOLINE_ADJUST_ADDRESS (tramp);
#endif

  return tramp;
}

/* Expand a call to the built-in signbit, signbitf or signbitl function.
   Return NULL_RTX if a normal call should be emitted rather than expanding
   the function in-line.  EXP is the expression that is a call to the builtin
   function; if convenient, the result should be placed in TARGET.  */

static rtx
expand_builtin_signbit (tree exp, rtx target)
{
  const struct real_format *fmt;
  enum machine_mode fmode, imode, rmode;
  HOST_WIDE_INT hi, lo;
  tree arg, arglist;
  int word, bitpos;
  rtx temp;

  arglist = TREE_OPERAND (exp, 1);
  if (!validate_arglist (arglist, REAL_TYPE, VOID_TYPE))
    return 0;

  arg = TREE_VALUE (arglist);
  fmode = TYPE_MODE (TREE_TYPE (arg));
  rmode = TYPE_MODE (TREE_TYPE (exp));
  fmt = REAL_MODE_FORMAT (fmode);

  /* For floating point formats without a sign bit, implement signbit
     as "ARG < 0.0".  */
  bitpos = fmt->signbit_ro;
  if (bitpos < 0)
  {
    /* But we can't do this if the format supports signed zero.  */
    if (fmt->has_signed_zero && HONOR_SIGNED_ZEROS (fmode))
      return 0;

    arg = fold_build2 (LT_EXPR, TREE_TYPE (exp), arg,
		       build_real (TREE_TYPE (arg), dconst0));
    return expand_expr (arg, target, VOIDmode, EXPAND_NORMAL);
  }

  temp = expand_normal (arg);
  if (GET_MODE_SIZE (fmode) <= UNITS_PER_WORD)
    {
      imode = int_mode_for_mode (fmode);
      if (imode == BLKmode)
	return 0;
      temp = gen_lowpart (imode, temp);
    }
  else
    {
      imode = word_mode;
      /* Handle targets with different FP word orders.  */
      if (FLOAT_WORDS_BIG_ENDIAN)
	word = (GET_MODE_BITSIZE (fmode) - bitpos) / BITS_PER_WORD;
      else
	word = bitpos / BITS_PER_WORD;
      temp = operand_subword_force (temp, word, fmode);
      bitpos = bitpos % BITS_PER_WORD;
    }

  /* Force the intermediate word_mode (or narrower) result into a
     register.  This avoids attempting to create paradoxical SUBREGs
     of floating point modes below.  */
  temp = force_reg (imode, temp);

  /* If the bitpos is within the "result mode" lowpart, the operation
     can be implement with a single bitwise AND.  Otherwise, we need
     a right shift and an AND.  */

  if (bitpos < GET_MODE_BITSIZE (rmode))
    {
      if (bitpos < HOST_BITS_PER_WIDE_INT)
	{
	  hi = 0;
	  lo = (HOST_WIDE_INT) 1 << bitpos;
	}
      else
	{
	  hi = (HOST_WIDE_INT) 1 << (bitpos - HOST_BITS_PER_WIDE_INT);
	  lo = 0;
	}

      if (imode != rmode)
	temp = gen_lowpart (rmode, temp);
      temp = expand_binop (rmode, and_optab, temp,
			   immed_double_const (lo, hi, rmode),
			   NULL_RTX, 1, OPTAB_LIB_WIDEN);
    }
  else
    {
      /* Perform a logical right shift to place the signbit in the least
	 significant bit, then truncate the result to the desired mode
	 and mask just this bit.  */
      temp = expand_shift (RSHIFT_EXPR, imode, temp,
			   build_int_cst (NULL_TREE, bitpos), NULL_RTX, 1);
      temp = gen_lowpart (rmode, temp);
      temp = expand_binop (rmode, and_optab, temp, const1_rtx,
			   NULL_RTX, 1, OPTAB_LIB_WIDEN);
    }

  return temp;
}

/* Expand fork or exec calls.  TARGET is the desired target of the
   call.  ARGLIST is the list of arguments of the call.  FN is the
   identificator of the actual function.  IGNORE is nonzero if the
   value is to be ignored.  */

static rtx
expand_builtin_fork_or_exec (tree fn, tree arglist, rtx target, int ignore)
{
  tree id, decl;
  tree call;

  /* If we are not profiling, just call the function.  */
  if (!profile_arc_flag)
    return NULL_RTX;

  /* Otherwise call the wrapper.  This should be equivalent for the rest of
     compiler, so the code does not diverge, and the wrapper may run the
     code necessary for keeping the profiling sane.  */

  switch (DECL_FUNCTION_CODE (fn))
    {
    case BUILT_IN_FORK:
      id = get_identifier ("__gcov_fork");
      break;

    case BUILT_IN_EXECL:
      id = get_identifier ("__gcov_execl");
      break;

    case BUILT_IN_EXECV:
      id = get_identifier ("__gcov_execv");
      break;

    case BUILT_IN_EXECLP:
      id = get_identifier ("__gcov_execlp");
      break;

    case BUILT_IN_EXECLE:
      id = get_identifier ("__gcov_execle");
      break;

    case BUILT_IN_EXECVP:
      id = get_identifier ("__gcov_execvp");
      break;

    case BUILT_IN_EXECVE:
      id = get_identifier ("__gcov_execve");
      break;

    default:
      gcc_unreachable ();
    }

  decl = build_decl (FUNCTION_DECL, id, TREE_TYPE (fn));
  DECL_EXTERNAL (decl) = 1;
  TREE_PUBLIC (decl) = 1;
  DECL_ARTIFICIAL (decl) = 1;
  TREE_NOTHROW (decl) = 1;
  DECL_VISIBILITY (decl) = VISIBILITY_DEFAULT;
  DECL_VISIBILITY_SPECIFIED (decl) = 1;
  call = build_function_call_expr (decl, arglist);

  return expand_call (call, target, ignore);
}


/* Reconstitute a mode for a __sync intrinsic operation.  Since the type of
   the pointer in these functions is void*, the tree optimizers may remove
   casts.  The mode computed in expand_builtin isn't reliable either, due
   to __sync_bool_compare_and_swap.

   FCODE_DIFF should be fcode - base, where base is the FOO_1 code for the
   group of builtins.  This gives us log2 of the mode size.  */

static inline enum machine_mode
get_builtin_sync_mode (int fcode_diff)
{
  /* The size is not negotiable, so ask not to get BLKmode in return
     if the target indicates that a smaller size would be better.  */
  return mode_for_size (BITS_PER_UNIT << fcode_diff, MODE_INT, 0);
}

/* Expand the memory expression LOC and return the appropriate memory operand
   for the builtin_sync operations.  */

static rtx
get_builtin_sync_mem (tree loc, enum machine_mode mode)
{
  rtx addr, mem;

  addr = expand_expr (loc, NULL, Pmode, EXPAND_SUM);

  /* Note that we explicitly do not want any alias information for this
     memory, so that we kill all other live memories.  Otherwise we don't
     satisfy the full barrier semantics of the intrinsic.  */
  mem = validize_mem (gen_rtx_MEM (mode, addr));

  set_mem_align (mem, get_pointer_alignment (loc, BIGGEST_ALIGNMENT));
  set_mem_alias_set (mem, ALIAS_SET_MEMORY_BARRIER);
  MEM_VOLATILE_P (mem) = 1;

  return mem;
}

/* Expand the __sync_xxx_and_fetch and __sync_fetch_and_xxx intrinsics.
   ARGLIST is the operands list to the function.  CODE is the rtx code
   that corresponds to the arithmetic or logical operation from the name;
   an exception here is that NOT actually means NAND.  TARGET is an optional
   place for us to store the results; AFTER is true if this is the
   fetch_and_xxx form.  IGNORE is true if we don't actually care about
   the result of the operation at all.  */

static rtx
expand_builtin_sync_operation (enum machine_mode mode, tree arglist,
			       enum rtx_code code, bool after,
			       rtx target, bool ignore)
{
  rtx val, mem;
  enum machine_mode old_mode;

  /* Expand the operands.  */
  mem = get_builtin_sync_mem (TREE_VALUE (arglist), mode);

  arglist = TREE_CHAIN (arglist);
  val = expand_expr (TREE_VALUE (arglist), NULL, mode, EXPAND_NORMAL);
  /* If VAL is promoted to a wider mode, convert it back to MODE.  Take care
     of CONST_INTs, where we know the old_mode only from the call argument.  */
  old_mode = GET_MODE (val);
  if (old_mode == VOIDmode)
    old_mode = TYPE_MODE (TREE_TYPE (TREE_VALUE (arglist)));
  val = convert_modes (mode, old_mode, val, 1);

  if (ignore)
    return expand_sync_operation (mem, val, code);
  else
    return expand_sync_fetch_operation (mem, val, code, after, target);
}

/* Expand the __sync_val_compare_and_swap and __sync_bool_compare_and_swap
   intrinsics.  ARGLIST is the operands list to the function.  IS_BOOL is
   true if this is the boolean form.  TARGET is a place for us to store the
   results; this is NOT optional if IS_BOOL is true.  */

static rtx
expand_builtin_compare_and_swap (enum machine_mode mode, tree arglist,
				 bool is_bool, rtx target)
{
  rtx old_val, new_val, mem;
  enum machine_mode old_mode;

  /* Expand the operands.  */
  mem = get_builtin_sync_mem (TREE_VALUE (arglist), mode);

  arglist = TREE_CHAIN (arglist);
  old_val = expand_expr (TREE_VALUE (arglist), NULL, mode, EXPAND_NORMAL);
  /* If VAL is promoted to a wider mode, convert it back to MODE.  Take care
     of CONST_INTs, where we know the old_mode only from the call argument.  */
  old_mode = GET_MODE (old_val);
  if (old_mode == VOIDmode)
    old_mode = TYPE_MODE (TREE_TYPE (TREE_VALUE (arglist)));
  old_val = convert_modes (mode, old_mode, old_val, 1);

  arglist = TREE_CHAIN (arglist);
  new_val = expand_expr (TREE_VALUE (arglist), NULL, mode, EXPAND_NORMAL);
  /* If VAL is promoted to a wider mode, convert it back to MODE.  Take care
     of CONST_INTs, where we know the old_mode only from the call argument.  */
  old_mode = GET_MODE (new_val);
  if (old_mode == VOIDmode)
    old_mode = TYPE_MODE (TREE_TYPE (TREE_VALUE (arglist)));
  new_val = convert_modes (mode, old_mode, new_val, 1);

  if (is_bool)
    return expand_bool_compare_and_swap (mem, old_val, new_val, target);
  else
    return expand_val_compare_and_swap (mem, old_val, new_val, target);
}

/* Expand the __sync_lock_test_and_set intrinsic.  Note that the most
   general form is actually an atomic exchange, and some targets only
   support a reduced form with the second argument being a constant 1.
   ARGLIST is the operands list to the function; TARGET is an optional
   place for us to store the results.  */

static rtx
expand_builtin_lock_test_and_set (enum machine_mode mode, tree arglist,
				  rtx target)
{
  rtx val, mem;
  enum machine_mode old_mode;

  /* Expand the operands.  */
  mem = get_builtin_sync_mem (TREE_VALUE (arglist), mode);

  arglist = TREE_CHAIN (arglist);
  val = expand_expr (TREE_VALUE (arglist), NULL, mode, EXPAND_NORMAL);
  /* If VAL is promoted to a wider mode, convert it back to MODE.  Take care
     of CONST_INTs, where we know the old_mode only from the call argument.  */
  old_mode = GET_MODE (val);
  if (old_mode == VOIDmode)
    old_mode = TYPE_MODE (TREE_TYPE (TREE_VALUE (arglist)));
  val = convert_modes (mode, old_mode, val, 1);

  return expand_sync_lock_test_and_set (mem, val, target);
}

/* Expand the __sync_synchronize intrinsic.  */

static void
expand_builtin_synchronize (void)
{
  tree x;

#ifdef HAVE_memory_barrier
  if (HAVE_memory_barrier)
    {
      emit_insn (gen_memory_barrier ());
      return;
    }
#endif

  /* If no explicit memory barrier instruction is available, create an
     empty asm stmt with a memory clobber.  */
  x = build4 (ASM_EXPR, void_type_node, build_string (0, ""), NULL, NULL,
	      tree_cons (NULL, build_string (6, "memory"), NULL));
  ASM_VOLATILE_P (x) = 1;
  expand_asm_expr (x);
}

/* Expand the __sync_lock_release intrinsic.  ARGLIST is the operands list
   to the function.  */

static void
expand_builtin_lock_release (enum machine_mode mode, tree arglist)
{
  enum insn_code icode;
  rtx mem, insn;
  rtx val = const0_rtx;

  /* Expand the operands.  */
  mem = get_builtin_sync_mem (TREE_VALUE (arglist), mode);

  /* If there is an explicit operation in the md file, use it.  */
  icode = sync_lock_release[mode];
  if (icode != CODE_FOR_nothing)
    {
      if (!insn_data[icode].operand[1].predicate (val, mode))
	val = force_reg (mode, val);

      insn = GEN_FCN (icode) (mem, val);
      if (insn)
	{
	  emit_insn (insn);
	  return;
	}
    }

  /* Otherwise we can implement this operation by emitting a barrier
     followed by a store of zero.  */
  expand_builtin_synchronize ();
  emit_move_insn (mem, val);
}

/* Expand an expression EXP that calls a built-in function,
   with result going to TARGET if that's convenient
   (and in mode MODE if that's convenient).
   SUBTARGET may be used as the target for computing one of EXP's operands.
   IGNORE is nonzero if the value is to be ignored.  */

rtx
expand_builtin (tree exp, rtx target, rtx subtarget, enum machine_mode mode,
		int ignore)
{
  tree fndecl = get_callee_fndecl (exp);
  tree arglist = TREE_OPERAND (exp, 1);
  enum built_in_function fcode = DECL_FUNCTION_CODE (fndecl);
  enum machine_mode target_mode = TYPE_MODE (TREE_TYPE (exp));

  if (DECL_BUILT_IN_CLASS (fndecl) == BUILT_IN_MD)
    return targetm.expand_builtin (exp, target, subtarget, mode, ignore);

  /* When not optimizing, generate calls to library functions for a certain
     set of builtins.  */
  if (!optimize
      && !called_as_built_in (fndecl)
      && DECL_ASSEMBLER_NAME_SET_P (fndecl)
      && fcode != BUILT_IN_ALLOCA)
    return expand_call (exp, target, ignore);

  /* The built-in function expanders test for target == const0_rtx
     to determine whether the function's result will be ignored.  */
  if (ignore)
    target = const0_rtx;

  /* If the result of a pure or const built-in function is ignored, and
     none of its arguments are volatile, we can avoid expanding the
     built-in call and just evaluate the arguments for side-effects.  */
  if (target == const0_rtx
      && (DECL_IS_PURE (fndecl) || TREE_READONLY (fndecl)))
    {
      bool volatilep = false;
      tree arg;

      for (arg = arglist; arg; arg = TREE_CHAIN (arg))
	if (TREE_THIS_VOLATILE (TREE_VALUE (arg)))
	  {
	    volatilep = true;
	    break;
	  }

      if (! volatilep)
	{
	  for (arg = arglist; arg; arg = TREE_CHAIN (arg))
	    expand_expr (TREE_VALUE (arg), const0_rtx,
			 VOIDmode, EXPAND_NORMAL);
	  return const0_rtx;
	}
    }

  switch (fcode)
    {
    CASE_FLT_FN (BUILT_IN_FABS):
      target = expand_builtin_fabs (arglist, target, subtarget);
      if (target)
	return target;
      break;

    CASE_FLT_FN (BUILT_IN_COPYSIGN):
      target = expand_builtin_copysign (arglist, target, subtarget);
      if (target)
	return target;
      break;

      /* Just do a normal library call if we were unable to fold
	 the values.  */
    CASE_FLT_FN (BUILT_IN_CABS):
      break;

    CASE_FLT_FN (BUILT_IN_EXP):
    CASE_FLT_FN (BUILT_IN_EXP10):
    CASE_FLT_FN (BUILT_IN_POW10):
    CASE_FLT_FN (BUILT_IN_EXP2):
    CASE_FLT_FN (BUILT_IN_EXPM1):
    CASE_FLT_FN (BUILT_IN_LOGB):
    CASE_FLT_FN (BUILT_IN_ILOGB):
    CASE_FLT_FN (BUILT_IN_LOG):
    CASE_FLT_FN (BUILT_IN_LOG10):
    CASE_FLT_FN (BUILT_IN_LOG2):
    CASE_FLT_FN (BUILT_IN_LOG1P):
    CASE_FLT_FN (BUILT_IN_TAN):
    CASE_FLT_FN (BUILT_IN_ASIN):
    CASE_FLT_FN (BUILT_IN_ACOS):
    CASE_FLT_FN (BUILT_IN_ATAN):
      /* Treat these like sqrt only if unsafe math optimizations are allowed,
	 because of possible accuracy problems.  */
      if (! flag_unsafe_math_optimizations)
	break;
    CASE_FLT_FN (BUILT_IN_SQRT):
    CASE_FLT_FN (BUILT_IN_FLOOR):
    CASE_FLT_FN (BUILT_IN_CEIL):
    CASE_FLT_FN (BUILT_IN_TRUNC):
    CASE_FLT_FN (BUILT_IN_ROUND):
    CASE_FLT_FN (BUILT_IN_NEARBYINT):
    CASE_FLT_FN (BUILT_IN_RINT):
    CASE_FLT_FN (BUILT_IN_LRINT):
    CASE_FLT_FN (BUILT_IN_LLRINT):
      target = expand_builtin_mathfn (exp, target, subtarget);
      if (target)
	return target;
      break;

    CASE_FLT_FN (BUILT_IN_LCEIL):
    CASE_FLT_FN (BUILT_IN_LLCEIL):
    CASE_FLT_FN (BUILT_IN_LFLOOR):
    CASE_FLT_FN (BUILT_IN_LLFLOOR):
      target = expand_builtin_int_roundingfn (exp, target, subtarget);
      if (target)
	return target;
      break;

    CASE_FLT_FN (BUILT_IN_POW):
      target = expand_builtin_pow (exp, target, subtarget);
      if (target)
	return target;
      break;

    CASE_FLT_FN (BUILT_IN_POWI):
      target = expand_builtin_powi (exp, target, subtarget);
      if (target)
	return target;
      break;

    CASE_FLT_FN (BUILT_IN_ATAN2):
    CASE_FLT_FN (BUILT_IN_LDEXP):
    CASE_FLT_FN (BUILT_IN_FMOD):
    CASE_FLT_FN (BUILT_IN_DREM):
      if (! flag_unsafe_math_optimizations)
	break;
      target = expand_builtin_mathfn_2 (exp, target, subtarget);
      if (target)
	return target;
      break;

    CASE_FLT_FN (BUILT_IN_SIN):
    CASE_FLT_FN (BUILT_IN_COS):
      if (! flag_unsafe_math_optimizations)
	break;
      target = expand_builtin_mathfn_3 (exp, target, subtarget);
      if (target)
	return target;
      break;

    CASE_FLT_FN (BUILT_IN_SINCOS):
      if (! flag_unsafe_math_optimizations)
	break;
      target = expand_builtin_sincos (exp);
      if (target)
	return target;
      break;

    case BUILT_IN_APPLY_ARGS:
      return expand_builtin_apply_args ();

      /* __builtin_apply (FUNCTION, ARGUMENTS, ARGSIZE) invokes
	 FUNCTION with a copy of the parameters described by
	 ARGUMENTS, and ARGSIZE.  It returns a block of memory
	 allocated on the stack into which is stored all the registers
	 that might possibly be used for returning the result of a
	 function.  ARGUMENTS is the value returned by
	 __builtin_apply_args.  ARGSIZE is the number of bytes of
	 arguments that must be copied.  ??? How should this value be
	 computed?  We'll also need a safe worst case value for varargs
	 functions.  */
    case BUILT_IN_APPLY:
      if (!validate_arglist (arglist, POINTER_TYPE,
			     POINTER_TYPE, INTEGER_TYPE, VOID_TYPE)
	  && !validate_arglist (arglist, REFERENCE_TYPE,
				POINTER_TYPE, INTEGER_TYPE, VOID_TYPE))
	return const0_rtx;
      else
	{
	  int i;
	  tree t;
	  rtx ops[3];

	  for (t = arglist, i = 0; t; t = TREE_CHAIN (t), i++)
	    ops[i] = expand_normal (TREE_VALUE (t));

	  return expand_builtin_apply (ops[0], ops[1], ops[2]);
	}

      /* __builtin_return (RESULT) causes the function to return the
	 value described by RESULT.  RESULT is address of the block of
	 memory returned by __builtin_apply.  */
    case BUILT_IN_RETURN:
      if (validate_arglist (arglist, POINTER_TYPE, VOID_TYPE))
	expand_builtin_return (expand_normal (TREE_VALUE (arglist)));
      return const0_rtx;

    case BUILT_IN_SAVEREGS:
      return expand_builtin_saveregs ();

    case BUILT_IN_ARGS_INFO:
      return expand_builtin_args_info (arglist);

      /* Return the address of the first anonymous stack arg.  */
    case BUILT_IN_NEXT_ARG:
      if (fold_builtin_next_arg (arglist))
	return const0_rtx;
      return expand_builtin_next_arg ();

    case BUILT_IN_CLASSIFY_TYPE:
      return expand_builtin_classify_type (arglist);

    case BUILT_IN_CONSTANT_P:
      return const0_rtx;

    case BUILT_IN_FRAME_ADDRESS:
    case BUILT_IN_RETURN_ADDRESS:
      return expand_builtin_frame_address (fndecl, arglist);

    /* Returns the address of the area where the structure is returned.
       0 otherwise.  */
    case BUILT_IN_AGGREGATE_INCOMING_ADDRESS:
      if (arglist != 0
	  || ! AGGREGATE_TYPE_P (TREE_TYPE (TREE_TYPE (current_function_decl)))
	  || !MEM_P (DECL_RTL (DECL_RESULT (current_function_decl))))
	return const0_rtx;
      else
	return XEXP (DECL_RTL (DECL_RESULT (current_function_decl)), 0);

    case BUILT_IN_ALLOCA:
      target = expand_builtin_alloca (arglist, target);
      if (target)
	return target;
      break;

    case BUILT_IN_STACK_SAVE:
      return expand_stack_save ();

    case BUILT_IN_STACK_RESTORE:
      expand_stack_restore (TREE_VALUE (arglist));
      return const0_rtx;

    CASE_INT_FN (BUILT_IN_FFS):
    case BUILT_IN_FFSIMAX:
      target = expand_builtin_unop (target_mode, arglist, target,
				    subtarget, ffs_optab);
      if (target)
	return target;
      break;

    CASE_INT_FN (BUILT_IN_CLZ):
    case BUILT_IN_CLZIMAX:
      target = expand_builtin_unop (target_mode, arglist, target,
				    subtarget, clz_optab);
      if (target)
	return target;
      break;

    CASE_INT_FN (BUILT_IN_CTZ):
    case BUILT_IN_CTZIMAX:
      target = expand_builtin_unop (target_mode, arglist, target,
				    subtarget, ctz_optab);
      if (target)
	return target;
      break;

    CASE_INT_FN (BUILT_IN_POPCOUNT):
    case BUILT_IN_POPCOUNTIMAX:
      target = expand_builtin_unop (target_mode, arglist, target,
				    subtarget, popcount_optab);
      if (target)
	return target;
      break;

    CASE_INT_FN (BUILT_IN_PARITY):
    case BUILT_IN_PARITYIMAX:
      target = expand_builtin_unop (target_mode, arglist, target,
				    subtarget, parity_optab);
      if (target)
	return target;
      break;

    case BUILT_IN_STRLEN:
      target = expand_builtin_strlen (arglist, target, target_mode);
      if (target)
	return target;
      break;

#ifndef NO_UNSAFE_BUILTINS
    case BUILT_IN_STRCPY:
      target = expand_builtin_strcpy (fndecl, arglist, target, mode);
      if (target)
	return target;
      break;
#endif

    case BUILT_IN_STRNCPY:
      target = expand_builtin_strncpy (exp, target, mode);
      if (target)
	return target;
      break;

#ifndef NO_UNSAFE_BUILTINS
    case BUILT_IN_STPCPY:
      target = expand_builtin_stpcpy (exp, target, mode);
      if (target)
	return target;
      break;
#endif

#ifndef NO_UNSAFE_BUILTINS
    case BUILT_IN_STRCAT:
      target = expand_builtin_strcat (fndecl, arglist, target, mode);
      if (target)
	return target;
      break;
#endif

    case BUILT_IN_STRNCAT:
      target = expand_builtin_strncat (arglist, target, mode);
      if (target)
	return target;
      break;

    case BUILT_IN_STRSPN:
      target = expand_builtin_strspn (arglist, target, mode);
      if (target)
	return target;
      break;

    case BUILT_IN_STRCSPN:
      target = expand_builtin_strcspn (arglist, target, mode);
      if (target)
	return target;
      break;

    case BUILT_IN_STRSTR:
      target = expand_builtin_strstr (arglist, TREE_TYPE (exp), target, mode);
      if (target)
	return target;
      break;

    case BUILT_IN_STRPBRK:
      target = expand_builtin_strpbrk (arglist, TREE_TYPE (exp), target, mode);
      if (target)
	return target;
      break;

    case BUILT_IN_INDEX:
    case BUILT_IN_STRCHR:
      target = expand_builtin_strchr (arglist, TREE_TYPE (exp), target, mode);
      if (target)
	return target;
      break;

    case BUILT_IN_RINDEX:
    case BUILT_IN_STRRCHR:
      target = expand_builtin_strrchr (arglist, TREE_TYPE (exp), target, mode);
      if (target)
	return target;
      break;

    case BUILT_IN_MEMCPY:
      target = expand_builtin_memcpy (exp, target, mode);
      if (target)
	return target;
      break;

    case BUILT_IN_MEMPCPY:
      target = expand_builtin_mempcpy (arglist, TREE_TYPE (exp), target, mode, /*endp=*/ 1);
      if (target)
	return target;
      break;

    case BUILT_IN_MEMMOVE:
      target = expand_builtin_memmove (arglist, TREE_TYPE (exp), target,
				       mode, exp);
      if (target)
	return target;
      break;

    case BUILT_IN_BCOPY:
      target = expand_builtin_bcopy (exp);
      if (target)
	return target;
      break;

    case BUILT_IN_MEMSET:
      target = expand_builtin_memset (arglist, target, mode, exp);
      if (target)
	return target;
      break;

    case BUILT_IN_BZERO:
      target = expand_builtin_bzero (exp);
      if (target)
	return target;
      break;

    case BUILT_IN_STRCMP:
      target = expand_builtin_strcmp (exp, target, mode);
      if (target)
	return target;
      break;

    case BUILT_IN_STRNCMP:
      target = expand_builtin_strncmp (exp, target, mode);
      if (target)
	return target;
      break;

    case BUILT_IN_BCMP:
    case BUILT_IN_MEMCMP:
      target = expand_builtin_memcmp (exp, arglist, target, mode);
      if (target)
	return target;
      break;

    case BUILT_IN_SETJMP:
      /* This should have been lowered to the builtins below.  */
      gcc_unreachable ();

    case BUILT_IN_SETJMP_SETUP:
      /* __builtin_setjmp_setup is passed a pointer to an array of five words
          and the receiver label.  */
      if (validate_arglist (arglist, POINTER_TYPE, POINTER_TYPE, VOID_TYPE))
	{
	  rtx buf_addr = expand_expr (TREE_VALUE (arglist), subtarget,
				      VOIDmode, EXPAND_NORMAL);
	  tree label = TREE_OPERAND (TREE_VALUE (TREE_CHAIN (arglist)), 0);
	  rtx label_r = label_rtx (label);

	  /* This is copied from the handling of non-local gotos.  */
	  expand_builtin_setjmp_setup (buf_addr, label_r);
	  nonlocal_goto_handler_labels
	    = gen_rtx_EXPR_LIST (VOIDmode, label_r,
				 nonlocal_goto_handler_labels);
	  /* ??? Do not let expand_label treat us as such since we would
	     not want to be both on the list of non-local labels and on
	     the list of forced labels.  */
	  FORCED_LABEL (label) = 0;
	  return const0_rtx;
	}
      break;

    case BUILT_IN_SETJMP_DISPATCHER:
       /* __builtin_setjmp_dispatcher is passed the dispatcher label.  */
      if (validate_arglist (arglist, POINTER_TYPE, VOID_TYPE))
	{
	  tree label = TREE_OPERAND (TREE_VALUE (arglist), 0);
	  rtx label_r = label_rtx (label);

	  /* Remove the dispatcher label from the list of non-local labels
	     since the receiver labels have been added to it above.  */
	  remove_node_from_expr_list (label_r, &nonlocal_goto_handler_labels);
	  return const0_rtx;
	}
      break;

    case BUILT_IN_SETJMP_RECEIVER:
       /* __builtin_setjmp_receiver is passed the receiver label.  */
      if (validate_arglist (arglist, POINTER_TYPE, VOID_TYPE))
	{
	  tree label = TREE_OPERAND (TREE_VALUE (arglist), 0);
	  rtx label_r = label_rtx (label);

	  expand_builtin_setjmp_receiver (label_r);
	  return const0_rtx;
	}
      break;

      /* __builtin_longjmp is passed a pointer to an array of five words.
	 It's similar to the C library longjmp function but works with
	 __builtin_setjmp above.  */
    case BUILT_IN_LONGJMP:
      if (validate_arglist (arglist, POINTER_TYPE, INTEGER_TYPE, VOID_TYPE))
	{
	  rtx buf_addr = expand_expr (TREE_VALUE (arglist), subtarget,
				      VOIDmode, EXPAND_NORMAL);
	  rtx value = expand_normal (TREE_VALUE (TREE_CHAIN (arglist)));

	  if (value != const1_rtx)
	    {
	      error ("%<__builtin_longjmp%> second argument must be 1");
	      return const0_rtx;
	    }

	  expand_builtin_longjmp (buf_addr, value);
	  return const0_rtx;
	}
      break;

    case BUILT_IN_NONLOCAL_GOTO:
      target = expand_builtin_nonlocal_goto (arglist);
      if (target)
	return target;
      break;

      /* This updates the setjmp buffer that is its argument with the value
	 of the current stack pointer.  */
    case BUILT_IN_UPDATE_SETJMP_BUF:
      if (validate_arglist (arglist, POINTER_TYPE, VOID_TYPE))
	{
	  rtx buf_addr
	    = expand_normal (TREE_VALUE (arglist));

	  expand_builtin_update_setjmp_buf (buf_addr);
	  return const0_rtx;
	}
      break;

    case BUILT_IN_TRAP:
      expand_builtin_trap ();
      return const0_rtx;

    case BUILT_IN_PRINTF:
      target = expand_builtin_printf (exp, target, mode, false);
      if (target)
	return target;
      break;

    case BUILT_IN_PRINTF_UNLOCKED:
      target = expand_builtin_printf (exp, target, mode, true);
      if (target)
	return target;
      break;

    case BUILT_IN_FPUTS:
      target = expand_builtin_fputs (arglist, target, false);
      if (target)
	return target;
      break;
    case BUILT_IN_FPUTS_UNLOCKED:
      target = expand_builtin_fputs (arglist, target, true);
      if (target)
	return target;
      break;

    case BUILT_IN_FPRINTF:
      target = expand_builtin_fprintf (exp, target, mode, false);
      if (target)
	return target;
      break;

    case BUILT_IN_FPRINTF_UNLOCKED:
      target = expand_builtin_fprintf (exp, target, mode, true);
      if (target)
	return target;
      break;

#ifndef NO_UNSAFE_BUILTINS
    case BUILT_IN_SPRINTF:
      target = expand_builtin_sprintf (arglist, target, mode);
      if (target)
	return target;
      break;
#endif

    CASE_FLT_FN (BUILT_IN_SIGNBIT):
      target = expand_builtin_signbit (exp, target);
      if (target)
	return target;
      break;

      /* Various hooks for the DWARF 2 __throw routine.  */
    case BUILT_IN_UNWIND_INIT:
      expand_builtin_unwind_init ();
      return const0_rtx;
    case BUILT_IN_DWARF_CFA:
      return virtual_cfa_rtx;
#ifdef DWARF2_UNWIND_INFO
    case BUILT_IN_DWARF_SP_COLUMN:
      return expand_builtin_dwarf_sp_column ();
    case BUILT_IN_INIT_DWARF_REG_SIZES:
      expand_builtin_init_dwarf_reg_sizes (TREE_VALUE (arglist));
      return const0_rtx;
#endif
    case BUILT_IN_FROB_RETURN_ADDR:
      return expand_builtin_frob_return_addr (TREE_VALUE (arglist));
    case BUILT_IN_EXTRACT_RETURN_ADDR:
      return expand_builtin_extract_return_addr (TREE_VALUE (arglist));
    case BUILT_IN_EH_RETURN:
      expand_builtin_eh_return (TREE_VALUE (arglist),
				TREE_VALUE (TREE_CHAIN (arglist)));
      return const0_rtx;
#ifdef EH_RETURN_DATA_REGNO
    case BUILT_IN_EH_RETURN_DATA_REGNO:
      return expand_builtin_eh_return_data_regno (arglist);
#endif
    case BUILT_IN_EXTEND_POINTER:
      return expand_builtin_extend_pointer (TREE_VALUE (arglist));

    case BUILT_IN_VA_START:
    case BUILT_IN_STDARG_START:
      return expand_builtin_va_start (arglist);
    case BUILT_IN_VA_END:
      return expand_builtin_va_end (arglist);
    case BUILT_IN_VA_COPY:
      return expand_builtin_va_copy (arglist);
    case BUILT_IN_EXPECT:
      return expand_builtin_expect (arglist, target);
    case BUILT_IN_PREFETCH:
      expand_builtin_prefetch (arglist);
      return const0_rtx;

    case BUILT_IN_PROFILE_FUNC_ENTER:
      return expand_builtin_profile_func (false);
    case BUILT_IN_PROFILE_FUNC_EXIT:
      return expand_builtin_profile_func (true);

    case BUILT_IN_INIT_TRAMPOLINE:
      return expand_builtin_init_trampoline (arglist);
    case BUILT_IN_ADJUST_TRAMPOLINE:
      return expand_builtin_adjust_trampoline (arglist);

    case BUILT_IN_FORK:
    case BUILT_IN_EXECL:
    case BUILT_IN_EXECV:
    case BUILT_IN_EXECLP:
    case BUILT_IN_EXECLE:
    case BUILT_IN_EXECVP:
    case BUILT_IN_EXECVE:
      target = expand_builtin_fork_or_exec (fndecl, arglist, target, ignore);
      if (target)
	return target;
      break;

    case BUILT_IN_FETCH_AND_ADD_1:
    case BUILT_IN_FETCH_AND_ADD_2:
    case BUILT_IN_FETCH_AND_ADD_4:
    case BUILT_IN_FETCH_AND_ADD_8:
    case BUILT_IN_FETCH_AND_ADD_16:
      mode = get_builtin_sync_mode (fcode - BUILT_IN_FETCH_AND_ADD_1);
      target = expand_builtin_sync_operation (mode, arglist, PLUS,
					      false, target, ignore);
      if (target)
	return target;
      break;

    case BUILT_IN_FETCH_AND_SUB_1:
    case BUILT_IN_FETCH_AND_SUB_2:
    case BUILT_IN_FETCH_AND_SUB_4:
    case BUILT_IN_FETCH_AND_SUB_8:
    case BUILT_IN_FETCH_AND_SUB_16:
      mode = get_builtin_sync_mode (fcode - BUILT_IN_FETCH_AND_SUB_1);
      target = expand_builtin_sync_operation (mode, arglist, MINUS,
					      false, target, ignore);
      if (target)
	return target;
      break;

    case BUILT_IN_FETCH_AND_OR_1:
    case BUILT_IN_FETCH_AND_OR_2:
    case BUILT_IN_FETCH_AND_OR_4:
    case BUILT_IN_FETCH_AND_OR_8:
    case BUILT_IN_FETCH_AND_OR_16:
      mode = get_builtin_sync_mode (fcode - BUILT_IN_FETCH_AND_OR_1);
      target = expand_builtin_sync_operation (mode, arglist, IOR,
					      false, target, ignore);
      if (target)
	return target;
      break;

    case BUILT_IN_FETCH_AND_AND_1:
    case BUILT_IN_FETCH_AND_AND_2:
    case BUILT_IN_FETCH_AND_AND_4:
    case BUILT_IN_FETCH_AND_AND_8:
    case BUILT_IN_FETCH_AND_AND_16:
      mode = get_builtin_sync_mode (fcode - BUILT_IN_FETCH_AND_AND_1);
      target = expand_builtin_sync_operation (mode, arglist, AND,
					      false, target, ignore);
      if (target)
	return target;
      break;

    case BUILT_IN_FETCH_AND_XOR_1:
    case BUILT_IN_FETCH_AND_XOR_2:
    case BUILT_IN_FETCH_AND_XOR_4:
    case BUILT_IN_FETCH_AND_XOR_8:
    case BUILT_IN_FETCH_AND_XOR_16:
      mode = get_builtin_sync_mode (fcode - BUILT_IN_FETCH_AND_XOR_1);
      target = expand_builtin_sync_operation (mode, arglist, XOR,
					      false, target, ignore);
      if (target)
	return target;
      break;

    case BUILT_IN_FETCH_AND_NAND_1:
    case BUILT_IN_FETCH_AND_NAND_2:
    case BUILT_IN_FETCH_AND_NAND_4:
    case BUILT_IN_FETCH_AND_NAND_8:
    case BUILT_IN_FETCH_AND_NAND_16:
      mode = get_builtin_sync_mode (fcode - BUILT_IN_FETCH_AND_NAND_1);
      target = expand_builtin_sync_operation (mode, arglist, NOT,
					      false, target, ignore);
      if (target)
	return target;
      break;

    case BUILT_IN_ADD_AND_FETCH_1:
    case BUILT_IN_ADD_AND_FETCH_2:
    case BUILT_IN_ADD_AND_FETCH_4:
    case BUILT_IN_ADD_AND_FETCH_8:
    case BUILT_IN_ADD_AND_FETCH_16:
      mode = get_builtin_sync_mode (fcode - BUILT_IN_ADD_AND_FETCH_1);
      target = expand_builtin_sync_operation (mode, arglist, PLUS,
					      true, target, ignore);
      if (target)
	return target;
      break;

    case BUILT_IN_SUB_AND_FETCH_1:
    case BUILT_IN_SUB_AND_FETCH_2:
    case BUILT_IN_SUB_AND_FETCH_4:
    case BUILT_IN_SUB_AND_FETCH_8:
    case BUILT_IN_SUB_AND_FETCH_16:
      mode = get_builtin_sync_mode (fcode - BUILT_IN_SUB_AND_FETCH_1);
      target = expand_builtin_sync_operation (mode, arglist, MINUS,
					      true, target, ignore);
      if (target)
	return target;
      break;

    case BUILT_IN_OR_AND_FETCH_1:
    case BUILT_IN_OR_AND_FETCH_2:
    case BUILT_IN_OR_AND_FETCH_4:
    case BUILT_IN_OR_AND_FETCH_8:
    case BUILT_IN_OR_AND_FETCH_16:
      mode = get_builtin_sync_mode (fcode - BUILT_IN_OR_AND_FETCH_1);
      target = expand_builtin_sync_operation (mode, arglist, IOR,
					      true, target, ignore);
      if (target)
	return target;
      break;

    case BUILT_IN_AND_AND_FETCH_1:
    case BUILT_IN_AND_AND_FETCH_2:
    case BUILT_IN_AND_AND_FETCH_4:
    case BUILT_IN_AND_AND_FETCH_8:
    case BUILT_IN_AND_AND_FETCH_16:
      mode = get_builtin_sync_mode (fcode - BUILT_IN_AND_AND_FETCH_1);
      target = expand_builtin_sync_operation (mode, arglist, AND,
					      true, target, ignore);
      if (target)
	return target;
      break;

    case BUILT_IN_XOR_AND_FETCH_1:
    case BUILT_IN_XOR_AND_FETCH_2:
    case BUILT_IN_XOR_AND_FETCH_4:
    case BUILT_IN_XOR_AND_FETCH_8:
    case BUILT_IN_XOR_AND_FETCH_16:
      mode = get_builtin_sync_mode (fcode - BUILT_IN_XOR_AND_FETCH_1);
      target = expand_builtin_sync_operation (mode, arglist, XOR,
					      true, target, ignore);
      if (target)
	return target;
      break;

    case BUILT_IN_NAND_AND_FETCH_1:
    case BUILT_IN_NAND_AND_FETCH_2:
    case BUILT_IN_NAND_AND_FETCH_4:
    case BUILT_IN_NAND_AND_FETCH_8:
    case BUILT_IN_NAND_AND_FETCH_16:
      mode = get_builtin_sync_mode (fcode - BUILT_IN_NAND_AND_FETCH_1);
      target = expand_builtin_sync_operation (mode, arglist, NOT,
					      true, target, ignore);
      if (target)
	return target;
      break;

    case BUILT_IN_BOOL_COMPARE_AND_SWAP_1:
    case BUILT_IN_BOOL_COMPARE_AND_SWAP_2:
    case BUILT_IN_BOOL_COMPARE_AND_SWAP_4:
    case BUILT_IN_BOOL_COMPARE_AND_SWAP_8:
    case BUILT_IN_BOOL_COMPARE_AND_SWAP_16:
      if (mode == VOIDmode)
	mode = TYPE_MODE (boolean_type_node);
      if (!target || !register_operand (target, mode))
	target = gen_reg_rtx (mode);

      mode = get_builtin_sync_mode (fcode - BUILT_IN_BOOL_COMPARE_AND_SWAP_1);
      target = expand_builtin_compare_and_swap (mode, arglist, true, target);
      if (target)
	return target;
      break;

    case BUILT_IN_VAL_COMPARE_AND_SWAP_1:
    case BUILT_IN_VAL_COMPARE_AND_SWAP_2:
    case BUILT_IN_VAL_COMPARE_AND_SWAP_4:
    case BUILT_IN_VAL_COMPARE_AND_SWAP_8:
    case BUILT_IN_VAL_COMPARE_AND_SWAP_16:
      mode = get_builtin_sync_mode (fcode - BUILT_IN_VAL_COMPARE_AND_SWAP_1);
      target = expand_builtin_compare_and_swap (mode, arglist, false, target);
      if (target)
	return target;
      break;

    case BUILT_IN_LOCK_TEST_AND_SET_1:
    case BUILT_IN_LOCK_TEST_AND_SET_2:
    case BUILT_IN_LOCK_TEST_AND_SET_4:
    case BUILT_IN_LOCK_TEST_AND_SET_8:
    case BUILT_IN_LOCK_TEST_AND_SET_16:
      mode = get_builtin_sync_mode (fcode - BUILT_IN_LOCK_TEST_AND_SET_1);
      target = expand_builtin_lock_test_and_set (mode, arglist, target);
      if (target)
	return target;
      break;

    case BUILT_IN_LOCK_RELEASE_1:
    case BUILT_IN_LOCK_RELEASE_2:
    case BUILT_IN_LOCK_RELEASE_4:
    case BUILT_IN_LOCK_RELEASE_8:
    case BUILT_IN_LOCK_RELEASE_16:
      mode = get_builtin_sync_mode (fcode - BUILT_IN_LOCK_RELEASE_1);
      expand_builtin_lock_release (mode, arglist);
      return const0_rtx;

    case BUILT_IN_SYNCHRONIZE:
      expand_builtin_synchronize ();
      return const0_rtx;

    case BUILT_IN_OBJECT_SIZE:
      return expand_builtin_object_size (exp);

    case BUILT_IN_MEMCPY_CHK:
    case BUILT_IN_MEMPCPY_CHK:
    case BUILT_IN_MEMMOVE_CHK:
    case BUILT_IN_MEMSET_CHK:
      target = expand_builtin_memory_chk (exp, target, mode, fcode);
      if (target)
	return target;
      break;

    case BUILT_IN_STRCPY_CHK:
    case BUILT_IN_STPCPY_CHK:
    case BUILT_IN_STRNCPY_CHK:
    case BUILT_IN_STRCAT_CHK:
    case BUILT_IN_SNPRINTF_CHK:
    case BUILT_IN_VSNPRINTF_CHK:
      maybe_emit_chk_warning (exp, fcode);
      break;

    case BUILT_IN_SPRINTF_CHK:
    case BUILT_IN_VSPRINTF_CHK:
      maybe_emit_sprintf_chk_warning (exp, fcode);
      break;

    default:	/* just do library call, if unknown builtin */
      break;
    }

  /* The switch statement above can drop through to cause the function
     to be called normally.  */
  return expand_call (exp, target, ignore);
}

/* Determine whether a tree node represents a call to a built-in
   function.  If the tree T is a call to a built-in function with
   the right number of arguments of the appropriate types, return
   the DECL_FUNCTION_CODE of the call, e.g. BUILT_IN_SQRT.
   Otherwise the return value is END_BUILTINS.  */

enum built_in_function
builtin_mathfn_code (tree t)
{
  tree fndecl, arglist, parmlist;
  tree argtype, parmtype;

  if (TREE_CODE (t) != CALL_EXPR
      || TREE_CODE (TREE_OPERAND (t, 0)) != ADDR_EXPR)
    return END_BUILTINS;

  fndecl = get_callee_fndecl (t);
  if (fndecl == NULL_TREE
      || TREE_CODE (fndecl) != FUNCTION_DECL
      || ! DECL_BUILT_IN (fndecl)
      || DECL_BUILT_IN_CLASS (fndecl) == BUILT_IN_MD)
    return END_BUILTINS;

  arglist = TREE_OPERAND (t, 1);
  parmlist = TYPE_ARG_TYPES (TREE_TYPE (fndecl));
  for (; parmlist; parmlist = TREE_CHAIN (parmlist))
    {
      /* If a function doesn't take a variable number of arguments,
	 the last element in the list will have type `void'.  */
      parmtype = TREE_VALUE (parmlist);
      if (VOID_TYPE_P (parmtype))
	{
	  if (arglist)
	    return END_BUILTINS;
	  return DECL_FUNCTION_CODE (fndecl);
	}

      if (! arglist)
	return END_BUILTINS;

      argtype = TREE_TYPE (TREE_VALUE (arglist));

      if (SCALAR_FLOAT_TYPE_P (parmtype))
	{
	  if (! SCALAR_FLOAT_TYPE_P (argtype))
	    return END_BUILTINS;
	}
      else if (COMPLEX_FLOAT_TYPE_P (parmtype))
	{
	  if (! COMPLEX_FLOAT_TYPE_P (argtype))
	    return END_BUILTINS;
	}
      else if (POINTER_TYPE_P (parmtype))
	{
	  if (! POINTER_TYPE_P (argtype))
	    return END_BUILTINS;
	}
      else if (INTEGRAL_TYPE_P (parmtype))
	{
	  if (! INTEGRAL_TYPE_P (argtype))
	    return END_BUILTINS;
	}
      else
	return END_BUILTINS;

      arglist = TREE_CHAIN (arglist);
    }

  /* Variable-length argument list.  */
  return DECL_FUNCTION_CODE (fndecl);
}

/* Fold a call to __builtin_constant_p, if we know it will evaluate to a
   constant.  ARGLIST is the argument list of the call.  */

static tree
fold_builtin_constant_p (tree arglist)
{
  if (arglist == 0)
    return 0;

  arglist = TREE_VALUE (arglist);

  /* We return 1 for a numeric type that's known to be a constant
     value at compile-time or for an aggregate type that's a
     literal constant.  */
  STRIP_NOPS (arglist);

  /* If we know this is a constant, emit the constant of one.  */
  if (CONSTANT_CLASS_P (arglist)
      || (TREE_CODE (arglist) == CONSTRUCTOR
	  && TREE_CONSTANT (arglist)))
    return integer_one_node;
  if (TREE_CODE (arglist) == ADDR_EXPR)
    {
       tree op = TREE_OPERAND (arglist, 0);
       if (TREE_CODE (op) == STRING_CST
	   || (TREE_CODE (op) == ARRAY_REF
	       && integer_zerop (TREE_OPERAND (op, 1))
	       && TREE_CODE (TREE_OPERAND (op, 0)) == STRING_CST))
	 return integer_one_node;
    }

  /* If this expression has side effects, show we don't know it to be a
     constant.  Likewise if it's a pointer or aggregate type since in
     those case we only want literals, since those are only optimized
     when generating RTL, not later.
     And finally, if we are compiling an initializer, not code, we
     need to return a definite result now; there's not going to be any
     more optimization done.  */
  if (TREE_SIDE_EFFECTS (arglist)
      || AGGREGATE_TYPE_P (TREE_TYPE (arglist))
      || POINTER_TYPE_P (TREE_TYPE (arglist))
      || cfun == 0
      || folding_initializer)
    return integer_zero_node;

  return 0;
}

/* Fold a call to __builtin_expect, if we expect that a comparison against
   the argument will fold to a constant.  In practice, this means a true
   constant or the address of a non-weak symbol.  ARGLIST is the argument
   list of the call.  */

static tree
fold_builtin_expect (tree arglist)
{
  tree arg, inner;

  if (arglist == 0)
    return 0;

  arg = TREE_VALUE (arglist);

  /* If the argument isn't invariant, then there's nothing we can do.  */
  if (!TREE_INVARIANT (arg))
    return 0;

  /* If we're looking at an address of a weak decl, then do not fold.  */
  inner = arg;
  STRIP_NOPS (inner);
  if (TREE_CODE (inner) == ADDR_EXPR)
    {
      do
	{
	  inner = TREE_OPERAND (inner, 0);
	}
      while (TREE_CODE (inner) == COMPONENT_REF
	     || TREE_CODE (inner) == ARRAY_REF);
      if (DECL_P (inner) && DECL_WEAK (inner))
	return 0;
    }

  /* Otherwise, ARG already has the proper type for the return value.  */
  return arg;
}

/* Fold a call to __builtin_classify_type.  */

static tree
fold_builtin_classify_type (tree arglist)
{
  if (arglist == 0)
    return build_int_cst (NULL_TREE, no_type_class);

  return build_int_cst (NULL_TREE,
			type_to_class (TREE_TYPE (TREE_VALUE (arglist))));
}

/* Fold a call to __builtin_strlen.  */

static tree
fold_builtin_strlen (tree arglist)
{
  if (!validate_arglist (arglist, POINTER_TYPE, VOID_TYPE))
    return NULL_TREE;
  else
    {
      tree len = c_strlen (TREE_VALUE (arglist), 0);

      if (len)
	{
	  /* Convert from the internal "sizetype" type to "size_t".  */
	  if (size_type_node)
	    len = fold_convert (size_type_node, len);
	  return len;
	}

      return NULL_TREE;
    }
}

/* Fold a call to __builtin_inf or __builtin_huge_val.  */

static tree
fold_builtin_inf (tree type, int warn)
{
  REAL_VALUE_TYPE real;

  /* __builtin_inff is intended to be usable to define INFINITY on all
     targets.  If an infinity is not available, INFINITY expands "to a
     positive constant of type float that overflows at translation
     time", footnote "In this case, using INFINITY will violate the
     constraint in 6.4.4 and thus require a diagnostic." (C99 7.12#4).
     Thus we pedwarn to ensure this constraint violation is
     diagnosed.  */
  if (!MODE_HAS_INFINITIES (TYPE_MODE (type)) && warn)
    pedwarn ("target format does not support infinity");

  real_inf (&real);
  return build_real (type, real);
}

/* Fold a call to __builtin_nan or __builtin_nans.  */

static tree
fold_builtin_nan (tree arglist, tree type, int quiet)
{
  REAL_VALUE_TYPE real;
  const char *str;

  if (!validate_arglist (arglist, POINTER_TYPE, VOID_TYPE))
    return 0;
  str = c_getstr (TREE_VALUE (arglist));
  if (!str)
    return 0;

  if (!real_nan (&real, str, quiet, TYPE_MODE (type)))
    return 0;

  return build_real (type, real);
}

/* Return true if the floating point expression T has an integer value.
   We also allow +Inf, -Inf and NaN to be considered integer values.  */

static bool
integer_valued_real_p (tree t)
{
  switch (TREE_CODE (t))
    {
    case FLOAT_EXPR:
      return true;

    case ABS_EXPR:
    case SAVE_EXPR:
    case NON_LVALUE_EXPR:
      return integer_valued_real_p (TREE_OPERAND (t, 0));

    case COMPOUND_EXPR:
    case MODIFY_EXPR:
    case BIND_EXPR:
      return integer_valued_real_p (TREE_OPERAND (t, 1));

    case PLUS_EXPR:
    case MINUS_EXPR:
    case MULT_EXPR:
    case MIN_EXPR:
    case MAX_EXPR:
      return integer_valued_real_p (TREE_OPERAND (t, 0))
	     && integer_valued_real_p (TREE_OPERAND (t, 1));

    case COND_EXPR:
      return integer_valued_real_p (TREE_OPERAND (t, 1))
	     && integer_valued_real_p (TREE_OPERAND (t, 2));

    case REAL_CST:
      if (! TREE_CONSTANT_OVERFLOW (t))
      {
	REAL_VALUE_TYPE c, cint;

	c = TREE_REAL_CST (t);
	real_trunc (&cint, TYPE_MODE (TREE_TYPE (t)), &c);
	return real_identical (&c, &cint);
      }
      break;

    case NOP_EXPR:
      {
	tree type = TREE_TYPE (TREE_OPERAND (t, 0));
	if (TREE_CODE (type) == INTEGER_TYPE)
	  return true;
	if (TREE_CODE (type) == REAL_TYPE)
	  return integer_valued_real_p (TREE_OPERAND (t, 0));
	break;
      }

    case CALL_EXPR:
      switch (builtin_mathfn_code (t))
	{
	CASE_FLT_FN (BUILT_IN_CEIL):
	CASE_FLT_FN (BUILT_IN_FLOOR):
	CASE_FLT_FN (BUILT_IN_NEARBYINT):
	CASE_FLT_FN (BUILT_IN_RINT):
	CASE_FLT_FN (BUILT_IN_ROUND):
	CASE_FLT_FN (BUILT_IN_TRUNC):
	  return true;

	default:
	  break;
	}
      break;

    default:
      break;
    }
  return false;
}

/* EXP is assumed to be builtin call where truncation can be propagated
   across (for instance floor((double)f) == (double)floorf (f).
   Do the transformation.  */

static tree
fold_trunc_transparent_mathfn (tree fndecl, tree arglist)
{
  enum built_in_function fcode = DECL_FUNCTION_CODE (fndecl);
  tree arg;

  if (! validate_arglist (arglist, REAL_TYPE, VOID_TYPE))
    return 0;

  arg = TREE_VALUE (arglist);
  /* Integer rounding functions are idempotent.  */
  if (fcode == builtin_mathfn_code (arg))
    return arg;

  /* If argument is already integer valued, and we don't need to worry
     about setting errno, there's no need to perform rounding.  */
  if (! flag_errno_math && integer_valued_real_p (arg))
    return arg;

  if (optimize)
    {
      tree arg0 = strip_float_extensions (arg);
      tree ftype = TREE_TYPE (TREE_TYPE (fndecl));
      tree newtype = TREE_TYPE (arg0);
      tree decl;

      if (TYPE_PRECISION (newtype) < TYPE_PRECISION (ftype)
	  && (decl = mathfn_built_in (newtype, fcode)))
	{
	  arglist =
	    build_tree_list (NULL_TREE, fold_convert (newtype, arg0));
	  return fold_convert (ftype,
			       build_function_call_expr (decl, arglist));
	}
    }
  return 0;
}

/* EXP is assumed to be builtin call which can narrow the FP type of
   the argument, for instance lround((double)f) -> lroundf (f).  */

static tree
fold_fixed_mathfn (tree fndecl, tree arglist)
{
  enum built_in_function fcode = DECL_FUNCTION_CODE (fndecl);
  tree arg;

  if (! validate_arglist (arglist, REAL_TYPE, VOID_TYPE))
    return 0;

  arg = TREE_VALUE (arglist);

  /* If argument is already integer valued, and we don't need to worry
     about setting errno, there's no need to perform rounding.  */
  if (! flag_errno_math && integer_valued_real_p (arg))
    return fold_build1 (FIX_TRUNC_EXPR, TREE_TYPE (TREE_TYPE (fndecl)), arg);

  if (optimize)
    {
      tree ftype = TREE_TYPE (arg);
      tree arg0 = strip_float_extensions (arg);
      tree newtype = TREE_TYPE (arg0);
      tree decl;

      if (TYPE_PRECISION (newtype) < TYPE_PRECISION (ftype)
	  && (decl = mathfn_built_in (newtype, fcode)))
	{
	  arglist =
	    build_tree_list (NULL_TREE, fold_convert (newtype, arg0));
	  return build_function_call_expr (decl, arglist);
	}
    }

  /* Canonicalize llround (x) to lround (x) on LP64 targets where
     sizeof (long long) == sizeof (long).  */
  if (TYPE_PRECISION (long_long_integer_type_node)
      == TYPE_PRECISION (long_integer_type_node))
    {
      tree newfn = NULL_TREE;
      switch (fcode)
	{
	CASE_FLT_FN (BUILT_IN_LLCEIL):
	  newfn = mathfn_built_in (TREE_TYPE (arg), BUILT_IN_LCEIL);
	  break;

	CASE_FLT_FN (BUILT_IN_LLFLOOR):
	  newfn = mathfn_built_in (TREE_TYPE (arg), BUILT_IN_LFLOOR);
	  break;

	CASE_FLT_FN (BUILT_IN_LLROUND):
	  newfn = mathfn_built_in (TREE_TYPE (arg), BUILT_IN_LROUND);
	  break;

	CASE_FLT_FN (BUILT_IN_LLRINT):
	  newfn = mathfn_built_in (TREE_TYPE (arg), BUILT_IN_LRINT);
	  break;

	default:
	  break;
	}

      if (newfn)
	{
	  tree newcall = build_function_call_expr (newfn, arglist);
	  return fold_convert (TREE_TYPE (TREE_TYPE (fndecl)), newcall);
	}
    }

  return 0;
}

/* Fold function call to builtin cabs, cabsf or cabsl.  ARGLIST
   is the argument list, TYPE is the return type and FNDECL is the
   original function DECL.  Return NULL_TREE if no if no simplification
   can be made.  */

static tree
fold_builtin_cabs (tree arglist, tree type, tree fndecl)
{
  tree arg;

  if (!arglist || TREE_CHAIN (arglist))
    return NULL_TREE;

  arg = TREE_VALUE (arglist);
  if (TREE_CODE (TREE_TYPE (arg)) != COMPLEX_TYPE
      || TREE_CODE (TREE_TYPE (TREE_TYPE (arg))) != REAL_TYPE)
    return NULL_TREE;

  /* Evaluate cabs of a constant at compile-time.  */
  if (flag_unsafe_math_optimizations
      && TREE_CODE (arg) == COMPLEX_CST
      && TREE_CODE (TREE_REALPART (arg)) == REAL_CST
      && TREE_CODE (TREE_IMAGPART (arg)) == REAL_CST
      && ! TREE_CONSTANT_OVERFLOW (TREE_REALPART (arg))
      && ! TREE_CONSTANT_OVERFLOW (TREE_IMAGPART (arg)))
    {
      REAL_VALUE_TYPE r, i;

      r = TREE_REAL_CST (TREE_REALPART (arg));
      i = TREE_REAL_CST (TREE_IMAGPART (arg));

      real_arithmetic (&r, MULT_EXPR, &r, &r);
      real_arithmetic (&i, MULT_EXPR, &i, &i);
      real_arithmetic (&r, PLUS_EXPR, &r, &i);
      if (real_sqrt (&r, TYPE_MODE (type), &r)
	  || ! flag_trapping_math)
	return build_real (type, r);
    }

  /* If either part is zero, cabs is fabs of the other.  */
  if (TREE_CODE (arg) == COMPLEX_EXPR
      && real_zerop (TREE_OPERAND (arg, 0)))
    return fold_build1 (ABS_EXPR, type, TREE_OPERAND (arg, 1));
  if (TREE_CODE (arg) == COMPLEX_EXPR
      && real_zerop (TREE_OPERAND (arg, 1)))
    return fold_build1 (ABS_EXPR, type, TREE_OPERAND (arg, 0));

  /* Optimize cabs(-z) and cabs(conj(z)) as cabs(z).  */
  if (TREE_CODE (arg) == NEGATE_EXPR
      || TREE_CODE (arg) == CONJ_EXPR)
    {
      tree arglist = build_tree_list (NULL_TREE, TREE_OPERAND (arg, 0));
      return build_function_call_expr (fndecl, arglist);
    }

  /* Don't do this when optimizing for size.  */
  if (flag_unsafe_math_optimizations
      && optimize && !optimize_size)
    {
      tree sqrtfn = mathfn_built_in (type, BUILT_IN_SQRT);

      if (sqrtfn != NULL_TREE)
	{
	  tree rpart, ipart, result, arglist;

	  arg = builtin_save_expr (arg);

	  rpart = fold_build1 (REALPART_EXPR, type, arg);
	  ipart = fold_build1 (IMAGPART_EXPR, type, arg);

	  rpart = builtin_save_expr (rpart);
	  ipart = builtin_save_expr (ipart);

	  result = fold_build2 (PLUS_EXPR, type,
				fold_build2 (MULT_EXPR, type,
					     rpart, rpart),
				fold_build2 (MULT_EXPR, type,
					     ipart, ipart));

	  arglist = build_tree_list (NULL_TREE, result);
	  return build_function_call_expr (sqrtfn, arglist);
	}
    }

  return NULL_TREE;
}

/* Fold a builtin function call to sqrt, sqrtf, or sqrtl.  Return
   NULL_TREE if no simplification can be made.  */

static tree
fold_builtin_sqrt (tree arglist, tree type)
{

  enum built_in_function fcode;
  tree arg = TREE_VALUE (arglist);

  if (!validate_arglist (arglist, REAL_TYPE, VOID_TYPE))
    return NULL_TREE;

  /* Optimize sqrt of constant value.  */
  if (TREE_CODE (arg) == REAL_CST
      && ! TREE_CONSTANT_OVERFLOW (arg))
    {
      REAL_VALUE_TYPE r, x;

      x = TREE_REAL_CST (arg);
      if (real_sqrt (&r, TYPE_MODE (type), &x)
	  || (!flag_trapping_math && !flag_errno_math))
	return build_real (type, r);
    }

  /* Optimize sqrt(expN(x)) = expN(x*0.5).  */
  fcode = builtin_mathfn_code (arg);
  if (flag_unsafe_math_optimizations && BUILTIN_EXPONENT_P (fcode))
    {
      tree expfn = TREE_OPERAND (TREE_OPERAND (arg, 0), 0);
      arg = fold_build2 (MULT_EXPR, type,
			 TREE_VALUE (TREE_OPERAND (arg, 1)),
			 build_real (type, dconsthalf));
      arglist = build_tree_list (NULL_TREE, arg);
      return build_function_call_expr (expfn, arglist);
    }

  /* Optimize sqrt(Nroot(x)) -> pow(x,1/(2*N)).  */
  if (flag_unsafe_math_optimizations && BUILTIN_ROOT_P (fcode))
    {
      tree powfn = mathfn_built_in (type, BUILT_IN_POW);

      if (powfn)
	{
	  tree arg0 = TREE_VALUE (TREE_OPERAND (arg, 1));
	  tree tree_root;
	  /* The inner root was either sqrt or cbrt.  */
	  REAL_VALUE_TYPE dconstroot =
	    BUILTIN_SQRT_P (fcode) ? dconsthalf : dconstthird;

	  /* Adjust for the outer root.  */
	  SET_REAL_EXP (&dconstroot, REAL_EXP (&dconstroot) - 1);
	  dconstroot = real_value_truncate (TYPE_MODE (type), dconstroot);
	  tree_root = build_real (type, dconstroot);
	  arglist = tree_cons (NULL_TREE, arg0,
			       build_tree_list (NULL_TREE, tree_root));
	  return build_function_call_expr (powfn, arglist);
	}
    }

  /* Optimize sqrt(pow(x,y)) = pow(|x|,y*0.5).  */
  if (flag_unsafe_math_optimizations
      && (fcode == BUILT_IN_POW
	  || fcode == BUILT_IN_POWF
	  || fcode == BUILT_IN_POWL))
    {
      tree powfn = TREE_OPERAND (TREE_OPERAND (arg, 0), 0);
      tree arg0 = TREE_VALUE (TREE_OPERAND (arg, 1));
      tree arg1 = TREE_VALUE (TREE_CHAIN (TREE_OPERAND (arg, 1)));
      tree narg1;
      if (!tree_expr_nonnegative_p (arg0))
	arg0 = build1 (ABS_EXPR, type, arg0);
      narg1 = fold_build2 (MULT_EXPR, type, arg1,
			   build_real (type, dconsthalf));
      arglist = tree_cons (NULL_TREE, arg0,
			   build_tree_list (NULL_TREE, narg1));
      return build_function_call_expr (powfn, arglist);
    }

  return NULL_TREE;
}

/* Fold a builtin function call to cbrt, cbrtf, or cbrtl.  Return
   NULL_TREE if no simplification can be made.  */
static tree
fold_builtin_cbrt (tree arglist, tree type)
{
  tree arg = TREE_VALUE (arglist);
  const enum built_in_function fcode = builtin_mathfn_code (arg);

  if (!validate_arglist (arglist, REAL_TYPE, VOID_TYPE))
    return NULL_TREE;

  /* Optimize cbrt of constant value.  */
  if (real_zerop (arg) || real_onep (arg) || real_minus_onep (arg))
    return arg;

  if (flag_unsafe_math_optimizations)
    {
      /* Optimize cbrt(expN(x)) -> expN(x/3).  */
      if (BUILTIN_EXPONENT_P (fcode))
	{
	  tree expfn = TREE_OPERAND (TREE_OPERAND (arg, 0), 0);
	  const REAL_VALUE_TYPE third_trunc =
	    real_value_truncate (TYPE_MODE (type), dconstthird);
	  arg = fold_build2 (MULT_EXPR, type,
			     TREE_VALUE (TREE_OPERAND (arg, 1)),
			     build_real (type, third_trunc));
	  arglist = build_tree_list (NULL_TREE, arg);
	  return build_function_call_expr (expfn, arglist);
	}

      /* Optimize cbrt(sqrt(x)) -> pow(x,1/6).  */
      if (BUILTIN_SQRT_P (fcode))
	{
	  tree powfn = mathfn_built_in (type, BUILT_IN_POW);

	  if (powfn)
	    {
	      tree arg0 = TREE_VALUE (TREE_OPERAND (arg, 1));
	      tree tree_root;
	      REAL_VALUE_TYPE dconstroot = dconstthird;

	      SET_REAL_EXP (&dconstroot, REAL_EXP (&dconstroot) - 1);
	      dconstroot = real_value_truncate (TYPE_MODE (type), dconstroot);
	      tree_root = build_real (type, dconstroot);
	      arglist = tree_cons (NULL_TREE, arg0,
				   build_tree_list (NULL_TREE, tree_root));
	      return build_function_call_expr (powfn, arglist);
	    }
	}

      /* Optimize cbrt(cbrt(x)) -> pow(x,1/9) iff x is nonnegative.  */
      if (BUILTIN_CBRT_P (fcode))
	{
	  tree arg0 = TREE_VALUE (TREE_OPERAND (arg, 1));
	  if (tree_expr_nonnegative_p (arg0))
	    {
	      tree powfn = mathfn_built_in (type, BUILT_IN_POW);

	      if (powfn)
		{
		  tree tree_root;
		  REAL_VALUE_TYPE dconstroot;

		  real_arithmetic (&dconstroot, MULT_EXPR, &dconstthird, &dconstthird);
		  dconstroot = real_value_truncate (TYPE_MODE (type), dconstroot);
		  tree_root = build_real (type, dconstroot);
		  arglist = tree_cons (NULL_TREE, arg0,
				       build_tree_list (NULL_TREE, tree_root));
		  return build_function_call_expr (powfn, arglist);
		}
	    }
	}

      /* Optimize cbrt(pow(x,y)) -> pow(x,y/3) iff x is nonnegative.  */
      if (fcode == BUILT_IN_POW || fcode == BUILT_IN_POWF
	  || fcode == BUILT_IN_POWL)
	{
	  tree arg00 = TREE_VALUE (TREE_OPERAND (arg, 1));
	  tree arg01 = TREE_VALUE (TREE_CHAIN (TREE_OPERAND (arg, 1)));
	  if (tree_expr_nonnegative_p (arg00))
	    {
	      tree powfn = TREE_OPERAND (TREE_OPERAND (arg, 0), 0);
	      const REAL_VALUE_TYPE dconstroot
		= real_value_truncate (TYPE_MODE (type), dconstthird);
	      tree narg01 = fold_build2 (MULT_EXPR, type, arg01,
					 build_real (type, dconstroot));
	      arglist = tree_cons (NULL_TREE, arg00,
				   build_tree_list (NULL_TREE, narg01));
	      return build_function_call_expr (powfn, arglist);
	    }
	}
    }
  return NULL_TREE;
}

/* Fold function call to builtin sin, sinf, or sinl.  Return
   NULL_TREE if no simplification can be made.  */
static tree
fold_builtin_sin (tree arglist)
{
  tree arg = TREE_VALUE (arglist);

  if (!validate_arglist (arglist, REAL_TYPE, VOID_TYPE))
    return NULL_TREE;

  /* Optimize sin (0.0) = 0.0.  */
  if (real_zerop (arg))
    return arg;

  return NULL_TREE;
}

/* Fold function call to builtin cos, cosf, or cosl.  Return
   NULL_TREE if no simplification can be made.  */
static tree
fold_builtin_cos (tree arglist, tree type, tree fndecl)
{
  tree arg = TREE_VALUE (arglist);

  if (!validate_arglist (arglist, REAL_TYPE, VOID_TYPE))
    return NULL_TREE;

  /* Optimize cos (0.0) = 1.0.  */
  if (real_zerop (arg))
    return build_real (type, dconst1);

  /* Optimize cos(-x) into cos (x).  */
  if (TREE_CODE (arg) == NEGATE_EXPR)
    {
      tree args = build_tree_list (NULL_TREE,
				   TREE_OPERAND (arg, 0));
      return build_function_call_expr (fndecl, args);
    }

  return NULL_TREE;
}

/* Fold function call to builtin tan, tanf, or tanl.  Return
   NULL_TREE if no simplification can be made.  */
static tree
fold_builtin_tan (tree arglist)
{
  enum built_in_function fcode;
  tree arg = TREE_VALUE (arglist);

  if (!validate_arglist (arglist, REAL_TYPE, VOID_TYPE))
    return NULL_TREE;

  /* Optimize tan(0.0) = 0.0.  */
  if (real_zerop (arg))
    return arg;

  /* Optimize tan(atan(x)) = x.  */
  fcode = builtin_mathfn_code (arg);
  if (flag_unsafe_math_optimizations
      && (fcode == BUILT_IN_ATAN
	  || fcode == BUILT_IN_ATANF
	  || fcode == BUILT_IN_ATANL))
    return TREE_VALUE (TREE_OPERAND (arg, 1));

  return NULL_TREE;
}

/* Fold function call to builtin atan, atanf, or atanl.  Return
   NULL_TREE if no simplification can be made.  */

static tree
fold_builtin_atan (tree arglist, tree type)
{

  tree arg = TREE_VALUE (arglist);

  if (!validate_arglist (arglist, REAL_TYPE, VOID_TYPE))
    return NULL_TREE;

  /* Optimize atan(0.0) = 0.0.  */
  if (real_zerop (arg))
    return arg;

  /* Optimize atan(1.0) = pi/4.  */
  if (real_onep (arg))
    {
      REAL_VALUE_TYPE cst;

      real_convert (&cst, TYPE_MODE (type), &dconstpi);
      SET_REAL_EXP (&cst, REAL_EXP (&cst) - 2);
      return build_real (type, cst);
    }

  return NULL_TREE;
}

/* Fold function call to builtin trunc, truncf or truncl.  Return
   NULL_TREE if no simplification can be made.  */

static tree
fold_builtin_trunc (tree fndecl, tree arglist)
{
  tree arg;

  if (! validate_arglist (arglist, REAL_TYPE, VOID_TYPE))
    return 0;

  /* Optimize trunc of constant value.  */
  arg = TREE_VALUE (arglist);
  if (TREE_CODE (arg) == REAL_CST && ! TREE_CONSTANT_OVERFLOW (arg))
    {
      REAL_VALUE_TYPE r, x;
      tree type = TREE_TYPE (TREE_TYPE (fndecl));

      x = TREE_REAL_CST (arg);
      real_trunc (&r, TYPE_MODE (type), &x);
      return build_real (type, r);
    }

  return fold_trunc_transparent_mathfn (fndecl, arglist);
}

/* Fold function call to builtin floor, floorf or floorl.  Return
   NULL_TREE if no simplification can be made.  */

static tree
fold_builtin_floor (tree fndecl, tree arglist)
{
  tree arg;

  if (! validate_arglist (arglist, REAL_TYPE, VOID_TYPE))
    return 0;

  /* Optimize floor of constant value.  */
  arg = TREE_VALUE (arglist);
  if (TREE_CODE (arg) == REAL_CST && ! TREE_CONSTANT_OVERFLOW (arg))
    {
      REAL_VALUE_TYPE x;

      x = TREE_REAL_CST (arg);
      if (! REAL_VALUE_ISNAN (x) || ! flag_errno_math)
	{
	  tree type = TREE_TYPE (TREE_TYPE (fndecl));
	  REAL_VALUE_TYPE r;

	  real_floor (&r, TYPE_MODE (type), &x);
	  return build_real (type, r);
	}
    }

  return fold_trunc_transparent_mathfn (fndecl, arglist);
}

/* Fold function call to builtin ceil, ceilf or ceill.  Return
   NULL_TREE if no simplification can be made.  */

static tree
fold_builtin_ceil (tree fndecl, tree arglist)
{
  tree arg;

  if (! validate_arglist (arglist, REAL_TYPE, VOID_TYPE))
    return 0;

  /* Optimize ceil of constant value.  */
  arg = TREE_VALUE (arglist);
  if (TREE_CODE (arg) == REAL_CST && ! TREE_CONSTANT_OVERFLOW (arg))
    {
      REAL_VALUE_TYPE x;

      x = TREE_REAL_CST (arg);
      if (! REAL_VALUE_ISNAN (x) || ! flag_errno_math)
	{
	  tree type = TREE_TYPE (TREE_TYPE (fndecl));
	  REAL_VALUE_TYPE r;

	  real_ceil (&r, TYPE_MODE (type), &x);
	  return build_real (type, r);
	}
    }

  return fold_trunc_transparent_mathfn (fndecl, arglist);
}

/* Fold function call to builtin round, roundf or roundl.  Return
   NULL_TREE if no simplification can be made.  */

static tree
fold_builtin_round (tree fndecl, tree arglist)
{
  tree arg;

  if (! validate_arglist (arglist, REAL_TYPE, VOID_TYPE))
    return 0;

  /* Optimize round of constant value.  */
  arg = TREE_VALUE (arglist);
  if (TREE_CODE (arg) == REAL_CST && ! TREE_CONSTANT_OVERFLOW (arg))
    {
      REAL_VALUE_TYPE x;

      x = TREE_REAL_CST (arg);
      if (! REAL_VALUE_ISNAN (x) || ! flag_errno_math)
	{
	  tree type = TREE_TYPE (TREE_TYPE (fndecl));
	  REAL_VALUE_TYPE r;

	  real_round (&r, TYPE_MODE (type), &x);
	  return build_real (type, r);
	}
    }

  return fold_trunc_transparent_mathfn (fndecl, arglist);
}

/* Fold function call to builtin lround, lroundf or lroundl (or the
   corresponding long long versions) and other rounding functions.
   Return NULL_TREE if no simplification can be made.  */

static tree
fold_builtin_int_roundingfn (tree fndecl, tree arglist)
{
  tree arg;

  if (! validate_arglist (arglist, REAL_TYPE, VOID_TYPE))
    return 0;

  /* Optimize lround of constant value.  */
  arg = TREE_VALUE (arglist);
  if (TREE_CODE (arg) == REAL_CST && ! TREE_CONSTANT_OVERFLOW (arg))
    {
      const REAL_VALUE_TYPE x = TREE_REAL_CST (arg);

      if (! REAL_VALUE_ISNAN (x) && ! REAL_VALUE_ISINF (x))
	{
	  tree itype = TREE_TYPE (TREE_TYPE (fndecl));
	  tree ftype = TREE_TYPE (arg), result;
	  HOST_WIDE_INT hi, lo;
	  REAL_VALUE_TYPE r;

	  switch (DECL_FUNCTION_CODE (fndecl))
	    {
	    CASE_FLT_FN (BUILT_IN_LFLOOR):
	    CASE_FLT_FN (BUILT_IN_LLFLOOR):
	      real_floor (&r, TYPE_MODE (ftype), &x);
	      break;

	    CASE_FLT_FN (BUILT_IN_LCEIL):
	    CASE_FLT_FN (BUILT_IN_LLCEIL):
	      real_ceil (&r, TYPE_MODE (ftype), &x);
	      break;

	    CASE_FLT_FN (BUILT_IN_LROUND):
	    CASE_FLT_FN (BUILT_IN_LLROUND):
	      real_round (&r, TYPE_MODE (ftype), &x);
	      break;

	    default:
	      gcc_unreachable ();
	    }

	  REAL_VALUE_TO_INT (&lo, &hi, r);
	  result = build_int_cst_wide (NULL_TREE, lo, hi);
	  if (int_fits_type_p (result, itype))
	    return fold_convert (itype, result);
	}
    }

  return fold_fixed_mathfn (fndecl, arglist);
}

/* Fold function call to builtin ffs, clz, ctz, popcount and parity
   and their long and long long variants (i.e. ffsl and ffsll).
   Return NULL_TREE if no simplification can be made.  */

static tree
fold_builtin_bitop (tree fndecl, tree arglist)
{
  tree arg;

  if (! validate_arglist (arglist, INTEGER_TYPE, VOID_TYPE))
    return NULL_TREE;

  /* Optimize for constant argument.  */
  arg = TREE_VALUE (arglist);
  if (TREE_CODE (arg) == INTEGER_CST && ! TREE_CONSTANT_OVERFLOW (arg))
    {
      HOST_WIDE_INT hi, width, result;
      unsigned HOST_WIDE_INT lo;
      tree type;

      type = TREE_TYPE (arg);
      width = TYPE_PRECISION (type);
      lo = TREE_INT_CST_LOW (arg);

      /* Clear all the bits that are beyond the type's precision.  */
      if (width > HOST_BITS_PER_WIDE_INT)
	{
	  hi = TREE_INT_CST_HIGH (arg);
	  if (width < 2 * HOST_BITS_PER_WIDE_INT)
	    hi &= ~((HOST_WIDE_INT) (-1) >> (width - HOST_BITS_PER_WIDE_INT));
	}
      else
	{
	  hi = 0;
	  if (width < HOST_BITS_PER_WIDE_INT)
	    lo &= ~((unsigned HOST_WIDE_INT) (-1) << width);
	}

      switch (DECL_FUNCTION_CODE (fndecl))
	{
	CASE_INT_FN (BUILT_IN_FFS):
	  if (lo != 0)
	    result = exact_log2 (lo & -lo) + 1;
	  else if (hi != 0)
	    result = HOST_BITS_PER_WIDE_INT + exact_log2 (hi & -hi) + 1;
	  else
	    result = 0;
	  break;

	CASE_INT_FN (BUILT_IN_CLZ):
	  if (hi != 0)
	    result = width - floor_log2 (hi) - 1 - HOST_BITS_PER_WIDE_INT;
	  else if (lo != 0)
	    result = width - floor_log2 (lo) - 1;
	  else if (! CLZ_DEFINED_VALUE_AT_ZERO (TYPE_MODE (type), result))
	    result = width;
	  break;

	CASE_INT_FN (BUILT_IN_CTZ):
	  if (lo != 0)
	    result = exact_log2 (lo & -lo);
	  else if (hi != 0)
	    result = HOST_BITS_PER_WIDE_INT + exact_log2 (hi & -hi);
	  else if (! CTZ_DEFINED_VALUE_AT_ZERO (TYPE_MODE (type), result))
	    result = width;
	  break;

	CASE_INT_FN (BUILT_IN_POPCOUNT):
	  result = 0;
	  while (lo)
	    result++, lo &= lo - 1;
	  while (hi)
	    result++, hi &= hi - 1;
	  break;

	CASE_INT_FN (BUILT_IN_PARITY):
	  result = 0;
	  while (lo)
	    result++, lo &= lo - 1;
	  while (hi)
	    result++, hi &= hi - 1;
	  result &= 1;
	  break;

	default:
	  gcc_unreachable ();
	}

      return build_int_cst (TREE_TYPE (TREE_TYPE (fndecl)), result);
    }

  return NULL_TREE;
}

/* Return true if EXPR is the real constant contained in VALUE.  */

static bool
real_dconstp (tree expr, const REAL_VALUE_TYPE *value)
{
  STRIP_NOPS (expr);

  return ((TREE_CODE (expr) == REAL_CST
	   && ! TREE_CONSTANT_OVERFLOW (expr)
	   && REAL_VALUES_EQUAL (TREE_REAL_CST (expr), *value))
	  || (TREE_CODE (expr) == COMPLEX_CST
	      && real_dconstp (TREE_REALPART (expr), value)
	      && real_zerop (TREE_IMAGPART (expr))));
}

/* A subroutine of fold_builtin to fold the various logarithmic
   functions.  EXP is the CALL_EXPR of a call to a builtin logN
   function.  VALUE is the base of the logN function.  */

static tree
fold_builtin_logarithm (tree fndecl, tree arglist,
			const REAL_VALUE_TYPE *value)
{
  if (validate_arglist (arglist, REAL_TYPE, VOID_TYPE))
    {
      tree type = TREE_TYPE (TREE_TYPE (fndecl));
      tree arg = TREE_VALUE (arglist);
      const enum built_in_function fcode = builtin_mathfn_code (arg);

      /* Optimize logN(1.0) = 0.0.  */
      if (real_onep (arg))
	return build_real (type, dconst0);

      /* Optimize logN(N) = 1.0.  If N can't be truncated to MODE
	 exactly, then only do this if flag_unsafe_math_optimizations.  */
      if (exact_real_truncate (TYPE_MODE (type), value)
	  || flag_unsafe_math_optimizations)
	{
	  const REAL_VALUE_TYPE value_truncate =
	    real_value_truncate (TYPE_MODE (type), *value);
	  if (real_dconstp (arg, &value_truncate))
	    return build_real (type, dconst1);
	}

      /* Special case, optimize logN(expN(x)) = x.  */
      if (flag_unsafe_math_optimizations
	  && ((value == &dconste
	       && (fcode == BUILT_IN_EXP
		   || fcode == BUILT_IN_EXPF
		   || fcode == BUILT_IN_EXPL))
	      || (value == &dconst2
		  && (fcode == BUILT_IN_EXP2
		      || fcode == BUILT_IN_EXP2F
		      || fcode == BUILT_IN_EXP2L))
	      || (value == &dconst10 && (BUILTIN_EXP10_P (fcode)))))
	return fold_convert (type, TREE_VALUE (TREE_OPERAND (arg, 1)));

      /* Optimize logN(func()) for various exponential functions.  We
	 want to determine the value "x" and the power "exponent" in
	 order to transform logN(x**exponent) into exponent*logN(x).  */
      if (flag_unsafe_math_optimizations)
	{
	  tree exponent = 0, x = 0;

	  switch (fcode)
	  {
	  CASE_FLT_FN (BUILT_IN_EXP):
	    /* Prepare to do logN(exp(exponent) -> exponent*logN(e).  */
	    x = build_real (type,
			    real_value_truncate (TYPE_MODE (type), dconste));
	    exponent = TREE_VALUE (TREE_OPERAND (arg, 1));
	    break;
	  CASE_FLT_FN (BUILT_IN_EXP2):
	    /* Prepare to do logN(exp2(exponent) -> exponent*logN(2).  */
	    x = build_real (type, dconst2);
	    exponent = TREE_VALUE (TREE_OPERAND (arg, 1));
	    break;
	  CASE_FLT_FN (BUILT_IN_EXP10):
	  CASE_FLT_FN (BUILT_IN_POW10):
	    /* Prepare to do logN(exp10(exponent) -> exponent*logN(10).  */
	    x = build_real (type, dconst10);
	    exponent = TREE_VALUE (TREE_OPERAND (arg, 1));
	    break;
	  CASE_FLT_FN (BUILT_IN_SQRT):
	    /* Prepare to do logN(sqrt(x) -> 0.5*logN(x).  */
	    x = TREE_VALUE (TREE_OPERAND (arg, 1));
	    exponent = build_real (type, dconsthalf);
	    break;
	  CASE_FLT_FN (BUILT_IN_CBRT):
	    /* Prepare to do logN(cbrt(x) -> (1/3)*logN(x).  */
	    x = TREE_VALUE (TREE_OPERAND (arg, 1));
	    exponent = build_real (type, real_value_truncate (TYPE_MODE (type),
							      dconstthird));
	    break;
	  CASE_FLT_FN (BUILT_IN_POW):
	    /* Prepare to do logN(pow(x,exponent) -> exponent*logN(x).  */
	    x = TREE_VALUE (TREE_OPERAND (arg, 1));
	    exponent = TREE_VALUE (TREE_CHAIN (TREE_OPERAND (arg, 1)));
	    break;
	  default:
	    break;
	  }

	  /* Now perform the optimization.  */
	  if (x && exponent)
	    {
	      tree logfn;
	      arglist = build_tree_list (NULL_TREE, x);
	      logfn = build_function_call_expr (fndecl, arglist);
	      return fold_build2 (MULT_EXPR, type, exponent, logfn);
	    }
	}
    }

  return 0;
}

/* Fold a builtin function call to pow, powf, or powl.  Return
   NULL_TREE if no simplification can be made.  */
static tree
fold_builtin_pow (tree fndecl, tree arglist, tree type)
{
  tree arg0 = TREE_VALUE (arglist);
  tree arg1 = TREE_VALUE (TREE_CHAIN (arglist));

  if (!validate_arglist (arglist, REAL_TYPE, REAL_TYPE, VOID_TYPE))
    return NULL_TREE;

  /* Optimize pow(1.0,y) = 1.0.  */
  if (real_onep (arg0))
    return omit_one_operand (type, build_real (type, dconst1), arg1);

  if (TREE_CODE (arg1) == REAL_CST
      && ! TREE_CONSTANT_OVERFLOW (arg1))
    {
      REAL_VALUE_TYPE cint;
      REAL_VALUE_TYPE c;
      HOST_WIDE_INT n;

      c = TREE_REAL_CST (arg1);

      /* Optimize pow(x,0.0) = 1.0.  */
      if (REAL_VALUES_EQUAL (c, dconst0))
	return omit_one_operand (type, build_real (type, dconst1),
				 arg0);

      /* Optimize pow(x,1.0) = x.  */
      if (REAL_VALUES_EQUAL (c, dconst1))
	return arg0;

      /* Optimize pow(x,-1.0) = 1.0/x.  */
      if (REAL_VALUES_EQUAL (c, dconstm1))
	return fold_build2 (RDIV_EXPR, type,
			    build_real (type, dconst1), arg0);

      /* Optimize pow(x,0.5) = sqrt(x).  */
      if (flag_unsafe_math_optimizations
	  && REAL_VALUES_EQUAL (c, dconsthalf))
	{
	  tree sqrtfn = mathfn_built_in (type, BUILT_IN_SQRT);

	  if (sqrtfn != NULL_TREE)
	    {
	      tree arglist = build_tree_list (NULL_TREE, arg0);
	      return build_function_call_expr (sqrtfn, arglist);
	    }
	}

      /* Check for an integer exponent.  */
      n = real_to_integer (&c);
      real_from_integer (&cint, VOIDmode, n, n < 0 ? -1 : 0, 0);
      if (real_identical (&c, &cint))
	{
	  /* Attempt to evaluate pow at compile-time.  */
	  if (TREE_CODE (arg0) == REAL_CST
	      && ! TREE_CONSTANT_OVERFLOW (arg0))
	    {
	      REAL_VALUE_TYPE x;
	      bool inexact;

	      x = TREE_REAL_CST (arg0);
	      inexact = real_powi (&x, TYPE_MODE (type), &x, n);
	      if (flag_unsafe_math_optimizations || !inexact)
		return build_real (type, x);
	    }

	  /* Strip sign ops from even integer powers.  */
	  if ((n & 1) == 0 && flag_unsafe_math_optimizations)
	    {
	      tree narg0 = fold_strip_sign_ops (arg0);
	      if (narg0)
		{
		  arglist = build_tree_list (NULL_TREE, arg1);
		  arglist = tree_cons (NULL_TREE, narg0, arglist);
		  return build_function_call_expr (fndecl, arglist);
		}
	    }
	}
    }

  if (flag_unsafe_math_optimizations)
    {
      const enum built_in_function fcode = builtin_mathfn_code (arg0);

      /* Optimize pow(expN(x),y) = expN(x*y).  */
      if (BUILTIN_EXPONENT_P (fcode))
	{
	  tree expfn = TREE_OPERAND (TREE_OPERAND (arg0, 0), 0);
	  tree arg = TREE_VALUE (TREE_OPERAND (arg0, 1));
	  arg = fold_build2 (MULT_EXPR, type, arg, arg1);
	  arglist = build_tree_list (NULL_TREE, arg);
	  return build_function_call_expr (expfn, arglist);
	}

      /* Optimize pow(sqrt(x),y) = pow(x,y*0.5).  */
      if (BUILTIN_SQRT_P (fcode))
	{
	  tree narg0 = TREE_VALUE (TREE_OPERAND (arg0, 1));
	  tree narg1 = fold_build2 (MULT_EXPR, type, arg1,
				    build_real (type, dconsthalf));

	  arglist = tree_cons (NULL_TREE, narg0,
			       build_tree_list (NULL_TREE, narg1));
	  return build_function_call_expr (fndecl, arglist);
	}

      /* Optimize pow(cbrt(x),y) = pow(x,y/3) iff x is nonnegative.  */
      if (BUILTIN_CBRT_P (fcode))
	{
	  tree arg = TREE_VALUE (TREE_OPERAND (arg0, 1));
	  if (tree_expr_nonnegative_p (arg))
	    {
	      const REAL_VALUE_TYPE dconstroot
		= real_value_truncate (TYPE_MODE (type), dconstthird);
	      tree narg1 = fold_build2 (MULT_EXPR, type, arg1,
					build_real (type, dconstroot));
	      arglist = tree_cons (NULL_TREE, arg,
				   build_tree_list (NULL_TREE, narg1));
	      return build_function_call_expr (fndecl, arglist);
	    }
	}

      /* Optimize pow(pow(x,y),z) = pow(x,y*z).  */
      if (fcode == BUILT_IN_POW || fcode == BUILT_IN_POWF
	   || fcode == BUILT_IN_POWL)
	{
	  tree arg00 = TREE_VALUE (TREE_OPERAND (arg0, 1));
	  tree arg01 = TREE_VALUE (TREE_CHAIN (TREE_OPERAND (arg0, 1)));
	  tree narg1 = fold_build2 (MULT_EXPR, type, arg01, arg1);
	  arglist = tree_cons (NULL_TREE, arg00,
			       build_tree_list (NULL_TREE, narg1));
	  return build_function_call_expr (fndecl, arglist);
	}
    }

  return NULL_TREE;
}

/* Fold a builtin function call to powi, powif, or powil.  Return
   NULL_TREE if no simplification can be made.  */
static tree
fold_builtin_powi (tree fndecl ATTRIBUTE_UNUSED, tree arglist, tree type)
{
  tree arg0 = TREE_VALUE (arglist);
  tree arg1 = TREE_VALUE (TREE_CHAIN (arglist));

  if (!validate_arglist (arglist, REAL_TYPE, INTEGER_TYPE, VOID_TYPE))
    return NULL_TREE;

  /* Optimize pow(1.0,y) = 1.0.  */
  if (real_onep (arg0))
    return omit_one_operand (type, build_real (type, dconst1), arg1);

  if (host_integerp (arg1, 0))
    {
      HOST_WIDE_INT c = TREE_INT_CST_LOW (arg1);

      /* Evaluate powi at compile-time.  */
      if (TREE_CODE (arg0) == REAL_CST
	  && ! TREE_CONSTANT_OVERFLOW (arg0))
	{
	  REAL_VALUE_TYPE x;
	  x = TREE_REAL_CST (arg0);
	  real_powi (&x, TYPE_MODE (type), &x, c);
	  return build_real (type, x);
	}

      /* Optimize pow(x,0) = 1.0.  */
      if (c == 0)
	return omit_one_operand (type, build_real (type, dconst1),
				 arg0);

      /* Optimize pow(x,1) = x.  */
      if (c == 1)
	return arg0;

      /* Optimize pow(x,-1) = 1.0/x.  */
      if (c == -1)
	return fold_build2 (RDIV_EXPR, type,
			   build_real (type, dconst1), arg0);
    }

  return NULL_TREE;
}

/* A subroutine of fold_builtin to fold the various exponent
   functions.  EXP is the CALL_EXPR of a call to a builtin function.
   VALUE is the value which will be raised to a power.  */

static tree
fold_builtin_exponent (tree fndecl, tree arglist,
		       const REAL_VALUE_TYPE *value)
{
  if (validate_arglist (arglist, REAL_TYPE, VOID_TYPE))
    {
      tree type = TREE_TYPE (TREE_TYPE (fndecl));
      tree arg = TREE_VALUE (arglist);

      /* Optimize exp*(0.0) = 1.0.  */
      if (real_zerop (arg))
	return build_real (type, dconst1);

      /* Optimize expN(1.0) = N.  */
      if (real_onep (arg))
	{
	  REAL_VALUE_TYPE cst;

	  real_convert (&cst, TYPE_MODE (type), value);
	  return build_real (type, cst);
	}

      /* Attempt to evaluate expN(integer) at compile-time.  */
      if (flag_unsafe_math_optimizations
	  && TREE_CODE (arg) == REAL_CST
	  && ! TREE_CONSTANT_OVERFLOW (arg))
	{
	  REAL_VALUE_TYPE cint;
	  REAL_VALUE_TYPE c;
	  HOST_WIDE_INT n;

	  c = TREE_REAL_CST (arg);
	  n = real_to_integer (&c);
	  real_from_integer (&cint, VOIDmode, n,
			     n < 0 ? -1 : 0, 0);
	  if (real_identical (&c, &cint))
	    {
	      REAL_VALUE_TYPE x;

	      real_powi (&x, TYPE_MODE (type), value, n);
	      return build_real (type, x);
	    }
	}

      /* Optimize expN(logN(x)) = x.  */
      if (flag_unsafe_math_optimizations)
	{
	  const enum built_in_function fcode = builtin_mathfn_code (arg);

	  if ((value == &dconste
	       && (fcode == BUILT_IN_LOG
		   || fcode == BUILT_IN_LOGF
		   || fcode == BUILT_IN_LOGL))
	      || (value == &dconst2
		  && (fcode == BUILT_IN_LOG2
		      || fcode == BUILT_IN_LOG2F
		      || fcode == BUILT_IN_LOG2L))
	      || (value == &dconst10
		  && (fcode == BUILT_IN_LOG10
		      || fcode == BUILT_IN_LOG10F
		      || fcode == BUILT_IN_LOG10L)))
	    return fold_convert (type, TREE_VALUE (TREE_OPERAND (arg, 1)));
	}
    }

  return 0;
}

/* Return true if VAR is a VAR_DECL or a component thereof.  */

static bool
var_decl_component_p (tree var)
{
  tree inner = var;
  while (handled_component_p (inner))
    inner = TREE_OPERAND (inner, 0);
  return SSA_VAR_P (inner);
}

/* Fold function call to builtin memset.  Return
   NULL_TREE if no simplification can be made.  */

static tree
fold_builtin_memset (tree arglist, tree type, bool ignore)
{
  tree dest, c, len, var, ret;
  unsigned HOST_WIDE_INT length, cval;

  if (!validate_arglist (arglist,
			 POINTER_TYPE, INTEGER_TYPE, INTEGER_TYPE, VOID_TYPE))
    return 0;

  dest = TREE_VALUE (arglist);
  c = TREE_VALUE (TREE_CHAIN (arglist));
  len = TREE_VALUE (TREE_CHAIN (TREE_CHAIN (arglist)));

  if (! host_integerp (len, 1))
    return 0;

  /* If the LEN parameter is zero, return DEST.  */
  if (integer_zerop (len))
    return omit_one_operand (type, dest, c);

  if (! host_integerp (c, 1) || TREE_SIDE_EFFECTS (dest))
    return 0;

  var = dest;
  STRIP_NOPS (var);
  if (TREE_CODE (var) != ADDR_EXPR)
    return 0;

  var = TREE_OPERAND (var, 0);
  if (TREE_THIS_VOLATILE (var))
    return 0;

  if (!INTEGRAL_TYPE_P (TREE_TYPE (var))
      && !POINTER_TYPE_P (TREE_TYPE (var)))
    return 0;

  if (! var_decl_component_p (var))
    return 0;

  length = tree_low_cst (len, 1);
  if (GET_MODE_SIZE (TYPE_MODE (TREE_TYPE (var))) != length
      || get_pointer_alignment (dest, BIGGEST_ALIGNMENT) / BITS_PER_UNIT
	 < (int) length)
    return 0;

  if (length > HOST_BITS_PER_WIDE_INT / BITS_PER_UNIT)
    return 0;

  if (integer_zerop (c))
    cval = 0;
  else
    {
      if (CHAR_BIT != 8 || BITS_PER_UNIT != 8 || HOST_BITS_PER_WIDE_INT > 64)
	return 0;

      cval = tree_low_cst (c, 1);
      cval &= 0xff;
      cval |= cval << 8;
      cval |= cval << 16;
      cval |= (cval << 31) << 1;
    }

  ret = build_int_cst_type (TREE_TYPE (var), cval);
  ret = build2 (MODIFY_EXPR, TREE_TYPE (var), var, ret);
  if (ignore)
    return ret;

  return omit_one_operand (type, dest, ret);
}

/* Fold function call to builtin memset.  Return
   NULL_TREE if no simplification can be made.  */

static tree
fold_builtin_bzero (tree arglist, bool ignore)
{
  tree dest, size, newarglist;

  if (!validate_arglist (arglist, POINTER_TYPE, INTEGER_TYPE, VOID_TYPE))
    return 0;

  if (!ignore)
    return 0;

  dest = TREE_VALUE (arglist);
  size = TREE_VALUE (TREE_CHAIN (arglist));

  /* New argument list transforming bzero(ptr x, int y) to
     memset(ptr x, int 0, size_t y).   This is done this way
     so that if it isn't expanded inline, we fallback to
     calling bzero instead of memset.  */

  newarglist = build_tree_list (NULL_TREE, fold_convert (sizetype, size));
  newarglist = tree_cons (NULL_TREE, integer_zero_node, newarglist);
  newarglist = tree_cons (NULL_TREE, dest, newarglist);
  return fold_builtin_memset (newarglist, void_type_node, ignore);
}

/* Fold function call to builtin mem{{,p}cpy,move}.  Return
   NULL_TREE if no simplification can be made.
   If ENDP is 0, return DEST (like memcpy).
   If ENDP is 1, return DEST+LEN (like mempcpy).
   If ENDP is 2, return DEST+LEN-1 (like stpcpy).
   If ENDP is 3, return DEST, additionally *SRC and *DEST may overlap
   (memmove).   */

static tree
fold_builtin_memory_op (tree arglist, tree type, bool ignore, int endp)
{
  tree dest, src, len, destvar, srcvar, expr;
  unsigned HOST_WIDE_INT length;

  if (! validate_arglist (arglist,
			  POINTER_TYPE, POINTER_TYPE, INTEGER_TYPE, VOID_TYPE))
    return 0;

  dest = TREE_VALUE (arglist);
  src = TREE_VALUE (TREE_CHAIN (arglist));
  len = TREE_VALUE (TREE_CHAIN (TREE_CHAIN (arglist)));

  /* If the LEN parameter is zero, return DEST.  */
  if (integer_zerop (len))
    return omit_one_operand (type, dest, src);

  /* If SRC and DEST are the same (and not volatile), return
     DEST{,+LEN,+LEN-1}.  */
  if (operand_equal_p (src, dest, 0))
    expr = len;
  else
    {
      if (! host_integerp (len, 1))
	return 0;

      if (TREE_SIDE_EFFECTS (dest) || TREE_SIDE_EFFECTS (src))
	return 0;

      destvar = dest;
      STRIP_NOPS (destvar);
      if (TREE_CODE (destvar) != ADDR_EXPR)
	return 0;

      destvar = TREE_OPERAND (destvar, 0);
      if (TREE_THIS_VOLATILE (destvar))
	return 0;

      if (!INTEGRAL_TYPE_P (TREE_TYPE (destvar))
	  && !POINTER_TYPE_P (TREE_TYPE (destvar))
	  && !SCALAR_FLOAT_TYPE_P (TREE_TYPE (destvar)))
	return 0;

      if (! var_decl_component_p (destvar))
	return 0;

      srcvar = src;
      STRIP_NOPS (srcvar);
      if (TREE_CODE (srcvar) != ADDR_EXPR)
	return 0;

      srcvar = TREE_OPERAND (srcvar, 0);
      if (TREE_THIS_VOLATILE (srcvar))
	return 0;

      if (!INTEGRAL_TYPE_P (TREE_TYPE (srcvar))
	  && !POINTER_TYPE_P (TREE_TYPE (srcvar))
	  && !SCALAR_FLOAT_TYPE_P (TREE_TYPE (srcvar)))
	return 0;

      if (! var_decl_component_p (srcvar))
	return 0;

      length = tree_low_cst (len, 1);
      if (GET_MODE_SIZE (TYPE_MODE (TREE_TYPE (destvar))) != length
	  || get_pointer_alignment (dest, BIGGEST_ALIGNMENT) / BITS_PER_UNIT
	     < (int) length
	  || GET_MODE_SIZE (TYPE_MODE (TREE_TYPE (srcvar))) != length
	  || get_pointer_alignment (src, BIGGEST_ALIGNMENT) / BITS_PER_UNIT
	     < (int) length)
	return 0;

      if ((INTEGRAL_TYPE_P (TREE_TYPE (srcvar))
	   || POINTER_TYPE_P (TREE_TYPE (srcvar)))
	  && (INTEGRAL_TYPE_P (TREE_TYPE (destvar))
	      || POINTER_TYPE_P (TREE_TYPE (destvar))))
	expr = fold_convert (TREE_TYPE (destvar), srcvar);
      else
	expr = fold_build1 (VIEW_CONVERT_EXPR, TREE_TYPE (destvar), srcvar);
      expr = build2 (MODIFY_EXPR, TREE_TYPE (destvar), destvar, expr);
    }

  if (ignore)
    return expr;

  if (endp == 0 || endp == 3)
    return omit_one_operand (type, dest, expr);

  if (expr == len)
    expr = 0;

  if (endp == 2)
    len = fold_build2 (MINUS_EXPR, TREE_TYPE (len), len,
		       ssize_int (1));

  len = fold_convert (TREE_TYPE (dest), len);
  dest = fold_build2 (PLUS_EXPR, TREE_TYPE (dest), dest, len);
  dest = fold_convert (type, dest);
  if (expr)
    dest = omit_one_operand (type, dest, expr);
  return dest;
}

/* Fold function call to builtin bcopy.  Return NULL_TREE if no
   simplification can be made.  */

static tree
fold_builtin_bcopy (tree arglist, bool ignore)
{
  tree src, dest, size, newarglist;

  if (!validate_arglist (arglist,
			 POINTER_TYPE, POINTER_TYPE, INTEGER_TYPE, VOID_TYPE))
    return 0;

  if (! ignore)
    return 0;

  src = TREE_VALUE (arglist);
  dest = TREE_VALUE (TREE_CHAIN (arglist));
  size = TREE_VALUE (TREE_CHAIN (TREE_CHAIN (arglist)));

  /* New argument list transforming bcopy(ptr x, ptr y, int z) to
     memmove(ptr y, ptr x, size_t z).   This is done this way
     so that if it isn't expanded inline, we fallback to
     calling bcopy instead of memmove.  */

  newarglist = build_tree_list (NULL_TREE, fold_convert (sizetype, size));
  newarglist = tree_cons (NULL_TREE, src, newarglist);
  newarglist = tree_cons (NULL_TREE, dest, newarglist);

  return fold_builtin_memory_op (newarglist, void_type_node, true, /*endp=*/3);
}

/* Fold function call to builtin strcpy.  If LEN is not NULL, it represents
   the length of the string to be copied.  Return NULL_TREE if no
   simplification can be made.  */

tree
fold_builtin_strcpy (tree fndecl, tree arglist, tree len)
{
  tree dest, src, fn;

  if (!validate_arglist (arglist,
			 POINTER_TYPE, POINTER_TYPE, VOID_TYPE))
    return 0;

  dest = TREE_VALUE (arglist);
  src = TREE_VALUE (TREE_CHAIN (arglist));

  /* If SRC and DEST are the same (and not volatile), return DEST.  */
  if (operand_equal_p (src, dest, 0))
    return fold_convert (TREE_TYPE (TREE_TYPE (fndecl)), dest);

  if (optimize_size)
    return 0;

  fn = implicit_built_in_decls[BUILT_IN_MEMCPY];
  if (!fn)
    return 0;

  if (!len)
    {
      len = c_strlen (src, 1);
      if (! len || TREE_SIDE_EFFECTS (len))
	return 0;
    }

  len = size_binop (PLUS_EXPR, len, ssize_int (1));
  arglist = build_tree_list (NULL_TREE, len);
  arglist = tree_cons (NULL_TREE, src, arglist);
  arglist = tree_cons (NULL_TREE, dest, arglist);
  return fold_convert (TREE_TYPE (TREE_TYPE (fndecl)),
		       build_function_call_expr (fn, arglist));
}

/* Fold function call to builtin strncpy.  If SLEN is not NULL, it represents
   the length of the source string.  Return NULL_TREE if no simplification
   can be made.  */

tree
fold_builtin_strncpy (tree fndecl, tree arglist, tree slen)
{
  tree dest, src, len, fn;

  if (!validate_arglist (arglist,
			 POINTER_TYPE, POINTER_TYPE, INTEGER_TYPE, VOID_TYPE))
    return 0;

  dest = TREE_VALUE (arglist);
  src = TREE_VALUE (TREE_CHAIN (arglist));
  len = TREE_VALUE (TREE_CHAIN (TREE_CHAIN (arglist)));

  /* If the LEN parameter is zero, return DEST.  */
  if (integer_zerop (len))
    return omit_one_operand (TREE_TYPE (TREE_TYPE (fndecl)), dest, src);

  /* We can't compare slen with len as constants below if len is not a
     constant.  */
  if (len == 0 || TREE_CODE (len) != INTEGER_CST)
    return 0;

  if (!slen)
    slen = c_strlen (src, 1);

  /* Now, we must be passed a constant src ptr parameter.  */
  if (slen == 0 || TREE_CODE (slen) != INTEGER_CST)
    return 0;

  slen = size_binop (PLUS_EXPR, slen, ssize_int (1));

  /* We do not support simplification of this case, though we do
     support it when expanding trees into RTL.  */
  /* FIXME: generate a call to __builtin_memset.  */
  if (tree_int_cst_lt (slen, len))
    return 0;

  /* OK transform into builtin memcpy.  */
  fn = implicit_built_in_decls[BUILT_IN_MEMCPY];
  if (!fn)
    return 0;
  return fold_convert (TREE_TYPE (TREE_TYPE (fndecl)),
		       build_function_call_expr (fn, arglist));
}

/* Fold function call to builtin memcmp.  Return
   NULL_TREE if no simplification can be made.  */

static tree
fold_builtin_memcmp (tree arglist)
{
  tree arg1, arg2, len;
  const char *p1, *p2;

  if (!validate_arglist (arglist,
			 POINTER_TYPE, POINTER_TYPE, INTEGER_TYPE, VOID_TYPE))
    return 0;

  arg1 = TREE_VALUE (arglist);
  arg2 = TREE_VALUE (TREE_CHAIN (arglist));
  len = TREE_VALUE (TREE_CHAIN (TREE_CHAIN (arglist)));

  /* If the LEN parameter is zero, return zero.  */
  if (integer_zerop (len))
    return omit_two_operands (integer_type_node, integer_zero_node,
			      arg1, arg2);

  /* If ARG1 and ARG2 are the same (and not volatile), return zero.  */
  if (operand_equal_p (arg1, arg2, 0))
    return omit_one_operand (integer_type_node, integer_zero_node, len);

  p1 = c_getstr (arg1);
  p2 = c_getstr (arg2);

  /* If all arguments are constant, and the value of len is not greater
     than the lengths of arg1 and arg2, evaluate at compile-time.  */
  if (host_integerp (len, 1) && p1 && p2
      && compare_tree_int (len, strlen (p1) + 1) <= 0
      && compare_tree_int (len, strlen (p2) + 1) <= 0)
    {
      const int r = memcmp (p1, p2, tree_low_cst (len, 1));

      if (r > 0)
	return integer_one_node;
      else if (r < 0)
	return integer_minus_one_node;
      else
	return integer_zero_node;
    }

  /* If len parameter is one, return an expression corresponding to
     (*(const unsigned char*)arg1 - (const unsigned char*)arg2).  */
  if (host_integerp (len, 1) && tree_low_cst (len, 1) == 1)
    {
      tree cst_uchar_node = build_type_variant (unsigned_char_type_node, 1, 0);
      tree cst_uchar_ptr_node
	= build_pointer_type_for_mode (cst_uchar_node, ptr_mode, true);

      tree ind1 = fold_convert (integer_type_node,
				build1 (INDIRECT_REF, cst_uchar_node,
					fold_convert (cst_uchar_ptr_node,
						      arg1)));
      tree ind2 = fold_convert (integer_type_node,
				build1 (INDIRECT_REF, cst_uchar_node,
					fold_convert (cst_uchar_ptr_node,
						      arg2)));
      return fold_build2 (MINUS_EXPR, integer_type_node, ind1, ind2);
    }

  return 0;
}

/* Fold function call to builtin strcmp.  Return
   NULL_TREE if no simplification can be made.  */

static tree
fold_builtin_strcmp (tree arglist)
{
  tree arg1, arg2;
  const char *p1, *p2;

  if (!validate_arglist (arglist, POINTER_TYPE, POINTER_TYPE, VOID_TYPE))
    return 0;

  arg1 = TREE_VALUE (arglist);
  arg2 = TREE_VALUE (TREE_CHAIN (arglist));

  /* If ARG1 and ARG2 are the same (and not volatile), return zero.  */
  if (operand_equal_p (arg1, arg2, 0))
    return integer_zero_node;

  p1 = c_getstr (arg1);
  p2 = c_getstr (arg2);

  if (p1 && p2)
    {
      const int i = strcmp (p1, p2);
      if (i < 0)
	return integer_minus_one_node;
      else if (i > 0)
	return integer_one_node;
      else
	return integer_zero_node;
    }

  /* If the second arg is "", return *(const unsigned char*)arg1.  */
  if (p2 && *p2 == '\0')
    {
      tree cst_uchar_node = build_type_variant (unsigned_char_type_node, 1, 0);
      tree cst_uchar_ptr_node
	= build_pointer_type_for_mode (cst_uchar_node, ptr_mode, true);

      return fold_convert (integer_type_node,
			   build1 (INDIRECT_REF, cst_uchar_node,
				   fold_convert (cst_uchar_ptr_node,
						 arg1)));
    }

  /* If the first arg is "", return -*(const unsigned char*)arg2.  */
  if (p1 && *p1 == '\0')
    {
      tree cst_uchar_node = build_type_variant (unsigned_char_type_node, 1, 0);
      tree cst_uchar_ptr_node
	= build_pointer_type_for_mode (cst_uchar_node, ptr_mode, true);

      tree temp = fold_convert (integer_type_node,
				build1 (INDIRECT_REF, cst_uchar_node,
					fold_convert (cst_uchar_ptr_node,
						      arg2)));
      return fold_build1 (NEGATE_EXPR, integer_type_node, temp);
    }

  return 0;
}

/* Fold function call to builtin strncmp.  Return
   NULL_TREE if no simplification can be made.  */

static tree
fold_builtin_strncmp (tree arglist)
{
  tree arg1, arg2, len;
  const char *p1, *p2;

  if (!validate_arglist (arglist,
			 POINTER_TYPE, POINTER_TYPE, INTEGER_TYPE, VOID_TYPE))
    return 0;

  arg1 = TREE_VALUE (arglist);
  arg2 = TREE_VALUE (TREE_CHAIN (arglist));
  len = TREE_VALUE (TREE_CHAIN (TREE_CHAIN (arglist)));

  /* If the LEN parameter is zero, return zero.  */
  if (integer_zerop (len))
    return omit_two_operands (integer_type_node, integer_zero_node,
			      arg1, arg2);

  /* If ARG1 and ARG2 are the same (and not volatile), return zero.  */
  if (operand_equal_p (arg1, arg2, 0))
    return omit_one_operand (integer_type_node, integer_zero_node, len);

  p1 = c_getstr (arg1);
  p2 = c_getstr (arg2);

  if (host_integerp (len, 1) && p1 && p2)
    {
      const int i = strncmp (p1, p2, tree_low_cst (len, 1));
      if (i > 0)
	return integer_one_node;
      else if (i < 0)
	return integer_minus_one_node;
      else
	return integer_zero_node;
    }

  /* If the second arg is "", and the length is greater than zero,
     return *(const unsigned char*)arg1.  */
  if (p2 && *p2 == '\0'
      && TREE_CODE (len) == INTEGER_CST
      && tree_int_cst_sgn (len) == 1)
    {
      tree cst_uchar_node = build_type_variant (unsigned_char_type_node, 1, 0);
      tree cst_uchar_ptr_node
	= build_pointer_type_for_mode (cst_uchar_node, ptr_mode, true);

      return fold_convert (integer_type_node,
			   build1 (INDIRECT_REF, cst_uchar_node,
				   fold_convert (cst_uchar_ptr_node,
						 arg1)));
    }

  /* If the first arg is "", and the length is greater than zero,
     return -*(const unsigned char*)arg2.  */
  if (p1 && *p1 == '\0'
      && TREE_CODE (len) == INTEGER_CST
      && tree_int_cst_sgn (len) == 1)
    {
      tree cst_uchar_node = build_type_variant (unsigned_char_type_node, 1, 0);
      tree cst_uchar_ptr_node
	= build_pointer_type_for_mode (cst_uchar_node, ptr_mode, true);

      tree temp = fold_convert (integer_type_node,
				build1 (INDIRECT_REF, cst_uchar_node,
					fold_convert (cst_uchar_ptr_node,
						      arg2)));
      return fold_build1 (NEGATE_EXPR, integer_type_node, temp);
    }

  /* If len parameter is one, return an expression corresponding to
     (*(const unsigned char*)arg1 - (const unsigned char*)arg2).  */
  if (host_integerp (len, 1) && tree_low_cst (len, 1) == 1)
    {
      tree cst_uchar_node = build_type_variant (unsigned_char_type_node, 1, 0);
      tree cst_uchar_ptr_node
	= build_pointer_type_for_mode (cst_uchar_node, ptr_mode, true);

      tree ind1 = fold_convert (integer_type_node,
				build1 (INDIRECT_REF, cst_uchar_node,
					fold_convert (cst_uchar_ptr_node,
						      arg1)));
      tree ind2 = fold_convert (integer_type_node,
				build1 (INDIRECT_REF, cst_uchar_node,
					fold_convert (cst_uchar_ptr_node,
						      arg2)));
      return fold_build2 (MINUS_EXPR, integer_type_node, ind1, ind2);
    }

  return 0;
}

/* Fold function call to builtin signbit, signbitf or signbitl.  Return
   NULL_TREE if no simplification can be made.  */

static tree
fold_builtin_signbit (tree fndecl, tree arglist)
{
  tree type = TREE_TYPE (TREE_TYPE (fndecl));
  tree arg, temp;

  if (!validate_arglist (arglist, REAL_TYPE, VOID_TYPE))
    return NULL_TREE;

  arg = TREE_VALUE (arglist);

  /* If ARG is a compile-time constant, determine the result.  */
  if (TREE_CODE (arg) == REAL_CST
      && !TREE_CONSTANT_OVERFLOW (arg))
    {
      REAL_VALUE_TYPE c;

      c = TREE_REAL_CST (arg);
      temp = REAL_VALUE_NEGATIVE (c) ? integer_one_node : integer_zero_node;
      return fold_convert (type, temp);
    }

  /* If ARG is non-negative, the result is always zero.  */
  if (tree_expr_nonnegative_p (arg))
    return omit_one_operand (type, integer_zero_node, arg);

  /* If ARG's format doesn't have signed zeros, return "arg < 0.0".  */
  if (!HONOR_SIGNED_ZEROS (TYPE_MODE (TREE_TYPE (arg))))
    return fold_build2 (LT_EXPR, type, arg,
			build_real (TREE_TYPE (arg), dconst0));

  return NULL_TREE;
}

/* Fold function call to builtin copysign, copysignf or copysignl.
   Return NULL_TREE if no simplification can be made.  */

static tree
fold_builtin_copysign (tree fndecl, tree arglist, tree type)
{
  tree arg1, arg2, tem;

  if (!validate_arglist (arglist, REAL_TYPE, REAL_TYPE, VOID_TYPE))
    return NULL_TREE;

  arg1 = TREE_VALUE (arglist);
  arg2 = TREE_VALUE (TREE_CHAIN (arglist));

  /* copysign(X,X) is X.  */
  if (operand_equal_p (arg1, arg2, 0))
    return fold_convert (type, arg1);

  /* If ARG1 and ARG2 are compile-time constants, determine the result.  */
  if (TREE_CODE (arg1) == REAL_CST
      && TREE_CODE (arg2) == REAL_CST
      && !TREE_CONSTANT_OVERFLOW (arg1)
      && !TREE_CONSTANT_OVERFLOW (arg2))
    {
      REAL_VALUE_TYPE c1, c2;

      c1 = TREE_REAL_CST (arg1);
      c2 = TREE_REAL_CST (arg2);
      /* c1.sign := c2.sign.  */
      real_copysign (&c1, &c2);
      return build_real (type, c1);
    }

  /* copysign(X, Y) is fabs(X) when Y is always non-negative.
     Remember to evaluate Y for side-effects.  */
  if (tree_expr_nonnegative_p (arg2))
    return omit_one_operand (type,
			     fold_build1 (ABS_EXPR, type, arg1),
			     arg2);

  /* Strip sign changing operations for the first argument.  */
  tem = fold_strip_sign_ops (arg1);
  if (tem)
    {
      arglist = tree_cons (NULL_TREE, tem, TREE_CHAIN (arglist));
      return build_function_call_expr (fndecl, arglist);
    }

  return NULL_TREE;
}

/* Fold a call to builtin isascii.  */

static tree
fold_builtin_isascii (tree arglist)
{
  if (! validate_arglist (arglist, INTEGER_TYPE, VOID_TYPE))
    return 0;
  else
    {
      /* Transform isascii(c) -> ((c & ~0x7f) == 0).  */
      tree arg = TREE_VALUE (arglist);

      arg = build2 (BIT_AND_EXPR, integer_type_node, arg,
		    build_int_cst (NULL_TREE,
				   ~ (unsigned HOST_WIDE_INT) 0x7f));
      return fold_build2 (EQ_EXPR, integer_type_node,
			  arg, integer_zero_node);
    }
}

/* Fold a call to builtin toascii.  */

static tree
fold_builtin_toascii (tree arglist)
{
  if (! validate_arglist (arglist, INTEGER_TYPE, VOID_TYPE))
    return 0;
  else
    {
      /* Transform toascii(c) -> (c & 0x7f).  */
      tree arg = TREE_VALUE (arglist);

      return fold_build2 (BIT_AND_EXPR, integer_type_node, arg,
			  build_int_cst (NULL_TREE, 0x7f));
    }
}

/* Fold a call to builtin isdigit.  */

static tree
fold_builtin_isdigit (tree arglist)
{
  if (! validate_arglist (arglist, INTEGER_TYPE, VOID_TYPE))
    return 0;
  else
    {
      /* Transform isdigit(c) -> (unsigned)(c) - '0' <= 9.  */
      /* According to the C standard, isdigit is unaffected by locale.
	 However, it definitely is affected by the target character set.  */
      tree arg;
      unsigned HOST_WIDE_INT target_digit0
	= lang_hooks.to_target_charset ('0');

      if (target_digit0 == 0)
	return NULL_TREE;

      arg = fold_convert (unsigned_type_node, TREE_VALUE (arglist));
      arg = build2 (MINUS_EXPR, unsigned_type_node, arg,
		    build_int_cst (unsigned_type_node, target_digit0));
      return fold_build2 (LE_EXPR, integer_type_node, arg,
			  build_int_cst (unsigned_type_node, 9));
    }
}

/* Fold a call to fabs, fabsf or fabsl.  */

static tree
fold_builtin_fabs (tree arglist, tree type)
{
  tree arg;

  if (!validate_arglist (arglist, REAL_TYPE, VOID_TYPE))
    return 0;

  arg = TREE_VALUE (arglist);
  arg = fold_convert (type, arg);
  if (TREE_CODE (arg) == REAL_CST)
    return fold_abs_const (arg, type);
  return fold_build1 (ABS_EXPR, type, arg);
}

/* Fold a call to abs, labs, llabs or imaxabs.  */

static tree
fold_builtin_abs (tree arglist, tree type)
{
  tree arg;

  if (!validate_arglist (arglist, INTEGER_TYPE, VOID_TYPE))
    return 0;

  arg = TREE_VALUE (arglist);
  arg = fold_convert (type, arg);
  if (TREE_CODE (arg) == INTEGER_CST)
    return fold_abs_const (arg, type);
  return fold_build1 (ABS_EXPR, type, arg);
}

/* Fold a call to __builtin_isnan(), __builtin_isinf, __builtin_finite.
   EXP is the CALL_EXPR for the call.  */

static tree
fold_builtin_classify (tree fndecl, tree arglist, int builtin_index)
{
  tree type = TREE_TYPE (TREE_TYPE (fndecl));
  tree arg;
  REAL_VALUE_TYPE r;

  if (!validate_arglist (arglist, REAL_TYPE, VOID_TYPE))
    {
      /* Check that we have exactly one argument.  */
      if (arglist == 0)
	{
	  error ("too few arguments to function %qs",
		 IDENTIFIER_POINTER (DECL_NAME (fndecl)));
	  return error_mark_node;
	}
      else if (TREE_CHAIN (arglist) != 0)
	{
	  error ("too many arguments to function %qs",
		 IDENTIFIER_POINTER (DECL_NAME (fndecl)));
	  return error_mark_node;
	}
      else
	{
	  error ("non-floating-point argument to function %qs",
		 IDENTIFIER_POINTER (DECL_NAME (fndecl)));
	  return error_mark_node;
	}
    }

  arg = TREE_VALUE (arglist);
  switch (builtin_index)
    {
    case BUILT_IN_ISINF:
      if (!MODE_HAS_INFINITIES (TYPE_MODE (TREE_TYPE (arg))))
	return omit_one_operand (type, integer_zero_node, arg);

      if (TREE_CODE (arg) == REAL_CST)
	{
	  r = TREE_REAL_CST (arg);
	  if (real_isinf (&r))
	    return real_compare (GT_EXPR, &r, &dconst0)
		   ? integer_one_node : integer_minus_one_node;
	  else
	    return integer_zero_node;
	}

      return NULL_TREE;

    case BUILT_IN_FINITE:
      if (!MODE_HAS_NANS (TYPE_MODE (TREE_TYPE (arg)))
	  && !MODE_HAS_INFINITIES (TYPE_MODE (TREE_TYPE (arg))))
	return omit_one_operand (type, integer_zero_node, arg);

      if (TREE_CODE (arg) == REAL_CST)
	{
	  r = TREE_REAL_CST (arg);
	  return real_isinf (&r) || real_isnan (&r)
		 ? integer_zero_node : integer_one_node;
	}

      return NULL_TREE;

    case BUILT_IN_ISNAN:
      if (!MODE_HAS_NANS (TYPE_MODE (TREE_TYPE (arg))))
	return omit_one_operand (type, integer_zero_node, arg);

      if (TREE_CODE (arg) == REAL_CST)
	{
	  r = TREE_REAL_CST (arg);
	  return real_isnan (&r) ? integer_one_node : integer_zero_node;
	}

      arg = builtin_save_expr (arg);
      return fold_build2 (UNORDERED_EXPR, type, arg, arg);

    default:
      gcc_unreachable ();
    }
}

/* Fold a call to an unordered comparison function such as
   __builtin_isgreater().  FNDECL is the FUNCTION_DECL for the function
   being called and ARGLIST is the argument list for the call.
   UNORDERED_CODE and ORDERED_CODE are comparison codes that give
   the opposite of the desired result.  UNORDERED_CODE is used
   for modes that can hold NaNs and ORDERED_CODE is used for
   the rest.  */

static tree
fold_builtin_unordered_cmp (tree fndecl, tree arglist,
			    enum tree_code unordered_code,
			    enum tree_code ordered_code)
{
  tree type = TREE_TYPE (TREE_TYPE (fndecl));
  enum tree_code code;
  tree arg0, arg1;
  tree type0, type1;
  enum tree_code code0, code1;
  tree cmp_type = NULL_TREE;

  if (!validate_arglist (arglist, REAL_TYPE, REAL_TYPE, VOID_TYPE))
    {
      /* Check that we have exactly two arguments.  */
      if (arglist == 0 || TREE_CHAIN (arglist) == 0)
	{
	  error ("too few arguments to function %qs",
		 IDENTIFIER_POINTER (DECL_NAME (fndecl)));
	  return error_mark_node;
	}
      else if (TREE_CHAIN (TREE_CHAIN (arglist)) != 0)
	{
	  error ("too many arguments to function %qs",
		 IDENTIFIER_POINTER (DECL_NAME (fndecl)));
	  return error_mark_node;
	}
    }

  arg0 = TREE_VALUE (arglist);
  arg1 = TREE_VALUE (TREE_CHAIN (arglist));

  type0 = TREE_TYPE (arg0);
  type1 = TREE_TYPE (arg1);

  code0 = TREE_CODE (type0);
  code1 = TREE_CODE (type1);

  if (code0 == REAL_TYPE && code1 == REAL_TYPE)
    /* Choose the wider of two real types.  */
    cmp_type = TYPE_PRECISION (type0) >= TYPE_PRECISION (type1)
      ? type0 : type1;
  else if (code0 == REAL_TYPE && code1 == INTEGER_TYPE)
    cmp_type = type0;
  else if (code0 == INTEGER_TYPE && code1 == REAL_TYPE)
    cmp_type = type1;
  else
    {
      error ("non-floating-point argument to function %qs",
		 IDENTIFIER_POINTER (DECL_NAME (fndecl)));
      return error_mark_node;
    }

  arg0 = fold_convert (cmp_type, arg0);
  arg1 = fold_convert (cmp_type, arg1);

  if (unordered_code == UNORDERED_EXPR)
    {
      if (!MODE_HAS_NANS (TYPE_MODE (TREE_TYPE (arg0))))
	return omit_two_operands (type, integer_zero_node, arg0, arg1);
      return fold_build2 (UNORDERED_EXPR, type, arg0, arg1);
    }

  code = MODE_HAS_NANS (TYPE_MODE (TREE_TYPE (arg0))) ? unordered_code
						      : ordered_code;
  return fold_build1 (TRUTH_NOT_EXPR, type,
		      fold_build2 (code, type, arg0, arg1));
}

/* Used by constant folding to simplify calls to builtin functions.  EXP is
   the CALL_EXPR of a call to a builtin function.  IGNORE is true if the
   result of the function call is ignored.  This function returns NULL_TREE
   if no simplification was possible.  */

static tree
fold_builtin_1 (tree fndecl, tree arglist, bool ignore)
{
  tree type = TREE_TYPE (TREE_TYPE (fndecl));
  enum built_in_function fcode;

  if (DECL_BUILT_IN_CLASS (fndecl) == BUILT_IN_MD)
    return targetm.fold_builtin (fndecl, arglist, ignore);

  fcode = DECL_FUNCTION_CODE (fndecl);
  switch (fcode)
    {
    case BUILT_IN_FPUTS:
      return fold_builtin_fputs (arglist, ignore, false, NULL_TREE);

    case BUILT_IN_FPUTS_UNLOCKED:
      return fold_builtin_fputs (arglist, ignore, true, NULL_TREE);

    case BUILT_IN_STRSTR:
      return fold_builtin_strstr (arglist, type);

#ifndef NO_UNSAFE_BUILTINS
    case BUILT_IN_STRCAT:
      return fold_builtin_strcat (arglist);
#endif

    case BUILT_IN_STRNCAT:
      return fold_builtin_strncat (arglist);

    case BUILT_IN_STRSPN:
      return fold_builtin_strspn (arglist);

    case BUILT_IN_STRCSPN:
      return fold_builtin_strcspn (arglist);

    case BUILT_IN_STRCHR:
    case BUILT_IN_INDEX:
      return fold_builtin_strchr (arglist, type);

    case BUILT_IN_STRRCHR:
    case BUILT_IN_RINDEX:
      return fold_builtin_strrchr (arglist, type);

#ifndef NO_UNSAFE_BUILTINS
    case BUILT_IN_STRCPY:
      return fold_builtin_strcpy (fndecl, arglist, NULL_TREE);
#endif

    case BUILT_IN_STRNCPY:
      return fold_builtin_strncpy (fndecl, arglist, NULL_TREE);

    case BUILT_IN_STRCMP:
      return fold_builtin_strcmp (arglist);

    case BUILT_IN_STRNCMP:
      return fold_builtin_strncmp (arglist);

    case BUILT_IN_STRPBRK:
      return fold_builtin_strpbrk (arglist, type);

    case BUILT_IN_BCMP:
    case BUILT_IN_MEMCMP:
      return fold_builtin_memcmp (arglist);

#ifndef NO_UNSAFE_BUILTINS
    case BUILT_IN_SPRINTF:
      return fold_builtin_sprintf (arglist, ignore);
#endif

    case BUILT_IN_CONSTANT_P:
      {
	tree val;

	val = fold_builtin_constant_p (arglist);
	/* Gimplification will pull the CALL_EXPR for the builtin out of
	   an if condition.  When not optimizing, we'll not CSE it back.
	   To avoid link error types of regressions, return false now.  */
	if (!val && !optimize)
	  val = integer_zero_node;

	return val;
      }

    case BUILT_IN_EXPECT:
      return fold_builtin_expect (arglist);

    case BUILT_IN_CLASSIFY_TYPE:
      return fold_builtin_classify_type (arglist);

    case BUILT_IN_STRLEN:
      return fold_builtin_strlen (arglist);

    CASE_FLT_FN (BUILT_IN_FABS):
      return fold_builtin_fabs (arglist, type);

    case BUILT_IN_ABS:
    case BUILT_IN_LABS:
    case BUILT_IN_LLABS:
    case BUILT_IN_IMAXABS:
      return fold_builtin_abs (arglist, type);

    CASE_FLT_FN (BUILT_IN_CONJ):
      if (validate_arglist (arglist, COMPLEX_TYPE, VOID_TYPE))
	return fold_build1 (CONJ_EXPR, type, TREE_VALUE (arglist));
      break;

    CASE_FLT_FN (BUILT_IN_CREAL):
      if (validate_arglist (arglist, COMPLEX_TYPE, VOID_TYPE))
	return non_lvalue (fold_build1 (REALPART_EXPR, type,
					TREE_VALUE (arglist)));
      break;

    CASE_FLT_FN (BUILT_IN_CIMAG):
      if (validate_arglist (arglist, COMPLEX_TYPE, VOID_TYPE))
	return non_lvalue (fold_build1 (IMAGPART_EXPR, type,
					TREE_VALUE (arglist)));
      break;

    CASE_FLT_FN (BUILT_IN_CABS):
      return fold_builtin_cabs (arglist, type, fndecl);

    CASE_FLT_FN (BUILT_IN_SQRT):
      return fold_builtin_sqrt (arglist, type);

    CASE_FLT_FN (BUILT_IN_CBRT):
      return fold_builtin_cbrt (arglist, type);

    CASE_FLT_FN (BUILT_IN_SIN):
      return fold_builtin_sin (arglist);

    CASE_FLT_FN (BUILT_IN_COS):
      return fold_builtin_cos (arglist, type, fndecl);

    CASE_FLT_FN (BUILT_IN_EXP):
      return fold_builtin_exponent (fndecl, arglist, &dconste);

    CASE_FLT_FN (BUILT_IN_EXP2):
      return fold_builtin_exponent (fndecl, arglist, &dconst2);

    CASE_FLT_FN (BUILT_IN_EXP10):
    CASE_FLT_FN (BUILT_IN_POW10):
      return fold_builtin_exponent (fndecl, arglist, &dconst10);

    CASE_FLT_FN (BUILT_IN_LOG):
      return fold_builtin_logarithm (fndecl, arglist, &dconste);

    CASE_FLT_FN (BUILT_IN_LOG2):
      return fold_builtin_logarithm (fndecl, arglist, &dconst2);

    CASE_FLT_FN (BUILT_IN_LOG10):
      return fold_builtin_logarithm (fndecl, arglist, &dconst10);

    CASE_FLT_FN (BUILT_IN_TAN):
      return fold_builtin_tan (arglist);

    CASE_FLT_FN (BUILT_IN_ATAN):
      return fold_builtin_atan (arglist, type);

    CASE_FLT_FN (BUILT_IN_POW):
      return fold_builtin_pow (fndecl, arglist, type);

    CASE_FLT_FN (BUILT_IN_POWI):
      return fold_builtin_powi (fndecl, arglist, type);

    CASE_FLT_FN (BUILT_IN_INF):
    case BUILT_IN_INFD32:
    case BUILT_IN_INFD64:
    case BUILT_IN_INFD128:
      return fold_builtin_inf (type, true);

    CASE_FLT_FN (BUILT_IN_HUGE_VAL):
      return fold_builtin_inf (type, false);

    CASE_FLT_FN (BUILT_IN_NAN):
    case BUILT_IN_NAND32:
    case BUILT_IN_NAND64:
    case BUILT_IN_NAND128:
      return fold_builtin_nan (arglist, type, true);

    CASE_FLT_FN (BUILT_IN_NANS):
      return fold_builtin_nan (arglist, type, false);

    CASE_FLT_FN (BUILT_IN_FLOOR):
      return fold_builtin_floor (fndecl, arglist);

    CASE_FLT_FN (BUILT_IN_CEIL):
      return fold_builtin_ceil (fndecl, arglist);

    CASE_FLT_FN (BUILT_IN_TRUNC):
      return fold_builtin_trunc (fndecl, arglist);

    CASE_FLT_FN (BUILT_IN_ROUND):
      return fold_builtin_round (fndecl, arglist);

    CASE_FLT_FN (BUILT_IN_NEARBYINT):
    CASE_FLT_FN (BUILT_IN_RINT):
      return fold_trunc_transparent_mathfn (fndecl, arglist);

    CASE_FLT_FN (BUILT_IN_LCEIL):
    CASE_FLT_FN (BUILT_IN_LLCEIL):
    CASE_FLT_FN (BUILT_IN_LFLOOR):
    CASE_FLT_FN (BUILT_IN_LLFLOOR):
    CASE_FLT_FN (BUILT_IN_LROUND):
    CASE_FLT_FN (BUILT_IN_LLROUND):
      return fold_builtin_int_roundingfn (fndecl, arglist);

    CASE_FLT_FN (BUILT_IN_LRINT):
    CASE_FLT_FN (BUILT_IN_LLRINT):
      return fold_fixed_mathfn (fndecl, arglist);

    CASE_INT_FN (BUILT_IN_FFS):
    CASE_INT_FN (BUILT_IN_CLZ):
    CASE_INT_FN (BUILT_IN_CTZ):
    CASE_INT_FN (BUILT_IN_POPCOUNT):
    CASE_INT_FN (BUILT_IN_PARITY):
      return fold_builtin_bitop (fndecl, arglist);

    case BUILT_IN_MEMSET:
      return fold_builtin_memset (arglist, type, ignore);

    case BUILT_IN_MEMCPY:
      return fold_builtin_memory_op (arglist, type, ignore, /*endp=*/0);

    case BUILT_IN_MEMPCPY:
      return fold_builtin_memory_op (arglist, type, ignore, /*endp=*/1);

    case BUILT_IN_MEMMOVE:
      return fold_builtin_memory_op (arglist, type, ignore, /*endp=*/3);

    case BUILT_IN_BZERO:
      return fold_builtin_bzero (arglist, ignore);

    case BUILT_IN_BCOPY:
      return fold_builtin_bcopy (arglist, ignore);

    CASE_FLT_FN (BUILT_IN_SIGNBIT):
      return fold_builtin_signbit (fndecl, arglist);

    case BUILT_IN_ISASCII:
      return fold_builtin_isascii (arglist);

    case BUILT_IN_TOASCII:
      return fold_builtin_toascii (arglist);

    case BUILT_IN_ISDIGIT:
      return fold_builtin_isdigit (arglist);

    CASE_FLT_FN (BUILT_IN_COPYSIGN):
      return fold_builtin_copysign (fndecl, arglist, type);

    CASE_FLT_FN (BUILT_IN_FINITE):
    case BUILT_IN_FINITED32:
    case BUILT_IN_FINITED64:
    case BUILT_IN_FINITED128:
      return fold_builtin_classify (fndecl, arglist, BUILT_IN_FINITE);

    CASE_FLT_FN (BUILT_IN_ISINF):
    case BUILT_IN_ISINFD32:
    case BUILT_IN_ISINFD64:
    case BUILT_IN_ISINFD128:
      return fold_builtin_classify (fndecl, arglist, BUILT_IN_ISINF);

    CASE_FLT_FN (BUILT_IN_ISNAN):
    case BUILT_IN_ISNAND32:
    case BUILT_IN_ISNAND64:
    case BUILT_IN_ISNAND128:
      return fold_builtin_classify (fndecl, arglist, BUILT_IN_ISNAN);

    case BUILT_IN_ISGREATER:
      return fold_builtin_unordered_cmp (fndecl, arglist, UNLE_EXPR, LE_EXPR);
    case BUILT_IN_ISGREATEREQUAL:
      return fold_builtin_unordered_cmp (fndecl, arglist, UNLT_EXPR, LT_EXPR);
    case BUILT_IN_ISLESS:
      return fold_builtin_unordered_cmp (fndecl, arglist, UNGE_EXPR, GE_EXPR);
    case BUILT_IN_ISLESSEQUAL:
      return fold_builtin_unordered_cmp (fndecl, arglist, UNGT_EXPR, GT_EXPR);
    case BUILT_IN_ISLESSGREATER:
      return fold_builtin_unordered_cmp (fndecl, arglist, UNEQ_EXPR, EQ_EXPR);
    case BUILT_IN_ISUNORDERED:
      return fold_builtin_unordered_cmp (fndecl, arglist, UNORDERED_EXPR,
					 NOP_EXPR);

      /* We do the folding for va_start in the expander.  */
    case BUILT_IN_VA_START:
      break;

    case BUILT_IN_OBJECT_SIZE:
      return fold_builtin_object_size (arglist);
    case BUILT_IN_MEMCPY_CHK:
    case BUILT_IN_MEMPCPY_CHK:
    case BUILT_IN_MEMMOVE_CHK:
    case BUILT_IN_MEMSET_CHK:
      return fold_builtin_memory_chk (fndecl, arglist, NULL_TREE, ignore,
				      DECL_FUNCTION_CODE (fndecl));
    case BUILT_IN_STRCPY_CHK:
    case BUILT_IN_STPCPY_CHK:
      return fold_builtin_stxcpy_chk (fndecl, arglist, NULL_TREE, ignore,
				      DECL_FUNCTION_CODE (fndecl));
    case BUILT_IN_STRNCPY_CHK:
      return fold_builtin_strncpy_chk (arglist, NULL_TREE);
    case BUILT_IN_STRCAT_CHK:
      return fold_builtin_strcat_chk (fndecl, arglist);
    case BUILT_IN_STRNCAT_CHK:
      return fold_builtin_strncat_chk (fndecl, arglist);
    case BUILT_IN_SPRINTF_CHK:
    case BUILT_IN_VSPRINTF_CHK:
      return fold_builtin_sprintf_chk (arglist, DECL_FUNCTION_CODE (fndecl));
    case BUILT_IN_SNPRINTF_CHK:
    case BUILT_IN_VSNPRINTF_CHK:
      return fold_builtin_snprintf_chk (arglist, NULL_TREE,
					DECL_FUNCTION_CODE (fndecl));

    case BUILT_IN_PRINTF:
    case BUILT_IN_PRINTF_UNLOCKED:
    case BUILT_IN_VPRINTF:
    case BUILT_IN_PRINTF_CHK:
    case BUILT_IN_VPRINTF_CHK:
      return fold_builtin_printf (fndecl, arglist, ignore,
				  DECL_FUNCTION_CODE (fndecl));

    case BUILT_IN_FPRINTF:
    case BUILT_IN_FPRINTF_UNLOCKED:
    case BUILT_IN_VFPRINTF:
    case BUILT_IN_FPRINTF_CHK:
    case BUILT_IN_VFPRINTF_CHK:
      return fold_builtin_fprintf (fndecl, arglist, ignore,
				   DECL_FUNCTION_CODE (fndecl));

    default:
      break;
    }

  return 0;
}

/* A wrapper function for builtin folding that prevents warnings for
   "statement without effect" and the like, caused by removing the
   call node earlier than the warning is generated.  */

tree
fold_builtin (tree fndecl, tree arglist, bool ignore)
{
  tree exp = fold_builtin_1 (fndecl, arglist, ignore);
  if (exp)
    {
      exp = build1 (NOP_EXPR, TREE_TYPE (exp), exp);
      TREE_NO_WARNING (exp) = 1;
    }

  return exp;
}

/* Conveniently construct a function call expression.  */

tree
build_function_call_expr (tree fn, tree arglist)
{
  tree call_expr;

  call_expr = build1 (ADDR_EXPR, build_pointer_type (TREE_TYPE (fn)), fn);
  return fold_build3 (CALL_EXPR, TREE_TYPE (TREE_TYPE (fn)),
		      call_expr, arglist, NULL_TREE);
}

/* This function validates the types of a function call argument list
   represented as a tree chain of parameters against a specified list
   of tree_codes.  If the last specifier is a 0, that represents an
   ellipses, otherwise the last specifier must be a VOID_TYPE.  */

static int
validate_arglist (tree arglist, ...)
{
  enum tree_code code;
  int res = 0;
  va_list ap;

  va_start (ap, arglist);

  do
    {
      code = va_arg (ap, enum tree_code);
      switch (code)
	{
	case 0:
	  /* This signifies an ellipses, any further arguments are all ok.  */
	  res = 1;
	  goto end;
	case VOID_TYPE:
	  /* This signifies an endlink, if no arguments remain, return
	     true, otherwise return false.  */
	  res = arglist == 0;
	  goto end;
	default:
	  /* If no parameters remain or the parameter's code does not
	     match the specified code, return false.  Otherwise continue
	     checking any remaining arguments.  */
	  if (arglist == 0)
	    goto end;
	  if (code == POINTER_TYPE)
	    {
	      if (! POINTER_TYPE_P (TREE_TYPE (TREE_VALUE (arglist))))
		goto end;
	    }
	  else if (code != TREE_CODE (TREE_TYPE (TREE_VALUE (arglist))))
	    goto end;
	  break;
	}
      arglist = TREE_CHAIN (arglist);
    }
  while (1);

  /* We need gotos here since we can only have one VA_CLOSE in a
     function.  */
 end: ;
  va_end (ap);

  return res;
}

/* Default target-specific builtin expander that does nothing.  */

rtx
default_expand_builtin (tree exp ATTRIBUTE_UNUSED,
			rtx target ATTRIBUTE_UNUSED,
			rtx subtarget ATTRIBUTE_UNUSED,
			enum machine_mode mode ATTRIBUTE_UNUSED,
			int ignore ATTRIBUTE_UNUSED)
{
  return NULL_RTX;
}

/* Returns true is EXP represents data that would potentially reside
   in a readonly section.  */

static bool
readonly_data_expr (tree exp)
{
  STRIP_NOPS (exp);

  if (TREE_CODE (exp) != ADDR_EXPR)
    return false;

  exp = get_base_address (TREE_OPERAND (exp, 0));
  if (!exp)
    return false;

  /* Make sure we call decl_readonly_section only for trees it
     can handle (since it returns true for everything it doesn't
     understand).  */
  if (TREE_CODE (exp) == STRING_CST
      || TREE_CODE (exp) == CONSTRUCTOR
      || (TREE_CODE (exp) == VAR_DECL && TREE_STATIC (exp)))
    return decl_readonly_section (exp, 0);
  else
    return false;
}

/* Simplify a call to the strstr builtin.

   Return 0 if no simplification was possible, otherwise return the
   simplified form of the call as a tree.

   The simplified form may be a constant or other expression which
   computes the same value, but in a more efficient manner (including
   calls to other builtin functions).

   The call may contain arguments which need to be evaluated, but
   which are not useful to determine the result of the call.  In
   this case we return a chain of COMPOUND_EXPRs.  The LHS of each
   COMPOUND_EXPR will be an argument which must be evaluated.
   COMPOUND_EXPRs are chained through their RHS.  The RHS of the last
   COMPOUND_EXPR in the chain will contain the tree for the simplified
   form of the builtin function call.  */

static tree
fold_builtin_strstr (tree arglist, tree type)
{
  if (!validate_arglist (arglist, POINTER_TYPE, POINTER_TYPE, VOID_TYPE))
    return 0;
  else
    {
      tree s1 = TREE_VALUE (arglist), s2 = TREE_VALUE (TREE_CHAIN (arglist));
      tree fn;
      const char *p1, *p2;

      p2 = c_getstr (s2);
      if (p2 == NULL)
	return 0;

      p1 = c_getstr (s1);
      if (p1 != NULL)
	{
	  const char *r = strstr (p1, p2);
	  tree tem;

	  if (r == NULL)
	    return build_int_cst (TREE_TYPE (s1), 0);

	  /* Return an offset into the constant string argument.  */
	  tem = fold_build2 (PLUS_EXPR, TREE_TYPE (s1),
			     s1, build_int_cst (TREE_TYPE (s1), r - p1));
	  return fold_convert (type, tem);
	}

      /* The argument is const char *, and the result is char *, so we need
	 a type conversion here to avoid a warning.  */
      if (p2[0] == '\0')
	return fold_convert (type, s1);

      if (p2[1] != '\0')
	return 0;

      fn = implicit_built_in_decls[BUILT_IN_STRCHR];
      if (!fn)
	return 0;

      /* New argument list transforming strstr(s1, s2) to
	 strchr(s1, s2[0]).  */
      arglist = build_tree_list (NULL_TREE,
				 build_int_cst (NULL_TREE, p2[0]));
      arglist = tree_cons (NULL_TREE, s1, arglist);
      return build_function_call_expr (fn, arglist);
    }
}

/* Simplify a call to the strchr builtin.

   Return 0 if no simplification was possible, otherwise return the
   simplified form of the call as a tree.

   The simplified form may be a constant or other expression which
   computes the same value, but in a more efficient manner (including
   calls to other builtin functions).

   The call may contain arguments which need to be evaluated, but
   which are not useful to determine the result of the call.  In
   this case we return a chain of COMPOUND_EXPRs.  The LHS of each
   COMPOUND_EXPR will be an argument which must be evaluated.
   COMPOUND_EXPRs are chained through their RHS.  The RHS of the last
   COMPOUND_EXPR in the chain will contain the tree for the simplified
   form of the builtin function call.  */

static tree
fold_builtin_strchr (tree arglist, tree type)
{
  if (!validate_arglist (arglist, POINTER_TYPE, INTEGER_TYPE, VOID_TYPE))
    return 0;
  else
    {
      tree s1 = TREE_VALUE (arglist), s2 = TREE_VALUE (TREE_CHAIN (arglist));
      const char *p1;

      if (TREE_CODE (s2) != INTEGER_CST)
	return 0;

      p1 = c_getstr (s1);
      if (p1 != NULL)
	{
	  char c;
	  const char *r;
	  tree tem;

	  if (target_char_cast (s2, &c))
	    return 0;

	  r = strchr (p1, c);

	  if (r == NULL)
	    return build_int_cst (TREE_TYPE (s1), 0);

	  /* Return an offset into the constant string argument.  */
	  tem = fold_build2 (PLUS_EXPR, TREE_TYPE (s1),
			     s1, build_int_cst (TREE_TYPE (s1), r - p1));
	  return fold_convert (type, tem);
	}
      return 0;
    }
}

/* Simplify a call to the strrchr builtin.

   Return 0 if no simplification was possible, otherwise return the
   simplified form of the call as a tree.

   The simplified form may be a constant or other expression which
   computes the same value, but in a more efficient manner (including
   calls to other builtin functions).

   The call may contain arguments which need to be evaluated, but
   which are not useful to determine the result of the call.  In
   this case we return a chain of COMPOUND_EXPRs.  The LHS of each
   COMPOUND_EXPR will be an argument which must be evaluated.
   COMPOUND_EXPRs are chained through their RHS.  The RHS of the last
   COMPOUND_EXPR in the chain will contain the tree for the simplified
   form of the builtin function call.  */

static tree
fold_builtin_strrchr (tree arglist, tree type)
{
  if (!validate_arglist (arglist, POINTER_TYPE, INTEGER_TYPE, VOID_TYPE))
    return 0;
  else
    {
      tree s1 = TREE_VALUE (arglist), s2 = TREE_VALUE (TREE_CHAIN (arglist));
      tree fn;
      const char *p1;

      if (TREE_CODE (s2) != INTEGER_CST)
	return 0;

      p1 = c_getstr (s1);
      if (p1 != NULL)
	{
	  char c;
	  const char *r;
	  tree tem;

	  if (target_char_cast (s2, &c))
	    return 0;

	  r = strrchr (p1, c);

	  if (r == NULL)
	    return build_int_cst (TREE_TYPE (s1), 0);

	  /* Return an offset into the constant string argument.  */
	  tem = fold_build2 (PLUS_EXPR, TREE_TYPE (s1),
			     s1, build_int_cst (TREE_TYPE (s1), r - p1));
	  return fold_convert (type, tem);
	}

      if (! integer_zerop (s2))
	return 0;

      fn = implicit_built_in_decls[BUILT_IN_STRCHR];
      if (!fn)
	return 0;

      /* Transform strrchr(s1, '\0') to strchr(s1, '\0').  */
      return build_function_call_expr (fn, arglist);
    }
}

/* Simplify a call to the strpbrk builtin.

   Return 0 if no simplification was possible, otherwise return the
   simplified form of the call as a tree.

   The simplified form may be a constant or other expression which
   computes the same value, but in a more efficient manner (including
   calls to other builtin functions).

   The call may contain arguments which need to be evaluated, but
   which are not useful to determine the result of the call.  In
   this case we return a chain of COMPOUND_EXPRs.  The LHS of each
   COMPOUND_EXPR will be an argument which must be evaluated.
   COMPOUND_EXPRs are chained through their RHS.  The RHS of the last
   COMPOUND_EXPR in the chain will contain the tree for the simplified
   form of the builtin function call.  */

static tree
fold_builtin_strpbrk (tree arglist, tree type)
{
  if (!validate_arglist (arglist, POINTER_TYPE, POINTER_TYPE, VOID_TYPE))
    return 0;
  else
    {
      tree s1 = TREE_VALUE (arglist), s2 = TREE_VALUE (TREE_CHAIN (arglist));
      tree fn;
      const char *p1, *p2;

      p2 = c_getstr (s2);
      if (p2 == NULL)
	return 0;

      p1 = c_getstr (s1);
      if (p1 != NULL)
	{
	  const char *r = strpbrk (p1, p2);
	  tree tem;

	  if (r == NULL)
	    return build_int_cst (TREE_TYPE (s1), 0);

	  /* Return an offset into the constant string argument.  */
	  tem = fold_build2 (PLUS_EXPR, TREE_TYPE (s1),
			     s1, build_int_cst (TREE_TYPE (s1), r - p1));
	  return fold_convert (type, tem);
	}

      if (p2[0] == '\0')
	/* strpbrk(x, "") == NULL.
	   Evaluate and ignore s1 in case it had side-effects.  */
	return omit_one_operand (TREE_TYPE (s1), integer_zero_node, s1);

      if (p2[1] != '\0')
	return 0;  /* Really call strpbrk.  */

      fn = implicit_built_in_decls[BUILT_IN_STRCHR];
      if (!fn)
	return 0;

      /* New argument list transforming strpbrk(s1, s2) to
	 strchr(s1, s2[0]).  */
      arglist = build_tree_list (NULL_TREE,
				 build_int_cst (NULL_TREE, p2[0]));
      arglist = tree_cons (NULL_TREE, s1, arglist);
      return build_function_call_expr (fn, arglist);
    }
}

/* Simplify a call to the strcat builtin.

   Return 0 if no simplification was possible, otherwise return the
   simplified form of the call as a tree.

   The simplified form may be a constant or other expression which
   computes the same value, but in a more efficient manner (including
   calls to other builtin functions).

   The call may contain arguments which need to be evaluated, but
   which are not useful to determine the result of the call.  In
   this case we return a chain of COMPOUND_EXPRs.  The LHS of each
   COMPOUND_EXPR will be an argument which must be evaluated.
   COMPOUND_EXPRs are chained through their RHS.  The RHS of the last
   COMPOUND_EXPR in the chain will contain the tree for the simplified
   form of the builtin function call.  */

static tree
fold_builtin_strcat (tree arglist)
{
  if (!validate_arglist (arglist, POINTER_TYPE, POINTER_TYPE, VOID_TYPE))
    return 0;
  else
    {
      tree dst = TREE_VALUE (arglist),
	src = TREE_VALUE (TREE_CHAIN (arglist));
      const char *p = c_getstr (src);

      /* If the string length is zero, return the dst parameter.  */
      if (p && *p == '\0')
	return dst;

      return 0;
    }
}

/* Simplify a call to the strncat builtin.

   Return 0 if no simplification was possible, otherwise return the
   simplified form of the call as a tree.

   The simplified form may be a constant or other expression which
   computes the same value, but in a more efficient manner (including
   calls to other builtin functions).

   The call may contain arguments which need to be evaluated, but
   which are not useful to determine the result of the call.  In
   this case we return a chain of COMPOUND_EXPRs.  The LHS of each
   COMPOUND_EXPR will be an argument which must be evaluated.
   COMPOUND_EXPRs are chained through their RHS.  The RHS of the last
   COMPOUND_EXPR in the chain will contain the tree for the simplified
   form of the builtin function call.  */

static tree
fold_builtin_strncat (tree arglist)
{
  if (!validate_arglist (arglist,
			 POINTER_TYPE, POINTER_TYPE, INTEGER_TYPE, VOID_TYPE))
    return 0;
  else
    {
      tree dst = TREE_VALUE (arglist);
      tree src = TREE_VALUE (TREE_CHAIN (arglist));
      tree len = TREE_VALUE (TREE_CHAIN (TREE_CHAIN (arglist)));
      const char *p = c_getstr (src);

      /* If the requested length is zero, or the src parameter string
	 length is zero, return the dst parameter.  */
      if (integer_zerop (len) || (p && *p == '\0'))
	return omit_two_operands (TREE_TYPE (dst), dst, src, len);

      /* If the requested len is greater than or equal to the string
	 length, call strcat.  */
      if (TREE_CODE (len) == INTEGER_CST && p
	  && compare_tree_int (len, strlen (p)) >= 0)
	{
	  tree newarglist
	    = tree_cons (NULL_TREE, dst, build_tree_list (NULL_TREE, src));
	  tree fn = implicit_built_in_decls[BUILT_IN_STRCAT];

	  /* If the replacement _DECL isn't initialized, don't do the
	     transformation.  */
#ifndef NO_UNSAFE_BUILTINS
	  if (!fn)
#endif
	    return 0;

	  return build_function_call_expr (fn, newarglist);
	}
      return 0;
    }
}

/* Simplify a call to the strspn builtin.

   Return 0 if no simplification was possible, otherwise return the
   simplified form of the call as a tree.

   The simplified form may be a constant or other expression which
   computes the same value, but in a more efficient manner (including
   calls to other builtin functions).

   The call may contain arguments which need to be evaluated, but
   which are not useful to determine the result of the call.  In
   this case we return a chain of COMPOUND_EXPRs.  The LHS of each
   COMPOUND_EXPR will be an argument which must be evaluated.
   COMPOUND_EXPRs are chained through their RHS.  The RHS of the last
   COMPOUND_EXPR in the chain will contain the tree for the simplified
   form of the builtin function call.  */

static tree
fold_builtin_strspn (tree arglist)
{
  if (!validate_arglist (arglist, POINTER_TYPE, POINTER_TYPE, VOID_TYPE))
    return 0;
  else
    {
      tree s1 = TREE_VALUE (arglist), s2 = TREE_VALUE (TREE_CHAIN (arglist));
      const char *p1 = c_getstr (s1), *p2 = c_getstr (s2);

      /* If both arguments are constants, evaluate at compile-time.  */
      if (p1 && p2)
	{
	  const size_t r = strspn (p1, p2);
	  return size_int (r);
	}

      /* If either argument is "", return 0.  */
      if ((p1 && *p1 == '\0') || (p2 && *p2 == '\0'))
	/* Evaluate and ignore both arguments in case either one has
	   side-effects.  */
	return omit_two_operands (integer_type_node, integer_zero_node,
				  s1, s2);
      return 0;
    }
}

/* Simplify a call to the strcspn builtin.

   Return 0 if no simplification was possible, otherwise return the
   simplified form of the call as a tree.

   The simplified form may be a constant or other expression which
   computes the same value, but in a more efficient manner (including
   calls to other builtin functions).

   The call may contain arguments which need to be evaluated, but
   which are not useful to determine the result of the call.  In
   this case we return a chain of COMPOUND_EXPRs.  The LHS of each
   COMPOUND_EXPR will be an argument which must be evaluated.
   COMPOUND_EXPRs are chained through their RHS.  The RHS of the last
   COMPOUND_EXPR in the chain will contain the tree for the simplified
   form of the builtin function call.  */

static tree
fold_builtin_strcspn (tree arglist)
{
  if (!validate_arglist (arglist, POINTER_TYPE, POINTER_TYPE, VOID_TYPE))
    return 0;
  else
    {
      tree s1 = TREE_VALUE (arglist), s2 = TREE_VALUE (TREE_CHAIN (arglist));
      const char *p1 = c_getstr (s1), *p2 = c_getstr (s2);

      /* If both arguments are constants, evaluate at compile-time.  */
      if (p1 && p2)
	{
	  const size_t r = strcspn (p1, p2);
	  return size_int (r);
	}

      /* If the first argument is "", return 0.  */
      if (p1 && *p1 == '\0')
	{
	  /* Evaluate and ignore argument s2 in case it has
	     side-effects.  */
	  return omit_one_operand (integer_type_node,
				   integer_zero_node, s2);
	}

      /* If the second argument is "", return __builtin_strlen(s1).  */
      if (p2 && *p2 == '\0')
	{
	  tree newarglist = build_tree_list (NULL_TREE, s1),
	    fn = implicit_built_in_decls[BUILT_IN_STRLEN];

	  /* If the replacement _DECL isn't initialized, don't do the
	     transformation.  */
	  if (!fn)
	    return 0;

	  return build_function_call_expr (fn, newarglist);
	}
      return 0;
    }
}

/* Fold a call to the fputs builtin.  IGNORE is true if the value returned
   by the builtin will be ignored.  UNLOCKED is true is true if this
   actually a call to fputs_unlocked.  If LEN in non-NULL, it represents
   the known length of the string.  Return NULL_TREE if no simplification
   was possible.  */

tree
fold_builtin_fputs (tree arglist, bool ignore, bool unlocked, tree len)
{
  tree fn;
  /* If we're using an unlocked function, assume the other unlocked
     functions exist explicitly.  */
  tree const fn_fputc = unlocked ? built_in_decls[BUILT_IN_FPUTC_UNLOCKED]
    : implicit_built_in_decls[BUILT_IN_FPUTC];
  tree const fn_fwrite = unlocked ? built_in_decls[BUILT_IN_FWRITE_UNLOCKED]
    : implicit_built_in_decls[BUILT_IN_FWRITE];

  /* If the return value is used, don't do the transformation.  */
  if (!ignore)
    return 0;

  /* Verify the arguments in the original call.  */
  if (!validate_arglist (arglist, POINTER_TYPE, POINTER_TYPE, VOID_TYPE))
    return 0;

  if (! len)
    len = c_strlen (TREE_VALUE (arglist), 0);

  /* Get the length of the string passed to fputs.  If the length
     can't be determined, punt.  */
  if (!len
      || TREE_CODE (len) != INTEGER_CST)
    return 0;

  switch (compare_tree_int (len, 1))
    {
    case -1: /* length is 0, delete the call entirely .  */
      return omit_one_operand (integer_type_node, integer_zero_node,
			       TREE_VALUE (TREE_CHAIN (arglist)));

    case 0: /* length is 1, call fputc.  */
      {
	const char *p = c_getstr (TREE_VALUE (arglist));

	if (p != NULL)
	  {
	    /* New argument list transforming fputs(string, stream) to
	       fputc(string[0], stream).  */
	    arglist = build_tree_list (NULL_TREE,
				       TREE_VALUE (TREE_CHAIN (arglist)));
	    arglist = tree_cons (NULL_TREE,
				 build_int_cst (NULL_TREE, p[0]),
				 arglist);
	    fn = fn_fputc;
	    break;
	  }
      }
      /* FALLTHROUGH */
    case 1: /* length is greater than 1, call fwrite.  */
      {
	tree string_arg;

	/* If optimizing for size keep fputs.  */
	if (optimize_size)
	  return 0;
	string_arg = TREE_VALUE (arglist);
	/* New argument list transforming fputs(string, stream) to
	   fwrite(string, 1, len, stream).  */
	arglist = build_tree_list (NULL_TREE,
				   TREE_VALUE (TREE_CHAIN (arglist)));
	arglist = tree_cons (NULL_TREE, len, arglist);
	arglist = tree_cons (NULL_TREE, size_one_node, arglist);
	arglist = tree_cons (NULL_TREE, string_arg, arglist);
	fn = fn_fwrite;
	break;
      }
    default:
      gcc_unreachable ();
    }

  /* If the replacement _DECL isn't initialized, don't do the
     transformation.  */
  if (!fn)
    return 0;

  /* These optimizations are only performed when the result is ignored,
     hence there's no need to cast the result to integer_type_node.  */
  return build_function_call_expr (fn, arglist);
}

/* Fold the new_arg's arguments (ARGLIST). Returns true if there was an error
   produced.  False otherwise.  This is done so that we don't output the error
   or warning twice or three times.  */
bool
fold_builtin_next_arg (tree arglist)
{
  tree fntype = TREE_TYPE (current_function_decl);

  if (TYPE_ARG_TYPES (fntype) == 0
      || (TREE_VALUE (tree_last (TYPE_ARG_TYPES (fntype)))
	  == void_type_node))
    {
      error ("%<va_start%> used in function with fixed args");
      return true;
    }
  else if (!arglist)
    {
      /* Evidently an out of date version of <stdarg.h>; can't validate
	 va_start's second argument, but can still work as intended.  */
      warning (0, "%<__builtin_next_arg%> called without an argument");
      return true;
    }
  /* We use __builtin_va_start (ap, 0, 0) or __builtin_next_arg (0, 0)
     when we checked the arguments and if needed issued a warning.  */
  else if (!TREE_CHAIN (arglist)
	   || !integer_zerop (TREE_VALUE (arglist))
	   || !integer_zerop (TREE_VALUE (TREE_CHAIN (arglist)))
	   || TREE_CHAIN (TREE_CHAIN (arglist)))
    {
      tree last_parm = tree_last (DECL_ARGUMENTS (current_function_decl));
      tree arg = TREE_VALUE (arglist);

      if (TREE_CHAIN (arglist))
	{
	  error ("%<va_start%> used with too many arguments");
	  return true;
	}

      /* Strip off all nops for the sake of the comparison.  This
	 is not quite the same as STRIP_NOPS.  It does more.
	 We must also strip off INDIRECT_EXPR for C++ reference
	 parameters.  */
      while (TREE_CODE (arg) == NOP_EXPR
	     || TREE_CODE (arg) == CONVERT_EXPR
	     || TREE_CODE (arg) == NON_LVALUE_EXPR
	     || TREE_CODE (arg) == INDIRECT_REF)
	arg = TREE_OPERAND (arg, 0);
      if (arg != last_parm)
	{
	  /* FIXME: Sometimes with the tree optimizers we can get the
	     not the last argument even though the user used the last
	     argument.  We just warn and set the arg to be the last
	     argument so that we will get wrong-code because of
	     it.  */
	  warning (0, "second parameter of %<va_start%> not last named argument");
	}
      /* We want to verify the second parameter just once before the tree
	 optimizers are run and then avoid keeping it in the tree,
	 as otherwise we could warn even for correct code like:
	 void foo (int i, ...)
	 { va_list ap; i++; va_start (ap, i); va_end (ap); }  */
      TREE_VALUE (arglist) = integer_zero_node;
      TREE_CHAIN (arglist) = build_tree_list (NULL, integer_zero_node);
    }
  return false;
}


/* Simplify a call to the sprintf builtin.

   Return 0 if no simplification was possible, otherwise return the
   simplified form of the call as a tree.  If IGNORED is true, it means that
   the caller does not use the returned value of the function.  */

static tree
fold_builtin_sprintf (tree arglist, int ignored)
{
  tree call, retval, dest, fmt;
  const char *fmt_str = NULL;

  /* Verify the required arguments in the original call.  We deal with two
     types of sprintf() calls: 'sprintf (str, fmt)' and
     'sprintf (dest, "%s", orig)'.  */
  if (!validate_arglist (arglist, POINTER_TYPE, POINTER_TYPE, VOID_TYPE)
      && !validate_arglist (arglist, POINTER_TYPE, POINTER_TYPE, POINTER_TYPE,
			    VOID_TYPE))
    return NULL_TREE;

  /* Get the destination string and the format specifier.  */
  dest = TREE_VALUE (arglist);
  fmt = TREE_VALUE (TREE_CHAIN (arglist));
  arglist = TREE_CHAIN (TREE_CHAIN (arglist));

  /* Check whether the format is a literal string constant.  */
  fmt_str = c_getstr (fmt);
  if (fmt_str == NULL)
    return NULL_TREE;

  call = NULL_TREE;
  retval = NULL_TREE;

  if (!init_target_chars())
    return 0;

  /* If the format doesn't contain % args or %%, use strcpy.  */
  if (strchr (fmt_str, target_percent) == NULL)
    {
      tree fn = implicit_built_in_decls[BUILT_IN_STRCPY];

#ifndef NO_UNSAFE_BUILTINS
      if (!fn)
#endif
	return NULL_TREE;

      /* Don't optimize sprintf (buf, "abc", ptr++).  */
      if (arglist)
	return NULL_TREE;

      /* Convert sprintf (str, fmt) into strcpy (str, fmt) when
	 'format' is known to contain no % formats.  */
      arglist = build_tree_list (NULL_TREE, fmt);
      arglist = tree_cons (NULL_TREE, dest, arglist);
      call = build_function_call_expr (fn, arglist);
      if (!ignored)
	retval = build_int_cst (NULL_TREE, strlen (fmt_str));
    }

  /* If the format is "%s", use strcpy if the result isn't used.  */
  else if (fmt_str && strcmp (fmt_str, target_percent_s) == 0)
    {
      tree fn, orig;
      fn = implicit_built_in_decls[BUILT_IN_STRCPY];

#ifndef NO_UNSAFE_BUILTINS
      if (!fn)
#endif
	return NULL_TREE;

      /* Don't crash on sprintf (str1, "%s").  */
      if (!arglist)
	return NULL_TREE;

      /* Convert sprintf (str1, "%s", str2) into strcpy (str1, str2).  */
      orig = TREE_VALUE (arglist);
      arglist = build_tree_list (NULL_TREE, orig);
      arglist = tree_cons (NULL_TREE, dest, arglist);
      if (!ignored)
	{
	  retval = c_strlen (orig, 1);
	  if (!retval || TREE_CODE (retval) != INTEGER_CST)
	    return NULL_TREE;
	}
      call = build_function_call_expr (fn, arglist);
    }

  if (call && retval)
    {
      retval = fold_convert
	(TREE_TYPE (TREE_TYPE (implicit_built_in_decls[BUILT_IN_SPRINTF])),
	 retval);
      return build2 (COMPOUND_EXPR, TREE_TYPE (retval), call, retval);
    }
  else
    return call;
}

/* Expand a call to __builtin_object_size.  */

rtx
expand_builtin_object_size (tree exp)
{
  tree ost;
  int object_size_type;
  tree fndecl = get_callee_fndecl (exp);
  tree arglist = TREE_OPERAND (exp, 1);
  location_t locus = EXPR_LOCATION (exp);

  if (!validate_arglist (arglist, POINTER_TYPE, INTEGER_TYPE, VOID_TYPE))
    {
      error ("%Hfirst argument of %D must be a pointer, second integer constant",
	     &locus, fndecl);
      expand_builtin_trap ();
      return const0_rtx;
    }

  ost = TREE_VALUE (TREE_CHAIN (arglist));
  STRIP_NOPS (ost);

  if (TREE_CODE (ost) != INTEGER_CST
      || tree_int_cst_sgn (ost) < 0
      || compare_tree_int (ost, 3) > 0)
    {
      error ("%Hlast argument of %D is not integer constant between 0 and 3",
	     &locus, fndecl);
      expand_builtin_trap ();
      return const0_rtx;
    }

  object_size_type = tree_low_cst (ost, 0);

  return object_size_type < 2 ? constm1_rtx : const0_rtx;
}

/* Expand EXP, a call to the __mem{cpy,pcpy,move,set}_chk builtin.
   FCODE is the BUILT_IN_* to use.
   Return 0 if we failed; the caller should emit a normal call,
   otherwise try to get the result in TARGET, if convenient (and in
   mode MODE if that's convenient).  */

static rtx
expand_builtin_memory_chk (tree exp, rtx target, enum machine_mode mode,
			   enum built_in_function fcode)
{
  tree arglist = TREE_OPERAND (exp, 1);
  tree dest, src, len, size;

  if (!validate_arglist (arglist,
			 POINTER_TYPE,
			 fcode == BUILT_IN_MEMSET_CHK
			 ? INTEGER_TYPE : POINTER_TYPE,
			 INTEGER_TYPE, INTEGER_TYPE, VOID_TYPE))
    return 0;

  dest = TREE_VALUE (arglist);
  src = TREE_VALUE (TREE_CHAIN (arglist));
  len = TREE_VALUE (TREE_CHAIN (TREE_CHAIN (arglist)));
  size = TREE_VALUE (TREE_CHAIN (TREE_CHAIN (TREE_CHAIN (arglist))));

  if (! host_integerp (size, 1))
    return 0;

  if (host_integerp (len, 1) || integer_all_onesp (size))
    {
      tree fn;

      if (! integer_all_onesp (size) && tree_int_cst_lt (size, len))
	{
	  location_t locus = EXPR_LOCATION (exp);
	  warning (0, "%Hcall to %D will always overflow destination buffer",
		   &locus, get_callee_fndecl (exp));
	  return 0;
	}

      arglist = build_tree_list (NULL_TREE, len);
      arglist = tree_cons (NULL_TREE, src, arglist);
      arglist = tree_cons (NULL_TREE, dest, arglist);

      fn = NULL_TREE;
      /* If __builtin_mem{cpy,pcpy,move,set}_chk is used, assume
	 mem{cpy,pcpy,move,set} is available.  */
      switch (fcode)
	{
	case BUILT_IN_MEMCPY_CHK:
	  fn = built_in_decls[BUILT_IN_MEMCPY];
	  break;
	case BUILT_IN_MEMPCPY_CHK:
	  fn = built_in_decls[BUILT_IN_MEMPCPY];
	  break;
	case BUILT_IN_MEMMOVE_CHK:
	  fn = built_in_decls[BUILT_IN_MEMMOVE];
	  break;
	case BUILT_IN_MEMSET_CHK:
	  fn = built_in_decls[BUILT_IN_MEMSET];
	  break;
	default:
	  break;
	}

      if (! fn)
	return 0;

      fn = build_function_call_expr (fn, arglist);
      if (TREE_CODE (fn) == CALL_EXPR)
	CALL_EXPR_TAILCALL (fn) = CALL_EXPR_TAILCALL (exp);
      return expand_expr (fn, target, mode, EXPAND_NORMAL);
    }
  else if (fcode == BUILT_IN_MEMSET_CHK)
    return 0;
  else
    {
      unsigned int dest_align
	= get_pointer_alignment (dest, BIGGEST_ALIGNMENT);

      /* If DEST is not a pointer type, call the normal function.  */
      if (dest_align == 0)
	return 0;

      /* If SRC and DEST are the same (and not volatile), do nothing.  */
      if (operand_equal_p (src, dest, 0))
	{
	  tree expr;

	  if (fcode != BUILT_IN_MEMPCPY_CHK)
	    {
	      /* Evaluate and ignore LEN in case it has side-effects.  */
	      expand_expr (len, const0_rtx, VOIDmode, EXPAND_NORMAL);
	      return expand_expr (dest, target, mode, EXPAND_NORMAL);
	    }

	  len = fold_convert (TREE_TYPE (dest), len);
	  expr = fold_build2 (PLUS_EXPR, TREE_TYPE (dest), dest, len);
	  return expand_expr (expr, target, mode, EXPAND_NORMAL);
	}

      /* __memmove_chk special case.  */
      if (fcode == BUILT_IN_MEMMOVE_CHK)
	{
	  unsigned int src_align
	    = get_pointer_alignment (src, BIGGEST_ALIGNMENT);

	  if (src_align == 0)
	    return 0;

	  /* If src is categorized for a readonly section we can use
	     normal __memcpy_chk.  */
	  if (readonly_data_expr (src))
	    {
	      tree fn = built_in_decls[BUILT_IN_MEMCPY_CHK];
	      if (!fn)
		return 0;
	      fn = build_function_call_expr (fn, arglist);
	      if (TREE_CODE (fn) == CALL_EXPR)
		CALL_EXPR_TAILCALL (fn) = CALL_EXPR_TAILCALL (exp);
	      return expand_expr (fn, target, mode, EXPAND_NORMAL);
	    }
	}
      return 0;
    }
}

/* Emit warning if a buffer overflow is detected at compile time.  */

static void
maybe_emit_chk_warning (tree exp, enum built_in_function fcode)
{
  int arg_mask, is_strlen = 0;
  tree arglist = TREE_OPERAND (exp, 1), a;
  tree len, size;
  location_t locus;

  switch (fcode)
    {
    case BUILT_IN_STRCPY_CHK:
    case BUILT_IN_STPCPY_CHK:
    /* For __strcat_chk the warning will be emitted only if overflowing
       by at least strlen (dest) + 1 bytes.  */
    case BUILT_IN_STRCAT_CHK:
      arg_mask = 6;
      is_strlen = 1;
      break;
    case BUILT_IN_STRNCPY_CHK:
      arg_mask = 12;
      break;
    case BUILT_IN_SNPRINTF_CHK:
    case BUILT_IN_VSNPRINTF_CHK:
      arg_mask = 10;
      break;
    default:
      gcc_unreachable ();
    }

  len = NULL_TREE;
  size = NULL_TREE;
  for (a = arglist; a && arg_mask; a = TREE_CHAIN (a), arg_mask >>= 1)
    if (arg_mask & 1)
      {
	if (len)
	  size = a;
	else
	  len = a;
      }

  if (!len || !size)
    return;

  len = TREE_VALUE (len);
  size = TREE_VALUE (size);

  if (! host_integerp (size, 1) || integer_all_onesp (size))
    return;

  if (is_strlen)
    {
      len = c_strlen (len, 1);
      if (! len || ! host_integerp (len, 1) || tree_int_cst_lt (len, size))
	return;
    }
  else if (! host_integerp (len, 1) || ! tree_int_cst_lt (size, len))
    return;

  locus = EXPR_LOCATION (exp);
  warning (0, "%Hcall to %D will always overflow destination buffer",
	   &locus, get_callee_fndecl (exp));
}

/* Emit warning if a buffer overflow is detected at compile time
   in __sprintf_chk/__vsprintf_chk calls.  */

static void
maybe_emit_sprintf_chk_warning (tree exp, enum built_in_function fcode)
{
  tree arglist = TREE_OPERAND (exp, 1);
  tree dest, size, len, fmt, flag;
  const char *fmt_str;

  /* Verify the required arguments in the original call.  */
  if (! arglist)
    return;
  dest = TREE_VALUE (arglist);
  arglist = TREE_CHAIN (arglist);
  if (! arglist)
    return;
  flag = TREE_VALUE (arglist);
  arglist = TREE_CHAIN (arglist);
  if (! arglist)
    return;
  size = TREE_VALUE (arglist);
  arglist = TREE_CHAIN (arglist);
  if (! arglist)
    return;
  fmt = TREE_VALUE (arglist);
  arglist = TREE_CHAIN (arglist);

  if (! host_integerp (size, 1) || integer_all_onesp (size))
    return;

  /* Check whether the format is a literal string constant.  */
  fmt_str = c_getstr (fmt);
  if (fmt_str == NULL)
    return;

  if (!init_target_chars())
    return;

  /* If the format doesn't contain % args or %%, we know its size.  */
  if (strchr (fmt_str, target_percent) == 0)
    len = build_int_cstu (size_type_node, strlen (fmt_str));
  /* If the format is "%s" and first ... argument is a string literal,
     we know it too.  */
  else if (fcode == BUILT_IN_SPRINTF_CHK && strcmp (fmt_str, target_percent_s) == 0)
    {
      tree arg;

      if (! arglist)
	return;
      arg = TREE_VALUE (arglist);
      if (! POINTER_TYPE_P (TREE_TYPE (arg)))
	return;

      len = c_strlen (arg, 1);
      if (!len || ! host_integerp (len, 1))
	return;
    }
  else
    return;

  if (! tree_int_cst_lt (len, size))
    {
      location_t locus = EXPR_LOCATION (exp);
      warning (0, "%Hcall to %D will always overflow destination buffer",
	       &locus, get_callee_fndecl (exp));
    }
}

/* Fold a call to __builtin_object_size, if possible.  */

tree
fold_builtin_object_size (tree arglist)
{
  tree ptr, ost, ret = 0;
  int object_size_type;

  if (!validate_arglist (arglist, POINTER_TYPE, INTEGER_TYPE, VOID_TYPE))
    return 0;

  ptr = TREE_VALUE (arglist);
  ost = TREE_VALUE (TREE_CHAIN (arglist));
  STRIP_NOPS (ost);

  if (TREE_CODE (ost) != INTEGER_CST
      || tree_int_cst_sgn (ost) < 0
      || compare_tree_int (ost, 3) > 0)
    return 0;

  object_size_type = tree_low_cst (ost, 0);

  /* __builtin_object_size doesn't evaluate side-effects in its arguments;
     if there are any side-effects, it returns (size_t) -1 for types 0 and 1
     and (size_t) 0 for types 2 and 3.  */
  if (TREE_SIDE_EFFECTS (ptr))
    return fold_convert (size_type_node,
			 object_size_type < 2
			 ? integer_minus_one_node : integer_zero_node);

  if (TREE_CODE (ptr) == ADDR_EXPR)
    ret = build_int_cstu (size_type_node,
			compute_builtin_object_size (ptr, object_size_type));

  else if (TREE_CODE (ptr) == SSA_NAME)
    {
      unsigned HOST_WIDE_INT bytes;

      /* If object size is not known yet, delay folding until
       later.  Maybe subsequent passes will help determining
       it.  */
      bytes = compute_builtin_object_size (ptr, object_size_type);
      if (bytes != (unsigned HOST_WIDE_INT) (object_size_type < 2
					     ? -1 : 0))
	ret = build_int_cstu (size_type_node, bytes);
    }

  if (ret)
    {
      ret = force_fit_type (ret, -1, false, false);
      if (TREE_CONSTANT_OVERFLOW (ret))
	ret = 0;
    }

  return ret;
}

/* Fold a call to the __mem{cpy,pcpy,move,set}_chk builtin.
   IGNORE is true, if return value can be ignored.  FCODE is the BUILT_IN_*
   code of the builtin.  If MAXLEN is not NULL, it is maximum length
   passed as third argument.  */

tree
fold_builtin_memory_chk (tree fndecl, tree arglist, tree maxlen, bool ignore,
			 enum built_in_function fcode)
{
  tree dest, src, len, size, fn;

  if (!validate_arglist (arglist,
			 POINTER_TYPE,
			 fcode == BUILT_IN_MEMSET_CHK
			 ? INTEGER_TYPE : POINTER_TYPE,
			 INTEGER_TYPE, INTEGER_TYPE, VOID_TYPE))
    return 0;

  dest = TREE_VALUE (arglist);
  /* Actually val for __memset_chk, but it doesn't matter.  */
  src = TREE_VALUE (TREE_CHAIN (arglist));
  len = TREE_VALUE (TREE_CHAIN (TREE_CHAIN (arglist)));
  size = TREE_VALUE (TREE_CHAIN (TREE_CHAIN (TREE_CHAIN (arglist))));

  /* If SRC and DEST are the same (and not volatile), return DEST
     (resp. DEST+LEN for __mempcpy_chk).  */
  if (fcode != BUILT_IN_MEMSET_CHK && operand_equal_p (src, dest, 0))
    {
      if (fcode != BUILT_IN_MEMPCPY_CHK)
	return omit_one_operand (TREE_TYPE (TREE_TYPE (fndecl)), dest, len);
      else
	{
	  tree temp = fold_convert (TREE_TYPE (dest), len);
	  temp = fold_build2 (PLUS_EXPR, TREE_TYPE (dest), dest, temp);
	  return fold_convert (TREE_TYPE (TREE_TYPE (fndecl)), temp);
	}
    }

  if (! host_integerp (size, 1))
    return 0;

  if (! integer_all_onesp (size))
    {
      if (! host_integerp (len, 1))
	{
	  /* If LEN is not constant, try MAXLEN too.
	     For MAXLEN only allow optimizing into non-_ocs function
	     if SIZE is >= MAXLEN, never convert to __ocs_fail ().  */
	  if (maxlen == NULL_TREE || ! host_integerp (maxlen, 1))
	    {
	      if (fcode == BUILT_IN_MEMPCPY_CHK && ignore)
		{
		  /* (void) __mempcpy_chk () can be optimized into
		     (void) __memcpy_chk ().  */
		  fn = built_in_decls[BUILT_IN_MEMCPY_CHK];
		  if (!fn)
		    return 0;

		  return build_function_call_expr (fn, arglist);
		}
	      return 0;
	    }
	}
      else
	maxlen = len;

      if (tree_int_cst_lt (size, maxlen))
	return 0;
    }

  arglist = build_tree_list (NULL_TREE, len);
  arglist = tree_cons (NULL_TREE, src, arglist);
  arglist = tree_cons (NULL_TREE, dest, arglist);

  fn = NULL_TREE;
  /* If __builtin_mem{cpy,pcpy,move,set}_chk is used, assume
     mem{cpy,pcpy,move,set} is available.  */
  switch (fcode)
    {
    case BUILT_IN_MEMCPY_CHK:
      fn = built_in_decls[BUILT_IN_MEMCPY];
      break;
    case BUILT_IN_MEMPCPY_CHK:
      fn = built_in_decls[BUILT_IN_MEMPCPY];
      break;
    case BUILT_IN_MEMMOVE_CHK:
      fn = built_in_decls[BUILT_IN_MEMMOVE];
      break;
    case BUILT_IN_MEMSET_CHK:
      fn = built_in_decls[BUILT_IN_MEMSET];
      break;
    default:
      break;
    }

  if (!fn)
    return 0;

  return build_function_call_expr (fn, arglist);
}

/* Fold a call to the __st[rp]cpy_chk builtin.
   IGNORE is true, if return value can be ignored.  FCODE is the BUILT_IN_*
   code of the builtin.  If MAXLEN is not NULL, it is maximum length of
   strings passed as second argument.  */

tree
fold_builtin_stxcpy_chk (tree fndecl, tree arglist, tree maxlen, bool ignore,
			 enum built_in_function fcode)
{
  tree dest, src, size, len, fn;

  if (!validate_arglist (arglist, POINTER_TYPE, POINTER_TYPE, INTEGER_TYPE,
			 VOID_TYPE))
    return 0;

  dest = TREE_VALUE (arglist);
  src = TREE_VALUE (TREE_CHAIN (arglist));
  size = TREE_VALUE (TREE_CHAIN (TREE_CHAIN (arglist)));

  /* If SRC and DEST are the same (and not volatile), return DEST.  */
  if (fcode == BUILT_IN_STRCPY_CHK && operand_equal_p (src, dest, 0))
    return fold_convert (TREE_TYPE (TREE_TYPE (fndecl)), dest);

  if (! host_integerp (size, 1))
    return 0;

  if (! integer_all_onesp (size))
    {
      len = c_strlen (src, 1);
      if (! len || ! host_integerp (len, 1))
	{
	  /* If LEN is not constant, try MAXLEN too.
	     For MAXLEN only allow optimizing into non-_ocs function
	     if SIZE is >= MAXLEN, never convert to __ocs_fail ().  */
	  if (maxlen == NULL_TREE || ! host_integerp (maxlen, 1))
	    {
	      if (fcode == BUILT_IN_STPCPY_CHK)
		{
		  if (! ignore)
		    return 0;

		  /* If return value of __stpcpy_chk is ignored,
		     optimize into __strcpy_chk.  */
		  fn = built_in_decls[BUILT_IN_STRCPY_CHK];
		  if (!fn)
		    return 0;

		  return build_function_call_expr (fn, arglist);
		}

	      if (! len || TREE_SIDE_EFFECTS (len))
		return 0;

	      /* If c_strlen returned something, but not a constant,
		 transform __strcpy_chk into __memcpy_chk.  */
	      fn = built_in_decls[BUILT_IN_MEMCPY_CHK];
	      if (!fn)
		return 0;

	      len = size_binop (PLUS_EXPR, len, ssize_int (1));
	      arglist = build_tree_list (NULL_TREE, size);
	      arglist = tree_cons (NULL_TREE, len, arglist);
	      arglist = tree_cons (NULL_TREE, src, arglist);
	      arglist = tree_cons (NULL_TREE, dest, arglist);
	      return fold_convert (TREE_TYPE (TREE_TYPE (fndecl)),
				   build_function_call_expr (fn, arglist));
	    }
	}
      else
	maxlen = len;

      if (! tree_int_cst_lt (maxlen, size))
	return 0;
    }

  arglist = build_tree_list (NULL_TREE, src);
  arglist = tree_cons (NULL_TREE, dest, arglist);

  /* If __builtin_st{r,p}cpy_chk is used, assume st{r,p}cpy is available.  */
  fn = built_in_decls[fcode == BUILT_IN_STPCPY_CHK
		      ? BUILT_IN_STPCPY : BUILT_IN_STRCPY];
  if (!fn)
    return 0;

  return build_function_call_expr (fn, arglist);
}

/* Fold a call to the __strncpy_chk builtin.
   If MAXLEN is not NULL, it is maximum length passed as third argument.  */

tree
fold_builtin_strncpy_chk (tree arglist, tree maxlen)
{
  tree dest, src, size, len, fn;

  if (!validate_arglist (arglist, POINTER_TYPE, POINTER_TYPE, INTEGER_TYPE,
			 INTEGER_TYPE, VOID_TYPE))
    return 0;

  dest = TREE_VALUE (arglist);
  src = TREE_VALUE (TREE_CHAIN (arglist));
  len = TREE_VALUE (TREE_CHAIN (TREE_CHAIN (arglist)));
  size = TREE_VALUE (TREE_CHAIN (TREE_CHAIN (TREE_CHAIN (arglist))));

  if (! host_integerp (size, 1))
    return 0;

  if (! integer_all_onesp (size))
    {
      if (! host_integerp (len, 1))
	{
	  /* If LEN is not constant, try MAXLEN too.
	     For MAXLEN only allow optimizing into non-_ocs function
	     if SIZE is >= MAXLEN, never convert to __ocs_fail ().  */
	  if (maxlen == NULL_TREE || ! host_integerp (maxlen, 1))
	    return 0;
	}
      else
	maxlen = len;

      if (tree_int_cst_lt (size, maxlen))
	return 0;
    }

  arglist = build_tree_list (NULL_TREE, len);
  arglist = tree_cons (NULL_TREE, src, arglist);
  arglist = tree_cons (NULL_TREE, dest, arglist);

  /* If __builtin_strncpy_chk is used, assume strncpy is available.  */
  fn = built_in_decls[BUILT_IN_STRNCPY];
  if (!fn)
    return 0;

  return build_function_call_expr (fn, arglist);
}

/* Fold a call to the __strcat_chk builtin FNDECL with ARGLIST.  */

static tree
fold_builtin_strcat_chk (tree fndecl, tree arglist)
{
  tree dest, src, size, fn;
  const char *p;

  if (!validate_arglist (arglist, POINTER_TYPE, POINTER_TYPE, INTEGER_TYPE,
			 VOID_TYPE))
    return 0;

  dest = TREE_VALUE (arglist);
  src = TREE_VALUE (TREE_CHAIN (arglist));
  size = TREE_VALUE (TREE_CHAIN (TREE_CHAIN (arglist)));

  p = c_getstr (src);
  /* If the SRC parameter is "", return DEST.  */
  if (p && *p == '\0')
    return omit_one_operand (TREE_TYPE (TREE_TYPE (fndecl)), dest, src);

  if (! host_integerp (size, 1) || ! integer_all_onesp (size))
    return 0;

  arglist = build_tree_list (NULL_TREE, src);
  arglist = tree_cons (NULL_TREE, dest, arglist);

  /* If __builtin_strcat_chk is used, assume strcat is available.  */
  fn = built_in_decls[BUILT_IN_STRCAT];
  if (!fn)
    return 0;

  return build_function_call_expr (fn, arglist);
}

/* Fold a call to the __strncat_chk builtin EXP.  */

static tree
fold_builtin_strncat_chk (tree fndecl, tree arglist)
{
  tree dest, src, size, len, fn;
  const char *p;

  if (!validate_arglist (arglist, POINTER_TYPE, POINTER_TYPE, INTEGER_TYPE,
			 INTEGER_TYPE, VOID_TYPE))
    return 0;

  dest = TREE_VALUE (arglist);
  src = TREE_VALUE (TREE_CHAIN (arglist));
  len = TREE_VALUE (TREE_CHAIN (TREE_CHAIN (arglist)));
  size = TREE_VALUE (TREE_CHAIN (TREE_CHAIN (TREE_CHAIN (arglist))));

  p = c_getstr (src);
  /* If the SRC parameter is "" or if LEN is 0, return DEST.  */
  if (p && *p == '\0')
    return omit_one_operand (TREE_TYPE (TREE_TYPE (fndecl)), dest, len);
  else if (integer_zerop (len))
    return omit_one_operand (TREE_TYPE (TREE_TYPE (fndecl)), dest, src);

  if (! host_integerp (size, 1))
    return 0;

  if (! integer_all_onesp (size))
    {
      tree src_len = c_strlen (src, 1);
      if (src_len
	  && host_integerp (src_len, 1)
	  && host_integerp (len, 1)
	  && ! tree_int_cst_lt (len, src_len))
	{
	  /* If LEN >= strlen (SRC), optimize into __strcat_chk.  */
	  fn = built_in_decls[BUILT_IN_STRCAT_CHK];
	  if (!fn)
	    return 0;

	  arglist = build_tree_list (NULL_TREE, size);
	  arglist = tree_cons (NULL_TREE, src, arglist);
	  arglist = tree_cons (NULL_TREE, dest, arglist);
	  return build_function_call_expr (fn, arglist);
	}
      return 0;
    }

  arglist = build_tree_list (NULL_TREE, len);
  arglist = tree_cons (NULL_TREE, src, arglist);
  arglist = tree_cons (NULL_TREE, dest, arglist);

  /* If __builtin_strncat_chk is used, assume strncat is available.  */
  fn = built_in_decls[BUILT_IN_STRNCAT];
  if (!fn)
    return 0;

  return build_function_call_expr (fn, arglist);
}

/* Fold a call to __{,v}sprintf_chk with argument list ARGLIST.  Return 0 if
   a normal call should be emitted rather than expanding the function
   inline.  FCODE is either BUILT_IN_SPRINTF_CHK or BUILT_IN_VSPRINTF_CHK.  */

static tree
fold_builtin_sprintf_chk (tree arglist, enum built_in_function fcode)
{
  tree dest, size, len, fn, fmt, flag;
  const char *fmt_str;

  /* Verify the required arguments in the original call.  */
  if (! arglist)
    return 0;
  dest = TREE_VALUE (arglist);
  if (! POINTER_TYPE_P (TREE_TYPE (dest)))
    return 0;
  arglist = TREE_CHAIN (arglist);
  if (! arglist)
    return 0;
  flag = TREE_VALUE (arglist);
  if (TREE_CODE (TREE_TYPE (flag)) != INTEGER_TYPE)
    return 0;
  arglist = TREE_CHAIN (arglist);
  if (! arglist)
    return 0;
  size = TREE_VALUE (arglist);
  if (TREE_CODE (TREE_TYPE (size)) != INTEGER_TYPE)
    return 0;
  arglist = TREE_CHAIN (arglist);
  if (! arglist)
    return 0;
  fmt = TREE_VALUE (arglist);
  if (! POINTER_TYPE_P (TREE_TYPE (fmt)))
    return 0;
  arglist = TREE_CHAIN (arglist);

  if (! host_integerp (size, 1))
    return 0;

  len = NULL_TREE;

  if (!init_target_chars())
    return 0;

  /* Check whether the format is a literal string constant.  */
  fmt_str = c_getstr (fmt);
  if (fmt_str != NULL)
    {
      /* If the format doesn't contain % args or %%, we know the size.  */
      if (strchr (fmt_str, target_percent) == 0)
	{
	  if (fcode != BUILT_IN_SPRINTF_CHK || arglist == NULL_TREE)
	    len = build_int_cstu (size_type_node, strlen (fmt_str));
	}
      /* If the format is "%s" and first ... argument is a string literal,
	 we know the size too.  */
      else if (fcode == BUILT_IN_SPRINTF_CHK && strcmp (fmt_str, target_percent_s) == 0)
	{
	  tree arg;

	  if (arglist && !TREE_CHAIN (arglist))
	    {
	      arg = TREE_VALUE (arglist);
	      if (POINTER_TYPE_P (TREE_TYPE (arg)))
		{
		  len = c_strlen (arg, 1);
		  if (! len || ! host_integerp (len, 1))
		    len = NULL_TREE;
		}
	    }
	}
    }

  if (! integer_all_onesp (size))
    {
      if (! len || ! tree_int_cst_lt (len, size))
	return 0;
    }

  /* Only convert __{,v}sprintf_chk to {,v}sprintf if flag is 0
     or if format doesn't contain % chars or is "%s".  */
  if (! integer_zerop (flag))
    {
      if (fmt_str == NULL)
	return 0;
      if (strchr (fmt_str, target_percent) != NULL && strcmp (fmt_str, target_percent_s))
	return 0;
    }

  arglist = tree_cons (NULL_TREE, fmt, arglist);
  arglist = tree_cons (NULL_TREE, dest, arglist);

  /* If __builtin_{,v}sprintf_chk is used, assume {,v}sprintf is available.  */
  fn = built_in_decls[fcode == BUILT_IN_VSPRINTF_CHK
		      ? BUILT_IN_VSPRINTF : BUILT_IN_SPRINTF];
  if (!fn)
    return 0;

  return build_function_call_expr (fn, arglist);
}

/* Fold a call to {,v}snprintf with argument list ARGLIST.  Return 0 if
   a normal call should be emitted rather than expanding the function
   inline.  FCODE is either BUILT_IN_SNPRINTF_CHK or
   BUILT_IN_VSNPRINTF_CHK.  If MAXLEN is not NULL, it is maximum length
   passed as second argument.  */

tree
fold_builtin_snprintf_chk (tree arglist, tree maxlen,
			   enum built_in_function fcode)
{
  tree dest, size, len, fn, fmt, flag;
  const char *fmt_str;

  /* Verify the required arguments in the original call.  */
  if (! arglist)
    return 0;
  dest = TREE_VALUE (arglist);
  if (! POINTER_TYPE_P (TREE_TYPE (dest)))
    return 0;
  arglist = TREE_CHAIN (arglist);
  if (! arglist)
    return 0;
  len = TREE_VALUE (arglist);
  if (TREE_CODE (TREE_TYPE (len)) != INTEGER_TYPE)
    return 0;
  arglist = TREE_CHAIN (arglist);
  if (! arglist)
    return 0;
  flag = TREE_VALUE (arglist);
  if (TREE_CODE (TREE_TYPE (len)) != INTEGER_TYPE)
    return 0;
  arglist = TREE_CHAIN (arglist);
  if (! arglist)
    return 0;
  size = TREE_VALUE (arglist);
  if (TREE_CODE (TREE_TYPE (size)) != INTEGER_TYPE)
    return 0;
  arglist = TREE_CHAIN (arglist);
  if (! arglist)
    return 0;
  fmt = TREE_VALUE (arglist);
  if (! POINTER_TYPE_P (TREE_TYPE (fmt)))
    return 0;
  arglist = TREE_CHAIN (arglist);

  if (! host_integerp (size, 1))
    return 0;

  if (! integer_all_onesp (size))
    {
      if (! host_integerp (len, 1))
	{
	  /* If LEN is not constant, try MAXLEN too.
	     For MAXLEN only allow optimizing into non-_ocs function
	     if SIZE is >= MAXLEN, never convert to __ocs_fail ().  */
	  if (maxlen == NULL_TREE || ! host_integerp (maxlen, 1))
	    return 0;
	}
      else
	maxlen = len;

      if (tree_int_cst_lt (size, maxlen))
	return 0;
    }

  if (!init_target_chars())
    return 0;

  /* Only convert __{,v}snprintf_chk to {,v}snprintf if flag is 0
     or if format doesn't contain % chars or is "%s".  */
  if (! integer_zerop (flag))
    {
      fmt_str = c_getstr (fmt);
      if (fmt_str == NULL)
	return 0;
      if (strchr (fmt_str, target_percent) != NULL && strcmp (fmt_str, target_percent_s))
	return 0;
    }

  arglist = tree_cons (NULL_TREE, fmt, arglist);
  arglist = tree_cons (NULL_TREE, len, arglist);
  arglist = tree_cons (NULL_TREE, dest, arglist);

  /* If __builtin_{,v}snprintf_chk is used, assume {,v}snprintf is
     available.  */
  fn = built_in_decls[fcode == BUILT_IN_VSNPRINTF_CHK
		      ? BUILT_IN_VSNPRINTF : BUILT_IN_SNPRINTF];
  if (!fn)
    return 0;

  return build_function_call_expr (fn, arglist);
}

/* Fold a call to the {,v}printf{,_unlocked} and __{,v}printf_chk builtins.

   Return 0 if no simplification was possible, otherwise return the
   simplified form of the call as a tree.  FCODE is the BUILT_IN_*
   code of the function to be simplified.  */

static tree
fold_builtin_printf (tree fndecl, tree arglist, bool ignore,
		     enum built_in_function fcode)
{
  tree fmt, fn = NULL_TREE, fn_putchar, fn_puts, arg, call;
  const char *fmt_str = NULL;

  /* If the return value is used, don't do the transformation.  */
  if (! ignore)
    return 0;

  /* Verify the required arguments in the original call.  */
  if (fcode == BUILT_IN_PRINTF_CHK || fcode == BUILT_IN_VPRINTF_CHK)
    {
      tree flag;

      if (! arglist)
	return 0;
      flag = TREE_VALUE (arglist);
      if (TREE_CODE (TREE_TYPE (flag)) != INTEGER_TYPE
	  || TREE_SIDE_EFFECTS (flag))
	return 0;
      arglist = TREE_CHAIN (arglist);
    }

  if (! arglist)
    return 0;
  fmt = TREE_VALUE (arglist);
  if (! POINTER_TYPE_P (TREE_TYPE (fmt)))
    return 0;
  arglist = TREE_CHAIN (arglist);

  /* Check whether the format is a literal string constant.  */
  fmt_str = c_getstr (fmt);
  if (fmt_str == NULL)
    return NULL_TREE;

  if (fcode == BUILT_IN_PRINTF_UNLOCKED)
    {
      /* If we're using an unlocked function, assume the other
	 unlocked functions exist explicitly.  */
      fn_putchar = built_in_decls[BUILT_IN_PUTCHAR_UNLOCKED];
      fn_puts = built_in_decls[BUILT_IN_PUTS_UNLOCKED];
    }
  else
    {
      fn_putchar = implicit_built_in_decls[BUILT_IN_PUTCHAR];
      fn_puts = implicit_built_in_decls[BUILT_IN_PUTS];
    }

  if (!init_target_chars())
    return 0;

  if (strcmp (fmt_str, target_percent_s) == 0 || strchr (fmt_str, target_percent) == NULL)
    {
      const char *str;

      if (strcmp (fmt_str, target_percent_s) == 0)
	{
	  if (fcode == BUILT_IN_VPRINTF || fcode == BUILT_IN_VPRINTF_CHK)
	    return 0;

	  if (! arglist
	      || ! POINTER_TYPE_P (TREE_TYPE (TREE_VALUE (arglist)))
	      || TREE_CHAIN (arglist))
	    return 0;

	  str = c_getstr (TREE_VALUE (arglist));
	  if (str == NULL)
	    return 0;
	}
      else
	{
	  /* The format specifier doesn't contain any '%' characters.  */
	  if (fcode != BUILT_IN_VPRINTF && fcode != BUILT_IN_VPRINTF_CHK
	      && arglist)
	    return 0;
	  str = fmt_str;
	}

      /* If the string was "", printf does nothing.  */
      if (str[0] == '\0')
	return build_int_cst (TREE_TYPE (TREE_TYPE (fndecl)), 0);

      /* If the string has length of 1, call putchar.  */
      if (str[1] == '\0')
	{
	  /* Given printf("c"), (where c is any one character,)
	     convert "c"[0] to an int and pass that to the replacement
	     function.  */
	  arg = build_int_cst (NULL_TREE, str[0]);
	  arglist = build_tree_list (NULL_TREE, arg);
	  fn = fn_putchar;
	}
      else
	{
	  /* If the string was "string\n", call puts("string").  */
	  size_t len = strlen (str);
	  if ((unsigned char)str[len - 1] == target_newline)
	    {
	      /* Create a NUL-terminated string that's one char shorter
		 than the original, stripping off the trailing '\n'.  */
	      char *newstr = alloca (len);
	      memcpy (newstr, str, len - 1);
	      newstr[len - 1] = 0;

	      arg = build_string_literal (len, newstr);
	      arglist = build_tree_list (NULL_TREE, arg);
	      fn = fn_puts;
	    }
	  else
	    /* We'd like to arrange to call fputs(string,stdout) here,
	       but we need stdout and don't have a way to get it yet.  */
	    return 0;
	}
    }

  /* The other optimizations can be done only on the non-va_list variants.  */
  else if (fcode == BUILT_IN_VPRINTF || fcode == BUILT_IN_VPRINTF_CHK)
    return 0;

  /* If the format specifier was "%s\n", call __builtin_puts(arg).  */
  else if (strcmp (fmt_str, target_percent_s_newline) == 0)
    {
      if (! arglist
	  || ! POINTER_TYPE_P (TREE_TYPE (TREE_VALUE (arglist)))
	  || TREE_CHAIN (arglist))
	return 0;
      fn = fn_puts;
    }

  /* If the format specifier was "%c", call __builtin_putchar(arg).  */
  else if (strcmp (fmt_str, target_percent_c) == 0)
    {
      if (! arglist
	  || TREE_CODE (TREE_TYPE (TREE_VALUE (arglist))) != INTEGER_TYPE
	  || TREE_CHAIN (arglist))
	return 0;
      fn = fn_putchar;
    }

  if (!fn)
    return 0;

  call = build_function_call_expr (fn, arglist);
  return fold_convert (TREE_TYPE (TREE_TYPE (fndecl)), call);
}

/* Fold a call to the {,v}fprintf{,_unlocked} and __{,v}printf_chk builtins.

   Return 0 if no simplification was possible, otherwise return the
   simplified form of the call as a tree.  FCODE is the BUILT_IN_*
   code of the function to be simplified.  */

static tree
fold_builtin_fprintf (tree fndecl, tree arglist, bool ignore,
		      enum built_in_function fcode)
{
  tree fp, fmt, fn = NULL_TREE, fn_fputc, fn_fputs, arg, call;
  const char *fmt_str = NULL;

  /* If the return value is used, don't do the transformation.  */
  if (! ignore)
    return 0;

  /* Verify the required arguments in the original call.  */
  if (! arglist)
    return 0;
  fp = TREE_VALUE (arglist);
  if (! POINTER_TYPE_P (TREE_TYPE (fp)))
    return 0;
  arglist = TREE_CHAIN (arglist);

  if (fcode == BUILT_IN_FPRINTF_CHK || fcode == BUILT_IN_VFPRINTF_CHK)
    {
      tree flag;

      if (! arglist)
	return 0;
      flag = TREE_VALUE (arglist);
      if (TREE_CODE (TREE_TYPE (flag)) != INTEGER_TYPE
	  || TREE_SIDE_EFFECTS (flag))
	return 0;
      arglist = TREE_CHAIN (arglist);
    }

  if (! arglist)
    return 0;
  fmt = TREE_VALUE (arglist);
  if (! POINTER_TYPE_P (TREE_TYPE (fmt)))
    return 0;
  arglist = TREE_CHAIN (arglist);

  /* Check whether the format is a literal string constant.  */
  fmt_str = c_getstr (fmt);
  if (fmt_str == NULL)
    return NULL_TREE;

  if (fcode == BUILT_IN_FPRINTF_UNLOCKED)
    {
      /* If we're using an unlocked function, assume the other
	 unlocked functions exist explicitly.  */
      fn_fputc = built_in_decls[BUILT_IN_FPUTC_UNLOCKED];
      fn_fputs = built_in_decls[BUILT_IN_FPUTS_UNLOCKED];
    }
  else
    {
      fn_fputc = implicit_built_in_decls[BUILT_IN_FPUTC];
      fn_fputs = implicit_built_in_decls[BUILT_IN_FPUTS];
    }

  if (!init_target_chars())
    return 0;

  /* If the format doesn't contain % args or %%, use strcpy.  */
  if (strchr (fmt_str, target_percent) == NULL)
    {
      if (fcode != BUILT_IN_VFPRINTF && fcode != BUILT_IN_VFPRINTF_CHK
	  && arglist)
	return 0;

      /* If the format specifier was "", fprintf does nothing.  */
      if (fmt_str[0] == '\0')
	{
	  /* If FP has side-effects, just wait until gimplification is
	     done.  */
	  if (TREE_SIDE_EFFECTS (fp))
	    return 0;

	  return build_int_cst (TREE_TYPE (TREE_TYPE (fndecl)), 0);
	}

      /* When "string" doesn't contain %, replace all cases of
	 fprintf (fp, string) with fputs (string, fp).  The fputs
	 builtin will take care of special cases like length == 1.  */
      arglist = build_tree_list (NULL_TREE, fp);
      arglist = tree_cons (NULL_TREE, fmt, arglist);
      fn = fn_fputs;
    }

  /* The other optimizations can be done only on the non-va_list variants.  */
  else if (fcode == BUILT_IN_VFPRINTF || fcode == BUILT_IN_VFPRINTF_CHK)
    return 0;

  /* If the format specifier was "%s", call __builtin_fputs (arg, fp).  */
  else if (strcmp (fmt_str, target_percent_s) == 0)
    {
      if (! arglist
	  || ! POINTER_TYPE_P (TREE_TYPE (TREE_VALUE (arglist)))
	  || TREE_CHAIN (arglist))
	return 0;
      arg = TREE_VALUE (arglist);
      arglist = build_tree_list (NULL_TREE, fp);
      arglist = tree_cons (NULL_TREE, arg, arglist);
      fn = fn_fputs;
    }

  /* If the format specifier was "%c", call __builtin_fputc (arg, fp).  */
  else if (strcmp (fmt_str, target_percent_c) == 0)
    {
      if (! arglist
	  || TREE_CODE (TREE_TYPE (TREE_VALUE (arglist))) != INTEGER_TYPE
	  || TREE_CHAIN (arglist))
	return 0;
      arg = TREE_VALUE (arglist);
      arglist = build_tree_list (NULL_TREE, fp);
      arglist = tree_cons (NULL_TREE, arg, arglist);
      fn = fn_fputc;
    }

  if (!fn)
    return 0;

  call = build_function_call_expr (fn, arglist);
  return fold_convert (TREE_TYPE (TREE_TYPE (fndecl)), call);
}

/* Initialize format string characters in the target charset.  */

static bool
init_target_chars (void)
{
  static bool init;
  if (!init)
    {
      target_newline = lang_hooks.to_target_charset ('\n');
      target_percent = lang_hooks.to_target_charset ('%');
      target_c = lang_hooks.to_target_charset ('c');
      target_s = lang_hooks.to_target_charset ('s');
      if (target_newline == 0 || target_percent == 0 || target_c == 0
	  || target_s == 0)
	return false;

      target_percent_c[0] = target_percent;
      target_percent_c[1] = target_c;
      target_percent_c[2] = '\0';

      target_percent_s[0] = target_percent;
      target_percent_s[1] = target_s;
      target_percent_s[2] = '\0';

      target_percent_s_newline[0] = target_percent;
      target_percent_s_newline[1] = target_s;
      target_percent_s_newline[2] = target_newline;
      target_percent_s_newline[3] = '\0';

      init = true;
    }
  return true;
}
