/* Output routines for GCC for CRX.
   Copyright (C) 1991, 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001,
   2002, 2003, 2004  Free Software Foundation, Inc.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 2, or (at your
   option) any later version.

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

/*****************************************************************************/
/* HEADER INCLUDES							     */
/*****************************************************************************/

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "tree.h"
#include "tm_p.h"
#include "regs.h"
#include "hard-reg-set.h"
#include "real.h"
#include "insn-config.h"
#include "conditions.h"
#include "output.h"
#include "insn-codes.h"
#include "insn-attr.h"
#include "flags.h"
#include "except.h"
#include "function.h"
#include "recog.h"
#include "expr.h"
#include "optabs.h"
#include "toplev.h"
#include "basic-block.h"
#include "target.h"
#include "target-def.h"

/*****************************************************************************/
/* DEFINITIONS								     */
/*****************************************************************************/

/* Maximum number of register used for passing parameters.  */
#define MAX_REG_FOR_PASSING_ARGS 6

/* Minimum number register used for passing parameters.  */
#define MIN_REG_FOR_PASSING_ARGS 2

/* The maximum count of words supported in the assembly of the architecture in
 * a push/pop instruction.  */
#define MAX_COUNT		8

/* Predicate is true if the current function is a 'noreturn' function, i.e. it
 * is qualified as volatile.  */
#define FUNC_IS_NORETURN_P(decl) (TREE_THIS_VOLATILE (decl))

/* The following macros are used in crx_decompose_address () */

/* Returns the factor of a scaled index address or -1 if invalid. */
#define SCALE_FOR_INDEX_P(X)	\
 (GET_CODE (X) == CONST_INT ?	\
  (INTVAL (X) == 1 ? 1 :	\
   INTVAL (X) == 2 ? 2 :	\
   INTVAL (X) == 4 ? 4 :	\
   INTVAL (X) == 8 ? 8 :	\
   -1) :			\
  -1)

/* Nonzero if the rtx X is a signed const int of n bits */
#define RTX_SIGNED_INT_FITS_N_BITS(X,n)			\
 ((GET_CODE (X) == CONST_INT				\
   && SIGNED_INT_FITS_N_BITS (INTVAL (X), n)) ? 1 : 0)

/* Nonzero if the rtx X is an unsigned const int of n bits.  */
#define RTX_UNSIGNED_INT_FITS_N_BITS(X, n)		\
 ((GET_CODE (X) == CONST_INT				\
   && UNSIGNED_INT_FITS_N_BITS (INTVAL (X), n)) ? 1 : 0)

/*****************************************************************************/
/* STATIC VARIABLES							     */
/*****************************************************************************/

/* Nonzero if the last param processed is passed in a register.  */
static int last_parm_in_reg;

/* Will hold the number of the last register the prologue saves, -1 if no
 * register is saved. */
static int last_reg_to_save;

/* Each object in the array is a register number. Mark 1 for registers that
 * need to be saved.  */
static int save_regs[FIRST_PSEUDO_REGISTER];

/* Number of bytes saved on the stack for non-scratch registers */
static int sum_regs = 0;

/* Number of bytes saved on the stack for local variables. */
static int local_vars_size;

/* The sum of 2 sizes: locals vars and padding byte for saving the registers.
 * Used in expand_prologue () and expand_epilogue ().  */
static int size_for_adjusting_sp;

/* In case of a POST_INC or POST_DEC memory reference, we must report the mode
 * of the memory reference from PRINT_OPERAND to PRINT_OPERAND_ADDRESS. */
static enum machine_mode output_memory_reference_mode;

/*****************************************************************************/
/* GLOBAL VARIABLES							     */
/*****************************************************************************/

/* Table of machine attributes.  */
const struct attribute_spec crx_attribute_table[];

/* Test and compare insns use these globals to generate branch insns.  */
rtx crx_compare_op0 = NULL_RTX;
rtx crx_compare_op1 = NULL_RTX;

/*****************************************************************************/
/* TARGETM FUNCTION PROTOTYPES						     */
/*****************************************************************************/

static bool crx_fixed_condition_code_regs (unsigned int *, unsigned int *);
static rtx crx_struct_value_rtx (tree fntype ATTRIBUTE_UNUSED,
				 int incoming ATTRIBUTE_UNUSED);
static bool crx_return_in_memory (tree type, tree fntype ATTRIBUTE_UNUSED);
static int crx_address_cost (rtx);

/*****************************************************************************/
/* STACK LAYOUT AND CALLING CONVENTIONS					     */
/*****************************************************************************/

#undef	TARGET_FIXED_CONDITION_CODE_REGS
#define	TARGET_FIXED_CONDITION_CODE_REGS crx_fixed_condition_code_regs

#undef	TARGET_STRUCT_VALUE_RTX
#define	TARGET_STRUCT_VALUE_RTX		crx_struct_value_rtx

#undef	TARGET_RETURN_IN_MEMORY
#define	TARGET_RETURN_IN_MEMORY		crx_return_in_memory

/*****************************************************************************/
/* RELATIVE COSTS OF OPERATIONS						     */
/*****************************************************************************/

#undef	TARGET_ADDRESS_COST
#define	TARGET_ADDRESS_COST		crx_address_cost

/*****************************************************************************/
/* TARGET-SPECIFIC USES OF `__attribute__'				     */
/*****************************************************************************/

#undef  TARGET_ATTRIBUTE_TABLE
#define TARGET_ATTRIBUTE_TABLE		crx_attribute_table

const struct attribute_spec crx_attribute_table[] = {
  /* ISRs have special prologue and epilogue requirements. */
  {"interrupt", 0, 0, false, true, true, NULL},
  {NULL, 0, 0, false, false, false, NULL}
};


/* Initialize 'targetm' variable which contains pointers to functions and data
 * relating to the target machine.  */

