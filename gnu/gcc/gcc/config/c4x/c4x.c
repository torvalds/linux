/* Subroutines for assembler code output on the TMS320C[34]x
   Copyright (C) 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2003,
   2004, 2005
   Free Software Foundation, Inc.

   Contributed by Michael Hayes (m.hayes@elec.canterbury.ac.nz)
              and Herman Ten Brugge (Haj.Ten.Brugge@net.HCC.nl).

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

/* Some output-actions in c4x.md need these.  */
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "tree.h"
#include "regs.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#include "real.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "conditions.h"
#include "output.h"
#include "function.h"
#include "expr.h"
#include "optabs.h"
#include "libfuncs.h"
#include "flags.h"
#include "recog.h"
#include "ggc.h"
#include "cpplib.h"
#include "toplev.h"
#include "tm_p.h"
#include "target.h"
#include "target-def.h"
#include "langhooks.h"

rtx smulhi3_libfunc;
rtx umulhi3_libfunc;
rtx fix_truncqfhi2_libfunc;
rtx fixuns_truncqfhi2_libfunc;
rtx fix_trunchfhi2_libfunc;
rtx fixuns_trunchfhi2_libfunc;
rtx floathiqf2_libfunc;
rtx floatunshiqf2_libfunc;
rtx floathihf2_libfunc;
rtx floatunshihf2_libfunc;

static int c4x_leaf_function;

static const char *const float_reg_names[] = FLOAT_REGISTER_NAMES;

/* Array of the smallest class containing reg number REGNO, indexed by
   REGNO.  Used by REGNO_REG_CLASS in c4x.h.  We assume that all these
   registers are available and set the class to NO_REGS for registers 
   that the target switches say are unavailable.  */

enum reg_class c4x_regclass_map[FIRST_PSEUDO_REGISTER] =
{
                                /* Reg          Modes           Saved.  */
  R0R1_REGS,			/* R0           QI, QF, HF      No.  */
  R0R1_REGS,			/* R1           QI, QF, HF      No.  */
  R2R3_REGS,			/* R2           QI, QF, HF      No.  */
  R2R3_REGS,			/* R3           QI, QF, HF      No.  */
  EXT_LOW_REGS,			/* R4           QI, QF, HF      QI.  */
  EXT_LOW_REGS,			/* R5           QI, QF, HF      QI.  */
  EXT_LOW_REGS,			/* R6           QI, QF, HF      QF.  */
  EXT_LOW_REGS,			/* R7           QI, QF, HF      QF.  */
  ADDR_REGS,			/* AR0          QI              No.  */
  ADDR_REGS,			/* AR1          QI              No.  */
  ADDR_REGS,			/* AR2          QI              No.  */
  ADDR_REGS,			/* AR3          QI              QI.  */
  ADDR_REGS,			/* AR4          QI              QI.  */
  ADDR_REGS,			/* AR5          QI              QI.  */
  ADDR_REGS,			/* AR6          QI              QI.  */
  ADDR_REGS,			/* AR7          QI              QI.  */
  DP_REG,			/* DP           QI              No.  */
  INDEX_REGS,			/* IR0          QI              No.  */
  INDEX_REGS,			/* IR1          QI              No.  */
  BK_REG,			/* BK           QI              QI.  */
  SP_REG,			/* SP           QI              No.  */
  ST_REG,			/* ST           CC              No.  */
  NO_REGS,			/* DIE/IE                       No.  */
  NO_REGS,			/* IIE/IF                       No.  */
  NO_REGS,			/* IIF/IOF                      No.  */
  INT_REGS,			/* RS           QI              No.  */
  INT_REGS,			/* RE           QI              No.  */
  RC_REG,			/* RC           QI              No.  */
  EXT_REGS,			/* R8           QI, QF, HF      QI.  */
  EXT_REGS,			/* R9           QI, QF, HF      No.  */
  EXT_REGS,			/* R10          QI, QF, HF      No.  */
  EXT_REGS,			/* R11          QI, QF, HF      No.  */
};

enum machine_mode c4x_caller_save_map[FIRST_PSEUDO_REGISTER] =
{
                                /* Reg          Modes           Saved.  */
  HFmode,			/* R0           QI, QF, HF      No.  */
  HFmode,			/* R1           QI, QF, HF      No.  */
  HFmode,			/* R2           QI, QF, HF      No.  */
  HFmode,			/* R3           QI, QF, HF      No.  */
  QFmode,			/* R4           QI, QF, HF      QI.  */
  QFmode,			/* R5           QI, QF, HF      QI.  */
  QImode,			/* R6           QI, QF, HF      QF.  */
  QImode,			/* R7           QI, QF, HF      QF.  */
  QImode,			/* AR0          QI              No.  */
  QImode,			/* AR1          QI              No.  */
  QImode,			/* AR2          QI              No.  */
  QImode,			/* AR3          QI              QI.  */
  QImode,			/* AR4          QI              QI.  */
  QImode,			/* AR5          QI              QI.  */
  QImode,			/* AR6          QI              QI.  */
  QImode,			/* AR7          QI              QI.  */
  VOIDmode,			/* DP           QI              No.  */
  QImode,			/* IR0          QI              No.  */
  QImode,			/* IR1          QI              No.  */
  QImode,			/* BK           QI              QI.  */
  VOIDmode,			/* SP           QI              No.  */
  VOIDmode,			/* ST           CC              No.  */
  VOIDmode,			/* DIE/IE                       No.  */
  VOIDmode,			/* IIE/IF                       No.  */
  VOIDmode,			/* IIF/IOF                      No.  */
  QImode,			/* RS           QI              No.  */
  QImode,			/* RE           QI              No.  */
  VOIDmode,			/* RC           QI              No.  */
  QFmode,			/* R8           QI, QF, HF      QI.  */
  HFmode,			/* R9           QI, QF, HF      No.  */
  HFmode,			/* R10          QI, QF, HF      No.  */
  HFmode,			/* R11          QI, QF, HF      No.  */
};


/* Test and compare insns in c4x.md store the information needed to
   generate branch and scc insns here.  */

rtx c4x_compare_op0;
rtx c4x_compare_op1;

int c4x_cpu_version = 40;	/* CPU version C30/31/32/33/40/44.  */

/* Pragma definitions.  */

tree code_tree = NULL_TREE;
tree data_tree = NULL_TREE;
tree pure_tree = NULL_TREE;
tree noreturn_tree = NULL_TREE;
tree interrupt_tree = NULL_TREE;
tree naked_tree = NULL_TREE;

/* Forward declarations */
static bool c4x_handle_option (size_t, const char *, int);
static int c4x_isr_reg_used_p (unsigned int);
static int c4x_leaf_function_p (void);
static int c4x_naked_function_p (void);
static int c4x_immed_int_constant (rtx);
static int c4x_immed_float_constant (rtx);
static int c4x_R_indirect (rtx);
static void c4x_S_address_parse (rtx , int *, int *, int *, int *);
static int c4x_valid_operands (enum rtx_code, rtx *, enum machine_mode, int);
static int c4x_arn_reg_operand (rtx, enum machine_mode, unsigned int);
static int c4x_arn_mem_operand (rtx, enum machine_mode, unsigned int);
static void c4x_file_start (void);
static void c4x_file_end (void);
static void c4x_check_attribute (const char *, tree, tree, tree *);
static int c4x_r11_set_p (rtx);
static int c4x_rptb_valid_p (rtx, rtx);
static void c4x_reorg (void);
static int c4x_label_ref_used_p (rtx, rtx);
static tree c4x_handle_fntype_attribute (tree *, tree, tree, int, bool *);
const struct attribute_spec c4x_attribute_table[];
static void c4x_insert_attributes (tree, tree *);
static void c4x_asm_named_section (const char *, unsigned int, tree);
static int c4x_adjust_cost (rtx, rtx, rtx, int);
static void c4x_globalize_label (FILE *, const char *);
static bool c4x_rtx_costs (rtx, int, int, int *);
static int c4x_address_cost (rtx);
static void c4x_init_libfuncs (void);
static void c4x_external_libcall (rtx);
static rtx c4x_struct_value_rtx (tree, int);
static tree c4x_gimplify_va_arg_expr (tree, tree, tree *, tree *);

/* Initialize the GCC target structure.  */
#undef TARGET_ASM_BYTE_OP
#define TARGET_ASM_BYTE_OP "\t.word\t"
#undef TARGET_ASM_ALIGNED_HI_OP
#define TARGET_ASM_ALIGNED_HI_OP NULL
#undef TARGET_ASM_ALIGNED_SI_OP
#define TARGET_ASM_ALIGNED_SI_OP NULL
#undef TARGET_ASM_FILE_START
#define TARGET_ASM_FILE_START c4x_file_start
#undef TARGET_ASM_FILE_START_FILE_DIRECTIVE
#define TARGET_ASM_FILE_START_FILE_DIRECTIVE true
#undef TARGET_ASM_FILE_END
#define TARGET_ASM_FILE_END c4x_file_end

#undef TARGET_ASM_EXTERNAL_LIBCALL
#define TARGET_ASM_EXTERNAL_LIBCALL c4x_external_libcall

/* Play safe, not the fastest code.  */
#undef TARGET_DEFAULT_TARGET_FLAGS
#define TARGET_DEFAULT_TARGET_FLAGS (MASK_ALIASES | MASK_PARALLEL \
				     | MASK_PARALLEL_MPY | MASK_RPTB)
#undef TARGET_HANDLE_OPTION
#define TARGET_HANDLE_OPTION c4x_handle_option

#undef TARGET_ATTRIBUTE_TABLE
#define TARGET_ATTRIBUTE_TABLE c4x_attribute_table

#undef TARGET_INSERT_ATTRIBUTES
#define TARGET_INSERT_ATTRIBUTES c4x_insert_attributes

#undef TARGET_INIT_BUILTINS
#define TARGET_INIT_BUILTINS c4x_init_builtins

#undef TARGET_EXPAND_BUILTIN
#define TARGET_EXPAND_BUILTIN c4x_expand_builtin

#undef TARGET_SCHED_ADJUST_COST
#define TARGET_SCHED_ADJUST_COST c4x_adjust_cost

#undef TARGET_ASM_GLOBALIZE_LABEL
#define TARGET_ASM_GLOBALIZE_LABEL c4x_globalize_label

#undef TARGET_RTX_COSTS
#define TARGET_RTX_COSTS c4x_rtx_costs
#undef TARGET_ADDRESS_COST
#define TARGET_ADDRESS_COST c4x_address_cost

#undef TARGET_MACHINE_DEPENDENT_REORG
#define TARGET_MACHINE_DEPENDENT_REORG c4x_reorg

#undef TARGET_INIT_LIBFUNCS
#define TARGET_INIT_LIBFUNCS c4x_init_libfuncs

#undef TARGET_STRUCT_VALUE_RTX
#define TARGET_STRUCT_VALUE_RTX c4x_struct_value_rtx

#undef TARGET_GIMPLIFY_VA_ARG_EXPR
#define TARGET_GIMPLIFY_VA_ARG_EXPR c4x_gimplify_va_arg_expr

struct gcc_target targetm = TARGET_INITIALIZER;

/* Implement TARGET_HANDLE_OPTION.  */

static bool
c4x_handle_option (size_t code, const char *arg, int value)
{
  switch (code)
    {
    case OPT_m30: c4x_cpu_version = 30; return true;
    case OPT_m31: c4x_cpu_version = 31; return true;
    case OPT_m32: c4x_cpu_version = 32; return true;
    case OPT_m33: c4x_cpu_version = 33; return true;
    case OPT_m40: c4x_cpu_version = 40; return true;
    case OPT_m44: c4x_cpu_version = 44; return true;

    case OPT_mcpu_:
      if (arg[0] == 'c' || arg[0] == 'C')
	arg++;
      value = atoi (arg);
      switch (value)
	{
	case 30: case 31: case 32: case 33: case 40: case 44:
	  c4x_cpu_version = value;
	  return true;
	}
      return false;

    default:
      return true;
    }
}

/* Override command line options.
   Called once after all options have been parsed.
   Mostly we process the processor
   type and sometimes adjust other TARGET_ options.  */

void
c4x_override_options (void)
{
  /* Convert foo / 8.0 into foo * 0.125, etc.  */
  set_fast_math_flags (1);

  /* We should phase out the following at some stage.
     This provides compatibility with the old -mno-aliases option.  */
  if (! TARGET_ALIASES && ! flag_argument_noalias)
    flag_argument_noalias = 1;

  if (!TARGET_C3X)
    target_flags |= MASK_MPYI | MASK_DB;

  if (optimize < 2)
    target_flags &= ~(MASK_RPTB | MASK_PARALLEL);

  if (!TARGET_PARALLEL)
    target_flags &= ~MASK_PARALLEL_MPY;
}


/* This is called before c4x_override_options.  */

void
c4x_optimization_options (int level ATTRIBUTE_UNUSED,
			  int size ATTRIBUTE_UNUSED)
{
  /* Scheduling before register allocation can screw up global
     register allocation, especially for functions that use MPY||ADD
     instructions.  The benefit we gain we get by scheduling before
     register allocation is probably marginal anyhow.  */
  flag_schedule_insns = 0;
}


/* Write an ASCII string.  */

#define C4X_ASCII_LIMIT 40

void
c4x_output_ascii (FILE *stream, const char *ptr, int len)
{
  char sbuf[C4X_ASCII_LIMIT + 1];
  int s, l, special, first = 1, onlys;

  if (len)
      fprintf (stream, "\t.byte\t");

  for (s = l = 0; len > 0; --len, ++ptr)
    {
      onlys = 0;

      /* Escape " and \ with a \".  */
      special = *ptr == '\"' || *ptr == '\\';

      /* If printable - add to buff.  */
      if ((! TARGET_TI || ! special) && *ptr >= 0x20 && *ptr < 0x7f)
	{
	  if (special)
	    sbuf[s++] = '\\';
	  sbuf[s++] = *ptr;
	  if (s < C4X_ASCII_LIMIT - 1)
	    continue;
	  onlys = 1;
	}
      if (s)
	{
	  if (first)
	    first = 0;
	  else
	    {
	      fputc (',', stream);
	      l++;
	    }

	  sbuf[s] = 0;
	  fprintf (stream, "\"%s\"", sbuf);
	  l += s + 2;
	  if (TARGET_TI && l >= 80 && len > 1)
	    {
	      fprintf (stream, "\n\t.byte\t");
	      first = 1;
	      l = 0;
	    }
	
	  s = 0;
	}
      if (onlys)
	continue;

      if (first)
	first = 0;
      else
	{
	  fputc (',', stream);
	  l++;
	}

      fprintf (stream, "%d", *ptr);
      l += 3;
      if (TARGET_TI && l >= 80 && len > 1)
	{
	  fprintf (stream, "\n\t.byte\t");
	  first = 1;
	  l = 0;
	}
    }
  if (s)
    {
      if (! first)
	fputc (',', stream);

      sbuf[s] = 0;
      fprintf (stream, "\"%s\"", sbuf);
      s = 0;
    }
  fputc ('\n', stream);
}


int
c4x_hard_regno_mode_ok (unsigned int regno, enum machine_mode mode)
{
  switch (mode)
    {
#if Pmode != QImode
    case Pmode:			/* Pointer (24/32 bits).  */
#endif
    case QImode:		/* Integer (32 bits).  */
      return IS_INT_REGNO (regno);

    case QFmode:		/* Float, Double (32 bits).  */
    case HFmode:		/* Long Double (40 bits).  */
      return IS_EXT_REGNO (regno);

    case CCmode:		/* Condition Codes.  */
    case CC_NOOVmode:		/* Condition Codes.  */
      return IS_ST_REGNO (regno);

    case HImode:		/* Long Long (64 bits).  */
      /* We need two registers to store long longs.  Note that 
	 it is much easier to constrain the first register
	 to start on an even boundary.  */
      return IS_INT_REGNO (regno)
	&& IS_INT_REGNO (regno + 1)
	&& (regno & 1) == 0;

    default:
      return 0;			/* We don't support these modes.  */
    }

  return 0;
}

/* Return nonzero if REGNO1 can be renamed to REGNO2.  */
int
c4x_hard_regno_rename_ok (unsigned int regno1, unsigned int regno2)
{
  /* We cannot copy call saved registers from mode QI into QF or from
     mode QF into QI.  */
  if (IS_FLOAT_CALL_SAVED_REGNO (regno1) && IS_INT_CALL_SAVED_REGNO (regno2))
    return 0;
  if (IS_INT_CALL_SAVED_REGNO (regno1) && IS_FLOAT_CALL_SAVED_REGNO (regno2))
    return 0;
  /* We cannot copy from an extended (40 bit) register to a standard
     (32 bit) register because we only set the condition codes for
     extended registers.  */
  if (IS_EXT_REGNO (regno1) && ! IS_EXT_REGNO (regno2))
    return 0;
  if (IS_EXT_REGNO (regno2) && ! IS_EXT_REGNO (regno1))
    return 0;
  return 1;
}

/* The TI C3x C compiler register argument runtime model uses 6 registers,
   AR2, R2, R3, RC, RS, RE.

   The first two floating point arguments (float, double, long double)
   that are found scanning from left to right are assigned to R2 and R3.

   The remaining integer (char, short, int, long) or pointer arguments
   are assigned to the remaining registers in the order AR2, R2, R3,
   RC, RS, RE when scanning left to right, except for the last named
   argument prior to an ellipsis denoting variable number of
   arguments.  We don't have to worry about the latter condition since
   function.c treats the last named argument as anonymous (unnamed).

   All arguments that cannot be passed in registers are pushed onto
   the stack in reverse order (right to left).  GCC handles that for us.

   c4x_init_cumulative_args() is called at the start, so we can parse
   the args to see how many floating point arguments and how many
   integer (or pointer) arguments there are.  c4x_function_arg() is
   then called (sometimes repeatedly) for each argument (parsed left
   to right) to obtain the register to pass the argument in, or zero
   if the argument is to be passed on the stack.  Once the compiler is
   happy, c4x_function_arg_advance() is called.

   Don't use R0 to pass arguments in, we use 0 to indicate a stack
   argument.  */

