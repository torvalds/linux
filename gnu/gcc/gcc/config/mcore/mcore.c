/* Output routines for Motorola MCore processor
   Copyright (C) 1993, 1999, 2000, 2001, 2002, 2003, 2004, 2005
   Free Software Foundation, Inc.

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

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "tree.h"
#include "tm_p.h"
#include "assert.h"
#include "mcore.h"
#include "regs.h"
#include "hard-reg-set.h"
#include "real.h"
#include "insn-config.h"
#include "conditions.h"
#include "output.h"
#include "insn-attr.h"
#include "flags.h"
#include "obstack.h"
#include "expr.h"
#include "reload.h"
#include "recog.h"
#include "function.h"
#include "ggc.h"
#include "toplev.h"
#include "target.h"
#include "target-def.h"

/* Maximum size we are allowed to grow the stack in a single operation.
   If we want more, we must do it in increments of at most this size.
   If this value is 0, we don't check at all.  */
int mcore_stack_increment = STACK_UNITS_MAXSTEP;

/* For dumping information about frame sizes.  */
char * mcore_current_function_name = 0;
long   mcore_current_compilation_timestamp = 0;

/* Global variables for machine-dependent things.  */

/* Saved operands from the last compare to use when we generate an scc
  or bcc insn.  */
rtx arch_compare_op0;
rtx arch_compare_op1;

/* Provides the class number of the smallest class containing
   reg number.  */
const int regno_reg_class[FIRST_PSEUDO_REGISTER] =
{
  GENERAL_REGS,	ONLYR1_REGS,  LRW_REGS,	    LRW_REGS,
  LRW_REGS,	LRW_REGS,     LRW_REGS,	    LRW_REGS,
  LRW_REGS,	LRW_REGS,     LRW_REGS,	    LRW_REGS,
  LRW_REGS,	LRW_REGS,     LRW_REGS,	    GENERAL_REGS,
  GENERAL_REGS, C_REGS,       NO_REGS,      NO_REGS,
};

/* Provide reg_class from a letter such as appears in the machine
   description.  */
const enum reg_class reg_class_from_letter[] =
{
  /* a */ LRW_REGS, /* b */ ONLYR1_REGS, /* c */ C_REGS,  /* d */ NO_REGS,
  /* e */ NO_REGS, /* f */ NO_REGS, /* g */ NO_REGS, /* h */ NO_REGS,
  /* i */ NO_REGS, /* j */ NO_REGS, /* k */ NO_REGS, /* l */ NO_REGS,
  /* m */ NO_REGS, /* n */ NO_REGS, /* o */ NO_REGS, /* p */ NO_REGS,
  /* q */ NO_REGS, /* r */ GENERAL_REGS, /* s */ NO_REGS, /* t */ NO_REGS,
  /* u */ NO_REGS, /* v */ NO_REGS, /* w */ NO_REGS, /* x */ ALL_REGS,
  /* y */ NO_REGS, /* z */ NO_REGS
};

struct mcore_frame
{
  int arg_size;			/* Stdarg spills (bytes).  */
  int reg_size;			/* Non-volatile reg saves (bytes).  */
  int reg_mask;			/* Non-volatile reg saves.  */
  int local_size;		/* Locals.  */
  int outbound_size;		/* Arg overflow on calls out.  */
  int pad_outbound;
  int pad_local;
  int pad_reg;
  /* Describe the steps we'll use to grow it.  */
#define	MAX_STACK_GROWS	4	/* Gives us some spare space.  */
  int growth[MAX_STACK_GROWS];
  int arg_offset;
  int reg_offset;
  int reg_growth;
  int local_growth;
};

typedef enum
{
  COND_NO,
  COND_MOV_INSN,
  COND_CLR_INSN,
  COND_INC_INSN,
  COND_DEC_INSN,
  COND_BRANCH_INSN
}
cond_type;

static void       output_stack_adjust           (int, int);
static int        calc_live_regs                (int *);
static int        try_constant_tricks           (long, int *, int *);
static const char *     output_inline_const     (enum machine_mode, rtx *);
static void       layout_mcore_frame            (struct mcore_frame *);
static void       mcore_setup_incoming_varargs	(CUMULATIVE_ARGS *, enum machine_mode, tree, int *, int);
static cond_type  is_cond_candidate             (rtx);
static rtx        emit_new_cond_insn            (rtx, int);
static rtx        conditionalize_block          (rtx);
static void       conditionalize_optimization   (void);
static void       mcore_reorg                   (void);
static rtx        handle_structs_in_regs        (enum machine_mode, tree, int);
static void       mcore_mark_dllexport          (tree);
static void       mcore_mark_dllimport          (tree);
static int        mcore_dllexport_p             (tree);
static int        mcore_dllimport_p             (tree);
const struct attribute_spec mcore_attribute_table[];
static tree       mcore_handle_naked_attribute  (tree *, tree, tree, int, bool *);
#ifdef OBJECT_FORMAT_ELF
static void	  mcore_asm_named_section       (const char *,
						 unsigned int, tree);
#endif
static void       mcore_unique_section	        (tree, int);
static void mcore_encode_section_info		(tree, rtx, int);
static const char *mcore_strip_name_encoding	(const char *);
static int        mcore_const_costs            	(rtx, RTX_CODE);
static int        mcore_and_cost               	(rtx);
static int        mcore_ior_cost               	(rtx);
static bool       mcore_rtx_costs		(rtx, int, int, int *);
static void       mcore_external_libcall	(rtx);
static bool       mcore_return_in_memory	(tree, tree);
static int        mcore_arg_partial_bytes       (CUMULATIVE_ARGS *,
						 enum machine_mode,
						 tree, bool);


/* Initialize the GCC target structure.  */
#undef  TARGET_ASM_EXTERNAL_LIBCALL
#define TARGET_ASM_EXTERNAL_LIBCALL	mcore_external_libcall

#if TARGET_DLLIMPORT_DECL_ATTRIBUTES
#undef  TARGET_MERGE_DECL_ATTRIBUTES
#define TARGET_MERGE_DECL_ATTRIBUTES	merge_dllimport_decl_attributes
#endif

#ifdef OBJECT_FORMAT_ELF
#undef  TARGET_ASM_UNALIGNED_HI_OP
#define TARGET_ASM_UNALIGNED_HI_OP "\t.short\t"
#undef  TARGET_ASM_UNALIGNED_SI_OP
#define TARGET_ASM_UNALIGNED_SI_OP "\t.long\t"
#endif

#undef  TARGET_ATTRIBUTE_TABLE
#define TARGET_ATTRIBUTE_TABLE 		mcore_attribute_table
#undef  TARGET_ASM_UNIQUE_SECTION
#define TARGET_ASM_UNIQUE_SECTION 	mcore_unique_section
#undef  TARGET_ASM_FUNCTION_RODATA_SECTION
#define TARGET_ASM_FUNCTION_RODATA_SECTION default_no_function_rodata_section
#undef  TARGET_DEFAULT_TARGET_FLAGS
#define TARGET_DEFAULT_TARGET_FLAGS	TARGET_DEFAULT
#undef  TARGET_ENCODE_SECTION_INFO
#define TARGET_ENCODE_SECTION_INFO 	mcore_encode_section_info
#undef  TARGET_STRIP_NAME_ENCODING
#define TARGET_STRIP_NAME_ENCODING	mcore_strip_name_encoding
#undef  TARGET_RTX_COSTS
#define TARGET_RTX_COSTS 		mcore_rtx_costs
#undef  TARGET_ADDRESS_COST
#define TARGET_ADDRESS_COST 		hook_int_rtx_0
#undef  TARGET_MACHINE_DEPENDENT_REORG
#define TARGET_MACHINE_DEPENDENT_REORG	mcore_reorg

#undef  TARGET_PROMOTE_FUNCTION_ARGS
#define TARGET_PROMOTE_FUNCTION_ARGS	hook_bool_tree_true
#undef  TARGET_PROMOTE_FUNCTION_RETURN
#define TARGET_PROMOTE_FUNCTION_RETURN	hook_bool_tree_true
#undef  TARGET_PROMOTE_PROTOTYPES
#define TARGET_PROMOTE_PROTOTYPES	hook_bool_tree_true

#undef  TARGET_RETURN_IN_MEMORY
#define TARGET_RETURN_IN_MEMORY		mcore_return_in_memory
#undef  TARGET_MUST_PASS_IN_STACK
#define TARGET_MUST_PASS_IN_STACK	must_pass_in_stack_var_size
#undef  TARGET_PASS_BY_REFERENCE
#define TARGET_PASS_BY_REFERENCE  hook_pass_by_reference_must_pass_in_stack
#undef  TARGET_ARG_PARTIAL_BYTES
#define TARGET_ARG_PARTIAL_BYTES	mcore_arg_partial_bytes

#undef  TARGET_SETUP_INCOMING_VARARGS
#define TARGET_SETUP_INCOMING_VARARGS	mcore_setup_incoming_varargs

struct gcc_target targetm = TARGET_INITIALIZER;

/* Adjust the stack and return the number of bytes taken to do it.  */
static void
output_stack_adjust (int direction, int size)
{
  /* If extending stack a lot, we do it incrementally.  */
  if (direction < 0 && size > mcore_stack_increment && mcore_stack_increment > 0)
    {
      rtx tmp = gen_rtx_REG (SImode, 1);
      rtx memref;

      emit_insn (gen_movsi (tmp, GEN_INT (mcore_stack_increment)));
      do
	{
	  emit_insn (gen_subsi3 (stack_pointer_rtx, stack_pointer_rtx, tmp));
	  memref = gen_rtx_MEM (SImode, stack_pointer_rtx);
	  MEM_VOLATILE_P (memref) = 1;
	  emit_insn (gen_movsi (memref, stack_pointer_rtx));
	  size -= mcore_stack_increment;
	}
      while (size > mcore_stack_increment);

      /* SIZE is now the residual for the last adjustment,
	 which doesn't require a probe.  */
    }

  if (size)
    {
      rtx insn;
      rtx val = GEN_INT (size);

      if (size > 32)
	{
	  rtx nval = gen_rtx_REG (SImode, 1);
	  emit_insn (gen_movsi (nval, val));
	  val = nval;
	}
      
      if (direction > 0)
	insn = gen_addsi3 (stack_pointer_rtx, stack_pointer_rtx, val);
      else
	insn = gen_subsi3 (stack_pointer_rtx, stack_pointer_rtx, val);
      
      emit_insn (insn);
    }
}

/* Work out the registers which need to be saved,
   both as a mask and a count.  */

static int
calc_live_regs (int * count)
{
  int reg;
  int live_regs_mask = 0;
  
  * count = 0;

  for (reg = 0; reg < FIRST_PSEUDO_REGISTER; reg++)
    {
      if (regs_ever_live[reg] && !call_used_regs[reg])
	{
	  (*count)++;
	  live_regs_mask |= (1 << reg);
	}
    }

  return live_regs_mask;
}

/* Print the operand address in x to the stream.  */

void
mcore_print_operand_address (FILE * stream, rtx x)
{
  switch (GET_CODE (x))
    {
    case REG:
      fprintf (stream, "(%s)", reg_names[REGNO (x)]);
      break;
      
    case PLUS:
      {
	rtx base = XEXP (x, 0);
	rtx index = XEXP (x, 1);

	if (GET_CODE (base) != REG)
	  {
	    /* Ensure that BASE is a register (one of them must be).  */
	    rtx temp = base;
	    base = index;
	    index = temp;
	  }

	switch (GET_CODE (index))
	  {
	  case CONST_INT:
	    fprintf (stream, "(%s," HOST_WIDE_INT_PRINT_DEC ")",
		     reg_names[REGNO(base)], INTVAL (index));
	    break;

	  default:
	    gcc_unreachable ();
	  }
      }

      break;

    default:
      output_addr_const (stream, x);
      break;
    }
}

/* Print operand x (an rtx) in assembler syntax to file stream
   according to modifier code.

   'R'  print the next register or memory location along, i.e. the lsw in
        a double word value
   'O'  print a constant without the #
   'M'  print a constant as its negative
   'P'  print log2 of a power of two
   'Q'  print log2 of an inverse of a power of two
   'U'  print register for ldm/stm instruction
   'X'  print byte number for xtrbN instruction.  */

