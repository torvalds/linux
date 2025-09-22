/* Subroutines for insn-output.c for Tensilica's Xtensa architecture.
   Copyright 2001, 2002, 2003, 2004, 2005, 2006 Free Software Foundation, Inc.
   Contributed by Bob Wilson (bwilson@tensilica.com) at Tensilica.

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
#include "regs.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#include "real.h"
#include "insn-config.h"
#include "conditions.h"
#include "insn-flags.h"
#include "insn-attr.h"
#include "insn-codes.h"
#include "recog.h"
#include "output.h"
#include "tree.h"
#include "expr.h"
#include "flags.h"
#include "reload.h"
#include "tm_p.h"
#include "function.h"
#include "toplev.h"
#include "optabs.h"
#include "libfuncs.h"
#include "ggc.h"
#include "target.h"
#include "target-def.h"
#include "langhooks.h"
#include "tree-gimple.h"


/* Enumeration for all of the relational tests, so that we can build
   arrays indexed by the test type, and not worry about the order
   of EQ, NE, etc.  */

enum internal_test
{
  ITEST_EQ,
  ITEST_NE,
  ITEST_GT,
  ITEST_GE,
  ITEST_LT,
  ITEST_LE,
  ITEST_GTU,
  ITEST_GEU,
  ITEST_LTU,
  ITEST_LEU,
  ITEST_MAX
};

/* Cached operands, and operator to compare for use in set/branch on
   condition codes.  */
rtx branch_cmp[2];

/* what type of branch to use */
enum cmp_type branch_type;

/* Array giving truth value on whether or not a given hard register
   can support a given mode.  */
char xtensa_hard_regno_mode_ok[(int) MAX_MACHINE_MODE][FIRST_PSEUDO_REGISTER];

/* Current frame size calculated by compute_frame_size.  */
unsigned xtensa_current_frame_size;

/* Largest block move to handle in-line.  */
#define LARGEST_MOVE_RATIO 15

/* Define the structure for the machine field in struct function.  */
struct machine_function GTY(())
{
  int accesses_prev_frame;
  bool need_a7_copy;
  bool vararg_a7;
  rtx set_frame_ptr_insn;
};

/* Vector, indexed by hard register number, which contains 1 for a
   register that is allowable in a candidate for leaf function
   treatment.  */

const char xtensa_leaf_regs[FIRST_PSEUDO_REGISTER] =
{
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1
};

/* Map hard register number to register class */
const enum reg_class xtensa_regno_to_class[FIRST_PSEUDO_REGISTER] =
{
  RL_REGS,	SP_REG,		RL_REGS,	RL_REGS,
  RL_REGS,	RL_REGS,	RL_REGS,	GR_REGS,
  RL_REGS,	RL_REGS,	RL_REGS,	RL_REGS,
  RL_REGS,	RL_REGS,	RL_REGS,	RL_REGS,
  AR_REGS,	AR_REGS,	BR_REGS,
  FP_REGS,	FP_REGS,	FP_REGS,	FP_REGS,
  FP_REGS,	FP_REGS,	FP_REGS,	FP_REGS,
  FP_REGS,	FP_REGS,	FP_REGS,	FP_REGS,
  FP_REGS,	FP_REGS,	FP_REGS,	FP_REGS,
  ACC_REG,
};

/* Map register constraint character to register class.  */
enum reg_class xtensa_char_to_class[256] =
{
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
  NO_REGS,	NO_REGS,	NO_REGS,	NO_REGS,
};

static enum internal_test map_test_to_internal_test (enum rtx_code);
static rtx gen_int_relational (enum rtx_code, rtx, rtx, int *);
static rtx gen_float_relational (enum rtx_code, rtx, rtx);
static rtx gen_conditional_move (rtx);
static rtx fixup_subreg_mem (rtx);
static struct machine_function * xtensa_init_machine_status (void);
static bool xtensa_return_in_msb (tree);
static void printx (FILE *, signed int);
static void xtensa_function_epilogue (FILE *, HOST_WIDE_INT);
static rtx xtensa_builtin_saveregs (void);
static unsigned int xtensa_multibss_section_type_flags (tree, const char *,
							int) ATTRIBUTE_UNUSED;
static section *xtensa_select_rtx_section (enum machine_mode, rtx,
					   unsigned HOST_WIDE_INT);
static bool xtensa_rtx_costs (rtx, int, int, int *);
static tree xtensa_build_builtin_va_list (void);
static bool xtensa_return_in_memory (tree, tree);
static tree xtensa_gimplify_va_arg_expr (tree, tree, tree *, tree *);

static const int reg_nonleaf_alloc_order[FIRST_PSEUDO_REGISTER] =
  REG_ALLOC_ORDER;


/* This macro generates the assembly code for function exit,
   on machines that need it.  If FUNCTION_EPILOGUE is not defined
   then individual return instructions are generated for each
   return statement.  Args are same as for FUNCTION_PROLOGUE.  */

#undef TARGET_ASM_FUNCTION_EPILOGUE
#define TARGET_ASM_FUNCTION_EPILOGUE xtensa_function_epilogue

/* These hooks specify assembly directives for creating certain kinds
   of integer object.  */

#undef TARGET_ASM_ALIGNED_SI_OP
#define TARGET_ASM_ALIGNED_SI_OP "\t.word\t"

#undef TARGET_ASM_SELECT_RTX_SECTION
#define TARGET_ASM_SELECT_RTX_SECTION  xtensa_select_rtx_section

#undef TARGET_DEFAULT_TARGET_FLAGS
#define TARGET_DEFAULT_TARGET_FLAGS (TARGET_DEFAULT | MASK_FUSED_MADD)

#undef TARGET_RTX_COSTS
#define TARGET_RTX_COSTS xtensa_rtx_costs
#undef TARGET_ADDRESS_COST
#define TARGET_ADDRESS_COST hook_int_rtx_0

#undef TARGET_BUILD_BUILTIN_VA_LIST
#define TARGET_BUILD_BUILTIN_VA_LIST xtensa_build_builtin_va_list

#undef TARGET_PROMOTE_FUNCTION_ARGS
#define TARGET_PROMOTE_FUNCTION_ARGS hook_bool_tree_true
#undef TARGET_PROMOTE_FUNCTION_RETURN
#define TARGET_PROMOTE_FUNCTION_RETURN hook_bool_tree_true
#undef TARGET_PROMOTE_PROTOTYPES
#define TARGET_PROMOTE_PROTOTYPES hook_bool_tree_true

#undef TARGET_RETURN_IN_MEMORY
#define TARGET_RETURN_IN_MEMORY xtensa_return_in_memory
#undef TARGET_SPLIT_COMPLEX_ARG
#define TARGET_SPLIT_COMPLEX_ARG hook_bool_tree_true
#undef TARGET_MUST_PASS_IN_STACK
#define TARGET_MUST_PASS_IN_STACK must_pass_in_stack_var_size

#undef TARGET_EXPAND_BUILTIN_SAVEREGS
#define TARGET_EXPAND_BUILTIN_SAVEREGS xtensa_builtin_saveregs
#undef TARGET_GIMPLIFY_VA_ARG_EXPR
#define TARGET_GIMPLIFY_VA_ARG_EXPR xtensa_gimplify_va_arg_expr

#undef TARGET_RETURN_IN_MSB
#define TARGET_RETURN_IN_MSB xtensa_return_in_msb

struct gcc_target targetm = TARGET_INITIALIZER;


/*
 * Functions to test Xtensa immediate operand validity.
 */

bool
xtensa_simm8 (HOST_WIDE_INT v)
{
  return v >= -128 && v <= 127;
}


bool
xtensa_simm8x256 (HOST_WIDE_INT v)
{
  return (v & 255) == 0 && (v >= -32768 && v <= 32512);
}


bool
xtensa_simm12b (HOST_WIDE_INT v)
{
  return v >= -2048 && v <= 2047;
}


static bool
xtensa_uimm8 (HOST_WIDE_INT v)
{
  return v >= 0 && v <= 255;
}


static bool
xtensa_uimm8x2 (HOST_WIDE_INT v)
{
  return (v & 1) == 0 && (v >= 0 && v <= 510);
}


static bool
xtensa_uimm8x4 (HOST_WIDE_INT v)
{
  return (v & 3) == 0 && (v >= 0 && v <= 1020);
}


static bool
xtensa_b4const (HOST_WIDE_INT v)
{
  switch (v)
    {
    case -1:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 12:
    case 16:
    case 32:
    case 64:
    case 128:
    case 256:
      return true;
    }
  return false;
}


bool
xtensa_b4const_or_zero (HOST_WIDE_INT v)
{
  if (v == 0)
    return true;
  return xtensa_b4const (v);
}


bool
xtensa_b4constu (HOST_WIDE_INT v)
{
  switch (v)
    {
    case 32768:
    case 65536:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 12:
    case 16:
    case 32:
    case 64:
    case 128:
    case 256:
      return true;
    }
  return false;
}


bool
xtensa_mask_immediate (HOST_WIDE_INT v)
{
#define MAX_MASK_SIZE 16
  int mask_size;

  for (mask_size = 1; mask_size <= MAX_MASK_SIZE; mask_size++)
    {
      if ((v & 1) == 0)
	return false;
      v = v >> 1;
      if (v == 0)
	return true;
    }

  return false;
}


bool
xtensa_const_ok_for_letter_p (HOST_WIDE_INT v, int c)
{
  switch (c)
    {
    case 'I': return xtensa_simm12b (v);
    case 'J': return xtensa_simm8 (v);
    case 'K': return (v == 0 || xtensa_b4const (v));
    case 'L': return xtensa_b4constu (v);
    case 'M': return (v >= -32 && v <= 95);
    case 'N': return xtensa_simm8x256 (v);
    case 'O': return (v == -1 || (v >= 1 && v <= 15));
    case 'P': return xtensa_mask_immediate (v);
    default: break;
    }
  return false;
}


/* This is just like the standard true_regnum() function except that it
   works even when reg_renumber is not initialized.  */

int
xt_true_regnum (rtx x)
{
  if (GET_CODE (x) == REG)
    {
      if (reg_renumber
	  && REGNO (x) >= FIRST_PSEUDO_REGISTER
	  && reg_renumber[REGNO (x)] >= 0)
	return reg_renumber[REGNO (x)];
      return REGNO (x);
    }
  if (GET_CODE (x) == SUBREG)
    {
      int base = xt_true_regnum (SUBREG_REG (x));
      if (base >= 0 && base < FIRST_PSEUDO_REGISTER)
        return base + subreg_regno_offset (REGNO (SUBREG_REG (x)),
                                           GET_MODE (SUBREG_REG (x)),
                                           SUBREG_BYTE (x), GET_MODE (x));
    }
  return -1;
}


