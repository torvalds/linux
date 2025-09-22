/* The Blackfin code generation auxiliary output file.
   Copyright (C) 2005, 2006  Free Software Foundation, Inc.
   Contributed by Analog Devices.

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
#include "regs.h"
#include "hard-reg-set.h"
#include "real.h"
#include "insn-config.h"
#include "insn-codes.h"
#include "conditions.h"
#include "insn-flags.h"
#include "output.h"
#include "insn-attr.h"
#include "tree.h"
#include "flags.h"
#include "except.h"
#include "function.h"
#include "input.h"
#include "target.h"
#include "target-def.h"
#include "expr.h"
#include "toplev.h"
#include "recog.h"
#include "optabs.h"
#include "ggc.h"
#include "integrate.h"
#include "cgraph.h"
#include "langhooks.h"
#include "bfin-protos.h"
#include "tm-preds.h"
#include "gt-bfin.h"
#include "basic-block.h"

/* A C structure for machine-specific, per-function data.
   This is added to the cfun structure.  */
struct machine_function GTY(())
{
  int has_hardware_loops;
};

/* Test and compare insns in bfin.md store the information needed to
   generate branch and scc insns here.  */
rtx bfin_compare_op0, bfin_compare_op1;

/* RTX for condition code flag register and RETS register */
extern GTY(()) rtx bfin_cc_rtx;
extern GTY(()) rtx bfin_rets_rtx;
rtx bfin_cc_rtx, bfin_rets_rtx;

int max_arg_registers = 0;

/* Arrays used when emitting register names.  */
const char *short_reg_names[]  =  SHORT_REGISTER_NAMES;
const char *high_reg_names[]   =  HIGH_REGISTER_NAMES;
const char *dregs_pair_names[] =  DREGS_PAIR_NAMES;
const char *byte_reg_names[]   =  BYTE_REGISTER_NAMES;

static int arg_regs[] = FUNCTION_ARG_REGISTERS;

/* Nonzero if -mshared-library-id was given.  */
static int bfin_lib_id_given;

static void
bfin_globalize_label (FILE *stream, const char *name)
{
  fputs (".global ", stream);
  assemble_name (stream, name);
  fputc (';',stream);
  fputc ('\n',stream);
}

static void 
output_file_start (void) 
{
  FILE *file = asm_out_file;
  int i;

  fprintf (file, ".file \"%s\";\n", input_filename);
  
  for (i = 0; arg_regs[i] >= 0; i++)
    ;
  max_arg_registers = i;	/* how many arg reg used  */
}

/* Called early in the compilation to conditionally modify
   fixed_regs/call_used_regs.  */

void 
conditional_register_usage (void)
{
  /* initialize condition code flag register rtx */
  bfin_cc_rtx = gen_rtx_REG (BImode, REG_CC);
  bfin_rets_rtx = gen_rtx_REG (Pmode, REG_RETS);
}

/* Examine machine-dependent attributes of function type FUNTYPE and return its
   type.  See the definition of E_FUNKIND.  */

static e_funkind funkind (tree funtype)
{
  tree attrs = TYPE_ATTRIBUTES (funtype);
  if (lookup_attribute ("interrupt_handler", attrs))
    return INTERRUPT_HANDLER;
  else if (lookup_attribute ("exception_handler", attrs))
    return EXCPT_HANDLER;
  else if (lookup_attribute ("nmi_handler", attrs))
    return NMI_HANDLER;
  else
    return SUBROUTINE;
}

/* Legitimize PIC addresses.  If the address is already position-independent,
   we return ORIG.  Newly generated position-independent addresses go into a
   reg.  This is REG if nonzero, otherwise we allocate register(s) as
   necessary.  PICREG is the register holding the pointer to the PIC offset
   table.  */

static rtx
legitimize_pic_address (rtx orig, rtx reg, rtx picreg)
{
  rtx addr = orig;
  rtx new = orig;

  if (GET_CODE (addr) == SYMBOL_REF || GET_CODE (addr) == LABEL_REF)
    {
      if (GET_CODE (addr) == SYMBOL_REF && CONSTANT_POOL_ADDRESS_P (addr))
	reg = new = orig;
      else
	{
	  int unspec;
	  rtx tmp;

	  if (TARGET_ID_SHARED_LIBRARY)
	    unspec = UNSPEC_MOVE_PIC;
	  else if (GET_CODE (addr) == SYMBOL_REF
		   && SYMBOL_REF_FUNCTION_P (addr))
	    {
	      unspec = UNSPEC_FUNCDESC_GOT17M4;
	    }
	  else
	    {
	      unspec = UNSPEC_MOVE_FDPIC;
	    }

	  if (reg == 0)
	    {
	      gcc_assert (!no_new_pseudos);
	      reg = gen_reg_rtx (Pmode);
	    }

	  tmp = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, addr), unspec);
	  new = gen_const_mem (Pmode, gen_rtx_PLUS (Pmode, picreg, tmp));

	  emit_move_insn (reg, new);
	}
      if (picreg == pic_offset_table_rtx)
	current_function_uses_pic_offset_table = 1;
      return reg;
    }

  else if (GET_CODE (addr) == CONST || GET_CODE (addr) == PLUS)
    {
      rtx base;

      if (GET_CODE (addr) == CONST)
	{
	  addr = XEXP (addr, 0);
	  gcc_assert (GET_CODE (addr) == PLUS);
	}

      if (XEXP (addr, 0) == picreg)
	return orig;

      if (reg == 0)
	{
	  gcc_assert (!no_new_pseudos);
	  reg = gen_reg_rtx (Pmode);
	}

      base = legitimize_pic_address (XEXP (addr, 0), reg, picreg);
      addr = legitimize_pic_address (XEXP (addr, 1),
				     base == reg ? NULL_RTX : reg,
				     picreg);

      if (GET_CODE (addr) == CONST_INT)
	{
	  gcc_assert (! reload_in_progress && ! reload_completed);
	  addr = force_reg (Pmode, addr);
	}

      if (GET_CODE (addr) == PLUS && CONSTANT_P (XEXP (addr, 1)))
	{
	  base = gen_rtx_PLUS (Pmode, base, XEXP (addr, 0));
	  addr = XEXP (addr, 1);
	}

      return gen_rtx_PLUS (Pmode, base, addr);
    }

  return new;
}

/* Stack frame layout. */

/* Compute the number of DREGS to save with a push_multiple operation.
   This could include registers that aren't modified in the function,
   since push_multiple only takes a range of registers.
   If IS_INTHANDLER, then everything that is live must be saved, even
   if normally call-clobbered.  */

static int
n_dregs_to_save (bool is_inthandler)
{
  unsigned i;

  for (i = REG_R0; i <= REG_R7; i++)
    {
      if (regs_ever_live[i] && (is_inthandler || ! call_used_regs[i]))
	return REG_R7 - i + 1;

      if (current_function_calls_eh_return)
	{
	  unsigned j;
	  for (j = 0; ; j++)
	    {
	      unsigned test = EH_RETURN_DATA_REGNO (j);
	      if (test == INVALID_REGNUM)
		break;
	      if (test == i)
		return REG_R7 - i + 1;
	    }
	}

    }
  return 0;
}

/* Like n_dregs_to_save, but compute number of PREGS to save.  */

static int
n_pregs_to_save (bool is_inthandler)
{
  unsigned i;

  for (i = REG_P0; i <= REG_P5; i++)
    if ((regs_ever_live[i] && (is_inthandler || ! call_used_regs[i]))
	|| (!TARGET_FDPIC
	    && i == PIC_OFFSET_TABLE_REGNUM
	    && (current_function_uses_pic_offset_table
		|| (TARGET_ID_SHARED_LIBRARY && ! current_function_is_leaf))))
      return REG_P5 - i + 1;
  return 0;
}

/* Determine if we are going to save the frame pointer in the prologue.  */

static bool
must_save_fp_p (void)
{
  return frame_pointer_needed || regs_ever_live[REG_FP];
}

static bool
stack_frame_needed_p (void)
{
  /* EH return puts a new return address into the frame using an
     address relative to the frame pointer.  */
  if (current_function_calls_eh_return)
    return true;
  return frame_pointer_needed;
}

/* Emit code to save registers in the prologue.  SAVEALL is nonzero if we
   must save all registers; this is used for interrupt handlers.
   SPREG contains (reg:SI REG_SP).  IS_INTHANDLER is true if we're doing
   this for an interrupt (or exception) handler.  */

static void
expand_prologue_reg_save (rtx spreg, int saveall, bool is_inthandler)
{
  int ndregs = saveall ? 8 : n_dregs_to_save (is_inthandler);
  int npregs = saveall ? 6 : n_pregs_to_save (is_inthandler);
  int dregno = REG_R7 + 1 - ndregs;
  int pregno = REG_P5 + 1 - npregs;
  int total = ndregs + npregs;
  int i;
  rtx pat, insn, val;

  if (total == 0)
    return;

  val = GEN_INT (-total * 4);
  pat = gen_rtx_PARALLEL (VOIDmode, rtvec_alloc (total + 2));
  XVECEXP (pat, 0, 0) = gen_rtx_UNSPEC (VOIDmode, gen_rtvec (1, val),
					UNSPEC_PUSH_MULTIPLE);
  XVECEXP (pat, 0, total + 1) = gen_rtx_SET (VOIDmode, spreg,
					     gen_rtx_PLUS (Pmode, spreg,
							   val));
  RTX_FRAME_RELATED_P (XVECEXP (pat, 0, total + 1)) = 1;
  for (i = 0; i < total; i++)
    {
      rtx memref = gen_rtx_MEM (word_mode,
				gen_rtx_PLUS (Pmode, spreg,
					      GEN_INT (- i * 4 - 4)));
      rtx subpat;
      if (ndregs > 0)
	{
	  subpat = gen_rtx_SET (VOIDmode, memref, gen_rtx_REG (word_mode,
							       dregno++));
	  ndregs--;
	}
      else
	{
	  subpat = gen_rtx_SET (VOIDmode, memref, gen_rtx_REG (word_mode,
							       pregno++));
	  npregs++;
	}
      XVECEXP (pat, 0, i + 1) = subpat;
      RTX_FRAME_RELATED_P (subpat) = 1;
    }
  insn = emit_insn (pat);
  RTX_FRAME_RELATED_P (insn) = 1;
}

/* Emit code to restore registers in the epilogue.  SAVEALL is nonzero if we
   must save all registers; this is used for interrupt handlers.
   SPREG contains (reg:SI REG_SP).  IS_INTHANDLER is true if we're doing
   this for an interrupt (or exception) handler.  */

static void
expand_epilogue_reg_restore (rtx spreg, bool saveall, bool is_inthandler)
{
  int ndregs = saveall ? 8 : n_dregs_to_save (is_inthandler);
  int npregs = saveall ? 6 : n_pregs_to_save (is_inthandler);
  int total = ndregs + npregs;
  int i, regno;
  rtx pat, insn;

  if (total == 0)
    return;

  pat = gen_rtx_PARALLEL (VOIDmode, rtvec_alloc (total + 1));
  XVECEXP (pat, 0, 0) = gen_rtx_SET (VOIDmode, spreg,
				     gen_rtx_PLUS (Pmode, spreg,
						   GEN_INT (total * 4)));

  if (npregs > 0)
    regno = REG_P5 + 1;
  else
    regno = REG_R7 + 1;

  for (i = 0; i < total; i++)
    {
      rtx addr = (i > 0
		  ? gen_rtx_PLUS (Pmode, spreg, GEN_INT (i * 4))
		  : spreg);
      rtx memref = gen_rtx_MEM (word_mode, addr);

      regno--;
      XVECEXP (pat, 0, i + 1)
	= gen_rtx_SET (VOIDmode, gen_rtx_REG (word_mode, regno), memref);

      if (npregs > 0)
	{
	  if (--npregs == 0)
	    regno = REG_R7 + 1;
	}
    }

  insn = emit_insn (pat);
  RTX_FRAME_RELATED_P (insn) = 1;
}

/* Perform any needed actions needed for a function that is receiving a
   variable number of arguments.

   CUM is as above.

   MODE and TYPE are the mode and type of the current parameter.

   PRETEND_SIZE is a variable that should be set to the amount of stack
   that must be pushed by the prolog to pretend that our caller pushed
   it.

   Normally, this macro will push all remaining incoming registers on the
   stack and set PRETEND_SIZE to the length of the registers pushed.  

   Blackfin specific :
   - VDSP C compiler manual (our ABI) says that a variable args function
     should save the R0, R1 and R2 registers in the stack.
   - The caller will always leave space on the stack for the
     arguments that are passed in registers, so we dont have
     to leave any extra space.
   - now, the vastart pointer can access all arguments from the stack.  */

static void
setup_incoming_varargs (CUMULATIVE_ARGS *cum,
			enum machine_mode mode ATTRIBUTE_UNUSED,
			tree type ATTRIBUTE_UNUSED, int *pretend_size,
			int no_rtl)
{
  rtx mem;
  int i;

  if (no_rtl)
    return;

  /* The move for named arguments will be generated automatically by the
     compiler.  We need to generate the move rtx for the unnamed arguments
     if they are in the first 3 words.  We assume at least 1 named argument
     exists, so we never generate [ARGP] = R0 here.  */

  for (i = cum->words + 1; i < max_arg_registers; i++)
    {
      mem = gen_rtx_MEM (Pmode,
			 plus_constant (arg_pointer_rtx, (i * UNITS_PER_WORD)));
      emit_move_insn (mem, gen_rtx_REG (Pmode, i));
    }

  *pretend_size = 0;
}

/* Value should be nonzero if functions must have frame pointers.
   Zero means the frame pointer need not be set up (and parms may
   be accessed via the stack pointer) in functions that seem suitable.  */

int
bfin_frame_pointer_required (void) 
{
  e_funkind fkind = funkind (TREE_TYPE (current_function_decl));

  if (fkind != SUBROUTINE)
    return 1;

  /* We turn on -fomit-frame-pointer if -momit-leaf-frame-pointer is used,
     so we have to override it for non-leaf functions.  */
  if (TARGET_OMIT_LEAF_FRAME_POINTER && ! current_function_is_leaf)
    return 1;

  return 0;
}

/* Return the number of registers pushed during the prologue.  */

static int
n_regs_saved_by_prologue (void)
{
  e_funkind fkind = funkind (TREE_TYPE (current_function_decl));
  bool is_inthandler = fkind != SUBROUTINE;
  tree attrs = TYPE_ATTRIBUTES (TREE_TYPE (current_function_decl));
  bool all = (lookup_attribute ("saveall", attrs) != NULL_TREE
	      || (is_inthandler && !current_function_is_leaf));
  int ndregs = all ? 8 : n_dregs_to_save (is_inthandler);
  int npregs = all ? 6 : n_pregs_to_save (is_inthandler);  
  int n = ndregs + npregs;

  if (all || stack_frame_needed_p ())
    /* We use a LINK instruction in this case.  */
    n += 2;
  else
    {
      if (must_save_fp_p ())
	n++;
      if (! current_function_is_leaf)
	n++;
    }

  if (fkind != SUBROUTINE)
    {
      int i;

      /* Increment once for ASTAT.  */
      n++;

      /* RETE/X/N.  */
      if (lookup_attribute ("nesting", attrs))
	n++;

      for (i = REG_P7 + 1; i < REG_CC; i++)
	if (all 
	    || regs_ever_live[i]
	    || (!leaf_function_p () && call_used_regs[i]))
	  n += i == REG_A0 || i == REG_A1 ? 2 : 1;
    }
  return n;
}

/* Return the offset between two registers, one to be eliminated, and the other
   its replacement, at the start of a routine.  */

HOST_WIDE_INT
bfin_initial_elimination_offset (int from, int to)
{
  HOST_WIDE_INT offset = 0;

  if (from == ARG_POINTER_REGNUM)
    offset = n_regs_saved_by_prologue () * 4;

  if (to == STACK_POINTER_REGNUM)
    {
      if (current_function_outgoing_args_size >= FIXED_STACK_AREA)
	offset += current_function_outgoing_args_size;
      else if (current_function_outgoing_args_size)
	offset += FIXED_STACK_AREA;

      offset += get_frame_size ();
    }

  return offset;
}

/* Emit code to load a constant CONSTANT into register REG; setting
   RTX_FRAME_RELATED_P on all insns we generate if RELATED is true.
   Make sure that the insns we generate need not be split.  */

static void
frame_related_constant_load (rtx reg, HOST_WIDE_INT constant, bool related)
{
  rtx insn;
  rtx cst = GEN_INT (constant);

  if (constant >= -32768 && constant < 65536)
    insn = emit_move_insn (reg, cst);
  else
    {
      /* We don't call split_load_immediate here, since dwarf2out.c can get
	 confused about some of the more clever sequences it can generate.  */
      insn = emit_insn (gen_movsi_high (reg, cst));
      if (related)
	RTX_FRAME_RELATED_P (insn) = 1;
      insn = emit_insn (gen_movsi_low (reg, reg, cst));
    }
  if (related)
    RTX_FRAME_RELATED_P (insn) = 1;
}

/* Generate efficient code to add a value to the frame pointer.  We
   can use P1 as a scratch register.  Set RTX_FRAME_RELATED_P on the
   generated insns if FRAME is nonzero.  */

static void
add_to_sp (rtx spreg, HOST_WIDE_INT value, int frame)
{
  if (value == 0)
    return;

  /* Choose whether to use a sequence using a temporary register, or
     a sequence with multiple adds.  We can add a signed 7 bit value
     in one instruction.  */
  if (value > 120 || value < -120)
    {
      rtx tmpreg = gen_rtx_REG (SImode, REG_P1);
      rtx insn;

      if (frame)
	frame_related_constant_load (tmpreg, value, TRUE);
      else
	{
	  insn = emit_move_insn (tmpreg, GEN_INT (value));
	  if (frame)
	    RTX_FRAME_RELATED_P (insn) = 1;
	}

      insn = emit_insn (gen_addsi3 (spreg, spreg, tmpreg));
      if (frame)
	RTX_FRAME_RELATED_P (insn) = 1;
    }
  else
    do
      {
	int size = value;
	rtx insn;

	if (size > 60)
	  size = 60;
	else if (size < -60)
	  /* We could use -62, but that would leave the stack unaligned, so
	     it's no good.  */
	  size = -60;

	insn = emit_insn (gen_addsi3 (spreg, spreg, GEN_INT (size)));
	if (frame)
	  RTX_FRAME_RELATED_P (insn) = 1;
	value -= size;
      }
    while (value != 0);
}

/* Generate a LINK insn for a frame sized FRAME_SIZE.  If this constant
   is too large, generate a sequence of insns that has the same effect.
   SPREG contains (reg:SI REG_SP).  */

static void
emit_link_insn (rtx spreg, HOST_WIDE_INT frame_size)
{
  HOST_WIDE_INT link_size = frame_size;
  rtx insn;
  int i;

  if (link_size > 262140)
    link_size = 262140;

  /* Use a LINK insn with as big a constant as possible, then subtract
     any remaining size from the SP.  */
  insn = emit_insn (gen_link (GEN_INT (-8 - link_size)));
  RTX_FRAME_RELATED_P (insn) = 1;

  for (i = 0; i < XVECLEN (PATTERN (insn), 0); i++)
    {
      rtx set = XVECEXP (PATTERN (insn), 0, i);
      gcc_assert (GET_CODE (set) == SET);
      RTX_FRAME_RELATED_P (set) = 1;
    }

  frame_size -= link_size;

  if (frame_size > 0)
    {
      /* Must use a call-clobbered PREG that isn't the static chain.  */
      rtx tmpreg = gen_rtx_REG (Pmode, REG_P1);

      frame_related_constant_load (tmpreg, -frame_size, TRUE);
      insn = emit_insn (gen_addsi3 (spreg, spreg, tmpreg));
      RTX_FRAME_RELATED_P (insn) = 1;
    }
}

