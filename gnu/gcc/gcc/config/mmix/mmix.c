/* Definitions of target machine for GNU compiler, for MMIX.
   Copyright (C) 2000, 2001, 2002, 2003, 2004, 2005
   Free Software Foundation, Inc.
   Contributed by Hans-Peter Nilsson (hp@bitrange.com)

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
#include "tm.h"
#include "rtl.h"
#include "regs.h"
#include "hard-reg-set.h"
#include "hashtab.h"
#include "insn-config.h"
#include "output.h"
#include "flags.h"
#include "tree.h"
#include "function.h"
#include "expr.h"
#include "toplev.h"
#include "recog.h"
#include "ggc.h"
#include "dwarf2.h"
#include "debug.h"
#include "tm_p.h"
#include "integrate.h"
#include "target.h"
#include "target-def.h"
#include "real.h"

/* First some local helper definitions.  */
#define MMIX_FIRST_GLOBAL_REGNUM 32

/* We'd need a current_function_has_landing_pad.  It's marked as such when
   a nonlocal_goto_receiver is expanded.  Not just a C++ thing, but
   mostly.  */
#define MMIX_CFUN_HAS_LANDING_PAD (cfun->machine->has_landing_pad != 0)

/* We have no means to tell DWARF 2 about the register stack, so we need
   to store the return address on the stack if an exception can get into
   this function.  FIXME: Narrow condition.  Before any whole-function
   analysis, regs_ever_live[] isn't initialized.  We know it's up-to-date
   after reload_completed; it may contain incorrect information some time
   before that.  Within a RTL sequence (after a call to start_sequence,
   such as in RTL expanders), leaf_function_p doesn't see all insns
   (perhaps any insn).  But regs_ever_live is up-to-date when
   leaf_function_p () isn't, so we "or" them together to get accurate
   information.  FIXME: Some tweak to leaf_function_p might be
   preferable.  */
#define MMIX_CFUN_NEEDS_SAVED_EH_RETURN_ADDRESS			\
 (flag_exceptions						\
  && ((reload_completed && regs_ever_live[MMIX_rJ_REGNUM])	\
      || !leaf_function_p ()))

#define IS_MMIX_EH_RETURN_DATA_REG(REGNO)	\
 (current_function_calls_eh_return		\
  && (EH_RETURN_DATA_REGNO (0) == REGNO		\
      || EH_RETURN_DATA_REGNO (1) == REGNO	\
      || EH_RETURN_DATA_REGNO (2) == REGNO	\
      || EH_RETURN_DATA_REGNO (3) == REGNO))

/* For the default ABI, we rename registers at output-time to fill the gap
   between the (statically partitioned) saved registers and call-clobbered
   registers.  In effect this makes unused call-saved registers to be used
   as call-clobbered registers.  The benefit comes from keeping the number
   of local registers (value of rL) low, since there's a cost of
   increasing rL and clearing unused (unset) registers with lower numbers.
   Don't translate while outputting the prologue.  */
#define MMIX_OUTPUT_REGNO(N)					\
 (TARGET_ABI_GNU 						\
  || (int) (N) < MMIX_RETURN_VALUE_REGNUM			\
  || (int) (N) > MMIX_LAST_STACK_REGISTER_REGNUM		\
  || cfun == NULL 						\
  || cfun->machine == NULL 					\
  || cfun->machine->in_prologue					\
  ? (N) : ((N) - MMIX_RETURN_VALUE_REGNUM			\
	   + cfun->machine->highest_saved_stack_register + 1))

/* The %d in "POP %d,0".  */
#define MMIX_POP_ARGUMENT()						\
 ((! TARGET_ABI_GNU							\
   && current_function_return_rtx != NULL				\
   && ! current_function_returns_struct)				\
  ? (GET_CODE (current_function_return_rtx) == PARALLEL			\
     ? GET_NUM_ELEM (XVEC (current_function_return_rtx, 0)) : 1)	\
  : 0)

/* The canonical saved comparison operands for non-cc0 machines, set in
   the compare expander.  */
rtx mmix_compare_op0;
rtx mmix_compare_op1;

/* Declarations of locals.  */

/* Intermediate for insn output.  */
static int mmix_output_destination_register;

static void mmix_output_shiftvalue_op_from_str
  (FILE *, const char *, HOST_WIDEST_INT);
static void mmix_output_shifted_value (FILE *, HOST_WIDEST_INT);
static void mmix_output_condition (FILE *, rtx, int);
static HOST_WIDEST_INT mmix_intval (rtx);
static void mmix_output_octa (FILE *, HOST_WIDEST_INT, int);
static bool mmix_assemble_integer (rtx, unsigned int, int);
static struct machine_function *mmix_init_machine_status (void);
static void mmix_encode_section_info (tree, rtx, int);
static const char *mmix_strip_name_encoding (const char *);
static void mmix_emit_sp_add (HOST_WIDE_INT offset);
static void mmix_target_asm_function_prologue (FILE *, HOST_WIDE_INT);
static void mmix_target_asm_function_end_prologue (FILE *);
static void mmix_target_asm_function_epilogue (FILE *, HOST_WIDE_INT);
static void mmix_reorg (void);
static void mmix_asm_output_mi_thunk
  (FILE *, tree, HOST_WIDE_INT, HOST_WIDE_INT, tree);
static void mmix_setup_incoming_varargs
  (CUMULATIVE_ARGS *, enum machine_mode, tree, int *, int);
static void mmix_file_start (void);
static void mmix_file_end (void);
static bool mmix_rtx_costs (rtx, int, int, int *);
static rtx mmix_struct_value_rtx (tree, int);
static bool mmix_pass_by_reference (const CUMULATIVE_ARGS *,
				    enum machine_mode, tree, bool);

/* Target structure macros.  Listed by node.  See `Using and Porting GCC'
   for a general description.  */

/* Node: Function Entry */

#undef TARGET_ASM_BYTE_OP
#define TARGET_ASM_BYTE_OP NULL
#undef TARGET_ASM_ALIGNED_HI_OP
#define TARGET_ASM_ALIGNED_HI_OP NULL
#undef TARGET_ASM_ALIGNED_SI_OP
#define TARGET_ASM_ALIGNED_SI_OP NULL
#undef TARGET_ASM_ALIGNED_DI_OP
#define TARGET_ASM_ALIGNED_DI_OP NULL
#undef TARGET_ASM_INTEGER
#define TARGET_ASM_INTEGER mmix_assemble_integer

#undef TARGET_ASM_FUNCTION_PROLOGUE
#define TARGET_ASM_FUNCTION_PROLOGUE mmix_target_asm_function_prologue

#undef TARGET_ASM_FUNCTION_END_PROLOGUE
#define TARGET_ASM_FUNCTION_END_PROLOGUE mmix_target_asm_function_end_prologue

#undef TARGET_ASM_FUNCTION_EPILOGUE
#define TARGET_ASM_FUNCTION_EPILOGUE mmix_target_asm_function_epilogue

#undef TARGET_ENCODE_SECTION_INFO
#define TARGET_ENCODE_SECTION_INFO  mmix_encode_section_info
#undef TARGET_STRIP_NAME_ENCODING
#define TARGET_STRIP_NAME_ENCODING  mmix_strip_name_encoding

#undef TARGET_ASM_OUTPUT_MI_THUNK
#define TARGET_ASM_OUTPUT_MI_THUNK mmix_asm_output_mi_thunk
#undef TARGET_ASM_CAN_OUTPUT_MI_THUNK
#define TARGET_ASM_CAN_OUTPUT_MI_THUNK default_can_output_mi_thunk_no_vcall
#undef TARGET_ASM_FILE_START
#define TARGET_ASM_FILE_START mmix_file_start
#undef TARGET_ASM_FILE_START_FILE_DIRECTIVE
#define TARGET_ASM_FILE_START_FILE_DIRECTIVE true
#undef TARGET_ASM_FILE_END
#define TARGET_ASM_FILE_END mmix_file_end

#undef TARGET_RTX_COSTS
#define TARGET_RTX_COSTS mmix_rtx_costs
#undef TARGET_ADDRESS_COST
#define TARGET_ADDRESS_COST hook_int_rtx_0

#undef TARGET_MACHINE_DEPENDENT_REORG
#define TARGET_MACHINE_DEPENDENT_REORG mmix_reorg

#undef TARGET_PROMOTE_FUNCTION_ARGS
#define TARGET_PROMOTE_FUNCTION_ARGS hook_bool_tree_true
#if 0
/* Apparently not doing TRT if int < register-size.  FIXME: Perhaps
   FUNCTION_VALUE and LIBCALL_VALUE needs tweaking as some ports say.  */
#undef TARGET_PROMOTE_FUNCTION_RETURN
#define TARGET_PROMOTE_FUNCTION_RETURN hook_bool_tree_true
#endif

#undef TARGET_STRUCT_VALUE_RTX
#define TARGET_STRUCT_VALUE_RTX mmix_struct_value_rtx
#undef TARGET_SETUP_INCOMING_VARARGS
#define TARGET_SETUP_INCOMING_VARARGS mmix_setup_incoming_varargs
#undef TARGET_PASS_BY_REFERENCE
#define TARGET_PASS_BY_REFERENCE mmix_pass_by_reference
#undef TARGET_CALLEE_COPIES
#define TARGET_CALLEE_COPIES hook_bool_CUMULATIVE_ARGS_mode_tree_bool_true
#undef TARGET_DEFAULT_TARGET_FLAGS
#define TARGET_DEFAULT_TARGET_FLAGS TARGET_DEFAULT

struct gcc_target targetm = TARGET_INITIALIZER;

/* Functions that are expansions for target macros.
   See Target Macros in `Using and Porting GCC'.  */

/* OVERRIDE_OPTIONS.  */

void
mmix_override_options (void)
{
  /* Should we err or should we warn?  Hmm.  At least we must neutralize
     it.  For example the wrong kind of case-tables will be generated with
     PIC; we use absolute address items for mmixal compatibility.  FIXME:
     They could be relative if we just elide them to after all pertinent
     labels.  */
  if (flag_pic)
    {
      warning (0, "-f%s not supported: ignored", (flag_pic > 1) ? "PIC" : "pic");
      flag_pic = 0;
    }
}

/* INIT_EXPANDERS.  */

void
mmix_init_expanders (void)
{
  init_machine_status = mmix_init_machine_status;
}

/* Set the per-function data.  */

static struct machine_function *
mmix_init_machine_status (void)
{
  return ggc_alloc_cleared (sizeof (struct machine_function));
}

/* DATA_ALIGNMENT.
   We have trouble getting the address of stuff that is located at other
   than 32-bit alignments (GETA requirements), so try to give everything
   at least 32-bit alignment.  */

int
mmix_data_alignment (tree type ATTRIBUTE_UNUSED, int basic_align)
{
  if (basic_align < 32)
    return 32;

  return basic_align;
}

/* CONSTANT_ALIGNMENT.  */

int
mmix_constant_alignment (tree constant ATTRIBUTE_UNUSED, int basic_align)
{
  if (basic_align < 32)
    return 32;

  return basic_align;
}

/* LOCAL_ALIGNMENT.  */

int
mmix_local_alignment (tree type ATTRIBUTE_UNUSED, int basic_align)
{
  if (basic_align < 32)
    return 32;

  return basic_align;
}

/* CONDITIONAL_REGISTER_USAGE.  */

