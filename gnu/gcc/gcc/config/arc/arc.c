/* Subroutines used for code generation on the Argonaut ARC cpu.
   Copyright (C) 1994, 1995, 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004,
   2005
   Free Software Foundation, Inc.

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

/* ??? This is an old port, and is undoubtedly suffering from bit rot.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
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
#include "recog.h"
#include "toplev.h"
#include "tm_p.h"
#include "target.h"
#include "target-def.h"

/* Which cpu we're compiling for.  */
int arc_cpu_type;

/* Name of mangle string to add to symbols to separate code compiled for each
   cpu (or NULL).  */
const char *arc_mangle_cpu;

/* Save the operands last given to a compare for use when we
   generate a scc or bcc insn.  */
rtx arc_compare_op0, arc_compare_op1;

/* Name of text, data, and rodata sections used in varasm.c.  */
const char *arc_text_section;
const char *arc_data_section;
const char *arc_rodata_section;

/* Array of valid operand punctuation characters.  */
char arc_punct_chars[256];

/* Variables used by arc_final_prescan_insn to implement conditional
   execution.  */
static int arc_ccfsm_state;
static int arc_ccfsm_current_cc;
static rtx arc_ccfsm_target_insn;
static int arc_ccfsm_target_label;

/* The maximum number of insns skipped which will be conditionalised if
   possible.  */
#define MAX_INSNS_SKIPPED 3

/* A nop is needed between a 4 byte insn that sets the condition codes and
   a branch that uses them (the same isn't true for an 8 byte insn that sets
   the condition codes).  Set by arc_final_prescan_insn.  Used by
   arc_print_operand.  */
static int last_insn_set_cc_p;
static int current_insn_set_cc_p;
static bool arc_handle_option (size_t, const char *, int);
static void record_cc_ref (rtx);
static void arc_init_reg_tables (void);
static int get_arc_condition_code (rtx);
const struct attribute_spec arc_attribute_table[];
static tree arc_handle_interrupt_attribute (tree *, tree, tree, int, bool *);
static bool arc_assemble_integer (rtx, unsigned int, int);
static void arc_output_function_prologue (FILE *, HOST_WIDE_INT);
static void arc_output_function_epilogue (FILE *, HOST_WIDE_INT);
static void arc_file_start (void);
static void arc_internal_label (FILE *, const char *, unsigned long);
static void arc_setup_incoming_varargs (CUMULATIVE_ARGS *, enum machine_mode,
					tree, int *, int);
static bool arc_rtx_costs (rtx, int, int, int *);
static int arc_address_cost (rtx);
static void arc_external_libcall (rtx);
static bool arc_return_in_memory (tree, tree);
static bool arc_pass_by_reference (CUMULATIVE_ARGS *, enum machine_mode,
				   tree, bool);

/* Initialize the GCC target structure.  */
#undef TARGET_ASM_ALIGNED_HI_OP
#define TARGET_ASM_ALIGNED_HI_OP "\t.hword\t"
#undef TARGET_ASM_ALIGNED_SI_OP
#define TARGET_ASM_ALIGNED_SI_OP "\t.word\t"
#undef TARGET_ASM_INTEGER
#define TARGET_ASM_INTEGER arc_assemble_integer

#undef TARGET_ASM_FUNCTION_PROLOGUE
#define TARGET_ASM_FUNCTION_PROLOGUE arc_output_function_prologue
#undef TARGET_ASM_FUNCTION_EPILOGUE
#define TARGET_ASM_FUNCTION_EPILOGUE arc_output_function_epilogue
#undef TARGET_ASM_FILE_START
#define TARGET_ASM_FILE_START arc_file_start
#undef TARGET_ATTRIBUTE_TABLE
#define TARGET_ATTRIBUTE_TABLE arc_attribute_table
#undef TARGET_ASM_INTERNAL_LABEL
#define TARGET_ASM_INTERNAL_LABEL arc_internal_label
#undef TARGET_ASM_EXTERNAL_LIBCALL
#define TARGET_ASM_EXTERNAL_LIBCALL arc_external_libcall

#undef TARGET_HANDLE_OPTION
#define TARGET_HANDLE_OPTION arc_handle_option

#undef TARGET_RTX_COSTS
#define TARGET_RTX_COSTS arc_rtx_costs
#undef TARGET_ADDRESS_COST
#define TARGET_ADDRESS_COST arc_address_cost

#undef TARGET_PROMOTE_FUNCTION_ARGS
#define TARGET_PROMOTE_FUNCTION_ARGS hook_bool_tree_true
#undef TARGET_PROMOTE_FUNCTION_RETURN
#define TARGET_PROMOTE_FUNCTION_RETURN hook_bool_tree_true
#undef TARGET_PROMOTE_PROTOTYPES
#define TARGET_PROMOTE_PROTOTYPES hook_bool_tree_true

#undef TARGET_RETURN_IN_MEMORY
#define TARGET_RETURN_IN_MEMORY arc_return_in_memory
#undef TARGET_PASS_BY_REFERENCE
#define TARGET_PASS_BY_REFERENCE arc_pass_by_reference
#undef TARGET_CALLEE_COPIES
#define TARGET_CALLEE_COPIES hook_bool_CUMULATIVE_ARGS_mode_tree_bool_true

#undef TARGET_SETUP_INCOMING_VARARGS
#define TARGET_SETUP_INCOMING_VARARGS arc_setup_incoming_varargs

struct gcc_target targetm = TARGET_INITIALIZER;

/* Implement TARGET_HANDLE_OPTION.  */

static bool
arc_handle_option (size_t code, const char *arg, int value ATTRIBUTE_UNUSED)
{
  switch (code)
    {
    case OPT_mcpu_:
      return strcmp (arg, "base") == 0 || ARC_EXTENSION_CPU (arg);

    default:
      return true;
    }
}

/* Called by OVERRIDE_OPTIONS to initialize various things.  */

void
arc_init (void)
{
  char *tmp;
  
  /* Set the pseudo-ops for the various standard sections.  */
  arc_text_section = tmp = xmalloc (strlen (arc_text_string) + sizeof (ARC_SECTION_FORMAT) + 1);
  sprintf (tmp, ARC_SECTION_FORMAT, arc_text_string);
  arc_data_section = tmp = xmalloc (strlen (arc_data_string) + sizeof (ARC_SECTION_FORMAT) + 1);
  sprintf (tmp, ARC_SECTION_FORMAT, arc_data_string);
  arc_rodata_section = tmp = xmalloc (strlen (arc_rodata_string) + sizeof (ARC_SECTION_FORMAT) + 1);
  sprintf (tmp, ARC_SECTION_FORMAT, arc_rodata_string);

  arc_init_reg_tables ();

  /* Initialize array for PRINT_OPERAND_PUNCT_VALID_P.  */
  memset (arc_punct_chars, 0, sizeof (arc_punct_chars));
  arc_punct_chars['#'] = 1;
  arc_punct_chars['*'] = 1;
  arc_punct_chars['?'] = 1;
  arc_punct_chars['!'] = 1;
  arc_punct_chars['~'] = 1;
}

/* The condition codes of the ARC, and the inverse function.  */
static const char *const arc_condition_codes[] =
{
  "al", 0, "eq", "ne", "p", "n", "c", "nc", "v", "nv",
  "gt", "le", "ge", "lt", "hi", "ls", "pnz", 0
};

#define ARC_INVERSE_CONDITION_CODE(X)  ((X) ^ 1)

/* Returns the index of the ARC condition code string in
   `arc_condition_codes'.  COMPARISON should be an rtx like
   `(eq (...) (...))'.  */

static int
get_arc_condition_code (rtx comparison)
{
  switch (GET_CODE (comparison))
    {
    case EQ : return 2;
    case NE : return 3;
    case GT : return 10;
    case LE : return 11;
    case GE : return 12;
    case LT : return 13;
    case GTU : return 14;
    case LEU : return 15;
    case LTU : return 6;
    case GEU : return 7;
    default : gcc_unreachable ();
    }
  /*NOTREACHED*/
  return (42);
}

/* Given a comparison code (EQ, NE, etc.) and the first operand of a COMPARE,
   return the mode to be used for the comparison.  */

enum machine_mode
arc_select_cc_mode (enum rtx_code op,
	            rtx x ATTRIBUTE_UNUSED,
                    rtx y ATTRIBUTE_UNUSED)
{
  switch (op)
    {
    case EQ :
    case NE :
      return CCZNmode;
    default :
      switch (GET_CODE (x))
	{
	case AND :
	case IOR :
	case XOR :
	case SIGN_EXTEND :
	case ZERO_EXTEND :
	  return CCZNmode;
	case ASHIFT :
	case ASHIFTRT :
	case LSHIFTRT :
	  return CCZNCmode;
	default:
	  break;
	}
    }
  return CCmode;
}

/* Vectors to keep interesting information about registers where it can easily
   be got.  We use to use the actual mode value as the bit number, but there
   is (or may be) more than 32 modes now.  Instead we use two tables: one
   indexed by hard register number, and one indexed by mode.  */

/* The purpose of arc_mode_class is to shrink the range of modes so that
   they all fit (as bit numbers) in a 32 bit word (again).  Each real mode is
   mapped into one arc_mode_class mode.  */

enum arc_mode_class {
  C_MODE,
  S_MODE, D_MODE, T_MODE, O_MODE,
  SF_MODE, DF_MODE, TF_MODE, OF_MODE
};

/* Modes for condition codes.  */
#define C_MODES (1 << (int) C_MODE)

/* Modes for single-word and smaller quantities.  */
#define S_MODES ((1 << (int) S_MODE) | (1 << (int) SF_MODE))

/* Modes for double-word and smaller quantities.  */
#define D_MODES (S_MODES | (1 << (int) D_MODE) | (1 << DF_MODE))

/* Modes for quad-word and smaller quantities.  */
#define T_MODES (D_MODES | (1 << (int) T_MODE) | (1 << (int) TF_MODE))

/* Value is 1 if register/mode pair is acceptable on arc.  */

