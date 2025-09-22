/* Subroutines for insn-output.c for NEC V850 series
   Copyright (C) 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005
   Free Software Foundation, Inc.
   Contributed by Jeff Law (law@cygnus.com).

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
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
#include "recog.h"
#include "expr.h"
#include "function.h"
#include "toplev.h"
#include "ggc.h"
#include "integrate.h"
#include "tm_p.h"
#include "target.h"
#include "target-def.h"

#ifndef streq
#define streq(a,b) (strcmp (a, b) == 0)
#endif

/* Function prototypes for stupid compilers:  */
static bool v850_handle_option       (size_t, const char *, int);
static void const_double_split       (rtx, HOST_WIDE_INT *, HOST_WIDE_INT *);
static int  const_costs_int          (HOST_WIDE_INT, int);
static int  const_costs		     (rtx, enum rtx_code);
static bool v850_rtx_costs	     (rtx, int, int, int *);
static void substitute_ep_register   (rtx, rtx, int, int, rtx *, rtx *);
static void v850_reorg		     (void);
static int  ep_memory_offset         (enum machine_mode, int);
static void v850_set_data_area       (tree, v850_data_area);
const struct attribute_spec v850_attribute_table[];
static tree v850_handle_interrupt_attribute (tree *, tree, tree, int, bool *);
static tree v850_handle_data_area_attribute (tree *, tree, tree, int, bool *);
static void v850_insert_attributes   (tree, tree *);
static void v850_asm_init_sections   (void);
static section *v850_select_section (tree, int, unsigned HOST_WIDE_INT);
static void v850_encode_data_area    (tree, rtx);
static void v850_encode_section_info (tree, rtx, int);
static bool v850_return_in_memory    (tree, tree);
static void v850_setup_incoming_varargs (CUMULATIVE_ARGS *, enum machine_mode,
					 tree, int *, int);
static bool v850_pass_by_reference (CUMULATIVE_ARGS *, enum machine_mode,
				    tree, bool);
static int v850_arg_partial_bytes (CUMULATIVE_ARGS *, enum machine_mode,
				   tree, bool);

/* Information about the various small memory areas.  */
struct small_memory_info small_memory[ (int)SMALL_MEMORY_max ] =
{
  /* name	max	physical max */
  { "tda",	0,		256 },
  { "sda",	0,		65536 },
  { "zda",	0,		32768 },
};

/* Names of the various data areas used on the v850.  */
tree GHS_default_section_names [(int) COUNT_OF_GHS_SECTION_KINDS];
tree GHS_current_section_names [(int) COUNT_OF_GHS_SECTION_KINDS];

/* Track the current data area set by the data area pragma (which 
   can be nested).  Tested by check_default_data_area.  */
data_area_stack_element * data_area_stack = NULL;

/* True if we don't need to check any more if the current
   function is an interrupt handler.  */
static int v850_interrupt_cache_p = FALSE;

/* Whether current function is an interrupt handler.  */
static int v850_interrupt_p = FALSE;

static GTY(()) section *rosdata_section;
static GTY(()) section *rozdata_section;
static GTY(()) section *tdata_section;
static GTY(()) section *zdata_section;
static GTY(()) section *zbss_section;

/* Initialize the GCC target structure.  */
#undef TARGET_ASM_ALIGNED_HI_OP
#define TARGET_ASM_ALIGNED_HI_OP "\t.hword\t"

#undef TARGET_ATTRIBUTE_TABLE
#define TARGET_ATTRIBUTE_TABLE v850_attribute_table

#undef TARGET_INSERT_ATTRIBUTES
#define TARGET_INSERT_ATTRIBUTES v850_insert_attributes

#undef  TARGET_ASM_SELECT_SECTION
#define TARGET_ASM_SELECT_SECTION  v850_select_section

/* The assembler supports switchable .bss sections, but
   v850_select_section doesn't yet make use of them.  */
#undef  TARGET_HAVE_SWITCHABLE_BSS_SECTIONS
#define TARGET_HAVE_SWITCHABLE_BSS_SECTIONS false

#undef TARGET_ENCODE_SECTION_INFO
#define TARGET_ENCODE_SECTION_INFO v850_encode_section_info

#undef TARGET_ASM_FILE_START_FILE_DIRECTIVE
#define TARGET_ASM_FILE_START_FILE_DIRECTIVE true

#undef TARGET_DEFAULT_TARGET_FLAGS
#define TARGET_DEFAULT_TARGET_FLAGS (MASK_DEFAULT | MASK_APP_REGS)
#undef TARGET_HANDLE_OPTION
#define TARGET_HANDLE_OPTION v850_handle_option

#undef TARGET_RTX_COSTS
#define TARGET_RTX_COSTS v850_rtx_costs

#undef TARGET_ADDRESS_COST
#define TARGET_ADDRESS_COST hook_int_rtx_0

#undef TARGET_MACHINE_DEPENDENT_REORG
#define TARGET_MACHINE_DEPENDENT_REORG v850_reorg

#undef TARGET_PROMOTE_PROTOTYPES
#define TARGET_PROMOTE_PROTOTYPES hook_bool_tree_true

#undef TARGET_RETURN_IN_MEMORY
#define TARGET_RETURN_IN_MEMORY v850_return_in_memory

#undef TARGET_PASS_BY_REFERENCE
#define TARGET_PASS_BY_REFERENCE v850_pass_by_reference

#undef TARGET_CALLEE_COPIES
#define TARGET_CALLEE_COPIES hook_bool_CUMULATIVE_ARGS_mode_tree_bool_true

#undef TARGET_SETUP_INCOMING_VARARGS
#define TARGET_SETUP_INCOMING_VARARGS v850_setup_incoming_varargs

#undef TARGET_ARG_PARTIAL_BYTES
#define TARGET_ARG_PARTIAL_BYTES v850_arg_partial_bytes

struct gcc_target targetm = TARGET_INITIALIZER;

/* Set the maximum size of small memory area TYPE to the value given
   by VALUE.  Return true if VALUE was syntactically correct.  VALUE
   starts with the argument separator: either "-" or "=".  */

static bool
v850_handle_memory_option (enum small_memory_type type, const char *value)
{
  int i, size;

  if (*value != '-' && *value != '=')
    return false;

  value++;
  for (i = 0; value[i]; i++)
    if (!ISDIGIT (value[i]))
      return false;

  size = atoi (value);
  if (size > small_memory[type].physical_max)
    error ("value passed to %<-m%s%> is too large", small_memory[type].name);
  else
    small_memory[type].max = size;
  return true;
}

/* Implement TARGET_HANDLE_OPTION.  */

static bool
v850_handle_option (size_t code, const char *arg, int value ATTRIBUTE_UNUSED)
{
  switch (code)
    {
    case OPT_mspace:
      target_flags |= MASK_EP | MASK_PROLOG_FUNCTION;
      return true;

    case OPT_mv850:
      target_flags &= ~(MASK_CPU ^ MASK_V850);
      return true;

    case OPT_mv850e:
    case OPT_mv850e1:
      target_flags &= ~(MASK_CPU ^ MASK_V850E);
      return true;

    case OPT_mtda:
      return v850_handle_memory_option (SMALL_MEMORY_TDA, arg);

    case OPT_msda:
      return v850_handle_memory_option (SMALL_MEMORY_SDA, arg);

    case OPT_mzda:
      return v850_handle_memory_option (SMALL_MEMORY_ZDA, arg);

    default:
      return true;
    }
}

static bool
v850_pass_by_reference (CUMULATIVE_ARGS *cum ATTRIBUTE_UNUSED,
			enum machine_mode mode, tree type,
			bool named ATTRIBUTE_UNUSED)
{
  unsigned HOST_WIDE_INT size;

  if (type)
    size = int_size_in_bytes (type);
  else
    size = GET_MODE_SIZE (mode);

  return size > 8;
}

/* Return an RTX to represent where a value with mode MODE will be returned
   from a function.  If the result is 0, the argument is pushed.  */

rtx
function_arg (CUMULATIVE_ARGS * cum,
              enum machine_mode mode,
              tree type,
              int named)
{
  rtx result = 0;
  int size, align;

  if (TARGET_GHS && !named)
    return NULL_RTX;

  if (mode == BLKmode)
    size = int_size_in_bytes (type);
  else
    size = GET_MODE_SIZE (mode);

  if (size < 1)
    return 0;

  if (type)
    align = TYPE_ALIGN (type) / BITS_PER_UNIT;
  else
    align = size;

  cum->nbytes = (cum->nbytes + align - 1) &~(align - 1);

  if (cum->nbytes > 4 * UNITS_PER_WORD)
    return 0;

  if (type == NULL_TREE
      && cum->nbytes + size > 4 * UNITS_PER_WORD)
    return 0;

  switch (cum->nbytes / UNITS_PER_WORD)
    {
    case 0:
      result = gen_rtx_REG (mode, 6);
      break;
    case 1:
      result = gen_rtx_REG (mode, 7);
      break;
    case 2:
      result = gen_rtx_REG (mode, 8);
      break;
    case 3:
      result = gen_rtx_REG (mode, 9);
      break;
    default:
      result = 0;
    }

  return result;
}


/* Return the number of bytes which must be put into registers
   for values which are part in registers and part in memory.  */

static int
v850_arg_partial_bytes (CUMULATIVE_ARGS * cum, enum machine_mode mode,
                        tree type, bool named)
{
  int size, align;

  if (TARGET_GHS && !named)
    return 0;

  if (mode == BLKmode)
    size = int_size_in_bytes (type);
  else
    size = GET_MODE_SIZE (mode);

  if (type)
    align = TYPE_ALIGN (type) / BITS_PER_UNIT;
  else
    align = size;

  cum->nbytes = (cum->nbytes + align - 1) &~(align - 1);

  if (cum->nbytes > 4 * UNITS_PER_WORD)
    return 0;

  if (cum->nbytes + size <= 4 * UNITS_PER_WORD)
    return 0;

  if (type == NULL_TREE
      && cum->nbytes + size > 4 * UNITS_PER_WORD)
    return 0;

  return 4 * UNITS_PER_WORD - cum->nbytes;
}


/* Return the high and low words of a CONST_DOUBLE */

static void
const_double_split (rtx x, HOST_WIDE_INT * p_high, HOST_WIDE_INT * p_low)
{
  if (GET_CODE (x) == CONST_DOUBLE)
    {
      long t[2];
      REAL_VALUE_TYPE rv;

      switch (GET_MODE (x))
	{
	case DFmode:
	  REAL_VALUE_FROM_CONST_DOUBLE (rv, x);
	  REAL_VALUE_TO_TARGET_DOUBLE (rv, t);
	  *p_high = t[1];	/* since v850 is little endian */
	  *p_low = t[0];	/* high is second word */
	  return;

	case SFmode:
	  REAL_VALUE_FROM_CONST_DOUBLE (rv, x);
	  REAL_VALUE_TO_TARGET_SINGLE (rv, *p_high);
	  *p_low = 0;
	  return;

	case VOIDmode:
	case DImode:
	  *p_high = CONST_DOUBLE_HIGH (x);
	  *p_low  = CONST_DOUBLE_LOW (x);
	  return;

	default:
	  break;
	}
    }

  fatal_insn ("const_double_split got a bad insn:", x);
}


/* Return the cost of the rtx R with code CODE.  */

static int
const_costs_int (HOST_WIDE_INT value, int zero_cost)
{
  if (CONST_OK_FOR_I (value))
      return zero_cost;
  else if (CONST_OK_FOR_J (value))
    return 1;
  else if (CONST_OK_FOR_K (value))
    return 2;
  else
    return 4;
}