int
xtensa_valid_move (enum machine_mode mode, rtx *operands)
{
  /* Either the destination or source must be a register, and the
     MAC16 accumulator doesn't count.  */

  if (register_operand (operands[0], mode))
    {
      int dst_regnum = xt_true_regnum (operands[0]);

      /* The stack pointer can only be assigned with a MOVSP opcode.  */
      if (dst_regnum == STACK_POINTER_REGNUM)
	return (mode == SImode
		&& register_operand (operands[1], mode)
		&& !ACC_REG_P (xt_true_regnum (operands[1])));

      if (!ACC_REG_P (dst_regnum))
	return true;
    }
  if (register_operand (operands[1], mode))
    {
      int src_regnum = xt_true_regnum (operands[1]);
      if (!ACC_REG_P (src_regnum))
	return true;
    }
  return FALSE;
}


int
smalloffset_mem_p (rtx op)
{
  if (GET_CODE (op) == MEM)
    {
      rtx addr = XEXP (op, 0);
      if (GET_CODE (addr) == REG)
	return REG_OK_FOR_BASE_P (addr);
      if (GET_CODE (addr) == PLUS)
	{
	  rtx offset = XEXP (addr, 0);
	  HOST_WIDE_INT val;
	  if (GET_CODE (offset) != CONST_INT)
	    offset = XEXP (addr, 1);
	  if (GET_CODE (offset) != CONST_INT)
	    return FALSE;

	  val = INTVAL (offset);
	  return (val & 3) == 0 && (val >= 0 && val <= 60);
	}
    }
  return FALSE;
}


int
constantpool_address_p (rtx addr)
{
  rtx sym = addr;

  if (GET_CODE (addr) == CONST)
    {
      rtx offset;

      /* Only handle (PLUS (SYM, OFFSET)) form.  */
      addr = XEXP (addr, 0);
      if (GET_CODE (addr) != PLUS)
	return FALSE;

      /* Make sure the address is word aligned.  */
      offset = XEXP (addr, 1);
      if ((GET_CODE (offset) != CONST_INT)
	  || ((INTVAL (offset) & 3) != 0))
	return FALSE;

      sym = XEXP (addr, 0);
    }

  if ((GET_CODE (sym) == SYMBOL_REF)
      && CONSTANT_POOL_ADDRESS_P (sym))
    return TRUE;
  return FALSE;
}


int
constantpool_mem_p (rtx op)
{
  if (GET_CODE (op) == SUBREG)
    op = SUBREG_REG (op);
  if (GET_CODE (op) == MEM)
    return constantpool_address_p (XEXP (op, 0));
  return FALSE;
}


void
xtensa_extend_reg (rtx dst, rtx src)
{
  rtx temp = gen_reg_rtx (SImode);
  rtx shift = GEN_INT (BITS_PER_WORD - GET_MODE_BITSIZE (GET_MODE (src)));

  /* Generate paradoxical subregs as needed so that the modes match.  */
  src = simplify_gen_subreg (SImode, src, GET_MODE (src), 0);
  dst = simplify_gen_subreg (SImode, dst, GET_MODE (dst), 0);

  emit_insn (gen_ashlsi3 (temp, src, shift));
  emit_insn (gen_ashrsi3 (dst, temp, shift));
}


bool
xtensa_mem_offset (unsigned v, enum machine_mode mode)
{
  switch (mode)
    {
    case BLKmode:
      /* Handle the worst case for block moves.  See xtensa_expand_block_move
	 where we emit an optimized block move operation if the block can be
	 moved in < "move_ratio" pieces.  The worst case is when the block is
	 aligned but has a size of (3 mod 4) (does this happen?) so that the
	 last piece requires a byte load/store.  */
      return (xtensa_uimm8 (v)
	      && xtensa_uimm8 (v + MOVE_MAX * LARGEST_MOVE_RATIO));

    case QImode:
      return xtensa_uimm8 (v);

    case HImode:
      return xtensa_uimm8x2 (v);

    case DFmode:
      return (xtensa_uimm8x4 (v) && xtensa_uimm8x4 (v + 4));

    default:
      break;
    }

  return xtensa_uimm8x4 (v);
}


bool
xtensa_extra_constraint (rtx op, int c)
{
  /* Allow pseudo registers during reload.  */
  if (GET_CODE (op) != MEM)
    return (c >= 'R' && c <= 'U'
	    && reload_in_progress && GET_CODE (op) == REG
	    && REGNO (op) >= FIRST_PSEUDO_REGISTER);

  switch (c)
    {
    case 'R': return smalloffset_mem_p (op);
    case 'T': return !TARGET_CONST16 && constantpool_mem_p (op);
    case 'U': return !constantpool_mem_p (op);
    default: break;
    }
  return false;
}


/* Make normal rtx_code into something we can index from an array.  */

static enum internal_test
map_test_to_internal_test (enum rtx_code test_code)
{
  enum internal_test test = ITEST_MAX;

  switch (test_code)
    {
    default:			break;
    case EQ:  test = ITEST_EQ;  break;
    case NE:  test = ITEST_NE;  break;
    case GT:  test = ITEST_GT;  break;
    case GE:  test = ITEST_GE;  break;
    case LT:  test = ITEST_LT;  break;
    case LE:  test = ITEST_LE;  break;
    case GTU: test = ITEST_GTU; break;
    case GEU: test = ITEST_GEU; break;
    case LTU: test = ITEST_LTU; break;
    case LEU: test = ITEST_LEU; break;
    }

  return test;
}


/* Generate the code to compare two integer values.  The return value is
   the comparison expression.  */

static rtx
gen_int_relational (enum rtx_code test_code, /* relational test (EQ, etc) */
		    rtx cmp0, /* first operand to compare */
		    rtx cmp1, /* second operand to compare */
		    int *p_invert /* whether branch needs to reverse test */)
{
  struct cmp_info
  {
    enum rtx_code test_code;	/* test code to use in insn */
    bool (*const_range_p) (HOST_WIDE_INT); /* range check function */
    int const_add;		/* constant to add (convert LE -> LT) */
    int reverse_regs;		/* reverse registers in test */
    int invert_const;		/* != 0 if invert value if cmp1 is constant */
    int invert_reg;		/* != 0 if invert value if cmp1 is register */
    int unsignedp;		/* != 0 for unsigned comparisons.  */
  };

  static struct cmp_info info[ (int)ITEST_MAX ] = {

    { EQ,	xtensa_b4const_or_zero,	0, 0, 0, 0, 0 },	/* EQ  */
    { NE,	xtensa_b4const_or_zero,	0, 0, 0, 0, 0 },	/* NE  */

    { LT,	xtensa_b4const_or_zero,	1, 1, 1, 0, 0 },	/* GT  */
    { GE,	xtensa_b4const_or_zero,	0, 0, 0, 0, 0 },	/* GE  */
    { LT,	xtensa_b4const_or_zero,	0, 0, 0, 0, 0 },	/* LT  */
    { GE,	xtensa_b4const_or_zero,	1, 1, 1, 0, 0 },	/* LE  */

    { LTU,	xtensa_b4constu,	1, 1, 1, 0, 1 },	/* GTU */
    { GEU,	xtensa_b4constu,	0, 0, 0, 0, 1 },	/* GEU */
    { LTU,	xtensa_b4constu,	0, 0, 0, 0, 1 },	/* LTU */
    { GEU,	xtensa_b4constu,	1, 1, 1, 0, 1 },	/* LEU */
  };

  enum internal_test test;
  enum machine_mode mode;
  struct cmp_info *p_info;

  test = map_test_to_internal_test (test_code);
  gcc_assert (test != ITEST_MAX);

  p_info = &info[ (int)test ];

  mode = GET_MODE (cmp0);
  if (mode == VOIDmode)
    mode = GET_MODE (cmp1);

  /* Make sure we can handle any constants given to us.  */
  if (GET_CODE (cmp1) == CONST_INT)
    {
      HOST_WIDE_INT value = INTVAL (cmp1);
      unsigned HOST_WIDE_INT uvalue = (unsigned HOST_WIDE_INT)value;

      /* if the immediate overflows or does not fit in the immediate field,
	 spill it to a register */

      if ((p_info->unsignedp ?
	   (uvalue + p_info->const_add > uvalue) :
	   (value + p_info->const_add > value)) != (p_info->const_add > 0))
	{
	  cmp1 = force_reg (mode, cmp1);
	}
      else if (!(p_info->const_range_p) (value + p_info->const_add))
	{
	  cmp1 = force_reg (mode, cmp1);
	}
    }
  else if ((GET_CODE (cmp1) != REG) && (GET_CODE (cmp1) != SUBREG))
    {
      cmp1 = force_reg (mode, cmp1);
    }

  /* See if we need to invert the result.  */
  *p_invert = ((GET_CODE (cmp1) == CONST_INT)
	       ? p_info->invert_const
	       : p_info->invert_reg);

  /* Comparison to constants, may involve adding 1 to change a LT into LE.
     Comparison between two registers, may involve switching operands.  */
  if (GET_CODE (cmp1) == CONST_INT)
    {
      if (p_info->const_add != 0)
	cmp1 = GEN_INT (INTVAL (cmp1) + p_info->const_add);

    }
  else if (p_info->reverse_regs)
    {
      rtx temp = cmp0;
      cmp0 = cmp1;
      cmp1 = temp;
    }

  return gen_rtx_fmt_ee (p_info->test_code, VOIDmode, cmp0, cmp1);
}


/* Generate the code to compare two float values.  The return value is
   the comparison expression.  */