struct gcc_target targetm = TARGET_INITIALIZER;


/*****************************************************************************/
/* TARGET HOOK IMPLEMENTATIONS						     */
/*****************************************************************************/

/* Return the fixed registers used for condition codes.  */

static bool
crx_fixed_condition_code_regs (unsigned int *p1, unsigned int *p2)
{
    *p1 = CC_REGNUM;
    *p2 = INVALID_REGNUM;
    return true;
}

/* Implements hook TARGET_STRUCT_VALUE_RTX.  */

static rtx
crx_struct_value_rtx (tree fntype ATTRIBUTE_UNUSED,
		      int incoming ATTRIBUTE_UNUSED)
{
  return gen_rtx_REG (Pmode, CRX_STRUCT_VALUE_REGNUM);
}

/* Implements hook TARGET_RETURN_IN_MEMORY.  */

static bool
crx_return_in_memory (tree type, tree fntype ATTRIBUTE_UNUSED)
{
  if (TYPE_MODE (type) == BLKmode)
    {
      HOST_WIDE_INT size = int_size_in_bytes (type);
      return (size == -1 || size > 8);
    }
  else
    return false;
}


/*****************************************************************************/
/* MACRO IMPLEMENTATIONS						     */
/*****************************************************************************/

/* STACK LAYOUT AND CALLING CONVENTIONS ROUTINES */
/* --------------------------------------------- */

/* Return nonzero if the current function being compiled is an interrupt
 * function as specified by the "interrupt" attribute.  */

int
crx_interrupt_function_p (void)
{
  tree attributes;

  attributes = TYPE_ATTRIBUTES (TREE_TYPE (current_function_decl));
  return lookup_attribute ("interrupt", attributes) != NULL_TREE;
}

/* Compute values for the array save_regs and the variable sum_regs.  The index
 * of save_regs is numbers of register, each will get 1 if we need to save it
 * in the current function, 0 if not. sum_regs is the total sum of the
 * registers being saved. */

static void
crx_compute_save_regs (void)
{
  unsigned int regno;

  /* initialize here so in case the function is no-return it will be -1. */
  last_reg_to_save = -1;

  /* No need to save any registers if the function never returns.  */
  if (FUNC_IS_NORETURN_P (current_function_decl))
    return;

  /* Initialize the number of bytes to be saved. */
  sum_regs = 0;

  for (regno = 0; regno < FIRST_PSEUDO_REGISTER; regno++)
    {
      if (fixed_regs[regno])
	{
	  save_regs[regno] = 0;
	  continue;
	}

      /* If this reg is used and not call-used (except RA), save it. */
      if (crx_interrupt_function_p ())
	{
	  if (!current_function_is_leaf && call_used_regs[regno])
	    /* this is a volatile reg in a non-leaf interrupt routine - save it
	     * for the sake of its sons.  */
	    save_regs[regno] = 1;

	  else if (regs_ever_live[regno])
	    /* This reg is used - save it.  */
	    save_regs[regno] = 1;
	  else
	    /* This reg is not used, and is not a volatile - don't save. */
      	    save_regs[regno] = 0;
	}
      else
	{
	  /* If this reg is used and not call-used (except RA), save it. */
	  if (regs_ever_live[regno]
	      && (!call_used_regs[regno] || regno == RETURN_ADDRESS_REGNUM))
	    save_regs[regno] = 1;
	  else
	    save_regs[regno] = 0;
	}
    }

  for (regno = 0; regno < FIRST_PSEUDO_REGISTER; regno++)
    if (save_regs[regno] == 1)
      {
	last_reg_to_save = regno;
	sum_regs += UNITS_PER_WORD;
      }
}

/* Compute the size of the local area and the size to be adjusted by the
 * prologue and epilogue. */

static void
crx_compute_frame (void)
{
  /* For aligning the local variables. */
  int stack_alignment = STACK_BOUNDARY / BITS_PER_UNIT;
  int padding_locals;

  /* Padding needed for each element of the frame.  */
  local_vars_size = get_frame_size ();

  /* Align to the stack alignment. */
  padding_locals = local_vars_size % stack_alignment;
  if (padding_locals)
    padding_locals = stack_alignment - padding_locals;

  local_vars_size += padding_locals;

  size_for_adjusting_sp = local_vars_size + (ACCUMULATE_OUTGOING_ARGS ?
				     current_function_outgoing_args_size : 0);
}

/* Implements the macro INITIAL_ELIMINATION_OFFSET, return the OFFSET. */

int
crx_initial_elimination_offset (int from, int to)
{
  /* Compute this since we need to use sum_regs.  */
  crx_compute_save_regs ();

  /* Compute this since we need to use local_vars_size.  */
  crx_compute_frame ();

  if ((from) == FRAME_POINTER_REGNUM && (to) == STACK_POINTER_REGNUM)
    return (ACCUMULATE_OUTGOING_ARGS ?
	    current_function_outgoing_args_size : 0);
  else if ((from) == ARG_POINTER_REGNUM && (to) == FRAME_POINTER_REGNUM)
    return (sum_regs + local_vars_size);
  else if ((from) == ARG_POINTER_REGNUM && (to) == STACK_POINTER_REGNUM)
    return (sum_regs + local_vars_size +
	    (ACCUMULATE_OUTGOING_ARGS ?
	     current_function_outgoing_args_size : 0));
  else
    abort ();
}

/* REGISTER USAGE */
/* -------------- */

/* Return the class number of the smallest class containing reg number REGNO.
 * This could be a conditional expression or could index an array. */

enum reg_class
crx_regno_reg_class (int regno)
{
  if (regno >= 0 && regno < SP_REGNUM)
    return NOSP_REGS;

  if (regno == SP_REGNUM)
    return GENERAL_REGS;

  if (regno == LO_REGNUM)
    return LO_REGS;
  if (regno == HI_REGNUM)
    return HI_REGS;

  return NO_REGS;
}