static const int c4x_int_reglist[3][6] =
{
  {AR2_REGNO, R2_REGNO, R3_REGNO, RC_REGNO, RS_REGNO, RE_REGNO},
  {AR2_REGNO, R3_REGNO, RC_REGNO, RS_REGNO, RE_REGNO, 0},
  {AR2_REGNO, RC_REGNO, RS_REGNO, RE_REGNO, 0, 0}
};

static const int c4x_fp_reglist[2] = {R2_REGNO, R3_REGNO};


/* Initialize a variable CUM of type CUMULATIVE_ARGS for a call to a
   function whose data type is FNTYPE.
   For a library call, FNTYPE is  0.  */

void
c4x_init_cumulative_args (CUMULATIVE_ARGS *cum, tree fntype, rtx libname)
{
  tree param, next_param;

  cum->floats = cum->ints = 0;
  cum->init = 0;
  cum->var = 0;
  cum->args = 0;

  if (TARGET_DEBUG)
    {
      fprintf (stderr, "\nc4x_init_cumulative_args (");
      if (fntype)
	{
	  tree ret_type = TREE_TYPE (fntype);

	  fprintf (stderr, "fntype code = %s, ret code = %s",
		   tree_code_name[(int) TREE_CODE (fntype)],
		   tree_code_name[(int) TREE_CODE (ret_type)]);
	}
      else
	fprintf (stderr, "no fntype");

      if (libname)
	fprintf (stderr, ", libname = %s", XSTR (libname, 0));
    }

  cum->prototype = (fntype && TYPE_ARG_TYPES (fntype));

  for (param = fntype ? TYPE_ARG_TYPES (fntype) : 0;
       param; param = next_param)
    {
      tree type;

      next_param = TREE_CHAIN (param);

      type = TREE_VALUE (param);
      if (type && type != void_type_node)
	{
	  enum machine_mode mode;

	  /* If the last arg doesn't have void type then we have
	     variable arguments.  */
	  if (! next_param)
	    cum->var = 1;

	  if ((mode = TYPE_MODE (type)))
	    {
	      if (! targetm.calls.must_pass_in_stack (mode, type))
		{
		  /* Look for float, double, or long double argument.  */
		  if (mode == QFmode || mode == HFmode)
		    cum->floats++;
		  /* Look for integer, enumeral, boolean, char, or pointer
		     argument.  */
		  else if (mode == QImode || mode == Pmode)
		    cum->ints++;
		}
	    }
	  cum->args++;
	}
    }

  if (TARGET_DEBUG)
    fprintf (stderr, "%s%s, args = %d)\n",
	     cum->prototype ? ", prototype" : "",
	     cum->var ? ", variable args" : "",
	     cum->args);
}


/* Update the data in CUM to advance over an argument
   of mode MODE and data type TYPE.
   (TYPE is null for libcalls where that information may not be available.)  */

void
c4x_function_arg_advance (CUMULATIVE_ARGS *cum, enum machine_mode mode,
			  tree type, int named)
{
  if (TARGET_DEBUG)
    fprintf (stderr, "c4x_function_adv(mode=%s, named=%d)\n\n",
	     GET_MODE_NAME (mode), named);
  if (! TARGET_MEMPARM 
      && named
      && type
      && ! targetm.calls.must_pass_in_stack (mode, type))
    {
      /* Look for float, double, or long double argument.  */
      if (mode == QFmode || mode == HFmode)
	cum->floats++;
      /* Look for integer, enumeral, boolean, char, or pointer argument.  */
      else if (mode == QImode || mode == Pmode)
	cum->ints++;
    }
  else if (! TARGET_MEMPARM && ! type)
    {
      /* Handle libcall arguments.  */
      if (mode == QFmode || mode == HFmode)
	cum->floats++;
      else if (mode == QImode || mode == Pmode)
	cum->ints++;
    }
  return;
}


/* Define where to put the arguments to a function.  Value is zero to
   push the argument on the stack, or a hard register in which to
   store the argument.

   MODE is the argument's machine mode.
   TYPE is the data type of the argument (as a tree).
   This is null for libcalls where that information may
   not be available.
   CUM is a variable of type CUMULATIVE_ARGS which gives info about
   the preceding args and about the function being called.
   NAMED is nonzero if this argument is a named parameter
   (otherwise it is an extra parameter matching an ellipsis).  */

struct rtx_def *
c4x_function_arg (CUMULATIVE_ARGS *cum, enum machine_mode mode,
		  tree type, int named)
{
  int reg = 0;			/* Default to passing argument on stack.  */

  if (! cum->init)
    {
      /* We can handle at most 2 floats in R2, R3.  */
      cum->maxfloats = (cum->floats > 2) ? 2 : cum->floats;

      /* We can handle at most 6 integers minus number of floats passed 
	 in registers.  */
      cum->maxints = (cum->ints > 6 - cum->maxfloats) ? 
	6 - cum->maxfloats : cum->ints;

      /* If there is no prototype, assume all the arguments are integers.  */
      if (! cum->prototype)
	cum->maxints = 6;

      cum->ints = cum->floats = 0;
      cum->init = 1;
    }

  /* This marks the last argument.  We don't need to pass this through
     to the call insn.  */
  if (type == void_type_node)
    return 0;

  if (! TARGET_MEMPARM 
      && named 
      && type
      && ! targetm.calls.must_pass_in_stack (mode, type))
    {
      /* Look for float, double, or long double argument.  */
      if (mode == QFmode || mode == HFmode)
	{
	  if (cum->floats < cum->maxfloats)
	    reg = c4x_fp_reglist[cum->floats];
	}
      /* Look for integer, enumeral, boolean, char, or pointer argument.  */
      else if (mode == QImode || mode == Pmode)
	{
	  if (cum->ints < cum->maxints)
	    reg = c4x_int_reglist[cum->maxfloats][cum->ints];
	}
    }
  else if (! TARGET_MEMPARM && ! type)
    {
      /* We could use a different argument calling model for libcalls,
         since we're only calling functions in libgcc.  Thus we could
         pass arguments for long longs in registers rather than on the
         stack.  In the meantime, use the odd TI format.  We make the
         assumption that we won't have more than two floating point
         args, six integer args, and that all the arguments are of the
         same mode.  */
      if (mode == QFmode || mode == HFmode)
	reg = c4x_fp_reglist[cum->floats];
      else if (mode == QImode || mode == Pmode)
	reg = c4x_int_reglist[0][cum->ints];
    }

  if (TARGET_DEBUG)
    {
      fprintf (stderr, "c4x_function_arg(mode=%s, named=%d",
	       GET_MODE_NAME (mode), named);
      if (reg)
	fprintf (stderr, ", reg=%s", reg_names[reg]);
      else
	fprintf (stderr, ", stack");
      fprintf (stderr, ")\n");
    }
  if (reg)
    return gen_rtx_REG (mode, reg);
  else
    return NULL_RTX;
}

/* C[34]x arguments grow in weird ways (downwards) that the standard
   varargs stuff can't handle..  */

static tree
c4x_gimplify_va_arg_expr (tree valist, tree type,
			  tree *pre_p ATTRIBUTE_UNUSED,
			  tree *post_p ATTRIBUTE_UNUSED)
{
  tree t;
  bool indirect;

  indirect = pass_by_reference (NULL, TYPE_MODE (type), type, false);
  if (indirect)
    type = build_pointer_type (type);

  t = build2 (PREDECREMENT_EXPR, TREE_TYPE (valist), valist,
	      build_int_cst (NULL_TREE, int_size_in_bytes (type)));
  t = fold_convert (build_pointer_type (type), t);
  t = build_va_arg_indirect_ref (t);

  if (indirect)
    t = build_va_arg_indirect_ref (t);

  return t;
}


static int
c4x_isr_reg_used_p (unsigned int regno)
{
  /* Don't save/restore FP or ST, we handle them separately.  */
  if (regno == FRAME_POINTER_REGNUM
      || IS_ST_REGNO (regno))
    return 0;

  /* We could be a little smarter abut saving/restoring DP.
     We'll only save if for the big memory model or if
     we're paranoid. ;-)  */
  if (IS_DP_REGNO (regno))
    return ! TARGET_SMALL || TARGET_PARANOID;

  /* Only save/restore regs in leaf function that are used.  */
  if (c4x_leaf_function)
    return regs_ever_live[regno] && fixed_regs[regno] == 0;

  /* Only save/restore regs that are used by the ISR and regs
     that are likely to be used by functions the ISR calls
     if they are not fixed.  */
  return IS_EXT_REGNO (regno)
    || ((regs_ever_live[regno] || call_used_regs[regno]) 
	&& fixed_regs[regno] == 0);
}


static int
c4x_leaf_function_p (void)
{
  /* A leaf function makes no calls, so we only need
     to save/restore the registers we actually use.
     For the global variable leaf_function to be set, we need
     to define LEAF_REGISTERS and all that it entails.
     Let's check ourselves....  */

  if (lookup_attribute ("leaf_pretend",
			TYPE_ATTRIBUTES (TREE_TYPE (current_function_decl))))
    return 1;

  /* Use the leaf_pretend attribute at your own risk.  This is a hack
     to speed up ISRs that call a function infrequently where the
     overhead of saving and restoring the additional registers is not
     warranted.  You must save and restore the additional registers
     required by the called function.  Caveat emptor.  Here's enough
     rope...  */

  if (leaf_function_p ())
    return 1;

  return 0;
}


static int
c4x_naked_function_p (void)
{
  tree type;

  type = TREE_TYPE (current_function_decl);
  return lookup_attribute ("naked", TYPE_ATTRIBUTES (type)) != NULL;
}


int
c4x_interrupt_function_p (void)
{
  const char *cfun_name;
  if (lookup_attribute ("interrupt",
			TYPE_ATTRIBUTES (TREE_TYPE (current_function_decl))))
    return 1;

  /* Look for TI style c_intnn.  */
  cfun_name = current_function_name ();
  return cfun_name[0] == 'c'
    && cfun_name[1] == '_'
    && cfun_name[2] == 'i'
    && cfun_name[3] == 'n' 
    && cfun_name[4] == 't'
    && ISDIGIT (cfun_name[5])
    && ISDIGIT (cfun_name[6]);
}

void
c4x_expand_prologue (void)
{
  unsigned int regno;
  int size = get_frame_size ();
  rtx insn;

  /* In functions where ar3 is not used but frame pointers are still
     specified, frame pointers are not adjusted (if >= -O2) and this
     is used so it won't needlessly push the frame pointer.  */
  int dont_push_ar3;

  /* For __naked__ function don't build a prologue.  */
  if (c4x_naked_function_p ())
    {
      return;
    }
  
  /* For __interrupt__ function build specific prologue.  */
  if (c4x_interrupt_function_p ())
    {
      c4x_leaf_function = c4x_leaf_function_p ();
      
      insn = emit_insn (gen_push_st ());
      RTX_FRAME_RELATED_P (insn) = 1;
      if (size)
	{
          insn = emit_insn (gen_pushqi ( gen_rtx_REG (QImode, AR3_REGNO)));
          RTX_FRAME_RELATED_P (insn) = 1;
	  insn = emit_insn (gen_movqi (gen_rtx_REG (QImode, AR3_REGNO),
				       gen_rtx_REG (QImode, SP_REGNO)));
          RTX_FRAME_RELATED_P (insn) = 1;
	  /* We require that an ISR uses fewer than 32768 words of
	     local variables, otherwise we have to go to lots of
	     effort to save a register, load it with the desired size,
	     adjust the stack pointer, and then restore the modified
	     register.  Frankly, I think it is a poor ISR that
	     requires more than 32767 words of local temporary
	     storage!  */
	  if (size > 32767)
	    error ("ISR %s requires %d words of local vars, max is 32767",
		   current_function_name (), size);

	  insn = emit_insn (gen_addqi3 (gen_rtx_REG (QImode, SP_REGNO),
				        gen_rtx_REG (QImode, SP_REGNO),
					GEN_INT (size)));
          RTX_FRAME_RELATED_P (insn) = 1;
	}
      for (regno = 0; regno < FIRST_PSEUDO_REGISTER; regno++)
	{
	  if (c4x_isr_reg_used_p (regno))
	    {
	      if (regno == DP_REGNO)
		{
		  insn = emit_insn (gen_push_dp ());
                  RTX_FRAME_RELATED_P (insn) = 1;
		}
	      else
		{
                  insn = emit_insn (gen_pushqi (gen_rtx_REG (QImode, regno)));
                  RTX_FRAME_RELATED_P (insn) = 1;
		  if (IS_EXT_REGNO (regno))
		    {
                      insn = emit_insn (gen_pushqf
					(gen_rtx_REG (QFmode, regno)));
                      RTX_FRAME_RELATED_P (insn) = 1;
		    }
		}
	    }
	}
      /* We need to clear the repeat mode flag if the ISR is
         going to use a RPTB instruction or uses the RC, RS, or RE
         registers.  */
      if (regs_ever_live[RC_REGNO] 
	  || regs_ever_live[RS_REGNO] 
	  || regs_ever_live[RE_REGNO])
	{
          insn = emit_insn (gen_andn_st (GEN_INT(~0x100)));
          RTX_FRAME_RELATED_P (insn) = 1;
	}

      /* Reload DP reg if we are paranoid about some turkey
         violating small memory model rules.  */
      if (TARGET_SMALL && TARGET_PARANOID)
	{
          insn = emit_insn (gen_set_ldp_prologue
			    (gen_rtx_REG (QImode, DP_REGNO),
			     gen_rtx_SYMBOL_REF (QImode, "data_sec")));
          RTX_FRAME_RELATED_P (insn) = 1;
	}
    }
  else
    {
      if (frame_pointer_needed)
	{
	  if ((size != 0)
	      || (current_function_args_size != 0)
	      || (optimize < 2))
	    {
              insn = emit_insn (gen_pushqi ( gen_rtx_REG (QImode, AR3_REGNO)));
              RTX_FRAME_RELATED_P (insn) = 1;
	      insn = emit_insn (gen_movqi (gen_rtx_REG (QImode, AR3_REGNO),
				           gen_rtx_REG (QImode, SP_REGNO)));
              RTX_FRAME_RELATED_P (insn) = 1;
	      dont_push_ar3 = 1;
	    }
	  else
	    {
	      /* Since ar3 is not used, we don't need to push it.  */
	      dont_push_ar3 = 1;
	    }
	}
      else
	{
	  /* If we use ar3, we need to push it.  */
	  dont_push_ar3 = 0;
	  if ((size != 0) || (current_function_args_size != 0))
	    {
	      /* If we are omitting the frame pointer, we still have
	         to make space for it so the offsets are correct
	         unless we don't use anything on the stack at all.  */
	      size += 1;
	    }
	}
      
      if (size > 32767)
	{
	  /* Local vars are too big, it will take multiple operations
	     to increment SP.  */
	  if (TARGET_C3X)
	    {
	      insn = emit_insn (gen_movqi (gen_rtx_REG (QImode, R1_REGNO),
					   GEN_INT(size >> 16)));
              RTX_FRAME_RELATED_P (insn) = 1;
	      insn = emit_insn (gen_lshrqi3 (gen_rtx_REG (QImode, R1_REGNO),
					     gen_rtx_REG (QImode, R1_REGNO),
					     GEN_INT(-16)));
              RTX_FRAME_RELATED_P (insn) = 1;
	    }
	  else
	    {
	      insn = emit_insn (gen_movqi (gen_rtx_REG (QImode, R1_REGNO),
					   GEN_INT(size & ~0xffff)));
              RTX_FRAME_RELATED_P (insn) = 1;
	    }
	  insn = emit_insn (gen_iorqi3 (gen_rtx_REG (QImode, R1_REGNO),
				        gen_rtx_REG (QImode, R1_REGNO),
					GEN_INT(size & 0xffff)));
          RTX_FRAME_RELATED_P (insn) = 1;
	  insn = emit_insn (gen_addqi3 (gen_rtx_REG (QImode, SP_REGNO),
				        gen_rtx_REG (QImode, SP_REGNO),
				        gen_rtx_REG (QImode, R1_REGNO)));
          RTX_FRAME_RELATED_P (insn) = 1;
	}
      else if (size != 0)
	{
	  /* Local vars take up less than 32767 words, so we can directly
	     add the number.  */
	  insn = emit_insn (gen_addqi3 (gen_rtx_REG (QImode, SP_REGNO),
				        gen_rtx_REG (QImode, SP_REGNO),
				        GEN_INT (size)));
          RTX_FRAME_RELATED_P (insn) = 1;
	}
      
      for (regno = 0; regno < FIRST_PSEUDO_REGISTER; regno++)
	{
	  if (regs_ever_live[regno] && ! call_used_regs[regno])
	    {
	      if (IS_FLOAT_CALL_SAVED_REGNO (regno))
		{
		  if (TARGET_PRESERVE_FLOAT)
		    {
                      insn = emit_insn (gen_pushqi
					(gen_rtx_REG (QImode, regno)));
		      RTX_FRAME_RELATED_P (insn) = 1;
		    }
                  insn = emit_insn (gen_pushqf (gen_rtx_REG (QFmode, regno)));
		  RTX_FRAME_RELATED_P (insn) = 1;
		}
	      else if ((! dont_push_ar3) || (regno != AR3_REGNO))
		{
                  insn = emit_insn (gen_pushqi ( gen_rtx_REG (QImode, regno)));
		  RTX_FRAME_RELATED_P (insn) = 1;
		}
	    }
	}
    }
}


