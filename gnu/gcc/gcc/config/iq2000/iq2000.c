/* Subroutines used for code generation on Vitesse IQ2000 processors
   Copyright (C) 2003, 2004, 2005 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include <signal.h>
#include "tm.h"
#include "tree.h"
#include "rtl.h"
#include "regs.h"
#include "hard-reg-set.h"
#include "real.h"
#include "insn-config.h"
#include "conditions.h"
#include "output.h"
#include "insn-attr.h"
#include "flags.h"
#include "function.h"
#include "expr.h"
#include "optabs.h"
#include "libfuncs.h"
#include "recog.h"
#include "toplev.h"
#include "reload.h"
#include "ggc.h"
#include "tm_p.h"
#include "debug.h"
#include "target.h"
#include "target-def.h"
#include "langhooks.h"

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

struct constant;


/* Structure to be filled in by compute_frame_size with register
   save masks, and offsets for the current function.  */

struct iq2000_frame_info
{
  long total_size;		/* # bytes that the entire frame takes up.  */
  long var_size;		/* # bytes that variables take up.  */
  long args_size;		/* # bytes that outgoing arguments take up.  */
  long extra_size;		/* # bytes of extra gunk.  */
  int  gp_reg_size;		/* # bytes needed to store gp regs.  */
  int  fp_reg_size;		/* # bytes needed to store fp regs.  */
  long mask;			/* Mask of saved gp registers.  */
  long gp_save_offset;		/* Offset from vfp to store gp registers.  */
  long fp_save_offset;		/* Offset from vfp to store fp registers.  */
  long gp_sp_offset;		/* Offset from new sp to store gp registers.  */
  long fp_sp_offset;		/* Offset from new sp to store fp registers.  */
  int  initialized;		/* != 0 if frame size already calculated.  */
  int  num_gp;			/* Number of gp registers saved.  */
} iq2000_frame_info;

struct machine_function GTY(())
{
  /* Current frame information, calculated by compute_frame_size.  */
  long total_size;		/* # bytes that the entire frame takes up.  */
  long var_size;		/* # bytes that variables take up.  */
  long args_size;		/* # bytes that outgoing arguments take up.  */
  long extra_size;		/* # bytes of extra gunk.  */
  int  gp_reg_size;		/* # bytes needed to store gp regs.  */
  int  fp_reg_size;		/* # bytes needed to store fp regs.  */
  long mask;			/* Mask of saved gp registers.  */
  long gp_save_offset;		/* Offset from vfp to store gp registers.  */
  long fp_save_offset;		/* Offset from vfp to store fp registers.  */
  long gp_sp_offset;		/* Offset from new sp to store gp registers.  */
  long fp_sp_offset;		/* Offset from new sp to store fp registers.  */
  int  initialized;		/* != 0 if frame size already calculated.  */
  int  num_gp;			/* Number of gp registers saved.  */
};

/* Global variables for machine-dependent things.  */

/* List of all IQ2000 punctuation characters used by print_operand.  */
char iq2000_print_operand_punct[256];

/* The target cpu for optimization and scheduling.  */
enum processor_type iq2000_tune;

/* Which instruction set architecture to use.  */
int iq2000_isa;

/* Cached operands, and operator to compare for use in set/branch/trap
   on condition codes.  */
rtx branch_cmp[2];

/* What type of branch to use.  */
enum cmp_type branch_type;

/* Local variables.  */

/* The next branch instruction is a branch likely, not branch normal.  */
static int iq2000_branch_likely;

/* Count of delay slots and how many are filled.  */
static int dslots_load_total;
static int dslots_load_filled;
static int dslots_jump_total;

/* # of nops needed by previous insn.  */
static int dslots_number_nops;

/* Number of 1/2/3 word references to data items (i.e., not jal's).  */
static int num_refs[3];

/* Registers to check for load delay.  */
static rtx iq2000_load_reg;
static rtx iq2000_load_reg2;
static rtx iq2000_load_reg3;
static rtx iq2000_load_reg4;

/* Mode used for saving/restoring general purpose registers.  */
static enum machine_mode gpr_mode;


/* Initialize the GCC target structure.  */
static struct machine_function* iq2000_init_machine_status (void);
static bool iq2000_handle_option      (size_t, const char *, int);
static section *iq2000_select_rtx_section (enum machine_mode, rtx,
					   unsigned HOST_WIDE_INT);
static void iq2000_init_builtins      (void);
static rtx  iq2000_expand_builtin     (tree, rtx, rtx, enum machine_mode, int);
static bool iq2000_return_in_memory   (tree, tree);
static void iq2000_setup_incoming_varargs (CUMULATIVE_ARGS *,
					   enum machine_mode, tree, int *,
					   int);
static bool iq2000_rtx_costs          (rtx, int, int, int *);
static int  iq2000_address_cost       (rtx);
static section *iq2000_select_section (tree, int, unsigned HOST_WIDE_INT);
static bool iq2000_return_in_memory   (tree, tree);
static bool iq2000_pass_by_reference  (CUMULATIVE_ARGS *, enum machine_mode,
				       tree, bool);
static int  iq2000_arg_partial_bytes  (CUMULATIVE_ARGS *, enum machine_mode,
				       tree, bool);

#undef  TARGET_INIT_BUILTINS
#define TARGET_INIT_BUILTINS 		iq2000_init_builtins
#undef  TARGET_EXPAND_BUILTIN
#define TARGET_EXPAND_BUILTIN 		iq2000_expand_builtin
#undef  TARGET_ASM_SELECT_RTX_SECTION
#define TARGET_ASM_SELECT_RTX_SECTION	iq2000_select_rtx_section
#undef  TARGET_HANDLE_OPTION
#define TARGET_HANDLE_OPTION		iq2000_handle_option
#undef  TARGET_RTX_COSTS
#define TARGET_RTX_COSTS		iq2000_rtx_costs
#undef  TARGET_ADDRESS_COST
#define TARGET_ADDRESS_COST		iq2000_address_cost
#undef  TARGET_ASM_SELECT_SECTION
#define TARGET_ASM_SELECT_SECTION	iq2000_select_section

/* The assembler supports switchable .bss sections, but
   iq2000_select_section doesn't yet make use of them.  */
#undef  TARGET_HAVE_SWITCHABLE_BSS_SECTIONS
#define TARGET_HAVE_SWITCHABLE_BSS_SECTIONS false

#undef  TARGET_PROMOTE_FUNCTION_ARGS
#define TARGET_PROMOTE_FUNCTION_ARGS	hook_bool_tree_true
#undef  TARGET_PROMOTE_FUNCTION_RETURN
#define TARGET_PROMOTE_FUNCTION_RETURN	hook_bool_tree_true
#undef  TARGET_PROMOTE_PROTOTYPES
#define TARGET_PROMOTE_PROTOTYPES	hook_bool_tree_true

#undef  TARGET_RETURN_IN_MEMORY
#define TARGET_RETURN_IN_MEMORY		iq2000_return_in_memory
#undef  TARGET_PASS_BY_REFERENCE
#define TARGET_PASS_BY_REFERENCE	iq2000_pass_by_reference
#undef  TARGET_CALLEE_COPIES
#define TARGET_CALLEE_COPIES		hook_callee_copies_named
#undef  TARGET_ARG_PARTIAL_BYTES
#define TARGET_ARG_PARTIAL_BYTES	iq2000_arg_partial_bytes

#undef  TARGET_SETUP_INCOMING_VARARGS
#define TARGET_SETUP_INCOMING_VARARGS	iq2000_setup_incoming_varargs
#undef  TARGET_STRICT_ARGUMENT_NAMING
#define TARGET_STRICT_ARGUMENT_NAMING	hook_bool_CUMULATIVE_ARGS_true

struct gcc_target targetm = TARGET_INITIALIZER;

/* Return nonzero if we split the address into high and low parts.  */

int
iq2000_check_split (rtx address, enum machine_mode mode)
{
  /* This is the same check used in simple_memory_operand.
     We use it here because LO_SUM is not offsettable.  */
  if (GET_MODE_SIZE (mode) > (unsigned) UNITS_PER_WORD)
    return 0;

  if ((GET_CODE (address) == SYMBOL_REF)
      || (GET_CODE (address) == CONST
	  && GET_CODE (XEXP (XEXP (address, 0), 0)) == SYMBOL_REF)
      || GET_CODE (address) == LABEL_REF)
    return 1;

  return 0;
}

/* Return nonzero if REG is valid for MODE.  */

int
iq2000_reg_mode_ok_for_base_p (rtx reg,
			       enum machine_mode mode ATTRIBUTE_UNUSED,
			       int strict)
{
  return (strict
	  ? REGNO_MODE_OK_FOR_BASE_P (REGNO (reg), mode)
	  : GP_REG_OR_PSEUDO_NONSTRICT_P (REGNO (reg), mode));
}

/* Return a nonzero value if XINSN is a legitimate address for a
   memory operand of the indicated MODE.  STRICT is nonzero if this
   function is called during reload.  */

int
iq2000_legitimate_address_p (enum machine_mode mode, rtx xinsn, int strict)
{
  if (TARGET_DEBUG_A_MODE)
    {
      GO_PRINTF2 ("\n========== GO_IF_LEGITIMATE_ADDRESS, %sstrict\n",
		  strict ? "" : "not ");
      GO_DEBUG_RTX (xinsn);
    }

  /* Check for constant before stripping off SUBREG, so that we don't
     accept (subreg (const_int)) which will fail to reload.  */
  if (CONSTANT_ADDRESS_P (xinsn)
      && ! (iq2000_check_split (xinsn, mode))
      && ! (GET_CODE (xinsn) == CONST_INT && ! SMALL_INT (xinsn)))
    return 1;

  while (GET_CODE (xinsn) == SUBREG)
    xinsn = SUBREG_REG (xinsn);

  if (GET_CODE (xinsn) == REG
      && iq2000_reg_mode_ok_for_base_p (xinsn, mode, strict))
    return 1;

  if (GET_CODE (xinsn) == LO_SUM)
    {
      rtx xlow0 = XEXP (xinsn, 0);
      rtx xlow1 = XEXP (xinsn, 1);

      while (GET_CODE (xlow0) == SUBREG)
	xlow0 = SUBREG_REG (xlow0);
      if (GET_CODE (xlow0) == REG
	  && iq2000_reg_mode_ok_for_base_p (xlow0, mode, strict)
	  && iq2000_check_split (xlow1, mode))
	return 1;
    }

  if (GET_CODE (xinsn) == PLUS)
    {
      rtx xplus0 = XEXP (xinsn, 0);
      rtx xplus1 = XEXP (xinsn, 1);
      enum rtx_code code0;
      enum rtx_code code1;

      while (GET_CODE (xplus0) == SUBREG)
	xplus0 = SUBREG_REG (xplus0);
      code0 = GET_CODE (xplus0);

      while (GET_CODE (xplus1) == SUBREG)
	xplus1 = SUBREG_REG (xplus1);
      code1 = GET_CODE (xplus1);

      if (code0 == REG
	  && iq2000_reg_mode_ok_for_base_p (xplus0, mode, strict))
	{
	  if (code1 == CONST_INT && SMALL_INT (xplus1)
	      && SMALL_INT_UNSIGNED (xplus1) /* No negative offsets */)
	    return 1;
	}
    }

  if (TARGET_DEBUG_A_MODE)
    GO_PRINTF ("Not a legitimate address\n");

  /* The address was not legitimate.  */
  return 0;
}

/* Returns an operand string for the given instruction's delay slot,
   after updating filled delay slot statistics.

   We assume that operands[0] is the target register that is set.

   In order to check the next insn, most of this functionality is moved
   to FINAL_PRESCAN_INSN, and we just set the global variables that
   it needs.  */

const char *
iq2000_fill_delay_slot (const char *ret, enum delay_type type, rtx operands[],
			rtx cur_insn)
{
  rtx set_reg;
  enum machine_mode mode;
  rtx next_insn = cur_insn ? NEXT_INSN (cur_insn) : NULL_RTX;
  int num_nops;

  if (type == DELAY_LOAD || type == DELAY_FCMP)
    num_nops = 1;

  else
    num_nops = 0;

  /* Make sure that we don't put nop's after labels.  */
  next_insn = NEXT_INSN (cur_insn);
  while (next_insn != 0
	 && (GET_CODE (next_insn) == NOTE
	     || GET_CODE (next_insn) == CODE_LABEL))
    next_insn = NEXT_INSN (next_insn);

  dslots_load_total += num_nops;
  if (TARGET_DEBUG_C_MODE
      || type == DELAY_NONE
      || operands == 0
      || cur_insn == 0
      || next_insn == 0
      || GET_CODE (next_insn) == CODE_LABEL
      || (set_reg = operands[0]) == 0)
    {
      dslots_number_nops = 0;
      iq2000_load_reg  = 0;
      iq2000_load_reg2 = 0;
      iq2000_load_reg3 = 0;
      iq2000_load_reg4 = 0;

      return ret;
    }

  set_reg = operands[0];
  if (set_reg == 0)
    return ret;

  while (GET_CODE (set_reg) == SUBREG)
    set_reg = SUBREG_REG (set_reg);

  mode = GET_MODE (set_reg);
  dslots_number_nops = num_nops;
  iq2000_load_reg = set_reg;
  if (GET_MODE_SIZE (mode)
      > (unsigned) (UNITS_PER_WORD))
    iq2000_load_reg2 = gen_rtx_REG (SImode, REGNO (set_reg) + 1);
  else
    iq2000_load_reg2 = 0;

  return ret;
}

/* Determine whether a memory reference takes one (based off of the GP
   pointer), two (normal), or three (label + reg) instructions, and bump the
   appropriate counter for -mstats.  */