void
mcore_print_operand (FILE * stream, rtx x, int code)
{
  switch (code)
    {
    case 'N':
      if (INTVAL(x) == -1)
	fprintf (asm_out_file, "32");
      else
	fprintf (asm_out_file, "%d", exact_log2 (INTVAL (x) + 1));
      break;
    case 'P':
      fprintf (asm_out_file, "%d", exact_log2 (INTVAL (x)));
      break;
    case 'Q':
      fprintf (asm_out_file, "%d", exact_log2 (~INTVAL (x)));
      break;
    case 'O':
      fprintf (asm_out_file, HOST_WIDE_INT_PRINT_DEC, INTVAL (x));
      break;
    case 'M':
      fprintf (asm_out_file, HOST_WIDE_INT_PRINT_DEC, - INTVAL (x));
      break;
    case 'R':
      /* Next location along in memory or register.  */
      switch (GET_CODE (x))
	{
	case REG:
	  fputs (reg_names[REGNO (x) + 1], (stream));
	  break;
	case MEM:
	  mcore_print_operand_address
	    (stream, XEXP (adjust_address (x, SImode, 4), 0));
	  break;
	default:
	  gcc_unreachable ();
	}
      break;
    case 'U':
      fprintf (asm_out_file, "%s-%s", reg_names[REGNO (x)],
	       reg_names[REGNO (x) + 3]);
      break;
    case 'x':
      fprintf (asm_out_file, HOST_WIDE_INT_PRINT_HEX, INTVAL (x));
      break;
    case 'X':
      fprintf (asm_out_file, HOST_WIDE_INT_PRINT_DEC, 3 - INTVAL (x) / 8);
      break;

    default:
      switch (GET_CODE (x))
	{
	case REG:
	  fputs (reg_names[REGNO (x)], (stream));
	  break;
	case MEM:
	  output_address (XEXP (x, 0));
	  break;
	default:
	  output_addr_const (stream, x);
	  break;
	}
      break;
    }
}

/* What does a constant cost ?  */

static int
mcore_const_costs (rtx exp, enum rtx_code code)
{
  int val = INTVAL (exp);

  /* Easy constants.  */
  if (   CONST_OK_FOR_I (val)	
      || CONST_OK_FOR_M (val)	
      || CONST_OK_FOR_N (val)	
      || (code == PLUS && CONST_OK_FOR_L (val)))
    return 1;					
  else if (code == AND
	   && (   CONST_OK_FOR_M (~val)
	       || CONST_OK_FOR_N (~val)))
    return 2;
  else if (code == PLUS			
	   && (   CONST_OK_FOR_I (-val)	
	       || CONST_OK_FOR_M (-val)	
	       || CONST_OK_FOR_N (-val)))	
    return 2;						

  return 5;					
}

/* What does an and instruction cost - we do this b/c immediates may 
   have been relaxed.   We want to ensure that cse will cse relaxed immeds
   out.  Otherwise we'll get bad code (multiple reloads of the same const).  */

static int
mcore_and_cost (rtx x)
{
  int val;

  if (GET_CODE (XEXP (x, 1)) != CONST_INT)
    return 2;

  val = INTVAL (XEXP (x, 1));
   
  /* Do it directly.  */
  if (CONST_OK_FOR_K (val) || CONST_OK_FOR_M (~val))
    return 2;
  /* Takes one instruction to load.  */
  else if (const_ok_for_mcore (val))
    return 3;
  /* Takes two instructions to load.  */
  else if (TARGET_HARDLIT && mcore_const_ok_for_inline (val))
    return 4;

  /* Takes a lrw to load.  */
  return 5;
}

/* What does an or cost - see and_cost().  */

static int
mcore_ior_cost (rtx x)
{
  int val;

  if (GET_CODE (XEXP (x, 1)) != CONST_INT)
    return 2;

  val = INTVAL (XEXP (x, 1));

  /* Do it directly with bclri.  */
  if (CONST_OK_FOR_M (val))
    return 2;
  /* Takes one instruction to load.  */
  else if (const_ok_for_mcore (val))
    return 3;
  /* Takes two instructions to load.  */
  else if (TARGET_HARDLIT && mcore_const_ok_for_inline (val))
    return 4;
  
  /* Takes a lrw to load.  */
  return 5;
}

static bool
mcore_rtx_costs (rtx x, int code, int outer_code, int * total)
{
  switch (code)
    {
    case CONST_INT:
      *total = mcore_const_costs (x, outer_code);
      return true;
    case CONST:
    case LABEL_REF:
    case SYMBOL_REF:
      *total = 5;
      return true;
    case CONST_DOUBLE:
      *total = 10;
      return true;

    case AND:
      *total = COSTS_N_INSNS (mcore_and_cost (x));
      return true;

    case IOR:
      *total = COSTS_N_INSNS (mcore_ior_cost (x));
      return true;

    case DIV:
    case UDIV:
    case MOD:
    case UMOD:
    case FLOAT:
    case FIX:
      *total = COSTS_N_INSNS (100);
      return true;
  
    default:
      return false;
    }
}

/* Check to see if a comparison against a constant can be made more efficient
   by incrementing/decrementing the constant to get one that is more efficient
   to load.  */

int
mcore_modify_comparison (enum rtx_code code)
{
  rtx op1 = arch_compare_op1;
  
  if (GET_CODE (op1) == CONST_INT)
    {
      int val = INTVAL (op1);
      
      switch (code)
	{
	case LE:
	  if (CONST_OK_FOR_J (val + 1))
	    {
	      arch_compare_op1 = GEN_INT (val + 1);
	      return 1;
	    }
	  break;
	  
	default:
	  break;
	}
    }
  
  return 0;
}

/* Prepare the operands for a comparison.  */

rtx
mcore_gen_compare_reg (enum rtx_code code)
{
  rtx op0 = arch_compare_op0;
  rtx op1 = arch_compare_op1;
  rtx cc_reg = gen_rtx_REG (CCmode, CC_REG);

  if (CONSTANT_P (op1) && GET_CODE (op1) != CONST_INT)
    op1 = force_reg (SImode, op1);

  /* cmpnei: 0-31 (K immediate)
     cmplti: 1-32 (J immediate, 0 using btsti x,31).  */
  switch (code)
    {
    case EQ:	/* Use inverted condition, cmpne.  */
      code = NE;
      /* Drop through.  */
      
    case NE:	/* Use normal condition, cmpne.  */
      if (GET_CODE (op1) == CONST_INT && ! CONST_OK_FOR_K (INTVAL (op1)))
	op1 = force_reg (SImode, op1);
      break;

    case LE:	/* Use inverted condition, reversed cmplt.  */
      code = GT;
      /* Drop through.  */
      
    case GT:	/* Use normal condition, reversed cmplt.  */
      if (GET_CODE (op1) == CONST_INT)
	op1 = force_reg (SImode, op1);
      break;

    case GE:	/* Use inverted condition, cmplt.  */
      code = LT;
      /* Drop through.  */
      
    case LT:	/* Use normal condition, cmplt.  */
      if (GET_CODE (op1) == CONST_INT && 
	  /* covered by btsti x,31.  */
	  INTVAL (op1) != 0 &&
	  ! CONST_OK_FOR_J (INTVAL (op1)))
	op1 = force_reg (SImode, op1);
      break;

    case GTU:	/* Use inverted condition, cmple.  */
      /* Unsigned > 0 is the same as != 0, but we need to invert the
	 condition, so we want to set code = EQ.  This cannot be done
	 however, as the mcore does not support such a test.  Instead
	 we cope with this case in the "bgtu" pattern itself so we
	 should never reach this point.  */
      gcc_assert (GET_CODE (op1) != CONST_INT || INTVAL (op1) != 0);
      code = LEU;
      /* Drop through.  */
      
    case LEU:	/* Use normal condition, reversed cmphs.  */
      if (GET_CODE (op1) == CONST_INT && INTVAL (op1) != 0)
	op1 = force_reg (SImode, op1);
      break;

    case LTU:	/* Use inverted condition, cmphs.  */
      code = GEU;
      /* Drop through.  */
      
    case GEU:	/* Use normal condition, cmphs.  */
      if (GET_CODE (op1) == CONST_INT && INTVAL (op1) != 0)
	op1 = force_reg (SImode, op1);
      break;

    default:
      break;
    }

  emit_insn (gen_rtx_SET (VOIDmode, cc_reg, gen_rtx_fmt_ee (code, CCmode, op0, op1)));
  
  return cc_reg;
}

int
mcore_symbolic_address_p (rtx x)
{
  switch (GET_CODE (x))
    {
    case SYMBOL_REF:
    case LABEL_REF:
      return 1;
    case CONST:
      x = XEXP (x, 0);
      return (   (GET_CODE (XEXP (x, 0)) == SYMBOL_REF
	       || GET_CODE (XEXP (x, 0)) == LABEL_REF)
	      && GET_CODE (XEXP (x, 1)) == CONST_INT);
    default:
      return 0;
    }
}

/* Functions to output assembly code for a function call.  */

char *
mcore_output_call (rtx operands[], int index)
{
  static char buffer[20];
  rtx addr = operands [index];
  
  if (REG_P (addr))
    {
      if (TARGET_CG_DATA)
	{
	  gcc_assert (mcore_current_function_name);
	  
	  ASM_OUTPUT_CG_EDGE (asm_out_file, mcore_current_function_name,
			      "unknown", 1);
	}

      sprintf (buffer, "jsr\t%%%d", index);
    }
  else
    {
      if (TARGET_CG_DATA)
	{
	  gcc_assert (mcore_current_function_name);
	  gcc_assert (GET_CODE (addr) == SYMBOL_REF);
	  
	  ASM_OUTPUT_CG_EDGE (asm_out_file, mcore_current_function_name,
			      XSTR (addr, 0), 0);
	}
      
      sprintf (buffer, "jbsr\t%%%d", index);
    }

  return buffer;
}

/* Can we load a constant with a single instruction ?  */

int
const_ok_for_mcore (int value)
{
  if (value >= 0 && value <= 127)
    return 1;
  
  /* Try exact power of two.  */
  if ((value & (value - 1)) == 0)
    return 1;
  
  /* Try exact power of two - 1.  */
  if ((value & (value + 1)) == 0)
    return 1;
  
  return 0;
}

/* Can we load a constant inline with up to 2 instructions ?  */

int
mcore_const_ok_for_inline (long value)
{
  int x, y;
   
  return try_constant_tricks (value, & x, & y) > 0;
}

/* Are we loading the constant using a not ?  */

int
mcore_const_trick_uses_not (long value)
{
  int x, y;

  return try_constant_tricks (value, & x, & y) == 2; 
}       

/* Try tricks to load a constant inline and return the trick number if
   success (0 is non-inlinable).
  
   0: not inlinable
   1: single instruction (do the usual thing)
   2: single insn followed by a 'not'
   3: single insn followed by a subi
   4: single insn followed by an addi
   5: single insn followed by rsubi
   6: single insn followed by bseti
   7: single insn followed by bclri
   8: single insn followed by rotli
   9: single insn followed by lsli
   10: single insn followed by ixh
   11: single insn followed by ixw.  */