const unsigned int arc_hard_regno_mode_ok[] = {
  T_MODES, T_MODES, T_MODES, T_MODES, T_MODES, T_MODES, T_MODES, T_MODES,
  T_MODES, T_MODES, T_MODES, T_MODES, T_MODES, T_MODES, T_MODES, T_MODES,
  T_MODES, T_MODES, T_MODES, T_MODES, T_MODES, T_MODES, T_MODES, D_MODES,
  D_MODES, S_MODES, S_MODES, S_MODES, S_MODES, S_MODES, S_MODES, S_MODES,

  /* ??? Leave these as S_MODES for now.  */
  S_MODES, S_MODES, S_MODES, S_MODES, S_MODES, S_MODES, S_MODES, S_MODES,
  S_MODES, S_MODES, S_MODES, S_MODES, S_MODES, S_MODES, S_MODES, S_MODES,
  S_MODES, S_MODES, S_MODES, S_MODES, S_MODES, S_MODES, S_MODES, S_MODES,
  S_MODES, S_MODES, S_MODES, S_MODES, S_MODES, C_MODES
};

unsigned int arc_mode_class [NUM_MACHINE_MODES];

enum reg_class arc_regno_reg_class[FIRST_PSEUDO_REGISTER];

static void
arc_init_reg_tables (void)
{
  int i;

  for (i = 0; i < NUM_MACHINE_MODES; i++)
    {
      switch (GET_MODE_CLASS (i))
	{
	case MODE_INT:
	case MODE_PARTIAL_INT:
	case MODE_COMPLEX_INT:
	  if (GET_MODE_SIZE (i) <= 4)
	    arc_mode_class[i] = 1 << (int) S_MODE;
	  else if (GET_MODE_SIZE (i) == 8)
	    arc_mode_class[i] = 1 << (int) D_MODE;
	  else if (GET_MODE_SIZE (i) == 16)
	    arc_mode_class[i] = 1 << (int) T_MODE;
	  else if (GET_MODE_SIZE (i) == 32)
	    arc_mode_class[i] = 1 << (int) O_MODE;
	  else 
	    arc_mode_class[i] = 0;
	  break;
	case MODE_FLOAT:
	case MODE_COMPLEX_FLOAT:
	  if (GET_MODE_SIZE (i) <= 4)
	    arc_mode_class[i] = 1 << (int) SF_MODE;
	  else if (GET_MODE_SIZE (i) == 8)
	    arc_mode_class[i] = 1 << (int) DF_MODE;
	  else if (GET_MODE_SIZE (i) == 16)
	    arc_mode_class[i] = 1 << (int) TF_MODE;
	  else if (GET_MODE_SIZE (i) == 32)
	    arc_mode_class[i] = 1 << (int) OF_MODE;
	  else 
	    arc_mode_class[i] = 0;
	  break;
	case MODE_CC:
	  arc_mode_class[i] = 1 << (int) C_MODE;
	  break;
	default:
	  arc_mode_class[i] = 0;
	  break;
	}
    }

  for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
    {
      if (i < 60)
	arc_regno_reg_class[i] = GENERAL_REGS;
      else if (i == 60)
	arc_regno_reg_class[i] = LPCOUNT_REG;
      else if (i == 61)
	arc_regno_reg_class[i] = NO_REGS /* CC_REG: must be NO_REGS */;
      else
	arc_regno_reg_class[i] = NO_REGS;
    }
}

/* ARC specific attribute support.

   The ARC has these attributes:
   interrupt - for interrupt functions
*/

const struct attribute_spec arc_attribute_table[] =
{
  /* { name, min_len, max_len, decl_req, type_req, fn_type_req, handler } */
  { "interrupt", 1, 1, true,  false, false, arc_handle_interrupt_attribute },
  { NULL,        0, 0, false, false, false, NULL }
};

/* Handle an "interrupt" attribute; arguments as in
   struct attribute_spec.handler.  */
static tree
arc_handle_interrupt_attribute (tree *node ATTRIBUTE_UNUSED,
                                tree name,
                                tree args,
                                int flags ATTRIBUTE_UNUSED,
                                bool *no_add_attrs)
{
  tree value = TREE_VALUE (args);

  if (TREE_CODE (value) != STRING_CST)
    {
      warning (OPT_Wattributes,
	       "argument of %qs attribute is not a string constant",
	       IDENTIFIER_POINTER (name));
      *no_add_attrs = true;
    }
  else if (strcmp (TREE_STRING_POINTER (value), "ilink1")
	   && strcmp (TREE_STRING_POINTER (value), "ilink2"))
    {
      warning (OPT_Wattributes,
	       "argument of %qs attribute is not \"ilink1\" or \"ilink2\"",
	       IDENTIFIER_POINTER (name));
      *no_add_attrs = true;
    }

  return NULL_TREE;
}


/* Acceptable arguments to the call insn.  */

int
call_address_operand (rtx op, enum machine_mode mode)
{
  return (symbolic_operand (op, mode)
	  || (GET_CODE (op) == CONST_INT && LEGITIMATE_CONSTANT_P (op))
	  || (GET_CODE (op) == REG));
}

int
call_operand (rtx op, enum machine_mode mode)
{
  if (GET_CODE (op) != MEM)
    return 0;
  op = XEXP (op, 0);
  return call_address_operand (op, mode);
}

/* Returns 1 if OP is a symbol reference.  */

int
symbolic_operand (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  switch (GET_CODE (op))
    {
    case SYMBOL_REF:
    case LABEL_REF:
    case CONST :
      return 1;
    default:
      return 0;
    }
}

/* Return truth value of statement that OP is a symbolic memory
   operand of mode MODE.  */

int
symbolic_memory_operand (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  if (GET_CODE (op) == SUBREG)
    op = SUBREG_REG (op);
  if (GET_CODE (op) != MEM)
    return 0;
  op = XEXP (op, 0);
  return (GET_CODE (op) == SYMBOL_REF || GET_CODE (op) == CONST
	  || GET_CODE (op) == LABEL_REF);
}

/* Return true if OP is a short immediate (shimm) value.  */

int
short_immediate_operand (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  if (GET_CODE (op) != CONST_INT)
    return 0;
  return SMALL_INT (INTVAL (op));
}

/* Return true if OP will require a long immediate (limm) value.
   This is currently only used when calculating length attributes.  */

int
long_immediate_operand (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  switch (GET_CODE (op))
    {
    case SYMBOL_REF :
    case LABEL_REF :
    case CONST :
      return 1;
    case CONST_INT :
      return !SMALL_INT (INTVAL (op));
    case CONST_DOUBLE :
      /* These can happen because large unsigned 32 bit constants are
	 represented this way (the multiplication patterns can cause these
	 to be generated).  They also occur for SFmode values.  */
      return 1;
    default:
      break;
    }
  return 0;
}

/* Return true if OP is a MEM that when used as a load or store address will
   require an 8 byte insn.
   Load and store instructions don't allow the same possibilities but they're
   similar enough that this one function will do.
   This is currently only used when calculating length attributes.  */

int
long_immediate_loadstore_operand (rtx op,
                                  enum machine_mode mode ATTRIBUTE_UNUSED)
{
  if (GET_CODE (op) != MEM)
    return 0;

  op = XEXP (op, 0);
  switch (GET_CODE (op))
    {
    case SYMBOL_REF :
    case LABEL_REF :
    case CONST :
      return 1;
    case CONST_INT :
      /* This must be handled as "st c,[limm]".  Ditto for load.
	 Technically, the assembler could translate some possibilities to
	 "st c,[limm/2 + limm/2]" if limm/2 will fit in a shimm, but we don't
	 assume that it does.  */
      return 1;
    case CONST_DOUBLE :
      /* These can happen because large unsigned 32 bit constants are
	 represented this way (the multiplication patterns can cause these
	 to be generated).  They also occur for SFmode values.  */
      return 1;
    case REG :
      return 0;
    case PLUS :
      if (GET_CODE (XEXP (op, 1)) == CONST_INT
	  && !SMALL_INT (INTVAL (XEXP (op, 1))))
	return 1;
      return 0;
    default:
      break;
    }
  return 0;
}

/* Return true if OP is an acceptable argument for a single word
   move source.  */

int
move_src_operand (rtx op, enum machine_mode mode)
{
  switch (GET_CODE (op))
    {
    case SYMBOL_REF :
    case LABEL_REF :
    case CONST :
      return 1;
    case CONST_INT :
      return (LARGE_INT (INTVAL (op)));
    case CONST_DOUBLE :
      /* We can handle DImode integer constants in SImode if the value
	 (signed or unsigned) will fit in 32 bits.  This is needed because
	 large unsigned 32 bit constants are represented as CONST_DOUBLEs.  */
      if (mode == SImode)
	return arc_double_limm_p (op);
      /* We can handle 32 bit floating point constants.  */
      if (mode == SFmode)
	return GET_MODE (op) == SFmode;
      return 0;
    case REG :
      return register_operand (op, mode);
    case SUBREG :
      /* (subreg (mem ...) ...) can occur here if the inner part was once a
	 pseudo-reg and is now a stack slot.  */
      if (GET_CODE (SUBREG_REG (op)) == MEM)
	return address_operand (XEXP (SUBREG_REG (op), 0), mode);
      else
	return register_operand (op, mode);
    case MEM :
      return address_operand (XEXP (op, 0), mode);
    default :
      return 0;
    }
}

/* Return true if OP is an acceptable argument for a double word
   move source.  */

int
move_double_src_operand (rtx op, enum machine_mode mode)
{
  switch (GET_CODE (op))
    {
    case REG :
      return register_operand (op, mode);
    case SUBREG :
      /* (subreg (mem ...) ...) can occur here if the inner part was once a
	 pseudo-reg and is now a stack slot.  */
      if (GET_CODE (SUBREG_REG (op)) == MEM)
	return move_double_src_operand (SUBREG_REG (op), mode);
      else
	return register_operand (op, mode);
    case MEM :
      /* Disallow auto inc/dec for now.  */
      if (GET_CODE (XEXP (op, 0)) == PRE_DEC
	  || GET_CODE (XEXP (op, 0)) == PRE_INC)
	return 0;
      return address_operand (XEXP (op, 0), mode);
    case CONST_INT :
    case CONST_DOUBLE :
      return 1;
    default :
      return 0;
    }
}