void
mmix_conditional_register_usage (void)
{
  int i;

  if (TARGET_ABI_GNU)
    {
      static const int gnu_abi_reg_alloc_order[]
	= MMIX_GNU_ABI_REG_ALLOC_ORDER;

      for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
	reg_alloc_order[i] = gnu_abi_reg_alloc_order[i];

      /* Change the default from the mmixware ABI.  For the GNU ABI,
	 $15..$30 are call-saved just as $0..$14.  There must be one
	 call-clobbered local register for the "hole" that holds the
	 number of saved local registers saved by PUSHJ/PUSHGO during the
	 function call, receiving the return value at return.  So best is
	 to use the highest, $31.  It's already marked call-clobbered for
	 the mmixware ABI.  */
      for (i = 15; i <= 30; i++)
	call_used_regs[i] = 0;

      /* "Unfix" the parameter registers.  */
      for (i = MMIX_RESERVED_GNU_ARG_0_REGNUM;
	   i < MMIX_RESERVED_GNU_ARG_0_REGNUM + MMIX_MAX_ARGS_IN_REGS;
	   i++)
	fixed_regs[i] = 0;
    }

  /* Step over the ":" in special register names.  */
  if (! TARGET_TOPLEVEL_SYMBOLS)
    for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
      if (reg_names[i][0] == ':')
	reg_names[i]++;
}

/* LOCAL_REGNO.
   All registers that are part of the register stack and that will be
   saved are local.  */

int
mmix_local_regno (int regno)
{
  return regno <= MMIX_LAST_STACK_REGISTER_REGNUM && !call_used_regs[regno];
}

/* PREFERRED_RELOAD_CLASS.
   We need to extend the reload class of REMAINDER_REG and HIMULT_REG.  */

enum reg_class
mmix_preferred_reload_class (rtx x ATTRIBUTE_UNUSED, enum reg_class class)
{
  /* FIXME: Revisit.  */
  return GET_CODE (x) == MOD && GET_MODE (x) == DImode
    ? REMAINDER_REG : class;
}

/* PREFERRED_OUTPUT_RELOAD_CLASS.
   We need to extend the reload class of REMAINDER_REG and HIMULT_REG.  */

enum reg_class
mmix_preferred_output_reload_class (rtx x ATTRIBUTE_UNUSED,
				    enum reg_class class)
{
  /* FIXME: Revisit.  */
  return GET_CODE (x) == MOD && GET_MODE (x) == DImode
    ? REMAINDER_REG : class;
}

/* SECONDARY_RELOAD_CLASS.
   We need to reload regs of REMAINDER_REG and HIMULT_REG elsewhere.  */

enum reg_class
mmix_secondary_reload_class (enum reg_class class,
			     enum machine_mode mode ATTRIBUTE_UNUSED,
			     rtx x ATTRIBUTE_UNUSED,
			     int in_p ATTRIBUTE_UNUSED)
{
  if (class == REMAINDER_REG
      || class == HIMULT_REG
      || class == SYSTEM_REGS)
    return GENERAL_REGS;

  return NO_REGS;
}

/* CONST_OK_FOR_LETTER_P.  */

int
mmix_const_ok_for_letter_p (HOST_WIDE_INT value, int c)
{
  return
    (c == 'I' ? value >= 0 && value <= 255
     : c == 'J' ? value >= 0 && value <= 65535
     : c == 'K' ? value <= 0 && value >= -255
     : c == 'L' ? mmix_shiftable_wyde_value (value)
     : c == 'M' ? value == 0
     : c == 'N' ? mmix_shiftable_wyde_value (~value)
     : c == 'O' ? (value == 3 || value == 5 || value == 9
		   || value == 17)
     : 0);
}

/* CONST_DOUBLE_OK_FOR_LETTER_P.  */

int
mmix_const_double_ok_for_letter_p (rtx value, int c)
{
  return
    (c == 'G' ? value == CONST0_RTX (GET_MODE (value))
     : 0);
}

/* EXTRA_CONSTRAINT.
   We need this since our constants are not always expressible as
   CONST_INT:s, but rather often as CONST_DOUBLE:s.  */

int
mmix_extra_constraint (rtx x, int c, int strict)
{
  HOST_WIDEST_INT value;

  /* When checking for an address, we need to handle strict vs. non-strict
     register checks.  Don't use address_operand, but instead its
     equivalent (its callee, which it is just a wrapper for),
     memory_operand_p and the strict-equivalent strict_memory_address_p.  */
  if (c == 'U')
    return
      strict
      ? strict_memory_address_p (Pmode, x)
      : memory_address_p (Pmode, x);

  /* R asks whether x is to be loaded with GETA or something else.  Right
     now, only a SYMBOL_REF and LABEL_REF can fit for
     TARGET_BASE_ADDRESSES.

     Only constant symbolic addresses apply.  With TARGET_BASE_ADDRESSES,
     we just allow straight LABEL_REF or SYMBOL_REFs with SYMBOL_REF_FLAG
     set right now; only function addresses and code labels.  If we change
     to let SYMBOL_REF_FLAG be set on other symbols, we have to check
     inside CONST expressions.  When TARGET_BASE_ADDRESSES is not in
     effect, a "raw" constant check together with mmix_constant_address_p
     is all that's needed; we want all constant addresses to be loaded
     with GETA then.  */
  if (c == 'R')
    return
      GET_CODE (x) != CONST_INT && GET_CODE (x) != CONST_DOUBLE
      && mmix_constant_address_p (x)
      && (! TARGET_BASE_ADDRESSES
	  || (GET_CODE (x) == LABEL_REF
	      || (GET_CODE (x) == SYMBOL_REF && SYMBOL_REF_FLAG (x))));

  if (GET_CODE (x) != CONST_DOUBLE || GET_MODE (x) != VOIDmode)
    return 0;

  value = mmix_intval (x);

  /* We used to map Q->J, R->K, S->L, T->N, U->O, but we don't have to any
     more ('U' taken for address_operand, 'R' similarly).  Some letters map
     outside of CONST_INT, though; we still use 'S' and 'T'.  */
  if (c == 'S')
    return mmix_shiftable_wyde_value (value);
  else if (c == 'T')
    return mmix_shiftable_wyde_value (~value);
  return 0;
}

/* DYNAMIC_CHAIN_ADDRESS.  */

rtx
mmix_dynamic_chain_address (rtx frame)
{
  /* FIXME: the frame-pointer is stored at offset -8 from the current
     frame-pointer.  Unfortunately, the caller assumes that a
     frame-pointer is present for *all* previous frames.  There should be
     a way to say that that cannot be done, like for RETURN_ADDR_RTX.  */
  return plus_constant (frame, -8);
}

/* STARTING_FRAME_OFFSET.  */

int
mmix_starting_frame_offset (void)
{
  /* The old frame pointer is in the slot below the new one, so
     FIRST_PARM_OFFSET does not need to depend on whether the
     frame-pointer is needed or not.  We have to adjust for the register
     stack pointer being located below the saved frame pointer.
     Similarly, we store the return address on the stack too, for
     exception handling, and always if we save the register stack pointer.  */
  return
    (-8
     + (MMIX_CFUN_HAS_LANDING_PAD
	? -16 : (MMIX_CFUN_NEEDS_SAVED_EH_RETURN_ADDRESS ? -8 : 0)));
}

/* RETURN_ADDR_RTX.  */

rtx
mmix_return_addr_rtx (int count, rtx frame ATTRIBUTE_UNUSED)
{
  return count == 0
    ? (MMIX_CFUN_NEEDS_SAVED_EH_RETURN_ADDRESS
       /* FIXME: Set frame_alias_set on the following.  (Why?)
	  See mmix_initial_elimination_offset for the reason we can't use
	  get_hard_reg_initial_val for both.  Always using a stack slot
	  and not a register would be suboptimal.  */
       ? validize_mem (gen_rtx_MEM (Pmode, plus_constant (frame_pointer_rtx, -16)))
       : get_hard_reg_initial_val (Pmode, MMIX_INCOMING_RETURN_ADDRESS_REGNUM))
    : NULL_RTX;
}

/* SETUP_FRAME_ADDRESSES.  */

void
mmix_setup_frame_addresses (void)
{
  /* Nothing needed at the moment.  */
}

/* The difference between the (imaginary) frame pointer and the stack
   pointer.  Used to eliminate the frame pointer.  */

int
mmix_initial_elimination_offset (int fromreg, int toreg)
{
  int regno;
  int fp_sp_offset
    = (get_frame_size () + current_function_outgoing_args_size + 7) & ~7;

  /* There is no actual offset between these two virtual values, but for
     the frame-pointer, we have the old one in the stack position below
     it, so the offset for the frame-pointer to the stack-pointer is one
     octabyte larger.  */
  if (fromreg == MMIX_ARG_POINTER_REGNUM
      && toreg == MMIX_FRAME_POINTER_REGNUM)
    return 0;

  /* The difference is the size of local variables plus the size of
     outgoing function arguments that would normally be passed as
     registers but must be passed on stack because we're out of
     function-argument registers.  Only global saved registers are
     counted; the others go on the register stack.

     The frame-pointer is counted too if it is what is eliminated, as we
     need to balance the offset for it from STARTING_FRAME_OFFSET.

     Also add in the slot for the register stack pointer we save if we
     have a landing pad.

     Unfortunately, we can't access $0..$14, from unwinder code easily, so
     store the return address in a frame slot too.  FIXME: Only for
     non-leaf functions.  FIXME: Always with a landing pad, because it's
     hard to know whether we need the other at the time we know we need
     the offset for one (and have to state it).  It's a kludge until we
     can express the register stack in the EH frame info.

     We have to do alignment here; get_frame_size will not return a
     multiple of STACK_BOUNDARY.  FIXME: Add note in manual.  */

  for (regno = MMIX_FIRST_GLOBAL_REGNUM;
       regno <= 255;
       regno++)
    if ((regs_ever_live[regno] && ! call_used_regs[regno])
	|| IS_MMIX_EH_RETURN_DATA_REG (regno))
      fp_sp_offset += 8;

  return fp_sp_offset
    + (MMIX_CFUN_HAS_LANDING_PAD
       ? 16 : (MMIX_CFUN_NEEDS_SAVED_EH_RETURN_ADDRESS ? 8 : 0))
    + (fromreg == MMIX_ARG_POINTER_REGNUM ? 0 : 8);
}

/* Return an rtx for a function argument to go in a register, and 0 for
   one that must go on stack.  */

rtx
mmix_function_arg (const CUMULATIVE_ARGS *argsp,
		   enum machine_mode mode,
		   tree type,
		   int named ATTRIBUTE_UNUSED,
		   int incoming)
{
  /* Last-argument marker.  */
  if (type == void_type_node)
    return (argsp->regs < MMIX_MAX_ARGS_IN_REGS)
      ? gen_rtx_REG (mode,
		     (incoming
		      ? MMIX_FIRST_INCOMING_ARG_REGNUM
		      : MMIX_FIRST_ARG_REGNUM) + argsp->regs)
      : NULL_RTX;

  return (argsp->regs < MMIX_MAX_ARGS_IN_REGS
	  && !targetm.calls.must_pass_in_stack (mode, type)
	  && (GET_MODE_BITSIZE (mode) <= 64
	      || argsp->lib
	      || TARGET_LIBFUNC))
    ? gen_rtx_REG (mode,
		   (incoming
		    ? MMIX_FIRST_INCOMING_ARG_REGNUM
		    : MMIX_FIRST_ARG_REGNUM)
		   + argsp->regs)
    : NULL_RTX;
}

/* Returns nonzero for everything that goes by reference, 0 for
   everything that goes by value.  */

static bool
mmix_pass_by_reference (const CUMULATIVE_ARGS *argsp, enum machine_mode mode,
			tree type, bool named ATTRIBUTE_UNUSED)
{
  /* FIXME: Check: I'm not sure the must_pass_in_stack check is
     necessary.  */
  if (targetm.calls.must_pass_in_stack (mode, type))
    return true;

  if (MMIX_FUNCTION_ARG_SIZE (mode, type) > 8
      && !TARGET_LIBFUNC
      && (!argsp || !argsp->lib))
    return true;

  return false;
}

/* Return nonzero if regno is a register number where a parameter is
   passed, and 0 otherwise.  */

int
mmix_function_arg_regno_p (int regno, int incoming)
{
  int first_arg_regnum
    = incoming ? MMIX_FIRST_INCOMING_ARG_REGNUM : MMIX_FIRST_ARG_REGNUM;

  return regno >= first_arg_regnum
    && regno < first_arg_regnum + MMIX_MAX_ARGS_IN_REGS;
}

