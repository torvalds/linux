/* Target definitions for the MorphoRISC1
   Copyright (C) 2005 Free Software Foundation, Inc.
   Contributed by Red Hat, Inc.

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
#include "real.h"
#include "insn-config.h"
#include "conditions.h"
#include "insn-attr.h"
#include "recog.h"
#include "toplev.h"
#include "output.h"
#include "integrate.h"
#include "tree.h"
#include "function.h"
#include "expr.h"
#include "optabs.h"
#include "libfuncs.h"
#include "flags.h"
#include "tm_p.h"
#include "ggc.h"
#include "insn-flags.h"
#include "obstack.h"
#include "except.h"
#include "target.h"
#include "target-def.h"
#include "basic-block.h"

/* Frame pointer register mask.  */
#define FP_MASK		 	 (1 << (GPR_FP))

/* Link register mask.  */
#define LINK_MASK	 	 (1 << (GPR_LINK))

/* Given a SIZE in bytes, advance to the next word.  */
#define ROUND_ADVANCE(SIZE) (((SIZE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD)

/* A C structure for machine-specific, per-function data.
   This is added to the cfun structure.  */
struct machine_function GTY(())
{
  /* Flags if __builtin_return_address (n) with n >= 1 was used.  */
  int ra_needs_full_frame;
  struct rtx_def * eh_stack_adjust;
  int interrupt_handler;
  int has_loops;
};

/* Define the information needed to generate branch and scc insns.
   This is stored from the compare operation.  */
struct rtx_def * mt_compare_op0;
struct rtx_def * mt_compare_op1;

/* Current frame information calculated by compute_frame_size.  */
struct mt_frame_info current_frame_info;

/* Zero structure to initialize current_frame_info.  */
struct mt_frame_info zero_frame_info;

/* mt doesn't have unsigned compares need a library call for this.  */
struct rtx_def * mt_ucmpsi3_libcall;

static int mt_flag_delayed_branch;


static rtx
mt_struct_value_rtx (tree fndecl ATTRIBUTE_UNUSED,
			 int incoming ATTRIBUTE_UNUSED)
{
  return gen_rtx_REG (Pmode, RETVAL_REGNUM);
}

/* Implement RETURN_ADDR_RTX.  */
rtx
mt_return_addr_rtx (int count)
{
  if (count != 0)
    return NULL_RTX;

  return get_hard_reg_initial_val (Pmode, GPR_LINK);
}

/* The following variable value indicates the number of nops required
   between the current instruction and the next instruction to avoid
   any pipeline hazards.  */
static int mt_nops_required = 0;
static const char * mt_nop_reasons = "";

/* Implement ASM_OUTPUT_OPCODE.  */
const char *
mt_asm_output_opcode (FILE *f ATTRIBUTE_UNUSED, const char *ptr)
{
  if (mt_nops_required)
    fprintf (f, ";# need %d nops because of %s\n\t",
	     mt_nops_required, mt_nop_reasons);
  
  while (mt_nops_required)
    {
      fprintf (f, "nop\n\t");
      -- mt_nops_required;
    }
  
  return ptr;
}

/* Given an insn, return whether it's a memory operation or a branch
   operation, otherwise return TYPE_ARITH.  */
static enum attr_type
mt_get_attr_type (rtx complete_insn)
{
  rtx insn = PATTERN (complete_insn);

  if (JUMP_P (complete_insn))
    return TYPE_BRANCH;
  if (CALL_P (complete_insn))
    return TYPE_BRANCH;

  if (GET_CODE (insn) != SET)
    return TYPE_ARITH;

  if (SET_DEST (insn) == pc_rtx)
    return TYPE_BRANCH;

  if (GET_CODE (SET_DEST (insn)) == MEM)
    return TYPE_STORE;

  if (GET_CODE (SET_SRC (insn)) == MEM)
    return TYPE_LOAD;
  
  return TYPE_ARITH;
}

/* A helper routine for insn_dependent_p called through note_stores.  */

static void
insn_dependent_p_1 (rtx x, rtx pat ATTRIBUTE_UNUSED, void *data)
{
  rtx * pinsn = (rtx *) data;

  if (*pinsn && reg_mentioned_p (x, *pinsn))
    *pinsn = NULL_RTX;
}

/* Return true if anything in insn X is (anti,output,true)
   dependent on anything in insn Y.  */

static bool
insn_dependent_p (rtx x, rtx y)
{
  rtx tmp;

  if (! INSN_P (x) || ! INSN_P (y))
    return 0;

  tmp = PATTERN (y);
  note_stores (PATTERN (x), insn_dependent_p_1, &tmp);
  if (tmp == NULL_RTX)
    return true;

  tmp = PATTERN (x);
  note_stores (PATTERN (y), insn_dependent_p_1, &tmp);
  return (tmp == NULL_RTX);
}


/* Return true if anything in insn X is true dependent on anything in
   insn Y.  */
static bool
insn_true_dependent_p (rtx x, rtx y)
{
  rtx tmp;

  if (! INSN_P (x) || ! INSN_P (y))
    return 0;

  tmp = PATTERN (y);
  note_stores (PATTERN (x), insn_dependent_p_1, &tmp);
  return (tmp == NULL_RTX);
}

/* The following determines the number of nops that need to be
   inserted between the previous instructions and current instruction
   to avoid pipeline hazards on the mt processor.  Remember that
   the function is not called for asm insns.  */

void
mt_final_prescan_insn (rtx   insn,
			rtx * opvec ATTRIBUTE_UNUSED,
			int   noperands ATTRIBUTE_UNUSED)
{
  rtx prev_i;
  enum attr_type prev_attr;

  mt_nops_required = 0;
  mt_nop_reasons = "";

  /* ms2 constraints are dealt with in reorg.  */
  if (TARGET_MS2)
    return;
  
  /* Only worry about real instructions.  */
  if (! INSN_P (insn))
    return;

  /* Find the previous real instructions.  */
  for (prev_i = PREV_INSN (insn);
       prev_i != NULL
	 && (! INSN_P (prev_i)
	     || GET_CODE (PATTERN (prev_i)) == USE
	     || GET_CODE (PATTERN (prev_i)) == CLOBBER);
       prev_i = PREV_INSN (prev_i))
    {
      /* If we meet a barrier, there is no flow through here.  */
      if (BARRIER_P (prev_i))
	return;
    }
  
  /* If there isn't one then there is nothing that we need do.  */
  if (prev_i == NULL || ! INSN_P (prev_i))
    return;

  prev_attr = mt_get_attr_type (prev_i);
  
  /* Delayed branch slots already taken care of by delay branch scheduling.  */
  if (prev_attr == TYPE_BRANCH)
    return;

  switch (mt_get_attr_type (insn))
    {
    case TYPE_LOAD:
    case TYPE_STORE:
      /* Avoid consecutive memory operation.  */
      if  ((prev_attr == TYPE_LOAD || prev_attr == TYPE_STORE)
	   && TARGET_MS1_64_001)
	{
	  mt_nops_required = 1;
	  mt_nop_reasons = "consecutive mem ops";
	}
      /* Drop through.  */

    case TYPE_ARITH:
    case TYPE_COMPLEX:
      /* One cycle of delay is required between load
	 and the dependent arithmetic instruction.  */
      if (prev_attr == TYPE_LOAD
	  && insn_true_dependent_p (prev_i, insn))
	{
	  mt_nops_required = 1;
	  mt_nop_reasons = "load->arith dependency delay";
	}
      break;

    case TYPE_BRANCH:
      if (insn_dependent_p (prev_i, insn))
	{
	  if (prev_attr == TYPE_ARITH && TARGET_MS1_64_001)
	    {
	      /* One cycle of delay between arith
		 instructions and branch dependent on arith.  */
	      mt_nops_required = 1;
	      mt_nop_reasons = "arith->branch dependency delay";
	    }
	  else if (prev_attr == TYPE_LOAD)
	    {
	      /* Two cycles of delay are required
		 between load and dependent branch.  */
	      if (TARGET_MS1_64_001)
		mt_nops_required = 2;
	      else
		mt_nops_required = 1;
	      mt_nop_reasons = "load->branch dependency delay";
	    }
	}
      break;

    default:
      fatal_insn ("mt_final_prescan_insn, invalid insn #1", insn);
      break;
    }
}

/* Print debugging information for a frame.  */
static void
mt_debug_stack (struct mt_frame_info * info)
{
  int regno;

  if (!info)
    {
      error ("info pointer NULL");
      gcc_unreachable ();
    }

  fprintf (stderr, "\nStack information for function %s:\n",
	   ((current_function_decl && DECL_NAME (current_function_decl))
	    ? IDENTIFIER_POINTER (DECL_NAME (current_function_decl))
	    : "<unknown>"));

  fprintf (stderr, "\ttotal_size       = %d\n", info->total_size);
  fprintf (stderr, "\tpretend_size     = %d\n", info->pretend_size);
  fprintf (stderr, "\targs_size        = %d\n", info->args_size);
  fprintf (stderr, "\textra_size       = %d\n", info->extra_size);
  fprintf (stderr, "\treg_size         = %d\n", info->reg_size);
  fprintf (stderr, "\tvar_size         = %d\n", info->var_size);
  fprintf (stderr, "\tframe_size       = %d\n", info->frame_size);
  fprintf (stderr, "\treg_mask         = 0x%x\n", info->reg_mask);
  fprintf (stderr, "\tsave_fp          = %d\n", info->save_fp);
  fprintf (stderr, "\tsave_lr          = %d\n", info->save_lr);
  fprintf (stderr, "\tinitialized      = %d\n", info->initialized);
  fprintf (stderr, "\tsaved registers =");

  /* Print out reg_mask in a more readable format.  */
  for (regno = GPR_R0; regno <= GPR_LAST; regno++)
    if ( (1 << regno) & info->reg_mask)
      fprintf (stderr, " %s", reg_names[regno]);

  putc ('\n', stderr);
  fflush (stderr);
}