/* Return true if OP is an acceptable argument for a move destination.  */

int
move_dest_operand (rtx op, enum machine_mode mode)
{
  switch (GET_CODE (op))
    {
    case REG :
      return register_operand (op, mode);
    case SUBREG :
      /* (subreg (mem ...) ...) can occur here if the inner part was once a
	 pseudo-reg and is now a stack slot.  */
      if (GET_CODE (SUBREG_REG (op)) == MEM)
	return address_operand (XEXP (SUBREG_REG (op), 0), mode);
      else
	return register_operand (op, mode);
    case MEM :
      return address_operand (XEXP (op, 0), mode);
    default :
      return 0;
    }
}

/* Return true if OP is valid load with update operand.  */

int
load_update_operand (rtx op, enum machine_mode mode)
{
  if (GET_CODE (op) != MEM
      || GET_MODE (op) != mode)
    return 0;
  op = XEXP (op, 0);
  if (GET_CODE (op) != PLUS
      || GET_MODE (op) != Pmode
      || !register_operand (XEXP (op, 0), Pmode)
      || !nonmemory_operand (XEXP (op, 1), Pmode))
    return 0;
  return 1;
}

/* Return true if OP is valid store with update operand.  */

int
store_update_operand (rtx op, enum machine_mode mode)
{
  if (GET_CODE (op) != MEM
      || GET_MODE (op) != mode)
    return 0;
  op = XEXP (op, 0);
  if (GET_CODE (op) != PLUS
      || GET_MODE (op) != Pmode
      || !register_operand (XEXP (op, 0), Pmode)
      || !(GET_CODE (XEXP (op, 1)) == CONST_INT
	   && SMALL_INT (INTVAL (XEXP (op, 1)))))
    return 0;
  return 1;
}

/* Return true if OP is a non-volatile non-immediate operand.
   Volatile memory refs require a special "cache-bypass" instruction
   and only the standard movXX patterns are set up to handle them.  */

int
nonvol_nonimm_operand (rtx op, enum machine_mode mode)
{
  if (GET_CODE (op) == MEM && MEM_VOLATILE_P (op))
    return 0;
  return nonimmediate_operand (op, mode);
}

/* Accept integer operands in the range -0x80000000..0x7fffffff.  We have
   to check the range carefully since this predicate is used in DImode
   contexts.  */

int
const_sint32_operand (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  /* All allowed constants will fit a CONST_INT.  */
  return (GET_CODE (op) == CONST_INT
	  && (INTVAL (op) >= (-0x7fffffff - 1) && INTVAL (op) <= 0x7fffffff));
}

/* Accept integer operands in the range 0..0xffffffff.  We have to check the
   range carefully since this predicate is used in DImode contexts.  Also, we
   need some extra crud to make it work when hosted on 64-bit machines.  */

int
const_uint32_operand (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
#if HOST_BITS_PER_WIDE_INT > 32
  /* All allowed constants will fit a CONST_INT.  */
  return (GET_CODE (op) == CONST_INT
	  && (INTVAL (op) >= 0 && INTVAL (op) <= 0xffffffffL));
#else
  return ((GET_CODE (op) == CONST_INT && INTVAL (op) >= 0)
	  || (GET_CODE (op) == CONST_DOUBLE && CONST_DOUBLE_HIGH (op) == 0));
#endif
}

/* Return 1 if OP is a comparison operator valid for the mode of CC.
   This allows the use of MATCH_OPERATOR to recognize all the branch insns.

   Some insns only set a few bits in the condition code.  So only allow those
   comparisons that use the bits that are valid.  */

int
proper_comparison_operator (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  enum rtx_code code;
  if (!COMPARISON_P (op))
    return 0;

  code = GET_CODE (op);
  if (GET_MODE (XEXP (op, 0)) == CCZNmode)
    return (code == EQ || code == NE);
  if (GET_MODE (XEXP (op, 0)) == CCZNCmode)
    return (code == EQ || code == NE
	    || code == LTU || code == GEU || code == GTU || code == LEU);
  return 1;
}

/* Misc. utilities.  */

/* X and Y are two things to compare using CODE.  Emit the compare insn and
   return the rtx for the cc reg in the proper mode.  */

rtx
gen_compare_reg (enum rtx_code code, rtx x, rtx y)
{
  enum machine_mode mode = SELECT_CC_MODE (code, x, y);
  rtx cc_reg;

  cc_reg = gen_rtx_REG (mode, 61);

  emit_insn (gen_rtx_SET (VOIDmode, cc_reg,
			  gen_rtx_COMPARE (mode, x, y)));

  return cc_reg;
}

/* Return 1 if VALUE, a const_double, will fit in a limm (4 byte number).
   We assume the value can be either signed or unsigned.  */

int
arc_double_limm_p (rtx value)
{
  HOST_WIDE_INT low, high;

  gcc_assert (GET_CODE (value) == CONST_DOUBLE);

  low = CONST_DOUBLE_LOW (value);
  high = CONST_DOUBLE_HIGH (value);

  if (low & 0x80000000)
    {
      return (((unsigned HOST_WIDE_INT) low <= 0xffffffff && high == 0)
	      || (((low & - (unsigned HOST_WIDE_INT) 0x80000000)
		   == - (unsigned HOST_WIDE_INT) 0x80000000)
		  && high == -1));
    }
  else
    {
      return (unsigned HOST_WIDE_INT) low <= 0x7fffffff && high == 0;
    }
}

/* Do any needed setup for a variadic function.  For the ARC, we must
   create a register parameter block, and then copy any anonymous arguments
   in registers to memory.

   CUM has not been updated for the last named argument which has type TYPE
   and mode MODE, and we rely on this fact.

   We do things a little weird here.  We're supposed to only allocate space
   for the anonymous arguments.  However we need to keep the stack eight byte
   aligned.  So we round the space up if necessary, and leave it to va_start
   to compensate.  */

static void
arc_setup_incoming_varargs (CUMULATIVE_ARGS *cum,
                            enum machine_mode mode,
                            tree type ATTRIBUTE_UNUSED,
                            int *pretend_size,
                            int no_rtl)
{
  int first_anon_arg;

  /* All BLKmode values are passed by reference.  */
  gcc_assert (mode != BLKmode);

  first_anon_arg = *cum + ((GET_MODE_SIZE (mode) + UNITS_PER_WORD - 1)
			   / UNITS_PER_WORD);

  if (first_anon_arg < MAX_ARC_PARM_REGS && !no_rtl)
    {
      /* Note that first_reg_offset < MAX_ARC_PARM_REGS.  */
      int first_reg_offset = first_anon_arg;
      /* Size in words to "pretend" allocate.  */
      int size = MAX_ARC_PARM_REGS - first_reg_offset;
      /* Extra slop to keep stack eight byte aligned.  */
      int align_slop = size & 1;
      rtx regblock;

      regblock = gen_rtx_MEM (BLKmode,
			      plus_constant (arg_pointer_rtx,
					     FIRST_PARM_OFFSET (0)
					     + align_slop * UNITS_PER_WORD));
      set_mem_alias_set (regblock, get_varargs_alias_set ());
      set_mem_align (regblock, BITS_PER_WORD);
      move_block_from_reg (first_reg_offset, regblock,
			   MAX_ARC_PARM_REGS - first_reg_offset);

      *pretend_size = ((MAX_ARC_PARM_REGS - first_reg_offset + align_slop)
		       * UNITS_PER_WORD);
    }
}

/* Cost functions.  */

/* Compute a (partial) cost for rtx X.  Return true if the complete
   cost has been computed, and false if subexpressions should be
   scanned.  In either case, *TOTAL contains the cost result.  */

static bool
arc_rtx_costs (rtx x, int code, int outer_code ATTRIBUTE_UNUSED, int *total)
{
  switch (code)
    {
      /* Small integers are as cheap as registers.  4 byte values can
	 be fetched as immediate constants - let's give that the cost
	 of an extra insn.  */
    case CONST_INT:
      if (SMALL_INT (INTVAL (x)))
	{
	  *total = 0;
	  return true;
	}
      /* FALLTHRU */

    case CONST:
    case LABEL_REF:
    case SYMBOL_REF:
      *total = COSTS_N_INSNS (1);
      return true;

    case CONST_DOUBLE:
      {
        rtx high, low;
        split_double (x, &high, &low);
	*total = COSTS_N_INSNS (!SMALL_INT (INTVAL (high))
				+ !SMALL_INT (INTVAL (low)));
	return true;
      }

    /* Encourage synth_mult to find a synthetic multiply when reasonable.
       If we need more than 12 insns to do a multiply, then go out-of-line,
       since the call overhead will be < 10% of the cost of the multiply.  */
    case ASHIFT:
    case ASHIFTRT:
    case LSHIFTRT:
      if (TARGET_SHIFTER)
        *total = COSTS_N_INSNS (1);
      else if (GET_CODE (XEXP (x, 1)) != CONST_INT)
        *total = COSTS_N_INSNS (16);
      else
        *total = COSTS_N_INSNS (INTVAL (XEXP ((x), 1)));
      return false;

    default:
      return false;
    }
}


/* Provide the costs of an addressing mode that contains ADDR.
   If ADDR is not a valid address, its cost is irrelevant.  */

static int
arc_address_cost (rtx addr)
{
  switch (GET_CODE (addr))
    {
    case REG :
      return 1;

    case LABEL_REF :
    case SYMBOL_REF :
    case CONST :
      return 2;

    case PLUS :
      {
	register rtx plus0 = XEXP (addr, 0);
	register rtx plus1 = XEXP (addr, 1);

	if (GET_CODE (plus0) != REG)
	  break;

	switch (GET_CODE (plus1))
	  {
	  case CONST_INT :
	    return SMALL_INT (plus1) ? 1 : 2;
	  case CONST :
	  case SYMBOL_REF :
	  case LABEL_REF :
	    return 2;
	  default:
	    break;
	  }
	break;
      }
    default:
      break;
    }

  return 4;
}