/* Return the number of bytes we must reserve for outgoing arguments
   in the current function's stack frame.  */

static HOST_WIDE_INT
arg_area_size (void)
{
  if (current_function_outgoing_args_size)
    {
      if (current_function_outgoing_args_size >= FIXED_STACK_AREA)
	return current_function_outgoing_args_size;
      else
	return FIXED_STACK_AREA;
    }
  return 0;
}

/* Save RETS and FP, and allocate a stack frame.  ALL is true if the
   function must save all its registers (true only for certain interrupt
   handlers).  */

static void
do_link (rtx spreg, HOST_WIDE_INT frame_size, bool all)
{
  frame_size += arg_area_size ();

  if (all || stack_frame_needed_p ()
      || (must_save_fp_p () && ! current_function_is_leaf))
    emit_link_insn (spreg, frame_size);
  else
    {
      if (! current_function_is_leaf)
	{
	  rtx pat = gen_movsi (gen_rtx_MEM (Pmode,
					    gen_rtx_PRE_DEC (Pmode, spreg)),
			       bfin_rets_rtx);
	  rtx insn = emit_insn (pat);
	  RTX_FRAME_RELATED_P (insn) = 1;
	}
      if (must_save_fp_p ())
	{
	  rtx pat = gen_movsi (gen_rtx_MEM (Pmode,
					    gen_rtx_PRE_DEC (Pmode, spreg)),
			       gen_rtx_REG (Pmode, REG_FP));
	  rtx insn = emit_insn (pat);
	  RTX_FRAME_RELATED_P (insn) = 1;
	}
      add_to_sp (spreg, -frame_size, 1);
    }
}

/* Like do_link, but used for epilogues to deallocate the stack frame.  */

static void
do_unlink (rtx spreg, HOST_WIDE_INT frame_size, bool all)
{
  frame_size += arg_area_size ();

  if (all || stack_frame_needed_p ())
    emit_insn (gen_unlink ());
  else 
    {
      rtx postinc = gen_rtx_MEM (Pmode, gen_rtx_POST_INC (Pmode, spreg));

      add_to_sp (spreg, frame_size, 0);
      if (must_save_fp_p ())
	{
	  rtx fpreg = gen_rtx_REG (Pmode, REG_FP);
	  emit_move_insn (fpreg, postinc);
	  emit_insn (gen_rtx_USE (VOIDmode, fpreg));
	}
      if (! current_function_is_leaf)
	{
	  emit_move_insn (bfin_rets_rtx, postinc);
	  emit_insn (gen_rtx_USE (VOIDmode, bfin_rets_rtx));
	}
    }
}

/* Generate a prologue suitable for a function of kind FKIND.  This is
   called for interrupt and exception handler prologues.
   SPREG contains (reg:SI REG_SP).  */

static void
expand_interrupt_handler_prologue (rtx spreg, e_funkind fkind)
{
  int i;
  HOST_WIDE_INT frame_size = get_frame_size ();
  rtx predec1 = gen_rtx_PRE_DEC (SImode, spreg);
  rtx predec = gen_rtx_MEM (SImode, predec1);
  rtx insn;
  tree attrs = TYPE_ATTRIBUTES (TREE_TYPE (current_function_decl));
  bool all = lookup_attribute ("saveall", attrs) != NULL_TREE;
  tree kspisusp = lookup_attribute ("kspisusp", attrs);

  if (kspisusp)
    {
      insn = emit_move_insn (spreg, gen_rtx_REG (Pmode, REG_USP));
      RTX_FRAME_RELATED_P (insn) = 1;
    }

  /* We need space on the stack in case we need to save the argument
     registers.  */
  if (fkind == EXCPT_HANDLER)
    {
      insn = emit_insn (gen_addsi3 (spreg, spreg, GEN_INT (-12)));
      RTX_FRAME_RELATED_P (insn) = 1;
    }

  insn = emit_move_insn (predec, gen_rtx_REG (SImode, REG_ASTAT));
  RTX_FRAME_RELATED_P (insn) = 1;

  /* If we're calling other functions, they won't save their call-clobbered
     registers, so we must save everything here.  */
  if (!current_function_is_leaf)
    all = true;
  expand_prologue_reg_save (spreg, all, true);

  for (i = REG_P7 + 1; i < REG_CC; i++)
    if (all 
	|| regs_ever_live[i]
	|| (!leaf_function_p () && call_used_regs[i]))
      {
	if (i == REG_A0 || i == REG_A1)
	  insn = emit_move_insn (gen_rtx_MEM (PDImode, predec1),
				 gen_rtx_REG (PDImode, i));
	else
	  insn = emit_move_insn (predec, gen_rtx_REG (SImode, i));
	RTX_FRAME_RELATED_P (insn) = 1;
      }

  if (lookup_attribute ("nesting", attrs))
    {
      rtx srcreg = gen_rtx_REG (Pmode, (fkind == EXCPT_HANDLER ? REG_RETX
					: fkind == NMI_HANDLER ? REG_RETN
					: REG_RETI));
      insn = emit_move_insn (predec, srcreg);
      RTX_FRAME_RELATED_P (insn) = 1;
    }

  do_link (spreg, frame_size, all);

  if (fkind == EXCPT_HANDLER)
    {
      rtx r0reg = gen_rtx_REG (SImode, REG_R0);
      rtx r1reg = gen_rtx_REG (SImode, REG_R1);
      rtx r2reg = gen_rtx_REG (SImode, REG_R2);
      rtx insn;

      insn = emit_move_insn (r0reg, gen_rtx_REG (SImode, REG_SEQSTAT));
      REG_NOTES (insn) = gen_rtx_EXPR_LIST (REG_MAYBE_DEAD, const0_rtx,
					    NULL_RTX);
      insn = emit_insn (gen_ashrsi3 (r0reg, r0reg, GEN_INT (26)));
      REG_NOTES (insn) = gen_rtx_EXPR_LIST (REG_MAYBE_DEAD, const0_rtx,
					    NULL_RTX);
      insn = emit_insn (gen_ashlsi3 (r0reg, r0reg, GEN_INT (26)));
      REG_NOTES (insn) = gen_rtx_EXPR_LIST (REG_MAYBE_DEAD, const0_rtx,
					    NULL_RTX);
      insn = emit_move_insn (r1reg, spreg);
      REG_NOTES (insn) = gen_rtx_EXPR_LIST (REG_MAYBE_DEAD, const0_rtx,
					    NULL_RTX);
      insn = emit_move_insn (r2reg, gen_rtx_REG (Pmode, REG_FP));
      REG_NOTES (insn) = gen_rtx_EXPR_LIST (REG_MAYBE_DEAD, const0_rtx,
					    NULL_RTX);
      insn = emit_insn (gen_addsi3 (r2reg, r2reg, GEN_INT (8)));
      REG_NOTES (insn) = gen_rtx_EXPR_LIST (REG_MAYBE_DEAD, const0_rtx,
					    NULL_RTX);
    }
}

/* Generate an epilogue suitable for a function of kind FKIND.  This is
   called for interrupt and exception handler epilogues.
   SPREG contains (reg:SI REG_SP).  */

static void
expand_interrupt_handler_epilogue (rtx spreg, e_funkind fkind)
{
  int i;
  rtx postinc1 = gen_rtx_POST_INC (SImode, spreg);
  rtx postinc = gen_rtx_MEM (SImode, postinc1);
  tree attrs = TYPE_ATTRIBUTES (TREE_TYPE (current_function_decl));
  bool all = lookup_attribute ("saveall", attrs) != NULL_TREE;

  /* A slightly crude technique to stop flow from trying to delete "dead"
     insns.  */
  MEM_VOLATILE_P (postinc) = 1;

  do_unlink (spreg, get_frame_size (), all);

  if (lookup_attribute ("nesting", attrs))
    {
      rtx srcreg = gen_rtx_REG (Pmode, (fkind == EXCPT_HANDLER ? REG_RETX
					: fkind == NMI_HANDLER ? REG_RETN
					: REG_RETI));
      emit_move_insn (srcreg, postinc);
    }

  /* If we're calling other functions, they won't save their call-clobbered
     registers, so we must save (and restore) everything here.  */
  if (!current_function_is_leaf)
    all = true;

  for (i = REG_CC - 1; i > REG_P7; i--)
    if (all
	|| regs_ever_live[i]
	|| (!leaf_function_p () && call_used_regs[i]))
      {
	if (i == REG_A0 || i == REG_A1)
	  {
	    rtx mem = gen_rtx_MEM (PDImode, postinc1);
	    MEM_VOLATILE_P (mem) = 1;
	    emit_move_insn (gen_rtx_REG (PDImode, i), mem);
	  }
	else
	  emit_move_insn (gen_rtx_REG (SImode, i), postinc);
      }

  expand_epilogue_reg_restore (spreg, all, true);

  emit_move_insn (gen_rtx_REG (SImode, REG_ASTAT), postinc);

  /* Deallocate any space we left on the stack in case we needed to save the
     argument registers.  */
  if (fkind == EXCPT_HANDLER)
    emit_insn (gen_addsi3 (spreg, spreg, GEN_INT (12)));

  emit_jump_insn (gen_return_internal (GEN_INT (fkind)));
}

/* Used while emitting the prologue to generate code to load the correct value
   into the PIC register, which is passed in DEST.  */

static rtx
bfin_load_pic_reg (rtx dest)
{
  struct cgraph_local_info *i = NULL;
  rtx addr, insn;
 
  if (flag_unit_at_a_time)
    i = cgraph_local_info (current_function_decl);
 
  /* Functions local to the translation unit don't need to reload the
     pic reg, since the caller always passes a usable one.  */
  if (i && i->local)
    return pic_offset_table_rtx;
      
  if (bfin_lib_id_given)
    addr = plus_constant (pic_offset_table_rtx, -4 - bfin_library_id * 4);
  else
    addr = gen_rtx_PLUS (Pmode, pic_offset_table_rtx,
			 gen_rtx_UNSPEC (Pmode, gen_rtvec (1, const0_rtx),
					 UNSPEC_LIBRARY_OFFSET));
  insn = emit_insn (gen_movsi (dest, gen_rtx_MEM (Pmode, addr)));
  REG_NOTES (insn) = gen_rtx_EXPR_LIST (REG_MAYBE_DEAD, const0_rtx, NULL);
  return dest;
}

/* Generate RTL for the prologue of the current function.  */

void
bfin_expand_prologue (void)
{
  rtx insn;
  HOST_WIDE_INT frame_size = get_frame_size ();
  rtx spreg = gen_rtx_REG (Pmode, REG_SP);
  e_funkind fkind = funkind (TREE_TYPE (current_function_decl));
  rtx pic_reg_loaded = NULL_RTX;

  if (fkind != SUBROUTINE)
    {
      expand_interrupt_handler_prologue (spreg, fkind);
      return;
    }

  if (current_function_limit_stack)
    {
      HOST_WIDE_INT offset
	= bfin_initial_elimination_offset (ARG_POINTER_REGNUM,
					   STACK_POINTER_REGNUM);
      rtx lim = stack_limit_rtx;

      if (GET_CODE (lim) == SYMBOL_REF)
	{
	  rtx p2reg = gen_rtx_REG (Pmode, REG_P2);
	  if (TARGET_ID_SHARED_LIBRARY)
	    {
	      rtx p1reg = gen_rtx_REG (Pmode, REG_P1);
	      rtx val;
	      pic_reg_loaded = bfin_load_pic_reg (p2reg);
	      val = legitimize_pic_address (stack_limit_rtx, p1reg,
					    pic_reg_loaded);
	      emit_move_insn (p1reg, val);
	      frame_related_constant_load (p2reg, offset, FALSE);
	      emit_insn (gen_addsi3 (p2reg, p2reg, p1reg));
	      lim = p2reg;
	    }
	  else
	    {
	      rtx limit = plus_constant (stack_limit_rtx, offset);
	      emit_move_insn (p2reg, limit);
	      lim = p2reg;
	    }
	}
      emit_insn (gen_compare_lt (bfin_cc_rtx, spreg, lim));
      emit_insn (gen_trapifcc ());
    }
  expand_prologue_reg_save (spreg, 0, false);

  do_link (spreg, frame_size, false);

  if (TARGET_ID_SHARED_LIBRARY
      && (current_function_uses_pic_offset_table
	  || !current_function_is_leaf))
    bfin_load_pic_reg (pic_offset_table_rtx);
}

/* Generate RTL for the epilogue of the current function.  NEED_RETURN is zero
   if this is for a sibcall.  EH_RETURN is nonzero if we're expanding an
   eh_return pattern.  */

void
bfin_expand_epilogue (int need_return, int eh_return)
{
  rtx spreg = gen_rtx_REG (Pmode, REG_SP);
  e_funkind fkind = funkind (TREE_TYPE (current_function_decl));

  if (fkind != SUBROUTINE)
    {
      expand_interrupt_handler_epilogue (spreg, fkind);
      return;
    }

  do_unlink (spreg, get_frame_size (), false);

  expand_epilogue_reg_restore (spreg, false, false);

  /* Omit the return insn if this is for a sibcall.  */
  if (! need_return)
    return;

  if (eh_return)
    emit_insn (gen_addsi3 (spreg, spreg, gen_rtx_REG (Pmode, REG_P2)));

  emit_jump_insn (gen_return_internal (GEN_INT (SUBROUTINE)));
}

/* Return nonzero if register OLD_REG can be renamed to register NEW_REG.  */

int
bfin_hard_regno_rename_ok (unsigned int old_reg ATTRIBUTE_UNUSED,
			   unsigned int new_reg)
{
  /* Interrupt functions can only use registers that have already been
     saved by the prologue, even if they would normally be
     call-clobbered.  */

  if (funkind (TREE_TYPE (current_function_decl)) != SUBROUTINE
      && !regs_ever_live[new_reg])
    return 0;

  return 1;
}

/* Return the value of the return address for the frame COUNT steps up
   from the current frame, after the prologue.
   We punt for everything but the current frame by returning const0_rtx.  */

rtx
bfin_return_addr_rtx (int count)
{
  if (count != 0)
    return const0_rtx;

  return get_hard_reg_initial_val (Pmode, REG_RETS);
}

/* Try machine-dependent ways of modifying an illegitimate address X
   to be legitimate.  If we find one, return the new, valid address,
   otherwise return NULL_RTX.

   OLDX is the address as it was before break_out_memory_refs was called.
   In some cases it is useful to look at this to decide what needs to be done.

   MODE is the mode of the memory reference.  */

rtx
legitimize_address (rtx x ATTRIBUTE_UNUSED, rtx oldx ATTRIBUTE_UNUSED,
		    enum machine_mode mode ATTRIBUTE_UNUSED)
{
  return NULL_RTX;
}

static rtx
bfin_delegitimize_address (rtx orig_x)
{
  rtx x = orig_x, y;

  if (GET_CODE (x) != MEM)
    return orig_x;

  x = XEXP (x, 0);
  if (GET_CODE (x) == PLUS
      && GET_CODE (XEXP (x, 1)) == UNSPEC
      && XINT (XEXP (x, 1), 1) == UNSPEC_MOVE_PIC
      && GET_CODE (XEXP (x, 0)) == REG
      && REGNO (XEXP (x, 0)) == PIC_OFFSET_TABLE_REGNUM)
    return XVECEXP (XEXP (x, 1), 0, 0);

  return orig_x;
}

/* This predicate is used to compute the length of a load/store insn.
   OP is a MEM rtx, we return nonzero if its addressing mode requires a
   32 bit instruction.  */

int
effective_address_32bit_p (rtx op, enum machine_mode mode) 
{
  HOST_WIDE_INT offset;

  mode = GET_MODE (op);
  op = XEXP (op, 0);

  if (GET_CODE (op) != PLUS)
    {
      gcc_assert (REG_P (op) || GET_CODE (op) == POST_INC
		  || GET_CODE (op) == PRE_DEC || GET_CODE (op) == POST_DEC);
      return 0;
    }

  offset = INTVAL (XEXP (op, 1));

  /* All byte loads use a 16 bit offset.  */
  if (GET_MODE_SIZE (mode) == 1)
    return 1;

  if (GET_MODE_SIZE (mode) == 4)
    {
      /* Frame pointer relative loads can use a negative offset, all others
	 are restricted to a small positive one.  */
      if (XEXP (op, 0) == frame_pointer_rtx)
	return offset < -128 || offset > 60;
      return offset < 0 || offset > 60;
    }

  /* Must be HImode now.  */
  return offset < 0 || offset > 30;
}

/* Returns true if X is a memory reference using an I register.  */
bool
bfin_dsp_memref_p (rtx x)
{
  if (! MEM_P (x))
    return false;
  x = XEXP (x, 0);
  if (GET_CODE (x) == POST_INC || GET_CODE (x) == PRE_INC
      || GET_CODE (x) == POST_DEC || GET_CODE (x) == PRE_DEC)
    x = XEXP (x, 0);
  return IREG_P (x);
}

/* Return cost of the memory address ADDR.
   All addressing modes are equally cheap on the Blackfin.  */

static int
bfin_address_cost (rtx addr ATTRIBUTE_UNUSED)
{
  return 1;
}

/* Subroutine of print_operand; used to print a memory reference X to FILE.  */

void
print_address_operand (FILE *file, rtx x)
{
  switch (GET_CODE (x))
    {
    case PLUS:
      output_address (XEXP (x, 0));
      fprintf (file, "+");
      output_address (XEXP (x, 1));
      break;

    case PRE_DEC:
      fprintf (file, "--");
      output_address (XEXP (x, 0));    
      break;
    case POST_INC:
      output_address (XEXP (x, 0));
      fprintf (file, "++");
      break;
    case POST_DEC:
      output_address (XEXP (x, 0));
      fprintf (file, "--");
      break;

    default:
      gcc_assert (GET_CODE (x) != MEM);
      print_operand (file, x, 0);
      break;
    }
}

/* Adding intp DImode support by Tony
 * -- Q: (low  word)
 * -- R: (high word)
 */