/* Print a memory address as an operand to reference that memory location.  */

static void
mt_print_operand_simple_address (FILE * file, rtx addr)
{
  if (!addr)
    error ("PRINT_OPERAND_ADDRESS, null pointer");

  else
    switch (GET_CODE (addr))
      {
      case REG:
	fprintf (file, "%s, #0", reg_names [REGNO (addr)]);
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
		fatal_insn ("PRINT_OPERAND_ADDRESS, 2 regs", addr);
	    }

	  else if (GET_CODE (arg1) == REG)
	      reg = arg1, offset = arg0;
	  else if (CONSTANT_P (arg0) && CONSTANT_P (arg1))
	    {
	      fprintf (file, "%s, #", reg_names [GPR_R0]);
	      output_addr_const (file, addr);
	      break;
	    }
	  fprintf (file, "%s, #", reg_names [REGNO (reg)]);
	  output_addr_const (file, offset);
	  break;
	}

      case LABEL_REF:
      case SYMBOL_REF:
      case CONST_INT:
      case CONST:
	output_addr_const (file, addr);
	break;

      default:
	fatal_insn ("PRINT_OPERAND_ADDRESS, invalid insn #1", addr);
	break;
      }
}

/* Implement PRINT_OPERAND_ADDRESS.  */
void
mt_print_operand_address (FILE * file, rtx addr)
{
  if (GET_CODE (addr) == AND
      && GET_CODE (XEXP (addr, 1)) == CONST_INT
      && INTVAL (XEXP (addr, 1)) == -3)
    mt_print_operand_simple_address (file, XEXP (addr, 0));
  else
    mt_print_operand_simple_address (file, addr);
}

/* Implement PRINT_OPERAND.  */
void
mt_print_operand (FILE * file, rtx x, int code)
{
  switch (code)
    {
    case '#':
      /* Output a nop if there's nothing for the delay slot.  */
      if (dbr_sequence_length () == 0)
	fputs ("\n\tnop", file);
      return;
      
    case 'H': 
      fprintf(file, "#%%hi16(");
      output_addr_const (file, x);
      fprintf(file, ")");
      return;
      
    case 'L': 
      fprintf(file, "#%%lo16(");
      output_addr_const (file, x);
      fprintf(file, ")");
      return;

    case 'N': 
      fprintf(file, "#%ld", ~INTVAL (x));
      return;

    case 'z':
      if (GET_CODE (x) == CONST_INT && INTVAL (x) == 0)
	{
	  fputs (reg_names[GPR_R0], file);
	  return;
	}

    case 0:
      /* Handled below.  */
      break;

    default:
      /* output_operand_lossage ("mt_print_operand: unknown code"); */
      fprintf (file, "unknown code");
      return;
    }

  switch (GET_CODE (x))
    {
    case REG:
      fputs (reg_names [REGNO (x)], file);
      break;

    case CONST:
    case CONST_INT:
      fprintf(file, "#%ld", INTVAL (x));
      break;

    case MEM:
      mt_print_operand_address(file, XEXP (x,0));
      break;

    case LABEL_REF:
    case SYMBOL_REF:
      output_addr_const (file, x);
      break;
      
    default:
      fprintf(file, "Unknown code: %d", GET_CODE (x));
      break;
    }

  return;
}

/* Implement INIT_CUMULATIVE_ARGS.  */
void
mt_init_cumulative_args (CUMULATIVE_ARGS * cum, tree fntype, rtx libname,
			 tree fndecl ATTRIBUTE_UNUSED, int incoming)
{
  *cum = 0;

  if (TARGET_DEBUG_ARG)
    {
      fprintf (stderr, "\nmt_init_cumulative_args:");

      if (incoming)
	fputs (" incoming", stderr);

      if (fntype)
	{
	  tree ret_type = TREE_TYPE (fntype);
	  fprintf (stderr, " return = %s,",
		   tree_code_name[ (int)TREE_CODE (ret_type) ]);
	}

      if (libname && GET_CODE (libname) == SYMBOL_REF)
	fprintf (stderr, " libname = %s", XSTR (libname, 0));

      if (cfun->returns_struct)
	fprintf (stderr, " return-struct");

      putc ('\n', stderr);
    }
}

/* Compute the slot number to pass an argument in.
   Returns the slot number or -1 if passing on the stack.

   CUM is a variable of type CUMULATIVE_ARGS which gives info about
    the preceding args and about the function being called.
   MODE is the argument's machine mode.
   TYPE is the data type of the argument (as a tree).
    This is null for libcalls where that information may
    not be available.
   NAMED is nonzero if this argument is a named parameter
    (otherwise it is an extra parameter matching an ellipsis).
   INCOMING_P is zero for FUNCTION_ARG, nonzero for FUNCTION_INCOMING_ARG.
   *PREGNO records the register number to use if scalar type.  */

static int
mt_function_arg_slotno (const CUMULATIVE_ARGS * cum,
			enum machine_mode mode,
			tree type,
			int named ATTRIBUTE_UNUSED,
			int incoming_p ATTRIBUTE_UNUSED,
			int * pregno)
{
  int regbase = FIRST_ARG_REGNUM;
  int slotno  = * cum;

  if (mode == VOIDmode || targetm.calls.must_pass_in_stack (mode, type))
    return -1;

  if (slotno >= MT_NUM_ARG_REGS)
    return -1;

  * pregno = regbase + slotno;

  return slotno;
}

/* Implement FUNCTION_ARG.  */
rtx
mt_function_arg (const CUMULATIVE_ARGS * cum,
		 enum machine_mode mode,
		 tree type,
		 int named,
		 int incoming_p)
{
  int slotno, regno;
  rtx reg;

  slotno = mt_function_arg_slotno (cum, mode, type, named, incoming_p, &regno);

  if (slotno == -1)
    reg = NULL_RTX;
  else
    reg = gen_rtx_REG (mode, regno);

  return reg;
}

/* Implement FUNCTION_ARG_ADVANCE.  */
void
mt_function_arg_advance (CUMULATIVE_ARGS * cum,
			 enum machine_mode mode,
			 tree type ATTRIBUTE_UNUSED,
			 int named)
{
  int slotno, regno;

  /* We pass 0 for incoming_p here, it doesn't matter.  */
  slotno = mt_function_arg_slotno (cum, mode, type, named, 0, &regno);

  * cum += (mode != BLKmode
	    ? ROUND_ADVANCE (GET_MODE_SIZE (mode))
	    : ROUND_ADVANCE (int_size_in_bytes (type)));

  if (TARGET_DEBUG_ARG)
    fprintf (stderr,
	     "mt_function_arg_advance: words = %2d, mode = %4s, named = %d, size = %3d\n",
	     *cum, GET_MODE_NAME (mode), named, 
	     (*cum) * UNITS_PER_WORD);
}

/* Implement hook TARGET_ARG_PARTIAL_BYTES.

   Returns the number of bytes at the beginning of an argument that
   must be put in registers.  The value must be zero for arguments
   that are passed entirely in registers or that are entirely pushed
   on the stack.  */
static int
mt_arg_partial_bytes (CUMULATIVE_ARGS * pcum,
		       enum machine_mode mode,
		       tree type,
		       bool named ATTRIBUTE_UNUSED)
{
  int cum = * pcum;
  int words;

  if (mode == BLKmode)
    words = ((int_size_in_bytes (type) + UNITS_PER_WORD - 1)
	     / UNITS_PER_WORD);
  else
    words = (GET_MODE_SIZE (mode) + UNITS_PER_WORD - 1) / UNITS_PER_WORD;

  if (! targetm.calls.pass_by_reference (&cum, mode, type, named)
      && cum < MT_NUM_ARG_REGS
      && (cum + words) > MT_NUM_ARG_REGS)
    {
      int bytes = (MT_NUM_ARG_REGS - cum) * UNITS_PER_WORD; 

      if (TARGET_DEBUG)
	fprintf (stderr, "function_arg_partial_nregs = %d\n", bytes);
      return bytes;
    }

  return 0;
}


/* Implement TARGET_PASS_BY_REFERENCE hook.  */
static bool
mt_pass_by_reference (CUMULATIVE_ARGS * cum ATTRIBUTE_UNUSED,
		       enum machine_mode mode ATTRIBUTE_UNUSED,
		       tree type,
		       bool named ATTRIBUTE_UNUSED)
{
  return (type && int_size_in_bytes (type) > 4 * UNITS_PER_WORD);
}

/* Implement FUNCTION_ARG_BOUNDARY.  */
int
mt_function_arg_boundary (enum machine_mode mode ATTRIBUTE_UNUSED,
			   tree type ATTRIBUTE_UNUSED)
{
  return BITS_PER_WORD;
}

/* Implement REG_OK_FOR_BASE_P.  */
int
mt_reg_ok_for_base_p (rtx x, int strict)
{
  if (strict)
    return  (((unsigned) REGNO (x)) < FIRST_PSEUDO_REGISTER);
  return 1;
}

/* Helper function of mt_legitimate_address_p.  Return true if XINSN
   is a simple address, otherwise false.  */