void
c4x_expand_epilogue(void)
{
  int regno;
  int jump = 0;
  int dont_pop_ar3;
  rtx insn;
  int size = get_frame_size ();
  
  /* For __naked__ function build no epilogue.  */
  if (c4x_naked_function_p ())
    {
      insn = emit_jump_insn (gen_return_from_epilogue ());
      RTX_FRAME_RELATED_P (insn) = 1;
      return;
    }

  /* For __interrupt__ function build specific epilogue.  */
  if (c4x_interrupt_function_p ())
    {
      for (regno = FIRST_PSEUDO_REGISTER - 1; regno >= 0; --regno)
	{
	  if (! c4x_isr_reg_used_p (regno))
	    continue;
	  if (regno == DP_REGNO)
	    {
	      insn = emit_insn (gen_pop_dp ());
	      RTX_FRAME_RELATED_P (insn) = 1;
	    }
	  else
	    {
	      /* We have to use unspec because the compiler will delete insns
	         that are not call-saved.  */
	      if (IS_EXT_REGNO (regno))
		{
                  insn = emit_insn (gen_popqf_unspec
				    (gen_rtx_REG (QFmode, regno)));
	          RTX_FRAME_RELATED_P (insn) = 1;
		}
	      insn = emit_insn (gen_popqi_unspec (gen_rtx_REG (QImode, regno)));
	      RTX_FRAME_RELATED_P (insn) = 1;
	    }
	}
      if (size)
	{
	  insn = emit_insn (gen_subqi3 (gen_rtx_REG (QImode, SP_REGNO),
				        gen_rtx_REG (QImode, SP_REGNO),
					GEN_INT(size)));
          RTX_FRAME_RELATED_P (insn) = 1;
	  insn = emit_insn (gen_popqi
			    (gen_rtx_REG (QImode, AR3_REGNO)));
          RTX_FRAME_RELATED_P (insn) = 1;
	}
      insn = emit_insn (gen_pop_st ());
      RTX_FRAME_RELATED_P (insn) = 1;
      insn = emit_jump_insn (gen_return_from_interrupt_epilogue ());
      RTX_FRAME_RELATED_P (insn) = 1;
    }
  else
    {
      if (frame_pointer_needed)
	{
	  if ((size != 0) 
	      || (current_function_args_size != 0) 
	      || (optimize < 2))
	    {
	      insn = emit_insn
		(gen_movqi (gen_rtx_REG (QImode, R2_REGNO),
			    gen_rtx_MEM (QImode,
					 gen_rtx_PLUS 
					 (QImode, gen_rtx_REG (QImode,
							       AR3_REGNO),
					  constm1_rtx))));
	      RTX_FRAME_RELATED_P (insn) = 1;
	      
	      /* We already have the return value and the fp,
	         so we need to add those to the stack.  */
	      size += 2;
	      jump = 1;
	      dont_pop_ar3 = 1;
	    }
	  else
	    {
	      /* Since ar3 is not used for anything, we don't need to
	         pop it.  */
	      dont_pop_ar3 = 1;
	    }
	}
      else
	{
	  dont_pop_ar3 = 0;	/* If we use ar3, we need to pop it.  */
	  if (size || current_function_args_size)
	    {
	      /* If we are omitting the frame pointer, we still have
	         to make space for it so the offsets are correct
	         unless we don't use anything on the stack at all.  */
	      size += 1;
	    }
	}
      
      /* Now restore the saved registers, putting in the delayed branch
         where required.  */
      for (regno = FIRST_PSEUDO_REGISTER - 1; regno >= 0; regno--)
	{
	  if (regs_ever_live[regno] && ! call_used_regs[regno])
	    {
	      if (regno == AR3_REGNO && dont_pop_ar3)
		continue;
	      
	      if (IS_FLOAT_CALL_SAVED_REGNO (regno))
		{
		  insn = emit_insn (gen_popqf_unspec
				    (gen_rtx_REG (QFmode, regno)));
		  RTX_FRAME_RELATED_P (insn) = 1;
		  if (TARGET_PRESERVE_FLOAT)
		    {
                      insn = emit_insn (gen_popqi_unspec
					(gen_rtx_REG (QImode, regno)));
		      RTX_FRAME_RELATED_P (insn) = 1;
		    }
		}
	      else
		{
		  insn = emit_insn (gen_popqi (gen_rtx_REG (QImode, regno)));
		  RTX_FRAME_RELATED_P (insn) = 1;
		}
	    }
	}
      
      if (frame_pointer_needed)
	{
	  if ((size != 0)
	      || (current_function_args_size != 0)
	      || (optimize < 2))
	    {
	      /* Restore the old FP.  */
	      insn = emit_insn 
		(gen_movqi 
		 (gen_rtx_REG (QImode, AR3_REGNO),
		  gen_rtx_MEM (QImode, gen_rtx_REG (QImode, AR3_REGNO))));
	      
	      RTX_FRAME_RELATED_P (insn) = 1;
	    }
	}
      
      if (size > 32767)
	{
	  /* Local vars are too big, it will take multiple operations
	     to decrement SP.  */
	  if (TARGET_C3X)
	    {
	      insn = emit_insn (gen_movqi (gen_rtx_REG (QImode, R3_REGNO),
					   GEN_INT(size >> 16)));
              RTX_FRAME_RELATED_P (insn) = 1;
	      insn = emit_insn (gen_lshrqi3 (gen_rtx_REG (QImode, R3_REGNO),
					     gen_rtx_REG (QImode, R3_REGNO),
					     GEN_INT(-16)));
              RTX_FRAME_RELATED_P (insn) = 1;
	    }
	  else
	    {
	      insn = emit_insn (gen_movqi (gen_rtx_REG (QImode, R3_REGNO),
					   GEN_INT(size & ~0xffff)));
              RTX_FRAME_RELATED_P (insn) = 1;
	    }
	  insn = emit_insn (gen_iorqi3 (gen_rtx_REG (QImode, R3_REGNO),
				        gen_rtx_REG (QImode, R3_REGNO),
					GEN_INT(size & 0xffff)));
          RTX_FRAME_RELATED_P (insn) = 1;
	  insn = emit_insn (gen_subqi3 (gen_rtx_REG (QImode, SP_REGNO),
				        gen_rtx_REG (QImode, SP_REGNO),
				        gen_rtx_REG (QImode, R3_REGNO)));
          RTX_FRAME_RELATED_P (insn) = 1;
	}
      else if (size != 0)
	{
	  /* Local vars take up less than 32768 words, so we can directly
	     subtract the number.  */
	  insn = emit_insn (gen_subqi3 (gen_rtx_REG (QImode, SP_REGNO),
				        gen_rtx_REG (QImode, SP_REGNO),
				        GEN_INT(size)));
          RTX_FRAME_RELATED_P (insn) = 1;
	}
      
      if (jump)
	{
	  insn = emit_jump_insn (gen_return_indirect_internal
				 (gen_rtx_REG (QImode, R2_REGNO)));
          RTX_FRAME_RELATED_P (insn) = 1;
	}
      else
	{
          insn = emit_jump_insn (gen_return_from_epilogue ());
          RTX_FRAME_RELATED_P (insn) = 1;
	}
    }
}


int
c4x_null_epilogue_p (void)
{
  int regno;

  if (reload_completed
      && ! c4x_naked_function_p ()
      && ! c4x_interrupt_function_p ()
      && ! current_function_calls_alloca
      && ! current_function_args_size
      && ! (optimize < 2)
      && ! get_frame_size ())
    {
      for (regno = FIRST_PSEUDO_REGISTER - 1; regno >= 0; regno--)
	if (regs_ever_live[regno] && ! call_used_regs[regno]
	    && (regno != AR3_REGNO))
	  return 1;
      return 0;
    }
  return 1;
}


int
c4x_emit_move_sequence (rtx *operands, enum machine_mode mode)
{
  rtx op0 = operands[0];
  rtx op1 = operands[1];

  if (! reload_in_progress
      && ! REG_P (op0) 
      && ! REG_P (op1)
      && ! (stik_const_operand (op1, mode) && ! push_operand (op0, mode)))
    op1 = force_reg (mode, op1);

  if (GET_CODE (op1) == LO_SUM
      && GET_MODE (op1) == Pmode
      && dp_reg_operand (XEXP (op1, 0), mode))
    {
      /* expand_increment will sometimes create a LO_SUM immediate
	 address.  Undo this silliness.  */
      op1 = XEXP (op1, 1);
    }
  
  if (symbolic_address_operand (op1, mode))
    {
      if (TARGET_LOAD_ADDRESS)
	{
	  /* Alias analysis seems to do a better job if we force
	     constant addresses to memory after reload.  */
	  emit_insn (gen_load_immed_address (op0, op1));
	  return 1;
	}
      else
	{
	  /* Stick symbol or label address into the constant pool.  */
	  op1 = force_const_mem (Pmode, op1);
	}
    }
  else if (mode == HFmode && CONSTANT_P (op1) && ! LEGITIMATE_CONSTANT_P (op1))
    {
      /* We could be a lot smarter about loading some of these
	 constants...  */
      op1 = force_const_mem (mode, op1);
    }

  /* Convert (MEM (SYMREF)) to a (MEM (LO_SUM (REG) (SYMREF)))
     and emit associated (HIGH (SYMREF)) if large memory model.  
     c4x_legitimize_address could be used to do this,
     perhaps by calling validize_address.  */
  if (TARGET_EXPOSE_LDP
      && ! (reload_in_progress || reload_completed)
      && GET_CODE (op1) == MEM
      && symbolic_address_operand (XEXP (op1, 0), Pmode))
    {
      rtx dp_reg = gen_rtx_REG (Pmode, DP_REGNO);
      if (! TARGET_SMALL)
	emit_insn (gen_set_ldp (dp_reg, XEXP (op1, 0)));
      op1 = change_address (op1, mode,
			    gen_rtx_LO_SUM (Pmode, dp_reg, XEXP (op1, 0)));
    }

  if (TARGET_EXPOSE_LDP
      && ! (reload_in_progress || reload_completed)
      && GET_CODE (op0) == MEM 
      && symbolic_address_operand (XEXP (op0, 0), Pmode))
    {
      rtx dp_reg = gen_rtx_REG (Pmode, DP_REGNO);
      if (! TARGET_SMALL)
	emit_insn (gen_set_ldp (dp_reg, XEXP (op0, 0)));
      op0 = change_address (op0, mode,
			    gen_rtx_LO_SUM (Pmode, dp_reg, XEXP (op0, 0)));
    }

  if (GET_CODE (op0) == SUBREG
      && mixed_subreg_operand (op0, mode))
    {
      /* We should only generate these mixed mode patterns
	 during RTL generation.  If we need do it later on
	 then we'll have to emit patterns that won't clobber CC.  */
      if (reload_in_progress || reload_completed)
	abort ();
      if (GET_MODE (SUBREG_REG (op0)) == QImode)
	op0 = SUBREG_REG (op0);
      else if (GET_MODE (SUBREG_REG (op0)) == HImode)
	{
	  op0 = copy_rtx (op0);
	  PUT_MODE (op0, QImode);
	}
      else
	abort ();

      if (mode == QFmode)
	emit_insn (gen_storeqf_int_clobber (op0, op1));
      else
	abort ();
      return 1;
    }

  if (GET_CODE (op1) == SUBREG
      && mixed_subreg_operand (op1, mode))
    {
      /* We should only generate these mixed mode patterns
	 during RTL generation.  If we need do it later on
	 then we'll have to emit patterns that won't clobber CC.  */
      if (reload_in_progress || reload_completed)
	abort ();
      if (GET_MODE (SUBREG_REG (op1)) == QImode)
	op1 = SUBREG_REG (op1);
      else if (GET_MODE (SUBREG_REG (op1)) == HImode)
	{
	  op1 = copy_rtx (op1);
	  PUT_MODE (op1, QImode);
	}
      else
	abort ();

      if (mode == QFmode)
	emit_insn (gen_loadqf_int_clobber (op0, op1));
      else
	abort ();
      return 1;
    }

  if (mode == QImode
      && reg_operand (op0, mode)
      && const_int_operand (op1, mode)
      && ! IS_INT16_CONST (INTVAL (op1))
      && ! IS_HIGH_CONST (INTVAL (op1)))
    {
      emit_insn (gen_loadqi_big_constant (op0, op1));
      return 1;
    }

  if (mode == HImode
      && reg_operand (op0, mode)
      && const_int_operand (op1, mode))
    {
      emit_insn (gen_loadhi_big_constant (op0, op1));
      return 1;
    }

  /* Adjust operands in case we have modified them.  */
  operands[0] = op0;
  operands[1] = op1;

  /* Emit normal pattern.  */
  return 0;
}


void
c4x_emit_libcall (rtx libcall, enum rtx_code code,
		  enum machine_mode dmode, enum machine_mode smode,
		  int noperands, rtx *operands)
{
  rtx ret;
  rtx insns;
  rtx equiv;

  start_sequence ();
  switch (noperands)
    {
    case 2:
      ret = emit_library_call_value (libcall, NULL_RTX, 1, dmode, 1,
				     operands[1], smode);
      equiv = gen_rtx_fmt_e (code, dmode, operands[1]);
      break;

    case 3:
      ret = emit_library_call_value (libcall, NULL_RTX, 1, dmode, 2,
				     operands[1], smode, operands[2], smode);
      equiv = gen_rtx_fmt_ee (code, dmode, operands[1], operands[2]);
      break;

    default:
      abort ();
    }

  insns = get_insns ();
  end_sequence ();
  emit_libcall_block (insns, operands[0], ret, equiv);
}


void
c4x_emit_libcall3 (rtx libcall, enum rtx_code code,
		   enum machine_mode mode, rtx *operands)
{
  c4x_emit_libcall (libcall, code, mode, mode, 3, operands);
}


void
c4x_emit_libcall_mulhi (rtx libcall, enum rtx_code code,
			enum machine_mode mode, rtx *operands)
{
  rtx ret;
  rtx insns;
  rtx equiv;

  start_sequence ();
  ret = emit_library_call_value (libcall, NULL_RTX, 1, mode, 2,
                                 operands[1], mode, operands[2], mode);
  equiv = gen_rtx_TRUNCATE (mode,
                   gen_rtx_LSHIFTRT (HImode,
                            gen_rtx_MULT (HImode,
                                     gen_rtx_fmt_e (code, HImode, operands[1]),
                                     gen_rtx_fmt_e (code, HImode, operands[2])),
                                     GEN_INT (32)));
  insns = get_insns ();
  end_sequence ();
  emit_libcall_block (insns, operands[0], ret, equiv);
}


int
c4x_legitimate_address_p (enum machine_mode mode, rtx addr, int strict)
{
  rtx base = NULL_RTX;		/* Base register (AR0-AR7).  */
  rtx indx = NULL_RTX;		/* Index register (IR0,IR1).  */
  rtx disp = NULL_RTX;		/* Displacement.  */
  enum rtx_code code;

  code = GET_CODE (addr);
  switch (code)
    {
      /* Register indirect with auto increment/decrement.  We don't
	 allow SP here---push_operand should recognize an operand
	 being pushed on the stack.  */

    case PRE_DEC:
    case PRE_INC:
    case POST_DEC:
      if (mode != QImode && mode != QFmode)
	return 0;

    case POST_INC:
      base = XEXP (addr, 0);
      if (! REG_P (base))
	return 0;
      break;

    case PRE_MODIFY:
    case POST_MODIFY:
      {
	rtx op0 = XEXP (addr, 0);
	rtx op1 = XEXP (addr, 1);

	if (mode != QImode && mode != QFmode)
	  return 0;

	if (! REG_P (op0) 
	    || (GET_CODE (op1) != PLUS && GET_CODE (op1) != MINUS))
	  return 0;
	base = XEXP (op1, 0);
	if (! REG_P (base))
	    return 0;
	if (REGNO (base) != REGNO (op0))
	  return 0;
	if (REG_P (XEXP (op1, 1)))
	  indx = XEXP (op1, 1);
	else
	  disp = XEXP (op1, 1);
      }
      break;
	
      /* Register indirect.  */
    case REG:
      base = addr;
      break;

      /* Register indirect with displacement or index.  */
    case PLUS:
      {
	rtx op0 = XEXP (addr, 0);
	rtx op1 = XEXP (addr, 1);
	enum rtx_code code0 = GET_CODE (op0);

	switch (code0)
	  {
	  case REG:
	    if (REG_P (op1))
	      {
		base = op0;	/* Base + index.  */
		indx = op1;
		if (IS_INDEX_REG (base) || IS_ADDR_REG (indx))
		  {
		    base = op1;
		    indx = op0;
		  }
	      }
	    else
	      {
		base = op0;	/* Base + displacement.  */
		disp = op1;
	      }
	    break;

	  default:
	    return 0;
	  }
      }
      break;

      /* Direct addressing with DP register.  */
    case LO_SUM:
      {
	rtx op0 = XEXP (addr, 0);
	rtx op1 = XEXP (addr, 1);

	/* HImode and HFmode direct memory references aren't truly
	   offsettable (consider case at end of data page).  We
	   probably get better code by loading a pointer and using an
	   indirect memory reference.  */
	if (mode == HImode || mode == HFmode)
	  return 0;

	if (!REG_P (op0) || REGNO (op0) != DP_REGNO)
	  return 0;

	if ((GET_CODE (op1) == SYMBOL_REF || GET_CODE (op1) == LABEL_REF))
	  return 1;

	if (GET_CODE (op1) == CONST)
	  return 1;
	return 0;
      }
      break;

      /* Direct addressing with some work for the assembler...  */
    case CONST:
      /* Direct addressing.  */
    case LABEL_REF:
    case SYMBOL_REF:
      if (! TARGET_EXPOSE_LDP && ! strict && mode != HFmode && mode != HImode)
	return 1;
      /* These need to be converted to a LO_SUM (...). 
	 LEGITIMIZE_RELOAD_ADDRESS will do this during reload.  */
      return 0;

      /* Do not allow direct memory access to absolute addresses.
         This is more pain than it's worth, especially for the
         small memory model where we can't guarantee that
         this address is within the data page---we don't want
         to modify the DP register in the small memory model,
         even temporarily, since an interrupt can sneak in....  */
    case CONST_INT:
      return 0;

      /* Indirect indirect addressing.  */
    case MEM:
      return 0;

    case CONST_DOUBLE:
      fatal_insn ("using CONST_DOUBLE for address", addr);

    default:
      return 0;
    }

  /* Validate the base register.  */
  if (base)
    {
      /* Check that the address is offsettable for HImode and HFmode.  */
      if (indx && (mode == HImode || mode == HFmode))
	return 0;

      /* Handle DP based stuff.  */
      if (REGNO (base) == DP_REGNO)
	return 1;
      if (strict && ! REGNO_OK_FOR_BASE_P (REGNO (base)))
	return 0;
      else if (! strict && ! IS_ADDR_OR_PSEUDO_REG (base))
	return 0;
    }

  /* Now validate the index register.  */
  if (indx)
    {
      if (GET_CODE (indx) != REG)
	return 0;
      if (strict && ! REGNO_OK_FOR_INDEX_P (REGNO (indx)))
	return 0;
      else if (! strict && ! IS_INDEX_OR_PSEUDO_REG (indx))
	return 0;
    }

  /* Validate displacement.  */
  if (disp)
    {
      if (GET_CODE (disp) != CONST_INT)
	return 0;
      if (mode == HImode || mode == HFmode)
	{
	  /* The offset displacement must be legitimate.  */
	  if (! IS_DISP8_OFF_CONST (INTVAL (disp)))
	    return 0;
	}
      else
	{
	  if (! IS_DISP8_CONST (INTVAL (disp)))
	    return 0;
	}
      /* Can't add an index with a disp.  */
      if (indx)
	return 0;		
    }
  return 1;
}