void
print_operand (FILE *file, rtx x, char code)
{
  enum machine_mode mode = GET_MODE (x);

  switch (code)
    {
    case 'j':
      switch (GET_CODE (x))
	{
	case EQ:
	  fprintf (file, "e");
	  break;
	case NE:
	  fprintf (file, "ne");
	  break;
	case GT:
	  fprintf (file, "g");
	  break;
	case LT:
	  fprintf (file, "l");
	  break;
	case GE:
	  fprintf (file, "ge");
	  break;
	case LE:
	  fprintf (file, "le");
	  break;
	case GTU:
	  fprintf (file, "g");
	  break;
	case LTU:
	  fprintf (file, "l");
	  break;
	case GEU:
	  fprintf (file, "ge");
	  break;
	case LEU:
	  fprintf (file, "le");
	  break;
	default:
	  output_operand_lossage ("invalid %%j value");
	}
      break;
    
    case 'J':					 /* reverse logic */
      switch (GET_CODE(x))
	{
	case EQ:
	  fprintf (file, "ne");
	  break;
	case NE:
	  fprintf (file, "e");
	  break;
	case GT:
	  fprintf (file, "le");
	  break;
	case LT:
	  fprintf (file, "ge");
	  break;
	case GE:
	  fprintf (file, "l");
	  break;
	case LE:
	  fprintf (file, "g");
	  break;
	case GTU:
	  fprintf (file, "le");
	  break;
	case LTU:
	  fprintf (file, "ge");
	  break;
	case GEU:
	  fprintf (file, "l");
	  break;
	case LEU:
	  fprintf (file, "g");
	  break;
	default:
	  output_operand_lossage ("invalid %%J value");
	}
      break;

    default:
      switch (GET_CODE (x))
	{
	case REG:
	  if (code == 'h')
	    {
	      gcc_assert (REGNO (x) < 32);
	      fprintf (file, "%s", short_reg_names[REGNO (x)]);
	      /*fprintf (file, "\n%d\n ", REGNO (x));*/
	      break;
	    }
	  else if (code == 'd')
	    {
	      gcc_assert (REGNO (x) < 32);
	      fprintf (file, "%s", high_reg_names[REGNO (x)]);
	      break;
	    }
	  else if (code == 'w')
	    {
	      gcc_assert (REGNO (x) == REG_A0 || REGNO (x) == REG_A1);
	      fprintf (file, "%s.w", reg_names[REGNO (x)]);
	    }
	  else if (code == 'x')
	    {
	      gcc_assert (REGNO (x) == REG_A0 || REGNO (x) == REG_A1);
	      fprintf (file, "%s.x", reg_names[REGNO (x)]);
	    }
	  else if (code == 'D')
	    {
	      fprintf (file, "%s", dregs_pair_names[REGNO (x)]);
	    }
	  else if (code == 'H')
	    {
	      gcc_assert (mode == DImode || mode == DFmode);
	      gcc_assert (REG_P (x));
	      fprintf (file, "%s", reg_names[REGNO (x) + 1]);
	    }
	  else if (code == 'T')
	    {
	      gcc_assert (D_REGNO_P (REGNO (x)));
	      fprintf (file, "%s", byte_reg_names[REGNO (x)]);
	    }
	  else 
	    fprintf (file, "%s", reg_names[REGNO (x)]);
	  break;

	case MEM:
	  fputc ('[', file);
	  x = XEXP (x,0);
	  print_address_operand (file, x);
	  fputc (']', file);
	  break;

	case CONST_INT:
	  if (code == 'M')
	    {
	      switch (INTVAL (x))
		{
		case MACFLAG_NONE:
		  break;
		case MACFLAG_FU:
		  fputs ("(FU)", file);
		  break;
		case MACFLAG_T:
		  fputs ("(T)", file);
		  break;
		case MACFLAG_TFU:
		  fputs ("(TFU)", file);
		  break;
		case MACFLAG_W32:
		  fputs ("(W32)", file);
		  break;
		case MACFLAG_IS:
		  fputs ("(IS)", file);
		  break;
		case MACFLAG_IU:
		  fputs ("(IU)", file);
		  break;
		case MACFLAG_IH:
		  fputs ("(IH)", file);
		  break;
		case MACFLAG_M:
		  fputs ("(M)", file);
		  break;
		case MACFLAG_ISS2:
		  fputs ("(ISS2)", file);
		  break;
		case MACFLAG_S2RND:
		  fputs ("(S2RND)", file);
		  break;
		default:
		  gcc_unreachable ();
		}
	      break;
	    }
	  else if (code == 'b')
	    {
	      if (INTVAL (x) == 0)
		fputs ("+=", file);
	      else if (INTVAL (x) == 1)
		fputs ("-=", file);
	      else
		gcc_unreachable ();
	      break;
	    }
	  /* Moves to half registers with d or h modifiers always use unsigned
	     constants.  */
	  else if (code == 'd')
	    x = GEN_INT ((INTVAL (x) >> 16) & 0xffff);
	  else if (code == 'h')
	    x = GEN_INT (INTVAL (x) & 0xffff);
	  else if (code == 'X')
	    x = GEN_INT (exact_log2 (0xffffffff & INTVAL (x)));
	  else if (code == 'Y')
	    x = GEN_INT (exact_log2 (0xffffffff & ~INTVAL (x)));
	  else if (code == 'Z')
	    /* Used for LINK insns.  */
	    x = GEN_INT (-8 - INTVAL (x));

	  /* fall through */

	case SYMBOL_REF:
	  output_addr_const (file, x);
	  break;

	case CONST_DOUBLE:
	  output_operand_lossage ("invalid const_double operand");
	  break;

	case UNSPEC:
	  switch (XINT (x, 1))
	    {
	    case UNSPEC_MOVE_PIC:
	      output_addr_const (file, XVECEXP (x, 0, 0));
	      fprintf (file, "@GOT");
	      break;

	    case UNSPEC_MOVE_FDPIC:
	      output_addr_const (file, XVECEXP (x, 0, 0));
	      fprintf (file, "@GOT17M4");
	      break;

	    case UNSPEC_FUNCDESC_GOT17M4:
	      output_addr_const (file, XVECEXP (x, 0, 0));
	      fprintf (file, "@FUNCDESC_GOT17M4");
	      break;

	    case UNSPEC_LIBRARY_OFFSET:
	      fprintf (file, "_current_shared_library_p5_offset_");
	      break;

	    default:
	      gcc_unreachable ();
	    }
	  break;

	default:
	  output_addr_const (file, x);
	}
    }
}

/* Argument support functions.  */

/* Initialize a variable CUM of type CUMULATIVE_ARGS
   for a call to a function whose data type is FNTYPE.
   For a library call, FNTYPE is 0.  
   VDSP C Compiler manual, our ABI says that
   first 3 words of arguments will use R0, R1 and R2.
*/

void
init_cumulative_args (CUMULATIVE_ARGS *cum, tree fntype,
		      rtx libname ATTRIBUTE_UNUSED)
{
  static CUMULATIVE_ARGS zero_cum;

  *cum = zero_cum;

  /* Set up the number of registers to use for passing arguments.  */

  cum->nregs = max_arg_registers;
  cum->arg_regs = arg_regs;

  cum->call_cookie = CALL_NORMAL;
  /* Check for a longcall attribute.  */
  if (fntype && lookup_attribute ("shortcall", TYPE_ATTRIBUTES (fntype)))
    cum->call_cookie |= CALL_SHORT;
  else if (fntype && lookup_attribute ("longcall", TYPE_ATTRIBUTES (fntype)))
    cum->call_cookie |= CALL_LONG;

  return;
}

/* Update the data in CUM to advance over an argument
   of mode MODE and data type TYPE.
   (TYPE is null for libcalls where that information may not be available.)  */