static bool
mt_legitimate_simple_address_p (enum machine_mode mode ATTRIBUTE_UNUSED,
				rtx xinsn, int strict)
{
  if (TARGET_DEBUG)						
    {									
      fprintf (stderr, "\n========== GO_IF_LEGITIMATE_ADDRESS, %sstrict\n",
	       strict ? "" : "not ");
      debug_rtx (xinsn);
    }

  if (GET_CODE (xinsn) == REG && mt_reg_ok_for_base_p (xinsn, strict))
    return true;

  if (GET_CODE (xinsn) == PLUS
      && GET_CODE (XEXP (xinsn, 0)) == REG
      && mt_reg_ok_for_base_p (XEXP (xinsn, 0), strict)
      && GET_CODE (XEXP (xinsn, 1)) == CONST_INT
      && SMALL_INT (XEXP (xinsn, 1)))
    return true;

  return false;
}


/* Helper function of GO_IF_LEGITIMATE_ADDRESS.  Return nonzero if
   XINSN is a legitimate address on MT.  */
int
mt_legitimate_address_p (enum machine_mode mode, rtx xinsn, int strict)
{
  if (mt_legitimate_simple_address_p (mode, xinsn, strict))
    return 1;

  if ((mode) == SImode
      && GET_CODE (xinsn) == AND
      && GET_CODE (XEXP (xinsn, 1)) == CONST_INT
      && INTVAL (XEXP (xinsn, 1)) == -3)
    return mt_legitimate_simple_address_p (mode, XEXP (xinsn, 0), strict);
  else
    return 0;
}

/* Return truth value of whether OP can be used as an operands where a
   register or 16 bit unsigned integer is needed.  */

int
uns_arith_operand (rtx op, enum machine_mode mode)
{
  if (GET_CODE (op) == CONST_INT && SMALL_INT_UNSIGNED (op))
    return 1;

  return register_operand (op, mode);
}

/* Return truth value of whether OP can be used as an operands where a
   16 bit integer is needed.  */

int
arith_operand (rtx op, enum machine_mode mode)
{
  if (GET_CODE (op) == CONST_INT && SMALL_INT (op))
    return 1;

  return register_operand (op, mode);
}

/* Return truth value of whether OP is a register or the constant 0.  */

int
reg_or_0_operand (rtx op, enum machine_mode mode)
{
  switch (GET_CODE (op))
    {
    case CONST_INT:
      return INTVAL (op) == 0;

    case REG:
    case SUBREG:
      return register_operand (op, mode);

    default:
      break;
    }

  return 0;
}

/* Return truth value of whether OP is a constant that requires two
   loads to put in a register.  */

int
big_const_operand (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  if (GET_CODE (op) == CONST_INT && CONST_OK_FOR_LETTER_P (INTVAL (op), 'M'))
    return 1;

  return 0;
}

/* Return truth value of whether OP is a constant that require only
   one load to put in a register.  */

int
single_const_operand (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  if (big_const_operand (op, mode)
      || GET_CODE (op) == CONST
      || GET_CODE (op) == LABEL_REF
      || GET_CODE (op) == SYMBOL_REF)
    return 0;

  return 1;
}

/* True if the current function is an interrupt handler
   (either via #pragma or an attribute specification).  */
int interrupt_handler;
enum processor_type mt_cpu;

static struct machine_function *
mt_init_machine_status (void)
{
  struct machine_function *f;

  f = ggc_alloc_cleared (sizeof (struct machine_function));

  return f;
}

/* Implement OVERRIDE_OPTIONS.  */
void
mt_override_options (void)
{
  if (mt_cpu_string != NULL)
    {
      if (!strcmp (mt_cpu_string, "ms1-64-001"))
	mt_cpu = PROCESSOR_MS1_64_001;
      else if (!strcmp (mt_cpu_string, "ms1-16-002"))
	mt_cpu = PROCESSOR_MS1_16_002;
      else if  (!strcmp (mt_cpu_string, "ms1-16-003"))
	mt_cpu = PROCESSOR_MS1_16_003;
      else if (!strcmp (mt_cpu_string, "ms2"))
	mt_cpu = PROCESSOR_MS2;
      else
	error ("bad value (%s) for -march= switch", mt_cpu_string);
    }
  else
    mt_cpu = PROCESSOR_MS1_16_002;

  if (flag_exceptions)
    {
      flag_omit_frame_pointer = 0;
      flag_gcse = 0;
    }

  /* We do delayed branch filling in machine dependent reorg */
  mt_flag_delayed_branch = flag_delayed_branch;
  flag_delayed_branch = 0;

  init_machine_status = mt_init_machine_status;
}

/* Do what is necessary for `va_start'.  We look at the current function
   to determine if stdarg or varargs is used and return the address of the
   first unnamed parameter.  */

static void
mt_setup_incoming_varargs (CUMULATIVE_ARGS *cum,
			   enum machine_mode mode ATTRIBUTE_UNUSED,
			   tree type ATTRIBUTE_UNUSED,
			   int *pretend_size, int no_rtl)
{
  int regno;
  int regs = MT_NUM_ARG_REGS - *cum;
  
  *pretend_size = regs < 0 ? 0 : GET_MODE_SIZE (SImode) * regs;
  
  if (no_rtl)
    return;
  
  for (regno = *cum; regno < MT_NUM_ARG_REGS; regno++)
    {
      rtx reg = gen_rtx_REG (SImode, FIRST_ARG_REGNUM + regno);
      rtx slot = gen_rtx_PLUS (Pmode,
			       gen_rtx_REG (SImode, ARG_POINTER_REGNUM),
			       GEN_INT (UNITS_PER_WORD * regno));
      
      emit_move_insn (gen_rtx_MEM (SImode, slot), reg);
    }
}

/* Returns the number of bytes offset between the frame pointer and the stack
   pointer for the current function.  SIZE is the number of bytes of space
   needed for local variables.  */

unsigned int
mt_compute_frame_size (int size)
{
  int           regno;
  unsigned int  total_size;
  unsigned int  var_size;
  unsigned int  args_size;
  unsigned int  pretend_size;
  unsigned int  extra_size;
  unsigned int  reg_size;
  unsigned int  frame_size;
  unsigned int  reg_mask;

  var_size      = size;
  args_size     = current_function_outgoing_args_size;
  pretend_size  = current_function_pretend_args_size;
  extra_size    = FIRST_PARM_OFFSET (0);
  total_size    = extra_size + pretend_size + args_size + var_size;
  reg_size      = 0;
  reg_mask	= 0;

  /* Calculate space needed for registers.  */
  for (regno = GPR_R0; regno <= GPR_LAST; regno++)
    {
      if (MUST_SAVE_REGISTER (regno))
        {
          reg_size += UNITS_PER_WORD;
          reg_mask |= 1 << regno;
        }
    }

  current_frame_info.save_fp = (regs_ever_live [GPR_FP]
				|| frame_pointer_needed
				|| interrupt_handler);
  current_frame_info.save_lr = (regs_ever_live [GPR_LINK]
				|| profile_flag
				|| interrupt_handler);
 
  reg_size += (current_frame_info.save_fp + current_frame_info.save_lr)
               * UNITS_PER_WORD;
  total_size += reg_size;
  total_size = ((total_size + 3) & ~3);

  frame_size = total_size;

  /* Save computed information.  */
  current_frame_info.pretend_size = pretend_size;
  current_frame_info.var_size     = var_size;
  current_frame_info.args_size    = args_size;
  current_frame_info.reg_size     = reg_size;
  current_frame_info.frame_size   = args_size + var_size;
  current_frame_info.total_size   = total_size;
  current_frame_info.extra_size   = extra_size;
  current_frame_info.reg_mask     = reg_mask;
  current_frame_info.initialized  = reload_completed;
 
  return total_size;
}

/* Emit code to save REG in stack offset pointed to by MEM.
   STACK_OFFSET is the offset from the SP where the save will happen.
   This function sets the REG_FRAME_RELATED_EXPR note accordingly.  */
static void
mt_emit_save_restore (enum save_direction direction,
		      rtx reg, rtx mem, int stack_offset)
{
  if (direction == FROM_PROCESSOR_TO_MEM)
    {
      rtx insn;
  
      insn = emit_move_insn (mem, reg);
      RTX_FRAME_RELATED_P (insn) = 1;
      REG_NOTES (insn)
	= gen_rtx_EXPR_LIST
	(REG_FRAME_RELATED_EXPR,
	 gen_rtx_SET (VOIDmode,
		      gen_rtx_MEM (SImode,
				   gen_rtx_PLUS (SImode,
						 stack_pointer_rtx,
						 GEN_INT (stack_offset))),
		      reg),
	 REG_NOTES (insn));
    }
  else
    emit_move_insn (reg, mem);
}


/* Emit code to save the frame pointer in the prologue and restore
   frame pointer in epilogue.  */

static void
mt_emit_save_fp (enum save_direction direction,
		  struct mt_frame_info info)
{
  rtx base_reg;
  int reg_mask = info.reg_mask  & ~(FP_MASK | LINK_MASK);
  int offset = info.total_size;
  int stack_offset = info.total_size;

  /* If there is nothing to save, get out now.  */
  if (! info.save_fp && ! info.save_lr && ! reg_mask)
    return;

  /* If offset doesn't fit in a 15-bit signed integer,
     uses a scratch registers to get a smaller offset.  */
  if (CONST_OK_FOR_LETTER_P(offset, 'O'))
    base_reg = stack_pointer_rtx;
  else
    {
      /* Use the scratch register R9 that holds old stack pointer.  */
      base_reg = gen_rtx_REG (SImode, GPR_R9);
      offset = 0;
    }

