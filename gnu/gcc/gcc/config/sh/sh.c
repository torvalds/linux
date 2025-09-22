/* Output routines for GCC for Renesas / SuperH SH.
   Copyright (C) 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002,
   2003, 2004, 2005, 2006 Free Software Foundation, Inc.
   Contributed by Steve Chamberlain (sac@cygnus.com).
   Improved by Jim Wilson (wilson@cygnus.com).

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
#include "insn-config.h"
#include "rtl.h"
#include "tree.h"
#include "flags.h"
#include "expr.h"
#include "optabs.h"
#include "function.h"
#include "regs.h"
#include "hard-reg-set.h"
#include "output.h"
#include "insn-attr.h"
#include "toplev.h"
#include "recog.h"
#include "c-pragma.h"
#include "integrate.h"
#include "dwarf2.h"
#include "tm_p.h"
#include "target.h"
#include "target-def.h"
#include "real.h"
#include "langhooks.h"
#include "basic-block.h"
#include "cfglayout.h"
#include "intl.h"
#include "sched-int.h"
#include "ggc.h"
#include "tree-gimple.h"
#include "cfgloop.h"
#include "alloc-pool.h"


int code_for_indirect_jump_scratch = CODE_FOR_indirect_jump_scratch;

#define MSW (TARGET_LITTLE_ENDIAN ? 1 : 0)
#define LSW (TARGET_LITTLE_ENDIAN ? 0 : 1)

/* These are some macros to abstract register modes.  */
#define CONST_OK_FOR_ADD(size) \
  (TARGET_SHMEDIA ? CONST_OK_FOR_I10 (size) : CONST_OK_FOR_I08 (size))
#define GEN_MOV (*(TARGET_SHMEDIA64 ? gen_movdi : gen_movsi))
#define GEN_ADD3 (*(TARGET_SHMEDIA64 ? gen_adddi3 : gen_addsi3))
#define GEN_SUB3 (*(TARGET_SHMEDIA64 ? gen_subdi3 : gen_subsi3))

/* Set to 1 by expand_prologue() when the function is an interrupt handler.  */
int current_function_interrupt;

tree sh_deferred_function_attributes;
tree *sh_deferred_function_attributes_tail = &sh_deferred_function_attributes;

/* Global variables for machine-dependent things.  */

/* Which cpu are we scheduling for.  */
enum processor_type sh_cpu;

/* Definitions used in ready queue reordering for first scheduling pass.  */

/* Reg weights arrays for modes SFmode and SImode, indexed by insn LUID.  */
static short *regmode_weight[2];

/* Total SFmode and SImode weights of scheduled insns.  */
static int curr_regmode_pressure[2];

/* If true, skip cycles for Q -> R movement.  */
static int skip_cycles = 0;

/* Cached value of can_issue_more. This is cached in sh_variable_issue hook
   and returned from sh_reorder2.  */
static short cached_can_issue_more;

/* Saved operands from the last compare to use when we generate an scc
   or bcc insn.  */

rtx sh_compare_op0;
rtx sh_compare_op1;

/* Provides the class number of the smallest class containing
   reg number.  */

enum reg_class regno_reg_class[FIRST_PSEUDO_REGISTER] =
{
  R0_REGS, GENERAL_REGS, GENERAL_REGS, GENERAL_REGS,
  GENERAL_REGS, GENERAL_REGS, GENERAL_REGS, GENERAL_REGS,
  GENERAL_REGS, GENERAL_REGS, GENERAL_REGS, GENERAL_REGS,
  GENERAL_REGS, GENERAL_REGS, GENERAL_REGS, GENERAL_REGS,
  GENERAL_REGS, GENERAL_REGS, GENERAL_REGS, GENERAL_REGS,
  GENERAL_REGS, GENERAL_REGS, GENERAL_REGS, GENERAL_REGS,
  GENERAL_REGS, GENERAL_REGS, GENERAL_REGS, GENERAL_REGS,
  GENERAL_REGS, GENERAL_REGS, GENERAL_REGS, GENERAL_REGS,
  GENERAL_REGS, GENERAL_REGS, GENERAL_REGS, GENERAL_REGS,
  GENERAL_REGS, GENERAL_REGS, GENERAL_REGS, GENERAL_REGS,
  GENERAL_REGS, GENERAL_REGS, GENERAL_REGS, GENERAL_REGS,
  GENERAL_REGS, GENERAL_REGS, GENERAL_REGS, GENERAL_REGS,
  GENERAL_REGS, GENERAL_REGS, GENERAL_REGS, GENERAL_REGS,
  GENERAL_REGS, GENERAL_REGS, GENERAL_REGS, GENERAL_REGS,
  GENERAL_REGS, GENERAL_REGS, GENERAL_REGS, GENERAL_REGS,
  GENERAL_REGS, GENERAL_REGS, GENERAL_REGS, GENERAL_REGS,
  FP0_REGS,FP_REGS, FP_REGS, FP_REGS,
  FP_REGS, FP_REGS, FP_REGS, FP_REGS,
  FP_REGS, FP_REGS, FP_REGS, FP_REGS,
  FP_REGS, FP_REGS, FP_REGS, FP_REGS,
  FP_REGS, FP_REGS, FP_REGS, FP_REGS,
  FP_REGS, FP_REGS, FP_REGS, FP_REGS,
  FP_REGS, FP_REGS, FP_REGS, FP_REGS,
  FP_REGS, FP_REGS, FP_REGS, FP_REGS,
  FP_REGS, FP_REGS, FP_REGS, FP_REGS,
  FP_REGS, FP_REGS, FP_REGS, FP_REGS,
  FP_REGS, FP_REGS, FP_REGS, FP_REGS,
  FP_REGS, FP_REGS, FP_REGS, FP_REGS,
  FP_REGS, FP_REGS, FP_REGS, FP_REGS,
  FP_REGS, FP_REGS, FP_REGS, FP_REGS,
  FP_REGS, FP_REGS, FP_REGS, FP_REGS,
  FP_REGS, FP_REGS, FP_REGS, FP_REGS,
  TARGET_REGS, TARGET_REGS, TARGET_REGS, TARGET_REGS,
  TARGET_REGS, TARGET_REGS, TARGET_REGS, TARGET_REGS,
  DF_REGS, DF_REGS, DF_REGS, DF_REGS,
  DF_REGS, DF_REGS, DF_REGS, DF_REGS,
  NO_REGS, GENERAL_REGS, PR_REGS, T_REGS,
  MAC_REGS, MAC_REGS, FPUL_REGS, FPSCR_REGS,
  GENERAL_REGS, GENERAL_REGS,
};

char sh_register_names[FIRST_PSEUDO_REGISTER] \
  [MAX_REGISTER_NAME_LENGTH + 1] = SH_REGISTER_NAMES_INITIALIZER;

char sh_additional_register_names[ADDREGNAMES_SIZE] \
  [MAX_ADDITIONAL_REGISTER_NAME_LENGTH + 1]
  = SH_ADDITIONAL_REGISTER_NAMES_INITIALIZER;

/* Provide reg_class from a letter such as appears in the machine
   description.  *: target independently reserved letter.
   reg_class_from_letter['e' - 'a'] is set to NO_REGS for TARGET_FMOVD.  */

enum reg_class reg_class_from_letter[] =
{
  /* a */ ALL_REGS,  /* b */ TARGET_REGS, /* c */ FPSCR_REGS, /* d */ DF_REGS,
  /* e */ FP_REGS,   /* f */ FP_REGS,  /* g **/ NO_REGS,     /* h */ NO_REGS,
  /* i **/ NO_REGS,  /* j */ NO_REGS,  /* k */ SIBCALL_REGS, /* l */ PR_REGS,
  /* m **/ NO_REGS,  /* n **/ NO_REGS, /* o **/ NO_REGS,     /* p **/ NO_REGS,
  /* q */ NO_REGS,   /* r **/ NO_REGS, /* s **/ NO_REGS,     /* t */ T_REGS,
  /* u */ NO_REGS,   /* v */ NO_REGS,  /* w */ FP0_REGS,     /* x */ MAC_REGS,
  /* y */ FPUL_REGS, /* z */ R0_REGS
};

int assembler_dialect;

static bool shmedia_space_reserved_for_target_registers;

static bool sh_handle_option (size_t, const char *, int);
static void split_branches (rtx);
static int branch_dest (rtx);
static void force_into (rtx, rtx);
static void print_slot (rtx);
static rtx add_constant (rtx, enum machine_mode, rtx);
static void dump_table (rtx, rtx);
static int hi_const (rtx);
static int broken_move (rtx);
static int mova_p (rtx);
static rtx find_barrier (int, rtx, rtx);
static int noncall_uses_reg (rtx, rtx, rtx *);
static rtx gen_block_redirect (rtx, int, int);
static void sh_reorg (void);
static void output_stack_adjust (int, rtx, int, HARD_REG_SET *);
static rtx frame_insn (rtx);
static rtx push (int);
static void pop (int);
static void push_regs (HARD_REG_SET *, int);
static int calc_live_regs (HARD_REG_SET *);
static void mark_use (rtx, rtx *);
static HOST_WIDE_INT rounded_frame_size (int);
static rtx mark_constant_pool_use (rtx);
const struct attribute_spec sh_attribute_table[];
static tree sh_handle_interrupt_handler_attribute (tree *, tree, tree, int, bool *);
static tree sh_handle_sp_switch_attribute (tree *, tree, tree, int, bool *);
static tree sh_handle_trap_exit_attribute (tree *, tree, tree, int, bool *);
static tree sh_handle_renesas_attribute (tree *, tree, tree, int, bool *);
static void sh_output_function_epilogue (FILE *, HOST_WIDE_INT);
static void sh_insert_attributes (tree, tree *);
static const char *sh_check_pch_target_flags (int);
static int sh_adjust_cost (rtx, rtx, rtx, int);
static int sh_issue_rate (void);
static int sh_dfa_new_cycle (FILE *, int, rtx, int, int, int *sort_p);
static short find_set_regmode_weight (rtx, enum machine_mode);
static short find_insn_regmode_weight (rtx, enum machine_mode);
static void find_regmode_weight (basic_block, enum machine_mode);
static void  sh_md_init_global (FILE *, int, int);
static void  sh_md_finish_global (FILE *, int);
static int rank_for_reorder (const void *, const void *);
static void swap_reorder (rtx *, int);
static void ready_reorder (rtx *, int);
static short high_pressure (enum machine_mode);
static int sh_reorder (FILE *, int, rtx *, int *, int);
static int sh_reorder2 (FILE *, int, rtx *, int *, int);
static void sh_md_init (FILE *, int, int);
static int sh_variable_issue (FILE *, int, rtx, int);

static bool sh_function_ok_for_sibcall (tree, tree);

static bool sh_cannot_modify_jumps_p (void);
static int sh_target_reg_class (void);
static bool sh_optimize_target_register_callee_saved (bool);
static bool sh_ms_bitfield_layout_p (tree);

static void sh_init_builtins (void);
static void sh_media_init_builtins (void);
static rtx sh_expand_builtin (tree, rtx, rtx, enum machine_mode, int);
static void sh_output_mi_thunk (FILE *, tree, HOST_WIDE_INT, HOST_WIDE_INT, tree);
static void sh_file_start (void);
static int flow_dependent_p (rtx, rtx);
static void flow_dependent_p_1 (rtx, rtx, void *);
static int shiftcosts (rtx);
static int andcosts (rtx);
static int addsubcosts (rtx);
static int multcosts (rtx);
static bool unspec_caller_rtx_p (rtx);
static bool sh_cannot_copy_insn_p (rtx);
static bool sh_rtx_costs (rtx, int, int, int *);
static int sh_address_cost (rtx);
#ifdef TARGET_ADJUST_UNROLL_MAX
static int sh_adjust_unroll_max (struct loop *, int, int, int, int);
#endif
static int sh_pr_n_sets (void);
static rtx sh_allocate_initial_value (rtx);
static int shmedia_target_regs_stack_space (HARD_REG_SET *);
static int shmedia_reserve_space_for_target_registers_p (int, HARD_REG_SET *);
static int shmedia_target_regs_stack_adjust (HARD_REG_SET *);
static int scavenge_reg (HARD_REG_SET *s);
struct save_schedule_s;
static struct save_entry_s *sh5_schedule_saves (HARD_REG_SET *,
						struct save_schedule_s *, int);

static rtx sh_struct_value_rtx (tree, int);
static bool sh_return_in_memory (tree, tree);
static rtx sh_builtin_saveregs (void);
static void sh_setup_incoming_varargs (CUMULATIVE_ARGS *, enum machine_mode, tree, int *, int);
static bool sh_strict_argument_naming (CUMULATIVE_ARGS *);
static bool sh_pretend_outgoing_varargs_named (CUMULATIVE_ARGS *);
static tree sh_build_builtin_va_list (void);
static tree sh_gimplify_va_arg_expr (tree, tree, tree *, tree *);
static bool sh_pass_by_reference (CUMULATIVE_ARGS *, enum machine_mode,
				  tree, bool);
static bool sh_callee_copies (CUMULATIVE_ARGS *, enum machine_mode,
			      tree, bool);
static int sh_arg_partial_bytes (CUMULATIVE_ARGS *, enum machine_mode,
			         tree, bool);
static int sh_dwarf_calling_convention (tree);
static int hard_regs_intersect_p (HARD_REG_SET *, HARD_REG_SET *);


/* Initialize the GCC target structure.  */
#undef TARGET_ATTRIBUTE_TABLE
#define TARGET_ATTRIBUTE_TABLE sh_attribute_table

/* The next two are used for debug info when compiling with -gdwarf.  */
#undef TARGET_ASM_UNALIGNED_HI_OP
#define TARGET_ASM_UNALIGNED_HI_OP "\t.uaword\t"
#undef TARGET_ASM_UNALIGNED_SI_OP
#define TARGET_ASM_UNALIGNED_SI_OP "\t.ualong\t"

/* These are NULLed out on non-SH5 in OVERRIDE_OPTIONS.  */
#undef TARGET_ASM_UNALIGNED_DI_OP
#define TARGET_ASM_UNALIGNED_DI_OP "\t.uaquad\t"
#undef TARGET_ASM_ALIGNED_DI_OP
#define TARGET_ASM_ALIGNED_DI_OP "\t.quad\t"

#undef TARGET_ASM_FUNCTION_EPILOGUE
#define TARGET_ASM_FUNCTION_EPILOGUE sh_output_function_epilogue

#undef TARGET_ASM_OUTPUT_MI_THUNK
#define TARGET_ASM_OUTPUT_MI_THUNK sh_output_mi_thunk

#undef TARGET_ASM_CAN_OUTPUT_MI_THUNK
#define TARGET_ASM_CAN_OUTPUT_MI_THUNK hook_bool_tree_hwi_hwi_tree_true

#undef TARGET_ASM_FILE_START
#define TARGET_ASM_FILE_START sh_file_start
#undef TARGET_ASM_FILE_START_FILE_DIRECTIVE
#define TARGET_ASM_FILE_START_FILE_DIRECTIVE true

#undef TARGET_DEFAULT_TARGET_FLAGS
#define TARGET_DEFAULT_TARGET_FLAGS TARGET_DEFAULT
#undef TARGET_HANDLE_OPTION
#define TARGET_HANDLE_OPTION sh_handle_option

#undef TARGET_INSERT_ATTRIBUTES
#define TARGET_INSERT_ATTRIBUTES sh_insert_attributes

#undef TARGET_SCHED_ADJUST_COST
#define TARGET_SCHED_ADJUST_COST sh_adjust_cost

#undef TARGET_SCHED_ISSUE_RATE
#define TARGET_SCHED_ISSUE_RATE sh_issue_rate

/* The next 5 hooks have been implemented for reenabling sched1.  With the
   help of these macros we are limiting the movement of insns in sched1 to
   reduce the register pressure.  The overall idea is to keep count of SImode
   and SFmode regs required by already scheduled insns. When these counts
   cross some threshold values; give priority to insns that free registers.
   The insn that frees registers is most likely to be the insn with lowest
   LUID (original insn order); but such an insn might be there in the stalled
   queue (Q) instead of the ready queue (R).  To solve this, we skip cycles
   upto a max of 8 cycles so that such insns may move from Q -> R.

   The description of the hooks are as below:

   TARGET_SCHED_INIT_GLOBAL: Added a new target hook in the generic
   scheduler; it is called inside the sched_init function just after
   find_insn_reg_weights function call. It is used to calculate the SImode
   and SFmode weights of insns of basic blocks; much similar to what
   find_insn_reg_weights does.
   TARGET_SCHED_FINISH_GLOBAL: Corresponding cleanup hook.

   TARGET_SCHED_DFA_NEW_CYCLE: Skip cycles if high register pressure is
   indicated by TARGET_SCHED_REORDER2; doing this may move insns from
   (Q)->(R).

   TARGET_SCHED_REORDER: If the register pressure for SImode or SFmode is
   high; reorder the ready queue so that the insn with lowest LUID will be
   issued next.

   TARGET_SCHED_REORDER2: If the register pressure is high, indicate to
   TARGET_SCHED_DFA_NEW_CYCLE to skip cycles.

   TARGET_SCHED_VARIABLE_ISSUE: Cache the value of can_issue_more so that it
   can be returned from TARGET_SCHED_REORDER2.

   TARGET_SCHED_INIT: Reset the register pressure counting variables.  */

#undef TARGET_SCHED_DFA_NEW_CYCLE
#define TARGET_SCHED_DFA_NEW_CYCLE sh_dfa_new_cycle

#undef TARGET_SCHED_INIT_GLOBAL
#define TARGET_SCHED_INIT_GLOBAL sh_md_init_global

#undef TARGET_SCHED_FINISH_GLOBAL
#define TARGET_SCHED_FINISH_GLOBAL sh_md_finish_global

#undef TARGET_SCHED_VARIABLE_ISSUE
#define TARGET_SCHED_VARIABLE_ISSUE sh_variable_issue

#undef TARGET_SCHED_REORDER
#define TARGET_SCHED_REORDER sh_reorder

#undef TARGET_SCHED_REORDER2
#define TARGET_SCHED_REORDER2 sh_reorder2

#undef TARGET_SCHED_INIT
#define TARGET_SCHED_INIT sh_md_init

#undef TARGET_CANNOT_MODIFY_JUMPS_P
#define TARGET_CANNOT_MODIFY_JUMPS_P sh_cannot_modify_jumps_p
#undef TARGET_BRANCH_TARGET_REGISTER_CLASS
#define TARGET_BRANCH_TARGET_REGISTER_CLASS sh_target_reg_class
#undef TARGET_BRANCH_TARGET_REGISTER_CALLEE_SAVED
#define TARGET_BRANCH_TARGET_REGISTER_CALLEE_SAVED \
 sh_optimize_target_register_callee_saved

#undef TARGET_MS_BITFIELD_LAYOUT_P
#define TARGET_MS_BITFIELD_LAYOUT_P sh_ms_bitfield_layout_p

#undef TARGET_INIT_BUILTINS
#define TARGET_INIT_BUILTINS sh_init_builtins
#undef TARGET_EXPAND_BUILTIN
#define TARGET_EXPAND_BUILTIN sh_expand_builtin

#undef TARGET_FUNCTION_OK_FOR_SIBCALL
#define TARGET_FUNCTION_OK_FOR_SIBCALL sh_function_ok_for_sibcall

#undef TARGET_CANNOT_COPY_INSN_P
#define TARGET_CANNOT_COPY_INSN_P sh_cannot_copy_insn_p
#undef TARGET_RTX_COSTS
#define TARGET_RTX_COSTS sh_rtx_costs
#undef TARGET_ADDRESS_COST
#define TARGET_ADDRESS_COST sh_address_cost
#undef TARGET_ALLOCATE_INITIAL_VALUE
#define TARGET_ALLOCATE_INITIAL_VALUE sh_allocate_initial_value

#undef TARGET_MACHINE_DEPENDENT_REORG
#define TARGET_MACHINE_DEPENDENT_REORG sh_reorg

#ifdef HAVE_AS_TLS
#undef TARGET_HAVE_TLS
#define TARGET_HAVE_TLS true
#endif

#undef TARGET_PROMOTE_PROTOTYPES
#define TARGET_PROMOTE_PROTOTYPES sh_promote_prototypes
#undef TARGET_PROMOTE_FUNCTION_ARGS
#define TARGET_PROMOTE_FUNCTION_ARGS sh_promote_prototypes
#undef TARGET_PROMOTE_FUNCTION_RETURN
#define TARGET_PROMOTE_FUNCTION_RETURN sh_promote_prototypes

#undef TARGET_STRUCT_VALUE_RTX
#define TARGET_STRUCT_VALUE_RTX sh_struct_value_rtx
#undef TARGET_RETURN_IN_MEMORY
#define TARGET_RETURN_IN_MEMORY sh_return_in_memory

#undef TARGET_EXPAND_BUILTIN_SAVEREGS
#define TARGET_EXPAND_BUILTIN_SAVEREGS sh_builtin_saveregs
#undef TARGET_SETUP_INCOMING_VARARGS
#define TARGET_SETUP_INCOMING_VARARGS sh_setup_incoming_varargs
#undef TARGET_STRICT_ARGUMENT_NAMING
#define TARGET_STRICT_ARGUMENT_NAMING sh_strict_argument_naming
#undef TARGET_PRETEND_OUTGOING_VARARGS_NAMED
#define TARGET_PRETEND_OUTGOING_VARARGS_NAMED sh_pretend_outgoing_varargs_named
#undef TARGET_MUST_PASS_IN_STACK
#define TARGET_MUST_PASS_IN_STACK must_pass_in_stack_var_size
#undef TARGET_PASS_BY_REFERENCE
#define TARGET_PASS_BY_REFERENCE sh_pass_by_reference
#undef TARGET_CALLEE_COPIES
#define TARGET_CALLEE_COPIES sh_callee_copies
#undef TARGET_ARG_PARTIAL_BYTES
#define TARGET_ARG_PARTIAL_BYTES sh_arg_partial_bytes

#undef TARGET_BUILD_BUILTIN_VA_LIST
#define TARGET_BUILD_BUILTIN_VA_LIST sh_build_builtin_va_list
#undef TARGET_GIMPLIFY_VA_ARG_EXPR
#define TARGET_GIMPLIFY_VA_ARG_EXPR sh_gimplify_va_arg_expr

#undef TARGET_VECTOR_MODE_SUPPORTED_P
#define TARGET_VECTOR_MODE_SUPPORTED_P sh_vector_mode_supported_p

#undef TARGET_CHECK_PCH_TARGET_FLAGS
#define TARGET_CHECK_PCH_TARGET_FLAGS sh_check_pch_target_flags

#undef TARGET_DWARF_CALLING_CONVENTION
#define TARGET_DWARF_CALLING_CONVENTION sh_dwarf_calling_convention

/* Return regmode weight for insn.  */
#define INSN_REGMODE_WEIGHT(INSN, MODE)  regmode_weight[((MODE) == SImode) ? 0 : 1][INSN_UID (INSN)]

/* Return current register pressure for regmode.  */
#define CURR_REGMODE_PRESSURE(MODE) 	curr_regmode_pressure[((MODE) == SImode) ? 0 : 1]

#ifdef SYMBIAN

#undef  TARGET_ENCODE_SECTION_INFO
#define TARGET_ENCODE_SECTION_INFO	sh_symbian_encode_section_info
#undef  TARGET_STRIP_NAME_ENCODING
#define TARGET_STRIP_NAME_ENCODING	sh_symbian_strip_name_encoding
#undef  TARGET_CXX_IMPORT_EXPORT_CLASS
#define TARGET_CXX_IMPORT_EXPORT_CLASS  symbian_import_export_class

#endif /* SYMBIAN */

#ifdef TARGET_ADJUST_UNROLL_MAX
#undef TARGET_ADJUST_UNROLL_MAX
#define TARGET_ADJUST_UNROLL_MAX sh_adjust_unroll_max
#endif

#undef TARGET_SECONDARY_RELOAD
#define TARGET_SECONDARY_RELOAD sh_secondary_reload

struct gcc_target targetm = TARGET_INITIALIZER;

/* Implement TARGET_HANDLE_OPTION.  */

static bool
sh_handle_option (size_t code, const char *arg ATTRIBUTE_UNUSED,
		  int value ATTRIBUTE_UNUSED)
{
  switch (code)
    {
    case OPT_m1:
      target_flags = (target_flags & ~MASK_ARCH) | SELECT_SH1;
      return true;

    case OPT_m2:
      target_flags = (target_flags & ~MASK_ARCH) | SELECT_SH2;
      return true;

    case OPT_m2a:
      target_flags = (target_flags & ~MASK_ARCH) | SELECT_SH2A;
      return true;

    case OPT_m2a_nofpu:
      target_flags = (target_flags & ~MASK_ARCH) | SELECT_SH2A_NOFPU;
      return true;

    case OPT_m2a_single:
      target_flags = (target_flags & ~MASK_ARCH) | SELECT_SH2A_SINGLE;
      return true;

    case OPT_m2a_single_only:
      target_flags = (target_flags & ~MASK_ARCH) | SELECT_SH2A_SINGLE_ONLY;
      return true;

    case OPT_m2e:
      target_flags = (target_flags & ~MASK_ARCH) | SELECT_SH2E;
      return true;

    case OPT_m3:
      target_flags = (target_flags & ~MASK_ARCH) | SELECT_SH3;
      return true;

    case OPT_m3e:
      target_flags = (target_flags & ~MASK_ARCH) | SELECT_SH3E;
      return true;

    case OPT_m4:
    case OPT_m4_100:
    case OPT_m4_200:
      target_flags = (target_flags & ~MASK_ARCH) | SELECT_SH4;
      return true;

    case OPT_m4_nofpu:
    case OPT_m4_400:
    case OPT_m4_500:
      target_flags = (target_flags & ~MASK_ARCH) | SELECT_SH4_NOFPU;
      return true;

    case OPT_m4_single:
    case OPT_m4_100_single:
    case OPT_m4_200_single:
      target_flags = (target_flags & ~MASK_ARCH) | SELECT_SH4_SINGLE;
      return true;

    case OPT_m4_single_only:
    case OPT_m4_100_single_only:
    case OPT_m4_200_single_only:
      target_flags = (target_flags & ~MASK_ARCH) | SELECT_SH4_SINGLE_ONLY;
      return true;

    case OPT_m4a:
      target_flags = (target_flags & ~MASK_ARCH) | SELECT_SH4A;
      return true;

    case OPT_m4a_nofpu:
    case OPT_m4al:
      target_flags = (target_flags & ~MASK_ARCH) | SELECT_SH4A_NOFPU;
      return true;

    case OPT_m4a_single:
      target_flags = (target_flags & ~MASK_ARCH) | SELECT_SH4A_SINGLE;
      return true;

    case OPT_m4a_single_only:
      target_flags = (target_flags & ~MASK_ARCH) | SELECT_SH4A_SINGLE_ONLY;
      return true;

    case OPT_m5_32media:
      target_flags = (target_flags & ~MASK_ARCH) | SELECT_SH5_32MEDIA;
      return true;

    case OPT_m5_32media_nofpu:
      target_flags = (target_flags & ~MASK_ARCH) | SELECT_SH5_32MEDIA_NOFPU;
      return true;

    case OPT_m5_64media:
      target_flags = (target_flags & ~MASK_ARCH) | SELECT_SH5_64MEDIA;
      return true;

    case OPT_m5_64media_nofpu:
      target_flags = (target_flags & ~MASK_ARCH) | SELECT_SH5_64MEDIA_NOFPU;
      return true;

    case OPT_m5_compact:
      target_flags = (target_flags & ~MASK_ARCH) | SELECT_SH5_COMPACT;
      return true;

    case OPT_m5_compact_nofpu:
      target_flags = (target_flags & ~MASK_ARCH) | SELECT_SH5_COMPACT_NOFPU;
      return true;

    default:
      return true;
    }
}

/* Print the operand address in x to the stream.  */

void
print_operand_address (FILE *stream, rtx x)
{
  switch (GET_CODE (x))
    {
    case REG:
    case SUBREG:
      fprintf (stream, "@%s", reg_names[true_regnum (x)]);
      break;

    case PLUS:
      {
	rtx base = XEXP (x, 0);
	rtx index = XEXP (x, 1);

	switch (GET_CODE (index))
	  {
	  case CONST_INT:
	    fprintf (stream, "@(%d,%s)", (int) INTVAL (index),
		     reg_names[true_regnum (base)]);
	    break;

	  case REG:
	  case SUBREG:
	    {
	      int base_num = true_regnum (base);
	      int index_num = true_regnum (index);

	      fprintf (stream, "@(r0,%s)",
		       reg_names[MAX (base_num, index_num)]);
	      break;
	    }

	  default:
	    gcc_unreachable ();
	  }
      }
      break;

    case PRE_DEC:
      fprintf (stream, "@-%s", reg_names[true_regnum (XEXP (x, 0))]);
      break;

    case POST_INC:
      fprintf (stream, "@%s+", reg_names[true_regnum (XEXP (x, 0))]);
      break;

    default:
      x = mark_constant_pool_use (x);
      output_addr_const (stream, x);
      break;
    }
}

/* Print operand x (an rtx) in assembler syntax to file stream
   according to modifier code.

   '.'  print a .s if insn needs delay slot
   ','  print LOCAL_LABEL_PREFIX
   '@'  print trap, rte or rts depending upon pragma interruptness
   '#'  output a nop if there is nothing to put in the delay slot
   '''  print likelihood suffix (/u for unlikely).
   '>'  print branch target if -fverbose-asm
   'O'  print a constant without the #
   'R'  print the LSW of a dp value - changes if in little endian
   'S'  print the MSW of a dp value - changes if in little endian
   'T'  print the next word of a dp value - same as 'R' in big endian mode.
   'M'  SHMEDIA: print an `x' if `m' will print `base,index'.
        otherwise: print .b / .w / .l / .s / .d suffix if operand is a MEM.
   'N'  print 'r63' if the operand is (const_int 0).
   'd'  print a V2SF reg as dN instead of fpN.
   'm'  print a pair `base,offset' or `base,index', for LD and ST.
   'U'  Likewise for {LD,ST}{HI,LO}.
   'u'  prints the lowest 16 bits of CONST_INT, as an unsigned value.
   'o'  output an operator.  */

void
print_operand (FILE *stream, rtx x, int code)
{
  int regno;
  enum machine_mode mode;

  switch (code)
    {
      tree trapa_attr;

    case '.':
      if (final_sequence
	  && ! INSN_ANNULLED_BRANCH_P (XVECEXP (final_sequence, 0, 0))
	  && get_attr_length (XVECEXP (final_sequence, 0, 1)))
	fprintf (stream, ASSEMBLER_DIALECT ? "/s" : ".s");
      break;
    case ',':
      fprintf (stream, "%s", LOCAL_LABEL_PREFIX);
      break;
    case '@':
      trapa_attr = lookup_attribute ("trap_exit",
				      DECL_ATTRIBUTES (current_function_decl));
      if (trapa_attr)
	fprintf (stream, "trapa #%ld",
		 (long) TREE_INT_CST_LOW (TREE_VALUE (TREE_VALUE (trapa_attr))));
      else if (sh_cfun_interrupt_handler_p ())
	fprintf (stream, "rte");
      else
	fprintf (stream, "rts");
      break;
    case '#':
      /* Output a nop if there's nothing in the delay slot.  */
      if (dbr_sequence_length () == 0)
	fprintf (stream, "\n\tnop");
      break;
    case '\'':
      {
	rtx note = find_reg_note (current_output_insn, REG_BR_PROB, 0);

	if (note && INTVAL (XEXP (note, 0)) * 2 < REG_BR_PROB_BASE)
	  fputs ("/u", stream);
	break;
      }
    case '>':
      if (flag_verbose_asm && JUMP_LABEL (current_output_insn))
	{
	  fputs ("\t! target: ", stream);
	  output_addr_const (stream, JUMP_LABEL (current_output_insn));
	}
      break;
    case 'O':
      x = mark_constant_pool_use (x);
      output_addr_const (stream, x);
      break;
    /* N.B.: %R / %S / %T adjust memory addresses by four.
       For SHMEDIA, that means they can be used to access the first and
       second 32 bit part of a 64 bit (or larger) value that
       might be held in floating point registers or memory.
       While they can be used to access 64 bit parts of a larger value
       held in general purpose registers, that won't work with memory -
       neither for fp registers, since the frxx names are used.  */
    case 'R':
      if (REG_P (x) || GET_CODE (x) == SUBREG)
	{
	  regno = true_regnum (x);
	  regno += FP_REGISTER_P (regno) ? 1 : LSW;
	  fputs (reg_names[regno], (stream));
	}
      else if (MEM_P (x))
	{
	  x = adjust_address (x, SImode, 4 * LSW);
	  print_operand_address (stream, XEXP (x, 0));
	}
      else
	{
	  rtx sub = NULL_RTX;

	  mode = GET_MODE (x);
	  if (mode == VOIDmode)
	    mode = DImode;
	  if (GET_MODE_SIZE (mode) >= 8)
	    sub = simplify_subreg (SImode, x, mode, 4 * LSW);
	  if (sub)
	    print_operand (stream, sub, 0);
	  else
	    output_operand_lossage ("invalid operand to %%R");
	}
      break;
    case 'S':
      if (REG_P (x) || GET_CODE (x) == SUBREG)
	{
	  regno = true_regnum (x);
	  regno += FP_REGISTER_P (regno) ? 0 : MSW;
	  fputs (reg_names[regno], (stream));
	}
      else if (MEM_P (x))
	{
	  x = adjust_address (x, SImode, 4 * MSW);
	  print_operand_address (stream, XEXP (x, 0));
	}
      else
	{
	  rtx sub = NULL_RTX;

	  mode = GET_MODE (x);
	  if (mode == VOIDmode)
	    mode = DImode;
	  if (GET_MODE_SIZE (mode) >= 8)
	    sub = simplify_subreg (SImode, x, mode, 4 * MSW);
	  if (sub)
	    print_operand (stream, sub, 0);
	  else
	    output_operand_lossage ("invalid operand to %%S");
	}
      break;
    case 'T':
      /* Next word of a double.  */
      switch (GET_CODE (x))
	{
	case REG:
	  fputs (reg_names[REGNO (x) + 1], (stream));
	  break;
	case MEM:
	  if (GET_CODE (XEXP (x, 0)) != PRE_DEC
	      && GET_CODE (XEXP (x, 0)) != POST_INC)
	    x = adjust_address (x, SImode, 4);
	  print_operand_address (stream, XEXP (x, 0));
	  break;
	default:
	  break;
	}
      break;
    case 'o':
      switch (GET_CODE (x))
	{
	case PLUS:  fputs ("add", stream); break;
	case MINUS: fputs ("sub", stream); break;
	case MULT:  fputs ("mul", stream); break;
	case DIV:   fputs ("div", stream); break;
	case EQ:    fputs ("eq",  stream); break;
	case NE:    fputs ("ne",  stream); break;
	case GT:  case LT:  fputs ("gt",  stream); break;
	case GE:  case LE:  fputs ("ge",  stream); break;
	case GTU: case LTU: fputs ("gtu", stream); break;
	case GEU: case LEU: fputs ("geu", stream); break;
	default:
	  break;
	}
      break;
    case 'M':
      if (TARGET_SHMEDIA)
	{
	  if (GET_CODE (x) == MEM
	      && GET_CODE (XEXP (x, 0)) == PLUS
	      && (GET_CODE (XEXP (XEXP (x, 0), 1)) == REG
		  || GET_CODE (XEXP (XEXP (x, 0), 1)) == SUBREG))
	    fputc ('x', stream);
	}
      else
	{
	  if (GET_CODE (x) == MEM)
	    {
	      switch (GET_MODE (x))
		{
		case QImode: fputs (".b", stream); break;
		case HImode: fputs (".w", stream); break;
		case SImode: fputs (".l", stream); break;
		case SFmode: fputs (".s", stream); break;
		case DFmode: fputs (".d", stream); break;
		default: gcc_unreachable ();
		}
	    }
	}
      break;

    case 'm':
      gcc_assert (GET_CODE (x) == MEM);
      x = XEXP (x, 0);
      /* Fall through.  */
    case 'U':
      switch (GET_CODE (x))
	{
	case REG:
	case SUBREG:
	  print_operand (stream, x, 0);
	  fputs (", 0", stream);
	  break;

	case PLUS:
	  print_operand (stream, XEXP (x, 0), 0);
	  fputs (", ", stream);
	  print_operand (stream, XEXP (x, 1), 0);
	  break;

	default:
	  gcc_unreachable ();
	}
      break;

    case 'd':
      gcc_assert (GET_CODE (x) == REG && GET_MODE (x) == V2SFmode);

      fprintf ((stream), "d%s", reg_names[REGNO (x)] + 1);
      break;

    case 'N':
      if (x == CONST0_RTX (GET_MODE (x)))
	{
	  fprintf ((stream), "r63");
	  break;
	}
      goto default_output;
    case 'u':
      if (GET_CODE (x) == CONST_INT)
	{
	  fprintf ((stream), "%u", (unsigned) INTVAL (x) & (0x10000 - 1));
	  break;
	}
      /* Fall through.  */

    default_output:
    default:
      regno = 0;
      mode = GET_MODE (x);

      switch (GET_CODE (x))
	{
	case TRUNCATE:
	  {
	    rtx inner = XEXP (x, 0);
	    int offset = 0;
	    enum machine_mode inner_mode;

	    /* We might see SUBREGs with vector mode registers inside.  */
	    if (GET_CODE (inner) == SUBREG
		&& (GET_MODE_SIZE (GET_MODE (inner))
		    == GET_MODE_SIZE (GET_MODE (SUBREG_REG (inner))))
		&& subreg_lowpart_p (inner))
	      inner = SUBREG_REG (inner);
	    if (GET_CODE (inner) == CONST_INT)
	      {
		x = GEN_INT (trunc_int_for_mode (INTVAL (inner), GET_MODE (x)));
		goto default_output;
	      }
	    inner_mode = GET_MODE (inner);
	    if (GET_CODE (inner) == SUBREG
		&& (GET_MODE_SIZE (GET_MODE (inner))
		    < GET_MODE_SIZE (GET_MODE (SUBREG_REG (inner))))
		&& GET_CODE (SUBREG_REG (inner)) == REG)
	      {
		offset = subreg_regno_offset (REGNO (SUBREG_REG (inner)),
					      GET_MODE (SUBREG_REG (inner)),
					      SUBREG_BYTE (inner),
					      GET_MODE (inner));
		inner = SUBREG_REG (inner);
	      }
	    if (GET_CODE (inner) != REG || GET_MODE_SIZE (inner_mode) > 8)
	      abort ();
	    /* Floating point register pairs are always big endian;
	       general purpose registers are 64 bit wide.  */
	    regno = REGNO (inner);
	    regno = (HARD_REGNO_NREGS (regno, inner_mode)
		     - HARD_REGNO_NREGS (regno, mode))
		     + offset;
	    x = inner;
	    goto reg;
	  }
	case SIGN_EXTEND:
	  x = XEXP (x, 0);
	  goto reg;
	  /* FIXME: We need this on SHmedia32 because reload generates
	     some sign-extended HI or QI loads into DImode registers
	     but, because Pmode is SImode, the address ends up with a
	     subreg:SI of the DImode register.  Maybe reload should be
	     fixed so as to apply alter_subreg to such loads?  */
	case IF_THEN_ELSE:
	  gcc_assert (trapping_target_operand (x, VOIDmode));
	  x = XEXP (XEXP (x, 2), 0);
	  goto default_output;
	case SUBREG:
	  gcc_assert (SUBREG_BYTE (x) == 0
		      && GET_CODE (SUBREG_REG (x)) == REG);

	  x = SUBREG_REG (x);
	  /* Fall through.  */

	reg:
	case REG:
	  regno += REGNO (x);
	  if (FP_REGISTER_P (regno)
	      && mode == V16SFmode)
	    fprintf ((stream), "mtrx%s", reg_names[regno] + 2);
	  else if (FP_REGISTER_P (REGNO (x))
		   && mode == V4SFmode)
	    fprintf ((stream), "fv%s", reg_names[regno] + 2);
	  else if (GET_CODE (x) == REG
		   && mode == V2SFmode)
	    fprintf ((stream), "fp%s", reg_names[regno] + 2);
	  else if (FP_REGISTER_P (REGNO (x))
		   && GET_MODE_SIZE (mode) > 4)
	    fprintf ((stream), "d%s", reg_names[regno] + 1);
	  else
	    fputs (reg_names[regno], (stream));
	  break;

	case MEM:
	  output_address (XEXP (x, 0));
	  break;

	case CONST:
	  if (TARGET_SHMEDIA
	      && (GET_CODE (XEXP (x, 0)) == SIGN_EXTEND
		  || GET_CODE (XEXP (x, 0)) == ZERO_EXTEND)
	      && (GET_MODE (XEXP (x, 0)) == DImode
		  || GET_MODE (XEXP (x, 0)) == SImode)
	      && GET_CODE (XEXP (XEXP (x, 0), 0)) == TRUNCATE
	      && GET_MODE (XEXP (XEXP (x, 0), 0)) == HImode)
	    {
	      rtx val = XEXP (XEXP (XEXP (x, 0), 0), 0);
	      rtx val2 = val;
	      bool nested_expr = false;

	      fputc ('(', stream);
	      if (GET_CODE (val) == ASHIFTRT)
		{
		  fputc ('(', stream);
		  val2 = XEXP (val, 0);
		}
	      if (GET_CODE (val2) == CONST
		  || GET_RTX_CLASS (GET_CODE (val2)) != RTX_OBJ)
		{
		  fputc ('(', stream);
		  nested_expr = true;
		}
	      output_addr_const (stream, val2);
	      if (nested_expr)
		fputc (')', stream);
	      if (GET_CODE (val) == ASHIFTRT)
		{
		  fputs (" >> ", stream);
		  output_addr_const (stream, XEXP (val, 1));
		  fputc (')', stream);
		}
	      fputs (" & 65535)", stream);
	      break;
	    }

	  /* Fall through.  */
	default:
	  if (TARGET_SH1)
	    fputc ('#', stream);
	  output_addr_const (stream, x);
	  break;
	}
      break;
    }
}

/* Like force_operand, but guarantees that VALUE ends up in TARGET.  */
static void
force_into (rtx value, rtx target)
{
  value = force_operand (value, target);
  if (! rtx_equal_p (value, target))
    emit_insn (gen_move_insn (target, value));
}

/* Emit code to perform a block move.  Choose the best method.

   OPERANDS[0] is the destination.
   OPERANDS[1] is the source.
   OPERANDS[2] is the size.
   OPERANDS[3] is the alignment safe to use.  */

int
expand_block_move (rtx *operands)
{
  int align = INTVAL (operands[3]);
  int constp = (GET_CODE (operands[2]) == CONST_INT);
  int bytes = (constp ? INTVAL (operands[2]) : 0);

  if (! constp)
    return 0;

  /* If we could use mov.l to move words and dest is word-aligned, we
     can use movua.l for loads and still generate a relatively short
     and efficient sequence.  */
  if (TARGET_SH4A_ARCH && align < 4
      && MEM_ALIGN (operands[0]) >= 32
      && can_move_by_pieces (bytes, 32))
    {
      rtx dest = copy_rtx (operands[0]);
      rtx src = copy_rtx (operands[1]);
      /* We could use different pseudos for each copied word, but
	 since movua can only load into r0, it's kind of
	 pointless.  */
      rtx temp = gen_reg_rtx (SImode);
      rtx src_addr = copy_addr_to_reg (XEXP (src, 0));
      int copied = 0;

      while (copied + 4 <= bytes)
	{
	  rtx to = adjust_address (dest, SImode, copied);
	  rtx from = adjust_automodify_address (src, SImode, src_addr, copied);

	  emit_insn (gen_movua (temp, from));
	  emit_move_insn (src_addr, plus_constant (src_addr, 4));
	  emit_move_insn (to, temp);
	  copied += 4;
	}

      if (copied < bytes)
	move_by_pieces (adjust_address (dest, BLKmode, copied),
			adjust_automodify_address (src, BLKmode,
						   src_addr, copied),
			bytes - copied, align, 0);

      return 1;
    }

  /* If it isn't a constant number of bytes, or if it doesn't have 4 byte
     alignment, or if it isn't a multiple of 4 bytes, then fail.  */
  if (align < 4 || (bytes % 4 != 0))
    return 0;

  if (TARGET_HARD_SH4)
    {
      if (bytes < 12)
	return 0;
      else if (bytes == 12)
	{
	  rtx func_addr_rtx = gen_reg_rtx (Pmode);
	  rtx r4 = gen_rtx_REG (SImode, 4);
	  rtx r5 = gen_rtx_REG (SImode, 5);

	  function_symbol (func_addr_rtx, "__movmemSI12_i4", SFUNC_STATIC);
	  force_into (XEXP (operands[0], 0), r4);
	  force_into (XEXP (operands[1], 0), r5);
	  emit_insn (gen_block_move_real_i4 (func_addr_rtx));
	  return 1;
	}
      else if (! TARGET_SMALLCODE)
	{
	  const char *entry_name;
	  rtx func_addr_rtx = gen_reg_rtx (Pmode);
	  int dwords;
	  rtx r4 = gen_rtx_REG (SImode, 4);
	  rtx r5 = gen_rtx_REG (SImode, 5);
	  rtx r6 = gen_rtx_REG (SImode, 6);

	  entry_name = (bytes & 4 ? "__movmem_i4_odd" : "__movmem_i4_even");
	  function_symbol (func_addr_rtx, entry_name, SFUNC_STATIC);
	  force_into (XEXP (operands[0], 0), r4);
	  force_into (XEXP (operands[1], 0), r5);

	  dwords = bytes >> 3;
	  emit_insn (gen_move_insn (r6, GEN_INT (dwords - 1)));
	  emit_insn (gen_block_lump_real_i4 (func_addr_rtx));
	  return 1;
	}
      else
	return 0;
    }
  if (bytes < 64)
    {
      char entry[30];
      rtx func_addr_rtx = gen_reg_rtx (Pmode);
      rtx r4 = gen_rtx_REG (SImode, 4);
      rtx r5 = gen_rtx_REG (SImode, 5);

      sprintf (entry, "__movmemSI%d", bytes);
      function_symbol (func_addr_rtx, entry, SFUNC_STATIC);
      force_into (XEXP (operands[0], 0), r4);
      force_into (XEXP (operands[1], 0), r5);
      emit_insn (gen_block_move_real (func_addr_rtx));
      return 1;
    }

  /* This is the same number of bytes as a memcpy call, but to a different
     less common function name, so this will occasionally use more space.  */
  if (! TARGET_SMALLCODE)
    {
      rtx func_addr_rtx = gen_reg_rtx (Pmode);
      int final_switch, while_loop;
      rtx r4 = gen_rtx_REG (SImode, 4);
      rtx r5 = gen_rtx_REG (SImode, 5);
      rtx r6 = gen_rtx_REG (SImode, 6);

      function_symbol (func_addr_rtx, "__movmem", SFUNC_STATIC);
      force_into (XEXP (operands[0], 0), r4);
      force_into (XEXP (operands[1], 0), r5);

      /* r6 controls the size of the move.  16 is decremented from it
	 for each 64 bytes moved.  Then the negative bit left over is used
	 as an index into a list of move instructions.  e.g., a 72 byte move
	 would be set up with size(r6) = 14, for one iteration through the
	 big while loop, and a switch of -2 for the last part.  */

      final_switch = 16 - ((bytes / 4) % 16);
      while_loop = ((bytes / 4) / 16 - 1) * 16;
      emit_insn (gen_move_insn (r6, GEN_INT (while_loop + final_switch)));
      emit_insn (gen_block_lump_real (func_addr_rtx));
      return 1;
    }

  return 0;
}

/* Prepare operands for a move define_expand; specifically, one of the
   operands must be in a register.  */

int
prepare_move_operands (rtx operands[], enum machine_mode mode)
{
  if ((mode == SImode || mode == DImode)
      && flag_pic
      && ! ((mode == Pmode || mode == ptr_mode)
	    && tls_symbolic_operand (operands[1], Pmode) != 0))
    {
      rtx temp;
      if (SYMBOLIC_CONST_P (operands[1]))
	{
	  if (GET_CODE (operands[0]) == MEM)
	    operands[1] = force_reg (Pmode, operands[1]);
	  else if (TARGET_SHMEDIA
		   && GET_CODE (operands[1]) == LABEL_REF
		   && target_reg_operand (operands[0], mode))
	    /* It's ok.  */;
	  else
	    {
	      temp = no_new_pseudos ? operands[0] : gen_reg_rtx (Pmode);
	      operands[1] = legitimize_pic_address (operands[1], mode, temp);
	    }
	}
      else if (GET_CODE (operands[1]) == CONST
	       && GET_CODE (XEXP (operands[1], 0)) == PLUS
	       && SYMBOLIC_CONST_P (XEXP (XEXP (operands[1], 0), 0)))
	{
	  temp = no_new_pseudos ? operands[0] : gen_reg_rtx (Pmode);
	  temp = legitimize_pic_address (XEXP (XEXP (operands[1], 0), 0),
					 mode, temp);
	  operands[1] = expand_binop (mode, add_optab, temp,
				      XEXP (XEXP (operands[1], 0), 1),
				      no_new_pseudos ? temp
				      : gen_reg_rtx (Pmode),
				      0, OPTAB_LIB_WIDEN);
	}
    }

  if (! reload_in_progress && ! reload_completed)
    {
      /* Copy the source to a register if both operands aren't registers.  */
      if (! register_operand (operands[0], mode)
	  && ! sh_register_operand (operands[1], mode))
	operands[1] = copy_to_mode_reg (mode, operands[1]);

      if (GET_CODE (operands[0]) == MEM && ! memory_operand (operands[0], mode))
	{
	  /* This is like change_address_1 (operands[0], mode, 0, 1) ,
	     except that we can't use that function because it is static.  */
	  rtx new = change_address (operands[0], mode, 0);
	  MEM_COPY_ATTRIBUTES (new, operands[0]);
	  operands[0] = new;
	}

      /* This case can happen while generating code to move the result
	 of a library call to the target.  Reject `st r0,@(rX,rY)' because
	 reload will fail to find a spill register for rX, since r0 is already
	 being used for the source.  */
      else if (TARGET_SH1
	       && refers_to_regno_p (R0_REG, R0_REG + 1, operands[1], (rtx *)0)
	       && GET_CODE (operands[0]) == MEM
	       && GET_CODE (XEXP (operands[0], 0)) == PLUS
	       && GET_CODE (XEXP (XEXP (operands[0], 0), 1)) == REG)
	operands[1] = copy_to_mode_reg (mode, operands[1]);
    }

  if (mode == Pmode || mode == ptr_mode)
    {
      rtx op0, op1, opc;
      enum tls_model tls_kind;

      op0 = operands[0];
      op1 = operands[1];
      if (GET_CODE (op1) == CONST
	  && GET_CODE (XEXP (op1, 0)) == PLUS
	  && tls_symbolic_operand (XEXP (XEXP (op1, 0), 0), Pmode))
	{
	  opc = XEXP (XEXP (op1, 0), 1);
	  op1 = XEXP (XEXP (op1, 0), 0);
	}
      else
	opc = NULL_RTX;

      if ((tls_kind = tls_symbolic_operand (op1, Pmode)))
	{
	  rtx tga_op1, tga_ret, tmp, tmp2;

	  switch (tls_kind)
	    {
	    case TLS_MODEL_GLOBAL_DYNAMIC:
	      tga_ret = gen_rtx_REG (Pmode, R0_REG);
	      emit_call_insn (gen_tls_global_dynamic (tga_ret, op1));
	      op1 = tga_ret;
	      break;

	    case TLS_MODEL_LOCAL_DYNAMIC:
	      tga_ret = gen_rtx_REG (Pmode, R0_REG);
	      emit_call_insn (gen_tls_local_dynamic (tga_ret, op1));

	      tmp = gen_reg_rtx (Pmode);
	      emit_move_insn (tmp, tga_ret);

	      if (register_operand (op0, Pmode))
		tmp2 = op0;
	      else
		tmp2 = gen_reg_rtx (Pmode);

	      emit_insn (gen_symDTPOFF2reg (tmp2, op1, tmp));
	      op1 = tmp2;
	      break;

	    case TLS_MODEL_INITIAL_EXEC:
	      if (! flag_pic)
		{
		  /* Don't schedule insns for getting GOT address when
		     the first scheduling is enabled, to avoid spill
		     failures for R0.  */
		  if (flag_schedule_insns)
		    emit_insn (gen_blockage ());
		  emit_insn (gen_GOTaddr2picreg ());
		  emit_insn (gen_rtx_USE (VOIDmode, gen_rtx_REG (SImode,
								 PIC_REG)));
		  if (flag_schedule_insns)
		    emit_insn (gen_blockage ());
		}
	      tga_op1 = no_new_pseudos ? op0 : gen_reg_rtx (Pmode);
	      tmp = gen_sym2GOTTPOFF (op1);
	      emit_insn (gen_tls_initial_exec (tga_op1, tmp));
	      op1 = tga_op1;
	      break;

	    case TLS_MODEL_LOCAL_EXEC:
	      tmp2 = gen_reg_rtx (Pmode);
	      emit_insn (gen_load_gbr (tmp2));
	      tmp = gen_reg_rtx (Pmode);
	      emit_insn (gen_symTPOFF2reg (tmp, op1));

	      if (register_operand (op0, Pmode))
		op1 = op0;
	      else
		op1 = gen_reg_rtx (Pmode);

	      emit_insn (gen_addsi3 (op1, tmp, tmp2));
	      break;

	    default:
	      gcc_unreachable ();
	    }
	  if (opc)
	    emit_insn (gen_addsi3 (op1, op1, force_reg (SImode, opc)));
	  operands[1] = op1;
	}
    }

  return 0;
}

/* Prepare the operands for an scc instruction; make sure that the
   compare has been done.  */
rtx
prepare_scc_operands (enum rtx_code code)
{
  rtx t_reg = gen_rtx_REG (SImode, T_REG);
  enum rtx_code oldcode = code;
  enum machine_mode mode;

  /* First need a compare insn.  */
  switch (code)
    {
    case NE:
      /* It isn't possible to handle this case.  */
      gcc_unreachable ();
    case LT:
      code = GT;
      break;
    case LE:
      code = GE;
      break;
    case LTU:
      code = GTU;
      break;
    case LEU:
      code = GEU;
      break;
    default:
      break;
    }
  if (code != oldcode)
    {
      rtx tmp = sh_compare_op0;
      sh_compare_op0 = sh_compare_op1;
      sh_compare_op1 = tmp;
    }

  mode = GET_MODE (sh_compare_op0);
  if (mode == VOIDmode)
    mode = GET_MODE (sh_compare_op1);

  sh_compare_op0 = force_reg (mode, sh_compare_op0);
  if ((code != EQ && code != NE
       && (sh_compare_op1 != const0_rtx
	   || code == GTU  || code == GEU || code == LTU || code == LEU))
      || (mode == DImode && sh_compare_op1 != const0_rtx)
      || (TARGET_SH2E && GET_MODE_CLASS (mode) == MODE_FLOAT))
    sh_compare_op1 = force_reg (mode, sh_compare_op1);

  if ((TARGET_SH4 || TARGET_SH2A) && GET_MODE_CLASS (mode) == MODE_FLOAT)
    (mode == SFmode ? emit_sf_insn : emit_df_insn)
     (gen_rtx_PARALLEL (VOIDmode, gen_rtvec (2,
		gen_rtx_SET (VOIDmode, t_reg,
			     gen_rtx_fmt_ee (code, SImode,
					     sh_compare_op0, sh_compare_op1)),
		gen_rtx_USE (VOIDmode, get_fpscr_rtx ()))));
  else
    emit_insn (gen_rtx_SET (VOIDmode, t_reg,
			    gen_rtx_fmt_ee (code, SImode,
					    sh_compare_op0, sh_compare_op1)));

  return t_reg;
}

/* Called from the md file, set up the operands of a compare instruction.  */

void
from_compare (rtx *operands, int code)
{
  enum machine_mode mode = GET_MODE (sh_compare_op0);
  rtx insn;
  if (mode == VOIDmode)
    mode = GET_MODE (sh_compare_op1);
  if (code != EQ
      || mode == DImode
      || (TARGET_SH2E && GET_MODE_CLASS (mode) == MODE_FLOAT))
    {
      /* Force args into regs, since we can't use constants here.  */
      sh_compare_op0 = force_reg (mode, sh_compare_op0);
      if (sh_compare_op1 != const0_rtx
	  || code == GTU  || code == GEU
	  || (TARGET_SH2E && GET_MODE_CLASS (mode) == MODE_FLOAT))
	sh_compare_op1 = force_reg (mode, sh_compare_op1);
    }
  if (TARGET_SH2E && GET_MODE_CLASS (mode) == MODE_FLOAT && code == GE)
    {
      from_compare (operands, GT);
      insn = gen_ieee_ccmpeqsf_t (sh_compare_op0, sh_compare_op1);
    }
  else
    insn = gen_rtx_SET (VOIDmode,
			gen_rtx_REG (SImode, T_REG),
			gen_rtx_fmt_ee (code, SImode,
					sh_compare_op0, sh_compare_op1));
  if ((TARGET_SH4 || TARGET_SH2A) && GET_MODE_CLASS (mode) == MODE_FLOAT)
    {
      insn = gen_rtx_PARALLEL (VOIDmode,
		      gen_rtvec (2, insn,
				 gen_rtx_USE (VOIDmode, get_fpscr_rtx ())));
      (mode == SFmode ? emit_sf_insn : emit_df_insn) (insn);
    }
  else
    emit_insn (insn);
}

/* Functions to output assembly code.  */

/* Return a sequence of instructions to perform DI or DF move.

   Since the SH cannot move a DI or DF in one instruction, we have
   to take care when we see overlapping source and dest registers.  */

const char *
output_movedouble (rtx insn ATTRIBUTE_UNUSED, rtx operands[],
		   enum machine_mode mode)
{
  rtx dst = operands[0];
  rtx src = operands[1];

  if (GET_CODE (dst) == MEM
      && GET_CODE (XEXP (dst, 0)) == PRE_DEC)
    return "mov.l	%T1,%0\n\tmov.l	%1,%0";

  if (register_operand (dst, mode)
      && register_operand (src, mode))
    {
      if (REGNO (src) == MACH_REG)
	return "sts	mach,%S0\n\tsts	macl,%R0";

      /* When mov.d r1,r2 do r2->r3 then r1->r2;
         when mov.d r1,r0 do r1->r0 then r2->r1.  */

      if (REGNO (src) + 1 == REGNO (dst))
	return "mov	%T1,%T0\n\tmov	%1,%0";
      else
	return "mov	%1,%0\n\tmov	%T1,%T0";
    }
  else if (GET_CODE (src) == CONST_INT)
    {
      if (INTVAL (src) < 0)
	output_asm_insn ("mov	#-1,%S0", operands);
      else
	output_asm_insn ("mov	#0,%S0", operands);

      return "mov	%1,%R0";
    }
  else if (GET_CODE (src) == MEM)
    {
      int ptrreg = -1;
      int dreg = REGNO (dst);
      rtx inside = XEXP (src, 0);

      switch (GET_CODE (inside))
	{
	case REG:
	  ptrreg = REGNO (inside);
	  break;

	case SUBREG:
	  ptrreg = subreg_regno (inside);
	  break;

	case PLUS:
	  ptrreg = REGNO (XEXP (inside, 0));
	  /* ??? A r0+REG address shouldn't be possible here, because it isn't
	     an offsettable address.  Unfortunately, offsettable addresses use
	     QImode to check the offset, and a QImode offsettable address
	     requires r0 for the other operand, which is not currently
	     supported, so we can't use the 'o' constraint.
	     Thus we must check for and handle r0+REG addresses here.
	     We punt for now, since this is likely very rare.  */
	  gcc_assert (GET_CODE (XEXP (inside, 1)) != REG);
	  break;
	  
	case LABEL_REF:
	  return "mov.l	%1,%0\n\tmov.l	%1+4,%T0";
	case POST_INC:
	  return "mov.l	%1,%0\n\tmov.l	%1,%T0";
	default:
	  gcc_unreachable ();
	}

      /* Work out the safe way to copy.  Copy into the second half first.  */
      if (dreg == ptrreg)
	return "mov.l	%T1,%T0\n\tmov.l	%1,%0";
    }

  return "mov.l	%1,%0\n\tmov.l	%T1,%T0";
}

/* Print an instruction which would have gone into a delay slot after
   another instruction, but couldn't because the other instruction expanded
   into a sequence where putting the slot insn at the end wouldn't work.  */

static void
print_slot (rtx insn)
{
  final_scan_insn (XVECEXP (insn, 0, 1), asm_out_file, optimize, 1, NULL);

  INSN_DELETED_P (XVECEXP (insn, 0, 1)) = 1;
}

const char *
output_far_jump (rtx insn, rtx op)
{
  struct { rtx lab, reg, op; } this;
  rtx braf_base_lab = NULL_RTX;
  const char *jump;
  int far;
  int offset = branch_dest (insn) - INSN_ADDRESSES (INSN_UID (insn));
  rtx prev;

  this.lab = gen_label_rtx ();

  if (TARGET_SH2
      && offset >= -32764
      && offset - get_attr_length (insn) <= 32766)
    {
      far = 0;
      jump = "mov.w	%O0,%1; braf	%1";
    }
  else
    {
      far = 1;
      if (flag_pic)
	{
	  if (TARGET_SH2)
	    jump = "mov.l	%O0,%1; braf	%1";
	  else
	    jump = "mov.l	r0,@-r15; mova	%O0,r0; mov.l	@r0,%1; add	r0,%1; mov.l	@r15+,r0; jmp	@%1";
	}
      else
	jump = "mov.l	%O0,%1; jmp	@%1";
    }
  /* If we have a scratch register available, use it.  */
  if (GET_CODE ((prev = prev_nonnote_insn (insn))) == INSN
      && INSN_CODE (prev) == CODE_FOR_indirect_jump_scratch)
    {
      this.reg = SET_DEST (XVECEXP (PATTERN (prev), 0, 0));
      if (REGNO (this.reg) == R0_REG && flag_pic && ! TARGET_SH2)
	jump = "mov.l	r1,@-r15; mova	%O0,r0; mov.l	@r0,r1; add	r1,r0; mov.l	@r15+,r1; jmp	@%1";
      output_asm_insn (jump, &this.lab);
      if (dbr_sequence_length ())
	print_slot (final_sequence);
      else
	output_asm_insn ("nop", 0);
    }
  else
    {
      /* Output the delay slot insn first if any.  */
      if (dbr_sequence_length ())
	print_slot (final_sequence);

      this.reg = gen_rtx_REG (SImode, 13);
      /* We must keep the stack aligned to 8-byte boundaries on SH5.
	 Fortunately, MACL is fixed and call-clobbered, and we never
	 need its value across jumps, so save r13 in it instead of in
	 the stack.  */
      if (TARGET_SH5)
	output_asm_insn ("lds	r13, macl", 0);
      else
	output_asm_insn ("mov.l	r13,@-r15", 0);
      output_asm_insn (jump, &this.lab);
      if (TARGET_SH5)
	output_asm_insn ("sts	macl, r13", 0);
      else
	output_asm_insn ("mov.l	@r15+,r13", 0);
    }
  if (far && flag_pic && TARGET_SH2)
    {
      braf_base_lab = gen_label_rtx ();
      (*targetm.asm_out.internal_label) (asm_out_file, "L",
				 CODE_LABEL_NUMBER (braf_base_lab));
    }
  if (far)
    output_asm_insn (".align	2", 0);
  (*targetm.asm_out.internal_label) (asm_out_file, "L", CODE_LABEL_NUMBER (this.lab));
  this.op = op;
  if (far && flag_pic)
    {
      if (TARGET_SH2)
	this.lab = braf_base_lab;
      output_asm_insn (".long	%O2-%O0", &this.lab);
    }
  else
    output_asm_insn (far ? ".long	%O2" : ".word %O2-%O0", &this.lab);
  return "";
}

/* Local label counter, used for constants in the pool and inside
   pattern branches.  */

static int lf = 100;

/* Output code for ordinary branches.  */

const char *
output_branch (int logic, rtx insn, rtx *operands)
{
  switch (get_attr_length (insn))
    {
    case 6:
      /* This can happen if filling the delay slot has caused a forward
	 branch to exceed its range (we could reverse it, but only
	 when we know we won't overextend other branches; this should
	 best be handled by relaxation).
	 It can also happen when other condbranches hoist delay slot insn
	 from their destination, thus leading to code size increase.
	 But the branch will still be in the range -4092..+4098 bytes.  */

      if (! TARGET_RELAX)
	{
	  int label = lf++;
	  /* The call to print_slot will clobber the operands.  */
	  rtx op0 = operands[0];

	  /* If the instruction in the delay slot is annulled (true), then
	     there is no delay slot where we can put it now.  The only safe
	     place for it is after the label.  final will do that by default.  */

	  if (final_sequence
	      && ! INSN_ANNULLED_BRANCH_P (XVECEXP (final_sequence, 0, 0))
	      && get_attr_length (XVECEXP (final_sequence, 0, 1)))
	    {
	      asm_fprintf (asm_out_file, "\tb%s%ss\t%LLF%d\n", logic ? "f" : "t",
	                   ASSEMBLER_DIALECT ? "/" : ".", label);
	      print_slot (final_sequence);
	    }
	  else
	    asm_fprintf (asm_out_file, "\tb%s\t%LLF%d\n", logic ? "f" : "t", label);

	  output_asm_insn ("bra\t%l0", &op0);
	  fprintf (asm_out_file, "\tnop\n");
	  (*targetm.asm_out.internal_label) (asm_out_file, "LF", label);

	  return "";
	}
      /* When relaxing, handle this like a short branch.  The linker
	 will fix it up if it still doesn't fit after relaxation.  */
    case 2:
      return logic ? "bt%.\t%l0" : "bf%.\t%l0";

      /* These are for SH2e, in which we have to account for the
	 extra nop because of the hardware bug in annulled branches.  */
    case 8:
      if (! TARGET_RELAX)
	{
	  int label = lf++;

	  gcc_assert (!final_sequence
		      || !(INSN_ANNULLED_BRANCH_P
			   (XVECEXP (final_sequence, 0, 0))));
	  asm_fprintf (asm_out_file, "b%s%ss\t%LLF%d\n",
		       logic ? "f" : "t",
		       ASSEMBLER_DIALECT ? "/" : ".", label);
	  fprintf (asm_out_file, "\tnop\n");
	  output_asm_insn ("bra\t%l0", operands);
	  fprintf (asm_out_file, "\tnop\n");
	  (*targetm.asm_out.internal_label) (asm_out_file, "LF", label);

	  return "";
	}
      /* When relaxing, fall through.  */
    case 4:
      {
	char buffer[10];

	sprintf (buffer, "b%s%ss\t%%l0",
		 logic ? "t" : "f",
		 ASSEMBLER_DIALECT ? "/" : ".");
	output_asm_insn (buffer, &operands[0]);
	return "nop";
      }

    default:
      /* There should be no longer branches now - that would
	 indicate that something has destroyed the branches set
	 up in machine_dependent_reorg.  */
      gcc_unreachable ();
    }
}

const char *
output_branchy_insn (enum rtx_code code, const char *template,
		     rtx insn, rtx *operands)
{
  rtx next_insn = NEXT_INSN (insn);

  if (next_insn && GET_CODE (next_insn) == JUMP_INSN && condjump_p (next_insn))
    {
      rtx src = SET_SRC (PATTERN (next_insn));
      if (GET_CODE (src) == IF_THEN_ELSE && GET_CODE (XEXP (src, 0)) != code)
	{
	  /* Following branch not taken */
	  operands[9] = gen_label_rtx ();
	  emit_label_after (operands[9], next_insn);
	  INSN_ADDRESSES_NEW (operands[9],
			      INSN_ADDRESSES (INSN_UID (next_insn))
			      + get_attr_length (next_insn));
	  return template;
	}
      else
	{
	  int offset = (branch_dest (next_insn)
			- INSN_ADDRESSES (INSN_UID (next_insn)) + 4);
	  if (offset >= -252 && offset <= 258)
	    {
	      if (GET_CODE (src) == IF_THEN_ELSE)
		/* branch_true */
		src = XEXP (src, 1);
	      operands[9] = src;
	      return template;
	    }
	}
    }
  operands[9] = gen_label_rtx ();
  emit_label_after (operands[9], insn);
  INSN_ADDRESSES_NEW (operands[9],
		      INSN_ADDRESSES (INSN_UID (insn))
		      + get_attr_length (insn));
  return template;
}

const char *
output_ieee_ccmpeq (rtx insn, rtx *operands)
{
  return output_branchy_insn (NE, "bt\t%l9\n\tfcmp/eq\t%1,%0",
			      insn, operands);
}

/* Output the start of the assembler file.  */

static void
sh_file_start (void)
{
  default_file_start ();

#ifdef SYMBIAN
  /* Declare the .directive section before it is used.  */
  fputs ("\t.section .directive, \"SM\", @progbits, 1\n", asm_out_file);
  fputs ("\t.asciz \"#<SYMEDIT>#\\n\"\n", asm_out_file);
#endif

  if (TARGET_ELF)
    /* We need to show the text section with the proper
       attributes as in TEXT_SECTION_ASM_OP, before dwarf2out
       emits it without attributes in TEXT_SECTION_ASM_OP, else GAS
       will complain.  We can teach GAS specifically about the
       default attributes for our choice of text section, but
       then we would have to change GAS again if/when we change
       the text section name.  */
    fprintf (asm_out_file, "%s\n", TEXT_SECTION_ASM_OP);
  else
    /* Switch to the data section so that the coffsem symbol
       isn't in the text section.  */
    switch_to_section (data_section);

  if (TARGET_LITTLE_ENDIAN)
    fputs ("\t.little\n", asm_out_file);

  if (!TARGET_ELF)
    {
      if (TARGET_SHCOMPACT)
	fputs ("\t.mode\tSHcompact\n", asm_out_file);
      else if (TARGET_SHMEDIA)
	fprintf (asm_out_file, "\t.mode\tSHmedia\n\t.abi\t%i\n",
		 TARGET_SHMEDIA64 ? 64 : 32);
    }
}

/* Check if PAT includes UNSPEC_CALLER unspec pattern.  */

static bool
unspec_caller_rtx_p (rtx pat)
{
  switch (GET_CODE (pat))
    {
    case CONST:
      return unspec_caller_rtx_p (XEXP (pat, 0));
    case PLUS:
    case MINUS:
      if (unspec_caller_rtx_p (XEXP (pat, 0)))
	return true;
      return unspec_caller_rtx_p (XEXP (pat, 1));
    case UNSPEC:
      if (XINT (pat, 1) == UNSPEC_CALLER)
	return true;
    default:
      break;
    }

  return false;
}

/* Indicate that INSN cannot be duplicated.  This is true for insn
   that generates a unique label.  */

static bool
sh_cannot_copy_insn_p (rtx insn)
{
  rtx pat;

  if (!reload_completed || !flag_pic)
    return false;

  if (GET_CODE (insn) != INSN)
    return false;
  if (asm_noperands (insn) >= 0)
    return false;

  pat = PATTERN (insn);
  if (GET_CODE (pat) != SET)
    return false;
  pat = SET_SRC (pat);

  if (unspec_caller_rtx_p (pat))
    return true;

  return false;
}

/* Actual number of instructions used to make a shift by N.  */
static const char ashiftrt_insns[] =
  { 0,1,2,3,4,5,8,8,8,8,8,8,8,8,8,8,2,3,4,5,8,8,8,8,8,8,8,8,8,8,8,2};

/* Left shift and logical right shift are the same.  */
static const char shift_insns[]    =
  { 0,1,1,2,2,3,3,4,1,2,2,3,3,4,3,3,1,2,2,3,3,4,3,3,2,3,3,4,4,4,3,3};

/* Individual shift amounts needed to get the above length sequences.
   One bit right shifts clobber the T bit, so when possible, put one bit
   shifts in the middle of the sequence, so the ends are eligible for
   branch delay slots.  */
static const short shift_amounts[32][5] = {
  {0}, {1}, {2}, {2, 1},
  {2, 2}, {2, 1, 2}, {2, 2, 2}, {2, 2, 1, 2},
  {8}, {8, 1}, {8, 2}, {8, 1, 2},
  {8, 2, 2}, {8, 2, 1, 2}, {8, -2, 8}, {8, -1, 8},
  {16}, {16, 1}, {16, 2}, {16, 1, 2},
  {16, 2, 2}, {16, 2, 1, 2}, {16, -2, 8}, {16, -1, 8},
  {16, 8}, {16, 1, 8}, {16, 8, 2}, {16, 8, 1, 2},
  {16, 8, 2, 2}, {16, -1, -2, 16}, {16, -2, 16}, {16, -1, 16}};

/* Likewise, but for shift amounts < 16, up to three highmost bits
   might be clobbered.  This is typically used when combined with some
   kind of sign or zero extension.  */

static const char ext_shift_insns[]    =
  { 0,1,1,2,2,3,2,2,1,2,2,3,3,3,2,2,1,2,2,3,3,4,3,3,2,3,3,4,4,4,3,3};

static const short ext_shift_amounts[32][4] = {
  {0}, {1}, {2}, {2, 1},
  {2, 2}, {2, 1, 2}, {8, -2}, {8, -1},
  {8}, {8, 1}, {8, 2}, {8, 1, 2},
  {8, 2, 2}, {16, -2, -1}, {16, -2}, {16, -1},
  {16}, {16, 1}, {16, 2}, {16, 1, 2},
  {16, 2, 2}, {16, 2, 1, 2}, {16, -2, 8}, {16, -1, 8},
  {16, 8}, {16, 1, 8}, {16, 8, 2}, {16, 8, 1, 2},
  {16, 8, 2, 2}, {16, -1, -2, 16}, {16, -2, 16}, {16, -1, 16}};

/* Assuming we have a value that has been sign-extended by at least one bit,
   can we use the ext_shift_amounts with the last shift turned to an arithmetic shift
   to shift it by N without data loss, and quicker than by other means?  */
#define EXT_SHIFT_SIGNED(n) (((n) | 8) == 15)

/* This is used in length attributes in sh.md to help compute the length
   of arbitrary constant shift instructions.  */

int
shift_insns_rtx (rtx insn)
{
  rtx set_src = SET_SRC (XVECEXP (PATTERN (insn), 0, 0));
  int shift_count = INTVAL (XEXP (set_src, 1));
  enum rtx_code shift_code = GET_CODE (set_src);

  switch (shift_code)
    {
    case ASHIFTRT:
      return ashiftrt_insns[shift_count];
    case LSHIFTRT:
    case ASHIFT:
      return shift_insns[shift_count];
    default:
      gcc_unreachable ();
    }
}

/* Return the cost of a shift.  */

static inline int
shiftcosts (rtx x)
{
  int value;

  if (TARGET_SHMEDIA)
    return 1;

  if (GET_MODE_SIZE (GET_MODE (x)) > UNITS_PER_WORD)
    {
      if (GET_MODE (x) == DImode
	  && GET_CODE (XEXP (x, 1)) == CONST_INT
	  && INTVAL (XEXP (x, 1)) == 1)
	return 2;

      /* Everything else is invalid, because there is no pattern for it.  */
      return MAX_COST;
    }
  /* If shift by a non constant, then this will be expensive.  */
  if (GET_CODE (XEXP (x, 1)) != CONST_INT)
    return SH_DYNAMIC_SHIFT_COST;

  value = INTVAL (XEXP (x, 1));

  /* Otherwise, return the true cost in instructions.  */
  if (GET_CODE (x) == ASHIFTRT)
    {
      int cost = ashiftrt_insns[value];
      /* If SH3, then we put the constant in a reg and use shad.  */
      if (cost > 1 + SH_DYNAMIC_SHIFT_COST)
	cost = 1 + SH_DYNAMIC_SHIFT_COST;
      return cost;
    }
  else
    return shift_insns[value];
}

/* Return the cost of an AND operation.  */

static inline int
andcosts (rtx x)
{
  int i;

  /* Anding with a register is a single cycle and instruction.  */
  if (GET_CODE (XEXP (x, 1)) != CONST_INT)
    return 1;

  i = INTVAL (XEXP (x, 1));

  if (TARGET_SHMEDIA)
    {
      if (GET_CODE (XEXP (x, 1)) == CONST_INT
	  && (CONST_OK_FOR_I10 (INTVAL (XEXP (x, 1)))
	      || CONST_OK_FOR_J16 (INTVAL (XEXP (x, 1)))))
	return 1;
      else
	return 1 + rtx_cost (XEXP (x, 1), AND);
    }

  /* These constants are single cycle extu.[bw] instructions.  */
  if (i == 0xff || i == 0xffff)
    return 1;
  /* Constants that can be used in an and immediate instruction in a single
     cycle, but this requires r0, so make it a little more expensive.  */
  if (CONST_OK_FOR_K08 (i))
    return 2;
  /* Constants that can be loaded with a mov immediate and an and.
     This case is probably unnecessary.  */
  if (CONST_OK_FOR_I08 (i))
    return 2;
  /* Any other constants requires a 2 cycle pc-relative load plus an and.
     This case is probably unnecessary.  */
  return 3;
}

/* Return the cost of an addition or a subtraction.  */

static inline int
addsubcosts (rtx x)
{
  /* Adding a register is a single cycle insn.  */
  if (GET_CODE (XEXP (x, 1)) == REG
      || GET_CODE (XEXP (x, 1)) == SUBREG)
    return 1;

  /* Likewise for small constants.  */
  if (GET_CODE (XEXP (x, 1)) == CONST_INT
      && CONST_OK_FOR_ADD (INTVAL (XEXP (x, 1))))
    return 1;

  if (TARGET_SHMEDIA)
    switch (GET_CODE (XEXP (x, 1)))
      {
      case CONST:
      case LABEL_REF:
      case SYMBOL_REF:
	return TARGET_SHMEDIA64 ? 5 : 3;

      case CONST_INT:
	if (CONST_OK_FOR_I16 (INTVAL (XEXP (x, 1))))
          return 2;
	else if (CONST_OK_FOR_I16 (INTVAL (XEXP (x, 1)) >> 16))
	  return 3;
	else if (CONST_OK_FOR_I16 ((INTVAL (XEXP (x, 1)) >> 16) >> 16))
	  return 4;

	/* Fall through.  */
      default:
	return 5;
      }

  /* Any other constant requires a 2 cycle pc-relative load plus an
     addition.  */
  return 3;
}

/* Return the cost of a multiply.  */
static inline int
multcosts (rtx x ATTRIBUTE_UNUSED)
{
  if (sh_multcost >= 0)
    return sh_multcost;
  if (TARGET_SHMEDIA)
    /* ??? We have a mul insn, but it has a latency of three, and doesn't
       accept constants.  Ideally, we would use a cost of one or two and
       add the cost of the operand, but disregard the latter when inside loops
       and loop invariant code motion is still to follow.
       Using a multiply first and splitting it later if it's a loss
       doesn't work because of different sign / zero extension semantics
       of multiplies vs. shifts.  */
    return TARGET_SMALLCODE ? 2 : 3;

  if (TARGET_SH2)
    {
      /* We have a mul insn, so we can never take more than the mul and the
	 read of the mac reg, but count more because of the latency and extra
	 reg usage.  */
      if (TARGET_SMALLCODE)
	return 2;
      return 3;
    }

  /* If we're aiming at small code, then just count the number of
     insns in a multiply call sequence.  */
  if (TARGET_SMALLCODE)
    return 5;

  /* Otherwise count all the insns in the routine we'd be calling too.  */
  return 20;
}

/* Compute a (partial) cost for rtx X.  Return true if the complete
   cost has been computed, and false if subexpressions should be
   scanned.  In either case, *TOTAL contains the cost result.  */

static bool
sh_rtx_costs (rtx x, int code, int outer_code, int *total)
{
  switch (code)
    {
    case CONST_INT:
      if (TARGET_SHMEDIA)
        {
	  if (INTVAL (x) == 0)
	    *total = 0;
	  else if (outer_code == AND && and_operand ((x), DImode))
	    *total = 0;
	  else if ((outer_code == IOR || outer_code == XOR
	            || outer_code == PLUS)
		   && CONST_OK_FOR_I10 (INTVAL (x)))
	    *total = 0;
	  else if (CONST_OK_FOR_I16 (INTVAL (x)))
            *total = COSTS_N_INSNS (outer_code != SET);
	  else if (CONST_OK_FOR_I16 (INTVAL (x) >> 16))
	    *total = COSTS_N_INSNS ((outer_code != SET) + 1);
	  else if (CONST_OK_FOR_I16 ((INTVAL (x) >> 16) >> 16))
	    *total = COSTS_N_INSNS ((outer_code != SET) + 2);
          else
	    *total = COSTS_N_INSNS ((outer_code != SET) + 3);
	  return true;
        }
      if (CONST_OK_FOR_I08 (INTVAL (x)))
        *total = 0;
      else if ((outer_code == AND || outer_code == IOR || outer_code == XOR)
	       && CONST_OK_FOR_K08 (INTVAL (x)))
        *total = 1;
      else
        *total = 8;
      return true;

    case CONST:
    case LABEL_REF:
    case SYMBOL_REF:
      if (TARGET_SHMEDIA64)
        *total = COSTS_N_INSNS (4);
      else if (TARGET_SHMEDIA32)
        *total = COSTS_N_INSNS (2);
      else
	*total = 5;
      return true;

    case CONST_DOUBLE:
      if (TARGET_SHMEDIA)
        *total = COSTS_N_INSNS (4);
      else
        *total = 10;
      return true;
    case CONST_VECTOR:
      if (x == CONST0_RTX (GET_MODE (x)))
	*total = 0;
      else if (sh_1el_vec (x, VOIDmode))
	*total = outer_code != SET;
      if (sh_rep_vec (x, VOIDmode))
	*total = ((GET_MODE_UNIT_SIZE (GET_MODE (x)) + 3) / 4
		  + (outer_code != SET));
      *total = COSTS_N_INSNS (3) + (outer_code != SET);
      return true;

    case PLUS:
    case MINUS:
      *total = COSTS_N_INSNS (addsubcosts (x));
      return true;

    case AND:
      *total = COSTS_N_INSNS (andcosts (x));
      return true;

    case MULT:
      *total = COSTS_N_INSNS (multcosts (x));
      return true;

    case ASHIFT:
    case ASHIFTRT:
    case LSHIFTRT:
      *total = COSTS_N_INSNS (shiftcosts (x));
      return true;

    case DIV:
    case UDIV:
    case MOD:
    case UMOD:
      *total = COSTS_N_INSNS (20);
      return true;

    case PARALLEL:
      if (sh_1el_vec (x, VOIDmode))
	*total = outer_code != SET;
      if (sh_rep_vec (x, VOIDmode))
	*total = ((GET_MODE_UNIT_SIZE (GET_MODE (x)) + 3) / 4
		  + (outer_code != SET));
      *total = COSTS_N_INSNS (3) + (outer_code != SET);
      return true;

    case FLOAT:
    case FIX:
      *total = 100;
      return true;

    default:
      return false;
    }
}

/* Compute the cost of an address.  For the SH, all valid addresses are
   the same cost.  Use a slightly higher cost for reg + reg addressing,
   since it increases pressure on r0.  */

static int
sh_address_cost (rtx X)
{
  return (GET_CODE (X) == PLUS
	  && ! CONSTANT_P (XEXP (X, 1))
	  && ! TARGET_SHMEDIA ? 1 : 0);
}

/* Code to expand a shift.  */

void
gen_ashift (int type, int n, rtx reg)
{
  /* Negative values here come from the shift_amounts array.  */
  if (n < 0)
    {
      if (type == ASHIFT)
	type = LSHIFTRT;
      else
	type = ASHIFT;
      n = -n;
    }

  switch (type)
    {
    case ASHIFTRT:
      emit_insn (gen_ashrsi3_k (reg, reg, GEN_INT (n)));
      break;
    case LSHIFTRT:
      if (n == 1)
	emit_insn (gen_lshrsi3_m (reg, reg, GEN_INT (n)));
      else
	emit_insn (gen_lshrsi3_k (reg, reg, GEN_INT (n)));
      break;
    case ASHIFT:
      emit_insn (gen_ashlsi3_std (reg, reg, GEN_INT (n)));
      break;
    }
}

/* Same for HImode */

void
gen_ashift_hi (int type, int n, rtx reg)
{
  /* Negative values here come from the shift_amounts array.  */
  if (n < 0)
    {
      if (type == ASHIFT)
	type = LSHIFTRT;
      else
	type = ASHIFT;
      n = -n;
    }

  switch (type)
    {
    case ASHIFTRT:
    case LSHIFTRT:
      /* We don't have HImode right shift operations because using the
	 ordinary 32 bit shift instructions for that doesn't generate proper
	 zero/sign extension.
	 gen_ashift_hi is only called in contexts where we know that the
	 sign extension works out correctly.  */
      {
	int offset = 0;
	if (GET_CODE (reg) == SUBREG)
	  {
	    offset = SUBREG_BYTE (reg);
	    reg = SUBREG_REG (reg);
	  }
	gen_ashift (type, n, gen_rtx_SUBREG (SImode, reg, offset));
	break;
      }
    case ASHIFT:
      emit_insn (gen_ashlhi3_k (reg, reg, GEN_INT (n)));
      break;
    }
}

/* Output RTL to split a constant shift into its component SH constant
   shift instructions.  */

void
gen_shifty_op (int code, rtx *operands)
{
  int value = INTVAL (operands[2]);
  int max, i;

  /* Truncate the shift count in case it is out of bounds.  */
  value = value & 0x1f;

  if (value == 31)
    {
      if (code == LSHIFTRT)
	{
	  emit_insn (gen_rotlsi3_1 (operands[0], operands[0]));
	  emit_insn (gen_movt (operands[0]));
	  return;
	}
      else if (code == ASHIFT)
	{
	  /* There is a two instruction sequence for 31 bit left shifts,
	     but it requires r0.  */
	  if (GET_CODE (operands[0]) == REG && REGNO (operands[0]) == 0)
	    {
	      emit_insn (gen_andsi3 (operands[0], operands[0], const1_rtx));
	      emit_insn (gen_rotlsi3_31 (operands[0], operands[0]));
	      return;
	    }
	}
    }
  else if (value == 0)
    {
      /* This can happen even when optimizing, if there were subregs before
	 reload.  Don't output a nop here, as this is never optimized away;
	 use a no-op move instead.  */
      emit_insn (gen_rtx_SET (VOIDmode, operands[0], operands[0]));
      return;
    }

  max = shift_insns[value];
  for (i = 0; i < max; i++)
    gen_ashift (code, shift_amounts[value][i], operands[0]);
}

/* Same as above, but optimized for values where the topmost bits don't
   matter.  */

void
gen_shifty_hi_op (int code, rtx *operands)
{
  int value = INTVAL (operands[2]);
  int max, i;
  void (*gen_fun) (int, int, rtx);

  /* This operation is used by and_shl for SImode values with a few
     high bits known to be cleared.  */
  value &= 31;
  if (value == 0)
    {
      emit_insn (gen_nop ());
      return;
    }

  gen_fun = GET_MODE (operands[0]) == HImode ? gen_ashift_hi : gen_ashift;
  if (code == ASHIFT)
    {
      max = ext_shift_insns[value];
      for (i = 0; i < max; i++)
	gen_fun (code, ext_shift_amounts[value][i], operands[0]);
    }
  else
    /* When shifting right, emit the shifts in reverse order, so that
       solitary negative values come first.  */
    for (i = ext_shift_insns[value] - 1; i >= 0; i--)
      gen_fun (code, ext_shift_amounts[value][i], operands[0]);
}

/* Output RTL for an arithmetic right shift.  */

/* ??? Rewrite to use super-optimizer sequences.  */

int
expand_ashiftrt (rtx *operands)
{
  rtx wrk;
  char func[18];
  int value;

  if (TARGET_SH3)
    {
      if (GET_CODE (operands[2]) != CONST_INT)
	{
	  rtx count = copy_to_mode_reg (SImode, operands[2]);
	  emit_insn (gen_negsi2 (count, count));
	  emit_insn (gen_ashrsi3_d (operands[0], operands[1], count));
	  return 1;
	}
      else if (ashiftrt_insns[INTVAL (operands[2]) & 31]
	       > 1 + SH_DYNAMIC_SHIFT_COST)
	{
	  rtx count
	    = force_reg (SImode, GEN_INT (- (INTVAL (operands[2]) & 31)));
	  emit_insn (gen_ashrsi3_d (operands[0], operands[1], count));
	  return 1;
	}
    }
  if (GET_CODE (operands[2]) != CONST_INT)
    return 0;

  value = INTVAL (operands[2]) & 31;

  if (value == 31)
    {
      /* If we are called from abs expansion, arrange things so that we
	 we can use a single MT instruction that doesn't clobber the source,
	 if LICM can hoist out the load of the constant zero.  */
      if (currently_expanding_to_rtl)
	{
	  emit_insn (gen_cmpgtsi_t (force_reg (SImode, CONST0_RTX (SImode)),
				    operands[1]));
	  emit_insn (gen_mov_neg_si_t (operands[0]));
	  return 1;
	}
      emit_insn (gen_ashrsi2_31 (operands[0], operands[1]));
      return 1;
    }
  else if (value >= 16 && value <= 19)
    {
      wrk = gen_reg_rtx (SImode);
      emit_insn (gen_ashrsi2_16 (wrk, operands[1]));
      value -= 16;
      while (value--)
	gen_ashift (ASHIFTRT, 1, wrk);
      emit_move_insn (operands[0], wrk);
      return 1;
    }
  /* Expand a short sequence inline, longer call a magic routine.  */
  else if (value <= 5)
    {
      wrk = gen_reg_rtx (SImode);
      emit_move_insn (wrk, operands[1]);
      while (value--)
	gen_ashift (ASHIFTRT, 1, wrk);
      emit_move_insn (operands[0], wrk);
      return 1;
    }

  wrk = gen_reg_rtx (Pmode);

  /* Load the value into an arg reg and call a helper.  */
  emit_move_insn (gen_rtx_REG (SImode, 4), operands[1]);
  sprintf (func, "__ashiftrt_r4_%d", value);
  function_symbol (wrk, func, SFUNC_STATIC);
  emit_insn (gen_ashrsi3_n (GEN_INT (value), wrk));
  emit_move_insn (operands[0], gen_rtx_REG (SImode, 4));
  return 1;
}

int
sh_dynamicalize_shift_p (rtx count)
{
  return shift_insns[INTVAL (count)] > 1 + SH_DYNAMIC_SHIFT_COST;
}

/* Try to find a good way to implement the combiner pattern
  [(set (match_operand:SI 0 "register_operand" "r")
        (and:SI (ashift:SI (match_operand:SI 1 "register_operand" "r")
                           (match_operand:SI 2 "const_int_operand" "n"))
                (match_operand:SI 3 "const_int_operand" "n"))) .
  LEFT_RTX is operand 2 in the above pattern, and MASK_RTX is operand 3.
  return 0 for simple right / left or left/right shift combination.
  return 1 for a combination of shifts with zero_extend.
  return 2 for a combination of shifts with an AND that needs r0.
  return 3 for a combination of shifts with an AND that needs an extra
    scratch register, when the three highmost bits of the AND mask are clear.
  return 4 for a combination of shifts with an AND that needs an extra
    scratch register, when any of the three highmost bits of the AND mask
    is set.
  If ATTRP is set, store an initial right shift width in ATTRP[0],
  and the instruction length in ATTRP[1] .  These values are not valid
  when returning 0.
  When ATTRP is set and returning 1, ATTRP[2] gets set to the index into
  shift_amounts for the last shift value that is to be used before the
  sign extend.  */
int
shl_and_kind (rtx left_rtx, rtx mask_rtx, int *attrp)
{
  unsigned HOST_WIDE_INT mask, lsb, mask2, lsb2;
  int left = INTVAL (left_rtx), right;
  int best = 0;
  int cost, best_cost = 10000;
  int best_right = 0, best_len = 0;
  int i;
  int can_ext;

  if (left < 0 || left > 31)
    return 0;
  if (GET_CODE (mask_rtx) == CONST_INT)
    mask = (unsigned HOST_WIDE_INT) INTVAL (mask_rtx) >> left;
  else
    mask = (unsigned HOST_WIDE_INT) GET_MODE_MASK (SImode) >> left;
  /* Can this be expressed as a right shift / left shift pair?  */
  lsb = ((mask ^ (mask - 1)) >> 1) + 1;
  right = exact_log2 (lsb);
  mask2 = ~(mask + lsb - 1);
  lsb2 = ((mask2 ^ (mask2 - 1)) >> 1) + 1;
  /* mask has no zeroes but trailing zeroes <==> ! mask2 */
  if (! mask2)
    best_cost = shift_insns[right] + shift_insns[right + left];
  /* mask has no trailing zeroes <==> ! right */
  else if (! right && mask2 == ~(lsb2 - 1))
    {
      int late_right = exact_log2 (lsb2);
      best_cost = shift_insns[left + late_right] + shift_insns[late_right];
    }
  /* Try to use zero extend.  */
  if (mask2 == ~(lsb2 - 1))
    {
      int width, first;

      for (width = 8; width <= 16; width += 8)
	{
	  /* Can we zero-extend right away?  */
	  if (lsb2 == (unsigned HOST_WIDE_INT) 1 << width)
	    {
	      cost
		= 1 + ext_shift_insns[right] + ext_shift_insns[left + right];
	      if (cost < best_cost)
		{
		  best = 1;
		  best_cost = cost;
		  best_right = right;
		  best_len = cost;
		  if (attrp)
		    attrp[2] = -1;
		}
	      continue;
	    }
	  /* ??? Could try to put zero extend into initial right shift,
	     or even shift a bit left before the right shift.  */
	  /* Determine value of first part of left shift, to get to the
	     zero extend cut-off point.  */
	  first = width - exact_log2 (lsb2) + right;
	  if (first >= 0 && right + left - first >= 0)
	    {
	      cost = ext_shift_insns[right] + ext_shift_insns[first] + 1
		+ ext_shift_insns[right + left - first];
	      if (cost < best_cost)
		{
		  best = 1;
		  best_cost = cost;
		  best_right = right;
		  best_len = cost;
		  if (attrp)
		    attrp[2] = first;
		}
	    }
	}
    }
  /* Try to use r0 AND pattern */
  for (i = 0; i <= 2; i++)
    {
      if (i > right)
	break;
      if (! CONST_OK_FOR_K08 (mask >> i))
	continue;
      cost = (i != 0) + 2 + ext_shift_insns[left + i];
      if (cost < best_cost)
	{
	  best = 2;
	  best_cost = cost;
	  best_right = i;
	  best_len = cost - 1;
	}
    }
  /* Try to use a scratch register to hold the AND operand.  */
  can_ext = ((mask << left) & ((unsigned HOST_WIDE_INT) 3 << 30)) == 0;
  for (i = 0; i <= 2; i++)
    {
      if (i > right)
	break;
      cost = (i != 0) + (CONST_OK_FOR_I08 (mask >> i) ? 2 : 3)
	+ (can_ext ? ext_shift_insns : shift_insns)[left + i];
      if (cost < best_cost)
	{
	  best = 4 - can_ext;
	  best_cost = cost;
	  best_right = i;
	  best_len = cost - 1 - ! CONST_OK_FOR_I08 (mask >> i);
	}
    }

  if (attrp)
    {
      attrp[0] = best_right;
      attrp[1] = best_len;
    }
  return best;
}

/* This is used in length attributes of the unnamed instructions
   corresponding to shl_and_kind return values of 1 and 2.  */
int
shl_and_length (rtx insn)
{
  rtx set_src, left_rtx, mask_rtx;
  int attributes[3];

  set_src = SET_SRC (XVECEXP (PATTERN (insn), 0, 0));
  left_rtx = XEXP (XEXP (set_src, 0), 1);
  mask_rtx = XEXP (set_src, 1);
  shl_and_kind (left_rtx, mask_rtx, attributes);
  return attributes[1];
}

/* This is used in length attribute of the and_shl_scratch instruction.  */

int
shl_and_scr_length (rtx insn)
{
  rtx set_src = SET_SRC (XVECEXP (PATTERN (insn), 0, 0));
  int len = shift_insns[INTVAL (XEXP (set_src, 1))];
  rtx op = XEXP (set_src, 0);
  len += shift_insns[INTVAL (XEXP (op, 1))] + 1;
  op = XEXP (XEXP (op, 0), 0);
  return len + shift_insns[INTVAL (XEXP (op, 1))];
}

/* Generate rtl for instructions for which shl_and_kind advised a particular
   method of generating them, i.e. returned zero.  */

int
gen_shl_and (rtx dest, rtx left_rtx, rtx mask_rtx, rtx source)
{
  int attributes[3];
  unsigned HOST_WIDE_INT mask;
  int kind = shl_and_kind (left_rtx, mask_rtx, attributes);
  int right, total_shift;
  void (*shift_gen_fun) (int, rtx *) = gen_shifty_hi_op;

  right = attributes[0];
  total_shift = INTVAL (left_rtx) + right;
  mask = (unsigned HOST_WIDE_INT) INTVAL (mask_rtx) >> total_shift;
  switch (kind)
    {
    default:
      return -1;
    case 1:
      {
	int first = attributes[2];
	rtx operands[3];

	if (first < 0)
	  {
	    emit_insn ((mask << right) <= 0xff
		       ? gen_zero_extendqisi2 (dest,
					       gen_lowpart (QImode, source))
		       : gen_zero_extendhisi2 (dest,
					       gen_lowpart (HImode, source)));
	    source = dest;
	  }
	if (source != dest)
	  emit_insn (gen_movsi (dest, source));
	operands[0] = dest;
	if (right)
	  {
	    operands[2] = GEN_INT (right);
	    gen_shifty_hi_op (LSHIFTRT, operands);
	  }
	if (first > 0)
	  {
	    operands[2] = GEN_INT (first);
	    gen_shifty_hi_op (ASHIFT, operands);
	    total_shift -= first;
	    mask <<= first;
	  }
	if (first >= 0)
	  emit_insn (mask <= 0xff
		     ? gen_zero_extendqisi2 (dest, gen_lowpart (QImode, dest))
		     : gen_zero_extendhisi2 (dest, gen_lowpart (HImode, dest)));
	if (total_shift > 0)
	  {
	    operands[2] = GEN_INT (total_shift);
	    gen_shifty_hi_op (ASHIFT, operands);
	  }
	break;
      }
    case 4:
      shift_gen_fun = gen_shifty_op;
    case 3:
      /* If the topmost bit that matters is set, set the topmost bits
	 that don't matter.  This way, we might be able to get a shorter
	 signed constant.  */
      if (mask & ((HOST_WIDE_INT) 1 << (31 - total_shift)))
	mask |= (HOST_WIDE_INT) ~0 << (31 - total_shift);
    case 2:
      /* Don't expand fine-grained when combining, because that will
         make the pattern fail.  */
      if (currently_expanding_to_rtl
	  || reload_in_progress || reload_completed)
	{
	  rtx operands[3];

	  /* Cases 3 and 4 should be handled by this split
	     only while combining  */
	  gcc_assert (kind <= 2);
	  if (right)
	    {
	      emit_insn (gen_lshrsi3 (dest, source, GEN_INT (right)));
	      source = dest;
	    }
	  emit_insn (gen_andsi3 (dest, source, GEN_INT (mask)));
	  if (total_shift)
	    {
	      operands[0] = dest;
	      operands[1] = dest;
	      operands[2] = GEN_INT (total_shift);
	      shift_gen_fun (ASHIFT, operands);
	    }
	  break;
	}
      else
	{
	  int neg = 0;
	  if (kind != 4 && total_shift < 16)
	    {
	      neg = -ext_shift_amounts[total_shift][1];
	      if (neg > 0)
		neg -= ext_shift_amounts[total_shift][2];
	      else
		neg = 0;
	    }
	  emit_insn (gen_and_shl_scratch (dest, source,
					  GEN_INT (right),
					  GEN_INT (mask),
					  GEN_INT (total_shift + neg),
					  GEN_INT (neg)));
	  emit_insn (gen_movsi (dest, dest));
	  break;
	}
    }
  return 0;
}

/* Try to find a good way to implement the combiner pattern
  [(set (match_operand:SI 0 "register_operand" "=r")
        (sign_extract:SI (ashift:SI (match_operand:SI 1 "register_operand" "r")
                                    (match_operand:SI 2 "const_int_operand" "n")
                         (match_operand:SI 3 "const_int_operand" "n")
                         (const_int 0)))
   (clobber (reg:SI T_REG))]
  LEFT_RTX is operand 2 in the above pattern, and SIZE_RTX is operand 3.
  return 0 for simple left / right shift combination.
  return 1 for left shift / 8 bit sign extend / left shift.
  return 2 for left shift / 16 bit sign extend / left shift.
  return 3 for left shift / 8 bit sign extend / shift / sign extend.
  return 4 for left shift / 16 bit sign extend / shift / sign extend.
  return 5 for left shift / 16 bit sign extend / right shift
  return 6 for < 8 bit sign extend / left shift.
  return 7 for < 8 bit sign extend / left shift / single right shift.
  If COSTP is nonzero, assign the calculated cost to *COSTP.  */

int
shl_sext_kind (rtx left_rtx, rtx size_rtx, int *costp)
{
  int left, size, insize, ext;
  int cost = 0, best_cost;
  int kind;

  left = INTVAL (left_rtx);
  size = INTVAL (size_rtx);
  insize = size - left;
  gcc_assert (insize > 0);
  /* Default to left / right shift.  */
  kind = 0;
  best_cost = shift_insns[32 - insize] + ashiftrt_insns[32 - size];
  if (size <= 16)
    {
      /* 16 bit shift / sign extend / 16 bit shift */
      cost = shift_insns[16 - insize] + 1 + ashiftrt_insns[16 - size];
      /* If ashiftrt_insns[16 - size] is 8, this choice will be overridden
	 below, by alternative 3 or something even better.  */
      if (cost < best_cost)
	{
	  kind = 5;
	  best_cost = cost;
	}
    }
  /* Try a plain sign extend between two shifts.  */
  for (ext = 16; ext >= insize; ext -= 8)
    {
      if (ext <= size)
	{
	  cost = ext_shift_insns[ext - insize] + 1 + shift_insns[size - ext];
	  if (cost < best_cost)
	    {
	      kind = ext / (unsigned) 8;
	      best_cost = cost;
	    }
	}
      /* Check if we can do a sloppy shift with a final signed shift
	 restoring the sign.  */
      if (EXT_SHIFT_SIGNED (size - ext))
	cost = ext_shift_insns[ext - insize] + ext_shift_insns[size - ext] + 1;
      /* If not, maybe it's still cheaper to do the second shift sloppy,
	 and do a final sign extend?  */
      else if (size <= 16)
	cost = ext_shift_insns[ext - insize] + 1
	  + ext_shift_insns[size > ext ? size - ext : ext - size] + 1;
      else
	continue;
      if (cost < best_cost)
	{
	  kind = ext / (unsigned) 8 + 2;
	  best_cost = cost;
	}
    }
  /* Check if we can sign extend in r0 */
  if (insize < 8)
    {
      cost = 3 + shift_insns[left];
      if (cost < best_cost)
	{
	  kind = 6;
	  best_cost = cost;
	}
      /* Try the same with a final signed shift.  */
      if (left < 31)
	{
	  cost = 3 + ext_shift_insns[left + 1] + 1;
	  if (cost < best_cost)
	    {
	      kind = 7;
	      best_cost = cost;
	    }
	}
    }
  if (TARGET_SH3)
    {
      /* Try to use a dynamic shift.  */
      cost = shift_insns[32 - insize] + 1 + SH_DYNAMIC_SHIFT_COST;
      if (cost < best_cost)
	{
	  kind = 0;
	  best_cost = cost;
	}
    }
  if (costp)
    *costp = cost;
  return kind;
}

/* Function to be used in the length attribute of the instructions
   implementing this pattern.  */

int
shl_sext_length (rtx insn)
{
  rtx set_src, left_rtx, size_rtx;
  int cost;

  set_src = SET_SRC (XVECEXP (PATTERN (insn), 0, 0));
  left_rtx = XEXP (XEXP (set_src, 0), 1);
  size_rtx = XEXP (set_src, 1);
  shl_sext_kind (left_rtx, size_rtx, &cost);
  return cost;
}

/* Generate rtl for this pattern */

int
gen_shl_sext (rtx dest, rtx left_rtx, rtx size_rtx, rtx source)
{
  int kind;
  int left, size, insize, cost;
  rtx operands[3];

  kind = shl_sext_kind (left_rtx, size_rtx, &cost);
  left = INTVAL (left_rtx);
  size = INTVAL (size_rtx);
  insize = size - left;
  switch (kind)
    {
    case 1:
    case 2:
    case 3:
    case 4:
      {
	int ext = kind & 1 ? 8 : 16;
	int shift2 = size - ext;

	/* Don't expand fine-grained when combining, because that will
	   make the pattern fail.  */
	if (! currently_expanding_to_rtl
	    && ! reload_in_progress && ! reload_completed)
	  {
	    emit_insn (gen_shl_sext_ext (dest, source, left_rtx, size_rtx));
	    emit_insn (gen_movsi (dest, source));
	    break;
	  }
	if (dest != source)
	  emit_insn (gen_movsi (dest, source));
	operands[0] = dest;
	if (ext - insize)
	  {
	    operands[2] = GEN_INT (ext - insize);
	    gen_shifty_hi_op (ASHIFT, operands);
	  }
	emit_insn (kind & 1
		   ? gen_extendqisi2 (dest, gen_lowpart (QImode, dest))
		   : gen_extendhisi2 (dest, gen_lowpart (HImode, dest)));
	if (kind <= 2)
	  {
	    if (shift2)
	      {
		operands[2] = GEN_INT (shift2);
		gen_shifty_op (ASHIFT, operands);
	      }
	  }
	else
	  {
	    if (shift2 > 0)
	      {
		if (EXT_SHIFT_SIGNED (shift2))
		  {
		    operands[2] = GEN_INT (shift2 + 1);
		    gen_shifty_op (ASHIFT, operands);
		    operands[2] = const1_rtx;
		    gen_shifty_op (ASHIFTRT, operands);
		    break;
		  }
		operands[2] = GEN_INT (shift2);
		gen_shifty_hi_op (ASHIFT, operands);
	      }
	    else if (shift2)
	      {
		operands[2] = GEN_INT (-shift2);
		gen_shifty_hi_op (LSHIFTRT, operands);
	      }
	    emit_insn (size <= 8
		       ? gen_extendqisi2 (dest, gen_lowpart (QImode, dest))
		       : gen_extendhisi2 (dest, gen_lowpart (HImode, dest)));
	  }
	break;
      }
    case 5:
      {
	int i = 16 - size;
	if (! currently_expanding_to_rtl
	    && ! reload_in_progress && ! reload_completed)
	  emit_insn (gen_shl_sext_ext (dest, source, left_rtx, size_rtx));
	else
	  {
	    operands[0] = dest;
	    operands[2] = GEN_INT (16 - insize);
	    gen_shifty_hi_op (ASHIFT, operands);
	    emit_insn (gen_extendhisi2 (dest, gen_lowpart (HImode, dest)));
	  }
	/* Don't use gen_ashrsi3 because it generates new pseudos.  */
	while (--i >= 0)
	  gen_ashift (ASHIFTRT, 1, dest);
	break;
      }
    case 6:
    case 7:
      /* Don't expand fine-grained when combining, because that will
	 make the pattern fail.  */
      if (! currently_expanding_to_rtl
	  && ! reload_in_progress && ! reload_completed)
	{
	  emit_insn (gen_shl_sext_ext (dest, source, left_rtx, size_rtx));
	  emit_insn (gen_movsi (dest, source));
	  break;
	}
      emit_insn (gen_andsi3 (dest, source, GEN_INT ((1 << insize) - 1)));
      emit_insn (gen_xorsi3 (dest, dest, GEN_INT (1 << (insize - 1))));
      emit_insn (gen_addsi3 (dest, dest, GEN_INT (-1 << (insize - 1))));
      operands[0] = dest;
      operands[2] = kind == 7 ? GEN_INT (left + 1) : left_rtx;
      gen_shifty_op (ASHIFT, operands);
      if (kind == 7)
	emit_insn (gen_ashrsi3_k (dest, dest, const1_rtx));
      break;
    default:
      return -1;
    }
  return 0;
}

/* Prefix a symbol_ref name with "datalabel".  */

rtx
gen_datalabel_ref (rtx sym)
{
  const char *str;

  if (GET_CODE (sym) == LABEL_REF)
    return gen_rtx_CONST (GET_MODE (sym),
			  gen_rtx_UNSPEC (GET_MODE (sym),
					  gen_rtvec (1, sym),
					  UNSPEC_DATALABEL));

  gcc_assert (GET_CODE (sym) == SYMBOL_REF);

  str = XSTR (sym, 0);
  /* Share all SYMBOL_REF strings with the same value - that is important
     for cse.  */
  str = IDENTIFIER_POINTER (get_identifier (str));
  XSTR (sym, 0) = str;

  return sym;
}


static alloc_pool label_ref_list_pool;

typedef struct label_ref_list_d
{
  rtx label;
  struct label_ref_list_d *next;
} *label_ref_list_t;

/* The SH cannot load a large constant into a register, constants have to
   come from a pc relative load.  The reference of a pc relative load
   instruction must be less than 1k in front of the instruction.  This
   means that we often have to dump a constant inside a function, and
   generate code to branch around it.

   It is important to minimize this, since the branches will slow things
   down and make things bigger.

   Worst case code looks like:

   mov.l L1,rn
   bra   L2
   nop
   align
   L1:   .long value
   L2:
   ..

   mov.l L3,rn
   bra   L4
   nop
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
   the first move can be shrunk from the 3 insn+constant sequence into
   just 1 insn, and the constant moved to L3 to make:

   mov.l        L1,rn
   ..
   mov.l        L3,rn
   bra          L4
   nop
   align
   L3:.long value
   L4:.long value

   Then the second move becomes the target for the shortening process.  */

typedef struct
{
  rtx value;			/* Value in table.  */
  rtx label;			/* Label of value.  */
  label_ref_list_t wend;	/* End of window.  */
  enum machine_mode mode;	/* Mode of value.  */

  /* True if this constant is accessed as part of a post-increment
     sequence.  Note that HImode constants are never accessed in this way.  */
  bool part_of_sequence_p;
} pool_node;

/* The maximum number of constants that can fit into one pool, since
   constants in the range 0..510 are at least 2 bytes long, and in the
   range from there to 1018 at least 4 bytes.  */

#define MAX_POOL_SIZE 372
static pool_node pool_vector[MAX_POOL_SIZE];
static int pool_size;
static rtx pool_window_label;
static int pool_window_last;

static int max_labelno_before_reorg;

/* ??? If we need a constant in HImode which is the truncated value of a
   constant we need in SImode, we could combine the two entries thus saving
   two bytes.  Is this common enough to be worth the effort of implementing
   it?  */

/* ??? This stuff should be done at the same time that we shorten branches.
   As it is now, we must assume that all branches are the maximum size, and
   this causes us to almost always output constant pools sooner than
   necessary.  */

/* Add a constant to the pool and return its label.  */

static rtx
add_constant (rtx x, enum machine_mode mode, rtx last_value)
{
  int i;
  rtx lab, new;
  label_ref_list_t ref, newref;

  /* First see if we've already got it.  */
  for (i = 0; i < pool_size; i++)
    {
      if (x->code == pool_vector[i].value->code
	  && mode == pool_vector[i].mode)
	{
	  if (x->code == CODE_LABEL)
	    {
	      if (XINT (x, 3) != XINT (pool_vector[i].value, 3))
		continue;
	    }
	  if (rtx_equal_p (x, pool_vector[i].value))
	    {
	      lab = new = 0;
	      if (! last_value
		  || ! i
		  || ! rtx_equal_p (last_value, pool_vector[i-1].value))
		{
		  new = gen_label_rtx ();
		  LABEL_REFS (new) = pool_vector[i].label;
		  pool_vector[i].label = lab = new;
		}
	      if (lab && pool_window_label)
		{
		  newref = (label_ref_list_t) pool_alloc (label_ref_list_pool);
		  newref->label = pool_window_label;
		  ref = pool_vector[pool_window_last].wend;
		  newref->next = ref;
		  pool_vector[pool_window_last].wend = newref;
		}
	      if (new)
		pool_window_label = new;
	      pool_window_last = i;
	      return lab;
	    }
	}
    }

  /* Need a new one.  */
  pool_vector[pool_size].value = x;
  if (last_value && rtx_equal_p (last_value, pool_vector[pool_size - 1].value))
    {
      lab = 0;
      pool_vector[pool_size - 1].part_of_sequence_p = true;
    }
  else
    lab = gen_label_rtx ();
  pool_vector[pool_size].mode = mode;
  pool_vector[pool_size].label = lab;
  pool_vector[pool_size].wend = NULL;
  pool_vector[pool_size].part_of_sequence_p = (lab == 0);
  if (lab && pool_window_label)
    {
      newref = (label_ref_list_t) pool_alloc (label_ref_list_pool);
      newref->label = pool_window_label;
      ref = pool_vector[pool_window_last].wend;
      newref->next = ref;
      pool_vector[pool_window_last].wend = newref;
    }
  if (lab)
    pool_window_label = lab;
  pool_window_last = pool_size;
  pool_size++;
  return lab;
}

/* Output the literal table.  START, if nonzero, is the first instruction
   this table is needed for, and also indicates that there is at least one
   casesi_worker_2 instruction; We have to emit the operand3 labels from
   these insns at a 4-byte  aligned position.  BARRIER is the barrier
   after which we are to place the table.  */

static void
dump_table (rtx start, rtx barrier)
{
  rtx scan = barrier;
  int i;
  int need_align = 1;
  rtx lab;
  label_ref_list_t ref;
  int have_df = 0;

  /* Do two passes, first time dump out the HI sized constants.  */

  for (i = 0; i < pool_size; i++)
    {
      pool_node *p = &pool_vector[i];

      if (p->mode == HImode)
	{
	  if (need_align)
	    {
	      scan = emit_insn_after (gen_align_2 (), scan);
	      need_align = 0;
	    }
	  for (lab = p->label; lab; lab = LABEL_REFS (lab))
	    scan = emit_label_after (lab, scan);
	  scan = emit_insn_after (gen_consttable_2 (p->value, const0_rtx),
				  scan);
	  for (ref = p->wend; ref; ref = ref->next)
	    {
	      lab = ref->label;
	      scan = emit_insn_after (gen_consttable_window_end (lab), scan);
	    }
	}
      else if (p->mode == DFmode)
	have_df = 1;
    }

  need_align = 1;

  if (start)
    {
      scan = emit_insn_after (gen_align_4 (), scan);
      need_align = 0;
      for (; start != barrier; start = NEXT_INSN (start))
	if (GET_CODE (start) == INSN
	    && recog_memoized (start) == CODE_FOR_casesi_worker_2)
	  {
	    rtx src = SET_SRC (XVECEXP (PATTERN (start), 0, 0));
	    rtx lab = XEXP (XVECEXP (src, 0, 3), 0);

	    scan = emit_label_after (lab, scan);
	  }
    }
  if (TARGET_FMOVD && TARGET_ALIGN_DOUBLE && have_df)
    {
      rtx align_insn = NULL_RTX;

      scan = emit_label_after (gen_label_rtx (), scan);
      scan = emit_insn_after (gen_align_log (GEN_INT (3)), scan);
      need_align = 0;

      for (i = 0; i < pool_size; i++)
	{
	  pool_node *p = &pool_vector[i];

	  switch (p->mode)
	    {
	    case HImode:
	      break;
	    case SImode:
	    case SFmode:
	      if (align_insn && !p->part_of_sequence_p)
		{
		  for (lab = p->label; lab; lab = LABEL_REFS (lab))
		    emit_label_before (lab, align_insn);
		  emit_insn_before (gen_consttable_4 (p->value, const0_rtx),
				    align_insn);
		  for (ref = p->wend; ref; ref = ref->next)
		    {
		      lab = ref->label;
		      emit_insn_before (gen_consttable_window_end (lab),
					align_insn);
		    }
		  delete_insn (align_insn);
		  align_insn = NULL_RTX;
		  continue;
		}
	      else
		{
		  for (lab = p->label; lab; lab = LABEL_REFS (lab))
		    scan = emit_label_after (lab, scan);
		  scan = emit_insn_after (gen_consttable_4 (p->value,
							    const0_rtx), scan);
		  need_align = ! need_align;
		}
	      break;
	    case DFmode:
	      if (need_align)
		{
		  scan = emit_insn_after (gen_align_log (GEN_INT (3)), scan);
		  align_insn = scan;
		  need_align = 0;
		}
	    case DImode:
	      for (lab = p->label; lab; lab = LABEL_REFS (lab))
		scan = emit_label_after (lab, scan);
	      scan = emit_insn_after (gen_consttable_8 (p->value, const0_rtx),
				      scan);
	      break;
	    default:
	      gcc_unreachable ();
	    }

	  if (p->mode != HImode)
	    {
	      for (ref = p->wend; ref; ref = ref->next)
		{
		  lab = ref->label;
		  scan = emit_insn_after (gen_consttable_window_end (lab),
					  scan);
		}
	    }
	}

      pool_size = 0;
    }

  for (i = 0; i < pool_size; i++)
    {
      pool_node *p = &pool_vector[i];

      switch (p->mode)
	{
	case HImode:
	  break;
	case SImode:
	case SFmode:
	  if (need_align)
	    {
	      need_align = 0;
	      scan = emit_label_after (gen_label_rtx (), scan);
	      scan = emit_insn_after (gen_align_4 (), scan);
	    }
	  for (lab = p->label; lab; lab = LABEL_REFS (lab))
	    scan = emit_label_after (lab, scan);
	  scan = emit_insn_after (gen_consttable_4 (p->value, const0_rtx),
				  scan);
	  break;
	case DFmode:
	case DImode:
	  if (need_align)
	    {
	      need_align = 0;
	      scan = emit_label_after (gen_label_rtx (), scan);
	      scan = emit_insn_after (gen_align_4 (), scan);
	    }
	  for (lab = p->label; lab; lab = LABEL_REFS (lab))
	    scan = emit_label_after (lab, scan);
	  scan = emit_insn_after (gen_consttable_8 (p->value, const0_rtx),
				  scan);
	  break;
	default:
	  gcc_unreachable ();
	}

      if (p->mode != HImode)
	{
	  for (ref = p->wend; ref; ref = ref->next)
	    {
	      lab = ref->label;
	      scan = emit_insn_after (gen_consttable_window_end (lab), scan);
	    }
	}
    }

  scan = emit_insn_after (gen_consttable_end (), scan);
  scan = emit_barrier_after (scan);
  pool_size = 0;
  pool_window_label = NULL_RTX;
  pool_window_last = 0;
}

/* Return nonzero if constant would be an ok source for a
   mov.w instead of a mov.l.  */

static int
hi_const (rtx src)
{
  return (GET_CODE (src) == CONST_INT
	  && INTVAL (src) >= -32768
	  && INTVAL (src) <= 32767);
}

#define MOVA_LABELREF(mova) XVECEXP (SET_SRC (PATTERN (mova)), 0, 0)

/* Nonzero if the insn is a move instruction which needs to be fixed.  */

/* ??? For a DImode/DFmode moves, we don't need to fix it if each half of the
   CONST_DOUBLE input value is CONST_OK_FOR_I08.  For a SFmode move, we don't
   need to fix it if the input value is CONST_OK_FOR_I08.  */

static int
broken_move (rtx insn)
{
  if (GET_CODE (insn) == INSN)
    {
      rtx pat = PATTERN (insn);
      if (GET_CODE (pat) == PARALLEL)
	pat = XVECEXP (pat, 0, 0);
      if (GET_CODE (pat) == SET
	  /* We can load any 8 bit value if we don't care what the high
	     order bits end up as.  */
	  && GET_MODE (SET_DEST (pat)) != QImode
	  && (CONSTANT_P (SET_SRC (pat))
	      /* Match mova_const.  */
	      || (GET_CODE (SET_SRC (pat)) == UNSPEC
		  && XINT (SET_SRC (pat), 1) == UNSPEC_MOVA
		  && GET_CODE (XVECEXP (SET_SRC (pat), 0, 0)) == CONST))
	  && ! (TARGET_SH2E
		&& GET_CODE (SET_SRC (pat)) == CONST_DOUBLE
		&& (fp_zero_operand (SET_SRC (pat))
		    || fp_one_operand (SET_SRC (pat)))
		/* ??? If this is a -m4 or -m4-single compilation, in general
		   we don't know the current setting of fpscr, so disable fldi.
		   There is an exception if this was a register-register move
		   before reload - and hence it was ascertained that we have
		   single precision setting - and in a post-reload optimization
		   we changed this to do a constant load.  In that case
		   we don't have an r0 clobber, hence we must use fldi.  */
		&& (! TARGET_SH4 || TARGET_FMOVD
		    || (GET_CODE (XEXP (XVECEXP (PATTERN (insn), 0, 2), 0))
			== SCRATCH))
		&& GET_CODE (SET_DEST (pat)) == REG
		&& FP_REGISTER_P (REGNO (SET_DEST (pat))))
	  && ! (TARGET_SH2A
		&& GET_MODE (SET_DEST (pat)) == SImode
		&& GET_CODE (SET_SRC (pat)) == CONST_INT
		&& CONST_OK_FOR_I20 (INTVAL (SET_SRC (pat))))
	  && (GET_CODE (SET_SRC (pat)) != CONST_INT
	      || ! CONST_OK_FOR_I08 (INTVAL (SET_SRC (pat)))))
	return 1;
    }

  return 0;
}

static int
mova_p (rtx insn)
{
  return (GET_CODE (insn) == INSN
	  && GET_CODE (PATTERN (insn)) == SET
	  && GET_CODE (SET_SRC (PATTERN (insn))) == UNSPEC
	  && XINT (SET_SRC (PATTERN (insn)), 1) == UNSPEC_MOVA
	  /* Don't match mova_const.  */
	  && GET_CODE (MOVA_LABELREF (insn)) == LABEL_REF);
}

/* Fix up a mova from a switch that went out of range.  */
static void
fixup_mova (rtx mova)
{
  PUT_MODE (XEXP (MOVA_LABELREF (mova), 0), QImode);
  if (! flag_pic)
    {
      SET_SRC (PATTERN (mova)) = MOVA_LABELREF (mova);
      INSN_CODE (mova) = -1;
    }
  else
    {
      rtx worker = mova;
      rtx lab = gen_label_rtx ();
      rtx wpat, wpat0, wpat1, wsrc, diff;

      do
	{
	  worker = NEXT_INSN (worker);
	  gcc_assert (worker
		      && GET_CODE (worker) != CODE_LABEL
		      && GET_CODE (worker) != JUMP_INSN);
	} while (GET_CODE (worker) == NOTE
		 || recog_memoized (worker) != CODE_FOR_casesi_worker_1);
      wpat = PATTERN (worker);
      wpat0 = XVECEXP (wpat, 0, 0);
      wpat1 = XVECEXP (wpat, 0, 1);
      wsrc = SET_SRC (wpat0);
      PATTERN (worker) = (gen_casesi_worker_2
			  (SET_DEST (wpat0), XVECEXP (wsrc, 0, 1),
			   XEXP (XVECEXP (wsrc, 0, 2), 0), lab,
			   XEXP (wpat1, 0)));
      INSN_CODE (worker) = -1;
      diff = gen_rtx_MINUS (Pmode, XVECEXP (SET_SRC (PATTERN (mova)), 0, 0),
			    gen_rtx_LABEL_REF (Pmode, lab));
      diff = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, diff), UNSPEC_PIC);
      SET_SRC (PATTERN (mova)) = gen_rtx_CONST (Pmode, diff);
      INSN_CODE (mova) = -1;
    }
}

/* NEW_MOVA is a mova we've just encountered while scanning forward.  Update
   *num_mova, and check if the new mova is not nested within the first one.
   return 0 if *first_mova was replaced, 1 if new_mova was replaced,
   2 if new_mova has been assigned to *first_mova, -1 otherwise..  */
static int
untangle_mova (int *num_mova, rtx *first_mova, rtx new_mova)
{
  int n_addr = 0; /* Initialization to shut up spurious warning.  */
  int f_target, n_target = 0; /* Likewise.  */

  if (optimize)
    {
      /* If NEW_MOVA has no address yet, it will be handled later.  */
      if (INSN_ADDRESSES_SIZE() <= (unsigned) INSN_UID (new_mova))
	return -1;

      n_addr = INSN_ADDRESSES (INSN_UID (new_mova));
      n_target = INSN_ADDRESSES (INSN_UID (XEXP (MOVA_LABELREF (new_mova), 0)));
      if (n_addr > n_target || n_addr + 1022 < n_target)
	{
	  /* Change the mova into a load.
	     broken_move will then return true for it.  */
	  fixup_mova (new_mova);
	  return 1;
	}
    }
  if (!(*num_mova)++)
    {
      *first_mova = new_mova;
      return 2;
    }
  if (!optimize
      || ((f_target
	   = INSN_ADDRESSES (INSN_UID (XEXP (MOVA_LABELREF (*first_mova), 0))))
	  >= n_target))
    return -1;

  (*num_mova)--;
  if (f_target - INSN_ADDRESSES (INSN_UID (*first_mova))
      > n_target - n_addr)
    {
      fixup_mova (*first_mova);
      return 0;
    }
  else
    {
      fixup_mova (new_mova);
      return 1;
    }
}

/* Find the last barrier from insn FROM which is close enough to hold the
   constant pool.  If we can't find one, then create one near the end of
   the range.  */

static rtx
find_barrier (int num_mova, rtx mova, rtx from)
{
  int count_si = 0;
  int count_hi = 0;
  int found_hi = 0;
  int found_si = 0;
  int found_di = 0;
  int hi_align = 2;
  int si_align = 2;
  int leading_mova = num_mova;
  rtx barrier_before_mova = 0, found_barrier = 0, good_barrier = 0;
  int si_limit;
  int hi_limit;
  rtx orig = from;

  /* For HImode: range is 510, add 4 because pc counts from address of
     second instruction after this one, subtract 2 for the jump instruction
     that we may need to emit before the table, subtract 2 for the instruction
     that fills the jump delay slot (in very rare cases, reorg will take an
     instruction from after the constant pool or will leave the delay slot
     empty).  This gives 510.
     For SImode: range is 1020, add 4 because pc counts from address of
     second instruction after this one, subtract 2 in case pc is 2 byte
     aligned, subtract 2 for the jump instruction that we may need to emit
     before the table, subtract 2 for the instruction that fills the jump
     delay slot.  This gives 1018.  */

  /* The branch will always be shortened now that the reference address for
     forward branches is the successor address, thus we need no longer make
     adjustments to the [sh]i_limit for -O0.  */

  si_limit = 1018;
  hi_limit = 510;

  while (from && count_si < si_limit && count_hi < hi_limit)
    {
      int inc = get_attr_length (from);
      int new_align = 1;

      /* If this is a label that existed at the time of the compute_alignments
	 call, determine the alignment.  N.B.  When find_barrier recurses for
	 an out-of-reach mova, we might see labels at the start of previously
	 inserted constant tables.  */
      if (GET_CODE (from) == CODE_LABEL
	  && CODE_LABEL_NUMBER (from) <= max_labelno_before_reorg)
	{
	  if (optimize)
	    new_align = 1 << label_to_alignment (from);
	  else if (GET_CODE (prev_nonnote_insn (from)) == BARRIER)
	    new_align = 1 << barrier_align (from);
	  else
	    new_align = 1;
	  inc = 0;
	}
      /* In case we are scanning a constant table because of recursion, check
	 for explicit alignments.  If the table is long, we might be forced
	 to emit the new table in front of it; the length of the alignment
	 might be the last straw.  */
      else if (GET_CODE (from) == INSN
	       && GET_CODE (PATTERN (from)) == UNSPEC_VOLATILE
	       && XINT (PATTERN (from), 1) == UNSPECV_ALIGN)
	new_align = INTVAL (XVECEXP (PATTERN (from), 0, 0));
      /* When we find the end of a constant table, paste the new constant
	 at the end.  That is better than putting it in front because
	 this way, we don't need extra alignment for adding a 4-byte-aligned
	 mov(a) label to a 2/4 or 8/4 byte aligned table.  */
      else if (GET_CODE (from) == INSN
	       && GET_CODE (PATTERN (from)) == UNSPEC_VOLATILE
	       && XINT (PATTERN (from), 1) == UNSPECV_CONST_END)
	return from;

      if (GET_CODE (from) == BARRIER)
	{

	  found_barrier = from;

	  /* If we are at the end of the function, or in front of an alignment
	     instruction, we need not insert an extra alignment.  We prefer
	     this kind of barrier.  */
	  if (barrier_align (from) > 2)
	    good_barrier = from;
	}

      if (broken_move (from))
	{
	  rtx pat, src, dst;
	  enum machine_mode mode;

	  pat = PATTERN (from);
	  if (GET_CODE (pat) == PARALLEL)
	    pat = XVECEXP (pat, 0, 0);
	  src = SET_SRC (pat);
	  dst = SET_DEST (pat);
	  mode = GET_MODE (dst);

	  /* We must explicitly check the mode, because sometimes the
	     front end will generate code to load unsigned constants into
	     HImode targets without properly sign extending them.  */
	  if (mode == HImode
	      || (mode == SImode && hi_const (src) && REGNO (dst) != FPUL_REG))
	    {
	      found_hi += 2;
	      /* We put the short constants before the long constants, so
		 we must count the length of short constants in the range
		 for the long constants.  */
	      /* ??? This isn't optimal, but is easy to do.  */
	      si_limit -= 2;
	    }
	  else
	    {
	      /* We dump DF/DI constants before SF/SI ones, because
		 the limit is the same, but the alignment requirements
		 are higher.  We may waste up to 4 additional bytes
		 for alignment, and the DF/DI constant may have
		 another SF/SI constant placed before it.  */
	      if (TARGET_SHCOMPACT
		  && ! found_di
		  && (mode == DFmode || mode == DImode))
		{
		  found_di = 1;
		  si_limit -= 8;
		}
	      while (si_align > 2 && found_si + si_align - 2 > count_si)
		si_align >>= 1;
	      if (found_si > count_si)
		count_si = found_si;
	      found_si += GET_MODE_SIZE (mode);
	      if (num_mova)
		si_limit -= GET_MODE_SIZE (mode);
	    }
	}

      if (mova_p (from))
	{
	  switch (untangle_mova (&num_mova, &mova, from))
	    {
	      case 0:	return find_barrier (0, 0, mova);
	      case 2:
		{
		  leading_mova = 0;
		  barrier_before_mova
		    = good_barrier ? good_barrier : found_barrier;
		}
	      default:	break;
	    }
	  if (found_si > count_si)
	    count_si = found_si;
	}
      else if (GET_CODE (from) == JUMP_INSN
	       && (GET_CODE (PATTERN (from)) == ADDR_VEC
		   || GET_CODE (PATTERN (from)) == ADDR_DIFF_VEC))
	{
	  if ((num_mova > 1 && GET_MODE (prev_nonnote_insn (from)) == VOIDmode)
	      || (num_mova
		  && (prev_nonnote_insn (from)
		      == XEXP (MOVA_LABELREF (mova), 0))))
	    num_mova--;
	  if (barrier_align (next_real_insn (from)) == align_jumps_log)
	    {
	      /* We have just passed the barrier in front of the
		 ADDR_DIFF_VEC, which is stored in found_barrier.  Since
		 the ADDR_DIFF_VEC is accessed as data, just like our pool
		 constants, this is a good opportunity to accommodate what
		 we have gathered so far.
		 If we waited any longer, we could end up at a barrier in
		 front of code, which gives worse cache usage for separated
		 instruction / data caches.  */
	      good_barrier = found_barrier;
	      break;
	    }
	  else
	    {
	      rtx body = PATTERN (from);
	      inc = XVECLEN (body, 1) * GET_MODE_SIZE (GET_MODE (body));
	    }
	}
      /* For the SH1, we generate alignments even after jumps-around-jumps.  */
      else if (GET_CODE (from) == JUMP_INSN
	       && ! TARGET_SH2
	       && ! TARGET_SMALLCODE)
	new_align = 4;

      if (found_si)
	{
	  count_si += inc;
	  if (new_align > si_align)
	    {
	      si_limit -= (count_si - 1) & (new_align - si_align);
	      si_align = new_align;
	    }
	  count_si = (count_si + new_align - 1) & -new_align;
	}
      if (found_hi)
	{
	  count_hi += inc;
	  if (new_align > hi_align)
	    {
	      hi_limit -= (count_hi - 1) & (new_align - hi_align);
	      hi_align = new_align;
	    }
	  count_hi = (count_hi + new_align - 1) & -new_align;
	}
      from = NEXT_INSN (from);
    }

  if (num_mova)
    {
      if (leading_mova)
	{
	  /* Try as we might, the leading mova is out of range.  Change
	     it into a load (which will become a pcload) and retry.  */
	  fixup_mova (mova);
	  return find_barrier (0, 0, mova);
	}
      else
	{
	  /* Insert the constant pool table before the mova instruction,
	     to prevent the mova label reference from going out of range.  */
	  from = mova;
	  good_barrier = found_barrier = barrier_before_mova;
	}
    }

  if (found_barrier)
    {
      if (good_barrier && next_real_insn (found_barrier))
	found_barrier = good_barrier;
    }
  else
    {
      /* We didn't find a barrier in time to dump our stuff,
	 so we'll make one.  */
      rtx label = gen_label_rtx ();

      /* If we exceeded the range, then we must back up over the last
	 instruction we looked at.  Otherwise, we just need to undo the
	 NEXT_INSN at the end of the loop.  */
      if (PREV_INSN (from) != orig
	  && (count_hi > hi_limit || count_si > si_limit))
	from = PREV_INSN (PREV_INSN (from));
      else
	from = PREV_INSN (from);

      /* Walk back to be just before any jump or label.
	 Putting it before a label reduces the number of times the branch
	 around the constant pool table will be hit.  Putting it before
	 a jump makes it more likely that the bra delay slot will be
	 filled.  */
      while (GET_CODE (from) == JUMP_INSN || GET_CODE (from) == NOTE
	     || GET_CODE (from) == CODE_LABEL)
	from = PREV_INSN (from);

      from = emit_jump_insn_after (gen_jump (label), from);
      JUMP_LABEL (from) = label;
      LABEL_NUSES (label) = 1;
      found_barrier = emit_barrier_after (from);
      emit_label_after (label, found_barrier);
    }

  return found_barrier;
}

/* If the instruction INSN is implemented by a special function, and we can
   positively find the register that is used to call the sfunc, and this
   register is not used anywhere else in this instruction - except as the
   destination of a set, return this register; else, return 0.  */
rtx
sfunc_uses_reg (rtx insn)
{
  int i;
  rtx pattern, part, reg_part, reg;

  if (GET_CODE (insn) != INSN)
    return 0;
  pattern = PATTERN (insn);
  if (GET_CODE (pattern) != PARALLEL || get_attr_type (insn) != TYPE_SFUNC)
    return 0;

  for (reg_part = 0, i = XVECLEN (pattern, 0) - 1; i >= 1; i--)
    {
      part = XVECEXP (pattern, 0, i);
      if (GET_CODE (part) == USE && GET_MODE (XEXP (part, 0)) == SImode)
	reg_part = part;
    }
  if (! reg_part)
    return 0;
  reg = XEXP (reg_part, 0);
  for (i = XVECLEN (pattern, 0) - 1; i >= 0; i--)
    {
      part = XVECEXP (pattern, 0, i);
      if (part == reg_part || GET_CODE (part) == CLOBBER)
	continue;
      if (reg_mentioned_p (reg, ((GET_CODE (part) == SET
				  && GET_CODE (SET_DEST (part)) == REG)
				 ? SET_SRC (part) : part)))
	return 0;
    }
  return reg;
}

/* See if the only way in which INSN uses REG is by calling it, or by
   setting it while calling it.  Set *SET to a SET rtx if the register
   is set by INSN.  */

static int
noncall_uses_reg (rtx reg, rtx insn, rtx *set)
{
  rtx pattern, reg2;

  *set = NULL_RTX;

  reg2 = sfunc_uses_reg (insn);
  if (reg2 && REGNO (reg2) == REGNO (reg))
    {
      pattern = single_set (insn);
      if (pattern
	  && GET_CODE (SET_DEST (pattern)) == REG
	  && REGNO (reg) == REGNO (SET_DEST (pattern)))
	*set = pattern;
      return 0;
    }
  if (GET_CODE (insn) != CALL_INSN)
    {
      /* We don't use rtx_equal_p because we don't care if the mode is
	 different.  */
      pattern = single_set (insn);
      if (pattern
	  && GET_CODE (SET_DEST (pattern)) == REG
	  && REGNO (reg) == REGNO (SET_DEST (pattern)))
	{
	  rtx par, part;
	  int i;

	  *set = pattern;
	  par = PATTERN (insn);
	  if (GET_CODE (par) == PARALLEL)
	    for (i = XVECLEN (par, 0) - 1; i >= 0; i--)
	      {
		part = XVECEXP (par, 0, i);
		if (GET_CODE (part) != SET && reg_mentioned_p (reg, part))
		  return 1;
	      }
	  return reg_mentioned_p (reg, SET_SRC (pattern));
	}

      return 1;
    }

  pattern = PATTERN (insn);

  if (GET_CODE (pattern) == PARALLEL)
    {
      int i;

      for (i = XVECLEN (pattern, 0) - 1; i >= 1; i--)
	if (reg_mentioned_p (reg, XVECEXP (pattern, 0, i)))
	  return 1;
      pattern = XVECEXP (pattern, 0, 0);
    }

  if (GET_CODE (pattern) == SET)
    {
      if (reg_mentioned_p (reg, SET_DEST (pattern)))
	{
	  /* We don't use rtx_equal_p, because we don't care if the
             mode is different.  */
	  if (GET_CODE (SET_DEST (pattern)) != REG
	      || REGNO (reg) != REGNO (SET_DEST (pattern)))
	    return 1;

	  *set = pattern;
	}

      pattern = SET_SRC (pattern);
    }

  if (GET_CODE (pattern) != CALL
      || GET_CODE (XEXP (pattern, 0)) != MEM
      || ! rtx_equal_p (reg, XEXP (XEXP (pattern, 0), 0)))
    return 1;

  return 0;
}

/* Given a X, a pattern of an insn or a part of it, return a mask of used
   general registers.  Bits 0..15 mean that the respective registers
   are used as inputs in the instruction.  Bits 16..31 mean that the
   registers 0..15, respectively, are used as outputs, or are clobbered.
   IS_DEST should be set to 16 if X is the destination of a SET, else to 0.  */
int
regs_used (rtx x, int is_dest)
{
  enum rtx_code code;
  const char *fmt;
  int i, used = 0;

  if (! x)
    return used;
  code = GET_CODE (x);
  switch (code)
    {
    case REG:
      if (REGNO (x) < 16)
	return (((1 << HARD_REGNO_NREGS (0, GET_MODE (x))) - 1)
		<< (REGNO (x) + is_dest));
      return 0;
    case SUBREG:
      {
	rtx y = SUBREG_REG (x);

	if (GET_CODE (y) != REG)
	  break;
	if (REGNO (y) < 16)
	  return (((1 << HARD_REGNO_NREGS (0, GET_MODE (x))) - 1)
		  << (REGNO (y) +
		      subreg_regno_offset (REGNO (y),
					   GET_MODE (y),
					   SUBREG_BYTE (x),
					   GET_MODE (x)) + is_dest));
	return 0;
      }
    case SET:
      return regs_used (SET_SRC (x), 0) | regs_used (SET_DEST (x), 16);
    case RETURN:
      /* If there was a return value, it must have been indicated with USE.  */
      return 0x00ffff00;
    case CLOBBER:
      is_dest = 1;
      break;
    case MEM:
      is_dest = 0;
      break;
    case CALL:
      used |= 0x00ff00f0;
      break;
    default:
      break;
    }

  fmt = GET_RTX_FORMAT (code);

  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    {
      if (fmt[i] == 'E')
	{
	  register int j;
	  for (j = XVECLEN (x, i) - 1; j >= 0; j--)
	    used |= regs_used (XVECEXP (x, i, j), is_dest);
	}
      else if (fmt[i] == 'e')
	used |= regs_used (XEXP (x, i), is_dest);
    }
  return used;
}

/* Create an instruction that prevents redirection of a conditional branch
   to the destination of the JUMP with address ADDR.
   If the branch needs to be implemented as an indirect jump, try to find
   a scratch register for it.
   If NEED_BLOCK is 0, don't do anything unless we need a scratch register.
   If any preceding insn that doesn't fit into a delay slot is good enough,
   pass 1.  Pass 2 if a definite blocking insn is needed.
   -1 is used internally to avoid deep recursion.
   If a blocking instruction is made or recognized, return it.  */

static rtx
gen_block_redirect (rtx jump, int addr, int need_block)
{
  int dead = 0;
  rtx prev = prev_nonnote_insn (jump);
  rtx dest;

  /* First, check if we already have an instruction that satisfies our need.  */
  if (prev && GET_CODE (prev) == INSN && ! INSN_DELETED_P (prev))
    {
      if (INSN_CODE (prev) == CODE_FOR_indirect_jump_scratch)
	return prev;
      if (GET_CODE (PATTERN (prev)) == USE
	  || GET_CODE (PATTERN (prev)) == CLOBBER
	  || get_attr_in_delay_slot (prev) == IN_DELAY_SLOT_YES)
	prev = jump;
      else if ((need_block &= ~1) < 0)
	return prev;
      else if (recog_memoized (prev) == CODE_FOR_block_branch_redirect)
	need_block = 0;
    }
  if (GET_CODE (PATTERN (jump)) == RETURN)
    {
      if (! need_block)
	return prev;
      /* Reorg even does nasty things with return insns that cause branches
	 to go out of range - see find_end_label and callers.  */
      return emit_insn_before (gen_block_branch_redirect (const0_rtx) , jump);
    }
  /* We can't use JUMP_LABEL here because it might be undefined
     when not optimizing.  */
  dest = XEXP (SET_SRC (PATTERN (jump)), 0);
  /* If the branch is out of range, try to find a scratch register for it.  */
  if (optimize
      && (INSN_ADDRESSES (INSN_UID (dest)) - addr + (unsigned) 4092
	  > 4092 + 4098))
    {
      rtx scan;
      /* Don't look for the stack pointer as a scratch register,
	 it would cause trouble if an interrupt occurred.  */
      unsigned try = 0x7fff, used;
      int jump_left = flag_expensive_optimizations + 1;

      /* It is likely that the most recent eligible instruction is wanted for
	 the delay slot.  Therefore, find out which registers it uses, and
	 try to avoid using them.  */

      for (scan = jump; (scan = PREV_INSN (scan)); )
	{
	  enum rtx_code code;

	  if (INSN_DELETED_P (scan))
	    continue;
	  code = GET_CODE (scan);
	  if (code == CODE_LABEL || code == JUMP_INSN)
	    break;
	  if (code == INSN
	      && GET_CODE (PATTERN (scan)) != USE
	      && GET_CODE (PATTERN (scan)) != CLOBBER
	      && get_attr_in_delay_slot (scan) == IN_DELAY_SLOT_YES)
	    {
	      try &= ~regs_used (PATTERN (scan), 0);
	      break;
	    }
	}
      for (used = dead = 0, scan = JUMP_LABEL (jump);
	   (scan = NEXT_INSN (scan)); )
	{
	  enum rtx_code code;

	  if (INSN_DELETED_P (scan))
	    continue;
	  code = GET_CODE (scan);
	  if (INSN_P (scan))
	    {
	      used |= regs_used (PATTERN (scan), 0);
	      if (code == CALL_INSN)
		used |= regs_used (CALL_INSN_FUNCTION_USAGE (scan), 0);
	      dead |= (used >> 16) & ~used;
	      if (dead & try)
		{
		  dead &= try;
		  break;
		}
	      if (code == JUMP_INSN)
		{
		  if (jump_left-- && simplejump_p (scan))
		    scan = JUMP_LABEL (scan);
		  else
		    break;
		}
	    }
	}
      /* Mask out the stack pointer again, in case it was
	 the only 'free' register we have found.  */
      dead &= 0x7fff;
    }
  /* If the immediate destination is still in range, check for possible
     threading with a jump beyond the delay slot insn.
     Don't check if we are called recursively; the jump has been or will be
     checked in a different invocation then.  */

  else if (optimize && need_block >= 0)
    {
      rtx next = next_active_insn (next_active_insn (dest));
      if (next && GET_CODE (next) == JUMP_INSN
	  && GET_CODE (PATTERN (next)) == SET
	  && recog_memoized (next) == CODE_FOR_jump_compact)
	{
	  dest = JUMP_LABEL (next);
	  if (dest
	      && (INSN_ADDRESSES (INSN_UID (dest)) - addr + (unsigned) 4092
		  > 4092 + 4098))
	    gen_block_redirect (next, INSN_ADDRESSES (INSN_UID (next)), -1);
	}
    }

  if (dead)
    {
      rtx reg = gen_rtx_REG (SImode, exact_log2 (dead & -dead));

      /* It would be nice if we could convert the jump into an indirect
	 jump / far branch right now, and thus exposing all constituent
	 instructions to further optimization.  However, reorg uses
	 simplejump_p to determine if there is an unconditional jump where
	 it should try to schedule instructions from the target of the
	 branch; simplejump_p fails for indirect jumps even if they have
	 a JUMP_LABEL.  */
      rtx insn = emit_insn_before (gen_indirect_jump_scratch
				   (reg, GEN_INT (INSN_UID (JUMP_LABEL (jump))))
				   , jump);
      /* ??? We would like this to have the scope of the jump, but that
	 scope will change when a delay slot insn of an inner scope is added.
	 Hence, after delay slot scheduling, we'll have to expect
	 NOTE_INSN_BLOCK_END notes between the indirect_jump_scratch and
	 the jump.  */

      INSN_LOCATOR (insn) = INSN_LOCATOR (jump);
      INSN_CODE (insn) = CODE_FOR_indirect_jump_scratch;
      return insn;
    }
  else if (need_block)
    /* We can't use JUMP_LABEL here because it might be undefined
       when not optimizing.  */
    return emit_insn_before (gen_block_branch_redirect
		      (GEN_INT (INSN_UID (XEXP (SET_SRC (PATTERN (jump)), 0))))
		      , jump);
  return prev;
}

#define CONDJUMP_MIN -252
#define CONDJUMP_MAX 262
struct far_branch
{
  /* A label (to be placed) in front of the jump
     that jumps to our ultimate destination.  */
  rtx near_label;
  /* Where we are going to insert it if we cannot move the jump any farther,
     or the jump itself if we have picked up an existing jump.  */
  rtx insert_place;
  /* The ultimate destination.  */
  rtx far_label;
  struct far_branch *prev;
  /* If the branch has already been created, its address;
     else the address of its first prospective user.  */
  int address;
};

static void gen_far_branch (struct far_branch *);
enum mdep_reorg_phase_e mdep_reorg_phase;
static void
gen_far_branch (struct far_branch *bp)
{
  rtx insn = bp->insert_place;
  rtx jump;
  rtx label = gen_label_rtx ();
  int ok;

  emit_label_after (label, insn);
  if (bp->far_label)
    {
      jump = emit_jump_insn_after (gen_jump (bp->far_label), insn);
      LABEL_NUSES (bp->far_label)++;
    }
  else
    jump = emit_jump_insn_after (gen_return (), insn);
  /* Emit a barrier so that reorg knows that any following instructions
     are not reachable via a fall-through path.
     But don't do this when not optimizing, since we wouldn't suppress the
     alignment for the barrier then, and could end up with out-of-range
     pc-relative loads.  */
  if (optimize)
    emit_barrier_after (jump);
  emit_label_after (bp->near_label, insn);
  JUMP_LABEL (jump) = bp->far_label;
  ok = invert_jump (insn, label, 1);
  gcc_assert (ok);
  
  /* If we are branching around a jump (rather than a return), prevent
     reorg from using an insn from the jump target as the delay slot insn -
     when reorg did this, it pessimized code (we rather hide the delay slot)
     and it could cause branches to go out of range.  */
  if (bp->far_label)
    (emit_insn_after
     (gen_stuff_delay_slot
      (GEN_INT (INSN_UID (XEXP (SET_SRC (PATTERN (jump)), 0))),
       GEN_INT (recog_memoized (insn) == CODE_FOR_branch_false)),
      insn));
  /* Prevent reorg from undoing our splits.  */
  gen_block_redirect (jump, bp->address += 2, 2);
}

/* Fix up ADDR_DIFF_VECs.  */
void
fixup_addr_diff_vecs (rtx first)
{
  rtx insn;

  for (insn = first; insn; insn = NEXT_INSN (insn))
    {
      rtx vec_lab, pat, prev, prevpat, x, braf_label;

      if (GET_CODE (insn) != JUMP_INSN
	  || GET_CODE (PATTERN (insn)) != ADDR_DIFF_VEC)
	continue;
      pat = PATTERN (insn);
      vec_lab = XEXP (XEXP (pat, 0), 0);

      /* Search the matching casesi_jump_2.  */
      for (prev = vec_lab; ; prev = PREV_INSN (prev))
	{
	  if (GET_CODE (prev) != JUMP_INSN)
	    continue;
	  prevpat = PATTERN (prev);
	  if (GET_CODE (prevpat) != PARALLEL || XVECLEN (prevpat, 0) != 2)
	    continue;
	  x = XVECEXP (prevpat, 0, 1);
	  if (GET_CODE (x) != USE)
	    continue;
	  x = XEXP (x, 0);
	  if (GET_CODE (x) == LABEL_REF && XEXP (x, 0) == vec_lab)
	    break;
	}
      /* FIXME: This is a bug in the optimizer, but it seems harmless
	 to just avoid panicing.  */
      if (!prev)
	continue;

      /* Emit the reference label of the braf where it belongs, right after
	 the casesi_jump_2 (i.e. braf).  */
      braf_label = XEXP (XEXP (SET_SRC (XVECEXP (prevpat, 0, 0)), 1), 0);
      emit_label_after (braf_label, prev);

      /* Fix up the ADDR_DIF_VEC to be relative
	 to the reference address of the braf.  */
      XEXP (XEXP (pat, 0), 0) = braf_label;
    }
}

/* BARRIER_OR_LABEL is either a BARRIER or a CODE_LABEL immediately following
   a barrier.  Return the base 2 logarithm of the desired alignment.  */
int
barrier_align (rtx barrier_or_label)
{
  rtx next = next_real_insn (barrier_or_label), pat, prev;
  int slot, credit, jump_to_next = 0;

  if (! next)
    return 0;

  pat = PATTERN (next);

  if (GET_CODE (pat) == ADDR_DIFF_VEC)
    return 2;

  if (GET_CODE (pat) == UNSPEC_VOLATILE && XINT (pat, 1) == UNSPECV_ALIGN)
    /* This is a barrier in front of a constant table.  */
    return 0;

  prev = prev_real_insn (barrier_or_label);
  if (GET_CODE (PATTERN (prev)) == ADDR_DIFF_VEC)
    {
      pat = PATTERN (prev);
      /* If this is a very small table, we want to keep the alignment after
	 the table to the minimum for proper code alignment.  */
      return ((TARGET_SMALLCODE
	       || ((unsigned) XVECLEN (pat, 1) * GET_MODE_SIZE (GET_MODE (pat))
		   <= (unsigned) 1 << (CACHE_LOG - 2)))
	      ? 1 << TARGET_SHMEDIA : align_jumps_log);
    }

  if (TARGET_SMALLCODE)
    return 0;

  if (! TARGET_SH2 || ! optimize)
    return align_jumps_log;

  /* When fixing up pcloads, a constant table might be inserted just before
     the basic block that ends with the barrier.  Thus, we can't trust the
     instruction lengths before that.  */
  if (mdep_reorg_phase > SH_FIXUP_PCLOAD)
    {
      /* Check if there is an immediately preceding branch to the insn beyond
	 the barrier.  We must weight the cost of discarding useful information
	 from the current cache line when executing this branch and there is
	 an alignment, against that of fetching unneeded insn in front of the
	 branch target when there is no alignment.  */

      /* There are two delay_slot cases to consider.  One is the simple case
	 where the preceding branch is to the insn beyond the barrier (simple
	 delay slot filling), and the other is where the preceding branch has
	 a delay slot that is a duplicate of the insn after the barrier
	 (fill_eager_delay_slots) and the branch is to the insn after the insn
	 after the barrier.  */

      /* PREV is presumed to be the JUMP_INSN for the barrier under
	 investigation.  Skip to the insn before it.  */
      prev = prev_real_insn (prev);

      for (slot = 2, credit = (1 << (CACHE_LOG - 2)) + 2;
	   credit >= 0 && prev && GET_CODE (prev) == INSN;
	   prev = prev_real_insn (prev))
	{
	  jump_to_next = 0;
	  if (GET_CODE (PATTERN (prev)) == USE
	      || GET_CODE (PATTERN (prev)) == CLOBBER)
	    continue;
	  if (GET_CODE (PATTERN (prev)) == SEQUENCE)
	    {
	      prev = XVECEXP (PATTERN (prev), 0, 1);
	      if (INSN_UID (prev) == INSN_UID (next))
		{
	  	  /* Delay slot was filled with insn at jump target.  */
		  jump_to_next = 1;
		  continue;
  		}
	    }

	  if (slot &&
	      get_attr_in_delay_slot (prev) == IN_DELAY_SLOT_YES)
	    slot = 0;
	  credit -= get_attr_length (prev);
	}
      if (prev
	  && GET_CODE (prev) == JUMP_INSN
	  && JUMP_LABEL (prev))
	{
	  rtx x;
	  if (jump_to_next
	      || next_real_insn (JUMP_LABEL (prev)) == next
	      /* If relax_delay_slots() decides NEXT was redundant
		 with some previous instruction, it will have
		 redirected PREV's jump to the following insn.  */
	      || JUMP_LABEL (prev) == next_nonnote_insn (next)
	      /* There is no upper bound on redundant instructions
		 that might have been skipped, but we must not put an
		 alignment where none had been before.  */
	      || (x = (NEXT_INSN (NEXT_INSN (PREV_INSN (prev)))),
		  (INSN_P (x)
		   && (INSN_CODE (x) == CODE_FOR_block_branch_redirect
		       || INSN_CODE (x) == CODE_FOR_indirect_jump_scratch
		       || INSN_CODE (x) == CODE_FOR_stuff_delay_slot))))
	    {
	      rtx pat = PATTERN (prev);
	      if (GET_CODE (pat) == PARALLEL)
		pat = XVECEXP (pat, 0, 0);
	      if (credit - slot >= (GET_CODE (SET_SRC (pat)) == PC ? 2 : 0))
		return 0;
	    }
	}
    }

  return align_jumps_log;
}

/* If we are inside a phony loop, almost any kind of label can turn up as the
   first one in the loop.  Aligning a braf label causes incorrect switch
   destination addresses; we can detect braf labels because they are
   followed by a BARRIER.
   Applying loop alignment to small constant or switch tables is a waste
   of space, so we suppress this too.  */
int
sh_loop_align (rtx label)
{
  rtx next = label;

  do
    next = next_nonnote_insn (next);
  while (next && GET_CODE (next) == CODE_LABEL);

  if (! next
      || ! INSN_P (next)
      || GET_CODE (PATTERN (next)) == ADDR_DIFF_VEC
      || recog_memoized (next) == CODE_FOR_consttable_2)
    return 0;

  return align_loops_log;
}

/* Do a final pass over the function, just before delayed branch
   scheduling.  */

static void
sh_reorg (void)
{
  rtx first, insn, mova = NULL_RTX;
  int num_mova;
  rtx r0_rtx = gen_rtx_REG (Pmode, 0);
  rtx r0_inc_rtx = gen_rtx_POST_INC (Pmode, r0_rtx);

  first = get_insns ();
  max_labelno_before_reorg = max_label_num ();

  /* We must split call insns before introducing `mova's.  If we're
     optimizing, they'll have already been split.  Otherwise, make
     sure we don't split them too late.  */
  if (! optimize)
    split_all_insns_noflow ();

  if (TARGET_SHMEDIA)
    return;

  /* If relaxing, generate pseudo-ops to associate function calls with
     the symbols they call.  It does no harm to not generate these
     pseudo-ops.  However, when we can generate them, it enables to
     linker to potentially relax the jsr to a bsr, and eliminate the
     register load and, possibly, the constant pool entry.  */

  mdep_reorg_phase = SH_INSERT_USES_LABELS;
  if (TARGET_RELAX)
    {
      /* Remove all REG_LABEL notes.  We want to use them for our own
	 purposes.  This works because none of the remaining passes
	 need to look at them.

	 ??? But it may break in the future.  We should use a machine
	 dependent REG_NOTE, or some other approach entirely.  */
      for (insn = first; insn; insn = NEXT_INSN (insn))
	{
	  if (INSN_P (insn))
	    {
	      rtx note;

	      while ((note = find_reg_note (insn, REG_LABEL, NULL_RTX)) != 0)
		remove_note (insn, note);
	    }
	}

      for (insn = first; insn; insn = NEXT_INSN (insn))
	{
	  rtx pattern, reg, link, set, scan, dies, label;
	  int rescan = 0, foundinsn = 0;

	  if (GET_CODE (insn) == CALL_INSN)
	    {
	      pattern = PATTERN (insn);

	      if (GET_CODE (pattern) == PARALLEL)
		pattern = XVECEXP (pattern, 0, 0);
	      if (GET_CODE (pattern) == SET)
		pattern = SET_SRC (pattern);

	      if (GET_CODE (pattern) != CALL
		  || GET_CODE (XEXP (pattern, 0)) != MEM)
		continue;

	      reg = XEXP (XEXP (pattern, 0), 0);
	    }
	  else
	    {
	      reg = sfunc_uses_reg (insn);
	      if (! reg)
		continue;
	    }

	  if (GET_CODE (reg) != REG)
	    continue;

	  /* This is a function call via REG.  If the only uses of REG
	     between the time that it is set and the time that it dies
	     are in function calls, then we can associate all the
	     function calls with the setting of REG.  */

	  for (link = LOG_LINKS (insn); link; link = XEXP (link, 1))
	    {
	      rtx linked_insn;

	      if (REG_NOTE_KIND (link) != 0)
		continue;
	      linked_insn = XEXP (link, 0);
	      set = single_set (linked_insn);
	      if (set
		  && rtx_equal_p (reg, SET_DEST (set))
		  && ! INSN_DELETED_P (linked_insn))
		{
		  link = linked_insn;
		  break;
		}
	    }

	  if (! link)
	    {
	      /* ??? Sometimes global register allocation will have
                 deleted the insn pointed to by LOG_LINKS.  Try
                 scanning backward to find where the register is set.  */
	      for (scan = PREV_INSN (insn);
		   scan && GET_CODE (scan) != CODE_LABEL;
		   scan = PREV_INSN (scan))
		{
		  if (! INSN_P (scan))
		    continue;

		  if (! reg_mentioned_p (reg, scan))
		    continue;

		  if (noncall_uses_reg (reg, scan, &set))
		    break;

		  if (set)
		    {
		      link = scan;
		      break;
		    }
		}
	    }

	  if (! link)
	    continue;

	  /* The register is set at LINK.  */

	  /* We can only optimize the function call if the register is
             being set to a symbol.  In theory, we could sometimes
             optimize calls to a constant location, but the assembler
             and linker do not support that at present.  */
	  if (GET_CODE (SET_SRC (set)) != SYMBOL_REF
	      && GET_CODE (SET_SRC (set)) != LABEL_REF)
	    continue;

	  /* Scan forward from LINK to the place where REG dies, and
             make sure that the only insns which use REG are
             themselves function calls.  */

	  /* ??? This doesn't work for call targets that were allocated
	     by reload, since there may not be a REG_DEAD note for the
	     register.  */

	  dies = NULL_RTX;
	  for (scan = NEXT_INSN (link); scan; scan = NEXT_INSN (scan))
	    {
	      rtx scanset;

	      /* Don't try to trace forward past a CODE_LABEL if we haven't
		 seen INSN yet.  Ordinarily, we will only find the setting insn
		 in LOG_LINKS if it is in the same basic block.  However,
		 cross-jumping can insert code labels in between the load and
		 the call, and can result in situations where a single call
		 insn may have two targets depending on where we came from.  */

	      if (GET_CODE (scan) == CODE_LABEL && ! foundinsn)
		break;

	      if (! INSN_P (scan))
		continue;

	      /* Don't try to trace forward past a JUMP.  To optimize
                 safely, we would have to check that all the
                 instructions at the jump destination did not use REG.  */

	      if (GET_CODE (scan) == JUMP_INSN)
		break;

	      if (! reg_mentioned_p (reg, scan))
		continue;

	      if (noncall_uses_reg (reg, scan, &scanset))
		break;

	      if (scan == insn)
		foundinsn = 1;

	      if (scan != insn
		  && (GET_CODE (scan) == CALL_INSN || sfunc_uses_reg (scan)))
		{
		  /* There is a function call to this register other
                     than the one we are checking.  If we optimize
                     this call, we need to rescan again below.  */
		  rescan = 1;
		}

	      /* ??? We shouldn't have to worry about SCANSET here.
		 We should just be able to check for a REG_DEAD note
		 on a function call.  However, the REG_DEAD notes are
		 apparently not dependable around libcalls; c-torture
		 execute/920501-2 is a test case.  If SCANSET is set,
		 then this insn sets the register, so it must have
		 died earlier.  Unfortunately, this will only handle
		 the cases in which the register is, in fact, set in a
		 later insn.  */

	      /* ??? We shouldn't have to use FOUNDINSN here.
		 However, the LOG_LINKS fields are apparently not
		 entirely reliable around libcalls;
		 newlib/libm/math/e_pow.c is a test case.  Sometimes
		 an insn will appear in LOG_LINKS even though it is
		 not the most recent insn which sets the register.  */

	      if (foundinsn
		  && (scanset
		      || find_reg_note (scan, REG_DEAD, reg)))
		{
		  dies = scan;
		  break;
		}
	    }

	  if (! dies)
	    {
	      /* Either there was a branch, or some insn used REG
                 other than as a function call address.  */
	      continue;
	    }

	  /* Create a code label, and put it in a REG_LABEL note on
             the insn which sets the register, and on each call insn
             which uses the register.  In final_prescan_insn we look
             for the REG_LABEL notes, and output the appropriate label
             or pseudo-op.  */

	  label = gen_label_rtx ();
	  REG_NOTES (link) = gen_rtx_INSN_LIST (REG_LABEL, label,
						REG_NOTES (link));
	  REG_NOTES (insn) = gen_rtx_INSN_LIST (REG_LABEL, label,
						REG_NOTES (insn));
	  if (rescan)
	    {
	      scan = link;
	      do
		{
		  rtx reg2;

		  scan = NEXT_INSN (scan);
		  if (scan != insn
		      && ((GET_CODE (scan) == CALL_INSN
			   && reg_mentioned_p (reg, scan))
			  || ((reg2 = sfunc_uses_reg (scan))
			      && REGNO (reg2) == REGNO (reg))))
		    REG_NOTES (scan)
		      = gen_rtx_INSN_LIST (REG_LABEL, label, REG_NOTES (scan));
		}
	      while (scan != dies);
	    }
	}
    }

  if (TARGET_SH2)
    fixup_addr_diff_vecs (first);

  if (optimize)
    {
      mdep_reorg_phase = SH_SHORTEN_BRANCHES0;
      shorten_branches (first);
    }

  /* Scan the function looking for move instructions which have to be
     changed to pc-relative loads and insert the literal tables.  */
  label_ref_list_pool = create_alloc_pool ("label references list",
					   sizeof (struct label_ref_list_d),
					   30);
  mdep_reorg_phase = SH_FIXUP_PCLOAD;
  for (insn = first, num_mova = 0; insn; insn = NEXT_INSN (insn))
    {
      if (mova_p (insn))
	{
	  /* ??? basic block reordering can move a switch table dispatch
	     below the switch table.  Check if that has happened.
	     We only have the addresses available when optimizing; but then,
	     this check shouldn't be needed when not optimizing.  */
	  if (!untangle_mova (&num_mova, &mova, insn))
	    {
	      insn = mova;
	      num_mova = 0;
	    }
	}
      else if (GET_CODE (insn) == JUMP_INSN
	       && GET_CODE (PATTERN (insn)) == ADDR_DIFF_VEC
	       && num_mova
	       /* ??? loop invariant motion can also move a mova out of a
		  loop.  Since loop does this code motion anyway, maybe we
		  should wrap UNSPEC_MOVA into a CONST, so that reload can
		  move it back.  */
	       && ((num_mova > 1
		    && GET_MODE (prev_nonnote_insn (insn)) == VOIDmode)
		   || (prev_nonnote_insn (insn)
		       == XEXP (MOVA_LABELREF (mova), 0))))
	{
	  rtx scan;
	  int total;

	  num_mova--;

	  /* Some code might have been inserted between the mova and
	     its ADDR_DIFF_VEC.  Check if the mova is still in range.  */
	  for (scan = mova, total = 0; scan != insn; scan = NEXT_INSN (scan))
	    total += get_attr_length (scan);

	  /* range of mova is 1020, add 4 because pc counts from address of
	     second instruction after this one, subtract 2 in case pc is 2
	     byte aligned.  Possible alignment needed for the ADDR_DIFF_VEC
	     cancels out with alignment effects of the mova itself.  */
	  if (total > 1022)
	    {
	      /* Change the mova into a load, and restart scanning
		 there.  broken_move will then return true for mova.  */
	      fixup_mova (mova);
	      insn = mova;
	    }
	}
      if (broken_move (insn)
	  || (GET_CODE (insn) == INSN
	      && recog_memoized (insn) == CODE_FOR_casesi_worker_2))
	{
	  rtx scan;
	  /* Scan ahead looking for a barrier to stick the constant table
	     behind.  */
	  rtx barrier = find_barrier (num_mova, mova, insn);
	  rtx last_float_move = NULL_RTX, last_float = 0, *last_float_addr = NULL;
	  int need_aligned_label = 0;

	  if (num_mova && ! mova_p (mova))
	    {
	      /* find_barrier had to change the first mova into a
		 pcload; thus, we have to start with this new pcload.  */
	      insn = mova;
	      num_mova = 0;
	    }
	  /* Now find all the moves between the points and modify them.  */
	  for (scan = insn; scan != barrier; scan = NEXT_INSN (scan))
	    {
	      if (GET_CODE (scan) == CODE_LABEL)
		last_float = 0;
	      if (GET_CODE (scan) == INSN
		  && recog_memoized (scan) == CODE_FOR_casesi_worker_2)
		need_aligned_label = 1;
	      if (broken_move (scan))
		{
		  rtx *patp = &PATTERN (scan), pat = *patp;
		  rtx src, dst;
		  rtx lab;
		  rtx newsrc;
		  enum machine_mode mode;

		  if (GET_CODE (pat) == PARALLEL)
		    patp = &XVECEXP (pat, 0, 0), pat = *patp;
		  src = SET_SRC (pat);
		  dst = SET_DEST (pat);
		  mode = GET_MODE (dst);

		  if (mode == SImode && hi_const (src)
		      && REGNO (dst) != FPUL_REG)
		    {
		      int offset = 0;

		      mode = HImode;
		      while (GET_CODE (dst) == SUBREG)
			{
			  offset += subreg_regno_offset (REGNO (SUBREG_REG (dst)),
							 GET_MODE (SUBREG_REG (dst)),
							 SUBREG_BYTE (dst),
							 GET_MODE (dst));
			  dst = SUBREG_REG (dst);
			}
		      dst = gen_rtx_REG (HImode, REGNO (dst) + offset);
		    }
		  if (GET_CODE (dst) == REG && FP_ANY_REGISTER_P (REGNO (dst)))
		    {
		      /* This must be an insn that clobbers r0.  */
		      rtx *clobberp = &XVECEXP (PATTERN (scan), 0,
						XVECLEN (PATTERN (scan), 0)
						- 1);
		      rtx clobber = *clobberp;

		      gcc_assert (GET_CODE (clobber) == CLOBBER
				  && rtx_equal_p (XEXP (clobber, 0), r0_rtx));

		      if (last_float
			  && reg_set_between_p (r0_rtx, last_float_move, scan))
			last_float = 0;
		      if (last_float
			  && TARGET_SHCOMPACT
			  && GET_MODE_SIZE (mode) != 4
			  && GET_MODE_SIZE (GET_MODE (last_float)) == 4)
			last_float = 0;
		      lab = add_constant (src, mode, last_float);
		      if (lab)
			emit_insn_before (gen_mova (lab), scan);
		      else
			{
			  /* There will be a REG_UNUSED note for r0 on
			     LAST_FLOAT_MOVE; we have to change it to REG_INC,
			     lest reorg:mark_target_live_regs will not
			     consider r0 to be used, and we end up with delay
			     slot insn in front of SCAN that clobbers r0.  */
			  rtx note
			    = find_regno_note (last_float_move, REG_UNUSED, 0);

			  /* If we are not optimizing, then there may not be
			     a note.  */
			  if (note)
			    PUT_MODE (note, REG_INC);

			  *last_float_addr = r0_inc_rtx;
			}
		      last_float_move = scan;
		      last_float = src;
		      newsrc = gen_const_mem (mode,
					(((TARGET_SH4 && ! TARGET_FMOVD)
					  || REGNO (dst) == FPUL_REG)
					 ? r0_inc_rtx
					 : r0_rtx));
		      last_float_addr = &XEXP (newsrc, 0);

		      /* Remove the clobber of r0.  */
		      *clobberp = gen_rtx_CLOBBER (GET_MODE (clobber),
						   gen_rtx_SCRATCH (Pmode));
		    }
		  /* This is a mova needing a label.  Create it.  */
		  else if (GET_CODE (src) == UNSPEC
			   && XINT (src, 1) == UNSPEC_MOVA
			   && GET_CODE (XVECEXP (src, 0, 0)) == CONST)
		    {
		      lab = add_constant (XVECEXP (src, 0, 0), mode, 0);
		      newsrc = gen_rtx_LABEL_REF (VOIDmode, lab);
		      newsrc = gen_rtx_UNSPEC (SImode,
					       gen_rtvec (1, newsrc),
					       UNSPEC_MOVA);
		    }
		  else
		    {
		      lab = add_constant (src, mode, 0);
		      newsrc = gen_rtx_LABEL_REF (VOIDmode, lab);
		      newsrc = gen_const_mem (mode, newsrc);
		    }
		  *patp = gen_rtx_SET (VOIDmode, dst, newsrc);
		  INSN_CODE (scan) = -1;
		}
	    }
	  dump_table (need_aligned_label ? insn : 0, barrier);
	  insn = barrier;
	}
    }
  free_alloc_pool (label_ref_list_pool);
  for (insn = first; insn; insn = NEXT_INSN (insn))
    PUT_MODE (insn, VOIDmode);

  mdep_reorg_phase = SH_SHORTEN_BRANCHES1;
  INSN_ADDRESSES_FREE ();
  split_branches (first);

  /* The INSN_REFERENCES_ARE_DELAYED in sh.h is problematic because it
     also has an effect on the register that holds the address of the sfunc.
     Insert an extra dummy insn in front of each sfunc that pretends to
     use this register.  */
  if (flag_delayed_branch)
    {
      for (insn = first; insn; insn = NEXT_INSN (insn))
	{
	  rtx reg = sfunc_uses_reg (insn);

	  if (! reg)
	    continue;
	  emit_insn_before (gen_use_sfunc_addr (reg), insn);
	}
    }
#if 0
  /* fpscr is not actually a user variable, but we pretend it is for the
     sake of the previous optimization passes, since we want it handled like
     one.  However, we don't have any debugging information for it, so turn
     it into a non-user variable now.  */
  if (TARGET_SH4)
    REG_USERVAR_P (get_fpscr_rtx ()) = 0;
#endif
  mdep_reorg_phase = SH_AFTER_MDEP_REORG;
}

int
get_dest_uid (rtx label, int max_uid)
{
  rtx dest = next_real_insn (label);
  int dest_uid;
  if (! dest)
    /* This can happen for an undefined label.  */
    return 0;
  dest_uid = INSN_UID (dest);
  /* If this is a newly created branch redirection blocking instruction,
     we cannot index the branch_uid or insn_addresses arrays with its
     uid.  But then, we won't need to, because the actual destination is
     the following branch.  */
  while (dest_uid >= max_uid)
    {
      dest = NEXT_INSN (dest);
      dest_uid = INSN_UID (dest);
    }
  if (GET_CODE (dest) == JUMP_INSN && GET_CODE (PATTERN (dest)) == RETURN)
    return 0;
  return dest_uid;
}

/* Split condbranches that are out of range.  Also add clobbers for
   scratch registers that are needed in far jumps.
   We do this before delay slot scheduling, so that it can take our
   newly created instructions into account.  It also allows us to
   find branches with common targets more easily.  */

static void
split_branches (rtx first)
{
  rtx insn;
  struct far_branch **uid_branch, *far_branch_list = 0;
  int max_uid = get_max_uid ();
  int ok;

  /* Find out which branches are out of range.  */
  shorten_branches (first);

  uid_branch = (struct far_branch **) alloca (max_uid * sizeof *uid_branch);
  memset ((char *) uid_branch, 0, max_uid * sizeof *uid_branch);

  for (insn = first; insn; insn = NEXT_INSN (insn))
    if (! INSN_P (insn))
      continue;
    else if (INSN_DELETED_P (insn))
      {
	/* Shorten_branches would split this instruction again,
	   so transform it into a note.  */
	PUT_CODE (insn, NOTE);
	NOTE_LINE_NUMBER (insn) = NOTE_INSN_DELETED;
	NOTE_SOURCE_FILE (insn) = 0;
      }
    else if (GET_CODE (insn) == JUMP_INSN
	     /* Don't mess with ADDR_DIFF_VEC */
	     && (GET_CODE (PATTERN (insn)) == SET
		 || GET_CODE (PATTERN (insn)) == RETURN))
      {
	enum attr_type type = get_attr_type (insn);
	if (type == TYPE_CBRANCH)
	  {
	    rtx next, beyond;

	    if (get_attr_length (insn) > 4)
	      {
		rtx src = SET_SRC (PATTERN (insn));
		rtx olabel = XEXP (XEXP (src, 1), 0);
		int addr = INSN_ADDRESSES (INSN_UID (insn));
		rtx label = 0;
		int dest_uid = get_dest_uid (olabel, max_uid);
		struct far_branch *bp = uid_branch[dest_uid];

		/* redirect_jump needs a valid JUMP_LABEL, and it might delete
		   the label if the LABEL_NUSES count drops to zero.  There is
		   always a jump_optimize pass that sets these values, but it
		   proceeds to delete unreferenced code, and then if not
		   optimizing, to un-delete the deleted instructions, thus
		   leaving labels with too low uses counts.  */
		if (! optimize)
		  {
		    JUMP_LABEL (insn) = olabel;
		    LABEL_NUSES (olabel)++;
		  }
		if (! bp)
		  {
		    bp = (struct far_branch *) alloca (sizeof *bp);
		    uid_branch[dest_uid] = bp;
		    bp->prev = far_branch_list;
		    far_branch_list = bp;
		    bp->far_label
		      = XEXP (XEXP (SET_SRC (PATTERN (insn)), 1), 0);
		    LABEL_NUSES (bp->far_label)++;
		  }
		else
		  {
		    label = bp->near_label;
		    if (! label && bp->address - addr >= CONDJUMP_MIN)
		      {
			rtx block = bp->insert_place;

			if (GET_CODE (PATTERN (block)) == RETURN)
			  block = PREV_INSN (block);
			else
			  block = gen_block_redirect (block,
						      bp->address, 2);
			label = emit_label_after (gen_label_rtx (),
						  PREV_INSN (block));
			bp->near_label = label;
		      }
		    else if (label && ! NEXT_INSN (label))
		      {
			if (addr + 2 - bp->address <= CONDJUMP_MAX)
			  bp->insert_place = insn;
			else
			  gen_far_branch (bp);
		      }
		  }
		if (! label
		    || (NEXT_INSN (label) && bp->address - addr < CONDJUMP_MIN))
		  {
		    bp->near_label = label = gen_label_rtx ();
		    bp->insert_place = insn;
		    bp->address = addr;
		  }
		ok = redirect_jump (insn, label, 1);
		gcc_assert (ok);
	      }
	    else
	      {
		/* get_attr_length (insn) == 2 */
		/* Check if we have a pattern where reorg wants to redirect
		   the branch to a label from an unconditional branch that
		   is too far away.  */
		/* We can't use JUMP_LABEL here because it might be undefined
		   when not optimizing.  */
		/* A syntax error might cause beyond to be NULL_RTX.  */
		beyond
		  = next_active_insn (XEXP (XEXP (SET_SRC (PATTERN (insn)), 1),
					    0));

		if (beyond
		    && (GET_CODE (beyond) == JUMP_INSN
			|| ((beyond = next_active_insn (beyond))
			    && GET_CODE (beyond) == JUMP_INSN))
		    && GET_CODE (PATTERN (beyond)) == SET
		    && recog_memoized (beyond) == CODE_FOR_jump_compact
		    && ((INSN_ADDRESSES
			 (INSN_UID (XEXP (SET_SRC (PATTERN (beyond)), 0)))
			 - INSN_ADDRESSES (INSN_UID (insn)) + (unsigned) 252)
			> 252 + 258 + 2))
		  gen_block_redirect (beyond,
				      INSN_ADDRESSES (INSN_UID (beyond)), 1);
	      }

	    next = next_active_insn (insn);

	    if ((GET_CODE (next) == JUMP_INSN
		 || ((next = next_active_insn (next))
		     && GET_CODE (next) == JUMP_INSN))
		&& GET_CODE (PATTERN (next)) == SET
		&& recog_memoized (next) == CODE_FOR_jump_compact
		&& ((INSN_ADDRESSES
		     (INSN_UID (XEXP (SET_SRC (PATTERN (next)), 0)))
		     - INSN_ADDRESSES (INSN_UID (insn)) + (unsigned) 252)
		    > 252 + 258 + 2))
	      gen_block_redirect (next, INSN_ADDRESSES (INSN_UID (next)), 1);
	  }
	else if (type == TYPE_JUMP || type == TYPE_RETURN)
	  {
	    int addr = INSN_ADDRESSES (INSN_UID (insn));
	    rtx far_label = 0;
	    int dest_uid = 0;
	    struct far_branch *bp;

	    if (type == TYPE_JUMP)
	      {
		far_label = XEXP (SET_SRC (PATTERN (insn)), 0);
		dest_uid = get_dest_uid (far_label, max_uid);
		if (! dest_uid)
		  {
		    /* Parse errors can lead to labels outside
		      the insn stream.  */
		    if (! NEXT_INSN (far_label))
		      continue;

		    if (! optimize)
		      {
			JUMP_LABEL (insn) = far_label;
			LABEL_NUSES (far_label)++;
		      }
		    redirect_jump (insn, NULL_RTX, 1);
		    far_label = 0;
		  }
	      }
	    bp = uid_branch[dest_uid];
	    if (! bp)
	      {
		bp = (struct far_branch *) alloca (sizeof *bp);
		uid_branch[dest_uid] = bp;
		bp->prev = far_branch_list;
		far_branch_list = bp;
		bp->near_label = 0;
		bp->far_label = far_label;
		if (far_label)
		  LABEL_NUSES (far_label)++;
	      }
	    else if (bp->near_label && ! NEXT_INSN (bp->near_label))
	      if (addr - bp->address <= CONDJUMP_MAX)
		emit_label_after (bp->near_label, PREV_INSN (insn));
	      else
		{
		  gen_far_branch (bp);
		  bp->near_label = 0;
		}
	    else
	      bp->near_label = 0;
	    bp->address = addr;
	    bp->insert_place = insn;
	    if (! far_label)
	      emit_insn_before (gen_block_branch_redirect (const0_rtx), insn);
	    else
	      gen_block_redirect (insn, addr, bp->near_label ? 2 : 0);
	  }
      }
  /* Generate all pending far branches,
     and free our references to the far labels.  */
  while (far_branch_list)
    {
      if (far_branch_list->near_label
	  && ! NEXT_INSN (far_branch_list->near_label))
	gen_far_branch (far_branch_list);
      if (optimize
	  && far_branch_list->far_label
	  && ! --LABEL_NUSES (far_branch_list->far_label))
	delete_insn (far_branch_list->far_label);
      far_branch_list = far_branch_list->prev;
    }

  /* Instruction length information is no longer valid due to the new
     instructions that have been generated.  */
  init_insn_lengths ();
}

/* Dump out instruction addresses, which is useful for debugging the
   constant pool table stuff.

   If relaxing, output the label and pseudo-ops used to link together
   calls and the instruction which set the registers.  */

/* ??? The addresses printed by this routine for insns are nonsense for
   insns which are inside of a sequence where none of the inner insns have
   variable length.  This is because the second pass of shorten_branches
   does not bother to update them.  */

void
final_prescan_insn (rtx insn, rtx *opvec ATTRIBUTE_UNUSED,
		    int noperands ATTRIBUTE_UNUSED)
{
  if (TARGET_DUMPISIZE)
    fprintf (asm_out_file, "\n! at %04x\n", INSN_ADDRESSES (INSN_UID (insn)));

  if (TARGET_RELAX)
    {
      rtx note;

      note = find_reg_note (insn, REG_LABEL, NULL_RTX);
      if (note)
	{
	  rtx pattern;

	  pattern = PATTERN (insn);
	  if (GET_CODE (pattern) == PARALLEL)
	    pattern = XVECEXP (pattern, 0, 0);
	  switch (GET_CODE (pattern))
	    {
	    case SET:
	      if (GET_CODE (SET_SRC (pattern)) != CALL
		  && get_attr_type (insn) != TYPE_SFUNC)
		{
		  targetm.asm_out.internal_label
		    (asm_out_file, "L", CODE_LABEL_NUMBER (XEXP (note, 0)));
		  break;
		}
	      /* else FALLTHROUGH */
	    case CALL:
	      asm_fprintf (asm_out_file, "\t.uses %LL%d\n",
			   CODE_LABEL_NUMBER (XEXP (note, 0)));
	      break;

	    default:
	      gcc_unreachable ();
	    }
	}
    }
}

/* Dump out any constants accumulated in the final pass.  These will
   only be labels.  */

const char *
output_jump_label_table (void)
{
  int i;

  if (pool_size)
    {
      fprintf (asm_out_file, "\t.align 2\n");
      for (i = 0; i < pool_size; i++)
	{
	  pool_node *p = &pool_vector[i];

	  (*targetm.asm_out.internal_label) (asm_out_file, "L",
				     CODE_LABEL_NUMBER (p->label));
	  output_asm_insn (".long	%O0", &p->value);
	}
      pool_size = 0;
    }

  return "";
}

/* A full frame looks like:

   arg-5
   arg-4
   [ if current_function_anonymous_args
   arg-3
   arg-2
   arg-1
   arg-0 ]
   saved-fp
   saved-r10
   saved-r11
   saved-r12
   saved-pr
   local-n
   ..
   local-1
   local-0        <- fp points here.  */

/* Number of bytes pushed for anonymous args, used to pass information
   between expand_prologue and expand_epilogue.  */

/* Adjust the stack by SIZE bytes.  REG holds the rtl of the register to be
   adjusted.  If epilogue_p is zero, this is for a prologue; otherwise, it's
   for an epilogue and a negative value means that it's for a sibcall
   epilogue.  If LIVE_REGS_MASK is nonzero, it points to a HARD_REG_SET of
   all the registers that are about to be restored, and hence dead.  */

static void
output_stack_adjust (int size, rtx reg, int epilogue_p,
		     HARD_REG_SET *live_regs_mask)
{
  rtx (*emit_fn) (rtx) = epilogue_p ? &emit_insn : &frame_insn;
  if (size)
    {
      HOST_WIDE_INT align = STACK_BOUNDARY / BITS_PER_UNIT;

/* This test is bogus, as output_stack_adjust is used to re-align the
   stack.  */
#if 0
      gcc_assert (!(size % align));
#endif

      if (CONST_OK_FOR_ADD (size))
	emit_fn (GEN_ADD3 (reg, reg, GEN_INT (size)));
      /* Try to do it with two partial adjustments; however, we must make
	 sure that the stack is properly aligned at all times, in case
	 an interrupt occurs between the two partial adjustments.  */
      else if (CONST_OK_FOR_ADD (size / 2 & -align)
	       && CONST_OK_FOR_ADD (size - (size / 2 & -align)))
	{
	  emit_fn (GEN_ADD3 (reg, reg, GEN_INT (size / 2 & -align)));
	  emit_fn (GEN_ADD3 (reg, reg, GEN_INT (size - (size / 2 & -align))));
	}
      else
	{
	  rtx const_reg;
	  rtx insn;
	  int temp = epilogue_p ? 7 : (TARGET_SH5 ? 0 : 1);
	  int i;

	  /* If TEMP is invalid, we could temporarily save a general
	     register to MACL.  However, there is currently no need
	     to handle this case, so just die when we see it.  */
	  if (epilogue_p < 0
	      || current_function_interrupt
	      || ! call_really_used_regs[temp] || fixed_regs[temp])
	    temp = -1;
	  if (temp < 0 && ! current_function_interrupt
	      && (TARGET_SHMEDIA || epilogue_p >= 0))
	    {
	      HARD_REG_SET temps;
	      COPY_HARD_REG_SET (temps, call_used_reg_set);
	      AND_COMPL_HARD_REG_SET (temps, call_fixed_reg_set);
	      if (epilogue_p > 0)
		{
		  int nreg = 0;
		  if (current_function_return_rtx)
		    {
		      enum machine_mode mode;
		      mode = GET_MODE (current_function_return_rtx);
		      if (BASE_RETURN_VALUE_REG (mode) == FIRST_RET_REG)
			nreg = HARD_REGNO_NREGS (FIRST_RET_REG, mode);
		    }
		  for (i = 0; i < nreg; i++)
		    CLEAR_HARD_REG_BIT (temps, FIRST_RET_REG + i);
		  if (current_function_calls_eh_return)
		    {
		      CLEAR_HARD_REG_BIT (temps, EH_RETURN_STACKADJ_REGNO);
		      for (i = 0; i <= 3; i++)
			CLEAR_HARD_REG_BIT (temps, EH_RETURN_DATA_REGNO (i));
		    }
		}
	      if (TARGET_SHMEDIA && epilogue_p < 0)
		for (i = FIRST_TARGET_REG; i <= LAST_TARGET_REG; i++)
		  CLEAR_HARD_REG_BIT (temps, i);
	      if (epilogue_p <= 0)
		{
		  for (i = FIRST_PARM_REG;
		       i < FIRST_PARM_REG + NPARM_REGS (SImode); i++)
		    CLEAR_HARD_REG_BIT (temps, i);
		  if (cfun->static_chain_decl != NULL)
		    CLEAR_HARD_REG_BIT (temps, STATIC_CHAIN_REGNUM);
		}
	      temp = scavenge_reg (&temps);
	    }
	  if (temp < 0 && live_regs_mask)
	    {
	      HARD_REG_SET temps;

	      COPY_HARD_REG_SET (temps, *live_regs_mask);
	      CLEAR_HARD_REG_BIT (temps, REGNO (reg));
	      temp = scavenge_reg (&temps);
	    }
	  if (temp < 0)
	    {
	      rtx adj_reg, tmp_reg, mem;
	      
	      /* If we reached here, the most likely case is the (sibcall)
		 epilogue for non SHmedia.  Put a special push/pop sequence
		 for such case as the last resort.  This looks lengthy but
		 would not be problem because it seems to be very
		 rare.  */
	      
	      gcc_assert (!TARGET_SHMEDIA && epilogue_p);
	      

	       /* ??? There is still the slight possibility that r4 or
		  r5 have been reserved as fixed registers or assigned
		  as global registers, and they change during an
		  interrupt.  There are possible ways to handle this:
		     
		  - If we are adjusting the frame pointer (r14), we can do
		    with a single temp register and an ordinary push / pop
		    on the stack.
		  - Grab any call-used or call-saved registers (i.e. not
		    fixed or globals) for the temps we need.  We might
		    also grab r14 if we are adjusting the stack pointer.
		    If we can't find enough available registers, issue
		    a diagnostic and die - the user must have reserved
		    way too many registers.
		 But since all this is rather unlikely to happen and
		 would require extra testing, we just die if r4 / r5
		 are not available.  */
	      gcc_assert (!fixed_regs[4] && !fixed_regs[5]
			  && !global_regs[4] && !global_regs[5]);

	      adj_reg = gen_rtx_REG (GET_MODE (reg), 4);
	      tmp_reg = gen_rtx_REG (GET_MODE (reg), 5);
	      emit_move_insn (gen_tmp_stack_mem (Pmode, reg), adj_reg);
	      emit_insn (GEN_MOV (adj_reg, GEN_INT (size)));
	      emit_insn (GEN_ADD3 (adj_reg, adj_reg, reg));
	      mem = gen_tmp_stack_mem (Pmode, gen_rtx_PRE_DEC (Pmode, adj_reg));
	      emit_move_insn (mem, tmp_reg);
	      emit_move_insn (tmp_reg, gen_tmp_stack_mem (Pmode, reg));
	      mem = gen_tmp_stack_mem (Pmode, gen_rtx_PRE_DEC (Pmode, adj_reg));
	      emit_move_insn (mem, tmp_reg);
	      emit_move_insn (reg, adj_reg);
	      mem = gen_tmp_stack_mem (Pmode, gen_rtx_POST_INC (Pmode, reg));
	      emit_move_insn (adj_reg, mem);
	      mem = gen_tmp_stack_mem (Pmode, gen_rtx_POST_INC (Pmode, reg));
	      emit_move_insn (tmp_reg, mem);
	      /* Tell flow the insns that pop r4/r5 aren't dead.  */
	      emit_insn (gen_rtx_USE (VOIDmode, tmp_reg));
	      emit_insn (gen_rtx_USE (VOIDmode, adj_reg));
	      return;
	    }
	  const_reg = gen_rtx_REG (GET_MODE (reg), temp);

	  /* If SIZE is negative, subtract the positive value.
	     This sometimes allows a constant pool entry to be shared
	     between prologue and epilogue code.  */
	  if (size < 0)
	    {
	      emit_insn (GEN_MOV (const_reg, GEN_INT (-size)));
	      insn = emit_fn (GEN_SUB3 (reg, reg, const_reg));
	    }
	  else
	    {
	      emit_insn (GEN_MOV (const_reg, GEN_INT (size)));
	      insn = emit_fn (GEN_ADD3 (reg, reg, const_reg));
	    }
	  if (! epilogue_p)
	    REG_NOTES (insn)
	      = (gen_rtx_EXPR_LIST
		 (REG_FRAME_RELATED_EXPR,
		  gen_rtx_SET (VOIDmode, reg,
			       gen_rtx_PLUS (SImode, reg, GEN_INT (size))),
		  REG_NOTES (insn)));
	}
    }
}

static rtx
frame_insn (rtx x)
{
  x = emit_insn (x);
  RTX_FRAME_RELATED_P (x) = 1;
  return x;
}

/* Output RTL to push register RN onto the stack.  */

static rtx
push (int rn)
{
  rtx x;
  if (rn == FPUL_REG)
    x = gen_push_fpul ();
  else if (rn == FPSCR_REG)
    x = gen_push_fpscr ();
  else if ((TARGET_SH4 || TARGET_SH2A_DOUBLE) && TARGET_FMOVD && ! TARGET_FPU_SINGLE
	   && FP_OR_XD_REGISTER_P (rn))
    {
      if (FP_REGISTER_P (rn) && (rn - FIRST_FP_REG) & 1)
	return NULL_RTX;
      x = gen_push_4 (gen_rtx_REG (DFmode, rn));
    }
  else if (TARGET_SH2E && FP_REGISTER_P (rn))
    x = gen_push_e (gen_rtx_REG (SFmode, rn));
  else
    x = gen_push (gen_rtx_REG (SImode, rn));

  x = frame_insn (x);
  REG_NOTES (x)
    = gen_rtx_EXPR_LIST (REG_INC,
			 gen_rtx_REG (SImode, STACK_POINTER_REGNUM), 0);
  return x;
}

/* Output RTL to pop register RN from the stack.  */

static void
pop (int rn)
{
  rtx x;
  if (rn == FPUL_REG)
    x = gen_pop_fpul ();
  else if (rn == FPSCR_REG)
    x = gen_pop_fpscr ();
  else if ((TARGET_SH4 || TARGET_SH2A_DOUBLE) && TARGET_FMOVD && ! TARGET_FPU_SINGLE
	   && FP_OR_XD_REGISTER_P (rn))
    {
      if (FP_REGISTER_P (rn) && (rn - FIRST_FP_REG) & 1)
	return;
      x = gen_pop_4 (gen_rtx_REG (DFmode, rn));
    }
  else if (TARGET_SH2E && FP_REGISTER_P (rn))
    x = gen_pop_e (gen_rtx_REG (SFmode, rn));
  else
    x = gen_pop (gen_rtx_REG (SImode, rn));

  x = emit_insn (x);
  REG_NOTES (x)
    = gen_rtx_EXPR_LIST (REG_INC,
			 gen_rtx_REG (SImode, STACK_POINTER_REGNUM), 0);
}

/* Generate code to push the regs specified in the mask.  */

static void
push_regs (HARD_REG_SET *mask, int interrupt_handler)
{
  int i;
  int skip_fpscr = 0;

  /* Push PR last; this gives better latencies after the prologue, and
     candidates for the return delay slot when there are no general
     registers pushed.  */
  for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
    {
      /* If this is an interrupt handler, and the SZ bit varies,
	 and we have to push any floating point register, we need
	 to switch to the correct precision first.  */
      if (i == FIRST_FP_REG && interrupt_handler && TARGET_FMOVD
	  && hard_regs_intersect_p (mask, &reg_class_contents[DF_REGS]))
	{
	  HARD_REG_SET unsaved;

	  push (FPSCR_REG);
	  COMPL_HARD_REG_SET (unsaved, *mask);
	  fpscr_set_from_mem (NORMAL_MODE (FP_MODE), unsaved);
	  skip_fpscr = 1;
	}
      if (i != PR_REG
	  && (i != FPSCR_REG || ! skip_fpscr)
	  && TEST_HARD_REG_BIT (*mask, i))
	push (i);
    }
  if (TEST_HARD_REG_BIT (*mask, PR_REG))
    push (PR_REG);
}

/* Calculate how much extra space is needed to save all callee-saved
   target registers.
   LIVE_REGS_MASK is the register mask calculated by calc_live_regs.  */

static int
shmedia_target_regs_stack_space (HARD_REG_SET *live_regs_mask)
{
  int reg;
  int stack_space = 0;
  int interrupt_handler = sh_cfun_interrupt_handler_p ();

  for (reg = LAST_TARGET_REG; reg >= FIRST_TARGET_REG; reg--)
    if ((! call_really_used_regs[reg] || interrupt_handler)
        && ! TEST_HARD_REG_BIT (*live_regs_mask, reg))
      /* Leave space to save this target register on the stack,
	 in case target register allocation wants to use it.  */
      stack_space += GET_MODE_SIZE (REGISTER_NATURAL_MODE (reg));
  return stack_space;
}

/* Decide whether we should reserve space for callee-save target registers,
   in case target register allocation wants to use them.  REGS_SAVED is
   the space, in bytes, that is already required for register saves.
   LIVE_REGS_MASK is the register mask calculated by calc_live_regs.  */

static int
shmedia_reserve_space_for_target_registers_p (int regs_saved,
					      HARD_REG_SET *live_regs_mask)
{
  if (optimize_size)
    return 0;
  return shmedia_target_regs_stack_space (live_regs_mask) <= regs_saved;
}

/* Decide how much space to reserve for callee-save target registers
   in case target register allocation wants to use them.
   LIVE_REGS_MASK is the register mask calculated by calc_live_regs.  */

static int
shmedia_target_regs_stack_adjust (HARD_REG_SET *live_regs_mask)
{
  if (shmedia_space_reserved_for_target_registers)
    return shmedia_target_regs_stack_space (live_regs_mask);
  else
    return 0;
}

/* Work out the registers which need to be saved, both as a mask and a
   count of saved words.  Return the count.

   If doing a pragma interrupt function, then push all regs used by the
   function, and if we call another function (we can tell by looking at PR),
   make sure that all the regs it clobbers are safe too.  */

static int
calc_live_regs (HARD_REG_SET *live_regs_mask)
{
  unsigned int reg;
  int count;
  tree attrs;
  bool interrupt_or_trapa_handler, trapa_handler, interrupt_handler;
  bool nosave_low_regs;
  int pr_live, has_call;

  attrs = DECL_ATTRIBUTES (current_function_decl);
  interrupt_or_trapa_handler = sh_cfun_interrupt_handler_p ();
  trapa_handler = lookup_attribute ("trapa_handler", attrs) != NULL_TREE;
  interrupt_handler = interrupt_or_trapa_handler && ! trapa_handler;
  nosave_low_regs = lookup_attribute ("nosave_low_regs", attrs) != NULL_TREE;

  CLEAR_HARD_REG_SET (*live_regs_mask);
  if ((TARGET_SH4 || TARGET_SH2A_DOUBLE) && TARGET_FMOVD && interrupt_handler
      && regs_ever_live[FPSCR_REG])
    target_flags &= ~MASK_FPU_SINGLE;
  /* If we can save a lot of saves by switching to double mode, do that.  */
  else if ((TARGET_SH4 || TARGET_SH2A_DOUBLE) && TARGET_FMOVD && TARGET_FPU_SINGLE)
    for (count = 0, reg = FIRST_FP_REG; reg <= LAST_FP_REG; reg += 2)
      if (regs_ever_live[reg] && regs_ever_live[reg+1]
	  && (! call_really_used_regs[reg]
	      || interrupt_handler)
	  && ++count > 2)
	{
	  target_flags &= ~MASK_FPU_SINGLE;
	  break;
	}
  /* PR_MEDIA_REG is a general purpose register, thus global_alloc already
     knows how to use it.  That means the pseudo originally allocated for
     the initial value can become the PR_MEDIA_REG hard register, as seen for
     execute/20010122-1.c:test9.  */
  if (TARGET_SHMEDIA)
    /* ??? this function is called from initial_elimination_offset, hence we
       can't use the result of sh_media_register_for_return here.  */
    pr_live = sh_pr_n_sets ();
  else
    {
      rtx pr_initial = has_hard_reg_initial_val (Pmode, PR_REG);
      pr_live = (pr_initial
		 ? (GET_CODE (pr_initial) != REG
		    || REGNO (pr_initial) != (PR_REG))
		 : regs_ever_live[PR_REG]);
      /* For Shcompact, if not optimizing, we end up with a memory reference
	 using the return address pointer for __builtin_return_address even
	 though there is no actual need to put the PR register on the stack.  */
      pr_live |= regs_ever_live[RETURN_ADDRESS_POINTER_REGNUM];
    }
  /* Force PR to be live if the prologue has to call the SHmedia
     argument decoder or register saver.  */
  if (TARGET_SHCOMPACT
      && ((current_function_args_info.call_cookie
	   & ~ CALL_COOKIE_RET_TRAMP (1))
	  || current_function_has_nonlocal_label))
    pr_live = 1;
  has_call = TARGET_SHMEDIA ? ! leaf_function_p () : pr_live;
  for (count = 0, reg = FIRST_PSEUDO_REGISTER; reg-- != 0; )
    {
      if (reg == (TARGET_SHMEDIA ? PR_MEDIA_REG : PR_REG)
	  ? pr_live
	  : interrupt_handler
	  ? (/* Need to save all the regs ever live.  */
	     (regs_ever_live[reg]
	      || (call_really_used_regs[reg]
		  && (! fixed_regs[reg] || reg == MACH_REG || reg == MACL_REG
		      || reg == PIC_OFFSET_TABLE_REGNUM)
		  && has_call)
	      || (TARGET_SHMEDIA && has_call
		  && REGISTER_NATURAL_MODE (reg) == SImode
		  && (GENERAL_REGISTER_P (reg) || TARGET_REGISTER_P (reg))))
	     && reg != STACK_POINTER_REGNUM && reg != ARG_POINTER_REGNUM
	     && reg != RETURN_ADDRESS_POINTER_REGNUM
	     && reg != T_REG && reg != GBR_REG
	     /* Push fpscr only on targets which have FPU */
	     && (reg != FPSCR_REG || TARGET_FPU_ANY))
	  : (/* Only push those regs which are used and need to be saved.  */
	     (TARGET_SHCOMPACT
	      && flag_pic
	      && current_function_args_info.call_cookie
	      && reg == PIC_OFFSET_TABLE_REGNUM)
	     || (regs_ever_live[reg]
		 && (!call_really_used_regs[reg]
		     || (trapa_handler && reg == FPSCR_REG && TARGET_FPU_ANY)))
	     || (current_function_calls_eh_return
		 && (reg == EH_RETURN_DATA_REGNO (0)
		     || reg == EH_RETURN_DATA_REGNO (1)
		     || reg == EH_RETURN_DATA_REGNO (2)
		     || reg == EH_RETURN_DATA_REGNO (3)))
	     || ((reg == MACL_REG || reg == MACH_REG)
		 && regs_ever_live[reg]
		 && sh_cfun_attr_renesas_p ())
	     ))
	{
	  SET_HARD_REG_BIT (*live_regs_mask, reg);
	  count += GET_MODE_SIZE (REGISTER_NATURAL_MODE (reg));

	  if ((TARGET_SH4 || TARGET_SH2A_DOUBLE || TARGET_SH5) && TARGET_FMOVD
	      && GET_MODE_CLASS (REGISTER_NATURAL_MODE (reg)) == MODE_FLOAT)
	    {
	      if (FP_REGISTER_P (reg))
		{
		  if (! TARGET_FPU_SINGLE && ! regs_ever_live[reg ^ 1])
		    {
		      SET_HARD_REG_BIT (*live_regs_mask, (reg ^ 1));
		      count += GET_MODE_SIZE (REGISTER_NATURAL_MODE (reg ^ 1));
		    }
		}
	      else if (XD_REGISTER_P (reg))
		{
		  /* Must switch to double mode to access these registers.  */
		  target_flags &= ~MASK_FPU_SINGLE;
		}
	    }
	}
      if (nosave_low_regs && reg == R8_REG)
	break;
    }
  /* If we have a target register optimization pass after prologue / epilogue
     threading, we need to assume all target registers will be live even if
     they aren't now.  */
  if (flag_branch_target_load_optimize2
      && TARGET_SAVE_ALL_TARGET_REGS
      && shmedia_space_reserved_for_target_registers)
    for (reg = LAST_TARGET_REG; reg >= FIRST_TARGET_REG; reg--)
      if ((! call_really_used_regs[reg] || interrupt_handler)
	  && ! TEST_HARD_REG_BIT (*live_regs_mask, reg))
	{
	  SET_HARD_REG_BIT (*live_regs_mask, reg);
	  count += GET_MODE_SIZE (REGISTER_NATURAL_MODE (reg));
	}
  /* If this is an interrupt handler, we don't have any call-clobbered
     registers we can conveniently use for target register save/restore.
     Make sure we save at least one general purpose register when we need
     to save target registers.  */
  if (interrupt_handler
      && hard_regs_intersect_p (live_regs_mask,
				&reg_class_contents[TARGET_REGS])
      && ! hard_regs_intersect_p (live_regs_mask,
				  &reg_class_contents[GENERAL_REGS]))
    {
      SET_HARD_REG_BIT (*live_regs_mask, R0_REG);
      count += GET_MODE_SIZE (REGISTER_NATURAL_MODE (R0_REG));
    }

  return count;
}

/* Code to generate prologue and epilogue sequences */

/* PUSHED is the number of bytes that are being pushed on the
   stack for register saves.  Return the frame size, padded
   appropriately so that the stack stays properly aligned.  */
static HOST_WIDE_INT
rounded_frame_size (int pushed)
{
  HOST_WIDE_INT size = get_frame_size ();
  HOST_WIDE_INT align = STACK_BOUNDARY / BITS_PER_UNIT;

  return ((size + pushed + align - 1) & -align) - pushed;
}

/* Choose a call-clobbered target-branch register that remains
   unchanged along the whole function.  We set it up as the return
   value in the prologue.  */
int
sh_media_register_for_return (void)
{
  int regno;
  int tr0_used;

  if (! current_function_is_leaf)
    return -1;
  if (lookup_attribute ("interrupt_handler",
			DECL_ATTRIBUTES (current_function_decl)))
    return -1;
  if (sh_cfun_interrupt_handler_p ())
    return -1;

  tr0_used = flag_pic && regs_ever_live[PIC_OFFSET_TABLE_REGNUM];

  for (regno = FIRST_TARGET_REG + tr0_used; regno <= LAST_TARGET_REG; regno++)
    if (call_really_used_regs[regno] && ! regs_ever_live[regno])
      return regno;

  return -1;
}

/* The maximum registers we need to save are:
   - 62 general purpose registers (r15 is stack pointer, r63 is zero)
   - 32 floating point registers (for each pair, we save none,
         one single precision value, or a double precision value).
   -  8 target registers
   -  add 1 entry for a delimiter.  */
#define MAX_SAVED_REGS (62+32+8)

typedef struct save_entry_s
{
  unsigned char reg;
  unsigned char mode;
  short offset;
} save_entry;

#define MAX_TEMPS 4

/* There will be a delimiter entry with VOIDmode both at the start and the
   end of a filled in schedule.  The end delimiter has the offset of the
   save with the smallest (i.e. most negative) offset.  */
typedef struct save_schedule_s
{
  save_entry entries[MAX_SAVED_REGS + 2];
  int temps[MAX_TEMPS+1];
} save_schedule;

/* Fill in SCHEDULE according to LIVE_REGS_MASK.  If RESTORE is nonzero,
   use reverse order.  Returns the last entry written to (not counting
   the delimiter).  OFFSET_BASE is a number to be added to all offset
   entries.  */

static save_entry *
sh5_schedule_saves (HARD_REG_SET *live_regs_mask, save_schedule *schedule,
		    int offset_base)
{
  int align, i;
  save_entry *entry = schedule->entries;
  int tmpx = 0;
  int offset;

  if (! current_function_interrupt)
    for (i = FIRST_GENERAL_REG; tmpx < MAX_TEMPS && i <= LAST_GENERAL_REG; i++)
      if (call_really_used_regs[i] && ! fixed_regs[i] && i != PR_MEDIA_REG
	  && ! FUNCTION_ARG_REGNO_P (i)
	  && i != FIRST_RET_REG
	  && ! (cfun->static_chain_decl != NULL && i == STATIC_CHAIN_REGNUM)
	  && ! (current_function_calls_eh_return
		&& (i == EH_RETURN_STACKADJ_REGNO
		    || ((unsigned) i >= EH_RETURN_DATA_REGNO (0)
			&& (unsigned) i <= EH_RETURN_DATA_REGNO (3)))))
	schedule->temps[tmpx++] = i;
  entry->reg = -1;
  entry->mode = VOIDmode;
  entry->offset = offset_base;
  entry++;
  /* We loop twice: first, we save 8-byte aligned registers in the
     higher addresses, that are known to be aligned.  Then, we
     proceed to saving 32-bit registers that don't need 8-byte
     alignment.
     If this is an interrupt function, all registers that need saving
     need to be saved in full.  moreover, we need to postpone saving
     target registers till we have saved some general purpose registers
     we can then use as scratch registers.  */
  offset = offset_base;
  for (align = 1; align >= 0; align--)
    {
      for (i = FIRST_PSEUDO_REGISTER - 1; i >= 0; i--)
	if (TEST_HARD_REG_BIT (*live_regs_mask, i))
	  {
	    enum machine_mode mode = REGISTER_NATURAL_MODE (i);
	    int reg = i;

	    if (current_function_interrupt)
	      {
		if (TARGET_REGISTER_P (i))
		  continue;
		if (GENERAL_REGISTER_P (i))
		  mode = DImode;
	      }
	    if (mode == SFmode && (i % 2) == 1
		&& ! TARGET_FPU_SINGLE && FP_REGISTER_P (i)
		&& (TEST_HARD_REG_BIT (*live_regs_mask, (i ^ 1))))
	      {
		mode = DFmode;
		i--;
		reg--;
	      }

	    /* If we're doing the aligned pass and this is not aligned,
	       or we're doing the unaligned pass and this is aligned,
	       skip it.  */
	    if ((GET_MODE_SIZE (mode) % (STACK_BOUNDARY / BITS_PER_UNIT) == 0)
		!= align)
	      continue;

	    if (current_function_interrupt
		&& GENERAL_REGISTER_P (i)
		&& tmpx < MAX_TEMPS)
	      schedule->temps[tmpx++] = i;

	    offset -= GET_MODE_SIZE (mode);
	    entry->reg = i;
	    entry->mode = mode;
	    entry->offset = offset;
	    entry++;
	  }
      if (align && current_function_interrupt)
	for (i = LAST_TARGET_REG; i >= FIRST_TARGET_REG; i--)
	  if (TEST_HARD_REG_BIT (*live_regs_mask, i))
	    {
	      offset -= GET_MODE_SIZE (DImode);
	      entry->reg = i;
	      entry->mode = DImode;
	      entry->offset = offset;
	      entry++;
	    }
    }
  entry->reg = -1;
  entry->mode = VOIDmode;
  entry->offset = offset;
  schedule->temps[tmpx] = -1;
  return entry - 1;
}

void
sh_expand_prologue (void)
{
  HARD_REG_SET live_regs_mask;
  int d, i;
  int d_rounding = 0;
  int save_flags = target_flags;
  int pretend_args;
  tree sp_switch_attr
    = lookup_attribute ("sp_switch", DECL_ATTRIBUTES (current_function_decl));

  current_function_interrupt = sh_cfun_interrupt_handler_p ();

  /* We have pretend args if we had an object sent partially in registers
     and partially on the stack, e.g. a large structure.  */
  pretend_args = current_function_pretend_args_size;
  if (TARGET_VARARGS_PRETEND_ARGS (current_function_decl)
      && (NPARM_REGS(SImode)
	  > current_function_args_info.arg_count[(int) SH_ARG_INT]))
    pretend_args = 0;
  output_stack_adjust (-pretend_args
		       - current_function_args_info.stack_regs * 8,
		       stack_pointer_rtx, 0, NULL);

  if (TARGET_SHCOMPACT && flag_pic && current_function_args_info.call_cookie)
    /* We're going to use the PIC register to load the address of the
       incoming-argument decoder and/or of the return trampoline from
       the GOT, so make sure the PIC register is preserved and
       initialized.  */
    regs_ever_live[PIC_OFFSET_TABLE_REGNUM] = 1;

  if (TARGET_SHCOMPACT
      && (current_function_args_info.call_cookie & ~ CALL_COOKIE_RET_TRAMP(1)))
    {
      int reg;

      /* First, make all registers with incoming arguments that will
	 be pushed onto the stack live, so that register renaming
	 doesn't overwrite them.  */
      for (reg = 0; reg < NPARM_REGS (SImode); reg++)
	if (CALL_COOKIE_STACKSEQ_GET (current_function_args_info.call_cookie)
	    >= NPARM_REGS (SImode) - reg)
	  for (; reg < NPARM_REGS (SImode); reg++)
	    emit_insn (gen_shcompact_preserve_incoming_args
		       (gen_rtx_REG (SImode, FIRST_PARM_REG + reg)));
	else if (CALL_COOKIE_INT_REG_GET
		 (current_function_args_info.call_cookie, reg) == 1)
	  emit_insn (gen_shcompact_preserve_incoming_args
		     (gen_rtx_REG (SImode, FIRST_PARM_REG + reg)));

      emit_move_insn (gen_rtx_REG (Pmode, MACL_REG),
		      stack_pointer_rtx);
      emit_move_insn (gen_rtx_REG (SImode, R0_REG),
		      GEN_INT (current_function_args_info.call_cookie));
      emit_move_insn (gen_rtx_REG (SImode, MACH_REG),
		      gen_rtx_REG (SImode, R0_REG));
    }
  else if (TARGET_SHMEDIA)
    {
      int tr = sh_media_register_for_return ();

      if (tr >= 0)
	{
	  rtx insn = emit_move_insn (gen_rtx_REG (DImode, tr),
				     gen_rtx_REG (DImode, PR_MEDIA_REG));

	  /* ??? We should suppress saving pr when we don't need it, but this
	     is tricky because of builtin_return_address.  */

	  /* If this function only exits with sibcalls, this copy
	     will be flagged as dead.  */
	  REG_NOTES (insn) = gen_rtx_EXPR_LIST (REG_MAYBE_DEAD,
						const0_rtx,
						REG_NOTES (insn));
	}
    }

  /* Emit the code for SETUP_VARARGS.  */
  if (current_function_stdarg)
    {
      if (TARGET_VARARGS_PRETEND_ARGS (current_function_decl))
	{
	  /* Push arg regs as if they'd been provided by caller in stack.  */
	  for (i = 0; i < NPARM_REGS(SImode); i++)
	    {
	      int rn = NPARM_REGS(SImode) + FIRST_PARM_REG - i - 1;
	      rtx insn;

	      if (i >= (NPARM_REGS(SImode)
			- current_function_args_info.arg_count[(int) SH_ARG_INT]
			))
		break;
	      insn = push (rn);
	      RTX_FRAME_RELATED_P (insn) = 0;
	    }
	}
    }

  /* If we're supposed to switch stacks at function entry, do so now.  */
  if (sp_switch_attr)
    {
      /* The argument specifies a variable holding the address of the
	 stack the interrupt function should switch to/from at entry/exit.  */
      const char *s
	= ggc_strdup (TREE_STRING_POINTER (TREE_VALUE (sp_switch_attr)));
      rtx sp_switch = gen_rtx_SYMBOL_REF (Pmode, s);

      emit_insn (gen_sp_switch_1 (sp_switch));
    }

  d = calc_live_regs (&live_regs_mask);
  /* ??? Maybe we could save some switching if we can move a mode switch
     that already happens to be at the function start into the prologue.  */
  if (target_flags != save_flags && ! current_function_interrupt)
    emit_insn (gen_toggle_sz ());

  if (TARGET_SH5)
    {
      int offset_base, offset;
      rtx r0 = NULL_RTX;
      int offset_in_r0 = -1;
      int sp_in_r0 = 0;
      int tregs_space = shmedia_target_regs_stack_adjust (&live_regs_mask);
      int total_size, save_size;
      save_schedule schedule;
      save_entry *entry;
      int *tmp_pnt;

      if (call_really_used_regs[R0_REG] && ! fixed_regs[R0_REG]
	  && ! current_function_interrupt)
	r0 = gen_rtx_REG (Pmode, R0_REG);

      /* D is the actual number of bytes that we need for saving registers,
	 however, in initial_elimination_offset we have committed to using
	 an additional TREGS_SPACE amount of bytes - in order to keep both
	 addresses to arguments supplied by the caller and local variables
	 valid, we must keep this gap.  Place it between the incoming
	 arguments and the actually saved registers in a bid to optimize
	 locality of reference.  */
      total_size = d + tregs_space;
      total_size += rounded_frame_size (total_size);
      save_size = total_size - rounded_frame_size (d);
      if (save_size % (STACK_BOUNDARY / BITS_PER_UNIT))
	d_rounding = ((STACK_BOUNDARY / BITS_PER_UNIT)
			- save_size % (STACK_BOUNDARY / BITS_PER_UNIT));

      /* If adjusting the stack in a single step costs nothing extra, do so.
	 I.e. either if a single addi is enough, or we need a movi anyway,
	 and we don't exceed the maximum offset range (the test for the
	 latter is conservative for simplicity).  */
      if (TARGET_SHMEDIA
	  && (CONST_OK_FOR_I10 (-total_size)
	      || (! CONST_OK_FOR_I10 (-(save_size + d_rounding))
		  && total_size <= 2044)))
	d_rounding = total_size - save_size;

      offset_base = d + d_rounding;

      output_stack_adjust (-(save_size + d_rounding), stack_pointer_rtx,
			   0, NULL);

      sh5_schedule_saves (&live_regs_mask, &schedule, offset_base);
      tmp_pnt = schedule.temps;
      for (entry = &schedule.entries[1]; entry->mode != VOIDmode; entry++)
        {
	  enum machine_mode mode = entry->mode;
	  unsigned int reg = entry->reg;
	  rtx reg_rtx, mem_rtx, pre_dec = NULL_RTX;
	  rtx orig_reg_rtx;

	  offset = entry->offset;

	  reg_rtx = gen_rtx_REG (mode, reg);

	  mem_rtx = gen_frame_mem (mode,
				   gen_rtx_PLUS (Pmode,
						 stack_pointer_rtx,
						 GEN_INT (offset)));

	  GO_IF_LEGITIMATE_ADDRESS (mode, XEXP (mem_rtx, 0), try_pre_dec);

	  gcc_assert (r0);
	  mem_rtx = NULL_RTX;

	try_pre_dec:
	  do
	    if (HAVE_PRE_DECREMENT
		&& (offset_in_r0 - offset == GET_MODE_SIZE (mode)
		    || mem_rtx == NULL_RTX
		    || reg == PR_REG || SPECIAL_REGISTER_P (reg)))
	      {
		pre_dec = gen_frame_mem (mode, gen_rtx_PRE_DEC (Pmode, r0));

		GO_IF_LEGITIMATE_ADDRESS (mode, XEXP (pre_dec, 0),
					  pre_dec_ok);

		pre_dec = NULL_RTX;

		break;

	      pre_dec_ok:
		mem_rtx = NULL_RTX;
		offset += GET_MODE_SIZE (mode);
	      }
	  while (0);

	  if (mem_rtx != NULL_RTX)
	    goto addr_ok;

	  if (offset_in_r0 == -1)
	    {
	      emit_move_insn (r0, GEN_INT (offset));
	      offset_in_r0 = offset;
	    }
	  else if (offset != offset_in_r0)
	    {
	      emit_move_insn (r0,
			      gen_rtx_PLUS
			      (Pmode, r0,
			       GEN_INT (offset - offset_in_r0)));
	      offset_in_r0 += offset - offset_in_r0;
	    }

	  if (pre_dec != NULL_RTX)
	    {
	      if (! sp_in_r0)
		{
		  emit_move_insn (r0,
				  gen_rtx_PLUS
				  (Pmode, r0, stack_pointer_rtx));
		  sp_in_r0 = 1;
		}

	      offset -= GET_MODE_SIZE (mode);
	      offset_in_r0 -= GET_MODE_SIZE (mode);

	      mem_rtx = pre_dec;
	    }
	  else if (sp_in_r0)
	    mem_rtx = gen_frame_mem (mode, r0);
	  else
	    mem_rtx = gen_frame_mem (mode,
				     gen_rtx_PLUS (Pmode,
						   stack_pointer_rtx,
						   r0));

	  /* We must not use an r0-based address for target-branch
	     registers or for special registers without pre-dec
	     memory addresses, since we store their values in r0
	     first.  */
	  gcc_assert (!TARGET_REGISTER_P (reg)
		      && ((reg != PR_REG && !SPECIAL_REGISTER_P (reg))
			  || mem_rtx == pre_dec));
	  
	addr_ok:
	  orig_reg_rtx = reg_rtx;
	  if (TARGET_REGISTER_P (reg)
	      || ((reg == PR_REG || SPECIAL_REGISTER_P (reg))
		  && mem_rtx != pre_dec))
	    {
	      rtx tmp_reg = gen_rtx_REG (GET_MODE (reg_rtx), *tmp_pnt);

	      emit_move_insn (tmp_reg, reg_rtx);

	      if (REGNO (tmp_reg) == R0_REG)
		{
		  offset_in_r0 = -1;
		  sp_in_r0 = 0;
		  gcc_assert (!refers_to_regno_p
			      (R0_REG, R0_REG+1, mem_rtx, (rtx *) 0));
		}

	      if (*++tmp_pnt <= 0)
		tmp_pnt = schedule.temps;

	      reg_rtx = tmp_reg;
	    }
	  {
	    rtx insn;

	    /* Mark as interesting for dwarf cfi generator */
	    insn = emit_move_insn (mem_rtx, reg_rtx);
	    RTX_FRAME_RELATED_P (insn) = 1;
	    /* If we use an intermediate register for the save, we can't
	       describe this exactly in cfi as a copy of the to-be-saved
	       register into the temporary register and then the temporary
	       register on the stack, because the temporary register can
	       have a different natural size than the to-be-saved register.
	       Thus, we gloss over the intermediate copy and pretend we do
	       a direct save from the to-be-saved register.  */
	    if (REGNO (reg_rtx) != reg)
	      {
		rtx set, note_rtx;

		set = gen_rtx_SET (VOIDmode, mem_rtx, orig_reg_rtx);
		note_rtx = gen_rtx_EXPR_LIST (REG_FRAME_RELATED_EXPR, set,
					      REG_NOTES (insn));
		REG_NOTES (insn) = note_rtx;
	      }

	    if (TARGET_SHCOMPACT && (offset_in_r0 != -1))
	      {
		rtx reg_rtx = gen_rtx_REG (mode, reg);
		rtx set, note_rtx;
		rtx mem_rtx = gen_frame_mem (mode,
					     gen_rtx_PLUS (Pmode,
							   stack_pointer_rtx,
							   GEN_INT (offset)));

		set = gen_rtx_SET (VOIDmode, mem_rtx, reg_rtx);
		note_rtx = gen_rtx_EXPR_LIST (REG_FRAME_RELATED_EXPR, set,
					      REG_NOTES (insn));
		REG_NOTES (insn) = note_rtx;
	      }
	  }
	}

      gcc_assert (entry->offset == d_rounding);
    }
  else
    push_regs (&live_regs_mask, current_function_interrupt);

  if (flag_pic && regs_ever_live[PIC_OFFSET_TABLE_REGNUM])
    {
      rtx insn = get_last_insn ();
      rtx last = emit_insn (gen_GOTaddr2picreg ());

      /* Mark these insns as possibly dead.  Sometimes, flow2 may
	 delete all uses of the PIC register.  In this case, let it
	 delete the initialization too.  */
      do
	{
	  insn = NEXT_INSN (insn);

	  REG_NOTES (insn) = gen_rtx_EXPR_LIST (REG_MAYBE_DEAD,
						const0_rtx,
						REG_NOTES (insn));
	}
      while (insn != last);
    }

  if (SHMEDIA_REGS_STACK_ADJUST ())
    {
      /* This must NOT go through the PLT, otherwise mach and macl
	 may be clobbered.  */
      function_symbol (gen_rtx_REG (Pmode, R0_REG),
		       (TARGET_FPU_ANY
			? "__GCC_push_shmedia_regs"
			: "__GCC_push_shmedia_regs_nofpu"), SFUNC_GOT);
      emit_insn (gen_shmedia_save_restore_regs_compact
		 (GEN_INT (-SHMEDIA_REGS_STACK_ADJUST ())));
    }

  if (target_flags != save_flags && ! current_function_interrupt)
    {
      rtx insn = emit_insn (gen_toggle_sz ());

      /* If we're lucky, a mode switch in the function body will
	 overwrite fpscr, turning this insn dead.  Tell flow this
	 insn is ok to delete.  */
      REG_NOTES (insn) = gen_rtx_EXPR_LIST (REG_MAYBE_DEAD,
					    const0_rtx,
					    REG_NOTES (insn));
    }

  target_flags = save_flags;

  output_stack_adjust (-rounded_frame_size (d) + d_rounding,
		       stack_pointer_rtx, 0, NULL);

  if (frame_pointer_needed)
    frame_insn (GEN_MOV (hard_frame_pointer_rtx, stack_pointer_rtx));

  if (TARGET_SHCOMPACT
      && (current_function_args_info.call_cookie & ~ CALL_COOKIE_RET_TRAMP(1)))
    {
      /* This must NOT go through the PLT, otherwise mach and macl
	 may be clobbered.  */
      function_symbol (gen_rtx_REG (Pmode, R0_REG),
		      "__GCC_shcompact_incoming_args", SFUNC_GOT);
      emit_insn (gen_shcompact_incoming_args ());
    }
}

void
sh_expand_epilogue (bool sibcall_p)
{
  HARD_REG_SET live_regs_mask;
  int d, i;
  int d_rounding = 0;

  int save_flags = target_flags;
  int frame_size, save_size;
  int fpscr_deferred = 0;
  int e = sibcall_p ? -1 : 1;

  d = calc_live_regs (&live_regs_mask);

  save_size = d;
  frame_size = rounded_frame_size (d);

  if (TARGET_SH5)
    {
      int tregs_space = shmedia_target_regs_stack_adjust (&live_regs_mask);
      int total_size;
      if (d % (STACK_BOUNDARY / BITS_PER_UNIT))
      d_rounding = ((STACK_BOUNDARY / BITS_PER_UNIT)
		    - d % (STACK_BOUNDARY / BITS_PER_UNIT));

      total_size = d + tregs_space;
      total_size += rounded_frame_size (total_size);
      save_size = total_size - frame_size;

      /* If adjusting the stack in a single step costs nothing extra, do so.
	 I.e. either if a single addi is enough, or we need a movi anyway,
	 and we don't exceed the maximum offset range (the test for the
	 latter is conservative for simplicity).  */
      if (TARGET_SHMEDIA
	  && ! frame_pointer_needed
	  && (CONST_OK_FOR_I10 (total_size)
	      || (! CONST_OK_FOR_I10 (save_size + d_rounding)
		  && total_size <= 2044)))
	d_rounding = frame_size;

      frame_size -= d_rounding;
    }

  if (frame_pointer_needed)
    {
      /* We must avoid scheduling the epilogue with previous basic blocks
	 when exception handling is enabled.  See PR/18032.  */
      if (flag_exceptions)
	emit_insn (gen_blockage ());
      output_stack_adjust (frame_size, hard_frame_pointer_rtx, e,
			   &live_regs_mask);

      /* We must avoid moving the stack pointer adjustment past code
	 which reads from the local frame, else an interrupt could
	 occur after the SP adjustment and clobber data in the local
	 frame.  */
      emit_insn (gen_blockage ());
      emit_insn (GEN_MOV (stack_pointer_rtx, hard_frame_pointer_rtx));
    }
  else if (frame_size)
    {
      /* We must avoid moving the stack pointer adjustment past code
	 which reads from the local frame, else an interrupt could
	 occur after the SP adjustment and clobber data in the local
	 frame.  */
      emit_insn (gen_blockage ());
      output_stack_adjust (frame_size, stack_pointer_rtx, e, &live_regs_mask);
    }

  if (SHMEDIA_REGS_STACK_ADJUST ())
    {
      function_symbol (gen_rtx_REG (Pmode, R0_REG),
		       (TARGET_FPU_ANY
			? "__GCC_pop_shmedia_regs"
			: "__GCC_pop_shmedia_regs_nofpu"), SFUNC_GOT);
      /* This must NOT go through the PLT, otherwise mach and macl
	 may be clobbered.  */
      emit_insn (gen_shmedia_save_restore_regs_compact
		 (GEN_INT (SHMEDIA_REGS_STACK_ADJUST ())));
    }

  /* Pop all the registers.  */

  if (target_flags != save_flags && ! current_function_interrupt)
    emit_insn (gen_toggle_sz ());
  if (TARGET_SH5)
    {
      int offset_base, offset;
      int offset_in_r0 = -1;
      int sp_in_r0 = 0;
      rtx r0 = gen_rtx_REG (Pmode, R0_REG);
      save_schedule schedule;
      save_entry *entry;
      int *tmp_pnt;

      entry = sh5_schedule_saves (&live_regs_mask, &schedule, d_rounding);
      offset_base = -entry[1].offset + d_rounding;
      tmp_pnt = schedule.temps;
      for (; entry->mode != VOIDmode; entry--)
	{
	  enum machine_mode mode = entry->mode;
	  int reg = entry->reg;
	  rtx reg_rtx, mem_rtx, post_inc = NULL_RTX, insn;

	  offset = offset_base + entry->offset;
	  reg_rtx = gen_rtx_REG (mode, reg);

	  mem_rtx = gen_frame_mem (mode,
				   gen_rtx_PLUS (Pmode,
						 stack_pointer_rtx,
						 GEN_INT (offset)));

	  GO_IF_LEGITIMATE_ADDRESS (mode, XEXP (mem_rtx, 0), try_post_inc);

	  mem_rtx = NULL_RTX;

	try_post_inc:
	  do
	    if (HAVE_POST_INCREMENT
		&& (offset == offset_in_r0
		    || (offset + GET_MODE_SIZE (mode) != d + d_rounding
			&& mem_rtx == NULL_RTX)
		    || reg == PR_REG || SPECIAL_REGISTER_P (reg)))
	      {
		post_inc = gen_frame_mem (mode, gen_rtx_POST_INC (Pmode, r0));

		GO_IF_LEGITIMATE_ADDRESS (mode, XEXP (post_inc, 0),
					  post_inc_ok);

		post_inc = NULL_RTX;

		break;

	      post_inc_ok:
		mem_rtx = NULL_RTX;
	      }
	  while (0);

	  if (mem_rtx != NULL_RTX)
	    goto addr_ok;

	  if (offset_in_r0 == -1)
	    {
	      emit_move_insn (r0, GEN_INT (offset));
	      offset_in_r0 = offset;
	    }
	  else if (offset != offset_in_r0)
	    {
	      emit_move_insn (r0,
			      gen_rtx_PLUS
			      (Pmode, r0,
			       GEN_INT (offset - offset_in_r0)));
	      offset_in_r0 += offset - offset_in_r0;
	    }

	  if (post_inc != NULL_RTX)
	    {
	      if (! sp_in_r0)
		{
		  emit_move_insn (r0,
				  gen_rtx_PLUS
				  (Pmode, r0, stack_pointer_rtx));
		  sp_in_r0 = 1;
		}

	      mem_rtx = post_inc;

	      offset_in_r0 += GET_MODE_SIZE (mode);
	    }
	  else if (sp_in_r0)
	    mem_rtx = gen_frame_mem (mode, r0);
	  else
	    mem_rtx = gen_frame_mem (mode,
				     gen_rtx_PLUS (Pmode,
						   stack_pointer_rtx,
						   r0));

	  gcc_assert ((reg != PR_REG && !SPECIAL_REGISTER_P (reg))
		      || mem_rtx == post_inc);

	addr_ok:
	  if ((reg == PR_REG || SPECIAL_REGISTER_P (reg))
	      && mem_rtx != post_inc)
	    {
	      insn = emit_move_insn (r0, mem_rtx);
	      mem_rtx = r0;
	    }
	  else if (TARGET_REGISTER_P (reg))
	    {
	      rtx tmp_reg = gen_rtx_REG (mode, *tmp_pnt);

	      /* Give the scheduler a bit of freedom by using up to
		 MAX_TEMPS registers in a round-robin fashion.  */
	      insn = emit_move_insn (tmp_reg, mem_rtx);
	      mem_rtx = tmp_reg;
	      if (*++tmp_pnt < 0)
		tmp_pnt = schedule.temps;
	    }

	  insn = emit_move_insn (reg_rtx, mem_rtx);
	  if (reg == PR_MEDIA_REG && sh_media_register_for_return () >= 0)
	    /* This is dead, unless we return with a sibcall.  */
	    REG_NOTES (insn) = gen_rtx_EXPR_LIST (REG_MAYBE_DEAD,
						  const0_rtx,
						  REG_NOTES (insn));
	}

      gcc_assert (entry->offset + offset_base == d + d_rounding);
    }
  else /* ! TARGET_SH5 */
    {
      save_size = 0;
      if (TEST_HARD_REG_BIT (live_regs_mask, PR_REG))
	pop (PR_REG);
      for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
	{
	  int j = (FIRST_PSEUDO_REGISTER - 1) - i;

	  if (j == FPSCR_REG && current_function_interrupt && TARGET_FMOVD
	      && hard_regs_intersect_p (&live_regs_mask,
					&reg_class_contents[DF_REGS]))
	    fpscr_deferred = 1;
	  else if (j != PR_REG && TEST_HARD_REG_BIT (live_regs_mask, j))
	    pop (j);
	  if (j == FIRST_FP_REG && fpscr_deferred)
	    pop (FPSCR_REG);

	}
    }
  if (target_flags != save_flags && ! current_function_interrupt)
    emit_insn (gen_toggle_sz ());
  target_flags = save_flags;

  output_stack_adjust (current_function_pretend_args_size
		       + save_size + d_rounding
		       + current_function_args_info.stack_regs * 8,
		       stack_pointer_rtx, e, NULL);

  if (current_function_calls_eh_return)
    emit_insn (GEN_ADD3 (stack_pointer_rtx, stack_pointer_rtx,
			 EH_RETURN_STACKADJ_RTX));

  /* Switch back to the normal stack if necessary.  */
  if (lookup_attribute ("sp_switch", DECL_ATTRIBUTES (current_function_decl)))
    emit_insn (gen_sp_switch_2 ());

  /* Tell flow the insn that pops PR isn't dead.  */
  /* PR_REG will never be live in SHmedia mode, and we don't need to
     USE PR_MEDIA_REG, since it will be explicitly copied to TR0_REG
     by the return pattern.  */
  if (TEST_HARD_REG_BIT (live_regs_mask, PR_REG))
    emit_insn (gen_rtx_USE (VOIDmode, gen_rtx_REG (SImode, PR_REG)));
}

static int sh_need_epilogue_known = 0;

int
sh_need_epilogue (void)
{
  if (! sh_need_epilogue_known)
    {
      rtx epilogue;

      start_sequence ();
      sh_expand_epilogue (0);
      epilogue = get_insns ();
      end_sequence ();
      sh_need_epilogue_known = (epilogue == NULL ? -1 : 1);
    }
  return sh_need_epilogue_known > 0;
}

/* Emit code to change the current function's return address to RA.
   TEMP is available as a scratch register, if needed.  */

void
sh_set_return_address (rtx ra, rtx tmp)
{
  HARD_REG_SET live_regs_mask;
  int d;
  int pr_reg = TARGET_SHMEDIA ? PR_MEDIA_REG : PR_REG;
  int pr_offset;

  d = calc_live_regs (&live_regs_mask);

  /* If pr_reg isn't life, we can set it (or the register given in
     sh_media_register_for_return) directly.  */
  if (! TEST_HARD_REG_BIT (live_regs_mask, pr_reg))
    {
      rtx rr;

      if (TARGET_SHMEDIA)
	{
	  int rr_regno = sh_media_register_for_return ();

	  if (rr_regno < 0)
	    rr_regno = pr_reg;

	  rr = gen_rtx_REG (DImode, rr_regno);
	}
      else
	rr = gen_rtx_REG (SImode, pr_reg);

      emit_insn (GEN_MOV (rr, ra));
      /* Tell flow the register for return isn't dead.  */
      emit_insn (gen_rtx_USE (VOIDmode, rr));
      return;
    }

  if (TARGET_SH5)
    {
      int offset;
      save_schedule schedule;
      save_entry *entry;

      entry = sh5_schedule_saves (&live_regs_mask, &schedule, 0);
      offset = entry[1].offset;
      for (; entry->mode != VOIDmode; entry--)
	if (entry->reg == pr_reg)
	  goto found;

      /* We can't find pr register.  */
      gcc_unreachable ();

    found:
      offset = entry->offset - offset;
      pr_offset = (rounded_frame_size (d) + offset
		   + SHMEDIA_REGS_STACK_ADJUST ());
    }
  else
    pr_offset = rounded_frame_size (d);

  emit_insn (GEN_MOV (tmp, GEN_INT (pr_offset)));
  emit_insn (GEN_ADD3 (tmp, tmp, hard_frame_pointer_rtx));

  tmp = gen_frame_mem (Pmode, tmp);
  emit_insn (GEN_MOV (tmp, ra));
}

/* Clear variables at function end.  */

static void
sh_output_function_epilogue (FILE *file ATTRIBUTE_UNUSED,
			     HOST_WIDE_INT size ATTRIBUTE_UNUSED)
{
  sh_need_epilogue_known = 0;
}

static rtx
sh_builtin_saveregs (void)
{
  /* First unnamed integer register.  */
  int first_intreg = current_function_args_info.arg_count[(int) SH_ARG_INT];
  /* Number of integer registers we need to save.  */
  int n_intregs = MAX (0, NPARM_REGS (SImode) - first_intreg);
  /* First unnamed SFmode float reg */
  int first_floatreg = current_function_args_info.arg_count[(int) SH_ARG_FLOAT];
  /* Number of SFmode float regs to save.  */
  int n_floatregs = MAX (0, NPARM_REGS (SFmode) - first_floatreg);
  rtx regbuf, fpregs;
  int bufsize, regno;
  HOST_WIDE_INT alias_set;

  if (TARGET_SH5)
    {
      if (n_intregs)
	{
	  int pushregs = n_intregs;

	  while (pushregs < NPARM_REGS (SImode) - 1
		 && (CALL_COOKIE_INT_REG_GET
			(current_function_args_info.call_cookie,
			 NPARM_REGS (SImode) - pushregs)
		     == 1))
	    {
	      current_function_args_info.call_cookie
		&= ~ CALL_COOKIE_INT_REG (NPARM_REGS (SImode)
					  - pushregs, 1);
	      pushregs++;
	    }

	  if (pushregs == NPARM_REGS (SImode))
	    current_function_args_info.call_cookie
	      |= (CALL_COOKIE_INT_REG (0, 1)
		  | CALL_COOKIE_STACKSEQ (pushregs - 1));
	  else
	    current_function_args_info.call_cookie
	      |= CALL_COOKIE_STACKSEQ (pushregs);

	  current_function_pretend_args_size += 8 * n_intregs;
	}
      if (TARGET_SHCOMPACT)
	return const0_rtx;
    }

  if (! TARGET_SH2E && ! TARGET_SH4 && ! TARGET_SH5)
    {
      error ("__builtin_saveregs not supported by this subtarget");
      return const0_rtx;
    }

  if (TARGET_SHMEDIA)
    n_floatregs = 0;

  /* Allocate block of memory for the regs.  */
  /* ??? If n_intregs + n_floatregs == 0, should we allocate at least 1 byte?
     Or can assign_stack_local accept a 0 SIZE argument?  */
  bufsize = (n_intregs * UNITS_PER_WORD) + (n_floatregs * UNITS_PER_WORD);

  if (TARGET_SHMEDIA)
    regbuf = gen_frame_mem (BLKmode, gen_rtx_REG (Pmode, ARG_POINTER_REGNUM));
  else if (n_floatregs & 1)
    {
      rtx addr;

      regbuf = assign_stack_local (BLKmode, bufsize + UNITS_PER_WORD, 0);
      addr = copy_to_mode_reg (Pmode, XEXP (regbuf, 0));
      emit_insn (gen_iorsi3 (addr, addr, GEN_INT (UNITS_PER_WORD)));
      regbuf = change_address (regbuf, BLKmode, addr);
    }
  else if (STACK_BOUNDARY < 64 && TARGET_FPU_DOUBLE && n_floatregs)
    {
      rtx addr, mask;

      regbuf = assign_stack_local (BLKmode, bufsize + UNITS_PER_WORD, 0);
      addr = copy_to_mode_reg (Pmode, plus_constant (XEXP (regbuf, 0), 4));
      mask = copy_to_mode_reg (Pmode, GEN_INT (-8));
      emit_insn (gen_andsi3 (addr, addr, mask));
      regbuf = change_address (regbuf, BLKmode, addr);
    }
  else
    regbuf = assign_stack_local (BLKmode, bufsize, TARGET_FPU_DOUBLE ? 64 : 0);
  alias_set = get_varargs_alias_set ();
  set_mem_alias_set (regbuf, alias_set);

  /* Save int args.
     This is optimized to only save the regs that are necessary.  Explicitly
     named args need not be saved.  */
  if (n_intregs > 0)
    move_block_from_reg (BASE_ARG_REG (SImode) + first_intreg,
			 adjust_address (regbuf, BLKmode,
					 n_floatregs * UNITS_PER_WORD),
			 n_intregs);

  if (TARGET_SHMEDIA)
    /* Return the address of the regbuf.  */
    return XEXP (regbuf, 0);

  /* Save float args.
     This is optimized to only save the regs that are necessary.  Explicitly
     named args need not be saved.
     We explicitly build a pointer to the buffer because it halves the insn
     count when not optimizing (otherwise the pointer is built for each reg
     saved).
     We emit the moves in reverse order so that we can use predecrement.  */

  fpregs = copy_to_mode_reg (Pmode,
			     plus_constant (XEXP (regbuf, 0),
                                            n_floatregs * UNITS_PER_WORD));
  if (TARGET_SH4 || TARGET_SH2A_DOUBLE)
    {
      rtx mem;
      for (regno = NPARM_REGS (DFmode) - 2; regno >= first_floatreg; regno -= 2)
	{
	  emit_insn (gen_addsi3 (fpregs, fpregs,
				 GEN_INT (-2 * UNITS_PER_WORD)));
	  mem = change_address (regbuf, DFmode, fpregs);
	  emit_move_insn (mem,
			  gen_rtx_REG (DFmode, BASE_ARG_REG (DFmode) + regno));
	}
      regno = first_floatreg;
      if (regno & 1)
	{
	  emit_insn (gen_addsi3 (fpregs, fpregs, GEN_INT (-UNITS_PER_WORD)));
	  mem = change_address (regbuf, SFmode, fpregs);
	  emit_move_insn (mem,
			  gen_rtx_REG (SFmode, BASE_ARG_REG (SFmode) + regno
						- (TARGET_LITTLE_ENDIAN != 0)));
	}
    }
  else
    for (regno = NPARM_REGS (SFmode) - 1; regno >= first_floatreg; regno--)
      {
        rtx mem;

	emit_insn (gen_addsi3 (fpregs, fpregs, GEN_INT (-UNITS_PER_WORD)));
	mem = change_address (regbuf, SFmode, fpregs);
	emit_move_insn (mem,
			gen_rtx_REG (SFmode, BASE_ARG_REG (SFmode) + regno));
      }

  /* Return the address of the regbuf.  */
  return XEXP (regbuf, 0);
}

/* Define the `__builtin_va_list' type for the ABI.  */

static tree
sh_build_builtin_va_list (void)
{
  tree f_next_o, f_next_o_limit, f_next_fp, f_next_fp_limit, f_next_stack;
  tree record;

  if (TARGET_SH5 || (! TARGET_SH2E && ! TARGET_SH4)
      || TARGET_HITACHI || sh_cfun_attr_renesas_p ())
    return ptr_type_node;

  record = (*lang_hooks.types.make_type) (RECORD_TYPE);

  f_next_o = build_decl (FIELD_DECL, get_identifier ("__va_next_o"),
			 ptr_type_node);
  f_next_o_limit = build_decl (FIELD_DECL,
			       get_identifier ("__va_next_o_limit"),
			       ptr_type_node);
  f_next_fp = build_decl (FIELD_DECL, get_identifier ("__va_next_fp"),
			  ptr_type_node);
  f_next_fp_limit = build_decl (FIELD_DECL,
				get_identifier ("__va_next_fp_limit"),
				ptr_type_node);
  f_next_stack = build_decl (FIELD_DECL, get_identifier ("__va_next_stack"),
			     ptr_type_node);

  DECL_FIELD_CONTEXT (f_next_o) = record;
  DECL_FIELD_CONTEXT (f_next_o_limit) = record;
  DECL_FIELD_CONTEXT (f_next_fp) = record;
  DECL_FIELD_CONTEXT (f_next_fp_limit) = record;
  DECL_FIELD_CONTEXT (f_next_stack) = record;

  TYPE_FIELDS (record) = f_next_o;
  TREE_CHAIN (f_next_o) = f_next_o_limit;
  TREE_CHAIN (f_next_o_limit) = f_next_fp;
  TREE_CHAIN (f_next_fp) = f_next_fp_limit;
  TREE_CHAIN (f_next_fp_limit) = f_next_stack;

  layout_type (record);

  return record;
}

/* Implement `va_start' for varargs and stdarg.  */

void
sh_va_start (tree valist, rtx nextarg)
{
  tree f_next_o, f_next_o_limit, f_next_fp, f_next_fp_limit, f_next_stack;
  tree next_o, next_o_limit, next_fp, next_fp_limit, next_stack;
  tree t, u;
  int nfp, nint;

  if (TARGET_SH5)
    {
      expand_builtin_saveregs ();
      std_expand_builtin_va_start (valist, nextarg);
      return;
    }

  if ((! TARGET_SH2E && ! TARGET_SH4)
      || TARGET_HITACHI || sh_cfun_attr_renesas_p ())
    {
      std_expand_builtin_va_start (valist, nextarg);
      return;
    }

  f_next_o = TYPE_FIELDS (va_list_type_node);
  f_next_o_limit = TREE_CHAIN (f_next_o);
  f_next_fp = TREE_CHAIN (f_next_o_limit);
  f_next_fp_limit = TREE_CHAIN (f_next_fp);
  f_next_stack = TREE_CHAIN (f_next_fp_limit);

  next_o = build3 (COMPONENT_REF, TREE_TYPE (f_next_o), valist, f_next_o,
		   NULL_TREE);
  next_o_limit = build3 (COMPONENT_REF, TREE_TYPE (f_next_o_limit),
			 valist, f_next_o_limit, NULL_TREE);
  next_fp = build3 (COMPONENT_REF, TREE_TYPE (f_next_fp), valist, f_next_fp,
		    NULL_TREE);
  next_fp_limit = build3 (COMPONENT_REF, TREE_TYPE (f_next_fp_limit),
			  valist, f_next_fp_limit, NULL_TREE);
  next_stack = build3 (COMPONENT_REF, TREE_TYPE (f_next_stack),
		       valist, f_next_stack, NULL_TREE);

  /* Call __builtin_saveregs.  */
  u = make_tree (ptr_type_node, expand_builtin_saveregs ());
  t = build2 (MODIFY_EXPR, ptr_type_node, next_fp, u);
  TREE_SIDE_EFFECTS (t) = 1;
  expand_expr (t, const0_rtx, VOIDmode, EXPAND_NORMAL);

  nfp = current_function_args_info.arg_count[SH_ARG_FLOAT];
  if (nfp < 8)
    nfp = 8 - nfp;
  else
    nfp = 0;
  u = fold_build2 (PLUS_EXPR, ptr_type_node, u,
		   build_int_cst (NULL_TREE, UNITS_PER_WORD * nfp));
  t = build2 (MODIFY_EXPR, ptr_type_node, next_fp_limit, u);
  TREE_SIDE_EFFECTS (t) = 1;
  expand_expr (t, const0_rtx, VOIDmode, EXPAND_NORMAL);

  t = build2 (MODIFY_EXPR, ptr_type_node, next_o, u);
  TREE_SIDE_EFFECTS (t) = 1;
  expand_expr (t, const0_rtx, VOIDmode, EXPAND_NORMAL);

  nint = current_function_args_info.arg_count[SH_ARG_INT];
  if (nint < 4)
    nint = 4 - nint;
  else
    nint = 0;
  u = fold_build2 (PLUS_EXPR, ptr_type_node, u,
		   build_int_cst (NULL_TREE, UNITS_PER_WORD * nint));
  t = build2 (MODIFY_EXPR, ptr_type_node, next_o_limit, u);
  TREE_SIDE_EFFECTS (t) = 1;
  expand_expr (t, const0_rtx, VOIDmode, EXPAND_NORMAL);

  u = make_tree (ptr_type_node, nextarg);
  t = build2 (MODIFY_EXPR, ptr_type_node, next_stack, u);
  TREE_SIDE_EFFECTS (t) = 1;
  expand_expr (t, const0_rtx, VOIDmode, EXPAND_NORMAL);
}

/* TYPE is a RECORD_TYPE.  If there is only a single nonzero-sized
   member, return it.  */
static tree
find_sole_member (tree type)
{
  tree field, member = NULL_TREE;

  for (field = TYPE_FIELDS (type); field; field = TREE_CHAIN (field))
    {
      if (TREE_CODE (field) != FIELD_DECL)
	continue;
      if (!DECL_SIZE (field))
	return NULL_TREE;
      if (integer_zerop (DECL_SIZE (field)))
	continue;
      if (member)
	return NULL_TREE;
      member = field;
    }
  return member;
}
/* Implement `va_arg'.  */

static tree
sh_gimplify_va_arg_expr (tree valist, tree type, tree *pre_p,
			 tree *post_p ATTRIBUTE_UNUSED)
{
  HOST_WIDE_INT size, rsize;
  tree tmp, pptr_type_node;
  tree addr, lab_over = NULL, result = NULL;
  int pass_by_ref = targetm.calls.must_pass_in_stack (TYPE_MODE (type), type);
  tree eff_type;

  if (pass_by_ref)
    type = build_pointer_type (type);

  size = int_size_in_bytes (type);
  rsize = (size + UNITS_PER_WORD - 1) & -UNITS_PER_WORD;
  pptr_type_node = build_pointer_type (ptr_type_node);

  if (! TARGET_SH5 && (TARGET_SH2E || TARGET_SH4)
      && ! (TARGET_HITACHI || sh_cfun_attr_renesas_p ()))
    {
      tree f_next_o, f_next_o_limit, f_next_fp, f_next_fp_limit, f_next_stack;
      tree next_o, next_o_limit, next_fp, next_fp_limit, next_stack;
      int pass_as_float;
      tree lab_false;
      tree member;

      f_next_o = TYPE_FIELDS (va_list_type_node);
      f_next_o_limit = TREE_CHAIN (f_next_o);
      f_next_fp = TREE_CHAIN (f_next_o_limit);
      f_next_fp_limit = TREE_CHAIN (f_next_fp);
      f_next_stack = TREE_CHAIN (f_next_fp_limit);

      next_o = build3 (COMPONENT_REF, TREE_TYPE (f_next_o), valist, f_next_o,
		       NULL_TREE);
      next_o_limit = build3 (COMPONENT_REF, TREE_TYPE (f_next_o_limit),
			     valist, f_next_o_limit, NULL_TREE);
      next_fp = build3 (COMPONENT_REF, TREE_TYPE (f_next_fp),
		        valist, f_next_fp, NULL_TREE);
      next_fp_limit = build3 (COMPONENT_REF, TREE_TYPE (f_next_fp_limit),
			      valist, f_next_fp_limit, NULL_TREE);
      next_stack = build3 (COMPONENT_REF, TREE_TYPE (f_next_stack),
			   valist, f_next_stack, NULL_TREE);

      /* Structures with a single member with a distinct mode are passed
	 like their member.  This is relevant if the latter has a REAL_TYPE
	 or COMPLEX_TYPE type.  */
      eff_type = type;
      while (TREE_CODE (eff_type) == RECORD_TYPE
	     && (member = find_sole_member (eff_type))
	     && (TREE_CODE (TREE_TYPE (member)) == REAL_TYPE
		 || TREE_CODE (TREE_TYPE (member)) == COMPLEX_TYPE
		 || TREE_CODE (TREE_TYPE (member)) == RECORD_TYPE))
	{
	  tree field_type = TREE_TYPE (member);

	  if (TYPE_MODE (eff_type) == TYPE_MODE (field_type))
	    eff_type = field_type;
	  else
	    {
	      gcc_assert ((TYPE_ALIGN (eff_type)
			   < GET_MODE_ALIGNMENT (TYPE_MODE (field_type)))
			  || (TYPE_ALIGN (eff_type)
			      > GET_MODE_BITSIZE (TYPE_MODE (field_type))));
	      break;
	    }
	}

      if (TARGET_SH4)
	{
	  pass_as_float = ((TREE_CODE (eff_type) == REAL_TYPE && size <= 8)
			   || (TREE_CODE (eff_type) == COMPLEX_TYPE
			       && TREE_CODE (TREE_TYPE (eff_type)) == REAL_TYPE
			       && size <= 16));
	}
      else
	{
	  pass_as_float = (TREE_CODE (eff_type) == REAL_TYPE && size == 4);
	}

      addr = create_tmp_var (pptr_type_node, NULL);
      lab_false = create_artificial_label ();
      lab_over = create_artificial_label ();

      valist = build1 (INDIRECT_REF, ptr_type_node, addr);

      if (pass_as_float)
	{
	  tree next_fp_tmp = create_tmp_var (TREE_TYPE (f_next_fp), NULL);
	  tree cmp;
	  bool is_double = size == 8 && TREE_CODE (eff_type) == REAL_TYPE;

	  tmp = build1 (ADDR_EXPR, pptr_type_node, next_fp);
	  tmp = build2 (MODIFY_EXPR, void_type_node, addr, tmp);
	  gimplify_and_add (tmp, pre_p);

	  tmp = build2 (MODIFY_EXPR, ptr_type_node, next_fp_tmp, valist);
	  gimplify_and_add (tmp, pre_p);
	  tmp = next_fp_limit;
	  if (size > 4 && !is_double)
	    tmp = build2 (PLUS_EXPR, TREE_TYPE (tmp), tmp,
			  fold_convert (TREE_TYPE (tmp), size_int (4 - size)));
	  tmp = build2 (GE_EXPR, boolean_type_node, next_fp_tmp, tmp);
	  cmp = build3 (COND_EXPR, void_type_node, tmp,
		        build1 (GOTO_EXPR, void_type_node, lab_false),
		        NULL_TREE);
	  if (!is_double)
	    gimplify_and_add (cmp, pre_p);

	  if (TYPE_ALIGN (eff_type) > BITS_PER_WORD
	      || (is_double || size == 16))
	    {
	      tmp = fold_convert (ptr_type_node, size_int (UNITS_PER_WORD));
	      tmp = build2 (BIT_AND_EXPR, ptr_type_node, next_fp_tmp, tmp);
	      tmp = build2 (PLUS_EXPR, ptr_type_node, next_fp_tmp, tmp);
	      tmp = build2 (MODIFY_EXPR, ptr_type_node, next_fp_tmp, tmp);
	      gimplify_and_add (tmp, pre_p);
	    }
	  if (is_double)
	    gimplify_and_add (cmp, pre_p);

#ifdef FUNCTION_ARG_SCmode_WART
	  if (TYPE_MODE (eff_type) == SCmode
	      && TARGET_SH4 && TARGET_LITTLE_ENDIAN)
	    {
	      tree subtype = TREE_TYPE (eff_type);
	      tree real, imag;

	      imag
		= std_gimplify_va_arg_expr (next_fp_tmp, subtype, pre_p, NULL);
	      imag = get_initialized_tmp_var (imag, pre_p, NULL);

	      real
		= std_gimplify_va_arg_expr (next_fp_tmp, subtype, pre_p, NULL);
	      real = get_initialized_tmp_var (real, pre_p, NULL);

	      result = build2 (COMPLEX_EXPR, type, real, imag);
	      result = get_initialized_tmp_var (result, pre_p, NULL);
	    }
#endif /* FUNCTION_ARG_SCmode_WART */

	  tmp = build1 (GOTO_EXPR, void_type_node, lab_over);
	  gimplify_and_add (tmp, pre_p);

	  tmp = build1 (LABEL_EXPR, void_type_node, lab_false);
	  gimplify_and_add (tmp, pre_p);

	  tmp = build1 (ADDR_EXPR, pptr_type_node, next_stack);
	  tmp = build2 (MODIFY_EXPR, void_type_node, addr, tmp);
	  gimplify_and_add (tmp, pre_p);
	  tmp = build2 (MODIFY_EXPR, ptr_type_node, next_fp_tmp, valist);
	  gimplify_and_add (tmp, pre_p);

	  tmp = build2 (MODIFY_EXPR, ptr_type_node, valist, next_fp_tmp);
	  gimplify_and_add (tmp, post_p);
	  valist = next_fp_tmp;
	}
      else
	{
	  tmp = fold_convert (ptr_type_node, size_int (rsize));
	  tmp = build2 (PLUS_EXPR, ptr_type_node, next_o, tmp);
	  tmp = build2 (GT_EXPR, boolean_type_node, tmp, next_o_limit);
	  tmp = build3 (COND_EXPR, void_type_node, tmp,
		        build1 (GOTO_EXPR, void_type_node, lab_false),
		        NULL_TREE);
	  gimplify_and_add (tmp, pre_p);

	  tmp = build1 (ADDR_EXPR, pptr_type_node, next_o);
	  tmp = build2 (MODIFY_EXPR, void_type_node, addr, tmp);
	  gimplify_and_add (tmp, pre_p);

	  tmp = build1 (GOTO_EXPR, void_type_node, lab_over);
	  gimplify_and_add (tmp, pre_p);

	  tmp = build1 (LABEL_EXPR, void_type_node, lab_false);
	  gimplify_and_add (tmp, pre_p);

	  if (size > 4 && ! TARGET_SH4)
	    {
	      tmp = build2 (MODIFY_EXPR, ptr_type_node, next_o, next_o_limit);
	      gimplify_and_add (tmp, pre_p);
	    }

	  tmp = build1 (ADDR_EXPR, pptr_type_node, next_stack);
	  tmp = build2 (MODIFY_EXPR, void_type_node, addr, tmp);
	  gimplify_and_add (tmp, pre_p);
	}

      if (!result)
	{
	  tmp = build1 (LABEL_EXPR, void_type_node, lab_over);
	  gimplify_and_add (tmp, pre_p);
	}
    }

  /* ??? In va-sh.h, there had been code to make values larger than
     size 8 indirect.  This does not match the FUNCTION_ARG macros.  */

  tmp = std_gimplify_va_arg_expr (valist, type, pre_p, NULL);
  if (result)
    {
      tmp = build2 (MODIFY_EXPR, void_type_node, result, tmp);
      gimplify_and_add (tmp, pre_p);

      tmp = build1 (LABEL_EXPR, void_type_node, lab_over);
      gimplify_and_add (tmp, pre_p);
    }
  else
    result = tmp;

  if (pass_by_ref)
    result = build_va_arg_indirect_ref (result);

  return result;
}

bool
sh_promote_prototypes (tree type)
{
  if (TARGET_HITACHI)
    return 0;
  if (! type)
    return 1;
  return ! sh_attr_renesas_p (type);
}

/* Whether an argument must be passed by reference.  On SHcompact, we
   pretend arguments wider than 32-bits that would have been passed in
   registers are passed by reference, so that an SHmedia trampoline
   loads them into the full 64-bits registers.  */

static int
shcompact_byref (CUMULATIVE_ARGS *cum, enum machine_mode mode,
		 tree type, bool named)
{
  unsigned HOST_WIDE_INT size;

  if (type)
    size = int_size_in_bytes (type);
  else
    size = GET_MODE_SIZE (mode);

  if (cum->arg_count[SH_ARG_INT] < NPARM_REGS (SImode)
      && (!named
	  || GET_SH_ARG_CLASS (mode) == SH_ARG_INT
	  || (GET_SH_ARG_CLASS (mode) == SH_ARG_FLOAT
	      && cum->arg_count[SH_ARG_FLOAT] >= NPARM_REGS (SFmode)))
      && size > 4
      && !SHCOMPACT_FORCE_ON_STACK (mode, type)
      && !SH5_WOULD_BE_PARTIAL_NREGS (*cum, mode, type, named))
    return size;
  else
    return 0;
}

static bool
sh_pass_by_reference (CUMULATIVE_ARGS *cum, enum machine_mode mode,
		      tree type, bool named)
{
  if (targetm.calls.must_pass_in_stack (mode, type))
    return true;

  /* ??? std_gimplify_va_arg_expr passes NULL for cum.  That function
     wants to know about pass-by-reference semantics for incoming
     arguments.  */
  if (! cum)
    return false;

  if (TARGET_SHCOMPACT)
    {
      cum->byref = shcompact_byref (cum, mode, type, named);
      return cum->byref != 0;
    }

  return false;
}

static bool
sh_callee_copies (CUMULATIVE_ARGS *cum, enum machine_mode mode,
		  tree type, bool named ATTRIBUTE_UNUSED)
{
  /* ??? How can it possibly be correct to return true only on the
     caller side of the equation?  Is there someplace else in the
     sh backend that's magically producing the copies?  */
  return (cum->outgoing
	  && ((mode == BLKmode ? TYPE_ALIGN (type) : GET_MODE_ALIGNMENT (mode))
	      % SH_MIN_ALIGN_FOR_CALLEE_COPY == 0));
}

static int
sh_arg_partial_bytes (CUMULATIVE_ARGS *cum, enum machine_mode mode,
		      tree type, bool named ATTRIBUTE_UNUSED)
{
  int words = 0;

  if (!TARGET_SH5
      && PASS_IN_REG_P (*cum, mode, type)
      && !(TARGET_SH4 || TARGET_SH2A_DOUBLE)
      && (ROUND_REG (*cum, mode)
	  + (mode != BLKmode
	     ? ROUND_ADVANCE (GET_MODE_SIZE (mode))
	     : ROUND_ADVANCE (int_size_in_bytes (type)))
	  > NPARM_REGS (mode)))
    words = NPARM_REGS (mode) - ROUND_REG (*cum, mode);

  else if (!TARGET_SHCOMPACT
	   && SH5_WOULD_BE_PARTIAL_NREGS (*cum, mode, type, named))
    words = NPARM_REGS (SImode) - cum->arg_count[SH_ARG_INT];

  return words * UNITS_PER_WORD;
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

   On SH the first args are normally in registers
   and the rest are pushed.  Any arg that starts within the first
   NPARM_REGS words is at least partially passed in a register unless
   its data type forbids.  */


rtx
sh_function_arg (CUMULATIVE_ARGS *ca, enum machine_mode mode,
		 tree type, int named)
{
  if (! TARGET_SH5 && mode == VOIDmode)
    return GEN_INT (ca->renesas_abi ? 1 : 0);

  if (! TARGET_SH5
      && PASS_IN_REG_P (*ca, mode, type)
      && (named || ! (TARGET_HITACHI || ca->renesas_abi)))
    {
      int regno;

      if (mode == SCmode && TARGET_SH4 && TARGET_LITTLE_ENDIAN
	  && (! FUNCTION_ARG_SCmode_WART || (ROUND_REG (*ca, mode) & 1)))
	{
	  rtx r1 = gen_rtx_EXPR_LIST (VOIDmode,
				      gen_rtx_REG (SFmode,
						   BASE_ARG_REG (mode)
						   + (ROUND_REG (*ca, mode) ^ 1)),
				      const0_rtx);
	  rtx r2 = gen_rtx_EXPR_LIST (VOIDmode,
				      gen_rtx_REG (SFmode,
						   BASE_ARG_REG (mode)
						   + ((ROUND_REG (*ca, mode) + 1) ^ 1)),
				      GEN_INT (4));
	  return gen_rtx_PARALLEL(SCmode, gen_rtvec(2, r1, r2));
	}

     /* If the alignment of a DF value causes an SF register to be
	skipped, we will use that skipped register for the next SF
	value.  */
      if ((TARGET_HITACHI || ca->renesas_abi)
	  && ca->free_single_fp_reg
	  && mode == SFmode)
	return gen_rtx_REG (mode, ca->free_single_fp_reg);

      regno = (BASE_ARG_REG (mode) + ROUND_REG (*ca, mode))
	       ^ (mode == SFmode && TARGET_SH4
		  && TARGET_LITTLE_ENDIAN != 0
		  && ! TARGET_HITACHI && ! ca->renesas_abi);
      return gen_rtx_REG (mode, regno);

    }

  if (TARGET_SH5)
    {
      if (mode == VOIDmode && TARGET_SHCOMPACT)
	return GEN_INT (ca->call_cookie);

      /* The following test assumes unnamed arguments are promoted to
	 DFmode.  */
      if (mode == SFmode && ca->free_single_fp_reg)
	return SH5_PROTOTYPED_FLOAT_ARG (*ca, mode, ca->free_single_fp_reg);

      if ((GET_SH_ARG_CLASS (mode) == SH_ARG_FLOAT)
	  && (named || ! ca->prototype_p)
	  && ca->arg_count[(int) SH_ARG_FLOAT] < NPARM_REGS (SFmode))
	{
	  if (! ca->prototype_p && TARGET_SHMEDIA)
	    return SH5_PROTOTYPELESS_FLOAT_ARG (*ca, mode);

	  return SH5_PROTOTYPED_FLOAT_ARG (*ca, mode,
					   FIRST_FP_PARM_REG
					   + ca->arg_count[(int) SH_ARG_FLOAT]);
	}

      if (ca->arg_count[(int) SH_ARG_INT] < NPARM_REGS (SImode)
	  && (! TARGET_SHCOMPACT
	      || (! SHCOMPACT_FORCE_ON_STACK (mode, type)
		  && ! SH5_WOULD_BE_PARTIAL_NREGS (*ca, mode,
						   type, named))))
	{
	  return gen_rtx_REG (mode, (FIRST_PARM_REG
				       + ca->arg_count[(int) SH_ARG_INT]));
	}

      return 0;
    }

  return 0;
}

/* Update the data in CUM to advance over an argument
   of mode MODE and data type TYPE.
   (TYPE is null for libcalls where that information may not be
   available.)  */

void
sh_function_arg_advance (CUMULATIVE_ARGS *ca, enum machine_mode mode,
			 tree type, int named)
{
  if (ca->force_mem)
    ca->force_mem = 0;
  else if (TARGET_SH5)
    {
      tree type2 = (ca->byref && type
		    ? TREE_TYPE (type)
		    : type);
      enum machine_mode mode2 = (ca->byref && type
				 ? TYPE_MODE (type2)
				 : mode);
      int dwords = ((ca->byref
		     ? ca->byref
		     : mode2 == BLKmode
		     ? int_size_in_bytes (type2)
		     : GET_MODE_SIZE (mode2)) + 7) / 8;
      int numregs = MIN (dwords, NPARM_REGS (SImode)
			 - ca->arg_count[(int) SH_ARG_INT]);

      if (numregs)
	{
	  ca->arg_count[(int) SH_ARG_INT] += numregs;
	  if (TARGET_SHCOMPACT
	      && SHCOMPACT_FORCE_ON_STACK (mode2, type2))
	    {
	      ca->call_cookie
		|= CALL_COOKIE_INT_REG (ca->arg_count[(int) SH_ARG_INT]
					- numregs, 1);
	      /* N.B. We want this also for outgoing.  */
	      ca->stack_regs += numregs;
	    }
	  else if (ca->byref)
	    {
	      if (! ca->outgoing)
		ca->stack_regs += numregs;
	      ca->byref_regs += numregs;
	      ca->byref = 0;
	      do
		ca->call_cookie
		  |= CALL_COOKIE_INT_REG (ca->arg_count[(int) SH_ARG_INT]
					  - numregs, 2);
	      while (--numregs);
	      ca->call_cookie
		|= CALL_COOKIE_INT_REG (ca->arg_count[(int) SH_ARG_INT]
					- 1, 1);
	    }
	  else if (dwords > numregs)
	    {
	      int pushregs = numregs;

	      if (TARGET_SHCOMPACT)
		ca->stack_regs += numregs;
	      while (pushregs < NPARM_REGS (SImode) - 1
		     && (CALL_COOKIE_INT_REG_GET
			 (ca->call_cookie,
			  NPARM_REGS (SImode) - pushregs)
			 == 1))
		{
		  ca->call_cookie
		    &= ~ CALL_COOKIE_INT_REG (NPARM_REGS (SImode)
					      - pushregs, 1);
		  pushregs++;
		}
	      if (numregs == NPARM_REGS (SImode))
		ca->call_cookie
		  |= CALL_COOKIE_INT_REG (0, 1)
		  | CALL_COOKIE_STACKSEQ (numregs - 1);
	      else
		ca->call_cookie
		  |= CALL_COOKIE_STACKSEQ (numregs);
	    }
	}
      if (GET_SH_ARG_CLASS (mode2) == SH_ARG_FLOAT
	  && (named || ! ca->prototype_p))
	{
	  if (mode2 == SFmode && ca->free_single_fp_reg)
	    ca->free_single_fp_reg = 0;
	  else if (ca->arg_count[(int) SH_ARG_FLOAT]
		   < NPARM_REGS (SFmode))
	    {
	      int numfpregs
		= MIN ((GET_MODE_SIZE (mode2) + 7) / 8 * 2,
		       NPARM_REGS (SFmode)
		       - ca->arg_count[(int) SH_ARG_FLOAT]);

	      ca->arg_count[(int) SH_ARG_FLOAT] += numfpregs;

	      if (TARGET_SHCOMPACT && ! ca->prototype_p)
		{
		  if (ca->outgoing && numregs > 0)
		    do
		      {
			ca->call_cookie
			  |= (CALL_COOKIE_INT_REG
			      (ca->arg_count[(int) SH_ARG_INT]
			       - numregs + ((numfpregs - 2) / 2),
			       4 + (ca->arg_count[(int) SH_ARG_FLOAT]
				    - numfpregs) / 2));
		      }
		    while (numfpregs -= 2);
		}
	      else if (mode2 == SFmode && (named)
		       && (ca->arg_count[(int) SH_ARG_FLOAT]
			   < NPARM_REGS (SFmode)))
		ca->free_single_fp_reg
		  = FIRST_FP_PARM_REG - numfpregs
		  + ca->arg_count[(int) SH_ARG_FLOAT] + 1;
	    }
	}
      return;
    }

  if ((TARGET_HITACHI || ca->renesas_abi) && TARGET_FPU_DOUBLE)
    {
      /* Note that we've used the skipped register.  */
      if (mode == SFmode && ca->free_single_fp_reg)
	{
	  ca->free_single_fp_reg = 0;
	  return;
	}
      /* When we have a DF after an SF, there's an SF register that get
	 skipped in order to align the DF value.  We note this skipped
	 register, because the next SF value will use it, and not the
	 SF that follows the DF.  */
      if (mode == DFmode
	  && ROUND_REG (*ca, DFmode) != ROUND_REG (*ca, SFmode))
	{
	  ca->free_single_fp_reg = (ROUND_REG (*ca, SFmode)
				    + BASE_ARG_REG (mode));
	}
    }

  if (! ((TARGET_SH4 || TARGET_SH2A) || ca->renesas_abi)
      || PASS_IN_REG_P (*ca, mode, type))
    (ca->arg_count[(int) GET_SH_ARG_CLASS (mode)]
     = (ROUND_REG (*ca, mode)
	+ (mode == BLKmode
	   ? ROUND_ADVANCE (int_size_in_bytes (type))
	   : ROUND_ADVANCE (GET_MODE_SIZE (mode)))));
}

/* The Renesas calling convention doesn't quite fit into this scheme since
   the address is passed like an invisible argument, but one that is always
   passed in memory.  */
static rtx
sh_struct_value_rtx (tree fndecl, int incoming ATTRIBUTE_UNUSED)
{
  if (TARGET_HITACHI || sh_attr_renesas_p (fndecl))
    return 0;
  return gen_rtx_REG (Pmode, 2);
}

/* Worker function for TARGET_RETURN_IN_MEMORY.  */

static bool
sh_return_in_memory (tree type, tree fndecl)
{
  if (TARGET_SH5)
    {
      if (TYPE_MODE (type) == BLKmode)
	return ((unsigned HOST_WIDE_INT) int_size_in_bytes (type)) > 8;
      else
	return GET_MODE_SIZE (TYPE_MODE (type)) > 8;
    }
  else
    {
      return (TYPE_MODE (type) == BLKmode
	      || ((TARGET_HITACHI || sh_attr_renesas_p (fndecl))
		  && TREE_CODE (type) == RECORD_TYPE));
    }
}

/* We actually emit the code in sh_expand_prologue.  We used to use
   a static variable to flag that we need to emit this code, but that
   doesn't when inlining, when functions are deferred and then emitted
   later.  Fortunately, we already have two flags that are part of struct
   function that tell if a function uses varargs or stdarg.  */
static void
sh_setup_incoming_varargs (CUMULATIVE_ARGS *ca,
			   enum machine_mode mode,
			   tree type,
			   int *pretend_arg_size,
			   int second_time ATTRIBUTE_UNUSED)
{
  gcc_assert (current_function_stdarg);
  if (TARGET_VARARGS_PRETEND_ARGS (current_function_decl))
    {
      int named_parm_regs, anon_parm_regs;

      named_parm_regs = (ROUND_REG (*ca, mode)
			 + (mode == BLKmode
			    ? ROUND_ADVANCE (int_size_in_bytes (type))
			    : ROUND_ADVANCE (GET_MODE_SIZE (mode))));
      anon_parm_regs = NPARM_REGS (SImode) - named_parm_regs;
      if (anon_parm_regs > 0)
	*pretend_arg_size = anon_parm_regs * 4;
    }
}

static bool
sh_strict_argument_naming (CUMULATIVE_ARGS *ca ATTRIBUTE_UNUSED)
{
  return TARGET_SH5;
}

static bool
sh_pretend_outgoing_varargs_named (CUMULATIVE_ARGS *ca)
{
  return ! (TARGET_HITACHI || ca->renesas_abi) && ! TARGET_SH5;
}


/* Define the offset between two registers, one to be eliminated, and
   the other its replacement, at the start of a routine.  */

int
initial_elimination_offset (int from, int to)
{
  int regs_saved;
  int regs_saved_rounding = 0;
  int total_saved_regs_space;
  int total_auto_space;
  int save_flags = target_flags;
  int copy_flags;
  HARD_REG_SET live_regs_mask;

  shmedia_space_reserved_for_target_registers = false;
  regs_saved = calc_live_regs (&live_regs_mask);
  regs_saved += SHMEDIA_REGS_STACK_ADJUST ();

  if (shmedia_reserve_space_for_target_registers_p (regs_saved, &live_regs_mask))
    {
      shmedia_space_reserved_for_target_registers = true;
      regs_saved += shmedia_target_regs_stack_adjust (&live_regs_mask);
    }

  if (TARGET_SH5 && regs_saved % (STACK_BOUNDARY / BITS_PER_UNIT))
    regs_saved_rounding = ((STACK_BOUNDARY / BITS_PER_UNIT)
			   - regs_saved % (STACK_BOUNDARY / BITS_PER_UNIT));

  total_auto_space = rounded_frame_size (regs_saved) - regs_saved_rounding;
  copy_flags = target_flags;
  target_flags = save_flags;

  total_saved_regs_space = regs_saved + regs_saved_rounding;

  if (from == ARG_POINTER_REGNUM && to == HARD_FRAME_POINTER_REGNUM)
    return total_saved_regs_space + total_auto_space
      + current_function_args_info.byref_regs * 8;

  if (from == ARG_POINTER_REGNUM && to == STACK_POINTER_REGNUM)
    return total_saved_regs_space + total_auto_space
      + current_function_args_info.byref_regs * 8;

  /* Initial gap between fp and sp is 0.  */
  if (from == HARD_FRAME_POINTER_REGNUM && to == STACK_POINTER_REGNUM)
    return 0;

  if (from == FRAME_POINTER_REGNUM && to == STACK_POINTER_REGNUM)
    return rounded_frame_size (0);

  if (from == FRAME_POINTER_REGNUM && to == HARD_FRAME_POINTER_REGNUM)
    return rounded_frame_size (0);

  gcc_assert (from == RETURN_ADDRESS_POINTER_REGNUM
	      && (to == HARD_FRAME_POINTER_REGNUM
		  || to == STACK_POINTER_REGNUM));
  if (TARGET_SH5)
    {
      int n = total_saved_regs_space;
      int pr_reg = TARGET_SHMEDIA ? PR_MEDIA_REG : PR_REG;
      save_schedule schedule;
      save_entry *entry;
      
      n += total_auto_space;
      
      /* If it wasn't saved, there's not much we can do.  */
      if (! TEST_HARD_REG_BIT (live_regs_mask, pr_reg))
	return n;
      
      target_flags = copy_flags;
      
      sh5_schedule_saves (&live_regs_mask, &schedule, n);
      for (entry = &schedule.entries[1]; entry->mode != VOIDmode; entry++)
	if (entry->reg == pr_reg)
	  {
	    target_flags = save_flags;
	    return entry->offset;
	  }
      gcc_unreachable ();
    }
  else
    return total_auto_space;
}

/* Insert any deferred function attributes from earlier pragmas.  */
static void
sh_insert_attributes (tree node, tree *attributes)
{
  tree attrs;

  if (TREE_CODE (node) != FUNCTION_DECL)
    return;

  /* We are only interested in fields.  */
  if (!DECL_P (node))
    return;

  /* Append the attributes to the deferred attributes.  */
  *sh_deferred_function_attributes_tail = *attributes;
  attrs = sh_deferred_function_attributes;
  if (!attrs)
    return;

  /* Some attributes imply or require the interrupt attribute.  */
  if (!lookup_attribute ("interrupt_handler", attrs)
      && !lookup_attribute ("interrupt_handler", DECL_ATTRIBUTES (node)))
    {
      /* If we have a trapa_handler, but no interrupt_handler attribute,
	 insert an interrupt_handler attribute.  */
      if (lookup_attribute ("trapa_handler", attrs) != NULL_TREE)
	/* We can't use sh_pr_interrupt here because that's not in the
	   java frontend.  */
	attrs
	  = tree_cons (get_identifier("interrupt_handler"), NULL_TREE, attrs);
      /* However, for sp_switch, trap_exit and nosave_low_regs, if the
	 interrupt attribute is missing, we ignore the attribute and warn.  */
      else if (lookup_attribute ("sp_switch", attrs)
	       || lookup_attribute ("trap_exit", attrs)
	       || lookup_attribute ("nosave_low_regs", attrs))
	{
	  tree *tail;

	  for (tail = attributes; attrs; attrs = TREE_CHAIN (attrs))
	    {
	      if (is_attribute_p ("sp_switch", TREE_PURPOSE (attrs))
		  || is_attribute_p ("trap_exit", TREE_PURPOSE (attrs))
		  || is_attribute_p ("nosave_low_regs", TREE_PURPOSE (attrs)))
		warning (OPT_Wattributes,
			 "%qs attribute only applies to interrupt functions",
			 IDENTIFIER_POINTER (TREE_PURPOSE (attrs)));
	      else
		{
		  *tail = tree_cons (TREE_PURPOSE (attrs), NULL_TREE,
				     NULL_TREE);
		  tail = &TREE_CHAIN (*tail);
		}
	    }
	  attrs = *attributes;
	}
    }

  /* Install the processed list.  */
  *attributes = attrs;

  /* Clear deferred attributes.  */
  sh_deferred_function_attributes = NULL_TREE;
  sh_deferred_function_attributes_tail = &sh_deferred_function_attributes;

  return;
}

/* Supported attributes:

   interrupt_handler -- specifies this function is an interrupt handler.

   trapa_handler - like above, but don't save all registers.

   sp_switch -- specifies an alternate stack for an interrupt handler
   to run on.

   trap_exit -- use a trapa to exit an interrupt function instead of
   an rte instruction.

   nosave_low_regs - don't save r0..r7 in an interrupt handler.
     This is useful on the SH3 and upwards,
     which has a separate set of low regs for User and Supervisor modes.
     This should only be used for the lowest level of interrupts.  Higher levels
     of interrupts must save the registers in case they themselves are
     interrupted.

   renesas -- use Renesas calling/layout conventions (functions and
   structures).

*/

const struct attribute_spec sh_attribute_table[] =
{
  /* { name, min_len, max_len, decl_req, type_req, fn_type_req, handler } */
  { "interrupt_handler", 0, 0, true,  false, false, sh_handle_interrupt_handler_attribute },
  { "sp_switch",         1, 1, true,  false, false, sh_handle_sp_switch_attribute },
  { "trap_exit",         1, 1, true,  false, false, sh_handle_trap_exit_attribute },
  { "renesas",           0, 0, false, true, false, sh_handle_renesas_attribute },
  { "trapa_handler",     0, 0, true,  false, false, sh_handle_interrupt_handler_attribute },
  { "nosave_low_regs",   0, 0, true,  false, false, sh_handle_interrupt_handler_attribute },
#ifdef SYMBIAN
  /* Symbian support adds three new attributes:
     dllexport - for exporting a function/variable that will live in a dll
     dllimport - for importing a function/variable from a dll

     Microsoft allows multiple declspecs in one __declspec, separating
     them with spaces.  We do NOT support this.  Instead, use __declspec
     multiple times.  */
  { "dllimport",         0, 0, true,  false, false, sh_symbian_handle_dll_attribute },
  { "dllexport",         0, 0, true,  false, false, sh_symbian_handle_dll_attribute },
#endif
  { NULL,                0, 0, false, false, false, NULL }
};

/* Handle an "interrupt_handler" attribute; arguments as in
   struct attribute_spec.handler.  */
static tree
sh_handle_interrupt_handler_attribute (tree *node, tree name,
				       tree args ATTRIBUTE_UNUSED,
				       int flags ATTRIBUTE_UNUSED,
				       bool *no_add_attrs)
{
  if (TREE_CODE (*node) != FUNCTION_DECL)
    {
      warning (OPT_Wattributes, "%qs attribute only applies to functions",
	       IDENTIFIER_POINTER (name));
      *no_add_attrs = true;
    }
  else if (TARGET_SHCOMPACT)
    {
      error ("attribute interrupt_handler is not compatible with -m5-compact");
      *no_add_attrs = true;
    }

  return NULL_TREE;
}

/* Handle an "sp_switch" attribute; arguments as in
   struct attribute_spec.handler.  */
static tree
sh_handle_sp_switch_attribute (tree *node, tree name, tree args,
			       int flags ATTRIBUTE_UNUSED, bool *no_add_attrs)
{
  if (TREE_CODE (*node) != FUNCTION_DECL)
    {
      warning (OPT_Wattributes, "%qs attribute only applies to functions",
	       IDENTIFIER_POINTER (name));
      *no_add_attrs = true;
    }
  else if (TREE_CODE (TREE_VALUE (args)) != STRING_CST)
    {
      /* The argument must be a constant string.  */
      warning (OPT_Wattributes, "%qs attribute argument not a string constant",
	       IDENTIFIER_POINTER (name));
      *no_add_attrs = true;
    }

  return NULL_TREE;
}

/* Handle an "trap_exit" attribute; arguments as in
   struct attribute_spec.handler.  */
static tree
sh_handle_trap_exit_attribute (tree *node, tree name, tree args,
			       int flags ATTRIBUTE_UNUSED, bool *no_add_attrs)
{
  if (TREE_CODE (*node) != FUNCTION_DECL)
    {
      warning (OPT_Wattributes, "%qs attribute only applies to functions",
	       IDENTIFIER_POINTER (name));
      *no_add_attrs = true;
    }
  /* The argument specifies a trap number to be used in a trapa instruction
     at function exit (instead of an rte instruction).  */
  else if (TREE_CODE (TREE_VALUE (args)) != INTEGER_CST)
    {
      /* The argument must be a constant integer.  */
      warning (OPT_Wattributes, "%qs attribute argument not an "
	       "integer constant", IDENTIFIER_POINTER (name));
      *no_add_attrs = true;
    }

  return NULL_TREE;
}

static tree
sh_handle_renesas_attribute (tree *node ATTRIBUTE_UNUSED,
			     tree name ATTRIBUTE_UNUSED,
			     tree args ATTRIBUTE_UNUSED,
			     int flags ATTRIBUTE_UNUSED,
			     bool *no_add_attrs ATTRIBUTE_UNUSED)
{
  return NULL_TREE;
}

/* True if __attribute__((renesas)) or -mrenesas.  */
int
sh_attr_renesas_p (tree td)
{
  if (TARGET_HITACHI)
    return 1;
  if (td == 0)
    return 0;
  if (DECL_P (td))
    td = TREE_TYPE (td);
  if (td == error_mark_node)
    return 0;
  return (lookup_attribute ("renesas", TYPE_ATTRIBUTES (td))
	  != NULL_TREE);
}

/* True if __attribute__((renesas)) or -mrenesas, for the current
   function.  */
int
sh_cfun_attr_renesas_p (void)
{
  return sh_attr_renesas_p (current_function_decl);
}

int
sh_cfun_interrupt_handler_p (void)
{
  return (lookup_attribute ("interrupt_handler",
			    DECL_ATTRIBUTES (current_function_decl))
	  != NULL_TREE);
}

/* Implement TARGET_CHECK_PCH_TARGET_FLAGS.  */

static const char *
sh_check_pch_target_flags (int old_flags)
{
  if ((old_flags ^ target_flags) & (MASK_SH1 | MASK_SH2 | MASK_SH3
				    | MASK_SH_E | MASK_HARD_SH4
				    | MASK_FPU_SINGLE | MASK_SH4))
    return _("created and used with different architectures / ABIs");
  if ((old_flags ^ target_flags) & MASK_HITACHI)
    return _("created and used with different ABIs");
  if ((old_flags ^ target_flags) & MASK_LITTLE_ENDIAN)
    return _("created and used with different endianness");
  return NULL;
}

/* Predicates used by the templates.  */

/* Returns 1 if OP is MACL, MACH or PR.  The input must be a REG rtx.
   Used only in general_movsrc_operand.  */

int
system_reg_operand (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  switch (REGNO (op))
    {
    case PR_REG:
    case MACL_REG:
    case MACH_REG:
      return 1;
    }
  return 0;
}

/* Nonzero if OP is a floating point value with value 0.0.  */

int
fp_zero_operand (rtx op)
{
  REAL_VALUE_TYPE r;

  if (GET_MODE (op) != SFmode)
    return 0;

  REAL_VALUE_FROM_CONST_DOUBLE (r, op);
  return REAL_VALUES_EQUAL (r, dconst0) && ! REAL_VALUE_MINUS_ZERO (r);
}

/* Nonzero if OP is a floating point value with value 1.0.  */

int
fp_one_operand (rtx op)
{
  REAL_VALUE_TYPE r;

  if (GET_MODE (op) != SFmode)
    return 0;

  REAL_VALUE_FROM_CONST_DOUBLE (r, op);
  return REAL_VALUES_EQUAL (r, dconst1);
}

/* For -m4 and -m4-single-only, mode switching is used.  If we are
   compiling without -mfmovd, movsf_ie isn't taken into account for
   mode switching.  We could check in machine_dependent_reorg for
   cases where we know we are in single precision mode, but there is
   interface to find that out during reload, so we must avoid
   choosing an fldi alternative during reload and thus failing to
   allocate a scratch register for the constant loading.  */
int
fldi_ok (void)
{
  return ! TARGET_SH4 || TARGET_FMOVD || reload_completed;
}

int
tertiary_reload_operand (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  enum rtx_code code = GET_CODE (op);
  return code == MEM || (TARGET_SH4 && code == CONST_DOUBLE);
}

/* Return the TLS type for TLS symbols, 0 for otherwise.  */
int
tls_symbolic_operand (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  if (GET_CODE (op) != SYMBOL_REF)
    return 0;
  return SYMBOL_REF_TLS_MODEL (op);
}

/* Return the destination address of a branch.  */

static int
branch_dest (rtx branch)
{
  rtx dest = SET_SRC (PATTERN (branch));
  int dest_uid;

  if (GET_CODE (dest) == IF_THEN_ELSE)
    dest = XEXP (dest, 1);
  dest = XEXP (dest, 0);
  dest_uid = INSN_UID (dest);
  return INSN_ADDRESSES (dest_uid);
}

/* Return nonzero if REG is not used after INSN.
   We assume REG is a reload reg, and therefore does
   not live past labels.  It may live past calls or jumps though.  */
int
reg_unused_after (rtx reg, rtx insn)
{
  enum rtx_code code;
  rtx set;

  /* If the reg is set by this instruction, then it is safe for our
     case.  Disregard the case where this is a store to memory, since
     we are checking a register used in the store address.  */
  set = single_set (insn);
  if (set && GET_CODE (SET_DEST (set)) != MEM
      && reg_overlap_mentioned_p (reg, SET_DEST (set)))
    return 1;

  while ((insn = NEXT_INSN (insn)))
    {
      rtx set;
      if (!INSN_P (insn))
	continue;

      code = GET_CODE (insn);

#if 0
      /* If this is a label that existed before reload, then the register
	 if dead here.  However, if this is a label added by reorg, then
	 the register may still be live here.  We can't tell the difference,
	 so we just ignore labels completely.  */
      if (code == CODE_LABEL)
	return 1;
      /* else */
#endif

      if (code == JUMP_INSN)
	return 0;

      /* If this is a sequence, we must handle them all at once.
	 We could have for instance a call that sets the target register,
	 and an insn in a delay slot that uses the register.  In this case,
	 we must return 0.  */
      else if (code == INSN && GET_CODE (PATTERN (insn)) == SEQUENCE)
	{
	  int i;
	  int retval = 0;

	  for (i = 0; i < XVECLEN (PATTERN (insn), 0); i++)
	    {
	      rtx this_insn = XVECEXP (PATTERN (insn), 0, i);
	      rtx set = single_set (this_insn);

	      if (GET_CODE (this_insn) == CALL_INSN)
		code = CALL_INSN;
	      else if (GET_CODE (this_insn) == JUMP_INSN)
		{
		  if (INSN_ANNULLED_BRANCH_P (this_insn))
		    return 0;
		  code = JUMP_INSN;
		}

	      if (set && reg_overlap_mentioned_p (reg, SET_SRC (set)))
		return 0;
	      if (set && reg_overlap_mentioned_p (reg, SET_DEST (set)))
		{
		  if (GET_CODE (SET_DEST (set)) != MEM)
		    retval = 1;
		  else
		    return 0;
		}
	      if (set == 0
		  && reg_overlap_mentioned_p (reg, PATTERN (this_insn)))
		return 0;
	    }
	  if (retval == 1)
	    return 1;
	  else if (code == JUMP_INSN)
	    return 0;
	}

      set = single_set (insn);
      if (set && reg_overlap_mentioned_p (reg, SET_SRC (set)))
	return 0;
      if (set && reg_overlap_mentioned_p (reg, SET_DEST (set)))
	return GET_CODE (SET_DEST (set)) != MEM;
      if (set == 0 && reg_overlap_mentioned_p (reg, PATTERN (insn)))
	return 0;

      if (code == CALL_INSN && call_really_used_regs[REGNO (reg)])
	return 1;
    }
  return 1;
}

#include "ggc.h"

static GTY(()) rtx fpscr_rtx;
rtx
get_fpscr_rtx (void)
{
  if (! fpscr_rtx)
    {
      fpscr_rtx = gen_rtx_REG (PSImode, FPSCR_REG);
      REG_USERVAR_P (fpscr_rtx) = 1;
      mark_user_reg (fpscr_rtx);
    }
  if (! reload_completed || mdep_reorg_phase != SH_AFTER_MDEP_REORG)
    mark_user_reg (fpscr_rtx);
  return fpscr_rtx;
}

static GTY(()) tree fpscr_values;

static void
emit_fpu_switch (rtx scratch, int index)
{
  rtx dst, src;

  if (fpscr_values == NULL)
    {
      tree t;

      t = build_index_type (integer_one_node);
      t = build_array_type (integer_type_node, t);
      t = build_decl (VAR_DECL, get_identifier ("__fpscr_values"), t);
      DECL_ARTIFICIAL (t) = 1;
      DECL_IGNORED_P (t) = 1;
      DECL_EXTERNAL (t) = 1;
      TREE_STATIC (t) = 1;
      TREE_PUBLIC (t) = 1;
      TREE_USED (t) = 1;

      fpscr_values = t;
    }

  src = DECL_RTL (fpscr_values);
  if (no_new_pseudos)
    {
      emit_move_insn (scratch, XEXP (src, 0));
      if (index != 0)
	emit_insn (gen_addsi3 (scratch, scratch, GEN_INT (index * 4)));
      src = adjust_automodify_address (src, PSImode, scratch, index * 4);
    }
  else
    src = adjust_address (src, PSImode, index * 4);

  dst = get_fpscr_rtx ();
  emit_move_insn (dst, src);
}

void
emit_sf_insn (rtx pat)
{
  emit_insn (pat);
}

void
emit_df_insn (rtx pat)
{
  emit_insn (pat);
}

void
expand_sf_unop (rtx (*fun) (rtx, rtx, rtx), rtx *operands)
{
  emit_sf_insn ((*fun) (operands[0], operands[1], get_fpscr_rtx ()));
}

void
expand_sf_binop (rtx (*fun) (rtx, rtx, rtx, rtx), rtx *operands)
{
  emit_sf_insn ((*fun) (operands[0], operands[1], operands[2],
			 get_fpscr_rtx ()));
}

void
expand_df_unop (rtx (*fun) (rtx, rtx, rtx), rtx *operands)
{
  emit_df_insn ((*fun) (operands[0], operands[1], get_fpscr_rtx ()));
}

void
expand_df_binop (rtx (*fun) (rtx, rtx, rtx, rtx), rtx *operands)
{
  emit_df_insn ((*fun) (operands[0], operands[1], operands[2],
			get_fpscr_rtx ()));
}

/* ??? gcc does flow analysis strictly after common subexpression
   elimination.  As a result, common subexpression elimination fails
   when there are some intervening statements setting the same register.
   If we did nothing about this, this would hurt the precision switching
   for SH4 badly.  There is some cse after reload, but it is unable to
   undo the extra register pressure from the unused instructions, and
   it cannot remove auto-increment loads.

   A C code example that shows this flow/cse weakness for (at least) SH
   and sparc (as of gcc ss-970706) is this:

double
f(double a)
{
  double d;
  d = 0.1;
  a += d;
  d = 1.1;
  d = 0.1;
  a *= d;
  return a;
}

   So we add another pass before common subexpression elimination, to
   remove assignments that are dead due to a following assignment in the
   same basic block.  */

static void
mark_use (rtx x, rtx *reg_set_block)
{
  enum rtx_code code;

  if (! x)
    return;
  code = GET_CODE (x);
  switch (code)
    {
    case REG:
      {
	int regno = REGNO (x);
	int nregs = (regno < FIRST_PSEUDO_REGISTER
		     ? HARD_REGNO_NREGS (regno, GET_MODE (x))
		     : 1);
	do
	  {
	    reg_set_block[regno + nregs - 1] = 0;
	  }
	while (--nregs);
	break;
      }
    case SET:
      {
	rtx dest = SET_DEST (x);

	if (GET_CODE (dest) == SUBREG)
	  dest = SUBREG_REG (dest);
	if (GET_CODE (dest) != REG)
	  mark_use (dest, reg_set_block);
	mark_use (SET_SRC (x), reg_set_block);
	break;
      }
    case CLOBBER:
      break;
    default:
      {
	const char *fmt = GET_RTX_FORMAT (code);
	int i, j;
	for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
	  {
	    if (fmt[i] == 'e')
	      mark_use (XEXP (x, i), reg_set_block);
	    else if (fmt[i] == 'E')
	      for (j = XVECLEN (x, i) - 1; j >= 0; j--)
		mark_use (XVECEXP (x, i, j), reg_set_block);
	  }
	break;
      }
    }
}

static rtx get_free_reg (HARD_REG_SET);

/* This function returns a register to use to load the address to load
   the fpscr from.  Currently it always returns r1 or r7, but when we are
   able to use pseudo registers after combine, or have a better mechanism
   for choosing a register, it should be done here.  */
/* REGS_LIVE is the liveness information for the point for which we
   need this allocation.  In some bare-bones exit blocks, r1 is live at the
   start.  We can even have all of r0..r3 being live:
__complex__ long long f (double d) { if (d == 0) return 2; else return 3; }
   INSN before which new insns are placed with will clobber the register
   we return.  If a basic block consists only of setting the return value
   register to a pseudo and using that register, the return value is not
   live before or after this block, yet we we'll insert our insns right in
   the middle.  */

static rtx
get_free_reg (HARD_REG_SET regs_live)
{
  if (! TEST_HARD_REG_BIT (regs_live, 1))
    return gen_rtx_REG (Pmode, 1);

  /* Hard reg 1 is live; since this is a SMALL_REGISTER_CLASSES target,
     there shouldn't be anything but a jump before the function end.  */
  gcc_assert (!TEST_HARD_REG_BIT (regs_live, 7));
  return gen_rtx_REG (Pmode, 7);
}

/* This function will set the fpscr from memory.
   MODE is the mode we are setting it to.  */
void
fpscr_set_from_mem (int mode, HARD_REG_SET regs_live)
{
  enum attr_fp_mode fp_mode = mode;
  enum attr_fp_mode norm_mode = ACTUAL_NORMAL_MODE (FP_MODE);
  rtx addr_reg = get_free_reg (regs_live);

  emit_fpu_switch (addr_reg, fp_mode == norm_mode);
}

/* Is the given character a logical line separator for the assembler?  */
#ifndef IS_ASM_LOGICAL_LINE_SEPARATOR
#define IS_ASM_LOGICAL_LINE_SEPARATOR(C) ((C) == ';')
#endif

int
sh_insn_length_adjustment (rtx insn)
{
  /* Instructions with unfilled delay slots take up an extra two bytes for
     the nop in the delay slot.  */
  if (((GET_CODE (insn) == INSN
	&& GET_CODE (PATTERN (insn)) != USE
	&& GET_CODE (PATTERN (insn)) != CLOBBER)
       || GET_CODE (insn) == CALL_INSN
       || (GET_CODE (insn) == JUMP_INSN
	   && GET_CODE (PATTERN (insn)) != ADDR_DIFF_VEC
	   && GET_CODE (PATTERN (insn)) != ADDR_VEC))
      && GET_CODE (PATTERN (NEXT_INSN (PREV_INSN (insn)))) != SEQUENCE
      && get_attr_needs_delay_slot (insn) == NEEDS_DELAY_SLOT_YES)
    return 2;

  /* SH2e has a bug that prevents the use of annulled branches, so if
     the delay slot is not filled, we'll have to put a NOP in it.  */
  if (sh_cpu == CPU_SH2E
      && GET_CODE (insn) == JUMP_INSN
      && GET_CODE (PATTERN (insn)) != ADDR_DIFF_VEC
      && GET_CODE (PATTERN (insn)) != ADDR_VEC
      && get_attr_type (insn) == TYPE_CBRANCH
      && GET_CODE (PATTERN (NEXT_INSN (PREV_INSN (insn)))) != SEQUENCE)
    return 2;

  /* sh-dsp parallel processing insn take four bytes instead of two.  */

  if (GET_CODE (insn) == INSN)
    {
      int sum = 0;
      rtx body = PATTERN (insn);
      const char *template;
      char c;
      int maybe_label = 1;

      if (GET_CODE (body) == ASM_INPUT)
	template = XSTR (body, 0);
      else if (asm_noperands (body) >= 0)
	template
	  = decode_asm_operands (body, NULL, NULL, NULL, NULL);
      else
	return 0;
      do
	{
	  int ppi_adjust = 0;

	  do
	    c = *template++;
	  while (c == ' ' || c == '\t');
	  /* all sh-dsp parallel-processing insns start with p.
	     The only non-ppi sh insn starting with p is pref.
	     The only ppi starting with pr is prnd.  */
	  if ((c == 'p' || c == 'P') && strncasecmp ("re", template, 2))
	    ppi_adjust = 2;
	  /* The repeat pseudo-insn expands two three insns, a total of
	     six bytes in size.  */
	  else if ((c == 'r' || c == 'R')
		   && ! strncasecmp ("epeat", template, 5))
	    ppi_adjust = 4;
	  while (c && c != '\n' && ! IS_ASM_LOGICAL_LINE_SEPARATOR (c))
	    {
	      /* If this is a label, it is obviously not a ppi insn.  */
	      if (c == ':' && maybe_label)
		{
		  ppi_adjust = 0;
		  break;
		}
	      else if (c == '\'' || c == '"')
		maybe_label = 0;
	      c = *template++;
	    }
	  sum += ppi_adjust;
	  maybe_label = c != ':';
	}
      while (c);
      return sum;
    }
  return 0;
}

/* Return TRUE if X references a SYMBOL_REF or LABEL_REF whose symbol
   isn't protected by a PIC unspec.  */
int
nonpic_symbol_mentioned_p (rtx x)
{
  register const char *fmt;
  register int i;

  if (GET_CODE (x) == SYMBOL_REF || GET_CODE (x) == LABEL_REF
      || GET_CODE (x) == PC)
    return 1;

  /* We don't want to look into the possible MEM location of a
     CONST_DOUBLE, since we're not going to use it, in general.  */
  if (GET_CODE (x) == CONST_DOUBLE)
    return 0;

  if (GET_CODE (x) == UNSPEC
      && (XINT (x, 1) == UNSPEC_PIC
	  || XINT (x, 1) == UNSPEC_GOT
	  || XINT (x, 1) == UNSPEC_GOTOFF
	  || XINT (x, 1) == UNSPEC_GOTPLT
	  || XINT (x, 1) == UNSPEC_GOTTPOFF
	  || XINT (x, 1) == UNSPEC_DTPOFF
	  || XINT (x, 1) == UNSPEC_PLT))
    return 0;

  fmt = GET_RTX_FORMAT (GET_CODE (x));
  for (i = GET_RTX_LENGTH (GET_CODE (x)) - 1; i >= 0; i--)
    {
      if (fmt[i] == 'E')
	{
	  register int j;

	  for (j = XVECLEN (x, i) - 1; j >= 0; j--)
	    if (nonpic_symbol_mentioned_p (XVECEXP (x, i, j)))
	      return 1;
	}
      else if (fmt[i] == 'e' && nonpic_symbol_mentioned_p (XEXP (x, i)))
	return 1;
    }

  return 0;
}

/* Convert a non-PIC address in `orig' to a PIC address using @GOT or
   @GOTOFF in `reg'.  */
rtx
legitimize_pic_address (rtx orig, enum machine_mode mode ATTRIBUTE_UNUSED,
			rtx reg)
{
  if (tls_symbolic_operand (orig, Pmode))
    return orig;

  if (GET_CODE (orig) == LABEL_REF
      || (GET_CODE (orig) == SYMBOL_REF && SYMBOL_REF_LOCAL_P (orig)))
    {
      if (reg == 0)
	reg = gen_reg_rtx (Pmode);

      emit_insn (gen_symGOTOFF2reg (reg, orig));
      return reg;
    }
  else if (GET_CODE (orig) == SYMBOL_REF)
    {
      if (reg == 0)
	reg = gen_reg_rtx (Pmode);

      emit_insn (gen_symGOT2reg (reg, orig));
      return reg;
    }
  return orig;
}

/* Mark the use of a constant in the literal table. If the constant
   has multiple labels, make it unique.  */
static rtx
mark_constant_pool_use (rtx x)
{
  rtx insn, lab, pattern;

  if (x == NULL)
    return x;

  switch (GET_CODE (x))
    {
    case LABEL_REF:
      x = XEXP (x, 0);
    case CODE_LABEL:
      break;
    default:
      return x;
    }

  /* Get the first label in the list of labels for the same constant
     and delete another labels in the list.  */
  lab = x;
  for (insn = PREV_INSN (x); insn; insn = PREV_INSN (insn))
    {
      if (GET_CODE (insn) != CODE_LABEL
	  || LABEL_REFS (insn) != NEXT_INSN (insn))
	break;
      lab = insn;
    }

  for (insn = LABEL_REFS (lab); insn; insn = LABEL_REFS (insn))
    INSN_DELETED_P (insn) = 1;

  /* Mark constants in a window.  */
  for (insn = NEXT_INSN (x); insn; insn = NEXT_INSN (insn))
    {
      if (GET_CODE (insn) != INSN)
	continue;

      pattern = PATTERN (insn);
      if (GET_CODE (pattern) != UNSPEC_VOLATILE)
	continue;

      switch (XINT (pattern, 1))
	{
	case UNSPECV_CONST2:
	case UNSPECV_CONST4:
	case UNSPECV_CONST8:
	  XVECEXP (pattern, 0, 1) = const1_rtx;
	  break;
	case UNSPECV_WINDOW_END:
	  if (XVECEXP (pattern, 0, 0) == x)
	    return lab;
	  break;
	case UNSPECV_CONST_END:
	  return lab;
	default:
	  break;
	}
    }

  return lab;
}

/* Return true if it's possible to redirect BRANCH1 to the destination
   of an unconditional jump BRANCH2.  We only want to do this if the
   resulting branch will have a short displacement.  */
int
sh_can_redirect_branch (rtx branch1, rtx branch2)
{
  if (flag_expensive_optimizations && simplejump_p (branch2))
    {
      rtx dest = XEXP (SET_SRC (single_set (branch2)), 0);
      rtx insn;
      int distance;

      for (distance = 0, insn = NEXT_INSN (branch1);
	   insn && distance < 256;
	   insn = PREV_INSN (insn))
	{
	  if (insn == dest)
	    return 1;
	  else
	    distance += get_attr_length (insn);
	}
      for (distance = 0, insn = NEXT_INSN (branch1);
	   insn && distance < 256;
	   insn = NEXT_INSN (insn))
	{
	  if (insn == dest)
	    return 1;
	  else
	    distance += get_attr_length (insn);
	}
    }
  return 0;
}

/* Return nonzero if register old_reg can be renamed to register new_reg.  */
int
sh_hard_regno_rename_ok (unsigned int old_reg ATTRIBUTE_UNUSED,
			 unsigned int new_reg)
{
  /* Interrupt functions can only use registers that have already been
     saved by the prologue, even if they would normally be
     call-clobbered.  */

  if (sh_cfun_interrupt_handler_p () && !regs_ever_live[new_reg])
    return 0;

  return 1;
}

/* Function to update the integer COST
   based on the relationship between INSN that is dependent on
   DEP_INSN through the dependence LINK.  The default is to make no
   adjustment to COST.  This can be used for example to specify to
   the scheduler that an output- or anti-dependence does not incur
   the same cost as a data-dependence.  The return value should be
   the new value for COST.  */
static int
sh_adjust_cost (rtx insn, rtx link ATTRIBUTE_UNUSED, rtx dep_insn, int cost)
{
  rtx reg, use_pat;

  if (TARGET_SHMEDIA)
    {
      /* On SHmedia, if the dependence is an anti-dependence or
         output-dependence, there is no cost.  */
      if (REG_NOTE_KIND (link) != 0)
	{
	  /* However, dependencies between target register loads and
	     uses of the register in a subsequent block that are separated
	     by a conditional branch are not modelled - we have to do with
	     the anti-dependency between the target register load and the
	     conditional branch that ends the current block.  */
	  if (REG_NOTE_KIND (link) == REG_DEP_ANTI
	      && GET_CODE (PATTERN (dep_insn)) == SET
	      && (get_attr_type (dep_insn) == TYPE_PT_MEDIA
		  || get_attr_type (dep_insn) == TYPE_PTABS_MEDIA)
	      && get_attr_type (insn) == TYPE_CBRANCH_MEDIA)
	    {
	      int orig_cost = cost;
	      rtx note = find_reg_note (insn, REG_BR_PROB, 0);
	      rtx target = ((! note
			     || INTVAL (XEXP (note, 0)) * 2 < REG_BR_PROB_BASE)
			    ? insn : JUMP_LABEL (insn));
	      /* On the likely path, the branch costs 1, on the unlikely path,
		 it costs 3.  */
	      cost--;
	      do
		target = next_active_insn (target);
	      while (target && ! flow_dependent_p (target, dep_insn)
		     && --cost > 0);
	      /* If two branches are executed in immediate succession, with the
		 first branch properly predicted, this causes a stall at the
		 second branch, hence we won't need the target for the
		 second branch for two cycles after the launch of the first
		 branch.  */
	      if (cost > orig_cost - 2)
		cost = orig_cost - 2;
	    }
	  else
	    cost = 0;
	}

      else if (get_attr_is_mac_media (insn)
	       && get_attr_is_mac_media (dep_insn))
	cost = 1;

      else if (! reload_completed
	       && GET_CODE (PATTERN (insn)) == SET
	       && GET_CODE (SET_SRC (PATTERN (insn))) == FLOAT
	       && GET_CODE (PATTERN (dep_insn)) == SET
	       && fp_arith_reg_operand (SET_SRC (PATTERN (dep_insn)), VOIDmode)
	       && cost < 4)
	cost = 4;
      /* Schedule the ptabs for a casesi_jump_media in preference to stuff
	 that is needed at the target.  */
      else if (get_attr_type (insn) == TYPE_JUMP_MEDIA
	       && ! flow_dependent_p (insn, dep_insn))
	cost--;
    }
  else if (REG_NOTE_KIND (link) == 0)
    {
      enum attr_type dep_type, type;

      if (recog_memoized (insn) < 0
	  || recog_memoized (dep_insn) < 0)
	return cost;

      dep_type = get_attr_type (dep_insn);
      if (dep_type == TYPE_FLOAD || dep_type == TYPE_PCFLOAD)
	cost--;
      if ((dep_type == TYPE_LOAD_SI || dep_type == TYPE_PCLOAD_SI)
	  && (type = get_attr_type (insn)) != TYPE_CALL
	  && type != TYPE_SFUNC)
	cost--;

      /* The only input for a call that is timing-critical is the
	 function's address.  */
      if (GET_CODE(insn) == CALL_INSN)
	{
	  rtx call = PATTERN (insn);

	  if (GET_CODE (call) == PARALLEL)
	    call = XVECEXP (call, 0 ,0);
	  if (GET_CODE (call) == SET)
	    call = SET_SRC (call);
	  if (GET_CODE (call) == CALL && GET_CODE (XEXP (call, 0)) == MEM
		  /* sibcalli_thunk uses a symbol_ref in an unspec.  */
	      && (GET_CODE (XEXP (XEXP (call, 0), 0)) == UNSPEC
		  || ! reg_set_p (XEXP (XEXP (call, 0), 0), dep_insn)))
	    cost = 0;
	}
      /* Likewise, the most timing critical input for an sfuncs call
	 is the function address.  However, sfuncs typically start
	 using their arguments pretty quickly.
	 Assume a four cycle delay before they are needed.  */
      /* All sfunc calls are parallels with at least four components.
	 Exploit this to avoid unnecessary calls to sfunc_uses_reg.  */
      else if (GET_CODE (PATTERN (insn)) == PARALLEL
	       && XVECLEN (PATTERN (insn), 0) >= 4
	       && (reg = sfunc_uses_reg (insn)))
	{
	  if (! reg_set_p (reg, dep_insn))
	    cost -= 4;
	}
      /* When the preceding instruction loads the shift amount of
	 the following SHAD/SHLD, the latency of the load is increased
	 by 1 cycle.  */
      else if (TARGET_SH4
	       && get_attr_type (insn) == TYPE_DYN_SHIFT
	       && get_attr_any_int_load (dep_insn) == ANY_INT_LOAD_YES
	       && reg_overlap_mentioned_p (SET_DEST (single_set (dep_insn)),
					   XEXP (SET_SRC (single_set (insn)),
						 1)))
	cost++;
      /* When an LS group instruction with a latency of less than
	 3 cycles is followed by a double-precision floating-point
	 instruction, FIPR, or FTRV, the latency of the first
	 instruction is increased to 3 cycles.  */
      else if (cost < 3
	       && get_attr_insn_class (dep_insn) == INSN_CLASS_LS_GROUP
	       && get_attr_dfp_comp (insn) == DFP_COMP_YES)
	cost = 3;
      /* The lsw register of a double-precision computation is ready one
	 cycle earlier.  */
      else if (reload_completed
	       && get_attr_dfp_comp (dep_insn) == DFP_COMP_YES
	       && (use_pat = single_set (insn))
	       && ! regno_use_in (REGNO (SET_DEST (single_set (dep_insn))),
				  SET_SRC (use_pat)))
	cost -= 1;

      if (get_attr_any_fp_comp (dep_insn) == ANY_FP_COMP_YES
	  && get_attr_late_fp_use (insn) == LATE_FP_USE_YES)
	cost -= 1;
    }
  /* An anti-dependence penalty of two applies if the first insn is a double
     precision fadd / fsub / fmul.  */
  else if (REG_NOTE_KIND (link) == REG_DEP_ANTI
	   && recog_memoized (dep_insn) >= 0
	   && get_attr_type (dep_insn) == TYPE_DFP_ARITH
	   /* A lot of alleged anti-flow dependences are fake,
	      so check this one is real.  */
	   && flow_dependent_p (dep_insn, insn))
    cost = 2;


  return cost;
}

/* Check if INSN is flow-dependent on DEP_INSN.  Can also be used to check
   if DEP_INSN is anti-flow dependent on INSN.  */
static int
flow_dependent_p (rtx insn, rtx dep_insn)
{
  rtx tmp = PATTERN (insn);

  note_stores (PATTERN (dep_insn), flow_dependent_p_1, &tmp);
  return tmp == NULL_RTX;
}

/* A helper function for flow_dependent_p called through note_stores.  */
static void
flow_dependent_p_1 (rtx x, rtx pat ATTRIBUTE_UNUSED, void *data)
{
  rtx * pinsn = (rtx *) data;

  if (*pinsn && reg_referenced_p (x, *pinsn))
    *pinsn = NULL_RTX;
}

/* For use by sh_allocate_initial_value.  Note that sh.md contains some
   'special function' patterns (type sfunc) that clobber pr, but that
   do not look like function calls to leaf_function_p.  Hence we must
   do this extra check.  */
static int
sh_pr_n_sets (void)
{
  return REG_N_SETS (TARGET_SHMEDIA ? PR_MEDIA_REG : PR_REG);
}

/* Return where to allocate pseudo for a given hard register initial
   value.  */
static rtx
sh_allocate_initial_value (rtx hard_reg)
{
  rtx x;

  if (REGNO (hard_reg) == (TARGET_SHMEDIA ? PR_MEDIA_REG : PR_REG))
    {
      if (current_function_is_leaf
	  && ! sh_pr_n_sets ()
	  && ! (TARGET_SHCOMPACT
		&& ((current_function_args_info.call_cookie
		     & ~ CALL_COOKIE_RET_TRAMP (1))
		    || current_function_has_nonlocal_label)))
	x = hard_reg;
      else
	x = gen_frame_mem (Pmode, return_address_pointer_rtx);
    }
  else
    x = NULL_RTX;

  return x;
}

/* This function returns "2" to indicate dual issue for the SH4
   processor.  To be used by the DFA pipeline description.  */
static int
sh_issue_rate (void)
{
  if (TARGET_SUPERSCALAR)
    return 2;
  else
    return 1;
}

/* Functions for ready queue reordering for sched1.  */

/* Get weight for mode for a set x.  */
static short
find_set_regmode_weight (rtx x, enum machine_mode mode)
{
  if (GET_CODE (x) == CLOBBER && register_operand (SET_DEST (x), mode))
    return 1;
  if (GET_CODE (x) == SET && register_operand (SET_DEST (x), mode))
    {
      if (GET_CODE (SET_DEST (x)) == REG)
	{
	  if (!reg_mentioned_p (SET_DEST (x), SET_SRC (x)))
	    return 1;
	  else
	    return 0;
	}
      return 1;
    }
  return 0;
}

/* Get regmode weight for insn.  */
static short
find_insn_regmode_weight (rtx insn, enum machine_mode mode)
{
  short reg_weight = 0;
  rtx x;

  /* Increment weight for each register born here.  */
  x = PATTERN (insn);
  reg_weight += find_set_regmode_weight (x, mode);
  if (GET_CODE (x) == PARALLEL)
    {
      int j;
      for (j = XVECLEN (x, 0) - 1; j >= 0; j--)
	{
	  x = XVECEXP (PATTERN (insn), 0, j);
	  reg_weight += find_set_regmode_weight (x, mode);
	}
    }
  /* Decrement weight for each register that dies here.  */
  for (x = REG_NOTES (insn); x; x = XEXP (x, 1))
    {
      if (REG_NOTE_KIND (x) == REG_DEAD || REG_NOTE_KIND (x) == REG_UNUSED)
	{
	  rtx note = XEXP (x, 0);
	  if (GET_CODE (note) == REG && GET_MODE (note) == mode)
	    reg_weight--;
	}
    }
  return reg_weight;
}

/* Calculate regmode weights for all insns of a basic block.  */
static void
find_regmode_weight (basic_block b, enum machine_mode mode)
{
  rtx insn, next_tail, head, tail;

  get_ebb_head_tail (b, b, &head, &tail);
  next_tail = NEXT_INSN (tail);

  for (insn = head; insn != next_tail; insn = NEXT_INSN (insn))
    {
      /* Handle register life information.  */
      if (!INSN_P (insn))
	continue;

      if (mode == SFmode)
	INSN_REGMODE_WEIGHT (insn, mode) =
	  find_insn_regmode_weight (insn, mode) + 2 * find_insn_regmode_weight (insn, DFmode);
      else if (mode == SImode)
	INSN_REGMODE_WEIGHT (insn, mode) =
	  find_insn_regmode_weight (insn, mode) + 2 * find_insn_regmode_weight (insn, DImode);
    }
}

/* Comparison function for ready queue sorting.  */
static int
rank_for_reorder (const void *x, const void *y)
{
  rtx tmp = *(const rtx *) y;
  rtx tmp2 = *(const rtx *) x;

  /* The insn in a schedule group should be issued the first.  */
  if (SCHED_GROUP_P (tmp) != SCHED_GROUP_P (tmp2))
    return SCHED_GROUP_P (tmp2) ? 1 : -1;

  /* If insns are equally good, sort by INSN_LUID (original insn order), This
     minimizes instruction movement, thus minimizing sched's effect on
     register pressure.  */
  return INSN_LUID (tmp) - INSN_LUID (tmp2);
}

/* Resort the array A in which only element at index N may be out of order.  */
static void
swap_reorder (rtx *a, int n)
{
  rtx insn = a[n - 1];
  int i = n - 2;

  while (i >= 0 && rank_for_reorder (a + i, &insn) >= 0)
    {
      a[i + 1] = a[i];
      i -= 1;
    }
  a[i + 1] = insn;
}

#define SCHED_REORDER(READY, N_READY)                                	\
  do									\
    {									\
      if ((N_READY) == 2)						\
	swap_reorder (READY, N_READY);					\
      else if ((N_READY) > 2)						\
	qsort (READY, N_READY, sizeof (rtx), rank_for_reorder);		\
    }									\
  while (0)

/* Sort the ready list READY by ascending priority, using the SCHED_REORDER
   macro.  */
static void
ready_reorder (rtx *ready, int nready)
{
  SCHED_REORDER (ready, nready);
}

/* Calculate regmode weights for all insns of all basic block.  */
static void
sh_md_init_global (FILE *dump ATTRIBUTE_UNUSED,
		   int verbose ATTRIBUTE_UNUSED,
		   int old_max_uid)
{
  basic_block b;

  regmode_weight[0] = (short *) xcalloc (old_max_uid, sizeof (short));
  regmode_weight[1] = (short *) xcalloc (old_max_uid, sizeof (short));

  FOR_EACH_BB_REVERSE (b)
  {
    find_regmode_weight (b, SImode);
    find_regmode_weight (b, SFmode);
  }

  CURR_REGMODE_PRESSURE (SImode) = 0;
  CURR_REGMODE_PRESSURE (SFmode) = 0;

}

/* Cleanup.  */
static void
sh_md_finish_global (FILE *dump ATTRIBUTE_UNUSED,
		     int verbose ATTRIBUTE_UNUSED)
{
  if (regmode_weight[0])
    {
      free (regmode_weight[0]);
      regmode_weight[0] = NULL;
    }
  if (regmode_weight[1])
    {
      free (regmode_weight[1]);
      regmode_weight[1] = NULL;
    }
}

/* Cache the can_issue_more so that we can return it from reorder2. Also,
   keep count of register pressures on SImode and SFmode. */
static int
sh_variable_issue (FILE *dump ATTRIBUTE_UNUSED,
		   int sched_verbose ATTRIBUTE_UNUSED,
		   rtx insn,
		   int can_issue_more)
{
  if (GET_CODE (PATTERN (insn)) != USE
      && GET_CODE (PATTERN (insn)) != CLOBBER)
    cached_can_issue_more = can_issue_more - 1;
  else
    cached_can_issue_more = can_issue_more;

  if (reload_completed)
    return cached_can_issue_more;

  CURR_REGMODE_PRESSURE (SImode) += INSN_REGMODE_WEIGHT (insn, SImode);
  CURR_REGMODE_PRESSURE (SFmode) += INSN_REGMODE_WEIGHT (insn, SFmode);

  return cached_can_issue_more;
}

static void
sh_md_init (FILE *dump ATTRIBUTE_UNUSED,
	    int verbose ATTRIBUTE_UNUSED,
	    int veclen ATTRIBUTE_UNUSED)
{
  CURR_REGMODE_PRESSURE (SImode) = 0;
  CURR_REGMODE_PRESSURE (SFmode) = 0;
}

/* Some magic numbers.  */
/* Pressure on register r0 can lead to spill failures. so avoid sched1 for
   functions that already have high pressure on r0. */
#define R0_MAX_LIFE_REGIONS 2
#define R0_MAX_LIVE_LENGTH 12
/* Register Pressure thresholds for SImode and SFmode registers.  */
#define SIMODE_MAX_WEIGHT 5
#define SFMODE_MAX_WEIGHT 10

/* Return true if the pressure is high for MODE.  */
static short
high_pressure (enum machine_mode mode)
{
  /* Pressure on register r0 can lead to spill failures. so avoid sched1 for
     functions that already have high pressure on r0. */
  if ((REG_N_SETS (0) - REG_N_DEATHS (0)) >= R0_MAX_LIFE_REGIONS
      && REG_LIVE_LENGTH (0) >= R0_MAX_LIVE_LENGTH)
    return 1;

  if (mode == SFmode)
    return (CURR_REGMODE_PRESSURE (SFmode) > SFMODE_MAX_WEIGHT);
  else
    return (CURR_REGMODE_PRESSURE (SImode) > SIMODE_MAX_WEIGHT);
}

/* Reorder ready queue if register pressure is high.  */
static int
sh_reorder (FILE *dump ATTRIBUTE_UNUSED,
	    int sched_verbose ATTRIBUTE_UNUSED,
	    rtx *ready,
	    int *n_readyp,
	    int clock_var ATTRIBUTE_UNUSED)
{
  if (reload_completed)
    return sh_issue_rate ();

  if (high_pressure (SFmode) || high_pressure (SImode))
    {
      ready_reorder (ready, *n_readyp);
    }

  return sh_issue_rate ();
}

/* Skip cycles if the current register pressure is high.  */
static int
sh_reorder2 (FILE *dump ATTRIBUTE_UNUSED,
	     int sched_verbose ATTRIBUTE_UNUSED,
	     rtx *ready ATTRIBUTE_UNUSED,
	     int *n_readyp ATTRIBUTE_UNUSED,
	     int clock_var ATTRIBUTE_UNUSED)
{
  if (reload_completed)
    return cached_can_issue_more;

  if (high_pressure(SFmode) || high_pressure (SImode))
    skip_cycles = 1;

  return cached_can_issue_more;
}

/* Skip cycles without sorting the ready queue. This will move insn from
   Q->R. If this is the last cycle we are skipping; allow sorting of ready
   queue by sh_reorder.  */

/* Generally, skipping these many cycles are sufficient for all insns to move
   from Q -> R.  */
#define MAX_SKIPS 8

static int
sh_dfa_new_cycle (FILE *sched_dump ATTRIBUTE_UNUSED,
		  int sched_verbose ATTRIBUTE_UNUSED,
		  rtx insn ATTRIBUTE_UNUSED,
		  int last_clock_var,
		  int clock_var,
		  int *sort_p)
{
  if (reload_completed)
    return 0;

  if (skip_cycles)
    {
      if ((clock_var - last_clock_var) < MAX_SKIPS)
	{
	  *sort_p = 0;
	  return 1;
	}
      /* If this is the last cycle we are skipping, allow reordering of R.  */
      if ((clock_var - last_clock_var) == MAX_SKIPS)
	{
	  *sort_p = 1;
	  return 1;
	}
    }

  skip_cycles = 0;

  return 0;
}

/* SHmedia requires registers for branches, so we can't generate new
   branches past reload.  */
static bool
sh_cannot_modify_jumps_p (void)
{
  return (TARGET_SHMEDIA && (reload_in_progress || reload_completed));
}

static int
sh_target_reg_class (void)
{
  return TARGET_SHMEDIA ? TARGET_REGS : NO_REGS;
}

static bool
sh_optimize_target_register_callee_saved (bool after_prologue_epilogue_gen)
{
  HARD_REG_SET dummy;
  rtx insn;

  if (! shmedia_space_reserved_for_target_registers)
    return 0;
  if (after_prologue_epilogue_gen && ! TARGET_SAVE_ALL_TARGET_REGS)
    return 0;
  if (calc_live_regs (&dummy) >= 6 * 8)
    return 1;
  /* This is a borderline case.  See if we got a nested loop, or a loop
     with a call, or with more than 4 labels inside.  */
  for (insn = get_insns(); insn; insn = NEXT_INSN (insn))
    {
      if (GET_CODE (insn) == NOTE
	  && NOTE_LINE_NUMBER (insn) == NOTE_INSN_LOOP_BEG)
	{
	  int labels = 0;

	  do
	    {
	      insn = NEXT_INSN (insn);
	      if ((GET_CODE (insn) == NOTE
		   && NOTE_LINE_NUMBER (insn) == NOTE_INSN_LOOP_BEG)
		  || GET_CODE (insn) == CALL_INSN
		  || (GET_CODE (insn) == CODE_LABEL && ++labels > 4))
		return 1;
	    }
	  while (GET_CODE (insn) != NOTE
		 || NOTE_LINE_NUMBER (insn) != NOTE_INSN_LOOP_END);
	}
    }
  return 0;
}

static bool
sh_ms_bitfield_layout_p (tree record_type ATTRIBUTE_UNUSED)
{
  return (TARGET_SH5 || TARGET_HITACHI || sh_attr_renesas_p (record_type));
}

/*
   On the SH1..SH4, the trampoline looks like
   2 0002 D202     	   	mov.l	l2,r2
   1 0000 D301     		mov.l	l1,r3
   3 0004 422B     		jmp	@r2
   4 0006 0009     		nop
   5 0008 00000000 	l1:  	.long   area
   6 000c 00000000 	l2:	.long   function

   SH5 (compact) uses r1 instead of r3 for the static chain.  */


/* Emit RTL insns to initialize the variable parts of a trampoline.
   FNADDR is an RTX for the address of the function's pure code.
   CXT is an RTX for the static chain value for the function.  */

void
sh_initialize_trampoline (rtx tramp, rtx fnaddr, rtx cxt)
{
  rtx tramp_mem = gen_frame_mem (BLKmode, tramp);

  if (TARGET_SHMEDIA64)
    {
      rtx tramp_templ;
      int fixed_len;

      rtx movi1 = GEN_INT (0xcc000010);
      rtx shori1 = GEN_INT (0xc8000010);
      rtx src, dst;

      /* The following trampoline works within a +- 128 KB range for cxt:
	 ptb/u cxt,tr1; movi fnaddr >> 48,r0; shori fnaddr >> 32,r0;
         shori fnaddr >> 16,r0; shori fnaddr,r0; ptabs/l r0,tr0
         gettr tr1,r1; blink tr0,r63  */
      /* Address rounding makes it hard to compute the exact bounds of the
	 offset for this trampoline, but we have a rather generous offset
	 range, so frame_offset should do fine as an upper bound.  */
      if (cxt == virtual_stack_vars_rtx && frame_offset < 0x20000)
	{
	  /* ??? could optimize this trampoline initialization
	     by writing DImode words with two insns each.  */
	  rtx mask = force_reg (DImode, GEN_INT (0x3fffc00));
	  rtx insn = gen_rtx_MINUS (DImode, cxt, tramp);
	  insn = gen_rtx_ASHIFT (DImode, insn, GEN_INT (10-2));
	  insn = gen_rtx_AND (DImode, insn, mask);
	  /* Or in ptb/u .,tr1 pattern */
	  insn = gen_rtx_IOR (DImode, insn, gen_int_mode (0xec000010, SImode));
	  insn = force_operand (insn, NULL_RTX);
	  insn = gen_lowpart (SImode, insn);
	  emit_move_insn (change_address (tramp_mem, SImode, NULL_RTX), insn);
	  insn = gen_rtx_LSHIFTRT (DImode, fnaddr, GEN_INT (38));
	  insn = gen_rtx_AND (DImode, insn, mask);
	  insn = force_operand (gen_rtx_IOR (DImode, movi1, insn), NULL_RTX);
	  insn = gen_lowpart (SImode, insn);
	  emit_move_insn (adjust_address (tramp_mem, SImode, 4), insn);
	  insn = gen_rtx_LSHIFTRT (DImode, fnaddr, GEN_INT (22));
	  insn = gen_rtx_AND (DImode, insn, mask);
	  insn = force_operand (gen_rtx_IOR (DImode, shori1, insn), NULL_RTX);
	  insn = gen_lowpart (SImode, insn);
	  emit_move_insn (adjust_address (tramp_mem, SImode, 8), insn);
	  insn = gen_rtx_LSHIFTRT (DImode, fnaddr, GEN_INT (6));
	  insn = gen_rtx_AND (DImode, insn, mask);
	  insn = force_operand (gen_rtx_IOR (DImode, shori1, insn), NULL_RTX);
	  insn = gen_lowpart (SImode, insn);
	  emit_move_insn (adjust_address (tramp_mem, SImode, 12), insn);
	  insn = gen_rtx_ASHIFT (DImode, fnaddr, GEN_INT (10));
	  insn = gen_rtx_AND (DImode, insn, mask);
	  insn = force_operand (gen_rtx_IOR (DImode, shori1, insn), NULL_RTX);
	  insn = gen_lowpart (SImode, insn);
	  emit_move_insn (adjust_address (tramp_mem, SImode, 16), insn);
	  emit_move_insn (adjust_address (tramp_mem, SImode, 20),
			  GEN_INT (0x6bf10600));
	  emit_move_insn (adjust_address (tramp_mem, SImode, 24),
			  GEN_INT (0x4415fc10));
	  emit_move_insn (adjust_address (tramp_mem, SImode, 28),
			  GEN_INT (0x4401fff0));
	  emit_insn (gen_ic_invalidate_line (tramp));
	  return;
	}
      tramp_templ = gen_rtx_SYMBOL_REF (Pmode,"__GCC_nested_trampoline");
      fixed_len = TRAMPOLINE_SIZE - 2 * GET_MODE_SIZE (Pmode);

      tramp_templ = gen_datalabel_ref (tramp_templ);
      dst = tramp_mem;
      src = gen_const_mem (BLKmode, tramp_templ);
      set_mem_align (dst, 256);
      set_mem_align (src, 64);
      emit_block_move (dst, src, GEN_INT (fixed_len), BLOCK_OP_NORMAL);

      emit_move_insn (adjust_address (tramp_mem, Pmode, fixed_len), fnaddr);
      emit_move_insn (adjust_address (tramp_mem, Pmode,
				      fixed_len + GET_MODE_SIZE (Pmode)),
		      cxt);
      emit_insn (gen_ic_invalidate_line (tramp));
      return;
    }
  else if (TARGET_SHMEDIA)
    {
      /* movi fnaddr >> 16,r1; shori fnaddr,r1; ptabs/l r1,tr0
         movi cxt >> 16,r1; shori cxt,r1; blink tr0,r63  */
      rtx quad0 = gen_reg_rtx (DImode), cxtload = gen_reg_rtx (DImode);
      rtx quad1 = gen_reg_rtx (DImode), quad2 = gen_reg_rtx (DImode);
      /* movi 0,r1: 0xcc000010 shori 0,r1: c8000010  concatenated,
	 rotated 10 right, and higher 16 bit of every 32 selected.  */
      rtx movishori
	= force_reg (V2HImode, (simplify_gen_subreg
				(V2HImode, GEN_INT (0x4330432), SImode, 0)));
      rtx ptabs = force_reg (DImode, GEN_INT (0x6bf10600));
      rtx blink = force_reg (DImode, GEN_INT (0x4401fff0));

      tramp = force_reg (Pmode, tramp);
      fnaddr = force_reg (SImode, fnaddr);
      cxt = force_reg (SImode, cxt);
      emit_insn (gen_mshflo_w_x (gen_rtx_SUBREG (V4HImode, quad0, 0),
				 gen_rtx_SUBREG (V2HImode, fnaddr, 0),
				 movishori));
      emit_insn (gen_rotrdi3_mextr (quad0, quad0,
				    GEN_INT (TARGET_LITTLE_ENDIAN ? 24 : 56)));
      emit_insn (gen_ashldi3_media (quad0, quad0, const2_rtx));
      emit_move_insn (change_address (tramp_mem, DImode, NULL_RTX), quad0);
      emit_insn (gen_mshflo_w_x (gen_rtx_SUBREG (V4HImode, cxtload, 0),
				 gen_rtx_SUBREG (V2HImode, cxt, 0),
				 movishori));
      emit_insn (gen_rotrdi3_mextr (cxtload, cxtload,
				    GEN_INT (TARGET_LITTLE_ENDIAN ? 24 : 56)));
      emit_insn (gen_ashldi3_media (cxtload, cxtload, const2_rtx));
      if (TARGET_LITTLE_ENDIAN)
	{
	  emit_insn (gen_mshflo_l_di (quad1, ptabs, cxtload));
	  emit_insn (gen_mextr4 (quad2, cxtload, blink));
	}
      else
	{
	  emit_insn (gen_mextr4 (quad1, cxtload, ptabs));
	  emit_insn (gen_mshflo_l_di (quad2, blink, cxtload));
	}
      emit_move_insn (adjust_address (tramp_mem, DImode, 8), quad1);
      emit_move_insn (adjust_address (tramp_mem, DImode, 16), quad2);
      emit_insn (gen_ic_invalidate_line (tramp));
      return;
    }
  else if (TARGET_SHCOMPACT)
    {
      emit_insn (gen_initialize_trampoline (tramp, cxt, fnaddr));
      return;
    }
  emit_move_insn (change_address (tramp_mem, SImode, NULL_RTX),
		  gen_int_mode (TARGET_LITTLE_ENDIAN ? 0xd301d202 : 0xd202d301,
				SImode));
  emit_move_insn (adjust_address (tramp_mem, SImode, 4),
		  gen_int_mode (TARGET_LITTLE_ENDIAN ? 0x0009422b : 0x422b0009,
				SImode));
  emit_move_insn (adjust_address (tramp_mem, SImode, 8), cxt);
  emit_move_insn (adjust_address (tramp_mem, SImode, 12), fnaddr);
  if (TARGET_HARVARD)
    {
      if (TARGET_USERMODE)
	emit_library_call (function_symbol (NULL, "__ic_invalidate",
					    FUNCTION_ORDINARY),
			   0, VOIDmode, 1, tramp, SImode);
      else
	emit_insn (gen_ic_invalidate_line (tramp));
    }
}

/* FIXME: This is overly conservative.  A SHcompact function that
   receives arguments ``by reference'' will have them stored in its
   own stack frame, so it must not pass pointers or references to
   these arguments to other functions by means of sibling calls.  */
/* If PIC, we cannot make sibling calls to global functions
   because the PLT requires r12 to be live.  */
static bool
sh_function_ok_for_sibcall (tree decl, tree exp ATTRIBUTE_UNUSED)
{
  return (1
	  && (! TARGET_SHCOMPACT
	      || current_function_args_info.stack_regs == 0)
	  && ! sh_cfun_interrupt_handler_p ()
	  && (! flag_pic
	      || (decl && ! TREE_PUBLIC (decl))
	      || (decl && DECL_VISIBILITY (decl) != VISIBILITY_DEFAULT)));
}

/* Machine specific built-in functions.  */

struct builtin_description
{
  const enum insn_code icode;
  const char *const name;
  int signature;
};

/* describe number and signedness of arguments; arg[0] == result
   (1: unsigned, 2: signed, 4: don't care, 8: pointer 0: no argument */
/* 9: 64 bit pointer, 10: 32 bit pointer */
static const char signature_args[][4] =
{
#define SH_BLTIN_V2SI2 0
  { 4, 4 },
#define SH_BLTIN_V4HI2 1
  { 4, 4 },
#define SH_BLTIN_V2SI3 2
  { 4, 4, 4 },
#define SH_BLTIN_V4HI3 3
  { 4, 4, 4 },
#define SH_BLTIN_V8QI3 4
  { 4, 4, 4 },
#define SH_BLTIN_MAC_HISI 5
  { 1, 4, 4, 1 },
#define SH_BLTIN_SH_HI 6
  { 4, 4, 1 },
#define SH_BLTIN_SH_SI 7
  { 4, 4, 1 },
#define SH_BLTIN_V4HI2V2SI 8
  { 4, 4, 4 },
#define SH_BLTIN_V4HI2V8QI 9
  { 4, 4, 4 },
#define SH_BLTIN_SISF 10
  { 4, 2 },
#define SH_BLTIN_LDUA_L 11
  { 2, 10 },
#define SH_BLTIN_LDUA_Q 12
  { 1, 10 },
#define SH_BLTIN_STUA_L 13
  { 0, 10, 2 },
#define SH_BLTIN_STUA_Q 14
  { 0, 10, 1 },
#define SH_BLTIN_LDUA_L64 15
  { 2, 9 },
#define SH_BLTIN_LDUA_Q64 16
  { 1, 9 },
#define SH_BLTIN_STUA_L64 17
  { 0, 9, 2 },
#define SH_BLTIN_STUA_Q64 18
  { 0, 9, 1 },
#define SH_BLTIN_NUM_SHARED_SIGNATURES 19
#define SH_BLTIN_2 19
#define SH_BLTIN_SU 19
  { 1, 2 },
#define SH_BLTIN_3 20
#define SH_BLTIN_SUS 20
  { 2, 2, 1 },
#define SH_BLTIN_PSSV 21
  { 0, 8, 2, 2 },
#define SH_BLTIN_XXUU 22
#define SH_BLTIN_UUUU 22
  { 1, 1, 1, 1 },
#define SH_BLTIN_PV 23
  { 0, 8 },
};
/* mcmv: operands considered unsigned.  */
/* mmulsum_wq, msad_ubq: result considered unsigned long long.  */
/* mperm: control value considered unsigned int.  */
/* mshalds, mshard, mshards, mshlld, mshlrd: shift count is unsigned int.  */
/* mshards_q: returns signed short.  */
/* nsb: takes long long arg, returns unsigned char.  */
static const struct builtin_description bdesc[] =
{
  { CODE_FOR_absv2si2,	"__builtin_absv2si2", SH_BLTIN_V2SI2 },
  { CODE_FOR_absv4hi2,	"__builtin_absv4hi2", SH_BLTIN_V4HI2 },
  { CODE_FOR_addv2si3,	"__builtin_addv2si3", SH_BLTIN_V2SI3 },
  { CODE_FOR_addv4hi3,	"__builtin_addv4hi3", SH_BLTIN_V4HI3 },
  { CODE_FOR_ssaddv2si3,"__builtin_ssaddv2si3", SH_BLTIN_V2SI3 },
  { CODE_FOR_usaddv8qi3,"__builtin_usaddv8qi3", SH_BLTIN_V8QI3 },
  { CODE_FOR_ssaddv4hi3,"__builtin_ssaddv4hi3", SH_BLTIN_V4HI3 },
  { CODE_FOR_alloco_i,	"__builtin_sh_media_ALLOCO", SH_BLTIN_PV },
  { CODE_FOR_negcmpeqv8qi,"__builtin_sh_media_MCMPEQ_B", SH_BLTIN_V8QI3 },
  { CODE_FOR_negcmpeqv2si,"__builtin_sh_media_MCMPEQ_L", SH_BLTIN_V2SI3 },
  { CODE_FOR_negcmpeqv4hi,"__builtin_sh_media_MCMPEQ_W", SH_BLTIN_V4HI3 },
  { CODE_FOR_negcmpgtuv8qi,"__builtin_sh_media_MCMPGT_UB", SH_BLTIN_V8QI3 },
  { CODE_FOR_negcmpgtv2si,"__builtin_sh_media_MCMPGT_L", SH_BLTIN_V2SI3 },
  { CODE_FOR_negcmpgtv4hi,"__builtin_sh_media_MCMPGT_W", SH_BLTIN_V4HI3 },
  { CODE_FOR_mcmv,	"__builtin_sh_media_MCMV", SH_BLTIN_UUUU },
  { CODE_FOR_mcnvs_lw,	"__builtin_sh_media_MCNVS_LW", SH_BLTIN_3 },
  { CODE_FOR_mcnvs_wb,	"__builtin_sh_media_MCNVS_WB", SH_BLTIN_V4HI2V8QI },
  { CODE_FOR_mcnvs_wub,	"__builtin_sh_media_MCNVS_WUB", SH_BLTIN_V4HI2V8QI },
  { CODE_FOR_mextr1,	"__builtin_sh_media_MEXTR1", SH_BLTIN_V8QI3 },
  { CODE_FOR_mextr2,	"__builtin_sh_media_MEXTR2", SH_BLTIN_V8QI3 },
  { CODE_FOR_mextr3,	"__builtin_sh_media_MEXTR3", SH_BLTIN_V8QI3 },
  { CODE_FOR_mextr4,	"__builtin_sh_media_MEXTR4", SH_BLTIN_V8QI3 },
  { CODE_FOR_mextr5,	"__builtin_sh_media_MEXTR5", SH_BLTIN_V8QI3 },
  { CODE_FOR_mextr6,	"__builtin_sh_media_MEXTR6", SH_BLTIN_V8QI3 },
  { CODE_FOR_mextr7,	"__builtin_sh_media_MEXTR7", SH_BLTIN_V8QI3 },
  { CODE_FOR_mmacfx_wl,	"__builtin_sh_media_MMACFX_WL", SH_BLTIN_MAC_HISI },
  { CODE_FOR_mmacnfx_wl,"__builtin_sh_media_MMACNFX_WL", SH_BLTIN_MAC_HISI },
  { CODE_FOR_mulv2si3,	"__builtin_mulv2si3", SH_BLTIN_V2SI3, },
  { CODE_FOR_mulv4hi3,	"__builtin_mulv4hi3", SH_BLTIN_V4HI3 },
  { CODE_FOR_mmulfx_l,	"__builtin_sh_media_MMULFX_L", SH_BLTIN_V2SI3 },
  { CODE_FOR_mmulfx_w,	"__builtin_sh_media_MMULFX_W", SH_BLTIN_V4HI3 },
  { CODE_FOR_mmulfxrp_w,"__builtin_sh_media_MMULFXRP_W", SH_BLTIN_V4HI3 },
  { CODE_FOR_mmulhi_wl,	"__builtin_sh_media_MMULHI_WL", SH_BLTIN_V4HI2V2SI },
  { CODE_FOR_mmullo_wl,	"__builtin_sh_media_MMULLO_WL", SH_BLTIN_V4HI2V2SI },
  { CODE_FOR_mmulsum_wq,"__builtin_sh_media_MMULSUM_WQ", SH_BLTIN_XXUU },
  { CODE_FOR_mperm_w,	"__builtin_sh_media_MPERM_W", SH_BLTIN_SH_HI },
  { CODE_FOR_msad_ubq,	"__builtin_sh_media_MSAD_UBQ", SH_BLTIN_XXUU },
  { CODE_FOR_mshalds_l,	"__builtin_sh_media_MSHALDS_L", SH_BLTIN_SH_SI },
  { CODE_FOR_mshalds_w,	"__builtin_sh_media_MSHALDS_W", SH_BLTIN_SH_HI },
  { CODE_FOR_ashrv2si3,	"__builtin_ashrv2si3", SH_BLTIN_SH_SI },
  { CODE_FOR_ashrv4hi3,	"__builtin_ashrv4hi3", SH_BLTIN_SH_HI },
  { CODE_FOR_mshards_q,	"__builtin_sh_media_MSHARDS_Q", SH_BLTIN_SUS },
  { CODE_FOR_mshfhi_b,	"__builtin_sh_media_MSHFHI_B", SH_BLTIN_V8QI3 },
  { CODE_FOR_mshfhi_l,	"__builtin_sh_media_MSHFHI_L", SH_BLTIN_V2SI3 },
  { CODE_FOR_mshfhi_w,	"__builtin_sh_media_MSHFHI_W", SH_BLTIN_V4HI3 },
  { CODE_FOR_mshflo_b,	"__builtin_sh_media_MSHFLO_B", SH_BLTIN_V8QI3 },
  { CODE_FOR_mshflo_l,	"__builtin_sh_media_MSHFLO_L", SH_BLTIN_V2SI3 },
  { CODE_FOR_mshflo_w,	"__builtin_sh_media_MSHFLO_W", SH_BLTIN_V4HI3 },
  { CODE_FOR_ashlv2si3,	"__builtin_ashlv2si3", SH_BLTIN_SH_SI },
  { CODE_FOR_ashlv4hi3,	"__builtin_ashlv4hi3", SH_BLTIN_SH_HI },
  { CODE_FOR_lshrv2si3,	"__builtin_lshrv2si3", SH_BLTIN_SH_SI },
  { CODE_FOR_lshrv4hi3,	"__builtin_lshrv4hi3", SH_BLTIN_SH_HI },
  { CODE_FOR_subv2si3,	"__builtin_subv2si3", SH_BLTIN_V2SI3 },
  { CODE_FOR_subv4hi3,	"__builtin_subv4hi3", SH_BLTIN_V4HI3 },
  { CODE_FOR_sssubv2si3,"__builtin_sssubv2si3", SH_BLTIN_V2SI3 },
  { CODE_FOR_ussubv8qi3,"__builtin_ussubv8qi3", SH_BLTIN_V8QI3 },
  { CODE_FOR_sssubv4hi3,"__builtin_sssubv4hi3", SH_BLTIN_V4HI3 },
  { CODE_FOR_fcosa_s,	"__builtin_sh_media_FCOSA_S", SH_BLTIN_SISF },
  { CODE_FOR_fsina_s,	"__builtin_sh_media_FSINA_S", SH_BLTIN_SISF },
  { CODE_FOR_fipr,	"__builtin_sh_media_FIPR_S", SH_BLTIN_3 },
  { CODE_FOR_ftrv,	"__builtin_sh_media_FTRV_S", SH_BLTIN_3 },
  { CODE_FOR_mac_media,	"__builtin_sh_media_FMAC_S", SH_BLTIN_3 },
  { CODE_FOR_sqrtdf2,	"__builtin_sh_media_FSQRT_D", SH_BLTIN_2 },
  { CODE_FOR_sqrtsf2,	"__builtin_sh_media_FSQRT_S", SH_BLTIN_2 },
  { CODE_FOR_fsrra_s,	"__builtin_sh_media_FSRRA_S", SH_BLTIN_2 },
  { CODE_FOR_ldhi_l,	"__builtin_sh_media_LDHI_L", SH_BLTIN_LDUA_L },
  { CODE_FOR_ldhi_q,	"__builtin_sh_media_LDHI_Q", SH_BLTIN_LDUA_Q },
  { CODE_FOR_ldlo_l,	"__builtin_sh_media_LDLO_L", SH_BLTIN_LDUA_L },
  { CODE_FOR_ldlo_q,	"__builtin_sh_media_LDLO_Q", SH_BLTIN_LDUA_Q },
  { CODE_FOR_sthi_l,	"__builtin_sh_media_STHI_L", SH_BLTIN_STUA_L },
  { CODE_FOR_sthi_q,	"__builtin_sh_media_STHI_Q", SH_BLTIN_STUA_Q },
  { CODE_FOR_stlo_l,	"__builtin_sh_media_STLO_L", SH_BLTIN_STUA_L },
  { CODE_FOR_stlo_q,	"__builtin_sh_media_STLO_Q", SH_BLTIN_STUA_Q },
  { CODE_FOR_ldhi_l64,	"__builtin_sh_media_LDHI_L", SH_BLTIN_LDUA_L64 },
  { CODE_FOR_ldhi_q64,	"__builtin_sh_media_LDHI_Q", SH_BLTIN_LDUA_Q64 },
  { CODE_FOR_ldlo_l64,	"__builtin_sh_media_LDLO_L", SH_BLTIN_LDUA_L64 },
  { CODE_FOR_ldlo_q64,	"__builtin_sh_media_LDLO_Q", SH_BLTIN_LDUA_Q64 },
  { CODE_FOR_sthi_l64,	"__builtin_sh_media_STHI_L", SH_BLTIN_STUA_L64 },
  { CODE_FOR_sthi_q64,	"__builtin_sh_media_STHI_Q", SH_BLTIN_STUA_Q64 },
  { CODE_FOR_stlo_l64,	"__builtin_sh_media_STLO_L", SH_BLTIN_STUA_L64 },
  { CODE_FOR_stlo_q64,	"__builtin_sh_media_STLO_Q", SH_BLTIN_STUA_Q64 },
  { CODE_FOR_nsb,	"__builtin_sh_media_NSB", SH_BLTIN_SU },
  { CODE_FOR_byterev,	"__builtin_sh_media_BYTEREV", SH_BLTIN_2 },
  { CODE_FOR_prefetch,	"__builtin_sh_media_PREFO", SH_BLTIN_PSSV },
};

static void
sh_media_init_builtins (void)
{
  tree shared[SH_BLTIN_NUM_SHARED_SIGNATURES];
  const struct builtin_description *d;

  memset (shared, 0, sizeof shared);
  for (d = bdesc; d - bdesc < (int) ARRAY_SIZE (bdesc); d++)
    {
      tree type, arg_type = 0;
      int signature = d->signature;
      int i;

      if (signature < SH_BLTIN_NUM_SHARED_SIGNATURES && shared[signature])
	type = shared[signature];
      else
	{
	  int has_result = signature_args[signature][0] != 0;

	  if ((signature_args[signature][1] & 8)
	      && (((signature_args[signature][1] & 1) && TARGET_SHMEDIA32)
		  || ((signature_args[signature][1] & 2) && TARGET_SHMEDIA64)))
	    continue;
	  if (! TARGET_FPU_ANY
	      && FLOAT_MODE_P (insn_data[d->icode].operand[0].mode))
	    continue;
	  type = void_list_node;
	  for (i = 3; ; i--)
	    {
	      int arg = signature_args[signature][i];
	      int opno = i - 1 + has_result;

	      if (arg & 8)
		arg_type = ptr_type_node;
	      else if (arg)
		arg_type = (*lang_hooks.types.type_for_mode)
		  (insn_data[d->icode].operand[opno].mode,
		   (arg & 1));
	      else if (i)
		continue;
	      else
		arg_type = void_type_node;
	      if (i == 0)
		break;
	      type = tree_cons (NULL_TREE, arg_type, type);
	    }
	  type = build_function_type (arg_type, type);
	  if (signature < SH_BLTIN_NUM_SHARED_SIGNATURES)
	    shared[signature] = type;
	}
      lang_hooks.builtin_function (d->name, type, d - bdesc, BUILT_IN_MD,
				   NULL, NULL_TREE);
    }
}

/* Implements target hook vector_mode_supported_p.  */
bool
sh_vector_mode_supported_p (enum machine_mode mode)
{
  if (TARGET_FPU_ANY
      && ((mode == V2SFmode)
	  || (mode == V4SFmode)
	  || (mode == V16SFmode)))
    return true;

  else if (TARGET_SHMEDIA
	   && ((mode == V8QImode)
	       || (mode == V2HImode)
	       || (mode == V4HImode)
	       || (mode == V2SImode)))
    return true;

  return false;
}

/* Implements target hook dwarf_calling_convention.  Return an enum
   of dwarf_calling_convention.  */
int
sh_dwarf_calling_convention (tree func)
{
  if (sh_attr_renesas_p (func))
    return DW_CC_GNU_renesas_sh;

  return DW_CC_normal;
}

static void
sh_init_builtins (void)
{
  if (TARGET_SHMEDIA)
    sh_media_init_builtins ();
}

/* Expand an expression EXP that calls a built-in function,
   with result going to TARGET if that's convenient
   (and in mode MODE if that's convenient).
   SUBTARGET may be used as the target for computing one of EXP's operands.
   IGNORE is nonzero if the value is to be ignored.  */

static rtx
sh_expand_builtin (tree exp, rtx target, rtx subtarget ATTRIBUTE_UNUSED,
		   enum machine_mode mode ATTRIBUTE_UNUSED, int ignore)
{
  tree fndecl = TREE_OPERAND (TREE_OPERAND (exp, 0), 0);
  tree arglist = TREE_OPERAND (exp, 1);
  unsigned int fcode = DECL_FUNCTION_CODE (fndecl);
  const struct builtin_description *d = &bdesc[fcode];
  enum insn_code icode = d->icode;
  int signature = d->signature;
  enum machine_mode tmode = VOIDmode;
  int nop = 0, i;
  rtx op[4];
  rtx pat = 0;

  if (signature_args[signature][0])
    {
      if (ignore)
	return 0;

      tmode = insn_data[icode].operand[0].mode;
      if (! target
	  || GET_MODE (target) != tmode
	  || ! (*insn_data[icode].operand[0].predicate) (target, tmode))
	target = gen_reg_rtx (tmode);
      op[nop++] = target;
    }
  else
    target = 0;

  for (i = 1; i <= 3; i++, nop++)
    {
      tree arg;
      enum machine_mode opmode, argmode;
      tree optype;

      if (! signature_args[signature][i])
	break;
      arg = TREE_VALUE (arglist);
      if (arg == error_mark_node)
	return const0_rtx;
      arglist = TREE_CHAIN (arglist);
      if (signature_args[signature][i] & 8)
	{
	  opmode = ptr_mode;
	  optype = ptr_type_node;
	}
      else
	{
	  opmode = insn_data[icode].operand[nop].mode;
	  optype = (*lang_hooks.types.type_for_mode) (opmode, 0);
	}
      argmode = TYPE_MODE (TREE_TYPE (arg));
      if (argmode != opmode)
	arg = build1 (NOP_EXPR, optype, arg);
      op[nop] = expand_expr (arg, NULL_RTX, opmode, 0);
      if (! (*insn_data[icode].operand[nop].predicate) (op[nop], opmode))
	op[nop] = copy_to_mode_reg (opmode, op[nop]);
    }

  switch (nop)
    {
    case 1:
      pat = (*insn_data[d->icode].genfun) (op[0]);
      break;
    case 2:
      pat = (*insn_data[d->icode].genfun) (op[0], op[1]);
      break;
    case 3:
      pat = (*insn_data[d->icode].genfun) (op[0], op[1], op[2]);
      break;
    case 4:
      pat = (*insn_data[d->icode].genfun) (op[0], op[1], op[2], op[3]);
      break;
    default:
      gcc_unreachable ();
    }
  if (! pat)
    return 0;
  emit_insn (pat);
  return target;
}

void
sh_expand_unop_v2sf (enum rtx_code code, rtx op0, rtx op1)
{
  rtx sel0 = const0_rtx;
  rtx sel1 = const1_rtx;
  rtx (*fn) (rtx, rtx, rtx, rtx, rtx) = gen_unary_sf_op;
  rtx op = gen_rtx_fmt_e (code, SFmode, op1);

  emit_insn ((*fn) (op0, op1, op, sel0, sel0));
  emit_insn ((*fn) (op0, op1, op, sel1, sel1));
}

void
sh_expand_binop_v2sf (enum rtx_code code, rtx op0, rtx op1, rtx op2)
{
  rtx sel0 = const0_rtx;
  rtx sel1 = const1_rtx;
  rtx (*fn) (rtx, rtx, rtx, rtx, rtx, rtx, rtx, rtx)
    = gen_binary_sf_op;
  rtx op = gen_rtx_fmt_ee (code, SFmode, op1, op2);

  emit_insn ((*fn) (op0, op1, op2, op, sel0, sel0, sel0, sel1));
  emit_insn ((*fn) (op0, op1, op2, op, sel1, sel1, sel1, sel0));
}

/* Return the class of registers for which a mode change from FROM to TO
   is invalid.  */
bool
sh_cannot_change_mode_class (enum machine_mode from, enum machine_mode to,
			     enum reg_class class)
{
  /* We want to enable the use of SUBREGs as a means to
     VEC_SELECT a single element of a vector.  */
  if (to == SFmode && VECTOR_MODE_P (from) && GET_MODE_INNER (from) == SFmode)
    return (reg_classes_intersect_p (GENERAL_REGS, class));

  if (GET_MODE_SIZE (from) != GET_MODE_SIZE (to))
    {
      if (TARGET_LITTLE_ENDIAN)
	{
	  if (GET_MODE_SIZE (to) < 8 || GET_MODE_SIZE (from) < 8)
	    return reg_classes_intersect_p (DF_REGS, class);
	}
      else
	{
	  if (GET_MODE_SIZE (from) < 8)
	    return reg_classes_intersect_p (DF_HI_REGS, class);
	}
    }
  return 0;
}


/* If ADDRESS refers to a CODE_LABEL, add NUSES to the number of times
   that label is used.  */

void
sh_mark_label (rtx address, int nuses)
{
  if (GOTOFF_P (address))
    {
      /* Extract the label or symbol.  */
      address = XEXP (address, 0);
      if (GET_CODE (address) == PLUS)
	address = XEXP (address, 0);
      address = XVECEXP (address, 0, 0);
    }
  if (GET_CODE (address) == LABEL_REF
      && GET_CODE (XEXP (address, 0)) == CODE_LABEL)
    LABEL_NUSES (XEXP (address, 0)) += nuses;
}

/* Compute extra cost of moving data between one register class
   and another.  */

/* If SECONDARY*_RELOAD_CLASS says something about the src/dst pair, regclass
   uses this information.  Hence, the general register <-> floating point
   register information here is not used for SFmode.  */

int
sh_register_move_cost (enum machine_mode mode,
		       enum reg_class srcclass, enum reg_class dstclass)
{
  if (dstclass == T_REGS || dstclass == PR_REGS)
    return 10;

  if (dstclass == MAC_REGS && srcclass == MAC_REGS)
    return 4;

  if (mode == SImode && ! TARGET_SHMEDIA && TARGET_FMOVD
      && REGCLASS_HAS_FP_REG (srcclass)
      && REGCLASS_HAS_FP_REG (dstclass))
    return 4;

  if (REGCLASS_HAS_FP_REG (dstclass) && srcclass == T_REGS)
    return ((TARGET_HARD_SH4 && !optimize_size) ? 10 : 7);

  if ((REGCLASS_HAS_FP_REG (dstclass) && srcclass == MAC_REGS)
      || (dstclass == MAC_REGS && REGCLASS_HAS_FP_REG (srcclass)))
    return 9;

  if ((REGCLASS_HAS_FP_REG (dstclass)
       && REGCLASS_HAS_GENERAL_REG (srcclass))
      || (REGCLASS_HAS_GENERAL_REG (dstclass)
	  && REGCLASS_HAS_FP_REG (srcclass)))
    return ((TARGET_SHMEDIA ? 4 : TARGET_FMOVD ? 8 : 12)
	    * ((GET_MODE_SIZE (mode) + 7) / 8U));

  if ((dstclass == FPUL_REGS
       && REGCLASS_HAS_GENERAL_REG (srcclass))
      || (srcclass == FPUL_REGS
	  && REGCLASS_HAS_GENERAL_REG (dstclass)))
    return 5;

  if ((dstclass == FPUL_REGS
       && (srcclass == PR_REGS || srcclass == MAC_REGS || srcclass == T_REGS))
      || (srcclass == FPUL_REGS
	  && (dstclass == PR_REGS || dstclass == MAC_REGS)))
    return 7;

  if ((srcclass == TARGET_REGS && ! REGCLASS_HAS_GENERAL_REG (dstclass))
      || ((dstclass) == TARGET_REGS && ! REGCLASS_HAS_GENERAL_REG (srcclass)))
    return 20;

  /* ??? ptabs faults on (value & 0x3) == 0x3  */
  if (TARGET_SHMEDIA
      && ((srcclass) == TARGET_REGS || (srcclass) == SIBCALL_REGS))
    {
      if (sh_gettrcost >= 0)
	return sh_gettrcost;
      else if (!TARGET_PT_FIXED)
	return 100;
    }

  if ((srcclass == FPSCR_REGS && ! REGCLASS_HAS_GENERAL_REG (dstclass))
      || (dstclass == FPSCR_REGS && ! REGCLASS_HAS_GENERAL_REG (srcclass)))
  return 4;

  if (TARGET_SHMEDIA
      || (TARGET_FMOVD
	  && ! REGCLASS_HAS_GENERAL_REG (srcclass)
	  && ! REGCLASS_HAS_GENERAL_REG (dstclass)))
    return 2 * ((GET_MODE_SIZE (mode) + 7) / 8U);

  return 2 * ((GET_MODE_SIZE (mode) + 3) / 4U);
}

static rtx emit_load_ptr (rtx, rtx);

static rtx
emit_load_ptr (rtx reg, rtx addr)
{
  rtx mem = gen_const_mem (ptr_mode, addr);

  if (Pmode != ptr_mode)
    mem = gen_rtx_SIGN_EXTEND (Pmode, mem);
  return emit_move_insn (reg, mem);
}

static void
sh_output_mi_thunk (FILE *file, tree thunk_fndecl ATTRIBUTE_UNUSED,
		    HOST_WIDE_INT delta, HOST_WIDE_INT vcall_offset,
		    tree function)
{
  CUMULATIVE_ARGS cum;
  int structure_value_byref = 0;
  rtx this, this_value, sibcall, insns, funexp;
  tree funtype = TREE_TYPE (function);
  int simple_add = CONST_OK_FOR_ADD (delta);
  int did_load = 0;
  rtx scratch0, scratch1, scratch2;
  unsigned i;

  reload_completed = 1;
  epilogue_completed = 1;
  no_new_pseudos = 1;
  current_function_uses_only_leaf_regs = 1;
  reset_block_changes ();

  emit_note (NOTE_INSN_PROLOGUE_END);

  /* Find the "this" pointer.  We have such a wide range of ABIs for the
     SH that it's best to do this completely machine independently.
     "this" is passed as first argument, unless a structure return pointer
     comes first, in which case "this" comes second.  */
  INIT_CUMULATIVE_ARGS (cum, funtype, NULL_RTX, 0, 1);
#ifndef PCC_STATIC_STRUCT_RETURN
  if (aggregate_value_p (TREE_TYPE (TREE_TYPE (function)), function))
    structure_value_byref = 1;
#endif /* not PCC_STATIC_STRUCT_RETURN */
  if (structure_value_byref && sh_struct_value_rtx (function, 0) == 0)
    {
      tree ptype = build_pointer_type (TREE_TYPE (funtype));

      FUNCTION_ARG_ADVANCE (cum, Pmode, ptype, 1);
    }
  this = FUNCTION_ARG (cum, Pmode, ptr_type_node, 1);

  /* For SHcompact, we only have r0 for a scratch register: r1 is the
     static chain pointer (even if you can't have nested virtual functions
     right now, someone might implement them sometime), and the rest of the
     registers are used for argument passing, are callee-saved, or reserved.  */
  /* We need to check call_used_regs / fixed_regs in case -fcall_saved-reg /
     -ffixed-reg has been used.  */
  if (! call_used_regs[0] || fixed_regs[0])
    error ("r0 needs to be available as a call-clobbered register");
  scratch0 = scratch1 = scratch2 = gen_rtx_REG (Pmode, 0);
  if (! TARGET_SH5)
    {
      if (call_used_regs[1] && ! fixed_regs[1])
	scratch1 = gen_rtx_REG (ptr_mode, 1);
      /* N.B., if not TARGET_HITACHI, register 2 is used to pass the pointer
	 pointing where to return struct values.  */
      if (call_used_regs[3] && ! fixed_regs[3])
	scratch2 = gen_rtx_REG (Pmode, 3);
    }
  else if (TARGET_SHMEDIA)
    {
      for (i = FIRST_GENERAL_REG; i <= LAST_GENERAL_REG; i++)
	if (i != REGNO (scratch0) &&
	    call_used_regs[i] && ! fixed_regs[i] && ! FUNCTION_ARG_REGNO_P (i))
	  {
	    scratch1 = gen_rtx_REG (ptr_mode, i);
	    break;
	  }
      if (scratch1 == scratch0)
	error ("Need a second call-clobbered general purpose register");
      for (i = FIRST_TARGET_REG; i <= LAST_TARGET_REG; i++)
	if (call_used_regs[i] && ! fixed_regs[i])
	  {
	    scratch2 = gen_rtx_REG (Pmode, i);
	    break;
	  }
      if (scratch2 == scratch0)
	error ("Need a call-clobbered target register");
    }

  this_value = plus_constant (this, delta);
  if (vcall_offset
      && (simple_add || scratch0 != scratch1)
      && strict_memory_address_p (ptr_mode, this_value))
    {
      emit_load_ptr (scratch0, this_value);
      did_load = 1;
    }

  if (!delta)
    ; /* Do nothing.  */
  else if (simple_add)
    emit_move_insn (this, this_value);
  else
    {
      emit_move_insn (scratch1, GEN_INT (delta));
      emit_insn (gen_add2_insn (this, scratch1));
    }

  if (vcall_offset)
    {
      rtx offset_addr;

      if (!did_load)
	emit_load_ptr (scratch0, this);

      offset_addr = plus_constant (scratch0, vcall_offset);
      if (strict_memory_address_p (ptr_mode, offset_addr))
	; /* Do nothing.  */
      else if (! TARGET_SH5 && scratch0 != scratch1)
	{
	  /* scratch0 != scratch1, and we have indexed loads.  Get better
	     schedule by loading the offset into r1 and using an indexed
	     load - then the load of r1 can issue before the load from
             (this + delta) finishes.  */
	  emit_move_insn (scratch1, GEN_INT (vcall_offset));
	  offset_addr = gen_rtx_PLUS (Pmode, scratch0, scratch1);
	}
      else if (CONST_OK_FOR_ADD (vcall_offset))
	{
	  emit_insn (gen_add2_insn (scratch0, GEN_INT (vcall_offset)));
	  offset_addr = scratch0;
	}
      else if (scratch0 != scratch1)
	{
	  emit_move_insn (scratch1, GEN_INT (vcall_offset));
	  emit_insn (gen_add2_insn (scratch0, scratch1));
	  offset_addr = scratch0;
	}
      else
	gcc_unreachable (); /* FIXME */
      emit_load_ptr (scratch0, offset_addr);

      if (Pmode != ptr_mode)
	scratch0 = gen_rtx_TRUNCATE (ptr_mode, scratch0);
      emit_insn (gen_add2_insn (this, scratch0));
    }

  /* Generate a tail call to the target function.  */
  if (! TREE_USED (function))
    {
      assemble_external (function);
      TREE_USED (function) = 1;
    }
  funexp = XEXP (DECL_RTL (function), 0);
  /* If the function is overridden, so is the thunk, hence we don't
     need GOT addressing even if this is a public symbol.  */
#if 0
  if (TARGET_SH1 && ! flag_weak)
    sibcall = gen_sibcalli_thunk (funexp, const0_rtx);
  else
#endif
  if (TARGET_SH2 && flag_pic)
    {
      sibcall = gen_sibcall_pcrel (funexp, const0_rtx);
      XEXP (XVECEXP (sibcall, 0, 2), 0) = scratch2;
    }
  else
    {
      if (TARGET_SHMEDIA && flag_pic)
	{
	  funexp = gen_sym2PIC (funexp);
	  PUT_MODE (funexp, Pmode);
	}
      emit_move_insn (scratch2, funexp);
      funexp = gen_rtx_MEM (FUNCTION_MODE, scratch2);
      sibcall = gen_sibcall (funexp, const0_rtx, NULL_RTX);
    }
  sibcall = emit_call_insn (sibcall);
  SIBLING_CALL_P (sibcall) = 1;
  use_reg (&CALL_INSN_FUNCTION_USAGE (sibcall), this);
  emit_barrier ();

  /* Run just enough of rest_of_compilation to do scheduling and get
     the insns emitted.  Note that use_thunk calls
     assemble_start_function and assemble_end_function.  */

  insn_locators_initialize ();
  insns = get_insns ();

  if (optimize > 0)
    {
      /* Initialize the bitmap obstacks.  */
      bitmap_obstack_initialize (NULL);
      bitmap_obstack_initialize (&reg_obstack);
      if (! cfun->cfg)
	init_flow ();
      rtl_register_cfg_hooks ();
      init_rtl_bb_info (ENTRY_BLOCK_PTR);
      init_rtl_bb_info (EXIT_BLOCK_PTR);
      ENTRY_BLOCK_PTR->flags |= BB_RTL;
      EXIT_BLOCK_PTR->flags |= BB_RTL;
      find_basic_blocks (insns);

      if (flag_schedule_insns_after_reload)
	{
	  life_analysis (PROP_FINAL);

	  split_all_insns (1);

	  schedule_insns ();
	}
      /* We must split jmp insn in PIC case.  */
      else if (flag_pic)
	split_all_insns_noflow ();
    }

  sh_reorg ();

  if (optimize > 0 && flag_delayed_branch)
    dbr_schedule (insns);

  shorten_branches (insns);
  final_start_function (insns, file, 1);
  final (insns, file, 1);
  final_end_function ();

  if (optimize > 0)
    {
      /* Release all memory allocated by flow.  */
      free_basic_block_vars ();

      /* Release the bitmap obstacks.  */
      bitmap_obstack_release (&reg_obstack);
      bitmap_obstack_release (NULL);
    }

  reload_completed = 0;
  epilogue_completed = 0;
  no_new_pseudos = 0;
}

rtx
function_symbol (rtx target, const char *name, enum sh_function_kind kind)
{
  rtx sym;

  /* If this is not an ordinary function, the name usually comes from a
     string literal or an sprintf buffer.  Make sure we use the same
     string consistently, so that cse will be able to unify address loads.  */
  if (kind != FUNCTION_ORDINARY)
    name = IDENTIFIER_POINTER (get_identifier (name));
  sym = gen_rtx_SYMBOL_REF (Pmode, name);
  SYMBOL_REF_FLAGS (sym) = SYMBOL_FLAG_FUNCTION;
  if (flag_pic)
    switch (kind)
      {
      case FUNCTION_ORDINARY:
	break;
      case SFUNC_GOT:
	{
	  rtx reg = target ? target : gen_reg_rtx (Pmode);

	  emit_insn (gen_symGOT2reg (reg, sym));
	  sym = reg;
	  break;
	}
      case SFUNC_STATIC:
	{
	  /* ??? To allow cse to work, we use GOTOFF relocations.
	     we could add combiner patterns to transform this into
	     straight pc-relative calls with sym2PIC / bsrf when
	     label load and function call are still 1:1 and in the
	     same basic block during combine.  */
	  rtx reg = target ? target : gen_reg_rtx (Pmode);

	  emit_insn (gen_symGOTOFF2reg (reg, sym));
	  sym = reg;
	  break;
	}
      }
  if (target && sym != target)
    {
      emit_move_insn (target, sym);
      return target;
    }
  return sym;
}

/* Find the number of a general purpose register in S.  */
static int
scavenge_reg (HARD_REG_SET *s)
{
  int r;
  for (r = FIRST_GENERAL_REG; r <= LAST_GENERAL_REG; r++)
    if (TEST_HARD_REG_BIT (*s, r))
      return r;
  return -1;
}

rtx
sh_get_pr_initial_val (void)
{
  rtx val;

  /* ??? Unfortunately, get_hard_reg_initial_val doesn't always work for the
     PR register on SHcompact, because it might be clobbered by the prologue.
     We check first if that is known to be the case.  */
  if (TARGET_SHCOMPACT
      && ((current_function_args_info.call_cookie
	   & ~ CALL_COOKIE_RET_TRAMP (1))
	  || current_function_has_nonlocal_label))
    return gen_frame_mem (SImode, return_address_pointer_rtx);

  /* If we haven't finished rtl generation, there might be a nonlocal label
     that we haven't seen yet.
     ??? get_hard_reg_initial_val fails if it is called while no_new_pseudos
     is set, unless it has been called before for the same register.  And even
     then, we end in trouble if we didn't use the register in the same
     basic block before.  So call get_hard_reg_initial_val now and wrap it
     in an unspec if we might need to replace it.  */
  /* ??? We also must do this for TARGET_SH1 in general, because otherwise
     combine can put the pseudo returned by get_hard_reg_initial_val into
     instructions that need a general purpose registers, which will fail to
     be recognized when the pseudo becomes allocated to PR.  */
  val
    = get_hard_reg_initial_val (Pmode, TARGET_SHMEDIA ? PR_MEDIA_REG : PR_REG);
  if (TARGET_SH1)
    return gen_rtx_UNSPEC (SImode, gen_rtvec (1, val), UNSPEC_RA);
  return val;
}

int
sh_expand_t_scc (enum rtx_code code, rtx target)
{
  rtx result = target;
  HOST_WIDE_INT val;

  if (GET_CODE (sh_compare_op0) != REG || REGNO (sh_compare_op0) != T_REG
      || GET_CODE (sh_compare_op1) != CONST_INT)
    return 0;
  if (GET_CODE (result) != REG)
    result = gen_reg_rtx (SImode);
  val = INTVAL (sh_compare_op1);
  if ((code == EQ && val == 1) || (code == NE && val == 0))
    emit_insn (gen_movt (result));
  else if ((code == EQ && val == 0) || (code == NE && val == 1))
    {
      emit_insn (gen_rtx_CLOBBER (VOIDmode, result));
      emit_insn (gen_subc (result, result, result));
      emit_insn (gen_addsi3 (result, result, const1_rtx));
    }
  else if (code == EQ || code == NE)
    emit_insn (gen_move_insn (result, GEN_INT (code == NE)));
  else
    return 0;
  if (result != target)
    emit_move_insn (target, result);
  return 1;
}

/* INSN is an sfunc; return the rtx that describes the address used.  */
static rtx
extract_sfunc_addr (rtx insn)
{
  rtx pattern, part = NULL_RTX;
  int len, i;

  pattern = PATTERN (insn);
  len = XVECLEN (pattern, 0);
  for (i = 0; i < len; i++)
    {
      part = XVECEXP (pattern, 0, i);
      if (GET_CODE (part) == USE && GET_MODE (XEXP (part, 0)) == Pmode
	  && GENERAL_REGISTER_P (true_regnum (XEXP (part, 0))))
	return XEXP (part, 0);
    }
  gcc_assert (GET_CODE (XVECEXP (pattern, 0, 0)) == UNSPEC_VOLATILE);
  return XVECEXP (XVECEXP (pattern, 0, 0), 0, 1);
}

/* Verify that the register in use_sfunc_addr still agrees with the address
   used in the sfunc.  This prevents fill_slots_from_thread from changing
   use_sfunc_addr.
   INSN is the use_sfunc_addr instruction, and REG is the register it
   guards.  */
int
check_use_sfunc_addr (rtx insn, rtx reg)
{
  /* Search for the sfunc.  It should really come right after INSN.  */
  while ((insn = NEXT_INSN (insn)))
    {
      if (GET_CODE (insn) == CODE_LABEL || GET_CODE (insn) == JUMP_INSN)
	break;
      if (! INSN_P (insn))
	continue;

      if (GET_CODE (PATTERN (insn)) == SEQUENCE)
	insn = XVECEXP (PATTERN (insn), 0, 0);
      if (GET_CODE (PATTERN (insn)) != PARALLEL
	  || get_attr_type (insn) != TYPE_SFUNC)
	continue;
      return rtx_equal_p (extract_sfunc_addr (insn), reg);
    }
  gcc_unreachable ();
}

/* This function returns a constant rtx that represents pi / 2**15 in
   SFmode.  it's used to scale SFmode angles, in radians, to a
   fixed-point signed 16.16-bit fraction of a full circle, i.e., 2*pi
   maps to 0x10000).  */

static GTY(()) rtx sh_fsca_sf2int_rtx;

rtx
sh_fsca_sf2int (void)
{
  if (! sh_fsca_sf2int_rtx)
    {
      REAL_VALUE_TYPE rv;

      real_from_string (&rv, "10430.378350470453");
      sh_fsca_sf2int_rtx = const_double_from_real_value (rv, SFmode);
    }

  return sh_fsca_sf2int_rtx;
}

/* This function returns a constant rtx that represents pi / 2**15 in
   DFmode.  it's used to scale DFmode angles, in radians, to a
   fixed-point signed 16.16-bit fraction of a full circle, i.e., 2*pi
   maps to 0x10000).  */

static GTY(()) rtx sh_fsca_df2int_rtx;

rtx
sh_fsca_df2int (void)
{
  if (! sh_fsca_df2int_rtx)
    {
      REAL_VALUE_TYPE rv;

      real_from_string (&rv, "10430.378350470453");
      sh_fsca_df2int_rtx = const_double_from_real_value (rv, DFmode);
    }

  return sh_fsca_df2int_rtx;
}

/* This function returns a constant rtx that represents 2**15 / pi in
   SFmode.  it's used to scale a fixed-point signed 16.16-bit fraction
   of a full circle back to a SFmode value, i.e., 0x10000 maps to
   2*pi).  */

static GTY(()) rtx sh_fsca_int2sf_rtx;

rtx
sh_fsca_int2sf (void)
{
  if (! sh_fsca_int2sf_rtx)
    {
      REAL_VALUE_TYPE rv;

      real_from_string (&rv, "9.587379924285257e-5");
      sh_fsca_int2sf_rtx = const_double_from_real_value (rv, SFmode);
    }

  return sh_fsca_int2sf_rtx;
}

/* Initialize the CUMULATIVE_ARGS structure.  */

void
sh_init_cumulative_args (CUMULATIVE_ARGS *  pcum,
			 tree               fntype,
			 rtx		    libname ATTRIBUTE_UNUSED,
			 tree               fndecl,
			 signed int         n_named_args,
			 enum machine_mode  mode)
{
  pcum->arg_count [(int) SH_ARG_FLOAT] = 0;
  pcum->free_single_fp_reg = 0;
  pcum->stack_regs = 0;
  pcum->byref_regs = 0;
  pcum->byref = 0;
  pcum->outgoing = (n_named_args == -1) ? 0 : 1;

  /* XXX - Should we check TARGET_HITACHI here ???  */
  pcum->renesas_abi = sh_attr_renesas_p (fntype) ? 1 : 0;

  if (fntype)
    {
      pcum->force_mem = ((TARGET_HITACHI || pcum->renesas_abi)
			 && aggregate_value_p (TREE_TYPE (fntype), fndecl));
      pcum->prototype_p = TYPE_ARG_TYPES (fntype) ? TRUE : FALSE;
      pcum->arg_count [(int) SH_ARG_INT]
	= TARGET_SH5 && aggregate_value_p (TREE_TYPE (fntype), fndecl);

      pcum->call_cookie
	= CALL_COOKIE_RET_TRAMP (TARGET_SHCOMPACT
				 && pcum->arg_count [(int) SH_ARG_INT] == 0
				 && (TYPE_MODE (TREE_TYPE (fntype)) == BLKmode
				     ? int_size_in_bytes (TREE_TYPE (fntype))
				     : GET_MODE_SIZE (TYPE_MODE (TREE_TYPE (fntype)))) > 4
				 && (BASE_RETURN_VALUE_REG (TYPE_MODE (TREE_TYPE (fntype)))
				     == FIRST_RET_REG));
    }
  else
    {
      pcum->arg_count [(int) SH_ARG_INT] = 0;
      pcum->prototype_p = FALSE;
      if (mode != VOIDmode)
	{
	  pcum->call_cookie =
	    CALL_COOKIE_RET_TRAMP (TARGET_SHCOMPACT
				   && GET_MODE_SIZE (mode) > 4
				   && BASE_RETURN_VALUE_REG (mode) == FIRST_RET_REG);

	  /* If the default ABI is the Renesas ABI then all library
	     calls must assume that the library will be using the
	     Renesas ABI.  So if the function would return its result
	     in memory then we must force the address of this memory
	     block onto the stack.  Ideally we would like to call
	     targetm.calls.return_in_memory() here but we do not have
	     the TYPE or the FNDECL available so we synthesize the
	     contents of that function as best we can.  */
	  pcum->force_mem =
	    (TARGET_DEFAULT & MASK_HITACHI)
	    && (mode == BLKmode
		|| (GET_MODE_SIZE (mode) > 4
		    && !(mode == DFmode
			 && TARGET_FPU_DOUBLE)));
	}
      else
	{
	  pcum->call_cookie = 0;
	  pcum->force_mem = FALSE;
	}
    }
}

/* Determine if two hard register sets intersect.
   Return 1 if they do.  */

static int
hard_regs_intersect_p (HARD_REG_SET *a, HARD_REG_SET *b)
{
  HARD_REG_SET c;
  COPY_HARD_REG_SET (c, *a);
  AND_HARD_REG_SET (c, *b);
  GO_IF_HARD_REG_SUBSET (c, reg_class_contents[(int) NO_REGS], lose);
  return 1;
lose:
  return 0;
}

#ifdef TARGET_ADJUST_UNROLL_MAX
static int
sh_adjust_unroll_max (struct loop * loop, int insn_count,
		      int max_unrolled_insns, int strength_reduce_p,
		      int unroll_type)
{
/* This doesn't work in 4.0 because the old unroller & loop.h  is gone.  */
  if (TARGET_ADJUST_UNROLL && TARGET_SHMEDIA)
    {
      /* Throttle back loop unrolling so that the costs of using more
	 targets than the eight target register we have don't outweigh
	 the benefits of unrolling.  */
      rtx insn;
      int n_labels = 0, n_calls = 0, n_exit_dest = 0, n_inner_loops = -1;
      int n_barriers = 0;
      rtx dest;
      int i;
      rtx exit_dest[8];
      int threshold;
      int unroll_benefit = 0, mem_latency = 0;
      int base_cost, best_cost, cost;
      int factor, best_factor;
      int n_dest;
      unsigned max_iterations = 32767;
      int n_iterations;
      int need_precond = 0, precond = 0;
      basic_block * bbs = get_loop_body (loop);
      struct niter_desc *desc;

      /* Assume that all labels inside the loop are used from inside the
	 loop.  If the loop has multiple entry points, it is unlikely to
	 be unrolled anyways.
	 Also assume that all calls are to different functions.  That is
	 somewhat pessimistic, but if you have lots of calls, unrolling the
	 loop is not likely to gain you much in the first place.  */
      i = loop->num_nodes - 1;
      for (insn = BB_HEAD (bbs[i]); ; )
	{
	  if (GET_CODE (insn) == CODE_LABEL)
	    n_labels++;
	  else if (GET_CODE (insn) == CALL_INSN)
	    n_calls++;
	  else if (GET_CODE (insn) == NOTE
		   && NOTE_LINE_NUMBER (insn) == NOTE_INSN_LOOP_BEG)
	    n_inner_loops++;
	  else if (GET_CODE (insn) == BARRIER)
	    n_barriers++;
	  if (insn != BB_END (bbs[i]))
	    insn = NEXT_INSN (insn);
	  else if (--i >= 0)
	    insn = BB_HEAD (bbs[i]);
	   else
	    break;
	}
      free (bbs);
      /* One label for the loop top is normal, and it won't be duplicated by
	 unrolling.  */
      if (n_labels <= 1)
	return max_unrolled_insns;
      if (n_inner_loops > 0)
	return 0;
      for (dest = loop->exit_labels; dest && n_exit_dest < 8;
	   dest = LABEL_NEXTREF (dest))
	{
	  for (i = n_exit_dest - 1;
	       i >= 0 && XEXP (dest, 0) != XEXP (exit_dest[i], 0); i--);
	  if (i < 0)
	    exit_dest[n_exit_dest++] = dest;
	}
      /* If the loop top and call and exit destinations are enough to fill up
	 the target registers, we're unlikely to do any more damage by
	 unrolling.  */
      if (n_calls + n_exit_dest >= 7)
	return max_unrolled_insns;

      /* ??? In the new loop unroller, there is no longer any strength
         reduction information available.  Thus, when it comes to unrolling,
         we know the cost of everything, but we know the value of nothing.  */
#if 0
      if (strength_reduce_p
	  && (unroll_type == LPT_UNROLL_RUNTIME
	      || unroll_type == LPT_UNROLL_CONSTANT
	      || unroll_type == LPT_PEEL_COMPLETELY))
	{
	  struct loop_ivs *ivs = LOOP_IVS (loop);
	  struct iv_class *bl;

	  /* We'll save one compare-and-branch in each loop body copy
	     but the last one.  */
	  unroll_benefit = 1;
	  /* Assess the benefit of removing biv & giv updates.  */
	  for (bl = ivs->list; bl; bl = bl->next)
	    {
	      rtx increment = biv_total_increment (bl);
	      struct induction *v;

	      if (increment && GET_CODE (increment) == CONST_INT)
		{
		  unroll_benefit++;
		  for (v = bl->giv; v; v = v->next_iv)
		    {
		      if (! v->ignore && v->same == 0
			  && GET_CODE (v->mult_val) == CONST_INT)
			unroll_benefit++;
		      /* If this giv uses an array, try to determine
			 a maximum iteration count from the size of the
			 array.  This need not be correct all the time,
			 but should not be too far off the mark too often.  */
		      while (v->giv_type == DEST_ADDR)
			{
			  rtx mem = PATTERN (v->insn);
			  tree mem_expr, type, size_tree;

			  if (GET_CODE (SET_SRC (mem)) == MEM)
			    mem = SET_SRC (mem);
			  else if (GET_CODE (SET_DEST (mem)) == MEM)
			    mem = SET_DEST (mem);
			  else
			    break;
			  mem_expr = MEM_EXPR (mem);
			  if (! mem_expr)
			    break;
			  type = TREE_TYPE (mem_expr);
			  if (TREE_CODE (type) != ARRAY_TYPE
			      || ! TYPE_SIZE (type) || ! TYPE_SIZE_UNIT (type))
			    break;
			  size_tree = fold_build2 (TRUNC_DIV_EXPR,
						   bitsizetype,
						   TYPE_SIZE (type),
						   TYPE_SIZE_UNIT (type));
			  if (TREE_CODE (size_tree) == INTEGER_CST
			      && ! TREE_INT_CST_HIGH (size_tree)
			      && TREE_INT_CST_LOW  (size_tree) < max_iterations)
			    max_iterations = TREE_INT_CST_LOW  (size_tree);
			  break;
			}
		    }
		}
	    }
	}
#else /* 0 */
      /* Assume there is at least some benefit.  */
      unroll_benefit = 1;
#endif /* 0 */

      desc = get_simple_loop_desc (loop);
      n_iterations = desc->const_iter ? desc->niter : 0;
      max_iterations
	= max_iterations < desc->niter_max ? max_iterations : desc->niter_max;

      if (! strength_reduce_p || ! n_iterations)
	need_precond = 1;
      if (! n_iterations)
	{
	  n_iterations
	    = max_iterations < 3 ? max_iterations : max_iterations * 3 / 4;
	  if (! n_iterations)
	    return 0;
	}
#if 0 /* ??? See above - missing induction variable information.  */
      while (unroll_benefit > 1) /* no loop */
	{
	  /* We include the benefit of biv/ giv updates.  Check if some or
	     all of these updates are likely to fit into a scheduling
	     bubble of a load.
	     We check for the following case:
	     - All the insns leading to the first JUMP_INSN are in a strict
	       dependency chain.
	     - there is at least one memory reference in them.

	     When we find such a pattern, we assume that we can hide as many
	     updates as the total of the load latency is, if we have an
	     unroll factor of at least two.  We might or might not also do
	     this without unrolling, so rather than considering this as an
	     extra unroll benefit, discount it in the unroll benefits of unroll
	     factors higher than two.  */
		
	  rtx set, last_set;

	  insn = next_active_insn (loop->start);
	  last_set = single_set (insn);
	  if (! last_set)
	    break;
	  if (GET_CODE (SET_SRC (last_set)) == MEM)
	    mem_latency += 2;
	  for (insn = NEXT_INSN (insn); insn != end; insn = NEXT_INSN (insn))
	    {
	      if (! INSN_P (insn))
		continue;
	      if (GET_CODE (insn) == JUMP_INSN)
		break;
	      if (! reg_referenced_p (SET_DEST (last_set), PATTERN (insn)))
		{
		  /* Check if this is a to-be-reduced giv insn.  */
		  struct loop_ivs *ivs = LOOP_IVS (loop);
		  struct iv_class *bl;
		  struct induction *v;
		  for (bl = ivs->list; bl; bl = bl->next)
		    {
		      if (bl->biv->insn == insn)
			goto is_biv;
		      for (v = bl->giv; v; v = v->next_iv)
			if (v->insn == insn)
			  goto is_giv;
		    }
		  mem_latency--;
		is_biv:
		is_giv:
		  continue;
		}
	      set = single_set (insn);
	      if (! set)
		continue;
	      if (GET_CODE (SET_SRC (set)) == MEM)
		mem_latency += 2;
	      last_set = set;
	    }
	  if (mem_latency < 0)
	    mem_latency = 0;
	  else if (mem_latency > unroll_benefit - 1)
	    mem_latency = unroll_benefit - 1;
	  break;
	}
#endif /* 0 */
      if (n_labels + (unroll_benefit + n_labels * 8) / n_iterations
	  <= unroll_benefit)
	return max_unrolled_insns;

      n_dest = n_labels + n_calls + n_exit_dest;
      base_cost = n_dest <= 8 ? 0 : n_dest - 7;
      best_cost = 0;
      best_factor = 1;
      if (n_barriers * 2 > n_labels - 1)
	n_barriers = (n_labels - 1) / 2;
      for (factor = 2; factor <= 8; factor++)
	{
	  /* Bump up preconditioning cost for each power of two.  */
	  if (! (factor & (factor-1)))
	    precond += 4;
	  /* When preconditioning, only powers of two will be considered.  */
	  else if (need_precond)
	    continue;
	  n_dest = ((unroll_type != LPT_PEEL_COMPLETELY)
		    + (n_labels - 1) * factor + n_calls + n_exit_dest
		    - (n_barriers * factor >> 1)
		    + need_precond);
	  cost
	    = ((n_dest <= 8 ? 0 : n_dest - 7)
	       - base_cost * factor
	       - ((factor > 2 ? unroll_benefit - mem_latency : unroll_benefit)
		  * (factor - (unroll_type != LPT_PEEL_COMPLETELY)))
	       + ((unroll_benefit + 1 + (n_labels - 1) * factor)
		  / n_iterations));
	  if (need_precond)
	    cost += (precond + unroll_benefit * factor / 2) / n_iterations;
	  if (cost < best_cost)
	    {
	      best_cost = cost;
	      best_factor = factor;
	    }
	}
      threshold = best_factor * insn_count;
      if (max_unrolled_insns > threshold)
	max_unrolled_insns = threshold;
    }
  return max_unrolled_insns;
}
#endif /* TARGET_ADJUST_UNROLL_MAX */

/* Replace any occurrence of FROM(n) in X with TO(n).  The function does
   not enter into CONST_DOUBLE for the replace.

   Note that copying is not done so X must not be shared unless all copies
   are to be modified.

   This is like replace_rtx, except that we operate on N_REPLACEMENTS
   replacements simultaneously - FROM(n) is replacements[n*2] and to(n) is
   replacements[n*2+1] - and that we take mode changes into account.

   If a replacement is ambiguous, return NULL_RTX.

   If MODIFY is zero, don't modify any rtl in place,
   just return zero or nonzero for failure / success.  */

rtx
replace_n_hard_rtx (rtx x, rtx *replacements, int n_replacements, int modify)
{
  int i, j;
  const char *fmt;

  /* The following prevents loops occurrence when we change MEM in
     CONST_DOUBLE onto the same CONST_DOUBLE.  */
  if (x != 0 && GET_CODE (x) == CONST_DOUBLE)
    return x;

  for (i = n_replacements - 1; i >= 0 ; i--)
  if (x == replacements[i*2] && GET_MODE (x) == GET_MODE (replacements[i*2+1]))
    return replacements[i*2+1];

  /* Allow this function to make replacements in EXPR_LISTs.  */
  if (x == 0)
    return 0;

  if (GET_CODE (x) == SUBREG)
    {
      rtx new = replace_n_hard_rtx (SUBREG_REG (x), replacements,
				    n_replacements, modify);

      if (GET_CODE (new) == CONST_INT)
	{
	  x = simplify_subreg (GET_MODE (x), new,
			       GET_MODE (SUBREG_REG (x)),
			       SUBREG_BYTE (x));
	  if (! x)
	    abort ();
	}
      else if (modify)
	SUBREG_REG (x) = new;

      return x;
    }
  else if (GET_CODE (x) == REG)
    {
      unsigned regno = REGNO (x);
      unsigned nregs = (regno < FIRST_PSEUDO_REGISTER
			? HARD_REGNO_NREGS (regno, GET_MODE (x)) : 1);
      rtx result = NULL_RTX;

      for (i = n_replacements - 1; i >= 0; i--)
	{
	  rtx from = replacements[i*2];
	  rtx to = replacements[i*2+1];
	  unsigned from_regno, from_nregs, to_regno, new_regno;

	  if (GET_CODE (from) != REG)
	    continue;
	  from_regno = REGNO (from);
	  from_nregs = (from_regno < FIRST_PSEUDO_REGISTER
			? HARD_REGNO_NREGS (from_regno, GET_MODE (from)) : 1);
	  if (regno < from_regno + from_nregs && regno + nregs > from_regno)
	    {
	      if (regno < from_regno
		  || regno + nregs > from_regno + nregs
		  || GET_CODE (to) != REG
		  || result)
		return NULL_RTX;
	      to_regno = REGNO (to);
	      if (to_regno < FIRST_PSEUDO_REGISTER)
		{
		  new_regno = regno + to_regno - from_regno;
		  if ((unsigned) HARD_REGNO_NREGS (new_regno, GET_MODE (x))
		      != nregs)
		    return NULL_RTX;
		  result = gen_rtx_REG (GET_MODE (x), new_regno);
		}
	      else if (GET_MODE (x) <= GET_MODE (to))
		result = gen_lowpart_common (GET_MODE (x), to);
	      else
		result = gen_lowpart_SUBREG (GET_MODE (x), to);
	    }
	}
      return result ? result : x;
    }
  else if (GET_CODE (x) == ZERO_EXTEND)
    {
      rtx new = replace_n_hard_rtx (XEXP (x, 0), replacements,
				    n_replacements, modify);

      if (GET_CODE (new) == CONST_INT)
	{
	  x = simplify_unary_operation (ZERO_EXTEND, GET_MODE (x),
					new, GET_MODE (XEXP (x, 0)));
	  if (! x)
	    abort ();
	}
      else if (modify)
	XEXP (x, 0) = new;

      return x;
    }

  fmt = GET_RTX_FORMAT (GET_CODE (x));
  for (i = GET_RTX_LENGTH (GET_CODE (x)) - 1; i >= 0; i--)
    {
      rtx new;

      if (fmt[i] == 'e')
	{
	  new = replace_n_hard_rtx (XEXP (x, i), replacements,
				    n_replacements, modify);
	  if (!new)
	    return NULL_RTX;
	  if (modify)
	    XEXP (x, i) = new;
	}
      else if (fmt[i] == 'E')
	for (j = XVECLEN (x, i) - 1; j >= 0; j--)
	  {
	    new = replace_n_hard_rtx (XVECEXP (x, i, j), replacements,
				      n_replacements, modify);
	  if (!new)
	    return NULL_RTX;
	    if (modify)
	      XVECEXP (x, i, j) = new;
	  }
    }

  return x;
}

rtx
sh_gen_truncate (enum machine_mode mode, rtx x, int need_sign_ext)
{
  enum rtx_code code = TRUNCATE;

  if (GET_CODE (x) == ZERO_EXTEND || GET_CODE (x) == SIGN_EXTEND)
    {
      rtx inner = XEXP (x, 0);
      enum machine_mode inner_mode = GET_MODE (inner);

      if (inner_mode == mode)
	return inner;
      else if (GET_MODE_SIZE (inner_mode) >= GET_MODE_SIZE (mode))
	x = inner;
      else if (GET_MODE_SIZE (inner_mode) < GET_MODE_SIZE (mode)
	       && (! need_sign_ext || GET_CODE (x) == SIGN_EXTEND))
	{
	  code = GET_CODE (x);
	  x = inner;
	}
    }
  return gen_rtx_fmt_e (code, mode, x);
}

/* called via for_each_rtx after reload, to clean up truncates of
   registers that span multiple actual hard registers.  */
int
shmedia_cleanup_truncate (rtx *p, void *n_changes)
{
  rtx x = *p, reg;

  if (GET_CODE (x) != TRUNCATE)
    return 0;
  reg = XEXP (x, 0);
  if (GET_MODE_SIZE (GET_MODE (reg)) > 8 && GET_CODE (reg) == REG)
    {
      enum machine_mode reg_mode = GET_MODE (reg);
      XEXP (x, 0) = simplify_subreg (DImode, reg, reg_mode,
				     subreg_lowpart_offset (DImode, reg_mode));
      *(int*) n_changes += 1;
      return -1;
    }
  return 0;
}

/* Load and store depend on the highpart of the address.  However,
   set_attr_alternative does not give well-defined results before reload,
   so we must look at the rtl ourselves to see if any of the feeding
   registers is used in a memref.  */

/* Called by sh_contains_memref_p via for_each_rtx.  */
static int
sh_contains_memref_p_1 (rtx *loc, void *data ATTRIBUTE_UNUSED)
{
  return (GET_CODE (*loc) == MEM);
}

/* Return nonzero iff INSN contains a MEM.  */
int
sh_contains_memref_p (rtx insn)
{
  return for_each_rtx (&PATTERN (insn), &sh_contains_memref_p_1, NULL);
}

/* FNADDR is the MEM expression from a call expander.  Return an address
   to use in an SHmedia insn pattern.  */
rtx
shmedia_prepare_call_address (rtx fnaddr, int is_sibcall)
{
  int is_sym;

  fnaddr = XEXP (fnaddr, 0);
  is_sym = GET_CODE (fnaddr) == SYMBOL_REF;
  if (flag_pic && is_sym)
    {
      if (! SYMBOL_REF_LOCAL_P (fnaddr))
	{
	  rtx reg = gen_reg_rtx (Pmode);

	  /* We must not use GOTPLT for sibcalls, because PIC_REG
	     must be restored before the PLT code gets to run.  */
	  if (is_sibcall)
	    emit_insn (gen_symGOT2reg (reg, fnaddr));
	  else
	    emit_insn (gen_symGOTPLT2reg (reg, fnaddr));
	  fnaddr = reg;
	}
      else
	{
	  fnaddr = gen_sym2PIC (fnaddr);
	  PUT_MODE (fnaddr, Pmode);
	}
    }
  /* If ptabs might trap, make this visible to the rest of the compiler.
     We generally assume that symbols pertain to valid locations, but
     it is possible to generate invalid symbols with asm or linker tricks.
     In a list of functions where each returns its successor, an invalid
     symbol might denote an empty list.  */
  if (!TARGET_PT_FIXED
      && (!is_sym || TARGET_INVALID_SYMBOLS)
      && (!REG_P (fnaddr) || ! TARGET_REGISTER_P (REGNO (fnaddr))))
    {
      rtx tr = gen_reg_rtx (PDImode);

      emit_insn (gen_ptabs (tr, fnaddr));
      fnaddr = tr;
    }
  else if (! target_reg_operand (fnaddr, Pmode))
    fnaddr = copy_to_mode_reg (Pmode, fnaddr);
  return fnaddr;
}

enum reg_class
sh_secondary_reload (bool in_p, rtx x, enum reg_class class,
		     enum machine_mode mode, secondary_reload_info *sri)
{
  if (in_p)
    {
      if (REGCLASS_HAS_FP_REG (class)
	  && ! TARGET_SHMEDIA
	  && immediate_operand ((x), mode)
	  && ! ((fp_zero_operand (x) || fp_one_operand (x))
		&& mode == SFmode && fldi_ok ()))
	switch (mode)
	  {
	  case SFmode:
	    sri->icode = CODE_FOR_reload_insf__frn;
	    return NO_REGS;
	  case DFmode:
	    sri->icode = CODE_FOR_reload_indf__frn;
	    return NO_REGS;
	  case SImode:
	    /* ??? If we knew that we are in the appropriate mode -
	       single precision - we could use a reload pattern directly.  */
	    return FPUL_REGS;
	  default:
	    abort ();
	  }
      if (class == FPUL_REGS
          && ((GET_CODE (x) == REG
               && (REGNO (x) == MACL_REG || REGNO (x) == MACH_REG
                   || REGNO (x) == T_REG))
              || GET_CODE (x) == PLUS))
        return GENERAL_REGS;
      if (class == FPUL_REGS && immediate_operand (x, mode))
	{
	  if (GET_CODE (x) == CONST_INT && CONST_OK_FOR_I08 (INTVAL (x)))
	    return GENERAL_REGS;
	  sri->icode = CODE_FOR_reload_insi__i_fpul;
	  return NO_REGS;
	}
      if (class == FPSCR_REGS
          && ((GET_CODE (x) == REG && REGNO (x) >= FIRST_PSEUDO_REGISTER)
              || (GET_CODE (x) == MEM && GET_CODE (XEXP (x, 0)) == PLUS)))
        return GENERAL_REGS;
      if (REGCLASS_HAS_FP_REG (class)
          && TARGET_SHMEDIA
          && immediate_operand (x, mode)
          && x != CONST0_RTX (GET_MODE (x))
          && GET_MODE (x) != V4SFmode)
        return GENERAL_REGS;
      if ((mode == QImode || mode == HImode)
          && TARGET_SHMEDIA && inqhi_operand (x, mode))
	{
	  sri->icode = ((mode == QImode)
			? CODE_FOR_reload_inqi : CODE_FOR_reload_inhi);
	  return NO_REGS;
	}
      if (TARGET_SHMEDIA && class == GENERAL_REGS
          && (GET_CODE (x) == LABEL_REF || PIC_DIRECT_ADDR_P (x)))
        return TARGET_REGS;
    } /* end of input-only processing.  */

  if (((REGCLASS_HAS_FP_REG (class)
	&& (GET_CODE (x) == REG
	    && (GENERAL_OR_AP_REGISTER_P (REGNO (x))
		|| (FP_REGISTER_P (REGNO (x)) && mode == SImode
		    && TARGET_FMOVD))))
       || (REGCLASS_HAS_GENERAL_REG (class)
	   && GET_CODE (x) == REG
	   && FP_REGISTER_P (REGNO (x))))
      && ! TARGET_SHMEDIA
      && (mode == SFmode || mode == SImode))
    return FPUL_REGS;
  if ((class == FPUL_REGS
       || (REGCLASS_HAS_FP_REG (class)
           && ! TARGET_SHMEDIA && mode == SImode))
      && (GET_CODE (x) == MEM
          || (GET_CODE (x) == REG
              && (REGNO (x) >= FIRST_PSEUDO_REGISTER
                  || REGNO (x) == T_REG
                  || system_reg_operand (x, VOIDmode)))))
    {
      if (class == FPUL_REGS)
	return GENERAL_REGS;
      return FPUL_REGS;
    }
  if ((class == TARGET_REGS
       || (TARGET_SHMEDIA && class == SIBCALL_REGS))
      && !EXTRA_CONSTRAINT_Csy (x)
      && (GET_CODE (x) != REG || ! GENERAL_REGISTER_P (REGNO (x))))
    return GENERAL_REGS;
  if ((class == MAC_REGS || class == PR_REGS)
      && GET_CODE (x) == REG && ! GENERAL_REGISTER_P (REGNO (x))
      && class != REGNO_REG_CLASS (REGNO (x)))
    return GENERAL_REGS;
  if (class != GENERAL_REGS && GET_CODE (x) == REG
      && TARGET_REGISTER_P (REGNO (x)))
    return GENERAL_REGS;
  return NO_REGS;
}

enum sh_divide_strategy_e sh_div_strategy = SH_DIV_STRATEGY_DEFAULT;

#include "gt-sh.h"

void
sh_override_options (void)
{
  int regno;

  if (flag_finite_math_only == 2)
    flag_finite_math_only
      = !flag_signaling_nans && TARGET_SH2E && ! TARGET_IEEE;
  if (TARGET_SH2E && !flag_finite_math_only)
    target_flags |= MASK_IEEE;
  sh_cpu = CPU_SH1;
  assembler_dialect = 0;
  if (TARGET_SH2)
    sh_cpu = CPU_SH2;
  if (TARGET_SH2E)
    sh_cpu = CPU_SH2E;
  if (TARGET_SH2A)
    {
      sh_cpu = CPU_SH2A;
      if (TARGET_SH2A_DOUBLE)
        target_flags |= MASK_FMOVD;
    }
  if (TARGET_SH3)
    sh_cpu = CPU_SH3;
  if (TARGET_SH3E)
    sh_cpu = CPU_SH3E;
  if (TARGET_SH4)
    {
      assembler_dialect = 1;
      sh_cpu = CPU_SH4;
    }
  if (TARGET_SH4A_ARCH)
    {
      assembler_dialect = 1;
      sh_cpu = CPU_SH4A;
    }
  if (TARGET_SH5)
    {
      sh_cpu = CPU_SH5;
      target_flags |= MASK_ALIGN_DOUBLE;
      if (TARGET_SHMEDIA_FPU)
	target_flags |= MASK_FMOVD;
      if (TARGET_SHMEDIA)
	{
	  /* There are no delay slots on SHmedia.  */
	  flag_delayed_branch = 0;
	  /* Relaxation isn't yet supported for SHmedia */
	  target_flags &= ~MASK_RELAX;
	  /* After reload, if conversion does little good but can cause 
	     ICEs:
	     - find_if_block doesn't do anything for SH because we don't
	       have conditional execution patterns.  (We use conditional
	       move patterns, which are handled differently, and only
	       before reload).
	     - find_cond_trap doesn't do anything for the SH because we \
	       don't have conditional traps.
	     - find_if_case_1 uses redirect_edge_and_branch_force in
	       the only path that does an optimization, and this causes
	       an ICE when branch targets are in registers.
	     - find_if_case_2 doesn't do anything for the SHmedia after
	       reload except when it can redirect a tablejump - and
	       that's rather rare.  */
	  flag_if_conversion2 = 0;
	  if (! strcmp (sh_div_str, "call"))
	    sh_div_strategy = SH_DIV_CALL;
	  else if (! strcmp (sh_div_str, "call2"))
	    sh_div_strategy = SH_DIV_CALL2;
	  if (! strcmp (sh_div_str, "fp") && TARGET_FPU_ANY)
	    sh_div_strategy = SH_DIV_FP;
	  else if (! strcmp (sh_div_str, "inv"))
	    sh_div_strategy = SH_DIV_INV;
	  else if (! strcmp (sh_div_str, "inv:minlat"))
	    sh_div_strategy = SH_DIV_INV_MINLAT;
	  else if (! strcmp (sh_div_str, "inv20u"))
	    sh_div_strategy = SH_DIV_INV20U;
	  else if (! strcmp (sh_div_str, "inv20l"))
	    sh_div_strategy = SH_DIV_INV20L;
	  else if (! strcmp (sh_div_str, "inv:call2"))
	    sh_div_strategy = SH_DIV_INV_CALL2;
	  else if (! strcmp (sh_div_str, "inv:call"))
	    sh_div_strategy = SH_DIV_INV_CALL;
	  else if (! strcmp (sh_div_str, "inv:fp"))
	    {
	      if (TARGET_FPU_ANY)
		sh_div_strategy = SH_DIV_INV_FP;
	      else
		sh_div_strategy = SH_DIV_INV;
	    }
	}
      /* -fprofile-arcs needs a working libgcov .  In unified tree
	 configurations with newlib, this requires to configure with
	 --with-newlib --with-headers.  But there is no way to check
	 here we have a working libgcov, so just assume that we have.  */
      if (profile_flag)
	warning (0, "profiling is still experimental for this target");
    }
  else
    {
       /* Only the sh64-elf assembler fully supports .quad properly.  */
       targetm.asm_out.aligned_op.di = NULL;
       targetm.asm_out.unaligned_op.di = NULL;
    }
  if (!TARGET_SH1)
    TARGET_PRETEND_CMOVE = 0;
  if (sh_divsi3_libfunc[0])
    ; /* User supplied - leave it alone.  */
  else if (TARGET_HARD_SH4 && TARGET_SH2E)
    sh_divsi3_libfunc = "__sdivsi3_i4";
  else if (TARGET_SH5)
    {
      if (TARGET_FPU_ANY && TARGET_SH1)
       sh_divsi3_libfunc = "__sdivsi3_i4";
      else
       sh_divsi3_libfunc = "__sdivsi3_1"; 
    }
  else
    sh_divsi3_libfunc = "__sdivsi3";
  if (TARGET_FMOVD)
    reg_class_from_letter['e' - 'a'] = NO_REGS;

  for (regno = 0; regno < FIRST_PSEUDO_REGISTER; regno++)
    if (! VALID_REGISTER_P (regno))
      sh_register_names[regno][0] = '\0';

  for (regno = 0; regno < ADDREGNAMES_SIZE; regno++)
    if (! VALID_REGISTER_P (ADDREGNAMES_REGNO (regno)))
      sh_additional_register_names[regno][0] = '\0';

  if (flag_omit_frame_pointer < 0)
   {
     /* The debugging information is sufficient,
        but gdb doesn't implement this yet */
     if (0)
      flag_omit_frame_pointer
        = (PREFERRED_DEBUGGING_TYPE == DWARF2_DEBUG);
     else
      flag_omit_frame_pointer = 0;
   }

  if ((flag_pic && ! TARGET_PREFERGOT)
      || (TARGET_SHMEDIA && !TARGET_PT_FIXED))
    flag_no_function_cse = 1;

  if (SMALL_REGISTER_CLASSES)
    {
      /* Never run scheduling before reload, since that can
	 break global alloc, and generates slower code anyway due
	 to the pressure on R0.  */
      /* Enable sched1 for SH4; ready queue will be reordered by
	 the target hooks when pressure is high. We can not do this for 
	 SH3 and lower as they give spill failures for R0.  */
      if (!TARGET_HARD_SH4 || flag_pic) 
        flag_schedule_insns = 0;		 
      /* ??? Current exception handling places basic block boundaries
	 after call_insns.  It causes the high pressure on R0 and gives
	 spill failures for R0 in reload.  See PR 22553 and the thread
	 on gcc-patches
         <http://gcc.gnu.org/ml/gcc-patches/2005-10/msg00816.html>.  */
      else if (flag_exceptions)
	{
	  if (flag_schedule_insns == 1)		 
	    warning (0, "ignoring -fschedule-insns because of exception handling bug");
	  flag_schedule_insns = 0;		 
	}
    }

  if (align_loops == 0)
    align_loops =  1 << (TARGET_SH5 ? 3 : 2);
#if defined(OPENBSD_NATIVE) || defined(OPENBSD_CROSS)
  /* Do not align jump targets to cache line boundaries at -O2 */
  if (align_jumps == 0)
    align_jumps =  2;
#else
  if (align_jumps == 0)
    align_jumps = 1 << CACHE_LOG;
#endif
  else if (align_jumps < (TARGET_SHMEDIA ? 4 : 2))
    align_jumps = TARGET_SHMEDIA ? 4 : 2;

  /* Allocation boundary (in *bytes*) for the code of a function.
     SH1: 32 bit alignment is faster, because instructions are always
     fetched as a pair from a longword boundary.
     SH2 .. SH5 : align to cache line start.  */
  if (align_functions == 0)
    align_functions
      = TARGET_SMALLCODE ? FUNCTION_BOUNDARY/8 : (1 << CACHE_LOG);
  /* The linker relaxation code breaks when a function contains
     alignments that are larger than that at the start of a
     compilation unit.  */
  if (TARGET_RELAX)
    {
      int min_align
	= align_loops > align_jumps ? align_loops : align_jumps;

      /* Also take possible .long constants / mova tables int account.	*/
      if (min_align < 4)
	min_align = 4;
      if (align_functions < min_align)
	align_functions = min_align;
    }
}