static int
try_constant_tricks (long value, int * x, int * y)
{
  int i;
  unsigned bit, shf, rot;

  if (const_ok_for_mcore (value))
    return 1;	/* Do the usual thing.  */
  
  if (TARGET_HARDLIT) 
    {
      if (const_ok_for_mcore (~value))
	{
	  *x = ~value;
	  return 2;
	}
      
      for (i = 1; i <= 32; i++)
	{
	  if (const_ok_for_mcore (value - i))
	    {
	      *x = value - i;
	      *y = i;
	      
	      return 3;
	    }
	  
	  if (const_ok_for_mcore (value + i))
	    {
	      *x = value + i;
	      *y = i;
	      
	      return 4;
	    }
	}
      
      bit = 0x80000000L;
      
      for (i = 0; i <= 31; i++)
	{
	  if (const_ok_for_mcore (i - value))
	    {
	      *x = i - value;
	      *y = i;
	      
	      return 5;
	    }
	  
	  if (const_ok_for_mcore (value & ~bit))
	    {
	      *y = bit;
	      *x = value & ~bit;
	      
	      return 6;
	    }
	  
	  if (const_ok_for_mcore (value | bit))
	    {
	      *y = ~bit;
	      *x = value | bit;
	      
	      return 7;
	    }
	  
	  bit >>= 1;
	}
      
      shf = value;
      rot = value;
      
      for (i = 1; i < 31; i++)
	{
	  int c;
	  
	  /* MCore has rotate left.  */
	  c = rot << 31;
	  rot >>= 1;
	  rot &= 0x7FFFFFFF;
	  rot |= c;   /* Simulate rotate.  */
	  
	  if (const_ok_for_mcore (rot))
	    {
	      *y = i;
	      *x = rot;
	      
	      return 8;
	    }
	  
	  if (shf & 1)
	    shf = 0;	/* Can't use logical shift, low order bit is one.  */
	  
	  shf >>= 1;
	  
	  if (shf != 0 && const_ok_for_mcore (shf))
	    {
	      *y = i;
	      *x = shf;
	      
	      return 9;
	    }
	}
      
      if ((value % 3) == 0 && const_ok_for_mcore (value / 3))
	{
	  *x = value / 3;
	  
	  return 10;
	}
      
      if ((value % 5) == 0 && const_ok_for_mcore (value / 5))
	{
	  *x = value / 5;
	  
	  return 11;
	}
    }
  
  return 0;
}

/* Check whether reg is dead at first.  This is done by searching ahead
   for either the next use (i.e., reg is live), a death note, or a set of
   reg.  Don't just use dead_or_set_p() since reload does not always mark 
   deaths (especially if PRESERVE_DEATH_NOTES_REGNO_P is not defined). We
   can ignore subregs by extracting the actual register.  BRC  */

int
mcore_is_dead (rtx first, rtx reg)
{
  rtx insn;

  /* For mcore, subregs can't live independently of their parent regs.  */
  if (GET_CODE (reg) == SUBREG)
    reg = SUBREG_REG (reg);

  /* Dies immediately.  */
  if (dead_or_set_p (first, reg))
    return 1;

  /* Look for conclusive evidence of live/death, otherwise we have
     to assume that it is live.  */
  for (insn = NEXT_INSN (first); insn; insn = NEXT_INSN (insn))
    {
      if (GET_CODE (insn) == JUMP_INSN)
	return 0;	/* We lose track, assume it is alive.  */

      else if (GET_CODE(insn) == CALL_INSN)
	{
	  /* Call's might use it for target or register parms.  */
	  if (reg_referenced_p (reg, PATTERN (insn))
	      || find_reg_fusage (insn, USE, reg))
	    return 0;
	  else if (dead_or_set_p (insn, reg))
            return 1;
	}
      else if (GET_CODE (insn) == INSN)
	{
	  if (reg_referenced_p (reg, PATTERN (insn)))
            return 0;
	  else if (dead_or_set_p (insn, reg))
            return 1;
	}
    }

  /* No conclusive evidence either way, we cannot take the chance
     that control flow hid the use from us -- "I'm not dead yet".  */
  return 0;
}

/* Count the number of ones in mask.  */

int
mcore_num_ones (int mask)
{
  /* A trick to count set bits recently posted on comp.compilers.  */
  mask =  (mask >> 1  & 0x55555555) + (mask & 0x55555555);
  mask = ((mask >> 2) & 0x33333333) + (mask & 0x33333333);
  mask = ((mask >> 4) + mask) & 0x0f0f0f0f;
  mask = ((mask >> 8) + mask);

  return (mask + (mask >> 16)) & 0xff;
}

/* Count the number of zeros in mask.  */

int
mcore_num_zeros (int mask)
{
  return 32 - mcore_num_ones (mask);
}

/* Determine byte being masked.  */

int
mcore_byte_offset (unsigned int mask)
{
  if (mask == 0x00ffffffL)
    return 0;
  else if (mask == 0xff00ffffL)
    return 1;
  else if (mask == 0xffff00ffL)
    return 2;
  else if (mask == 0xffffff00L)
    return 3;

  return -1;
}

/* Determine halfword being masked.  */

int
mcore_halfword_offset (unsigned int mask)
{
  if (mask == 0x0000ffffL)
    return 0;
  else if (mask == 0xffff0000L)
    return 1;

  return -1;
}

/* Output a series of bseti's corresponding to mask.  */

const char *
mcore_output_bseti (rtx dst, int mask)
{
  rtx out_operands[2];
  int bit;

  out_operands[0] = dst;

  for (bit = 0; bit < 32; bit++)
    {
      if ((mask & 0x1) == 0x1)
	{
	  out_operands[1] = GEN_INT (bit);
	  
	  output_asm_insn ("bseti\t%0,%1", out_operands);
	}
      mask >>= 1;
    }  

  return "";
}

/* Output a series of bclri's corresponding to mask.  */

const char *
mcore_output_bclri (rtx dst, int mask)
{
  rtx out_operands[2];
  int bit;

  out_operands[0] = dst;

  for (bit = 0; bit < 32; bit++)
    {
      if ((mask & 0x1) == 0x0)
	{
	  out_operands[1] = GEN_INT (bit);
	  
	  output_asm_insn ("bclri\t%0,%1", out_operands);
	}
      
      mask >>= 1;
    }  

  return "";
}

/* Output a conditional move of two constants that are +/- 1 within each
   other.  See the "movtK" patterns in mcore.md.   I'm not sure this is
   really worth the effort.  */

const char *
mcore_output_cmov (rtx operands[], int cmp_t, const char * test)
{
  int load_value;
  int adjust_value;
  rtx out_operands[4];

  out_operands[0] = operands[0];

  /* Check to see which constant is loadable.  */
  if (const_ok_for_mcore (INTVAL (operands[1])))
    {
      out_operands[1] = operands[1];
      out_operands[2] = operands[2];
    }
  else if (const_ok_for_mcore (INTVAL (operands[2])))
    {
      out_operands[1] = operands[2];
      out_operands[2] = operands[1];

      /* Complement test since constants are swapped.  */
      cmp_t = (cmp_t == 0);
    }
  load_value   = INTVAL (out_operands[1]);
  adjust_value = INTVAL (out_operands[2]);

  /* First output the test if folded into the pattern.  */

  if (test) 
    output_asm_insn (test, operands);

  /* Load the constant - for now, only support constants that can be
     generated with a single instruction.  maybe add general inlinable
     constants later (this will increase the # of patterns since the
     instruction sequence has a different length attribute).  */
  if (load_value >= 0 && load_value <= 127)
    output_asm_insn ("movi\t%0,%1", out_operands);
  else if ((load_value & (load_value - 1)) == 0)
    output_asm_insn ("bgeni\t%0,%P1", out_operands);
  else if ((load_value & (load_value + 1)) == 0)
    output_asm_insn ("bmaski\t%0,%N1", out_operands);
   
  /* Output the constant adjustment.  */
  if (load_value > adjust_value)
    {
      if (cmp_t)
	output_asm_insn ("decf\t%0", out_operands);
      else
	output_asm_insn ("dect\t%0", out_operands);
    }
  else
    {
      if (cmp_t)
	output_asm_insn ("incf\t%0", out_operands);
      else
	output_asm_insn ("inct\t%0", out_operands);
    }

  return "";
}

/* Outputs the peephole for moving a constant that gets not'ed followed 
   by an and (i.e. combine the not and the and into andn). BRC  */

const char *
mcore_output_andn (rtx insn ATTRIBUTE_UNUSED, rtx operands[])
{
  int x, y;
  rtx out_operands[3];
  const char * load_op;
  char buf[256];
  int trick_no;

  trick_no = try_constant_tricks (INTVAL (operands[1]), &x, &y);
  gcc_assert (trick_no == 2);

  out_operands[0] = operands[0];
  out_operands[1] = GEN_INT(x);
  out_operands[2] = operands[2];

  if (x >= 0 && x <= 127)
    load_op = "movi\t%0,%1";
  
  /* Try exact power of two.  */
  else if ((x & (x - 1)) == 0)
    load_op = "bgeni\t%0,%P1";
  
  /* Try exact power of two - 1.  */
  else if ((x & (x + 1)) == 0)
    load_op = "bmaski\t%0,%N1";
  
  else 
    load_op = "BADMOVI\t%0,%1";

  sprintf (buf, "%s\n\tandn\t%%2,%%0", load_op);
  output_asm_insn (buf, out_operands);

  return "";
}

/* Output an inline constant.  */

static const char *
output_inline_const (enum machine_mode mode, rtx operands[])
{
  int x = 0, y = 0;
  int trick_no;
  rtx out_operands[3];
  char buf[256];
  char load_op[256];
  const char *dst_fmt;
  int value;

  value = INTVAL (operands[1]);

  trick_no = try_constant_tricks (value, &x, &y);
  /* lrw's are handled separately: Large inlinable constants never get
     turned into lrw's.  Our caller uses try_constant_tricks to back
     off to an lrw rather than calling this routine.  */
  gcc_assert (trick_no != 0);
  
  if (trick_no == 1)
    x = value;

  /* operands: 0 = dst, 1 = load immed., 2 = immed. adjustment.  */
  out_operands[0] = operands[0];
  out_operands[1] = GEN_INT (x);
  
  if (trick_no > 2)
    out_operands[2] = GEN_INT (y);

  /* Select dst format based on mode.  */
  if (mode == DImode && (! TARGET_LITTLE_END))
    dst_fmt = "%R0";
  else
    dst_fmt = "%0";

  if (x >= 0 && x <= 127)
    sprintf (load_op, "movi\t%s,%%1", dst_fmt);
  
  /* Try exact power of two.  */
  else if ((x & (x - 1)) == 0)
    sprintf (load_op, "bgeni\t%s,%%P1", dst_fmt);
  
  /* Try exact power of two - 1.  */
  else if ((x & (x + 1)) == 0)
    sprintf (load_op, "bmaski\t%s,%%N1", dst_fmt);
  
  else 
    sprintf (load_op, "BADMOVI\t%s,%%1", dst_fmt);

  switch (trick_no)
    {
    case 1:
      strcpy (buf, load_op);
      break;
    case 2:   /* not */
      sprintf (buf, "%s\n\tnot\t%s\t// %d 0x%x", load_op, dst_fmt, value, value);
      break;
    case 3:   /* add */
      sprintf (buf, "%s\n\taddi\t%s,%%2\t// %d 0x%x", load_op, dst_fmt, value, value);
      break;
    case 4:   /* sub */
      sprintf (buf, "%s\n\tsubi\t%s,%%2\t// %d 0x%x", load_op, dst_fmt, value, value);
      break;
    case 5:   /* rsub */
      /* Never happens unless -mrsubi, see try_constant_tricks().  */
      sprintf (buf, "%s\n\trsubi\t%s,%%2\t// %d 0x%x", load_op, dst_fmt, value, value);
      break;
    case 6:   /* bset */
      sprintf (buf, "%s\n\tbseti\t%s,%%P2\t// %d 0x%x", load_op, dst_fmt, value, value);
      break;
    case 7:   /* bclr */
      sprintf (buf, "%s\n\tbclri\t%s,%%Q2\t// %d 0x%x", load_op, dst_fmt, value, value);
      break;
    case 8:   /* rotl */
      sprintf (buf, "%s\n\trotli\t%s,%%2\t// %d 0x%x", load_op, dst_fmt, value, value);
      break;
    case 9:   /* lsl */
      sprintf (buf, "%s\n\tlsli\t%s,%%2\t// %d 0x%x", load_op, dst_fmt, value, value);
      break;
    case 10:  /* ixh */
      sprintf (buf, "%s\n\tixh\t%s,%s\t// %d 0x%x", load_op, dst_fmt, dst_fmt, value, value);
      break;
    case 11:  /* ixw */
      sprintf (buf, "%s\n\tixw\t%s,%s\t// %d 0x%x", load_op, dst_fmt, dst_fmt, value, value);
      break;
    default:
      return "";
    }
  
  output_asm_insn (buf, out_operands);

  return "";
}

/* Output a move of a word or less value.  */

const char *
mcore_output_move (rtx insn ATTRIBUTE_UNUSED, rtx operands[],
		   enum machine_mode mode ATTRIBUTE_UNUSED)
{
  rtx dst = operands[0];
  rtx src = operands[1];