  if (info.save_fp)
    {
      offset -= UNITS_PER_WORD;
      stack_offset -= UNITS_PER_WORD;
      mt_emit_save_restore
	(direction, gen_rtx_REG (SImode, GPR_FP),
	 gen_rtx_MEM (SImode,
		      gen_rtx_PLUS (SImode, base_reg, GEN_INT (offset))),
	 stack_offset);
    }
}

/* Emit code to save registers in the prologue and restore register
   in epilogue.  */

static void
mt_emit_save_regs (enum save_direction direction,
		    struct mt_frame_info info)
{
  rtx base_reg;
  int regno;
  int reg_mask = info.reg_mask  & ~(FP_MASK | LINK_MASK);
  int offset = info.total_size;
  int stack_offset = info.total_size;

  /* If there is nothing to save, get out now.  */
  if (! info.save_fp && ! info.save_lr && ! reg_mask)
    return;

  /* If offset doesn't fit in a 15-bit signed integer,
     uses a scratch registers to get a smaller offset.  */
  if (CONST_OK_FOR_LETTER_P(offset, 'O'))
    base_reg = stack_pointer_rtx;
  else
    {
      /* Use the scratch register R9 that holds old stack pointer.  */
      base_reg = gen_rtx_REG (SImode, GPR_R9);
      offset = 0;
    }

  if (info.save_fp)
    {
      /* This just records the space for it, the actual move generated in
	 mt_emit_save_fp ().  */
      offset -= UNITS_PER_WORD;
      stack_offset -= UNITS_PER_WORD;
    }

  if (info.save_lr)
    {
      offset -= UNITS_PER_WORD;
      stack_offset -= UNITS_PER_WORD;
      mt_emit_save_restore
	(direction, gen_rtx_REG (SImode, GPR_LINK), 
	 gen_rtx_MEM (SImode,
		      gen_rtx_PLUS (SImode, base_reg, GEN_INT (offset))),
	 stack_offset);
    }

  /* Save any needed call-saved regs.  */
  for (regno = GPR_R0; regno <= GPR_LAST; regno++)
    {
      if ((reg_mask & (1 << regno)) != 0)
	{
	  offset -= UNITS_PER_WORD;
	  stack_offset -= UNITS_PER_WORD;
	  mt_emit_save_restore
	    (direction, gen_rtx_REG (SImode, regno),
	     gen_rtx_MEM (SImode,
			  gen_rtx_PLUS (SImode, base_reg, GEN_INT (offset))),
	     stack_offset);
	}
    }
}

/* Return true if FUNC is a function with the 'interrupt' attribute.  */
static bool
mt_interrupt_function_p (tree func)
{
  tree a;

  if (TREE_CODE (func) != FUNCTION_DECL)
    return false;

  a = lookup_attribute ("interrupt", DECL_ATTRIBUTES (func));
  return a != NULL_TREE;
}

/* Generate prologue code.  */
void
mt_expand_prologue (void)
{
  rtx size_rtx, insn;
  unsigned int frame_size;

  if (mt_interrupt_function_p (current_function_decl))
    {
      interrupt_handler = 1;
      if (cfun->machine)
	cfun->machine->interrupt_handler = 1;
    }

  mt_compute_frame_size (get_frame_size ());

  if (TARGET_DEBUG_STACK)
    mt_debug_stack (&current_frame_info);

  /* Compute size of stack adjustment.  */
  frame_size = current_frame_info.total_size;

  /* If offset doesn't fit in a 15-bit signed integer,
     uses a scratch registers to get a smaller offset.  */
  if (CONST_OK_FOR_LETTER_P(frame_size, 'O'))
    size_rtx = GEN_INT (frame_size);
  else
    {
      /* We do not have any scratch registers.  */
      gcc_assert (!interrupt_handler);

      size_rtx = gen_rtx_REG (SImode, GPR_R9);
      insn = emit_move_insn (size_rtx, GEN_INT (frame_size & 0xffff0000));
      insn = emit_insn (gen_iorsi3 (size_rtx, size_rtx,
				    GEN_INT (frame_size & 0x0000ffff)));
    }

  /* Allocate stack for this frame.  */
  /* Make stack adjustment and use scratch register if constant too
     large to fit as immediate.  */
  if (frame_size)
    {
      insn = emit_insn (gen_subsi3 (stack_pointer_rtx,
				 stack_pointer_rtx,
				 size_rtx));
      RTX_FRAME_RELATED_P (insn) = 1;
      REG_NOTES (insn)
	= gen_rtx_EXPR_LIST (REG_FRAME_RELATED_EXPR,
			     gen_rtx_SET (VOIDmode,
					  stack_pointer_rtx,
					  gen_rtx_MINUS (SImode,
							stack_pointer_rtx,
							GEN_INT (frame_size))),
			     REG_NOTES (insn));
    }

  /* Set R9 to point to old sp if required for access to register save
     area.  */
  if ( current_frame_info.reg_size != 0
       && !CONST_OK_FOR_LETTER_P (frame_size, 'O'))
      emit_insn (gen_addsi3 (size_rtx, size_rtx, stack_pointer_rtx));
  
  /* Save the frame pointer.  */
  mt_emit_save_fp (FROM_PROCESSOR_TO_MEM, current_frame_info);

  /* Now put the frame pointer into the frame pointer register.  */
  if (frame_pointer_needed)
    {
      insn = emit_move_insn (frame_pointer_rtx, stack_pointer_rtx);
      RTX_FRAME_RELATED_P (insn) = 1;
    }

  /* Save the registers.  */
  mt_emit_save_regs (FROM_PROCESSOR_TO_MEM, current_frame_info);

  /* If we are profiling, make sure no instructions are scheduled before
     the call to mcount.  */
  if (profile_flag)
    emit_insn (gen_blockage ());
}

/* Implement EPILOGUE_USES.  */
int
mt_epilogue_uses (int regno)
{
  if (cfun->machine && cfun->machine->interrupt_handler && reload_completed)
    return 1;
  return regno == GPR_LINK;
}

/* Generate epilogue.  EH_MODE is NORMAL_EPILOGUE when generating a
   function epilogue, or EH_EPILOGUE when generating an EH
   epilogue.  */
void
mt_expand_epilogue (enum epilogue_type eh_mode)
{
  rtx size_rtx, insn;
  unsigned frame_size;

  mt_compute_frame_size (get_frame_size ());

  if (TARGET_DEBUG_STACK)
    mt_debug_stack (& current_frame_info);

  /* Compute size of stack adjustment.  */
  frame_size = current_frame_info.total_size;

  /* If offset doesn't fit in a 15-bit signed integer,
     uses a scratch registers to get a smaller offset.  */
  if (CONST_OK_FOR_LETTER_P(frame_size, 'O'))
    size_rtx = GEN_INT (frame_size);
  else
    {
      /* We do not have any scratch registers.  */
      gcc_assert (!interrupt_handler);

      size_rtx = gen_rtx_REG (SImode, GPR_R9);
      insn = emit_move_insn (size_rtx, GEN_INT (frame_size & 0xffff0000));
      insn = emit_insn (gen_iorsi3 (size_rtx, size_rtx,
				    GEN_INT (frame_size & 0x0000ffff)));
      /* Set R9 to point to old sp if required for access to register
	 save area.  */
      emit_insn (gen_addsi3 (size_rtx, size_rtx, stack_pointer_rtx));
    }

  /* Restore sp if there was some possible change to it.  */
  if (frame_pointer_needed)
    insn = emit_move_insn (stack_pointer_rtx, frame_pointer_rtx);

  /* Restore the registers.  */
  mt_emit_save_fp (FROM_MEM_TO_PROCESSOR, current_frame_info);
  mt_emit_save_regs (FROM_MEM_TO_PROCESSOR, current_frame_info);

  /* Make stack adjustment and use scratch register if constant too
     large to fit as immediate.  */
  if (frame_size)
    {
      if (CONST_OK_FOR_LETTER_P(frame_size, 'O'))
	/* Can handle this with simple add.  */
	insn = emit_insn (gen_addsi3 (stack_pointer_rtx,
				      stack_pointer_rtx,
				      size_rtx));
      else
	/* Scratch reg R9 has the old sp value.  */
	insn = emit_move_insn (stack_pointer_rtx, 
			       gen_rtx_REG (SImode, GPR_R9));

      REG_NOTES (insn)
	= gen_rtx_EXPR_LIST (REG_FRAME_RELATED_EXPR,
			     gen_rtx_SET (VOIDmode,
					  stack_pointer_rtx,
					  gen_rtx_PLUS (SImode,
							stack_pointer_rtx,
							GEN_INT (frame_size))),
			     REG_NOTES (insn));
    }

  if (cfun->machine && cfun->machine->eh_stack_adjust != NULL_RTX)
    /* Perform the additional bump for __throw.  */
    emit_insn (gen_addsi3 (stack_pointer_rtx,
			   stack_pointer_rtx,
			   cfun->machine->eh_stack_adjust));

  /* Generate the appropriate return.  */
  if (eh_mode == EH_EPILOGUE)
    {
      emit_jump_insn (gen_eh_return_internal ());
      emit_barrier ();
    }
  else if (interrupt_handler)
    emit_jump_insn (gen_return_interrupt_internal ());
  else
    emit_jump_insn (gen_return_internal ());

  /* Reset state info for each function.  */
  interrupt_handler = 0;
  current_frame_info = zero_frame_info;
  if (cfun->machine)
    cfun->machine->eh_stack_adjust = NULL_RTX;
}