static int
const_costs (rtx r, enum rtx_code c)
{
  HOST_WIDE_INT high, low;

  switch (c)
    {
    case CONST_INT:
      return const_costs_int (INTVAL (r), 0);

    case CONST_DOUBLE:
      const_double_split (r, &high, &low);
      if (GET_MODE (r) == SFmode)
	return const_costs_int (high, 1);
      else
	return const_costs_int (high, 1) + const_costs_int (low, 1);

    case SYMBOL_REF:
    case LABEL_REF:
    case CONST:
      return 2;

    case HIGH:
      return 1;

    default:
      return 4;
    }
}

static bool
v850_rtx_costs (rtx x,
                int code,
                int outer_code ATTRIBUTE_UNUSED,
                int * total)
{
  switch (code)
    {
    case CONST_INT:
    case CONST_DOUBLE:
    case CONST:
    case SYMBOL_REF:
    case LABEL_REF:
      *total = COSTS_N_INSNS (const_costs (x, code));
      return true;

    case MOD:
    case DIV:
    case UMOD:
    case UDIV:
      if (TARGET_V850E && optimize_size)
        *total = 6;
      else
	*total = 60;
      return true;

    case MULT:
      if (TARGET_V850E
	  && (   GET_MODE (x) == SImode
	      || GET_MODE (x) == HImode
	      || GET_MODE (x) == QImode))
        {
	  if (GET_CODE (XEXP (x, 1)) == REG)
	    *total = 4;
	  else if (GET_CODE (XEXP (x, 1)) == CONST_INT)
	    {
	      if (CONST_OK_FOR_O (INTVAL (XEXP (x, 1))))
	        *total = 6;
	      else if (CONST_OK_FOR_K (INTVAL (XEXP (x, 1))))
	        *total = 10;
	    }
        }
      else
	*total = 20;
      return true;

    default:
      return false;
    }
}

/* Print operand X using operand code CODE to assembly language output file
   FILE.  */

void
print_operand (FILE * file, rtx x, int code)
{
  HOST_WIDE_INT high, low;

  switch (code)
    {
    case 'c':
      /* We use 'c' operands with symbols for .vtinherit */
      if (GET_CODE (x) == SYMBOL_REF)
        {
          output_addr_const(file, x);
          break;
        }
      /* fall through */
    case 'b':
    case 'B':
    case 'C':
      switch ((code == 'B' || code == 'C')
	      ? reverse_condition (GET_CODE (x)) : GET_CODE (x))
	{
	  case NE:
	    if (code == 'c' || code == 'C')
	      fprintf (file, "nz");
	    else
	      fprintf (file, "ne");
	    break;
	  case EQ:
	    if (code == 'c' || code == 'C')
	      fprintf (file, "z");
	    else
	      fprintf (file, "e");
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
	    fprintf (file, "nl");
	    break;
	  case GTU:
	    fprintf (file, "h");
	    break;
	  case LEU:
	    fprintf (file, "nh");
	    break;
	  case LTU:
	    fprintf (file, "l");
	    break;
	  default:
	    gcc_unreachable ();
	}
      break;
    case 'F':			/* high word of CONST_DOUBLE */
      switch (GET_CODE (x))
	{
	case CONST_INT:
	  fprintf (file, "%d", (INTVAL (x) >= 0) ? 0 : -1);
	  break;
	  
	case CONST_DOUBLE:
	  const_double_split (x, &high, &low);
	  fprintf (file, "%ld", (long) high);
	  break;

	default:
	  gcc_unreachable ();
	}
      break;
    case 'G':			/* low word of CONST_DOUBLE */
      switch (GET_CODE (x))
	{
	case CONST_INT:
	  fprintf (file, "%ld", (long) INTVAL (x));
	  break;
	  
	case CONST_DOUBLE:
	  const_double_split (x, &high, &low);
	  fprintf (file, "%ld", (long) low);
	  break;

	default:
	  gcc_unreachable ();
	}
      break;
    case 'L':
      fprintf (file, "%d\n", (int)(INTVAL (x) & 0xffff));
      break;
    case 'M':
      fprintf (file, "%d", exact_log2 (INTVAL (x)));
      break;
    case 'O':
      gcc_assert (special_symbolref_operand (x, VOIDmode));
      
      if (GET_CODE (x) == CONST)
	x = XEXP (XEXP (x, 0), 0);
      else
	gcc_assert (GET_CODE (x) == SYMBOL_REF);
      
      if (SYMBOL_REF_ZDA_P (x))
	fprintf (file, "zdaoff");
      else if (SYMBOL_REF_SDA_P (x))
	fprintf (file, "sdaoff");
      else if (SYMBOL_REF_TDA_P (x))
	fprintf (file, "tdaoff");
      else
	gcc_unreachable ();
      break;
    case 'P':
      gcc_assert (special_symbolref_operand (x, VOIDmode));
      output_addr_const (file, x);
      break;
    case 'Q':
      gcc_assert (special_symbolref_operand (x, VOIDmode));
      
      if (GET_CODE (x) == CONST)
	x = XEXP (XEXP (x, 0), 0);
      else
	gcc_assert (GET_CODE (x) == SYMBOL_REF);
      
      if (SYMBOL_REF_ZDA_P (x))
	fprintf (file, "r0");
      else if (SYMBOL_REF_SDA_P (x))
	fprintf (file, "gp");
      else if (SYMBOL_REF_TDA_P (x))
	fprintf (file, "ep");
      else
	gcc_unreachable ();
      break;
    case 'R':		/* 2nd word of a double.  */
      switch (GET_CODE (x))
	{
	case REG:
	  fprintf (file, reg_names[REGNO (x) + 1]);
	  break;
	case MEM:
	  x = XEXP (adjust_address (x, SImode, 4), 0);
	  print_operand_address (file, x);
	  if (GET_CODE (x) == CONST_INT)
	    fprintf (file, "[r0]");
	  break;
	  
	default:
	  break;
	}
      break;
    case 'S':
      {
        /* If it's a reference to a TDA variable, use sst/sld vs. st/ld.  */
        if (GET_CODE (x) == MEM && ep_memory_operand (x, GET_MODE (x), FALSE))
          fputs ("s", file);

        break;
      }
    case 'T':
      {
	/* Like an 'S' operand above, but for unsigned loads only.  */
        if (GET_CODE (x) == MEM && ep_memory_operand (x, GET_MODE (x), TRUE))
          fputs ("s", file);

        break;
      }
    case 'W':			/* print the instruction suffix */
      switch (GET_MODE (x))
	{
	default:
	  gcc_unreachable ();

	case QImode: fputs (".b", file); break;
	case HImode: fputs (".h", file); break;
	case SImode: fputs (".w", file); break;
	case SFmode: fputs (".w", file); break;
	}
      break;
    case '.':			/* register r0 */
      fputs (reg_names[0], file);
      break;
    case 'z':			/* reg or zero */
      if (GET_CODE (x) == REG)
	fputs (reg_names[REGNO (x)], file);
      else
	{
	  gcc_assert (x == const0_rtx);
	  fputs (reg_names[0], file);
	}
      break;
    default:
      switch (GET_CODE (x))
	{
	case MEM:
	  if (GET_CODE (XEXP (x, 0)) == CONST_INT)
	    output_address (gen_rtx_PLUS (SImode, gen_rtx_REG (SImode, 0),
					  XEXP (x, 0)));
	  else
	    output_address (XEXP (x, 0));
	  break;

	case REG:
	  fputs (reg_names[REGNO (x)], file);
	  break;
	case SUBREG:
	  fputs (reg_names[subreg_regno (x)], file);
	  break;
	case CONST_INT:
	case SYMBOL_REF:
	case CONST:
	case LABEL_REF:
	case CODE_LABEL:
	  print_operand_address (file, x);
	  break;
	default:
	  gcc_unreachable ();
	}
      break;

    }
}


/* Output assembly language output for the address ADDR to FILE.  */

void
print_operand_address (FILE * file, rtx addr)
{
  switch (GET_CODE (addr))
    {
    case REG:
      fprintf (file, "0[");
      print_operand (file, addr, 0);
      fprintf (file, "]");
      break;
    case LO_SUM:
      if (GET_CODE (XEXP (addr, 0)) == REG)
	{
	  /* reg,foo */
	  fprintf (file, "lo(");
	  print_operand (file, XEXP (addr, 1), 0);
	  fprintf (file, ")[");
	  print_operand (file, XEXP (addr, 0), 0);
	  fprintf (file, "]");
	}
      break;
    case PLUS:
      if (GET_CODE (XEXP (addr, 0)) == REG
	  || GET_CODE (XEXP (addr, 0)) == SUBREG)
	{
	  /* reg,foo */
	  print_operand (file, XEXP (addr, 1), 0);
	  fprintf (file, "[");
	  print_operand (file, XEXP (addr, 0), 0);
	  fprintf (file, "]");
	}
      else
	{
	  print_operand (file, XEXP (addr, 0), 0);
	  fprintf (file, "+");
	  print_operand (file, XEXP (addr, 1), 0);
	}
      break;
    case SYMBOL_REF:
      {
        const char *off_name = NULL;
        const char *reg_name = NULL;

	if (SYMBOL_REF_ZDA_P (addr))
          {
            off_name = "zdaoff";
            reg_name = "r0";
          }
        else if (SYMBOL_REF_SDA_P (addr))
          {
            off_name = "sdaoff";
            reg_name = "gp";
          }
        else if (SYMBOL_REF_TDA_P (addr))
          {
            off_name = "tdaoff";
            reg_name = "ep";
          }

	if (off_name)
          fprintf (file, "%s(", off_name);
        output_addr_const (file, addr);
	if (reg_name)
          fprintf (file, ")[%s]", reg_name);
      }
      break;
    case CONST:
      if (special_symbolref_operand (addr, VOIDmode))
        {
	  rtx x = XEXP (XEXP (addr, 0), 0);
          const char *off_name;
          const char *reg_name;

          if (SYMBOL_REF_ZDA_P (x))
            {
              off_name = "zdaoff";
              reg_name = "r0";
            }
          else if (SYMBOL_REF_SDA_P (x))
            {
              off_name = "sdaoff";
              reg_name = "gp";
            }
          else if (SYMBOL_REF_TDA_P (x))
            {
              off_name = "tdaoff";
              reg_name = "ep";
            }
          else
            gcc_unreachable ();

          fprintf (file, "%s(", off_name);
          output_addr_const (file, addr);
          fprintf (file, ")[%s]", reg_name);
        }
      else
        output_addr_const (file, addr);
      break;
    default:
      output_addr_const (file, addr);
      break;
    }
}

/* When assemble_integer is used to emit the offsets for a switch
   table it can encounter (TRUNCATE:HI (MINUS:SI (LABEL_REF:SI) (LABEL_REF:SI))).
   output_addr_const will normally barf at this, but it is OK to omit
   the truncate and just emit the difference of the two labels.  The
   .hword directive will automatically handle the truncation for us.
   
   Returns 1 if rtx was handled, 0 otherwise.  */

int
v850_output_addr_const_extra (FILE * file, rtx x)
{
  if (GET_CODE (x) != TRUNCATE)
    return 0;

  x = XEXP (x, 0);

  /* We must also handle the case where the switch table was passed a
     constant value and so has been collapsed.  In this case the first
     label will have been deleted.  In such a case it is OK to emit
     nothing, since the table will not be used.
     (cf gcc.c-torture/compile/990801-1.c).  */
  if (GET_CODE (x) == MINUS
      && GET_CODE (XEXP (x, 0)) == LABEL_REF
      && GET_CODE (XEXP (XEXP (x, 0), 0)) == CODE_LABEL
      && INSN_DELETED_P (XEXP (XEXP (x, 0), 0)))
    return 1;

  output_addr_const (file, x);
  return 1;
}