  if (GET_CODE (dst) == REG)
    {
      if (GET_CODE (src) == REG)
	{               
	  if (REGNO (src) == CC_REG)            /* r-c */
            return "mvc\t%0"; 
	  else 
            return "mov\t%0,%1";                /* r-r*/
	}
      else if (GET_CODE (src) == MEM)
	{
	  if (GET_CODE (XEXP (src, 0)) == LABEL_REF) 
            return "lrw\t%0,[%1]";              /* a-R */
	  else
	    switch (GET_MODE (src))		/* r-m */
	      {
	      case SImode:
		return "ldw\t%0,%1";
	      case HImode:
		return "ld.h\t%0,%1";
	      case QImode:
		return "ld.b\t%0,%1";
	      default:
		gcc_unreachable ();
	      }
	}
      else if (GET_CODE (src) == CONST_INT)
	{
	  int x, y;
	  
	  if (CONST_OK_FOR_I (INTVAL (src)))       /* r-I */
            return "movi\t%0,%1";
	  else if (CONST_OK_FOR_M (INTVAL (src)))  /* r-M */
            return "bgeni\t%0,%P1\t// %1 %x1";
	  else if (CONST_OK_FOR_N (INTVAL (src)))  /* r-N */
            return "bmaski\t%0,%N1\t// %1 %x1";
	  else if (try_constant_tricks (INTVAL (src), &x, &y))     /* R-P */
            return output_inline_const (SImode, operands);  /* 1-2 insns */
	  else 
            return "lrw\t%0,%x1\t// %1";	/* Get it from literal pool.  */
	}
      else
	return "lrw\t%0, %1";                /* Into the literal pool.  */
    }
  else if (GET_CODE (dst) == MEM)               /* m-r */
    switch (GET_MODE (dst))
      {
      case SImode:
	return "stw\t%1,%0";
      case HImode:
	return "st.h\t%1,%0";
      case QImode:
	return "st.b\t%1,%0";
      default:
	gcc_unreachable ();
      }

  gcc_unreachable ();
}

/* Return a sequence of instructions to perform DI or DF move.
   Since the MCORE cannot move a DI or DF in one instruction, we have
   to take care when we see overlapping source and dest registers.  */

const char *
mcore_output_movedouble (rtx operands[], enum machine_mode mode ATTRIBUTE_UNUSED)
{
  rtx dst = operands[0];
  rtx src = operands[1];

  if (GET_CODE (dst) == REG)
    {
      if (GET_CODE (src) == REG)
	{
	  int dstreg = REGNO (dst);
	  int srcreg = REGNO (src);
	  
	  /* Ensure the second source not overwritten.  */
	  if (srcreg + 1 == dstreg)
	    return "mov	%R0,%R1\n\tmov	%0,%1";
	  else
	    return "mov	%0,%1\n\tmov	%R0,%R1";
	}
      else if (GET_CODE (src) == MEM)
	{
	  rtx memexp = memexp = XEXP (src, 0);
	  int dstreg = REGNO (dst);
	  int basereg = -1;
	  
	  if (GET_CODE (memexp) == LABEL_REF)
	    return "lrw\t%0,[%1]\n\tlrw\t%R0,[%R1]";
	  else if (GET_CODE (memexp) == REG) 
	    basereg = REGNO (memexp);
	  else if (GET_CODE (memexp) == PLUS)
	    {
	      if (GET_CODE (XEXP (memexp, 0)) == REG)
		basereg = REGNO (XEXP (memexp, 0));
	      else if (GET_CODE (XEXP (memexp, 1)) == REG)
		basereg = REGNO (XEXP (memexp, 1));
	      else
		gcc_unreachable ();
	    }
	  else
	    gcc_unreachable ();

          /* ??? length attribute is wrong here.  */
	  if (dstreg == basereg)
	    {
	      /* Just load them in reverse order.  */
	      return "ldw\t%R0,%R1\n\tldw\t%0,%1";
	      
	      /* XXX: alternative: move basereg to basereg+1
	         and then fall through.  */
	    }
	  else
	    return "ldw\t%0,%1\n\tldw\t%R0,%R1";
	}
      else if (GET_CODE (src) == CONST_INT)
	{
	  if (TARGET_LITTLE_END)
	    {
	      if (CONST_OK_FOR_I (INTVAL (src)))
		output_asm_insn ("movi	%0,%1", operands);
	      else if (CONST_OK_FOR_M (INTVAL (src)))
		output_asm_insn ("bgeni	%0,%P1", operands);
	      else if (INTVAL (src) == -1)
		output_asm_insn ("bmaski	%0,32", operands);
	      else if (CONST_OK_FOR_N (INTVAL (src)))
		output_asm_insn ("bmaski	%0,%N1", operands);
	      else
		gcc_unreachable ();

	      if (INTVAL (src) < 0)
		return "bmaski	%R0,32";
	      else
		return "movi	%R0,0";
	    }
	  else
	    {
	      if (CONST_OK_FOR_I (INTVAL (src)))
		output_asm_insn ("movi	%R0,%1", operands);
	      else if (CONST_OK_FOR_M (INTVAL (src)))
		output_asm_insn ("bgeni	%R0,%P1", operands);
	      else if (INTVAL (src) == -1)
		output_asm_insn ("bmaski	%R0,32", operands);
	      else if (CONST_OK_FOR_N (INTVAL (src)))
		output_asm_insn ("bmaski	%R0,%N1", operands);
	      else
		gcc_unreachable ();
	      
	      if (INTVAL (src) < 0)
		return "bmaski	%0,32";
	      else
		return "movi	%0,0";
	    }
	}
      else
	gcc_unreachable ();
    }
  else if (GET_CODE (dst) == MEM && GET_CODE (src) == REG)
    return "stw\t%1,%0\n\tstw\t%R1,%R0";
  else
    gcc_unreachable ();
}

/* Predicates used by the templates.  */

int
mcore_arith_S_operand (rtx op)
{
  if (GET_CODE (op) == CONST_INT && CONST_OK_FOR_M (~INTVAL (op)))
    return 1;
  
  return 0;
}

/* Expand insert bit field.  BRC  */

int
mcore_expand_insv (rtx operands[])
{
  int width = INTVAL (operands[1]);
  int posn = INTVAL (operands[2]);
  int mask;
  rtx mreg, sreg, ereg;

  /* To get width 1 insv, the test in store_bit_field() (expmed.c, line 191)
     for width==1 must be removed.  Look around line 368.  This is something
     we really want the md part to do.  */
  if (width == 1 && GET_CODE (operands[3]) == CONST_INT)
    {
      /* Do directly with bseti or bclri.  */
      /* RBE: 2/97 consider only low bit of constant.  */
      if ((INTVAL(operands[3])&1) == 0)
	{
	  mask = ~(1 << posn);
	  emit_insn (gen_rtx_SET (SImode, operands[0],
			      gen_rtx_AND (SImode, operands[0], GEN_INT (mask))));
	}
      else
	{
	  mask = 1 << posn;
	  emit_insn (gen_rtx_SET (SImode, operands[0],
			    gen_rtx_IOR (SImode, operands[0], GEN_INT (mask))));
	}
      
      return 1;
    }

  /* Look at some bit-field placements that we aren't interested
     in handling ourselves, unless specifically directed to do so.  */
  if (! TARGET_W_FIELD)
    return 0;		/* Generally, give up about now.  */

  if (width == 8 && posn % 8 == 0)
    /* Byte sized and aligned; let caller break it up.  */
    return 0;
  
  if (width == 16 && posn % 16 == 0)
    /* Short sized and aligned; let caller break it up.  */
    return 0;

  /* The general case - we can do this a little bit better than what the
     machine independent part tries.  This will get rid of all the subregs
     that mess up constant folding in combine when working with relaxed
     immediates.  */

  /* If setting the entire field, do it directly.  */
  if (GET_CODE (operands[3]) == CONST_INT && 
      INTVAL (operands[3]) == ((1 << width) - 1))
    {
      mreg = force_reg (SImode, GEN_INT (INTVAL (operands[3]) << posn));
      emit_insn (gen_rtx_SET (SImode, operands[0],
                         gen_rtx_IOR (SImode, operands[0], mreg)));
      return 1;
    }

  /* Generate the clear mask.  */
  mreg = force_reg (SImode, GEN_INT (~(((1 << width) - 1) << posn)));

  /* Clear the field, to overlay it later with the source.  */
  emit_insn (gen_rtx_SET (SImode, operands[0], 
		      gen_rtx_AND (SImode, operands[0], mreg)));

  /* If the source is constant 0, we've nothing to add back.  */
  if (GET_CODE (operands[3]) == CONST_INT && INTVAL (operands[3]) == 0)
    return 1;

  /* XXX: Should we worry about more games with constant values?
     We've covered the high profile: set/clear single-bit and many-bit
     fields. How often do we see "arbitrary bit pattern" constants?  */
  sreg = copy_to_mode_reg (SImode, operands[3]);

  /* Extract src as same width as dst (needed for signed values).  We
     always have to do this since we widen everything to SImode.
     We don't have to mask if we're shifting this up against the
     MSB of the register (e.g., the shift will push out any hi-order
     bits.  */
  if (width + posn != (int) GET_MODE_SIZE (SImode))
    {
      ereg = force_reg (SImode, GEN_INT ((1 << width) - 1));      
      emit_insn (gen_rtx_SET (SImode, sreg,
                          gen_rtx_AND (SImode, sreg, ereg)));
    }

  /* Insert source value in dest.  */
  if (posn != 0)
    emit_insn (gen_rtx_SET (SImode, sreg,
		        gen_rtx_ASHIFT (SImode, sreg, GEN_INT (posn))));
  
  emit_insn (gen_rtx_SET (SImode, operands[0],
		      gen_rtx_IOR (SImode, operands[0], sreg)));

  return 1;
}

/* ??? Block move stuff stolen from m88k.  This code has not been
   verified for correctness.  */

/* Emit code to perform a block move.  Choose the best method.

   OPERANDS[0] is the destination.
   OPERANDS[1] is the source.
   OPERANDS[2] is the size.
   OPERANDS[3] is the alignment safe to use.  */

/* Emit code to perform a block move with an offset sequence of ldw/st
   instructions (..., ldw 0, stw 1, ldw 1, stw 0, ...).  SIZE and ALIGN are
   known constants.  DEST and SRC are registers.  OFFSET is the known
   starting point for the output pattern.  */

static const enum machine_mode mode_from_align[] =
{
  VOIDmode, QImode, HImode, VOIDmode, SImode,
};