/* Generate code for the "eh_return" pattern.  */
void
mt_expand_eh_return (rtx * operands)
{
  if (GET_CODE (operands[0]) != REG
      || REGNO (operands[0]) != EH_RETURN_STACKADJ_REGNO)
    {
      rtx sp = EH_RETURN_STACKADJ_RTX;

      emit_move_insn (sp, operands[0]);
      operands[0] = sp;
    }

  emit_insn (gen_eh_epilogue (operands[0]));
}

/* Generate code for the "eh_epilogue" pattern.  */
void
mt_emit_eh_epilogue (rtx * operands ATTRIBUTE_UNUSED)
{
  cfun->machine->eh_stack_adjust = EH_RETURN_STACKADJ_RTX; /* operands[0]; */
  mt_expand_epilogue (EH_EPILOGUE);
}

/* Handle an "interrupt" attribute.  */
static tree
mt_handle_interrupt_attribute (tree * node,
			  tree   name,
			  tree   args  ATTRIBUTE_UNUSED,
			  int    flags ATTRIBUTE_UNUSED,
			  bool * no_add_attrs)
{
  if (TREE_CODE (*node) != FUNCTION_DECL)
    {
      warning (OPT_Wattributes,
	       "%qs attribute only applies to functions",
	       IDENTIFIER_POINTER (name));
      *no_add_attrs = true;
    }

  return NULL_TREE;
}

/* Table of machine attributes.  */
const struct attribute_spec mt_attribute_table[] =
{
  /* name,        min, max, decl?, type?, func?, handler  */
  { "interrupt",  0,   0,   false, false, false, mt_handle_interrupt_attribute },
  { NULL,         0,   0,   false, false, false, NULL }
};

/* Implement INITIAL_ELIMINATION_OFFSET.  */
int
mt_initial_elimination_offset (int from, int to)
{
  mt_compute_frame_size (get_frame_size ());

  if (from == FRAME_POINTER_REGNUM && to == STACK_POINTER_REGNUM)
    return 0;

  else if (from == ARG_POINTER_REGNUM && to == STACK_POINTER_REGNUM)
    return current_frame_info.total_size;

  else if (from == ARG_POINTER_REGNUM && to == FRAME_POINTER_REGNUM)
    return current_frame_info.total_size;

  else
    gcc_unreachable ();
}

/* Generate a compare for CODE.  Return a brand-new rtx that
   represents the result of the compare.  */

static rtx
mt_generate_compare (enum rtx_code code, rtx op0, rtx op1)
{
  rtx scratch0, scratch1, const_scratch;

  switch (code)
    {
    case GTU:
    case LTU:
    case GEU:
    case LEU:
      /* Need to adjust ranges for faking unsigned compares.  */
      scratch0 = gen_reg_rtx (SImode);
      scratch1 = gen_reg_rtx (SImode);
      const_scratch = force_reg (SImode, GEN_INT(MT_MIN_INT));
      emit_insn (gen_addsi3 (scratch0, const_scratch, op0));
      emit_insn (gen_addsi3 (scratch1, const_scratch, op1));
      break;
    default:
      scratch0 = op0;
      scratch1 = op1;
      break;
    }
    
  /* Adjust compare operator to fake unsigned compares.  */
  switch (code)
    {
    case GTU:
      code = GT; break;
    case LTU:
      code = LT; break;
    case GEU:
      code = GE; break;
    case LEU:
      code = LE; break;
    default:
      /* do nothing */
      break;
    }

  /* Generate the actual compare.  */
  return gen_rtx_fmt_ee (code, VOIDmode, scratch0, scratch1);
}

/* Emit a branch of kind CODE to location LOC.  */

void
mt_emit_cbranch (enum rtx_code code, rtx loc, rtx op0, rtx op1)
{
  rtx condition_rtx, loc_ref;

  if (! reg_or_0_operand (op0, SImode))
    op0 = copy_to_mode_reg (SImode, op0);

  if (! reg_or_0_operand (op1, SImode))
    op1 = copy_to_mode_reg (SImode, op1);

  condition_rtx = mt_generate_compare (code, op0, op1);
  loc_ref = gen_rtx_LABEL_REF (VOIDmode, loc);
  emit_jump_insn (gen_rtx_SET (VOIDmode, pc_rtx,
			       gen_rtx_IF_THEN_ELSE (VOIDmode, condition_rtx,
						     loc_ref, pc_rtx)));
}

/* Subfunction of the following function.  Update the flags of any MEM
   found in part of X.  */

static void
mt_set_memflags_1 (rtx x, int in_struct_p, int volatile_p)
{
  int i;

  switch (GET_CODE (x))
    {
    case SEQUENCE:
    case PARALLEL:
      for (i = XVECLEN (x, 0) - 1; i >= 0; i--)
	mt_set_memflags_1 (XVECEXP (x, 0, i), in_struct_p, volatile_p);
      break;

    case INSN:
      mt_set_memflags_1 (PATTERN (x), in_struct_p, volatile_p);
      break;

    case SET:
      mt_set_memflags_1 (SET_DEST (x), in_struct_p, volatile_p);
      mt_set_memflags_1 (SET_SRC (x), in_struct_p, volatile_p);
      break;

    case MEM:
      MEM_IN_STRUCT_P (x) = in_struct_p;
      MEM_VOLATILE_P (x) = volatile_p;
      /* Sadly, we cannot use alias sets because the extra aliasing
	 produced by the AND interferes.  Given that two-byte quantities
	 are the only thing we would be able to differentiate anyway,
	 there does not seem to be any point in convoluting the early
	 out of the alias check.  */
      /* set_mem_alias_set (x, alias_set); */
      break;

    default:
      break;
    }
}

/* Look for any MEMs in the current sequence of insns and set the
   in-struct, unchanging, and volatile flags from the flags in REF.
   If REF is not a MEM, don't do anything.  */

void
mt_set_memflags (rtx ref)
{
  rtx insn;
  int in_struct_p, volatile_p;

  if (GET_CODE (ref) != MEM)
    return;

  in_struct_p = MEM_IN_STRUCT_P (ref);
  volatile_p = MEM_VOLATILE_P (ref);

  /* This is only called from mt.md, after having had something 
     generated from one of the insn patterns.  So if everything is
     zero, the pattern is already up-to-date.  */
  if (! in_struct_p && ! volatile_p)
    return;

  for (insn = get_insns (); insn; insn = NEXT_INSN (insn))
    mt_set_memflags_1 (insn, in_struct_p, volatile_p);
}

/* Implement SECONDARY_RELOAD_CLASS.  */
enum reg_class
mt_secondary_reload_class (enum reg_class class ATTRIBUTE_UNUSED,
			    enum machine_mode mode,
			    rtx x)
{
  if ((mode == QImode && (!TARGET_BYTE_ACCESS)) || mode == HImode)
    {
      if (GET_CODE (x) == MEM
	  || (GET_CODE (x) == REG && true_regnum (x) == -1)
	  || (GET_CODE (x) == SUBREG
	      && (GET_CODE (SUBREG_REG (x)) == MEM
		  || (GET_CODE (SUBREG_REG (x)) == REG
		      && true_regnum (SUBREG_REG (x)) == -1))))
	return GENERAL_REGS;
    }

  return NO_REGS;
}

/* Handle FUNCTION_VALUE, FUNCTION_OUTGOING_VALUE, and LIBCALL_VALUE
   macros.  */
rtx
mt_function_value (tree valtype, enum machine_mode mode, tree func_decl ATTRIBUTE_UNUSED)
{
  if ((mode) == DImode || (mode) == DFmode)
    return gen_rtx_MEM (mode, gen_rtx_REG (mode, RETURN_VALUE_REGNUM));

  if (valtype)
    mode = TYPE_MODE (valtype);

  return gen_rtx_REG (mode, RETURN_VALUE_REGNUM);
}

/* Split a move into two smaller pieces.
   MODE indicates the reduced mode.  OPERANDS[0] is the original destination
   OPERANDS[1] is the original src.  The new destinations are
   OPERANDS[2] and OPERANDS[4], while the new sources are OPERANDS[3]
   and OPERANDS[5].  */