/* Return appropriate code to load up a 1, 2, or 4 integer/floating
   point value.  */

const char *
output_move_single (rtx * operands)
{
  rtx dst = operands[0];
  rtx src = operands[1];

  if (REG_P (dst))
    {
      if (REG_P (src))
	return "mov %1,%0";

      else if (GET_CODE (src) == CONST_INT)
	{
	  HOST_WIDE_INT value = INTVAL (src);

	  if (CONST_OK_FOR_J (value))		/* Signed 5 bit immediate.  */
	    return "mov %1,%0";

	  else if (CONST_OK_FOR_K (value))	/* Signed 16 bit immediate.  */
	    return "movea lo(%1),%.,%0";

	  else if (CONST_OK_FOR_L (value))	/* Upper 16 bits were set.  */
	    return "movhi hi(%1),%.,%0";

	  /* A random constant.  */
	  else if (TARGET_V850E)
	      return "mov %1,%0";
	  else
	    return "movhi hi(%1),%.,%0\n\tmovea lo(%1),%0,%0";
	}

      else if (GET_CODE (src) == CONST_DOUBLE && GET_MODE (src) == SFmode)
	{
	  HOST_WIDE_INT high, low;

	  const_double_split (src, &high, &low);

	  if (CONST_OK_FOR_J (high))		/* Signed 5 bit immediate.  */
	    return "mov %F1,%0";

	  else if (CONST_OK_FOR_K (high))	/* Signed 16 bit immediate.  */
	    return "movea lo(%F1),%.,%0";

	  else if (CONST_OK_FOR_L (high))	/* Upper 16 bits were set.  */
	    return "movhi hi(%F1),%.,%0";

	  /* A random constant.  */
	  else if (TARGET_V850E)
	      return "mov %F1,%0";

	  else
	    return "movhi hi(%F1),%.,%0\n\tmovea lo(%F1),%0,%0";
	}

      else if (GET_CODE (src) == MEM)
	return "%S1ld%W1 %1,%0";

      else if (special_symbolref_operand (src, VOIDmode))
	return "movea %O1(%P1),%Q1,%0";

      else if (GET_CODE (src) == LABEL_REF
	       || GET_CODE (src) == SYMBOL_REF
	       || GET_CODE (src) == CONST)
	{
	  if (TARGET_V850E)
	    return "mov hilo(%1),%0";
	  else
	    return "movhi hi(%1),%.,%0\n\tmovea lo(%1),%0,%0";
	}

      else if (GET_CODE (src) == HIGH)
	return "movhi hi(%1),%.,%0";

      else if (GET_CODE (src) == LO_SUM)
	{
	  operands[2] = XEXP (src, 0);
	  operands[3] = XEXP (src, 1);
	  return "movea lo(%3),%2,%0";
	}
    }

  else if (GET_CODE (dst) == MEM)
    {
      if (REG_P (src))
	return "%S0st%W0 %1,%0";

      else if (GET_CODE (src) == CONST_INT && INTVAL (src) == 0)
	return "%S0st%W0 %.,%0";

      else if (GET_CODE (src) == CONST_DOUBLE
	       && CONST0_RTX (GET_MODE (dst)) == src)
	return "%S0st%W0 %.,%0";
    }

  fatal_insn ("output_move_single:", gen_rtx_SET (VOIDmode, dst, src));
  return "";
}


/* Return appropriate code to load up an 8 byte integer or
   floating point value */

const char *
output_move_double (rtx * operands)
{
  enum machine_mode mode = GET_MODE (operands[0]);
  rtx dst = operands[0];
  rtx src = operands[1];

  if (register_operand (dst, mode)
      && register_operand (src, mode))
    {
      if (REGNO (src) + 1 == REGNO (dst))
	return "mov %R1,%R0\n\tmov %1,%0";
      else
	return "mov %1,%0\n\tmov %R1,%R0";
    }

  /* Storing 0 */
  if (GET_CODE (dst) == MEM
      && ((GET_CODE (src) == CONST_INT && INTVAL (src) == 0)
	  || (GET_CODE (src) == CONST_DOUBLE && CONST_DOUBLE_OK_FOR_G (src))))
    return "st.w %.,%0\n\tst.w %.,%R0";

  if (GET_CODE (src) == CONST_INT || GET_CODE (src) == CONST_DOUBLE)
    {
      HOST_WIDE_INT high_low[2];
      int i;
      rtx xop[10];

      if (GET_CODE (src) == CONST_DOUBLE)
	const_double_split (src, &high_low[1], &high_low[0]);
      else
	{
	  high_low[0] = INTVAL (src);
	  high_low[1] = (INTVAL (src) >= 0) ? 0 : -1;
	}

      for (i = 0; i < 2; i++)
	{
	  xop[0] = gen_rtx_REG (SImode, REGNO (dst)+i);
	  xop[1] = GEN_INT (high_low[i]);
	  output_asm_insn (output_move_single (xop), xop);
	}

      return "";
    }

  if (GET_CODE (src) == MEM)
    {
      int ptrreg = -1;
      int dreg = REGNO (dst);
      rtx inside = XEXP (src, 0);

      if (GET_CODE (inside) == REG)
 	ptrreg = REGNO (inside);
      else if (GET_CODE (inside) == SUBREG)
	ptrreg = subreg_regno (inside);
      else if (GET_CODE (inside) == PLUS)
	ptrreg = REGNO (XEXP (inside, 0));
      else if (GET_CODE (inside) == LO_SUM)
	ptrreg = REGNO (XEXP (inside, 0));

      if (dreg == ptrreg)
	return "ld.w %R1,%R0\n\tld.w %1,%0";
    }

  if (GET_CODE (src) == MEM)
    return "ld.w %1,%0\n\tld.w %R1,%R0";
  
  if (GET_CODE (dst) == MEM)
    return "st.w %1,%0\n\tst.w %R1,%R0";

  return "mov %1,%0\n\tmov %R1,%R0";
}


/* Return maximum offset supported for a short EP memory reference of mode
   MODE and signedness UNSIGNEDP.  */

static int
ep_memory_offset (enum machine_mode mode, int unsignedp ATTRIBUTE_UNUSED)
{
  int max_offset = 0;

  switch (mode)
    {
    case QImode:
      if (TARGET_SMALL_SLD)
	max_offset = (1 << 4);
      else if (TARGET_V850E 
	       && (   (  unsignedp && ! TARGET_US_BIT_SET)
		   || (! unsignedp &&   TARGET_US_BIT_SET)))
	max_offset = (1 << 4);
      else
	max_offset = (1 << 7);
      break;

    case HImode:
      if (TARGET_SMALL_SLD)
	max_offset = (1 << 5);
      else if (TARGET_V850E
	       && (   (  unsignedp && ! TARGET_US_BIT_SET)
		   || (! unsignedp &&   TARGET_US_BIT_SET)))
	max_offset = (1 << 5);
      else
	max_offset = (1 << 8);
      break;

    case SImode:
    case SFmode:
      max_offset = (1 << 8);
      break;
      
    default:
      break;
    }

  return max_offset;
}

/* Return true if OP is a valid short EP memory reference */

int
ep_memory_operand (rtx op, enum machine_mode mode, int unsigned_load)
{
  rtx addr, op0, op1;
  int max_offset;
  int mask;

  /* If we are not using the EP register on a per-function basis
     then do not allow this optimization at all.  This is to
     prevent the use of the SLD/SST instructions which cannot be
     guaranteed to work properly due to a hardware bug.  */
  if (!TARGET_EP)
    return FALSE;

  if (GET_CODE (op) != MEM)
    return FALSE;

  max_offset = ep_memory_offset (mode, unsigned_load);

  mask = GET_MODE_SIZE (mode) - 1;

  addr = XEXP (op, 0);
  if (GET_CODE (addr) == CONST)
    addr = XEXP (addr, 0);

  switch (GET_CODE (addr))
    {
    default:
      break;

    case SYMBOL_REF:
      return SYMBOL_REF_TDA_P (addr);

    case REG:
      return REGNO (addr) == EP_REGNUM;

    case PLUS:
      op0 = XEXP (addr, 0);
      op1 = XEXP (addr, 1);
      if (GET_CODE (op1) == CONST_INT
	  && INTVAL (op1) < max_offset
	  && INTVAL (op1) >= 0
	  && (INTVAL (op1) & mask) == 0)
	{
	  if (GET_CODE (op0) == REG && REGNO (op0) == EP_REGNUM)
	    return TRUE;

	  if (GET_CODE (op0) == SYMBOL_REF && SYMBOL_REF_TDA_P (op0))
	    return TRUE;
	}
      break;
    }

  return FALSE;
}

/* Substitute memory references involving a pointer, to use the ep pointer,
   taking care to save and preserve the ep.  */

static void
substitute_ep_register (rtx first_insn,
                        rtx last_insn,
                        int uses,
                        int regno,
                        rtx * p_r1,
                        rtx * p_ep)
{
  rtx reg = gen_rtx_REG (Pmode, regno);
  rtx insn;

  if (!*p_r1)
    {
      regs_ever_live[1] = 1;
      *p_r1 = gen_rtx_REG (Pmode, 1);
      *p_ep = gen_rtx_REG (Pmode, 30);
    }

  if (TARGET_DEBUG)
    fprintf (stderr, "\
Saved %d bytes (%d uses of register %s) in function %s, starting as insn %d, ending at %d\n",
	     2 * (uses - 3), uses, reg_names[regno],
	     IDENTIFIER_POINTER (DECL_NAME (current_function_decl)),
	     INSN_UID (first_insn), INSN_UID (last_insn));

  if (GET_CODE (first_insn) == NOTE)
    first_insn = next_nonnote_insn (first_insn);

  last_insn = next_nonnote_insn (last_insn);
  for (insn = first_insn; insn && insn != last_insn; insn = NEXT_INSN (insn))
    {
      if (GET_CODE (insn) == INSN)
	{
	  rtx pattern = single_set (insn);

	  /* Replace the memory references.  */
	  if (pattern)
	    {
	      rtx *p_mem;
	      /* Memory operands are signed by default.  */
	      int unsignedp = FALSE;

	      if (GET_CODE (SET_DEST (pattern)) == MEM
		  && GET_CODE (SET_SRC (pattern)) == MEM)
		p_mem = (rtx *)0;

	      else if (GET_CODE (SET_DEST (pattern)) == MEM)
		p_mem = &SET_DEST (pattern);

	      else if (GET_CODE (SET_SRC (pattern)) == MEM)
		p_mem = &SET_SRC (pattern);

	      else if (GET_CODE (SET_SRC (pattern)) == SIGN_EXTEND
		       && GET_CODE (XEXP (SET_SRC (pattern), 0)) == MEM)
		p_mem = &XEXP (SET_SRC (pattern), 0);

	      else if (GET_CODE (SET_SRC (pattern)) == ZERO_EXTEND
		       && GET_CODE (XEXP (SET_SRC (pattern), 0)) == MEM)
		{
		  p_mem = &XEXP (SET_SRC (pattern), 0);
		  unsignedp = TRUE;
		}
	      else
		p_mem = (rtx *)0;

	      if (p_mem)
		{
		  rtx addr = XEXP (*p_mem, 0);

		  if (GET_CODE (addr) == REG && REGNO (addr) == (unsigned) regno)
		    *p_mem = change_address (*p_mem, VOIDmode, *p_ep);

		  else if (GET_CODE (addr) == PLUS
			   && GET_CODE (XEXP (addr, 0)) == REG
			   && REGNO (XEXP (addr, 0)) == (unsigned) regno
			   && GET_CODE (XEXP (addr, 1)) == CONST_INT
			   && ((INTVAL (XEXP (addr, 1)))
			       < ep_memory_offset (GET_MODE (*p_mem),
						   unsignedp))
			   && ((INTVAL (XEXP (addr, 1))) >= 0))
		    *p_mem = change_address (*p_mem, VOIDmode,
					     gen_rtx_PLUS (Pmode,
							   *p_ep,
							   XEXP (addr, 1)));
		}
	    }
	}
    }

  /* Optimize back to back cases of ep <- r1 & r1 <- ep.  */
  insn = prev_nonnote_insn (first_insn);
  if (insn && GET_CODE (insn) == INSN
      && GET_CODE (PATTERN (insn)) == SET
      && SET_DEST (PATTERN (insn)) == *p_ep
      && SET_SRC (PATTERN (insn)) == *p_r1)
    delete_insn (insn);
  else
    emit_insn_before (gen_rtx_SET (Pmode, *p_r1, *p_ep), first_insn);

  emit_insn_before (gen_rtx_SET (Pmode, *p_ep, reg), first_insn);
  emit_insn_before (gen_rtx_SET (Pmode, *p_ep, *p_r1), last_insn);
}