void
function_arg_advance (CUMULATIVE_ARGS *cum, enum machine_mode mode, tree type,
		      int named ATTRIBUTE_UNUSED)
{
  int count, bytes, words;

  bytes = (mode == BLKmode) ? int_size_in_bytes (type) : GET_MODE_SIZE (mode);
  words = (bytes + UNITS_PER_WORD - 1) / UNITS_PER_WORD;

  cum->words += words;
  cum->nregs -= words;

  if (cum->nregs <= 0)
    {
      cum->nregs = 0;
      cum->arg_regs = NULL;
    }
  else
    {
      for (count = 1; count <= words; count++)
        cum->arg_regs++;
    }

  return;
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
    (otherwise it is an extra parameter matching an ellipsis).  */

struct rtx_def *
function_arg (CUMULATIVE_ARGS *cum, enum machine_mode mode, tree type,
	      int named ATTRIBUTE_UNUSED)
{
  int bytes
    = (mode == BLKmode) ? int_size_in_bytes (type) : GET_MODE_SIZE (mode);

  if (mode == VOIDmode)
    /* Compute operand 2 of the call insn.  */
    return GEN_INT (cum->call_cookie);

  if (bytes == -1)
    return NULL_RTX;

  if (cum->nregs)
    return gen_rtx_REG (mode, *(cum->arg_regs));

  return NULL_RTX;
}

/* For an arg passed partly in registers and partly in memory,
   this is the number of bytes passed in registers.
   For args passed entirely in registers or entirely in memory, zero.

   Refer VDSP C Compiler manual, our ABI.
   First 3 words are in registers. So, if a an argument is larger
   than the registers available, it will span the register and
   stack.   */

static int
bfin_arg_partial_bytes (CUMULATIVE_ARGS *cum, enum machine_mode mode,
			tree type ATTRIBUTE_UNUSED,
			bool named ATTRIBUTE_UNUSED)
{
  int bytes
    = (mode == BLKmode) ? int_size_in_bytes (type) : GET_MODE_SIZE (mode);
  int bytes_left = cum->nregs * UNITS_PER_WORD;
  
  if (bytes == -1)
    return 0;

  if (bytes_left == 0)
    return 0;
  if (bytes > bytes_left)
    return bytes_left;
  return 0;
}

/* Variable sized types are passed by reference.  */

static bool
bfin_pass_by_reference (CUMULATIVE_ARGS *cum ATTRIBUTE_UNUSED,
			enum machine_mode mode ATTRIBUTE_UNUSED,
			tree type, bool named ATTRIBUTE_UNUSED)
{
  return type && TREE_CODE (TYPE_SIZE (type)) != INTEGER_CST;
}

/* Decide whether a type should be returned in memory (true)
   or in a register (false).  This is called by the macro
   RETURN_IN_MEMORY.  */

int
bfin_return_in_memory (tree type)
{
  int size = int_size_in_bytes (type);
  return size > 2 * UNITS_PER_WORD || size == -1;
}

/* Register in which address to store a structure value
   is passed to a function.  */
static rtx
bfin_struct_value_rtx (tree fntype ATTRIBUTE_UNUSED,
		      int incoming ATTRIBUTE_UNUSED)
{
  return gen_rtx_REG (Pmode, REG_P0);
}

/* Return true when register may be used to pass function parameters.  */

bool 
function_arg_regno_p (int n)
{
  int i;
  for (i = 0; arg_regs[i] != -1; i++)
    if (n == arg_regs[i])
      return true;
  return false;
}

/* Returns 1 if OP contains a symbol reference */

int
symbolic_reference_mentioned_p (rtx op)
{
  register const char *fmt;
  register int i;

  if (GET_CODE (op) == SYMBOL_REF || GET_CODE (op) == LABEL_REF)
    return 1;

  fmt = GET_RTX_FORMAT (GET_CODE (op));
  for (i = GET_RTX_LENGTH (GET_CODE (op)) - 1; i >= 0; i--)
    {
      if (fmt[i] == 'E')
	{
	  register int j;

	  for (j = XVECLEN (op, i) - 1; j >= 0; j--)
	    if (symbolic_reference_mentioned_p (XVECEXP (op, i, j)))
	      return 1;
	}

      else if (fmt[i] == 'e' && symbolic_reference_mentioned_p (XEXP (op, i)))
	return 1;
    }

  return 0;
}

/* Decide whether we can make a sibling call to a function.  DECL is the
   declaration of the function being targeted by the call and EXP is the
   CALL_EXPR representing the call.  */

static bool
bfin_function_ok_for_sibcall (tree decl ATTRIBUTE_UNUSED,
			      tree exp ATTRIBUTE_UNUSED)
{
  e_funkind fkind = funkind (TREE_TYPE (current_function_decl));
  return fkind == SUBROUTINE;
}

/* Emit RTL insns to initialize the variable parts of a trampoline at
   TRAMP. FNADDR is an RTX for the address of the function's pure
   code.  CXT is an RTX for the static chain value for the function.  */

void
initialize_trampoline (tramp, fnaddr, cxt)
     rtx tramp, fnaddr, cxt;
{
  rtx t1 = copy_to_reg (fnaddr);
  rtx t2 = copy_to_reg (cxt);
  rtx addr;
  int i = 0;

  if (TARGET_FDPIC)
    {
      rtx a = memory_address (Pmode, plus_constant (tramp, 8));
      addr = memory_address (Pmode, tramp);
      emit_move_insn (gen_rtx_MEM (SImode, addr), a);
      i = 8;
    }

  addr = memory_address (Pmode, plus_constant (tramp, i + 2));
  emit_move_insn (gen_rtx_MEM (HImode, addr), gen_lowpart (HImode, t1));
  emit_insn (gen_ashrsi3 (t1, t1, GEN_INT (16)));
  addr = memory_address (Pmode, plus_constant (tramp, i + 6));
  emit_move_insn (gen_rtx_MEM (HImode, addr), gen_lowpart (HImode, t1));

  addr = memory_address (Pmode, plus_constant (tramp, i + 10));
  emit_move_insn (gen_rtx_MEM (HImode, addr), gen_lowpart (HImode, t2));
  emit_insn (gen_ashrsi3 (t2, t2, GEN_INT (16)));
  addr = memory_address (Pmode, plus_constant (tramp, i + 14));
  emit_move_insn (gen_rtx_MEM (HImode, addr), gen_lowpart (HImode, t2));
}

/* Emit insns to move operands[1] into operands[0].  */

void
emit_pic_move (rtx *operands, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  rtx temp = reload_in_progress ? operands[0] : gen_reg_rtx (Pmode);

  gcc_assert (!TARGET_FDPIC || !(reload_in_progress || reload_completed));
  if (GET_CODE (operands[0]) == MEM && SYMBOLIC_CONST (operands[1]))
    operands[1] = force_reg (SImode, operands[1]);
  else
    operands[1] = legitimize_pic_address (operands[1], temp,
					  TARGET_FDPIC ? OUR_FDPIC_REG
					  : pic_offset_table_rtx);
}

/* Expand a move operation in mode MODE.  The operands are in OPERANDS.  */

void
expand_move (rtx *operands, enum machine_mode mode)
{
  rtx op = operands[1];
  if ((TARGET_ID_SHARED_LIBRARY || TARGET_FDPIC)
      && SYMBOLIC_CONST (op))
    emit_pic_move (operands, mode);
  /* Don't generate memory->memory or constant->memory moves, go through a
     register */
  else if ((reload_in_progress | reload_completed) == 0
	   && GET_CODE (operands[0]) == MEM
    	   && GET_CODE (operands[1]) != REG)
    operands[1] = force_reg (mode, operands[1]);
}

/* Split one or more DImode RTL references into pairs of SImode
   references.  The RTL can be REG, offsettable MEM, integer constant, or
   CONST_DOUBLE.  "operands" is a pointer to an array of DImode RTL to
   split and "num" is its length.  lo_half and hi_half are output arrays
   that parallel "operands".  */

void
split_di (rtx operands[], int num, rtx lo_half[], rtx hi_half[])
{
  while (num--)
    {
      rtx op = operands[num];

      /* simplify_subreg refuse to split volatile memory addresses,
         but we still have to handle it.  */
      if (GET_CODE (op) == MEM)
	{
	  lo_half[num] = adjust_address (op, SImode, 0);
	  hi_half[num] = adjust_address (op, SImode, 4);
	}
      else
	{
	  lo_half[num] = simplify_gen_subreg (SImode, op,
					      GET_MODE (op) == VOIDmode
					      ? DImode : GET_MODE (op), 0);
	  hi_half[num] = simplify_gen_subreg (SImode, op,
					      GET_MODE (op) == VOIDmode
					      ? DImode : GET_MODE (op), 4);
	}
    }
}

bool
bfin_longcall_p (rtx op, int call_cookie)
{
  gcc_assert (GET_CODE (op) == SYMBOL_REF);
  if (call_cookie & CALL_SHORT)
    return 0;
  if (call_cookie & CALL_LONG)
    return 1;
  if (TARGET_LONG_CALLS)
    return 1;
  return 0;
}

/* Expand a call instruction.  FNADDR is the call target, RETVAL the return value.
   COOKIE is a CONST_INT holding the call_cookie prepared init_cumulative_args.
   SIBCALL is nonzero if this is a sibling call.  */

void
bfin_expand_call (rtx retval, rtx fnaddr, rtx callarg1, rtx cookie, int sibcall)
{
  rtx use = NULL, call;
  rtx callee = XEXP (fnaddr, 0);
  int nelts = 2 + !!sibcall;
  rtx pat;
  rtx picreg = get_hard_reg_initial_val (SImode, FDPIC_REGNO);
  int n;

  /* In an untyped call, we can get NULL for operand 2.  */
  if (cookie == NULL_RTX)
    cookie = const0_rtx;

  /* Static functions and indirect calls don't need the pic register.  */
  if (!TARGET_FDPIC && flag_pic
      && GET_CODE (callee) == SYMBOL_REF
      && !SYMBOL_REF_LOCAL_P (callee))
    use_reg (&use, pic_offset_table_rtx);

  if (TARGET_FDPIC)
    {
      if (GET_CODE (callee) != SYMBOL_REF
	  || bfin_longcall_p (callee, INTVAL (cookie)))
	{
	  rtx addr = callee;
	  if (! address_operand (addr, Pmode))
	    addr = force_reg (Pmode, addr);

	  fnaddr = gen_reg_rtx (SImode);
	  emit_insn (gen_load_funcdescsi (fnaddr, addr));
	  fnaddr = gen_rtx_MEM (Pmode, fnaddr);

	  picreg = gen_reg_rtx (SImode);
	  emit_insn (gen_load_funcdescsi (picreg,
					  plus_constant (addr, 4)));
	}

      nelts++;
    }
  else if ((!register_no_elim_operand (callee, Pmode)
	    && GET_CODE (callee) != SYMBOL_REF)
	   || (GET_CODE (callee) == SYMBOL_REF
	       && (flag_pic
		   || bfin_longcall_p (callee, INTVAL (cookie)))))
    {
      callee = copy_to_mode_reg (Pmode, callee);
      fnaddr = gen_rtx_MEM (Pmode, callee);
    }
  call = gen_rtx_CALL (VOIDmode, fnaddr, callarg1);

  if (retval)
    call = gen_rtx_SET (VOIDmode, retval, call);

  pat = gen_rtx_PARALLEL (VOIDmode, rtvec_alloc (nelts));
  n = 0;
  XVECEXP (pat, 0, n++) = call;
  if (TARGET_FDPIC)
    XVECEXP (pat, 0, n++) = gen_rtx_USE (VOIDmode, picreg);
  XVECEXP (pat, 0, n++) = gen_rtx_USE (VOIDmode, cookie);
  if (sibcall)
    XVECEXP (pat, 0, n++) = gen_rtx_RETURN (VOIDmode);
  call = emit_call_insn (pat);
  if (use)
    CALL_INSN_FUNCTION_USAGE (call) = use;
}

/* Return 1 if hard register REGNO can hold a value of machine-mode MODE.  */

int
hard_regno_mode_ok (int regno, enum machine_mode mode)
{
  /* Allow only dregs to store value of mode HI or QI */
  enum reg_class class = REGNO_REG_CLASS (regno);

  if (mode == CCmode)
    return 0;

  if (mode == V2HImode)
    return D_REGNO_P (regno);
  if (class == CCREGS)
    return mode == BImode;
  if (mode == PDImode || mode == V2PDImode)
    return regno == REG_A0 || regno == REG_A1;
  if (mode == SImode
      && TEST_HARD_REG_BIT (reg_class_contents[PROLOGUE_REGS], regno))
    return 1;
      
  return TEST_HARD_REG_BIT (reg_class_contents[MOST_REGS], regno);
}

/* Implements target hook vector_mode_supported_p.  */

static bool
bfin_vector_mode_supported_p (enum machine_mode mode)
{
  return mode == V2HImode;
}

/* Return the cost of moving data from a register in class CLASS1 to
   one in class CLASS2.  A cost of 2 is the default.  */

int
bfin_register_move_cost (enum machine_mode mode ATTRIBUTE_UNUSED,
			 enum reg_class class1, enum reg_class class2)
{
  /* These need secondary reloads, so they're more expensive.  */
  if ((class1 == CCREGS && class2 != DREGS)
      || (class1 != DREGS && class2 == CCREGS))
    return 4;

  /* If optimizing for size, always prefer reg-reg over reg-memory moves.  */
  if (optimize_size)
    return 2;

  /* There are some stalls involved when moving from a DREG to a different
     class reg, and using the value in one of the following instructions.
     Attempt to model this by slightly discouraging such moves.  */
  if (class1 == DREGS && class2 != DREGS)
    return 2 * 2;

  return 2;
}

/* Return the cost of moving data of mode M between a
   register and memory.  A value of 2 is the default; this cost is
   relative to those in `REGISTER_MOVE_COST'.

   ??? In theory L1 memory has single-cycle latency.  We should add a switch
   that tells the compiler whether we expect to use only L1 memory for the
   program; it'll make the costs more accurate.  */

int
bfin_memory_move_cost (enum machine_mode mode ATTRIBUTE_UNUSED,
		       enum reg_class class,
		       int in ATTRIBUTE_UNUSED)
{
  /* Make memory accesses slightly more expensive than any register-register
     move.  Also, penalize non-DP registers, since they need secondary
     reloads to load and store.  */
  if (! reg_class_subset_p (class, DPREGS))
    return 10;

  return 8;
}

/* Inform reload about cases where moving X with a mode MODE to a register in
   CLASS requires an extra scratch register.  Return the class needed for the
   scratch register.  */

static enum reg_class
bfin_secondary_reload (bool in_p, rtx x, enum reg_class class,
		     enum machine_mode mode, secondary_reload_info *sri)
{
  /* If we have HImode or QImode, we can only use DREGS as secondary registers;
     in most other cases we can also use PREGS.  */
  enum reg_class default_class = GET_MODE_SIZE (mode) >= 4 ? DPREGS : DREGS;
  enum reg_class x_class = NO_REGS;
  enum rtx_code code = GET_CODE (x);

  if (code == SUBREG)
    x = SUBREG_REG (x), code = GET_CODE (x);
  if (REG_P (x))
    {
      int regno = REGNO (x);
      if (regno >= FIRST_PSEUDO_REGISTER)
	regno = reg_renumber[regno];

      if (regno == -1)
	code = MEM;
      else
	x_class = REGNO_REG_CLASS (regno);
    }

  /* We can be asked to reload (plus (FP) (large_constant)) into a DREG.
     This happens as a side effect of register elimination, and we need
     a scratch register to do it.  */
  if (fp_plus_const_operand (x, mode))
    {
      rtx op2 = XEXP (x, 1);
      int large_constant_p = ! CONST_7BIT_IMM_P (INTVAL (op2));

      if (class == PREGS || class == PREGS_CLOBBERED)
	return NO_REGS;
      /* If destination is a DREG, we can do this without a scratch register
	 if the constant is valid for an add instruction.  */
      if ((class == DREGS || class == DPREGS)
	  && ! large_constant_p)
	return NO_REGS;
      /* Reloading to anything other than a DREG?  Use a PREG scratch
	 register.  */
      sri->icode = CODE_FOR_reload_insi;
      return NO_REGS;
    }

  /* Data can usually be moved freely between registers of most classes.
     AREGS are an exception; they can only move to or from another register
     in AREGS or one in DREGS.  They can also be assigned the constant 0.  */
  if (x_class == AREGS)
    return class == DREGS || class == AREGS ? NO_REGS : DREGS;

  if (class == AREGS)
    {
      if (x != const0_rtx && x_class != DREGS)
	return DREGS;
      else
	return NO_REGS;
    }

  /* CCREGS can only be moved from/to DREGS.  */
  if (class == CCREGS && x_class != DREGS)
    return DREGS;
  if (x_class == CCREGS && class != DREGS)
    return DREGS;

  /* All registers other than AREGS can load arbitrary constants.  The only
     case that remains is MEM.  */
  if (code == MEM)
    if (! reg_class_subset_p (class, default_class))
      return default_class;
  return NO_REGS;
}

/* Implement TARGET_HANDLE_OPTION.  */

static bool
bfin_handle_option (size_t code, const char *arg, int value)
{
  switch (code)
    {
    case OPT_mshared_library_id_:
      if (value > MAX_LIBRARY_ID)
	error ("-mshared-library-id=%s is not between 0 and %d",
	       arg, MAX_LIBRARY_ID);
      bfin_lib_id_given = 1;
      return true;

    default:
      return true;
    }
}

static struct machine_function *
bfin_init_machine_status (void)
{
  struct machine_function *f;

  f = ggc_alloc_cleared (sizeof (struct machine_function));

  return f;
}

/* Implement the macro OVERRIDE_OPTIONS.  */

void
override_options (void)
{
  if (TARGET_OMIT_LEAF_FRAME_POINTER)
    flag_omit_frame_pointer = 1;

  /* Library identification */
  if (bfin_lib_id_given && ! TARGET_ID_SHARED_LIBRARY)
    error ("-mshared-library-id= specified without -mid-shared-library");

  if (TARGET_ID_SHARED_LIBRARY && flag_pic == 0)
    flag_pic = 1;

  if (TARGET_ID_SHARED_LIBRARY && TARGET_FDPIC)
      error ("ID shared libraries and FD-PIC mode can't be used together.");

  /* There is no single unaligned SI op for PIC code.  Sometimes we
     need to use ".4byte" and sometimes we need to use ".picptr".
     See bfin_assemble_integer for details.  */
  if (TARGET_FDPIC)
    targetm.asm_out.unaligned_op.si = 0;

  /* Silently turn off flag_pic if not doing FDPIC or ID shared libraries,
     since we don't support it and it'll just break.  */
  if (flag_pic && !TARGET_FDPIC && !TARGET_ID_SHARED_LIBRARY)
    flag_pic = 0;

  flag_schedule_insns = 0;

  init_machine_status = bfin_init_machine_status;
}

/* Return the destination address of BRANCH.
   We need to use this instead of get_attr_length, because the
   cbranch_with_nops pattern conservatively sets its length to 6, and
   we still prefer to use shorter sequences.  */

static int
branch_dest (rtx branch)
{
  rtx dest;
  int dest_uid;
  rtx pat = PATTERN (branch);
  if (GET_CODE (pat) == PARALLEL)
    pat = XVECEXP (pat, 0, 0);
  dest = SET_SRC (pat);
  if (GET_CODE (dest) == IF_THEN_ELSE)
    dest = XEXP (dest, 1);
  dest = XEXP (dest, 0);
  dest_uid = INSN_UID (dest);
  return INSN_ADDRESSES (dest_uid);
}

/* Return nonzero if INSN is annotated with a REG_BR_PROB note that indicates
   it's a branch that's predicted taken.  */

static int
cbranch_predicted_taken_p (rtx insn)
{
  rtx x = find_reg_note (insn, REG_BR_PROB, 0);

  if (x)
    {
      int pred_val = INTVAL (XEXP (x, 0));

      return pred_val >= REG_BR_PROB_BASE / 2;
    }

  return 0;
}

/* Templates for use by asm_conditional_branch.  */

static const char *ccbranch_templates[][3] = {
  { "if !cc jump %3;",  "if cc jump 4 (bp); jump.s %3;",  "if cc jump 6 (bp); jump.l %3;" },
  { "if cc jump %3;",   "if !cc jump 4 (bp); jump.s %3;", "if !cc jump 6 (bp); jump.l %3;" },
  { "if !cc jump %3 (bp);",  "if cc jump 4; jump.s %3;",  "if cc jump 6; jump.l %3;" },
  { "if cc jump %3 (bp);",  "if !cc jump 4; jump.s %3;",  "if !cc jump 6; jump.l %3;" },
};

/* Output INSN, which is a conditional branch instruction with operands
   OPERANDS.

   We deal with the various forms of conditional branches that can be generated
   by bfin_reorg to prevent the hardware from doing speculative loads, by
   - emitting a sufficient number of nops, if N_NOPS is nonzero, or
   - always emitting the branch as predicted taken, if PREDICT_TAKEN is true.
   Either of these is only necessary if the branch is short, otherwise the
   template we use ends in an unconditional jump which flushes the pipeline
   anyway.  */

void
asm_conditional_branch (rtx insn, rtx *operands, int n_nops, int predict_taken)
{
  int offset = branch_dest (insn) - INSN_ADDRESSES (INSN_UID (insn));
  /* Note : offset for instructions like if cc jmp; jump.[sl] offset
            is to be taken from start of if cc rather than jump.
            Range for jump.s is (-4094, 4096) instead of (-4096, 4094)
  */
  int len = (offset >= -1024 && offset <= 1022 ? 0
	     : offset >= -4094 && offset <= 4096 ? 1
	     : 2);
  int bp = predict_taken && len == 0 ? 1 : cbranch_predicted_taken_p (insn);
  int idx = (bp << 1) | (GET_CODE (operands[0]) == EQ ? BRF : BRT);
  output_asm_insn (ccbranch_templates[idx][len], operands);
  gcc_assert (n_nops == 0 || !bp);
  if (len == 0)
    while (n_nops-- > 0)
      output_asm_insn ("nop;", NULL);
}

/* Emit rtl for a comparison operation CMP in mode MODE.  Operands have been
   stored in bfin_compare_op0 and bfin_compare_op1 already.  */

rtx
bfin_gen_compare (rtx cmp, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  enum rtx_code code1, code2;
  rtx op0 = bfin_compare_op0, op1 = bfin_compare_op1;
  rtx tem = bfin_cc_rtx;
  enum rtx_code code = GET_CODE (cmp);

  /* If we have a BImode input, then we already have a compare result, and
     do not need to emit another comparison.  */
  if (GET_MODE (op0) == BImode)
    {
      gcc_assert ((code == NE || code == EQ) && op1 == const0_rtx);
      tem = op0, code2 = code;
    }
  else
    {
      switch (code) {
	/* bfin has these conditions */
      case EQ:
      case LT:
      case LE:
      case LEU:
      case LTU:
	code1 = code;
	code2 = NE;
	break;
      default:
	code1 = reverse_condition (code);
	code2 = EQ;
	break;
      }
      emit_insn (gen_rtx_SET (BImode, tem,
			      gen_rtx_fmt_ee (code1, BImode, op0, op1)));
    }

  return gen_rtx_fmt_ee (code2, BImode, tem, CONST0_RTX (BImode));
}

/* Return nonzero iff C has exactly one bit set if it is interpreted
   as a 32 bit constant.  */

int
log2constp (unsigned HOST_WIDE_INT c)
{
  c &= 0xFFFFFFFF;
  return c != 0 && (c & (c-1)) == 0;
}

/* Returns the number of consecutive least significant zeros in the binary
   representation of *V.
   We modify *V to contain the original value arithmetically shifted right by
   the number of zeroes.  */

static int
shiftr_zero (HOST_WIDE_INT *v)
{
  unsigned HOST_WIDE_INT tmp = *v;
  unsigned HOST_WIDE_INT sgn;
  int n = 0;

  if (tmp == 0)
    return 0;

  sgn = tmp & ((unsigned HOST_WIDE_INT) 1 << (HOST_BITS_PER_WIDE_INT - 1));
  while ((tmp & 0x1) == 0 && n <= 32)
    {
      tmp = (tmp >> 1) | sgn;
      n++;
    }
  *v = tmp;
  return n;
}

/* After reload, split the load of an immediate constant.  OPERANDS are the
   operands of the movsi_insn pattern which we are splitting.  We return
   nonzero if we emitted a sequence to load the constant, zero if we emitted
   nothing because we want to use the splitter's default sequence.  */

int
split_load_immediate (rtx operands[])
{
  HOST_WIDE_INT val = INTVAL (operands[1]);
  HOST_WIDE_INT tmp;
  HOST_WIDE_INT shifted = val;
  HOST_WIDE_INT shifted_compl = ~val;
  int num_zero = shiftr_zero (&shifted);
  int num_compl_zero = shiftr_zero (&shifted_compl);
  unsigned int regno = REGNO (operands[0]);
  enum reg_class class1 = REGNO_REG_CLASS (regno);

  /* This case takes care of single-bit set/clear constants, which we could
     also implement with BITSET/BITCLR.  */
  if (num_zero
      && shifted >= -32768 && shifted < 65536
      && (D_REGNO_P (regno)
	  || (regno >= REG_P0 && regno <= REG_P7 && num_zero <= 2)))
    {
      emit_insn (gen_movsi (operands[0], GEN_INT (shifted)));
      emit_insn (gen_ashlsi3 (operands[0], operands[0], GEN_INT (num_zero)));
      return 1;
    }

  tmp = val & 0xFFFF;
  tmp |= -(tmp & 0x8000);

  /* If high word has one bit set or clear, try to use a bit operation.  */
  if (D_REGNO_P (regno))
    {
      if (log2constp (val & 0xFFFF0000))
	{
	  emit_insn (gen_movsi (operands[0], GEN_INT (val & 0xFFFF)));
	  emit_insn (gen_iorsi3 (operands[0], operands[0], GEN_INT (val & 0xFFFF0000)));
	  return 1;
	}
      else if (log2constp (val | 0xFFFF) && (val & 0x8000) != 0)
	{
	  emit_insn (gen_movsi (operands[0], GEN_INT (tmp)));
	  emit_insn (gen_andsi3 (operands[0], operands[0], GEN_INT (val | 0xFFFF)));
	}
    }

  if (D_REGNO_P (regno))
    {
      if (CONST_7BIT_IMM_P (tmp))
	{
	  emit_insn (gen_movsi (operands[0], GEN_INT (tmp)));
	  emit_insn (gen_movstricthi_high (operands[0], GEN_INT (val & -65536)));
	  return 1;
	}

      if ((val & 0xFFFF0000) == 0)
	{
	  emit_insn (gen_movsi (operands[0], const0_rtx));
	  emit_insn (gen_movsi_low (operands[0], operands[0], operands[1]));
	  return 1;
	}

      if ((val & 0xFFFF0000) == 0xFFFF0000)
	{
	  emit_insn (gen_movsi (operands[0], constm1_rtx));
	  emit_insn (gen_movsi_low (operands[0], operands[0], operands[1]));
	  return 1;
	}
    }

  /* Need DREGs for the remaining case.  */
  if (regno > REG_R7)
    return 0;

  if (optimize_size
      && num_compl_zero && CONST_7BIT_IMM_P (shifted_compl))
    {
      /* If optimizing for size, generate a sequence that has more instructions
	 but is shorter.  */
      emit_insn (gen_movsi (operands[0], GEN_INT (shifted_compl)));
      emit_insn (gen_ashlsi3 (operands[0], operands[0],
			      GEN_INT (num_compl_zero)));
      emit_insn (gen_one_cmplsi2 (operands[0], operands[0]));
      return 1;
    }
  return 0;
}

/* Return true if the legitimate memory address for a memory operand of mode
   MODE.  Return false if not.  */

static bool
bfin_valid_add (enum machine_mode mode, HOST_WIDE_INT value)
{
  unsigned HOST_WIDE_INT v = value > 0 ? value : -value;
  int sz = GET_MODE_SIZE (mode);
  int shift = sz == 1 ? 0 : sz == 2 ? 1 : 2;
  /* The usual offsettable_memref machinery doesn't work so well for this
     port, so we deal with the problem here.  */
  unsigned HOST_WIDE_INT mask = sz == 8 ? 0x7ffe : 0x7fff;
  return (v & ~(mask << shift)) == 0;
}

static bool
bfin_valid_reg_p (unsigned int regno, int strict, enum machine_mode mode,
		  enum rtx_code outer_code)
{
  if (strict)
    return REGNO_OK_FOR_BASE_STRICT_P (regno, mode, outer_code, SCRATCH);
  else
    return REGNO_OK_FOR_BASE_NONSTRICT_P (regno, mode, outer_code, SCRATCH);
}

bool
bfin_legitimate_address_p (enum machine_mode mode, rtx x, int strict)
{
  switch (GET_CODE (x)) {
  case REG:
    if (bfin_valid_reg_p (REGNO (x), strict, mode, MEM))
      return true;
    break;
  case PLUS:
    if (REG_P (XEXP (x, 0))
	&& bfin_valid_reg_p (REGNO (XEXP (x, 0)), strict, mode, PLUS)
	&& ((GET_CODE (XEXP (x, 1)) == UNSPEC && mode == SImode)
	    || (GET_CODE (XEXP (x, 1)) == CONST_INT
		&& bfin_valid_add (mode, INTVAL (XEXP (x, 1))))))
      return true;
    break;
  case POST_INC:
  case POST_DEC:
    if (LEGITIMATE_MODE_FOR_AUTOINC_P (mode)
	&& REG_P (XEXP (x, 0))
	&& bfin_valid_reg_p (REGNO (XEXP (x, 0)), strict, mode, POST_INC))
      return true;
  case PRE_DEC:
    if (LEGITIMATE_MODE_FOR_AUTOINC_P (mode)
	&& XEXP (x, 0) == stack_pointer_rtx
	&& REG_P (XEXP (x, 0))
	&& bfin_valid_reg_p (REGNO (XEXP (x, 0)), strict, mode, PRE_DEC))
      return true;
    break;
  default:
    break;
  }
  return false;
}

static bool
bfin_rtx_costs (rtx x, int code, int outer_code, int *total)
{
  int cost2 = COSTS_N_INSNS (1);

  switch (code)
    {
    case CONST_INT:
      if (outer_code == SET || outer_code == PLUS)
        *total = CONST_7BIT_IMM_P (INTVAL (x)) ? 0 : cost2;
      else if (outer_code == AND)
        *total = log2constp (~INTVAL (x)) ? 0 : cost2;
      else if (outer_code == LE || outer_code == LT || outer_code == EQ)
        *total = (INTVAL (x) >= -4 && INTVAL (x) <= 3) ? 0 : cost2;
      else if (outer_code == LEU || outer_code == LTU)
        *total = (INTVAL (x) >= 0 && INTVAL (x) <= 7) ? 0 : cost2;
      else if (outer_code == MULT)
        *total = (INTVAL (x) == 2 || INTVAL (x) == 4) ? 0 : cost2;
      else if (outer_code == ASHIFT && (INTVAL (x) == 1 || INTVAL (x) == 2))
        *total = 0;
      else if (outer_code == ASHIFT || outer_code == ASHIFTRT
	       || outer_code == LSHIFTRT)
        *total = (INTVAL (x) >= 0 && INTVAL (x) <= 31) ? 0 : cost2;
      else if (outer_code == IOR || outer_code == XOR)
        *total = (INTVAL (x) & (INTVAL (x) - 1)) == 0 ? 0 : cost2;
      else
	*total = cost2;
      return true;

    case CONST:
    case LABEL_REF:
    case SYMBOL_REF:
    case CONST_DOUBLE:
      *total = COSTS_N_INSNS (2);
      return true;

    case PLUS:
      if (GET_MODE (x) == Pmode)
	{
	  if (GET_CODE (XEXP (x, 0)) == MULT
	      && GET_CODE (XEXP (XEXP (x, 0), 1)) == CONST_INT)
	    {
	      HOST_WIDE_INT val = INTVAL (XEXP (XEXP (x, 0), 1));
	      if (val == 2 || val == 4)
		{
		  *total = cost2;
		  *total += rtx_cost (XEXP (XEXP (x, 0), 0), outer_code);
		  *total += rtx_cost (XEXP (x, 1), outer_code);
		  return true;
		}
	    }
	}

      /* fall through */

    case MINUS:
    case ASHIFT: 
    case ASHIFTRT:
    case LSHIFTRT:
      if (GET_MODE (x) == DImode)
	*total = 6 * cost2;
      return false;
	  
    case AND:
    case IOR:
    case XOR:
      if (GET_MODE (x) == DImode)
	*total = 2 * cost2;
      return false;

    case MULT:
      if (GET_MODE_SIZE (GET_MODE (x)) <= UNITS_PER_WORD)
	*total = COSTS_N_INSNS (3);
      return false;

    case UDIV:
    case UMOD:
      *total = COSTS_N_INSNS (32);
      return true;

    case VEC_CONCAT:
    case VEC_SELECT:
      if (outer_code == SET)
	*total = cost2;
      return true;

    default:
      return false;
    }
}

static void
bfin_internal_label (FILE *stream, const char *prefix, unsigned long num)
{
  fprintf (stream, "%s%s$%ld:\n", LOCAL_LABEL_PREFIX, prefix, num);
}

/* Used for communication between {push,pop}_multiple_operation (which
   we use not only as a predicate) and the corresponding output functions.  */
static int first_preg_to_save, first_dreg_to_save;

int
push_multiple_operation (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  int lastdreg = 8, lastpreg = 6;
  int i, group;

  first_preg_to_save = lastpreg;
  first_dreg_to_save = lastdreg;
  for (i = 1, group = 0; i < XVECLEN (op, 0) - 1; i++)
    {
      rtx t = XVECEXP (op, 0, i);
      rtx src, dest;
      int regno;

      if (GET_CODE (t) != SET)
	return 0;

      src = SET_SRC (t);
      dest = SET_DEST (t);
      if (GET_CODE (dest) != MEM || ! REG_P (src))
	return 0;
      dest = XEXP (dest, 0);
      if (GET_CODE (dest) != PLUS
	  || ! REG_P (XEXP (dest, 0))
	  || REGNO (XEXP (dest, 0)) != REG_SP
	  || GET_CODE (XEXP (dest, 1)) != CONST_INT
	  || INTVAL (XEXP (dest, 1)) != -i * 4)
	return 0;

      regno = REGNO (src);
      if (group == 0)
	{
	  if (D_REGNO_P (regno))
	    {
	      group = 1;
	      first_dreg_to_save = lastdreg = regno - REG_R0;
	    }
	  else if (regno >= REG_P0 && regno <= REG_P7)
	    {
	      group = 2;
	      first_preg_to_save = lastpreg = regno - REG_P0;
	    }
	  else
	    return 0;

	  continue;
	}

      if (group == 1)
	{
	  if (regno >= REG_P0 && regno <= REG_P7)
	    {
	      group = 2;
	      first_preg_to_save = lastpreg = regno - REG_P0;
	    }
	  else if (regno != REG_R0 + lastdreg + 1)
	    return 0;
	  else
	    lastdreg++;
	}
      else if (group == 2)
	{
	  if (regno != REG_P0 + lastpreg + 1)
	    return 0;
	  lastpreg++;
	}
    }
  return 1;
}

int
pop_multiple_operation (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  int lastdreg = 8, lastpreg = 6;
  int i, group;

  for (i = 1, group = 0; i < XVECLEN (op, 0); i++)
    {
      rtx t = XVECEXP (op, 0, i);
      rtx src, dest;
      int regno;

      if (GET_CODE (t) != SET)
	return 0;

      src = SET_SRC (t);
      dest = SET_DEST (t);
      if (GET_CODE (src) != MEM || ! REG_P (dest))
	return 0;
      src = XEXP (src, 0);

      if (i == 1)
	{
	  if (! REG_P (src) || REGNO (src) != REG_SP)
	    return 0;
	}
      else if (GET_CODE (src) != PLUS
	       || ! REG_P (XEXP (src, 0))
	       || REGNO (XEXP (src, 0)) != REG_SP
	       || GET_CODE (XEXP (src, 1)) != CONST_INT
	       || INTVAL (XEXP (src, 1)) != (i - 1) * 4)
	return 0;

      regno = REGNO (dest);
      if (group == 0)
	{
	  if (regno == REG_R7)
	    {
	      group = 1;
	      lastdreg = 7;
	    }
	  else if (regno != REG_P0 + lastpreg - 1)
	    return 0;
	  else
	    lastpreg--;
	}
      else if (group == 1)
	{
	  if (regno != REG_R0 + lastdreg - 1)
	    return 0;
	  else
	    lastdreg--;
	}
    }
  first_dreg_to_save = lastdreg;
  first_preg_to_save = lastpreg;
  return 1;
}

/* Emit assembly code for one multi-register push described by INSN, with
   operands in OPERANDS.  */

void
output_push_multiple (rtx insn, rtx *operands)
{
  char buf[80];
  int ok;
  
  /* Validate the insn again, and compute first_[dp]reg_to_save. */
  ok = push_multiple_operation (PATTERN (insn), VOIDmode);
  gcc_assert (ok);
  
  if (first_dreg_to_save == 8)
    sprintf (buf, "[--sp] = ( p5:%d );\n", first_preg_to_save);
  else if (first_preg_to_save == 6)
    sprintf (buf, "[--sp] = ( r7:%d );\n", first_dreg_to_save);
  else
    sprintf (buf, "[--sp] = ( r7:%d, p5:%d );\n",
	     first_dreg_to_save, first_preg_to_save);

  output_asm_insn (buf, operands);
}

/* Emit assembly code for one multi-register pop described by INSN, with
   operands in OPERANDS.  */

void
output_pop_multiple (rtx insn, rtx *operands)
{
  char buf[80];
  int ok;
  
  /* Validate the insn again, and compute first_[dp]reg_to_save. */
  ok = pop_multiple_operation (PATTERN (insn), VOIDmode);
  gcc_assert (ok);

  if (first_dreg_to_save == 8)
    sprintf (buf, "( p5:%d ) = [sp++];\n", first_preg_to_save);
  else if (first_preg_to_save == 6)
    sprintf (buf, "( r7:%d ) = [sp++];\n", first_dreg_to_save);
  else
    sprintf (buf, "( r7:%d, p5:%d ) = [sp++];\n",
	     first_dreg_to_save, first_preg_to_save);

  output_asm_insn (buf, operands);
}

/* Adjust DST and SRC by OFFSET bytes, and generate one move in mode MODE.  */

static void
single_move_for_movmem (rtx dst, rtx src, enum machine_mode mode, HOST_WIDE_INT offset)
{
  rtx scratch = gen_reg_rtx (mode);
  rtx srcmem, dstmem;

  srcmem = adjust_address_nv (src, mode, offset);
  dstmem = adjust_address_nv (dst, mode, offset);
  emit_move_insn (scratch, srcmem);
  emit_move_insn (dstmem, scratch);
}

/* Expand a string move operation of COUNT_EXP bytes from SRC to DST, with
   alignment ALIGN_EXP.  Return true if successful, false if we should fall
   back on a different method.  */

bool
bfin_expand_movmem (rtx dst, rtx src, rtx count_exp, rtx align_exp)
{
  rtx srcreg, destreg, countreg;
  HOST_WIDE_INT align = 0;
  unsigned HOST_WIDE_INT count = 0;

  if (GET_CODE (align_exp) == CONST_INT)
    align = INTVAL (align_exp);
  if (GET_CODE (count_exp) == CONST_INT)
    {
      count = INTVAL (count_exp);
#if 0
      if (!TARGET_INLINE_ALL_STRINGOPS && count > 64)
	return false;
#endif
    }

  /* If optimizing for size, only do single copies inline.  */
  if (optimize_size)
    {
      if (count == 2 && align < 2)
	return false;
      if (count == 4 && align < 4)
	return false;
      if (count != 1 && count != 2 && count != 4)
	return false;
    }
  if (align < 2 && count != 1)
    return false;

  destreg = copy_to_mode_reg (Pmode, XEXP (dst, 0));
  if (destreg != XEXP (dst, 0))
    dst = replace_equiv_address_nv (dst, destreg);
  srcreg = copy_to_mode_reg (Pmode, XEXP (src, 0));
  if (srcreg != XEXP (src, 0))
    src = replace_equiv_address_nv (src, srcreg);

  if (count != 0 && align >= 2)
    {
      unsigned HOST_WIDE_INT offset = 0;

      if (align >= 4)
	{
	  if ((count & ~3) == 4)
	    {
	      single_move_for_movmem (dst, src, SImode, offset);
	      offset = 4;
	    }
	  else if (count & ~3)
	    {
	      HOST_WIDE_INT new_count = ((count >> 2) & 0x3fffffff) - 1;
	      countreg = copy_to_mode_reg (Pmode, GEN_INT (new_count));

	      emit_insn (gen_rep_movsi (destreg, srcreg, countreg, destreg, srcreg));
	    }
	  if (count & 2)
	    {
	      single_move_for_movmem (dst, src, HImode, offset);
	      offset += 2;
	    }
	}
      else
	{
	  if ((count & ~1) == 2)
	    {
	      single_move_for_movmem (dst, src, HImode, offset);
	      offset = 2;
	    }
	  else if (count & ~1)
	    {
	      HOST_WIDE_INT new_count = ((count >> 1) & 0x7fffffff) - 1;
	      countreg = copy_to_mode_reg (Pmode, GEN_INT (new_count));

	      emit_insn (gen_rep_movhi (destreg, srcreg, countreg, destreg, srcreg));
	    }
	}
      if (count & 1)
	{
	  single_move_for_movmem (dst, src, QImode, offset);
	}
      return true;
    }
  return false;
}


static int
bfin_adjust_cost (rtx insn, rtx link, rtx dep_insn, int cost)
{
  enum attr_type insn_type, dep_insn_type;
  int dep_insn_code_number;

  /* Anti and output dependencies have zero cost.  */
  if (REG_NOTE_KIND (link) != 0)
    return 0;

  dep_insn_code_number = recog_memoized (dep_insn);

  /* If we can't recognize the insns, we can't really do anything.  */
  if (dep_insn_code_number < 0 || recog_memoized (insn) < 0)
    return cost;

  insn_type = get_attr_type (insn);
  dep_insn_type = get_attr_type (dep_insn);

  if (dep_insn_type == TYPE_MOVE || dep_insn_type == TYPE_MCLD)
    {
      rtx pat = PATTERN (dep_insn);
      rtx dest = SET_DEST (pat);
      rtx src = SET_SRC (pat);
      if (! ADDRESS_REGNO_P (REGNO (dest)) || ! D_REGNO_P (REGNO (src)))
	return cost;
      return cost + (dep_insn_type == TYPE_MOVE ? 4 : 3);
    }

  return cost;
}


/* Increment the counter for the number of loop instructions in the
   current function.  */

void
bfin_hardware_loop (void)
{
  cfun->machine->has_hardware_loops++;
}

/* Maximum loop nesting depth.  */
#define MAX_LOOP_DEPTH 2

/* Maximum size of a loop.  */
#define MAX_LOOP_LENGTH 2042

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

  /* First block in the loop.  This is the one branched to by the loop_end
     insn.  */
  basic_block head;

  /* Last block in the loop (the one with the loop_end insn).  */
  basic_block tail;

  /* The successor block of the loop.  This is the one the loop_end insn
     falls into.  */
  basic_block successor;

  /* The last instruction in the tail.  */
  rtx last_insn;

  /* The loop_end insn.  */
  rtx loop_end;

  /* The iteration register.  */
  rtx iter_reg;

  /* The new initialization insn.  */
  rtx init;

  /* The new initialization instruction.  */
  rtx loop_init;

  /* The new label placed at the beginning of the loop. */
  rtx start_label;

  /* The new label placed at the end of the loop. */
  rtx end_label;

  /* The length of the loop.  */
  int length;

  /* The nesting depth of the loop.  */
  int depth;

  /* Nonzero if we can't optimize this loop.  */
  int bad;

  /* True if we have visited this loop.  */
  int visited;

  /* True if this loop body clobbers any of LC0, LT0, or LB0.  */
  int clobber_loop0;

  /* True if this loop body clobbers any of LC1, LT1, or LB1.  */
  int clobber_loop1;

  /* Next loop in the graph. */
  struct loop_info *next;

  /* Immediate outer loop of this loop.  */
  struct loop_info *outer;

  /* Vector of blocks only within the loop, including those within
     inner loops.  */
  VEC (basic_block,heap) *blocks;

  /* Same information in a bitmap.  */
  bitmap block_bitmap;

  /* Vector of inner loops within this loop  */
  VEC (loop_info,heap) *loops;
};