void
mt_split_words (enum machine_mode nmode,
		 enum machine_mode omode,
		 rtx *operands)
{
  rtx dl,dh;	/* src/dest pieces.  */
  rtx sl,sh;
  int	move_high_first = 0;	/* Assume no overlap.  */

  switch (GET_CODE (operands[0])) /* Dest.  */
    {
    case SUBREG:
    case REG:
      if ((GET_CODE (operands[1]) == REG
	   || GET_CODE (operands[1]) == SUBREG)
	  && true_regnum (operands[0]) <= true_regnum (operands[1]))
	move_high_first = 1;

      if (GET_CODE (operands[0]) == SUBREG)
	{
	  dl = gen_rtx_SUBREG (nmode, SUBREG_REG (operands[0]),
			       SUBREG_BYTE (operands[0]) + GET_MODE_SIZE (nmode));
	  dh = gen_rtx_SUBREG (nmode, SUBREG_REG (operands[0]), SUBREG_BYTE (operands[0]));
	}
      else if (GET_CODE (operands[0]) == REG && ! IS_PSEUDO_P (operands[0]))
	{
	  int	r = REGNO (operands[0]);
	  dh = gen_rtx_REG (nmode, r);
	  dl = gen_rtx_REG (nmode, r + HARD_REGNO_NREGS (r, nmode));
	}
      else
	{
	  dh = gen_rtx_SUBREG (nmode, operands[0], 0);
	  dl = gen_rtx_SUBREG (nmode, operands[0], GET_MODE_SIZE (nmode));
	}
      break;

    case MEM:
      switch (GET_CODE (XEXP (operands[0], 0)))
	{
	case POST_INC:
	case POST_DEC:
	  gcc_unreachable ();
	default:
	  dl = operand_subword (operands[0],
				GET_MODE_SIZE (nmode)/UNITS_PER_WORD,
				0, omode);
	  dh = operand_subword (operands[0], 0, 0, omode);
	}
      break;
    default:
      gcc_unreachable ();
    }

  switch (GET_CODE (operands[1]))
    {
    case REG:
      if (! IS_PSEUDO_P (operands[1]))
	{
	  int r = REGNO (operands[1]);

	  sh = gen_rtx_REG (nmode, r);
	  sl = gen_rtx_REG (nmode, r + HARD_REGNO_NREGS (r, nmode));
	}
      else
	{
	  sh = gen_rtx_SUBREG (nmode, operands[1], 0);
	  sl = gen_rtx_SUBREG (nmode, operands[1], GET_MODE_SIZE (nmode));
	}
      break;

    case CONST_DOUBLE:
      if (operands[1] == const0_rtx)
	sh = sl = const0_rtx;
      else
	split_double (operands[1], & sh, & sl);
      break;

    case CONST_INT:
      if (operands[1] == const0_rtx)
	sh = sl = const0_rtx;
      else
	{
	  int vl, vh;

	  switch (nmode)
	    {
	    default:
	      gcc_unreachable ();
	    }
	    
	  sl = GEN_INT (vl);
	  sh = GEN_INT (vh);
	}
      break;

    case SUBREG:
      sl = gen_rtx_SUBREG (nmode,
			   SUBREG_REG (operands[1]),
			   SUBREG_BYTE (operands[1]) + GET_MODE_SIZE (nmode));
      sh = gen_rtx_SUBREG (nmode,
			   SUBREG_REG (operands[1]),
			   SUBREG_BYTE (operands[1]));
      break;

    case MEM:
      switch (GET_CODE (XEXP (operands[1], 0)))
	{
	case POST_DEC:
	case POST_INC:
	  gcc_unreachable ();
	  break;
	default:
	  sl = operand_subword (operands[1], 
				GET_MODE_SIZE (nmode)/UNITS_PER_WORD,
				0, omode);
	  sh = operand_subword (operands[1], 0, 0, omode);
	  
	  /* Check if the DF load is going to clobber the register
             used for the address, and if so make sure that is going
             to be the second move.  */
	  if (GET_CODE (dl) == REG
	      && true_regnum (dl)
	      == true_regnum (XEXP (XEXP (sl, 0 ), 0)))
	    move_high_first = 1;
	}
      break;
    default:
      gcc_unreachable ();
    }

  if (move_high_first)
    {
      operands[2] = dh;
      operands[3] = sh;
      operands[4] = dl;
      operands[5] = sl;
    }
  else
    {
      operands[2] = dl;
      operands[3] = sl;
      operands[4] = dh;
      operands[5] = sh;
    }
  return;
}

/* Implement TARGET_MUST_PASS_IN_STACK hook.  */
static bool
mt_pass_in_stack (enum machine_mode mode ATTRIBUTE_UNUSED, tree type)
{
  return (((type) != 0
	   && (TREE_CODE (TYPE_SIZE (type)) != INTEGER_CST
	       || TREE_ADDRESSABLE (type))));
}

/* Increment the counter for the number of loop instructions in the
   current function.  */

void mt_add_loop (void)
{
  cfun->machine->has_loops++;
}


/* Maximum loop nesting depth.  */
#define MAX_LOOP_DEPTH 4
/* Maximum size of a loop (allows some headroom for delayed branch slot
   filling.  */
#define MAX_LOOP_LENGTH (200 * 4)

/* We need to keep a vector of loops */
typedef struct loop_info *loop_info;
DEF_VEC_P (loop_info);
DEF_VEC_ALLOC_P (loop_info,heap);

/* Information about a loop we have found (or are in the process of
   finding).  */
struct loop_info GTY (())
{
  /* loop number, for dumps */
  int loop_no;
  
  /* Predecessor block of the loop.   This is the one that falls into
     the loop and contains the initialization instruction.  */
  basic_block predecessor;

  /* First block in the loop.  This is the one branched to by the dbnz
     insn.  */
  basic_block head;
  
  /* Last block in the loop (the one with the dbnz insn */
  basic_block tail;

  /* The successor block of the loop.  This is the one the dbnz insn
     falls into.  */
  basic_block successor;

  /* The dbnz insn.  */
  rtx dbnz;

  /* The initialization insn.  */
  rtx init;

  /* The new initialization instruction.  */
  rtx loop_init;

  /* The new ending instruction. */
  rtx loop_end;

  /* The new label placed at the end of the loop. */
  rtx end_label;

  /* The nesting depth of the loop.  Set to -1 for a bad loop.  */
  int depth;

  /* The length of the loop.  */
  int length;

  /* Next loop in the graph. */
  struct loop_info *next;

  /* Vector of blocks only within the loop, (excluding those within
     inner loops).  */
  VEC (basic_block,heap) *blocks;

  /* Vector of inner loops within this loop  */
  VEC (loop_info,heap) *loops;
};

/* Information used during loop detection.  */
typedef struct loop_work GTY(())
{
  /* Basic block to be scanned.  */
  basic_block block;

  /* Loop it will be within.  */
  loop_info loop;
} loop_work;

/* Work list.  */
DEF_VEC_O (loop_work);
DEF_VEC_ALLOC_O (loop_work,heap);

/* Determine the nesting and length of LOOP.  Return false if the loop
   is bad.  */

static bool
mt_loop_nesting (loop_info loop)
{
  loop_info inner;
  unsigned ix;
  int inner_depth = 0;
  
  if (!loop->depth)
    {
      /* Make sure we only have one entry point.  */
      if (EDGE_COUNT (loop->head->preds) == 2)
	{
	  loop->predecessor = EDGE_PRED (loop->head, 0)->src;
	  if (loop->predecessor == loop->tail)
	    /* We wanted the other predecessor.  */
	    loop->predecessor = EDGE_PRED (loop->head, 1)->src;
	  
	  /* We can only place a loop insn on a fall through edge of a
	     single exit block.  */
	  if (EDGE_COUNT (loop->predecessor->succs) != 1
	      || !(EDGE_SUCC (loop->predecessor, 0)->flags & EDGE_FALLTHRU))
	    loop->predecessor = NULL;
	}

      /* Mark this loop as bad for now.  */
      loop->depth = -1;
      if (loop->predecessor)
	{
	  for (ix = 0; VEC_iterate (loop_info, loop->loops, ix++, inner);)
	    {
	      if (!inner->depth)
		mt_loop_nesting (inner);
	      
	      if (inner->depth < 0)
		{
		  inner_depth = -1;
		  break;
		}
	      
	      if (inner_depth < inner->depth)
		inner_depth = inner->depth;
	      loop->length += inner->length;
	    }
	  
	  /* Set the proper loop depth, if it was good. */
	  if (inner_depth >= 0)
	    loop->depth = inner_depth + 1;
	}
    }
  return (loop->depth > 0
	  && loop->predecessor
	  && loop->depth < MAX_LOOP_DEPTH
	  && loop->length < MAX_LOOP_LENGTH);
}

/* Determine the length of block BB.  */

static int
mt_block_length (basic_block bb)
{
  int length = 0;
  rtx insn;

  for (insn = BB_HEAD (bb);
       insn != NEXT_INSN (BB_END (bb));
       insn = NEXT_INSN (insn))
    {
      if (!INSN_P (insn))
	continue;
      if (CALL_P (insn))
	{
	  /* Calls are not allowed in loops.  */
	  length = MAX_LOOP_LENGTH + 1;
	  break;
	}
      
      length += get_attr_length (insn);
    }
  return length;
}

/* Scan the blocks of LOOP (and its inferiors) looking for uses of
   REG.  Return true, if we find any.  Don't count the loop's dbnz
   insn if it matches DBNZ.  */

static bool
mt_scan_loop (loop_info loop, rtx reg, rtx dbnz)
{
  unsigned ix;
  loop_info inner;
  basic_block bb;
  
  for (ix = 0; VEC_iterate (basic_block, loop->blocks, ix, bb); ix++)
    {
      rtx insn;

      for (insn = BB_HEAD (bb);
	   insn != NEXT_INSN (BB_END (bb));
	   insn = NEXT_INSN (insn))
	{
	  if (!INSN_P (insn))
	    continue;
	  if (insn == dbnz)
	    continue;
	  if (reg_mentioned_p (reg, PATTERN (insn)))
	    return true;
	}
    }
  for (ix = 0; VEC_iterate (loop_info, loop->loops, ix, inner); ix++)
    if (mt_scan_loop (inner, reg, NULL_RTX))
      return true;
  
  return false;
}

/* MS2 has a loop instruction which needs to be placed just before the
   loop.  It indicates the end of the loop and specifies the number of
   loop iterations.  It can be nested with an automatically maintained
   stack of counter and end address registers.  It's an ideal
   candidate for doloop.  Unfortunately, gcc presumes that loops
   always end with an explicit instruction, and the doloop_begin
   instruction is not a flow control instruction so it can be
   scheduled earlier than just before the start of the loop.  To make
   matters worse, the optimization pipeline can duplicate loop exit
   and entrance blocks and fails to track abnormally exiting loops.
   Thus we cannot simply use doloop.

   What we do is emit a dbnz pattern for the doloop optimization, and
   let that be optimized as normal.  Then in machine dependent reorg
   we have to repeat the loop searching algorithm.  We use the
   flow graph to find closed loops ending in a dbnz insn.  We then try
   and convert it to use the loop instruction.  The conditions are,

   * the loop has no abnormal exits, duplicated end conditions or
   duplicated entrance blocks

   * the loop counter register is only used in the dbnz instruction
   within the loop
   
   * we can find the instruction setting the initial value of the loop
   counter

   * the loop is not executed more than 65535 times. (This might be
   changed to 2^32-1, and would therefore allow variable initializers.)

   * the loop is not nested more than 4 deep 5) there are no
   subroutine calls in the loop.  */