rtx
c4x_legitimize_address (rtx orig ATTRIBUTE_UNUSED,
			enum machine_mode mode ATTRIBUTE_UNUSED)
{
  if (GET_CODE (orig) == SYMBOL_REF
      || GET_CODE (orig) == LABEL_REF)
    {
      if (mode == HImode || mode == HFmode)
	{
	  /* We need to force the address into
	     a register so that it is offsettable.  */
	  rtx addr_reg = gen_reg_rtx (Pmode);
	  emit_move_insn (addr_reg, orig);
	  return addr_reg;
	}
      else
	{
	  rtx dp_reg = gen_rtx_REG (Pmode, DP_REGNO);
	  
	  if (! TARGET_SMALL)
	    emit_insn (gen_set_ldp (dp_reg, orig));
	  
	  return gen_rtx_LO_SUM (Pmode, dp_reg, orig);
	}
    }

  return NULL_RTX;
}


/* Provide the costs of an addressing mode that contains ADDR.
   If ADDR is not a valid address, its cost is irrelevant.  
   This is used in cse and loop optimization to determine
   if it is worthwhile storing a common address into a register. 
   Unfortunately, the C4x address cost depends on other operands.  */

static int 
c4x_address_cost (rtx addr)
{
  switch (GET_CODE (addr))
    {
    case REG:
      return 1;

    case POST_INC:
    case POST_DEC:
    case PRE_INC:
    case PRE_DEC:
      return 1;
      
      /* These shouldn't be directly generated.  */
    case SYMBOL_REF:
    case LABEL_REF:
    case CONST:
      return 10;

    case LO_SUM:
      {
	rtx op1 = XEXP (addr, 1);

	if (GET_CODE (op1) == LABEL_REF || GET_CODE (op1) == SYMBOL_REF)
	  return TARGET_SMALL ? 3 : 4;
	
	if (GET_CODE (op1) == CONST)
	  {
	    rtx offset = const0_rtx;
	    
	    op1 = eliminate_constant_term (op1, &offset);
	    
	    /* ??? These costs need rethinking...  */
	    if (GET_CODE (op1) == LABEL_REF)
	      return 3;
	    
	    if (GET_CODE (op1) != SYMBOL_REF)
	      return 4;
	    
	    if (INTVAL (offset) == 0)
	      return 3;

	    return 4;
	  }
	fatal_insn ("c4x_address_cost: Invalid addressing mode", addr);
      }
      break;
      
    case PLUS:
      {
	register rtx op0 = XEXP (addr, 0);
	register rtx op1 = XEXP (addr, 1);
	
	if (GET_CODE (op0) != REG)
	  break;
	
	switch (GET_CODE (op1))
	  {
	  default:
	    break;

	  case REG:
	    /* This cost for REG+REG must be greater than the cost
	       for REG if we want autoincrement addressing modes.  */
	    return 2;

	  case CONST_INT:
	    /* The following tries to improve GIV combination
	       in strength reduce but appears not to help.  */
	    if (TARGET_DEVEL && IS_UINT5_CONST (INTVAL (op1)))
	      return 1;

	    if (IS_DISP1_CONST (INTVAL (op1)))
	      return 1;

	    if (! TARGET_C3X && IS_UINT5_CONST (INTVAL (op1)))
	      return 2;

	    return 3;
	  }
      }
    default:
      break;
    }
  
  return 4;
}


rtx
c4x_gen_compare_reg (enum rtx_code code, rtx x, rtx y)
{
  enum machine_mode mode = SELECT_CC_MODE (code, x, y);
  rtx cc_reg;

  if (mode == CC_NOOVmode
      && (code == LE || code == GE || code == LT || code == GT))
    return NULL_RTX;

  cc_reg = gen_rtx_REG (mode, ST_REGNO);
  emit_insn (gen_rtx_SET (VOIDmode, cc_reg,
			  gen_rtx_COMPARE (mode, x, y)));
  return cc_reg;
}

char *
c4x_output_cbranch (const char *form, rtx seq)
{
  int delayed = 0;
  int annultrue = 0;
  int annulfalse = 0;
  rtx delay;
  char *cp;
  static char str[100];
  
  if (final_sequence)
    {
      delay = XVECEXP (final_sequence, 0, 1);
      delayed = ! INSN_ANNULLED_BRANCH_P (seq);
      annultrue = INSN_ANNULLED_BRANCH_P (seq) && ! INSN_FROM_TARGET_P (delay);
      annulfalse = INSN_ANNULLED_BRANCH_P (seq) && INSN_FROM_TARGET_P (delay);
    }
  strcpy (str, form);
  cp = &str [strlen (str)];
  if (delayed)
    {
      *cp++ = '%';
      *cp++ = '#';
    }
  if (annultrue)
    {
      *cp++ = 'a';
      *cp++ = 't';
    }
  if (annulfalse)
    {
      *cp++ = 'a'; 
      *cp++ = 'f';
    }
  *cp++ = '\t';
  *cp++ = '%'; 
  *cp++ = 'l';
  *cp++ = '1';
  *cp = 0;
  return str;
}

void
c4x_print_operand (FILE *file, rtx op, int letter)
{
  rtx op1;
  enum rtx_code code;

  switch (letter)
    {
    case '#':			/* Delayed.  */
      if (final_sequence)
	fprintf (file, "d");
      return;
    }

  code = GET_CODE (op);
  switch (letter)
    {
    case 'A':			/* Direct address.  */
      if (code == CONST_INT || code == SYMBOL_REF || code == CONST)
	fprintf (file, "@");
      break;

    case 'H':			/* Sethi.  */
      output_addr_const (file, op);
      return;

    case 'I':			/* Reversed condition.  */
      code = reverse_condition (code);
      break;

    case 'L':			/* Log 2 of constant.  */
      if (code != CONST_INT)
	fatal_insn ("c4x_print_operand: %%L inconsistency", op);
      fprintf (file, "%d", exact_log2 (INTVAL (op)));
      return;

    case 'N':			/* Ones complement of small constant.  */
      if (code != CONST_INT)
	fatal_insn ("c4x_print_operand: %%N inconsistency", op);
      fprintf (file, HOST_WIDE_INT_PRINT_DEC, ~INTVAL (op));
      return;

    case 'K':			/* Generate ldp(k) if direct address.  */
      if (! TARGET_SMALL
	  && code == MEM
	  && GET_CODE (XEXP (op, 0)) == LO_SUM
	  && GET_CODE (XEXP (XEXP (op, 0), 0)) == REG
	  && REGNO (XEXP (XEXP (op, 0), 0)) == DP_REGNO)
	{
	  op1 = XEXP (XEXP (op, 0), 1);
          if (GET_CODE(op1) == CONST_INT || GET_CODE(op1) == SYMBOL_REF)
	    {
	      fprintf (file, "\t%s\t@", TARGET_C3X ? "ldp" : "ldpk");
	      output_address (XEXP (adjust_address (op, VOIDmode, 1), 0));
	      fprintf (file, "\n");
	    }
	}
      return;

    case 'M':			/* Generate ldp(k) if direct address.  */
      if (! TARGET_SMALL	/* Only used in asm statements.  */
	  && code == MEM
	  && (GET_CODE (XEXP (op, 0)) == CONST
	      || GET_CODE (XEXP (op, 0)) == SYMBOL_REF))
	{
	  fprintf (file, "%s\t@", TARGET_C3X ? "ldp" : "ldpk");
          output_address (XEXP (op, 0));
	  fprintf (file, "\n\t");
	}
      return;

    case 'O':			/* Offset address.  */
      if (code == MEM && c4x_autoinc_operand (op, Pmode))
	break;
      else if (code == MEM)
	output_address (XEXP (adjust_address (op, VOIDmode, 1), 0));
      else if (code == REG)
	fprintf (file, "%s", reg_names[REGNO (op) + 1]);
      else
	fatal_insn ("c4x_print_operand: %%O inconsistency", op);
      return;

    case 'C':			/* Call.  */
      break;

    case 'U':			/* Call/callu.  */
      if (code != SYMBOL_REF)
	fprintf (file, "u");
      return;

    default:
      break;
    }
  
  switch (code)
    {
    case REG:
      if (GET_MODE_CLASS (GET_MODE (op)) == MODE_FLOAT
	  && ! TARGET_TI)
	fprintf (file, "%s", float_reg_names[REGNO (op)]);
      else
	fprintf (file, "%s", reg_names[REGNO (op)]);
      break;
      
    case MEM:
      output_address (XEXP (op, 0));
      break;
      
    case CONST_DOUBLE:
      {
	char str[64];
	
	real_to_decimal (str, CONST_DOUBLE_REAL_VALUE (op),
			 sizeof (str), 0, 1);
	fprintf (file, "%s", str);
      }
      break;
      
    case CONST_INT:
      fprintf (file, HOST_WIDE_INT_PRINT_DEC, INTVAL (op));
      break;
      
    case NE:
      fprintf (file, "ne");
      break;
      
    case EQ:
      fprintf (file, "eq");
      break;
      
    case GE:
      fprintf (file, "ge");
      break;

    case GT:
      fprintf (file, "gt");
      break;

    case LE:
      fprintf (file, "le");
      break;

    case LT:
      fprintf (file, "lt");
      break;

    case GEU:
      fprintf (file, "hs");
      break;

    case GTU:
      fprintf (file, "hi");
      break;

    case LEU:
      fprintf (file, "ls");
      break;

    case LTU:
      fprintf (file, "lo");
      break;

    case SYMBOL_REF:
      output_addr_const (file, op);
      break;

    case CONST:
      output_addr_const (file, XEXP (op, 0));
      break;

    case CODE_LABEL:
      break;

    default:
      fatal_insn ("c4x_print_operand: Bad operand case", op);
      break;
    }
}


void
c4x_print_operand_address (FILE *file, rtx addr)
{
  switch (GET_CODE (addr))
    {
    case REG:
      fprintf (file, "*%s", reg_names[REGNO (addr)]);
      break;

    case PRE_DEC:
      fprintf (file, "*--%s", reg_names[REGNO (XEXP (addr, 0))]);
      break;

    case POST_INC:
      fprintf (file, "*%s++", reg_names[REGNO (XEXP (addr, 0))]);
      break;

    case POST_MODIFY:
      {
	rtx op0 = XEXP (XEXP (addr, 1), 0);
	rtx op1 = XEXP (XEXP (addr, 1), 1);
	
	if (GET_CODE (XEXP (addr, 1)) == PLUS && REG_P (op1))
	  fprintf (file, "*%s++(%s)", reg_names[REGNO (op0)],
		   reg_names[REGNO (op1)]);
	else if (GET_CODE (XEXP (addr, 1)) == PLUS && INTVAL (op1) > 0)
	  fprintf (file, "*%s++(" HOST_WIDE_INT_PRINT_DEC ")",
		   reg_names[REGNO (op0)], INTVAL (op1));
	else if (GET_CODE (XEXP (addr, 1)) == PLUS && INTVAL (op1) < 0)
	  fprintf (file, "*%s--(" HOST_WIDE_INT_PRINT_DEC ")",
		   reg_names[REGNO (op0)], -INTVAL (op1));
	else if (GET_CODE (XEXP (addr, 1)) == MINUS && REG_P (op1))
	  fprintf (file, "*%s--(%s)", reg_names[REGNO (op0)],
		   reg_names[REGNO (op1)]);
	else
	  fatal_insn ("c4x_print_operand_address: Bad post_modify", addr);
      }
      break;
      
    case PRE_MODIFY:
      {
	rtx op0 = XEXP (XEXP (addr, 1), 0);
	rtx op1 = XEXP (XEXP (addr, 1), 1);
	
	if (GET_CODE (XEXP (addr, 1)) == PLUS && REG_P (op1))
	  fprintf (file, "*++%s(%s)", reg_names[REGNO (op0)],
		   reg_names[REGNO (op1)]);
	else if (GET_CODE (XEXP (addr, 1)) == PLUS && INTVAL (op1) > 0)
	  fprintf (file, "*++%s(" HOST_WIDE_INT_PRINT_DEC ")",
		   reg_names[REGNO (op0)], INTVAL (op1));
	else if (GET_CODE (XEXP (addr, 1)) == PLUS && INTVAL (op1) < 0)
	  fprintf (file, "*--%s(" HOST_WIDE_INT_PRINT_DEC ")",
		   reg_names[REGNO (op0)], -INTVAL (op1));
	else if (GET_CODE (XEXP (addr, 1)) == MINUS && REG_P (op1))
	  fprintf (file, "*--%s(%s)", reg_names[REGNO (op0)],
		   reg_names[REGNO (op1)]);
	else
	  fatal_insn ("c4x_print_operand_address: Bad pre_modify", addr);
      }
      break;
      
    case PRE_INC:
      fprintf (file, "*++%s", reg_names[REGNO (XEXP (addr, 0))]);
      break;

    case POST_DEC:
      fprintf (file, "*%s--", reg_names[REGNO (XEXP (addr, 0))]);
      break;

    case PLUS:			/* Indirect with displacement.  */
      {
	rtx op0 = XEXP (addr, 0);
	rtx op1 = XEXP (addr, 1);

	if (REG_P (op0))
	  {
	    if (REG_P (op1))
	      {
		if (IS_INDEX_REG (op0))
		  {
		    fprintf (file, "*+%s(%s)",
			     reg_names[REGNO (op1)],
			     reg_names[REGNO (op0)]);	/* Index + base.  */
		  }
		else
		  {
		    fprintf (file, "*+%s(%s)",
			     reg_names[REGNO (op0)],
			     reg_names[REGNO (op1)]);	/* Base + index.  */
		  }
	      }
	    else if (INTVAL (op1) < 0)
	      {
		fprintf (file, "*-%s(" HOST_WIDE_INT_PRINT_DEC ")",
			 reg_names[REGNO (op0)],
			 -INTVAL (op1));	/* Base - displacement.  */
	      }
	    else
	      {
		fprintf (file, "*+%s(" HOST_WIDE_INT_PRINT_DEC ")",
			 reg_names[REGNO (op0)],
			 INTVAL (op1));	/* Base + displacement.  */
	      }
	  }
	else
          fatal_insn ("c4x_print_operand_address: Bad operand case", addr);
      }
      break;

    case LO_SUM:
      {
	rtx op0 = XEXP (addr, 0);
	rtx op1 = XEXP (addr, 1);
	  
	if (REG_P (op0) && REGNO (op0) == DP_REGNO)
	  c4x_print_operand_address (file, op1);
	else
          fatal_insn ("c4x_print_operand_address: Bad operand case", addr);
      }
      break;

    case CONST:
    case SYMBOL_REF:
    case LABEL_REF:
      fprintf (file, "@");
      output_addr_const (file, addr);
      break;

      /* We shouldn't access CONST_INT addresses.  */
    case CONST_INT:

    default:
      fatal_insn ("c4x_print_operand_address: Bad operand case", addr);
      break;
    }
}


/* Return nonzero if the floating point operand will fit
   in the immediate field.  */

int
c4x_immed_float_p (rtx op)
{
  long convval[2];
  int exponent;
  REAL_VALUE_TYPE r;

  REAL_VALUE_FROM_CONST_DOUBLE (r, op);
  if (GET_MODE (op) == HFmode)
    REAL_VALUE_TO_TARGET_DOUBLE (r, convval);
  else
    {
      REAL_VALUE_TO_TARGET_SINGLE (r, convval[0]);
      convval[1] = 0;
    }

  /* Sign extend exponent.  */
  exponent = (((convval[0] >> 24) & 0xff) ^ 0x80) - 0x80;
  if (exponent == -128)
    return 1;			/* 0.0  */
  if ((convval[0] & 0x00000fff) != 0 || convval[1] != 0)
    return 0;			/* Precision doesn't fit.  */
  return (exponent <= 7)	/* Positive exp.  */
    && (exponent >= -7);	/* Negative exp.  */
}


/* The last instruction in a repeat block cannot be a Bcond, DBcound,
   CALL, CALLCond, TRAPcond, RETIcond, RETScond, IDLE, RPTB or RPTS.

   None of the last four instructions from the bottom of the block can
   be a BcondD, BRD, DBcondD, RPTBD, LAJ, LAJcond, LATcond, BcondAF,
   BcondAT or RETIcondD.

   This routine scans the four previous insns for a jump insn, and if
   one is found, returns 1 so that we bung in a nop instruction.
   This simple minded strategy will add a nop, when it may not
   be required.  Say when there is a JUMP_INSN near the end of the
   block that doesn't get converted into a delayed branch.

   Note that we cannot have a call insn, since we don't generate
   repeat loops with calls in them (although I suppose we could, but
   there's no benefit.)  

   !!! FIXME.  The rptb_top insn may be sucked into a SEQUENCE.  */

int
c4x_rptb_nop_p (rtx insn)
{
  rtx start_label;
  int i;

  /* Extract the start label from the jump pattern (rptb_end).  */
  start_label = XEXP (XEXP (SET_SRC (XVECEXP (PATTERN (insn), 0, 0)), 1), 0);

  /* If there is a label at the end of the loop we must insert
     a NOP.  */
  do {
    insn = previous_insn (insn);
  } while (GET_CODE (insn) == NOTE
	   || GET_CODE (insn) == USE
	   || GET_CODE (insn) == CLOBBER);
  if (GET_CODE (insn) == CODE_LABEL)
    return 1;

  for (i = 0; i < 4; i++)
    {
      /* Search back for prev non-note and non-label insn.  */
      while (GET_CODE (insn) == NOTE || GET_CODE (insn) == CODE_LABEL
	     || GET_CODE (insn) == USE || GET_CODE (insn) == CLOBBER)
	{
	  if (insn == start_label)
	    return i == 0;

	  insn = previous_insn (insn);
	};

      /* If we have a jump instruction we should insert a NOP. If we
	 hit repeat block top we should only insert a NOP if the loop
	 is empty.  */
      if (GET_CODE (insn) == JUMP_INSN)
	return 1;
      insn = previous_insn (insn);
    }
  return 0;
}