static void
iq2000_count_memory_refs (rtx op, int num)
{
  int additional = 0;
  int n_words = 0;
  rtx addr, plus0, plus1;
  enum rtx_code code0, code1;
  int looping;

  if (TARGET_DEBUG_B_MODE)
    {
      fprintf (stderr, "\n========== iq2000_count_memory_refs:\n");
      debug_rtx (op);
    }

  /* Skip MEM if passed, otherwise handle movsi of address.  */
  addr = (GET_CODE (op) != MEM) ? op : XEXP (op, 0);

  /* Loop, going through the address RTL.  */
  do
    {
      looping = FALSE;
      switch (GET_CODE (addr))
	{
	case REG:
	case CONST_INT:
	case LO_SUM:
	  break;

	case PLUS:
	  plus0 = XEXP (addr, 0);
	  plus1 = XEXP (addr, 1);
	  code0 = GET_CODE (plus0);
	  code1 = GET_CODE (plus1);

	  if (code0 == REG)
	    {
	      additional++;
	      addr = plus1;
	      looping = 1;
	      continue;
	    }

	  if (code0 == CONST_INT)
	    {
	      addr = plus1;
	      looping = 1;
	      continue;
	    }

	  if (code1 == REG)
	    {
	      additional++;
	      addr = plus0;
	      looping = 1;
	      continue;
	    }

	  if (code1 == CONST_INT)
	    {
	      addr = plus0;
	      looping = 1;
	      continue;
	    }

	  if (code0 == SYMBOL_REF || code0 == LABEL_REF || code0 == CONST)
	    {
	      addr = plus0;
	      looping = 1;
	      continue;
	    }

	  if (code1 == SYMBOL_REF || code1 == LABEL_REF || code1 == CONST)
	    {
	      addr = plus1;
	      looping = 1;
	      continue;
	    }

	  break;

	case LABEL_REF:
	  n_words = 2;		/* Always 2 words.  */
	  break;

	case CONST:
	  addr = XEXP (addr, 0);
	  looping = 1;
	  continue;

	case SYMBOL_REF:
	  n_words = SYMBOL_REF_FLAG (addr) ? 1 : 2;
	  break;

	default:
	  break;
	}
    }
  while (looping);

  if (n_words == 0)
    return;

  n_words += additional;
  if (n_words > 3)
    n_words = 3;

  num_refs[n_words-1] += num;
}

/* Abort after printing out a specific insn.  */

static void
abort_with_insn (rtx insn, const char * reason)
{
  error (reason);
  debug_rtx (insn);
  fancy_abort (__FILE__, __LINE__, __FUNCTION__);
}

/* Return the appropriate instructions to move one operand to another.  */

const char *
iq2000_move_1word (rtx operands[], rtx insn, int unsignedp)
{
  const char *ret = 0;
  rtx op0 = operands[0];
  rtx op1 = operands[1];
  enum rtx_code code0 = GET_CODE (op0);
  enum rtx_code code1 = GET_CODE (op1);
  enum machine_mode mode = GET_MODE (op0);
  int subreg_offset0 = 0;
  int subreg_offset1 = 0;
  enum delay_type delay = DELAY_NONE;

  while (code0 == SUBREG)
    {
      subreg_offset0 += subreg_regno_offset (REGNO (SUBREG_REG (op0)),
					     GET_MODE (SUBREG_REG (op0)),
					     SUBREG_BYTE (op0),
					     GET_MODE (op0));
      op0 = SUBREG_REG (op0);
      code0 = GET_CODE (op0);
    }

  while (code1 == SUBREG)
    {
      subreg_offset1 += subreg_regno_offset (REGNO (SUBREG_REG (op1)),
					     GET_MODE (SUBREG_REG (op1)),
					     SUBREG_BYTE (op1),
					     GET_MODE (op1));
      op1 = SUBREG_REG (op1);
      code1 = GET_CODE (op1);
    }

  /* For our purposes, a condition code mode is the same as SImode.  */
  if (mode == CCmode)
    mode = SImode;

  if (code0 == REG)
    {
      int regno0 = REGNO (op0) + subreg_offset0;

      if (code1 == REG)
	{
	  int regno1 = REGNO (op1) + subreg_offset1;

	  /* Do not do anything for assigning a register to itself */
	  if (regno0 == regno1)
	    ret = "";

	  else if (GP_REG_P (regno0))
	    {
	      if (GP_REG_P (regno1))
		ret = "or\t%0,%%0,%1";
	    }

	}

      else if (code1 == MEM)
	{
	  delay = DELAY_LOAD;

	  if (TARGET_STATS)
	    iq2000_count_memory_refs (op1, 1);

	  if (GP_REG_P (regno0))
	    {
	      /* For loads, use the mode of the memory item, instead of the
		 target, so zero/sign extend can use this code as well.  */
	      switch (GET_MODE (op1))
		{
		default:
		  break;
		case SFmode:
		  ret = "lw\t%0,%1";
		  break;
		case SImode:
		case CCmode:
		  ret = "lw\t%0,%1";
		  break;
		case HImode:
		  ret = (unsignedp) ? "lhu\t%0,%1" : "lh\t%0,%1";
		  break;
		case QImode:
		  ret = (unsignedp) ? "lbu\t%0,%1" : "lb\t%0,%1";
		  break;
		}
	    }
	}

      else if (code1 == CONST_INT
	       || (code1 == CONST_DOUBLE
		   && GET_MODE (op1) == VOIDmode))
	{
	  if (code1 == CONST_DOUBLE)
	    {
	      /* This can happen when storing constants into long long
                 bitfields.  Just store the least significant word of
                 the value.  */
	      operands[1] = op1 = GEN_INT (CONST_DOUBLE_LOW (op1));
	    }

	  if (INTVAL (op1) == 0)
	    {
	      if (GP_REG_P (regno0))
		ret = "or\t%0,%%0,%z1";
	    }
	 else if (GP_REG_P (regno0))
	    {
	      if (SMALL_INT_UNSIGNED (op1))
		ret = "ori\t%0,%%0,%x1\t\t\t# %1";
	      else if (SMALL_INT (op1))
		ret = "addiu\t%0,%%0,%1\t\t\t# %1";
	      else
		ret = "lui\t%0,%X1\t\t\t# %1\n\tori\t%0,%0,%x1";
	    }
	}

      else if (code1 == CONST_DOUBLE && mode == SFmode)
	{
	  if (op1 == CONST0_RTX (SFmode))
	    {
	      if (GP_REG_P (regno0))
		ret = "or\t%0,%%0,%.";
	    }

	  else
	    {
	      delay = DELAY_LOAD;
	      ret = "li.s\t%0,%1";
	    }
	}

      else if (code1 == LABEL_REF)
	{
	  if (TARGET_STATS)
	    iq2000_count_memory_refs (op1, 1);

	  ret = "la\t%0,%a1";
	}

      else if (code1 == SYMBOL_REF || code1 == CONST)
	{
	  if (TARGET_STATS)
	    iq2000_count_memory_refs (op1, 1);

	  ret = "la\t%0,%a1";
	}

      else if (code1 == PLUS)
	{
	  rtx add_op0 = XEXP (op1, 0);
	  rtx add_op1 = XEXP (op1, 1);

	  if (GET_CODE (XEXP (op1, 1)) == REG
	      && GET_CODE (XEXP (op1, 0)) == CONST_INT)
	    add_op0 = XEXP (op1, 1), add_op1 = XEXP (op1, 0);

	  operands[2] = add_op0;
	  operands[3] = add_op1;
	  ret = "add%:\t%0,%2,%3";
	}

      else if (code1 == HIGH)
	{
	  operands[1] = XEXP (op1, 0);
	  ret = "lui\t%0,%%hi(%1)";
	}
    }

  else if (code0 == MEM)
    {
      if (TARGET_STATS)
	iq2000_count_memory_refs (op0, 1);

      if (code1 == REG)
	{
	  int regno1 = REGNO (op1) + subreg_offset1;

	  if (GP_REG_P (regno1))
	    {
	      switch (mode)
		{
		case SFmode: ret = "sw\t%1,%0"; break;
		case SImode: ret = "sw\t%1,%0"; break;
		case HImode: ret = "sh\t%1,%0"; break;
		case QImode: ret = "sb\t%1,%0"; break;
		default: break;
		}
	    }
	}

      else if (code1 == CONST_INT && INTVAL (op1) == 0)
	{
	  switch (mode)
	    {
	    case SFmode: ret = "sw\t%z1,%0"; break;
	    case SImode: ret = "sw\t%z1,%0"; break;
	    case HImode: ret = "sh\t%z1,%0"; break;
	    case QImode: ret = "sb\t%z1,%0"; break;
	    default: break;
	    }
	}

      else if (code1 == CONST_DOUBLE && op1 == CONST0_RTX (mode))
	{
	  switch (mode)
	    {
	    case SFmode: ret = "sw\t%.,%0"; break;
	    case SImode: ret = "sw\t%.,%0"; break;
	    case HImode: ret = "sh\t%.,%0"; break;
	    case QImode: ret = "sb\t%.,%0"; break;
	    default: break;
	    }
	}
    }

  if (ret == 0)
    {
      abort_with_insn (insn, "Bad move");
      return 0;
    }

  if (delay != DELAY_NONE)
    return iq2000_fill_delay_slot (ret, delay, operands, insn);

  return ret;
}

/* Provide the costs of an addressing mode that contains ADDR.  */

static int
iq2000_address_cost (rtx addr)
{
  switch (GET_CODE (addr))
    {
    case LO_SUM:
      return 1;

    case LABEL_REF:
      return 2;

    case CONST:
      {
	rtx offset = const0_rtx;

	addr = eliminate_constant_term (XEXP (addr, 0), & offset);
	if (GET_CODE (addr) == LABEL_REF)
	  return 2;

	if (GET_CODE (addr) != SYMBOL_REF)
	  return 4;

	if (! SMALL_INT (offset))
	  return 2;
      }

      /* Fall through.  */

    case SYMBOL_REF:
      return SYMBOL_REF_FLAG (addr) ? 1 : 2;

    case PLUS:
      {
	rtx plus0 = XEXP (addr, 0);
	rtx plus1 = XEXP (addr, 1);

	if (GET_CODE (plus0) != REG && GET_CODE (plus1) == REG)
	  plus0 = XEXP (addr, 1), plus1 = XEXP (addr, 0);

	if (GET_CODE (plus0) != REG)
	  break;

	switch (GET_CODE (plus1))
	  {
	  case CONST_INT:
	    return SMALL_INT (plus1) ? 1 : 2;

	  case CONST:
	  case SYMBOL_REF:
	  case LABEL_REF:
	  case HIGH:
	  case LO_SUM:
	    return iq2000_address_cost (plus1) + 1;

	  default:
	    break;
	  }
      }

    default:
      break;
    }

  return 4;
}

/* Make normal rtx_code into something we can index from an array.  */

static enum internal_test
map_test_to_internal_test (enum rtx_code test_code)
{
  enum internal_test test = ITEST_MAX;

  switch (test_code)
    {
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
    default:			break;
    }

  return test;
}

/* Generate the code to do a TEST_CODE comparison on two integer values CMP0
   and CMP1.  P_INVERT is NULL or ptr if branch needs to reverse its test.
   The return value RESULT is:
   (reg:SI xx)		The pseudo register the comparison is in
   0		       	No register, generate a simple branch.  */

rtx
gen_int_relational (enum rtx_code test_code, rtx result, rtx cmp0, rtx cmp1,
		    int *p_invert)
{
  struct cmp_info
  {
    enum rtx_code test_code;	/* Code to use in instruction (LT vs. LTU).  */
    int const_low;		/* Low bound of constant we can accept.  */
    int const_high;		/* High bound of constant we can accept.  */
    int const_add;		/* Constant to add (convert LE -> LT).  */
    int reverse_regs;		/* Reverse registers in test.  */
    int invert_const;		/* != 0 if invert value if cmp1 is constant.  */
    int invert_reg;		/* != 0 if invert value if cmp1 is register.  */
    int unsignedp;		/* != 0 for unsigned comparisons.  */
  };

  static struct cmp_info info[ (int)ITEST_MAX ] =
  {
    { XOR,	 0,  65535,  0,	 0,  0,	 0, 0 },	/* EQ  */
    { XOR,	 0,  65535,  0,	 0,  1,	 1, 0 },	/* NE  */
    { LT,   -32769,  32766,  1,	 1,  1,	 0, 0 },	/* GT  */
    { LT,   -32768,  32767,  0,	 0,  1,	 1, 0 },	/* GE  */
    { LT,   -32768,  32767,  0,	 0,  0,	 0, 0 },	/* LT  */
    { LT,   -32769,  32766,  1,	 1,  0,	 1, 0 },	/* LE  */
    { LTU,  -32769,  32766,  1,	 1,  1,	 0, 1 },	/* GTU */
    { LTU,  -32768,  32767,  0,	 0,  1,	 1, 1 },	/* GEU */
    { LTU,  -32768,  32767,  0,	 0,  0,	 0, 1 },	/* LTU */
    { LTU,  -32769,  32766,  1,	 1,  0,	 1, 1 },	/* LEU */
  };

  enum internal_test test;
  enum machine_mode mode;
  struct cmp_info *p_info;
  int branch_p;
  int eqne_p;
  int invert;
  rtx reg;
  rtx reg2;

  test = map_test_to_internal_test (test_code);
  gcc_assert (test != ITEST_MAX);

  p_info = &info[(int) test];
  eqne_p = (p_info->test_code == XOR);

  mode = GET_MODE (cmp0);
  if (mode == VOIDmode)
    mode = GET_MODE (cmp1);

  /* Eliminate simple branches.  */
  branch_p = (result == 0);
  if (branch_p)
    {
      if (GET_CODE (cmp0) == REG || GET_CODE (cmp0) == SUBREG)
	{
	  /* Comparisons against zero are simple branches.  */
	  if (GET_CODE (cmp1) == CONST_INT && INTVAL (cmp1) == 0)
	    return 0;

	  /* Test for beq/bne.  */
	  if (eqne_p)
	    return 0;
	}

      /* Allocate a pseudo to calculate the value in.  */
      result = gen_reg_rtx (mode);
    }

  /* Make sure we can handle any constants given to us.  */
  if (GET_CODE (cmp0) == CONST_INT)
    cmp0 = force_reg (mode, cmp0);

  if (GET_CODE (cmp1) == CONST_INT)
    {
      HOST_WIDE_INT value = INTVAL (cmp1);

      if (value < p_info->const_low
	  || value > p_info->const_high)
	cmp1 = force_reg (mode, cmp1);
    }

  /* See if we need to invert the result.  */
  invert = (GET_CODE (cmp1) == CONST_INT
	    ? p_info->invert_const : p_info->invert_reg);

  if (p_invert != (int *)0)
    {
      *p_invert = invert;
      invert = 0;
    }

  /* Comparison to constants, may involve adding 1 to change a LT into LE.
     Comparison between two registers, may involve switching operands.  */
  if (GET_CODE (cmp1) == CONST_INT)
    {
      if (p_info->const_add != 0)
	{
	  HOST_WIDE_INT new = INTVAL (cmp1) + p_info->const_add;

	  /* If modification of cmp1 caused overflow,
	     we would get the wrong answer if we follow the usual path;
	     thus, x > 0xffffffffU would turn into x > 0U.  */
	  if ((p_info->unsignedp
	       ? (unsigned HOST_WIDE_INT) new >
	       (unsigned HOST_WIDE_INT) INTVAL (cmp1)
	       : new > INTVAL (cmp1))
	      != (p_info->const_add > 0))
	    {
	      /* This test is always true, but if INVERT is true then
		 the result of the test needs to be inverted so 0 should
		 be returned instead.  */
	      emit_move_insn (result, invert ? const0_rtx : const_true_rtx);
	      return result;
	    }
	  else
	    cmp1 = GEN_INT (new);
	}
    }

  else if (p_info->reverse_regs)
    {
      rtx temp = cmp0;
      cmp0 = cmp1;
      cmp1 = temp;
    }

  if (test == ITEST_NE && GET_CODE (cmp1) == CONST_INT && INTVAL (cmp1) == 0)
    reg = cmp0;
  else
    {
      reg = (invert || eqne_p) ? gen_reg_rtx (mode) : result;
      convert_move (reg, gen_rtx_fmt_ee (p_info->test_code, mode, cmp0, cmp1), 0);
    }

  if (test == ITEST_NE)
    {
      convert_move (result, gen_rtx_GTU (mode, reg, const0_rtx), 0);
      if (p_invert != NULL)
	*p_invert = 0;
      invert = 0;
    }

  else if (test == ITEST_EQ)
    {
      reg2 = invert ? gen_reg_rtx (mode) : result;
      convert_move (reg2, gen_rtx_LTU (mode, reg, const1_rtx), 0);
      reg = reg2;
    }

  if (invert)
    {
      rtx one;

      one = const1_rtx;
      convert_move (result, gen_rtx_XOR (mode, reg, one), 0);
    }

  return result;
}