/* Transfer between HILO_REGS and memory via secondary reloading. */

enum reg_class
crx_secondary_reload_class (enum reg_class class,
			    enum machine_mode mode ATTRIBUTE_UNUSED,
			    rtx x ATTRIBUTE_UNUSED)
{
  if (reg_classes_intersect_p (class, HILO_REGS)
      && true_regnum (x) == -1)
    return GENERAL_REGS;

  return NO_REGS;
}

/* Return 1 if hard register REGNO can hold a value of machine-mode MODE. */

int
crx_hard_regno_mode_ok (int regno, enum machine_mode mode)
{
  /* CC can only hold CCmode values.  */
  if (regno == CC_REGNUM)
    return GET_MODE_CLASS (mode) == MODE_CC;
  if (GET_MODE_CLASS (mode) == MODE_CC)
    return 0;
  /* HILO registers can only hold SImode and DImode */
  if (HILO_REGNO_P (regno))
    return mode == SImode || mode == DImode;
  return 1;
}

/* PASSING FUNCTION ARGUMENTS */
/* -------------------------- */

/* If enough param regs are available for passing the param of type TYPE return
 * the number of registers needed else 0.  */

static int
enough_regs_for_param (CUMULATIVE_ARGS * cum, tree type,
		       enum machine_mode mode)
{
  int type_size;
  int remaining_size;

  if (mode != BLKmode)
    type_size = GET_MODE_BITSIZE (mode);
  else
    type_size = int_size_in_bytes (type) * BITS_PER_UNIT;

  remaining_size =
    BITS_PER_WORD * (MAX_REG_FOR_PASSING_ARGS -
    (MIN_REG_FOR_PASSING_ARGS + cum->ints) + 1);

  /* Any variable which is too big to pass in two registers, will pass on
   * stack. */
  if ((remaining_size >= type_size) && (type_size <= 2 * BITS_PER_WORD))
    return (type_size + BITS_PER_WORD - 1) / BITS_PER_WORD;

  return 0;
}

/* Implements the macro FUNCTION_ARG defined in crx.h.  */

rtx
crx_function_arg (CUMULATIVE_ARGS * cum, enum machine_mode mode, tree type,
	      int named ATTRIBUTE_UNUSED)
{
  last_parm_in_reg = 0;

  /* Function_arg () is called with this type just after all the args have had
   * their registers assigned. The rtx that function_arg returns from this type
   * is supposed to pass to 'gen_call' but currently it is not implemented (see
   * macro GEN_CALL).  */
  if (type == void_type_node)
    return NULL_RTX;

  if (targetm.calls.must_pass_in_stack (mode, type) || (cum->ints < 0))
    return NULL_RTX;

  if (mode == BLKmode)
    {
      /* Enable structures that need padding bytes at the end to pass to a
       * function in registers. */
      if (enough_regs_for_param (cum, type, mode) != 0)
	{
	  last_parm_in_reg = 1;
	  return gen_rtx_REG (mode, MIN_REG_FOR_PASSING_ARGS + cum->ints);
	}
    }

  if (MIN_REG_FOR_PASSING_ARGS + cum->ints > MAX_REG_FOR_PASSING_ARGS)
    return NULL_RTX;
  else
    {
      if (enough_regs_for_param (cum, type, mode) != 0)
	{
	  last_parm_in_reg = 1;
	  return gen_rtx_REG (mode, MIN_REG_FOR_PASSING_ARGS + cum->ints);
	}
    }

  return NULL_RTX;
}

/* Implements the macro INIT_CUMULATIVE_ARGS defined in crx.h.  */

void
crx_init_cumulative_args (CUMULATIVE_ARGS * cum, tree fntype,
		      rtx libfunc ATTRIBUTE_UNUSED)
{
  tree param, next_param;

  cum->ints = 0;

  /* Determine if this function has variable arguments.  This is indicated by
   * the last argument being 'void_type_mode' if there are no variable
   * arguments.  Change here for a different vararg.  */
  for (param = (fntype) ? TYPE_ARG_TYPES (fntype) : 0;
       param != (tree) 0; param = next_param)
    {
      next_param = TREE_CHAIN (param);
      if (next_param == (tree) 0 && TREE_VALUE (param) != void_type_node)
	{
	  cum->ints = -1;
	  return;
	}
    }
}

/* Implements the macro FUNCTION_ARG_ADVANCE defined in crx.h.  */

void
crx_function_arg_advance (CUMULATIVE_ARGS * cum, enum machine_mode mode,
		      tree type, int named ATTRIBUTE_UNUSED)
{
  /* l holds the number of registers required */
  int l = GET_MODE_BITSIZE (mode) / BITS_PER_WORD;

  /* If the parameter isn't passed on a register don't advance cum.  */
  if (!last_parm_in_reg)
    return;

  if (targetm.calls.must_pass_in_stack (mode, type) || (cum->ints < 0))
    return;

  if (mode == SImode || mode == HImode || mode == QImode || mode == DImode)
    {
      if (l <= 1)
	cum->ints += 1;
      else
	cum->ints += l;
    }
  else if (mode == SFmode || mode == DFmode)
    cum->ints += l;
  else if ((mode) == BLKmode)
    {
      if ((l = enough_regs_for_param (cum, type, mode)) != 0)
	cum->ints += l;
    }

}

/* Implements the macro FUNCTION_ARG_REGNO_P defined in crx.h.  Return nonzero
 * if N is a register used for passing parameters.  */

int
crx_function_arg_regno_p (int n)
{
  return (n <= MAX_REG_FOR_PASSING_ARGS && n >= MIN_REG_FOR_PASSING_ARGS);
}

/* ADDRESSING MODES */
/* ---------------- */