static void
bfin_dump_loops (loop_info loops)
{
  loop_info loop;

  for (loop = loops; loop; loop = loop->next)
    {
      loop_info i;
      basic_block b;
      unsigned ix;

      fprintf (dump_file, ";; loop %d: ", loop->loop_no);
      if (loop->bad)
	fprintf (dump_file, "(bad) ");
      fprintf (dump_file, "{head:%d, depth:%d}", loop->head->index, loop->depth);

      fprintf (dump_file, " blocks: [ ");
      for (ix = 0; VEC_iterate (basic_block, loop->blocks, ix, b); ix++)
	fprintf (dump_file, "%d ", b->index);
      fprintf (dump_file, "] ");

      fprintf (dump_file, " inner loops: [ ");
      for (ix = 0; VEC_iterate (loop_info, loop->loops, ix, i); ix++)
	fprintf (dump_file, "%d ", i->loop_no);
      fprintf (dump_file, "]\n");
    }
  fprintf (dump_file, "\n");
}

/* Scan the blocks of LOOP (and its inferiors) looking for basic block
   BB. Return true, if we find it.  */

static bool
bfin_bb_in_loop (loop_info loop, basic_block bb)
{
  return bitmap_bit_p (loop->block_bitmap, bb->index);
}

/* Scan the blocks of LOOP (and its inferiors) looking for uses of
   REG.  Return true, if we find any.  Don't count the loop's loop_end
   insn if it matches LOOP_END.  */

static bool
bfin_scan_loop (loop_info loop, rtx reg, rtx loop_end)
{
  unsigned ix;
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
	  if (insn == loop_end)
	    continue;
	  if (reg_mentioned_p (reg, PATTERN (insn)))
	    return true;
	}
    }
  return false;
}

/* Optimize LOOP.  */