/* TARGET_MACHINE_DEPENDENT_REORG.  On the 850, we use it to implement
   the -mep mode to copy heavily used pointers to ep to use the implicit
   addressing.  */

static void
v850_reorg (void)
{
  struct
  {
    int uses;
    rtx first_insn;
    rtx last_insn;
  }
  regs[FIRST_PSEUDO_REGISTER];

  int i;
  int use_ep = FALSE;
  rtx r1 = NULL_RTX;
  rtx ep = NULL_RTX;
  rtx insn;
  rtx pattern;

  /* If not ep mode, just return now.  */
  if (!TARGET_EP)
    return;

  for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
    {
      regs[i].uses = 0;
      regs[i].first_insn = NULL_RTX;
      regs[i].last_insn = NULL_RTX;
    }

  for (insn = get_insns (); insn != NULL_RTX; insn = NEXT_INSN (insn))
    {
      switch (GET_CODE (insn))
	{
	  /* End of basic block */
	default:
	  if (!use_ep)
	    {
	      int max_uses = -1;
	      int max_regno = -1;

	      for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
		{
		  if (max_uses < regs[i].uses)
		    {
		      max_uses = regs[i].uses;
		      max_regno = i;
		    }
		}

	      if (max_uses > 3)
		substitute_ep_register (regs[max_regno].first_insn,
					regs[max_regno].last_insn,
					max_uses, max_regno, &r1, &ep);
	    }

	  use_ep = FALSE;
	  for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
	    {
	      regs[i].uses = 0;
	      regs[i].first_insn = NULL_RTX;
	      regs[i].last_insn = NULL_RTX;
	    }
	  break;

	case NOTE:
	  break;

	case INSN:
	  pattern = single_set (insn);

	  /* See if there are any memory references we can shorten */
	  if (pattern)
	    {
	      rtx src = SET_SRC (pattern);
	      rtx dest = SET_DEST (pattern);
	      rtx mem;
	      /* Memory operands are signed by default.  */
	      int unsignedp = FALSE;

	      /* We might have (SUBREG (MEM)) here, so just get rid of the
		 subregs to make this code simpler.  */
	      if (GET_CODE (dest) == SUBREG
		  && (GET_CODE (SUBREG_REG (dest)) == MEM
		      || GET_CODE (SUBREG_REG (dest)) == REG))
		alter_subreg (&dest);
	      if (GET_CODE (src) == SUBREG
		  && (GET_CODE (SUBREG_REG (src)) == MEM
		      || GET_CODE (SUBREG_REG (src)) == REG))
		alter_subreg (&src);

	      if (GET_CODE (dest) == MEM && GET_CODE (src) == MEM)
		mem = NULL_RTX;

	      else if (GET_CODE (dest) == MEM)
		mem = dest;

	      else if (GET_CODE (src) == MEM)
		mem = src;

	      else if (GET_CODE (src) == SIGN_EXTEND
		       && GET_CODE (XEXP (src, 0)) == MEM)
		mem = XEXP (src, 0);

	      else if (GET_CODE (src) == ZERO_EXTEND
		       && GET_CODE (XEXP (src, 0)) == MEM)
		{
		  mem = XEXP (src, 0);
		  unsignedp = TRUE;
		}
	      else
		mem = NULL_RTX;

	      if (mem && ep_memory_operand (mem, GET_MODE (mem), unsignedp))
		use_ep = TRUE;

	      else if (!use_ep && mem
		       && GET_MODE_SIZE (GET_MODE (mem)) <= UNITS_PER_WORD)
		{
		  rtx addr = XEXP (mem, 0);
		  int regno = -1;
		  int short_p;

		  if (GET_CODE (addr) == REG)
		    {
		      short_p = TRUE;
		      regno = REGNO (addr);
		    }

		  else if (GET_CODE (addr) == PLUS
			   && GET_CODE (XEXP (addr, 0)) == REG
			   && GET_CODE (XEXP (addr, 1)) == CONST_INT
			   && ((INTVAL (XEXP (addr, 1)))
			       < ep_memory_offset (GET_MODE (mem), unsignedp))
			   && ((INTVAL (XEXP (addr, 1))) >= 0))
		    {
		      short_p = TRUE;
		      regno = REGNO (XEXP (addr, 0));
		    }

		  else
		    short_p = FALSE;

		  if (short_p)
		    {
		      regs[regno].uses++;
		      regs[regno].last_insn = insn;
		      if (!regs[regno].first_insn)
			regs[regno].first_insn = insn;
		    }
		}

	      /* Loading up a register in the basic block zaps any savings
		 for the register */
	      if (GET_CODE (dest) == REG)
		{
		  enum machine_mode mode = GET_MODE (dest);
		  int regno;
		  int endregno;

		  regno = REGNO (dest);
		  endregno = regno + HARD_REGNO_NREGS (regno, mode);

		  if (!use_ep)
		    {
		      /* See if we can use the pointer before this
			 modification.  */
		      int max_uses = -1;
		      int max_regno = -1;

		      for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
			{
			  if (max_uses < regs[i].uses)
			    {
			      max_uses = regs[i].uses;
			      max_regno = i;
			    }
			}

		      if (max_uses > 3
			  && max_regno >= regno
			  && max_regno < endregno)
			{
			  substitute_ep_register (regs[max_regno].first_insn,
						  regs[max_regno].last_insn,
						  max_uses, max_regno, &r1,
						  &ep);

			  /* Since we made a substitution, zap all remembered
			     registers.  */
			  for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
			    {
			      regs[i].uses = 0;
			      regs[i].first_insn = NULL_RTX;
			      regs[i].last_insn = NULL_RTX;
			    }
			}
		    }

		  for (i = regno; i < endregno; i++)
		    {
		      regs[i].uses = 0;
		      regs[i].first_insn = NULL_RTX;
		      regs[i].last_insn = NULL_RTX;
		    }
		}
	    }
	}
    }
}


/* # of registers saved by the interrupt handler.  */
#define INTERRUPT_FIXED_NUM 4

/* # of bytes for registers saved by the interrupt handler.  */
#define INTERRUPT_FIXED_SAVE_SIZE (4 * INTERRUPT_FIXED_NUM)

/* # of registers saved in register parameter area.  */
#define INTERRUPT_REGPARM_NUM 4
/* # of words saved for other registers.  */
#define INTERRUPT_ALL_SAVE_NUM \
  (30 - INTERRUPT_FIXED_NUM + INTERRUPT_REGPARM_NUM)

#define INTERRUPT_ALL_SAVE_SIZE (4 * INTERRUPT_ALL_SAVE_NUM)

int
compute_register_save_size (long * p_reg_saved)
{
  int size = 0;
  int i;
  int interrupt_handler = v850_interrupt_function_p (current_function_decl);
  int call_p = regs_ever_live [LINK_POINTER_REGNUM];
  long reg_saved = 0;

  /* Count the return pointer if we need to save it.  */
  if (current_function_profile && !call_p)
    regs_ever_live [LINK_POINTER_REGNUM] = call_p = 1;
 
  /* Count space for the register saves.  */
  if (interrupt_handler)
    {
      for (i = 0; i <= 31; i++)
	switch (i)
	  {
	  default:
	    if (regs_ever_live[i] || call_p)
	      {
		size += 4;
		reg_saved |= 1L << i;
	      }
	    break;

	    /* We don't save/restore r0 or the stack pointer */
	  case 0:
	  case STACK_POINTER_REGNUM:
	    break;

	    /* For registers with fixed use, we save them, set them to the
	       appropriate value, and then restore them.
	       These registers are handled specially, so don't list them
	       on the list of registers to save in the prologue.  */
	  case 1:		/* temp used to hold ep */
	  case 4:		/* gp */
	  case 10:		/* temp used to call interrupt save/restore */
	  case EP_REGNUM:	/* ep */
	    size += 4;
	    break;
	  }
    }
  else
    {
      /* Find the first register that needs to be saved.  */
      for (i = 0; i <= 31; i++)
	if (regs_ever_live[i] && ((! call_used_regs[i])
				  || i == LINK_POINTER_REGNUM))
	  break;

      /* If it is possible that an out-of-line helper function might be
	 used to generate the prologue for the current function, then we
	 need to cover the possibility that such a helper function will
	 be used, despite the fact that there might be gaps in the list of
	 registers that need to be saved.  To detect this we note that the
	 helper functions always push at least register r29 (provided
	 that the function is not an interrupt handler).  */
	 
      if (TARGET_PROLOG_FUNCTION
          && (i == 2 || ((i >= 20) && (i < 30))))
	{
	  if (i == 2)
	    {
	      size += 4;
	      reg_saved |= 1L << i;

	      i = 20;
	    }

	  /* Helper functions save all registers between the starting
	     register and the last register, regardless of whether they
	     are actually used by the function or not.  */
	  for (; i <= 29; i++)
	    {
	      size += 4;
	      reg_saved |= 1L << i;
	    }

	  if (regs_ever_live [LINK_POINTER_REGNUM])
	    {
	      size += 4;
	      reg_saved |= 1L << LINK_POINTER_REGNUM;
	    }
	}
      else
	{
	  for (; i <= 31; i++)
	    if (regs_ever_live[i] && ((! call_used_regs[i])
				      || i == LINK_POINTER_REGNUM))
	      {
		size += 4;
		reg_saved |= 1L << i;
	      }
	}
    }
  
  if (p_reg_saved)
    *p_reg_saved = reg_saved;

  return size;
}

int
compute_frame_size (int size, long * p_reg_saved)
{
  return (size
	  + compute_register_save_size (p_reg_saved)
	  + current_function_outgoing_args_size);
}