/* Function prologue/epilogue handlers.  */

/* ARC stack frames look like:

             Before call                       After call
        +-----------------------+       +-----------------------+
        |                       |       |                       |
   high |  local variables,     |       |  local variables,     |
   mem  |  reg save area, etc.  |       |  reg save area, etc.  |
        |                       |       |                       |
        +-----------------------+       +-----------------------+
        |                       |       |                       |
        |  arguments on stack.  |       |  arguments on stack.  |
        |                       |       |                       |
 SP+16->+-----------------------+FP+48->+-----------------------+
        | 4 word save area for  |       |  reg parm save area,  |
        | return addr, prev %fp |       |  only created for     |    
  SP+0->+-----------------------+       |  variable argument    |    
                                        |  functions            |    
                                 FP+16->+-----------------------+    
                                        | 4 word save area for  |    
                                        | return addr, prev %fp |    
                                  FP+0->+-----------------------+    
                                        |                       |    
                                        |  local variables      |    
                                        |                       |    
                                        +-----------------------+    
                                        |                       |    
                                        |  register save area   |    
                                        |                       |    
                                        +-----------------------+    
                                        |                       |    
                                        |  alloca allocations   |    
                                        |                       |    
                                        +-----------------------+    
                                        |                       |    
                                        |  arguments on stack   |    
                                        |                       |    
                                 SP+16->+-----------------------+
   low                                  | 4 word save area for  |    
   memory                               | return addr, prev %fp |    
                                  SP+0->+-----------------------+    

Notes:
1) The "reg parm save area" does not exist for non variable argument fns.
   The "reg parm save area" can be eliminated completely if we created our
   own va-arc.h, but that has tradeoffs as well (so it's not done).  */

/* Structure to be filled in by arc_compute_frame_size with register
   save masks, and offsets for the current function.  */
struct arc_frame_info
{
  unsigned int total_size;	/* # bytes that the entire frame takes up.  */
  unsigned int extra_size;	/* # bytes of extra stuff.  */
  unsigned int pretend_size;	/* # bytes we push and pretend caller did.  */
  unsigned int args_size;	/* # bytes that outgoing arguments take up.  */
  unsigned int reg_size;	/* # bytes needed to store regs.  */
  unsigned int var_size;	/* # bytes that variables take up.  */
  unsigned int reg_offset;	/* Offset from new sp to store regs.  */
  unsigned int gmask;		/* Mask of saved gp registers.  */
  int          initialized;	/* Nonzero if frame size already calculated.  */
};

/* Current frame information calculated by arc_compute_frame_size.  */
static struct arc_frame_info current_frame_info;

/* Zero structure to initialize current_frame_info.  */
static struct arc_frame_info zero_frame_info;

/* Type of function DECL.

   The result is cached.  To reset the cache at the end of a function,
   call with DECL = NULL_TREE.  */

enum arc_function_type
arc_compute_function_type (tree decl)
{
  tree a;
  /* Cached value.  */
  static enum arc_function_type fn_type = ARC_FUNCTION_UNKNOWN;
  /* Last function we were called for.  */
  static tree last_fn = NULL_TREE;

  /* Resetting the cached value?  */
  if (decl == NULL_TREE)
    {
      fn_type = ARC_FUNCTION_UNKNOWN;
      last_fn = NULL_TREE;
      return fn_type;
    }

  if (decl == last_fn && fn_type != ARC_FUNCTION_UNKNOWN)
    return fn_type;

  /* Assume we have a normal function (not an interrupt handler).  */
  fn_type = ARC_FUNCTION_NORMAL;

  /* Now see if this is an interrupt handler.  */
  for (a = DECL_ATTRIBUTES (current_function_decl);
       a;
       a = TREE_CHAIN (a))
    {
      tree name = TREE_PURPOSE (a), args = TREE_VALUE (a);

      if (name == get_identifier ("__interrupt__")
	  && list_length (args) == 1
	  && TREE_CODE (TREE_VALUE (args)) == STRING_CST)
	{
	  tree value = TREE_VALUE (args);

	  if (!strcmp (TREE_STRING_POINTER (value), "ilink1"))
	    fn_type = ARC_FUNCTION_ILINK1;
	  else if (!strcmp (TREE_STRING_POINTER (value), "ilink2"))
	    fn_type = ARC_FUNCTION_ILINK2;
	  else
	    gcc_unreachable ();
	  break;
	}
    }

  last_fn = decl;
  return fn_type;
}

#define ILINK1_REGNUM 29
#define ILINK2_REGNUM 30
#define RETURN_ADDR_REGNUM 31
#define FRAME_POINTER_MASK (1 << (FRAME_POINTER_REGNUM))
#define RETURN_ADDR_MASK (1 << (RETURN_ADDR_REGNUM))

/* Tell prologue and epilogue if register REGNO should be saved / restored.
   The return address and frame pointer are treated separately.
   Don't consider them here.  */
#define MUST_SAVE_REGISTER(regno, interrupt_p) \
((regno) != RETURN_ADDR_REGNUM && (regno) != FRAME_POINTER_REGNUM \
 && (regs_ever_live[regno] && (!call_used_regs[regno] || interrupt_p)))

#define MUST_SAVE_RETURN_ADDR (regs_ever_live[RETURN_ADDR_REGNUM])

/* Return the bytes needed to compute the frame pointer from the current
   stack pointer.

   SIZE is the size needed for local variables.  */

unsigned int
arc_compute_frame_size (int size /* # of var. bytes allocated.  */)
{
  int regno;
  unsigned int total_size, var_size, args_size, pretend_size, extra_size;
  unsigned int reg_size, reg_offset;
  unsigned int gmask;
  enum arc_function_type fn_type;
  int interrupt_p;

  var_size	= size;
  args_size	= current_function_outgoing_args_size;
  pretend_size	= current_function_pretend_args_size;
  extra_size	= FIRST_PARM_OFFSET (0);
  total_size	= extra_size + pretend_size + args_size + var_size;
  reg_offset	= FIRST_PARM_OFFSET(0) + current_function_outgoing_args_size;
  reg_size	= 0;
  gmask		= 0;

  /* See if this is an interrupt handler.  Call used registers must be saved
     for them too.  */
  fn_type = arc_compute_function_type (current_function_decl);
  interrupt_p = ARC_INTERRUPT_P (fn_type);

  /* Calculate space needed for registers.
     ??? We ignore the extension registers for now.  */

  for (regno = 0; regno <= 31; regno++)
    {
      if (MUST_SAVE_REGISTER (regno, interrupt_p))
	{
	  reg_size += UNITS_PER_WORD;
	  gmask |= 1 << regno;
	}
    }

  total_size += reg_size;

  /* If the only space to allocate is the fp/blink save area this is an
     empty frame.  However, if we'll be making a function call we need to
     allocate a stack frame for our callee's fp/blink save area.  */
  if (total_size == extra_size
      && !MUST_SAVE_RETURN_ADDR)
    total_size = extra_size = 0;

  total_size = ARC_STACK_ALIGN (total_size);

  /* Save computed information.  */
  current_frame_info.total_size   = total_size;
  current_frame_info.extra_size   = extra_size;
  current_frame_info.pretend_size = pretend_size;
  current_frame_info.var_size     = var_size;
  current_frame_info.args_size    = args_size;
  current_frame_info.reg_size	  = reg_size;
  current_frame_info.reg_offset	  = reg_offset;
  current_frame_info.gmask	  = gmask;
  current_frame_info.initialized  = reload_completed;

  /* Ok, we're done.  */
  return total_size;
}

/* Common code to save/restore registers.  */

void
arc_save_restore (FILE *file,
                  const char *base_reg,
                  unsigned int offset,
                  unsigned int gmask,
                  const char *op)
{
  int regno;

  if (gmask == 0)
    return;

  for (regno = 0; regno <= 31; regno++)
    {
      if ((gmask & (1L << regno)) != 0)
	{
	  fprintf (file, "\t%s %s,[%s,%d]\n",
		     op, reg_names[regno], base_reg, offset);
	  offset += UNITS_PER_WORD;
	}
    }
}

/* Target hook to assemble an integer object.  The ARC version needs to
   emit a special directive for references to labels and function
   symbols.  */

static bool
arc_assemble_integer (rtx x, unsigned int size, int aligned_p)
{
  if (size == UNITS_PER_WORD && aligned_p
      && ((GET_CODE (x) == SYMBOL_REF && SYMBOL_REF_FUNCTION_P (x))
	  || GET_CODE (x) == LABEL_REF))
    {
      fputs ("\t.word\t%st(", asm_out_file);
      output_addr_const (asm_out_file, x);
      fputs (")\n", asm_out_file);
      return true;
    }
  return default_assemble_integer (x, size, aligned_p);
}

/* Set up the stack and frame pointer (if desired) for the function.  */