/* FUNCTION_OUTGOING_VALUE.  */

rtx
mmix_function_outgoing_value (tree valtype, tree func ATTRIBUTE_UNUSED)
{
  enum machine_mode mode = TYPE_MODE (valtype);
  enum machine_mode cmode;
  int first_val_regnum = MMIX_OUTGOING_RETURN_VALUE_REGNUM;
  rtx vec[MMIX_MAX_REGS_FOR_VALUE];
  int i;
  int nregs;

  /* Return values that fit in a register need no special handling.
     There's no register hole when parameters are passed in global
     registers.  */
  if (TARGET_ABI_GNU
      || GET_MODE_BITSIZE (mode) <= BITS_PER_WORD)
    return
      gen_rtx_REG (mode, MMIX_OUTGOING_RETURN_VALUE_REGNUM);

  if (COMPLEX_MODE_P (mode))
    /* A complex type, made up of components.  */
    cmode = TYPE_MODE (TREE_TYPE (valtype));
  else
    {
      /* Of the other larger-than-register modes, we only support
	 scalar mode TImode.  (At least, that's the only one that's
	 been rudimentally tested.)  Make sure we're alerted for
	 unexpected cases.  */
      if (mode != TImode)
	sorry ("support for mode %qs", GET_MODE_NAME (mode));

      /* In any case, we will fill registers to the natural size.  */
      cmode = DImode;
    }

  nregs = ((GET_MODE_BITSIZE (mode) + BITS_PER_WORD - 1) / BITS_PER_WORD);

  /* We need to take care of the effect of the register hole on return
     values of large sizes; the last register will appear as the first
     register, with the rest shifted.  (For complex modes, this is just
     swapped registers.)  */

  if (nregs > MMIX_MAX_REGS_FOR_VALUE)
    internal_error ("too large function value type, needs %d registers,\
 have only %d registers for this", nregs, MMIX_MAX_REGS_FOR_VALUE);

  /* FIXME: Maybe we should handle structure values like this too
     (adjusted for BLKmode), perhaps for both ABI:s.  */
  for (i = 0; i < nregs - 1; i++)
    vec[i]
      = gen_rtx_EXPR_LIST (VOIDmode,
			   gen_rtx_REG (cmode, first_val_regnum + i),
			   GEN_INT ((i + 1) * BITS_PER_UNIT));

  vec[nregs - 1]
    = gen_rtx_EXPR_LIST (VOIDmode,
			 gen_rtx_REG (cmode, first_val_regnum + nregs - 1),
			 const0_rtx);

  return gen_rtx_PARALLEL (VOIDmode, gen_rtvec_v (nregs, vec));
}

/* FUNCTION_VALUE_REGNO_P.  */

int
mmix_function_value_regno_p (int regno)
{
  return regno == MMIX_RETURN_VALUE_REGNUM;
}

/* EH_RETURN_DATA_REGNO. */

int
mmix_eh_return_data_regno (int n)
{
  if (n >= 0 && n < 4)
    return MMIX_EH_RETURN_DATA_REGNO_START + n;

  return INVALID_REGNUM;
}

/* EH_RETURN_STACKADJ_RTX. */

rtx
mmix_eh_return_stackadj_rtx (void)
{
  return gen_rtx_REG (Pmode, MMIX_EH_RETURN_STACKADJ_REGNUM);
}

/* EH_RETURN_HANDLER_RTX.  */

rtx
mmix_eh_return_handler_rtx (void)
{
  return gen_rtx_REG (Pmode, MMIX_INCOMING_RETURN_ADDRESS_REGNUM);
}

/* ASM_PREFERRED_EH_DATA_FORMAT. */

int
mmix_asm_preferred_eh_data_format (int code ATTRIBUTE_UNUSED,
				   int global ATTRIBUTE_UNUSED)
{
  /* This is the default (was at 2001-07-20).  Revisit when needed.  */
  return DW_EH_PE_absptr;
}

/* Make a note that we've seen the beginning of the prologue.  This
   matters to whether we'll translate register numbers as calculated by
   mmix_reorg.  */

static void
mmix_target_asm_function_prologue (FILE *stream ATTRIBUTE_UNUSED,
				   HOST_WIDE_INT framesize ATTRIBUTE_UNUSED)
{
  cfun->machine->in_prologue = 1;
}

/* Make a note that we've seen the end of the prologue.  */

static void
mmix_target_asm_function_end_prologue (FILE *stream ATTRIBUTE_UNUSED)
{
  cfun->machine->in_prologue = 0;
}

/* Implement TARGET_MACHINE_DEPENDENT_REORG.  No actual rearrangements
   done here; just virtually by calculating the highest saved stack
   register number used to modify the register numbers at output time.  */

static void
mmix_reorg (void)
{
  int regno;

  /* We put the number of the highest saved register-file register in a
     location convenient for the call-patterns to output.  Note that we
     don't tell dwarf2 about these registers, since it can't restore them
     anyway.  */
  for (regno = MMIX_LAST_STACK_REGISTER_REGNUM;
       regno >= 0;
       regno--)
    if ((regs_ever_live[regno] && !call_used_regs[regno])
	|| (regno == MMIX_FRAME_POINTER_REGNUM && frame_pointer_needed))
      break;

  /* Regardless of whether they're saved (they might be just read), we
     mustn't include registers that carry parameters.  We could scan the
     insns to see whether they're actually used (and indeed do other less
     trivial register usage analysis and transformations), but it seems
     wasteful to optimize for unused parameter registers.  As of
     2002-04-30, regs_ever_live[n] seems to be set for only-reads too, but
     that might change.  */
  if (!TARGET_ABI_GNU && regno < current_function_args_info.regs - 1)
    {
      regno = current_function_args_info.regs - 1;

      /* We don't want to let this cause us to go over the limit and make
	 incoming parameter registers be misnumbered and treating the last
	 parameter register and incoming return value register call-saved.
	 Stop things at the unmodified scheme.  */
      if (regno > MMIX_RETURN_VALUE_REGNUM - 1)
	regno = MMIX_RETURN_VALUE_REGNUM - 1;
    }

  cfun->machine->highest_saved_stack_register = regno;
}

/* TARGET_ASM_FUNCTION_EPILOGUE.  */

static void
mmix_target_asm_function_epilogue (FILE *stream,
				   HOST_WIDE_INT locals_size ATTRIBUTE_UNUSED)
{
  /* Emit an \n for readability of the generated assembly.  */
  fputc ('\n', stream);
}

/* TARGET_ASM_OUTPUT_MI_THUNK.  */

static void
mmix_asm_output_mi_thunk (FILE *stream,
			  tree fndecl ATTRIBUTE_UNUSED,
			  HOST_WIDE_INT delta,
			  HOST_WIDE_INT vcall_offset ATTRIBUTE_UNUSED,
			  tree func)
{
  /* If you define TARGET_STRUCT_VALUE_RTX that returns 0 (i.e. pass
     location of structure to return as invisible first argument), you
     need to tweak this code too.  */
  const char *regname = reg_names[MMIX_FIRST_INCOMING_ARG_REGNUM];

  if (delta >= 0 && delta < 65536)
    fprintf (stream, "\tINCL %s,%d\n", regname, (int)delta);
  else if (delta < 0 && delta >= -255)
    fprintf (stream, "\tSUBU %s,%s,%d\n", regname, regname, (int)-delta);
  else
    {
      mmix_output_register_setting (stream, 255, delta, 1);
      fprintf (stream, "\tADDU %s,%s,$255\n", regname, regname);
    }

  fprintf (stream, "\tJMP ");
  assemble_name (stream, XSTR (XEXP (DECL_RTL (func), 0), 0));
  fprintf (stream, "\n");
}

/* FUNCTION_PROFILER.  */

void
mmix_function_profiler (FILE *stream ATTRIBUTE_UNUSED,
			int labelno ATTRIBUTE_UNUSED)
{
  sorry ("function_profiler support for MMIX");
}

/* Worker function for TARGET_SETUP_INCOMING_VARARGS.  For the moment,
   let's stick to pushing argument registers on the stack.  Later, we
   can parse all arguments in registers, to improve performance.  */

static void
mmix_setup_incoming_varargs (CUMULATIVE_ARGS *args_so_farp,
			     enum machine_mode mode,
			     tree vartype,
			     int *pretend_sizep,
			     int second_time ATTRIBUTE_UNUSED)
{
  /* The last named variable has been handled, but
     args_so_farp has not been advanced for it.  */
  if (args_so_farp->regs + 1 < MMIX_MAX_ARGS_IN_REGS)
    *pretend_sizep = (MMIX_MAX_ARGS_IN_REGS - (args_so_farp->regs + 1)) * 8;

  /* We assume that one argument takes up one register here.  That should
     be true until we start messing with multi-reg parameters.  */
  if ((7 + (MMIX_FUNCTION_ARG_SIZE (mode, vartype))) / 8 != 1)
    internal_error ("MMIX Internal: Last named vararg would not fit in a register");
}

/* TRAMPOLINE_SIZE.  */
/* Four 4-byte insns plus two 8-byte values.  */
int mmix_trampoline_size = 32;


/* TRAMPOLINE_TEMPLATE.  */

void
mmix_trampoline_template (FILE *stream)
{
  /* Read a value into the static-chain register and jump somewhere.  The
     static chain is stored at offset 16, and the function address is
     stored at offset 24.  */
  /* FIXME: GCC copies this using *intsize* (tetra), when it should use
     register size (octa).  */
  fprintf (stream, "\tGETA $255,1F\n\t");
  fprintf (stream, "LDOU %s,$255,0\n\t",
	   reg_names[MMIX_STATIC_CHAIN_REGNUM]);
  fprintf (stream, "LDOU $255,$255,8\n\t");
  fprintf (stream, "GO $255,$255,0\n");
  fprintf (stream, "1H\tOCTA 0\n\t");
  fprintf (stream, "OCTA 0\n");
}

/* INITIALIZE_TRAMPOLINE.  */
/* Set the static chain and function pointer field in the trampoline.
   We also SYNCID here to be sure (doesn't matter in the simulator, but
   some day it will).  */

void
mmix_initialize_trampoline (rtx trampaddr, rtx fnaddr, rtx static_chain)
{
  emit_move_insn (gen_rtx_MEM (DImode, plus_constant (trampaddr, 16)),
		  static_chain);
  emit_move_insn (gen_rtx_MEM (DImode,
			       plus_constant (trampaddr, 24)),
		  fnaddr);
  emit_insn (gen_sync_icache (validize_mem (gen_rtx_MEM (DImode,
							 trampaddr)),
			      GEN_INT (mmix_trampoline_size - 1)));
}

/* We must exclude constant addresses that have an increment that is not a
   multiple of four bytes because of restrictions of the GETA
   instruction, unless TARGET_BASE_ADDRESSES.  */

int
mmix_constant_address_p (rtx x)
{
  RTX_CODE code = GET_CODE (x);
  int addend = 0;
  /* When using "base addresses", anything constant goes.  */
  int constant_ok = TARGET_BASE_ADDRESSES != 0;

  switch (code)
    {
    case LABEL_REF:
    case SYMBOL_REF:
      return 1;

    case HIGH:
      /* FIXME: Don't know how to dissect these.  Avoid them for now,
	 except we know they're constants.  */
      return constant_ok;

    case CONST_INT:
      addend = INTVAL (x);
      break;

    case CONST_DOUBLE:
      if (GET_MODE (x) != VOIDmode)
	/* Strange that we got here.  FIXME: Check if we do.  */
	return constant_ok;
      addend = CONST_DOUBLE_LOW (x);
      break;

    case CONST:
      /* Note that expressions with arithmetic on forward references don't
	 work in mmixal.  People using gcc assembly code with mmixal might
	 need to move arrays and such to before the point of use.  */
      if (GET_CODE (XEXP (x, 0)) == PLUS)
	{
	  rtx x0 = XEXP (XEXP (x, 0), 0);
	  rtx x1 = XEXP (XEXP (x, 0), 1);

	  if ((GET_CODE (x0) == SYMBOL_REF
	       || GET_CODE (x0) == LABEL_REF)
	      && (GET_CODE (x1) == CONST_INT
		  || (GET_CODE (x1) == CONST_DOUBLE
		      && GET_MODE (x1) == VOIDmode)))
	    addend = mmix_intval (x1);
	  else
	    return constant_ok;
	}
      else
	return constant_ok;
      break;

    default:
      return 0;
    }

  return constant_ok || (addend & 3) == 0;
}

