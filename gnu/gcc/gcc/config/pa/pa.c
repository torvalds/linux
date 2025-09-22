/* Subroutines for insn-output.c for HPPA.
   Copyright (C) 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001,
   2002, 2003, 2004, 2005, 2006 Free Software Foundation, Inc.
   Contributed by Tim Moore (moore@cs.utah.edu), based on sparc.c

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
#include "real.h"
#include "insn-config.h"
#include "conditions.h"
#include "insn-attr.h"
#include "flags.h"
#include "tree.h"
#include "output.h"
#include "except.h"
#include "expr.h"
#include "optabs.h"
#include "reload.h"
#include "integrate.h"
#include "function.h"
#include "toplev.h"
#include "ggc.h"
#include "recog.h"
#include "predict.h"
#include "tm_p.h"
#include "target.h"
#include "target-def.h"

/* Return nonzero if there is a bypass for the output of 
   OUT_INSN and the fp store IN_INSN.  */
int
hppa_fpstore_bypass_p (rtx out_insn, rtx in_insn)
{
  enum machine_mode store_mode;
  enum machine_mode other_mode;
  rtx set;

  if (recog_memoized (in_insn) < 0
      || get_attr_type (in_insn) != TYPE_FPSTORE
      || recog_memoized (out_insn) < 0)
    return 0;

  store_mode = GET_MODE (SET_SRC (PATTERN (in_insn)));

  set = single_set (out_insn);
  if (!set)
    return 0;

  other_mode = GET_MODE (SET_SRC (set));

  return (GET_MODE_SIZE (store_mode) == GET_MODE_SIZE (other_mode));
}
  

#ifndef DO_FRAME_NOTES
#ifdef INCOMING_RETURN_ADDR_RTX
#define DO_FRAME_NOTES 1
#else
#define DO_FRAME_NOTES 0
#endif
#endif

static void copy_reg_pointer (rtx, rtx);
static void fix_range (const char *);
static bool pa_handle_option (size_t, const char *, int);
static int hppa_address_cost (rtx);
static bool hppa_rtx_costs (rtx, int, int, int *);
static inline rtx force_mode (enum machine_mode, rtx);
static void pa_reorg (void);
static void pa_combine_instructions (void);
static int pa_can_combine_p (rtx, rtx, rtx, int, rtx, rtx, rtx);
static int forward_branch_p (rtx);
static void compute_zdepwi_operands (unsigned HOST_WIDE_INT, unsigned *);
static int compute_movmem_length (rtx);
static int compute_clrmem_length (rtx);
static bool pa_assemble_integer (rtx, unsigned int, int);
static void remove_useless_addtr_insns (int);
static void store_reg (int, HOST_WIDE_INT, int);
static void store_reg_modify (int, int, HOST_WIDE_INT);
static void load_reg (int, HOST_WIDE_INT, int);
static void set_reg_plus_d (int, int, HOST_WIDE_INT, int);
static void pa_output_function_prologue (FILE *, HOST_WIDE_INT);
static void update_total_code_bytes (int);
static void pa_output_function_epilogue (FILE *, HOST_WIDE_INT);
static int pa_adjust_cost (rtx, rtx, rtx, int);
static int pa_adjust_priority (rtx, int);
static int pa_issue_rate (void);
static void pa_som_asm_init_sections (void) ATTRIBUTE_UNUSED;
static section *pa_select_section (tree, int, unsigned HOST_WIDE_INT)
     ATTRIBUTE_UNUSED;
static void pa_encode_section_info (tree, rtx, int);
static const char *pa_strip_name_encoding (const char *);
static bool pa_function_ok_for_sibcall (tree, tree);
static void pa_globalize_label (FILE *, const char *)
     ATTRIBUTE_UNUSED;
static void pa_asm_output_mi_thunk (FILE *, tree, HOST_WIDE_INT,
				    HOST_WIDE_INT, tree);
#if !defined(USE_COLLECT2)
static void pa_asm_out_constructor (rtx, int);
static void pa_asm_out_destructor (rtx, int);
#endif
static void pa_init_builtins (void);
static rtx hppa_builtin_saveregs (void);
static tree hppa_gimplify_va_arg_expr (tree, tree, tree *, tree *);
static bool pa_scalar_mode_supported_p (enum machine_mode);
static bool pa_commutative_p (rtx x, int outer_code);
static void copy_fp_args (rtx) ATTRIBUTE_UNUSED;
static int length_fp_args (rtx) ATTRIBUTE_UNUSED;
static inline void pa_file_start_level (void) ATTRIBUTE_UNUSED;
static inline void pa_file_start_space (int) ATTRIBUTE_UNUSED;
static inline void pa_file_start_file (int) ATTRIBUTE_UNUSED;
static inline void pa_file_start_mcount (const char*) ATTRIBUTE_UNUSED;
static void pa_elf_file_start (void) ATTRIBUTE_UNUSED;
static void pa_som_file_start (void) ATTRIBUTE_UNUSED;
static void pa_linux_file_start (void) ATTRIBUTE_UNUSED;
static void pa_hpux64_gas_file_start (void) ATTRIBUTE_UNUSED;
static void pa_hpux64_hpas_file_start (void) ATTRIBUTE_UNUSED;
static void output_deferred_plabels (void);
static void output_deferred_profile_counters (void) ATTRIBUTE_UNUSED;
#ifdef ASM_OUTPUT_EXTERNAL_REAL
static void pa_hpux_file_end (void);
#endif
#ifdef HPUX_LONG_DOUBLE_LIBRARY
static void pa_hpux_init_libfuncs (void);
#endif
static rtx pa_struct_value_rtx (tree, int);
static bool pa_pass_by_reference (CUMULATIVE_ARGS *, enum machine_mode,
				  tree, bool);
static int pa_arg_partial_bytes (CUMULATIVE_ARGS *, enum machine_mode,
				 tree, bool);
static struct machine_function * pa_init_machine_status (void);
static enum reg_class pa_secondary_reload (bool, rtx, enum reg_class,
					   enum machine_mode,
					   secondary_reload_info *);


/* The following extra sections are only used for SOM.  */
static GTY(()) section *som_readonly_data_section;
static GTY(()) section *som_one_only_readonly_data_section;
static GTY(()) section *som_one_only_data_section;

/* Save the operands last given to a compare for use when we
   generate a scc or bcc insn.  */
rtx hppa_compare_op0, hppa_compare_op1;
enum cmp_type hppa_branch_type;

/* Which cpu we are scheduling for.  */
enum processor_type pa_cpu = TARGET_SCHED_DEFAULT;

/* The UNIX standard to use for predefines and linking.  */
int flag_pa_unix = TARGET_HPUX_11_11 ? 1998 : TARGET_HPUX_10_10 ? 1995 : 1993;

/* Counts for the number of callee-saved general and floating point
   registers which were saved by the current function's prologue.  */
static int gr_saved, fr_saved;

static rtx find_addr_reg (rtx);

/* Keep track of the number of bytes we have output in the CODE subspace
   during this compilation so we'll know when to emit inline long-calls.  */
unsigned long total_code_bytes;

/* The last address of the previous function plus the number of bytes in
   associated thunks that have been output.  This is used to determine if
   a thunk can use an IA-relative branch to reach its target function.  */
static int last_address;

/* Variables to handle plabels that we discover are necessary at assembly
   output time.  They are output after the current function.  */
struct deferred_plabel GTY(())
{
  rtx internal_label;
  rtx symbol;
};
static GTY((length ("n_deferred_plabels"))) struct deferred_plabel *
  deferred_plabels;
static size_t n_deferred_plabels = 0;


/* Initialize the GCC target structure.  */

#undef TARGET_ASM_ALIGNED_HI_OP
#define TARGET_ASM_ALIGNED_HI_OP "\t.half\t"
#undef TARGET_ASM_ALIGNED_SI_OP
#define TARGET_ASM_ALIGNED_SI_OP "\t.word\t"
#undef TARGET_ASM_ALIGNED_DI_OP
#define TARGET_ASM_ALIGNED_DI_OP "\t.dword\t"
#undef TARGET_ASM_UNALIGNED_HI_OP
#define TARGET_ASM_UNALIGNED_HI_OP TARGET_ASM_ALIGNED_HI_OP
#undef TARGET_ASM_UNALIGNED_SI_OP
#define TARGET_ASM_UNALIGNED_SI_OP TARGET_ASM_ALIGNED_SI_OP
#undef TARGET_ASM_UNALIGNED_DI_OP
#define TARGET_ASM_UNALIGNED_DI_OP TARGET_ASM_ALIGNED_DI_OP
#undef TARGET_ASM_INTEGER
#define TARGET_ASM_INTEGER pa_assemble_integer

#undef TARGET_ASM_FUNCTION_PROLOGUE
#define TARGET_ASM_FUNCTION_PROLOGUE pa_output_function_prologue
#undef TARGET_ASM_FUNCTION_EPILOGUE
#define TARGET_ASM_FUNCTION_EPILOGUE pa_output_function_epilogue

#undef TARGET_SCHED_ADJUST_COST
#define TARGET_SCHED_ADJUST_COST pa_adjust_cost
#undef TARGET_SCHED_ADJUST_PRIORITY
#define TARGET_SCHED_ADJUST_PRIORITY pa_adjust_priority
#undef TARGET_SCHED_ISSUE_RATE
#define TARGET_SCHED_ISSUE_RATE pa_issue_rate

#undef TARGET_ENCODE_SECTION_INFO
#define TARGET_ENCODE_SECTION_INFO pa_encode_section_info
#undef TARGET_STRIP_NAME_ENCODING
#define TARGET_STRIP_NAME_ENCODING pa_strip_name_encoding

#undef TARGET_FUNCTION_OK_FOR_SIBCALL
#define TARGET_FUNCTION_OK_FOR_SIBCALL pa_function_ok_for_sibcall

#undef TARGET_COMMUTATIVE_P
#define TARGET_COMMUTATIVE_P pa_commutative_p

#undef TARGET_ASM_OUTPUT_MI_THUNK
#define TARGET_ASM_OUTPUT_MI_THUNK pa_asm_output_mi_thunk
#undef TARGET_ASM_CAN_OUTPUT_MI_THUNK
#define TARGET_ASM_CAN_OUTPUT_MI_THUNK default_can_output_mi_thunk_no_vcall

#undef TARGET_ASM_FILE_END
#ifdef ASM_OUTPUT_EXTERNAL_REAL
#define TARGET_ASM_FILE_END pa_hpux_file_end
#else
#define TARGET_ASM_FILE_END output_deferred_plabels
#endif

#if !defined(USE_COLLECT2)
#undef TARGET_ASM_CONSTRUCTOR
#define TARGET_ASM_CONSTRUCTOR pa_asm_out_constructor
#undef TARGET_ASM_DESTRUCTOR
#define TARGET_ASM_DESTRUCTOR pa_asm_out_destructor
#endif

#undef TARGET_DEFAULT_TARGET_FLAGS
#define TARGET_DEFAULT_TARGET_FLAGS (TARGET_DEFAULT | TARGET_CPU_DEFAULT)
#undef TARGET_HANDLE_OPTION
#define TARGET_HANDLE_OPTION pa_handle_option

#undef TARGET_INIT_BUILTINS
#define TARGET_INIT_BUILTINS pa_init_builtins

#undef TARGET_RTX_COSTS
#define TARGET_RTX_COSTS hppa_rtx_costs
#undef TARGET_ADDRESS_COST
#define TARGET_ADDRESS_COST hppa_address_cost

#undef TARGET_MACHINE_DEPENDENT_REORG
#define TARGET_MACHINE_DEPENDENT_REORG pa_reorg

#ifdef HPUX_LONG_DOUBLE_LIBRARY
#undef TARGET_INIT_LIBFUNCS
#define TARGET_INIT_LIBFUNCS pa_hpux_init_libfuncs
#endif

#undef TARGET_PROMOTE_FUNCTION_RETURN
#define TARGET_PROMOTE_FUNCTION_RETURN hook_bool_tree_true
#undef TARGET_PROMOTE_PROTOTYPES
#define TARGET_PROMOTE_PROTOTYPES hook_bool_tree_true

#undef TARGET_STRUCT_VALUE_RTX
#define TARGET_STRUCT_VALUE_RTX pa_struct_value_rtx
#undef TARGET_RETURN_IN_MEMORY
#define TARGET_RETURN_IN_MEMORY pa_return_in_memory
#undef TARGET_MUST_PASS_IN_STACK
#define TARGET_MUST_PASS_IN_STACK must_pass_in_stack_var_size
#undef TARGET_PASS_BY_REFERENCE
#define TARGET_PASS_BY_REFERENCE pa_pass_by_reference
#undef TARGET_CALLEE_COPIES
#define TARGET_CALLEE_COPIES hook_bool_CUMULATIVE_ARGS_mode_tree_bool_true
#undef TARGET_ARG_PARTIAL_BYTES
#define TARGET_ARG_PARTIAL_BYTES pa_arg_partial_bytes

#undef TARGET_EXPAND_BUILTIN_SAVEREGS
#define TARGET_EXPAND_BUILTIN_SAVEREGS hppa_builtin_saveregs
#undef TARGET_GIMPLIFY_VA_ARG_EXPR
#define TARGET_GIMPLIFY_VA_ARG_EXPR hppa_gimplify_va_arg_expr

#undef TARGET_SCALAR_MODE_SUPPORTED_P
#define TARGET_SCALAR_MODE_SUPPORTED_P pa_scalar_mode_supported_p

#undef TARGET_CANNOT_FORCE_CONST_MEM
#define TARGET_CANNOT_FORCE_CONST_MEM pa_tls_referenced_p

#undef TARGET_SECONDARY_RELOAD
#define TARGET_SECONDARY_RELOAD pa_secondary_reload

struct gcc_target targetm = TARGET_INITIALIZER;

/* Parse the -mfixed-range= option string.  */

static void
fix_range (const char *const_str)
{
  int i, first, last;
  char *str, *dash, *comma;

  /* str must be of the form REG1'-'REG2{,REG1'-'REG} where REG1 and
     REG2 are either register names or register numbers.  The effect
     of this option is to mark the registers in the range from REG1 to
     REG2 as ``fixed'' so they won't be used by the compiler.  This is
     used, e.g., to ensure that kernel mode code doesn't use fr4-fr31.  */

  i = strlen (const_str);
  str = (char *) alloca (i + 1);
  memcpy (str, const_str, i + 1);

  while (1)
    {
      dash = strchr (str, '-');
      if (!dash)
	{
	  warning (0, "value of -mfixed-range must have form REG1-REG2");
	  return;
	}
      *dash = '\0';

      comma = strchr (dash + 1, ',');
      if (comma)
	*comma = '\0';

      first = decode_reg_name (str);
      if (first < 0)
	{
	  warning (0, "unknown register name: %s", str);
	  return;
	}

      last = decode_reg_name (dash + 1);
      if (last < 0)
	{
	  warning (0, "unknown register name: %s", dash + 1);
	  return;
	}

      *dash = '-';

      if (first > last)
	{
	  warning (0, "%s-%s is an empty range", str, dash + 1);
	  return;
	}

      for (i = first; i <= last; ++i)
	fixed_regs[i] = call_used_regs[i] = 1;

      if (!comma)
	break;

      *comma = ',';
      str = comma + 1;
    }

  /* Check if all floating point registers have been fixed.  */
  for (i = FP_REG_FIRST; i <= FP_REG_LAST; i++)
    if (!fixed_regs[i])
      break;

  if (i > FP_REG_LAST)
    target_flags |= MASK_DISABLE_FPREGS;
}

/* Implement TARGET_HANDLE_OPTION.  */

static bool
pa_handle_option (size_t code, const char *arg, int value ATTRIBUTE_UNUSED)
{
  switch (code)
    {
    case OPT_mnosnake:
    case OPT_mpa_risc_1_0:
    case OPT_march_1_0:
      target_flags &= ~(MASK_PA_11 | MASK_PA_20);
      return true;

    case OPT_msnake:
    case OPT_mpa_risc_1_1:
    case OPT_march_1_1:
      target_flags &= ~MASK_PA_20;
      target_flags |= MASK_PA_11;
      return true;

    case OPT_mpa_risc_2_0:
    case OPT_march_2_0:
      target_flags |= MASK_PA_11 | MASK_PA_20;
      return true;

    case OPT_mschedule_:
      if (strcmp (arg, "8000") == 0)
	pa_cpu = PROCESSOR_8000;
      else if (strcmp (arg, "7100") == 0)
	pa_cpu = PROCESSOR_7100;
      else if (strcmp (arg, "700") == 0)
	pa_cpu = PROCESSOR_700;
      else if (strcmp (arg, "7100LC") == 0)
	pa_cpu = PROCESSOR_7100LC;
      else if (strcmp (arg, "7200") == 0)
	pa_cpu = PROCESSOR_7200;
      else if (strcmp (arg, "7300") == 0)
	pa_cpu = PROCESSOR_7300;
      else
	return false;
      return true;

    case OPT_mfixed_range_:
      fix_range (arg);
      return true;

#if TARGET_HPUX
    case OPT_munix_93:
      flag_pa_unix = 1993;
      return true;
#endif

#if TARGET_HPUX_10_10
    case OPT_munix_95:
      flag_pa_unix = 1995;
      return true;
#endif

#if TARGET_HPUX_11_11
    case OPT_munix_98:
      flag_pa_unix = 1998;
      return true;
#endif

    default:
      return true;
    }
}

void
override_options (void)
{
  /* Unconditional branches in the delay slot are not compatible with dwarf2
     call frame information.  There is no benefit in using this optimization
     on PA8000 and later processors.  */
  if (pa_cpu >= PROCESSOR_8000
      || (! USING_SJLJ_EXCEPTIONS && flag_exceptions)
      || flag_unwind_tables)
    target_flags &= ~MASK_JUMP_IN_DELAY;

  if (flag_pic && TARGET_PORTABLE_RUNTIME)
    {
      warning (0, "PIC code generation is not supported in the portable runtime model");
    }

  if (flag_pic && TARGET_FAST_INDIRECT_CALLS)
   {
      warning (0, "PIC code generation is not compatible with fast indirect calls");
   }

  if (! TARGET_GAS && write_symbols != NO_DEBUG)
    {
      warning (0, "-g is only supported when using GAS on this processor,");
      warning (0, "-g option disabled");
      write_symbols = NO_DEBUG;
    }

  /* We only support the "big PIC" model now.  And we always generate PIC
     code when in 64bit mode.  */
  if (flag_pic == 1 || TARGET_64BIT)
    flag_pic = 2;

  /* We can't guarantee that .dword is available for 32-bit targets.  */
  if (UNITS_PER_WORD == 4)
    targetm.asm_out.aligned_op.di = NULL;

  /* The unaligned ops are only available when using GAS.  */
  if (!TARGET_GAS)
    {
      targetm.asm_out.unaligned_op.hi = NULL;
      targetm.asm_out.unaligned_op.si = NULL;
      targetm.asm_out.unaligned_op.di = NULL;
    }

  init_machine_status = pa_init_machine_status;
}

static void
pa_init_builtins (void)
{
#ifdef DONT_HAVE_FPUTC_UNLOCKED
  built_in_decls[(int) BUILT_IN_FPUTC_UNLOCKED] =
    built_in_decls[(int) BUILT_IN_PUTC_UNLOCKED];
  implicit_built_in_decls[(int) BUILT_IN_FPUTC_UNLOCKED]
    = implicit_built_in_decls[(int) BUILT_IN_PUTC_UNLOCKED];
#endif
}

/* Function to init struct machine_function.
   This will be called, via a pointer variable,
   from push_function_context.  */

static struct machine_function *
pa_init_machine_status (void)
{
  return ggc_alloc_cleared (sizeof (machine_function));
}

/* If FROM is a probable pointer register, mark TO as a probable
   pointer register with the same pointer alignment as FROM.  */

static void
copy_reg_pointer (rtx to, rtx from)
{
  if (REG_POINTER (from))
    mark_reg_pointer (to, REGNO_POINTER_ALIGN (REGNO (from)));
}

/* Return 1 if X contains a symbolic expression.  We know these
   expressions will have one of a few well defined forms, so
   we need only check those forms.  */
int
symbolic_expression_p (rtx x)
{

  /* Strip off any HIGH.  */
  if (GET_CODE (x) == HIGH)
    x = XEXP (x, 0);

  return (symbolic_operand (x, VOIDmode));
}

/* Accept any constant that can be moved in one instruction into a
   general register.  */
int
cint_ok_for_move (HOST_WIDE_INT intval)
{
  /* OK if ldo, ldil, or zdepi, can be used.  */
  return (CONST_OK_FOR_LETTER_P (intval, 'J')
	  || CONST_OK_FOR_LETTER_P (intval, 'N')
	  || CONST_OK_FOR_LETTER_P (intval, 'K'));
}

/* Return truth value of whether OP can be used as an operand in a
   adddi3 insn.  */
int
adddi3_operand (rtx op, enum machine_mode mode)
{
  return (register_operand (op, mode)
	  || (GET_CODE (op) == CONST_INT
	      && (TARGET_64BIT ? INT_14_BITS (op) : INT_11_BITS (op))));
}

/* True iff zdepi can be used to generate this CONST_INT.
   zdepi first sign extends a 5 bit signed number to a given field
   length, then places this field anywhere in a zero.  */
int
zdepi_cint_p (unsigned HOST_WIDE_INT x)
{
  unsigned HOST_WIDE_INT lsb_mask, t;

  /* This might not be obvious, but it's at least fast.
     This function is critical; we don't have the time loops would take.  */
  lsb_mask = x & -x;
  t = ((x >> 4) + lsb_mask) & ~(lsb_mask - 1);
  /* Return true iff t is a power of two.  */
  return ((t & (t - 1)) == 0);
}

/* True iff depi or extru can be used to compute (reg & mask).
   Accept bit pattern like these:
   0....01....1
   1....10....0
   1..10..01..1  */
int
and_mask_p (unsigned HOST_WIDE_INT mask)
{
  mask = ~mask;
  mask += mask & -mask;
  return (mask & (mask - 1)) == 0;
}

/* True iff depi can be used to compute (reg | MASK).  */
int
ior_mask_p (unsigned HOST_WIDE_INT mask)
{
  mask += mask & -mask;
  return (mask & (mask - 1)) == 0;
}

/* Legitimize PIC addresses.  If the address is already
   position-independent, we return ORIG.  Newly generated
   position-independent addresses go to REG.  If we need more
   than one register, we lose.  */

rtx
legitimize_pic_address (rtx orig, enum machine_mode mode, rtx reg)
{
  rtx pic_ref = orig;

  gcc_assert (!PA_SYMBOL_REF_TLS_P (orig));

  /* Labels need special handling.  */
  if (pic_label_operand (orig, mode))
    {
      /* We do not want to go through the movXX expanders here since that
	 would create recursion.

	 Nor do we really want to call a generator for a named pattern
	 since that requires multiple patterns if we want to support
	 multiple word sizes.

	 So instead we just emit the raw set, which avoids the movXX
	 expanders completely.  */
      mark_reg_pointer (reg, BITS_PER_UNIT);
      emit_insn (gen_rtx_SET (VOIDmode, reg, orig));
      current_function_uses_pic_offset_table = 1;
      return reg;
    }
  if (GET_CODE (orig) == SYMBOL_REF)
    {
      rtx insn, tmp_reg;

      gcc_assert (reg);

      /* Before reload, allocate a temporary register for the intermediate
	 result.  This allows the sequence to be deleted when the final
	 result is unused and the insns are trivially dead.  */
      tmp_reg = ((reload_in_progress || reload_completed)
		 ? reg : gen_reg_rtx (Pmode));

      emit_move_insn (tmp_reg,
		      gen_rtx_PLUS (word_mode, pic_offset_table_rtx,
				    gen_rtx_HIGH (word_mode, orig)));
      pic_ref
	= gen_const_mem (Pmode,
		         gen_rtx_LO_SUM (Pmode, tmp_reg,
				         gen_rtx_UNSPEC (Pmode,
						         gen_rtvec (1, orig),
						         UNSPEC_DLTIND14R)));

      current_function_uses_pic_offset_table = 1;
      mark_reg_pointer (reg, BITS_PER_UNIT);
      insn = emit_move_insn (reg, pic_ref);

      /* Put a REG_EQUAL note on this insn, so that it can be optimized.  */
      REG_NOTES (insn) = gen_rtx_EXPR_LIST (REG_EQUAL, orig, REG_NOTES (insn));

      return reg;
    }
  else if (GET_CODE (orig) == CONST)
    {
      rtx base;

      if (GET_CODE (XEXP (orig, 0)) == PLUS
	  && XEXP (XEXP (orig, 0), 0) == pic_offset_table_rtx)
	return orig;

      gcc_assert (reg);
      gcc_assert (GET_CODE (XEXP (orig, 0)) == PLUS);
      
      base = legitimize_pic_address (XEXP (XEXP (orig, 0), 0), Pmode, reg);
      orig = legitimize_pic_address (XEXP (XEXP (orig, 0), 1), Pmode,
				     base == reg ? 0 : reg);

      if (GET_CODE (orig) == CONST_INT)
	{
	  if (INT_14_BITS (orig))
	    return plus_constant (base, INTVAL (orig));
	  orig = force_reg (Pmode, orig);
	}
      pic_ref = gen_rtx_PLUS (Pmode, base, orig);
      /* Likewise, should we set special REG_NOTEs here?  */
    }

  return pic_ref;
}

static GTY(()) rtx gen_tls_tga;

static rtx
gen_tls_get_addr (void)
{
  if (!gen_tls_tga)
    gen_tls_tga = init_one_libfunc ("__tls_get_addr");
  return gen_tls_tga;
}

static rtx
hppa_tls_call (rtx arg)
{
  rtx ret;

  ret = gen_reg_rtx (Pmode);
  emit_library_call_value (gen_tls_get_addr (), ret,
		  	   LCT_CONST, Pmode, 1, arg, Pmode);

  return ret;
}

static rtx
legitimize_tls_address (rtx addr)
{
  rtx ret, insn, tmp, t1, t2, tp;
  enum tls_model model = SYMBOL_REF_TLS_MODEL (addr);

  switch (model) 
    {
      case TLS_MODEL_GLOBAL_DYNAMIC:
	tmp = gen_reg_rtx (Pmode);
	if (flag_pic)
	  emit_insn (gen_tgd_load_pic (tmp, addr));
	else
	  emit_insn (gen_tgd_load (tmp, addr));
	ret = hppa_tls_call (tmp);
	break;

      case TLS_MODEL_LOCAL_DYNAMIC:
	ret = gen_reg_rtx (Pmode);
	tmp = gen_reg_rtx (Pmode);
	start_sequence ();
	if (flag_pic)
	  emit_insn (gen_tld_load_pic (tmp, addr));
	else
	  emit_insn (gen_tld_load (tmp, addr));
	t1 = hppa_tls_call (tmp);
	insn = get_insns ();
	end_sequence ();
	t2 = gen_reg_rtx (Pmode);
	emit_libcall_block (insn, t2, t1, 
			    gen_rtx_UNSPEC (Pmode, gen_rtvec (1, const0_rtx),
				            UNSPEC_TLSLDBASE));
	emit_insn (gen_tld_offset_load (ret, addr, t2));
	break;

      case TLS_MODEL_INITIAL_EXEC:
	tp = gen_reg_rtx (Pmode);
	tmp = gen_reg_rtx (Pmode);
	ret = gen_reg_rtx (Pmode);
	emit_insn (gen_tp_load (tp));
	if (flag_pic)
	  emit_insn (gen_tie_load_pic (tmp, addr));
	else
	  emit_insn (gen_tie_load (tmp, addr));
	emit_move_insn (ret, gen_rtx_PLUS (Pmode, tp, tmp));
	break;

      case TLS_MODEL_LOCAL_EXEC:
	tp = gen_reg_rtx (Pmode);
	ret = gen_reg_rtx (Pmode);
	emit_insn (gen_tp_load (tp));
	emit_insn (gen_tle_load (ret, addr, tp));
	break;

      default:
	gcc_unreachable ();
    }

  return ret;
}

/* Try machine-dependent ways of modifying an illegitimate address
   to be legitimate.  If we find one, return the new, valid address.
   This macro is used in only one place: `memory_address' in explow.c.

   OLDX is the address as it was before break_out_memory_refs was called.
   In some cases it is useful to look at this to decide what needs to be done.

   MODE and WIN are passed so that this macro can use
   GO_IF_LEGITIMATE_ADDRESS.

   It is always safe for this macro to do nothing.  It exists to recognize
   opportunities to optimize the output.

   For the PA, transform:

	memory(X + <large int>)

   into:

	if (<large int> & mask) >= 16
	  Y = (<large int> & ~mask) + mask + 1	Round up.
	else
	  Y = (<large int> & ~mask)		Round down.
	Z = X + Y
	memory (Z + (<large int> - Y));

   This is for CSE to find several similar references, and only use one Z.

   X can either be a SYMBOL_REF or REG, but because combine cannot
   perform a 4->2 combination we do nothing for SYMBOL_REF + D where
   D will not fit in 14 bits.

   MODE_FLOAT references allow displacements which fit in 5 bits, so use
   0x1f as the mask.

   MODE_INT references allow displacements which fit in 14 bits, so use
   0x3fff as the mask.

   This relies on the fact that most mode MODE_FLOAT references will use FP
   registers and most mode MODE_INT references will use integer registers.
   (In the rare case of an FP register used in an integer MODE, we depend
   on secondary reloads to clean things up.)


   It is also beneficial to handle (plus (mult (X) (Y)) (Z)) in a special
   manner if Y is 2, 4, or 8.  (allows more shadd insns and shifted indexed
   addressing modes to be used).

   Put X and Z into registers.  Then put the entire expression into
   a register.  */

rtx
hppa_legitimize_address (rtx x, rtx oldx ATTRIBUTE_UNUSED,
			 enum machine_mode mode)
{
  rtx orig = x;

  /* We need to canonicalize the order of operands in unscaled indexed
     addresses since the code that checks if an address is valid doesn't
     always try both orders.  */
  if (!TARGET_NO_SPACE_REGS
      && GET_CODE (x) == PLUS
      && GET_MODE (x) == Pmode
      && REG_P (XEXP (x, 0))
      && REG_P (XEXP (x, 1))
      && REG_POINTER (XEXP (x, 0))
      && !REG_POINTER (XEXP (x, 1)))
    return gen_rtx_PLUS (Pmode, XEXP (x, 1), XEXP (x, 0));

  if (PA_SYMBOL_REF_TLS_P (x))
    return legitimize_tls_address (x);
  else if (flag_pic)
    return legitimize_pic_address (x, mode, gen_reg_rtx (Pmode));

  /* Strip off CONST.  */
  if (GET_CODE (x) == CONST)
    x = XEXP (x, 0);

  /* Special case.  Get the SYMBOL_REF into a register and use indexing.
     That should always be safe.  */
  if (GET_CODE (x) == PLUS
      && GET_CODE (XEXP (x, 0)) == REG
      && GET_CODE (XEXP (x, 1)) == SYMBOL_REF)
    {
      rtx reg = force_reg (Pmode, XEXP (x, 1));
      return force_reg (Pmode, gen_rtx_PLUS (Pmode, reg, XEXP (x, 0)));
    }

  /* Note we must reject symbols which represent function addresses
     since the assembler/linker can't handle arithmetic on plabels.  */
  if (GET_CODE (x) == PLUS
      && GET_CODE (XEXP (x, 1)) == CONST_INT
      && ((GET_CODE (XEXP (x, 0)) == SYMBOL_REF
	   && !FUNCTION_NAME_P (XSTR (XEXP (x, 0), 0)))
	  || GET_CODE (XEXP (x, 0)) == REG))
    {
      rtx int_part, ptr_reg;
      int newoffset;
      int offset = INTVAL (XEXP (x, 1));
      int mask;

      mask = (GET_MODE_CLASS (mode) == MODE_FLOAT
	      ? (TARGET_PA_20 ? 0x3fff : 0x1f) : 0x3fff);

      /* Choose which way to round the offset.  Round up if we
	 are >= halfway to the next boundary.  */
      if ((offset & mask) >= ((mask + 1) / 2))
	newoffset = (offset & ~ mask) + mask + 1;
      else
	newoffset = (offset & ~ mask);

      /* If the newoffset will not fit in 14 bits (ldo), then
	 handling this would take 4 or 5 instructions (2 to load
	 the SYMBOL_REF + 1 or 2 to load the newoffset + 1 to
	 add the new offset and the SYMBOL_REF.)  Combine can
	 not handle 4->2 or 5->2 combinations, so do not create
	 them.  */
      if (! VAL_14_BITS_P (newoffset)
	  && GET_CODE (XEXP (x, 0)) == SYMBOL_REF)
	{
	  rtx const_part = plus_constant (XEXP (x, 0), newoffset);
	  rtx tmp_reg
	    = force_reg (Pmode,
			 gen_rtx_HIGH (Pmode, const_part));
	  ptr_reg
	    = force_reg (Pmode,
			 gen_rtx_LO_SUM (Pmode,
					 tmp_reg, const_part));
	}
      else
	{
	  if (! VAL_14_BITS_P (newoffset))
	    int_part = force_reg (Pmode, GEN_INT (newoffset));
	  else
	    int_part = GEN_INT (newoffset);

	  ptr_reg = force_reg (Pmode,
			       gen_rtx_PLUS (Pmode,
					     force_reg (Pmode, XEXP (x, 0)),
					     int_part));
	}
      return plus_constant (ptr_reg, offset - newoffset);
    }

  /* Handle (plus (mult (a) (shadd_constant)) (b)).  */

  if (GET_CODE (x) == PLUS && GET_CODE (XEXP (x, 0)) == MULT
      && GET_CODE (XEXP (XEXP (x, 0), 1)) == CONST_INT
      && shadd_constant_p (INTVAL (XEXP (XEXP (x, 0), 1)))
      && (OBJECT_P (XEXP (x, 1))
	  || GET_CODE (XEXP (x, 1)) == SUBREG)
      && GET_CODE (XEXP (x, 1)) != CONST)
    {
      int val = INTVAL (XEXP (XEXP (x, 0), 1));
      rtx reg1, reg2;

      reg1 = XEXP (x, 1);
      if (GET_CODE (reg1) != REG)
	reg1 = force_reg (Pmode, force_operand (reg1, 0));

      reg2 = XEXP (XEXP (x, 0), 0);
      if (GET_CODE (reg2) != REG)
        reg2 = force_reg (Pmode, force_operand (reg2, 0));

      return force_reg (Pmode, gen_rtx_PLUS (Pmode,
					     gen_rtx_MULT (Pmode,
							   reg2,
							   GEN_INT (val)),
					     reg1));
    }

  /* Similarly for (plus (plus (mult (a) (shadd_constant)) (b)) (c)).

     Only do so for floating point modes since this is more speculative
     and we lose if it's an integer store.  */
  if (GET_CODE (x) == PLUS
      && GET_CODE (XEXP (x, 0)) == PLUS
      && GET_CODE (XEXP (XEXP (x, 0), 0)) == MULT
      && GET_CODE (XEXP (XEXP (XEXP (x, 0), 0), 1)) == CONST_INT
      && shadd_constant_p (INTVAL (XEXP (XEXP (XEXP (x, 0), 0), 1)))
      && (mode == SFmode || mode == DFmode))
    {

      /* First, try and figure out what to use as a base register.  */
      rtx reg1, reg2, base, idx, orig_base;

      reg1 = XEXP (XEXP (x, 0), 1);
      reg2 = XEXP (x, 1);
      base = NULL_RTX;
      idx = NULL_RTX;

      /* Make sure they're both regs.  If one was a SYMBOL_REF [+ const],
	 then emit_move_sequence will turn on REG_POINTER so we'll know
	 it's a base register below.  */
      if (GET_CODE (reg1) != REG)
	reg1 = force_reg (Pmode, force_operand (reg1, 0));

      if (GET_CODE (reg2) != REG)
	reg2 = force_reg (Pmode, force_operand (reg2, 0));

      /* Figure out what the base and index are.  */

      if (GET_CODE (reg1) == REG
	  && REG_POINTER (reg1))
	{
	  base = reg1;
	  orig_base = XEXP (XEXP (x, 0), 1);
	  idx = gen_rtx_PLUS (Pmode,
			      gen_rtx_MULT (Pmode,
					    XEXP (XEXP (XEXP (x, 0), 0), 0),
					    XEXP (XEXP (XEXP (x, 0), 0), 1)),
			      XEXP (x, 1));
	}
      else if (GET_CODE (reg2) == REG
	       && REG_POINTER (reg2))
	{
	  base = reg2;
	  orig_base = XEXP (x, 1);
	  idx = XEXP (x, 0);
	}

      if (base == 0)
	return orig;

      /* If the index adds a large constant, try to scale the
	 constant so that it can be loaded with only one insn.  */
      if (GET_CODE (XEXP (idx, 1)) == CONST_INT
	  && VAL_14_BITS_P (INTVAL (XEXP (idx, 1))
			    / INTVAL (XEXP (XEXP (idx, 0), 1)))
	  && INTVAL (XEXP (idx, 1)) % INTVAL (XEXP (XEXP (idx, 0), 1)) == 0)
	{
	  /* Divide the CONST_INT by the scale factor, then add it to A.  */
	  int val = INTVAL (XEXP (idx, 1));

	  val /= INTVAL (XEXP (XEXP (idx, 0), 1));
	  reg1 = XEXP (XEXP (idx, 0), 0);
	  if (GET_CODE (reg1) != REG)
	    reg1 = force_reg (Pmode, force_operand (reg1, 0));

	  reg1 = force_reg (Pmode, gen_rtx_PLUS (Pmode, reg1, GEN_INT (val)));

	  /* We can now generate a simple scaled indexed address.  */
	  return
	    force_reg
	      (Pmode, gen_rtx_PLUS (Pmode,
				    gen_rtx_MULT (Pmode, reg1,
						  XEXP (XEXP (idx, 0), 1)),
				    base));
	}

      /* If B + C is still a valid base register, then add them.  */
      if (GET_CODE (XEXP (idx, 1)) == CONST_INT
	  && INTVAL (XEXP (idx, 1)) <= 4096
	  && INTVAL (XEXP (idx, 1)) >= -4096)
	{
	  int val = INTVAL (XEXP (XEXP (idx, 0), 1));
	  rtx reg1, reg2;

	  reg1 = force_reg (Pmode, gen_rtx_PLUS (Pmode, base, XEXP (idx, 1)));

	  reg2 = XEXP (XEXP (idx, 0), 0);
	  if (GET_CODE (reg2) != CONST_INT)
	    reg2 = force_reg (Pmode, force_operand (reg2, 0));

	  return force_reg (Pmode, gen_rtx_PLUS (Pmode,
						 gen_rtx_MULT (Pmode,
							       reg2,
							       GEN_INT (val)),
						 reg1));
	}

      /* Get the index into a register, then add the base + index and
	 return a register holding the result.  */

      /* First get A into a register.  */
      reg1 = XEXP (XEXP (idx, 0), 0);
      if (GET_CODE (reg1) != REG)
	reg1 = force_reg (Pmode, force_operand (reg1, 0));

      /* And get B into a register.  */
      reg2 = XEXP (idx, 1);
      if (GET_CODE (reg2) != REG)
	reg2 = force_reg (Pmode, force_operand (reg2, 0));

      reg1 = force_reg (Pmode,
			gen_rtx_PLUS (Pmode,
				      gen_rtx_MULT (Pmode, reg1,
						    XEXP (XEXP (idx, 0), 1)),
				      reg2));

      /* Add the result to our base register and return.  */
      return force_reg (Pmode, gen_rtx_PLUS (Pmode, base, reg1));

    }

  /* Uh-oh.  We might have an address for x[n-100000].  This needs
     special handling to avoid creating an indexed memory address
     with x-100000 as the base.

     If the constant part is small enough, then it's still safe because
     there is a guard page at the beginning and end of the data segment.

     Scaled references are common enough that we want to try and rearrange the
     terms so that we can use indexing for these addresses too.  Only
     do the optimization for floatint point modes.  */

  if (GET_CODE (x) == PLUS
      && symbolic_expression_p (XEXP (x, 1)))
    {
      /* Ugly.  We modify things here so that the address offset specified
	 by the index expression is computed first, then added to x to form
	 the entire address.  */

      rtx regx1, regx2, regy1, regy2, y;

      /* Strip off any CONST.  */
      y = XEXP (x, 1);
      if (GET_CODE (y) == CONST)
	y = XEXP (y, 0);

      if (GET_CODE (y) == PLUS || GET_CODE (y) == MINUS)
	{
	  /* See if this looks like
		(plus (mult (reg) (shadd_const))
		      (const (plus (symbol_ref) (const_int))))

	     Where const_int is small.  In that case the const
	     expression is a valid pointer for indexing.

	     If const_int is big, but can be divided evenly by shadd_const
	     and added to (reg).  This allows more scaled indexed addresses.  */
	  if (GET_CODE (XEXP (y, 0)) == SYMBOL_REF
	      && GET_CODE (XEXP (x, 0)) == MULT
	      && GET_CODE (XEXP (y, 1)) == CONST_INT
	      && INTVAL (XEXP (y, 1)) >= -4096
	      && INTVAL (XEXP (y, 1)) <= 4095
	      && GET_CODE (XEXP (XEXP (x, 0), 1)) == CONST_INT
	      && shadd_constant_p (INTVAL (XEXP (XEXP (x, 0), 1))))
	    {
	      int val = INTVAL (XEXP (XEXP (x, 0), 1));
	      rtx reg1, reg2;

	      reg1 = XEXP (x, 1);
	      if (GET_CODE (reg1) != REG)
		reg1 = force_reg (Pmode, force_operand (reg1, 0));

	      reg2 = XEXP (XEXP (x, 0), 0);
	      if (GET_CODE (reg2) != REG)
	        reg2 = force_reg (Pmode, force_operand (reg2, 0));

	      return force_reg (Pmode,
				gen_rtx_PLUS (Pmode,
					      gen_rtx_MULT (Pmode,
							    reg2,
							    GEN_INT (val)),
					      reg1));
	    }
	  else if ((mode == DFmode || mode == SFmode)
		   && GET_CODE (XEXP (y, 0)) == SYMBOL_REF
		   && GET_CODE (XEXP (x, 0)) == MULT
		   && GET_CODE (XEXP (y, 1)) == CONST_INT
		   && INTVAL (XEXP (y, 1)) % INTVAL (XEXP (XEXP (x, 0), 1)) == 0
		   && GET_CODE (XEXP (XEXP (x, 0), 1)) == CONST_INT
		   && shadd_constant_p (INTVAL (XEXP (XEXP (x, 0), 1))))
	    {
	      regx1
		= force_reg (Pmode, GEN_INT (INTVAL (XEXP (y, 1))
					     / INTVAL (XEXP (XEXP (x, 0), 1))));
	      regx2 = XEXP (XEXP (x, 0), 0);
	      if (GET_CODE (regx2) != REG)
		regx2 = force_reg (Pmode, force_operand (regx2, 0));
	      regx2 = force_reg (Pmode, gen_rtx_fmt_ee (GET_CODE (y), Pmode,
							regx2, regx1));
	      return
		force_reg (Pmode,
			   gen_rtx_PLUS (Pmode,
					 gen_rtx_MULT (Pmode, regx2,
						       XEXP (XEXP (x, 0), 1)),
					 force_reg (Pmode, XEXP (y, 0))));
	    }
	  else if (GET_CODE (XEXP (y, 1)) == CONST_INT
		   && INTVAL (XEXP (y, 1)) >= -4096
		   && INTVAL (XEXP (y, 1)) <= 4095)
	    {
	      /* This is safe because of the guard page at the
		 beginning and end of the data space.  Just
		 return the original address.  */
	      return orig;
	    }
	  else
	    {
	      /* Doesn't look like one we can optimize.  */
	      regx1 = force_reg (Pmode, force_operand (XEXP (x, 0), 0));
	      regy1 = force_reg (Pmode, force_operand (XEXP (y, 0), 0));
	      regy2 = force_reg (Pmode, force_operand (XEXP (y, 1), 0));
	      regx1 = force_reg (Pmode,
				 gen_rtx_fmt_ee (GET_CODE (y), Pmode,
						 regx1, regy2));
	      return force_reg (Pmode, gen_rtx_PLUS (Pmode, regx1, regy1));
	    }
	}
    }

  return orig;
}

/* For the HPPA, REG and REG+CONST is cost 0
   and addresses involving symbolic constants are cost 2.

   PIC addresses are very expensive.

   It is no coincidence that this has the same structure
   as GO_IF_LEGITIMATE_ADDRESS.  */

static int
hppa_address_cost (rtx X)
{
  switch (GET_CODE (X))
    {
    case REG:
    case PLUS:
    case LO_SUM:
      return 1;
    case HIGH:
      return 2;
    default:
      return 4;
    }
}

/* Compute a (partial) cost for rtx X.  Return true if the complete
   cost has been computed, and false if subexpressions should be
   scanned.  In either case, *TOTAL contains the cost result.  */

static bool
hppa_rtx_costs (rtx x, int code, int outer_code, int *total)
{
  switch (code)
    {
    case CONST_INT:
      if (INTVAL (x) == 0)
	*total = 0;
      else if (INT_14_BITS (x))
	*total = 1;
      else
	*total = 2;
      return true;

    case HIGH:
      *total = 2;
      return true;

    case CONST:
    case LABEL_REF:
    case SYMBOL_REF:
      *total = 4;
      return true;

    case CONST_DOUBLE:
      if ((x == CONST0_RTX (DFmode) || x == CONST0_RTX (SFmode))
	  && outer_code != SET)
	*total = 0;
      else
        *total = 8;
      return true;

    case MULT:
      if (GET_MODE_CLASS (GET_MODE (x)) == MODE_FLOAT)
        *total = COSTS_N_INSNS (3);
      else if (TARGET_PA_11 && !TARGET_DISABLE_FPREGS && !TARGET_SOFT_FLOAT)
	*total = COSTS_N_INSNS (8);
      else
	*total = COSTS_N_INSNS (20);
      return true;

    case DIV:
      if (GET_MODE_CLASS (GET_MODE (x)) == MODE_FLOAT)
	{
	  *total = COSTS_N_INSNS (14);
	  return true;
	}
      /* FALLTHRU */

    case UDIV:
    case MOD:
    case UMOD:
      *total = COSTS_N_INSNS (60);
      return true;

    case PLUS: /* this includes shNadd insns */
    case MINUS:
      if (GET_MODE_CLASS (GET_MODE (x)) == MODE_FLOAT)
	*total = COSTS_N_INSNS (3);
      else
        *total = COSTS_N_INSNS (1);
      return true;

    case ASHIFT:
    case ASHIFTRT:
    case LSHIFTRT:
      *total = COSTS_N_INSNS (1);
      return true;

    default:
      return false;
    }
}

/* Ensure mode of ORIG, a REG rtx, is MODE.  Returns either ORIG or a
   new rtx with the correct mode.  */
static inline rtx
force_mode (enum machine_mode mode, rtx orig)
{
  if (mode == GET_MODE (orig))
    return orig;

  gcc_assert (REGNO (orig) < FIRST_PSEUDO_REGISTER);

  return gen_rtx_REG (mode, REGNO (orig));
}

/* Return 1 if *X is a thread-local symbol.  */

static int
pa_tls_symbol_ref_1 (rtx *x, void *data ATTRIBUTE_UNUSED)
{
  return PA_SYMBOL_REF_TLS_P (*x);
}

/* Return 1 if X contains a thread-local symbol.  */

bool
pa_tls_referenced_p (rtx x)
{
  if (!TARGET_HAVE_TLS)
    return false;

  return for_each_rtx (&x, &pa_tls_symbol_ref_1, 0);
}

/* Emit insns to move operands[1] into operands[0].

   Return 1 if we have written out everything that needs to be done to
   do the move.  Otherwise, return 0 and the caller will emit the move
   normally.

   Note SCRATCH_REG may not be in the proper mode depending on how it
   will be used.  This routine is responsible for creating a new copy
   of SCRATCH_REG in the proper mode.  */

int
emit_move_sequence (rtx *operands, enum machine_mode mode, rtx scratch_reg)
{
  register rtx operand0 = operands[0];
  register rtx operand1 = operands[1];
  register rtx tem;

  /* We can only handle indexed addresses in the destination operand
     of floating point stores.  Thus, we need to break out indexed
     addresses from the destination operand.  */
  if (GET_CODE (operand0) == MEM && IS_INDEX_ADDR_P (XEXP (operand0, 0)))
    {
      /* This is only safe up to the beginning of life analysis.  */
      gcc_assert (!no_new_pseudos);

      tem = copy_to_mode_reg (Pmode, XEXP (operand0, 0));
      operand0 = replace_equiv_address (operand0, tem);
    }

  /* On targets with non-equivalent space registers, break out unscaled
     indexed addresses from the source operand before the final CSE.
     We have to do this because the REG_POINTER flag is not correctly
     carried through various optimization passes and CSE may substitute
     a pseudo without the pointer set for one with the pointer set.  As
     a result, we loose various opportunities to create insns with
     unscaled indexed addresses.  */
  if (!TARGET_NO_SPACE_REGS
      && !cse_not_expected
      && GET_CODE (operand1) == MEM
      && GET_CODE (XEXP (operand1, 0)) == PLUS
      && REG_P (XEXP (XEXP (operand1, 0), 0))
      && REG_P (XEXP (XEXP (operand1, 0), 1)))
    operand1
      = replace_equiv_address (operand1,
			       copy_to_mode_reg (Pmode, XEXP (operand1, 0)));

  if (scratch_reg
      && reload_in_progress && GET_CODE (operand0) == REG
      && REGNO (operand0) >= FIRST_PSEUDO_REGISTER)
    operand0 = reg_equiv_mem[REGNO (operand0)];
  else if (scratch_reg
	   && reload_in_progress && GET_CODE (operand0) == SUBREG
	   && GET_CODE (SUBREG_REG (operand0)) == REG
	   && REGNO (SUBREG_REG (operand0)) >= FIRST_PSEUDO_REGISTER)
    {
     /* We must not alter SUBREG_BYTE (operand0) since that would confuse
	the code which tracks sets/uses for delete_output_reload.  */
      rtx temp = gen_rtx_SUBREG (GET_MODE (operand0),
				 reg_equiv_mem [REGNO (SUBREG_REG (operand0))],
				 SUBREG_BYTE (operand0));
      operand0 = alter_subreg (&temp);
    }

  if (scratch_reg
      && reload_in_progress && GET_CODE (operand1) == REG
      && REGNO (operand1) >= FIRST_PSEUDO_REGISTER)
    operand1 = reg_equiv_mem[REGNO (operand1)];
  else if (scratch_reg
	   && reload_in_progress && GET_CODE (operand1) == SUBREG
	   && GET_CODE (SUBREG_REG (operand1)) == REG
	   && REGNO (SUBREG_REG (operand1)) >= FIRST_PSEUDO_REGISTER)
    {
     /* We must not alter SUBREG_BYTE (operand0) since that would confuse
	the code which tracks sets/uses for delete_output_reload.  */
      rtx temp = gen_rtx_SUBREG (GET_MODE (operand1),
				 reg_equiv_mem [REGNO (SUBREG_REG (operand1))],
				 SUBREG_BYTE (operand1));
      operand1 = alter_subreg (&temp);
    }

  if (scratch_reg && reload_in_progress && GET_CODE (operand0) == MEM
      && ((tem = find_replacement (&XEXP (operand0, 0)))
	  != XEXP (operand0, 0)))
    operand0 = replace_equiv_address (operand0, tem);

  if (scratch_reg && reload_in_progress && GET_CODE (operand1) == MEM
      && ((tem = find_replacement (&XEXP (operand1, 0)))
	  != XEXP (operand1, 0)))
    operand1 = replace_equiv_address (operand1, tem);

  /* Handle secondary reloads for loads/stores of FP registers from
     REG+D addresses where D does not fit in 5 or 14 bits, including
     (subreg (mem (addr))) cases.  */
  if (scratch_reg
      && fp_reg_operand (operand0, mode)
      && ((GET_CODE (operand1) == MEM
	   && !memory_address_p ((GET_MODE_SIZE (mode) == 4 ? SFmode : DFmode),
				 XEXP (operand1, 0)))
	  || ((GET_CODE (operand1) == SUBREG
	       && GET_CODE (XEXP (operand1, 0)) == MEM
	       && !memory_address_p ((GET_MODE_SIZE (mode) == 4
				      ? SFmode : DFmode),
				     XEXP (XEXP (operand1, 0), 0))))))
    {
      if (GET_CODE (operand1) == SUBREG)
	operand1 = XEXP (operand1, 0);

      /* SCRATCH_REG will hold an address and maybe the actual data.  We want
	 it in WORD_MODE regardless of what mode it was originally given
	 to us.  */
      scratch_reg = force_mode (word_mode, scratch_reg);

      /* D might not fit in 14 bits either; for such cases load D into
	 scratch reg.  */
      if (!memory_address_p (Pmode, XEXP (operand1, 0)))
	{
	  emit_move_insn (scratch_reg, XEXP (XEXP (operand1, 0), 1));
	  emit_move_insn (scratch_reg,
			  gen_rtx_fmt_ee (GET_CODE (XEXP (operand1, 0)),
					  Pmode,
					  XEXP (XEXP (operand1, 0), 0),
					  scratch_reg));
	}
      else
	emit_move_insn (scratch_reg, XEXP (operand1, 0));
      emit_insn (gen_rtx_SET (VOIDmode, operand0,
			      replace_equiv_address (operand1, scratch_reg)));
      return 1;
    }
  else if (scratch_reg
	   && fp_reg_operand (operand1, mode)
	   && ((GET_CODE (operand0) == MEM
		&& !memory_address_p ((GET_MODE_SIZE (mode) == 4
					? SFmode : DFmode),
				       XEXP (operand0, 0)))
	       || ((GET_CODE (operand0) == SUBREG)
		   && GET_CODE (XEXP (operand0, 0)) == MEM
		   && !memory_address_p ((GET_MODE_SIZE (mode) == 4
					  ? SFmode : DFmode),
			   		 XEXP (XEXP (operand0, 0), 0)))))
    {
      if (GET_CODE (operand0) == SUBREG)
	operand0 = XEXP (operand0, 0);

      /* SCRATCH_REG will hold an address and maybe the actual data.  We want
	 it in WORD_MODE regardless of what mode it was originally given
	 to us.  */
      scratch_reg = force_mode (word_mode, scratch_reg);

      /* D might not fit in 14 bits either; for such cases load D into
	 scratch reg.  */
      if (!memory_address_p (Pmode, XEXP (operand0, 0)))
	{
	  emit_move_insn (scratch_reg, XEXP (XEXP (operand0, 0), 1));
	  emit_move_insn (scratch_reg, gen_rtx_fmt_ee (GET_CODE (XEXP (operand0,
								        0)),
						       Pmode,
						       XEXP (XEXP (operand0, 0),
								   0),
						       scratch_reg));
	}
      else
	emit_move_insn (scratch_reg, XEXP (operand0, 0));
      emit_insn (gen_rtx_SET (VOIDmode,
			      replace_equiv_address (operand0, scratch_reg),
			      operand1));
      return 1;
    }
  /* Handle secondary reloads for loads of FP registers from constant
     expressions by forcing the constant into memory.

     Use scratch_reg to hold the address of the memory location.

     The proper fix is to change PREFERRED_RELOAD_CLASS to return
     NO_REGS when presented with a const_int and a register class
     containing only FP registers.  Doing so unfortunately creates
     more problems than it solves.   Fix this for 2.5.  */
  else if (scratch_reg
	   && CONSTANT_P (operand1)
	   && fp_reg_operand (operand0, mode))
    {
      rtx const_mem, xoperands[2];

      /* SCRATCH_REG will hold an address and maybe the actual data.  We want
	 it in WORD_MODE regardless of what mode it was originally given
	 to us.  */
      scratch_reg = force_mode (word_mode, scratch_reg);

      /* Force the constant into memory and put the address of the
	 memory location into scratch_reg.  */
      const_mem = force_const_mem (mode, operand1);
      xoperands[0] = scratch_reg;
      xoperands[1] = XEXP (const_mem, 0);
      emit_move_sequence (xoperands, Pmode, 0);

      /* Now load the destination register.  */
      emit_insn (gen_rtx_SET (mode, operand0,
			      replace_equiv_address (const_mem, scratch_reg)));
      return 1;
    }
  /* Handle secondary reloads for SAR.  These occur when trying to load
     the SAR from memory, FP register, or with a constant.  */
  else if (scratch_reg
	   && GET_CODE (operand0) == REG
	   && REGNO (operand0) < FIRST_PSEUDO_REGISTER
	   && REGNO_REG_CLASS (REGNO (operand0)) == SHIFT_REGS
	   && (GET_CODE (operand1) == MEM
	       || GET_CODE (operand1) == CONST_INT
	       || (GET_CODE (operand1) == REG
		   && FP_REG_CLASS_P (REGNO_REG_CLASS (REGNO (operand1))))))
    {
      /* D might not fit in 14 bits either; for such cases load D into
	 scratch reg.  */
      if (GET_CODE (operand1) == MEM
	  && !memory_address_p (Pmode, XEXP (operand1, 0)))
	{
	  /* We are reloading the address into the scratch register, so we
	     want to make sure the scratch register is a full register.  */
	  scratch_reg = force_mode (word_mode, scratch_reg);

	  emit_move_insn (scratch_reg, XEXP (XEXP (operand1, 0), 1));
	  emit_move_insn (scratch_reg, gen_rtx_fmt_ee (GET_CODE (XEXP (operand1,
								        0)),
						       Pmode,
						       XEXP (XEXP (operand1, 0),
						       0),
						       scratch_reg));

	  /* Now we are going to load the scratch register from memory,
	     we want to load it in the same width as the original MEM,
	     which must be the same as the width of the ultimate destination,
	     OPERAND0.  */
	  scratch_reg = force_mode (GET_MODE (operand0), scratch_reg);

	  emit_move_insn (scratch_reg,
			  replace_equiv_address (operand1, scratch_reg));
	}
      else
	{
	  /* We want to load the scratch register using the same mode as
	     the ultimate destination.  */
	  scratch_reg = force_mode (GET_MODE (operand0), scratch_reg);

	  emit_move_insn (scratch_reg, operand1);
	}

      /* And emit the insn to set the ultimate destination.  We know that
	 the scratch register has the same mode as the destination at this
	 point.  */
      emit_move_insn (operand0, scratch_reg);
      return 1;
    }
  /* Handle the most common case: storing into a register.  */
  else if (register_operand (operand0, mode))
    {
      if (register_operand (operand1, mode)
	  || (GET_CODE (operand1) == CONST_INT
	      && cint_ok_for_move (INTVAL (operand1)))
	  || (operand1 == CONST0_RTX (mode))
	  || (GET_CODE (operand1) == HIGH
	      && !symbolic_operand (XEXP (operand1, 0), VOIDmode))
	  /* Only `general_operands' can come here, so MEM is ok.  */
	  || GET_CODE (operand1) == MEM)
	{
	  /* Various sets are created during RTL generation which don't
	     have the REG_POINTER flag correctly set.  After the CSE pass,
	     instruction recognition can fail if we don't consistently
	     set this flag when performing register copies.  This should
	     also improve the opportunities for creating insns that use
	     unscaled indexing.  */
	  if (REG_P (operand0) && REG_P (operand1))
	    {
	      if (REG_POINTER (operand1)
		  && !REG_POINTER (operand0)
		  && !HARD_REGISTER_P (operand0))
		copy_reg_pointer (operand0, operand1);
	      else if (REG_POINTER (operand0)
		       && !REG_POINTER (operand1)
		       && !HARD_REGISTER_P (operand1))
		copy_reg_pointer (operand1, operand0);
	    }
	  
	  /* When MEMs are broken out, the REG_POINTER flag doesn't
	     get set.  In some cases, we can set the REG_POINTER flag
	     from the declaration for the MEM.  */
	  if (REG_P (operand0)
	      && GET_CODE (operand1) == MEM
	      && !REG_POINTER (operand0))
	    {
	      tree decl = MEM_EXPR (operand1);

	      /* Set the register pointer flag and register alignment
		 if the declaration for this memory reference is a
		 pointer type.  Fortran indirect argument references
		 are ignored.  */
	      if (decl
		  && !(flag_argument_noalias > 1
		       && TREE_CODE (decl) == INDIRECT_REF
		       && TREE_CODE (TREE_OPERAND (decl, 0)) == PARM_DECL))
		{
		  tree type;

		  /* If this is a COMPONENT_REF, use the FIELD_DECL from
		     tree operand 1.  */
		  if (TREE_CODE (decl) == COMPONENT_REF)
		    decl = TREE_OPERAND (decl, 1);

		  type = TREE_TYPE (decl);
		  if (TREE_CODE (type) == ARRAY_TYPE)
		    type = get_inner_array_type (type);

		  if (POINTER_TYPE_P (type))
		    {
		      int align;

		      type = TREE_TYPE (type);
		      /* Using TYPE_ALIGN_OK is rather conservative as
			 only the ada frontend actually sets it.  */
		      align = (TYPE_ALIGN_OK (type) ? TYPE_ALIGN (type)
			       : BITS_PER_UNIT);
		      mark_reg_pointer (operand0, align);
		    }
		}
	    }

	  emit_insn (gen_rtx_SET (VOIDmode, operand0, operand1));
	  return 1;
	}
    }
  else if (GET_CODE (operand0) == MEM)
    {
      if (mode == DFmode && operand1 == CONST0_RTX (mode)
	  && !(reload_in_progress || reload_completed))
	{
	  rtx temp = gen_reg_rtx (DFmode);

	  emit_insn (gen_rtx_SET (VOIDmode, temp, operand1));
	  emit_insn (gen_rtx_SET (VOIDmode, operand0, temp));
	  return 1;
	}
      if (register_operand (operand1, mode) || operand1 == CONST0_RTX (mode))
	{
	  /* Run this case quickly.  */
	  emit_insn (gen_rtx_SET (VOIDmode, operand0, operand1));
	  return 1;
	}
      if (! (reload_in_progress || reload_completed))
	{
	  operands[0] = validize_mem (operand0);
	  operands[1] = operand1 = force_reg (mode, operand1);
	}
    }

  /* Simplify the source if we need to.
     Note we do have to handle function labels here, even though we do
     not consider them legitimate constants.  Loop optimizations can
     call the emit_move_xxx with one as a source.  */
  if ((GET_CODE (operand1) != HIGH && immediate_operand (operand1, mode))
      || function_label_operand (operand1, mode)
      || (GET_CODE (operand1) == HIGH
	  && symbolic_operand (XEXP (operand1, 0), mode)))
    {
      int ishighonly = 0;

      if (GET_CODE (operand1) == HIGH)
	{
	  ishighonly = 1;
	  operand1 = XEXP (operand1, 0);
	}
      if (symbolic_operand (operand1, mode))
	{
	  /* Argh.  The assembler and linker can't handle arithmetic
	     involving plabels.

	     So we force the plabel into memory, load operand0 from
	     the memory location, then add in the constant part.  */
	  if ((GET_CODE (operand1) == CONST
	       && GET_CODE (XEXP (operand1, 0)) == PLUS
	       && function_label_operand (XEXP (XEXP (operand1, 0), 0), Pmode))
	      || function_label_operand (operand1, mode))
	    {
	      rtx temp, const_part;

	      /* Figure out what (if any) scratch register to use.  */
	      if (reload_in_progress || reload_completed)
		{
		  scratch_reg = scratch_reg ? scratch_reg : operand0;
		  /* SCRATCH_REG will hold an address and maybe the actual
		     data.  We want it in WORD_MODE regardless of what mode it
		     was originally given to us.  */
		  scratch_reg = force_mode (word_mode, scratch_reg);
		}
	      else if (flag_pic)
		scratch_reg = gen_reg_rtx (Pmode);

	      if (GET_CODE (operand1) == CONST)
		{
		  /* Save away the constant part of the expression.  */
		  const_part = XEXP (XEXP (operand1, 0), 1);
		  gcc_assert (GET_CODE (const_part) == CONST_INT);

		  /* Force the function label into memory.  */
		  temp = force_const_mem (mode, XEXP (XEXP (operand1, 0), 0));
		}
	      else
		{
		  /* No constant part.  */
		  const_part = NULL_RTX;

		  /* Force the function label into memory.  */
		  temp = force_const_mem (mode, operand1);
		}


	      /* Get the address of the memory location.  PIC-ify it if
		 necessary.  */
	      temp = XEXP (temp, 0);
	      if (flag_pic)
		temp = legitimize_pic_address (temp, mode, scratch_reg);

	      /* Put the address of the memory location into our destination
		 register.  */
	      operands[1] = temp;
	      emit_move_sequence (operands, mode, scratch_reg);

	      /* Now load from the memory location into our destination
		 register.  */
	      operands[1] = gen_rtx_MEM (Pmode, operands[0]);
	      emit_move_sequence (operands, mode, scratch_reg);

	      /* And add back in the constant part.  */
	      if (const_part != NULL_RTX)
		expand_inc (operand0, const_part);

	      return 1;
	    }

	  if (flag_pic)
	    {
	      rtx temp;

	      if (reload_in_progress || reload_completed)
		{
		  temp = scratch_reg ? scratch_reg : operand0;
		  /* TEMP will hold an address and maybe the actual
		     data.  We want it in WORD_MODE regardless of what mode it
		     was originally given to us.  */
		  temp = force_mode (word_mode, temp);
		}
	      else
		temp = gen_reg_rtx (Pmode);

	      /* (const (plus (symbol) (const_int))) must be forced to
		 memory during/after reload if the const_int will not fit
		 in 14 bits.  */
	      if (GET_CODE (operand1) == CONST
		       && GET_CODE (XEXP (operand1, 0)) == PLUS
		       && GET_CODE (XEXP (XEXP (operand1, 0), 1)) == CONST_INT
		       && !INT_14_BITS (XEXP (XEXP (operand1, 0), 1))
		       && (reload_completed || reload_in_progress)
		       && flag_pic)
		{
		  rtx const_mem = force_const_mem (mode, operand1);
		  operands[1] = legitimize_pic_address (XEXP (const_mem, 0),
							mode, temp);
		  operands[1] = replace_equiv_address (const_mem, operands[1]);
		  emit_move_sequence (operands, mode, temp);
		}
	      else
		{
		  operands[1] = legitimize_pic_address (operand1, mode, temp);
		  if (REG_P (operand0) && REG_P (operands[1]))
		    copy_reg_pointer (operand0, operands[1]);
		  emit_insn (gen_rtx_SET (VOIDmode, operand0, operands[1]));
		}
	    }
	  /* On the HPPA, references to data space are supposed to use dp,
	     register 27, but showing it in the RTL inhibits various cse
	     and loop optimizations.  */
	  else
	    {
	      rtx temp, set;

	      if (reload_in_progress || reload_completed)
		{
		  temp = scratch_reg ? scratch_reg : operand0;
		  /* TEMP will hold an address and maybe the actual
		     data.  We want it in WORD_MODE regardless of what mode it
		     was originally given to us.  */
		  temp = force_mode (word_mode, temp);
		}
	      else
		temp = gen_reg_rtx (mode);

	      /* Loading a SYMBOL_REF into a register makes that register
		 safe to be used as the base in an indexed address.

		 Don't mark hard registers though.  That loses.  */
	      if (GET_CODE (operand0) == REG
		  && REGNO (operand0) >= FIRST_PSEUDO_REGISTER)
		mark_reg_pointer (operand0, BITS_PER_UNIT);
	      if (REGNO (temp) >= FIRST_PSEUDO_REGISTER)
		mark_reg_pointer (temp, BITS_PER_UNIT);

	      if (ishighonly)
		set = gen_rtx_SET (mode, operand0, temp);
	      else
		set = gen_rtx_SET (VOIDmode,
				   operand0,
				   gen_rtx_LO_SUM (mode, temp, operand1));

	      emit_insn (gen_rtx_SET (VOIDmode,
				      temp,
				      gen_rtx_HIGH (mode, operand1)));
	      emit_insn (set);

	    }
	  return 1;
	}
      else if (pa_tls_referenced_p (operand1))
	{
	  rtx tmp = operand1;
	  rtx addend = NULL;

	  if (GET_CODE (tmp) == CONST && GET_CODE (XEXP (tmp, 0)) == PLUS)
	    {
	      addend = XEXP (XEXP (tmp, 0), 1);
	      tmp = XEXP (XEXP (tmp, 0), 0);
	    }

	  gcc_assert (GET_CODE (tmp) == SYMBOL_REF);
	  tmp = legitimize_tls_address (tmp);
	  if (addend)
	    {
	      tmp = gen_rtx_PLUS (mode, tmp, addend);
	      tmp = force_operand (tmp, operands[0]);
	    }
	  operands[1] = tmp;
	}
      else if (GET_CODE (operand1) != CONST_INT
	       || !cint_ok_for_move (INTVAL (operand1)))
	{
	  rtx insn, temp;
	  rtx op1 = operand1;
	  HOST_WIDE_INT value = 0;
	  HOST_WIDE_INT insv = 0;
	  int insert = 0;

	  if (GET_CODE (operand1) == CONST_INT)
	    value = INTVAL (operand1);

	  if (TARGET_64BIT
	      && GET_CODE (operand1) == CONST_INT
	      && HOST_BITS_PER_WIDE_INT > 32
	      && GET_MODE_BITSIZE (GET_MODE (operand0)) > 32)
	    {
	      HOST_WIDE_INT nval;

	      /* Extract the low order 32 bits of the value and sign extend.
		 If the new value is the same as the original value, we can
		 can use the original value as-is.  If the new value is
		 different, we use it and insert the most-significant 32-bits
		 of the original value into the final result.  */
	      nval = ((value & (((HOST_WIDE_INT) 2 << 31) - 1))
		      ^ ((HOST_WIDE_INT) 1 << 31)) - ((HOST_WIDE_INT) 1 << 31);
	      if (value != nval)
		{
#if HOST_BITS_PER_WIDE_INT > 32
		  insv = value >= 0 ? value >> 32 : ~(~value >> 32);
#endif
		  insert = 1;
		  value = nval;
		  operand1 = GEN_INT (nval);
		}
	    }

	  if (reload_in_progress || reload_completed)
	    temp = scratch_reg ? scratch_reg : operand0;
	  else
	    temp = gen_reg_rtx (mode);

	  /* We don't directly split DImode constants on 32-bit targets
	     because PLUS uses an 11-bit immediate and the insn sequence
	     generated is not as efficient as the one using HIGH/LO_SUM.  */
	  if (GET_CODE (operand1) == CONST_INT
	      && GET_MODE_BITSIZE (mode) <= BITS_PER_WORD
	      && GET_MODE_BITSIZE (mode) <= HOST_BITS_PER_WIDE_INT
	      && !insert)
	    {
	      /* Directly break constant into high and low parts.  This
		 provides better optimization opportunities because various
		 passes recognize constants split with PLUS but not LO_SUM.
		 We use a 14-bit signed low part except when the addition
		 of 0x4000 to the high part might change the sign of the
		 high part.  */
	      HOST_WIDE_INT low = value & 0x3fff;
	      HOST_WIDE_INT high = value & ~ 0x3fff;

	      if (low >= 0x2000)
		{
		  if (high == 0x7fffc000 || (mode == HImode && high == 0x4000))
		    high += 0x2000;
		  else
		    high += 0x4000;
		}

	      low = value - high;

	      emit_insn (gen_rtx_SET (VOIDmode, temp, GEN_INT (high)));
	      operands[1] = gen_rtx_PLUS (mode, temp, GEN_INT (low));
	    }
	  else
	    {
	      emit_insn (gen_rtx_SET (VOIDmode, temp,
				      gen_rtx_HIGH (mode, operand1)));
	      operands[1] = gen_rtx_LO_SUM (mode, temp, operand1);
	    }

	  insn = emit_move_insn (operands[0], operands[1]);

	  /* Now insert the most significant 32 bits of the value
	     into the register.  When we don't have a second register
	     available, it could take up to nine instructions to load
	     a 64-bit integer constant.  Prior to reload, we force
	     constants that would take more than three instructions
	     to load to the constant pool.  During and after reload,
	     we have to handle all possible values.  */
	  if (insert)
	    {
	      /* Use a HIGH/LO_SUM/INSV sequence if we have a second
		 register and the value to be inserted is outside the
		 range that can be loaded with three depdi instructions.  */
	      if (temp != operand0 && (insv >= 16384 || insv < -16384))
		{
		  operand1 = GEN_INT (insv);

		  emit_insn (gen_rtx_SET (VOIDmode, temp,
					  gen_rtx_HIGH (mode, operand1)));
		  emit_move_insn (temp, gen_rtx_LO_SUM (mode, temp, operand1));
		  emit_insn (gen_insv (operand0, GEN_INT (32),
				       const0_rtx, temp));
		}
	      else
		{
		  int len = 5, pos = 27;

		  /* Insert the bits using the depdi instruction.  */
		  while (pos >= 0)
		    {
		      HOST_WIDE_INT v5 = ((insv & 31) ^ 16) - 16;
		      HOST_WIDE_INT sign = v5 < 0;

		      /* Left extend the insertion.  */
		      insv = (insv >= 0 ? insv >> len : ~(~insv >> len));
		      while (pos > 0 && (insv & 1) == sign)
			{
			  insv = (insv >= 0 ? insv >> 1 : ~(~insv >> 1));
			  len += 1;
			  pos -= 1;
			}

		      emit_insn (gen_insv (operand0, GEN_INT (len),
					   GEN_INT (pos), GEN_INT (v5)));

		      len = pos > 0 && pos < 5 ? pos : 5;
		      pos -= len;
		    }
		}
	    }

	  REG_NOTES (insn)
	    = gen_rtx_EXPR_LIST (REG_EQUAL, op1, REG_NOTES (insn));

	  return 1;
	}
    }
  /* Now have insn-emit do whatever it normally does.  */
  return 0;
}

/* Examine EXP and return nonzero if it contains an ADDR_EXPR (meaning
   it will need a link/runtime reloc).  */

int
reloc_needed (tree exp)
{
  int reloc = 0;

  switch (TREE_CODE (exp))
    {
    case ADDR_EXPR:
      return 1;

    case PLUS_EXPR:
    case MINUS_EXPR:
      reloc = reloc_needed (TREE_OPERAND (exp, 0));
      reloc |= reloc_needed (TREE_OPERAND (exp, 1));
      break;

    case NOP_EXPR:
    case CONVERT_EXPR:
    case NON_LVALUE_EXPR:
      reloc = reloc_needed (TREE_OPERAND (exp, 0));
      break;

    case CONSTRUCTOR:
      {
	tree value;
	unsigned HOST_WIDE_INT ix;

	FOR_EACH_CONSTRUCTOR_VALUE (CONSTRUCTOR_ELTS (exp), ix, value)
	  if (value)
	    reloc |= reloc_needed (value);
      }
      break;

    case ERROR_MARK:
      break;

    default:
      break;
    }
  return reloc;
}

/* Does operand (which is a symbolic_operand) live in text space?
   If so, SYMBOL_REF_FLAG, which is set by pa_encode_section_info,
   will be true.  */

int
read_only_operand (rtx operand, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  if (GET_CODE (operand) == CONST)
    operand = XEXP (XEXP (operand, 0), 0);
  if (flag_pic)
    {
      if (GET_CODE (operand) == SYMBOL_REF)
	return SYMBOL_REF_FLAG (operand) && !CONSTANT_POOL_ADDRESS_P (operand);
    }
  else
    {
      if (GET_CODE (operand) == SYMBOL_REF)
	return SYMBOL_REF_FLAG (operand) || CONSTANT_POOL_ADDRESS_P (operand);
    }
  return 1;
}


/* Return the best assembler insn template
   for moving operands[1] into operands[0] as a fullword.  */
const char *
singlemove_string (rtx *operands)
{
  HOST_WIDE_INT intval;

  if (GET_CODE (operands[0]) == MEM)
    return "stw %r1,%0";
  if (GET_CODE (operands[1]) == MEM)
    return "ldw %1,%0";
  if (GET_CODE (operands[1]) == CONST_DOUBLE)
    {
      long i;
      REAL_VALUE_TYPE d;

      gcc_assert (GET_MODE (operands[1]) == SFmode);

      /* Translate the CONST_DOUBLE to a CONST_INT with the same target
	 bit pattern.  */
      REAL_VALUE_FROM_CONST_DOUBLE (d, operands[1]);
      REAL_VALUE_TO_TARGET_SINGLE (d, i);

      operands[1] = GEN_INT (i);
      /* Fall through to CONST_INT case.  */
    }
  if (GET_CODE (operands[1]) == CONST_INT)
    {
      intval = INTVAL (operands[1]);

      if (VAL_14_BITS_P (intval))
	return "ldi %1,%0";
      else if ((intval & 0x7ff) == 0)
	return "ldil L'%1,%0";
      else if (zdepi_cint_p (intval))
	return "{zdepi %Z1,%0|depwi,z %Z1,%0}";
      else
	return "ldil L'%1,%0\n\tldo R'%1(%0),%0";
    }
  return "copy %1,%0";
}


/* Compute position (in OP[1]) and width (in OP[2])
   useful for copying IMM to a register using the zdepi
   instructions.  Store the immediate value to insert in OP[0].  */
static void
compute_zdepwi_operands (unsigned HOST_WIDE_INT imm, unsigned *op)
{
  int lsb, len;

  /* Find the least significant set bit in IMM.  */
  for (lsb = 0; lsb < 32; lsb++)
    {
      if ((imm & 1) != 0)
        break;
      imm >>= 1;
    }

  /* Choose variants based on *sign* of the 5-bit field.  */
  if ((imm & 0x10) == 0)
    len = (lsb <= 28) ? 4 : 32 - lsb;
  else
    {
      /* Find the width of the bitstring in IMM.  */
      for (len = 5; len < 32; len++)
	{
	  if ((imm & (1 << len)) == 0)
	    break;
	}

      /* Sign extend IMM as a 5-bit value.  */
      imm = (imm & 0xf) - 0x10;
    }

  op[0] = imm;
  op[1] = 31 - lsb;
  op[2] = len;
}

/* Compute position (in OP[1]) and width (in OP[2])
   useful for copying IMM to a register using the depdi,z
   instructions.  Store the immediate value to insert in OP[0].  */
void
compute_zdepdi_operands (unsigned HOST_WIDE_INT imm, unsigned *op)
{
  HOST_WIDE_INT lsb, len;

  /* Find the least significant set bit in IMM.  */
  for (lsb = 0; lsb < HOST_BITS_PER_WIDE_INT; lsb++)
    {
      if ((imm & 1) != 0)
        break;
      imm >>= 1;
    }

  /* Choose variants based on *sign* of the 5-bit field.  */
  if ((imm & 0x10) == 0)
    len = ((lsb <= HOST_BITS_PER_WIDE_INT - 4)
	   ? 4 : HOST_BITS_PER_WIDE_INT - lsb);
  else
    {
      /* Find the width of the bitstring in IMM.  */
      for (len = 5; len < HOST_BITS_PER_WIDE_INT; len++)
	{
	  if ((imm & ((unsigned HOST_WIDE_INT) 1 << len)) == 0)
	    break;
	}

      /* Sign extend IMM as a 5-bit value.  */
      imm = (imm & 0xf) - 0x10;
    }

  op[0] = imm;
  op[1] = 63 - lsb;
  op[2] = len;
}

/* Output assembler code to perform a doubleword move insn
   with operands OPERANDS.  */

const char *
output_move_double (rtx *operands)
{
  enum { REGOP, OFFSOP, MEMOP, CNSTOP, RNDOP } optype0, optype1;
  rtx latehalf[2];
  rtx addreg0 = 0, addreg1 = 0;

  /* First classify both operands.  */

  if (REG_P (operands[0]))
    optype0 = REGOP;
  else if (offsettable_memref_p (operands[0]))
    optype0 = OFFSOP;
  else if (GET_CODE (operands[0]) == MEM)
    optype0 = MEMOP;
  else
    optype0 = RNDOP;

  if (REG_P (operands[1]))
    optype1 = REGOP;
  else if (CONSTANT_P (operands[1]))
    optype1 = CNSTOP;
  else if (offsettable_memref_p (operands[1]))
    optype1 = OFFSOP;
  else if (GET_CODE (operands[1]) == MEM)
    optype1 = MEMOP;
  else
    optype1 = RNDOP;

  /* Check for the cases that the operand constraints are not
     supposed to allow to happen.  */
  gcc_assert (optype0 == REGOP || optype1 == REGOP);

  /* Handle copies between general and floating registers.  */

  if (optype0 == REGOP && optype1 == REGOP
      && FP_REG_P (operands[0]) ^ FP_REG_P (operands[1]))
    {
      if (FP_REG_P (operands[0]))
	{
	  output_asm_insn ("{stws|stw} %1,-16(%%sp)", operands);
	  output_asm_insn ("{stws|stw} %R1,-12(%%sp)", operands);
	  return "{fldds|fldd} -16(%%sp),%0";
	}
      else
	{
	  output_asm_insn ("{fstds|fstd} %1,-16(%%sp)", operands);
	  output_asm_insn ("{ldws|ldw} -16(%%sp),%0", operands);
	  return "{ldws|ldw} -12(%%sp),%R0";
	}
    }

   /* Handle auto decrementing and incrementing loads and stores
     specifically, since the structure of the function doesn't work
     for them without major modification.  Do it better when we learn
     this port about the general inc/dec addressing of PA.
     (This was written by tege.  Chide him if it doesn't work.)  */

  if (optype0 == MEMOP)
    {
      /* We have to output the address syntax ourselves, since print_operand
	 doesn't deal with the addresses we want to use.  Fix this later.  */

      rtx addr = XEXP (operands[0], 0);
      if (GET_CODE (addr) == POST_INC || GET_CODE (addr) == POST_DEC)
	{
	  rtx high_reg = gen_rtx_SUBREG (SImode, operands[1], 0);

	  operands[0] = XEXP (addr, 0);
	  gcc_assert (GET_CODE (operands[1]) == REG
		      && GET_CODE (operands[0]) == REG);

	  gcc_assert (!reg_overlap_mentioned_p (high_reg, addr));
	  
	  /* No overlap between high target register and address
	     register.  (We do this in a non-obvious way to
	     save a register file writeback)  */
	  if (GET_CODE (addr) == POST_INC)
	    return "{stws|stw},ma %1,8(%0)\n\tstw %R1,-4(%0)";
	  return "{stws|stw},ma %1,-8(%0)\n\tstw %R1,12(%0)";
	}
      else if (GET_CODE (addr) == PRE_INC || GET_CODE (addr) == PRE_DEC)
	{
	  rtx high_reg = gen_rtx_SUBREG (SImode, operands[1], 0);

	  operands[0] = XEXP (addr, 0);
	  gcc_assert (GET_CODE (operands[1]) == REG
		      && GET_CODE (operands[0]) == REG);
	  
	  gcc_assert (!reg_overlap_mentioned_p (high_reg, addr));
	  /* No overlap between high target register and address
	     register.  (We do this in a non-obvious way to save a
	     register file writeback)  */
	  if (GET_CODE (addr) == PRE_INC)
	    return "{stws|stw},mb %1,8(%0)\n\tstw %R1,4(%0)";
	  return "{stws|stw},mb %1,-8(%0)\n\tstw %R1,4(%0)";
	}
    }
  if (optype1 == MEMOP)
    {
      /* We have to output the address syntax ourselves, since print_operand
	 doesn't deal with the addresses we want to use.  Fix this later.  */

      rtx addr = XEXP (operands[1], 0);
      if (GET_CODE (addr) == POST_INC || GET_CODE (addr) == POST_DEC)
	{
	  rtx high_reg = gen_rtx_SUBREG (SImode, operands[0], 0);

	  operands[1] = XEXP (addr, 0);
	  gcc_assert (GET_CODE (operands[0]) == REG
		      && GET_CODE (operands[1]) == REG);

	  if (!reg_overlap_mentioned_p (high_reg, addr))
	    {
	      /* No overlap between high target register and address
		 register.  (We do this in a non-obvious way to
		 save a register file writeback)  */
	      if (GET_CODE (addr) == POST_INC)
		return "{ldws|ldw},ma 8(%1),%0\n\tldw -4(%1),%R0";
	      return "{ldws|ldw},ma -8(%1),%0\n\tldw 12(%1),%R0";
	    }
	  else
	    {
	      /* This is an undefined situation.  We should load into the
		 address register *and* update that register.  Probably
		 we don't need to handle this at all.  */
	      if (GET_CODE (addr) == POST_INC)
		return "ldw 4(%1),%R0\n\t{ldws|ldw},ma 8(%1),%0";
	      return "ldw 4(%1),%R0\n\t{ldws|ldw},ma -8(%1),%0";
	    }
	}
      else if (GET_CODE (addr) == PRE_INC || GET_CODE (addr) == PRE_DEC)
	{
	  rtx high_reg = gen_rtx_SUBREG (SImode, operands[0], 0);

	  operands[1] = XEXP (addr, 0);
	  gcc_assert (GET_CODE (operands[0]) == REG
		      && GET_CODE (operands[1]) == REG);

	  if (!reg_overlap_mentioned_p (high_reg, addr))
	    {
	      /* No overlap between high target register and address
		 register.  (We do this in a non-obvious way to
		 save a register file writeback)  */
	      if (GET_CODE (addr) == PRE_INC)
		return "{ldws|ldw},mb 8(%1),%0\n\tldw 4(%1),%R0";
	      return "{ldws|ldw},mb -8(%1),%0\n\tldw 4(%1),%R0";
	    }
	  else
	    {
	      /* This is an undefined situation.  We should load into the
		 address register *and* update that register.  Probably
		 we don't need to handle this at all.  */
	      if (GET_CODE (addr) == PRE_INC)
		return "ldw 12(%1),%R0\n\t{ldws|ldw},mb 8(%1),%0";
	      return "ldw -4(%1),%R0\n\t{ldws|ldw},mb -8(%1),%0";
	    }
	}
      else if (GET_CODE (addr) == PLUS
	       && GET_CODE (XEXP (addr, 0)) == MULT)
	{
	  rtx xoperands[4];
	  rtx high_reg = gen_rtx_SUBREG (SImode, operands[0], 0);

	  if (!reg_overlap_mentioned_p (high_reg, addr))
	    {
	      xoperands[0] = high_reg;
	      xoperands[1] = XEXP (addr, 1);
	      xoperands[2] = XEXP (XEXP (addr, 0), 0);
	      xoperands[3] = XEXP (XEXP (addr, 0), 1);
	      output_asm_insn ("{sh%O3addl %2,%1,%0|shladd,l %2,%O3,%1,%0}",
			       xoperands);
	      return "ldw 4(%0),%R0\n\tldw 0(%0),%0";
	    }
	  else
	    {
	      xoperands[0] = high_reg;
	      xoperands[1] = XEXP (addr, 1);
	      xoperands[2] = XEXP (XEXP (addr, 0), 0);
	      xoperands[3] = XEXP (XEXP (addr, 0), 1);
	      output_asm_insn ("{sh%O3addl %2,%1,%R0|shladd,l %2,%O3,%1,%R0}",
			       xoperands);
	      return "ldw 0(%R0),%0\n\tldw 4(%R0),%R0";
	    }
	}
    }

  /* If an operand is an unoffsettable memory ref, find a register
     we can increment temporarily to make it refer to the second word.  */

  if (optype0 == MEMOP)
    addreg0 = find_addr_reg (XEXP (operands[0], 0));

  if (optype1 == MEMOP)
    addreg1 = find_addr_reg (XEXP (operands[1], 0));

  /* Ok, we can do one word at a time.
     Normally we do the low-numbered word first.

     In either case, set up in LATEHALF the operands to use
     for the high-numbered word and in some cases alter the
     operands in OPERANDS to be suitable for the low-numbered word.  */

  if (optype0 == REGOP)
    latehalf[0] = gen_rtx_REG (SImode, REGNO (operands[0]) + 1);
  else if (optype0 == OFFSOP)
    latehalf[0] = adjust_address (operands[0], SImode, 4);
  else
    latehalf[0] = operands[0];

  if (optype1 == REGOP)
    latehalf[1] = gen_rtx_REG (SImode, REGNO (operands[1]) + 1);
  else if (optype1 == OFFSOP)
    latehalf[1] = adjust_address (operands[1], SImode, 4);
  else if (optype1 == CNSTOP)
    split_double (operands[1], &operands[1], &latehalf[1]);
  else
    latehalf[1] = operands[1];

  /* If the first move would clobber the source of the second one,
     do them in the other order.

     This can happen in two cases:

	mem -> register where the first half of the destination register
 	is the same register used in the memory's address.  Reload
	can create such insns.

	mem in this case will be either register indirect or register
	indirect plus a valid offset.

	register -> register move where REGNO(dst) == REGNO(src + 1)
	someone (Tim/Tege?) claimed this can happen for parameter loads.

     Handle mem -> register case first.  */
  if (optype0 == REGOP
      && (optype1 == MEMOP || optype1 == OFFSOP)
      && refers_to_regno_p (REGNO (operands[0]), REGNO (operands[0]) + 1,
			    operands[1], 0))
    {
      /* Do the late half first.  */
      if (addreg1)
	output_asm_insn ("ldo 4(%0),%0", &addreg1);
      output_asm_insn (singlemove_string (latehalf), latehalf);

      /* Then clobber.  */
      if (addreg1)
	output_asm_insn ("ldo -4(%0),%0", &addreg1);
      return singlemove_string (operands);
    }

  /* Now handle register -> register case.  */
  if (optype0 == REGOP && optype1 == REGOP
      && REGNO (operands[0]) == REGNO (operands[1]) + 1)
    {
      output_asm_insn (singlemove_string (latehalf), latehalf);
      return singlemove_string (operands);
    }

  /* Normal case: do the two words, low-numbered first.  */

  output_asm_insn (singlemove_string (operands), operands);

  /* Make any unoffsettable addresses point at high-numbered word.  */
  if (addreg0)
    output_asm_insn ("ldo 4(%0),%0", &addreg0);
  if (addreg1)
    output_asm_insn ("ldo 4(%0),%0", &addreg1);

  /* Do that word.  */
  output_asm_insn (singlemove_string (latehalf), latehalf);

  /* Undo the adds we just did.  */
  if (addreg0)
    output_asm_insn ("ldo -4(%0),%0", &addreg0);
  if (addreg1)
    output_asm_insn ("ldo -4(%0),%0", &addreg1);

  return "";
}

const char *
output_fp_move_double (rtx *operands)
{
  if (FP_REG_P (operands[0]))
    {
      if (FP_REG_P (operands[1])
	  || operands[1] == CONST0_RTX (GET_MODE (operands[0])))
	output_asm_insn ("fcpy,dbl %f1,%0", operands);
      else
	output_asm_insn ("fldd%F1 %1,%0", operands);
    }
  else if (FP_REG_P (operands[1]))
    {
      output_asm_insn ("fstd%F0 %1,%0", operands);
    }
  else
    {
      rtx xoperands[2];
      
      gcc_assert (operands[1] == CONST0_RTX (GET_MODE (operands[0])));
      
      /* This is a pain.  You have to be prepared to deal with an
	 arbitrary address here including pre/post increment/decrement.

	 so avoid this in the MD.  */
      gcc_assert (GET_CODE (operands[0]) == REG);
      
      xoperands[1] = gen_rtx_REG (SImode, REGNO (operands[0]) + 1);
      xoperands[0] = operands[0];
      output_asm_insn ("copy %%r0,%0\n\tcopy %%r0,%1", xoperands);
    }
  return "";
}

/* Return a REG that occurs in ADDR with coefficient 1.
   ADDR can be effectively incremented by incrementing REG.  */

static rtx
find_addr_reg (rtx addr)
{
  while (GET_CODE (addr) == PLUS)
    {
      if (GET_CODE (XEXP (addr, 0)) == REG)
	addr = XEXP (addr, 0);
      else if (GET_CODE (XEXP (addr, 1)) == REG)
	addr = XEXP (addr, 1);
      else if (CONSTANT_P (XEXP (addr, 0)))
	addr = XEXP (addr, 1);
      else if (CONSTANT_P (XEXP (addr, 1)))
	addr = XEXP (addr, 0);
      else
	gcc_unreachable ();
    }
  gcc_assert (GET_CODE (addr) == REG);
  return addr;
}

/* Emit code to perform a block move.

   OPERANDS[0] is the destination pointer as a REG, clobbered.
   OPERANDS[1] is the source pointer as a REG, clobbered.
   OPERANDS[2] is a register for temporary storage.
   OPERANDS[3] is a register for temporary storage.
   OPERANDS[4] is the size as a CONST_INT
   OPERANDS[5] is the alignment safe to use, as a CONST_INT.
   OPERANDS[6] is another temporary register.  */

const char *
output_block_move (rtx *operands, int size_is_constant ATTRIBUTE_UNUSED)
{
  int align = INTVAL (operands[5]);
  unsigned long n_bytes = INTVAL (operands[4]);

  /* We can't move more than a word at a time because the PA
     has no longer integer move insns.  (Could use fp mem ops?)  */
  if (align > (TARGET_64BIT ? 8 : 4))
    align = (TARGET_64BIT ? 8 : 4);

  /* Note that we know each loop below will execute at least twice
     (else we would have open-coded the copy).  */
  switch (align)
    {
      case 8:
	/* Pre-adjust the loop counter.  */
	operands[4] = GEN_INT (n_bytes - 16);
	output_asm_insn ("ldi %4,%2", operands);

	/* Copying loop.  */
	output_asm_insn ("ldd,ma 8(%1),%3", operands);
	output_asm_insn ("ldd,ma 8(%1),%6", operands);
	output_asm_insn ("std,ma %3,8(%0)", operands);
	output_asm_insn ("addib,>= -16,%2,.-12", operands);
	output_asm_insn ("std,ma %6,8(%0)", operands);

	/* Handle the residual.  There could be up to 7 bytes of
	   residual to copy!  */
	if (n_bytes % 16 != 0)
	  {
	    operands[4] = GEN_INT (n_bytes % 8);
	    if (n_bytes % 16 >= 8)
	      output_asm_insn ("ldd,ma 8(%1),%3", operands);
	    if (n_bytes % 8 != 0)
	      output_asm_insn ("ldd 0(%1),%6", operands);
	    if (n_bytes % 16 >= 8)
	      output_asm_insn ("std,ma %3,8(%0)", operands);
	    if (n_bytes % 8 != 0)
	      output_asm_insn ("stdby,e %6,%4(%0)", operands);
	  }
	return "";

      case 4:
	/* Pre-adjust the loop counter.  */
	operands[4] = GEN_INT (n_bytes - 8);
	output_asm_insn ("ldi %4,%2", operands);

	/* Copying loop.  */
	output_asm_insn ("{ldws|ldw},ma 4(%1),%3", operands);
	output_asm_insn ("{ldws|ldw},ma 4(%1),%6", operands);
	output_asm_insn ("{stws|stw},ma %3,4(%0)", operands);
	output_asm_insn ("addib,>= -8,%2,.-12", operands);
	output_asm_insn ("{stws|stw},ma %6,4(%0)", operands);

	/* Handle the residual.  There could be up to 7 bytes of
	   residual to copy!  */
	if (n_bytes % 8 != 0)
	  {
	    operands[4] = GEN_INT (n_bytes % 4);
	    if (n_bytes % 8 >= 4)
	      output_asm_insn ("{ldws|ldw},ma 4(%1),%3", operands);
	    if (n_bytes % 4 != 0)
	      output_asm_insn ("ldw 0(%1),%6", operands);
	    if (n_bytes % 8 >= 4)
	      output_asm_insn ("{stws|stw},ma %3,4(%0)", operands);
	    if (n_bytes % 4 != 0)
	      output_asm_insn ("{stbys|stby},e %6,%4(%0)", operands);
	  }
	return "";

      case 2:
	/* Pre-adjust the loop counter.  */
	operands[4] = GEN_INT (n_bytes - 4);
	output_asm_insn ("ldi %4,%2", operands);

	/* Copying loop.  */
	output_asm_insn ("{ldhs|ldh},ma 2(%1),%3", operands);
	output_asm_insn ("{ldhs|ldh},ma 2(%1),%6", operands);
	output_asm_insn ("{sths|sth},ma %3,2(%0)", operands);
	output_asm_insn ("addib,>= -4,%2,.-12", operands);
	output_asm_insn ("{sths|sth},ma %6,2(%0)", operands);

	/* Handle the residual.  */
	if (n_bytes % 4 != 0)
	  {
	    if (n_bytes % 4 >= 2)
	      output_asm_insn ("{ldhs|ldh},ma 2(%1),%3", operands);
	    if (n_bytes % 2 != 0)
	      output_asm_insn ("ldb 0(%1),%6", operands);
	    if (n_bytes % 4 >= 2)
	      output_asm_insn ("{sths|sth},ma %3,2(%0)", operands);
	    if (n_bytes % 2 != 0)
	      output_asm_insn ("stb %6,0(%0)", operands);
	  }
	return "";

      case 1:
	/* Pre-adjust the loop counter.  */
	operands[4] = GEN_INT (n_bytes - 2);
	output_asm_insn ("ldi %4,%2", operands);

	/* Copying loop.  */
	output_asm_insn ("{ldbs|ldb},ma 1(%1),%3", operands);
	output_asm_insn ("{ldbs|ldb},ma 1(%1),%6", operands);
	output_asm_insn ("{stbs|stb},ma %3,1(%0)", operands);
	output_asm_insn ("addib,>= -2,%2,.-12", operands);
	output_asm_insn ("{stbs|stb},ma %6,1(%0)", operands);

	/* Handle the residual.  */
	if (n_bytes % 2 != 0)
	  {
	    output_asm_insn ("ldb 0(%1),%3", operands);
	    output_asm_insn ("stb %3,0(%0)", operands);
	  }
	return "";

      default:
	gcc_unreachable ();
    }
}

/* Count the number of insns necessary to handle this block move.

   Basic structure is the same as emit_block_move, except that we
   count insns rather than emit them.  */

static int
compute_movmem_length (rtx insn)
{
  rtx pat = PATTERN (insn);
  unsigned int align = INTVAL (XEXP (XVECEXP (pat, 0, 7), 0));
  unsigned long n_bytes = INTVAL (XEXP (XVECEXP (pat, 0, 6), 0));
  unsigned int n_insns = 0;

  /* We can't move more than four bytes at a time because the PA
     has no longer integer move insns.  (Could use fp mem ops?)  */
  if (align > (TARGET_64BIT ? 8 : 4))
    align = (TARGET_64BIT ? 8 : 4);

  /* The basic copying loop.  */
  n_insns = 6;

  /* Residuals.  */
  if (n_bytes % (2 * align) != 0)
    {
      if ((n_bytes % (2 * align)) >= align)
	n_insns += 2;

      if ((n_bytes % align) != 0)
	n_insns += 2;
    }

  /* Lengths are expressed in bytes now; each insn is 4 bytes.  */
  return n_insns * 4;
}

/* Emit code to perform a block clear.

   OPERANDS[0] is the destination pointer as a REG, clobbered.
   OPERANDS[1] is a register for temporary storage.
   OPERANDS[2] is the size as a CONST_INT
   OPERANDS[3] is the alignment safe to use, as a CONST_INT.  */

const char *
output_block_clear (rtx *operands, int size_is_constant ATTRIBUTE_UNUSED)
{
  int align = INTVAL (operands[3]);
  unsigned long n_bytes = INTVAL (operands[2]);

  /* We can't clear more than a word at a time because the PA
     has no longer integer move insns.  */
  if (align > (TARGET_64BIT ? 8 : 4))
    align = (TARGET_64BIT ? 8 : 4);

  /* Note that we know each loop below will execute at least twice
     (else we would have open-coded the copy).  */
  switch (align)
    {
      case 8:
	/* Pre-adjust the loop counter.  */
	operands[2] = GEN_INT (n_bytes - 16);
	output_asm_insn ("ldi %2,%1", operands);

	/* Loop.  */
	output_asm_insn ("std,ma %%r0,8(%0)", operands);
	output_asm_insn ("addib,>= -16,%1,.-4", operands);
	output_asm_insn ("std,ma %%r0,8(%0)", operands);

	/* Handle the residual.  There could be up to 7 bytes of
	   residual to copy!  */
	if (n_bytes % 16 != 0)
	  {
	    operands[2] = GEN_INT (n_bytes % 8);
	    if (n_bytes % 16 >= 8)
	      output_asm_insn ("std,ma %%r0,8(%0)", operands);
	    if (n_bytes % 8 != 0)
	      output_asm_insn ("stdby,e %%r0,%2(%0)", operands);
	  }
	return "";

      case 4:
	/* Pre-adjust the loop counter.  */
	operands[2] = GEN_INT (n_bytes - 8);
	output_asm_insn ("ldi %2,%1", operands);

	/* Loop.  */
	output_asm_insn ("{stws|stw},ma %%r0,4(%0)", operands);
	output_asm_insn ("addib,>= -8,%1,.-4", operands);
	output_asm_insn ("{stws|stw},ma %%r0,4(%0)", operands);

	/* Handle the residual.  There could be up to 7 bytes of
	   residual to copy!  */
	if (n_bytes % 8 != 0)
	  {
	    operands[2] = GEN_INT (n_bytes % 4);
	    if (n_bytes % 8 >= 4)
	      output_asm_insn ("{stws|stw},ma %%r0,4(%0)", operands);
	    if (n_bytes % 4 != 0)
	      output_asm_insn ("{stbys|stby},e %%r0,%2(%0)", operands);
	  }
	return "";

      case 2:
	/* Pre-adjust the loop counter.  */
	operands[2] = GEN_INT (n_bytes - 4);
	output_asm_insn ("ldi %2,%1", operands);

	/* Loop.  */
	output_asm_insn ("{sths|sth},ma %%r0,2(%0)", operands);
	output_asm_insn ("addib,>= -4,%1,.-4", operands);
	output_asm_insn ("{sths|sth},ma %%r0,2(%0)", operands);

	/* Handle the residual.  */
	if (n_bytes % 4 != 0)
	  {
	    if (n_bytes % 4 >= 2)
	      output_asm_insn ("{sths|sth},ma %%r0,2(%0)", operands);
	    if (n_bytes % 2 != 0)
	      output_asm_insn ("stb %%r0,0(%0)", operands);
	  }
	return "";

      case 1:
	/* Pre-adjust the loop counter.  */
	operands[2] = GEN_INT (n_bytes - 2);
	output_asm_insn ("ldi %2,%1", operands);

	/* Loop.  */
	output_asm_insn ("{stbs|stb},ma %%r0,1(%0)", operands);
	output_asm_insn ("addib,>= -2,%1,.-4", operands);
	output_asm_insn ("{stbs|stb},ma %%r0,1(%0)", operands);

	/* Handle the residual.  */
	if (n_bytes % 2 != 0)
	  output_asm_insn ("stb %%r0,0(%0)", operands);

	return "";

      default:
	gcc_unreachable ();
    }
}

/* Count the number of insns necessary to handle this block move.

   Basic structure is the same as emit_block_move, except that we
   count insns rather than emit them.  */

static int
compute_clrmem_length (rtx insn)
{
  rtx pat = PATTERN (insn);
  unsigned int align = INTVAL (XEXP (XVECEXP (pat, 0, 4), 0));
  unsigned long n_bytes = INTVAL (XEXP (XVECEXP (pat, 0, 3), 0));
  unsigned int n_insns = 0;

  /* We can't clear more than a word at a time because the PA
     has no longer integer move insns.  */
  if (align > (TARGET_64BIT ? 8 : 4))
    align = (TARGET_64BIT ? 8 : 4);

  /* The basic loop.  */
  n_insns = 4;

  /* Residuals.  */
  if (n_bytes % (2 * align) != 0)
    {
      if ((n_bytes % (2 * align)) >= align)
	n_insns++;

      if ((n_bytes % align) != 0)
	n_insns++;
    }

  /* Lengths are expressed in bytes now; each insn is 4 bytes.  */
  return n_insns * 4;
}


const char *
output_and (rtx *operands)
{
  if (GET_CODE (operands[2]) == CONST_INT && INTVAL (operands[2]) != 0)
    {
      unsigned HOST_WIDE_INT mask = INTVAL (operands[2]);
      int ls0, ls1, ms0, p, len;

      for (ls0 = 0; ls0 < 32; ls0++)
	if ((mask & (1 << ls0)) == 0)
	  break;

      for (ls1 = ls0; ls1 < 32; ls1++)
	if ((mask & (1 << ls1)) != 0)
	  break;

      for (ms0 = ls1; ms0 < 32; ms0++)
	if ((mask & (1 << ms0)) == 0)
	  break;

      gcc_assert (ms0 == 32);

      if (ls1 == 32)
	{
	  len = ls0;

	  gcc_assert (len);

	  operands[2] = GEN_INT (len);
	  return "{extru|extrw,u} %1,31,%2,%0";
	}
      else
	{
	  /* We could use this `depi' for the case above as well, but `depi'
	     requires one more register file access than an `extru'.  */

	  p = 31 - ls0;
	  len = ls1 - ls0;

	  operands[2] = GEN_INT (p);
	  operands[3] = GEN_INT (len);
	  return "{depi|depwi} 0,%2,%3,%0";
	}
    }
  else
    return "and %1,%2,%0";
}

/* Return a string to perform a bitwise-and of operands[1] with operands[2]
   storing the result in operands[0].  */
const char *
output_64bit_and (rtx *operands)
{
  if (GET_CODE (operands[2]) == CONST_INT && INTVAL (operands[2]) != 0)
    {
      unsigned HOST_WIDE_INT mask = INTVAL (operands[2]);
      int ls0, ls1, ms0, p, len;

      for (ls0 = 0; ls0 < HOST_BITS_PER_WIDE_INT; ls0++)
	if ((mask & ((unsigned HOST_WIDE_INT) 1 << ls0)) == 0)
	  break;

      for (ls1 = ls0; ls1 < HOST_BITS_PER_WIDE_INT; ls1++)
	if ((mask & ((unsigned HOST_WIDE_INT) 1 << ls1)) != 0)
	  break;

      for (ms0 = ls1; ms0 < HOST_BITS_PER_WIDE_INT; ms0++)
	if ((mask & ((unsigned HOST_WIDE_INT) 1 << ms0)) == 0)
	  break;

      gcc_assert (ms0 == HOST_BITS_PER_WIDE_INT);

      if (ls1 == HOST_BITS_PER_WIDE_INT)
	{
	  len = ls0;

	  gcc_assert (len);

	  operands[2] = GEN_INT (len);
	  return "extrd,u %1,63,%2,%0";
	}
      else
	{
	  /* We could use this `depi' for the case above as well, but `depi'
	     requires one more register file access than an `extru'.  */

	  p = 63 - ls0;
	  len = ls1 - ls0;

	  operands[2] = GEN_INT (p);
	  operands[3] = GEN_INT (len);
	  return "depdi 0,%2,%3,%0";
	}
    }
  else
    return "and %1,%2,%0";
}

const char *
output_ior (rtx *operands)
{
  unsigned HOST_WIDE_INT mask = INTVAL (operands[2]);
  int bs0, bs1, p, len;

  if (INTVAL (operands[2]) == 0)
    return "copy %1,%0";

  for (bs0 = 0; bs0 < 32; bs0++)
    if ((mask & (1 << bs0)) != 0)
      break;

  for (bs1 = bs0; bs1 < 32; bs1++)
    if ((mask & (1 << bs1)) == 0)
      break;

  gcc_assert (bs1 == 32 || ((unsigned HOST_WIDE_INT) 1 << bs1) > mask);

  p = 31 - bs0;
  len = bs1 - bs0;

  operands[2] = GEN_INT (p);
  operands[3] = GEN_INT (len);
  return "{depi|depwi} -1,%2,%3,%0";
}

/* Return a string to perform a bitwise-and of operands[1] with operands[2]
   storing the result in operands[0].  */
const char *
output_64bit_ior (rtx *operands)
{
  unsigned HOST_WIDE_INT mask = INTVAL (operands[2]);
  int bs0, bs1, p, len;

  if (INTVAL (operands[2]) == 0)
    return "copy %1,%0";

  for (bs0 = 0; bs0 < HOST_BITS_PER_WIDE_INT; bs0++)
    if ((mask & ((unsigned HOST_WIDE_INT) 1 << bs0)) != 0)
      break;

  for (bs1 = bs0; bs1 < HOST_BITS_PER_WIDE_INT; bs1++)
    if ((mask & ((unsigned HOST_WIDE_INT) 1 << bs1)) == 0)
      break;

  gcc_assert (bs1 == HOST_BITS_PER_WIDE_INT
	      || ((unsigned HOST_WIDE_INT) 1 << bs1) > mask);

  p = 63 - bs0;
  len = bs1 - bs0;

  operands[2] = GEN_INT (p);
  operands[3] = GEN_INT (len);
  return "depdi -1,%2,%3,%0";
}

/* Target hook for assembling integer objects.  This code handles
   aligned SI and DI integers specially since function references
   must be preceded by P%.  */

static bool
pa_assemble_integer (rtx x, unsigned int size, int aligned_p)
{
  if (size == UNITS_PER_WORD
      && aligned_p
      && function_label_operand (x, VOIDmode))
    {
      fputs (size == 8? "\t.dword\tP%" : "\t.word\tP%", asm_out_file);
      output_addr_const (asm_out_file, x);
      fputc ('\n', asm_out_file);
      return true;
    }
  return default_assemble_integer (x, size, aligned_p);
}

/* Output an ascii string.  */
void
output_ascii (FILE *file, const char *p, int size)
{
  int i;
  int chars_output;
  unsigned char partial_output[16];	/* Max space 4 chars can occupy.  */

  /* The HP assembler can only take strings of 256 characters at one
     time.  This is a limitation on input line length, *not* the
     length of the string.  Sigh.  Even worse, it seems that the
     restriction is in number of input characters (see \xnn &
     \whatever).  So we have to do this very carefully.  */

  fputs ("\t.STRING \"", file);

  chars_output = 0;
  for (i = 0; i < size; i += 4)
    {
      int co = 0;
      int io = 0;
      for (io = 0, co = 0; io < MIN (4, size - i); io++)
	{
	  register unsigned int c = (unsigned char) p[i + io];

	  if (c == '\"' || c == '\\')
	    partial_output[co++] = '\\';
	  if (c >= ' ' && c < 0177)
	    partial_output[co++] = c;
	  else
	    {
	      unsigned int hexd;
	      partial_output[co++] = '\\';
	      partial_output[co++] = 'x';
	      hexd =  c  / 16 - 0 + '0';
	      if (hexd > '9')
		hexd -= '9' - 'a' + 1;
	      partial_output[co++] = hexd;
	      hexd =  c % 16 - 0 + '0';
	      if (hexd > '9')
		hexd -= '9' - 'a' + 1;
	      partial_output[co++] = hexd;
	    }
	}
      if (chars_output + co > 243)
	{
	  fputs ("\"\n\t.STRING \"", file);
	  chars_output = 0;
	}
      fwrite (partial_output, 1, (size_t) co, file);
      chars_output += co;
      co = 0;
    }
  fputs ("\"\n", file);
}

/* Try to rewrite floating point comparisons & branches to avoid
   useless add,tr insns.

   CHECK_NOTES is nonzero if we should examine REG_DEAD notes
   to see if FPCC is dead.  CHECK_NOTES is nonzero for the
   first attempt to remove useless add,tr insns.  It is zero
   for the second pass as reorg sometimes leaves bogus REG_DEAD
   notes lying around.

   When CHECK_NOTES is zero we can only eliminate add,tr insns
   when there's a 1:1 correspondence between fcmp and ftest/fbranch
   instructions.  */
static void
remove_useless_addtr_insns (int check_notes)
{
  rtx insn;
  static int pass = 0;

  /* This is fairly cheap, so always run it when optimizing.  */
  if (optimize > 0)
    {
      int fcmp_count = 0;
      int fbranch_count = 0;

      /* Walk all the insns in this function looking for fcmp & fbranch
	 instructions.  Keep track of how many of each we find.  */
      for (insn = get_insns (); insn; insn = next_insn (insn))
	{
	  rtx tmp;

	  /* Ignore anything that isn't an INSN or a JUMP_INSN.  */
	  if (GET_CODE (insn) != INSN && GET_CODE (insn) != JUMP_INSN)
	    continue;

	  tmp = PATTERN (insn);

	  /* It must be a set.  */
	  if (GET_CODE (tmp) != SET)
	    continue;

	  /* If the destination is CCFP, then we've found an fcmp insn.  */
	  tmp = SET_DEST (tmp);
	  if (GET_CODE (tmp) == REG && REGNO (tmp) == 0)
	    {
	      fcmp_count++;
	      continue;
	    }

	  tmp = PATTERN (insn);
	  /* If this is an fbranch instruction, bump the fbranch counter.  */
	  if (GET_CODE (tmp) == SET
	      && SET_DEST (tmp) == pc_rtx
	      && GET_CODE (SET_SRC (tmp)) == IF_THEN_ELSE
	      && GET_CODE (XEXP (SET_SRC (tmp), 0)) == NE
	      && GET_CODE (XEXP (XEXP (SET_SRC (tmp), 0), 0)) == REG
	      && REGNO (XEXP (XEXP (SET_SRC (tmp), 0), 0)) == 0)
	    {
	      fbranch_count++;
	      continue;
	    }
	}


      /* Find all floating point compare + branch insns.  If possible,
	 reverse the comparison & the branch to avoid add,tr insns.  */
      for (insn = get_insns (); insn; insn = next_insn (insn))
	{
	  rtx tmp, next;

	  /* Ignore anything that isn't an INSN.  */
	  if (GET_CODE (insn) != INSN)
	    continue;

	  tmp = PATTERN (insn);

	  /* It must be a set.  */
	  if (GET_CODE (tmp) != SET)
	    continue;

	  /* The destination must be CCFP, which is register zero.  */
	  tmp = SET_DEST (tmp);
	  if (GET_CODE (tmp) != REG || REGNO (tmp) != 0)
	    continue;

	  /* INSN should be a set of CCFP.

	     See if the result of this insn is used in a reversed FP
	     conditional branch.  If so, reverse our condition and
	     the branch.  Doing so avoids useless add,tr insns.  */
	  next = next_insn (insn);
	  while (next)
	    {
	      /* Jumps, calls and labels stop our search.  */
	      if (GET_CODE (next) == JUMP_INSN
		  || GET_CODE (next) == CALL_INSN
		  || GET_CODE (next) == CODE_LABEL)
		break;

	      /* As does another fcmp insn.  */
	      if (GET_CODE (next) == INSN
		  && GET_CODE (PATTERN (next)) == SET
		  && GET_CODE (SET_DEST (PATTERN (next))) == REG
		  && REGNO (SET_DEST (PATTERN (next))) == 0)
		break;

	      next = next_insn (next);
	    }

	  /* Is NEXT_INSN a branch?  */
	  if (next
	      && GET_CODE (next) == JUMP_INSN)
	    {
	      rtx pattern = PATTERN (next);

	      /* If it a reversed fp conditional branch (e.g. uses add,tr)
		 and CCFP dies, then reverse our conditional and the branch
		 to avoid the add,tr.  */
	      if (GET_CODE (pattern) == SET
		  && SET_DEST (pattern) == pc_rtx
		  && GET_CODE (SET_SRC (pattern)) == IF_THEN_ELSE
		  && GET_CODE (XEXP (SET_SRC (pattern), 0)) == NE
		  && GET_CODE (XEXP (XEXP (SET_SRC (pattern), 0), 0)) == REG
		  && REGNO (XEXP (XEXP (SET_SRC (pattern), 0), 0)) == 0
		  && GET_CODE (XEXP (SET_SRC (pattern), 1)) == PC
		  && (fcmp_count == fbranch_count
		      || (check_notes
			  && find_regno_note (next, REG_DEAD, 0))))
		{
		  /* Reverse the branch.  */
		  tmp = XEXP (SET_SRC (pattern), 1);
		  XEXP (SET_SRC (pattern), 1) = XEXP (SET_SRC (pattern), 2);
		  XEXP (SET_SRC (pattern), 2) = tmp;
		  INSN_CODE (next) = -1;

		  /* Reverse our condition.  */
		  tmp = PATTERN (insn);
		  PUT_CODE (XEXP (tmp, 1),
			    (reverse_condition_maybe_unordered
			     (GET_CODE (XEXP (tmp, 1)))));
		}
	    }
	}
    }

  pass = !pass;

}

/* You may have trouble believing this, but this is the 32 bit HP-PA
   stack layout.  Wow.

   Offset		Contents

   Variable arguments	(optional; any number may be allocated)

   SP-(4*(N+9))		arg word N
   	:		    :
      SP-56		arg word 5
      SP-52		arg word 4

   Fixed arguments	(must be allocated; may remain unused)

      SP-48		arg word 3
      SP-44		arg word 2
      SP-40		arg word 1
      SP-36		arg word 0

   Frame Marker

      SP-32		External Data Pointer (DP)
      SP-28		External sr4
      SP-24		External/stub RP (RP')
      SP-20		Current RP
      SP-16		Static Link
      SP-12		Clean up
      SP-8		Calling Stub RP (RP'')
      SP-4		Previous SP

   Top of Frame

      SP-0		Stack Pointer (points to next available address)

*/

/* This function saves registers as follows.  Registers marked with ' are
   this function's registers (as opposed to the previous function's).
   If a frame_pointer isn't needed, r4 is saved as a general register;
   the space for the frame pointer is still allocated, though, to keep
   things simple.


   Top of Frame

       SP (FP')		Previous FP
       SP + 4		Alignment filler (sigh)
       SP + 8		Space for locals reserved here.
       .
       .
       .
       SP + n		All call saved register used.
       .
       .
       .
       SP + o		All call saved fp registers used.
       .
       .
       .
       SP + p (SP')	points to next available address.

*/

/* Global variables set by output_function_prologue().  */
/* Size of frame.  Need to know this to emit return insns from
   leaf procedures.  */
static HOST_WIDE_INT actual_fsize, local_fsize;
static int save_fregs;

/* Emit RTL to store REG at the memory location specified by BASE+DISP.
   Handle case where DISP > 8k by using the add_high_const patterns.

   Note in DISP > 8k case, we will leave the high part of the address
   in %r1.  There is code in expand_hppa_{prologue,epilogue} that knows this.*/

static void
store_reg (int reg, HOST_WIDE_INT disp, int base)
{
  rtx insn, dest, src, basereg;

  src = gen_rtx_REG (word_mode, reg);
  basereg = gen_rtx_REG (Pmode, base);
  if (VAL_14_BITS_P (disp))
    {
      dest = gen_rtx_MEM (word_mode, plus_constant (basereg, disp));
      insn = emit_move_insn (dest, src);
    }
  else if (TARGET_64BIT && !VAL_32_BITS_P (disp))
    {
      rtx delta = GEN_INT (disp);
      rtx tmpreg = gen_rtx_REG (Pmode, 1);

      emit_move_insn (tmpreg, delta);
      insn = emit_move_insn (tmpreg, gen_rtx_PLUS (Pmode, tmpreg, basereg));
      if (DO_FRAME_NOTES)
	{
	  REG_NOTES (insn)
	    = gen_rtx_EXPR_LIST (REG_FRAME_RELATED_EXPR,
		gen_rtx_SET (VOIDmode, tmpreg,
			     gen_rtx_PLUS (Pmode, basereg, delta)),
                REG_NOTES (insn));
	  RTX_FRAME_RELATED_P (insn) = 1;
	}
      dest = gen_rtx_MEM (word_mode, tmpreg);
      insn = emit_move_insn (dest, src);
    }
  else
    {
      rtx delta = GEN_INT (disp);
      rtx high = gen_rtx_PLUS (Pmode, basereg, gen_rtx_HIGH (Pmode, delta));
      rtx tmpreg = gen_rtx_REG (Pmode, 1);

      emit_move_insn (tmpreg, high);
      dest = gen_rtx_MEM (word_mode, gen_rtx_LO_SUM (Pmode, tmpreg, delta));
      insn = emit_move_insn (dest, src);
      if (DO_FRAME_NOTES)
	{
	  REG_NOTES (insn)
	    = gen_rtx_EXPR_LIST (REG_FRAME_RELATED_EXPR,
		gen_rtx_SET (VOIDmode,
			     gen_rtx_MEM (word_mode,
					  gen_rtx_PLUS (word_mode, basereg,
							delta)),
                             src),
                REG_NOTES (insn));
	}
    }

  if (DO_FRAME_NOTES)
    RTX_FRAME_RELATED_P (insn) = 1;
}

/* Emit RTL to store REG at the memory location specified by BASE and then
   add MOD to BASE.  MOD must be <= 8k.  */

static void
store_reg_modify (int base, int reg, HOST_WIDE_INT mod)
{
  rtx insn, basereg, srcreg, delta;

  gcc_assert (VAL_14_BITS_P (mod));

  basereg = gen_rtx_REG (Pmode, base);
  srcreg = gen_rtx_REG (word_mode, reg);
  delta = GEN_INT (mod);

  insn = emit_insn (gen_post_store (basereg, srcreg, delta));
  if (DO_FRAME_NOTES)
    {
      RTX_FRAME_RELATED_P (insn) = 1;

      /* RTX_FRAME_RELATED_P must be set on each frame related set
	 in a parallel with more than one element.  */
      RTX_FRAME_RELATED_P (XVECEXP (PATTERN (insn), 0, 0)) = 1;
      RTX_FRAME_RELATED_P (XVECEXP (PATTERN (insn), 0, 1)) = 1;
    }
}

/* Emit RTL to set REG to the value specified by BASE+DISP.  Handle case
   where DISP > 8k by using the add_high_const patterns.  NOTE indicates
   whether to add a frame note or not.

   In the DISP > 8k case, we leave the high part of the address in %r1.
   There is code in expand_hppa_{prologue,epilogue} that knows about this.  */

static void
set_reg_plus_d (int reg, int base, HOST_WIDE_INT disp, int note)
{
  rtx insn;

  if (VAL_14_BITS_P (disp))
    {
      insn = emit_move_insn (gen_rtx_REG (Pmode, reg),
			     plus_constant (gen_rtx_REG (Pmode, base), disp));
    }
  else if (TARGET_64BIT && !VAL_32_BITS_P (disp))
    {
      rtx basereg = gen_rtx_REG (Pmode, base);
      rtx delta = GEN_INT (disp);
      rtx tmpreg = gen_rtx_REG (Pmode, 1);

      emit_move_insn (tmpreg, delta);
      insn = emit_move_insn (gen_rtx_REG (Pmode, reg),
			     gen_rtx_PLUS (Pmode, tmpreg, basereg));
      if (DO_FRAME_NOTES)
	REG_NOTES (insn)
	  = gen_rtx_EXPR_LIST (REG_FRAME_RELATED_EXPR,
	      gen_rtx_SET (VOIDmode, tmpreg,
			   gen_rtx_PLUS (Pmode, basereg, delta)),
	      REG_NOTES (insn));
    }
  else
    {
      rtx basereg = gen_rtx_REG (Pmode, base);
      rtx delta = GEN_INT (disp);
      rtx tmpreg = gen_rtx_REG (Pmode, 1);

      emit_move_insn (tmpreg,
		      gen_rtx_PLUS (Pmode, basereg,
				    gen_rtx_HIGH (Pmode, delta)));
      insn = emit_move_insn (gen_rtx_REG (Pmode, reg),
			     gen_rtx_LO_SUM (Pmode, tmpreg, delta));
    }

  if (DO_FRAME_NOTES && note)
    RTX_FRAME_RELATED_P (insn) = 1;
}

HOST_WIDE_INT
compute_frame_size (HOST_WIDE_INT size, int *fregs_live)
{
  int freg_saved = 0;
  int i, j;

  /* The code in hppa_expand_prologue and hppa_expand_epilogue must
     be consistent with the rounding and size calculation done here.
     Change them at the same time.  */

  /* We do our own stack alignment.  First, round the size of the
     stack locals up to a word boundary.  */
  size = (size + UNITS_PER_WORD - 1) & ~(UNITS_PER_WORD - 1);

  /* Space for previous frame pointer + filler.  If any frame is
     allocated, we need to add in the STARTING_FRAME_OFFSET.  We
     waste some space here for the sake of HP compatibility.  The
     first slot is only used when the frame pointer is needed.  */
  if (size || frame_pointer_needed)
    size += STARTING_FRAME_OFFSET;
  
  /* If the current function calls __builtin_eh_return, then we need
     to allocate stack space for registers that will hold data for
     the exception handler.  */
  if (DO_FRAME_NOTES && current_function_calls_eh_return)
    {
      unsigned int i;

      for (i = 0; EH_RETURN_DATA_REGNO (i) != INVALID_REGNUM; ++i)
	continue;
      size += i * UNITS_PER_WORD;
    }

  /* Account for space used by the callee general register saves.  */
  for (i = 18, j = frame_pointer_needed ? 4 : 3; i >= j; i--)
    if (regs_ever_live[i])
      size += UNITS_PER_WORD;

  /* Account for space used by the callee floating point register saves.  */
  for (i = FP_SAVED_REG_LAST; i >= FP_SAVED_REG_FIRST; i -= FP_REG_STEP)
    if (regs_ever_live[i]
	|| (!TARGET_64BIT && regs_ever_live[i + 1]))
      {
	freg_saved = 1;

	/* We always save both halves of the FP register, so always
	   increment the frame size by 8 bytes.  */
	size += 8;
      }

  /* If any of the floating registers are saved, account for the
     alignment needed for the floating point register save block.  */
  if (freg_saved)
    {
      size = (size + 7) & ~7;
      if (fregs_live)
	*fregs_live = 1;
    }

  /* The various ABIs include space for the outgoing parameters in the
     size of the current function's stack frame.  We don't need to align
     for the outgoing arguments as their alignment is set by the final
     rounding for the frame as a whole.  */
  size += current_function_outgoing_args_size;

  /* Allocate space for the fixed frame marker.  This space must be
     allocated for any function that makes calls or allocates
     stack space.  */
  if (!current_function_is_leaf || size)
    size += TARGET_64BIT ? 48 : 32;

  /* Finally, round to the preferred stack boundary.  */
  return ((size + PREFERRED_STACK_BOUNDARY / BITS_PER_UNIT - 1)
	  & ~(PREFERRED_STACK_BOUNDARY / BITS_PER_UNIT - 1));
}

/* Generate the assembly code for function entry.  FILE is a stdio
   stream to output the code to.  SIZE is an int: how many units of
   temporary storage to allocate.

   Refer to the array `regs_ever_live' to determine which registers to
   save; `regs_ever_live[I]' is nonzero if register number I is ever
   used in the function.  This function is responsible for knowing
   which registers should not be saved even if used.  */

/* On HP-PA, move-double insns between fpu and cpu need an 8-byte block
   of memory.  If any fpu reg is used in the function, we allocate
   such a block here, at the bottom of the frame, just in case it's needed.

   If this function is a leaf procedure, then we may choose not
   to do a "save" insn.  The decision about whether or not
   to do this is made in regclass.c.  */

static void
pa_output_function_prologue (FILE *file, HOST_WIDE_INT size ATTRIBUTE_UNUSED)
{
  /* The function's label and associated .PROC must never be
     separated and must be output *after* any profiling declarations
     to avoid changing spaces/subspaces within a procedure.  */
  ASM_OUTPUT_LABEL (file, XSTR (XEXP (DECL_RTL (current_function_decl), 0), 0));
  fputs ("\t.PROC\n", file);

  /* hppa_expand_prologue does the dirty work now.  We just need
     to output the assembler directives which denote the start
     of a function.  */
  fprintf (file, "\t.CALLINFO FRAME=" HOST_WIDE_INT_PRINT_DEC, actual_fsize);
  if (regs_ever_live[2])
    fputs (",CALLS,SAVE_RP", file);
  else
    fputs (",NO_CALLS", file);

  /* The SAVE_SP flag is used to indicate that register %r3 is stored
     at the beginning of the frame and that it is used as the frame
     pointer for the frame.  We do this because our current frame
     layout doesn't conform to that specified in the HP runtime
     documentation and we need a way to indicate to programs such as
     GDB where %r3 is saved.  The SAVE_SP flag was chosen because it
     isn't used by HP compilers but is supported by the assembler.
     However, SAVE_SP is supposed to indicate that the previous stack
     pointer has been saved in the frame marker.  */
  if (frame_pointer_needed)
    fputs (",SAVE_SP", file);

  /* Pass on information about the number of callee register saves
     performed in the prologue.

     The compiler is supposed to pass the highest register number
     saved, the assembler then has to adjust that number before
     entering it into the unwind descriptor (to account for any
     caller saved registers with lower register numbers than the
     first callee saved register).  */
  if (gr_saved)
    fprintf (file, ",ENTRY_GR=%d", gr_saved + 2);

  if (fr_saved)
    fprintf (file, ",ENTRY_FR=%d", fr_saved + 11);

  fputs ("\n\t.ENTRY\n", file);

  remove_useless_addtr_insns (0);
}

void
hppa_expand_prologue (void)
{
  int merge_sp_adjust_with_store = 0;
  HOST_WIDE_INT size = get_frame_size ();
  HOST_WIDE_INT offset;
  int i;
  rtx insn, tmpreg;

  gr_saved = 0;
  fr_saved = 0;
  save_fregs = 0;

  /* Compute total size for frame pointer, filler, locals and rounding to
     the next word boundary.  Similar code appears in compute_frame_size
     and must be changed in tandem with this code.  */
  local_fsize = (size + UNITS_PER_WORD - 1) & ~(UNITS_PER_WORD - 1);
  if (local_fsize || frame_pointer_needed)
    local_fsize += STARTING_FRAME_OFFSET;

  actual_fsize = compute_frame_size (size, &save_fregs);

  if (warn_stack_larger_than && actual_fsize > stack_larger_than_size)
    warning (0, "stack usage is %d bytes", actual_fsize);

  /* Compute a few things we will use often.  */
  tmpreg = gen_rtx_REG (word_mode, 1);

  /* Save RP first.  The calling conventions manual states RP will
     always be stored into the caller's frame at sp - 20 or sp - 16
     depending on which ABI is in use.  */
  if (regs_ever_live[2] || current_function_calls_eh_return)
    store_reg (2, TARGET_64BIT ? -16 : -20, STACK_POINTER_REGNUM);

  /* Allocate the local frame and set up the frame pointer if needed.  */
  if (actual_fsize != 0)
    {
      if (frame_pointer_needed)
	{
	  /* Copy the old frame pointer temporarily into %r1.  Set up the
	     new stack pointer, then store away the saved old frame pointer
	     into the stack at sp and at the same time update the stack
	     pointer by actual_fsize bytes.  Two versions, first
	     handles small (<8k) frames.  The second handles large (>=8k)
	     frames.  */
	  insn = emit_move_insn (tmpreg, frame_pointer_rtx);
	  if (DO_FRAME_NOTES)
	    RTX_FRAME_RELATED_P (insn) = 1;

	  insn = emit_move_insn (frame_pointer_rtx, stack_pointer_rtx);
	  if (DO_FRAME_NOTES)
	    RTX_FRAME_RELATED_P (insn) = 1;

	  if (VAL_14_BITS_P (actual_fsize))
	    store_reg_modify (STACK_POINTER_REGNUM, 1, actual_fsize);
	  else
	    {
	      /* It is incorrect to store the saved frame pointer at *sp,
		 then increment sp (writes beyond the current stack boundary).

		 So instead use stwm to store at *sp and post-increment the
		 stack pointer as an atomic operation.  Then increment sp to
		 finish allocating the new frame.  */
	      HOST_WIDE_INT adjust1 = 8192 - 64;
	      HOST_WIDE_INT adjust2 = actual_fsize - adjust1;

	      store_reg_modify (STACK_POINTER_REGNUM, 1, adjust1);
	      set_reg_plus_d (STACK_POINTER_REGNUM, STACK_POINTER_REGNUM,
			      adjust2, 1);
	    }

	  /* We set SAVE_SP in frames that need a frame pointer.  Thus,
	     we need to store the previous stack pointer (frame pointer)
	     into the frame marker on targets that use the HP unwind
	     library.  This allows the HP unwind library to be used to
	     unwind GCC frames.  However, we are not fully compatible
	     with the HP library because our frame layout differs from
	     that specified in the HP runtime specification.

	     We don't want a frame note on this instruction as the frame
	     marker moves during dynamic stack allocation.

	     This instruction also serves as a blockage to prevent
	     register spills from being scheduled before the stack
	     pointer is raised.  This is necessary as we store
	     registers using the frame pointer as a base register,
	     and the frame pointer is set before sp is raised.  */
	  if (TARGET_HPUX_UNWIND_LIBRARY)
	    {
	      rtx addr = gen_rtx_PLUS (word_mode, stack_pointer_rtx,
				       GEN_INT (TARGET_64BIT ? -8 : -4));

	      emit_move_insn (gen_rtx_MEM (word_mode, addr),
			      frame_pointer_rtx);
	    }
	  else
	    emit_insn (gen_blockage ());
	}
      /* no frame pointer needed.  */
      else
	{
	  /* In some cases we can perform the first callee register save
	     and allocating the stack frame at the same time.   If so, just
	     make a note of it and defer allocating the frame until saving
	     the callee registers.  */
	  if (VAL_14_BITS_P (actual_fsize) && local_fsize == 0)
	    merge_sp_adjust_with_store = 1;
	  /* Can not optimize.  Adjust the stack frame by actual_fsize
	     bytes.  */
	  else
	    set_reg_plus_d (STACK_POINTER_REGNUM, STACK_POINTER_REGNUM,
			    actual_fsize, 1);
	}
    }

  /* Normal register save.

     Do not save the frame pointer in the frame_pointer_needed case.  It
     was done earlier.  */
  if (frame_pointer_needed)
    {
      offset = local_fsize;

      /* Saving the EH return data registers in the frame is the simplest
	 way to get the frame unwind information emitted.  We put them
	 just before the general registers.  */
      if (DO_FRAME_NOTES && current_function_calls_eh_return)
	{
	  unsigned int i, regno;

	  for (i = 0; ; ++i)
	    {
	      regno = EH_RETURN_DATA_REGNO (i);
	      if (regno == INVALID_REGNUM)
		break;

	      store_reg (regno, offset, FRAME_POINTER_REGNUM);
	      offset += UNITS_PER_WORD;
	    }
	}

      for (i = 18; i >= 4; i--)
	if (regs_ever_live[i] && ! call_used_regs[i])
	  {
	    store_reg (i, offset, FRAME_POINTER_REGNUM);
	    offset += UNITS_PER_WORD;
	    gr_saved++;
	  }
      /* Account for %r3 which is saved in a special place.  */
      gr_saved++;
    }
  /* No frame pointer needed.  */
  else
    {
      offset = local_fsize - actual_fsize;

      /* Saving the EH return data registers in the frame is the simplest
         way to get the frame unwind information emitted.  */
      if (DO_FRAME_NOTES && current_function_calls_eh_return)
	{
	  unsigned int i, regno;

	  for (i = 0; ; ++i)
	    {
	      regno = EH_RETURN_DATA_REGNO (i);
	      if (regno == INVALID_REGNUM)
		break;

	      /* If merge_sp_adjust_with_store is nonzero, then we can
		 optimize the first save.  */
	      if (merge_sp_adjust_with_store)
		{
		  store_reg_modify (STACK_POINTER_REGNUM, regno, -offset);
		  merge_sp_adjust_with_store = 0;
		}
	      else
		store_reg (regno, offset, STACK_POINTER_REGNUM);
	      offset += UNITS_PER_WORD;
	    }
	}

      for (i = 18; i >= 3; i--)
      	if (regs_ever_live[i] && ! call_used_regs[i])
	  {
	    /* If merge_sp_adjust_with_store is nonzero, then we can
	       optimize the first GR save.  */
	    if (merge_sp_adjust_with_store)
	      {
		store_reg_modify (STACK_POINTER_REGNUM, i, -offset);
		merge_sp_adjust_with_store = 0;
	      }
	    else
	      store_reg (i, offset, STACK_POINTER_REGNUM);
	    offset += UNITS_PER_WORD;
	    gr_saved++;
	  }

      /* If we wanted to merge the SP adjustment with a GR save, but we never
	 did any GR saves, then just emit the adjustment here.  */
      if (merge_sp_adjust_with_store)
	set_reg_plus_d (STACK_POINTER_REGNUM, STACK_POINTER_REGNUM,
			actual_fsize, 1);
    }

  /* The hppa calling conventions say that %r19, the pic offset
     register, is saved at sp - 32 (in this function's frame)
     when generating PIC code.  FIXME:  What is the correct thing
     to do for functions which make no calls and allocate no
     frame?  Do we need to allocate a frame, or can we just omit
     the save?   For now we'll just omit the save.
     
     We don't want a note on this insn as the frame marker can
     move if there is a dynamic stack allocation.  */
  if (flag_pic && actual_fsize != 0 && !TARGET_64BIT)
    {
      rtx addr = gen_rtx_PLUS (word_mode, stack_pointer_rtx, GEN_INT (-32));

      emit_move_insn (gen_rtx_MEM (word_mode, addr), pic_offset_table_rtx);

    }

  /* Align pointer properly (doubleword boundary).  */
  offset = (offset + 7) & ~7;

  /* Floating point register store.  */
  if (save_fregs)
    {
      rtx base;

      /* First get the frame or stack pointer to the start of the FP register
	 save area.  */
      if (frame_pointer_needed)
	{
	  set_reg_plus_d (1, FRAME_POINTER_REGNUM, offset, 0);
	  base = frame_pointer_rtx;
	}
      else
	{
	  set_reg_plus_d (1, STACK_POINTER_REGNUM, offset, 0);
	  base = stack_pointer_rtx;
	}

      /* Now actually save the FP registers.  */
      for (i = FP_SAVED_REG_LAST; i >= FP_SAVED_REG_FIRST; i -= FP_REG_STEP)
	{
	  if (regs_ever_live[i]
	      || (! TARGET_64BIT && regs_ever_live[i + 1]))
	    {
	      rtx addr, insn, reg;
	      addr = gen_rtx_MEM (DFmode, gen_rtx_POST_INC (DFmode, tmpreg));
	      reg = gen_rtx_REG (DFmode, i);
	      insn = emit_move_insn (addr, reg);
	      if (DO_FRAME_NOTES)
		{
		  RTX_FRAME_RELATED_P (insn) = 1;
		  if (TARGET_64BIT)
		    {
		      rtx mem = gen_rtx_MEM (DFmode,
					     plus_constant (base, offset));
		      REG_NOTES (insn)
			= gen_rtx_EXPR_LIST (REG_FRAME_RELATED_EXPR,
					     gen_rtx_SET (VOIDmode, mem, reg),
					     REG_NOTES (insn));
		    }
		  else
		    {
		      rtx meml = gen_rtx_MEM (SFmode,
					      plus_constant (base, offset));
		      rtx memr = gen_rtx_MEM (SFmode,
					      plus_constant (base, offset + 4));
		      rtx regl = gen_rtx_REG (SFmode, i);
		      rtx regr = gen_rtx_REG (SFmode, i + 1);
		      rtx setl = gen_rtx_SET (VOIDmode, meml, regl);
		      rtx setr = gen_rtx_SET (VOIDmode, memr, regr);
		      rtvec vec;

		      RTX_FRAME_RELATED_P (setl) = 1;
		      RTX_FRAME_RELATED_P (setr) = 1;
		      vec = gen_rtvec (2, setl, setr);
		      REG_NOTES (insn)
			= gen_rtx_EXPR_LIST (REG_FRAME_RELATED_EXPR,
					     gen_rtx_SEQUENCE (VOIDmode, vec),
					     REG_NOTES (insn));
		    }
		}
	      offset += GET_MODE_SIZE (DFmode);
	      fr_saved++;
	    }
	}
    }
}

/* Emit RTL to load REG from the memory location specified by BASE+DISP.
   Handle case where DISP > 8k by using the add_high_const patterns.  */

static void
load_reg (int reg, HOST_WIDE_INT disp, int base)
{
  rtx dest = gen_rtx_REG (word_mode, reg);
  rtx basereg = gen_rtx_REG (Pmode, base);
  rtx src;

  if (VAL_14_BITS_P (disp))
    src = gen_rtx_MEM (word_mode, plus_constant (basereg, disp));
  else if (TARGET_64BIT && !VAL_32_BITS_P (disp))
    {
      rtx delta = GEN_INT (disp);
      rtx tmpreg = gen_rtx_REG (Pmode, 1);

      emit_move_insn (tmpreg, delta);
      if (TARGET_DISABLE_INDEXING)
	{
	  emit_move_insn (tmpreg, gen_rtx_PLUS (Pmode, tmpreg, basereg));
	  src = gen_rtx_MEM (word_mode, tmpreg);
	}
      else
	src = gen_rtx_MEM (word_mode, gen_rtx_PLUS (Pmode, tmpreg, basereg));
    }
  else
    {
      rtx delta = GEN_INT (disp);
      rtx high = gen_rtx_PLUS (Pmode, basereg, gen_rtx_HIGH (Pmode, delta));
      rtx tmpreg = gen_rtx_REG (Pmode, 1);

      emit_move_insn (tmpreg, high);
      src = gen_rtx_MEM (word_mode, gen_rtx_LO_SUM (Pmode, tmpreg, delta));
    }

  emit_move_insn (dest, src);
}

/* Update the total code bytes output to the text section.  */

static void
update_total_code_bytes (int nbytes)
{
  if ((TARGET_PORTABLE_RUNTIME || !TARGET_GAS || !TARGET_SOM)
      && !IN_NAMED_SECTION_P (cfun->decl))
    {
      if (INSN_ADDRESSES_SET_P ())
	{
	  unsigned long old_total = total_code_bytes;

	  total_code_bytes += nbytes;

	  /* Be prepared to handle overflows.  */
	  if (old_total > total_code_bytes)
	    total_code_bytes = -1;
	}
      else
	total_code_bytes = -1;
    }
}

/* This function generates the assembly code for function exit.
   Args are as for output_function_prologue ().

   The function epilogue should not depend on the current stack
   pointer!  It should use the frame pointer only.  This is mandatory
   because of alloca; we also take advantage of it to omit stack
   adjustments before returning.  */

static void
pa_output_function_epilogue (FILE *file, HOST_WIDE_INT size ATTRIBUTE_UNUSED)
{
  rtx insn = get_last_insn ();

  last_address = 0;

  /* hppa_expand_epilogue does the dirty work now.  We just need
     to output the assembler directives which denote the end
     of a function.

     To make debuggers happy, emit a nop if the epilogue was completely
     eliminated due to a volatile call as the last insn in the
     current function.  That way the return address (in %r2) will
     always point to a valid instruction in the current function.  */

  /* Get the last real insn.  */
  if (GET_CODE (insn) == NOTE)
    insn = prev_real_insn (insn);

  /* If it is a sequence, then look inside.  */
  if (insn && GET_CODE (insn) == INSN && GET_CODE (PATTERN (insn)) == SEQUENCE)
    insn = XVECEXP (PATTERN (insn), 0, 0);

  /* If insn is a CALL_INSN, then it must be a call to a volatile
     function (otherwise there would be epilogue insns).  */
  if (insn && GET_CODE (insn) == CALL_INSN)
    {
      fputs ("\tnop\n", file);
      last_address += 4;
    }

  fputs ("\t.EXIT\n\t.PROCEND\n", file);

  if (TARGET_SOM && TARGET_GAS)
    {
      /* We done with this subspace except possibly for some additional
	 debug information.  Forget that we are in this subspace to ensure
	 that the next function is output in its own subspace.  */
      in_section = NULL;
      cfun->machine->in_nsubspa = 2;
    }

  if (INSN_ADDRESSES_SET_P ())
    {
      insn = get_last_nonnote_insn ();
      last_address += INSN_ADDRESSES (INSN_UID (insn));
      if (INSN_P (insn))
	last_address += insn_default_length (insn);
      last_address = ((last_address + FUNCTION_BOUNDARY / BITS_PER_UNIT - 1)
		      & ~(FUNCTION_BOUNDARY / BITS_PER_UNIT - 1));
    }

  /* Finally, update the total number of code bytes output so far.  */
  update_total_code_bytes (last_address);
}

void
hppa_expand_epilogue (void)
{
  rtx tmpreg;
  HOST_WIDE_INT offset;
  HOST_WIDE_INT ret_off = 0;
  int i;
  int merge_sp_adjust_with_load = 0;

  /* We will use this often.  */
  tmpreg = gen_rtx_REG (word_mode, 1);

  /* Try to restore RP early to avoid load/use interlocks when
     RP gets used in the return (bv) instruction.  This appears to still
     be necessary even when we schedule the prologue and epilogue.  */
  if (regs_ever_live [2] || current_function_calls_eh_return)
    {
      ret_off = TARGET_64BIT ? -16 : -20;
      if (frame_pointer_needed)
	{
	  load_reg (2, ret_off, FRAME_POINTER_REGNUM);
	  ret_off = 0;
	}
      else
	{
	  /* No frame pointer, and stack is smaller than 8k.  */
	  if (VAL_14_BITS_P (ret_off - actual_fsize))
	    {
	      load_reg (2, ret_off - actual_fsize, STACK_POINTER_REGNUM);
	      ret_off = 0;
	    }
	}
    }

  /* General register restores.  */
  if (frame_pointer_needed)
    {
      offset = local_fsize;

      /* If the current function calls __builtin_eh_return, then we need
         to restore the saved EH data registers.  */
      if (DO_FRAME_NOTES && current_function_calls_eh_return)
	{
	  unsigned int i, regno;

	  for (i = 0; ; ++i)
	    {
	      regno = EH_RETURN_DATA_REGNO (i);
	      if (regno == INVALID_REGNUM)
		break;

	      load_reg (regno, offset, FRAME_POINTER_REGNUM);
	      offset += UNITS_PER_WORD;
	    }
	}

      for (i = 18; i >= 4; i--)
	if (regs_ever_live[i] && ! call_used_regs[i])
	  {
	    load_reg (i, offset, FRAME_POINTER_REGNUM);
	    offset += UNITS_PER_WORD;
	  }
    }
  else
    {
      offset = local_fsize - actual_fsize;

      /* If the current function calls __builtin_eh_return, then we need
         to restore the saved EH data registers.  */
      if (DO_FRAME_NOTES && current_function_calls_eh_return)
	{
	  unsigned int i, regno;

	  for (i = 0; ; ++i)
	    {
	      regno = EH_RETURN_DATA_REGNO (i);
	      if (regno == INVALID_REGNUM)
		break;

	      /* Only for the first load.
	         merge_sp_adjust_with_load holds the register load
	         with which we will merge the sp adjustment.  */
	      if (merge_sp_adjust_with_load == 0
		  && local_fsize == 0
		  && VAL_14_BITS_P (-actual_fsize))
	        merge_sp_adjust_with_load = regno;
	      else
		load_reg (regno, offset, STACK_POINTER_REGNUM);
	      offset += UNITS_PER_WORD;
	    }
	}

      for (i = 18; i >= 3; i--)
	{
	  if (regs_ever_live[i] && ! call_used_regs[i])
	    {
	      /* Only for the first load.
	         merge_sp_adjust_with_load holds the register load
	         with which we will merge the sp adjustment.  */
	      if (merge_sp_adjust_with_load == 0
		  && local_fsize == 0
		  && VAL_14_BITS_P (-actual_fsize))
	        merge_sp_adjust_with_load = i;
	      else
		load_reg (i, offset, STACK_POINTER_REGNUM);
	      offset += UNITS_PER_WORD;
	    }
	}
    }

  /* Align pointer properly (doubleword boundary).  */
  offset = (offset + 7) & ~7;

  /* FP register restores.  */
  if (save_fregs)
    {
      /* Adjust the register to index off of.  */
      if (frame_pointer_needed)
	set_reg_plus_d (1, FRAME_POINTER_REGNUM, offset, 0);
      else
	set_reg_plus_d (1, STACK_POINTER_REGNUM, offset, 0);

      /* Actually do the restores now.  */
      for (i = FP_SAVED_REG_LAST; i >= FP_SAVED_REG_FIRST; i -= FP_REG_STEP)
	if (regs_ever_live[i]
	    || (! TARGET_64BIT && regs_ever_live[i + 1]))
	  {
	    rtx src = gen_rtx_MEM (DFmode, gen_rtx_POST_INC (DFmode, tmpreg));
	    rtx dest = gen_rtx_REG (DFmode, i);
	    emit_move_insn (dest, src);
	  }
    }

  /* Emit a blockage insn here to keep these insns from being moved to
     an earlier spot in the epilogue, or into the main instruction stream.

     This is necessary as we must not cut the stack back before all the
     restores are finished.  */
  emit_insn (gen_blockage ());

  /* Reset stack pointer (and possibly frame pointer).  The stack
     pointer is initially set to fp + 64 to avoid a race condition.  */
  if (frame_pointer_needed)
    {
      rtx delta = GEN_INT (-64);

      set_reg_plus_d (STACK_POINTER_REGNUM, FRAME_POINTER_REGNUM, 64, 0);
      emit_insn (gen_pre_load (frame_pointer_rtx, stack_pointer_rtx, delta));
    }
  /* If we were deferring a callee register restore, do it now.  */
  else if (merge_sp_adjust_with_load)
    {
      rtx delta = GEN_INT (-actual_fsize);
      rtx dest = gen_rtx_REG (word_mode, merge_sp_adjust_with_load);

      emit_insn (gen_pre_load (dest, stack_pointer_rtx, delta));
    }
  else if (actual_fsize != 0)
    set_reg_plus_d (STACK_POINTER_REGNUM, STACK_POINTER_REGNUM,
		    - actual_fsize, 0);

  /* If we haven't restored %r2 yet (no frame pointer, and a stack
     frame greater than 8k), do so now.  */
  if (ret_off != 0)
    load_reg (2, ret_off, STACK_POINTER_REGNUM);

  if (DO_FRAME_NOTES && current_function_calls_eh_return)
    {
      rtx sa = EH_RETURN_STACKADJ_RTX;

      emit_insn (gen_blockage ());
      emit_insn (TARGET_64BIT
		 ? gen_subdi3 (stack_pointer_rtx, stack_pointer_rtx, sa)
		 : gen_subsi3 (stack_pointer_rtx, stack_pointer_rtx, sa));
    }
}

rtx
hppa_pic_save_rtx (void)
{
  return get_hard_reg_initial_val (word_mode, PIC_OFFSET_TABLE_REGNUM);
}

#ifndef NO_DEFERRED_PROFILE_COUNTERS
#define NO_DEFERRED_PROFILE_COUNTERS 0
#endif

/* Define heap vector type for funcdef numbers.  */
DEF_VEC_I(int);
DEF_VEC_ALLOC_I(int,heap);

/* Vector of funcdef numbers.  */
static VEC(int,heap) *funcdef_nos;

/* Output deferred profile counters.  */
static void
output_deferred_profile_counters (void)
{
  unsigned int i;
  int align, n;

  if (VEC_empty (int, funcdef_nos))
   return;

  switch_to_section (data_section);
  align = MIN (BIGGEST_ALIGNMENT, LONG_TYPE_SIZE);
  ASM_OUTPUT_ALIGN (asm_out_file, floor_log2 (align / BITS_PER_UNIT));

  for (i = 0; VEC_iterate (int, funcdef_nos, i, n); i++)
    {
      targetm.asm_out.internal_label (asm_out_file, "LP", n);
      assemble_integer (const0_rtx, LONG_TYPE_SIZE / BITS_PER_UNIT, align, 1);
    }

  VEC_free (int, heap, funcdef_nos);
}

void
hppa_profile_hook (int label_no)
{
  /* We use SImode for the address of the function in both 32 and
     64-bit code to avoid having to provide DImode versions of the
     lcla2 and load_offset_label_address insn patterns.  */
  rtx reg = gen_reg_rtx (SImode);
  rtx label_rtx = gen_label_rtx ();
  rtx begin_label_rtx, call_insn;
  char begin_label_name[16];

  ASM_GENERATE_INTERNAL_LABEL (begin_label_name, FUNC_BEGIN_PROLOG_LABEL,
			       label_no);
  begin_label_rtx = gen_rtx_SYMBOL_REF (SImode, ggc_strdup (begin_label_name));

  if (TARGET_64BIT)
    emit_move_insn (arg_pointer_rtx,
		    gen_rtx_PLUS (word_mode, virtual_outgoing_args_rtx,
				  GEN_INT (64)));

  emit_move_insn (gen_rtx_REG (word_mode, 26), gen_rtx_REG (word_mode, 2));

  /* The address of the function is loaded into %r25 with a instruction-
     relative sequence that avoids the use of relocations.  The sequence
     is split so that the load_offset_label_address instruction can
     occupy the delay slot of the call to _mcount.  */
  if (TARGET_PA_20)
    emit_insn (gen_lcla2 (reg, label_rtx));
  else
    emit_insn (gen_lcla1 (reg, label_rtx));

  emit_insn (gen_load_offset_label_address (gen_rtx_REG (SImode, 25), 
					    reg, begin_label_rtx, label_rtx));

#if !NO_DEFERRED_PROFILE_COUNTERS
  {
    rtx count_label_rtx, addr, r24;
    char count_label_name[16];

    VEC_safe_push (int, heap, funcdef_nos, label_no);
    ASM_GENERATE_INTERNAL_LABEL (count_label_name, "LP", label_no);
    count_label_rtx = gen_rtx_SYMBOL_REF (Pmode, ggc_strdup (count_label_name));

    addr = force_reg (Pmode, count_label_rtx);
    r24 = gen_rtx_REG (Pmode, 24);
    emit_move_insn (r24, addr);

    call_insn =
      emit_call_insn (gen_call (gen_rtx_MEM (Pmode, 
					     gen_rtx_SYMBOL_REF (Pmode, 
								 "_mcount")),
				GEN_INT (TARGET_64BIT ? 24 : 12)));

    use_reg (&CALL_INSN_FUNCTION_USAGE (call_insn), r24);
  }
#else

  call_insn =
    emit_call_insn (gen_call (gen_rtx_MEM (Pmode, 
					   gen_rtx_SYMBOL_REF (Pmode, 
							       "_mcount")),
			      GEN_INT (TARGET_64BIT ? 16 : 8)));

#endif

  use_reg (&CALL_INSN_FUNCTION_USAGE (call_insn), gen_rtx_REG (SImode, 25));
  use_reg (&CALL_INSN_FUNCTION_USAGE (call_insn), gen_rtx_REG (SImode, 26));

  /* Indicate the _mcount call cannot throw, nor will it execute a
     non-local goto.  */
  REG_NOTES (call_insn)
    = gen_rtx_EXPR_LIST (REG_EH_REGION, constm1_rtx, REG_NOTES (call_insn));
}

/* Fetch the return address for the frame COUNT steps up from
   the current frame, after the prologue.  FRAMEADDR is the
   frame pointer of the COUNT frame.

   We want to ignore any export stub remnants here.  To handle this,
   we examine the code at the return address, and if it is an export
   stub, we return a memory rtx for the stub return address stored
   at frame-24.

   The value returned is used in two different ways:

	1. To find a function's caller.

	2. To change the return address for a function.

   This function handles most instances of case 1; however, it will
   fail if there are two levels of stubs to execute on the return
   path.  The only way I believe that can happen is if the return value
   needs a parameter relocation, which never happens for C code.

   This function handles most instances of case 2; however, it will
   fail if we did not originally have stub code on the return path
   but will need stub code on the new return path.  This can happen if
   the caller & callee are both in the main program, but the new
   return location is in a shared library.  */

rtx
return_addr_rtx (int count, rtx frameaddr)
{
  rtx label;
  rtx rp;
  rtx saved_rp;
  rtx ins;

  if (count != 0)
    return NULL_RTX;

  rp = get_hard_reg_initial_val (Pmode, 2);

  if (TARGET_64BIT || TARGET_NO_SPACE_REGS)
    return rp;

  saved_rp = gen_reg_rtx (Pmode);
  emit_move_insn (saved_rp, rp);

  /* Get pointer to the instruction stream.  We have to mask out the
     privilege level from the two low order bits of the return address
     pointer here so that ins will point to the start of the first
     instruction that would have been executed if we returned.  */
  ins = copy_to_reg (gen_rtx_AND (Pmode, rp, MASK_RETURN_ADDR));
  label = gen_label_rtx ();

  /* Check the instruction stream at the normal return address for the
     export stub:

	0x4bc23fd1 | stub+8:   ldw -18(sr0,sp),rp
	0x004010a1 | stub+12:  ldsid (sr0,rp),r1
	0x00011820 | stub+16:  mtsp r1,sr0
	0xe0400002 | stub+20:  be,n 0(sr0,rp)

     If it is an export stub, than our return address is really in
     -24[frameaddr].  */

  emit_cmp_insn (gen_rtx_MEM (SImode, ins), GEN_INT (0x4bc23fd1), NE,
		 NULL_RTX, SImode, 1);
  emit_jump_insn (gen_bne (label));

  emit_cmp_insn (gen_rtx_MEM (SImode, plus_constant (ins, 4)),
		 GEN_INT (0x004010a1), NE, NULL_RTX, SImode, 1);
  emit_jump_insn (gen_bne (label));

  emit_cmp_insn (gen_rtx_MEM (SImode, plus_constant (ins, 8)),
		 GEN_INT (0x00011820), NE, NULL_RTX, SImode, 1);
  emit_jump_insn (gen_bne (label));

  /* 0xe0400002 must be specified as -532676606 so that it won't be
     rejected as an invalid immediate operand on 64-bit hosts.  */
  emit_cmp_insn (gen_rtx_MEM (SImode, plus_constant (ins, 12)),
		 GEN_INT (-532676606), NE, NULL_RTX, SImode, 1);

  /* If there is no export stub then just use the value saved from
     the return pointer register.  */

  emit_jump_insn (gen_bne (label));

  /* Here we know that our return address points to an export
     stub.  We don't want to return the address of the export stub,
     but rather the return address of the export stub.  That return
     address is stored at -24[frameaddr].  */

  emit_move_insn (saved_rp,
		  gen_rtx_MEM (Pmode,
			       memory_address (Pmode,
					       plus_constant (frameaddr,
							      -24))));

  emit_label (label);
  return saved_rp;
}

/* This is only valid once reload has completed because it depends on
   knowing exactly how much (if any) frame there is and...

   It's only valid if there is no frame marker to de-allocate and...

   It's only valid if %r2 hasn't been saved into the caller's frame
   (we're not profiling and %r2 isn't live anywhere).  */
int
hppa_can_use_return_insn_p (void)
{
  return (reload_completed
	  && (compute_frame_size (get_frame_size (), 0) ? 0 : 1)
	  && ! regs_ever_live[2]
	  && ! frame_pointer_needed);
}

void
emit_bcond_fp (enum rtx_code code, rtx operand0)
{
  emit_jump_insn (gen_rtx_SET (VOIDmode, pc_rtx,
			       gen_rtx_IF_THEN_ELSE (VOIDmode,
						     gen_rtx_fmt_ee (code,
							      VOIDmode,
							      gen_rtx_REG (CCFPmode, 0),
							      const0_rtx),
						     gen_rtx_LABEL_REF (VOIDmode, operand0),
						     pc_rtx)));

}

rtx
gen_cmp_fp (enum rtx_code code, rtx operand0, rtx operand1)
{
  return gen_rtx_SET (VOIDmode, gen_rtx_REG (CCFPmode, 0),
		      gen_rtx_fmt_ee (code, CCFPmode, operand0, operand1));
}

/* Adjust the cost of a scheduling dependency.  Return the new cost of
   a dependency LINK or INSN on DEP_INSN.  COST is the current cost.  */

static int
pa_adjust_cost (rtx insn, rtx link, rtx dep_insn, int cost)
{
  enum attr_type attr_type;

  /* Don't adjust costs for a pa8000 chip, also do not adjust any
     true dependencies as they are described with bypasses now.  */
  if (pa_cpu >= PROCESSOR_8000 || REG_NOTE_KIND (link) == 0)
    return cost;

  if (! recog_memoized (insn))
    return 0;

  attr_type = get_attr_type (insn);

  switch (REG_NOTE_KIND (link))
    {
    case REG_DEP_ANTI:
      /* Anti dependency; DEP_INSN reads a register that INSN writes some
	 cycles later.  */

      if (attr_type == TYPE_FPLOAD)
	{
	  rtx pat = PATTERN (insn);
	  rtx dep_pat = PATTERN (dep_insn);
	  if (GET_CODE (pat) == PARALLEL)
	    {
	      /* This happens for the fldXs,mb patterns.  */
	      pat = XVECEXP (pat, 0, 0);
	    }
	  if (GET_CODE (pat) != SET || GET_CODE (dep_pat) != SET)
	    /* If this happens, we have to extend this to schedule
	       optimally.  Return 0 for now.  */
	  return 0;

	  if (reg_mentioned_p (SET_DEST (pat), SET_SRC (dep_pat)))
	    {
	      if (! recog_memoized (dep_insn))
		return 0;
	      switch (get_attr_type (dep_insn))
		{
		case TYPE_FPALU:
		case TYPE_FPMULSGL:
		case TYPE_FPMULDBL:
		case TYPE_FPDIVSGL:
		case TYPE_FPDIVDBL:
		case TYPE_FPSQRTSGL:
		case TYPE_FPSQRTDBL:
		  /* A fpload can't be issued until one cycle before a
		     preceding arithmetic operation has finished if
		     the target of the fpload is any of the sources
		     (or destination) of the arithmetic operation.  */
		  return insn_default_latency (dep_insn) - 1;

		default:
		  return 0;
		}
	    }
	}
      else if (attr_type == TYPE_FPALU)
	{
	  rtx pat = PATTERN (insn);
	  rtx dep_pat = PATTERN (dep_insn);
	  if (GET_CODE (pat) == PARALLEL)
	    {
	      /* This happens for the fldXs,mb patterns.  */
	      pat = XVECEXP (pat, 0, 0);
	    }
	  if (GET_CODE (pat) != SET || GET_CODE (dep_pat) != SET)
	    /* If this happens, we have to extend this to schedule
	       optimally.  Return 0 for now.  */
	  return 0;

	  if (reg_mentioned_p (SET_DEST (pat), SET_SRC (dep_pat)))
	    {
	      if (! recog_memoized (dep_insn))
		return 0;
	      switch (get_attr_type (dep_insn))
		{
		case TYPE_FPDIVSGL:
		case TYPE_FPDIVDBL:
		case TYPE_FPSQRTSGL:
		case TYPE_FPSQRTDBL:
		  /* An ALU flop can't be issued until two cycles before a
		     preceding divide or sqrt operation has finished if
		     the target of the ALU flop is any of the sources
		     (or destination) of the divide or sqrt operation.  */
		  return insn_default_latency (dep_insn) - 2;

		default:
		  return 0;
		}
	    }
	}

      /* For other anti dependencies, the cost is 0.  */
      return 0;

    case REG_DEP_OUTPUT:
      /* Output dependency; DEP_INSN writes a register that INSN writes some
	 cycles later.  */
      if (attr_type == TYPE_FPLOAD)
	{
	  rtx pat = PATTERN (insn);
	  rtx dep_pat = PATTERN (dep_insn);
	  if (GET_CODE (pat) == PARALLEL)
	    {
	      /* This happens for the fldXs,mb patterns.  */
	      pat = XVECEXP (pat, 0, 0);
	    }
	  if (GET_CODE (pat) != SET || GET_CODE (dep_pat) != SET)
	    /* If this happens, we have to extend this to schedule
	       optimally.  Return 0 for now.  */
	  return 0;

	  if (reg_mentioned_p (SET_DEST (pat), SET_DEST (dep_pat)))
	    {
	      if (! recog_memoized (dep_insn))
		return 0;
	      switch (get_attr_type (dep_insn))
		{
		case TYPE_FPALU:
		case TYPE_FPMULSGL:
		case TYPE_FPMULDBL:
		case TYPE_FPDIVSGL:
		case TYPE_FPDIVDBL:
		case TYPE_FPSQRTSGL:
		case TYPE_FPSQRTDBL:
		  /* A fpload can't be issued until one cycle before a
		     preceding arithmetic operation has finished if
		     the target of the fpload is the destination of the
		     arithmetic operation. 

		     Exception: For PA7100LC, PA7200 and PA7300, the cost
		     is 3 cycles, unless they bundle together.   We also
		     pay the penalty if the second insn is a fpload.  */
		  return insn_default_latency (dep_insn) - 1;

		default:
		  return 0;
		}
	    }
	}
      else if (attr_type == TYPE_FPALU)
	{
	  rtx pat = PATTERN (insn);
	  rtx dep_pat = PATTERN (dep_insn);
	  if (GET_CODE (pat) == PARALLEL)
	    {
	      /* This happens for the fldXs,mb patterns.  */
	      pat = XVECEXP (pat, 0, 0);
	    }
	  if (GET_CODE (pat) != SET || GET_CODE (dep_pat) != SET)
	    /* If this happens, we have to extend this to schedule
	       optimally.  Return 0 for now.  */
	  return 0;

	  if (reg_mentioned_p (SET_DEST (pat), SET_DEST (dep_pat)))
	    {
	      if (! recog_memoized (dep_insn))
		return 0;
	      switch (get_attr_type (dep_insn))
		{
		case TYPE_FPDIVSGL:
		case TYPE_FPDIVDBL:
		case TYPE_FPSQRTSGL:
		case TYPE_FPSQRTDBL:
		  /* An ALU flop can't be issued until two cycles before a
		     preceding divide or sqrt operation has finished if
		     the target of the ALU flop is also the target of
		     the divide or sqrt operation.  */
		  return insn_default_latency (dep_insn) - 2;

		default:
		  return 0;
		}
	    }
	}

      /* For other output dependencies, the cost is 0.  */
      return 0;

    default:
      gcc_unreachable ();
    }
}

/* Adjust scheduling priorities.  We use this to try and keep addil
   and the next use of %r1 close together.  */
static int
pa_adjust_priority (rtx insn, int priority)
{
  rtx set = single_set (insn);
  rtx src, dest;
  if (set)
    {
      src = SET_SRC (set);
      dest = SET_DEST (set);
      if (GET_CODE (src) == LO_SUM
	  && symbolic_operand (XEXP (src, 1), VOIDmode)
	  && ! read_only_operand (XEXP (src, 1), VOIDmode))
	priority >>= 3;

      else if (GET_CODE (src) == MEM
	       && GET_CODE (XEXP (src, 0)) == LO_SUM
	       && symbolic_operand (XEXP (XEXP (src, 0), 1), VOIDmode)
	       && ! read_only_operand (XEXP (XEXP (src, 0), 1), VOIDmode))
	priority >>= 1;

      else if (GET_CODE (dest) == MEM
	       && GET_CODE (XEXP (dest, 0)) == LO_SUM
	       && symbolic_operand (XEXP (XEXP (dest, 0), 1), VOIDmode)
	       && ! read_only_operand (XEXP (XEXP (dest, 0), 1), VOIDmode))
	priority >>= 3;
    }
  return priority;
}

/* The 700 can only issue a single insn at a time.
   The 7XXX processors can issue two insns at a time.
   The 8000 can issue 4 insns at a time.  */
static int
pa_issue_rate (void)
{
  switch (pa_cpu)
    {
    case PROCESSOR_700:		return 1;
    case PROCESSOR_7100:	return 2;
    case PROCESSOR_7100LC:	return 2;
    case PROCESSOR_7200:	return 2;
    case PROCESSOR_7300:	return 2;
    case PROCESSOR_8000:	return 4;

    default:
      gcc_unreachable ();
    }
}



/* Return any length adjustment needed by INSN which already has its length
   computed as LENGTH.   Return zero if no adjustment is necessary.

   For the PA: function calls, millicode calls, and backwards short
   conditional branches with unfilled delay slots need an adjustment by +1
   (to account for the NOP which will be inserted into the instruction stream).

   Also compute the length of an inline block move here as it is too
   complicated to express as a length attribute in pa.md.  */
int
pa_adjust_insn_length (rtx insn, int length)
{
  rtx pat = PATTERN (insn);

  /* Jumps inside switch tables which have unfilled delay slots need
     adjustment.  */
  if (GET_CODE (insn) == JUMP_INSN
      && GET_CODE (pat) == PARALLEL
      && get_attr_type (insn) == TYPE_BTABLE_BRANCH)
    return 4;
  /* Millicode insn with an unfilled delay slot.  */
  else if (GET_CODE (insn) == INSN
	   && GET_CODE (pat) != SEQUENCE
	   && GET_CODE (pat) != USE
	   && GET_CODE (pat) != CLOBBER
	   && get_attr_type (insn) == TYPE_MILLI)
    return 4;
  /* Block move pattern.  */
  else if (GET_CODE (insn) == INSN
	   && GET_CODE (pat) == PARALLEL
	   && GET_CODE (XVECEXP (pat, 0, 0)) == SET
	   && GET_CODE (XEXP (XVECEXP (pat, 0, 0), 0)) == MEM
	   && GET_CODE (XEXP (XVECEXP (pat, 0, 0), 1)) == MEM
	   && GET_MODE (XEXP (XVECEXP (pat, 0, 0), 0)) == BLKmode
	   && GET_MODE (XEXP (XVECEXP (pat, 0, 0), 1)) == BLKmode)
    return compute_movmem_length (insn) - 4;
  /* Block clear pattern.  */
  else if (GET_CODE (insn) == INSN
	   && GET_CODE (pat) == PARALLEL
	   && GET_CODE (XVECEXP (pat, 0, 0)) == SET
	   && GET_CODE (XEXP (XVECEXP (pat, 0, 0), 0)) == MEM
	   && XEXP (XVECEXP (pat, 0, 0), 1) == const0_rtx
	   && GET_MODE (XEXP (XVECEXP (pat, 0, 0), 0)) == BLKmode)
    return compute_clrmem_length (insn) - 4;
  /* Conditional branch with an unfilled delay slot.  */
  else if (GET_CODE (insn) == JUMP_INSN && ! simplejump_p (insn))
    {
      /* Adjust a short backwards conditional with an unfilled delay slot.  */
      if (GET_CODE (pat) == SET
	  && length == 4
	  && ! forward_branch_p (insn))
	return 4;
      else if (GET_CODE (pat) == PARALLEL
	       && get_attr_type (insn) == TYPE_PARALLEL_BRANCH
	       && length == 4)
	return 4;
      /* Adjust dbra insn with short backwards conditional branch with
	 unfilled delay slot -- only for case where counter is in a
	 general register register.  */
      else if (GET_CODE (pat) == PARALLEL
	       && GET_CODE (XVECEXP (pat, 0, 1)) == SET
	       && GET_CODE (XEXP (XVECEXP (pat, 0, 1), 0)) == REG
 	       && ! FP_REG_P (XEXP (XVECEXP (pat, 0, 1), 0))
	       && length == 4
	       && ! forward_branch_p (insn))
	return 4;
      else
	return 0;
    }
  return 0;
}

/* Print operand X (an rtx) in assembler syntax to file FILE.
   CODE is a letter or dot (`z' in `%z0') or 0 if no letter was specified.
   For `%' followed by punctuation, CODE is the punctuation and X is null.  */

void
print_operand (FILE *file, rtx x, int code)
{
  switch (code)
    {
    case '#':
      /* Output a 'nop' if there's nothing for the delay slot.  */
      if (dbr_sequence_length () == 0)
	fputs ("\n\tnop", file);
      return;
    case '*':
      /* Output a nullification completer if there's nothing for the */
      /* delay slot or nullification is requested.  */
      if (dbr_sequence_length () == 0 ||
	  (final_sequence &&
	   INSN_ANNULLED_BRANCH_P (XVECEXP (final_sequence, 0, 0))))
        fputs (",n", file);
      return;
    case 'R':
      /* Print out the second register name of a register pair.
	 I.e., R (6) => 7.  */
      fputs (reg_names[REGNO (x) + 1], file);
      return;
    case 'r':
      /* A register or zero.  */
      if (x == const0_rtx
	  || (x == CONST0_RTX (DFmode))
	  || (x == CONST0_RTX (SFmode)))
	{
	  fputs ("%r0", file);
	  return;
	}
      else
	break;
    case 'f':
      /* A register or zero (floating point).  */
      if (x == const0_rtx
	  || (x == CONST0_RTX (DFmode))
	  || (x == CONST0_RTX (SFmode)))
	{
	  fputs ("%fr0", file);
	  return;
	}
      else
	break;
    case 'A':
      {
	rtx xoperands[2];

	xoperands[0] = XEXP (XEXP (x, 0), 0);
	xoperands[1] = XVECEXP (XEXP (XEXP (x, 0), 1), 0, 0);
	output_global_address (file, xoperands[1], 0);
        fprintf (file, "(%s)", reg_names [REGNO (xoperands[0])]);
	return;
      }

    case 'C':			/* Plain (C)ondition */
    case 'X':
      switch (GET_CODE (x))
	{
	case EQ:
	  fputs ("=", file);  break;
	case NE:
	  fputs ("<>", file);  break;
	case GT:
	  fputs (">", file);  break;
	case GE:
	  fputs (">=", file);  break;
	case GEU:
	  fputs (">>=", file);  break;
	case GTU:
	  fputs (">>", file);  break;
	case LT:
	  fputs ("<", file);  break;
	case LE:
	  fputs ("<=", file);  break;
	case LEU:
	  fputs ("<<=", file);  break;
	case LTU:
	  fputs ("<<", file);  break;
	default:
	  gcc_unreachable ();
	}
      return;
    case 'N':			/* Condition, (N)egated */
      switch (GET_CODE (x))
	{
	case EQ:
	  fputs ("<>", file);  break;
	case NE:
	  fputs ("=", file);  break;
	case GT:
	  fputs ("<=", file);  break;
	case GE:
	  fputs ("<", file);  break;
	case GEU:
	  fputs ("<<", file);  break;
	case GTU:
	  fputs ("<<=", file);  break;
	case LT:
	  fputs (">=", file);  break;
	case LE:
	  fputs (">", file);  break;
	case LEU:
	  fputs (">>", file);  break;
	case LTU:
	  fputs (">>=", file);  break;
	default:
	  gcc_unreachable ();
	}
      return;
    /* For floating point comparisons.  Note that the output
       predicates are the complement of the desired mode.  The
       conditions for GT, GE, LT, LE and LTGT cause an invalid
       operation exception if the result is unordered and this
       exception is enabled in the floating-point status register.  */
    case 'Y':
      switch (GET_CODE (x))
	{
	case EQ:
	  fputs ("!=", file);  break;
	case NE:
	  fputs ("=", file);  break;
	case GT:
	  fputs ("!>", file);  break;
	case GE:
	  fputs ("!>=", file);  break;
	case LT:
	  fputs ("!<", file);  break;
	case LE:
	  fputs ("!<=", file);  break;
	case LTGT:
	  fputs ("!<>", file);  break;
	case UNLE:
	  fputs ("!?<=", file);  break;
	case UNLT:
	  fputs ("!?<", file);  break;
	case UNGE:
	  fputs ("!?>=", file);  break;
	case UNGT:
	  fputs ("!?>", file);  break;
	case UNEQ:
	  fputs ("!?=", file);  break;
	case UNORDERED:
	  fputs ("!?", file);  break;
	case ORDERED:
	  fputs ("?", file);  break;
	default:
	  gcc_unreachable ();
	}
      return;
    case 'S':			/* Condition, operands are (S)wapped.  */
      switch (GET_CODE (x))
	{
	case EQ:
	  fputs ("=", file);  break;
	case NE:
	  fputs ("<>", file);  break;
	case GT:
	  fputs ("<", file);  break;
	case GE:
	  fputs ("<=", file);  break;
	case GEU:
	  fputs ("<<=", file);  break;
	case GTU:
	  fputs ("<<", file);  break;
	case LT:
	  fputs (">", file);  break;
	case LE:
	  fputs (">=", file);  break;
	case LEU:
	  fputs (">>=", file);  break;
	case LTU:
	  fputs (">>", file);  break;
	default:
	  gcc_unreachable ();
	}
      return;
    case 'B':			/* Condition, (B)oth swapped and negate.  */
      switch (GET_CODE (x))
	{
	case EQ:
	  fputs ("<>", file);  break;
	case NE:
	  fputs ("=", file);  break;
	case GT:
	  fputs (">=", file);  break;
	case GE:
	  fputs (">", file);  break;
	case GEU:
	  fputs (">>", file);  break;
	case GTU:
	  fputs (">>=", file);  break;
	case LT:
	  fputs ("<=", file);  break;
	case LE:
	  fputs ("<", file);  break;
	case LEU:
	  fputs ("<<", file);  break;
	case LTU:
	  fputs ("<<=", file);  break;
	default:
	  gcc_unreachable ();
	}
      return;
    case 'k':
      gcc_assert (GET_CODE (x) == CONST_INT);
      fprintf (file, HOST_WIDE_INT_PRINT_DEC, ~INTVAL (x));
      return;
    case 'Q':
      gcc_assert (GET_CODE (x) == CONST_INT);
      fprintf (file, HOST_WIDE_INT_PRINT_DEC, 64 - (INTVAL (x) & 63));
      return;
    case 'L':
      gcc_assert (GET_CODE (x) == CONST_INT);
      fprintf (file, HOST_WIDE_INT_PRINT_DEC, 32 - (INTVAL (x) & 31));
      return;
    case 'O':
      gcc_assert (GET_CODE (x) == CONST_INT && exact_log2 (INTVAL (x)) >= 0);
      fprintf (file, "%d", exact_log2 (INTVAL (x)));
      return;
    case 'p':
      gcc_assert (GET_CODE (x) == CONST_INT);
      fprintf (file, HOST_WIDE_INT_PRINT_DEC, 63 - (INTVAL (x) & 63));
      return;
    case 'P':
      gcc_assert (GET_CODE (x) == CONST_INT);
      fprintf (file, HOST_WIDE_INT_PRINT_DEC, 31 - (INTVAL (x) & 31));
      return;
    case 'I':
      if (GET_CODE (x) == CONST_INT)
	fputs ("i", file);
      return;
    case 'M':
    case 'F':
      switch (GET_CODE (XEXP (x, 0)))
	{
	case PRE_DEC:
	case PRE_INC:
	  if (ASSEMBLER_DIALECT == 0)
	    fputs ("s,mb", file);
	  else
	    fputs (",mb", file);
	  break;
	case POST_DEC:
	case POST_INC:
	  if (ASSEMBLER_DIALECT == 0)
	    fputs ("s,ma", file);
	  else
	    fputs (",ma", file);
	  break;
	case PLUS:
	  if (GET_CODE (XEXP (XEXP (x, 0), 0)) == REG
	      && GET_CODE (XEXP (XEXP (x, 0), 1)) == REG)
	    {
	      if (ASSEMBLER_DIALECT == 0)
		fputs ("x", file);
	    }
	  else if (GET_CODE (XEXP (XEXP (x, 0), 0)) == MULT
		   || GET_CODE (XEXP (XEXP (x, 0), 1)) == MULT)
	    {
	      if (ASSEMBLER_DIALECT == 0)
		fputs ("x,s", file);
	      else
		fputs (",s", file);
	    }
	  else if (code == 'F' && ASSEMBLER_DIALECT == 0)
	    fputs ("s", file);
	  break;
	default:
	  if (code == 'F' && ASSEMBLER_DIALECT == 0)
	    fputs ("s", file);
	  break;
	}
      return;
    case 'G':
      output_global_address (file, x, 0);
      return;
    case 'H':
      output_global_address (file, x, 1);
      return;
    case 0:			/* Don't do anything special */
      break;
    case 'Z':
      {
	unsigned op[3];
	compute_zdepwi_operands (INTVAL (x), op);
	fprintf (file, "%d,%d,%d", op[0], op[1], op[2]);
	return;
      }
    case 'z':
      {
	unsigned op[3];
	compute_zdepdi_operands (INTVAL (x), op);
	fprintf (file, "%d,%d,%d", op[0], op[1], op[2]);
	return;
      }
    case 'c':
      /* We can get here from a .vtable_inherit due to our
	 CONSTANT_ADDRESS_P rejecting perfectly good constant
	 addresses.  */
      break;
    default:
      gcc_unreachable ();
    }
  if (GET_CODE (x) == REG)
    {
      fputs (reg_names [REGNO (x)], file);
      if (TARGET_64BIT && FP_REG_P (x) && GET_MODE_SIZE (GET_MODE (x)) <= 4)
	{
	  fputs ("R", file);
	  return;
	}
      if (FP_REG_P (x)
	  && GET_MODE_SIZE (GET_MODE (x)) <= 4
	  && (REGNO (x) & 1) == 0)
	fputs ("L", file);
    }
  else if (GET_CODE (x) == MEM)
    {
      int size = GET_MODE_SIZE (GET_MODE (x));
      rtx base = NULL_RTX;
      switch (GET_CODE (XEXP (x, 0)))
	{
	case PRE_DEC:
	case POST_DEC:
          base = XEXP (XEXP (x, 0), 0);
	  fprintf (file, "-%d(%s)", size, reg_names [REGNO (base)]);
	  break;
	case PRE_INC:
	case POST_INC:
          base = XEXP (XEXP (x, 0), 0);
	  fprintf (file, "%d(%s)", size, reg_names [REGNO (base)]);
	  break;
	case PLUS:
	  if (GET_CODE (XEXP (XEXP (x, 0), 0)) == MULT)
	    fprintf (file, "%s(%s)",
		     reg_names [REGNO (XEXP (XEXP (XEXP (x, 0), 0), 0))],
		     reg_names [REGNO (XEXP (XEXP (x, 0), 1))]);
	  else if (GET_CODE (XEXP (XEXP (x, 0), 1)) == MULT)
	    fprintf (file, "%s(%s)",
		     reg_names [REGNO (XEXP (XEXP (XEXP (x, 0), 1), 0))],
		     reg_names [REGNO (XEXP (XEXP (x, 0), 0))]);
	  else if (GET_CODE (XEXP (XEXP (x, 0), 0)) == REG
		   && GET_CODE (XEXP (XEXP (x, 0), 1)) == REG)
	    {
	      /* Because the REG_POINTER flag can get lost during reload,
		 GO_IF_LEGITIMATE_ADDRESS canonicalizes the order of the
		 index and base registers in the combined move patterns.  */
	      rtx base = XEXP (XEXP (x, 0), 1);
	      rtx index = XEXP (XEXP (x, 0), 0);

	      fprintf (file, "%s(%s)",
		       reg_names [REGNO (index)], reg_names [REGNO (base)]);
	    }
	  else
	    output_address (XEXP (x, 0));
	  break;
	default:
	  output_address (XEXP (x, 0));
	  break;
	}
    }
  else
    output_addr_const (file, x);
}

/* output a SYMBOL_REF or a CONST expression involving a SYMBOL_REF.  */

void
output_global_address (FILE *file, rtx x, int round_constant)
{

  /* Imagine  (high (const (plus ...))).  */
  if (GET_CODE (x) == HIGH)
    x = XEXP (x, 0);

  if (GET_CODE (x) == SYMBOL_REF && read_only_operand (x, VOIDmode))
    output_addr_const (file, x);
  else if (GET_CODE (x) == SYMBOL_REF && !flag_pic)
    {
      output_addr_const (file, x);
      fputs ("-$global$", file);
    }
  else if (GET_CODE (x) == CONST)
    {
      const char *sep = "";
      int offset = 0;		/* assembler wants -$global$ at end */
      rtx base = NULL_RTX;

      switch (GET_CODE (XEXP (XEXP (x, 0), 0)))
	{
	case SYMBOL_REF:
	  base = XEXP (XEXP (x, 0), 0);
	  output_addr_const (file, base);
	  break;
	case CONST_INT:
	  offset = INTVAL (XEXP (XEXP (x, 0), 0));
	  break;
	default:
	  gcc_unreachable ();
	}

      switch (GET_CODE (XEXP (XEXP (x, 0), 1)))
	{
	case SYMBOL_REF:
	  base = XEXP (XEXP (x, 0), 1);
	  output_addr_const (file, base);
	  break;
	case CONST_INT:
	  offset = INTVAL (XEXP (XEXP (x, 0), 1));
	  break;
	default:
	  gcc_unreachable ();
	}

      /* How bogus.  The compiler is apparently responsible for
	 rounding the constant if it uses an LR field selector.

	 The linker and/or assembler seem a better place since
	 they have to do this kind of thing already.

	 If we fail to do this, HP's optimizing linker may eliminate
	 an addil, but not update the ldw/stw/ldo instruction that
	 uses the result of the addil.  */
      if (round_constant)
	offset = ((offset + 0x1000) & ~0x1fff);

      switch (GET_CODE (XEXP (x, 0)))
	{
	case PLUS:
	  if (offset < 0)
	    {
	      offset = -offset;
	      sep = "-";
	    }
	  else
	    sep = "+";
	  break;

	case MINUS:
	  gcc_assert (GET_CODE (XEXP (XEXP (x, 0), 0)) == SYMBOL_REF);
	  sep = "-";
	  break;

	default:
	  gcc_unreachable ();
	}
      
      if (!read_only_operand (base, VOIDmode) && !flag_pic)
	fputs ("-$global$", file);
      if (offset)
	fprintf (file, "%s%d", sep, offset);
    }
  else
    output_addr_const (file, x);
}

/* Output boilerplate text to appear at the beginning of the file.
   There are several possible versions.  */
#define aputs(x) fputs(x, asm_out_file)
static inline void
pa_file_start_level (void)
{
  if (TARGET_64BIT)
    aputs ("\t.LEVEL 2.0w\n");
  else if (TARGET_PA_20)
    aputs ("\t.LEVEL 2.0\n");
  else if (TARGET_PA_11)
    aputs ("\t.LEVEL 1.1\n");
  else
    aputs ("\t.LEVEL 1.0\n");
}

static inline void
pa_file_start_space (int sortspace)
{
  aputs ("\t.SPACE $PRIVATE$");
  if (sortspace)
    aputs (",SORT=16");
  aputs ("\n\t.SUBSPA $DATA$,QUAD=1,ALIGN=8,ACCESS=31"
         "\n\t.SUBSPA $BSS$,QUAD=1,ALIGN=8,ACCESS=31,ZERO,SORT=82"
         "\n\t.SPACE $TEXT$");
  if (sortspace)
    aputs (",SORT=8");
  aputs ("\n\t.SUBSPA $LIT$,QUAD=0,ALIGN=8,ACCESS=44"
         "\n\t.SUBSPA $CODE$,QUAD=0,ALIGN=8,ACCESS=44,CODE_ONLY\n");
}

static inline void
pa_file_start_file (int want_version)
{
  if (write_symbols != NO_DEBUG)
    {
      output_file_directive (asm_out_file, main_input_filename);
      if (want_version)
	aputs ("\t.version\t\"01.01\"\n");
    }
}

static inline void
pa_file_start_mcount (const char *aswhat)
{
  if (profile_flag)
    fprintf (asm_out_file, "\t.IMPORT _mcount,%s\n", aswhat);
}
  
static void
pa_elf_file_start (void)
{
  pa_file_start_level ();
  pa_file_start_mcount ("ENTRY");
  pa_file_start_file (0);
}

static void
pa_som_file_start (void)
{
  pa_file_start_level ();
  pa_file_start_space (0);
  aputs ("\t.IMPORT $global$,DATA\n"
         "\t.IMPORT $$dyncall,MILLICODE\n");
  pa_file_start_mcount ("CODE");
  pa_file_start_file (0);
}

static void
pa_linux_file_start (void)
{
  pa_file_start_file (1);
  pa_file_start_level ();
  pa_file_start_mcount ("CODE");
}

static void
pa_hpux64_gas_file_start (void)
{
  pa_file_start_level ();
#ifdef ASM_OUTPUT_TYPE_DIRECTIVE
  if (profile_flag)
    ASM_OUTPUT_TYPE_DIRECTIVE (asm_out_file, "_mcount", "function");
#endif
  pa_file_start_file (1);
}

static void
pa_hpux64_hpas_file_start (void)
{
  pa_file_start_level ();
  pa_file_start_space (1);
  pa_file_start_mcount ("CODE");
  pa_file_start_file (0);
}
#undef aputs

/* Search the deferred plabel list for SYMBOL and return its internal
   label.  If an entry for SYMBOL is not found, a new entry is created.  */

rtx
get_deferred_plabel (rtx symbol)
{
  const char *fname = XSTR (symbol, 0);
  size_t i;

  /* See if we have already put this function on the list of deferred
     plabels.  This list is generally small, so a liner search is not
     too ugly.  If it proves too slow replace it with something faster.  */
  for (i = 0; i < n_deferred_plabels; i++)
    if (strcmp (fname, XSTR (deferred_plabels[i].symbol, 0)) == 0)
      break;

  /* If the deferred plabel list is empty, or this entry was not found
     on the list, create a new entry on the list.  */
  if (deferred_plabels == NULL || i == n_deferred_plabels)
    {
      tree id;

      if (deferred_plabels == 0)
	deferred_plabels = (struct deferred_plabel *)
	  ggc_alloc (sizeof (struct deferred_plabel));
      else
	deferred_plabels = (struct deferred_plabel *)
	  ggc_realloc (deferred_plabels,
		       ((n_deferred_plabels + 1)
			* sizeof (struct deferred_plabel)));

      i = n_deferred_plabels++;
      deferred_plabels[i].internal_label = gen_label_rtx ();
      deferred_plabels[i].symbol = symbol;

      /* Gross.  We have just implicitly taken the address of this
	 function.  Mark it in the same manner as assemble_name.  */
      id = maybe_get_identifier (targetm.strip_name_encoding (fname));
      if (id)
	mark_referenced (id);
    }

  return deferred_plabels[i].internal_label;
}

static void
output_deferred_plabels (void)
{
  size_t i;

  /* If we have some deferred plabels, then we need to switch into the
     data or readonly data section, and align it to a 4 byte boundary
     before outputting the deferred plabels.  */
  if (n_deferred_plabels)
    {
      switch_to_section (flag_pic ? data_section : readonly_data_section);
      ASM_OUTPUT_ALIGN (asm_out_file, TARGET_64BIT ? 3 : 2);
    }

  /* Now output the deferred plabels.  */
  for (i = 0; i < n_deferred_plabels; i++)
    {
      (*targetm.asm_out.internal_label) (asm_out_file, "L",
		 CODE_LABEL_NUMBER (deferred_plabels[i].internal_label));
      assemble_integer (deferred_plabels[i].symbol,
			TARGET_64BIT ? 8 : 4, TARGET_64BIT ? 64 : 32, 1);
    }
}

#ifdef HPUX_LONG_DOUBLE_LIBRARY
/* Initialize optabs to point to HPUX long double emulation routines.  */
static void
pa_hpux_init_libfuncs (void)
{
  set_optab_libfunc (add_optab, TFmode, "_U_Qfadd");
  set_optab_libfunc (sub_optab, TFmode, "_U_Qfsub");
  set_optab_libfunc (smul_optab, TFmode, "_U_Qfmpy");
  set_optab_libfunc (sdiv_optab, TFmode, "_U_Qfdiv");
  set_optab_libfunc (smin_optab, TFmode, "_U_Qmin");
  set_optab_libfunc (smax_optab, TFmode, "_U_Qfmax");
  set_optab_libfunc (sqrt_optab, TFmode, "_U_Qfsqrt");
  set_optab_libfunc (abs_optab, TFmode, "_U_Qfabs");
  set_optab_libfunc (neg_optab, TFmode, "_U_Qfneg");

  set_optab_libfunc (eq_optab, TFmode, "_U_Qfeq");
  set_optab_libfunc (ne_optab, TFmode, "_U_Qfne");
  set_optab_libfunc (gt_optab, TFmode, "_U_Qfgt");
  set_optab_libfunc (ge_optab, TFmode, "_U_Qfge");
  set_optab_libfunc (lt_optab, TFmode, "_U_Qflt");
  set_optab_libfunc (le_optab, TFmode, "_U_Qfle");
  set_optab_libfunc (unord_optab, TFmode, "_U_Qfunord");

  set_conv_libfunc (sext_optab,   TFmode, SFmode, "_U_Qfcnvff_sgl_to_quad");
  set_conv_libfunc (sext_optab,   TFmode, DFmode, "_U_Qfcnvff_dbl_to_quad");
  set_conv_libfunc (trunc_optab,  SFmode, TFmode, "_U_Qfcnvff_quad_to_sgl");
  set_conv_libfunc (trunc_optab,  DFmode, TFmode, "_U_Qfcnvff_quad_to_dbl");

  set_conv_libfunc (sfix_optab,   SImode, TFmode, TARGET_64BIT
						  ? "__U_Qfcnvfxt_quad_to_sgl"
						  : "_U_Qfcnvfxt_quad_to_sgl");
  set_conv_libfunc (sfix_optab,   DImode, TFmode, "_U_Qfcnvfxt_quad_to_dbl");
  set_conv_libfunc (ufix_optab,   SImode, TFmode, "_U_Qfcnvfxt_quad_to_usgl");
  set_conv_libfunc (ufix_optab,   DImode, TFmode, "_U_Qfcnvfxt_quad_to_udbl");

  set_conv_libfunc (sfloat_optab, TFmode, SImode, "_U_Qfcnvxf_sgl_to_quad");
  set_conv_libfunc (sfloat_optab, TFmode, DImode, "_U_Qfcnvxf_dbl_to_quad");
  set_conv_libfunc (ufloat_optab, TFmode, SImode, "_U_Qfcnvxf_usgl_to_quad");
  set_conv_libfunc (ufloat_optab, TFmode, DImode, "_U_Qfcnvxf_udbl_to_quad");
}
#endif

/* HP's millicode routines mean something special to the assembler.
   Keep track of which ones we have used.  */

enum millicodes { remI, remU, divI, divU, mulI, end1000 };
static void import_milli (enum millicodes);
static char imported[(int) end1000];
static const char * const milli_names[] = {"remI", "remU", "divI", "divU", "mulI"};
static const char import_string[] = ".IMPORT $$....,MILLICODE";
#define MILLI_START 10

static void
import_milli (enum millicodes code)
{
  char str[sizeof (import_string)];

  if (!imported[(int) code])
    {
      imported[(int) code] = 1;
      strcpy (str, import_string);
      strncpy (str + MILLI_START, milli_names[(int) code], 4);
      output_asm_insn (str, 0);
    }
}

/* The register constraints have put the operands and return value in
   the proper registers.  */

const char *
output_mul_insn (int unsignedp ATTRIBUTE_UNUSED, rtx insn)
{
  import_milli (mulI);
  return output_millicode_call (insn, gen_rtx_SYMBOL_REF (Pmode, "$$mulI"));
}

/* Emit the rtl for doing a division by a constant.  */

/* Do magic division millicodes exist for this value? */
const int magic_milli[]= {0, 0, 0, 1, 0, 1, 1, 1, 0, 1, 1, 0, 1, 0, 1, 1};

/* We'll use an array to keep track of the magic millicodes and
   whether or not we've used them already. [n][0] is signed, [n][1] is
   unsigned.  */

static int div_milli[16][2];

int
emit_hpdiv_const (rtx *operands, int unsignedp)
{
  if (GET_CODE (operands[2]) == CONST_INT
      && INTVAL (operands[2]) > 0
      && INTVAL (operands[2]) < 16
      && magic_milli[INTVAL (operands[2])])
    {
      rtx ret = gen_rtx_REG (SImode, TARGET_64BIT ? 2 : 31);

      emit_move_insn (gen_rtx_REG (SImode, 26), operands[1]);
      emit
	(gen_rtx_PARALLEL
	 (VOIDmode,
	  gen_rtvec (6, gen_rtx_SET (VOIDmode, gen_rtx_REG (SImode, 29),
				     gen_rtx_fmt_ee (unsignedp ? UDIV : DIV,
						     SImode,
						     gen_rtx_REG (SImode, 26),
						     operands[2])),
		     gen_rtx_CLOBBER (VOIDmode, operands[4]),
		     gen_rtx_CLOBBER (VOIDmode, operands[3]),
		     gen_rtx_CLOBBER (VOIDmode, gen_rtx_REG (SImode, 26)),
		     gen_rtx_CLOBBER (VOIDmode, gen_rtx_REG (SImode, 25)),
		     gen_rtx_CLOBBER (VOIDmode, ret))));
      emit_move_insn (operands[0], gen_rtx_REG (SImode, 29));
      return 1;
    }
  return 0;
}

const char *
output_div_insn (rtx *operands, int unsignedp, rtx insn)
{
  int divisor;

  /* If the divisor is a constant, try to use one of the special
     opcodes .*/
  if (GET_CODE (operands[0]) == CONST_INT)
    {
      static char buf[100];
      divisor = INTVAL (operands[0]);
      if (!div_milli[divisor][unsignedp])
	{
	  div_milli[divisor][unsignedp] = 1;
	  if (unsignedp)
	    output_asm_insn (".IMPORT $$divU_%0,MILLICODE", operands);
	  else
	    output_asm_insn (".IMPORT $$divI_%0,MILLICODE", operands);
	}
      if (unsignedp)
	{
	  sprintf (buf, "$$divU_" HOST_WIDE_INT_PRINT_DEC,
		   INTVAL (operands[0]));
	  return output_millicode_call (insn,
					gen_rtx_SYMBOL_REF (SImode, buf));
	}
      else
	{
	  sprintf (buf, "$$divI_" HOST_WIDE_INT_PRINT_DEC,
		   INTVAL (operands[0]));
	  return output_millicode_call (insn,
					gen_rtx_SYMBOL_REF (SImode, buf));
	}
    }
  /* Divisor isn't a special constant.  */
  else
    {
      if (unsignedp)
	{
	  import_milli (divU);
	  return output_millicode_call (insn,
					gen_rtx_SYMBOL_REF (SImode, "$$divU"));
	}
      else
	{
	  import_milli (divI);
	  return output_millicode_call (insn,
					gen_rtx_SYMBOL_REF (SImode, "$$divI"));
	}
    }
}

/* Output a $$rem millicode to do mod.  */

const char *
output_mod_insn (int unsignedp, rtx insn)
{
  if (unsignedp)
    {
      import_milli (remU);
      return output_millicode_call (insn,
				    gen_rtx_SYMBOL_REF (SImode, "$$remU"));
    }
  else
    {
      import_milli (remI);
      return output_millicode_call (insn,
				    gen_rtx_SYMBOL_REF (SImode, "$$remI"));
    }
}

void
output_arg_descriptor (rtx call_insn)
{
  const char *arg_regs[4];
  enum machine_mode arg_mode;
  rtx link;
  int i, output_flag = 0;
  int regno;

  /* We neither need nor want argument location descriptors for the
     64bit runtime environment or the ELF32 environment.  */
  if (TARGET_64BIT || TARGET_ELF32)
    return;

  for (i = 0; i < 4; i++)
    arg_regs[i] = 0;

  /* Specify explicitly that no argument relocations should take place
     if using the portable runtime calling conventions.  */
  if (TARGET_PORTABLE_RUNTIME)
    {
      fputs ("\t.CALL ARGW0=NO,ARGW1=NO,ARGW2=NO,ARGW3=NO,RETVAL=NO\n",
	     asm_out_file);
      return;
    }

  gcc_assert (GET_CODE (call_insn) == CALL_INSN);
  for (link = CALL_INSN_FUNCTION_USAGE (call_insn);
       link; link = XEXP (link, 1))
    {
      rtx use = XEXP (link, 0);

      if (! (GET_CODE (use) == USE
	     && GET_CODE (XEXP (use, 0)) == REG
	     && FUNCTION_ARG_REGNO_P (REGNO (XEXP (use, 0)))))
	continue;

      arg_mode = GET_MODE (XEXP (use, 0));
      regno = REGNO (XEXP (use, 0));
      if (regno >= 23 && regno <= 26)
	{
	  arg_regs[26 - regno] = "GR";
	  if (arg_mode == DImode)
	    arg_regs[25 - regno] = "GR";
	}
      else if (regno >= 32 && regno <= 39)
	{
	  if (arg_mode == SFmode)
	    arg_regs[(regno - 32) / 2] = "FR";
	  else
	    {
#ifndef HP_FP_ARG_DESCRIPTOR_REVERSED
	      arg_regs[(regno - 34) / 2] = "FR";
	      arg_regs[(regno - 34) / 2 + 1] = "FU";
#else
	      arg_regs[(regno - 34) / 2] = "FU";
	      arg_regs[(regno - 34) / 2 + 1] = "FR";
#endif
	    }
	}
    }
  fputs ("\t.CALL ", asm_out_file);
  for (i = 0; i < 4; i++)
    {
      if (arg_regs[i])
	{
	  if (output_flag++)
	    fputc (',', asm_out_file);
	  fprintf (asm_out_file, "ARGW%d=%s", i, arg_regs[i]);
	}
    }
  fputc ('\n', asm_out_file);
}

static enum reg_class
pa_secondary_reload (bool in_p, rtx x, enum reg_class class,
		     enum machine_mode mode, secondary_reload_info *sri)
{
  int is_symbolic, regno;

  /* Handle the easy stuff first.  */
  if (class == R1_REGS)
    return NO_REGS;

  if (REG_P (x))
    {
      regno = REGNO (x);
      if (class == BASE_REG_CLASS && regno < FIRST_PSEUDO_REGISTER)
	return NO_REGS;
    }
  else
    regno = -1;

  /* If we have something like (mem (mem (...)), we can safely assume the
     inner MEM will end up in a general register after reloading, so there's
     no need for a secondary reload.  */
  if (GET_CODE (x) == MEM && GET_CODE (XEXP (x, 0)) == MEM)
    return NO_REGS;

  /* Trying to load a constant into a FP register during PIC code
     generation requires %r1 as a scratch register.  */
  if (flag_pic
      && (mode == SImode || mode == DImode)
      && FP_REG_CLASS_P (class)
      && (GET_CODE (x) == CONST_INT || GET_CODE (x) == CONST_DOUBLE))
    {
      sri->icode = (mode == SImode ? CODE_FOR_reload_insi_r1
		    : CODE_FOR_reload_indi_r1);
      return NO_REGS;
    }

  /* Profiling showed the PA port spends about 1.3% of its compilation
     time in true_regnum from calls inside pa_secondary_reload_class.  */
  if (regno >= FIRST_PSEUDO_REGISTER || GET_CODE (x) == SUBREG)
    regno = true_regnum (x);

  /* Handle out of range displacement for integer mode loads/stores of
     FP registers.  */
  if (((regno >= FIRST_PSEUDO_REGISTER || regno == -1)
       && GET_MODE_CLASS (mode) == MODE_INT
       && FP_REG_CLASS_P (class))
      || (class == SHIFT_REGS && (regno <= 0 || regno >= 32)))
    {
      sri->icode = in_p ? reload_in_optab[mode] : reload_out_optab[mode];
      return NO_REGS;
    }

  /* A SAR<->FP register copy requires a secondary register (GPR) as
     well as secondary memory.  */
  if (regno >= 0 && regno < FIRST_PSEUDO_REGISTER
      && ((REGNO_REG_CLASS (regno) == SHIFT_REGS && FP_REG_CLASS_P (class))
	  || (class == SHIFT_REGS
	      && FP_REG_CLASS_P (REGNO_REG_CLASS (regno)))))
    {
      sri->icode = in_p ? reload_in_optab[mode] : reload_out_optab[mode];
      return NO_REGS;
    }

  /* Secondary reloads of symbolic operands require %r1 as a scratch
     register when we're generating PIC code and the operand isn't
     readonly.  */
  if (GET_CODE (x) == HIGH)
    x = XEXP (x, 0);

  /* Profiling has showed GCC spends about 2.6% of its compilation
     time in symbolic_operand from calls inside pa_secondary_reload_class.
     So, we use an inline copy to avoid useless work.  */
  switch (GET_CODE (x))
    {
      rtx op;

      case SYMBOL_REF:
        is_symbolic = !SYMBOL_REF_TLS_MODEL (x);
        break;
      case LABEL_REF:
        is_symbolic = 1;
        break;
      case CONST:
	op = XEXP (x, 0);
	is_symbolic = (((GET_CODE (XEXP (op, 0)) == SYMBOL_REF
			 && !SYMBOL_REF_TLS_MODEL (XEXP (op, 0)))
			|| GET_CODE (XEXP (op, 0)) == LABEL_REF)
		       && GET_CODE (XEXP (op, 1)) == CONST_INT);
        break;
      default:
        is_symbolic = 0;
        break;
    }

  if (is_symbolic && (flag_pic || !read_only_operand (x, VOIDmode)))
    {
      gcc_assert (mode == SImode || mode == DImode);
      sri->icode = (mode == SImode ? CODE_FOR_reload_insi_r1
		    : CODE_FOR_reload_indi_r1);
    }

  return NO_REGS;
}

/* In the 32-bit runtime, arguments larger than eight bytes are passed
   by invisible reference.  As a GCC extension, we also pass anything
   with a zero or variable size by reference.

   The 64-bit runtime does not describe passing any types by invisible
   reference.  The internals of GCC can't currently handle passing
   empty structures, and zero or variable length arrays when they are
   not passed entirely on the stack or by reference.  Thus, as a GCC
   extension, we pass these types by reference.  The HP compiler doesn't
   support these types, so hopefully there shouldn't be any compatibility
   issues.  This may have to be revisited when HP releases a C99 compiler
   or updates the ABI.  */

static bool
pa_pass_by_reference (CUMULATIVE_ARGS *ca ATTRIBUTE_UNUSED,
		      enum machine_mode mode, tree type,
		      bool named ATTRIBUTE_UNUSED)
{
  HOST_WIDE_INT size;

  if (type)
    size = int_size_in_bytes (type);
  else
    size = GET_MODE_SIZE (mode);

  if (TARGET_64BIT)
    return size <= 0;
  else
    return size <= 0 || size > 8;
}

enum direction
function_arg_padding (enum machine_mode mode, tree type)
{
  if (mode == BLKmode
      || (TARGET_64BIT && type && AGGREGATE_TYPE_P (type)))
    {
      /* Return none if justification is not required.  */
      if (type
	  && TREE_CODE (TYPE_SIZE (type)) == INTEGER_CST
	  && (int_size_in_bytes (type) * BITS_PER_UNIT) % PARM_BOUNDARY == 0)
	return none;

      /* The directions set here are ignored when a BLKmode argument larger
	 than a word is placed in a register.  Different code is used for
	 the stack and registers.  This makes it difficult to have a
	 consistent data representation for both the stack and registers.
	 For both runtimes, the justification and padding for arguments on
	 the stack and in registers should be identical.  */
      if (TARGET_64BIT)
	/* The 64-bit runtime specifies left justification for aggregates.  */
        return upward;
      else
	/* The 32-bit runtime architecture specifies right justification.
	   When the argument is passed on the stack, the argument is padded
	   with garbage on the left.  The HP compiler pads with zeros.  */
	return downward;
    }

  if (GET_MODE_BITSIZE (mode) < PARM_BOUNDARY)
    return downward;
  else
    return none;
}


/* Do what is necessary for `va_start'.  We look at the current function
   to determine if stdargs or varargs is used and fill in an initial
   va_list.  A pointer to this constructor is returned.  */

static rtx
hppa_builtin_saveregs (void)
{
  rtx offset, dest;
  tree fntype = TREE_TYPE (current_function_decl);
  int argadj = ((!(TYPE_ARG_TYPES (fntype) != 0
		   && (TREE_VALUE (tree_last (TYPE_ARG_TYPES (fntype)))
		       != void_type_node)))
		? UNITS_PER_WORD : 0);

  if (argadj)
    offset = plus_constant (current_function_arg_offset_rtx, argadj);
  else
    offset = current_function_arg_offset_rtx;

  if (TARGET_64BIT)
    {
      int i, off;

      /* Adjust for varargs/stdarg differences.  */
      if (argadj)
	offset = plus_constant (current_function_arg_offset_rtx, -argadj);
      else
	offset = current_function_arg_offset_rtx;

      /* We need to save %r26 .. %r19 inclusive starting at offset -64
	 from the incoming arg pointer and growing to larger addresses.  */
      for (i = 26, off = -64; i >= 19; i--, off += 8)
	emit_move_insn (gen_rtx_MEM (word_mode,
				     plus_constant (arg_pointer_rtx, off)),
			gen_rtx_REG (word_mode, i));

      /* The incoming args pointer points just beyond the flushback area;
	 normally this is not a serious concern.  However, when we are doing
	 varargs/stdargs we want to make the arg pointer point to the start
	 of the incoming argument area.  */
      emit_move_insn (virtual_incoming_args_rtx,
		      plus_constant (arg_pointer_rtx, -64));

      /* Now return a pointer to the first anonymous argument.  */
      return copy_to_reg (expand_binop (Pmode, add_optab,
					virtual_incoming_args_rtx,
					offset, 0, 0, OPTAB_LIB_WIDEN));
    }

  /* Store general registers on the stack.  */
  dest = gen_rtx_MEM (BLKmode,
		      plus_constant (current_function_internal_arg_pointer,
				     -16));
  set_mem_alias_set (dest, get_varargs_alias_set ());
  set_mem_align (dest, BITS_PER_WORD);
  move_block_from_reg (23, dest, 4);

  /* move_block_from_reg will emit code to store the argument registers
     individually as scalar stores.

     However, other insns may later load from the same addresses for
     a structure load (passing a struct to a varargs routine).

     The alias code assumes that such aliasing can never happen, so we
     have to keep memory referencing insns from moving up beyond the
     last argument register store.  So we emit a blockage insn here.  */
  emit_insn (gen_blockage ());

  return copy_to_reg (expand_binop (Pmode, add_optab,
				    current_function_internal_arg_pointer,
				    offset, 0, 0, OPTAB_LIB_WIDEN));
}

void
hppa_va_start (tree valist, rtx nextarg)
{
  nextarg = expand_builtin_saveregs ();
  std_expand_builtin_va_start (valist, nextarg);
}

static tree
hppa_gimplify_va_arg_expr (tree valist, tree type, tree *pre_p, tree *post_p)
{
  if (TARGET_64BIT)
    {
      /* Args grow upward.  We can use the generic routines.  */
      return std_gimplify_va_arg_expr (valist, type, pre_p, post_p);
    }
  else /* !TARGET_64BIT */
    {
      tree ptr = build_pointer_type (type);
      tree valist_type;
      tree t, u;
      unsigned int size, ofs;
      bool indirect;

      indirect = pass_by_reference (NULL, TYPE_MODE (type), type, 0);
      if (indirect)
	{
	  type = ptr;
	  ptr = build_pointer_type (type);
	}
      size = int_size_in_bytes (type);
      valist_type = TREE_TYPE (valist);

      /* Args grow down.  Not handled by generic routines.  */

      u = fold_convert (valist_type, size_in_bytes (type));
      t = build2 (MINUS_EXPR, valist_type, valist, u);

      /* Copied from va-pa.h, but we probably don't need to align to
	 word size, since we generate and preserve that invariant.  */
      u = build_int_cst (valist_type, (size > 4 ? -8 : -4));
      t = build2 (BIT_AND_EXPR, valist_type, t, u);

      t = build2 (MODIFY_EXPR, valist_type, valist, t);

      ofs = (8 - size) % 4;
      if (ofs != 0)
	{
	  u = fold_convert (valist_type, size_int (ofs));
	  t = build2 (PLUS_EXPR, valist_type, t, u);
	}

      t = fold_convert (ptr, t);
      t = build_va_arg_indirect_ref (t);

      if (indirect)
	t = build_va_arg_indirect_ref (t);

      return t;
    }
}

/* True if MODE is valid for the target.  By "valid", we mean able to
   be manipulated in non-trivial ways.  In particular, this means all
   the arithmetic is supported.

   Currently, TImode is not valid as the HP 64-bit runtime documentation
   doesn't document the alignment and calling conventions for this type. 
   Thus, we return false when PRECISION is 2 * BITS_PER_WORD and
   2 * BITS_PER_WORD isn't equal LONG_LONG_TYPE_SIZE.  */

static bool
pa_scalar_mode_supported_p (enum machine_mode mode)
{
  int precision = GET_MODE_PRECISION (mode);

  switch (GET_MODE_CLASS (mode))
    {
    case MODE_PARTIAL_INT:
    case MODE_INT:
      if (precision == CHAR_TYPE_SIZE)
	return true;
      if (precision == SHORT_TYPE_SIZE)
	return true;
      if (precision == INT_TYPE_SIZE)
	return true;
      if (precision == LONG_TYPE_SIZE)
	return true;
      if (precision == LONG_LONG_TYPE_SIZE)
	return true;
      return false;

    case MODE_FLOAT:
      if (precision == FLOAT_TYPE_SIZE)
	return true;
      if (precision == DOUBLE_TYPE_SIZE)
	return true;
      if (precision == LONG_DOUBLE_TYPE_SIZE)
	return true;
      return false;

    case MODE_DECIMAL_FLOAT:
      return false;

    default:
      gcc_unreachable ();
    }
}

/* This routine handles all the normal conditional branch sequences we
   might need to generate.  It handles compare immediate vs compare
   register, nullification of delay slots, varying length branches,
   negated branches, and all combinations of the above.  It returns the
   output appropriate to emit the branch corresponding to all given
   parameters.  */

const char *
output_cbranch (rtx *operands, int negated, rtx insn)
{
  static char buf[100];
  int useskip = 0;
  int nullify = INSN_ANNULLED_BRANCH_P (insn);
  int length = get_attr_length (insn);
  int xdelay;

  /* A conditional branch to the following instruction (e.g. the delay slot)
     is asking for a disaster.  This can happen when not optimizing and
     when jump optimization fails.

     While it is usually safe to emit nothing, this can fail if the
     preceding instruction is a nullified branch with an empty delay
     slot and the same branch target as this branch.  We could check
     for this but jump optimization should eliminate nop jumps.  It
     is always safe to emit a nop.  */
  if (next_real_insn (JUMP_LABEL (insn)) == next_real_insn (insn))
    return "nop";

  /* The doubleword form of the cmpib instruction doesn't have the LEU
     and GTU conditions while the cmpb instruction does.  Since we accept
     zero for cmpb, we must ensure that we use cmpb for the comparison.  */
  if (GET_MODE (operands[1]) == DImode && operands[2] == const0_rtx)
    operands[2] = gen_rtx_REG (DImode, 0);
  if (GET_MODE (operands[2]) == DImode && operands[1] == const0_rtx)
    operands[1] = gen_rtx_REG (DImode, 0);

  /* If this is a long branch with its delay slot unfilled, set `nullify'
     as it can nullify the delay slot and save a nop.  */
  if (length == 8 && dbr_sequence_length () == 0)
    nullify = 1;

  /* If this is a short forward conditional branch which did not get
     its delay slot filled, the delay slot can still be nullified.  */
  if (! nullify && length == 4 && dbr_sequence_length () == 0)
    nullify = forward_branch_p (insn);

  /* A forward branch over a single nullified insn can be done with a
     comclr instruction.  This avoids a single cycle penalty due to
     mis-predicted branch if we fall through (branch not taken).  */
  if (length == 4
      && next_real_insn (insn) != 0
      && get_attr_length (next_real_insn (insn)) == 4
      && JUMP_LABEL (insn) == next_nonnote_insn (next_real_insn (insn))
      && nullify)
    useskip = 1;

  switch (length)
    {
      /* All short conditional branches except backwards with an unfilled
	 delay slot.  */
      case 4:
	if (useskip)
	  strcpy (buf, "{com%I2clr,|cmp%I2clr,}");
	else
	  strcpy (buf, "{com%I2b,|cmp%I2b,}");
	if (GET_MODE (operands[1]) == DImode)
	  strcat (buf, "*");
	if (negated)
	  strcat (buf, "%B3");
	else
	  strcat (buf, "%S3");
	if (useskip)
	  strcat (buf, " %2,%r1,%%r0");
	else if (nullify)
	  strcat (buf, ",n %2,%r1,%0");
	else
	  strcat (buf, " %2,%r1,%0");
	break;

     /* All long conditionals.  Note a short backward branch with an
	unfilled delay slot is treated just like a long backward branch
	with an unfilled delay slot.  */
      case 8:
	/* Handle weird backwards branch with a filled delay slot
	   which is nullified.  */
	if (dbr_sequence_length () != 0
	    && ! forward_branch_p (insn)
	    && nullify)
	  {
	    strcpy (buf, "{com%I2b,|cmp%I2b,}");
	    if (GET_MODE (operands[1]) == DImode)
	      strcat (buf, "*");
	    if (negated)
	      strcat (buf, "%S3");
	    else
	      strcat (buf, "%B3");
	    strcat (buf, ",n %2,%r1,.+12\n\tb %0");
	  }
	/* Handle short backwards branch with an unfilled delay slot.
	   Using a comb;nop rather than comiclr;bl saves 1 cycle for both
	   taken and untaken branches.  */
	else if (dbr_sequence_length () == 0
		 && ! forward_branch_p (insn)
		 && INSN_ADDRESSES_SET_P ()
		 && VAL_14_BITS_P (INSN_ADDRESSES (INSN_UID (JUMP_LABEL (insn)))
				    - INSN_ADDRESSES (INSN_UID (insn)) - 8))
	  {
	    strcpy (buf, "{com%I2b,|cmp%I2b,}");
	    if (GET_MODE (operands[1]) == DImode)
	      strcat (buf, "*");
	    if (negated)
	      strcat (buf, "%B3 %2,%r1,%0%#");
	    else
	      strcat (buf, "%S3 %2,%r1,%0%#");
	  }
	else
	  {
	    strcpy (buf, "{com%I2clr,|cmp%I2clr,}");
	    if (GET_MODE (operands[1]) == DImode)
	      strcat (buf, "*");
	    if (negated)
	      strcat (buf, "%S3");
	    else
	      strcat (buf, "%B3");
	    if (nullify)
	      strcat (buf, " %2,%r1,%%r0\n\tb,n %0");
	    else
	      strcat (buf, " %2,%r1,%%r0\n\tb %0");
	  }
	break;

      default:
	/* The reversed conditional branch must branch over one additional
	   instruction if the delay slot is filled and needs to be extracted
	   by output_lbranch.  If the delay slot is empty or this is a
	   nullified forward branch, the instruction after the reversed
	   condition branch must be nullified.  */
	if (dbr_sequence_length () == 0
	    || (nullify && forward_branch_p (insn)))
	  {
	    nullify = 1;
	    xdelay = 0;
	    operands[4] = GEN_INT (length);
	  }
	else
	  {
	    xdelay = 1;
	    operands[4] = GEN_INT (length + 4);
	  }

	/* Create a reversed conditional branch which branches around
	   the following insns.  */
	if (GET_MODE (operands[1]) != DImode)
	  {
	    if (nullify)
	      {
		if (negated)
		  strcpy (buf,
		    "{com%I2b,%S3,n %2,%r1,.+%4|cmp%I2b,%S3,n %2,%r1,.+%4}");
		else
		  strcpy (buf,
		    "{com%I2b,%B3,n %2,%r1,.+%4|cmp%I2b,%B3,n %2,%r1,.+%4}");
	      }
	    else
	      {
		if (negated)
		  strcpy (buf,
		    "{com%I2b,%S3 %2,%r1,.+%4|cmp%I2b,%S3 %2,%r1,.+%4}");
		else
		  strcpy (buf,
		    "{com%I2b,%B3 %2,%r1,.+%4|cmp%I2b,%B3 %2,%r1,.+%4}");
	      }
	  }
	else
	  {
	    if (nullify)
	      {
		if (negated)
		  strcpy (buf,
		    "{com%I2b,*%S3,n %2,%r1,.+%4|cmp%I2b,*%S3,n %2,%r1,.+%4}");
		else
		  strcpy (buf,
		    "{com%I2b,*%B3,n %2,%r1,.+%4|cmp%I2b,*%B3,n %2,%r1,.+%4}");
	      }
	    else
	      {
		if (negated)
		  strcpy (buf,
		    "{com%I2b,*%S3 %2,%r1,.+%4|cmp%I2b,*%S3 %2,%r1,.+%4}");
		else
		  strcpy (buf,
		    "{com%I2b,*%B3 %2,%r1,.+%4|cmp%I2b,*%B3 %2,%r1,.+%4}");
	      }
	  }

	output_asm_insn (buf, operands);
	return output_lbranch (operands[0], insn, xdelay);
    }
  return buf;
}

/* This routine handles output of long unconditional branches that
   exceed the maximum range of a simple branch instruction.  Since
   we don't have a register available for the branch, we save register
   %r1 in the frame marker, load the branch destination DEST into %r1,
   execute the branch, and restore %r1 in the delay slot of the branch.

   Since long branches may have an insn in the delay slot and the
   delay slot is used to restore %r1, we in general need to extract
   this insn and execute it before the branch.  However, to facilitate
   use of this function by conditional branches, we also provide an
   option to not extract the delay insn so that it will be emitted
   after the long branch.  So, if there is an insn in the delay slot,
   it is extracted if XDELAY is nonzero.

   The lengths of the various long-branch sequences are 20, 16 and 24
   bytes for the portable runtime, non-PIC and PIC cases, respectively.  */

const char *
output_lbranch (rtx dest, rtx insn, int xdelay)
{
  rtx xoperands[2];
 
  xoperands[0] = dest;

  /* First, free up the delay slot.  */
  if (xdelay && dbr_sequence_length () != 0)
    {
      /* We can't handle a jump in the delay slot.  */
      gcc_assert (GET_CODE (NEXT_INSN (insn)) != JUMP_INSN);

      final_scan_insn (NEXT_INSN (insn), asm_out_file,
		       optimize, 0, NULL);

      /* Now delete the delay insn.  */
      PUT_CODE (NEXT_INSN (insn), NOTE);
      NOTE_LINE_NUMBER (NEXT_INSN (insn)) = NOTE_INSN_DELETED;
      NOTE_SOURCE_FILE (NEXT_INSN (insn)) = 0;
    }

  /* Output an insn to save %r1.  The runtime documentation doesn't
     specify whether the "Clean Up" slot in the callers frame can
     be clobbered by the callee.  It isn't copied by HP's builtin
     alloca, so this suggests that it can be clobbered if necessary.
     The "Static Link" location is copied by HP builtin alloca, so
     we avoid using it.  Using the cleanup slot might be a problem
     if we have to interoperate with languages that pass cleanup
     information.  However, it should be possible to handle these
     situations with GCC's asm feature.

     The "Current RP" slot is reserved for the called procedure, so
     we try to use it when we don't have a frame of our own.  It's
     rather unlikely that we won't have a frame when we need to emit
     a very long branch.

     Really the way to go long term is a register scavenger; goto
     the target of the jump and find a register which we can use
     as a scratch to hold the value in %r1.  Then, we wouldn't have
     to free up the delay slot or clobber a slot that may be needed
     for other purposes.  */
  if (TARGET_64BIT)
    {
      if (actual_fsize == 0 && !regs_ever_live[2])
	/* Use the return pointer slot in the frame marker.  */
	output_asm_insn ("std %%r1,-16(%%r30)", xoperands);
      else
	/* Use the slot at -40 in the frame marker since HP builtin
	   alloca doesn't copy it.  */
	output_asm_insn ("std %%r1,-40(%%r30)", xoperands);
    }
  else
    {
      if (actual_fsize == 0 && !regs_ever_live[2])
	/* Use the return pointer slot in the frame marker.  */
	output_asm_insn ("stw %%r1,-20(%%r30)", xoperands);
      else
	/* Use the "Clean Up" slot in the frame marker.  In GCC,
	   the only other use of this location is for copying a
	   floating point double argument from a floating-point
	   register to two general registers.  The copy is done
	   as an "atomic" operation when outputting a call, so it
	   won't interfere with our using the location here.  */
	output_asm_insn ("stw %%r1,-12(%%r30)", xoperands);
    }

  if (TARGET_PORTABLE_RUNTIME)
    {
      output_asm_insn ("ldil L'%0,%%r1", xoperands);
      output_asm_insn ("ldo R'%0(%%r1),%%r1", xoperands);
      output_asm_insn ("bv %%r0(%%r1)", xoperands);
    }
  else if (flag_pic)
    {
      output_asm_insn ("{bl|b,l} .+8,%%r1", xoperands);
      if (TARGET_SOM || !TARGET_GAS)
	{
	  xoperands[1] = gen_label_rtx ();
	  output_asm_insn ("addil L'%l0-%l1,%%r1", xoperands);
	  (*targetm.asm_out.internal_label) (asm_out_file, "L",
					     CODE_LABEL_NUMBER (xoperands[1]));
	  output_asm_insn ("ldo R'%l0-%l1(%%r1),%%r1", xoperands);
	}
      else
	{
	  output_asm_insn ("addil L'%l0-$PIC_pcrel$0+4,%%r1", xoperands);
	  output_asm_insn ("ldo R'%l0-$PIC_pcrel$0+8(%%r1),%%r1", xoperands);
	}
      output_asm_insn ("bv %%r0(%%r1)", xoperands);
    }
  else
    /* Now output a very long branch to the original target.  */
    output_asm_insn ("ldil L'%l0,%%r1\n\tbe R'%l0(%%sr4,%%r1)", xoperands);

  /* Now restore the value of %r1 in the delay slot.  */
  if (TARGET_64BIT)
    {
      if (actual_fsize == 0 && !regs_ever_live[2])
	return "ldd -16(%%r30),%%r1";
      else
	return "ldd -40(%%r30),%%r1";
    }
  else
    {
      if (actual_fsize == 0 && !regs_ever_live[2])
	return "ldw -20(%%r30),%%r1";
      else
	return "ldw -12(%%r30),%%r1";
    }
}

/* This routine handles all the branch-on-bit conditional branch sequences we
   might need to generate.  It handles nullification of delay slots,
   varying length branches, negated branches and all combinations of the
   above.  it returns the appropriate output template to emit the branch.  */

const char *
output_bb (rtx *operands ATTRIBUTE_UNUSED, int negated, rtx insn, int which)
{
  static char buf[100];
  int useskip = 0;
  int nullify = INSN_ANNULLED_BRANCH_P (insn);
  int length = get_attr_length (insn);
  int xdelay;

  /* A conditional branch to the following instruction (e.g. the delay slot) is
     asking for a disaster.  I do not think this can happen as this pattern
     is only used when optimizing; jump optimization should eliminate the
     jump.  But be prepared just in case.  */

  if (next_real_insn (JUMP_LABEL (insn)) == next_real_insn (insn))
    return "nop";

  /* If this is a long branch with its delay slot unfilled, set `nullify'
     as it can nullify the delay slot and save a nop.  */
  if (length == 8 && dbr_sequence_length () == 0)
    nullify = 1;

  /* If this is a short forward conditional branch which did not get
     its delay slot filled, the delay slot can still be nullified.  */
  if (! nullify && length == 4 && dbr_sequence_length () == 0)
    nullify = forward_branch_p (insn);

  /* A forward branch over a single nullified insn can be done with a
     extrs instruction.  This avoids a single cycle penalty due to
     mis-predicted branch if we fall through (branch not taken).  */

  if (length == 4
      && next_real_insn (insn) != 0
      && get_attr_length (next_real_insn (insn)) == 4
      && JUMP_LABEL (insn) == next_nonnote_insn (next_real_insn (insn))
      && nullify)
    useskip = 1;

  switch (length)
    {

      /* All short conditional branches except backwards with an unfilled
	 delay slot.  */
      case 4:
	if (useskip)
	  strcpy (buf, "{extrs,|extrw,s,}");
	else
	  strcpy (buf, "bb,");
	if (useskip && GET_MODE (operands[0]) == DImode)
	  strcpy (buf, "extrd,s,*");
	else if (GET_MODE (operands[0]) == DImode)
	  strcpy (buf, "bb,*");
	if ((which == 0 && negated)
	     || (which == 1 && ! negated))
	  strcat (buf, ">=");
	else
	  strcat (buf, "<");
	if (useskip)
	  strcat (buf, " %0,%1,1,%%r0");
	else if (nullify && negated)
	  strcat (buf, ",n %0,%1,%3");
	else if (nullify && ! negated)
	  strcat (buf, ",n %0,%1,%2");
	else if (! nullify && negated)
	  strcat (buf, "%0,%1,%3");
	else if (! nullify && ! negated)
	  strcat (buf, " %0,%1,%2");
	break;

     /* All long conditionals.  Note a short backward branch with an
	unfilled delay slot is treated just like a long backward branch
	with an unfilled delay slot.  */
      case 8:
	/* Handle weird backwards branch with a filled delay slot
	   which is nullified.  */
	if (dbr_sequence_length () != 0
	    && ! forward_branch_p (insn)
	    && nullify)
	  {
	    strcpy (buf, "bb,");
	    if (GET_MODE (operands[0]) == DImode)
	      strcat (buf, "*");
	    if ((which == 0 && negated)
		|| (which == 1 && ! negated))
	      strcat (buf, "<");
	    else
	      strcat (buf, ">=");
	    if (negated)
	      strcat (buf, ",n %0,%1,.+12\n\tb %3");
	    else
	      strcat (buf, ",n %0,%1,.+12\n\tb %2");
	  }
	/* Handle short backwards branch with an unfilled delay slot.
	   Using a bb;nop rather than extrs;bl saves 1 cycle for both
	   taken and untaken branches.  */
	else if (dbr_sequence_length () == 0
		 && ! forward_branch_p (insn)
		 && INSN_ADDRESSES_SET_P ()
		 && VAL_14_BITS_P (INSN_ADDRESSES (INSN_UID (JUMP_LABEL (insn)))
				    - INSN_ADDRESSES (INSN_UID (insn)) - 8))
	  {
	    strcpy (buf, "bb,");
	    if (GET_MODE (operands[0]) == DImode)
	      strcat (buf, "*");
	    if ((which == 0 && negated)
		|| (which == 1 && ! negated))
	      strcat (buf, ">=");
	    else
	      strcat (buf, "<");
	    if (negated)
	      strcat (buf, " %0,%1,%3%#");
	    else
	      strcat (buf, " %0,%1,%2%#");
	  }
	else
	  {
	    if (GET_MODE (operands[0]) == DImode)
	      strcpy (buf, "extrd,s,*");
	    else
	      strcpy (buf, "{extrs,|extrw,s,}");
	    if ((which == 0 && negated)
		|| (which == 1 && ! negated))
	      strcat (buf, "<");
	    else
	      strcat (buf, ">=");
	    if (nullify && negated)
	      strcat (buf, " %0,%1,1,%%r0\n\tb,n %3");
	    else if (nullify && ! negated)
	      strcat (buf, " %0,%1,1,%%r0\n\tb,n %2");
	    else if (negated)
	      strcat (buf, " %0,%1,1,%%r0\n\tb %3");
	    else
	      strcat (buf, " %0,%1,1,%%r0\n\tb %2");
	  }
	break;

      default:
	/* The reversed conditional branch must branch over one additional
	   instruction if the delay slot is filled and needs to be extracted
	   by output_lbranch.  If the delay slot is empty or this is a
	   nullified forward branch, the instruction after the reversed
	   condition branch must be nullified.  */
	if (dbr_sequence_length () == 0
	    || (nullify && forward_branch_p (insn)))
	  {
	    nullify = 1;
	    xdelay = 0;
	    operands[4] = GEN_INT (length);
	  }
	else
	  {
	    xdelay = 1;
	    operands[4] = GEN_INT (length + 4);
	  }

	if (GET_MODE (operands[0]) == DImode)
	  strcpy (buf, "bb,*");
	else
	  strcpy (buf, "bb,");
	if ((which == 0 && negated)
	    || (which == 1 && !negated))
	  strcat (buf, "<");
	else
	  strcat (buf, ">=");
	if (nullify)
	  strcat (buf, ",n %0,%1,.+%4");
	else
	  strcat (buf, " %0,%1,.+%4");
	output_asm_insn (buf, operands);
	return output_lbranch (negated ? operands[3] : operands[2],
			       insn, xdelay);
    }
  return buf;
}

/* This routine handles all the branch-on-variable-bit conditional branch
   sequences we might need to generate.  It handles nullification of delay
   slots, varying length branches, negated branches and all combinations
   of the above.  it returns the appropriate output template to emit the
   branch.  */

const char *
output_bvb (rtx *operands ATTRIBUTE_UNUSED, int negated, rtx insn, int which)
{
  static char buf[100];
  int useskip = 0;
  int nullify = INSN_ANNULLED_BRANCH_P (insn);
  int length = get_attr_length (insn);
  int xdelay;

  /* A conditional branch to the following instruction (e.g. the delay slot) is
     asking for a disaster.  I do not think this can happen as this pattern
     is only used when optimizing; jump optimization should eliminate the
     jump.  But be prepared just in case.  */

  if (next_real_insn (JUMP_LABEL (insn)) == next_real_insn (insn))
    return "nop";

  /* If this is a long branch with its delay slot unfilled, set `nullify'
     as it can nullify the delay slot and save a nop.  */
  if (length == 8 && dbr_sequence_length () == 0)
    nullify = 1;

  /* If this is a short forward conditional branch which did not get
     its delay slot filled, the delay slot can still be nullified.  */
  if (! nullify && length == 4 && dbr_sequence_length () == 0)
    nullify = forward_branch_p (insn);

  /* A forward branch over a single nullified insn can be done with a
     extrs instruction.  This avoids a single cycle penalty due to
     mis-predicted branch if we fall through (branch not taken).  */

  if (length == 4
      && next_real_insn (insn) != 0
      && get_attr_length (next_real_insn (insn)) == 4
      && JUMP_LABEL (insn) == next_nonnote_insn (next_real_insn (insn))
      && nullify)
    useskip = 1;

  switch (length)
    {

      /* All short conditional branches except backwards with an unfilled
	 delay slot.  */
      case 4:
	if (useskip)
	  strcpy (buf, "{vextrs,|extrw,s,}");
	else
	  strcpy (buf, "{bvb,|bb,}");
	if (useskip && GET_MODE (operands[0]) == DImode)
	  strcpy (buf, "extrd,s,*");
	else if (GET_MODE (operands[0]) == DImode)
	  strcpy (buf, "bb,*");
	if ((which == 0 && negated)
	     || (which == 1 && ! negated))
	  strcat (buf, ">=");
	else
	  strcat (buf, "<");
	if (useskip)
	  strcat (buf, "{ %0,1,%%r0| %0,%%sar,1,%%r0}");
	else if (nullify && negated)
	  strcat (buf, "{,n %0,%3|,n %0,%%sar,%3}");
	else if (nullify && ! negated)
	  strcat (buf, "{,n %0,%2|,n %0,%%sar,%2}");
	else if (! nullify && negated)
	  strcat (buf, "{%0,%3|%0,%%sar,%3}");
	else if (! nullify && ! negated)
	  strcat (buf, "{ %0,%2| %0,%%sar,%2}");
	break;

     /* All long conditionals.  Note a short backward branch with an
	unfilled delay slot is treated just like a long backward branch
	with an unfilled delay slot.  */
      case 8:
	/* Handle weird backwards branch with a filled delay slot
	   which is nullified.  */
	if (dbr_sequence_length () != 0
	    && ! forward_branch_p (insn)
	    && nullify)
	  {
	    strcpy (buf, "{bvb,|bb,}");
	    if (GET_MODE (operands[0]) == DImode)
	      strcat (buf, "*");
	    if ((which == 0 && negated)
		|| (which == 1 && ! negated))
	      strcat (buf, "<");
	    else
	      strcat (buf, ">=");
	    if (negated)
	      strcat (buf, "{,n %0,.+12\n\tb %3|,n %0,%%sar,.+12\n\tb %3}");
	    else
	      strcat (buf, "{,n %0,.+12\n\tb %2|,n %0,%%sar,.+12\n\tb %2}");
	  }
	/* Handle short backwards branch with an unfilled delay slot.
	   Using a bb;nop rather than extrs;bl saves 1 cycle for both
	   taken and untaken branches.  */
	else if (dbr_sequence_length () == 0
		 && ! forward_branch_p (insn)
		 && INSN_ADDRESSES_SET_P ()
		 && VAL_14_BITS_P (INSN_ADDRESSES (INSN_UID (JUMP_LABEL (insn)))
				    - INSN_ADDRESSES (INSN_UID (insn)) - 8))
	  {
	    strcpy (buf, "{bvb,|bb,}");
	    if (GET_MODE (operands[0]) == DImode)
	      strcat (buf, "*");
	    if ((which == 0 && negated)
		|| (which == 1 && ! negated))
	      strcat (buf, ">=");
	    else
	      strcat (buf, "<");
	    if (negated)
	      strcat (buf, "{ %0,%3%#| %0,%%sar,%3%#}");
	    else
	      strcat (buf, "{ %0,%2%#| %0,%%sar,%2%#}");
	  }
	else
	  {
	    strcpy (buf, "{vextrs,|extrw,s,}");
	    if (GET_MODE (operands[0]) == DImode)
	      strcpy (buf, "extrd,s,*");
	    if ((which == 0 && negated)
		|| (which == 1 && ! negated))
	      strcat (buf, "<");
	    else
	      strcat (buf, ">=");
	    if (nullify && negated)
	      strcat (buf, "{ %0,1,%%r0\n\tb,n %3| %0,%%sar,1,%%r0\n\tb,n %3}");
	    else if (nullify && ! negated)
	      strcat (buf, "{ %0,1,%%r0\n\tb,n %2| %0,%%sar,1,%%r0\n\tb,n %2}");
	    else if (negated)
	      strcat (buf, "{ %0,1,%%r0\n\tb %3| %0,%%sar,1,%%r0\n\tb %3}");
	    else
	      strcat (buf, "{ %0,1,%%r0\n\tb %2| %0,%%sar,1,%%r0\n\tb %2}");
	  }
	break;

      default:
	/* The reversed conditional branch must branch over one additional
	   instruction if the delay slot is filled and needs to be extracted
	   by output_lbranch.  If the delay slot is empty or this is a
	   nullified forward branch, the instruction after the reversed
	   condition branch must be nullified.  */
	if (dbr_sequence_length () == 0
	    || (nullify && forward_branch_p (insn)))
	  {
	    nullify = 1;
	    xdelay = 0;
	    operands[4] = GEN_INT (length);
	  }
	else
	  {
	    xdelay = 1;
	    operands[4] = GEN_INT (length + 4);
	  }

	if (GET_MODE (operands[0]) == DImode)
	  strcpy (buf, "bb,*");
	else
	  strcpy (buf, "{bvb,|bb,}");
	if ((which == 0 && negated)
	    || (which == 1 && !negated))
	  strcat (buf, "<");
	else
	  strcat (buf, ">=");
	if (nullify)
	  strcat (buf, ",n {%0,.+%4|%0,%%sar,.+%4}");
	else
	  strcat (buf, " {%0,.+%4|%0,%%sar,.+%4}");
	output_asm_insn (buf, operands);
	return output_lbranch (negated ? operands[3] : operands[2],
			       insn, xdelay);
    }
  return buf;
}

/* Return the output template for emitting a dbra type insn.

   Note it may perform some output operations on its own before
   returning the final output string.  */
const char *
output_dbra (rtx *operands, rtx insn, int which_alternative)
{
  int length = get_attr_length (insn);

  /* A conditional branch to the following instruction (e.g. the delay slot) is
     asking for a disaster.  Be prepared!  */

  if (next_real_insn (JUMP_LABEL (insn)) == next_real_insn (insn))
    {
      if (which_alternative == 0)
	return "ldo %1(%0),%0";
      else if (which_alternative == 1)
	{
	  output_asm_insn ("{fstws|fstw} %0,-16(%%r30)", operands);
	  output_asm_insn ("ldw -16(%%r30),%4", operands);
	  output_asm_insn ("ldo %1(%4),%4\n\tstw %4,-16(%%r30)", operands);
	  return "{fldws|fldw} -16(%%r30),%0";
	}
      else
	{
	  output_asm_insn ("ldw %0,%4", operands);
	  return "ldo %1(%4),%4\n\tstw %4,%0";
	}
    }

  if (which_alternative == 0)
    {
      int nullify = INSN_ANNULLED_BRANCH_P (insn);
      int xdelay;

      /* If this is a long branch with its delay slot unfilled, set `nullify'
	 as it can nullify the delay slot and save a nop.  */
      if (length == 8 && dbr_sequence_length () == 0)
	nullify = 1;

      /* If this is a short forward conditional branch which did not get
	 its delay slot filled, the delay slot can still be nullified.  */
      if (! nullify && length == 4 && dbr_sequence_length () == 0)
	nullify = forward_branch_p (insn);

      switch (length)
	{
	case 4:
	  if (nullify)
	    return "addib,%C2,n %1,%0,%3";
	  else
	    return "addib,%C2 %1,%0,%3";
      
	case 8:
	  /* Handle weird backwards branch with a fulled delay slot
	     which is nullified.  */
	  if (dbr_sequence_length () != 0
	      && ! forward_branch_p (insn)
	      && nullify)
	    return "addib,%N2,n %1,%0,.+12\n\tb %3";
	  /* Handle short backwards branch with an unfilled delay slot.
	     Using a addb;nop rather than addi;bl saves 1 cycle for both
	     taken and untaken branches.  */
	  else if (dbr_sequence_length () == 0
		   && ! forward_branch_p (insn)
		   && INSN_ADDRESSES_SET_P ()
		   && VAL_14_BITS_P (INSN_ADDRESSES (INSN_UID (JUMP_LABEL (insn)))
				      - INSN_ADDRESSES (INSN_UID (insn)) - 8))
	      return "addib,%C2 %1,%0,%3%#";

	  /* Handle normal cases.  */
	  if (nullify)
	    return "addi,%N2 %1,%0,%0\n\tb,n %3";
	  else
	    return "addi,%N2 %1,%0,%0\n\tb %3";

	default:
	  /* The reversed conditional branch must branch over one additional
	     instruction if the delay slot is filled and needs to be extracted
	     by output_lbranch.  If the delay slot is empty or this is a
	     nullified forward branch, the instruction after the reversed
	     condition branch must be nullified.  */
	  if (dbr_sequence_length () == 0
	      || (nullify && forward_branch_p (insn)))
	    {
	      nullify = 1;
	      xdelay = 0;
	      operands[4] = GEN_INT (length);
	    }
	  else
	    {
	      xdelay = 1;
	      operands[4] = GEN_INT (length + 4);
	    }

	  if (nullify)
	    output_asm_insn ("addib,%N2,n %1,%0,.+%4", operands);
	  else
	    output_asm_insn ("addib,%N2 %1,%0,.+%4", operands);

	  return output_lbranch (operands[3], insn, xdelay);
	}
      
    }
  /* Deal with gross reload from FP register case.  */
  else if (which_alternative == 1)
    {
      /* Move loop counter from FP register to MEM then into a GR,
	 increment the GR, store the GR into MEM, and finally reload
	 the FP register from MEM from within the branch's delay slot.  */
      output_asm_insn ("{fstws|fstw} %0,-16(%%r30)\n\tldw -16(%%r30),%4",
		       operands);
      output_asm_insn ("ldo %1(%4),%4\n\tstw %4,-16(%%r30)", operands);
      if (length == 24)
	return "{comb|cmpb},%S2 %%r0,%4,%3\n\t{fldws|fldw} -16(%%r30),%0";
      else if (length == 28)
	return "{comclr|cmpclr},%B2 %%r0,%4,%%r0\n\tb %3\n\t{fldws|fldw} -16(%%r30),%0";
      else
	{
	  operands[5] = GEN_INT (length - 16);
	  output_asm_insn ("{comb|cmpb},%B2 %%r0,%4,.+%5", operands);
	  output_asm_insn ("{fldws|fldw} -16(%%r30),%0", operands);
	  return output_lbranch (operands[3], insn, 0);
	}
    }
  /* Deal with gross reload from memory case.  */
  else
    {
      /* Reload loop counter from memory, the store back to memory
	 happens in the branch's delay slot.  */
      output_asm_insn ("ldw %0,%4", operands);
      if (length == 12)
	return "addib,%C2 %1,%4,%3\n\tstw %4,%0";
      else if (length == 16)
	return "addi,%N2 %1,%4,%4\n\tb %3\n\tstw %4,%0";
      else
	{
	  operands[5] = GEN_INT (length - 4);
	  output_asm_insn ("addib,%N2 %1,%4,.+%5\n\tstw %4,%0", operands);
	  return output_lbranch (operands[3], insn, 0);
	}
    }
}

/* Return the output template for emitting a movb type insn.

   Note it may perform some output operations on its own before
   returning the final output string.  */
const char *
output_movb (rtx *operands, rtx insn, int which_alternative,
	     int reverse_comparison)
{
  int length = get_attr_length (insn);

  /* A conditional branch to the following instruction (e.g. the delay slot) is
     asking for a disaster.  Be prepared!  */

  if (next_real_insn (JUMP_LABEL (insn)) == next_real_insn (insn))
    {
      if (which_alternative == 0)
	return "copy %1,%0";
      else if (which_alternative == 1)
	{
	  output_asm_insn ("stw %1,-16(%%r30)", operands);
	  return "{fldws|fldw} -16(%%r30),%0";
	}
      else if (which_alternative == 2)
	return "stw %1,%0";
      else
	return "mtsar %r1";
    }

  /* Support the second variant.  */
  if (reverse_comparison)
    PUT_CODE (operands[2], reverse_condition (GET_CODE (operands[2])));

  if (which_alternative == 0)
    {
      int nullify = INSN_ANNULLED_BRANCH_P (insn);
      int xdelay;

      /* If this is a long branch with its delay slot unfilled, set `nullify'
	 as it can nullify the delay slot and save a nop.  */
      if (length == 8 && dbr_sequence_length () == 0)
	nullify = 1;

      /* If this is a short forward conditional branch which did not get
	 its delay slot filled, the delay slot can still be nullified.  */
      if (! nullify && length == 4 && dbr_sequence_length () == 0)
	nullify = forward_branch_p (insn);

      switch (length)
	{
	case 4:
	  if (nullify)
	    return "movb,%C2,n %1,%0,%3";
	  else
	    return "movb,%C2 %1,%0,%3";

	case 8:
	  /* Handle weird backwards branch with a filled delay slot
	     which is nullified.  */
	  if (dbr_sequence_length () != 0
	      && ! forward_branch_p (insn)
	      && nullify)
	    return "movb,%N2,n %1,%0,.+12\n\tb %3";

	  /* Handle short backwards branch with an unfilled delay slot.
	     Using a movb;nop rather than or;bl saves 1 cycle for both
	     taken and untaken branches.  */
	  else if (dbr_sequence_length () == 0
		   && ! forward_branch_p (insn)
		   && INSN_ADDRESSES_SET_P ()
		   && VAL_14_BITS_P (INSN_ADDRESSES (INSN_UID (JUMP_LABEL (insn)))
				      - INSN_ADDRESSES (INSN_UID (insn)) - 8))
	    return "movb,%C2 %1,%0,%3%#";
	  /* Handle normal cases.  */
	  if (nullify)
	    return "or,%N2 %1,%%r0,%0\n\tb,n %3";
	  else
	    return "or,%N2 %1,%%r0,%0\n\tb %3";

	default:
	  /* The reversed conditional branch must branch over one additional
	     instruction if the delay slot is filled and needs to be extracted
	     by output_lbranch.  If the delay slot is empty or this is a
	     nullified forward branch, the instruction after the reversed
	     condition branch must be nullified.  */
	  if (dbr_sequence_length () == 0
	      || (nullify && forward_branch_p (insn)))
	    {
	      nullify = 1;
	      xdelay = 0;
	      operands[4] = GEN_INT (length);
	    }
	  else
	    {
	      xdelay = 1;
	      operands[4] = GEN_INT (length + 4);
	    }

	  if (nullify)
	    output_asm_insn ("movb,%N2,n %1,%0,.+%4", operands);
	  else
	    output_asm_insn ("movb,%N2 %1,%0,.+%4", operands);

	  return output_lbranch (operands[3], insn, xdelay);
	}
    }
  /* Deal with gross reload for FP destination register case.  */
  else if (which_alternative == 1)
    {
      /* Move source register to MEM, perform the branch test, then
	 finally load the FP register from MEM from within the branch's
	 delay slot.  */
      output_asm_insn ("stw %1,-16(%%r30)", operands);
      if (length == 12)
	return "{comb|cmpb},%S2 %%r0,%1,%3\n\t{fldws|fldw} -16(%%r30),%0";
      else if (length == 16)
	return "{comclr|cmpclr},%B2 %%r0,%1,%%r0\n\tb %3\n\t{fldws|fldw} -16(%%r30),%0";
      else
	{
	  operands[4] = GEN_INT (length - 4);
	  output_asm_insn ("{comb|cmpb},%B2 %%r0,%1,.+%4", operands);
	  output_asm_insn ("{fldws|fldw} -16(%%r30),%0", operands);
	  return output_lbranch (operands[3], insn, 0);
	}
    }
  /* Deal with gross reload from memory case.  */
  else if (which_alternative == 2)
    {
      /* Reload loop counter from memory, the store back to memory
	 happens in the branch's delay slot.  */
      if (length == 8)
	return "{comb|cmpb},%S2 %%r0,%1,%3\n\tstw %1,%0";
      else if (length == 12)
	return "{comclr|cmpclr},%B2 %%r0,%1,%%r0\n\tb %3\n\tstw %1,%0";
      else
	{
	  operands[4] = GEN_INT (length);
	  output_asm_insn ("{comb|cmpb},%B2 %%r0,%1,.+%4\n\tstw %1,%0",
			   operands);
	  return output_lbranch (operands[3], insn, 0);
	}
    }
  /* Handle SAR as a destination.  */
  else
    {
      if (length == 8)
	return "{comb|cmpb},%S2 %%r0,%1,%3\n\tmtsar %r1";
      else if (length == 12)
	return "{comclr|cmpclr},%B2 %%r0,%1,%%r0\n\tb %3\n\tmtsar %r1";
      else
	{
	  operands[4] = GEN_INT (length);
	  output_asm_insn ("{comb|cmpb},%B2 %%r0,%1,.+%4\n\tmtsar %r1",
			   operands);
	  return output_lbranch (operands[3], insn, 0);
	}
    }
}

/* Copy any FP arguments in INSN into integer registers.  */
static void
copy_fp_args (rtx insn)
{
  rtx link;
  rtx xoperands[2];

  for (link = CALL_INSN_FUNCTION_USAGE (insn); link; link = XEXP (link, 1))
    {
      int arg_mode, regno;
      rtx use = XEXP (link, 0);

      if (! (GET_CODE (use) == USE
	  && GET_CODE (XEXP (use, 0)) == REG
	  && FUNCTION_ARG_REGNO_P (REGNO (XEXP (use, 0)))))
	continue;

      arg_mode = GET_MODE (XEXP (use, 0));
      regno = REGNO (XEXP (use, 0));

      /* Is it a floating point register?  */
      if (regno >= 32 && regno <= 39)
	{
	  /* Copy the FP register into an integer register via memory.  */
	  if (arg_mode == SFmode)
	    {
	      xoperands[0] = XEXP (use, 0);
	      xoperands[1] = gen_rtx_REG (SImode, 26 - (regno - 32) / 2);
	      output_asm_insn ("{fstws|fstw} %0,-16(%%sr0,%%r30)", xoperands);
	      output_asm_insn ("ldw -16(%%sr0,%%r30),%1", xoperands);
	    }
	  else
	    {
	      xoperands[0] = XEXP (use, 0);
	      xoperands[1] = gen_rtx_REG (DImode, 25 - (regno - 34) / 2);
	      output_asm_insn ("{fstds|fstd} %0,-16(%%sr0,%%r30)", xoperands);
	      output_asm_insn ("ldw -12(%%sr0,%%r30),%R1", xoperands);
	      output_asm_insn ("ldw -16(%%sr0,%%r30),%1", xoperands);
	    }
	}
    }
}

/* Compute length of the FP argument copy sequence for INSN.  */
static int
length_fp_args (rtx insn)
{
  int length = 0;
  rtx link;

  for (link = CALL_INSN_FUNCTION_USAGE (insn); link; link = XEXP (link, 1))
    {
      int arg_mode, regno;
      rtx use = XEXP (link, 0);

      if (! (GET_CODE (use) == USE
	  && GET_CODE (XEXP (use, 0)) == REG
	  && FUNCTION_ARG_REGNO_P (REGNO (XEXP (use, 0)))))
	continue;

      arg_mode = GET_MODE (XEXP (use, 0));
      regno = REGNO (XEXP (use, 0));

      /* Is it a floating point register?  */
      if (regno >= 32 && regno <= 39)
	{
	  if (arg_mode == SFmode)
	    length += 8;
	  else
	    length += 12;
	}
    }

  return length;
}

/* Return the attribute length for the millicode call instruction INSN.
   The length must match the code generated by output_millicode_call.
   We include the delay slot in the returned length as it is better to
   over estimate the length than to under estimate it.  */

int
attr_length_millicode_call (rtx insn)
{
  unsigned long distance = -1;
  unsigned long total = IN_NAMED_SECTION_P (cfun->decl) ? 0 : total_code_bytes;

  if (INSN_ADDRESSES_SET_P ())
    {
      distance = (total + insn_current_reference_address (insn));
      if (distance < total)
	distance = -1;
    }

  if (TARGET_64BIT)
    {
      if (!TARGET_LONG_CALLS && distance < 7600000)
	return 8;

      return 20;
    }
  else if (TARGET_PORTABLE_RUNTIME)
    return 24;
  else
    {
      if (!TARGET_LONG_CALLS && distance < 240000)
	return 8;

      if (TARGET_LONG_ABS_CALL && !flag_pic)
	return 12;

      return 24;
    }
}

/* INSN is a function call.  It may have an unconditional jump
   in its delay slot.

   CALL_DEST is the routine we are calling.  */

const char *
output_millicode_call (rtx insn, rtx call_dest)
{
  int attr_length = get_attr_length (insn);
  int seq_length = dbr_sequence_length ();
  int distance;
  rtx seq_insn;
  rtx xoperands[3];

  xoperands[0] = call_dest;
  xoperands[2] = gen_rtx_REG (Pmode, TARGET_64BIT ? 2 : 31);

  /* Handle the common case where we are sure that the branch will
     reach the beginning of the $CODE$ subspace.  The within reach
     form of the $$sh_func_adrs call has a length of 28.  Because
     it has an attribute type of multi, it never has a nonzero
     sequence length.  The length of the $$sh_func_adrs is the same
     as certain out of reach PIC calls to other routines.  */
  if (!TARGET_LONG_CALLS
      && ((seq_length == 0
	   && (attr_length == 12
	       || (attr_length == 28 && get_attr_type (insn) == TYPE_MULTI)))
	  || (seq_length != 0 && attr_length == 8)))
    {
      output_asm_insn ("{bl|b,l} %0,%2", xoperands);
    }
  else
    {
      if (TARGET_64BIT)
	{
	  /* It might seem that one insn could be saved by accessing
	     the millicode function using the linkage table.  However,
	     this doesn't work in shared libraries and other dynamically
	     loaded objects.  Using a pc-relative sequence also avoids
	     problems related to the implicit use of the gp register.  */
	  output_asm_insn ("b,l .+8,%%r1", xoperands);

	  if (TARGET_GAS)
	    {
	      output_asm_insn ("addil L'%0-$PIC_pcrel$0+4,%%r1", xoperands);
	      output_asm_insn ("ldo R'%0-$PIC_pcrel$0+8(%%r1),%%r1", xoperands);
	    }
	  else
	    {
	      xoperands[1] = gen_label_rtx ();
	      output_asm_insn ("addil L'%0-%l1,%%r1", xoperands);
	      (*targetm.asm_out.internal_label) (asm_out_file, "L",
					 CODE_LABEL_NUMBER (xoperands[1]));
	      output_asm_insn ("ldo R'%0-%l1(%%r1),%%r1", xoperands);
	    }

	  output_asm_insn ("bve,l (%%r1),%%r2", xoperands);
	}
      else if (TARGET_PORTABLE_RUNTIME)
	{
	  /* Pure portable runtime doesn't allow be/ble; we also don't
	     have PIC support in the assembler/linker, so this sequence
	     is needed.  */

	  /* Get the address of our target into %r1.  */
	  output_asm_insn ("ldil L'%0,%%r1", xoperands);
	  output_asm_insn ("ldo R'%0(%%r1),%%r1", xoperands);

	  /* Get our return address into %r31.  */
	  output_asm_insn ("{bl|b,l} .+8,%%r31", xoperands);
	  output_asm_insn ("addi 8,%%r31,%%r31", xoperands);

	  /* Jump to our target address in %r1.  */
	  output_asm_insn ("bv %%r0(%%r1)", xoperands);
	}
      else if (!flag_pic)
	{
	  output_asm_insn ("ldil L'%0,%%r1", xoperands);
	  if (TARGET_PA_20)
	    output_asm_insn ("be,l R'%0(%%sr4,%%r1),%%sr0,%%r31", xoperands);
	  else
	    output_asm_insn ("ble R'%0(%%sr4,%%r1)", xoperands);
	}
      else
	{
	  output_asm_insn ("{bl|b,l} .+8,%%r1", xoperands);
	  output_asm_insn ("addi 16,%%r1,%%r31", xoperands);

	  if (TARGET_SOM || !TARGET_GAS)
	    {
	      /* The HP assembler can generate relocations for the
		 difference of two symbols.  GAS can do this for a
		 millicode symbol but not an arbitrary external
		 symbol when generating SOM output.  */
	      xoperands[1] = gen_label_rtx ();
	      (*targetm.asm_out.internal_label) (asm_out_file, "L",
					 CODE_LABEL_NUMBER (xoperands[1]));
	      output_asm_insn ("addil L'%0-%l1,%%r1", xoperands);
	      output_asm_insn ("ldo R'%0-%l1(%%r1),%%r1", xoperands);
	    }
	  else
	    {
	      output_asm_insn ("addil L'%0-$PIC_pcrel$0+8,%%r1", xoperands);
	      output_asm_insn ("ldo R'%0-$PIC_pcrel$0+12(%%r1),%%r1",
			       xoperands);
	    }

	  /* Jump to our target address in %r1.  */
	  output_asm_insn ("bv %%r0(%%r1)", xoperands);
	}
    }

  if (seq_length == 0)
    output_asm_insn ("nop", xoperands);

  /* We are done if there isn't a jump in the delay slot.  */
  if (seq_length == 0 || GET_CODE (NEXT_INSN (insn)) != JUMP_INSN)
    return "";

  /* This call has an unconditional jump in its delay slot.  */
  xoperands[0] = XEXP (PATTERN (NEXT_INSN (insn)), 1);

  /* See if the return address can be adjusted.  Use the containing
     sequence insn's address.  */
  if (INSN_ADDRESSES_SET_P ())
    {
      seq_insn = NEXT_INSN (PREV_INSN (XVECEXP (final_sequence, 0, 0)));
      distance = (INSN_ADDRESSES (INSN_UID (JUMP_LABEL (NEXT_INSN (insn))))
		  - INSN_ADDRESSES (INSN_UID (seq_insn)) - 8);

      if (VAL_14_BITS_P (distance))
	{
	  xoperands[1] = gen_label_rtx ();
	  output_asm_insn ("ldo %0-%1(%2),%2", xoperands);
	  (*targetm.asm_out.internal_label) (asm_out_file, "L",
					     CODE_LABEL_NUMBER (xoperands[1]));
	}
      else
	/* ??? This branch may not reach its target.  */
	output_asm_insn ("nop\n\tb,n %0", xoperands);
    }
  else
    /* ??? This branch may not reach its target.  */
    output_asm_insn ("nop\n\tb,n %0", xoperands);

  /* Delete the jump.  */
  PUT_CODE (NEXT_INSN (insn), NOTE);
  NOTE_LINE_NUMBER (NEXT_INSN (insn)) = NOTE_INSN_DELETED;
  NOTE_SOURCE_FILE (NEXT_INSN (insn)) = 0;

  return "";
}

/* Return the attribute length of the call instruction INSN.  The SIBCALL
   flag indicates whether INSN is a regular call or a sibling call.  The
   length returned must be longer than the code actually generated by
   output_call.  Since branch shortening is done before delay branch
   sequencing, there is no way to determine whether or not the delay
   slot will be filled during branch shortening.  Even when the delay
   slot is filled, we may have to add a nop if the delay slot contains
   a branch that can't reach its target.  Thus, we always have to include
   the delay slot in the length estimate.  This used to be done in
   pa_adjust_insn_length but we do it here now as some sequences always
   fill the delay slot and we can save four bytes in the estimate for
   these sequences.  */

int
attr_length_call (rtx insn, int sibcall)
{
  int local_call;
  rtx call_dest;
  tree call_decl;
  int length = 0;
  rtx pat = PATTERN (insn);
  unsigned long distance = -1;

  if (INSN_ADDRESSES_SET_P ())
    {
      unsigned long total;

      total = IN_NAMED_SECTION_P (cfun->decl) ? 0 : total_code_bytes;
      distance = (total + insn_current_reference_address (insn));
      if (distance < total)
	distance = -1;
    }

  /* Determine if this is a local call.  */
  if (GET_CODE (XVECEXP (pat, 0, 0)) == CALL)
    call_dest = XEXP (XEXP (XVECEXP (pat, 0, 0), 0), 0);
  else
    call_dest = XEXP (XEXP (XEXP (XVECEXP (pat, 0, 0), 1), 0), 0);

  call_decl = SYMBOL_REF_DECL (call_dest);
  local_call = call_decl && (*targetm.binds_local_p) (call_decl);

  /* pc-relative branch.  */
  if (!TARGET_LONG_CALLS
      && ((TARGET_PA_20 && !sibcall && distance < 7600000)
	  || distance < 240000))
    length += 8;

  /* 64-bit plabel sequence.  */
  else if (TARGET_64BIT && !local_call)
    length += sibcall ? 28 : 24;

  /* non-pic long absolute branch sequence.  */
  else if ((TARGET_LONG_ABS_CALL || local_call) && !flag_pic)
    length += 12;

  /* long pc-relative branch sequence.  */
  else if ((TARGET_SOM && TARGET_LONG_PIC_SDIFF_CALL)
	   || (TARGET_64BIT && !TARGET_GAS)
	   || (TARGET_GAS && !TARGET_SOM
	       && (TARGET_LONG_PIC_PCREL_CALL || local_call)))
    {
      length += 20;

      if (!TARGET_PA_20 && !TARGET_NO_SPACE_REGS)
	length += 8;
    }

  /* 32-bit plabel sequence.  */
  else
    {
      length += 32;

      if (TARGET_SOM)
	length += length_fp_args (insn);

      if (flag_pic)
	length += 4;

      if (!TARGET_PA_20)
	{
	  if (!sibcall)
	    length += 8;

	  if (!TARGET_NO_SPACE_REGS)
	    length += 8;
	}
    }

  return length;
}

/* INSN is a function call.  It may have an unconditional jump
   in its delay slot.

   CALL_DEST is the routine we are calling.  */

const char *
output_call (rtx insn, rtx call_dest, int sibcall)
{
  int delay_insn_deleted = 0;
  int delay_slot_filled = 0;
  int seq_length = dbr_sequence_length ();
  tree call_decl = SYMBOL_REF_DECL (call_dest);
  int local_call = call_decl && (*targetm.binds_local_p) (call_decl);
  rtx xoperands[2];

  xoperands[0] = call_dest;

  /* Handle the common case where we're sure that the branch will reach
     the beginning of the "$CODE$" subspace.  This is the beginning of
     the current function if we are in a named section.  */
  if (!TARGET_LONG_CALLS && attr_length_call (insn, sibcall) == 8)
    {
      xoperands[1] = gen_rtx_REG (word_mode, sibcall ? 0 : 2);
      output_asm_insn ("{bl|b,l} %0,%1", xoperands);
    }
  else
    {
      if (TARGET_64BIT && !local_call)
	{
	  /* ??? As far as I can tell, the HP linker doesn't support the
	     long pc-relative sequence described in the 64-bit runtime
	     architecture.  So, we use a slightly longer indirect call.  */
	  xoperands[0] = get_deferred_plabel (call_dest);
	  xoperands[1] = gen_label_rtx ();

	  /* If this isn't a sibcall, we put the load of %r27 into the
	     delay slot.  We can't do this in a sibcall as we don't
	     have a second call-clobbered scratch register available.  */
	  if (seq_length != 0
	      && GET_CODE (NEXT_INSN (insn)) != JUMP_INSN
	      && !sibcall)
	    {
	      final_scan_insn (NEXT_INSN (insn), asm_out_file,
			       optimize, 0, NULL);

	      /* Now delete the delay insn.  */
	      PUT_CODE (NEXT_INSN (insn), NOTE);
	      NOTE_LINE_NUMBER (NEXT_INSN (insn)) = NOTE_INSN_DELETED;
	      NOTE_SOURCE_FILE (NEXT_INSN (insn)) = 0;
	      delay_insn_deleted = 1;
	    }

	  output_asm_insn ("addil LT'%0,%%r27", xoperands);
	  output_asm_insn ("ldd RT'%0(%%r1),%%r1", xoperands);
	  output_asm_insn ("ldd 0(%%r1),%%r1", xoperands);

	  if (sibcall)
	    {
	      output_asm_insn ("ldd 24(%%r1),%%r27", xoperands);
	      output_asm_insn ("ldd 16(%%r1),%%r1", xoperands);
	      output_asm_insn ("bve (%%r1)", xoperands);
	    }
	  else
	    {
	      output_asm_insn ("ldd 16(%%r1),%%r2", xoperands);
	      output_asm_insn ("bve,l (%%r2),%%r2", xoperands);
	      output_asm_insn ("ldd 24(%%r1),%%r27", xoperands);
	      delay_slot_filled = 1;
	    }
	}
      else
	{
	  int indirect_call = 0;

	  /* Emit a long call.  There are several different sequences
	     of increasing length and complexity.  In most cases,
             they don't allow an instruction in the delay slot.  */
	  if (!((TARGET_LONG_ABS_CALL || local_call) && !flag_pic)
	      && !(TARGET_SOM && TARGET_LONG_PIC_SDIFF_CALL)
	      && !(TARGET_GAS && !TARGET_SOM
		   && (TARGET_LONG_PIC_PCREL_CALL || local_call))
	      && !TARGET_64BIT)
	    indirect_call = 1;

	  if (seq_length != 0
	      && GET_CODE (NEXT_INSN (insn)) != JUMP_INSN
	      && !sibcall
	      && (!TARGET_PA_20 || indirect_call))
	    {
	      /* A non-jump insn in the delay slot.  By definition we can
		 emit this insn before the call (and in fact before argument
		 relocating.  */
	      final_scan_insn (NEXT_INSN (insn), asm_out_file, optimize, 0,
			       NULL);

	      /* Now delete the delay insn.  */
	      PUT_CODE (NEXT_INSN (insn), NOTE);
	      NOTE_LINE_NUMBER (NEXT_INSN (insn)) = NOTE_INSN_DELETED;
	      NOTE_SOURCE_FILE (NEXT_INSN (insn)) = 0;
	      delay_insn_deleted = 1;
	    }

	  if ((TARGET_LONG_ABS_CALL || local_call) && !flag_pic)
	    {
	      /* This is the best sequence for making long calls in
		 non-pic code.  Unfortunately, GNU ld doesn't provide
		 the stub needed for external calls, and GAS's support
		 for this with the SOM linker is buggy.  It is safe
		 to use this for local calls.  */
	      output_asm_insn ("ldil L'%0,%%r1", xoperands);
	      if (sibcall)
		output_asm_insn ("be R'%0(%%sr4,%%r1)", xoperands);
	      else
		{
		  if (TARGET_PA_20)
		    output_asm_insn ("be,l R'%0(%%sr4,%%r1),%%sr0,%%r31",
				     xoperands);
		  else
		    output_asm_insn ("ble R'%0(%%sr4,%%r1)", xoperands);

		  output_asm_insn ("copy %%r31,%%r2", xoperands);
		  delay_slot_filled = 1;
		}
	    }
	  else
	    {
	      if ((TARGET_SOM && TARGET_LONG_PIC_SDIFF_CALL)
		  || (TARGET_64BIT && !TARGET_GAS))
		{
		  /* The HP assembler and linker can handle relocations
		     for the difference of two symbols.  GAS and the HP
		     linker can't do this when one of the symbols is
		     external.  */
		  xoperands[1] = gen_label_rtx ();
		  output_asm_insn ("{bl|b,l} .+8,%%r1", xoperands);
		  output_asm_insn ("addil L'%0-%l1,%%r1", xoperands);
		  (*targetm.asm_out.internal_label) (asm_out_file, "L",
					     CODE_LABEL_NUMBER (xoperands[1]));
		  output_asm_insn ("ldo R'%0-%l1(%%r1),%%r1", xoperands);
		}
	      else if (TARGET_GAS && !TARGET_SOM
		       && (TARGET_LONG_PIC_PCREL_CALL || local_call))
		{
		  /*  GAS currently can't generate the relocations that
		      are needed for the SOM linker under HP-UX using this
		      sequence.  The GNU linker doesn't generate the stubs
		      that are needed for external calls on TARGET_ELF32
		      with this sequence.  For now, we have to use a
		      longer plabel sequence when using GAS.  */
		  output_asm_insn ("{bl|b,l} .+8,%%r1", xoperands);
		  output_asm_insn ("addil L'%0-$PIC_pcrel$0+4,%%r1",
				   xoperands);
		  output_asm_insn ("ldo R'%0-$PIC_pcrel$0+8(%%r1),%%r1",
				   xoperands);
		}
	      else
		{
		  /* Emit a long plabel-based call sequence.  This is
		     essentially an inline implementation of $$dyncall.
		     We don't actually try to call $$dyncall as this is
		     as difficult as calling the function itself.  */
		  xoperands[0] = get_deferred_plabel (call_dest);
		  xoperands[1] = gen_label_rtx ();

		  /* Since the call is indirect, FP arguments in registers
		     need to be copied to the general registers.  Then, the
		     argument relocation stub will copy them back.  */
		  if (TARGET_SOM)
		    copy_fp_args (insn);

		  if (flag_pic)
		    {
		      output_asm_insn ("addil LT'%0,%%r19", xoperands);
		      output_asm_insn ("ldw RT'%0(%%r1),%%r1", xoperands);
		      output_asm_insn ("ldw 0(%%r1),%%r1", xoperands);
		    }
		  else
		    {
		      output_asm_insn ("addil LR'%0-$global$,%%r27",
				       xoperands);
		      output_asm_insn ("ldw RR'%0-$global$(%%r1),%%r1",
				       xoperands);
		    }

		  output_asm_insn ("bb,>=,n %%r1,30,.+16", xoperands);
		  output_asm_insn ("depi 0,31,2,%%r1", xoperands);
		  output_asm_insn ("ldw 4(%%sr0,%%r1),%%r19", xoperands);
		  output_asm_insn ("ldw 0(%%sr0,%%r1),%%r1", xoperands);

		  if (!sibcall && !TARGET_PA_20)
		    {
		      output_asm_insn ("{bl|b,l} .+8,%%r2", xoperands);
		      if (TARGET_NO_SPACE_REGS)
			output_asm_insn ("addi 8,%%r2,%%r2", xoperands);
		      else
			output_asm_insn ("addi 16,%%r2,%%r2", xoperands);
		    }
		}

	      if (TARGET_PA_20)
		{
		  if (sibcall)
		    output_asm_insn ("bve (%%r1)", xoperands);
		  else
		    {
		      if (indirect_call)
			{
			  output_asm_insn ("bve,l (%%r1),%%r2", xoperands);
			  output_asm_insn ("stw %%r2,-24(%%sp)", xoperands);
			  delay_slot_filled = 1;
			}
		      else
			output_asm_insn ("bve,l (%%r1),%%r2", xoperands);
		    }
		}
	      else
		{
		  if (!TARGET_NO_SPACE_REGS)
		    output_asm_insn ("ldsid (%%r1),%%r31\n\tmtsp %%r31,%%sr0",
				     xoperands);

		  if (sibcall)
		    {
		      if (TARGET_NO_SPACE_REGS)
			output_asm_insn ("be 0(%%sr4,%%r1)", xoperands);
		      else
			output_asm_insn ("be 0(%%sr0,%%r1)", xoperands);
		    }
		  else
		    {
		      if (TARGET_NO_SPACE_REGS)
			output_asm_insn ("ble 0(%%sr4,%%r1)", xoperands);
		      else
			output_asm_insn ("ble 0(%%sr0,%%r1)", xoperands);

		      if (indirect_call)
			output_asm_insn ("stw %%r31,-24(%%sp)", xoperands);
		      else
			output_asm_insn ("copy %%r31,%%r2", xoperands);
		      delay_slot_filled = 1;
		    }
		}
	    }
	}
    }

  if (!delay_slot_filled && (seq_length == 0 || delay_insn_deleted))
    output_asm_insn ("nop", xoperands);

  /* We are done if there isn't a jump in the delay slot.  */
  if (seq_length == 0
      || delay_insn_deleted
      || GET_CODE (NEXT_INSN (insn)) != JUMP_INSN)
    return "";

  /* A sibcall should never have a branch in the delay slot.  */
  gcc_assert (!sibcall);

  /* This call has an unconditional jump in its delay slot.  */
  xoperands[0] = XEXP (PATTERN (NEXT_INSN (insn)), 1);

  if (!delay_slot_filled && INSN_ADDRESSES_SET_P ())
    {
      /* See if the return address can be adjusted.  Use the containing
         sequence insn's address.  */
      rtx seq_insn = NEXT_INSN (PREV_INSN (XVECEXP (final_sequence, 0, 0)));
      int distance = (INSN_ADDRESSES (INSN_UID (JUMP_LABEL (NEXT_INSN (insn))))
		      - INSN_ADDRESSES (INSN_UID (seq_insn)) - 8);

      if (VAL_14_BITS_P (distance))
	{
	  xoperands[1] = gen_label_rtx ();
	  output_asm_insn ("ldo %0-%1(%%r2),%%r2", xoperands);
	  (*targetm.asm_out.internal_label) (asm_out_file, "L",
					     CODE_LABEL_NUMBER (xoperands[1]));
	}
      else
	output_asm_insn ("nop\n\tb,n %0", xoperands);
    }
  else
    output_asm_insn ("b,n %0", xoperands);

  /* Delete the jump.  */
  PUT_CODE (NEXT_INSN (insn), NOTE);
  NOTE_LINE_NUMBER (NEXT_INSN (insn)) = NOTE_INSN_DELETED;
  NOTE_SOURCE_FILE (NEXT_INSN (insn)) = 0;

  return "";
}

/* Return the attribute length of the indirect call instruction INSN.
   The length must match the code generated by output_indirect call.
   The returned length includes the delay slot.  Currently, the delay
   slot of an indirect call sequence is not exposed and it is used by
   the sequence itself.  */

int
attr_length_indirect_call (rtx insn)
{
  unsigned long distance = -1;
  unsigned long total = IN_NAMED_SECTION_P (cfun->decl) ? 0 : total_code_bytes;

  if (INSN_ADDRESSES_SET_P ())
    {
      distance = (total + insn_current_reference_address (insn));
      if (distance < total)
	distance = -1;
    }

  if (TARGET_64BIT)
    return 12;

  if (TARGET_FAST_INDIRECT_CALLS
      || (!TARGET_PORTABLE_RUNTIME
	  && ((TARGET_PA_20 && !TARGET_SOM && distance < 7600000)
	      || distance < 240000)))
    return 8;

  if (flag_pic)
    return 24;

  if (TARGET_PORTABLE_RUNTIME)
    return 20;

  /* Out of reach, can use ble.  */
  return 12;
}

const char *
output_indirect_call (rtx insn, rtx call_dest)
{
  rtx xoperands[1];

  if (TARGET_64BIT)
    {
      xoperands[0] = call_dest;
      output_asm_insn ("ldd 16(%0),%%r2", xoperands);
      output_asm_insn ("bve,l (%%r2),%%r2\n\tldd 24(%0),%%r27", xoperands);
      return "";
    }

  /* First the special case for kernels, level 0 systems, etc.  */
  if (TARGET_FAST_INDIRECT_CALLS)
    return "ble 0(%%sr4,%%r22)\n\tcopy %%r31,%%r2"; 

  /* Now the normal case -- we can reach $$dyncall directly or
     we're sure that we can get there via a long-branch stub. 

     No need to check target flags as the length uniquely identifies
     the remaining cases.  */
  if (attr_length_indirect_call (insn) == 8)
    {
      /* The HP linker sometimes substitutes a BLE for BL/B,L calls to
	 $$dyncall.  Since BLE uses %r31 as the link register, the 22-bit
	 variant of the B,L instruction can't be used on the SOM target.  */
      if (TARGET_PA_20 && !TARGET_SOM)
	return ".CALL\tARGW0=GR\n\tb,l $$dyncall,%%r2\n\tcopy %%r2,%%r31";
      else
	return ".CALL\tARGW0=GR\n\tbl $$dyncall,%%r31\n\tcopy %%r31,%%r2";
    }

  /* Long millicode call, but we are not generating PIC or portable runtime
     code.  */
  if (attr_length_indirect_call (insn) == 12)
    return ".CALL\tARGW0=GR\n\tldil L'$$dyncall,%%r2\n\tble R'$$dyncall(%%sr4,%%r2)\n\tcopy %%r31,%%r2";

  /* Long millicode call for portable runtime.  */
  if (attr_length_indirect_call (insn) == 20)
    return "ldil L'$$dyncall,%%r31\n\tldo R'$$dyncall(%%r31),%%r31\n\tblr %%r0,%%r2\n\tbv,n %%r0(%%r31)\n\tnop";

  /* We need a long PIC call to $$dyncall.  */
  xoperands[0] = NULL_RTX;
  output_asm_insn ("{bl|b,l} .+8,%%r1", xoperands);
  if (TARGET_SOM || !TARGET_GAS)
    {
      xoperands[0] = gen_label_rtx ();
      output_asm_insn ("addil L'$$dyncall-%0,%%r1", xoperands);
      (*targetm.asm_out.internal_label) (asm_out_file, "L",
					 CODE_LABEL_NUMBER (xoperands[0]));
      output_asm_insn ("ldo R'$$dyncall-%0(%%r1),%%r1", xoperands);
    }
  else
    {
      output_asm_insn ("addil L'$$dyncall-$PIC_pcrel$0+4,%%r1", xoperands);
      output_asm_insn ("ldo R'$$dyncall-$PIC_pcrel$0+8(%%r1),%%r1",
		       xoperands);
    }
  output_asm_insn ("blr %%r0,%%r2", xoperands);
  output_asm_insn ("bv,n %%r0(%%r1)\n\tnop", xoperands);
  return "";
}

/* Return the total length of the save and restore instructions needed for
   the data linkage table pointer (i.e., the PIC register) across the call
   instruction INSN.  No-return calls do not require a save and restore.
   In addition, we may be able to avoid the save and restore for calls
   within the same translation unit.  */

int
attr_length_save_restore_dltp (rtx insn)
{
  if (find_reg_note (insn, REG_NORETURN, NULL_RTX))
    return 0;

  return 8;
}

/* In HPUX 8.0's shared library scheme, special relocations are needed
   for function labels if they might be passed to a function
   in a shared library (because shared libraries don't live in code
   space), and special magic is needed to construct their address.  */

void
hppa_encode_label (rtx sym)
{
  const char *str = XSTR (sym, 0);
  int len = strlen (str) + 1;
  char *newstr, *p;

  p = newstr = alloca (len + 1);
  *p++ = '@';
  strcpy (p, str);

  XSTR (sym, 0) = ggc_alloc_string (newstr, len);
}

static void
pa_encode_section_info (tree decl, rtx rtl, int first)
{
  default_encode_section_info (decl, rtl, first);

  if (first && TEXT_SPACE_P (decl))
    {
      SYMBOL_REF_FLAG (XEXP (rtl, 0)) = 1;
      if (TREE_CODE (decl) == FUNCTION_DECL)
	hppa_encode_label (XEXP (rtl, 0));
    }
}

/* This is sort of inverse to pa_encode_section_info.  */

static const char *
pa_strip_name_encoding (const char *str)
{
  str += (*str == '@');
  str += (*str == '*');
  return str;
}

int
function_label_operand (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  return GET_CODE (op) == SYMBOL_REF && FUNCTION_NAME_P (XSTR (op, 0));
}

/* Returns 1 if OP is a function label involved in a simple addition
   with a constant.  Used to keep certain patterns from matching
   during instruction combination.  */
int
is_function_label_plus_const (rtx op)
{
  /* Strip off any CONST.  */
  if (GET_CODE (op) == CONST)
    op = XEXP (op, 0);

  return (GET_CODE (op) == PLUS
	  && function_label_operand (XEXP (op, 0), Pmode)
	  && GET_CODE (XEXP (op, 1)) == CONST_INT);
}

/* Output assembly code for a thunk to FUNCTION.  */

static void
pa_asm_output_mi_thunk (FILE *file, tree thunk_fndecl, HOST_WIDE_INT delta,
			HOST_WIDE_INT vcall_offset ATTRIBUTE_UNUSED,
			tree function)
{
  static unsigned int current_thunk_number;
  int val_14 = VAL_14_BITS_P (delta);
  int nbytes = 0;
  char label[16];
  rtx xoperands[4];

  xoperands[0] = XEXP (DECL_RTL (function), 0);
  xoperands[1] = XEXP (DECL_RTL (thunk_fndecl), 0);
  xoperands[2] = GEN_INT (delta);

  ASM_OUTPUT_LABEL (file, XSTR (xoperands[1], 0));
  fprintf (file, "\t.PROC\n\t.CALLINFO FRAME=0,NO_CALLS\n\t.ENTRY\n");

  /* Output the thunk.  We know that the function is in the same
     translation unit (i.e., the same space) as the thunk, and that
     thunks are output after their method.  Thus, we don't need an
     external branch to reach the function.  With SOM and GAS,
     functions and thunks are effectively in different sections.
     Thus, we can always use a IA-relative branch and the linker
     will add a long branch stub if necessary.

     However, we have to be careful when generating PIC code on the
     SOM port to ensure that the sequence does not transfer to an
     import stub for the target function as this could clobber the
     return value saved at SP-24.  This would also apply to the
     32-bit linux port if the multi-space model is implemented.  */
  if ((!TARGET_LONG_CALLS && TARGET_SOM && !TARGET_PORTABLE_RUNTIME
       && !(flag_pic && TREE_PUBLIC (function))
       && (TARGET_GAS || last_address < 262132))
      || (!TARGET_LONG_CALLS && !TARGET_SOM && !TARGET_PORTABLE_RUNTIME
	  && ((targetm.have_named_sections
	       && DECL_SECTION_NAME (thunk_fndecl) != NULL
	       /* The GNU 64-bit linker has rather poor stub management.
		  So, we use a long branch from thunks that aren't in
		  the same section as the target function.  */
	       && ((!TARGET_64BIT
		    && (DECL_SECTION_NAME (thunk_fndecl)
			!= DECL_SECTION_NAME (function)))
		   || ((DECL_SECTION_NAME (thunk_fndecl)
			== DECL_SECTION_NAME (function))
		       && last_address < 262132)))
	      || (!targetm.have_named_sections && last_address < 262132))))
    {
      if (!val_14)
	output_asm_insn ("addil L'%2,%%r26", xoperands);

      output_asm_insn ("b %0", xoperands);

      if (val_14)
	{
	  output_asm_insn ("ldo %2(%%r26),%%r26", xoperands);
	  nbytes += 8;
	}
      else
	{
	  output_asm_insn ("ldo R'%2(%%r1),%%r26", xoperands);
	  nbytes += 12;
	}
    }
  else if (TARGET_64BIT)
    {
      /* We only have one call-clobbered scratch register, so we can't
         make use of the delay slot if delta doesn't fit in 14 bits.  */
      if (!val_14)
	{
	  output_asm_insn ("addil L'%2,%%r26", xoperands);
	  output_asm_insn ("ldo R'%2(%%r1),%%r26", xoperands);
	}

      output_asm_insn ("b,l .+8,%%r1", xoperands);

      if (TARGET_GAS)
	{
	  output_asm_insn ("addil L'%0-$PIC_pcrel$0+4,%%r1", xoperands);
	  output_asm_insn ("ldo R'%0-$PIC_pcrel$0+8(%%r1),%%r1", xoperands);
	}
      else
	{
	  xoperands[3] = GEN_INT (val_14 ? 8 : 16);
	  output_asm_insn ("addil L'%0-%1-%3,%%r1", xoperands);
	}

      if (val_14)
	{
	  output_asm_insn ("bv %%r0(%%r1)", xoperands);
	  output_asm_insn ("ldo %2(%%r26),%%r26", xoperands);
	  nbytes += 20;
	}
      else
	{
	  output_asm_insn ("bv,n %%r0(%%r1)", xoperands);
	  nbytes += 24;
	}
    }
  else if (TARGET_PORTABLE_RUNTIME)
    {
      output_asm_insn ("ldil L'%0,%%r1", xoperands);
      output_asm_insn ("ldo R'%0(%%r1),%%r22", xoperands);

      if (!val_14)
	output_asm_insn ("addil L'%2,%%r26", xoperands);

      output_asm_insn ("bv %%r0(%%r22)", xoperands);

      if (val_14)
	{
	  output_asm_insn ("ldo %2(%%r26),%%r26", xoperands);
	  nbytes += 16;
	}
      else
	{
	  output_asm_insn ("ldo R'%2(%%r1),%%r26", xoperands);
	  nbytes += 20;
	}
    }
  else if (TARGET_SOM && flag_pic && TREE_PUBLIC (function))
    {
      /* The function is accessible from outside this module.  The only
	 way to avoid an import stub between the thunk and function is to
	 call the function directly with an indirect sequence similar to
	 that used by $$dyncall.  This is possible because $$dyncall acts
	 as the import stub in an indirect call.  */
      ASM_GENERATE_INTERNAL_LABEL (label, "LTHN", current_thunk_number);
      xoperands[3] = gen_rtx_SYMBOL_REF (Pmode, label);
      output_asm_insn ("addil LT'%3,%%r19", xoperands);
      output_asm_insn ("ldw RT'%3(%%r1),%%r22", xoperands);
      output_asm_insn ("ldw 0(%%sr0,%%r22),%%r22", xoperands);
      output_asm_insn ("bb,>=,n %%r22,30,.+16", xoperands);
      output_asm_insn ("depi 0,31,2,%%r22", xoperands);
      output_asm_insn ("ldw 4(%%sr0,%%r22),%%r19", xoperands);
      output_asm_insn ("ldw 0(%%sr0,%%r22),%%r22", xoperands);

      if (!val_14)
	{
	  output_asm_insn ("addil L'%2,%%r26", xoperands);
	  nbytes += 4;
	}

      if (TARGET_PA_20)
	{
	  output_asm_insn ("bve (%%r22)", xoperands);
	  nbytes += 36;
	}
      else if (TARGET_NO_SPACE_REGS)
	{
	  output_asm_insn ("be 0(%%sr4,%%r22)", xoperands);
	  nbytes += 36;
	}
      else
	{
	  output_asm_insn ("ldsid (%%sr0,%%r22),%%r21", xoperands);
	  output_asm_insn ("mtsp %%r21,%%sr0", xoperands);
	  output_asm_insn ("be 0(%%sr0,%%r22)", xoperands);
	  nbytes += 44;
	}

      if (val_14)
	output_asm_insn ("ldo %2(%%r26),%%r26", xoperands);
      else
	output_asm_insn ("ldo R'%2(%%r1),%%r26", xoperands);
    }
  else if (flag_pic)
    {
      output_asm_insn ("{bl|b,l} .+8,%%r1", xoperands);

      if (TARGET_SOM || !TARGET_GAS)
	{
	  output_asm_insn ("addil L'%0-%1-8,%%r1", xoperands);
	  output_asm_insn ("ldo R'%0-%1-8(%%r1),%%r22", xoperands);
	}
      else
	{
	  output_asm_insn ("addil L'%0-$PIC_pcrel$0+4,%%r1", xoperands);
	  output_asm_insn ("ldo R'%0-$PIC_pcrel$0+8(%%r1),%%r22", xoperands);
	}

      if (!val_14)
	output_asm_insn ("addil L'%2,%%r26", xoperands);

      output_asm_insn ("bv %%r0(%%r22)", xoperands);

      if (val_14)
	{
	  output_asm_insn ("ldo %2(%%r26),%%r26", xoperands);
	  nbytes += 20;
	}
      else
	{
	  output_asm_insn ("ldo R'%2(%%r1),%%r26", xoperands);
	  nbytes += 24;
	}
    }
  else
    {
      if (!val_14)
	output_asm_insn ("addil L'%2,%%r26", xoperands);

      output_asm_insn ("ldil L'%0,%%r22", xoperands);
      output_asm_insn ("be R'%0(%%sr4,%%r22)", xoperands);

      if (val_14)
	{
	  output_asm_insn ("ldo %2(%%r26),%%r26", xoperands);
	  nbytes += 12;
	}
      else
	{
	  output_asm_insn ("ldo R'%2(%%r1),%%r26", xoperands);
	  nbytes += 16;
	}
    }

  fprintf (file, "\t.EXIT\n\t.PROCEND\n");

  if (TARGET_SOM && TARGET_GAS)
    {
      /* We done with this subspace except possibly for some additional
	 debug information.  Forget that we are in this subspace to ensure
	 that the next function is output in its own subspace.  */
      in_section = NULL;
      cfun->machine->in_nsubspa = 2;
    }

  if (TARGET_SOM && flag_pic && TREE_PUBLIC (function))
    {
      switch_to_section (data_section);
      output_asm_insn (".align 4", xoperands);
      ASM_OUTPUT_LABEL (file, label);
      output_asm_insn (".word P'%0", xoperands);
    }

  current_thunk_number++;
  nbytes = ((nbytes + FUNCTION_BOUNDARY / BITS_PER_UNIT - 1)
	    & ~(FUNCTION_BOUNDARY / BITS_PER_UNIT - 1));
  last_address += nbytes;
  update_total_code_bytes (nbytes);
}

/* Only direct calls to static functions are allowed to be sibling (tail)
   call optimized.

   This restriction is necessary because some linker generated stubs will
   store return pointers into rp' in some cases which might clobber a
   live value already in rp'.

   In a sibcall the current function and the target function share stack
   space.  Thus if the path to the current function and the path to the
   target function save a value in rp', they save the value into the
   same stack slot, which has undesirable consequences.

   Because of the deferred binding nature of shared libraries any function
   with external scope could be in a different load module and thus require
   rp' to be saved when calling that function.  So sibcall optimizations
   can only be safe for static function.

   Note that GCC never needs return value relocations, so we don't have to
   worry about static calls with return value relocations (which require
   saving rp').

   It is safe to perform a sibcall optimization when the target function
   will never return.  */
static bool
pa_function_ok_for_sibcall (tree decl, tree exp ATTRIBUTE_UNUSED)
{
  if (TARGET_PORTABLE_RUNTIME)
    return false;

  /* Sibcalls are ok for TARGET_ELF32 as along as the linker is used in
     single subspace mode and the call is not indirect.  As far as I know,
     there is no operating system support for the multiple subspace mode.
     It might be possible to support indirect calls if we didn't use
     $$dyncall (see the indirect sequence generated in output_call).  */
  if (TARGET_ELF32)
    return (decl != NULL_TREE);

  /* Sibcalls are not ok because the arg pointer register is not a fixed
     register.  This prevents the sibcall optimization from occurring.  In
     addition, there are problems with stub placement using GNU ld.  This
     is because a normal sibcall branch uses a 17-bit relocation while
     a regular call branch uses a 22-bit relocation.  As a result, more
     care needs to be taken in the placement of long-branch stubs.  */
  if (TARGET_64BIT)
    return false;

  /* Sibcalls are only ok within a translation unit.  */
  return (decl && !TREE_PUBLIC (decl));
}

/* ??? Addition is not commutative on the PA due to the weird implicit
   space register selection rules for memory addresses.  Therefore, we
   don't consider a + b == b + a, as this might be inside a MEM.  */
static bool
pa_commutative_p (rtx x, int outer_code)
{
  return (COMMUTATIVE_P (x)
	  && (TARGET_NO_SPACE_REGS
	      || (outer_code != UNKNOWN && outer_code != MEM)
	      || GET_CODE (x) != PLUS));
}

/* Returns 1 if the 6 operands specified in OPERANDS are suitable for
   use in fmpyadd instructions.  */
int
fmpyaddoperands (rtx *operands)
{
  enum machine_mode mode = GET_MODE (operands[0]);

  /* Must be a floating point mode.  */
  if (mode != SFmode && mode != DFmode)
    return 0;

  /* All modes must be the same.  */
  if (! (mode == GET_MODE (operands[1])
	 && mode == GET_MODE (operands[2])
	 && mode == GET_MODE (operands[3])
	 && mode == GET_MODE (operands[4])
	 && mode == GET_MODE (operands[5])))
    return 0;

  /* All operands must be registers.  */
  if (! (GET_CODE (operands[1]) == REG
	 && GET_CODE (operands[2]) == REG
	 && GET_CODE (operands[3]) == REG
	 && GET_CODE (operands[4]) == REG
	 && GET_CODE (operands[5]) == REG))
    return 0;

  /* Only 2 real operands to the addition.  One of the input operands must
     be the same as the output operand.  */
  if (! rtx_equal_p (operands[3], operands[4])
      && ! rtx_equal_p (operands[3], operands[5]))
    return 0;

  /* Inout operand of add cannot conflict with any operands from multiply.  */
  if (rtx_equal_p (operands[3], operands[0])
     || rtx_equal_p (operands[3], operands[1])
     || rtx_equal_p (operands[3], operands[2]))
    return 0;

  /* multiply cannot feed into addition operands.  */
  if (rtx_equal_p (operands[4], operands[0])
      || rtx_equal_p (operands[5], operands[0]))
    return 0;

  /* SFmode limits the registers to the upper 32 of the 32bit FP regs.  */
  if (mode == SFmode
      && (REGNO_REG_CLASS (REGNO (operands[0])) != FPUPPER_REGS
	  || REGNO_REG_CLASS (REGNO (operands[1])) != FPUPPER_REGS
	  || REGNO_REG_CLASS (REGNO (operands[2])) != FPUPPER_REGS
	  || REGNO_REG_CLASS (REGNO (operands[3])) != FPUPPER_REGS
	  || REGNO_REG_CLASS (REGNO (operands[4])) != FPUPPER_REGS
	  || REGNO_REG_CLASS (REGNO (operands[5])) != FPUPPER_REGS))
    return 0;

  /* Passed.  Operands are suitable for fmpyadd.  */
  return 1;
}

#if !defined(USE_COLLECT2)
static void
pa_asm_out_constructor (rtx symbol, int priority)
{
  if (!function_label_operand (symbol, VOIDmode))
    hppa_encode_label (symbol);

#ifdef CTORS_SECTION_ASM_OP
  default_ctor_section_asm_out_constructor (symbol, priority);
#else
# ifdef TARGET_ASM_NAMED_SECTION
  default_named_section_asm_out_constructor (symbol, priority);
# else
  default_stabs_asm_out_constructor (symbol, priority);
# endif
#endif
}

static void
pa_asm_out_destructor (rtx symbol, int priority)
{
  if (!function_label_operand (symbol, VOIDmode))
    hppa_encode_label (symbol);

#ifdef DTORS_SECTION_ASM_OP
  default_dtor_section_asm_out_destructor (symbol, priority);
#else
# ifdef TARGET_ASM_NAMED_SECTION
  default_named_section_asm_out_destructor (symbol, priority);
# else
  default_stabs_asm_out_destructor (symbol, priority);
# endif
#endif
}
#endif

/* This function places uninitialized global data in the bss section.
   The ASM_OUTPUT_ALIGNED_BSS macro needs to be defined to call this
   function on the SOM port to prevent uninitialized global data from
   being placed in the data section.  */
   
void
pa_asm_output_aligned_bss (FILE *stream,
			   const char *name,
			   unsigned HOST_WIDE_INT size,
			   unsigned int align)
{
  switch_to_section (bss_section);
  fprintf (stream, "\t.align %u\n", align / BITS_PER_UNIT);

#ifdef ASM_OUTPUT_TYPE_DIRECTIVE
  ASM_OUTPUT_TYPE_DIRECTIVE (stream, name, "object");
#endif

#ifdef ASM_OUTPUT_SIZE_DIRECTIVE
  ASM_OUTPUT_SIZE_DIRECTIVE (stream, name, size);
#endif

  fprintf (stream, "\t.align %u\n", align / BITS_PER_UNIT);
  ASM_OUTPUT_LABEL (stream, name);
  fprintf (stream, "\t.block "HOST_WIDE_INT_PRINT_UNSIGNED"\n", size);
}

/* Both the HP and GNU assemblers under HP-UX provide a .comm directive
   that doesn't allow the alignment of global common storage to be directly
   specified.  The SOM linker aligns common storage based on the rounded
   value of the NUM_BYTES parameter in the .comm directive.  It's not
   possible to use the .align directive as it doesn't affect the alignment
   of the label associated with a .comm directive.  */

void
pa_asm_output_aligned_common (FILE *stream,
			      const char *name,
			      unsigned HOST_WIDE_INT size,
			      unsigned int align)
{
  unsigned int max_common_align;

  max_common_align = TARGET_64BIT ? 128 : (size >= 4096 ? 256 : 64);
  if (align > max_common_align)
    {
      warning (0, "alignment (%u) for %s exceeds maximum alignment "
	       "for global common data.  Using %u",
	       align / BITS_PER_UNIT, name, max_common_align / BITS_PER_UNIT);
      align = max_common_align;
    }

  switch_to_section (bss_section);

  assemble_name (stream, name);
  fprintf (stream, "\t.comm "HOST_WIDE_INT_PRINT_UNSIGNED"\n",
           MAX (size, align / BITS_PER_UNIT));
}

/* We can't use .comm for local common storage as the SOM linker effectively
   treats the symbol as universal and uses the same storage for local symbols
   with the same name in different object files.  The .block directive
   reserves an uninitialized block of storage.  However, it's not common
   storage.  Fortunately, GCC never requests common storage with the same
   name in any given translation unit.  */

void
pa_asm_output_aligned_local (FILE *stream,
			     const char *name,
			     unsigned HOST_WIDE_INT size,
			     unsigned int align)
{
  switch_to_section (bss_section);
  fprintf (stream, "\t.align %u\n", align / BITS_PER_UNIT);

#ifdef LOCAL_ASM_OP
  fprintf (stream, "%s", LOCAL_ASM_OP);
  assemble_name (stream, name);
  fprintf (stream, "\n");
#endif

  ASM_OUTPUT_LABEL (stream, name);
  fprintf (stream, "\t.block "HOST_WIDE_INT_PRINT_UNSIGNED"\n", size);
}

/* Returns 1 if the 6 operands specified in OPERANDS are suitable for
   use in fmpysub instructions.  */
int
fmpysuboperands (rtx *operands)
{
  enum machine_mode mode = GET_MODE (operands[0]);

  /* Must be a floating point mode.  */
  if (mode != SFmode && mode != DFmode)
    return 0;

  /* All modes must be the same.  */
  if (! (mode == GET_MODE (operands[1])
	 && mode == GET_MODE (operands[2])
	 && mode == GET_MODE (operands[3])
	 && mode == GET_MODE (operands[4])
	 && mode == GET_MODE (operands[5])))
    return 0;

  /* All operands must be registers.  */
  if (! (GET_CODE (operands[1]) == REG
	 && GET_CODE (operands[2]) == REG
	 && GET_CODE (operands[3]) == REG
	 && GET_CODE (operands[4]) == REG
	 && GET_CODE (operands[5]) == REG))
    return 0;

  /* Only 2 real operands to the subtraction.  Subtraction is not a commutative
     operation, so operands[4] must be the same as operand[3].  */
  if (! rtx_equal_p (operands[3], operands[4]))
    return 0;

  /* multiply cannot feed into subtraction.  */
  if (rtx_equal_p (operands[5], operands[0]))
    return 0;

  /* Inout operand of sub cannot conflict with any operands from multiply.  */
  if (rtx_equal_p (operands[3], operands[0])
     || rtx_equal_p (operands[3], operands[1])
     || rtx_equal_p (operands[3], operands[2]))
    return 0;

  /* SFmode limits the registers to the upper 32 of the 32bit FP regs.  */
  if (mode == SFmode
      && (REGNO_REG_CLASS (REGNO (operands[0])) != FPUPPER_REGS
	  || REGNO_REG_CLASS (REGNO (operands[1])) != FPUPPER_REGS
	  || REGNO_REG_CLASS (REGNO (operands[2])) != FPUPPER_REGS
	  || REGNO_REG_CLASS (REGNO (operands[3])) != FPUPPER_REGS
	  || REGNO_REG_CLASS (REGNO (operands[4])) != FPUPPER_REGS
	  || REGNO_REG_CLASS (REGNO (operands[5])) != FPUPPER_REGS))
    return 0;

  /* Passed.  Operands are suitable for fmpysub.  */
  return 1;
}

/* Return 1 if the given constant is 2, 4, or 8.  These are the valid
   constants for shadd instructions.  */
int
shadd_constant_p (int val)
{
  if (val == 2 || val == 4 || val == 8)
    return 1;
  else
    return 0;
}

/* Return 1 if OP is valid as a base or index register in a
   REG+REG address.  */

int
borx_reg_operand (rtx op, enum machine_mode mode)
{
  if (GET_CODE (op) != REG)
    return 0;

  /* We must reject virtual registers as the only expressions that
     can be instantiated are REG and REG+CONST.  */
  if (op == virtual_incoming_args_rtx
      || op == virtual_stack_vars_rtx
      || op == virtual_stack_dynamic_rtx
      || op == virtual_outgoing_args_rtx
      || op == virtual_cfa_rtx)
    return 0;

  /* While it's always safe to index off the frame pointer, it's not
     profitable to do so when the frame pointer is being eliminated.  */
  if (!reload_completed
      && flag_omit_frame_pointer
      && !current_function_calls_alloca
      && op == frame_pointer_rtx)
    return 0;

  return register_operand (op, mode);
}

/* Return 1 if this operand is anything other than a hard register.  */

int
non_hard_reg_operand (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  return ! (GET_CODE (op) == REG && REGNO (op) < FIRST_PSEUDO_REGISTER);
}

/* Return 1 if INSN branches forward.  Should be using insn_addresses
   to avoid walking through all the insns...  */
static int
forward_branch_p (rtx insn)
{
  rtx label = JUMP_LABEL (insn);

  while (insn)
    {
      if (insn == label)
	break;
      else
	insn = NEXT_INSN (insn);
    }

  return (insn == label);
}

/* Return 1 if OP is an equality comparison, else return 0.  */
int
eq_neq_comparison_operator (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  return (GET_CODE (op) == EQ || GET_CODE (op) == NE);
}

/* Return 1 if INSN is in the delay slot of a call instruction.  */
int
jump_in_call_delay (rtx insn)
{

  if (GET_CODE (insn) != JUMP_INSN)
    return 0;

  if (PREV_INSN (insn)
      && PREV_INSN (PREV_INSN (insn))
      && GET_CODE (next_real_insn (PREV_INSN (PREV_INSN (insn)))) == INSN)
    {
      rtx test_insn = next_real_insn (PREV_INSN (PREV_INSN (insn)));

      return (GET_CODE (PATTERN (test_insn)) == SEQUENCE
	      && XVECEXP (PATTERN (test_insn), 0, 1) == insn);

    }
  else
    return 0;
}

/* Output an unconditional move and branch insn.  */

const char *
output_parallel_movb (rtx *operands, rtx insn)
{
  int length = get_attr_length (insn);

  /* These are the cases in which we win.  */
  if (length == 4)
    return "mov%I1b,tr %1,%0,%2";

  /* None of the following cases win, but they don't lose either.  */
  if (length == 8)
    {
      if (dbr_sequence_length () == 0)
	{
	  /* Nothing in the delay slot, fake it by putting the combined
	     insn (the copy or add) in the delay slot of a bl.  */
	  if (GET_CODE (operands[1]) == CONST_INT)
	    return "b %2\n\tldi %1,%0";
	  else
	    return "b %2\n\tcopy %1,%0";
	}
      else
	{
	  /* Something in the delay slot, but we've got a long branch.  */
	  if (GET_CODE (operands[1]) == CONST_INT)
	    return "ldi %1,%0\n\tb %2";
	  else
	    return "copy %1,%0\n\tb %2";
	}
    }

  if (GET_CODE (operands[1]) == CONST_INT)
    output_asm_insn ("ldi %1,%0", operands);
  else
    output_asm_insn ("copy %1,%0", operands);
  return output_lbranch (operands[2], insn, 1);
}

/* Output an unconditional add and branch insn.  */

const char *
output_parallel_addb (rtx *operands, rtx insn)
{
  int length = get_attr_length (insn);

  /* To make life easy we want operand0 to be the shared input/output
     operand and operand1 to be the readonly operand.  */
  if (operands[0] == operands[1])
    operands[1] = operands[2];

  /* These are the cases in which we win.  */
  if (length == 4)
    return "add%I1b,tr %1,%0,%3";

  /* None of the following cases win, but they don't lose either.  */
  if (length == 8)
    {
      if (dbr_sequence_length () == 0)
	/* Nothing in the delay slot, fake it by putting the combined
	   insn (the copy or add) in the delay slot of a bl.  */
	return "b %3\n\tadd%I1 %1,%0,%0";
      else
	/* Something in the delay slot, but we've got a long branch.  */
	return "add%I1 %1,%0,%0\n\tb %3";
    }

  output_asm_insn ("add%I1 %1,%0,%0", operands);
  return output_lbranch (operands[3], insn, 1);
}

/* Return nonzero if INSN (a jump insn) immediately follows a call
   to a named function.  This is used to avoid filling the delay slot
   of the jump since it can usually be eliminated by modifying RP in
   the delay slot of the call.  */

int
following_call (rtx insn)
{
  if (! TARGET_JUMP_IN_DELAY)
    return 0;

  /* Find the previous real insn, skipping NOTEs.  */
  insn = PREV_INSN (insn);
  while (insn && GET_CODE (insn) == NOTE)
    insn = PREV_INSN (insn);

  /* Check for CALL_INSNs and millicode calls.  */
  if (insn
      && ((GET_CODE (insn) == CALL_INSN
	   && get_attr_type (insn) != TYPE_DYNCALL)
	  || (GET_CODE (insn) == INSN
	      && GET_CODE (PATTERN (insn)) != SEQUENCE
	      && GET_CODE (PATTERN (insn)) != USE
	      && GET_CODE (PATTERN (insn)) != CLOBBER
	      && get_attr_type (insn) == TYPE_MILLI)))
    return 1;

  return 0;
}

/* We use this hook to perform a PA specific optimization which is difficult
   to do in earlier passes.

   We want the delay slots of branches within jump tables to be filled.
   None of the compiler passes at the moment even has the notion that a
   PA jump table doesn't contain addresses, but instead contains actual
   instructions!

   Because we actually jump into the table, the addresses of each entry
   must stay constant in relation to the beginning of the table (which
   itself must stay constant relative to the instruction to jump into
   it).  I don't believe we can guarantee earlier passes of the compiler
   will adhere to those rules.

   So, late in the compilation process we find all the jump tables, and
   expand them into real code -- e.g. each entry in the jump table vector
   will get an appropriate label followed by a jump to the final target.

   Reorg and the final jump pass can then optimize these branches and
   fill their delay slots.  We end up with smaller, more efficient code.

   The jump instructions within the table are special; we must be able
   to identify them during assembly output (if the jumps don't get filled
   we need to emit a nop rather than nullifying the delay slot)).  We
   identify jumps in switch tables by using insns with the attribute
   type TYPE_BTABLE_BRANCH.

   We also surround the jump table itself with BEGIN_BRTAB and END_BRTAB
   insns.  This serves two purposes, first it prevents jump.c from
   noticing that the last N entries in the table jump to the instruction
   immediately after the table and deleting the jumps.  Second, those
   insns mark where we should emit .begin_brtab and .end_brtab directives
   when using GAS (allows for better link time optimizations).  */

static void
pa_reorg (void)
{
  rtx insn;

  remove_useless_addtr_insns (1);

  if (pa_cpu < PROCESSOR_8000)
    pa_combine_instructions ();


  /* This is fairly cheap, so always run it if optimizing.  */
  if (optimize > 0 && !TARGET_BIG_SWITCH)
    {
      /* Find and explode all ADDR_VEC or ADDR_DIFF_VEC insns.  */
      for (insn = get_insns (); insn; insn = NEXT_INSN (insn))
	{
	  rtx pattern, tmp, location, label;
	  unsigned int length, i;

	  /* Find an ADDR_VEC or ADDR_DIFF_VEC insn to explode.  */
	  if (GET_CODE (insn) != JUMP_INSN
	      || (GET_CODE (PATTERN (insn)) != ADDR_VEC
		  && GET_CODE (PATTERN (insn)) != ADDR_DIFF_VEC))
	    continue;

	  /* Emit marker for the beginning of the branch table.  */
	  emit_insn_before (gen_begin_brtab (), insn);

	  pattern = PATTERN (insn);
	  location = PREV_INSN (insn);
          length = XVECLEN (pattern, GET_CODE (pattern) == ADDR_DIFF_VEC);

	  for (i = 0; i < length; i++)
	    {
	      /* Emit a label before each jump to keep jump.c from
		 removing this code.  */
	      tmp = gen_label_rtx ();
	      LABEL_NUSES (tmp) = 1;
	      emit_label_after (tmp, location);
	      location = NEXT_INSN (location);

	      if (GET_CODE (pattern) == ADDR_VEC)
		label = XEXP (XVECEXP (pattern, 0, i), 0);
	      else
		label = XEXP (XVECEXP (pattern, 1, i), 0);

	      tmp = gen_short_jump (label);

	      /* Emit the jump itself.  */
	      tmp = emit_jump_insn_after (tmp, location);
	      JUMP_LABEL (tmp) = label;
	      LABEL_NUSES (label)++;
	      location = NEXT_INSN (location);

	      /* Emit a BARRIER after the jump.  */
	      emit_barrier_after (location);
	      location = NEXT_INSN (location);
	    }

	  /* Emit marker for the end of the branch table.  */
	  emit_insn_before (gen_end_brtab (), location);
	  location = NEXT_INSN (location);
	  emit_barrier_after (location);

	  /* Delete the ADDR_VEC or ADDR_DIFF_VEC.  */
	  delete_insn (insn);
	}
    }
  else
    {
      /* Still need brtab marker insns.  FIXME: the presence of these
	 markers disables output of the branch table to readonly memory,
	 and any alignment directives that might be needed.  Possibly,
	 the begin_brtab insn should be output before the label for the
	 table.  This doesn't matter at the moment since the tables are
	 always output in the text section.  */
      for (insn = get_insns (); insn; insn = NEXT_INSN (insn))
	{
	  /* Find an ADDR_VEC insn.  */
	  if (GET_CODE (insn) != JUMP_INSN
	      || (GET_CODE (PATTERN (insn)) != ADDR_VEC
		  && GET_CODE (PATTERN (insn)) != ADDR_DIFF_VEC))
	    continue;

	  /* Now generate markers for the beginning and end of the
	     branch table.  */
	  emit_insn_before (gen_begin_brtab (), insn);
	  emit_insn_after (gen_end_brtab (), insn);
	}
    }
}

/* The PA has a number of odd instructions which can perform multiple
   tasks at once.  On first generation PA machines (PA1.0 and PA1.1)
   it may be profitable to combine two instructions into one instruction
   with two outputs.  It's not profitable PA2.0 machines because the
   two outputs would take two slots in the reorder buffers.

   This routine finds instructions which can be combined and combines
   them.  We only support some of the potential combinations, and we
   only try common ways to find suitable instructions.

      * addb can add two registers or a register and a small integer
      and jump to a nearby (+-8k) location.  Normally the jump to the
      nearby location is conditional on the result of the add, but by
      using the "true" condition we can make the jump unconditional.
      Thus addb can perform two independent operations in one insn.

      * movb is similar to addb in that it can perform a reg->reg
      or small immediate->reg copy and jump to a nearby (+-8k location).

      * fmpyadd and fmpysub can perform a FP multiply and either an
      FP add or FP sub if the operands of the multiply and add/sub are
      independent (there are other minor restrictions).  Note both
      the fmpy and fadd/fsub can in theory move to better spots according
      to data dependencies, but for now we require the fmpy stay at a
      fixed location.

      * Many of the memory operations can perform pre & post updates
      of index registers.  GCC's pre/post increment/decrement addressing
      is far too simple to take advantage of all the possibilities.  This
      pass may not be suitable since those insns may not be independent.

      * comclr can compare two ints or an int and a register, nullify
      the following instruction and zero some other register.  This
      is more difficult to use as it's harder to find an insn which
      will generate a comclr than finding something like an unconditional
      branch.  (conditional moves & long branches create comclr insns).

      * Most arithmetic operations can conditionally skip the next
      instruction.  They can be viewed as "perform this operation
      and conditionally jump to this nearby location" (where nearby
      is an insns away).  These are difficult to use due to the
      branch length restrictions.  */

static void
pa_combine_instructions (void)
{
  rtx anchor, new;

  /* This can get expensive since the basic algorithm is on the
     order of O(n^2) (or worse).  Only do it for -O2 or higher
     levels of optimization.  */
  if (optimize < 2)
    return;

  /* Walk down the list of insns looking for "anchor" insns which
     may be combined with "floating" insns.  As the name implies,
     "anchor" instructions don't move, while "floating" insns may
     move around.  */
  new = gen_rtx_PARALLEL (VOIDmode, gen_rtvec (2, NULL_RTX, NULL_RTX));
  new = make_insn_raw (new);

  for (anchor = get_insns (); anchor; anchor = NEXT_INSN (anchor))
    {
      enum attr_pa_combine_type anchor_attr;
      enum attr_pa_combine_type floater_attr;

      /* We only care about INSNs, JUMP_INSNs, and CALL_INSNs.
	 Also ignore any special USE insns.  */
      if ((GET_CODE (anchor) != INSN
	  && GET_CODE (anchor) != JUMP_INSN
	  && GET_CODE (anchor) != CALL_INSN)
	  || GET_CODE (PATTERN (anchor)) == USE
	  || GET_CODE (PATTERN (anchor)) == CLOBBER
	  || GET_CODE (PATTERN (anchor)) == ADDR_VEC
	  || GET_CODE (PATTERN (anchor)) == ADDR_DIFF_VEC)
	continue;

      anchor_attr = get_attr_pa_combine_type (anchor);
      /* See if anchor is an insn suitable for combination.  */
      if (anchor_attr == PA_COMBINE_TYPE_FMPY
	  || anchor_attr == PA_COMBINE_TYPE_FADDSUB
	  || (anchor_attr == PA_COMBINE_TYPE_UNCOND_BRANCH
	      && ! forward_branch_p (anchor)))
	{
	  rtx floater;

	  for (floater = PREV_INSN (anchor);
	       floater;
	       floater = PREV_INSN (floater))
	    {
	      if (GET_CODE (floater) == NOTE
		  || (GET_CODE (floater) == INSN
		      && (GET_CODE (PATTERN (floater)) == USE
			  || GET_CODE (PATTERN (floater)) == CLOBBER)))
		continue;

	      /* Anything except a regular INSN will stop our search.  */
	      if (GET_CODE (floater) != INSN
		  || GET_CODE (PATTERN (floater)) == ADDR_VEC
		  || GET_CODE (PATTERN (floater)) == ADDR_DIFF_VEC)
		{
		  floater = NULL_RTX;
		  break;
		}

	      /* See if FLOATER is suitable for combination with the
		 anchor.  */
	      floater_attr = get_attr_pa_combine_type (floater);
	      if ((anchor_attr == PA_COMBINE_TYPE_FMPY
		   && floater_attr == PA_COMBINE_TYPE_FADDSUB)
		  || (anchor_attr == PA_COMBINE_TYPE_FADDSUB
		      && floater_attr == PA_COMBINE_TYPE_FMPY))
		{
		  /* If ANCHOR and FLOATER can be combined, then we're
		     done with this pass.  */
		  if (pa_can_combine_p (new, anchor, floater, 0,
					SET_DEST (PATTERN (floater)),
					XEXP (SET_SRC (PATTERN (floater)), 0),
					XEXP (SET_SRC (PATTERN (floater)), 1)))
		    break;
		}

	      else if (anchor_attr == PA_COMBINE_TYPE_UNCOND_BRANCH
		       && floater_attr == PA_COMBINE_TYPE_ADDMOVE)
		{
		  if (GET_CODE (SET_SRC (PATTERN (floater))) == PLUS)
		    {
		      if (pa_can_combine_p (new, anchor, floater, 0,
					    SET_DEST (PATTERN (floater)),
					XEXP (SET_SRC (PATTERN (floater)), 0),
					XEXP (SET_SRC (PATTERN (floater)), 1)))
			break;
		    }
		  else
		    {
		      if (pa_can_combine_p (new, anchor, floater, 0,
					    SET_DEST (PATTERN (floater)),
					    SET_SRC (PATTERN (floater)),
					    SET_SRC (PATTERN (floater))))
			break;
		    }
		}
	    }

	  /* If we didn't find anything on the backwards scan try forwards.  */
	  if (!floater
	      && (anchor_attr == PA_COMBINE_TYPE_FMPY
		  || anchor_attr == PA_COMBINE_TYPE_FADDSUB))
	    {
	      for (floater = anchor; floater; floater = NEXT_INSN (floater))
		{
		  if (GET_CODE (floater) == NOTE
		      || (GET_CODE (floater) == INSN
			  && (GET_CODE (PATTERN (floater)) == USE
			      || GET_CODE (PATTERN (floater)) == CLOBBER)))

		    continue;

		  /* Anything except a regular INSN will stop our search.  */
		  if (GET_CODE (floater) != INSN
		      || GET_CODE (PATTERN (floater)) == ADDR_VEC
		      || GET_CODE (PATTERN (floater)) == ADDR_DIFF_VEC)
		    {
		      floater = NULL_RTX;
		      break;
		    }

		  /* See if FLOATER is suitable for combination with the
		     anchor.  */
		  floater_attr = get_attr_pa_combine_type (floater);
		  if ((anchor_attr == PA_COMBINE_TYPE_FMPY
		       && floater_attr == PA_COMBINE_TYPE_FADDSUB)
		      || (anchor_attr == PA_COMBINE_TYPE_FADDSUB
			  && floater_attr == PA_COMBINE_TYPE_FMPY))
		    {
		      /* If ANCHOR and FLOATER can be combined, then we're
			 done with this pass.  */
		      if (pa_can_combine_p (new, anchor, floater, 1,
					    SET_DEST (PATTERN (floater)),
					    XEXP (SET_SRC (PATTERN (floater)),
						  0),
					    XEXP (SET_SRC (PATTERN (floater)),
						  1)))
			break;
		    }
		}
	    }

	  /* FLOATER will be nonzero if we found a suitable floating
	     insn for combination with ANCHOR.  */
	  if (floater
	      && (anchor_attr == PA_COMBINE_TYPE_FADDSUB
		  || anchor_attr == PA_COMBINE_TYPE_FMPY))
	    {
	      /* Emit the new instruction and delete the old anchor.  */
	      emit_insn_before (gen_rtx_PARALLEL
				(VOIDmode,
				 gen_rtvec (2, PATTERN (anchor),
					    PATTERN (floater))),
				anchor);

	      PUT_CODE (anchor, NOTE);
	      NOTE_LINE_NUMBER (anchor) = NOTE_INSN_DELETED;
	      NOTE_SOURCE_FILE (anchor) = 0;

	      /* Emit a special USE insn for FLOATER, then delete
		 the floating insn.  */
	      emit_insn_before (gen_rtx_USE (VOIDmode, floater), floater);
	      delete_insn (floater);

	      continue;
	    }
	  else if (floater
		   && anchor_attr == PA_COMBINE_TYPE_UNCOND_BRANCH)
	    {
	      rtx temp;
	      /* Emit the new_jump instruction and delete the old anchor.  */
	      temp
		= emit_jump_insn_before (gen_rtx_PARALLEL
					 (VOIDmode,
					  gen_rtvec (2, PATTERN (anchor),
						     PATTERN (floater))),
					 anchor);

	      JUMP_LABEL (temp) = JUMP_LABEL (anchor);
	      PUT_CODE (anchor, NOTE);
	      NOTE_LINE_NUMBER (anchor) = NOTE_INSN_DELETED;
	      NOTE_SOURCE_FILE (anchor) = 0;

	      /* Emit a special USE insn for FLOATER, then delete
		 the floating insn.  */
	      emit_insn_before (gen_rtx_USE (VOIDmode, floater), floater);
	      delete_insn (floater);
	      continue;
	    }
	}
    }
}

static int
pa_can_combine_p (rtx new, rtx anchor, rtx floater, int reversed, rtx dest,
		  rtx src1, rtx src2)
{
  int insn_code_number;
  rtx start, end;

  /* Create a PARALLEL with the patterns of ANCHOR and
     FLOATER, try to recognize it, then test constraints
     for the resulting pattern.

     If the pattern doesn't match or the constraints
     aren't met keep searching for a suitable floater
     insn.  */
  XVECEXP (PATTERN (new), 0, 0) = PATTERN (anchor);
  XVECEXP (PATTERN (new), 0, 1) = PATTERN (floater);
  INSN_CODE (new) = -1;
  insn_code_number = recog_memoized (new);
  if (insn_code_number < 0
      || (extract_insn (new), ! constrain_operands (1)))
    return 0;

  if (reversed)
    {
      start = anchor;
      end = floater;
    }
  else
    {
      start = floater;
      end = anchor;
    }

  /* There's up to three operands to consider.  One
     output and two inputs.

     The output must not be used between FLOATER & ANCHOR
     exclusive.  The inputs must not be set between
     FLOATER and ANCHOR exclusive.  */

  if (reg_used_between_p (dest, start, end))
    return 0;

  if (reg_set_between_p (src1, start, end))
    return 0;

  if (reg_set_between_p (src2, start, end))
    return 0;

  /* If we get here, then everything is good.  */
  return 1;
}

/* Return nonzero if references for INSN are delayed.

   Millicode insns are actually function calls with some special
   constraints on arguments and register usage.

   Millicode calls always expect their arguments in the integer argument
   registers, and always return their result in %r29 (ret1).  They
   are expected to clobber their arguments, %r1, %r29, and the return
   pointer which is %r31 on 32-bit and %r2 on 64-bit, and nothing else.

   This function tells reorg that the references to arguments and
   millicode calls do not appear to happen until after the millicode call.
   This allows reorg to put insns which set the argument registers into the
   delay slot of the millicode call -- thus they act more like traditional
   CALL_INSNs.

   Note we cannot consider side effects of the insn to be delayed because
   the branch and link insn will clobber the return pointer.  If we happened
   to use the return pointer in the delay slot of the call, then we lose.

   get_attr_type will try to recognize the given insn, so make sure to
   filter out things it will not accept -- SEQUENCE, USE and CLOBBER insns
   in particular.  */
int
insn_refs_are_delayed (rtx insn)
{
  return ((GET_CODE (insn) == INSN
	   && GET_CODE (PATTERN (insn)) != SEQUENCE
	   && GET_CODE (PATTERN (insn)) != USE
	   && GET_CODE (PATTERN (insn)) != CLOBBER
	   && get_attr_type (insn) == TYPE_MILLI));
}

/* On the HP-PA the value is found in register(s) 28(-29), unless
   the mode is SF or DF. Then the value is returned in fr4 (32).

   This must perform the same promotions as PROMOTE_MODE, else
   TARGET_PROMOTE_FUNCTION_RETURN will not work correctly.

   Small structures must be returned in a PARALLEL on PA64 in order
   to match the HP Compiler ABI.  */

rtx
function_value (tree valtype, tree func ATTRIBUTE_UNUSED)
{
  enum machine_mode valmode;

  if (AGGREGATE_TYPE_P (valtype)
      || TREE_CODE (valtype) == COMPLEX_TYPE
      || TREE_CODE (valtype) == VECTOR_TYPE)
    {
      if (TARGET_64BIT)
	{
          /* Aggregates with a size less than or equal to 128 bits are
	     returned in GR 28(-29).  They are left justified.  The pad
	     bits are undefined.  Larger aggregates are returned in
	     memory.  */
	  rtx loc[2];
	  int i, offset = 0;
	  int ub = int_size_in_bytes (valtype) <= UNITS_PER_WORD ? 1 : 2;

	  for (i = 0; i < ub; i++)
	    {
	      loc[i] = gen_rtx_EXPR_LIST (VOIDmode,
					  gen_rtx_REG (DImode, 28 + i),
					  GEN_INT (offset));
	      offset += 8;
	    }

	  return gen_rtx_PARALLEL (BLKmode, gen_rtvec_v (ub, loc));
	}
      else if (int_size_in_bytes (valtype) > UNITS_PER_WORD)
	{
	  /* Aggregates 5 to 8 bytes in size are returned in general
	     registers r28-r29 in the same manner as other non
	     floating-point objects.  The data is right-justified and
	     zero-extended to 64 bits.  This is opposite to the normal
	     justification used on big endian targets and requires
	     special treatment.  */
	  rtx loc = gen_rtx_EXPR_LIST (VOIDmode,
				       gen_rtx_REG (DImode, 28), const0_rtx);
	  return gen_rtx_PARALLEL (BLKmode, gen_rtvec (1, loc));
	}
    }

  if ((INTEGRAL_TYPE_P (valtype)
       && TYPE_PRECISION (valtype) < BITS_PER_WORD)
      || POINTER_TYPE_P (valtype))
    valmode = word_mode;
  else
    valmode = TYPE_MODE (valtype);

  if (TREE_CODE (valtype) == REAL_TYPE
      && !AGGREGATE_TYPE_P (valtype)
      && TYPE_MODE (valtype) != TFmode
      && !TARGET_SOFT_FLOAT)
    return gen_rtx_REG (valmode, 32);

  return gen_rtx_REG (valmode, 28);
}

/* Return the location of a parameter that is passed in a register or NULL
   if the parameter has any component that is passed in memory.

   This is new code and will be pushed to into the net sources after
   further testing.

   ??? We might want to restructure this so that it looks more like other
   ports.  */
rtx
function_arg (CUMULATIVE_ARGS *cum, enum machine_mode mode, tree type,
	      int named ATTRIBUTE_UNUSED)
{
  int max_arg_words = (TARGET_64BIT ? 8 : 4);
  int alignment = 0;
  int arg_size;
  int fpr_reg_base;
  int gpr_reg_base;
  rtx retval;

  if (mode == VOIDmode)
    return NULL_RTX;

  arg_size = FUNCTION_ARG_SIZE (mode, type);

  /* If this arg would be passed partially or totally on the stack, then
     this routine should return zero.  pa_arg_partial_bytes will
     handle arguments which are split between regs and stack slots if
     the ABI mandates split arguments.  */
  if (!TARGET_64BIT)
    {
      /* The 32-bit ABI does not split arguments.  */
      if (cum->words + arg_size > max_arg_words)
	return NULL_RTX;
    }
  else
    {
      if (arg_size > 1)
	alignment = cum->words & 1;
      if (cum->words + alignment >= max_arg_words)
	return NULL_RTX;
    }

  /* The 32bit ABIs and the 64bit ABIs are rather different,
     particularly in their handling of FP registers.  We might
     be able to cleverly share code between them, but I'm not
     going to bother in the hope that splitting them up results
     in code that is more easily understood.  */

  if (TARGET_64BIT)
    {
      /* Advance the base registers to their current locations.

         Remember, gprs grow towards smaller register numbers while
	 fprs grow to higher register numbers.  Also remember that
	 although FP regs are 32-bit addressable, we pretend that
	 the registers are 64-bits wide.  */
      gpr_reg_base = 26 - cum->words;
      fpr_reg_base = 32 + cum->words;

      /* Arguments wider than one word and small aggregates need special
	 treatment.  */
      if (arg_size > 1
	  || mode == BLKmode
	  || (type && (AGGREGATE_TYPE_P (type)
		       || TREE_CODE (type) == COMPLEX_TYPE
		       || TREE_CODE (type) == VECTOR_TYPE)))
	{
	  /* Double-extended precision (80-bit), quad-precision (128-bit)
	     and aggregates including complex numbers are aligned on
	     128-bit boundaries.  The first eight 64-bit argument slots
	     are associated one-to-one, with general registers r26
	     through r19, and also with floating-point registers fr4
	     through fr11.  Arguments larger than one word are always
	     passed in general registers.

	     Using a PARALLEL with a word mode register results in left
	     justified data on a big-endian target.  */

	  rtx loc[8];
	  int i, offset = 0, ub = arg_size;

	  /* Align the base register.  */
	  gpr_reg_base -= alignment;

	  ub = MIN (ub, max_arg_words - cum->words - alignment);
	  for (i = 0; i < ub; i++)
	    {
	      loc[i] = gen_rtx_EXPR_LIST (VOIDmode,
					  gen_rtx_REG (DImode, gpr_reg_base),
					  GEN_INT (offset));
	      gpr_reg_base -= 1;
	      offset += 8;
	    }

	  return gen_rtx_PARALLEL (mode, gen_rtvec_v (ub, loc));
	}
     }
  else
    {
      /* If the argument is larger than a word, then we know precisely
	 which registers we must use.  */
      if (arg_size > 1)
	{
	  if (cum->words)
	    {
	      gpr_reg_base = 23;
	      fpr_reg_base = 38;
	    }
	  else
	    {
	      gpr_reg_base = 25;
	      fpr_reg_base = 34;
	    }

	  /* Structures 5 to 8 bytes in size are passed in the general
	     registers in the same manner as other non floating-point
	     objects.  The data is right-justified and zero-extended
	     to 64 bits.  This is opposite to the normal justification
	     used on big endian targets and requires special treatment.
	     We now define BLOCK_REG_PADDING to pad these objects.
	     Aggregates, complex and vector types are passed in the same
	     manner as structures.  */
	  if (mode == BLKmode
	      || (type && (AGGREGATE_TYPE_P (type)
			   || TREE_CODE (type) == COMPLEX_TYPE
			   || TREE_CODE (type) == VECTOR_TYPE)))
	    {
	      rtx loc = gen_rtx_EXPR_LIST (VOIDmode,
					   gen_rtx_REG (DImode, gpr_reg_base),
					   const0_rtx);
	      return gen_rtx_PARALLEL (BLKmode, gen_rtvec (1, loc));
	    }
	}
      else
        {
	   /* We have a single word (32 bits).  A simple computation
	      will get us the register #s we need.  */
	   gpr_reg_base = 26 - cum->words;
	   fpr_reg_base = 32 + 2 * cum->words;
	}
    }

  /* Determine if the argument needs to be passed in both general and
     floating point registers.  */
  if (((TARGET_PORTABLE_RUNTIME || TARGET_64BIT || TARGET_ELF32)
       /* If we are doing soft-float with portable runtime, then there
	  is no need to worry about FP regs.  */
       && !TARGET_SOFT_FLOAT
       /* The parameter must be some kind of scalar float, else we just
	  pass it in integer registers.  */
       && GET_MODE_CLASS (mode) == MODE_FLOAT
       /* The target function must not have a prototype.  */
       && cum->nargs_prototype <= 0
       /* libcalls do not need to pass items in both FP and general
	  registers.  */
       && type != NULL_TREE
       /* All this hair applies to "outgoing" args only.  This includes
	  sibcall arguments setup with FUNCTION_INCOMING_ARG.  */
       && !cum->incoming)
      /* Also pass outgoing floating arguments in both registers in indirect
	 calls with the 32 bit ABI and the HP assembler since there is no
	 way to the specify argument locations in static functions.  */
      || (!TARGET_64BIT
	  && !TARGET_GAS
	  && !cum->incoming
	  && cum->indirect
	  && GET_MODE_CLASS (mode) == MODE_FLOAT))
    {
      retval
	= gen_rtx_PARALLEL
	    (mode,
	     gen_rtvec (2,
			gen_rtx_EXPR_LIST (VOIDmode,
					   gen_rtx_REG (mode, fpr_reg_base),
					   const0_rtx),
			gen_rtx_EXPR_LIST (VOIDmode,
					   gen_rtx_REG (mode, gpr_reg_base),
					   const0_rtx)));
    }
  else
    {
      /* See if we should pass this parameter in a general register.  */
      if (TARGET_SOFT_FLOAT
	  /* Indirect calls in the normal 32bit ABI require all arguments
	     to be passed in general registers.  */
	  || (!TARGET_PORTABLE_RUNTIME
	      && !TARGET_64BIT
	      && !TARGET_ELF32
	      && cum->indirect)
	  /* If the parameter is not a scalar floating-point parameter,
	     then it belongs in GPRs.  */
	  || GET_MODE_CLASS (mode) != MODE_FLOAT
	  /* Structure with single SFmode field belongs in GPR.  */
	  || (type && AGGREGATE_TYPE_P (type)))
	retval = gen_rtx_REG (mode, gpr_reg_base);
      else
	retval = gen_rtx_REG (mode, fpr_reg_base);
    }
  return retval;
}


/* If this arg would be passed totally in registers or totally on the stack,
   then this routine should return zero.  */

static int
pa_arg_partial_bytes (CUMULATIVE_ARGS *cum, enum machine_mode mode,
		      tree type, bool named ATTRIBUTE_UNUSED)
{
  unsigned int max_arg_words = 8;
  unsigned int offset = 0;

  if (!TARGET_64BIT)
    return 0;

  if (FUNCTION_ARG_SIZE (mode, type) > 1 && (cum->words & 1))
    offset = 1;

  if (cum->words + offset + FUNCTION_ARG_SIZE (mode, type) <= max_arg_words)
    /* Arg fits fully into registers.  */
    return 0;
  else if (cum->words + offset >= max_arg_words)
    /* Arg fully on the stack.  */
    return 0;
  else
    /* Arg is split.  */
    return (max_arg_words - cum->words - offset) * UNITS_PER_WORD;
}


/* A get_unnamed_section callback for switching to the text section.

   This function is only used with SOM.  Because we don't support
   named subspaces, we can only create a new subspace or switch back
   to the default text subspace.  */

static void
som_output_text_section_asm_op (const void *data ATTRIBUTE_UNUSED)
{
  gcc_assert (TARGET_SOM);
  if (TARGET_GAS)
    {
      if (cfun && cfun->machine && !cfun->machine->in_nsubspa)
	{
	  /* We only want to emit a .nsubspa directive once at the
	     start of the function.  */
	  cfun->machine->in_nsubspa = 1;

	  /* Create a new subspace for the text.  This provides
	     better stub placement and one-only functions.  */
	  if (cfun->decl
	      && DECL_ONE_ONLY (cfun->decl)
	      && !DECL_WEAK (cfun->decl))
	    {
	      output_section_asm_op ("\t.SPACE $TEXT$\n"
				     "\t.NSUBSPA $CODE$,QUAD=0,ALIGN=8,"
				     "ACCESS=44,SORT=24,COMDAT");
	      return;
	    }
	}
      else
	{
	  /* There isn't a current function or the body of the current
	     function has been completed.  So, we are changing to the
	     text section to output debugging information.  Thus, we
	     need to forget that we are in the text section so that
	     varasm.c will call us when text_section is selected again.  */
	  gcc_assert (!cfun || !cfun->machine
		      || cfun->machine->in_nsubspa == 2);
	  in_section = NULL;
	}
      output_section_asm_op ("\t.SPACE $TEXT$\n\t.NSUBSPA $CODE$");
      return;
    }
  output_section_asm_op ("\t.SPACE $TEXT$\n\t.SUBSPA $CODE$");
}

/* A get_unnamed_section callback for switching to comdat data
   sections.  This function is only used with SOM.  */

static void
som_output_comdat_data_section_asm_op (const void *data)
{
  in_section = NULL;
  output_section_asm_op (data);
}

/* Implement TARGET_ASM_INITIALIZE_SECTIONS  */

static void
pa_som_asm_init_sections (void)
{
  text_section
    = get_unnamed_section (0, som_output_text_section_asm_op, NULL);

  /* SOM puts readonly data in the default $LIT$ subspace when PIC code
     is not being generated.  */
  som_readonly_data_section
    = get_unnamed_section (0, output_section_asm_op,
			   "\t.SPACE $TEXT$\n\t.SUBSPA $LIT$");

  /* When secondary definitions are not supported, SOM makes readonly
     data one-only by creating a new $LIT$ subspace in $TEXT$ with
     the comdat flag.  */
  som_one_only_readonly_data_section
    = get_unnamed_section (0, som_output_comdat_data_section_asm_op,
			   "\t.SPACE $TEXT$\n"
			   "\t.NSUBSPA $LIT$,QUAD=0,ALIGN=8,"
			   "ACCESS=0x2c,SORT=16,COMDAT");


  /* When secondary definitions are not supported, SOM makes data one-only
     by creating a new $DATA$ subspace in $PRIVATE$ with the comdat flag.  */
  som_one_only_data_section
    = get_unnamed_section (SECTION_WRITE,
			   som_output_comdat_data_section_asm_op,
			   "\t.SPACE $PRIVATE$\n"
			   "\t.NSUBSPA $DATA$,QUAD=1,ALIGN=8,"
			   "ACCESS=31,SORT=24,COMDAT");

  /* FIXME: HPUX ld generates incorrect GOT entries for "T" fixups
     which reference data within the $TEXT$ space (for example constant
     strings in the $LIT$ subspace).

     The assemblers (GAS and HP as) both have problems with handling
     the difference of two symbols which is the other correct way to
     reference constant data during PIC code generation.

     So, there's no way to reference constant data which is in the
     $TEXT$ space during PIC generation.  Instead place all constant
     data into the $PRIVATE$ subspace (this reduces sharing, but it
     works correctly).  */
  readonly_data_section = flag_pic ? data_section : som_readonly_data_section;

  /* We must not have a reference to an external symbol defined in a
     shared library in a readonly section, else the SOM linker will
     complain.

     So, we force exception information into the data section.  */
  exception_section = data_section;
}

/* On hpux10, the linker will give an error if we have a reference
   in the read-only data section to a symbol defined in a shared
   library.  Therefore, expressions that might require a reloc can
   not be placed in the read-only data section.  */

static section *
pa_select_section (tree exp, int reloc,
		   unsigned HOST_WIDE_INT align ATTRIBUTE_UNUSED)
{
  if (TREE_CODE (exp) == VAR_DECL
      && TREE_READONLY (exp)
      && !TREE_THIS_VOLATILE (exp)
      && DECL_INITIAL (exp)
      && (DECL_INITIAL (exp) == error_mark_node
          || TREE_CONSTANT (DECL_INITIAL (exp)))
      && !reloc)
    {
      if (TARGET_SOM
	  && DECL_ONE_ONLY (exp)
	  && !DECL_WEAK (exp))
	return som_one_only_readonly_data_section;
      else
	return readonly_data_section;
    }
  else if (CONSTANT_CLASS_P (exp) && !reloc)
    return readonly_data_section;
  else if (TARGET_SOM
	   && TREE_CODE (exp) == VAR_DECL
	   && DECL_ONE_ONLY (exp)
	   && !DECL_WEAK (exp))
    return som_one_only_data_section;
  else
    return data_section;
}

static void
pa_globalize_label (FILE *stream, const char *name)
{
  /* We only handle DATA objects here, functions are globalized in
     ASM_DECLARE_FUNCTION_NAME.  */
  if (! FUNCTION_NAME_P (name))
  {
    fputs ("\t.EXPORT ", stream);
    assemble_name (stream, name);
    fputs (",DATA\n", stream);
  }
}

/* Worker function for TARGET_STRUCT_VALUE_RTX.  */

static rtx
pa_struct_value_rtx (tree fntype ATTRIBUTE_UNUSED,
		     int incoming ATTRIBUTE_UNUSED)
{
  return gen_rtx_REG (Pmode, PA_STRUCT_VALUE_REGNUM);
}

/* Worker function for TARGET_RETURN_IN_MEMORY.  */

bool
pa_return_in_memory (tree type, tree fntype ATTRIBUTE_UNUSED)
{
  /* SOM ABI says that objects larger than 64 bits are returned in memory.
     PA64 ABI says that objects larger than 128 bits are returned in memory.
     Note, int_size_in_bytes can return -1 if the size of the object is
     variable or larger than the maximum value that can be expressed as
     a HOST_WIDE_INT.   It can also return zero for an empty type.  The
     simplest way to handle variable and empty types is to pass them in
     memory.  This avoids problems in defining the boundaries of argument
     slots, allocating registers, etc.  */
  return (int_size_in_bytes (type) > (TARGET_64BIT ? 16 : 8)
	  || int_size_in_bytes (type) <= 0);
}

/* Structure to hold declaration and name of external symbols that are
   emitted by GCC.  We generate a vector of these symbols and output them
   at the end of the file if and only if SYMBOL_REF_REFERENCED_P is true.
   This avoids putting out names that are never really used.  */

typedef struct extern_symbol GTY(())
{
  tree decl;
  const char *name;
} extern_symbol;

/* Define gc'd vector type for extern_symbol.  */
DEF_VEC_O(extern_symbol);
DEF_VEC_ALLOC_O(extern_symbol,gc);

/* Vector of extern_symbol pointers.  */
static GTY(()) VEC(extern_symbol,gc) *extern_symbols;

#ifdef ASM_OUTPUT_EXTERNAL_REAL
/* Mark DECL (name NAME) as an external reference (assembler output
   file FILE).  This saves the names to output at the end of the file
   if actually referenced.  */

void
pa_hpux_asm_output_external (FILE *file, tree decl, const char *name)
{
  extern_symbol * p = VEC_safe_push (extern_symbol, gc, extern_symbols, NULL);

  gcc_assert (file == asm_out_file);
  p->decl = decl;
  p->name = name;
}

/* Output text required at the end of an assembler file.
   This includes deferred plabels and .import directives for
   all external symbols that were actually referenced.  */

static void
pa_hpux_file_end (void)
{
  unsigned int i;
  extern_symbol *p;

  if (!NO_DEFERRED_PROFILE_COUNTERS)
    output_deferred_profile_counters ();

  output_deferred_plabels ();

  for (i = 0; VEC_iterate (extern_symbol, extern_symbols, i, p); i++)
    {
      tree decl = p->decl;

      if (!TREE_ASM_WRITTEN (decl)
	  && SYMBOL_REF_REFERENCED_P (XEXP (DECL_RTL (decl), 0)))
	ASM_OUTPUT_EXTERNAL_REAL (asm_out_file, decl, p->name);
    }

  VEC_free (extern_symbol, gc, extern_symbols);
}
#endif

#include "gt-pa.h"