static void
block_move_sequence (rtx dst_mem, rtx src_mem, int size, int align)
{
  rtx temp[2];
  enum machine_mode mode[2];
  int amount[2];
  bool active[2];
  int phase = 0;
  int next;
  int offset_ld = 0;
  int offset_st = 0;
  rtx x;

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

      if (size > 0)
	{
	  int next_amount;

	  next_amount = (size >= 4 ? 4 : (size >= 2 ? 2 : 1));
	  next_amount = MIN (next_amount, align);

	  amount[next] = next_amount;
	  mode[next] = mode_from_align[next_amount];
	  temp[next] = gen_reg_rtx (mode[next]);

	  x = adjust_address (src_mem, mode[next], offset_ld);
	  emit_insn (gen_rtx_SET (VOIDmode, temp[next], x));

	  offset_ld += next_amount;
	  size -= next_amount;
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
}

bool
mcore_expand_block_move (rtx *operands)
{
  HOST_WIDE_INT align, bytes, max;

  if (GET_CODE (operands[2]) != CONST_INT)
    return false;

  bytes = INTVAL (operands[2]);
  align = INTVAL (operands[3]);

  if (bytes <= 0)
    return false;
  if (align > 4)
    align = 4;

  switch (align)
    {
    case 4:
      if (bytes & 1)
	max = 4*4;
      else if (bytes & 3)
	max = 8*4;
      else
	max = 16*4;
      break;
    case 2:
      max = 4*2;
      break;
    case 1:
      max = 4*1;
      break;
    default:
      gcc_unreachable ();
    }

  if (bytes <= max)
    {
      block_move_sequence (operands[0], operands[1], bytes, align);
      return true;
    }

  return false;
}


/* Code to generate prologue and epilogue sequences.  */
static int number_of_regs_before_varargs;

/* Set by TARGET_SETUP_INCOMING_VARARGS to indicate to prolog that this is
   for a varargs function.  */
static int current_function_anonymous_args;

#define	STACK_BYTES (STACK_BOUNDARY/BITS_PER_UNIT)
#define	STORE_REACH (64)	/* Maximum displace of word store + 4.  */
#define	ADDI_REACH (32)		/* Maximum addi operand.  */

static void
layout_mcore_frame (struct mcore_frame * infp)
{
  int n;
  unsigned int i;
  int nbytes;
  int regarg;
  int localregarg;
  int localreg;
  int outbounds;
  unsigned int growths;
  int step;

  /* Might have to spill bytes to re-assemble a big argument that
     was passed partially in registers and partially on the stack.  */
  nbytes = current_function_pretend_args_size;
  
  /* Determine how much space for spilled anonymous args (e.g., stdarg).  */
  if (current_function_anonymous_args)
    nbytes += (NPARM_REGS - number_of_regs_before_varargs) * UNITS_PER_WORD;
  
  infp->arg_size = nbytes;

  /* How much space to save non-volatile registers we stomp.  */
  infp->reg_mask = calc_live_regs (& n);
  infp->reg_size = n * 4;

  /* And the rest of it... locals and space for overflowed outbounds.  */
  infp->local_size = get_frame_size ();
  infp->outbound_size = current_function_outgoing_args_size;

  /* Make sure we have a whole number of words for the locals.  */
  if (infp->local_size % STACK_BYTES)
    infp->local_size = (infp->local_size + STACK_BYTES - 1) & ~ (STACK_BYTES -1);
  
  /* Only thing we know we have to pad is the outbound space, since
     we've aligned our locals assuming that base of locals is aligned.  */
  infp->pad_local = 0;
  infp->pad_reg = 0;
  infp->pad_outbound = 0;
  if (infp->outbound_size % STACK_BYTES)
    infp->pad_outbound = STACK_BYTES - (infp->outbound_size % STACK_BYTES);

  /* Now we see how we want to stage the prologue so that it does
     the most appropriate stack growth and register saves to either:
     (1) run fast,
     (2) reduce instruction space, or
     (3) reduce stack space.  */
  for (i = 0; i < ARRAY_SIZE (infp->growth); i++)
    infp->growth[i] = 0;

  regarg      = infp->reg_size + infp->arg_size;
  localregarg = infp->local_size + regarg;
  localreg    = infp->local_size + infp->reg_size;
  outbounds   = infp->outbound_size + infp->pad_outbound;
  growths     = 0;

  /* XXX: Consider one where we consider localregarg + outbound too! */

  /* Frame of <= 32 bytes and using stm would get <= 2 registers.
     use stw's with offsets and buy the frame in one shot.  */
  if (localregarg <= ADDI_REACH
      && (infp->reg_size <= 8 || (infp->reg_mask & 0xc000) != 0xc000))
    {
      /* Make sure we'll be aligned.  */
      if (localregarg % STACK_BYTES)
	infp->pad_reg = STACK_BYTES - (localregarg % STACK_BYTES);

      step = localregarg + infp->pad_reg;
      infp->reg_offset = infp->local_size;
      
      if (outbounds + step <= ADDI_REACH && !frame_pointer_needed)
	{
	  step += outbounds;
	  infp->reg_offset += outbounds;
	  outbounds = 0;
	}
      
      infp->arg_offset = step - 4;
      infp->growth[growths++] = step;
      infp->reg_growth = growths;
      infp->local_growth = growths;
      
      /* If we haven't already folded it in.  */
      if (outbounds)
	infp->growth[growths++] = outbounds;
      
      goto finish;
    }

  /* Frame can't be done with a single subi, but can be done with 2
     insns.  If the 'stm' is getting <= 2 registers, we use stw's and
     shift some of the stack purchase into the first subi, so both are
     single instructions.  */
  if (localregarg <= STORE_REACH
      && (infp->local_size > ADDI_REACH)
      && (infp->reg_size <= 8 || (infp->reg_mask & 0xc000) != 0xc000))
    {
      int all;

      /* Make sure we'll be aligned; use either pad_reg or pad_local.  */
      if (localregarg % STACK_BYTES)
	infp->pad_reg = STACK_BYTES - (localregarg % STACK_BYTES);

      all = localregarg + infp->pad_reg + infp->pad_local;
      step = ADDI_REACH;	/* As much up front as we can.  */
      if (step > all)
	step = all;
      
      /* XXX: Consider whether step will still be aligned; we believe so.  */
      infp->arg_offset = step - 4;
      infp->growth[growths++] = step;
      infp->reg_growth = growths;
      infp->reg_offset = step - infp->pad_reg - infp->reg_size;
      all -= step;

      /* Can we fold in any space required for outbounds?  */
      if (outbounds + all <= ADDI_REACH && !frame_pointer_needed)
	{
	  all += outbounds;
	  outbounds = 0;
	}

      /* Get the rest of the locals in place.  */
      step = all;
      infp->growth[growths++] = step;
      infp->local_growth = growths;
      all -= step;

      assert (all == 0);

      /* Finish off if we need to do so.  */
      if (outbounds)
	infp->growth[growths++] = outbounds;
      
      goto finish;
    }

  /* Registers + args is nicely aligned, so we'll buy that in one shot.
     Then we buy the rest of the frame in 1 or 2 steps depending on
     whether we need a frame pointer.  */
  if ((regarg % STACK_BYTES) == 0)
    {
      infp->growth[growths++] = regarg;
      infp->reg_growth = growths;
      infp->arg_offset = regarg - 4;
      infp->reg_offset = 0;

      if (infp->local_size % STACK_BYTES)
	infp->pad_local = STACK_BYTES - (infp->local_size % STACK_BYTES);
      
      step = infp->local_size + infp->pad_local;
      
      if (!frame_pointer_needed)
	{
	  step += outbounds;
	  outbounds = 0;
	}
      
      infp->growth[growths++] = step;
      infp->local_growth = growths;

      /* If there's any left to be done.  */
      if (outbounds)
	infp->growth[growths++] = outbounds;
      
      goto finish;
    }

  /* XXX: optimizations that we'll want to play with....
     -- regarg is not aligned, but it's a small number of registers;
    	use some of localsize so that regarg is aligned and then 
    	save the registers.  */

  /* Simple encoding; plods down the stack buying the pieces as it goes.
     -- does not optimize space consumption.
     -- does not attempt to optimize instruction counts.
     -- but it is safe for all alignments.  */
  if (regarg % STACK_BYTES != 0)
    infp->pad_reg = STACK_BYTES - (regarg % STACK_BYTES);
  
  infp->growth[growths++] = infp->arg_size + infp->reg_size + infp->pad_reg;
  infp->reg_growth = growths;
  infp->arg_offset = infp->growth[0] - 4;
  infp->reg_offset = 0;
  
  if (frame_pointer_needed)
    {
      if (infp->local_size % STACK_BYTES != 0)
	infp->pad_local = STACK_BYTES - (infp->local_size % STACK_BYTES);
      
      infp->growth[growths++] = infp->local_size + infp->pad_local;
      infp->local_growth = growths;
      
      infp->growth[growths++] = outbounds;
    }
  else
    {
      if ((infp->local_size + outbounds) % STACK_BYTES != 0)
	infp->pad_local = STACK_BYTES - ((infp->local_size + outbounds) % STACK_BYTES);
      
      infp->growth[growths++] = infp->local_size + infp->pad_local + outbounds;
      infp->local_growth = growths;
    }

  /* Anything else that we've forgotten?, plus a few consistency checks.  */
 finish:
  assert (infp->reg_offset >= 0);
  assert (growths <= MAX_STACK_GROWS);
  
  for (i = 0; i < growths; i++)
    gcc_assert (!(infp->growth[i] % STACK_BYTES));
}

/* Define the offset between two registers, one to be eliminated, and
   the other its replacement, at the start of a routine.  */

int
mcore_initial_elimination_offset (int from, int to)
{
  int above_frame;
  int below_frame;
  struct mcore_frame fi;

  layout_mcore_frame (& fi);

  /* fp to ap */
  above_frame = fi.local_size + fi.pad_local + fi.reg_size + fi.pad_reg;
  /* sp to fp */
  below_frame = fi.outbound_size + fi.pad_outbound;

  if (from == ARG_POINTER_REGNUM && to == FRAME_POINTER_REGNUM)
    return above_frame;

  if (from == ARG_POINTER_REGNUM && to == STACK_POINTER_REGNUM)
    return above_frame + below_frame;

  if (from == FRAME_POINTER_REGNUM && to == STACK_POINTER_REGNUM)
    return below_frame;

  gcc_unreachable ();
}

/* Keep track of some information about varargs for the prolog.  */

static void
mcore_setup_incoming_varargs (CUMULATIVE_ARGS *args_so_far,
			      enum machine_mode mode, tree type,
			      int * ptr_pretend_size ATTRIBUTE_UNUSED,
			      int second_time ATTRIBUTE_UNUSED)
{
  current_function_anonymous_args = 1;

  /* We need to know how many argument registers are used before
     the varargs start, so that we can push the remaining argument
     registers during the prologue.  */
  number_of_regs_before_varargs = *args_so_far + mcore_num_arg_regs (mode, type);
  
  /* There is a bug somewhere in the arg handling code.
     Until I can find it this workaround always pushes the
     last named argument onto the stack.  */
  number_of_regs_before_varargs = *args_so_far;
  
  /* The last named argument may be split between argument registers
     and the stack.  Allow for this here.  */
  if (number_of_regs_before_varargs > NPARM_REGS)
    number_of_regs_before_varargs = NPARM_REGS;
}

void
mcore_expand_prolog (void)
{
  struct mcore_frame fi;
  int space_allocated = 0;
  int growth = 0;

  /* Find out what we're doing.  */
  layout_mcore_frame (&fi);
  
  space_allocated = fi.arg_size + fi.reg_size + fi.local_size +
    fi.outbound_size + fi.pad_outbound + fi.pad_local + fi.pad_reg;

  if (TARGET_CG_DATA)
    {
      /* Emit a symbol for this routine's frame size.  */
      rtx x;

      x = DECL_RTL (current_function_decl);
      
      gcc_assert (GET_CODE (x) == MEM);
      
      x = XEXP (x, 0);
      
      gcc_assert (GET_CODE (x) == SYMBOL_REF);
      
      if (mcore_current_function_name)
	free (mcore_current_function_name);
      
      mcore_current_function_name = xstrdup (XSTR (x, 0));
      
      ASM_OUTPUT_CG_NODE (asm_out_file, mcore_current_function_name, space_allocated);

      if (current_function_calls_alloca)
	ASM_OUTPUT_CG_EDGE (asm_out_file, mcore_current_function_name, "alloca", 1);

      /* 970425: RBE:
         We're looking at how the 8byte alignment affects stack layout
         and where we had to pad things. This emits information we can
         extract which tells us about frame sizes and the like.  */
      fprintf (asm_out_file,
	       "\t.equ\t__$frame$info$_%s_$_%d_%d_x%x_%d_%d_%d,0\n",
	       mcore_current_function_name,
	       fi.arg_size, fi.reg_size, fi.reg_mask,
	       fi.local_size, fi.outbound_size,
	       frame_pointer_needed);
    }

  if (mcore_naked_function_p ())
    return;
  
  /* Handle stdarg+regsaves in one shot: can't be more than 64 bytes.  */
  output_stack_adjust (-1, fi.growth[growth++]);	/* Grows it.  */

  /* If we have a parameter passed partially in regs and partially in memory,
     the registers will have been stored to memory already in function.c.  So
     we only need to do something here for varargs functions.  */
  if (fi.arg_size != 0 && current_function_pretend_args_size == 0)
    {
      int offset;
      int rn = FIRST_PARM_REG + NPARM_REGS - 1;
      int remaining = fi.arg_size;

      for (offset = fi.arg_offset; remaining >= 4; offset -= 4, rn--, remaining -= 4)
        {
          emit_insn (gen_movsi
                     (gen_rtx_MEM (SImode,
                               plus_constant (stack_pointer_rtx, offset)),
                      gen_rtx_REG (SImode, rn)));
        }
    }

  /* Do we need another stack adjustment before we do the register saves?  */
  if (growth < fi.reg_growth)
    output_stack_adjust (-1, fi.growth[growth++]);		/* Grows it.  */

  if (fi.reg_size != 0)
    {
      int i;
      int offs = fi.reg_offset;
      
      for (i = 15; i >= 0; i--)
        {
          if (offs == 0 && i == 15 && ((fi.reg_mask & 0xc000) == 0xc000))
	    {
	      int first_reg = 15;

	      while (fi.reg_mask & (1 << first_reg))
	        first_reg--;
	      first_reg++;

	      emit_insn (gen_store_multiple (gen_rtx_MEM (SImode, stack_pointer_rtx),
					     gen_rtx_REG (SImode, first_reg),
					     GEN_INT (16 - first_reg)));

	      i -= (15 - first_reg);
	      offs += (16 - first_reg) * 4;
	    }
          else if (fi.reg_mask & (1 << i))
	    {
	      emit_insn (gen_movsi
		         (gen_rtx_MEM (SImode,
			           plus_constant (stack_pointer_rtx, offs)),
		          gen_rtx_REG (SImode, i)));
	      offs += 4;
	    }
        }
    }

  /* Figure the locals + outbounds.  */
  if (frame_pointer_needed)
    {
      /* If we haven't already purchased to 'fp'.  */
      if (growth < fi.local_growth)
        output_stack_adjust (-1, fi.growth[growth++]);		/* Grows it.  */
      
      emit_insn (gen_movsi (frame_pointer_rtx, stack_pointer_rtx));

      /* ... and then go any remaining distance for outbounds, etc.  */
      if (fi.growth[growth])
        output_stack_adjust (-1, fi.growth[growth++]);
    }
  else
    {
      if (growth < fi.local_growth)
        output_stack_adjust (-1, fi.growth[growth++]);		/* Grows it.  */
      if (fi.growth[growth])
        output_stack_adjust (-1, fi.growth[growth++]);
    }
}

void
mcore_expand_epilog (void)
{
  struct mcore_frame fi;
  int i;
  int offs;
  int growth = MAX_STACK_GROWS - 1 ;

    
  /* Find out what we're doing.  */
  layout_mcore_frame(&fi);

  if (mcore_naked_function_p ())
    return;

  /* If we had a frame pointer, restore the sp from that.  */
  if (frame_pointer_needed)
    {
      emit_insn (gen_movsi (stack_pointer_rtx, frame_pointer_rtx));
      growth = fi.local_growth - 1;
    }
  else
    {
      /* XXX: while loop should accumulate and do a single sell.  */
      while (growth >= fi.local_growth)
        {
          if (fi.growth[growth] != 0)
            output_stack_adjust (1, fi.growth[growth]);
	  growth--;
        }
    }

  /* Make sure we've shrunk stack back to the point where the registers
     were laid down. This is typically 0/1 iterations.  Then pull the
     register save information back off the stack.  */
  while (growth >= fi.reg_growth)
    output_stack_adjust ( 1, fi.growth[growth--]);
  
  offs = fi.reg_offset;
  
  for (i = 15; i >= 0; i--)
    {
      if (offs == 0 && i == 15 && ((fi.reg_mask & 0xc000) == 0xc000))
	{
	  int first_reg;

	  /* Find the starting register.  */
	  first_reg = 15;
	  
	  while (fi.reg_mask & (1 << first_reg))
	    first_reg--;
	  
	  first_reg++;

	  emit_insn (gen_load_multiple (gen_rtx_REG (SImode, first_reg),
					gen_rtx_MEM (SImode, stack_pointer_rtx),
					GEN_INT (16 - first_reg)));

	  i -= (15 - first_reg);
	  offs += (16 - first_reg) * 4;
	}
      else if (fi.reg_mask & (1 << i))
	{
	  emit_insn (gen_movsi
		     (gen_rtx_REG (SImode, i),
		      gen_rtx_MEM (SImode,
			       plus_constant (stack_pointer_rtx, offs))));
	  offs += 4;
	}
    }

  /* Give back anything else.  */
  /* XXX: Should accumulate total and then give it back.  */
  while (growth >= 0)
    output_stack_adjust ( 1, fi.growth[growth--]);
}

/* This code is borrowed from the SH port.  */

/* The MCORE cannot load a large constant into a register, constants have to
   come from a pc relative load.  The reference of a pc relative load
   instruction must be less than 1k in front of the instruction.  This
   means that we often have to dump a constant inside a function, and
   generate code to branch around it.

   It is important to minimize this, since the branches will slow things
   down and make things bigger.

   Worst case code looks like:

   lrw   L1,r0
   br    L2
   align
   L1:   .long value
   L2:
   ..

   lrw   L3,r0
   br    L4
   align
   L3:   .long value
   L4:
   ..

   We fix this by performing a scan before scheduling, which notices which
   instructions need to have their operands fetched from the constant table
   and builds the table.

   The algorithm is:

   scan, find an instruction which needs a pcrel move.  Look forward, find the
   last barrier which is within MAX_COUNT bytes of the requirement.
   If there isn't one, make one.  Process all the instructions between
   the find and the barrier.

   In the above example, we can tell that L3 is within 1k of L1, so
   the first move can be shrunk from the 2 insn+constant sequence into
   just 1 insn, and the constant moved to L3 to make:

   lrw          L1,r0
   ..
   lrw          L3,r0
   bra          L4
   align
   L3:.long value
   L4:.long value

   Then the second move becomes the target for the shortening process.  */

typedef struct
{
  rtx value;			/* Value in table.  */
  rtx label;			/* Label of value.  */
} pool_node;

/* The maximum number of constants that can fit into one pool, since
   the pc relative range is 0...1020 bytes and constants are at least 4
   bytes long.  We subtract 4 from the range to allow for the case where
   we need to add a branch/align before the constant pool.  */

#define MAX_COUNT 1016
#define MAX_POOL_SIZE (MAX_COUNT/4)
static pool_node pool_vector[MAX_POOL_SIZE];
static int pool_size;

/* Dump out any constants accumulated in the final pass.  These
   will only be labels.  */

const char *
mcore_output_jump_label_table (void)
{
  int i;

  if (pool_size)
    {
      fprintf (asm_out_file, "\t.align 2\n");
      
      for (i = 0; i < pool_size; i++)
	{
	  pool_node * p = pool_vector + i;

	  (*targetm.asm_out.internal_label) (asm_out_file, "L", CODE_LABEL_NUMBER (p->label));
	  
	  output_asm_insn (".long	%0", &p->value);
	}
      
      pool_size = 0;
    }

  return "";
}

/* Check whether insn is a candidate for a conditional.  */

static cond_type
is_cond_candidate (rtx insn)
{
  /* The only things we conditionalize are those that can be directly
     changed into a conditional.  Only bother with SImode items.  If 
     we wanted to be a little more aggressive, we could also do other
     modes such as DImode with reg-reg move or load 0.  */
  if (GET_CODE (insn) == INSN)
    {
      rtx pat = PATTERN (insn);
      rtx src, dst;

      if (GET_CODE (pat) != SET)
	return COND_NO;

      dst = XEXP (pat, 0);

      if ((GET_CODE (dst) != REG &&
           GET_CODE (dst) != SUBREG) ||
	  GET_MODE (dst) != SImode)
	return COND_NO;
  
      src = XEXP (pat, 1);

      if ((GET_CODE (src) == REG ||
           (GET_CODE (src) == SUBREG &&
	    GET_CODE (SUBREG_REG (src)) == REG)) &&
	  GET_MODE (src) == SImode)
	return COND_MOV_INSN;
      else if (GET_CODE (src) == CONST_INT && 
               INTVAL (src) == 0)
	return COND_CLR_INSN;
      else if (GET_CODE (src) == PLUS &&
               (GET_CODE (XEXP (src, 0)) == REG ||
                (GET_CODE (XEXP (src, 0)) == SUBREG &&
                 GET_CODE (SUBREG_REG (XEXP (src, 0))) == REG)) &&
               GET_MODE (XEXP (src, 0)) == SImode &&
               GET_CODE (XEXP (src, 1)) == CONST_INT &&
               INTVAL (XEXP (src, 1)) == 1)
	return COND_INC_INSN;
      else if (((GET_CODE (src) == MINUS &&
		 GET_CODE (XEXP (src, 1)) == CONST_INT &&
		 INTVAL( XEXP (src, 1)) == 1) ||
                (GET_CODE (src) == PLUS &&
		 GET_CODE (XEXP (src, 1)) == CONST_INT &&
		 INTVAL (XEXP (src, 1)) == -1)) &&
               (GET_CODE (XEXP (src, 0)) == REG ||
		(GET_CODE (XEXP (src, 0)) == SUBREG &&
		 GET_CODE (SUBREG_REG (XEXP (src, 0))) == REG)) &&
               GET_MODE (XEXP (src, 0)) == SImode)
	return COND_DEC_INSN;

      /* Some insns that we don't bother with:
	 (set (rx:DI) (ry:DI))
	 (set (rx:DI) (const_int 0))
      */            

    }
  else if (GET_CODE (insn) == JUMP_INSN &&
	   GET_CODE (PATTERN (insn)) == SET &&
	   GET_CODE (XEXP (PATTERN (insn), 1)) == LABEL_REF)
    return COND_BRANCH_INSN;

  return COND_NO;
}

/* Emit a conditional version of insn and replace the old insn with the
   new one.  Return the new insn if emitted.  */

static rtx
emit_new_cond_insn (rtx insn, int cond)
{
  rtx c_insn = 0;
  rtx pat, dst, src;
  cond_type num;

  if ((num = is_cond_candidate (insn)) == COND_NO)
    return NULL;

  pat = PATTERN (insn);

  if (GET_CODE (insn) == INSN)
    {
      dst = SET_DEST (pat);
      src = SET_SRC (pat);
    }
  else
    {
      dst = JUMP_LABEL (insn);
      src = NULL_RTX;
    }

  switch (num)
    {
    case COND_MOV_INSN: 
    case COND_CLR_INSN:
      if (cond)
	c_insn = gen_movt0 (dst, src, dst);
      else
	c_insn = gen_movt0 (dst, dst, src);
      break;

    case COND_INC_INSN:
      if (cond)
	c_insn = gen_incscc (dst, dst);
      else
	c_insn = gen_incscc_false (dst, dst);
      break;
  
    case COND_DEC_INSN:
      if (cond)
	c_insn = gen_decscc (dst, dst);
      else
	c_insn = gen_decscc_false (dst, dst);
      break;

    case COND_BRANCH_INSN:
      if (cond)
	c_insn = gen_branch_true (dst);
      else
	c_insn = gen_branch_false (dst);
      break;

    default:
      return NULL;
    }

  /* Only copy the notes if they exist.  */
  if (rtx_length [GET_CODE (c_insn)] >= 7 && rtx_length [GET_CODE (insn)] >= 7)
    {
      /* We really don't need to bother with the notes and links at this
	 point, but go ahead and save the notes.  This will help is_dead()
	 when applying peepholes (links don't matter since they are not
	 used any more beyond this point for the mcore).  */
      REG_NOTES (c_insn) = REG_NOTES (insn);
    }
  
  if (num == COND_BRANCH_INSN)
    {
      /* For jumps, we need to be a little bit careful and emit the new jump
         before the old one and to update the use count for the target label.
         This way, the barrier following the old (uncond) jump will get
	 deleted, but the label won't.  */
      c_insn = emit_jump_insn_before (c_insn, insn);
      
      ++ LABEL_NUSES (dst);
      
      JUMP_LABEL (c_insn) = dst;
    }
  else
    c_insn = emit_insn_after (c_insn, insn);

  delete_insn (insn);
  
  return c_insn;
}

/* Attempt to change a basic block into a series of conditional insns.  This
   works by taking the branch at the end of the 1st block and scanning for the 
   end of the 2nd block.  If all instructions in the 2nd block have cond.
   versions and the label at the start of block 3 is the same as the target
   from the branch at block 1, then conditionalize all insn in block 2 using
   the inverse condition of the branch at block 1.  (Note I'm bending the
   definition of basic block here.)

   e.g., change:   

		bt	L2             <-- end of block 1 (delete)
		mov	r7,r8          
		addu	r7,1           
		br	L3             <-- end of block 2

	L2:	...                    <-- start of block 3 (NUSES==1)
	L3:	...

   to:

		movf	r7,r8
		incf	r7
		bf	L3

	L3:	...

   we can delete the L2 label if NUSES==1 and re-apply the optimization
   starting at the last instruction of block 2.  This may allow an entire
   if-then-else statement to be conditionalized.  BRC  */
static rtx
conditionalize_block (rtx first)
{
  rtx insn;
  rtx br_pat;
  rtx end_blk_1_br = 0;
  rtx end_blk_2_insn = 0;
  rtx start_blk_3_lab = 0;
  int cond;
  int br_lab_num;
  int blk_size = 0;

    
  /* Check that the first insn is a candidate conditional jump.  This is
     the one that we'll eliminate.  If not, advance to the next insn to
     try.  */
  if (GET_CODE (first) != JUMP_INSN ||
      GET_CODE (PATTERN (first)) != SET ||
      GET_CODE (XEXP (PATTERN (first), 1)) != IF_THEN_ELSE)
    return NEXT_INSN (first);

  /* Extract some information we need.  */
  end_blk_1_br = first;
  br_pat = PATTERN (end_blk_1_br);

  /* Complement the condition since we use the reverse cond. for the insns.  */
  cond = (GET_CODE (XEXP (XEXP (br_pat, 1), 0)) == EQ);

  /* Determine what kind of branch we have.  */
  if (GET_CODE (XEXP (XEXP (br_pat, 1), 1)) == LABEL_REF)
    {
      /* A normal branch, so extract label out of first arm.  */
      br_lab_num = CODE_LABEL_NUMBER (XEXP (XEXP (XEXP (br_pat, 1), 1), 0));
    }
  else
    {
      /* An inverse branch, so extract the label out of the 2nd arm
	 and complement the condition.  */
      cond = (cond == 0);
      br_lab_num = CODE_LABEL_NUMBER (XEXP (XEXP (XEXP (br_pat, 1), 2), 0));
    }

  /* Scan forward for the start of block 2: it must start with a
     label and that label must be the same as the branch target
     label from block 1.  We don't care about whether block 2 actually
     ends with a branch or a label (an uncond. branch is 
     conditionalizable).  */
  for (insn = NEXT_INSN (first); insn; insn = NEXT_INSN (insn))
    {
      enum rtx_code code;
      
      code = GET_CODE (insn);

      /* Look for the label at the start of block 3.  */
      if (code == CODE_LABEL && CODE_LABEL_NUMBER (insn) == br_lab_num)
	break;

      /* Skip barriers, notes, and conditionalizable insns.  If the
         insn is not conditionalizable or makes this optimization fail,
         just return the next insn so we can start over from that point.  */
      if (code != BARRIER && code != NOTE && !is_cond_candidate (insn))
	return NEXT_INSN (insn);
     
      /* Remember the last real insn before the label (i.e. end of block 2).  */
      if (code == JUMP_INSN || code == INSN)
	{
	  blk_size ++;
	  end_blk_2_insn = insn;
	}
    }

  if (!insn)
    return insn;
 
  /* It is possible for this optimization to slow performance if the blocks 
     are long.  This really depends upon whether the branch is likely taken 
     or not.  If the branch is taken, we slow performance in many cases.  But,
     if the branch is not taken, we always help performance (for a single 
     block, but for a double block (i.e. when the optimization is re-applied) 
     this is not true since the 'right thing' depends on the overall length of
     the collapsed block).  As a compromise, don't apply this optimization on 
     blocks larger than size 2 (unlikely for the mcore) when speed is important.
     the best threshold depends on the latencies of the instructions (i.e., 
     the branch penalty).  */
  if (optimize > 1 && blk_size > 2)
    return insn;

  /* At this point, we've found the start of block 3 and we know that
     it is the destination of the branch from block 1.   Also, all
     instructions in the block 2 are conditionalizable.  So, apply the
     conditionalization and delete the branch.  */
  start_blk_3_lab = insn;   
   
  for (insn = NEXT_INSN (end_blk_1_br); insn != start_blk_3_lab; 
       insn = NEXT_INSN (insn))
    {
      rtx newinsn;

      if (INSN_DELETED_P (insn))
	continue;
      
      /* Try to form a conditional variant of the instruction and emit it.  */
      if ((newinsn = emit_new_cond_insn (insn, cond)))
	{
	  if (end_blk_2_insn == insn)
            end_blk_2_insn = newinsn;

	  insn = newinsn;
	}
    }

  /* Note whether we will delete the label starting blk 3 when the jump
     gets deleted.  If so, we want to re-apply this optimization at the 
     last real instruction right before the label.  */
  if (LABEL_NUSES (start_blk_3_lab) == 1)
    {
      start_blk_3_lab = 0;
    }

  /* ??? we probably should redistribute the death notes for this insn, esp.
     the death of cc, but it doesn't really matter this late in the game.
     The peepholes all use is_dead() which will find the correct death
     regardless of whether there is a note.  */
  delete_insn (end_blk_1_br);

  if (! start_blk_3_lab)
    return end_blk_2_insn;
  
  /* Return the insn right after the label at the start of block 3.  */
  return NEXT_INSN (start_blk_3_lab);
}

/* Apply the conditionalization of blocks optimization.  This is the
   outer loop that traverses through the insns scanning for a branch
   that signifies an opportunity to apply the optimization.  Note that
   this optimization is applied late.  If we could apply it earlier,
   say before cse 2, it may expose more optimization opportunities.  
   but, the pay back probably isn't really worth the effort (we'd have 
   to update all reg/flow/notes/links/etc to make it work - and stick it
   in before cse 2).  */

static void
conditionalize_optimization (void)
{
  rtx insn;

  for (insn = get_insns (); insn; insn = conditionalize_block (insn))
    continue;
}

static int saved_warn_return_type = -1;
static int saved_warn_return_type_count = 0;

/* This is to handle loads from the constant pool.  */

static void
mcore_reorg (void)
{
  /* Reset this variable.  */
  current_function_anonymous_args = 0;
  
  /* Restore the warn_return_type if it has been altered.  */
  if (saved_warn_return_type != -1)
    {
      /* Only restore the value if we have reached another function.
	 The test of warn_return_type occurs in final_function () in
	 c-decl.c a long time after the code for the function is generated,
	 so we need a counter to tell us when we have finished parsing that
	 function and can restore the flag.  */
      if (--saved_warn_return_type_count == 0)
	{
	  warn_return_type = saved_warn_return_type;
	  saved_warn_return_type = -1;
	}
    }
  
  if (optimize == 0)
    return;
  
  /* Conditionalize blocks where we can.  */
  conditionalize_optimization ();

  /* Literal pool generation is now pushed off until the assembler.  */
}


/* Return true if X is something that can be moved directly into r15.  */

bool
mcore_r15_operand_p (rtx x)
{
  switch (GET_CODE (x))
    {
    case CONST_INT:
      return mcore_const_ok_for_inline (INTVAL (x));

    case REG:
    case SUBREG:
    case MEM:
      return 1;

    default:
      return 0;
    }
}

/* Implement SECONDARY_RELOAD_CLASS.  If CLASS contains r15, and we can't
   directly move X into it, use r1-r14 as a temporary.  */

enum reg_class
mcore_secondary_reload_class (enum reg_class class,
			      enum machine_mode mode ATTRIBUTE_UNUSED, rtx x)
{
  if (TEST_HARD_REG_BIT (reg_class_contents[class], 15)
      && !mcore_r15_operand_p (x))
    return LRW_REGS;
  return NO_REGS;
}

/* Return the reg_class to use when reloading the rtx X into the class
   CLASS.  If X is too complex to move directly into r15, prefer to
   use LRW_REGS instead.  */

enum reg_class
mcore_reload_class (rtx x, enum reg_class class)
{
  if (reg_class_subset_p (LRW_REGS, class) && !mcore_r15_operand_p (x))
    return LRW_REGS;

  return class;
}

/* Tell me if a pair of reg/subreg rtx's actually refer to the same
   register.  Note that the current version doesn't worry about whether
   they are the same mode or note (e.g., a QImode in r2 matches an HImode
   in r2 matches an SImode in r2. Might think in the future about whether
   we want to be able to say something about modes.  */

int
mcore_is_same_reg (rtx x, rtx y)
{
  /* Strip any and all of the subreg wrappers.  */
  while (GET_CODE (x) == SUBREG)
    x = SUBREG_REG (x);
  
  while (GET_CODE (y) == SUBREG)
    y = SUBREG_REG (y);

  if (GET_CODE(x) == REG && GET_CODE(y) == REG && REGNO(x) == REGNO(y))
    return 1;

  return 0;
}

void
mcore_override_options (void)
{
  /* Only the m340 supports little endian code.  */
  if (TARGET_LITTLE_END && ! TARGET_M340)
    target_flags |= MASK_M340;
}

/* Compute the number of word sized registers needed to 
   hold a function argument of mode MODE and type TYPE.  */

int
mcore_num_arg_regs (enum machine_mode mode, tree type)
{
  int size;

  if (targetm.calls.must_pass_in_stack (mode, type))
    return 0;

  if (type && mode == BLKmode)
    size = int_size_in_bytes (type);
  else
    size = GET_MODE_SIZE (mode);

  return ROUND_ADVANCE (size);
}

static rtx
handle_structs_in_regs (enum machine_mode mode, tree type, int reg)
{
  int size;

  /* The MCore ABI defines that a structure whose size is not a whole multiple
     of bytes is passed packed into registers (or spilled onto the stack if
     not enough registers are available) with the last few bytes of the
     structure being packed, left-justified, into the last register/stack slot.
     GCC handles this correctly if the last word is in a stack slot, but we
     have to generate a special, PARALLEL RTX if the last word is in an
     argument register.  */
  if (type
      && TYPE_MODE (type) == BLKmode
      && TREE_CODE (TYPE_SIZE (type)) == INTEGER_CST
      && (size = int_size_in_bytes (type)) > UNITS_PER_WORD
      && (size % UNITS_PER_WORD != 0)
      && (reg + mcore_num_arg_regs (mode, type) <= (FIRST_PARM_REG + NPARM_REGS)))
    {
      rtx    arg_regs [NPARM_REGS]; 
      int    nregs;
      rtx    result;
      rtvec  rtvec;
		     
      for (nregs = 0; size > 0; size -= UNITS_PER_WORD)
        {
          arg_regs [nregs] =
	    gen_rtx_EXPR_LIST (SImode, gen_rtx_REG (SImode, reg ++),
		  	       GEN_INT (nregs * UNITS_PER_WORD));
	  nregs ++;
        }

      /* We assume here that NPARM_REGS == 6.  The assert checks this.  */
      assert (ARRAY_SIZE (arg_regs) == 6);
      rtvec = gen_rtvec (nregs, arg_regs[0], arg_regs[1], arg_regs[2],
			  arg_regs[3], arg_regs[4], arg_regs[5]);
      
      result = gen_rtx_PARALLEL (mode, rtvec);
      return result;
    }
  
  return gen_rtx_REG (mode, reg);
}

rtx
mcore_function_value (tree valtype, tree func ATTRIBUTE_UNUSED)
{
  enum machine_mode mode;
  int unsigned_p;
  
  mode = TYPE_MODE (valtype);

  PROMOTE_MODE (mode, unsigned_p, NULL);
  
  return handle_structs_in_regs (mode, valtype, FIRST_RET_REG);
}

/* Define where to put the arguments to a function.
   Value is zero to push the argument on the stack,
   or a hard register in which to store the argument.

   MODE is the argument's machine mode.
   TYPE is the data type of the argument (as a tree).
    This is null for libcalls where that information may
    not be available.
   CUM is a variable of type CUMULATIVE_ARGS which gives info about
    the preceding args and about the function being called.
   NAMED is nonzero if this argument is a named parameter
    (otherwise it is an extra parameter matching an ellipsis).

   On MCore the first args are normally in registers
   and the rest are pushed.  Any arg that starts within the first
   NPARM_REGS words is at least partially passed in a register unless
   its data type forbids.  */

rtx
mcore_function_arg (CUMULATIVE_ARGS cum, enum machine_mode mode,
		    tree type, int named)
{
  int arg_reg;
  
  if (! named || mode == VOIDmode)
    return 0;

  if (targetm.calls.must_pass_in_stack (mode, type))
    return 0;

  arg_reg = ROUND_REG (cum, mode);
  
  if (arg_reg < NPARM_REGS)
    return handle_structs_in_regs (mode, type, FIRST_PARM_REG + arg_reg);

  return 0;
}

/* Returns the number of bytes of argument registers required to hold *part*
   of a parameter of machine mode MODE and type TYPE (which may be NULL if
   the type is not known).  If the argument fits entirely in the argument
   registers, or entirely on the stack, then 0 is returned.  CUM is the
   number of argument registers already used by earlier parameters to
   the function.  */

static int
mcore_arg_partial_bytes (CUMULATIVE_ARGS *cum, enum machine_mode mode,
			 tree type, bool named)
{
  int reg = ROUND_REG (*cum, mode);

  if (named == 0)
    return 0;

  if (targetm.calls.must_pass_in_stack (mode, type))
    return 0;
      
  /* REG is not the *hardware* register number of the register that holds
     the argument, it is the *argument* register number.  So for example,
     the first argument to a function goes in argument register 0, which
     translates (for the MCore) into hardware register 2.  The second
     argument goes into argument register 1, which translates into hardware
     register 3, and so on.  NPARM_REGS is the number of argument registers
     supported by the target, not the maximum hardware register number of
     the target.  */
  if (reg >= NPARM_REGS)
    return 0;

  /* If the argument fits entirely in registers, return 0.  */
  if (reg + mcore_num_arg_regs (mode, type) <= NPARM_REGS)
    return 0;

  /* The argument overflows the number of available argument registers.
     Compute how many argument registers have not yet been assigned to
     hold an argument.  */
  reg = NPARM_REGS - reg;

  /* Return partially in registers and partially on the stack.  */
  return reg * UNITS_PER_WORD;
}

/* Return nonzero if SYMBOL is marked as being dllexport'd.  */

int
mcore_dllexport_name_p (const char * symbol)
{
  return symbol[0] == '@' && symbol[1] == 'e' && symbol[2] == '.';
}

/* Return nonzero if SYMBOL is marked as being dllimport'd.  */

int
mcore_dllimport_name_p (const char * symbol)
{
  return symbol[0] == '@' && symbol[1] == 'i' && symbol[2] == '.';
}

/* Mark a DECL as being dllexport'd.  */

static void
mcore_mark_dllexport (tree decl)
{
  const char * oldname;
  char * newname;
  rtx    rtlname;
  tree   idp;

  rtlname = XEXP (DECL_RTL (decl), 0);
  
  if (GET_CODE (rtlname) == MEM)
    rtlname = XEXP (rtlname, 0);
  gcc_assert (GET_CODE (rtlname) == SYMBOL_REF);
  oldname = XSTR (rtlname, 0);
  
  if (mcore_dllexport_name_p (oldname))
    return;  /* Already done.  */

  newname = alloca (strlen (oldname) + 4);
  sprintf (newname, "@e.%s", oldname);

  /* We pass newname through get_identifier to ensure it has a unique
     address.  RTL processing can sometimes peek inside the symbol ref
     and compare the string's addresses to see if two symbols are
     identical.  */
  /* ??? At least I think that's why we do this.  */
  idp = get_identifier (newname);

  XEXP (DECL_RTL (decl), 0) =
    gen_rtx_SYMBOL_REF (Pmode, IDENTIFIER_POINTER (idp));
}

/* Mark a DECL as being dllimport'd.  */

static void
mcore_mark_dllimport (tree decl)
{
  const char * oldname;
  char * newname;
  tree   idp;
  rtx    rtlname;
  rtx    newrtl;

  rtlname = XEXP (DECL_RTL (decl), 0);
  
  if (GET_CODE (rtlname) == MEM)
    rtlname = XEXP (rtlname, 0);
  gcc_assert (GET_CODE (rtlname) == SYMBOL_REF);
  oldname = XSTR (rtlname, 0);
  
  gcc_assert (!mcore_dllexport_name_p (oldname));
  if (mcore_dllimport_name_p (oldname))
    return; /* Already done.  */

  /* ??? One can well ask why we're making these checks here,
     and that would be a good question.  */

  /* Imported variables can't be initialized.  */
  if (TREE_CODE (decl) == VAR_DECL
      && !DECL_VIRTUAL_P (decl)
      && DECL_INITIAL (decl))
    {
      error ("initialized variable %q+D is marked dllimport", decl);
      return;
    }
  
  /* `extern' needn't be specified with dllimport.
     Specify `extern' now and hope for the best.  Sigh.  */
  if (TREE_CODE (decl) == VAR_DECL
      /* ??? Is this test for vtables needed?  */
      && !DECL_VIRTUAL_P (decl))
    {
      DECL_EXTERNAL (decl) = 1;
      TREE_PUBLIC (decl) = 1;
    }

  newname = alloca (strlen (oldname) + 11);
  sprintf (newname, "@i.__imp_%s", oldname);

  /* We pass newname through get_identifier to ensure it has a unique
     address.  RTL processing can sometimes peek inside the symbol ref
     and compare the string's addresses to see if two symbols are
     identical.  */
  /* ??? At least I think that's why we do this.  */
  idp = get_identifier (newname);

  newrtl = gen_rtx_MEM (Pmode,
		    gen_rtx_SYMBOL_REF (Pmode,
			     IDENTIFIER_POINTER (idp)));
  XEXP (DECL_RTL (decl), 0) = newrtl;
}

static int
mcore_dllexport_p (tree decl)
{
  if (   TREE_CODE (decl) != VAR_DECL
      && TREE_CODE (decl) != FUNCTION_DECL)
    return 0;

  return lookup_attribute ("dllexport", DECL_ATTRIBUTES (decl)) != 0;
}

static int
mcore_dllimport_p (tree decl)
{
  if (   TREE_CODE (decl) != VAR_DECL
      && TREE_CODE (decl) != FUNCTION_DECL)
    return 0;

  return lookup_attribute ("dllimport", DECL_ATTRIBUTES (decl)) != 0;
}

/* We must mark dll symbols specially.  Definitions of dllexport'd objects
   install some info in the .drective (PE) or .exports (ELF) sections.  */

static void
mcore_encode_section_info (tree decl, rtx rtl ATTRIBUTE_UNUSED, int first ATTRIBUTE_UNUSED)
{
  /* Mark the decl so we can tell from the rtl whether the object is
     dllexport'd or dllimport'd.  */
  if (mcore_dllexport_p (decl))
    mcore_mark_dllexport (decl);
  else if (mcore_dllimport_p (decl))
    mcore_mark_dllimport (decl);
  
  /* It might be that DECL has already been marked as dllimport, but
     a subsequent definition nullified that.  The attribute is gone
     but DECL_RTL still has @i.__imp_foo.  We need to remove that.  */
  else if ((TREE_CODE (decl) == FUNCTION_DECL
	    || TREE_CODE (decl) == VAR_DECL)
	   && DECL_RTL (decl) != NULL_RTX
	   && GET_CODE (DECL_RTL (decl)) == MEM
	   && GET_CODE (XEXP (DECL_RTL (decl), 0)) == MEM
	   && GET_CODE (XEXP (XEXP (DECL_RTL (decl), 0), 0)) == SYMBOL_REF
	   && mcore_dllimport_name_p (XSTR (XEXP (XEXP (DECL_RTL (decl), 0), 0), 0)))
    {
      const char * oldname = XSTR (XEXP (XEXP (DECL_RTL (decl), 0), 0), 0);
      tree idp = get_identifier (oldname + 9);
      rtx newrtl = gen_rtx_SYMBOL_REF (Pmode, IDENTIFIER_POINTER (idp));

      XEXP (DECL_RTL (decl), 0) = newrtl;

      /* We previously set TREE_PUBLIC and DECL_EXTERNAL.
	 ??? We leave these alone for now.  */
    }
}

/* Undo the effects of the above.  */

static const char *
mcore_strip_name_encoding (const char * str)
{
  return str + (str[0] == '@' ? 3 : 0);
}

/* MCore specific attribute support.
   dllexport - for exporting a function/variable that will live in a dll
   dllimport - for importing a function/variable from a dll
   naked     - do not create a function prologue/epilogue.  */

const struct attribute_spec mcore_attribute_table[] =
{
  /* { name, min_len, max_len, decl_req, type_req, fn_type_req, handler } */
  { "dllexport", 0, 0, true,  false, false, NULL },
  { "dllimport", 0, 0, true,  false, false, NULL },
  { "naked",     0, 0, true,  false, false, mcore_handle_naked_attribute },
  { NULL,        0, 0, false, false, false, NULL }
};

/* Handle a "naked" attribute; arguments as in
   struct attribute_spec.handler.  */

static tree
mcore_handle_naked_attribute (tree * node, tree name, tree args ATTRIBUTE_UNUSED,
			      int flags ATTRIBUTE_UNUSED, bool * no_add_attrs)
{
  if (TREE_CODE (*node) == FUNCTION_DECL)
    {
      /* PR14310 - don't complain about lack of return statement
	 in naked functions.  The solution here is a gross hack
	 but this is the only way to solve the problem without
	 adding a new feature to GCC.  I did try submitting a patch
	 that would add such a new feature, but it was (rightfully)
	 rejected on the grounds that it was creeping featurism,
	 so hence this code.  */
      if (warn_return_type)
	{
	  saved_warn_return_type = warn_return_type;
	  warn_return_type = 0;
	  saved_warn_return_type_count = 2;
	}
      else if (saved_warn_return_type_count)
	saved_warn_return_type_count = 2;
    }
  else
    {
      warning (OPT_Wattributes, "%qs attribute only applies to functions",
	       IDENTIFIER_POINTER (name));
      *no_add_attrs = true;
    }

  return NULL_TREE;
}

/* ??? It looks like this is PE specific?  Oh well, this is what the
   old code did as well.  */

static void
mcore_unique_section (tree decl, int reloc ATTRIBUTE_UNUSED)
{
  int len;
  const char * name;
  char * string;
  const char * prefix;

  name = IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (decl));
  
  /* Strip off any encoding in name.  */
  name = (* targetm.strip_name_encoding) (name);

  /* The object is put in, for example, section .text$foo.
     The linker will then ultimately place them in .text
     (everything from the $ on is stripped).  */
  if (TREE_CODE (decl) == FUNCTION_DECL)
    prefix = ".text$";
  /* For compatibility with EPOC, we ignore the fact that the
     section might have relocs against it.  */
  else if (decl_readonly_section (decl, 0))
    prefix = ".rdata$";
  else
    prefix = ".data$";
  
  len = strlen (name) + strlen (prefix);
  string = alloca (len + 1);
  
  sprintf (string, "%s%s", prefix, name);

  DECL_SECTION_NAME (decl) = build_string (len, string);
}

int
mcore_naked_function_p (void)
{
  return lookup_attribute ("naked", DECL_ATTRIBUTES (current_function_decl)) != NULL_TREE;
}

#ifdef OBJECT_FORMAT_ELF
static void
mcore_asm_named_section (const char *name, 
			 unsigned int flags ATTRIBUTE_UNUSED,
			 tree decl ATTRIBUTE_UNUSED)
{
  fprintf (asm_out_file, "\t.section %s\n", name);
}
#endif /* OBJECT_FORMAT_ELF */

/* Worker function for TARGET_ASM_EXTERNAL_LIBCALL.  */

static void
mcore_external_libcall (rtx fun)
{
  fprintf (asm_out_file, "\t.import\t");
  assemble_name (asm_out_file, XSTR (fun, 0));
  fprintf (asm_out_file, "\n");
}

/* Worker function for TARGET_RETURN_IN_MEMORY.  */

static bool
mcore_return_in_memory (tree type, tree fntype ATTRIBUTE_UNUSED)
{
  HOST_WIDE_INT size = int_size_in_bytes (type);
  return (size == -1 || size > 2 * UNITS_PER_WORD);
}
