/* Definitions of target machine for GNU compiler.
   Copyright (C) 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007
   Free Software Foundation, Inc.
   Contributed by James E. Wilson <wilson@cygnus.com> and
		  David Mosberger <davidm@hpl.hp.com>.

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
#include "tree.h"
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
#include "optabs.h"
#include "except.h"
#include "function.h"
#include "ggc.h"
#include "basic-block.h"
#include "toplev.h"
#include "sched-int.h"
#include "timevar.h"
#include "target.h"
#include "target-def.h"
#include "tm_p.h"
#include "hashtab.h"
#include "langhooks.h"
#include "cfglayout.h"
#include "tree-gimple.h"
#include "intl.h"
#include "debug.h"
#include "params.h"

/* This is used for communication between ASM_OUTPUT_LABEL and
   ASM_OUTPUT_LABELREF.  */
int ia64_asm_output_label = 0;

/* Define the information needed to generate branch and scc insns.  This is
   stored from the compare operation.  */
struct rtx_def * ia64_compare_op0;
struct rtx_def * ia64_compare_op1;

/* Register names for ia64_expand_prologue.  */
static const char * const ia64_reg_numbers[96] =
{ "r32", "r33", "r34", "r35", "r36", "r37", "r38", "r39",
  "r40", "r41", "r42", "r43", "r44", "r45", "r46", "r47",
  "r48", "r49", "r50", "r51", "r52", "r53", "r54", "r55",
  "r56", "r57", "r58", "r59", "r60", "r61", "r62", "r63",
  "r64", "r65", "r66", "r67", "r68", "r69", "r70", "r71",
  "r72", "r73", "r74", "r75", "r76", "r77", "r78", "r79",
  "r80", "r81", "r82", "r83", "r84", "r85", "r86", "r87",
  "r88", "r89", "r90", "r91", "r92", "r93", "r94", "r95",
  "r96", "r97", "r98", "r99", "r100","r101","r102","r103",
  "r104","r105","r106","r107","r108","r109","r110","r111",
  "r112","r113","r114","r115","r116","r117","r118","r119",
  "r120","r121","r122","r123","r124","r125","r126","r127"};

/* ??? These strings could be shared with REGISTER_NAMES.  */
static const char * const ia64_input_reg_names[8] =
{ "in0",  "in1",  "in2",  "in3",  "in4",  "in5",  "in6",  "in7" };

/* ??? These strings could be shared with REGISTER_NAMES.  */
static const char * const ia64_local_reg_names[80] =
{ "loc0", "loc1", "loc2", "loc3", "loc4", "loc5", "loc6", "loc7",
  "loc8", "loc9", "loc10","loc11","loc12","loc13","loc14","loc15",
  "loc16","loc17","loc18","loc19","loc20","loc21","loc22","loc23",
  "loc24","loc25","loc26","loc27","loc28","loc29","loc30","loc31",
  "loc32","loc33","loc34","loc35","loc36","loc37","loc38","loc39",
  "loc40","loc41","loc42","loc43","loc44","loc45","loc46","loc47",
  "loc48","loc49","loc50","loc51","loc52","loc53","loc54","loc55",
  "loc56","loc57","loc58","loc59","loc60","loc61","loc62","loc63",
  "loc64","loc65","loc66","loc67","loc68","loc69","loc70","loc71",
  "loc72","loc73","loc74","loc75","loc76","loc77","loc78","loc79" };

/* ??? These strings could be shared with REGISTER_NAMES.  */
static const char * const ia64_output_reg_names[8] =
{ "out0", "out1", "out2", "out3", "out4", "out5", "out6", "out7" };

/* Which cpu are we scheduling for.  */
enum processor_type ia64_tune = PROCESSOR_ITANIUM2;

/* Determines whether we run our final scheduling pass or not.  We always
   avoid the normal second scheduling pass.  */
static int ia64_flag_schedule_insns2;

/* Determines whether we run variable tracking in machine dependent
   reorganization.  */
static int ia64_flag_var_tracking;

/* Variables which are this size or smaller are put in the sdata/sbss
   sections.  */

unsigned int ia64_section_threshold;

/* The following variable is used by the DFA insn scheduler.  The value is
   TRUE if we do insn bundling instead of insn scheduling.  */
int bundling_p = 0;

/* Structure to be filled in by ia64_compute_frame_size with register
   save masks and offsets for the current function.  */

struct ia64_frame_info
{
  HOST_WIDE_INT total_size;	/* size of the stack frame, not including
				   the caller's scratch area.  */
  HOST_WIDE_INT spill_cfa_off;	/* top of the reg spill area from the cfa.  */
  HOST_WIDE_INT spill_size;	/* size of the gr/br/fr spill area.  */
  HOST_WIDE_INT extra_spill_size;  /* size of spill area for others.  */
  HARD_REG_SET mask;		/* mask of saved registers.  */
  unsigned int gr_used_mask;	/* mask of registers in use as gr spill
				   registers or long-term scratches.  */
  int n_spilled;		/* number of spilled registers.  */
  int reg_fp;			/* register for fp.  */
  int reg_save_b0;		/* save register for b0.  */
  int reg_save_pr;		/* save register for prs.  */
  int reg_save_ar_pfs;		/* save register for ar.pfs.  */
  int reg_save_ar_unat;		/* save register for ar.unat.  */
  int reg_save_ar_lc;		/* save register for ar.lc.  */
  int reg_save_gp;		/* save register for gp.  */
  int n_input_regs;		/* number of input registers used.  */
  int n_local_regs;		/* number of local registers used.  */
  int n_output_regs;		/* number of output registers used.  */
  int n_rotate_regs;		/* number of rotating registers used.  */

  char need_regstk;		/* true if a .regstk directive needed.  */
  char initialized;		/* true if the data is finalized.  */
};

/* Current frame information calculated by ia64_compute_frame_size.  */
static struct ia64_frame_info current_frame_info;

static int ia64_first_cycle_multipass_dfa_lookahead (void);
static void ia64_dependencies_evaluation_hook (rtx, rtx);
static void ia64_init_dfa_pre_cycle_insn (void);
static rtx ia64_dfa_pre_cycle_insn (void);
static int ia64_first_cycle_multipass_dfa_lookahead_guard (rtx);
static bool ia64_first_cycle_multipass_dfa_lookahead_guard_spec (rtx);
static int ia64_dfa_new_cycle (FILE *, int, rtx, int, int, int *);
static void ia64_h_i_d_extended (void);
static int ia64_mode_to_int (enum machine_mode);
static void ia64_set_sched_flags (spec_info_t);
static int ia64_speculate_insn (rtx, ds_t, rtx *);
static rtx ia64_gen_spec_insn (rtx, ds_t, int, bool, bool);
static bool ia64_needs_block_p (rtx);
static rtx ia64_gen_check (rtx, rtx, bool);
static int ia64_spec_check_p (rtx);
static int ia64_spec_check_src_p (rtx);
static rtx gen_tls_get_addr (void);
static rtx gen_thread_pointer (void);
static int find_gr_spill (int);
static int next_scratch_gr_reg (void);
static void mark_reg_gr_used_mask (rtx, void *);
static void ia64_compute_frame_size (HOST_WIDE_INT);
static void setup_spill_pointers (int, rtx, HOST_WIDE_INT);
static void finish_spill_pointers (void);
static rtx spill_restore_mem (rtx, HOST_WIDE_INT);
static void do_spill (rtx (*)(rtx, rtx, rtx), rtx, HOST_WIDE_INT, rtx);
static void do_restore (rtx (*)(rtx, rtx, rtx), rtx, HOST_WIDE_INT);
static rtx gen_movdi_x (rtx, rtx, rtx);
static rtx gen_fr_spill_x (rtx, rtx, rtx);
static rtx gen_fr_restore_x (rtx, rtx, rtx);

static enum machine_mode hfa_element_mode (tree, bool);
static void ia64_setup_incoming_varargs (CUMULATIVE_ARGS *, enum machine_mode,
					 tree, int *, int);
static int ia64_arg_partial_bytes (CUMULATIVE_ARGS *, enum machine_mode,
				   tree, bool);
static bool ia64_function_ok_for_sibcall (tree, tree);
static bool ia64_return_in_memory (tree, tree);
static bool ia64_rtx_costs (rtx, int, int, int *);
static void fix_range (const char *);
static bool ia64_handle_option (size_t, const char *, int);
static struct machine_function * ia64_init_machine_status (void);
static void emit_insn_group_barriers (FILE *);
static void emit_all_insn_group_barriers (FILE *);
static void final_emit_insn_group_barriers (FILE *);
static void emit_predicate_relation_info (void);
static void ia64_reorg (void);
static bool ia64_in_small_data_p (tree);
static void process_epilogue (FILE *, rtx, bool, bool);
static int process_set (FILE *, rtx, rtx, bool, bool);

static bool ia64_assemble_integer (rtx, unsigned int, int);
static void ia64_output_function_prologue (FILE *, HOST_WIDE_INT);
static void ia64_output_function_epilogue (FILE *, HOST_WIDE_INT);
static void ia64_output_function_end_prologue (FILE *);

static int ia64_issue_rate (void);
static int ia64_adjust_cost_2 (rtx, int, rtx, int);
static void ia64_sched_init (FILE *, int, int);
static void ia64_sched_init_global (FILE *, int, int);
static void ia64_sched_finish_global (FILE *, int);
static void ia64_sched_finish (FILE *, int);
static int ia64_dfa_sched_reorder (FILE *, int, rtx *, int *, int, int);
static int ia64_sched_reorder (FILE *, int, rtx *, int *, int);
static int ia64_sched_reorder2 (FILE *, int, rtx *, int *, int);
static int ia64_variable_issue (FILE *, int, rtx, int);

static struct bundle_state *get_free_bundle_state (void);
static void free_bundle_state (struct bundle_state *);
static void initiate_bundle_states (void);
static void finish_bundle_states (void);
static unsigned bundle_state_hash (const void *);
static int bundle_state_eq_p (const void *, const void *);
static int insert_bundle_state (struct bundle_state *);
static void initiate_bundle_state_table (void);
static void finish_bundle_state_table (void);
static int try_issue_nops (struct bundle_state *, int);
static int try_issue_insn (struct bundle_state *, rtx);
static void issue_nops_and_insn (struct bundle_state *, int, rtx, int, int);
static int get_max_pos (state_t);
static int get_template (state_t, int);

static rtx get_next_important_insn (rtx, rtx);
static void bundling (FILE *, int, rtx, rtx);

static void ia64_output_mi_thunk (FILE *, tree, HOST_WIDE_INT,
				  HOST_WIDE_INT, tree);
static void ia64_file_start (void);

static int ia64_hpux_reloc_rw_mask (void) ATTRIBUTE_UNUSED;
static int ia64_reloc_rw_mask (void) ATTRIBUTE_UNUSED;
static section *ia64_select_rtx_section (enum machine_mode, rtx,
					 unsigned HOST_WIDE_INT);
static void ia64_output_dwarf_dtprel (FILE *, int, rtx)
     ATTRIBUTE_UNUSED;
static unsigned int ia64_section_type_flags (tree, const char *, int);
static void ia64_init_libfuncs (void)
     ATTRIBUTE_UNUSED;
static void ia64_hpux_init_libfuncs (void)
     ATTRIBUTE_UNUSED;
static void ia64_sysv4_init_libfuncs (void)
     ATTRIBUTE_UNUSED;
static void ia64_vms_init_libfuncs (void)
     ATTRIBUTE_UNUSED;

static tree ia64_handle_model_attribute (tree *, tree, tree, int, bool *);
static void ia64_encode_section_info (tree, rtx, int);
static rtx ia64_struct_value_rtx (tree, int);
static tree ia64_gimplify_va_arg (tree, tree, tree *, tree *);
static bool ia64_scalar_mode_supported_p (enum machine_mode mode);
static bool ia64_vector_mode_supported_p (enum machine_mode mode);
static bool ia64_cannot_force_const_mem (rtx);
static const char *ia64_mangle_fundamental_type (tree);
static const char *ia64_invalid_conversion (tree, tree);
static const char *ia64_invalid_unary_op (int, tree);
static const char *ia64_invalid_binary_op (int, tree, tree);

/* Table of valid machine attributes.  */
static const struct attribute_spec ia64_attribute_table[] =
{
  /* { name, min_len, max_len, decl_req, type_req, fn_type_req, handler } */
  { "syscall_linkage", 0, 0, false, true,  true,  NULL },
  { "model",	       1, 1, true, false, false, ia64_handle_model_attribute },
  { NULL,	       0, 0, false, false, false, NULL }
};

/* Initialize the GCC target structure.  */
#undef TARGET_ATTRIBUTE_TABLE
#define TARGET_ATTRIBUTE_TABLE ia64_attribute_table

#undef TARGET_INIT_BUILTINS
#define TARGET_INIT_BUILTINS ia64_init_builtins

#undef TARGET_EXPAND_BUILTIN
#define TARGET_EXPAND_BUILTIN ia64_expand_builtin

#undef TARGET_ASM_BYTE_OP
#define TARGET_ASM_BYTE_OP "\tdata1\t"
#undef TARGET_ASM_ALIGNED_HI_OP
#define TARGET_ASM_ALIGNED_HI_OP "\tdata2\t"
#undef TARGET_ASM_ALIGNED_SI_OP
#define TARGET_ASM_ALIGNED_SI_OP "\tdata4\t"
#undef TARGET_ASM_ALIGNED_DI_OP
#define TARGET_ASM_ALIGNED_DI_OP "\tdata8\t"
#undef TARGET_ASM_UNALIGNED_HI_OP
#define TARGET_ASM_UNALIGNED_HI_OP "\tdata2.ua\t"
#undef TARGET_ASM_UNALIGNED_SI_OP
#define TARGET_ASM_UNALIGNED_SI_OP "\tdata4.ua\t"
#undef TARGET_ASM_UNALIGNED_DI_OP
#define TARGET_ASM_UNALIGNED_DI_OP "\tdata8.ua\t"
#undef TARGET_ASM_INTEGER
#define TARGET_ASM_INTEGER ia64_assemble_integer

#undef TARGET_ASM_FUNCTION_PROLOGUE
#define TARGET_ASM_FUNCTION_PROLOGUE ia64_output_function_prologue
#undef TARGET_ASM_FUNCTION_END_PROLOGUE
#define TARGET_ASM_FUNCTION_END_PROLOGUE ia64_output_function_end_prologue
#undef TARGET_ASM_FUNCTION_EPILOGUE
#define TARGET_ASM_FUNCTION_EPILOGUE ia64_output_function_epilogue

#undef TARGET_IN_SMALL_DATA_P
#define TARGET_IN_SMALL_DATA_P  ia64_in_small_data_p

#undef TARGET_SCHED_ADJUST_COST_2
#define TARGET_SCHED_ADJUST_COST_2 ia64_adjust_cost_2
#undef TARGET_SCHED_ISSUE_RATE
#define TARGET_SCHED_ISSUE_RATE ia64_issue_rate
#undef TARGET_SCHED_VARIABLE_ISSUE
#define TARGET_SCHED_VARIABLE_ISSUE ia64_variable_issue
#undef TARGET_SCHED_INIT
#define TARGET_SCHED_INIT ia64_sched_init
#undef TARGET_SCHED_FINISH
#define TARGET_SCHED_FINISH ia64_sched_finish
#undef TARGET_SCHED_INIT_GLOBAL
#define TARGET_SCHED_INIT_GLOBAL ia64_sched_init_global
#undef TARGET_SCHED_FINISH_GLOBAL
#define TARGET_SCHED_FINISH_GLOBAL ia64_sched_finish_global
#undef TARGET_SCHED_REORDER
#define TARGET_SCHED_REORDER ia64_sched_reorder
#undef TARGET_SCHED_REORDER2
#define TARGET_SCHED_REORDER2 ia64_sched_reorder2

#undef TARGET_SCHED_DEPENDENCIES_EVALUATION_HOOK
#define TARGET_SCHED_DEPENDENCIES_EVALUATION_HOOK ia64_dependencies_evaluation_hook

#undef TARGET_SCHED_FIRST_CYCLE_MULTIPASS_DFA_LOOKAHEAD
#define TARGET_SCHED_FIRST_CYCLE_MULTIPASS_DFA_LOOKAHEAD ia64_first_cycle_multipass_dfa_lookahead

#undef TARGET_SCHED_INIT_DFA_PRE_CYCLE_INSN
#define TARGET_SCHED_INIT_DFA_PRE_CYCLE_INSN ia64_init_dfa_pre_cycle_insn
#undef TARGET_SCHED_DFA_PRE_CYCLE_INSN
#define TARGET_SCHED_DFA_PRE_CYCLE_INSN ia64_dfa_pre_cycle_insn

#undef TARGET_SCHED_FIRST_CYCLE_MULTIPASS_DFA_LOOKAHEAD_GUARD
#define TARGET_SCHED_FIRST_CYCLE_MULTIPASS_DFA_LOOKAHEAD_GUARD\
  ia64_first_cycle_multipass_dfa_lookahead_guard

#undef TARGET_SCHED_DFA_NEW_CYCLE
#define TARGET_SCHED_DFA_NEW_CYCLE ia64_dfa_new_cycle

#undef TARGET_SCHED_H_I_D_EXTENDED
#define TARGET_SCHED_H_I_D_EXTENDED ia64_h_i_d_extended

#undef TARGET_SCHED_SET_SCHED_FLAGS
#define TARGET_SCHED_SET_SCHED_FLAGS ia64_set_sched_flags

#undef TARGET_SCHED_SPECULATE_INSN
#define TARGET_SCHED_SPECULATE_INSN ia64_speculate_insn

#undef TARGET_SCHED_NEEDS_BLOCK_P
#define TARGET_SCHED_NEEDS_BLOCK_P ia64_needs_block_p

#undef TARGET_SCHED_GEN_CHECK
#define TARGET_SCHED_GEN_CHECK ia64_gen_check

#undef TARGET_SCHED_FIRST_CYCLE_MULTIPASS_DFA_LOOKAHEAD_GUARD_SPEC
#define TARGET_SCHED_FIRST_CYCLE_MULTIPASS_DFA_LOOKAHEAD_GUARD_SPEC\
  ia64_first_cycle_multipass_dfa_lookahead_guard_spec

#undef TARGET_FUNCTION_OK_FOR_SIBCALL
#define TARGET_FUNCTION_OK_FOR_SIBCALL ia64_function_ok_for_sibcall
#undef TARGET_ARG_PARTIAL_BYTES
#define TARGET_ARG_PARTIAL_BYTES ia64_arg_partial_bytes

#undef TARGET_ASM_OUTPUT_MI_THUNK
#define TARGET_ASM_OUTPUT_MI_THUNK ia64_output_mi_thunk
#undef TARGET_ASM_CAN_OUTPUT_MI_THUNK
#define TARGET_ASM_CAN_OUTPUT_MI_THUNK hook_bool_tree_hwi_hwi_tree_true

#undef TARGET_ASM_FILE_START
#define TARGET_ASM_FILE_START ia64_file_start

#undef TARGET_RTX_COSTS
#define TARGET_RTX_COSTS ia64_rtx_costs
#undef TARGET_ADDRESS_COST
#define TARGET_ADDRESS_COST hook_int_rtx_0

#undef TARGET_MACHINE_DEPENDENT_REORG
#define TARGET_MACHINE_DEPENDENT_REORG ia64_reorg

#undef TARGET_ENCODE_SECTION_INFO
#define TARGET_ENCODE_SECTION_INFO ia64_encode_section_info

#undef  TARGET_SECTION_TYPE_FLAGS
#define TARGET_SECTION_TYPE_FLAGS  ia64_section_type_flags

#ifdef HAVE_AS_TLS
#undef TARGET_ASM_OUTPUT_DWARF_DTPREL
#define TARGET_ASM_OUTPUT_DWARF_DTPREL ia64_output_dwarf_dtprel
#endif

/* ??? ABI doesn't allow us to define this.  */
#if 0
#undef TARGET_PROMOTE_FUNCTION_ARGS
#define TARGET_PROMOTE_FUNCTION_ARGS hook_bool_tree_true
#endif

/* ??? ABI doesn't allow us to define this.  */
#if 0
#undef TARGET_PROMOTE_FUNCTION_RETURN
#define TARGET_PROMOTE_FUNCTION_RETURN hook_bool_tree_true
#endif

/* ??? Investigate.  */
#if 0
#undef TARGET_PROMOTE_PROTOTYPES
#define TARGET_PROMOTE_PROTOTYPES hook_bool_tree_true
#endif

#undef TARGET_STRUCT_VALUE_RTX
#define TARGET_STRUCT_VALUE_RTX ia64_struct_value_rtx
#undef TARGET_RETURN_IN_MEMORY
#define TARGET_RETURN_IN_MEMORY ia64_return_in_memory
#undef TARGET_SETUP_INCOMING_VARARGS
#define TARGET_SETUP_INCOMING_VARARGS ia64_setup_incoming_varargs
#undef TARGET_STRICT_ARGUMENT_NAMING
#define TARGET_STRICT_ARGUMENT_NAMING hook_bool_CUMULATIVE_ARGS_true
#undef TARGET_MUST_PASS_IN_STACK
#define TARGET_MUST_PASS_IN_STACK must_pass_in_stack_var_size

#undef TARGET_GIMPLIFY_VA_ARG_EXPR
#define TARGET_GIMPLIFY_VA_ARG_EXPR ia64_gimplify_va_arg

#undef TARGET_UNWIND_EMIT
#define TARGET_UNWIND_EMIT process_for_unwind_directive

#undef TARGET_SCALAR_MODE_SUPPORTED_P
#define TARGET_SCALAR_MODE_SUPPORTED_P ia64_scalar_mode_supported_p
#undef TARGET_VECTOR_MODE_SUPPORTED_P
#define TARGET_VECTOR_MODE_SUPPORTED_P ia64_vector_mode_supported_p

/* ia64 architecture manual 4.4.7: ... reads, writes, and flushes may occur
   in an order different from the specified program order.  */
#undef TARGET_RELAXED_ORDERING
#define TARGET_RELAXED_ORDERING true

#undef TARGET_DEFAULT_TARGET_FLAGS
#define TARGET_DEFAULT_TARGET_FLAGS (TARGET_DEFAULT | TARGET_CPU_DEFAULT)
#undef TARGET_HANDLE_OPTION
#define TARGET_HANDLE_OPTION ia64_handle_option

#undef TARGET_CANNOT_FORCE_CONST_MEM
#define TARGET_CANNOT_FORCE_CONST_MEM ia64_cannot_force_const_mem

#undef TARGET_MANGLE_FUNDAMENTAL_TYPE
#define TARGET_MANGLE_FUNDAMENTAL_TYPE ia64_mangle_fundamental_type

#undef TARGET_INVALID_CONVERSION
#define TARGET_INVALID_CONVERSION ia64_invalid_conversion
#undef TARGET_INVALID_UNARY_OP
#define TARGET_INVALID_UNARY_OP ia64_invalid_unary_op
#undef TARGET_INVALID_BINARY_OP
#define TARGET_INVALID_BINARY_OP ia64_invalid_binary_op

struct gcc_target targetm = TARGET_INITIALIZER;

typedef enum
  {
    ADDR_AREA_NORMAL,	/* normal address area */
    ADDR_AREA_SMALL	/* addressable by "addl" (-2MB < addr < 2MB) */
  }
ia64_addr_area;

static GTY(()) tree small_ident1;
static GTY(()) tree small_ident2;

static void
init_idents (void)
{
  if (small_ident1 == 0)
    {
      small_ident1 = get_identifier ("small");
      small_ident2 = get_identifier ("__small__");
    }
}

/* Retrieve the address area that has been chosen for the given decl.  */

static ia64_addr_area
ia64_get_addr_area (tree decl)
{
  tree model_attr;

  model_attr = lookup_attribute ("model", DECL_ATTRIBUTES (decl));
  if (model_attr)
    {
      tree id;

      init_idents ();
      id = TREE_VALUE (TREE_VALUE (model_attr));
      if (id == small_ident1 || id == small_ident2)
	return ADDR_AREA_SMALL;
    }
  return ADDR_AREA_NORMAL;
}

static tree
ia64_handle_model_attribute (tree *node, tree name, tree args,
			     int flags ATTRIBUTE_UNUSED, bool *no_add_attrs)
{
  ia64_addr_area addr_area = ADDR_AREA_NORMAL;
  ia64_addr_area area;
  tree arg, decl = *node;

  init_idents ();
  arg = TREE_VALUE (args);
  if (arg == small_ident1 || arg == small_ident2)
    {
      addr_area = ADDR_AREA_SMALL;
    }
  else
    {
      warning (OPT_Wattributes, "invalid argument of %qs attribute",
	       IDENTIFIER_POINTER (name));
      *no_add_attrs = true;
    }

  switch (TREE_CODE (decl))
    {
    case VAR_DECL:
      if ((DECL_CONTEXT (decl) && TREE_CODE (DECL_CONTEXT (decl))
	   == FUNCTION_DECL)
	  && !TREE_STATIC (decl))
	{
	  error ("%Jan address area attribute cannot be specified for "
		 "local variables", decl);
	  *no_add_attrs = true;
	}
      area = ia64_get_addr_area (decl);
      if (area != ADDR_AREA_NORMAL && addr_area != area)
	{
	  error ("address area of %q+D conflicts with previous "
		 "declaration", decl);
	  *no_add_attrs = true;
	}
      break;

    case FUNCTION_DECL:
      error ("%Jaddress area attribute cannot be specified for functions",
	     decl);
      *no_add_attrs = true;
      break;

    default:
      warning (OPT_Wattributes, "%qs attribute ignored",
	       IDENTIFIER_POINTER (name));
      *no_add_attrs = true;
      break;
    }

  return NULL_TREE;
}

static void
ia64_encode_addr_area (tree decl, rtx symbol)
{
  int flags;

  flags = SYMBOL_REF_FLAGS (symbol);
  switch (ia64_get_addr_area (decl))
    {
    case ADDR_AREA_NORMAL: break;
    case ADDR_AREA_SMALL: flags |= SYMBOL_FLAG_SMALL_ADDR; break;
    default: gcc_unreachable ();
    }
  SYMBOL_REF_FLAGS (symbol) = flags;
}

static void
ia64_encode_section_info (tree decl, rtx rtl, int first)
{
  default_encode_section_info (decl, rtl, first);

  /* Careful not to prod global register variables.  */
  if (TREE_CODE (decl) == VAR_DECL
      && GET_CODE (DECL_RTL (decl)) == MEM
      && GET_CODE (XEXP (DECL_RTL (decl), 0)) == SYMBOL_REF
      && (TREE_STATIC (decl) || DECL_EXTERNAL (decl)))
    ia64_encode_addr_area (decl, XEXP (rtl, 0));
}

/* Implement CONST_OK_FOR_LETTER_P.  */

bool
ia64_const_ok_for_letter_p (HOST_WIDE_INT value, char c)
{
  switch (c)
    {
    case 'I':
      return CONST_OK_FOR_I (value);
    case 'J':
      return CONST_OK_FOR_J (value);
    case 'K':
      return CONST_OK_FOR_K (value);
    case 'L':
      return CONST_OK_FOR_L (value);
    case 'M':
      return CONST_OK_FOR_M (value);
    case 'N':
      return CONST_OK_FOR_N (value);
    case 'O':
      return CONST_OK_FOR_O (value);
    case 'P':
      return CONST_OK_FOR_P (value);
    default:
      return false;
    }
}

/* Implement CONST_DOUBLE_OK_FOR_LETTER_P.  */

bool
ia64_const_double_ok_for_letter_p (rtx value, char c)
{
  switch (c)
    {
    case 'G':
      return CONST_DOUBLE_OK_FOR_G (value);
    default:
      return false;
    }
}

/* Implement EXTRA_CONSTRAINT.  */

bool
ia64_extra_constraint (rtx value, char c)
{
  switch (c)
    {
    case 'Q':
      /* Non-volatile memory for FP_REG loads/stores.  */
      return memory_operand(value, VOIDmode) && !MEM_VOLATILE_P (value);

    case 'R':
      /* 1..4 for shladd arguments.  */
      return (GET_CODE (value) == CONST_INT
	      && INTVAL (value) >= 1 && INTVAL (value) <= 4);

    case 'S':
      /* Non-post-inc memory for asms and other unsavory creatures.  */
      return (GET_CODE (value) == MEM
	      && GET_RTX_CLASS (GET_CODE (XEXP (value, 0))) != RTX_AUTOINC
	      && (reload_in_progress || memory_operand (value, VOIDmode)));

    case 'T':
      /* Symbol ref to small-address-area.  */
      return small_addr_symbolic_operand (value, VOIDmode);

    case 'U':
      /* Vector zero.  */
      return value == CONST0_RTX (GET_MODE (value));

    case 'W':
      /* An integer vector, such that conversion to an integer yields a
	 value appropriate for an integer 'J' constraint.  */
      if (GET_CODE (value) == CONST_VECTOR
	  && GET_MODE_CLASS (GET_MODE (value)) == MODE_VECTOR_INT)
	{
	  value = simplify_subreg (DImode, value, GET_MODE (value), 0);
	  return ia64_const_ok_for_letter_p (INTVAL (value), 'J');
	}
      return false;

    case 'Y':
      /* A V2SF vector containing elements that satisfy 'G'.  */
      return
	(GET_CODE (value) == CONST_VECTOR
	 && GET_MODE (value) == V2SFmode
	 && ia64_const_double_ok_for_letter_p (XVECEXP (value, 0, 0), 'G')
	 && ia64_const_double_ok_for_letter_p (XVECEXP (value, 0, 1), 'G'));

    default:
      return false;
    }
}

/* Return 1 if the operands of a move are ok.  */

int
ia64_move_ok (rtx dst, rtx src)
{
  /* If we're under init_recog_no_volatile, we'll not be able to use
     memory_operand.  So check the code directly and don't worry about
     the validity of the underlying address, which should have been
     checked elsewhere anyway.  */
  if (GET_CODE (dst) != MEM)
    return 1;
  if (GET_CODE (src) == MEM)
    return 0;
  if (register_operand (src, VOIDmode))
    return 1;

  /* Otherwise, this must be a constant, and that either 0 or 0.0 or 1.0.  */
  if (INTEGRAL_MODE_P (GET_MODE (dst)))
    return src == const0_rtx;
  else
    return GET_CODE (src) == CONST_DOUBLE && CONST_DOUBLE_OK_FOR_G (src);
}

/* Return 1 if the operands are ok for a floating point load pair.  */

int
ia64_load_pair_ok (rtx dst, rtx src)
{
  if (GET_CODE (dst) != REG || !FP_REGNO_P (REGNO (dst)))
    return 0;
  if (GET_CODE (src) != MEM || MEM_VOLATILE_P (src))
    return 0;
  switch (GET_CODE (XEXP (src, 0)))
    {
    case REG:
    case POST_INC:
      break;
    case POST_DEC:
      return 0;
    case POST_MODIFY:
      {
	rtx adjust = XEXP (XEXP (XEXP (src, 0), 1), 1);

	if (GET_CODE (adjust) != CONST_INT
	    || INTVAL (adjust) != GET_MODE_SIZE (GET_MODE (src)))
	  return 0;
      }
      break;
    default:
      abort ();
    }
  return 1;
}

int
addp4_optimize_ok (rtx op1, rtx op2)
{
  return (basereg_operand (op1, GET_MODE(op1)) !=
	  basereg_operand (op2, GET_MODE(op2)));
}

/* Check if OP is a mask suitable for use with SHIFT in a dep.z instruction.
   Return the length of the field, or <= 0 on failure.  */

int
ia64_depz_field_mask (rtx rop, rtx rshift)
{
  unsigned HOST_WIDE_INT op = INTVAL (rop);
  unsigned HOST_WIDE_INT shift = INTVAL (rshift);

  /* Get rid of the zero bits we're shifting in.  */
  op >>= shift;

  /* We must now have a solid block of 1's at bit 0.  */
  return exact_log2 (op + 1);
}

/* Return the TLS model to use for ADDR.  */

static enum tls_model
tls_symbolic_operand_type (rtx addr)
{
  enum tls_model tls_kind = 0;

  if (GET_CODE (addr) == CONST)
    {
      if (GET_CODE (XEXP (addr, 0)) == PLUS
	  && GET_CODE (XEXP (XEXP (addr, 0), 0)) == SYMBOL_REF)
        tls_kind = SYMBOL_REF_TLS_MODEL (XEXP (XEXP (addr, 0), 0));
    }
  else if (GET_CODE (addr) == SYMBOL_REF)
    tls_kind = SYMBOL_REF_TLS_MODEL (addr);

  return tls_kind;
}

/* Return true if X is a constant that is valid for some immediate
   field in an instruction.  */

bool
ia64_legitimate_constant_p (rtx x)
{
  switch (GET_CODE (x))
    {
    case CONST_INT:
    case LABEL_REF:
      return true;

    case CONST_DOUBLE:
      if (GET_MODE (x) == VOIDmode)
	return true;
      return CONST_DOUBLE_OK_FOR_G (x);

    case CONST:
    case SYMBOL_REF:
      /* ??? Short term workaround for PR 28490.  We must make the code here
	 match the code in ia64_expand_move and move_operand, even though they
	 are both technically wrong.  */
      if (tls_symbolic_operand_type (x) == 0)
	{
	  HOST_WIDE_INT addend = 0;
	  rtx op = x;

	  if (GET_CODE (op) == CONST
	      && GET_CODE (XEXP (op, 0)) == PLUS
	      && GET_CODE (XEXP (XEXP (op, 0), 1)) == CONST_INT)
	    {
	      addend = INTVAL (XEXP (XEXP (op, 0), 1));
	      op = XEXP (XEXP (op, 0), 0);
	    }

          if (any_offset_symbol_operand (op, GET_MODE (op))
              || function_operand (op, GET_MODE (op)))
            return true;
	  if (aligned_offset_symbol_operand (op, GET_MODE (op)))
	    return (addend & 0x3fff) == 0;
	  return false;
	}
      return false;

    case CONST_VECTOR:
      {
	enum machine_mode mode = GET_MODE (x);

	if (mode == V2SFmode)
	  return ia64_extra_constraint (x, 'Y');

	return (GET_MODE_CLASS (mode) == MODE_VECTOR_INT
		&& GET_MODE_SIZE (mode) <= 8);
      }

    default:
      return false;
    }
}

/* Don't allow TLS addresses to get spilled to memory.  */

static bool
ia64_cannot_force_const_mem (rtx x)
{
  return tls_symbolic_operand_type (x) != 0;
}

/* Expand a symbolic constant load.  */

bool
ia64_expand_load_address (rtx dest, rtx src)
{
  gcc_assert (GET_CODE (dest) == REG);

  /* ILP32 mode still loads 64-bits of data from the GOT.  This avoids
     having to pointer-extend the value afterward.  Other forms of address
     computation below are also more natural to compute as 64-bit quantities.
     If we've been given an SImode destination register, change it.  */
  if (GET_MODE (dest) != Pmode)
    dest = gen_rtx_REG_offset (dest, Pmode, REGNO (dest), 0);

  if (TARGET_NO_PIC)
    return false;
  if (small_addr_symbolic_operand (src, VOIDmode))
    return false;

  if (TARGET_AUTO_PIC)
    emit_insn (gen_load_gprel64 (dest, src));
  else if (GET_CODE (src) == SYMBOL_REF && SYMBOL_REF_FUNCTION_P (src))
    emit_insn (gen_load_fptr (dest, src));
  else if (sdata_symbolic_operand (src, VOIDmode))
    emit_insn (gen_load_gprel (dest, src));
  else
    {
      HOST_WIDE_INT addend = 0;
      rtx tmp;

      /* We did split constant offsets in ia64_expand_move, and we did try
	 to keep them split in move_operand, but we also allowed reload to
	 rematerialize arbitrary constants rather than spill the value to
	 the stack and reload it.  So we have to be prepared here to split
	 them apart again.  */
      if (GET_CODE (src) == CONST)
	{
	  HOST_WIDE_INT hi, lo;

	  hi = INTVAL (XEXP (XEXP (src, 0), 1));
	  lo = ((hi & 0x3fff) ^ 0x2000) - 0x2000;
	  hi = hi - lo;

	  if (lo != 0)
	    {
	      addend = lo;
	      src = plus_constant (XEXP (XEXP (src, 0), 0), hi);
	    }
	}

      tmp = gen_rtx_HIGH (Pmode, src);
      tmp = gen_rtx_PLUS (Pmode, tmp, pic_offset_table_rtx);
      emit_insn (gen_rtx_SET (VOIDmode, dest, tmp));

      tmp = gen_rtx_LO_SUM (Pmode, dest, src);
      emit_insn (gen_rtx_SET (VOIDmode, dest, tmp));

      if (addend)
	{
	  tmp = gen_rtx_PLUS (Pmode, dest, GEN_INT (addend));
	  emit_insn (gen_rtx_SET (VOIDmode, dest, tmp));
	}
    }

  return true;
}

static GTY(()) rtx gen_tls_tga;
static rtx
gen_tls_get_addr (void)
{
  if (!gen_tls_tga)
    gen_tls_tga = init_one_libfunc ("__tls_get_addr");
  return gen_tls_tga;
}

static GTY(()) rtx thread_pointer_rtx;
static rtx
gen_thread_pointer (void)
{
  if (!thread_pointer_rtx)
    thread_pointer_rtx = gen_rtx_REG (Pmode, 13);
  return thread_pointer_rtx;
}

static rtx
ia64_expand_tls_address (enum tls_model tls_kind, rtx op0, rtx op1,
			 rtx orig_op1, HOST_WIDE_INT addend)
{
  rtx tga_op1, tga_op2, tga_ret, tga_eqv, tmp, insns;
  rtx orig_op0 = op0;
  HOST_WIDE_INT addend_lo, addend_hi;

  switch (tls_kind)
    {
    case TLS_MODEL_GLOBAL_DYNAMIC:
      start_sequence ();

      tga_op1 = gen_reg_rtx (Pmode);
      emit_insn (gen_load_dtpmod (tga_op1, op1));

      tga_op2 = gen_reg_rtx (Pmode);
      emit_insn (gen_load_dtprel (tga_op2, op1));

      tga_ret = emit_library_call_value (gen_tls_get_addr (), NULL_RTX,
					 LCT_CONST, Pmode, 2, tga_op1,
					 Pmode, tga_op2, Pmode);

      insns = get_insns ();
      end_sequence ();

      if (GET_MODE (op0) != Pmode)
	op0 = tga_ret;
      emit_libcall_block (insns, op0, tga_ret, op1);
      break;

    case TLS_MODEL_LOCAL_DYNAMIC:
      /* ??? This isn't the completely proper way to do local-dynamic
	 If the call to __tls_get_addr is used only by a single symbol,
	 then we should (somehow) move the dtprel to the second arg
	 to avoid the extra add.  */
      start_sequence ();

      tga_op1 = gen_reg_rtx (Pmode);
      emit_insn (gen_load_dtpmod (tga_op1, op1));

      tga_op2 = const0_rtx;

      tga_ret = emit_library_call_value (gen_tls_get_addr (), NULL_RTX,
					 LCT_CONST, Pmode, 2, tga_op1,
					 Pmode, tga_op2, Pmode);

      insns = get_insns ();
      end_sequence ();

      tga_eqv = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, const0_rtx),
				UNSPEC_LD_BASE);
      tmp = gen_reg_rtx (Pmode);
      emit_libcall_block (insns, tmp, tga_ret, tga_eqv);

      if (!register_operand (op0, Pmode))
	op0 = gen_reg_rtx (Pmode);
      if (TARGET_TLS64)
	{
	  emit_insn (gen_load_dtprel (op0, op1));
	  emit_insn (gen_adddi3 (op0, tmp, op0));
	}
      else
	emit_insn (gen_add_dtprel (op0, op1, tmp));
      break;

    case TLS_MODEL_INITIAL_EXEC:
      addend_lo = ((addend & 0x3fff) ^ 0x2000) - 0x2000;
      addend_hi = addend - addend_lo;

      op1 = plus_constant (op1, addend_hi);
      addend = addend_lo;

      tmp = gen_reg_rtx (Pmode);
      emit_insn (gen_load_tprel (tmp, op1));

      if (!register_operand (op0, Pmode))
	op0 = gen_reg_rtx (Pmode);
      emit_insn (gen_adddi3 (op0, tmp, gen_thread_pointer ()));
      break;

    case TLS_MODEL_LOCAL_EXEC:
      if (!register_operand (op0, Pmode))
	op0 = gen_reg_rtx (Pmode);

      op1 = orig_op1;
      addend = 0;
      if (TARGET_TLS64)
	{
	  emit_insn (gen_load_tprel (op0, op1));
	  emit_insn (gen_adddi3 (op0, op0, gen_thread_pointer ()));
	}
      else
	emit_insn (gen_add_tprel (op0, op1, gen_thread_pointer ()));
      break;

    default:
      gcc_unreachable ();
    }

  if (addend)
    op0 = expand_simple_binop (Pmode, PLUS, op0, GEN_INT (addend),
			       orig_op0, 1, OPTAB_DIRECT);
  if (orig_op0 == op0)
    return NULL_RTX;
  if (GET_MODE (orig_op0) == Pmode)
    return op0;
  return gen_lowpart (GET_MODE (orig_op0), op0);
}

rtx
ia64_expand_move (rtx op0, rtx op1)
{
  enum machine_mode mode = GET_MODE (op0);

  if (!reload_in_progress && !reload_completed && !ia64_move_ok (op0, op1))
    op1 = force_reg (mode, op1);

  if ((mode == Pmode || mode == ptr_mode) && symbolic_operand (op1, VOIDmode))
    {
      HOST_WIDE_INT addend = 0;
      enum tls_model tls_kind;
      rtx sym = op1;

      if (GET_CODE (op1) == CONST
	  && GET_CODE (XEXP (op1, 0)) == PLUS
	  && GET_CODE (XEXP (XEXP (op1, 0), 1)) == CONST_INT)
	{
	  addend = INTVAL (XEXP (XEXP (op1, 0), 1));
	  sym = XEXP (XEXP (op1, 0), 0);
	}

      tls_kind = tls_symbolic_operand_type (sym);
      if (tls_kind)
	return ia64_expand_tls_address (tls_kind, op0, sym, op1, addend);

      if (any_offset_symbol_operand (sym, mode))
	addend = 0;
      else if (aligned_offset_symbol_operand (sym, mode))
	{
	  HOST_WIDE_INT addend_lo, addend_hi;
	      
	  addend_lo = ((addend & 0x3fff) ^ 0x2000) - 0x2000;
	  addend_hi = addend - addend_lo;

	  if (addend_lo != 0)
	    {
	      op1 = plus_constant (sym, addend_hi);
	      addend = addend_lo;
	    }
	  else
	    addend = 0;
	}
      else
	op1 = sym;

      if (reload_completed)
	{
	  /* We really should have taken care of this offset earlier.  */
	  gcc_assert (addend == 0);
	  if (ia64_expand_load_address (op0, op1))
	    return NULL_RTX;
	}

      if (addend)
	{
	  rtx subtarget = no_new_pseudos ? op0 : gen_reg_rtx (mode);

	  emit_insn (gen_rtx_SET (VOIDmode, subtarget, op1));

	  op1 = expand_simple_binop (mode, PLUS, subtarget,
				     GEN_INT (addend), op0, 1, OPTAB_DIRECT);
	  if (op0 == op1)
	    return NULL_RTX;
	}
    }

  return op1;
}

/* Split a move from OP1 to OP0 conditional on COND.  */

void
ia64_emit_cond_move (rtx op0, rtx op1, rtx cond)
{
  rtx insn, first = get_last_insn ();

  emit_move_insn (op0, op1);

  for (insn = get_last_insn (); insn != first; insn = PREV_INSN (insn))
    if (INSN_P (insn))
      PATTERN (insn) = gen_rtx_COND_EXEC (VOIDmode, copy_rtx (cond),
					  PATTERN (insn));
}

/* Split a post-reload TImode or TFmode reference into two DImode
   components.  This is made extra difficult by the fact that we do
   not get any scratch registers to work with, because reload cannot
   be prevented from giving us a scratch that overlaps the register
   pair involved.  So instead, when addressing memory, we tweak the
   pointer register up and back down with POST_INCs.  Or up and not
   back down when we can get away with it.

   REVERSED is true when the loads must be done in reversed order
   (high word first) for correctness.  DEAD is true when the pointer
   dies with the second insn we generate and therefore the second
   address must not carry a postmodify.

   May return an insn which is to be emitted after the moves.  */

static rtx
ia64_split_tmode (rtx out[2], rtx in, bool reversed, bool dead)
{
  rtx fixup = 0;

  switch (GET_CODE (in))
    {
    case REG:
      out[reversed] = gen_rtx_REG (DImode, REGNO (in));
      out[!reversed] = gen_rtx_REG (DImode, REGNO (in) + 1);
      break;

    case CONST_INT:
    case CONST_DOUBLE:
      /* Cannot occur reversed.  */
      gcc_assert (!reversed);
      
      if (GET_MODE (in) != TFmode)
	split_double (in, &out[0], &out[1]);
      else
	/* split_double does not understand how to split a TFmode
	   quantity into a pair of DImode constants.  */
	{
	  REAL_VALUE_TYPE r;
	  unsigned HOST_WIDE_INT p[2];
	  long l[4];  /* TFmode is 128 bits */

	  REAL_VALUE_FROM_CONST_DOUBLE (r, in);
	  real_to_target (l, &r, TFmode);

	  if (FLOAT_WORDS_BIG_ENDIAN)
	    {
	      p[0] = (((unsigned HOST_WIDE_INT) l[0]) << 32) + l[1];
	      p[1] = (((unsigned HOST_WIDE_INT) l[2]) << 32) + l[3];
	    }
	  else
	    {
	      p[0] = (((unsigned HOST_WIDE_INT) l[3]) << 32) + l[2];
	      p[1] = (((unsigned HOST_WIDE_INT) l[1]) << 32) + l[0];
	    }
	  out[0] = GEN_INT (p[0]);
	  out[1] = GEN_INT (p[1]);
	}
      break;

    case MEM:
      {
	rtx base = XEXP (in, 0);
	rtx offset;

	switch (GET_CODE (base))
	  {
	  case REG:
	    if (!reversed)
	      {
		out[0] = adjust_automodify_address
		  (in, DImode, gen_rtx_POST_INC (Pmode, base), 0);
		out[1] = adjust_automodify_address
		  (in, DImode, dead ? 0 : gen_rtx_POST_DEC (Pmode, base), 8);
	      }
	    else
	      {
		/* Reversal requires a pre-increment, which can only
		   be done as a separate insn.  */
		emit_insn (gen_adddi3 (base, base, GEN_INT (8)));
		out[0] = adjust_automodify_address
		  (in, DImode, gen_rtx_POST_DEC (Pmode, base), 8);
		out[1] = adjust_address (in, DImode, 0);
	      }
	    break;

	  case POST_INC:
	    gcc_assert (!reversed && !dead);
	    
	    /* Just do the increment in two steps.  */
	    out[0] = adjust_automodify_address (in, DImode, 0, 0);
	    out[1] = adjust_automodify_address (in, DImode, 0, 8);
	    break;

	  case POST_DEC:
	    gcc_assert (!reversed && !dead);
	    
	    /* Add 8, subtract 24.  */
	    base = XEXP (base, 0);
	    out[0] = adjust_automodify_address
	      (in, DImode, gen_rtx_POST_INC (Pmode, base), 0);
	    out[1] = adjust_automodify_address
	      (in, DImode,
	       gen_rtx_POST_MODIFY (Pmode, base, plus_constant (base, -24)),
	       8);
	    break;

	  case POST_MODIFY:
	    gcc_assert (!reversed && !dead);

	    /* Extract and adjust the modification.  This case is
	       trickier than the others, because we might have an
	       index register, or we might have a combined offset that
	       doesn't fit a signed 9-bit displacement field.  We can
	       assume the incoming expression is already legitimate.  */
	    offset = XEXP (base, 1);
	    base = XEXP (base, 0);

	    out[0] = adjust_automodify_address
	      (in, DImode, gen_rtx_POST_INC (Pmode, base), 0);

	    if (GET_CODE (XEXP (offset, 1)) == REG)
	      {
		/* Can't adjust the postmodify to match.  Emit the
		   original, then a separate addition insn.  */
		out[1] = adjust_automodify_address (in, DImode, 0, 8);
		fixup = gen_adddi3 (base, base, GEN_INT (-8));
	      }
	    else
	      {
		gcc_assert (GET_CODE (XEXP (offset, 1)) == CONST_INT);
		if (INTVAL (XEXP (offset, 1)) < -256 + 8)
		  {
		    /* Again the postmodify cannot be made to match,
		       but in this case it's more efficient to get rid
		       of the postmodify entirely and fix up with an
		       add insn.  */
		    out[1] = adjust_automodify_address (in, DImode, base, 8);
		    fixup = gen_adddi3
		      (base, base, GEN_INT (INTVAL (XEXP (offset, 1)) - 8));
		  }
		else
		  {
		    /* Combined offset still fits in the displacement field.
		       (We cannot overflow it at the high end.)  */
		    out[1] = adjust_automodify_address
		      (in, DImode, gen_rtx_POST_MODIFY
		       (Pmode, base, gen_rtx_PLUS
			(Pmode, base,
			 GEN_INT (INTVAL (XEXP (offset, 1)) - 8))),
		       8);
		  }
	      }
	    break;

	  default:
	    gcc_unreachable ();
	  }
	break;
      }

    default:
      gcc_unreachable ();
    }

  return fixup;
}

/* Split a TImode or TFmode move instruction after reload.
   This is used by *movtf_internal and *movti_internal.  */
void
ia64_split_tmode_move (rtx operands[])
{
  rtx in[2], out[2], insn;
  rtx fixup[2];
  bool dead = false;
  bool reversed = false;

  /* It is possible for reload to decide to overwrite a pointer with
     the value it points to.  In that case we have to do the loads in
     the appropriate order so that the pointer is not destroyed too
     early.  Also we must not generate a postmodify for that second
     load, or rws_access_regno will die.  */
  if (GET_CODE (operands[1]) == MEM
      && reg_overlap_mentioned_p (operands[0], operands[1]))
    {
      rtx base = XEXP (operands[1], 0);
      while (GET_CODE (base) != REG)
	base = XEXP (base, 0);

      if (REGNO (base) == REGNO (operands[0]))
	reversed = true;
      dead = true;
    }
  /* Another reason to do the moves in reversed order is if the first
     element of the target register pair is also the second element of
     the source register pair.  */
  if (GET_CODE (operands[0]) == REG && GET_CODE (operands[1]) == REG
      && REGNO (operands[0]) == REGNO (operands[1]) + 1)
    reversed = true;

  fixup[0] = ia64_split_tmode (in, operands[1], reversed, dead);
  fixup[1] = ia64_split_tmode (out, operands[0], reversed, dead);

#define MAYBE_ADD_REG_INC_NOTE(INSN, EXP)				\
  if (GET_CODE (EXP) == MEM						\
      && (GET_CODE (XEXP (EXP, 0)) == POST_MODIFY			\
	  || GET_CODE (XEXP (EXP, 0)) == POST_INC			\
	  || GET_CODE (XEXP (EXP, 0)) == POST_DEC))			\
    REG_NOTES (INSN) = gen_rtx_EXPR_LIST (REG_INC,			\
					  XEXP (XEXP (EXP, 0), 0),	\
					  REG_NOTES (INSN))

  insn = emit_insn (gen_rtx_SET (VOIDmode, out[0], in[0]));
  MAYBE_ADD_REG_INC_NOTE (insn, in[0]);
  MAYBE_ADD_REG_INC_NOTE (insn, out[0]);

  insn = emit_insn (gen_rtx_SET (VOIDmode, out[1], in[1]));
  MAYBE_ADD_REG_INC_NOTE (insn, in[1]);
  MAYBE_ADD_REG_INC_NOTE (insn, out[1]);

  if (fixup[0])
    emit_insn (fixup[0]);
  if (fixup[1])
    emit_insn (fixup[1]);

#undef MAYBE_ADD_REG_INC_NOTE
}

/* ??? Fixing GR->FR XFmode moves during reload is hard.  You need to go
   through memory plus an extra GR scratch register.  Except that you can
   either get the first from SECONDARY_MEMORY_NEEDED or the second from
   SECONDARY_RELOAD_CLASS, but not both.

   We got into problems in the first place by allowing a construct like
   (subreg:XF (reg:TI)), which we got from a union containing a long double.
   This solution attempts to prevent this situation from occurring.  When
   we see something like the above, we spill the inner register to memory.  */

static rtx
spill_xfmode_rfmode_operand (rtx in, int force, enum machine_mode mode)
{
  if (GET_CODE (in) == SUBREG
      && GET_MODE (SUBREG_REG (in)) == TImode
      && GET_CODE (SUBREG_REG (in)) == REG)
    {
      rtx memt = assign_stack_temp (TImode, 16, 0);
      emit_move_insn (memt, SUBREG_REG (in));
      return adjust_address (memt, mode, 0);
    }
  else if (force && GET_CODE (in) == REG)
    {
      rtx memx = assign_stack_temp (mode, 16, 0);
      emit_move_insn (memx, in);
      return memx;
    }
  else
    return in;
}

/* Expand the movxf or movrf pattern (MODE says which) with the given
   OPERANDS, returning true if the pattern should then invoke
   DONE.  */

bool
ia64_expand_movxf_movrf (enum machine_mode mode, rtx operands[])
{
  rtx op0 = operands[0];

  if (GET_CODE (op0) == SUBREG)
    op0 = SUBREG_REG (op0);

  /* We must support XFmode loads into general registers for stdarg/vararg,
     unprototyped calls, and a rare case where a long double is passed as
     an argument after a float HFA fills the FP registers.  We split them into
     DImode loads for convenience.  We also need to support XFmode stores
     for the last case.  This case does not happen for stdarg/vararg routines,
     because we do a block store to memory of unnamed arguments.  */

  if (GET_CODE (op0) == REG && GR_REGNO_P (REGNO (op0)))
    {
      rtx out[2];

      /* We're hoping to transform everything that deals with XFmode
	 quantities and GR registers early in the compiler.  */
      gcc_assert (!no_new_pseudos);

      /* Struct to register can just use TImode instead.  */
      if ((GET_CODE (operands[1]) == SUBREG
	   && GET_MODE (SUBREG_REG (operands[1])) == TImode)
	  || (GET_CODE (operands[1]) == REG
	      && GR_REGNO_P (REGNO (operands[1]))))
	{
	  rtx op1 = operands[1];

	  if (GET_CODE (op1) == SUBREG)
	    op1 = SUBREG_REG (op1);
	  else
	    op1 = gen_rtx_REG (TImode, REGNO (op1));

	  emit_move_insn (gen_rtx_REG (TImode, REGNO (op0)), op1);
	  return true;
	}

      if (GET_CODE (operands[1]) == CONST_DOUBLE)
	{
	  /* Don't word-swap when reading in the constant.  */
	  emit_move_insn (gen_rtx_REG (DImode, REGNO (op0)),
			  operand_subword (operands[1], WORDS_BIG_ENDIAN,
					   0, mode));
	  emit_move_insn (gen_rtx_REG (DImode, REGNO (op0) + 1),
			  operand_subword (operands[1], !WORDS_BIG_ENDIAN,
					   0, mode));
	  return true;
	}

      /* If the quantity is in a register not known to be GR, spill it.  */
      if (register_operand (operands[1], mode))
	operands[1] = spill_xfmode_rfmode_operand (operands[1], 1, mode);

      gcc_assert (GET_CODE (operands[1]) == MEM);

      /* Don't word-swap when reading in the value.  */
      out[0] = gen_rtx_REG (DImode, REGNO (op0));
      out[1] = gen_rtx_REG (DImode, REGNO (op0) + 1);

      emit_move_insn (out[0], adjust_address (operands[1], DImode, 0));
      emit_move_insn (out[1], adjust_address (operands[1], DImode, 8));
      return true;
    }

  if (GET_CODE (operands[1]) == REG && GR_REGNO_P (REGNO (operands[1])))
    {
      /* We're hoping to transform everything that deals with XFmode
	 quantities and GR registers early in the compiler.  */
      gcc_assert (!no_new_pseudos);

      /* Op0 can't be a GR_REG here, as that case is handled above.
	 If op0 is a register, then we spill op1, so that we now have a
	 MEM operand.  This requires creating an XFmode subreg of a TImode reg
	 to force the spill.  */
      if (register_operand (operands[0], mode))
	{
	  rtx op1 = gen_rtx_REG (TImode, REGNO (operands[1]));
	  op1 = gen_rtx_SUBREG (mode, op1, 0);
	  operands[1] = spill_xfmode_rfmode_operand (op1, 0, mode);
	}

      else
	{
	  rtx in[2];

	  gcc_assert (GET_CODE (operands[0]) == MEM);

	  /* Don't word-swap when writing out the value.  */
	  in[0] = gen_rtx_REG (DImode, REGNO (operands[1]));
	  in[1] = gen_rtx_REG (DImode, REGNO (operands[1]) + 1);

	  emit_move_insn (adjust_address (operands[0], DImode, 0), in[0]);
	  emit_move_insn (adjust_address (operands[0], DImode, 8), in[1]);
	  return true;
	}
    }

  if (!reload_in_progress && !reload_completed)
    {
      operands[1] = spill_xfmode_rfmode_operand (operands[1], 0, mode);

      if (GET_MODE (op0) == TImode && GET_CODE (op0) == REG)
	{
	  rtx memt, memx, in = operands[1];
	  if (CONSTANT_P (in))
	    in = validize_mem (force_const_mem (mode, in));
	  if (GET_CODE (in) == MEM)
	    memt = adjust_address (in, TImode, 0);
	  else
	    {
	      memt = assign_stack_temp (TImode, 16, 0);
	      memx = adjust_address (memt, mode, 0);
	      emit_move_insn (memx, in);
	    }
	  emit_move_insn (op0, memt);
	  return true;
	}

      if (!ia64_move_ok (operands[0], operands[1]))
	operands[1] = force_reg (mode, operands[1]);
    }

  return false;
}

/* Emit comparison instruction if necessary, returning the expression
   that holds the compare result in the proper mode.  */

static GTY(()) rtx cmptf_libfunc;

rtx
ia64_expand_compare (enum rtx_code code, enum machine_mode mode)
{
  rtx op0 = ia64_compare_op0, op1 = ia64_compare_op1;
  rtx cmp;

  /* If we have a BImode input, then we already have a compare result, and
     do not need to emit another comparison.  */
  if (GET_MODE (op0) == BImode)
    {
      gcc_assert ((code == NE || code == EQ) && op1 == const0_rtx);
      cmp = op0;
    }
  /* HPUX TFmode compare requires a library call to _U_Qfcmp, which takes a
     magic number as its third argument, that indicates what to do.
     The return value is an integer to be compared against zero.  */
  else if (GET_MODE (op0) == TFmode)
    {
      enum qfcmp_magic {
	QCMP_INV = 1,	/* Raise FP_INVALID on SNaN as a side effect.  */
	QCMP_UNORD = 2,
	QCMP_EQ = 4,
	QCMP_LT = 8,
	QCMP_GT = 16
      } magic;
      enum rtx_code ncode;
      rtx ret, insns;
      
      gcc_assert (cmptf_libfunc && GET_MODE (op1) == TFmode);
      switch (code)
	{
	  /* 1 = equal, 0 = not equal.  Equality operators do
	     not raise FP_INVALID when given an SNaN operand.  */
	case EQ:        magic = QCMP_EQ;                  ncode = NE; break;
	case NE:        magic = QCMP_EQ;                  ncode = EQ; break;
	  /* isunordered() from C99.  */
	case UNORDERED: magic = QCMP_UNORD;               ncode = NE; break;
	case ORDERED:   magic = QCMP_UNORD;               ncode = EQ; break;
	  /* Relational operators raise FP_INVALID when given
	     an SNaN operand.  */
	case LT:        magic = QCMP_LT        |QCMP_INV; ncode = NE; break;
	case LE:        magic = QCMP_LT|QCMP_EQ|QCMP_INV; ncode = NE; break;
	case GT:        magic = QCMP_GT        |QCMP_INV; ncode = NE; break;
	case GE:        magic = QCMP_GT|QCMP_EQ|QCMP_INV; ncode = NE; break;
	  /* FUTURE: Implement UNEQ, UNLT, UNLE, UNGT, UNGE, LTGT.
	     Expanders for buneq etc. weuld have to be added to ia64.md
	     for this to be useful.  */
	default: gcc_unreachable ();
	}

      start_sequence ();

      ret = emit_library_call_value (cmptf_libfunc, 0, LCT_CONST, DImode, 3,
				     op0, TFmode, op1, TFmode,
				     GEN_INT (magic), DImode);
      cmp = gen_reg_rtx (BImode);
      emit_insn (gen_rtx_SET (VOIDmode, cmp,
			      gen_rtx_fmt_ee (ncode, BImode,
					      ret, const0_rtx)));

      insns = get_insns ();
      end_sequence ();

      emit_libcall_block (insns, cmp, cmp,
			  gen_rtx_fmt_ee (code, BImode, op0, op1));
      code = NE;
    }
  else
    {
      cmp = gen_reg_rtx (BImode);
      emit_insn (gen_rtx_SET (VOIDmode, cmp,
			      gen_rtx_fmt_ee (code, BImode, op0, op1)));
      code = NE;
    }

  return gen_rtx_fmt_ee (code, mode, cmp, const0_rtx);
}

/* Generate an integral vector comparison.  Return true if the condition has
   been reversed, and so the sense of the comparison should be inverted.  */

static bool
ia64_expand_vecint_compare (enum rtx_code code, enum machine_mode mode,
			    rtx dest, rtx op0, rtx op1)
{
  bool negate = false;
  rtx x;

  /* Canonicalize the comparison to EQ, GT, GTU.  */
  switch (code)
    {
    case EQ:
    case GT:
    case GTU:
      break;

    case NE:
    case LE:
    case LEU:
      code = reverse_condition (code);
      negate = true;
      break;

    case GE:
    case GEU:
      code = reverse_condition (code);
      negate = true;
      /* FALLTHRU */

    case LT:
    case LTU:
      code = swap_condition (code);
      x = op0, op0 = op1, op1 = x;
      break;

    default:
      gcc_unreachable ();
    }

  /* Unsigned parallel compare is not supported by the hardware.  Play some
     tricks to turn this into a signed comparison against 0.  */
  if (code == GTU)
    {
      switch (mode)
	{
	case V2SImode:
	  {
	    rtx t1, t2, mask;

	    /* Perform a parallel modulo subtraction.  */
	    t1 = gen_reg_rtx (V2SImode);
	    emit_insn (gen_subv2si3 (t1, op0, op1));

	    /* Extract the original sign bit of op0.  */
	    mask = GEN_INT (-0x80000000);
	    mask = gen_rtx_CONST_VECTOR (V2SImode, gen_rtvec (2, mask, mask));
	    mask = force_reg (V2SImode, mask);
	    t2 = gen_reg_rtx (V2SImode);
	    emit_insn (gen_andv2si3 (t2, op0, mask));

	    /* XOR it back into the result of the subtraction.  This results
	       in the sign bit set iff we saw unsigned underflow.  */
	    x = gen_reg_rtx (V2SImode);
	    emit_insn (gen_xorv2si3 (x, t1, t2));

	    code = GT;
	    op0 = x;
	    op1 = CONST0_RTX (mode);
	  }
	  break;

	case V8QImode:
	case V4HImode:
	  /* Perform a parallel unsigned saturating subtraction.  */
	  x = gen_reg_rtx (mode);
	  emit_insn (gen_rtx_SET (VOIDmode, x,
				  gen_rtx_US_MINUS (mode, op0, op1)));

	  code = EQ;
	  op0 = x;
	  op1 = CONST0_RTX (mode);
	  negate = !negate;
	  break;

	default:
	  gcc_unreachable ();
	}
    }

  x = gen_rtx_fmt_ee (code, mode, op0, op1);
  emit_insn (gen_rtx_SET (VOIDmode, dest, x));

  return negate;
}

/* Emit an integral vector conditional move.  */

void
ia64_expand_vecint_cmov (rtx operands[])
{
  enum machine_mode mode = GET_MODE (operands[0]);
  enum rtx_code code = GET_CODE (operands[3]);
  bool negate;
  rtx cmp, x, ot, of;

  cmp = gen_reg_rtx (mode);
  negate = ia64_expand_vecint_compare (code, mode, cmp,
				       operands[4], operands[5]);

  ot = operands[1+negate];
  of = operands[2-negate];

  if (ot == CONST0_RTX (mode))
    {
      if (of == CONST0_RTX (mode))
	{
	  emit_move_insn (operands[0], ot);
	  return;
	}

      x = gen_rtx_NOT (mode, cmp);
      x = gen_rtx_AND (mode, x, of);
      emit_insn (gen_rtx_SET (VOIDmode, operands[0], x));
    }
  else if (of == CONST0_RTX (mode))
    {
      x = gen_rtx_AND (mode, cmp, ot);
      emit_insn (gen_rtx_SET (VOIDmode, operands[0], x));
    }
  else
    {
      rtx t, f;

      t = gen_reg_rtx (mode);
      x = gen_rtx_AND (mode, cmp, operands[1+negate]);
      emit_insn (gen_rtx_SET (VOIDmode, t, x));

      f = gen_reg_rtx (mode);
      x = gen_rtx_NOT (mode, cmp);
      x = gen_rtx_AND (mode, x, operands[2-negate]);
      emit_insn (gen_rtx_SET (VOIDmode, f, x));

      x = gen_rtx_IOR (mode, t, f);
      emit_insn (gen_rtx_SET (VOIDmode, operands[0], x));
    }
}

/* Emit an integral vector min or max operation.  Return true if all done.  */

bool
ia64_expand_vecint_minmax (enum rtx_code code, enum machine_mode mode,
			   rtx operands[])
{
  rtx xops[6];

  /* These four combinations are supported directly.  */
  if (mode == V8QImode && (code == UMIN || code == UMAX))
    return false;
  if (mode == V4HImode && (code == SMIN || code == SMAX))
    return false;

  /* This combination can be implemented with only saturating subtraction.  */
  if (mode == V4HImode && code == UMAX)
    {
      rtx x, tmp = gen_reg_rtx (mode);

      x = gen_rtx_US_MINUS (mode, operands[1], operands[2]);
      emit_insn (gen_rtx_SET (VOIDmode, tmp, x));

      emit_insn (gen_addv4hi3 (operands[0], tmp, operands[2]));
      return true;
    }

  /* Everything else implemented via vector comparisons.  */
  xops[0] = operands[0];
  xops[4] = xops[1] = operands[1];
  xops[5] = xops[2] = operands[2];

  switch (code)
    {
    case UMIN:
      code = LTU;
      break;
    case UMAX:
      code = GTU;
      break;
    case SMIN:
      code = LT;
      break;
    case SMAX:
      code = GT;
      break;
    default:
      gcc_unreachable ();
    }
  xops[3] = gen_rtx_fmt_ee (code, VOIDmode, operands[1], operands[2]);

  ia64_expand_vecint_cmov (xops);
  return true;
}

/* Emit an integral vector widening sum operations.  */

void
ia64_expand_widen_sum (rtx operands[3], bool unsignedp)
{
  rtx l, h, x, s;
  enum machine_mode wmode, mode;
  rtx (*unpack_l) (rtx, rtx, rtx);
  rtx (*unpack_h) (rtx, rtx, rtx);
  rtx (*plus) (rtx, rtx, rtx);

  wmode = GET_MODE (operands[0]);
  mode = GET_MODE (operands[1]);

  switch (mode)
    {
    case V8QImode:
      unpack_l = gen_unpack1_l;
      unpack_h = gen_unpack1_h;
      plus = gen_addv4hi3;
      break;
    case V4HImode:
      unpack_l = gen_unpack2_l;
      unpack_h = gen_unpack2_h;
      plus = gen_addv2si3;
      break;
    default:
      gcc_unreachable ();
    }

  /* Fill in x with the sign extension of each element in op1.  */
  if (unsignedp)
    x = CONST0_RTX (mode);
  else
    {
      bool neg;

      x = gen_reg_rtx (mode);

      neg = ia64_expand_vecint_compare (LT, mode, x, operands[1],
					CONST0_RTX (mode));
      gcc_assert (!neg);
    }

  l = gen_reg_rtx (wmode);
  h = gen_reg_rtx (wmode);
  s = gen_reg_rtx (wmode);

  emit_insn (unpack_l (gen_lowpart (mode, l), operands[1], x));
  emit_insn (unpack_h (gen_lowpart (mode, h), operands[1], x));
  emit_insn (plus (s, l, operands[2]));
  emit_insn (plus (operands[0], h, s));
}

/* Emit a signed or unsigned V8QI dot product operation.  */

void
ia64_expand_dot_prod_v8qi (rtx operands[4], bool unsignedp)
{
  rtx l1, l2, h1, h2, x1, x2, p1, p2, p3, p4, s1, s2, s3;

  /* Fill in x1 and x2 with the sign extension of each element.  */
  if (unsignedp)
    x1 = x2 = CONST0_RTX (V8QImode);
  else
    {
      bool neg;

      x1 = gen_reg_rtx (V8QImode);
      x2 = gen_reg_rtx (V8QImode);

      neg = ia64_expand_vecint_compare (LT, V8QImode, x1, operands[1],
					CONST0_RTX (V8QImode));
      gcc_assert (!neg);
      neg = ia64_expand_vecint_compare (LT, V8QImode, x2, operands[2],
					CONST0_RTX (V8QImode));
      gcc_assert (!neg);
    }

  l1 = gen_reg_rtx (V4HImode);
  l2 = gen_reg_rtx (V4HImode);
  h1 = gen_reg_rtx (V4HImode);
  h2 = gen_reg_rtx (V4HImode);

  emit_insn (gen_unpack1_l (gen_lowpart (V8QImode, l1), operands[1], x1));
  emit_insn (gen_unpack1_l (gen_lowpart (V8QImode, l2), operands[2], x2));
  emit_insn (gen_unpack1_h (gen_lowpart (V8QImode, h1), operands[1], x1));
  emit_insn (gen_unpack1_h (gen_lowpart (V8QImode, h2), operands[2], x2));

  p1 = gen_reg_rtx (V2SImode);
  p2 = gen_reg_rtx (V2SImode);
  p3 = gen_reg_rtx (V2SImode);
  p4 = gen_reg_rtx (V2SImode);
  emit_insn (gen_pmpy2_r (p1, l1, l2));
  emit_insn (gen_pmpy2_l (p2, l1, l2));
  emit_insn (gen_pmpy2_r (p3, h1, h2));
  emit_insn (gen_pmpy2_l (p4, h1, h2));

  s1 = gen_reg_rtx (V2SImode);
  s2 = gen_reg_rtx (V2SImode);
  s3 = gen_reg_rtx (V2SImode);
  emit_insn (gen_addv2si3 (s1, p1, p2));
  emit_insn (gen_addv2si3 (s2, p3, p4));
  emit_insn (gen_addv2si3 (s3, s1, operands[3]));
  emit_insn (gen_addv2si3 (operands[0], s2, s3));
}

/* Emit the appropriate sequence for a call.  */

void
ia64_expand_call (rtx retval, rtx addr, rtx nextarg ATTRIBUTE_UNUSED,
		  int sibcall_p)
{
  rtx insn, b0;

  addr = XEXP (addr, 0);
  addr = convert_memory_address (DImode, addr);
  b0 = gen_rtx_REG (DImode, R_BR (0));

  /* ??? Should do this for functions known to bind local too.  */
  if (TARGET_NO_PIC || TARGET_AUTO_PIC)
    {
      if (sibcall_p)
	insn = gen_sibcall_nogp (addr);
      else if (! retval)
	insn = gen_call_nogp (addr, b0);
      else
	insn = gen_call_value_nogp (retval, addr, b0);
      insn = emit_call_insn (insn);
    }
  else
    {
      if (sibcall_p)
	insn = gen_sibcall_gp (addr);
      else if (! retval)
	insn = gen_call_gp (addr, b0);
      else
	insn = gen_call_value_gp (retval, addr, b0);
      insn = emit_call_insn (insn);

      use_reg (&CALL_INSN_FUNCTION_USAGE (insn), pic_offset_table_rtx);
    }

  if (sibcall_p)
    use_reg (&CALL_INSN_FUNCTION_USAGE (insn), b0);
}

void
ia64_reload_gp (void)
{
  rtx tmp;

  if (current_frame_info.reg_save_gp)
    tmp = gen_rtx_REG (DImode, current_frame_info.reg_save_gp);
  else
    {
      HOST_WIDE_INT offset;

      offset = (current_frame_info.spill_cfa_off
	        + current_frame_info.spill_size);
      if (frame_pointer_needed)
        {
          tmp = hard_frame_pointer_rtx;
          offset = -offset;
        }
      else
        {
          tmp = stack_pointer_rtx;
          offset = current_frame_info.total_size - offset;
        }

      if (CONST_OK_FOR_I (offset))
        emit_insn (gen_adddi3 (pic_offset_table_rtx,
			       tmp, GEN_INT (offset)));
      else
        {
          emit_move_insn (pic_offset_table_rtx, GEN_INT (offset));
          emit_insn (gen_adddi3 (pic_offset_table_rtx,
			         pic_offset_table_rtx, tmp));
        }

      tmp = gen_rtx_MEM (DImode, pic_offset_table_rtx);
    }

  emit_move_insn (pic_offset_table_rtx, tmp);
}

void
ia64_split_call (rtx retval, rtx addr, rtx retaddr, rtx scratch_r,
		 rtx scratch_b, int noreturn_p, int sibcall_p)
{
  rtx insn;
  bool is_desc = false;

  /* If we find we're calling through a register, then we're actually
     calling through a descriptor, so load up the values.  */
  if (REG_P (addr) && GR_REGNO_P (REGNO (addr)))
    {
      rtx tmp;
      bool addr_dead_p;

      /* ??? We are currently constrained to *not* use peep2, because
	 we can legitimately change the global lifetime of the GP
	 (in the form of killing where previously live).  This is
	 because a call through a descriptor doesn't use the previous
	 value of the GP, while a direct call does, and we do not
	 commit to either form until the split here.

	 That said, this means that we lack precise life info for
	 whether ADDR is dead after this call.  This is not terribly
	 important, since we can fix things up essentially for free
	 with the POST_DEC below, but it's nice to not use it when we
	 can immediately tell it's not necessary.  */
      addr_dead_p = ((noreturn_p || sibcall_p
		      || TEST_HARD_REG_BIT (regs_invalidated_by_call,
					    REGNO (addr)))
		     && !FUNCTION_ARG_REGNO_P (REGNO (addr)));

      /* Load the code address into scratch_b.  */
      tmp = gen_rtx_POST_INC (Pmode, addr);
      tmp = gen_rtx_MEM (Pmode, tmp);
      emit_move_insn (scratch_r, tmp);
      emit_move_insn (scratch_b, scratch_r);

      /* Load the GP address.  If ADDR is not dead here, then we must
	 revert the change made above via the POST_INCREMENT.  */
      if (!addr_dead_p)
	tmp = gen_rtx_POST_DEC (Pmode, addr);
      else
	tmp = addr;
      tmp = gen_rtx_MEM (Pmode, tmp);
      emit_move_insn (pic_offset_table_rtx, tmp);

      is_desc = true;
      addr = scratch_b;
    }

  if (sibcall_p)
    insn = gen_sibcall_nogp (addr);
  else if (retval)
    insn = gen_call_value_nogp (retval, addr, retaddr);
  else
    insn = gen_call_nogp (addr, retaddr);
  emit_call_insn (insn);

  if ((!TARGET_CONST_GP || is_desc) && !noreturn_p && !sibcall_p)
    ia64_reload_gp ();
}

/* Expand an atomic operation.  We want to perform MEM <CODE>= VAL atomically.

   This differs from the generic code in that we know about the zero-extending
   properties of cmpxchg, and the zero-extending requirements of ar.ccv.  We
   also know that ld.acq+cmpxchg.rel equals a full barrier.

   The loop we want to generate looks like

	cmp_reg = mem;
      label:
        old_reg = cmp_reg;
	new_reg = cmp_reg op val;
	cmp_reg = compare-and-swap(mem, old_reg, new_reg)
	if (cmp_reg != old_reg)
	  goto label;

   Note that we only do the plain load from memory once.  Subsequent
   iterations use the value loaded by the compare-and-swap pattern.  */

void
ia64_expand_atomic_op (enum rtx_code code, rtx mem, rtx val,
		       rtx old_dst, rtx new_dst)
{
  enum machine_mode mode = GET_MODE (mem);
  rtx old_reg, new_reg, cmp_reg, ar_ccv, label;
  enum insn_code icode;

  /* Special case for using fetchadd.  */
  if ((mode == SImode || mode == DImode)
      && (code == PLUS || code == MINUS)
      && fetchadd_operand (val, mode))
    {
      if (code == MINUS)
	val = GEN_INT (-INTVAL (val));

      if (!old_dst)
        old_dst = gen_reg_rtx (mode);

      emit_insn (gen_memory_barrier ());

      if (mode == SImode)
	icode = CODE_FOR_fetchadd_acq_si;
      else
	icode = CODE_FOR_fetchadd_acq_di;
      emit_insn (GEN_FCN (icode) (old_dst, mem, val));

      if (new_dst)
	{
	  new_reg = expand_simple_binop (mode, PLUS, old_dst, val, new_dst,
					 true, OPTAB_WIDEN);
	  if (new_reg != new_dst)
	    emit_move_insn (new_dst, new_reg);
	}
      return;
    }

  /* Because of the volatile mem read, we get an ld.acq, which is the
     front half of the full barrier.  The end half is the cmpxchg.rel.  */
  gcc_assert (MEM_VOLATILE_P (mem));

  old_reg = gen_reg_rtx (DImode);
  cmp_reg = gen_reg_rtx (DImode);
  label = gen_label_rtx ();

  if (mode != DImode)
    {
      val = simplify_gen_subreg (DImode, val, mode, 0);
      emit_insn (gen_extend_insn (cmp_reg, mem, DImode, mode, 1));
    }
  else
    emit_move_insn (cmp_reg, mem);

  emit_label (label);

  ar_ccv = gen_rtx_REG (DImode, AR_CCV_REGNUM);
  emit_move_insn (old_reg, cmp_reg);
  emit_move_insn (ar_ccv, cmp_reg);

  if (old_dst)
    emit_move_insn (old_dst, gen_lowpart (mode, cmp_reg));

  new_reg = cmp_reg;
  if (code == NOT)
    {
      new_reg = expand_simple_unop (DImode, NOT, new_reg, NULL_RTX, true);
      code = AND;
    }
  new_reg = expand_simple_binop (DImode, code, new_reg, val, NULL_RTX,
				 true, OPTAB_DIRECT);

  if (mode != DImode)
    new_reg = gen_lowpart (mode, new_reg);
  if (new_dst)
    emit_move_insn (new_dst, new_reg);

  switch (mode)
    {
    case QImode:  icode = CODE_FOR_cmpxchg_rel_qi;  break;
    case HImode:  icode = CODE_FOR_cmpxchg_rel_hi;  break;
    case SImode:  icode = CODE_FOR_cmpxchg_rel_si;  break;
    case DImode:  icode = CODE_FOR_cmpxchg_rel_di;  break;
    default:
      gcc_unreachable ();
    }

  emit_insn (GEN_FCN (icode) (cmp_reg, mem, ar_ccv, new_reg));

  emit_cmp_and_jump_insns (cmp_reg, old_reg, NE, NULL, DImode, true, label);
}

/* Begin the assembly file.  */

static void
ia64_file_start (void)
{
  /* Variable tracking should be run after all optimizations which change order
     of insns.  It also needs a valid CFG.  This can't be done in
     ia64_override_options, because flag_var_tracking is finalized after
     that.  */
  ia64_flag_var_tracking = flag_var_tracking;
  flag_var_tracking = 0;

  default_file_start ();
  emit_safe_across_calls ();
}

void
emit_safe_across_calls (void)
{
  unsigned int rs, re;
  int out_state;

  rs = 1;
  out_state = 0;
  while (1)
    {
      while (rs < 64 && call_used_regs[PR_REG (rs)])
	rs++;
      if (rs >= 64)
	break;
      for (re = rs + 1; re < 64 && ! call_used_regs[PR_REG (re)]; re++)
	continue;
      if (out_state == 0)
	{
	  fputs ("\t.pred.safe_across_calls ", asm_out_file);
	  out_state = 1;
	}
      else
	fputc (',', asm_out_file);
      if (re == rs + 1)
	fprintf (asm_out_file, "p%u", rs);
      else
	fprintf (asm_out_file, "p%u-p%u", rs, re - 1);
      rs = re + 1;
    }
  if (out_state)
    fputc ('\n', asm_out_file);
}

/* Helper function for ia64_compute_frame_size: find an appropriate general
   register to spill some special register to.  SPECIAL_SPILL_MASK contains
   bits in GR0 to GR31 that have already been allocated by this routine.
   TRY_LOCALS is true if we should attempt to locate a local regnum.  */

static int
find_gr_spill (int try_locals)
{
  int regno;

  /* If this is a leaf function, first try an otherwise unused
     call-clobbered register.  */
  if (current_function_is_leaf)
    {
      for (regno = GR_REG (1); regno <= GR_REG (31); regno++)
	if (! regs_ever_live[regno]
	    && call_used_regs[regno]
	    && ! fixed_regs[regno]
	    && ! global_regs[regno]
	    && ((current_frame_info.gr_used_mask >> regno) & 1) == 0)
	  {
	    current_frame_info.gr_used_mask |= 1 << regno;
	    return regno;
	  }
    }

  if (try_locals)
    {
      regno = current_frame_info.n_local_regs;
      /* If there is a frame pointer, then we can't use loc79, because
	 that is HARD_FRAME_POINTER_REGNUM.  In particular, see the
	 reg_name switching code in ia64_expand_prologue.  */
      if (regno < (80 - frame_pointer_needed))
	{
	  current_frame_info.n_local_regs = regno + 1;
	  return LOC_REG (0) + regno;
	}
    }

  /* Failed to find a general register to spill to.  Must use stack.  */
  return 0;
}

/* In order to make for nice schedules, we try to allocate every temporary
   to a different register.  We must of course stay away from call-saved,
   fixed, and global registers.  We must also stay away from registers
   allocated in current_frame_info.gr_used_mask, since those include regs
   used all through the prologue.

   Any register allocated here must be used immediately.  The idea is to
   aid scheduling, not to solve data flow problems.  */

static int last_scratch_gr_reg;

static int
next_scratch_gr_reg (void)
{
  int i, regno;

  for (i = 0; i < 32; ++i)
    {
      regno = (last_scratch_gr_reg + i + 1) & 31;
      if (call_used_regs[regno]
	  && ! fixed_regs[regno]
	  && ! global_regs[regno]
	  && ((current_frame_info.gr_used_mask >> regno) & 1) == 0)
	{
	  last_scratch_gr_reg = regno;
	  return regno;
	}
    }

  /* There must be _something_ available.  */
  gcc_unreachable ();
}

/* Helper function for ia64_compute_frame_size, called through
   diddle_return_value.  Mark REG in current_frame_info.gr_used_mask.  */

static void
mark_reg_gr_used_mask (rtx reg, void *data ATTRIBUTE_UNUSED)
{
  unsigned int regno = REGNO (reg);
  if (regno < 32)
    {
      unsigned int i, n = hard_regno_nregs[regno][GET_MODE (reg)];
      for (i = 0; i < n; ++i)
	current_frame_info.gr_used_mask |= 1 << (regno + i);
    }
}

/* Returns the number of bytes offset between the frame pointer and the stack
   pointer for the current function.  SIZE is the number of bytes of space
   needed for local variables.  */

static void
ia64_compute_frame_size (HOST_WIDE_INT size)
{
  HOST_WIDE_INT total_size;
  HOST_WIDE_INT spill_size = 0;
  HOST_WIDE_INT extra_spill_size = 0;
  HOST_WIDE_INT pretend_args_size;
  HARD_REG_SET mask;
  int n_spilled = 0;
  int spilled_gr_p = 0;
  int spilled_fr_p = 0;
  unsigned int regno;
  int i;

  if (current_frame_info.initialized)
    return;

  memset (&current_frame_info, 0, sizeof current_frame_info);
  CLEAR_HARD_REG_SET (mask);

  /* Don't allocate scratches to the return register.  */
  diddle_return_value (mark_reg_gr_used_mask, NULL);

  /* Don't allocate scratches to the EH scratch registers.  */
  if (cfun->machine->ia64_eh_epilogue_sp)
    mark_reg_gr_used_mask (cfun->machine->ia64_eh_epilogue_sp, NULL);
  if (cfun->machine->ia64_eh_epilogue_bsp)
    mark_reg_gr_used_mask (cfun->machine->ia64_eh_epilogue_bsp, NULL);

  /* Find the size of the register stack frame.  We have only 80 local
     registers, because we reserve 8 for the inputs and 8 for the
     outputs.  */

  /* Skip HARD_FRAME_POINTER_REGNUM (loc79) when frame_pointer_needed,
     since we'll be adjusting that down later.  */
  regno = LOC_REG (78) + ! frame_pointer_needed;
  for (; regno >= LOC_REG (0); regno--)
    if (regs_ever_live[regno])
      break;
  current_frame_info.n_local_regs = regno - LOC_REG (0) + 1;

  /* For functions marked with the syscall_linkage attribute, we must mark
     all eight input registers as in use, so that locals aren't visible to
     the caller.  */

  if (cfun->machine->n_varargs > 0
      || lookup_attribute ("syscall_linkage",
			   TYPE_ATTRIBUTES (TREE_TYPE (current_function_decl))))
    current_frame_info.n_input_regs = 8;
  else
    {
      for (regno = IN_REG (7); regno >= IN_REG (0); regno--)
	if (regs_ever_live[regno])
	  break;
      current_frame_info.n_input_regs = regno - IN_REG (0) + 1;
    }

  for (regno = OUT_REG (7); regno >= OUT_REG (0); regno--)
    if (regs_ever_live[regno])
      break;
  i = regno - OUT_REG (0) + 1;

#ifndef PROFILE_HOOK
  /* When -p profiling, we need one output register for the mcount argument.
     Likewise for -a profiling for the bb_init_func argument.  For -ax
     profiling, we need two output registers for the two bb_init_trace_func
     arguments.  */
  if (current_function_profile)
    i = MAX (i, 1);
#endif
  current_frame_info.n_output_regs = i;

  /* ??? No rotating register support yet.  */
  current_frame_info.n_rotate_regs = 0;

  /* Discover which registers need spilling, and how much room that
     will take.  Begin with floating point and general registers,
     which will always wind up on the stack.  */

  for (regno = FR_REG (2); regno <= FR_REG (127); regno++)
    if (regs_ever_live[regno] && ! call_used_regs[regno])
      {
	SET_HARD_REG_BIT (mask, regno);
	spill_size += 16;
	n_spilled += 1;
	spilled_fr_p = 1;
      }

  for (regno = GR_REG (1); regno <= GR_REG (31); regno++)
    if (regs_ever_live[regno] && ! call_used_regs[regno])
      {
	SET_HARD_REG_BIT (mask, regno);
	spill_size += 8;
	n_spilled += 1;
	spilled_gr_p = 1;
      }

  for (regno = BR_REG (1); regno <= BR_REG (7); regno++)
    if (regs_ever_live[regno] && ! call_used_regs[regno])
      {
	SET_HARD_REG_BIT (mask, regno);
	spill_size += 8;
	n_spilled += 1;
      }

  /* Now come all special registers that might get saved in other
     general registers.  */

  if (frame_pointer_needed)
    {
      current_frame_info.reg_fp = find_gr_spill (1);
      /* If we did not get a register, then we take LOC79.  This is guaranteed
	 to be free, even if regs_ever_live is already set, because this is
	 HARD_FRAME_POINTER_REGNUM.  This requires incrementing n_local_regs,
	 as we don't count loc79 above.  */
      if (current_frame_info.reg_fp == 0)
	{
	  current_frame_info.reg_fp = LOC_REG (79);
	  current_frame_info.n_local_regs++;
	}
    }

  if (! current_function_is_leaf)
    {
      /* Emit a save of BR0 if we call other functions.  Do this even
	 if this function doesn't return, as EH depends on this to be
	 able to unwind the stack.  */
      SET_HARD_REG_BIT (mask, BR_REG (0));

      current_frame_info.reg_save_b0 = find_gr_spill (1);
      if (current_frame_info.reg_save_b0 == 0)
	{
	  extra_spill_size += 8;
	  n_spilled += 1;
	}

      /* Similarly for ar.pfs.  */
      SET_HARD_REG_BIT (mask, AR_PFS_REGNUM);
      current_frame_info.reg_save_ar_pfs = find_gr_spill (1);
      if (current_frame_info.reg_save_ar_pfs == 0)
	{
	  extra_spill_size += 8;
	  n_spilled += 1;
	}

      /* Similarly for gp.  Note that if we're calling setjmp, the stacked
	 registers are clobbered, so we fall back to the stack.  */
      current_frame_info.reg_save_gp
	= (current_function_calls_setjmp ? 0 : find_gr_spill (1));
      if (current_frame_info.reg_save_gp == 0)
	{
	  SET_HARD_REG_BIT (mask, GR_REG (1));
	  spill_size += 8;
	  n_spilled += 1;
	}
    }
  else
    {
      if (regs_ever_live[BR_REG (0)] && ! call_used_regs[BR_REG (0)])
	{
	  SET_HARD_REG_BIT (mask, BR_REG (0));
	  extra_spill_size += 8;
	  n_spilled += 1;
	}

      if (regs_ever_live[AR_PFS_REGNUM])
	{
	  SET_HARD_REG_BIT (mask, AR_PFS_REGNUM);
	  current_frame_info.reg_save_ar_pfs = find_gr_spill (1);
	  if (current_frame_info.reg_save_ar_pfs == 0)
	    {
	      extra_spill_size += 8;
	      n_spilled += 1;
	    }
	}
    }

  /* Unwind descriptor hackery: things are most efficient if we allocate
     consecutive GR save registers for RP, PFS, FP in that order. However,
     it is absolutely critical that FP get the only hard register that's
     guaranteed to be free, so we allocated it first.  If all three did
     happen to be allocated hard regs, and are consecutive, rearrange them
     into the preferred order now.  */
  if (current_frame_info.reg_fp != 0
      && current_frame_info.reg_save_b0 == current_frame_info.reg_fp + 1
      && current_frame_info.reg_save_ar_pfs == current_frame_info.reg_fp + 2)
    {
      current_frame_info.reg_save_b0 = current_frame_info.reg_fp;
      current_frame_info.reg_save_ar_pfs = current_frame_info.reg_fp + 1;
      current_frame_info.reg_fp = current_frame_info.reg_fp + 2;
    }

  /* See if we need to store the predicate register block.  */
  for (regno = PR_REG (0); regno <= PR_REG (63); regno++)
    if (regs_ever_live[regno] && ! call_used_regs[regno])
      break;
  if (regno <= PR_REG (63))
    {
      SET_HARD_REG_BIT (mask, PR_REG (0));
      current_frame_info.reg_save_pr = find_gr_spill (1);
      if (current_frame_info.reg_save_pr == 0)
	{
	  extra_spill_size += 8;
	  n_spilled += 1;
	}

      /* ??? Mark them all as used so that register renaming and such
	 are free to use them.  */
      for (regno = PR_REG (0); regno <= PR_REG (63); regno++)
	regs_ever_live[regno] = 1;
    }

  /* If we're forced to use st8.spill, we're forced to save and restore
     ar.unat as well.  The check for existing liveness allows inline asm
     to touch ar.unat.  */
  if (spilled_gr_p || cfun->machine->n_varargs
      || regs_ever_live[AR_UNAT_REGNUM])
    {
      regs_ever_live[AR_UNAT_REGNUM] = 1;
      SET_HARD_REG_BIT (mask, AR_UNAT_REGNUM);
      current_frame_info.reg_save_ar_unat = find_gr_spill (spill_size == 0);
      if (current_frame_info.reg_save_ar_unat == 0)
	{
	  extra_spill_size += 8;
	  n_spilled += 1;
	}
    }

  if (regs_ever_live[AR_LC_REGNUM])
    {
      SET_HARD_REG_BIT (mask, AR_LC_REGNUM);
      current_frame_info.reg_save_ar_lc = find_gr_spill (spill_size == 0);
      if (current_frame_info.reg_save_ar_lc == 0)
	{
	  extra_spill_size += 8;
	  n_spilled += 1;
	}
    }

  /* If we have an odd number of words of pretend arguments written to
     the stack, then the FR save area will be unaligned.  We round the
     size of this area up to keep things 16 byte aligned.  */
  if (spilled_fr_p)
    pretend_args_size = IA64_STACK_ALIGN (current_function_pretend_args_size);
  else
    pretend_args_size = current_function_pretend_args_size;

  total_size = (spill_size + extra_spill_size + size + pretend_args_size
		+ current_function_outgoing_args_size);
  total_size = IA64_STACK_ALIGN (total_size);

  /* We always use the 16-byte scratch area provided by the caller, but
     if we are a leaf function, there's no one to which we need to provide
     a scratch area.  */
  if (current_function_is_leaf)
    total_size = MAX (0, total_size - 16);

  current_frame_info.total_size = total_size;
  current_frame_info.spill_cfa_off = pretend_args_size - 16;
  current_frame_info.spill_size = spill_size;
  current_frame_info.extra_spill_size = extra_spill_size;
  COPY_HARD_REG_SET (current_frame_info.mask, mask);
  current_frame_info.n_spilled = n_spilled;
  current_frame_info.initialized = reload_completed;
}

/* Compute the initial difference between the specified pair of registers.  */

HOST_WIDE_INT
ia64_initial_elimination_offset (int from, int to)
{
  HOST_WIDE_INT offset;

  ia64_compute_frame_size (get_frame_size ());
  switch (from)
    {
    case FRAME_POINTER_REGNUM:
      switch (to)
	{
	case HARD_FRAME_POINTER_REGNUM:
	  if (current_function_is_leaf)
	    offset = -current_frame_info.total_size;
	  else
	    offset = -(current_frame_info.total_size
		       - current_function_outgoing_args_size - 16);
	  break;

	case STACK_POINTER_REGNUM:
	  if (current_function_is_leaf)
	    offset = 0;
	  else
	    offset = 16 + current_function_outgoing_args_size;
	  break;

	default:
	  gcc_unreachable ();
	}
      break;

    case ARG_POINTER_REGNUM:
      /* Arguments start above the 16 byte save area, unless stdarg
	 in which case we store through the 16 byte save area.  */
      switch (to)
	{
	case HARD_FRAME_POINTER_REGNUM:
	  offset = 16 - current_function_pretend_args_size;
	  break;

	case STACK_POINTER_REGNUM:
	  offset = (current_frame_info.total_size
		    + 16 - current_function_pretend_args_size);
	  break;

	default:
	  gcc_unreachable ();
	}
      break;

    default:
      gcc_unreachable ();
    }

  return offset;
}

/* If there are more than a trivial number of register spills, we use
   two interleaved iterators so that we can get two memory references
   per insn group.

   In order to simplify things in the prologue and epilogue expanders,
   we use helper functions to fix up the memory references after the
   fact with the appropriate offsets to a POST_MODIFY memory mode.
   The following data structure tracks the state of the two iterators
   while insns are being emitted.  */

struct spill_fill_data
{
  rtx init_after;		/* point at which to emit initializations */
  rtx init_reg[2];		/* initial base register */
  rtx iter_reg[2];		/* the iterator registers */
  rtx *prev_addr[2];		/* address of last memory use */
  rtx prev_insn[2];		/* the insn corresponding to prev_addr */
  HOST_WIDE_INT prev_off[2];	/* last offset */
  int n_iter;			/* number of iterators in use */
  int next_iter;		/* next iterator to use */
  unsigned int save_gr_used_mask;
};

static struct spill_fill_data spill_fill_data;

static void
setup_spill_pointers (int n_spills, rtx init_reg, HOST_WIDE_INT cfa_off)
{
  int i;

  spill_fill_data.init_after = get_last_insn ();
  spill_fill_data.init_reg[0] = init_reg;
  spill_fill_data.init_reg[1] = init_reg;
  spill_fill_data.prev_addr[0] = NULL;
  spill_fill_data.prev_addr[1] = NULL;
  spill_fill_data.prev_insn[0] = NULL;
  spill_fill_data.prev_insn[1] = NULL;
  spill_fill_data.prev_off[0] = cfa_off;
  spill_fill_data.prev_off[1] = cfa_off;
  spill_fill_data.next_iter = 0;
  spill_fill_data.save_gr_used_mask = current_frame_info.gr_used_mask;

  spill_fill_data.n_iter = 1 + (n_spills > 2);
  for (i = 0; i < spill_fill_data.n_iter; ++i)
    {
      int regno = next_scratch_gr_reg ();
      spill_fill_data.iter_reg[i] = gen_rtx_REG (DImode, regno);
      current_frame_info.gr_used_mask |= 1 << regno;
    }
}

static void
finish_spill_pointers (void)
{
  current_frame_info.gr_used_mask = spill_fill_data.save_gr_used_mask;
}

static rtx
spill_restore_mem (rtx reg, HOST_WIDE_INT cfa_off)
{
  int iter = spill_fill_data.next_iter;
  HOST_WIDE_INT disp = spill_fill_data.prev_off[iter] - cfa_off;
  rtx disp_rtx = GEN_INT (disp);
  rtx mem;

  if (spill_fill_data.prev_addr[iter])
    {
      if (CONST_OK_FOR_N (disp))
	{
	  *spill_fill_data.prev_addr[iter]
	    = gen_rtx_POST_MODIFY (DImode, spill_fill_data.iter_reg[iter],
				   gen_rtx_PLUS (DImode,
						 spill_fill_data.iter_reg[iter],
						 disp_rtx));
	  REG_NOTES (spill_fill_data.prev_insn[iter])
	    = gen_rtx_EXPR_LIST (REG_INC, spill_fill_data.iter_reg[iter],
				 REG_NOTES (spill_fill_data.prev_insn[iter]));
	}
      else
	{
	  /* ??? Could use register post_modify for loads.  */
	  if (! CONST_OK_FOR_I (disp))
	    {
	      rtx tmp = gen_rtx_REG (DImode, next_scratch_gr_reg ());
	      emit_move_insn (tmp, disp_rtx);
	      disp_rtx = tmp;
	    }
	  emit_insn (gen_adddi3 (spill_fill_data.iter_reg[iter],
				 spill_fill_data.iter_reg[iter], disp_rtx));
	}
    }
  /* Micro-optimization: if we've created a frame pointer, it's at
     CFA 0, which may allow the real iterator to be initialized lower,
     slightly increasing parallelism.  Also, if there are few saves
     it may eliminate the iterator entirely.  */
  else if (disp == 0
	   && spill_fill_data.init_reg[iter] == stack_pointer_rtx
	   && frame_pointer_needed)
    {
      mem = gen_rtx_MEM (GET_MODE (reg), hard_frame_pointer_rtx);
      set_mem_alias_set (mem, get_varargs_alias_set ());
      return mem;
    }
  else
    {
      rtx seq, insn;

      if (disp == 0)
	seq = gen_movdi (spill_fill_data.iter_reg[iter],
			 spill_fill_data.init_reg[iter]);
      else
	{
	  start_sequence ();

	  if (! CONST_OK_FOR_I (disp))
	    {
	      rtx tmp = gen_rtx_REG (DImode, next_scratch_gr_reg ());
	      emit_move_insn (tmp, disp_rtx);
	      disp_rtx = tmp;
	    }

	  emit_insn (gen_adddi3 (spill_fill_data.iter_reg[iter],
				 spill_fill_data.init_reg[iter],
				 disp_rtx));

	  seq = get_insns ();
	  end_sequence ();
	}

      /* Careful for being the first insn in a sequence.  */
      if (spill_fill_data.init_after)
	insn = emit_insn_after (seq, spill_fill_data.init_after);
      else
	{
	  rtx first = get_insns ();
	  if (first)
	    insn = emit_insn_before (seq, first);
	  else
	    insn = emit_insn (seq);
	}
      spill_fill_data.init_after = insn;

      /* If DISP is 0, we may or may not have a further adjustment
	 afterward.  If we do, then the load/store insn may be modified
	 to be a post-modify.  If we don't, then this copy may be
	 eliminated by copyprop_hardreg_forward, which makes this
	 insn garbage, which runs afoul of the sanity check in
	 propagate_one_insn.  So mark this insn as legal to delete.  */
      if (disp == 0)
	REG_NOTES(insn) = gen_rtx_EXPR_LIST (REG_MAYBE_DEAD, const0_rtx,
					     REG_NOTES (insn));
    }

  mem = gen_rtx_MEM (GET_MODE (reg), spill_fill_data.iter_reg[iter]);

  /* ??? Not all of the spills are for varargs, but some of them are.
     The rest of the spills belong in an alias set of their own.  But
     it doesn't actually hurt to include them here.  */
  set_mem_alias_set (mem, get_varargs_alias_set ());

  spill_fill_data.prev_addr[iter] = &XEXP (mem, 0);
  spill_fill_data.prev_off[iter] = cfa_off;

  if (++iter >= spill_fill_data.n_iter)
    iter = 0;
  spill_fill_data.next_iter = iter;

  return mem;
}

static void
do_spill (rtx (*move_fn) (rtx, rtx, rtx), rtx reg, HOST_WIDE_INT cfa_off,
	  rtx frame_reg)
{
  int iter = spill_fill_data.next_iter;
  rtx mem, insn;

  mem = spill_restore_mem (reg, cfa_off);
  insn = emit_insn ((*move_fn) (mem, reg, GEN_INT (cfa_off)));
  spill_fill_data.prev_insn[iter] = insn;

  if (frame_reg)
    {
      rtx base;
      HOST_WIDE_INT off;

      RTX_FRAME_RELATED_P (insn) = 1;

      /* Don't even pretend that the unwind code can intuit its way
	 through a pair of interleaved post_modify iterators.  Just
	 provide the correct answer.  */

      if (frame_pointer_needed)
	{
	  base = hard_frame_pointer_rtx;
	  off = - cfa_off;
	}
      else
	{
	  base = stack_pointer_rtx;
	  off = current_frame_info.total_size - cfa_off;
	}

      REG_NOTES (insn)
	= gen_rtx_EXPR_LIST (REG_FRAME_RELATED_EXPR,
		gen_rtx_SET (VOIDmode,
			     gen_rtx_MEM (GET_MODE (reg),
					  plus_constant (base, off)),
			     frame_reg),
		REG_NOTES (insn));
    }
}

static void
do_restore (rtx (*move_fn) (rtx, rtx, rtx), rtx reg, HOST_WIDE_INT cfa_off)
{
  int iter = spill_fill_data.next_iter;
  rtx insn;

  insn = emit_insn ((*move_fn) (reg, spill_restore_mem (reg, cfa_off),
				GEN_INT (cfa_off)));
  spill_fill_data.prev_insn[iter] = insn;
}

/* Wrapper functions that discards the CONST_INT spill offset.  These
   exist so that we can give gr_spill/gr_fill the offset they need and
   use a consistent function interface.  */

static rtx
gen_movdi_x (rtx dest, rtx src, rtx offset ATTRIBUTE_UNUSED)
{
  return gen_movdi (dest, src);
}

static rtx
gen_fr_spill_x (rtx dest, rtx src, rtx offset ATTRIBUTE_UNUSED)
{
  return gen_fr_spill (dest, src);
}

static rtx
gen_fr_restore_x (rtx dest, rtx src, rtx offset ATTRIBUTE_UNUSED)
{
  return gen_fr_restore (dest, src);
}

/* Called after register allocation to add any instructions needed for the
   prologue.  Using a prologue insn is favored compared to putting all of the
   instructions in output_function_prologue(), since it allows the scheduler
   to intermix instructions with the saves of the caller saved registers.  In
   some cases, it might be necessary to emit a barrier instruction as the last
   insn to prevent such scheduling.

   Also any insns generated here should have RTX_FRAME_RELATED_P(insn) = 1
   so that the debug info generation code can handle them properly.

   The register save area is layed out like so:
   cfa+16
	[ varargs spill area ]
	[ fr register spill area ]
	[ br register spill area ]
	[ ar register spill area ]
	[ pr register spill area ]
	[ gr register spill area ] */

/* ??? Get inefficient code when the frame size is larger than can fit in an
   adds instruction.  */

void
ia64_expand_prologue (void)
{
  rtx insn, ar_pfs_save_reg, ar_unat_save_reg;
  int i, epilogue_p, regno, alt_regno, cfa_off, n_varargs;
  rtx reg, alt_reg;

  ia64_compute_frame_size (get_frame_size ());
  last_scratch_gr_reg = 15;

  /* If there is no epilogue, then we don't need some prologue insns.
     We need to avoid emitting the dead prologue insns, because flow
     will complain about them.  */
  if (optimize)
    {
      edge e;
      edge_iterator ei;

      FOR_EACH_EDGE (e, ei, EXIT_BLOCK_PTR->preds)
	if ((e->flags & EDGE_FAKE) == 0
	    && (e->flags & EDGE_FALLTHRU) != 0)
	  break;
      epilogue_p = (e != NULL);
    }
  else
    epilogue_p = 1;

  /* Set the local, input, and output register names.  We need to do this
     for GNU libc, which creates crti.S/crtn.S by splitting initfini.c in
     half.  If we use in/loc/out register names, then we get assembler errors
     in crtn.S because there is no alloc insn or regstk directive in there.  */
  if (! TARGET_REG_NAMES)
    {
      int inputs = current_frame_info.n_input_regs;
      int locals = current_frame_info.n_local_regs;
      int outputs = current_frame_info.n_output_regs;

      for (i = 0; i < inputs; i++)
	reg_names[IN_REG (i)] = ia64_reg_numbers[i];
      for (i = 0; i < locals; i++)
	reg_names[LOC_REG (i)] = ia64_reg_numbers[inputs + i];
      for (i = 0; i < outputs; i++)
	reg_names[OUT_REG (i)] = ia64_reg_numbers[inputs + locals + i];
    }

  /* Set the frame pointer register name.  The regnum is logically loc79,
     but of course we'll not have allocated that many locals.  Rather than
     worrying about renumbering the existing rtxs, we adjust the name.  */
  /* ??? This code means that we can never use one local register when
     there is a frame pointer.  loc79 gets wasted in this case, as it is
     renamed to a register that will never be used.  See also the try_locals
     code in find_gr_spill.  */
  if (current_frame_info.reg_fp)
    {
      const char *tmp = reg_names[HARD_FRAME_POINTER_REGNUM];
      reg_names[HARD_FRAME_POINTER_REGNUM]
	= reg_names[current_frame_info.reg_fp];
      reg_names[current_frame_info.reg_fp] = tmp;
    }

  /* We don't need an alloc instruction if we've used no outputs or locals.  */
  if (current_frame_info.n_local_regs == 0
      && current_frame_info.n_output_regs == 0
      && current_frame_info.n_input_regs <= current_function_args_info.int_regs
      && !TEST_HARD_REG_BIT (current_frame_info.mask, AR_PFS_REGNUM))
    {
      /* If there is no alloc, but there are input registers used, then we
	 need a .regstk directive.  */
      current_frame_info.need_regstk = (TARGET_REG_NAMES != 0);
      ar_pfs_save_reg = NULL_RTX;
    }
  else
    {
      current_frame_info.need_regstk = 0;

      if (current_frame_info.reg_save_ar_pfs)
	regno = current_frame_info.reg_save_ar_pfs;
      else
	regno = next_scratch_gr_reg ();
      ar_pfs_save_reg = gen_rtx_REG (DImode, regno);

      insn = emit_insn (gen_alloc (ar_pfs_save_reg,
				   GEN_INT (current_frame_info.n_input_regs),
				   GEN_INT (current_frame_info.n_local_regs),
				   GEN_INT (current_frame_info.n_output_regs),
				   GEN_INT (current_frame_info.n_rotate_regs)));
      RTX_FRAME_RELATED_P (insn) = (current_frame_info.reg_save_ar_pfs != 0);
    }

  /* Set up frame pointer, stack pointer, and spill iterators.  */

  n_varargs = cfun->machine->n_varargs;
  setup_spill_pointers (current_frame_info.n_spilled + n_varargs,
			stack_pointer_rtx, 0);

  if (frame_pointer_needed)
    {
      insn = emit_move_insn (hard_frame_pointer_rtx, stack_pointer_rtx);
      RTX_FRAME_RELATED_P (insn) = 1;
    }

  if (current_frame_info.total_size != 0)
    {
      rtx frame_size_rtx = GEN_INT (- current_frame_info.total_size);
      rtx offset;

      if (CONST_OK_FOR_I (- current_frame_info.total_size))
	offset = frame_size_rtx;
      else
	{
	  regno = next_scratch_gr_reg ();
	  offset = gen_rtx_REG (DImode, regno);
	  emit_move_insn (offset, frame_size_rtx);
	}

      insn = emit_insn (gen_adddi3 (stack_pointer_rtx,
				    stack_pointer_rtx, offset));

      if (! frame_pointer_needed)
	{
	  RTX_FRAME_RELATED_P (insn) = 1;
	  if (GET_CODE (offset) != CONST_INT)
	    {
	      REG_NOTES (insn)
		= gen_rtx_EXPR_LIST (REG_FRAME_RELATED_EXPR,
			gen_rtx_SET (VOIDmode,
				     stack_pointer_rtx,
				     gen_rtx_PLUS (DImode,
						   stack_pointer_rtx,
						   frame_size_rtx)),
			REG_NOTES (insn));
	    }
	}

      /* ??? At this point we must generate a magic insn that appears to
	 modify the stack pointer, the frame pointer, and all spill
	 iterators.  This would allow the most scheduling freedom.  For
	 now, just hard stop.  */
      emit_insn (gen_blockage ());
    }

  /* Must copy out ar.unat before doing any integer spills.  */
  if (TEST_HARD_REG_BIT (current_frame_info.mask, AR_UNAT_REGNUM))
    {
      if (current_frame_info.reg_save_ar_unat)
	ar_unat_save_reg
	  = gen_rtx_REG (DImode, current_frame_info.reg_save_ar_unat);
      else
	{
	  alt_regno = next_scratch_gr_reg ();
	  ar_unat_save_reg = gen_rtx_REG (DImode, alt_regno);
	  current_frame_info.gr_used_mask |= 1 << alt_regno;
	}

      reg = gen_rtx_REG (DImode, AR_UNAT_REGNUM);
      insn = emit_move_insn (ar_unat_save_reg, reg);
      RTX_FRAME_RELATED_P (insn) = (current_frame_info.reg_save_ar_unat != 0);

      /* Even if we're not going to generate an epilogue, we still
	 need to save the register so that EH works.  */
      if (! epilogue_p && current_frame_info.reg_save_ar_unat)
	emit_insn (gen_prologue_use (ar_unat_save_reg));
    }
  else
    ar_unat_save_reg = NULL_RTX;

  /* Spill all varargs registers.  Do this before spilling any GR registers,
     since we want the UNAT bits for the GR registers to override the UNAT
     bits from varargs, which we don't care about.  */

  cfa_off = -16;
  for (regno = GR_ARG_FIRST + 7; n_varargs > 0; --n_varargs, --regno)
    {
      reg = gen_rtx_REG (DImode, regno);
      do_spill (gen_gr_spill, reg, cfa_off += 8, NULL_RTX);
    }

  /* Locate the bottom of the register save area.  */
  cfa_off = (current_frame_info.spill_cfa_off
	     + current_frame_info.spill_size
	     + current_frame_info.extra_spill_size);

  /* Save the predicate register block either in a register or in memory.  */
  if (TEST_HARD_REG_BIT (current_frame_info.mask, PR_REG (0)))
    {
      reg = gen_rtx_REG (DImode, PR_REG (0));
      if (current_frame_info.reg_save_pr != 0)
	{
	  alt_reg = gen_rtx_REG (DImode, current_frame_info.reg_save_pr);
	  insn = emit_move_insn (alt_reg, reg);

	  /* ??? Denote pr spill/fill by a DImode move that modifies all
	     64 hard registers.  */
	  RTX_FRAME_RELATED_P (insn) = 1;
	  REG_NOTES (insn)
	    = gen_rtx_EXPR_LIST (REG_FRAME_RELATED_EXPR,
			gen_rtx_SET (VOIDmode, alt_reg, reg),
			REG_NOTES (insn));

	  /* Even if we're not going to generate an epilogue, we still
	     need to save the register so that EH works.  */
	  if (! epilogue_p)
	    emit_insn (gen_prologue_use (alt_reg));
	}
      else
	{
	  alt_regno = next_scratch_gr_reg ();
	  alt_reg = gen_rtx_REG (DImode, alt_regno);
	  insn = emit_move_insn (alt_reg, reg);
	  do_spill (gen_movdi_x, alt_reg, cfa_off, reg);
	  cfa_off -= 8;
	}
    }

  /* Handle AR regs in numerical order.  All of them get special handling.  */
  if (TEST_HARD_REG_BIT (current_frame_info.mask, AR_UNAT_REGNUM)
      && current_frame_info.reg_save_ar_unat == 0)
    {
      reg = gen_rtx_REG (DImode, AR_UNAT_REGNUM);
      do_spill (gen_movdi_x, ar_unat_save_reg, cfa_off, reg);
      cfa_off -= 8;
    }

  /* The alloc insn already copied ar.pfs into a general register.  The
     only thing we have to do now is copy that register to a stack slot
     if we'd not allocated a local register for the job.  */
  if (TEST_HARD_REG_BIT (current_frame_info.mask, AR_PFS_REGNUM)
      && current_frame_info.reg_save_ar_pfs == 0)
    {
      reg = gen_rtx_REG (DImode, AR_PFS_REGNUM);
      do_spill (gen_movdi_x, ar_pfs_save_reg, cfa_off, reg);
      cfa_off -= 8;
    }

  if (TEST_HARD_REG_BIT (current_frame_info.mask, AR_LC_REGNUM))
    {
      reg = gen_rtx_REG (DImode, AR_LC_REGNUM);
      if (current_frame_info.reg_save_ar_lc != 0)
	{
	  alt_reg = gen_rtx_REG (DImode, current_frame_info.reg_save_ar_lc);
	  insn = emit_move_insn (alt_reg, reg);
	  RTX_FRAME_RELATED_P (insn) = 1;

	  /* Even if we're not going to generate an epilogue, we still
	     need to save the register so that EH works.  */
	  if (! epilogue_p)
	    emit_insn (gen_prologue_use (alt_reg));
	}
      else
	{
	  alt_regno = next_scratch_gr_reg ();
	  alt_reg = gen_rtx_REG (DImode, alt_regno);
	  emit_move_insn (alt_reg, reg);
	  do_spill (gen_movdi_x, alt_reg, cfa_off, reg);
	  cfa_off -= 8;
	}
    }

  /* Save the return pointer.  */
  if (TEST_HARD_REG_BIT (current_frame_info.mask, BR_REG (0)))
    {
      reg = gen_rtx_REG (DImode, BR_REG (0));
      if (current_frame_info.reg_save_b0 != 0)
	{
	  alt_reg = gen_rtx_REG (DImode, current_frame_info.reg_save_b0);
	  insn = emit_move_insn (alt_reg, reg);
	  RTX_FRAME_RELATED_P (insn) = 1;

	  /* Even if we're not going to generate an epilogue, we still
	     need to save the register so that EH works.  */
	  if (! epilogue_p)
	    emit_insn (gen_prologue_use (alt_reg));
	}
      else
	{
	  alt_regno = next_scratch_gr_reg ();
	  alt_reg = gen_rtx_REG (DImode, alt_regno);
	  emit_move_insn (alt_reg, reg);
	  do_spill (gen_movdi_x, alt_reg, cfa_off, reg);
	  cfa_off -= 8;
	}
    }

  if (current_frame_info.reg_save_gp)
    {
      insn = emit_move_insn (gen_rtx_REG (DImode,
					  current_frame_info.reg_save_gp),
			     pic_offset_table_rtx);
      /* We don't know for sure yet if this is actually needed, since
	 we've not split the PIC call patterns.  If all of the calls
	 are indirect, and not followed by any uses of the gp, then
	 this save is dead.  Allow it to go away.  */
      REG_NOTES (insn)
	= gen_rtx_EXPR_LIST (REG_MAYBE_DEAD, const0_rtx, REG_NOTES (insn));
    }

  /* We should now be at the base of the gr/br/fr spill area.  */
  gcc_assert (cfa_off == (current_frame_info.spill_cfa_off
			  + current_frame_info.spill_size));

  /* Spill all general registers.  */
  for (regno = GR_REG (1); regno <= GR_REG (31); ++regno)
    if (TEST_HARD_REG_BIT (current_frame_info.mask, regno))
      {
	reg = gen_rtx_REG (DImode, regno);
	do_spill (gen_gr_spill, reg, cfa_off, reg);
	cfa_off -= 8;
      }

  /* Spill the rest of the BR registers.  */
  for (regno = BR_REG (1); regno <= BR_REG (7); ++regno)
    if (TEST_HARD_REG_BIT (current_frame_info.mask, regno))
      {
	alt_regno = next_scratch_gr_reg ();
	alt_reg = gen_rtx_REG (DImode, alt_regno);
	reg = gen_rtx_REG (DImode, regno);
	emit_move_insn (alt_reg, reg);
	do_spill (gen_movdi_x, alt_reg, cfa_off, reg);
	cfa_off -= 8;
      }

  /* Align the frame and spill all FR registers.  */
  for (regno = FR_REG (2); regno <= FR_REG (127); ++regno)
    if (TEST_HARD_REG_BIT (current_frame_info.mask, regno))
      {
        gcc_assert (!(cfa_off & 15));
	reg = gen_rtx_REG (XFmode, regno);
	do_spill (gen_fr_spill_x, reg, cfa_off, reg);
	cfa_off -= 16;
      }

  gcc_assert (cfa_off == current_frame_info.spill_cfa_off);

  finish_spill_pointers ();
}

/* Called after register allocation to add any instructions needed for the
   epilogue.  Using an epilogue insn is favored compared to putting all of the
   instructions in output_function_prologue(), since it allows the scheduler
   to intermix instructions with the saves of the caller saved registers.  In
   some cases, it might be necessary to emit a barrier instruction as the last
   insn to prevent such scheduling.  */

void
ia64_expand_epilogue (int sibcall_p)
{
  rtx insn, reg, alt_reg, ar_unat_save_reg;
  int regno, alt_regno, cfa_off;

  ia64_compute_frame_size (get_frame_size ());

  /* If there is a frame pointer, then we use it instead of the stack
     pointer, so that the stack pointer does not need to be valid when
     the epilogue starts.  See EXIT_IGNORE_STACK.  */
  if (frame_pointer_needed)
    setup_spill_pointers (current_frame_info.n_spilled,
			  hard_frame_pointer_rtx, 0);
  else
    setup_spill_pointers (current_frame_info.n_spilled, stack_pointer_rtx,
			  current_frame_info.total_size);

  if (current_frame_info.total_size != 0)
    {
      /* ??? At this point we must generate a magic insn that appears to
         modify the spill iterators and the frame pointer.  This would
	 allow the most scheduling freedom.  For now, just hard stop.  */
      emit_insn (gen_blockage ());
    }

  /* Locate the bottom of the register save area.  */
  cfa_off = (current_frame_info.spill_cfa_off
	     + current_frame_info.spill_size
	     + current_frame_info.extra_spill_size);

  /* Restore the predicate registers.  */
  if (TEST_HARD_REG_BIT (current_frame_info.mask, PR_REG (0)))
    {
      if (current_frame_info.reg_save_pr != 0)
	alt_reg = gen_rtx_REG (DImode, current_frame_info.reg_save_pr);
      else
	{
	  alt_regno = next_scratch_gr_reg ();
	  alt_reg = gen_rtx_REG (DImode, alt_regno);
	  do_restore (gen_movdi_x, alt_reg, cfa_off);
	  cfa_off -= 8;
	}
      reg = gen_rtx_REG (DImode, PR_REG (0));
      emit_move_insn (reg, alt_reg);
    }

  /* Restore the application registers.  */

  /* Load the saved unat from the stack, but do not restore it until
     after the GRs have been restored.  */
  if (TEST_HARD_REG_BIT (current_frame_info.mask, AR_UNAT_REGNUM))
    {
      if (current_frame_info.reg_save_ar_unat != 0)
        ar_unat_save_reg
	  = gen_rtx_REG (DImode, current_frame_info.reg_save_ar_unat);
      else
	{
	  alt_regno = next_scratch_gr_reg ();
	  ar_unat_save_reg = gen_rtx_REG (DImode, alt_regno);
	  current_frame_info.gr_used_mask |= 1 << alt_regno;
	  do_restore (gen_movdi_x, ar_unat_save_reg, cfa_off);
	  cfa_off -= 8;
	}
    }
  else
    ar_unat_save_reg = NULL_RTX;

  if (current_frame_info.reg_save_ar_pfs != 0)
    {
      alt_reg = gen_rtx_REG (DImode, current_frame_info.reg_save_ar_pfs);
      reg = gen_rtx_REG (DImode, AR_PFS_REGNUM);
      emit_move_insn (reg, alt_reg);
    }
  else if (TEST_HARD_REG_BIT (current_frame_info.mask, AR_PFS_REGNUM))
    {
      alt_regno = next_scratch_gr_reg ();
      alt_reg = gen_rtx_REG (DImode, alt_regno);
      do_restore (gen_movdi_x, alt_reg, cfa_off);
      cfa_off -= 8;
      reg = gen_rtx_REG (DImode, AR_PFS_REGNUM);
      emit_move_insn (reg, alt_reg);
    }

  if (TEST_HARD_REG_BIT (current_frame_info.mask, AR_LC_REGNUM))
    {
      if (current_frame_info.reg_save_ar_lc != 0)
	alt_reg = gen_rtx_REG (DImode, current_frame_info.reg_save_ar_lc);
      else
	{
	  alt_regno = next_scratch_gr_reg ();
	  alt_reg = gen_rtx_REG (DImode, alt_regno);
	  do_restore (gen_movdi_x, alt_reg, cfa_off);
	  cfa_off -= 8;
	}
      reg = gen_rtx_REG (DImode, AR_LC_REGNUM);
      emit_move_insn (reg, alt_reg);
    }

  /* Restore the return pointer.  */
  if (TEST_HARD_REG_BIT (current_frame_info.mask, BR_REG (0)))
    {
      if (current_frame_info.reg_save_b0 != 0)
	alt_reg = gen_rtx_REG (DImode, current_frame_info.reg_save_b0);
      else
	{
	  alt_regno = next_scratch_gr_reg ();
	  alt_reg = gen_rtx_REG (DImode, alt_regno);
	  do_restore (gen_movdi_x, alt_reg, cfa_off);
	  cfa_off -= 8;
	}
      reg = gen_rtx_REG (DImode, BR_REG (0));
      emit_move_insn (reg, alt_reg);
    }

  /* We should now be at the base of the gr/br/fr spill area.  */
  gcc_assert (cfa_off == (current_frame_info.spill_cfa_off
			  + current_frame_info.spill_size));

  /* The GP may be stored on the stack in the prologue, but it's
     never restored in the epilogue.  Skip the stack slot.  */
  if (TEST_HARD_REG_BIT (current_frame_info.mask, GR_REG (1)))
    cfa_off -= 8;

  /* Restore all general registers.  */
  for (regno = GR_REG (2); regno <= GR_REG (31); ++regno)
    if (TEST_HARD_REG_BIT (current_frame_info.mask, regno))
      {
	reg = gen_rtx_REG (DImode, regno);
	do_restore (gen_gr_restore, reg, cfa_off);
	cfa_off -= 8;
      }

  /* Restore the branch registers.  */
  for (regno = BR_REG (1); regno <= BR_REG (7); ++regno)
    if (TEST_HARD_REG_BIT (current_frame_info.mask, regno))
      {
	alt_regno = next_scratch_gr_reg ();
	alt_reg = gen_rtx_REG (DImode, alt_regno);
	do_restore (gen_movdi_x, alt_reg, cfa_off);
	cfa_off -= 8;
	reg = gen_rtx_REG (DImode, regno);
	emit_move_insn (reg, alt_reg);
      }

  /* Restore floating point registers.  */
  for (regno = FR_REG (2); regno <= FR_REG (127); ++regno)
    if (TEST_HARD_REG_BIT (current_frame_info.mask, regno))
      {
        gcc_assert (!(cfa_off & 15));
	reg = gen_rtx_REG (XFmode, regno);
	do_restore (gen_fr_restore_x, reg, cfa_off);
	cfa_off -= 16;
      }

  /* Restore ar.unat for real.  */
  if (TEST_HARD_REG_BIT (current_frame_info.mask, AR_UNAT_REGNUM))
    {
      reg = gen_rtx_REG (DImode, AR_UNAT_REGNUM);
      emit_move_insn (reg, ar_unat_save_reg);
    }

  gcc_assert (cfa_off == current_frame_info.spill_cfa_off);

  finish_spill_pointers ();

  if (current_frame_info.total_size || cfun->machine->ia64_eh_epilogue_sp)
    {
      /* ??? At this point we must generate a magic insn that appears to
         modify the spill iterators, the stack pointer, and the frame
	 pointer.  This would allow the most scheduling freedom.  For now,
	 just hard stop.  */
      emit_insn (gen_blockage ());
    }

  if (cfun->machine->ia64_eh_epilogue_sp)
    emit_move_insn (stack_pointer_rtx, cfun->machine->ia64_eh_epilogue_sp);
  else if (frame_pointer_needed)
    {
      insn = emit_move_insn (stack_pointer_rtx, hard_frame_pointer_rtx);
      RTX_FRAME_RELATED_P (insn) = 1;
    }
  else if (current_frame_info.total_size)
    {
      rtx offset, frame_size_rtx;

      frame_size_rtx = GEN_INT (current_frame_info.total_size);
      if (CONST_OK_FOR_I (current_frame_info.total_size))
	offset = frame_size_rtx;
      else
	{
	  regno = next_scratch_gr_reg ();
	  offset = gen_rtx_REG (DImode, regno);
	  emit_move_insn (offset, frame_size_rtx);
	}

      insn = emit_insn (gen_adddi3 (stack_pointer_rtx, stack_pointer_rtx,
				    offset));

      RTX_FRAME_RELATED_P (insn) = 1;
      if (GET_CODE (offset) != CONST_INT)
	{
	  REG_NOTES (insn)
	    = gen_rtx_EXPR_LIST (REG_FRAME_RELATED_EXPR,
			gen_rtx_SET (VOIDmode,
				     stack_pointer_rtx,
				     gen_rtx_PLUS (DImode,
						   stack_pointer_rtx,
						   frame_size_rtx)),
			REG_NOTES (insn));
	}
    }

  if (cfun->machine->ia64_eh_epilogue_bsp)
    emit_insn (gen_set_bsp (cfun->machine->ia64_eh_epilogue_bsp));

  if (! sibcall_p)
    emit_jump_insn (gen_return_internal (gen_rtx_REG (DImode, BR_REG (0))));
  else
    {
      int fp = GR_REG (2);
      /* We need a throw away register here, r0 and r1 are reserved, so r2 is the
	 first available call clobbered register.  If there was a frame_pointer
	 register, we may have swapped the names of r2 and HARD_FRAME_POINTER_REGNUM,
	 so we have to make sure we're using the string "r2" when emitting
	 the register name for the assembler.  */
      if (current_frame_info.reg_fp && current_frame_info.reg_fp == GR_REG (2))
	fp = HARD_FRAME_POINTER_REGNUM;

      /* We must emit an alloc to force the input registers to become output
	 registers.  Otherwise, if the callee tries to pass its parameters
	 through to another call without an intervening alloc, then these
	 values get lost.  */
      /* ??? We don't need to preserve all input registers.  We only need to
	 preserve those input registers used as arguments to the sibling call.
	 It is unclear how to compute that number here.  */
      if (current_frame_info.n_input_regs != 0)
	{
	  rtx n_inputs = GEN_INT (current_frame_info.n_input_regs);
	  insn = emit_insn (gen_alloc (gen_rtx_REG (DImode, fp),
				const0_rtx, const0_rtx,
				n_inputs, const0_rtx));
	  RTX_FRAME_RELATED_P (insn) = 1;
	}
    }
}

/* Return 1 if br.ret can do all the work required to return from a
   function.  */

int
ia64_direct_return (void)
{
  if (reload_completed && ! frame_pointer_needed)
    {
      ia64_compute_frame_size (get_frame_size ());

      return (current_frame_info.total_size == 0
	      && current_frame_info.n_spilled == 0
	      && current_frame_info.reg_save_b0 == 0
	      && current_frame_info.reg_save_pr == 0
	      && current_frame_info.reg_save_ar_pfs == 0
	      && current_frame_info.reg_save_ar_unat == 0
	      && current_frame_info.reg_save_ar_lc == 0);
    }
  return 0;
}

/* Return the magic cookie that we use to hold the return address
   during early compilation.  */

rtx
ia64_return_addr_rtx (HOST_WIDE_INT count, rtx frame ATTRIBUTE_UNUSED)
{
  if (count != 0)
    return NULL;
  return gen_rtx_UNSPEC (Pmode, gen_rtvec (1, const0_rtx), UNSPEC_RET_ADDR);
}

/* Split this value after reload, now that we know where the return
   address is saved.  */

void
ia64_split_return_addr_rtx (rtx dest)
{
  rtx src;

  if (TEST_HARD_REG_BIT (current_frame_info.mask, BR_REG (0)))
    {
      if (current_frame_info.reg_save_b0 != 0)
	src = gen_rtx_REG (DImode, current_frame_info.reg_save_b0);
      else
	{
	  HOST_WIDE_INT off;
	  unsigned int regno;

	  /* Compute offset from CFA for BR0.  */
	  /* ??? Must be kept in sync with ia64_expand_prologue.  */
	  off = (current_frame_info.spill_cfa_off
		 + current_frame_info.spill_size);
	  for (regno = GR_REG (1); regno <= GR_REG (31); ++regno)
	    if (TEST_HARD_REG_BIT (current_frame_info.mask, regno))
	      off -= 8;

	  /* Convert CFA offset to a register based offset.  */
	  if (frame_pointer_needed)
	    src = hard_frame_pointer_rtx;
	  else
	    {
	      src = stack_pointer_rtx;
	      off += current_frame_info.total_size;
	    }

	  /* Load address into scratch register.  */
	  if (CONST_OK_FOR_I (off))
	    emit_insn (gen_adddi3 (dest, src, GEN_INT (off)));
	  else
	    {
	      emit_move_insn (dest, GEN_INT (off));
	      emit_insn (gen_adddi3 (dest, src, dest));
	    }

	  src = gen_rtx_MEM (Pmode, dest);
	}
    }
  else
    src = gen_rtx_REG (DImode, BR_REG (0));

  emit_move_insn (dest, src);
}

int
ia64_hard_regno_rename_ok (int from, int to)
{
  /* Don't clobber any of the registers we reserved for the prologue.  */
  if (to == current_frame_info.reg_fp
      || to == current_frame_info.reg_save_b0
      || to == current_frame_info.reg_save_pr
      || to == current_frame_info.reg_save_ar_pfs
      || to == current_frame_info.reg_save_ar_unat
      || to == current_frame_info.reg_save_ar_lc)
    return 0;

  if (from == current_frame_info.reg_fp
      || from == current_frame_info.reg_save_b0
      || from == current_frame_info.reg_save_pr
      || from == current_frame_info.reg_save_ar_pfs
      || from == current_frame_info.reg_save_ar_unat
      || from == current_frame_info.reg_save_ar_lc)
    return 0;

  /* Don't use output registers outside the register frame.  */
  if (OUT_REGNO_P (to) && to >= OUT_REG (current_frame_info.n_output_regs))
    return 0;

  /* Retain even/oddness on predicate register pairs.  */
  if (PR_REGNO_P (from) && PR_REGNO_P (to))
    return (from & 1) == (to & 1);

  return 1;
}

/* Target hook for assembling integer objects.  Handle word-sized
   aligned objects and detect the cases when @fptr is needed.  */

static bool
ia64_assemble_integer (rtx x, unsigned int size, int aligned_p)
{
  if (size == POINTER_SIZE / BITS_PER_UNIT
      && !(TARGET_NO_PIC || TARGET_AUTO_PIC)
      && GET_CODE (x) == SYMBOL_REF
      && SYMBOL_REF_FUNCTION_P (x))
    {
      static const char * const directive[2][2] = {
	  /* 64-bit pointer */  /* 32-bit pointer */
	{ "\tdata8.ua\t@fptr(", "\tdata4.ua\t@fptr("},	/* unaligned */
	{ "\tdata8\t@fptr(",    "\tdata4\t@fptr("}	/* aligned */
      };
      fputs (directive[(aligned_p != 0)][POINTER_SIZE == 32], asm_out_file);
      output_addr_const (asm_out_file, x);
      fputs (")\n", asm_out_file);
      return true;
    }
  return default_assemble_integer (x, size, aligned_p);
}

/* Emit the function prologue.  */

static void
ia64_output_function_prologue (FILE *file, HOST_WIDE_INT size ATTRIBUTE_UNUSED)
{
  int mask, grsave, grsave_prev;

  if (current_frame_info.need_regstk)
    fprintf (file, "\t.regstk %d, %d, %d, %d\n",
	     current_frame_info.n_input_regs,
	     current_frame_info.n_local_regs,
	     current_frame_info.n_output_regs,
	     current_frame_info.n_rotate_regs);

  if (!flag_unwind_tables && (!flag_exceptions || USING_SJLJ_EXCEPTIONS))
    return;

  /* Emit the .prologue directive.  */

  mask = 0;
  grsave = grsave_prev = 0;
  if (current_frame_info.reg_save_b0 != 0)
    {
      mask |= 8;
      grsave = grsave_prev = current_frame_info.reg_save_b0;
    }
  if (current_frame_info.reg_save_ar_pfs != 0
      && (grsave_prev == 0
	  || current_frame_info.reg_save_ar_pfs == grsave_prev + 1))
    {
      mask |= 4;
      if (grsave_prev == 0)
	grsave = current_frame_info.reg_save_ar_pfs;
      grsave_prev = current_frame_info.reg_save_ar_pfs;
    }
  if (current_frame_info.reg_fp != 0
      && (grsave_prev == 0
	  || current_frame_info.reg_fp == grsave_prev + 1))
    {
      mask |= 2;
      if (grsave_prev == 0)
	grsave = HARD_FRAME_POINTER_REGNUM;
      grsave_prev = current_frame_info.reg_fp;
    }
  if (current_frame_info.reg_save_pr != 0
      && (grsave_prev == 0
	  || current_frame_info.reg_save_pr == grsave_prev + 1))
    {
      mask |= 1;
      if (grsave_prev == 0)
	grsave = current_frame_info.reg_save_pr;
    }

  if (mask && TARGET_GNU_AS)
    fprintf (file, "\t.prologue %d, %d\n", mask,
	     ia64_dbx_register_number (grsave));
  else
    fputs ("\t.prologue\n", file);

  /* Emit a .spill directive, if necessary, to relocate the base of
     the register spill area.  */
  if (current_frame_info.spill_cfa_off != -16)
    fprintf (file, "\t.spill %ld\n",
	     (long) (current_frame_info.spill_cfa_off
		     + current_frame_info.spill_size));
}

/* Emit the .body directive at the scheduled end of the prologue.  */

static void
ia64_output_function_end_prologue (FILE *file)
{
  if (!flag_unwind_tables && (!flag_exceptions || USING_SJLJ_EXCEPTIONS))
    return;

  fputs ("\t.body\n", file);
}

/* Emit the function epilogue.  */

static void
ia64_output_function_epilogue (FILE *file ATTRIBUTE_UNUSED,
			       HOST_WIDE_INT size ATTRIBUTE_UNUSED)
{
  int i;

  if (current_frame_info.reg_fp)
    {
      const char *tmp = reg_names[HARD_FRAME_POINTER_REGNUM];
      reg_names[HARD_FRAME_POINTER_REGNUM]
	= reg_names[current_frame_info.reg_fp];
      reg_names[current_frame_info.reg_fp] = tmp;
    }
  if (! TARGET_REG_NAMES)
    {
      for (i = 0; i < current_frame_info.n_input_regs; i++)
	reg_names[IN_REG (i)] = ia64_input_reg_names[i];
      for (i = 0; i < current_frame_info.n_local_regs; i++)
	reg_names[LOC_REG (i)] = ia64_local_reg_names[i];
      for (i = 0; i < current_frame_info.n_output_regs; i++)
	reg_names[OUT_REG (i)] = ia64_output_reg_names[i];
    }

  current_frame_info.initialized = 0;
}

int
ia64_dbx_register_number (int regno)
{
  /* In ia64_expand_prologue we quite literally renamed the frame pointer
     from its home at loc79 to something inside the register frame.  We
     must perform the same renumbering here for the debug info.  */
  if (current_frame_info.reg_fp)
    {
      if (regno == HARD_FRAME_POINTER_REGNUM)
	regno = current_frame_info.reg_fp;
      else if (regno == current_frame_info.reg_fp)
	regno = HARD_FRAME_POINTER_REGNUM;
    }

  if (IN_REGNO_P (regno))
    return 32 + regno - IN_REG (0);
  else if (LOC_REGNO_P (regno))
    return 32 + current_frame_info.n_input_regs + regno - LOC_REG (0);
  else if (OUT_REGNO_P (regno))
    return (32 + current_frame_info.n_input_regs
	    + current_frame_info.n_local_regs + regno - OUT_REG (0));
  else
    return regno;
}

void
ia64_initialize_trampoline (rtx addr, rtx fnaddr, rtx static_chain)
{
  rtx addr_reg, eight = GEN_INT (8);

  /* The Intel assembler requires that the global __ia64_trampoline symbol
     be declared explicitly */
  if (!TARGET_GNU_AS)
    {
      static bool declared_ia64_trampoline = false;

      if (!declared_ia64_trampoline)
	{
	  declared_ia64_trampoline = true;
	  (*targetm.asm_out.globalize_label) (asm_out_file,
					      "__ia64_trampoline");
	}
    }

  /* Make sure addresses are Pmode even if we are in ILP32 mode. */
  addr = convert_memory_address (Pmode, addr);
  fnaddr = convert_memory_address (Pmode, fnaddr);
  static_chain = convert_memory_address (Pmode, static_chain);

  /* Load up our iterator.  */
  addr_reg = gen_reg_rtx (Pmode);
  emit_move_insn (addr_reg, addr);

  /* The first two words are the fake descriptor:
     __ia64_trampoline, ADDR+16.  */
  emit_move_insn (gen_rtx_MEM (Pmode, addr_reg),
		  gen_rtx_SYMBOL_REF (Pmode, "__ia64_trampoline"));
  emit_insn (gen_adddi3 (addr_reg, addr_reg, eight));

  emit_move_insn (gen_rtx_MEM (Pmode, addr_reg),
		  copy_to_reg (plus_constant (addr, 16)));
  emit_insn (gen_adddi3 (addr_reg, addr_reg, eight));

  /* The third word is the target descriptor.  */
  emit_move_insn (gen_rtx_MEM (Pmode, addr_reg), fnaddr);
  emit_insn (gen_adddi3 (addr_reg, addr_reg, eight));

  /* The fourth word is the static chain.  */
  emit_move_insn (gen_rtx_MEM (Pmode, addr_reg), static_chain);
}

/* Do any needed setup for a variadic function.  CUM has not been updated
   for the last named argument which has type TYPE and mode MODE.

   We generate the actual spill instructions during prologue generation.  */

static void
ia64_setup_incoming_varargs (CUMULATIVE_ARGS *cum, enum machine_mode mode,
			     tree type, int * pretend_size,
			     int second_time ATTRIBUTE_UNUSED)
{
  CUMULATIVE_ARGS next_cum = *cum;

  /* Skip the current argument.  */
  ia64_function_arg_advance (&next_cum, mode, type, 1);

  if (next_cum.words < MAX_ARGUMENT_SLOTS)
    {
      int n = MAX_ARGUMENT_SLOTS - next_cum.words;
      *pretend_size = n * UNITS_PER_WORD;
      cfun->machine->n_varargs = n;
    }
}

/* Check whether TYPE is a homogeneous floating point aggregate.  If
   it is, return the mode of the floating point type that appears
   in all leafs.  If it is not, return VOIDmode.

   An aggregate is a homogeneous floating point aggregate is if all
   fields/elements in it have the same floating point type (e.g,
   SFmode).  128-bit quad-precision floats are excluded.

   Variable sized aggregates should never arrive here, since we should
   have already decided to pass them by reference.  Top-level zero-sized
   aggregates are excluded because our parallels crash the middle-end.  */

static enum machine_mode
hfa_element_mode (tree type, bool nested)
{
  enum machine_mode element_mode = VOIDmode;
  enum machine_mode mode;
  enum tree_code code = TREE_CODE (type);
  int know_element_mode = 0;
  tree t;

  if (!nested && (!TYPE_SIZE (type) || integer_zerop (TYPE_SIZE (type))))
    return VOIDmode;

  switch (code)
    {
    case VOID_TYPE:	case INTEGER_TYPE:	case ENUMERAL_TYPE:
    case BOOLEAN_TYPE:	case POINTER_TYPE:
    case OFFSET_TYPE:	case REFERENCE_TYPE:	case METHOD_TYPE:
    case LANG_TYPE:		case FUNCTION_TYPE:
      return VOIDmode;

      /* Fortran complex types are supposed to be HFAs, so we need to handle
	 gcc's COMPLEX_TYPEs as HFAs.  We need to exclude the integral complex
	 types though.  */
    case COMPLEX_TYPE:
      if (GET_MODE_CLASS (TYPE_MODE (type)) == MODE_COMPLEX_FLOAT
	  && TYPE_MODE (type) != TCmode)
	return GET_MODE_INNER (TYPE_MODE (type));
      else
	return VOIDmode;

    case REAL_TYPE:
      /* We want to return VOIDmode for raw REAL_TYPEs, but the actual
	 mode if this is contained within an aggregate.  */
      if (nested && TYPE_MODE (type) != TFmode)
	return TYPE_MODE (type);
      else
	return VOIDmode;

    case ARRAY_TYPE:
      return hfa_element_mode (TREE_TYPE (type), 1);

    case RECORD_TYPE:
    case UNION_TYPE:
    case QUAL_UNION_TYPE:
      for (t = TYPE_FIELDS (type); t; t = TREE_CHAIN (t))
	{
	  if (TREE_CODE (t) != FIELD_DECL)
	    continue;

	  mode = hfa_element_mode (TREE_TYPE (t), 1);
	  if (know_element_mode)
	    {
	      if (mode != element_mode)
		return VOIDmode;
	    }
	  else if (GET_MODE_CLASS (mode) != MODE_FLOAT)
	    return VOIDmode;
	  else
	    {
	      know_element_mode = 1;
	      element_mode = mode;
	    }
	}
      return element_mode;

    default:
      /* If we reach here, we probably have some front-end specific type
	 that the backend doesn't know about.  This can happen via the
	 aggregate_value_p call in init_function_start.  All we can do is
	 ignore unknown tree types.  */
      return VOIDmode;
    }

  return VOIDmode;
}

/* Return the number of words required to hold a quantity of TYPE and MODE
   when passed as an argument.  */
static int
ia64_function_arg_words (tree type, enum machine_mode mode)
{
  int words;

  if (mode == BLKmode)
    words = int_size_in_bytes (type);
  else
    words = GET_MODE_SIZE (mode);

  return (words + UNITS_PER_WORD - 1) / UNITS_PER_WORD;  /* round up */
}

/* Return the number of registers that should be skipped so the current
   argument (described by TYPE and WORDS) will be properly aligned.

   Integer and float arguments larger than 8 bytes start at the next
   even boundary.  Aggregates larger than 8 bytes start at the next
   even boundary if the aggregate has 16 byte alignment.  Note that
   in the 32-bit ABI, TImode and TFmode have only 8-byte alignment
   but are still to be aligned in registers.

   ??? The ABI does not specify how to handle aggregates with
   alignment from 9 to 15 bytes, or greater than 16.  We handle them
   all as if they had 16 byte alignment.  Such aggregates can occur
   only if gcc extensions are used.  */
static int
ia64_function_arg_offset (CUMULATIVE_ARGS *cum, tree type, int words)
{
  if ((cum->words & 1) == 0)
    return 0;

  if (type
      && TREE_CODE (type) != INTEGER_TYPE
      && TREE_CODE (type) != REAL_TYPE)
    return TYPE_ALIGN (type) > 8 * BITS_PER_UNIT;
  else
    return words > 1;
}

/* Return rtx for register where argument is passed, or zero if it is passed
   on the stack.  */
/* ??? 128-bit quad-precision floats are always passed in general
   registers.  */

rtx
ia64_function_arg (CUMULATIVE_ARGS *cum, enum machine_mode mode, tree type,
		   int named, int incoming)
{
  int basereg = (incoming ? GR_ARG_FIRST : AR_ARG_FIRST);
  int words = ia64_function_arg_words (type, mode);
  int offset = ia64_function_arg_offset (cum, type, words);
  enum machine_mode hfa_mode = VOIDmode;

  /* If all argument slots are used, then it must go on the stack.  */
  if (cum->words + offset >= MAX_ARGUMENT_SLOTS)
    return 0;

  /* Check for and handle homogeneous FP aggregates.  */
  if (type)
    hfa_mode = hfa_element_mode (type, 0);

  /* Unnamed prototyped hfas are passed as usual.  Named prototyped hfas
     and unprototyped hfas are passed specially.  */
  if (hfa_mode != VOIDmode && (! cum->prototype || named))
    {
      rtx loc[16];
      int i = 0;
      int fp_regs = cum->fp_regs;
      int int_regs = cum->words + offset;
      int hfa_size = GET_MODE_SIZE (hfa_mode);
      int byte_size;
      int args_byte_size;

      /* If prototyped, pass it in FR regs then GR regs.
	 If not prototyped, pass it in both FR and GR regs.

	 If this is an SFmode aggregate, then it is possible to run out of
	 FR regs while GR regs are still left.  In that case, we pass the
	 remaining part in the GR regs.  */

      /* Fill the FP regs.  We do this always.  We stop if we reach the end
	 of the argument, the last FP register, or the last argument slot.  */

      byte_size = ((mode == BLKmode)
		   ? int_size_in_bytes (type) : GET_MODE_SIZE (mode));
      args_byte_size = int_regs * UNITS_PER_WORD;
      offset = 0;
      for (; (offset < byte_size && fp_regs < MAX_ARGUMENT_SLOTS
	      && args_byte_size < (MAX_ARGUMENT_SLOTS * UNITS_PER_WORD)); i++)
	{
	  loc[i] = gen_rtx_EXPR_LIST (VOIDmode,
				      gen_rtx_REG (hfa_mode, (FR_ARG_FIRST
							      + fp_regs)),
				      GEN_INT (offset));
	  offset += hfa_size;
	  args_byte_size += hfa_size;
	  fp_regs++;
	}

      /* If no prototype, then the whole thing must go in GR regs.  */
      if (! cum->prototype)
	offset = 0;
      /* If this is an SFmode aggregate, then we might have some left over
	 that needs to go in GR regs.  */
      else if (byte_size != offset)
	int_regs += offset / UNITS_PER_WORD;

      /* Fill in the GR regs.  We must use DImode here, not the hfa mode.  */

      for (; offset < byte_size && int_regs < MAX_ARGUMENT_SLOTS; i++)
	{
	  enum machine_mode gr_mode = DImode;
	  unsigned int gr_size;

	  /* If we have an odd 4 byte hunk because we ran out of FR regs,
	     then this goes in a GR reg left adjusted/little endian, right
	     adjusted/big endian.  */
	  /* ??? Currently this is handled wrong, because 4-byte hunks are
	     always right adjusted/little endian.  */
	  if (offset & 0x4)
	    gr_mode = SImode;
	  /* If we have an even 4 byte hunk because the aggregate is a
	     multiple of 4 bytes in size, then this goes in a GR reg right
	     adjusted/little endian.  */
	  else if (byte_size - offset == 4)
	    gr_mode = SImode;

	  loc[i] = gen_rtx_EXPR_LIST (VOIDmode,
				      gen_rtx_REG (gr_mode, (basereg
							     + int_regs)),
				      GEN_INT (offset));

	  gr_size = GET_MODE_SIZE (gr_mode);
	  offset += gr_size;
	  if (gr_size == UNITS_PER_WORD
	      || (gr_size < UNITS_PER_WORD && offset % UNITS_PER_WORD == 0))
	    int_regs++;
	  else if (gr_size > UNITS_PER_WORD)
	    int_regs += gr_size / UNITS_PER_WORD;
	}
      return gen_rtx_PARALLEL (mode, gen_rtvec_v (i, loc));
    }

  /* Integral and aggregates go in general registers.  If we have run out of
     FR registers, then FP values must also go in general registers.  This can
     happen when we have a SFmode HFA.  */
  else if (mode == TFmode || mode == TCmode
	   || (! FLOAT_MODE_P (mode) || cum->fp_regs == MAX_ARGUMENT_SLOTS))
    {
      int byte_size = ((mode == BLKmode)
                       ? int_size_in_bytes (type) : GET_MODE_SIZE (mode));
      if (BYTES_BIG_ENDIAN
	&& (mode == BLKmode || (type && AGGREGATE_TYPE_P (type)))
	&& byte_size < UNITS_PER_WORD
	&& byte_size > 0)
	{
	  rtx gr_reg = gen_rtx_EXPR_LIST (VOIDmode,
					  gen_rtx_REG (DImode,
						       (basereg + cum->words
							+ offset)),
					  const0_rtx);
	  return gen_rtx_PARALLEL (mode, gen_rtvec (1, gr_reg));
	}
      else
	return gen_rtx_REG (mode, basereg + cum->words + offset);

    }

  /* If there is a prototype, then FP values go in a FR register when
     named, and in a GR register when unnamed.  */
  else if (cum->prototype)
    {
      if (named)
	return gen_rtx_REG (mode, FR_ARG_FIRST + cum->fp_regs);
      /* In big-endian mode, an anonymous SFmode value must be represented
         as (parallel:SF [(expr_list (reg:DI n) (const_int 0))]) to force
	 the value into the high half of the general register.  */
      else if (BYTES_BIG_ENDIAN && mode == SFmode)
	return gen_rtx_PARALLEL (mode,
		 gen_rtvec (1,
                   gen_rtx_EXPR_LIST (VOIDmode,
		     gen_rtx_REG (DImode, basereg + cum->words + offset),
				      const0_rtx)));
      else
	return gen_rtx_REG (mode, basereg + cum->words + offset);
    }
  /* If there is no prototype, then FP values go in both FR and GR
     registers.  */
  else
    {
      /* See comment above.  */
      enum machine_mode inner_mode =
	(BYTES_BIG_ENDIAN && mode == SFmode) ? DImode : mode;

      rtx fp_reg = gen_rtx_EXPR_LIST (VOIDmode,
				      gen_rtx_REG (mode, (FR_ARG_FIRST
							  + cum->fp_regs)),
				      const0_rtx);
      rtx gr_reg = gen_rtx_EXPR_LIST (VOIDmode,
				      gen_rtx_REG (inner_mode,
						   (basereg + cum->words
						    + offset)),
				      const0_rtx);

      return gen_rtx_PARALLEL (mode, gen_rtvec (2, fp_reg, gr_reg));
    }
}

/* Return number of bytes, at the beginning of the argument, that must be
   put in registers.  0 is the argument is entirely in registers or entirely
   in memory.  */

static int
ia64_arg_partial_bytes (CUMULATIVE_ARGS *cum, enum machine_mode mode,
			tree type, bool named ATTRIBUTE_UNUSED)
{
  int words = ia64_function_arg_words (type, mode);
  int offset = ia64_function_arg_offset (cum, type, words);

  /* If all argument slots are used, then it must go on the stack.  */
  if (cum->words + offset >= MAX_ARGUMENT_SLOTS)
    return 0;

  /* It doesn't matter whether the argument goes in FR or GR regs.  If
     it fits within the 8 argument slots, then it goes entirely in
     registers.  If it extends past the last argument slot, then the rest
     goes on the stack.  */

  if (words + cum->words + offset <= MAX_ARGUMENT_SLOTS)
    return 0;

  return (MAX_ARGUMENT_SLOTS - cum->words - offset) * UNITS_PER_WORD;
}

/* Update CUM to point after this argument.  This is patterned after
   ia64_function_arg.  */

void
ia64_function_arg_advance (CUMULATIVE_ARGS *cum, enum machine_mode mode,
			   tree type, int named)
{
  int words = ia64_function_arg_words (type, mode);
  int offset = ia64_function_arg_offset (cum, type, words);
  enum machine_mode hfa_mode = VOIDmode;

  /* If all arg slots are already full, then there is nothing to do.  */
  if (cum->words >= MAX_ARGUMENT_SLOTS)
    return;

  cum->words += words + offset;

  /* Check for and handle homogeneous FP aggregates.  */
  if (type)
    hfa_mode = hfa_element_mode (type, 0);

  /* Unnamed prototyped hfas are passed as usual.  Named prototyped hfas
     and unprototyped hfas are passed specially.  */
  if (hfa_mode != VOIDmode && (! cum->prototype || named))
    {
      int fp_regs = cum->fp_regs;
      /* This is the original value of cum->words + offset.  */
      int int_regs = cum->words - words;
      int hfa_size = GET_MODE_SIZE (hfa_mode);
      int byte_size;
      int args_byte_size;

      /* If prototyped, pass it in FR regs then GR regs.
	 If not prototyped, pass it in both FR and GR regs.

	 If this is an SFmode aggregate, then it is possible to run out of
	 FR regs while GR regs are still left.  In that case, we pass the
	 remaining part in the GR regs.  */

      /* Fill the FP regs.  We do this always.  We stop if we reach the end
	 of the argument, the last FP register, or the last argument slot.  */

      byte_size = ((mode == BLKmode)
		   ? int_size_in_bytes (type) : GET_MODE_SIZE (mode));
      args_byte_size = int_regs * UNITS_PER_WORD;
      offset = 0;
      for (; (offset < byte_size && fp_regs < MAX_ARGUMENT_SLOTS
	      && args_byte_size < (MAX_ARGUMENT_SLOTS * UNITS_PER_WORD));)
	{
	  offset += hfa_size;
	  args_byte_size += hfa_size;
	  fp_regs++;
	}

      cum->fp_regs = fp_regs;
    }

  /* Integral and aggregates go in general registers.  So do TFmode FP values.
     If we have run out of FR registers, then other FP values must also go in
     general registers.  This can happen when we have a SFmode HFA.  */
  else if (mode == TFmode || mode == TCmode
           || (! FLOAT_MODE_P (mode) || cum->fp_regs == MAX_ARGUMENT_SLOTS))
    cum->int_regs = cum->words;

  /* If there is a prototype, then FP values go in a FR register when
     named, and in a GR register when unnamed.  */
  else if (cum->prototype)
    {
      if (! named)
	cum->int_regs = cum->words;
      else
	/* ??? Complex types should not reach here.  */
	cum->fp_regs += (GET_MODE_CLASS (mode) == MODE_COMPLEX_FLOAT ? 2 : 1);
    }
  /* If there is no prototype, then FP values go in both FR and GR
     registers.  */
  else
    {
      /* ??? Complex types should not reach here.  */
      cum->fp_regs += (GET_MODE_CLASS (mode) == MODE_COMPLEX_FLOAT ? 2 : 1);
      cum->int_regs = cum->words;
    }
}

/* Arguments with alignment larger than 8 bytes start at the next even
   boundary.  On ILP32 HPUX, TFmode arguments start on next even boundary
   even though their normal alignment is 8 bytes.  See ia64_function_arg.  */

int
ia64_function_arg_boundary (enum machine_mode mode, tree type)
{

  if (mode == TFmode && TARGET_HPUX && TARGET_ILP32)
    return PARM_BOUNDARY * 2;

  if (type)
    {
      if (TYPE_ALIGN (type) > PARM_BOUNDARY)
        return PARM_BOUNDARY * 2;
      else
        return PARM_BOUNDARY;
    }

  if (GET_MODE_BITSIZE (mode) > PARM_BOUNDARY)
    return PARM_BOUNDARY * 2;
  else
    return PARM_BOUNDARY;
}

/* True if it is OK to do sibling call optimization for the specified
   call expression EXP.  DECL will be the called function, or NULL if
   this is an indirect call.  */
static bool
ia64_function_ok_for_sibcall (tree decl, tree exp ATTRIBUTE_UNUSED)
{
  /* We can't perform a sibcall if the current function has the syscall_linkage
     attribute.  */
  if (lookup_attribute ("syscall_linkage",
			TYPE_ATTRIBUTES (TREE_TYPE (current_function_decl))))
    return false;

  /* We must always return with our current GP.  This means we can
     only sibcall to functions defined in the current module.  */
  return decl && (*targetm.binds_local_p) (decl);
}


/* Implement va_arg.  */

static tree
ia64_gimplify_va_arg (tree valist, tree type, tree *pre_p, tree *post_p)
{
  /* Variable sized types are passed by reference.  */
  if (pass_by_reference (NULL, TYPE_MODE (type), type, false))
    {
      tree ptrtype = build_pointer_type (type);
      tree addr = std_gimplify_va_arg_expr (valist, ptrtype, pre_p, post_p);
      return build_va_arg_indirect_ref (addr);
    }

  /* Aggregate arguments with alignment larger than 8 bytes start at
     the next even boundary.  Integer and floating point arguments
     do so if they are larger than 8 bytes, whether or not they are
     also aligned larger than 8 bytes.  */
  if ((TREE_CODE (type) == REAL_TYPE || TREE_CODE (type) == INTEGER_TYPE)
      ? int_size_in_bytes (type) > 8 : TYPE_ALIGN (type) > 8 * BITS_PER_UNIT)
    {
      tree t = build2 (PLUS_EXPR, TREE_TYPE (valist), valist,
		       build_int_cst (NULL_TREE, 2 * UNITS_PER_WORD - 1));
      t = build2 (BIT_AND_EXPR, TREE_TYPE (t), t,
		  build_int_cst (NULL_TREE, -2 * UNITS_PER_WORD));
      t = build2 (MODIFY_EXPR, TREE_TYPE (valist), valist, t);
      gimplify_and_add (t, pre_p);
    }

  return std_gimplify_va_arg_expr (valist, type, pre_p, post_p);
}

/* Return 1 if function return value returned in memory.  Return 0 if it is
   in a register.  */

static bool
ia64_return_in_memory (tree valtype, tree fntype ATTRIBUTE_UNUSED)
{
  enum machine_mode mode;
  enum machine_mode hfa_mode;
  HOST_WIDE_INT byte_size;

  mode = TYPE_MODE (valtype);
  byte_size = GET_MODE_SIZE (mode);
  if (mode == BLKmode)
    {
      byte_size = int_size_in_bytes (valtype);
      if (byte_size < 0)
	return true;
    }

  /* Hfa's with up to 8 elements are returned in the FP argument registers.  */

  hfa_mode = hfa_element_mode (valtype, 0);
  if (hfa_mode != VOIDmode)
    {
      int hfa_size = GET_MODE_SIZE (hfa_mode);

      if (byte_size / hfa_size > MAX_ARGUMENT_SLOTS)
	return true;
      else
	return false;
    }
  else if (byte_size > UNITS_PER_WORD * MAX_INT_RETURN_SLOTS)
    return true;
  else
    return false;
}

/* Return rtx for register that holds the function return value.  */

rtx
ia64_function_value (tree valtype, tree func ATTRIBUTE_UNUSED)
{
  enum machine_mode mode;
  enum machine_mode hfa_mode;

  mode = TYPE_MODE (valtype);
  hfa_mode = hfa_element_mode (valtype, 0);

  if (hfa_mode != VOIDmode)
    {
      rtx loc[8];
      int i;
      int hfa_size;
      int byte_size;
      int offset;

      hfa_size = GET_MODE_SIZE (hfa_mode);
      byte_size = ((mode == BLKmode)
		   ? int_size_in_bytes (valtype) : GET_MODE_SIZE (mode));
      offset = 0;
      for (i = 0; offset < byte_size; i++)
	{
	  loc[i] = gen_rtx_EXPR_LIST (VOIDmode,
				      gen_rtx_REG (hfa_mode, FR_ARG_FIRST + i),
				      GEN_INT (offset));
	  offset += hfa_size;
	}
      return gen_rtx_PARALLEL (mode, gen_rtvec_v (i, loc));
    }
  else if (FLOAT_TYPE_P (valtype) && mode != TFmode && mode != TCmode)
    return gen_rtx_REG (mode, FR_ARG_FIRST);
  else
    {
      bool need_parallel = false;

      /* In big-endian mode, we need to manage the layout of aggregates
	 in the registers so that we get the bits properly aligned in
	 the highpart of the registers.  */
      if (BYTES_BIG_ENDIAN
	  && (mode == BLKmode || (valtype && AGGREGATE_TYPE_P (valtype))))
	need_parallel = true;

      /* Something like struct S { long double x; char a[0] } is not an
	 HFA structure, and therefore doesn't go in fp registers.  But
	 the middle-end will give it XFmode anyway, and XFmode values
	 don't normally fit in integer registers.  So we need to smuggle
	 the value inside a parallel.  */
      else if (mode == XFmode || mode == XCmode || mode == RFmode)
	need_parallel = true;

      if (need_parallel)
	{
	  rtx loc[8];
	  int offset;
	  int bytesize;
	  int i;

	  offset = 0;
	  bytesize = int_size_in_bytes (valtype);
	  /* An empty PARALLEL is invalid here, but the return value
	     doesn't matter for empty structs.  */
	  if (bytesize == 0)
	    return gen_rtx_REG (mode, GR_RET_FIRST);
	  for (i = 0; offset < bytesize; i++)
	    {
	      loc[i] = gen_rtx_EXPR_LIST (VOIDmode,
					  gen_rtx_REG (DImode,
						       GR_RET_FIRST + i),
					  GEN_INT (offset));
	      offset += UNITS_PER_WORD;
	    }
	  return gen_rtx_PARALLEL (mode, gen_rtvec_v (i, loc));
	}

      return gen_rtx_REG (mode, GR_RET_FIRST);
    }
}

/* This is called from dwarf2out.c via TARGET_ASM_OUTPUT_DWARF_DTPREL.
   We need to emit DTP-relative relocations.  */

static void
ia64_output_dwarf_dtprel (FILE *file, int size, rtx x)
{
  gcc_assert (size == 4 || size == 8);
  if (size == 4)
    fputs ("\tdata4.ua\t@dtprel(", file);
  else
    fputs ("\tdata8.ua\t@dtprel(", file);
  output_addr_const (file, x);
  fputs (")", file);
}

/* Print a memory address as an operand to reference that memory location.  */

/* ??? Do we need this?  It gets used only for 'a' operands.  We could perhaps
   also call this from ia64_print_operand for memory addresses.  */

void
ia64_print_operand_address (FILE * stream ATTRIBUTE_UNUSED,
			    rtx address ATTRIBUTE_UNUSED)
{
}

/* Print an operand to an assembler instruction.
   C	Swap and print a comparison operator.
   D	Print an FP comparison operator.
   E    Print 32 - constant, for SImode shifts as extract.
   e    Print 64 - constant, for DImode rotates.
   F	A floating point constant 0.0 emitted as f0, or 1.0 emitted as f1, or
        a floating point register emitted normally.
   I	Invert a predicate register by adding 1.
   J    Select the proper predicate register for a condition.
   j    Select the inverse predicate register for a condition.
   O	Append .acq for volatile load.
   P	Postincrement of a MEM.
   Q	Append .rel for volatile store.
   S	Shift amount for shladd instruction.
   T	Print an 8-bit sign extended number (K) as a 32-bit unsigned number
	for Intel assembler.
   U	Print an 8-bit sign extended number (K) as a 64-bit unsigned number
	for Intel assembler.
   X	A pair of floating point registers.
   r	Print register name, or constant 0 as r0.  HP compatibility for
	Linux kernel.
   v    Print vector constant value as an 8-byte integer value.  */

void
ia64_print_operand (FILE * file, rtx x, int code)
{
  const char *str;

  switch (code)
    {
    case 0:
      /* Handled below.  */
      break;

    case 'C':
      {
	enum rtx_code c = swap_condition (GET_CODE (x));
	fputs (GET_RTX_NAME (c), file);
	return;
      }

    case 'D':
      switch (GET_CODE (x))
	{
	case NE:
	  str = "neq";
	  break;
	case UNORDERED:
	  str = "unord";
	  break;
	case ORDERED:
	  str = "ord";
	  break;
	default:
	  str = GET_RTX_NAME (GET_CODE (x));
	  break;
	}
      fputs (str, file);
      return;

    case 'E':
      fprintf (file, HOST_WIDE_INT_PRINT_DEC, 32 - INTVAL (x));
      return;

    case 'e':
      fprintf (file, HOST_WIDE_INT_PRINT_DEC, 64 - INTVAL (x));
      return;

    case 'F':
      if (x == CONST0_RTX (GET_MODE (x)))
	str = reg_names [FR_REG (0)];
      else if (x == CONST1_RTX (GET_MODE (x)))
	str = reg_names [FR_REG (1)];
      else
	{
	  gcc_assert (GET_CODE (x) == REG);
	  str = reg_names [REGNO (x)];
	}
      fputs (str, file);
      return;

    case 'I':
      fputs (reg_names [REGNO (x) + 1], file);
      return;

    case 'J':
    case 'j':
      {
	unsigned int regno = REGNO (XEXP (x, 0));
	if (GET_CODE (x) == EQ)
	  regno += 1;
	if (code == 'j')
	  regno ^= 1;
        fputs (reg_names [regno], file);
      }
      return;

    case 'O':
      if (MEM_VOLATILE_P (x))
	fputs(".acq", file);
      return;

    case 'P':
      {
	HOST_WIDE_INT value;

	switch (GET_CODE (XEXP (x, 0)))
	  {
	  default:
	    return;

	  case POST_MODIFY:
	    x = XEXP (XEXP (XEXP (x, 0), 1), 1);
	    if (GET_CODE (x) == CONST_INT)
	      value = INTVAL (x);
	    else
	      {
		gcc_assert (GET_CODE (x) == REG);
		fprintf (file, ", %s", reg_names[REGNO (x)]);
		return;
	      }
	    break;

	  case POST_INC:
	    value = GET_MODE_SIZE (GET_MODE (x));
	    break;

	  case POST_DEC:
	    value = - (HOST_WIDE_INT) GET_MODE_SIZE (GET_MODE (x));
	    break;
	  }

	fprintf (file, ", " HOST_WIDE_INT_PRINT_DEC, value);
	return;
      }

    case 'Q':
      if (MEM_VOLATILE_P (x))
	fputs(".rel", file);
      return;

    case 'S':
      fprintf (file, "%d", exact_log2 (INTVAL (x)));
      return;

    case 'T':
      if (! TARGET_GNU_AS && GET_CODE (x) == CONST_INT)
	{
	  fprintf (file, "0x%x", (int) INTVAL (x) & 0xffffffff);
	  return;
	}
      break;

    case 'U':
      if (! TARGET_GNU_AS && GET_CODE (x) == CONST_INT)
	{
	  const char *prefix = "0x";
	  if (INTVAL (x) & 0x80000000)
	    {
	      fprintf (file, "0xffffffff");
	      prefix = "";
	    }
	  fprintf (file, "%s%x", prefix, (int) INTVAL (x) & 0xffffffff);
	  return;
	}
      break;

    case 'X':
      {
	unsigned int regno = REGNO (x);
	fprintf (file, "%s, %s", reg_names [regno], reg_names [regno + 1]);
      }
      return;

    case 'r':
      /* If this operand is the constant zero, write it as register zero.
	 Any register, zero, or CONST_INT value is OK here.  */
      if (GET_CODE (x) == REG)
	fputs (reg_names[REGNO (x)], file);
      else if (x == CONST0_RTX (GET_MODE (x)))
	fputs ("r0", file);
      else if (GET_CODE (x) == CONST_INT)
	output_addr_const (file, x);
      else
	output_operand_lossage ("invalid %%r value");
      return;

    case 'v':
      gcc_assert (GET_CODE (x) == CONST_VECTOR);
      x = simplify_subreg (DImode, x, GET_MODE (x), 0);
      break;

    case '+':
      {
	const char *which;

	/* For conditional branches, returns or calls, substitute
	   sptk, dptk, dpnt, or spnt for %s.  */
	x = find_reg_note (current_output_insn, REG_BR_PROB, 0);
	if (x)
	  {
	    int pred_val = INTVAL (XEXP (x, 0));

	    /* Guess top and bottom 10% statically predicted.  */
	    if (pred_val < REG_BR_PROB_BASE / 50
		&& br_prob_note_reliable_p (x))
	      which = ".spnt";
	    else if (pred_val < REG_BR_PROB_BASE / 2)
	      which = ".dpnt";
	    else if (pred_val < REG_BR_PROB_BASE / 100 * 98
		     || !br_prob_note_reliable_p (x))
	      which = ".dptk";
	    else
	      which = ".sptk";
	  }
	else if (GET_CODE (current_output_insn) == CALL_INSN)
	  which = ".sptk";
	else
	  which = ".dptk";

	fputs (which, file);
	return;
      }

    case ',':
      x = current_insn_predicate;
      if (x)
	{
	  unsigned int regno = REGNO (XEXP (x, 0));
	  if (GET_CODE (x) == EQ)
	    regno += 1;
          fprintf (file, "(%s) ", reg_names [regno]);
	}
      return;

    default:
      output_operand_lossage ("ia64_print_operand: unknown code");
      return;
    }

  switch (GET_CODE (x))
    {
      /* This happens for the spill/restore instructions.  */
    case POST_INC:
    case POST_DEC:
    case POST_MODIFY:
      x = XEXP (x, 0);
      /* ... fall through ...  */

    case REG:
      fputs (reg_names [REGNO (x)], file);
      break;

    case MEM:
      {
	rtx addr = XEXP (x, 0);
	if (GET_RTX_CLASS (GET_CODE (addr)) == RTX_AUTOINC)
	  addr = XEXP (addr, 0);
	fprintf (file, "[%s]", reg_names [REGNO (addr)]);
	break;
      }

    default:
      output_addr_const (file, x);
      break;
    }

  return;
}

/* Compute a (partial) cost for rtx X.  Return true if the complete
   cost has been computed, and false if subexpressions should be
   scanned.  In either case, *TOTAL contains the cost result.  */
/* ??? This is incomplete.  */

static bool
ia64_rtx_costs (rtx x, int code, int outer_code, int *total)
{
  switch (code)
    {
    case CONST_INT:
      switch (outer_code)
        {
        case SET:
	  *total = CONST_OK_FOR_J (INTVAL (x)) ? 0 : COSTS_N_INSNS (1);
	  return true;
        case PLUS:
	  if (CONST_OK_FOR_I (INTVAL (x)))
	    *total = 0;
	  else if (CONST_OK_FOR_J (INTVAL (x)))
	    *total = 1;
	  else
	    *total = COSTS_N_INSNS (1);
	  return true;
        default:
	  if (CONST_OK_FOR_K (INTVAL (x)) || CONST_OK_FOR_L (INTVAL (x)))
	    *total = 0;
	  else
	    *total = COSTS_N_INSNS (1);
	  return true;
	}

    case CONST_DOUBLE:
      *total = COSTS_N_INSNS (1);
      return true;

    case CONST:
    case SYMBOL_REF:
    case LABEL_REF:
      *total = COSTS_N_INSNS (3);
      return true;

    case MULT:
      /* For multiplies wider than HImode, we have to go to the FPU,
         which normally involves copies.  Plus there's the latency
         of the multiply itself, and the latency of the instructions to
         transfer integer regs to FP regs.  */
      /* ??? Check for FP mode.  */
      if (GET_MODE_SIZE (GET_MODE (x)) > 2)
        *total = COSTS_N_INSNS (10);
      else
	*total = COSTS_N_INSNS (2);
      return true;

    case PLUS:
    case MINUS:
    case ASHIFT:
    case ASHIFTRT:
    case LSHIFTRT:
      *total = COSTS_N_INSNS (1);
      return true;

    case DIV:
    case UDIV:
    case MOD:
    case UMOD:
      /* We make divide expensive, so that divide-by-constant will be
         optimized to a multiply.  */
      *total = COSTS_N_INSNS (60);
      return true;

    default:
      return false;
    }
}

/* Calculate the cost of moving data from a register in class FROM to
   one in class TO, using MODE.  */

int
ia64_register_move_cost (enum machine_mode mode, enum reg_class from,
			 enum reg_class to)
{
  /* ADDL_REGS is the same as GR_REGS for movement purposes.  */
  if (to == ADDL_REGS)
    to = GR_REGS;
  if (from == ADDL_REGS)
    from = GR_REGS;

  /* All costs are symmetric, so reduce cases by putting the
     lower number class as the destination.  */
  if (from < to)
    {
      enum reg_class tmp = to;
      to = from, from = tmp;
    }

  /* Moving from FR<->GR in XFmode must be more expensive than 2,
     so that we get secondary memory reloads.  Between FR_REGS,
     we have to make this at least as expensive as MEMORY_MOVE_COST
     to avoid spectacularly poor register class preferencing.  */
  if (mode == XFmode || mode == RFmode)
    {
      if (to != GR_REGS || from != GR_REGS)
        return MEMORY_MOVE_COST (mode, to, 0);
      else
	return 3;
    }

  switch (to)
    {
    case PR_REGS:
      /* Moving between PR registers takes two insns.  */
      if (from == PR_REGS)
	return 3;
      /* Moving between PR and anything but GR is impossible.  */
      if (from != GR_REGS)
	return MEMORY_MOVE_COST (mode, to, 0);
      break;

    case BR_REGS:
      /* Moving between BR and anything but GR is impossible.  */
      if (from != GR_REGS && from != GR_AND_BR_REGS)
	return MEMORY_MOVE_COST (mode, to, 0);
      break;

    case AR_I_REGS:
    case AR_M_REGS:
      /* Moving between AR and anything but GR is impossible.  */
      if (from != GR_REGS)
	return MEMORY_MOVE_COST (mode, to, 0);
      break;

    case GR_REGS:
    case FR_REGS:
    case FP_REGS:
    case GR_AND_FR_REGS:
    case GR_AND_BR_REGS:
    case ALL_REGS:
      break;

    default:
      gcc_unreachable ();
    }

  return 2;
}

/* Implement PREFERRED_RELOAD_CLASS.  Place additional restrictions on CLASS
   to use when copying X into that class.  */

enum reg_class
ia64_preferred_reload_class (rtx x, enum reg_class class)
{
  switch (class)
    {
    case FR_REGS:
    case FP_REGS:
      /* Don't allow volatile mem reloads into floating point registers.
	 This is defined to force reload to choose the r/m case instead
	 of the f/f case when reloading (set (reg fX) (mem/v)).  */
      if (MEM_P (x) && MEM_VOLATILE_P (x))
	return NO_REGS;
      
      /* Force all unrecognized constants into the constant pool.  */
      if (CONSTANT_P (x))
	return NO_REGS;
      break;

    case AR_M_REGS:
    case AR_I_REGS:
      if (!OBJECT_P (x))
	return NO_REGS;
      break;

    default:
      break;
    }

  return class;
}

/* This function returns the register class required for a secondary
   register when copying between one of the registers in CLASS, and X,
   using MODE.  A return value of NO_REGS means that no secondary register
   is required.  */

enum reg_class
ia64_secondary_reload_class (enum reg_class class,
			     enum machine_mode mode ATTRIBUTE_UNUSED, rtx x)
{
  int regno = -1;

  if (GET_CODE (x) == REG || GET_CODE (x) == SUBREG)
    regno = true_regnum (x);

  switch (class)
    {
    case BR_REGS:
    case AR_M_REGS:
    case AR_I_REGS:
      /* ??? BR<->BR register copies can happen due to a bad gcse/cse/global
	 interaction.  We end up with two pseudos with overlapping lifetimes
	 both of which are equiv to the same constant, and both which need
	 to be in BR_REGS.  This seems to be a cse bug.  cse_basic_block_end
	 changes depending on the path length, which means the qty_first_reg
	 check in make_regs_eqv can give different answers at different times.
	 At some point I'll probably need a reload_indi pattern to handle
	 this.

	 We can also get GR_AND_FR_REGS to BR_REGS/AR_REGS copies, where we
	 wound up with a FP register from GR_AND_FR_REGS.  Extend that to all
	 non-general registers for good measure.  */
      if (regno >= 0 && ! GENERAL_REGNO_P (regno))
	return GR_REGS;

      /* This is needed if a pseudo used as a call_operand gets spilled to a
	 stack slot.  */
      if (GET_CODE (x) == MEM)
	return GR_REGS;
      break;

    case FR_REGS:
    case FP_REGS:
      /* Need to go through general registers to get to other class regs.  */
      if (regno >= 0 && ! (FR_REGNO_P (regno) || GENERAL_REGNO_P (regno)))
	return GR_REGS;

      /* This can happen when a paradoxical subreg is an operand to the
	 muldi3 pattern.  */
      /* ??? This shouldn't be necessary after instruction scheduling is
	 enabled, because paradoxical subregs are not accepted by
	 register_operand when INSN_SCHEDULING is defined.  Or alternatively,
	 stop the paradoxical subreg stupidity in the *_operand functions
	 in recog.c.  */
      if (GET_CODE (x) == MEM
	  && (GET_MODE (x) == SImode || GET_MODE (x) == HImode
	      || GET_MODE (x) == QImode))
	return GR_REGS;

      /* This can happen because of the ior/and/etc patterns that accept FP
	 registers as operands.  If the third operand is a constant, then it
	 needs to be reloaded into a FP register.  */
      if (GET_CODE (x) == CONST_INT)
	return GR_REGS;

      /* This can happen because of register elimination in a muldi3 insn.
	 E.g. `26107 * (unsigned long)&u'.  */
      if (GET_CODE (x) == PLUS)
	return GR_REGS;
      break;

    case PR_REGS:
      /* ??? This happens if we cse/gcse a BImode value across a call,
	 and the function has a nonlocal goto.  This is because global
	 does not allocate call crossing pseudos to hard registers when
	 current_function_has_nonlocal_goto is true.  This is relatively
	 common for C++ programs that use exceptions.  To reproduce,
	 return NO_REGS and compile libstdc++.  */
      if (GET_CODE (x) == MEM)
	return GR_REGS;

      /* This can happen when we take a BImode subreg of a DImode value,
	 and that DImode value winds up in some non-GR register.  */
      if (regno >= 0 && ! GENERAL_REGNO_P (regno) && ! PR_REGNO_P (regno))
	return GR_REGS;
      break;

    default:
      break;
    }

  return NO_REGS;
}


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
     used, e.g., to ensure that kernel mode code doesn't use f32-f127.  */

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
}

/* Implement TARGET_HANDLE_OPTION.  */

static bool
ia64_handle_option (size_t code, const char *arg, int value)
{
  switch (code)
    {
    case OPT_mfixed_range_:
      fix_range (arg);
      return true;

    case OPT_mtls_size_:
      if (value != 14 && value != 22 && value != 64)
	error ("bad value %<%s%> for -mtls-size= switch", arg);
      return true;

    case OPT_mtune_:
      {
	static struct pta
	  {
	    const char *name;		/* processor name or nickname.  */
	    enum processor_type processor;
	  }
	const processor_alias_table[] =
	  {
	    {"itanium", PROCESSOR_ITANIUM},
	    {"itanium1", PROCESSOR_ITANIUM},
	    {"merced", PROCESSOR_ITANIUM},
	    {"itanium2", PROCESSOR_ITANIUM2},
	    {"mckinley", PROCESSOR_ITANIUM2},
	  };
	int const pta_size = ARRAY_SIZE (processor_alias_table);
	int i;

	for (i = 0; i < pta_size; i++)
	  if (!strcmp (arg, processor_alias_table[i].name))
	    {
	      ia64_tune = processor_alias_table[i].processor;
	      break;
	    }
	if (i == pta_size)
	  error ("bad value %<%s%> for -mtune= switch", arg);
	return true;
      }

    default:
      return true;
    }
}

/* Implement OVERRIDE_OPTIONS.  */

void
ia64_override_options (void)
{
  if (TARGET_AUTO_PIC)
    target_flags |= MASK_CONST_GP;

  if (TARGET_INLINE_SQRT == INL_MIN_LAT)
    {
      warning (0, "not yet implemented: latency-optimized inline square root");
      TARGET_INLINE_SQRT = INL_MAX_THR;
    }

  ia64_flag_schedule_insns2 = flag_schedule_insns_after_reload;
  flag_schedule_insns_after_reload = 0;

  ia64_section_threshold = g_switch_set ? g_switch_value : IA64_DEFAULT_GVALUE;

  init_machine_status = ia64_init_machine_status;
}

static struct machine_function *
ia64_init_machine_status (void)
{
  return ggc_alloc_cleared (sizeof (struct machine_function));
}

static enum attr_itanium_class ia64_safe_itanium_class (rtx);
static enum attr_type ia64_safe_type (rtx);

static enum attr_itanium_class
ia64_safe_itanium_class (rtx insn)
{
  if (recog_memoized (insn) >= 0)
    return get_attr_itanium_class (insn);
  else
    return ITANIUM_CLASS_UNKNOWN;
}

static enum attr_type
ia64_safe_type (rtx insn)
{
  if (recog_memoized (insn) >= 0)
    return get_attr_type (insn);
  else
    return TYPE_UNKNOWN;
}

/* The following collection of routines emit instruction group stop bits as
   necessary to avoid dependencies.  */

/* Need to track some additional registers as far as serialization is
   concerned so we can properly handle br.call and br.ret.  We could
   make these registers visible to gcc, but since these registers are
   never explicitly used in gcc generated code, it seems wasteful to
   do so (plus it would make the call and return patterns needlessly
   complex).  */
#define REG_RP		(BR_REG (0))
#define REG_AR_CFM	(FIRST_PSEUDO_REGISTER + 1)
/* This is used for volatile asms which may require a stop bit immediately
   before and after them.  */
#define REG_VOLATILE	(FIRST_PSEUDO_REGISTER + 2)
#define AR_UNAT_BIT_0	(FIRST_PSEUDO_REGISTER + 3)
#define NUM_REGS	(AR_UNAT_BIT_0 + 64)

/* For each register, we keep track of how it has been written in the
   current instruction group.

   If a register is written unconditionally (no qualifying predicate),
   WRITE_COUNT is set to 2 and FIRST_PRED is ignored.

   If a register is written if its qualifying predicate P is true, we
   set WRITE_COUNT to 1 and FIRST_PRED to P.  Later on, the same register
   may be written again by the complement of P (P^1) and when this happens,
   WRITE_COUNT gets set to 2.

   The result of this is that whenever an insn attempts to write a register
   whose WRITE_COUNT is two, we need to issue an insn group barrier first.

   If a predicate register is written by a floating-point insn, we set
   WRITTEN_BY_FP to true.

   If a predicate register is written by an AND.ORCM we set WRITTEN_BY_AND
   to true; if it was written by an OR.ANDCM we set WRITTEN_BY_OR to true.  */

struct reg_write_state
{
  unsigned int write_count : 2;
  unsigned int first_pred : 16;
  unsigned int written_by_fp : 1;
  unsigned int written_by_and : 1;
  unsigned int written_by_or : 1;
};

/* Cumulative info for the current instruction group.  */
struct reg_write_state rws_sum[NUM_REGS];
/* Info for the current instruction.  This gets copied to rws_sum after a
   stop bit is emitted.  */
struct reg_write_state rws_insn[NUM_REGS];

/* Indicates whether this is the first instruction after a stop bit,
   in which case we don't need another stop bit.  Without this,
   ia64_variable_issue will die when scheduling an alloc.  */
static int first_instruction;

/* Misc flags needed to compute RAW/WAW dependencies while we are traversing
   RTL for one instruction.  */
struct reg_flags
{
  unsigned int is_write : 1;	/* Is register being written?  */
  unsigned int is_fp : 1;	/* Is register used as part of an fp op?  */
  unsigned int is_branch : 1;	/* Is register used as part of a branch?  */
  unsigned int is_and : 1;	/* Is register used as part of and.orcm?  */
  unsigned int is_or : 1;	/* Is register used as part of or.andcm?  */
  unsigned int is_sibcall : 1;	/* Is this a sibling or normal call?  */
};

static void rws_update (struct reg_write_state *, int, struct reg_flags, int);
static int rws_access_regno (int, struct reg_flags, int);
static int rws_access_reg (rtx, struct reg_flags, int);
static void update_set_flags (rtx, struct reg_flags *);
static int set_src_needs_barrier (rtx, struct reg_flags, int);
static int rtx_needs_barrier (rtx, struct reg_flags, int);
static void init_insn_group_barriers (void);
static int group_barrier_needed (rtx);
static int safe_group_barrier_needed (rtx);

/* Update *RWS for REGNO, which is being written by the current instruction,
   with predicate PRED, and associated register flags in FLAGS.  */

static void
rws_update (struct reg_write_state *rws, int regno, struct reg_flags flags, int pred)
{
  if (pred)
    rws[regno].write_count++;
  else
    rws[regno].write_count = 2;
  rws[regno].written_by_fp |= flags.is_fp;
  /* ??? Not tracking and/or across differing predicates.  */
  rws[regno].written_by_and = flags.is_and;
  rws[regno].written_by_or = flags.is_or;
  rws[regno].first_pred = pred;
}

/* Handle an access to register REGNO of type FLAGS using predicate register
   PRED.  Update rws_insn and rws_sum arrays.  Return 1 if this access creates
   a dependency with an earlier instruction in the same group.  */

static int
rws_access_regno (int regno, struct reg_flags flags, int pred)
{
  int need_barrier = 0;

  gcc_assert (regno < NUM_REGS);

  if (! PR_REGNO_P (regno))
    flags.is_and = flags.is_or = 0;

  if (flags.is_write)
    {
      int write_count;

      /* One insn writes same reg multiple times?  */
      gcc_assert (!rws_insn[regno].write_count);

      /* Update info for current instruction.  */
      rws_update (rws_insn, regno, flags, pred);
      write_count = rws_sum[regno].write_count;

      switch (write_count)
	{
	case 0:
	  /* The register has not been written yet.  */
	  rws_update (rws_sum, regno, flags, pred);
	  break;

	case 1:
	  /* The register has been written via a predicate.  If this is
	     not a complementary predicate, then we need a barrier.  */
	  /* ??? This assumes that P and P+1 are always complementary
	     predicates for P even.  */
	  if (flags.is_and && rws_sum[regno].written_by_and)
	    ;
	  else if (flags.is_or && rws_sum[regno].written_by_or)
	    ;
	  else if ((rws_sum[regno].first_pred ^ 1) != pred)
	    need_barrier = 1;
	  rws_update (rws_sum, regno, flags, pred);
	  break;

	case 2:
	  /* The register has been unconditionally written already.  We
	     need a barrier.  */
	  if (flags.is_and && rws_sum[regno].written_by_and)
	    ;
	  else if (flags.is_or && rws_sum[regno].written_by_or)
	    ;
	  else
	    need_barrier = 1;
	  rws_sum[regno].written_by_and = flags.is_and;
	  rws_sum[regno].written_by_or = flags.is_or;
	  break;

	default:
	  gcc_unreachable ();
	}
    }
  else
    {
      if (flags.is_branch)
	{
	  /* Branches have several RAW exceptions that allow to avoid
	     barriers.  */

	  if (REGNO_REG_CLASS (regno) == BR_REGS || regno == AR_PFS_REGNUM)
	    /* RAW dependencies on branch regs are permissible as long
	       as the writer is a non-branch instruction.  Since we
	       never generate code that uses a branch register written
	       by a branch instruction, handling this case is
	       easy.  */
	    return 0;

	  if (REGNO_REG_CLASS (regno) == PR_REGS
	      && ! rws_sum[regno].written_by_fp)
	    /* The predicates of a branch are available within the
	       same insn group as long as the predicate was written by
	       something other than a floating-point instruction.  */
	    return 0;
	}

      if (flags.is_and && rws_sum[regno].written_by_and)
	return 0;
      if (flags.is_or && rws_sum[regno].written_by_or)
	return 0;

      switch (rws_sum[regno].write_count)
	{
	case 0:
	  /* The register has not been written yet.  */
	  break;

	case 1:
	  /* The register has been written via a predicate.  If this is
	     not a complementary predicate, then we need a barrier.  */
	  /* ??? This assumes that P and P+1 are always complementary
	     predicates for P even.  */
	  if ((rws_sum[regno].first_pred ^ 1) != pred)
	    need_barrier = 1;
	  break;

	case 2:
	  /* The register has been unconditionally written already.  We
	     need a barrier.  */
	  need_barrier = 1;
	  break;

	default:
	  gcc_unreachable ();
	}
    }

  return need_barrier;
}

static int
rws_access_reg (rtx reg, struct reg_flags flags, int pred)
{
  int regno = REGNO (reg);
  int n = HARD_REGNO_NREGS (REGNO (reg), GET_MODE (reg));

  if (n == 1)
    return rws_access_regno (regno, flags, pred);
  else
    {
      int need_barrier = 0;
      while (--n >= 0)
	need_barrier |= rws_access_regno (regno + n, flags, pred);
      return need_barrier;
    }
}

/* Examine X, which is a SET rtx, and update the flags, the predicate, and
   the condition, stored in *PFLAGS, *PPRED and *PCOND.  */

static void
update_set_flags (rtx x, struct reg_flags *pflags)
{
  rtx src = SET_SRC (x);

  switch (GET_CODE (src))
    {
    case CALL:
      return;

    case IF_THEN_ELSE:
      /* There are four cases here:
	 (1) The destination is (pc), in which case this is a branch,
	 nothing here applies.
	 (2) The destination is ar.lc, in which case this is a
	 doloop_end_internal,
	 (3) The destination is an fp register, in which case this is
	 an fselect instruction.
	 (4) The condition has (unspec [(reg)] UNSPEC_LDC), in which case 
	 this is a check load.
	 In all cases, nothing we do in this function applies.  */
      return;

    default:
      if (COMPARISON_P (src)
	  && SCALAR_FLOAT_MODE_P (GET_MODE (XEXP (src, 0))))
	/* Set pflags->is_fp to 1 so that we know we're dealing
	   with a floating point comparison when processing the
	   destination of the SET.  */
	pflags->is_fp = 1;

      /* Discover if this is a parallel comparison.  We only handle
	 and.orcm and or.andcm at present, since we must retain a
	 strict inverse on the predicate pair.  */
      else if (GET_CODE (src) == AND)
	pflags->is_and = 1;
      else if (GET_CODE (src) == IOR)
	pflags->is_or = 1;

      break;
    }
}

/* Subroutine of rtx_needs_barrier; this function determines whether the
   source of a given SET rtx found in X needs a barrier.  FLAGS and PRED
   are as in rtx_needs_barrier.  COND is an rtx that holds the condition
   for this insn.  */

static int
set_src_needs_barrier (rtx x, struct reg_flags flags, int pred)
{
  int need_barrier = 0;
  rtx dst;
  rtx src = SET_SRC (x);

  if (GET_CODE (src) == CALL)
    /* We don't need to worry about the result registers that
       get written by subroutine call.  */
    return rtx_needs_barrier (src, flags, pred);
  else if (SET_DEST (x) == pc_rtx)
    {
      /* X is a conditional branch.  */
      /* ??? This seems redundant, as the caller sets this bit for
	 all JUMP_INSNs.  */
      if (!ia64_spec_check_src_p (src))
	flags.is_branch = 1;
      return rtx_needs_barrier (src, flags, pred);
    }

  if (ia64_spec_check_src_p (src))
    /* Avoid checking one register twice (in condition 
       and in 'then' section) for ldc pattern.  */
    {
      gcc_assert (REG_P (XEXP (src, 2)));
      need_barrier = rtx_needs_barrier (XEXP (src, 2), flags, pred);
		  
      /* We process MEM below.  */
      src = XEXP (src, 1);
    }

  need_barrier |= rtx_needs_barrier (src, flags, pred);

  dst = SET_DEST (x);
  if (GET_CODE (dst) == ZERO_EXTRACT)
    {
      need_barrier |= rtx_needs_barrier (XEXP (dst, 1), flags, pred);
      need_barrier |= rtx_needs_barrier (XEXP (dst, 2), flags, pred);
    }
  return need_barrier;
}

/* Handle an access to rtx X of type FLAGS using predicate register
   PRED.  Return 1 if this access creates a dependency with an earlier
   instruction in the same group.  */

static int
rtx_needs_barrier (rtx x, struct reg_flags flags, int pred)
{
  int i, j;
  int is_complemented = 0;
  int need_barrier = 0;
  const char *format_ptr;
  struct reg_flags new_flags;
  rtx cond;

  if (! x)
    return 0;

  new_flags = flags;

  switch (GET_CODE (x))
    {
    case SET:
      update_set_flags (x, &new_flags);
      need_barrier = set_src_needs_barrier (x, new_flags, pred);
      if (GET_CODE (SET_SRC (x)) != CALL)
	{
	  new_flags.is_write = 1;
	  need_barrier |= rtx_needs_barrier (SET_DEST (x), new_flags, pred);
	}
      break;

    case CALL:
      new_flags.is_write = 0;
      need_barrier |= rws_access_regno (AR_EC_REGNUM, new_flags, pred);

      /* Avoid multiple register writes, in case this is a pattern with
	 multiple CALL rtx.  This avoids a failure in rws_access_reg.  */
      if (! flags.is_sibcall && ! rws_insn[REG_AR_CFM].write_count)
	{
	  new_flags.is_write = 1;
	  need_barrier |= rws_access_regno (REG_RP, new_flags, pred);
	  need_barrier |= rws_access_regno (AR_PFS_REGNUM, new_flags, pred);
	  need_barrier |= rws_access_regno (REG_AR_CFM, new_flags, pred);
	}
      break;

    case COND_EXEC:
      /* X is a predicated instruction.  */

      cond = COND_EXEC_TEST (x);
      gcc_assert (!pred);
      need_barrier = rtx_needs_barrier (cond, flags, 0);

      if (GET_CODE (cond) == EQ)
	is_complemented = 1;
      cond = XEXP (cond, 0);
      gcc_assert (GET_CODE (cond) == REG
		  && REGNO_REG_CLASS (REGNO (cond)) == PR_REGS);
      pred = REGNO (cond);
      if (is_complemented)
	++pred;

      need_barrier |= rtx_needs_barrier (COND_EXEC_CODE (x), flags, pred);
      return need_barrier;

    case CLOBBER:
    case USE:
      /* Clobber & use are for earlier compiler-phases only.  */
      break;

    case ASM_OPERANDS:
    case ASM_INPUT:
      /* We always emit stop bits for traditional asms.  We emit stop bits
	 for volatile extended asms if TARGET_VOL_ASM_STOP is true.  */
      if (GET_CODE (x) != ASM_OPERANDS
	  || (MEM_VOLATILE_P (x) && TARGET_VOL_ASM_STOP))
	{
	  /* Avoid writing the register multiple times if we have multiple
	     asm outputs.  This avoids a failure in rws_access_reg.  */
	  if (! rws_insn[REG_VOLATILE].write_count)
	    {
	      new_flags.is_write = 1;
	      rws_access_regno (REG_VOLATILE, new_flags, pred);
	    }
	  return 1;
	}

      /* For all ASM_OPERANDS, we must traverse the vector of input operands.
	 We cannot just fall through here since then we would be confused
	 by the ASM_INPUT rtx inside ASM_OPERANDS, which do not indicate
	 traditional asms unlike their normal usage.  */

      for (i = ASM_OPERANDS_INPUT_LENGTH (x) - 1; i >= 0; --i)
	if (rtx_needs_barrier (ASM_OPERANDS_INPUT (x, i), flags, pred))
	  need_barrier = 1;
      break;

    case PARALLEL:
      for (i = XVECLEN (x, 0) - 1; i >= 0; --i)
	{
	  rtx pat = XVECEXP (x, 0, i);
	  switch (GET_CODE (pat))
	    {
	    case SET:
	      update_set_flags (pat, &new_flags);
	      need_barrier |= set_src_needs_barrier (pat, new_flags, pred);
	      break;

	    case USE:
	    case CALL:
	    case ASM_OPERANDS:
	      need_barrier |= rtx_needs_barrier (pat, flags, pred);
	      break;

	    case CLOBBER:
	    case RETURN:
	      break;

	    default:
	      gcc_unreachable ();
	    }
	}
      for (i = XVECLEN (x, 0) - 1; i >= 0; --i)
	{
	  rtx pat = XVECEXP (x, 0, i);
	  if (GET_CODE (pat) == SET)
	    {
	      if (GET_CODE (SET_SRC (pat)) != CALL)
		{
		  new_flags.is_write = 1;
		  need_barrier |= rtx_needs_barrier (SET_DEST (pat), new_flags,
						     pred);
		}
	    }
	  else if (GET_CODE (pat) == CLOBBER || GET_CODE (pat) == RETURN)
	    need_barrier |= rtx_needs_barrier (pat, flags, pred);
	}
      break;

    case SUBREG:
      need_barrier |= rtx_needs_barrier (SUBREG_REG (x), flags, pred);
      break;
    case REG:
      if (REGNO (x) == AR_UNAT_REGNUM)
	{
	  for (i = 0; i < 64; ++i)
	    need_barrier |= rws_access_regno (AR_UNAT_BIT_0 + i, flags, pred);
	}
      else
	need_barrier = rws_access_reg (x, flags, pred);
      break;

    case MEM:
      /* Find the regs used in memory address computation.  */
      new_flags.is_write = 0;
      need_barrier = rtx_needs_barrier (XEXP (x, 0), new_flags, pred);
      break;

    case CONST_INT:   case CONST_DOUBLE:  case CONST_VECTOR:
    case SYMBOL_REF:  case LABEL_REF:     case CONST:
      break;

      /* Operators with side-effects.  */
    case POST_INC:    case POST_DEC:
      gcc_assert (GET_CODE (XEXP (x, 0)) == REG);

      new_flags.is_write = 0;
      need_barrier  = rws_access_reg (XEXP (x, 0), new_flags, pred);
      new_flags.is_write = 1;
      need_barrier |= rws_access_reg (XEXP (x, 0), new_flags, pred);
      break;

    case POST_MODIFY:
      gcc_assert (GET_CODE (XEXP (x, 0)) == REG);

      new_flags.is_write = 0;
      need_barrier  = rws_access_reg (XEXP (x, 0), new_flags, pred);
      need_barrier |= rtx_needs_barrier (XEXP (x, 1), new_flags, pred);
      new_flags.is_write = 1;
      need_barrier |= rws_access_reg (XEXP (x, 0), new_flags, pred);
      break;

      /* Handle common unary and binary ops for efficiency.  */
    case COMPARE:  case PLUS:    case MINUS:   case MULT:      case DIV:
    case MOD:      case UDIV:    case UMOD:    case AND:       case IOR:
    case XOR:      case ASHIFT:  case ROTATE:  case ASHIFTRT:  case LSHIFTRT:
    case ROTATERT: case SMIN:    case SMAX:    case UMIN:      case UMAX:
    case NE:       case EQ:      case GE:      case GT:        case LE:
    case LT:       case GEU:     case GTU:     case LEU:       case LTU:
      need_barrier = rtx_needs_barrier (XEXP (x, 0), new_flags, pred);
      need_barrier |= rtx_needs_barrier (XEXP (x, 1), new_flags, pred);
      break;

    case NEG:      case NOT:	        case SIGN_EXTEND:     case ZERO_EXTEND:
    case TRUNCATE: case FLOAT_EXTEND:   case FLOAT_TRUNCATE:  case FLOAT:
    case FIX:      case UNSIGNED_FLOAT: case UNSIGNED_FIX:    case ABS:
    case SQRT:     case FFS:		case POPCOUNT:
      need_barrier = rtx_needs_barrier (XEXP (x, 0), flags, pred);
      break;

    case VEC_SELECT:
      /* VEC_SELECT's second argument is a PARALLEL with integers that
	 describe the elements selected.  On ia64, those integers are
	 always constants.  Avoid walking the PARALLEL so that we don't
	 get confused with "normal" parallels and then die.  */
      need_barrier = rtx_needs_barrier (XEXP (x, 0), flags, pred);
      break;

    case UNSPEC:
      switch (XINT (x, 1))
	{
	case UNSPEC_LTOFF_DTPMOD:
	case UNSPEC_LTOFF_DTPREL:
	case UNSPEC_DTPREL:
	case UNSPEC_LTOFF_TPREL:
	case UNSPEC_TPREL:
	case UNSPEC_PRED_REL_MUTEX:
	case UNSPEC_PIC_CALL:
        case UNSPEC_MF:
        case UNSPEC_FETCHADD_ACQ:
	case UNSPEC_BSP_VALUE:
	case UNSPEC_FLUSHRS:
	case UNSPEC_BUNDLE_SELECTOR:
          break;

	case UNSPEC_GR_SPILL:
	case UNSPEC_GR_RESTORE:
	  {
	    HOST_WIDE_INT offset = INTVAL (XVECEXP (x, 0, 1));
	    HOST_WIDE_INT bit = (offset >> 3) & 63;

	    need_barrier = rtx_needs_barrier (XVECEXP (x, 0, 0), flags, pred);
	    new_flags.is_write = (XINT (x, 1) == UNSPEC_GR_SPILL);
	    need_barrier |= rws_access_regno (AR_UNAT_BIT_0 + bit,
					      new_flags, pred);
	    break;
	  }

	case UNSPEC_FR_SPILL:
	case UNSPEC_FR_RESTORE:
	case UNSPEC_GETF_EXP:
	case UNSPEC_SETF_EXP:
        case UNSPEC_ADDP4:
	case UNSPEC_FR_SQRT_RECIP_APPROX:
	case UNSPEC_LDA:
	case UNSPEC_LDS:
	case UNSPEC_LDSA:
	case UNSPEC_CHKACLR:
        case UNSPEC_CHKS:
	  need_barrier = rtx_needs_barrier (XVECEXP (x, 0, 0), flags, pred);
	  break;

	case UNSPEC_FR_RECIP_APPROX:
	case UNSPEC_SHRP:
	case UNSPEC_COPYSIGN:
	  need_barrier = rtx_needs_barrier (XVECEXP (x, 0, 0), flags, pred);
	  need_barrier |= rtx_needs_barrier (XVECEXP (x, 0, 1), flags, pred);
	  break;

        case UNSPEC_CMPXCHG_ACQ:
	  need_barrier = rtx_needs_barrier (XVECEXP (x, 0, 1), flags, pred);
	  need_barrier |= rtx_needs_barrier (XVECEXP (x, 0, 2), flags, pred);
	  break;

	default:
	  gcc_unreachable ();
	}
      break;

    case UNSPEC_VOLATILE:
      switch (XINT (x, 1))
	{
	case UNSPECV_ALLOC:
	  /* Alloc must always be the first instruction of a group.
	     We force this by always returning true.  */
	  /* ??? We might get better scheduling if we explicitly check for
	     input/local/output register dependencies, and modify the
	     scheduler so that alloc is always reordered to the start of
	     the current group.  We could then eliminate all of the
	     first_instruction code.  */
	  rws_access_regno (AR_PFS_REGNUM, flags, pred);

	  new_flags.is_write = 1;
	  rws_access_regno (REG_AR_CFM, new_flags, pred);
	  return 1;

	case UNSPECV_SET_BSP:
	  need_barrier = 1;
          break;

	case UNSPECV_BLOCKAGE:
	case UNSPECV_INSN_GROUP_BARRIER:
	case UNSPECV_BREAK:
	case UNSPECV_PSAC_ALL:
	case UNSPECV_PSAC_NORMAL:
	  return 0;

	default:
	  gcc_unreachable ();
	}
      break;

    case RETURN:
      new_flags.is_write = 0;
      need_barrier  = rws_access_regno (REG_RP, flags, pred);
      need_barrier |= rws_access_regno (AR_PFS_REGNUM, flags, pred);

      new_flags.is_write = 1;
      need_barrier |= rws_access_regno (AR_EC_REGNUM, new_flags, pred);
      need_barrier |= rws_access_regno (REG_AR_CFM, new_flags, pred);
      break;

    default:
      format_ptr = GET_RTX_FORMAT (GET_CODE (x));
      for (i = GET_RTX_LENGTH (GET_CODE (x)) - 1; i >= 0; i--)
	switch (format_ptr[i])
	  {
	  case '0':	/* unused field */
	  case 'i':	/* integer */
	  case 'n':	/* note */
	  case 'w':	/* wide integer */
	  case 's':	/* pointer to string */
	  case 'S':	/* optional pointer to string */
	    break;

	  case 'e':
	    if (rtx_needs_barrier (XEXP (x, i), flags, pred))
	      need_barrier = 1;
	    break;

	  case 'E':
	    for (j = XVECLEN (x, i) - 1; j >= 0; --j)
	      if (rtx_needs_barrier (XVECEXP (x, i, j), flags, pred))
		need_barrier = 1;
	    break;

	  default:
	    gcc_unreachable ();
	  }
      break;
    }
  return need_barrier;
}

/* Clear out the state for group_barrier_needed at the start of a
   sequence of insns.  */

static void
init_insn_group_barriers (void)
{
  memset (rws_sum, 0, sizeof (rws_sum));
  first_instruction = 1;
}

/* Given the current state, determine whether a group barrier (a stop bit) is
   necessary before INSN.  Return nonzero if so.  This modifies the state to
   include the effects of INSN as a side-effect.  */

static int
group_barrier_needed (rtx insn)
{
  rtx pat;
  int need_barrier = 0;
  struct reg_flags flags;

  memset (&flags, 0, sizeof (flags));
  switch (GET_CODE (insn))
    {
    case NOTE:
      break;

    case BARRIER:
      /* A barrier doesn't imply an instruction group boundary.  */
      break;

    case CODE_LABEL:
      memset (rws_insn, 0, sizeof (rws_insn));
      return 1;

    case CALL_INSN:
      flags.is_branch = 1;
      flags.is_sibcall = SIBLING_CALL_P (insn);
      memset (rws_insn, 0, sizeof (rws_insn));

      /* Don't bundle a call following another call.  */
      if ((pat = prev_active_insn (insn))
	  && GET_CODE (pat) == CALL_INSN)
	{
	  need_barrier = 1;
	  break;
	}

      need_barrier = rtx_needs_barrier (PATTERN (insn), flags, 0);
      break;

    case JUMP_INSN:
      if (!ia64_spec_check_p (insn))
	flags.is_branch = 1;

      /* Don't bundle a jump following a call.  */
      if ((pat = prev_active_insn (insn))
	  && GET_CODE (pat) == CALL_INSN)
	{
	  need_barrier = 1;
	  break;
	}
      /* FALLTHRU */

    case INSN:
      if (GET_CODE (PATTERN (insn)) == USE
	  || GET_CODE (PATTERN (insn)) == CLOBBER)
	/* Don't care about USE and CLOBBER "insns"---those are used to
	   indicate to the optimizer that it shouldn't get rid of
	   certain operations.  */
	break;

      pat = PATTERN (insn);

      /* Ug.  Hack hacks hacked elsewhere.  */
      switch (recog_memoized (insn))
	{
	  /* We play dependency tricks with the epilogue in order
	     to get proper schedules.  Undo this for dv analysis.  */
	case CODE_FOR_epilogue_deallocate_stack:
	case CODE_FOR_prologue_allocate_stack:
	  pat = XVECEXP (pat, 0, 0);
	  break;

	  /* The pattern we use for br.cloop confuses the code above.
	     The second element of the vector is representative.  */
	case CODE_FOR_doloop_end_internal:
	  pat = XVECEXP (pat, 0, 1);
	  break;

	  /* Doesn't generate code.  */
	case CODE_FOR_pred_rel_mutex:
	case CODE_FOR_prologue_use:
	  return 0;

	default:
	  break;
	}

      memset (rws_insn, 0, sizeof (rws_insn));
      need_barrier = rtx_needs_barrier (pat, flags, 0);

      /* Check to see if the previous instruction was a volatile
	 asm.  */
      if (! need_barrier)
	need_barrier = rws_access_regno (REG_VOLATILE, flags, 0);
      break;

    default:
      gcc_unreachable ();
    }

  if (first_instruction && INSN_P (insn)
      && ia64_safe_itanium_class (insn) != ITANIUM_CLASS_IGNORE
      && GET_CODE (PATTERN (insn)) != USE
      && GET_CODE (PATTERN (insn)) != CLOBBER)
    {
      need_barrier = 0;
      first_instruction = 0;
    }

  return need_barrier;
}

/* Like group_barrier_needed, but do not clobber the current state.  */

static int
safe_group_barrier_needed (rtx insn)
{
  struct reg_write_state rws_saved[NUM_REGS];
  int saved_first_instruction;
  int t;

  memcpy (rws_saved, rws_sum, NUM_REGS * sizeof *rws_saved);
  saved_first_instruction = first_instruction;

  t = group_barrier_needed (insn);

  memcpy (rws_sum, rws_saved, NUM_REGS * sizeof *rws_saved);
  first_instruction = saved_first_instruction;

  return t;
}

/* Scan the current function and insert stop bits as necessary to
   eliminate dependencies.  This function assumes that a final
   instruction scheduling pass has been run which has already
   inserted most of the necessary stop bits.  This function only
   inserts new ones at basic block boundaries, since these are
   invisible to the scheduler.  */

static void
emit_insn_group_barriers (FILE *dump)
{
  rtx insn;
  rtx last_label = 0;
  int insns_since_last_label = 0;

  init_insn_group_barriers ();

  for (insn = get_insns (); insn; insn = NEXT_INSN (insn))
    {
      if (GET_CODE (insn) == CODE_LABEL)
	{
	  if (insns_since_last_label)
	    last_label = insn;
	  insns_since_last_label = 0;
	}
      else if (GET_CODE (insn) == NOTE
	       && NOTE_LINE_NUMBER (insn) == NOTE_INSN_BASIC_BLOCK)
	{
	  if (insns_since_last_label)
	    last_label = insn;
	  insns_since_last_label = 0;
	}
      else if (GET_CODE (insn) == INSN
	       && GET_CODE (PATTERN (insn)) == UNSPEC_VOLATILE
	       && XINT (PATTERN (insn), 1) == UNSPECV_INSN_GROUP_BARRIER)
	{
	  init_insn_group_barriers ();
	  last_label = 0;
	}
      else if (INSN_P (insn))
	{
	  insns_since_last_label = 1;

	  if (group_barrier_needed (insn))
	    {
	      if (last_label)
		{
		  if (dump)
		    fprintf (dump, "Emitting stop before label %d\n",
			     INSN_UID (last_label));
		  emit_insn_before (gen_insn_group_barrier (GEN_INT (3)), last_label);
		  insn = last_label;

		  init_insn_group_barriers ();
		  last_label = 0;
		}
	    }
	}
    }
}

/* Like emit_insn_group_barriers, but run if no final scheduling pass was run.
   This function has to emit all necessary group barriers.  */

static void
emit_all_insn_group_barriers (FILE *dump ATTRIBUTE_UNUSED)
{
  rtx insn;

  init_insn_group_barriers ();

  for (insn = get_insns (); insn; insn = NEXT_INSN (insn))
    {
      if (GET_CODE (insn) == BARRIER)
	{
	  rtx last = prev_active_insn (insn);

	  if (! last)
	    continue;
	  if (GET_CODE (last) == JUMP_INSN
	      && GET_CODE (PATTERN (last)) == ADDR_DIFF_VEC)
	    last = prev_active_insn (last);
	  if (recog_memoized (last) != CODE_FOR_insn_group_barrier)
	    emit_insn_after (gen_insn_group_barrier (GEN_INT (3)), last);

	  init_insn_group_barriers ();
	}
      else if (INSN_P (insn))
	{
	  if (recog_memoized (insn) == CODE_FOR_insn_group_barrier)
	    init_insn_group_barriers ();
	  else if (group_barrier_needed (insn))
	    {
	      emit_insn_before (gen_insn_group_barrier (GEN_INT (3)), insn);
	      init_insn_group_barriers ();
	      group_barrier_needed (insn);
	    }
	}
    }
}



/* Instruction scheduling support.  */

#define NR_BUNDLES 10

/* A list of names of all available bundles.  */

static const char *bundle_name [NR_BUNDLES] =
{
  ".mii",
  ".mmi",
  ".mfi",
  ".mmf",
#if NR_BUNDLES == 10
  ".bbb",
  ".mbb",
#endif
  ".mib",
  ".mmb",
  ".mfb",
  ".mlx"
};

/* Nonzero if we should insert stop bits into the schedule.  */

int ia64_final_schedule = 0;

/* Codes of the corresponding queried units: */

static int _0mii_, _0mmi_, _0mfi_, _0mmf_;
static int _0bbb_, _0mbb_, _0mib_, _0mmb_, _0mfb_, _0mlx_;

static int _1mii_, _1mmi_, _1mfi_, _1mmf_;
static int _1bbb_, _1mbb_, _1mib_, _1mmb_, _1mfb_, _1mlx_;

static int pos_1, pos_2, pos_3, pos_4, pos_5, pos_6;

/* The following variable value is an insn group barrier.  */

static rtx dfa_stop_insn;

/* The following variable value is the last issued insn.  */

static rtx last_scheduled_insn;

/* The following variable value is size of the DFA state.  */

static size_t dfa_state_size;

/* The following variable value is pointer to a DFA state used as
   temporary variable.  */

static state_t temp_dfa_state = NULL;

/* The following variable value is DFA state after issuing the last
   insn.  */

static state_t prev_cycle_state = NULL;

/* The following array element values are TRUE if the corresponding
   insn requires to add stop bits before it.  */

static char *stops_p = NULL;

/* The following array element values are ZERO for non-speculative
   instructions and hold corresponding speculation check number for
   speculative instructions.  */
static int *spec_check_no = NULL;

/* Size of spec_check_no array.  */
static int max_uid = 0;

/* The following variable is used to set up the mentioned above array.  */

static int stop_before_p = 0;

/* The following variable value is length of the arrays `clocks' and
   `add_cycles'. */

static int clocks_length;

/* The following array element values are cycles on which the
   corresponding insn will be issued.  The array is used only for
   Itanium1.  */

static int *clocks;

/* The following array element values are numbers of cycles should be
   added to improve insn scheduling for MM_insns for Itanium1.  */

static int *add_cycles;

/* The following variable value is number of data speculations in progress.  */
static int pending_data_specs = 0;

static rtx ia64_single_set (rtx);
static void ia64_emit_insn_before (rtx, rtx);

/* Map a bundle number to its pseudo-op.  */

const char *
get_bundle_name (int b)
{
  return bundle_name[b];
}


/* Return the maximum number of instructions a cpu can issue.  */

static int
ia64_issue_rate (void)
{
  return 6;
}

/* Helper function - like single_set, but look inside COND_EXEC.  */

static rtx
ia64_single_set (rtx insn)
{
  rtx x = PATTERN (insn), ret;
  if (GET_CODE (x) == COND_EXEC)
    x = COND_EXEC_CODE (x);
  if (GET_CODE (x) == SET)
    return x;

  /* Special case here prologue_allocate_stack and epilogue_deallocate_stack.
     Although they are not classical single set, the second set is there just
     to protect it from moving past FP-relative stack accesses.  */
  switch (recog_memoized (insn))
    {
    case CODE_FOR_prologue_allocate_stack:
    case CODE_FOR_epilogue_deallocate_stack:
      ret = XVECEXP (x, 0, 0);
      break;

    default:
      ret = single_set_2 (insn, x);
      break;
    }

  return ret;
}

/* Adjust the cost of a scheduling dependency.
   Return the new cost of a dependency of type DEP_TYPE or INSN on DEP_INSN.
   COST is the current cost.  */

static int
ia64_adjust_cost_2 (rtx insn, int dep_type1, rtx dep_insn, int cost)
{
  enum reg_note dep_type = (enum reg_note) dep_type1;
  enum attr_itanium_class dep_class;
  enum attr_itanium_class insn_class;

  if (dep_type != REG_DEP_OUTPUT)
    return cost;

  insn_class = ia64_safe_itanium_class (insn);
  dep_class = ia64_safe_itanium_class (dep_insn);
  if (dep_class == ITANIUM_CLASS_ST || dep_class == ITANIUM_CLASS_STF
      || insn_class == ITANIUM_CLASS_ST || insn_class == ITANIUM_CLASS_STF)
    return 0;

  return cost;
}

/* Like emit_insn_before, but skip cycle_display notes.
   ??? When cycle display notes are implemented, update this.  */

static void
ia64_emit_insn_before (rtx insn, rtx before)
{
  emit_insn_before (insn, before);
}

/* The following function marks insns who produce addresses for load
   and store insns.  Such insns will be placed into M slots because it
   decrease latency time for Itanium1 (see function
   `ia64_produce_address_p' and the DFA descriptions).  */

static void
ia64_dependencies_evaluation_hook (rtx head, rtx tail)
{
  rtx insn, link, next, next_tail;

  /* Before reload, which_alternative is not set, which means that
     ia64_safe_itanium_class will produce wrong results for (at least)
     move instructions.  */
  if (!reload_completed)
    return;

  next_tail = NEXT_INSN (tail);
  for (insn = head; insn != next_tail; insn = NEXT_INSN (insn))
    if (INSN_P (insn))
      insn->call = 0;
  for (insn = head; insn != next_tail; insn = NEXT_INSN (insn))
    if (INSN_P (insn)
	&& ia64_safe_itanium_class (insn) == ITANIUM_CLASS_IALU)
      {
	for (link = INSN_DEPEND (insn); link != 0; link = XEXP (link, 1))
	  {
	    enum attr_itanium_class c;

	    if (REG_NOTE_KIND (link) != REG_DEP_TRUE)
	      continue;
	    next = XEXP (link, 0);
	    c = ia64_safe_itanium_class (next);
	    if ((c == ITANIUM_CLASS_ST
		 || c == ITANIUM_CLASS_STF)
		&& ia64_st_address_bypass_p (insn, next))
	      break;
	    else if ((c == ITANIUM_CLASS_LD
		      || c == ITANIUM_CLASS_FLD
		      || c == ITANIUM_CLASS_FLDP)
		     && ia64_ld_address_bypass_p (insn, next))
	      break;
	  }
	insn->call = link != 0;
      }
}

/* We're beginning a new block.  Initialize data structures as necessary.  */

static void
ia64_sched_init (FILE *dump ATTRIBUTE_UNUSED,
		 int sched_verbose ATTRIBUTE_UNUSED,
		 int max_ready ATTRIBUTE_UNUSED)
{
#ifdef ENABLE_CHECKING
  rtx insn;

  if (reload_completed)
    for (insn = NEXT_INSN (current_sched_info->prev_head);
	 insn != current_sched_info->next_tail;
	 insn = NEXT_INSN (insn))
      gcc_assert (!SCHED_GROUP_P (insn));
#endif
  last_scheduled_insn = NULL_RTX;
  init_insn_group_barriers ();
}

/* We're beginning a scheduling pass.  Check assertion.  */

static void
ia64_sched_init_global (FILE *dump ATTRIBUTE_UNUSED,
                        int sched_verbose ATTRIBUTE_UNUSED,
                        int max_ready ATTRIBUTE_UNUSED)
{  
  gcc_assert (!pending_data_specs);
}

/* Scheduling pass is now finished.  Free/reset static variable.  */
static void
ia64_sched_finish_global (FILE *dump ATTRIBUTE_UNUSED,
			  int sched_verbose ATTRIBUTE_UNUSED)
{
  free (spec_check_no);
  spec_check_no = 0;
  max_uid = 0;
}

/* We are about to being issuing insns for this clock cycle.
   Override the default sort algorithm to better slot instructions.  */

static int
ia64_dfa_sched_reorder (FILE *dump, int sched_verbose, rtx *ready,
			int *pn_ready, int clock_var ATTRIBUTE_UNUSED,
			int reorder_type)
{
  int n_asms;
  int n_ready = *pn_ready;
  rtx *e_ready = ready + n_ready;
  rtx *insnp;

  if (sched_verbose)
    fprintf (dump, "// ia64_dfa_sched_reorder (type %d):\n", reorder_type);

  if (reorder_type == 0)
    {
      /* First, move all USEs, CLOBBERs and other crud out of the way.  */
      n_asms = 0;
      for (insnp = ready; insnp < e_ready; insnp++)
	if (insnp < e_ready)
	  {
	    rtx insn = *insnp;
	    enum attr_type t = ia64_safe_type (insn);
	    if (t == TYPE_UNKNOWN)
	      {
		if (GET_CODE (PATTERN (insn)) == ASM_INPUT
		    || asm_noperands (PATTERN (insn)) >= 0)
		  {
		    rtx lowest = ready[n_asms];
		    ready[n_asms] = insn;
		    *insnp = lowest;
		    n_asms++;
		  }
		else
		  {
		    rtx highest = ready[n_ready - 1];
		    ready[n_ready - 1] = insn;
		    *insnp = highest;
		    return 1;
		  }
	      }
	  }

      if (n_asms < n_ready)
	{
	  /* Some normal insns to process.  Skip the asms.  */
	  ready += n_asms;
	  n_ready -= n_asms;
	}
      else if (n_ready > 0)
	return 1;
    }

  if (ia64_final_schedule)
    {
      int deleted = 0;
      int nr_need_stop = 0;

      for (insnp = ready; insnp < e_ready; insnp++)
	if (safe_group_barrier_needed (*insnp))
	  nr_need_stop++;

      if (reorder_type == 1 && n_ready == nr_need_stop)
	return 0;
      if (reorder_type == 0)
	return 1;
      insnp = e_ready;
      /* Move down everything that needs a stop bit, preserving
	 relative order.  */
      while (insnp-- > ready + deleted)
	while (insnp >= ready + deleted)
	  {
	    rtx insn = *insnp;
	    if (! safe_group_barrier_needed (insn))
	      break;
	    memmove (ready + 1, ready, (insnp - ready) * sizeof (rtx));
	    *ready = insn;
	    deleted++;
	  }
      n_ready -= deleted;
      ready += deleted;
    }

  return 1;
}

/* We are about to being issuing insns for this clock cycle.  Override
   the default sort algorithm to better slot instructions.  */

static int
ia64_sched_reorder (FILE *dump, int sched_verbose, rtx *ready, int *pn_ready,
		    int clock_var)
{
  return ia64_dfa_sched_reorder (dump, sched_verbose, ready,
				 pn_ready, clock_var, 0);
}

/* Like ia64_sched_reorder, but called after issuing each insn.
   Override the default sort algorithm to better slot instructions.  */

static int
ia64_sched_reorder2 (FILE *dump ATTRIBUTE_UNUSED,
		     int sched_verbose ATTRIBUTE_UNUSED, rtx *ready,
		     int *pn_ready, int clock_var)
{
  if (ia64_tune == PROCESSOR_ITANIUM && reload_completed && last_scheduled_insn)
    clocks [INSN_UID (last_scheduled_insn)] = clock_var;
  return ia64_dfa_sched_reorder (dump, sched_verbose, ready, pn_ready,
				 clock_var, 1);
}

/* We are about to issue INSN.  Return the number of insns left on the
   ready queue that can be issued this cycle.  */

static int
ia64_variable_issue (FILE *dump ATTRIBUTE_UNUSED,
		     int sched_verbose ATTRIBUTE_UNUSED,
		     rtx insn ATTRIBUTE_UNUSED,
		     int can_issue_more ATTRIBUTE_UNUSED)
{
  if (current_sched_info->flags & DO_SPECULATION)
    /* Modulo scheduling does not extend h_i_d when emitting
       new instructions.  Deal with it.  */
    {
      if (DONE_SPEC (insn) & BEGIN_DATA)
	pending_data_specs++;
      if (CHECK_SPEC (insn) & BEGIN_DATA)
	pending_data_specs--;
    }

  last_scheduled_insn = insn;
  memcpy (prev_cycle_state, curr_state, dfa_state_size);
  if (reload_completed)
    {
      int needed = group_barrier_needed (insn);
      
      gcc_assert (!needed);
      if (GET_CODE (insn) == CALL_INSN)
	init_insn_group_barriers ();
      stops_p [INSN_UID (insn)] = stop_before_p;
      stop_before_p = 0;
    }
  return 1;
}

/* We are choosing insn from the ready queue.  Return nonzero if INSN
   can be chosen.  */

static int
ia64_first_cycle_multipass_dfa_lookahead_guard (rtx insn)
{
  gcc_assert (insn  && INSN_P (insn));
  return ((!reload_completed
	   || !safe_group_barrier_needed (insn))
	  && ia64_first_cycle_multipass_dfa_lookahead_guard_spec (insn));
}

/* We are choosing insn from the ready queue.  Return nonzero if INSN
   can be chosen.  */

static bool
ia64_first_cycle_multipass_dfa_lookahead_guard_spec (rtx insn)
{
  gcc_assert (insn  && INSN_P (insn));
  /* Size of ALAT is 32.  As far as we perform conservative data speculation,
     we keep ALAT half-empty.  */
  return (pending_data_specs < 16
	  || !(TODO_SPEC (insn) & BEGIN_DATA));
}

/* The following variable value is pseudo-insn used by the DFA insn
   scheduler to change the DFA state when the simulated clock is
   increased.  */

static rtx dfa_pre_cycle_insn;

/* We are about to being issuing INSN.  Return nonzero if we cannot
   issue it on given cycle CLOCK and return zero if we should not sort
   the ready queue on the next clock start.  */

static int
ia64_dfa_new_cycle (FILE *dump, int verbose, rtx insn, int last_clock,
		    int clock, int *sort_p)
{
  int setup_clocks_p = FALSE;

  gcc_assert (insn && INSN_P (insn));
  if ((reload_completed && safe_group_barrier_needed (insn))
      || (last_scheduled_insn
	  && (GET_CODE (last_scheduled_insn) == CALL_INSN
	      || GET_CODE (PATTERN (last_scheduled_insn)) == ASM_INPUT
	      || asm_noperands (PATTERN (last_scheduled_insn)) >= 0)))
    {
      init_insn_group_barriers ();
      if (verbose && dump)
	fprintf (dump, "//    Stop should be before %d%s\n", INSN_UID (insn),
		 last_clock == clock ? " + cycle advance" : "");
      stop_before_p = 1;
      if (last_clock == clock)
	{
	  state_transition (curr_state, dfa_stop_insn);
	  if (TARGET_EARLY_STOP_BITS)
	    *sort_p = (last_scheduled_insn == NULL_RTX
		       || GET_CODE (last_scheduled_insn) != CALL_INSN);
	  else
	    *sort_p = 0;
	  return 1;
	}
      else if (reload_completed)
	setup_clocks_p = TRUE;
      if (GET_CODE (PATTERN (last_scheduled_insn)) == ASM_INPUT
	  || asm_noperands (PATTERN (last_scheduled_insn)) >= 0)
	state_reset (curr_state);
      else
	{
	  memcpy (curr_state, prev_cycle_state, dfa_state_size);
	  state_transition (curr_state, dfa_stop_insn);
	  state_transition (curr_state, dfa_pre_cycle_insn);
	  state_transition (curr_state, NULL);
	}
    }
  else if (reload_completed)
    setup_clocks_p = TRUE;
  if (setup_clocks_p && ia64_tune == PROCESSOR_ITANIUM
      && GET_CODE (PATTERN (insn)) != ASM_INPUT
      && asm_noperands (PATTERN (insn)) < 0)
    {
      enum attr_itanium_class c = ia64_safe_itanium_class (insn);

      if (c != ITANIUM_CLASS_MMMUL && c != ITANIUM_CLASS_MMSHF)
	{
	  rtx link;
	  int d = -1;

	  for (link = LOG_LINKS (insn); link; link = XEXP (link, 1))
	    if (REG_NOTE_KIND (link) == 0)
	      {
		enum attr_itanium_class dep_class;
		rtx dep_insn = XEXP (link, 0);

		dep_class = ia64_safe_itanium_class (dep_insn);
		if ((dep_class == ITANIUM_CLASS_MMMUL
		     || dep_class == ITANIUM_CLASS_MMSHF)
		    && last_clock - clocks [INSN_UID (dep_insn)] < 4
		    && (d < 0
			|| last_clock - clocks [INSN_UID (dep_insn)] < d))
		  d = last_clock - clocks [INSN_UID (dep_insn)];
	      }
	  if (d >= 0)
	    add_cycles [INSN_UID (insn)] = 3 - d;
	}
    }
  return 0;
}

/* Implement targetm.sched.h_i_d_extended hook.
   Extend internal data structures.  */
static void
ia64_h_i_d_extended (void)
{
  if (current_sched_info->flags & DO_SPECULATION)
    {
      int new_max_uid = get_max_uid () + 1;

      spec_check_no = xrecalloc (spec_check_no, new_max_uid,
				 max_uid, sizeof (*spec_check_no));
      max_uid = new_max_uid;
    }

  if (stops_p != NULL) 
    {
      int new_clocks_length = get_max_uid () + 1;
      
      stops_p = xrecalloc (stops_p, new_clocks_length, clocks_length, 1);
      
      if (ia64_tune == PROCESSOR_ITANIUM)
	{
	  clocks = xrecalloc (clocks, new_clocks_length, clocks_length,
			      sizeof (int));
	  add_cycles = xrecalloc (add_cycles, new_clocks_length, clocks_length,
				  sizeof (int));
	}
      
      clocks_length = new_clocks_length;
    }
}

/* Constants that help mapping 'enum machine_mode' to int.  */
enum SPEC_MODES
  {
    SPEC_MODE_INVALID = -1,
    SPEC_MODE_FIRST = 0,
    SPEC_MODE_FOR_EXTEND_FIRST = 1,
    SPEC_MODE_FOR_EXTEND_LAST = 3,
    SPEC_MODE_LAST = 8
  };

/* Return index of the MODE.  */
static int
ia64_mode_to_int (enum machine_mode mode)
{
  switch (mode)
    {
    case BImode: return 0; /* SPEC_MODE_FIRST  */
    case QImode: return 1; /* SPEC_MODE_FOR_EXTEND_FIRST  */
    case HImode: return 2;
    case SImode: return 3; /* SPEC_MODE_FOR_EXTEND_LAST  */
    case DImode: return 4;
    case SFmode: return 5;
    case DFmode: return 6;
    case XFmode: return 7;
    case TImode:
      /* ??? This mode needs testing.  Bypasses for ldfp8 instruction are not
	 mentioned in itanium[12].md.  Predicate fp_register_operand also
	 needs to be defined.  Bottom line: better disable for now.  */
      return SPEC_MODE_INVALID;
    default:     return SPEC_MODE_INVALID;
    }
}

/* Provide information about speculation capabilities.  */
static void
ia64_set_sched_flags (spec_info_t spec_info)
{
  unsigned int *flags = &(current_sched_info->flags);

  if (*flags & SCHED_RGN
      || *flags & SCHED_EBB)  
    {
      int mask = 0;

      if ((mflag_sched_br_data_spec && !reload_completed && optimize > 0)
	  || (mflag_sched_ar_data_spec && reload_completed))
	{
	  mask |= BEGIN_DATA;
	  
	  if ((mflag_sched_br_in_data_spec && !reload_completed)
	      || (mflag_sched_ar_in_data_spec && reload_completed))
	    mask |= BE_IN_DATA;
	}
      
      if (mflag_sched_control_spec)
	{
	  mask |= BEGIN_CONTROL;
	  
	  if (mflag_sched_in_control_spec)
	    mask |= BE_IN_CONTROL;
	}

      gcc_assert (*flags & USE_GLAT);

      if (mask)
	{
	  *flags |= USE_DEPS_LIST | DETACH_LIFE_INFO | DO_SPECULATION;
	  
	  spec_info->mask = mask;
	  spec_info->flags = 0;
      
	  if ((mask & DATA_SPEC) && mflag_sched_prefer_non_data_spec_insns)
	    spec_info->flags |= PREFER_NON_DATA_SPEC;

	  if ((mask & CONTROL_SPEC)
	      && mflag_sched_prefer_non_control_spec_insns)
	    spec_info->flags |= PREFER_NON_CONTROL_SPEC;

	  if (mflag_sched_spec_verbose)
	    {
	      if (sched_verbose >= 1)
		spec_info->dump = sched_dump;
	      else
		spec_info->dump = stderr;
	    }
	  else
	    spec_info->dump = 0;
	  
	  if (mflag_sched_count_spec_in_critical_path)
	    spec_info->flags |= COUNT_SPEC_IN_CRITICAL_PATH;
	}
    }
}

/* Implement targetm.sched.speculate_insn hook.
   Check if the INSN can be TS speculative.
   If 'no' - return -1.
   If 'yes' - generate speculative pattern in the NEW_PAT and return 1.
   If current pattern of the INSN already provides TS speculation, return 0.  */
static int
ia64_speculate_insn (rtx insn, ds_t ts, rtx *new_pat)
{  
  rtx pat, reg, mem, mem_reg;
  int mode_no, gen_p = 1;
  bool extend_p;
  
  gcc_assert (!(ts & ~BEGIN_SPEC) && ts);
           
  pat = PATTERN (insn);

  if (GET_CODE (pat) == COND_EXEC)
    pat = COND_EXEC_CODE (pat);

  /* This should be a SET ...  */
  if (GET_CODE (pat) != SET)
    return -1;

  reg = SET_DEST (pat);
  /* ... to the general/fp register ...  */
  if (!REG_P (reg) || !(GR_REGNO_P (REGNO (reg)) || FP_REGNO_P (REGNO (reg))))
    return -1;

  /* ... from the mem ...  */
  mem = SET_SRC (pat);

  /* ... that can, possibly, be a zero_extend ...  */
  if (GET_CODE (mem) == ZERO_EXTEND)
    {
      mem = XEXP (mem, 0);
      extend_p = true;      
    }
  else
    extend_p = false;

  /* ... or a speculative load.  */
  if (GET_CODE (mem) == UNSPEC)
    {
      int code;
      
      code = XINT (mem, 1);
      if (code != UNSPEC_LDA && code != UNSPEC_LDS && code != UNSPEC_LDSA)
	return -1;

      if ((code == UNSPEC_LDA && !(ts & BEGIN_CONTROL))
	  || (code == UNSPEC_LDS && !(ts & BEGIN_DATA))
	  || code == UNSPEC_LDSA)
	gen_p = 0;

      mem = XVECEXP (mem, 0, 0);
      gcc_assert (MEM_P (mem));
    }

  /* Source should be a mem ...  */
  if (!MEM_P (mem))
    return -1;

  /* ... addressed by a register.  */
  mem_reg = XEXP (mem, 0);
  if (!REG_P (mem_reg))
    return -1;
     
  /* We should use MEM's mode since REG's mode in presence of ZERO_EXTEND
     will always be DImode.  */
  mode_no = ia64_mode_to_int (GET_MODE (mem));
  
  if (mode_no == SPEC_MODE_INVALID
      || (extend_p
	  && !(SPEC_MODE_FOR_EXTEND_FIRST <= mode_no
	       && mode_no <= SPEC_MODE_FOR_EXTEND_LAST)))
    return -1;

  extract_insn_cached (insn);
  gcc_assert (reg == recog_data.operand[0] && mem == recog_data.operand[1]);

  *new_pat = ia64_gen_spec_insn (insn, ts, mode_no, gen_p != 0, extend_p);

  return gen_p;
}

enum
  {
    /* Offset to reach ZERO_EXTEND patterns.  */
    SPEC_GEN_EXTEND_OFFSET = SPEC_MODE_LAST - SPEC_MODE_FOR_EXTEND_FIRST + 1,
    /* Number of patterns for each speculation mode.  */
    SPEC_N = (SPEC_MODE_LAST
              + SPEC_MODE_FOR_EXTEND_LAST - SPEC_MODE_FOR_EXTEND_FIRST + 2)
  };

enum SPEC_GEN_LD_MAP
  {
    /* Offset to ld.a patterns.  */
    SPEC_GEN_A = 0 * SPEC_N,
    /* Offset to ld.s patterns.  */
    SPEC_GEN_S = 1 * SPEC_N,
    /* Offset to ld.sa patterns.  */
    SPEC_GEN_SA = 2 * SPEC_N,
    /* Offset to ld.sa patterns.  For this patterns corresponding ld.c will
       mutate to chk.s.  */
    SPEC_GEN_SA_FOR_S = 3 * SPEC_N
  };

/* These offsets are used to get (4 * SPEC_N).  */
enum SPEC_GEN_CHECK_OFFSET
  {
    SPEC_GEN_CHKA_FOR_A_OFFSET = 4 * SPEC_N - SPEC_GEN_A,
    SPEC_GEN_CHKA_FOR_SA_OFFSET = 4 * SPEC_N - SPEC_GEN_SA
  };

/* If GEN_P is true, calculate the index of needed speculation check and return
   speculative pattern for INSN with speculative mode TS, machine mode
   MODE_NO and with ZERO_EXTEND (if EXTEND_P is true).
   If GEN_P is false, just calculate the index of needed speculation check.  */
static rtx
ia64_gen_spec_insn (rtx insn, ds_t ts, int mode_no, bool gen_p, bool extend_p)
{
  rtx pat, new_pat;
  int load_no;
  int shift = 0;

  static rtx (* const gen_load[]) (rtx, rtx) = {
    gen_movbi_advanced,
    gen_movqi_advanced,
    gen_movhi_advanced,
    gen_movsi_advanced,
    gen_movdi_advanced,
    gen_movsf_advanced,
    gen_movdf_advanced,
    gen_movxf_advanced,
    gen_movti_advanced,
    gen_zero_extendqidi2_advanced,
    gen_zero_extendhidi2_advanced,
    gen_zero_extendsidi2_advanced,

    gen_movbi_speculative,
    gen_movqi_speculative,
    gen_movhi_speculative,
    gen_movsi_speculative,
    gen_movdi_speculative,
    gen_movsf_speculative,
    gen_movdf_speculative,
    gen_movxf_speculative,
    gen_movti_speculative,
    gen_zero_extendqidi2_speculative,
    gen_zero_extendhidi2_speculative,
    gen_zero_extendsidi2_speculative,

    gen_movbi_speculative_advanced,
    gen_movqi_speculative_advanced,
    gen_movhi_speculative_advanced,
    gen_movsi_speculative_advanced,
    gen_movdi_speculative_advanced,
    gen_movsf_speculative_advanced,
    gen_movdf_speculative_advanced,
    gen_movxf_speculative_advanced,
    gen_movti_speculative_advanced,
    gen_zero_extendqidi2_speculative_advanced,
    gen_zero_extendhidi2_speculative_advanced,
    gen_zero_extendsidi2_speculative_advanced,

    gen_movbi_speculative_advanced,
    gen_movqi_speculative_advanced,
    gen_movhi_speculative_advanced,
    gen_movsi_speculative_advanced,
    gen_movdi_speculative_advanced,
    gen_movsf_speculative_advanced,
    gen_movdf_speculative_advanced,
    gen_movxf_speculative_advanced,
    gen_movti_speculative_advanced,
    gen_zero_extendqidi2_speculative_advanced,
    gen_zero_extendhidi2_speculative_advanced,
    gen_zero_extendsidi2_speculative_advanced
  };

  load_no = extend_p ? mode_no + SPEC_GEN_EXTEND_OFFSET : mode_no;

  if (ts & BEGIN_DATA)
    {
      /* We don't need recovery because even if this is ld.sa
	 ALAT entry will be allocated only if NAT bit is set to zero. 
	 So it is enough to use ld.c here.  */	  

      if (ts & BEGIN_CONTROL)
	{	      
	  load_no += SPEC_GEN_SA;

	  if (!mflag_sched_ldc)
	    shift = SPEC_GEN_CHKA_FOR_SA_OFFSET;
	}
      else
	{
	  load_no += SPEC_GEN_A;

	  if (!mflag_sched_ldc)		
	    shift = SPEC_GEN_CHKA_FOR_A_OFFSET;
	}
    }
  else if (ts & BEGIN_CONTROL)
    {
      /* ld.sa can be used instead of ld.s to avoid basic block splitting.  */
      if (!mflag_control_ldc)
	load_no += SPEC_GEN_S;
      else
	{
	  gcc_assert (mflag_sched_ldc);
	  load_no += SPEC_GEN_SA_FOR_S;
	}
    }
  else
    gcc_unreachable ();

  /* Set the desired check index.  We add '1', because zero element in this
     array means, that instruction with such uid is non-speculative.  */
  spec_check_no[INSN_UID (insn)] = load_no + shift + 1;

  if (!gen_p)
    return 0;

  new_pat = gen_load[load_no] (copy_rtx (recog_data.operand[0]),
			       copy_rtx (recog_data.operand[1]));

  pat = PATTERN (insn);
  if (GET_CODE (pat) == COND_EXEC)
    new_pat = gen_rtx_COND_EXEC (VOIDmode, copy_rtx 
				 (COND_EXEC_TEST (pat)), new_pat);

  return new_pat;
}

/* Offset to branchy checks.  */
enum { SPEC_GEN_CHECK_MUTATION_OFFSET = 5 * SPEC_N };

/* Return nonzero, if INSN needs branchy recovery check.  */
static bool
ia64_needs_block_p (rtx insn)
{
  int check_no;

  check_no = spec_check_no[INSN_UID(insn)] - 1;
  gcc_assert (0 <= check_no && check_no < SPEC_GEN_CHECK_MUTATION_OFFSET);

  return ((SPEC_GEN_S <= check_no && check_no < SPEC_GEN_S + SPEC_N)
	  || (4 * SPEC_N <= check_no && check_no < 4 * SPEC_N + SPEC_N));
}

/* Generate (or regenerate, if (MUTATE_P)) recovery check for INSN.
   If (LABEL != 0 || MUTATE_P), generate branchy recovery check.
   Otherwise, generate a simple check.  */
static rtx
ia64_gen_check (rtx insn, rtx label, bool mutate_p)
{
  rtx op1, pat, check_pat;

  static rtx (* const gen_check[]) (rtx, rtx) = {
    gen_movbi_clr,
    gen_movqi_clr,
    gen_movhi_clr,
    gen_movsi_clr,
    gen_movdi_clr,
    gen_movsf_clr,
    gen_movdf_clr,
    gen_movxf_clr,
    gen_movti_clr,
    gen_zero_extendqidi2_clr,
    gen_zero_extendhidi2_clr,
    gen_zero_extendsidi2_clr,

    gen_speculation_check_bi,
    gen_speculation_check_qi,
    gen_speculation_check_hi,
    gen_speculation_check_si,
    gen_speculation_check_di,
    gen_speculation_check_sf,
    gen_speculation_check_df,
    gen_speculation_check_xf,
    gen_speculation_check_ti,
    gen_speculation_check_di,
    gen_speculation_check_di,
    gen_speculation_check_di,

    gen_movbi_clr,
    gen_movqi_clr,
    gen_movhi_clr,
    gen_movsi_clr,
    gen_movdi_clr,
    gen_movsf_clr,
    gen_movdf_clr,
    gen_movxf_clr,
    gen_movti_clr,
    gen_zero_extendqidi2_clr,
    gen_zero_extendhidi2_clr,
    gen_zero_extendsidi2_clr,

    gen_movbi_clr,
    gen_movqi_clr,
    gen_movhi_clr,
    gen_movsi_clr,
    gen_movdi_clr,
    gen_movsf_clr,
    gen_movdf_clr,
    gen_movxf_clr,
    gen_movti_clr,
    gen_zero_extendqidi2_clr,
    gen_zero_extendhidi2_clr,
    gen_zero_extendsidi2_clr,

    gen_advanced_load_check_clr_bi,
    gen_advanced_load_check_clr_qi,
    gen_advanced_load_check_clr_hi,
    gen_advanced_load_check_clr_si,
    gen_advanced_load_check_clr_di,
    gen_advanced_load_check_clr_sf,
    gen_advanced_load_check_clr_df,
    gen_advanced_load_check_clr_xf,
    gen_advanced_load_check_clr_ti,
    gen_advanced_load_check_clr_di,
    gen_advanced_load_check_clr_di,
    gen_advanced_load_check_clr_di,

    /* Following checks are generated during mutation.  */
    gen_advanced_load_check_clr_bi,
    gen_advanced_load_check_clr_qi,
    gen_advanced_load_check_clr_hi,
    gen_advanced_load_check_clr_si,
    gen_advanced_load_check_clr_di,
    gen_advanced_load_check_clr_sf,
    gen_advanced_load_check_clr_df,
    gen_advanced_load_check_clr_xf,
    gen_advanced_load_check_clr_ti,
    gen_advanced_load_check_clr_di,
    gen_advanced_load_check_clr_di,
    gen_advanced_load_check_clr_di,

    0,0,0,0,0,0,0,0,0,0,0,0,

    gen_advanced_load_check_clr_bi,
    gen_advanced_load_check_clr_qi,
    gen_advanced_load_check_clr_hi,
    gen_advanced_load_check_clr_si,
    gen_advanced_load_check_clr_di,
    gen_advanced_load_check_clr_sf,
    gen_advanced_load_check_clr_df,
    gen_advanced_load_check_clr_xf,
    gen_advanced_load_check_clr_ti,
    gen_advanced_load_check_clr_di,
    gen_advanced_load_check_clr_di,
    gen_advanced_load_check_clr_di,

    gen_speculation_check_bi,
    gen_speculation_check_qi,
    gen_speculation_check_hi,
    gen_speculation_check_si,
    gen_speculation_check_di,
    gen_speculation_check_sf,
    gen_speculation_check_df,
    gen_speculation_check_xf,
    gen_speculation_check_ti,
    gen_speculation_check_di,
    gen_speculation_check_di,
    gen_speculation_check_di
  };

  extract_insn_cached (insn);

  if (label)
    {
      gcc_assert (mutate_p || ia64_needs_block_p (insn));
      op1 = label;
    }
  else
    {
      gcc_assert (!mutate_p && !ia64_needs_block_p (insn));
      op1 = copy_rtx (recog_data.operand[1]);
    }
      
  if (mutate_p)
    /* INSN is ld.c.
       Find the speculation check number by searching for original
       speculative load in the RESOLVED_DEPS list of INSN.
       As long as patterns are unique for each instruction, this can be
       accomplished by matching ORIG_PAT fields.  */
    {
      rtx link;
      int check_no = 0;
      rtx orig_pat = ORIG_PAT (insn);

      for (link = RESOLVED_DEPS (insn); link; link = XEXP (link, 1))
	{
	  rtx x = XEXP (link, 0);

	  if (ORIG_PAT (x) == orig_pat)
	    check_no = spec_check_no[INSN_UID (x)];
	}
      gcc_assert (check_no);

      spec_check_no[INSN_UID (insn)] = (check_no
					+ SPEC_GEN_CHECK_MUTATION_OFFSET);
    }

  check_pat = (gen_check[spec_check_no[INSN_UID (insn)] - 1]
	       (copy_rtx (recog_data.operand[0]), op1));
    
  pat = PATTERN (insn);
  if (GET_CODE (pat) == COND_EXEC)
    check_pat = gen_rtx_COND_EXEC (VOIDmode, copy_rtx (COND_EXEC_TEST (pat)),
				   check_pat);

  return check_pat;
}

/* Return nonzero, if X is branchy recovery check.  */
static int
ia64_spec_check_p (rtx x)
{
  x = PATTERN (x);
  if (GET_CODE (x) == COND_EXEC)
    x = COND_EXEC_CODE (x);
  if (GET_CODE (x) == SET)
    return ia64_spec_check_src_p (SET_SRC (x));
  return 0;
}

/* Return nonzero, if SRC belongs to recovery check.  */
static int
ia64_spec_check_src_p (rtx src)
{
  if (GET_CODE (src) == IF_THEN_ELSE)
    {
      rtx t;

      t = XEXP (src, 0);
      if (GET_CODE (t) == NE)
	{
	  t = XEXP (t, 0);	    

	  if (GET_CODE (t) == UNSPEC)
	    {
	      int code;
	      
	      code = XINT (t, 1);
	     
	      if (code == UNSPEC_CHKACLR
		  || code == UNSPEC_CHKS
		  || code == UNSPEC_LDCCLR)
		{
		  gcc_assert (code != 0);
		  return code;
		}
	    }
	}
    }
  return 0;
}


/* The following page contains abstract data `bundle states' which are
   used for bundling insns (inserting nops and template generation).  */

/* The following describes state of insn bundling.  */

struct bundle_state
{
  /* Unique bundle state number to identify them in the debugging
     output  */
  int unique_num;
  rtx insn;     /* corresponding insn, NULL for the 1st and the last state  */
  /* number nops before and after the insn  */
  short before_nops_num, after_nops_num;
  int insn_num; /* insn number (0 - for initial state, 1 - for the 1st
                   insn */
  int cost;     /* cost of the state in cycles */
  int accumulated_insns_num; /* number of all previous insns including
				nops.  L is considered as 2 insns */
  int branch_deviation; /* deviation of previous branches from 3rd slots  */
  struct bundle_state *next;  /* next state with the same insn_num  */
  struct bundle_state *originator; /* originator (previous insn state)  */
  /* All bundle states are in the following chain.  */
  struct bundle_state *allocated_states_chain;
  /* The DFA State after issuing the insn and the nops.  */
  state_t dfa_state;
};

/* The following is map insn number to the corresponding bundle state.  */

static struct bundle_state **index_to_bundle_states;

/* The unique number of next bundle state.  */

static int bundle_states_num;

/* All allocated bundle states are in the following chain.  */

static struct bundle_state *allocated_bundle_states_chain;

/* All allocated but not used bundle states are in the following
   chain.  */

static struct bundle_state *free_bundle_state_chain;


/* The following function returns a free bundle state.  */

static struct bundle_state *
get_free_bundle_state (void)
{
  struct bundle_state *result;

  if (free_bundle_state_chain != NULL)
    {
      result = free_bundle_state_chain;
      free_bundle_state_chain = result->next;
    }
  else
    {
      result = xmalloc (sizeof (struct bundle_state));
      result->dfa_state = xmalloc (dfa_state_size);
      result->allocated_states_chain = allocated_bundle_states_chain;
      allocated_bundle_states_chain = result;
    }
  result->unique_num = bundle_states_num++;
  return result;

}

/* The following function frees given bundle state.  */

static void
free_bundle_state (struct bundle_state *state)
{
  state->next = free_bundle_state_chain;
  free_bundle_state_chain = state;
}

/* Start work with abstract data `bundle states'.  */

static void
initiate_bundle_states (void)
{
  bundle_states_num = 0;
  free_bundle_state_chain = NULL;
  allocated_bundle_states_chain = NULL;
}

/* Finish work with abstract data `bundle states'.  */

static void
finish_bundle_states (void)
{
  struct bundle_state *curr_state, *next_state;

  for (curr_state = allocated_bundle_states_chain;
       curr_state != NULL;
       curr_state = next_state)
    {
      next_state = curr_state->allocated_states_chain;
      free (curr_state->dfa_state);
      free (curr_state);
    }
}

/* Hash table of the bundle states.  The key is dfa_state and insn_num
   of the bundle states.  */

static htab_t bundle_state_table;

/* The function returns hash of BUNDLE_STATE.  */

static unsigned
bundle_state_hash (const void *bundle_state)
{
  const struct bundle_state *state = (struct bundle_state *) bundle_state;
  unsigned result, i;

  for (result = i = 0; i < dfa_state_size; i++)
    result += (((unsigned char *) state->dfa_state) [i]
	       << ((i % CHAR_BIT) * 3 + CHAR_BIT));
  return result + state->insn_num;
}

/* The function returns nonzero if the bundle state keys are equal.  */

static int
bundle_state_eq_p (const void *bundle_state_1, const void *bundle_state_2)
{
  const struct bundle_state * state1 = (struct bundle_state *) bundle_state_1;
  const struct bundle_state * state2 = (struct bundle_state *) bundle_state_2;

  return (state1->insn_num == state2->insn_num
	  && memcmp (state1->dfa_state, state2->dfa_state,
		     dfa_state_size) == 0);
}

/* The function inserts the BUNDLE_STATE into the hash table.  The
   function returns nonzero if the bundle has been inserted into the
   table.  The table contains the best bundle state with given key.  */

static int
insert_bundle_state (struct bundle_state *bundle_state)
{
  void **entry_ptr;

  entry_ptr = htab_find_slot (bundle_state_table, bundle_state, 1);
  if (*entry_ptr == NULL)
    {
      bundle_state->next = index_to_bundle_states [bundle_state->insn_num];
      index_to_bundle_states [bundle_state->insn_num] = bundle_state;
      *entry_ptr = (void *) bundle_state;
      return TRUE;
    }
  else if (bundle_state->cost < ((struct bundle_state *) *entry_ptr)->cost
	   || (bundle_state->cost == ((struct bundle_state *) *entry_ptr)->cost
	       && (((struct bundle_state *)*entry_ptr)->accumulated_insns_num
		   > bundle_state->accumulated_insns_num
		   || (((struct bundle_state *)
			*entry_ptr)->accumulated_insns_num
		       == bundle_state->accumulated_insns_num
		       && ((struct bundle_state *)
			   *entry_ptr)->branch_deviation
		       > bundle_state->branch_deviation))))

    {
      struct bundle_state temp;

      temp = *(struct bundle_state *) *entry_ptr;
      *(struct bundle_state *) *entry_ptr = *bundle_state;
      ((struct bundle_state *) *entry_ptr)->next = temp.next;
      *bundle_state = temp;
    }
  return FALSE;
}

/* Start work with the hash table.  */

static void
initiate_bundle_state_table (void)
{
  bundle_state_table = htab_create (50, bundle_state_hash, bundle_state_eq_p,
				    (htab_del) 0);
}

/* Finish work with the hash table.  */

static void
finish_bundle_state_table (void)
{
  htab_delete (bundle_state_table);
}



/* The following variable is a insn `nop' used to check bundle states
   with different number of inserted nops.  */

static rtx ia64_nop;

/* The following function tries to issue NOPS_NUM nops for the current
   state without advancing processor cycle.  If it failed, the
   function returns FALSE and frees the current state.  */

static int
try_issue_nops (struct bundle_state *curr_state, int nops_num)
{
  int i;

  for (i = 0; i < nops_num; i++)
    if (state_transition (curr_state->dfa_state, ia64_nop) >= 0)
      {
	free_bundle_state (curr_state);
	return FALSE;
      }
  return TRUE;
}

/* The following function tries to issue INSN for the current
   state without advancing processor cycle.  If it failed, the
   function returns FALSE and frees the current state.  */

static int
try_issue_insn (struct bundle_state *curr_state, rtx insn)
{
  if (insn && state_transition (curr_state->dfa_state, insn) >= 0)
    {
      free_bundle_state (curr_state);
      return FALSE;
    }
  return TRUE;
}

/* The following function tries to issue BEFORE_NOPS_NUM nops and INSN
   starting with ORIGINATOR without advancing processor cycle.  If
   TRY_BUNDLE_END_P is TRUE, the function also/only (if
   ONLY_BUNDLE_END_P is TRUE) tries to issue nops to fill all bundle.
   If it was successful, the function creates new bundle state and
   insert into the hash table and into `index_to_bundle_states'.  */

static void
issue_nops_and_insn (struct bundle_state *originator, int before_nops_num,
		     rtx insn, int try_bundle_end_p, int only_bundle_end_p)
{
  struct bundle_state *curr_state;

  curr_state = get_free_bundle_state ();
  memcpy (curr_state->dfa_state, originator->dfa_state, dfa_state_size);
  curr_state->insn = insn;
  curr_state->insn_num = originator->insn_num + 1;
  curr_state->cost = originator->cost;
  curr_state->originator = originator;
  curr_state->before_nops_num = before_nops_num;
  curr_state->after_nops_num = 0;
  curr_state->accumulated_insns_num
    = originator->accumulated_insns_num + before_nops_num;
  curr_state->branch_deviation = originator->branch_deviation;
  gcc_assert (insn);
  if (INSN_CODE (insn) == CODE_FOR_insn_group_barrier)
    {
      gcc_assert (GET_MODE (insn) != TImode);
      if (!try_issue_nops (curr_state, before_nops_num))
	return;
      if (!try_issue_insn (curr_state, insn))
	return;
      memcpy (temp_dfa_state, curr_state->dfa_state, dfa_state_size);
      if (state_transition (temp_dfa_state, dfa_pre_cycle_insn) >= 0
	  && curr_state->accumulated_insns_num % 3 != 0)
	{
	  free_bundle_state (curr_state);
	  return;
	}
    }
  else if (GET_MODE (insn) != TImode)
    {
      if (!try_issue_nops (curr_state, before_nops_num))
	return;
      if (!try_issue_insn (curr_state, insn))
	return;
      curr_state->accumulated_insns_num++;
      gcc_assert (GET_CODE (PATTERN (insn)) != ASM_INPUT
		  && asm_noperands (PATTERN (insn)) < 0);

      if (ia64_safe_type (insn) == TYPE_L)
	curr_state->accumulated_insns_num++;
    }
  else
    {
      /* If this is an insn that must be first in a group, then don't allow
	 nops to be emitted before it.  Currently, alloc is the only such
	 supported instruction.  */
      /* ??? The bundling automatons should handle this for us, but they do
	 not yet have support for the first_insn attribute.  */
      if (before_nops_num > 0 && get_attr_first_insn (insn) == FIRST_INSN_YES)
	{
	  free_bundle_state (curr_state);
	  return;
	}

      state_transition (curr_state->dfa_state, dfa_pre_cycle_insn);
      state_transition (curr_state->dfa_state, NULL);
      curr_state->cost++;
      if (!try_issue_nops (curr_state, before_nops_num))
	return;
      if (!try_issue_insn (curr_state, insn))
	return;
      curr_state->accumulated_insns_num++;
      if (GET_CODE (PATTERN (insn)) == ASM_INPUT
	  || asm_noperands (PATTERN (insn)) >= 0)
	{
	  /* Finish bundle containing asm insn.  */
	  curr_state->after_nops_num
	    = 3 - curr_state->accumulated_insns_num % 3;
	  curr_state->accumulated_insns_num
	    += 3 - curr_state->accumulated_insns_num % 3;
	}
      else if (ia64_safe_type (insn) == TYPE_L)
	curr_state->accumulated_insns_num++;
    }
  if (ia64_safe_type (insn) == TYPE_B)
    curr_state->branch_deviation
      += 2 - (curr_state->accumulated_insns_num - 1) % 3;
  if (try_bundle_end_p && curr_state->accumulated_insns_num % 3 != 0)
    {
      if (!only_bundle_end_p && insert_bundle_state (curr_state))
	{
	  state_t dfa_state;
	  struct bundle_state *curr_state1;
	  struct bundle_state *allocated_states_chain;

	  curr_state1 = get_free_bundle_state ();
	  dfa_state = curr_state1->dfa_state;
	  allocated_states_chain = curr_state1->allocated_states_chain;
	  *curr_state1 = *curr_state;
	  curr_state1->dfa_state = dfa_state;
	  curr_state1->allocated_states_chain = allocated_states_chain;
	  memcpy (curr_state1->dfa_state, curr_state->dfa_state,
		  dfa_state_size);
	  curr_state = curr_state1;
	}
      if (!try_issue_nops (curr_state,
			   3 - curr_state->accumulated_insns_num % 3))
	return;
      curr_state->after_nops_num
	= 3 - curr_state->accumulated_insns_num % 3;
      curr_state->accumulated_insns_num
	+= 3 - curr_state->accumulated_insns_num % 3;
    }
  if (!insert_bundle_state (curr_state))
    free_bundle_state (curr_state);
  return;
}

/* The following function returns position in the two window bundle
   for given STATE.  */

static int
get_max_pos (state_t state)
{
  if (cpu_unit_reservation_p (state, pos_6))
    return 6;
  else if (cpu_unit_reservation_p (state, pos_5))
    return 5;
  else if (cpu_unit_reservation_p (state, pos_4))
    return 4;
  else if (cpu_unit_reservation_p (state, pos_3))
    return 3;
  else if (cpu_unit_reservation_p (state, pos_2))
    return 2;
  else if (cpu_unit_reservation_p (state, pos_1))
    return 1;
  else
    return 0;
}

/* The function returns code of a possible template for given position
   and state.  The function should be called only with 2 values of
   position equal to 3 or 6.  We avoid generating F NOPs by putting
   templates containing F insns at the end of the template search
   because undocumented anomaly in McKinley derived cores which can
   cause stalls if an F-unit insn (including a NOP) is issued within a
   six-cycle window after reading certain application registers (such
   as ar.bsp).  Furthermore, power-considerations also argue against
   the use of F-unit instructions unless they're really needed.  */

static int
get_template (state_t state, int pos)
{
  switch (pos)
    {
    case 3:
      if (cpu_unit_reservation_p (state, _0mmi_))
	return 1;
      else if (cpu_unit_reservation_p (state, _0mii_))
	return 0;
      else if (cpu_unit_reservation_p (state, _0mmb_))
	return 7;
      else if (cpu_unit_reservation_p (state, _0mib_))
	return 6;
      else if (cpu_unit_reservation_p (state, _0mbb_))
	return 5;
      else if (cpu_unit_reservation_p (state, _0bbb_))
	return 4;
      else if (cpu_unit_reservation_p (state, _0mmf_))
	return 3;
      else if (cpu_unit_reservation_p (state, _0mfi_))
	return 2;
      else if (cpu_unit_reservation_p (state, _0mfb_))
	return 8;
      else if (cpu_unit_reservation_p (state, _0mlx_))
	return 9;
      else
	gcc_unreachable ();
    case 6:
      if (cpu_unit_reservation_p (state, _1mmi_))
	return 1;
      else if (cpu_unit_reservation_p (state, _1mii_))
	return 0;
      else if (cpu_unit_reservation_p (state, _1mmb_))
	return 7;
      else if (cpu_unit_reservation_p (state, _1mib_))
	return 6;
      else if (cpu_unit_reservation_p (state, _1mbb_))
	return 5;
      else if (cpu_unit_reservation_p (state, _1bbb_))
	return 4;
      else if (_1mmf_ >= 0 && cpu_unit_reservation_p (state, _1mmf_))
	return 3;
      else if (cpu_unit_reservation_p (state, _1mfi_))
	return 2;
      else if (cpu_unit_reservation_p (state, _1mfb_))
	return 8;
      else if (cpu_unit_reservation_p (state, _1mlx_))
	return 9;
      else
	gcc_unreachable ();
    default:
      gcc_unreachable ();
    }
}

/* The following function returns an insn important for insn bundling
   followed by INSN and before TAIL.  */

static rtx
get_next_important_insn (rtx insn, rtx tail)
{
  for (; insn && insn != tail; insn = NEXT_INSN (insn))
    if (INSN_P (insn)
	&& ia64_safe_itanium_class (insn) != ITANIUM_CLASS_IGNORE
	&& GET_CODE (PATTERN (insn)) != USE
	&& GET_CODE (PATTERN (insn)) != CLOBBER)
      return insn;
  return NULL_RTX;
}

/* Add a bundle selector TEMPLATE0 before INSN.  */

static void
ia64_add_bundle_selector_before (int template0, rtx insn)
{
  rtx b = gen_bundle_selector (GEN_INT (template0));

  ia64_emit_insn_before (b, insn);
#if NR_BUNDLES == 10
  if ((template0 == 4 || template0 == 5)
      && (flag_unwind_tables || (flag_exceptions && !USING_SJLJ_EXCEPTIONS)))
    {
      int i;
      rtx note = NULL_RTX;

      /* In .mbb and .bbb bundles, check if CALL_INSN isn't in the
	 first or second slot.  If it is and has REG_EH_NOTE set, copy it
	 to following nops, as br.call sets rp to the address of following
	 bundle and therefore an EH region end must be on a bundle
	 boundary.  */
      insn = PREV_INSN (insn);
      for (i = 0; i < 3; i++)
	{
	  do
	    insn = next_active_insn (insn);
	  while (GET_CODE (insn) == INSN
		 && get_attr_empty (insn) == EMPTY_YES);
	  if (GET_CODE (insn) == CALL_INSN)
	    note = find_reg_note (insn, REG_EH_REGION, NULL_RTX);
	  else if (note)
	    {
	      int code;

	      gcc_assert ((code = recog_memoized (insn)) == CODE_FOR_nop
			  || code == CODE_FOR_nop_b);
	      if (find_reg_note (insn, REG_EH_REGION, NULL_RTX))
		note = NULL_RTX;
	      else
		REG_NOTES (insn)
		  = gen_rtx_EXPR_LIST (REG_EH_REGION, XEXP (note, 0),
				       REG_NOTES (insn));
	    }
	}
    }
#endif
}

/* The following function does insn bundling.  Bundling means
   inserting templates and nop insns to fit insn groups into permitted
   templates.  Instruction scheduling uses NDFA (non-deterministic
   finite automata) encoding informations about the templates and the
   inserted nops.  Nondeterminism of the automata permits follows
   all possible insn sequences very fast.

   Unfortunately it is not possible to get information about inserting
   nop insns and used templates from the automata states.  The
   automata only says that we can issue an insn possibly inserting
   some nops before it and using some template.  Therefore insn
   bundling in this function is implemented by using DFA
   (deterministic finite automata).  We follow all possible insn
   sequences by inserting 0-2 nops (that is what the NDFA describe for
   insn scheduling) before/after each insn being bundled.  We know the
   start of simulated processor cycle from insn scheduling (insn
   starting a new cycle has TImode).

   Simple implementation of insn bundling would create enormous
   number of possible insn sequences satisfying information about new
   cycle ticks taken from the insn scheduling.  To make the algorithm
   practical we use dynamic programming.  Each decision (about
   inserting nops and implicitly about previous decisions) is described
   by structure bundle_state (see above).  If we generate the same
   bundle state (key is automaton state after issuing the insns and
   nops for it), we reuse already generated one.  As consequence we
   reject some decisions which cannot improve the solution and
   reduce memory for the algorithm.

   When we reach the end of EBB (extended basic block), we choose the
   best sequence and then, moving back in EBB, insert templates for
   the best alternative.  The templates are taken from querying
   automaton state for each insn in chosen bundle states.

   So the algorithm makes two (forward and backward) passes through
   EBB.  There is an additional forward pass through EBB for Itanium1
   processor.  This pass inserts more nops to make dependency between
   a producer insn and MMMUL/MMSHF at least 4 cycles long.  */

static void
bundling (FILE *dump, int verbose, rtx prev_head_insn, rtx tail)
{
  struct bundle_state *curr_state, *next_state, *best_state;
  rtx insn, next_insn;
  int insn_num;
  int i, bundle_end_p, only_bundle_end_p, asm_p;
  int pos = 0, max_pos, template0, template1;
  rtx b;
  rtx nop;
  enum attr_type type;

  insn_num = 0;
  /* Count insns in the EBB.  */
  for (insn = NEXT_INSN (prev_head_insn);
       insn && insn != tail;
       insn = NEXT_INSN (insn))
    if (INSN_P (insn))
      insn_num++;
  if (insn_num == 0)
    return;
  bundling_p = 1;
  dfa_clean_insn_cache ();
  initiate_bundle_state_table ();
  index_to_bundle_states = xmalloc ((insn_num + 2)
				    * sizeof (struct bundle_state *));
  /* First (forward) pass -- generation of bundle states.  */
  curr_state = get_free_bundle_state ();
  curr_state->insn = NULL;
  curr_state->before_nops_num = 0;
  curr_state->after_nops_num = 0;
  curr_state->insn_num = 0;
  curr_state->cost = 0;
  curr_state->accumulated_insns_num = 0;
  curr_state->branch_deviation = 0;
  curr_state->next = NULL;
  curr_state->originator = NULL;
  state_reset (curr_state->dfa_state);
  index_to_bundle_states [0] = curr_state;
  insn_num = 0;
  /* Shift cycle mark if it is put on insn which could be ignored.  */
  for (insn = NEXT_INSN (prev_head_insn);
       insn != tail;
       insn = NEXT_INSN (insn))
    if (INSN_P (insn)
	&& (ia64_safe_itanium_class (insn) == ITANIUM_CLASS_IGNORE
	    || GET_CODE (PATTERN (insn)) == USE
	    || GET_CODE (PATTERN (insn)) == CLOBBER)
	&& GET_MODE (insn) == TImode)
      {
	PUT_MODE (insn, VOIDmode);
	for (next_insn = NEXT_INSN (insn);
	     next_insn != tail;
	     next_insn = NEXT_INSN (next_insn))
	  if (INSN_P (next_insn)
	      && ia64_safe_itanium_class (next_insn) != ITANIUM_CLASS_IGNORE
	      && GET_CODE (PATTERN (next_insn)) != USE
	      && GET_CODE (PATTERN (next_insn)) != CLOBBER)
	    {
	      PUT_MODE (next_insn, TImode);
	      break;
	    }
      }
  /* Forward pass: generation of bundle states.  */
  for (insn = get_next_important_insn (NEXT_INSN (prev_head_insn), tail);
       insn != NULL_RTX;
       insn = next_insn)
    {
      gcc_assert (INSN_P (insn)
		  && ia64_safe_itanium_class (insn) != ITANIUM_CLASS_IGNORE
		  && GET_CODE (PATTERN (insn)) != USE
		  && GET_CODE (PATTERN (insn)) != CLOBBER);
      type = ia64_safe_type (insn);
      next_insn = get_next_important_insn (NEXT_INSN (insn), tail);
      insn_num++;
      index_to_bundle_states [insn_num] = NULL;
      for (curr_state = index_to_bundle_states [insn_num - 1];
	   curr_state != NULL;
	   curr_state = next_state)
	{
	  pos = curr_state->accumulated_insns_num % 3;
	  next_state = curr_state->next;
	  /* We must fill up the current bundle in order to start a
	     subsequent asm insn in a new bundle.  Asm insn is always
	     placed in a separate bundle.  */
	  only_bundle_end_p
	    = (next_insn != NULL_RTX
	       && INSN_CODE (insn) == CODE_FOR_insn_group_barrier
	       && ia64_safe_type (next_insn) == TYPE_UNKNOWN);
	  /* We may fill up the current bundle if it is the cycle end
	     without a group barrier.  */
	  bundle_end_p
	    = (only_bundle_end_p || next_insn == NULL_RTX
	       || (GET_MODE (next_insn) == TImode
		   && INSN_CODE (insn) != CODE_FOR_insn_group_barrier));
	  if (type == TYPE_F || type == TYPE_B || type == TYPE_L
	      || type == TYPE_S
	      /* We need to insert 2 nops for cases like M_MII.  To
		 guarantee issuing all insns on the same cycle for
		 Itanium 1, we need to issue 2 nops after the first M
		 insn (MnnMII where n is a nop insn).  */
	      || ((type == TYPE_M || type == TYPE_A)
		  && ia64_tune == PROCESSOR_ITANIUM
		  && !bundle_end_p && pos == 1))
	    issue_nops_and_insn (curr_state, 2, insn, bundle_end_p,
				 only_bundle_end_p);
	  issue_nops_and_insn (curr_state, 1, insn, bundle_end_p,
			       only_bundle_end_p);
	  issue_nops_and_insn (curr_state, 0, insn, bundle_end_p,
			       only_bundle_end_p);
	}
      gcc_assert (index_to_bundle_states [insn_num]);
      for (curr_state = index_to_bundle_states [insn_num];
	   curr_state != NULL;
	   curr_state = curr_state->next)
	if (verbose >= 2 && dump)
	  {
	    /* This structure is taken from generated code of the
	       pipeline hazard recognizer (see file insn-attrtab.c).
	       Please don't forget to change the structure if a new
	       automaton is added to .md file.  */
	    struct DFA_chip
	    {
	      unsigned short one_automaton_state;
	      unsigned short oneb_automaton_state;
	      unsigned short two_automaton_state;
	      unsigned short twob_automaton_state;
	    };

	    fprintf
	      (dump,
	       "//    Bundle state %d (orig %d, cost %d, nops %d/%d, insns %d, branch %d, state %d) for %d\n",
	       curr_state->unique_num,
	       (curr_state->originator == NULL
		? -1 : curr_state->originator->unique_num),
	       curr_state->cost,
	       curr_state->before_nops_num, curr_state->after_nops_num,
	       curr_state->accumulated_insns_num, curr_state->branch_deviation,
	       (ia64_tune == PROCESSOR_ITANIUM
		? ((struct DFA_chip *) curr_state->dfa_state)->oneb_automaton_state
		: ((struct DFA_chip *) curr_state->dfa_state)->twob_automaton_state),
	       INSN_UID (insn));
	  }
    }
  
  /* We should find a solution because the 2nd insn scheduling has
     found one.  */
  gcc_assert (index_to_bundle_states [insn_num]);
  /* Find a state corresponding to the best insn sequence.  */
  best_state = NULL;
  for (curr_state = index_to_bundle_states [insn_num];
       curr_state != NULL;
       curr_state = curr_state->next)
    /* We are just looking at the states with fully filled up last
       bundle.  The first we prefer insn sequences with minimal cost
       then with minimal inserted nops and finally with branch insns
       placed in the 3rd slots.  */
    if (curr_state->accumulated_insns_num % 3 == 0
	&& (best_state == NULL || best_state->cost > curr_state->cost
	    || (best_state->cost == curr_state->cost
		&& (curr_state->accumulated_insns_num
		    < best_state->accumulated_insns_num
		    || (curr_state->accumulated_insns_num
			== best_state->accumulated_insns_num
			&& curr_state->branch_deviation
			< best_state->branch_deviation)))))
      best_state = curr_state;
  /* Second (backward) pass: adding nops and templates.  */
  insn_num = best_state->before_nops_num;
  template0 = template1 = -1;
  for (curr_state = best_state;
       curr_state->originator != NULL;
       curr_state = curr_state->originator)
    {
      insn = curr_state->insn;
      asm_p = (GET_CODE (PATTERN (insn)) == ASM_INPUT
	       || asm_noperands (PATTERN (insn)) >= 0);
      insn_num++;
      if (verbose >= 2 && dump)
	{
	  struct DFA_chip
	  {
	    unsigned short one_automaton_state;
	    unsigned short oneb_automaton_state;
	    unsigned short two_automaton_state;
	    unsigned short twob_automaton_state;
	  };

	  fprintf
	    (dump,
	     "//    Best %d (orig %d, cost %d, nops %d/%d, insns %d, branch %d, state %d) for %d\n",
	     curr_state->unique_num,
	     (curr_state->originator == NULL
	      ? -1 : curr_state->originator->unique_num),
	     curr_state->cost,
	     curr_state->before_nops_num, curr_state->after_nops_num,
	     curr_state->accumulated_insns_num, curr_state->branch_deviation,
	     (ia64_tune == PROCESSOR_ITANIUM
	      ? ((struct DFA_chip *) curr_state->dfa_state)->oneb_automaton_state
	      : ((struct DFA_chip *) curr_state->dfa_state)->twob_automaton_state),
	     INSN_UID (insn));
	}
      /* Find the position in the current bundle window.  The window can
	 contain at most two bundles.  Two bundle window means that
	 the processor will make two bundle rotation.  */
      max_pos = get_max_pos (curr_state->dfa_state);
      if (max_pos == 6
	  /* The following (negative template number) means that the
	     processor did one bundle rotation.  */
	  || (max_pos == 3 && template0 < 0))
	{
	  /* We are at the end of the window -- find template(s) for
	     its bundle(s).  */
	  pos = max_pos;
	  if (max_pos == 3)
	    template0 = get_template (curr_state->dfa_state, 3);
	  else
	    {
	      template1 = get_template (curr_state->dfa_state, 3);
	      template0 = get_template (curr_state->dfa_state, 6);
	    }
	}
      if (max_pos > 3 && template1 < 0)
	/* It may happen when we have the stop inside a bundle.  */
	{
	  gcc_assert (pos <= 3);
	  template1 = get_template (curr_state->dfa_state, 3);
	  pos += 3;
	}
      if (!asm_p)
	/* Emit nops after the current insn.  */
	for (i = 0; i < curr_state->after_nops_num; i++)
	  {
	    nop = gen_nop ();
	    emit_insn_after (nop, insn);
	    pos--;
	    gcc_assert (pos >= 0);
	    if (pos % 3 == 0)
	      {
		/* We are at the start of a bundle: emit the template
		   (it should be defined).  */
		gcc_assert (template0 >= 0);
		ia64_add_bundle_selector_before (template0, nop);
		/* If we have two bundle window, we make one bundle
		   rotation.  Otherwise template0 will be undefined
		   (negative value).  */
		template0 = template1;
		template1 = -1;
	      }
	  }
      /* Move the position backward in the window.  Group barrier has
	 no slot.  Asm insn takes all bundle.  */
      if (INSN_CODE (insn) != CODE_FOR_insn_group_barrier
	  && GET_CODE (PATTERN (insn)) != ASM_INPUT
	  && asm_noperands (PATTERN (insn)) < 0)
	pos--;
      /* Long insn takes 2 slots.  */
      if (ia64_safe_type (insn) == TYPE_L)
	pos--;
      gcc_assert (pos >= 0);
      if (pos % 3 == 0
	  && INSN_CODE (insn) != CODE_FOR_insn_group_barrier
	  && GET_CODE (PATTERN (insn)) != ASM_INPUT
	  && asm_noperands (PATTERN (insn)) < 0)
	{
	  /* The current insn is at the bundle start: emit the
	     template.  */
	  gcc_assert (template0 >= 0);
	  ia64_add_bundle_selector_before (template0, insn);
	  b = PREV_INSN (insn);
	  insn = b;
	  /* See comment above in analogous place for emitting nops
	     after the insn.  */
	  template0 = template1;
	  template1 = -1;
	}
      /* Emit nops after the current insn.  */
      for (i = 0; i < curr_state->before_nops_num; i++)
	{
	  nop = gen_nop ();
	  ia64_emit_insn_before (nop, insn);
	  nop = PREV_INSN (insn);
	  insn = nop;
	  pos--;
	  gcc_assert (pos >= 0);
	  if (pos % 3 == 0)
	    {
	      /* See comment above in analogous place for emitting nops
		 after the insn.  */
	      gcc_assert (template0 >= 0);
	      ia64_add_bundle_selector_before (template0, insn);
	      b = PREV_INSN (insn);
	      insn = b;
	      template0 = template1;
	      template1 = -1;
	    }
	}
    }
  if (ia64_tune == PROCESSOR_ITANIUM)
    /* Insert additional cycles for MM-insns (MMMUL and MMSHF).
       Itanium1 has a strange design, if the distance between an insn
       and dependent MM-insn is less 4 then we have a 6 additional
       cycles stall.  So we make the distance equal to 4 cycles if it
       is less.  */
    for (insn = get_next_important_insn (NEXT_INSN (prev_head_insn), tail);
	 insn != NULL_RTX;
	 insn = next_insn)
      {
	gcc_assert (INSN_P (insn)
		    && ia64_safe_itanium_class (insn) != ITANIUM_CLASS_IGNORE
		    && GET_CODE (PATTERN (insn)) != USE
		    && GET_CODE (PATTERN (insn)) != CLOBBER);
	next_insn = get_next_important_insn (NEXT_INSN (insn), tail);
	if (INSN_UID (insn) < clocks_length && add_cycles [INSN_UID (insn)])
	  /* We found a MM-insn which needs additional cycles.  */
	  {
	    rtx last;
	    int i, j, n;
	    int pred_stop_p;

	    /* Now we are searching for a template of the bundle in
	       which the MM-insn is placed and the position of the
	       insn in the bundle (0, 1, 2).  Also we are searching
	       for that there is a stop before the insn.  */
	    last = prev_active_insn (insn);
	    pred_stop_p = recog_memoized (last) == CODE_FOR_insn_group_barrier;
	    if (pred_stop_p)
	      last = prev_active_insn (last);
	    n = 0;
	    for (;; last = prev_active_insn (last))
	      if (recog_memoized (last) == CODE_FOR_bundle_selector)
		{
		  template0 = XINT (XVECEXP (PATTERN (last), 0, 0), 0);
		  if (template0 == 9)
		    /* The insn is in MLX bundle.  Change the template
		       onto MFI because we will add nops before the
		       insn.  It simplifies subsequent code a lot.  */
		    PATTERN (last)
		      = gen_bundle_selector (const2_rtx); /* -> MFI */
		  break;
		}
	      else if (recog_memoized (last) != CODE_FOR_insn_group_barrier
		       && (ia64_safe_itanium_class (last)
			   != ITANIUM_CLASS_IGNORE))
		n++;
	    /* Some check of correctness: the stop is not at the
	       bundle start, there are no more 3 insns in the bundle,
	       and the MM-insn is not at the start of bundle with
	       template MLX.  */
	    gcc_assert ((!pred_stop_p || n)
			&& n <= 2
			&& (template0 != 9 || !n));
	    /* Put nops after the insn in the bundle.  */
	    for (j = 3 - n; j > 0; j --)
	      ia64_emit_insn_before (gen_nop (), insn);
	    /* It takes into account that we will add more N nops
	       before the insn lately -- please see code below.  */
	    add_cycles [INSN_UID (insn)]--;
	    if (!pred_stop_p || add_cycles [INSN_UID (insn)])
	      ia64_emit_insn_before (gen_insn_group_barrier (GEN_INT (3)),
				     insn);
	    if (pred_stop_p)
	      add_cycles [INSN_UID (insn)]--;
	    for (i = add_cycles [INSN_UID (insn)]; i > 0; i--)
	      {
		/* Insert "MII;" template.  */
		ia64_emit_insn_before (gen_bundle_selector (const0_rtx),
				       insn);
		ia64_emit_insn_before (gen_nop (), insn);
		ia64_emit_insn_before (gen_nop (), insn);
		if (i > 1)
		  {
		    /* To decrease code size, we use "MI;I;"
		       template.  */
		    ia64_emit_insn_before
		      (gen_insn_group_barrier (GEN_INT (3)), insn);
		    i--;
		  }
		ia64_emit_insn_before (gen_nop (), insn);
		ia64_emit_insn_before (gen_insn_group_barrier (GEN_INT (3)),
				       insn);
	      }
	    /* Put the MM-insn in the same slot of a bundle with the
	       same template as the original one.  */
	    ia64_add_bundle_selector_before (template0, insn);
	    /* To put the insn in the same slot, add necessary number
	       of nops.  */
	    for (j = n; j > 0; j --)
	      ia64_emit_insn_before (gen_nop (), insn);
	    /* Put the stop if the original bundle had it.  */
	    if (pred_stop_p)
	      ia64_emit_insn_before (gen_insn_group_barrier (GEN_INT (3)),
				     insn);
	  }
      }
  free (index_to_bundle_states);
  finish_bundle_state_table ();
  bundling_p = 0;
  dfa_clean_insn_cache ();
}

/* The following function is called at the end of scheduling BB or
   EBB.  After reload, it inserts stop bits and does insn bundling.  */

static void
ia64_sched_finish (FILE *dump, int sched_verbose)
{
  if (sched_verbose)
    fprintf (dump, "// Finishing schedule.\n");
  if (!reload_completed)
    return;
  if (reload_completed)
    {
      final_emit_insn_group_barriers (dump);
      bundling (dump, sched_verbose, current_sched_info->prev_head,
		current_sched_info->next_tail);
      if (sched_verbose && dump)
	fprintf (dump, "//    finishing %d-%d\n",
		 INSN_UID (NEXT_INSN (current_sched_info->prev_head)),
		 INSN_UID (PREV_INSN (current_sched_info->next_tail)));

      return;
    }
}

/* The following function inserts stop bits in scheduled BB or EBB.  */

static void
final_emit_insn_group_barriers (FILE *dump ATTRIBUTE_UNUSED)
{
  rtx insn;
  int need_barrier_p = 0;
  rtx prev_insn = NULL_RTX;

  init_insn_group_barriers ();

  for (insn = NEXT_INSN (current_sched_info->prev_head);
       insn != current_sched_info->next_tail;
       insn = NEXT_INSN (insn))
    {
      if (GET_CODE (insn) == BARRIER)
	{
	  rtx last = prev_active_insn (insn);

	  if (! last)
	    continue;
	  if (GET_CODE (last) == JUMP_INSN
	      && GET_CODE (PATTERN (last)) == ADDR_DIFF_VEC)
	    last = prev_active_insn (last);
	  if (recog_memoized (last) != CODE_FOR_insn_group_barrier)
	    emit_insn_after (gen_insn_group_barrier (GEN_INT (3)), last);

	  init_insn_group_barriers ();
	  need_barrier_p = 0;
	  prev_insn = NULL_RTX;
	}
      else if (INSN_P (insn))
	{
	  if (recog_memoized (insn) == CODE_FOR_insn_group_barrier)
	    {
	      init_insn_group_barriers ();
	      need_barrier_p = 0;
	      prev_insn = NULL_RTX;
	    }
	  else if (need_barrier_p || group_barrier_needed (insn))
	    {
	      if (TARGET_EARLY_STOP_BITS)
		{
		  rtx last;

		  for (last = insn;
		       last != current_sched_info->prev_head;
		       last = PREV_INSN (last))
		    if (INSN_P (last) && GET_MODE (last) == TImode
			&& stops_p [INSN_UID (last)])
		      break;
		  if (last == current_sched_info->prev_head)
		    last = insn;
		  last = prev_active_insn (last);
		  if (last
		      && recog_memoized (last) != CODE_FOR_insn_group_barrier)
		    emit_insn_after (gen_insn_group_barrier (GEN_INT (3)),
				     last);
		  init_insn_group_barriers ();
		  for (last = NEXT_INSN (last);
		       last != insn;
		       last = NEXT_INSN (last))
		    if (INSN_P (last))
		      group_barrier_needed (last);
		}
	      else
		{
		  emit_insn_before (gen_insn_group_barrier (GEN_INT (3)),
				    insn);
		  init_insn_group_barriers ();
		}
	      group_barrier_needed (insn);
	      prev_insn = NULL_RTX;
	    }
	  else if (recog_memoized (insn) >= 0)
	    prev_insn = insn;
	  need_barrier_p = (GET_CODE (insn) == CALL_INSN
			    || GET_CODE (PATTERN (insn)) == ASM_INPUT
			    || asm_noperands (PATTERN (insn)) >= 0);
	}
    }
}



/* If the following function returns TRUE, we will use the DFA
   insn scheduler.  */

static int
ia64_first_cycle_multipass_dfa_lookahead (void)
{
  return (reload_completed ? 6 : 4);
}

/* The following function initiates variable `dfa_pre_cycle_insn'.  */

static void
ia64_init_dfa_pre_cycle_insn (void)
{
  if (temp_dfa_state == NULL)
    {
      dfa_state_size = state_size ();
      temp_dfa_state = xmalloc (dfa_state_size);
      prev_cycle_state = xmalloc (dfa_state_size);
    }
  dfa_pre_cycle_insn = make_insn_raw (gen_pre_cycle ());
  PREV_INSN (dfa_pre_cycle_insn) = NEXT_INSN (dfa_pre_cycle_insn) = NULL_RTX;
  recog_memoized (dfa_pre_cycle_insn);
  dfa_stop_insn = make_insn_raw (gen_insn_group_barrier (GEN_INT (3)));
  PREV_INSN (dfa_stop_insn) = NEXT_INSN (dfa_stop_insn) = NULL_RTX;
  recog_memoized (dfa_stop_insn);
}

/* The following function returns the pseudo insn DFA_PRE_CYCLE_INSN
   used by the DFA insn scheduler.  */

static rtx
ia64_dfa_pre_cycle_insn (void)
{
  return dfa_pre_cycle_insn;
}

/* The following function returns TRUE if PRODUCER (of type ilog or
   ld) produces address for CONSUMER (of type st or stf). */

int
ia64_st_address_bypass_p (rtx producer, rtx consumer)
{
  rtx dest, reg, mem;

  gcc_assert (producer && consumer);
  dest = ia64_single_set (producer);
  gcc_assert (dest);
  reg = SET_DEST (dest);
  gcc_assert (reg);
  if (GET_CODE (reg) == SUBREG)
    reg = SUBREG_REG (reg);
  gcc_assert (GET_CODE (reg) == REG);
  
  dest = ia64_single_set (consumer);
  gcc_assert (dest);
  mem = SET_DEST (dest);
  gcc_assert (mem && GET_CODE (mem) == MEM);
  return reg_mentioned_p (reg, mem);
}

/* The following function returns TRUE if PRODUCER (of type ilog or
   ld) produces address for CONSUMER (of type ld or fld). */

int
ia64_ld_address_bypass_p (rtx producer, rtx consumer)
{
  rtx dest, src, reg, mem;

  gcc_assert (producer && consumer);
  dest = ia64_single_set (producer);
  gcc_assert (dest);
  reg = SET_DEST (dest);
  gcc_assert (reg);
  if (GET_CODE (reg) == SUBREG)
    reg = SUBREG_REG (reg);
  gcc_assert (GET_CODE (reg) == REG);
  
  src = ia64_single_set (consumer);
  gcc_assert (src);
  mem = SET_SRC (src);
  gcc_assert (mem);
 
  if (GET_CODE (mem) == UNSPEC && XVECLEN (mem, 0) > 0)
    mem = XVECEXP (mem, 0, 0);
  else if (GET_CODE (mem) == IF_THEN_ELSE)
    /* ??? Is this bypass necessary for ld.c?  */
    {
      gcc_assert (XINT (XEXP (XEXP (mem, 0), 0), 1) == UNSPEC_LDCCLR);
      mem = XEXP (mem, 1);
    }
     
  while (GET_CODE (mem) == SUBREG || GET_CODE (mem) == ZERO_EXTEND)
    mem = XEXP (mem, 0);

  if (GET_CODE (mem) == UNSPEC)
    {
      int c = XINT (mem, 1);

      gcc_assert (c == UNSPEC_LDA || c == UNSPEC_LDS || c == UNSPEC_LDSA);
      mem = XVECEXP (mem, 0, 0);
    }

  /* Note that LO_SUM is used for GOT loads.  */
  gcc_assert (GET_CODE (mem) == LO_SUM || GET_CODE (mem) == MEM);

  return reg_mentioned_p (reg, mem);
}

/* The following function returns TRUE if INSN produces address for a
   load/store insn.  We will place such insns into M slot because it
   decreases its latency time.  */

int
ia64_produce_address_p (rtx insn)
{
  return insn->call;
}


/* Emit pseudo-ops for the assembler to describe predicate relations.
   At present this assumes that we only consider predicate pairs to
   be mutex, and that the assembler can deduce proper values from
   straight-line code.  */

static void
emit_predicate_relation_info (void)
{
  basic_block bb;

  FOR_EACH_BB_REVERSE (bb)
    {
      int r;
      rtx head = BB_HEAD (bb);

      /* We only need such notes at code labels.  */
      if (GET_CODE (head) != CODE_LABEL)
	continue;
      if (GET_CODE (NEXT_INSN (head)) == NOTE
	  && NOTE_LINE_NUMBER (NEXT_INSN (head)) == NOTE_INSN_BASIC_BLOCK)
	head = NEXT_INSN (head);

      /* Skip p0, which may be thought to be live due to (reg:DI p0)
	 grabbing the entire block of predicate registers.  */
      for (r = PR_REG (2); r < PR_REG (64); r += 2)
	if (REGNO_REG_SET_P (bb->il.rtl->global_live_at_start, r))
	  {
	    rtx p = gen_rtx_REG (BImode, r);
	    rtx n = emit_insn_after (gen_pred_rel_mutex (p), head);
	    if (head == BB_END (bb))
	      BB_END (bb) = n;
	    head = n;
	  }
    }

  /* Look for conditional calls that do not return, and protect predicate
     relations around them.  Otherwise the assembler will assume the call
     returns, and complain about uses of call-clobbered predicates after
     the call.  */
  FOR_EACH_BB_REVERSE (bb)
    {
      rtx insn = BB_HEAD (bb);

      while (1)
	{
	  if (GET_CODE (insn) == CALL_INSN
	      && GET_CODE (PATTERN (insn)) == COND_EXEC
	      && find_reg_note (insn, REG_NORETURN, NULL_RTX))
	    {
	      rtx b = emit_insn_before (gen_safe_across_calls_all (), insn);
	      rtx a = emit_insn_after (gen_safe_across_calls_normal (), insn);
	      if (BB_HEAD (bb) == insn)
		BB_HEAD (bb) = b;
	      if (BB_END (bb) == insn)
		BB_END (bb) = a;
	    }

	  if (insn == BB_END (bb))
	    break;
	  insn = NEXT_INSN (insn);
	}
    }
}

/* Perform machine dependent operations on the rtl chain INSNS.  */

static void
ia64_reorg (void)
{
  /* We are freeing block_for_insn in the toplev to keep compatibility
     with old MDEP_REORGS that are not CFG based.  Recompute it now.  */
  compute_bb_for_insn ();

  /* If optimizing, we'll have split before scheduling.  */
  if (optimize == 0)
    split_all_insns (0);

  /* ??? update_life_info_in_dirty_blocks fails to terminate during
     non-optimizing bootstrap.  */
  update_life_info (NULL, UPDATE_LIFE_GLOBAL_RM_NOTES, PROP_DEATH_NOTES);

  if (optimize && ia64_flag_schedule_insns2)
    {
      timevar_push (TV_SCHED2);
      ia64_final_schedule = 1;

      initiate_bundle_states ();
      ia64_nop = make_insn_raw (gen_nop ());
      PREV_INSN (ia64_nop) = NEXT_INSN (ia64_nop) = NULL_RTX;
      recog_memoized (ia64_nop);
      clocks_length = get_max_uid () + 1;
      stops_p = xcalloc (1, clocks_length);
      if (ia64_tune == PROCESSOR_ITANIUM)
	{
	  clocks = xcalloc (clocks_length, sizeof (int));
	  add_cycles = xcalloc (clocks_length, sizeof (int));
	}
      if (ia64_tune == PROCESSOR_ITANIUM2)
	{
	  pos_1 = get_cpu_unit_code ("2_1");
	  pos_2 = get_cpu_unit_code ("2_2");
	  pos_3 = get_cpu_unit_code ("2_3");
	  pos_4 = get_cpu_unit_code ("2_4");
	  pos_5 = get_cpu_unit_code ("2_5");
	  pos_6 = get_cpu_unit_code ("2_6");
	  _0mii_ = get_cpu_unit_code ("2b_0mii.");
	  _0mmi_ = get_cpu_unit_code ("2b_0mmi.");
	  _0mfi_ = get_cpu_unit_code ("2b_0mfi.");
	  _0mmf_ = get_cpu_unit_code ("2b_0mmf.");
	  _0bbb_ = get_cpu_unit_code ("2b_0bbb.");
	  _0mbb_ = get_cpu_unit_code ("2b_0mbb.");
	  _0mib_ = get_cpu_unit_code ("2b_0mib.");
	  _0mmb_ = get_cpu_unit_code ("2b_0mmb.");
	  _0mfb_ = get_cpu_unit_code ("2b_0mfb.");
	  _0mlx_ = get_cpu_unit_code ("2b_0mlx.");
	  _1mii_ = get_cpu_unit_code ("2b_1mii.");
	  _1mmi_ = get_cpu_unit_code ("2b_1mmi.");
	  _1mfi_ = get_cpu_unit_code ("2b_1mfi.");
	  _1mmf_ = get_cpu_unit_code ("2b_1mmf.");
	  _1bbb_ = get_cpu_unit_code ("2b_1bbb.");
	  _1mbb_ = get_cpu_unit_code ("2b_1mbb.");
	  _1mib_ = get_cpu_unit_code ("2b_1mib.");
	  _1mmb_ = get_cpu_unit_code ("2b_1mmb.");
	  _1mfb_ = get_cpu_unit_code ("2b_1mfb.");
	  _1mlx_ = get_cpu_unit_code ("2b_1mlx.");
	}
      else
	{
	  pos_1 = get_cpu_unit_code ("1_1");
	  pos_2 = get_cpu_unit_code ("1_2");
	  pos_3 = get_cpu_unit_code ("1_3");
	  pos_4 = get_cpu_unit_code ("1_4");
	  pos_5 = get_cpu_unit_code ("1_5");
	  pos_6 = get_cpu_unit_code ("1_6");
	  _0mii_ = get_cpu_unit_code ("1b_0mii.");
	  _0mmi_ = get_cpu_unit_code ("1b_0mmi.");
	  _0mfi_ = get_cpu_unit_code ("1b_0mfi.");
	  _0mmf_ = get_cpu_unit_code ("1b_0mmf.");
	  _0bbb_ = get_cpu_unit_code ("1b_0bbb.");
	  _0mbb_ = get_cpu_unit_code ("1b_0mbb.");
	  _0mib_ = get_cpu_unit_code ("1b_0mib.");
	  _0mmb_ = get_cpu_unit_code ("1b_0mmb.");
	  _0mfb_ = get_cpu_unit_code ("1b_0mfb.");
	  _0mlx_ = get_cpu_unit_code ("1b_0mlx.");
	  _1mii_ = get_cpu_unit_code ("1b_1mii.");
	  _1mmi_ = get_cpu_unit_code ("1b_1mmi.");
	  _1mfi_ = get_cpu_unit_code ("1b_1mfi.");
	  _1mmf_ = get_cpu_unit_code ("1b_1mmf.");
	  _1bbb_ = get_cpu_unit_code ("1b_1bbb.");
	  _1mbb_ = get_cpu_unit_code ("1b_1mbb.");
	  _1mib_ = get_cpu_unit_code ("1b_1mib.");
	  _1mmb_ = get_cpu_unit_code ("1b_1mmb.");
	  _1mfb_ = get_cpu_unit_code ("1b_1mfb.");
	  _1mlx_ = get_cpu_unit_code ("1b_1mlx.");
	}
      schedule_ebbs ();
      finish_bundle_states ();
      if (ia64_tune == PROCESSOR_ITANIUM)
	{
	  free (add_cycles);
	  free (clocks);
	}
      free (stops_p);
      stops_p = NULL;
      emit_insn_group_barriers (dump_file);

      ia64_final_schedule = 0;
      timevar_pop (TV_SCHED2);
    }
  else
    emit_all_insn_group_barriers (dump_file);

  /* A call must not be the last instruction in a function, so that the
     return address is still within the function, so that unwinding works
     properly.  Note that IA-64 differs from dwarf2 on this point.  */
  if (flag_unwind_tables || (flag_exceptions && !USING_SJLJ_EXCEPTIONS))
    {
      rtx insn;
      int saw_stop = 0;

      insn = get_last_insn ();
      if (! INSN_P (insn))
        insn = prev_active_insn (insn);
      /* Skip over insns that expand to nothing.  */
      while (GET_CODE (insn) == INSN && get_attr_empty (insn) == EMPTY_YES)
        {
	  if (GET_CODE (PATTERN (insn)) == UNSPEC_VOLATILE
	      && XINT (PATTERN (insn), 1) == UNSPECV_INSN_GROUP_BARRIER)
	    saw_stop = 1;
	  insn = prev_active_insn (insn);
	}
      if (GET_CODE (insn) == CALL_INSN)
	{
	  if (! saw_stop)
	    emit_insn (gen_insn_group_barrier (GEN_INT (3)));
	  emit_insn (gen_break_f ());
	  emit_insn (gen_insn_group_barrier (GEN_INT (3)));
	}
    }

  emit_predicate_relation_info ();

  if (ia64_flag_var_tracking)
    {
      timevar_push (TV_VAR_TRACKING);
      variable_tracking_main ();
      timevar_pop (TV_VAR_TRACKING);
    }
}

/* Return true if REGNO is used by the epilogue.  */

int
ia64_epilogue_uses (int regno)
{
  switch (regno)
    {
    case R_GR (1):
      /* With a call to a function in another module, we will write a new
	 value to "gp".  After returning from such a call, we need to make
	 sure the function restores the original gp-value, even if the
	 function itself does not use the gp anymore.  */
      return !(TARGET_AUTO_PIC || TARGET_NO_PIC);

    case IN_REG (0): case IN_REG (1): case IN_REG (2): case IN_REG (3):
    case IN_REG (4): case IN_REG (5): case IN_REG (6): case IN_REG (7):
      /* For functions defined with the syscall_linkage attribute, all
	 input registers are marked as live at all function exits.  This
	 prevents the register allocator from using the input registers,
	 which in turn makes it possible to restart a system call after
	 an interrupt without having to save/restore the input registers.
	 This also prevents kernel data from leaking to application code.  */
      return lookup_attribute ("syscall_linkage",
	   TYPE_ATTRIBUTES (TREE_TYPE (current_function_decl))) != NULL;

    case R_BR (0):
      /* Conditional return patterns can't represent the use of `b0' as
         the return address, so we force the value live this way.  */
      return 1;

    case AR_PFS_REGNUM:
      /* Likewise for ar.pfs, which is used by br.ret.  */
      return 1;

    default:
      return 0;
    }
}

/* Return true if REGNO is used by the frame unwinder.  */

int
ia64_eh_uses (int regno)
{
  if (! reload_completed)
    return 0;

  if (current_frame_info.reg_save_b0
      && regno == current_frame_info.reg_save_b0)
    return 1;
  if (current_frame_info.reg_save_pr
      && regno == current_frame_info.reg_save_pr)
    return 1;
  if (current_frame_info.reg_save_ar_pfs
      && regno == current_frame_info.reg_save_ar_pfs)
    return 1;
  if (current_frame_info.reg_save_ar_unat
      && regno == current_frame_info.reg_save_ar_unat)
    return 1;
  if (current_frame_info.reg_save_ar_lc
      && regno == current_frame_info.reg_save_ar_lc)
    return 1;

  return 0;
}

/* Return true if this goes in small data/bss.  */

/* ??? We could also support own long data here.  Generating movl/add/ld8
   instead of addl,ld8/ld8.  This makes the code bigger, but should make the
   code faster because there is one less load.  This also includes incomplete
   types which can't go in sdata/sbss.  */

static bool
ia64_in_small_data_p (tree exp)
{
  if (TARGET_NO_SDATA)
    return false;

  /* We want to merge strings, so we never consider them small data.  */
  if (TREE_CODE (exp) == STRING_CST)
    return false;

  /* Functions are never small data.  */
  if (TREE_CODE (exp) == FUNCTION_DECL)
    return false;

  if (TREE_CODE (exp) == VAR_DECL && DECL_SECTION_NAME (exp))
    {
      const char *section = TREE_STRING_POINTER (DECL_SECTION_NAME (exp));

      if (strcmp (section, ".sdata") == 0
	  || strncmp (section, ".sdata.", 7) == 0
	  || strncmp (section, ".gnu.linkonce.s.", 16) == 0
	  || strcmp (section, ".sbss") == 0
	  || strncmp (section, ".sbss.", 6) == 0
	  || strncmp (section, ".gnu.linkonce.sb.", 17) == 0)
	return true;
    }
  else
    {
      HOST_WIDE_INT size = int_size_in_bytes (TREE_TYPE (exp));

      /* If this is an incomplete type with size 0, then we can't put it
	 in sdata because it might be too big when completed.  */
      if (size > 0 && size <= ia64_section_threshold)
	return true;
    }

  return false;
}

/* Output assembly directives for prologue regions.  */

/* The current basic block number.  */

static bool last_block;

/* True if we need a copy_state command at the start of the next block.  */

static bool need_copy_state;

#ifndef MAX_ARTIFICIAL_LABEL_BYTES
# define MAX_ARTIFICIAL_LABEL_BYTES 30
#endif

/* Emit a debugging label after a call-frame-related insn.  We'd
   rather output the label right away, but we'd have to output it
   after, not before, the instruction, and the instruction has not
   been output yet.  So we emit the label after the insn, delete it to
   avoid introducing basic blocks, and mark it as preserved, such that
   it is still output, given that it is referenced in debug info.  */

static const char *
ia64_emit_deleted_label_after_insn (rtx insn)
{
  char label[MAX_ARTIFICIAL_LABEL_BYTES];
  rtx lb = gen_label_rtx ();
  rtx label_insn = emit_label_after (lb, insn);

  LABEL_PRESERVE_P (lb) = 1;

  delete_insn (label_insn);

  ASM_GENERATE_INTERNAL_LABEL (label, "L", CODE_LABEL_NUMBER (label_insn));

  return xstrdup (label);
}

/* Define the CFA after INSN with the steady-state definition.  */

static void
ia64_dwarf2out_def_steady_cfa (rtx insn)
{
  rtx fp = frame_pointer_needed
    ? hard_frame_pointer_rtx
    : stack_pointer_rtx;

  dwarf2out_def_cfa
    (ia64_emit_deleted_label_after_insn (insn),
     REGNO (fp),
     ia64_initial_elimination_offset
     (REGNO (arg_pointer_rtx), REGNO (fp))
     + ARG_POINTER_CFA_OFFSET (current_function_decl));
}

/* The generic dwarf2 frame debug info generator does not define a
   separate region for the very end of the epilogue, so refrain from
   doing so in the IA64-specific code as well.  */

#define IA64_CHANGE_CFA_IN_EPILOGUE 0

/* The function emits unwind directives for the start of an epilogue.  */

static void
process_epilogue (FILE *asm_out_file, rtx insn, bool unwind, bool frame)
{
  /* If this isn't the last block of the function, then we need to label the
     current state, and copy it back in at the start of the next block.  */

  if (!last_block)
    {
      if (unwind)
	fprintf (asm_out_file, "\t.label_state %d\n",
		 ++cfun->machine->state_num);
      need_copy_state = true;
    }

  if (unwind)
    fprintf (asm_out_file, "\t.restore sp\n");
  if (IA64_CHANGE_CFA_IN_EPILOGUE && frame)
    dwarf2out_def_cfa (ia64_emit_deleted_label_after_insn (insn),
		       STACK_POINTER_REGNUM, INCOMING_FRAME_SP_OFFSET);
}

/* This function processes a SET pattern looking for specific patterns
   which result in emitting an assembly directive required for unwinding.  */

static int
process_set (FILE *asm_out_file, rtx pat, rtx insn, bool unwind, bool frame)
{
  rtx src = SET_SRC (pat);
  rtx dest = SET_DEST (pat);
  int src_regno, dest_regno;

  /* Look for the ALLOC insn.  */
  if (GET_CODE (src) == UNSPEC_VOLATILE
      && XINT (src, 1) == UNSPECV_ALLOC
      && GET_CODE (dest) == REG)
    {
      dest_regno = REGNO (dest);

      /* If this is the final destination for ar.pfs, then this must
	 be the alloc in the prologue.  */
      if (dest_regno == current_frame_info.reg_save_ar_pfs)
	{
	  if (unwind)
	    fprintf (asm_out_file, "\t.save ar.pfs, r%d\n",
		     ia64_dbx_register_number (dest_regno));
	}
      else
	{
	  /* This must be an alloc before a sibcall.  We must drop the
	     old frame info.  The easiest way to drop the old frame
	     info is to ensure we had a ".restore sp" directive
	     followed by a new prologue.  If the procedure doesn't
	     have a memory-stack frame, we'll issue a dummy ".restore
	     sp" now.  */
	  if (current_frame_info.total_size == 0 && !frame_pointer_needed)
	    /* if haven't done process_epilogue() yet, do it now */
	    process_epilogue (asm_out_file, insn, unwind, frame);
	  if (unwind)
	    fprintf (asm_out_file, "\t.prologue\n");
	}
      return 1;
    }

  /* Look for SP = ....  */
  if (GET_CODE (dest) == REG && REGNO (dest) == STACK_POINTER_REGNUM)
    {
      if (GET_CODE (src) == PLUS)
        {
	  rtx op0 = XEXP (src, 0);
	  rtx op1 = XEXP (src, 1);
	  
	  gcc_assert (op0 == dest && GET_CODE (op1) == CONST_INT);
	  
	  if (INTVAL (op1) < 0)
	    {
	      gcc_assert (!frame_pointer_needed);
	      if (unwind)
		fprintf (asm_out_file, "\t.fframe "HOST_WIDE_INT_PRINT_DEC"\n",
			 -INTVAL (op1));
	      if (frame)
		ia64_dwarf2out_def_steady_cfa (insn);
	    }
	  else
	    process_epilogue (asm_out_file, insn, unwind, frame);
	}
      else
	{
	  gcc_assert (GET_CODE (src) == REG
		      && REGNO (src) == HARD_FRAME_POINTER_REGNUM);
	  process_epilogue (asm_out_file, insn, unwind, frame);
	}

      return 1;
    }

  /* Register move we need to look at.  */
  if (GET_CODE (dest) == REG && GET_CODE (src) == REG)
    {
      src_regno = REGNO (src);
      dest_regno = REGNO (dest);

      switch (src_regno)
	{
	case BR_REG (0):
	  /* Saving return address pointer.  */
	  gcc_assert (dest_regno == current_frame_info.reg_save_b0);
	  if (unwind)
	    fprintf (asm_out_file, "\t.save rp, r%d\n",
		     ia64_dbx_register_number (dest_regno));
	  return 1;

	case PR_REG (0):
	  gcc_assert (dest_regno == current_frame_info.reg_save_pr);
	  if (unwind)
	    fprintf (asm_out_file, "\t.save pr, r%d\n",
		     ia64_dbx_register_number (dest_regno));
	  return 1;

	case AR_UNAT_REGNUM:
	  gcc_assert (dest_regno == current_frame_info.reg_save_ar_unat);
	  if (unwind)
	    fprintf (asm_out_file, "\t.save ar.unat, r%d\n",
		     ia64_dbx_register_number (dest_regno));
	  return 1;

	case AR_LC_REGNUM:
	  gcc_assert (dest_regno == current_frame_info.reg_save_ar_lc);
	  if (unwind)
	    fprintf (asm_out_file, "\t.save ar.lc, r%d\n",
		     ia64_dbx_register_number (dest_regno));
	  return 1;

	case STACK_POINTER_REGNUM:
	  gcc_assert (dest_regno == HARD_FRAME_POINTER_REGNUM
		      && frame_pointer_needed);
	  if (unwind)
	    fprintf (asm_out_file, "\t.vframe r%d\n",
		     ia64_dbx_register_number (dest_regno));
	  if (frame)
	    ia64_dwarf2out_def_steady_cfa (insn);
	  return 1;

	default:
	  /* Everything else should indicate being stored to memory.  */
	  gcc_unreachable ();
	}
    }

  /* Memory store we need to look at.  */
  if (GET_CODE (dest) == MEM && GET_CODE (src) == REG)
    {
      long off;
      rtx base;
      const char *saveop;

      if (GET_CODE (XEXP (dest, 0)) == REG)
	{
	  base = XEXP (dest, 0);
	  off = 0;
	}
      else
	{
	  gcc_assert (GET_CODE (XEXP (dest, 0)) == PLUS
		      && GET_CODE (XEXP (XEXP (dest, 0), 1)) == CONST_INT);
	  base = XEXP (XEXP (dest, 0), 0);
	  off = INTVAL (XEXP (XEXP (dest, 0), 1));
	}

      if (base == hard_frame_pointer_rtx)
	{
	  saveop = ".savepsp";
	  off = - off;
	}
      else
	{
	  gcc_assert (base == stack_pointer_rtx);
	  saveop = ".savesp";
	}

      src_regno = REGNO (src);
      switch (src_regno)
	{
	case BR_REG (0):
	  gcc_assert (!current_frame_info.reg_save_b0);
	  if (unwind)
	    fprintf (asm_out_file, "\t%s rp, %ld\n", saveop, off);
	  return 1;

	case PR_REG (0):
	  gcc_assert (!current_frame_info.reg_save_pr);
	  if (unwind)
	    fprintf (asm_out_file, "\t%s pr, %ld\n", saveop, off);
	  return 1;

	case AR_LC_REGNUM:
	  gcc_assert (!current_frame_info.reg_save_ar_lc);
	  if (unwind)
	    fprintf (asm_out_file, "\t%s ar.lc, %ld\n", saveop, off);
	  return 1;

	case AR_PFS_REGNUM:
	  gcc_assert (!current_frame_info.reg_save_ar_pfs);
	  if (unwind)
	    fprintf (asm_out_file, "\t%s ar.pfs, %ld\n", saveop, off);
	  return 1;

	case AR_UNAT_REGNUM:
	  gcc_assert (!current_frame_info.reg_save_ar_unat);
	  if (unwind)
	    fprintf (asm_out_file, "\t%s ar.unat, %ld\n", saveop, off);
	  return 1;

	case GR_REG (4):
	case GR_REG (5):
	case GR_REG (6):
	case GR_REG (7):
	  if (unwind)
	    fprintf (asm_out_file, "\t.save.g 0x%x\n",
		     1 << (src_regno - GR_REG (4)));
	  return 1;

	case BR_REG (1):
	case BR_REG (2):
	case BR_REG (3):
	case BR_REG (4):
	case BR_REG (5):
	  if (unwind)
	    fprintf (asm_out_file, "\t.save.b 0x%x\n",
		     1 << (src_regno - BR_REG (1)));
	  return 1;

	case FR_REG (2):
	case FR_REG (3):
	case FR_REG (4):
	case FR_REG (5):
	  if (unwind)
	    fprintf (asm_out_file, "\t.save.f 0x%x\n",
		     1 << (src_regno - FR_REG (2)));
	  return 1;

	case FR_REG (16): case FR_REG (17): case FR_REG (18): case FR_REG (19):
	case FR_REG (20): case FR_REG (21): case FR_REG (22): case FR_REG (23):
	case FR_REG (24): case FR_REG (25): case FR_REG (26): case FR_REG (27):
	case FR_REG (28): case FR_REG (29): case FR_REG (30): case FR_REG (31):
	  if (unwind)
	    fprintf (asm_out_file, "\t.save.gf 0x0, 0x%x\n",
		     1 << (src_regno - FR_REG (12)));
	  return 1;

	default:
	  return 0;
	}
    }

  return 0;
}


/* This function looks at a single insn and emits any directives
   required to unwind this insn.  */
void
process_for_unwind_directive (FILE *asm_out_file, rtx insn)
{
  bool unwind = (flag_unwind_tables
		 || (flag_exceptions && !USING_SJLJ_EXCEPTIONS));
  bool frame = dwarf2out_do_frame ();

  if (unwind || frame)
    {
      rtx pat;

      if (GET_CODE (insn) == NOTE
	  && NOTE_LINE_NUMBER (insn) == NOTE_INSN_BASIC_BLOCK)
	{
	  last_block = NOTE_BASIC_BLOCK (insn)->next_bb == EXIT_BLOCK_PTR;

	  /* Restore unwind state from immediately before the epilogue.  */
	  if (need_copy_state)
	    {
	      if (unwind)
		{
		  fprintf (asm_out_file, "\t.body\n");
		  fprintf (asm_out_file, "\t.copy_state %d\n",
			   cfun->machine->state_num);
		}
	      if (IA64_CHANGE_CFA_IN_EPILOGUE && frame)
		ia64_dwarf2out_def_steady_cfa (insn);
	      need_copy_state = false;
	    }
	}

      if (GET_CODE (insn) == NOTE || ! RTX_FRAME_RELATED_P (insn))
	return;

      pat = find_reg_note (insn, REG_FRAME_RELATED_EXPR, NULL_RTX);
      if (pat)
	pat = XEXP (pat, 0);
      else
	pat = PATTERN (insn);

      switch (GET_CODE (pat))
        {
	case SET:
	  process_set (asm_out_file, pat, insn, unwind, frame);
	  break;

	case PARALLEL:
	  {
	    int par_index;
	    int limit = XVECLEN (pat, 0);
	    for (par_index = 0; par_index < limit; par_index++)
	      {
		rtx x = XVECEXP (pat, 0, par_index);
		if (GET_CODE (x) == SET)
		  process_set (asm_out_file, x, insn, unwind, frame);
	      }
	    break;
	  }

	default:
	  gcc_unreachable ();
	}
    }
}


enum ia64_builtins
{
  IA64_BUILTIN_BSP,
  IA64_BUILTIN_FLUSHRS
};

void
ia64_init_builtins (void)
{
  tree fpreg_type;
  tree float80_type;

  /* The __fpreg type.  */
  fpreg_type = make_node (REAL_TYPE);
  TYPE_PRECISION (fpreg_type) = 82;
  layout_type (fpreg_type);
  (*lang_hooks.types.register_builtin_type) (fpreg_type, "__fpreg");

  /* The __float80 type.  */
  float80_type = make_node (REAL_TYPE);
  TYPE_PRECISION (float80_type) = 80;
  layout_type (float80_type);
  (*lang_hooks.types.register_builtin_type) (float80_type, "__float80");

  /* The __float128 type.  */
  if (!TARGET_HPUX)
    {
      tree float128_type = make_node (REAL_TYPE);
      TYPE_PRECISION (float128_type) = 128;
      layout_type (float128_type);
      (*lang_hooks.types.register_builtin_type) (float128_type, "__float128");
    }
  else
    /* Under HPUX, this is a synonym for "long double".  */
    (*lang_hooks.types.register_builtin_type) (long_double_type_node,
					       "__float128");

#define def_builtin(name, type, code)					\
  lang_hooks.builtin_function ((name), (type), (code), BUILT_IN_MD,	\
			       NULL, NULL_TREE)

  def_builtin ("__builtin_ia64_bsp",
	       build_function_type (ptr_type_node, void_list_node),
	       IA64_BUILTIN_BSP);

  def_builtin ("__builtin_ia64_flushrs",
	       build_function_type (void_type_node, void_list_node),
	       IA64_BUILTIN_FLUSHRS);

#undef def_builtin
}

rtx
ia64_expand_builtin (tree exp, rtx target, rtx subtarget ATTRIBUTE_UNUSED,
		     enum machine_mode mode ATTRIBUTE_UNUSED,
		     int ignore ATTRIBUTE_UNUSED)
{
  tree fndecl = TREE_OPERAND (TREE_OPERAND (exp, 0), 0);
  unsigned int fcode = DECL_FUNCTION_CODE (fndecl);

  switch (fcode)
    {
    case IA64_BUILTIN_BSP:
      if (! target || ! register_operand (target, DImode))
	target = gen_reg_rtx (DImode);
      emit_insn (gen_bsp_value (target));
#ifdef POINTERS_EXTEND_UNSIGNED
      target = convert_memory_address (ptr_mode, target);
#endif
      return target;

    case IA64_BUILTIN_FLUSHRS:
      emit_insn (gen_flushrs ());
      return const0_rtx;

    default:
      break;
    }

  return NULL_RTX;
}

/* For the HP-UX IA64 aggregate parameters are passed stored in the
   most significant bits of the stack slot.  */

enum direction
ia64_hpux_function_arg_padding (enum machine_mode mode, tree type)
{
   /* Exception to normal case for structures/unions/etc.  */

   if (type && AGGREGATE_TYPE_P (type)
       && int_size_in_bytes (type) < UNITS_PER_WORD)
     return upward;

   /* Fall back to the default.  */
   return DEFAULT_FUNCTION_ARG_PADDING (mode, type);
}

/* Emit text to declare externally defined variables and functions, because
   the Intel assembler does not support undefined externals.  */

void
ia64_asm_output_external (FILE *file, tree decl, const char *name)
{
  /* We output the name if and only if TREE_SYMBOL_REFERENCED is
     set in order to avoid putting out names that are never really
     used. */
  if (TREE_SYMBOL_REFERENCED (DECL_ASSEMBLER_NAME (decl)))
    {
      /* maybe_assemble_visibility will return 1 if the assembler
	 visibility directive is outputed.  */
      int need_visibility = ((*targetm.binds_local_p) (decl)
			     && maybe_assemble_visibility (decl));

      /* GNU as does not need anything here, but the HP linker does
	 need something for external functions.  */
      if ((TARGET_HPUX_LD || !TARGET_GNU_AS)
	  && TREE_CODE (decl) == FUNCTION_DECL)
	{
	  ASM_OUTPUT_TYPE_DIRECTIVE (file, name, "function");
	  (*targetm.asm_out.globalize_label) (file, name);
	}
      else if (need_visibility && !TARGET_GNU_AS)
	(*targetm.asm_out.globalize_label) (file, name);
    }
}

/* Set SImode div/mod functions, init_integral_libfuncs only initializes
   modes of word_mode and larger.  Rename the TFmode libfuncs using the
   HPUX conventions. __divtf3 is used for XFmode. We need to keep it for
   backward compatibility. */

static void
ia64_init_libfuncs (void)
{
  set_optab_libfunc (sdiv_optab, SImode, "__divsi3");
  set_optab_libfunc (udiv_optab, SImode, "__udivsi3");
  set_optab_libfunc (smod_optab, SImode, "__modsi3");
  set_optab_libfunc (umod_optab, SImode, "__umodsi3");

  set_optab_libfunc (add_optab, TFmode, "_U_Qfadd");
  set_optab_libfunc (sub_optab, TFmode, "_U_Qfsub");
  set_optab_libfunc (smul_optab, TFmode, "_U_Qfmpy");
  set_optab_libfunc (sdiv_optab, TFmode, "_U_Qfdiv");
  set_optab_libfunc (neg_optab, TFmode, "_U_Qfneg");

  set_conv_libfunc (sext_optab, TFmode, SFmode, "_U_Qfcnvff_sgl_to_quad");
  set_conv_libfunc (sext_optab, TFmode, DFmode, "_U_Qfcnvff_dbl_to_quad");
  set_conv_libfunc (sext_optab, TFmode, XFmode, "_U_Qfcnvff_f80_to_quad");
  set_conv_libfunc (trunc_optab, SFmode, TFmode, "_U_Qfcnvff_quad_to_sgl");
  set_conv_libfunc (trunc_optab, DFmode, TFmode, "_U_Qfcnvff_quad_to_dbl");
  set_conv_libfunc (trunc_optab, XFmode, TFmode, "_U_Qfcnvff_quad_to_f80");

  set_conv_libfunc (sfix_optab, SImode, TFmode, "_U_Qfcnvfxt_quad_to_sgl");
  set_conv_libfunc (sfix_optab, DImode, TFmode, "_U_Qfcnvfxt_quad_to_dbl");
  set_conv_libfunc (sfix_optab, TImode, TFmode, "_U_Qfcnvfxt_quad_to_quad");
  set_conv_libfunc (ufix_optab, SImode, TFmode, "_U_Qfcnvfxut_quad_to_sgl");
  set_conv_libfunc (ufix_optab, DImode, TFmode, "_U_Qfcnvfxut_quad_to_dbl");

  set_conv_libfunc (sfloat_optab, TFmode, SImode, "_U_Qfcnvxf_sgl_to_quad");
  set_conv_libfunc (sfloat_optab, TFmode, DImode, "_U_Qfcnvxf_dbl_to_quad");
  set_conv_libfunc (sfloat_optab, TFmode, TImode, "_U_Qfcnvxf_quad_to_quad");
  /* HP-UX 11.23 libc does not have a function for unsigned
     SImode-to-TFmode conversion.  */
  set_conv_libfunc (ufloat_optab, TFmode, DImode, "_U_Qfcnvxuf_dbl_to_quad");
}

/* Rename all the TFmode libfuncs using the HPUX conventions.  */

static void
ia64_hpux_init_libfuncs (void)
{
  ia64_init_libfuncs ();

  /* The HP SI millicode division and mod functions expect DI arguments.
     By turning them off completely we avoid using both libgcc and the
     non-standard millicode routines and use the HP DI millicode routines
     instead.  */

  set_optab_libfunc (sdiv_optab, SImode, 0);
  set_optab_libfunc (udiv_optab, SImode, 0);
  set_optab_libfunc (smod_optab, SImode, 0);
  set_optab_libfunc (umod_optab, SImode, 0);

  set_optab_libfunc (sdiv_optab, DImode, "__milli_divI");
  set_optab_libfunc (udiv_optab, DImode, "__milli_divU");
  set_optab_libfunc (smod_optab, DImode, "__milli_remI");
  set_optab_libfunc (umod_optab, DImode, "__milli_remU");

  /* HP-UX libc has TF min/max/abs routines in it.  */
  set_optab_libfunc (smin_optab, TFmode, "_U_Qfmin");
  set_optab_libfunc (smax_optab, TFmode, "_U_Qfmax");
  set_optab_libfunc (abs_optab, TFmode, "_U_Qfabs");

  /* ia64_expand_compare uses this.  */
  cmptf_libfunc = init_one_libfunc ("_U_Qfcmp");

  /* These should never be used.  */
  set_optab_libfunc (eq_optab, TFmode, 0);
  set_optab_libfunc (ne_optab, TFmode, 0);
  set_optab_libfunc (gt_optab, TFmode, 0);
  set_optab_libfunc (ge_optab, TFmode, 0);
  set_optab_libfunc (lt_optab, TFmode, 0);
  set_optab_libfunc (le_optab, TFmode, 0);
}

/* Rename the division and modulus functions in VMS.  */

static void
ia64_vms_init_libfuncs (void)
{
  set_optab_libfunc (sdiv_optab, SImode, "OTS$DIV_I");
  set_optab_libfunc (sdiv_optab, DImode, "OTS$DIV_L");
  set_optab_libfunc (udiv_optab, SImode, "OTS$DIV_UI");
  set_optab_libfunc (udiv_optab, DImode, "OTS$DIV_UL");
  set_optab_libfunc (smod_optab, SImode, "OTS$REM_I");
  set_optab_libfunc (smod_optab, DImode, "OTS$REM_L");
  set_optab_libfunc (umod_optab, SImode, "OTS$REM_UI");
  set_optab_libfunc (umod_optab, DImode, "OTS$REM_UL");
}

/* Rename the TFmode libfuncs available from soft-fp in glibc using
   the HPUX conventions.  */

static void
ia64_sysv4_init_libfuncs (void)
{
  ia64_init_libfuncs ();

  /* These functions are not part of the HPUX TFmode interface.  We
     use them instead of _U_Qfcmp, which doesn't work the way we
     expect.  */
  set_optab_libfunc (eq_optab, TFmode, "_U_Qfeq");
  set_optab_libfunc (ne_optab, TFmode, "_U_Qfne");
  set_optab_libfunc (gt_optab, TFmode, "_U_Qfgt");
  set_optab_libfunc (ge_optab, TFmode, "_U_Qfge");
  set_optab_libfunc (lt_optab, TFmode, "_U_Qflt");
  set_optab_libfunc (le_optab, TFmode, "_U_Qfle");

  /* We leave out _U_Qfmin, _U_Qfmax and _U_Qfabs since soft-fp in
     glibc doesn't have them.  */
}

/* For HPUX, it is illegal to have relocations in shared segments.  */

static int
ia64_hpux_reloc_rw_mask (void)
{
  return 3;
}

/* For others, relax this so that relocations to local data goes in
   read-only segments, but we still cannot allow global relocations
   in read-only segments.  */

static int
ia64_reloc_rw_mask (void)
{
  return flag_pic ? 3 : 2;
}

/* Return the section to use for X.  The only special thing we do here
   is to honor small data.  */

static section *
ia64_select_rtx_section (enum machine_mode mode, rtx x,
			 unsigned HOST_WIDE_INT align)
{
  if (GET_MODE_SIZE (mode) > 0
      && GET_MODE_SIZE (mode) <= ia64_section_threshold
      && !TARGET_NO_SDATA)
    return sdata_section;
  else
    return default_elf_select_rtx_section (mode, x, align);
}

static unsigned int
ia64_section_type_flags (tree decl, const char *name, int reloc)
{
  unsigned int flags = 0;

  if (strcmp (name, ".sdata") == 0
      || strncmp (name, ".sdata.", 7) == 0
      || strncmp (name, ".gnu.linkonce.s.", 16) == 0
      || strncmp (name, ".sdata2.", 8) == 0
      || strncmp (name, ".gnu.linkonce.s2.", 17) == 0
      || strcmp (name, ".sbss") == 0
      || strncmp (name, ".sbss.", 6) == 0
      || strncmp (name, ".gnu.linkonce.sb.", 17) == 0)
    flags = SECTION_SMALL;

  flags |= default_section_type_flags (decl, name, reloc);
  return flags;
}

/* Returns true if FNTYPE (a FUNCTION_TYPE or a METHOD_TYPE) returns a
   structure type and that the address of that type should be passed
   in out0, rather than in r8.  */

static bool
ia64_struct_retval_addr_is_first_parm_p (tree fntype)
{
  tree ret_type = TREE_TYPE (fntype);

  /* The Itanium C++ ABI requires that out0, rather than r8, be used
     as the structure return address parameter, if the return value
     type has a non-trivial copy constructor or destructor.  It is not
     clear if this same convention should be used for other
     programming languages.  Until G++ 3.4, we incorrectly used r8 for
     these return values.  */
  return (abi_version_at_least (2)
	  && ret_type
	  && TYPE_MODE (ret_type) == BLKmode 
	  && TREE_ADDRESSABLE (ret_type)
	  && strcmp (lang_hooks.name, "GNU C++") == 0);
}

/* Output the assembler code for a thunk function.  THUNK_DECL is the
   declaration for the thunk function itself, FUNCTION is the decl for
   the target function.  DELTA is an immediate constant offset to be
   added to THIS.  If VCALL_OFFSET is nonzero, the word at
   *(*this + vcall_offset) should be added to THIS.  */

static void
ia64_output_mi_thunk (FILE *file, tree thunk ATTRIBUTE_UNUSED,
		      HOST_WIDE_INT delta, HOST_WIDE_INT vcall_offset,
		      tree function)
{
  rtx this, insn, funexp;
  unsigned int this_parmno;
  unsigned int this_regno;

  reload_completed = 1;
  epilogue_completed = 1;
  no_new_pseudos = 1;
  reset_block_changes ();

  /* Set things up as ia64_expand_prologue might.  */
  last_scratch_gr_reg = 15;

  memset (&current_frame_info, 0, sizeof (current_frame_info));
  current_frame_info.spill_cfa_off = -16;
  current_frame_info.n_input_regs = 1;
  current_frame_info.need_regstk = (TARGET_REG_NAMES != 0);

  /* Mark the end of the (empty) prologue.  */
  emit_note (NOTE_INSN_PROLOGUE_END);

  /* Figure out whether "this" will be the first parameter (the
     typical case) or the second parameter (as happens when the
     virtual function returns certain class objects).  */
  this_parmno
    = (ia64_struct_retval_addr_is_first_parm_p (TREE_TYPE (thunk))
       ? 1 : 0);
  this_regno = IN_REG (this_parmno);
  if (!TARGET_REG_NAMES)
    reg_names[this_regno] = ia64_reg_numbers[this_parmno];

  this = gen_rtx_REG (Pmode, this_regno);
  if (TARGET_ILP32)
    {
      rtx tmp = gen_rtx_REG (ptr_mode, this_regno);
      REG_POINTER (tmp) = 1;
      if (delta && CONST_OK_FOR_I (delta))
	{
	  emit_insn (gen_ptr_extend_plus_imm (this, tmp, GEN_INT (delta)));
	  delta = 0;
	}
      else
	emit_insn (gen_ptr_extend (this, tmp));
    }

  /* Apply the constant offset, if required.  */
  if (delta)
    {
      rtx delta_rtx = GEN_INT (delta);

      if (!CONST_OK_FOR_I (delta))
	{
	  rtx tmp = gen_rtx_REG (Pmode, 2);
	  emit_move_insn (tmp, delta_rtx);
	  delta_rtx = tmp;
	}
      emit_insn (gen_adddi3 (this, this, delta_rtx));
    }

  /* Apply the offset from the vtable, if required.  */
  if (vcall_offset)
    {
      rtx vcall_offset_rtx = GEN_INT (vcall_offset);
      rtx tmp = gen_rtx_REG (Pmode, 2);

      if (TARGET_ILP32)
	{
	  rtx t = gen_rtx_REG (ptr_mode, 2);
	  REG_POINTER (t) = 1;
	  emit_move_insn (t, gen_rtx_MEM (ptr_mode, this));
	  if (CONST_OK_FOR_I (vcall_offset))
	    {
	      emit_insn (gen_ptr_extend_plus_imm (tmp, t, 
						  vcall_offset_rtx));
	      vcall_offset = 0;
	    }
	  else
	    emit_insn (gen_ptr_extend (tmp, t));
	}
      else
	emit_move_insn (tmp, gen_rtx_MEM (Pmode, this));

      if (vcall_offset)
	{
	  if (!CONST_OK_FOR_J (vcall_offset))
	    {
	      rtx tmp2 = gen_rtx_REG (Pmode, next_scratch_gr_reg ());
	      emit_move_insn (tmp2, vcall_offset_rtx);
	      vcall_offset_rtx = tmp2;
	    }
	  emit_insn (gen_adddi3 (tmp, tmp, vcall_offset_rtx));
	}

      if (TARGET_ILP32)
	emit_move_insn (gen_rtx_REG (ptr_mode, 2), 
			gen_rtx_MEM (ptr_mode, tmp));
      else
	emit_move_insn (tmp, gen_rtx_MEM (Pmode, tmp));

      emit_insn (gen_adddi3 (this, this, tmp));
    }

  /* Generate a tail call to the target function.  */
  if (! TREE_USED (function))
    {
      assemble_external (function);
      TREE_USED (function) = 1;
    }
  funexp = XEXP (DECL_RTL (function), 0);
  funexp = gen_rtx_MEM (FUNCTION_MODE, funexp);
  ia64_expand_call (NULL_RTX, funexp, NULL_RTX, 1);
  insn = get_last_insn ();
  SIBLING_CALL_P (insn) = 1;

  /* Code generation for calls relies on splitting.  */
  reload_completed = 1;
  epilogue_completed = 1;
  try_split (PATTERN (insn), insn, 0);

  emit_barrier ();

  /* Run just enough of rest_of_compilation to get the insns emitted.
     There's not really enough bulk here to make other passes such as
     instruction scheduling worth while.  Note that use_thunk calls
     assemble_start_function and assemble_end_function.  */

  insn_locators_initialize ();
  emit_all_insn_group_barriers (NULL);
  insn = get_insns ();
  shorten_branches (insn);
  final_start_function (insn, file, 1);
  final (insn, file, 1);
  final_end_function ();

  reload_completed = 0;
  epilogue_completed = 0;
  no_new_pseudos = 0;
}

/* Worker function for TARGET_STRUCT_VALUE_RTX.  */

static rtx
ia64_struct_value_rtx (tree fntype,
		       int incoming ATTRIBUTE_UNUSED)
{
  if (fntype && ia64_struct_retval_addr_is_first_parm_p (fntype))
    return NULL_RTX;
  return gen_rtx_REG (Pmode, GR_REG (8));
}

static bool
ia64_scalar_mode_supported_p (enum machine_mode mode)
{
  switch (mode)
    {
    case QImode:
    case HImode:
    case SImode:
    case DImode:
    case TImode:
      return true;

    case SFmode:
    case DFmode:
    case XFmode:
    case RFmode:
      return true;

    case TFmode:
      return TARGET_HPUX;

    default:
      return false;
    }
}

static bool
ia64_vector_mode_supported_p (enum machine_mode mode)
{
  switch (mode)
    {
    case V8QImode:
    case V4HImode:
    case V2SImode:
      return true;

    case V2SFmode:
      return true;

    default:
      return false;
    }
}

/* Implement the FUNCTION_PROFILER macro.  */

void
ia64_output_function_profiler (FILE *file, int labelno)
{
  bool indirect_call;

  /* If the function needs a static chain and the static chain
     register is r15, we use an indirect call so as to bypass
     the PLT stub in case the executable is dynamically linked,
     because the stub clobbers r15 as per 5.3.6 of the psABI.
     We don't need to do that in non canonical PIC mode.  */

  if (cfun->static_chain_decl && !TARGET_NO_PIC && !TARGET_AUTO_PIC)
    {
      gcc_assert (STATIC_CHAIN_REGNUM == 15);
      indirect_call = true;
    }
  else
    indirect_call = false;

  if (TARGET_GNU_AS)
    fputs ("\t.prologue 4, r40\n", file);
  else
    fputs ("\t.prologue\n\t.save ar.pfs, r40\n", file);
  fputs ("\talloc out0 = ar.pfs, 8, 0, 4, 0\n", file);

  if (NO_PROFILE_COUNTERS)
    fputs ("\tmov out3 = r0\n", file);
  else
    {
      char buf[20];
      ASM_GENERATE_INTERNAL_LABEL (buf, "LP", labelno);

      if (TARGET_AUTO_PIC)
	fputs ("\tmovl out3 = @gprel(", file);
      else
	fputs ("\taddl out3 = @ltoff(", file);
      assemble_name (file, buf);
      if (TARGET_AUTO_PIC)
	fputs (")\n", file);
      else
	fputs ("), r1\n", file);
    }

  if (indirect_call)
    fputs ("\taddl r14 = @ltoff(@fptr(_mcount)), r1\n", file);
  fputs ("\t;;\n", file);

  fputs ("\t.save rp, r42\n", file);
  fputs ("\tmov out2 = b0\n", file);
  if (indirect_call)
    fputs ("\tld8 r14 = [r14]\n\t;;\n", file);
  fputs ("\t.body\n", file);
  fputs ("\tmov out1 = r1\n", file);
  if (indirect_call)
    {
      fputs ("\tld8 r16 = [r14], 8\n\t;;\n", file);
      fputs ("\tmov b6 = r16\n", file);
      fputs ("\tld8 r1 = [r14]\n", file);
      fputs ("\tbr.call.sptk.many b0 = b6\n\t;;\n", file);
    }
  else
    fputs ("\tbr.call.sptk.many b0 = _mcount\n\t;;\n", file);
}

static GTY(()) rtx mcount_func_rtx;
static rtx
gen_mcount_func_rtx (void)
{
  if (!mcount_func_rtx)
    mcount_func_rtx = init_one_libfunc ("_mcount");
  return mcount_func_rtx;
}

void
ia64_profile_hook (int labelno)
{
  rtx label, ip;

  if (NO_PROFILE_COUNTERS)
    label = const0_rtx;
  else
    {
      char buf[30];
      const char *label_name;
      ASM_GENERATE_INTERNAL_LABEL (buf, "LP", labelno);
      label_name = (*targetm.strip_name_encoding) (ggc_strdup (buf));
      label = gen_rtx_SYMBOL_REF (Pmode, label_name);
      SYMBOL_REF_FLAGS (label) = SYMBOL_FLAG_LOCAL;
    }
  ip = gen_reg_rtx (Pmode);
  emit_insn (gen_ip_value (ip));
  emit_library_call (gen_mcount_func_rtx (), LCT_NORMAL,
                     VOIDmode, 3,
		     gen_rtx_REG (Pmode, BR_REG (0)), Pmode,
		     ip, Pmode,
		     label, Pmode);
}

/* Return the mangling of TYPE if it is an extended fundamental type.  */

static const char *
ia64_mangle_fundamental_type (tree type)
{
  /* On HP-UX, "long double" is mangled as "e" so __float128 is
     mangled as "e".  */
  if (!TARGET_HPUX && TYPE_MODE (type) == TFmode)
    return "g";
  /* On HP-UX, "e" is not available as a mangling of __float80 so use
     an extended mangling.  Elsewhere, "e" is available since long
     double is 80 bits.  */
  if (TYPE_MODE (type) == XFmode)
    return TARGET_HPUX ? "u9__float80" : "e";
  if (TYPE_MODE (type) == RFmode)
    return "u7__fpreg";
  return NULL;
}

/* Return the diagnostic message string if conversion from FROMTYPE to
   TOTYPE is not allowed, NULL otherwise.  */
static const char *
ia64_invalid_conversion (tree fromtype, tree totype)
{
  /* Reject nontrivial conversion to or from __fpreg.  */
  if (TYPE_MODE (fromtype) == RFmode
      && TYPE_MODE (totype) != RFmode
      && TYPE_MODE (totype) != VOIDmode)
    return N_("invalid conversion from %<__fpreg%>");
  if (TYPE_MODE (totype) == RFmode
      && TYPE_MODE (fromtype) != RFmode)
    return N_("invalid conversion to %<__fpreg%>");
  return NULL;
}

/* Return the diagnostic message string if the unary operation OP is
   not permitted on TYPE, NULL otherwise.  */
static const char *
ia64_invalid_unary_op (int op, tree type)
{
  /* Reject operations on __fpreg other than unary + or &.  */
  if (TYPE_MODE (type) == RFmode
      && op != CONVERT_EXPR
      && op != ADDR_EXPR)
    return N_("invalid operation on %<__fpreg%>");
  return NULL;
}

/* Return the diagnostic message string if the binary operation OP is
   not permitted on TYPE1 and TYPE2, NULL otherwise.  */
static const char *
ia64_invalid_binary_op (int op ATTRIBUTE_UNUSED, tree type1, tree type2)
{
  /* Reject operations on __fpreg.  */
  if (TYPE_MODE (type1) == RFmode || TYPE_MODE (type2) == RFmode)
    return N_("invalid operation on %<__fpreg%>");
  return NULL;
}

/* Implement overriding of the optimization options.  */
void
ia64_optimization_options (int level ATTRIBUTE_UNUSED,
                           int size ATTRIBUTE_UNUSED)
{
  /* Let the scheduler form additional regions.  */
  set_param_value ("max-sched-extend-regions-iters", 2);
}

#include "gt-ia64.h"