/* The C4x looping instruction needs to be emitted at the top of the
  loop.  Emitting the true RTL for a looping instruction at the top of
  the loop can cause problems with flow analysis.  So instead, a dummy
  doloop insn is emitted at the end of the loop.  This routine checks
  for the presence of this doloop insn and then searches back to the
  top of the loop, where it inserts the true looping insn (provided
  there are no instructions in the loop which would cause problems).
  Any additional labels can be emitted at this point.  In addition, if
  the desired loop count register was not allocated, this routine does
  nothing. 

  Before we can create a repeat block looping instruction we have to
  verify that there are no jumps outside the loop and no jumps outside
  the loop go into this loop. This can happen in the basic blocks reorder
  pass. The C4x cpu cannot handle this.  */

static int
c4x_label_ref_used_p (rtx x, rtx code_label)
{
  enum rtx_code code;
  int i, j;
  const char *fmt;

  if (x == 0)
    return 0;

  code = GET_CODE (x);
  if (code == LABEL_REF)
    return INSN_UID (XEXP (x,0)) == INSN_UID (code_label);

  fmt = GET_RTX_FORMAT (code);
  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    {
      if (fmt[i] == 'e')
	{
          if (c4x_label_ref_used_p (XEXP (x, i), code_label))
	    return 1;
	}
      else if (fmt[i] == 'E')
        for (j = XVECLEN (x, i) - 1; j >= 0; j--)
          if (c4x_label_ref_used_p (XVECEXP (x, i, j), code_label))
	    return 1;
    }
  return 0;
}


static int
c4x_rptb_valid_p (rtx insn, rtx start_label)
{
  rtx end = insn;
  rtx start;
  rtx tmp;

  /* Find the start label.  */
  for (; insn; insn = PREV_INSN (insn))
    if (insn == start_label)
      break;

  /* Note found then we cannot use a rptb or rpts.  The label was
     probably moved by the basic block reorder pass.  */
  if (! insn)
    return 0;

  start = insn;
  /* If any jump jumps inside this block then we must fail.  */
  for (insn = PREV_INSN (start); insn; insn = PREV_INSN (insn))
    {
      if (GET_CODE (insn) == CODE_LABEL)
	{
	  for (tmp = NEXT_INSN (start); tmp != end; tmp = NEXT_INSN(tmp))
	    if (GET_CODE (tmp) == JUMP_INSN
                && c4x_label_ref_used_p (tmp, insn))
	      return 0;
        }
    }
  for (insn = NEXT_INSN (end); insn; insn = NEXT_INSN (insn))
    {
      if (GET_CODE (insn) == CODE_LABEL)
	{
	  for (tmp = NEXT_INSN (start); tmp != end; tmp = NEXT_INSN(tmp))
	    if (GET_CODE (tmp) == JUMP_INSN
                && c4x_label_ref_used_p (tmp, insn))
	      return 0;
        }
    }
  /* If any jump jumps outside this block then we must fail.  */
  for (insn = NEXT_INSN (start); insn != end; insn = NEXT_INSN (insn))
    {
      if (GET_CODE (insn) == CODE_LABEL)
	{
	  for (tmp = NEXT_INSN (end); tmp; tmp = NEXT_INSN(tmp))
	    if (GET_CODE (tmp) == JUMP_INSN
                && c4x_label_ref_used_p (tmp, insn))
	      return 0;
	  for (tmp = PREV_INSN (start); tmp; tmp = PREV_INSN(tmp))
	    if (GET_CODE (tmp) == JUMP_INSN
                && c4x_label_ref_used_p (tmp, insn))
	      return 0;
        }
    }

  /* All checks OK.  */
  return 1;
}


void
c4x_rptb_insert (rtx insn)
{
  rtx end_label;
  rtx start_label;
  rtx new_start_label;
  rtx count_reg;

  /* If the count register has not been allocated to RC, say if
     there is a movmem pattern in the loop, then do not insert a
     RPTB instruction.  Instead we emit a decrement and branch
     at the end of the loop.  */
  count_reg = XEXP (XEXP (SET_SRC (XVECEXP (PATTERN (insn), 0, 0)), 0), 0);
  if (REGNO (count_reg) != RC_REGNO)
    return;

  /* Extract the start label from the jump pattern (rptb_end).  */
  start_label = XEXP (XEXP (SET_SRC (XVECEXP (PATTERN (insn), 0, 0)), 1), 0);
  
  if (! c4x_rptb_valid_p (insn, start_label))
    {
      /* We cannot use the rptb insn.  Replace it so reorg can use
         the delay slots of the jump insn.  */
      emit_insn_before (gen_addqi3 (count_reg, count_reg, constm1_rtx), insn);
      emit_insn_before (gen_cmpqi (count_reg, const0_rtx), insn);
      emit_insn_before (gen_bge (start_label), insn);
      LABEL_NUSES (start_label)++;
      delete_insn (insn);
      return;
    }

  end_label = gen_label_rtx ();
  LABEL_NUSES (end_label)++;
  emit_label_after (end_label, insn);

  new_start_label = gen_label_rtx ();
  LABEL_NUSES (new_start_label)++;

  for (; insn; insn = PREV_INSN (insn))
    {
      if (insn == start_label)
	 break;
      if (GET_CODE (insn) == JUMP_INSN &&
	  JUMP_LABEL (insn) == start_label)
	redirect_jump (insn, new_start_label, 0);
    }
  if (! insn)
    fatal_insn ("c4x_rptb_insert: Cannot find start label", start_label);

  emit_label_after (new_start_label, insn);

  if (TARGET_RPTS && c4x_rptb_rpts_p (PREV_INSN (insn), 0))
    emit_insn_after (gen_rpts_top (new_start_label, end_label), insn);
  else
    emit_insn_after (gen_rptb_top (new_start_label, end_label), insn);
  if (LABEL_NUSES (start_label) == 0)
    delete_insn (start_label);
}


/* We need to use direct addressing for large constants and addresses
   that cannot fit within an instruction.  We must check for these
   after after the final jump optimization pass, since this may
   introduce a local_move insn for a SYMBOL_REF.  This pass
   must come before delayed branch slot filling since it can generate
   additional instructions.

   This function also fixes up RTPB style loops that didn't get RC
   allocated as the loop counter.  */

static void
c4x_reorg (void)
{
  rtx insn;

  for (insn = get_insns (); insn; insn = NEXT_INSN (insn))
    {
      /* Look for insn.  */
      if (INSN_P (insn))
	{
	  int insn_code_number;
	  rtx old;

	  insn_code_number = recog_memoized (insn);

	  if (insn_code_number < 0)
	    continue;

	  /* Insert the RTX for RPTB at the top of the loop
	     and a label at the end of the loop.  */
	  if (insn_code_number == CODE_FOR_rptb_end)
	    c4x_rptb_insert(insn);

	  /* We need to split the insn here. Otherwise the calls to
	     force_const_mem will not work for load_immed_address.  */
	  old = insn;

	  /* Don't split the insn if it has been deleted.  */
	  if (! INSN_DELETED_P (old))
	    insn = try_split (PATTERN(old), old, 1);

	  /* When not optimizing, the old insn will be still left around
	     with only the 'deleted' bit set.  Transform it into a note
	     to avoid confusion of subsequent processing.  */
	  if (INSN_DELETED_P (old))
	    {
	      PUT_CODE (old, NOTE);
	      NOTE_LINE_NUMBER (old) = NOTE_INSN_DELETED;
	      NOTE_SOURCE_FILE (old) = 0;
	    }
	}
    }
}


int
c4x_a_register (rtx op)
{
  return REG_P (op) && IS_ADDR_OR_PSEUDO_REG (op);
}


int
c4x_x_register (rtx op)
{
  return REG_P (op) && IS_INDEX_OR_PSEUDO_REG (op);
}


static int
c4x_immed_int_constant (rtx op)
{
  if (GET_CODE (op) != CONST_INT)
    return 0;

  return GET_MODE (op) == VOIDmode
    || GET_MODE_CLASS (GET_MODE (op)) == MODE_INT
    || GET_MODE_CLASS (GET_MODE (op)) == MODE_PARTIAL_INT;
}


static int
c4x_immed_float_constant (rtx op)
{
  if (GET_CODE (op) != CONST_DOUBLE)
    return 0;

  /* Do not check if the CONST_DOUBLE is in memory. If there is a MEM
     present this only means that a MEM rtx has been generated. It does
     not mean the rtx is really in memory.  */

  return GET_MODE (op) == QFmode || GET_MODE (op) == HFmode;
}


int
c4x_shiftable_constant (rtx op)
{
  int i;
  int mask;
  int val = INTVAL (op);

  for (i = 0; i < 16; i++)
    {
      if (val & (1 << i))
	break;
    }
  mask = ((0xffff >> i) << 16) | 0xffff;
  if (IS_INT16_CONST (val & (1 << 31) ? (val >> i) | ~mask
				      : (val >> i) & mask))
    return i;
  return -1;
} 


int
c4x_H_constant (rtx op)
{
  return c4x_immed_float_constant (op) && c4x_immed_float_p (op);
}


int
c4x_I_constant (rtx op)
{
  return c4x_immed_int_constant (op) && IS_INT16_CONST (INTVAL (op));
}


int
c4x_J_constant (rtx op)
{
  if (TARGET_C3X)
    return 0;
  return c4x_immed_int_constant (op) && IS_INT8_CONST (INTVAL (op));
}


int
c4x_K_constant (rtx op)
{
  if (TARGET_C3X || ! c4x_immed_int_constant (op))
    return 0;
  return IS_INT5_CONST (INTVAL (op));
}


int
c4x_L_constant (rtx op)
{
  return c4x_immed_int_constant (op) && IS_UINT16_CONST (INTVAL (op));
}


int
c4x_N_constant (rtx op)
{
  return c4x_immed_int_constant (op) && IS_NOT_UINT16_CONST (INTVAL (op));
}


int
c4x_O_constant (rtx op)
{
  return c4x_immed_int_constant (op) && IS_HIGH_CONST (INTVAL (op));
}


/* The constraints do not have to check the register class,
   except when needed to discriminate between the constraints.
   The operand has been checked by the predicates to be valid.  */

/* ARx + 9-bit signed const or IRn
   *ARx, *+ARx(n), *-ARx(n), *+ARx(IRn), *-Arx(IRn) for -256 < n < 256
   We don't include the pre/post inc/dec forms here since
   they are handled by the <> constraints.  */

int
c4x_Q_constraint (rtx op)
{
  enum machine_mode mode = GET_MODE (op);

  if (GET_CODE (op) != MEM)
    return 0;
  op = XEXP (op, 0);
  switch (GET_CODE (op))
    {
    case REG:
      return 1;

    case PLUS:
      {
	rtx op0 = XEXP (op, 0);
	rtx op1 = XEXP (op, 1);

	if (! REG_P (op0))
	  return 0;

	if (REG_P (op1))
	  return 1;

	if (GET_CODE (op1) != CONST_INT)
	  return 0;

	/* HImode and HFmode must be offsettable.  */
	if (mode == HImode || mode == HFmode)
	  return IS_DISP8_OFF_CONST (INTVAL (op1));
	
	return IS_DISP8_CONST (INTVAL (op1));
      }
      break;

    default:
      break;
    }
  return 0;
}


/* ARx + 5-bit unsigned const
   *ARx, *+ARx(n) for n < 32.  */

int
c4x_R_constraint (rtx op)
{
  enum machine_mode mode = GET_MODE (op);

  if (TARGET_C3X)
    return 0;
  if (GET_CODE (op) != MEM)
    return 0;
  op = XEXP (op, 0);
  switch (GET_CODE (op))
    {
    case REG:
      return 1;

    case PLUS:
      {
	rtx op0 = XEXP (op, 0);
	rtx op1 = XEXP (op, 1);

	if (! REG_P (op0))
	  return 0;

	if (GET_CODE (op1) != CONST_INT)
	  return 0;

	/* HImode and HFmode must be offsettable.  */
	if (mode == HImode || mode == HFmode)
	  return IS_UINT5_CONST (INTVAL (op1) + 1);
	
	return IS_UINT5_CONST (INTVAL (op1));
      }
      break;

    default:
      break;
    }
  return 0;
}


static int
c4x_R_indirect (rtx op)
{
  enum machine_mode mode = GET_MODE (op);

  if (TARGET_C3X || GET_CODE (op) != MEM)
    return 0;

  op = XEXP (op, 0);
  switch (GET_CODE (op))
    {
    case REG:
      return IS_ADDR_OR_PSEUDO_REG (op);

    case PLUS:
      {
	rtx op0 = XEXP (op, 0);
	rtx op1 = XEXP (op, 1);

	/* HImode and HFmode must be offsettable.  */
	if (mode == HImode || mode == HFmode)
	  return IS_ADDR_OR_PSEUDO_REG (op0)
	    && GET_CODE (op1) == CONST_INT 
	    && IS_UINT5_CONST (INTVAL (op1) + 1);

	return REG_P (op0)
	  && IS_ADDR_OR_PSEUDO_REG (op0)
	  && GET_CODE (op1) == CONST_INT
	  && IS_UINT5_CONST (INTVAL (op1));
      }
      break;

    default:
      break;
    }
  return 0;
}


/* ARx + 1-bit unsigned const or IRn
   *ARx, *+ARx(1), *-ARx(1), *+ARx(IRn), *-Arx(IRn)
   We don't include the pre/post inc/dec forms here since
   they are handled by the <> constraints.  */

int
c4x_S_constraint (rtx op)
{
  enum machine_mode mode = GET_MODE (op);
  if (GET_CODE (op) != MEM)
    return 0;
  op = XEXP (op, 0);
  switch (GET_CODE (op))
    {
    case REG:
      return 1;

    case PRE_MODIFY:
    case POST_MODIFY:
      {
	rtx op0 = XEXP (op, 0);
	rtx op1 = XEXP (op, 1);
	
	if ((GET_CODE (op1) != PLUS && GET_CODE (op1) != MINUS)
	    || (op0 != XEXP (op1, 0)))
	  return 0;
	
	op0 = XEXP (op1, 0);
	op1 = XEXP (op1, 1);
	return REG_P (op0) && REG_P (op1);
	/* Pre or post_modify with a displacement of 0 or 1 
	   should not be generated.  */
      }
      break;

    case PLUS:
      {
	rtx op0 = XEXP (op, 0);
	rtx op1 = XEXP (op, 1);

	if (!REG_P (op0))
	  return 0;

	if (REG_P (op1))
	  return 1;

	if (GET_CODE (op1) != CONST_INT)
	  return 0;
	
	/* HImode and HFmode must be offsettable.  */
	if (mode == HImode || mode == HFmode)
	  return IS_DISP1_OFF_CONST (INTVAL (op1));
	
	return IS_DISP1_CONST (INTVAL (op1));
      }
      break;

    default:
      break;
    }
  return 0;
}


int
c4x_S_indirect (rtx op)
{
  enum machine_mode mode = GET_MODE (op);
  if (GET_CODE (op) != MEM)
    return 0;

  op = XEXP (op, 0);
  switch (GET_CODE (op))
    {
    case PRE_DEC:
    case POST_DEC:
      if (mode != QImode && mode != QFmode)
	return 0;
    case PRE_INC:
    case POST_INC:
      op = XEXP (op, 0);

    case REG:
      return IS_ADDR_OR_PSEUDO_REG (op);

    case PRE_MODIFY:
    case POST_MODIFY:
      {
	rtx op0 = XEXP (op, 0);
	rtx op1 = XEXP (op, 1);
	
	if (mode != QImode && mode != QFmode)
	  return 0;

	if ((GET_CODE (op1) != PLUS && GET_CODE (op1) != MINUS)
	    || (op0 != XEXP (op1, 0)))
	  return 0;
	
	op0 = XEXP (op1, 0);
	op1 = XEXP (op1, 1);
	return REG_P (op0) && IS_ADDR_OR_PSEUDO_REG (op0)
	  && REG_P (op1) && IS_INDEX_OR_PSEUDO_REG (op1);
	/* Pre or post_modify with a displacement of 0 or 1 
	   should not be generated.  */
      }

    case PLUS:
      {
	rtx op0 = XEXP (op, 0);
	rtx op1 = XEXP (op, 1);

	if (REG_P (op0))
	  {
	    /* HImode and HFmode must be offsettable.  */
	    if (mode == HImode || mode == HFmode)
	      return IS_ADDR_OR_PSEUDO_REG (op0)
		&& GET_CODE (op1) == CONST_INT 
		&& IS_DISP1_OFF_CONST (INTVAL (op1));

	    if (REG_P (op1))
	      return (IS_INDEX_OR_PSEUDO_REG (op1)
		      && IS_ADDR_OR_PSEUDO_REG (op0))
		|| (IS_ADDR_OR_PSEUDO_REG (op1)
		    && IS_INDEX_OR_PSEUDO_REG (op0));
	    
	    return IS_ADDR_OR_PSEUDO_REG (op0)
	      && GET_CODE (op1) == CONST_INT 
	      && IS_DISP1_CONST (INTVAL (op1));
	  }
      }
      break;

    default:
      break;
    }
  return 0;
}


/* Direct memory operand.  */

int
c4x_T_constraint (rtx op)
{
  if (GET_CODE (op) != MEM)
    return 0;
  op = XEXP (op, 0);

  if (GET_CODE (op) != LO_SUM)
    {
      /* Allow call operands.  */
      return GET_CODE (op) == SYMBOL_REF
	&& GET_MODE (op) == Pmode
	&& SYMBOL_REF_FUNCTION_P (op);
    }

  /* HImode and HFmode are not offsettable.  */
  if (GET_MODE (op) == HImode || GET_CODE (op) == HFmode)
    return 0;

  if ((GET_CODE (XEXP (op, 0)) == REG)
      && (REGNO (XEXP (op, 0)) == DP_REGNO))
    return c4x_U_constraint (XEXP (op, 1));
  
  return 0;
}


/* Symbolic operand.  */

int
c4x_U_constraint (rtx op)
{
  /* Don't allow direct addressing to an arbitrary constant.  */
  return GET_CODE (op) == CONST
	 || GET_CODE (op) == SYMBOL_REF
	 || GET_CODE (op) == LABEL_REF;
}