void
expand_prologue (void)
{
  unsigned int i;
  int offset;
  unsigned int size = get_frame_size ();
  unsigned int actual_fsize;
  unsigned int init_stack_alloc = 0;
  rtx save_regs[32];
  rtx save_all;
  unsigned int num_save;
  unsigned int default_stack;
  int code;
  int interrupt_handler = v850_interrupt_function_p (current_function_decl);
  long reg_saved = 0;

  actual_fsize = compute_frame_size (size, &reg_saved);

  /* Save/setup global registers for interrupt functions right now.  */
  if (interrupt_handler)
    {
      if (TARGET_V850E && ! TARGET_DISABLE_CALLT)
	emit_insn (gen_callt_save_interrupt ());
      else
	emit_insn (gen_save_interrupt ());

      actual_fsize -= INTERRUPT_FIXED_SAVE_SIZE;
      
      if (((1L << LINK_POINTER_REGNUM) & reg_saved) != 0)
	actual_fsize -= INTERRUPT_ALL_SAVE_SIZE;
    }

  /* Save arg registers to the stack if necessary.  */
  else if (current_function_args_info.anonymous_args)
    {
      if (TARGET_PROLOG_FUNCTION && TARGET_V850E && !TARGET_DISABLE_CALLT)
	emit_insn (gen_save_r6_r9_v850e ());
      else if (TARGET_PROLOG_FUNCTION && ! TARGET_LONG_CALLS)
	emit_insn (gen_save_r6_r9 ());
      else
	{
	  offset = 0;
	  for (i = 6; i < 10; i++)
	    {
	      emit_move_insn (gen_rtx_MEM (SImode,
					   plus_constant (stack_pointer_rtx,
							  offset)),
			      gen_rtx_REG (SImode, i));
	      offset += 4;
	    }
	}
    }

  /* Identify all of the saved registers.  */
  num_save = 0;
  default_stack = 0;
  for (i = 1; i < 31; i++)
    {
      if (((1L << i) & reg_saved) != 0)
	save_regs[num_save++] = gen_rtx_REG (Pmode, i);
    }

  /* If the return pointer is saved, the helper functions also allocate
     16 bytes of stack for arguments to be saved in.  */
  if (((1L << LINK_POINTER_REGNUM) & reg_saved) != 0)
    {
      save_regs[num_save++] = gen_rtx_REG (Pmode, LINK_POINTER_REGNUM);
      default_stack = 16;
    }

  /* See if we have an insn that allocates stack space and saves the particular
     registers we want to.  */
  save_all = NULL_RTX;
  if (TARGET_PROLOG_FUNCTION && num_save > 0 && actual_fsize >= default_stack)
    {
      int alloc_stack = (4 * num_save) + default_stack;
      int unalloc_stack = actual_fsize - alloc_stack;
      int save_func_len = 4;
      int save_normal_len;

      if (unalloc_stack)
	save_func_len += CONST_OK_FOR_J (unalloc_stack) ? 2 : 4;

      /* see if we would have used ep to save the stack */
      if (TARGET_EP && num_save > 3 && (unsigned)actual_fsize < 255)
	save_normal_len = (3 * 2) + (2 * num_save);
      else
	save_normal_len = 4 * num_save;

      save_normal_len += CONST_OK_FOR_J (actual_fsize) ? 2 : 4;

      /* Don't bother checking if we don't actually save any space.
	 This happens for instance if one register is saved and additional
	 stack space is allocated.  */
      if (save_func_len < save_normal_len)
	{
	  save_all = gen_rtx_PARALLEL
	    (VOIDmode,
	     rtvec_alloc (num_save + 1
			  + (TARGET_V850 ? (TARGET_LONG_CALLS ? 2 : 1) : 0)));

	  XVECEXP (save_all, 0, 0)
	    = gen_rtx_SET (VOIDmode,
			   stack_pointer_rtx,
			   plus_constant (stack_pointer_rtx, -alloc_stack));

	  offset = - default_stack;
	  for (i = 0; i < num_save; i++)
	    {
	      XVECEXP (save_all, 0, i+1)
		= gen_rtx_SET (VOIDmode,
			       gen_rtx_MEM (Pmode,
					    plus_constant (stack_pointer_rtx,
							   offset)),
			       save_regs[i]);
	      offset -= 4;
	    }

	  if (TARGET_V850)
	    {
	      XVECEXP (save_all, 0, num_save + 1)
		= gen_rtx_CLOBBER (VOIDmode, gen_rtx_REG (Pmode, 10));

	      if (TARGET_LONG_CALLS)
		XVECEXP (save_all, 0, num_save + 2)
		  = gen_rtx_CLOBBER (VOIDmode, gen_rtx_REG (Pmode, 11));
	    }

	  code = recog (save_all, NULL_RTX, NULL);
	  if (code >= 0)
	    {
	      rtx insn = emit_insn (save_all);
	      INSN_CODE (insn) = code;
	      actual_fsize -= alloc_stack;
	      
	      if (TARGET_DEBUG)
		fprintf (stderr, "\
Saved %d bytes via prologue function (%d vs. %d) for function %s\n",
			 save_normal_len - save_func_len,
			 save_normal_len, save_func_len,
			 IDENTIFIER_POINTER (DECL_NAME (current_function_decl)));
	    }
	  else
	    save_all = NULL_RTX;
	}
    }

  /* If no prolog save function is available, store the registers the old
     fashioned way (one by one).  */
  if (!save_all)
    {
      /* Special case interrupt functions that save all registers for a call.  */
      if (interrupt_handler && ((1L << LINK_POINTER_REGNUM) & reg_saved) != 0)
	{
	  if (TARGET_V850E && ! TARGET_DISABLE_CALLT)
	    emit_insn (gen_callt_save_all_interrupt ());
	  else
	    emit_insn (gen_save_all_interrupt ());
	}
      else
	{
	  /* If the stack is too big, allocate it in chunks so we can do the
	     register saves.  We use the register save size so we use the ep
	     register.  */
	  if (actual_fsize && !CONST_OK_FOR_K (-actual_fsize))
	    init_stack_alloc = compute_register_save_size (NULL);
	  else
	    init_stack_alloc = actual_fsize;
	      
	  /* Save registers at the beginning of the stack frame.  */
	  offset = init_stack_alloc - 4;
	  
	  if (init_stack_alloc)
	    emit_insn (gen_addsi3 (stack_pointer_rtx,
				   stack_pointer_rtx,
				   GEN_INT (-init_stack_alloc)));
	  
	  /* Save the return pointer first.  */
	  if (num_save > 0 && REGNO (save_regs[num_save-1]) == LINK_POINTER_REGNUM)
	    {
	      emit_move_insn (gen_rtx_MEM (SImode,
					   plus_constant (stack_pointer_rtx,
							  offset)),
			      save_regs[--num_save]);
	      offset -= 4;
	    }
	  
	  for (i = 0; i < num_save; i++)
	    {
	      emit_move_insn (gen_rtx_MEM (SImode,
					   plus_constant (stack_pointer_rtx,
							  offset)),
			      save_regs[i]);
	      offset -= 4;
	    }
	}
    }

  /* Allocate the rest of the stack that was not allocated above (either it is
     > 32K or we just called a function to save the registers and needed more
     stack.  */
  if (actual_fsize > init_stack_alloc)
    {
      int diff = actual_fsize - init_stack_alloc;
      if (CONST_OK_FOR_K (diff))
	emit_insn (gen_addsi3 (stack_pointer_rtx,
			       stack_pointer_rtx,
			       GEN_INT (-diff)));
      else
	{
	  rtx reg = gen_rtx_REG (Pmode, 12);
	  emit_move_insn (reg, GEN_INT (-diff));
	  emit_insn (gen_addsi3 (stack_pointer_rtx, stack_pointer_rtx, reg));
	}
    }

  /* If we need a frame pointer, set it up now.  */
  if (frame_pointer_needed)
    emit_move_insn (hard_frame_pointer_rtx, stack_pointer_rtx);
}


void
expand_epilogue (void)
{
  unsigned int i;
  int offset;
  unsigned int size = get_frame_size ();
  long reg_saved = 0;
  unsigned int actual_fsize = compute_frame_size (size, &reg_saved);
  unsigned int init_stack_free = 0;
  rtx restore_regs[32];
  rtx restore_all;
  unsigned int num_restore;
  unsigned int default_stack;
  int code;
  int interrupt_handler = v850_interrupt_function_p (current_function_decl);

  /* Eliminate the initial stack stored by interrupt functions.  */
  if (interrupt_handler)
    {
      actual_fsize -= INTERRUPT_FIXED_SAVE_SIZE;
      if (((1L << LINK_POINTER_REGNUM) & reg_saved) != 0)
	actual_fsize -= INTERRUPT_ALL_SAVE_SIZE;
    }

  /* Cut off any dynamic stack created.  */
  if (frame_pointer_needed)
    emit_move_insn (stack_pointer_rtx, hard_frame_pointer_rtx);

  /* Identify all of the saved registers.  */
  num_restore = 0;
  default_stack = 0;
  for (i = 1; i < 31; i++)
    {
      if (((1L << i) & reg_saved) != 0)
	restore_regs[num_restore++] = gen_rtx_REG (Pmode, i);
    }

  /* If the return pointer is saved, the helper functions also allocate
     16 bytes of stack for arguments to be saved in.  */
  if (((1L << LINK_POINTER_REGNUM) & reg_saved) != 0)
    {
      restore_regs[num_restore++] = gen_rtx_REG (Pmode, LINK_POINTER_REGNUM);
      default_stack = 16;
    }

  /* See if we have an insn that restores the particular registers we
     want to.  */
  restore_all = NULL_RTX;
  
  if (TARGET_PROLOG_FUNCTION
      && num_restore > 0
      && actual_fsize >= default_stack
      && !interrupt_handler)
    {
      int alloc_stack = (4 * num_restore) + default_stack;
      int unalloc_stack = actual_fsize - alloc_stack;
      int restore_func_len = 4;
      int restore_normal_len;

      if (unalloc_stack)
	restore_func_len += CONST_OK_FOR_J (unalloc_stack) ? 2 : 4;

      /* See if we would have used ep to restore the registers.  */
      if (TARGET_EP && num_restore > 3 && (unsigned)actual_fsize < 255)
	restore_normal_len = (3 * 2) + (2 * num_restore);
      else
	restore_normal_len = 4 * num_restore;

      restore_normal_len += (CONST_OK_FOR_J (actual_fsize) ? 2 : 4) + 2;

      /* Don't bother checking if we don't actually save any space.  */
      if (restore_func_len < restore_normal_len)
	{
	  restore_all = gen_rtx_PARALLEL (VOIDmode,
					  rtvec_alloc (num_restore + 2));
	  XVECEXP (restore_all, 0, 0) = gen_rtx_RETURN (VOIDmode);
	  XVECEXP (restore_all, 0, 1)
	    = gen_rtx_SET (VOIDmode, stack_pointer_rtx,
			    gen_rtx_PLUS (Pmode,
					  stack_pointer_rtx,
					  GEN_INT (alloc_stack)));

	  offset = alloc_stack - 4;
	  for (i = 0; i < num_restore; i++)
	    {
	      XVECEXP (restore_all, 0, i+2)
		= gen_rtx_SET (VOIDmode,
			       restore_regs[i],
			       gen_rtx_MEM (Pmode,
					    plus_constant (stack_pointer_rtx,
							   offset)));
	      offset -= 4;
	    }

	  code = recog (restore_all, NULL_RTX, NULL);
	  
	  if (code >= 0)
	    {
	      rtx insn;

	      actual_fsize -= alloc_stack;
	      if (actual_fsize)
		{
		  if (CONST_OK_FOR_K (actual_fsize))
		    emit_insn (gen_addsi3 (stack_pointer_rtx,
					   stack_pointer_rtx,
					   GEN_INT (actual_fsize)));
		  else
		    {
		      rtx reg = gen_rtx_REG (Pmode, 12);
		      emit_move_insn (reg, GEN_INT (actual_fsize));
		      emit_insn (gen_addsi3 (stack_pointer_rtx,
					     stack_pointer_rtx,
					     reg));
		    }
		}

	      insn = emit_jump_insn (restore_all);
	      INSN_CODE (insn) = code;

	      if (TARGET_DEBUG)
		fprintf (stderr, "\
Saved %d bytes via epilogue function (%d vs. %d) in function %s\n",
			 restore_normal_len - restore_func_len,
			 restore_normal_len, restore_func_len,
			 IDENTIFIER_POINTER (DECL_NAME (current_function_decl)));
	    }
	  else
	    restore_all = NULL_RTX;
	}
    }

  /* If no epilog save function is available, restore the registers the
     old fashioned way (one by one).  */
  if (!restore_all)
    {
      /* If the stack is large, we need to cut it down in 2 pieces.  */
      if (actual_fsize && !CONST_OK_FOR_K (-actual_fsize))
	init_stack_free = 4 * num_restore;
      else
	init_stack_free = actual_fsize;

      /* Deallocate the rest of the stack if it is > 32K.  */
      if (actual_fsize > init_stack_free)
	{
	  int diff;

	  diff = actual_fsize - ((interrupt_handler) ? 0 : init_stack_free);

	  if (CONST_OK_FOR_K (diff))
	    emit_insn (gen_addsi3 (stack_pointer_rtx,
				   stack_pointer_rtx,
				   GEN_INT (diff)));
	  else
	    {
	      rtx reg = gen_rtx_REG (Pmode, 12);
	      emit_move_insn (reg, GEN_INT (diff));
	      emit_insn (gen_addsi3 (stack_pointer_rtx,
				     stack_pointer_rtx,
				     reg));
	    }
	}

      /* Special case interrupt functions that save all registers
	 for a call.  */
      if (interrupt_handler && ((1L << LINK_POINTER_REGNUM) & reg_saved) != 0)
	{
	  if (TARGET_V850E && ! TARGET_DISABLE_CALLT)
	    emit_insn (gen_callt_restore_all_interrupt ());
	  else
	    emit_insn (gen_restore_all_interrupt ());
	}
      else
	{
	  /* Restore registers from the beginning of the stack frame.  */
	  offset = init_stack_free - 4;

	  /* Restore the return pointer first.  */
	  if (num_restore > 0
	      && REGNO (restore_regs [num_restore - 1]) == LINK_POINTER_REGNUM)
	    {
	      emit_move_insn (restore_regs[--num_restore],
			      gen_rtx_MEM (SImode,
					   plus_constant (stack_pointer_rtx,
							  offset)));
	      offset -= 4;
	    }

	  for (i = 0; i < num_restore; i++)
	    {
	      emit_move_insn (restore_regs[i],
			      gen_rtx_MEM (SImode,
					   plus_constant (stack_pointer_rtx,
							  offset)));

	      emit_insn (gen_rtx_USE (VOIDmode, restore_regs[i]));
	      offset -= 4;
	    }

	  /* Cut back the remainder of the stack.  */
	  if (init_stack_free)
	    emit_insn (gen_addsi3 (stack_pointer_rtx,
				   stack_pointer_rtx,
				   GEN_INT (init_stack_free)));
	}

      /* And return or use reti for interrupt handlers.  */
      if (interrupt_handler)
        {
          if (TARGET_V850E && ! TARGET_DISABLE_CALLT)
            emit_insn (gen_callt_return_interrupt ());
          else
            emit_jump_insn (gen_return_interrupt ());
	 }
      else if (actual_fsize)
	emit_jump_insn (gen_return_internal ());
      else
	emit_jump_insn (gen_return ());
    }

  v850_interrupt_cache_p = FALSE;
  v850_interrupt_p = FALSE;
}