/* Emit the common code for doing conditional branches.
   operand[0] is the label to jump to.
   The comparison operands are saved away by cmp{si,di,sf,df}.  */

void
gen_conditional_branch (rtx operands[], enum rtx_code test_code)
{
  enum cmp_type type = branch_type;
  rtx cmp0 = branch_cmp[0];
  rtx cmp1 = branch_cmp[1];
  enum machine_mode mode;
  rtx reg;
  int invert;
  rtx label1, label2;

  switch (type)
    {
    case CMP_SI:
    case CMP_DI:
      mode = type == CMP_SI ? SImode : DImode;
      invert = 0;
      reg = gen_int_relational (test_code, NULL_RTX, cmp0, cmp1, &invert);

      if (reg)
	{
	  cmp0 = reg;
	  cmp1 = const0_rtx;
	  test_code = NE;
	}
      else if (GET_CODE (cmp1) == CONST_INT && INTVAL (cmp1) != 0)
	/* We don't want to build a comparison against a nonzero
	   constant.  */
	cmp1 = force_reg (mode, cmp1);

      break;

    case CMP_SF:
    case CMP_DF:
      reg = gen_reg_rtx (CCmode);

      /* For cmp0 != cmp1, build cmp0 == cmp1, and test for result == 0.  */
      emit_insn (gen_rtx_SET (VOIDmode, reg,
			      gen_rtx_fmt_ee (test_code == NE ? EQ : test_code,
					      CCmode, cmp0, cmp1)));

      test_code = test_code == NE ? EQ : NE;
      mode = CCmode;
      cmp0 = reg;
      cmp1 = const0_rtx;
      invert = 0;
      break;

    default:
      abort_with_insn (gen_rtx_fmt_ee (test_code, VOIDmode, cmp0, cmp1),
		       "bad test");
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
			       gen_rtx_IF_THEN_ELSE (VOIDmode,
						     gen_rtx_fmt_ee (test_code,
								     mode,
								     cmp0, cmp1),
						     label1, label2)));
}

/* Initialize CUM for a function FNTYPE.  */

void
init_cumulative_args (CUMULATIVE_ARGS *cum, tree fntype,
		      rtx libname ATTRIBUTE_UNUSED)
{
  static CUMULATIVE_ARGS zero_cum;
  tree param;
  tree next_param;

  if (TARGET_DEBUG_D_MODE)
    {
      fprintf (stderr,
	       "\ninit_cumulative_args, fntype = 0x%.8lx", (long) fntype);

      if (!fntype)
	fputc ('\n', stderr);

      else
	{
	  tree ret_type = TREE_TYPE (fntype);

	  fprintf (stderr, ", fntype code = %s, ret code = %s\n",
		   tree_code_name[(int)TREE_CODE (fntype)],
		   tree_code_name[(int)TREE_CODE (ret_type)]);
	}
    }

  *cum = zero_cum;

  /* Determine if this function has variable arguments.  This is
     indicated by the last argument being 'void_type_mode' if there
     are no variable arguments.  The standard IQ2000 calling sequence
     passes all arguments in the general purpose registers in this case.  */

  for (param = fntype ? TYPE_ARG_TYPES (fntype) : 0;
       param != 0; param = next_param)
    {
      next_param = TREE_CHAIN (param);
      if (next_param == 0 && TREE_VALUE (param) != void_type_node)
	cum->gp_reg_found = 1;
    }
}

/* Advance the argument of type TYPE and mode MODE to the next argument
   position in CUM.  */

void
function_arg_advance (CUMULATIVE_ARGS *cum, enum machine_mode mode, tree type,
		      int named)
{
  if (TARGET_DEBUG_D_MODE)
    {
      fprintf (stderr,
	       "function_adv({gp reg found = %d, arg # = %2d, words = %2d}, %4s, ",
	       cum->gp_reg_found, cum->arg_number, cum->arg_words,
	       GET_MODE_NAME (mode));
      fprintf (stderr, "%p", (void *) type);
      fprintf (stderr, ", %d )\n\n", named);
    }

  cum->arg_number++;
  switch (mode)
    {
    case VOIDmode:
      break;

    default:
      gcc_assert (GET_MODE_CLASS (mode) == MODE_COMPLEX_INT
		  || GET_MODE_CLASS (mode) == MODE_COMPLEX_FLOAT);

      cum->gp_reg_found = 1;
      cum->arg_words += ((GET_MODE_SIZE (mode) + UNITS_PER_WORD - 1)
			 / UNITS_PER_WORD);
      break;

    case BLKmode:
      cum->gp_reg_found = 1;
      cum->arg_words += ((int_size_in_bytes (type) + UNITS_PER_WORD - 1)
			 / UNITS_PER_WORD);
      break;

    case SFmode:
      cum->arg_words ++;
      if (! cum->gp_reg_found && cum->arg_number <= 2)
	cum->fp_code += 1 << ((cum->arg_number - 1) * 2);
      break;

    case DFmode:
      cum->arg_words += 2;
      if (! cum->gp_reg_found && cum->arg_number <= 2)
	cum->fp_code += 2 << ((cum->arg_number - 1) * 2);
      break;

    case DImode:
      cum->gp_reg_found = 1;
      cum->arg_words += 2;
      break;

    case QImode:
    case HImode:
    case SImode:
      cum->gp_reg_found = 1;
      cum->arg_words ++;
      break;
    }
}

/* Return an RTL expression containing the register for the given mode MODE
   and type TYPE in CUM, or 0 if the argument is to be passed on the stack.  */

struct rtx_def *
function_arg (CUMULATIVE_ARGS *cum, enum machine_mode mode, tree type,
	      int named)
{
  rtx ret;
  int regbase = -1;
  int bias = 0;
  unsigned int *arg_words = &cum->arg_words;
  int struct_p = (type != 0
		  && (TREE_CODE (type) == RECORD_TYPE
		      || TREE_CODE (type) == UNION_TYPE
		      || TREE_CODE (type) == QUAL_UNION_TYPE));

  if (TARGET_DEBUG_D_MODE)
    {
      fprintf (stderr,
	       "function_arg( {gp reg found = %d, arg # = %2d, words = %2d}, %4s, ",
	       cum->gp_reg_found, cum->arg_number, cum->arg_words,
	       GET_MODE_NAME (mode));
      fprintf (stderr, "%p", (void *) type);
      fprintf (stderr, ", %d ) = ", named);
    }


  cum->last_arg_fp = 0;
  switch (mode)
    {
    case SFmode:
      regbase = GP_ARG_FIRST;
      break;

    case DFmode:
      cum->arg_words += cum->arg_words & 1;

      regbase = GP_ARG_FIRST;
      break;

    default:
      gcc_assert (GET_MODE_CLASS (mode) == MODE_COMPLEX_INT
		  || GET_MODE_CLASS (mode) == MODE_COMPLEX_FLOAT);

      /* Drops through.  */
    case BLKmode:
      if (type != NULL_TREE && TYPE_ALIGN (type) > (unsigned) BITS_PER_WORD)
	cum->arg_words += (cum->arg_words & 1);
      regbase = GP_ARG_FIRST;
      break;

    case VOIDmode:
    case QImode:
    case HImode:
    case SImode:
      regbase = GP_ARG_FIRST;
      break;

    case DImode:
      cum->arg_words += (cum->arg_words & 1);
      regbase = GP_ARG_FIRST;
    }

  if (*arg_words >= (unsigned) MAX_ARGS_IN_REGISTERS)
    {
      if (TARGET_DEBUG_D_MODE)
	fprintf (stderr, "<stack>%s\n", struct_p ? ", [struct]" : "");

      ret = 0;
    }
  else
    {
      gcc_assert (regbase != -1);

      if (! type || TREE_CODE (type) != RECORD_TYPE
	  || ! named  || ! TYPE_SIZE_UNIT (type)
	  || ! host_integerp (TYPE_SIZE_UNIT (type), 1))
	ret = gen_rtx_REG (mode, regbase + *arg_words + bias);
      else
	{
	  tree field;

	  for (field = TYPE_FIELDS (type); field; field = TREE_CHAIN (field))
	    if (TREE_CODE (field) == FIELD_DECL
		&& TREE_CODE (TREE_TYPE (field)) == REAL_TYPE
		&& TYPE_PRECISION (TREE_TYPE (field)) == BITS_PER_WORD
		&& host_integerp (bit_position (field), 0)
		&& int_bit_position (field) % BITS_PER_WORD == 0)
	      break;

	  /* If the whole struct fits a DFmode register,
	     we don't need the PARALLEL.  */
	  if (! field || mode == DFmode)
	    ret = gen_rtx_REG (mode, regbase + *arg_words + bias);
	  else
	    {
	      unsigned int chunks;
	      HOST_WIDE_INT bitpos;
	      unsigned int regno;
	      unsigned int i;

	      /* ??? If this is a packed structure, then the last hunk won't
		 be 64 bits.  */
	      chunks
		= tree_low_cst (TYPE_SIZE_UNIT (type), 1) / UNITS_PER_WORD;
	      if (chunks + *arg_words + bias > (unsigned) MAX_ARGS_IN_REGISTERS)
		chunks = MAX_ARGS_IN_REGISTERS - *arg_words - bias;

	      /* Assign_parms checks the mode of ENTRY_PARM, so we must
		 use the actual mode here.  */
	      ret = gen_rtx_PARALLEL (mode, rtvec_alloc (chunks));

	      bitpos = 0;
	      regno = regbase + *arg_words + bias;
	      field = TYPE_FIELDS (type);
	      for (i = 0; i < chunks; i++)
		{
		  rtx reg;

		  for (; field; field = TREE_CHAIN (field))
		    if (TREE_CODE (field) == FIELD_DECL
			&& int_bit_position (field) >= bitpos)
		      break;

		  if (field
		      && int_bit_position (field) == bitpos
		      && TREE_CODE (TREE_TYPE (field)) == REAL_TYPE
		      && TYPE_PRECISION (TREE_TYPE (field)) == BITS_PER_WORD)
		    reg = gen_rtx_REG (DFmode, regno++);
		  else
		    reg = gen_rtx_REG (word_mode, regno);

		  XVECEXP (ret, 0, i)
		    = gen_rtx_EXPR_LIST (VOIDmode, reg,
					 GEN_INT (bitpos / BITS_PER_UNIT));

		  bitpos += 64;
		  regno++;
		}
	    }
	}

      if (TARGET_DEBUG_D_MODE)
	fprintf (stderr, "%s%s\n", reg_names[regbase + *arg_words + bias],
		 struct_p ? ", [struct]" : "");
    }

  /* We will be called with a mode of VOIDmode after the last argument
     has been seen.  Whatever we return will be passed to the call
     insn.  If we need any shifts for small structures, return them in
     a PARALLEL.  */
  if (mode == VOIDmode)
    {
      if (cum->num_adjusts > 0)
	ret = gen_rtx_PARALLEL ((enum machine_mode) cum->fp_code,
		       gen_rtvec_v (cum->num_adjusts, cum->adjust));
    }

  return ret;
}

static int
iq2000_arg_partial_bytes (CUMULATIVE_ARGS *cum, enum machine_mode mode,
			  tree type ATTRIBUTE_UNUSED,
			  bool named ATTRIBUTE_UNUSED)
{
  if (mode == DImode && cum->arg_words == MAX_ARGS_IN_REGISTERS - 1)
    {
      if (TARGET_DEBUG_D_MODE)
	fprintf (stderr, "iq2000_arg_partial_bytes=%d\n", UNITS_PER_WORD);
      return UNITS_PER_WORD;
    }

  return 0;
}

/* Implement va_start.  */

void
iq2000_va_start (tree valist, rtx nextarg)
{
  int int_arg_words;
  /* Find out how many non-float named formals.  */
  int gpr_save_area_size;
  /* Note UNITS_PER_WORD is 4 bytes.  */
  int_arg_words = current_function_args_info.arg_words;

  if (int_arg_words < 8 )
    /* Adjust for the prologue's economy measure.  */
    gpr_save_area_size = (8 - int_arg_words) * UNITS_PER_WORD;
  else
    gpr_save_area_size = 0;

  /* Everything is in the GPR save area, or in the overflow
     area which is contiguous with it.  */
  nextarg = plus_constant (nextarg, - gpr_save_area_size);
  std_expand_builtin_va_start (valist, nextarg);
}