/* Return 1 if the address is OK, otherwise 0.
   Used by GO_IF_LEGITIMATE_ADDRESS.  */

int
mmix_legitimate_address (enum machine_mode mode ATTRIBUTE_UNUSED,
			 rtx x,
			 int strict_checking)
{
#define MMIX_REG_OK(X)							\
  ((strict_checking							\
    && (REGNO (X) <= MMIX_LAST_GENERAL_REGISTER				\
	|| (reg_renumber[REGNO (X)] > 0					\
	    && reg_renumber[REGNO (X)] <= MMIX_LAST_GENERAL_REGISTER)))	\
   || (!strict_checking							\
       && (REGNO (X) <= MMIX_LAST_GENERAL_REGISTER			\
	   || REGNO (X) >= FIRST_PSEUDO_REGISTER			\
	   || REGNO (X) == ARG_POINTER_REGNUM)))

  /* We only accept:
     (mem reg)
     (mem (plus reg reg))
     (mem (plus reg 0..255)).
     unless TARGET_BASE_ADDRESSES, in which case we accept all
     (mem constant_address) too.  */


    /* (mem reg) */
  if (REG_P (x) && MMIX_REG_OK (x))
    return 1;

  if (GET_CODE(x) == PLUS)
    {
      rtx x1 = XEXP (x, 0);
      rtx x2 = XEXP (x, 1);

      /* Try swapping the order.  FIXME: Do we need this?  */
      if (! REG_P (x1))
	{
	  rtx tem = x1;
	  x1 = x2;
	  x2 = tem;
	}

      /* (mem (plus (reg?) (?))) */
      if (!REG_P (x1) || !MMIX_REG_OK (x1))
	return TARGET_BASE_ADDRESSES && mmix_constant_address_p (x);

      /* (mem (plus (reg) (reg?))) */
      if (REG_P (x2) && MMIX_REG_OK (x2))
	return 1;

      /* (mem (plus (reg) (0..255?))) */
      if (GET_CODE (x2) == CONST_INT
	  && CONST_OK_FOR_LETTER_P (INTVAL (x2), 'I'))
	return 1;

      return 0;
    }

  return TARGET_BASE_ADDRESSES && mmix_constant_address_p (x);
}

/* LEGITIMATE_CONSTANT_P.  */

int
mmix_legitimate_constant_p (rtx x)
{
  RTX_CODE code = GET_CODE (x);

  /* We must allow any number due to the way the cse passes works; if we
     do not allow any number here, general_operand will fail, and insns
     will fatally fail recognition instead of "softly".  */
  if (code == CONST_INT || code == CONST_DOUBLE)
    return 1;

  return CONSTANT_ADDRESS_P (x);
}

/* SELECT_CC_MODE.  */

enum machine_mode
mmix_select_cc_mode (RTX_CODE op, rtx x, rtx y ATTRIBUTE_UNUSED)
{
  /* We use CCmode, CC_UNSmode, CC_FPmode, CC_FPEQmode and CC_FUNmode to
     output different compare insns.  Note that we do not check the
     validity of the comparison here.  */

  if (GET_MODE_CLASS (GET_MODE (x)) == MODE_FLOAT)
    {
      if (op == ORDERED || op == UNORDERED || op == UNGE
	  || op == UNGT || op == UNLE || op == UNLT)
	return CC_FUNmode;

      if (op == EQ || op == NE)
	return CC_FPEQmode;

      return CC_FPmode;
    }

  if (op == GTU || op == LTU || op == GEU || op == LEU)
    return CC_UNSmode;

  return CCmode;
}

/* REVERSIBLE_CC_MODE.  */

int
mmix_reversible_cc_mode (enum machine_mode mode)
{
  /* That is, all integer and the EQ, NE, ORDERED and UNORDERED float
     compares.  */
  return mode != CC_FPmode;
}

/* TARGET_RTX_COSTS.  */

static bool
mmix_rtx_costs (rtx x ATTRIBUTE_UNUSED,
		int code ATTRIBUTE_UNUSED,
		int outer_code ATTRIBUTE_UNUSED,
		int *total ATTRIBUTE_UNUSED)
{
  /* For the time being, this is just a stub and we'll accept the
     generic calculations, until we can do measurements, at least.
     Say we did not modify any calculated costs.  */
  return false;
}

/* REGISTER_MOVE_COST.  */

int
mmix_register_move_cost (enum machine_mode mode ATTRIBUTE_UNUSED,
			 enum reg_class from,
			 enum reg_class to)
{
  return (from == GENERAL_REGS && from == to) ? 2 : 3;
}

/* Note that we don't have a TEXT_SECTION_ASM_OP, because it has to be a
   compile-time constant; it's used in an asm in crtstuff.c, compiled for
   the target.  */

/* DATA_SECTION_ASM_OP.  */

const char *
mmix_data_section_asm_op (void)
{
  return "\t.data ! mmixal:= 8H LOC 9B";
}

static void
mmix_encode_section_info (tree decl, rtx rtl, int first)
{
  /* Test for an external declaration, and do nothing if it is one.  */
  if ((TREE_CODE (decl) == VAR_DECL
       && (DECL_EXTERNAL (decl) || TREE_PUBLIC (decl)))
      || (TREE_CODE (decl) == FUNCTION_DECL && TREE_PUBLIC (decl)))
    ;
  else if (first && DECL_P (decl))
    {
      /* For non-visible declarations, add a "@" prefix, which we skip
	 when the label is output.  If the label does not have this
	 prefix, a ":" is output if -mtoplevel-symbols.

	 Note that this does not work for data that is declared extern and
	 later defined as static.  If there's code in between, that code
	 will refer to the extern declaration, and vice versa.  This just
	 means that when -mtoplevel-symbols is in use, we can just handle
	 well-behaved ISO-compliant code.  */

      const char *str = XSTR (XEXP (rtl, 0), 0);
      int len = strlen (str);
      char *newstr;

      /* Why is the return type of ggc_alloc_string const?  */
      newstr = (char *) ggc_alloc_string ("", len + 1);

      strcpy (newstr + 1, str);
      *newstr = '@';
      XSTR (XEXP (rtl, 0), 0) = newstr;
    }

  /* Set SYMBOL_REF_FLAG for things that we want to access with GETA.  We
     may need different options to reach for different things with GETA.
     For now, functions and things we know or have been told are constant.  */
  if (TREE_CODE (decl) == FUNCTION_DECL
      || TREE_CONSTANT (decl)
      || (TREE_CODE (decl) == VAR_DECL
	  && TREE_READONLY (decl)
	  && !TREE_SIDE_EFFECTS (decl)
	  && (!DECL_INITIAL (decl)
	      || TREE_CONSTANT (DECL_INITIAL (decl)))))
    SYMBOL_REF_FLAG (XEXP (rtl, 0)) = 1;
}

static const char *
mmix_strip_name_encoding (const char *name)
{
  for (; (*name == '@' || *name == '*'); name++)
    ;

  return name;
}

/* TARGET_ASM_FILE_START.
   We just emit a little comment for the time being.  */

static void
mmix_file_start (void)
{
  default_file_start ();

  fputs ("! mmixal:= 8H LOC Data_Section\n", asm_out_file);

  /* Make sure each file starts with the text section.  */
  switch_to_section (text_section);
}

/* TARGET_ASM_FILE_END.  */

static void
mmix_file_end (void)
{
  /* Make sure each file ends with the data section.  */
  switch_to_section (data_section);
}

/* ASM_OUTPUT_SOURCE_FILENAME.  */

void
mmix_asm_output_source_filename (FILE *stream, const char *name)
{
  fprintf (stream, "# 1 ");
  OUTPUT_QUOTED_STRING (stream, name);
  fprintf (stream, "\n");
}

/* OUTPUT_QUOTED_STRING.  */

void
mmix_output_quoted_string (FILE *stream, const char *string, int length)
{
  const char * string_end = string + length;
  static const char *const unwanted_chars = "\"[]\\";

  /* Output "any character except newline and double quote character".  We
     play it safe and avoid all control characters too.  We also do not
     want [] as characters, should input be passed through m4 with [] as
     quotes.  Further, we avoid "\", because the GAS port handles it as a
     quoting character.  */
  while (string < string_end)
    {
      if (*string
	  && (unsigned char) *string < 128
	  && !ISCNTRL (*string)
	  && strchr (unwanted_chars, *string) == NULL)
	{
	  fputc ('"', stream);
	  while (*string
		 && (unsigned char) *string < 128
		 && !ISCNTRL (*string)
		 && strchr (unwanted_chars, *string) == NULL
		 && string < string_end)
	    {
	      fputc (*string, stream);
	      string++;
	    }
	  fputc ('"', stream);
	  if (string < string_end)
	    fprintf (stream, ",");
	}
      if (string < string_end)
	{
	  fprintf (stream, "#%x", *string & 255);
	  string++;
	  if (string < string_end)
	    fprintf (stream, ",");
	}
    }
}

/* Target hook for assembling integer objects.  Use mmix_print_operand
   for WYDE and TETRA.  Use mmix_output_octa to output 8-byte
   CONST_DOUBLEs.  */

static bool
mmix_assemble_integer (rtx x, unsigned int size, int aligned_p)
{
  if (aligned_p)
    switch (size)
      {
	/* We handle a limited number of types of operands in here.  But
	   that's ok, because we can punt to generic functions.  We then
	   pretend that aligned data isn't needed, so the usual .<pseudo>
	   syntax is used (which works for aligned data too).  We actually
	   *must* do that, since we say we don't have simple aligned
	   pseudos, causing this function to be called.  We just try and
	   keep as much compatibility as possible with mmixal syntax for
	   normal cases (i.e. without GNU extensions and C only).  */
      case 1:
	if (GET_CODE (x) != CONST_INT)
	  {
	    aligned_p = 0;
	    break;
	  }
	fputs ("\tBYTE\t", asm_out_file);
	mmix_print_operand (asm_out_file, x, 'B');
	fputc ('\n', asm_out_file);
	return true;

      case 2:
	if (GET_CODE (x) != CONST_INT)
	  {
	    aligned_p = 0;
	    break;
	  }
	fputs ("\tWYDE\t", asm_out_file);
	mmix_print_operand (asm_out_file, x, 'W');
	fputc ('\n', asm_out_file);
	return true;

      case 4:
	if (GET_CODE (x) != CONST_INT)
	  {
	    aligned_p = 0;
	    break;
	  }
	fputs ("\tTETRA\t", asm_out_file);
	mmix_print_operand (asm_out_file, x, 'L');
	fputc ('\n', asm_out_file);
	return true;

      case 8:
	/* We don't get here anymore for CONST_DOUBLE, because DImode
	   isn't expressed as CONST_DOUBLE, and DFmode is handled
	   elsewhere.  */
	gcc_assert (GET_CODE (x) != CONST_DOUBLE);
	assemble_integer_with_op ("\tOCTA\t", x);
	return true;
      }
  return default_assemble_integer (x, size, aligned_p);
}

/* ASM_OUTPUT_ASCII.  */

void
mmix_asm_output_ascii (FILE *stream, const char *string, int length)
{
  while (length > 0)
    {
      int chunk_size = length > 60 ? 60 : length;
      fprintf (stream, "\tBYTE ");
      mmix_output_quoted_string (stream, string, chunk_size);
      string += chunk_size;
      length -= chunk_size;
      fprintf (stream, "\n");
    }
}

/* ASM_OUTPUT_ALIGNED_COMMON.  */