static void
arc_output_function_prologue (FILE *file, HOST_WIDE_INT size)
{
  const char *sp_str = reg_names[STACK_POINTER_REGNUM];
  const char *fp_str = reg_names[FRAME_POINTER_REGNUM];
  unsigned int gmask = current_frame_info.gmask;
  enum arc_function_type fn_type = arc_compute_function_type (current_function_decl);

  /* If this is an interrupt handler, set up our stack frame.
     ??? Optimize later.  */
  if (ARC_INTERRUPT_P (fn_type))
    {
      fprintf (file, "\t%s interrupt handler\n",
	       ASM_COMMENT_START);
      fprintf (file, "\tsub %s,%s,16\n", sp_str, sp_str);
    }

  /* This is only for the human reader.  */
  fprintf (file, "\t%s BEGIN PROLOGUE %s vars= %d, regs= %d, args= %d, extra= %d\n",
	   ASM_COMMENT_START, ASM_COMMENT_START,
	   current_frame_info.var_size,
	   current_frame_info.reg_size / 4,
	   current_frame_info.args_size,
	   current_frame_info.extra_size);

  size = ARC_STACK_ALIGN (size);
  size = (! current_frame_info.initialized
	   ? arc_compute_frame_size (size)
	   : current_frame_info.total_size);

  /* These cases shouldn't happen.  Catch them now.  */
  gcc_assert (size || !gmask);

  /* Allocate space for register arguments if this is a variadic function.  */
  if (current_frame_info.pretend_size != 0)
    fprintf (file, "\tsub %s,%s,%d\n",
	     sp_str, sp_str, current_frame_info.pretend_size);

  /* The home-grown ABI says link register is saved first.  */
  if (MUST_SAVE_RETURN_ADDR)
    fprintf (file, "\tst %s,[%s,%d]\n",
	     reg_names[RETURN_ADDR_REGNUM], sp_str, UNITS_PER_WORD);

  /* Set up the previous frame pointer next (if we need to).  */
  if (frame_pointer_needed)
    {
      fprintf (file, "\tst %s,[%s]\n", fp_str, sp_str);
      fprintf (file, "\tmov %s,%s\n", fp_str, sp_str);
    }

  /* ??? We don't handle the case where the saved regs are more than 252
     bytes away from sp.  This can be handled by decrementing sp once, saving
     the regs, and then decrementing it again.  The epilogue doesn't have this
     problem as the `ld' insn takes reg+limm values (though it would be more
     efficient to avoid reg+limm).  */

  /* Allocate the stack frame.  */
  if (size - current_frame_info.pretend_size > 0)
    fprintf (file, "\tsub %s,%s," HOST_WIDE_INT_PRINT_DEC "\n",
	     sp_str, sp_str, size - current_frame_info.pretend_size);

  /* Save any needed call-saved regs (and call-used if this is an
     interrupt handler).  */
  arc_save_restore (file, sp_str, current_frame_info.reg_offset,
		    /* The zeroing of these two bits is unnecessary,
		       but leave this in for clarity.  */
		    gmask & ~(FRAME_POINTER_MASK | RETURN_ADDR_MASK),
		    "st");

  fprintf (file, "\t%s END PROLOGUE\n", ASM_COMMENT_START);
}

/* Do any necessary cleanup after a function to restore stack, frame,
   and regs.  */

static void
arc_output_function_epilogue (FILE *file, HOST_WIDE_INT size)
{
  rtx epilogue_delay = current_function_epilogue_delay_list;
  int noepilogue = FALSE;
  enum arc_function_type fn_type = arc_compute_function_type (current_function_decl);

  /* This is only for the human reader.  */
  fprintf (file, "\t%s EPILOGUE\n", ASM_COMMENT_START);

  size = ARC_STACK_ALIGN (size);
  size = (!current_frame_info.initialized
	   ? arc_compute_frame_size (size)
	   : current_frame_info.total_size);

  if (size == 0 && epilogue_delay == 0)
    {
      rtx insn = get_last_insn ();

      /* If the last insn was a BARRIER, we don't have to write any code
	 because a jump (aka return) was put there.  */
      if (GET_CODE (insn) == NOTE)
	insn = prev_nonnote_insn (insn);
      if (insn && GET_CODE (insn) == BARRIER)
	noepilogue = TRUE;
    }

  if (!noepilogue)
    {
      unsigned int pretend_size = current_frame_info.pretend_size;
      unsigned int frame_size = size - pretend_size;
      int restored, fp_restored_p;
      int can_trust_sp_p = !current_function_calls_alloca;
      const char *sp_str = reg_names[STACK_POINTER_REGNUM];
      const char *fp_str = reg_names[FRAME_POINTER_REGNUM];

      /* ??? There are lots of optimizations that can be done here.
	 EG: Use fp to restore regs if it's closer.
	 Maybe in time we'll do them all.  For now, always restore regs from
	 sp, but don't restore sp if we don't have to.  */

      if (!can_trust_sp_p)
	{
	  gcc_assert (frame_pointer_needed);
	  fprintf (file,"\tsub %s,%s,%d\t\t%s sp not trusted here\n",
		   sp_str, fp_str, frame_size, ASM_COMMENT_START);
	}

      /* Restore any saved registers.  */
      arc_save_restore (file, sp_str, current_frame_info.reg_offset,
			/* The zeroing of these two bits is unnecessary,
			   but leave this in for clarity.  */
			current_frame_info.gmask & ~(FRAME_POINTER_MASK | RETURN_ADDR_MASK),
			"ld");

      if (MUST_SAVE_RETURN_ADDR)
	fprintf (file, "\tld %s,[%s,%d]\n",
		 reg_names[RETURN_ADDR_REGNUM],
		 frame_pointer_needed ? fp_str : sp_str,
		 UNITS_PER_WORD + (frame_pointer_needed ? 0 : frame_size));

      /* Keep track of how much of the stack pointer we've restored.
	 It makes the following a lot more readable.  */
      restored = 0;
      fp_restored_p = 0;

      /* We try to emit the epilogue delay slot insn right after the load
	 of the return address register so that it can execute with the
	 stack intact.  Secondly, loads are delayed.  */
      /* ??? If stack intactness is important, always emit now.  */
      if (MUST_SAVE_RETURN_ADDR && epilogue_delay != NULL_RTX)
	{
	  final_scan_insn (XEXP (epilogue_delay, 0), file, 1, 1, NULL);
	  epilogue_delay = NULL_RTX;
	}

      if (frame_pointer_needed)
	{
	  /* Try to restore the frame pointer in the delay slot.  We can't,
	     however, if any of these is true.  */
	  if (epilogue_delay != NULL_RTX
	      || !SMALL_INT (frame_size)
	      || pretend_size
	      || ARC_INTERRUPT_P (fn_type))
	    {
	      /* Note that we restore fp and sp here!  */
	      fprintf (file, "\tld.a %s,[%s,%d]\n", fp_str, sp_str, frame_size);
	      restored += frame_size;
	      fp_restored_p = 1;
	    }
	}
      else if (!SMALL_INT (size /* frame_size + pretend_size */)
	       || ARC_INTERRUPT_P (fn_type))
	{
	  fprintf (file, "\tadd %s,%s,%d\n", sp_str, sp_str, frame_size);
	  restored += frame_size;
	}

      /* These must be done before the return insn because the delay slot
	 does the final stack restore.  */
      if (ARC_INTERRUPT_P (fn_type))
	{
	  if (epilogue_delay)
	    {
	      final_scan_insn (XEXP (epilogue_delay, 0), file, 1, 1, NULL);
	    }
	}

      /* Emit the return instruction.  */
      {
	static const int regs[4] = {
	  0, RETURN_ADDR_REGNUM, ILINK1_REGNUM, ILINK2_REGNUM
	};

	/* Update the flags, if returning from an interrupt handler. */
	if (ARC_INTERRUPT_P (fn_type))
	  fprintf (file, "\tj.d.f %s\n", reg_names[regs[fn_type]]);
	else
	  fprintf (file, "\tj.d %s\n", reg_names[regs[fn_type]]);
	}

      /* If the only register saved is the return address, we need a
	 nop, unless we have an instruction to put into it.  Otherwise
	 we don't since reloading multiple registers doesn't reference
	 the register being loaded.  */

      if (ARC_INTERRUPT_P (fn_type))
	fprintf (file, "\tadd %s,%s,16\n", sp_str, sp_str);
      else if (epilogue_delay != NULL_RTX)
	{
	  gcc_assert (!frame_pointer_needed || fp_restored_p);
	  gcc_assert (restored >= size);
	  final_scan_insn (XEXP (epilogue_delay, 0), file, 1, 1, NULL);
	}
      else if (frame_pointer_needed && !fp_restored_p)
	{
	  gcc_assert (SMALL_INT (frame_size));
	  /* Note that we restore fp and sp here!  */
	  fprintf (file, "\tld.a %s,[%s,%d]\n", fp_str, sp_str, frame_size);
	}
      else if (restored < size)
	{
	  gcc_assert (SMALL_INT (size - restored));
	  fprintf (file, "\tadd %s,%s," HOST_WIDE_INT_PRINT_DEC "\n",
		   sp_str, sp_str, size - restored);
	}
      else
	fprintf (file, "\tnop\n");
    }

  /* Reset state info for each function.  */
  current_frame_info = zero_frame_info;
  arc_compute_function_type (NULL_TREE);
}

/* Define the number of delay slots needed for the function epilogue.

   Interrupt handlers can't have any epilogue delay slots (it's always needed
   for something else, I think).  For normal functions, we have to worry about
   using call-saved regs as they'll be restored before the delay slot insn.
   Functions with non-empty frames already have enough choices for the epilogue
   delay slot so for now we only consider functions with empty frames.  */

int
arc_delay_slots_for_epilogue (void)
{
  if (arc_compute_function_type (current_function_decl) != ARC_FUNCTION_NORMAL)
    return 0;
  if (!current_frame_info.initialized)
    (void) arc_compute_frame_size (get_frame_size ());
  if (current_frame_info.total_size == 0)
    return 1;
  return 0;
}

/* Return true if TRIAL is a valid insn for the epilogue delay slot.
   Any single length instruction which doesn't reference the stack or frame
   pointer or any call-saved register is OK.  SLOT will always be 0.  */

int
arc_eligible_for_epilogue_delay (rtx trial, int slot)
{
  gcc_assert (!slot);

  if (get_attr_length (trial) == 1
      /* If registers where saved, presumably there's more than enough
	 possibilities for the delay slot.  The alternative is something
	 more complicated (of course, if we expanded the epilogue as rtl
	 this problem would go away).  */
      /* ??? Note that this will always be true since only functions with
	 empty frames have epilogue delay slots.  See
	 arc_delay_slots_for_epilogue.  */
      && current_frame_info.gmask == 0
      && ! reg_mentioned_p (stack_pointer_rtx, PATTERN (trial))
      && ! reg_mentioned_p (frame_pointer_rtx, PATTERN (trial)))
    return 1;
  return 0;
}