/* Allocate a chunk of memory for per-function machine-dependent data.  */

static struct machine_function *
iq2000_init_machine_status (void)
{
  struct machine_function *f;

  f = ggc_alloc_cleared (sizeof (struct machine_function));

  return f;
}

/* Implement TARGET_HANDLE_OPTION.  */

static bool
iq2000_handle_option (size_t code, const char *arg, int value ATTRIBUTE_UNUSED)
{
  switch (code)
    {
    case OPT_mcpu_:
      if (strcmp (arg, "iq10") == 0)
	iq2000_tune = PROCESSOR_IQ10;
      else if (strcmp (arg, "iq2000") == 0)
	iq2000_tune = PROCESSOR_IQ2000;
      else
	return false;
      return true;

    case OPT_march_:
      /* This option has no effect at the moment.  */
      return (strcmp (arg, "default") == 0
	      || strcmp (arg, "DEFAULT") == 0
	      || strcmp (arg, "iq2000") == 0);

    default:
      return true;
    }
}

/* Detect any conflicts in the switches.  */

void
override_options (void)
{
  target_flags &= ~MASK_GPOPT;

  iq2000_isa = IQ2000_ISA_DEFAULT;

  /* Identify the processor type.  */

  iq2000_print_operand_punct['?'] = 1;
  iq2000_print_operand_punct['#'] = 1;
  iq2000_print_operand_punct['&'] = 1;
  iq2000_print_operand_punct['!'] = 1;
  iq2000_print_operand_punct['*'] = 1;
  iq2000_print_operand_punct['@'] = 1;
  iq2000_print_operand_punct['.'] = 1;
  iq2000_print_operand_punct['('] = 1;
  iq2000_print_operand_punct[')'] = 1;
  iq2000_print_operand_punct['['] = 1;
  iq2000_print_operand_punct[']'] = 1;
  iq2000_print_operand_punct['<'] = 1;
  iq2000_print_operand_punct['>'] = 1;
  iq2000_print_operand_punct['{'] = 1;
  iq2000_print_operand_punct['}'] = 1;
  iq2000_print_operand_punct['^'] = 1;
  iq2000_print_operand_punct['$'] = 1;
  iq2000_print_operand_punct['+'] = 1;
  iq2000_print_operand_punct['~'] = 1;

  /* Save GPR registers in word_mode sized hunks.  word_mode hasn't been
     initialized yet, so we can't use that here.  */
  gpr_mode = SImode;

  /* Function to allocate machine-dependent function status.  */
  init_machine_status = iq2000_init_machine_status;
}

/* The arg pointer (which is eliminated) points to the virtual frame pointer,
   while the frame pointer (which may be eliminated) points to the stack
   pointer after the initial adjustments.  */

HOST_WIDE_INT
iq2000_debugger_offset (rtx addr, HOST_WIDE_INT offset)
{
  rtx offset2 = const0_rtx;
  rtx reg = eliminate_constant_term (addr, & offset2);

  if (offset == 0)
    offset = INTVAL (offset2);

  if (reg == stack_pointer_rtx || reg == frame_pointer_rtx
      || reg == hard_frame_pointer_rtx)
    {
      HOST_WIDE_INT frame_size = (!cfun->machine->initialized)
				  ? compute_frame_size (get_frame_size ())
				  : cfun->machine->total_size;

      offset = offset - frame_size;
    }

  return offset;
}

/* If defined, a C statement to be executed just prior to the output of
   assembler code for INSN, to modify the extracted operands so they will be
   output differently.

   Here the argument OPVEC is the vector containing the operands extracted
   from INSN, and NOPERANDS is the number of elements of the vector which
   contain meaningful data for this insn.  The contents of this vector are
   what will be used to convert the insn template into assembler code, so you
   can change the assembler output by changing the contents of the vector.

   We use it to check if the current insn needs a nop in front of it because
   of load delays, and also to update the delay slot statistics.  */

void
final_prescan_insn (rtx insn, rtx opvec[] ATTRIBUTE_UNUSED,
		    int noperands ATTRIBUTE_UNUSED)
{
  if (dslots_number_nops > 0)
    {
      rtx pattern = PATTERN (insn);
      int length = get_attr_length (insn);

      /* Do we need to emit a NOP?  */
      if (length == 0
	  || (iq2000_load_reg != 0 && reg_mentioned_p (iq2000_load_reg,  pattern))
	  || (iq2000_load_reg2 != 0 && reg_mentioned_p (iq2000_load_reg2, pattern))
	  || (iq2000_load_reg3 != 0 && reg_mentioned_p (iq2000_load_reg3, pattern))
	  || (iq2000_load_reg4 != 0
	      && reg_mentioned_p (iq2000_load_reg4, pattern)))
	fputs ("\tnop\n", asm_out_file);

      else
	dslots_load_filled ++;

      while (--dslots_number_nops > 0)
	fputs ("\tnop\n", asm_out_file);

      iq2000_load_reg = 0;
      iq2000_load_reg2 = 0;
      iq2000_load_reg3 = 0;
      iq2000_load_reg4 = 0;
    }

  if (   (GET_CODE (insn) == JUMP_INSN
       || GET_CODE (insn) == CALL_INSN
       || (GET_CODE (PATTERN (insn)) == RETURN))
	   && NEXT_INSN (PREV_INSN (insn)) == insn)
    {
      rtx nop_insn = emit_insn_after (gen_nop (), insn);

      INSN_ADDRESSES_NEW (nop_insn, -1);
    }
  
  if (TARGET_STATS
      && (GET_CODE (insn) == JUMP_INSN || GET_CODE (insn) == CALL_INSN))
    dslots_jump_total ++;
}

/* Return the bytes needed to compute the frame pointer from the current
   stack pointer where SIZE is the # of var. bytes allocated.

   IQ2000 stack frames look like:

             Before call		        After call
        +-----------------------+	+-----------------------+
   high |			|       |      			|
   mem. |		        |	|			|
        |  caller's temps.    	|       |  caller's temps.    	|
	|       		|       |       	        |
        +-----------------------+	+-----------------------+
 	|       		|	|		        |
        |  arguments on stack.  |	|  arguments on stack.  |
	|       		|	|			|
        +-----------------------+	+-----------------------+
 	|  4 words to save     	|	|  4 words to save	|
	|  arguments passed	|	|  arguments passed	|
	|  in registers, even	|	|  in registers, even	|
    SP->|  if not passed.       |  VFP->|  if not passed.	|
	+-----------------------+       +-----------------------+
					|		        |
                                        |  fp register save     |
					|			|
					+-----------------------+
					|		        |
                                        |  gp register save     |
                                        |       		|
					+-----------------------+
					|			|
					|  local variables	|
					|			|
					+-----------------------+
					|			|
                                        |  alloca allocations   |
        				|			|
					+-----------------------+
					|			|
					|  GP save for V.4 abi	|
					|			|
					+-----------------------+
					|			|
                                        |  arguments on stack   |
        				|		        |
					+-----------------------+
                                        |  4 words to save      |
					|  arguments passed     |
                                        |  in registers, even   |
   low                              SP->|  if not passed.       |
   memory        			+-----------------------+  */

HOST_WIDE_INT
compute_frame_size (HOST_WIDE_INT size)
{
  int regno;
  HOST_WIDE_INT total_size;	/* # bytes that the entire frame takes up.  */
  HOST_WIDE_INT var_size;	/* # bytes that variables take up.  */
  HOST_WIDE_INT args_size;	/* # bytes that outgoing arguments take up.  */
  HOST_WIDE_INT extra_size;	/* # extra bytes.  */
  HOST_WIDE_INT gp_reg_rounded;	/* # bytes needed to store gp after rounding.  */
  HOST_WIDE_INT gp_reg_size;	/* # bytes needed to store gp regs.  */
  HOST_WIDE_INT fp_reg_size;	/* # bytes needed to store fp regs.  */
  long mask;			/* mask of saved gp registers.  */
  int  fp_inc;			/* 1 or 2 depending on the size of fp regs.  */
  long fp_bits;			/* bitmask to use for each fp register.  */

  gp_reg_size = 0;
  fp_reg_size = 0;
  mask = 0;
  extra_size = IQ2000_STACK_ALIGN ((0));
  var_size = IQ2000_STACK_ALIGN (size);
  args_size = IQ2000_STACK_ALIGN (current_function_outgoing_args_size);

  /* If a function dynamically allocates the stack and
     has 0 for STACK_DYNAMIC_OFFSET then allocate some stack space.  */
  if (args_size == 0 && current_function_calls_alloca)
    args_size = 4 * UNITS_PER_WORD;

  total_size = var_size + args_size + extra_size;

  /* Calculate space needed for gp registers.  */
  for (regno = GP_REG_FIRST; regno <= GP_REG_LAST; regno++)
    {
      if (MUST_SAVE_REGISTER (regno))
	{
	  gp_reg_size += GET_MODE_SIZE (gpr_mode);
	  mask |= 1L << (regno - GP_REG_FIRST);
	}
    }

  /* We need to restore these for the handler.  */
  if (current_function_calls_eh_return)
    {
      unsigned int i;

      for (i = 0; ; ++i)
	{
	  regno = EH_RETURN_DATA_REGNO (i);
	  if (regno == (int) INVALID_REGNUM)
	    break;
	  gp_reg_size += GET_MODE_SIZE (gpr_mode);
	  mask |= 1L << (regno - GP_REG_FIRST);
	}
    }

  fp_inc = 2;
  fp_bits = 3;
  gp_reg_rounded = IQ2000_STACK_ALIGN (gp_reg_size);
  total_size += gp_reg_rounded + IQ2000_STACK_ALIGN (fp_reg_size);

  /* The gp reg is caller saved, so there is no need for leaf routines 
     (total_size == extra_size) to save the gp reg.  */
  if (total_size == extra_size
      && ! profile_flag)
    total_size = extra_size = 0;

  total_size += IQ2000_STACK_ALIGN (current_function_pretend_args_size);

  /* Save other computed information.  */
  cfun->machine->total_size = total_size;
  cfun->machine->var_size = var_size;
  cfun->machine->args_size = args_size;
  cfun->machine->extra_size = extra_size;
  cfun->machine->gp_reg_size = gp_reg_size;
  cfun->machine->fp_reg_size = fp_reg_size;
  cfun->machine->mask = mask;
  cfun->machine->initialized = reload_completed;
  cfun->machine->num_gp = gp_reg_size / UNITS_PER_WORD;

  if (mask)
    {
      unsigned long offset;

      offset = (args_size + extra_size + var_size
		+ gp_reg_size - GET_MODE_SIZE (gpr_mode));

      cfun->machine->gp_sp_offset = offset;
      cfun->machine->gp_save_offset = offset - total_size;
    }
  else
    {
      cfun->machine->gp_sp_offset = 0;
      cfun->machine->gp_save_offset = 0;
    }

  cfun->machine->fp_sp_offset = 0;
  cfun->machine->fp_save_offset = 0;

  /* Ok, we're done.  */
  return total_size;
}

/* Implement INITIAL_ELIMINATION_OFFSET.  FROM is either the frame
   pointer, argument pointer, or return address pointer.  TO is either
   the stack pointer or hard frame pointer.  */

int
iq2000_initial_elimination_offset (int from, int to ATTRIBUTE_UNUSED)
{
  int offset;

  compute_frame_size (get_frame_size ());				 
  if ((from) == FRAME_POINTER_REGNUM) 
    (offset) = 0; 
  else if ((from) == ARG_POINTER_REGNUM) 
    (offset) = (cfun->machine->total_size); 
  else if ((from) == RETURN_ADDRESS_POINTER_REGNUM) 
    {
      if (leaf_function_p ()) 
	(offset) = 0; 
      else (offset) = cfun->machine->gp_sp_offset 
	     + ((UNITS_PER_WORD - (POINTER_SIZE / BITS_PER_UNIT)) 
		* (BYTES_BIG_ENDIAN != 0)); 
    }

  return offset;
}

/* Common code to emit the insns (or to write the instructions to a file)
   to save/restore registers.  
   Other parts of the code assume that IQ2000_TEMP1_REGNUM (aka large_reg)
   is not modified within save_restore_insns.  */

#define BITSET_P(VALUE,BIT) (((VALUE) & (1L << (BIT))) != 0)

/* Emit instructions to load the value (SP + OFFSET) into IQ2000_TEMP2_REGNUM
   and return an rtl expression for the register.  Write the assembly
   instructions directly to FILE if it is not null, otherwise emit them as
   rtl.

   This function is a subroutine of save_restore_insns.  It is used when
   OFFSET is too large to add in a single instruction.  */

static rtx
iq2000_add_large_offset_to_sp (HOST_WIDE_INT offset)
{
  rtx reg = gen_rtx_REG (Pmode, IQ2000_TEMP2_REGNUM);
  rtx offset_rtx = GEN_INT (offset);

  emit_move_insn (reg, offset_rtx);
  emit_insn (gen_addsi3 (reg, reg, stack_pointer_rtx));
  return reg;
}

/* Make INSN frame related and note that it performs the frame-related
   operation DWARF_PATTERN.  */

static void
iq2000_annotate_frame_insn (rtx insn, rtx dwarf_pattern)
{
  RTX_FRAME_RELATED_P (insn) = 1;
  REG_NOTES (insn) = alloc_EXPR_LIST (REG_FRAME_RELATED_EXPR,
				      dwarf_pattern,
				      REG_NOTES (insn));
}

/* Emit a move instruction that stores REG in MEM.  Make the instruction
   frame related and note that it stores REG at (SP + OFFSET).  */

static void
iq2000_emit_frame_related_store (rtx mem, rtx reg, HOST_WIDE_INT offset)
{
  rtx dwarf_address = plus_constant (stack_pointer_rtx, offset);
  rtx dwarf_mem = gen_rtx_MEM (GET_MODE (reg), dwarf_address);

  iq2000_annotate_frame_insn (emit_move_insn (mem, reg),
			    gen_rtx_SET (GET_MODE (reg), dwarf_mem, reg));
}

/* Emit instructions to save/restore registers, as determined by STORE_P.  */