static rtx
gen_float_relational (enum rtx_code test_code, /* relational test (EQ, etc) */
		      rtx cmp0, /* first operand to compare */
		      rtx cmp1 /* second operand to compare */)
{
  rtx (*gen_fn) (rtx, rtx, rtx);
  rtx brtmp;
  int reverse_regs, invert;

  switch (test_code)
    {
    case EQ: reverse_regs = 0; invert = 0; gen_fn = gen_seq_sf; break;
    case NE: reverse_regs = 0; invert = 1; gen_fn = gen_seq_sf; break;
    case LE: reverse_regs = 0; invert = 0; gen_fn = gen_sle_sf; break;
    case GT: reverse_regs = 1; invert = 0; gen_fn = gen_slt_sf; break;
    case LT: reverse_regs = 0; invert = 0; gen_fn = gen_slt_sf; break;
    case GE: reverse_regs = 1; invert = 0; gen_fn = gen_sle_sf; break;
    default:
      fatal_insn ("bad test", gen_rtx_fmt_ee (test_code, VOIDmode, cmp0, cmp1));
      reverse_regs = 0; invert = 0; gen_fn = 0; /* avoid compiler warnings */
    }

  if (reverse_regs)
    {
      rtx temp = cmp0;
      cmp0 = cmp1;
      cmp1 = temp;
    }

  brtmp = gen_rtx_REG (CCmode, FPCC_REGNUM);
  emit_insn (gen_fn (brtmp, cmp0, cmp1));

  return gen_rtx_fmt_ee (invert ? EQ : NE, VOIDmode, brtmp, const0_rtx);
}


void
xtensa_expand_conditional_branch (rtx *operands, enum rtx_code test_code)
{
  enum cmp_type type = branch_type;
  rtx cmp0 = branch_cmp[0];
  rtx cmp1 = branch_cmp[1];
  rtx cmp;
  int invert;
  rtx label1, label2;

  switch (type)
    {
    case CMP_DF:
    default:
      fatal_insn ("bad test", gen_rtx_fmt_ee (test_code, VOIDmode, cmp0, cmp1));

    case CMP_SI:
      invert = FALSE;
      cmp = gen_int_relational (test_code, cmp0, cmp1, &invert);
      break;

    case CMP_SF:
      if (!TARGET_HARD_FLOAT)
	fatal_insn ("bad test", gen_rtx_fmt_ee (test_code, VOIDmode, cmp0, cmp1));
      invert = FALSE;
      cmp = gen_float_relational (test_code, cmp0, cmp1);
      break;
    }

  /* Generate the branch.  */

  label1 = gen_rtx_LABEL_REF (VOIDmode, operands[0]);
  label2 = pc_rtx;

  if (invert)
    {
      label2 = label1;
      label1 = pc_rtx;
    }

  emit_jump_insn (gen_rtx_SET (VOIDmode, pc_rtx,
			       gen_rtx_IF_THEN_ELSE (VOIDmode, cmp,
						     label1,
						     label2)));
}


static rtx
gen_conditional_move (rtx cmp)
{
  enum rtx_code code = GET_CODE (cmp);
  rtx op0 = branch_cmp[0];
  rtx op1 = branch_cmp[1];

  if (branch_type == CMP_SI)
    {
      /* Jump optimization calls get_condition() which canonicalizes
	 comparisons like (GE x <const>) to (GT x <const-1>).
	 Transform those comparisons back to GE, since that is the
	 comparison supported in Xtensa.  We shouldn't have to
	 transform <LE x const> comparisons, because neither
	 xtensa_expand_conditional_branch() nor get_condition() will
	 produce them.  */

      if ((code == GT) && (op1 == constm1_rtx))
	{
	  code = GE;
	  op1 = const0_rtx;
	}
      cmp = gen_rtx_fmt_ee (code, VOIDmode, cc0_rtx, const0_rtx);

      if (boolean_operator (cmp, VOIDmode))
	{
	  /* Swap the operands to make const0 second.  */
	  if (op0 == const0_rtx)
	    {
	      op0 = op1;
	      op1 = const0_rtx;
	    }

	  /* If not comparing against zero, emit a comparison (subtract).  */
	  if (op1 != const0_rtx)
	    {
	      op0 = expand_binop (SImode, sub_optab, op0, op1,
				  0, 0, OPTAB_LIB_WIDEN);
	      op1 = const0_rtx;
	    }
	}
      else if (branch_operator (cmp, VOIDmode))
	{
	  /* Swap the operands to make const0 second.  */
	  if (op0 == const0_rtx)
	    {
	      op0 = op1;
	      op1 = const0_rtx;

	      switch (code)
		{
		case LT: code = GE; break;
		case GE: code = LT; break;
		default: gcc_unreachable ();
		}
	    }

	  if (op1 != const0_rtx)
	    return 0;
	}
      else
	return 0;

      return gen_rtx_fmt_ee (code, VOIDmode, op0, op1);
    }

  if (TARGET_HARD_FLOAT && (branch_type == CMP_SF))
    return gen_float_relational (code, op0, op1);

  return 0;
}


int
xtensa_expand_conditional_move (rtx *operands, int isflt)
{
  rtx cmp;
  rtx (*gen_fn) (rtx, rtx, rtx, rtx, rtx);

  if (!(cmp = gen_conditional_move (operands[1])))
    return 0;

  if (isflt)
    gen_fn = (branch_type == CMP_SI
	      ? gen_movsfcc_internal0
	      : gen_movsfcc_internal1);
  else
    gen_fn = (branch_type == CMP_SI
	      ? gen_movsicc_internal0
	      : gen_movsicc_internal1);

  emit_insn (gen_fn (operands[0], XEXP (cmp, 0),
		     operands[2], operands[3], cmp));
  return 1;
}


int
xtensa_expand_scc (rtx *operands)
{
  rtx dest = operands[0];
  rtx cmp = operands[1];
  rtx one_tmp, zero_tmp;
  rtx (*gen_fn) (rtx, rtx, rtx, rtx, rtx);

  if (!(cmp = gen_conditional_move (cmp)))
    return 0;

  one_tmp = gen_reg_rtx (SImode);
  zero_tmp = gen_reg_rtx (SImode);
  emit_insn (gen_movsi (one_tmp, const_true_rtx));
  emit_insn (gen_movsi (zero_tmp, const0_rtx));

  gen_fn = (branch_type == CMP_SI
	    ? gen_movsicc_internal0
	    : gen_movsicc_internal1);
  emit_insn (gen_fn (dest, XEXP (cmp, 0), one_tmp, zero_tmp, cmp));
  return 1;
}


/* Split OP[1] into OP[2,3] and likewise for OP[0] into OP[0,1].  MODE is
   for the output, i.e., the input operands are twice as big as MODE.  */

void
xtensa_split_operand_pair (rtx operands[4], enum machine_mode mode)
{
  switch (GET_CODE (operands[1]))
    {
    case REG:
      operands[3] = gen_rtx_REG (mode, REGNO (operands[1]) + 1);
      operands[2] = gen_rtx_REG (mode, REGNO (operands[1]));
      break;

    case MEM:
      operands[3] = adjust_address (operands[1], mode, GET_MODE_SIZE (mode));
      operands[2] = adjust_address (operands[1], mode, 0);
      break;

    case CONST_INT:
    case CONST_DOUBLE:
      split_double (operands[1], &operands[2], &operands[3]);
      break;

    default:
      gcc_unreachable ();
    }

  switch (GET_CODE (operands[0]))
    {
    case REG:
      operands[1] = gen_rtx_REG (mode, REGNO (operands[0]) + 1);
      operands[0] = gen_rtx_REG (mode, REGNO (operands[0]));
      break;

    case MEM:
      operands[1] = adjust_address (operands[0], mode, GET_MODE_SIZE (mode));
      operands[0] = adjust_address (operands[0], mode, 0);
      break;

    default:
      gcc_unreachable ();
    }
}


/* Emit insns to move operands[1] into operands[0].
   Return 1 if we have written out everything that needs to be done to
   do the move.  Otherwise, return 0 and the caller will emit the move
   normally.  */

int
xtensa_emit_move_sequence (rtx *operands, enum machine_mode mode)
{
  if (CONSTANT_P (operands[1])
      && (GET_CODE (operands[1]) != CONST_INT
	  || !xtensa_simm12b (INTVAL (operands[1]))))
    {
      if (!TARGET_CONST16)
	operands[1] = force_const_mem (SImode, operands[1]);

      /* PC-relative loads are always SImode, and CONST16 is only
	 supported in the movsi pattern, so add a SUBREG for any other
	 (smaller) mode.  */

      if (mode != SImode)
	{
	  if (register_operand (operands[0], mode))
	    {
	      operands[0] = simplify_gen_subreg (SImode, operands[0], mode, 0);
	      emit_move_insn (operands[0], operands[1]);
	      return 1;
	    }
	  else
	    {
	      operands[1] = force_reg (SImode, operands[1]);
	      operands[1] = gen_lowpart_SUBREG (mode, operands[1]);
	    }
	}
    }

  if (!(reload_in_progress | reload_completed)
      && !xtensa_valid_move (mode, operands))
    operands[1] = force_reg (mode, operands[1]);

  operands[1] = xtensa_copy_incoming_a7 (operands[1]);

  /* During reload we don't want to emit (subreg:X (mem:Y)) since that
     instruction won't be recognized after reload, so we remove the
     subreg and adjust mem accordingly.  */
  if (reload_in_progress)
    {
      operands[0] = fixup_subreg_mem (operands[0]);
      operands[1] = fixup_subreg_mem (operands[1]);
    }
  return 0;
}


static rtx
fixup_subreg_mem (rtx x)
{
  if (GET_CODE (x) == SUBREG
      && GET_CODE (SUBREG_REG (x)) == REG
      && REGNO (SUBREG_REG (x)) >= FIRST_PSEUDO_REGISTER)
    {
      rtx temp =
	gen_rtx_SUBREG (GET_MODE (x),
			reg_equiv_mem [REGNO (SUBREG_REG (x))],
			SUBREG_BYTE (x));
      x = alter_subreg (&temp);
    }
  return x;
}


/* Check if an incoming argument in a7 is expected to be used soon and
   if OPND is a register or register pair that includes a7.  If so,
   create a new pseudo and copy a7 into that pseudo at the very
   beginning of the function, followed by the special "set_frame_ptr"
   unspec_volatile insn.  The return value is either the original
   operand, if it is not a7, or the new pseudo containing a copy of
   the incoming argument.  This is necessary because the register
   allocator will ignore conflicts with a7 and may either assign some
   other pseudo to a7 or use a7 as the hard_frame_pointer, clobbering
   the incoming argument in a7.  By copying the argument out of a7 as
   the very first thing, and then immediately following that with an
   unspec_volatile to keep the scheduler away, we should avoid any
   problems.  Putting the set_frame_ptr insn at the beginning, with
   only the a7 copy before it, also makes it easier for the prologue
   expander to initialize the frame pointer after the a7 copy and to
   fix up the a7 copy to use the stack pointer instead of the frame
   pointer.  */