/* Return true if OP is a shift operator.  */

int
shift_operator (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  switch (GET_CODE (op))
    {
    case ASHIFTRT:
    case LSHIFTRT:
    case ASHIFT:
      return 1;
    default:
      return 0;
    }
}

/* Output the assembler code for doing a shift.
   We go to a bit of trouble to generate efficient code as the ARC only has
   single bit shifts.  This is taken from the h8300 port.  We only have one
   mode of shifting and can't access individual bytes like the h8300 can, so
   this is greatly simplified (at the expense of not generating hyper-
   efficient code).

   This function is not used if the variable shift insns are present.  */

/* ??? We assume the output operand is the same as operand 1.
   This can be optimized (deleted) in the case of 1 bit shifts.  */
/* ??? We use the loop register here.  We don't use it elsewhere (yet) and
   using it here will give us a chance to play with it.  */

const char *
output_shift (rtx *operands)
{
  rtx shift = operands[3];
  enum machine_mode mode = GET_MODE (shift);
  enum rtx_code code = GET_CODE (shift);
  const char *shift_one;

  gcc_assert (mode == SImode);

  switch (code)
    {
    case ASHIFT:   shift_one = "asl %0,%0"; break;
    case ASHIFTRT: shift_one = "asr %0,%0"; break;
    case LSHIFTRT: shift_one = "lsr %0,%0"; break;
    default:       gcc_unreachable ();
    }

  if (GET_CODE (operands[2]) != CONST_INT)
    {
      if (optimize)
	{
	  output_asm_insn ("sub.f 0,%2,0", operands);
      	  output_asm_insn ("mov lp_count,%2", operands);
	  output_asm_insn ("bz 2f", operands);
	}
      else
	output_asm_insn ("mov %4,%2", operands);
      goto shiftloop;
    }
  else
    {
      int n = INTVAL (operands[2]);

      /* If the count is negative, make it 0.  */
      if (n < 0)
	n = 0;
      /* If the count is too big, truncate it.
         ANSI says shifts of GET_MODE_BITSIZE are undefined - we choose to
	 do the intuitive thing.  */
      else if (n > GET_MODE_BITSIZE (mode))
	n = GET_MODE_BITSIZE (mode);

      /* First see if we can do them inline.  */
      if (n <= 8)
	{
	  while (--n >= 0)
	    output_asm_insn (shift_one, operands);
	}
      /* See if we can use a rotate/and.  */
      else if (n == BITS_PER_WORD - 1)
	{
	  switch (code)
	    {
	    case ASHIFT :
	      output_asm_insn ("and %0,%0,1\n\tror %0,%0", operands);
	      break;
	    case ASHIFTRT :
	      /* The ARC doesn't have a rol insn.  Use something else.  */
	      output_asm_insn ("asl.f 0,%0\n\tsbc %0,0,0", operands);
	      break;
	    case LSHIFTRT :
	      /* The ARC doesn't have a rol insn.  Use something else.  */
	      output_asm_insn ("asl.f 0,%0\n\tadc %0,0,0", operands);
	      break;
	    default:
	      break;
	    }
	}
      /* Must loop.  */
      else
	{
	  char buf[100];

	  if (optimize)
	    output_asm_insn ("mov lp_count,%c2", operands);
	  else
	    output_asm_insn ("mov %4,%c2", operands);
	shiftloop:
	  if (optimize)
	    {
	      if (flag_pic)
		sprintf (buf, "lr %%4,[status]\n\tadd %%4,%%4,6\t%s single insn loop start",
			 ASM_COMMENT_START);
	      else
		sprintf (buf, "mov %%4,%%%%st(1f)\t%s (single insn loop start) >> 2",
			 ASM_COMMENT_START);
	      output_asm_insn (buf, operands);
	      output_asm_insn ("sr %4,[lp_start]", operands);
	      output_asm_insn ("add %4,%4,1", operands);
	      output_asm_insn ("sr %4,[lp_end]", operands);
	      output_asm_insn ("nop\n\tnop", operands);
	      if (flag_pic)
		fprintf (asm_out_file, "\t%s single insn loop\n",
			 ASM_COMMENT_START);
	      else
		fprintf (asm_out_file, "1:\t%s single insn loop\n",
			 ASM_COMMENT_START);
	      output_asm_insn (shift_one, operands);
	      fprintf (asm_out_file, "2:\t%s end single insn loop\n",
		       ASM_COMMENT_START);
	    }
	  else 
	    {
	      fprintf (asm_out_file, "1:\t%s begin shift loop\n",
		       ASM_COMMENT_START);
	      output_asm_insn ("sub.f %4,%4,1", operands);
	      output_asm_insn ("nop", operands);
	      output_asm_insn ("bn.nd 2f", operands);
	      output_asm_insn (shift_one, operands);
	      output_asm_insn ("b.nd 1b", operands);
	      fprintf (asm_out_file, "2:\t%s end shift loop\n",
		       ASM_COMMENT_START);
	    }
	}
    }

  return "";
}

/* Nested function support.  */

/* Emit RTL insns to initialize the variable parts of a trampoline.
   FNADDR is an RTX for the address of the function's pure code.
   CXT is an RTX for the static chain value for the function.  */

void
arc_initialize_trampoline (rtx tramp ATTRIBUTE_UNUSED,
                           rtx fnaddr ATTRIBUTE_UNUSED,
                           rtx cxt ATTRIBUTE_UNUSED)
{
}

/* Set the cpu type and print out other fancy things,
   at the top of the file.  */

static void
arc_file_start (void)
{
  default_file_start ();
  fprintf (asm_out_file, "\t.cpu %s\n", arc_cpu_string);
}

/* Print operand X (an rtx) in assembler syntax to file FILE.
   CODE is a letter or dot (`z' in `%z0') or 0 if no letter was specified.
   For `%' followed by punctuation, CODE is the punctuation and X is null.  */

void
arc_print_operand (FILE *file, rtx x, int code)
{
  switch (code)
    {
    case '#' :
      /* Conditional branches.  For now these are equivalent.  */
    case '*' :
      /* Unconditional branches.  Output the appropriate delay slot suffix.  */
      if (!final_sequence || XVECLEN (final_sequence, 0) == 1)
	{
	  /* There's nothing in the delay slot.  */
	  fputs (".nd", file);
	}
      else
	{
	  rtx jump = XVECEXP (final_sequence, 0, 0);
	  rtx delay = XVECEXP (final_sequence, 0, 1);
	  if (INSN_ANNULLED_BRANCH_P (jump))
	    fputs (INSN_FROM_TARGET_P (delay) ? ".jd" : ".nd", file);
	  else
	    fputs (".d", file);
	}
      return;
    case '?' : /* with leading "." */
    case '!' : /* without leading "." */
      /* This insn can be conditionally executed.  See if the ccfsm machinery
	 says it should be conditionalized.  */
      if (arc_ccfsm_state == 3 || arc_ccfsm_state == 4)
	{
	  /* Is this insn in a delay slot?  */
	  if (final_sequence && XVECLEN (final_sequence, 0) == 2)
	    {
	      rtx insn = XVECEXP (final_sequence, 0, 1);

	      /* If the insn is annulled and is from the target path, we need
		 to inverse the condition test.  */
	      if (INSN_ANNULLED_BRANCH_P (insn))
		{
		  if (INSN_FROM_TARGET_P (insn))
		    fprintf (file, "%s%s",
			     code == '?' ? "." : "",
			     arc_condition_codes[ARC_INVERSE_CONDITION_CODE (arc_ccfsm_current_cc)]);
		  else
		    fprintf (file, "%s%s",
			     code == '?' ? "." : "",
			     arc_condition_codes[arc_ccfsm_current_cc]);
		}
	      else
	        {
		  /* This insn is executed for either path, so don't
		     conditionalize it at all.  */
		  ; /* nothing to do */
		}
	    }
	  else
	    {
	      /* This insn isn't in a delay slot.  */
	      fprintf (file, "%s%s",
		       code == '?' ? "." : "",
		       arc_condition_codes[arc_ccfsm_current_cc]);
	    }
	}
      return;
    case '~' :
      /* Output a nop if we're between a set of the condition codes,
	 and a conditional branch.  */
      if (last_insn_set_cc_p)
	fputs ("nop\n\t", file);
      return;
    case 'd' :
      fputs (arc_condition_codes[get_arc_condition_code (x)], file);
      return;
    case 'D' :
      fputs (arc_condition_codes[ARC_INVERSE_CONDITION_CODE
				 (get_arc_condition_code (x))],
	     file);
      return;
    case 'R' :
      /* Write second word of DImode or DFmode reference,
	 register or memory.  */
      if (GET_CODE (x) == REG)
	fputs (reg_names[REGNO (x)+1], file);
      else if (GET_CODE (x) == MEM)
	{
	  fputc ('[', file);
	  /* Handle possible auto-increment.  Since it is pre-increment and
	     we have already done it, we can just use an offset of four.  */
	  /* ??? This is taken from rs6000.c I think.  I don't think it is
	     currently necessary, but keep it around.  */
	  if (GET_CODE (XEXP (x, 0)) == PRE_INC
	      || GET_CODE (XEXP (x, 0)) == PRE_DEC)
	    output_address (plus_constant (XEXP (XEXP (x, 0), 0), 4));
	  else
	    output_address (plus_constant (XEXP (x, 0), 4));
	  fputc (']', file);
	}
      else
	output_operand_lossage ("invalid operand to %%R code");
      return;
    case 'S' :
      if ((GET_CODE (x) == SYMBOL_REF && SYMBOL_REF_FUNCTION_P (x))
	  || GET_CODE (x) == LABEL_REF)
	{
	  fprintf (file, "%%st(");
	  output_addr_const (file, x);
	  fprintf (file, ")");
	  return;
	}
      break;
    case 'H' :
    case 'L' :
      if (GET_CODE (x) == REG)
	{
	  /* L = least significant word, H = most significant word */
	  if ((TARGET_BIG_ENDIAN != 0) ^ (code == 'L'))
	    fputs (reg_names[REGNO (x)], file);
	  else
	    fputs (reg_names[REGNO (x)+1], file);
	}
      else if (GET_CODE (x) == CONST_INT
	       || GET_CODE (x) == CONST_DOUBLE)
	{
	  rtx first, second;

	  split_double (x, &first, &second);
	  fprintf (file, "0x%08lx",
		   (long)(code == 'L' ? INTVAL (first) : INTVAL (second)));
	}
      else
	output_operand_lossage ("invalid operand to %%H/%%L code");
      return;
    case 'A' :
      {
	char str[30];

	gcc_assert (GET_CODE (x) == CONST_DOUBLE
		    && GET_MODE_CLASS (GET_MODE (x)) == MODE_FLOAT);

	real_to_decimal (str, CONST_DOUBLE_REAL_VALUE (x), sizeof (str), 0, 1);
	fprintf (file, "%s", str);
	return;
      }
    case 'U' :
      /* Output a load/store with update indicator if appropriate.  */
      if (GET_CODE (x) == MEM)
	{
	  if (GET_CODE (XEXP (x, 0)) == PRE_INC
	      || GET_CODE (XEXP (x, 0)) == PRE_DEC)
	    fputs (".a", file);
	}
      else
	output_operand_lossage ("invalid operand to %%U code");
      return;
    case 'V' :
      /* Output cache bypass indicator for a load/store insn.  Volatile memory
	 refs are defined to use the cache bypass mechanism.  */
      if (GET_CODE (x) == MEM)
	{
	  if (MEM_VOLATILE_P (x))
	    fputs (".di", file);
	}
      else
	output_operand_lossage ("invalid operand to %%V code");
      return;
    case 0 :
      /* Do nothing special.  */
      break;
    default :
      /* Unknown flag.  */
      output_operand_lossage ("invalid operand output code");
    }

  switch (GET_CODE (x))
    {
    case REG :
      fputs (reg_names[REGNO (x)], file);
      break;
    case MEM :
      fputc ('[', file);
      if (GET_CODE (XEXP (x, 0)) == PRE_INC)
	output_address (plus_constant (XEXP (XEXP (x, 0), 0),
				       GET_MODE_SIZE (GET_MODE (x))));
      else if (GET_CODE (XEXP (x, 0)) == PRE_DEC)
	output_address (plus_constant (XEXP (XEXP (x, 0), 0),
				       - GET_MODE_SIZE (GET_MODE (x))));
      else
	output_address (XEXP (x, 0));
      fputc (']', file);
      break;
    case CONST_DOUBLE :
      /* We handle SFmode constants here as output_addr_const doesn't.  */
      if (GET_MODE (x) == SFmode)
	{
	  REAL_VALUE_TYPE d;
	  long l;

	  REAL_VALUE_FROM_CONST_DOUBLE (d, x);
	  REAL_VALUE_TO_TARGET_SINGLE (d, l);
	  fprintf (file, "0x%08lx", l);
	  break;
	}
      /* Fall through.  Let output_addr_const deal with it.  */
    default :
      output_addr_const (file, x);
      break;
    }
}