static void
bfin_optimize_loop (loop_info loop)
{
  basic_block bb;
  loop_info inner;
  rtx insn, init_insn, last_insn, nop_insn;
  rtx loop_init, start_label, end_label;
  rtx reg_lc0, reg_lc1, reg_lt0, reg_lt1, reg_lb0, reg_lb1;
  rtx iter_reg;
  rtx lc_reg, lt_reg, lb_reg;
  rtx seq;
  int length;
  unsigned ix;
  int inner_depth = 0;

  if (loop->visited)
    return;

  loop->visited = 1;

  if (loop->bad)
    {
      if (dump_file)
	fprintf (dump_file, ";; loop %d bad when found\n", loop->loop_no);
      goto bad_loop;
    }

  /* Every loop contains in its list of inner loops every loop nested inside
     it, even if there are intermediate loops.  This works because we're doing
     a depth-first search here and never visit a loop more than once.  */
  for (ix = 0; VEC_iterate (loop_info, loop->loops, ix, inner); ix++)
    {
      bfin_optimize_loop (inner);

      if (!inner->bad && inner_depth < inner->depth)
	{
	  inner_depth = inner->depth;

	  loop->clobber_loop0 |= inner->clobber_loop0;
	  loop->clobber_loop1 |= inner->clobber_loop1;
	}
    }

  loop->depth = inner_depth + 1;
  if (loop->depth > MAX_LOOP_DEPTH)
    {
      if (dump_file)
	fprintf (dump_file, ";; loop %d too deep\n", loop->loop_no);
      goto bad_loop;
    }

  /* Get the loop iteration register.  */
  iter_reg = loop->iter_reg;

  if (!DPREG_P (iter_reg))
    {
      if (dump_file)
	fprintf (dump_file, ";; loop %d iteration count NOT in PREG or DREG\n",
		 loop->loop_no);
      goto bad_loop;
    }

  /* Check if start_label appears before loop_end and calculate the
     offset between them.  We calculate the length of instructions
     conservatively.  */
  length = 0;
  for (insn = loop->start_label;
       insn && insn != loop->loop_end;
       insn = NEXT_INSN (insn))
    {
      if (JUMP_P (insn) && any_condjump_p (insn) && !optimize_size)
	{
	  if (TARGET_CSYNC_ANOMALY)
	    length += 8;
	  else if (TARGET_SPECLD_ANOMALY)
	    length += 6;
	}
      else if (LABEL_P (insn))
	{
	  if (TARGET_CSYNC_ANOMALY)
	    length += 4;
	}

      if (INSN_P (insn))
	length += get_attr_length (insn);
    }

  if (!insn)
    {
      if (dump_file)
	fprintf (dump_file, ";; loop %d start_label not before loop_end\n",
		 loop->loop_no);
      goto bad_loop;
    }

  loop->length = length;
  if (loop->length > MAX_LOOP_LENGTH)
    {
      if (dump_file)
	fprintf (dump_file, ";; loop %d too long\n", loop->loop_no);
      goto bad_loop;
    }

  /* Scan all the blocks to make sure they don't use iter_reg.  */
  if (bfin_scan_loop (loop, iter_reg, loop->loop_end))
    {
      if (dump_file)
	fprintf (dump_file, ";; loop %d uses iterator\n", loop->loop_no);
      goto bad_loop;
    }

  /* Scan all the insns to see if the loop body clobber
     any hardware loop registers. */

  reg_lc0 = gen_rtx_REG (SImode, REG_LC0);
  reg_lc1 = gen_rtx_REG (SImode, REG_LC1);
  reg_lt0 = gen_rtx_REG (SImode, REG_LT0);
  reg_lt1 = gen_rtx_REG (SImode, REG_LT1);
  reg_lb0 = gen_rtx_REG (SImode, REG_LB0);
  reg_lb1 = gen_rtx_REG (SImode, REG_LB1);

  for (ix = 0; VEC_iterate (basic_block, loop->blocks, ix, bb); ix++)
    {
      rtx insn;

      for (insn = BB_HEAD (bb);
	   insn != NEXT_INSN (BB_END (bb));
	   insn = NEXT_INSN (insn))
	{
	  if (!INSN_P (insn))
	    continue;

	  if (reg_set_p (reg_lc0, insn)
	      || reg_set_p (reg_lt0, insn)
	      || reg_set_p (reg_lb0, insn))
	    loop->clobber_loop0 = 1;
	  
	  if (reg_set_p (reg_lc1, insn)
	      || reg_set_p (reg_lt1, insn)
	      || reg_set_p (reg_lb1, insn))
	    loop->clobber_loop1 |= 1;
	}
    }

  if ((loop->clobber_loop0 && loop->clobber_loop1)
      || (loop->depth == MAX_LOOP_DEPTH && loop->clobber_loop0))
    {
      loop->depth = MAX_LOOP_DEPTH + 1;
      if (dump_file)
	fprintf (dump_file, ";; loop %d no loop reg available\n",
		 loop->loop_no);
      goto bad_loop;
    }

  /* There should be an instruction before the loop_end instruction
     in the same basic block. And the instruction must not be
     - JUMP
     - CONDITIONAL BRANCH
     - CALL
     - CSYNC
     - SSYNC
     - Returns (RTS, RTN, etc.)  */

  bb = loop->tail;
  last_insn = PREV_INSN (loop->loop_end);

  while (1)
    {
      for (; last_insn != PREV_INSN (BB_HEAD (bb));
	   last_insn = PREV_INSN (last_insn))
	if (INSN_P (last_insn))
	  break;

      if (last_insn != PREV_INSN (BB_HEAD (bb)))
	break;

      if (single_pred_p (bb)
	  && single_pred (bb) != ENTRY_BLOCK_PTR)
	{
	  bb = single_pred (bb);
	  last_insn = BB_END (bb);
	  continue;
	}
      else
	{
	  last_insn = NULL_RTX;
	  break;
	}
    }

  if (!last_insn)
    {
      if (dump_file)
	fprintf (dump_file, ";; loop %d has no last instruction\n",
		 loop->loop_no);
      goto bad_loop;
    }

  if (JUMP_P (last_insn))
    {
      loop_info inner = bb->aux;
      if (inner
	  && inner->outer == loop
	  && inner->loop_end == last_insn
	  && inner->depth == 1)
	/* This jump_insn is the exact loop_end of an inner loop
	   and to be optimized away. So use the inner's last_insn.  */
	last_insn = inner->last_insn;
      else
	{
	  if (dump_file)
	    fprintf (dump_file, ";; loop %d has bad last instruction\n",
		     loop->loop_no);
	  goto bad_loop;
	}
    }
  else if (CALL_P (last_insn)
	   || get_attr_type (last_insn) == TYPE_SYNC
	   || recog_memoized (last_insn) == CODE_FOR_return_internal)
    {
      if (dump_file)
	fprintf (dump_file, ";; loop %d has bad last instruction\n",
		 loop->loop_no);
      goto bad_loop;
    }

  if (GET_CODE (PATTERN (last_insn)) == ASM_INPUT
      || asm_noperands (PATTERN (last_insn)) >= 0
      || get_attr_seq_insns (last_insn) == SEQ_INSNS_MULTI)
    {
      nop_insn = emit_insn_after (gen_nop (), last_insn);
      last_insn = nop_insn;
    }

  loop->last_insn = last_insn;

  /* The loop is good for replacement.  */
  start_label = loop->start_label;
  end_label = gen_label_rtx ();
  iter_reg = loop->iter_reg;

  if (loop->depth == 1 && !loop->clobber_loop1)
    {
      lc_reg = reg_lc1;
      lt_reg = reg_lt1;
      lb_reg = reg_lb1;
      loop->clobber_loop1 = 1;
    }
  else
    {
      lc_reg = reg_lc0;
      lt_reg = reg_lt0;
      lb_reg = reg_lb0;
      loop->clobber_loop0 = 1;
    }

  /* If iter_reg is a DREG, we need generate an instruction to load
     the loop count into LC register. */
  if (D_REGNO_P (REGNO (iter_reg)))
    {
      init_insn = gen_movsi (lc_reg, iter_reg);
      loop_init = gen_lsetup_without_autoinit (lt_reg, start_label,
					       lb_reg, end_label,
					       lc_reg);
    }
  else if (P_REGNO_P (REGNO (iter_reg)))
    {
      init_insn = NULL_RTX;
      loop_init = gen_lsetup_with_autoinit (lt_reg, start_label,
					    lb_reg, end_label,
					    lc_reg, iter_reg);
    }
  else
    gcc_unreachable ();

  loop->init = init_insn;
  loop->end_label = end_label;
  loop->loop_init = loop_init;

  if (dump_file)
    {
      fprintf (dump_file, ";; replacing loop %d initializer with\n",
	       loop->loop_no);
      print_rtl_single (dump_file, loop->loop_init);
      fprintf (dump_file, ";; replacing loop %d terminator with\n",
	       loop->loop_no);
      print_rtl_single (dump_file, loop->loop_end);
    }

  start_sequence ();

  if (loop->init != NULL_RTX)
    emit_insn (loop->init);
  emit_insn(loop->loop_init);
  emit_label (loop->start_label);

  seq = get_insns ();
  end_sequence ();

  emit_insn_after (seq, BB_END (loop->predecessor));
  delete_insn (loop->loop_end);

  /* Insert the loop end label before the last instruction of the loop.  */
  emit_label_before (loop->end_label, loop->last_insn);

  return;

bad_loop:

  if (dump_file)
    fprintf (dump_file, ";; loop %d is bad\n", loop->loop_no);

  loop->bad = 1;

  if (DPREG_P (loop->iter_reg))
    {
      /* If loop->iter_reg is a DREG or PREG, we can split it here
	 without scratch register.  */
      rtx insn;

      emit_insn_before (gen_addsi3 (loop->iter_reg,
				    loop->iter_reg,
				    constm1_rtx),
			loop->loop_end);

      emit_insn_before (gen_cmpsi (loop->iter_reg, const0_rtx),
			loop->loop_end);

      insn = emit_jump_insn_before (gen_bne (loop->start_label),
				    loop->loop_end);

      JUMP_LABEL (insn) = loop->start_label;
      LABEL_NUSES (loop->start_label)++;
      delete_insn (loop->loop_end);
    }
}

/* Called from bfin_reorg_loops when a potential loop end is found.  LOOP is
   a newly set up structure describing the loop, it is this function's
   responsibility to fill most of it.  TAIL_BB and TAIL_INSN point to the
   loop_end insn and its enclosing basic block.  */

static void
bfin_discover_loop (loop_info loop, basic_block tail_bb, rtx tail_insn)
{
  unsigned dwork = 0;
  basic_block bb;
  VEC (basic_block,heap) *works = VEC_alloc (basic_block,heap,20);

  loop->tail = tail_bb;
  loop->head = BRANCH_EDGE (tail_bb)->dest;
  loop->successor = FALLTHRU_EDGE (tail_bb)->dest;
  loop->predecessor = NULL;
  loop->loop_end = tail_insn;
  loop->last_insn = NULL_RTX;
  loop->iter_reg = SET_DEST (XVECEXP (PATTERN (tail_insn), 0, 1));
  loop->depth = loop->length = 0;
  loop->visited = 0;
  loop->clobber_loop0 = loop->clobber_loop1 = 0;
  loop->outer = NULL;
  loop->loops = NULL;

  loop->init = loop->loop_init = NULL_RTX;
  loop->start_label = XEXP (XEXP (SET_SRC (XVECEXP (PATTERN (tail_insn), 0, 0)), 1), 0);
  loop->end_label = NULL_RTX;
  loop->bad = 0;

  VEC_safe_push (basic_block, heap, works, loop->head);

  while (VEC_iterate (basic_block, works, dwork++, bb))
    {
      edge e;
      edge_iterator ei;
      if (bb == EXIT_BLOCK_PTR)
	{
	  /* We've reached the exit block.  The loop must be bad. */
	  if (dump_file)
	    fprintf (dump_file,
		     ";; Loop is bad - reached exit block while scanning\n");
	  loop->bad = 1;
	  break;
	}

      if (bitmap_bit_p (loop->block_bitmap, bb->index))
	continue;

      /* We've not seen this block before.  Add it to the loop's
	 list and then add each successor to the work list.  */

      VEC_safe_push (basic_block, heap, loop->blocks, bb);
      bitmap_set_bit (loop->block_bitmap, bb->index);

      if (bb != tail_bb)
	{
	  FOR_EACH_EDGE (e, ei, bb->succs)
	    {
	      basic_block succ = EDGE_SUCC (bb, ei.index)->dest;
	      if (!REGNO_REG_SET_P (succ->il.rtl->global_live_at_start,
				    REGNO (loop->iter_reg)))
		continue;
	      if (!VEC_space (basic_block, works, 1))
		{
		  if (dwork)
		    {
		      VEC_block_remove (basic_block, works, 0, dwork);
		      dwork = 0;
		    }
		  else
		    VEC_reserve (basic_block, heap, works, 1);
		}
	      VEC_quick_push (basic_block, works, succ);
	    }
	}
    }

  if (!loop->bad)
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
	      || !(EDGE_SUCC (loop->predecessor, 0)->flags & EDGE_FALLTHRU)
	      /* If loop->predecessor is in loop, loop->head is not really
		 the head of the loop.  */
	      || bfin_bb_in_loop (loop, loop->predecessor))
	    loop->predecessor = NULL;
	}

      if (loop->predecessor == NULL)
	{
	  if (dump_file)
	    fprintf (dump_file, ";; loop has bad predecessor\n");
	  loop->bad = 1;
	}
    }

#ifdef ENABLE_CHECKING
  /* Make sure nothing jumps into this loop.  This shouldn't happen as we
     wouldn't have generated the counted loop patterns in such a case.
     However, this test must be done after the test above to detect loops
     with invalid headers.  */
  if (!loop->bad)
    for (dwork = 0; VEC_iterate (basic_block, loop->blocks, dwork, bb); dwork++)
      {
	edge e;
	edge_iterator ei;
	if (bb == loop->head)
	  continue;
	FOR_EACH_EDGE (e, ei, bb->preds)
	  {
	    basic_block pred = EDGE_PRED (bb, ei.index)->src;
	    if (!bfin_bb_in_loop (loop, pred))
	      abort ();
	  }
      }
#endif
  VEC_free (basic_block, heap, works);
}

static void
bfin_reorg_loops (FILE *dump_file)
{
  bitmap_obstack stack;
  bitmap tmp_bitmap;
  basic_block bb;
  loop_info loops = NULL;
  loop_info loop;
  int nloops = 0;

  bitmap_obstack_initialize (&stack);

  /* Find all the possible loop tails.  This means searching for every
     loop_end instruction.  For each one found, create a loop_info
     structure and add the head block to the work list. */
  FOR_EACH_BB (bb)
    {
      rtx tail = BB_END (bb);

      while (GET_CODE (tail) == NOTE)
	tail = PREV_INSN (tail);

      bb->aux = NULL;

      if (INSN_P (tail) && recog_memoized (tail) == CODE_FOR_loop_end)
	{
	  /* A possible loop end */

	  loop = XNEW (struct loop_info);
	  loop->next = loops;
	  loops = loop;
	  loop->loop_no = nloops++;
	  loop->blocks = VEC_alloc (basic_block, heap, 20);
	  loop->block_bitmap = BITMAP_ALLOC (&stack);
	  bb->aux = loop;

	  if (dump_file)
	    {
	      fprintf (dump_file, ";; potential loop %d ending at\n",
		       loop->loop_no);
	      print_rtl_single (dump_file, tail);
	    }

	  bfin_discover_loop (loop, bb, tail);
	}
    }

  tmp_bitmap = BITMAP_ALLOC (&stack);
  /* Compute loop nestings.  */
  for (loop = loops; loop; loop = loop->next)
    {
      loop_info other;
      if (loop->bad)
	continue;

      for (other = loop->next; other; other = other->next)
	{
	  if (other->bad)
	    continue;

	  bitmap_and (tmp_bitmap, other->block_bitmap, loop->block_bitmap);
	  if (bitmap_empty_p (tmp_bitmap))
	    continue;
	  if (bitmap_equal_p (tmp_bitmap, other->block_bitmap))
	    {
	      other->outer = loop;
	      VEC_safe_push (loop_info, heap, loop->loops, other);
	    }
	  else if (bitmap_equal_p (tmp_bitmap, loop->block_bitmap))
	    {
	      loop->outer = other;
	      VEC_safe_push (loop_info, heap, other->loops, loop);
	    }
	  else
	    {
	      loop->bad = other->bad = 1;
	    }
	}
    }
  BITMAP_FREE (tmp_bitmap);

  if (dump_file)
    {
      fprintf (dump_file, ";; All loops found:\n\n");
      bfin_dump_loops (loops);
    }
  
  /* Now apply the optimizations.  */
  for (loop = loops; loop; loop = loop->next)
    bfin_optimize_loop (loop);

  if (dump_file)
    {
      fprintf (dump_file, ";; After hardware loops optimization:\n\n");
      bfin_dump_loops (loops);
    }

  /* Free up the loop structures */
  while (loops)
    {
      loop = loops;
      loops = loop->next;
      VEC_free (loop_info, heap, loop->loops);
      VEC_free (basic_block, heap, loop->blocks);
      BITMAP_FREE (loop->block_bitmap);
      XDELETE (loop);
    }

  if (dump_file)
    print_rtl (dump_file, get_insns ());
}


/* We use the machine specific reorg pass for emitting CSYNC instructions
   after conditional branches as needed.

   The Blackfin is unusual in that a code sequence like
     if cc jump label
     r0 = (p0)
   may speculatively perform the load even if the condition isn't true.  This
   happens for a branch that is predicted not taken, because the pipeline
   isn't flushed or stalled, so the early stages of the following instructions,
   which perform the memory reference, are allowed to execute before the
   jump condition is evaluated.
   Therefore, we must insert additional instructions in all places where this
   could lead to incorrect behavior.  The manual recommends CSYNC, while
   VDSP seems to use NOPs (even though its corresponding compiler option is
   named CSYNC).

   When optimizing for speed, we emit NOPs, which seems faster than a CSYNC.
   When optimizing for size, we turn the branch into a predicted taken one.
   This may be slower due to mispredicts, but saves code size.  */

static void
bfin_reorg (void)
{
  rtx insn, last_condjump = NULL_RTX;
  int cycles_since_jump = INT_MAX;

  /* Doloop optimization */
  if (cfun->machine->has_hardware_loops)
    bfin_reorg_loops (dump_file);

  if (! TARGET_SPECLD_ANOMALY && ! TARGET_CSYNC_ANOMALY)
    return;

  /* First pass: find predicted-false branches; if something after them
     needs nops, insert them or change the branch to predict true.  */
  for (insn = get_insns (); insn; insn = NEXT_INSN (insn))
    {
      rtx pat;

      if (NOTE_P (insn) || BARRIER_P (insn) || LABEL_P (insn))
	continue;

      pat = PATTERN (insn);
      if (GET_CODE (pat) == USE || GET_CODE (pat) == CLOBBER
	  || GET_CODE (pat) == ASM_INPUT || GET_CODE (pat) == ADDR_VEC
	  || GET_CODE (pat) == ADDR_DIFF_VEC || asm_noperands (pat) >= 0)
	continue;

      if (JUMP_P (insn))
	{
	  if (any_condjump_p (insn)
	      && ! cbranch_predicted_taken_p (insn))
	    {
	      last_condjump = insn;
	      cycles_since_jump = 0;
	    }
	  else
	    cycles_since_jump = INT_MAX;
	}
      else if (INSN_P (insn))
	{
	  enum attr_type type = get_attr_type (insn);
	  int delay_needed = 0;
	  if (cycles_since_jump < INT_MAX)
	    cycles_since_jump++;

	  if (type == TYPE_MCLD && TARGET_SPECLD_ANOMALY)
	    {
	      rtx pat = single_set (insn);
	      if (may_trap_p (SET_SRC (pat)))
		delay_needed = 3;
	    }
	  else if (type == TYPE_SYNC && TARGET_CSYNC_ANOMALY)
	    delay_needed = 4;

	  if (delay_needed > cycles_since_jump)
	    {
	      rtx pat;
	      int num_clobbers;
	      rtx *op = recog_data.operand;

	      delay_needed -= cycles_since_jump;

	      extract_insn (last_condjump);
	      if (optimize_size)
		{
		  pat = gen_cbranch_predicted_taken (op[0], op[1], op[2],
						     op[3]);
		  cycles_since_jump = INT_MAX;
		}
	      else
		/* Do not adjust cycles_since_jump in this case, so that
		   we'll increase the number of NOPs for a subsequent insn
		   if necessary.  */
		pat = gen_cbranch_with_nops (op[0], op[1], op[2], op[3],
					     GEN_INT (delay_needed));
	      PATTERN (last_condjump) = pat;
	      INSN_CODE (last_condjump) = recog (pat, insn, &num_clobbers);
	    }
	}
    }
  /* Second pass: for predicted-true branches, see if anything at the
     branch destination needs extra nops.  */
  if (! TARGET_CSYNC_ANOMALY)
    return;

  for (insn = get_insns (); insn; insn = NEXT_INSN (insn))
    {
      if (JUMP_P (insn)
	  && any_condjump_p (insn)
	  && (INSN_CODE (insn) == CODE_FOR_cbranch_predicted_taken
	      || cbranch_predicted_taken_p (insn)))
	{
	  rtx target = JUMP_LABEL (insn);
	  rtx label = target;
	  cycles_since_jump = 0;
	  for (; target && cycles_since_jump < 3; target = NEXT_INSN (target))
	    {
	      rtx pat;

	      if (NOTE_P (target) || BARRIER_P (target) || LABEL_P (target))
		continue;

	      pat = PATTERN (target);
	      if (GET_CODE (pat) == USE || GET_CODE (pat) == CLOBBER
		  || GET_CODE (pat) == ASM_INPUT || GET_CODE (pat) == ADDR_VEC
		  || GET_CODE (pat) == ADDR_DIFF_VEC || asm_noperands (pat) >= 0)
		continue;

	      if (INSN_P (target))
		{
		  enum attr_type type = get_attr_type (target);
		  int delay_needed = 0;
		  if (cycles_since_jump < INT_MAX)
		    cycles_since_jump++;

		  if (type == TYPE_SYNC && TARGET_CSYNC_ANOMALY)
		    delay_needed = 2;

		  if (delay_needed > cycles_since_jump)
		    {
		      rtx prev = prev_real_insn (label);
		      delay_needed -= cycles_since_jump;
		      if (dump_file)
			fprintf (dump_file, "Adding %d nops after %d\n",
				 delay_needed, INSN_UID (label));
		      if (JUMP_P (prev)
			  && INSN_CODE (prev) == CODE_FOR_cbranch_with_nops)
			{
			  rtx x;
			  HOST_WIDE_INT v;

			  if (dump_file)
			    fprintf (dump_file,
				     "Reducing nops on insn %d.\n",
				     INSN_UID (prev));
			  x = PATTERN (prev);
			  x = XVECEXP (x, 0, 1);
			  v = INTVAL (XVECEXP (x, 0, 0)) - delay_needed;
			  XVECEXP (x, 0, 0) = GEN_INT (v);
			}
		      while (delay_needed-- > 0)
			emit_insn_after (gen_nop (), label);
		      break;
		    }
		}
	    }
	}
    }
}

/* Handle interrupt_handler, exception_handler and nmi_handler function
   attributes; arguments as in struct attribute_spec.handler.  */