rtx
xtensa_copy_incoming_a7 (rtx opnd)
{
  rtx entry_insns = 0;
  rtx reg, tmp;
  enum machine_mode mode;

  if (!cfun->machine->need_a7_copy)
    return opnd;

  /* This function should never be called again once a7 has been copied.  */
  gcc_assert (!cfun->machine->set_frame_ptr_insn);

  mode = GET_MODE (opnd);

  /* The operand using a7 may come in a later instruction, so just return
     the original operand if it doesn't use a7.  */
  reg = opnd;
  if (GET_CODE (reg) == SUBREG)
    {
      gcc_assert (SUBREG_BYTE (reg) == 0);
      reg = SUBREG_REG (reg);
    }
  if (GET_CODE (reg) != REG
      || REGNO (reg) > A7_REG
      || REGNO (reg) + HARD_REGNO_NREGS (A7_REG, mode) <= A7_REG)
    return opnd;

  /* 1-word args will always be in a7; 2-word args in a6/a7.  */
  gcc_assert (REGNO (reg) + HARD_REGNO_NREGS (A7_REG, mode) - 1 == A7_REG);

  cfun->machine->need_a7_copy = false;

  /* Copy a7 to a new pseudo at the function entry.  Use gen_raw_REG to
     create the REG for a7 so that hard_frame_pointer_rtx is not used.  */

  push_to_sequence (entry_insns);
  tmp = gen_reg_rtx (mode);

  switch (mode)
    {
    case DFmode:
    case DImode:
      emit_insn (gen_movsi_internal (gen_rtx_SUBREG (SImode, tmp, 0),
				     gen_rtx_REG (SImode, A7_REG - 1)));
      emit_insn (gen_movsi_internal (gen_rtx_SUBREG (SImode, tmp, 4),
				     gen_raw_REG (SImode, A7_REG)));
      break;
    case SFmode:
      emit_insn (gen_movsf_internal (tmp, gen_raw_REG (mode, A7_REG)));
      break;
    case SImode:
      emit_insn (gen_movsi_internal (tmp, gen_raw_REG (mode, A7_REG)));
      break;
    case HImode:
      emit_insn (gen_movhi_internal (tmp, gen_raw_REG (mode, A7_REG)));
      break;
    case QImode:
      emit_insn (gen_movqi_internal (tmp, gen_raw_REG (mode, A7_REG)));
      break;
    default:
      gcc_unreachable ();
    }

  cfun->machine->set_frame_ptr_insn = emit_insn (gen_set_frame_ptr ());
  entry_insns = get_insns ();
  end_sequence ();

  if (cfun->machine->vararg_a7)
    {
      /* This is called from within builtin_savereg, so we're already
	 inside a start_sequence that will be placed at the start of
	 the function.  */
      emit_insn (entry_insns);
    }
  else
    {
      /* Put entry_insns after the NOTE that starts the function.  If
	 this is inside a start_sequence, make the outer-level insn
	 chain current, so the code is placed at the start of the
	 function.  */
      push_topmost_sequence ();
      emit_insn_after (entry_insns, get_insns ());
      pop_topmost_sequence ();
    }

  return tmp;
}


/* Try to expand a block move operation to a sequence of RTL move
   instructions.  If not optimizing, or if the block size is not a
   constant, or if the block is too large, the expansion fails and GCC
   falls back to calling memcpy().

   operands[0] is the destination
   operands[1] is the source
   operands[2] is the length
   operands[3] is the alignment */

int
xtensa_expand_block_move (rtx *operands)
{
  static const enum machine_mode mode_from_align[] =
  {
    VOIDmode, QImode, HImode, VOIDmode, SImode,
  };

  rtx dst_mem = operands[0];
  rtx src_mem = operands[1];
  HOST_WIDE_INT bytes, align;
  int num_pieces, move_ratio;
  rtx temp[2];
  enum machine_mode mode[2];
  int amount[2];
  bool active[2];
  int phase = 0;
  int next;
  int offset_ld = 0;
  int offset_st = 0;
  rtx x;

  /* If this is not a fixed size move, just call memcpy.  */
  if (!optimize || (GET_CODE (operands[2]) != CONST_INT))
    return 0;

  bytes = INTVAL (operands[2]);
  align = INTVAL (operands[3]);

  /* Anything to move?  */
  if (bytes <= 0)
    return 0;

  if (align > MOVE_MAX)
    align = MOVE_MAX;

  /* Decide whether to expand inline based on the optimization level.  */
  move_ratio = 4;
  if (optimize > 2)
    move_ratio = LARGEST_MOVE_RATIO;
  num_pieces = (bytes / align) + (bytes % align); /* Close enough anyway.  */
  if (num_pieces > move_ratio)
    return 0;

  x = XEXP (dst_mem, 0);
  if (!REG_P (x))
    {
      x = force_reg (Pmode, x);
      dst_mem = replace_equiv_address (dst_mem, x);
    }

  x = XEXP (src_mem, 0);
  if (!REG_P (x))
    {
      x = force_reg (Pmode, x);
      src_mem = replace_equiv_address (src_mem, x);
    }

  active[0] = active[1] = false;

  do
    {
      next = phase;
      phase ^= 1;

      if (bytes > 0)
	{
	  int next_amount;

	  next_amount = (bytes >= 4 ? 4 : (bytes >= 2 ? 2 : 1));
	  next_amount = MIN (next_amount, align);

	  amount[next] = next_amount;
	  mode[next] = mode_from_align[next_amount];
	  temp[next] = gen_reg_rtx (mode[next]);

	  x = adjust_address (src_mem, mode[next], offset_ld);
	  emit_insn (gen_rtx_SET (VOIDmode, temp[next], x));

	  offset_ld += next_amount;
	  bytes -= next_amount;
	  active[next] = true;
	}

      if (active[phase])
	{
	  active[phase] = false;
	  
	  x = adjust_address (dst_mem, mode[phase], offset_st);
	  emit_insn (gen_rtx_SET (VOIDmode, x, temp[phase]));

	  offset_st += amount[phase];
	}
    }
  while (active[next]);

  return 1;
}


void
xtensa_expand_nonlocal_goto (rtx *operands)
{
  rtx goto_handler = operands[1];
  rtx containing_fp = operands[3];

  /* Generate a call to "__xtensa_nonlocal_goto" (in libgcc); the code
     is too big to generate in-line.  */

  if (GET_CODE (containing_fp) != REG)
    containing_fp = force_reg (Pmode, containing_fp);

  goto_handler = replace_rtx (copy_rtx (goto_handler),
			      virtual_stack_vars_rtx,
			      containing_fp);

  emit_library_call (gen_rtx_SYMBOL_REF (Pmode, "__xtensa_nonlocal_goto"),
		     0, VOIDmode, 2,
		     containing_fp, Pmode,
		     goto_handler, Pmode);
}


static struct machine_function *
xtensa_init_machine_status (void)
{
  return ggc_alloc_cleared (sizeof (struct machine_function));
}


void
xtensa_setup_frame_addresses (void)
{
  /* Set flag to cause FRAME_POINTER_REQUIRED to be set.  */
  cfun->machine->accesses_prev_frame = 1;

  emit_library_call
    (gen_rtx_SYMBOL_REF (Pmode, "__xtensa_libgcc_window_spill"),
     0, VOIDmode, 0);
}


/* Emit the assembly for the end of a zero-cost loop.  Normally we just emit
   a comment showing where the end of the loop is.  However, if there is a
   label or a branch at the end of the loop then we need to place a nop
   there.  If the loop ends with a label we need the nop so that branches
   targeting that label will target the nop (and thus remain in the loop),
   instead of targeting the instruction after the loop (and thus exiting
   the loop).  If the loop ends with a branch, we need the nop in case the
   branch is targeting a location inside the loop.  When the branch
   executes it will cause the loop count to be decremented even if it is
   taken (because it is the last instruction in the loop), so we need to
   nop after the branch to prevent the loop count from being decremented
   when the branch is taken.  */

void
xtensa_emit_loop_end (rtx insn, rtx *operands)
{
  char done = 0;

  for (insn = PREV_INSN (insn); insn && !done; insn = PREV_INSN (insn))
    {
      switch (GET_CODE (insn))
	{
	case NOTE:
	case BARRIER:
	  break;

	case CODE_LABEL:
	  output_asm_insn (TARGET_DENSITY ? "nop.n" : "nop", operands);
	  done = 1;
	  break;

	default:
	  {
	    rtx body = PATTERN (insn);

	    if (GET_CODE (body) == JUMP_INSN)
	      {
		output_asm_insn (TARGET_DENSITY ? "nop.n" : "nop", operands);
		done = 1;
	      }
	    else if ((GET_CODE (body) != USE)
		     && (GET_CODE (body) != CLOBBER))
	      done = 1;
	  }
	  break;
        }
    }

  output_asm_insn ("# loop end for %0", operands);
}


char *
xtensa_emit_call (int callop, rtx *operands)
{
  static char result[64];
  rtx tgt = operands[callop];

  if (GET_CODE (tgt) == CONST_INT)
    sprintf (result, "call8\t0x%lx", INTVAL (tgt));
  else if (register_operand (tgt, VOIDmode))
    sprintf (result, "callx8\t%%%d", callop);
  else
    sprintf (result, "call8\t%%%d", callop);

  return result;
}


/* Return the debugger register number to use for 'regno'.  */

int
xtensa_dbx_register_number (int regno)
{
  int first = -1;

  if (GP_REG_P (regno))
    {
      regno -= GP_REG_FIRST;
      first = 0;
    }
  else if (BR_REG_P (regno))
    {
      regno -= BR_REG_FIRST;
      first = 16;
    }
  else if (FP_REG_P (regno))
    {
      regno -= FP_REG_FIRST;
      first = 48;
    }
  else if (ACC_REG_P (regno))
    {
      first = 0x200;	/* Start of Xtensa special registers.  */
      regno = 16;	/* ACCLO is special register 16.  */
    }

  /* When optimizing, we sometimes get asked about pseudo-registers
     that don't represent hard registers.  Return 0 for these.  */
  if (first == -1)
    return 0;

  return first + regno;
}


/* Argument support functions.  */

/* Initialize CUMULATIVE_ARGS for a function.  */

void
init_cumulative_args (CUMULATIVE_ARGS *cum, int incoming)
{
  cum->arg_words = 0;
  cum->incoming = incoming;
}


/* Advance the argument to the next argument position.  */