/* Implements the macro GO_IF_LEGITIMATE_ADDRESS defined in crx.h.
 * The following addressing modes are supported on CRX:
 *
 * Relocations		--> const | symbol_ref | label_ref
 * Absolute address	--> 32 bit absolute
 * Post increment	--> reg + 12 bit disp.
 * Post modify		--> reg + 12 bit disp.
 * Register relative	--> reg | 32 bit disp. + reg | 4 bit + reg
 * Scaled index		--> reg + reg | 22 bit disp. + reg + reg |
 *			    22 disp. + reg + reg + (2 | 4 | 8) */

static int crx_addr_reg_p (rtx addr_reg)
{
  rtx reg;

  if (REG_P (addr_reg))
    {
      reg = addr_reg;
    }
  else if ((GET_CODE (addr_reg) == SUBREG
	   && REG_P (SUBREG_REG (addr_reg))
	   && GET_MODE_SIZE (GET_MODE (SUBREG_REG (addr_reg)))
	   <= UNITS_PER_WORD))
    {
      reg = SUBREG_REG (addr_reg);
    }
  else
    return FALSE;

  if (GET_MODE (addr_reg) != Pmode)
    {
      return FALSE;
    }

  return TRUE;
}

enum crx_addrtype
crx_decompose_address (rtx addr, struct crx_address *out)
{
  rtx base = NULL_RTX, index = NULL_RTX, disp = NULL_RTX;
  rtx scale_rtx = NULL_RTX, side_effect = NULL_RTX;
  int scale = -1;
  
  enum crx_addrtype retval = CRX_INVALID;

  switch (GET_CODE (addr))
    {
    case CONST_INT:
      /* Absolute address (known at compile time) */
      retval = CRX_ABSOLUTE;
      disp = addr;
      if (!UNSIGNED_INT_FITS_N_BITS (INTVAL (disp), GET_MODE_BITSIZE (Pmode)))
	return CRX_INVALID;
      break;
      
    case CONST:
    case SYMBOL_REF:
    case LABEL_REF:
      /* Absolute address (known at link time) */
      retval = CRX_ABSOLUTE;
      disp = addr;
      break;

    case REG:
    case SUBREG:
      /* Register relative address */
      retval = CRX_REG_REL;
      base = addr;
      break;

    case PLUS:
      switch (GET_CODE (XEXP (addr, 0)))
	{
	case REG:
	case SUBREG:
	  if (REG_P (XEXP (addr, 1)))
	    {
	      /* Scaled index with scale = 1 and disp. = 0 */
	      retval = CRX_SCALED_INDX;
	      base = XEXP (addr, 1);
	      index = XEXP (addr, 0); 
	      scale = 1;
	    }
	  else if (RTX_SIGNED_INT_FITS_N_BITS (XEXP (addr, 1), 28))
	    {
	      /* Register relative address and <= 28-bit disp. */
	      retval = CRX_REG_REL;
	      base = XEXP (addr, 0);
	      disp = XEXP (addr, 1);
	    }
	  else
	    return CRX_INVALID;
	  break;

	case PLUS:
	  /* Scaled index and <= 22-bit disp. */
	  retval = CRX_SCALED_INDX;
	  base = XEXP (XEXP (addr, 0), 1); 
	  disp = XEXP (addr, 1);
	  if (!RTX_SIGNED_INT_FITS_N_BITS (disp, 22))
	    return CRX_INVALID;
	  switch (GET_CODE (XEXP (XEXP (addr, 0), 0)))
	    {
	    case REG:
	      /* Scaled index with scale = 0 and <= 22-bit disp. */
	      index = XEXP (XEXP (addr, 0), 0); 
	      scale = 1;
	      break;
	      
	    case MULT:
	      /* Scaled index with scale >= 0 and <= 22-bit disp. */
	      index = XEXP (XEXP (XEXP (addr, 0), 0), 0); 
	      scale_rtx = XEXP (XEXP (XEXP (addr, 0), 0), 1); 
	      if ((scale = SCALE_FOR_INDEX_P (scale_rtx)) == -1)
		return CRX_INVALID;
	      break;

	    default:
	      return CRX_INVALID;
	    }
	  break;
	  
	case MULT:
	  /* Scaled index with scale >= 0 */
	  retval = CRX_SCALED_INDX;
	  base = XEXP (addr, 1); 
	  index = XEXP (XEXP (addr, 0), 0); 
	  scale_rtx = XEXP (XEXP (addr, 0), 1); 
	  /* Scaled index with scale >= 0 and <= 22-bit disp. */
	  if ((scale = SCALE_FOR_INDEX_P (scale_rtx)) == -1)
	    return CRX_INVALID;
	  break;

	default:
	  return CRX_INVALID;
	}
      break;

    case POST_INC:
    case POST_DEC:
      /* Simple post-increment */
      retval = CRX_POST_INC;
      base = XEXP (addr, 0);
      side_effect = addr;
      break;

    case POST_MODIFY:
      /* Generic post-increment with <= 12-bit disp. */
      retval = CRX_POST_INC;
      base = XEXP (addr, 0);
      side_effect = XEXP (addr, 1);
      if (base != XEXP (side_effect, 0))
	return CRX_INVALID;
      switch (GET_CODE (side_effect))
	{
	case PLUS:
	case MINUS:
	  disp = XEXP (side_effect, 1);
	  if (!RTX_SIGNED_INT_FITS_N_BITS (disp, 12))
	    return CRX_INVALID;
	  break;

	default:
	  /* CRX only supports PLUS and MINUS */
	  return CRX_INVALID;
	}
      break;

    default:
      return CRX_INVALID;
    }

  if (base && !crx_addr_reg_p (base)) return CRX_INVALID;
  if (index && !crx_addr_reg_p (index)) return CRX_INVALID;
  
  out->base = base;
  out->index = index;
  out->disp = disp;
  out->scale = scale;
  out->side_effect = side_effect;

  return retval;
}