static void
save_restore_insns (int store_p)
{
  long mask = cfun->machine->mask;
  int regno;
  rtx base_reg_rtx;
  HOST_WIDE_INT base_offset;
  HOST_WIDE_INT gp_offset;
  HOST_WIDE_INT end_offset;

  gcc_assert (!frame_pointer_needed
	      || BITSET_P (mask, HARD_FRAME_POINTER_REGNUM - GP_REG_FIRST));

  if (mask == 0)
    {
      base_reg_rtx = 0, base_offset  = 0;
      return;
    }

  /* Save registers starting from high to low.  The debuggers prefer at least
     the return register be stored at func+4, and also it allows us not to
     need a nop in the epilog if at least one register is reloaded in
     addition to return address.  */

  /* Save GP registers if needed.  */
  /* Pick which pointer to use as a base register.  For small frames, just
     use the stack pointer.  Otherwise, use a temporary register.  Save 2
     cycles if the save area is near the end of a large frame, by reusing
     the constant created in the prologue/epilogue to adjust the stack
     frame.  */

  gp_offset = cfun->machine->gp_sp_offset;
  end_offset
    = gp_offset - (cfun->machine->gp_reg_size
		   - GET_MODE_SIZE (gpr_mode));

  if (gp_offset < 0 || end_offset < 0)
    internal_error
      ("gp_offset (%ld) or end_offset (%ld) is less than zero",
       (long) gp_offset, (long) end_offset);

  else if (gp_offset < 32768)
    base_reg_rtx = stack_pointer_rtx, base_offset  = 0;
  else
    {
      int regno;
      int reg_save_count = 0;

      for (regno = GP_REG_LAST; regno >= GP_REG_FIRST; regno--)
	if (BITSET_P (mask, regno - GP_REG_FIRST)) reg_save_count += 1;
      base_offset = gp_offset - ((reg_save_count - 1) * 4);
      base_reg_rtx = iq2000_add_large_offset_to_sp (base_offset);
    }

  for (regno = GP_REG_LAST; regno >= GP_REG_FIRST; regno--)
    {
      if (BITSET_P (mask, regno - GP_REG_FIRST))
	{
	  rtx reg_rtx;
	  rtx mem_rtx
	    = gen_rtx_MEM (gpr_mode,
		       gen_rtx_PLUS (Pmode, base_reg_rtx,
				GEN_INT (gp_offset - base_offset)));

	  reg_rtx = gen_rtx_REG (gpr_mode, regno);

	  if (store_p)
	    iq2000_emit_frame_related_store (mem_rtx, reg_rtx, gp_offset);
	  else 
	    {
	      emit_move_insn (reg_rtx, mem_rtx);
	    }
	  gp_offset -= GET_MODE_SIZE (gpr_mode);
	}
    }
}

/* Expand the prologue into a bunch of separate insns.  */

void
iq2000_expand_prologue (void)
{
  int regno;
  HOST_WIDE_INT tsize;
  int last_arg_is_vararg_marker = 0;
  tree fndecl = current_function_decl;
  tree fntype = TREE_TYPE (fndecl);
  tree fnargs = DECL_ARGUMENTS (fndecl);
  rtx next_arg_reg;
  int i;
  tree next_arg;
  tree cur_arg;
  CUMULATIVE_ARGS args_so_far;
  int store_args_on_stack = (iq2000_can_use_return_insn ());

  /* If struct value address is treated as the first argument.  */
  if (aggregate_value_p (DECL_RESULT (fndecl), fndecl)
      && ! current_function_returns_pcc_struct
      && targetm.calls.struct_value_rtx (TREE_TYPE (fndecl), 1) == 0)
    {
      tree type = build_pointer_type (fntype);
      tree function_result_decl = build_decl (PARM_DECL, NULL_TREE, type);

      DECL_ARG_TYPE (function_result_decl) = type;
      TREE_CHAIN (function_result_decl) = fnargs;
      fnargs = function_result_decl;
    }

  /* For arguments passed in registers, find the register number
     of the first argument in the variable part of the argument list,
     otherwise GP_ARG_LAST+1.  Note also if the last argument is
     the varargs special argument, and treat it as part of the
     variable arguments.

     This is only needed if store_args_on_stack is true.  */
  INIT_CUMULATIVE_ARGS (args_so_far, fntype, NULL_RTX, 0, 0);
  regno = GP_ARG_FIRST;

  for (cur_arg = fnargs; cur_arg != 0; cur_arg = next_arg)
    {
      tree passed_type = DECL_ARG_TYPE (cur_arg);
      enum machine_mode passed_mode = TYPE_MODE (passed_type);
      rtx entry_parm;

      if (TREE_ADDRESSABLE (passed_type))
	{
	  passed_type = build_pointer_type (passed_type);
	  passed_mode = Pmode;
	}

      entry_parm = FUNCTION_ARG (args_so_far, passed_mode, passed_type, 1);

      FUNCTION_ARG_ADVANCE (args_so_far, passed_mode, passed_type, 1);
      next_arg = TREE_CHAIN (cur_arg);

      if (entry_parm && store_args_on_stack)
	{
	  if (next_arg == 0
	      && DECL_NAME (cur_arg)
	      && ((0 == strcmp (IDENTIFIER_POINTER (DECL_NAME (cur_arg)),
				"__builtin_va_alist"))
		  || (0 == strcmp (IDENTIFIER_POINTER (DECL_NAME (cur_arg)),
				   "va_alist"))))
	    {
	      last_arg_is_vararg_marker = 1;
	      break;
	    }
	  else
	    {
	      int words;

	      gcc_assert (GET_CODE (entry_parm) == REG);

	      /* Passed in a register, so will get homed automatically.  */
	      if (GET_MODE (entry_parm) == BLKmode)
		words = (int_size_in_bytes (passed_type) + 3) / 4;
	      else
		words = (GET_MODE_SIZE (GET_MODE (entry_parm)) + 3) / 4;

	      regno = REGNO (entry_parm) + words - 1;
	    }
	}
      else
	{
	  regno = GP_ARG_LAST+1;
	  break;
	}
    }

  /* In order to pass small structures by value in registers we need to
     shift the value into the high part of the register.
     Function_arg has encoded a PARALLEL rtx, holding a vector of
     adjustments to be made as the next_arg_reg variable, so we split up the
     insns, and emit them separately.  */
  next_arg_reg = FUNCTION_ARG (args_so_far, VOIDmode, void_type_node, 1);
  if (next_arg_reg != 0 && GET_CODE (next_arg_reg) == PARALLEL)
    {
      rtvec adjust = XVEC (next_arg_reg, 0);
      int num = GET_NUM_ELEM (adjust);

      for (i = 0; i < num; i++)
	{
	  rtx insn, pattern;

	  pattern = RTVEC_ELT (adjust, i);
	  if (GET_CODE (pattern) != SET
	      || GET_CODE (SET_SRC (pattern)) != ASHIFT)
	    abort_with_insn (pattern, "Insn is not a shift");
	  PUT_CODE (SET_SRC (pattern), ASHIFTRT);

	  insn = emit_insn (pattern);

	  /* Global life information isn't valid at this point, so we
	     can't check whether these shifts are actually used.  Mark
	     them MAYBE_DEAD so that flow2 will remove them, and not
	     complain about dead code in the prologue.  */
	  REG_NOTES(insn) = gen_rtx_EXPR_LIST (REG_MAYBE_DEAD, NULL_RTX,
					       REG_NOTES (insn));
	}
    }

  tsize = compute_frame_size (get_frame_size ());

  /* If this function is a varargs function, store any registers that
     would normally hold arguments ($4 - $7) on the stack.  */
  if (store_args_on_stack
      && ((TYPE_ARG_TYPES (fntype) != 0
	   && (TREE_VALUE (tree_last (TYPE_ARG_TYPES (fntype)))
	       != void_type_node))
	  || last_arg_is_vararg_marker))
    {
      int offset = (regno - GP_ARG_FIRST) * UNITS_PER_WORD;
      rtx ptr = stack_pointer_rtx;

      for (; regno <= GP_ARG_LAST; regno++)
	{
	  if (offset != 0)
	    ptr = gen_rtx_PLUS (Pmode, stack_pointer_rtx, GEN_INT (offset));
	  emit_move_insn (gen_rtx_MEM (gpr_mode, ptr),
			  gen_rtx_REG (gpr_mode, regno));

	  offset += GET_MODE_SIZE (gpr_mode);
	}
    }

  if (tsize > 0)
    {
      rtx tsize_rtx = GEN_INT (tsize);
      rtx adjustment_rtx, insn, dwarf_pattern;

      if (tsize > 32767)
	{
	  adjustment_rtx = gen_rtx_REG (Pmode, IQ2000_TEMP1_REGNUM);
	  emit_move_insn (adjustment_rtx, tsize_rtx);
	}
      else
	adjustment_rtx = tsize_rtx;

      insn = emit_insn (gen_subsi3 (stack_pointer_rtx, stack_pointer_rtx,
				    adjustment_rtx));

      dwarf_pattern = gen_rtx_SET (Pmode, stack_pointer_rtx,
				   plus_constant (stack_pointer_rtx, -tsize));

      iq2000_annotate_frame_insn (insn, dwarf_pattern);

      save_restore_insns (1);

      if (frame_pointer_needed)
	{
	  rtx insn = 0;

	  insn = emit_insn (gen_movsi (hard_frame_pointer_rtx,
				       stack_pointer_rtx));

	  if (insn)
	    RTX_FRAME_RELATED_P (insn) = 1;
	}
    }

  emit_insn (gen_blockage ());
}

/* Expand the epilogue into a bunch of separate insns.  */

void
iq2000_expand_epilogue (void)
{
  HOST_WIDE_INT tsize = cfun->machine->total_size;
  rtx tsize_rtx = GEN_INT (tsize);
  rtx tmp_rtx = (rtx)0;

  if (iq2000_can_use_return_insn ())
    {
      emit_jump_insn (gen_return ());
      return;
    }

  if (tsize > 32767)
    {
      tmp_rtx = gen_rtx_REG (Pmode, IQ2000_TEMP1_REGNUM);
      emit_move_insn (tmp_rtx, tsize_rtx);
      tsize_rtx = tmp_rtx;
    }

  if (tsize > 0)
    {
      if (frame_pointer_needed)
	{
	  emit_insn (gen_blockage ());

	  emit_insn (gen_movsi (stack_pointer_rtx, hard_frame_pointer_rtx));
	}

      save_restore_insns (0);

      if (current_function_calls_eh_return)
	{
	  rtx eh_ofs = EH_RETURN_STACKADJ_RTX;
	  emit_insn (gen_addsi3 (eh_ofs, eh_ofs, tsize_rtx));
	  tsize_rtx = eh_ofs;
	}

      emit_insn (gen_blockage ());

      if (tsize != 0 || current_function_calls_eh_return)
	{
	  emit_insn (gen_addsi3 (stack_pointer_rtx, stack_pointer_rtx,
				 tsize_rtx));
	}
    }

  if (current_function_calls_eh_return)
    {
      /* Perform the additional bump for __throw.  */
      emit_move_insn (gen_rtx_REG (Pmode, HARD_FRAME_POINTER_REGNUM),
		      stack_pointer_rtx);
      emit_insn (gen_rtx_USE (VOIDmode, gen_rtx_REG (Pmode,
						  HARD_FRAME_POINTER_REGNUM)));
      emit_jump_insn (gen_eh_return_internal ());
    }
  else
      emit_jump_insn (gen_return_internal (gen_rtx_REG (Pmode,
						  GP_REG_FIRST + 31)));
}

void
iq2000_expand_eh_return (rtx address)
{
  HOST_WIDE_INT gp_offset = cfun->machine->gp_sp_offset;
  rtx scratch;

  scratch = plus_constant (stack_pointer_rtx, gp_offset);
  emit_move_insn (gen_rtx_MEM (GET_MODE (address), scratch), address);
}

/* Return nonzero if this function is known to have a null epilogue.
   This allows the optimizer to omit jumps to jumps if no stack
   was created.  */

int
iq2000_can_use_return_insn (void)
{
  if (! reload_completed)
    return 0;

  if (regs_ever_live[31] || profile_flag)
    return 0;

  if (cfun->machine->initialized)
    return cfun->machine->total_size == 0;

  return compute_frame_size (get_frame_size ()) == 0;
}

/* Returns nonzero if X contains a SYMBOL_REF.  */

static int
symbolic_expression_p (rtx x)
{
  if (GET_CODE (x) == SYMBOL_REF)
    return 1;

  if (GET_CODE (x) == CONST)
    return symbolic_expression_p (XEXP (x, 0));

  if (UNARY_P (x))
    return symbolic_expression_p (XEXP (x, 0));

  if (ARITHMETIC_P (x))
    return (symbolic_expression_p (XEXP (x, 0))
	    || symbolic_expression_p (XEXP (x, 1)));

  return 0;
}

/* Choose the section to use for the constant rtx expression X that has
   mode MODE.  */

static section *
iq2000_select_rtx_section (enum machine_mode mode, rtx x ATTRIBUTE_UNUSED,
			   unsigned HOST_WIDE_INT align)
{
  /* For embedded applications, always put constants in read-only data,
     in order to reduce RAM usage.  */
  return mergeable_constant_section (mode, align, 0);
}

/* Choose the section to use for DECL.  RELOC is true if its value contains
   any relocatable expression.

   Some of the logic used here needs to be replicated in
   ENCODE_SECTION_INFO in iq2000.h so that references to these symbols
   are done correctly.  */

static section *
iq2000_select_section (tree decl, int reloc ATTRIBUTE_UNUSED,
		       unsigned HOST_WIDE_INT align ATTRIBUTE_UNUSED)
{
  if (TARGET_EMBEDDED_DATA)
    {
      /* For embedded applications, always put an object in read-only data
	 if possible, in order to reduce RAM usage.  */
      if ((TREE_CODE (decl) == VAR_DECL
	   && TREE_READONLY (decl) && !TREE_SIDE_EFFECTS (decl)
	   && DECL_INITIAL (decl)
	   && (DECL_INITIAL (decl) == error_mark_node
	       || TREE_CONSTANT (DECL_INITIAL (decl))))
	  /* Deal with calls from output_constant_def_contents.  */
	  || TREE_CODE (decl) != VAR_DECL)
	return readonly_data_section;
      else
	return data_section;
    }
  else
    {
      /* For hosted applications, always put an object in small data if
	 possible, as this gives the best performance.  */
      if ((TREE_CODE (decl) == VAR_DECL
	   && TREE_READONLY (decl) && !TREE_SIDE_EFFECTS (decl)
	   && DECL_INITIAL (decl)
	   && (DECL_INITIAL (decl) == error_mark_node
	       || TREE_CONSTANT (DECL_INITIAL (decl))))
	  /* Deal with calls from output_constant_def_contents.  */
	  || TREE_CODE (decl) != VAR_DECL)
	return readonly_data_section;
      else
	return data_section;
    }
}
/* Return register to use for a function return value with VALTYPE for function
   FUNC.  */