void
function_arg_advance (CUMULATIVE_ARGS *cum, enum machine_mode mode, tree type)
{
  int words, max;
  int *arg_words;

  arg_words = &cum->arg_words;
  max = MAX_ARGS_IN_REGISTERS;

  words = (((mode != BLKmode)
	    ? (int) GET_MODE_SIZE (mode)
	    : int_size_in_bytes (type)) + UNITS_PER_WORD - 1) / UNITS_PER_WORD;

  if (*arg_words < max
      && (targetm.calls.must_pass_in_stack (mode, type)
	  || *arg_words + words > max))
    *arg_words = max;

  *arg_words += words;
}


/* Return an RTL expression containing the register for the given mode,
   or 0 if the argument is to be passed on the stack.  INCOMING_P is nonzero
   if this is an incoming argument to the current function.  */

rtx
function_arg (CUMULATIVE_ARGS *cum, enum machine_mode mode, tree type,
	      int incoming_p)
{
  int regbase, words, max;
  int *arg_words;
  int regno;

  arg_words = &cum->arg_words;
  regbase = (incoming_p ? GP_ARG_FIRST : GP_OUTGOING_ARG_FIRST);
  max = MAX_ARGS_IN_REGISTERS;

  words = (((mode != BLKmode)
	    ? (int) GET_MODE_SIZE (mode)
	    : int_size_in_bytes (type)) + UNITS_PER_WORD - 1) / UNITS_PER_WORD;

  if (type && (TYPE_ALIGN (type) > BITS_PER_WORD))
    {
      int align = MIN (TYPE_ALIGN (type), STACK_BOUNDARY) / BITS_PER_WORD;
      *arg_words = (*arg_words + align - 1) & -align;
    }

  if (*arg_words + words > max)
    return (rtx)0;

  regno = regbase + *arg_words;

  if (cum->incoming && regno <= A7_REG && regno + words > A7_REG)
    cfun->machine->need_a7_copy = true;

  return gen_rtx_REG (mode, regno);
}


int
function_arg_boundary (enum machine_mode mode, tree type)
{
  unsigned int alignment;

  alignment = type ? TYPE_ALIGN (type) : GET_MODE_ALIGNMENT (mode);
  if (alignment < PARM_BOUNDARY)
    alignment = PARM_BOUNDARY;
  if (alignment > STACK_BOUNDARY)
    alignment = STACK_BOUNDARY;
  return alignment;
}


static bool
xtensa_return_in_msb (tree valtype)
{
  return (TARGET_BIG_ENDIAN
	  && AGGREGATE_TYPE_P (valtype)
	  && int_size_in_bytes (valtype) >= UNITS_PER_WORD);
}


void
override_options (void)
{
  int regno;
  enum machine_mode mode;

  if (!TARGET_BOOLEANS && TARGET_HARD_FLOAT)
    error ("boolean registers required for the floating-point option");

  xtensa_char_to_class['q'] = SP_REG;
  xtensa_char_to_class['a'] = GR_REGS;
  xtensa_char_to_class['b'] = ((TARGET_BOOLEANS) ? BR_REGS : NO_REGS);
  xtensa_char_to_class['f'] = ((TARGET_HARD_FLOAT) ? FP_REGS : NO_REGS);
  xtensa_char_to_class['A'] = ((TARGET_MAC16) ? ACC_REG : NO_REGS);
  xtensa_char_to_class['B'] = ((TARGET_SEXT) ? GR_REGS : NO_REGS);
  xtensa_char_to_class['C'] = ((TARGET_MUL16) ? GR_REGS: NO_REGS);
  xtensa_char_to_class['D'] = ((TARGET_DENSITY) ? GR_REGS: NO_REGS);
  xtensa_char_to_class['d'] = ((TARGET_DENSITY) ? AR_REGS: NO_REGS);
  xtensa_char_to_class['W'] = ((TARGET_CONST16) ? GR_REGS: NO_REGS);

  /* Set up array giving whether a given register can hold a given mode.  */
  for (mode = VOIDmode;
       mode != MAX_MACHINE_MODE;
       mode = (enum machine_mode) ((int) mode + 1))
    {
      int size = GET_MODE_SIZE (mode);
      enum mode_class class = GET_MODE_CLASS (mode);

      for (regno = 0; regno < FIRST_PSEUDO_REGISTER; regno++)
	{
	  int temp;

	  if (ACC_REG_P (regno))
	    temp = (TARGET_MAC16
		    && (class == MODE_INT) && (size <= UNITS_PER_WORD));
	  else if (GP_REG_P (regno))
	    temp = ((regno & 1) == 0 || (size <= UNITS_PER_WORD));
	  else if (FP_REG_P (regno))
	    temp = (TARGET_HARD_FLOAT && (mode == SFmode));
	  else if (BR_REG_P (regno))
	    temp = (TARGET_BOOLEANS && (mode == CCmode));
	  else
	    temp = FALSE;

	  xtensa_hard_regno_mode_ok[(int) mode][regno] = temp;
	}
    }

  init_machine_status = xtensa_init_machine_status;

  /* Check PIC settings.  PIC is only supported when using L32R
     instructions, and some targets need to always use PIC.  */
  if (flag_pic && TARGET_CONST16)
    error ("-f%s is not supported with CONST16 instructions",
	   (flag_pic > 1 ? "PIC" : "pic"));
  else if (XTENSA_ALWAYS_PIC)
    {
      if (TARGET_CONST16)
	error ("PIC is required but not supported with CONST16 instructions");
      flag_pic = 1;
    }
  /* There's no need for -fPIC (as opposed to -fpic) on Xtensa.  */
  if (flag_pic > 1)
    flag_pic = 1;

  /* Hot/cold partitioning does not work on this architecture, because of
     constant pools (the load instruction cannot necessarily reach that far).
     Therefore disable it on this architecture.  */
  if (flag_reorder_blocks_and_partition)
    {
      flag_reorder_blocks_and_partition = 0;
      flag_reorder_blocks = 1;
    }
}


/* A C compound statement to output to stdio stream STREAM the
   assembler syntax for an instruction operand X.  X is an RTL
   expression.

   CODE is a value that can be used to specify one of several ways
   of printing the operand.  It is used when identical operands
   must be printed differently depending on the context.  CODE
   comes from the '%' specification that was used to request
   printing of the operand.  If the specification was just '%DIGIT'
   then CODE is 0; if the specification was '%LTR DIGIT' then CODE
   is the ASCII code for LTR.

   If X is a register, this macro should print the register's name.
   The names can be found in an array 'reg_names' whose type is
   'char *[]'.  'reg_names' is initialized from 'REGISTER_NAMES'.

   When the machine description has a specification '%PUNCT' (a '%'
   followed by a punctuation character), this macro is called with
   a null pointer for X and the punctuation character for CODE.

   'a', 'c', 'l', and 'n' are reserved.

   The Xtensa specific codes are:

   'd'  CONST_INT, print as signed decimal
   'x'  CONST_INT, print as signed hexadecimal
   'K'  CONST_INT, print number of bits in mask for EXTUI
   'R'  CONST_INT, print (X & 0x1f)
   'L'  CONST_INT, print ((32 - X) & 0x1f)
   'D'  REG, print second register of double-word register operand
   'N'  MEM, print address of next word following a memory operand
   'v'  MEM, if memory reference is volatile, output a MEMW before it
   't'  any constant, add "@h" suffix for top 16 bits
   'b'  any constant, add "@l" suffix for bottom 16 bits
*/

static void
printx (FILE *file, signed int val)
{
  /* Print a hexadecimal value in a nice way.  */
  if ((val > -0xa) && (val < 0xa))
    fprintf (file, "%d", val);
  else if (val < 0)
    fprintf (file, "-0x%x", -val);
  else
    fprintf (file, "0x%x", val);
}


void
print_operand (FILE *file, rtx x, int letter)
{
  if (!x)
    error ("PRINT_OPERAND null pointer");

  switch (letter)
    {
    case 'D':
      if (GET_CODE (x) == REG || GET_CODE (x) == SUBREG)
	fprintf (file, "%s", reg_names[xt_true_regnum (x) + 1]);
      else
	output_operand_lossage ("invalid %%D value");
      break;

    case 'v':
      if (GET_CODE (x) == MEM)
	{
	  /* For a volatile memory reference, emit a MEMW before the
	     load or store.  */
	  if (MEM_VOLATILE_P (x))
	    fprintf (file, "memw\n\t");
	}
      else
	output_operand_lossage ("invalid %%v value");
      break;

    case 'N':
      if (GET_CODE (x) == MEM
	  && (GET_MODE (x) == DFmode || GET_MODE (x) == DImode))
	{
	  x = adjust_address (x, GET_MODE (x) == DFmode ? SFmode : SImode, 4);
	  output_address (XEXP (x, 0));
	}
      else
	output_operand_lossage ("invalid %%N value");
      break;

    case 'K':
      if (GET_CODE (x) == CONST_INT)
	{
	  int num_bits = 0;
	  unsigned val = INTVAL (x);
	  while (val & 1)
	    {
	      num_bits += 1;
	      val = val >> 1;
	    }
	  if ((val != 0) || (num_bits == 0) || (num_bits > 16))
	    fatal_insn ("invalid mask", x);

	  fprintf (file, "%d", num_bits);
	}
      else
	output_operand_lossage ("invalid %%K value");
      break;

    case 'L':
      if (GET_CODE (x) == CONST_INT)
	fprintf (file, "%ld", (32 - INTVAL (x)) & 0x1f);
      else
	output_operand_lossage ("invalid %%L value");
      break;

    case 'R':
      if (GET_CODE (x) == CONST_INT)
	fprintf (file, "%ld", INTVAL (x) & 0x1f);
      else
	output_operand_lossage ("invalid %%R value");
      break;

    case 'x':
      if (GET_CODE (x) == CONST_INT)
	printx (file, INTVAL (x));
      else
	output_operand_lossage ("invalid %%x value");
      break;

    case 'd':
      if (GET_CODE (x) == CONST_INT)
	fprintf (file, "%ld", INTVAL (x));
      else
	output_operand_lossage ("invalid %%d value");
      break;

    case 't':
    case 'b':
      if (GET_CODE (x) == CONST_INT)
	{
	  printx (file, INTVAL (x));
	  fputs (letter == 't' ? "@h" : "@l", file);
	}
      else if (GET_CODE (x) == CONST_DOUBLE)
	{
	  REAL_VALUE_TYPE r;
	  REAL_VALUE_FROM_CONST_DOUBLE (r, x);
	  if (GET_MODE (x) == SFmode)
	    {
	      long l;
	      REAL_VALUE_TO_TARGET_SINGLE (r, l);
	      fprintf (file, "0x%08lx@%c", l, letter == 't' ? 'h' : 'l');
	    }
	  else
	    output_operand_lossage ("invalid %%t/%%b value");
	}
      else if (GET_CODE (x) == CONST)
	{
	  /* X must be a symbolic constant on ELF.  Write an expression
	     suitable for 'const16' that sets the high or low 16 bits.  */
	  if (GET_CODE (XEXP (x, 0)) != PLUS
	      || (GET_CODE (XEXP (XEXP (x, 0), 0)) != SYMBOL_REF
		  && GET_CODE (XEXP (XEXP (x, 0), 0)) != LABEL_REF)
	      || GET_CODE (XEXP (XEXP (x, 0), 1)) != CONST_INT)
	    output_operand_lossage ("invalid %%t/%%b value");
	  print_operand (file, XEXP (XEXP (x, 0), 0), 0);
	  fputs (letter == 't' ? "@h" : "@l", file);
	  /* There must be a non-alphanumeric character between 'h' or 'l'
	     and the number.  The '-' is added by print_operand() already.  */
	  if (INTVAL (XEXP (XEXP (x, 0), 1)) >= 0)
	    fputs ("+", file);
	  print_operand (file, XEXP (XEXP (x, 0), 1), 0);
	}
      else
	{
	  output_addr_const (file, x);
	  fputs (letter == 't' ? "@h" : "@l", file);
	}
      break;

    default:
      if (GET_CODE (x) == REG || GET_CODE (x) == SUBREG)
	fprintf (file, "%s", reg_names[xt_true_regnum (x)]);
      else if (GET_CODE (x) == MEM)
	output_address (XEXP (x, 0));
      else if (GET_CODE (x) == CONST_INT)
	fprintf (file, "%ld", INTVAL (x));
      else
	output_addr_const (file, x);
    }
}