static void
mt_reorg_loops (FILE *dump_file)
{
  basic_block bb;
  loop_info loops = NULL;
  loop_info loop;
  int nloops = 0;
  unsigned dwork = 0;
  VEC (loop_work,heap) *works = VEC_alloc (loop_work,heap,20);
  loop_work *work;
  edge e;
  edge_iterator ei;
  bool replaced = false;

  /* Find all the possible loop tails.  This means searching for every
     dbnz instruction.  For each one found, create a loop_info
     structure and add the head block to the work list. */
  FOR_EACH_BB (bb)
    {
      rtx tail = BB_END (bb);

      while (GET_CODE (tail) == NOTE)
	tail = PREV_INSN (tail);
      
      bb->aux = NULL;
      if (recog_memoized (tail) == CODE_FOR_decrement_and_branch_until_zero)
	{
	  /* A possible loop end */

	  loop = XNEW (struct loop_info);
	  loop->next = loops;
	  loops = loop;
	  loop->tail = bb;
	  loop->head = BRANCH_EDGE (bb)->dest;
	  loop->successor = FALLTHRU_EDGE (bb)->dest;
	  loop->predecessor = NULL;
	  loop->dbnz = tail;
	  loop->depth = 0;
	  loop->length = mt_block_length (bb);
	  loop->blocks = VEC_alloc (basic_block, heap, 20);
	  VEC_quick_push (basic_block, loop->blocks, bb);
	  loop->loops = NULL;
	  loop->loop_no = nloops++;
	  
	  loop->init = loop->end_label = NULL_RTX;
	  loop->loop_init = loop->loop_end = NULL_RTX;
	  
	  work = VEC_safe_push (loop_work, heap, works, NULL);
	  work->block = loop->head;
	  work->loop = loop;

	  bb->aux = loop;

	  if (dump_file)
	    {
	      fprintf (dump_file, ";; potential loop %d ending at\n",
		       loop->loop_no);
	      print_rtl_single (dump_file, tail);
	    }
	}
    }

  /*  Now find all the closed loops.
      until work list empty,
       if block's auxptr is set
         if != loop slot
           if block's loop's start != block
	     mark loop as bad
	   else
             append block's loop's fallthrough block to worklist
	     increment this loop's depth
       else if block is exit block
         mark loop as bad
       else
     	  set auxptr
	  for each target of block
     	    add to worklist */
  while (VEC_iterate (loop_work, works, dwork++, work))
    {
      loop = work->loop;
      bb = work->block;
      if (bb == EXIT_BLOCK_PTR)
	/* We've reached the exit block.  The loop must be bad. */
	loop->depth = -1;
      else if (!bb->aux)
	{
	  /* We've not seen this block before.  Add it to the loop's
	     list and then add each successor to the work list.  */
	  bb->aux = loop;
	  loop->length += mt_block_length (bb);
	  VEC_safe_push (basic_block, heap, loop->blocks, bb);
	  FOR_EACH_EDGE (e, ei, bb->succs)
	    {
	      if (!VEC_space (loop_work, works, 1))
		{
		  if (dwork)
		    {
		      VEC_block_remove (loop_work, works, 0, dwork);
		      dwork = 0;
		    }
		  else
		    VEC_reserve (loop_work, heap, works, 1);
		}
	      work = VEC_quick_push (loop_work, works, NULL);
	      work->block = EDGE_SUCC (bb, ei.index)->dest;
	      work->loop = loop;
	    }
	}
      else if (bb->aux != loop)
	{
	  /* We've seen this block in a different loop.  If it's not
	     the other loop's head, then this loop must be bad.
	     Otherwise, the other loop might be a nested loop, so
	     continue from that loop's successor.  */
	  loop_info other = bb->aux;
	  
	  if (other->head != bb)
	    loop->depth = -1;
	  else
	    {
	      VEC_safe_push (loop_info, heap, loop->loops, other);
	      work = VEC_safe_push (loop_work, heap, works, NULL);
	      work->loop = loop;
	      work->block = other->successor;
	    }
	}
    }
  VEC_free (loop_work, heap, works);

  /* Now optimize the loops.  */
  for (loop = loops; loop; loop = loop->next)
    {
      rtx iter_reg, insn, init_insn;
      rtx init_val, loop_end, loop_init, end_label, head_label;

      if (!mt_loop_nesting (loop))
	{
	  if (dump_file)
	    fprintf (dump_file, ";; loop %d is bad\n", loop->loop_no);
	  continue;
	}

      /* Get the loop iteration register.  */
      iter_reg = SET_DEST (XVECEXP (PATTERN (loop->dbnz), 0, 1));
      
      if (!REG_P (iter_reg))
	{
	  /* Spilled */
	  if (dump_file)
	    fprintf (dump_file, ";; loop %d has spilled iteration count\n",
		     loop->loop_no);
	  continue;
	}

      /* Look for the initializing insn */
      init_insn = NULL_RTX;
      for (insn = BB_END (loop->predecessor);
	   insn != PREV_INSN (BB_HEAD (loop->predecessor));
	   insn = PREV_INSN (insn))
	{
	  if (!INSN_P (insn))
	    continue;
	  if (reg_mentioned_p (iter_reg, PATTERN (insn)))
	    {
	      rtx set = single_set (insn);

	      if (set && rtx_equal_p (iter_reg, SET_DEST (set)))
		init_insn = insn;
	      break;
	    }
	}

      if (!init_insn)
	{
	  if (dump_file)
	    fprintf (dump_file, ";; loop %d has no initializer\n",
		     loop->loop_no);
	  continue;
	}
      if (dump_file)
	{
	  fprintf (dump_file, ";; loop %d initialized by\n",
		   loop->loop_no);
	  print_rtl_single (dump_file, init_insn);
	}

      init_val = PATTERN (init_insn);
      if (GET_CODE (init_val) == SET)
	init_val = SET_SRC (init_val);
      if (GET_CODE (init_val) != CONST_INT || INTVAL (init_val) >= 65535)
	{
	  if (dump_file)
	    fprintf (dump_file, ";; loop %d has complex initializer\n",
		     loop->loop_no);
	  continue;
	}
      
      /* Scan all the blocks to make sure they don't use iter_reg.  */
      if (mt_scan_loop (loop, iter_reg, loop->dbnz))
	{
	  if (dump_file)
	    fprintf (dump_file, ";; loop %d uses iterator\n",
		     loop->loop_no);
	  continue;
	}

      /* The loop is good for replacement.  */
      
      /* loop is 1 based, dbnz is zero based.  */
      init_val = GEN_INT (INTVAL (init_val) + 1);
      
      iter_reg = gen_rtx_REG (SImode, LOOP_FIRST + loop->depth - 1);
      end_label = gen_label_rtx ();
      head_label = XEXP (SET_SRC (XVECEXP (PATTERN (loop->dbnz), 0, 0)), 1);
      loop_end = gen_loop_end (iter_reg, head_label);
      loop_init = gen_loop_init (iter_reg, init_val, end_label);
      loop->init = init_insn;
      loop->end_label = end_label;
      loop->loop_init = loop_init;
      loop->loop_end = loop_end;
      replaced = true;
      
      if (dump_file)
	{
	  fprintf (dump_file, ";; replacing loop %d initializer with\n",
		   loop->loop_no);
	  print_rtl_single (dump_file, loop->loop_init);
	  fprintf (dump_file, ";; replacing loop %d terminator with\n",
		   loop->loop_no);
	  print_rtl_single (dump_file, loop->loop_end);
	}
    }

  /* Now apply the optimizations.  Do it this way so we don't mess up
     the flow graph half way through.  */
  for (loop = loops; loop; loop = loop->next)
    if (loop->loop_init)
      {
	emit_jump_insn_after (loop->loop_init, BB_END (loop->predecessor));
	delete_insn (loop->init);
	emit_label_before (loop->end_label, loop->dbnz);
	emit_jump_insn_before (loop->loop_end, loop->dbnz);
	delete_insn (loop->dbnz);
      }

  /* Free up the loop structures */
  while (loops)
    {
      loop = loops;
      loops = loop->next;
      VEC_free (loop_info, heap, loop->loops);
      VEC_free (basic_block, heap, loop->blocks);
      XDELETE (loop);
    }

  if (replaced && dump_file)
    {
      fprintf (dump_file, ";; Replaced loops\n");
      print_rtl (dump_file, get_insns ());
    }
}

/* Structures to hold branch information during reorg.  */
typedef struct branch_info
{
  rtx insn;  /* The branch insn.  */
  
  struct branch_info *next;
} branch_info;

typedef struct label_info
{
  rtx label;  /* The label.  */
  branch_info *branches;  /* branches to this label.  */
  struct label_info *next;
} label_info;

/* Chain of labels found in current function, used during reorg.  */
static label_info *mt_labels;

/* If *X is a label, add INSN to the list of branches for that
   label.  */