void
mmix_asm_output_aligned_common (FILE *stream,
				const char *name,
				int size,
				int align)
{
  /* This is mostly the elfos.h one.  There doesn't seem to be a way to
     express this in a mmixal-compatible way.  */
  fprintf (stream, "\t.comm\t");
  assemble_name (stream, name);
  fprintf (stream, ",%u,%u ! mmixal-incompatible COMMON\n",
	   size, align / BITS_PER_UNIT);
}

/* ASM_OUTPUT_ALIGNED_LOCAL.  */

void
mmix_asm_output_aligned_local (FILE *stream,
			       const char *name,
			       int size,
			       int align)
{
  switch_to_section (data_section);

  ASM_OUTPUT_ALIGN (stream, exact_log2 (align/BITS_PER_UNIT));
  assemble_name (stream, name);
  fprintf (stream, "\tLOC @+%d\n", size);
}

/* ASM_OUTPUT_LABEL.  */

void
mmix_asm_output_label (FILE *stream, const char *name)
{
  assemble_name (stream, name);
  fprintf (stream, "\tIS @\n");
}

/* ASM_OUTPUT_INTERNAL_LABEL.  */

void
mmix_asm_output_internal_label (FILE *stream, const char *name)
{
  assemble_name_raw (stream, name);
  fprintf (stream, "\tIS @\n");
}

/* ASM_DECLARE_REGISTER_GLOBAL.  */

void
mmix_asm_declare_register_global (FILE *stream ATTRIBUTE_UNUSED,
				  tree decl ATTRIBUTE_UNUSED,
				  int regno ATTRIBUTE_UNUSED,
				  const char *name ATTRIBUTE_UNUSED)
{
  /* Nothing to do here, but there *will* be, therefore the framework is
     here.  */
}

/* ASM_WEAKEN_LABEL.  */

void
mmix_asm_weaken_label (FILE *stream ATTRIBUTE_UNUSED,
		       const char *name ATTRIBUTE_UNUSED)
{
  fprintf (stream, "\t.weak ");
  assemble_name (stream, name);
  fprintf (stream, " ! mmixal-incompatible\n");
}

/* MAKE_DECL_ONE_ONLY.  */

void
mmix_make_decl_one_only (tree decl)
{
  DECL_WEAK (decl) = 1;
}

/* ASM_OUTPUT_LABELREF.
   Strip GCC's '*' and our own '@'.  No order is assumed.  */

void
mmix_asm_output_labelref (FILE *stream, const char *name)
{
  int is_extern = 1;

  for (; (*name == '@' || *name == '*'); name++)
    if (*name == '@')
      is_extern = 0;

  asm_fprintf (stream, "%s%U%s",
	       is_extern && TARGET_TOPLEVEL_SYMBOLS ? ":" : "",
	       name);
}

/* ASM_OUTPUT_DEF.  */

void
mmix_asm_output_def (FILE *stream, const char *name, const char *value)
{
  assemble_name (stream, name);
  fprintf (stream, "\tIS ");
  assemble_name (stream, value);
  fputc ('\n', stream);
}

/* PRINT_OPERAND.  */

void
mmix_print_operand (FILE *stream, rtx x, int code)
{
  /* When we add support for different codes later, we can, when needed,
     drop through to the main handler with a modified operand.  */
  rtx modified_x = x;
  int regno = x != NULL_RTX && REG_P (x) ? REGNO (x) : 0;

  switch (code)
    {
      /* Unrelated codes are in alphabetic order.  */

    case '+':
      /* For conditional branches, output "P" for a probable branch.  */
      if (TARGET_BRANCH_PREDICT)
	{
	  x = find_reg_note (current_output_insn, REG_BR_PROB, 0);
	  if (x && INTVAL (XEXP (x, 0)) > REG_BR_PROB_BASE / 2)
	    putc ('P', stream);
	}
      return;

    case '.':
      /* For the %d in POP %d,0.  */
      fprintf (stream, "%d", MMIX_POP_ARGUMENT ());
      return;

    case 'B':
      if (GET_CODE (x) != CONST_INT)
	fatal_insn ("MMIX Internal: Expected a CONST_INT, not this", x);
      fprintf (stream, "%d", (int) (INTVAL (x) & 0xff));
      return;

    case 'H':
      /* Highpart.  Must be general register, and not the last one, as
	 that one cannot be part of a consecutive register pair.  */
      if (regno > MMIX_LAST_GENERAL_REGISTER - 1)
	internal_error ("MMIX Internal: Bad register: %d", regno);

      /* This is big-endian, so the high-part is the first one.  */
      fprintf (stream, "%s", reg_names[MMIX_OUTPUT_REGNO (regno)]);
      return;

    case 'L':
      /* Lowpart.  Must be CONST_INT or general register, and not the last
	 one, as that one cannot be part of a consecutive register pair.  */
      if (GET_CODE (x) == CONST_INT)
	{
	  fprintf (stream, "#%lx",
		   (unsigned long) (INTVAL (x)
				    & ((unsigned int) 0x7fffffff * 2 + 1)));
	  return;
	}

      if (GET_CODE (x) == SYMBOL_REF)
	{
	  output_addr_const (stream, x);
	  return;
	}

      if (regno > MMIX_LAST_GENERAL_REGISTER - 1)
	internal_error ("MMIX Internal: Bad register: %d", regno);

      /* This is big-endian, so the low-part is + 1.  */
      fprintf (stream, "%s", reg_names[MMIX_OUTPUT_REGNO (regno) + 1]);
      return;

      /* Can't use 'a' because that's a generic modifier for address
	 output.  */
    case 'A':
      mmix_output_shiftvalue_op_from_str (stream, "ANDN",
					  ~(unsigned HOST_WIDEST_INT)
					  mmix_intval (x));
      return;

    case 'i':
      mmix_output_shiftvalue_op_from_str (stream, "INC",
					  (unsigned HOST_WIDEST_INT)
					  mmix_intval (x));
      return;

    case 'o':
      mmix_output_shiftvalue_op_from_str (stream, "OR",
					  (unsigned HOST_WIDEST_INT)
					  mmix_intval (x));
      return;

    case 's':
      mmix_output_shiftvalue_op_from_str (stream, "SET",
					  (unsigned HOST_WIDEST_INT)
					  mmix_intval (x));
      return;

    case 'd':
    case 'D':
      mmix_output_condition (stream, x, (code == 'D'));
      return;

    case 'e':
      /* Output an extra "e" to make fcmpe, fune.  */
      if (TARGET_FCMP_EPSILON)
	fprintf (stream, "e");
      return;

    case 'm':
      /* Output the number minus 1.  */
      if (GET_CODE (x) != CONST_INT)
	{
	  fatal_insn ("MMIX Internal: Bad value for 'm', not a CONST_INT",
		      x);
	}
      fprintf (stream, HOST_WIDEST_INT_PRINT_DEC,
	       (HOST_WIDEST_INT) (mmix_intval (x) - 1));
      return;

    case 'p':
      /* Store the number of registers we want to save.  This was setup
	 by the prologue.  The actual operand contains the number of
	 registers to pass, but we don't use it currently.  Anyway, we
	 need to output the number of saved registers here.  */
      fprintf (stream, "%d",
	       cfun->machine->highest_saved_stack_register + 1);
      return;

    case 'r':
      /* Store the register to output a constant to.  */
      if (! REG_P (x))
	fatal_insn ("MMIX Internal: Expected a register, not this", x);
      mmix_output_destination_register = MMIX_OUTPUT_REGNO (regno);
      return;

    case 'I':
      /* Output the constant.  Note that we use this for floats as well.  */
      if (GET_CODE (x) != CONST_INT
	  && (GET_CODE (x) != CONST_DOUBLE
	      || (GET_MODE (x) != VOIDmode && GET_MODE (x) != DFmode
		  && GET_MODE (x) != SFmode)))
	fatal_insn ("MMIX Internal: Expected a constant, not this", x);
      mmix_output_register_setting (stream,
				    mmix_output_destination_register,
				    mmix_intval (x), 0);
      return;

    case 'U':
      /* An U for unsigned, if TARGET_ZERO_EXTEND.  Ignore the operand.  */
      if (TARGET_ZERO_EXTEND)
	putc ('U', stream);
      return;

    case 'v':
      mmix_output_shifted_value (stream, (HOST_WIDEST_INT) mmix_intval (x));
      return;

    case 'V':
      mmix_output_shifted_value (stream, (HOST_WIDEST_INT) ~mmix_intval (x));
      return;

    case 'W':
      if (GET_CODE (x) != CONST_INT)
	fatal_insn ("MMIX Internal: Expected a CONST_INT, not this", x);
      fprintf (stream, "#%x", (int) (INTVAL (x) & 0xffff));
      return;

    case 0:
      /* Nothing to do.  */
      break;

    default:
      /* Presumably there's a missing case above if we get here.  */
      internal_error ("MMIX Internal: Missing %qc case in mmix_print_operand", code);
    }

  switch (GET_CODE (modified_x))
    {
    case REG:
      regno = REGNO (modified_x);
      if (regno >= FIRST_PSEUDO_REGISTER)
	internal_error ("MMIX Internal: Bad register: %d", regno);
      fprintf (stream, "%s", reg_names[MMIX_OUTPUT_REGNO (regno)]);
      return;

    case MEM:
      output_address (XEXP (modified_x, 0));
      return;

    case CONST_INT:
      /* For -2147483648, mmixal complains that the constant does not fit
	 in 4 bytes, so let's output it as hex.  Take care to handle hosts
	 where HOST_WIDE_INT is longer than an int.

	 Print small constants +-255 using decimal.  */

      if (INTVAL (modified_x) > -256 && INTVAL (modified_x) < 256)
	fprintf (stream, "%d", (int) (INTVAL (modified_x)));
      else
	fprintf (stream, "#%x",
		 (int) (INTVAL (modified_x)) & (unsigned int) ~0);
      return;

    case CONST_DOUBLE:
      /* Do somewhat as CONST_INT.  */
      mmix_output_octa (stream, mmix_intval (modified_x), 0);
      return;

    case CONST:
      output_addr_const (stream, modified_x);
      return;

    default:
      /* No need to test for all strange things.  Let output_addr_const do
	 it for us.  */
      if (CONSTANT_P (modified_x)
	  /* Strangely enough, this is not included in CONSTANT_P.
	     FIXME: Ask/check about sanity here.  */
	  || GET_CODE (modified_x) == CODE_LABEL)
	{
	  output_addr_const (stream, modified_x);
	  return;
	}

      /* We need the original here.  */
      fatal_insn ("MMIX Internal: Cannot decode this operand", x);
    }
}

/* PRINT_OPERAND_PUNCT_VALID_P.  */

int
mmix_print_operand_punct_valid_p (int code ATTRIBUTE_UNUSED)
{
  /* A '+' is used for branch prediction, similar to other ports.  */
  return code == '+'
    /* A '.' is used for the %d in the POP %d,0 return insn.  */
    || code == '.';
}

/* PRINT_OPERAND_ADDRESS.  */

void
mmix_print_operand_address (FILE *stream, rtx x)
{
  if (REG_P (x))
    {
      /* I find the generated assembly code harder to read without
	 the ",0".  */
      fprintf (stream, "%s,0", reg_names[MMIX_OUTPUT_REGNO (REGNO (x))]);
      return;
    }
  else if (GET_CODE (x) == PLUS)
    {
      rtx x1 = XEXP (x, 0);
      rtx x2 = XEXP (x, 1);

      if (REG_P (x1))
	{
	  fprintf (stream, "%s,", reg_names[MMIX_OUTPUT_REGNO (REGNO (x1))]);

	  if (REG_P (x2))
	    {
	      fprintf (stream, "%s",
		       reg_names[MMIX_OUTPUT_REGNO (REGNO (x2))]);
	      return;
	    }
	  else if (GET_CODE (x2) == CONST_INT
		   && CONST_OK_FOR_LETTER_P (INTVAL (x2), 'I'))
	    {
	      output_addr_const (stream, x2);
	      return;
	    }
	}
    }

  if (TARGET_BASE_ADDRESSES && mmix_legitimate_constant_p (x))
    {
      output_addr_const (stream, x);
      return;
    }

  fatal_insn ("MMIX Internal: This is not a recognized address", x);
}