/* Print a memory address as an operand to reference that memory location.  */

void
arc_print_operand_address (FILE *file, rtx addr)
{
  register rtx base, index = 0;
  int offset = 0;

  switch (GET_CODE (addr))
    {
    case REG :
      fputs (reg_names[REGNO (addr)], file);
      break;
    case SYMBOL_REF :
      if (/*???*/ 0 && SYMBOL_REF_FUNCTION_P (addr))
	{
	  fprintf (file, "%%st(");
	  output_addr_const (file, addr);
	  fprintf (file, ")");
	}
      else
	output_addr_const (file, addr);
      break;
    case PLUS :
      if (GET_CODE (XEXP (addr, 0)) == CONST_INT)
	offset = INTVAL (XEXP (addr, 0)), base = XEXP (addr, 1);
      else if (GET_CODE (XEXP (addr, 1)) == CONST_INT)
	offset = INTVAL (XEXP (addr, 1)), base = XEXP (addr, 0);
      else
	base = XEXP (addr, 0), index = XEXP (addr, 1);
      gcc_assert (GET_CODE (base) == REG);
      fputs (reg_names[REGNO (base)], file);
      if (index == 0)
	{
	  if (offset != 0)
	    fprintf (file, ",%d", offset);
	}
      else
	{
	  switch (GET_CODE (index))
	    {
	    case REG:
	      fprintf (file, ",%s", reg_names[REGNO (index)]);
	      break;
	    case SYMBOL_REF:
	      fputc (',', file), output_addr_const (file, index);
	      break;
	    default:
	      gcc_unreachable ();
	    }
	}
      break;
    case PRE_INC :
    case PRE_DEC :
      /* We shouldn't get here as we've lost the mode of the memory object
	 (which says how much to inc/dec by.  */
      gcc_unreachable ();
      break;
    default :
      output_addr_const (file, addr);
      break;
    }
}

/* Update compare/branch separation marker.  */

static void
record_cc_ref (rtx insn)
{
  last_insn_set_cc_p = current_insn_set_cc_p;

  switch (get_attr_cond (insn))
    {
    case COND_SET :
    case COND_SET_ZN :
    case COND_SET_ZNC :
      if (get_attr_length (insn) == 1)
	current_insn_set_cc_p = 1;
      else
	current_insn_set_cc_p = 0;
      break;
    default :
      current_insn_set_cc_p = 0;
      break;
    }
}

/* Conditional execution support.

   This is based on the ARM port but for now is much simpler.

   A finite state machine takes care of noticing whether or not instructions
   can be conditionally executed, and thus decrease execution time and code
   size by deleting branch instructions.  The fsm is controlled by
   final_prescan_insn, and controls the actions of PRINT_OPERAND.  The patterns
   in the .md file for the branch insns also have a hand in this.  */

/* The state of the fsm controlling condition codes are:
   0: normal, do nothing special
   1: don't output this insn
   2: don't output this insn
   3: make insns conditional
   4: make insns conditional

   State transitions (state->state by whom, under what condition):
   0 -> 1 final_prescan_insn, if insn is conditional branch
   0 -> 2 final_prescan_insn, if the `target' is an unconditional branch
   1 -> 3 branch patterns, after having not output the conditional branch
   2 -> 4 branch patterns, after having not output the conditional branch
   3 -> 0 (*targetm.asm_out.internal_label), if the `target' label is reached
          (the target label has CODE_LABEL_NUMBER equal to
	  arc_ccfsm_target_label).
   4 -> 0 final_prescan_insn, if `target' unconditional branch is reached

   If the jump clobbers the conditions then we use states 2 and 4.

   A similar thing can be done with conditional return insns.

   We also handle separating branches from sets of the condition code.
   This is done here because knowledge of the ccfsm state is required,
   we may not be outputting the branch.  */