int
c4x_autoinc_operand (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  if (GET_CODE (op) == MEM)
    {
      enum rtx_code code = GET_CODE (XEXP (op, 0));
      
      if (code == PRE_INC
	  || code == PRE_DEC
	  || code == POST_INC
	  || code == POST_DEC
	  || code == PRE_MODIFY
	  || code == POST_MODIFY
	  )
	return 1;
    }
  return 0;
}


int
mixed_subreg_operand (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  /* Allow (subreg:HF (reg:HI)) that be generated for a union of an
     int and a long double.  */
  if (GET_CODE (op) == SUBREG
      && (GET_MODE (op) == QFmode)
      && (GET_MODE (SUBREG_REG (op)) == QImode
	  || GET_MODE (SUBREG_REG (op)) == HImode))
    return 1;
  return 0;
}


int
reg_imm_operand (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  if (REG_P (op) || CONSTANT_P (op))
    return 1;
  return 0;
}


int
not_modify_reg (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  if (REG_P (op) || CONSTANT_P (op))
    return 1;
  if (GET_CODE (op) != MEM)
    return 0;
  op = XEXP (op, 0);
  switch (GET_CODE (op))
    {
    case REG:
      return 1;

    case PLUS:
      {
	rtx op0 = XEXP (op, 0);
	rtx op1 = XEXP (op, 1);

	if (! REG_P (op0))
	  return 0;
	
	if (REG_P (op1) || GET_CODE (op1) == CONST_INT)
	  return 1;
      }

    case LO_SUM:
      {
	rtx op0 = XEXP (op, 0);
	  
	if (REG_P (op0) && REGNO (op0) == DP_REGNO)
	  return 1;
      }
      break;
     
    case CONST:
    case SYMBOL_REF:
    case LABEL_REF:
      return 1;

    default:
      break;
    }
  return 0;
}


int
not_rc_reg (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  if (REG_P (op) && REGNO (op) == RC_REGNO)
    return 0;
  return 1;
}


static void 
c4x_S_address_parse (rtx op, int *base, int *incdec, int *index, int *disp)
{
  *base = 0;
  *incdec = 0;
  *index = 0;
  *disp = 0;
       
  if (GET_CODE (op) != MEM)
    fatal_insn ("invalid indirect memory address", op);
  
  op = XEXP (op, 0);
  switch (GET_CODE (op))
    {
    case PRE_DEC:
      *base = REGNO (XEXP (op, 0));
      *incdec = 1;
      *disp = -1;
      return;

    case POST_DEC:
      *base = REGNO (XEXP (op, 0));
      *incdec = 1;
      *disp = 0;
      return;

    case PRE_INC:
      *base = REGNO (XEXP (op, 0));
      *incdec = 1;
      *disp = 1;
      return;

    case POST_INC:
      *base = REGNO (XEXP (op, 0));
      *incdec = 1;
      *disp = 0;
      return;

    case POST_MODIFY:
      *base = REGNO (XEXP (op, 0));
      if (REG_P (XEXP (XEXP (op, 1), 1)))
	{
	  *index = REGNO (XEXP (XEXP (op, 1), 1));
	  *disp = 0;		/* ??? */
	}
      else
	  *disp = INTVAL (XEXP (XEXP (op, 1), 1));
      *incdec = 1;
      return;

    case PRE_MODIFY:
      *base = REGNO (XEXP (op, 0));
      if (REG_P (XEXP (XEXP (op, 1), 1)))
	{
	  *index = REGNO (XEXP (XEXP (op, 1), 1));
	  *disp = 1;		/* ??? */
	}
      else
	  *disp = INTVAL (XEXP (XEXP (op, 1), 1));
      *incdec = 1;

      return;

    case REG:
      *base = REGNO (op);
      return;

    case PLUS:
      {
	rtx op0 = XEXP (op, 0);
	rtx op1 = XEXP (op, 1);

	if (c4x_a_register (op0))
	  {
	    if (c4x_x_register (op1))
	      {
		*base = REGNO (op0);
		*index = REGNO (op1);
		return;
	      }
	    else if ((GET_CODE (op1) == CONST_INT 
		      && IS_DISP1_CONST (INTVAL (op1))))
	      {
		*base = REGNO (op0);
		*disp = INTVAL (op1);
		return;
	      }
	  }
	else if (c4x_x_register (op0) && c4x_a_register (op1))
	  {
	    *base = REGNO (op1);
	    *index = REGNO (op0);
	    return;
	  }
      }
      /* Fall through.  */

    default:
      fatal_insn ("invalid indirect (S) memory address", op);
    }
}


int
c4x_address_conflict (rtx op0, rtx op1, int store0, int store1)
{
  int base0;
  int base1;
  int incdec0;
  int incdec1;
  int index0;
  int index1;
  int disp0;
  int disp1;
  
  if (MEM_VOLATILE_P (op0) && MEM_VOLATILE_P (op1))
    return 1;

  c4x_S_address_parse (op0, &base0, &incdec0, &index0, &disp0);
  c4x_S_address_parse (op1, &base1, &incdec1, &index1, &disp1);

  if (store0 && store1)
    {
      /* If we have two stores in parallel to the same address, then
	 the C4x only executes one of the stores.  This is unlikely to
	 cause problems except when writing to a hardware device such
	 as a FIFO since the second write will be lost.  The user
	 should flag the hardware location as being volatile so that
	 we don't do this optimization.  While it is unlikely that we
	 have an aliased address if both locations are not marked
	 volatile, it is probably safer to flag a potential conflict
	 if either location is volatile.  */
      if (! flag_argument_noalias)
	{
	  if (MEM_VOLATILE_P (op0) || MEM_VOLATILE_P (op1))
	    return 1;
	}
    }

  /* If have a parallel load and a store to the same address, the load
     is performed first, so there is no conflict.  Similarly, there is
     no conflict if have parallel loads from the same address.  */

  /* Cannot use auto increment or auto decrement twice for same
     base register.  */
  if (base0 == base1 && incdec0 && incdec0)
    return 1;

  /* It might be too confusing for GCC if we have use a base register
     with a side effect and a memory reference using the same register
     in parallel.  */
  if (! TARGET_DEVEL && base0 == base1 && (incdec0 || incdec1))
    return 1;

  /* We cannot optimize the case where op1 and op2 refer to the same
     address.  */
  if (base0 == base1 && disp0 == disp1 && index0 == index1)
    return 1;

  /* No conflict.  */
  return 0;
}


/* Check for while loop inside a decrement and branch loop.  */

int
c4x_label_conflict (rtx insn, rtx jump, rtx db)
{
  while (insn)
    {
      if (GET_CODE (insn) == CODE_LABEL)
	{
          if (CODE_LABEL_NUMBER (jump) == CODE_LABEL_NUMBER (insn))
	    return 1;
          if (CODE_LABEL_NUMBER (db) == CODE_LABEL_NUMBER (insn))
	    return 0;
	}
      insn = PREV_INSN (insn);
    }
  return 1;
}


/* Validate combination of operands for parallel load/store instructions.  */

int
valid_parallel_load_store (rtx *operands,
			   enum machine_mode mode ATTRIBUTE_UNUSED)
{
  rtx op0 = operands[0];
  rtx op1 = operands[1];
  rtx op2 = operands[2];
  rtx op3 = operands[3];

  if (GET_CODE (op0) == SUBREG)
    op0 = SUBREG_REG (op0);
  if (GET_CODE (op1) == SUBREG)
    op1 = SUBREG_REG (op1);
  if (GET_CODE (op2) == SUBREG)
    op2 = SUBREG_REG (op2);
  if (GET_CODE (op3) == SUBREG)
    op3 = SUBREG_REG (op3);

  /* The patterns should only allow ext_low_reg_operand() or
     par_ind_operand() operands.  Thus of the 4 operands, only 2
     should be REGs and the other 2 should be MEMs.  */

  /* This test prevents the multipack pass from using this pattern if
     op0 is used as an index or base register in op2 or op3, since
     this combination will require reloading.  */
  if (GET_CODE (op0) == REG
      && ((GET_CODE (op2) == MEM && reg_mentioned_p (op0, XEXP (op2, 0)))
	  || (GET_CODE (op3) == MEM && reg_mentioned_p (op0, XEXP (op3, 0)))))
    return 0;

  /* LDI||LDI.  */
  if (GET_CODE (op0) == REG && GET_CODE (op2) == REG)
    return (REGNO (op0) != REGNO (op2))
      && GET_CODE (op1) == MEM && GET_CODE (op3) == MEM
      && ! c4x_address_conflict (op1, op3, 0, 0);

  /* STI||STI.  */
  if (GET_CODE (op1) == REG && GET_CODE (op3) == REG)
    return GET_CODE (op0) == MEM && GET_CODE (op2) == MEM
      && ! c4x_address_conflict (op0, op2, 1, 1);

  /* LDI||STI.  */
  if (GET_CODE (op0) == REG && GET_CODE (op3) == REG)
    return GET_CODE (op1) == MEM && GET_CODE (op2) == MEM
      && ! c4x_address_conflict (op1, op2, 0, 1);

  /* STI||LDI.  */
  if (GET_CODE (op1) == REG && GET_CODE (op2) == REG)
    return GET_CODE (op0) == MEM && GET_CODE (op3) == MEM
      && ! c4x_address_conflict (op0, op3, 1, 0);

  return 0;
}


int
valid_parallel_operands_4 (rtx *operands,
			   enum machine_mode mode ATTRIBUTE_UNUSED)
{
  rtx op0 = operands[0];
  rtx op2 = operands[2];

  if (GET_CODE (op0) == SUBREG)
    op0 = SUBREG_REG (op0);
  if (GET_CODE (op2) == SUBREG)
    op2 = SUBREG_REG (op2);

  /* This test prevents the multipack pass from using this pattern if
     op0 is used as an index or base register in op2, since this combination
     will require reloading.  */
  if (GET_CODE (op0) == REG
      && GET_CODE (op2) == MEM
      && reg_mentioned_p (op0, XEXP (op2, 0)))
    return 0;

  return 1;
}


int
valid_parallel_operands_5 (rtx *operands,
			   enum machine_mode mode ATTRIBUTE_UNUSED)
{
  int regs = 0;
  rtx op0 = operands[0];
  rtx op1 = operands[1];
  rtx op2 = operands[2];
  rtx op3 = operands[3];

  if (GET_CODE (op0) == SUBREG)
    op0 = SUBREG_REG (op0);
  if (GET_CODE (op1) == SUBREG)
    op1 = SUBREG_REG (op1);
  if (GET_CODE (op2) == SUBREG)
    op2 = SUBREG_REG (op2);

  /* The patterns should only allow ext_low_reg_operand() or
     par_ind_operand() operands.  Operands 1 and 2 may be commutative
     but only one of them can be a register.  */
  if (GET_CODE (op1) == REG)
    regs++;
  if (GET_CODE (op2) == REG)
    regs++;

  if (regs != 1)
    return 0;

  /* This test prevents the multipack pass from using this pattern if
     op0 is used as an index or base register in op3, since this combination
     will require reloading.  */
  if (GET_CODE (op0) == REG
      && GET_CODE (op3) == MEM
      && reg_mentioned_p (op0, XEXP (op3, 0)))
    return 0;

  return 1;
}


int
valid_parallel_operands_6 (rtx *operands,
			   enum machine_mode mode ATTRIBUTE_UNUSED)
{
  int regs = 0;
  rtx op0 = operands[0];
  rtx op1 = operands[1];
  rtx op2 = operands[2];
  rtx op4 = operands[4];
  rtx op5 = operands[5];

  if (GET_CODE (op1) == SUBREG)
    op1 = SUBREG_REG (op1);
  if (GET_CODE (op2) == SUBREG)
    op2 = SUBREG_REG (op2);
  if (GET_CODE (op4) == SUBREG)
    op4 = SUBREG_REG (op4);
  if (GET_CODE (op5) == SUBREG)
    op5 = SUBREG_REG (op5);

  /* The patterns should only allow ext_low_reg_operand() or
     par_ind_operand() operands.  Thus of the 4 input operands, only 2
     should be REGs and the other 2 should be MEMs.  */

  if (GET_CODE (op1) == REG)
    regs++;
  if (GET_CODE (op2) == REG)
    regs++;
  if (GET_CODE (op4) == REG)
    regs++;
  if (GET_CODE (op5) == REG)
    regs++;

  /* The new C30/C40 silicon dies allow 3 regs of the 4 input operands. 
     Perhaps we should count the MEMs as well?  */
  if (regs != 2)
    return 0;

  /* This test prevents the multipack pass from using this pattern if
     op0 is used as an index or base register in op4 or op5, since
     this combination will require reloading.  */
  if (GET_CODE (op0) == REG
      && ((GET_CODE (op4) == MEM && reg_mentioned_p (op0, XEXP (op4, 0)))
	  || (GET_CODE (op5) == MEM && reg_mentioned_p (op0, XEXP (op5, 0)))))
    return 0;

  return 1;
}


/* Validate combination of src operands.  Note that the operands have
   been screened by the src_operand predicate.  We just have to check
   that the combination of operands is valid.  If FORCE is set, ensure
   that the destination regno is valid if we have a 2 operand insn.  */

static int
c4x_valid_operands (enum rtx_code code, rtx *operands,
		    enum machine_mode mode ATTRIBUTE_UNUSED,
		    int force)
{
  rtx op0;
  rtx op1;
  rtx op2;
  enum rtx_code code1;
  enum rtx_code code2;


  /* FIXME, why can't we tighten the operands for IF_THEN_ELSE?  */
  if (code == IF_THEN_ELSE)
      return 1 || (operands[0] == operands[2] || operands[0] == operands[3]);

  if (code == COMPARE)
    {
      op1 = operands[0];
      op2 = operands[1];
    }
  else
    {
      op1 = operands[1];
      op2 = operands[2];
    }

  op0 = operands[0];

  if (GET_CODE (op0) == SUBREG)
    op0 = SUBREG_REG (op0);
  if (GET_CODE (op1) == SUBREG)
    op1 = SUBREG_REG (op1);
  if (GET_CODE (op2) == SUBREG)
    op2 = SUBREG_REG (op2);

  code1 = GET_CODE (op1);
  code2 = GET_CODE (op2);

  
  if (code1 == REG && code2 == REG)
    return 1;

  if (code1 == MEM && code2 == MEM)
    {
      if (c4x_S_indirect (op1) && c4x_S_indirect (op2))
	return 1;
      return c4x_R_indirect (op1) && c4x_R_indirect (op2);
    }

  /* We cannot handle two MEMs or two CONSTS, etc.  */
  if (code1 == code2)
    return 0;

  if (code1 == REG)
    {
      switch (code2)
	{
	case CONST_INT:
	  if (c4x_J_constant (op2) && c4x_R_indirect (op1))
	    return 1;
	  break;
	  
	case CONST_DOUBLE:
	  if (! c4x_H_constant (op2))
	    return 0;
	  break;

	  /* Any valid memory operand screened by src_operand is OK.  */
  	case MEM:
	  break;
	  
	default:
	  fatal_insn ("c4x_valid_operands: Internal error", op2);
	  break;
	}
      
      if (GET_CODE (op0) == SCRATCH)
	  return 1;

      if (!REG_P (op0))
	  return 0;

      /* Check that we have a valid destination register for a two operand
	 instruction.  */
      return ! force || code == COMPARE || REGNO (op1) == REGNO (op0);
    }


  /* Check non-commutative operators.  */
  if (code == ASHIFTRT || code == LSHIFTRT
      || code == ASHIFT || code == COMPARE)
    return code2 == REG
      && (c4x_S_indirect (op1) || c4x_R_indirect (op1));


  /* Assume MINUS is commutative since the subtract patterns
     also support the reverse subtract instructions.  Since op1
     is not a register, and op2 is a register, op1 can only
     be a restricted memory operand for a shift instruction.  */
  if (code2 == REG)
    {
      switch (code1)
	{
	case CONST_INT:
	  break;
      
	case CONST_DOUBLE:
	  if (! c4x_H_constant (op1))
	    return 0;
	  break;

	  /* Any valid memory operand screened by src_operand is OK.  */      
	case MEM:
	  break;
	  
	default:
	  abort ();
	  break;
	}

      if (GET_CODE (op0) == SCRATCH)
	  return 1;

      if (!REG_P (op0))
	  return 0;

      /* Check that we have a valid destination register for a two operand
	 instruction.  */
      return ! force || REGNO (op1) == REGNO (op0);
    }
      
  if (c4x_J_constant (op1) && c4x_R_indirect (op2))
    return 1;

  return 0;
}


int valid_operands (enum rtx_code code, rtx *operands, enum machine_mode mode)
{

  /* If we are not optimizing then we have to let anything go and let
     reload fix things up.  instantiate_decl in function.c can produce
     invalid insns by changing the offset of a memory operand from a
     valid one into an invalid one, when the second operand is also a
     memory operand.  The alternative is not to allow two memory
     operands for an insn when not optimizing.  The problem only rarely
     occurs, for example with the C-torture program DFcmp.c.  */

  return ! optimize || c4x_valid_operands (code, operands, mode, 0);
}