static int
mt_add_branches (rtx *x, void *insn)
{
  if (GET_CODE (*x) == LABEL_REF)
    {
      branch_info *branch = xmalloc (sizeof (*branch));
      rtx label = XEXP (*x, 0);
      label_info *info;

      for (info = mt_labels; info; info = info->next)
	if (info->label == label)
	  break;

      if (!info)
	{
	  info = xmalloc (sizeof (*info));
	  info->next = mt_labels;
	  mt_labels = info;
	  
	  info->label = label;
	  info->branches = NULL;
	}

      branch->next = info->branches;
      info->branches = branch;
      branch->insn = insn;
    }
  return 0;
}

/* If BRANCH has a filled delay slot, check if INSN is dependent upon
   it.  If so, undo the delay slot fill.   Returns the next insn, if
   we patch out the branch.  Returns the branch insn, if we cannot
   patch out the branch (due to anti-dependency in the delay slot).
   In that case, the caller must insert nops at the branch target.  */

static rtx
mt_check_delay_slot (rtx branch, rtx insn)
{
  rtx slot;
  rtx tmp;
  rtx p;
  rtx jmp;
  
  gcc_assert (GET_CODE (PATTERN (branch)) == SEQUENCE);
  if (INSN_DELETED_P (branch))
    return NULL_RTX;
  slot = XVECEXP (PATTERN (branch), 0, 1);
  
  tmp = PATTERN (insn);
  note_stores (PATTERN (slot), insn_dependent_p_1, &tmp);
  if (tmp)
    /* Not dependent.  */
    return NULL_RTX;
  
  /* Undo the delay slot.  */
  jmp = XVECEXP (PATTERN (branch), 0, 0);
  
  tmp = PATTERN (jmp);
  note_stores (PATTERN (slot), insn_dependent_p_1, &tmp);
  if (!tmp)
    /* Anti dependent. */
    return branch;
      
  p = PREV_INSN (branch);
  NEXT_INSN (p) = slot;
  PREV_INSN (slot) = p;
  NEXT_INSN (slot) = jmp;
  PREV_INSN (jmp) = slot;
  NEXT_INSN (jmp) = branch;
  PREV_INSN (branch) = jmp;
  XVECEXP (PATTERN (branch), 0, 0) = NULL_RTX;
  XVECEXP (PATTERN (branch), 0, 1) = NULL_RTX;
  delete_insn (branch);
  return jmp;
}

/* Insert nops to satisfy pipeline constraints.  We only deal with ms2
   constraints here.  Earlier CPUs are dealt with by inserting nops with
   final_prescan (but that can lead to inferior code, and is
   impractical with ms2's JAL hazard).

   ms2 dynamic constraints
   1) a load and a following use must be separated by one insn
   2) an insn and a following dependent call must be separated by two insns
   
   only arith insns are placed in delay slots so #1 cannot happen with
   a load in a delay slot.  #2 can happen with an arith insn in the
   delay slot.  */

static void
mt_reorg_hazard (void)
{
  rtx insn, next;

  /* Find all the branches */
  for (insn = get_insns ();
       insn;
       insn = NEXT_INSN (insn))
    {
      rtx jmp;

      if (!INSN_P (insn))
	continue;

      jmp = PATTERN (insn);
      
      if (GET_CODE (jmp) != SEQUENCE)
	/* If it's not got a filled delay slot, then it can't
	   conflict.  */
	continue;
      
      jmp = XVECEXP (jmp, 0, 0);

      if (recog_memoized (jmp) == CODE_FOR_tablejump)
	for (jmp = XEXP (XEXP (XVECEXP (PATTERN (jmp), 0, 1), 0), 0);
	     !JUMP_TABLE_DATA_P (jmp);
	     jmp = NEXT_INSN (jmp))
	  continue;

      for_each_rtx (&PATTERN (jmp), mt_add_branches, insn);
    }

  /* Now scan for dependencies.  */
  for (insn = get_insns ();
       insn && !INSN_P (insn);
       insn = NEXT_INSN (insn))
    continue;
  
  for (;
       insn;
       insn = next)
    {
      rtx jmp, tmp;
      enum attr_type attr;
      
      gcc_assert (INSN_P (insn) && !INSN_DELETED_P (insn));
      for (next = NEXT_INSN (insn);
	   next;
	   next = NEXT_INSN (next))
	{
	  if (!INSN_P (next))
	    continue;
	  if (GET_CODE (PATTERN (next)) != USE)
	    break;
	}

      jmp = insn;
      if (GET_CODE (PATTERN (insn)) == SEQUENCE)
	jmp = XVECEXP (PATTERN (insn), 0, 0);
      
      attr = recog_memoized (jmp) >= 0 ? get_attr_type (jmp) : TYPE_UNKNOWN;
      
      if (next && attr == TYPE_LOAD)
	{
	  /* A load.  See if NEXT is dependent, and if so insert a
	     nop.  */
	  
	  tmp = PATTERN (next);
	  if (GET_CODE (tmp) == SEQUENCE)
	    tmp = PATTERN (XVECEXP (tmp, 0, 0));
	  note_stores (PATTERN (insn), insn_dependent_p_1, &tmp);
	  if (!tmp)
	    emit_insn_after (gen_nop (), insn);
	}
      
      if (attr == TYPE_CALL)
	{
	  /* A call.  Make sure we're not dependent on either of the
	     previous two dynamic instructions.  */
	  int nops = 0;
	  int count;
	  rtx prev = insn;
	  rtx rescan = NULL_RTX;

	  for (count = 2; count && !nops;)
	    {
	      int type;
	      
	      prev = PREV_INSN (prev);
	      if (!prev)
		{
		  /* If we reach the start of the function, we must
		     presume the caller set the address in the delay
		     slot of the call instruction.  */
		  nops = count;
		  break;
		}
	      
	      if (BARRIER_P (prev))
		break;
	      if (LABEL_P (prev))
		{
		  /* Look at branches to this label.  */
		  label_info *label;
		  branch_info *branch;

		  for (label = mt_labels;
		       label;
		       label = label->next)
		    if (label->label == prev)
		      {
			for (branch = label->branches;
			     branch;
			     branch = branch->next)
			  {
			    tmp = mt_check_delay_slot (branch->insn, jmp);

			    if (tmp == branch->insn)
			      {
				nops = count;
				break;
			      }
			    
			    if (tmp && branch->insn == next)
			      rescan = tmp;
			  }
			break;
		      }
		  continue;
		}
	      if (!INSN_P (prev) || GET_CODE (PATTERN (prev)) == USE)
		continue;
	      
	      if (GET_CODE (PATTERN (prev)) == SEQUENCE)
		{
		  /* Look at the delay slot.  */
		  tmp = mt_check_delay_slot (prev, jmp);
		  if (tmp == prev)
		    nops = count;
		  break;
		}
	      
	      type = (INSN_CODE (prev) >= 0 ? get_attr_type (prev)
		      : TYPE_COMPLEX);
	      if (type == TYPE_CALL || type == TYPE_BRANCH)
		break;
	      
	      if (type == TYPE_LOAD
		  || type == TYPE_ARITH
		  || type == TYPE_COMPLEX)
		{
		  tmp = PATTERN (jmp);
		  note_stores (PATTERN (prev), insn_dependent_p_1, &tmp);
		  if (!tmp)
		    {
		      nops = count;
		      break;
		    }
		}

	      if (INSN_CODE (prev) >= 0)
		count--;
	    }

	  if (rescan)
	    for (next = NEXT_INSN (rescan);
		 next && !INSN_P (next);
		 next = NEXT_INSN (next))
	      continue;
	  while (nops--)
	    emit_insn_before (gen_nop (), insn);
	}
    }

  /* Free the data structures.  */
  while (mt_labels)
    {
      label_info *label = mt_labels;
      branch_info *branch, *next;
      
      mt_labels = label->next;
      for (branch = label->branches; branch; branch = next)
	{
	  next = branch->next;
	  free (branch);
	}
      free (label);
    }
}

/* Fixup the looping instructions, do delayed branch scheduling, fixup
   scheduling hazards.  */

static void
mt_machine_reorg (void)
{
  if (cfun->machine->has_loops && TARGET_MS2)
    mt_reorg_loops (dump_file);

  if (mt_flag_delayed_branch)
    dbr_schedule (get_insns ());
  
  if (TARGET_MS2)
    {
      /* Force all instructions to be split into their final form.  */
      split_all_insns_noflow ();
      mt_reorg_hazard ();
    }
}

/* Initialize the GCC target structure.  */
const struct attribute_spec mt_attribute_table[];

#undef  TARGET_ATTRIBUTE_TABLE
#define TARGET_ATTRIBUTE_TABLE 		mt_attribute_table
#undef  TARGET_STRUCT_VALUE_RTX
#define TARGET_STRUCT_VALUE_RTX		mt_struct_value_rtx
#undef  TARGET_PROMOTE_PROTOTYPES
#define TARGET_PROMOTE_PROTOTYPES	hook_bool_tree_true
#undef  TARGET_PASS_BY_REFERENCE
#define TARGET_PASS_BY_REFERENCE	mt_pass_by_reference
#undef  TARGET_MUST_PASS_IN_STACK
#define TARGET_MUST_PASS_IN_STACK       mt_pass_in_stack
#undef  TARGET_ARG_PARTIAL_BYTES
#define TARGET_ARG_PARTIAL_BYTES	mt_arg_partial_bytes
#undef  TARGET_SETUP_INCOMING_VARARGS
#define TARGET_SETUP_INCOMING_VARARGS 	mt_setup_incoming_varargs
#undef  TARGET_MACHINE_DEPENDENT_REORG
#define TARGET_MACHINE_DEPENDENT_REORG  mt_machine_reorg

struct gcc_target targetm = TARGET_INITIALIZER;

#include "gt-mt.h"