int
crx_legitimate_address_p (enum machine_mode mode ATTRIBUTE_UNUSED,
			  rtx addr, int strict)
{
  enum crx_addrtype addrtype;
  struct crx_address address;
						 
  if (TARGET_DEBUG_ADDR)
    {
      fprintf (stderr,
               "\n======\nGO_IF_LEGITIMATE_ADDRESS, mode = %s, strict = %d\n",
               GET_MODE_NAME (mode), strict);
      debug_rtx (addr);
    }
  
  addrtype = crx_decompose_address (addr, &address);

  if (addrtype == CRX_POST_INC && GET_MODE_SIZE (mode) > UNITS_PER_WORD)
    return FALSE;

  if (TARGET_DEBUG_ADDR)
    {
      const char *typestr;
      switch (addrtype)
	{
	case CRX_INVALID:
	  typestr = "Invalid";
	  break;
	case CRX_REG_REL:
	  typestr = "Register relative";
	  break;
	case CRX_POST_INC:
	  typestr = "Post-increment";
	  break;
	case CRX_SCALED_INDX:
	  typestr = "Scaled index";
	  break;
	case CRX_ABSOLUTE:
	  typestr = "Absolute";
	  break;
	default:
	  abort ();
	}
      fprintf (stderr, "CRX Address type: %s\n", typestr);
    }
  
  if (addrtype == CRX_INVALID)
    return FALSE;

  if (strict)
    {
      if (address.base && !REGNO_OK_FOR_BASE_P (REGNO (address.base)))
	{
	  if (TARGET_DEBUG_ADDR)
	    fprintf (stderr, "Base register not strict\n");
	  return FALSE;
	}
      if (address.index && !REGNO_OK_FOR_INDEX_P (REGNO (address.index)))
	{
	  if (TARGET_DEBUG_ADDR)
	    fprintf (stderr, "Index register not strict\n");
	  return FALSE;
	}
    }

  return TRUE;
}

/* ROUTINES TO COMPUTE COSTS */
/* ------------------------- */

/* Return cost of the memory address x. */

static int
crx_address_cost (rtx addr)
{
  enum crx_addrtype addrtype;
  struct crx_address address;
						 
  int cost = 2;
  
  addrtype = crx_decompose_address (addr, &address);
  
  gcc_assert (addrtype != CRX_INVALID);

  /* An absolute address causes a 3-word instruction */
  if (addrtype == CRX_ABSOLUTE)
    cost+=2;
  
  /* Post-modifying addresses are more powerful.  */
  if (addrtype == CRX_POST_INC)
    cost-=2;

  /* Attempt to minimize number of registers in the address. */
  if (address.base)
    cost++;
  
  if (address.index && address.scale == 1)
    cost+=5;

  if (address.disp && !INT_CST4 (INTVAL (address.disp)))
    cost+=2;

  if (TARGET_DEBUG_ADDR)
    {
      fprintf (stderr, "\n======\nTARGET_ADDRESS_COST = %d\n", cost);
      debug_rtx (addr);
    }
  
  return cost;
}

/* Return the cost of moving data of mode MODE between a register of class
 * CLASS and memory; IN is zero if the value is to be written to memory,
 * nonzero if it is to be read in. This cost is relative to those in
 * REGISTER_MOVE_COST.  */

int
crx_memory_move_cost (enum machine_mode mode,
		  enum reg_class class ATTRIBUTE_UNUSED,
		  int in ATTRIBUTE_UNUSED)
{
  /* One LD or ST takes twice the time of a simple reg-reg move */
  if (reg_classes_intersect_p (class, GENERAL_REGS))
    {
      /* printf ("GENERAL_REGS LD/ST = %d\n", 4 * HARD_REGNO_NREGS (0, mode));*/
      return 4 * HARD_REGNO_NREGS (0, mode);
    }	
  else if (reg_classes_intersect_p (class, HILO_REGS))
    {
      /* HILO to memory and vice versa */
      /* printf ("HILO_REGS %s = %d\n", in ? "LD" : "ST",
	     (REGISTER_MOVE_COST (mode,
				 in ? GENERAL_REGS : HILO_REGS,
				 in ? HILO_REGS : GENERAL_REGS) + 4)
	* HARD_REGNO_NREGS (0, mode)); */
      return (REGISTER_MOVE_COST (mode,
				 in ? GENERAL_REGS : HILO_REGS,
				 in ? HILO_REGS : GENERAL_REGS) + 4)
	* HARD_REGNO_NREGS (0, mode);
    }
  else /* default (like in i386) */
    {
      /* printf ("ANYREGS = 100\n"); */
      return 100;
    }
}

/* INSTRUCTION OUTPUT */
/* ------------------ */

/* Check if a const_double is ok for crx store-immediate instructions */

int
crx_const_double_ok (rtx op)
{
  if (GET_MODE (op) == DFmode)
  {
    REAL_VALUE_TYPE r;
    long l[2];
    REAL_VALUE_FROM_CONST_DOUBLE (r, op);
    REAL_VALUE_TO_TARGET_DOUBLE (r, l);
    return (UNSIGNED_INT_FITS_N_BITS (l[0], 4) &&
	    UNSIGNED_INT_FITS_N_BITS (l[1], 4)) ? 1 : 0;
  }

  if (GET_MODE (op) == SFmode)
  {
    REAL_VALUE_TYPE r;
    long l;
    REAL_VALUE_FROM_CONST_DOUBLE (r, op);
    REAL_VALUE_TO_TARGET_SINGLE (r, l);
    return UNSIGNED_INT_FITS_N_BITS (l, 4) ? 1 : 0;
  }

  return (UNSIGNED_INT_FITS_N_BITS (CONST_DOUBLE_LOW (op), 4) &&
	  UNSIGNED_INT_FITS_N_BITS (CONST_DOUBLE_HIGH (op), 4)) ? 1 : 0;
}