/* ASM_OUTPUT_REG_PUSH.  */

void
mmix_asm_output_reg_push (FILE *stream, int regno)
{
  fprintf (stream, "\tSUBU %s,%s,8\n\tSTOU %s,%s,0\n",
	   reg_names[MMIX_STACK_POINTER_REGNUM],
	   reg_names[MMIX_STACK_POINTER_REGNUM],
	   reg_names[MMIX_OUTPUT_REGNO (regno)],
	   reg_names[MMIX_STACK_POINTER_REGNUM]);
}

/* ASM_OUTPUT_REG_POP.  */

void
mmix_asm_output_reg_pop (FILE *stream, int regno)
{
  fprintf (stream, "\tLDOU %s,%s,0\n\tINCL %s,8\n",
	   reg_names[MMIX_OUTPUT_REGNO (regno)],
	   reg_names[MMIX_STACK_POINTER_REGNUM],
	   reg_names[MMIX_STACK_POINTER_REGNUM]);
}

/* ASM_OUTPUT_ADDR_DIFF_ELT.  */

void
mmix_asm_output_addr_diff_elt (FILE *stream,
			       rtx body ATTRIBUTE_UNUSED,
			       int value,
			       int rel)
{
  fprintf (stream, "\tTETRA L%d-L%d\n", value, rel);
}

/* ASM_OUTPUT_ADDR_VEC_ELT.  */

void
mmix_asm_output_addr_vec_elt (FILE *stream, int value)
{
  fprintf (stream, "\tOCTA L:%d\n", value);
}

/* ASM_OUTPUT_SKIP.  */

void
mmix_asm_output_skip (FILE *stream, int nbytes)
{
  fprintf (stream, "\tLOC @+%d\n", nbytes);
}

/* ASM_OUTPUT_ALIGN.  */

void
mmix_asm_output_align (FILE *stream, int power)
{
  /* We need to record the needed alignment of this section in the object,
     so we have to output an alignment directive.  Use a .p2align (not
     .align) so people will never have to wonder about whether the
     argument is in number of bytes or the log2 thereof.  We do it in
     addition to the LOC directive, so nothing needs tweaking when
     copy-pasting assembly into mmixal.  */
 fprintf (stream, "\t.p2align %d\n", power);
 fprintf (stream, "\tLOC @+(%d-@)&%d\n", 1 << power, (1 << power) - 1);
}

/* DBX_REGISTER_NUMBER.  */

int
mmix_dbx_register_number (int regno)
{
  /* Adjust the register number to the one it will be output as, dammit.
     It'd be nice if we could check the assumption that we're filling a
     gap, but every register between the last saved register and parameter
     registers might be a valid parameter register.  */
  regno = MMIX_OUTPUT_REGNO (regno);

  /* We need to renumber registers to get the number of the return address
     register in the range 0..255.  It is also space-saving if registers
     mentioned in the call-frame information (which uses this function by
     defaulting DWARF_FRAME_REGNUM to DBX_REGISTER_NUMBER) are numbered
     0 .. 63.  So map 224 .. 256+15 -> 0 .. 47 and 0 .. 223 -> 48..223+48.  */
  return regno >= 224 ? (regno - 224) : (regno + 48);
}

/* End of target macro support functions.

   Now the MMIX port's own functions.  First the exported ones.  */

/* Wrapper for get_hard_reg_initial_val since integrate.h isn't included
   from insn-emit.c.  */

rtx
mmix_get_hard_reg_initial_val (enum machine_mode mode, int regno)
{
  return get_hard_reg_initial_val (mode, regno);
}

/* Nonzero when the function epilogue is simple enough that a single
   "POP %d,0" should be used even within the function.  */

int
mmix_use_simple_return (void)
{
  int regno;

  int stack_space_to_allocate
    = (current_function_outgoing_args_size
       + current_function_pretend_args_size
       + get_frame_size () + 7) & ~7;

  if (!TARGET_USE_RETURN_INSN || !reload_completed)
    return 0;

  for (regno = 255;
       regno >= MMIX_FIRST_GLOBAL_REGNUM;
       regno--)
    /* Note that we assume that the frame-pointer-register is one of these
       registers, in which case we don't count it here.  */
    if ((((regno != MMIX_FRAME_POINTER_REGNUM || !frame_pointer_needed)
	  && regs_ever_live[regno] && !call_used_regs[regno]))
	|| IS_MMIX_EH_RETURN_DATA_REG (regno))
      return 0;

  if (frame_pointer_needed)
    stack_space_to_allocate += 8;

  if (MMIX_CFUN_HAS_LANDING_PAD)
    stack_space_to_allocate += 16;
  else if (MMIX_CFUN_NEEDS_SAVED_EH_RETURN_ADDRESS)
    stack_space_to_allocate += 8;

  return stack_space_to_allocate == 0;
}


/* Expands the function prologue into RTX.  */

void
mmix_expand_prologue (void)
{
  HOST_WIDE_INT locals_size = get_frame_size ();
  int regno;
  HOST_WIDE_INT stack_space_to_allocate
    = (current_function_outgoing_args_size
       + current_function_pretend_args_size
       + locals_size + 7) & ~7;
  HOST_WIDE_INT offset = -8;

  /* Add room needed to save global non-register-stack registers.  */
  for (regno = 255;
       regno >= MMIX_FIRST_GLOBAL_REGNUM;
       regno--)
    /* Note that we assume that the frame-pointer-register is one of these
       registers, in which case we don't count it here.  */
    if ((((regno != MMIX_FRAME_POINTER_REGNUM || !frame_pointer_needed)
	  && regs_ever_live[regno] && !call_used_regs[regno]))
	|| IS_MMIX_EH_RETURN_DATA_REG (regno))
      stack_space_to_allocate += 8;

  /* If we do have a frame-pointer, add room for it.  */
  if (frame_pointer_needed)
    stack_space_to_allocate += 8;

  /* If we have a non-local label, we need to be able to unwind to it, so
     store the current register stack pointer.  Also store the return
     address if we do that.  */
  if (MMIX_CFUN_HAS_LANDING_PAD)
    stack_space_to_allocate += 16;
  else if (MMIX_CFUN_NEEDS_SAVED_EH_RETURN_ADDRESS)
    /* If we do have a saved return-address slot, add room for it.  */
    stack_space_to_allocate += 8;

  /* Make sure we don't get an unaligned stack.  */
  if ((stack_space_to_allocate % 8) != 0)
    internal_error ("stack frame not a multiple of 8 bytes: %wd",
		    stack_space_to_allocate);

  if (current_function_pretend_args_size)
    {
      int mmix_first_vararg_reg
	= (MMIX_FIRST_INCOMING_ARG_REGNUM
	   + (MMIX_MAX_ARGS_IN_REGS
	      - current_function_pretend_args_size / 8));

      for (regno
	     = MMIX_FIRST_INCOMING_ARG_REGNUM + MMIX_MAX_ARGS_IN_REGS - 1;
	   regno >= mmix_first_vararg_reg;
	   regno--)
	{
	  if (offset < 0)
	    {
	      HOST_WIDE_INT stack_chunk
		= stack_space_to_allocate > (256 - 8)
		? (256 - 8) : stack_space_to_allocate;

	      mmix_emit_sp_add (-stack_chunk);
	      offset += stack_chunk;
	      stack_space_to_allocate -= stack_chunk;
	    }

	  /* These registers aren't actually saved (as in "will be
	     restored"), so don't tell DWARF2 they're saved.  */
	  emit_move_insn (gen_rtx_MEM (DImode,
				       plus_constant (stack_pointer_rtx,
						      offset)),
			  gen_rtx_REG (DImode, regno));
	  offset -= 8;
	}
    }

  /* Store the frame-pointer.  */

  if (frame_pointer_needed)
    {
      rtx insn;

      if (offset < 0)
	{
	  /* Get 8 less than otherwise, since we need to reach offset + 8.  */
	  HOST_WIDE_INT stack_chunk
	    = stack_space_to_allocate > (256 - 8 - 8)
	    ? (256 - 8 - 8) : stack_space_to_allocate;

	  mmix_emit_sp_add (-stack_chunk);

	  offset += stack_chunk;
	  stack_space_to_allocate -= stack_chunk;
	}

      insn = emit_move_insn (gen_rtx_MEM (DImode,
					  plus_constant (stack_pointer_rtx,
							 offset)),
			     hard_frame_pointer_rtx);
      RTX_FRAME_RELATED_P (insn) = 1;
      insn = emit_insn (gen_adddi3 (hard_frame_pointer_rtx,
				    stack_pointer_rtx,
				    GEN_INT (offset + 8)));
      RTX_FRAME_RELATED_P (insn) = 1;
      offset -= 8;
    }

  if (MMIX_CFUN_NEEDS_SAVED_EH_RETURN_ADDRESS)
    {
      rtx tmpreg, retreg;
      rtx insn;

      /* Store the return-address, if one is needed on the stack.  We
	 usually store it in a register when needed, but that doesn't work
	 with -fexceptions.  */

      if (offset < 0)
	{
	  /* Get 8 less than otherwise, since we need to reach offset + 8.  */
	  HOST_WIDE_INT stack_chunk
	    = stack_space_to_allocate > (256 - 8 - 8)
	    ? (256 - 8 - 8) : stack_space_to_allocate;

	  mmix_emit_sp_add (-stack_chunk);

	  offset += stack_chunk;
	  stack_space_to_allocate -= stack_chunk;
	}

      tmpreg = gen_rtx_REG (DImode, 255);
      retreg = gen_rtx_REG (DImode, MMIX_rJ_REGNUM);

      /* Dwarf2 code is confused by the use of a temporary register for
	 storing the return address, so we have to express it as a note,
	 which we attach to the actual store insn.  */
      emit_move_insn (tmpreg, retreg);

      insn = emit_move_insn (gen_rtx_MEM (DImode,
					  plus_constant (stack_pointer_rtx,
							 offset)),
			     tmpreg);
      RTX_FRAME_RELATED_P (insn) = 1;
      REG_NOTES (insn)
	= gen_rtx_EXPR_LIST (REG_FRAME_RELATED_EXPR,
			     gen_rtx_SET (VOIDmode,
					  gen_rtx_MEM (DImode,
						       plus_constant (stack_pointer_rtx,
								      offset)),
					  retreg),
			     REG_NOTES (insn));

      offset -= 8;
    }
  else if (MMIX_CFUN_HAS_LANDING_PAD)
    offset -= 8;

  if (MMIX_CFUN_HAS_LANDING_PAD)
    {
      /* Store the register defining the numbering of local registers, so
	 we know how long to unwind the register stack.  */

      if (offset < 0)
	{
	  /* Get 8 less than otherwise, since we need to reach offset + 8.  */
	  HOST_WIDE_INT stack_chunk
	    = stack_space_to_allocate > (256 - 8 - 8)
	    ? (256 - 8 - 8) : stack_space_to_allocate;

	  mmix_emit_sp_add (-stack_chunk);

	  offset += stack_chunk;
	  stack_space_to_allocate -= stack_chunk;
	}

      /* We don't tell dwarf2 about this one; we just have it to unwind
	 the register stack at landing pads.  FIXME: It's a kludge because
	 we can't describe the effect of the PUSHJ and PUSHGO insns on the
	 register stack at the moment.  Best thing would be to handle it
	 like stack-pointer offsets.  Better: some hook into dwarf2out.c
	 to produce DW_CFA_expression:s that specify the increment of rO,
	 and unwind it at eh_return (preferred) or at the landing pad.
	 Then saves to $0..$G-1 could be specified through that register.  */

      emit_move_insn (gen_rtx_REG (DImode, 255),
		      gen_rtx_REG (DImode,
				   MMIX_rO_REGNUM));
      emit_move_insn (gen_rtx_MEM (DImode,
				   plus_constant (stack_pointer_rtx, offset)),
		      gen_rtx_REG (DImode, 255));
      offset -= 8;
    }

  /* After the return-address and the frame-pointer, we have the local
     variables.  They're the ones that may have an "unaligned" size.  */
  offset -= (locals_size + 7) & ~7;

  /* Now store all registers that are global, i.e. not saved by the
     register file machinery.

     It is assumed that the frame-pointer is one of these registers, so it
     is explicitly excluded in the count.  */

  for (regno = 255;
       regno >= MMIX_FIRST_GLOBAL_REGNUM;
       regno--)
    if (((regno != MMIX_FRAME_POINTER_REGNUM || !frame_pointer_needed)
	 && regs_ever_live[regno] && ! call_used_regs[regno])
	|| IS_MMIX_EH_RETURN_DATA_REG (regno))
      {
	rtx insn;

	if (offset < 0)
	  {
	    HOST_WIDE_INT stack_chunk
	      = (stack_space_to_allocate > (256 - offset - 8)
		 ? (256 - offset - 8) : stack_space_to_allocate);

	    mmix_emit_sp_add (-stack_chunk);
	    offset += stack_chunk;
	    stack_space_to_allocate -= stack_chunk;
	  }

	insn = emit_move_insn (gen_rtx_MEM (DImode,
					    plus_constant (stack_pointer_rtx,
							   offset)),
			       gen_rtx_REG (DImode, regno));
	RTX_FRAME_RELATED_P (insn) = 1;
	offset -= 8;
      }

  /* Finally, allocate room for outgoing args and local vars if room
     wasn't allocated above.  */
  if (stack_space_to_allocate)
    mmix_emit_sp_add (-stack_space_to_allocate);
}