int
legitimize_operands (enum rtx_code code, rtx *operands, enum machine_mode mode)
{
  /* Compare only has 2 operands.  */
  if (code == COMPARE)
    {
      /* During RTL generation, force constants into pseudos so that
	 they can get hoisted out of loops.  This will tie up an extra
	 register but can save an extra cycle.  Only do this if loop
	 optimization enabled.  (We cannot pull this trick for add and
	 sub instructions since the flow pass won't find
	 autoincrements etc.)  This allows us to generate compare
	 instructions like CMPI R0, *AR0++ where R0 = 42, say, instead
	 of LDI *AR0++, R0; CMPI 42, R0. 

	 Note that expand_binops will try to load an expensive constant
	 into a register if it is used within a loop.  Unfortunately,
	 the cost mechanism doesn't allow us to look at the other
	 operand to decide whether the constant is expensive.  */
      
      if (! reload_in_progress
	  && TARGET_HOIST
	  && optimize > 0
	  && GET_CODE (operands[1]) == CONST_INT 
	  && rtx_cost (operands[1], code) > 1)
	operands[1] = force_reg (mode, operands[1]);
      
      if (! reload_in_progress
          && ! c4x_valid_operands (code, operands, mode, 0))
	operands[0] = force_reg (mode, operands[0]);
      return 1;
    }
  
  /* We cannot do this for ADDI/SUBI insns since we will
     defeat the flow pass from finding autoincrement addressing
     opportunities.  */
  if (! reload_in_progress
      && ! ((code == PLUS || code == MINUS) && mode == Pmode)
      && TARGET_HOIST
      && optimize > 1
      && GET_CODE (operands[2]) == CONST_INT
      && rtx_cost (operands[2], code) > 1)
    operands[2] = force_reg (mode, operands[2]);

  /* We can get better code on a C30 if we force constant shift counts
     into a register.  This way they can get hoisted out of loops,
     tying up a register but saving an instruction.  The downside is
     that they may get allocated to an address or index register, and
     thus we will get a pipeline conflict if there is a nearby
     indirect address using an address register. 

     Note that expand_binops will not try to load an expensive constant
     into a register if it is used within a loop for a shift insn.  */
  
  if (! reload_in_progress
      && ! c4x_valid_operands (code, operands, mode, TARGET_FORCE))
    {
      /* If the operand combination is invalid, we force operand1 into a
         register, preventing reload from having doing to do this at a
         later stage.  */
      operands[1] = force_reg (mode, operands[1]);
      if (TARGET_FORCE)
	{
	  emit_move_insn (operands[0], operands[1]);
	  operands[1] = copy_rtx (operands[0]);
	}
      else
	{
	  /* Just in case...  */
	  if (! c4x_valid_operands (code, operands, mode, 0))
	    operands[2] = force_reg (mode, operands[2]);
	}
    }

  /* Right shifts require a negative shift count, but GCC expects
     a positive count, so we emit a NEG.  */
  if ((code == ASHIFTRT || code == LSHIFTRT)
      && (GET_CODE (operands[2]) != CONST_INT))
    operands[2] = gen_rtx_NEG (mode, negate_rtx (mode, operands[2]));
  

  /* When the shift count is greater than 32 then the result 
     can be implementation dependent.  We truncate the result to
     fit in 5 bits so that we do not emit invalid code when
     optimizing---such as trying to generate lhu2 with 20021124-1.c.  */
  if (((code == ASHIFTRT || code == LSHIFTRT || code == ASHIFT)
      && (GET_CODE (operands[2]) == CONST_INT))
      && INTVAL (operands[2]) > (GET_MODE_BITSIZE (mode) - 1))
      operands[2]
	  = GEN_INT (INTVAL (operands[2]) & (GET_MODE_BITSIZE (mode) - 1));

  return 1;
}


/* The following predicates are used for instruction scheduling.  */

int
group1_reg_operand (rtx op, enum machine_mode mode)
{
  if (mode != VOIDmode && mode != GET_MODE (op))
    return 0;
  if (GET_CODE (op) == SUBREG)
    op = SUBREG_REG (op);
  return REG_P (op) && (! reload_completed || IS_GROUP1_REG (op));
}


int
group1_mem_operand (rtx op, enum machine_mode mode)
{
  if (mode != VOIDmode && mode != GET_MODE (op))
    return 0;

  if (GET_CODE (op) == MEM)
    {
      op = XEXP (op, 0);
      if (GET_CODE (op) == PLUS)
	{
	  rtx op0 = XEXP (op, 0);
	  rtx op1 = XEXP (op, 1);

	  if ((REG_P (op0) && (! reload_completed || IS_GROUP1_REG (op0)))
	      || (REG_P (op1) && (! reload_completed || IS_GROUP1_REG (op1))))
	    return 1;
	}
      else if ((REG_P (op)) && (! reload_completed || IS_GROUP1_REG (op)))
	return 1;
    }

  return 0;
}


/* Return true if any one of the address registers.  */

int
arx_reg_operand (rtx op, enum machine_mode mode)
{
  if (mode != VOIDmode && mode != GET_MODE (op))
    return 0;
  if (GET_CODE (op) == SUBREG)
    op = SUBREG_REG (op);
  return REG_P (op) && (! reload_completed || IS_ADDR_REG (op));
}


static int
c4x_arn_reg_operand (rtx op, enum machine_mode mode, unsigned int regno)
{
  if (mode != VOIDmode && mode != GET_MODE (op))
    return 0;
  if (GET_CODE (op) == SUBREG)
    op = SUBREG_REG (op);
  return REG_P (op) && (! reload_completed || (REGNO (op) == regno));
}


static int
c4x_arn_mem_operand (rtx op, enum machine_mode mode, unsigned int regno)
{
  if (mode != VOIDmode && mode != GET_MODE (op))
    return 0;

  if (GET_CODE (op) == MEM)
    {
      op = XEXP (op, 0);
      switch (GET_CODE (op))
	{
	case PRE_DEC:
	case POST_DEC:
	case PRE_INC:
	case POST_INC:
	  op = XEXP (op, 0);

	case REG:
          return REG_P (op) && (! reload_completed || (REGNO (op) == regno));

	case PRE_MODIFY:
	case POST_MODIFY:
          if (REG_P (XEXP (op, 0)) && (! reload_completed 
				       || (REGNO (XEXP (op, 0)) == regno)))
	    return 1;
          if (REG_P (XEXP (XEXP (op, 1), 1))
	      && (! reload_completed
		  || (REGNO (XEXP (XEXP (op, 1), 1)) == regno)))
	    return 1;
	  break;

	case PLUS:
	  {
	    rtx op0 = XEXP (op, 0);
	    rtx op1 = XEXP (op, 1);

	    if ((REG_P (op0) && (! reload_completed
				 || (REGNO (op0) == regno)))
	        || (REG_P (op1) && (! reload_completed
				    || (REGNO (op1) == regno))))
	      return 1;
	  }
	  break;

	default:
	  break;
	}
    }
  return 0;
}


int
ar0_reg_operand (rtx op, enum machine_mode mode)
{
  return c4x_arn_reg_operand (op, mode, AR0_REGNO);
}


int
ar0_mem_operand (rtx op, enum machine_mode mode)
{
  return c4x_arn_mem_operand (op, mode, AR0_REGNO);
}


int
ar1_reg_operand (rtx op, enum machine_mode mode)
{
  return c4x_arn_reg_operand (op, mode, AR1_REGNO);
}


int
ar1_mem_operand (rtx op, enum machine_mode mode)
{
  return c4x_arn_mem_operand (op, mode, AR1_REGNO);
}


int
ar2_reg_operand (rtx op, enum machine_mode mode)
{
  return c4x_arn_reg_operand (op, mode, AR2_REGNO);
}


int
ar2_mem_operand (rtx op, enum machine_mode mode)
{
  return c4x_arn_mem_operand (op, mode, AR2_REGNO);
}


int
ar3_reg_operand (rtx op, enum machine_mode mode)
{
  return c4x_arn_reg_operand (op, mode, AR3_REGNO);
}


int
ar3_mem_operand (rtx op, enum machine_mode mode)
{
  return c4x_arn_mem_operand (op, mode, AR3_REGNO);
}


int
ar4_reg_operand (rtx op, enum machine_mode mode)
{
  return c4x_arn_reg_operand (op, mode, AR4_REGNO);
}


int
ar4_mem_operand (rtx op, enum machine_mode mode)
{
  return c4x_arn_mem_operand (op, mode, AR4_REGNO);
}


int
ar5_reg_operand (rtx op, enum machine_mode mode)
{
  return c4x_arn_reg_operand (op, mode, AR5_REGNO);
}


int
ar5_mem_operand (rtx op, enum machine_mode mode)
{
  return c4x_arn_mem_operand (op, mode, AR5_REGNO);
}


int
ar6_reg_operand (rtx op, enum machine_mode mode)
{
  return c4x_arn_reg_operand (op, mode, AR6_REGNO);
}


int
ar6_mem_operand (rtx op, enum machine_mode mode)
{
  return c4x_arn_mem_operand (op, mode, AR6_REGNO);
}


int
ar7_reg_operand (rtx op, enum machine_mode mode)
{
  return c4x_arn_reg_operand (op, mode, AR7_REGNO);
}


int
ar7_mem_operand (rtx op, enum machine_mode mode)
{
  return c4x_arn_mem_operand (op, mode, AR7_REGNO);
}


int
ir0_reg_operand (rtx op, enum machine_mode mode)
{
  return c4x_arn_reg_operand (op, mode, IR0_REGNO);
}


int
ir0_mem_operand (rtx op, enum machine_mode mode)
{
  return c4x_arn_mem_operand (op, mode, IR0_REGNO);
}


int
ir1_reg_operand (rtx op, enum machine_mode mode)
{
  return c4x_arn_reg_operand (op, mode, IR1_REGNO);
}


int
ir1_mem_operand (rtx op, enum machine_mode mode)
{
  return c4x_arn_mem_operand (op, mode, IR1_REGNO);
}


/* This is similar to operand_subword but allows autoincrement
   addressing.  */

rtx
c4x_operand_subword (rtx op, int i, int validate_address,
		     enum machine_mode  mode)
{
  if (mode != HImode && mode != HFmode)
    fatal_insn ("c4x_operand_subword: invalid mode", op);

  if (mode == HFmode && REG_P (op))
    fatal_insn ("c4x_operand_subword: invalid operand", op);

  if (GET_CODE (op) == MEM)
    {
      enum rtx_code code = GET_CODE (XEXP (op, 0));
      enum machine_mode mode = GET_MODE (XEXP (op, 0));
      enum machine_mode submode;

      submode = mode;
      if (mode == HImode)
	submode = QImode;
      else if (mode == HFmode)
	submode = QFmode;

      switch (code)
	{
	case POST_INC:
	case PRE_INC:
	  return gen_rtx_MEM (submode, XEXP (op, 0));
	  
	case POST_DEC:
	case PRE_DEC:
	case PRE_MODIFY:
	case POST_MODIFY:
	  /* We could handle these with some difficulty.
	     e.g., *p-- => *(p-=2); *(p+1).  */
	  fatal_insn ("c4x_operand_subword: invalid autoincrement", op);

	case SYMBOL_REF:
	case LABEL_REF:
	case CONST:
	case CONST_INT:
	  fatal_insn ("c4x_operand_subword: invalid address", op);

	  /* Even though offsettable_address_p considers (MEM
	     (LO_SUM)) to be offsettable, it is not safe if the
	     address is at the end of the data page since we also have
	     to fix up the associated high PART.  In this case where
	     we are trying to split a HImode or HFmode memory
	     reference, we would have to emit another insn to reload a
	     new HIGH value.  It's easier to disable LO_SUM memory references
	     in HImode or HFmode and we probably get better code.  */
	case LO_SUM:
	  fatal_insn ("c4x_operand_subword: address not offsettable", op);
  
	default:
	  break;
	}
    }
  
  return operand_subword (op, i, validate_address, mode);
}

struct name_list
{
  struct name_list *next;
  const char *name;
};

static struct name_list *global_head;
static struct name_list *extern_head;


/* Add NAME to list of global symbols and remove from external list if
   present on external list.  */

void
c4x_global_label (const char *name)
{
  struct name_list *p, *last;

  /* Do not insert duplicate names, so linearly search through list of
     existing names.  */
  p = global_head;
  while (p)
    {
      if (strcmp (p->name, name) == 0)
	return;
      p = p->next;
    }
  p = (struct name_list *) xmalloc (sizeof *p);
  p->next = global_head;
  p->name = name;
  global_head = p;

  /* Remove this name from ref list if present.  */
  last = NULL;
  p = extern_head;
  while (p)
    {
      if (strcmp (p->name, name) == 0)
	{
	  if (last)
	    last->next = p->next;
	  else
	    extern_head = p->next;
	  break;
	}
      last = p;
      p = p->next;
    }
}


/* Add NAME to list of external symbols.  */

void
c4x_external_ref (const char *name)
{
  struct name_list *p;

  /* Do not insert duplicate names.  */
  p = extern_head;
  while (p)
    {
      if (strcmp (p->name, name) == 0)
	return;
      p = p->next;
    }
  
  /* Do not insert ref if global found.  */
  p = global_head;
  while (p)
    {
      if (strcmp (p->name, name) == 0)
	return;
      p = p->next;
    }
  p = (struct name_list *) xmalloc (sizeof *p);
  p->next = extern_head;
  p->name = name;
  extern_head = p;
}

/* We need to have a data section we can identify so that we can set
   the DP register back to a data pointer in the small memory model.
   This is only required for ISRs if we are paranoid that someone
   may have quietly changed this register on the sly.  */
static void
c4x_file_start (void)
{
  default_file_start ();
  fprintf (asm_out_file, "\t.version\t%d\n", c4x_cpu_version);
  fputs ("\n\t.data\ndata_sec:\n", asm_out_file);
}


static void
c4x_file_end (void)
{
  struct name_list *p;
  
  /* Output all external names that are not global.  */
  p = extern_head;
  while (p)
    {
      fprintf (asm_out_file, "\t.ref\t");
      assemble_name (asm_out_file, p->name);
      fprintf (asm_out_file, "\n");
      p = p->next;
    }
  fprintf (asm_out_file, "\t.end\n");
}


static void
c4x_check_attribute (const char *attrib, tree list, tree decl, tree *attributes)
{
  while (list != NULL_TREE
         && IDENTIFIER_POINTER (TREE_PURPOSE (list))
	 != IDENTIFIER_POINTER (DECL_NAME (decl)))
    list = TREE_CHAIN (list);
  if (list)
    *attributes = tree_cons (get_identifier (attrib), TREE_VALUE (list),
			     *attributes);
}


static void
c4x_insert_attributes (tree decl, tree *attributes)
{
  switch (TREE_CODE (decl))
    {
    case FUNCTION_DECL:
      c4x_check_attribute ("section", code_tree, decl, attributes);
      c4x_check_attribute ("const", pure_tree, decl, attributes);
      c4x_check_attribute ("noreturn", noreturn_tree, decl, attributes);
      c4x_check_attribute ("interrupt", interrupt_tree, decl, attributes);
      c4x_check_attribute ("naked", naked_tree, decl, attributes);
      break;

    case VAR_DECL:
      c4x_check_attribute ("section", data_tree, decl, attributes);
      break;

    default:
      break;
    }
}

/* Table of valid machine attributes.  */
const struct attribute_spec c4x_attribute_table[] =
{
  /* { name, min_len, max_len, decl_req, type_req, fn_type_req, handler } */
  { "interrupt",    0, 0, false, true,  true,  c4x_handle_fntype_attribute },
  { "naked",    0, 0, false, true,  true,  c4x_handle_fntype_attribute },
  { "leaf_pretend", 0, 0, false, true,  true,  c4x_handle_fntype_attribute },
  { NULL,           0, 0, false, false, false, NULL }
};

/* Handle an attribute requiring a FUNCTION_TYPE;
   arguments as in struct attribute_spec.handler.  */
static tree
c4x_handle_fntype_attribute (tree *node, tree name,
			     tree args ATTRIBUTE_UNUSED,
			     int flags ATTRIBUTE_UNUSED,
			     bool *no_add_attrs)
{
  if (TREE_CODE (*node) != FUNCTION_TYPE)
    {
      warning (OPT_Wattributes, "%qs attribute only applies to functions",
	       IDENTIFIER_POINTER (name));
      *no_add_attrs = true;
    }

  return NULL_TREE;
}


/* !!! FIXME to emit RPTS correctly.  */

int
c4x_rptb_rpts_p (rtx insn, rtx op)
{
  /* The next insn should be our label marking where the
     repeat block starts.  */
  insn = NEXT_INSN (insn);
  if (GET_CODE (insn) != CODE_LABEL)
    {
      /* Some insns may have been shifted between the RPTB insn
         and the top label... They were probably destined to
         be moved out of the loop.  For now, let's leave them
         where they are and print a warning.  We should
         probably move these insns before the repeat block insn.  */
      if (TARGET_DEBUG)
	fatal_insn ("c4x_rptb_rpts_p: Repeat block top label moved",
		    insn);
      return 0;
    }

  /* Skip any notes.  */
  insn = next_nonnote_insn (insn);

  /* This should be our first insn in the loop.  */
  if (! INSN_P (insn))
    return 0;

  /* Skip any notes.  */
  insn = next_nonnote_insn (insn);

  if (! INSN_P (insn))
    return 0;

  if (recog_memoized (insn) != CODE_FOR_rptb_end)
    return 0;

  if (TARGET_RPTS)
    return 1;

  return (GET_CODE (op) == CONST_INT) && TARGET_RPTS_CYCLES (INTVAL (op));
}


/* Check if register r11 is used as the destination of an insn.  */

static int
c4x_r11_set_p(rtx x)
{
  rtx set;
  int i, j;
  const char *fmt;

  if (x == 0)
    return 0;

  if (INSN_P (x) && GET_CODE (PATTERN (x)) == SEQUENCE)
    x = XVECEXP (PATTERN (x), 0, XVECLEN (PATTERN (x), 0) - 1);

  if (INSN_P (x) && (set = single_set (x)))
    x = SET_DEST (set);

  if (GET_CODE (x) == REG && REGNO (x) == R11_REGNO)
    return 1;

  fmt = GET_RTX_FORMAT (GET_CODE (x));
  for (i = GET_RTX_LENGTH (GET_CODE (x)) - 1; i >= 0; i--)
    {
      if (fmt[i] == 'e')
	{
          if (c4x_r11_set_p (XEXP (x, i)))
	    return 1;
	}
      else if (fmt[i] == 'E')
        for (j = XVECLEN (x, i) - 1; j >= 0; j--)
          if (c4x_r11_set_p (XVECEXP (x, i, j)))
	    return 1;
    }
  return 0;
}


/* The c4x sometimes has a problem when the insn before the laj insn
   sets the r11 register.  Check for this situation.  */

int
c4x_check_laj_p (rtx insn)
{
  insn = prev_nonnote_insn (insn);

  /* If this is the start of the function no nop is needed.  */
  if (insn == 0)
    return 0;

  /* If the previous insn is a code label we have to insert a nop. This
     could be a jump or table jump. We can find the normal jumps by
     scanning the function but this will not find table jumps.  */
  if (GET_CODE (insn) == CODE_LABEL)
    return 1;

  /* If the previous insn sets register r11 we have to insert a nop.  */
  if (c4x_r11_set_p (insn))
    return 1;

  /* No nop needed.  */
  return 0;
}