rtx
iq2000_function_value (tree valtype, tree func ATTRIBUTE_UNUSED)
{
  int reg = GP_RETURN;
  enum machine_mode mode = TYPE_MODE (valtype);
  int unsignedp = TYPE_UNSIGNED (valtype);

  /* Since we define TARGET_PROMOTE_FUNCTION_RETURN that returns true,
     we must promote the mode just as PROMOTE_MODE does.  */
  mode = promote_mode (valtype, mode, &unsignedp, 1);

  return gen_rtx_REG (mode, reg);
}

/* Return true when an argument must be passed by reference.  */

static bool
iq2000_pass_by_reference (CUMULATIVE_ARGS *cum, enum machine_mode mode,
			  tree type, bool named ATTRIBUTE_UNUSED)
{
  int size;

  /* We must pass by reference if we would be both passing in registers
     and the stack.  This is because any subsequent partial arg would be
     handled incorrectly in this case.  */
  if (cum && targetm.calls.must_pass_in_stack (mode, type))
     {
       /* Don't pass the actual CUM to FUNCTION_ARG, because we would
	  get double copies of any offsets generated for small structs
	  passed in registers.  */
       CUMULATIVE_ARGS temp;

       temp = *cum;
       if (FUNCTION_ARG (temp, mode, type, named) != 0)
	 return 1;
     }

  if (type == NULL_TREE || mode == DImode || mode == DFmode)
    return 0;

  size = int_size_in_bytes (type);
  return size == -1 || size > UNITS_PER_WORD;
}

/* Return the length of INSN.  LENGTH is the initial length computed by
   attributes in the machine-description file.  */

int
iq2000_adjust_insn_length (rtx insn, int length)
{
  /* A unconditional jump has an unfilled delay slot if it is not part
     of a sequence.  A conditional jump normally has a delay slot.  */
  if (simplejump_p (insn)
      || (   (GET_CODE (insn) == JUMP_INSN
	   || GET_CODE (insn) == CALL_INSN)))
    length += 4;

  return length;
}

/* Output assembly instructions to perform a conditional branch.

   INSN is the branch instruction.  OPERANDS[0] is the condition.
   OPERANDS[1] is the target of the branch.  OPERANDS[2] is the target
   of the first operand to the condition.  If TWO_OPERANDS_P is
   nonzero the comparison takes two operands; OPERANDS[3] will be the
   second operand.

   If INVERTED_P is nonzero we are to branch if the condition does
   not hold.  If FLOAT_P is nonzero this is a floating-point comparison.

   LENGTH is the length (in bytes) of the sequence we are to generate.
   That tells us whether to generate a simple conditional branch, or a
   reversed conditional branch around a `jr' instruction.  */

char *
iq2000_output_conditional_branch (rtx insn, rtx * operands, int two_operands_p,
				  int float_p, int inverted_p, int length)
{
  static char buffer[200];
  /* The kind of comparison we are doing.  */
  enum rtx_code code = GET_CODE (operands[0]);
  /* Nonzero if the opcode for the comparison needs a `z' indicating
     that it is a comparison against zero.  */
  int need_z_p;
  /* A string to use in the assembly output to represent the first
     operand.  */
  const char *op1 = "%z2";
  /* A string to use in the assembly output to represent the second
     operand.  Use the hard-wired zero register if there's no second
     operand.  */
  const char *op2 = (two_operands_p ? ",%z3" : ",%.");
  /* The operand-printing string for the comparison.  */
  const char *comp = (float_p ? "%F0" : "%C0");
  /* The operand-printing string for the inverted comparison.  */
  const char *inverted_comp = (float_p ? "%W0" : "%N0");

  /* Likely variants of each branch instruction annul the instruction
     in the delay slot if the branch is not taken.  */
  iq2000_branch_likely = (final_sequence && INSN_ANNULLED_BRANCH_P (insn));

  if (!two_operands_p)
    {
      /* To compute whether than A > B, for example, we normally
	 subtract B from A and then look at the sign bit.  But, if we
	 are doing an unsigned comparison, and B is zero, we don't
	 have to do the subtraction.  Instead, we can just check to
	 see if A is nonzero.  Thus, we change the CODE here to
	 reflect the simpler comparison operation.  */
      switch (code)
	{
	case GTU:
	  code = NE;
	  break;

	case LEU:
	  code = EQ;
	  break;

	case GEU:
	  /* A condition which will always be true.  */
	  code = EQ;
	  op1 = "%.";
	  break;

	case LTU:
	  /* A condition which will always be false.  */
	  code = NE;
	  op1 = "%.";
	  break;

	default:
	  /* Not a special case.  */
	  break;
	}
    }

  /* Relative comparisons are always done against zero.  But
     equality comparisons are done between two operands, and therefore
     do not require a `z' in the assembly language output.  */
  need_z_p = (!float_p && code != EQ && code != NE);
  /* For comparisons against zero, the zero is not provided
     explicitly.  */
  if (need_z_p)
    op2 = "";

  /* Begin by terminating the buffer.  That way we can always use
     strcat to add to it.  */
  buffer[0] = '\0';

  switch (length)
    {
    case 4:
    case 8:
      /* Just a simple conditional branch.  */
      if (float_p)
	sprintf (buffer, "b%s%%?\t%%Z2%%1",
		 inverted_p ? inverted_comp : comp);
      else
	sprintf (buffer, "b%s%s%%?\t%s%s,%%1",
		 inverted_p ? inverted_comp : comp,
		 need_z_p ? "z" : "",
		 op1,
		 op2);
      return buffer;

    case 12:
    case 16:
      {
	/* Generate a reversed conditional branch around ` j'
	   instruction:

		.set noreorder
		.set nomacro
		bc    l
		nop
		j     target
		.set macro
		.set reorder
	     l:

	   Because we have to jump four bytes *past* the following
	   instruction if this branch was annulled, we can't just use
	   a label, as in the picture above; there's no way to put the
	   label after the next instruction, as the assembler does not
	   accept `.L+4' as the target of a branch.  (We can't just
	   wait until the next instruction is output; it might be a
	   macro and take up more than four bytes.  Once again, we see
	   why we want to eliminate macros.)

	   If the branch is annulled, we jump four more bytes that we
	   would otherwise; that way we skip the annulled instruction
	   in the delay slot.  */

	const char *target
	  = ((iq2000_branch_likely || length == 16) ? ".+16" : ".+12");
	char *c;

	c = strchr (buffer, '\0');
	/* Generate the reversed comparison.  This takes four
	   bytes.  */
	if (float_p)
	  sprintf (c, "b%s\t%%Z2%s",
		   inverted_p ? comp : inverted_comp,
		   target);
	else
	  sprintf (c, "b%s%s\t%s%s,%s",
		   inverted_p ? comp : inverted_comp,
		   need_z_p ? "z" : "",
		   op1,
		   op2,
		   target);
	strcat (c, "\n\tnop\n\tj\t%1");
	if (length == 16)
	  /* The delay slot was unfilled.  Since we're inside
	     .noreorder, the assembler will not fill in the NOP for
	     us, so we must do it ourselves.  */
	  strcat (buffer, "\n\tnop");
	return buffer;
      }

    default:
      gcc_unreachable ();
    }

  /* NOTREACHED */
  return 0;
}

#define def_builtin(NAME, TYPE, CODE)					\
  lang_hooks.builtin_function ((NAME), (TYPE), (CODE), BUILT_IN_MD,	\
			       NULL, NULL_TREE)

static void
iq2000_init_builtins (void)
{
  tree endlink = void_list_node;
  tree void_ftype, void_ftype_int, void_ftype_int_int;
  tree void_ftype_int_int_int;
  tree int_ftype_int, int_ftype_int_int, int_ftype_int_int_int;
  tree int_ftype_int_int_int_int;

  /* func () */
  void_ftype
    = build_function_type (void_type_node,
			   tree_cons (NULL_TREE, void_type_node, endlink));

  /* func (int) */
  void_ftype_int
    = build_function_type (void_type_node,
			   tree_cons (NULL_TREE, integer_type_node, endlink));

  /* void func (int, int) */
  void_ftype_int_int
    = build_function_type (void_type_node,
                           tree_cons (NULL_TREE, integer_type_node,
                                      tree_cons (NULL_TREE, integer_type_node,
                                                 endlink)));

  /* int func (int) */
  int_ftype_int
    = build_function_type (integer_type_node,
                           tree_cons (NULL_TREE, integer_type_node, endlink));

  /* int func (int, int) */
  int_ftype_int_int
    = build_function_type (integer_type_node,
                           tree_cons (NULL_TREE, integer_type_node,
                                      tree_cons (NULL_TREE, integer_type_node,
                                                 endlink)));

  /* void func (int, int, int) */
void_ftype_int_int_int
    = build_function_type
    (void_type_node,
     tree_cons (NULL_TREE, integer_type_node,
		tree_cons (NULL_TREE, integer_type_node,
			   tree_cons (NULL_TREE,
				      integer_type_node,
				      endlink))));

  /* int func (int, int, int, int) */
  int_ftype_int_int_int_int
    = build_function_type
    (integer_type_node,
     tree_cons (NULL_TREE, integer_type_node,
		tree_cons (NULL_TREE, integer_type_node,
			   tree_cons (NULL_TREE,
				      integer_type_node,
				      tree_cons (NULL_TREE,
						 integer_type_node,
						 endlink)))));

  /* int func (int, int, int) */
  int_ftype_int_int_int
    = build_function_type
    (integer_type_node,
     tree_cons (NULL_TREE, integer_type_node,
		tree_cons (NULL_TREE, integer_type_node,
			   tree_cons (NULL_TREE,
				      integer_type_node,
				      endlink))));

  /* int func (int, int, int, int) */
  int_ftype_int_int_int_int
    = build_function_type
    (integer_type_node,
     tree_cons (NULL_TREE, integer_type_node,
		tree_cons (NULL_TREE, integer_type_node,
			   tree_cons (NULL_TREE,
				      integer_type_node,
				      tree_cons (NULL_TREE,
						 integer_type_node,
						 endlink)))));

  def_builtin ("__builtin_ado16", int_ftype_int_int, IQ2000_BUILTIN_ADO16);
  def_builtin ("__builtin_ram", int_ftype_int_int_int_int, IQ2000_BUILTIN_RAM);
  def_builtin ("__builtin_chkhdr", void_ftype_int_int, IQ2000_BUILTIN_CHKHDR);
  def_builtin ("__builtin_pkrl", void_ftype_int_int, IQ2000_BUILTIN_PKRL);
  def_builtin ("__builtin_cfc0", int_ftype_int, IQ2000_BUILTIN_CFC0);
  def_builtin ("__builtin_cfc1", int_ftype_int, IQ2000_BUILTIN_CFC1);
  def_builtin ("__builtin_cfc2", int_ftype_int, IQ2000_BUILTIN_CFC2);
  def_builtin ("__builtin_cfc3", int_ftype_int, IQ2000_BUILTIN_CFC3);
  def_builtin ("__builtin_ctc0", void_ftype_int_int, IQ2000_BUILTIN_CTC0);
  def_builtin ("__builtin_ctc1", void_ftype_int_int, IQ2000_BUILTIN_CTC1);
  def_builtin ("__builtin_ctc2", void_ftype_int_int, IQ2000_BUILTIN_CTC2);
  def_builtin ("__builtin_ctc3", void_ftype_int_int, IQ2000_BUILTIN_CTC3);
  def_builtin ("__builtin_mfc0", int_ftype_int, IQ2000_BUILTIN_MFC0);
  def_builtin ("__builtin_mfc1", int_ftype_int, IQ2000_BUILTIN_MFC1);
  def_builtin ("__builtin_mfc2", int_ftype_int, IQ2000_BUILTIN_MFC2);
  def_builtin ("__builtin_mfc3", int_ftype_int, IQ2000_BUILTIN_MFC3);
  def_builtin ("__builtin_mtc0", void_ftype_int_int, IQ2000_BUILTIN_MTC0);
  def_builtin ("__builtin_mtc1", void_ftype_int_int, IQ2000_BUILTIN_MTC1);
  def_builtin ("__builtin_mtc2", void_ftype_int_int, IQ2000_BUILTIN_MTC2);
  def_builtin ("__builtin_mtc3", void_ftype_int_int, IQ2000_BUILTIN_MTC3);
  def_builtin ("__builtin_lur", void_ftype_int_int, IQ2000_BUILTIN_LUR);
  def_builtin ("__builtin_rb", void_ftype_int_int, IQ2000_BUILTIN_RB);
  def_builtin ("__builtin_rx", void_ftype_int_int, IQ2000_BUILTIN_RX);
  def_builtin ("__builtin_srrd", void_ftype_int, IQ2000_BUILTIN_SRRD);
  def_builtin ("__builtin_srwr", void_ftype_int_int, IQ2000_BUILTIN_SRWR);
  def_builtin ("__builtin_wb", void_ftype_int_int, IQ2000_BUILTIN_WB);
  def_builtin ("__builtin_wx", void_ftype_int_int, IQ2000_BUILTIN_WX);
  def_builtin ("__builtin_luc32l", void_ftype_int_int, IQ2000_BUILTIN_LUC32L);
  def_builtin ("__builtin_luc64", void_ftype_int_int, IQ2000_BUILTIN_LUC64);
  def_builtin ("__builtin_luc64l", void_ftype_int_int, IQ2000_BUILTIN_LUC64L);
  def_builtin ("__builtin_luk", void_ftype_int_int, IQ2000_BUILTIN_LUK);
  def_builtin ("__builtin_lulck", void_ftype_int, IQ2000_BUILTIN_LULCK);
  def_builtin ("__builtin_lum32", void_ftype_int_int, IQ2000_BUILTIN_LUM32);
  def_builtin ("__builtin_lum32l", void_ftype_int_int, IQ2000_BUILTIN_LUM32L);
  def_builtin ("__builtin_lum64", void_ftype_int_int, IQ2000_BUILTIN_LUM64);
  def_builtin ("__builtin_lum64l", void_ftype_int_int, IQ2000_BUILTIN_LUM64L);
  def_builtin ("__builtin_lurl", void_ftype_int_int, IQ2000_BUILTIN_LURL);
  def_builtin ("__builtin_mrgb", int_ftype_int_int_int, IQ2000_BUILTIN_MRGB);
  def_builtin ("__builtin_srrdl", void_ftype_int, IQ2000_BUILTIN_SRRDL);
  def_builtin ("__builtin_srulck", void_ftype_int, IQ2000_BUILTIN_SRULCK);
  def_builtin ("__builtin_srwru", void_ftype_int_int, IQ2000_BUILTIN_SRWRU);
  def_builtin ("__builtin_trapqfl", void_ftype, IQ2000_BUILTIN_TRAPQFL);
  def_builtin ("__builtin_trapqne", void_ftype, IQ2000_BUILTIN_TRAPQNE);
  def_builtin ("__builtin_traprel", void_ftype_int, IQ2000_BUILTIN_TRAPREL);
  def_builtin ("__builtin_wbu", void_ftype_int_int_int, IQ2000_BUILTIN_WBU);
  def_builtin ("__builtin_syscall", void_ftype, IQ2000_BUILTIN_SYSCALL);
}