/* Expands the function epilogue into RTX.  */

void
mmix_expand_epilogue (void)
{
  HOST_WIDE_INT locals_size = get_frame_size ();
  int regno;
  HOST_WIDE_INT stack_space_to_deallocate
    = (current_function_outgoing_args_size
       + current_function_pretend_args_size
       + locals_size + 7) & ~7;

  /* The first address to access is beyond the outgoing_args area.  */
  HOST_WIDE_INT offset = current_function_outgoing_args_size;

  /* Add the space for global non-register-stack registers.
     It is assumed that the frame-pointer register can be one of these
     registers, in which case it is excluded from the count when needed.  */
  for (regno = 255;
       regno >= MMIX_FIRST_GLOBAL_REGNUM;
       regno--)
    if (((regno != MMIX_FRAME_POINTER_REGNUM || !frame_pointer_needed)
	 && regs_ever_live[regno] && !call_used_regs[regno])
	|| IS_MMIX_EH_RETURN_DATA_REG (regno))
      stack_space_to_deallocate += 8;

  /* Add in the space for register stack-pointer.  If so, always add room
     for the saved PC.  */
  if (MMIX_CFUN_HAS_LANDING_PAD)
    stack_space_to_deallocate += 16;
  else if (MMIX_CFUN_NEEDS_SAVED_EH_RETURN_ADDRESS)
    /* If we have a saved return-address slot, add it in.  */
    stack_space_to_deallocate += 8;

  /* Add in the frame-pointer.  */
  if (frame_pointer_needed)
    stack_space_to_deallocate += 8;

  /* Make sure we don't get an unaligned stack.  */
  if ((stack_space_to_deallocate % 8) != 0)
    internal_error ("stack frame not a multiple of octabyte: %wd",
		    stack_space_to_deallocate);

  /* We will add back small offsets to the stack pointer as we go.
     First, we restore all registers that are global, i.e. not saved by
     the register file machinery.  */

  for (regno = MMIX_FIRST_GLOBAL_REGNUM;
       regno <= 255;
       regno++)
    if (((regno != MMIX_FRAME_POINTER_REGNUM || !frame_pointer_needed)
	 && regs_ever_live[regno] && !call_used_regs[regno])
	|| IS_MMIX_EH_RETURN_DATA_REG (regno))
      {
	if (offset > 255)
	  {
	    mmix_emit_sp_add (offset);
	    stack_space_to_deallocate -= offset;
	    offset = 0;
	  }

	emit_move_insn (gen_rtx_REG (DImode, regno),
			gen_rtx_MEM (DImode,
				     plus_constant (stack_pointer_rtx,
						    offset)));
	offset += 8;
      }

  /* Here is where the local variables were.  As in the prologue, they
     might be of an unaligned size.  */
  offset += (locals_size + 7) & ~7;

  /* The saved register stack pointer is just below the frame-pointer
     register.  We don't need to restore it "manually"; the POP
     instruction does that.  */
  if (MMIX_CFUN_HAS_LANDING_PAD)
    offset += 16;
  else if (MMIX_CFUN_NEEDS_SAVED_EH_RETURN_ADDRESS)
    /* The return-address slot is just below the frame-pointer register.
       We don't need to restore it because we don't really use it.  */
    offset += 8;

  /* Get back the old frame-pointer-value.  */
  if (frame_pointer_needed)
    {
      if (offset > 255)
	{
	  mmix_emit_sp_add (offset);

	  stack_space_to_deallocate -= offset;
	  offset = 0;
	}

      emit_move_insn (hard_frame_pointer_rtx,
		      gen_rtx_MEM (DImode,
				   plus_constant (stack_pointer_rtx,
						  offset)));
      offset += 8;
    }

  /* We do not need to restore pretended incoming args, just add back
     offset to sp.  */
  if (stack_space_to_deallocate != 0)
    mmix_emit_sp_add (stack_space_to_deallocate);

  if (current_function_calls_eh_return)
    /* Adjust the (normal) stack-pointer to that of the receiver.
       FIXME: It would be nice if we could also adjust the register stack
       here, but we need to express it through DWARF 2 too.  */
    emit_insn (gen_adddi3 (stack_pointer_rtx, stack_pointer_rtx,
			   gen_rtx_REG (DImode,
					MMIX_EH_RETURN_STACKADJ_REGNUM)));
}

/* Output an optimal sequence for setting a register to a specific
   constant.  Used in an alternative for const_ints in movdi, and when
   using large stack-frame offsets.

   Use do_begin_end to say if a line-starting TAB and newline before the
   first insn and after the last insn is wanted.  */

void
mmix_output_register_setting (FILE *stream,
			      int regno,
			      HOST_WIDEST_INT value,
			      int do_begin_end)
{
  if (do_begin_end)
    fprintf (stream, "\t");

  if (mmix_shiftable_wyde_value ((unsigned HOST_WIDEST_INT) value))
    {
      /* First, the one-insn cases.  */
      mmix_output_shiftvalue_op_from_str (stream, "SET",
					  (unsigned HOST_WIDEST_INT)
					  value);
      fprintf (stream, " %s,", reg_names[regno]);
      mmix_output_shifted_value (stream, (unsigned HOST_WIDEST_INT) value);
    }
  else if (mmix_shiftable_wyde_value (-(unsigned HOST_WIDEST_INT) value))
    {
      /* We do this to get a bit more legible assembly code.  The next
	 alternative is mostly redundant with this.  */

      mmix_output_shiftvalue_op_from_str (stream, "SET",
					  -(unsigned HOST_WIDEST_INT)
					  value);
      fprintf (stream, " %s,", reg_names[regno]);
      mmix_output_shifted_value (stream, -(unsigned HOST_WIDEST_INT) value);
      fprintf (stream, "\n\tNEGU %s,0,%s", reg_names[regno],
	       reg_names[regno]);
    }
  else if (mmix_shiftable_wyde_value (~(unsigned HOST_WIDEST_INT) value))
    {
      /* Slightly more expensive, the two-insn cases.  */

      /* FIXME: We could of course also test if 0..255-N or ~(N | 1..255)
	 is shiftable, or any other one-insn transformation of the value.
	 FIXME: Check first if the value is "shiftable" by two loading
	 with two insns, since it makes more readable assembly code (if
	 anyone else cares).  */

      mmix_output_shiftvalue_op_from_str (stream, "SET",
					  ~(unsigned HOST_WIDEST_INT)
					  value);
      fprintf (stream, " %s,", reg_names[regno]);
      mmix_output_shifted_value (stream, ~(unsigned HOST_WIDEST_INT) value);
      fprintf (stream, "\n\tNOR %s,%s,0", reg_names[regno],
	       reg_names[regno]);
    }
  else
    {
      /* The generic case.  2..4 insns.  */
      static const char *const higher_parts[] = {"L", "ML", "MH", "H"};
      const char *op = "SET";
      const char *line_begin = "";
      int insns = 0;
      int i;
      HOST_WIDEST_INT tmpvalue = value;

      /* Compute the number of insns needed to output this constant.  */
      for (i = 0; i < 4 && tmpvalue != 0; i++)
	{
	  if (tmpvalue & 65535)
	    insns++;
	  tmpvalue >>= 16;
	}
      if (TARGET_BASE_ADDRESSES && insns == 3)
	{
	  /* The number three is based on a static observation on
	     ghostscript-6.52.  Two and four are excluded because there
	     are too many such constants, and each unique constant (maybe
	     offset by 1..255) were used few times compared to other uses,
	     e.g. addresses.

	     We use base-plus-offset addressing to force it into a global
	     register; we just use a "LDA reg,VALUE", which will cause the
	     assembler and linker to DTRT (for constants as well as
	     addresses).  */
	  fprintf (stream, "LDA %s,", reg_names[regno]);
	  mmix_output_octa (stream, value, 0);
	}
      else
	{
	  /* Output pertinent parts of the 4-wyde sequence.
	     Still more to do if we want this to be optimal, but hey...
	     Note that the zero case has been handled above.  */
	  for (i = 0; i < 4 && value != 0; i++)
	    {
	      if (value & 65535)
		{
		  fprintf (stream, "%s%s%s %s,#%x", line_begin, op,
			   higher_parts[i], reg_names[regno],
			   (int) (value & 65535));
		  /* The first one sets the rest of the bits to 0, the next
		     ones add set bits.  */
		  op = "INC";
		  line_begin = "\n\t";
		}

	      value >>= 16;
	    }
	}
    }

  if (do_begin_end)
    fprintf (stream, "\n");
}

/* Return 1 if value is 0..65535*2**(16*N) for N=0..3.
   else return 0.  */

int
mmix_shiftable_wyde_value (unsigned HOST_WIDEST_INT value)
{
  /* Shift by 16 bits per group, stop when we've found two groups with
     nonzero bits.  */
  int i;
  int has_candidate = 0;

  for (i = 0; i < 4; i++)
    {
      if (value & 65535)
	{
	  if (has_candidate)
	    return 0;
	  else
	    has_candidate = 1;
	}

      value >>= 16;
    }

  return 1;
}

/* Returns zero if code and mode is not a valid condition from a
   compare-type insn.  Nonzero if it is.  The parameter op, if non-NULL,
   is the comparison of mode is CC-somethingmode.  */

int
mmix_valid_comparison (RTX_CODE code, enum machine_mode mode, rtx op)
{
  if (mode == VOIDmode && op != NULL_RTX)
    mode = GET_MODE (op);

  /* We don't care to look at these, they should always be valid.  */
  if (mode == CCmode || mode == CC_UNSmode || mode == DImode)
    return 1;

  if ((mode == CC_FPmode || mode == DFmode)
      && (code == GT || code == LT))
    return 1;

  if ((mode == CC_FPEQmode || mode == DFmode)
      && (code == EQ || code == NE))
    return 1;

  if ((mode == CC_FUNmode || mode == DFmode)
      && (code == ORDERED || code == UNORDERED))
    return 1;

  return 0;
}

/* X and Y are two things to compare using CODE.  Emit a compare insn if
   possible and return the rtx for the cc-reg in the proper mode, or
   NULL_RTX if this is not a valid comparison.  */