/* A C compound statement to output to stdio stream STREAM the
   assembler syntax for an instruction operand that is a memory
   reference whose address is ADDR.  ADDR is an RTL expression.  */

void
print_operand_address (FILE *file, rtx addr)
{
  if (!addr)
    error ("PRINT_OPERAND_ADDRESS, null pointer");

  switch (GET_CODE (addr))
    {
    default:
      fatal_insn ("invalid address", addr);
      break;

    case REG:
      fprintf (file, "%s, 0", reg_names [REGNO (addr)]);
      break;

    case PLUS:
      {
	rtx reg = (rtx)0;
	rtx offset = (rtx)0;
	rtx arg0 = XEXP (addr, 0);
	rtx arg1 = XEXP (addr, 1);

	if (GET_CODE (arg0) == REG)
	  {
	    reg = arg0;
	    offset = arg1;
	  }
	else if (GET_CODE (arg1) == REG)
	  {
	    reg = arg1;
	    offset = arg0;
	  }
	else
	  fatal_insn ("no register in address", addr);

	if (CONSTANT_P (offset))
	  {
	    fprintf (file, "%s, ", reg_names [REGNO (reg)]);
	    output_addr_const (file, offset);
	  }
	else
	  fatal_insn ("address offset not a constant", addr);
      }
      break;

    case LABEL_REF:
    case SYMBOL_REF:
    case CONST_INT:
    case CONST:
      output_addr_const (file, addr);
      break;
    }
}


void
xtensa_output_literal (FILE *file, rtx x, enum machine_mode mode, int labelno)
{
  long value_long[2];
  REAL_VALUE_TYPE r;
  int size;

  fprintf (file, "\t.literal .LC%u, ", (unsigned) labelno);

  switch (GET_MODE_CLASS (mode))
    {
    case MODE_FLOAT:
      gcc_assert (GET_CODE (x) == CONST_DOUBLE);

      REAL_VALUE_FROM_CONST_DOUBLE (r, x);
      switch (mode)
	{
	case SFmode:
	  REAL_VALUE_TO_TARGET_SINGLE (r, value_long[0]);
	  fprintf (file, "0x%08lx\n", value_long[0]);
	  break;

	case DFmode:
	  REAL_VALUE_TO_TARGET_DOUBLE (r, value_long);
	  fprintf (file, "0x%08lx, 0x%08lx\n",
		   value_long[0], value_long[1]);
	  break;

	default:
	  gcc_unreachable ();
	}

      break;

    case MODE_INT:
    case MODE_PARTIAL_INT:
      size = GET_MODE_SIZE (mode);
      switch (size)
	{
	case 4:
	  output_addr_const (file, x);
	  fputs ("\n", file);
	  break;

	case 8:
	  output_addr_const (file, operand_subword (x, 0, 0, DImode));
	  fputs (", ", file);
	  output_addr_const (file, operand_subword (x, 1, 0, DImode));
	  fputs ("\n", file);
	  break;

	default:
	  gcc_unreachable ();
	}
      break;

    default:
      gcc_unreachable ();
    }
}


/* Return the bytes needed to compute the frame pointer from the current
   stack pointer.  */

#define STACK_BYTES (STACK_BOUNDARY / BITS_PER_UNIT)
#define XTENSA_STACK_ALIGN(LOC) (((LOC) + STACK_BYTES-1) & ~(STACK_BYTES-1))

long
compute_frame_size (int size)
{
  /* Add space for the incoming static chain value.  */
  if (cfun->static_chain_decl != NULL)
    size += (1 * UNITS_PER_WORD);

  xtensa_current_frame_size =
    XTENSA_STACK_ALIGN (size
			+ current_function_outgoing_args_size
			+ (WINDOW_SIZE * UNITS_PER_WORD));
  return xtensa_current_frame_size;
}


int
xtensa_frame_pointer_required (void)
{
  /* The code to expand builtin_frame_addr and builtin_return_addr
     currently uses the hard_frame_pointer instead of frame_pointer.
     This seems wrong but maybe it's necessary for other architectures.
     This function is derived from the i386 code.  */

  if (cfun->machine->accesses_prev_frame)
    return 1;

  return 0;
}


void
xtensa_expand_prologue (void)
{
  HOST_WIDE_INT total_size;
  rtx size_rtx;

  total_size = compute_frame_size (get_frame_size ());
  size_rtx = GEN_INT (total_size);

  if (total_size < (1 << (12+3)))
    emit_insn (gen_entry (size_rtx, size_rtx));
  else
    {
      /* Use a8 as a temporary since a0-a7 may be live.  */
      rtx tmp_reg = gen_rtx_REG (Pmode, A8_REG);
      emit_insn (gen_entry (size_rtx, GEN_INT (MIN_FRAME_SIZE)));
      emit_move_insn (tmp_reg, GEN_INT (total_size - MIN_FRAME_SIZE));
      emit_insn (gen_subsi3 (tmp_reg, stack_pointer_rtx, tmp_reg));
      emit_move_insn (stack_pointer_rtx, tmp_reg);
    }

  if (frame_pointer_needed)
    {
      if (cfun->machine->set_frame_ptr_insn)
	{
	  rtx first, insn;

	  push_topmost_sequence ();
	  first = get_insns ();
	  pop_topmost_sequence ();

	  /* For all instructions prior to set_frame_ptr_insn, replace
	     hard_frame_pointer references with stack_pointer.  */
	  for (insn = first;
	       insn != cfun->machine->set_frame_ptr_insn;
	       insn = NEXT_INSN (insn))
	    {
	      if (INSN_P (insn))
		PATTERN (insn) = replace_rtx (copy_rtx (PATTERN (insn)),
					      hard_frame_pointer_rtx,
					      stack_pointer_rtx);
	    }
	}
      else
	emit_move_insn (hard_frame_pointer_rtx, stack_pointer_rtx);
    }
}


/* Clear variables at function end.  */

void
xtensa_function_epilogue (FILE *file ATTRIBUTE_UNUSED,
			  HOST_WIDE_INT size ATTRIBUTE_UNUSED)
{
  xtensa_current_frame_size = 0;
}


rtx
xtensa_return_addr (int count, rtx frame)
{
  rtx result, retaddr;

  if (count == -1)
    retaddr = gen_rtx_REG (Pmode, A0_REG);
  else
    {
      rtx addr = plus_constant (frame, -4 * UNITS_PER_WORD);
      addr = memory_address (Pmode, addr);
      retaddr = gen_reg_rtx (Pmode);
      emit_move_insn (retaddr, gen_rtx_MEM (Pmode, addr));
    }

  /* The 2 most-significant bits of the return address on Xtensa hold
     the register window size.  To get the real return address, these
     bits must be replaced with the high bits from the current PC.  */

  result = gen_reg_rtx (Pmode);
  emit_insn (gen_fix_return_addr (result, retaddr));
  return result;
}


/* Create the va_list data type.

   This structure is set up by __builtin_saveregs.  The __va_reg field
   points to a stack-allocated region holding the contents of the
   incoming argument registers.  The __va_ndx field is an index
   initialized to the position of the first unnamed (variable)
   argument.  This same index is also used to address the arguments
   passed in memory.  Thus, the __va_stk field is initialized to point
   to the position of the first argument in memory offset to account
   for the arguments passed in registers and to account for the size
   of the argument registers not being 16-byte aligned.  E.G., there
   are 6 argument registers of 4 bytes each, but we want the __va_ndx
   for the first stack argument to have the maximal alignment of 16
   bytes, so we offset the __va_stk address by 32 bytes so that
   __va_stk[32] references the first argument on the stack.  */

static tree
xtensa_build_builtin_va_list (void)
{
  tree f_stk, f_reg, f_ndx, record, type_decl;

  record = (*lang_hooks.types.make_type) (RECORD_TYPE);
  type_decl = build_decl (TYPE_DECL, get_identifier ("__va_list_tag"), record);

  f_stk = build_decl (FIELD_DECL, get_identifier ("__va_stk"),
		      ptr_type_node);
  f_reg = build_decl (FIELD_DECL, get_identifier ("__va_reg"),
		      ptr_type_node);
  f_ndx = build_decl (FIELD_DECL, get_identifier ("__va_ndx"),
		      integer_type_node);

  DECL_FIELD_CONTEXT (f_stk) = record;
  DECL_FIELD_CONTEXT (f_reg) = record;
  DECL_FIELD_CONTEXT (f_ndx) = record;

  TREE_CHAIN (record) = type_decl;
  TYPE_NAME (record) = type_decl;
  TYPE_FIELDS (record) = f_stk;
  TREE_CHAIN (f_stk) = f_reg;
  TREE_CHAIN (f_reg) = f_ndx;

  layout_type (record);
  return record;
}


/* Save the incoming argument registers on the stack.  Returns the
   address of the saved registers.  */