static tree
handle_int_attribute (tree *node, tree name,
		      tree args ATTRIBUTE_UNUSED,
		      int flags ATTRIBUTE_UNUSED,
		      bool *no_add_attrs)
{
  tree x = *node;
  if (TREE_CODE (x) == FUNCTION_DECL)
    x = TREE_TYPE (x);

  if (TREE_CODE (x) != FUNCTION_TYPE)
    {
      warning (OPT_Wattributes, "%qs attribute only applies to functions",
	       IDENTIFIER_POINTER (name));
      *no_add_attrs = true;
    }
  else if (funkind (x) != SUBROUTINE)
    error ("multiple function type attributes specified");

  return NULL_TREE;
}

/* Return 0 if the attributes for two types are incompatible, 1 if they
   are compatible, and 2 if they are nearly compatible (which causes a
   warning to be generated).  */

static int
bfin_comp_type_attributes (tree type1, tree type2)
{
  e_funkind kind1, kind2;

  if (TREE_CODE (type1) != FUNCTION_TYPE)
    return 1;

  kind1 = funkind (type1);
  kind2 = funkind (type2);

  if (kind1 != kind2)
    return 0;
  
  /*  Check for mismatched modifiers */
  if (!lookup_attribute ("nesting", TYPE_ATTRIBUTES (type1))
      != !lookup_attribute ("nesting", TYPE_ATTRIBUTES (type2)))
    return 0;

  if (!lookup_attribute ("saveall", TYPE_ATTRIBUTES (type1))
      != !lookup_attribute ("saveall", TYPE_ATTRIBUTES (type2)))
    return 0;

  if (!lookup_attribute ("kspisusp", TYPE_ATTRIBUTES (type1))
      != !lookup_attribute ("kspisusp", TYPE_ATTRIBUTES (type2)))
    return 0;

  if (!lookup_attribute ("longcall", TYPE_ATTRIBUTES (type1))
      != !lookup_attribute ("longcall", TYPE_ATTRIBUTES (type2)))
    return 0;

  return 1;
}

/* Handle a "longcall" or "shortcall" attribute; arguments as in
   struct attribute_spec.handler.  */

static tree
bfin_handle_longcall_attribute (tree *node, tree name, 
				tree args ATTRIBUTE_UNUSED, 
				int flags ATTRIBUTE_UNUSED, 
				bool *no_add_attrs)
{
  if (TREE_CODE (*node) != FUNCTION_TYPE
      && TREE_CODE (*node) != FIELD_DECL
      && TREE_CODE (*node) != TYPE_DECL)
    {
      warning (OPT_Wattributes, "`%s' attribute only applies to functions",
	       IDENTIFIER_POINTER (name));
      *no_add_attrs = true;
    }

  if ((strcmp (IDENTIFIER_POINTER (name), "longcall") == 0
       && lookup_attribute ("shortcall", TYPE_ATTRIBUTES (*node)))
      || (strcmp (IDENTIFIER_POINTER (name), "shortcall") == 0
	  && lookup_attribute ("longcall", TYPE_ATTRIBUTES (*node))))
    {
      warning (OPT_Wattributes,
	       "can't apply both longcall and shortcall attributes to the same function");
      *no_add_attrs = true;
    }

  return NULL_TREE;
}

/* Table of valid machine attributes.  */
const struct attribute_spec bfin_attribute_table[] =
{
  /* { name, min_len, max_len, decl_req, type_req, fn_type_req, handler } */
  { "interrupt_handler", 0, 0, false, true,  true, handle_int_attribute },
  { "exception_handler", 0, 0, false, true,  true, handle_int_attribute },
  { "nmi_handler", 0, 0, false, true,  true, handle_int_attribute },
  { "nesting", 0, 0, false, true,  true, NULL },
  { "kspisusp", 0, 0, false, true,  true, NULL },
  { "saveall", 0, 0, false, true,  true, NULL },
  { "longcall",  0, 0, false, true,  true,  bfin_handle_longcall_attribute },
  { "shortcall", 0, 0, false, true,  true,  bfin_handle_longcall_attribute },
  { NULL, 0, 0, false, false, false, NULL }
};

/* Implementation of TARGET_ASM_INTEGER.  When using FD-PIC, we need to
   tell the assembler to generate pointers to function descriptors in
   some cases.  */

static bool
bfin_assemble_integer (rtx value, unsigned int size, int aligned_p)
{
  if (TARGET_FDPIC && size == UNITS_PER_WORD)
    {
      if (GET_CODE (value) == SYMBOL_REF
	  && SYMBOL_REF_FUNCTION_P (value))
	{
	  fputs ("\t.picptr\tfuncdesc(", asm_out_file);
	  output_addr_const (asm_out_file, value);
	  fputs (")\n", asm_out_file);
	  return true;
	}
      if (!aligned_p)
	{
	  /* We've set the unaligned SI op to NULL, so we always have to
	     handle the unaligned case here.  */
	  assemble_integer_with_op ("\t.4byte\t", value);
	  return true;
	}
    }
  return default_assemble_integer (value, size, aligned_p);
}

/* Output the assembler code for a thunk function.  THUNK_DECL is the
   declaration for the thunk function itself, FUNCTION is the decl for
   the target function.  DELTA is an immediate constant offset to be
   added to THIS.  If VCALL_OFFSET is nonzero, the word at
   *(*this + vcall_offset) should be added to THIS.  */

static void
bfin_output_mi_thunk (FILE *file ATTRIBUTE_UNUSED,
		      tree thunk ATTRIBUTE_UNUSED, HOST_WIDE_INT delta,
		      HOST_WIDE_INT vcall_offset, tree function)
{
  rtx xops[3];
  /* The this parameter is passed as the first argument.  */
  rtx this = gen_rtx_REG (Pmode, REG_R0);

  /* Adjust the this parameter by a fixed constant.  */
  if (delta)
    {
      xops[1] = this;
      if (delta >= -64 && delta <= 63)
	{
	  xops[0] = GEN_INT (delta);
	  output_asm_insn ("%1 += %0;", xops);
	}
      else if (delta >= -128 && delta < -64)
	{
	  xops[0] = GEN_INT (delta + 64);
	  output_asm_insn ("%1 += -64; %1 += %0;", xops);
	}
      else if (delta > 63 && delta <= 126)
	{
	  xops[0] = GEN_INT (delta - 63);
	  output_asm_insn ("%1 += 63; %1 += %0;", xops);
	}
      else
	{
	  xops[0] = GEN_INT (delta);
	  output_asm_insn ("r3.l = %h0; r3.h = %d0; %1 = %1 + r3;", xops);
	}
    }

  /* Adjust the this parameter by a value stored in the vtable.  */
  if (vcall_offset)
    {
      rtx p2tmp = gen_rtx_REG (Pmode, REG_P2);
      rtx tmp = gen_rtx_REG (Pmode, REG_R2);

      xops[1] = tmp;
      xops[2] = p2tmp;
      output_asm_insn ("%2 = r0; %2 = [%2];", xops);

      /* Adjust the this parameter.  */
      xops[0] = gen_rtx_MEM (Pmode, plus_constant (p2tmp, vcall_offset));
      if (!memory_operand (xops[0], Pmode))
	{
	  rtx tmp2 = gen_rtx_REG (Pmode, REG_P1);
	  xops[0] = GEN_INT (vcall_offset);
	  xops[1] = tmp2;
	  output_asm_insn ("%h1 = %h0; %d1 = %d0; %2 = %2 + %1", xops);
	  xops[0] = gen_rtx_MEM (Pmode, p2tmp);
	}
      xops[2] = this;
      output_asm_insn ("%1 = %0; %2 = %2 + %1;", xops);
    }

  xops[0] = XEXP (DECL_RTL (function), 0);
  if (1 || !flag_pic || (*targetm.binds_local_p) (function))
    output_asm_insn ("jump.l\t%P0", xops);
}

/* Codes for all the Blackfin builtins.  */
enum bfin_builtins
{
  BFIN_BUILTIN_CSYNC,
  BFIN_BUILTIN_SSYNC,
  BFIN_BUILTIN_COMPOSE_2X16,
  BFIN_BUILTIN_EXTRACTLO,
  BFIN_BUILTIN_EXTRACTHI,

  BFIN_BUILTIN_SSADD_2X16,
  BFIN_BUILTIN_SSSUB_2X16,
  BFIN_BUILTIN_SSADDSUB_2X16,
  BFIN_BUILTIN_SSSUBADD_2X16,
  BFIN_BUILTIN_MULT_2X16,
  BFIN_BUILTIN_MULTR_2X16,
  BFIN_BUILTIN_NEG_2X16,
  BFIN_BUILTIN_ABS_2X16,
  BFIN_BUILTIN_MIN_2X16,
  BFIN_BUILTIN_MAX_2X16,

  BFIN_BUILTIN_SSADD_1X16,
  BFIN_BUILTIN_SSSUB_1X16,
  BFIN_BUILTIN_MULT_1X16,
  BFIN_BUILTIN_MULTR_1X16,
  BFIN_BUILTIN_NORM_1X16,
  BFIN_BUILTIN_NEG_1X16,
  BFIN_BUILTIN_ABS_1X16,
  BFIN_BUILTIN_MIN_1X16,
  BFIN_BUILTIN_MAX_1X16,

  BFIN_BUILTIN_DIFFHL_2X16,
  BFIN_BUILTIN_DIFFLH_2X16,

  BFIN_BUILTIN_SSADD_1X32,
  BFIN_BUILTIN_SSSUB_1X32,
  BFIN_BUILTIN_NORM_1X32,
  BFIN_BUILTIN_NEG_1X32,
  BFIN_BUILTIN_MIN_1X32,
  BFIN_BUILTIN_MAX_1X32,
  BFIN_BUILTIN_MULT_1X32,

  BFIN_BUILTIN_MULHISILL,
  BFIN_BUILTIN_MULHISILH,
  BFIN_BUILTIN_MULHISIHL,
  BFIN_BUILTIN_MULHISIHH,

  BFIN_BUILTIN_LSHIFT_1X16,
  BFIN_BUILTIN_LSHIFT_2X16,
  BFIN_BUILTIN_SSASHIFT_1X16,
  BFIN_BUILTIN_SSASHIFT_2X16,

  BFIN_BUILTIN_CPLX_MUL_16,
  BFIN_BUILTIN_CPLX_MAC_16,
  BFIN_BUILTIN_CPLX_MSU_16,

  BFIN_BUILTIN_MAX
};

#define def_builtin(NAME, TYPE, CODE)					\
do {									\
  lang_hooks.builtin_function ((NAME), (TYPE), (CODE), BUILT_IN_MD,	\
			       NULL, NULL_TREE);			\
} while (0)

/* Set up all builtin functions for this target.  */
static void
bfin_init_builtins (void)
{
  tree V2HI_type_node = build_vector_type_for_mode (intHI_type_node, V2HImode);
  tree void_ftype_void
    = build_function_type (void_type_node, void_list_node);
  tree short_ftype_short
    = build_function_type_list (short_integer_type_node, short_integer_type_node,
				NULL_TREE);
  tree short_ftype_int_int
    = build_function_type_list (short_integer_type_node, integer_type_node,
				integer_type_node, NULL_TREE);
  tree int_ftype_int_int
    = build_function_type_list (integer_type_node, integer_type_node,
				integer_type_node, NULL_TREE);
  tree int_ftype_int
    = build_function_type_list (integer_type_node, integer_type_node,
				NULL_TREE);
  tree short_ftype_int
    = build_function_type_list (short_integer_type_node, integer_type_node,
				NULL_TREE);
  tree int_ftype_v2hi_v2hi
    = build_function_type_list (integer_type_node, V2HI_type_node,
				V2HI_type_node, NULL_TREE);
  tree v2hi_ftype_v2hi_v2hi
    = build_function_type_list (V2HI_type_node, V2HI_type_node,
				V2HI_type_node, NULL_TREE);
  tree v2hi_ftype_v2hi_v2hi_v2hi
    = build_function_type_list (V2HI_type_node, V2HI_type_node,
				V2HI_type_node, V2HI_type_node, NULL_TREE);
  tree v2hi_ftype_int_int
    = build_function_type_list (V2HI_type_node, integer_type_node,
				integer_type_node, NULL_TREE);
  tree v2hi_ftype_v2hi_int
    = build_function_type_list (V2HI_type_node, V2HI_type_node,
				integer_type_node, NULL_TREE);
  tree int_ftype_short_short
    = build_function_type_list (integer_type_node, short_integer_type_node,
				short_integer_type_node, NULL_TREE);
  tree v2hi_ftype_v2hi
    = build_function_type_list (V2HI_type_node, V2HI_type_node, NULL_TREE);
  tree short_ftype_v2hi
    = build_function_type_list (short_integer_type_node, V2HI_type_node,
				NULL_TREE);

  /* Add the remaining MMX insns with somewhat more complicated types.  */
  def_builtin ("__builtin_bfin_csync", void_ftype_void, BFIN_BUILTIN_CSYNC);
  def_builtin ("__builtin_bfin_ssync", void_ftype_void, BFIN_BUILTIN_SSYNC);

  def_builtin ("__builtin_bfin_compose_2x16", v2hi_ftype_int_int,
	       BFIN_BUILTIN_COMPOSE_2X16);
  def_builtin ("__builtin_bfin_extract_hi", short_ftype_v2hi,
	       BFIN_BUILTIN_EXTRACTHI);
  def_builtin ("__builtin_bfin_extract_lo", short_ftype_v2hi,
	       BFIN_BUILTIN_EXTRACTLO);

  def_builtin ("__builtin_bfin_min_fr2x16", v2hi_ftype_v2hi_v2hi,
	       BFIN_BUILTIN_MIN_2X16);
  def_builtin ("__builtin_bfin_max_fr2x16", v2hi_ftype_v2hi_v2hi,
	       BFIN_BUILTIN_MAX_2X16);

  def_builtin ("__builtin_bfin_add_fr2x16", v2hi_ftype_v2hi_v2hi,
	       BFIN_BUILTIN_SSADD_2X16);
  def_builtin ("__builtin_bfin_sub_fr2x16", v2hi_ftype_v2hi_v2hi,
	       BFIN_BUILTIN_SSSUB_2X16);
  def_builtin ("__builtin_bfin_dspaddsubsat", v2hi_ftype_v2hi_v2hi,
	       BFIN_BUILTIN_SSADDSUB_2X16);
  def_builtin ("__builtin_bfin_dspsubaddsat", v2hi_ftype_v2hi_v2hi,
	       BFIN_BUILTIN_SSSUBADD_2X16);
  def_builtin ("__builtin_bfin_mult_fr2x16", v2hi_ftype_v2hi_v2hi,
	       BFIN_BUILTIN_MULT_2X16);
  def_builtin ("__builtin_bfin_multr_fr2x16", v2hi_ftype_v2hi_v2hi,
	       BFIN_BUILTIN_MULTR_2X16);
  def_builtin ("__builtin_bfin_negate_fr2x16", v2hi_ftype_v2hi,
	       BFIN_BUILTIN_NEG_2X16);
  def_builtin ("__builtin_bfin_abs_fr2x16", v2hi_ftype_v2hi,
	       BFIN_BUILTIN_ABS_2X16);

  def_builtin ("__builtin_bfin_add_fr1x16", short_ftype_int_int,
	       BFIN_BUILTIN_SSADD_1X16);
  def_builtin ("__builtin_bfin_sub_fr1x16", short_ftype_int_int,
	       BFIN_BUILTIN_SSSUB_1X16);
  def_builtin ("__builtin_bfin_mult_fr1x16", short_ftype_int_int,
	       BFIN_BUILTIN_MULT_1X16);
  def_builtin ("__builtin_bfin_multr_fr1x16", short_ftype_int_int,
	       BFIN_BUILTIN_MULTR_1X16);
  def_builtin ("__builtin_bfin_negate_fr1x16", short_ftype_short,
	       BFIN_BUILTIN_NEG_1X16);
  def_builtin ("__builtin_bfin_abs_fr1x16", short_ftype_short,
	       BFIN_BUILTIN_ABS_1X16);
  def_builtin ("__builtin_bfin_norm_fr1x16", short_ftype_int,
	       BFIN_BUILTIN_NORM_1X16);

  def_builtin ("__builtin_bfin_diff_hl_fr2x16", short_ftype_v2hi,
	       BFIN_BUILTIN_DIFFHL_2X16);
  def_builtin ("__builtin_bfin_diff_lh_fr2x16", short_ftype_v2hi,
	       BFIN_BUILTIN_DIFFLH_2X16);

  def_builtin ("__builtin_bfin_mulhisill", int_ftype_v2hi_v2hi,
	       BFIN_BUILTIN_MULHISILL);
  def_builtin ("__builtin_bfin_mulhisihl", int_ftype_v2hi_v2hi,
	       BFIN_BUILTIN_MULHISIHL);
  def_builtin ("__builtin_bfin_mulhisilh", int_ftype_v2hi_v2hi,
	       BFIN_BUILTIN_MULHISILH);
  def_builtin ("__builtin_bfin_mulhisihh", int_ftype_v2hi_v2hi,
	       BFIN_BUILTIN_MULHISIHH);

  def_builtin ("__builtin_bfin_add_fr1x32", int_ftype_int_int,
	       BFIN_BUILTIN_SSADD_1X32);
  def_builtin ("__builtin_bfin_sub_fr1x32", int_ftype_int_int,
	       BFIN_BUILTIN_SSSUB_1X32);
  def_builtin ("__builtin_bfin_negate_fr1x32", int_ftype_int,
	       BFIN_BUILTIN_NEG_1X32);
  def_builtin ("__builtin_bfin_norm_fr1x32", short_ftype_int,
	       BFIN_BUILTIN_NORM_1X32);
  def_builtin ("__builtin_bfin_mult_fr1x32", int_ftype_short_short,
	       BFIN_BUILTIN_MULT_1X32);

  /* Shifts.  */
  def_builtin ("__builtin_bfin_shl_fr1x16", short_ftype_int_int,
	       BFIN_BUILTIN_SSASHIFT_1X16);
  def_builtin ("__builtin_bfin_shl_fr2x16", v2hi_ftype_v2hi_int,
	       BFIN_BUILTIN_SSASHIFT_2X16);
  def_builtin ("__builtin_bfin_lshl_fr1x16", short_ftype_int_int,
	       BFIN_BUILTIN_LSHIFT_1X16);
  def_builtin ("__builtin_bfin_lshl_fr2x16", v2hi_ftype_v2hi_int,
	       BFIN_BUILTIN_LSHIFT_2X16);

  /* Complex numbers.  */
  def_builtin ("__builtin_bfin_cmplx_mul", v2hi_ftype_v2hi_v2hi,
	       BFIN_BUILTIN_CPLX_MUL_16);
  def_builtin ("__builtin_bfin_cmplx_mac", v2hi_ftype_v2hi_v2hi_v2hi,
	       BFIN_BUILTIN_CPLX_MAC_16);
  def_builtin ("__builtin_bfin_cmplx_msu", v2hi_ftype_v2hi_v2hi_v2hi,
	       BFIN_BUILTIN_CPLX_MSU_16);
}


struct builtin_description
{
  const enum insn_code icode;
  const char *const name;
  const enum bfin_builtins code;
  int macflag;
};

