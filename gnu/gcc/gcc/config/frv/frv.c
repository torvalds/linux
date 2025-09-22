/* Copyright (C) 1997, 1998, 1999, 2000, 2001, 2003, 2004, 2005
   Free Software Foundation, Inc.
   Contributed by Red Hat, Inc.

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
#include "insn-flags.h"
#include "output.h"
#include "insn-attr.h"
#include "flags.h"
#include "recog.h"
#include "reload.h"
#include "expr.h"
#include "obstack.h"
#include "except.h"
#include "function.h"
#include "optabs.h"
#include "toplev.h"
#include "basic-block.h"
#include "tm_p.h"
#include "ggc.h"
#include <ctype.h>
#include "target.h"
#include "target-def.h"
#include "targhooks.h"
#include "integrate.h"
#include "langhooks.h"

#ifndef FRV_INLINE
#define FRV_INLINE inline
#endif

/* The maximum number of distinct NOP patterns.  There are three:
   nop, fnop and mnop.  */
#define NUM_NOP_PATTERNS 3

/* Classification of instructions and units: integer, floating-point/media,
   branch and control.  */
enum frv_insn_group { GROUP_I, GROUP_FM, GROUP_B, GROUP_C, NUM_GROUPS };

/* The DFA names of the units, in packet order.  */
static const char *const frv_unit_names[] =
{
  "c",
  "i0", "f0",
  "i1", "f1",
  "i2", "f2",
  "i3", "f3",
  "b0", "b1"
};

/* The classification of each unit in frv_unit_names[].  */
static const enum frv_insn_group frv_unit_groups[ARRAY_SIZE (frv_unit_names)] =
{
  GROUP_C,
  GROUP_I, GROUP_FM,
  GROUP_I, GROUP_FM,
  GROUP_I, GROUP_FM,
  GROUP_I, GROUP_FM,
  GROUP_B, GROUP_B
};

/* Return the DFA unit code associated with the Nth unit of integer
   or floating-point group GROUP,  */
#define NTH_UNIT(GROUP, N) frv_unit_codes[(GROUP) + (N) * 2 + 1]

/* Return the number of integer or floating-point unit UNIT
   (1 for I1, 2 for F2, etc.).  */
#define UNIT_NUMBER(UNIT) (((UNIT) - 1) / 2)

/* The DFA unit number for each unit in frv_unit_names[].  */
static int frv_unit_codes[ARRAY_SIZE (frv_unit_names)];

/* FRV_TYPE_TO_UNIT[T] is the last unit in frv_unit_names[] that can issue
   an instruction of type T.  The value is ARRAY_SIZE (frv_unit_names) if
   no instruction of type T has been seen.  */
static unsigned int frv_type_to_unit[TYPE_UNKNOWN + 1];

/* An array of dummy nop INSNs, one for each type of nop that the
   target supports.  */
static GTY(()) rtx frv_nops[NUM_NOP_PATTERNS];

/* The number of nop instructions in frv_nops[].  */
static unsigned int frv_num_nops;

/* Information about one __builtin_read or __builtin_write access, or
   the combination of several such accesses.  The most general value
   is all-zeros (an unknown access to an unknown address).  */
struct frv_io {
  /* The type of access.  FRV_IO_UNKNOWN means the access can be either
     a read or a write.  */
  enum { FRV_IO_UNKNOWN, FRV_IO_READ, FRV_IO_WRITE } type;

  /* The constant address being accessed, or zero if not known.  */
  HOST_WIDE_INT const_address;

  /* The run-time address, as used in operand 0 of the membar pattern.  */
  rtx var_address;
};

/* Return true if instruction INSN should be packed with the following
   instruction.  */
#define PACKING_FLAG_P(INSN) (GET_MODE (INSN) == TImode)

/* Set the value of PACKING_FLAG_P(INSN).  */
#define SET_PACKING_FLAG(INSN) PUT_MODE (INSN, TImode)
#define CLEAR_PACKING_FLAG(INSN) PUT_MODE (INSN, VOIDmode)

/* Loop with REG set to each hard register in rtx X.  */
#define FOR_EACH_REGNO(REG, X)						\
  for (REG = REGNO (X);							\
       REG < REGNO (X) + HARD_REGNO_NREGS (REGNO (X), GET_MODE (X));	\
       REG++)

/* This structure contains machine specific function data.  */
struct machine_function GTY(())
{
  /* True if we have created an rtx that relies on the stack frame.  */
  int frame_needed;

  /* True if this function contains at least one __builtin_{read,write}*.  */
  bool has_membar_p;
};

/* Temporary register allocation support structure.  */
typedef struct frv_tmp_reg_struct
  {
    HARD_REG_SET regs;		/* possible registers to allocate */
    int next_reg[N_REG_CLASSES];	/* next register to allocate per class */
  }
frv_tmp_reg_t;

/* Register state information for VLIW re-packing phase.  */
#define REGSTATE_CC_MASK	0x07	/* Mask to isolate CCn for cond exec */
#define REGSTATE_MODIFIED	0x08	/* reg modified in current VLIW insn */
#define REGSTATE_IF_TRUE	0x10	/* reg modified in cond exec true */
#define REGSTATE_IF_FALSE	0x20	/* reg modified in cond exec false */

#define REGSTATE_IF_EITHER	(REGSTATE_IF_TRUE | REGSTATE_IF_FALSE)

typedef unsigned char regstate_t;

/* Used in frv_frame_accessor_t to indicate the direction of a register-to-
   memory move.  */
enum frv_stack_op
{
  FRV_LOAD,
  FRV_STORE
};

/* Information required by frv_frame_access.  */
typedef struct
{
  /* This field is FRV_LOAD if registers are to be loaded from the stack and
     FRV_STORE if they should be stored onto the stack.  FRV_STORE implies
     the move is being done by the prologue code while FRV_LOAD implies it
     is being done by the epilogue.  */
  enum frv_stack_op op;

  /* The base register to use when accessing the stack.  This may be the
     frame pointer, stack pointer, or a temporary.  The choice of register
     depends on which part of the frame is being accessed and how big the
     frame is.  */
  rtx base;

  /* The offset of BASE from the bottom of the current frame, in bytes.  */
  int base_offset;
} frv_frame_accessor_t;

/* Define the information needed to generate branch and scc insns.  This is
   stored from the compare operation.  */
rtx frv_compare_op0;
rtx frv_compare_op1;

/* Conditional execution support gathered together in one structure.  */
typedef struct
  {
    /* Linked list of insns to add if the conditional execution conversion was
       successful.  Each link points to an EXPR_LIST which points to the pattern
       of the insn to add, and the insn to be inserted before.  */
    rtx added_insns_list;

    /* Identify which registers are safe to allocate for if conversions to
       conditional execution.  We keep the last allocated register in the
       register classes between COND_EXEC statements.  This will mean we allocate
       different registers for each different COND_EXEC group if we can.  This
       might allow the scheduler to intermix two different COND_EXEC sections.  */
    frv_tmp_reg_t tmp_reg;

    /* For nested IFs, identify which CC registers are used outside of setting
       via a compare isnsn, and using via a check insn.  This will allow us to
       know if we can rewrite the register to use a different register that will
       be paired with the CR register controlling the nested IF-THEN blocks.  */
    HARD_REG_SET nested_cc_ok_rewrite;

    /* Temporary registers allocated to hold constants during conditional
       execution.  */
    rtx scratch_regs[FIRST_PSEUDO_REGISTER];

    /* Current number of temp registers available.  */
    int cur_scratch_regs;

    /* Number of nested conditional execution blocks.  */
    int num_nested_cond_exec;

    /* Map of insns that set up constants in scratch registers.  */
    bitmap scratch_insns_bitmap;

    /* Conditional execution test register (CC0..CC7).  */
    rtx cr_reg;

    /* Conditional execution compare register that is paired with cr_reg, so that
       nested compares can be done.  The csubcc and caddcc instructions don't
       have enough bits to specify both a CC register to be set and a CR register
       to do the test on, so the same bit number is used for both.  Needless to
       say, this is rather inconvenient for GCC.  */
    rtx nested_cc_reg;

    /* Extra CR registers used for &&, ||.  */
    rtx extra_int_cr;
    rtx extra_fp_cr;

    /* Previous CR used in nested if, to make sure we are dealing with the same
       nested if as the previous statement.  */
    rtx last_nested_if_cr;
  }
frv_ifcvt_t;

static /* GTY(()) */ frv_ifcvt_t frv_ifcvt;

/* Map register number to smallest register class.  */
enum reg_class regno_reg_class[FIRST_PSEUDO_REGISTER];

/* Map class letter into register class.  */
enum reg_class reg_class_from_letter[256];

/* Cached value of frv_stack_info.  */
static frv_stack_t *frv_stack_cache = (frv_stack_t *)0;

/* -mcpu= support */
frv_cpu_t frv_cpu_type = CPU_TYPE;	/* value of -mcpu= */

/* Forward references */

static bool frv_handle_option			(size_t, const char *, int);
static int frv_default_flags_for_cpu		(void);
static int frv_string_begins_with		(tree, const char *);
static FRV_INLINE bool frv_small_data_reloc_p	(rtx, int);
static void frv_print_operand_memory_reference_reg
						(FILE *, rtx);
static void frv_print_operand_memory_reference	(FILE *, rtx, int);
static int frv_print_operand_jump_hint		(rtx);
static const char *comparison_string		(enum rtx_code, rtx);
static FRV_INLINE int frv_regno_ok_for_base_p	(int, int);
static rtx single_set_pattern			(rtx);
static int frv_function_contains_far_jump	(void);
static rtx frv_alloc_temp_reg			(frv_tmp_reg_t *,
						 enum reg_class,
						 enum machine_mode,
						 int, int);
static rtx frv_frame_offset_rtx			(int);
static rtx frv_frame_mem			(enum machine_mode, rtx, int);
static rtx frv_dwarf_store			(rtx, int);
static void frv_frame_insn			(rtx, rtx);
static void frv_frame_access			(frv_frame_accessor_t*,
						 rtx, int);
static void frv_frame_access_multi		(frv_frame_accessor_t*,
						 frv_stack_t *, int);
static void frv_frame_access_standard_regs	(enum frv_stack_op,
						 frv_stack_t *);
static struct machine_function *frv_init_machine_status		(void);
static rtx frv_int_to_acc			(enum insn_code, int, rtx);
static enum machine_mode frv_matching_accg_mode	(enum machine_mode);
static rtx frv_read_argument			(tree *);
static rtx frv_read_iacc_argument		(enum machine_mode, tree *);
static int frv_check_constant_argument		(enum insn_code, int, rtx);
static rtx frv_legitimize_target		(enum insn_code, rtx);
static rtx frv_legitimize_argument		(enum insn_code, int, rtx);
static rtx frv_legitimize_tls_address		(rtx, enum tls_model);
static rtx frv_expand_set_builtin		(enum insn_code, tree, rtx);
static rtx frv_expand_unop_builtin		(enum insn_code, tree, rtx);
static rtx frv_expand_binop_builtin		(enum insn_code, tree, rtx);
static rtx frv_expand_cut_builtin		(enum insn_code, tree, rtx);
static rtx frv_expand_binopimm_builtin		(enum insn_code, tree, rtx);
static rtx frv_expand_voidbinop_builtin		(enum insn_code, tree);
static rtx frv_expand_int_void2arg		(enum insn_code, tree);
static rtx frv_expand_prefetches		(enum insn_code, tree);
static rtx frv_expand_voidtriop_builtin		(enum insn_code, tree);
static rtx frv_expand_voidaccop_builtin		(enum insn_code, tree);
static rtx frv_expand_mclracc_builtin		(tree);
static rtx frv_expand_mrdacc_builtin		(enum insn_code, tree);
static rtx frv_expand_mwtacc_builtin		(enum insn_code, tree);
static rtx frv_expand_noargs_builtin		(enum insn_code);
static void frv_split_iacc_move			(rtx, rtx);
static rtx frv_emit_comparison			(enum rtx_code, rtx, rtx);
static int frv_clear_registers_used		(rtx *, void *);
static void frv_ifcvt_add_insn			(rtx, rtx, int);
static rtx frv_ifcvt_rewrite_mem		(rtx, enum machine_mode, rtx);
static rtx frv_ifcvt_load_value			(rtx, rtx);
static int frv_acc_group_1			(rtx *, void *);
static unsigned int frv_insn_unit		(rtx);
static bool frv_issues_to_branch_unit_p		(rtx);
static int frv_cond_flags 			(rtx);
static bool frv_regstate_conflict_p 		(regstate_t, regstate_t);
static int frv_registers_conflict_p_1 		(rtx *, void *);
static bool frv_registers_conflict_p 		(rtx);
static void frv_registers_update_1 		(rtx, rtx, void *);
static void frv_registers_update 		(rtx);
static void frv_start_packet 			(void);
static void frv_start_packet_block 		(void);
static void frv_finish_packet 			(void (*) (void));
static bool frv_pack_insn_p 			(rtx);
static void frv_add_insn_to_packet		(rtx);
static void frv_insert_nop_in_packet		(rtx);
static bool frv_for_each_packet 		(void (*) (void));
static bool frv_sort_insn_group_1		(enum frv_insn_group,
						 unsigned int, unsigned int,
						 unsigned int, unsigned int,
						 state_t);
static int frv_compare_insns			(const void *, const void *);
static void frv_sort_insn_group			(enum frv_insn_group);
static void frv_reorder_packet 			(void);
static void frv_fill_unused_units		(enum frv_insn_group);
static void frv_align_label 			(void);
static void frv_reorg_packet 			(void);
static void frv_register_nop			(rtx);
static void frv_reorg 				(void);
static void frv_pack_insns			(void);
static void frv_function_prologue		(FILE *, HOST_WIDE_INT);
static void frv_function_epilogue		(FILE *, HOST_WIDE_INT);
static bool frv_assemble_integer		(rtx, unsigned, int);
static void frv_init_builtins			(void);
static rtx frv_expand_builtin			(tree, rtx, rtx, enum machine_mode, int);
static void frv_init_libfuncs			(void);
static bool frv_in_small_data_p			(tree);
static void frv_asm_output_mi_thunk
  (FILE *, tree, HOST_WIDE_INT, HOST_WIDE_INT, tree);
static void frv_setup_incoming_varargs		(CUMULATIVE_ARGS *,
						 enum machine_mode,
						 tree, int *, int);
static rtx frv_expand_builtin_saveregs		(void);
static bool frv_rtx_costs			(rtx, int, int, int*);
static void frv_asm_out_constructor		(rtx, int);
static void frv_asm_out_destructor		(rtx, int);
static bool frv_function_symbol_referenced_p	(rtx);
static bool frv_cannot_force_const_mem		(rtx);
static const char *unspec_got_name		(int);
static void frv_output_const_unspec		(FILE *,
						 const struct frv_unspec *);
static bool frv_function_ok_for_sibcall		(tree, tree);
static rtx frv_struct_value_rtx			(tree, int);
static bool frv_must_pass_in_stack (enum machine_mode mode, tree type);
static int frv_arg_partial_bytes (CUMULATIVE_ARGS *, enum machine_mode,
				  tree, bool);
static void frv_output_dwarf_dtprel		(FILE *, int, rtx)
  ATTRIBUTE_UNUSED;

/* Allow us to easily change the default for -malloc-cc.  */
#ifndef DEFAULT_NO_ALLOC_CC
#define MASK_DEFAULT_ALLOC_CC	MASK_ALLOC_CC
#else
#define MASK_DEFAULT_ALLOC_CC	0
#endif

/* Initialize the GCC target structure.  */
#undef  TARGET_ASM_FUNCTION_PROLOGUE
#define TARGET_ASM_FUNCTION_PROLOGUE frv_function_prologue
#undef  TARGET_ASM_FUNCTION_EPILOGUE
#define TARGET_ASM_FUNCTION_EPILOGUE frv_function_epilogue
#undef  TARGET_ASM_INTEGER
#define TARGET_ASM_INTEGER frv_assemble_integer
#undef TARGET_DEFAULT_TARGET_FLAGS
#define TARGET_DEFAULT_TARGET_FLAGS		\
  (MASK_DEFAULT_ALLOC_CC			\
   | MASK_COND_MOVE				\
   | MASK_SCC					\
   | MASK_COND_EXEC				\
   | MASK_VLIW_BRANCH				\
   | MASK_MULTI_CE				\
   | MASK_NESTED_CE)
#undef TARGET_HANDLE_OPTION
#define TARGET_HANDLE_OPTION frv_handle_option
#undef TARGET_INIT_BUILTINS
#define TARGET_INIT_BUILTINS frv_init_builtins
#undef TARGET_EXPAND_BUILTIN
#define TARGET_EXPAND_BUILTIN frv_expand_builtin
#undef TARGET_INIT_LIBFUNCS
#define TARGET_INIT_LIBFUNCS frv_init_libfuncs
#undef TARGET_IN_SMALL_DATA_P
#define TARGET_IN_SMALL_DATA_P frv_in_small_data_p
#undef TARGET_RTX_COSTS
#define TARGET_RTX_COSTS frv_rtx_costs
#undef TARGET_ASM_CONSTRUCTOR
#define TARGET_ASM_CONSTRUCTOR frv_asm_out_constructor
#undef TARGET_ASM_DESTRUCTOR
#define TARGET_ASM_DESTRUCTOR frv_asm_out_destructor

#undef TARGET_ASM_OUTPUT_MI_THUNK
#define TARGET_ASM_OUTPUT_MI_THUNK frv_asm_output_mi_thunk
#undef TARGET_ASM_CAN_OUTPUT_MI_THUNK
#define TARGET_ASM_CAN_OUTPUT_MI_THUNK default_can_output_mi_thunk_no_vcall

#undef  TARGET_SCHED_ISSUE_RATE
#define TARGET_SCHED_ISSUE_RATE frv_issue_rate

#undef TARGET_FUNCTION_OK_FOR_SIBCALL
#define TARGET_FUNCTION_OK_FOR_SIBCALL frv_function_ok_for_sibcall
#undef TARGET_CANNOT_FORCE_CONST_MEM
#define TARGET_CANNOT_FORCE_CONST_MEM frv_cannot_force_const_mem

#undef TARGET_HAVE_TLS
#define TARGET_HAVE_TLS HAVE_AS_TLS

#undef TARGET_STRUCT_VALUE_RTX
#define TARGET_STRUCT_VALUE_RTX frv_struct_value_rtx
#undef TARGET_MUST_PASS_IN_STACK
#define TARGET_MUST_PASS_IN_STACK frv_must_pass_in_stack
#undef TARGET_PASS_BY_REFERENCE
#define TARGET_PASS_BY_REFERENCE hook_pass_by_reference_must_pass_in_stack
#undef TARGET_ARG_PARTIAL_BYTES
#define TARGET_ARG_PARTIAL_BYTES frv_arg_partial_bytes

#undef TARGET_EXPAND_BUILTIN_SAVEREGS
#define TARGET_EXPAND_BUILTIN_SAVEREGS frv_expand_builtin_saveregs
#undef TARGET_SETUP_INCOMING_VARARGS
#define TARGET_SETUP_INCOMING_VARARGS frv_setup_incoming_varargs
#undef TARGET_MACHINE_DEPENDENT_REORG
#define TARGET_MACHINE_DEPENDENT_REORG frv_reorg

#if HAVE_AS_TLS
#undef TARGET_ASM_OUTPUT_DWARF_DTPREL
#define TARGET_ASM_OUTPUT_DWARF_DTPREL frv_output_dwarf_dtprel
#endif

struct gcc_target targetm = TARGET_INITIALIZER;

#define FRV_SYMBOL_REF_TLS_P(RTX) \
  (GET_CODE (RTX) == SYMBOL_REF && SYMBOL_REF_TLS_MODEL (RTX) != 0)


/* Any function call that satisfies the machine-independent
   requirements is eligible on FR-V.  */

static bool
frv_function_ok_for_sibcall (tree decl ATTRIBUTE_UNUSED,
			     tree exp ATTRIBUTE_UNUSED)
{
  return true;
}

/* Return true if SYMBOL is a small data symbol and relocation RELOC
   can be used to access it directly in a load or store.  */

static FRV_INLINE bool
frv_small_data_reloc_p (rtx symbol, int reloc)
{
  return (GET_CODE (symbol) == SYMBOL_REF
	  && SYMBOL_REF_SMALL_P (symbol)
	  && (!TARGET_FDPIC || flag_pic == 1)
	  && (reloc == R_FRV_GOTOFF12 || reloc == R_FRV_GPREL12));
}

/* Return true if X is a valid relocation unspec.  If it is, fill in UNSPEC
   appropriately.  */

bool
frv_const_unspec_p (rtx x, struct frv_unspec *unspec)
{
  if (GET_CODE (x) == CONST)
    {
      unspec->offset = 0;
      x = XEXP (x, 0);
      if (GET_CODE (x) == PLUS && GET_CODE (XEXP (x, 1)) == CONST_INT)
	{
	  unspec->offset += INTVAL (XEXP (x, 1));
	  x = XEXP (x, 0);
	}
      if (GET_CODE (x) == UNSPEC && XINT (x, 1) == UNSPEC_GOT)
	{
	  unspec->symbol = XVECEXP (x, 0, 0);
	  unspec->reloc = INTVAL (XVECEXP (x, 0, 1));

	  if (unspec->offset == 0)
	    return true;

	  if (frv_small_data_reloc_p (unspec->symbol, unspec->reloc)
	      && unspec->offset > 0
	      && (unsigned HOST_WIDE_INT) unspec->offset < g_switch_value)
	    return true;
	}
    }
  return false;
}

/* Decide whether we can force certain constants to memory.  If we
   decide we can't, the caller should be able to cope with it in
   another way.

   We never allow constants to be forced into memory for TARGET_FDPIC.
   This is necessary for several reasons:

   1. Since LEGITIMATE_CONSTANT_P rejects constant pool addresses, the
      target-independent code will try to force them into the constant
      pool, thus leading to infinite recursion.

   2. We can never introduce new constant pool references during reload.
      Any such reference would require use of the pseudo FDPIC register.

   3. We can't represent a constant added to a function pointer (which is
      not the same as a pointer to a function+constant).

   4. In many cases, it's more efficient to calculate the constant in-line.  */

static bool
frv_cannot_force_const_mem (rtx x ATTRIBUTE_UNUSED)
{
  return TARGET_FDPIC;
}

/* Implement TARGET_HANDLE_OPTION.  */

static bool
frv_handle_option (size_t code, const char *arg, int value ATTRIBUTE_UNUSED)
{
  switch (code)
    {
    case OPT_mcpu_:
      if (strcmp (arg, "simple") == 0)
	frv_cpu_type = FRV_CPU_SIMPLE;
      else if (strcmp (arg, "tomcat") == 0)
	frv_cpu_type = FRV_CPU_TOMCAT;
      else if (strcmp (arg, "fr550") == 0)
	frv_cpu_type = FRV_CPU_FR550;
      else if (strcmp (arg, "fr500") == 0)
	frv_cpu_type = FRV_CPU_FR500;
      else if (strcmp (arg, "fr450") == 0)
	frv_cpu_type = FRV_CPU_FR450;
      else if (strcmp (arg, "fr405") == 0)
	frv_cpu_type = FRV_CPU_FR405;
      else if (strcmp (arg, "fr400") == 0)
	frv_cpu_type = FRV_CPU_FR400;
      else if (strcmp (arg, "fr300") == 0)
	frv_cpu_type = FRV_CPU_FR300;
      else if (strcmp (arg, "frv") == 0)
	frv_cpu_type = FRV_CPU_GENERIC;
      else
	return false;
      return true;

    default:
      return true;
    }
}

static int
frv_default_flags_for_cpu (void)
{
  switch (frv_cpu_type)
    {
    case FRV_CPU_GENERIC:
      return MASK_DEFAULT_FRV;

    case FRV_CPU_FR550:
      return MASK_DEFAULT_FR550;

    case FRV_CPU_FR500:
    case FRV_CPU_TOMCAT:
      return MASK_DEFAULT_FR500;

    case FRV_CPU_FR450:
      return MASK_DEFAULT_FR450;

    case FRV_CPU_FR405:
    case FRV_CPU_FR400:
      return MASK_DEFAULT_FR400;

    case FRV_CPU_FR300:
    case FRV_CPU_SIMPLE:
      return MASK_DEFAULT_SIMPLE;

    default:
      gcc_unreachable ();
    }
}

/* Sometimes certain combinations of command options do not make
   sense on a particular target machine.  You can define a macro
   `OVERRIDE_OPTIONS' to take account of this.  This macro, if
   defined, is executed once just after all the command options have
   been parsed.

   Don't use this macro to turn on various extra optimizations for
   `-O'.  That is what `OPTIMIZATION_OPTIONS' is for.  */

void
frv_override_options (void)
{
  int regno;
  unsigned int i;

  target_flags |= (frv_default_flags_for_cpu () & ~target_flags_explicit);

  /* -mlibrary-pic sets -fPIC and -G0 and also suppresses warnings from the
     linker about linking pic and non-pic code.  */
  if (TARGET_LIBPIC)
    {
      if (!flag_pic)		/* -fPIC */
	flag_pic = 2;

      if (! g_switch_set)	/* -G0 */
	{
	  g_switch_set = 1;
	  g_switch_value = 0;
	}
    }

  /* A C expression whose value is a register class containing hard
     register REGNO.  In general there is more than one such class;
     choose a class which is "minimal", meaning that no smaller class
     also contains the register.  */

  for (regno = 0; regno < FIRST_PSEUDO_REGISTER; regno++)
    {
      enum reg_class class;

      if (GPR_P (regno))
	{
	  int gpr_reg = regno - GPR_FIRST;

	  if (gpr_reg == GR8_REG)
	    class = GR8_REGS;

	  else if (gpr_reg == GR9_REG)
	    class = GR9_REGS;

	  else if (gpr_reg == GR14_REG)
	    class = FDPIC_FPTR_REGS;

	  else if (gpr_reg == FDPIC_REGNO)
	    class = FDPIC_REGS;

	  else if ((gpr_reg & 3) == 0)
	    class = QUAD_REGS;

	  else if ((gpr_reg & 1) == 0)
	    class = EVEN_REGS;

	  else
	    class = GPR_REGS;
	}

      else if (FPR_P (regno))
	{
	  int fpr_reg = regno - GPR_FIRST;
	  if ((fpr_reg & 3) == 0)
	    class = QUAD_FPR_REGS;

	  else if ((fpr_reg & 1) == 0)
	    class = FEVEN_REGS;

	  else
	    class = FPR_REGS;
	}

      else if (regno == LR_REGNO)
	class = LR_REG;

      else if (regno == LCR_REGNO)
	class = LCR_REG;

      else if (ICC_P (regno))
	class = ICC_REGS;

      else if (FCC_P (regno))
	class = FCC_REGS;

      else if (ICR_P (regno))
	class = ICR_REGS;

      else if (FCR_P (regno))
	class = FCR_REGS;

      else if (ACC_P (regno))
	{
	  int r = regno - ACC_FIRST;
	  if ((r & 3) == 0)
	    class = QUAD_ACC_REGS;
	  else if ((r & 1) == 0)
	    class = EVEN_ACC_REGS;
	  else
	    class = ACC_REGS;
	}

      else if (ACCG_P (regno))
	class = ACCG_REGS;

      else
	class = NO_REGS;

      regno_reg_class[regno] = class;
    }

  /* Check for small data option */
  if (!g_switch_set)
    g_switch_value = SDATA_DEFAULT_SIZE;

  /* A C expression which defines the machine-dependent operand
     constraint letters for register classes.  If CHAR is such a
     letter, the value should be the register class corresponding to
     it.  Otherwise, the value should be `NO_REGS'.  The register
     letter `r', corresponding to class `GENERAL_REGS', will not be
     passed to this macro; you do not need to handle it.

     The following letters are unavailable, due to being used as
     constraints:
	'0'..'9'
	'<', '>'
	'E', 'F', 'G', 'H'
	'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P'
	'Q', 'R', 'S', 'T', 'U'
	'V', 'X'
	'g', 'i', 'm', 'n', 'o', 'p', 'r', 's' */

  for (i = 0; i < 256; i++)
    reg_class_from_letter[i] = NO_REGS;

  reg_class_from_letter['a'] = ACC_REGS;
  reg_class_from_letter['b'] = EVEN_ACC_REGS;
  reg_class_from_letter['c'] = CC_REGS;
  reg_class_from_letter['d'] = GPR_REGS;
  reg_class_from_letter['e'] = EVEN_REGS;
  reg_class_from_letter['f'] = FPR_REGS;
  reg_class_from_letter['h'] = FEVEN_REGS;
  reg_class_from_letter['l'] = LR_REG;
  reg_class_from_letter['q'] = QUAD_REGS;
  reg_class_from_letter['t'] = ICC_REGS;
  reg_class_from_letter['u'] = FCC_REGS;
  reg_class_from_letter['v'] = ICR_REGS;
  reg_class_from_letter['w'] = FCR_REGS;
  reg_class_from_letter['x'] = QUAD_FPR_REGS;
  reg_class_from_letter['y'] = LCR_REG;
  reg_class_from_letter['z'] = SPR_REGS;
  reg_class_from_letter['A'] = QUAD_ACC_REGS;
  reg_class_from_letter['B'] = ACCG_REGS;
  reg_class_from_letter['C'] = CR_REGS;
  reg_class_from_letter['W'] = FDPIC_CALL_REGS; /* gp14+15 */
  reg_class_from_letter['Z'] = FDPIC_REGS; /* gp15 */

  /* There is no single unaligned SI op for PIC code.  Sometimes we
     need to use ".4byte" and sometimes we need to use ".picptr".
     See frv_assemble_integer for details.  */
  if (flag_pic || TARGET_FDPIC)
    targetm.asm_out.unaligned_op.si = 0;

  if ((target_flags_explicit & MASK_LINKED_FP) == 0)
    target_flags |= MASK_LINKED_FP;

  if ((target_flags_explicit & MASK_OPTIMIZE_MEMBAR) == 0)
    target_flags |= MASK_OPTIMIZE_MEMBAR;

  for (i = 0; i < ARRAY_SIZE (frv_unit_names); i++)
    frv_unit_codes[i] = get_cpu_unit_code (frv_unit_names[i]);

  for (i = 0; i < ARRAY_SIZE (frv_type_to_unit); i++)
    frv_type_to_unit[i] = ARRAY_SIZE (frv_unit_codes);

  init_machine_status = frv_init_machine_status;
}


/* Some machines may desire to change what optimizations are performed for
   various optimization levels.  This macro, if defined, is executed once just
   after the optimization level is determined and before the remainder of the
   command options have been parsed.  Values set in this macro are used as the
   default values for the other command line options.

   LEVEL is the optimization level specified; 2 if `-O2' is specified, 1 if
   `-O' is specified, and 0 if neither is specified.

   SIZE is nonzero if `-Os' is specified, 0 otherwise.

   You should not use this macro to change options that are not
   machine-specific.  These should uniformly selected by the same optimization
   level on all supported machines.  Use this macro to enable machine-specific
   optimizations.

   *Do not examine `write_symbols' in this macro!* The debugging options are
   *not supposed to alter the generated code.  */

/* On the FRV, possibly disable VLIW packing which is done by the 2nd
   scheduling pass at the current time.  */
void
frv_optimization_options (int level, int size ATTRIBUTE_UNUSED)
{
  if (level >= 2)
    {
#ifdef DISABLE_SCHED2
      flag_schedule_insns_after_reload = 0;
#endif
#ifdef ENABLE_RCSP
      flag_rcsp = 1;
#endif
    }
}


/* Return true if NAME (a STRING_CST node) begins with PREFIX.  */

static int
frv_string_begins_with (tree name, const char *prefix)
{
  int prefix_len = strlen (prefix);

  /* Remember: NAME's length includes the null terminator.  */
  return (TREE_STRING_LENGTH (name) > prefix_len
	  && strncmp (TREE_STRING_POINTER (name), prefix, prefix_len) == 0);
}

/* Zero or more C statements that may conditionally modify two variables
   `fixed_regs' and `call_used_regs' (both of type `char []') after they have
   been initialized from the two preceding macros.

   This is necessary in case the fixed or call-clobbered registers depend on
   target flags.

   You need not define this macro if it has no work to do.

   If the usage of an entire class of registers depends on the target flags,
   you may indicate this to GCC by using this macro to modify `fixed_regs' and
   `call_used_regs' to 1 for each of the registers in the classes which should
   not be used by GCC.  Also define the macro `REG_CLASS_FROM_LETTER' to return
   `NO_REGS' if it is called with a letter for a class that shouldn't be used.

   (However, if this class is not included in `GENERAL_REGS' and all of the
   insn patterns whose constraints permit this class are controlled by target
   switches, then GCC will automatically avoid using these registers when the
   target switches are opposed to them.)  */

void
frv_conditional_register_usage (void)
{
  int i;

  for (i = GPR_FIRST + NUM_GPRS; i <= GPR_LAST; i++)
    fixed_regs[i] = call_used_regs[i] = 1;

  for (i = FPR_FIRST + NUM_FPRS; i <= FPR_LAST; i++)
    fixed_regs[i] = call_used_regs[i] = 1;

  /* Reserve the registers used for conditional execution.  At present, we need
     1 ICC and 1 ICR register.  */
  fixed_regs[ICC_TEMP] = call_used_regs[ICC_TEMP] = 1;
  fixed_regs[ICR_TEMP] = call_used_regs[ICR_TEMP] = 1;

  if (TARGET_FIXED_CC)
    {
      fixed_regs[ICC_FIRST] = call_used_regs[ICC_FIRST] = 1;
      fixed_regs[FCC_FIRST] = call_used_regs[FCC_FIRST] = 1;
      fixed_regs[ICR_FIRST] = call_used_regs[ICR_FIRST] = 1;
      fixed_regs[FCR_FIRST] = call_used_regs[FCR_FIRST] = 1;
    }

  if (TARGET_FDPIC)
    fixed_regs[GPR_FIRST + 16] = fixed_regs[GPR_FIRST + 17] =
      call_used_regs[GPR_FIRST + 16] = call_used_regs[GPR_FIRST + 17] = 0;

#if 0
  /* If -fpic, SDA_BASE_REG is the PIC register.  */
  if (g_switch_value == 0 && !flag_pic)
    fixed_regs[SDA_BASE_REG] = call_used_regs[SDA_BASE_REG] = 0;

  if (!flag_pic)
    fixed_regs[PIC_REGNO] = call_used_regs[PIC_REGNO] = 0;
#endif
}


/*
 * Compute the stack frame layout
 *
 * Register setup:
 * +---------------+-----------------------+-----------------------+
 * |Register       |type                   |caller-save/callee-save|
 * +---------------+-----------------------+-----------------------+
 * |GR0            |Zero register          |        -              |
 * |GR1            |Stack pointer(SP)      |        -              |
 * |GR2            |Frame pointer(FP)      |        -              |
 * |GR3            |Hidden parameter       |        caller save    |
 * |GR4-GR7        |        -              |        caller save    |
 * |GR8-GR13       |Argument register      |        caller save    |
 * |GR14-GR15      |        -              |        caller save    |
 * |GR16-GR31      |        -              |        callee save    |
 * |GR32-GR47      |        -              |        caller save    |
 * |GR48-GR63      |        -              |        callee save    |
 * |FR0-FR15       |        -              |        caller save    |
 * |FR16-FR31      |        -              |        callee save    |
 * |FR32-FR47      |        -              |        caller save    |
 * |FR48-FR63      |        -              |        callee save    |
 * +---------------+-----------------------+-----------------------+
 *
 * Stack frame setup:
 * Low
 *     SP-> |-----------------------------------|
 *	    |         Argument area		|
 *	    |-----------------------------------|
 *	    |	 Register save area		|
 *	    |-----------------------------------|
 *	    |	Local variable save area	|
 *     FP-> |-----------------------------------|
 *	    |	    Old FP			|
 *	    |-----------------------------------|
 *	    |    Hidden parameter save area     |
 *	    |-----------------------------------|
 *	    | Return address(LR) storage area   |
 *	    |-----------------------------------|
 *	    |     Padding for alignment         |
 *	    |-----------------------------------|
 *	    |     Register argument area	|
 * OLD SP-> |-----------------------------------|
 *          |       Parameter area		|
 *          |-----------------------------------|
 * High
 *
 * Argument area/Parameter area:
 *
 * When a function is called, this area is used for argument transfer.  When
 * the argument is set up by the caller function, this area is referred to as
 * the argument area.  When the argument is referenced by the callee function,
 * this area is referred to as the parameter area.  The area is allocated when
 * all arguments cannot be placed on the argument register at the time of
 * argument transfer.
 *
 * Register save area:
 *
 * This is a register save area that must be guaranteed for the caller
 * function.  This area is not secured when the register save operation is not
 * needed.
 *
 * Local variable save area:
 *
 * This is the area for local variables and temporary variables.
 *
 * Old FP:
 *
 * This area stores the FP value of the caller function.
 *
 * Hidden parameter save area:
 *
 * This area stores the start address of the return value storage
 * area for a struct/union return function.
 * When a struct/union is used as the return value, the caller
 * function stores the return value storage area start address in
 * register GR3 and passes it to the caller function.
 * The callee function interprets the address stored in the GR3
 * as the return value storage area start address.
 * When register GR3 needs to be saved into memory, the callee
 * function saves it in the hidden parameter save area.  This
 * area is not secured when the save operation is not needed.
 *
 * Return address(LR) storage area:
 *
 * This area saves the LR.  The LR stores the address of a return to the caller
 * function for the purpose of function calling.
 *
 * Argument register area:
 *
 * This area saves the argument register.  This area is not secured when the
 * save operation is not needed.
 *
 * Argument:
 *
 * Arguments, the count of which equals the count of argument registers (6
 * words), are positioned in registers GR8 to GR13 and delivered to the callee
 * function.  When a struct/union return function is called, the return value
 * area address is stored in register GR3.  Arguments not placed in the
 * argument registers will be stored in the stack argument area for transfer
 * purposes.  When an 8-byte type argument is to be delivered using registers,
 * it is divided into two and placed in two registers for transfer.  When
 * argument registers must be saved to memory, the callee function secures an
 * argument register save area in the stack.  In this case, a continuous
 * argument register save area must be established in the parameter area.  The
 * argument register save area must be allocated as needed to cover the size of
 * the argument register to be saved.  If the function has a variable count of
 * arguments, it saves all argument registers in the argument register save
 * area.
 *
 * Argument Extension Format:
 *
 * When an argument is to be stored in the stack, its type is converted to an
 * extended type in accordance with the individual argument type.  The argument
 * is freed by the caller function after the return from the callee function is
 * made.
 *
 * +-----------------------+---------------+------------------------+
 * |    Argument Type      |Extended Type  |Stack Storage Size(byte)|
 * +-----------------------+---------------+------------------------+
 * |char                   |int            |        4		    |
 * |signed char            |int            |        4		    |
 * |unsigned char          |int            |        4		    |
 * |[signed] short int     |int            |        4		    |
 * |unsigned short int     |int            |        4		    |
 * |[signed] int           |No extension   |        4		    |
 * |unsigned int           |No extension   |        4		    |
 * |[signed] long int      |No extension   |        4		    |
 * |unsigned long int      |No extension   |        4		    |
 * |[signed] long long int |No extension   |        8		    |
 * |unsigned long long int |No extension   |        8		    |
 * |float                  |double         |        8		    |
 * |double                 |No extension   |        8		    |
 * |long double            |No extension   |        8		    |
 * |pointer                |No extension   |        4		    |
 * |struct/union           |-              |        4 (*1)	    |
 * +-----------------------+---------------+------------------------+
 *
 * When a struct/union is to be delivered as an argument, the caller copies it
 * to the local variable area and delivers the address of that area.
 *
 * Return Value:
 *
 * +-------------------------------+----------------------+
 * |Return Value Type              |Return Value Interface|
 * +-------------------------------+----------------------+
 * |void                           |None                  |
 * |[signed|unsigned] char         |GR8                   |
 * |[signed|unsigned] short int    |GR8                   |
 * |[signed|unsigned] int          |GR8                   |
 * |[signed|unsigned] long int     |GR8                   |
 * |pointer                        |GR8                   |
 * |[signed|unsigned] long long int|GR8 & GR9             |
 * |float                          |GR8                   |
 * |double                         |GR8 & GR9             |
 * |long double                    |GR8 & GR9             |
 * |struct/union                   |(*1)                  |
 * +-------------------------------+----------------------+
 *
 * When a struct/union is used as the return value, the caller function stores
 * the start address of the return value storage area into GR3 and then passes
 * it to the callee function.  The callee function interprets GR3 as the start
 * address of the return value storage area.  When this address needs to be
 * saved in memory, the callee function secures the hidden parameter save area
 * and saves the address in that area.
 */

frv_stack_t *
frv_stack_info (void)
{
  static frv_stack_t info, zero_info;
  frv_stack_t *info_ptr	= &info;
  tree fndecl		= current_function_decl;
  int varargs_p		= 0;
  tree cur_arg;
  tree next_arg;
  int range;
  int alignment;
  int offset;

  /* If we've already calculated the values and reload is complete,
     just return now.  */
  if (frv_stack_cache)
    return frv_stack_cache;

  /* Zero all fields.  */
  info = zero_info;

  /* Set up the register range information.  */
  info_ptr->regs[STACK_REGS_GPR].name         = "gpr";
  info_ptr->regs[STACK_REGS_GPR].first        = LAST_ARG_REGNUM + 1;
  info_ptr->regs[STACK_REGS_GPR].last         = GPR_LAST;
  info_ptr->regs[STACK_REGS_GPR].dword_p      = TRUE;

  info_ptr->regs[STACK_REGS_FPR].name         = "fpr";
  info_ptr->regs[STACK_REGS_FPR].first        = FPR_FIRST;
  info_ptr->regs[STACK_REGS_FPR].last         = FPR_LAST;
  info_ptr->regs[STACK_REGS_FPR].dword_p      = TRUE;

  info_ptr->regs[STACK_REGS_LR].name          = "lr";
  info_ptr->regs[STACK_REGS_LR].first         = LR_REGNO;
  info_ptr->regs[STACK_REGS_LR].last          = LR_REGNO;
  info_ptr->regs[STACK_REGS_LR].special_p     = 1;

  info_ptr->regs[STACK_REGS_CC].name          = "cc";
  info_ptr->regs[STACK_REGS_CC].first         = CC_FIRST;
  info_ptr->regs[STACK_REGS_CC].last          = CC_LAST;
  info_ptr->regs[STACK_REGS_CC].field_p       = TRUE;

  info_ptr->regs[STACK_REGS_LCR].name         = "lcr";
  info_ptr->regs[STACK_REGS_LCR].first        = LCR_REGNO;
  info_ptr->regs[STACK_REGS_LCR].last         = LCR_REGNO;

  info_ptr->regs[STACK_REGS_STDARG].name      = "stdarg";
  info_ptr->regs[STACK_REGS_STDARG].first     = FIRST_ARG_REGNUM;
  info_ptr->regs[STACK_REGS_STDARG].last      = LAST_ARG_REGNUM;
  info_ptr->regs[STACK_REGS_STDARG].dword_p   = 1;
  info_ptr->regs[STACK_REGS_STDARG].special_p = 1;

  info_ptr->regs[STACK_REGS_STRUCT].name      = "struct";
  info_ptr->regs[STACK_REGS_STRUCT].first     = FRV_STRUCT_VALUE_REGNUM;
  info_ptr->regs[STACK_REGS_STRUCT].last      = FRV_STRUCT_VALUE_REGNUM;
  info_ptr->regs[STACK_REGS_STRUCT].special_p = 1;

  info_ptr->regs[STACK_REGS_FP].name          = "fp";
  info_ptr->regs[STACK_REGS_FP].first         = FRAME_POINTER_REGNUM;
  info_ptr->regs[STACK_REGS_FP].last          = FRAME_POINTER_REGNUM;
  info_ptr->regs[STACK_REGS_FP].special_p     = 1;

  /* Determine if this is a stdarg function.  If so, allocate space to store
     the 6 arguments.  */
  if (cfun->stdarg)
    varargs_p = 1;

  else
    {
      /* Find the last argument, and see if it is __builtin_va_alist.  */
      for (cur_arg = DECL_ARGUMENTS (fndecl); cur_arg != (tree)0; cur_arg = next_arg)
	{
	  next_arg = TREE_CHAIN (cur_arg);
	  if (next_arg == (tree)0)
	    {
	      if (DECL_NAME (cur_arg)
		  && !strcmp (IDENTIFIER_POINTER (DECL_NAME (cur_arg)), "__builtin_va_alist"))
		varargs_p = 1;

	      break;
	    }
	}
    }

  /* Iterate over all of the register ranges.  */
  for (range = 0; range < STACK_REGS_MAX; range++)
    {
      frv_stack_regs_t *reg_ptr = &(info_ptr->regs[range]);
      int first = reg_ptr->first;
      int last = reg_ptr->last;
      int size_1word = 0;
      int size_2words = 0;
      int regno;

      /* Calculate which registers need to be saved & save area size.  */
      switch (range)
	{
	default:
	  for (regno = first; regno <= last; regno++)
	    {
	      if ((regs_ever_live[regno] && !call_used_regs[regno])
		  || (current_function_calls_eh_return
		      && (regno >= FIRST_EH_REGNUM && regno <= LAST_EH_REGNUM))
		  || (!TARGET_FDPIC && flag_pic
		      && cfun->uses_pic_offset_table && regno == PIC_REGNO))
		{
		  info_ptr->save_p[regno] = REG_SAVE_1WORD;
		  size_1word += UNITS_PER_WORD;
		}
	    }
	  break;

	  /* Calculate whether we need to create a frame after everything else
             has been processed.  */
	case STACK_REGS_FP:
	  break;

	case STACK_REGS_LR:
	  if (regs_ever_live[LR_REGNO]
              || profile_flag
	      /* This is set for __builtin_return_address, etc.  */
	      || cfun->machine->frame_needed
              || (TARGET_LINKED_FP && frame_pointer_needed)
              || (!TARGET_FDPIC && flag_pic
		  && cfun->uses_pic_offset_table))
	    {
	      info_ptr->save_p[LR_REGNO] = REG_SAVE_1WORD;
	      size_1word += UNITS_PER_WORD;
	    }
	  break;

	case STACK_REGS_STDARG:
	  if (varargs_p)
	    {
	      /* If this is a stdarg function with a non varardic
		 argument split between registers and the stack,
		 adjust the saved registers downward.  */
	      last -= (ADDR_ALIGN (cfun->pretend_args_size, UNITS_PER_WORD)
		       / UNITS_PER_WORD);

	      for (regno = first; regno <= last; regno++)
		{
		  info_ptr->save_p[regno] = REG_SAVE_1WORD;
		  size_1word += UNITS_PER_WORD;
		}

	      info_ptr->stdarg_size = size_1word;
	    }
	  break;

	case STACK_REGS_STRUCT:
	  if (cfun->returns_struct)
	    {
	      info_ptr->save_p[FRV_STRUCT_VALUE_REGNUM] = REG_SAVE_1WORD;
	      size_1word += UNITS_PER_WORD;
	    }
	  break;
	}


      if (size_1word)
	{
	  /* If this is a field, it only takes one word.  */
	  if (reg_ptr->field_p)
	    size_1word = UNITS_PER_WORD;

	  /* Determine which register pairs can be saved together.  */
	  else if (reg_ptr->dword_p && TARGET_DWORD)
	    {
	      for (regno = first; regno < last; regno += 2)
		{
		  if (info_ptr->save_p[regno] && info_ptr->save_p[regno+1])
		    {
		      size_2words += 2 * UNITS_PER_WORD;
		      size_1word -= 2 * UNITS_PER_WORD;
		      info_ptr->save_p[regno] = REG_SAVE_2WORDS;
		      info_ptr->save_p[regno+1] = REG_SAVE_NO_SAVE;
		    }
		}
	    }

	  reg_ptr->size_1word = size_1word;
	  reg_ptr->size_2words = size_2words;

	  if (! reg_ptr->special_p)
	    {
	      info_ptr->regs_size_1word += size_1word;
	      info_ptr->regs_size_2words += size_2words;
	    }
	}
    }

  /* Set up the sizes of each each field in the frame body, making the sizes
     of each be divisible by the size of a dword if dword operations might
     be used, or the size of a word otherwise.  */
  alignment = (TARGET_DWORD? 2 * UNITS_PER_WORD : UNITS_PER_WORD);

  info_ptr->parameter_size = ADDR_ALIGN (cfun->outgoing_args_size, alignment);
  info_ptr->regs_size = ADDR_ALIGN (info_ptr->regs_size_2words
				    + info_ptr->regs_size_1word,
				    alignment);
  info_ptr->vars_size = ADDR_ALIGN (get_frame_size (), alignment);

  info_ptr->pretend_size = cfun->pretend_args_size;

  /* Work out the size of the frame, excluding the header.  Both the frame
     body and register parameter area will be dword-aligned.  */
  info_ptr->total_size
    = (ADDR_ALIGN (info_ptr->parameter_size
		   + info_ptr->regs_size
		   + info_ptr->vars_size,
		   2 * UNITS_PER_WORD)
       + ADDR_ALIGN (info_ptr->pretend_size
		     + info_ptr->stdarg_size,
		     2 * UNITS_PER_WORD));

  /* See if we need to create a frame at all, if so add header area.  */
  if (info_ptr->total_size  > 0
      || frame_pointer_needed
      || info_ptr->regs[STACK_REGS_LR].size_1word > 0
      || info_ptr->regs[STACK_REGS_STRUCT].size_1word > 0)
    {
      offset = info_ptr->parameter_size;
      info_ptr->header_size = 4 * UNITS_PER_WORD;
      info_ptr->total_size += 4 * UNITS_PER_WORD;

      /* Calculate the offsets to save normal register pairs.  */
      for (range = 0; range < STACK_REGS_MAX; range++)
	{
	  frv_stack_regs_t *reg_ptr = &(info_ptr->regs[range]);
	  if (! reg_ptr->special_p)
	    {
	      int first = reg_ptr->first;
	      int last = reg_ptr->last;
	      int regno;

	      for (regno = first; regno <= last; regno++)
		if (info_ptr->save_p[regno] == REG_SAVE_2WORDS
		    && regno != FRAME_POINTER_REGNUM
		    && (regno < FIRST_ARG_REGNUM
			|| regno > LAST_ARG_REGNUM))
		  {
		    info_ptr->reg_offset[regno] = offset;
		    offset += 2 * UNITS_PER_WORD;
		  }
	    }
	}

      /* Calculate the offsets to save normal single registers.  */
      for (range = 0; range < STACK_REGS_MAX; range++)
	{
	  frv_stack_regs_t *reg_ptr = &(info_ptr->regs[range]);
	  if (! reg_ptr->special_p)
	    {
	      int first = reg_ptr->first;
	      int last = reg_ptr->last;
	      int regno;

	      for (regno = first; regno <= last; regno++)
		if (info_ptr->save_p[regno] == REG_SAVE_1WORD
		    && regno != FRAME_POINTER_REGNUM
		    && (regno < FIRST_ARG_REGNUM
			|| regno > LAST_ARG_REGNUM))
		  {
		    info_ptr->reg_offset[regno] = offset;
		    offset += UNITS_PER_WORD;
		  }
	    }
	}

      /* Calculate the offset to save the local variables at.  */
      offset = ADDR_ALIGN (offset, alignment);
      if (info_ptr->vars_size)
	{
	  info_ptr->vars_offset = offset;
	  offset += info_ptr->vars_size;
	}

      /* Align header to a dword-boundary.  */
      offset = ADDR_ALIGN (offset, 2 * UNITS_PER_WORD);

      /* Calculate the offsets in the fixed frame.  */
      info_ptr->save_p[FRAME_POINTER_REGNUM] = REG_SAVE_1WORD;
      info_ptr->reg_offset[FRAME_POINTER_REGNUM] = offset;
      info_ptr->regs[STACK_REGS_FP].size_1word = UNITS_PER_WORD;

      info_ptr->save_p[LR_REGNO] = REG_SAVE_1WORD;
      info_ptr->reg_offset[LR_REGNO] = offset + 2*UNITS_PER_WORD;
      info_ptr->regs[STACK_REGS_LR].size_1word = UNITS_PER_WORD;

      if (cfun->returns_struct)
	{
	  info_ptr->save_p[FRV_STRUCT_VALUE_REGNUM] = REG_SAVE_1WORD;
	  info_ptr->reg_offset[FRV_STRUCT_VALUE_REGNUM] = offset + UNITS_PER_WORD;
	  info_ptr->regs[STACK_REGS_STRUCT].size_1word = UNITS_PER_WORD;
	}

      /* Calculate the offsets to store the arguments passed in registers
         for stdarg functions.  The register pairs are first and the single
         register if any is last.  The register save area starts on a
         dword-boundary.  */
      if (info_ptr->stdarg_size)
	{
	  int first = info_ptr->regs[STACK_REGS_STDARG].first;
	  int last  = info_ptr->regs[STACK_REGS_STDARG].last;
	  int regno;

	  /* Skip the header.  */
	  offset += 4 * UNITS_PER_WORD;
	  for (regno = first; regno <= last; regno++)
	    {
	      if (info_ptr->save_p[regno] == REG_SAVE_2WORDS)
		{
		  info_ptr->reg_offset[regno] = offset;
		  offset += 2 * UNITS_PER_WORD;
		}
	      else if (info_ptr->save_p[regno] == REG_SAVE_1WORD)
		{
		  info_ptr->reg_offset[regno] = offset;
		  offset += UNITS_PER_WORD;
		}
	    }
	}
    }

  if (reload_completed)
    frv_stack_cache = info_ptr;

  return info_ptr;
}


/* Print the information about the frv stack offsets, etc. when debugging.  */

void
frv_debug_stack (frv_stack_t *info)
{
  int range;

  if (!info)
    info = frv_stack_info ();

  fprintf (stderr, "\nStack information for function %s:\n",
	   ((current_function_decl && DECL_NAME (current_function_decl))
	    ? IDENTIFIER_POINTER (DECL_NAME (current_function_decl))
	    : "<unknown>"));

  fprintf (stderr, "\ttotal_size\t= %6d\n", info->total_size);
  fprintf (stderr, "\tvars_size\t= %6d\n", info->vars_size);
  fprintf (stderr, "\tparam_size\t= %6d\n", info->parameter_size);
  fprintf (stderr, "\tregs_size\t= %6d, 1w = %3d, 2w = %3d\n",
	   info->regs_size, info->regs_size_1word, info->regs_size_2words);

  fprintf (stderr, "\theader_size\t= %6d\n", info->header_size);
  fprintf (stderr, "\tpretend_size\t= %6d\n", info->pretend_size);
  fprintf (stderr, "\tvars_offset\t= %6d\n", info->vars_offset);
  fprintf (stderr, "\tregs_offset\t= %6d\n", info->regs_offset);

  for (range = 0; range < STACK_REGS_MAX; range++)
    {
      frv_stack_regs_t *regs = &(info->regs[range]);
      if ((regs->size_1word + regs->size_2words) > 0)
	{
	  int first = regs->first;
	  int last  = regs->last;
	  int regno;

	  fprintf (stderr, "\t%s\tsize\t= %6d, 1w = %3d, 2w = %3d, save =",
		   regs->name, regs->size_1word + regs->size_2words,
		   regs->size_1word, regs->size_2words);

	  for (regno = first; regno <= last; regno++)
	    {
	      if (info->save_p[regno] == REG_SAVE_1WORD)
		fprintf (stderr, " %s (%d)", reg_names[regno],
			 info->reg_offset[regno]);

	      else if (info->save_p[regno] == REG_SAVE_2WORDS)
		fprintf (stderr, " %s-%s (%d)", reg_names[regno],
			 reg_names[regno+1], info->reg_offset[regno]);
	    }

	  fputc ('\n', stderr);
	}
    }

  fflush (stderr);
}




/* Used during final to control the packing of insns.  The value is
   1 if the current instruction should be packed with the next one,
   0 if it shouldn't or -1 if packing is disabled altogether.  */

static int frv_insn_packing_flag;

/* True if the current function contains a far jump.  */

static int
frv_function_contains_far_jump (void)
{
  rtx insn = get_insns ();
  while (insn != NULL
	 && !(GET_CODE (insn) == JUMP_INSN
	      /* Ignore tablejump patterns.  */
	      && GET_CODE (PATTERN (insn)) != ADDR_VEC
	      && GET_CODE (PATTERN (insn)) != ADDR_DIFF_VEC
	      && get_attr_far_jump (insn) == FAR_JUMP_YES))
    insn = NEXT_INSN (insn);
  return (insn != NULL);
}

/* For the FRV, this function makes sure that a function with far jumps
   will return correctly.  It also does the VLIW packing.  */

static void
frv_function_prologue (FILE *file, HOST_WIDE_INT size ATTRIBUTE_UNUSED)
{
  /* If no frame was created, check whether the function uses a call
     instruction to implement a far jump.  If so, save the link in gr3 and
     replace all returns to LR with returns to GR3.  GR3 is used because it
     is call-clobbered, because is not available to the register allocator,
     and because all functions that take a hidden argument pointer will have
     a stack frame.  */
  if (frv_stack_info ()->total_size == 0 && frv_function_contains_far_jump ())
    {
      rtx insn;

      /* Just to check that the above comment is true.  */
      gcc_assert (!regs_ever_live[GPR_FIRST + 3]);

      /* Generate the instruction that saves the link register.  */
      fprintf (file, "\tmovsg lr,gr3\n");

      /* Replace the LR with GR3 in *return_internal patterns.  The insn
	 will now return using jmpl @(gr3,0) rather than bralr.  We cannot
	 simply emit a different assembly directive because bralr and jmpl
	 execute in different units.  */
      for (insn = get_insns(); insn != NULL; insn = NEXT_INSN (insn))
	if (GET_CODE (insn) == JUMP_INSN)
	  {
	    rtx pattern = PATTERN (insn);
	    if (GET_CODE (pattern) == PARALLEL
		&& XVECLEN (pattern, 0) >= 2
		&& GET_CODE (XVECEXP (pattern, 0, 0)) == RETURN
		&& GET_CODE (XVECEXP (pattern, 0, 1)) == USE)
	      {
		rtx address = XEXP (XVECEXP (pattern, 0, 1), 0);
		if (GET_CODE (address) == REG && REGNO (address) == LR_REGNO)
		  REGNO (address) = GPR_FIRST + 3;
	      }
	  }
    }

  frv_pack_insns ();

  /* Allow the garbage collector to free the nops created by frv_reorg.  */
  memset (frv_nops, 0, sizeof (frv_nops));
}


/* Return the next available temporary register in a given class.  */

static rtx
frv_alloc_temp_reg (
     frv_tmp_reg_t *info,	/* which registers are available */
     enum reg_class class,	/* register class desired */
     enum machine_mode mode,	/* mode to allocate register with */
     int mark_as_used,		/* register not available after allocation */
     int no_abort)		/* return NULL instead of aborting */
{
  int regno = info->next_reg[ (int)class ];
  int orig_regno = regno;
  HARD_REG_SET *reg_in_class = &reg_class_contents[ (int)class ];
  int i, nr;

  for (;;)
    {
      if (TEST_HARD_REG_BIT (*reg_in_class, regno)
	  && TEST_HARD_REG_BIT (info->regs, regno))
	  break;

      if (++regno >= FIRST_PSEUDO_REGISTER)
	regno = 0;
      if (regno == orig_regno)
	{
	  gcc_assert (no_abort);
	  return NULL_RTX;
	}
    }

  nr = HARD_REGNO_NREGS (regno, mode);
  info->next_reg[ (int)class ] = regno + nr;

  if (mark_as_used)
    for (i = 0; i < nr; i++)
      CLEAR_HARD_REG_BIT (info->regs, regno+i);

  return gen_rtx_REG (mode, regno);
}


/* Return an rtx with the value OFFSET, which will either be a register or a
   signed 12-bit integer.  It can be used as the second operand in an "add"
   instruction, or as the index in a load or store.

   The function returns a constant rtx if OFFSET is small enough, otherwise
   it loads the constant into register OFFSET_REGNO and returns that.  */
static rtx
frv_frame_offset_rtx (int offset)
{
  rtx offset_rtx = GEN_INT (offset);
  if (IN_RANGE_P (offset, -2048, 2047))
    return offset_rtx;
  else
    {
      rtx reg_rtx = gen_rtx_REG (SImode, OFFSET_REGNO);
      if (IN_RANGE_P (offset, -32768, 32767))
	emit_insn (gen_movsi (reg_rtx, offset_rtx));
      else
	{
	  emit_insn (gen_movsi_high (reg_rtx, offset_rtx));
	  emit_insn (gen_movsi_lo_sum (reg_rtx, offset_rtx));
	}
      return reg_rtx;
    }
}

/* Generate (mem:MODE (plus:Pmode BASE (frv_frame_offset OFFSET)))).  The
   prologue and epilogue uses such expressions to access the stack.  */
static rtx
frv_frame_mem (enum machine_mode mode, rtx base, int offset)
{
  return gen_rtx_MEM (mode, gen_rtx_PLUS (Pmode,
					  base,
					  frv_frame_offset_rtx (offset)));
}

/* Generate a frame-related expression:

	(set REG (mem (plus (sp) (const_int OFFSET)))).

   Such expressions are used in FRAME_RELATED_EXPR notes for more complex
   instructions.  Marking the expressions as frame-related is superfluous if
   the note contains just a single set.  But if the note contains a PARALLEL
   or SEQUENCE that has several sets, each set must be individually marked
   as frame-related.  */
static rtx
frv_dwarf_store (rtx reg, int offset)
{
  rtx set = gen_rtx_SET (VOIDmode,
			 gen_rtx_MEM (GET_MODE (reg),
				      plus_constant (stack_pointer_rtx,
						     offset)),
			 reg);
  RTX_FRAME_RELATED_P (set) = 1;
  return set;
}

/* Emit a frame-related instruction whose pattern is PATTERN.  The
   instruction is the last in a sequence that cumulatively performs the
   operation described by DWARF_PATTERN.  The instruction is marked as
   frame-related and has a REG_FRAME_RELATED_EXPR note containing
   DWARF_PATTERN.  */
static void
frv_frame_insn (rtx pattern, rtx dwarf_pattern)
{
  rtx insn = emit_insn (pattern);
  RTX_FRAME_RELATED_P (insn) = 1;
  REG_NOTES (insn) = alloc_EXPR_LIST (REG_FRAME_RELATED_EXPR,
				      dwarf_pattern,
				      REG_NOTES (insn));
}

/* Emit instructions that transfer REG to or from the memory location (sp +
   STACK_OFFSET).  The register is stored in memory if ACCESSOR->OP is
   FRV_STORE and loaded if it is FRV_LOAD.  Only the prologue uses this
   function to store registers and only the epilogue uses it to load them.

   The caller sets up ACCESSOR so that BASE is equal to (sp + BASE_OFFSET).
   The generated instruction will use BASE as its base register.  BASE may
   simply be the stack pointer, but if several accesses are being made to a
   region far away from the stack pointer, it may be more efficient to set
   up a temporary instead.

   Store instructions will be frame-related and will be annotated with the
   overall effect of the store.  Load instructions will be followed by a
   (use) to prevent later optimizations from zapping them.

   The function takes care of the moves to and from SPRs, using TEMP_REGNO
   as a temporary in such cases.  */
static void
frv_frame_access (frv_frame_accessor_t *accessor, rtx reg, int stack_offset)
{
  enum machine_mode mode = GET_MODE (reg);
  rtx mem = frv_frame_mem (mode,
			   accessor->base,
			   stack_offset - accessor->base_offset);

  if (accessor->op == FRV_LOAD)
    {
      if (SPR_P (REGNO (reg)))
	{
	  rtx temp = gen_rtx_REG (mode, TEMP_REGNO);
	  emit_insn (gen_rtx_SET (VOIDmode, temp, mem));
	  emit_insn (gen_rtx_SET (VOIDmode, reg, temp));
	}
      else
	emit_insn (gen_rtx_SET (VOIDmode, reg, mem));
      emit_insn (gen_rtx_USE (VOIDmode, reg));
    }
  else
    {
      if (SPR_P (REGNO (reg)))
	{
	  rtx temp = gen_rtx_REG (mode, TEMP_REGNO);
	  emit_insn (gen_rtx_SET (VOIDmode, temp, reg));
	  frv_frame_insn (gen_rtx_SET (Pmode, mem, temp),
			  frv_dwarf_store (reg, stack_offset));
	}
      else if (GET_MODE (reg) == DImode)
	{
	  /* For DImode saves, the dwarf2 version needs to be a SEQUENCE
	     with a separate save for each register.  */
	  rtx reg1 = gen_rtx_REG (SImode, REGNO (reg));
	  rtx reg2 = gen_rtx_REG (SImode, REGNO (reg) + 1);
	  rtx set1 = frv_dwarf_store (reg1, stack_offset);
	  rtx set2 = frv_dwarf_store (reg2, stack_offset + 4);
	  frv_frame_insn (gen_rtx_SET (Pmode, mem, reg),
			  gen_rtx_PARALLEL (VOIDmode,
					    gen_rtvec (2, set1, set2)));
	}
      else
	frv_frame_insn (gen_rtx_SET (Pmode, mem, reg),
			frv_dwarf_store (reg, stack_offset));
    }
}

/* A function that uses frv_frame_access to transfer a group of registers to
   or from the stack.  ACCESSOR is passed directly to frv_frame_access, INFO
   is the stack information generated by frv_stack_info, and REG_SET is the
   number of the register set to transfer.  */
static void
frv_frame_access_multi (frv_frame_accessor_t *accessor,
                        frv_stack_t *info,
                        int reg_set)
{
  frv_stack_regs_t *regs_info;
  int regno;

  regs_info = &info->regs[reg_set];
  for (regno = regs_info->first; regno <= regs_info->last; regno++)
    if (info->save_p[regno])
      frv_frame_access (accessor,
			info->save_p[regno] == REG_SAVE_2WORDS
			? gen_rtx_REG (DImode, regno)
			: gen_rtx_REG (SImode, regno),
			info->reg_offset[regno]);
}

/* Save or restore callee-saved registers that are kept outside the frame
   header.  The function saves the registers if OP is FRV_STORE and restores
   them if OP is FRV_LOAD.  INFO is the stack information generated by
   frv_stack_info.  */
static void
frv_frame_access_standard_regs (enum frv_stack_op op, frv_stack_t *info)
{
  frv_frame_accessor_t accessor;

  accessor.op = op;
  accessor.base = stack_pointer_rtx;
  accessor.base_offset = 0;
  frv_frame_access_multi (&accessor, info, STACK_REGS_GPR);
  frv_frame_access_multi (&accessor, info, STACK_REGS_FPR);
  frv_frame_access_multi (&accessor, info, STACK_REGS_LCR);
}


/* Called after register allocation to add any instructions needed for the
   prologue.  Using a prologue insn is favored compared to putting all of the
   instructions in the TARGET_ASM_FUNCTION_PROLOGUE target hook, since
   it allows the scheduler to intermix instructions with the saves of
   the caller saved registers.  In some cases, it might be necessary
   to emit a barrier instruction as the last insn to prevent such
   scheduling.

   Also any insns generated here should have RTX_FRAME_RELATED_P(insn) = 1
   so that the debug info generation code can handle them properly.  */
void
frv_expand_prologue (void)
{
  frv_stack_t *info = frv_stack_info ();
  rtx sp = stack_pointer_rtx;
  rtx fp = frame_pointer_rtx;
  frv_frame_accessor_t accessor;

  if (TARGET_DEBUG_STACK)
    frv_debug_stack (info);

  if (info->total_size == 0)
    return;

  /* We're interested in three areas of the frame here:

         A: the register save area
	 B: the old FP
	 C: the header after B

     If the frame pointer isn't used, we'll have to set up A, B and C
     using the stack pointer.  If the frame pointer is used, we'll access
     them as follows:

         A: set up using sp
	 B: set up using sp or a temporary (see below)
	 C: set up using fp

     We set up B using the stack pointer if the frame is small enough.
     Otherwise, it's more efficient to copy the old stack pointer into a
     temporary and use that.

     Note that it's important to make sure the prologue and epilogue use the
     same registers to access A and C, since doing otherwise will confuse
     the aliasing code.  */

  /* Set up ACCESSOR for accessing region B above.  If the frame pointer
     isn't used, the same method will serve for C.  */
  accessor.op = FRV_STORE;
  if (frame_pointer_needed && info->total_size > 2048)
    {
      rtx insn;

      accessor.base = gen_rtx_REG (Pmode, OLD_SP_REGNO);
      accessor.base_offset = info->total_size;
      insn = emit_insn (gen_movsi (accessor.base, sp));
    }
  else
    {
      accessor.base = stack_pointer_rtx;
      accessor.base_offset = 0;
    }

  /* Allocate the stack space.  */
  {
    rtx asm_offset = frv_frame_offset_rtx (-info->total_size);
    rtx dwarf_offset = GEN_INT (-info->total_size);

    frv_frame_insn (gen_stack_adjust (sp, sp, asm_offset),
		    gen_rtx_SET (Pmode,
				 sp,
				 gen_rtx_PLUS (Pmode, sp, dwarf_offset)));
  }

  /* If the frame pointer is needed, store the old one at (sp + FP_OFFSET)
     and point the new one to that location.  */
  if (frame_pointer_needed)
    {
      int fp_offset = info->reg_offset[FRAME_POINTER_REGNUM];

      /* ASM_SRC and DWARF_SRC both point to the frame header.  ASM_SRC is
	 based on ACCESSOR.BASE but DWARF_SRC is always based on the stack
	 pointer.  */
      rtx asm_src = plus_constant (accessor.base,
				   fp_offset - accessor.base_offset);
      rtx dwarf_src = plus_constant (sp, fp_offset);

      /* Store the old frame pointer at (sp + FP_OFFSET).  */
      frv_frame_access (&accessor, fp, fp_offset);

      /* Set up the new frame pointer.  */
      frv_frame_insn (gen_rtx_SET (VOIDmode, fp, asm_src),
		      gen_rtx_SET (VOIDmode, fp, dwarf_src));

      /* Access region C from the frame pointer.  */
      accessor.base = fp;
      accessor.base_offset = fp_offset;
    }

  /* Set up region C.  */
  frv_frame_access_multi (&accessor, info, STACK_REGS_STRUCT);
  frv_frame_access_multi (&accessor, info, STACK_REGS_LR);
  frv_frame_access_multi (&accessor, info, STACK_REGS_STDARG);

  /* Set up region A.  */
  frv_frame_access_standard_regs (FRV_STORE, info);

  /* If this is a varargs/stdarg function, issue a blockage to prevent the
     scheduler from moving loads before the stores saving the registers.  */
  if (info->stdarg_size > 0)
    emit_insn (gen_blockage ());

  /* Set up pic register/small data register for this function.  */
  if (!TARGET_FDPIC && flag_pic && cfun->uses_pic_offset_table)
    emit_insn (gen_pic_prologue (gen_rtx_REG (Pmode, PIC_REGNO),
				 gen_rtx_REG (Pmode, LR_REGNO),
				 gen_rtx_REG (SImode, OFFSET_REGNO)));
}


/* Under frv, all of the work is done via frv_expand_epilogue, but
   this function provides a convenient place to do cleanup.  */

static void
frv_function_epilogue (FILE *file ATTRIBUTE_UNUSED,
                       HOST_WIDE_INT size ATTRIBUTE_UNUSED)
{
  frv_stack_cache = (frv_stack_t *)0;

  /* Zap last used registers for conditional execution.  */
  memset (&frv_ifcvt.tmp_reg, 0, sizeof (frv_ifcvt.tmp_reg));

  /* Release the bitmap of created insns.  */
  BITMAP_FREE (frv_ifcvt.scratch_insns_bitmap);
}


/* Called after register allocation to add any instructions needed for the
   epilogue.  Using an epilogue insn is favored compared to putting all of the
   instructions in the TARGET_ASM_FUNCTION_PROLOGUE target hook, since
   it allows the scheduler to intermix instructions with the saves of
   the caller saved registers.  In some cases, it might be necessary
   to emit a barrier instruction as the last insn to prevent such
   scheduling.  */

void
frv_expand_epilogue (bool emit_return)
{
  frv_stack_t *info = frv_stack_info ();
  rtx fp = frame_pointer_rtx;
  rtx sp = stack_pointer_rtx;
  rtx return_addr;
  int fp_offset;

  fp_offset = info->reg_offset[FRAME_POINTER_REGNUM];

  /* Restore the stack pointer to its original value if alloca or the like
     is used.  */
  if (! current_function_sp_is_unchanging)
    emit_insn (gen_addsi3 (sp, fp, frv_frame_offset_rtx (-fp_offset)));

  /* Restore the callee-saved registers that were used in this function.  */
  frv_frame_access_standard_regs (FRV_LOAD, info);

  /* Set RETURN_ADDR to the address we should return to.  Set it to NULL if
     no return instruction should be emitted.  */
  if (info->save_p[LR_REGNO])
    {
      int lr_offset;
      rtx mem;

      /* Use the same method to access the link register's slot as we did in
	 the prologue.  In other words, use the frame pointer if available,
	 otherwise use the stack pointer.

	 LR_OFFSET is the offset of the link register's slot from the start
	 of the frame and MEM is a memory rtx for it.  */
      lr_offset = info->reg_offset[LR_REGNO];
      if (frame_pointer_needed)
	mem = frv_frame_mem (Pmode, fp, lr_offset - fp_offset);
      else
	mem = frv_frame_mem (Pmode, sp, lr_offset);

      /* Load the old link register into a GPR.  */
      return_addr = gen_rtx_REG (Pmode, TEMP_REGNO);
      emit_insn (gen_rtx_SET (VOIDmode, return_addr, mem));
    }
  else
    return_addr = gen_rtx_REG (Pmode, LR_REGNO);

  /* Restore the old frame pointer.  Emit a USE afterwards to make sure
     the load is preserved.  */
  if (frame_pointer_needed)
    {
      emit_insn (gen_rtx_SET (VOIDmode, fp, gen_rtx_MEM (Pmode, fp)));
      emit_insn (gen_rtx_USE (VOIDmode, fp));
    }

  /* Deallocate the stack frame.  */
  if (info->total_size != 0)
    {
      rtx offset = frv_frame_offset_rtx (info->total_size);
      emit_insn (gen_stack_adjust (sp, sp, offset));
    }

  /* If this function uses eh_return, add the final stack adjustment now.  */
  if (current_function_calls_eh_return)
    emit_insn (gen_stack_adjust (sp, sp, EH_RETURN_STACKADJ_RTX));

  if (emit_return)
    emit_jump_insn (gen_epilogue_return (return_addr));
  else
    {
      rtx lr = return_addr;

      if (REGNO (return_addr) != LR_REGNO)
	{
	  lr = gen_rtx_REG (Pmode, LR_REGNO);
	  emit_move_insn (lr, return_addr);
	}

      emit_insn (gen_rtx_USE (VOIDmode, lr));
    }
}


/* Worker function for TARGET_ASM_OUTPUT_MI_THUNK.  */

static void
frv_asm_output_mi_thunk (FILE *file,
                         tree thunk_fndecl ATTRIBUTE_UNUSED,
                         HOST_WIDE_INT delta,
                         HOST_WIDE_INT vcall_offset ATTRIBUTE_UNUSED,
                         tree function)
{
  const char *name_func = XSTR (XEXP (DECL_RTL (function), 0), 0);
  const char *name_arg0 = reg_names[FIRST_ARG_REGNUM];
  const char *name_jmp = reg_names[JUMP_REGNO];
  const char *parallel = (frv_issue_rate () > 1 ? ".p" : "");

  /* Do the add using an addi if possible.  */
  if (IN_RANGE_P (delta, -2048, 2047))
    fprintf (file, "\taddi %s,#%d,%s\n", name_arg0, (int) delta, name_arg0);
  else
    {
      const char *const name_add = reg_names[TEMP_REGNO];
      fprintf (file, "\tsethi%s #hi(" HOST_WIDE_INT_PRINT_DEC "),%s\n",
	       parallel, delta, name_add);
      fprintf (file, "\tsetlo #lo(" HOST_WIDE_INT_PRINT_DEC "),%s\n",
	       delta, name_add);
      fprintf (file, "\tadd %s,%s,%s\n", name_add, name_arg0, name_arg0);
    }

  if (TARGET_FDPIC)
    {
      const char *name_pic = reg_names[FDPIC_REGNO];
      name_jmp = reg_names[FDPIC_FPTR_REGNO];

      if (flag_pic != 1)
	{
	  fprintf (file, "\tsethi%s #gotofffuncdeschi(", parallel);
	  assemble_name (file, name_func);
	  fprintf (file, "),%s\n", name_jmp);

	  fprintf (file, "\tsetlo #gotofffuncdesclo(");
	  assemble_name (file, name_func);
	  fprintf (file, "),%s\n", name_jmp);

	  fprintf (file, "\tldd @(%s,%s), %s\n", name_jmp, name_pic, name_jmp);
	}
      else
	{
	  fprintf (file, "\tlddo @(%s,#gotofffuncdesc12(", name_pic);
	  assemble_name (file, name_func);
	  fprintf (file, "\t)), %s\n", name_jmp);
	}
    }
  else if (!flag_pic)
    {
      fprintf (file, "\tsethi%s #hi(", parallel);
      assemble_name (file, name_func);
      fprintf (file, "),%s\n", name_jmp);

      fprintf (file, "\tsetlo #lo(");
      assemble_name (file, name_func);
      fprintf (file, "),%s\n", name_jmp);
    }
  else
    {
      /* Use JUMP_REGNO as a temporary PIC register.  */
      const char *name_lr = reg_names[LR_REGNO];
      const char *name_gppic = name_jmp;
      const char *name_tmp = reg_names[TEMP_REGNO];

      fprintf (file, "\tmovsg %s,%s\n", name_lr, name_tmp);
      fprintf (file, "\tcall 1f\n");
      fprintf (file, "1:\tmovsg %s,%s\n", name_lr, name_gppic);
      fprintf (file, "\tmovgs %s,%s\n", name_tmp, name_lr);
      fprintf (file, "\tsethi%s #gprelhi(1b),%s\n", parallel, name_tmp);
      fprintf (file, "\tsetlo #gprello(1b),%s\n", name_tmp);
      fprintf (file, "\tsub %s,%s,%s\n", name_gppic, name_tmp, name_gppic);

      fprintf (file, "\tsethi%s #gprelhi(", parallel);
      assemble_name (file, name_func);
      fprintf (file, "),%s\n", name_tmp);

      fprintf (file, "\tsetlo #gprello(");
      assemble_name (file, name_func);
      fprintf (file, "),%s\n", name_tmp);

      fprintf (file, "\tadd %s,%s,%s\n", name_gppic, name_tmp, name_jmp);
    }

  /* Jump to the function address.  */
  fprintf (file, "\tjmpl @(%s,%s)\n", name_jmp, reg_names[GPR_FIRST+0]);
}


/* A C expression which is nonzero if a function must have and use a frame
   pointer.  This expression is evaluated in the reload pass.  If its value is
   nonzero the function will have a frame pointer.

   The expression can in principle examine the current function and decide
   according to the facts, but on most machines the constant 0 or the constant
   1 suffices.  Use 0 when the machine allows code to be generated with no
   frame pointer, and doing so saves some time or space.  Use 1 when there is
   no possible advantage to avoiding a frame pointer.

   In certain cases, the compiler does not know how to produce valid code
   without a frame pointer.  The compiler recognizes those cases and
   automatically gives the function a frame pointer regardless of what
   `FRAME_POINTER_REQUIRED' says.  You don't need to worry about them.

   In a function that does not require a frame pointer, the frame pointer
   register can be allocated for ordinary usage, unless you mark it as a fixed
   register.  See `FIXED_REGISTERS' for more information.  */

/* On frv, create a frame whenever we need to create stack.  */

int
frv_frame_pointer_required (void)
{
  /* If we forgoing the usual linkage requirements, we only need
     a frame pointer if the stack pointer might change.  */
  if (!TARGET_LINKED_FP)
    return !current_function_sp_is_unchanging;

  if (! current_function_is_leaf)
    return TRUE;

  if (get_frame_size () != 0)
    return TRUE;

  if (cfun->stdarg)
    return TRUE;

  if (!current_function_sp_is_unchanging)
    return TRUE;

  if (!TARGET_FDPIC && flag_pic && cfun->uses_pic_offset_table)
    return TRUE;

  if (profile_flag)
    return TRUE;

  if (cfun->machine->frame_needed)
    return TRUE;

  return FALSE;
}


/* This macro is similar to `INITIAL_FRAME_POINTER_OFFSET'.  It specifies the
   initial difference between the specified pair of registers.  This macro must
   be defined if `ELIMINABLE_REGS' is defined.  */

/* See frv_stack_info for more details on the frv stack frame.  */

int
frv_initial_elimination_offset (int from, int to)
{
  frv_stack_t *info = frv_stack_info ();
  int ret = 0;

  if (to == STACK_POINTER_REGNUM && from == ARG_POINTER_REGNUM)
    ret = info->total_size - info->pretend_size;

  else if (to == STACK_POINTER_REGNUM && from == FRAME_POINTER_REGNUM)
    ret = info->reg_offset[FRAME_POINTER_REGNUM];

  else if (to == FRAME_POINTER_REGNUM && from == ARG_POINTER_REGNUM)
    ret = (info->total_size
	   - info->reg_offset[FRAME_POINTER_REGNUM]
	   - info->pretend_size);

  else
    gcc_unreachable ();

  if (TARGET_DEBUG_STACK)
    fprintf (stderr, "Eliminate %s to %s by adding %d\n",
	     reg_names [from], reg_names[to], ret);

  return ret;
}


/* Worker function for TARGET_SETUP_INCOMING_VARARGS.  */

static void
frv_setup_incoming_varargs (CUMULATIVE_ARGS *cum,
                            enum machine_mode mode,
                            tree type ATTRIBUTE_UNUSED,
                            int *pretend_size,
                            int second_time)
{
  if (TARGET_DEBUG_ARG)
    fprintf (stderr,
	     "setup_vararg: words = %2d, mode = %4s, pretend_size = %d, second_time = %d\n",
	     *cum, GET_MODE_NAME (mode), *pretend_size, second_time);
}


/* Worker function for TARGET_EXPAND_BUILTIN_SAVEREGS.  */

static rtx
frv_expand_builtin_saveregs (void)
{
  int offset = UNITS_PER_WORD * FRV_NUM_ARG_REGS;

  if (TARGET_DEBUG_ARG)
    fprintf (stderr, "expand_builtin_saveregs: offset from ap = %d\n",
	     offset);

  return gen_rtx_PLUS (Pmode, virtual_incoming_args_rtx, GEN_INT (- offset));
}


/* Expand __builtin_va_start to do the va_start macro.  */

void
frv_expand_builtin_va_start (tree valist, rtx nextarg)
{
  tree t;
  int num = cfun->args_info - FIRST_ARG_REGNUM - FRV_NUM_ARG_REGS;

  nextarg = gen_rtx_PLUS (Pmode, virtual_incoming_args_rtx,
			  GEN_INT (UNITS_PER_WORD * num));

  if (TARGET_DEBUG_ARG)
    {
      fprintf (stderr, "va_start: args_info = %d, num = %d\n",
	       cfun->args_info, num);

      debug_rtx (nextarg);
    }

  t = build2 (MODIFY_EXPR, TREE_TYPE (valist), valist,
	      make_tree (ptr_type_node, nextarg));
  TREE_SIDE_EFFECTS (t) = 1;

  expand_expr (t, const0_rtx, VOIDmode, EXPAND_NORMAL);
}


/* Expand a block move operation, and return 1 if successful.  Return 0
   if we should let the compiler generate normal code.

   operands[0] is the destination
   operands[1] is the source
   operands[2] is the length
   operands[3] is the alignment */

/* Maximum number of loads to do before doing the stores */
#ifndef MAX_MOVE_REG
#define MAX_MOVE_REG 4
#endif

/* Maximum number of total loads to do.  */
#ifndef TOTAL_MOVE_REG
#define TOTAL_MOVE_REG 8
#endif

int
frv_expand_block_move (rtx operands[])
{
  rtx orig_dest = operands[0];
  rtx orig_src	= operands[1];
  rtx bytes_rtx	= operands[2];
  rtx align_rtx = operands[3];
  int constp	= (GET_CODE (bytes_rtx) == CONST_INT);
  int align;
  int bytes;
  int offset;
  int num_reg;
  int i;
  rtx src_reg;
  rtx dest_reg;
  rtx src_addr;
  rtx dest_addr;
  rtx src_mem;
  rtx dest_mem;
  rtx tmp_reg;
  rtx stores[MAX_MOVE_REG];
  int move_bytes;
  enum machine_mode mode;

  /* If this is not a fixed size move, just call memcpy.  */
  if (! constp)
    return FALSE;

  /* This should be a fixed size alignment.  */
  gcc_assert (GET_CODE (align_rtx) == CONST_INT);

  align = INTVAL (align_rtx);

  /* Anything to move? */
  bytes = INTVAL (bytes_rtx);
  if (bytes <= 0)
    return TRUE;

  /* Don't support real large moves.  */
  if (bytes > TOTAL_MOVE_REG*align)
    return FALSE;

  /* Move the address into scratch registers.  */
  dest_reg = copy_addr_to_reg (XEXP (orig_dest, 0));
  src_reg  = copy_addr_to_reg (XEXP (orig_src,  0));

  num_reg = offset = 0;
  for ( ; bytes > 0; (bytes -= move_bytes), (offset += move_bytes))
    {
      /* Calculate the correct offset for src/dest.  */
      if (offset == 0)
	{
	  src_addr  = src_reg;
	  dest_addr = dest_reg;
	}
      else
	{
	  src_addr = plus_constant (src_reg, offset);
	  dest_addr = plus_constant (dest_reg, offset);
	}

      /* Generate the appropriate load and store, saving the stores
	 for later.  */
      if (bytes >= 4 && align >= 4)
	mode = SImode;
      else if (bytes >= 2 && align >= 2)
	mode = HImode;
      else
	mode = QImode;

      move_bytes = GET_MODE_SIZE (mode);
      tmp_reg = gen_reg_rtx (mode);
      src_mem = change_address (orig_src, mode, src_addr);
      dest_mem = change_address (orig_dest, mode, dest_addr);
      emit_insn (gen_rtx_SET (VOIDmode, tmp_reg, src_mem));
      stores[num_reg++] = gen_rtx_SET (VOIDmode, dest_mem, tmp_reg);

      if (num_reg >= MAX_MOVE_REG)
	{
	  for (i = 0; i < num_reg; i++)
	    emit_insn (stores[i]);
	  num_reg = 0;
	}
    }

  for (i = 0; i < num_reg; i++)
    emit_insn (stores[i]);

  return TRUE;
}


/* Expand a block clear operation, and return 1 if successful.  Return 0
   if we should let the compiler generate normal code.

   operands[0] is the destination
   operands[1] is the length
   operands[3] is the alignment */

int
frv_expand_block_clear (rtx operands[])
{
  rtx orig_dest = operands[0];
  rtx bytes_rtx	= operands[1];
  rtx align_rtx = operands[3];
  int constp	= (GET_CODE (bytes_rtx) == CONST_INT);
  int align;
  int bytes;
  int offset;
  int num_reg;
  rtx dest_reg;
  rtx dest_addr;
  rtx dest_mem;
  int clear_bytes;
  enum machine_mode mode;

  /* If this is not a fixed size move, just call memcpy.  */
  if (! constp)
    return FALSE;

  /* This should be a fixed size alignment.  */
  gcc_assert (GET_CODE (align_rtx) == CONST_INT);

  align = INTVAL (align_rtx);

  /* Anything to move? */
  bytes = INTVAL (bytes_rtx);
  if (bytes <= 0)
    return TRUE;

  /* Don't support real large clears.  */
  if (bytes > TOTAL_MOVE_REG*align)
    return FALSE;

  /* Move the address into a scratch register.  */
  dest_reg = copy_addr_to_reg (XEXP (orig_dest, 0));

  num_reg = offset = 0;
  for ( ; bytes > 0; (bytes -= clear_bytes), (offset += clear_bytes))
    {
      /* Calculate the correct offset for src/dest.  */
      dest_addr = ((offset == 0)
		   ? dest_reg
		   : plus_constant (dest_reg, offset));

      /* Generate the appropriate store of gr0.  */
      if (bytes >= 4 && align >= 4)
	mode = SImode;
      else if (bytes >= 2 && align >= 2)
	mode = HImode;
      else
	mode = QImode;

      clear_bytes = GET_MODE_SIZE (mode);
      dest_mem = change_address (orig_dest, mode, dest_addr);
      emit_insn (gen_rtx_SET (VOIDmode, dest_mem, const0_rtx));
    }

  return TRUE;
}


/* The following variable is used to output modifiers of assembler
   code of the current output insn.  */

static rtx *frv_insn_operands;

/* The following function is used to add assembler insn code suffix .p
   if it is necessary.  */

const char *
frv_asm_output_opcode (FILE *f, const char *ptr)
{
  int c;

  if (frv_insn_packing_flag <= 0)
    return ptr;

  for (; *ptr && *ptr != ' ' && *ptr != '\t';)
    {
      c = *ptr++;
      if (c == '%' && ((*ptr >= 'a' && *ptr <= 'z')
		       || (*ptr >= 'A' && *ptr <= 'Z')))
	{
	  int letter = *ptr++;

	  c = atoi (ptr);
	  frv_print_operand (f, frv_insn_operands [c], letter);
	  while ((c = *ptr) >= '0' && c <= '9')
	    ptr++;
	}
      else
	fputc (c, f);
    }

  fprintf (f, ".p");

  return ptr;
}

/* Set up the packing bit for the current output insn.  Note that this
   function is not called for asm insns.  */

void
frv_final_prescan_insn (rtx insn, rtx *opvec,
			int noperands ATTRIBUTE_UNUSED)
{
  if (INSN_P (insn))
    {
      if (frv_insn_packing_flag >= 0)
	{
	  frv_insn_operands = opvec;
	  frv_insn_packing_flag = PACKING_FLAG_P (insn);
	}
      else if (recog_memoized (insn) >= 0
	       && get_attr_acc_group (insn) == ACC_GROUP_ODD)
	/* Packing optimizations have been disabled, but INSN can only
	   be issued in M1.  Insert an mnop in M0.  */
	fprintf (asm_out_file, "\tmnop.p\n");
    }
}



/* A C expression whose value is RTL representing the address in a stack frame
   where the pointer to the caller's frame is stored.  Assume that FRAMEADDR is
   an RTL expression for the address of the stack frame itself.

   If you don't define this macro, the default is to return the value of
   FRAMEADDR--that is, the stack frame address is also the address of the stack
   word that points to the previous frame.  */

/* The default is correct, but we need to make sure the frame gets created.  */
rtx
frv_dynamic_chain_address (rtx frame)
{
  cfun->machine->frame_needed = 1;
  return frame;
}


/* A C expression whose value is RTL representing the value of the return
   address for the frame COUNT steps up from the current frame, after the
   prologue.  FRAMEADDR is the frame pointer of the COUNT frame, or the frame
   pointer of the COUNT - 1 frame if `RETURN_ADDR_IN_PREVIOUS_FRAME' is
   defined.

   The value of the expression must always be the correct address when COUNT is
   zero, but may be `NULL_RTX' if there is not way to determine the return
   address of other frames.  */

rtx
frv_return_addr_rtx (int count, rtx frame)
{
  if (count != 0)
    return const0_rtx;
  cfun->machine->frame_needed = 1;
  return gen_rtx_MEM (Pmode, plus_constant (frame, 8));
}

/* Given a memory reference MEMREF, interpret the referenced memory as
   an array of MODE values, and return a reference to the element
   specified by INDEX.  Assume that any pre-modification implicit in
   MEMREF has already happened.

   MEMREF must be a legitimate operand for modes larger than SImode.
   GO_IF_LEGITIMATE_ADDRESS forbids register+register addresses, which
   this function cannot handle.  */
rtx
frv_index_memory (rtx memref, enum machine_mode mode, int index)
{
  rtx base = XEXP (memref, 0);
  if (GET_CODE (base) == PRE_MODIFY)
    base = XEXP (base, 0);
  return change_address (memref, mode,
			 plus_constant (base, index * GET_MODE_SIZE (mode)));
}


/* Print a memory address as an operand to reference that memory location.  */
void
frv_print_operand_address (FILE * stream, rtx x)
{
  if (GET_CODE (x) == MEM)
    x = XEXP (x, 0);

  switch (GET_CODE (x))
    {
    case REG:
      fputs (reg_names [ REGNO (x)], stream);
      return;

    case CONST_INT:
      fprintf (stream, "%ld", (long) INTVAL (x));
      return;

    case SYMBOL_REF:
      assemble_name (stream, XSTR (x, 0));
      return;

    case LABEL_REF:
    case CONST:
      output_addr_const (stream, x);
      return;

    default:
      break;
    }

  fatal_insn ("bad insn to frv_print_operand_address:", x);
}


static void
frv_print_operand_memory_reference_reg (FILE * stream, rtx x)
{
  int regno = true_regnum (x);
  if (GPR_P (regno))
    fputs (reg_names[regno], stream);
  else
    fatal_insn ("bad register to frv_print_operand_memory_reference_reg:", x);
}

/* Print a memory reference suitable for the ld/st instructions.  */

static void
frv_print_operand_memory_reference (FILE * stream, rtx x, int addr_offset)
{
  struct frv_unspec unspec;
  rtx x0 = NULL_RTX;
  rtx x1 = NULL_RTX;

  switch (GET_CODE (x))
    {
    case SUBREG:
    case REG:
      x0 = x;
      break;

    case PRE_MODIFY:		/* (pre_modify (reg) (plus (reg) (reg))) */
      x0 = XEXP (x, 0);
      x1 = XEXP (XEXP (x, 1), 1);
      break;

    case CONST_INT:
      x1 = x;
      break;

    case PLUS:
      x0 = XEXP (x, 0);
      x1 = XEXP (x, 1);
      if (GET_CODE (x0) == CONST_INT)
	{
	  x0 = XEXP (x, 1);
	  x1 = XEXP (x, 0);
	}
      break;

    default:
      fatal_insn ("bad insn to frv_print_operand_memory_reference:", x);
      break;

    }

  if (addr_offset)
    {
      if (!x1)
	x1 = const0_rtx;
      else if (GET_CODE (x1) != CONST_INT)
	fatal_insn ("bad insn to frv_print_operand_memory_reference:", x);
    }

  fputs ("@(", stream);
  if (!x0)
    fputs (reg_names[GPR_R0], stream);
  else if (GET_CODE (x0) == REG || GET_CODE (x0) == SUBREG)
    frv_print_operand_memory_reference_reg (stream, x0);
  else
    fatal_insn ("bad insn to frv_print_operand_memory_reference:", x);

  fputs (",", stream);
  if (!x1)
    fputs (reg_names [GPR_R0], stream);

  else
    {
      switch (GET_CODE (x1))
	{
	case SUBREG:
	case REG:
	  frv_print_operand_memory_reference_reg (stream, x1);
	  break;

	case CONST_INT:
	  fprintf (stream, "%ld", (long) (INTVAL (x1) + addr_offset));
	  break;

	case CONST:
	  if (!frv_const_unspec_p (x1, &unspec))
	    fatal_insn ("bad insn to frv_print_operand_memory_reference:", x1);
	  frv_output_const_unspec (stream, &unspec);
	  break;

	default:
	  fatal_insn ("bad insn to frv_print_operand_memory_reference:", x);
	}
    }

  fputs (")", stream);
}


/* Return 2 for likely branches and 0 for non-likely branches  */

#define FRV_JUMP_LIKELY 2
#define FRV_JUMP_NOT_LIKELY 0

static int
frv_print_operand_jump_hint (rtx insn)
{
  rtx note;
  rtx labelref;
  int ret;
  HOST_WIDE_INT prob = -1;
  enum { UNKNOWN, BACKWARD, FORWARD } jump_type = UNKNOWN;

  gcc_assert (GET_CODE (insn) == JUMP_INSN);

  /* Assume any non-conditional jump is likely.  */
  if (! any_condjump_p (insn))
    ret = FRV_JUMP_LIKELY;

  else
    {
      labelref = condjump_label (insn);
      if (labelref)
	{
	  rtx label = XEXP (labelref, 0);
	  jump_type = (insn_current_address > INSN_ADDRESSES (INSN_UID (label))
		       ? BACKWARD
		       : FORWARD);
	}

      note = find_reg_note (insn, REG_BR_PROB, 0);
      if (!note)
	ret = ((jump_type == BACKWARD) ? FRV_JUMP_LIKELY : FRV_JUMP_NOT_LIKELY);

      else
	{
	  prob = INTVAL (XEXP (note, 0));
	  ret = ((prob >= (REG_BR_PROB_BASE / 2))
		 ? FRV_JUMP_LIKELY
		 : FRV_JUMP_NOT_LIKELY);
	}
    }

#if 0
  if (TARGET_DEBUG)
    {
      char *direction;

      switch (jump_type)
	{
	default:
	case UNKNOWN:	direction = "unknown jump direction";	break;
	case BACKWARD:	direction = "jump backward";		break;
	case FORWARD:	direction = "jump forward";		break;
	}

      fprintf (stderr,
	       "%s: uid %ld, %s, probability = %ld, max prob. = %ld, hint = %d\n",
	       IDENTIFIER_POINTER (DECL_NAME (current_function_decl)),
	       (long)INSN_UID (insn), direction, (long)prob,
	       (long)REG_BR_PROB_BASE, ret);
    }
#endif

  return ret;
}


/* Return the comparison operator to use for CODE given that the ICC
   register is OP0.  */

static const char *
comparison_string (enum rtx_code code, rtx op0)
{
  bool is_nz_p = GET_MODE (op0) == CC_NZmode;
  switch (code)
    {
    default:  output_operand_lossage ("bad condition code");
    case EQ:  return "eq";
    case NE:  return "ne";
    case LT:  return is_nz_p ? "n" : "lt";
    case LE:  return "le";
    case GT:  return "gt";
    case GE:  return is_nz_p ? "p" : "ge";
    case LTU: return is_nz_p ? "no" : "c";
    case LEU: return is_nz_p ? "eq" : "ls";
    case GTU: return is_nz_p ? "ne" : "hi";
    case GEU: return is_nz_p ? "ra" : "nc";
    }
}

/* Print an operand to an assembler instruction.

   `%' followed by a letter and a digit says to output an operand in an
   alternate fashion.  Four letters have standard, built-in meanings described
   below.  The machine description macro `PRINT_OPERAND' can define additional
   letters with nonstandard meanings.

   `%cDIGIT' can be used to substitute an operand that is a constant value
   without the syntax that normally indicates an immediate operand.

   `%nDIGIT' is like `%cDIGIT' except that the value of the constant is negated
   before printing.

   `%aDIGIT' can be used to substitute an operand as if it were a memory
   reference, with the actual operand treated as the address.  This may be
   useful when outputting a "load address" instruction, because often the
   assembler syntax for such an instruction requires you to write the operand
   as if it were a memory reference.

   `%lDIGIT' is used to substitute a `label_ref' into a jump instruction.

   `%=' outputs a number which is unique to each instruction in the entire
   compilation.  This is useful for making local labels to be referred to more
   than once in a single template that generates multiple assembler
   instructions.

   `%' followed by a punctuation character specifies a substitution that does
   not use an operand.  Only one case is standard: `%%' outputs a `%' into the
   assembler code.  Other nonstandard cases can be defined in the
   `PRINT_OPERAND' macro.  You must also define which punctuation characters
   are valid with the `PRINT_OPERAND_PUNCT_VALID_P' macro.  */

void
frv_print_operand (FILE * file, rtx x, int code)
{
  struct frv_unspec unspec;
  HOST_WIDE_INT value;
  int offset;

  if (code != 0 && !isalpha (code))
    value = 0;

  else if (GET_CODE (x) == CONST_INT)
    value = INTVAL (x);

  else if (GET_CODE (x) == CONST_DOUBLE)
    {
      if (GET_MODE (x) == SFmode)
	{
	  REAL_VALUE_TYPE rv;
	  long l;

	  REAL_VALUE_FROM_CONST_DOUBLE (rv, x);
	  REAL_VALUE_TO_TARGET_SINGLE (rv, l);
	  value = l;
	}

      else if (GET_MODE (x) == VOIDmode)
	value = CONST_DOUBLE_LOW (x);

      else
        fatal_insn ("bad insn in frv_print_operand, bad const_double", x);
    }

  else
    value = 0;

  switch (code)
    {

    case '.':
      /* Output r0.  */
      fputs (reg_names[GPR_R0], file);
      break;

    case '#':
      fprintf (file, "%d", frv_print_operand_jump_hint (current_output_insn));
      break;

    case '@':
      /* Output small data area base register (gr16).  */
      fputs (reg_names[SDA_BASE_REG], file);
      break;

    case '~':
      /* Output pic register (gr17).  */
      fputs (reg_names[PIC_REGNO], file);
      break;

    case '*':
      /* Output the temporary integer CCR register.  */
      fputs (reg_names[ICR_TEMP], file);
      break;

    case '&':
      /* Output the temporary integer CC register.  */
      fputs (reg_names[ICC_TEMP], file);
      break;

    /* case 'a': print an address.  */

    case 'C':
      /* Print appropriate test for integer branch false operation.  */
      fputs (comparison_string (reverse_condition (GET_CODE (x)),
				XEXP (x, 0)), file);
      break;

    case 'c':
      /* Print appropriate test for integer branch true operation.  */
      fputs (comparison_string (GET_CODE (x), XEXP (x, 0)), file);
      break;

    case 'e':
      /* Print 1 for a NE and 0 for an EQ to give the final argument
	 for a conditional instruction.  */
      if (GET_CODE (x) == NE)
	fputs ("1", file);

      else if (GET_CODE (x) == EQ)
	fputs ("0", file);

      else
	fatal_insn ("bad insn to frv_print_operand, 'e' modifier:", x);
      break;

    case 'F':
      /* Print appropriate test for floating point branch false operation.  */
      switch (GET_CODE (x))
	{
	default:
	  fatal_insn ("bad insn to frv_print_operand, 'F' modifier:", x);

	case EQ:  fputs ("ne",  file); break;
	case NE:  fputs ("eq",  file); break;
	case LT:  fputs ("uge", file); break;
	case LE:  fputs ("ug",  file); break;
	case GT:  fputs ("ule", file); break;
	case GE:  fputs ("ul",  file); break;
	}
      break;

    case 'f':
      /* Print appropriate test for floating point branch true operation.  */
      switch (GET_CODE (x))
	{
	default:
	  fatal_insn ("bad insn to frv_print_operand, 'f' modifier:", x);

	case EQ:  fputs ("eq",  file); break;
	case NE:  fputs ("ne",  file); break;
	case LT:  fputs ("lt",  file); break;
	case LE:  fputs ("le",  file); break;
	case GT:  fputs ("gt",  file); break;
	case GE:  fputs ("ge",  file); break;
	}
      break;

    case 'g':
      /* Print appropriate GOT function.  */
      if (GET_CODE (x) != CONST_INT)
	fatal_insn ("bad insn to frv_print_operand, 'g' modifier:", x);
      fputs (unspec_got_name (INTVAL (x)), file);
      break;

    case 'I':
      /* Print 'i' if the operand is a constant, or is a memory reference that
         adds a constant.  */
      if (GET_CODE (x) == MEM)
	x = ((GET_CODE (XEXP (x, 0)) == PLUS)
	     ? XEXP (XEXP (x, 0), 1)
	     : XEXP (x, 0));
      else if (GET_CODE (x) == PLUS)
	x = XEXP (x, 1);

      switch (GET_CODE (x))
	{
	default:
	  break;

	case CONST_INT:
	case SYMBOL_REF:
	case CONST:
	  fputs ("i", file);
	  break;
	}
      break;

    case 'i':
      /* For jump instructions, print 'i' if the operand is a constant or
         is an expression that adds a constant.  */
      if (GET_CODE (x) == CONST_INT)
        fputs ("i", file);

      else
        {
          if (GET_CODE (x) == CONST_INT
              || (GET_CODE (x) == PLUS
                  && (GET_CODE (XEXP (x, 1)) == CONST_INT
                      || GET_CODE (XEXP (x, 0)) == CONST_INT)))
            fputs ("i", file);
        }
      break;

    case 'L':
      /* Print the lower register of a double word register pair */
      if (GET_CODE (x) == REG)
	fputs (reg_names[ REGNO (x)+1 ], file);
      else
	fatal_insn ("bad insn to frv_print_operand, 'L' modifier:", x);
      break;

    /* case 'l': print a LABEL_REF.  */

    case 'M':
    case 'N':
      /* Print a memory reference for ld/st/jmp, %N prints a memory reference
         for the second word of double memory operations.  */
      offset = (code == 'M') ? 0 : UNITS_PER_WORD;
      switch (GET_CODE (x))
	{
	default:
	  fatal_insn ("bad insn to frv_print_operand, 'M/N' modifier:", x);

	case MEM:
	  frv_print_operand_memory_reference (file, XEXP (x, 0), offset);
	  break;

	case REG:
	case SUBREG:
	case CONST_INT:
	case PLUS:
        case SYMBOL_REF:
	  frv_print_operand_memory_reference (file, x, offset);
	  break;
	}
      break;

    case 'O':
      /* Print the opcode of a command.  */
      switch (GET_CODE (x))
	{
	default:
	  fatal_insn ("bad insn to frv_print_operand, 'O' modifier:", x);

	case PLUS:     fputs ("add", file); break;
	case MINUS:    fputs ("sub", file); break;
	case AND:      fputs ("and", file); break;
	case IOR:      fputs ("or",  file); break;
	case XOR:      fputs ("xor", file); break;
	case ASHIFT:   fputs ("sll", file); break;
	case ASHIFTRT: fputs ("sra", file); break;
	case LSHIFTRT: fputs ("srl", file); break;
	}
      break;

    /* case 'n': negate and print a constant int.  */

    case 'P':
      /* Print PIC label using operand as the number.  */
      if (GET_CODE (x) != CONST_INT)
	fatal_insn ("bad insn to frv_print_operand, P modifier:", x);

      fprintf (file, ".LCF%ld", (long)INTVAL (x));
      break;

    case 'U':
      /* Print 'u' if the operand is a update load/store.  */
      if (GET_CODE (x) == MEM && GET_CODE (XEXP (x, 0)) == PRE_MODIFY)
	fputs ("u", file);
      break;

    case 'z':
      /* If value is 0, print gr0, otherwise it must be a register.  */
      if (GET_CODE (x) == CONST_INT && INTVAL (x) == 0)
	fputs (reg_names[GPR_R0], file);

      else if (GET_CODE (x) == REG)
        fputs (reg_names [REGNO (x)], file);

      else
        fatal_insn ("bad insn in frv_print_operand, z case", x);
      break;

    case 'x':
      /* Print constant in hex.  */
      if (GET_CODE (x) == CONST_INT || GET_CODE (x) == CONST_DOUBLE)
        {
	  fprintf (file, "%s0x%.4lx", IMMEDIATE_PREFIX, (long) value);
	  break;
	}

      /* Fall through.  */

    case '\0':
      if (GET_CODE (x) == REG)
        fputs (reg_names [REGNO (x)], file);

      else if (GET_CODE (x) == CONST_INT
              || GET_CODE (x) == CONST_DOUBLE)
        fprintf (file, "%s%ld", IMMEDIATE_PREFIX, (long) value);

      else if (frv_const_unspec_p (x, &unspec))
	frv_output_const_unspec (file, &unspec);

      else if (GET_CODE (x) == MEM)
        frv_print_operand_address (file, XEXP (x, 0));

      else if (CONSTANT_ADDRESS_P (x))
        frv_print_operand_address (file, x);

      else
        fatal_insn ("bad insn in frv_print_operand, 0 case", x);

      break;

    default:
      fatal_insn ("frv_print_operand: unknown code", x);
      break;
    }

  return;
}


/* A C statement (sans semicolon) for initializing the variable CUM for the
   state at the beginning of the argument list.  The variable has type
   `CUMULATIVE_ARGS'.  The value of FNTYPE is the tree node for the data type
   of the function which will receive the args, or 0 if the args are to a
   compiler support library function.  The value of INDIRECT is nonzero when
   processing an indirect call, for example a call through a function pointer.
   The value of INDIRECT is zero for a call to an explicitly named function, a
   library function call, or when `INIT_CUMULATIVE_ARGS' is used to find
   arguments for the function being compiled.

   When processing a call to a compiler support library function, LIBNAME
   identifies which one.  It is a `symbol_ref' rtx which contains the name of
   the function, as a string.  LIBNAME is 0 when an ordinary C function call is
   being processed.  Thus, each time this macro is called, either LIBNAME or
   FNTYPE is nonzero, but never both of them at once.  */

void
frv_init_cumulative_args (CUMULATIVE_ARGS *cum,
                          tree fntype,
                          rtx libname,
                          tree fndecl,
                          int incoming)
{
  *cum = FIRST_ARG_REGNUM;

  if (TARGET_DEBUG_ARG)
    {
      fprintf (stderr, "\ninit_cumulative_args:");
      if (!fndecl && fntype)
	fputs (" indirect", stderr);

      if (incoming)
	fputs (" incoming", stderr);

      if (fntype)
	{
	  tree ret_type = TREE_TYPE (fntype);
	  fprintf (stderr, " return=%s,",
		   tree_code_name[ (int)TREE_CODE (ret_type) ]);
	}

      if (libname && GET_CODE (libname) == SYMBOL_REF)
	fprintf (stderr, " libname=%s", XSTR (libname, 0));

      if (cfun->returns_struct)
	fprintf (stderr, " return-struct");

      putc ('\n', stderr);
    }
}


/* Return true if we should pass an argument on the stack rather than
   in registers.  */

static bool
frv_must_pass_in_stack (enum machine_mode mode, tree type)
{
  if (mode == BLKmode)
    return true;
  if (type == NULL)
    return false;
  return AGGREGATE_TYPE_P (type);
}

/* If defined, a C expression that gives the alignment boundary, in bits, of an
   argument with the specified mode and type.  If it is not defined,
   `PARM_BOUNDARY' is used for all arguments.  */

int
frv_function_arg_boundary (enum machine_mode mode ATTRIBUTE_UNUSED,
                           tree type ATTRIBUTE_UNUSED)
{
  return BITS_PER_WORD;
}

rtx
frv_function_arg (CUMULATIVE_ARGS *cum,
                  enum machine_mode mode,
                  tree type ATTRIBUTE_UNUSED,
                  int named,
                  int incoming ATTRIBUTE_UNUSED)
{
  enum machine_mode xmode = (mode == BLKmode) ? SImode : mode;
  int arg_num = *cum;
  rtx ret;
  const char *debstr;

  /* Return a marker for use in the call instruction.  */
  if (xmode == VOIDmode)
    {
      ret = const0_rtx;
      debstr = "<0>";
    }

  else if (arg_num <= LAST_ARG_REGNUM)
    {
      ret = gen_rtx_REG (xmode, arg_num);
      debstr = reg_names[arg_num];
    }

  else
    {
      ret = NULL_RTX;
      debstr = "memory";
    }

  if (TARGET_DEBUG_ARG)
    fprintf (stderr,
	     "function_arg: words = %2d, mode = %4s, named = %d, size = %3d, arg = %s\n",
	     arg_num, GET_MODE_NAME (mode), named, GET_MODE_SIZE (mode), debstr);

  return ret;
}


/* A C statement (sans semicolon) to update the summarizer variable CUM to
   advance past an argument in the argument list.  The values MODE, TYPE and
   NAMED describe that argument.  Once this is done, the variable CUM is
   suitable for analyzing the *following* argument with `FUNCTION_ARG', etc.

   This macro need not do anything if the argument in question was passed on
   the stack.  The compiler knows how to track the amount of stack space used
   for arguments without any special help.  */

void
frv_function_arg_advance (CUMULATIVE_ARGS *cum,
                          enum machine_mode mode,
                          tree type ATTRIBUTE_UNUSED,
                          int named)
{
  enum machine_mode xmode = (mode == BLKmode) ? SImode : mode;
  int bytes = GET_MODE_SIZE (xmode);
  int words = (bytes + UNITS_PER_WORD  - 1) / UNITS_PER_WORD;
  int arg_num = *cum;

  *cum = arg_num + words;

  if (TARGET_DEBUG_ARG)
    fprintf (stderr,
	     "function_adv: words = %2d, mode = %4s, named = %d, size = %3d\n",
	     arg_num, GET_MODE_NAME (mode), named, words * UNITS_PER_WORD);
}


/* A C expression for the number of words, at the beginning of an argument,
   must be put in registers.  The value must be zero for arguments that are
   passed entirely in registers or that are entirely pushed on the stack.

   On some machines, certain arguments must be passed partially in registers
   and partially in memory.  On these machines, typically the first N words of
   arguments are passed in registers, and the rest on the stack.  If a
   multi-word argument (a `double' or a structure) crosses that boundary, its
   first few words must be passed in registers and the rest must be pushed.
   This macro tells the compiler when this occurs, and how many of the words
   should go in registers.

   `FUNCTION_ARG' for these arguments should return the first register to be
   used by the caller for this argument; likewise `FUNCTION_INCOMING_ARG', for
   the called function.  */

static int
frv_arg_partial_bytes (CUMULATIVE_ARGS *cum, enum machine_mode mode,
		       tree type ATTRIBUTE_UNUSED, bool named ATTRIBUTE_UNUSED)
{
  enum machine_mode xmode = (mode == BLKmode) ? SImode : mode;
  int bytes = GET_MODE_SIZE (xmode);
  int words = (bytes + UNITS_PER_WORD - 1) / UNITS_PER_WORD;
  int arg_num = *cum;
  int ret;

  ret = ((arg_num <= LAST_ARG_REGNUM && arg_num + words > LAST_ARG_REGNUM+1)
	 ? LAST_ARG_REGNUM - arg_num + 1
	 : 0);
  ret *= UNITS_PER_WORD;

  if (TARGET_DEBUG_ARG && ret)
    fprintf (stderr, "frv_arg_partial_bytes: %d\n", ret);

  return ret;
}


/* Return true if a register is ok to use as a base or index register.  */

static FRV_INLINE int
frv_regno_ok_for_base_p (int regno, int strict_p)
{
  if (GPR_P (regno))
    return TRUE;

  if (strict_p)
    return (reg_renumber[regno] >= 0 && GPR_P (reg_renumber[regno]));

  if (regno == ARG_POINTER_REGNUM)
    return TRUE;

  return (regno >= FIRST_PSEUDO_REGISTER);
}


/* A C compound statement with a conditional `goto LABEL;' executed if X (an
   RTX) is a legitimate memory address on the target machine for a memory
   operand of mode MODE.

   It usually pays to define several simpler macros to serve as subroutines for
   this one.  Otherwise it may be too complicated to understand.

   This macro must exist in two variants: a strict variant and a non-strict
   one.  The strict variant is used in the reload pass.  It must be defined so
   that any pseudo-register that has not been allocated a hard register is
   considered a memory reference.  In contexts where some kind of register is
   required, a pseudo-register with no hard register must be rejected.

   The non-strict variant is used in other passes.  It must be defined to
   accept all pseudo-registers in every context where some kind of register is
   required.

   Compiler source files that want to use the strict variant of this macro
   define the macro `REG_OK_STRICT'.  You should use an `#ifdef REG_OK_STRICT'
   conditional to define the strict variant in that case and the non-strict
   variant otherwise.

   Subroutines to check for acceptable registers for various purposes (one for
   base registers, one for index registers, and so on) are typically among the
   subroutines used to define `GO_IF_LEGITIMATE_ADDRESS'.  Then only these
   subroutine macros need have two variants; the higher levels of macros may be
   the same whether strict or not.

   Normally, constant addresses which are the sum of a `symbol_ref' and an
   integer are stored inside a `const' RTX to mark them as constant.
   Therefore, there is no need to recognize such sums specifically as
   legitimate addresses.  Normally you would simply recognize any `const' as
   legitimate.

   Usually `PRINT_OPERAND_ADDRESS' is not prepared to handle constant sums that
   are not marked with `const'.  It assumes that a naked `plus' indicates
   indexing.  If so, then you *must* reject such naked constant sums as
   illegitimate addresses, so that none of them will be given to
   `PRINT_OPERAND_ADDRESS'.

   On some machines, whether a symbolic address is legitimate depends on the
   section that the address refers to.  On these machines, define the macro
   `ENCODE_SECTION_INFO' to store the information into the `symbol_ref', and
   then check for it here.  When you see a `const', you will have to look
   inside it to find the `symbol_ref' in order to determine the section.

   The best way to modify the name string is by adding text to the beginning,
   with suitable punctuation to prevent any ambiguity.  Allocate the new name
   in `saveable_obstack'.  You will have to modify `ASM_OUTPUT_LABELREF' to
   remove and decode the added text and output the name accordingly, and define
   `(* targetm.strip_name_encoding)' to access the original name string.

   You can check the information stored here into the `symbol_ref' in the
   definitions of the macros `GO_IF_LEGITIMATE_ADDRESS' and
   `PRINT_OPERAND_ADDRESS'.  */

int
frv_legitimate_address_p (enum machine_mode mode,
                          rtx x,
                          int strict_p,
                          int condexec_p,
			  int allow_double_reg_p)
{
  rtx x0, x1;
  int ret = 0;
  HOST_WIDE_INT value;
  unsigned regno0;

  if (FRV_SYMBOL_REF_TLS_P (x))
    return 0;

  switch (GET_CODE (x))
    {
    default:
      break;

    case SUBREG:
      x = SUBREG_REG (x);
      if (GET_CODE (x) != REG)
        break;

      /* Fall through.  */

    case REG:
      ret = frv_regno_ok_for_base_p (REGNO (x), strict_p);
      break;

    case PRE_MODIFY:
      x0 = XEXP (x, 0);
      x1 = XEXP (x, 1);
      if (GET_CODE (x0) != REG
	  || ! frv_regno_ok_for_base_p (REGNO (x0), strict_p)
	  || GET_CODE (x1) != PLUS
	  || ! rtx_equal_p (x0, XEXP (x1, 0))
	  || GET_CODE (XEXP (x1, 1)) != REG
	  || ! frv_regno_ok_for_base_p (REGNO (XEXP (x1, 1)), strict_p))
	break;

      ret = 1;
      break;

    case CONST_INT:
      /* 12 bit immediate */
      if (condexec_p)
	ret = FALSE;
      else
	{
	  ret = IN_RANGE_P (INTVAL (x), -2048, 2047);

	  /* If we can't use load/store double operations, make sure we can
	     address the second word.  */
	  if (ret && GET_MODE_SIZE (mode) > UNITS_PER_WORD)
	    ret = IN_RANGE_P (INTVAL (x) + GET_MODE_SIZE (mode) - 1,
			      -2048, 2047);
	}
      break;

    case PLUS:
      x0 = XEXP (x, 0);
      x1 = XEXP (x, 1);

      if (GET_CODE (x0) == SUBREG)
	x0 = SUBREG_REG (x0);

      if (GET_CODE (x0) != REG)
	break;

      regno0 = REGNO (x0);
      if (!frv_regno_ok_for_base_p (regno0, strict_p))
	break;

      switch (GET_CODE (x1))
	{
	default:
	  break;

	case SUBREG:
	  x1 = SUBREG_REG (x1);
	  if (GET_CODE (x1) != REG)
	    break;

	  /* Fall through.  */

	case REG:
	  /* Do not allow reg+reg addressing for modes > 1 word if we
	     can't depend on having move double instructions.  */
	  if (!allow_double_reg_p && GET_MODE_SIZE (mode) > UNITS_PER_WORD)
	    ret = FALSE;
	  else
	    ret = frv_regno_ok_for_base_p (REGNO (x1), strict_p);
	  break;

	case CONST_INT:
          /* 12 bit immediate */
	  if (condexec_p)
	    ret = FALSE;
	  else
	    {
	      value = INTVAL (x1);
	      ret = IN_RANGE_P (value, -2048, 2047);

	      /* If we can't use load/store double operations, make sure we can
		 address the second word.  */
	      if (ret && GET_MODE_SIZE (mode) > UNITS_PER_WORD)
		ret = IN_RANGE_P (value + GET_MODE_SIZE (mode) - 1, -2048, 2047);
	    }
	  break;

	case CONST:
	  if (!condexec_p && got12_operand (x1, VOIDmode))
	    ret = TRUE;
	  break;

	}
      break;
    }

  if (TARGET_DEBUG_ADDR)
    {
      fprintf (stderr, "\n========== GO_IF_LEGITIMATE_ADDRESS, mode = %s, result = %d, addresses are %sstrict%s\n",
	       GET_MODE_NAME (mode), ret, (strict_p) ? "" : "not ",
	       (condexec_p) ? ", inside conditional code" : "");
      debug_rtx (x);
    }

  return ret;
}

/* Given an ADDR, generate code to inline the PLT.  */
static rtx
gen_inlined_tls_plt (rtx addr)
{
  rtx retval, dest;
  rtx picreg = get_hard_reg_initial_val (Pmode, FDPIC_REG);


  dest = gen_reg_rtx (DImode);

  if (flag_pic == 1)
    {
      /*
	-fpic version:

	lddi.p  @(gr15, #gottlsdesc12(ADDR)), gr8
	calll    #gettlsoff(ADDR)@(gr8, gr0)
      */
      emit_insn (gen_tls_lddi (dest, addr, picreg));
    }
  else
    {
      /*
	-fPIC version:

	sethi.p #gottlsdeschi(ADDR), gr8
	setlo   #gottlsdesclo(ADDR), gr8
	ldd     #tlsdesc(ADDR)@(gr15, gr8), gr8
	calll   #gettlsoff(ADDR)@(gr8, gr0)
      */
      rtx reguse = gen_reg_rtx (Pmode);
      emit_insn (gen_tlsoff_hilo (reguse, addr, GEN_INT (R_FRV_GOTTLSDESCHI)));
      emit_insn (gen_tls_tlsdesc_ldd (dest, picreg, reguse, addr));
    }

  retval = gen_reg_rtx (Pmode);
  emit_insn (gen_tls_indirect_call (retval, addr, dest, picreg));
  return retval;
}

/* Emit a TLSMOFF or TLSMOFF12 offset, depending on -mTLS.  Returns
   the destination address.  */
static rtx
gen_tlsmoff (rtx addr, rtx reg)
{
  rtx dest = gen_reg_rtx (Pmode);

  if (TARGET_BIG_TLS)
    {
      /* sethi.p #tlsmoffhi(x), grA
	 setlo   #tlsmofflo(x), grA
      */
      dest = gen_reg_rtx (Pmode);
      emit_insn (gen_tlsoff_hilo (dest, addr,
				  GEN_INT (R_FRV_TLSMOFFHI)));
      dest = gen_rtx_PLUS (Pmode, dest, reg);
    }
  else
    {
      /* addi grB, #tlsmoff12(x), grC
	   -or-
	 ld/st @(grB, #tlsmoff12(x)), grC
      */
      dest = gen_reg_rtx (Pmode);
      emit_insn (gen_symGOTOFF2reg_i (dest, addr, reg,
				      GEN_INT (R_FRV_TLSMOFF12)));
    }
  return dest;
}

/* Generate code for a TLS address.  */
static rtx
frv_legitimize_tls_address (rtx addr, enum tls_model model)
{
  rtx dest, tp = gen_rtx_REG (Pmode, 29);
  rtx picreg = get_hard_reg_initial_val (Pmode, 15);

  switch (model)
    {
    case TLS_MODEL_INITIAL_EXEC:
      if (flag_pic == 1)
	{
	  /* -fpic version.
	     ldi @(gr15, #gottlsoff12(x)), gr5
	   */
	  dest = gen_reg_rtx (Pmode);
	  emit_insn (gen_tls_load_gottlsoff12 (dest, addr, picreg));
	  dest = gen_rtx_PLUS (Pmode, tp, dest);
	}
      else
	{
	  /* -fPIC or anything else.

	    sethi.p #gottlsoffhi(x), gr14
	    setlo   #gottlsofflo(x), gr14
	    ld      #tlsoff(x)@(gr15, gr14), gr9
	  */
	  rtx tmp = gen_reg_rtx (Pmode);
	  dest = gen_reg_rtx (Pmode);
	  emit_insn (gen_tlsoff_hilo (tmp, addr,
				      GEN_INT (R_FRV_GOTTLSOFF_HI)));

	  emit_insn (gen_tls_tlsoff_ld (dest, picreg, tmp, addr));
	  dest = gen_rtx_PLUS (Pmode, tp, dest);
	}
      break;
    case TLS_MODEL_LOCAL_DYNAMIC:
      {
	rtx reg, retval;

	if (TARGET_INLINE_PLT)
	  retval = gen_inlined_tls_plt (GEN_INT (0));
	else
	  {
	    /* call #gettlsoff(0) */
	    retval = gen_reg_rtx (Pmode);
	    emit_insn (gen_call_gettlsoff (retval, GEN_INT (0), picreg));
	  }

	reg = gen_reg_rtx (Pmode);
	emit_insn (gen_rtx_SET (VOIDmode, reg,
				gen_rtx_PLUS (Pmode,
					      retval, tp)));

	dest = gen_tlsmoff (addr, reg);

	/*
	dest = gen_reg_rtx (Pmode);
	emit_insn (gen_tlsoff_hilo (dest, addr,
				    GEN_INT (R_FRV_TLSMOFFHI)));
	dest = gen_rtx_PLUS (Pmode, dest, reg);
	*/
	break;
      }
    case TLS_MODEL_LOCAL_EXEC:
      dest = gen_tlsmoff (addr, gen_rtx_REG (Pmode, 29));
      break;
    case TLS_MODEL_GLOBAL_DYNAMIC:
      {
	rtx retval;

	if (TARGET_INLINE_PLT)
	  retval = gen_inlined_tls_plt (addr);
	else
	  {
	    /* call #gettlsoff(x) */
	    retval = gen_reg_rtx (Pmode);
	    emit_insn (gen_call_gettlsoff (retval, addr, picreg));
	  }
	dest = gen_rtx_PLUS (Pmode, retval, tp);
	break;
      }
    default:
      gcc_unreachable ();
    }

  return dest;
}

rtx
frv_legitimize_address (rtx x,
			rtx oldx ATTRIBUTE_UNUSED,
			enum machine_mode mode ATTRIBUTE_UNUSED)
{
  if (GET_CODE (x) == SYMBOL_REF)
    {
      enum tls_model model = SYMBOL_REF_TLS_MODEL (x);
      if (model != 0)
        return frv_legitimize_tls_address (x, model);
    }

  return NULL_RTX;
}

/* Test whether a local function descriptor is canonical, i.e.,
   whether we can use FUNCDESC_GOTOFF to compute the address of the
   function.  */

static bool
frv_local_funcdesc_p (rtx fnx)
{
  tree fn;
  enum symbol_visibility vis;
  bool ret;

  if (! SYMBOL_REF_LOCAL_P (fnx))
    return FALSE;

  fn = SYMBOL_REF_DECL (fnx);

  if (! fn)
    return FALSE;

  vis = DECL_VISIBILITY (fn);

  if (vis == VISIBILITY_PROTECTED)
    /* Private function descriptors for protected functions are not
       canonical.  Temporarily change the visibility to global.  */
    vis = VISIBILITY_DEFAULT;
  else if (flag_shlib)
    /* If we're already compiling for a shared library (that, unlike
       executables, can't assume that the existence of a definition
       implies local binding), we can skip the re-testing.  */
    return TRUE;

  ret = default_binds_local_p_1 (fn, flag_pic);

  DECL_VISIBILITY (fn) = vis;

  return ret;
}

/* Load the _gp symbol into DEST.  SRC is supposed to be the FDPIC
   register.  */

rtx
frv_gen_GPsym2reg (rtx dest, rtx src)
{
  tree gp = get_identifier ("_gp");
  rtx gp_sym = gen_rtx_SYMBOL_REF (Pmode, IDENTIFIER_POINTER (gp));

  return gen_symGOT2reg (dest, gp_sym, src, GEN_INT (R_FRV_GOT12));
}

static const char *
unspec_got_name (int i)
{
  switch (i)
    {
    case R_FRV_GOT12: return "got12";
    case R_FRV_GOTHI: return "gothi";
    case R_FRV_GOTLO: return "gotlo";
    case R_FRV_FUNCDESC: return "funcdesc";
    case R_FRV_FUNCDESC_GOT12: return "gotfuncdesc12";
    case R_FRV_FUNCDESC_GOTHI: return "gotfuncdeschi";
    case R_FRV_FUNCDESC_GOTLO: return "gotfuncdesclo";
    case R_FRV_FUNCDESC_VALUE: return "funcdescvalue";
    case R_FRV_FUNCDESC_GOTOFF12: return "gotofffuncdesc12";
    case R_FRV_FUNCDESC_GOTOFFHI: return "gotofffuncdeschi";
    case R_FRV_FUNCDESC_GOTOFFLO: return "gotofffuncdesclo";
    case R_FRV_GOTOFF12: return "gotoff12";
    case R_FRV_GOTOFFHI: return "gotoffhi";
    case R_FRV_GOTOFFLO: return "gotofflo";
    case R_FRV_GPREL12: return "gprel12";
    case R_FRV_GPRELHI: return "gprelhi";
    case R_FRV_GPRELLO: return "gprello";
    case R_FRV_GOTTLSOFF_HI: return "gottlsoffhi";
    case R_FRV_GOTTLSOFF_LO: return "gottlsofflo";
    case R_FRV_TLSMOFFHI: return "tlsmoffhi";
    case R_FRV_TLSMOFFLO: return "tlsmofflo";
    case R_FRV_TLSMOFF12: return "tlsmoff12";
    case R_FRV_TLSDESCHI: return "tlsdeschi";
    case R_FRV_TLSDESCLO: return "tlsdesclo";
    case R_FRV_GOTTLSDESCHI: return "gottlsdeschi";
    case R_FRV_GOTTLSDESCLO: return "gottlsdesclo";
    default: gcc_unreachable ();
    }
}

/* Write the assembler syntax for UNSPEC to STREAM.  Note that any offset
   is added inside the relocation operator.  */

static void
frv_output_const_unspec (FILE *stream, const struct frv_unspec *unspec)
{
  fprintf (stream, "#%s(", unspec_got_name (unspec->reloc));
  output_addr_const (stream, plus_constant (unspec->symbol, unspec->offset));
  fputs (")", stream);
}

/* Implement FIND_BASE_TERM.  See whether ORIG_X represents #gprel12(foo)
   or #gotoff12(foo) for some small data symbol foo.  If so, return foo,
   otherwise return ORIG_X.  */

rtx
frv_find_base_term (rtx x)
{
  struct frv_unspec unspec;

  if (frv_const_unspec_p (x, &unspec)
      && frv_small_data_reloc_p (unspec.symbol, unspec.reloc))
    return plus_constant (unspec.symbol, unspec.offset);

  return x;
}

/* Return 1 if operand is a valid FRV address.  CONDEXEC_P is true if
   the operand is used by a predicated instruction.  */

int
frv_legitimate_memory_operand (rtx op, enum machine_mode mode, int condexec_p)
{
  return ((GET_MODE (op) == mode || mode == VOIDmode)
	  && GET_CODE (op) == MEM
	  && frv_legitimate_address_p (mode, XEXP (op, 0),
				       reload_completed, condexec_p, FALSE));
}

void
frv_expand_fdpic_call (rtx *operands, bool ret_value, bool sibcall)
{
  rtx lr = gen_rtx_REG (Pmode, LR_REGNO);
  rtx picreg = get_hard_reg_initial_val (SImode, FDPIC_REG);
  rtx c, rvrtx=0;
  rtx addr;

  if (ret_value)
    {
      rvrtx = operands[0];
      operands ++;
    }

  addr = XEXP (operands[0], 0);

  /* Inline PLTs if we're optimizing for speed.  We'd like to inline
     any calls that would involve a PLT, but can't tell, since we
     don't know whether an extern function is going to be provided by
     a separate translation unit or imported from a separate module.
     When compiling for shared libraries, if the function has default
     visibility, we assume it's overridable, so we inline the PLT, but
     for executables, we don't really have a way to make a good
     decision: a function is as likely to be imported from a shared
     library as it is to be defined in the executable itself.  We
     assume executables will get global functions defined locally,
     whereas shared libraries will have them potentially overridden,
     so we only inline PLTs when compiling for shared libraries.

     In order to mark a function as local to a shared library, any
     non-default visibility attribute suffices.  Unfortunately,
     there's no simple way to tag a function declaration as ``in a
     different module'', which we could then use to trigger PLT
     inlining on executables.  There's -minline-plt, but it affects
     all external functions, so one would have to also mark function
     declarations available in the same module with non-default
     visibility, which is advantageous in itself.  */
  if (GET_CODE (addr) == SYMBOL_REF
      && ((!SYMBOL_REF_LOCAL_P (addr) && TARGET_INLINE_PLT)
	  || sibcall))
    {
      rtx x, dest;
      dest = gen_reg_rtx (SImode);
      if (flag_pic != 1)
	x = gen_symGOTOFF2reg_hilo (dest, addr, OUR_FDPIC_REG,
				    GEN_INT (R_FRV_FUNCDESC_GOTOFF12));
      else
	x = gen_symGOTOFF2reg (dest, addr, OUR_FDPIC_REG,
			       GEN_INT (R_FRV_FUNCDESC_GOTOFF12));
      emit_insn (x);
      cfun->uses_pic_offset_table = TRUE;
      addr = dest;
    }    
  else if (GET_CODE (addr) == SYMBOL_REF)
    {
      /* These are always either local, or handled through a local
	 PLT.  */
      if (ret_value)
	c = gen_call_value_fdpicsi (rvrtx, addr, operands[1],
				    operands[2], picreg, lr);
      else
	c = gen_call_fdpicsi (addr, operands[1], operands[2], picreg, lr);
      emit_call_insn (c);
      return;
    }
  else if (! ldd_address_operand (addr, Pmode))
    addr = force_reg (Pmode, addr);

  picreg = gen_reg_rtx (DImode);
  emit_insn (gen_movdi_ldd (picreg, addr));

  if (sibcall && ret_value)
    c = gen_sibcall_value_fdpicdi (rvrtx, picreg, const0_rtx);
  else if (sibcall)
    c = gen_sibcall_fdpicdi (picreg, const0_rtx);
  else if (ret_value)
    c = gen_call_value_fdpicdi (rvrtx, picreg, const0_rtx, lr);
  else
    c = gen_call_fdpicdi (picreg, const0_rtx, lr);
  emit_call_insn (c);
}

/* Look for a SYMBOL_REF of a function in an rtx.  We always want to
   process these separately from any offsets, such that we add any
   offsets to the function descriptor (the actual pointer), not to the
   function address.  */

static bool
frv_function_symbol_referenced_p (rtx x)
{
  const char *format;
  int length;
  int j;

  if (GET_CODE (x) == SYMBOL_REF)
    return SYMBOL_REF_FUNCTION_P (x);

  length = GET_RTX_LENGTH (GET_CODE (x));
  format = GET_RTX_FORMAT (GET_CODE (x));

  for (j = 0; j < length; ++j)
    {
      switch (format[j])
	{
	case 'e':
	  if (frv_function_symbol_referenced_p (XEXP (x, j)))
	    return TRUE;
	  break;

	case 'V':
	case 'E':
	  if (XVEC (x, j) != 0)
	    {
	      int k;
	      for (k = 0; k < XVECLEN (x, j); ++k)
		if (frv_function_symbol_referenced_p (XVECEXP (x, j, k)))
		  return TRUE;
	    }
	  break;

	default:
	  /* Nothing to do.  */
	  break;
	}
    }

  return FALSE;
}

/* Return true if the memory operand is one that can be conditionally
   executed.  */

int
condexec_memory_operand (rtx op, enum machine_mode mode)
{
  enum machine_mode op_mode = GET_MODE (op);
  rtx addr;

  if (mode != VOIDmode && op_mode != mode)
    return FALSE;

  switch (op_mode)
    {
    default:
      return FALSE;

    case QImode:
    case HImode:
    case SImode:
    case SFmode:
      break;
    }

  if (GET_CODE (op) != MEM)
    return FALSE;

  addr = XEXP (op, 0);
  return frv_legitimate_address_p (mode, addr, reload_completed, TRUE, FALSE);
}

/* Return true if the bare return instruction can be used outside of the
   epilog code.  For frv, we only do it if there was no stack allocation.  */

int
direct_return_p (void)
{
  frv_stack_t *info;

  if (!reload_completed)
    return FALSE;

  info = frv_stack_info ();
  return (info->total_size == 0);
}


void
frv_emit_move (enum machine_mode mode, rtx dest, rtx src)
{
  if (GET_CODE (src) == SYMBOL_REF)
    {
      enum tls_model model = SYMBOL_REF_TLS_MODEL (src);
      if (model != 0)
	src = frv_legitimize_tls_address (src, model);
    }

  switch (mode)
    {
    case SImode:
      if (frv_emit_movsi (dest, src))
	return;
      break;

    case QImode:
    case HImode:
    case DImode:
    case SFmode:
    case DFmode:
      if (!reload_in_progress
	  && !reload_completed
	  && !register_operand (dest, mode)
	  && !reg_or_0_operand (src, mode))
	src = copy_to_mode_reg (mode, src);
      break;

    default:
      gcc_unreachable ();
    }

  emit_insn (gen_rtx_SET (VOIDmode, dest, src));
}

/* Emit code to handle a MOVSI, adding in the small data register or pic
   register if needed to load up addresses.  Return TRUE if the appropriate
   instructions are emitted.  */

int
frv_emit_movsi (rtx dest, rtx src)
{
  int base_regno = -1;
  int unspec = 0;
  rtx sym = src;
  struct frv_unspec old_unspec;

  if (!reload_in_progress
      && !reload_completed
      && !register_operand (dest, SImode)
      && (!reg_or_0_operand (src, SImode)
	     /* Virtual registers will almost always be replaced by an
		add instruction, so expose this to CSE by copying to
		an intermediate register.  */
	  || (GET_CODE (src) == REG
	      && IN_RANGE_P (REGNO (src),
			     FIRST_VIRTUAL_REGISTER,
			     LAST_VIRTUAL_REGISTER))))
    {
      emit_insn (gen_rtx_SET (VOIDmode, dest, copy_to_mode_reg (SImode, src)));
      return TRUE;
    }

  /* Explicitly add in the PIC or small data register if needed.  */
  switch (GET_CODE (src))
    {
    default:
      break;

    case LABEL_REF:
    handle_label:
      if (TARGET_FDPIC)
	{
	  /* Using GPREL12, we use a single GOT entry for all symbols
	     in read-only sections, but trade sequences such as:

	     sethi #gothi(label), gr#
	     setlo #gotlo(label), gr#
	     ld    @(gr15,gr#), gr#

	     for

	     ld    @(gr15,#got12(_gp)), gr#
	     sethi #gprelhi(label), gr##
	     setlo #gprello(label), gr##
	     add   gr#, gr##, gr##

	     We may often be able to share gr# for multiple
	     computations of GPREL addresses, and we may often fold
	     the final add into the pair of registers of a load or
	     store instruction, so it's often profitable.  Even when
	     optimizing for size, we're trading a GOT entry for an
	     additional instruction, which trades GOT space
	     (read-write) for code size (read-only, shareable), as
	     long as the symbol is not used in more than two different
	     locations.
	     
	     With -fpie/-fpic, we'd be trading a single load for a
	     sequence of 4 instructions, because the offset of the
	     label can't be assumed to be addressable with 12 bits, so
	     we don't do this.  */
	  if (TARGET_GPREL_RO)
	    unspec = R_FRV_GPREL12;
	  else
	    unspec = R_FRV_GOT12;
	}
      else if (flag_pic)
	base_regno = PIC_REGNO;

      break;

    case CONST:
      if (frv_const_unspec_p (src, &old_unspec))
	break;

      if (TARGET_FDPIC && frv_function_symbol_referenced_p (XEXP (src, 0)))
	{
	handle_whatever:
	  src = force_reg (GET_MODE (XEXP (src, 0)), XEXP (src, 0));
	  emit_move_insn (dest, src);
	  return TRUE;
	}
      else
	{
	  sym = XEXP (sym, 0);
	  if (GET_CODE (sym) == PLUS
	      && GET_CODE (XEXP (sym, 0)) == SYMBOL_REF
	      && GET_CODE (XEXP (sym, 1)) == CONST_INT)
	    sym = XEXP (sym, 0);
	  if (GET_CODE (sym) == SYMBOL_REF)
	    goto handle_sym;
	  else if (GET_CODE (sym) == LABEL_REF)
	    goto handle_label;
	  else
	    goto handle_whatever;
	}
      break;

    case SYMBOL_REF:
    handle_sym:
      if (TARGET_FDPIC)
	{
	  enum tls_model model = SYMBOL_REF_TLS_MODEL (sym);

	  if (model != 0)
	    {
	      src = frv_legitimize_tls_address (src, model);
	      emit_move_insn (dest, src);
	      return TRUE;
	    }

	  if (SYMBOL_REF_FUNCTION_P (sym))
	    {
	      if (frv_local_funcdesc_p (sym))
		unspec = R_FRV_FUNCDESC_GOTOFF12;
	      else
		unspec = R_FRV_FUNCDESC_GOT12;
	    }
	  else
	    {
	      if (CONSTANT_POOL_ADDRESS_P (sym))
		switch (GET_CODE (get_pool_constant (sym)))
		  {
		  case CONST:
		  case SYMBOL_REF:
		  case LABEL_REF:
		    if (flag_pic)
		      {
			unspec = R_FRV_GOTOFF12;
			break;
		      }
		    /* Fall through.  */
		  default:
		    if (TARGET_GPREL_RO)
		      unspec = R_FRV_GPREL12;
		    else
		      unspec = R_FRV_GOT12;
		    break;
		  }
	      else if (SYMBOL_REF_LOCAL_P (sym)
		       && !SYMBOL_REF_EXTERNAL_P (sym)
		       && SYMBOL_REF_DECL (sym)
		       && (!DECL_P (SYMBOL_REF_DECL (sym))
			   || !DECL_COMMON (SYMBOL_REF_DECL (sym))))
		{
		  tree decl = SYMBOL_REF_DECL (sym);
		  tree init = TREE_CODE (decl) == VAR_DECL
		    ? DECL_INITIAL (decl)
		    : TREE_CODE (decl) == CONSTRUCTOR
		    ? decl : 0;
		  int reloc = 0;
		  bool named_section, readonly;

		  if (init && init != error_mark_node)
		    reloc = compute_reloc_for_constant (init);
		  
		  named_section = TREE_CODE (decl) == VAR_DECL
		    && lookup_attribute ("section", DECL_ATTRIBUTES (decl));
		  readonly = decl_readonly_section (decl, reloc);
		  
		  if (named_section)
		    unspec = R_FRV_GOT12;
		  else if (!readonly)
		    unspec = R_FRV_GOTOFF12;
		  else if (readonly && TARGET_GPREL_RO)
		    unspec = R_FRV_GPREL12;
		  else
		    unspec = R_FRV_GOT12;
		}
	      else
		unspec = R_FRV_GOT12;
	    }
	}

      else if (SYMBOL_REF_SMALL_P (sym))
	base_regno = SDA_BASE_REG;

      else if (flag_pic)
	base_regno = PIC_REGNO;

      break;
    }

  if (base_regno >= 0)
    {
      if (GET_CODE (sym) == SYMBOL_REF && SYMBOL_REF_SMALL_P (sym))
	emit_insn (gen_symGOTOFF2reg (dest, src,
				      gen_rtx_REG (Pmode, base_regno),
				      GEN_INT (R_FRV_GPREL12)));
      else
	emit_insn (gen_symGOTOFF2reg_hilo (dest, src,
					   gen_rtx_REG (Pmode, base_regno),
					   GEN_INT (R_FRV_GPREL12)));
      if (base_regno == PIC_REGNO)
	cfun->uses_pic_offset_table = TRUE;
      return TRUE;
    }

  if (unspec)
    {
      rtx x;

      /* Since OUR_FDPIC_REG is a pseudo register, we can't safely introduce
	 new uses of it once reload has begun.  */
      gcc_assert (!reload_in_progress && !reload_completed);

      switch (unspec)
	{
	case R_FRV_GOTOFF12:
	  if (!frv_small_data_reloc_p (sym, unspec))
	    x = gen_symGOTOFF2reg_hilo (dest, src, OUR_FDPIC_REG,
					GEN_INT (unspec));
	  else
	    x = gen_symGOTOFF2reg (dest, src, OUR_FDPIC_REG, GEN_INT (unspec));
	  break;
	case R_FRV_GPREL12:
	  if (!frv_small_data_reloc_p (sym, unspec))
	    x = gen_symGPREL2reg_hilo (dest, src, OUR_FDPIC_REG,
				       GEN_INT (unspec));
	  else
	    x = gen_symGPREL2reg (dest, src, OUR_FDPIC_REG, GEN_INT (unspec));
	  break;
	case R_FRV_FUNCDESC_GOTOFF12:
	  if (flag_pic != 1)
	    x = gen_symGOTOFF2reg_hilo (dest, src, OUR_FDPIC_REG,
					GEN_INT (unspec));
	  else
	    x = gen_symGOTOFF2reg (dest, src, OUR_FDPIC_REG, GEN_INT (unspec));
	  break;
	default:
	  if (flag_pic != 1)
	    x = gen_symGOT2reg_hilo (dest, src, OUR_FDPIC_REG,
				     GEN_INT (unspec));
	  else
	    x = gen_symGOT2reg (dest, src, OUR_FDPIC_REG, GEN_INT (unspec));
	  break;
	}
      emit_insn (x);
      cfun->uses_pic_offset_table = TRUE;
      return TRUE;
    }


  return FALSE;
}


/* Return a string to output a single word move.  */

const char *
output_move_single (rtx operands[], rtx insn)
{
  rtx dest = operands[0];
  rtx src  = operands[1];

  if (GET_CODE (dest) == REG)
    {
      int dest_regno = REGNO (dest);
      enum machine_mode mode = GET_MODE (dest);

      if (GPR_P (dest_regno))
	{
	  if (GET_CODE (src) == REG)
	    {
	      /* gpr <- some sort of register */
	      int src_regno = REGNO (src);

	      if (GPR_P (src_regno))
		return "mov %1, %0";

	      else if (FPR_P (src_regno))
		return "movfg %1, %0";

	      else if (SPR_P (src_regno))
		return "movsg %1, %0";
	    }

	  else if (GET_CODE (src) == MEM)
	    {
	      /* gpr <- memory */
	      switch (mode)
		{
		default:
		  break;

		case QImode:
		  return "ldsb%I1%U1 %M1,%0";

		case HImode:
		  return "ldsh%I1%U1 %M1,%0";

		case SImode:
		case SFmode:
		  return "ld%I1%U1 %M1, %0";
		}
	    }

	  else if (GET_CODE (src) == CONST_INT
		   || GET_CODE (src) == CONST_DOUBLE)
	    {
	      /* gpr <- integer/floating constant */
	      HOST_WIDE_INT value;

	      if (GET_CODE (src) == CONST_INT)
		value = INTVAL (src);

	      else if (mode == SFmode)
		{
		  REAL_VALUE_TYPE rv;
		  long l;

		  REAL_VALUE_FROM_CONST_DOUBLE (rv, src);
		  REAL_VALUE_TO_TARGET_SINGLE (rv, l);
		  value = l;
		}

	      else
		value = CONST_DOUBLE_LOW (src);

	      if (IN_RANGE_P (value, -32768, 32767))
		return "setlos %1, %0";

	      return "#";
	    }

          else if (GET_CODE (src) == SYMBOL_REF
		   || GET_CODE (src) == LABEL_REF
		   || GET_CODE (src) == CONST)
	    {
	      return "#";
	    }
	}

      else if (FPR_P (dest_regno))
	{
	  if (GET_CODE (src) == REG)
	    {
	      /* fpr <- some sort of register */
	      int src_regno = REGNO (src);

	      if (GPR_P (src_regno))
		return "movgf %1, %0";

	      else if (FPR_P (src_regno))
		{
		  if (TARGET_HARD_FLOAT)
		    return "fmovs %1, %0";
		  else
		    return "mor %1, %1, %0";
		}
	    }

	  else if (GET_CODE (src) == MEM)
	    {
	      /* fpr <- memory */
	      switch (mode)
		{
		default:
		  break;

		case QImode:
		  return "ldbf%I1%U1 %M1,%0";

		case HImode:
		  return "ldhf%I1%U1 %M1,%0";

		case SImode:
		case SFmode:
		  return "ldf%I1%U1 %M1, %0";
		}
	    }

	  else if (ZERO_P (src))
	    return "movgf %., %0";
	}

      else if (SPR_P (dest_regno))
	{
	  if (GET_CODE (src) == REG)
	    {
	      /* spr <- some sort of register */
	      int src_regno = REGNO (src);

	      if (GPR_P (src_regno))
		return "movgs %1, %0";
	    }
	  else if (ZERO_P (src))
	    return "movgs %., %0";
	}
    }

  else if (GET_CODE (dest) == MEM)
    {
      if (GET_CODE (src) == REG)
	{
	  int src_regno = REGNO (src);
	  enum machine_mode mode = GET_MODE (dest);

	  if (GPR_P (src_regno))
	    {
	      switch (mode)
		{
		default:
		  break;

		case QImode:
		  return "stb%I0%U0 %1, %M0";

		case HImode:
		  return "sth%I0%U0 %1, %M0";

		case SImode:
		case SFmode:
		  return "st%I0%U0 %1, %M0";
		}
	    }

	  else if (FPR_P (src_regno))
	    {
	      switch (mode)
		{
		default:
		  break;

		case QImode:
		  return "stbf%I0%U0 %1, %M0";

		case HImode:
		  return "sthf%I0%U0 %1, %M0";

		case SImode:
		case SFmode:
		  return "stf%I0%U0 %1, %M0";
		}
	    }
	}

      else if (ZERO_P (src))
	{
	  switch (GET_MODE (dest))
	    {
	    default:
	      break;

	    case QImode:
	      return "stb%I0%U0 %., %M0";

	    case HImode:
	      return "sth%I0%U0 %., %M0";

	    case SImode:
	    case SFmode:
	      return "st%I0%U0 %., %M0";
	    }
	}
    }

  fatal_insn ("bad output_move_single operand", insn);
  return "";
}


/* Return a string to output a double word move.  */

const char *
output_move_double (rtx operands[], rtx insn)
{
  rtx dest = operands[0];
  rtx src  = operands[1];
  enum machine_mode mode = GET_MODE (dest);

  if (GET_CODE (dest) == REG)
    {
      int dest_regno = REGNO (dest);

      if (GPR_P (dest_regno))
	{
	  if (GET_CODE (src) == REG)
	    {
	      /* gpr <- some sort of register */
	      int src_regno = REGNO (src);

	      if (GPR_P (src_regno))
		return "#";

	      else if (FPR_P (src_regno))
		{
		  if (((dest_regno - GPR_FIRST) & 1) == 0
		      && ((src_regno - FPR_FIRST) & 1) == 0)
		    return "movfgd %1, %0";

		  return "#";
		}
	    }

	  else if (GET_CODE (src) == MEM)
	    {
	      /* gpr <- memory */
	      if (dbl_memory_one_insn_operand (src, mode))
		return "ldd%I1%U1 %M1, %0";

	      return "#";
	    }

	  else if (GET_CODE (src) == CONST_INT
		   || GET_CODE (src) == CONST_DOUBLE)
	    return "#";
	}

      else if (FPR_P (dest_regno))
	{
	  if (GET_CODE (src) == REG)
	    {
	      /* fpr <- some sort of register */
	      int src_regno = REGNO (src);

	      if (GPR_P (src_regno))
		{
		  if (((dest_regno - FPR_FIRST) & 1) == 0
		      && ((src_regno - GPR_FIRST) & 1) == 0)
		    return "movgfd %1, %0";

		  return "#";
		}

	      else if (FPR_P (src_regno))
		{
		  if (TARGET_DOUBLE
		      && ((dest_regno - FPR_FIRST) & 1) == 0
		      && ((src_regno - FPR_FIRST) & 1) == 0)
		    return "fmovd %1, %0";

		  return "#";
		}
	    }

	  else if (GET_CODE (src) == MEM)
	    {
	      /* fpr <- memory */
	      if (dbl_memory_one_insn_operand (src, mode))
		return "lddf%I1%U1 %M1, %0";

	      return "#";
	    }

	  else if (ZERO_P (src))
	    return "#";
	}
    }

  else if (GET_CODE (dest) == MEM)
    {
      if (GET_CODE (src) == REG)
	{
	  int src_regno = REGNO (src);

	  if (GPR_P (src_regno))
	    {
	      if (((src_regno - GPR_FIRST) & 1) == 0
		  && dbl_memory_one_insn_operand (dest, mode))
		return "std%I0%U0 %1, %M0";

	      return "#";
	    }

	  if (FPR_P (src_regno))
	    {
	      if (((src_regno - FPR_FIRST) & 1) == 0
		  && dbl_memory_one_insn_operand (dest, mode))
		return "stdf%I0%U0 %1, %M0";

	      return "#";
	    }
	}

      else if (ZERO_P (src))
	{
	  if (dbl_memory_one_insn_operand (dest, mode))
	    return "std%I0%U0 %., %M0";

	  return "#";
	}
    }

  fatal_insn ("bad output_move_double operand", insn);
  return "";
}


/* Return a string to output a single word conditional move.
   Operand0 -- EQ/NE of ccr register and 0
   Operand1 -- CCR register
   Operand2 -- destination
   Operand3 -- source  */

const char *
output_condmove_single (rtx operands[], rtx insn)
{
  rtx dest = operands[2];
  rtx src  = operands[3];

  if (GET_CODE (dest) == REG)
    {
      int dest_regno = REGNO (dest);
      enum machine_mode mode = GET_MODE (dest);

      if (GPR_P (dest_regno))
	{
	  if (GET_CODE (src) == REG)
	    {
	      /* gpr <- some sort of register */
	      int src_regno = REGNO (src);

	      if (GPR_P (src_regno))
		return "cmov %z3, %2, %1, %e0";

	      else if (FPR_P (src_regno))
		return "cmovfg %3, %2, %1, %e0";
	    }

	  else if (GET_CODE (src) == MEM)
	    {
	      /* gpr <- memory */
	      switch (mode)
		{
		default:
		  break;

		case QImode:
		  return "cldsb%I3%U3 %M3, %2, %1, %e0";

		case HImode:
		  return "cldsh%I3%U3 %M3, %2, %1, %e0";

		case SImode:
		case SFmode:
		  return "cld%I3%U3 %M3, %2, %1, %e0";
		}
	    }

	  else if (ZERO_P (src))
	    return "cmov %., %2, %1, %e0";
	}

      else if (FPR_P (dest_regno))
	{
	  if (GET_CODE (src) == REG)
	    {
	      /* fpr <- some sort of register */
	      int src_regno = REGNO (src);

	      if (GPR_P (src_regno))
		return "cmovgf %3, %2, %1, %e0";

	      else if (FPR_P (src_regno))
		{
		  if (TARGET_HARD_FLOAT)
		    return "cfmovs %3,%2,%1,%e0";
		  else
		    return "cmor %3, %3, %2, %1, %e0";
		}
	    }

	  else if (GET_CODE (src) == MEM)
	    {
	      /* fpr <- memory */
	      if (mode == SImode || mode == SFmode)
		return "cldf%I3%U3 %M3, %2, %1, %e0";
	    }

	  else if (ZERO_P (src))
	    return "cmovgf %., %2, %1, %e0";
	}
    }

  else if (GET_CODE (dest) == MEM)
    {
      if (GET_CODE (src) == REG)
	{
	  int src_regno = REGNO (src);
	  enum machine_mode mode = GET_MODE (dest);

	  if (GPR_P (src_regno))
	    {
	      switch (mode)
		{
		default:
		  break;

		case QImode:
		  return "cstb%I2%U2 %3, %M2, %1, %e0";

		case HImode:
		  return "csth%I2%U2 %3, %M2, %1, %e0";

		case SImode:
		case SFmode:
		  return "cst%I2%U2 %3, %M2, %1, %e0";
		}
	    }

	  else if (FPR_P (src_regno) && (mode == SImode || mode == SFmode))
	    return "cstf%I2%U2 %3, %M2, %1, %e0";
	}

      else if (ZERO_P (src))
	{
	  enum machine_mode mode = GET_MODE (dest);
	  switch (mode)
	    {
	    default:
	      break;

	    case QImode:
	      return "cstb%I2%U2 %., %M2, %1, %e0";

	    case HImode:
	      return "csth%I2%U2 %., %M2, %1, %e0";

	    case SImode:
	    case SFmode:
	      return "cst%I2%U2 %., %M2, %1, %e0";
	    }
	}
    }

  fatal_insn ("bad output_condmove_single operand", insn);
  return "";
}


/* Emit the appropriate code to do a comparison, returning the register the
   comparison was done it.  */

static rtx
frv_emit_comparison (enum rtx_code test, rtx op0, rtx op1)
{
  enum machine_mode cc_mode;
  rtx cc_reg;

  /* Floating point doesn't have comparison against a constant.  */
  if (GET_MODE (op0) == CC_FPmode && GET_CODE (op1) != REG)
    op1 = force_reg (GET_MODE (op0), op1);

  /* Possibly disable using anything but a fixed register in order to work
     around cse moving comparisons past function calls.  */
  cc_mode = SELECT_CC_MODE (test, op0, op1);
  cc_reg = ((TARGET_ALLOC_CC)
	    ? gen_reg_rtx (cc_mode)
	    : gen_rtx_REG (cc_mode,
			   (cc_mode == CC_FPmode) ? FCC_FIRST : ICC_FIRST));

  emit_insn (gen_rtx_SET (VOIDmode, cc_reg,
			  gen_rtx_COMPARE (cc_mode, op0, op1)));

  return cc_reg;
}


/* Emit code for a conditional branch.  The comparison operands were previously
   stored in frv_compare_op0 and frv_compare_op1.

   XXX: I originally wanted to add a clobber of a CCR register to use in
   conditional execution, but that confuses the rest of the compiler.  */

int
frv_emit_cond_branch (enum rtx_code test, rtx label)
{
  rtx test_rtx;
  rtx label_ref;
  rtx if_else;
  rtx cc_reg = frv_emit_comparison (test, frv_compare_op0, frv_compare_op1);
  enum machine_mode cc_mode = GET_MODE (cc_reg);

  /* Branches generate:
	(set (pc)
	     (if_then_else (<test>, <cc_reg>, (const_int 0))
			    (label_ref <branch_label>)
			    (pc))) */
  label_ref = gen_rtx_LABEL_REF (VOIDmode, label);
  test_rtx = gen_rtx_fmt_ee (test, cc_mode, cc_reg, const0_rtx);
  if_else = gen_rtx_IF_THEN_ELSE (cc_mode, test_rtx, label_ref, pc_rtx);
  emit_jump_insn (gen_rtx_SET (VOIDmode, pc_rtx, if_else));
  return TRUE;
}


/* Emit code to set a gpr to 1/0 based on a comparison.  The comparison
   operands were previously stored in frv_compare_op0 and frv_compare_op1.  */

int
frv_emit_scc (enum rtx_code test, rtx target)
{
  rtx set;
  rtx test_rtx;
  rtx clobber;
  rtx cr_reg;
  rtx cc_reg = frv_emit_comparison (test, frv_compare_op0, frv_compare_op1);

  /* SCC instructions generate:
	(parallel [(set <target> (<test>, <cc_reg>, (const_int 0))
		   (clobber (<ccr_reg>))])  */
  test_rtx = gen_rtx_fmt_ee (test, SImode, cc_reg, const0_rtx);
  set = gen_rtx_SET (VOIDmode, target, test_rtx);

  cr_reg = ((TARGET_ALLOC_CC)
	    ? gen_reg_rtx (CC_CCRmode)
	    : gen_rtx_REG (CC_CCRmode,
			   ((GET_MODE (cc_reg) == CC_FPmode)
			    ? FCR_FIRST
			    : ICR_FIRST)));

  clobber = gen_rtx_CLOBBER (VOIDmode, cr_reg);
  emit_insn (gen_rtx_PARALLEL (VOIDmode, gen_rtvec (2, set, clobber)));
  return TRUE;
}


/* Split a SCC instruction into component parts, returning a SEQUENCE to hold
   the separate insns.  */

rtx
frv_split_scc (rtx dest, rtx test, rtx cc_reg, rtx cr_reg, HOST_WIDE_INT value)
{
  rtx ret;

  start_sequence ();

  /* Set the appropriate CCR bit.  */
  emit_insn (gen_rtx_SET (VOIDmode,
			  cr_reg,
			  gen_rtx_fmt_ee (GET_CODE (test),
					  GET_MODE (cr_reg),
					  cc_reg,
					  const0_rtx)));

  /* Move the value into the destination.  */
  emit_move_insn (dest, GEN_INT (value));

  /* Move 0 into the destination if the test failed */
  emit_insn (gen_rtx_COND_EXEC (VOIDmode,
				gen_rtx_EQ (GET_MODE (cr_reg),
					    cr_reg,
					    const0_rtx),
				gen_rtx_SET (VOIDmode, dest, const0_rtx)));

  /* Finish up, return sequence.  */
  ret = get_insns ();
  end_sequence ();
  return ret;
}


/* Emit the code for a conditional move, return TRUE if we could do the
   move.  */

int
frv_emit_cond_move (rtx dest, rtx test_rtx, rtx src1, rtx src2)
{
  rtx set;
  rtx clobber_cc;
  rtx test2;
  rtx cr_reg;
  rtx if_rtx;
  enum rtx_code test = GET_CODE (test_rtx);
  rtx cc_reg = frv_emit_comparison (test, frv_compare_op0, frv_compare_op1);
  enum machine_mode cc_mode = GET_MODE (cc_reg);

  /* Conditional move instructions generate:
	(parallel [(set <target>
			(if_then_else (<test> <cc_reg> (const_int 0))
				      <src1>
				      <src2>))
		   (clobber (<ccr_reg>))])  */

  /* Handle various cases of conditional move involving two constants.  */
  if (GET_CODE (src1) == CONST_INT && GET_CODE (src2) == CONST_INT)
    {
      HOST_WIDE_INT value1 = INTVAL (src1);
      HOST_WIDE_INT value2 = INTVAL (src2);

      /* Having 0 as one of the constants can be done by loading the other
         constant, and optionally moving in gr0.  */
      if (value1 == 0 || value2 == 0)
	;

      /* If the first value is within an addi range and also the difference
         between the two fits in an addi's range, load up the difference, then
         conditionally move in 0, and then unconditionally add the first
	 value.  */
      else if (IN_RANGE_P (value1, -2048, 2047)
	       && IN_RANGE_P (value2 - value1, -2048, 2047))
	;

      /* If neither condition holds, just force the constant into a
	 register.  */
      else
	{
	  src1 = force_reg (GET_MODE (dest), src1);
	  src2 = force_reg (GET_MODE (dest), src2);
	}
    }

  /* If one value is a register, insure the other value is either 0 or a
     register.  */
  else
    {
      if (GET_CODE (src1) == CONST_INT && INTVAL (src1) != 0)
	src1 = force_reg (GET_MODE (dest), src1);

      if (GET_CODE (src2) == CONST_INT && INTVAL (src2) != 0)
	src2 = force_reg (GET_MODE (dest), src2);
    }

  test2 = gen_rtx_fmt_ee (test, cc_mode, cc_reg, const0_rtx);
  if_rtx = gen_rtx_IF_THEN_ELSE (GET_MODE (dest), test2, src1, src2);

  set = gen_rtx_SET (VOIDmode, dest, if_rtx);

  cr_reg = ((TARGET_ALLOC_CC)
	    ? gen_reg_rtx (CC_CCRmode)
	    : gen_rtx_REG (CC_CCRmode,
			   (cc_mode == CC_FPmode) ? FCR_FIRST : ICR_FIRST));

  clobber_cc = gen_rtx_CLOBBER (VOIDmode, cr_reg);
  emit_insn (gen_rtx_PARALLEL (VOIDmode, gen_rtvec (2, set, clobber_cc)));
  return TRUE;
}


/* Split a conditional move into constituent parts, returning a SEQUENCE
   containing all of the insns.  */

rtx
frv_split_cond_move (rtx operands[])
{
  rtx dest	= operands[0];
  rtx test	= operands[1];
  rtx cc_reg	= operands[2];
  rtx src1	= operands[3];
  rtx src2	= operands[4];
  rtx cr_reg	= operands[5];
  rtx ret;
  enum machine_mode cr_mode = GET_MODE (cr_reg);

  start_sequence ();

  /* Set the appropriate CCR bit.  */
  emit_insn (gen_rtx_SET (VOIDmode,
			  cr_reg,
			  gen_rtx_fmt_ee (GET_CODE (test),
					  GET_MODE (cr_reg),
					  cc_reg,
					  const0_rtx)));

  /* Handle various cases of conditional move involving two constants.  */
  if (GET_CODE (src1) == CONST_INT && GET_CODE (src2) == CONST_INT)
    {
      HOST_WIDE_INT value1 = INTVAL (src1);
      HOST_WIDE_INT value2 = INTVAL (src2);

      /* Having 0 as one of the constants can be done by loading the other
         constant, and optionally moving in gr0.  */
      if (value1 == 0)
	{
	  emit_move_insn (dest, src2);
	  emit_insn (gen_rtx_COND_EXEC (VOIDmode,
					gen_rtx_NE (cr_mode, cr_reg,
						    const0_rtx),
					gen_rtx_SET (VOIDmode, dest, src1)));
	}

      else if (value2 == 0)
	{
	  emit_move_insn (dest, src1);
	  emit_insn (gen_rtx_COND_EXEC (VOIDmode,
					gen_rtx_EQ (cr_mode, cr_reg,
						    const0_rtx),
					gen_rtx_SET (VOIDmode, dest, src2)));
	}

      /* If the first value is within an addi range and also the difference
         between the two fits in an addi's range, load up the difference, then
         conditionally move in 0, and then unconditionally add the first
	 value.  */
      else if (IN_RANGE_P (value1, -2048, 2047)
	       && IN_RANGE_P (value2 - value1, -2048, 2047))
	{
	  rtx dest_si = ((GET_MODE (dest) == SImode)
			 ? dest
			 : gen_rtx_SUBREG (SImode, dest, 0));

	  emit_move_insn (dest_si, GEN_INT (value2 - value1));
	  emit_insn (gen_rtx_COND_EXEC (VOIDmode,
					gen_rtx_NE (cr_mode, cr_reg,
						    const0_rtx),
					gen_rtx_SET (VOIDmode, dest_si,
						     const0_rtx)));
	  emit_insn (gen_addsi3 (dest_si, dest_si, src1));
	}

      else
	gcc_unreachable ();
    }
  else
    {
      /* Emit the conditional move for the test being true if needed.  */
      if (! rtx_equal_p (dest, src1))
	emit_insn (gen_rtx_COND_EXEC (VOIDmode,
				      gen_rtx_NE (cr_mode, cr_reg, const0_rtx),
				      gen_rtx_SET (VOIDmode, dest, src1)));

      /* Emit the conditional move for the test being false if needed.  */
      if (! rtx_equal_p (dest, src2))
	emit_insn (gen_rtx_COND_EXEC (VOIDmode,
				      gen_rtx_EQ (cr_mode, cr_reg, const0_rtx),
				      gen_rtx_SET (VOIDmode, dest, src2)));
    }

  /* Finish up, return sequence.  */
  ret = get_insns ();
  end_sequence ();
  return ret;
}


/* Split (set DEST SOURCE), where DEST is a double register and SOURCE is a
   memory location that is not known to be dword-aligned.  */
void
frv_split_double_load (rtx dest, rtx source)
{
  int regno = REGNO (dest);
  rtx dest1 = gen_highpart (SImode, dest);
  rtx dest2 = gen_lowpart (SImode, dest);
  rtx address = XEXP (source, 0);

  /* If the address is pre-modified, load the lower-numbered register
     first, then load the other register using an integer offset from
     the modified base register.  This order should always be safe,
     since the pre-modification cannot affect the same registers as the
     load does.

     The situation for other loads is more complicated.  Loading one
     of the registers could affect the value of ADDRESS, so we must
     be careful which order we do them in.  */
  if (GET_CODE (address) == PRE_MODIFY
      || ! refers_to_regno_p (regno, regno + 1, address, NULL))
    {
      /* It is safe to load the lower-numbered register first.  */
      emit_move_insn (dest1, change_address (source, SImode, NULL));
      emit_move_insn (dest2, frv_index_memory (source, SImode, 1));
    }
  else
    {
      /* ADDRESS is not pre-modified and the address depends on the
         lower-numbered register.  Load the higher-numbered register
         first.  */
      emit_move_insn (dest2, frv_index_memory (source, SImode, 1));
      emit_move_insn (dest1, change_address (source, SImode, NULL));
    }
}

/* Split (set DEST SOURCE), where DEST refers to a dword memory location
   and SOURCE is either a double register or the constant zero.  */
void
frv_split_double_store (rtx dest, rtx source)
{
  rtx dest1 = change_address (dest, SImode, NULL);
  rtx dest2 = frv_index_memory (dest, SImode, 1);
  if (ZERO_P (source))
    {
      emit_move_insn (dest1, CONST0_RTX (SImode));
      emit_move_insn (dest2, CONST0_RTX (SImode));
    }
  else
    {
      emit_move_insn (dest1, gen_highpart (SImode, source));
      emit_move_insn (dest2, gen_lowpart (SImode, source));
    }
}


/* Split a min/max operation returning a SEQUENCE containing all of the
   insns.  */

rtx
frv_split_minmax (rtx operands[])
{
  rtx dest	= operands[0];
  rtx minmax	= operands[1];
  rtx src1	= operands[2];
  rtx src2	= operands[3];
  rtx cc_reg	= operands[4];
  rtx cr_reg	= operands[5];
  rtx ret;
  enum rtx_code test_code;
  enum machine_mode cr_mode = GET_MODE (cr_reg);

  start_sequence ();

  /* Figure out which test to use.  */
  switch (GET_CODE (minmax))
    {
    default:
      gcc_unreachable ();

    case SMIN: test_code = LT;  break;
    case SMAX: test_code = GT;  break;
    case UMIN: test_code = LTU; break;
    case UMAX: test_code = GTU; break;
    }

  /* Issue the compare instruction.  */
  emit_insn (gen_rtx_SET (VOIDmode,
			  cc_reg,
			  gen_rtx_COMPARE (GET_MODE (cc_reg),
					   src1, src2)));

  /* Set the appropriate CCR bit.  */
  emit_insn (gen_rtx_SET (VOIDmode,
			  cr_reg,
			  gen_rtx_fmt_ee (test_code,
					  GET_MODE (cr_reg),
					  cc_reg,
					  const0_rtx)));

  /* If are taking the min/max of a nonzero constant, load that first, and
     then do a conditional move of the other value.  */
  if (GET_CODE (src2) == CONST_INT && INTVAL (src2) != 0)
    {
      gcc_assert (!rtx_equal_p (dest, src1));

      emit_move_insn (dest, src2);
      emit_insn (gen_rtx_COND_EXEC (VOIDmode,
				    gen_rtx_NE (cr_mode, cr_reg, const0_rtx),
				    gen_rtx_SET (VOIDmode, dest, src1)));
    }

  /* Otherwise, do each half of the move.  */
  else
    {
      /* Emit the conditional move for the test being true if needed.  */
      if (! rtx_equal_p (dest, src1))
	emit_insn (gen_rtx_COND_EXEC (VOIDmode,
				      gen_rtx_NE (cr_mode, cr_reg, const0_rtx),
				      gen_rtx_SET (VOIDmode, dest, src1)));

      /* Emit the conditional move for the test being false if needed.  */
      if (! rtx_equal_p (dest, src2))
	emit_insn (gen_rtx_COND_EXEC (VOIDmode,
				      gen_rtx_EQ (cr_mode, cr_reg, const0_rtx),
				      gen_rtx_SET (VOIDmode, dest, src2)));
    }

  /* Finish up, return sequence.  */
  ret = get_insns ();
  end_sequence ();
  return ret;
}


/* Split an integer abs operation returning a SEQUENCE containing all of the
   insns.  */

rtx
frv_split_abs (rtx operands[])
{
  rtx dest	= operands[0];
  rtx src	= operands[1];
  rtx cc_reg	= operands[2];
  rtx cr_reg	= operands[3];
  rtx ret;

  start_sequence ();

  /* Issue the compare < 0 instruction.  */
  emit_insn (gen_rtx_SET (VOIDmode,
			  cc_reg,
			  gen_rtx_COMPARE (CCmode, src, const0_rtx)));

  /* Set the appropriate CCR bit.  */
  emit_insn (gen_rtx_SET (VOIDmode,
			  cr_reg,
			  gen_rtx_fmt_ee (LT, CC_CCRmode, cc_reg, const0_rtx)));

  /* Emit the conditional negate if the value is negative.  */
  emit_insn (gen_rtx_COND_EXEC (VOIDmode,
				gen_rtx_NE (CC_CCRmode, cr_reg, const0_rtx),
				gen_negsi2 (dest, src)));

  /* Emit the conditional move for the test being false if needed.  */
  if (! rtx_equal_p (dest, src))
    emit_insn (gen_rtx_COND_EXEC (VOIDmode,
				  gen_rtx_EQ (CC_CCRmode, cr_reg, const0_rtx),
				  gen_rtx_SET (VOIDmode, dest, src)));

  /* Finish up, return sequence.  */
  ret = get_insns ();
  end_sequence ();
  return ret;
}


/* An internal function called by for_each_rtx to clear in a hard_reg set each
   register used in an insn.  */

static int
frv_clear_registers_used (rtx *ptr, void *data)
{
  if (GET_CODE (*ptr) == REG)
    {
      int regno = REGNO (*ptr);
      HARD_REG_SET *p_regs = (HARD_REG_SET *)data;

      if (regno < FIRST_PSEUDO_REGISTER)
	{
	  int reg_max = regno + HARD_REGNO_NREGS (regno, GET_MODE (*ptr));

	  while (regno < reg_max)
	    {
	      CLEAR_HARD_REG_BIT (*p_regs, regno);
	      regno++;
	    }
	}
    }

  return 0;
}


/* Initialize the extra fields provided by IFCVT_EXTRA_FIELDS.  */

/* On the FR-V, we don't have any extra fields per se, but it is useful hook to
   initialize the static storage.  */
void
frv_ifcvt_init_extra_fields (ce_if_block_t *ce_info ATTRIBUTE_UNUSED)
{
  frv_ifcvt.added_insns_list = NULL_RTX;
  frv_ifcvt.cur_scratch_regs = 0;
  frv_ifcvt.num_nested_cond_exec = 0;
  frv_ifcvt.cr_reg = NULL_RTX;
  frv_ifcvt.nested_cc_reg = NULL_RTX;
  frv_ifcvt.extra_int_cr = NULL_RTX;
  frv_ifcvt.extra_fp_cr = NULL_RTX;
  frv_ifcvt.last_nested_if_cr = NULL_RTX;
}


/* Internal function to add a potential insn to the list of insns to be inserted
   if the conditional execution conversion is successful.  */

static void
frv_ifcvt_add_insn (rtx pattern, rtx insn, int before_p)
{
  rtx link = alloc_EXPR_LIST (VOIDmode, pattern, insn);

  link->jump = before_p;	/* Mark to add this before or after insn.  */
  frv_ifcvt.added_insns_list = alloc_EXPR_LIST (VOIDmode, link,
						frv_ifcvt.added_insns_list);

  if (TARGET_DEBUG_COND_EXEC)
    {
      fprintf (stderr,
	       "\n:::::::::: frv_ifcvt_add_insn: add the following %s insn %d:\n",
	       (before_p) ? "before" : "after",
	       (int)INSN_UID (insn));

      debug_rtx (pattern);
    }
}


/* A C expression to modify the code described by the conditional if
   information CE_INFO, possibly updating the tests in TRUE_EXPR, and
   FALSE_EXPR for converting if-then and if-then-else code to conditional
   instructions.  Set either TRUE_EXPR or FALSE_EXPR to a null pointer if the
   tests cannot be converted.  */

void
frv_ifcvt_modify_tests (ce_if_block_t *ce_info, rtx *p_true, rtx *p_false)
{
  basic_block test_bb = ce_info->test_bb;	/* test basic block */
  basic_block then_bb = ce_info->then_bb;	/* THEN */
  basic_block else_bb = ce_info->else_bb;	/* ELSE or NULL */
  basic_block join_bb = ce_info->join_bb;	/* join block or NULL */
  rtx true_expr = *p_true;
  rtx cr;
  rtx cc;
  rtx nested_cc;
  enum machine_mode mode = GET_MODE (true_expr);
  int j;
  basic_block *bb;
  int num_bb;
  frv_tmp_reg_t *tmp_reg = &frv_ifcvt.tmp_reg;
  rtx check_insn;
  rtx sub_cond_exec_reg;
  enum rtx_code code;
  enum rtx_code code_true;
  enum rtx_code code_false;
  enum reg_class cc_class;
  enum reg_class cr_class;
  int cc_first;
  int cc_last;
  reg_set_iterator rsi;

  /* Make sure we are only dealing with hard registers.  Also honor the
     -mno-cond-exec switch, and -mno-nested-cond-exec switches if
     applicable.  */
  if (!reload_completed || !TARGET_COND_EXEC
      || (!TARGET_NESTED_CE && ce_info->pass > 1))
    goto fail;

  /* Figure out which registers we can allocate for our own purposes.  Only
     consider registers that are not preserved across function calls and are
     not fixed.  However, allow the ICC/ICR temporary registers to be allocated
     if we did not need to use them in reloading other registers.  */
  memset (&tmp_reg->regs, 0, sizeof (tmp_reg->regs));
  COPY_HARD_REG_SET (tmp_reg->regs, call_used_reg_set);
  AND_COMPL_HARD_REG_SET (tmp_reg->regs, fixed_reg_set);
  SET_HARD_REG_BIT (tmp_reg->regs, ICC_TEMP);
  SET_HARD_REG_BIT (tmp_reg->regs, ICR_TEMP);

  /* If this is a nested IF, we need to discover whether the CC registers that
     are set/used inside of the block are used anywhere else.  If not, we can
     change them to be the CC register that is paired with the CR register that
     controls the outermost IF block.  */
  if (ce_info->pass > 1)
    {
      CLEAR_HARD_REG_SET (frv_ifcvt.nested_cc_ok_rewrite);
      for (j = CC_FIRST; j <= CC_LAST; j++)
	if (TEST_HARD_REG_BIT (tmp_reg->regs, j))
	  {
	    if (REGNO_REG_SET_P (then_bb->il.rtl->global_live_at_start, j))
	      continue;

	    if (else_bb
		&& REGNO_REG_SET_P (else_bb->il.rtl->global_live_at_start, j))
	      continue;

	    if (join_bb
		&& REGNO_REG_SET_P (join_bb->il.rtl->global_live_at_start, j))
	      continue;

	    SET_HARD_REG_BIT (frv_ifcvt.nested_cc_ok_rewrite, j);
	  }
    }

  for (j = 0; j < frv_ifcvt.cur_scratch_regs; j++)
    frv_ifcvt.scratch_regs[j] = NULL_RTX;

  frv_ifcvt.added_insns_list = NULL_RTX;
  frv_ifcvt.cur_scratch_regs = 0;

  bb = (basic_block *) alloca ((2 + ce_info->num_multiple_test_blocks)
			       * sizeof (basic_block));

  if (join_bb)
    {
      unsigned int regno;

      /* Remove anything live at the beginning of the join block from being
         available for allocation.  */
      EXECUTE_IF_SET_IN_REG_SET (join_bb->il.rtl->global_live_at_start, 0, regno, rsi)
	{
	  if (regno < FIRST_PSEUDO_REGISTER)
	    CLEAR_HARD_REG_BIT (tmp_reg->regs, regno);
	}
    }

  /* Add in all of the blocks in multiple &&/|| blocks to be scanned.  */
  num_bb = 0;
  if (ce_info->num_multiple_test_blocks)
    {
      basic_block multiple_test_bb = ce_info->last_test_bb;

      while (multiple_test_bb != test_bb)
	{
	  bb[num_bb++] = multiple_test_bb;
	  multiple_test_bb = EDGE_PRED (multiple_test_bb, 0)->src;
	}
    }

  /* Add in the THEN and ELSE blocks to be scanned.  */
  bb[num_bb++] = then_bb;
  if (else_bb)
    bb[num_bb++] = else_bb;

  sub_cond_exec_reg = NULL_RTX;
  frv_ifcvt.num_nested_cond_exec = 0;

  /* Scan all of the blocks for registers that must not be allocated.  */
  for (j = 0; j < num_bb; j++)
    {
      rtx last_insn = BB_END (bb[j]);
      rtx insn = BB_HEAD (bb[j]);
      unsigned int regno;

      if (dump_file)
	fprintf (dump_file, "Scanning %s block %d, start %d, end %d\n",
		 (bb[j] == else_bb) ? "else" : ((bb[j] == then_bb) ? "then" : "test"),
		 (int) bb[j]->index,
		 (int) INSN_UID (BB_HEAD (bb[j])),
		 (int) INSN_UID (BB_END (bb[j])));

      /* Anything live at the beginning of the block is obviously unavailable
         for allocation.  */
      EXECUTE_IF_SET_IN_REG_SET (bb[j]->il.rtl->global_live_at_start, 0, regno, rsi)
	{
	  if (regno < FIRST_PSEUDO_REGISTER)
	    CLEAR_HARD_REG_BIT (tmp_reg->regs, regno);
	}

      /* Loop through the insns in the block.  */
      for (;;)
	{
	  /* Mark any new registers that are created as being unavailable for
             allocation.  Also see if the CC register used in nested IFs can be
             reallocated.  */
	  if (INSN_P (insn))
	    {
	      rtx pattern;
	      rtx set;
	      int skip_nested_if = FALSE;

	      for_each_rtx (&PATTERN (insn), frv_clear_registers_used,
			    (void *)&tmp_reg->regs);

	      pattern = PATTERN (insn);
	      if (GET_CODE (pattern) == COND_EXEC)
		{
		  rtx reg = XEXP (COND_EXEC_TEST (pattern), 0);

		  if (reg != sub_cond_exec_reg)
		    {
		      sub_cond_exec_reg = reg;
		      frv_ifcvt.num_nested_cond_exec++;
		    }
		}

	      set = single_set_pattern (pattern);
	      if (set)
		{
		  rtx dest = SET_DEST (set);
		  rtx src = SET_SRC (set);

		  if (GET_CODE (dest) == REG)
		    {
		      int regno = REGNO (dest);
		      enum rtx_code src_code = GET_CODE (src);

		      if (CC_P (regno) && src_code == COMPARE)
			skip_nested_if = TRUE;

		      else if (CR_P (regno)
			       && (src_code == IF_THEN_ELSE
				   || COMPARISON_P (src)))
			skip_nested_if = TRUE;
		    }
		}

	      if (! skip_nested_if)
		for_each_rtx (&PATTERN (insn), frv_clear_registers_used,
			      (void *)&frv_ifcvt.nested_cc_ok_rewrite);
	    }

	  if (insn == last_insn)
	    break;

	  insn = NEXT_INSN (insn);
	}
    }

  /* If this is a nested if, rewrite the CC registers that are available to
     include the ones that can be rewritten, to increase the chance of being
     able to allocate a paired CC/CR register combination.  */
  if (ce_info->pass > 1)
    {
      for (j = CC_FIRST; j <= CC_LAST; j++)
	if (TEST_HARD_REG_BIT (frv_ifcvt.nested_cc_ok_rewrite, j))
	  SET_HARD_REG_BIT (tmp_reg->regs, j);
	else
	  CLEAR_HARD_REG_BIT (tmp_reg->regs, j);
    }

  if (dump_file)
    {
      int num_gprs = 0;
      fprintf (dump_file, "Available GPRs: ");

      for (j = GPR_FIRST; j <= GPR_LAST; j++)
	if (TEST_HARD_REG_BIT (tmp_reg->regs, j))
	  {
	    fprintf (dump_file, " %d [%s]", j, reg_names[j]);
	    if (++num_gprs > GPR_TEMP_NUM+2)
	      break;
	  }

      fprintf (dump_file, "%s\nAvailable CRs:  ",
	       (num_gprs > GPR_TEMP_NUM+2) ? " ..." : "");

      for (j = CR_FIRST; j <= CR_LAST; j++)
	if (TEST_HARD_REG_BIT (tmp_reg->regs, j))
	  fprintf (dump_file, " %d [%s]", j, reg_names[j]);

      fputs ("\n", dump_file);

      if (ce_info->pass > 1)
	{
	  fprintf (dump_file, "Modifiable CCs: ");
	  for (j = CC_FIRST; j <= CC_LAST; j++)
	    if (TEST_HARD_REG_BIT (tmp_reg->regs, j))
	      fprintf (dump_file, " %d [%s]", j, reg_names[j]);

	  fprintf (dump_file, "\n%d nested COND_EXEC statements\n",
		   frv_ifcvt.num_nested_cond_exec);
	}
    }

  /* Allocate the appropriate temporary condition code register.  Try to
     allocate the ICR/FCR register that corresponds to the ICC/FCC register so
     that conditional cmp's can be done.  */
  if (mode == CCmode || mode == CC_UNSmode || mode == CC_NZmode)
    {
      cr_class = ICR_REGS;
      cc_class = ICC_REGS;
      cc_first = ICC_FIRST;
      cc_last = ICC_LAST;
    }
  else if (mode == CC_FPmode)
    {
      cr_class = FCR_REGS;
      cc_class = FCC_REGS;
      cc_first = FCC_FIRST;
      cc_last = FCC_LAST;
    }
  else
    {
      cc_first = cc_last = 0;
      cr_class = cc_class = NO_REGS;
    }

  cc = XEXP (true_expr, 0);
  nested_cc = cr = NULL_RTX;
  if (cc_class != NO_REGS)
    {
      /* For nested IFs and &&/||, see if we can find a CC and CR register pair
         so we can execute a csubcc/caddcc/cfcmps instruction.  */
      int cc_regno;

      for (cc_regno = cc_first; cc_regno <= cc_last; cc_regno++)
	{
	  int cr_regno = cc_regno - CC_FIRST + CR_FIRST;

	  if (TEST_HARD_REG_BIT (frv_ifcvt.tmp_reg.regs, cc_regno)
	      && TEST_HARD_REG_BIT (frv_ifcvt.tmp_reg.regs, cr_regno))
	    {
	      frv_ifcvt.tmp_reg.next_reg[ (int)cr_class ] = cr_regno;
	      cr = frv_alloc_temp_reg (tmp_reg, cr_class, CC_CCRmode, TRUE,
				       TRUE);

	      frv_ifcvt.tmp_reg.next_reg[ (int)cc_class ] = cc_regno;
	      nested_cc = frv_alloc_temp_reg (tmp_reg, cc_class, CCmode,
						  TRUE, TRUE);
	      break;
	    }
	}
    }

  if (! cr)
    {
      if (dump_file)
	fprintf (dump_file, "Could not allocate a CR temporary register\n");

      goto fail;
    }

  if (dump_file)
    fprintf (dump_file,
	     "Will use %s for conditional execution, %s for nested comparisons\n",
	     reg_names[ REGNO (cr)],
	     (nested_cc) ? reg_names[ REGNO (nested_cc) ] : "<none>");

  /* Set the CCR bit.  Note for integer tests, we reverse the condition so that
     in an IF-THEN-ELSE sequence, we are testing the TRUE case against the CCR
     bit being true.  We don't do this for floating point, because of NaNs.  */
  code = GET_CODE (true_expr);
  if (GET_MODE (cc) != CC_FPmode)
    {
      code = reverse_condition (code);
      code_true = EQ;
      code_false = NE;
    }
  else
    {
      code_true = NE;
      code_false = EQ;
    }

  check_insn = gen_rtx_SET (VOIDmode, cr,
			    gen_rtx_fmt_ee (code, CC_CCRmode, cc, const0_rtx));

  /* Record the check insn to be inserted later.  */
  frv_ifcvt_add_insn (check_insn, BB_END (test_bb), TRUE);

  /* Update the tests.  */
  frv_ifcvt.cr_reg = cr;
  frv_ifcvt.nested_cc_reg = nested_cc;
  *p_true = gen_rtx_fmt_ee (code_true, CC_CCRmode, cr, const0_rtx);
  *p_false = gen_rtx_fmt_ee (code_false, CC_CCRmode, cr, const0_rtx);
  return;

  /* Fail, don't do this conditional execution.  */
 fail:
  *p_true = NULL_RTX;
  *p_false = NULL_RTX;
  if (dump_file)
    fprintf (dump_file, "Disabling this conditional execution.\n");

  return;
}


/* A C expression to modify the code described by the conditional if
   information CE_INFO, for the basic block BB, possibly updating the tests in
   TRUE_EXPR, and FALSE_EXPR for converting the && and || parts of if-then or
   if-then-else code to conditional instructions.  Set either TRUE_EXPR or
   FALSE_EXPR to a null pointer if the tests cannot be converted.  */

/* p_true and p_false are given expressions of the form:

	(and (eq:CC_CCR (reg:CC_CCR)
			(const_int 0))
	     (eq:CC (reg:CC)
		    (const_int 0))) */

void
frv_ifcvt_modify_multiple_tests (ce_if_block_t *ce_info,
                                 basic_block bb,
                                 rtx *p_true,
                                 rtx *p_false)
{
  rtx old_true = XEXP (*p_true, 0);
  rtx old_false = XEXP (*p_false, 0);
  rtx true_expr = XEXP (*p_true, 1);
  rtx false_expr = XEXP (*p_false, 1);
  rtx test_expr;
  rtx old_test;
  rtx cr = XEXP (old_true, 0);
  rtx check_insn;
  rtx new_cr = NULL_RTX;
  rtx *p_new_cr = (rtx *)0;
  rtx if_else;
  rtx compare;
  rtx cc;
  enum reg_class cr_class;
  enum machine_mode mode = GET_MODE (true_expr);
  rtx (*logical_func)(rtx, rtx, rtx);

  if (TARGET_DEBUG_COND_EXEC)
    {
      fprintf (stderr,
	       "\n:::::::::: frv_ifcvt_modify_multiple_tests, before modification for %s\ntrue insn:\n",
	       ce_info->and_and_p ? "&&" : "||");

      debug_rtx (*p_true);

      fputs ("\nfalse insn:\n", stderr);
      debug_rtx (*p_false);
    }

  if (!TARGET_MULTI_CE)
    goto fail;

  if (GET_CODE (cr) != REG)
    goto fail;

  if (mode == CCmode || mode == CC_UNSmode || mode == CC_NZmode)
    {
      cr_class = ICR_REGS;
      p_new_cr = &frv_ifcvt.extra_int_cr;
    }
  else if (mode == CC_FPmode)
    {
      cr_class = FCR_REGS;
      p_new_cr = &frv_ifcvt.extra_fp_cr;
    }
  else
    goto fail;

  /* Allocate a temp CR, reusing a previously allocated temp CR if we have 3 or
     more &&/|| tests.  */
  new_cr = *p_new_cr;
  if (! new_cr)
    {
      new_cr = *p_new_cr = frv_alloc_temp_reg (&frv_ifcvt.tmp_reg, cr_class,
					       CC_CCRmode, TRUE, TRUE);
      if (! new_cr)
	goto fail;
    }

  if (ce_info->and_and_p)
    {
      old_test = old_false;
      test_expr = true_expr;
      logical_func = (GET_CODE (old_true) == EQ) ? gen_andcr : gen_andncr;
      *p_true = gen_rtx_NE (CC_CCRmode, cr, const0_rtx);
      *p_false = gen_rtx_EQ (CC_CCRmode, cr, const0_rtx);
    }
  else
    {
      old_test = old_false;
      test_expr = false_expr;
      logical_func = (GET_CODE (old_false) == EQ) ? gen_orcr : gen_orncr;
      *p_true = gen_rtx_EQ (CC_CCRmode, cr, const0_rtx);
      *p_false = gen_rtx_NE (CC_CCRmode, cr, const0_rtx);
    }

  /* First add the andcr/andncr/orcr/orncr, which will be added after the
     conditional check instruction, due to frv_ifcvt_add_insn being a LIFO
     stack.  */
  frv_ifcvt_add_insn ((*logical_func) (cr, cr, new_cr), BB_END (bb), TRUE);

  /* Now add the conditional check insn.  */
  cc = XEXP (test_expr, 0);
  compare = gen_rtx_fmt_ee (GET_CODE (test_expr), CC_CCRmode, cc, const0_rtx);
  if_else = gen_rtx_IF_THEN_ELSE (CC_CCRmode, old_test, compare, const0_rtx);

  check_insn = gen_rtx_SET (VOIDmode, new_cr, if_else);

  /* Add the new check insn to the list of check insns that need to be
     inserted.  */
  frv_ifcvt_add_insn (check_insn, BB_END (bb), TRUE);

  if (TARGET_DEBUG_COND_EXEC)
    {
      fputs ("\n:::::::::: frv_ifcvt_modify_multiple_tests, after modification\ntrue insn:\n",
	     stderr);

      debug_rtx (*p_true);

      fputs ("\nfalse insn:\n", stderr);
      debug_rtx (*p_false);
    }

  return;

 fail:
  *p_true = *p_false = NULL_RTX;

  /* If we allocated a CR register, release it.  */
  if (new_cr)
    {
      CLEAR_HARD_REG_BIT (frv_ifcvt.tmp_reg.regs, REGNO (new_cr));
      *p_new_cr = NULL_RTX;
    }

  if (TARGET_DEBUG_COND_EXEC)
    fputs ("\n:::::::::: frv_ifcvt_modify_multiple_tests, failed.\n", stderr);

  return;
}


/* Return a register which will be loaded with a value if an IF block is
   converted to conditional execution.  This is used to rewrite instructions
   that use constants to ones that just use registers.  */

static rtx
frv_ifcvt_load_value (rtx value, rtx insn ATTRIBUTE_UNUSED)
{
  int num_alloc = frv_ifcvt.cur_scratch_regs;
  int i;
  rtx reg;

  /* We know gr0 == 0, so replace any errant uses.  */
  if (value == const0_rtx)
    return gen_rtx_REG (SImode, GPR_FIRST);

  /* First search all registers currently loaded to see if we have an
     applicable constant.  */
  if (CONSTANT_P (value)
      || (GET_CODE (value) == REG && REGNO (value) == LR_REGNO))
    {
      for (i = 0; i < num_alloc; i++)
	{
	  if (rtx_equal_p (SET_SRC (frv_ifcvt.scratch_regs[i]), value))
	    return SET_DEST (frv_ifcvt.scratch_regs[i]);
	}
    }

  /* Have we exhausted the number of registers available?  */
  if (num_alloc >= GPR_TEMP_NUM)
    {
      if (dump_file)
	fprintf (dump_file, "Too many temporary registers allocated\n");

      return NULL_RTX;
    }

  /* Allocate the new register.  */
  reg = frv_alloc_temp_reg (&frv_ifcvt.tmp_reg, GPR_REGS, SImode, TRUE, TRUE);
  if (! reg)
    {
      if (dump_file)
	fputs ("Could not find a scratch register\n", dump_file);

      return NULL_RTX;
    }

  frv_ifcvt.cur_scratch_regs++;
  frv_ifcvt.scratch_regs[num_alloc] = gen_rtx_SET (VOIDmode, reg, value);

  if (dump_file)
    {
      if (GET_CODE (value) == CONST_INT)
	fprintf (dump_file, "Register %s will hold %ld\n",
		 reg_names[ REGNO (reg)], (long)INTVAL (value));

      else if (GET_CODE (value) == REG && REGNO (value) == LR_REGNO)
	fprintf (dump_file, "Register %s will hold LR\n",
		 reg_names[ REGNO (reg)]);

      else
	fprintf (dump_file, "Register %s will hold a saved value\n",
		 reg_names[ REGNO (reg)]);
    }

  return reg;
}


/* Update a MEM used in conditional code that might contain an offset to put
   the offset into a scratch register, so that the conditional load/store
   operations can be used.  This function returns the original pointer if the
   MEM is valid to use in conditional code, NULL if we can't load up the offset
   into a temporary register, or the new MEM if we were successful.  */

static rtx
frv_ifcvt_rewrite_mem (rtx mem, enum machine_mode mode, rtx insn)
{
  rtx addr = XEXP (mem, 0);

  if (!frv_legitimate_address_p (mode, addr, reload_completed, TRUE, FALSE))
    {
      if (GET_CODE (addr) == PLUS)
	{
	  rtx addr_op0 = XEXP (addr, 0);
	  rtx addr_op1 = XEXP (addr, 1);

	  if (GET_CODE (addr_op0) == REG && CONSTANT_P (addr_op1))
	    {
	      rtx reg = frv_ifcvt_load_value (addr_op1, insn);
	      if (!reg)
		return NULL_RTX;

	      addr = gen_rtx_PLUS (Pmode, addr_op0, reg);
	    }

	  else
	    return NULL_RTX;
	}

      else if (CONSTANT_P (addr))
	addr = frv_ifcvt_load_value (addr, insn);

      else
	return NULL_RTX;

      if (addr == NULL_RTX)
	return NULL_RTX;

      else if (XEXP (mem, 0) != addr)
	return change_address (mem, mode, addr);
    }

  return mem;
}


/* Given a PATTERN, return a SET expression if this PATTERN has only a single
   SET, possibly conditionally executed.  It may also have CLOBBERs, USEs.  */

static rtx
single_set_pattern (rtx pattern)
{
  rtx set;
  int i;

  if (GET_CODE (pattern) == COND_EXEC)
    pattern = COND_EXEC_CODE (pattern);

  if (GET_CODE (pattern) == SET)
    return pattern;

  else if (GET_CODE (pattern) == PARALLEL)
    {
      for (i = 0, set = 0; i < XVECLEN (pattern, 0); i++)
	{
	  rtx sub = XVECEXP (pattern, 0, i);

	  switch (GET_CODE (sub))
	    {
	    case USE:
	    case CLOBBER:
	      break;

	    case SET:
	      if (set)
		return 0;
	      else
		set = sub;
	      break;

	    default:
	      return 0;
	    }
	}
      return set;
    }

  return 0;
}


/* A C expression to modify the code described by the conditional if
   information CE_INFO with the new PATTERN in INSN.  If PATTERN is a null
   pointer after the IFCVT_MODIFY_INSN macro executes, it is assumed that that
   insn cannot be converted to be executed conditionally.  */

rtx
frv_ifcvt_modify_insn (ce_if_block_t *ce_info,
                       rtx pattern,
                       rtx insn)
{
  rtx orig_ce_pattern = pattern;
  rtx set;
  rtx op0;
  rtx op1;
  rtx test;

  gcc_assert (GET_CODE (pattern) == COND_EXEC);

  test = COND_EXEC_TEST (pattern);
  if (GET_CODE (test) == AND)
    {
      rtx cr = frv_ifcvt.cr_reg;
      rtx test_reg;

      op0 = XEXP (test, 0);
      if (! rtx_equal_p (cr, XEXP (op0, 0)))
	goto fail;

      op1 = XEXP (test, 1);
      test_reg = XEXP (op1, 0);
      if (GET_CODE (test_reg) != REG)
	goto fail;

      /* Is this the first nested if block in this sequence?  If so, generate
         an andcr or andncr.  */
      if (! frv_ifcvt.last_nested_if_cr)
	{
	  rtx and_op;

	  frv_ifcvt.last_nested_if_cr = test_reg;
	  if (GET_CODE (op0) == NE)
	    and_op = gen_andcr (test_reg, cr, test_reg);
	  else
	    and_op = gen_andncr (test_reg, cr, test_reg);

	  frv_ifcvt_add_insn (and_op, insn, TRUE);
	}

      /* If this isn't the first statement in the nested if sequence, see if we
         are dealing with the same register.  */
      else if (! rtx_equal_p (test_reg, frv_ifcvt.last_nested_if_cr))
	goto fail;

      COND_EXEC_TEST (pattern) = test = op1;
    }

  /* If this isn't a nested if, reset state variables.  */
  else
    {
      frv_ifcvt.last_nested_if_cr = NULL_RTX;
    }

  set = single_set_pattern (pattern);
  if (set)
    {
      rtx dest = SET_DEST (set);
      rtx src = SET_SRC (set);
      enum machine_mode mode = GET_MODE (dest);

      /* Check for normal binary operators.  */
      if (mode == SImode && ARITHMETIC_P (src))
	{
	  op0 = XEXP (src, 0);
	  op1 = XEXP (src, 1);

	  if (integer_register_operand (op0, SImode) && CONSTANT_P (op1))
	    {
	      op1 = frv_ifcvt_load_value (op1, insn);
	      if (op1)
		COND_EXEC_CODE (pattern)
		  = gen_rtx_SET (VOIDmode, dest, gen_rtx_fmt_ee (GET_CODE (src),
								 GET_MODE (src),
								 op0, op1));
	      else
		goto fail;
	    }
	}

      /* For multiply by a constant, we need to handle the sign extending
         correctly.  Add a USE of the value after the multiply to prevent flow
         from cratering because only one register out of the two were used.  */
      else if (mode == DImode && GET_CODE (src) == MULT)
	{
	  op0 = XEXP (src, 0);
	  op1 = XEXP (src, 1);
	  if (GET_CODE (op0) == SIGN_EXTEND && GET_CODE (op1) == CONST_INT)
	    {
	      op1 = frv_ifcvt_load_value (op1, insn);
	      if (op1)
		{
		  op1 = gen_rtx_SIGN_EXTEND (DImode, op1);
		  COND_EXEC_CODE (pattern)
		    = gen_rtx_SET (VOIDmode, dest,
				   gen_rtx_MULT (DImode, op0, op1));
		}
	      else
		goto fail;
	    }

	  frv_ifcvt_add_insn (gen_rtx_USE (VOIDmode, dest), insn, FALSE);
	}

      /* If we are just loading a constant created for a nested conditional
         execution statement, just load the constant without any conditional
         execution, since we know that the constant will not interfere with any
         other registers.  */
      else if (frv_ifcvt.scratch_insns_bitmap
	       && bitmap_bit_p (frv_ifcvt.scratch_insns_bitmap,
				INSN_UID (insn))
	       && REG_P (SET_DEST (set))
	       /* We must not unconditionally set a scratch reg chosen
		  for a nested if-converted block if its incoming
		  value from the TEST block (or the result of the THEN
		  branch) could/should propagate to the JOIN block.
		  It suffices to test whether the register is live at
		  the JOIN point: if it's live there, we can infer
		  that we set it in the former JOIN block of the
		  nested if-converted block (otherwise it wouldn't
		  have been available as a scratch register), and it
		  is either propagated through or set in the other
		  conditional block.  It's probably not worth trying
		  to catch the latter case, and it could actually
		  limit scheduling of the combined block quite
		  severely.  */
	       && ce_info->join_bb
	       && ! (REGNO_REG_SET_P
		     (ce_info->join_bb->il.rtl->global_live_at_start,
		      REGNO (SET_DEST (set))))
	       /* Similarly, we must not unconditionally set a reg
		  used as scratch in the THEN branch if the same reg
		  is live in the ELSE branch.  */
	       && (! ce_info->else_bb
		   || BLOCK_FOR_INSN (insn) == ce_info->else_bb
		   || ! (REGNO_REG_SET_P
			 (ce_info->else_bb->il.rtl->global_live_at_start,
			  REGNO (SET_DEST (set))))))
	pattern = set;

      else if (mode == QImode || mode == HImode || mode == SImode
	       || mode == SFmode)
	{
	  int changed_p = FALSE;

	  /* Check for just loading up a constant */
	  if (CONSTANT_P (src) && integer_register_operand (dest, mode))
	    {
	      src = frv_ifcvt_load_value (src, insn);
	      if (!src)
		goto fail;

	      changed_p = TRUE;
	    }

	  /* See if we need to fix up stores */
	  if (GET_CODE (dest) == MEM)
	    {
	      rtx new_mem = frv_ifcvt_rewrite_mem (dest, mode, insn);

	      if (!new_mem)
		goto fail;

	      else if (new_mem != dest)
		{
		  changed_p = TRUE;
		  dest = new_mem;
		}
	    }

	  /* See if we need to fix up loads */
	  if (GET_CODE (src) == MEM)
	    {
	      rtx new_mem = frv_ifcvt_rewrite_mem (src, mode, insn);

	      if (!new_mem)
		goto fail;

	      else if (new_mem != src)
		{
		  changed_p = TRUE;
		  src = new_mem;
		}
	    }

	  /* If either src or destination changed, redo SET.  */
	  if (changed_p)
	    COND_EXEC_CODE (pattern) = gen_rtx_SET (VOIDmode, dest, src);
	}

      /* Rewrite a nested set cccr in terms of IF_THEN_ELSE.  Also deal with
         rewriting the CC register to be the same as the paired CC/CR register
         for nested ifs.  */
      else if (mode == CC_CCRmode && COMPARISON_P (src))
	{
	  int regno = REGNO (XEXP (src, 0));
	  rtx if_else;

	  if (ce_info->pass > 1
	      && regno != (int)REGNO (frv_ifcvt.nested_cc_reg)
	      && TEST_HARD_REG_BIT (frv_ifcvt.nested_cc_ok_rewrite, regno))
	    {
	      src = gen_rtx_fmt_ee (GET_CODE (src),
				    CC_CCRmode,
				    frv_ifcvt.nested_cc_reg,
				    XEXP (src, 1));
	    }

	  if_else = gen_rtx_IF_THEN_ELSE (CC_CCRmode, test, src, const0_rtx);
	  pattern = gen_rtx_SET (VOIDmode, dest, if_else);
	}

      /* Remap a nested compare instruction to use the paired CC/CR reg.  */
      else if (ce_info->pass > 1
	       && GET_CODE (dest) == REG
	       && CC_P (REGNO (dest))
	       && REGNO (dest) != REGNO (frv_ifcvt.nested_cc_reg)
	       && TEST_HARD_REG_BIT (frv_ifcvt.nested_cc_ok_rewrite,
				     REGNO (dest))
	       && GET_CODE (src) == COMPARE)
	{
	  PUT_MODE (frv_ifcvt.nested_cc_reg, GET_MODE (dest));
	  COND_EXEC_CODE (pattern)
	    = gen_rtx_SET (VOIDmode, frv_ifcvt.nested_cc_reg, copy_rtx (src));
	}
    }

  if (TARGET_DEBUG_COND_EXEC)
    {
      rtx orig_pattern = PATTERN (insn);

      PATTERN (insn) = pattern;
      fprintf (stderr,
	       "\n:::::::::: frv_ifcvt_modify_insn: pass = %d, insn after modification:\n",
	       ce_info->pass);

      debug_rtx (insn);
      PATTERN (insn) = orig_pattern;
    }

  return pattern;

 fail:
  if (TARGET_DEBUG_COND_EXEC)
    {
      rtx orig_pattern = PATTERN (insn);

      PATTERN (insn) = orig_ce_pattern;
      fprintf (stderr,
	       "\n:::::::::: frv_ifcvt_modify_insn: pass = %d, insn could not be modified:\n",
	       ce_info->pass);

      debug_rtx (insn);
      PATTERN (insn) = orig_pattern;
    }

  return NULL_RTX;
}


/* A C expression to perform any final machine dependent modifications in
   converting code to conditional execution in the code described by the
   conditional if information CE_INFO.  */

void
frv_ifcvt_modify_final (ce_if_block_t *ce_info ATTRIBUTE_UNUSED)
{
  rtx existing_insn;
  rtx check_insn;
  rtx p = frv_ifcvt.added_insns_list;
  int i;

  /* Loop inserting the check insns.  The last check insn is the first test,
     and is the appropriate place to insert constants.  */
  gcc_assert (p);

  do
    {
      rtx check_and_insert_insns = XEXP (p, 0);
      rtx old_p = p;

      check_insn = XEXP (check_and_insert_insns, 0);
      existing_insn = XEXP (check_and_insert_insns, 1);
      p = XEXP (p, 1);

      /* The jump bit is used to say that the new insn is to be inserted BEFORE
         the existing insn, otherwise it is to be inserted AFTER.  */
      if (check_and_insert_insns->jump)
	{
	  emit_insn_before (check_insn, existing_insn);
	  check_and_insert_insns->jump = 0;
	}
      else
	emit_insn_after (check_insn, existing_insn);

      free_EXPR_LIST_node (check_and_insert_insns);
      free_EXPR_LIST_node (old_p);
    }
  while (p != NULL_RTX);

  /* Load up any constants needed into temp gprs */
  for (i = 0; i < frv_ifcvt.cur_scratch_regs; i++)
    {
      rtx insn = emit_insn_before (frv_ifcvt.scratch_regs[i], existing_insn);
      if (! frv_ifcvt.scratch_insns_bitmap)
	frv_ifcvt.scratch_insns_bitmap = BITMAP_ALLOC (NULL);
      bitmap_set_bit (frv_ifcvt.scratch_insns_bitmap, INSN_UID (insn));
      frv_ifcvt.scratch_regs[i] = NULL_RTX;
    }

  frv_ifcvt.added_insns_list = NULL_RTX;
  frv_ifcvt.cur_scratch_regs = 0;
}


/* A C expression to cancel any machine dependent modifications in converting
   code to conditional execution in the code described by the conditional if
   information CE_INFO.  */

void
frv_ifcvt_modify_cancel (ce_if_block_t *ce_info ATTRIBUTE_UNUSED)
{
  int i;
  rtx p = frv_ifcvt.added_insns_list;

  /* Loop freeing up the EXPR_LIST's allocated.  */
  while (p != NULL_RTX)
    {
      rtx check_and_jump = XEXP (p, 0);
      rtx old_p = p;

      p = XEXP (p, 1);
      free_EXPR_LIST_node (check_and_jump);
      free_EXPR_LIST_node (old_p);
    }

  /* Release any temporary gprs allocated.  */
  for (i = 0; i < frv_ifcvt.cur_scratch_regs; i++)
    frv_ifcvt.scratch_regs[i] = NULL_RTX;

  frv_ifcvt.added_insns_list = NULL_RTX;
  frv_ifcvt.cur_scratch_regs = 0;
  return;
}

/* A C expression for the size in bytes of the trampoline, as an integer.
   The template is:

	setlo #0, <jmp_reg>
	setlo #0, <static_chain>
	sethi #0, <jmp_reg>
	sethi #0, <static_chain>
	jmpl @(gr0,<jmp_reg>) */

int
frv_trampoline_size (void)
{
  if (TARGET_FDPIC)
    /* Allocate room for the function descriptor and the lddi
       instruction.  */
    return 8 + 6 * 4;
  return 5 /* instructions */ * 4 /* instruction size.  */;
}


/* A C statement to initialize the variable parts of a trampoline.  ADDR is an
   RTX for the address of the trampoline; FNADDR is an RTX for the address of
   the nested function; STATIC_CHAIN is an RTX for the static chain value that
   should be passed to the function when it is called.

   The template is:

	setlo #0, <jmp_reg>
	setlo #0, <static_chain>
	sethi #0, <jmp_reg>
	sethi #0, <static_chain>
	jmpl @(gr0,<jmp_reg>) */

void
frv_initialize_trampoline (rtx addr, rtx fnaddr, rtx static_chain)
{
  rtx sc_reg = force_reg (Pmode, static_chain);

  emit_library_call (gen_rtx_SYMBOL_REF (SImode, "__trampoline_setup"),
		     FALSE, VOIDmode, 4,
		     addr, Pmode,
		     GEN_INT (frv_trampoline_size ()), SImode,
		     fnaddr, Pmode,
		     sc_reg, Pmode);
}


/* Many machines have some registers that cannot be copied directly to or from
   memory or even from other types of registers.  An example is the `MQ'
   register, which on most machines, can only be copied to or from general
   registers, but not memory.  Some machines allow copying all registers to and
   from memory, but require a scratch register for stores to some memory
   locations (e.g., those with symbolic address on the RT, and those with
   certain symbolic address on the SPARC when compiling PIC).  In some cases,
   both an intermediate and a scratch register are required.

   You should define these macros to indicate to the reload phase that it may
   need to allocate at least one register for a reload in addition to the
   register to contain the data.  Specifically, if copying X to a register
   CLASS in MODE requires an intermediate register, you should define
   `SECONDARY_INPUT_RELOAD_CLASS' to return the largest register class all of
   whose registers can be used as intermediate registers or scratch registers.

   If copying a register CLASS in MODE to X requires an intermediate or scratch
   register, `SECONDARY_OUTPUT_RELOAD_CLASS' should be defined to return the
   largest register class required.  If the requirements for input and output
   reloads are the same, the macro `SECONDARY_RELOAD_CLASS' should be used
   instead of defining both macros identically.

   The values returned by these macros are often `GENERAL_REGS'.  Return
   `NO_REGS' if no spare register is needed; i.e., if X can be directly copied
   to or from a register of CLASS in MODE without requiring a scratch register.
   Do not define this macro if it would always return `NO_REGS'.

   If a scratch register is required (either with or without an intermediate
   register), you should define patterns for `reload_inM' or `reload_outM', as
   required..  These patterns, which will normally be implemented with a
   `define_expand', should be similar to the `movM' patterns, except that
   operand 2 is the scratch register.

   Define constraints for the reload register and scratch register that contain
   a single register class.  If the original reload register (whose class is
   CLASS) can meet the constraint given in the pattern, the value returned by
   these macros is used for the class of the scratch register.  Otherwise, two
   additional reload registers are required.  Their classes are obtained from
   the constraints in the insn pattern.

   X might be a pseudo-register or a `subreg' of a pseudo-register, which could
   either be in a hard register or in memory.  Use `true_regnum' to find out;
   it will return -1 if the pseudo is in memory and the hard register number if
   it is in a register.

   These macros should not be used in the case where a particular class of
   registers can only be copied to memory and not to another class of
   registers.  In that case, secondary reload registers are not needed and
   would not be helpful.  Instead, a stack location must be used to perform the
   copy and the `movM' pattern should use memory as an intermediate storage.
   This case often occurs between floating-point and general registers.  */

enum reg_class
frv_secondary_reload_class (enum reg_class class,
                            enum machine_mode mode ATTRIBUTE_UNUSED,
                            rtx x,
                            int in_p ATTRIBUTE_UNUSED)
{
  enum reg_class ret;

  switch (class)
    {
    default:
      ret = NO_REGS;
      break;

      /* Accumulators/Accumulator guard registers need to go through floating
         point registers.  */
    case QUAD_REGS:
    case EVEN_REGS:
    case GPR_REGS:
      ret = NO_REGS;
      if (x && GET_CODE (x) == REG)
	{
	  int regno = REGNO (x);

	  if (ACC_P (regno) || ACCG_P (regno))
	    ret = FPR_REGS;
	}
      break;

      /* Nonzero constants should be loaded into an FPR through a GPR.  */
    case QUAD_FPR_REGS:
    case FEVEN_REGS:
    case FPR_REGS:
      if (x && CONSTANT_P (x) && !ZERO_P (x))
	ret = GPR_REGS;
      else
	ret = NO_REGS;
      break;

      /* All of these types need gpr registers.  */
    case ICC_REGS:
    case FCC_REGS:
    case CC_REGS:
    case ICR_REGS:
    case FCR_REGS:
    case CR_REGS:
    case LCR_REG:
    case LR_REG:
      ret = GPR_REGS;
      break;

      /* The accumulators need fpr registers */
    case ACC_REGS:
    case EVEN_ACC_REGS:
    case QUAD_ACC_REGS:
    case ACCG_REGS:
      ret = FPR_REGS;
      break;
    }

  return ret;
}


/* A C expression whose value is nonzero if pseudos that have been assigned to
   registers of class CLASS would likely be spilled because registers of CLASS
   are needed for spill registers.

   The default value of this macro returns 1 if CLASS has exactly one register
   and zero otherwise.  On most machines, this default should be used.  Only
   define this macro to some other expression if pseudo allocated by
   `local-alloc.c' end up in memory because their hard registers were needed
   for spill registers.  If this macro returns nonzero for those classes, those
   pseudos will only be allocated by `global.c', which knows how to reallocate
   the pseudo to another register.  If there would not be another register
   available for reallocation, you should not change the definition of this
   macro since the only effect of such a definition would be to slow down
   register allocation.  */

int
frv_class_likely_spilled_p (enum reg_class class)
{
  switch (class)
    {
    default:
      break;

    case GR8_REGS:
    case GR9_REGS:
    case GR89_REGS:
    case FDPIC_FPTR_REGS:
    case FDPIC_REGS:
    case ICC_REGS:
    case FCC_REGS:
    case CC_REGS:
    case ICR_REGS:
    case FCR_REGS:
    case CR_REGS:
    case LCR_REG:
    case LR_REG:
    case SPR_REGS:
    case QUAD_ACC_REGS:
    case EVEN_ACC_REGS:
    case ACC_REGS:
    case ACCG_REGS:
      return TRUE;
    }

  return FALSE;
}


/* An expression for the alignment of a structure field FIELD if the
   alignment computed in the usual way is COMPUTED.  GCC uses this
   value instead of the value in `BIGGEST_ALIGNMENT' or
   `BIGGEST_FIELD_ALIGNMENT', if defined, for structure fields only.  */

/* The definition type of the bit field data is either char, short, long or
   long long. The maximum bit size is the number of bits of its own type.

   The bit field data is assigned to a storage unit that has an adequate size
   for bit field data retention and is located at the smallest address.

   Consecutive bit field data are packed at consecutive bits having the same
   storage unit, with regard to the type, beginning with the MSB and continuing
   toward the LSB.

   If a field to be assigned lies over a bit field type boundary, its
   assignment is completed by aligning it with a boundary suitable for the
   type.

   When a bit field having a bit length of 0 is declared, it is forcibly
   assigned to the next storage unit.

   e.g)
	struct {
		int	a:2;
		int	b:6;
		char	c:4;
		int	d:10;
		int	 :0;
		int	f:2;
	} x;

		+0	  +1	    +2	      +3
	&x	00000000  00000000  00000000  00000000
		MLM----L
		a    b
	&x+4	00000000  00000000  00000000  00000000
		M--L
		c
	&x+8	00000000  00000000  00000000  00000000
		M----------L
		d
	&x+12	00000000  00000000  00000000  00000000
		ML
		f
*/

int
frv_adjust_field_align (tree field, int computed)
{
  /* Make sure that the bitfield is not wider than the type.  */
  if (DECL_BIT_FIELD (field)
      && !DECL_ARTIFICIAL (field))
    {
      tree parent = DECL_CONTEXT (field);
      tree prev = NULL_TREE;
      tree cur;

      for (cur = TYPE_FIELDS (parent); cur && cur != field; cur = TREE_CHAIN (cur))
	{
	  if (TREE_CODE (cur) != FIELD_DECL)
	    continue;

	  prev = cur;
	}

      gcc_assert (cur);

      /* If this isn't a :0 field and if the previous element is a bitfield
	 also, see if the type is different, if so, we will need to align the
	 bit-field to the next boundary.  */
      if (prev
	  && ! DECL_PACKED (field)
	  && ! integer_zerop (DECL_SIZE (field))
	  && DECL_BIT_FIELD_TYPE (field) != DECL_BIT_FIELD_TYPE (prev))
	{
	  int prev_align = TYPE_ALIGN (TREE_TYPE (prev));
	  int cur_align  = TYPE_ALIGN (TREE_TYPE (field));
	  computed = (prev_align > cur_align) ? prev_align : cur_align;
	}
    }

  return computed;
}


/* A C expression that is nonzero if it is permissible to store a value of mode
   MODE in hard register number REGNO (or in several registers starting with
   that one).  For a machine where all registers are equivalent, a suitable
   definition is

        #define HARD_REGNO_MODE_OK(REGNO, MODE) 1

   It is not necessary for this macro to check for the numbers of fixed
   registers, because the allocation mechanism considers them to be always
   occupied.

   On some machines, double-precision values must be kept in even/odd register
   pairs.  The way to implement that is to define this macro to reject odd
   register numbers for such modes.

   The minimum requirement for a mode to be OK in a register is that the
   `movMODE' instruction pattern support moves between the register and any
   other hard register for which the mode is OK; and that moving a value into
   the register and back out not alter it.

   Since the same instruction used to move `SImode' will work for all narrower
   integer modes, it is not necessary on any machine for `HARD_REGNO_MODE_OK'
   to distinguish between these modes, provided you define patterns `movhi',
   etc., to take advantage of this.  This is useful because of the interaction
   between `HARD_REGNO_MODE_OK' and `MODES_TIEABLE_P'; it is very desirable for
   all integer modes to be tieable.

   Many machines have special registers for floating point arithmetic.  Often
   people assume that floating point machine modes are allowed only in floating
   point registers.  This is not true.  Any registers that can hold integers
   can safely *hold* a floating point machine mode, whether or not floating
   arithmetic can be done on it in those registers.  Integer move instructions
   can be used to move the values.

   On some machines, though, the converse is true: fixed-point machine modes
   may not go in floating registers.  This is true if the floating registers
   normalize any value stored in them, because storing a non-floating value
   there would garble it.  In this case, `HARD_REGNO_MODE_OK' should reject
   fixed-point machine modes in floating registers.  But if the floating
   registers do not automatically normalize, if you can store any bit pattern
   in one and retrieve it unchanged without a trap, then any machine mode may
   go in a floating register, so you can define this macro to say so.

   The primary significance of special floating registers is rather that they
   are the registers acceptable in floating point arithmetic instructions.
   However, this is of no concern to `HARD_REGNO_MODE_OK'.  You handle it by
   writing the proper constraints for those instructions.

   On some machines, the floating registers are especially slow to access, so
   that it is better to store a value in a stack frame than in such a register
   if floating point arithmetic is not being done.  As long as the floating
   registers are not in class `GENERAL_REGS', they will not be used unless some
   pattern's constraint asks for one.  */

int
frv_hard_regno_mode_ok (int regno, enum machine_mode mode)
{
  int base;
  int mask;

  switch (mode)
    {
    case CCmode:
    case CC_UNSmode:
    case CC_NZmode:
      return ICC_P (regno) || GPR_P (regno);

    case CC_CCRmode:
      return CR_P (regno) || GPR_P (regno);

    case CC_FPmode:
      return FCC_P (regno) || GPR_P (regno);

    default:
      break;
    }

  /* Set BASE to the first register in REGNO's class.  Set MASK to the
     bits that must be clear in (REGNO - BASE) for the register to be
     well-aligned.  */
  if (INTEGRAL_MODE_P (mode) || FLOAT_MODE_P (mode) || VECTOR_MODE_P (mode))
    {
      if (ACCG_P (regno))
	{
	  /* ACCGs store one byte.  Two-byte quantities must start in
	     even-numbered registers, four-byte ones in registers whose
	     numbers are divisible by four, and so on.  */
	  base = ACCG_FIRST;
	  mask = GET_MODE_SIZE (mode) - 1;
	}
      else
	{
	   /* The other registers store one word.  */
	  if (GPR_P (regno) || regno == AP_FIRST)
	    base = GPR_FIRST;

	  else if (FPR_P (regno))
	    base = FPR_FIRST;

	  else if (ACC_P (regno))
	    base = ACC_FIRST;

	  else if (SPR_P (regno))
	    return mode == SImode;

	  /* Fill in the table.  */
	  else
	    return 0;

	  /* Anything smaller than an SI is OK in any word-sized register.  */
	  if (GET_MODE_SIZE (mode) < 4)
	    return 1;

	  mask = (GET_MODE_SIZE (mode) / 4) - 1;
	}
      return (((regno - base) & mask) == 0);
    }

  return 0;
}


/* A C expression for the number of consecutive hard registers, starting at
   register number REGNO, required to hold a value of mode MODE.

   On a machine where all registers are exactly one word, a suitable definition
   of this macro is

        #define HARD_REGNO_NREGS(REGNO, MODE)            \
           ((GET_MODE_SIZE (MODE) + UNITS_PER_WORD - 1)  \
            / UNITS_PER_WORD))  */

/* On the FRV, make the CC_FP mode take 3 words in the integer registers, so
   that we can build the appropriate instructions to properly reload the
   values.  Also, make the byte-sized accumulator guards use one guard
   for each byte.  */

int
frv_hard_regno_nregs (int regno, enum machine_mode mode)
{
  if (ACCG_P (regno))
    return GET_MODE_SIZE (mode);
  else
    return (GET_MODE_SIZE (mode) + UNITS_PER_WORD - 1) / UNITS_PER_WORD;
}


/* A C expression for the maximum number of consecutive registers of
   class CLASS needed to hold a value of mode MODE.

   This is closely related to the macro `HARD_REGNO_NREGS'.  In fact, the value
   of the macro `CLASS_MAX_NREGS (CLASS, MODE)' should be the maximum value of
   `HARD_REGNO_NREGS (REGNO, MODE)' for all REGNO values in the class CLASS.

   This macro helps control the handling of multiple-word values in
   the reload pass.

   This declaration is required.  */

int
frv_class_max_nregs (enum reg_class class, enum machine_mode mode)
{
  if (class == ACCG_REGS)
    /* An N-byte value requires N accumulator guards.  */
    return GET_MODE_SIZE (mode);
  else
    return (GET_MODE_SIZE (mode) + UNITS_PER_WORD - 1) / UNITS_PER_WORD;
}


/* A C expression that is nonzero if X is a legitimate constant for an
   immediate operand on the target machine.  You can assume that X satisfies
   `CONSTANT_P', so you need not check this.  In fact, `1' is a suitable
   definition for this macro on machines where anything `CONSTANT_P' is valid.  */

int
frv_legitimate_constant_p (rtx x)
{
  enum machine_mode mode = GET_MODE (x);

  /* frv_cannot_force_const_mem always returns true for FDPIC.  This
     means that the move expanders will be expected to deal with most
     kinds of constant, regardless of what we return here.

     However, among its other duties, LEGITIMATE_CONSTANT_P decides whether
     a constant can be entered into reg_equiv_constant[].  If we return true,
     reload can create new instances of the constant whenever it likes.

     The idea is therefore to accept as many constants as possible (to give
     reload more freedom) while rejecting constants that can only be created
     at certain times.  In particular, anything with a symbolic component will
     require use of the pseudo FDPIC register, which is only available before
     reload.  */
  if (TARGET_FDPIC)
    return LEGITIMATE_PIC_OPERAND_P (x);

  /* All of the integer constants are ok.  */
  if (GET_CODE (x) != CONST_DOUBLE)
    return TRUE;

  /* double integer constants are ok.  */
  if (mode == VOIDmode || mode == DImode)
    return TRUE;

  /* 0 is always ok.  */
  if (x == CONST0_RTX (mode))
    return TRUE;

  /* If floating point is just emulated, allow any constant, since it will be
     constructed in the GPRs.  */
  if (!TARGET_HAS_FPRS)
    return TRUE;

  if (mode == DFmode && !TARGET_DOUBLE)
    return TRUE;

  /* Otherwise store the constant away and do a load.  */
  return FALSE;
}

/* Implement SELECT_CC_MODE.  Choose CC_FP for floating-point comparisons,
   CC_NZ for comparisons against zero in which a single Z or N flag test
   is enough, CC_UNS for other unsigned comparisons, and CC for other
   signed comparisons.  */

enum machine_mode
frv_select_cc_mode (enum rtx_code code, rtx x, rtx y)
{
  if (GET_MODE_CLASS (GET_MODE (x)) == MODE_FLOAT)
    return CC_FPmode;

  switch (code)
    {
    case EQ:
    case NE:
    case LT:
    case GE:
      return y == const0_rtx ? CC_NZmode : CCmode;

    case GTU:
    case GEU:
    case LTU:
    case LEU:
      return y == const0_rtx ? CC_NZmode : CC_UNSmode;

    default:
      return CCmode;
    }
}

/* A C expression for the cost of moving data from a register in class FROM to
   one in class TO.  The classes are expressed using the enumeration values
   such as `GENERAL_REGS'.  A value of 4 is the default; other values are
   interpreted relative to that.

   It is not required that the cost always equal 2 when FROM is the same as TO;
   on some machines it is expensive to move between registers if they are not
   general registers.

   If reload sees an insn consisting of a single `set' between two hard
   registers, and if `REGISTER_MOVE_COST' applied to their classes returns a
   value of 2, reload does not check to ensure that the constraints of the insn
   are met.  Setting a cost of other than 2 will allow reload to verify that
   the constraints are met.  You should do this if the `movM' pattern's
   constraints do not allow such copying.  */

#define HIGH_COST 40
#define MEDIUM_COST 3
#define LOW_COST 1

int
frv_register_move_cost (enum reg_class from, enum reg_class to)
{
  switch (from)
    {
    default:
      break;

    case QUAD_REGS:
    case EVEN_REGS:
    case GPR_REGS:
      switch (to)
	{
	default:
	  break;

	case QUAD_REGS:
	case EVEN_REGS:
	case GPR_REGS:
	  return LOW_COST;

	case FEVEN_REGS:
	case FPR_REGS:
	  return LOW_COST;

	case LCR_REG:
	case LR_REG:
	case SPR_REGS:
	  return LOW_COST;
	}

    case FEVEN_REGS:
    case FPR_REGS:
      switch (to)
	{
	default:
	  break;

	case QUAD_REGS:
	case EVEN_REGS:
	case GPR_REGS:
	case ACC_REGS:
	case EVEN_ACC_REGS:
	case QUAD_ACC_REGS:
	case ACCG_REGS:
	  return MEDIUM_COST;

	case FEVEN_REGS:
	case FPR_REGS:
	  return LOW_COST;
	}

    case LCR_REG:
    case LR_REG:
    case SPR_REGS:
      switch (to)
	{
	default:
	  break;

	case QUAD_REGS:
	case EVEN_REGS:
	case GPR_REGS:
	  return MEDIUM_COST;
	}

    case ACC_REGS:
    case EVEN_ACC_REGS:
    case QUAD_ACC_REGS:
    case ACCG_REGS:
      switch (to)
	{
	default:
	  break;

	case FEVEN_REGS:
	case FPR_REGS:
	  return MEDIUM_COST;

	}
    }

  return HIGH_COST;
}

/* Implementation of TARGET_ASM_INTEGER.  In the FRV case we need to
   use ".picptr" to generate safe relocations for PIC code.  We also
   need a fixup entry for aligned (non-debugging) code.  */

static bool
frv_assemble_integer (rtx value, unsigned int size, int aligned_p)
{
  if ((flag_pic || TARGET_FDPIC) && size == UNITS_PER_WORD)
    {
      if (GET_CODE (value) == CONST
	  || GET_CODE (value) == SYMBOL_REF
	  || GET_CODE (value) == LABEL_REF)
	{
	  if (TARGET_FDPIC && GET_CODE (value) == SYMBOL_REF
	      && SYMBOL_REF_FUNCTION_P (value))
	    {
	      fputs ("\t.picptr\tfuncdesc(", asm_out_file);
	      output_addr_const (asm_out_file, value);
	      fputs (")\n", asm_out_file);
	      return true;
	    }
	  else if (TARGET_FDPIC && GET_CODE (value) == CONST
		   && frv_function_symbol_referenced_p (value))
	    return false;
	  if (aligned_p && !TARGET_FDPIC)
	    {
	      static int label_num = 0;
	      char buf[256];
	      const char *p;

	      ASM_GENERATE_INTERNAL_LABEL (buf, "LCP", label_num++);
	      p = (* targetm.strip_name_encoding) (buf);

	      fprintf (asm_out_file, "%s:\n", p);
	      fprintf (asm_out_file, "%s\n", FIXUP_SECTION_ASM_OP);
	      fprintf (asm_out_file, "\t.picptr\t%s\n", p);
	      fprintf (asm_out_file, "\t.previous\n");
	    }
	  assemble_integer_with_op ("\t.picptr\t", value);
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

/* Function to set up the backend function structure.  */

static struct machine_function *
frv_init_machine_status (void)
{
  return ggc_alloc_cleared (sizeof (struct machine_function));
}

/* Implement TARGET_SCHED_ISSUE_RATE.  */

int
frv_issue_rate (void)
{
  if (!TARGET_PACK)
    return 1;

  switch (frv_cpu_type)
    {
    default:
    case FRV_CPU_FR300:
    case FRV_CPU_SIMPLE:
      return 1;

    case FRV_CPU_FR400:
    case FRV_CPU_FR405:
    case FRV_CPU_FR450:
      return 2;

    case FRV_CPU_GENERIC:
    case FRV_CPU_FR500:
    case FRV_CPU_TOMCAT:
      return 4;

    case FRV_CPU_FR550:
      return 8;
    }
}

/* A for_each_rtx callback.  If X refers to an accumulator, return
   ACC_GROUP_ODD if the bit 2 of the register number is set and
   ACC_GROUP_EVEN if it is clear.  Return 0 (ACC_GROUP_NONE)
   otherwise.  */

static int
frv_acc_group_1 (rtx *x, void *data ATTRIBUTE_UNUSED)
{
  if (REG_P (*x))
    {
      if (ACC_P (REGNO (*x)))
	return (REGNO (*x) - ACC_FIRST) & 4 ? ACC_GROUP_ODD : ACC_GROUP_EVEN;
      if (ACCG_P (REGNO (*x)))
	return (REGNO (*x) - ACCG_FIRST) & 4 ? ACC_GROUP_ODD : ACC_GROUP_EVEN;
    }
  return 0;
}

/* Return the value of INSN's acc_group attribute.  */

int
frv_acc_group (rtx insn)
{
  /* This distinction only applies to the FR550 packing constraints.  */
  if (frv_cpu_type != FRV_CPU_FR550)
    return ACC_GROUP_NONE;
  return for_each_rtx (&PATTERN (insn), frv_acc_group_1, 0);
}

/* Return the index of the DFA unit in FRV_UNIT_NAMES[] that instruction
   INSN will try to claim first.  Since this value depends only on the
   type attribute, we can cache the results in FRV_TYPE_TO_UNIT[].  */

static unsigned int
frv_insn_unit (rtx insn)
{
  enum attr_type type;

  type = get_attr_type (insn);
  if (frv_type_to_unit[type] == ARRAY_SIZE (frv_unit_codes))
    {
      /* We haven't seen this type of instruction before.  */
      state_t state;
      unsigned int unit;

      /* Issue the instruction on its own to see which unit it prefers.  */
      state = alloca (state_size ());
      state_reset (state);
      state_transition (state, insn);

      /* Find out which unit was taken.  */
      for (unit = 0; unit < ARRAY_SIZE (frv_unit_codes); unit++)
	if (cpu_unit_reservation_p (state, frv_unit_codes[unit]))
	  break;

      gcc_assert (unit != ARRAY_SIZE (frv_unit_codes));

      frv_type_to_unit[type] = unit;
    }
  return frv_type_to_unit[type];
}

/* Return true if INSN issues to a branch unit.  */

static bool
frv_issues_to_branch_unit_p (rtx insn)
{
  return frv_unit_groups[frv_insn_unit (insn)] == GROUP_B;
}

/* The current state of the packing pass, implemented by frv_pack_insns.  */
static struct {
  /* The state of the pipeline DFA.  */
  state_t dfa_state;

  /* Which hardware registers are set within the current packet,
     and the conditions under which they are set.  */
  regstate_t regstate[FIRST_PSEUDO_REGISTER];

  /* The memory locations that have been modified so far in this
     packet.  MEM is the memref and COND is the regstate_t condition
     under which it is set.  */
  struct {
    rtx mem;
    regstate_t cond;
  } mems[2];

  /* The number of valid entries in MEMS.  The value is larger than
     ARRAY_SIZE (mems) if there were too many mems to record.  */
  unsigned int num_mems;

  /* The maximum number of instructions that can be packed together.  */
  unsigned int issue_rate;

  /* The instructions in the packet, partitioned into groups.  */
  struct frv_packet_group {
    /* How many instructions in the packet belong to this group.  */
    unsigned int num_insns;

    /* A list of the instructions that belong to this group, in the order
       they appear in the rtl stream.  */
    rtx insns[ARRAY_SIZE (frv_unit_codes)];

    /* The contents of INSNS after they have been sorted into the correct
       assembly-language order.  Element X issues to unit X.  The list may
       contain extra nops.  */
    rtx sorted[ARRAY_SIZE (frv_unit_codes)];

    /* The member of frv_nops[] to use in sorted[].  */
    rtx nop;
  } groups[NUM_GROUPS];

  /* The instructions that make up the current packet.  */
  rtx insns[ARRAY_SIZE (frv_unit_codes)];
  unsigned int num_insns;
} frv_packet;

/* Return the regstate_t flags for the given COND_EXEC condition.
   Abort if the condition isn't in the right form.  */

static int
frv_cond_flags (rtx cond)
{
  gcc_assert ((GET_CODE (cond) == EQ || GET_CODE (cond) == NE)
	      && GET_CODE (XEXP (cond, 0)) == REG
	      && CR_P (REGNO (XEXP (cond, 0)))
	      && XEXP (cond, 1) == const0_rtx);
  return ((REGNO (XEXP (cond, 0)) - CR_FIRST)
	  | (GET_CODE (cond) == NE
	     ? REGSTATE_IF_TRUE
	     : REGSTATE_IF_FALSE));
}


/* Return true if something accessed under condition COND2 can
   conflict with something written under condition COND1.  */

static bool
frv_regstate_conflict_p (regstate_t cond1, regstate_t cond2)
{
  /* If either reference was unconditional, we have a conflict.  */
  if ((cond1 & REGSTATE_IF_EITHER) == 0
      || (cond2 & REGSTATE_IF_EITHER) == 0)
    return true;

  /* The references might conflict if they were controlled by
     different CRs.  */
  if ((cond1 & REGSTATE_CC_MASK) != (cond2 & REGSTATE_CC_MASK))
    return true;

  /* They definitely conflict if they are controlled by the
     same condition.  */
  if ((cond1 & cond2 & REGSTATE_IF_EITHER) != 0)
    return true;

  return false;
}


/* A for_each_rtx callback.  Return 1 if *X depends on an instruction in
   the current packet.  DATA points to a regstate_t that describes the
   condition under which *X might be set or used.  */

static int
frv_registers_conflict_p_1 (rtx *x, void *data)
{
  unsigned int regno, i;
  regstate_t cond;

  cond = *(regstate_t *) data;

  if (GET_CODE (*x) == REG)
    FOR_EACH_REGNO (regno, *x)
      if ((frv_packet.regstate[regno] & REGSTATE_MODIFIED) != 0)
	if (frv_regstate_conflict_p (frv_packet.regstate[regno], cond))
	  return 1;

  if (GET_CODE (*x) == MEM)
    {
      /* If we ran out of memory slots, assume a conflict.  */
      if (frv_packet.num_mems > ARRAY_SIZE (frv_packet.mems))
	return 1;

      /* Check for output or true dependencies with earlier MEMs.  */
      for (i = 0; i < frv_packet.num_mems; i++)
	if (frv_regstate_conflict_p (frv_packet.mems[i].cond, cond))
	  {
	    if (true_dependence (frv_packet.mems[i].mem, VOIDmode,
				 *x, rtx_varies_p))
	      return 1;

	    if (output_dependence (frv_packet.mems[i].mem, *x))
	      return 1;
	  }
    }

  /* The return values of calls aren't significant: they describe
     the effect of the call as a whole, not of the insn itself.  */
  if (GET_CODE (*x) == SET && GET_CODE (SET_SRC (*x)) == CALL)
    {
      if (for_each_rtx (&SET_SRC (*x), frv_registers_conflict_p_1, data))
	return 1;
      return -1;
    }

  /* Check subexpressions.  */
  return 0;
}


/* Return true if something in X might depend on an instruction
   in the current packet.  */

static bool
frv_registers_conflict_p (rtx x)
{
  regstate_t flags;

  flags = 0;
  if (GET_CODE (x) == COND_EXEC)
    {
      if (for_each_rtx (&XEXP (x, 0), frv_registers_conflict_p_1, &flags))
	return true;

      flags |= frv_cond_flags (XEXP (x, 0));
      x = XEXP (x, 1);
    }
  return for_each_rtx (&x, frv_registers_conflict_p_1, &flags);
}


/* A note_stores callback.  DATA points to the regstate_t condition
   under which X is modified.  Update FRV_PACKET accordingly.  */

static void
frv_registers_update_1 (rtx x, rtx pat ATTRIBUTE_UNUSED, void *data)
{
  unsigned int regno;

  if (GET_CODE (x) == REG)
    FOR_EACH_REGNO (regno, x)
      frv_packet.regstate[regno] |= *(regstate_t *) data;

  if (GET_CODE (x) == MEM)
    {
      if (frv_packet.num_mems < ARRAY_SIZE (frv_packet.mems))
	{
	  frv_packet.mems[frv_packet.num_mems].mem = x;
	  frv_packet.mems[frv_packet.num_mems].cond = *(regstate_t *) data;
	}
      frv_packet.num_mems++;
    }
}


/* Update the register state information for an instruction whose
   body is X.  */

static void
frv_registers_update (rtx x)
{
  regstate_t flags;

  flags = REGSTATE_MODIFIED;
  if (GET_CODE (x) == COND_EXEC)
    {
      flags |= frv_cond_flags (XEXP (x, 0));
      x = XEXP (x, 1);
    }
  note_stores (x, frv_registers_update_1, &flags);
}


/* Initialize frv_packet for the start of a new packet.  */

static void
frv_start_packet (void)
{
  enum frv_insn_group group;

  memset (frv_packet.regstate, 0, sizeof (frv_packet.regstate));
  frv_packet.num_mems = 0;
  frv_packet.num_insns = 0;
  for (group = 0; group < NUM_GROUPS; group++)
    frv_packet.groups[group].num_insns = 0;
}


/* Likewise for the start of a new basic block.  */

static void
frv_start_packet_block (void)
{
  state_reset (frv_packet.dfa_state);
  frv_start_packet ();
}


/* Finish the current packet, if any, and start a new one.  Call
   HANDLE_PACKET with FRV_PACKET describing the completed packet.  */

static void
frv_finish_packet (void (*handle_packet) (void))
{
  if (frv_packet.num_insns > 0)
    {
      handle_packet ();
      state_transition (frv_packet.dfa_state, 0);
      frv_start_packet ();
    }
}


/* Return true if INSN can be added to the current packet.  Update
   the DFA state on success.  */

static bool
frv_pack_insn_p (rtx insn)
{
  /* See if the packet is already as long as it can be.  */
  if (frv_packet.num_insns == frv_packet.issue_rate)
    return false;

  /* If the scheduler thought that an instruction should start a packet,
     it's usually a good idea to believe it.  It knows much more about
     the latencies than we do.

     There are some exceptions though:

       - Conditional instructions are scheduled on the assumption that
	 they will be executed.  This is usually a good thing, since it
	 tends to avoid unnecessary stalls in the conditional code.
	 But we want to pack conditional instructions as tightly as
	 possible, in order to optimize the case where they aren't
	 executed.

       - The scheduler will always put branches on their own, even
	 if there's no real dependency.

       - There's no point putting a call in its own packet unless
	 we have to.  */
  if (frv_packet.num_insns > 0
      && GET_CODE (insn) == INSN
      && GET_MODE (insn) == TImode
      && GET_CODE (PATTERN (insn)) != COND_EXEC)
    return false;

  /* Check for register conflicts.  Don't do this for setlo since any
     conflict will be with the partnering sethi, with which it can
     be packed.  */
  if (get_attr_type (insn) != TYPE_SETLO)
    if (frv_registers_conflict_p (PATTERN (insn)))
      return false;

  return state_transition (frv_packet.dfa_state, insn) < 0;
}


/* Add instruction INSN to the current packet.  */

static void
frv_add_insn_to_packet (rtx insn)
{
  struct frv_packet_group *packet_group;

  packet_group = &frv_packet.groups[frv_unit_groups[frv_insn_unit (insn)]];
  packet_group->insns[packet_group->num_insns++] = insn;
  frv_packet.insns[frv_packet.num_insns++] = insn;

  frv_registers_update (PATTERN (insn));
}


/* Insert INSN (a member of frv_nops[]) into the current packet.  If the
   packet ends in a branch or call, insert the nop before it, otherwise
   add to the end.  */

static void
frv_insert_nop_in_packet (rtx insn)
{
  struct frv_packet_group *packet_group;
  rtx last;

  packet_group = &frv_packet.groups[frv_unit_groups[frv_insn_unit (insn)]];
  last = frv_packet.insns[frv_packet.num_insns - 1];
  if (GET_CODE (last) != INSN)
    {
      insn = emit_insn_before (PATTERN (insn), last);
      frv_packet.insns[frv_packet.num_insns - 1] = insn;
      frv_packet.insns[frv_packet.num_insns++] = last;
    }
  else
    {
      insn = emit_insn_after (PATTERN (insn), last);
      frv_packet.insns[frv_packet.num_insns++] = insn;
    }
  packet_group->insns[packet_group->num_insns++] = insn;
}


/* If packing is enabled, divide the instructions into packets and
   return true.  Call HANDLE_PACKET for each complete packet.  */

static bool
frv_for_each_packet (void (*handle_packet) (void))
{
  rtx insn, next_insn;

  frv_packet.issue_rate = frv_issue_rate ();

  /* Early exit if we don't want to pack insns.  */
  if (!optimize
      || !flag_schedule_insns_after_reload
      || !TARGET_VLIW_BRANCH
      || frv_packet.issue_rate == 1)
    return false;

  /* Set up the initial packing state.  */
  dfa_start ();
  frv_packet.dfa_state = alloca (state_size ());

  frv_start_packet_block ();
  for (insn = get_insns (); insn != 0; insn = next_insn)
    {
      enum rtx_code code;
      bool eh_insn_p;

      code = GET_CODE (insn);
      next_insn = NEXT_INSN (insn);

      if (code == CODE_LABEL)
	{
	  frv_finish_packet (handle_packet);
	  frv_start_packet_block ();
	}

      if (INSN_P (insn))
	switch (GET_CODE (PATTERN (insn)))
	  {
	  case USE:
	  case CLOBBER:
	  case ADDR_VEC:
	  case ADDR_DIFF_VEC:
	    break;

	  default:
	    /* Calls mustn't be packed on a TOMCAT.  */
	    if (GET_CODE (insn) == CALL_INSN && frv_cpu_type == FRV_CPU_TOMCAT)
	      frv_finish_packet (handle_packet);

	    /* Since the last instruction in a packet determines the EH
	       region, any exception-throwing instruction must come at
	       the end of reordered packet.  Insns that issue to a
	       branch unit are bound to come last; for others it's
	       too hard to predict.  */
	    eh_insn_p = (find_reg_note (insn, REG_EH_REGION, NULL) != NULL);
	    if (eh_insn_p && !frv_issues_to_branch_unit_p (insn))
	      frv_finish_packet (handle_packet);

	    /* Finish the current packet if we can't add INSN to it.
	       Simulate cycles until INSN is ready to issue.  */
	    if (!frv_pack_insn_p (insn))
	      {
		frv_finish_packet (handle_packet);
		while (!frv_pack_insn_p (insn))
		  state_transition (frv_packet.dfa_state, 0);
	      }

	    /* Add the instruction to the packet.  */
	    frv_add_insn_to_packet (insn);

	    /* Calls and jumps end a packet, as do insns that throw
	       an exception.  */
	    if (code == CALL_INSN || code == JUMP_INSN || eh_insn_p)
	      frv_finish_packet (handle_packet);
	    break;
	  }
    }
  frv_finish_packet (handle_packet);
  dfa_finish ();
  return true;
}

/* Subroutine of frv_sort_insn_group.  We are trying to sort
   frv_packet.groups[GROUP].sorted[0...NUM_INSNS-1] into assembly
   language order.  We have already picked a new position for
   frv_packet.groups[GROUP].sorted[X] if bit X of ISSUED is set.
   These instructions will occupy elements [0, LOWER_SLOT) and
   [UPPER_SLOT, NUM_INSNS) of the final (sorted) array.  STATE is
   the DFA state after issuing these instructions.

   Try filling elements [LOWER_SLOT, UPPER_SLOT) with every permutation
   of the unused instructions.  Return true if one such permutation gives
   a valid ordering, leaving the successful permutation in sorted[].
   Do not modify sorted[] until a valid permutation is found.  */

static bool
frv_sort_insn_group_1 (enum frv_insn_group group,
		       unsigned int lower_slot, unsigned int upper_slot,
		       unsigned int issued, unsigned int num_insns,
		       state_t state)
{
  struct frv_packet_group *packet_group;
  unsigned int i;
  state_t test_state;
  size_t dfa_size;
  rtx insn;

  /* Early success if we've filled all the slots.  */
  if (lower_slot == upper_slot)
    return true;

  packet_group = &frv_packet.groups[group];
  dfa_size = state_size ();
  test_state = alloca (dfa_size);

  /* Try issuing each unused instruction.  */
  for (i = num_insns - 1; i + 1 != 0; i--)
    if (~issued & (1 << i))
      {
	insn = packet_group->sorted[i];
	memcpy (test_state, state, dfa_size);
	if (state_transition (test_state, insn) < 0
	    && cpu_unit_reservation_p (test_state,
				       NTH_UNIT (group, upper_slot - 1))
	    && frv_sort_insn_group_1 (group, lower_slot, upper_slot - 1,
				      issued | (1 << i), num_insns,
				      test_state))
	  {
	    packet_group->sorted[upper_slot - 1] = insn;
	    return true;
	  }
      }

  return false;
}

/* Compare two instructions by their frv_insn_unit.  */

static int
frv_compare_insns (const void *first, const void *second)
{
  const rtx *insn1 = first, *insn2 = second;
  return frv_insn_unit (*insn1) - frv_insn_unit (*insn2);
}

/* Copy frv_packet.groups[GROUP].insns[] to frv_packet.groups[GROUP].sorted[]
   and sort it into assembly language order.  See frv.md for a description of
   the algorithm.  */

static void
frv_sort_insn_group (enum frv_insn_group group)
{
  struct frv_packet_group *packet_group;
  unsigned int first, i, nop, max_unit, num_slots;
  state_t state, test_state;
  size_t dfa_size;

  packet_group = &frv_packet.groups[group];

  /* Assume no nop is needed.  */
  packet_group->nop = 0;

  if (packet_group->num_insns == 0)
    return;

  /* Copy insns[] to sorted[].  */
  memcpy (packet_group->sorted, packet_group->insns,
	  sizeof (rtx) * packet_group->num_insns);

  /* Sort sorted[] by the unit that each insn tries to take first.  */
  if (packet_group->num_insns > 1)
    qsort (packet_group->sorted, packet_group->num_insns,
	   sizeof (rtx), frv_compare_insns);

  /* That's always enough for branch and control insns.  */
  if (group == GROUP_B || group == GROUP_C)
    return;

  dfa_size = state_size ();
  state = alloca (dfa_size);
  test_state = alloca (dfa_size);

  /* Find the highest FIRST such that sorted[0...FIRST-1] can issue
     consecutively and such that the DFA takes unit X when sorted[X]
     is added.  Set STATE to the new DFA state.  */
  state_reset (test_state);
  for (first = 0; first < packet_group->num_insns; first++)
    {
      memcpy (state, test_state, dfa_size);
      if (state_transition (test_state, packet_group->sorted[first]) >= 0
	  || !cpu_unit_reservation_p (test_state, NTH_UNIT (group, first)))
	break;
    }

  /* If all the instructions issued in ascending order, we're done.  */
  if (first == packet_group->num_insns)
    return;

  /* Add nops to the end of sorted[] and try each permutation until
     we find one that works.  */
  for (nop = 0; nop < frv_num_nops; nop++)
    {
      max_unit = frv_insn_unit (frv_nops[nop]);
      if (frv_unit_groups[max_unit] == group)
	{
	  packet_group->nop = frv_nops[nop];
	  num_slots = UNIT_NUMBER (max_unit) + 1;
	  for (i = packet_group->num_insns; i < num_slots; i++)
	    packet_group->sorted[i] = frv_nops[nop];
	  if (frv_sort_insn_group_1 (group, first, num_slots,
				     (1 << first) - 1, num_slots, state))
	    return;
	}
    }
  gcc_unreachable ();
}

/* Sort the current packet into assembly-language order.  Set packing
   flags as appropriate.  */

static void
frv_reorder_packet (void)
{
  unsigned int cursor[NUM_GROUPS];
  rtx insns[ARRAY_SIZE (frv_unit_groups)];
  unsigned int unit, to, from;
  enum frv_insn_group group;
  struct frv_packet_group *packet_group;

  /* First sort each group individually.  */
  for (group = 0; group < NUM_GROUPS; group++)
    {
      cursor[group] = 0;
      frv_sort_insn_group (group);
    }

  /* Go through the unit template and try add an instruction from
     that unit's group.  */
  to = 0;
  for (unit = 0; unit < ARRAY_SIZE (frv_unit_groups); unit++)
    {
      group = frv_unit_groups[unit];
      packet_group = &frv_packet.groups[group];
      if (cursor[group] < packet_group->num_insns)
	{
	  /* frv_reorg should have added nops for us.  */
	  gcc_assert (packet_group->sorted[cursor[group]]
		      != packet_group->nop);
	  insns[to++] = packet_group->sorted[cursor[group]++];
	}
    }

  gcc_assert (to == frv_packet.num_insns);

  /* Clear the last instruction's packing flag, thus marking the end of
     a packet.  Reorder the other instructions relative to it.  */
  CLEAR_PACKING_FLAG (insns[to - 1]);
  for (from = 0; from < to - 1; from++)
    {
      remove_insn (insns[from]);
      add_insn_before (insns[from], insns[to - 1]);
      SET_PACKING_FLAG (insns[from]);
    }
}


/* Divide instructions into packets.  Reorder the contents of each
   packet so that they are in the correct assembly-language order.

   Since this pass can change the raw meaning of the rtl stream, it must
   only be called at the last minute, just before the instructions are
   written out.  */

static void
frv_pack_insns (void)
{
  if (frv_for_each_packet (frv_reorder_packet))
    frv_insn_packing_flag = 0;
  else
    frv_insn_packing_flag = -1;
}

/* See whether we need to add nops to group GROUP in order to
   make a valid packet.  */

static void
frv_fill_unused_units (enum frv_insn_group group)
{
  unsigned int non_nops, nops, i;
  struct frv_packet_group *packet_group;

  packet_group = &frv_packet.groups[group];

  /* Sort the instructions into assembly-language order.
     Use nops to fill slots that are otherwise unused.  */
  frv_sort_insn_group (group);

  /* See how many nops are needed before the final useful instruction.  */
  i = nops = 0;
  for (non_nops = 0; non_nops < packet_group->num_insns; non_nops++)
    while (packet_group->sorted[i++] == packet_group->nop)
      nops++;

  /* Insert that many nops into the instruction stream.  */
  while (nops-- > 0)
    frv_insert_nop_in_packet (packet_group->nop);
}

/* Return true if accesses IO1 and IO2 refer to the same doubleword.  */

static bool
frv_same_doubleword_p (const struct frv_io *io1, const struct frv_io *io2)
{
  if (io1->const_address != 0 && io2->const_address != 0)
    return io1->const_address == io2->const_address;

  if (io1->var_address != 0 && io2->var_address != 0)
    return rtx_equal_p (io1->var_address, io2->var_address);

  return false;
}

/* Return true if operations IO1 and IO2 are guaranteed to complete
   in order.  */

static bool
frv_io_fixed_order_p (const struct frv_io *io1, const struct frv_io *io2)
{
  /* The order of writes is always preserved.  */
  if (io1->type == FRV_IO_WRITE && io2->type == FRV_IO_WRITE)
    return true;

  /* The order of reads isn't preserved.  */
  if (io1->type != FRV_IO_WRITE && io2->type != FRV_IO_WRITE)
    return false;

  /* One operation is a write and the other is (or could be) a read.
     The order is only guaranteed if the accesses are to the same
     doubleword.  */
  return frv_same_doubleword_p (io1, io2);
}

/* Generalize I/O operation X so that it covers both X and Y. */

static void
frv_io_union (struct frv_io *x, const struct frv_io *y)
{
  if (x->type != y->type)
    x->type = FRV_IO_UNKNOWN;
  if (!frv_same_doubleword_p (x, y))
    {
      x->const_address = 0;
      x->var_address = 0;
    }
}

/* Fill IO with information about the load or store associated with
   membar instruction INSN.  */

static void
frv_extract_membar (struct frv_io *io, rtx insn)
{
  extract_insn (insn);
  io->type = INTVAL (recog_data.operand[2]);
  io->const_address = INTVAL (recog_data.operand[1]);
  io->var_address = XEXP (recog_data.operand[0], 0);
}

/* A note_stores callback for which DATA points to an rtx.  Nullify *DATA
   if X is a register and *DATA depends on X.  */

static void
frv_io_check_address (rtx x, rtx pat ATTRIBUTE_UNUSED, void *data)
{
  rtx *other = data;

  if (REG_P (x) && *other != 0 && reg_overlap_mentioned_p (x, *other))
    *other = 0;
}

/* A note_stores callback for which DATA points to a HARD_REG_SET.
   Remove every modified register from the set.  */

static void
frv_io_handle_set (rtx x, rtx pat ATTRIBUTE_UNUSED, void *data)
{
  HARD_REG_SET *set = data;
  unsigned int regno;

  if (REG_P (x))
    FOR_EACH_REGNO (regno, x)
      CLEAR_HARD_REG_BIT (*set, regno);
}

/* A for_each_rtx callback for which DATA points to a HARD_REG_SET.
   Add every register in *X to the set.  */

static int
frv_io_handle_use_1 (rtx *x, void *data)
{
  HARD_REG_SET *set = data;
  unsigned int regno;

  if (REG_P (*x))
    FOR_EACH_REGNO (regno, *x)
      SET_HARD_REG_BIT (*set, regno);

  return 0;
}

/* A note_stores callback that applies frv_io_handle_use_1 to an
   entire rhs value.  */

static void
frv_io_handle_use (rtx *x, void *data)
{
  for_each_rtx (x, frv_io_handle_use_1, data);
}

/* Go through block BB looking for membars to remove.  There are two
   cases where intra-block analysis is enough:

   - a membar is redundant if it occurs between two consecutive I/O
   operations and if those operations are guaranteed to complete
   in order.

   - a membar for a __builtin_read is redundant if the result is
   used before the next I/O operation is issued.

   If the last membar in the block could not be removed, and there
   are guaranteed to be no I/O operations between that membar and
   the end of the block, store the membar in *LAST_MEMBAR, otherwise
   store null.

   Describe the block's first I/O operation in *NEXT_IO.  Describe
   an unknown operation if the block doesn't do any I/O.  */

static void
frv_optimize_membar_local (basic_block bb, struct frv_io *next_io,
			   rtx *last_membar)
{
  HARD_REG_SET used_regs;
  rtx next_membar, set, insn;
  bool next_is_end_p;

  /* NEXT_IO is the next I/O operation to be performed after the current
     instruction.  It starts off as being an unknown operation.  */
  memset (next_io, 0, sizeof (*next_io));

  /* NEXT_IS_END_P is true if NEXT_IO describes the end of the block.  */
  next_is_end_p = true;

  /* If the current instruction is a __builtin_read or __builtin_write,
     NEXT_MEMBAR is the membar instruction associated with it.  NEXT_MEMBAR
     is null if the membar has already been deleted.

     Note that the initialization here should only be needed to
     suppress warnings.  */
  next_membar = 0;

  /* USED_REGS is the set of registers that are used before the
     next I/O instruction.  */
  CLEAR_HARD_REG_SET (used_regs);

  for (insn = BB_END (bb); insn != BB_HEAD (bb); insn = PREV_INSN (insn))
    if (GET_CODE (insn) == CALL_INSN)
      {
	/* We can't predict what a call will do to volatile memory.  */
	memset (next_io, 0, sizeof (struct frv_io));
	next_is_end_p = false;
	CLEAR_HARD_REG_SET (used_regs);
      }
    else if (INSN_P (insn))
      switch (recog_memoized (insn))
	{
	case CODE_FOR_optional_membar_qi:
	case CODE_FOR_optional_membar_hi:
	case CODE_FOR_optional_membar_si:
	case CODE_FOR_optional_membar_di:
	  next_membar = insn;
	  if (next_is_end_p)
	    {
	      /* Local information isn't enough to decide whether this
		 membar is needed.  Stash it away for later.  */
	      *last_membar = insn;
	      frv_extract_membar (next_io, insn);
	      next_is_end_p = false;
	    }
	  else
	    {
	      /* Check whether the I/O operation before INSN could be
		 reordered with one described by NEXT_IO.  If it can't,
		 INSN will not be needed.  */
	      struct frv_io prev_io;

	      frv_extract_membar (&prev_io, insn);
	      if (frv_io_fixed_order_p (&prev_io, next_io))
		{
		  if (dump_file)
		    fprintf (dump_file,
			     ";; [Local] Removing membar %d since order"
			     " of accesses is guaranteed\n",
			     INSN_UID (next_membar));

		  insn = NEXT_INSN (insn);
		  delete_insn (next_membar);
		  next_membar = 0;
		}
	      *next_io = prev_io;
	    }
	  break;

	default:
	  /* Invalidate NEXT_IO's address if it depends on something that
	     is clobbered by INSN.  */
	  if (next_io->var_address)
	    note_stores (PATTERN (insn), frv_io_check_address,
			 &next_io->var_address);

	  /* If the next membar is associated with a __builtin_read,
	     see if INSN reads from that address.  If it does, and if
	     the destination register is used before the next I/O access,
	     there is no need for the membar.  */
	  set = PATTERN (insn);
	  if (next_io->type == FRV_IO_READ
	      && next_io->var_address != 0
	      && next_membar != 0
	      && GET_CODE (set) == SET
	      && GET_CODE (SET_DEST (set)) == REG
	      && TEST_HARD_REG_BIT (used_regs, REGNO (SET_DEST (set))))
	    {
	      rtx src;

	      src = SET_SRC (set);
	      if (GET_CODE (src) == ZERO_EXTEND)
		src = XEXP (src, 0);

	      if (GET_CODE (src) == MEM
		  && rtx_equal_p (XEXP (src, 0), next_io->var_address))
		{
		  if (dump_file)
		    fprintf (dump_file,
			     ";; [Local] Removing membar %d since the target"
			     " of %d is used before the I/O operation\n",
			     INSN_UID (next_membar), INSN_UID (insn));

		  if (next_membar == *last_membar)
		    *last_membar = 0;

		  delete_insn (next_membar);
		  next_membar = 0;
		}
	    }

	  /* If INSN has volatile references, forget about any registers
	     that are used after it.  Otherwise forget about uses that
	     are (or might be) defined by INSN.  */
	  if (volatile_refs_p (PATTERN (insn)))
	    CLEAR_HARD_REG_SET (used_regs);
	  else
	    note_stores (PATTERN (insn), frv_io_handle_set, &used_regs);

	  note_uses (&PATTERN (insn), frv_io_handle_use, &used_regs);
	  break;
	}
}

/* See if MEMBAR, the last membar instruction in BB, can be removed.
   FIRST_IO[X] describes the first operation performed by basic block X.  */

static void
frv_optimize_membar_global (basic_block bb, struct frv_io *first_io,
			    rtx membar)
{
  struct frv_io this_io, next_io;
  edge succ;
  edge_iterator ei;

  /* We need to keep the membar if there is an edge to the exit block.  */
  FOR_EACH_EDGE (succ, ei, bb->succs)
  /* for (succ = bb->succ; succ != 0; succ = succ->succ_next) */
    if (succ->dest == EXIT_BLOCK_PTR)
      return;

  /* Work out the union of all successor blocks.  */
  ei = ei_start (bb->succs);
  ei_cond (ei, &succ);
  /* next_io = first_io[bb->succ->dest->index]; */
  next_io = first_io[succ->dest->index];
  ei = ei_start (bb->succs);
  if (ei_cond (ei, &succ))
    {
      for (ei_next (&ei); ei_cond (ei, &succ); ei_next (&ei))
	/*for (succ = bb->succ->succ_next; succ != 0; succ = succ->succ_next)*/
	frv_io_union (&next_io, &first_io[succ->dest->index]);
    }
  else
    gcc_unreachable ();

  frv_extract_membar (&this_io, membar);
  if (frv_io_fixed_order_p (&this_io, &next_io))
    {
      if (dump_file)
	fprintf (dump_file,
		 ";; [Global] Removing membar %d since order of accesses"
		 " is guaranteed\n", INSN_UID (membar));

      delete_insn (membar);
    }
}

/* Remove redundant membars from the current function.  */

static void
frv_optimize_membar (void)
{
  basic_block bb;
  struct frv_io *first_io;
  rtx *last_membar;

  compute_bb_for_insn ();
  first_io = xcalloc (last_basic_block, sizeof (struct frv_io));
  last_membar = xcalloc (last_basic_block, sizeof (rtx));

  FOR_EACH_BB (bb)
    frv_optimize_membar_local (bb, &first_io[bb->index],
			       &last_membar[bb->index]);

  FOR_EACH_BB (bb)
    if (last_membar[bb->index] != 0)
      frv_optimize_membar_global (bb, first_io, last_membar[bb->index]);

  free (first_io);
  free (last_membar);
}

/* Used by frv_reorg to keep track of the current packet's address.  */
static unsigned int frv_packet_address;

/* If the current packet falls through to a label, try to pad the packet
   with nops in order to fit the label's alignment requirements.  */

static void
frv_align_label (void)
{
  unsigned int alignment, target, nop;
  rtx x, last, barrier, label;

  /* Walk forward to the start of the next packet.  Set ALIGNMENT to the
     maximum alignment of that packet, LABEL to the last label between
     the packets, and BARRIER to the last barrier.  */
  last = frv_packet.insns[frv_packet.num_insns - 1];
  label = barrier = 0;
  alignment = 4;
  for (x = NEXT_INSN (last); x != 0 && !INSN_P (x); x = NEXT_INSN (x))
    {
      if (LABEL_P (x))
	{
	  unsigned int subalign = 1 << label_to_alignment (x);
	  alignment = MAX (alignment, subalign);
	  label = x;
	}
      if (BARRIER_P (x))
	barrier = x;
    }

  /* If -malign-labels, and the packet falls through to an unaligned
     label, try introducing a nop to align that label to 8 bytes.  */
  if (TARGET_ALIGN_LABELS
      && label != 0
      && barrier == 0
      && frv_packet.num_insns < frv_packet.issue_rate)
    alignment = MAX (alignment, 8);

  /* Advance the address to the end of the current packet.  */
  frv_packet_address += frv_packet.num_insns * 4;

  /* Work out the target address, after alignment.  */
  target = (frv_packet_address + alignment - 1) & -alignment;

  /* If the packet falls through to the label, try to find an efficient
     padding sequence.  */
  if (barrier == 0)
    {
      /* First try adding nops to the current packet.  */
      for (nop = 0; nop < frv_num_nops; nop++)
	while (frv_packet_address < target && frv_pack_insn_p (frv_nops[nop]))
	  {
	    frv_insert_nop_in_packet (frv_nops[nop]);
	    frv_packet_address += 4;
	  }

      /* If we still haven't reached the target, add some new packets that
	 contain only nops.  If there are two types of nop, insert an
	 alternating sequence of frv_nops[0] and frv_nops[1], which will
	 lead to packets like:

		nop.p
		mnop.p/fnop.p
		nop.p
		mnop/fnop

	 etc.  Just emit frv_nops[0] if that's the only nop we have.  */
      last = frv_packet.insns[frv_packet.num_insns - 1];
      nop = 0;
      while (frv_packet_address < target)
	{
	  last = emit_insn_after (PATTERN (frv_nops[nop]), last);
	  frv_packet_address += 4;
	  if (frv_num_nops > 1)
	    nop ^= 1;
	}
    }

  frv_packet_address = target;
}

/* Subroutine of frv_reorg, called after each packet has been constructed
   in frv_packet.  */

static void
frv_reorg_packet (void)
{
  frv_fill_unused_units (GROUP_I);
  frv_fill_unused_units (GROUP_FM);
  frv_align_label ();
}

/* Add an instruction with pattern NOP to frv_nops[].  */

static void
frv_register_nop (rtx nop)
{
  nop = make_insn_raw (nop);
  NEXT_INSN (nop) = 0;
  PREV_INSN (nop) = 0;
  frv_nops[frv_num_nops++] = nop;
}

/* Implement TARGET_MACHINE_DEPENDENT_REORG.  Divide the instructions
   into packets and check whether we need to insert nops in order to
   fulfill the processor's issue requirements.  Also, if the user has
   requested a certain alignment for a label, try to meet that alignment
   by inserting nops in the previous packet.  */

static void
frv_reorg (void)
{
  if (optimize > 0 && TARGET_OPTIMIZE_MEMBAR && cfun->machine->has_membar_p)
    frv_optimize_membar ();

  frv_num_nops = 0;
  frv_register_nop (gen_nop ());
  if (TARGET_MEDIA)
    frv_register_nop (gen_mnop ());
  if (TARGET_HARD_FLOAT)
    frv_register_nop (gen_fnop ());

  /* Estimate the length of each branch.  Although this may change after
     we've inserted nops, it will only do so in big functions.  */
  shorten_branches (get_insns ());

  frv_packet_address = 0;
  frv_for_each_packet (frv_reorg_packet);
}

#define def_builtin(name, type, code) \
  lang_hooks.builtin_function ((name), (type), (code), BUILT_IN_MD, NULL, NULL)

struct builtin_description
{
  enum insn_code icode;
  const char *name;
  enum frv_builtins code;
  enum rtx_code comparison;
  unsigned int flag;
};

/* Media intrinsics that take a single, constant argument.  */

static struct builtin_description bdesc_set[] =
{
  { CODE_FOR_mhdsets, "__MHDSETS", FRV_BUILTIN_MHDSETS, 0, 0 }
};

/* Media intrinsics that take just one argument.  */

static struct builtin_description bdesc_1arg[] =
{
  { CODE_FOR_mnot, "__MNOT", FRV_BUILTIN_MNOT, 0, 0 },
  { CODE_FOR_munpackh, "__MUNPACKH", FRV_BUILTIN_MUNPACKH, 0, 0 },
  { CODE_FOR_mbtoh, "__MBTOH", FRV_BUILTIN_MBTOH, 0, 0 },
  { CODE_FOR_mhtob, "__MHTOB", FRV_BUILTIN_MHTOB, 0, 0 },
  { CODE_FOR_mabshs, "__MABSHS", FRV_BUILTIN_MABSHS, 0, 0 },
  { CODE_FOR_scutss, "__SCUTSS", FRV_BUILTIN_SCUTSS, 0, 0 }
};

/* Media intrinsics that take two arguments.  */

static struct builtin_description bdesc_2arg[] =
{
  { CODE_FOR_mand, "__MAND", FRV_BUILTIN_MAND, 0, 0 },
  { CODE_FOR_mor, "__MOR", FRV_BUILTIN_MOR, 0, 0 },
  { CODE_FOR_mxor, "__MXOR", FRV_BUILTIN_MXOR, 0, 0 },
  { CODE_FOR_maveh, "__MAVEH", FRV_BUILTIN_MAVEH, 0, 0 },
  { CODE_FOR_msaths, "__MSATHS", FRV_BUILTIN_MSATHS, 0, 0 },
  { CODE_FOR_msathu, "__MSATHU", FRV_BUILTIN_MSATHU, 0, 0 },
  { CODE_FOR_maddhss, "__MADDHSS", FRV_BUILTIN_MADDHSS, 0, 0 },
  { CODE_FOR_maddhus, "__MADDHUS", FRV_BUILTIN_MADDHUS, 0, 0 },
  { CODE_FOR_msubhss, "__MSUBHSS", FRV_BUILTIN_MSUBHSS, 0, 0 },
  { CODE_FOR_msubhus, "__MSUBHUS", FRV_BUILTIN_MSUBHUS, 0, 0 },
  { CODE_FOR_mqaddhss, "__MQADDHSS", FRV_BUILTIN_MQADDHSS, 0, 0 },
  { CODE_FOR_mqaddhus, "__MQADDHUS", FRV_BUILTIN_MQADDHUS, 0, 0 },
  { CODE_FOR_mqsubhss, "__MQSUBHSS", FRV_BUILTIN_MQSUBHSS, 0, 0 },
  { CODE_FOR_mqsubhus, "__MQSUBHUS", FRV_BUILTIN_MQSUBHUS, 0, 0 },
  { CODE_FOR_mpackh, "__MPACKH", FRV_BUILTIN_MPACKH, 0, 0 },
  { CODE_FOR_mcop1, "__Mcop1", FRV_BUILTIN_MCOP1, 0, 0 },
  { CODE_FOR_mcop2, "__Mcop2", FRV_BUILTIN_MCOP2, 0, 0 },
  { CODE_FOR_mwcut, "__MWCUT", FRV_BUILTIN_MWCUT, 0, 0 },
  { CODE_FOR_mqsaths, "__MQSATHS", FRV_BUILTIN_MQSATHS, 0, 0 },
  { CODE_FOR_mqlclrhs, "__MQLCLRHS", FRV_BUILTIN_MQLCLRHS, 0, 0 },
  { CODE_FOR_mqlmths, "__MQLMTHS", FRV_BUILTIN_MQLMTHS, 0, 0 },
  { CODE_FOR_smul, "__SMUL", FRV_BUILTIN_SMUL, 0, 0 },
  { CODE_FOR_umul, "__UMUL", FRV_BUILTIN_UMUL, 0, 0 },
  { CODE_FOR_addss, "__ADDSS", FRV_BUILTIN_ADDSS, 0, 0 },
  { CODE_FOR_subss, "__SUBSS", FRV_BUILTIN_SUBSS, 0, 0 },
  { CODE_FOR_slass, "__SLASS", FRV_BUILTIN_SLASS, 0, 0 },
  { CODE_FOR_scan, "__SCAN", FRV_BUILTIN_SCAN, 0, 0 }
};

/* Integer intrinsics that take two arguments and have no return value.  */

static struct builtin_description bdesc_int_void2arg[] =
{
  { CODE_FOR_smass, "__SMASS", FRV_BUILTIN_SMASS, 0, 0 },
  { CODE_FOR_smsss, "__SMSSS", FRV_BUILTIN_SMSSS, 0, 0 },
  { CODE_FOR_smu, "__SMU", FRV_BUILTIN_SMU, 0, 0 }
};

static struct builtin_description bdesc_prefetches[] =
{
  { CODE_FOR_frv_prefetch0, "__data_prefetch0", FRV_BUILTIN_PREFETCH0, 0, 0 },
  { CODE_FOR_frv_prefetch, "__data_prefetch", FRV_BUILTIN_PREFETCH, 0, 0 }
};

/* Media intrinsics that take two arguments, the first being an ACC number.  */

static struct builtin_description bdesc_cut[] =
{
  { CODE_FOR_mcut, "__MCUT", FRV_BUILTIN_MCUT, 0, 0 },
  { CODE_FOR_mcutss, "__MCUTSS", FRV_BUILTIN_MCUTSS, 0, 0 },
  { CODE_FOR_mdcutssi, "__MDCUTSSI", FRV_BUILTIN_MDCUTSSI, 0, 0 }
};

/* Two-argument media intrinsics with an immediate second argument.  */

static struct builtin_description bdesc_2argimm[] =
{
  { CODE_FOR_mrotli, "__MROTLI", FRV_BUILTIN_MROTLI, 0, 0 },
  { CODE_FOR_mrotri, "__MROTRI", FRV_BUILTIN_MROTRI, 0, 0 },
  { CODE_FOR_msllhi, "__MSLLHI", FRV_BUILTIN_MSLLHI, 0, 0 },
  { CODE_FOR_msrlhi, "__MSRLHI", FRV_BUILTIN_MSRLHI, 0, 0 },
  { CODE_FOR_msrahi, "__MSRAHI", FRV_BUILTIN_MSRAHI, 0, 0 },
  { CODE_FOR_mexpdhw, "__MEXPDHW", FRV_BUILTIN_MEXPDHW, 0, 0 },
  { CODE_FOR_mexpdhd, "__MEXPDHD", FRV_BUILTIN_MEXPDHD, 0, 0 },
  { CODE_FOR_mdrotli, "__MDROTLI", FRV_BUILTIN_MDROTLI, 0, 0 },
  { CODE_FOR_mcplhi, "__MCPLHI", FRV_BUILTIN_MCPLHI, 0, 0 },
  { CODE_FOR_mcpli, "__MCPLI", FRV_BUILTIN_MCPLI, 0, 0 },
  { CODE_FOR_mhsetlos, "__MHSETLOS", FRV_BUILTIN_MHSETLOS, 0, 0 },
  { CODE_FOR_mhsetloh, "__MHSETLOH", FRV_BUILTIN_MHSETLOH, 0, 0 },
  { CODE_FOR_mhsethis, "__MHSETHIS", FRV_BUILTIN_MHSETHIS, 0, 0 },
  { CODE_FOR_mhsethih, "__MHSETHIH", FRV_BUILTIN_MHSETHIH, 0, 0 },
  { CODE_FOR_mhdseth, "__MHDSETH", FRV_BUILTIN_MHDSETH, 0, 0 },
  { CODE_FOR_mqsllhi, "__MQSLLHI", FRV_BUILTIN_MQSLLHI, 0, 0 },
  { CODE_FOR_mqsrahi, "__MQSRAHI", FRV_BUILTIN_MQSRAHI, 0, 0 }
};

/* Media intrinsics that take two arguments and return void, the first argument
   being a pointer to 4 words in memory.  */

static struct builtin_description bdesc_void2arg[] =
{
  { CODE_FOR_mdunpackh, "__MDUNPACKH", FRV_BUILTIN_MDUNPACKH, 0, 0 },
  { CODE_FOR_mbtohe, "__MBTOHE", FRV_BUILTIN_MBTOHE, 0, 0 },
};

/* Media intrinsics that take three arguments, the first being a const_int that
   denotes an accumulator, and that return void.  */

static struct builtin_description bdesc_void3arg[] =
{
  { CODE_FOR_mcpxrs, "__MCPXRS", FRV_BUILTIN_MCPXRS, 0, 0 },
  { CODE_FOR_mcpxru, "__MCPXRU", FRV_BUILTIN_MCPXRU, 0, 0 },
  { CODE_FOR_mcpxis, "__MCPXIS", FRV_BUILTIN_MCPXIS, 0, 0 },
  { CODE_FOR_mcpxiu, "__MCPXIU", FRV_BUILTIN_MCPXIU, 0, 0 },
  { CODE_FOR_mmulhs, "__MMULHS", FRV_BUILTIN_MMULHS, 0, 0 },
  { CODE_FOR_mmulhu, "__MMULHU", FRV_BUILTIN_MMULHU, 0, 0 },
  { CODE_FOR_mmulxhs, "__MMULXHS", FRV_BUILTIN_MMULXHS, 0, 0 },
  { CODE_FOR_mmulxhu, "__MMULXHU", FRV_BUILTIN_MMULXHU, 0, 0 },
  { CODE_FOR_mmachs, "__MMACHS", FRV_BUILTIN_MMACHS, 0, 0 },
  { CODE_FOR_mmachu, "__MMACHU", FRV_BUILTIN_MMACHU, 0, 0 },
  { CODE_FOR_mmrdhs, "__MMRDHS", FRV_BUILTIN_MMRDHS, 0, 0 },
  { CODE_FOR_mmrdhu, "__MMRDHU", FRV_BUILTIN_MMRDHU, 0, 0 },
  { CODE_FOR_mqcpxrs, "__MQCPXRS", FRV_BUILTIN_MQCPXRS, 0, 0 },
  { CODE_FOR_mqcpxru, "__MQCPXRU", FRV_BUILTIN_MQCPXRU, 0, 0 },
  { CODE_FOR_mqcpxis, "__MQCPXIS", FRV_BUILTIN_MQCPXIS, 0, 0 },
  { CODE_FOR_mqcpxiu, "__MQCPXIU", FRV_BUILTIN_MQCPXIU, 0, 0 },
  { CODE_FOR_mqmulhs, "__MQMULHS", FRV_BUILTIN_MQMULHS, 0, 0 },
  { CODE_FOR_mqmulhu, "__MQMULHU", FRV_BUILTIN_MQMULHU, 0, 0 },
  { CODE_FOR_mqmulxhs, "__MQMULXHS", FRV_BUILTIN_MQMULXHS, 0, 0 },
  { CODE_FOR_mqmulxhu, "__MQMULXHU", FRV_BUILTIN_MQMULXHU, 0, 0 },
  { CODE_FOR_mqmachs, "__MQMACHS", FRV_BUILTIN_MQMACHS, 0, 0 },
  { CODE_FOR_mqmachu, "__MQMACHU", FRV_BUILTIN_MQMACHU, 0, 0 },
  { CODE_FOR_mqxmachs, "__MQXMACHS", FRV_BUILTIN_MQXMACHS, 0, 0 },
  { CODE_FOR_mqxmacxhs, "__MQXMACXHS", FRV_BUILTIN_MQXMACXHS, 0, 0 },
  { CODE_FOR_mqmacxhs, "__MQMACXHS", FRV_BUILTIN_MQMACXHS, 0, 0 }
};

/* Media intrinsics that take two accumulator numbers as argument and
   return void.  */

static struct builtin_description bdesc_voidacc[] =
{
  { CODE_FOR_maddaccs, "__MADDACCS", FRV_BUILTIN_MADDACCS, 0, 0 },
  { CODE_FOR_msubaccs, "__MSUBACCS", FRV_BUILTIN_MSUBACCS, 0, 0 },
  { CODE_FOR_masaccs, "__MASACCS", FRV_BUILTIN_MASACCS, 0, 0 },
  { CODE_FOR_mdaddaccs, "__MDADDACCS", FRV_BUILTIN_MDADDACCS, 0, 0 },
  { CODE_FOR_mdsubaccs, "__MDSUBACCS", FRV_BUILTIN_MDSUBACCS, 0, 0 },
  { CODE_FOR_mdasaccs, "__MDASACCS", FRV_BUILTIN_MDASACCS, 0, 0 }
};

/* Intrinsics that load a value and then issue a MEMBAR.  The load is
   a normal move and the ICODE is for the membar.  */

static struct builtin_description bdesc_loads[] =
{
  { CODE_FOR_optional_membar_qi, "__builtin_read8",
    FRV_BUILTIN_READ8, 0, 0 },
  { CODE_FOR_optional_membar_hi, "__builtin_read16",
    FRV_BUILTIN_READ16, 0, 0 },
  { CODE_FOR_optional_membar_si, "__builtin_read32",
    FRV_BUILTIN_READ32, 0, 0 },
  { CODE_FOR_optional_membar_di, "__builtin_read64",
    FRV_BUILTIN_READ64, 0, 0 }
};

/* Likewise stores.  */

static struct builtin_description bdesc_stores[] =
{
  { CODE_FOR_optional_membar_qi, "__builtin_write8",
    FRV_BUILTIN_WRITE8, 0, 0 },
  { CODE_FOR_optional_membar_hi, "__builtin_write16",
    FRV_BUILTIN_WRITE16, 0, 0 },
  { CODE_FOR_optional_membar_si, "__builtin_write32",
    FRV_BUILTIN_WRITE32, 0, 0 },
  { CODE_FOR_optional_membar_di, "__builtin_write64",
    FRV_BUILTIN_WRITE64, 0, 0 },
};

/* Initialize media builtins.  */

static void
frv_init_builtins (void)
{
  tree endlink = void_list_node;
  tree accumulator = integer_type_node;
  tree integer = integer_type_node;
  tree voidt = void_type_node;
  tree uhalf = short_unsigned_type_node;
  tree sword1 = long_integer_type_node;
  tree uword1 = long_unsigned_type_node;
  tree sword2 = long_long_integer_type_node;
  tree uword2 = long_long_unsigned_type_node;
  tree uword4 = build_pointer_type (uword1);
  tree vptr   = build_pointer_type (build_type_variant (void_type_node, 0, 1));
  tree ubyte  = unsigned_char_type_node;
  tree iacc   = integer_type_node;

#define UNARY(RET, T1) \
  build_function_type (RET, tree_cons (NULL_TREE, T1, endlink))

#define BINARY(RET, T1, T2) \
  build_function_type (RET, tree_cons (NULL_TREE, T1, \
			    tree_cons (NULL_TREE, T2, endlink)))

#define TRINARY(RET, T1, T2, T3) \
  build_function_type (RET, tree_cons (NULL_TREE, T1, \
			    tree_cons (NULL_TREE, T2, \
			    tree_cons (NULL_TREE, T3, endlink))))

#define QUAD(RET, T1, T2, T3, T4) \
  build_function_type (RET, tree_cons (NULL_TREE, T1, \
			    tree_cons (NULL_TREE, T2, \
			    tree_cons (NULL_TREE, T3, \
			    tree_cons (NULL_TREE, T4, endlink)))))

  tree void_ftype_void = build_function_type (voidt, endlink);

  tree void_ftype_acc = UNARY (voidt, accumulator);
  tree void_ftype_uw4_uw1 = BINARY (voidt, uword4, uword1);
  tree void_ftype_uw4_uw2 = BINARY (voidt, uword4, uword2);
  tree void_ftype_acc_uw1 = BINARY (voidt, accumulator, uword1);
  tree void_ftype_acc_acc = BINARY (voidt, accumulator, accumulator);
  tree void_ftype_acc_uw1_uw1 = TRINARY (voidt, accumulator, uword1, uword1);
  tree void_ftype_acc_sw1_sw1 = TRINARY (voidt, accumulator, sword1, sword1);
  tree void_ftype_acc_uw2_uw2 = TRINARY (voidt, accumulator, uword2, uword2);
  tree void_ftype_acc_sw2_sw2 = TRINARY (voidt, accumulator, sword2, sword2);

  tree uw1_ftype_uw1 = UNARY (uword1, uword1);
  tree uw1_ftype_sw1 = UNARY (uword1, sword1);
  tree uw1_ftype_uw2 = UNARY (uword1, uword2);
  tree uw1_ftype_acc = UNARY (uword1, accumulator);
  tree uw1_ftype_uh_uh = BINARY (uword1, uhalf, uhalf);
  tree uw1_ftype_uw1_uw1 = BINARY (uword1, uword1, uword1);
  tree uw1_ftype_uw1_int = BINARY (uword1, uword1, integer);
  tree uw1_ftype_acc_uw1 = BINARY (uword1, accumulator, uword1);
  tree uw1_ftype_acc_sw1 = BINARY (uword1, accumulator, sword1);
  tree uw1_ftype_uw2_uw1 = BINARY (uword1, uword2, uword1);
  tree uw1_ftype_uw2_int = BINARY (uword1, uword2, integer);

  tree sw1_ftype_int = UNARY (sword1, integer);
  tree sw1_ftype_sw1_sw1 = BINARY (sword1, sword1, sword1);
  tree sw1_ftype_sw1_int = BINARY (sword1, sword1, integer);

  tree uw2_ftype_uw1 = UNARY (uword2, uword1);
  tree uw2_ftype_uw1_int = BINARY (uword2, uword1, integer);
  tree uw2_ftype_uw2_uw2 = BINARY (uword2, uword2, uword2);
  tree uw2_ftype_uw2_int = BINARY (uword2, uword2, integer);
  tree uw2_ftype_acc_int = BINARY (uword2, accumulator, integer);
  tree uw2_ftype_uh_uh_uh_uh = QUAD (uword2, uhalf, uhalf, uhalf, uhalf);

  tree sw2_ftype_sw2_sw2 = BINARY (sword2, sword2, sword2);
  tree sw2_ftype_sw2_int   = BINARY (sword2, sword2, integer);
  tree uw2_ftype_uw1_uw1   = BINARY (uword2, uword1, uword1);
  tree sw2_ftype_sw1_sw1   = BINARY (sword2, sword1, sword1);
  tree void_ftype_sw1_sw1  = BINARY (voidt, sword1, sword1);
  tree void_ftype_iacc_sw2 = BINARY (voidt, iacc, sword2);
  tree void_ftype_iacc_sw1 = BINARY (voidt, iacc, sword1);
  tree sw1_ftype_sw1       = UNARY (sword1, sword1);
  tree sw2_ftype_iacc      = UNARY (sword2, iacc);
  tree sw1_ftype_iacc      = UNARY (sword1, iacc);
  tree void_ftype_ptr      = UNARY (voidt, const_ptr_type_node);
  tree uw1_ftype_vptr      = UNARY (uword1, vptr);
  tree uw2_ftype_vptr      = UNARY (uword2, vptr);
  tree void_ftype_vptr_ub  = BINARY (voidt, vptr, ubyte);
  tree void_ftype_vptr_uh  = BINARY (voidt, vptr, uhalf);
  tree void_ftype_vptr_uw1 = BINARY (voidt, vptr, uword1);
  tree void_ftype_vptr_uw2 = BINARY (voidt, vptr, uword2);

  def_builtin ("__MAND", uw1_ftype_uw1_uw1, FRV_BUILTIN_MAND);
  def_builtin ("__MOR", uw1_ftype_uw1_uw1, FRV_BUILTIN_MOR);
  def_builtin ("__MXOR", uw1_ftype_uw1_uw1, FRV_BUILTIN_MXOR);
  def_builtin ("__MNOT", uw1_ftype_uw1, FRV_BUILTIN_MNOT);
  def_builtin ("__MROTLI", uw1_ftype_uw1_int, FRV_BUILTIN_MROTLI);
  def_builtin ("__MROTRI", uw1_ftype_uw1_int, FRV_BUILTIN_MROTRI);
  def_builtin ("__MWCUT", uw1_ftype_uw2_uw1, FRV_BUILTIN_MWCUT);
  def_builtin ("__MAVEH", uw1_ftype_uw1_uw1, FRV_BUILTIN_MAVEH);
  def_builtin ("__MSLLHI", uw1_ftype_uw1_int, FRV_BUILTIN_MSLLHI);
  def_builtin ("__MSRLHI", uw1_ftype_uw1_int, FRV_BUILTIN_MSRLHI);
  def_builtin ("__MSRAHI", sw1_ftype_sw1_int, FRV_BUILTIN_MSRAHI);
  def_builtin ("__MSATHS", sw1_ftype_sw1_sw1, FRV_BUILTIN_MSATHS);
  def_builtin ("__MSATHU", uw1_ftype_uw1_uw1, FRV_BUILTIN_MSATHU);
  def_builtin ("__MADDHSS", sw1_ftype_sw1_sw1, FRV_BUILTIN_MADDHSS);
  def_builtin ("__MADDHUS", uw1_ftype_uw1_uw1, FRV_BUILTIN_MADDHUS);
  def_builtin ("__MSUBHSS", sw1_ftype_sw1_sw1, FRV_BUILTIN_MSUBHSS);
  def_builtin ("__MSUBHUS", uw1_ftype_uw1_uw1, FRV_BUILTIN_MSUBHUS);
  def_builtin ("__MMULHS", void_ftype_acc_sw1_sw1, FRV_BUILTIN_MMULHS);
  def_builtin ("__MMULHU", void_ftype_acc_uw1_uw1, FRV_BUILTIN_MMULHU);
  def_builtin ("__MMULXHS", void_ftype_acc_sw1_sw1, FRV_BUILTIN_MMULXHS);
  def_builtin ("__MMULXHU", void_ftype_acc_uw1_uw1, FRV_BUILTIN_MMULXHU);
  def_builtin ("__MMACHS", void_ftype_acc_sw1_sw1, FRV_BUILTIN_MMACHS);
  def_builtin ("__MMACHU", void_ftype_acc_uw1_uw1, FRV_BUILTIN_MMACHU);
  def_builtin ("__MMRDHS", void_ftype_acc_sw1_sw1, FRV_BUILTIN_MMRDHS);
  def_builtin ("__MMRDHU", void_ftype_acc_uw1_uw1, FRV_BUILTIN_MMRDHU);
  def_builtin ("__MQADDHSS", sw2_ftype_sw2_sw2, FRV_BUILTIN_MQADDHSS);
  def_builtin ("__MQADDHUS", uw2_ftype_uw2_uw2, FRV_BUILTIN_MQADDHUS);
  def_builtin ("__MQSUBHSS", sw2_ftype_sw2_sw2, FRV_BUILTIN_MQSUBHSS);
  def_builtin ("__MQSUBHUS", uw2_ftype_uw2_uw2, FRV_BUILTIN_MQSUBHUS);
  def_builtin ("__MQMULHS", void_ftype_acc_sw2_sw2, FRV_BUILTIN_MQMULHS);
  def_builtin ("__MQMULHU", void_ftype_acc_uw2_uw2, FRV_BUILTIN_MQMULHU);
  def_builtin ("__MQMULXHS", void_ftype_acc_sw2_sw2, FRV_BUILTIN_MQMULXHS);
  def_builtin ("__MQMULXHU", void_ftype_acc_uw2_uw2, FRV_BUILTIN_MQMULXHU);
  def_builtin ("__MQMACHS", void_ftype_acc_sw2_sw2, FRV_BUILTIN_MQMACHS);
  def_builtin ("__MQMACHU", void_ftype_acc_uw2_uw2, FRV_BUILTIN_MQMACHU);
  def_builtin ("__MCPXRS", void_ftype_acc_sw1_sw1, FRV_BUILTIN_MCPXRS);
  def_builtin ("__MCPXRU", void_ftype_acc_uw1_uw1, FRV_BUILTIN_MCPXRU);
  def_builtin ("__MCPXIS", void_ftype_acc_sw1_sw1, FRV_BUILTIN_MCPXIS);
  def_builtin ("__MCPXIU", void_ftype_acc_uw1_uw1, FRV_BUILTIN_MCPXIU);
  def_builtin ("__MQCPXRS", void_ftype_acc_sw2_sw2, FRV_BUILTIN_MQCPXRS);
  def_builtin ("__MQCPXRU", void_ftype_acc_uw2_uw2, FRV_BUILTIN_MQCPXRU);
  def_builtin ("__MQCPXIS", void_ftype_acc_sw2_sw2, FRV_BUILTIN_MQCPXIS);
  def_builtin ("__MQCPXIU", void_ftype_acc_uw2_uw2, FRV_BUILTIN_MQCPXIU);
  def_builtin ("__MCUT", uw1_ftype_acc_uw1, FRV_BUILTIN_MCUT);
  def_builtin ("__MCUTSS", uw1_ftype_acc_sw1, FRV_BUILTIN_MCUTSS);
  def_builtin ("__MEXPDHW", uw1_ftype_uw1_int, FRV_BUILTIN_MEXPDHW);
  def_builtin ("__MEXPDHD", uw2_ftype_uw1_int, FRV_BUILTIN_MEXPDHD);
  def_builtin ("__MPACKH", uw1_ftype_uh_uh, FRV_BUILTIN_MPACKH);
  def_builtin ("__MUNPACKH", uw2_ftype_uw1, FRV_BUILTIN_MUNPACKH);
  def_builtin ("__MDPACKH", uw2_ftype_uh_uh_uh_uh, FRV_BUILTIN_MDPACKH);
  def_builtin ("__MDUNPACKH", void_ftype_uw4_uw2, FRV_BUILTIN_MDUNPACKH);
  def_builtin ("__MBTOH", uw2_ftype_uw1, FRV_BUILTIN_MBTOH);
  def_builtin ("__MHTOB", uw1_ftype_uw2, FRV_BUILTIN_MHTOB);
  def_builtin ("__MBTOHE", void_ftype_uw4_uw1, FRV_BUILTIN_MBTOHE);
  def_builtin ("__MCLRACC", void_ftype_acc, FRV_BUILTIN_MCLRACC);
  def_builtin ("__MCLRACCA", void_ftype_void, FRV_BUILTIN_MCLRACCA);
  def_builtin ("__MRDACC", uw1_ftype_acc, FRV_BUILTIN_MRDACC);
  def_builtin ("__MRDACCG", uw1_ftype_acc, FRV_BUILTIN_MRDACCG);
  def_builtin ("__MWTACC", void_ftype_acc_uw1, FRV_BUILTIN_MWTACC);
  def_builtin ("__MWTACCG", void_ftype_acc_uw1, FRV_BUILTIN_MWTACCG);
  def_builtin ("__Mcop1", uw1_ftype_uw1_uw1, FRV_BUILTIN_MCOP1);
  def_builtin ("__Mcop2", uw1_ftype_uw1_uw1, FRV_BUILTIN_MCOP2);
  def_builtin ("__MTRAP", void_ftype_void, FRV_BUILTIN_MTRAP);
  def_builtin ("__MQXMACHS", void_ftype_acc_sw2_sw2, FRV_BUILTIN_MQXMACHS);
  def_builtin ("__MQXMACXHS", void_ftype_acc_sw2_sw2, FRV_BUILTIN_MQXMACXHS);
  def_builtin ("__MQMACXHS", void_ftype_acc_sw2_sw2, FRV_BUILTIN_MQMACXHS);
  def_builtin ("__MADDACCS", void_ftype_acc_acc, FRV_BUILTIN_MADDACCS);
  def_builtin ("__MSUBACCS", void_ftype_acc_acc, FRV_BUILTIN_MSUBACCS);
  def_builtin ("__MASACCS", void_ftype_acc_acc, FRV_BUILTIN_MASACCS);
  def_builtin ("__MDADDACCS", void_ftype_acc_acc, FRV_BUILTIN_MDADDACCS);
  def_builtin ("__MDSUBACCS", void_ftype_acc_acc, FRV_BUILTIN_MDSUBACCS);
  def_builtin ("__MDASACCS", void_ftype_acc_acc, FRV_BUILTIN_MDASACCS);
  def_builtin ("__MABSHS", uw1_ftype_sw1, FRV_BUILTIN_MABSHS);
  def_builtin ("__MDROTLI", uw2_ftype_uw2_int, FRV_BUILTIN_MDROTLI);
  def_builtin ("__MCPLHI", uw1_ftype_uw2_int, FRV_BUILTIN_MCPLHI);
  def_builtin ("__MCPLI", uw1_ftype_uw2_int, FRV_BUILTIN_MCPLI);
  def_builtin ("__MDCUTSSI", uw2_ftype_acc_int, FRV_BUILTIN_MDCUTSSI);
  def_builtin ("__MQSATHS", sw2_ftype_sw2_sw2, FRV_BUILTIN_MQSATHS);
  def_builtin ("__MHSETLOS", sw1_ftype_sw1_int, FRV_BUILTIN_MHSETLOS);
  def_builtin ("__MHSETHIS", sw1_ftype_sw1_int, FRV_BUILTIN_MHSETHIS);
  def_builtin ("__MHDSETS", sw1_ftype_int, FRV_BUILTIN_MHDSETS);
  def_builtin ("__MHSETLOH", uw1_ftype_uw1_int, FRV_BUILTIN_MHSETLOH);
  def_builtin ("__MHSETHIH", uw1_ftype_uw1_int, FRV_BUILTIN_MHSETHIH);
  def_builtin ("__MHDSETH", uw1_ftype_uw1_int, FRV_BUILTIN_MHDSETH);
  def_builtin ("__MQLCLRHS", sw2_ftype_sw2_sw2, FRV_BUILTIN_MQLCLRHS);
  def_builtin ("__MQLMTHS", sw2_ftype_sw2_sw2, FRV_BUILTIN_MQLMTHS);
  def_builtin ("__MQSLLHI", uw2_ftype_uw2_int, FRV_BUILTIN_MQSLLHI);
  def_builtin ("__MQSRAHI", sw2_ftype_sw2_int, FRV_BUILTIN_MQSRAHI);
  def_builtin ("__SMUL", sw2_ftype_sw1_sw1, FRV_BUILTIN_SMUL);
  def_builtin ("__UMUL", uw2_ftype_uw1_uw1, FRV_BUILTIN_UMUL);
  def_builtin ("__SMASS", void_ftype_sw1_sw1, FRV_BUILTIN_SMASS);
  def_builtin ("__SMSSS", void_ftype_sw1_sw1, FRV_BUILTIN_SMSSS);
  def_builtin ("__SMU", void_ftype_sw1_sw1, FRV_BUILTIN_SMU);
  def_builtin ("__ADDSS", sw1_ftype_sw1_sw1, FRV_BUILTIN_ADDSS);
  def_builtin ("__SUBSS", sw1_ftype_sw1_sw1, FRV_BUILTIN_SUBSS);
  def_builtin ("__SLASS", sw1_ftype_sw1_sw1, FRV_BUILTIN_SLASS);
  def_builtin ("__SCAN", sw1_ftype_sw1_sw1, FRV_BUILTIN_SCAN);
  def_builtin ("__SCUTSS", sw1_ftype_sw1, FRV_BUILTIN_SCUTSS);
  def_builtin ("__IACCreadll", sw2_ftype_iacc, FRV_BUILTIN_IACCreadll);
  def_builtin ("__IACCreadl", sw1_ftype_iacc, FRV_BUILTIN_IACCreadl);
  def_builtin ("__IACCsetll", void_ftype_iacc_sw2, FRV_BUILTIN_IACCsetll);
  def_builtin ("__IACCsetl", void_ftype_iacc_sw1, FRV_BUILTIN_IACCsetl);
  def_builtin ("__data_prefetch0", void_ftype_ptr, FRV_BUILTIN_PREFETCH0);
  def_builtin ("__data_prefetch", void_ftype_ptr, FRV_BUILTIN_PREFETCH);
  def_builtin ("__builtin_read8", uw1_ftype_vptr, FRV_BUILTIN_READ8);
  def_builtin ("__builtin_read16", uw1_ftype_vptr, FRV_BUILTIN_READ16);
  def_builtin ("__builtin_read32", uw1_ftype_vptr, FRV_BUILTIN_READ32);
  def_builtin ("__builtin_read64", uw2_ftype_vptr, FRV_BUILTIN_READ64);

  def_builtin ("__builtin_write8", void_ftype_vptr_ub, FRV_BUILTIN_WRITE8);
  def_builtin ("__builtin_write16", void_ftype_vptr_uh, FRV_BUILTIN_WRITE16);
  def_builtin ("__builtin_write32", void_ftype_vptr_uw1, FRV_BUILTIN_WRITE32);
  def_builtin ("__builtin_write64", void_ftype_vptr_uw2, FRV_BUILTIN_WRITE64);

#undef UNARY
#undef BINARY
#undef TRINARY
#undef QUAD
}

/* Set the names for various arithmetic operations according to the
   FRV ABI.  */
static void
frv_init_libfuncs (void)
{
  set_optab_libfunc (smod_optab,     SImode, "__modi");
  set_optab_libfunc (umod_optab,     SImode, "__umodi");

  set_optab_libfunc (add_optab,      DImode, "__addll");
  set_optab_libfunc (sub_optab,      DImode, "__subll");
  set_optab_libfunc (smul_optab,     DImode, "__mulll");
  set_optab_libfunc (sdiv_optab,     DImode, "__divll");
  set_optab_libfunc (smod_optab,     DImode, "__modll");
  set_optab_libfunc (umod_optab,     DImode, "__umodll");
  set_optab_libfunc (and_optab,      DImode, "__andll");
  set_optab_libfunc (ior_optab,      DImode, "__orll");
  set_optab_libfunc (xor_optab,      DImode, "__xorll");
  set_optab_libfunc (one_cmpl_optab, DImode, "__notll");

  set_optab_libfunc (add_optab,      SFmode, "__addf");
  set_optab_libfunc (sub_optab,      SFmode, "__subf");
  set_optab_libfunc (smul_optab,     SFmode, "__mulf");
  set_optab_libfunc (sdiv_optab,     SFmode, "__divf");

  set_optab_libfunc (add_optab,      DFmode, "__addd");
  set_optab_libfunc (sub_optab,      DFmode, "__subd");
  set_optab_libfunc (smul_optab,     DFmode, "__muld");
  set_optab_libfunc (sdiv_optab,     DFmode, "__divd");

  set_conv_libfunc (sext_optab,   DFmode, SFmode, "__ftod");
  set_conv_libfunc (trunc_optab,  SFmode, DFmode, "__dtof");

  set_conv_libfunc (sfix_optab,   SImode, SFmode, "__ftoi");
  set_conv_libfunc (sfix_optab,   DImode, SFmode, "__ftoll");
  set_conv_libfunc (sfix_optab,   SImode, DFmode, "__dtoi");
  set_conv_libfunc (sfix_optab,   DImode, DFmode, "__dtoll");

  set_conv_libfunc (ufix_optab,   SImode, SFmode, "__ftoui");
  set_conv_libfunc (ufix_optab,   DImode, SFmode, "__ftoull");
  set_conv_libfunc (ufix_optab,   SImode, DFmode, "__dtoui");
  set_conv_libfunc (ufix_optab,   DImode, DFmode, "__dtoull");

  set_conv_libfunc (sfloat_optab, SFmode, SImode, "__itof");
  set_conv_libfunc (sfloat_optab, SFmode, DImode, "__lltof");
  set_conv_libfunc (sfloat_optab, DFmode, SImode, "__itod");
  set_conv_libfunc (sfloat_optab, DFmode, DImode, "__lltod");
}

/* Convert an integer constant to an accumulator register.  ICODE is the
   code of the target instruction, OPNUM is the number of the
   accumulator operand and OPVAL is the constant integer.  Try both
   ACC and ACCG registers; only report an error if neither fit the
   instruction.  */

static rtx
frv_int_to_acc (enum insn_code icode, int opnum, rtx opval)
{
  rtx reg;
  int i;

  /* ACCs and ACCGs are implicit global registers if media intrinsics
     are being used.  We set up this lazily to avoid creating lots of
     unnecessary call_insn rtl in non-media code.  */
  for (i = 0; i <= ACC_MASK; i++)
    if ((i & ACC_MASK) == i)
      global_regs[i + ACC_FIRST] = global_regs[i + ACCG_FIRST] = 1;

  if (GET_CODE (opval) != CONST_INT)
    {
      error ("accumulator is not a constant integer");
      return NULL_RTX;
    }
  if ((INTVAL (opval) & ~ACC_MASK) != 0)
    {
      error ("accumulator number is out of bounds");
      return NULL_RTX;
    }

  reg = gen_rtx_REG (insn_data[icode].operand[opnum].mode,
		     ACC_FIRST + INTVAL (opval));
  if (! (*insn_data[icode].operand[opnum].predicate) (reg, VOIDmode))
    REGNO (reg) = ACCG_FIRST + INTVAL (opval);

  if (! (*insn_data[icode].operand[opnum].predicate) (reg, VOIDmode))
    {
      error ("inappropriate accumulator for %qs", insn_data[icode].name);
      return NULL_RTX;
    }
  return reg;
}

/* If an ACC rtx has mode MODE, return the mode that the matching ACCG
   should have.  */

static enum machine_mode
frv_matching_accg_mode (enum machine_mode mode)
{
  switch (mode)
    {
    case V4SImode:
      return V4QImode;

    case DImode:
      return HImode;

    case SImode:
      return QImode;

    default:
      gcc_unreachable ();
    }
}

/* Given that a __builtin_read or __builtin_write function is accessing
   address ADDRESS, return the value that should be used as operand 1
   of the membar.  */

static rtx
frv_io_address_cookie (rtx address)
{
  return (GET_CODE (address) == CONST_INT
	  ? GEN_INT (INTVAL (address) / 8 * 8)
	  : const0_rtx);
}

/* Return the accumulator guard that should be paired with accumulator
   register ACC.  The mode of the returned register is in the same
   class as ACC, but is four times smaller.  */

rtx
frv_matching_accg_for_acc (rtx acc)
{
  return gen_rtx_REG (frv_matching_accg_mode (GET_MODE (acc)),
		      REGNO (acc) - ACC_FIRST + ACCG_FIRST);
}

/* Read a value from the head of the tree list pointed to by ARGLISTPTR.
   Return the value as an rtx and replace *ARGLISTPTR with the tail of the
   list.  */

static rtx
frv_read_argument (tree *arglistptr)
{
  tree next = TREE_VALUE (*arglistptr);
  *arglistptr = TREE_CHAIN (*arglistptr);
  return expand_expr (next, NULL_RTX, VOIDmode, 0);
}

/* Like frv_read_argument, but interpret the argument as the number
   of an IACC register and return a (reg:MODE ...) rtx for it.  */

static rtx
frv_read_iacc_argument (enum machine_mode mode, tree *arglistptr)
{
  int i, regno;
  rtx op;

  op = frv_read_argument (arglistptr);
  if (GET_CODE (op) != CONST_INT
      || INTVAL (op) < 0
      || INTVAL (op) > IACC_LAST - IACC_FIRST
      || ((INTVAL (op) * 4) & (GET_MODE_SIZE (mode) - 1)) != 0)
    {
      error ("invalid IACC argument");
      op = const0_rtx;
    }

  /* IACCs are implicit global registers.  We set up this lazily to
     avoid creating lots of unnecessary call_insn rtl when IACCs aren't
     being used.  */
  regno = INTVAL (op) + IACC_FIRST;
  for (i = 0; i < HARD_REGNO_NREGS (regno, mode); i++)
    global_regs[regno + i] = 1;

  return gen_rtx_REG (mode, regno);
}

/* Return true if OPVAL can be used for operand OPNUM of instruction ICODE.
   The instruction should require a constant operand of some sort.  The
   function prints an error if OPVAL is not valid.  */

static int
frv_check_constant_argument (enum insn_code icode, int opnum, rtx opval)
{
  if (GET_CODE (opval) != CONST_INT)
    {
      error ("%qs expects a constant argument", insn_data[icode].name);
      return FALSE;
    }
  if (! (*insn_data[icode].operand[opnum].predicate) (opval, VOIDmode))
    {
      error ("constant argument out of range for %qs", insn_data[icode].name);
      return FALSE;
    }
  return TRUE;
}

/* Return a legitimate rtx for instruction ICODE's return value.  Use TARGET
   if it's not null, has the right mode, and satisfies operand 0's
   predicate.  */

static rtx
frv_legitimize_target (enum insn_code icode, rtx target)
{
  enum machine_mode mode = insn_data[icode].operand[0].mode;

  if (! target
      || GET_MODE (target) != mode
      || ! (*insn_data[icode].operand[0].predicate) (target, mode))
    return gen_reg_rtx (mode);
  else
    return target;
}

/* Given that ARG is being passed as operand OPNUM to instruction ICODE,
   check whether ARG satisfies the operand's constraints.  If it doesn't,
   copy ARG to a temporary register and return that.  Otherwise return ARG
   itself.  */

static rtx
frv_legitimize_argument (enum insn_code icode, int opnum, rtx arg)
{
  enum machine_mode mode = insn_data[icode].operand[opnum].mode;

  if ((*insn_data[icode].operand[opnum].predicate) (arg, mode))
    return arg;
  else
    return copy_to_mode_reg (mode, arg);
}

/* Return a volatile memory reference of mode MODE whose address is ARG.  */

static rtx
frv_volatile_memref (enum machine_mode mode, rtx arg)
{
  rtx mem;

  mem = gen_rtx_MEM (mode, memory_address (mode, arg));
  MEM_VOLATILE_P (mem) = 1;
  return mem;
}

/* Expand builtins that take a single, constant argument.  At the moment,
   only MHDSETS falls into this category.  */

static rtx
frv_expand_set_builtin (enum insn_code icode, tree arglist, rtx target)
{
  rtx pat;
  rtx op0 = frv_read_argument (&arglist);

  if (! frv_check_constant_argument (icode, 1, op0))
    return NULL_RTX;

  target = frv_legitimize_target (icode, target);
  pat = GEN_FCN (icode) (target, op0);
  if (! pat)
    return NULL_RTX;

  emit_insn (pat);
  return target;
}

/* Expand builtins that take one operand.  */

static rtx
frv_expand_unop_builtin (enum insn_code icode, tree arglist, rtx target)
{
  rtx pat;
  rtx op0 = frv_read_argument (&arglist);

  target = frv_legitimize_target (icode, target);
  op0 = frv_legitimize_argument (icode, 1, op0);
  pat = GEN_FCN (icode) (target, op0);
  if (! pat)
    return NULL_RTX;

  emit_insn (pat);
  return target;
}

/* Expand builtins that take two operands.  */

static rtx
frv_expand_binop_builtin (enum insn_code icode, tree arglist, rtx target)
{
  rtx pat;
  rtx op0 = frv_read_argument (&arglist);
  rtx op1 = frv_read_argument (&arglist);

  target = frv_legitimize_target (icode, target);
  op0 = frv_legitimize_argument (icode, 1, op0);
  op1 = frv_legitimize_argument (icode, 2, op1);
  pat = GEN_FCN (icode) (target, op0, op1);
  if (! pat)
    return NULL_RTX;

  emit_insn (pat);
  return target;
}

/* Expand cut-style builtins, which take two operands and an implicit ACCG
   one.  */

static rtx
frv_expand_cut_builtin (enum insn_code icode, tree arglist, rtx target)
{
  rtx pat;
  rtx op0 = frv_read_argument (&arglist);
  rtx op1 = frv_read_argument (&arglist);
  rtx op2;

  target = frv_legitimize_target (icode, target);
  op0 = frv_int_to_acc (icode, 1, op0);
  if (! op0)
    return NULL_RTX;

  if (icode == CODE_FOR_mdcutssi || GET_CODE (op1) == CONST_INT)
    {
      if (! frv_check_constant_argument (icode, 2, op1))
    	return NULL_RTX;
    }
  else
    op1 = frv_legitimize_argument (icode, 2, op1);

  op2 = frv_matching_accg_for_acc (op0);
  pat = GEN_FCN (icode) (target, op0, op1, op2);
  if (! pat)
    return NULL_RTX;

  emit_insn (pat);
  return target;
}

/* Expand builtins that take two operands and the second is immediate.  */

static rtx
frv_expand_binopimm_builtin (enum insn_code icode, tree arglist, rtx target)
{
  rtx pat;
  rtx op0 = frv_read_argument (&arglist);
  rtx op1 = frv_read_argument (&arglist);

  if (! frv_check_constant_argument (icode, 2, op1))
    return NULL_RTX;

  target = frv_legitimize_target (icode, target);
  op0 = frv_legitimize_argument (icode, 1, op0);
  pat = GEN_FCN (icode) (target, op0, op1);
  if (! pat)
    return NULL_RTX;

  emit_insn (pat);
  return target;
}

/* Expand builtins that take two operands, the first operand being a pointer to
   ints and return void.  */

static rtx
frv_expand_voidbinop_builtin (enum insn_code icode, tree arglist)
{
  rtx pat;
  rtx op0 = frv_read_argument (&arglist);
  rtx op1 = frv_read_argument (&arglist);
  enum machine_mode mode0 = insn_data[icode].operand[0].mode;
  rtx addr;

  if (GET_CODE (op0) != MEM)
    {
      rtx reg = op0;

      if (! offsettable_address_p (0, mode0, op0))
	{
	  reg = gen_reg_rtx (Pmode);
	  emit_insn (gen_rtx_SET (VOIDmode, reg, op0));
	}

      op0 = gen_rtx_MEM (SImode, reg);
    }

  addr = XEXP (op0, 0);
  if (! offsettable_address_p (0, mode0, addr))
    addr = copy_to_mode_reg (Pmode, op0);

  op0 = change_address (op0, V4SImode, addr);
  op1 = frv_legitimize_argument (icode, 1, op1);
  pat = GEN_FCN (icode) (op0, op1);
  if (! pat)
    return 0;

  emit_insn (pat);
  return 0;
}

/* Expand builtins that take two long operands and return void.  */

static rtx
frv_expand_int_void2arg (enum insn_code icode, tree arglist)
{
  rtx pat;
  rtx op0 = frv_read_argument (&arglist);
  rtx op1 = frv_read_argument (&arglist);

  op0 = frv_legitimize_argument (icode, 1, op0);
  op1 = frv_legitimize_argument (icode, 1, op1);
  pat = GEN_FCN (icode) (op0, op1);
  if (! pat)
    return NULL_RTX;

  emit_insn (pat);
  return NULL_RTX;
}

/* Expand prefetch builtins.  These take a single address as argument.  */

static rtx
frv_expand_prefetches (enum insn_code icode, tree arglist)
{
  rtx pat;
  rtx op0 = frv_read_argument (&arglist);

  pat = GEN_FCN (icode) (force_reg (Pmode, op0));
  if (! pat)
    return 0;

  emit_insn (pat);
  return 0;
}

/* Expand builtins that take three operands and return void.  The first
   argument must be a constant that describes a pair or quad accumulators.  A
   fourth argument is created that is the accumulator guard register that
   corresponds to the accumulator.  */

static rtx
frv_expand_voidtriop_builtin (enum insn_code icode, tree arglist)
{
  rtx pat;
  rtx op0 = frv_read_argument (&arglist);
  rtx op1 = frv_read_argument (&arglist);
  rtx op2 = frv_read_argument (&arglist);
  rtx op3;

  op0 = frv_int_to_acc (icode, 0, op0);
  if (! op0)
    return NULL_RTX;

  op1 = frv_legitimize_argument (icode, 1, op1);
  op2 = frv_legitimize_argument (icode, 2, op2);
  op3 = frv_matching_accg_for_acc (op0);
  pat = GEN_FCN (icode) (op0, op1, op2, op3);
  if (! pat)
    return NULL_RTX;

  emit_insn (pat);
  return NULL_RTX;
}

/* Expand builtins that perform accumulator-to-accumulator operations.
   These builtins take two accumulator numbers as argument and return
   void.  */

static rtx
frv_expand_voidaccop_builtin (enum insn_code icode, tree arglist)
{
  rtx pat;
  rtx op0 = frv_read_argument (&arglist);
  rtx op1 = frv_read_argument (&arglist);
  rtx op2;
  rtx op3;

  op0 = frv_int_to_acc (icode, 0, op0);
  if (! op0)
    return NULL_RTX;

  op1 = frv_int_to_acc (icode, 1, op1);
  if (! op1)
    return NULL_RTX;

  op2 = frv_matching_accg_for_acc (op0);
  op3 = frv_matching_accg_for_acc (op1);
  pat = GEN_FCN (icode) (op0, op1, op2, op3);
  if (! pat)
    return NULL_RTX;

  emit_insn (pat);
  return NULL_RTX;
}

/* Expand a __builtin_read* function.  ICODE is the instruction code for the
   membar and TARGET_MODE is the mode that the loaded value should have.  */

static rtx
frv_expand_load_builtin (enum insn_code icode, enum machine_mode target_mode,
                         tree arglist, rtx target)
{
  rtx op0 = frv_read_argument (&arglist);
  rtx cookie = frv_io_address_cookie (op0);

  if (target == 0 || !REG_P (target))
    target = gen_reg_rtx (target_mode);
  op0 = frv_volatile_memref (insn_data[icode].operand[0].mode, op0);
  convert_move (target, op0, 1);
  emit_insn (GEN_FCN (icode) (copy_rtx (op0), cookie, GEN_INT (FRV_IO_READ)));
  cfun->machine->has_membar_p = 1;
  return target;
}

/* Likewise __builtin_write* functions.  */

static rtx
frv_expand_store_builtin (enum insn_code icode, tree arglist)
{
  rtx op0 = frv_read_argument (&arglist);
  rtx op1 = frv_read_argument (&arglist);
  rtx cookie = frv_io_address_cookie (op0);

  op0 = frv_volatile_memref (insn_data[icode].operand[0].mode, op0);
  convert_move (op0, force_reg (insn_data[icode].operand[0].mode, op1), 1);
  emit_insn (GEN_FCN (icode) (copy_rtx (op0), cookie, GEN_INT (FRV_IO_WRITE)));
  cfun->machine->has_membar_p = 1;
  return NULL_RTX;
}

/* Expand the MDPACKH builtin.  It takes four unsigned short arguments and
   each argument forms one word of the two double-word input registers.
   ARGLIST is a TREE_LIST of the arguments and TARGET, if nonnull,
   suggests a good place to put the return value.  */

static rtx
frv_expand_mdpackh_builtin (tree arglist, rtx target)
{
  enum insn_code icode = CODE_FOR_mdpackh;
  rtx pat, op0, op1;
  rtx arg1 = frv_read_argument (&arglist);
  rtx arg2 = frv_read_argument (&arglist);
  rtx arg3 = frv_read_argument (&arglist);
  rtx arg4 = frv_read_argument (&arglist);

  target = frv_legitimize_target (icode, target);
  op0 = gen_reg_rtx (DImode);
  op1 = gen_reg_rtx (DImode);

  /* The high half of each word is not explicitly initialized, so indicate
     that the input operands are not live before this point.  */
  emit_insn (gen_rtx_CLOBBER (DImode, op0));
  emit_insn (gen_rtx_CLOBBER (DImode, op1));

  /* Move each argument into the low half of its associated input word.  */
  emit_move_insn (simplify_gen_subreg (HImode, op0, DImode, 2), arg1);
  emit_move_insn (simplify_gen_subreg (HImode, op0, DImode, 6), arg2);
  emit_move_insn (simplify_gen_subreg (HImode, op1, DImode, 2), arg3);
  emit_move_insn (simplify_gen_subreg (HImode, op1, DImode, 6), arg4);

  pat = GEN_FCN (icode) (target, op0, op1);
  if (! pat)
    return NULL_RTX;

  emit_insn (pat);
  return target;
}

/* Expand the MCLRACC builtin.  This builtin takes a single accumulator
   number as argument.  */

static rtx
frv_expand_mclracc_builtin (tree arglist)
{
  enum insn_code icode = CODE_FOR_mclracc;
  rtx pat;
  rtx op0 = frv_read_argument (&arglist);

  op0 = frv_int_to_acc (icode, 0, op0);
  if (! op0)
    return NULL_RTX;

  pat = GEN_FCN (icode) (op0);
  if (pat)
    emit_insn (pat);

  return NULL_RTX;
}

/* Expand builtins that take no arguments.  */

static rtx
frv_expand_noargs_builtin (enum insn_code icode)
{
  rtx pat = GEN_FCN (icode) (const0_rtx);
  if (pat)
    emit_insn (pat);

  return NULL_RTX;
}

/* Expand MRDACC and MRDACCG.  These builtins take a single accumulator
   number or accumulator guard number as argument and return an SI integer.  */

static rtx
frv_expand_mrdacc_builtin (enum insn_code icode, tree arglist)
{
  rtx pat;
  rtx target = gen_reg_rtx (SImode);
  rtx op0 = frv_read_argument (&arglist);

  op0 = frv_int_to_acc (icode, 1, op0);
  if (! op0)
    return NULL_RTX;

  pat = GEN_FCN (icode) (target, op0);
  if (! pat)
    return NULL_RTX;

  emit_insn (pat);
  return target;
}

/* Expand MWTACC and MWTACCG.  These builtins take an accumulator or
   accumulator guard as their first argument and an SImode value as their
   second.  */

static rtx
frv_expand_mwtacc_builtin (enum insn_code icode, tree arglist)
{
  rtx pat;
  rtx op0 = frv_read_argument (&arglist);
  rtx op1 = frv_read_argument (&arglist);

  op0 = frv_int_to_acc (icode, 0, op0);
  if (! op0)
    return NULL_RTX;

  op1 = frv_legitimize_argument (icode, 1, op1);
  pat = GEN_FCN (icode) (op0, op1);
  if (pat)
    emit_insn (pat);

  return NULL_RTX;
}

/* Emit a move from SRC to DEST in SImode chunks.  This can be used
   to move DImode values into and out of IACC0.  */

static void
frv_split_iacc_move (rtx dest, rtx src)
{
  enum machine_mode inner;
  int i;

  inner = GET_MODE (dest);
  for (i = 0; i < GET_MODE_SIZE (inner); i += GET_MODE_SIZE (SImode))
    emit_move_insn (simplify_gen_subreg (SImode, dest, inner, i),
		    simplify_gen_subreg (SImode, src, inner, i));
}

/* Expand builtins.  */

static rtx
frv_expand_builtin (tree exp,
                    rtx target,
                    rtx subtarget ATTRIBUTE_UNUSED,
                    enum machine_mode mode ATTRIBUTE_UNUSED,
                    int ignore ATTRIBUTE_UNUSED)
{
  tree arglist = TREE_OPERAND (exp, 1);
  tree fndecl = TREE_OPERAND (TREE_OPERAND (exp, 0), 0);
  unsigned fcode = (unsigned)DECL_FUNCTION_CODE (fndecl);
  unsigned i;
  struct builtin_description *d;

  if (fcode < FRV_BUILTIN_FIRST_NONMEDIA && !TARGET_MEDIA)
    {
      error ("media functions are not available unless -mmedia is used");
      return NULL_RTX;
    }

  switch (fcode)
    {
    case FRV_BUILTIN_MCOP1:
    case FRV_BUILTIN_MCOP2:
    case FRV_BUILTIN_MDUNPACKH:
    case FRV_BUILTIN_MBTOHE:
      if (! TARGET_MEDIA_REV1)
	{
	  error ("this media function is only available on the fr500");
	  return NULL_RTX;
	}
      break;

    case FRV_BUILTIN_MQXMACHS:
    case FRV_BUILTIN_MQXMACXHS:
    case FRV_BUILTIN_MQMACXHS:
    case FRV_BUILTIN_MADDACCS:
    case FRV_BUILTIN_MSUBACCS:
    case FRV_BUILTIN_MASACCS:
    case FRV_BUILTIN_MDADDACCS:
    case FRV_BUILTIN_MDSUBACCS:
    case FRV_BUILTIN_MDASACCS:
    case FRV_BUILTIN_MABSHS:
    case FRV_BUILTIN_MDROTLI:
    case FRV_BUILTIN_MCPLHI:
    case FRV_BUILTIN_MCPLI:
    case FRV_BUILTIN_MDCUTSSI:
    case FRV_BUILTIN_MQSATHS:
    case FRV_BUILTIN_MHSETLOS:
    case FRV_BUILTIN_MHSETLOH:
    case FRV_BUILTIN_MHSETHIS:
    case FRV_BUILTIN_MHSETHIH:
    case FRV_BUILTIN_MHDSETS:
    case FRV_BUILTIN_MHDSETH:
      if (! TARGET_MEDIA_REV2)
	{
	  error ("this media function is only available on the fr400"
		 " and fr550");
	  return NULL_RTX;
	}
      break;

    case FRV_BUILTIN_SMASS:
    case FRV_BUILTIN_SMSSS:
    case FRV_BUILTIN_SMU:
    case FRV_BUILTIN_ADDSS:
    case FRV_BUILTIN_SUBSS:
    case FRV_BUILTIN_SLASS:
    case FRV_BUILTIN_SCUTSS:
    case FRV_BUILTIN_IACCreadll:
    case FRV_BUILTIN_IACCreadl:
    case FRV_BUILTIN_IACCsetll:
    case FRV_BUILTIN_IACCsetl:
      if (!TARGET_FR405_BUILTINS)
	{
	  error ("this builtin function is only available"
		 " on the fr405 and fr450");
	  return NULL_RTX;
	}
      break;

    case FRV_BUILTIN_PREFETCH:
      if (!TARGET_FR500_FR550_BUILTINS)
	{
	  error ("this builtin function is only available on the fr500"
		 " and fr550");
	  return NULL_RTX;
	}
      break;

    case FRV_BUILTIN_MQLCLRHS:
    case FRV_BUILTIN_MQLMTHS:
    case FRV_BUILTIN_MQSLLHI:
    case FRV_BUILTIN_MQSRAHI:
      if (!TARGET_MEDIA_FR450)
	{
	  error ("this builtin function is only available on the fr450");
	  return NULL_RTX;
	}
      break;

    default:
      break;
    }

  /* Expand unique builtins.  */

  switch (fcode)
    {
    case FRV_BUILTIN_MTRAP:
      return frv_expand_noargs_builtin (CODE_FOR_mtrap);

    case FRV_BUILTIN_MCLRACC:
      return frv_expand_mclracc_builtin (arglist);

    case FRV_BUILTIN_MCLRACCA:
      if (TARGET_ACC_8)
	return frv_expand_noargs_builtin (CODE_FOR_mclracca8);
      else
	return frv_expand_noargs_builtin (CODE_FOR_mclracca4);

    case FRV_BUILTIN_MRDACC:
      return frv_expand_mrdacc_builtin (CODE_FOR_mrdacc, arglist);

    case FRV_BUILTIN_MRDACCG:
      return frv_expand_mrdacc_builtin (CODE_FOR_mrdaccg, arglist);

    case FRV_BUILTIN_MWTACC:
      return frv_expand_mwtacc_builtin (CODE_FOR_mwtacc, arglist);

    case FRV_BUILTIN_MWTACCG:
      return frv_expand_mwtacc_builtin (CODE_FOR_mwtaccg, arglist);

    case FRV_BUILTIN_MDPACKH:
      return frv_expand_mdpackh_builtin (arglist, target);

    case FRV_BUILTIN_IACCreadll:
      {
	rtx src = frv_read_iacc_argument (DImode, &arglist);
	if (target == 0 || !REG_P (target))
	  target = gen_reg_rtx (DImode);
	frv_split_iacc_move (target, src);
	return target;
      }

    case FRV_BUILTIN_IACCreadl:
      return frv_read_iacc_argument (SImode, &arglist);

    case FRV_BUILTIN_IACCsetll:
      {
	rtx dest = frv_read_iacc_argument (DImode, &arglist);
	rtx src = frv_read_argument (&arglist);
	frv_split_iacc_move (dest, force_reg (DImode, src));
	return 0;
      }

    case FRV_BUILTIN_IACCsetl:
      {
	rtx dest = frv_read_iacc_argument (SImode, &arglist);
	rtx src = frv_read_argument (&arglist);
	emit_move_insn (dest, force_reg (SImode, src));
	return 0;
      }

    default:
      break;
    }

  /* Expand groups of builtins.  */

  for (i = 0, d = bdesc_set; i < ARRAY_SIZE (bdesc_set); i++, d++)
    if (d->code == fcode)
      return frv_expand_set_builtin (d->icode, arglist, target);

  for (i = 0, d = bdesc_1arg; i < ARRAY_SIZE (bdesc_1arg); i++, d++)
    if (d->code == fcode)
      return frv_expand_unop_builtin (d->icode, arglist, target);

  for (i = 0, d = bdesc_2arg; i < ARRAY_SIZE (bdesc_2arg); i++, d++)
    if (d->code == fcode)
      return frv_expand_binop_builtin (d->icode, arglist, target);

  for (i = 0, d = bdesc_cut; i < ARRAY_SIZE (bdesc_cut); i++, d++)
    if (d->code == fcode)
      return frv_expand_cut_builtin (d->icode, arglist, target);

  for (i = 0, d = bdesc_2argimm; i < ARRAY_SIZE (bdesc_2argimm); i++, d++)
    if (d->code == fcode)
      return frv_expand_binopimm_builtin (d->icode, arglist, target);

  for (i = 0, d = bdesc_void2arg; i < ARRAY_SIZE (bdesc_void2arg); i++, d++)
    if (d->code == fcode)
      return frv_expand_voidbinop_builtin (d->icode, arglist);

  for (i = 0, d = bdesc_void3arg; i < ARRAY_SIZE (bdesc_void3arg); i++, d++)
    if (d->code == fcode)
      return frv_expand_voidtriop_builtin (d->icode, arglist);

  for (i = 0, d = bdesc_voidacc; i < ARRAY_SIZE (bdesc_voidacc); i++, d++)
    if (d->code == fcode)
      return frv_expand_voidaccop_builtin (d->icode, arglist);

  for (i = 0, d = bdesc_int_void2arg;
       i < ARRAY_SIZE (bdesc_int_void2arg); i++, d++)
    if (d->code == fcode)
      return frv_expand_int_void2arg (d->icode, arglist);

  for (i = 0, d = bdesc_prefetches;
       i < ARRAY_SIZE (bdesc_prefetches); i++, d++)
    if (d->code == fcode)
      return frv_expand_prefetches (d->icode, arglist);

  for (i = 0, d = bdesc_loads; i < ARRAY_SIZE (bdesc_loads); i++, d++)
    if (d->code == fcode)
      return frv_expand_load_builtin (d->icode, TYPE_MODE (TREE_TYPE (exp)),
				      arglist, target);

  for (i = 0, d = bdesc_stores; i < ARRAY_SIZE (bdesc_stores); i++, d++)
    if (d->code == fcode)
      return frv_expand_store_builtin (d->icode, arglist);

  return 0;
}

static bool
frv_in_small_data_p (tree decl)
{
  HOST_WIDE_INT size;
  tree section_name;

  /* Don't apply the -G flag to internal compiler structures.  We
     should leave such structures in the main data section, partly
     for efficiency and partly because the size of some of them
     (such as C++ typeinfos) is not known until later.  */
  if (TREE_CODE (decl) != VAR_DECL || DECL_ARTIFICIAL (decl))
    return false;

  /* If we already know which section the decl should be in, see if
     it's a small data section.  */
  section_name = DECL_SECTION_NAME (decl);
  if (section_name)
    {
      gcc_assert (TREE_CODE (section_name) == STRING_CST);
      if (frv_string_begins_with (section_name, ".sdata"))
	return true;
      if (frv_string_begins_with (section_name, ".sbss"))
	return true;
      return false;
    }

  size = int_size_in_bytes (TREE_TYPE (decl));
  if (size > 0 && (unsigned HOST_WIDE_INT) size <= g_switch_value)
    return true;

  return false;
}

static bool
frv_rtx_costs (rtx x,
               int code ATTRIBUTE_UNUSED,
               int outer_code ATTRIBUTE_UNUSED,
               int *total)
{
  if (outer_code == MEM)
    {
      /* Don't differentiate between memory addresses.  All the ones
	 we accept have equal cost.  */
      *total = COSTS_N_INSNS (0);
      return true;
    }

  switch (code)
    {
    case CONST_INT:
      /* Make 12 bit integers really cheap.  */
      if (IN_RANGE_P (INTVAL (x), -2048, 2047))
	{
	  *total = 0;
	  return true;
	}
      /* Fall through.  */

    case CONST:
    case LABEL_REF:
    case SYMBOL_REF:
    case CONST_DOUBLE:
      *total = COSTS_N_INSNS (2);
      return true;

    case PLUS:
    case MINUS:
    case AND:
    case IOR:
    case XOR:
    case ASHIFT:
    case ASHIFTRT:
    case LSHIFTRT:
    case NOT:
    case NEG:
    case COMPARE:
      if (GET_MODE (x) == SImode)
	*total = COSTS_N_INSNS (1);
      else if (GET_MODE (x) == DImode)
        *total = COSTS_N_INSNS (2);
      else
        *total = COSTS_N_INSNS (3);
      return true;

    case MULT:
      if (GET_MODE (x) == SImode)
        *total = COSTS_N_INSNS (2);
      else
        *total = COSTS_N_INSNS (6);	/* guess */
      return true;

    case DIV:
    case UDIV:
    case MOD:
    case UMOD:
      *total = COSTS_N_INSNS (18);
      return true;

    case MEM:
      *total = COSTS_N_INSNS (3);
      return true;

    default:
      return false;
    }
}

static void
frv_asm_out_constructor (rtx symbol, int priority ATTRIBUTE_UNUSED)
{
  switch_to_section (ctors_section);
  assemble_align (POINTER_SIZE);
  if (TARGET_FDPIC)
    {
      int ok = frv_assemble_integer (symbol, POINTER_SIZE / BITS_PER_UNIT, 1);

      gcc_assert (ok);
      return;
    }
  assemble_integer_with_op ("\t.picptr\t", symbol);
}

static void
frv_asm_out_destructor (rtx symbol, int priority ATTRIBUTE_UNUSED)
{
  switch_to_section (dtors_section);
  assemble_align (POINTER_SIZE);
  if (TARGET_FDPIC)
    {
      int ok = frv_assemble_integer (symbol, POINTER_SIZE / BITS_PER_UNIT, 1);
      
      gcc_assert (ok);
      return;
    }
  assemble_integer_with_op ("\t.picptr\t", symbol);
}

/* Worker function for TARGET_STRUCT_VALUE_RTX.  */

static rtx
frv_struct_value_rtx (tree fntype ATTRIBUTE_UNUSED,
		      int incoming ATTRIBUTE_UNUSED)
{
  return gen_rtx_REG (Pmode, FRV_STRUCT_VALUE_REGNUM);
}

#define TLS_BIAS (2048 - 16)

/* This is called from dwarf2out.c via TARGET_ASM_OUTPUT_DWARF_DTPREL.
   We need to emit DTP-relative relocations.  */

static void
frv_output_dwarf_dtprel (FILE *file, int size, rtx x)
{
  gcc_assert (size == 4);
  fputs ("\t.picptr\ttlsmoff(", file);
  /* We want the unbiased TLS offset, so add the bias to the
     expression, such that the implicit biasing cancels out.  */
  output_addr_const (file, plus_constant (x, TLS_BIAS));
  fputs (")", file);
}

#include "gt-frv.h"