/* Implements the macro PRINT_OPERAND defined in crx.h.  */

void
crx_print_operand (FILE * file, rtx x, int code)
{
  switch (code)
    {
    case 'p' :
      if (GET_CODE (x) == REG) {
	if (GET_MODE (x) == DImode || GET_MODE (x) == DFmode)
	  {
	    int regno = REGNO (x);
	    if (regno + 1 >= SP_REGNUM) abort ();
	    fprintf (file, "{%s, %s}", reg_names[regno], reg_names[regno + 1]);
	    return;
	  }
	else
	  {
	    if (REGNO (x) >= SP_REGNUM) abort ();
	    fprintf (file, "%s", reg_names[REGNO (x)]);
	    return;
	  }
      }

    case 'd' :
	{
	  const char *crx_cmp_str;
	  switch (GET_CODE (x))
	    { /* MD: compare (reg, reg or imm) but CRX: cmp (reg or imm, reg)
	       * -> swap all non symmetric ops */
	    case EQ  : crx_cmp_str = "eq"; break;
	    case NE  : crx_cmp_str = "ne"; break;
	    case GT  : crx_cmp_str = "lt"; break;
	    case GTU : crx_cmp_str = "lo"; break;
	    case LT  : crx_cmp_str = "gt"; break;
	    case LTU : crx_cmp_str = "hi"; break;
	    case GE  : crx_cmp_str = "le"; break;
	    case GEU : crx_cmp_str = "ls"; break;
	    case LE  : crx_cmp_str = "ge"; break;
	    case LEU : crx_cmp_str = "hs"; break;
	    default : abort ();
	    }
	  fprintf (file, "%s", crx_cmp_str);
	  return;
	}

    case 'H':
      /* Print high part of a double precision value. */
      switch (GET_CODE (x))
	{
	case CONST_DOUBLE:
	  if (GET_MODE (x) == SFmode) abort ();
	  if (GET_MODE (x) == DFmode)
	    {
	      /* High part of a DF const. */
	      REAL_VALUE_TYPE r;
	      long l[2];

	      REAL_VALUE_FROM_CONST_DOUBLE (r, x);
	      REAL_VALUE_TO_TARGET_DOUBLE (r, l);

	      fprintf (file, "$0x%lx", l[1]);
	      return;
	    }

	  /* -- Fallthrough to handle DI consts -- */

	case CONST_INT:
	    {
	      rtx high, low;
	      split_double (x, &low, &high);
	      putc ('$', file);
	      output_addr_const (file, high);
	      return;
	    }

	case REG:
	  if (REGNO (x) + 1 >= FIRST_PSEUDO_REGISTER) abort ();
	  fprintf (file, "%s", reg_names[REGNO (x) + 1]);
	  return;

	case MEM:
	  /* Adjust memory address to high part.  */
	    {
	      rtx adj_mem = x;
	      adj_mem = adjust_address (adj_mem, GET_MODE (adj_mem), 4);

	      output_memory_reference_mode = GET_MODE (adj_mem);
	      output_address (XEXP (adj_mem, 0));
	      return;
	    }

	default:
	  abort ();
	}

    case 'L':
      /* Print low part of a double precision value. */
      switch (GET_CODE (x))
	{
	case CONST_DOUBLE:
	  if (GET_MODE (x) == SFmode) abort ();
	  if (GET_MODE (x) == DFmode)
	    {
	      /* High part of a DF const. */
	      REAL_VALUE_TYPE r;
	      long l[2];

	      REAL_VALUE_FROM_CONST_DOUBLE (r, x);
	      REAL_VALUE_TO_TARGET_DOUBLE (r, l);

	      fprintf (file, "$0x%lx", l[0]);
	      return;
	    }

	  /* -- Fallthrough to handle DI consts -- */

	case CONST_INT:
	    {
	      rtx high, low;
	      split_double (x, &low, &high);
	      putc ('$', file);
	      output_addr_const (file, low);
	      return;
	    }

	case REG:
	  fprintf (file, "%s", reg_names[REGNO (x)]);
	  return;

	case MEM:
	  output_memory_reference_mode = GET_MODE (x);
	  output_address (XEXP (x, 0));
	  return;

	default:
	  abort ();
	}

    case 0 : /* default */
      switch (GET_CODE (x))
	{
	case REG:
	  fprintf (file, "%s", reg_names[REGNO (x)]);
	  return;

	case MEM:
	  output_memory_reference_mode = GET_MODE (x);
	  output_address (XEXP (x, 0));
	  return;

	case CONST_DOUBLE:
	    {
	      REAL_VALUE_TYPE r;
	      long l;

	      /* Always use H and L for double precision - see above */
	      gcc_assert (GET_MODE (x) == SFmode);

	      REAL_VALUE_FROM_CONST_DOUBLE (r, x);
	      REAL_VALUE_TO_TARGET_SINGLE (r, l);

	      fprintf (file, "$0x%lx", l);
	      return;
	    }

	default:
	  putc ('$', file);
	  output_addr_const (file, x);
	  return;
	}

    default:
      output_operand_lossage ("invalid %%xn code");
    }

  abort ();
}

/* Implements the macro PRINT_OPERAND_ADDRESS defined in crx.h.  */