/* Adjust the cost of a scheduling dependency.  Return the new cost of
   a dependency LINK or INSN on DEP_INSN.  COST is the current cost. 
   A set of an address register followed by a use occurs a 2 cycle
   stall (reduced to a single cycle on the c40 using LDA), while
   a read of an address register followed by a use occurs a single cycle.  */

#define	SET_USE_COST	3
#define	SETLDA_USE_COST	2
#define	READ_USE_COST	2

static int
c4x_adjust_cost (rtx insn, rtx link, rtx dep_insn, int cost)
{
  /* Don't worry about this until we know what registers have been
     assigned.  */
  if (flag_schedule_insns == 0 && ! reload_completed)
    return 0;

  /* How do we handle dependencies where a read followed by another
     read causes a pipeline stall?  For example, a read of ar0 followed
     by the use of ar0 for a memory reference.  It looks like we
     need to extend the scheduler to handle this case.  */

  /* Reload sometimes generates a CLOBBER of a stack slot, e.g.,
     (clobber (mem:QI (plus:QI (reg:QI 11 ar3) (const_int 261)))),
     so only deal with insns we know about.  */
  if (recog_memoized (dep_insn) < 0)
    return 0;

  if (REG_NOTE_KIND (link) == 0)
    {
      int max = 0;

      /* Data dependency; DEP_INSN writes a register that INSN reads some
	 cycles later.  */
      if (TARGET_C3X)
	{
	  if (get_attr_setgroup1 (dep_insn) && get_attr_usegroup1 (insn))
	    max = SET_USE_COST > max ? SET_USE_COST : max;
	  if (get_attr_readarx (dep_insn) && get_attr_usegroup1 (insn))
	    max = READ_USE_COST > max ? READ_USE_COST : max;
	}
      else
	{
	  /* This could be significantly optimized. We should look
	     to see if dep_insn sets ar0-ar7 or ir0-ir1 and if
	     insn uses ar0-ar7.  We then test if the same register
	     is used.  The tricky bit is that some operands will
	     use several registers...  */
	  if (get_attr_setar0 (dep_insn) && get_attr_usear0 (insn))
	    max = SET_USE_COST > max ? SET_USE_COST : max;
	  if (get_attr_setlda_ar0 (dep_insn) && get_attr_usear0 (insn))
	    max = SETLDA_USE_COST > max ? SETLDA_USE_COST : max;
	  if (get_attr_readar0 (dep_insn) && get_attr_usear0 (insn))
	    max = READ_USE_COST > max ? READ_USE_COST : max;

	  if (get_attr_setar1 (dep_insn) && get_attr_usear1 (insn))
	    max = SET_USE_COST > max ? SET_USE_COST : max;
	  if (get_attr_setlda_ar1 (dep_insn) && get_attr_usear1 (insn))
	    max = SETLDA_USE_COST > max ? SETLDA_USE_COST : max;
	  if (get_attr_readar1 (dep_insn) && get_attr_usear1 (insn))
	    max = READ_USE_COST > max ? READ_USE_COST : max;

	  if (get_attr_setar2 (dep_insn) && get_attr_usear2 (insn))
	    max = SET_USE_COST > max ? SET_USE_COST : max;
	  if (get_attr_setlda_ar2 (dep_insn) && get_attr_usear2 (insn))
	    max = SETLDA_USE_COST > max ? SETLDA_USE_COST : max;
	  if (get_attr_readar2 (dep_insn) && get_attr_usear2 (insn))
	    max = READ_USE_COST > max ? READ_USE_COST : max;

	  if (get_attr_setar3 (dep_insn) && get_attr_usear3 (insn))
	    max = SET_USE_COST > max ? SET_USE_COST : max;
	  if (get_attr_setlda_ar3 (dep_insn) && get_attr_usear3 (insn))
	    max = SETLDA_USE_COST > max ? SETLDA_USE_COST : max;
	  if (get_attr_readar3 (dep_insn) && get_attr_usear3 (insn))
	    max = READ_USE_COST > max ? READ_USE_COST : max;

	  if (get_attr_setar4 (dep_insn) && get_attr_usear4 (insn))
	    max = SET_USE_COST > max ? SET_USE_COST : max;
	  if (get_attr_setlda_ar4 (dep_insn) && get_attr_usear4 (insn))
	    max = SETLDA_USE_COST > max ? SETLDA_USE_COST : max;
	  if (get_attr_readar4 (dep_insn) && get_attr_usear4 (insn))
	    max = READ_USE_COST > max ? READ_USE_COST : max;

	  if (get_attr_setar5 (dep_insn) && get_attr_usear5 (insn))
	    max = SET_USE_COST > max ? SET_USE_COST : max;
	  if (get_attr_setlda_ar5 (dep_insn) && get_attr_usear5 (insn))
	    max = SETLDA_USE_COST > max ? SETLDA_USE_COST : max;
	  if (get_attr_readar5 (dep_insn) && get_attr_usear5 (insn))
	    max = READ_USE_COST > max ? READ_USE_COST : max;

	  if (get_attr_setar6 (dep_insn) && get_attr_usear6 (insn))
	    max = SET_USE_COST > max ? SET_USE_COST : max;
	  if (get_attr_setlda_ar6 (dep_insn) && get_attr_usear6 (insn))
	    max = SETLDA_USE_COST > max ? SETLDA_USE_COST : max;
	  if (get_attr_readar6 (dep_insn) && get_attr_usear6 (insn))
	    max = READ_USE_COST > max ? READ_USE_COST : max;

	  if (get_attr_setar7 (dep_insn) && get_attr_usear7 (insn))
	    max = SET_USE_COST > max ? SET_USE_COST : max;
	  if (get_attr_setlda_ar7 (dep_insn) && get_attr_usear7 (insn))
	    max = SETLDA_USE_COST > max ? SETLDA_USE_COST : max;
	  if (get_attr_readar7 (dep_insn) && get_attr_usear7 (insn))
	    max = READ_USE_COST > max ? READ_USE_COST : max;

	  if (get_attr_setir0 (dep_insn) && get_attr_useir0 (insn))
	    max = SET_USE_COST > max ? SET_USE_COST : max;
	  if (get_attr_setlda_ir0 (dep_insn) && get_attr_useir0 (insn))
	    max = SETLDA_USE_COST > max ? SETLDA_USE_COST : max;

	  if (get_attr_setir1 (dep_insn) && get_attr_useir1 (insn))
	    max = SET_USE_COST > max ? SET_USE_COST : max;
	  if (get_attr_setlda_ir1 (dep_insn) && get_attr_useir1 (insn))
	    max = SETLDA_USE_COST > max ? SETLDA_USE_COST : max;
	}

      if (max)
	cost = max;

      /* For other data dependencies, the default cost specified in the
	 md is correct.  */
      return cost;
    }
  else if (REG_NOTE_KIND (link) == REG_DEP_ANTI)
    {
      /* Anti dependency; DEP_INSN reads a register that INSN writes some
	 cycles later.  */

      /* For c4x anti dependencies, the cost is 0.  */
      return 0;
    }
  else if (REG_NOTE_KIND (link) == REG_DEP_OUTPUT)
    {
      /* Output dependency; DEP_INSN writes a register that INSN writes some
	 cycles later.  */

      /* For c4x output dependencies, the cost is 0.  */
      return 0;
    }
  else
    abort ();
}

void
c4x_init_builtins (void)
{
  tree endlink = void_list_node;

  lang_hooks.builtin_function ("fast_ftoi",
			       build_function_type 
			       (integer_type_node,
				tree_cons (NULL_TREE, double_type_node,
					   endlink)),
			       C4X_BUILTIN_FIX, BUILT_IN_MD, NULL, NULL_TREE);
  lang_hooks.builtin_function ("ansi_ftoi",
			       build_function_type 
			       (integer_type_node, 
				tree_cons (NULL_TREE, double_type_node,
					   endlink)),
			       C4X_BUILTIN_FIX_ANSI, BUILT_IN_MD, NULL,
			       NULL_TREE);
  if (TARGET_C3X)
    lang_hooks.builtin_function ("fast_imult",
				 build_function_type
				 (integer_type_node, 
				  tree_cons (NULL_TREE, integer_type_node,
					     tree_cons (NULL_TREE,
							integer_type_node,
							endlink))),
				 C4X_BUILTIN_MPYI, BUILT_IN_MD, NULL,
				 NULL_TREE);
  else
    {
      lang_hooks.builtin_function ("toieee",
				   build_function_type 
				   (double_type_node,
				    tree_cons (NULL_TREE, double_type_node,
					       endlink)),
				   C4X_BUILTIN_TOIEEE, BUILT_IN_MD, NULL,
				   NULL_TREE);
      lang_hooks.builtin_function ("frieee",
				   build_function_type
				   (double_type_node, 
				    tree_cons (NULL_TREE, double_type_node,
					       endlink)),
				   C4X_BUILTIN_FRIEEE, BUILT_IN_MD, NULL,
				   NULL_TREE);
      lang_hooks.builtin_function ("fast_invf",
				   build_function_type 
				   (double_type_node, 
				    tree_cons (NULL_TREE, double_type_node,
					       endlink)),
				   C4X_BUILTIN_RCPF, BUILT_IN_MD, NULL,
				   NULL_TREE);
    }
}


rtx
c4x_expand_builtin (tree exp, rtx target,
		    rtx subtarget ATTRIBUTE_UNUSED,
		    enum machine_mode mode ATTRIBUTE_UNUSED,
		    int ignore ATTRIBUTE_UNUSED)
{
  tree fndecl = TREE_OPERAND (TREE_OPERAND (exp, 0), 0);
  unsigned int fcode = DECL_FUNCTION_CODE (fndecl);
  tree arglist = TREE_OPERAND (exp, 1);
  tree arg0, arg1;
  rtx r0, r1;

  switch (fcode)
    {
    case C4X_BUILTIN_FIX:
      arg0 = TREE_VALUE (arglist);
      r0 = expand_expr (arg0, NULL_RTX, QFmode, 0);
      if (! target || ! register_operand (target, QImode))
	target = gen_reg_rtx (QImode);
      emit_insn (gen_fixqfqi_clobber (target, r0));
      return target;

    case C4X_BUILTIN_FIX_ANSI:
      arg0 = TREE_VALUE (arglist);
      r0 = expand_expr (arg0, NULL_RTX, QFmode, 0);
      if (! target || ! register_operand (target, QImode))
	target = gen_reg_rtx (QImode);
      emit_insn (gen_fix_truncqfqi2 (target, r0));
      return target;

    case C4X_BUILTIN_MPYI:
      if (! TARGET_C3X)
	break;
      arg0 = TREE_VALUE (arglist);
      arg1 = TREE_VALUE (TREE_CHAIN (arglist));
      r0 = expand_expr (arg0, NULL_RTX, QImode, 0);
      r1 = expand_expr (arg1, NULL_RTX, QImode, 0);
      if (! target || ! register_operand (target, QImode))
	target = gen_reg_rtx (QImode);
      emit_insn (gen_mulqi3_24_clobber (target, r0, r1));
      return target;

    case C4X_BUILTIN_TOIEEE:
      if (TARGET_C3X)
	break;
      arg0 = TREE_VALUE (arglist);
      r0 = expand_expr (arg0, NULL_RTX, QFmode, 0);
      if (! target || ! register_operand (target, QFmode))
	target = gen_reg_rtx (QFmode);
      emit_insn (gen_toieee (target, r0));
      return target;

    case C4X_BUILTIN_FRIEEE:
      if (TARGET_C3X)
	break;
      arg0 = TREE_VALUE (arglist);
      r0 = expand_expr (arg0, NULL_RTX, QFmode, 0);
      if (register_operand (r0, QFmode))
	{
	  r1 = assign_stack_local (QFmode, GET_MODE_SIZE (QFmode), 0);
	  emit_move_insn (r1, r0);
	  r0 = r1;
	}
      if (! target || ! register_operand (target, QFmode))
	target = gen_reg_rtx (QFmode);
      emit_insn (gen_frieee (target, r0));
      return target;

    case C4X_BUILTIN_RCPF:
      if (TARGET_C3X)
	break;
      arg0 = TREE_VALUE (arglist);
      r0 = expand_expr (arg0, NULL_RTX, QFmode, 0);
      if (! target || ! register_operand (target, QFmode))
	target = gen_reg_rtx (QFmode);
      emit_insn (gen_rcpfqf_clobber (target, r0));
      return target;
    }
  return NULL_RTX;
}

static void
c4x_init_libfuncs (void)
{
  set_optab_libfunc (smul_optab, QImode, "__mulqi3");
  set_optab_libfunc (sdiv_optab, QImode, "__divqi3");
  set_optab_libfunc (udiv_optab, QImode, "__udivqi3");
  set_optab_libfunc (smod_optab, QImode, "__modqi3");
  set_optab_libfunc (umod_optab, QImode, "__umodqi3");
  set_optab_libfunc (sdiv_optab, QFmode, "__divqf3");
  set_optab_libfunc (smul_optab, HFmode, "__mulhf3");
  set_optab_libfunc (sdiv_optab, HFmode, "__divhf3");
  set_optab_libfunc (smul_optab, HImode, "__mulhi3");
  set_optab_libfunc (sdiv_optab, HImode, "__divhi3");
  set_optab_libfunc (udiv_optab, HImode, "__udivhi3");
  set_optab_libfunc (smod_optab, HImode, "__modhi3");
  set_optab_libfunc (umod_optab, HImode, "__umodhi3");
  set_optab_libfunc (ffs_optab,  QImode, "__ffs");
  smulhi3_libfunc           = init_one_libfunc ("__smulhi3_high");
  umulhi3_libfunc           = init_one_libfunc ("__umulhi3_high");
  fix_truncqfhi2_libfunc    = init_one_libfunc ("__fix_truncqfhi2");
  fixuns_truncqfhi2_libfunc = init_one_libfunc ("__ufix_truncqfhi2");
  fix_trunchfhi2_libfunc    = init_one_libfunc ("__fix_trunchfhi2");
  fixuns_trunchfhi2_libfunc = init_one_libfunc ("__ufix_trunchfhi2");
  floathiqf2_libfunc        = init_one_libfunc ("__floathiqf2");
  floatunshiqf2_libfunc     = init_one_libfunc ("__ufloathiqf2");
  floathihf2_libfunc        = init_one_libfunc ("__floathihf2");
  floatunshihf2_libfunc     = init_one_libfunc ("__ufloathihf2");
}

static void
c4x_asm_named_section (const char *name, unsigned int flags ATTRIBUTE_UNUSED,
		       tree decl ATTRIBUTE_UNUSED)
{
  fprintf (asm_out_file, "\t.sect\t\"%s\"\n", name);
}

static void
c4x_globalize_label (FILE *stream, const char *name)
{
  default_globalize_label (stream, name);
  c4x_global_label (name);
}

#define SHIFT_CODE_P(C) \
  ((C) == ASHIFT || (C) == ASHIFTRT || (C) == LSHIFTRT)
#define LOGICAL_CODE_P(C) \
  ((C) == NOT || (C) == AND || (C) == IOR || (C) == XOR)

/* Compute a (partial) cost for rtx X.  Return true if the complete
   cost has been computed, and false if subexpressions should be
   scanned.  In either case, *TOTAL contains the cost result.  */

static bool
c4x_rtx_costs (rtx x, int code, int outer_code, int *total)
{
  HOST_WIDE_INT val;

  switch (code)
    {
      /* Some small integers are effectively free for the C40.  We should
         also consider if we are using the small memory model.  With
         the big memory model we require an extra insn for a constant
         loaded from memory.  */

    case CONST_INT:
      val = INTVAL (x);
      if (c4x_J_constant (x))
	*total = 0;
      else if (! TARGET_C3X
	       && outer_code == AND
	       && (val == 255 || val == 65535))
	*total = 0;
      else if (! TARGET_C3X
	       && (outer_code == ASHIFTRT || outer_code == LSHIFTRT)
	       && (val == 16 || val == 24))
	*total = 0;
      else if (TARGET_C3X && SHIFT_CODE_P (outer_code))
	*total = 3;
      else if (LOGICAL_CODE_P (outer_code)
               ? c4x_L_constant (x) : c4x_I_constant (x))
	*total = 2;
      else
	*total = 4;
      return true;

    case CONST:
    case LABEL_REF:
    case SYMBOL_REF:
      *total = 4;
      return true;

    case CONST_DOUBLE:
      if (c4x_H_constant (x))
	*total = 2;
      else if (GET_MODE (x) == QFmode)
	*total = 4;
      else
	*total = 8;
      return true;

    /* ??? Note that we return true, rather than false so that rtx_cost
       doesn't include the constant costs.  Otherwise expand_mult will
       think that it is cheaper to synthesize a multiply rather than to
       use a multiply instruction.  I think this is because the algorithm
       synth_mult doesn't take into account the loading of the operands,
       whereas the calculation of mult_cost does.  */
    case PLUS:
    case MINUS:
    case AND:
    case IOR:
    case XOR:
    case ASHIFT:
    case ASHIFTRT:
    case LSHIFTRT:
      *total = COSTS_N_INSNS (1);
      return true;

    case MULT:
      *total = COSTS_N_INSNS (GET_MODE_CLASS (GET_MODE (x)) == MODE_FLOAT
			      || TARGET_MPYI ? 1 : 14);
      return true;

    case DIV:
    case UDIV:
    case MOD:
    case UMOD:
      *total = COSTS_N_INSNS (GET_MODE_CLASS (GET_MODE (x)) == MODE_FLOAT
			      ? 15 : 50);
      return true;

    default:
      return false;
    }
}

/* Worker function for TARGET_ASM_EXTERNAL_LIBCALL.  */

static void
c4x_external_libcall (rtx fun)
{
  /* This is only needed to keep asm30 happy for ___divqf3 etc.  */
  c4x_external_ref (XSTR (fun, 0));
}

/* Worker function for TARGET_STRUCT_VALUE_RTX.  */

static rtx
c4x_struct_value_rtx (tree fntype ATTRIBUTE_UNUSED,
		      int incoming ATTRIBUTE_UNUSED)
{
  return gen_rtx_REG (Pmode, AR0_REGNO);
}