static rtx
xtensa_builtin_saveregs (void)
{
  rtx gp_regs, dest;
  int arg_words = current_function_args_info.arg_words;
  int gp_left = MAX_ARGS_IN_REGISTERS - arg_words;

  if (gp_left <= 0)
    return const0_rtx;

  /* Allocate the general-purpose register space.  */
  gp_regs = assign_stack_local
    (BLKmode, MAX_ARGS_IN_REGISTERS * UNITS_PER_WORD, -1);
  set_mem_alias_set (gp_regs, get_varargs_alias_set ());

  /* Now store the incoming registers.  */
  dest = change_address (gp_regs, SImode,
			 plus_constant (XEXP (gp_regs, 0),
					arg_words * UNITS_PER_WORD));
  cfun->machine->need_a7_copy = true;
  cfun->machine->vararg_a7 = true;
  move_block_from_reg (GP_ARG_FIRST + arg_words, dest, gp_left);

  return XEXP (gp_regs, 0);
}


/* Implement `va_start' for varargs and stdarg.  We look at the
   current function to fill in an initial va_list.  */

void
xtensa_va_start (tree valist, rtx nextarg ATTRIBUTE_UNUSED)
{
  tree f_stk, stk;
  tree f_reg, reg;
  tree f_ndx, ndx;
  tree t, u;
  int arg_words;

  arg_words = current_function_args_info.arg_words;

  f_stk = TYPE_FIELDS (va_list_type_node);
  f_reg = TREE_CHAIN (f_stk);
  f_ndx = TREE_CHAIN (f_reg);

  stk = build3 (COMPONENT_REF, TREE_TYPE (f_stk), valist, f_stk, NULL_TREE);
  reg = build3 (COMPONENT_REF, TREE_TYPE (f_reg), valist, f_reg, NULL_TREE);
  ndx = build3 (COMPONENT_REF, TREE_TYPE (f_ndx), valist, f_ndx, NULL_TREE);

  /* Call __builtin_saveregs; save the result in __va_reg */
  u = make_tree (ptr_type_node, expand_builtin_saveregs ());
  t = build2 (MODIFY_EXPR, ptr_type_node, reg, u);
  TREE_SIDE_EFFECTS (t) = 1;
  expand_expr (t, const0_rtx, VOIDmode, EXPAND_NORMAL);

  /* Set the __va_stk member to ($arg_ptr - 32).  */
  u = make_tree (ptr_type_node, virtual_incoming_args_rtx);
  u = fold_build2 (PLUS_EXPR, ptr_type_node, u,
		   build_int_cst (NULL_TREE, -32));
  t = build2 (MODIFY_EXPR, ptr_type_node, stk, u);
  TREE_SIDE_EFFECTS (t) = 1;
  expand_expr (t, const0_rtx, VOIDmode, EXPAND_NORMAL);

  /* Set the __va_ndx member.  If the first variable argument is on
     the stack, adjust __va_ndx by 2 words to account for the extra
     alignment offset for __va_stk.  */
  if (arg_words >= MAX_ARGS_IN_REGISTERS)
    arg_words += 2;
  u = build_int_cst (NULL_TREE, arg_words * UNITS_PER_WORD);
  t = build2 (MODIFY_EXPR, integer_type_node, ndx, u);
  TREE_SIDE_EFFECTS (t) = 1;
  expand_expr (t, const0_rtx, VOIDmode, EXPAND_NORMAL);
}


/* Implement `va_arg'.  */

static tree
xtensa_gimplify_va_arg_expr (tree valist, tree type, tree *pre_p,
			     tree *post_p ATTRIBUTE_UNUSED)
{
  tree f_stk, stk;
  tree f_reg, reg;
  tree f_ndx, ndx;
  tree type_size, array, orig_ndx, addr, size, va_size, t;
  tree lab_false, lab_over, lab_false2;
  bool indirect;

  indirect = pass_by_reference (NULL, TYPE_MODE (type), type, false);
  if (indirect)
    type = build_pointer_type (type);

  /* Handle complex values as separate real and imaginary parts.  */
  if (TREE_CODE (type) == COMPLEX_TYPE)
    {
      tree real_part, imag_part;

      real_part = xtensa_gimplify_va_arg_expr (valist, TREE_TYPE (type),
					       pre_p, NULL);
      real_part = get_initialized_tmp_var (real_part, pre_p, NULL);

      imag_part = xtensa_gimplify_va_arg_expr (valist, TREE_TYPE (type),
					       pre_p, NULL);
      imag_part = get_initialized_tmp_var (imag_part, pre_p, NULL);

      return build2 (COMPLEX_EXPR, type, real_part, imag_part);
    }

  f_stk = TYPE_FIELDS (va_list_type_node);
  f_reg = TREE_CHAIN (f_stk);
  f_ndx = TREE_CHAIN (f_reg);

  stk = build3 (COMPONENT_REF, TREE_TYPE (f_stk), valist, f_stk, NULL_TREE);
  reg = build3 (COMPONENT_REF, TREE_TYPE (f_reg), valist, f_reg, NULL_TREE);
  ndx = build3 (COMPONENT_REF, TREE_TYPE (f_ndx), valist, f_ndx, NULL_TREE);

  type_size = size_in_bytes (type);
  va_size = round_up (type_size, UNITS_PER_WORD);
  gimplify_expr (&va_size, pre_p, NULL, is_gimple_val, fb_rvalue);


  /* First align __va_ndx if necessary for this arg:

     orig_ndx = (AP).__va_ndx;
     if (__alignof__ (TYPE) > 4 )
       orig_ndx = ((orig_ndx + __alignof__ (TYPE) - 1)
			& -__alignof__ (TYPE)); */

  orig_ndx = get_initialized_tmp_var (ndx, pre_p, NULL);

  if (TYPE_ALIGN (type) > BITS_PER_WORD)
    {
      int align = MIN (TYPE_ALIGN (type), STACK_BOUNDARY) / BITS_PER_UNIT;

      t = build2 (PLUS_EXPR, integer_type_node, orig_ndx,
		  build_int_cst (NULL_TREE, align - 1));
      t = build2 (BIT_AND_EXPR, integer_type_node, t,
		  build_int_cst (NULL_TREE, -align));
      t = build2 (MODIFY_EXPR, integer_type_node, orig_ndx, t);
      gimplify_and_add (t, pre_p);
    }


  /* Increment __va_ndx to point past the argument:

     (AP).__va_ndx = orig_ndx + __va_size (TYPE); */

  t = fold_convert (integer_type_node, va_size);
  t = build2 (PLUS_EXPR, integer_type_node, orig_ndx, t);
  t = build2 (MODIFY_EXPR, integer_type_node, ndx, t);
  gimplify_and_add (t, pre_p);


  /* Check if the argument is in registers:

     if ((AP).__va_ndx <= __MAX_ARGS_IN_REGISTERS * 4
         && !must_pass_in_stack (type))
        __array = (AP).__va_reg; */

  array = create_tmp_var (ptr_type_node, NULL);

  lab_over = NULL;
  if (!targetm.calls.must_pass_in_stack (TYPE_MODE (type), type))
    {
      lab_false = create_artificial_label ();
      lab_over = create_artificial_label ();

      t = build_int_cst (NULL_TREE, MAX_ARGS_IN_REGISTERS * UNITS_PER_WORD);
      t = build2 (GT_EXPR, boolean_type_node, ndx, t);
      t = build3 (COND_EXPR, void_type_node, t,
		  build1 (GOTO_EXPR, void_type_node, lab_false),
		  NULL_TREE);
      gimplify_and_add (t, pre_p);

      t = build2 (MODIFY_EXPR, void_type_node, array, reg);
      gimplify_and_add (t, pre_p);

      t = build1 (GOTO_EXPR, void_type_node, lab_over);
      gimplify_and_add (t, pre_p);

      t = build1 (LABEL_EXPR, void_type_node, lab_false);
      gimplify_and_add (t, pre_p);
    }


  /* ...otherwise, the argument is on the stack (never split between
     registers and the stack -- change __va_ndx if necessary):

     else
       {
	 if (orig_ndx <= __MAX_ARGS_IN_REGISTERS * 4)
	     (AP).__va_ndx = 32 + __va_size (TYPE);
	 __array = (AP).__va_stk;
       } */

  lab_false2 = create_artificial_label ();

  t = build_int_cst (NULL_TREE, MAX_ARGS_IN_REGISTERS * UNITS_PER_WORD);
  t = build2 (GT_EXPR, boolean_type_node, orig_ndx, t);
  t = build3 (COND_EXPR, void_type_node, t,
	      build1 (GOTO_EXPR, void_type_node, lab_false2),
	      NULL_TREE);
  gimplify_and_add (t, pre_p);

  t = size_binop (PLUS_EXPR, va_size, size_int (32));
  t = fold_convert (integer_type_node, t);
  t = build2 (MODIFY_EXPR, integer_type_node, ndx, t);
  gimplify_and_add (t, pre_p);

  t = build1 (LABEL_EXPR, void_type_node, lab_false2);
  gimplify_and_add (t, pre_p);

  t = build2 (MODIFY_EXPR, void_type_node, array, stk);
  gimplify_and_add (t, pre_p);

  if (lab_over)
    {
      t = build1 (LABEL_EXPR, void_type_node, lab_over);
      gimplify_and_add (t, pre_p);
    }


  /* Given the base array pointer (__array) and index to the subsequent
     argument (__va_ndx), find the address:

     __array + (AP).__va_ndx - (BYTES_BIG_ENDIAN && sizeof (TYPE) < 4
				? sizeof (TYPE)
				: __va_size (TYPE))

     The results are endian-dependent because values smaller than one word
     are aligned differently.  */


  if (BYTES_BIG_ENDIAN && TREE_CODE (type_size) == INTEGER_CST)
    {
      t = size_int (PARM_BOUNDARY / BITS_PER_UNIT);
      t = fold_build2 (GE_EXPR, boolean_type_node, type_size, t);
      t = fold_build3 (COND_EXPR, sizetype, t, va_size, type_size);
      size = t;
    }
  else
    size = va_size;

  t = fold_convert (ptr_type_node, ndx);
  addr = build2 (PLUS_EXPR, ptr_type_node, array, t);
  t = fold_convert (ptr_type_node, size);
  addr = build2 (MINUS_EXPR, ptr_type_node, addr, t);

  addr = fold_convert (build_pointer_type (type), addr);
  if (indirect)
    addr = build_va_arg_indirect_ref (addr);
  return build_va_arg_indirect_ref (addr);
}