/* Update the condition code from the insn.  */

void
notice_update_cc (rtx body, rtx insn)
{
  switch (get_attr_cc (insn))
    {
    case CC_NONE:
      /* Insn does not affect CC at all.  */
      break;

    case CC_NONE_0HIT:
      /* Insn does not change CC, but the 0'th operand has been changed.  */
      if (cc_status.value1 != 0
	  && reg_overlap_mentioned_p (recog_data.operand[0], cc_status.value1))
	cc_status.value1 = 0;
      break;

    case CC_SET_ZN:
      /* Insn sets the Z,N flags of CC to recog_data.operand[0].
	 V,C is in an unusable state.  */
      CC_STATUS_INIT;
      cc_status.flags |= CC_OVERFLOW_UNUSABLE | CC_NO_CARRY;
      cc_status.value1 = recog_data.operand[0];
      break;

    case CC_SET_ZNV:
      /* Insn sets the Z,N,V flags of CC to recog_data.operand[0].
	 C is in an unusable state.  */
      CC_STATUS_INIT;
      cc_status.flags |= CC_NO_CARRY;
      cc_status.value1 = recog_data.operand[0];
      break;

    case CC_COMPARE:
      /* The insn is a compare instruction.  */
      CC_STATUS_INIT;
      cc_status.value1 = SET_SRC (body);
      break;

    case CC_CLOBBER:
      /* Insn doesn't leave CC in a usable state.  */
      CC_STATUS_INIT;
      break;
    }
}

/* Retrieve the data area that has been chosen for the given decl.  */

v850_data_area
v850_get_data_area (tree decl)
{
  if (lookup_attribute ("sda", DECL_ATTRIBUTES (decl)) != NULL_TREE)
    return DATA_AREA_SDA;
  
  if (lookup_attribute ("tda", DECL_ATTRIBUTES (decl)) != NULL_TREE)
    return DATA_AREA_TDA;
  
  if (lookup_attribute ("zda", DECL_ATTRIBUTES (decl)) != NULL_TREE)
    return DATA_AREA_ZDA;

  return DATA_AREA_NORMAL;
}

/* Store the indicated data area in the decl's attributes.  */

static void
v850_set_data_area (tree decl, v850_data_area data_area)
{
  tree name;
  
  switch (data_area)
    {
    case DATA_AREA_SDA: name = get_identifier ("sda"); break;
    case DATA_AREA_TDA: name = get_identifier ("tda"); break;
    case DATA_AREA_ZDA: name = get_identifier ("zda"); break;
    default:
      return;
    }

  DECL_ATTRIBUTES (decl) = tree_cons
    (name, NULL, DECL_ATTRIBUTES (decl));
}

const struct attribute_spec v850_attribute_table[] =
{
  /* { name, min_len, max_len, decl_req, type_req, fn_type_req, handler } */
  { "interrupt_handler", 0, 0, true,  false, false, v850_handle_interrupt_attribute },
  { "interrupt",         0, 0, true,  false, false, v850_handle_interrupt_attribute },
  { "sda",               0, 0, true,  false, false, v850_handle_data_area_attribute },
  { "tda",               0, 0, true,  false, false, v850_handle_data_area_attribute },
  { "zda",               0, 0, true,  false, false, v850_handle_data_area_attribute },
  { NULL,                0, 0, false, false, false, NULL }
};

/* Handle an "interrupt" attribute; arguments as in
   struct attribute_spec.handler.  */
static tree
v850_handle_interrupt_attribute (tree * node,
                                 tree name,
                                 tree args ATTRIBUTE_UNUSED,
                                 int flags ATTRIBUTE_UNUSED,
                                 bool * no_add_attrs)
{
  if (TREE_CODE (*node) != FUNCTION_DECL)
    {
      warning (OPT_Wattributes, "%qs attribute only applies to functions",
	       IDENTIFIER_POINTER (name));
      *no_add_attrs = true;
    }

  return NULL_TREE;
}

/* Handle a "sda", "tda" or "zda" attribute; arguments as in
   struct attribute_spec.handler.  */
static tree
v850_handle_data_area_attribute (tree* node,
                                 tree name,
                                 tree args ATTRIBUTE_UNUSED,
                                 int flags ATTRIBUTE_UNUSED,
                                 bool * no_add_attrs)
{
  v850_data_area data_area;
  v850_data_area area;
  tree decl = *node;

  /* Implement data area attribute.  */
  if (is_attribute_p ("sda", name))
    data_area = DATA_AREA_SDA;
  else if (is_attribute_p ("tda", name))
    data_area = DATA_AREA_TDA;
  else if (is_attribute_p ("zda", name))
    data_area = DATA_AREA_ZDA;
  else
    gcc_unreachable ();
  
  switch (TREE_CODE (decl))
    {
    case VAR_DECL:
      if (current_function_decl != NULL_TREE)
	{
          error ("%Jdata area attributes cannot be specified for "
                 "local variables", decl);
	  *no_add_attrs = true;
	}

      /* Drop through.  */

    case FUNCTION_DECL:
      area = v850_get_data_area (decl);
      if (area != DATA_AREA_NORMAL && data_area != area)
	{
	  error ("data area of %q+D conflicts with previous declaration",
                 decl);
	  *no_add_attrs = true;
	}
      break;
      
    default:
      break;
    }

  return NULL_TREE;
}


/* Return nonzero if FUNC is an interrupt function as specified
   by the "interrupt" attribute.  */

int
v850_interrupt_function_p (tree func)
{
  tree a;
  int ret = 0;

  if (v850_interrupt_cache_p)
    return v850_interrupt_p;

  if (TREE_CODE (func) != FUNCTION_DECL)
    return 0;

  a = lookup_attribute ("interrupt_handler", DECL_ATTRIBUTES (func));
  if (a != NULL_TREE)
    ret = 1;

  else
    {
      a = lookup_attribute ("interrupt", DECL_ATTRIBUTES (func));
      ret = a != NULL_TREE;
    }

  /* Its not safe to trust global variables until after function inlining has
     been done.  */
  if (reload_completed | reload_in_progress)
    v850_interrupt_p = ret;

  return ret;
}


static void
v850_encode_data_area (tree decl, rtx symbol)
{
  int flags;

  /* Map explicit sections into the appropriate attribute */
  if (v850_get_data_area (decl) == DATA_AREA_NORMAL)
    {
      if (DECL_SECTION_NAME (decl))
	{
	  const char *name = TREE_STRING_POINTER (DECL_SECTION_NAME (decl));
	  
	  if (streq (name, ".zdata") || streq (name, ".zbss"))
	    v850_set_data_area (decl, DATA_AREA_ZDA);

	  else if (streq (name, ".sdata") || streq (name, ".sbss"))
	    v850_set_data_area (decl, DATA_AREA_SDA);

	  else if (streq (name, ".tdata"))
	    v850_set_data_area (decl, DATA_AREA_TDA);
	}

      /* If no attribute, support -m{zda,sda,tda}=n */
      else
	{
	  int size = int_size_in_bytes (TREE_TYPE (decl));
	  if (size <= 0)
	    ;

	  else if (size <= small_memory [(int) SMALL_MEMORY_TDA].max)
	    v850_set_data_area (decl, DATA_AREA_TDA);

	  else if (size <= small_memory [(int) SMALL_MEMORY_SDA].max)
	    v850_set_data_area (decl, DATA_AREA_SDA);

	  else if (size <= small_memory [(int) SMALL_MEMORY_ZDA].max)
	    v850_set_data_area (decl, DATA_AREA_ZDA);
	}
      
      if (v850_get_data_area (decl) == DATA_AREA_NORMAL)
	return;
    }

  flags = SYMBOL_REF_FLAGS (symbol);
  switch (v850_get_data_area (decl))
    {
    case DATA_AREA_ZDA: flags |= SYMBOL_FLAG_ZDA; break;
    case DATA_AREA_TDA: flags |= SYMBOL_FLAG_TDA; break;
    case DATA_AREA_SDA: flags |= SYMBOL_FLAG_SDA; break;
    default: gcc_unreachable ();
    }
  SYMBOL_REF_FLAGS (symbol) = flags;
}

static void
v850_encode_section_info (tree decl, rtx rtl, int first)
{
  default_encode_section_info (decl, rtl, first);

  if (TREE_CODE (decl) == VAR_DECL
      && (TREE_STATIC (decl) || DECL_EXTERNAL (decl)))
    v850_encode_data_area (decl, XEXP (rtl, 0));
}

/* Construct a JR instruction to a routine that will perform the equivalent of
   the RTL passed in as an argument.  This RTL is a function epilogue that
   pops registers off the stack and possibly releases some extra stack space
   as well.  The code has already verified that the RTL matches these
   requirements.  */