void
crx_print_operand_address (FILE * file, rtx addr)
{
  enum crx_addrtype addrtype;
  struct crx_address address;

  int offset;
  
  addrtype = crx_decompose_address (addr, &address);
  
  if (address.disp)
    offset = INTVAL (address.disp);
  else
    offset = 0;

  switch (addrtype)
    {
    case CRX_REG_REL:
      fprintf (file, "%d(%s)", offset, reg_names[REGNO (address.base)]);
      return;
      
    case CRX_POST_INC:
      switch (GET_CODE (address.side_effect))
	{
	case PLUS:
	  break;
	case MINUS:
	  offset = -offset;
	  break;
	case POST_INC:
	  offset = GET_MODE_SIZE (output_memory_reference_mode);
	  break;
	case POST_DEC:
	  offset = -GET_MODE_SIZE (output_memory_reference_mode);
	  break;
	default:
	  abort ();
	}
	fprintf (file, "%d(%s)+", offset, reg_names[REGNO (address.base)]);
      return;
      
    case CRX_SCALED_INDX:
      fprintf (file, "%d(%s, %s, %d)", offset, reg_names[REGNO (address.base)],
	       reg_names[REGNO (address.index)], address.scale);
      return;
      
    case CRX_ABSOLUTE:
      output_addr_const (file, address.disp);
      return;
      
    default:
      abort ();
    }
}


/*****************************************************************************/
/* MACHINE DESCRIPTION HELPER-FUNCTIONS					     */
/*****************************************************************************/

void crx_expand_movmem_single (rtx src, rtx srcbase, rtx dst, rtx dstbase,
			       rtx tmp_reg, unsigned HOST_WIDE_INT *offset_p)
{
  rtx addr, mem;
  unsigned HOST_WIDE_INT offset = *offset_p;

  /* Load */
  addr = plus_constant (src, offset);
  mem = adjust_automodify_address (srcbase, SImode, addr, offset);
  emit_move_insn (tmp_reg, mem);

  /* Store */
  addr = plus_constant (dst, offset);
  mem = adjust_automodify_address (dstbase, SImode, addr, offset);
  emit_move_insn (mem, tmp_reg);

  *offset_p = offset + 4;
}

int
crx_expand_movmem (rtx dstbase, rtx srcbase, rtx count_exp, rtx align_exp)
{
  unsigned HOST_WIDE_INT count = 0, offset, si_moves, i;
  HOST_WIDE_INT align = 0;

  rtx src, dst;
  rtx tmp_reg;

  if (GET_CODE (align_exp) == CONST_INT)
    { /* Only if aligned */
      align = INTVAL (align_exp);
      if (align & 3)
	return 0;
    }

  if (GET_CODE (count_exp) == CONST_INT)
    { /* No more than 16 SImode moves */
      count = INTVAL (count_exp);
      if (count > 64)
	return 0;
    }

  tmp_reg = gen_reg_rtx (SImode);

  /* Create psrs for the src and dest pointers */
  dst = copy_to_mode_reg (Pmode, XEXP (dstbase, 0));
  if (dst != XEXP (dstbase, 0))
    dstbase = replace_equiv_address_nv (dstbase, dst);
  src = copy_to_mode_reg (Pmode, XEXP (srcbase, 0));
  if (src != XEXP (srcbase, 0))
    srcbase = replace_equiv_address_nv (srcbase, src);

  offset = 0;

  /* Emit SImode moves */
  si_moves = count >> 2;
  for (i = 0; i < si_moves; i++)
    crx_expand_movmem_single (src, srcbase, dst, dstbase, tmp_reg, &offset);

  /* Special cases */
  if (count & 3)
    {
      offset = count - 4;
      crx_expand_movmem_single (src, srcbase, dst, dstbase, tmp_reg, &offset);
    }

  gcc_assert (offset == count);

  return 1;
}

rtx
crx_expand_compare (enum rtx_code code, enum machine_mode mode)
{
  rtx op0, op1, cc_reg, ret;

  op0 = crx_compare_op0;
  op1 = crx_compare_op1;

  /* Emit the compare that writes into CC_REGNUM) */
  cc_reg = gen_rtx_REG (CCmode, CC_REGNUM);
  ret = gen_rtx_COMPARE (CCmode, op0, op1);
  emit_insn (gen_rtx_SET (VOIDmode, cc_reg, ret));
  /* debug_rtx (get_last_insn ()); */

  /* Return the rtx for using the result in CC_REGNUM */
  return gen_rtx_fmt_ee (code, mode, cc_reg, const0_rtx);
}

void
crx_expand_branch (enum rtx_code code, rtx label)
{
  rtx tmp = crx_expand_compare (code, VOIDmode);
  tmp = gen_rtx_IF_THEN_ELSE (VOIDmode, tmp,
			      gen_rtx_LABEL_REF (VOIDmode, label),
			      pc_rtx);
  emit_jump_insn (gen_rtx_SET (VOIDmode, pc_rtx, tmp));
  /* debug_rtx (get_last_insn ()); */
}

void
crx_expand_scond (enum rtx_code code, rtx dest)
{
  rtx tmp = crx_expand_compare (code, GET_MODE (dest));
  emit_move_insn (dest, tmp);
  /* debug_rtx (get_last_insn ()); */
}

static void
mpushpop_str (char *stringbuffer, const char *mnemonic, char *mask)
{
  if (strlen (mask) > 2 || crx_interrupt_function_p ()) /* needs 2-word instr. */
    sprintf (stringbuffer, "\n\t%s\tsp, {%s}", mnemonic, mask);
  else /* single word instruction */
    sprintf (stringbuffer, "\n\t%s\t%s", mnemonic, mask);
}

/* Called from crx.md. The return value depends on the parameter push_or_pop:
 * When push_or_pop is zero -> string for push instructions of prologue.
 * When push_or_pop is nonzero -> string for pop/popret/retx in epilogue.
 * Relies on the assumptions:
 * 1. RA is the last register to be saved.
 * 2. The maximal value of the counter is MAX_COUNT. */