enum reg_class
xtensa_preferred_reload_class (rtx x, enum reg_class class, int isoutput)
{
  if (!isoutput && CONSTANT_P (x) && GET_CODE (x) == CONST_DOUBLE)
    return NO_REGS;

  /* Don't use the stack pointer or hard frame pointer for reloads!
     The hard frame pointer would normally be OK except that it may
     briefly hold an incoming argument in the prologue, and reload
     won't know that it is live because the hard frame pointer is
     treated specially.  */

  if (class == AR_REGS || class == GR_REGS)
    return RL_REGS;

  return class;
}


enum reg_class
xtensa_secondary_reload_class (enum reg_class class,
			       enum machine_mode mode ATTRIBUTE_UNUSED,
			       rtx x, int isoutput)
{
  int regno;

  if (GET_CODE (x) == SIGN_EXTEND)
    x = XEXP (x, 0);
  regno = xt_true_regnum (x);

  if (!isoutput)
    {
      if (class == FP_REGS && constantpool_mem_p (x))
	return RL_REGS;
    }

  if (ACC_REG_P (regno))
    return ((class == GR_REGS || class == RL_REGS) ? NO_REGS : RL_REGS);
  if (class == ACC_REG)
    return (GP_REG_P (regno) ? NO_REGS : RL_REGS);

  return NO_REGS;
}


void
order_regs_for_local_alloc (void)
{
  if (!leaf_function_p ())
    {
      memcpy (reg_alloc_order, reg_nonleaf_alloc_order,
	      FIRST_PSEUDO_REGISTER * sizeof (int));
    }
  else
    {
      int i, num_arg_regs;
      int nxt = 0;

      /* Use the AR registers in increasing order (skipping a0 and a1)
	 but save the incoming argument registers for a last resort.  */
      num_arg_regs = current_function_args_info.arg_words;
      if (num_arg_regs > MAX_ARGS_IN_REGISTERS)
	num_arg_regs = MAX_ARGS_IN_REGISTERS;
      for (i = GP_ARG_FIRST; i < 16 - num_arg_regs; i++)
	reg_alloc_order[nxt++] = i + num_arg_regs;
      for (i = 0; i < num_arg_regs; i++)
	reg_alloc_order[nxt++] = GP_ARG_FIRST + i;

      /* List the coprocessor registers in order.  */
      for (i = 0; i < BR_REG_NUM; i++)
	reg_alloc_order[nxt++] = BR_REG_FIRST + i;

      /* List the FP registers in order for now.  */
      for (i = 0; i < 16; i++)
	reg_alloc_order[nxt++] = FP_REG_FIRST + i;

      /* GCC requires that we list *all* the registers....  */
      reg_alloc_order[nxt++] = 0;	/* a0 = return address */
      reg_alloc_order[nxt++] = 1;	/* a1 = stack pointer */
      reg_alloc_order[nxt++] = 16;	/* pseudo frame pointer */
      reg_alloc_order[nxt++] = 17;	/* pseudo arg pointer */

      reg_alloc_order[nxt++] = ACC_REG_FIRST;	/* MAC16 accumulator */
    }
}


/* Some Xtensa targets support multiple bss sections.  If the section
   name ends with ".bss", add SECTION_BSS to the flags.  */

static unsigned int
xtensa_multibss_section_type_flags (tree decl, const char *name, int reloc)
{
  unsigned int flags = default_section_type_flags (decl, name, reloc);
  const char *suffix;

  suffix = strrchr (name, '.');
  if (suffix && strcmp (suffix, ".bss") == 0)
    {
      if (!decl || (TREE_CODE (decl) == VAR_DECL
		    && DECL_INITIAL (decl) == NULL_TREE))
	flags |= SECTION_BSS;  /* @nobits */
      else
	warning (0, "only uninitialized variables can be placed in a "
		 ".bss section");
    }

  return flags;
}


/* The literal pool stays with the function.  */

static section *
xtensa_select_rtx_section (enum machine_mode mode ATTRIBUTE_UNUSED,
			   rtx x ATTRIBUTE_UNUSED,
			   unsigned HOST_WIDE_INT align ATTRIBUTE_UNUSED)
{
  return function_section (current_function_decl);
}


/* Compute a (partial) cost for rtx X.  Return true if the complete
   cost has been computed, and false if subexpressions should be
   scanned.  In either case, *TOTAL contains the cost result.  */

static bool
xtensa_rtx_costs (rtx x, int code, int outer_code, int *total)
{
  switch (code)
    {
    case CONST_INT:
      switch (outer_code)
	{
	case SET:
	  if (xtensa_simm12b (INTVAL (x)))
	    {
	      *total = 4;
	      return true;
	    }
	  break;
	case PLUS:
	  if (xtensa_simm8 (INTVAL (x))
	      || xtensa_simm8x256 (INTVAL (x)))
	    {
	      *total = 0;
	      return true;
	    }
	  break;
	case AND:
	  if (xtensa_mask_immediate (INTVAL (x)))
	    {
	      *total = 0;
	      return true;
	    }
	  break;
	case COMPARE:
	  if ((INTVAL (x) == 0) || xtensa_b4const (INTVAL (x)))
	    {
	      *total = 0;
	      return true;
	    }
	  break;
	case ASHIFT:
	case ASHIFTRT:
	case LSHIFTRT:
	case ROTATE:
	case ROTATERT:
	  /* No way to tell if X is the 2nd operand so be conservative.  */
	default: break;
	}
      if (xtensa_simm12b (INTVAL (x)))
	*total = 5;
      else if (TARGET_CONST16)
	*total = COSTS_N_INSNS (2);
      else
	*total = 6;
      return true;

    case CONST:
    case LABEL_REF:
    case SYMBOL_REF:
      if (TARGET_CONST16)
	*total = COSTS_N_INSNS (2);
      else
	*total = 5;
      return true;

    case CONST_DOUBLE:
      if (TARGET_CONST16)
	*total = COSTS_N_INSNS (4);
      else
	*total = 7;
      return true;

    case MEM:
      {
	int num_words =
	  (GET_MODE_SIZE (GET_MODE (x)) > UNITS_PER_WORD) ?  2 : 1;

	if (memory_address_p (GET_MODE (x), XEXP ((x), 0)))
	  *total = COSTS_N_INSNS (num_words);
	else
	  *total = COSTS_N_INSNS (2*num_words);
	return true;
      }

    case FFS:
      *total = COSTS_N_INSNS (TARGET_NSA ? 5 : 50);
      return true;

    case NOT:
      *total = COSTS_N_INSNS ((GET_MODE (x) == DImode) ? 3 : 2);
      return true;

    case AND:
    case IOR:
    case XOR:
      if (GET_MODE (x) == DImode)
	*total = COSTS_N_INSNS (2);
      else
	*total = COSTS_N_INSNS (1);
      return true;

    case ASHIFT:
    case ASHIFTRT:
    case LSHIFTRT:
      if (GET_MODE (x) == DImode)
	*total = COSTS_N_INSNS (50);
      else
	*total = COSTS_N_INSNS (1);
      return true;

    case ABS:
      {
	enum machine_mode xmode = GET_MODE (x);
	if (xmode == SFmode)
	  *total = COSTS_N_INSNS (TARGET_HARD_FLOAT ? 1 : 50);
	else if (xmode == DFmode)
	  *total = COSTS_N_INSNS (50);
	else
	  *total = COSTS_N_INSNS (4);
	return true;
      }

    case PLUS:
    case MINUS:
      {
	enum machine_mode xmode = GET_MODE (x);
	if (xmode == SFmode)
	  *total = COSTS_N_INSNS (TARGET_HARD_FLOAT ? 1 : 50);
	else if (xmode == DFmode || xmode == DImode)
	  *total = COSTS_N_INSNS (50);
	else
	  *total = COSTS_N_INSNS (1);
	return true;
      }

    case NEG:
      *total = COSTS_N_INSNS ((GET_MODE (x) == DImode) ? 4 : 2);
      return true;

    case MULT:
      {
	enum machine_mode xmode = GET_MODE (x);
	if (xmode == SFmode)
	  *total = COSTS_N_INSNS (TARGET_HARD_FLOAT ? 4 : 50);
	else if (xmode == DFmode || xmode == DImode)
	  *total = COSTS_N_INSNS (50);
	else if (TARGET_MUL32)
	  *total = COSTS_N_INSNS (4);
	else if (TARGET_MAC16)
	  *total = COSTS_N_INSNS (16);
	else if (TARGET_MUL16)
	  *total = COSTS_N_INSNS (12);
	else
	  *total = COSTS_N_INSNS (50);
	return true;
      }

    case DIV:
    case MOD:
      {
	enum machine_mode xmode = GET_MODE (x);
	if (xmode == SFmode)
	  {
	    *total = COSTS_N_INSNS (TARGET_HARD_FLOAT_DIV ? 8 : 50);
	    return true;
	  }
	else if (xmode == DFmode)
	  {
	    *total = COSTS_N_INSNS (50);
	    return true;
	  }
      }
      /* Fall through.  */

    case UDIV:
    case UMOD:
      {
	enum machine_mode xmode = GET_MODE (x);
	if (xmode == DImode)
	  *total = COSTS_N_INSNS (50);
	else if (TARGET_DIV32)
	  *total = COSTS_N_INSNS (32);
	else
	  *total = COSTS_N_INSNS (50);
	return true;
      }

    case SQRT:
      if (GET_MODE (x) == SFmode)
	*total = COSTS_N_INSNS (TARGET_HARD_FLOAT_SQRT ? 8 : 50);
      else
	*total = COSTS_N_INSNS (50);
      return true;

    case SMIN:
    case UMIN:
    case SMAX:
    case UMAX:
      *total = COSTS_N_INSNS (TARGET_MINMAX ? 1 : 50);
      return true;

    case SIGN_EXTRACT:
    case SIGN_EXTEND:
      *total = COSTS_N_INSNS (TARGET_SEXT ? 1 : 2);
      return true;

    case ZERO_EXTRACT:
    case ZERO_EXTEND:
      *total = COSTS_N_INSNS (1);
      return true;

    default:
      return false;
    }
}

/* Worker function for TARGET_RETURN_IN_MEMORY.  */

static bool
xtensa_return_in_memory (tree type, tree fntype ATTRIBUTE_UNUSED)
{
  return ((unsigned HOST_WIDE_INT) int_size_in_bytes (type)
	  > 4 * UNITS_PER_WORD);
}

#include "gt-xtensa.h"