char *
construct_restore_jr (rtx op)
{
  int count = XVECLEN (op, 0);
  int stack_bytes;
  unsigned long int mask;
  unsigned long int first;
  unsigned long int last;
  int i;
  static char buff [100]; /* XXX */
  
  if (count <= 2)
    {
      error ("bogus JR construction: %d", count);
      return NULL;
    }

  /* Work out how many bytes to pop off the stack before retrieving
     registers.  */
  gcc_assert (GET_CODE (XVECEXP (op, 0, 1)) == SET);
  gcc_assert (GET_CODE (SET_SRC (XVECEXP (op, 0, 1))) == PLUS);
  gcc_assert (GET_CODE (XEXP (SET_SRC (XVECEXP (op, 0, 1)), 1)) == CONST_INT);
    
  stack_bytes = INTVAL (XEXP (SET_SRC (XVECEXP (op, 0, 1)), 1));

  /* Each pop will remove 4 bytes from the stack....  */
  stack_bytes -= (count - 2) * 4;

  /* Make sure that the amount we are popping either 0 or 16 bytes.  */
  if (stack_bytes != 0 && stack_bytes != 16)
    {
      error ("bad amount of stack space removal: %d", stack_bytes);
      return NULL;
    }

  /* Now compute the bit mask of registers to push.  */
  mask = 0;
  for (i = 2; i < count; i++)
    {
      rtx vector_element = XVECEXP (op, 0, i);
      
      gcc_assert (GET_CODE (vector_element) == SET);
      gcc_assert (GET_CODE (SET_DEST (vector_element)) == REG);
      gcc_assert (register_is_ok_for_epilogue (SET_DEST (vector_element),
					       SImode));
      
      mask |= 1 << REGNO (SET_DEST (vector_element));
    }

  /* Scan for the first register to pop.  */
  for (first = 0; first < 32; first++)
    {
      if (mask & (1 << first))
	break;
    }

  gcc_assert (first < 32);

  /* Discover the last register to pop.  */
  if (mask & (1 << LINK_POINTER_REGNUM))
    {
      gcc_assert (stack_bytes == 16);
      
      last = LINK_POINTER_REGNUM;
    }
  else
    {
      gcc_assert (!stack_bytes);
      gcc_assert (mask & (1 << 29));
      
      last = 29;
    }

  /* Note, it is possible to have gaps in the register mask.
     We ignore this here, and generate a JR anyway.  We will
     be popping more registers than is strictly necessary, but
     it does save code space.  */
  
  if (TARGET_LONG_CALLS)
    {
      char name[40];
      
      if (first == last)
	sprintf (name, "__return_%s", reg_names [first]);
      else
	sprintf (name, "__return_%s_%s", reg_names [first], reg_names [last]);
      
      sprintf (buff, "movhi hi(%s), r0, r6\n\tmovea lo(%s), r6, r6\n\tjmp r6",
	       name, name);
    }
  else
    {
      if (first == last)
	sprintf (buff, "jr __return_%s", reg_names [first]);
      else
	sprintf (buff, "jr __return_%s_%s", reg_names [first], reg_names [last]);
    }
  
  return buff;
}


/* Construct a JARL instruction to a routine that will perform the equivalent
   of the RTL passed as a parameter.  This RTL is a function prologue that
   saves some of the registers r20 - r31 onto the stack, and possibly acquires
   some stack space as well.  The code has already verified that the RTL
   matches these requirements.  */
char *
construct_save_jarl (rtx op)
{
  int count = XVECLEN (op, 0);
  int stack_bytes;
  unsigned long int mask;
  unsigned long int first;
  unsigned long int last;
  int i;
  static char buff [100]; /* XXX */
  
  if (count <= 2)
    {
      error ("bogus JARL construction: %d\n", count);
      return NULL;
    }

  /* Paranoia.  */
  gcc_assert (GET_CODE (XVECEXP (op, 0, 0)) == SET);
  gcc_assert (GET_CODE (SET_SRC (XVECEXP (op, 0, 0))) == PLUS);
  gcc_assert (GET_CODE (XEXP (SET_SRC (XVECEXP (op, 0, 0)), 0)) == REG);
  gcc_assert (GET_CODE (XEXP (SET_SRC (XVECEXP (op, 0, 0)), 1)) == CONST_INT);
    
  /* Work out how many bytes to push onto the stack after storing the
     registers.  */
  stack_bytes = INTVAL (XEXP (SET_SRC (XVECEXP (op, 0, 0)), 1));

  /* Each push will put 4 bytes from the stack....  */
  stack_bytes += (count - (TARGET_LONG_CALLS ? 3 : 2)) * 4;

  /* Make sure that the amount we are popping either 0 or 16 bytes.  */
  if (stack_bytes != 0 && stack_bytes != -16)
    {
      error ("bad amount of stack space removal: %d", stack_bytes);
      return NULL;
    }

  /* Now compute the bit mask of registers to push.  */
  mask = 0;
  for (i = 1; i < count - (TARGET_LONG_CALLS ? 2 : 1); i++)
    {
      rtx vector_element = XVECEXP (op, 0, i);
      
      gcc_assert (GET_CODE (vector_element) == SET);
      gcc_assert (GET_CODE (SET_SRC (vector_element)) == REG);
      gcc_assert (register_is_ok_for_epilogue (SET_SRC (vector_element),
					       SImode));
      
      mask |= 1 << REGNO (SET_SRC (vector_element));
    }

  /* Scan for the first register to push.  */  
  for (first = 0; first < 32; first++)
    {
      if (mask & (1 << first))
	break;
    }

  gcc_assert (first < 32);

  /* Discover the last register to push.  */
  if (mask & (1 << LINK_POINTER_REGNUM))
    {
      gcc_assert (stack_bytes == -16);
      
      last = LINK_POINTER_REGNUM;
    }
  else
    {
      gcc_assert (!stack_bytes);
      gcc_assert (mask & (1 << 29));
      
      last = 29;
    }

  /* Note, it is possible to have gaps in the register mask.
     We ignore this here, and generate a JARL anyway.  We will
     be pushing more registers than is strictly necessary, but
     it does save code space.  */
  
  if (TARGET_LONG_CALLS)
    {
      char name[40];
      
      if (first == last)
	sprintf (name, "__save_%s", reg_names [first]);
      else
	sprintf (name, "__save_%s_%s", reg_names [first], reg_names [last]);
      
      sprintf (buff, "movhi hi(%s), r0, r11\n\tmovea lo(%s), r11, r11\n\tjarl .+4, r10\n\tadd 4, r10\n\tjmp r11",
	       name, name);
    }
  else
    {
      if (first == last)
	sprintf (buff, "jarl __save_%s, r10", reg_names [first]);
      else
	sprintf (buff, "jarl __save_%s_%s, r10", reg_names [first],
		 reg_names [last]);
    }

  return buff;
}

extern tree last_assemble_variable_decl;
extern int size_directive_output;

/* A version of asm_output_aligned_bss() that copes with the special
   data areas of the v850.  */
void
v850_output_aligned_bss (FILE * file,
                         tree decl,
                         const char * name,
                         unsigned HOST_WIDE_INT size,
                         int align)
{
  switch (v850_get_data_area (decl))
    {
    case DATA_AREA_ZDA:
      switch_to_section (zbss_section);
      break;

    case DATA_AREA_SDA:
      switch_to_section (sbss_section);
      break;

    case DATA_AREA_TDA:
      switch_to_section (tdata_section);
      
    default:
      switch_to_section (bss_section);
      break;
    }
  
  ASM_OUTPUT_ALIGN (file, floor_log2 (align / BITS_PER_UNIT));
#ifdef ASM_DECLARE_OBJECT_NAME
  last_assemble_variable_decl = decl;
  ASM_DECLARE_OBJECT_NAME (file, name, decl);
#else
  /* Standard thing is just output label for the object.  */
  ASM_OUTPUT_LABEL (file, name);
#endif /* ASM_DECLARE_OBJECT_NAME */
  ASM_OUTPUT_SKIP (file, size ? size : 1);
}

/* Called via the macro ASM_OUTPUT_DECL_COMMON */
void
v850_output_common (FILE * file,
                    tree decl,
                    const char * name,
                    int size,
                    int align)
{
  if (decl == NULL_TREE)
    {
      fprintf (file, "%s", COMMON_ASM_OP);
    }
  else
    {
      switch (v850_get_data_area (decl))
	{
	case DATA_AREA_ZDA:
	  fprintf (file, "%s", ZCOMMON_ASM_OP);
	  break;

	case DATA_AREA_SDA:
	  fprintf (file, "%s", SCOMMON_ASM_OP);
	  break;

	case DATA_AREA_TDA:
	  fprintf (file, "%s", TCOMMON_ASM_OP);
	  break;
      
	default:
	  fprintf (file, "%s", COMMON_ASM_OP);
	  break;
	}
    }
  
  assemble_name (file, name);
  fprintf (file, ",%u,%u\n", size, align / BITS_PER_UNIT);
}

/* Called via the macro ASM_OUTPUT_DECL_LOCAL */
void
v850_output_local (FILE * file,
                   tree decl,
                   const char * name,
                   int size,
                   int align)
{
  fprintf (file, "%s", LOCAL_ASM_OP);
  assemble_name (file, name);
  fprintf (file, "\n");
  
  ASM_OUTPUT_ALIGNED_DECL_COMMON (file, decl, name, size, align);
}

/* Add data area to the given declaration if a ghs data area pragma is
   currently in effect (#pragma ghs startXXX/endXXX).  */
static void
v850_insert_attributes (tree decl, tree * attr_ptr ATTRIBUTE_UNUSED )
{
  if (data_area_stack
      && data_area_stack->data_area
      && current_function_decl == NULL_TREE
      && (TREE_CODE (decl) == VAR_DECL || TREE_CODE (decl) == CONST_DECL)
      && v850_get_data_area (decl) == DATA_AREA_NORMAL)
    v850_set_data_area (decl, data_area_stack->data_area);

  /* Initialize the default names of the v850 specific sections,
     if this has not been done before.  */
  
  if (GHS_default_section_names [(int) GHS_SECTION_KIND_SDATA] == NULL)
    {
      GHS_default_section_names [(int) GHS_SECTION_KIND_SDATA]
	= build_string (sizeof (".sdata")-1, ".sdata");

      GHS_default_section_names [(int) GHS_SECTION_KIND_ROSDATA]
	= build_string (sizeof (".rosdata")-1, ".rosdata");

      GHS_default_section_names [(int) GHS_SECTION_KIND_TDATA]
	= build_string (sizeof (".tdata")-1, ".tdata");
      
      GHS_default_section_names [(int) GHS_SECTION_KIND_ZDATA]
	= build_string (sizeof (".zdata")-1, ".zdata");

      GHS_default_section_names [(int) GHS_SECTION_KIND_ROZDATA]
	= build_string (sizeof (".rozdata")-1, ".rozdata");
    }
  
  if (current_function_decl == NULL_TREE
      && (TREE_CODE (decl) == VAR_DECL
	  || TREE_CODE (decl) == CONST_DECL
	  || TREE_CODE (decl) == FUNCTION_DECL)
      && (!DECL_EXTERNAL (decl) || DECL_INITIAL (decl))
      && !DECL_SECTION_NAME (decl))
    {
      enum GHS_section_kind kind = GHS_SECTION_KIND_DEFAULT;
      tree chosen_section;

      if (TREE_CODE (decl) == FUNCTION_DECL)
	kind = GHS_SECTION_KIND_TEXT;
      else
	{
	  /* First choose a section kind based on the data area of the decl.  */
	  switch (v850_get_data_area (decl))
	    {
	    default:
	      gcc_unreachable ();
	      
	    case DATA_AREA_SDA:
	      kind = ((TREE_READONLY (decl))
		      ? GHS_SECTION_KIND_ROSDATA
		      : GHS_SECTION_KIND_SDATA);
	      break;
	      
	    case DATA_AREA_TDA:
	      kind = GHS_SECTION_KIND_TDATA;
	      break;
	      
	    case DATA_AREA_ZDA:
	      kind = ((TREE_READONLY (decl))
		      ? GHS_SECTION_KIND_ROZDATA
		      : GHS_SECTION_KIND_ZDATA);
	      break;
	      
	    case DATA_AREA_NORMAL:		 /* default data area */
	      if (TREE_READONLY (decl))
		kind = GHS_SECTION_KIND_RODATA;
	      else if (DECL_INITIAL (decl))
		kind = GHS_SECTION_KIND_DATA;
	      else
		kind = GHS_SECTION_KIND_BSS;
	    }
	}

      /* Now, if the section kind has been explicitly renamed,
         then attach a section attribute.  */
      chosen_section = GHS_current_section_names [(int) kind];

      /* Otherwise, if this kind of section needs an explicit section
         attribute, then also attach one.  */
      if (chosen_section == NULL)
        chosen_section = GHS_default_section_names [(int) kind];

      if (chosen_section)
	{
	  /* Only set the section name if specified by a pragma, because
	     otherwise it will force those variables to get allocated storage
	     in this module, rather than by the linker.  */
	  DECL_SECTION_NAME (decl) = chosen_section;
	}
    }
}