void
arc_final_prescan_insn (rtx insn,
                        rtx *opvec ATTRIBUTE_UNUSED,
                        int noperands ATTRIBUTE_UNUSED)
{
  /* BODY will hold the body of INSN.  */
  register rtx body = PATTERN (insn);

  /* This will be 1 if trying to repeat the trick (i.e.: do the `else' part of
     an if/then/else), and things need to be reversed.  */
  int reverse = 0;

  /* If we start with a return insn, we only succeed if we find another one.  */
  int seeking_return = 0;
  
  /* START_INSN will hold the insn from where we start looking.  This is the
     first insn after the following code_label if REVERSE is true.  */
  rtx start_insn = insn;

  /* Update compare/branch separation marker.  */
  record_cc_ref (insn);

  /* Allow -mdebug-ccfsm to turn this off so we can see how well it does.
     We can't do this in macro FINAL_PRESCAN_INSN because its called from
     final_scan_insn which has `optimize' as a local.  */
  if (optimize < 2 || TARGET_NO_COND_EXEC)
    return;

  /* If in state 4, check if the target branch is reached, in order to
     change back to state 0.  */
  if (arc_ccfsm_state == 4)
    {
      if (insn == arc_ccfsm_target_insn)
	{
	  arc_ccfsm_target_insn = NULL;
	  arc_ccfsm_state = 0;
	}
      return;
    }

  /* If in state 3, it is possible to repeat the trick, if this insn is an
     unconditional branch to a label, and immediately following this branch
     is the previous target label which is only used once, and the label this
     branch jumps to is not too far off.  Or in other words "we've done the
     `then' part, see if we can do the `else' part."  */
  if (arc_ccfsm_state == 3)
    {
      if (simplejump_p (insn))
	{
	  start_insn = next_nonnote_insn (start_insn);
	  if (GET_CODE (start_insn) == BARRIER)
	    {
	      /* ??? Isn't this always a barrier?  */
	      start_insn = next_nonnote_insn (start_insn);
	    }
	  if (GET_CODE (start_insn) == CODE_LABEL
	      && CODE_LABEL_NUMBER (start_insn) == arc_ccfsm_target_label
	      && LABEL_NUSES (start_insn) == 1)
	    reverse = TRUE;
	  else
	    return;
	}
      else if (GET_CODE (body) == RETURN)
        {
	  start_insn = next_nonnote_insn (start_insn);
	  if (GET_CODE (start_insn) == BARRIER)
	    start_insn = next_nonnote_insn (start_insn);
	  if (GET_CODE (start_insn) == CODE_LABEL
	      && CODE_LABEL_NUMBER (start_insn) == arc_ccfsm_target_label
	      && LABEL_NUSES (start_insn) == 1)
	    {
	      reverse = TRUE;
	      seeking_return = 1;
	    }
	  else
	    return;
        }
      else
	return;
    }

  if (GET_CODE (insn) != JUMP_INSN)
    return;

  /* This jump might be paralleled with a clobber of the condition codes,
     the jump should always come first.  */
  if (GET_CODE (body) == PARALLEL && XVECLEN (body, 0) > 0)
    body = XVECEXP (body, 0, 0);

  if (reverse
      || (GET_CODE (body) == SET && GET_CODE (SET_DEST (body)) == PC
	  && GET_CODE (SET_SRC (body)) == IF_THEN_ELSE))
    {
      int insns_skipped = 0, fail = FALSE, succeed = FALSE;
      /* Flag which part of the IF_THEN_ELSE is the LABEL_REF.  */
      int then_not_else = TRUE;
      /* Nonzero if next insn must be the target label.  */
      int next_must_be_target_label_p;
      rtx this_insn = start_insn, label = 0;

      /* Register the insn jumped to.  */
      if (reverse)
        {
	  if (!seeking_return)
	    label = XEXP (SET_SRC (body), 0);
        }
      else if (GET_CODE (XEXP (SET_SRC (body), 1)) == LABEL_REF)
	label = XEXP (XEXP (SET_SRC (body), 1), 0);
      else if (GET_CODE (XEXP (SET_SRC (body), 2)) == LABEL_REF)
	{
	  label = XEXP (XEXP (SET_SRC (body), 2), 0);
	  then_not_else = FALSE;
	}
      else if (GET_CODE (XEXP (SET_SRC (body), 1)) == RETURN)
	seeking_return = 1;
      else if (GET_CODE (XEXP (SET_SRC (body), 2)) == RETURN)
        {
	  seeking_return = 1;
	  then_not_else = FALSE;
        }
      else
	gcc_unreachable ();

      /* See how many insns this branch skips, and what kind of insns.  If all
	 insns are okay, and the label or unconditional branch to the same
	 label is not too far away, succeed.  */
      for (insns_skipped = 0, next_must_be_target_label_p = FALSE;
	   !fail && !succeed && insns_skipped < MAX_INSNS_SKIPPED;
	   insns_skipped++)
	{
	  rtx scanbody;

	  this_insn = next_nonnote_insn (this_insn);
	  if (!this_insn)
	    break;

	  if (next_must_be_target_label_p)
	    {
	      if (GET_CODE (this_insn) == BARRIER)
		continue;
	      if (GET_CODE (this_insn) == CODE_LABEL
		  && this_insn == label)
		{
		  arc_ccfsm_state = 1;
		  succeed = TRUE;
		}
	      else
		fail = TRUE;
	      break;
	    }

	  scanbody = PATTERN (this_insn);

	  switch (GET_CODE (this_insn))
	    {
	    case CODE_LABEL:
	      /* Succeed if it is the target label, otherwise fail since
		 control falls in from somewhere else.  */
	      if (this_insn == label)
		{
		  arc_ccfsm_state = 1;
		  succeed = TRUE;
		}
	      else
		fail = TRUE;
	      break;

	    case BARRIER:
	      /* Succeed if the following insn is the target label.
		 Otherwise fail.  
		 If return insns are used then the last insn in a function 
		 will be a barrier.  */
	      next_must_be_target_label_p = TRUE;
	      break;

	    case CALL_INSN:
	      /* Can handle a call insn if there are no insns after it.
		 IE: The next "insn" is the target label.  We don't have to
		 worry about delay slots as such insns are SEQUENCE's inside
		 INSN's.  ??? It is possible to handle such insns though.  */
	      if (get_attr_cond (this_insn) == COND_CANUSE)
		next_must_be_target_label_p = TRUE;
	      else
		fail = TRUE;
	      break;

	    case JUMP_INSN:
      	      /* If this is an unconditional branch to the same label, succeed.
		 If it is to another label, do nothing.  If it is conditional,
		 fail.  */
	      /* ??? Probably, the test for the SET and the PC are unnecessary.  */

	      if (GET_CODE (scanbody) == SET
		  && GET_CODE (SET_DEST (scanbody)) == PC)
		{
		  if (GET_CODE (SET_SRC (scanbody)) == LABEL_REF
		      && XEXP (SET_SRC (scanbody), 0) == label && !reverse)
		    {
		      arc_ccfsm_state = 2;
		      succeed = TRUE;
		    }
		  else if (GET_CODE (SET_SRC (scanbody)) == IF_THEN_ELSE)
		    fail = TRUE;
		}
	      else if (GET_CODE (scanbody) == RETURN
		       && seeking_return)
	        {
		  arc_ccfsm_state = 2;
		  succeed = TRUE;
	        }
	      else if (GET_CODE (scanbody) == PARALLEL)
	        {
		  if (get_attr_cond (this_insn) != COND_CANUSE)
		    fail = TRUE;
		}
	      break;

	    case INSN:
	      /* We can only do this with insns that can use the condition
		 codes (and don't set them).  */
	      if (GET_CODE (scanbody) == SET
		  || GET_CODE (scanbody) == PARALLEL)
		{
		  if (get_attr_cond (this_insn) != COND_CANUSE)
		    fail = TRUE;
		}
	      /* We can't handle other insns like sequences.  */
	      else
		fail = TRUE;
	      break;

	    default:
	      break;
	    }
	}

      if (succeed)
	{
	  if ((!seeking_return) && (arc_ccfsm_state == 1 || reverse))
	    arc_ccfsm_target_label = CODE_LABEL_NUMBER (label);
	  else
	    {
	      gcc_assert (seeking_return || arc_ccfsm_state == 2);
	      while (this_insn && GET_CODE (PATTERN (this_insn)) == USE)
	        {
		  this_insn = next_nonnote_insn (this_insn);
		  gcc_assert (!this_insn
			      || (GET_CODE (this_insn) != BARRIER
				  && GET_CODE (this_insn) != CODE_LABEL));
	        }
	      if (!this_insn)
	        {
		  /* Oh dear! we ran off the end, give up.  */
		  extract_insn_cached (insn);
		  arc_ccfsm_state = 0;
		  arc_ccfsm_target_insn = NULL;
		  return;
	        }
	      arc_ccfsm_target_insn = this_insn;
	    }

	  /* If REVERSE is true, ARM_CURRENT_CC needs to be inverted from
	     what it was.  */
	  if (!reverse)
	    arc_ccfsm_current_cc = get_arc_condition_code (XEXP (SET_SRC (body),
								 0));

	  if (reverse || then_not_else)
	    arc_ccfsm_current_cc = ARC_INVERSE_CONDITION_CODE (arc_ccfsm_current_cc);
	}

      /* Restore recog_data.  Getting the attributes of other insns can
	 destroy this array, but final.c assumes that it remains intact
	 across this call.  */
      extract_insn_cached (insn);
    }
}

/* Record that we are currently outputting label NUM with prefix PREFIX.
   It it's the label we're looking for, reset the ccfsm machinery.

   Called from (*targetm.asm_out.internal_label).  */

void
arc_ccfsm_at_label (const char *prefix, int num)
{
  if (arc_ccfsm_state == 3 && arc_ccfsm_target_label == num
      && !strcmp (prefix, "L"))
    {
      arc_ccfsm_state = 0;
      arc_ccfsm_target_insn = NULL_RTX;
    }
}

/* See if the current insn, which is a conditional branch, is to be
   deleted.  */

int
arc_ccfsm_branch_deleted_p (void)
{
  if (arc_ccfsm_state == 1 || arc_ccfsm_state == 2)
    return 1;
  return 0;
}

/* Record a branch isn't output because subsequent insns can be
   conditionalized.  */

void
arc_ccfsm_record_branch_deleted (void)
{
  /* Indicate we're conditionalizing insns now.  */
  arc_ccfsm_state += 2;

  /* If the next insn is a subroutine call, we still need a nop between the
     cc setter and user.  We need to undo the effect of calling record_cc_ref
     for the just deleted branch.  */
  current_insn_set_cc_p = last_insn_set_cc_p;
}

void
arc_va_start (tree valist, rtx nextarg)
{
  /* See arc_setup_incoming_varargs for reasons for this oddity.  */
  if (current_function_args_info < 8
      && (current_function_args_info & 1))
    nextarg = plus_constant (nextarg, UNITS_PER_WORD);

  std_expand_builtin_va_start (valist, nextarg);
}

/* This is how to output a definition of an internal numbered label where
   PREFIX is the class of label and NUM is the number within the class.  */

static void
arc_internal_label (FILE *stream, const char *prefix, unsigned long labelno)
{
  arc_ccfsm_at_label (prefix, labelno);
  default_internal_label (stream, prefix, labelno);
}

/* Worker function for TARGET_ASM_EXTERNAL_LIBCALL.  */

static void
arc_external_libcall (rtx fun ATTRIBUTE_UNUSED)
{
#if 0
/* On the ARC we want to have libgcc's for multiple cpus in one binary.
   We can't use `assemble_name' here as that will call ASM_OUTPUT_LABELREF
   and we'll get another suffix added on if -mmangle-cpu.  */
  if (TARGET_MANGLE_CPU_LIBGCC)
    {
      fprintf (FILE, "\t.rename\t_%s, _%s%s\n",
	       XSTR (SYMREF, 0), XSTR (SYMREF, 0),
	       arc_mangle_suffix);
    }
#endif
}

/* Worker function for TARGET_RETURN_IN_MEMORY.  */

static bool
arc_return_in_memory (tree type, tree fntype ATTRIBUTE_UNUSED)
{
  if (AGGREGATE_TYPE_P (type))
    return true;
  else
    {
      HOST_WIDE_INT size = int_size_in_bytes (type);
      return (size == -1 || size > 8);
    }
}

/* For ARC, All aggregates and arguments greater than 8 bytes are
   passed by reference.  */

static bool
arc_pass_by_reference (CUMULATIVE_ARGS *ca ATTRIBUTE_UNUSED,
		       enum machine_mode mode, tree type,
		       bool named ATTRIBUTE_UNUSED)
{
  unsigned HOST_WIDE_INT size;

  if (type)
    {
      if (AGGREGATE_TYPE_P (type))
	return true;
      size = int_size_in_bytes (type);
    }
  else
    size = GET_MODE_SIZE (mode);

  return size > 8;
}