/* Builtin for ICODE having ARGCOUNT args in ARGLIST where each arg
   has an rtx CODE.  */

static rtx
expand_one_builtin (enum insn_code icode, rtx target, tree arglist,
		    enum rtx_code *code, int argcount)
{
  rtx pat;
  tree arg [5];
  rtx op [5];
  enum machine_mode mode [5];
  int i;

  mode[0] = insn_data[icode].operand[0].mode;
  for (i = 0; i < argcount; i++)
    {
      arg[i] = TREE_VALUE (arglist);
      arglist = TREE_CHAIN (arglist);
      op[i] = expand_expr (arg[i], NULL_RTX, VOIDmode, 0);
      mode[i] = insn_data[icode].operand[i].mode;
      if (code[i] == CONST_INT && GET_CODE (op[i]) != CONST_INT)
	error ("argument %qd is not a constant", i + 1);
      if (code[i] == REG
	  && ! (*insn_data[icode].operand[i].predicate) (op[i], mode[i]))
	op[i] = copy_to_mode_reg (mode[i], op[i]);
    }

  if (insn_data[icode].operand[0].constraint[0] == '=')
    {
      if (target == 0
	  || GET_MODE (target) != mode[0]
	  || ! (*insn_data[icode].operand[0].predicate) (target, mode[0]))
	target = gen_reg_rtx (mode[0]);
    }
  else
    target = 0;

  switch (argcount)
    {
    case 0:
	pat = GEN_FCN (icode) (target);
    case 1:
      if (target)
	pat = GEN_FCN (icode) (target, op[0]);
      else
	pat = GEN_FCN (icode) (op[0]);
      break;
    case 2:
      if (target)
	pat = GEN_FCN (icode) (target, op[0], op[1]);
      else
	pat = GEN_FCN (icode) (op[0], op[1]);
      break;
    case 3:
      if (target)
	pat = GEN_FCN (icode) (target, op[0], op[1], op[2]);
      else
	pat = GEN_FCN (icode) (op[0], op[1], op[2]);
      break;
    case 4:
      if (target)
	pat = GEN_FCN (icode) (target, op[0], op[1], op[2], op[3]);
      else
	pat = GEN_FCN (icode) (op[0], op[1], op[2], op[3]);
      break;
    default:
      gcc_unreachable ();
    }
  
  if (! pat)
    return 0;
  emit_insn (pat);
  return target;
}

/* Expand an expression EXP that calls a built-in function,
   with result going to TARGET if that's convenient
   (and in mode MODE if that's convenient).
   SUBTARGET may be used as the target for computing one of EXP's operands.
   IGNORE is nonzero if the value is to be ignored.  */

static rtx
iq2000_expand_builtin (tree exp, rtx target, rtx subtarget ATTRIBUTE_UNUSED,
		       enum machine_mode mode ATTRIBUTE_UNUSED,
		       int ignore ATTRIBUTE_UNUSED)
{
  tree fndecl = TREE_OPERAND (TREE_OPERAND (exp, 0), 0);
  tree arglist = TREE_OPERAND (exp, 1);
  int fcode = DECL_FUNCTION_CODE (fndecl);
  enum rtx_code code [5];

  code[0] = REG;
  code[1] = REG;
  code[2] = REG;
  code[3] = REG;
  code[4] = REG;
  switch (fcode)
    {
    default:
      break;
      
    case IQ2000_BUILTIN_ADO16:
      return expand_one_builtin (CODE_FOR_ado16, target, arglist, code, 2);

    case IQ2000_BUILTIN_RAM:
      code[1] = CONST_INT;
      code[2] = CONST_INT;
      code[3] = CONST_INT;
      return expand_one_builtin (CODE_FOR_ram, target, arglist, code, 4);
      
    case IQ2000_BUILTIN_CHKHDR:
      return expand_one_builtin (CODE_FOR_chkhdr, target, arglist, code, 2);
      
    case IQ2000_BUILTIN_PKRL:
      return expand_one_builtin (CODE_FOR_pkrl, target, arglist, code, 2);

    case IQ2000_BUILTIN_CFC0:
      code[0] = CONST_INT;
      return expand_one_builtin (CODE_FOR_cfc0, target, arglist, code, 1);

    case IQ2000_BUILTIN_CFC1:
      code[0] = CONST_INT;
      return expand_one_builtin (CODE_FOR_cfc1, target, arglist, code, 1);

    case IQ2000_BUILTIN_CFC2:
      code[0] = CONST_INT;
      return expand_one_builtin (CODE_FOR_cfc2, target, arglist, code, 1);

    case IQ2000_BUILTIN_CFC3:
      code[0] = CONST_INT;
      return expand_one_builtin (CODE_FOR_cfc3, target, arglist, code, 1);

    case IQ2000_BUILTIN_CTC0:
      code[1] = CONST_INT;
      return expand_one_builtin (CODE_FOR_ctc0, target, arglist, code, 2);

    case IQ2000_BUILTIN_CTC1:
      code[1] = CONST_INT;
      return expand_one_builtin (CODE_FOR_ctc1, target, arglist, code, 2);

    case IQ2000_BUILTIN_CTC2:
      code[1] = CONST_INT;
      return expand_one_builtin (CODE_FOR_ctc2, target, arglist, code, 2);

    case IQ2000_BUILTIN_CTC3:
      code[1] = CONST_INT;
      return expand_one_builtin (CODE_FOR_ctc3, target, arglist, code, 2);

    case IQ2000_BUILTIN_MFC0:
      code[0] = CONST_INT;
      return expand_one_builtin (CODE_FOR_mfc0, target, arglist, code, 1);

    case IQ2000_BUILTIN_MFC1:
      code[0] = CONST_INT;
      return expand_one_builtin (CODE_FOR_mfc1, target, arglist, code, 1);

    case IQ2000_BUILTIN_MFC2:
      code[0] = CONST_INT;
      return expand_one_builtin (CODE_FOR_mfc2, target, arglist, code, 1);

    case IQ2000_BUILTIN_MFC3:
      code[0] = CONST_INT;
      return expand_one_builtin (CODE_FOR_mfc3, target, arglist, code, 1);

    case IQ2000_BUILTIN_MTC0:
      code[1] = CONST_INT;
      return expand_one_builtin (CODE_FOR_mtc0, target, arglist, code, 2);

    case IQ2000_BUILTIN_MTC1:
      code[1] = CONST_INT;
      return expand_one_builtin (CODE_FOR_mtc1, target, arglist, code, 2);

    case IQ2000_BUILTIN_MTC2:
      code[1] = CONST_INT;
      return expand_one_builtin (CODE_FOR_mtc2, target, arglist, code, 2);

    case IQ2000_BUILTIN_MTC3:
      code[1] = CONST_INT;
      return expand_one_builtin (CODE_FOR_mtc3, target, arglist, code, 2);

    case IQ2000_BUILTIN_LUR:
      return expand_one_builtin (CODE_FOR_lur, target, arglist, code, 2);

    case IQ2000_BUILTIN_RB:
      return expand_one_builtin (CODE_FOR_rb, target, arglist, code, 2);

    case IQ2000_BUILTIN_RX:
      return expand_one_builtin (CODE_FOR_rx, target, arglist, code, 2);

    case IQ2000_BUILTIN_SRRD:
      return expand_one_builtin (CODE_FOR_srrd, target, arglist, code, 1);

    case IQ2000_BUILTIN_SRWR:
      return expand_one_builtin (CODE_FOR_srwr, target, arglist, code, 2);

    case IQ2000_BUILTIN_WB:
      return expand_one_builtin (CODE_FOR_wb, target, arglist, code, 2);

    case IQ2000_BUILTIN_WX:
      return expand_one_builtin (CODE_FOR_wx, target, arglist, code, 2);

    case IQ2000_BUILTIN_LUC32L:
      return expand_one_builtin (CODE_FOR_luc32l, target, arglist, code, 2);

    case IQ2000_BUILTIN_LUC64:
      return expand_one_builtin (CODE_FOR_luc64, target, arglist, code, 2);

    case IQ2000_BUILTIN_LUC64L:
      return expand_one_builtin (CODE_FOR_luc64l, target, arglist, code, 2);

    case IQ2000_BUILTIN_LUK:
      return expand_one_builtin (CODE_FOR_luk, target, arglist, code, 2);

    case IQ2000_BUILTIN_LULCK:
      return expand_one_builtin (CODE_FOR_lulck, target, arglist, code, 1);

    case IQ2000_BUILTIN_LUM32:
      return expand_one_builtin (CODE_FOR_lum32, target, arglist, code, 2);

    case IQ2000_BUILTIN_LUM32L:
      return expand_one_builtin (CODE_FOR_lum32l, target, arglist, code, 2);

    case IQ2000_BUILTIN_LUM64:
      return expand_one_builtin (CODE_FOR_lum64, target, arglist, code, 2);

    case IQ2000_BUILTIN_LUM64L:
      return expand_one_builtin (CODE_FOR_lum64l, target, arglist, code, 2);

    case IQ2000_BUILTIN_LURL:
      return expand_one_builtin (CODE_FOR_lurl, target, arglist, code, 2);

    case IQ2000_BUILTIN_MRGB:
      code[2] = CONST_INT;
      return expand_one_builtin (CODE_FOR_mrgb, target, arglist, code, 3);

    case IQ2000_BUILTIN_SRRDL:
      return expand_one_builtin (CODE_FOR_srrdl, target, arglist, code, 1);

    case IQ2000_BUILTIN_SRULCK:
      return expand_one_builtin (CODE_FOR_srulck, target, arglist, code, 1);

    case IQ2000_BUILTIN_SRWRU:
      return expand_one_builtin (CODE_FOR_srwru, target, arglist, code, 2);

    case IQ2000_BUILTIN_TRAPQFL:
      return expand_one_builtin (CODE_FOR_trapqfl, target, arglist, code, 0);

    case IQ2000_BUILTIN_TRAPQNE:
      return expand_one_builtin (CODE_FOR_trapqne, target, arglist, code, 0);

    case IQ2000_BUILTIN_TRAPREL:
      return expand_one_builtin (CODE_FOR_traprel, target, arglist, code, 1);

    case IQ2000_BUILTIN_WBU:
      return expand_one_builtin (CODE_FOR_wbu, target, arglist, code, 3);

    case IQ2000_BUILTIN_SYSCALL:
      return expand_one_builtin (CODE_FOR_syscall, target, arglist, code, 0);
    }
  
  return NULL_RTX;
}

/* Worker function for TARGET_RETURN_IN_MEMORY.  */

static bool
iq2000_return_in_memory (tree type, tree fntype ATTRIBUTE_UNUSED)
{
  return ((int_size_in_bytes (type) > (2 * UNITS_PER_WORD))
	  || (int_size_in_bytes (type) == -1));
}

/* Worker function for TARGET_SETUP_INCOMING_VARARGS.  */

static void
iq2000_setup_incoming_varargs (CUMULATIVE_ARGS *cum,
			       enum machine_mode mode ATTRIBUTE_UNUSED,
			       tree type ATTRIBUTE_UNUSED, int * pretend_size,
			       int no_rtl)
{
  unsigned int iq2000_off = ! cum->last_arg_fp; 
  unsigned int iq2000_fp_off = cum->last_arg_fp; 

  if ((cum->arg_words < MAX_ARGS_IN_REGISTERS - iq2000_off))
    {
      int iq2000_save_gp_regs 
	= MAX_ARGS_IN_REGISTERS - cum->arg_words - iq2000_off; 
      int iq2000_save_fp_regs 
        = (MAX_ARGS_IN_REGISTERS - cum->fp_arg_words - iq2000_fp_off); 

      if (iq2000_save_gp_regs < 0) 
	iq2000_save_gp_regs = 0; 
      if (iq2000_save_fp_regs < 0) 
	iq2000_save_fp_regs = 0; 

      *pretend_size = ((iq2000_save_gp_regs * UNITS_PER_WORD) 
                      + (iq2000_save_fp_regs * UNITS_PER_FPREG)); 

      if (! (no_rtl)) 
	{
	  if (cum->arg_words < MAX_ARGS_IN_REGISTERS - iq2000_off) 
	    {
	      rtx ptr, mem; 
	      ptr = plus_constant (virtual_incoming_args_rtx, 
				   - (iq2000_save_gp_regs 
				      * UNITS_PER_WORD)); 
	      mem = gen_rtx_MEM (BLKmode, ptr); 
	      move_block_from_reg 
		(cum->arg_words + GP_ARG_FIRST + iq2000_off, 
		 mem, 
		 iq2000_save_gp_regs);
	    } 
	} 
    }
}