/* Construct a DISPOSE instruction that is the equivalent of
   the given RTX.  We have already verified that this should
   be possible.  */

char *
construct_dispose_instruction (rtx op)
{
  int                count = XVECLEN (op, 0);
  int                stack_bytes;
  unsigned long int  mask;
  int		     i;
  static char        buff[ 100 ]; /* XXX */
  int                use_callt = 0;
  
  if (count <= 2)
    {
      error ("bogus DISPOSE construction: %d", count);
      return NULL;
    }

  /* Work out how many bytes to pop off the
     stack before retrieving registers.  */
  gcc_assert (GET_CODE (XVECEXP (op, 0, 1)) == SET);
  gcc_assert (GET_CODE (SET_SRC (XVECEXP (op, 0, 1))) == PLUS);
  gcc_assert (GET_CODE (XEXP (SET_SRC (XVECEXP (op, 0, 1)), 1)) == CONST_INT);
    
  stack_bytes = INTVAL (XEXP (SET_SRC (XVECEXP (op, 0, 1)), 1));

  /* Each pop will remove 4 bytes from the stack....  */
  stack_bytes -= (count - 2) * 4;

  /* Make sure that the amount we are popping
     will fit into the DISPOSE instruction.  */
  if (stack_bytes > 128)
    {
      error ("too much stack space to dispose of: %d", stack_bytes);
      return NULL;
    }

  /* Now compute the bit mask of registers to push.  */
  mask = 0;

  for (i = 2; i < count; i++)
    {
      rtx vector_element = XVECEXP (op, 0, i);
      
      gcc_assert (GET_CODE (vector_element) == SET);
      gcc_assert (GET_CODE (SET_DEST (vector_element)) == REG);
      gcc_assert (register_is_ok_for_epilogue (SET_DEST (vector_element),
					       SImode));

      if (REGNO (SET_DEST (vector_element)) == 2)
	use_callt = 1;
      else
        mask |= 1 << REGNO (SET_DEST (vector_element));
    }

  if (! TARGET_DISABLE_CALLT
      && (use_callt || stack_bytes == 0 || stack_bytes == 16))
    {
      if (use_callt)
	{
	  sprintf (buff, "callt ctoff(__callt_return_r2_r%d)", (mask & (1 << 31)) ? 31 : 29);
	  return buff;
	}
      else
	{
	  for (i = 20; i < 32; i++)
	    if (mask & (1 << i))
	      break;
	  
	  if (i == 31)
	    sprintf (buff, "callt ctoff(__callt_return_r31c)");
	  else
	    sprintf (buff, "callt ctoff(__callt_return_r%d_r%d%s)",
		     i, (mask & (1 << 31)) ? 31 : 29, stack_bytes ? "c" : "");
	}
    }
  else
    {
      static char        regs [100]; /* XXX */
      int                done_one;
      
      /* Generate the DISPOSE instruction.  Note we could just issue the
	 bit mask as a number as the assembler can cope with this, but for
	 the sake of our readers we turn it into a textual description.  */
      regs[0] = 0;
      done_one = 0;
      
      for (i = 20; i < 32; i++)
	{
	  if (mask & (1 << i))
	    {
	      int first;
	      
	      if (done_one)
		strcat (regs, ", ");
	      else
		done_one = 1;
	      
	      first = i;
	      strcat (regs, reg_names[ first ]);
	      
	      for (i++; i < 32; i++)
		if ((mask & (1 << i)) == 0)
		  break;
	      
	      if (i > first + 1)
		{
		  strcat (regs, " - ");
		  strcat (regs, reg_names[ i - 1 ] );
		}
	    }
	}
      
      sprintf (buff, "dispose %d {%s}, r31", stack_bytes / 4, regs);
    }
  
  return buff;
}

/* Construct a PREPARE instruction that is the equivalent of
   the given RTL.  We have already verified that this should
   be possible.  */

char *
construct_prepare_instruction (rtx op)
{
  int                count = XVECLEN (op, 0);
  int                stack_bytes;
  unsigned long int  mask;
  int		     i;
  static char        buff[ 100 ]; /* XXX */
  int		     use_callt = 0;
  
  if (count <= 1)
    {
      error ("bogus PREPEARE construction: %d", count);
      return NULL;
    }

  /* Work out how many bytes to push onto
     the stack after storing the registers.  */
  gcc_assert (GET_CODE (XVECEXP (op, 0, 0)) == SET);
  gcc_assert (GET_CODE (SET_SRC (XVECEXP (op, 0, 0))) == PLUS);
  gcc_assert (GET_CODE (XEXP (SET_SRC (XVECEXP (op, 0, 0)), 1)) == CONST_INT);
    
  stack_bytes = INTVAL (XEXP (SET_SRC (XVECEXP (op, 0, 0)), 1));

  /* Each push will put 4 bytes from the stack.  */
  stack_bytes += (count - 1) * 4;

  /* Make sure that the amount we are popping
     will fit into the DISPOSE instruction.  */
  if (stack_bytes < -128)
    {
      error ("too much stack space to prepare: %d", stack_bytes);
      return NULL;
    }

  /* Now compute the bit mask of registers to push.  */
  mask = 0;
  for (i = 1; i < count; i++)
    {
      rtx vector_element = XVECEXP (op, 0, i);
      
      gcc_assert (GET_CODE (vector_element) == SET);
      gcc_assert (GET_CODE (SET_SRC (vector_element)) == REG);
      gcc_assert (register_is_ok_for_epilogue (SET_SRC (vector_element),
					       SImode));

      if (REGNO (SET_SRC (vector_element)) == 2)
	use_callt = 1;
      else
	mask |= 1 << REGNO (SET_SRC (vector_element));
    }

  if ((! TARGET_DISABLE_CALLT)
      && (use_callt || stack_bytes == 0 || stack_bytes == -16))
    {
      if (use_callt)
	{
	  sprintf (buff, "callt ctoff(__callt_save_r2_r%d)", (mask & (1 << 31)) ? 31 : 29 );
	  return buff;
	}
      
      for (i = 20; i < 32; i++)
	if (mask & (1 << i))
	  break;

      if (i == 31)
	sprintf (buff, "callt ctoff(__callt_save_r31c)");
      else
	sprintf (buff, "callt ctoff(__callt_save_r%d_r%d%s)",
		 i, (mask & (1 << 31)) ? 31 : 29, stack_bytes ? "c" : "");
    }
  else
    {
      static char        regs [100]; /* XXX */
      int                done_one;

      
      /* Generate the PREPARE instruction.  Note we could just issue the
	 bit mask as a number as the assembler can cope with this, but for
	 the sake of our readers we turn it into a textual description.  */      
      regs[0] = 0;
      done_one = 0;
      
      for (i = 20; i < 32; i++)
	{
	  if (mask & (1 << i))
	    {
	      int first;
	      
	      if (done_one)
		strcat (regs, ", ");
	      else
		done_one = 1;
	      
	      first = i;
	      strcat (regs, reg_names[ first ]);
	      
	      for (i++; i < 32; i++)
		if ((mask & (1 << i)) == 0)
		  break;
	      
	      if (i > first + 1)
		{
		  strcat (regs, " - ");
		  strcat (regs, reg_names[ i - 1 ] );
		}
	    }
	}
      	 
      sprintf (buff, "prepare {%s}, %d", regs, (- stack_bytes) / 4);
    }
  
  return buff;
}

/* Return an RTX indicating where the return address to the
   calling function can be found.  */

rtx
v850_return_addr (int count)
{
  if (count != 0)
    return const0_rtx;

  return get_hard_reg_initial_val (Pmode, LINK_POINTER_REGNUM);
}

/* Implement TARGET_ASM_INIT_SECTIONS.  */

static void
v850_asm_init_sections (void)
{
  rosdata_section
    = get_unnamed_section (0, output_section_asm_op,
			   "\t.section .rosdata,\"a\"");

  rozdata_section
    = get_unnamed_section (0, output_section_asm_op,
			   "\t.section .rozdata,\"a\"");

  tdata_section
    = get_unnamed_section (SECTION_WRITE, output_section_asm_op,
			   "\t.section .tdata,\"aw\"");

  zdata_section
    = get_unnamed_section (SECTION_WRITE, output_section_asm_op,
			   "\t.section .zdata,\"aw\"");

  zbss_section
    = get_unnamed_section (SECTION_WRITE | SECTION_BSS,
			   output_section_asm_op,
			   "\t.section .zbss,\"aw\"");
}

static section *
v850_select_section (tree exp,
                     int reloc ATTRIBUTE_UNUSED,
                     unsigned HOST_WIDE_INT align ATTRIBUTE_UNUSED)
{
  if (TREE_CODE (exp) == VAR_DECL)
    {
      int is_const;
      if (!TREE_READONLY (exp)
	  || TREE_SIDE_EFFECTS (exp)
	  || !DECL_INITIAL (exp)
	  || (DECL_INITIAL (exp) != error_mark_node
	      && !TREE_CONSTANT (DECL_INITIAL (exp))))
        is_const = FALSE;
      else
        is_const = TRUE;

      switch (v850_get_data_area (exp))
        {
        case DATA_AREA_ZDA:
	  return is_const ? rozdata_section : zdata_section;

        case DATA_AREA_TDA:
	  return tdata_section;

        case DATA_AREA_SDA:
	  return is_const ? rosdata_section : sdata_section;

        default:
	  return is_const ? readonly_data_section : data_section;
        }
    }
  return readonly_data_section;
}

/* Worker function for TARGET_RETURN_IN_MEMORY.  */

static bool
v850_return_in_memory (tree type, tree fntype ATTRIBUTE_UNUSED)
{
  /* Return values > 8 bytes in length in memory.  */
  return int_size_in_bytes (type) > 8 || TYPE_MODE (type) == BLKmode;
}

/* Worker function for TARGET_SETUP_INCOMING_VARARGS.  */

static void
v850_setup_incoming_varargs (CUMULATIVE_ARGS *ca,
			     enum machine_mode mode ATTRIBUTE_UNUSED,
			     tree type ATTRIBUTE_UNUSED,
			     int *pretend_arg_size ATTRIBUTE_UNUSED,
			     int second_time ATTRIBUTE_UNUSED)
{
  ca->anonymous_args = (!TARGET_GHS ? 1 : 0);
}

#include "gt-v850.h"