char *
crx_prepare_push_pop_string (int push_or_pop)
{
  /* j is the number of registers being saved, takes care that there won't be
   * more than 8 in one push/pop instruction */

  /* For the register mask string */
  static char mask_str[50];

  /* i is the index of save_regs[], going from 0 until last_reg_to_save */
  int i = 0;

  int ra_in_bitmask = 0;

  char *return_str;

  /* For reversing on the push instructions if there are more than one. */
  char *temp_str;

  return_str = (char *) xmalloc (120);
  temp_str = (char *) xmalloc (120);

  /* Initialize */
  memset (return_str, 0, 3);

  while (i <= last_reg_to_save)
    {
      /* Prepare mask for one instruction. */
      mask_str[0] = 0;

      if (i <= SP_REGNUM)
	{ /* Add regs unit full or SP register reached */
	  int j = 0;
	  while (j < MAX_COUNT && i <= SP_REGNUM)
	    {
	      if (save_regs[i])
		{
		  /* TODO to use ra_in_bitmask for detecting last pop is not
		   * smart it prevents things like:  popret r5 */
		  if (i == RETURN_ADDRESS_REGNUM) ra_in_bitmask = 1;
		  if (j > 0) strcat (mask_str, ", ");
		  strcat (mask_str, reg_names[i]);
		  ++j;
		}
	      ++i;
	    }
	}
      else
	{
	  /* Handle hi/lo savings */
	  while (i <= last_reg_to_save)
	    {
	      if (save_regs[i])
		{
		  strcat (mask_str, "lo, hi");
		  i = last_reg_to_save + 1;
		  break;
		}
	      ++i;
	    }
	}

      if (strlen (mask_str) == 0) continue;
       	
      if (push_or_pop == 1)
	{
	  if (crx_interrupt_function_p ())
	    mpushpop_str (temp_str, "popx", mask_str);
	  else
	    {
	      if (ra_in_bitmask)
		{
		  mpushpop_str (temp_str, "popret", mask_str);
		  ra_in_bitmask = 0;
		}
	      else mpushpop_str (temp_str, "pop", mask_str);
	    }

	  strcat (return_str, temp_str);
	}
      else
	{
	  /* push - We need to reverse the order of the instructions if there
	   * are more than one. (since the pop will not be reversed in the
	   * epilogue */
      	  if (crx_interrupt_function_p ())
	    mpushpop_str (temp_str, "pushx", mask_str);
	  else
	    mpushpop_str (temp_str, "push", mask_str);
	  strcat (temp_str, return_str);
	  strcpy (strcat (return_str, "\t"), temp_str);
	}

    }

  if (push_or_pop == 1)
    {
      /* pop */
      if (crx_interrupt_function_p ())
	strcat (return_str, "\n\tretx\n");

      else if (!FUNC_IS_NORETURN_P (current_function_decl)
	       && !save_regs[RETURN_ADDRESS_REGNUM])
	strcat (return_str, "\n\tjump\tra\n");
    }

  /* Skip the newline and the tab in the start of return_str. */
  return_str += 2;
  return return_str;
}

/*  CompactRISC CRX Architecture stack layout:

     0 +---------------------
	|
	.
	.
	|
	+==================== Sp(x)=Ap(x+1)
      A | Args for functions
      | | called by X and      Dynamically
      | | Dynamic allocations  allocated and
      | | (alloca, variable    deallocated
  Stack | length arrays).
  grows +-------------------- Fp(x)
  down| | Local variables of X
  ward| +--------------------
      | | Regs saved for X-1
      | +==================== Sp(x-1)=Ap(x)
	| Args for func X
	| pushed by X-1
	+-------------------- Fp(x-1)
	|
	|
	V

*/

void
crx_expand_prologue (void)
{
  crx_compute_frame ();
  crx_compute_save_regs ();

  /* If there is no need in push and adjustment to sp, return. */
  if (size_for_adjusting_sp + sum_regs == 0)
    return;

  if (last_reg_to_save != -1)
    /* If there are registers to push.  */
    emit_insn (gen_push_for_prologue (GEN_INT (sum_regs)));

  if (size_for_adjusting_sp > 0)
    emit_insn (gen_addsi3 (stack_pointer_rtx, stack_pointer_rtx,
			   GEN_INT (-size_for_adjusting_sp)));

  if (frame_pointer_needed)
    /* Initialize the frame pointer with the value of the stack pointer
     * pointing now to the locals. */
    emit_move_insn (frame_pointer_rtx, stack_pointer_rtx);
}

/* Generate insn that updates the stack for local variables and padding for
 * registers we save. - Generate the appropriate return insn. */

void
crx_expand_epilogue (void)
{
  rtx return_reg;

  /* Nonzero if we need to return and pop only RA. This will generate a
   * different insn. This differentiate is for the peepholes for call as last
   * statement in function. */
  int only_popret_RA = (save_regs[RETURN_ADDRESS_REGNUM]
			&& (sum_regs == UNITS_PER_WORD));

  /* Return register.  */
  return_reg = gen_rtx_REG (Pmode, RETURN_ADDRESS_REGNUM);

  if (frame_pointer_needed)
    /* Restore the stack pointer with the frame pointers value */
    emit_move_insn (stack_pointer_rtx, frame_pointer_rtx);

  if (size_for_adjusting_sp > 0)
    emit_insn (gen_addsi3 (stack_pointer_rtx, stack_pointer_rtx,
			   GEN_INT (size_for_adjusting_sp)));

  if (crx_interrupt_function_p ())
    emit_jump_insn (gen_interrupt_return ());
  else if (last_reg_to_save == -1)
    /* Nothing to pop */
    /* Don't output jump for interrupt routine, only retx.  */
    emit_jump_insn (gen_indirect_jump_return ());
  else if (only_popret_RA)
    emit_jump_insn (gen_popret_RA_return ());
  else
    emit_jump_insn (gen_pop_and_popret_return (GEN_INT (sum_regs)));
}