static const struct builtin_description bdesc_2arg[] =
{
  { CODE_FOR_composev2hi, "__builtin_bfin_compose_2x16", BFIN_BUILTIN_COMPOSE_2X16, -1 },

  { CODE_FOR_ssashiftv2hi3, "__builtin_bfin_shl_fr2x16", BFIN_BUILTIN_SSASHIFT_2X16, -1 },
  { CODE_FOR_ssashifthi3, "__builtin_bfin_shl_fr1x16", BFIN_BUILTIN_SSASHIFT_1X16, -1 },
  { CODE_FOR_lshiftv2hi3, "__builtin_bfin_lshl_fr2x16", BFIN_BUILTIN_LSHIFT_2X16, -1 },
  { CODE_FOR_lshifthi3, "__builtin_bfin_lshl_fr1x16", BFIN_BUILTIN_LSHIFT_1X16, -1 },

  { CODE_FOR_sminhi3, "__builtin_bfin_min_fr1x16", BFIN_BUILTIN_MIN_1X16, -1 },
  { CODE_FOR_smaxhi3, "__builtin_bfin_max_fr1x16", BFIN_BUILTIN_MAX_1X16, -1 },
  { CODE_FOR_ssaddhi3, "__builtin_bfin_add_fr1x16", BFIN_BUILTIN_SSADD_1X16, -1 },
  { CODE_FOR_sssubhi3, "__builtin_bfin_sub_fr1x16", BFIN_BUILTIN_SSSUB_1X16, -1 },

  { CODE_FOR_sminsi3, "__builtin_bfin_min_fr1x32", BFIN_BUILTIN_MIN_1X32, -1 },
  { CODE_FOR_smaxsi3, "__builtin_bfin_max_fr1x32", BFIN_BUILTIN_MAX_1X32, -1 },
  { CODE_FOR_ssaddsi3, "__builtin_bfin_add_fr1x32", BFIN_BUILTIN_SSADD_1X32, -1 },
  { CODE_FOR_sssubsi3, "__builtin_bfin_sub_fr1x32", BFIN_BUILTIN_SSSUB_1X32, -1 },

  { CODE_FOR_sminv2hi3, "__builtin_bfin_min_fr2x16", BFIN_BUILTIN_MIN_2X16, -1 },
  { CODE_FOR_smaxv2hi3, "__builtin_bfin_max_fr2x16", BFIN_BUILTIN_MAX_2X16, -1 },
  { CODE_FOR_ssaddv2hi3, "__builtin_bfin_add_fr2x16", BFIN_BUILTIN_SSADD_2X16, -1 },
  { CODE_FOR_sssubv2hi3, "__builtin_bfin_sub_fr2x16", BFIN_BUILTIN_SSSUB_2X16, -1 },
  { CODE_FOR_ssaddsubv2hi3, "__builtin_bfin_dspaddsubsat", BFIN_BUILTIN_SSADDSUB_2X16, -1 },
  { CODE_FOR_sssubaddv2hi3, "__builtin_bfin_dspsubaddsat", BFIN_BUILTIN_SSSUBADD_2X16, -1 },

  { CODE_FOR_flag_mulhisi, "__builtin_bfin_mult_fr1x32", BFIN_BUILTIN_MULT_1X32, MACFLAG_NONE },
  { CODE_FOR_flag_mulhi, "__builtin_bfin_mult_fr1x16", BFIN_BUILTIN_MULT_1X16, MACFLAG_T },
  { CODE_FOR_flag_mulhi, "__builtin_bfin_multr_fr1x16", BFIN_BUILTIN_MULTR_1X16, MACFLAG_NONE },
  { CODE_FOR_flag_mulv2hi, "__builtin_bfin_mult_fr2x16", BFIN_BUILTIN_MULT_2X16, MACFLAG_T },
  { CODE_FOR_flag_mulv2hi, "__builtin_bfin_multr_fr2x16", BFIN_BUILTIN_MULTR_2X16, MACFLAG_NONE }
};

static const struct builtin_description bdesc_1arg[] =
{
  { CODE_FOR_signbitshi2, "__builtin_bfin_norm_fr1x16", BFIN_BUILTIN_NORM_1X16, 0 },
  { CODE_FOR_ssneghi2, "__builtin_bfin_negate_fr1x16", BFIN_BUILTIN_NEG_1X16, 0 },
  { CODE_FOR_abshi2, "__builtin_bfin_abs_fr1x16", BFIN_BUILTIN_ABS_1X16, 0 },

  { CODE_FOR_signbitssi2, "__builtin_bfin_norm_fr1x32", BFIN_BUILTIN_NORM_1X32, 0 },
  { CODE_FOR_ssnegsi2, "__builtin_bfin_negate_fr1x32", BFIN_BUILTIN_NEG_1X32, 0 },

  { CODE_FOR_movv2hi_hi_low, "__builtin_bfin_extract_lo", BFIN_BUILTIN_EXTRACTLO, 0 },
  { CODE_FOR_movv2hi_hi_high, "__builtin_bfin_extract_hi", BFIN_BUILTIN_EXTRACTHI, 0 },
  { CODE_FOR_ssnegv2hi2, "__builtin_bfin_negate_fr2x16", BFIN_BUILTIN_NEG_2X16, 0 },
  { CODE_FOR_absv2hi2, "__builtin_bfin_abs_fr2x16", BFIN_BUILTIN_ABS_2X16, 0 }
};

/* Errors in the source file can cause expand_expr to return const0_rtx
   where we expect a vector.  To avoid crashing, use one of the vector
   clear instructions.  */
static rtx
safe_vector_operand (rtx x, enum machine_mode mode)
{
  if (x != const0_rtx)
    return x;
  x = gen_reg_rtx (SImode);

  emit_insn (gen_movsi (x, CONST0_RTX (SImode)));
  return gen_lowpart (mode, x);
}

/* Subroutine of bfin_expand_builtin to take care of binop insns.  MACFLAG is -1
   if this is a normal binary op, or one of the MACFLAG_xxx constants.  */

static rtx
bfin_expand_binop_builtin (enum insn_code icode, tree arglist, rtx target,
			   int macflag)
{
  rtx pat;
  tree arg0 = TREE_VALUE (arglist);
  tree arg1 = TREE_VALUE (TREE_CHAIN (arglist));
  rtx op0 = expand_expr (arg0, NULL_RTX, VOIDmode, 0);
  rtx op1 = expand_expr (arg1, NULL_RTX, VOIDmode, 0);
  enum machine_mode op0mode = GET_MODE (op0);
  enum machine_mode op1mode = GET_MODE (op1);
  enum machine_mode tmode = insn_data[icode].operand[0].mode;
  enum machine_mode mode0 = insn_data[icode].operand[1].mode;
  enum machine_mode mode1 = insn_data[icode].operand[2].mode;

  if (VECTOR_MODE_P (mode0))
    op0 = safe_vector_operand (op0, mode0);
  if (VECTOR_MODE_P (mode1))
    op1 = safe_vector_operand (op1, mode1);

  if (! target
      || GET_MODE (target) != tmode
      || ! (*insn_data[icode].operand[0].predicate) (target, tmode))
    target = gen_reg_rtx (tmode);

  if ((op0mode == SImode || op0mode == VOIDmode) && mode0 == HImode)
    {
      op0mode = HImode;
      op0 = gen_lowpart (HImode, op0);
    }
  if ((op1mode == SImode || op1mode == VOIDmode) && mode1 == HImode)
    {
      op1mode = HImode;
      op1 = gen_lowpart (HImode, op1);
    }
  /* In case the insn wants input operands in modes different from
     the result, abort.  */
  gcc_assert ((op0mode == mode0 || op0mode == VOIDmode)
	      && (op1mode == mode1 || op1mode == VOIDmode));

  if (! (*insn_data[icode].operand[1].predicate) (op0, mode0))
    op0 = copy_to_mode_reg (mode0, op0);
  if (! (*insn_data[icode].operand[2].predicate) (op1, mode1))
    op1 = copy_to_mode_reg (mode1, op1);

  if (macflag == -1)
    pat = GEN_FCN (icode) (target, op0, op1);
  else
    pat = GEN_FCN (icode) (target, op0, op1, GEN_INT (macflag));
  if (! pat)
    return 0;

  emit_insn (pat);
  return target;
}

/* Subroutine of bfin_expand_builtin to take care of unop insns.  */

static rtx
bfin_expand_unop_builtin (enum insn_code icode, tree arglist,
			  rtx target)
{
  rtx pat;
  tree arg0 = TREE_VALUE (arglist);
  rtx op0 = expand_expr (arg0, NULL_RTX, VOIDmode, 0);
  enum machine_mode op0mode = GET_MODE (op0);
  enum machine_mode tmode = insn_data[icode].operand[0].mode;
  enum machine_mode mode0 = insn_data[icode].operand[1].mode;

  if (! target
      || GET_MODE (target) != tmode
      || ! (*insn_data[icode].operand[0].predicate) (target, tmode))
    target = gen_reg_rtx (tmode);

  if (VECTOR_MODE_P (mode0))
    op0 = safe_vector_operand (op0, mode0);

  if (op0mode == SImode && mode0 == HImode)
    {
      op0mode = HImode;
      op0 = gen_lowpart (HImode, op0);
    }
  gcc_assert (op0mode == mode0 || op0mode == VOIDmode);

  if (! (*insn_data[icode].operand[1].predicate) (op0, mode0))
    op0 = copy_to_mode_reg (mode0, op0);

  pat = GEN_FCN (icode) (target, op0);
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
bfin_expand_builtin (tree exp, rtx target ATTRIBUTE_UNUSED,
		     rtx subtarget ATTRIBUTE_UNUSED,
		     enum machine_mode mode ATTRIBUTE_UNUSED,
		     int ignore ATTRIBUTE_UNUSED)
{
  size_t i;
  enum insn_code icode;
  const struct builtin_description *d;
  tree fndecl = TREE_OPERAND (TREE_OPERAND (exp, 0), 0);
  tree arglist = TREE_OPERAND (exp, 1);
  unsigned int fcode = DECL_FUNCTION_CODE (fndecl);
  tree arg0, arg1, arg2;
  rtx op0, op1, op2, accvec, pat, tmp1, tmp2;
  enum machine_mode tmode, mode0;

  switch (fcode)
    {
    case BFIN_BUILTIN_CSYNC:
      emit_insn (gen_csync ());
      return 0;
    case BFIN_BUILTIN_SSYNC:
      emit_insn (gen_ssync ());
      return 0;

    case BFIN_BUILTIN_DIFFHL_2X16:
    case BFIN_BUILTIN_DIFFLH_2X16:
      arg0 = TREE_VALUE (arglist);
      op0 = expand_expr (arg0, NULL_RTX, VOIDmode, 0);
      icode = (fcode == BFIN_BUILTIN_DIFFHL_2X16
	       ? CODE_FOR_subhilov2hi3 : CODE_FOR_sublohiv2hi3);
      tmode = insn_data[icode].operand[0].mode;
      mode0 = insn_data[icode].operand[1].mode;

      if (! target
	  || GET_MODE (target) != tmode
	  || ! (*insn_data[icode].operand[0].predicate) (target, tmode))
	target = gen_reg_rtx (tmode);

      if (VECTOR_MODE_P (mode0))
	op0 = safe_vector_operand (op0, mode0);

      if (! (*insn_data[icode].operand[1].predicate) (op0, mode0))
	op0 = copy_to_mode_reg (mode0, op0);

      pat = GEN_FCN (icode) (target, op0, op0);
      if (! pat)
	return 0;
      emit_insn (pat);
      return target;

    case BFIN_BUILTIN_CPLX_MUL_16:
      arg0 = TREE_VALUE (arglist);
      arg1 = TREE_VALUE (TREE_CHAIN (arglist));
      op0 = expand_expr (arg0, NULL_RTX, VOIDmode, 0);
      op1 = expand_expr (arg1, NULL_RTX, VOIDmode, 0);
      accvec = gen_reg_rtx (V2PDImode);

      if (! target
	  || GET_MODE (target) != V2HImode
	  || ! (*insn_data[icode].operand[0].predicate) (target, V2HImode))
	target = gen_reg_rtx (tmode);
      if (! register_operand (op0, GET_MODE (op0)))
	op0 = copy_to_mode_reg (GET_MODE (op0), op0);
      if (! register_operand (op1, GET_MODE (op1)))
	op1 = copy_to_mode_reg (GET_MODE (op1), op1);

      emit_insn (gen_flag_macinit1v2hi_parts (accvec, op0, op1, const0_rtx,
					      const0_rtx, const0_rtx,
					      const1_rtx, GEN_INT (MACFLAG_NONE)));
      emit_insn (gen_flag_macv2hi_parts (target, op0, op1, const1_rtx,
					 const1_rtx, const1_rtx,
					 const0_rtx, accvec, const1_rtx, const0_rtx,
					 GEN_INT (MACFLAG_NONE), accvec));

      return target;

    case BFIN_BUILTIN_CPLX_MAC_16:
    case BFIN_BUILTIN_CPLX_MSU_16:
      arg0 = TREE_VALUE (arglist);
      arg1 = TREE_VALUE (TREE_CHAIN (arglist));
      arg2 = TREE_VALUE (TREE_CHAIN (TREE_CHAIN (arglist)));
      op0 = expand_expr (arg0, NULL_RTX, VOIDmode, 0);
      op1 = expand_expr (arg1, NULL_RTX, VOIDmode, 0);
      op2 = expand_expr (arg2, NULL_RTX, VOIDmode, 0);
      accvec = gen_reg_rtx (V2PDImode);

      if (! target
	  || GET_MODE (target) != V2HImode
	  || ! (*insn_data[icode].operand[0].predicate) (target, V2HImode))
	target = gen_reg_rtx (tmode);
      if (! register_operand (op0, GET_MODE (op0)))
	op0 = copy_to_mode_reg (GET_MODE (op0), op0);
      if (! register_operand (op1, GET_MODE (op1)))
	op1 = copy_to_mode_reg (GET_MODE (op1), op1);

      tmp1 = gen_reg_rtx (SImode);
      tmp2 = gen_reg_rtx (SImode);
      emit_insn (gen_ashlsi3 (tmp1, gen_lowpart (SImode, op2), GEN_INT (16)));
      emit_move_insn (tmp2, gen_lowpart (SImode, op2));
      emit_insn (gen_movstricthi_1 (gen_lowpart (HImode, tmp2), const0_rtx));
      emit_insn (gen_load_accumulator_pair (accvec, tmp1, tmp2));
      emit_insn (gen_flag_macv2hi_parts_acconly (accvec, op0, op1, const0_rtx,
						 const0_rtx, const0_rtx,
						 const1_rtx, accvec, const0_rtx,
						 const0_rtx,
						 GEN_INT (MACFLAG_W32)));
      tmp1 = (fcode == BFIN_BUILTIN_CPLX_MAC_16 ? const1_rtx : const0_rtx);
      tmp2 = (fcode == BFIN_BUILTIN_CPLX_MAC_16 ? const0_rtx : const1_rtx);
      emit_insn (gen_flag_macv2hi_parts (target, op0, op1, const1_rtx,
					 const1_rtx, const1_rtx,
					 const0_rtx, accvec, tmp1, tmp2,
					 GEN_INT (MACFLAG_NONE), accvec));

      return target;

    default:
      break;
    }

  for (i = 0, d = bdesc_2arg; i < ARRAY_SIZE (bdesc_2arg); i++, d++)
    if (d->code == fcode)
      return bfin_expand_binop_builtin (d->icode, arglist, target,
					d->macflag);

  for (i = 0, d = bdesc_1arg; i < ARRAY_SIZE (bdesc_1arg); i++, d++)
    if (d->code == fcode)
      return bfin_expand_unop_builtin (d->icode, arglist, target);

  gcc_unreachable ();
}

#undef TARGET_INIT_BUILTINS
#define TARGET_INIT_BUILTINS bfin_init_builtins

#undef TARGET_EXPAND_BUILTIN
#define TARGET_EXPAND_BUILTIN bfin_expand_builtin

#undef TARGET_ASM_GLOBALIZE_LABEL
#define TARGET_ASM_GLOBALIZE_LABEL bfin_globalize_label 

#undef TARGET_ASM_FILE_START
#define TARGET_ASM_FILE_START output_file_start

#undef TARGET_ATTRIBUTE_TABLE
#define TARGET_ATTRIBUTE_TABLE bfin_attribute_table

#undef TARGET_COMP_TYPE_ATTRIBUTES
#define TARGET_COMP_TYPE_ATTRIBUTES bfin_comp_type_attributes

#undef TARGET_RTX_COSTS
#define TARGET_RTX_COSTS bfin_rtx_costs

#undef  TARGET_ADDRESS_COST
#define TARGET_ADDRESS_COST bfin_address_cost

#undef TARGET_ASM_INTERNAL_LABEL
#define TARGET_ASM_INTERNAL_LABEL bfin_internal_label

#undef  TARGET_ASM_INTEGER
#define TARGET_ASM_INTEGER bfin_assemble_integer

#undef TARGET_MACHINE_DEPENDENT_REORG
#define TARGET_MACHINE_DEPENDENT_REORG bfin_reorg

#undef TARGET_FUNCTION_OK_FOR_SIBCALL
#define TARGET_FUNCTION_OK_FOR_SIBCALL bfin_function_ok_for_sibcall

#undef TARGET_ASM_OUTPUT_MI_THUNK
#define TARGET_ASM_OUTPUT_MI_THUNK bfin_output_mi_thunk
#undef TARGET_ASM_CAN_OUTPUT_MI_THUNK
#define TARGET_ASM_CAN_OUTPUT_MI_THUNK hook_bool_tree_hwi_hwi_tree_true

#undef TARGET_SCHED_ADJUST_COST
#define TARGET_SCHED_ADJUST_COST bfin_adjust_cost

#undef TARGET_PROMOTE_PROTOTYPES
#define TARGET_PROMOTE_PROTOTYPES hook_bool_tree_true
#undef TARGET_PROMOTE_FUNCTION_ARGS
#define TARGET_PROMOTE_FUNCTION_ARGS hook_bool_tree_true
#undef TARGET_PROMOTE_FUNCTION_RETURN
#define TARGET_PROMOTE_FUNCTION_RETURN hook_bool_tree_true

#undef TARGET_ARG_PARTIAL_BYTES
#define TARGET_ARG_PARTIAL_BYTES bfin_arg_partial_bytes

#undef TARGET_PASS_BY_REFERENCE
#define TARGET_PASS_BY_REFERENCE bfin_pass_by_reference

#undef TARGET_SETUP_INCOMING_VARARGS
#define TARGET_SETUP_INCOMING_VARARGS setup_incoming_varargs

#undef TARGET_STRUCT_VALUE_RTX
#define TARGET_STRUCT_VALUE_RTX bfin_struct_value_rtx

#undef TARGET_VECTOR_MODE_SUPPORTED_P
#define TARGET_VECTOR_MODE_SUPPORTED_P bfin_vector_mode_supported_p

#undef TARGET_HANDLE_OPTION
#define TARGET_HANDLE_OPTION bfin_handle_option

#undef TARGET_DEFAULT_TARGET_FLAGS
#define TARGET_DEFAULT_TARGET_FLAGS TARGET_DEFAULT

#undef TARGET_SECONDARY_RELOAD
#define TARGET_SECONDARY_RELOAD bfin_secondary_reload

#undef TARGET_DELEGITIMIZE_ADDRESS
#define TARGET_DELEGITIMIZE_ADDRESS bfin_delegitimize_address

struct gcc_target targetm = TARGET_INITIALIZER;