rtx
mmix_gen_compare_reg (RTX_CODE code, rtx x, rtx y)
{
  enum machine_mode ccmode = SELECT_CC_MODE (code, x, y);
  rtx cc_reg;

  /* FIXME: Do we get constants here?  Of double mode?  */
  enum machine_mode mode
    = GET_MODE (x) == VOIDmode
    ? GET_MODE (y)
    : GET_MODE_CLASS (GET_MODE (x)) == MODE_FLOAT ? DFmode : DImode;

  if (! mmix_valid_comparison (code, mode, x))
    return NULL_RTX;

  cc_reg = gen_reg_rtx (ccmode);

  /* FIXME:  Can we avoid emitting a compare insn here?  */
  if (! REG_P (x) && ! REG_P (y))
    x = force_reg (mode, x);

  /* If it's not quite right yet, put y in a register.  */
  if (! REG_P (y)
      && (GET_CODE (y) != CONST_INT
	  || ! CONST_OK_FOR_LETTER_P (INTVAL (y), 'I')))
    y = force_reg (mode, y);

  emit_insn (gen_rtx_SET (VOIDmode, cc_reg,
			  gen_rtx_COMPARE (ccmode, x, y)));

  return cc_reg;
}

/* Local (static) helper functions.  */

static void
mmix_emit_sp_add (HOST_WIDE_INT offset)
{
  rtx insn;

  if (offset < 0)
    {
      /* Negative stack-pointer adjustments are allocations and appear in
	 the prologue only.  We mark them as frame-related so unwind and
	 debug info is properly emitted for them.  */
      if (offset > -255)
	insn = emit_insn (gen_adddi3 (stack_pointer_rtx,
				      stack_pointer_rtx,
				      GEN_INT (offset)));
      else
	{
	  rtx tmpr = gen_rtx_REG (DImode, 255);
	  RTX_FRAME_RELATED_P (emit_move_insn (tmpr, GEN_INT (offset))) = 1;
	  insn = emit_insn (gen_adddi3 (stack_pointer_rtx,
					stack_pointer_rtx, tmpr));
	}
      RTX_FRAME_RELATED_P (insn) = 1;
    }
  else
    {
      /* Positive adjustments are in the epilogue only.  Don't mark them
	 as "frame-related" for unwind info.  */
      if (CONST_OK_FOR_LETTER_P (offset, 'L'))
	emit_insn (gen_adddi3 (stack_pointer_rtx,
			       stack_pointer_rtx,
			       GEN_INT (offset)));
      else
	{
	  rtx tmpr = gen_rtx_REG (DImode, 255);
	  emit_move_insn (tmpr, GEN_INT (offset));
	  insn = emit_insn (gen_adddi3 (stack_pointer_rtx,
					stack_pointer_rtx, tmpr));
	}
    }
}

/* Print operator suitable for doing something with a shiftable
   wyde.  The type of operator is passed as an asm output modifier.  */

static void
mmix_output_shiftvalue_op_from_str (FILE *stream,
				    const char *mainop,
				    HOST_WIDEST_INT value)
{
  static const char *const op_part[] = {"L", "ML", "MH", "H"};
  int i;

  if (! mmix_shiftable_wyde_value (value))
    {
      char s[sizeof ("0xffffffffffffffff")];
      sprintf (s, HOST_WIDEST_INT_PRINT_HEX, value);
      internal_error ("MMIX Internal: %s is not a shiftable int", s);
    }

  for (i = 0; i < 4; i++)
    {
      /* We know we're through when we find one-bits in the low
	 16 bits.  */
      if (value & 0xffff)
	{
	  fprintf (stream, "%s%s", mainop, op_part[i]);
	  return;
	}
      value >>= 16;
    }

  /* No bits set?  Then it must have been zero.  */
  fprintf (stream, "%sL", mainop);
}

/* Print a 64-bit value, optionally prefixed by assembly pseudo.  */

static void
mmix_output_octa (FILE *stream, HOST_WIDEST_INT value, int do_begin_end)
{
  /* Snipped from final.c:output_addr_const.  We need to avoid the
     presumed universal "0x" prefix.  We can do it by replacing "0x" with
     "#0" here; we must avoid a space in the operands and no, the zero
     won't cause the number to be assumed in octal format.  */
  char hex_format[sizeof (HOST_WIDEST_INT_PRINT_HEX)];

  if (do_begin_end)
    fprintf (stream, "\tOCTA ");

  strcpy (hex_format, HOST_WIDEST_INT_PRINT_HEX);
  hex_format[0] = '#';
  hex_format[1] = '0';

  /* Provide a few alternative output formats depending on the number, to
     improve legibility of assembler output.  */
  if ((value < (HOST_WIDEST_INT) 0 && value > (HOST_WIDEST_INT) -10000)
      || (value >= (HOST_WIDEST_INT) 0 && value <= (HOST_WIDEST_INT) 16384))
    fprintf (stream, "%d", (int) value);
  else if (value > (HOST_WIDEST_INT) 0
	   && value < ((HOST_WIDEST_INT) 1 << 31) * 2)
    fprintf (stream, "#%x", (unsigned int) value);
  else
    fprintf (stream, hex_format, value);

  if (do_begin_end)
    fprintf (stream, "\n");
}

/* Print the presumed shiftable wyde argument shifted into place (to
   be output with an operand).  */

static void
mmix_output_shifted_value (FILE *stream, HOST_WIDEST_INT value)
{
  int i;

  if (! mmix_shiftable_wyde_value (value))
    {
      char s[16+2+1];
      sprintf (s, HOST_WIDEST_INT_PRINT_HEX, value);
      internal_error ("MMIX Internal: %s is not a shiftable int", s);
    }

  for (i = 0; i < 4; i++)
    {
      /* We know we're through when we find one-bits in the low 16 bits.  */
      if (value & 0xffff)
	{
	  fprintf (stream, "#%x", (int) (value & 0xffff));
	  return;
	}

    value >>= 16;
  }

  /* No bits set?  Then it must have been zero.  */
  fprintf (stream, "0");
}

/* Output an MMIX condition name corresponding to an operator
   and operands:
   (comparison_operator [(comparison_operator ...) (const_int 0)])
   which means we have to look at *two* operators.

   The argument "reversed" refers to reversal of the condition (not the
   same as swapping the arguments).  */

static void
mmix_output_condition (FILE *stream, rtx x, int reversed)
{
  struct cc_conv
  {
    RTX_CODE cc;

    /* The normal output cc-code.  */
    const char *const normal;

    /* The reversed cc-code, or NULL if invalid.  */
    const char *const reversed;
  };

  struct cc_type_conv
  {
    enum machine_mode cc_mode;

    /* Terminated with {UNKNOWN, NULL, NULL} */
    const struct cc_conv *const convs;
  };

#undef CCEND
#define CCEND {UNKNOWN, NULL, NULL}

  static const struct cc_conv cc_fun_convs[]
    = {{ORDERED, "Z", "P"},
       {UNORDERED, "P", "Z"},
       CCEND};
  static const struct cc_conv cc_fp_convs[]
    = {{GT, "P", NULL},
       {LT, "N", NULL},
       CCEND};
  static const struct cc_conv cc_fpeq_convs[]
    = {{NE, "Z", "P"},
       {EQ, "P", "Z"},
       CCEND};
  static const struct cc_conv cc_uns_convs[]
    = {{GEU, "NN", "N"},
       {GTU, "P", "NP"},
       {LEU, "NP", "P"},
       {LTU, "N", "NN"},
       CCEND};
  static const struct cc_conv cc_signed_convs[]
    = {{NE, "NZ", "Z"},
       {EQ, "Z", "NZ"},
       {GE, "NN", "N"},
       {GT, "P", "NP"},
       {LE, "NP", "P"},
       {LT, "N", "NN"},
       CCEND};
  static const struct cc_conv cc_di_convs[]
    = {{NE, "NZ", "Z"},
       {EQ, "Z", "NZ"},
       {GE, "NN", "N"},
       {GT, "P", "NP"},
       {LE, "NP", "P"},
       {LT, "N", "NN"},
       {GTU, "NZ", "Z"},
       {LEU, "Z", "NZ"},
       CCEND};
#undef CCEND

  static const struct cc_type_conv cc_convs[]
    = {{CC_FUNmode, cc_fun_convs},
       {CC_FPmode, cc_fp_convs},
       {CC_FPEQmode, cc_fpeq_convs},
       {CC_UNSmode, cc_uns_convs},
       {CCmode, cc_signed_convs},
       {DImode, cc_di_convs}};

  size_t i;
  int j;

  enum machine_mode mode = GET_MODE (XEXP (x, 0));
  RTX_CODE cc = GET_CODE (x);

  for (i = 0; i < ARRAY_SIZE (cc_convs); i++)
    {
      if (mode == cc_convs[i].cc_mode)
	{
	  for (j = 0; cc_convs[i].convs[j].cc != UNKNOWN; j++)
	    if (cc == cc_convs[i].convs[j].cc)
	      {
		const char *mmix_cc
		  = (reversed ? cc_convs[i].convs[j].reversed
		     : cc_convs[i].convs[j].normal);

		if (mmix_cc == NULL)
		  fatal_insn ("MMIX Internal: Trying to output invalidly\
 reversed condition:", x);

		fprintf (stream, "%s", mmix_cc);
		return;
	      }

	  fatal_insn ("MMIX Internal: What's the CC of this?", x);
	}
    }

  fatal_insn ("MMIX Internal: What is the CC of this?", x);
}

/* Return the bit-value for a const_int or const_double.  */

static HOST_WIDEST_INT
mmix_intval (rtx x)
{
  unsigned HOST_WIDEST_INT retval;

  if (GET_CODE (x) == CONST_INT)
    return INTVAL (x);

  /* We make a little song and dance because converting to long long in
     gcc-2.7.2 is broken.  I still want people to be able to use it for
     cross-compilation to MMIX.  */
  if (GET_CODE (x) == CONST_DOUBLE && GET_MODE (x) == VOIDmode)
    {
      if (sizeof (HOST_WIDE_INT) < sizeof (HOST_WIDEST_INT))
	{
	  retval = (unsigned) CONST_DOUBLE_LOW (x) / 2;
	  retval *= 2;
	  retval |= CONST_DOUBLE_LOW (x) & 1;

	  retval |=
	    (unsigned HOST_WIDEST_INT) CONST_DOUBLE_HIGH (x)
	      << (HOST_BITS_PER_LONG);
	}
      else
	retval = CONST_DOUBLE_HIGH (x);

      return retval;
    }

  if (GET_CODE (x) == CONST_DOUBLE)
    {
      REAL_VALUE_TYPE value;

      /* FIXME:  This macro is not in the manual but should be.  */
      REAL_VALUE_FROM_CONST_DOUBLE (value, x);

      if (GET_MODE (x) == DFmode)
	{
	  long bits[2];

	  REAL_VALUE_TO_TARGET_DOUBLE (value, bits);

	  /* The double cast is necessary to avoid getting the long
	     sign-extended to unsigned long long(!) when they're of
	     different size (usually 32-bit hosts).  */
	  return
	    ((unsigned HOST_WIDEST_INT) (unsigned long) bits[0]
	     << (unsigned HOST_WIDEST_INT) 32U)
	    | (unsigned HOST_WIDEST_INT) (unsigned long) bits[1];
	}
      else if (GET_MODE (x) == SFmode)
	{
	  long bits;
	  REAL_VALUE_TO_TARGET_SINGLE (value, bits);

	  return (unsigned long) bits;
	}
    }

  fatal_insn ("MMIX Internal: This is not a constant:", x);
}

/* Worker function for TARGET_STRUCT_VALUE_RTX.  */

static rtx
mmix_struct_value_rtx (tree fntype ATTRIBUTE_UNUSED,
		       int incoming ATTRIBUTE_UNUSED)
{
  return gen_rtx_REG (Pmode, MMIX_STRUCT_VALUE_REGNUM);
}

/*
 * Local variables:
 * eval: (c-set-style "gnu")
 * indent-tabs-mode: t
 * End:
 */