/* A C compound statement to output to stdio stream STREAM the
   assembler syntax for an instruction operand that is a memory
   reference whose address is ADDR.  ADDR is an RTL expression.  */

void
print_operand_address (FILE * file, rtx addr)
{
  if (!addr)
    error ("PRINT_OPERAND_ADDRESS, null pointer");

  else
    switch (GET_CODE (addr))
      {
      case REG:
	if (REGNO (addr) == ARG_POINTER_REGNUM)
	  abort_with_insn (addr, "Arg pointer not eliminated.");

	fprintf (file, "0(%s)", reg_names [REGNO (addr)]);
	break;

      case LO_SUM:
	{
	  rtx arg0 = XEXP (addr, 0);
	  rtx arg1 = XEXP (addr, 1);

	  if (GET_CODE (arg0) != REG)
	    abort_with_insn (addr,
			     "PRINT_OPERAND_ADDRESS, LO_SUM with #1 not REG.");

	  fprintf (file, "%%lo(");
	  print_operand_address (file, arg1);
	  fprintf (file, ")(%s)", reg_names [REGNO (arg0)]);
	}
	break;

      case PLUS:
	{
	  rtx reg = 0;
	  rtx offset = 0;
	  rtx arg0 = XEXP (addr, 0);
	  rtx arg1 = XEXP (addr, 1);

	  if (GET_CODE (arg0) == REG)
	    {
	      reg = arg0;
	      offset = arg1;
	      if (GET_CODE (offset) == REG)
		abort_with_insn (addr, "PRINT_OPERAND_ADDRESS, 2 regs");
	    }

	  else if (GET_CODE (arg1) == REG)
	      reg = arg1, offset = arg0;
	  else if (CONSTANT_P (arg0) && CONSTANT_P (arg1))
	    {
	      output_addr_const (file, addr);
	      break;
	    }
	  else
	    abort_with_insn (addr, "PRINT_OPERAND_ADDRESS, no regs");

	  if (! CONSTANT_P (offset))
	    abort_with_insn (addr, "PRINT_OPERAND_ADDRESS, invalid insn #2");

	  if (REGNO (reg) == ARG_POINTER_REGNUM)
	    abort_with_insn (addr, "Arg pointer not eliminated.");

	  output_addr_const (file, offset);
	  fprintf (file, "(%s)", reg_names [REGNO (reg)]);
	}
	break;

      case LABEL_REF:
      case SYMBOL_REF:
      case CONST_INT:
      case CONST:
	output_addr_const (file, addr);
	if (GET_CODE (addr) == CONST_INT)
	  fprintf (file, "(%s)", reg_names [0]);
	break;

      default:
	abort_with_insn (addr, "PRINT_OPERAND_ADDRESS, invalid insn #1");
	break;
    }
}

/* A C compound statement to output to stdio stream FILE the
   assembler syntax for an instruction operand OP.

   LETTER is a value that can be used to specify one of several ways
   of printing the operand.  It is used when identical operands
   must be printed differently depending on the context.  LETTER
   comes from the `%' specification that was used to request
   printing of the operand.  If the specification was just `%DIGIT'
   then LETTER is 0; if the specification was `%LTR DIGIT' then LETTER
   is the ASCII code for LTR.

   If OP is a register, this macro should print the register's name.
   The names can be found in an array `reg_names' whose type is
   `char *[]'.  `reg_names' is initialized from `REGISTER_NAMES'.

   When the machine description has a specification `%PUNCT' (a `%'
   followed by a punctuation character), this macro is called with
   a null pointer for X and the punctuation character for LETTER.

   The IQ2000 specific codes are:

   'X'  X is CONST_INT, prints upper 16 bits in hexadecimal format = "0x%04x",
   'x'  X is CONST_INT, prints lower 16 bits in hexadecimal format = "0x%04x",
   'd'  output integer constant in decimal,
   'z'	if the operand is 0, use $0 instead of normal operand.
   'D'  print second part of double-word register or memory operand.
   'L'  print low-order register of double-word register operand.
   'M'  print high-order register of double-word register operand.
   'C'  print part of opcode for a branch condition.
   'F'  print part of opcode for a floating-point branch condition.
   'N'  print part of opcode for a branch condition, inverted.
   'W'  print part of opcode for a floating-point branch condition, inverted.
   'A'	Print part of opcode for a bit test condition.
   'P'  Print label for a bit test.
   'p'  Print log for a bit test.
   'B'  print 'z' for EQ, 'n' for NE
   'b'  print 'n' for EQ, 'z' for NE
   'T'  print 'f' for EQ, 't' for NE
   't'  print 't' for EQ, 'f' for NE
   'Z'  print register and a comma, but print nothing for $fcc0
   '?'	Print 'l' if we are to use a branch likely instead of normal branch.
   '@'	Print the name of the assembler temporary register (at or $1).
   '.'	Print the name of the register with a hard-wired zero (zero or $0).
   '$'	Print the name of the stack pointer register (sp or $29).
   '+'	Print the name of the gp register (gp or $28).  */

void
print_operand (FILE *file, rtx op, int letter)
{
  enum rtx_code code;

  if (PRINT_OPERAND_PUNCT_VALID_P (letter))
    {
      switch (letter)
	{
	case '?':
	  if (iq2000_branch_likely)
	    putc ('l', file);
	  break;

	case '@':
	  fputs (reg_names [GP_REG_FIRST + 1], file);
	  break;

	case '.':
	  fputs (reg_names [GP_REG_FIRST + 0], file);
	  break;

	case '$':
	  fputs (reg_names[STACK_POINTER_REGNUM], file);
	  break;

	case '+':
	  fputs (reg_names[GP_REG_FIRST + 28], file);
	  break;

	default:
	  error ("PRINT_OPERAND: Unknown punctuation '%c'", letter);
	  break;
	}

      return;
    }

  if (! op)
    {
      error ("PRINT_OPERAND null pointer");
      return;
    }

  code = GET_CODE (op);

  if (code == SIGN_EXTEND)
    op = XEXP (op, 0), code = GET_CODE (op);

  if (letter == 'C')
    switch (code)
      {
      case EQ:	fputs ("eq",  file); break;
      case NE:	fputs ("ne",  file); break;
      case GT:	fputs ("gt",  file); break;
      case GE:	fputs ("ge",  file); break;
      case LT:	fputs ("lt",  file); break;
      case LE:	fputs ("le",  file); break;
      case GTU: fputs ("ne", file); break;
      case GEU: fputs ("geu", file); break;
      case LTU: fputs ("ltu", file); break;
      case LEU: fputs ("eq", file); break;
      default:
	abort_with_insn (op, "PRINT_OPERAND, invalid insn for %%C");
      }

  else if (letter == 'N')
    switch (code)
      {
      case EQ:	fputs ("ne",  file); break;
      case NE:	fputs ("eq",  file); break;
      case GT:	fputs ("le",  file); break;
      case GE:	fputs ("lt",  file); break;
      case LT:	fputs ("ge",  file); break;
      case LE:	fputs ("gt",  file); break;
      case GTU: fputs ("leu", file); break;
      case GEU: fputs ("ltu", file); break;
      case LTU: fputs ("geu", file); break;
      case LEU: fputs ("gtu", file); break;
      default:
	abort_with_insn (op, "PRINT_OPERAND, invalid insn for %%N");
      }

  else if (letter == 'F')
    switch (code)
      {
      case EQ: fputs ("c1f", file); break;
      case NE: fputs ("c1t", file); break;
      default:
	abort_with_insn (op, "PRINT_OPERAND, invalid insn for %%F");
      }

  else if (letter == 'W')
    switch (code)
      {
      case EQ: fputs ("c1t", file); break;
      case NE: fputs ("c1f", file); break;
      default:
	abort_with_insn (op, "PRINT_OPERAND, invalid insn for %%W");
      }

  else if (letter == 'A')
    fputs (code == LABEL_REF ? "i" : "in", file);

  else if (letter == 'P')
    {
      if (code == LABEL_REF)
	output_addr_const (file, op);
      else if (code != PC)
	output_operand_lossage ("invalid %%P operand");
    }

  else if (letter == 'p')
    {
      int value;
      if (code != CONST_INT
	  || (value = exact_log2 (INTVAL (op))) < 0)
	output_operand_lossage ("invalid %%p value");
      fprintf (file, "%d", value);
    }

  else if (letter == 'Z')
    {
      gcc_unreachable ();
    }

  else if (code == REG || code == SUBREG)
    {
      int regnum;

      if (code == REG)
	regnum = REGNO (op);
      else
	regnum = true_regnum (op);

      if ((letter == 'M' && ! WORDS_BIG_ENDIAN)
	  || (letter == 'L' && WORDS_BIG_ENDIAN)
	  || letter == 'D')
	regnum++;

      fprintf (file, "%s", reg_names[regnum]);
    }

  else if (code == MEM)
    {
      if (letter == 'D')
	output_address (plus_constant (XEXP (op, 0), 4));
      else
	output_address (XEXP (op, 0));
    }

  else if (code == CONST_DOUBLE
	   && GET_MODE_CLASS (GET_MODE (op)) == MODE_FLOAT)
    {
      char s[60];

      real_to_decimal (s, CONST_DOUBLE_REAL_VALUE (op), sizeof (s), 0, 1);
      fputs (s, file);
    }

  else if (letter == 'x' && GET_CODE (op) == CONST_INT)
    fprintf (file, HOST_WIDE_INT_PRINT_HEX, 0xffff & INTVAL(op));

  else if (letter == 'X' && GET_CODE(op) == CONST_INT)
    fprintf (file, HOST_WIDE_INT_PRINT_HEX, 0xffff & (INTVAL (op) >> 16));

  else if (letter == 'd' && GET_CODE(op) == CONST_INT)
    fprintf (file, HOST_WIDE_INT_PRINT_DEC, (INTVAL(op)));

  else if (letter == 'z' && GET_CODE (op) == CONST_INT && INTVAL (op) == 0)
    fputs (reg_names[GP_REG_FIRST], file);

  else if (letter == 'd' || letter == 'x' || letter == 'X')
    output_operand_lossage ("invalid use of %%d, %%x, or %%X");

  else if (letter == 'B')
    fputs (code == EQ ? "z" : "n", file);
  else if (letter == 'b')
    fputs (code == EQ ? "n" : "z", file);
  else if (letter == 'T')
    fputs (code == EQ ? "f" : "t", file);
  else if (letter == 't')
    fputs (code == EQ ? "t" : "f", file);

  else if (code == CONST && GET_CODE (XEXP (op, 0)) == REG)
    {
      print_operand (file, XEXP (op, 0), letter);
    }

  else
    output_addr_const (file, op);
}

static bool
iq2000_rtx_costs (rtx x, int code, int outer_code ATTRIBUTE_UNUSED, int * total)
{
  enum machine_mode mode = GET_MODE (x);

  switch (code)
    {
    case MEM:
      {
	int num_words = (GET_MODE_SIZE (mode) > UNITS_PER_WORD) ? 2 : 1;

	if (simple_memory_operand (x, mode))
	  return COSTS_N_INSNS (num_words);

	* total = COSTS_N_INSNS (2 * num_words);
	break;
      }
      
    case FFS:
      * total = COSTS_N_INSNS (6);
      break;

    case AND:
    case IOR:
    case XOR:
    case NOT:
      * total = COSTS_N_INSNS (mode == DImode ? 2 : 1);
      break;

    case ASHIFT:
    case ASHIFTRT:
    case LSHIFTRT:
      if (mode == DImode)
	* total = COSTS_N_INSNS ((GET_CODE (XEXP (x, 1)) == CONST_INT) ? 4 : 12);
      else
	* total = COSTS_N_INSNS (1);
    break;								

    case ABS:
      if (mode == SFmode || mode == DFmode)
	* total = COSTS_N_INSNS (1);
      else
	* total = COSTS_N_INSNS (4);
      break;
    
    case PLUS:
    case MINUS:
      if (mode == SFmode || mode == DFmode)
	* total = COSTS_N_INSNS (6);
      else if (mode == DImode)
	* total = COSTS_N_INSNS (4);
      else
	* total = COSTS_N_INSNS (1);
      break;
    
    case NEG:
      * total = (mode == DImode) ? 4 : 1;
      break;

    case MULT:
      if (mode == SFmode)
	* total = COSTS_N_INSNS (7);
      else if (mode == DFmode)
	* total = COSTS_N_INSNS (8);
      else
	* total = COSTS_N_INSNS (10);
      break;

    case DIV:
    case MOD:
      if (mode == SFmode)
	* total = COSTS_N_INSNS (23);
      else if (mode == DFmode)
	* total = COSTS_N_INSNS (36);
      else
	* total = COSTS_N_INSNS (69);
      break;
      
    case UDIV:
    case UMOD:
      * total = COSTS_N_INSNS (69);
      break;
      
    case SIGN_EXTEND:
      * total = COSTS_N_INSNS (2);
      break;
    
    case ZERO_EXTEND:
      * total = COSTS_N_INSNS (1);
      break;

    case CONST_INT:
      * total = 0;
      break;
    
    case LABEL_REF:
      * total = COSTS_N_INSNS (2);
      break;

    case CONST:
      {
	rtx offset = const0_rtx;
	rtx symref = eliminate_constant_term (XEXP (x, 0), & offset);

	if (GET_CODE (symref) == LABEL_REF)
	  * total = COSTS_N_INSNS (2);
	else if (GET_CODE (symref) != SYMBOL_REF)
	  * total = COSTS_N_INSNS (4);
	/* Let's be paranoid....  */
	else if (INTVAL (offset) < -32768 || INTVAL (offset) > 32767)
	  * total = COSTS_N_INSNS (2);
	else
	  * total = COSTS_N_INSNS (SYMBOL_REF_FLAG (symref) ? 1 : 2);
	break;
      }

    case SYMBOL_REF:
      * total = COSTS_N_INSNS (SYMBOL_REF_FLAG (x) ? 1 : 2);
      break;
    
    case CONST_DOUBLE:
      {
	rtx high, low;
      
	split_double (x, & high, & low);
      
	* total = COSTS_N_INSNS (  (high == CONST0_RTX (GET_MODE (high))
				  || low == CONST0_RTX (GET_MODE (low)))
				   ? 2 : 4);
	break;
      }
    
    default:
      return false;
    }
  return true;
}

#include "gt-iq2000.h"
