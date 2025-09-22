/* Subroutines used for MIPS code generation.
   Copyright (C) 1989, 1990, 1991, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by A. Lichnewsky, lich@inria.inria.fr.
   Changes by Michael Meissner, meissner@osf.org.
   64 bit r4000 support by Ian Lance Taylor, ian@cygnus.com, and
   Brendan Eich, brendan@microunity.com.

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
#include <signal.h>
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
#include "tree.h"
#include "function.h"
#include "expr.h"
#include "optabs.h"
#include "flags.h"
#include "reload.h"
#include "tm_p.h"
#include "ggc.h"
#include "gstab.h"
#include "hashtab.h"
#include "debug.h"
#include "target.h"
#include "target-def.h"
#include "integrate.h"
#include "langhooks.h"
#include "cfglayout.h"
#include "sched-int.h"
#include "tree-gimple.h"
#include "bitmap.h"

/* True if X is an unspec wrapper around a SYMBOL_REF or LABEL_REF.  */
#define UNSPEC_ADDRESS_P(X)					\
  (GET_CODE (X) == UNSPEC					\
   && XINT (X, 1) >= UNSPEC_ADDRESS_FIRST			\
   && XINT (X, 1) < UNSPEC_ADDRESS_FIRST + NUM_SYMBOL_TYPES)

/* Extract the symbol or label from UNSPEC wrapper X.  */
#define UNSPEC_ADDRESS(X) \
  XVECEXP (X, 0, 0)

/* Extract the symbol type from UNSPEC wrapper X.  */
#define UNSPEC_ADDRESS_TYPE(X) \
  ((enum mips_symbol_type) (XINT (X, 1) - UNSPEC_ADDRESS_FIRST))

/* The maximum distance between the top of the stack frame and the
   value $sp has when we save & restore registers.

   Use a maximum gap of 0x100 in the mips16 case.  We can then use
   unextended instructions to save and restore registers, and to
   allocate and deallocate the top part of the frame.

   The value in the !mips16 case must be a SMALL_OPERAND and must
   preserve the maximum stack alignment.  */
#define MIPS_MAX_FIRST_STACK_STEP (TARGET_MIPS16 ? 0x100 : 0x7ff0)

/* True if INSN is a mips.md pattern or asm statement.  */
#define USEFUL_INSN_P(INSN)						\
  (INSN_P (INSN)							\
   && GET_CODE (PATTERN (INSN)) != USE					\
   && GET_CODE (PATTERN (INSN)) != CLOBBER				\
   && GET_CODE (PATTERN (INSN)) != ADDR_VEC				\
   && GET_CODE (PATTERN (INSN)) != ADDR_DIFF_VEC)

/* If INSN is a delayed branch sequence, return the first instruction
   in the sequence, otherwise return INSN itself.  */
#define SEQ_BEGIN(INSN)							\
  (INSN_P (INSN) && GET_CODE (PATTERN (INSN)) == SEQUENCE		\
   ? XVECEXP (PATTERN (INSN), 0, 0)					\
   : (INSN))

/* Likewise for the last instruction in a delayed branch sequence.  */
#define SEQ_END(INSN)							\
  (INSN_P (INSN) && GET_CODE (PATTERN (INSN)) == SEQUENCE		\
   ? XVECEXP (PATTERN (INSN), 0, XVECLEN (PATTERN (INSN), 0) - 1)	\
   : (INSN))

/* Execute the following loop body with SUBINSN set to each instruction
   between SEQ_BEGIN (INSN) and SEQ_END (INSN) inclusive.  */
#define FOR_EACH_SUBINSN(SUBINSN, INSN)					\
  for ((SUBINSN) = SEQ_BEGIN (INSN);					\
       (SUBINSN) != NEXT_INSN (SEQ_END (INSN));				\
       (SUBINSN) = NEXT_INSN (SUBINSN))

/* Classifies an address.

   ADDRESS_REG
       A natural register + offset address.  The register satisfies
       mips_valid_base_register_p and the offset is a const_arith_operand.

   ADDRESS_LO_SUM
       A LO_SUM rtx.  The first operand is a valid base register and
       the second operand is a symbolic address.

   ADDRESS_CONST_INT
       A signed 16-bit constant address.

   ADDRESS_SYMBOLIC:
       A constant symbolic address (equivalent to CONSTANT_SYMBOLIC).  */
enum mips_address_type {
  ADDRESS_REG,
  ADDRESS_LO_SUM,
  ADDRESS_CONST_INT,
  ADDRESS_SYMBOLIC
};

/* Classifies the prototype of a builtin function.  */
enum mips_function_type
{
  MIPS_V2SF_FTYPE_V2SF,
  MIPS_V2SF_FTYPE_V2SF_V2SF,
  MIPS_V2SF_FTYPE_V2SF_V2SF_INT,
  MIPS_V2SF_FTYPE_V2SF_V2SF_V2SF_V2SF,
  MIPS_V2SF_FTYPE_SF_SF,
  MIPS_INT_FTYPE_V2SF_V2SF,
  MIPS_INT_FTYPE_V2SF_V2SF_V2SF_V2SF,
  MIPS_INT_FTYPE_SF_SF,
  MIPS_INT_FTYPE_DF_DF,
  MIPS_SF_FTYPE_V2SF,
  MIPS_SF_FTYPE_SF,
  MIPS_SF_FTYPE_SF_SF,
  MIPS_DF_FTYPE_DF,
  MIPS_DF_FTYPE_DF_DF,

  /* For MIPS DSP ASE  */
  MIPS_DI_FTYPE_DI_SI,
  MIPS_DI_FTYPE_DI_SI_SI,
  MIPS_DI_FTYPE_DI_V2HI_V2HI,
  MIPS_DI_FTYPE_DI_V4QI_V4QI,
  MIPS_SI_FTYPE_DI_SI,
  MIPS_SI_FTYPE_PTR_SI,
  MIPS_SI_FTYPE_SI,
  MIPS_SI_FTYPE_SI_SI,
  MIPS_SI_FTYPE_V2HI,
  MIPS_SI_FTYPE_V2HI_V2HI,
  MIPS_SI_FTYPE_V4QI,
  MIPS_SI_FTYPE_V4QI_V4QI,
  MIPS_SI_FTYPE_VOID,
  MIPS_V2HI_FTYPE_SI,
  MIPS_V2HI_FTYPE_SI_SI,
  MIPS_V2HI_FTYPE_V2HI,
  MIPS_V2HI_FTYPE_V2HI_SI,
  MIPS_V2HI_FTYPE_V2HI_V2HI,
  MIPS_V2HI_FTYPE_V4QI,
  MIPS_V2HI_FTYPE_V4QI_V2HI,
  MIPS_V4QI_FTYPE_SI,
  MIPS_V4QI_FTYPE_V2HI_V2HI,
  MIPS_V4QI_FTYPE_V4QI_SI,
  MIPS_V4QI_FTYPE_V4QI_V4QI,
  MIPS_VOID_FTYPE_SI_SI,
  MIPS_VOID_FTYPE_V2HI_V2HI,
  MIPS_VOID_FTYPE_V4QI_V4QI,

  /* The last type.  */
  MIPS_MAX_FTYPE_MAX
};

/* Specifies how a builtin function should be converted into rtl.  */
enum mips_builtin_type
{
  /* The builtin corresponds directly to an .md pattern.  The return
     value is mapped to operand 0 and the arguments are mapped to
     operands 1 and above.  */
  MIPS_BUILTIN_DIRECT,

  /* The builtin corresponds directly to an .md pattern.  There is no return
     value and the arguments are mapped to operands 0 and above.  */
  MIPS_BUILTIN_DIRECT_NO_TARGET,

  /* The builtin corresponds to a comparison instruction followed by
     a mips_cond_move_tf_ps pattern.  The first two arguments are the
     values to compare and the second two arguments are the vector
     operands for the movt.ps or movf.ps instruction (in assembly order).  */
  MIPS_BUILTIN_MOVF,
  MIPS_BUILTIN_MOVT,

  /* The builtin corresponds to a V2SF comparison instruction.  Operand 0
     of this instruction is the result of the comparison, which has mode
     CCV2 or CCV4.  The function arguments are mapped to operands 1 and
     above.  The function's return value is an SImode boolean that is
     true under the following conditions:

     MIPS_BUILTIN_CMP_ANY: one of the registers is true
     MIPS_BUILTIN_CMP_ALL: all of the registers are true
     MIPS_BUILTIN_CMP_LOWER: the first register is true
     MIPS_BUILTIN_CMP_UPPER: the second register is true.  */
  MIPS_BUILTIN_CMP_ANY,
  MIPS_BUILTIN_CMP_ALL,
  MIPS_BUILTIN_CMP_UPPER,
  MIPS_BUILTIN_CMP_LOWER,

  /* As above, but the instruction only sets a single $fcc register.  */
  MIPS_BUILTIN_CMP_SINGLE,

  /* For generating bposge32 branch instructions in MIPS32 DSP ASE.  */
  MIPS_BUILTIN_BPOSGE32
};

/* Invokes MACRO (COND) for each c.cond.fmt condition.  */
#define MIPS_FP_CONDITIONS(MACRO) \
  MACRO (f),	\
  MACRO (un),	\
  MACRO (eq),	\
  MACRO (ueq),	\
  MACRO (olt),	\
  MACRO (ult),	\
  MACRO (ole),	\
  MACRO (ule),	\
  MACRO (sf),	\
  MACRO (ngle),	\
  MACRO (seq),	\
  MACRO (ngl),	\
  MACRO (lt),	\
  MACRO (nge),	\
  MACRO (le),	\
  MACRO (ngt)

/* Enumerates the codes above as MIPS_FP_COND_<X>.  */
#define DECLARE_MIPS_COND(X) MIPS_FP_COND_ ## X
enum mips_fp_condition {
  MIPS_FP_CONDITIONS (DECLARE_MIPS_COND)
};

/* Index X provides the string representation of MIPS_FP_COND_<X>.  */
#define STRINGIFY(X) #X
static const char *const mips_fp_conditions[] = {
  MIPS_FP_CONDITIONS (STRINGIFY)
};

/* A function to save or store a register.  The first argument is the
   register and the second is the stack slot.  */
typedef void (*mips_save_restore_fn) (rtx, rtx);

struct mips16_constant;
struct mips_arg_info;
struct mips_address_info;
struct mips_integer_op;
struct mips_sim;

static enum mips_symbol_type mips_classify_symbol (rtx);
static void mips_split_const (rtx, rtx *, HOST_WIDE_INT *);
static bool mips_offset_within_object_p (rtx, HOST_WIDE_INT);
static bool mips_valid_base_register_p (rtx, enum machine_mode, int);
static bool mips_symbolic_address_p (enum mips_symbol_type, enum machine_mode);
static bool mips_classify_address (struct mips_address_info *, rtx,
				   enum machine_mode, int);
static bool mips_cannot_force_const_mem (rtx);
static bool mips_use_blocks_for_constant_p (enum machine_mode, rtx);
static int mips_symbol_insns (enum mips_symbol_type);
static bool mips16_unextended_reference_p (enum machine_mode mode, rtx, rtx);
static rtx mips_force_temporary (rtx, rtx);
static rtx mips_unspec_offset_high (rtx, rtx, rtx, enum mips_symbol_type);
static rtx mips_add_offset (rtx, rtx, HOST_WIDE_INT);
static unsigned int mips_build_shift (struct mips_integer_op *, HOST_WIDE_INT);
static unsigned int mips_build_lower (struct mips_integer_op *,
				      unsigned HOST_WIDE_INT);
static unsigned int mips_build_integer (struct mips_integer_op *,
					unsigned HOST_WIDE_INT);
static void mips_legitimize_const_move (enum machine_mode, rtx, rtx);
static int m16_check_op (rtx, int, int, int);
static bool mips_rtx_costs (rtx, int, int, int *);
static int mips_address_cost (rtx);
static void mips_emit_compare (enum rtx_code *, rtx *, rtx *, bool);
static void mips_load_call_address (rtx, rtx, int);
static bool mips_function_ok_for_sibcall (tree, tree);
static void mips_block_move_straight (rtx, rtx, HOST_WIDE_INT);
static void mips_adjust_block_mem (rtx, HOST_WIDE_INT, rtx *, rtx *);
static void mips_block_move_loop (rtx, rtx, HOST_WIDE_INT);
static void mips_arg_info (const CUMULATIVE_ARGS *, enum machine_mode,
			   tree, int, struct mips_arg_info *);
static bool mips_get_unaligned_mem (rtx *, unsigned int, int, rtx *, rtx *);
static void mips_set_architecture (const struct mips_cpu_info *);
static void mips_set_tune (const struct mips_cpu_info *);
static bool mips_handle_option (size_t, const char *, int);
static struct machine_function *mips_init_machine_status (void);
static void print_operand_reloc (FILE *, rtx, const char **);
#if TARGET_IRIX
static void irix_output_external_libcall (rtx);
#endif
static void mips_file_start (void);
static void mips_file_end (void);
static bool mips_rewrite_small_data_p (rtx);
static int mips_small_data_pattern_1 (rtx *, void *);
static int mips_rewrite_small_data_1 (rtx *, void *);
static bool mips_function_has_gp_insn (void);
static unsigned int mips_global_pointer	(void);
static bool mips_save_reg_p (unsigned int);
static void mips_save_restore_reg (enum machine_mode, int, HOST_WIDE_INT,
				   mips_save_restore_fn);
static void mips_for_each_saved_reg (HOST_WIDE_INT, mips_save_restore_fn);
static void mips_output_cplocal (void);
static void mips_emit_loadgp (void);
static void mips_output_function_prologue (FILE *, HOST_WIDE_INT);
static void mips_set_frame_expr (rtx);
static rtx mips_frame_set (rtx, rtx);
static void mips_save_reg (rtx, rtx);
static void mips_output_function_epilogue (FILE *, HOST_WIDE_INT);
static void mips_restore_reg (rtx, rtx);
static void mips_output_mi_thunk (FILE *, tree, HOST_WIDE_INT,
				  HOST_WIDE_INT, tree);
static int symbolic_expression_p (rtx);
static section *mips_select_rtx_section (enum machine_mode, rtx,
					 unsigned HOST_WIDE_INT);
static section *mips_function_rodata_section (tree);
static bool mips_in_small_data_p (tree);
static bool mips_use_anchors_for_symbol_p (rtx);
static int mips_fpr_return_fields (tree, tree *);
static bool mips_return_in_msb (tree);
static rtx mips_return_fpr_pair (enum machine_mode mode,
				 enum machine_mode mode1, HOST_WIDE_INT,
				 enum machine_mode mode2, HOST_WIDE_INT);
static rtx mips16_gp_pseudo_reg (void);
static void mips16_fp_args (FILE *, int, int);
static void build_mips16_function_stub (FILE *);
static rtx dump_constants_1 (enum machine_mode, rtx, rtx);
static void dump_constants (struct mips16_constant *, rtx);
static int mips16_insn_length (rtx);
static int mips16_rewrite_pool_refs (rtx *, void *);
static void mips16_lay_out_constants (void);
static void mips_sim_reset (struct mips_sim *);
static void mips_sim_init (struct mips_sim *, state_t);
static void mips_sim_next_cycle (struct mips_sim *);
static void mips_sim_wait_reg (struct mips_sim *, rtx, rtx);
static int mips_sim_wait_regs_2 (rtx *, void *);
static void mips_sim_wait_regs_1 (rtx *, void *);
static void mips_sim_wait_regs (struct mips_sim *, rtx);
static void mips_sim_wait_units (struct mips_sim *, rtx);
static void mips_sim_wait_insn (struct mips_sim *, rtx);
static void mips_sim_record_set (rtx, rtx, void *);
static void mips_sim_issue_insn (struct mips_sim *, rtx);
static void mips_sim_issue_nop (struct mips_sim *);
static void mips_sim_finish_insn (struct mips_sim *, rtx);
static void vr4130_avoid_branch_rt_conflict (rtx);
static void vr4130_align_insns (void);
static void mips_avoid_hazard (rtx, rtx, int *, rtx *, rtx);
static void mips_avoid_hazards (void);
static void mips_reorg (void);
static bool mips_strict_matching_cpu_name_p (const char *, const char *);
static bool mips_matching_cpu_name_p (const char *, const char *);
static const struct mips_cpu_info *mips_parse_cpu (const char *);
static const struct mips_cpu_info *mips_cpu_info_from_isa (int);
static bool mips_return_in_memory (tree, tree);
static bool mips_strict_argument_naming (CUMULATIVE_ARGS *);
static void mips_macc_chains_record (rtx);
static void mips_macc_chains_reorder (rtx *, int);
static void vr4130_true_reg_dependence_p_1 (rtx, rtx, void *);
static bool vr4130_true_reg_dependence_p (rtx);
static bool vr4130_swap_insns_p (rtx, rtx);
static void vr4130_reorder (rtx *, int);
static void mips_promote_ready (rtx *, int, int);
static int mips_sched_reorder (FILE *, int, rtx *, int *, int);
static int mips_variable_issue (FILE *, int, rtx, int);
static int mips_adjust_cost (rtx, rtx, rtx, int);
static int mips_issue_rate (void);
static int mips_multipass_dfa_lookahead (void);
static void mips_init_libfuncs (void);
static void mips_setup_incoming_varargs (CUMULATIVE_ARGS *, enum machine_mode,
					 tree, int *, int);
static tree mips_build_builtin_va_list (void);
static tree mips_gimplify_va_arg_expr (tree, tree, tree *, tree *);
static bool mips_pass_by_reference (CUMULATIVE_ARGS *, enum machine_mode mode,
				    tree, bool);
static bool mips_callee_copies (CUMULATIVE_ARGS *, enum machine_mode mode,
				tree, bool);
static int mips_arg_partial_bytes (CUMULATIVE_ARGS *, enum machine_mode mode,
				   tree, bool);
static bool mips_valid_pointer_mode (enum machine_mode);
static bool mips_vector_mode_supported_p (enum machine_mode);
static rtx mips_prepare_builtin_arg (enum insn_code, unsigned int, tree *);
static rtx mips_prepare_builtin_target (enum insn_code, unsigned int, rtx);
static rtx mips_expand_builtin (tree, rtx, rtx, enum machine_mode, int);
static void mips_init_builtins (void);
static rtx mips_expand_builtin_direct (enum insn_code, rtx, tree, bool);
static rtx mips_expand_builtin_movtf (enum mips_builtin_type,
				      enum insn_code, enum mips_fp_condition,
				      rtx, tree);
static rtx mips_expand_builtin_compare (enum mips_builtin_type,
					enum insn_code, enum mips_fp_condition,
					rtx, tree);
static rtx mips_expand_builtin_bposge (enum mips_builtin_type, rtx);
static void mips_encode_section_info (tree, rtx, int);
static void mips_extra_live_on_entry (bitmap);
static int mips_mode_rep_extended (enum machine_mode, enum machine_mode);

/* Structure to be filled in by compute_frame_size with register
   save masks, and offsets for the current function.  */

struct mips_frame_info GTY(())
{
  HOST_WIDE_INT total_size;	/* # bytes that the entire frame takes up */
  HOST_WIDE_INT var_size;	/* # bytes that variables take up */
  HOST_WIDE_INT args_size;	/* # bytes that outgoing arguments take up */
  HOST_WIDE_INT cprestore_size;	/* # bytes that the .cprestore slot takes up */
  HOST_WIDE_INT gp_reg_size;	/* # bytes needed to store gp regs */
  HOST_WIDE_INT fp_reg_size;	/* # bytes needed to store fp regs */
  unsigned int mask;		/* mask of saved gp registers */
  unsigned int fmask;		/* mask of saved fp registers */
  HOST_WIDE_INT gp_save_offset;	/* offset from vfp to store gp registers */
  HOST_WIDE_INT fp_save_offset;	/* offset from vfp to store fp registers */
  HOST_WIDE_INT gp_sp_offset;	/* offset from new sp to store gp registers */
  HOST_WIDE_INT fp_sp_offset;	/* offset from new sp to store fp registers */
  bool initialized;		/* true if frame size already calculated */
  int num_gp;			/* number of gp registers saved */
  int num_fp;			/* number of fp registers saved */
};

struct machine_function GTY(()) {
  /* Pseudo-reg holding the value of $28 in a mips16 function which
     refers to GP relative global variables.  */
  rtx mips16_gp_pseudo_rtx;

  /* The number of extra stack bytes taken up by register varargs.
     This area is allocated by the callee at the very top of the frame.  */
  int varargs_size;

  /* Current frame information, calculated by compute_frame_size.  */
  struct mips_frame_info frame;

  /* The register to use as the global pointer within this function.  */
  unsigned int global_pointer;

  /* True if mips_adjust_insn_length should ignore an instruction's
     hazard attribute.  */
  bool ignore_hazard_length_p;

  /* True if the whole function is suitable for .set noreorder and
     .set nomacro.  */
  bool all_noreorder_p;

  /* True if the function is known to have an instruction that needs $gp.  */
  bool has_gp_insn_p;
};

/* Information about a single argument.  */
struct mips_arg_info
{
  /* True if the argument is passed in a floating-point register, or
     would have been if we hadn't run out of registers.  */
  bool fpr_p;

  /* The number of words passed in registers, rounded up.  */
  unsigned int reg_words;

  /* For EABI, the offset of the first register from GP_ARG_FIRST or
     FP_ARG_FIRST.  For other ABIs, the offset of the first register from
     the start of the ABI's argument structure (see the CUMULATIVE_ARGS
     comment for details).

     The value is MAX_ARGS_IN_REGISTERS if the argument is passed entirely
     on the stack.  */
  unsigned int reg_offset;

  /* The number of words that must be passed on the stack, rounded up.  */
  unsigned int stack_words;

  /* The offset from the start of the stack overflow area of the argument's
     first stack word.  Only meaningful when STACK_WORDS is nonzero.  */
  unsigned int stack_offset;
};


/* Information about an address described by mips_address_type.

   ADDRESS_CONST_INT
       No fields are used.

   ADDRESS_REG
       REG is the base register and OFFSET is the constant offset.

   ADDRESS_LO_SUM
       REG is the register that contains the high part of the address,
       OFFSET is the symbolic address being referenced and SYMBOL_TYPE
       is the type of OFFSET's symbol.

   ADDRESS_SYMBOLIC
       SYMBOL_TYPE is the type of symbol being referenced.  */

struct mips_address_info
{
  enum mips_address_type type;
  rtx reg;
  rtx offset;
  enum mips_symbol_type symbol_type;
};


/* One stage in a constant building sequence.  These sequences have
   the form:

	A = VALUE[0]
	A = A CODE[1] VALUE[1]
	A = A CODE[2] VALUE[2]
	...

   where A is an accumulator, each CODE[i] is a binary rtl operation
   and each VALUE[i] is a constant integer.  */
struct mips_integer_op {
  enum rtx_code code;
  unsigned HOST_WIDE_INT value;
};


/* The largest number of operations needed to load an integer constant.
   The worst accepted case for 64-bit constants is LUI,ORI,SLL,ORI,SLL,ORI.
   When the lowest bit is clear, we can try, but reject a sequence with
   an extra SLL at the end.  */
#define MIPS_MAX_INTEGER_OPS 7


/* Global variables for machine-dependent things.  */

/* Threshold for data being put into the small data/bss area, instead
   of the normal data area.  */
int mips_section_threshold = -1;

/* Count the number of .file directives, so that .loc is up to date.  */
int num_source_filenames = 0;

/* Count the number of sdb related labels are generated (to find block
   start and end boundaries).  */
int sdb_label_count = 0;

/* Next label # for each statement for Silicon Graphics IRIS systems.  */
int sym_lineno = 0;

/* Linked list of all externals that are to be emitted when optimizing
   for the global pointer if they haven't been declared by the end of
   the program with an appropriate .comm or initialization.  */

struct extern_list GTY (())
{
  struct extern_list *next;	/* next external */
  const char *name;		/* name of the external */
  int size;			/* size in bytes */
};

static GTY (()) struct extern_list *extern_head = 0;

/* Name of the file containing the current function.  */
const char *current_function_file = "";

/* Number of nested .set noreorder, noat, nomacro, and volatile requests.  */
int set_noreorder;
int set_noat;
int set_nomacro;
int set_volatile;

/* The next branch instruction is a branch likely, not branch normal.  */
int mips_branch_likely;

/* The operands passed to the last cmpMM expander.  */
rtx cmp_operands[2];

/* The target cpu for code generation.  */
enum processor_type mips_arch;
const struct mips_cpu_info *mips_arch_info;

/* The target cpu for optimization and scheduling.  */
enum processor_type mips_tune;
const struct mips_cpu_info *mips_tune_info;

/* Which instruction set architecture to use.  */
int mips_isa;

/* Which ABI to use.  */
int mips_abi = MIPS_ABI_DEFAULT;

/* Cost information to use.  */
const struct mips_rtx_cost_data *mips_cost;

/* Whether we are generating mips16 hard float code.  In mips16 mode
   we always set TARGET_SOFT_FLOAT; this variable is nonzero if
   -msoft-float was not specified by the user, which means that we
   should arrange to call mips32 hard floating point code.  */
int mips16_hard_float;

/* The architecture selected by -mipsN.  */
static const struct mips_cpu_info *mips_isa_info;

/* If TRUE, we split addresses into their high and low parts in the RTL.  */
int mips_split_addresses;

/* Mode used for saving/restoring general purpose registers.  */
static enum machine_mode gpr_mode;

/* Array giving truth value on whether or not a given hard register
   can support a given mode.  */
char mips_hard_regno_mode_ok[(int)MAX_MACHINE_MODE][FIRST_PSEUDO_REGISTER];

/* List of all MIPS punctuation characters used by print_operand.  */
char mips_print_operand_punct[256];

/* Map GCC register number to debugger register number.  */
int mips_dbx_regno[FIRST_PSEUDO_REGISTER];

/* A copy of the original flag_delayed_branch: see override_options.  */
static int mips_flag_delayed_branch;

static GTY (()) int mips_output_filename_first_time = 1;

/* mips_split_p[X] is true if symbols of type X can be split by
   mips_split_symbol().  */
bool mips_split_p[NUM_SYMBOL_TYPES];

/* mips_lo_relocs[X] is the relocation to use when a symbol of type X
   appears in a LO_SUM.  It can be null if such LO_SUMs aren't valid or
   if they are matched by a special .md file pattern.  */
static const char *mips_lo_relocs[NUM_SYMBOL_TYPES];

/* Likewise for HIGHs.  */
static const char *mips_hi_relocs[NUM_SYMBOL_TYPES];

/* Map hard register number to register class */
const enum reg_class mips_regno_to_class[] =
{
  LEA_REGS,	LEA_REGS,	M16_NA_REGS,	V1_REG,
  M16_REGS,	M16_REGS,	M16_REGS,	M16_REGS,
  LEA_REGS,	LEA_REGS,	LEA_REGS,	LEA_REGS,
  LEA_REGS,	LEA_REGS,	LEA_REGS,	LEA_REGS,
  M16_NA_REGS,	M16_NA_REGS,	LEA_REGS,	LEA_REGS,
  LEA_REGS,	LEA_REGS,	LEA_REGS,	LEA_REGS,
  T_REG,	PIC_FN_ADDR_REG, LEA_REGS,	LEA_REGS,
  LEA_REGS,	LEA_REGS,	LEA_REGS,	LEA_REGS,
  FP_REGS,	FP_REGS,	FP_REGS,	FP_REGS,
  FP_REGS,	FP_REGS,	FP_REGS,	FP_REGS,
  FP_REGS,	FP_REGS,	FP_REGS,	FP_REGS,
  FP_REGS,	FP_REGS,	FP_REGS,	FP_REGS,
  FP_REGS,	FP_REGS,	FP_REGS,	FP_REGS,
  FP_REGS,	FP_REGS,	FP_REGS,	FP_REGS,
  FP_REGS,	FP_REGS,	FP_REGS,	FP_REGS,
  FP_REGS,	FP_REGS,	FP_REGS,	FP_REGS,
  HI_REG,	LO_REG,		NO_REGS,	ST_REGS,
  ST_REGS,	ST_REGS,	ST_REGS,	ST_REGS,
  ST_REGS,	ST_REGS,	ST_REGS,	NO_REGS,
  NO_REGS,	ALL_REGS,	ALL_REGS,	NO_REGS,
  COP0_REGS,	COP0_REGS,	COP0_REGS,	COP0_REGS,
  COP0_REGS,	COP0_REGS,	COP0_REGS,	COP0_REGS,
  COP0_REGS,	COP0_REGS,	COP0_REGS,	COP0_REGS,
  COP0_REGS,	COP0_REGS,	COP0_REGS,	COP0_REGS,
  COP0_REGS,	COP0_REGS,	COP0_REGS,	COP0_REGS,
  COP0_REGS,	COP0_REGS,	COP0_REGS,	COP0_REGS,
  COP0_REGS,	COP0_REGS,	COP0_REGS,	COP0_REGS,
  COP0_REGS,	COP0_REGS,	COP0_REGS,	COP0_REGS,
  COP2_REGS,	COP2_REGS,	COP2_REGS,	COP2_REGS,
  COP2_REGS,	COP2_REGS,	COP2_REGS,	COP2_REGS,
  COP2_REGS,	COP2_REGS,	COP2_REGS,	COP2_REGS,
  COP2_REGS,	COP2_REGS,	COP2_REGS,	COP2_REGS,
  COP2_REGS,	COP2_REGS,	COP2_REGS,	COP2_REGS,
  COP2_REGS,	COP2_REGS,	COP2_REGS,	COP2_REGS,
  COP2_REGS,	COP2_REGS,	COP2_REGS,	COP2_REGS,
  COP2_REGS,	COP2_REGS,	COP2_REGS,	COP2_REGS,
  COP3_REGS,	COP3_REGS,	COP3_REGS,	COP3_REGS,
  COP3_REGS,	COP3_REGS,	COP3_REGS,	COP3_REGS,
  COP3_REGS,	COP3_REGS,	COP3_REGS,	COP3_REGS,
  COP3_REGS,	COP3_REGS,	COP3_REGS,	COP3_REGS,
  COP3_REGS,	COP3_REGS,	COP3_REGS,	COP3_REGS,
  COP3_REGS,	COP3_REGS,	COP3_REGS,	COP3_REGS,
  COP3_REGS,	COP3_REGS,	COP3_REGS,	COP3_REGS,
  COP3_REGS,	COP3_REGS,	COP3_REGS,	COP3_REGS,
  DSP_ACC_REGS,	DSP_ACC_REGS,	DSP_ACC_REGS,	DSP_ACC_REGS,
  DSP_ACC_REGS,	DSP_ACC_REGS,	ALL_REGS,	ALL_REGS,
  ALL_REGS,	ALL_REGS,	ALL_REGS,	ALL_REGS
};

/* Table of machine dependent attributes.  */
const struct attribute_spec mips_attribute_table[] =
{
  { "long_call",   0, 0, false, true,  true,  NULL },
  { NULL,	   0, 0, false, false, false, NULL }
};

/* A table describing all the processors gcc knows about.  Names are
   matched in the order listed.  The first mention of an ISA level is
   taken as the canonical name for that ISA.

   To ease comparison, please keep this table in the same order as
   gas's mips_cpu_info_table[].  */
const struct mips_cpu_info mips_cpu_info_table[] = {
  /* Entries for generic ISAs */
  { "mips1", PROCESSOR_R3000, 1 },
  { "mips2", PROCESSOR_R6000, 2 },
  { "mips3", PROCESSOR_R4000, 3 },
  { "mips4", PROCESSOR_R8000, 4 },
  { "mips32", PROCESSOR_4KC, 32 },
  { "mips32r2", PROCESSOR_M4K, 33 },
  { "mips64", PROCESSOR_5KC, 64 },

  /* MIPS I */
  { "r3000", PROCESSOR_R3000, 1 },
  { "r2000", PROCESSOR_R3000, 1 }, /* = r3000 */
  { "r3900", PROCESSOR_R3900, 1 },

  /* MIPS II */
  { "r6000", PROCESSOR_R6000, 2 },

  /* MIPS III */
  { "r4000", PROCESSOR_R4000, 3 },
  { "vr4100", PROCESSOR_R4100, 3 },
  { "vr4111", PROCESSOR_R4111, 3 },
  { "vr4120", PROCESSOR_R4120, 3 },
  { "vr4130", PROCESSOR_R4130, 3 },
  { "vr4300", PROCESSOR_R4300, 3 },
  { "r4400", PROCESSOR_R4000, 3 }, /* = r4000 */
  { "r4600", PROCESSOR_R4600, 3 },
  { "orion", PROCESSOR_R4600, 3 }, /* = r4600 */
  { "r4650", PROCESSOR_R4650, 3 },

  /* MIPS IV */
  { "r8000", PROCESSOR_R8000, 4 },
  { "vr5000", PROCESSOR_R5000, 4 },
  { "vr5400", PROCESSOR_R5400, 4 },
  { "vr5500", PROCESSOR_R5500, 4 },
  { "rm7000", PROCESSOR_R7000, 4 },
  { "rm9000", PROCESSOR_R9000, 4 },

  /* MIPS32 */
  { "4kc", PROCESSOR_4KC, 32 },
  { "4km", PROCESSOR_4KC, 32 }, /* = 4kc */
  { "4kp", PROCESSOR_4KP, 32 },

  /* MIPS32 Release 2 */
  { "m4k", PROCESSOR_M4K, 33 },
  { "24k", PROCESSOR_24K, 33 },
  { "24kc", PROCESSOR_24K, 33 },  /* 24K  no FPU */
  { "24kf", PROCESSOR_24K, 33 },  /* 24K 1:2 FPU */
  { "24kx", PROCESSOR_24KX, 33 }, /* 24K 1:1 FPU */

  /* MIPS64 */
  { "5kc", PROCESSOR_5KC, 64 },
  { "5kf", PROCESSOR_5KF, 64 },
  { "20kc", PROCESSOR_20KC, 64 },
  { "sb1", PROCESSOR_SB1, 64 },
  { "sb1a", PROCESSOR_SB1A, 64 },
  { "sr71000", PROCESSOR_SR71000, 64 },

  /* End marker */
  { 0, 0, 0 }
};

/* Default costs. If these are used for a processor we should look
   up the actual costs.  */
#define DEFAULT_COSTS COSTS_N_INSNS (6),  /* fp_add */       \
                      COSTS_N_INSNS (7),  /* fp_mult_sf */   \
                      COSTS_N_INSNS (8),  /* fp_mult_df */   \
                      COSTS_N_INSNS (23), /* fp_div_sf */    \
                      COSTS_N_INSNS (36), /* fp_div_df */    \
                      COSTS_N_INSNS (10), /* int_mult_si */  \
                      COSTS_N_INSNS (10), /* int_mult_di */  \
                      COSTS_N_INSNS (69), /* int_div_si */   \
                      COSTS_N_INSNS (69), /* int_div_di */   \
                                       2, /* branch_cost */  \
                                       4  /* memory_latency */

/* Need to replace these with the costs of calling the appropriate
   libgcc routine.  */
#define SOFT_FP_COSTS COSTS_N_INSNS (256), /* fp_add */       \
                      COSTS_N_INSNS (256), /* fp_mult_sf */   \
                      COSTS_N_INSNS (256), /* fp_mult_df */   \
                      COSTS_N_INSNS (256), /* fp_div_sf */    \
                      COSTS_N_INSNS (256)  /* fp_div_df */

static struct mips_rtx_cost_data const mips_rtx_cost_data[PROCESSOR_MAX] =
  {
    { /* R3000 */
      COSTS_N_INSNS (2),            /* fp_add */
      COSTS_N_INSNS (4),            /* fp_mult_sf */
      COSTS_N_INSNS (5),            /* fp_mult_df */
      COSTS_N_INSNS (12),           /* fp_div_sf */
      COSTS_N_INSNS (19),           /* fp_div_df */
      COSTS_N_INSNS (12),           /* int_mult_si */
      COSTS_N_INSNS (12),           /* int_mult_di */
      COSTS_N_INSNS (35),           /* int_div_si */
      COSTS_N_INSNS (35),           /* int_div_di */
                       1,           /* branch_cost */
                       4            /* memory_latency */

    },
    { /* 4KC */
      SOFT_FP_COSTS,
      COSTS_N_INSNS (6),            /* int_mult_si */
      COSTS_N_INSNS (6),            /* int_mult_di */
      COSTS_N_INSNS (36),           /* int_div_si */
      COSTS_N_INSNS (36),           /* int_div_di */
                       1,           /* branch_cost */
                       4            /* memory_latency */
    },
    { /* 4KP */
      SOFT_FP_COSTS,
      COSTS_N_INSNS (36),           /* int_mult_si */
      COSTS_N_INSNS (36),           /* int_mult_di */
      COSTS_N_INSNS (37),           /* int_div_si */
      COSTS_N_INSNS (37),           /* int_div_di */
                       1,           /* branch_cost */
                       4            /* memory_latency */
    },
    { /* 5KC */
      SOFT_FP_COSTS,
      COSTS_N_INSNS (4),            /* int_mult_si */
      COSTS_N_INSNS (11),           /* int_mult_di */
      COSTS_N_INSNS (36),           /* int_div_si */
      COSTS_N_INSNS (68),           /* int_div_di */
                       1,           /* branch_cost */
                       4            /* memory_latency */
    },
    { /* 5KF */
      COSTS_N_INSNS (4),            /* fp_add */
      COSTS_N_INSNS (4),            /* fp_mult_sf */
      COSTS_N_INSNS (5),            /* fp_mult_df */
      COSTS_N_INSNS (17),           /* fp_div_sf */
      COSTS_N_INSNS (32),           /* fp_div_df */
      COSTS_N_INSNS (4),            /* int_mult_si */
      COSTS_N_INSNS (11),           /* int_mult_di */
      COSTS_N_INSNS (36),           /* int_div_si */
      COSTS_N_INSNS (68),           /* int_div_di */
                       1,           /* branch_cost */
                       4            /* memory_latency */
    },
    { /* 20KC */
      DEFAULT_COSTS
    },
    { /* 24k */
      COSTS_N_INSNS (8),            /* fp_add */
      COSTS_N_INSNS (8),            /* fp_mult_sf */
      COSTS_N_INSNS (10),           /* fp_mult_df */
      COSTS_N_INSNS (34),           /* fp_div_sf */
      COSTS_N_INSNS (64),           /* fp_div_df */
      COSTS_N_INSNS (5),            /* int_mult_si */
      COSTS_N_INSNS (5),            /* int_mult_di */
      COSTS_N_INSNS (41),           /* int_div_si */
      COSTS_N_INSNS (41),           /* int_div_di */
                       1,           /* branch_cost */
                       4            /* memory_latency */
    },
    { /* 24kx */
      COSTS_N_INSNS (4),            /* fp_add */
      COSTS_N_INSNS (4),            /* fp_mult_sf */
      COSTS_N_INSNS (5),            /* fp_mult_df */
      COSTS_N_INSNS (17),           /* fp_div_sf */
      COSTS_N_INSNS (32),           /* fp_div_df */
      COSTS_N_INSNS (5),            /* int_mult_si */
      COSTS_N_INSNS (5),            /* int_mult_di */
      COSTS_N_INSNS (41),           /* int_div_si */
      COSTS_N_INSNS (41),           /* int_div_di */
                       1,           /* branch_cost */
                       4            /* memory_latency */
    },
    { /* M4k */
      DEFAULT_COSTS
    },
    { /* R3900 */
      COSTS_N_INSNS (2),            /* fp_add */
      COSTS_N_INSNS (4),            /* fp_mult_sf */
      COSTS_N_INSNS (5),            /* fp_mult_df */
      COSTS_N_INSNS (12),           /* fp_div_sf */
      COSTS_N_INSNS (19),           /* fp_div_df */
      COSTS_N_INSNS (2),            /* int_mult_si */
      COSTS_N_INSNS (2),            /* int_mult_di */
      COSTS_N_INSNS (35),           /* int_div_si */
      COSTS_N_INSNS (35),           /* int_div_di */
                       1,           /* branch_cost */
                       4            /* memory_latency */
    },
    { /* R6000 */
      COSTS_N_INSNS (3),            /* fp_add */
      COSTS_N_INSNS (5),            /* fp_mult_sf */
      COSTS_N_INSNS (6),            /* fp_mult_df */
      COSTS_N_INSNS (15),           /* fp_div_sf */
      COSTS_N_INSNS (16),           /* fp_div_df */
      COSTS_N_INSNS (17),           /* int_mult_si */
      COSTS_N_INSNS (17),           /* int_mult_di */
      COSTS_N_INSNS (38),           /* int_div_si */
      COSTS_N_INSNS (38),           /* int_div_di */
                       2,           /* branch_cost */
                       6            /* memory_latency */
    },
    { /* R4000 */
       COSTS_N_INSNS (6),           /* fp_add */
       COSTS_N_INSNS (7),           /* fp_mult_sf */
       COSTS_N_INSNS (8),           /* fp_mult_df */
       COSTS_N_INSNS (23),          /* fp_div_sf */
       COSTS_N_INSNS (36),          /* fp_div_df */
       COSTS_N_INSNS (10),          /* int_mult_si */
       COSTS_N_INSNS (10),          /* int_mult_di */
       COSTS_N_INSNS (69),          /* int_div_si */
       COSTS_N_INSNS (69),          /* int_div_di */
                        2,          /* branch_cost */
                        6           /* memory_latency */
    },
    { /* R4100 */
      DEFAULT_COSTS
    },
    { /* R4111 */
      DEFAULT_COSTS
    },
    { /* R4120 */
      DEFAULT_COSTS
    },
    { /* R4130 */
      /* The only costs that appear to be updated here are
	 integer multiplication.  */
      SOFT_FP_COSTS,
      COSTS_N_INSNS (4),            /* int_mult_si */
      COSTS_N_INSNS (6),            /* int_mult_di */
      COSTS_N_INSNS (69),           /* int_div_si */
      COSTS_N_INSNS (69),           /* int_div_di */
                       1,           /* branch_cost */
                       4            /* memory_latency */
    },
    { /* R4300 */
      DEFAULT_COSTS
    },
    { /* R4600 */
      DEFAULT_COSTS
    },
    { /* R4650 */
      DEFAULT_COSTS
    },
    { /* R5000 */
      COSTS_N_INSNS (6),            /* fp_add */
      COSTS_N_INSNS (4),            /* fp_mult_sf */
      COSTS_N_INSNS (5),            /* fp_mult_df */
      COSTS_N_INSNS (23),           /* fp_div_sf */
      COSTS_N_INSNS (36),           /* fp_div_df */
      COSTS_N_INSNS (5),            /* int_mult_si */
      COSTS_N_INSNS (5),            /* int_mult_di */
      COSTS_N_INSNS (36),           /* int_div_si */
      COSTS_N_INSNS (36),           /* int_div_di */
                       1,           /* branch_cost */
                       4            /* memory_latency */
    },
    { /* R5400 */
      COSTS_N_INSNS (6),            /* fp_add */
      COSTS_N_INSNS (5),            /* fp_mult_sf */
      COSTS_N_INSNS (6),            /* fp_mult_df */
      COSTS_N_INSNS (30),           /* fp_div_sf */
      COSTS_N_INSNS (59),           /* fp_div_df */
      COSTS_N_INSNS (3),            /* int_mult_si */
      COSTS_N_INSNS (4),            /* int_mult_di */
      COSTS_N_INSNS (42),           /* int_div_si */
      COSTS_N_INSNS (74),           /* int_div_di */
                       1,           /* branch_cost */
                       4            /* memory_latency */
    },
    { /* R5500 */
      COSTS_N_INSNS (6),            /* fp_add */
      COSTS_N_INSNS (5),            /* fp_mult_sf */
      COSTS_N_INSNS (6),            /* fp_mult_df */
      COSTS_N_INSNS (30),           /* fp_div_sf */
      COSTS_N_INSNS (59),           /* fp_div_df */
      COSTS_N_INSNS (5),            /* int_mult_si */
      COSTS_N_INSNS (9),            /* int_mult_di */
      COSTS_N_INSNS (42),           /* int_div_si */
      COSTS_N_INSNS (74),           /* int_div_di */
                       1,           /* branch_cost */
                       4            /* memory_latency */
    },
    { /* R7000 */
      /* The only costs that are changed here are
	 integer multiplication.  */
      COSTS_N_INSNS (6),            /* fp_add */
      COSTS_N_INSNS (7),            /* fp_mult_sf */
      COSTS_N_INSNS (8),            /* fp_mult_df */
      COSTS_N_INSNS (23),           /* fp_div_sf */
      COSTS_N_INSNS (36),           /* fp_div_df */
      COSTS_N_INSNS (5),            /* int_mult_si */
      COSTS_N_INSNS (9),            /* int_mult_di */
      COSTS_N_INSNS (69),           /* int_div_si */
      COSTS_N_INSNS (69),           /* int_div_di */
                       1,           /* branch_cost */
                       4            /* memory_latency */
    },
    { /* R8000 */
      DEFAULT_COSTS
    },
    { /* R9000 */
      /* The only costs that are changed here are
	 integer multiplication.  */
      COSTS_N_INSNS (6),            /* fp_add */
      COSTS_N_INSNS (7),            /* fp_mult_sf */
      COSTS_N_INSNS (8),            /* fp_mult_df */
      COSTS_N_INSNS (23),           /* fp_div_sf */
      COSTS_N_INSNS (36),           /* fp_div_df */
      COSTS_N_INSNS (3),            /* int_mult_si */
      COSTS_N_INSNS (8),            /* int_mult_di */
      COSTS_N_INSNS (69),           /* int_div_si */
      COSTS_N_INSNS (69),           /* int_div_di */
                       1,           /* branch_cost */
                       4            /* memory_latency */
    },
    { /* SB1 */
      /* These costs are the same as the SB-1A below.  */
      COSTS_N_INSNS (4),            /* fp_add */
      COSTS_N_INSNS (4),            /* fp_mult_sf */
      COSTS_N_INSNS (4),            /* fp_mult_df */
      COSTS_N_INSNS (24),           /* fp_div_sf */
      COSTS_N_INSNS (32),           /* fp_div_df */
      COSTS_N_INSNS (3),            /* int_mult_si */
      COSTS_N_INSNS (4),            /* int_mult_di */
      COSTS_N_INSNS (36),           /* int_div_si */
      COSTS_N_INSNS (68),           /* int_div_di */
                       1,           /* branch_cost */
                       4            /* memory_latency */
    },
    { /* SB1-A */
      /* These costs are the same as the SB-1 above.  */
      COSTS_N_INSNS (4),            /* fp_add */
      COSTS_N_INSNS (4),            /* fp_mult_sf */
      COSTS_N_INSNS (4),            /* fp_mult_df */
      COSTS_N_INSNS (24),           /* fp_div_sf */
      COSTS_N_INSNS (32),           /* fp_div_df */
      COSTS_N_INSNS (3),            /* int_mult_si */
      COSTS_N_INSNS (4),            /* int_mult_di */
      COSTS_N_INSNS (36),           /* int_div_si */
      COSTS_N_INSNS (68),           /* int_div_di */
                       1,           /* branch_cost */
                       4            /* memory_latency */
    },
    { /* SR71000 */
      DEFAULT_COSTS
    },
  };


/* Nonzero if -march should decide the default value of MASK_SOFT_FLOAT.  */
#ifndef MIPS_MARCH_CONTROLS_SOFT_FLOAT
#define MIPS_MARCH_CONTROLS_SOFT_FLOAT 0
#endif

/* Initialize the GCC target structure.  */
#undef TARGET_ASM_ALIGNED_HI_OP
#define TARGET_ASM_ALIGNED_HI_OP "\t.half\t"
#undef TARGET_ASM_ALIGNED_SI_OP
#define TARGET_ASM_ALIGNED_SI_OP "\t.word\t"
#undef TARGET_ASM_ALIGNED_DI_OP
#define TARGET_ASM_ALIGNED_DI_OP "\t.dword\t"

#undef TARGET_ASM_FUNCTION_PROLOGUE
#define TARGET_ASM_FUNCTION_PROLOGUE mips_output_function_prologue
#undef TARGET_ASM_FUNCTION_EPILOGUE
#define TARGET_ASM_FUNCTION_EPILOGUE mips_output_function_epilogue
#undef TARGET_ASM_SELECT_RTX_SECTION
#define TARGET_ASM_SELECT_RTX_SECTION mips_select_rtx_section
#undef TARGET_ASM_FUNCTION_RODATA_SECTION
#define TARGET_ASM_FUNCTION_RODATA_SECTION mips_function_rodata_section

#undef TARGET_SCHED_REORDER
#define TARGET_SCHED_REORDER mips_sched_reorder
#undef TARGET_SCHED_VARIABLE_ISSUE
#define TARGET_SCHED_VARIABLE_ISSUE mips_variable_issue
#undef TARGET_SCHED_ADJUST_COST
#define TARGET_SCHED_ADJUST_COST mips_adjust_cost
#undef TARGET_SCHED_ISSUE_RATE
#define TARGET_SCHED_ISSUE_RATE mips_issue_rate
#undef TARGET_SCHED_FIRST_CYCLE_MULTIPASS_DFA_LOOKAHEAD
#define TARGET_SCHED_FIRST_CYCLE_MULTIPASS_DFA_LOOKAHEAD \
  mips_multipass_dfa_lookahead

#undef TARGET_DEFAULT_TARGET_FLAGS
#define TARGET_DEFAULT_TARGET_FLAGS		\
  (TARGET_DEFAULT				\
   | TARGET_CPU_DEFAULT				\
   | TARGET_ENDIAN_DEFAULT			\
   | TARGET_FP_EXCEPTIONS_DEFAULT		\
   | MASK_CHECK_ZERO_DIV			\
   | MASK_FUSED_MADD)
#undef TARGET_HANDLE_OPTION
#define TARGET_HANDLE_OPTION mips_handle_option

#undef TARGET_FUNCTION_OK_FOR_SIBCALL
#define TARGET_FUNCTION_OK_FOR_SIBCALL mips_function_ok_for_sibcall

#undef TARGET_VALID_POINTER_MODE
#define TARGET_VALID_POINTER_MODE mips_valid_pointer_mode
#undef TARGET_RTX_COSTS
#define TARGET_RTX_COSTS mips_rtx_costs
#undef TARGET_ADDRESS_COST
#define TARGET_ADDRESS_COST mips_address_cost

#undef TARGET_IN_SMALL_DATA_P
#define TARGET_IN_SMALL_DATA_P mips_in_small_data_p

#undef TARGET_MACHINE_DEPENDENT_REORG
#define TARGET_MACHINE_DEPENDENT_REORG mips_reorg

#undef TARGET_ASM_FILE_START
#undef TARGET_ASM_FILE_END
#define TARGET_ASM_FILE_START mips_file_start
#define TARGET_ASM_FILE_END mips_file_end
#undef TARGET_ASM_FILE_START_FILE_DIRECTIVE
#define TARGET_ASM_FILE_START_FILE_DIRECTIVE true

#undef TARGET_INIT_LIBFUNCS
#define TARGET_INIT_LIBFUNCS mips_init_libfuncs

#undef TARGET_BUILD_BUILTIN_VA_LIST
#define TARGET_BUILD_BUILTIN_VA_LIST mips_build_builtin_va_list
#undef TARGET_GIMPLIFY_VA_ARG_EXPR
#define TARGET_GIMPLIFY_VA_ARG_EXPR mips_gimplify_va_arg_expr

#undef TARGET_PROMOTE_FUNCTION_ARGS
#define TARGET_PROMOTE_FUNCTION_ARGS hook_bool_tree_true
#undef TARGET_PROMOTE_FUNCTION_RETURN
#define TARGET_PROMOTE_FUNCTION_RETURN hook_bool_tree_true
#undef TARGET_PROMOTE_PROTOTYPES
#define TARGET_PROMOTE_PROTOTYPES hook_bool_tree_true

#undef TARGET_RETURN_IN_MEMORY
#define TARGET_RETURN_IN_MEMORY mips_return_in_memory
#undef TARGET_RETURN_IN_MSB
#define TARGET_RETURN_IN_MSB mips_return_in_msb

#undef TARGET_ASM_OUTPUT_MI_THUNK
#define TARGET_ASM_OUTPUT_MI_THUNK mips_output_mi_thunk
#undef TARGET_ASM_CAN_OUTPUT_MI_THUNK
#define TARGET_ASM_CAN_OUTPUT_MI_THUNK hook_bool_tree_hwi_hwi_tree_true

#undef TARGET_SETUP_INCOMING_VARARGS
#define TARGET_SETUP_INCOMING_VARARGS mips_setup_incoming_varargs
#undef TARGET_STRICT_ARGUMENT_NAMING
#define TARGET_STRICT_ARGUMENT_NAMING mips_strict_argument_naming
#undef TARGET_MUST_PASS_IN_STACK
#define TARGET_MUST_PASS_IN_STACK must_pass_in_stack_var_size
#undef TARGET_PASS_BY_REFERENCE
#define TARGET_PASS_BY_REFERENCE mips_pass_by_reference
#undef TARGET_CALLEE_COPIES
#define TARGET_CALLEE_COPIES mips_callee_copies
#undef TARGET_ARG_PARTIAL_BYTES
#define TARGET_ARG_PARTIAL_BYTES mips_arg_partial_bytes

#undef TARGET_MODE_REP_EXTENDED
#define TARGET_MODE_REP_EXTENDED mips_mode_rep_extended

#undef TARGET_VECTOR_MODE_SUPPORTED_P
#define TARGET_VECTOR_MODE_SUPPORTED_P mips_vector_mode_supported_p

#undef TARGET_INIT_BUILTINS
#define TARGET_INIT_BUILTINS mips_init_builtins
#undef TARGET_EXPAND_BUILTIN
#define TARGET_EXPAND_BUILTIN mips_expand_builtin

#undef TARGET_HAVE_TLS
#define TARGET_HAVE_TLS HAVE_AS_TLS

#undef TARGET_CANNOT_FORCE_CONST_MEM
#define TARGET_CANNOT_FORCE_CONST_MEM mips_cannot_force_const_mem

#undef TARGET_ENCODE_SECTION_INFO
#define TARGET_ENCODE_SECTION_INFO mips_encode_section_info

#undef TARGET_ATTRIBUTE_TABLE
#define TARGET_ATTRIBUTE_TABLE mips_attribute_table

#undef TARGET_EXTRA_LIVE_ON_ENTRY
#define TARGET_EXTRA_LIVE_ON_ENTRY mips_extra_live_on_entry

#undef TARGET_MIN_ANCHOR_OFFSET
#define TARGET_MIN_ANCHOR_OFFSET -32768
#undef TARGET_MAX_ANCHOR_OFFSET
#define TARGET_MAX_ANCHOR_OFFSET 32767
#undef TARGET_USE_BLOCKS_FOR_CONSTANT_P
#define TARGET_USE_BLOCKS_FOR_CONSTANT_P mips_use_blocks_for_constant_p
#undef TARGET_USE_ANCHORS_FOR_SYMBOL_P
#define TARGET_USE_ANCHORS_FOR_SYMBOL_P mips_use_anchors_for_symbol_p

struct gcc_target targetm = TARGET_INITIALIZER;

/* Classify symbol X, which must be a SYMBOL_REF or a LABEL_REF.  */

static enum mips_symbol_type
mips_classify_symbol (rtx x)
{
  if (GET_CODE (x) == LABEL_REF)
    {
      if (TARGET_MIPS16)
	return SYMBOL_CONSTANT_POOL;
      if (TARGET_ABICALLS && !TARGET_ABSOLUTE_ABICALLS)
	return SYMBOL_GOT_LOCAL;
      return SYMBOL_GENERAL;
    }

  gcc_assert (GET_CODE (x) == SYMBOL_REF);

  if (SYMBOL_REF_TLS_MODEL (x))
    return SYMBOL_TLS;

  if (CONSTANT_POOL_ADDRESS_P (x))
    {
      if (TARGET_MIPS16)
	return SYMBOL_CONSTANT_POOL;

      if (GET_MODE_SIZE (get_pool_mode (x)) <= mips_section_threshold)
	return SYMBOL_SMALL_DATA;
    }

  /* Do not use small-data accesses for weak symbols; they may end up
     being zero.  */
  if (SYMBOL_REF_SMALL_P (x)
      && !SYMBOL_REF_WEAK (x))
    return SYMBOL_SMALL_DATA;

  if (TARGET_ABICALLS)
    {
      if (SYMBOL_REF_DECL (x) == 0)
	{
	  if (!SYMBOL_REF_LOCAL_P (x))
	    return SYMBOL_GOT_GLOBAL;
	}
      else
	{
	  /* Don't use GOT accesses for locally-binding symbols if
	     TARGET_ABSOLUTE_ABICALLS.  Otherwise, there are three
	     cases to consider:

		- o32 PIC (either with or without explicit relocs)
		- n32/n64 PIC without explicit relocs
		- n32/n64 PIC with explicit relocs

	     In the first case, both local and global accesses will use an
	     R_MIPS_GOT16 relocation.  We must correctly predict which of
	     the two semantics (local or global) the assembler and linker
	     will apply.  The choice doesn't depend on the symbol's
	     visibility, so we deliberately ignore decl_visibility and
	     binds_local_p here.

	     In the second case, the assembler will not use R_MIPS_GOT16
	     relocations, but it chooses between local and global accesses
	     in the same way as for o32 PIC.

	     In the third case we have more freedom since both forms of
	     access will work for any kind of symbol.  However, there seems
	     little point in doing things differently.  */
	  if (DECL_P (SYMBOL_REF_DECL (x))
	      && TREE_PUBLIC (SYMBOL_REF_DECL (x))
	      && !(TARGET_ABSOLUTE_ABICALLS
		   && targetm.binds_local_p (SYMBOL_REF_DECL (x))))
	    return SYMBOL_GOT_GLOBAL;
	}

      if (!TARGET_ABSOLUTE_ABICALLS)
	return SYMBOL_GOT_LOCAL;
    }

  return SYMBOL_GENERAL;
}


/* Split X into a base and a constant offset, storing them in *BASE
   and *OFFSET respectively.  */

static void
mips_split_const (rtx x, rtx *base, HOST_WIDE_INT *offset)
{
  *offset = 0;

  if (GET_CODE (x) == CONST)
    {
      x = XEXP (x, 0);
      if (GET_CODE (x) == PLUS && GET_CODE (XEXP (x, 1)) == CONST_INT)
	{
	  *offset += INTVAL (XEXP (x, 1));
	  x = XEXP (x, 0);
	}
    }
  *base = x;
}


/* Return true if SYMBOL is a SYMBOL_REF and OFFSET + SYMBOL points
   to the same object as SYMBOL, or to the same object_block.  */

static bool
mips_offset_within_object_p (rtx symbol, HOST_WIDE_INT offset)
{
  if (GET_CODE (symbol) != SYMBOL_REF)
    return false;

  if (CONSTANT_POOL_ADDRESS_P (symbol)
      && offset >= 0
      && offset < (int) GET_MODE_SIZE (get_pool_mode (symbol)))
    return true;

  if (SYMBOL_REF_DECL (symbol) != 0
      && offset >= 0
      && offset < int_size_in_bytes (TREE_TYPE (SYMBOL_REF_DECL (symbol))))
    return true;

  if (SYMBOL_REF_HAS_BLOCK_INFO_P (symbol)
      && SYMBOL_REF_BLOCK (symbol)
      && SYMBOL_REF_BLOCK_OFFSET (symbol) >= 0
      && ((unsigned HOST_WIDE_INT) offset + SYMBOL_REF_BLOCK_OFFSET (symbol)
	  < (unsigned HOST_WIDE_INT) SYMBOL_REF_BLOCK (symbol)->size))
    return true;

  return false;
}


/* Return true if X is a symbolic constant that can be calculated in
   the same way as a bare symbol.  If it is, store the type of the
   symbol in *SYMBOL_TYPE.  */

bool
mips_symbolic_constant_p (rtx x, enum mips_symbol_type *symbol_type)
{
  HOST_WIDE_INT offset;

  mips_split_const (x, &x, &offset);
  if (UNSPEC_ADDRESS_P (x))
    *symbol_type = UNSPEC_ADDRESS_TYPE (x);
  else if (GET_CODE (x) == SYMBOL_REF || GET_CODE (x) == LABEL_REF)
    {
      *symbol_type = mips_classify_symbol (x);
      if (*symbol_type == SYMBOL_TLS)
	return false;
    }
  else
    return false;

  if (offset == 0)
    return true;

  /* Check whether a nonzero offset is valid for the underlying
     relocations.  */
  switch (*symbol_type)
    {
    case SYMBOL_GENERAL:
    case SYMBOL_64_HIGH:
    case SYMBOL_64_MID:
    case SYMBOL_64_LOW:
      /* If the target has 64-bit pointers and the object file only
	 supports 32-bit symbols, the values of those symbols will be
	 sign-extended.  In this case we can't allow an arbitrary offset
	 in case the 32-bit value X + OFFSET has a different sign from X.  */
      if (Pmode == DImode && !ABI_HAS_64BIT_SYMBOLS)
	return mips_offset_within_object_p (x, offset);

      /* In other cases the relocations can handle any offset.  */
      return true;

    case SYMBOL_CONSTANT_POOL:
      /* Allow constant pool references to be converted to LABEL+CONSTANT.
	 In this case, we no longer have access to the underlying constant,
	 but the original symbol-based access was known to be valid.  */
      if (GET_CODE (x) == LABEL_REF)
	return true;

      /* Fall through.  */

    case SYMBOL_SMALL_DATA:
      /* Make sure that the offset refers to something within the
	 underlying object.  This should guarantee that the final
	 PC- or GP-relative offset is within the 16-bit limit.  */
      return mips_offset_within_object_p (x, offset);

    case SYMBOL_GOT_LOCAL:
    case SYMBOL_GOTOFF_PAGE:
      /* The linker should provide enough local GOT entries for a
	 16-bit offset.  Larger offsets may lead to GOT overflow.  */
      return SMALL_OPERAND (offset);

    case SYMBOL_GOT_GLOBAL:
    case SYMBOL_GOTOFF_GLOBAL:
    case SYMBOL_GOTOFF_CALL:
    case SYMBOL_GOTOFF_LOADGP:
    case SYMBOL_TLSGD:
    case SYMBOL_TLSLDM:
    case SYMBOL_DTPREL:
    case SYMBOL_TPREL:
    case SYMBOL_GOTTPREL:
    case SYMBOL_TLS:
      return false;
    }
  gcc_unreachable ();
}


/* This function is used to implement REG_MODE_OK_FOR_BASE_P.  */

int
mips_regno_mode_ok_for_base_p (int regno, enum machine_mode mode, int strict)
{
  if (regno >= FIRST_PSEUDO_REGISTER)
    {
      if (!strict)
	return true;
      regno = reg_renumber[regno];
    }

  /* These fake registers will be eliminated to either the stack or
     hard frame pointer, both of which are usually valid base registers.
     Reload deals with the cases where the eliminated form isn't valid.  */
  if (regno == ARG_POINTER_REGNUM || regno == FRAME_POINTER_REGNUM)
    return true;

  /* In mips16 mode, the stack pointer can only address word and doubleword
     values, nothing smaller.  There are two problems here:

       (a) Instantiating virtual registers can introduce new uses of the
	   stack pointer.  If these virtual registers are valid addresses,
	   the stack pointer should be too.

       (b) Most uses of the stack pointer are not made explicit until
	   FRAME_POINTER_REGNUM and ARG_POINTER_REGNUM have been eliminated.
	   We don't know until that stage whether we'll be eliminating to the
	   stack pointer (which needs the restriction) or the hard frame
	   pointer (which doesn't).

     All in all, it seems more consistent to only enforce this restriction
     during and after reload.  */
  if (TARGET_MIPS16 && regno == STACK_POINTER_REGNUM)
    return !strict || GET_MODE_SIZE (mode) == 4 || GET_MODE_SIZE (mode) == 8;

  return TARGET_MIPS16 ? M16_REG_P (regno) : GP_REG_P (regno);
}


/* Return true if X is a valid base register for the given mode.
   Allow only hard registers if STRICT.  */

static bool
mips_valid_base_register_p (rtx x, enum machine_mode mode, int strict)
{
  if (!strict && GET_CODE (x) == SUBREG)
    x = SUBREG_REG (x);

  return (REG_P (x)
	  && mips_regno_mode_ok_for_base_p (REGNO (x), mode, strict));
}


/* Return true if symbols of type SYMBOL_TYPE can directly address a value
   with mode MODE.  This is used for both symbolic and LO_SUM addresses.  */

static bool
mips_symbolic_address_p (enum mips_symbol_type symbol_type,
			 enum machine_mode mode)
{
  switch (symbol_type)
    {
    case SYMBOL_GENERAL:
      return !TARGET_MIPS16;

    case SYMBOL_SMALL_DATA:
      return true;

    case SYMBOL_CONSTANT_POOL:
      /* PC-relative addressing is only available for lw and ld.  */
      return GET_MODE_SIZE (mode) == 4 || GET_MODE_SIZE (mode) == 8;

    case SYMBOL_GOT_LOCAL:
      return true;

    case SYMBOL_GOT_GLOBAL:
      /* The address will have to be loaded from the GOT first.  */
      return false;

    case SYMBOL_GOTOFF_PAGE:
    case SYMBOL_GOTOFF_GLOBAL:
    case SYMBOL_GOTOFF_CALL:
    case SYMBOL_GOTOFF_LOADGP:
    case SYMBOL_TLS:
    case SYMBOL_TLSGD:
    case SYMBOL_TLSLDM:
    case SYMBOL_DTPREL:
    case SYMBOL_GOTTPREL:
    case SYMBOL_TPREL:
    case SYMBOL_64_HIGH:
    case SYMBOL_64_MID:
    case SYMBOL_64_LOW:
      return true;
    }
  gcc_unreachable ();
}


/* Return true if X is a valid address for machine mode MODE.  If it is,
   fill in INFO appropriately.  STRICT is true if we should only accept
   hard base registers.  */

static bool
mips_classify_address (struct mips_address_info *info, rtx x,
		       enum machine_mode mode, int strict)
{
  switch (GET_CODE (x))
    {
    case REG:
    case SUBREG:
      info->type = ADDRESS_REG;
      info->reg = x;
      info->offset = const0_rtx;
      return mips_valid_base_register_p (info->reg, mode, strict);

    case PLUS:
      info->type = ADDRESS_REG;
      info->reg = XEXP (x, 0);
      info->offset = XEXP (x, 1);
      return (mips_valid_base_register_p (info->reg, mode, strict)
	      && const_arith_operand (info->offset, VOIDmode));

    case LO_SUM:
      info->type = ADDRESS_LO_SUM;
      info->reg = XEXP (x, 0);
      info->offset = XEXP (x, 1);
      return (mips_valid_base_register_p (info->reg, mode, strict)
	      && mips_symbolic_constant_p (info->offset, &info->symbol_type)
	      && mips_symbolic_address_p (info->symbol_type, mode)
	      && mips_lo_relocs[info->symbol_type] != 0);

    case CONST_INT:
      /* Small-integer addresses don't occur very often, but they
	 are legitimate if $0 is a valid base register.  */
      info->type = ADDRESS_CONST_INT;
      return !TARGET_MIPS16 && SMALL_INT (x);

    case CONST:
    case LABEL_REF:
    case SYMBOL_REF:
      info->type = ADDRESS_SYMBOLIC;
      return (mips_symbolic_constant_p (x, &info->symbol_type)
	      && mips_symbolic_address_p (info->symbol_type, mode)
	      && !mips_split_p[info->symbol_type]);

    default:
      return false;
    }
}

/* Return true if X is a thread-local symbol.  */

static bool
mips_tls_operand_p (rtx x)
{
  return GET_CODE (x) == SYMBOL_REF && SYMBOL_REF_TLS_MODEL (x) != 0;
}

/* Return true if X can not be forced into a constant pool.  */

static int
mips_tls_symbol_ref_1 (rtx *x, void *data ATTRIBUTE_UNUSED)
{
  return mips_tls_operand_p (*x);
}

/* Return true if X can not be forced into a constant pool.  */

static bool
mips_cannot_force_const_mem (rtx x)
{
  rtx base;
  HOST_WIDE_INT offset;

  if (!TARGET_MIPS16)
    {
      /* As an optimization, reject constants that mips_legitimize_move
	 can expand inline.

	 Suppose we have a multi-instruction sequence that loads constant C
	 into register R.  If R does not get allocated a hard register, and
	 R is used in an operand that allows both registers and memory
	 references, reload will consider forcing C into memory and using
	 one of the instruction's memory alternatives.  Returning false
	 here will force it to use an input reload instead.  */
      if (GET_CODE (x) == CONST_INT)
	return true;

      mips_split_const (x, &base, &offset);
      if (symbolic_operand (base, VOIDmode) && SMALL_OPERAND (offset))
	return true;
    }

  if (TARGET_HAVE_TLS && for_each_rtx (&x, &mips_tls_symbol_ref_1, 0))
    return true;

  return false;
}

/* Implement TARGET_USE_BLOCKS_FOR_CONSTANT_P.  MIPS16 uses per-function
   constant pools, but normal-mode code doesn't need to.  */

static bool
mips_use_blocks_for_constant_p (enum machine_mode mode ATTRIBUTE_UNUSED,
				rtx x ATTRIBUTE_UNUSED)
{
  return !TARGET_MIPS16;
}

/* Return the number of instructions needed to load a symbol of the
   given type into a register.  If valid in an address, the same number
   of instructions are needed for loads and stores.  Treat extended
   mips16 instructions as two instructions.  */

static int
mips_symbol_insns (enum mips_symbol_type type)
{
  switch (type)
    {
    case SYMBOL_GENERAL:
      /* In mips16 code, general symbols must be fetched from the
	 constant pool.  */
      if (TARGET_MIPS16)
	return 0;

      /* When using 64-bit symbols, we need 5 preparatory instructions,
	 such as:

	     lui     $at,%highest(symbol)
	     daddiu  $at,$at,%higher(symbol)
	     dsll    $at,$at,16
	     daddiu  $at,$at,%hi(symbol)
	     dsll    $at,$at,16

	 The final address is then $at + %lo(symbol).  With 32-bit
	 symbols we just need a preparatory lui.  */
      return (ABI_HAS_64BIT_SYMBOLS ? 6 : 2);

    case SYMBOL_SMALL_DATA:
      return 1;

    case SYMBOL_CONSTANT_POOL:
      /* This case is for mips16 only.  Assume we'll need an
	 extended instruction.  */
      return 2;

    case SYMBOL_GOT_LOCAL:
    case SYMBOL_GOT_GLOBAL:
      /* Unless -funit-at-a-time is in effect, we can't be sure whether
	 the local/global classification is accurate.  See override_options
	 for details.

	 The worst cases are:

	 (1) For local symbols when generating o32 or o64 code.  The assembler
	     will use:

		 lw	      $at,%got(symbol)
		 nop

	     ...and the final address will be $at + %lo(symbol).

	 (2) For global symbols when -mxgot.  The assembler will use:

	         lui     $at,%got_hi(symbol)
	         (d)addu $at,$at,$gp

	     ...and the final address will be $at + %got_lo(symbol).  */
      return 3;

    case SYMBOL_GOTOFF_PAGE:
    case SYMBOL_GOTOFF_GLOBAL:
    case SYMBOL_GOTOFF_CALL:
    case SYMBOL_GOTOFF_LOADGP:
    case SYMBOL_64_HIGH:
    case SYMBOL_64_MID:
    case SYMBOL_64_LOW:
    case SYMBOL_TLSGD:
    case SYMBOL_TLSLDM:
    case SYMBOL_DTPREL:
    case SYMBOL_GOTTPREL:
    case SYMBOL_TPREL:
      /* Check whether the offset is a 16- or 32-bit value.  */
      return mips_split_p[type] ? 2 : 1;

    case SYMBOL_TLS:
      /* We don't treat a bare TLS symbol as a constant.  */
      return 0;
    }
  gcc_unreachable ();
}

/* Return true if X is a legitimate $sp-based address for mode MDOE.  */

bool
mips_stack_address_p (rtx x, enum machine_mode mode)
{
  struct mips_address_info addr;

  return (mips_classify_address (&addr, x, mode, false)
	  && addr.type == ADDRESS_REG
	  && addr.reg == stack_pointer_rtx);
}

/* Return true if a value at OFFSET bytes from BASE can be accessed
   using an unextended mips16 instruction.  MODE is the mode of the
   value.

   Usually the offset in an unextended instruction is a 5-bit field.
   The offset is unsigned and shifted left once for HIs, twice
   for SIs, and so on.  An exception is SImode accesses off the
   stack pointer, which have an 8-bit immediate field.  */

static bool
mips16_unextended_reference_p (enum machine_mode mode, rtx base, rtx offset)
{
  if (TARGET_MIPS16
      && GET_CODE (offset) == CONST_INT
      && INTVAL (offset) >= 0
      && (INTVAL (offset) & (GET_MODE_SIZE (mode) - 1)) == 0)
    {
      if (GET_MODE_SIZE (mode) == 4 && base == stack_pointer_rtx)
	return INTVAL (offset) < 256 * GET_MODE_SIZE (mode);
      return INTVAL (offset) < 32 * GET_MODE_SIZE (mode);
    }
  return false;
}


/* Return the number of instructions needed to load or store a value
   of mode MODE at X.  Return 0 if X isn't valid for MODE.

   For mips16 code, count extended instructions as two instructions.  */

int
mips_address_insns (rtx x, enum machine_mode mode)
{
  struct mips_address_info addr;
  int factor;

  if (mode == BLKmode)
    /* BLKmode is used for single unaligned loads and stores.  */
    factor = 1;
  else
    /* Each word of a multi-word value will be accessed individually.  */
    factor = (GET_MODE_SIZE (mode) + UNITS_PER_WORD - 1) / UNITS_PER_WORD;

  if (mips_classify_address (&addr, x, mode, false))
    switch (addr.type)
      {
      case ADDRESS_REG:
	if (TARGET_MIPS16
	    && !mips16_unextended_reference_p (mode, addr.reg, addr.offset))
	  return factor * 2;
	return factor;

      case ADDRESS_LO_SUM:
	return (TARGET_MIPS16 ? factor * 2 : factor);

      case ADDRESS_CONST_INT:
	return factor;

      case ADDRESS_SYMBOLIC:
	return factor * mips_symbol_insns (addr.symbol_type);
      }
  return 0;
}


/* Likewise for constant X.  */

int
mips_const_insns (rtx x)
{
  struct mips_integer_op codes[MIPS_MAX_INTEGER_OPS];
  enum mips_symbol_type symbol_type;
  HOST_WIDE_INT offset;

  switch (GET_CODE (x))
    {
    case HIGH:
      if (TARGET_MIPS16
	  || !mips_symbolic_constant_p (XEXP (x, 0), &symbol_type)
	  || !mips_split_p[symbol_type])
	return 0;

      return 1;

    case CONST_INT:
      if (TARGET_MIPS16)
	/* Unsigned 8-bit constants can be loaded using an unextended
	   LI instruction.  Unsigned 16-bit constants can be loaded
	   using an extended LI.  Negative constants must be loaded
	   using LI and then negated.  */
	return (INTVAL (x) >= 0 && INTVAL (x) < 256 ? 1
		: SMALL_OPERAND_UNSIGNED (INTVAL (x)) ? 2
		: INTVAL (x) > -256 && INTVAL (x) < 0 ? 2
		: SMALL_OPERAND_UNSIGNED (-INTVAL (x)) ? 3
		: 0);

      return mips_build_integer (codes, INTVAL (x));

    case CONST_DOUBLE:
    case CONST_VECTOR:
      return (!TARGET_MIPS16 && x == CONST0_RTX (GET_MODE (x)) ? 1 : 0);

    case CONST:
      if (CONST_GP_P (x))
	return 1;

      /* See if we can refer to X directly.  */
      if (mips_symbolic_constant_p (x, &symbol_type))
	return mips_symbol_insns (symbol_type);

      /* Otherwise try splitting the constant into a base and offset.
	 16-bit offsets can be added using an extra addiu.  Larger offsets
	 must be calculated separately and then added to the base.  */
      mips_split_const (x, &x, &offset);
      if (offset != 0)
	{
	  int n = mips_const_insns (x);
	  if (n != 0)
	    {
	      if (SMALL_OPERAND (offset))
		return n + 1;
	      else
		return n + 1 + mips_build_integer (codes, offset);
	    }
	}
      return 0;

    case SYMBOL_REF:
    case LABEL_REF:
      return mips_symbol_insns (mips_classify_symbol (x));

    default:
      return 0;
    }
}


/* Return the number of instructions needed for memory reference X.
   Count extended mips16 instructions as two instructions.  */

int
mips_fetch_insns (rtx x)
{
  gcc_assert (MEM_P (x));
  return mips_address_insns (XEXP (x, 0), GET_MODE (x));
}


/* Return the number of instructions needed for an integer division.  */

int
mips_idiv_insns (void)
{
  int count;

  count = 1;
  if (TARGET_CHECK_ZERO_DIV)
    {
      if (GENERATE_DIVIDE_TRAPS)
        count++;
      else
        count += 2;
    }

  if (TARGET_FIX_R4000 || TARGET_FIX_R4400)
    count++;
  return count;
}

/* This function is used to implement GO_IF_LEGITIMATE_ADDRESS.  It
   returns a nonzero value if X is a legitimate address for a memory
   operand of the indicated MODE.  STRICT is nonzero if this function
   is called during reload.  */

bool
mips_legitimate_address_p (enum machine_mode mode, rtx x, int strict)
{
  struct mips_address_info addr;

  return mips_classify_address (&addr, x, mode, strict);
}


/* Copy VALUE to a register and return that register.  If new psuedos
   are allowed, copy it into a new register, otherwise use DEST.  */

static rtx
mips_force_temporary (rtx dest, rtx value)
{
  if (!no_new_pseudos)
    return force_reg (Pmode, value);
  else
    {
      emit_move_insn (copy_rtx (dest), value);
      return dest;
    }
}


/* Return a LO_SUM expression for ADDR.  TEMP is as for mips_force_temporary
   and is used to load the high part into a register.  */

rtx
mips_split_symbol (rtx temp, rtx addr)
{
  rtx high;

  if (TARGET_MIPS16)
    high = mips16_gp_pseudo_reg ();
  else
    high = mips_force_temporary (temp, gen_rtx_HIGH (Pmode, copy_rtx (addr)));
  return gen_rtx_LO_SUM (Pmode, high, addr);
}


/* Return an UNSPEC address with underlying address ADDRESS and symbol
   type SYMBOL_TYPE.  */

rtx
mips_unspec_address (rtx address, enum mips_symbol_type symbol_type)
{
  rtx base;
  HOST_WIDE_INT offset;

  mips_split_const (address, &base, &offset);
  base = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, base),
			 UNSPEC_ADDRESS_FIRST + symbol_type);
  return plus_constant (gen_rtx_CONST (Pmode, base), offset);
}


/* If mips_unspec_address (ADDR, SYMBOL_TYPE) is a 32-bit value, add the
   high part to BASE and return the result.  Just return BASE otherwise.
   TEMP is available as a temporary register if needed.

   The returned expression can be used as the first operand to a LO_SUM.  */

static rtx
mips_unspec_offset_high (rtx temp, rtx base, rtx addr,
			 enum mips_symbol_type symbol_type)
{
  if (mips_split_p[symbol_type])
    {
      addr = gen_rtx_HIGH (Pmode, mips_unspec_address (addr, symbol_type));
      addr = mips_force_temporary (temp, addr);
      return mips_force_temporary (temp, gen_rtx_PLUS (Pmode, addr, base));
    }
  return base;
}


/* Return a legitimate address for REG + OFFSET.  TEMP is as for
   mips_force_temporary; it is only needed when OFFSET is not a
   SMALL_OPERAND.  */

static rtx
mips_add_offset (rtx temp, rtx reg, HOST_WIDE_INT offset)
{
  if (!SMALL_OPERAND (offset))
    {
      rtx high;
      if (TARGET_MIPS16)
	{
	  /* Load the full offset into a register so that we can use
	     an unextended instruction for the address itself.  */
	  high = GEN_INT (offset);
	  offset = 0;
	}
      else
	{
	  /* Leave OFFSET as a 16-bit offset and put the excess in HIGH.  */
	  high = GEN_INT (CONST_HIGH_PART (offset));
	  offset = CONST_LOW_PART (offset);
	}
      high = mips_force_temporary (temp, high);
      reg = mips_force_temporary (temp, gen_rtx_PLUS (Pmode, high, reg));
    }
  return plus_constant (reg, offset);
}

/* Emit a call to __tls_get_addr.  SYM is the TLS symbol we are
   referencing, and TYPE is the symbol type to use (either global
   dynamic or local dynamic).  V0 is an RTX for the return value
   location.  The entire insn sequence is returned.  */

static GTY(()) rtx mips_tls_symbol;

static rtx
mips_call_tls_get_addr (rtx sym, enum mips_symbol_type type, rtx v0)
{
  rtx insn, loc, tga, a0;

  a0 = gen_rtx_REG (Pmode, GP_ARG_FIRST);

  if (!mips_tls_symbol)
    mips_tls_symbol = init_one_libfunc ("__tls_get_addr");

  loc = mips_unspec_address (sym, type);

  start_sequence ();

  emit_insn (gen_rtx_SET (Pmode, a0,
			  gen_rtx_LO_SUM (Pmode, pic_offset_table_rtx, loc)));
  tga = gen_rtx_MEM (Pmode, mips_tls_symbol);
  insn = emit_call_insn (gen_call_value (v0, tga, const0_rtx, const0_rtx));
  CONST_OR_PURE_CALL_P (insn) = 1;
  use_reg (&CALL_INSN_FUNCTION_USAGE (insn), v0);
  use_reg (&CALL_INSN_FUNCTION_USAGE (insn), a0);
  insn = get_insns ();

  end_sequence ();

  return insn;
}

/* Generate the code to access LOC, a thread local SYMBOL_REF.  The
   return value will be a valid address and move_operand (either a REG
   or a LO_SUM).  */

static rtx
mips_legitimize_tls_address (rtx loc)
{
  rtx dest, insn, v0, v1, tmp1, tmp2, eqv;
  enum tls_model model;

  v0 = gen_rtx_REG (Pmode, GP_RETURN);
  v1 = gen_rtx_REG (Pmode, GP_RETURN + 1);

  model = SYMBOL_REF_TLS_MODEL (loc);
  /* Only TARGET_ABICALLS code can have more than one module; other
     code must be be static and should not use a GOT.  All TLS models
     reduce to local exec in this situation.  */
  if (!TARGET_ABICALLS)
    model = TLS_MODEL_LOCAL_EXEC;

  switch (model)
    {
    case TLS_MODEL_GLOBAL_DYNAMIC:
      insn = mips_call_tls_get_addr (loc, SYMBOL_TLSGD, v0);
      dest = gen_reg_rtx (Pmode);
      emit_libcall_block (insn, dest, v0, loc);
      break;

    case TLS_MODEL_LOCAL_DYNAMIC:
      insn = mips_call_tls_get_addr (loc, SYMBOL_TLSLDM, v0);
      tmp1 = gen_reg_rtx (Pmode);

      /* Attach a unique REG_EQUIV, to allow the RTL optimizers to
	 share the LDM result with other LD model accesses.  */
      eqv = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, const0_rtx),
			    UNSPEC_TLS_LDM);
      emit_libcall_block (insn, tmp1, v0, eqv);

      tmp2 = mips_unspec_offset_high (NULL, tmp1, loc, SYMBOL_DTPREL);
      dest = gen_rtx_LO_SUM (Pmode, tmp2,
			     mips_unspec_address (loc, SYMBOL_DTPREL));
      break;

    case TLS_MODEL_INITIAL_EXEC:
      tmp1 = gen_reg_rtx (Pmode);
      tmp2 = mips_unspec_address (loc, SYMBOL_GOTTPREL);
      if (Pmode == DImode)
	{
	  emit_insn (gen_tls_get_tp_di (v1));
	  emit_insn (gen_load_gotdi (tmp1, pic_offset_table_rtx, tmp2));
	}
      else
	{
	  emit_insn (gen_tls_get_tp_si (v1));
	  emit_insn (gen_load_gotsi (tmp1, pic_offset_table_rtx, tmp2));
	}
      dest = gen_reg_rtx (Pmode);
      emit_insn (gen_add3_insn (dest, tmp1, v1));
      break;

    case TLS_MODEL_LOCAL_EXEC:
      if (Pmode == DImode)
	emit_insn (gen_tls_get_tp_di (v1));
      else
	emit_insn (gen_tls_get_tp_si (v1));

      tmp1 = mips_unspec_offset_high (NULL, v1, loc, SYMBOL_TPREL);
      dest = gen_rtx_LO_SUM (Pmode, tmp1,
			     mips_unspec_address (loc, SYMBOL_TPREL));
      break;

    default:
      gcc_unreachable ();
    }

  return dest;
}

/* This function is used to implement LEGITIMIZE_ADDRESS.  If *XLOC can
   be legitimized in a way that the generic machinery might not expect,
   put the new address in *XLOC and return true.  MODE is the mode of
   the memory being accessed.  */

bool
mips_legitimize_address (rtx *xloc, enum machine_mode mode)
{
  enum mips_symbol_type symbol_type;

  if (mips_tls_operand_p (*xloc))
    {
      *xloc = mips_legitimize_tls_address (*xloc);
      return true;
    }

  /* See if the address can split into a high part and a LO_SUM.  */
  if (mips_symbolic_constant_p (*xloc, &symbol_type)
      && mips_symbolic_address_p (symbol_type, mode)
      && mips_split_p[symbol_type])
    {
      *xloc = mips_split_symbol (0, *xloc);
      return true;
    }

  if (GET_CODE (*xloc) == PLUS && GET_CODE (XEXP (*xloc, 1)) == CONST_INT)
    {
      /* Handle REG + CONSTANT using mips_add_offset.  */
      rtx reg;

      reg = XEXP (*xloc, 0);
      if (!mips_valid_base_register_p (reg, mode, 0))
	reg = copy_to_mode_reg (Pmode, reg);
      *xloc = mips_add_offset (0, reg, INTVAL (XEXP (*xloc, 1)));
      return true;
    }

  return false;
}


/* Subroutine of mips_build_integer (with the same interface).
   Assume that the final action in the sequence should be a left shift.  */

static unsigned int
mips_build_shift (struct mips_integer_op *codes, HOST_WIDE_INT value)
{
  unsigned int i, shift;

  /* Shift VALUE right until its lowest bit is set.  Shift arithmetically
     since signed numbers are easier to load than unsigned ones.  */
  shift = 0;
  while ((value & 1) == 0)
    value /= 2, shift++;

  i = mips_build_integer (codes, value);
  codes[i].code = ASHIFT;
  codes[i].value = shift;
  return i + 1;
}


/* As for mips_build_shift, but assume that the final action will be
   an IOR or PLUS operation.  */

static unsigned int
mips_build_lower (struct mips_integer_op *codes, unsigned HOST_WIDE_INT value)
{
  unsigned HOST_WIDE_INT high;
  unsigned int i;

  high = value & ~(unsigned HOST_WIDE_INT) 0xffff;
  if (!LUI_OPERAND (high) && (value & 0x18000) == 0x18000)
    {
      /* The constant is too complex to load with a simple lui/ori pair
	 so our goal is to clear as many trailing zeros as possible.
	 In this case, we know bit 16 is set and that the low 16 bits
	 form a negative number.  If we subtract that number from VALUE,
	 we will clear at least the lowest 17 bits, maybe more.  */
      i = mips_build_integer (codes, CONST_HIGH_PART (value));
      codes[i].code = PLUS;
      codes[i].value = CONST_LOW_PART (value);
    }
  else
    {
      i = mips_build_integer (codes, high);
      codes[i].code = IOR;
      codes[i].value = value & 0xffff;
    }
  return i + 1;
}


/* Fill CODES with a sequence of rtl operations to load VALUE.
   Return the number of operations needed.  */

static unsigned int
mips_build_integer (struct mips_integer_op *codes,
		    unsigned HOST_WIDE_INT value)
{
  if (SMALL_OPERAND (value)
      || SMALL_OPERAND_UNSIGNED (value)
      || LUI_OPERAND (value))
    {
      /* The value can be loaded with a single instruction.  */
      codes[0].code = UNKNOWN;
      codes[0].value = value;
      return 1;
    }
  else if ((value & 1) != 0 || LUI_OPERAND (CONST_HIGH_PART (value)))
    {
      /* Either the constant is a simple LUI/ORI combination or its
	 lowest bit is set.  We don't want to shift in this case.  */
      return mips_build_lower (codes, value);
    }
  else if ((value & 0xffff) == 0)
    {
      /* The constant will need at least three actions.  The lowest
	 16 bits are clear, so the final action will be a shift.  */
      return mips_build_shift (codes, value);
    }
  else
    {
      /* The final action could be a shift, add or inclusive OR.
	 Rather than use a complex condition to select the best
	 approach, try both mips_build_shift and mips_build_lower
	 and pick the one that gives the shortest sequence.
	 Note that this case is only used once per constant.  */
      struct mips_integer_op alt_codes[MIPS_MAX_INTEGER_OPS];
      unsigned int cost, alt_cost;

      cost = mips_build_shift (codes, value);
      alt_cost = mips_build_lower (alt_codes, value);
      if (alt_cost < cost)
	{
	  memcpy (codes, alt_codes, alt_cost * sizeof (codes[0]));
	  cost = alt_cost;
	}
      return cost;
    }
}


/* Load VALUE into DEST, using TEMP as a temporary register if need be.  */

void
mips_move_integer (rtx dest, rtx temp, unsigned HOST_WIDE_INT value)
{
  struct mips_integer_op codes[MIPS_MAX_INTEGER_OPS];
  enum machine_mode mode;
  unsigned int i, cost;
  rtx x;

  mode = GET_MODE (dest);
  cost = mips_build_integer (codes, value);

  /* Apply each binary operation to X.  Invariant: X is a legitimate
     source operand for a SET pattern.  */
  x = GEN_INT (codes[0].value);
  for (i = 1; i < cost; i++)
    {
      if (no_new_pseudos)
	{
	  emit_insn (gen_rtx_SET (VOIDmode, temp, x));
	  x = temp;
	}
      else
	x = force_reg (mode, x);
      x = gen_rtx_fmt_ee (codes[i].code, mode, x, GEN_INT (codes[i].value));
    }

  emit_insn (gen_rtx_SET (VOIDmode, dest, x));
}


/* Subroutine of mips_legitimize_move.  Move constant SRC into register
   DEST given that SRC satisfies immediate_operand but doesn't satisfy
   move_operand.  */

static void
mips_legitimize_const_move (enum machine_mode mode, rtx dest, rtx src)
{
  rtx base;
  HOST_WIDE_INT offset;

  /* Split moves of big integers into smaller pieces.  */
  if (splittable_const_int_operand (src, mode))
    {
      mips_move_integer (dest, dest, INTVAL (src));
      return;
    }

  /* Split moves of symbolic constants into high/low pairs.  */
  if (splittable_symbolic_operand (src, mode))
    {
      emit_insn (gen_rtx_SET (VOIDmode, dest, mips_split_symbol (dest, src)));
      return;
    }

  if (mips_tls_operand_p (src))
    {
      emit_move_insn (dest, mips_legitimize_tls_address (src));
      return;
    }

  /* If we have (const (plus symbol offset)), load the symbol first
     and then add in the offset.  This is usually better than forcing
     the constant into memory, at least in non-mips16 code.  */
  mips_split_const (src, &base, &offset);
  if (!TARGET_MIPS16
      && offset != 0
      && (!no_new_pseudos || SMALL_OPERAND (offset)))
    {
      base = mips_force_temporary (dest, base);
      emit_move_insn (dest, mips_add_offset (0, base, offset));
      return;
    }

  src = force_const_mem (mode, src);

  /* When using explicit relocs, constant pool references are sometimes
     not legitimate addresses.  */
  if (!memory_operand (src, VOIDmode))
    src = replace_equiv_address (src, mips_split_symbol (dest, XEXP (src, 0)));
  emit_move_insn (dest, src);
}


/* If (set DEST SRC) is not a valid instruction, emit an equivalent
   sequence that is valid.  */

bool
mips_legitimize_move (enum machine_mode mode, rtx dest, rtx src)
{
  if (!register_operand (dest, mode) && !reg_or_0_operand (src, mode))
    {
      emit_move_insn (dest, force_reg (mode, src));
      return true;
    }

  /* Check for individual, fully-reloaded mflo and mfhi instructions.  */
  if (GET_MODE_SIZE (mode) <= UNITS_PER_WORD
      && REG_P (src) && MD_REG_P (REGNO (src))
      && REG_P (dest) && GP_REG_P (REGNO (dest)))
    {
      int other_regno = REGNO (src) == HI_REGNUM ? LO_REGNUM : HI_REGNUM;
      if (GET_MODE_SIZE (mode) <= 4)
	emit_insn (gen_mfhilo_si (gen_rtx_REG (SImode, REGNO (dest)),
				  gen_rtx_REG (SImode, REGNO (src)),
				  gen_rtx_REG (SImode, other_regno)));
      else
	emit_insn (gen_mfhilo_di (gen_rtx_REG (DImode, REGNO (dest)),
				  gen_rtx_REG (DImode, REGNO (src)),
				  gen_rtx_REG (DImode, other_regno)));
      return true;
    }

  /* We need to deal with constants that would be legitimate
     immediate_operands but not legitimate move_operands.  */
  if (CONSTANT_P (src) && !move_operand (src, mode))
    {
      mips_legitimize_const_move (mode, dest, src);
      set_unique_reg_note (get_last_insn (), REG_EQUAL, copy_rtx (src));
      return true;
    }
  return false;
}

/* We need a lot of little routines to check constant values on the
   mips16.  These are used to figure out how long the instruction will
   be.  It would be much better to do this using constraints, but
   there aren't nearly enough letters available.  */

static int
m16_check_op (rtx op, int low, int high, int mask)
{
  return (GET_CODE (op) == CONST_INT
	  && INTVAL (op) >= low
	  && INTVAL (op) <= high
	  && (INTVAL (op) & mask) == 0);
}

int
m16_uimm3_b (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  return m16_check_op (op, 0x1, 0x8, 0);
}

int
m16_simm4_1 (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  return m16_check_op (op, - 0x8, 0x7, 0);
}

int
m16_nsimm4_1 (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  return m16_check_op (op, - 0x7, 0x8, 0);
}

int
m16_simm5_1 (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  return m16_check_op (op, - 0x10, 0xf, 0);
}

int
m16_nsimm5_1 (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  return m16_check_op (op, - 0xf, 0x10, 0);
}

int
m16_uimm5_4 (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  return m16_check_op (op, (- 0x10) << 2, 0xf << 2, 3);
}

int
m16_nuimm5_4 (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  return m16_check_op (op, (- 0xf) << 2, 0x10 << 2, 3);
}

int
m16_simm8_1 (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  return m16_check_op (op, - 0x80, 0x7f, 0);
}

int
m16_nsimm8_1 (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  return m16_check_op (op, - 0x7f, 0x80, 0);
}

int
m16_uimm8_1 (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  return m16_check_op (op, 0x0, 0xff, 0);
}

int
m16_nuimm8_1 (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  return m16_check_op (op, - 0xff, 0x0, 0);
}

int
m16_uimm8_m1_1 (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  return m16_check_op (op, - 0x1, 0xfe, 0);
}

int
m16_uimm8_4 (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  return m16_check_op (op, 0x0, 0xff << 2, 3);
}

int
m16_nuimm8_4 (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  return m16_check_op (op, (- 0xff) << 2, 0x0, 3);
}

int
m16_simm8_8 (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  return m16_check_op (op, (- 0x80) << 3, 0x7f << 3, 7);
}

int
m16_nsimm8_8 (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  return m16_check_op (op, (- 0x7f) << 3, 0x80 << 3, 7);
}

static bool
mips_rtx_costs (rtx x, int code, int outer_code, int *total)
{
  enum machine_mode mode = GET_MODE (x);
  bool float_mode_p = FLOAT_MODE_P (mode);

  switch (code)
    {
    case CONST_INT:
      if (TARGET_MIPS16)
        {
	  /* A number between 1 and 8 inclusive is efficient for a shift.
	     Otherwise, we will need an extended instruction.  */
	  if ((outer_code) == ASHIFT || (outer_code) == ASHIFTRT
	      || (outer_code) == LSHIFTRT)
	    {
	      if (INTVAL (x) >= 1 && INTVAL (x) <= 8)
		*total = 0;
	      else
		*total = COSTS_N_INSNS (1);
	      return true;
	    }

	  /* We can use cmpi for an xor with an unsigned 16 bit value.  */
	  if ((outer_code) == XOR
	      && INTVAL (x) >= 0 && INTVAL (x) < 0x10000)
	    {
	      *total = 0;
	      return true;
	    }

	  /* We may be able to use slt or sltu for a comparison with a
	     signed 16 bit value.  (The boundary conditions aren't quite
	     right, but this is just a heuristic anyhow.)  */
	  if (((outer_code) == LT || (outer_code) == LE
	       || (outer_code) == GE || (outer_code) == GT
	       || (outer_code) == LTU || (outer_code) == LEU
	       || (outer_code) == GEU || (outer_code) == GTU)
	      && INTVAL (x) >= -0x8000 && INTVAL (x) < 0x8000)
	    {
	      *total = 0;
	      return true;
	    }

	  /* Equality comparisons with 0 are cheap.  */
	  if (((outer_code) == EQ || (outer_code) == NE)
	      && INTVAL (x) == 0)
	    {
	      *total = 0;
	      return true;
	    }

	  /* Constants in the range 0...255 can be loaded with an unextended
	     instruction.  They are therefore as cheap as a register move.

	     Given the choice between "li R1,0...255" and "move R1,R2"
	     (where R2 is a known constant), it is usually better to use "li",
	     since we do not want to unnecessarily extend the lifetime
	     of R2.  */
	  if (outer_code == SET
	      && INTVAL (x) >= 0
	      && INTVAL (x) < 256)
	    {
	      *total = 0;
	      return true;
	    }
	}
      else
	{
	  /* These can be used anywhere. */
	  *total = 0;
	  return true;
	}

      /* Otherwise fall through to the handling below because
	 we'll need to construct the constant.  */

    case CONST:
    case SYMBOL_REF:
    case LABEL_REF:
    case CONST_DOUBLE:
      if (LEGITIMATE_CONSTANT_P (x))
	{
	  *total = COSTS_N_INSNS (1);
	  return true;
	}
      else
	{
	  /* The value will need to be fetched from the constant pool.  */
	  *total = CONSTANT_POOL_COST;
	  return true;
	}

    case MEM:
      {
	/* If the address is legitimate, return the number of
	   instructions it needs, otherwise use the default handling.  */
	int n = mips_address_insns (XEXP (x, 0), GET_MODE (x));
	if (n > 0)
	  {
	    *total = COSTS_N_INSNS (n + 1);
	    return true;
	  }
	return false;
      }

    case FFS:
      *total = COSTS_N_INSNS (6);
      return true;

    case NOT:
      *total = COSTS_N_INSNS ((mode == DImode && !TARGET_64BIT) ? 2 : 1);
      return true;

    case AND:
    case IOR:
    case XOR:
      if (mode == DImode && !TARGET_64BIT)
        {
          *total = COSTS_N_INSNS (2);
          return true;
        }
      return false;

    case ASHIFT:
    case ASHIFTRT:
    case LSHIFTRT:
      if (mode == DImode && !TARGET_64BIT)
        {
          *total = COSTS_N_INSNS ((GET_CODE (XEXP (x, 1)) == CONST_INT)
                                  ? 4 : 12);
          return true;
        }
      return false;

    case ABS:
      if (float_mode_p)
        *total = COSTS_N_INSNS (1);
      else
        *total = COSTS_N_INSNS (4);
      return true;

    case LO_SUM:
      *total = COSTS_N_INSNS (1);
      return true;

    case PLUS:
    case MINUS:
      if (float_mode_p)
	{
	  *total = mips_cost->fp_add;
	  return true;
	}

      else if (mode == DImode && !TARGET_64BIT)
        {
          *total = COSTS_N_INSNS (4);
          return true;
        }
      return false;

    case NEG:
      if (mode == DImode && !TARGET_64BIT)
        {
          *total = COSTS_N_INSNS (4);
          return true;
        }
      return false;

    case MULT:
      if (mode == SFmode)
	*total = mips_cost->fp_mult_sf;

      else if (mode == DFmode)
	*total = mips_cost->fp_mult_df;

      else if (mode == SImode)
	*total = mips_cost->int_mult_si;

      else
	*total = mips_cost->int_mult_di;

      return true;

    case DIV:
    case MOD:
      if (float_mode_p)
	{
	  if (mode == SFmode)
	    *total = mips_cost->fp_div_sf;
	  else
	    *total = mips_cost->fp_div_df;

	  return true;
	}
      /* Fall through.  */

    case UDIV:
    case UMOD:
      if (mode == DImode)
        *total = mips_cost->int_div_di;
      else
	*total = mips_cost->int_div_si;

      return true;

    case SIGN_EXTEND:
      /* A sign extend from SImode to DImode in 64 bit mode is often
         zero instructions, because the result can often be used
         directly by another instruction; we'll call it one.  */
      if (TARGET_64BIT && mode == DImode
          && GET_MODE (XEXP (x, 0)) == SImode)
        *total = COSTS_N_INSNS (1);
      else
        *total = COSTS_N_INSNS (2);
      return true;

    case ZERO_EXTEND:
      if (TARGET_64BIT && mode == DImode
          && GET_MODE (XEXP (x, 0)) == SImode)
        *total = COSTS_N_INSNS (2);
      else
        *total = COSTS_N_INSNS (1);
      return true;

    case FLOAT:
    case UNSIGNED_FLOAT:
    case FIX:
    case FLOAT_EXTEND:
    case FLOAT_TRUNCATE:
    case SQRT:
      *total = mips_cost->fp_add;
      return true;

    default:
      return false;
    }
}

/* Provide the costs of an addressing mode that contains ADDR.
   If ADDR is not a valid address, its cost is irrelevant.  */

static int
mips_address_cost (rtx addr)
{
  return mips_address_insns (addr, SImode);
}

/* Return one word of double-word value OP, taking into account the fixed
   endianness of certain registers.  HIGH_P is true to select the high part,
   false to select the low part.  */

rtx
mips_subword (rtx op, int high_p)
{
  unsigned int byte;
  enum machine_mode mode;

  mode = GET_MODE (op);
  if (mode == VOIDmode)
    mode = DImode;

  if (TARGET_BIG_ENDIAN ? !high_p : high_p)
    byte = UNITS_PER_WORD;
  else
    byte = 0;

  if (REG_P (op))
    {
      if (FP_REG_P (REGNO (op)))
	return gen_rtx_REG (word_mode, high_p ? REGNO (op) + 1 : REGNO (op));
      if (ACC_HI_REG_P (REGNO (op)))
	return gen_rtx_REG (word_mode, high_p ? REGNO (op) : REGNO (op) + 1);
    }

  if (MEM_P (op))
    return mips_rewrite_small_data (adjust_address (op, word_mode, byte));

  return simplify_gen_subreg (word_mode, op, mode, byte);
}


/* Return true if a 64-bit move from SRC to DEST should be split into two.  */

bool
mips_split_64bit_move_p (rtx dest, rtx src)
{
  if (TARGET_64BIT)
    return false;

  /* FP->FP moves can be done in a single instruction.  */
  if (FP_REG_RTX_P (src) && FP_REG_RTX_P (dest))
    return false;

  /* Check for floating-point loads and stores.  They can be done using
     ldc1 and sdc1 on MIPS II and above.  */
  if (mips_isa > 1)
    {
      if (FP_REG_RTX_P (dest) && MEM_P (src))
	return false;
      if (FP_REG_RTX_P (src) && MEM_P (dest))
	return false;
    }
  return true;
}


/* Split a 64-bit move from SRC to DEST assuming that
   mips_split_64bit_move_p holds.

   Moves into and out of FPRs cause some difficulty here.  Such moves
   will always be DFmode, since paired FPRs are not allowed to store
   DImode values.  The most natural representation would be two separate
   32-bit moves, such as:

	(set (reg:SI $f0) (mem:SI ...))
	(set (reg:SI $f1) (mem:SI ...))

   However, the second insn is invalid because odd-numbered FPRs are
   not allowed to store independent values.  Use the patterns load_df_low,
   load_df_high and store_df_high instead.  */

void
mips_split_64bit_move (rtx dest, rtx src)
{
  if (FP_REG_RTX_P (dest))
    {
      /* Loading an FPR from memory or from GPRs.  */
      emit_insn (gen_load_df_low (copy_rtx (dest), mips_subword (src, 0)));
      emit_insn (gen_load_df_high (dest, mips_subword (src, 1),
				   copy_rtx (dest)));
    }
  else if (FP_REG_RTX_P (src))
    {
      /* Storing an FPR into memory or GPRs.  */
      emit_move_insn (mips_subword (dest, 0), mips_subword (src, 0));
      emit_insn (gen_store_df_high (mips_subword (dest, 1), src));
    }
  else
    {
      /* The operation can be split into two normal moves.  Decide in
	 which order to do them.  */
      rtx low_dest;

      low_dest = mips_subword (dest, 0);
      if (REG_P (low_dest)
	  && reg_overlap_mentioned_p (low_dest, src))
	{
	  emit_move_insn (mips_subword (dest, 1), mips_subword (src, 1));
	  emit_move_insn (low_dest, mips_subword (src, 0));
	}
      else
	{
	  emit_move_insn (low_dest, mips_subword (src, 0));
	  emit_move_insn (mips_subword (dest, 1), mips_subword (src, 1));
	}
    }
}

/* Return the appropriate instructions to move SRC into DEST.  Assume
   that SRC is operand 1 and DEST is operand 0.  */

const char *
mips_output_move (rtx dest, rtx src)
{
  enum rtx_code dest_code, src_code;
  bool dbl_p;

  dest_code = GET_CODE (dest);
  src_code = GET_CODE (src);
  dbl_p = (GET_MODE_SIZE (GET_MODE (dest)) == 8);

  if (dbl_p && mips_split_64bit_move_p (dest, src))
    return "#";

  if ((src_code == REG && GP_REG_P (REGNO (src)))
      || (!TARGET_MIPS16 && src == CONST0_RTX (GET_MODE (dest))))
    {
      if (dest_code == REG)
	{
	  if (GP_REG_P (REGNO (dest)))
	    return "move\t%0,%z1";

	  if (MD_REG_P (REGNO (dest)))
	    return "mt%0\t%z1";

	  if (DSP_ACC_REG_P (REGNO (dest)))
	    {
	      static char retval[] = "mt__\t%z1,%q0";
	      retval[2] = reg_names[REGNO (dest)][4];
	      retval[3] = reg_names[REGNO (dest)][5];
	      return retval;
	    }

	  if (FP_REG_P (REGNO (dest)))
	    return (dbl_p ? "dmtc1\t%z1,%0" : "mtc1\t%z1,%0");

	  if (ALL_COP_REG_P (REGNO (dest)))
	    {
	      static char retval[] = "dmtc_\t%z1,%0";

	      retval[4] = COPNUM_AS_CHAR_FROM_REGNUM (REGNO (dest));
	      return (dbl_p ? retval : retval + 1);
	    }
	}
      if (dest_code == MEM)
	return (dbl_p ? "sd\t%z1,%0" : "sw\t%z1,%0");
    }
  if (dest_code == REG && GP_REG_P (REGNO (dest)))
    {
      if (src_code == REG)
	{
	  if (DSP_ACC_REG_P (REGNO (src)))
	    {
	      static char retval[] = "mf__\t%0,%q1";
	      retval[2] = reg_names[REGNO (src)][4];
	      retval[3] = reg_names[REGNO (src)][5];
	      return retval;
	    }

	  if (ST_REG_P (REGNO (src)) && ISA_HAS_8CC)
	    return "lui\t%0,0x3f80\n\tmovf\t%0,%.,%1";

	  if (FP_REG_P (REGNO (src)))
	    return (dbl_p ? "dmfc1\t%0,%1" : "mfc1\t%0,%1");

	  if (ALL_COP_REG_P (REGNO (src)))
	    {
	      static char retval[] = "dmfc_\t%0,%1";

	      retval[4] = COPNUM_AS_CHAR_FROM_REGNUM (REGNO (src));
	      return (dbl_p ? retval : retval + 1);
	    }
	}

      if (src_code == MEM)
	return (dbl_p ? "ld\t%0,%1" : "lw\t%0,%1");

      if (src_code == CONST_INT)
	{
	  /* Don't use the X format, because that will give out of
	     range numbers for 64 bit hosts and 32 bit targets.  */
	  if (!TARGET_MIPS16)
	    return "li\t%0,%1\t\t\t# %X1";

	  if (INTVAL (src) >= 0 && INTVAL (src) <= 0xffff)
	    return "li\t%0,%1";

	  if (INTVAL (src) < 0 && INTVAL (src) >= -0xffff)
	    return "#";
	}

      if (src_code == HIGH)
	return "lui\t%0,%h1";

      if (CONST_GP_P (src))
	return "move\t%0,%1";

      if (symbolic_operand (src, VOIDmode))
	return (dbl_p ? "dla\t%0,%1" : "la\t%0,%1");
    }
  if (src_code == REG && FP_REG_P (REGNO (src)))
    {
      if (dest_code == REG && FP_REG_P (REGNO (dest)))
	{
	  if (GET_MODE (dest) == V2SFmode)
	    return "mov.ps\t%0,%1";
	  else
	    return (dbl_p ? "mov.d\t%0,%1" : "mov.s\t%0,%1");
	}

      if (dest_code == MEM)
	return (dbl_p ? "sdc1\t%1,%0" : "swc1\t%1,%0");
    }
  if (dest_code == REG && FP_REG_P (REGNO (dest)))
    {
      if (src_code == MEM)
	return (dbl_p ? "ldc1\t%0,%1" : "lwc1\t%0,%1");
    }
  if (dest_code == REG && ALL_COP_REG_P (REGNO (dest)) && src_code == MEM)
    {
      static char retval[] = "l_c_\t%0,%1";

      retval[1] = (dbl_p ? 'd' : 'w');
      retval[3] = COPNUM_AS_CHAR_FROM_REGNUM (REGNO (dest));
      return retval;
    }
  if (dest_code == MEM && src_code == REG && ALL_COP_REG_P (REGNO (src)))
    {
      static char retval[] = "s_c_\t%1,%0";

      retval[1] = (dbl_p ? 'd' : 'w');
      retval[3] = COPNUM_AS_CHAR_FROM_REGNUM (REGNO (src));
      return retval;
    }
  gcc_unreachable ();
}

/* Restore $gp from its save slot.  Valid only when using o32 or
   o64 abicalls.  */

void
mips_restore_gp (void)
{
  rtx address, slot;

  gcc_assert (TARGET_ABICALLS && TARGET_OLDABI);

  address = mips_add_offset (pic_offset_table_rtx,
			     frame_pointer_needed
			     ? hard_frame_pointer_rtx
			     : stack_pointer_rtx,
			     current_function_outgoing_args_size);
  slot = gen_rtx_MEM (Pmode, address);

  emit_move_insn (pic_offset_table_rtx, slot);
  if (!TARGET_EXPLICIT_RELOCS)
    emit_insn (gen_blockage ());
}

/* Emit an instruction of the form (set TARGET (CODE OP0 OP1)).  */

static void
mips_emit_binary (enum rtx_code code, rtx target, rtx op0, rtx op1)
{
  emit_insn (gen_rtx_SET (VOIDmode, target,
			  gen_rtx_fmt_ee (code, GET_MODE (target), op0, op1)));
}

/* Return true if CMP1 is a suitable second operand for relational
   operator CODE.  See also the *sCC patterns in mips.md.  */

static bool
mips_relational_operand_ok_p (enum rtx_code code, rtx cmp1)
{
  switch (code)
    {
    case GT:
    case GTU:
      return reg_or_0_operand (cmp1, VOIDmode);

    case GE:
    case GEU:
      return !TARGET_MIPS16 && cmp1 == const1_rtx;

    case LT:
    case LTU:
      return arith_operand (cmp1, VOIDmode);

    case LE:
      return sle_operand (cmp1, VOIDmode);

    case LEU:
      return sleu_operand (cmp1, VOIDmode);

    default:
      gcc_unreachable ();
    }
}

/* Canonicalize LE or LEU comparisons into LT comparisons when
   possible to avoid extra instructions or inverting the
   comparison.  */

static bool
mips_canonicalize_comparison (enum rtx_code *code, rtx *cmp1, 
			      enum machine_mode mode)
{
  HOST_WIDE_INT original, plus_one;

  if (GET_CODE (*cmp1) != CONST_INT)
    return false;
  
  original = INTVAL (*cmp1);
  plus_one = trunc_int_for_mode ((unsigned HOST_WIDE_INT) original + 1, mode);
  
  switch (*code)
    {
    case LE:
      if (original < plus_one)
	{
	  *code = LT;
	  *cmp1 = force_reg (mode, GEN_INT (plus_one));
	  return true;
	}
      break;
      
    case LEU:
      if (plus_one != 0)
	{
	  *code = LTU;
	  *cmp1 = force_reg (mode, GEN_INT (plus_one));
	  return true;
	}
      break;
      
    default:
      return false;
   }
  
  return false;

}

/* Compare CMP0 and CMP1 using relational operator CODE and store the
   result in TARGET.  CMP0 and TARGET are register_operands that have
   the same integer mode.  If INVERT_PTR is nonnull, it's OK to set
   TARGET to the inverse of the result and flip *INVERT_PTR instead.  */

static void
mips_emit_int_relational (enum rtx_code code, bool *invert_ptr,
			  rtx target, rtx cmp0, rtx cmp1)
{
  /* First see if there is a MIPS instruction that can do this operation
     with CMP1 in its current form. If not, try to canonicalize the
     comparison to LT. If that fails, try doing the same for the
     inverse operation.  If that also fails, force CMP1 into a register
     and try again.  */
  if (mips_relational_operand_ok_p (code, cmp1))
    mips_emit_binary (code, target, cmp0, cmp1);
  else if (mips_canonicalize_comparison (&code, &cmp1, GET_MODE (target)))
    mips_emit_binary (code, target, cmp0, cmp1);
  else
    {
      enum rtx_code inv_code = reverse_condition (code);
      if (!mips_relational_operand_ok_p (inv_code, cmp1))
	{
	  cmp1 = force_reg (GET_MODE (cmp0), cmp1);
	  mips_emit_int_relational (code, invert_ptr, target, cmp0, cmp1);
	}
      else if (invert_ptr == 0)
	{
	  rtx inv_target = gen_reg_rtx (GET_MODE (target));
	  mips_emit_binary (inv_code, inv_target, cmp0, cmp1);
	  mips_emit_binary (XOR, target, inv_target, const1_rtx);
	}
      else
	{
	  *invert_ptr = !*invert_ptr;
	  mips_emit_binary (inv_code, target, cmp0, cmp1);
	}
    }
}

/* Return a register that is zero iff CMP0 and CMP1 are equal.
   The register will have the same mode as CMP0.  */

static rtx
mips_zero_if_equal (rtx cmp0, rtx cmp1)
{
  if (cmp1 == const0_rtx)
    return cmp0;

  if (uns_arith_operand (cmp1, VOIDmode))
    return expand_binop (GET_MODE (cmp0), xor_optab,
			 cmp0, cmp1, 0, 0, OPTAB_DIRECT);

  return expand_binop (GET_MODE (cmp0), sub_optab,
		       cmp0, cmp1, 0, 0, OPTAB_DIRECT);
}

/* Convert *CODE into a code that can be used in a floating-point
   scc instruction (c.<cond>.<fmt>).  Return true if the values of
   the condition code registers will be inverted, with 0 indicating
   that the condition holds.  */

static bool
mips_reverse_fp_cond_p (enum rtx_code *code)
{
  switch (*code)
    {
    case NE:
    case LTGT:
    case ORDERED:
      *code = reverse_condition_maybe_unordered (*code);
      return true;

    default:
      return false;
    }
}

/* Convert a comparison into something that can be used in a branch or
   conditional move.  cmp_operands[0] and cmp_operands[1] are the values
   being compared and *CODE is the code used to compare them.

   Update *CODE, *OP0 and *OP1 so that they describe the final comparison.
   If NEED_EQ_NE_P, then only EQ/NE comparisons against zero are possible,
   otherwise any standard branch condition can be used.  The standard branch
   conditions are:

      - EQ/NE between two registers.
      - any comparison between a register and zero.  */

static void
mips_emit_compare (enum rtx_code *code, rtx *op0, rtx *op1, bool need_eq_ne_p)
{
  if (GET_MODE_CLASS (GET_MODE (cmp_operands[0])) == MODE_INT)
    {
      if (!need_eq_ne_p && cmp_operands[1] == const0_rtx)
	{
	  *op0 = cmp_operands[0];
	  *op1 = cmp_operands[1];
	}
      else if (*code == EQ || *code == NE)
	{
	  if (need_eq_ne_p)
	    {
	      *op0 = mips_zero_if_equal (cmp_operands[0], cmp_operands[1]);
	      *op1 = const0_rtx;
	    }
	  else
	    {
	      *op0 = cmp_operands[0];
	      *op1 = force_reg (GET_MODE (*op0), cmp_operands[1]);
	    }
	}
      else
	{
	  /* The comparison needs a separate scc instruction.  Store the
	     result of the scc in *OP0 and compare it against zero.  */
	  bool invert = false;
	  *op0 = gen_reg_rtx (GET_MODE (cmp_operands[0]));
	  *op1 = const0_rtx;
	  mips_emit_int_relational (*code, &invert, *op0,
				    cmp_operands[0], cmp_operands[1]);
	  *code = (invert ? EQ : NE);
	}
    }
  else
    {
      enum rtx_code cmp_code;

      /* Floating-point tests use a separate c.cond.fmt comparison to
	 set a condition code register.  The branch or conditional move
	 will then compare that register against zero.

	 Set CMP_CODE to the code of the comparison instruction and
	 *CODE to the code that the branch or move should use.  */
      cmp_code = *code;
      *code = mips_reverse_fp_cond_p (&cmp_code) ? EQ : NE;
      *op0 = (ISA_HAS_8CC
	      ? gen_reg_rtx (CCmode)
	      : gen_rtx_REG (CCmode, FPSW_REGNUM));
      *op1 = const0_rtx;
      mips_emit_binary (cmp_code, *op0, cmp_operands[0], cmp_operands[1]);
    }
}

/* Try comparing cmp_operands[0] and cmp_operands[1] using rtl code CODE.
   Store the result in TARGET and return true if successful.

   On 64-bit targets, TARGET may be wider than cmp_operands[0].  */

bool
mips_emit_scc (enum rtx_code code, rtx target)
{
  if (GET_MODE_CLASS (GET_MODE (cmp_operands[0])) != MODE_INT)
    return false;

  target = gen_lowpart (GET_MODE (cmp_operands[0]), target);
  if (code == EQ || code == NE)
    {
      rtx zie = mips_zero_if_equal (cmp_operands[0], cmp_operands[1]);
      mips_emit_binary (code, target, zie, const0_rtx);
    }
  else
    mips_emit_int_relational (code, 0, target,
			      cmp_operands[0], cmp_operands[1]);
  return true;
}

/* Emit the common code for doing conditional branches.
   operand[0] is the label to jump to.
   The comparison operands are saved away by cmp{si,di,sf,df}.  */

void
gen_conditional_branch (rtx *operands, enum rtx_code code)
{
  rtx op0, op1, condition;

  mips_emit_compare (&code, &op0, &op1, TARGET_MIPS16);
  condition = gen_rtx_fmt_ee (code, VOIDmode, op0, op1);
  emit_jump_insn (gen_condjump (condition, operands[0]));
}

/* Implement:

   (set temp (COND:CCV2 CMP_OP0 CMP_OP1))
   (set DEST (unspec [TRUE_SRC FALSE_SRC temp] UNSPEC_MOVE_TF_PS))  */

void
mips_expand_vcondv2sf (rtx dest, rtx true_src, rtx false_src,
		       enum rtx_code cond, rtx cmp_op0, rtx cmp_op1)
{
  rtx cmp_result;
  bool reversed_p;

  reversed_p = mips_reverse_fp_cond_p (&cond);
  cmp_result = gen_reg_rtx (CCV2mode);
  emit_insn (gen_scc_ps (cmp_result,
			 gen_rtx_fmt_ee (cond, VOIDmode, cmp_op0, cmp_op1)));
  if (reversed_p)
    emit_insn (gen_mips_cond_move_tf_ps (dest, false_src, true_src,
					 cmp_result));
  else
    emit_insn (gen_mips_cond_move_tf_ps (dest, true_src, false_src,
					 cmp_result));
}

/* Emit the common code for conditional moves.  OPERANDS is the array
   of operands passed to the conditional move define_expand.  */

void
gen_conditional_move (rtx *operands)
{
  enum rtx_code code;
  rtx op0, op1;

  code = GET_CODE (operands[1]);
  mips_emit_compare (&code, &op0, &op1, true);
  emit_insn (gen_rtx_SET (VOIDmode, operands[0],
			  gen_rtx_IF_THEN_ELSE (GET_MODE (operands[0]),
						gen_rtx_fmt_ee (code,
								GET_MODE (op0),
								op0, op1),
						operands[2], operands[3])));
}

/* Emit a conditional trap.  OPERANDS is the array of operands passed to
   the conditional_trap expander.  */

void
mips_gen_conditional_trap (rtx *operands)
{
  rtx op0, op1;
  enum rtx_code cmp_code = GET_CODE (operands[0]);
  enum machine_mode mode = GET_MODE (cmp_operands[0]);

  /* MIPS conditional trap machine instructions don't have GT or LE
     flavors, so we must invert the comparison and convert to LT and
     GE, respectively.  */
  switch (cmp_code)
    {
    case GT: cmp_code = LT; break;
    case LE: cmp_code = GE; break;
    case GTU: cmp_code = LTU; break;
    case LEU: cmp_code = GEU; break;
    default: break;
    }
  if (cmp_code == GET_CODE (operands[0]))
    {
      op0 = cmp_operands[0];
      op1 = cmp_operands[1];
    }
  else
    {
      op0 = cmp_operands[1];
      op1 = cmp_operands[0];
    }
  op0 = force_reg (mode, op0);
  if (!arith_operand (op1, mode))
    op1 = force_reg (mode, op1);

  emit_insn (gen_rtx_TRAP_IF (VOIDmode,
			      gen_rtx_fmt_ee (cmp_code, mode, op0, op1),
			      operands[1]));
}

/* Load function address ADDR into register DEST.  SIBCALL_P is true
   if the address is needed for a sibling call.  */

static void
mips_load_call_address (rtx dest, rtx addr, int sibcall_p)
{
  /* If we're generating PIC, and this call is to a global function,
     try to allow its address to be resolved lazily.  This isn't
     possible for NewABI sibcalls since the value of $gp on entry
     to the stub would be our caller's gp, not ours.  */
  if (TARGET_EXPLICIT_RELOCS
      && !(sibcall_p && TARGET_NEWABI)
      && global_got_operand (addr, VOIDmode))
    {
      rtx high, lo_sum_symbol;

      high = mips_unspec_offset_high (dest, pic_offset_table_rtx,
				      addr, SYMBOL_GOTOFF_CALL);
      lo_sum_symbol = mips_unspec_address (addr, SYMBOL_GOTOFF_CALL);
      if (Pmode == SImode)
	emit_insn (gen_load_callsi (dest, high, lo_sum_symbol));
      else
	emit_insn (gen_load_calldi (dest, high, lo_sum_symbol));
    }
  else
    emit_move_insn (dest, addr);
}


/* Expand a call or call_value instruction.  RESULT is where the
   result will go (null for calls), ADDR is the address of the
   function, ARGS_SIZE is the size of the arguments and AUX is
   the value passed to us by mips_function_arg.  SIBCALL_P is true
   if we are expanding a sibling call, false if we're expanding
   a normal call.  */

void
mips_expand_call (rtx result, rtx addr, rtx args_size, rtx aux, int sibcall_p)
{
  rtx orig_addr, pattern, insn;

  orig_addr = addr;
  if (!call_insn_operand (addr, VOIDmode))
    {
      addr = gen_reg_rtx (Pmode);
      mips_load_call_address (addr, orig_addr, sibcall_p);
    }

  if (TARGET_MIPS16
      && mips16_hard_float
      && build_mips16_call_stub (result, addr, args_size,
				 aux == 0 ? 0 : (int) GET_MODE (aux)))
    return;

  if (result == 0)
    pattern = (sibcall_p
	       ? gen_sibcall_internal (addr, args_size)
	       : gen_call_internal (addr, args_size));
  else if (GET_CODE (result) == PARALLEL && XVECLEN (result, 0) == 2)
    {
      rtx reg1, reg2;

      reg1 = XEXP (XVECEXP (result, 0, 0), 0);
      reg2 = XEXP (XVECEXP (result, 0, 1), 0);
      pattern =
	(sibcall_p
	 ? gen_sibcall_value_multiple_internal (reg1, addr, args_size, reg2)
	 : gen_call_value_multiple_internal (reg1, addr, args_size, reg2));
    }
  else
    pattern = (sibcall_p
	       ? gen_sibcall_value_internal (result, addr, args_size)
	       : gen_call_value_internal (result, addr, args_size));

  insn = emit_call_insn (pattern);

  /* Lazy-binding stubs require $gp to be valid on entry.  */
  if (global_got_operand (orig_addr, VOIDmode))
    use_reg (&CALL_INSN_FUNCTION_USAGE (insn), pic_offset_table_rtx);
}


/* We can handle any sibcall when TARGET_SIBCALLS is true.  */

static bool
mips_function_ok_for_sibcall (tree decl ATTRIBUTE_UNUSED,
			      tree exp ATTRIBUTE_UNUSED)
{
  return TARGET_SIBCALLS;
}

/* Emit code to move general operand SRC into condition-code
   register DEST.  SCRATCH is a scratch TFmode float register.
   The sequence is:

	FP1 = SRC
	FP2 = 0.0f
	DEST = FP2 < FP1

   where FP1 and FP2 are single-precision float registers
   taken from SCRATCH.  */

void
mips_emit_fcc_reload (rtx dest, rtx src, rtx scratch)
{
  rtx fp1, fp2;

  /* Change the source to SFmode.  */
  if (MEM_P (src))
    src = adjust_address (src, SFmode, 0);
  else if (REG_P (src) || GET_CODE (src) == SUBREG)
    src = gen_rtx_REG (SFmode, true_regnum (src));

  fp1 = gen_rtx_REG (SFmode, REGNO (scratch));
  fp2 = gen_rtx_REG (SFmode, REGNO (scratch) + FP_INC);

  emit_move_insn (copy_rtx (fp1), src);
  emit_move_insn (copy_rtx (fp2), CONST0_RTX (SFmode));
  emit_insn (gen_slt_sf (dest, fp2, fp1));
}

/* Emit code to change the current function's return address to
   ADDRESS.  SCRATCH is available as a scratch register, if needed.
   ADDRESS and SCRATCH are both word-mode GPRs.  */

void
mips_set_return_address (rtx address, rtx scratch)
{
  rtx slot_address;

  compute_frame_size (get_frame_size ());
  gcc_assert ((cfun->machine->frame.mask >> 31) & 1);
  slot_address = mips_add_offset (scratch, stack_pointer_rtx,
				  cfun->machine->frame.gp_sp_offset);

  emit_move_insn (gen_rtx_MEM (GET_MODE (address), slot_address), address);
}

/* Emit straight-line code to move LENGTH bytes from SRC to DEST.
   Assume that the areas do not overlap.  */

static void
mips_block_move_straight (rtx dest, rtx src, HOST_WIDE_INT length)
{
  HOST_WIDE_INT offset, delta;
  unsigned HOST_WIDE_INT bits;
  int i;
  enum machine_mode mode;
  rtx *regs;

  /* Work out how many bits to move at a time.  If both operands have
     half-word alignment, it is usually better to move in half words.
     For instance, lh/lh/sh/sh is usually better than lwl/lwr/swl/swr
     and lw/lw/sw/sw is usually better than ldl/ldr/sdl/sdr.
     Otherwise move word-sized chunks.  */
  if (MEM_ALIGN (src) == BITS_PER_WORD / 2
      && MEM_ALIGN (dest) == BITS_PER_WORD / 2)
    bits = BITS_PER_WORD / 2;
  else
    bits = BITS_PER_WORD;

  mode = mode_for_size (bits, MODE_INT, 0);
  delta = bits / BITS_PER_UNIT;

  /* Allocate a buffer for the temporary registers.  */
  regs = alloca (sizeof (rtx) * length / delta);

  /* Load as many BITS-sized chunks as possible.  Use a normal load if
     the source has enough alignment, otherwise use left/right pairs.  */
  for (offset = 0, i = 0; offset + delta <= length; offset += delta, i++)
    {
      regs[i] = gen_reg_rtx (mode);
      if (MEM_ALIGN (src) >= bits)
	emit_move_insn (regs[i], adjust_address (src, mode, offset));
      else
	{
	  rtx part = adjust_address (src, BLKmode, offset);
	  if (!mips_expand_unaligned_load (regs[i], part, bits, 0))
	    gcc_unreachable ();
	}
    }

  /* Copy the chunks to the destination.  */
  for (offset = 0, i = 0; offset + delta <= length; offset += delta, i++)
    if (MEM_ALIGN (dest) >= bits)
      emit_move_insn (adjust_address (dest, mode, offset), regs[i]);
    else
      {
	rtx part = adjust_address (dest, BLKmode, offset);
	if (!mips_expand_unaligned_store (part, regs[i], bits, 0))
	  gcc_unreachable ();
      }

  /* Mop up any left-over bytes.  */
  if (offset < length)
    {
      src = adjust_address (src, BLKmode, offset);
      dest = adjust_address (dest, BLKmode, offset);
      move_by_pieces (dest, src, length - offset,
		      MIN (MEM_ALIGN (src), MEM_ALIGN (dest)), 0);
    }
}

#define MAX_MOVE_REGS 4
#define MAX_MOVE_BYTES (MAX_MOVE_REGS * UNITS_PER_WORD)


/* Helper function for doing a loop-based block operation on memory
   reference MEM.  Each iteration of the loop will operate on LENGTH
   bytes of MEM.

   Create a new base register for use within the loop and point it to
   the start of MEM.  Create a new memory reference that uses this
   register.  Store them in *LOOP_REG and *LOOP_MEM respectively.  */

static void
mips_adjust_block_mem (rtx mem, HOST_WIDE_INT length,
		       rtx *loop_reg, rtx *loop_mem)
{
  *loop_reg = copy_addr_to_reg (XEXP (mem, 0));

  /* Although the new mem does not refer to a known location,
     it does keep up to LENGTH bytes of alignment.  */
  *loop_mem = change_address (mem, BLKmode, *loop_reg);
  set_mem_align (*loop_mem, MIN (MEM_ALIGN (mem), length * BITS_PER_UNIT));
}


/* Move LENGTH bytes from SRC to DEST using a loop that moves MAX_MOVE_BYTES
   per iteration.  LENGTH must be at least MAX_MOVE_BYTES.  Assume that the
   memory regions do not overlap.  */

static void
mips_block_move_loop (rtx dest, rtx src, HOST_WIDE_INT length)
{
  rtx label, src_reg, dest_reg, final_src;
  HOST_WIDE_INT leftover;

  leftover = length % MAX_MOVE_BYTES;
  length -= leftover;

  /* Create registers and memory references for use within the loop.  */
  mips_adjust_block_mem (src, MAX_MOVE_BYTES, &src_reg, &src);
  mips_adjust_block_mem (dest, MAX_MOVE_BYTES, &dest_reg, &dest);

  /* Calculate the value that SRC_REG should have after the last iteration
     of the loop.  */
  final_src = expand_simple_binop (Pmode, PLUS, src_reg, GEN_INT (length),
				   0, 0, OPTAB_WIDEN);

  /* Emit the start of the loop.  */
  label = gen_label_rtx ();
  emit_label (label);

  /* Emit the loop body.  */
  mips_block_move_straight (dest, src, MAX_MOVE_BYTES);

  /* Move on to the next block.  */
  emit_move_insn (src_reg, plus_constant (src_reg, MAX_MOVE_BYTES));
  emit_move_insn (dest_reg, plus_constant (dest_reg, MAX_MOVE_BYTES));

  /* Emit the loop condition.  */
  if (Pmode == DImode)
    emit_insn (gen_cmpdi (src_reg, final_src));
  else
    emit_insn (gen_cmpsi (src_reg, final_src));
  emit_jump_insn (gen_bne (label));

  /* Mop up any left-over bytes.  */
  if (leftover)
    mips_block_move_straight (dest, src, leftover);
}

/* Expand a movmemsi instruction.  */

bool
mips_expand_block_move (rtx dest, rtx src, rtx length)
{
  if (GET_CODE (length) == CONST_INT)
    {
      if (INTVAL (length) <= 2 * MAX_MOVE_BYTES)
	{
	  mips_block_move_straight (dest, src, INTVAL (length));
	  return true;
	}
      else if (optimize)
	{
	  mips_block_move_loop (dest, src, INTVAL (length));
	  return true;
	}
    }
  return false;
}

/* Argument support functions.  */

/* Initialize CUMULATIVE_ARGS for a function.  */

void
init_cumulative_args (CUMULATIVE_ARGS *cum, tree fntype,
		      rtx libname ATTRIBUTE_UNUSED)
{
  static CUMULATIVE_ARGS zero_cum;
  tree param, next_param;

  *cum = zero_cum;
  cum->prototype = (fntype && TYPE_ARG_TYPES (fntype));

  /* Determine if this function has variable arguments.  This is
     indicated by the last argument being 'void_type_mode' if there
     are no variable arguments.  The standard MIPS calling sequence
     passes all arguments in the general purpose registers in this case.  */

  for (param = fntype ? TYPE_ARG_TYPES (fntype) : 0;
       param != 0; param = next_param)
    {
      next_param = TREE_CHAIN (param);
      if (next_param == 0 && TREE_VALUE (param) != void_type_node)
	cum->gp_reg_found = 1;
    }
}


/* Fill INFO with information about a single argument.  CUM is the
   cumulative state for earlier arguments.  MODE is the mode of this
   argument and TYPE is its type (if known).  NAMED is true if this
   is a named (fixed) argument rather than a variable one.  */

static void
mips_arg_info (const CUMULATIVE_ARGS *cum, enum machine_mode mode,
	       tree type, int named, struct mips_arg_info *info)
{
  bool doubleword_aligned_p;
  unsigned int num_bytes, num_words, max_regs;

  /* Work out the size of the argument.  */
  num_bytes = type ? int_size_in_bytes (type) : GET_MODE_SIZE (mode);
  num_words = (num_bytes + UNITS_PER_WORD - 1) / UNITS_PER_WORD;

  /* Decide whether it should go in a floating-point register, assuming
     one is free.  Later code checks for availability.

     The checks against UNITS_PER_FPVALUE handle the soft-float and
     single-float cases.  */
  switch (mips_abi)
    {
    case ABI_EABI:
      /* The EABI conventions have traditionally been defined in terms
	 of TYPE_MODE, regardless of the actual type.  */
      info->fpr_p = ((GET_MODE_CLASS (mode) == MODE_FLOAT
		      || GET_MODE_CLASS (mode) == MODE_VECTOR_FLOAT)
		     && GET_MODE_SIZE (mode) <= UNITS_PER_FPVALUE);
      break;

    case ABI_32:
    case ABI_O64:
      /* Only leading floating-point scalars are passed in
	 floating-point registers.  We also handle vector floats the same
	 say, which is OK because they are not covered by the standard ABI.  */
      info->fpr_p = (!cum->gp_reg_found
		     && cum->arg_number < 2
		     && (type == 0 || SCALAR_FLOAT_TYPE_P (type)
			 || VECTOR_FLOAT_TYPE_P (type))
		     && (GET_MODE_CLASS (mode) == MODE_FLOAT
			 || GET_MODE_CLASS (mode) == MODE_VECTOR_FLOAT)
		     && GET_MODE_SIZE (mode) <= UNITS_PER_FPVALUE);
      break;

    case ABI_N32:
    case ABI_64:
      /* Scalar and complex floating-point types are passed in
	 floating-point registers.  */
      info->fpr_p = (named
		     && (type == 0 || FLOAT_TYPE_P (type))
		     && (GET_MODE_CLASS (mode) == MODE_FLOAT
			 || GET_MODE_CLASS (mode) == MODE_COMPLEX_FLOAT
			 || GET_MODE_CLASS (mode) == MODE_VECTOR_FLOAT)
		     && GET_MODE_UNIT_SIZE (mode) <= UNITS_PER_FPVALUE);

      /* ??? According to the ABI documentation, the real and imaginary
	 parts of complex floats should be passed in individual registers.
	 The real and imaginary parts of stack arguments are supposed
	 to be contiguous and there should be an extra word of padding
	 at the end.

	 This has two problems.  First, it makes it impossible to use a
	 single "void *" va_list type, since register and stack arguments
	 are passed differently.  (At the time of writing, MIPSpro cannot
	 handle complex float varargs correctly.)  Second, it's unclear
	 what should happen when there is only one register free.

	 For now, we assume that named complex floats should go into FPRs
	 if there are two FPRs free, otherwise they should be passed in the
	 same way as a struct containing two floats.  */
      if (info->fpr_p
	  && GET_MODE_CLASS (mode) == MODE_COMPLEX_FLOAT
	  && GET_MODE_UNIT_SIZE (mode) < UNITS_PER_FPVALUE)
	{
	  if (cum->num_gprs >= MAX_ARGS_IN_REGISTERS - 1)
	    info->fpr_p = false;
	  else
	    num_words = 2;
	}
      break;

    default:
      gcc_unreachable ();
    }

  /* See whether the argument has doubleword alignment.  */
  doubleword_aligned_p = FUNCTION_ARG_BOUNDARY (mode, type) > BITS_PER_WORD;

  /* Set REG_OFFSET to the register count we're interested in.
     The EABI allocates the floating-point registers separately,
     but the other ABIs allocate them like integer registers.  */
  info->reg_offset = (mips_abi == ABI_EABI && info->fpr_p
		      ? cum->num_fprs
		      : cum->num_gprs);

  /* Advance to an even register if the argument is doubleword-aligned.  */
  if (doubleword_aligned_p)
    info->reg_offset += info->reg_offset & 1;

  /* Work out the offset of a stack argument.  */
  info->stack_offset = cum->stack_words;
  if (doubleword_aligned_p)
    info->stack_offset += info->stack_offset & 1;

  max_regs = MAX_ARGS_IN_REGISTERS - info->reg_offset;

  /* Partition the argument between registers and stack.  */
  info->reg_words = MIN (num_words, max_regs);
  info->stack_words = num_words - info->reg_words;
}


/* Implement FUNCTION_ARG_ADVANCE.  */

void
function_arg_advance (CUMULATIVE_ARGS *cum, enum machine_mode mode,
		      tree type, int named)
{
  struct mips_arg_info info;

  mips_arg_info (cum, mode, type, named, &info);

  if (!info.fpr_p)
    cum->gp_reg_found = true;

  /* See the comment above the cumulative args structure in mips.h
     for an explanation of what this code does.  It assumes the O32
     ABI, which passes at most 2 arguments in float registers.  */
  if (cum->arg_number < 2 && info.fpr_p)
    cum->fp_code += (mode == SFmode ? 1 : 2) << ((cum->arg_number - 1) * 2);

  if (mips_abi != ABI_EABI || !info.fpr_p)
    cum->num_gprs = info.reg_offset + info.reg_words;
  else if (info.reg_words > 0)
    cum->num_fprs += FP_INC;

  if (info.stack_words > 0)
    cum->stack_words = info.stack_offset + info.stack_words;

  cum->arg_number++;
}

/* Implement FUNCTION_ARG.  */

struct rtx_def *
function_arg (const CUMULATIVE_ARGS *cum, enum machine_mode mode,
	      tree type, int named)
{
  struct mips_arg_info info;

  /* We will be called with a mode of VOIDmode after the last argument
     has been seen.  Whatever we return will be passed to the call
     insn.  If we need a mips16 fp_code, return a REG with the code
     stored as the mode.  */
  if (mode == VOIDmode)
    {
      if (TARGET_MIPS16 && cum->fp_code != 0)
	return gen_rtx_REG ((enum machine_mode) cum->fp_code, 0);

      else
	return 0;
    }

  mips_arg_info (cum, mode, type, named, &info);

  /* Return straight away if the whole argument is passed on the stack.  */
  if (info.reg_offset == MAX_ARGS_IN_REGISTERS)
    return 0;

  if (type != 0
      && TREE_CODE (type) == RECORD_TYPE
      && TARGET_NEWABI
      && TYPE_SIZE_UNIT (type)
      && host_integerp (TYPE_SIZE_UNIT (type), 1)
      && named)
    {
      /* The Irix 6 n32/n64 ABIs say that if any 64 bit chunk of the
	 structure contains a double in its entirety, then that 64 bit
	 chunk is passed in a floating point register.  */
      tree field;

      /* First check to see if there is any such field.  */
      for (field = TYPE_FIELDS (type); field; field = TREE_CHAIN (field))
	if (TREE_CODE (field) == FIELD_DECL
	    && TREE_CODE (TREE_TYPE (field)) == REAL_TYPE
	    && TYPE_PRECISION (TREE_TYPE (field)) == BITS_PER_WORD
	    && host_integerp (bit_position (field), 0)
	    && int_bit_position (field) % BITS_PER_WORD == 0)
	  break;

      if (field != 0)
	{
	  /* Now handle the special case by returning a PARALLEL
	     indicating where each 64 bit chunk goes.  INFO.REG_WORDS
	     chunks are passed in registers.  */
	  unsigned int i;
	  HOST_WIDE_INT bitpos;
	  rtx ret;

	  /* assign_parms checks the mode of ENTRY_PARM, so we must
	     use the actual mode here.  */
	  ret = gen_rtx_PARALLEL (mode, rtvec_alloc (info.reg_words));

	  bitpos = 0;
	  field = TYPE_FIELDS (type);
	  for (i = 0; i < info.reg_words; i++)
	    {
	      rtx reg;

	      for (; field; field = TREE_CHAIN (field))
		if (TREE_CODE (field) == FIELD_DECL
		    && int_bit_position (field) >= bitpos)
		  break;

	      if (field
		  && int_bit_position (field) == bitpos
		  && TREE_CODE (TREE_TYPE (field)) == REAL_TYPE
		  && !TARGET_SOFT_FLOAT
		  && TYPE_PRECISION (TREE_TYPE (field)) == BITS_PER_WORD)
		reg = gen_rtx_REG (DFmode, FP_ARG_FIRST + info.reg_offset + i);
	      else
		reg = gen_rtx_REG (DImode, GP_ARG_FIRST + info.reg_offset + i);

	      XVECEXP (ret, 0, i)
		= gen_rtx_EXPR_LIST (VOIDmode, reg,
				     GEN_INT (bitpos / BITS_PER_UNIT));

	      bitpos += BITS_PER_WORD;
	    }
	  return ret;
	}
    }

  /* Handle the n32/n64 conventions for passing complex floating-point
     arguments in FPR pairs.  The real part goes in the lower register
     and the imaginary part goes in the upper register.  */
  if (TARGET_NEWABI
      && info.fpr_p
      && GET_MODE_CLASS (mode) == MODE_COMPLEX_FLOAT)
    {
      rtx real, imag;
      enum machine_mode inner;
      int reg;

      inner = GET_MODE_INNER (mode);
      reg = FP_ARG_FIRST + info.reg_offset;
      if (info.reg_words * UNITS_PER_WORD == GET_MODE_SIZE (inner))
	{
	  /* Real part in registers, imaginary part on stack.  */
	  gcc_assert (info.stack_words == info.reg_words);
	  return gen_rtx_REG (inner, reg);
	}
      else
	{
	  gcc_assert (info.stack_words == 0);
	  real = gen_rtx_EXPR_LIST (VOIDmode,
				    gen_rtx_REG (inner, reg),
				    const0_rtx);
	  imag = gen_rtx_EXPR_LIST (VOIDmode,
				    gen_rtx_REG (inner,
						 reg + info.reg_words / 2),
				    GEN_INT (GET_MODE_SIZE (inner)));
	  return gen_rtx_PARALLEL (mode, gen_rtvec (2, real, imag));
	}
    }

  if (!info.fpr_p)
    return gen_rtx_REG (mode, GP_ARG_FIRST + info.reg_offset);
  else if (info.reg_offset == 1)
    /* This code handles the special o32 case in which the second word
       of the argument structure is passed in floating-point registers.  */
    return gen_rtx_REG (mode, FP_ARG_FIRST + FP_INC);
  else
    return gen_rtx_REG (mode, FP_ARG_FIRST + info.reg_offset);
}


/* Implement TARGET_ARG_PARTIAL_BYTES.  */

static int
mips_arg_partial_bytes (CUMULATIVE_ARGS *cum,
			enum machine_mode mode, tree type, bool named)
{
  struct mips_arg_info info;

  mips_arg_info (cum, mode, type, named, &info);
  return info.stack_words > 0 ? info.reg_words * UNITS_PER_WORD : 0;
}


/* Implement FUNCTION_ARG_BOUNDARY.  Every parameter gets at least
   PARM_BOUNDARY bits of alignment, but will be given anything up
   to STACK_BOUNDARY bits if the type requires it.  */

int
function_arg_boundary (enum machine_mode mode, tree type)
{
  unsigned int alignment;

  alignment = type ? TYPE_ALIGN (type) : GET_MODE_ALIGNMENT (mode);
  if (alignment < PARM_BOUNDARY)
    alignment = PARM_BOUNDARY;
  if (alignment > STACK_BOUNDARY)
    alignment = STACK_BOUNDARY;
  return alignment;
}

/* Return true if FUNCTION_ARG_PADDING (MODE, TYPE) should return
   upward rather than downward.  In other words, return true if the
   first byte of the stack slot has useful data, false if the last
   byte does.  */

bool
mips_pad_arg_upward (enum machine_mode mode, tree type)
{
  /* On little-endian targets, the first byte of every stack argument
     is passed in the first byte of the stack slot.  */
  if (!BYTES_BIG_ENDIAN)
    return true;

  /* Otherwise, integral types are padded downward: the last byte of a
     stack argument is passed in the last byte of the stack slot.  */
  if (type != 0
      ? INTEGRAL_TYPE_P (type) || POINTER_TYPE_P (type)
      : GET_MODE_CLASS (mode) == MODE_INT)
    return false;

  /* Big-endian o64 pads floating-point arguments downward.  */
  if (mips_abi == ABI_O64)
    if (type != 0 ? FLOAT_TYPE_P (type) : GET_MODE_CLASS (mode) == MODE_FLOAT)
      return false;

  /* Other types are padded upward for o32, o64, n32 and n64.  */
  if (mips_abi != ABI_EABI)
    return true;

  /* Arguments smaller than a stack slot are padded downward.  */
  if (mode != BLKmode)
    return (GET_MODE_BITSIZE (mode) >= PARM_BOUNDARY);
  else
    return (int_size_in_bytes (type) >= (PARM_BOUNDARY / BITS_PER_UNIT));
}


/* Likewise BLOCK_REG_PADDING (MODE, TYPE, ...).  Return !BYTES_BIG_ENDIAN
   if the least significant byte of the register has useful data.  Return
   the opposite if the most significant byte does.  */

bool
mips_pad_reg_upward (enum machine_mode mode, tree type)
{
  /* No shifting is required for floating-point arguments.  */
  if (type != 0 ? FLOAT_TYPE_P (type) : GET_MODE_CLASS (mode) == MODE_FLOAT)
    return !BYTES_BIG_ENDIAN;

  /* Otherwise, apply the same padding to register arguments as we do
     to stack arguments.  */
  return mips_pad_arg_upward (mode, type);
}

static void
mips_setup_incoming_varargs (CUMULATIVE_ARGS *cum, enum machine_mode mode,
			     tree type, int *pretend_size ATTRIBUTE_UNUSED,
			     int no_rtl)
{
  CUMULATIVE_ARGS local_cum;
  int gp_saved, fp_saved;

  /* The caller has advanced CUM up to, but not beyond, the last named
     argument.  Advance a local copy of CUM past the last "real" named
     argument, to find out how many registers are left over.  */

  local_cum = *cum;
  FUNCTION_ARG_ADVANCE (local_cum, mode, type, 1);

  /* Found out how many registers we need to save.  */
  gp_saved = MAX_ARGS_IN_REGISTERS - local_cum.num_gprs;
  fp_saved = (EABI_FLOAT_VARARGS_P
	      ? MAX_ARGS_IN_REGISTERS - local_cum.num_fprs
	      : 0);

  if (!no_rtl)
    {
      if (gp_saved > 0)
	{
	  rtx ptr, mem;

	  ptr = plus_constant (virtual_incoming_args_rtx,
			       REG_PARM_STACK_SPACE (cfun->decl)
			       - gp_saved * UNITS_PER_WORD);
	  mem = gen_rtx_MEM (BLKmode, ptr);
	  set_mem_alias_set (mem, get_varargs_alias_set ());

	  move_block_from_reg (local_cum.num_gprs + GP_ARG_FIRST,
			       mem, gp_saved);
	}
      if (fp_saved > 0)
	{
	  /* We can't use move_block_from_reg, because it will use
	     the wrong mode.  */
	  enum machine_mode mode;
	  int off, i;

	  /* Set OFF to the offset from virtual_incoming_args_rtx of
	     the first float register.  The FP save area lies below
	     the integer one, and is aligned to UNITS_PER_FPVALUE bytes.  */
	  off = -gp_saved * UNITS_PER_WORD;
	  off &= ~(UNITS_PER_FPVALUE - 1);
	  off -= fp_saved * UNITS_PER_FPREG;

	  mode = TARGET_SINGLE_FLOAT ? SFmode : DFmode;

	  for (i = local_cum.num_fprs; i < MAX_ARGS_IN_REGISTERS; i += FP_INC)
	    {
	      rtx ptr, mem;

	      ptr = plus_constant (virtual_incoming_args_rtx, off);
	      mem = gen_rtx_MEM (mode, ptr);
	      set_mem_alias_set (mem, get_varargs_alias_set ());
	      emit_move_insn (mem, gen_rtx_REG (mode, FP_ARG_FIRST + i));
	      off += UNITS_PER_HWFPVALUE;
	    }
	}
    }
  if (REG_PARM_STACK_SPACE (cfun->decl) == 0)
    cfun->machine->varargs_size = (gp_saved * UNITS_PER_WORD
				   + fp_saved * UNITS_PER_FPREG);
}

/* Create the va_list data type.
   We keep 3 pointers, and two offsets.
   Two pointers are to the overflow area, which starts at the CFA.
     One of these is constant, for addressing into the GPR save area below it.
     The other is advanced up the stack through the overflow region.
   The third pointer is to the GPR save area.  Since the FPR save area
     is just below it, we can address FPR slots off this pointer.
   We also keep two one-byte offsets, which are to be subtracted from the
     constant pointers to yield addresses in the GPR and FPR save areas.
     These are downcounted as float or non-float arguments are used,
     and when they get to zero, the argument must be obtained from the
     overflow region.
   If !EABI_FLOAT_VARARGS_P, then no FPR save area exists, and a single
     pointer is enough.  It's started at the GPR save area, and is
     advanced, period.
   Note that the GPR save area is not constant size, due to optimization
     in the prologue.  Hence, we can't use a design with two pointers
     and two offsets, although we could have designed this with two pointers
     and three offsets.  */

static tree
mips_build_builtin_va_list (void)
{
  if (EABI_FLOAT_VARARGS_P)
    {
      tree f_ovfl, f_gtop, f_ftop, f_goff, f_foff, f_res, record;
      tree array, index;

      record = (*lang_hooks.types.make_type) (RECORD_TYPE);

      f_ovfl = build_decl (FIELD_DECL, get_identifier ("__overflow_argptr"),
			  ptr_type_node);
      f_gtop = build_decl (FIELD_DECL, get_identifier ("__gpr_top"),
			  ptr_type_node);
      f_ftop = build_decl (FIELD_DECL, get_identifier ("__fpr_top"),
			  ptr_type_node);
      f_goff = build_decl (FIELD_DECL, get_identifier ("__gpr_offset"),
			  unsigned_char_type_node);
      f_foff = build_decl (FIELD_DECL, get_identifier ("__fpr_offset"),
			  unsigned_char_type_node);
      /* Explicitly pad to the size of a pointer, so that -Wpadded won't
	 warn on every user file.  */
      index = build_int_cst (NULL_TREE, GET_MODE_SIZE (ptr_mode) - 2 - 1);
      array = build_array_type (unsigned_char_type_node,
			        build_index_type (index));
      f_res = build_decl (FIELD_DECL, get_identifier ("__reserved"), array);

      DECL_FIELD_CONTEXT (f_ovfl) = record;
      DECL_FIELD_CONTEXT (f_gtop) = record;
      DECL_FIELD_CONTEXT (f_ftop) = record;
      DECL_FIELD_CONTEXT (f_goff) = record;
      DECL_FIELD_CONTEXT (f_foff) = record;
      DECL_FIELD_CONTEXT (f_res) = record;

      TYPE_FIELDS (record) = f_ovfl;
      TREE_CHAIN (f_ovfl) = f_gtop;
      TREE_CHAIN (f_gtop) = f_ftop;
      TREE_CHAIN (f_ftop) = f_goff;
      TREE_CHAIN (f_goff) = f_foff;
      TREE_CHAIN (f_foff) = f_res;

      layout_type (record);
      return record;
    }
  else if (TARGET_IRIX && TARGET_IRIX6)
    /* On IRIX 6, this type is 'char *'.  */
    return build_pointer_type (char_type_node);
  else
    /* Otherwise, we use 'void *'.  */
    return ptr_type_node;
}

/* Implement va_start.  */

void
mips_va_start (tree valist, rtx nextarg)
{
  if (EABI_FLOAT_VARARGS_P)
    {
      const CUMULATIVE_ARGS *cum;
      tree f_ovfl, f_gtop, f_ftop, f_goff, f_foff;
      tree ovfl, gtop, ftop, goff, foff;
      tree t;
      int gpr_save_area_size;
      int fpr_save_area_size;
      int fpr_offset;

      cum = &current_function_args_info;
      gpr_save_area_size
	= (MAX_ARGS_IN_REGISTERS - cum->num_gprs) * UNITS_PER_WORD;
      fpr_save_area_size
	= (MAX_ARGS_IN_REGISTERS - cum->num_fprs) * UNITS_PER_FPREG;

      f_ovfl = TYPE_FIELDS (va_list_type_node);
      f_gtop = TREE_CHAIN (f_ovfl);
      f_ftop = TREE_CHAIN (f_gtop);
      f_goff = TREE_CHAIN (f_ftop);
      f_foff = TREE_CHAIN (f_goff);

      ovfl = build3 (COMPONENT_REF, TREE_TYPE (f_ovfl), valist, f_ovfl,
		     NULL_TREE);
      gtop = build3 (COMPONENT_REF, TREE_TYPE (f_gtop), valist, f_gtop,
		     NULL_TREE);
      ftop = build3 (COMPONENT_REF, TREE_TYPE (f_ftop), valist, f_ftop,
		     NULL_TREE);
      goff = build3 (COMPONENT_REF, TREE_TYPE (f_goff), valist, f_goff,
		     NULL_TREE);
      foff = build3 (COMPONENT_REF, TREE_TYPE (f_foff), valist, f_foff,
		     NULL_TREE);

      /* Emit code to initialize OVFL, which points to the next varargs
	 stack argument.  CUM->STACK_WORDS gives the number of stack
	 words used by named arguments.  */
      t = make_tree (TREE_TYPE (ovfl), virtual_incoming_args_rtx);
      if (cum->stack_words > 0)
	t = build2 (PLUS_EXPR, TREE_TYPE (ovfl), t,
		    build_int_cst (NULL_TREE,
				   cum->stack_words * UNITS_PER_WORD));
      t = build2 (MODIFY_EXPR, TREE_TYPE (ovfl), ovfl, t);
      expand_expr (t, const0_rtx, VOIDmode, EXPAND_NORMAL);

      /* Emit code to initialize GTOP, the top of the GPR save area.  */
      t = make_tree (TREE_TYPE (gtop), virtual_incoming_args_rtx);
      t = build2 (MODIFY_EXPR, TREE_TYPE (gtop), gtop, t);
      expand_expr (t, const0_rtx, VOIDmode, EXPAND_NORMAL);

      /* Emit code to initialize FTOP, the top of the FPR save area.
	 This address is gpr_save_area_bytes below GTOP, rounded
	 down to the next fp-aligned boundary.  */
      t = make_tree (TREE_TYPE (ftop), virtual_incoming_args_rtx);
      fpr_offset = gpr_save_area_size + UNITS_PER_FPVALUE - 1;
      fpr_offset &= ~(UNITS_PER_FPVALUE - 1);
      if (fpr_offset)
	t = build2 (PLUS_EXPR, TREE_TYPE (ftop), t,
		    build_int_cst (NULL_TREE, -fpr_offset));
      t = build2 (MODIFY_EXPR, TREE_TYPE (ftop), ftop, t);
      expand_expr (t, const0_rtx, VOIDmode, EXPAND_NORMAL);

      /* Emit code to initialize GOFF, the offset from GTOP of the
	 next GPR argument.  */
      t = build2 (MODIFY_EXPR, TREE_TYPE (goff), goff,
		  build_int_cst (NULL_TREE, gpr_save_area_size));
      expand_expr (t, const0_rtx, VOIDmode, EXPAND_NORMAL);

      /* Likewise emit code to initialize FOFF, the offset from FTOP
	 of the next FPR argument.  */
      t = build2 (MODIFY_EXPR, TREE_TYPE (foff), foff,
		  build_int_cst (NULL_TREE, fpr_save_area_size));
      expand_expr (t, const0_rtx, VOIDmode, EXPAND_NORMAL);
    }
  else
    {
      nextarg = plus_constant (nextarg, -cfun->machine->varargs_size);
      std_expand_builtin_va_start (valist, nextarg);
    }
}

/* Implement va_arg.  */

static tree
mips_gimplify_va_arg_expr (tree valist, tree type, tree *pre_p, tree *post_p)
{
  HOST_WIDE_INT size, rsize;
  tree addr;
  bool indirect;

  indirect = pass_by_reference (NULL, TYPE_MODE (type), type, 0);

  if (indirect)
    type = build_pointer_type (type);

  size = int_size_in_bytes (type);
  rsize = (size + UNITS_PER_WORD - 1) & -UNITS_PER_WORD;

  if (mips_abi != ABI_EABI || !EABI_FLOAT_VARARGS_P)
    addr = std_gimplify_va_arg_expr (valist, type, pre_p, post_p);
  else
    {
      /* Not a simple merged stack.	 */

      tree f_ovfl, f_gtop, f_ftop, f_goff, f_foff;
      tree ovfl, top, off, align;
      HOST_WIDE_INT osize;
      tree t, u;

      f_ovfl = TYPE_FIELDS (va_list_type_node);
      f_gtop = TREE_CHAIN (f_ovfl);
      f_ftop = TREE_CHAIN (f_gtop);
      f_goff = TREE_CHAIN (f_ftop);
      f_foff = TREE_CHAIN (f_goff);

      /* We maintain separate pointers and offsets for floating-point
	 and integer arguments, but we need similar code in both cases.
	 Let:

	 TOP be the top of the register save area;
	 OFF be the offset from TOP of the next register;
	 ADDR_RTX be the address of the argument;
	 RSIZE be the number of bytes used to store the argument
	 when it's in the register save area;
	 OSIZE be the number of bytes used to store it when it's
	 in the stack overflow area; and
	 PADDING be (BYTES_BIG_ENDIAN ? OSIZE - RSIZE : 0)

	 The code we want is:

	 1: off &= -rsize;	  // round down
	 2: if (off != 0)
	 3:   {
	 4:	 addr_rtx = top - off;
	 5:	 off -= rsize;
	 6:   }
	 7: else
	 8:   {
	 9:	 ovfl += ((intptr_t) ovfl + osize - 1) & -osize;
	 10:	 addr_rtx = ovfl + PADDING;
	 11:	 ovfl += osize;
	 14:   }

	 [1] and [9] can sometimes be optimized away.  */

      ovfl = build3 (COMPONENT_REF, TREE_TYPE (f_ovfl), valist, f_ovfl,
		     NULL_TREE);

      if (GET_MODE_CLASS (TYPE_MODE (type)) == MODE_FLOAT
	  && GET_MODE_SIZE (TYPE_MODE (type)) <= UNITS_PER_FPVALUE)
	{
	  top = build3 (COMPONENT_REF, TREE_TYPE (f_ftop), valist, f_ftop,
		        NULL_TREE);
	  off = build3 (COMPONENT_REF, TREE_TYPE (f_foff), valist, f_foff,
		        NULL_TREE);

	  /* When floating-point registers are saved to the stack,
	     each one will take up UNITS_PER_HWFPVALUE bytes, regardless
	     of the float's precision.  */
	  rsize = UNITS_PER_HWFPVALUE;

	  /* Overflow arguments are padded to UNITS_PER_WORD bytes
	     (= PARM_BOUNDARY bits).  This can be different from RSIZE
	     in two cases:

	     (1) On 32-bit targets when TYPE is a structure such as:

	     struct s { float f; };

	     Such structures are passed in paired FPRs, so RSIZE
	     will be 8 bytes.  However, the structure only takes
	     up 4 bytes of memory, so OSIZE will only be 4.

	     (2) In combinations such as -mgp64 -msingle-float
	     -fshort-double.  Doubles passed in registers
	     will then take up 4 (UNITS_PER_HWFPVALUE) bytes,
	     but those passed on the stack take up
	     UNITS_PER_WORD bytes.  */
	  osize = MAX (GET_MODE_SIZE (TYPE_MODE (type)), UNITS_PER_WORD);
	}
      else
	{
	  top = build3 (COMPONENT_REF, TREE_TYPE (f_gtop), valist, f_gtop,
		        NULL_TREE);
	  off = build3 (COMPONENT_REF, TREE_TYPE (f_goff), valist, f_goff,
		        NULL_TREE);
	  if (rsize > UNITS_PER_WORD)
	    {
	      /* [1] Emit code for: off &= -rsize.	*/
	      t = build2 (BIT_AND_EXPR, TREE_TYPE (off), off,
			  build_int_cst (NULL_TREE, -rsize));
	      t = build2 (MODIFY_EXPR, TREE_TYPE (off), off, t);
	      gimplify_and_add (t, pre_p);
	    }
	  osize = rsize;
	}

      /* [2] Emit code to branch if off == 0.  */
      t = build2 (NE_EXPR, boolean_type_node, off,
		  build_int_cst (TREE_TYPE (off), 0));
      addr = build3 (COND_EXPR, ptr_type_node, t, NULL_TREE, NULL_TREE);

      /* [5] Emit code for: off -= rsize.  We do this as a form of
	 post-increment not available to C.  Also widen for the
	 coming pointer arithmetic.  */
      t = fold_convert (TREE_TYPE (off), build_int_cst (NULL_TREE, rsize));
      t = build2 (POSTDECREMENT_EXPR, TREE_TYPE (off), off, t);
      t = fold_convert (sizetype, t);
      t = fold_convert (TREE_TYPE (top), t);

      /* [4] Emit code for: addr_rtx = top - off.  On big endian machines,
	 the argument has RSIZE - SIZE bytes of leading padding.  */
      t = build2 (MINUS_EXPR, TREE_TYPE (top), top, t);
      if (BYTES_BIG_ENDIAN && rsize > size)
	{
	  u = fold_convert (TREE_TYPE (t), build_int_cst (NULL_TREE,
							  rsize - size));
	  t = build2 (PLUS_EXPR, TREE_TYPE (t), t, u);
	}
      COND_EXPR_THEN (addr) = t;

      if (osize > UNITS_PER_WORD)
	{
	  /* [9] Emit: ovfl += ((intptr_t) ovfl + osize - 1) & -osize.  */
	  u = fold_convert (TREE_TYPE (ovfl),
			    build_int_cst (NULL_TREE, osize - 1));
	  t = build2 (PLUS_EXPR, TREE_TYPE (ovfl), ovfl, u);
	  u = fold_convert (TREE_TYPE (ovfl),
			    build_int_cst (NULL_TREE, -osize));
	  t = build2 (BIT_AND_EXPR, TREE_TYPE (ovfl), t, u);
	  align = build2 (MODIFY_EXPR, TREE_TYPE (ovfl), ovfl, t);
	}
      else
	align = NULL;

      /* [10, 11].	Emit code to store ovfl in addr_rtx, then
	 post-increment ovfl by osize.  On big-endian machines,
	 the argument has OSIZE - SIZE bytes of leading padding.  */
      u = fold_convert (TREE_TYPE (ovfl),
			build_int_cst (NULL_TREE, osize));
      t = build2 (POSTINCREMENT_EXPR, TREE_TYPE (ovfl), ovfl, u);
      if (BYTES_BIG_ENDIAN && osize > size)
	{
	  u = fold_convert (TREE_TYPE (t),
			    build_int_cst (NULL_TREE, osize - size));
	  t = build2 (PLUS_EXPR, TREE_TYPE (t), t, u);
	}

      /* String [9] and [10,11] together.  */
      if (align)
	t = build2 (COMPOUND_EXPR, TREE_TYPE (t), align, t);
      COND_EXPR_ELSE (addr) = t;

      addr = fold_convert (build_pointer_type (type), addr);
      addr = build_va_arg_indirect_ref (addr);
    }

  if (indirect)
    addr = build_va_arg_indirect_ref (addr);

  return addr;
}

/* Return true if it is possible to use left/right accesses for a
   bitfield of WIDTH bits starting BITPOS bits into *OP.  When
   returning true, update *OP, *LEFT and *RIGHT as follows:

   *OP is a BLKmode reference to the whole field.

   *LEFT is a QImode reference to the first byte if big endian or
   the last byte if little endian.  This address can be used in the
   left-side instructions (lwl, swl, ldl, sdl).

   *RIGHT is a QImode reference to the opposite end of the field and
   can be used in the patterning right-side instruction.  */

static bool
mips_get_unaligned_mem (rtx *op, unsigned int width, int bitpos,
			rtx *left, rtx *right)
{
  rtx first, last;

  /* Check that the operand really is a MEM.  Not all the extv and
     extzv predicates are checked.  */
  if (!MEM_P (*op))
    return false;

  /* Check that the size is valid.  */
  if (width != 32 && (!TARGET_64BIT || width != 64))
    return false;

  /* We can only access byte-aligned values.  Since we are always passed
     a reference to the first byte of the field, it is not necessary to
     do anything with BITPOS after this check.  */
  if (bitpos % BITS_PER_UNIT != 0)
    return false;

  /* Reject aligned bitfields: we want to use a normal load or store
     instead of a left/right pair.  */
  if (MEM_ALIGN (*op) >= width)
    return false;

  /* Adjust *OP to refer to the whole field.  This also has the effect
     of legitimizing *OP's address for BLKmode, possibly simplifying it.  */
  *op = adjust_address (*op, BLKmode, 0);
  set_mem_size (*op, GEN_INT (width / BITS_PER_UNIT));

  /* Get references to both ends of the field.  We deliberately don't
     use the original QImode *OP for FIRST since the new BLKmode one
     might have a simpler address.  */
  first = adjust_address (*op, QImode, 0);
  last = adjust_address (*op, QImode, width / BITS_PER_UNIT - 1);

  /* Allocate to LEFT and RIGHT according to endianness.  LEFT should
     be the upper word and RIGHT the lower word.  */
  if (TARGET_BIG_ENDIAN)
    *left = first, *right = last;
  else
    *left = last, *right = first;

  return true;
}


/* Try to emit the equivalent of (set DEST (zero_extract SRC WIDTH BITPOS)).
   Return true on success.  We only handle cases where zero_extract is
   equivalent to sign_extract.  */

bool
mips_expand_unaligned_load (rtx dest, rtx src, unsigned int width, int bitpos)
{
  rtx left, right, temp;

  /* If TARGET_64BIT, the destination of a 32-bit load will be a
     paradoxical word_mode subreg.  This is the only case in which
     we allow the destination to be larger than the source.  */
  if (GET_CODE (dest) == SUBREG
      && GET_MODE (dest) == DImode
      && SUBREG_BYTE (dest) == 0
      && GET_MODE (SUBREG_REG (dest)) == SImode)
    dest = SUBREG_REG (dest);

  /* After the above adjustment, the destination must be the same
     width as the source.  */
  if (GET_MODE_BITSIZE (GET_MODE (dest)) != width)
    return false;

  if (!mips_get_unaligned_mem (&src, width, bitpos, &left, &right))
    return false;

  temp = gen_reg_rtx (GET_MODE (dest));
  if (GET_MODE (dest) == DImode)
    {
      emit_insn (gen_mov_ldl (temp, src, left));
      emit_insn (gen_mov_ldr (dest, copy_rtx (src), right, temp));
    }
  else
    {
      emit_insn (gen_mov_lwl (temp, src, left));
      emit_insn (gen_mov_lwr (dest, copy_rtx (src), right, temp));
    }
  return true;
}


/* Try to expand (set (zero_extract DEST WIDTH BITPOS) SRC).  Return
   true on success.  */

bool
mips_expand_unaligned_store (rtx dest, rtx src, unsigned int width, int bitpos)
{
  rtx left, right;
  enum machine_mode mode;

  if (!mips_get_unaligned_mem (&dest, width, bitpos, &left, &right))
    return false;

  mode = mode_for_size (width, MODE_INT, 0);
  src = gen_lowpart (mode, src);

  if (mode == DImode)
    {
      emit_insn (gen_mov_sdl (dest, src, left));
      emit_insn (gen_mov_sdr (copy_rtx (dest), copy_rtx (src), right));
    }
  else
    {
      emit_insn (gen_mov_swl (dest, src, left));
      emit_insn (gen_mov_swr (copy_rtx (dest), copy_rtx (src), right));
    }
  return true;
}

/* Return true if X is a MEM with the same size as MODE.  */

bool
mips_mem_fits_mode_p (enum machine_mode mode, rtx x)
{
  rtx size;

  if (!MEM_P (x))
    return false;

  size = MEM_SIZE (x);
  return size && INTVAL (size) == GET_MODE_SIZE (mode);
}

/* Return true if (zero_extract OP SIZE POSITION) can be used as the
   source of an "ext" instruction or the destination of an "ins"
   instruction.  OP must be a register operand and the following
   conditions must hold:

     0 <= POSITION < GET_MODE_BITSIZE (GET_MODE (op))
     0 < SIZE <= GET_MODE_BITSIZE (GET_MODE (op))
     0 < POSITION + SIZE <= GET_MODE_BITSIZE (GET_MODE (op))

   Also reject lengths equal to a word as they are better handled
   by the move patterns.  */

bool
mips_use_ins_ext_p (rtx op, rtx size, rtx position)
{
  HOST_WIDE_INT len, pos;

  if (!ISA_HAS_EXT_INS
      || !register_operand (op, VOIDmode)
      || GET_MODE_BITSIZE (GET_MODE (op)) > BITS_PER_WORD)
    return false;

  len = INTVAL (size);
  pos = INTVAL (position);
  
  if (len <= 0 || len >= GET_MODE_BITSIZE (GET_MODE (op)) 
      || pos < 0 || pos + len > GET_MODE_BITSIZE (GET_MODE (op)))
    return false;

  return true;
}

/* Set up globals to generate code for the ISA or processor
   described by INFO.  */

static void
mips_set_architecture (const struct mips_cpu_info *info)
{
  if (info != 0)
    {
      mips_arch_info = info;
      mips_arch = info->cpu;
      mips_isa = info->isa;
    }
}


/* Likewise for tuning.  */

static void
mips_set_tune (const struct mips_cpu_info *info)
{
  if (info != 0)
    {
      mips_tune_info = info;
      mips_tune = info->cpu;
    }
}

/* Implement TARGET_HANDLE_OPTION.  */

static bool
mips_handle_option (size_t code, const char *arg, int value ATTRIBUTE_UNUSED)
{
  switch (code)
    {
    case OPT_mabi_:
      if (strcmp (arg, "32") == 0)
	mips_abi = ABI_32;
      else if (strcmp (arg, "o64") == 0)
	mips_abi = ABI_O64;
      else if (strcmp (arg, "n32") == 0)
	mips_abi = ABI_N32;
      else if (strcmp (arg, "64") == 0)
	mips_abi = ABI_64;
      else if (strcmp (arg, "eabi") == 0)
	mips_abi = ABI_EABI;
      else
	return false;
      return true;

    case OPT_march_:
    case OPT_mtune_:
      return mips_parse_cpu (arg) != 0;

    case OPT_mips:
      mips_isa_info = mips_parse_cpu (ACONCAT (("mips", arg, NULL)));
      return mips_isa_info != 0;

    case OPT_mno_flush_func:
      mips_cache_flush_func = NULL;
      return true;

    default:
      return true;
    }
}

/* Set up the threshold for data to go into the small data area, instead
   of the normal data area, and detect any conflicts in the switches.  */

void
override_options (void)
{
  int i, start, regno;
  enum machine_mode mode;

  mips_section_threshold = g_switch_set ? g_switch_value : MIPS_DEFAULT_GVALUE;

  /* The following code determines the architecture and register size.
     Similar code was added to GAS 2.14 (see tc-mips.c:md_after_parse_args()).
     The GAS and GCC code should be kept in sync as much as possible.  */

  if (mips_arch_string != 0)
    mips_set_architecture (mips_parse_cpu (mips_arch_string));

  if (mips_isa_info != 0)
    {
      if (mips_arch_info == 0)
	mips_set_architecture (mips_isa_info);
      else if (mips_arch_info->isa != mips_isa_info->isa)
	error ("-%s conflicts with the other architecture options, "
	       "which specify a %s processor",
	       mips_isa_info->name,
	       mips_cpu_info_from_isa (mips_arch_info->isa)->name);
    }

  if (mips_arch_info == 0)
    {
#ifdef MIPS_CPU_STRING_DEFAULT
      mips_set_architecture (mips_parse_cpu (MIPS_CPU_STRING_DEFAULT));
#else
      mips_set_architecture (mips_cpu_info_from_isa (MIPS_ISA_DEFAULT));
#endif
    }

  if (ABI_NEEDS_64BIT_REGS && !ISA_HAS_64BIT_REGS)
    error ("-march=%s is not compatible with the selected ABI",
	   mips_arch_info->name);

  /* Optimize for mips_arch, unless -mtune selects a different processor.  */
  if (mips_tune_string != 0)
    mips_set_tune (mips_parse_cpu (mips_tune_string));

  if (mips_tune_info == 0)
    mips_set_tune (mips_arch_info);

  /* Set cost structure for the processor.  */
  mips_cost = &mips_rtx_cost_data[mips_tune];

  if ((target_flags_explicit & MASK_64BIT) != 0)
    {
      /* The user specified the size of the integer registers.  Make sure
	 it agrees with the ABI and ISA.  */
      if (TARGET_64BIT && !ISA_HAS_64BIT_REGS)
	error ("-mgp64 used with a 32-bit processor");
      else if (!TARGET_64BIT && ABI_NEEDS_64BIT_REGS)
	error ("-mgp32 used with a 64-bit ABI");
      else if (TARGET_64BIT && ABI_NEEDS_32BIT_REGS)
	error ("-mgp64 used with a 32-bit ABI");
    }
  else
    {
      /* Infer the integer register size from the ABI and processor.
	 Restrict ourselves to 32-bit registers if that's all the
	 processor has, or if the ABI cannot handle 64-bit registers.  */
      if (ABI_NEEDS_32BIT_REGS || !ISA_HAS_64BIT_REGS)
	target_flags &= ~MASK_64BIT;
      else
	target_flags |= MASK_64BIT;
    }

  if ((target_flags_explicit & MASK_FLOAT64) != 0)
    {
      /* Really, -mfp32 and -mfp64 are ornamental options.  There's
	 only one right answer here.  */
      if (TARGET_64BIT && TARGET_DOUBLE_FLOAT && !TARGET_FLOAT64)
	error ("unsupported combination: %s", "-mgp64 -mfp32 -mdouble-float");
      else if (!TARGET_64BIT && TARGET_FLOAT64)
	error ("unsupported combination: %s", "-mgp32 -mfp64");
      else if (TARGET_SINGLE_FLOAT && TARGET_FLOAT64)
	error ("unsupported combination: %s", "-mfp64 -msingle-float");
    }
  else
    {
      /* -msingle-float selects 32-bit float registers.  Otherwise the
	 float registers should be the same size as the integer ones.  */
      if (TARGET_64BIT && TARGET_DOUBLE_FLOAT)
	target_flags |= MASK_FLOAT64;
      else
	target_flags &= ~MASK_FLOAT64;
    }

  /* End of code shared with GAS.  */

  if ((target_flags_explicit & MASK_LONG64) == 0)
    {
      if ((mips_abi == ABI_EABI && TARGET_64BIT) || mips_abi == ABI_64)
	target_flags |= MASK_LONG64;
      else
	target_flags &= ~MASK_LONG64;
    }

  if (MIPS_MARCH_CONTROLS_SOFT_FLOAT
      && (target_flags_explicit & MASK_SOFT_FLOAT) == 0)
    {
      /* For some configurations, it is useful to have -march control
	 the default setting of MASK_SOFT_FLOAT.  */
      switch ((int) mips_arch)
	{
	case PROCESSOR_R4100:
	case PROCESSOR_R4111:
	case PROCESSOR_R4120:
	case PROCESSOR_R4130:
	  target_flags |= MASK_SOFT_FLOAT;
	  break;

	default:
	  target_flags &= ~MASK_SOFT_FLOAT;
	  break;
	}
    }

  if (!TARGET_OLDABI)
    flag_pcc_struct_return = 0;

  if ((target_flags_explicit & MASK_BRANCHLIKELY) == 0)
    {
      /* If neither -mbranch-likely nor -mno-branch-likely was given
	 on the command line, set MASK_BRANCHLIKELY based on the target
	 architecture.

	 By default, we enable use of Branch Likely instructions on
	 all architectures which support them with the following
	 exceptions: when creating MIPS32 or MIPS64 code, and when
	 tuning for architectures where their use tends to hurt
	 performance.

	 The MIPS32 and MIPS64 architecture specifications say "Software
	 is strongly encouraged to avoid use of Branch Likely
	 instructions, as they will be removed from a future revision
	 of the [MIPS32 and MIPS64] architecture."  Therefore, we do not
	 issue those instructions unless instructed to do so by
	 -mbranch-likely.  */
      if (ISA_HAS_BRANCHLIKELY
	  && !(ISA_MIPS32 || ISA_MIPS32R2 || ISA_MIPS64)
	  && !(TUNE_MIPS5500 || TUNE_SB1))
	target_flags |= MASK_BRANCHLIKELY;
      else
	target_flags &= ~MASK_BRANCHLIKELY;
    }
  if (TARGET_BRANCHLIKELY && !ISA_HAS_BRANCHLIKELY)
    warning (0, "generation of Branch Likely instructions enabled, but not supported by architecture");

  /* The effect of -mabicalls isn't defined for the EABI.  */
  if (mips_abi == ABI_EABI && TARGET_ABICALLS)
    {
      error ("unsupported combination: %s", "-mabicalls -mabi=eabi");
      target_flags &= ~MASK_ABICALLS;
    }

  if (TARGET_ABICALLS)
    {
      /* We need to set flag_pic for executables as well as DSOs
	 because we may reference symbols that are not defined in
	 the final executable.  (MIPS does not use things like
	 copy relocs, for example.)

	 Also, there is a body of code that uses __PIC__ to distinguish
	 between -mabicalls and -mno-abicalls code.  */
      flag_pic = 1;
      if (mips_section_threshold > 0)
	warning (0, "%<-G%> is incompatible with %<-mabicalls%>");
    }

  /* mips_split_addresses is a half-way house between explicit
     relocations and the traditional assembler macros.  It can
     split absolute 32-bit symbolic constants into a high/lo_sum
     pair but uses macros for other sorts of access.

     Like explicit relocation support for REL targets, it relies
     on GNU extensions in the assembler and the linker.

     Although this code should work for -O0, it has traditionally
     been treated as an optimization.  */
  if (!TARGET_MIPS16 && TARGET_SPLIT_ADDRESSES
      && optimize && !flag_pic
      && !ABI_HAS_64BIT_SYMBOLS)
    mips_split_addresses = 1;
  else
    mips_split_addresses = 0;

  /* -mvr4130-align is a "speed over size" optimization: it usually produces
     faster code, but at the expense of more nops.  Enable it at -O3 and
     above.  */
  if (optimize > 2 && (target_flags_explicit & MASK_VR4130_ALIGN) == 0)
    target_flags |= MASK_VR4130_ALIGN;

  /* When compiling for the mips16, we cannot use floating point.  We
     record the original hard float value in mips16_hard_float.  */
  if (TARGET_MIPS16)
    {
      if (TARGET_SOFT_FLOAT)
	mips16_hard_float = 0;
      else
	mips16_hard_float = 1;
      target_flags |= MASK_SOFT_FLOAT;

      /* Don't run the scheduler before reload, since it tends to
         increase register pressure.  */
      flag_schedule_insns = 0;

      /* Don't do hot/cold partitioning.  The constant layout code expects
	 the whole function to be in a single section.  */
      flag_reorder_blocks_and_partition = 0;

      /* Silently disable -mexplicit-relocs since it doesn't apply
	 to mips16 code.  Even so, it would overly pedantic to warn
	 about "-mips16 -mexplicit-relocs", especially given that
	 we use a %gprel() operator.  */
      target_flags &= ~MASK_EXPLICIT_RELOCS;
    }

  /* When using explicit relocs, we call dbr_schedule from within
     mips_reorg.  */
  if (TARGET_EXPLICIT_RELOCS)
    {
      mips_flag_delayed_branch = flag_delayed_branch;
      flag_delayed_branch = 0;
    }

#ifdef MIPS_TFMODE_FORMAT
  REAL_MODE_FORMAT (TFmode) = &MIPS_TFMODE_FORMAT;
#endif

  /* Make sure that the user didn't turn off paired single support when
     MIPS-3D support is requested.  */
  if (TARGET_MIPS3D && (target_flags_explicit & MASK_PAIRED_SINGLE_FLOAT)
      && !TARGET_PAIRED_SINGLE_FLOAT)
    error ("-mips3d requires -mpaired-single");

  /* If TARGET_MIPS3D, enable MASK_PAIRED_SINGLE_FLOAT.  */
  if (TARGET_MIPS3D)
    target_flags |= MASK_PAIRED_SINGLE_FLOAT;

  /* Make sure that when TARGET_PAIRED_SINGLE_FLOAT is true, TARGET_FLOAT64
     and TARGET_HARD_FLOAT are both true.  */
  if (TARGET_PAIRED_SINGLE_FLOAT && !(TARGET_FLOAT64 && TARGET_HARD_FLOAT))
    error ("-mips3d/-mpaired-single must be used with -mfp64 -mhard-float");

  /* Make sure that the ISA supports TARGET_PAIRED_SINGLE_FLOAT when it is
     enabled.  */
  if (TARGET_PAIRED_SINGLE_FLOAT && !ISA_MIPS64)
    error ("-mips3d/-mpaired-single must be used with -mips64");

  if (TARGET_MIPS16 && TARGET_DSP)
    error ("-mips16 and -mdsp cannot be used together");

  mips_print_operand_punct['?'] = 1;
  mips_print_operand_punct['#'] = 1;
  mips_print_operand_punct['/'] = 1;
  mips_print_operand_punct['&'] = 1;
  mips_print_operand_punct['!'] = 1;
  mips_print_operand_punct['*'] = 1;
  mips_print_operand_punct['@'] = 1;
  mips_print_operand_punct['.'] = 1;
  mips_print_operand_punct['('] = 1;
  mips_print_operand_punct[')'] = 1;
  mips_print_operand_punct['['] = 1;
  mips_print_operand_punct[']'] = 1;
  mips_print_operand_punct['<'] = 1;
  mips_print_operand_punct['>'] = 1;
  mips_print_operand_punct['{'] = 1;
  mips_print_operand_punct['}'] = 1;
  mips_print_operand_punct['^'] = 1;
  mips_print_operand_punct['$'] = 1;
  mips_print_operand_punct['+'] = 1;
  mips_print_operand_punct['~'] = 1;

  /* Set up array to map GCC register number to debug register number.
     Ignore the special purpose register numbers.  */

  for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
    mips_dbx_regno[i] = -1;

  start = GP_DBX_FIRST - GP_REG_FIRST;
  for (i = GP_REG_FIRST; i <= GP_REG_LAST; i++)
    mips_dbx_regno[i] = i + start;

  start = FP_DBX_FIRST - FP_REG_FIRST;
  for (i = FP_REG_FIRST; i <= FP_REG_LAST; i++)
    mips_dbx_regno[i] = i + start;

  mips_dbx_regno[HI_REGNUM] = MD_DBX_FIRST + 0;
  mips_dbx_regno[LO_REGNUM] = MD_DBX_FIRST + 1;

  /* Set up array giving whether a given register can hold a given mode.  */

  for (mode = VOIDmode;
       mode != MAX_MACHINE_MODE;
       mode = (enum machine_mode) ((int)mode + 1))
    {
      register int size		     = GET_MODE_SIZE (mode);
      register enum mode_class class = GET_MODE_CLASS (mode);

      for (regno = 0; regno < FIRST_PSEUDO_REGISTER; regno++)
	{
	  register int temp;

	  if (mode == CCV2mode)
	    temp = (ISA_HAS_8CC
		    && ST_REG_P (regno)
		    && (regno - ST_REG_FIRST) % 2 == 0);

	  else if (mode == CCV4mode)
	    temp = (ISA_HAS_8CC
		    && ST_REG_P (regno)
		    && (regno - ST_REG_FIRST) % 4 == 0);

	  else if (mode == CCmode)
	    {
	      if (! ISA_HAS_8CC)
		temp = (regno == FPSW_REGNUM);
	      else
		temp = (ST_REG_P (regno) || GP_REG_P (regno)
			|| FP_REG_P (regno));
	    }

	  else if (GP_REG_P (regno))
	    temp = ((regno & 1) == 0 || size <= UNITS_PER_WORD);

	  else if (FP_REG_P (regno))
	    temp = ((regno % FP_INC) == 0)
		    && (((class == MODE_FLOAT || class == MODE_COMPLEX_FLOAT
			  || class == MODE_VECTOR_FLOAT)
			 && size <= UNITS_PER_FPVALUE)
			/* Allow integer modes that fit into a single
			   register.  We need to put integers into FPRs
			   when using instructions like cvt and trunc.
			   We can't allow sizes smaller than a word,
			   the FPU has no appropriate load/store
			   instructions for those.  */
			|| (class == MODE_INT
			    && size >= MIN_UNITS_PER_WORD
			    && size <= UNITS_PER_FPREG)
			/* Allow TFmode for CCmode reloads.  */
			|| (ISA_HAS_8CC && mode == TFmode));

          else if (ACC_REG_P (regno))
	    temp = (INTEGRAL_MODE_P (mode)
		    && (size <= UNITS_PER_WORD
			|| (ACC_HI_REG_P (regno)
			    && size == 2 * UNITS_PER_WORD)));

	  else if (ALL_COP_REG_P (regno))
	    temp = (class == MODE_INT && size <= UNITS_PER_WORD);
	  else
	    temp = 0;

	  mips_hard_regno_mode_ok[(int)mode][regno] = temp;
	}
    }

  /* Save GPR registers in word_mode sized hunks.  word_mode hasn't been
     initialized yet, so we can't use that here.  */
  gpr_mode = TARGET_64BIT ? DImode : SImode;

  /* Provide default values for align_* for 64-bit targets.  */
  if (TARGET_64BIT && !TARGET_MIPS16)
    {
      if (align_loops == 0)
	align_loops = 8;
      if (align_jumps == 0)
	align_jumps = 8;
      if (align_functions == 0)
	align_functions = 8;
    }

  /* Function to allocate machine-dependent function status.  */
  init_machine_status = &mips_init_machine_status;

  if (ABI_HAS_64BIT_SYMBOLS)
    {
      if (TARGET_EXPLICIT_RELOCS)
	{
	  mips_split_p[SYMBOL_64_HIGH] = true;
	  mips_hi_relocs[SYMBOL_64_HIGH] = "%highest(";
	  mips_lo_relocs[SYMBOL_64_HIGH] = "%higher(";

	  mips_split_p[SYMBOL_64_MID] = true;
	  mips_hi_relocs[SYMBOL_64_MID] = "%higher(";
	  mips_lo_relocs[SYMBOL_64_MID] = "%hi(";

	  mips_split_p[SYMBOL_64_LOW] = true;
	  mips_hi_relocs[SYMBOL_64_LOW] = "%hi(";
	  mips_lo_relocs[SYMBOL_64_LOW] = "%lo(";

	  mips_split_p[SYMBOL_GENERAL] = true;
	  mips_lo_relocs[SYMBOL_GENERAL] = "%lo(";
	}
    }
  else
    {
      if (TARGET_EXPLICIT_RELOCS || mips_split_addresses)
	{
	  mips_split_p[SYMBOL_GENERAL] = true;
	  mips_hi_relocs[SYMBOL_GENERAL] = "%hi(";
	  mips_lo_relocs[SYMBOL_GENERAL] = "%lo(";
	}
    }

  if (TARGET_MIPS16)
    {
      /* The high part is provided by a pseudo copy of $gp.  */
      mips_split_p[SYMBOL_SMALL_DATA] = true;
      mips_lo_relocs[SYMBOL_SMALL_DATA] = "%gprel(";
    }

  if (TARGET_EXPLICIT_RELOCS)
    {
      /* Small data constants are kept whole until after reload,
	 then lowered by mips_rewrite_small_data.  */
      mips_lo_relocs[SYMBOL_SMALL_DATA] = "%gp_rel(";

      mips_split_p[SYMBOL_GOT_LOCAL] = true;
      if (TARGET_NEWABI)
	{
	  mips_lo_relocs[SYMBOL_GOTOFF_PAGE] = "%got_page(";
	  mips_lo_relocs[SYMBOL_GOT_LOCAL] = "%got_ofst(";
	}
      else
	{
	  mips_lo_relocs[SYMBOL_GOTOFF_PAGE] = "%got(";
	  mips_lo_relocs[SYMBOL_GOT_LOCAL] = "%lo(";
	}

      if (TARGET_XGOT)
	{
	  /* The HIGH and LO_SUM are matched by special .md patterns.  */
	  mips_split_p[SYMBOL_GOT_GLOBAL] = true;

	  mips_split_p[SYMBOL_GOTOFF_GLOBAL] = true;
	  mips_hi_relocs[SYMBOL_GOTOFF_GLOBAL] = "%got_hi(";
	  mips_lo_relocs[SYMBOL_GOTOFF_GLOBAL] = "%got_lo(";

	  mips_split_p[SYMBOL_GOTOFF_CALL] = true;
	  mips_hi_relocs[SYMBOL_GOTOFF_CALL] = "%call_hi(";
	  mips_lo_relocs[SYMBOL_GOTOFF_CALL] = "%call_lo(";
	}
      else
	{
	  if (TARGET_NEWABI)
	    mips_lo_relocs[SYMBOL_GOTOFF_GLOBAL] = "%got_disp(";
	  else
	    mips_lo_relocs[SYMBOL_GOTOFF_GLOBAL] = "%got(";
	  mips_lo_relocs[SYMBOL_GOTOFF_CALL] = "%call16(";
	}
    }

  if (TARGET_NEWABI)
    {
      mips_split_p[SYMBOL_GOTOFF_LOADGP] = true;
      mips_hi_relocs[SYMBOL_GOTOFF_LOADGP] = "%hi(%neg(%gp_rel(";
      mips_lo_relocs[SYMBOL_GOTOFF_LOADGP] = "%lo(%neg(%gp_rel(";
    }

  /* Thread-local relocation operators.  */
  mips_lo_relocs[SYMBOL_TLSGD] = "%tlsgd(";
  mips_lo_relocs[SYMBOL_TLSLDM] = "%tlsldm(";
  mips_split_p[SYMBOL_DTPREL] = 1;
  mips_hi_relocs[SYMBOL_DTPREL] = "%dtprel_hi(";
  mips_lo_relocs[SYMBOL_DTPREL] = "%dtprel_lo(";
  mips_lo_relocs[SYMBOL_GOTTPREL] = "%gottprel(";
  mips_split_p[SYMBOL_TPREL] = 1;
  mips_hi_relocs[SYMBOL_TPREL] = "%tprel_hi(";
  mips_lo_relocs[SYMBOL_TPREL] = "%tprel_lo(";

  /* We don't have a thread pointer access instruction on MIPS16, or
     appropriate TLS relocations.  */
  if (TARGET_MIPS16)
    targetm.have_tls = false;

  /* Default to working around R4000 errata only if the processor
     was selected explicitly.  */
  if ((target_flags_explicit & MASK_FIX_R4000) == 0
      && mips_matching_cpu_name_p (mips_arch_info->name, "r4000"))
    target_flags |= MASK_FIX_R4000;

  /* Default to working around R4400 errata only if the processor
     was selected explicitly.  */
  if ((target_flags_explicit & MASK_FIX_R4400) == 0
      && mips_matching_cpu_name_p (mips_arch_info->name, "r4400"))
    target_flags |= MASK_FIX_R4400;
}

/* Implement CONDITIONAL_REGISTER_USAGE.  */

void
mips_conditional_register_usage (void)
{
  if (!TARGET_DSP)
    {
      int regno;

      for (regno = DSP_ACC_REG_FIRST; regno <= DSP_ACC_REG_LAST; regno++)
	fixed_regs[regno] = call_used_regs[regno] = 1;
    }
  if (!TARGET_HARD_FLOAT)
    {
      int regno;

      for (regno = FP_REG_FIRST; regno <= FP_REG_LAST; regno++)
	fixed_regs[regno] = call_used_regs[regno] = 1;
      for (regno = ST_REG_FIRST; regno <= ST_REG_LAST; regno++)
	fixed_regs[regno] = call_used_regs[regno] = 1;
    }
  else if (! ISA_HAS_8CC)
    {
      int regno;

      /* We only have a single condition code register.  We
	 implement this by hiding all the condition code registers,
	 and generating RTL that refers directly to ST_REG_FIRST.  */
      for (regno = ST_REG_FIRST; regno <= ST_REG_LAST; regno++)
	fixed_regs[regno] = call_used_regs[regno] = 1;
    }
  /* In mips16 mode, we permit the $t temporary registers to be used
     for reload.  We prohibit the unused $s registers, since they
     are caller saved, and saving them via a mips16 register would
     probably waste more time than just reloading the value.  */
  if (TARGET_MIPS16)
    {
      fixed_regs[18] = call_used_regs[18] = 1;
      fixed_regs[19] = call_used_regs[19] = 1;
      fixed_regs[20] = call_used_regs[20] = 1;
      fixed_regs[21] = call_used_regs[21] = 1;
      fixed_regs[22] = call_used_regs[22] = 1;
      fixed_regs[23] = call_used_regs[23] = 1;
      fixed_regs[26] = call_used_regs[26] = 1;
      fixed_regs[27] = call_used_regs[27] = 1;
      fixed_regs[30] = call_used_regs[30] = 1;
    }
  /* fp20-23 are now caller saved.  */
  if (mips_abi == ABI_64)
    {
      int regno;
      for (regno = FP_REG_FIRST + 20; regno < FP_REG_FIRST + 24; regno++)
	call_really_used_regs[regno] = call_used_regs[regno] = 1;
    }
  /* Odd registers from fp21 to fp31 are now caller saved.  */
  if (mips_abi == ABI_N32)
    {
      int regno;
      for (regno = FP_REG_FIRST + 21; regno <= FP_REG_FIRST + 31; regno+=2)
	call_really_used_regs[regno] = call_used_regs[regno] = 1;
    }
}

/* Allocate a chunk of memory for per-function machine-dependent data.  */
static struct machine_function *
mips_init_machine_status (void)
{
  return ((struct machine_function *)
	  ggc_alloc_cleared (sizeof (struct machine_function)));
}

/* On the mips16, we want to allocate $24 (T_REG) before other
   registers for instructions for which it is possible.  This helps
   avoid shuffling registers around in order to set up for an xor,
   encouraging the compiler to use a cmp instead.  */

void
mips_order_regs_for_local_alloc (void)
{
  register int i;

  for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
    reg_alloc_order[i] = i;

  if (TARGET_MIPS16)
    {
      /* It really doesn't matter where we put register 0, since it is
         a fixed register anyhow.  */
      reg_alloc_order[0] = 24;
      reg_alloc_order[24] = 0;
    }
}


/* The MIPS debug format wants all automatic variables and arguments
   to be in terms of the virtual frame pointer (stack pointer before
   any adjustment in the function), while the MIPS 3.0 linker wants
   the frame pointer to be the stack pointer after the initial
   adjustment.  So, we do the adjustment here.  The arg pointer (which
   is eliminated) points to the virtual frame pointer, while the frame
   pointer (which may be eliminated) points to the stack pointer after
   the initial adjustments.  */

HOST_WIDE_INT
mips_debugger_offset (rtx addr, HOST_WIDE_INT offset)
{
  rtx offset2 = const0_rtx;
  rtx reg = eliminate_constant_term (addr, &offset2);

  if (offset == 0)
    offset = INTVAL (offset2);

  if (reg == stack_pointer_rtx || reg == frame_pointer_rtx
      || reg == hard_frame_pointer_rtx)
    {
      HOST_WIDE_INT frame_size = (!cfun->machine->frame.initialized)
				  ? compute_frame_size (get_frame_size ())
				  : cfun->machine->frame.total_size;

      /* MIPS16 frame is smaller */
      if (frame_pointer_needed && TARGET_MIPS16)
	frame_size -= cfun->machine->frame.args_size;

      offset = offset - frame_size;
    }

  /* sdbout_parms does not want this to crash for unrecognized cases.  */
#if 0
  else if (reg != arg_pointer_rtx)
    fatal_insn ("mips_debugger_offset called with non stack/frame/arg pointer",
		addr);
#endif

  return offset;
}

/* Implement the PRINT_OPERAND macro.  The MIPS-specific operand codes are:

   'X'  OP is CONST_INT, prints 32 bits in hexadecimal format = "0x%08x",
   'x'  OP is CONST_INT, prints 16 bits in hexadecimal format = "0x%04x",
   'h'  OP is HIGH, prints %hi(X),
   'd'  output integer constant in decimal,
   'z'	if the operand is 0, use $0 instead of normal operand.
   'D'  print second part of double-word register or memory operand.
   'L'  print low-order register of double-word register operand.
   'M'  print high-order register of double-word register operand.
   'C'  print part of opcode for a branch condition.
   'F'  print part of opcode for a floating-point branch condition.
   'N'  print part of opcode for a branch condition, inverted.
   'W'  print part of opcode for a floating-point branch condition, inverted.
   'T'  print 'f' for (eq:CC ...), 't' for (ne:CC ...),
	      'z' for (eq:?I ...), 'n' for (ne:?I ...).
   't'  like 'T', but with the EQ/NE cases reversed
   'Y'  for a CONST_INT X, print mips_fp_conditions[X]
   'Z'  print the operand and a comma for ISA_HAS_8CC, otherwise print nothing
   'R'  print the reloc associated with LO_SUM
   'q'  print DSP accumulator registers

   The punctuation characters are:

   '('	Turn on .set noreorder
   ')'	Turn on .set reorder
   '['	Turn on .set noat
   ']'	Turn on .set at
   '<'	Turn on .set nomacro
   '>'	Turn on .set macro
   '{'	Turn on .set volatile (not GAS)
   '}'	Turn on .set novolatile (not GAS)
   '&'	Turn on .set noreorder if filling delay slots
   '*'	Turn on both .set noreorder and .set nomacro if filling delay slots
   '!'	Turn on .set nomacro if filling delay slots
   '#'	Print nop if in a .set noreorder section.
   '/'	Like '#', but does nothing within a delayed branch sequence
   '?'	Print 'l' if we are to use a branch likely instead of normal branch.
   '@'	Print the name of the assembler temporary register (at or $1).
   '.'	Print the name of the register with a hard-wired zero (zero or $0).
   '^'	Print the name of the pic call-through register (t9 or $25).
   '$'	Print the name of the stack pointer register (sp or $29).
   '+'	Print the name of the gp register (usually gp or $28).
   '~'	Output a branch alignment to LABEL_ALIGN(NULL).  */

void
print_operand (FILE *file, rtx op, int letter)
{
  register enum rtx_code code;

  if (PRINT_OPERAND_PUNCT_VALID_P (letter))
    {
      switch (letter)
	{
	case '?':
	  if (mips_branch_likely)
	    putc ('l', file);
	  break;

	case '@':
	  fputs (reg_names [GP_REG_FIRST + 1], file);
	  break;

	case '^':
	  fputs (reg_names [PIC_FUNCTION_ADDR_REGNUM], file);
	  break;

	case '.':
	  fputs (reg_names [GP_REG_FIRST + 0], file);
	  break;

	case '$':
	  fputs (reg_names[STACK_POINTER_REGNUM], file);
	  break;

	case '+':
	  fputs (reg_names[PIC_OFFSET_TABLE_REGNUM], file);
	  break;

	case '&':
	  if (final_sequence != 0 && set_noreorder++ == 0)
	    fputs (".set\tnoreorder\n\t", file);
	  break;

	case '*':
	  if (final_sequence != 0)
	    {
	      if (set_noreorder++ == 0)
		fputs (".set\tnoreorder\n\t", file);

	      if (set_nomacro++ == 0)
		fputs (".set\tnomacro\n\t", file);
	    }
	  break;

	case '!':
	  if (final_sequence != 0 && set_nomacro++ == 0)
	    fputs ("\n\t.set\tnomacro", file);
	  break;

	case '#':
	  if (set_noreorder != 0)
	    fputs ("\n\tnop", file);
	  break;

	case '/':
	  /* Print an extra newline so that the delayed insn is separated
	     from the following ones.  This looks neater and is consistent
	     with non-nop delayed sequences.  */
	  if (set_noreorder != 0 && final_sequence == 0)
	    fputs ("\n\tnop\n", file);
	  break;

	case '(':
	  if (set_noreorder++ == 0)
	    fputs (".set\tnoreorder\n\t", file);
	  break;

	case ')':
	  if (set_noreorder == 0)
	    error ("internal error: %%) found without a %%( in assembler pattern");

	  else if (--set_noreorder == 0)
	    fputs ("\n\t.set\treorder", file);

	  break;

	case '[':
	  if (set_noat++ == 0)
	    fputs (".set\tnoat\n\t", file);
	  break;

	case ']':
	  if (set_noat == 0)
	    error ("internal error: %%] found without a %%[ in assembler pattern");
	  else if (--set_noat == 0)
	    fputs ("\n\t.set\tat", file);

	  break;

	case '<':
	  if (set_nomacro++ == 0)
	    fputs (".set\tnomacro\n\t", file);
	  break;

	case '>':
	  if (set_nomacro == 0)
	    error ("internal error: %%> found without a %%< in assembler pattern");
	  else if (--set_nomacro == 0)
	    fputs ("\n\t.set\tmacro", file);

	  break;

	case '{':
	  if (set_volatile++ == 0)
	    fputs ("#.set\tvolatile\n\t", file);
	  break;

	case '}':
	  if (set_volatile == 0)
	    error ("internal error: %%} found without a %%{ in assembler pattern");
	  else if (--set_volatile == 0)
	    fputs ("\n\t#.set\tnovolatile", file);

	  break;

	case '~':
	  {
	    if (align_labels_log > 0)
	      ASM_OUTPUT_ALIGN (file, align_labels_log);
	  }
	  break;

	default:
	  error ("PRINT_OPERAND: unknown punctuation '%c'", letter);
	  break;
	}

      return;
    }

  if (! op)
    {
      error ("PRINT_OPERAND null pointer");
      return;
    }

  code = GET_CODE (op);

  if (letter == 'C')
    switch (code)
      {
      case EQ:	fputs ("eq",  file); break;
      case NE:	fputs ("ne",  file); break;
      case GT:	fputs ("gt",  file); break;
      case GE:	fputs ("ge",  file); break;
      case LT:	fputs ("lt",  file); break;
      case LE:	fputs ("le",  file); break;
      case GTU: fputs ("gtu", file); break;
      case GEU: fputs ("geu", file); break;
      case LTU: fputs ("ltu", file); break;
      case LEU: fputs ("leu", file); break;
      default:
	fatal_insn ("PRINT_OPERAND, invalid insn for %%C", op);
      }

  else if (letter == 'N')
    switch (code)
      {
      case EQ:	fputs ("ne",  file); break;
      case NE:	fputs ("eq",  file); break;
      case GT:	fputs ("le",  file); break;
      case GE:	fputs ("lt",  file); break;
      case LT:	fputs ("ge",  file); break;
      case LE:	fputs ("gt",  file); break;
      case GTU: fputs ("leu", file); break;
      case GEU: fputs ("ltu", file); break;
      case LTU: fputs ("geu", file); break;
      case LEU: fputs ("gtu", file); break;
      default:
	fatal_insn ("PRINT_OPERAND, invalid insn for %%N", op);
      }

  else if (letter == 'F')
    switch (code)
      {
      case EQ: fputs ("c1f", file); break;
      case NE: fputs ("c1t", file); break;
      default:
	fatal_insn ("PRINT_OPERAND, invalid insn for %%F", op);
      }

  else if (letter == 'W')
    switch (code)
      {
      case EQ: fputs ("c1t", file); break;
      case NE: fputs ("c1f", file); break;
      default:
	fatal_insn ("PRINT_OPERAND, invalid insn for %%W", op);
      }

  else if (letter == 'h')
    {
      if (GET_CODE (op) == HIGH)
	op = XEXP (op, 0);

      print_operand_reloc (file, op, mips_hi_relocs);
    }

  else if (letter == 'R')
    print_operand_reloc (file, op, mips_lo_relocs);

  else if (letter == 'Y')
    {
      if (GET_CODE (op) == CONST_INT
	  && ((unsigned HOST_WIDE_INT) INTVAL (op)
	      < ARRAY_SIZE (mips_fp_conditions)))
	fputs (mips_fp_conditions[INTVAL (op)], file);
      else
	output_operand_lossage ("invalid %%Y value");
    }

  else if (letter == 'Z')
    {
      if (ISA_HAS_8CC)
	{
	  print_operand (file, op, 0);
	  fputc (',', file);
	}
    }

  else if (letter == 'q')
    {
      int regnum;

      if (code != REG)
	fatal_insn ("PRINT_OPERAND, invalid insn for %%q", op);

      regnum = REGNO (op);
      if (MD_REG_P (regnum))
	fprintf (file, "$ac0");
      else if (DSP_ACC_REG_P (regnum))
	fprintf (file, "$ac%c", reg_names[regnum][3]);
      else
	fatal_insn ("PRINT_OPERAND, invalid insn for %%q", op);
    }

  else if (code == REG || code == SUBREG)
    {
      register int regnum;

      if (code == REG)
	regnum = REGNO (op);
      else
	regnum = true_regnum (op);

      if ((letter == 'M' && ! WORDS_BIG_ENDIAN)
	  || (letter == 'L' && WORDS_BIG_ENDIAN)
	  || letter == 'D')
	regnum++;

      fprintf (file, "%s", reg_names[regnum]);
    }

  else if (code == MEM)
    {
      if (letter == 'D')
	output_address (plus_constant (XEXP (op, 0), 4));
      else
	output_address (XEXP (op, 0));
    }

  else if (letter == 'x' && GET_CODE (op) == CONST_INT)
    fprintf (file, HOST_WIDE_INT_PRINT_HEX, 0xffff & INTVAL(op));

  else if (letter == 'X' && GET_CODE(op) == CONST_INT)
    fprintf (file, HOST_WIDE_INT_PRINT_HEX, INTVAL (op));

  else if (letter == 'd' && GET_CODE(op) == CONST_INT)
    fprintf (file, HOST_WIDE_INT_PRINT_DEC, (INTVAL(op)));

  else if (letter == 'z' && op == CONST0_RTX (GET_MODE (op)))
    fputs (reg_names[GP_REG_FIRST], file);

  else if (letter == 'd' || letter == 'x' || letter == 'X')
    output_operand_lossage ("invalid use of %%d, %%x, or %%X");

  else if (letter == 'T' || letter == 't')
    {
      int truth = (code == NE) == (letter == 'T');
      fputc ("zfnt"[truth * 2 + (GET_MODE (op) == CCmode)], file);
    }

  else if (CONST_GP_P (op))
    fputs (reg_names[GLOBAL_POINTER_REGNUM], file);

  else
    output_addr_const (file, op);
}


/* Print symbolic operand OP, which is part of a HIGH or LO_SUM.
   RELOCS is the array of relocations to use.  */

static void
print_operand_reloc (FILE *file, rtx op, const char **relocs)
{
  enum mips_symbol_type symbol_type;
  const char *p;
  rtx base;
  HOST_WIDE_INT offset;

  if (!mips_symbolic_constant_p (op, &symbol_type) || relocs[symbol_type] == 0)
    fatal_insn ("PRINT_OPERAND, invalid operand for relocation", op);

  /* If OP uses an UNSPEC address, we want to print the inner symbol.  */
  mips_split_const (op, &base, &offset);
  if (UNSPEC_ADDRESS_P (base))
    op = plus_constant (UNSPEC_ADDRESS (base), offset);

  fputs (relocs[symbol_type], file);
  output_addr_const (file, op);
  for (p = relocs[symbol_type]; *p != 0; p++)
    if (*p == '(')
      fputc (')', file);
}

/* Output address operand X to FILE.  */

void
print_operand_address (FILE *file, rtx x)
{
  struct mips_address_info addr;

  if (mips_classify_address (&addr, x, word_mode, true))
    switch (addr.type)
      {
      case ADDRESS_REG:
	print_operand (file, addr.offset, 0);
	fprintf (file, "(%s)", reg_names[REGNO (addr.reg)]);
	return;

      case ADDRESS_LO_SUM:
	print_operand (file, addr.offset, 'R');
	fprintf (file, "(%s)", reg_names[REGNO (addr.reg)]);
	return;

      case ADDRESS_CONST_INT:
	output_addr_const (file, x);
	fprintf (file, "(%s)", reg_names[0]);
	return;

      case ADDRESS_SYMBOLIC:
	output_addr_const (file, x);
	return;
      }
  gcc_unreachable ();
}

/* When using assembler macros, keep track of all of small-data externs
   so that mips_file_end can emit the appropriate declarations for them.

   In most cases it would be safe (though pointless) to emit .externs
   for other symbols too.  One exception is when an object is within
   the -G limit but declared by the user to be in a section other
   than .sbss or .sdata.  */

int
mips_output_external (FILE *file ATTRIBUTE_UNUSED, tree decl, const char *name)
{
  register struct extern_list *p;

  default_elf_asm_output_external(file, decl, name);

  if (!TARGET_EXPLICIT_RELOCS && mips_in_small_data_p (decl))
    {
      p = (struct extern_list *) ggc_alloc (sizeof (struct extern_list));
      p->next = extern_head;
      p->name = name;
      p->size = int_size_in_bytes (TREE_TYPE (decl));
      extern_head = p;
    }

  if (TARGET_IRIX && mips_abi == ABI_32 && TREE_CODE (decl) == FUNCTION_DECL)
    {
      p = (struct extern_list *) ggc_alloc (sizeof (struct extern_list));
      p->next = extern_head;
      p->name = name;
      p->size = -1;
      extern_head = p;
    }

  return 0;
}

#if TARGET_IRIX
static void
irix_output_external_libcall (rtx fun)
{
  register struct extern_list *p;

  if (mips_abi == ABI_32)
    {
      p = (struct extern_list *) ggc_alloc (sizeof (struct extern_list));
      p->next = extern_head;
      p->name = XSTR (fun, 0);
      p->size = -1;
      extern_head = p;
    }
}
#endif

/* Emit a new filename to a stream.  If we are smuggling stabs, try to
   put out a MIPS ECOFF file and a stab.  */

void
mips_output_filename (FILE *stream, const char *name)
{

  /* If we are emitting DWARF-2, let dwarf2out handle the ".file"
     directives.  */
  if (write_symbols == DWARF2_DEBUG)
    return;
  else if (mips_output_filename_first_time)
    {
      mips_output_filename_first_time = 0;
      num_source_filenames += 1;
      current_function_file = name;
      fprintf (stream, "\t.file\t%d ", num_source_filenames);
      output_quoted_string (stream, name);
      putc ('\n', stream);
    }

  /* If we are emitting stabs, let dbxout.c handle this (except for
     the mips_output_filename_first_time case).  */
  else if (write_symbols == DBX_DEBUG)
    return;

  else if (name != current_function_file
	   && strcmp (name, current_function_file) != 0)
    {
      num_source_filenames += 1;
      current_function_file = name;
      fprintf (stream, "\t.file\t%d ", num_source_filenames);
      output_quoted_string (stream, name);
      putc ('\n', stream);
    }
}

/* Output an ASCII string, in a space-saving way.  PREFIX is the string
   that should be written before the opening quote, such as "\t.ascii\t"
   for real string data or "\t# " for a comment.  */

void
mips_output_ascii (FILE *stream, const char *string_param, size_t len,
		   const char *prefix)
{
  size_t i;
  int cur_pos = 17;
  register const unsigned char *string =
    (const unsigned char *)string_param;

  fprintf (stream, "%s\"", prefix);
  for (i = 0; i < len; i++)
    {
      register int c = string[i];

      if (ISPRINT (c))
	{
	  if (c == '\\' || c == '\"')
	    {
	      putc ('\\', stream);
	      cur_pos++;
	    }
	  putc (c, stream);
	  cur_pos++;
	}
      else
	{
	  fprintf (stream, "\\%03o", c);
	  cur_pos += 4;
	}

      if (cur_pos > 72 && i+1 < len)
	{
	  cur_pos = 17;
	  fprintf (stream, "\"\n%s\"", prefix);
	}
    }
  fprintf (stream, "\"\n");
}

/* Implement TARGET_ASM_FILE_START.  */

static void
mips_file_start (void)
{
  default_file_start ();

  if (!TARGET_IRIX)
    {
      /* Generate a special section to describe the ABI switches used to
	 produce the resultant binary.  This used to be done by the assembler
	 setting bits in the ELF header's flags field, but we have run out of
	 bits.  GDB needs this information in order to be able to correctly
	 debug these binaries.  See the function mips_gdbarch_init() in
	 gdb/mips-tdep.c.  This is unnecessary for the IRIX 5/6 ABIs and
	 causes unnecessary IRIX 6 ld warnings.  */
      const char * abi_string = NULL;

      switch (mips_abi)
	{
	case ABI_32:   abi_string = "abi32"; break;
	case ABI_N32:  abi_string = "abiN32"; break;
	case ABI_64:   abi_string = "abi64"; break;
	case ABI_O64:  abi_string = "abiO64"; break;
	case ABI_EABI: abi_string = TARGET_64BIT ? "eabi64" : "eabi32"; break;
	default:
	  gcc_unreachable ();
	}
      /* Note - we use fprintf directly rather than calling switch_to_section
	 because in this way we can avoid creating an allocated section.  We
	 do not want this section to take up any space in the running
	 executable.  */
      fprintf (asm_out_file, "\t.section .mdebug.%s\n", abi_string);

      /* There is no ELF header flag to distinguish long32 forms of the
	 EABI from long64 forms.  Emit a special section to help tools
	 such as GDB.  Do the same for o64, which is sometimes used with
	 -mlong64.  */
      if (mips_abi == ABI_EABI || mips_abi == ABI_O64)
	fprintf (asm_out_file, "\t.section .gcc_compiled_long%d\n",
		 TARGET_LONG64 ? 64 : 32);

      /* Restore the default section.  */
      fprintf (asm_out_file, "\t.previous\n");
    }

  /* Generate the pseudo ops that System V.4 wants.  */
  if (TARGET_ABICALLS)
    fprintf (asm_out_file, "\t.abicalls\n");

  if (TARGET_MIPS16)
    fprintf (asm_out_file, "\t.set\tmips16\n");

  if (flag_verbose_asm)
    fprintf (asm_out_file, "\n%s -G value = %d, Arch = %s, ISA = %d\n",
	     ASM_COMMENT_START,
	     mips_section_threshold, mips_arch_info->name, mips_isa);
}

#ifdef BSS_SECTION_ASM_OP
/* Implement ASM_OUTPUT_ALIGNED_BSS.  This differs from the default only
   in the use of sbss.  */

void
mips_output_aligned_bss (FILE *stream, tree decl, const char *name,
			 unsigned HOST_WIDE_INT size, int align)
{
  extern tree last_assemble_variable_decl;

  if (mips_in_small_data_p (decl))
    switch_to_section (get_named_section (NULL, ".sbss", 0));
  else
    switch_to_section (bss_section);
  ASM_OUTPUT_ALIGN (stream, floor_log2 (align / BITS_PER_UNIT));
  last_assemble_variable_decl = decl;
  ASM_DECLARE_OBJECT_NAME (stream, name, decl);
  ASM_OUTPUT_SKIP (stream, size != 0 ? size : 1);
}
#endif

/* Implement TARGET_ASM_FILE_END.  When using assembler macros, emit
   .externs for any small-data variables that turned out to be external.  */

static void
mips_file_end (void)
{
  tree name_tree;
  struct extern_list *p;

  if (extern_head)
    {
      fputs ("\n", asm_out_file);

      for (p = extern_head; p != 0; p = p->next)
	{
	  name_tree = get_identifier (p->name);

	  /* Positively ensure only one .extern for any given symbol.  */
	  if (!TREE_ASM_WRITTEN (name_tree)
	      && TREE_SYMBOL_REFERENCED (name_tree))
	    {
	      TREE_ASM_WRITTEN (name_tree) = 1;
	      /* In IRIX 5 or IRIX 6 for the O32 ABI, we must output a
		 `.global name .text' directive for every used but
		 undefined function.  If we don't, the linker may perform
		 an optimization (skipping over the insns that set $gp)
		 when it is unsafe.  */
	      if (TARGET_IRIX && mips_abi == ABI_32 && p->size == -1)
		{
		  fputs ("\t.globl ", asm_out_file);
		  assemble_name (asm_out_file, p->name);
		  fputs (" .text\n", asm_out_file);
		}
	      else
		{
		  fputs ("\t.extern\t", asm_out_file);
		  assemble_name (asm_out_file, p->name);
		  fprintf (asm_out_file, ", %d\n", p->size);
		}
	    }
	}
    }
}

/* Implement ASM_OUTPUT_ALIGNED_DECL_COMMON.  This is usually the same as the
   elfos.h version, but we also need to handle -muninit-const-in-rodata.  */

void
mips_output_aligned_decl_common (FILE *stream, tree decl, const char *name,
				 unsigned HOST_WIDE_INT size,
				 unsigned int align)
{
  /* If the target wants uninitialized const declarations in
     .rdata then don't put them in .comm.  */
  if (TARGET_EMBEDDED_DATA && TARGET_UNINIT_CONST_IN_RODATA
      && TREE_CODE (decl) == VAR_DECL && TREE_READONLY (decl)
      && (DECL_INITIAL (decl) == 0 || DECL_INITIAL (decl) == error_mark_node))
    {
      if (TREE_PUBLIC (decl) && DECL_NAME (decl))
	targetm.asm_out.globalize_label (stream, name);

      switch_to_section (readonly_data_section);
      ASM_OUTPUT_ALIGN (stream, floor_log2 (align / BITS_PER_UNIT));
      mips_declare_object (stream, name, "",
			   ":\n\t.space\t" HOST_WIDE_INT_PRINT_UNSIGNED "\n",
			   size);
    }
  else
    mips_declare_common_object (stream, name, "\n\t.comm\t",
				size, align, true);
}

/* Declare a common object of SIZE bytes using asm directive INIT_STRING.
   NAME is the name of the object and ALIGN is the required alignment
   in bytes.  TAKES_ALIGNMENT_P is true if the directive takes a third
   alignment argument.  */

void
mips_declare_common_object (FILE *stream, const char *name,
			    const char *init_string,
			    unsigned HOST_WIDE_INT size,
			    unsigned int align, bool takes_alignment_p)
{
  if (!takes_alignment_p)
    {
      size += (align / BITS_PER_UNIT) - 1;
      size -= size % (align / BITS_PER_UNIT);
      mips_declare_object (stream, name, init_string,
			   "," HOST_WIDE_INT_PRINT_UNSIGNED "\n", size);
    }
  else
    mips_declare_object (stream, name, init_string,
			 "," HOST_WIDE_INT_PRINT_UNSIGNED ",%u\n",
			 size, align / BITS_PER_UNIT);
}

/* Emit either a label, .comm, or .lcomm directive.  When using assembler
   macros, mark the symbol as written so that mips_file_end won't emit an
   .extern for it.  STREAM is the output file, NAME is the name of the
   symbol, INIT_STRING is the string that should be written before the
   symbol and FINAL_STRING is the string that should be written after it.
   FINAL_STRING is a printf() format that consumes the remaining arguments.  */

void
mips_declare_object (FILE *stream, const char *name, const char *init_string,
		     const char *final_string, ...)
{
  va_list ap;

  fputs (init_string, stream);
  assemble_name (stream, name);
  va_start (ap, final_string);
  vfprintf (stream, final_string, ap);
  va_end (ap);

  if (!TARGET_EXPLICIT_RELOCS)
    {
      tree name_tree = get_identifier (name);
      TREE_ASM_WRITTEN (name_tree) = 1;
    }
}

#ifdef ASM_OUTPUT_SIZE_DIRECTIVE
extern int size_directive_output;

/* Implement ASM_DECLARE_OBJECT_NAME.  This is like most of the standard ELF
   definitions except that it uses mips_declare_object() to emit the label.  */

void
mips_declare_object_name (FILE *stream, const char *name,
			  tree decl ATTRIBUTE_UNUSED)
{
#ifdef ASM_OUTPUT_TYPE_DIRECTIVE
  ASM_OUTPUT_TYPE_DIRECTIVE (stream, name, "object");
#endif

  size_directive_output = 0;
  if (!flag_inhibit_size_directive && DECL_SIZE (decl))
    {
      HOST_WIDE_INT size;

      size_directive_output = 1;
      size = int_size_in_bytes (TREE_TYPE (decl));
      ASM_OUTPUT_SIZE_DIRECTIVE (stream, name, size);
    }

  mips_declare_object (stream, name, "", ":\n");
}

/* Implement ASM_FINISH_DECLARE_OBJECT.  This is generic ELF stuff.  */

void
mips_finish_declare_object (FILE *stream, tree decl, int top_level, int at_end)
{
  const char *name;

  name = XSTR (XEXP (DECL_RTL (decl), 0), 0);
  if (!flag_inhibit_size_directive
      && DECL_SIZE (decl) != 0
      && !at_end && top_level
      && DECL_INITIAL (decl) == error_mark_node
      && !size_directive_output)
    {
      HOST_WIDE_INT size;

      size_directive_output = 1;
      size = int_size_in_bytes (TREE_TYPE (decl));
      ASM_OUTPUT_SIZE_DIRECTIVE (stream, name, size);
    }
}
#endif

/* Return true if X is a small data address that can be rewritten
   as a LO_SUM.  */

static bool
mips_rewrite_small_data_p (rtx x)
{
  enum mips_symbol_type symbol_type;

  return (TARGET_EXPLICIT_RELOCS
	  && mips_symbolic_constant_p (x, &symbol_type)
	  && symbol_type == SYMBOL_SMALL_DATA);
}


/* A for_each_rtx callback for mips_small_data_pattern_p.  */

static int
mips_small_data_pattern_1 (rtx *loc, void *data ATTRIBUTE_UNUSED)
{
  if (GET_CODE (*loc) == LO_SUM)
    return -1;

  return mips_rewrite_small_data_p (*loc);
}

/* Return true if OP refers to small data symbols directly, not through
   a LO_SUM.  */

bool
mips_small_data_pattern_p (rtx op)
{
  return for_each_rtx (&op, mips_small_data_pattern_1, 0);
}

/* A for_each_rtx callback, used by mips_rewrite_small_data.  */

static int
mips_rewrite_small_data_1 (rtx *loc, void *data ATTRIBUTE_UNUSED)
{
  if (mips_rewrite_small_data_p (*loc))
    *loc = gen_rtx_LO_SUM (Pmode, pic_offset_table_rtx, *loc);

  if (GET_CODE (*loc) == LO_SUM)
    return -1;

  return 0;
}

/* If possible, rewrite OP so that it refers to small data using
   explicit relocations.  */

rtx
mips_rewrite_small_data (rtx op)
{
  op = copy_insn (op);
  for_each_rtx (&op, mips_rewrite_small_data_1, 0);
  return op;
}

/* Return true if the current function has an insn that implicitly
   refers to $gp.  */

static bool
mips_function_has_gp_insn (void)
{
  /* Don't bother rechecking if we found one last time.  */
  if (!cfun->machine->has_gp_insn_p)
    {
      rtx insn;

      push_topmost_sequence ();
      for (insn = get_insns (); insn; insn = NEXT_INSN (insn))
	if (INSN_P (insn)
	    && GET_CODE (PATTERN (insn)) != USE
	    && GET_CODE (PATTERN (insn)) != CLOBBER
	    && (get_attr_got (insn) != GOT_UNSET
		|| small_data_pattern (PATTERN (insn), VOIDmode)))
	  break;
      pop_topmost_sequence ();

      cfun->machine->has_gp_insn_p = (insn != 0);
    }
  return cfun->machine->has_gp_insn_p;
}


/* Return the register that should be used as the global pointer
   within this function.  Return 0 if the function doesn't need
   a global pointer.  */

static unsigned int
mips_global_pointer (void)
{
  unsigned int regno;

  /* $gp is always available in non-abicalls code.  */
  if (!TARGET_ABICALLS)
    return GLOBAL_POINTER_REGNUM;

  /* We must always provide $gp when it is used implicitly.  */
  if (!TARGET_EXPLICIT_RELOCS)
    return GLOBAL_POINTER_REGNUM;

  /* FUNCTION_PROFILER includes a jal macro, so we need to give it
     a valid gp.  */
  if (current_function_profile)
    return GLOBAL_POINTER_REGNUM;

  /* If the function has a nonlocal goto, $gp must hold the correct
     global pointer for the target function.  */
  if (current_function_has_nonlocal_goto)
    return GLOBAL_POINTER_REGNUM;

  /* If the gp is never referenced, there's no need to initialize it.
     Note that reload can sometimes introduce constant pool references
     into a function that otherwise didn't need them.  For example,
     suppose we have an instruction like:

	  (set (reg:DF R1) (float:DF (reg:SI R2)))

     If R2 turns out to be constant such as 1, the instruction may have a
     REG_EQUAL note saying that R1 == 1.0.  Reload then has the option of
     using this constant if R2 doesn't get allocated to a register.

     In cases like these, reload will have added the constant to the pool
     but no instruction will yet refer to it.  */
  if (!regs_ever_live[GLOBAL_POINTER_REGNUM]
      && !current_function_uses_const_pool
      && !mips_function_has_gp_insn ())
    return 0;

  /* We need a global pointer, but perhaps we can use a call-clobbered
     register instead of $gp.  */
  if (TARGET_NEWABI && current_function_is_leaf)
    for (regno = GP_REG_FIRST; regno <= GP_REG_LAST; regno++)
      if (!regs_ever_live[regno]
	  && call_used_regs[regno]
	  && !fixed_regs[regno]
	  && regno != PIC_FUNCTION_ADDR_REGNUM)
	return regno;

  return GLOBAL_POINTER_REGNUM;
}


/* Return true if the current function must save REGNO.  */

static bool
mips_save_reg_p (unsigned int regno)
{
  /* We only need to save $gp for NewABI PIC.  */
  if (regno == GLOBAL_POINTER_REGNUM)
    return (TARGET_ABICALLS && TARGET_NEWABI
	    && cfun->machine->global_pointer == regno);

  /* Check call-saved registers.  */
  if (regs_ever_live[regno] && !call_used_regs[regno])
    return true;

  /* We need to save the old frame pointer before setting up a new one.  */
  if (regno == HARD_FRAME_POINTER_REGNUM && frame_pointer_needed)
    return true;

  /* We need to save the incoming return address if it is ever clobbered
     within the function.  */
  if (regno == GP_REG_FIRST + 31 && regs_ever_live[regno])
    return true;

  if (TARGET_MIPS16)
    {
      tree return_type;

      return_type = DECL_RESULT (current_function_decl);

      /* $18 is a special case in mips16 code.  It may be used to call
	 a function which returns a floating point value, but it is
	 marked in call_used_regs.  */
      if (regno == GP_REG_FIRST + 18 && regs_ever_live[regno])
	return true;

      /* $31 is also a special case.  It will be used to copy a return
	 value into the floating point registers if the return value is
	 floating point.  */
      if (regno == GP_REG_FIRST + 31
	  && mips16_hard_float
	  && !aggregate_value_p (return_type, current_function_decl)
	  && GET_MODE_CLASS (DECL_MODE (return_type)) == MODE_FLOAT
	  && GET_MODE_SIZE (DECL_MODE (return_type)) <= UNITS_PER_FPVALUE)
	return true;
    }

  return false;
}


/* Return the bytes needed to compute the frame pointer from the current
   stack pointer.  SIZE is the size (in bytes) of the local variables.

   MIPS stack frames look like:

             Before call		        After call
        +-----------------------+	+-----------------------+
   high |			|       |      			|
   mem. |		        |	|			|
        |  caller's temps.    	|       |  caller's temps.    	|
	|       		|       |       	        |
        +-----------------------+	+-----------------------+
 	|       		|	|		        |
        |  arguments on stack.  |	|  arguments on stack.  |
	|       		|	|			|
        +-----------------------+	+-----------------------+
 	|  4 words to save     	|	|  4 words to save	|
	|  arguments passed	|	|  arguments passed	|
	|  in registers, even	|	|  in registers, even	|
    SP->|  if not passed.       |  VFP->|  if not passed.	|
	+-----------------------+       +-----------------------+
					|		        |
                                        |  fp register save     |
					|			|
					+-----------------------+
					|		        |
                                        |  gp register save     |
                                        |       		|
					+-----------------------+
					|			|
					|  local variables	|
					|			|
					+-----------------------+
					|			|
                                        |  alloca allocations   |
        				|			|
					+-----------------------+
					|			|
					|  GP save for V.4 abi	|
					|			|
					+-----------------------+
					|			|
                                        |  arguments on stack   |
        				|		        |
					+-----------------------+
                                        |  4 words to save      |
					|  arguments passed     |
                                        |  in registers, even   |
   low                              SP->|  if not passed.       |
   memory        			+-----------------------+

*/

HOST_WIDE_INT
compute_frame_size (HOST_WIDE_INT size)
{
  unsigned int regno;
  HOST_WIDE_INT total_size;	/* # bytes that the entire frame takes up */
  HOST_WIDE_INT var_size;	/* # bytes that variables take up */
  HOST_WIDE_INT args_size;	/* # bytes that outgoing arguments take up */
  HOST_WIDE_INT cprestore_size; /* # bytes that the cprestore slot takes up */
  HOST_WIDE_INT gp_reg_rounded;	/* # bytes needed to store gp after rounding */
  HOST_WIDE_INT gp_reg_size;	/* # bytes needed to store gp regs */
  HOST_WIDE_INT fp_reg_size;	/* # bytes needed to store fp regs */
  unsigned int mask;		/* mask of saved gp registers */
  unsigned int fmask;		/* mask of saved fp registers */

  cfun->machine->global_pointer = mips_global_pointer ();

  gp_reg_size = 0;
  fp_reg_size = 0;
  mask = 0;
  fmask	= 0;
  var_size = MIPS_STACK_ALIGN (size);
  args_size = current_function_outgoing_args_size;
  cprestore_size = (TARGET_ABICALLS && !TARGET_NEWABI
		    ? MIPS_STACK_ALIGN (UNITS_PER_WORD) : 0);

  /* The space set aside by STARTING_FRAME_OFFSET isn't needed in leaf
     functions.  If the function has local variables, we're committed
     to allocating it anyway.  Otherwise reclaim it here.  */
  if (current_function_is_leaf)
    cprestore_size = args_size = 0;

  /* The MIPS 3.0 linker does not like functions that dynamically
     allocate the stack and have 0 for STACK_DYNAMIC_OFFSET, since it
     looks like we are trying to create a second frame pointer to the
     function, so allocate some stack space to make it happy.  */

  if (args_size == 0 && current_function_calls_alloca)
    args_size = 4 * UNITS_PER_WORD;

  total_size = var_size + args_size + cprestore_size;

  /* Calculate space needed for gp registers.  */
  for (regno = GP_REG_FIRST; regno <= GP_REG_LAST; regno++)
    if (mips_save_reg_p (regno))
      {
	gp_reg_size += GET_MODE_SIZE (gpr_mode);
	mask |= 1 << (regno - GP_REG_FIRST);
      }

  /* We need to restore these for the handler.  */
  if (current_function_calls_eh_return)
    {
      unsigned int i;
      for (i = 0; ; ++i)
	{
	  regno = EH_RETURN_DATA_REGNO (i);
	  if (regno == INVALID_REGNUM)
	    break;
	  gp_reg_size += GET_MODE_SIZE (gpr_mode);
	  mask |= 1 << (regno - GP_REG_FIRST);
	}
    }

  /* This loop must iterate over the same space as its companion in
     save_restore_insns.  */
  for (regno = (FP_REG_LAST - FP_INC + 1);
       regno >= FP_REG_FIRST;
       regno -= FP_INC)
    {
      if (mips_save_reg_p (regno))
	{
	  fp_reg_size += FP_INC * UNITS_PER_FPREG;
	  fmask |= ((1 << FP_INC) - 1) << (regno - FP_REG_FIRST);
	}
    }

  gp_reg_rounded = MIPS_STACK_ALIGN (gp_reg_size);
  total_size += gp_reg_rounded + MIPS_STACK_ALIGN (fp_reg_size);

  /* Add in the space required for saving incoming register arguments.  */
  total_size += current_function_pretend_args_size;
  total_size += MIPS_STACK_ALIGN (cfun->machine->varargs_size);

  /* Save other computed information.  */
  cfun->machine->frame.total_size = total_size;
  cfun->machine->frame.var_size = var_size;
  cfun->machine->frame.args_size = args_size;
  cfun->machine->frame.cprestore_size = cprestore_size;
  cfun->machine->frame.gp_reg_size = gp_reg_size;
  cfun->machine->frame.fp_reg_size = fp_reg_size;
  cfun->machine->frame.mask = mask;
  cfun->machine->frame.fmask = fmask;
  cfun->machine->frame.initialized = reload_completed;
  cfun->machine->frame.num_gp = gp_reg_size / UNITS_PER_WORD;
  cfun->machine->frame.num_fp = fp_reg_size / (FP_INC * UNITS_PER_FPREG);

  if (mask)
    {
      HOST_WIDE_INT offset;

      offset = (args_size + cprestore_size + var_size
		+ gp_reg_size - GET_MODE_SIZE (gpr_mode));
      cfun->machine->frame.gp_sp_offset = offset;
      cfun->machine->frame.gp_save_offset = offset - total_size;
    }
  else
    {
      cfun->machine->frame.gp_sp_offset = 0;
      cfun->machine->frame.gp_save_offset = 0;
    }

  if (fmask)
    {
      HOST_WIDE_INT offset;

      offset = (args_size + cprestore_size + var_size
		+ gp_reg_rounded + fp_reg_size
		- FP_INC * UNITS_PER_FPREG);
      cfun->machine->frame.fp_sp_offset = offset;
      cfun->machine->frame.fp_save_offset = offset - total_size;
    }
  else
    {
      cfun->machine->frame.fp_sp_offset = 0;
      cfun->machine->frame.fp_save_offset = 0;
    }

  /* Ok, we're done.  */
  return total_size;
}

/* Implement INITIAL_ELIMINATION_OFFSET.  FROM is either the frame
   pointer or argument pointer.  TO is either the stack pointer or
   hard frame pointer.  */

HOST_WIDE_INT
mips_initial_elimination_offset (int from, int to)
{
  HOST_WIDE_INT offset;

  compute_frame_size (get_frame_size ());

  /* Set OFFSET to the offset from the stack pointer.  */
  switch (from)
    {
    case FRAME_POINTER_REGNUM:
      offset = (cfun->machine->frame.args_size
		+ cfun->machine->frame.cprestore_size
		+ cfun->machine->frame.var_size);
      break;

    case ARG_POINTER_REGNUM:
      offset = (cfun->machine->frame.total_size
		- current_function_pretend_args_size);
      break;

    default:
      gcc_unreachable ();
    }

  if (TARGET_MIPS16 && to == HARD_FRAME_POINTER_REGNUM)
    offset -= cfun->machine->frame.args_size;

  return offset;
}

/* Implement RETURN_ADDR_RTX.  Note, we do not support moving
   back to a previous frame.  */
rtx
mips_return_addr (int count, rtx frame ATTRIBUTE_UNUSED)
{
  if (count != 0)
    return const0_rtx;

  return get_hard_reg_initial_val (Pmode, GP_REG_FIRST + 31);
}

/* Use FN to save or restore register REGNO.  MODE is the register's
   mode and OFFSET is the offset of its save slot from the current
   stack pointer.  */

static void
mips_save_restore_reg (enum machine_mode mode, int regno,
		       HOST_WIDE_INT offset, mips_save_restore_fn fn)
{
  rtx mem;

  mem = gen_frame_mem (mode, plus_constant (stack_pointer_rtx, offset));

  fn (gen_rtx_REG (mode, regno), mem);
}


/* Call FN for each register that is saved by the current function.
   SP_OFFSET is the offset of the current stack pointer from the start
   of the frame.  */

static void
mips_for_each_saved_reg (HOST_WIDE_INT sp_offset, mips_save_restore_fn fn)
{
#define BITSET_P(VALUE, BIT) (((VALUE) & (1L << (BIT))) != 0)

  enum machine_mode fpr_mode;
  HOST_WIDE_INT offset;
  int regno;

  /* Save registers starting from high to low.  The debuggers prefer at least
     the return register be stored at func+4, and also it allows us not to
     need a nop in the epilog if at least one register is reloaded in
     addition to return address.  */
  offset = cfun->machine->frame.gp_sp_offset - sp_offset;
  for (regno = GP_REG_LAST; regno >= GP_REG_FIRST; regno--)
    if (BITSET_P (cfun->machine->frame.mask, regno - GP_REG_FIRST))
      {
	mips_save_restore_reg (gpr_mode, regno, offset, fn);
	offset -= GET_MODE_SIZE (gpr_mode);
      }

  /* This loop must iterate over the same space as its companion in
     compute_frame_size.  */
  offset = cfun->machine->frame.fp_sp_offset - sp_offset;
  fpr_mode = (TARGET_SINGLE_FLOAT ? SFmode : DFmode);
  for (regno = (FP_REG_LAST - FP_INC + 1);
       regno >= FP_REG_FIRST;
       regno -= FP_INC)
    if (BITSET_P (cfun->machine->frame.fmask, regno - FP_REG_FIRST))
      {
	mips_save_restore_reg (fpr_mode, regno, offset, fn);
	offset -= GET_MODE_SIZE (fpr_mode);
      }
#undef BITSET_P
}

/* If we're generating n32 or n64 abicalls, and the current function
   does not use $28 as its global pointer, emit a cplocal directive.
   Use pic_offset_table_rtx as the argument to the directive.  */

static void
mips_output_cplocal (void)
{
  if (!TARGET_EXPLICIT_RELOCS
      && cfun->machine->global_pointer > 0
      && cfun->machine->global_pointer != GLOBAL_POINTER_REGNUM)
    output_asm_insn (".cplocal %+", 0);
}

/* Return the style of GP load sequence that is being used for the
   current function.  */

enum mips_loadgp_style
mips_current_loadgp_style (void)
{
  if (!TARGET_ABICALLS || cfun->machine->global_pointer == 0)
    return LOADGP_NONE;

  if (TARGET_ABSOLUTE_ABICALLS)
    return LOADGP_ABSOLUTE;

  return TARGET_NEWABI ? LOADGP_NEWABI : LOADGP_OLDABI;
}

/* The __gnu_local_gp symbol.  */

static GTY(()) rtx mips_gnu_local_gp;

/* If we're generating n32 or n64 abicalls, emit instructions
   to set up the global pointer.  */

static void
mips_emit_loadgp (void)
{
  rtx addr, offset, incoming_address;

  switch (mips_current_loadgp_style ())
    {
    case LOADGP_ABSOLUTE:
      if (mips_gnu_local_gp == NULL)
	{
	  mips_gnu_local_gp = gen_rtx_SYMBOL_REF (Pmode, "__gnu_local_gp");
	  SYMBOL_REF_FLAGS (mips_gnu_local_gp) |= SYMBOL_FLAG_LOCAL;
	}
      emit_insn (gen_loadgp_noshared (mips_gnu_local_gp));
      break;

    case LOADGP_NEWABI:
      addr = XEXP (DECL_RTL (current_function_decl), 0);
      offset = mips_unspec_address (addr, SYMBOL_GOTOFF_LOADGP);
      incoming_address = gen_rtx_REG (Pmode, PIC_FUNCTION_ADDR_REGNUM);
      emit_insn (gen_loadgp (offset, incoming_address));
      if (!TARGET_EXPLICIT_RELOCS)
	emit_insn (gen_loadgp_blockage ());
      break;

    default:
      break;
    }
}

/* Set up the stack and frame (if desired) for the function.  */

static void
mips_output_function_prologue (FILE *file, HOST_WIDE_INT size ATTRIBUTE_UNUSED)
{
  const char *fnname;
  HOST_WIDE_INT tsize = cfun->machine->frame.total_size;

#ifdef SDB_DEBUGGING_INFO
  if (debug_info_level != DINFO_LEVEL_TERSE && write_symbols == SDB_DEBUG)
    SDB_OUTPUT_SOURCE_LINE (file, DECL_SOURCE_LINE (current_function_decl));
#endif

  /* In mips16 mode, we may need to generate a 32 bit to handle
     floating point arguments.  The linker will arrange for any 32 bit
     functions to call this stub, which will then jump to the 16 bit
     function proper.  */
  if (TARGET_MIPS16 && !TARGET_SOFT_FLOAT
      && current_function_args_info.fp_code != 0)
    build_mips16_function_stub (file);

  if (!FUNCTION_NAME_ALREADY_DECLARED)
    {
      /* Get the function name the same way that toplev.c does before calling
	 assemble_start_function.  This is needed so that the name used here
	 exactly matches the name used in ASM_DECLARE_FUNCTION_NAME.  */
      fnname = XSTR (XEXP (DECL_RTL (current_function_decl), 0), 0);

      if (!flag_inhibit_size_directive)
	{
	  fputs ("\t.ent\t", file);
	  assemble_name (file, fnname);
	  fputs ("\n", file);
	}

      assemble_name (file, fnname);
      fputs (":\n", file);
    }

  /* Stop mips_file_end from treating this function as external.  */
  if (TARGET_IRIX && mips_abi == ABI_32)
    TREE_ASM_WRITTEN (DECL_NAME (cfun->decl)) = 1;

  if (!flag_inhibit_size_directive)
    {
      /* .frame FRAMEREG, FRAMESIZE, RETREG */
      fprintf (file,
	       "\t.frame\t%s," HOST_WIDE_INT_PRINT_DEC ",%s\t\t"
	       "# vars= " HOST_WIDE_INT_PRINT_DEC ", regs= %d/%d"
	       ", args= " HOST_WIDE_INT_PRINT_DEC
	       ", gp= " HOST_WIDE_INT_PRINT_DEC "\n",
	       (reg_names[(frame_pointer_needed)
			  ? HARD_FRAME_POINTER_REGNUM : STACK_POINTER_REGNUM]),
	       ((frame_pointer_needed && TARGET_MIPS16)
		? tsize - cfun->machine->frame.args_size
		: tsize),
	       reg_names[GP_REG_FIRST + 31],
	       cfun->machine->frame.var_size,
	       cfun->machine->frame.num_gp,
	       cfun->machine->frame.num_fp,
	       cfun->machine->frame.args_size,
	       cfun->machine->frame.cprestore_size);

      /* .mask MASK, GPOFFSET; .fmask FPOFFSET */
      fprintf (file, "\t.mask\t0x%08x," HOST_WIDE_INT_PRINT_DEC "\n",
	       cfun->machine->frame.mask,
	       cfun->machine->frame.gp_save_offset);
      fprintf (file, "\t.fmask\t0x%08x," HOST_WIDE_INT_PRINT_DEC "\n",
	       cfun->machine->frame.fmask,
	       cfun->machine->frame.fp_save_offset);

      /* Require:
	 OLD_SP == *FRAMEREG + FRAMESIZE => can find old_sp from nominated FP reg.
	 HIGHEST_GP_SAVED == *FRAMEREG + FRAMESIZE + GPOFFSET => can find saved regs.  */
    }

  if (mips_current_loadgp_style () == LOADGP_OLDABI)
    {
      /* Handle the initialization of $gp for SVR4 PIC.  */
      if (!cfun->machine->all_noreorder_p)
	output_asm_insn ("%(.cpload\t%^%)", 0);
      else
	output_asm_insn ("%(.cpload\t%^\n\t%<", 0);
    }
  else if (cfun->machine->all_noreorder_p)
    output_asm_insn ("%(%<", 0);

  /* Tell the assembler which register we're using as the global
     pointer.  This is needed for thunks, since they can use either
     explicit relocs or assembler macros.  */
  mips_output_cplocal ();
}

/* Make the last instruction frame related and note that it performs
   the operation described by FRAME_PATTERN.  */

static void
mips_set_frame_expr (rtx frame_pattern)
{
  rtx insn;

  insn = get_last_insn ();
  RTX_FRAME_RELATED_P (insn) = 1;
  REG_NOTES (insn) = alloc_EXPR_LIST (REG_FRAME_RELATED_EXPR,
				      frame_pattern,
				      REG_NOTES (insn));
}


/* Return a frame-related rtx that stores REG at MEM.
   REG must be a single register.  */

static rtx
mips_frame_set (rtx mem, rtx reg)
{
  rtx set;

  /* If we're saving the return address register and the dwarf return
     address column differs from the hard register number, adjust the
     note reg to refer to the former.  */
  if (REGNO (reg) == GP_REG_FIRST + 31
      && DWARF_FRAME_RETURN_COLUMN != GP_REG_FIRST + 31)
    reg = gen_rtx_REG (GET_MODE (reg), DWARF_FRAME_RETURN_COLUMN);

  set = gen_rtx_SET (VOIDmode, mem, reg);
  RTX_FRAME_RELATED_P (set) = 1;

  return set;
}


/* Save register REG to MEM.  Make the instruction frame-related.  */

static void
mips_save_reg (rtx reg, rtx mem)
{
  if (GET_MODE (reg) == DFmode && !TARGET_FLOAT64)
    {
      rtx x1, x2;

      if (mips_split_64bit_move_p (mem, reg))
	mips_split_64bit_move (mem, reg);
      else
	emit_move_insn (mem, reg);

      x1 = mips_frame_set (mips_subword (mem, 0), mips_subword (reg, 0));
      x2 = mips_frame_set (mips_subword (mem, 1), mips_subword (reg, 1));
      mips_set_frame_expr (gen_rtx_PARALLEL (VOIDmode, gen_rtvec (2, x1, x2)));
    }
  else
    {
      if (TARGET_MIPS16
	  && REGNO (reg) != GP_REG_FIRST + 31
	  && !M16_REG_P (REGNO (reg)))
	{
	  /* Save a non-mips16 register by moving it through a temporary.
	     We don't need to do this for $31 since there's a special
	     instruction for it.  */
	  emit_move_insn (MIPS_PROLOGUE_TEMP (GET_MODE (reg)), reg);
	  emit_move_insn (mem, MIPS_PROLOGUE_TEMP (GET_MODE (reg)));
	}
      else
	emit_move_insn (mem, reg);

      mips_set_frame_expr (mips_frame_set (mem, reg));
    }
}


/* Expand the prologue into a bunch of separate insns.  */

void
mips_expand_prologue (void)
{
  HOST_WIDE_INT size;

  if (cfun->machine->global_pointer > 0)
    REGNO (pic_offset_table_rtx) = cfun->machine->global_pointer;

  size = compute_frame_size (get_frame_size ());

  /* Save the registers.  Allocate up to MIPS_MAX_FIRST_STACK_STEP
     bytes beforehand; this is enough to cover the register save area
     without going out of range.  */
  if ((cfun->machine->frame.mask | cfun->machine->frame.fmask) != 0)
    {
      HOST_WIDE_INT step1;

      step1 = MIN (size, MIPS_MAX_FIRST_STACK_STEP);
      RTX_FRAME_RELATED_P (emit_insn (gen_add3_insn (stack_pointer_rtx,
						     stack_pointer_rtx,
						     GEN_INT (-step1)))) = 1;
      size -= step1;
      mips_for_each_saved_reg (size, mips_save_reg);
    }

  /* Allocate the rest of the frame.  */
  if (size > 0)
    {
      if (SMALL_OPERAND (-size))
	RTX_FRAME_RELATED_P (emit_insn (gen_add3_insn (stack_pointer_rtx,
						       stack_pointer_rtx,
						       GEN_INT (-size)))) = 1;
      else
	{
	  emit_move_insn (MIPS_PROLOGUE_TEMP (Pmode), GEN_INT (size));
	  if (TARGET_MIPS16)
	    {
	      /* There are no instructions to add or subtract registers
		 from the stack pointer, so use the frame pointer as a
		 temporary.  We should always be using a frame pointer
		 in this case anyway.  */
	      gcc_assert (frame_pointer_needed);
	      emit_move_insn (hard_frame_pointer_rtx, stack_pointer_rtx);
	      emit_insn (gen_sub3_insn (hard_frame_pointer_rtx,
					hard_frame_pointer_rtx,
					MIPS_PROLOGUE_TEMP (Pmode)));
	      emit_move_insn (stack_pointer_rtx, hard_frame_pointer_rtx);
	    }
	  else
	    emit_insn (gen_sub3_insn (stack_pointer_rtx,
				      stack_pointer_rtx,
				      MIPS_PROLOGUE_TEMP (Pmode)));

	  /* Describe the combined effect of the previous instructions.  */
	  mips_set_frame_expr
	    (gen_rtx_SET (VOIDmode, stack_pointer_rtx,
			  plus_constant (stack_pointer_rtx, -size)));
	}
    }

  /* Set up the frame pointer, if we're using one.  In mips16 code,
     we point the frame pointer ahead of the outgoing argument area.
     This should allow more variables & incoming arguments to be
     accessed with unextended instructions.  */
  if (frame_pointer_needed)
    {
      if (TARGET_MIPS16 && cfun->machine->frame.args_size != 0)
	{
	  rtx offset = GEN_INT (cfun->machine->frame.args_size);
	  if (SMALL_OPERAND (cfun->machine->frame.args_size))
	    RTX_FRAME_RELATED_P 
	      (emit_insn (gen_add3_insn (hard_frame_pointer_rtx,
					 stack_pointer_rtx,
					 offset))) = 1;
	  else
	    {
	      emit_move_insn (MIPS_PROLOGUE_TEMP (Pmode), offset);
	      emit_move_insn (hard_frame_pointer_rtx, stack_pointer_rtx);
	      emit_insn (gen_add3_insn (hard_frame_pointer_rtx,
					hard_frame_pointer_rtx,
					MIPS_PROLOGUE_TEMP (Pmode)));
	      mips_set_frame_expr
		(gen_rtx_SET (VOIDmode, hard_frame_pointer_rtx,
			      plus_constant (stack_pointer_rtx, 
					     cfun->machine->frame.args_size)));
	    }
	}
      else
	RTX_FRAME_RELATED_P (emit_move_insn (hard_frame_pointer_rtx,
					     stack_pointer_rtx)) = 1;
    }

  mips_emit_loadgp ();

  /* If generating o32/o64 abicalls, save $gp on the stack.  */
  if (TARGET_ABICALLS && !TARGET_NEWABI && !current_function_is_leaf)
    emit_insn (gen_cprestore (GEN_INT (current_function_outgoing_args_size)));

  /* If we are profiling, make sure no instructions are scheduled before
     the call to mcount.  */

  if (current_function_profile)
    emit_insn (gen_blockage ());
}

/* Do any necessary cleanup after a function to restore stack, frame,
   and regs.  */

#define RA_MASK BITMASK_HIGH	/* 1 << 31 */

static void
mips_output_function_epilogue (FILE *file ATTRIBUTE_UNUSED,
			       HOST_WIDE_INT size ATTRIBUTE_UNUSED)
{
  /* Reinstate the normal $gp.  */
  REGNO (pic_offset_table_rtx) = GLOBAL_POINTER_REGNUM;
  mips_output_cplocal ();

  if (cfun->machine->all_noreorder_p)
    {
      /* Avoid using %>%) since it adds excess whitespace.  */
      output_asm_insn (".set\tmacro", 0);
      output_asm_insn (".set\treorder", 0);
      set_noreorder = set_nomacro = 0;
    }

  if (!FUNCTION_NAME_ALREADY_DECLARED && !flag_inhibit_size_directive)
    {
      const char *fnname;

      /* Get the function name the same way that toplev.c does before calling
	 assemble_start_function.  This is needed so that the name used here
	 exactly matches the name used in ASM_DECLARE_FUNCTION_NAME.  */
      fnname = XSTR (XEXP (DECL_RTL (current_function_decl), 0), 0);
      fputs ("\t.end\t", file);
      assemble_name (file, fnname);
      fputs ("\n", file);
    }
}

/* Emit instructions to restore register REG from slot MEM.  */

static void
mips_restore_reg (rtx reg, rtx mem)
{
  /* There's no mips16 instruction to load $31 directly.  Load into
     $7 instead and adjust the return insn appropriately.  */
  if (TARGET_MIPS16 && REGNO (reg) == GP_REG_FIRST + 31)
    reg = gen_rtx_REG (GET_MODE (reg), 7);

  if (TARGET_MIPS16 && !M16_REG_P (REGNO (reg)))
    {
      /* Can't restore directly; move through a temporary.  */
      emit_move_insn (MIPS_EPILOGUE_TEMP (GET_MODE (reg)), mem);
      emit_move_insn (reg, MIPS_EPILOGUE_TEMP (GET_MODE (reg)));
    }
  else
    emit_move_insn (reg, mem);
}


/* Expand the epilogue into a bunch of separate insns.  SIBCALL_P is true
   if this epilogue precedes a sibling call, false if it is for a normal
   "epilogue" pattern.  */

void
mips_expand_epilogue (int sibcall_p)
{
  HOST_WIDE_INT step1, step2;
  rtx base, target;

  if (!sibcall_p && mips_can_use_return_insn ())
    {
      emit_jump_insn (gen_return ());
      return;
    }

  /* Split the frame into two.  STEP1 is the amount of stack we should
     deallocate before restoring the registers.  STEP2 is the amount we
     should deallocate afterwards.

     Start off by assuming that no registers need to be restored.  */
  step1 = cfun->machine->frame.total_size;
  step2 = 0;

  /* Work out which register holds the frame address.  Account for the
     frame pointer offset used by mips16 code.  */
  if (!frame_pointer_needed)
    base = stack_pointer_rtx;
  else
    {
      base = hard_frame_pointer_rtx;
      if (TARGET_MIPS16)
	step1 -= cfun->machine->frame.args_size;
    }

  /* If we need to restore registers, deallocate as much stack as
     possible in the second step without going out of range.  */
  if ((cfun->machine->frame.mask | cfun->machine->frame.fmask) != 0)
    {
      step2 = MIN (step1, MIPS_MAX_FIRST_STACK_STEP);
      step1 -= step2;
    }

  /* Set TARGET to BASE + STEP1.  */
  target = base;
  if (step1 > 0)
    {
      rtx adjust;

      /* Get an rtx for STEP1 that we can add to BASE.  */
      adjust = GEN_INT (step1);
      if (!SMALL_OPERAND (step1))
	{
	  emit_move_insn (MIPS_EPILOGUE_TEMP (Pmode), adjust);
	  adjust = MIPS_EPILOGUE_TEMP (Pmode);
	}

      /* Normal mode code can copy the result straight into $sp.  */
      if (!TARGET_MIPS16)
	target = stack_pointer_rtx;

      emit_insn (gen_add3_insn (target, base, adjust));
    }

  /* Copy TARGET into the stack pointer.  */
  if (target != stack_pointer_rtx)
    emit_move_insn (stack_pointer_rtx, target);

  /* If we're using addressing macros for n32/n64 abicalls, $gp is
     implicitly used by all SYMBOL_REFs.  We must emit a blockage
     insn before restoring it.  */
  if (TARGET_ABICALLS && TARGET_NEWABI && !TARGET_EXPLICIT_RELOCS)
    emit_insn (gen_blockage ());

  /* Restore the registers.  */
  mips_for_each_saved_reg (cfun->machine->frame.total_size - step2,
			   mips_restore_reg);

  /* Deallocate the final bit of the frame.  */
  if (step2 > 0)
    emit_insn (gen_add3_insn (stack_pointer_rtx,
			      stack_pointer_rtx,
			      GEN_INT (step2)));

  /* Add in the __builtin_eh_return stack adjustment.  We need to
     use a temporary in mips16 code.  */
  if (current_function_calls_eh_return)
    {
      if (TARGET_MIPS16)
	{
	  emit_move_insn (MIPS_EPILOGUE_TEMP (Pmode), stack_pointer_rtx);
	  emit_insn (gen_add3_insn (MIPS_EPILOGUE_TEMP (Pmode),
				    MIPS_EPILOGUE_TEMP (Pmode),
				    EH_RETURN_STACKADJ_RTX));
	  emit_move_insn (stack_pointer_rtx, MIPS_EPILOGUE_TEMP (Pmode));
	}
      else
	emit_insn (gen_add3_insn (stack_pointer_rtx,
				  stack_pointer_rtx,
				  EH_RETURN_STACKADJ_RTX));
    }

  if (!sibcall_p)
    {
      /* The mips16 loads the return address into $7, not $31.  */
      if (TARGET_MIPS16 && (cfun->machine->frame.mask & RA_MASK) != 0)
	emit_jump_insn (gen_return_internal (gen_rtx_REG (Pmode,
							  GP_REG_FIRST + 7)));
      else
	emit_jump_insn (gen_return_internal (gen_rtx_REG (Pmode,
							  GP_REG_FIRST + 31)));
    }
}

/* Return nonzero if this function is known to have a null epilogue.
   This allows the optimizer to omit jumps to jumps if no stack
   was created.  */

int
mips_can_use_return_insn (void)
{
  tree return_type;

  if (! reload_completed)
    return 0;

  if (regs_ever_live[31] || current_function_profile)
    return 0;

  return_type = DECL_RESULT (current_function_decl);

  /* In mips16 mode, a function which returns a floating point value
     needs to arrange to copy the return value into the floating point
     registers.  */
  if (TARGET_MIPS16
      && mips16_hard_float
      && ! aggregate_value_p (return_type, current_function_decl)
      && GET_MODE_CLASS (DECL_MODE (return_type)) == MODE_FLOAT
      && GET_MODE_SIZE (DECL_MODE (return_type)) <= UNITS_PER_FPVALUE)
    return 0;

  if (cfun->machine->frame.initialized)
    return cfun->machine->frame.total_size == 0;

  return compute_frame_size (get_frame_size ()) == 0;
}

/* Implement TARGET_ASM_OUTPUT_MI_THUNK.  Generate rtl rather than asm text
   in order to avoid duplicating too much logic from elsewhere.  */

static void
mips_output_mi_thunk (FILE *file, tree thunk_fndecl ATTRIBUTE_UNUSED,
		      HOST_WIDE_INT delta, HOST_WIDE_INT vcall_offset,
		      tree function)
{
  rtx this, temp1, temp2, insn, fnaddr;

  /* Pretend to be a post-reload pass while generating rtl.  */
  no_new_pseudos = 1;
  reload_completed = 1;
  reset_block_changes ();

  /* Pick a global pointer for -mabicalls.  Use $15 rather than $28
     for TARGET_NEWABI since the latter is a call-saved register.  */
  if (TARGET_ABICALLS)
    cfun->machine->global_pointer
      = REGNO (pic_offset_table_rtx)
      = TARGET_NEWABI ? 15 : GLOBAL_POINTER_REGNUM;

  /* Set up the global pointer for n32 or n64 abicalls.  */
  mips_emit_loadgp ();

  /* We need two temporary registers in some cases.  */
  temp1 = gen_rtx_REG (Pmode, 2);
  temp2 = gen_rtx_REG (Pmode, 3);

  /* Find out which register contains the "this" pointer.  */
  if (aggregate_value_p (TREE_TYPE (TREE_TYPE (function)), function))
    this = gen_rtx_REG (Pmode, GP_ARG_FIRST + 1);
  else
    this = gen_rtx_REG (Pmode, GP_ARG_FIRST);

  /* Add DELTA to THIS.  */
  if (delta != 0)
    {
      rtx offset = GEN_INT (delta);
      if (!SMALL_OPERAND (delta))
	{
	  emit_move_insn (temp1, offset);
	  offset = temp1;
	}
      emit_insn (gen_add3_insn (this, this, offset));
    }

  /* If needed, add *(*THIS + VCALL_OFFSET) to THIS.  */
  if (vcall_offset != 0)
    {
      rtx addr;

      /* Set TEMP1 to *THIS.  */
      emit_move_insn (temp1, gen_rtx_MEM (Pmode, this));

      /* Set ADDR to a legitimate address for *THIS + VCALL_OFFSET.  */
      addr = mips_add_offset (temp2, temp1, vcall_offset);

      /* Load the offset and add it to THIS.  */
      emit_move_insn (temp1, gen_rtx_MEM (Pmode, addr));
      emit_insn (gen_add3_insn (this, this, temp1));
    }

  /* Jump to the target function.  Use a sibcall if direct jumps are
     allowed, otherwise load the address into a register first.  */
  fnaddr = XEXP (DECL_RTL (function), 0);
  if (TARGET_MIPS16 || TARGET_ABICALLS || TARGET_LONG_CALLS)
    {
      /* This is messy.  gas treats "la $25,foo" as part of a call
	 sequence and may allow a global "foo" to be lazily bound.
	 The general move patterns therefore reject this combination.

	 In this context, lazy binding would actually be OK for o32 and o64,
	 but it's still wrong for n32 and n64; see mips_load_call_address.
	 We must therefore load the address via a temporary register if
	 mips_dangerous_for_la25_p.

	 If we jump to the temporary register rather than $25, the assembler
	 can use the move insn to fill the jump's delay slot.  */
      if (TARGET_ABICALLS && !mips_dangerous_for_la25_p (fnaddr))
	temp1 = gen_rtx_REG (Pmode, PIC_FUNCTION_ADDR_REGNUM);
      mips_load_call_address (temp1, fnaddr, true);

      if (TARGET_ABICALLS && REGNO (temp1) != PIC_FUNCTION_ADDR_REGNUM)
	emit_move_insn (gen_rtx_REG (Pmode, PIC_FUNCTION_ADDR_REGNUM), temp1);
      emit_jump_insn (gen_indirect_jump (temp1));
    }
  else
    {
      insn = emit_call_insn (gen_sibcall_internal (fnaddr, const0_rtx));
      SIBLING_CALL_P (insn) = 1;
    }

  /* Run just enough of rest_of_compilation.  This sequence was
     "borrowed" from alpha.c.  */
  insn = get_insns ();
  insn_locators_initialize ();
  split_all_insns_noflow ();
  if (TARGET_MIPS16)
    mips16_lay_out_constants ();
  shorten_branches (insn);
  final_start_function (insn, file, 1);
  final (insn, file, 1);
  final_end_function ();

  /* Clean up the vars set above.  Note that final_end_function resets
     the global pointer for us.  */
  reload_completed = 0;
  no_new_pseudos = 0;
}

/* Returns nonzero if X contains a SYMBOL_REF.  */

static int
symbolic_expression_p (rtx x)
{
  if (GET_CODE (x) == SYMBOL_REF)
    return 1;

  if (GET_CODE (x) == CONST)
    return symbolic_expression_p (XEXP (x, 0));

  if (UNARY_P (x))
    return symbolic_expression_p (XEXP (x, 0));

  if (ARITHMETIC_P (x))
    return (symbolic_expression_p (XEXP (x, 0))
	    || symbolic_expression_p (XEXP (x, 1)));

  return 0;
}

/* Choose the section to use for the constant rtx expression X that has
   mode MODE.  */

static section *
mips_select_rtx_section (enum machine_mode mode, rtx x,
			 unsigned HOST_WIDE_INT align)
{
  if (TARGET_MIPS16)
    {
      /* In mips16 mode, the constant table always goes in the same section
         as the function, so that constants can be loaded using PC relative
         addressing.  */
      return function_section (current_function_decl);
    }
  else if (TARGET_EMBEDDED_DATA)
    {
      /* For embedded applications, always put constants in read-only data,
	 in order to reduce RAM usage.  */
      return mergeable_constant_section (mode, align, 0);
    }
  else
    {
      /* For hosted applications, always put constants in small data if
	 possible, as this gives the best performance.  */
      /* ??? Consider using mergeable small data sections.  */

      if (GET_MODE_SIZE (mode) <= (unsigned) mips_section_threshold
	  && mips_section_threshold > 0)
	return get_named_section (NULL, ".sdata", 0);
      else if (flag_pic && symbolic_expression_p (x))
	return get_named_section (NULL, ".data.rel.ro", 3);
      else
	return mergeable_constant_section (mode, align, 0);
    }
}

/* Implement TARGET_ASM_FUNCTION_RODATA_SECTION.

   The complication here is that, with the combination TARGET_ABICALLS
   && !TARGET_GPWORD, jump tables will use absolute addresses, and should
   therefore not be included in the read-only part of a DSO.  Handle such
   cases by selecting a normal data section instead of a read-only one.
   The logic apes that in default_function_rodata_section.  */

static section *
mips_function_rodata_section (tree decl)
{
  if (!TARGET_ABICALLS || TARGET_GPWORD)
    return default_function_rodata_section (decl);

  if (decl && DECL_SECTION_NAME (decl))
    {
      const char *name = TREE_STRING_POINTER (DECL_SECTION_NAME (decl));
      if (DECL_ONE_ONLY (decl) && strncmp (name, ".gnu.linkonce.t.", 16) == 0)
	{
	  char *rname = ASTRDUP (name);
	  rname[14] = 'd';
	  return get_section (rname, SECTION_LINKONCE | SECTION_WRITE, decl);
	}
      else if (flag_function_sections && flag_data_sections
	       && strncmp (name, ".text.", 6) == 0)
	{
	  char *rname = ASTRDUP (name);
	  memcpy (rname + 1, "data", 4);
	  return get_section (rname, SECTION_WRITE, decl);
	}
    }
  return data_section;
}

/* Implement TARGET_IN_SMALL_DATA_P.  This function controls whether
   locally-defined objects go in a small data section.  It also controls
   the setting of the SYMBOL_REF_SMALL_P flag, which in turn helps
   mips_classify_symbol decide when to use %gp_rel(...)($gp) accesses.  */

static bool
mips_in_small_data_p (tree decl)
{
  HOST_WIDE_INT size;

  if (TREE_CODE (decl) == STRING_CST || TREE_CODE (decl) == FUNCTION_DECL)
    return false;

  /* We don't yet generate small-data references for -mabicalls.  See related
     -G handling in override_options.  */
  if (TARGET_ABICALLS)
    return false;

  if (TREE_CODE (decl) == VAR_DECL && DECL_SECTION_NAME (decl) != 0)
    {
      const char *name;

      /* Reject anything that isn't in a known small-data section.  */
      name = TREE_STRING_POINTER (DECL_SECTION_NAME (decl));
      if (strcmp (name, ".sdata") != 0 && strcmp (name, ".sbss") != 0)
	return false;

      /* If a symbol is defined externally, the assembler will use the
	 usual -G rules when deciding how to implement macros.  */
      if (TARGET_EXPLICIT_RELOCS || !DECL_EXTERNAL (decl))
	return true;
    }
  else if (TARGET_EMBEDDED_DATA)
    {
      /* Don't put constants into the small data section: we want them
	 to be in ROM rather than RAM.  */
      if (TREE_CODE (decl) != VAR_DECL)
	return false;

      if (TREE_READONLY (decl)
	  && !TREE_SIDE_EFFECTS (decl)
	  && (!DECL_INITIAL (decl) || TREE_CONSTANT (DECL_INITIAL (decl))))
	return false;
    }

  size = int_size_in_bytes (TREE_TYPE (decl));
  return (size > 0 && size <= mips_section_threshold);
}

/* Implement TARGET_USE_ANCHORS_FOR_SYMBOL_P.  We don't want to use
   anchors for small data: the GP register acts as an anchor in that
   case.  We also don't want to use them for PC-relative accesses,
   where the PC acts as an anchor.  */

static bool
mips_use_anchors_for_symbol_p (rtx symbol)
{
  switch (mips_classify_symbol (symbol))
    {
    case SYMBOL_CONSTANT_POOL:
    case SYMBOL_SMALL_DATA:
      return false;

    default:
      return true;
    }
}

/* See whether VALTYPE is a record whose fields should be returned in
   floating-point registers.  If so, return the number of fields and
   list them in FIELDS (which should have two elements).  Return 0
   otherwise.

   For n32 & n64, a structure with one or two fields is returned in
   floating-point registers as long as every field has a floating-point
   type.  */

static int
mips_fpr_return_fields (tree valtype, tree *fields)
{
  tree field;
  int i;

  if (!TARGET_NEWABI)
    return 0;

  if (TREE_CODE (valtype) != RECORD_TYPE)
    return 0;

  i = 0;
  for (field = TYPE_FIELDS (valtype); field != 0; field = TREE_CHAIN (field))
    {
      if (TREE_CODE (field) != FIELD_DECL)
	continue;

      if (TREE_CODE (TREE_TYPE (field)) != REAL_TYPE)
	return 0;

      if (i == 2)
	return 0;

      fields[i++] = field;
    }
  return i;
}


/* Implement TARGET_RETURN_IN_MSB.  For n32 & n64, we should return
   a value in the most significant part of $2/$3 if:

      - the target is big-endian;

      - the value has a structure or union type (we generalize this to
	cover aggregates from other languages too); and

      - the structure is not returned in floating-point registers.  */

static bool
mips_return_in_msb (tree valtype)
{
  tree fields[2];

  return (TARGET_NEWABI
	  && TARGET_BIG_ENDIAN
	  && AGGREGATE_TYPE_P (valtype)
	  && mips_fpr_return_fields (valtype, fields) == 0);
}


/* Return a composite value in a pair of floating-point registers.
   MODE1 and OFFSET1 are the mode and byte offset for the first value,
   likewise MODE2 and OFFSET2 for the second.  MODE is the mode of the
   complete value.

   For n32 & n64, $f0 always holds the first value and $f2 the second.
   Otherwise the values are packed together as closely as possible.  */

static rtx
mips_return_fpr_pair (enum machine_mode mode,
		      enum machine_mode mode1, HOST_WIDE_INT offset1,
		      enum machine_mode mode2, HOST_WIDE_INT offset2)
{
  int inc;

  inc = (TARGET_NEWABI ? 2 : FP_INC);
  return gen_rtx_PARALLEL
    (mode,
     gen_rtvec (2,
		gen_rtx_EXPR_LIST (VOIDmode,
				   gen_rtx_REG (mode1, FP_RETURN),
				   GEN_INT (offset1)),
		gen_rtx_EXPR_LIST (VOIDmode,
				   gen_rtx_REG (mode2, FP_RETURN + inc),
				   GEN_INT (offset2))));

}


/* Implement FUNCTION_VALUE and LIBCALL_VALUE.  For normal calls,
   VALTYPE is the return type and MODE is VOIDmode.  For libcalls,
   VALTYPE is null and MODE is the mode of the return value.  */

rtx
mips_function_value (tree valtype, tree func ATTRIBUTE_UNUSED,
		     enum machine_mode mode)
{
  if (valtype)
    {
      tree fields[2];
      int unsignedp;

      mode = TYPE_MODE (valtype);
      unsignedp = TYPE_UNSIGNED (valtype);

      /* Since we define TARGET_PROMOTE_FUNCTION_RETURN that returns
	 true, we must promote the mode just as PROMOTE_MODE does.  */
      mode = promote_mode (valtype, mode, &unsignedp, 1);

      /* Handle structures whose fields are returned in $f0/$f2.  */
      switch (mips_fpr_return_fields (valtype, fields))
	{
	case 1:
	  return gen_rtx_REG (mode, FP_RETURN);

	case 2:
	  return mips_return_fpr_pair (mode,
				       TYPE_MODE (TREE_TYPE (fields[0])),
				       int_byte_position (fields[0]),
				       TYPE_MODE (TREE_TYPE (fields[1])),
				       int_byte_position (fields[1]));
	}

      /* If a value is passed in the most significant part of a register, see
	 whether we have to round the mode up to a whole number of words.  */
      if (mips_return_in_msb (valtype))
	{
	  HOST_WIDE_INT size = int_size_in_bytes (valtype);
	  if (size % UNITS_PER_WORD != 0)
	    {
	      size += UNITS_PER_WORD - size % UNITS_PER_WORD;
	      mode = mode_for_size (size * BITS_PER_UNIT, MODE_INT, 0);
	    }
	}

      /* For EABI, the class of return register depends entirely on MODE.
	 For example, "struct { some_type x; }" and "union { some_type x; }"
	 are returned in the same way as a bare "some_type" would be.
	 Other ABIs only use FPRs for scalar, complex or vector types.  */
      if (mips_abi != ABI_EABI && !FLOAT_TYPE_P (valtype))
	return gen_rtx_REG (mode, GP_RETURN);
    }

  if ((GET_MODE_CLASS (mode) == MODE_FLOAT
       || GET_MODE_CLASS (mode) == MODE_VECTOR_FLOAT)
      && GET_MODE_SIZE (mode) <= UNITS_PER_HWFPVALUE)
    return gen_rtx_REG (mode, FP_RETURN);

  /* Handle long doubles for n32 & n64.  */
  if (mode == TFmode)
    return mips_return_fpr_pair (mode,
				 DImode, 0,
				 DImode, GET_MODE_SIZE (mode) / 2);

  if (GET_MODE_CLASS (mode) == MODE_COMPLEX_FLOAT
      && GET_MODE_SIZE (mode) <= UNITS_PER_HWFPVALUE * 2)
    return mips_return_fpr_pair (mode,
				 GET_MODE_INNER (mode), 0,
				 GET_MODE_INNER (mode),
				 GET_MODE_SIZE (mode) / 2);

  return gen_rtx_REG (mode, GP_RETURN);
}

/* Return nonzero when an argument must be passed by reference.  */

static bool
mips_pass_by_reference (CUMULATIVE_ARGS *cum ATTRIBUTE_UNUSED,
			enum machine_mode mode, tree type,
			bool named ATTRIBUTE_UNUSED)
{
  if (mips_abi == ABI_EABI)
    {
      int size;

      /* ??? How should SCmode be handled?  */
      if (mode == DImode || mode == DFmode)
	return 0;

      size = type ? int_size_in_bytes (type) : GET_MODE_SIZE (mode);
      return size == -1 || size > UNITS_PER_WORD;
    }
  else
    {
      /* If we have a variable-sized parameter, we have no choice.  */
      return targetm.calls.must_pass_in_stack (mode, type);
    }
}

static bool
mips_callee_copies (CUMULATIVE_ARGS *cum ATTRIBUTE_UNUSED,
		    enum machine_mode mode ATTRIBUTE_UNUSED,
		    tree type ATTRIBUTE_UNUSED, bool named)
{
  return mips_abi == ABI_EABI && named;
}

/* Return true if registers of class CLASS cannot change from mode FROM
   to mode TO.  */

bool
mips_cannot_change_mode_class (enum machine_mode from,
			       enum machine_mode to, enum reg_class class)
{
  if (MIN (GET_MODE_SIZE (from), GET_MODE_SIZE (to)) <= UNITS_PER_WORD
      && MAX (GET_MODE_SIZE (from), GET_MODE_SIZE (to)) > UNITS_PER_WORD)
    {
      if (TARGET_BIG_ENDIAN)
	{
	  /* When a multi-word value is stored in paired floating-point
	     registers, the first register always holds the low word.
	     We therefore can't allow FPRs to change between single-word
	     and multi-word modes.  */
	  if (FP_INC > 1 && reg_classes_intersect_p (FP_REGS, class))
	    return true;
	}
      else
	{
	  /* LO_REGNO == HI_REGNO + 1, so if a multi-word value is stored
	     in LO and HI, the high word always comes first.  We therefore
	     can't allow values stored in HI to change between single-word
	     and multi-word modes.
	     This rule applies to both the original HI/LO pair and the new
	     DSP accumulators.  */
	  if (reg_classes_intersect_p (ACC_REGS, class))
	    return true;
	}
    }
  /* Loading a 32-bit value into a 64-bit floating-point register
     will not sign-extend the value, despite what LOAD_EXTEND_OP says.
     We can't allow 64-bit float registers to change from SImode to
     to a wider mode.  */
  if (TARGET_FLOAT64
      && from == SImode
      && GET_MODE_SIZE (to) >= UNITS_PER_WORD
      && reg_classes_intersect_p (FP_REGS, class))
    return true;
  return false;
}

/* Return true if X should not be moved directly into register $25.
   We need this because many versions of GAS will treat "la $25,foo" as
   part of a call sequence and so allow a global "foo" to be lazily bound.  */

bool
mips_dangerous_for_la25_p (rtx x)
{
  HOST_WIDE_INT offset;

  if (TARGET_EXPLICIT_RELOCS)
    return false;

  mips_split_const (x, &x, &offset);
  return global_got_operand (x, VOIDmode);
}

/* Implement PREFERRED_RELOAD_CLASS.  */

enum reg_class
mips_preferred_reload_class (rtx x, enum reg_class class)
{
  if (mips_dangerous_for_la25_p (x) && reg_class_subset_p (LEA_REGS, class))
    return LEA_REGS;

  if (TARGET_HARD_FLOAT
      && FLOAT_MODE_P (GET_MODE (x))
      && reg_class_subset_p (FP_REGS, class))
    return FP_REGS;

  if (reg_class_subset_p (GR_REGS, class))
    class = GR_REGS;

  if (TARGET_MIPS16 && reg_class_subset_p (M16_REGS, class))
    class = M16_REGS;

  return class;
}

/* This function returns the register class required for a secondary
   register when copying between one of the registers in CLASS, and X,
   using MODE.  If IN_P is nonzero, the copy is going from X to the
   register, otherwise the register is the source.  A return value of
   NO_REGS means that no secondary register is required.  */

enum reg_class
mips_secondary_reload_class (enum reg_class class,
			     enum machine_mode mode, rtx x, int in_p)
{
  enum reg_class gr_regs = TARGET_MIPS16 ? M16_REGS : GR_REGS;
  int regno = -1;
  int gp_reg_p;

  if (REG_P (x)|| GET_CODE (x) == SUBREG)
    regno = true_regnum (x);

  gp_reg_p = TARGET_MIPS16 ? M16_REG_P (regno) : GP_REG_P (regno);

  if (mips_dangerous_for_la25_p (x))
    {
      gr_regs = LEA_REGS;
      if (TEST_HARD_REG_BIT (reg_class_contents[(int) class], 25))
	return gr_regs;
    }

  /* Copying from HI or LO to anywhere other than a general register
     requires a general register.
     This rule applies to both the original HI/LO pair and the new
     DSP accumulators.  */
  if (reg_class_subset_p (class, ACC_REGS))
    {
      if (TARGET_MIPS16 && in_p)
	{
	  /* We can't really copy to HI or LO at all in mips16 mode.  */
	  return M16_REGS;
	}
      return gp_reg_p ? NO_REGS : gr_regs;
    }
  if (ACC_REG_P (regno))
    {
      if (TARGET_MIPS16 && ! in_p)
	{
	  /* We can't really copy to HI or LO at all in mips16 mode.  */
	  return M16_REGS;
	}
      return class == gr_regs ? NO_REGS : gr_regs;
    }

  /* We can only copy a value to a condition code register from a
     floating point register, and even then we require a scratch
     floating point register.  We can only copy a value out of a
     condition code register into a general register.  */
  if (class == ST_REGS)
    {
      if (in_p)
	return FP_REGS;
      return gp_reg_p ? NO_REGS : gr_regs;
    }
  if (ST_REG_P (regno))
    {
      if (! in_p)
	return FP_REGS;
      return class == gr_regs ? NO_REGS : gr_regs;
    }

  if (class == FP_REGS)
    {
      if (MEM_P (x))
	{
	  /* In this case we can use lwc1, swc1, ldc1 or sdc1.  */
	  return NO_REGS;
	}
      else if (CONSTANT_P (x) && GET_MODE_CLASS (mode) == MODE_FLOAT)
	{
	  /* We can use the l.s and l.d macros to load floating-point
	     constants.  ??? For l.s, we could probably get better
	     code by returning GR_REGS here.  */
	  return NO_REGS;
	}
      else if (gp_reg_p || x == CONST0_RTX (mode))
	{
	  /* In this case we can use mtc1, mfc1, dmtc1 or dmfc1.  */
	  return NO_REGS;
	}
      else if (FP_REG_P (regno))
	{
	  /* In this case we can use mov.s or mov.d.  */
	  return NO_REGS;
	}
      else
	{
	  /* Otherwise, we need to reload through an integer register.  */
	  return gr_regs;
	}
    }

  /* In mips16 mode, going between memory and anything but M16_REGS
     requires an M16_REG.  */
  if (TARGET_MIPS16)
    {
      if (class != M16_REGS && class != M16_NA_REGS)
	{
	  if (gp_reg_p)
	    return NO_REGS;
	  return M16_REGS;
	}
      if (! gp_reg_p)
	{
	  if (class == M16_REGS || class == M16_NA_REGS)
	    return NO_REGS;
	  return M16_REGS;
	}
    }

  return NO_REGS;
}

/* Implement CLASS_MAX_NREGS.

   Usually all registers are word-sized.  The only supported exception
   is -mgp64 -msingle-float, which has 64-bit words but 32-bit float
   registers.  A word-based calculation is correct even in that case,
   since -msingle-float disallows multi-FPR values.

   The FP status registers are an exception to this rule.  They are always
   4 bytes wide as they only hold condition code modes, and CCmode is always
   considered to be 4 bytes wide.  */

int
mips_class_max_nregs (enum reg_class class ATTRIBUTE_UNUSED,
		      enum machine_mode mode)
{
  if (class == ST_REGS)
    return (GET_MODE_SIZE (mode) + 3) / 4;
  else
    return (GET_MODE_SIZE (mode) + UNITS_PER_WORD - 1) / UNITS_PER_WORD;
}

static bool
mips_valid_pointer_mode (enum machine_mode mode)
{
  return (mode == SImode || (TARGET_64BIT && mode == DImode));
}

/* Target hook for vector_mode_supported_p.  */

static bool
mips_vector_mode_supported_p (enum machine_mode mode)
{
  switch (mode)
    {
    case V2SFmode:
      return TARGET_PAIRED_SINGLE_FLOAT;

    case V2HImode:
    case V4QImode:
      return TARGET_DSP;

    default:
      return false;
    }
}

/* If we can access small data directly (using gp-relative relocation
   operators) return the small data pointer, otherwise return null.

   For each mips16 function which refers to GP relative symbols, we
   use a pseudo register, initialized at the start of the function, to
   hold the $gp value.  */

static rtx
mips16_gp_pseudo_reg (void)
{
  if (cfun->machine->mips16_gp_pseudo_rtx == NULL_RTX)
    {
      rtx unspec;
      rtx insn, scan;

      cfun->machine->mips16_gp_pseudo_rtx = gen_reg_rtx (Pmode);

      /* We want to initialize this to a value which gcc will believe
         is constant.  */
      start_sequence ();
      unspec = gen_rtx_UNSPEC (VOIDmode, gen_rtvec (1, const0_rtx), UNSPEC_GP);
      emit_move_insn (cfun->machine->mips16_gp_pseudo_rtx,
		      gen_rtx_CONST (Pmode, unspec));
      insn = get_insns ();
      end_sequence ();

      push_topmost_sequence ();
      /* We need to emit the initialization after the FUNCTION_BEG
         note, so that it will be integrated.  */
      for (scan = get_insns (); scan != NULL_RTX; scan = NEXT_INSN (scan))
	if (NOTE_P (scan)
	    && NOTE_LINE_NUMBER (scan) == NOTE_INSN_FUNCTION_BEG)
	  break;
      if (scan == NULL_RTX)
	scan = get_insns ();
      insn = emit_insn_after (insn, scan);
      pop_topmost_sequence ();
    }

  return cfun->machine->mips16_gp_pseudo_rtx;
}

/* Write out code to move floating point arguments in or out of
   general registers.  Output the instructions to FILE.  FP_CODE is
   the code describing which arguments are present (see the comment at
   the definition of CUMULATIVE_ARGS in mips.h).  FROM_FP_P is nonzero if
   we are copying from the floating point registers.  */

static void
mips16_fp_args (FILE *file, int fp_code, int from_fp_p)
{
  const char *s;
  int gparg, fparg;
  unsigned int f;

  /* This code only works for the original 32 bit ABI and the O64 ABI.  */
  gcc_assert (TARGET_OLDABI);

  if (from_fp_p)
    s = "mfc1";
  else
    s = "mtc1";
  gparg = GP_ARG_FIRST;
  fparg = FP_ARG_FIRST;
  for (f = (unsigned int) fp_code; f != 0; f >>= 2)
    {
      if ((f & 3) == 1)
	{
	  if ((fparg & 1) != 0)
	    ++fparg;
	  fprintf (file, "\t%s\t%s,%s\n", s,
		   reg_names[gparg], reg_names[fparg]);
	}
      else if ((f & 3) == 2)
	{
	  if (TARGET_64BIT)
	    fprintf (file, "\td%s\t%s,%s\n", s,
		     reg_names[gparg], reg_names[fparg]);
	  else
	    {
	      if ((fparg & 1) != 0)
		++fparg;
	      if (TARGET_BIG_ENDIAN)
		fprintf (file, "\t%s\t%s,%s\n\t%s\t%s,%s\n", s,
			 reg_names[gparg], reg_names[fparg + 1], s,
			 reg_names[gparg + 1], reg_names[fparg]);
	      else
		fprintf (file, "\t%s\t%s,%s\n\t%s\t%s,%s\n", s,
			 reg_names[gparg], reg_names[fparg], s,
			 reg_names[gparg + 1], reg_names[fparg + 1]);
	      ++gparg;
	      ++fparg;
	    }
	}
      else
	gcc_unreachable ();

      ++gparg;
      ++fparg;
    }
}

/* Build a mips16 function stub.  This is used for functions which
   take arguments in the floating point registers.  It is 32 bit code
   that moves the floating point args into the general registers, and
   then jumps to the 16 bit code.  */

static void
build_mips16_function_stub (FILE *file)
{
  const char *fnname;
  char *secname, *stubname;
  tree stubid, stubdecl;
  int need_comma;
  unsigned int f;

  fnname = XSTR (XEXP (DECL_RTL (current_function_decl), 0), 0);
  secname = (char *) alloca (strlen (fnname) + 20);
  sprintf (secname, ".mips16.fn.%s", fnname);
  stubname = (char *) alloca (strlen (fnname) + 20);
  sprintf (stubname, "__fn_stub_%s", fnname);
  stubid = get_identifier (stubname);
  stubdecl = build_decl (FUNCTION_DECL, stubid,
			 build_function_type (void_type_node, NULL_TREE));
  DECL_SECTION_NAME (stubdecl) = build_string (strlen (secname), secname);

  fprintf (file, "\t# Stub function for %s (", current_function_name ());
  need_comma = 0;
  for (f = (unsigned int) current_function_args_info.fp_code; f != 0; f >>= 2)
    {
      fprintf (file, "%s%s",
	       need_comma ? ", " : "",
	       (f & 3) == 1 ? "float" : "double");
      need_comma = 1;
    }
  fprintf (file, ")\n");

  fprintf (file, "\t.set\tnomips16\n");
  switch_to_section (function_section (stubdecl));
  ASM_OUTPUT_ALIGN (file, floor_log2 (FUNCTION_BOUNDARY / BITS_PER_UNIT));

  /* ??? If FUNCTION_NAME_ALREADY_DECLARED is defined, then we are
     within a .ent, and we cannot emit another .ent.  */
  if (!FUNCTION_NAME_ALREADY_DECLARED)
    {
      fputs ("\t.ent\t", file);
      assemble_name (file, stubname);
      fputs ("\n", file);
    }

  assemble_name (file, stubname);
  fputs (":\n", file);

  /* We don't want the assembler to insert any nops here.  */
  fprintf (file, "\t.set\tnoreorder\n");

  mips16_fp_args (file, current_function_args_info.fp_code, 1);

  fprintf (asm_out_file, "\t.set\tnoat\n");
  fprintf (asm_out_file, "\tla\t%s,", reg_names[GP_REG_FIRST + 1]);
  assemble_name (file, fnname);
  fprintf (file, "\n");
  fprintf (asm_out_file, "\tjr\t%s\n", reg_names[GP_REG_FIRST + 1]);
  fprintf (asm_out_file, "\t.set\tat\n");

  /* Unfortunately, we can't fill the jump delay slot.  We can't fill
     with one of the mfc1 instructions, because the result is not
     available for one instruction, so if the very first instruction
     in the function refers to the register, it will see the wrong
     value.  */
  fprintf (file, "\tnop\n");

  fprintf (file, "\t.set\treorder\n");

  if (!FUNCTION_NAME_ALREADY_DECLARED)
    {
      fputs ("\t.end\t", file);
      assemble_name (file, stubname);
      fputs ("\n", file);
    }

  fprintf (file, "\t.set\tmips16\n");

  switch_to_section (function_section (current_function_decl));
}

/* We keep a list of functions for which we have already built stubs
   in build_mips16_call_stub.  */

struct mips16_stub
{
  struct mips16_stub *next;
  char *name;
  int fpret;
};

static struct mips16_stub *mips16_stubs;

/* Build a call stub for a mips16 call.  A stub is needed if we are
   passing any floating point values which should go into the floating
   point registers.  If we are, and the call turns out to be to a 32
   bit function, the stub will be used to move the values into the
   floating point registers before calling the 32 bit function.  The
   linker will magically adjust the function call to either the 16 bit
   function or the 32 bit stub, depending upon where the function call
   is actually defined.

   Similarly, we need a stub if the return value might come back in a
   floating point register.

   RETVAL is the location of the return value, or null if this is
   a call rather than a call_value.  FN is the address of the
   function and ARG_SIZE is the size of the arguments.  FP_CODE
   is the code built by function_arg.  This function returns a nonzero
   value if it builds the call instruction itself.  */

int
build_mips16_call_stub (rtx retval, rtx fn, rtx arg_size, int fp_code)
{
  int fpret;
  const char *fnname;
  char *secname, *stubname;
  struct mips16_stub *l;
  tree stubid, stubdecl;
  int need_comma;
  unsigned int f;

  /* We don't need to do anything if we aren't in mips16 mode, or if
     we were invoked with the -msoft-float option.  */
  if (! TARGET_MIPS16 || ! mips16_hard_float)
    return 0;

  /* Figure out whether the value might come back in a floating point
     register.  */
  fpret = (retval != 0
	   && GET_MODE_CLASS (GET_MODE (retval)) == MODE_FLOAT
	   && GET_MODE_SIZE (GET_MODE (retval)) <= UNITS_PER_FPVALUE);

  /* We don't need to do anything if there were no floating point
     arguments and the value will not be returned in a floating point
     register.  */
  if (fp_code == 0 && ! fpret)
    return 0;

  /* We don't need to do anything if this is a call to a special
     mips16 support function.  */
  if (GET_CODE (fn) == SYMBOL_REF
      && strncmp (XSTR (fn, 0), "__mips16_", 9) == 0)
    return 0;

  /* This code will only work for o32 and o64 abis.  The other ABI's
     require more sophisticated support.  */
  gcc_assert (TARGET_OLDABI);

  /* We can only handle SFmode and DFmode floating point return
     values.  */
  if (fpret)
    gcc_assert (GET_MODE (retval) == SFmode || GET_MODE (retval) == DFmode);

  /* If we're calling via a function pointer, then we must always call
     via a stub.  There are magic stubs provided in libgcc.a for each
     of the required cases.  Each of them expects the function address
     to arrive in register $2.  */

  if (GET_CODE (fn) != SYMBOL_REF)
    {
      char buf[30];
      tree id;
      rtx stub_fn, insn;

      /* ??? If this code is modified to support other ABI's, we need
         to handle PARALLEL return values here.  */

      sprintf (buf, "__mips16_call_stub_%s%d",
	       (fpret
		? (GET_MODE (retval) == SFmode ? "sf_" : "df_")
		: ""),
	       fp_code);
      id = get_identifier (buf);
      stub_fn = gen_rtx_SYMBOL_REF (Pmode, IDENTIFIER_POINTER (id));

      emit_move_insn (gen_rtx_REG (Pmode, 2), fn);

      if (retval == NULL_RTX)
	insn = gen_call_internal (stub_fn, arg_size);
      else
	insn = gen_call_value_internal (retval, stub_fn, arg_size);
      insn = emit_call_insn (insn);

      /* Put the register usage information on the CALL.  */
      CALL_INSN_FUNCTION_USAGE (insn) =
	gen_rtx_EXPR_LIST (VOIDmode,
			   gen_rtx_USE (VOIDmode, gen_rtx_REG (Pmode, 2)),
			   CALL_INSN_FUNCTION_USAGE (insn));

      /* If we are handling a floating point return value, we need to
         save $18 in the function prologue.  Putting a note on the
         call will mean that regs_ever_live[$18] will be true if the
         call is not eliminated, and we can check that in the prologue
         code.  */
      if (fpret)
	CALL_INSN_FUNCTION_USAGE (insn) =
	  gen_rtx_EXPR_LIST (VOIDmode,
			     gen_rtx_USE (VOIDmode,
					  gen_rtx_REG (word_mode, 18)),
			     CALL_INSN_FUNCTION_USAGE (insn));

      /* Return 1 to tell the caller that we've generated the call
         insn.  */
      return 1;
    }

  /* We know the function we are going to call.  If we have already
     built a stub, we don't need to do anything further.  */

  fnname = XSTR (fn, 0);
  for (l = mips16_stubs; l != NULL; l = l->next)
    if (strcmp (l->name, fnname) == 0)
      break;

  if (l == NULL)
    {
      /* Build a special purpose stub.  When the linker sees a
	 function call in mips16 code, it will check where the target
	 is defined.  If the target is a 32 bit call, the linker will
	 search for the section defined here.  It can tell which
	 symbol this section is associated with by looking at the
	 relocation information (the name is unreliable, since this
	 might be a static function).  If such a section is found, the
	 linker will redirect the call to the start of the magic
	 section.

	 If the function does not return a floating point value, the
	 special stub section is named
	     .mips16.call.FNNAME

	 If the function does return a floating point value, the stub
	 section is named
	     .mips16.call.fp.FNNAME
	 */

      secname = (char *) alloca (strlen (fnname) + 40);
      sprintf (secname, ".mips16.call.%s%s",
	       fpret ? "fp." : "",
	       fnname);
      stubname = (char *) alloca (strlen (fnname) + 20);
      sprintf (stubname, "__call_stub_%s%s",
	       fpret ? "fp_" : "",
	       fnname);
      stubid = get_identifier (stubname);
      stubdecl = build_decl (FUNCTION_DECL, stubid,
			     build_function_type (void_type_node, NULL_TREE));
      DECL_SECTION_NAME (stubdecl) = build_string (strlen (secname), secname);

      fprintf (asm_out_file, "\t# Stub function to call %s%s (",
	       (fpret
		? (GET_MODE (retval) == SFmode ? "float " : "double ")
		: ""),
	       fnname);
      need_comma = 0;
      for (f = (unsigned int) fp_code; f != 0; f >>= 2)
	{
	  fprintf (asm_out_file, "%s%s",
		   need_comma ? ", " : "",
		   (f & 3) == 1 ? "float" : "double");
	  need_comma = 1;
	}
      fprintf (asm_out_file, ")\n");

      fprintf (asm_out_file, "\t.set\tnomips16\n");
      assemble_start_function (stubdecl, stubname);

      if (!FUNCTION_NAME_ALREADY_DECLARED)
	{
	  fputs ("\t.ent\t", asm_out_file);
	  assemble_name (asm_out_file, stubname);
	  fputs ("\n", asm_out_file);

	  assemble_name (asm_out_file, stubname);
	  fputs (":\n", asm_out_file);
	}

      /* We build the stub code by hand.  That's the only way we can
	 do it, since we can't generate 32 bit code during a 16 bit
	 compilation.  */

      /* We don't want the assembler to insert any nops here.  */
      fprintf (asm_out_file, "\t.set\tnoreorder\n");

      mips16_fp_args (asm_out_file, fp_code, 0);

      if (! fpret)
	{
	  fprintf (asm_out_file, "\t.set\tnoat\n");
	  fprintf (asm_out_file, "\tla\t%s,%s\n", reg_names[GP_REG_FIRST + 1],
		   fnname);
	  fprintf (asm_out_file, "\tjr\t%s\n", reg_names[GP_REG_FIRST + 1]);
	  fprintf (asm_out_file, "\t.set\tat\n");
	  /* Unfortunately, we can't fill the jump delay slot.  We
	     can't fill with one of the mtc1 instructions, because the
	     result is not available for one instruction, so if the
	     very first instruction in the function refers to the
	     register, it will see the wrong value.  */
	  fprintf (asm_out_file, "\tnop\n");
	}
      else
	{
	  fprintf (asm_out_file, "\tmove\t%s,%s\n",
		   reg_names[GP_REG_FIRST + 18], reg_names[GP_REG_FIRST + 31]);
	  fprintf (asm_out_file, "\tjal\t%s\n", fnname);
	  /* As above, we can't fill the delay slot.  */
	  fprintf (asm_out_file, "\tnop\n");
	  if (GET_MODE (retval) == SFmode)
	    fprintf (asm_out_file, "\tmfc1\t%s,%s\n",
		     reg_names[GP_REG_FIRST + 2], reg_names[FP_REG_FIRST + 0]);
	  else
	    {
	      if (TARGET_BIG_ENDIAN)
		{
		  fprintf (asm_out_file, "\tmfc1\t%s,%s\n",
			   reg_names[GP_REG_FIRST + 2],
			   reg_names[FP_REG_FIRST + 1]);
		  fprintf (asm_out_file, "\tmfc1\t%s,%s\n",
			   reg_names[GP_REG_FIRST + 3],
			   reg_names[FP_REG_FIRST + 0]);
		}
	      else
		{
		  fprintf (asm_out_file, "\tmfc1\t%s,%s\n",
			   reg_names[GP_REG_FIRST + 2],
			   reg_names[FP_REG_FIRST + 0]);
		  fprintf (asm_out_file, "\tmfc1\t%s,%s\n",
			   reg_names[GP_REG_FIRST + 3],
			   reg_names[FP_REG_FIRST + 1]);
		}
	    }
	  fprintf (asm_out_file, "\tj\t%s\n", reg_names[GP_REG_FIRST + 18]);
	  /* As above, we can't fill the delay slot.  */
	  fprintf (asm_out_file, "\tnop\n");
	}

      fprintf (asm_out_file, "\t.set\treorder\n");

#ifdef ASM_DECLARE_FUNCTION_SIZE
      ASM_DECLARE_FUNCTION_SIZE (asm_out_file, stubname, stubdecl);
#endif

      if (!FUNCTION_NAME_ALREADY_DECLARED)
	{
	  fputs ("\t.end\t", asm_out_file);
	  assemble_name (asm_out_file, stubname);
	  fputs ("\n", asm_out_file);
	}

      fprintf (asm_out_file, "\t.set\tmips16\n");

      /* Record this stub.  */
      l = (struct mips16_stub *) xmalloc (sizeof *l);
      l->name = xstrdup (fnname);
      l->fpret = fpret;
      l->next = mips16_stubs;
      mips16_stubs = l;
    }

  /* If we expect a floating point return value, but we've built a
     stub which does not expect one, then we're in trouble.  We can't
     use the existing stub, because it won't handle the floating point
     value.  We can't build a new stub, because the linker won't know
     which stub to use for the various calls in this object file.
     Fortunately, this case is illegal, since it means that a function
     was declared in two different ways in a single compilation.  */
  if (fpret && ! l->fpret)
    error ("cannot handle inconsistent calls to %qs", fnname);

  /* If we are calling a stub which handles a floating point return
     value, we need to arrange to save $18 in the prologue.  We do
     this by marking the function call as using the register.  The
     prologue will later see that it is used, and emit code to save
     it.  */

  if (l->fpret)
    {
      rtx insn;

      if (retval == NULL_RTX)
	insn = gen_call_internal (fn, arg_size);
      else
	insn = gen_call_value_internal (retval, fn, arg_size);
      insn = emit_call_insn (insn);

      CALL_INSN_FUNCTION_USAGE (insn) =
	gen_rtx_EXPR_LIST (VOIDmode,
			   gen_rtx_USE (VOIDmode, gen_rtx_REG (word_mode, 18)),
			   CALL_INSN_FUNCTION_USAGE (insn));

      /* Return 1 to tell the caller that we've generated the call
         insn.  */
      return 1;
    }

  /* Return 0 to let the caller generate the call insn.  */
  return 0;
}

/* An entry in the mips16 constant pool.  VALUE is the pool constant,
   MODE is its mode, and LABEL is the CODE_LABEL associated with it.  */

struct mips16_constant {
  struct mips16_constant *next;
  rtx value;
  rtx label;
  enum machine_mode mode;
};

/* Information about an incomplete mips16 constant pool.  FIRST is the
   first constant, HIGHEST_ADDRESS is the highest address that the first
   byte of the pool can have, and INSN_ADDRESS is the current instruction
   address.  */

struct mips16_constant_pool {
  struct mips16_constant *first;
  int highest_address;
  int insn_address;
};

/* Add constant VALUE to POOL and return its label.  MODE is the
   value's mode (used for CONST_INTs, etc.).  */

static rtx
add_constant (struct mips16_constant_pool *pool,
	      rtx value, enum machine_mode mode)
{
  struct mips16_constant **p, *c;
  bool first_of_size_p;

  /* See whether the constant is already in the pool.  If so, return the
     existing label, otherwise leave P pointing to the place where the
     constant should be added.

     Keep the pool sorted in increasing order of mode size so that we can
     reduce the number of alignments needed.  */
  first_of_size_p = true;
  for (p = &pool->first; *p != 0; p = &(*p)->next)
    {
      if (mode == (*p)->mode && rtx_equal_p (value, (*p)->value))
	return (*p)->label;
      if (GET_MODE_SIZE (mode) < GET_MODE_SIZE ((*p)->mode))
	break;
      if (GET_MODE_SIZE (mode) == GET_MODE_SIZE ((*p)->mode))
	first_of_size_p = false;
    }

  /* In the worst case, the constant needed by the earliest instruction
     will end up at the end of the pool.  The entire pool must then be
     accessible from that instruction.

     When adding the first constant, set the pool's highest address to
     the address of the first out-of-range byte.  Adjust this address
     downwards each time a new constant is added.  */
  if (pool->first == 0)
    /* For pc-relative lw, addiu and daddiu instructions, the base PC value
       is the address of the instruction with the lowest two bits clear.
       The base PC value for ld has the lowest three bits clear.  Assume
       the worst case here.  */
    pool->highest_address = pool->insn_address - (UNITS_PER_WORD - 2) + 0x8000;
  pool->highest_address -= GET_MODE_SIZE (mode);
  if (first_of_size_p)
    /* Take into account the worst possible padding due to alignment.  */
    pool->highest_address -= GET_MODE_SIZE (mode) - 1;

  /* Create a new entry.  */
  c = (struct mips16_constant *) xmalloc (sizeof *c);
  c->value = value;
  c->mode = mode;
  c->label = gen_label_rtx ();
  c->next = *p;
  *p = c;

  return c->label;
}

/* Output constant VALUE after instruction INSN and return the last
   instruction emitted.  MODE is the mode of the constant.  */

static rtx
dump_constants_1 (enum machine_mode mode, rtx value, rtx insn)
{
  switch (GET_MODE_CLASS (mode))
    {
    case MODE_INT:
      {
	rtx size = GEN_INT (GET_MODE_SIZE (mode));
	return emit_insn_after (gen_consttable_int (value, size), insn);
      }

    case MODE_FLOAT:
      return emit_insn_after (gen_consttable_float (value), insn);

    case MODE_VECTOR_FLOAT:
    case MODE_VECTOR_INT:
      {
	int i;
	for (i = 0; i < CONST_VECTOR_NUNITS (value); i++)
	  insn = dump_constants_1 (GET_MODE_INNER (mode),
				   CONST_VECTOR_ELT (value, i), insn);
	return insn;
      }

    default:
      gcc_unreachable ();
    }
}


/* Dump out the constants in CONSTANTS after INSN.  */

static void
dump_constants (struct mips16_constant *constants, rtx insn)
{
  struct mips16_constant *c, *next;
  int align;

  align = 0;
  for (c = constants; c != NULL; c = next)
    {
      /* If necessary, increase the alignment of PC.  */
      if (align < GET_MODE_SIZE (c->mode))
	{
	  int align_log = floor_log2 (GET_MODE_SIZE (c->mode));
	  insn = emit_insn_after (gen_align (GEN_INT (align_log)), insn);
	}
      align = GET_MODE_SIZE (c->mode);

      insn = emit_label_after (c->label, insn);
      insn = dump_constants_1 (c->mode, c->value, insn);

      next = c->next;
      free (c);
    }

  emit_barrier_after (insn);
}

/* Return the length of instruction INSN.  */

static int
mips16_insn_length (rtx insn)
{
  if (JUMP_P (insn))
    {
      rtx body = PATTERN (insn);
      if (GET_CODE (body) == ADDR_VEC)
	return GET_MODE_SIZE (GET_MODE (body)) * XVECLEN (body, 0);
      if (GET_CODE (body) == ADDR_DIFF_VEC)
	return GET_MODE_SIZE (GET_MODE (body)) * XVECLEN (body, 1);
    }
  return get_attr_length (insn);
}

/* Rewrite *X so that constant pool references refer to the constant's
   label instead.  DATA points to the constant pool structure.  */

static int
mips16_rewrite_pool_refs (rtx *x, void *data)
{
  struct mips16_constant_pool *pool = data;
  if (GET_CODE (*x) == SYMBOL_REF && CONSTANT_POOL_ADDRESS_P (*x))
    *x = gen_rtx_LABEL_REF (Pmode, add_constant (pool,
						 get_pool_constant (*x),
						 get_pool_mode (*x)));
  return 0;
}

/* Build MIPS16 constant pools.  */

static void
mips16_lay_out_constants (void)
{
  struct mips16_constant_pool pool;
  rtx insn, barrier;

  barrier = 0;
  memset (&pool, 0, sizeof (pool));
  for (insn = get_insns (); insn; insn = NEXT_INSN (insn))
    {
      /* Rewrite constant pool references in INSN.  */
      if (INSN_P (insn))
	for_each_rtx (&PATTERN (insn), mips16_rewrite_pool_refs, &pool);

      pool.insn_address += mips16_insn_length (insn);

      if (pool.first != NULL)
	{
	  /* If there are no natural barriers between the first user of
	     the pool and the highest acceptable address, we'll need to
	     create a new instruction to jump around the constant pool.
	     In the worst case, this instruction will be 4 bytes long.

	     If it's too late to do this transformation after INSN,
	     do it immediately before INSN.  */
	  if (barrier == 0 && pool.insn_address + 4 > pool.highest_address)
	    {
	      rtx label, jump;

	      label = gen_label_rtx ();

	      jump = emit_jump_insn_before (gen_jump (label), insn);
	      JUMP_LABEL (jump) = label;
	      LABEL_NUSES (label) = 1;
	      barrier = emit_barrier_after (jump);

	      emit_label_after (label, barrier);
	      pool.insn_address += 4;
	    }

	  /* See whether the constant pool is now out of range of the first
	     user.  If so, output the constants after the previous barrier.
	     Note that any instructions between BARRIER and INSN (inclusive)
	     will use negative offsets to refer to the pool.  */
	  if (pool.insn_address > pool.highest_address)
	    {
	      dump_constants (pool.first, barrier);
	      pool.first = NULL;
	      barrier = 0;
	    }
	  else if (BARRIER_P (insn))
	    barrier = insn;
	}
    }
  dump_constants (pool.first, get_last_insn ());
}

/* A temporary variable used by for_each_rtx callbacks, etc.  */
static rtx mips_sim_insn;

/* A structure representing the state of the processor pipeline.
   Used by the mips_sim_* family of functions.  */
struct mips_sim {
  /* The maximum number of instructions that can be issued in a cycle.
     (Caches mips_issue_rate.)  */
  unsigned int issue_rate;

  /* The current simulation time.  */
  unsigned int time;

  /* How many more instructions can be issued in the current cycle.  */
  unsigned int insns_left;

  /* LAST_SET[X].INSN is the last instruction to set register X.
     LAST_SET[X].TIME is the time at which that instruction was issued.
     INSN is null if no instruction has yet set register X.  */
  struct {
    rtx insn;
    unsigned int time;
  } last_set[FIRST_PSEUDO_REGISTER];

  /* The pipeline's current DFA state.  */
  state_t dfa_state;
};

/* Reset STATE to the initial simulation state.  */

static void
mips_sim_reset (struct mips_sim *state)
{
  state->time = 0;
  state->insns_left = state->issue_rate;
  memset (&state->last_set, 0, sizeof (state->last_set));
  state_reset (state->dfa_state);
}

/* Initialize STATE before its first use.  DFA_STATE points to an
   allocated but uninitialized DFA state.  */

static void
mips_sim_init (struct mips_sim *state, state_t dfa_state)
{
  state->issue_rate = mips_issue_rate ();
  state->dfa_state = dfa_state;
  mips_sim_reset (state);
}

/* Advance STATE by one clock cycle.  */

static void
mips_sim_next_cycle (struct mips_sim *state)
{
  state->time++;
  state->insns_left = state->issue_rate;
  state_transition (state->dfa_state, 0);
}

/* Advance simulation state STATE until instruction INSN can read
   register REG.  */

static void
mips_sim_wait_reg (struct mips_sim *state, rtx insn, rtx reg)
{
  unsigned int i;

  for (i = 0; i < HARD_REGNO_NREGS (REGNO (reg), GET_MODE (reg)); i++)
    if (state->last_set[REGNO (reg) + i].insn != 0)
      {
	unsigned int t;

	t = state->last_set[REGNO (reg) + i].time;
	t += insn_latency (state->last_set[REGNO (reg) + i].insn, insn);
	while (state->time < t)
	  mips_sim_next_cycle (state);
    }
}

/* A for_each_rtx callback.  If *X is a register, advance simulation state
   DATA until mips_sim_insn can read the register's value.  */

static int
mips_sim_wait_regs_2 (rtx *x, void *data)
{
  if (REG_P (*x))
    mips_sim_wait_reg (data, mips_sim_insn, *x);
  return 0;
}

/* Call mips_sim_wait_regs_2 (R, DATA) for each register R mentioned in *X.  */

static void
mips_sim_wait_regs_1 (rtx *x, void *data)
{
  for_each_rtx (x, mips_sim_wait_regs_2, data);
}

/* Advance simulation state STATE until all of INSN's register
   dependencies are satisfied.  */

static void
mips_sim_wait_regs (struct mips_sim *state, rtx insn)
{
  mips_sim_insn = insn;
  note_uses (&PATTERN (insn), mips_sim_wait_regs_1, state);
}

/* Advance simulation state STATE until the units required by
   instruction INSN are available.  */

static void
mips_sim_wait_units (struct mips_sim *state, rtx insn)
{
  state_t tmp_state;

  tmp_state = alloca (state_size ());
  while (state->insns_left == 0
	 || (memcpy (tmp_state, state->dfa_state, state_size ()),
	     state_transition (tmp_state, insn) >= 0))
    mips_sim_next_cycle (state);
}

/* Advance simulation state STATE until INSN is ready to issue.  */

static void
mips_sim_wait_insn (struct mips_sim *state, rtx insn)
{
  mips_sim_wait_regs (state, insn);
  mips_sim_wait_units (state, insn);
}

/* mips_sim_insn has just set X.  Update the LAST_SET array
   in simulation state DATA.  */

static void
mips_sim_record_set (rtx x, rtx pat ATTRIBUTE_UNUSED, void *data)
{
  struct mips_sim *state;
  unsigned int i;

  state = data;
  if (REG_P (x))
    for (i = 0; i < HARD_REGNO_NREGS (REGNO (x), GET_MODE (x)); i++)
      {
	state->last_set[REGNO (x) + i].insn = mips_sim_insn;
	state->last_set[REGNO (x) + i].time = state->time;
      }
}

/* Issue instruction INSN in scheduler state STATE.  Assume that INSN
   can issue immediately (i.e., that mips_sim_wait_insn has already
   been called).  */

static void
mips_sim_issue_insn (struct mips_sim *state, rtx insn)
{
  state_transition (state->dfa_state, insn);
  state->insns_left--;

  mips_sim_insn = insn;
  note_stores (PATTERN (insn), mips_sim_record_set, state);
}

/* Simulate issuing a NOP in state STATE.  */

static void
mips_sim_issue_nop (struct mips_sim *state)
{
  if (state->insns_left == 0)
    mips_sim_next_cycle (state);
  state->insns_left--;
}

/* Update simulation state STATE so that it's ready to accept the instruction
   after INSN.  INSN should be part of the main rtl chain, not a member of a
   SEQUENCE.  */

static void
mips_sim_finish_insn (struct mips_sim *state, rtx insn)
{
  /* If INSN is a jump with an implicit delay slot, simulate a nop.  */
  if (JUMP_P (insn))
    mips_sim_issue_nop (state);

  switch (GET_CODE (SEQ_BEGIN (insn)))
    {
    case CODE_LABEL:
    case CALL_INSN:
      /* We can't predict the processor state after a call or label.  */
      mips_sim_reset (state);
      break;

    case JUMP_INSN:
      /* The delay slots of branch likely instructions are only executed
	 when the branch is taken.  Therefore, if the caller has simulated
	 the delay slot instruction, STATE does not really reflect the state
	 of the pipeline for the instruction after the delay slot.  Also,
	 branch likely instructions tend to incur a penalty when not taken,
	 so there will probably be an extra delay between the branch and
	 the instruction after the delay slot.  */
      if (INSN_ANNULLED_BRANCH_P (SEQ_BEGIN (insn)))
	mips_sim_reset (state);
      break;

    default:
      break;
    }
}

/* The VR4130 pipeline issues aligned pairs of instructions together,
   but it stalls the second instruction if it depends on the first.
   In order to cut down the amount of logic required, this dependence
   check is not based on a full instruction decode.  Instead, any non-SPECIAL
   instruction is assumed to modify the register specified by bits 20-16
   (which is usually the "rt" field).

   In beq, beql, bne and bnel instructions, the rt field is actually an
   input, so we can end up with a false dependence between the branch
   and its delay slot.  If this situation occurs in instruction INSN,
   try to avoid it by swapping rs and rt.  */

static void
vr4130_avoid_branch_rt_conflict (rtx insn)
{
  rtx first, second;

  first = SEQ_BEGIN (insn);
  second = SEQ_END (insn);
  if (JUMP_P (first)
      && NONJUMP_INSN_P (second)
      && GET_CODE (PATTERN (first)) == SET
      && GET_CODE (SET_DEST (PATTERN (first))) == PC
      && GET_CODE (SET_SRC (PATTERN (first))) == IF_THEN_ELSE)
    {
      /* Check for the right kind of condition.  */
      rtx cond = XEXP (SET_SRC (PATTERN (first)), 0);
      if ((GET_CODE (cond) == EQ || GET_CODE (cond) == NE)
	  && REG_P (XEXP (cond, 0))
	  && REG_P (XEXP (cond, 1))
	  && reg_referenced_p (XEXP (cond, 1), PATTERN (second))
	  && !reg_referenced_p (XEXP (cond, 0), PATTERN (second)))
	{
	  /* SECOND mentions the rt register but not the rs register.  */
	  rtx tmp = XEXP (cond, 0);
	  XEXP (cond, 0) = XEXP (cond, 1);
	  XEXP (cond, 1) = tmp;
	}
    }
}

/* Implement -mvr4130-align.  Go through each basic block and simulate the
   processor pipeline.  If we find that a pair of instructions could execute
   in parallel, and the first of those instruction is not 8-byte aligned,
   insert a nop to make it aligned.  */

static void
vr4130_align_insns (void)
{
  struct mips_sim state;
  rtx insn, subinsn, last, last2, next;
  bool aligned_p;

  dfa_start ();

  /* LAST is the last instruction before INSN to have a nonzero length.
     LAST2 is the last such instruction before LAST.  */
  last = 0;
  last2 = 0;

  /* ALIGNED_P is true if INSN is known to be at an aligned address.  */
  aligned_p = true;

  mips_sim_init (&state, alloca (state_size ()));
  for (insn = get_insns (); insn != 0; insn = next)
    {
      unsigned int length;

      next = NEXT_INSN (insn);

      /* See the comment above vr4130_avoid_branch_rt_conflict for details.
	 This isn't really related to the alignment pass, but we do it on
	 the fly to avoid a separate instruction walk.  */
      vr4130_avoid_branch_rt_conflict (insn);

      if (USEFUL_INSN_P (insn))
	FOR_EACH_SUBINSN (subinsn, insn)
	  {
	    mips_sim_wait_insn (&state, subinsn);

	    /* If we want this instruction to issue in parallel with the
	       previous one, make sure that the previous instruction is
	       aligned.  There are several reasons why this isn't worthwhile
	       when the second instruction is a call:

	          - Calls are less likely to be performance critical,
		  - There's a good chance that the delay slot can execute
		    in parallel with the call.
	          - The return address would then be unaligned.

	       In general, if we're going to insert a nop between instructions
	       X and Y, it's better to insert it immediately after X.  That
	       way, if the nop makes Y aligned, it will also align any labels
	       between X and Y.  */
	    if (state.insns_left != state.issue_rate
		&& !CALL_P (subinsn))
	      {
		if (subinsn == SEQ_BEGIN (insn) && aligned_p)
		  {
		    /* SUBINSN is the first instruction in INSN and INSN is
		       aligned.  We want to align the previous instruction
		       instead, so insert a nop between LAST2 and LAST.

		       Note that LAST could be either a single instruction
		       or a branch with a delay slot.  In the latter case,
		       LAST, like INSN, is already aligned, but the delay
		       slot must have some extra delay that stops it from
		       issuing at the same time as the branch.  We therefore
		       insert a nop before the branch in order to align its
		       delay slot.  */
		    emit_insn_after (gen_nop (), last2);
		    aligned_p = false;
		  }
		else if (subinsn != SEQ_BEGIN (insn) && !aligned_p)
		  {
		    /* SUBINSN is the delay slot of INSN, but INSN is
		       currently unaligned.  Insert a nop between
		       LAST and INSN to align it.  */
		    emit_insn_after (gen_nop (), last);
		    aligned_p = true;
		  }
	      }
	    mips_sim_issue_insn (&state, subinsn);
	  }
      mips_sim_finish_insn (&state, insn);

      /* Update LAST, LAST2 and ALIGNED_P for the next instruction.  */
      length = get_attr_length (insn);
      if (length > 0)
	{
	  /* If the instruction is an asm statement or multi-instruction
	     mips.md patern, the length is only an estimate.  Insert an
	     8 byte alignment after it so that the following instructions
	     can be handled correctly.  */
	  if (NONJUMP_INSN_P (SEQ_BEGIN (insn))
	      && (recog_memoized (insn) < 0 || length >= 8))
	    {
	      next = emit_insn_after (gen_align (GEN_INT (3)), insn);
	      next = NEXT_INSN (next);
	      mips_sim_next_cycle (&state);
	      aligned_p = true;
	    }
	  else if (length & 4)
	    aligned_p = !aligned_p;
	  last2 = last;
	  last = insn;
	}

      /* See whether INSN is an aligned label.  */
      if (LABEL_P (insn) && label_to_alignment (insn) >= 3)
	aligned_p = true;
    }
  dfa_finish ();
}

/* Subroutine of mips_reorg.  If there is a hazard between INSN
   and a previous instruction, avoid it by inserting nops after
   instruction AFTER.

   *DELAYED_REG and *HILO_DELAY describe the hazards that apply at
   this point.  If *DELAYED_REG is non-null, INSN must wait a cycle
   before using the value of that register.  *HILO_DELAY counts the
   number of instructions since the last hilo hazard (that is,
   the number of instructions since the last mflo or mfhi).

   After inserting nops for INSN, update *DELAYED_REG and *HILO_DELAY
   for the next instruction.

   LO_REG is an rtx for the LO register, used in dependence checking.  */

static void
mips_avoid_hazard (rtx after, rtx insn, int *hilo_delay,
		   rtx *delayed_reg, rtx lo_reg)
{
  rtx pattern, set;
  int nops, ninsns;

  if (!INSN_P (insn))
    return;

  pattern = PATTERN (insn);

  /* Do not put the whole function in .set noreorder if it contains
     an asm statement.  We don't know whether there will be hazards
     between the asm statement and the gcc-generated code.  */
  if (GET_CODE (pattern) == ASM_INPUT || asm_noperands (pattern) >= 0)
    cfun->machine->all_noreorder_p = false;

  /* Ignore zero-length instructions (barriers and the like).  */
  ninsns = get_attr_length (insn) / 4;
  if (ninsns == 0)
    return;

  /* Work out how many nops are needed.  Note that we only care about
     registers that are explicitly mentioned in the instruction's pattern.
     It doesn't matter that calls use the argument registers or that they
     clobber hi and lo.  */
  if (*hilo_delay < 2 && reg_set_p (lo_reg, pattern))
    nops = 2 - *hilo_delay;
  else if (*delayed_reg != 0 && reg_referenced_p (*delayed_reg, pattern))
    nops = 1;
  else
    nops = 0;

  /* Insert the nops between this instruction and the previous one.
     Each new nop takes us further from the last hilo hazard.  */
  *hilo_delay += nops;
  while (nops-- > 0)
    emit_insn_after (gen_hazard_nop (), after);

  /* Set up the state for the next instruction.  */
  *hilo_delay += ninsns;
  *delayed_reg = 0;
  if (INSN_CODE (insn) >= 0)
    switch (get_attr_hazard (insn))
      {
      case HAZARD_NONE:
	break;

      case HAZARD_HILO:
	*hilo_delay = 0;
	break;

      case HAZARD_DELAY:
	set = single_set (insn);
	gcc_assert (set != 0);
	*delayed_reg = SET_DEST (set);
	break;
      }
}


/* Go through the instruction stream and insert nops where necessary.
   See if the whole function can then be put into .set noreorder &
   .set nomacro.  */

static void
mips_avoid_hazards (void)
{
  rtx insn, last_insn, lo_reg, delayed_reg;
  int hilo_delay, i;

  /* Force all instructions to be split into their final form.  */
  split_all_insns_noflow ();

  /* Recalculate instruction lengths without taking nops into account.  */
  cfun->machine->ignore_hazard_length_p = true;
  shorten_branches (get_insns ());

  cfun->machine->all_noreorder_p = true;

  /* Profiled functions can't be all noreorder because the profiler
     support uses assembler macros.  */
  if (current_function_profile)
    cfun->machine->all_noreorder_p = false;

  /* Code compiled with -mfix-vr4120 can't be all noreorder because
     we rely on the assembler to work around some errata.  */
  if (TARGET_FIX_VR4120)
    cfun->machine->all_noreorder_p = false;

  /* The same is true for -mfix-vr4130 if we might generate mflo or
     mfhi instructions.  Note that we avoid using mflo and mfhi if
     the VR4130 macc and dmacc instructions are available instead;
     see the *mfhilo_{si,di}_macc patterns.  */
  if (TARGET_FIX_VR4130 && !ISA_HAS_MACCHI)
    cfun->machine->all_noreorder_p = false;

  last_insn = 0;
  hilo_delay = 2;
  delayed_reg = 0;
  lo_reg = gen_rtx_REG (SImode, LO_REGNUM);

  for (insn = get_insns (); insn != 0; insn = NEXT_INSN (insn))
    if (INSN_P (insn))
      {
	if (GET_CODE (PATTERN (insn)) == SEQUENCE)
	  for (i = 0; i < XVECLEN (PATTERN (insn), 0); i++)
	    mips_avoid_hazard (last_insn, XVECEXP (PATTERN (insn), 0, i),
			       &hilo_delay, &delayed_reg, lo_reg);
	else
	  mips_avoid_hazard (last_insn, insn, &hilo_delay,
			     &delayed_reg, lo_reg);

	last_insn = insn;
      }
}


/* Implement TARGET_MACHINE_DEPENDENT_REORG.  */

static void
mips_reorg (void)
{
  if (TARGET_MIPS16)
    mips16_lay_out_constants ();
  else if (TARGET_EXPLICIT_RELOCS)
    {
      if (mips_flag_delayed_branch)
	dbr_schedule (get_insns ());
      mips_avoid_hazards ();
      if (TUNE_MIPS4130 && TARGET_VR4130_ALIGN)
	vr4130_align_insns ();
    }
}

/* This function does three things:

   - Register the special divsi3 and modsi3 functions if -mfix-vr4120.
   - Register the mips16 hardware floating point stubs.
   - Register the gofast functions if selected using --enable-gofast.  */

#include "config/gofast.h"

static void
mips_init_libfuncs (void)
{
  if (TARGET_FIX_VR4120)
    {
      set_optab_libfunc (sdiv_optab, SImode, "__vr4120_divsi3");
      set_optab_libfunc (smod_optab, SImode, "__vr4120_modsi3");
    }

  if (TARGET_MIPS16 && mips16_hard_float)
    {
      set_optab_libfunc (add_optab, SFmode, "__mips16_addsf3");
      set_optab_libfunc (sub_optab, SFmode, "__mips16_subsf3");
      set_optab_libfunc (smul_optab, SFmode, "__mips16_mulsf3");
      set_optab_libfunc (sdiv_optab, SFmode, "__mips16_divsf3");

      set_optab_libfunc (eq_optab, SFmode, "__mips16_eqsf2");
      set_optab_libfunc (ne_optab, SFmode, "__mips16_nesf2");
      set_optab_libfunc (gt_optab, SFmode, "__mips16_gtsf2");
      set_optab_libfunc (ge_optab, SFmode, "__mips16_gesf2");
      set_optab_libfunc (lt_optab, SFmode, "__mips16_ltsf2");
      set_optab_libfunc (le_optab, SFmode, "__mips16_lesf2");

      set_conv_libfunc (sfix_optab, SImode, SFmode, "__mips16_fix_truncsfsi");
      set_conv_libfunc (sfloat_optab, SFmode, SImode, "__mips16_floatsisf");

      if (TARGET_DOUBLE_FLOAT)
	{
	  set_optab_libfunc (add_optab, DFmode, "__mips16_adddf3");
	  set_optab_libfunc (sub_optab, DFmode, "__mips16_subdf3");
	  set_optab_libfunc (smul_optab, DFmode, "__mips16_muldf3");
	  set_optab_libfunc (sdiv_optab, DFmode, "__mips16_divdf3");

	  set_optab_libfunc (eq_optab, DFmode, "__mips16_eqdf2");
	  set_optab_libfunc (ne_optab, DFmode, "__mips16_nedf2");
	  set_optab_libfunc (gt_optab, DFmode, "__mips16_gtdf2");
	  set_optab_libfunc (ge_optab, DFmode, "__mips16_gedf2");
	  set_optab_libfunc (lt_optab, DFmode, "__mips16_ltdf2");
	  set_optab_libfunc (le_optab, DFmode, "__mips16_ledf2");

	  set_conv_libfunc (sext_optab, DFmode, SFmode, "__mips16_extendsfdf2");
	  set_conv_libfunc (trunc_optab, SFmode, DFmode, "__mips16_truncdfsf2");

	  set_conv_libfunc (sfix_optab, SImode, DFmode, "__mips16_fix_truncdfsi");
	  set_conv_libfunc (sfloat_optab, DFmode, SImode, "__mips16_floatsidf");
	}
    }
  else
    gofast_maybe_init_libfuncs ();
}

/* Return a number assessing the cost of moving a register in class
   FROM to class TO.  The classes are expressed using the enumeration
   values such as `GENERAL_REGS'.  A value of 2 is the default; other
   values are interpreted relative to that.

   It is not required that the cost always equal 2 when FROM is the
   same as TO; on some machines it is expensive to move between
   registers if they are not general registers.

   If reload sees an insn consisting of a single `set' between two
   hard registers, and if `REGISTER_MOVE_COST' applied to their
   classes returns a value of 2, reload does not check to ensure that
   the constraints of the insn are met.  Setting a cost of other than
   2 will allow reload to verify that the constraints are met.  You
   should do this if the `movM' pattern's constraints do not allow
   such copying.

   ??? We make the cost of moving from HI/LO into general
   registers the same as for one of moving general registers to
   HI/LO for TARGET_MIPS16 in order to prevent allocating a
   pseudo to HI/LO.  This might hurt optimizations though, it
   isn't clear if it is wise.  And it might not work in all cases.  We
   could solve the DImode LO reg problem by using a multiply, just
   like reload_{in,out}si.  We could solve the SImode/HImode HI reg
   problem by using divide instructions.  divu puts the remainder in
   the HI reg, so doing a divide by -1 will move the value in the HI
   reg for all values except -1.  We could handle that case by using a
   signed divide, e.g.  -1 / 2 (or maybe 1 / -2?).  We'd have to emit
   a compare/branch to test the input value to see which instruction
   we need to use.  This gets pretty messy, but it is feasible.  */

int
mips_register_move_cost (enum machine_mode mode ATTRIBUTE_UNUSED,
			 enum reg_class to, enum reg_class from)
{
  if (from == M16_REGS && GR_REG_CLASS_P (to))
    return 2;
  else if (from == M16_NA_REGS && GR_REG_CLASS_P (to))
    return 2;
  else if (GR_REG_CLASS_P (from))
    {
      if (to == M16_REGS)
	return 2;
      else if (to == M16_NA_REGS)
	return 2;
      else if (GR_REG_CLASS_P (to))
	{
	  if (TARGET_MIPS16)
	    return 4;
	  else
	    return 2;
	}
      else if (to == FP_REGS)
	return 4;
      else if (reg_class_subset_p (to, ACC_REGS))
	{
	  if (TARGET_MIPS16)
	    return 12;
	  else
	    return 6;
	}
      else if (COP_REG_CLASS_P (to))
	{
	  return 5;
	}
    }
  else if (from == FP_REGS)
    {
      if (GR_REG_CLASS_P (to))
	return 4;
      else if (to == FP_REGS)
	return 2;
      else if (to == ST_REGS)
	return 8;
    }
  else if (reg_class_subset_p (from, ACC_REGS))
    {
      if (GR_REG_CLASS_P (to))
	{
	  if (TARGET_MIPS16)
	    return 12;
	  else
	    return 6;
	}
    }
  else if (from == ST_REGS && GR_REG_CLASS_P (to))
    return 4;
  else if (COP_REG_CLASS_P (from))
    {
      return 5;
    }

  /* Fall through.
     ??? What cases are these? Shouldn't we return 2 here?  */

  return 12;
}

/* Return the length of INSN.  LENGTH is the initial length computed by
   attributes in the machine-description file.  */

int
mips_adjust_insn_length (rtx insn, int length)
{
  /* A unconditional jump has an unfilled delay slot if it is not part
     of a sequence.  A conditional jump normally has a delay slot, but
     does not on MIPS16.  */
  if (CALL_P (insn) || (TARGET_MIPS16 ? simplejump_p (insn) : JUMP_P (insn)))
    length += 4;

  /* See how many nops might be needed to avoid hardware hazards.  */
  if (!cfun->machine->ignore_hazard_length_p && INSN_CODE (insn) >= 0)
    switch (get_attr_hazard (insn))
      {
      case HAZARD_NONE:
	break;

      case HAZARD_DELAY:
	length += 4;
	break;

      case HAZARD_HILO:
	length += 8;
	break;
      }

  /* All MIPS16 instructions are a measly two bytes.  */
  if (TARGET_MIPS16)
    length /= 2;

  return length;
}


/* Return an asm sequence to start a noat block and load the address
   of a label into $1.  */

const char *
mips_output_load_label (void)
{
  if (TARGET_EXPLICIT_RELOCS)
    switch (mips_abi)
      {
      case ABI_N32:
	return "%[lw\t%@,%%got_page(%0)(%+)\n\taddiu\t%@,%@,%%got_ofst(%0)";

      case ABI_64:
	return "%[ld\t%@,%%got_page(%0)(%+)\n\tdaddiu\t%@,%@,%%got_ofst(%0)";

      default:
	if (ISA_HAS_LOAD_DELAY)
	  return "%[lw\t%@,%%got(%0)(%+)%#\n\taddiu\t%@,%@,%%lo(%0)";
	return "%[lw\t%@,%%got(%0)(%+)\n\taddiu\t%@,%@,%%lo(%0)";
      }
  else
    {
      if (Pmode == DImode)
	return "%[dla\t%@,%0";
      else
	return "%[la\t%@,%0";
    }
}

/* Return the assembly code for INSN, which has the operands given by
   OPERANDS, and which branches to OPERANDS[1] if some condition is true.
   BRANCH_IF_TRUE is the asm template that should be used if OPERANDS[1]
   is in range of a direct branch.  BRANCH_IF_FALSE is an inverted
   version of BRANCH_IF_TRUE.  */

const char *
mips_output_conditional_branch (rtx insn, rtx *operands,
				const char *branch_if_true,
				const char *branch_if_false)
{
  unsigned int length;
  rtx taken, not_taken;

  length = get_attr_length (insn);
  if (length <= 8)
    {
      /* Just a simple conditional branch.  */
      mips_branch_likely = (final_sequence && INSN_ANNULLED_BRANCH_P (insn));
      return branch_if_true;
    }

  /* Generate a reversed branch around a direct jump.  This fallback does
     not use branch-likely instructions.  */
  mips_branch_likely = false;
  not_taken = gen_label_rtx ();
  taken = operands[1];

  /* Generate the reversed branch to NOT_TAKEN.  */
  operands[1] = not_taken;
  output_asm_insn (branch_if_false, operands);

  /* If INSN has a delay slot, we must provide delay slots for both the
     branch to NOT_TAKEN and the conditional jump.  We must also ensure
     that INSN's delay slot is executed in the appropriate cases.  */
  if (final_sequence)
    {
      /* This first delay slot will always be executed, so use INSN's
	 delay slot if is not annulled.  */
      if (!INSN_ANNULLED_BRANCH_P (insn))
	{
	  final_scan_insn (XVECEXP (final_sequence, 0, 1),
			   asm_out_file, optimize, 1, NULL);
	  INSN_DELETED_P (XVECEXP (final_sequence, 0, 1)) = 1;
	}
      else
	output_asm_insn ("nop", 0);
      fprintf (asm_out_file, "\n");
    }

  /* Output the unconditional branch to TAKEN.  */
  if (length <= 16)
    output_asm_insn ("j\t%0%/", &taken);
  else
    {
      output_asm_insn (mips_output_load_label (), &taken);
      output_asm_insn ("jr\t%@%]%/", 0);
    }

  /* Now deal with its delay slot; see above.  */
  if (final_sequence)
    {
      /* This delay slot will only be executed if the branch is taken.
	 Use INSN's delay slot if is annulled.  */
      if (INSN_ANNULLED_BRANCH_P (insn))
	{
	  final_scan_insn (XVECEXP (final_sequence, 0, 1),
			   asm_out_file, optimize, 1, NULL);
	  INSN_DELETED_P (XVECEXP (final_sequence, 0, 1)) = 1;
	}
      else
	output_asm_insn ("nop", 0);
      fprintf (asm_out_file, "\n");
    }

  /* Output NOT_TAKEN.  */
  (*targetm.asm_out.internal_label) (asm_out_file, "L",
				     CODE_LABEL_NUMBER (not_taken));
  return "";
}

/* Return the assembly code for INSN, which branches to OPERANDS[1]
   if some ordered condition is true.  The condition is given by
   OPERANDS[0] if !INVERTED_P, otherwise it is the inverse of
   OPERANDS[0].  OPERANDS[2] is the comparison's first operand;
   its second is always zero.  */

const char *
mips_output_order_conditional_branch (rtx insn, rtx *operands, bool inverted_p)
{
  const char *branch[2];

  /* Make BRANCH[1] branch to OPERANDS[1] when the condition is true.
     Make BRANCH[0] branch on the inverse condition.  */
  switch (GET_CODE (operands[0]))
    {
      /* These cases are equivalent to comparisons against zero.  */
    case LEU:
      inverted_p = !inverted_p;
      /* Fall through.  */
    case GTU:
      branch[!inverted_p] = MIPS_BRANCH ("bne", "%2,%.,%1");
      branch[inverted_p] = MIPS_BRANCH ("beq", "%2,%.,%1");
      break;

      /* These cases are always true or always false.  */
    case LTU:
      inverted_p = !inverted_p;
      /* Fall through.  */
    case GEU:
      branch[!inverted_p] = MIPS_BRANCH ("beq", "%.,%.,%1");
      branch[inverted_p] = MIPS_BRANCH ("bne", "%.,%.,%1");
      break;

    default:
      branch[!inverted_p] = MIPS_BRANCH ("b%C0z", "%2,%1");
      branch[inverted_p] = MIPS_BRANCH ("b%N0z", "%2,%1");
      break;
    }
  return mips_output_conditional_branch (insn, operands, branch[1], branch[0]);
}

/* Used to output div or ddiv instruction DIVISION, which has the operands
   given by OPERANDS.  Add in a divide-by-zero check if needed.

   When working around R4000 and R4400 errata, we need to make sure that
   the division is not immediately followed by a shift[1][2].  We also
   need to stop the division from being put into a branch delay slot[3].
   The easiest way to avoid both problems is to add a nop after the
   division.  When a divide-by-zero check is needed, this nop can be
   used to fill the branch delay slot.

   [1] If a double-word or a variable shift executes immediately
       after starting an integer division, the shift may give an
       incorrect result.  See quotations of errata #16 and #28 from
       "MIPS R4000PC/SC Errata, Processor Revision 2.2 and 3.0"
       in mips.md for details.

   [2] A similar bug to [1] exists for all revisions of the
       R4000 and the R4400 when run in an MC configuration.
       From "MIPS R4000MC Errata, Processor Revision 2.2 and 3.0":

       "19. In this following sequence:

		    ddiv		(or ddivu or div or divu)
		    dsll32		(or dsrl32, dsra32)

	    if an MPT stall occurs, while the divide is slipping the cpu
	    pipeline, then the following double shift would end up with an
	    incorrect result.

	    Workaround: The compiler needs to avoid generating any
	    sequence with divide followed by extended double shift."

       This erratum is also present in "MIPS R4400MC Errata, Processor
       Revision 1.0" and "MIPS R4400MC Errata, Processor Revision 2.0
       & 3.0" as errata #10 and #4, respectively.

   [3] From "MIPS R4000PC/SC Errata, Processor Revision 2.2 and 3.0"
       (also valid for MIPS R4000MC processors):

       "52. R4000SC: This bug does not apply for the R4000PC.

	    There are two flavors of this bug:

	    1) If the instruction just after divide takes an RF exception
	       (tlb-refill, tlb-invalid) and gets an instruction cache
	       miss (both primary and secondary) and the line which is
	       currently in secondary cache at this index had the first
	       data word, where the bits 5..2 are set, then R4000 would
	       get a wrong result for the div.

	    ##1
		    nop
		    div	r8, r9
		    -------------------		# end-of page. -tlb-refill
		    nop
	    ##2
		    nop
		    div	r8, r9
		    -------------------		# end-of page. -tlb-invalid
		    nop

	    2) If the divide is in the taken branch delay slot, where the
	       target takes RF exception and gets an I-cache miss for the
	       exception vector or where I-cache miss occurs for the
	       target address, under the above mentioned scenarios, the
	       div would get wrong results.

	    ##1
		    j	r2		# to next page mapped or unmapped
		    div	r8,r9		# this bug would be there as long
					# as there is an ICache miss and
		    nop			# the "data pattern" is present

	    ##2
		    beq	r0, r0, NextPage	# to Next page
		    div	r8,r9
		    nop

	    This bug is present for div, divu, ddiv, and ddivu
	    instructions.

	    Workaround: For item 1), OS could make sure that the next page
	    after the divide instruction is also mapped.  For item 2), the
	    compiler could make sure that the divide instruction is not in
	    the branch delay slot."

       These processors have PRId values of 0x00004220 and 0x00004300 for
       the R4000 and 0x00004400, 0x00004500 and 0x00004600 for the R4400.  */

const char *
mips_output_division (const char *division, rtx *operands)
{
  const char *s;

  s = division;
  if (TARGET_FIX_R4000 || TARGET_FIX_R4400)
    {
      output_asm_insn (s, operands);
      s = "nop";
    }
  if (TARGET_CHECK_ZERO_DIV)
    {
      if (TARGET_MIPS16)
	{
	  output_asm_insn (s, operands);
	  s = "bnez\t%2,1f\n\tbreak\t7\n1:";
	}
      else if (GENERATE_DIVIDE_TRAPS)
        {
	  output_asm_insn (s, operands);
	  s = "teq\t%2,%.,7";
        }
      else
	{
	  output_asm_insn ("%(bne\t%2,%.,1f", operands);
	  output_asm_insn (s, operands);
	  s = "break\t7%)\n1:";
	}
    }
  return s;
}

/* Return true if GIVEN is the same as CANONICAL, or if it is CANONICAL
   with a final "000" replaced by "k".  Ignore case.

   Note: this function is shared between GCC and GAS.  */

static bool
mips_strict_matching_cpu_name_p (const char *canonical, const char *given)
{
  while (*given != 0 && TOLOWER (*given) == TOLOWER (*canonical))
    given++, canonical++;

  return ((*given == 0 && *canonical == 0)
	  || (strcmp (canonical, "000") == 0 && strcasecmp (given, "k") == 0));
}


/* Return true if GIVEN matches CANONICAL, where GIVEN is a user-supplied
   CPU name.  We've traditionally allowed a lot of variation here.

   Note: this function is shared between GCC and GAS.  */

static bool
mips_matching_cpu_name_p (const char *canonical, const char *given)
{
  /* First see if the name matches exactly, or with a final "000"
     turned into "k".  */
  if (mips_strict_matching_cpu_name_p (canonical, given))
    return true;

  /* If not, try comparing based on numerical designation alone.
     See if GIVEN is an unadorned number, or 'r' followed by a number.  */
  if (TOLOWER (*given) == 'r')
    given++;
  if (!ISDIGIT (*given))
    return false;

  /* Skip over some well-known prefixes in the canonical name,
     hoping to find a number there too.  */
  if (TOLOWER (canonical[0]) == 'v' && TOLOWER (canonical[1]) == 'r')
    canonical += 2;
  else if (TOLOWER (canonical[0]) == 'r' && TOLOWER (canonical[1]) == 'm')
    canonical += 2;
  else if (TOLOWER (canonical[0]) == 'r')
    canonical += 1;

  return mips_strict_matching_cpu_name_p (canonical, given);
}


/* Return the mips_cpu_info entry for the processor or ISA given
   by CPU_STRING.  Return null if the string isn't recognized.

   A similar function exists in GAS.  */

static const struct mips_cpu_info *
mips_parse_cpu (const char *cpu_string)
{
  const struct mips_cpu_info *p;
  const char *s;

  /* In the past, we allowed upper-case CPU names, but it doesn't
     work well with the multilib machinery.  */
  for (s = cpu_string; *s != 0; s++)
    if (ISUPPER (*s))
      {
	warning (0, "the cpu name must be lower case");
	break;
      }

  /* 'from-abi' selects the most compatible architecture for the given
     ABI: MIPS I for 32-bit ABIs and MIPS III for 64-bit ABIs.  For the
     EABIs, we have to decide whether we're using the 32-bit or 64-bit
     version.  Look first at the -mgp options, if given, otherwise base
     the choice on MASK_64BIT in TARGET_DEFAULT.  */
  if (strcasecmp (cpu_string, "from-abi") == 0)
    return mips_cpu_info_from_isa (ABI_NEEDS_32BIT_REGS ? 1
				   : ABI_NEEDS_64BIT_REGS ? 3
				   : (TARGET_64BIT ? 3 : 1));

  /* 'default' has traditionally been a no-op.  Probably not very useful.  */
  if (strcasecmp (cpu_string, "default") == 0)
    return 0;

  for (p = mips_cpu_info_table; p->name != 0; p++)
    if (mips_matching_cpu_name_p (p->name, cpu_string))
      return p;

  return 0;
}


/* Return the processor associated with the given ISA level, or null
   if the ISA isn't valid.  */

static const struct mips_cpu_info *
mips_cpu_info_from_isa (int isa)
{
  const struct mips_cpu_info *p;

  for (p = mips_cpu_info_table; p->name != 0; p++)
    if (p->isa == isa)
      return p;

  return 0;
}

/* Implement HARD_REGNO_NREGS.  The size of FP registers is controlled
   by UNITS_PER_FPREG.  The size of FP status registers is always 4, because
   they only hold condition code modes, and CCmode is always considered to
   be 4 bytes wide.  All other registers are word sized.  */

unsigned int
mips_hard_regno_nregs (int regno, enum machine_mode mode)
{
  if (ST_REG_P (regno))
    return ((GET_MODE_SIZE (mode) + 3) / 4);
  else if (! FP_REG_P (regno))
    return ((GET_MODE_SIZE (mode) + UNITS_PER_WORD - 1) / UNITS_PER_WORD);
  else
    return ((GET_MODE_SIZE (mode) + UNITS_PER_FPREG - 1) / UNITS_PER_FPREG);
}

/* Implement TARGET_RETURN_IN_MEMORY.  Under the old (i.e., 32 and O64 ABIs)
   all BLKmode objects are returned in memory.  Under the new (N32 and
   64-bit MIPS ABIs) small structures are returned in a register.
   Objects with varying size must still be returned in memory, of
   course.  */

static bool
mips_return_in_memory (tree type, tree fndecl ATTRIBUTE_UNUSED)
{
  if (TARGET_OLDABI)
    return (TYPE_MODE (type) == BLKmode);
  else
    return ((int_size_in_bytes (type) > (2 * UNITS_PER_WORD))
	    || (int_size_in_bytes (type) == -1));
}

static bool
mips_strict_argument_naming (CUMULATIVE_ARGS *ca ATTRIBUTE_UNUSED)
{
  return !TARGET_OLDABI;
}

/* Return true if INSN is a multiply-add or multiply-subtract
   instruction and PREV assigns to the accumulator operand.  */

bool
mips_linked_madd_p (rtx prev, rtx insn)
{
  rtx x;

  x = single_set (insn);
  if (x == 0)
    return false;

  x = SET_SRC (x);

  if (GET_CODE (x) == PLUS
      && GET_CODE (XEXP (x, 0)) == MULT
      && reg_set_p (XEXP (x, 1), prev))
    return true;

  if (GET_CODE (x) == MINUS
      && GET_CODE (XEXP (x, 1)) == MULT
      && reg_set_p (XEXP (x, 0), prev))
    return true;

  return false;
}

/* Used by TUNE_MACC_CHAINS to record the last scheduled instruction
   that may clobber hi or lo.  */

static rtx mips_macc_chains_last_hilo;

/* A TUNE_MACC_CHAINS helper function.  Record that instruction INSN has
   been scheduled, updating mips_macc_chains_last_hilo appropriately.  */

static void
mips_macc_chains_record (rtx insn)
{
  if (get_attr_may_clobber_hilo (insn))
    mips_macc_chains_last_hilo = insn;
}

/* A TUNE_MACC_CHAINS helper function.  Search ready queue READY, which
   has NREADY elements, looking for a multiply-add or multiply-subtract
   instruction that is cumulative with mips_macc_chains_last_hilo.
   If there is one, promote it ahead of anything else that might
   clobber hi or lo.  */

static void
mips_macc_chains_reorder (rtx *ready, int nready)
{
  int i, j;

  if (mips_macc_chains_last_hilo != 0)
    for (i = nready - 1; i >= 0; i--)
      if (mips_linked_madd_p (mips_macc_chains_last_hilo, ready[i]))
	{
	  for (j = nready - 1; j > i; j--)
	    if (recog_memoized (ready[j]) >= 0
		&& get_attr_may_clobber_hilo (ready[j]))
	      {
		mips_promote_ready (ready, i, j);
		break;
	      }
	  break;
	}
}

/* The last instruction to be scheduled.  */

static rtx vr4130_last_insn;

/* A note_stores callback used by vr4130_true_reg_dependence_p.  DATA
   points to an rtx that is initially an instruction.  Nullify the rtx
   if the instruction uses the value of register X.  */

static void
vr4130_true_reg_dependence_p_1 (rtx x, rtx pat ATTRIBUTE_UNUSED, void *data)
{
  rtx *insn_ptr = data;
  if (REG_P (x)
      && *insn_ptr != 0
      && reg_referenced_p (x, PATTERN (*insn_ptr)))
    *insn_ptr = 0;
}

/* Return true if there is true register dependence between vr4130_last_insn
   and INSN.  */

static bool
vr4130_true_reg_dependence_p (rtx insn)
{
  note_stores (PATTERN (vr4130_last_insn),
	       vr4130_true_reg_dependence_p_1, &insn);
  return insn == 0;
}

/* A TUNE_MIPS4130 helper function.  Given that INSN1 is at the head of
   the ready queue and that INSN2 is the instruction after it, return
   true if it is worth promoting INSN2 ahead of INSN1.  Look for cases
   in which INSN1 and INSN2 can probably issue in parallel, but for
   which (INSN2, INSN1) should be less sensitive to instruction
   alignment than (INSN1, INSN2).  See 4130.md for more details.  */

static bool
vr4130_swap_insns_p (rtx insn1, rtx insn2)
{
  rtx dep;

  /* Check for the following case:

     1) there is some other instruction X with an anti dependence on INSN1;
     2) X has a higher priority than INSN2; and
     3) X is an arithmetic instruction (and thus has no unit restrictions).

     If INSN1 is the last instruction blocking X, it would better to
     choose (INSN1, X) over (INSN2, INSN1).  */
  for (dep = INSN_DEPEND (insn1); dep != 0; dep = XEXP (dep, 1))
    if (REG_NOTE_KIND (dep) == REG_DEP_ANTI
	&& INSN_PRIORITY (XEXP (dep, 0)) > INSN_PRIORITY (insn2)
	&& recog_memoized (XEXP (dep, 0)) >= 0
	&& get_attr_vr4130_class (XEXP (dep, 0)) == VR4130_CLASS_ALU)
      return false;

  if (vr4130_last_insn != 0
      && recog_memoized (insn1) >= 0
      && recog_memoized (insn2) >= 0)
    {
      /* See whether INSN1 and INSN2 use different execution units,
	 or if they are both ALU-type instructions.  If so, they can
	 probably execute in parallel.  */
      enum attr_vr4130_class class1 = get_attr_vr4130_class (insn1);
      enum attr_vr4130_class class2 = get_attr_vr4130_class (insn2);
      if (class1 != class2 || class1 == VR4130_CLASS_ALU)
	{
	  /* If only one of the instructions has a dependence on
	     vr4130_last_insn, prefer to schedule the other one first.  */
	  bool dep1 = vr4130_true_reg_dependence_p (insn1);
	  bool dep2 = vr4130_true_reg_dependence_p (insn2);
	  if (dep1 != dep2)
	    return dep1;

	  /* Prefer to schedule INSN2 ahead of INSN1 if vr4130_last_insn
	     is not an ALU-type instruction and if INSN1 uses the same
	     execution unit.  (Note that if this condition holds, we already
	     know that INSN2 uses a different execution unit.)  */
	  if (class1 != VR4130_CLASS_ALU
	      && recog_memoized (vr4130_last_insn) >= 0
	      && class1 == get_attr_vr4130_class (vr4130_last_insn))
	    return true;
	}
    }
  return false;
}

/* A TUNE_MIPS4130 helper function.  (READY, NREADY) describes a ready
   queue with at least two instructions.  Swap the first two if
   vr4130_swap_insns_p says that it could be worthwhile.  */

static void
vr4130_reorder (rtx *ready, int nready)
{
  if (vr4130_swap_insns_p (ready[nready - 1], ready[nready - 2]))
    mips_promote_ready (ready, nready - 2, nready - 1);
}

/* Remove the instruction at index LOWER from ready queue READY and
   reinsert it in front of the instruction at index HIGHER.  LOWER must
   be <= HIGHER.  */

static void
mips_promote_ready (rtx *ready, int lower, int higher)
{
  rtx new_head;
  int i;

  new_head = ready[lower];
  for (i = lower; i < higher; i++)
    ready[i] = ready[i + 1];
  ready[i] = new_head;
}

/* Implement TARGET_SCHED_REORDER.  */

static int
mips_sched_reorder (FILE *file ATTRIBUTE_UNUSED, int verbose ATTRIBUTE_UNUSED,
		    rtx *ready, int *nreadyp, int cycle)
{
  if (!reload_completed && TUNE_MACC_CHAINS)
    {
      if (cycle == 0)
	mips_macc_chains_last_hilo = 0;
      if (*nreadyp > 0)
	mips_macc_chains_reorder (ready, *nreadyp);
    }
  if (reload_completed && TUNE_MIPS4130 && !TARGET_VR4130_ALIGN)
    {
      if (cycle == 0)
	vr4130_last_insn = 0;
      if (*nreadyp > 1)
	vr4130_reorder (ready, *nreadyp);
    }
  return mips_issue_rate ();
}

/* Implement TARGET_SCHED_VARIABLE_ISSUE.  */

static int
mips_variable_issue (FILE *file ATTRIBUTE_UNUSED, int verbose ATTRIBUTE_UNUSED,
		     rtx insn, int more)
{
  switch (GET_CODE (PATTERN (insn)))
    {
    case USE:
    case CLOBBER:
      /* Don't count USEs and CLOBBERs against the issue rate.  */
      break;

    default:
      more--;
      if (!reload_completed && TUNE_MACC_CHAINS)
	mips_macc_chains_record (insn);
      vr4130_last_insn = insn;
      break;
    }
  return more;
}

/* Implement TARGET_SCHED_ADJUST_COST.  We assume that anti and output
   dependencies have no cost.  */

static int
mips_adjust_cost (rtx insn ATTRIBUTE_UNUSED, rtx link,
		  rtx dep ATTRIBUTE_UNUSED, int cost)
{
  if (REG_NOTE_KIND (link) != 0)
    return 0;
  return cost;
}

/* Return the number of instructions that can be issued per cycle.  */

static int
mips_issue_rate (void)
{
  switch (mips_tune)
    {
    case PROCESSOR_R4130:
    case PROCESSOR_R5400:
    case PROCESSOR_R5500:
    case PROCESSOR_R7000:
    case PROCESSOR_R9000:
      return 2;

    case PROCESSOR_SB1:
    case PROCESSOR_SB1A:
      /* This is actually 4, but we get better performance if we claim 3.
	 This is partly because of unwanted speculative code motion with the
	 larger number, and partly because in most common cases we can't
	 reach the theoretical max of 4.  */
      return 3;

    default:
      return 1;
    }
}

/* Implements TARGET_SCHED_FIRST_CYCLE_MULTIPASS_DFA_LOOKAHEAD.  This should
   be as wide as the scheduling freedom in the DFA.  */

static int
mips_multipass_dfa_lookahead (void)
{
  /* Can schedule up to 4 of the 6 function units in any one cycle.  */
  if (TUNE_SB1)
    return 4;

  return 0;
}

/* Implements a store data bypass check.  We need this because the cprestore
   pattern is type store, but defined using an UNSPEC.  This UNSPEC causes the
   default routine to abort.  We just return false for that case.  */
/* ??? Should try to give a better result here than assuming false.  */

int
mips_store_data_bypass_p (rtx out_insn, rtx in_insn)
{
  if (GET_CODE (PATTERN (in_insn)) == UNSPEC_VOLATILE)
    return false;

  return ! store_data_bypass_p (out_insn, in_insn);
}

/* Given that we have an rtx of the form (prefetch ... WRITE LOCALITY),
   return the first operand of the associated "pref" or "prefx" insn.  */

rtx
mips_prefetch_cookie (rtx write, rtx locality)
{
  /* store_streamed / load_streamed.  */
  if (INTVAL (locality) <= 0)
    return GEN_INT (INTVAL (write) + 4);

  /* store / load.  */
  if (INTVAL (locality) <= 2)
    return write;

  /* store_retained / load_retained.  */
  return GEN_INT (INTVAL (write) + 6);
}

/* MIPS builtin function support. */

struct builtin_description
{
  /* The code of the main .md file instruction.  See mips_builtin_type
     for more information.  */
  enum insn_code icode;

  /* The floating-point comparison code to use with ICODE, if any.  */
  enum mips_fp_condition cond;

  /* The name of the builtin function.  */
  const char *name;

  /* Specifies how the function should be expanded.  */
  enum mips_builtin_type builtin_type;

  /* The function's prototype.  */
  enum mips_function_type function_type;

  /* The target flags required for this function.  */
  int target_flags;
};

/* Define a MIPS_BUILTIN_DIRECT function for instruction CODE_FOR_mips_<INSN>.
   FUNCTION_TYPE and TARGET_FLAGS are builtin_description fields.  */
#define DIRECT_BUILTIN(INSN, FUNCTION_TYPE, TARGET_FLAGS)		\
  { CODE_FOR_mips_ ## INSN, 0, "__builtin_mips_" #INSN,			\
    MIPS_BUILTIN_DIRECT, FUNCTION_TYPE, TARGET_FLAGS }

/* Define __builtin_mips_<INSN>_<COND>_{s,d}, both of which require
   TARGET_FLAGS.  */
#define CMP_SCALAR_BUILTINS(INSN, COND, TARGET_FLAGS)			\
  { CODE_FOR_mips_ ## INSN ## _cond_s, MIPS_FP_COND_ ## COND,		\
    "__builtin_mips_" #INSN "_" #COND "_s",				\
    MIPS_BUILTIN_CMP_SINGLE, MIPS_INT_FTYPE_SF_SF, TARGET_FLAGS },	\
  { CODE_FOR_mips_ ## INSN ## _cond_d, MIPS_FP_COND_ ## COND,		\
    "__builtin_mips_" #INSN "_" #COND "_d",				\
    MIPS_BUILTIN_CMP_SINGLE, MIPS_INT_FTYPE_DF_DF, TARGET_FLAGS }

/* Define __builtin_mips_{any,all,upper,lower}_<INSN>_<COND>_ps.
   The lower and upper forms require TARGET_FLAGS while the any and all
   forms require MASK_MIPS3D.  */
#define CMP_PS_BUILTINS(INSN, COND, TARGET_FLAGS)			\
  { CODE_FOR_mips_ ## INSN ## _cond_ps, MIPS_FP_COND_ ## COND,		\
    "__builtin_mips_any_" #INSN "_" #COND "_ps",			\
    MIPS_BUILTIN_CMP_ANY, MIPS_INT_FTYPE_V2SF_V2SF, MASK_MIPS3D },	\
  { CODE_FOR_mips_ ## INSN ## _cond_ps, MIPS_FP_COND_ ## COND,		\
    "__builtin_mips_all_" #INSN "_" #COND "_ps",			\
    MIPS_BUILTIN_CMP_ALL, MIPS_INT_FTYPE_V2SF_V2SF, MASK_MIPS3D },	\
  { CODE_FOR_mips_ ## INSN ## _cond_ps, MIPS_FP_COND_ ## COND,		\
    "__builtin_mips_lower_" #INSN "_" #COND "_ps",			\
    MIPS_BUILTIN_CMP_LOWER, MIPS_INT_FTYPE_V2SF_V2SF, TARGET_FLAGS },	\
  { CODE_FOR_mips_ ## INSN ## _cond_ps, MIPS_FP_COND_ ## COND,		\
    "__builtin_mips_upper_" #INSN "_" #COND "_ps",			\
    MIPS_BUILTIN_CMP_UPPER, MIPS_INT_FTYPE_V2SF_V2SF, TARGET_FLAGS }

/* Define __builtin_mips_{any,all}_<INSN>_<COND>_4s.  The functions
   require MASK_MIPS3D.  */
#define CMP_4S_BUILTINS(INSN, COND)					\
  { CODE_FOR_mips_ ## INSN ## _cond_4s, MIPS_FP_COND_ ## COND,		\
    "__builtin_mips_any_" #INSN "_" #COND "_4s",			\
    MIPS_BUILTIN_CMP_ANY, MIPS_INT_FTYPE_V2SF_V2SF_V2SF_V2SF,		\
    MASK_MIPS3D },							\
  { CODE_FOR_mips_ ## INSN ## _cond_4s, MIPS_FP_COND_ ## COND,		\
    "__builtin_mips_all_" #INSN "_" #COND "_4s",			\
    MIPS_BUILTIN_CMP_ALL, MIPS_INT_FTYPE_V2SF_V2SF_V2SF_V2SF,		\
    MASK_MIPS3D }

/* Define __builtin_mips_mov{t,f}_<INSN>_<COND>_ps.  The comparison
   instruction requires TARGET_FLAGS.  */
#define MOVTF_BUILTINS(INSN, COND, TARGET_FLAGS)			\
  { CODE_FOR_mips_ ## INSN ## _cond_ps, MIPS_FP_COND_ ## COND,		\
    "__builtin_mips_movt_" #INSN "_" #COND "_ps",			\
    MIPS_BUILTIN_MOVT, MIPS_V2SF_FTYPE_V2SF_V2SF_V2SF_V2SF,		\
    TARGET_FLAGS },							\
  { CODE_FOR_mips_ ## INSN ## _cond_ps, MIPS_FP_COND_ ## COND,		\
    "__builtin_mips_movf_" #INSN "_" #COND "_ps",			\
    MIPS_BUILTIN_MOVF, MIPS_V2SF_FTYPE_V2SF_V2SF_V2SF_V2SF,		\
    TARGET_FLAGS }

/* Define all the builtins related to c.cond.fmt condition COND.  */
#define CMP_BUILTINS(COND)						\
  MOVTF_BUILTINS (c, COND, MASK_PAIRED_SINGLE_FLOAT),			\
  MOVTF_BUILTINS (cabs, COND, MASK_MIPS3D),				\
  CMP_SCALAR_BUILTINS (cabs, COND, MASK_MIPS3D),			\
  CMP_PS_BUILTINS (c, COND, MASK_PAIRED_SINGLE_FLOAT),			\
  CMP_PS_BUILTINS (cabs, COND, MASK_MIPS3D),				\
  CMP_4S_BUILTINS (c, COND),						\
  CMP_4S_BUILTINS (cabs, COND)

static const struct builtin_description mips_bdesc[] =
{
  DIRECT_BUILTIN (pll_ps, MIPS_V2SF_FTYPE_V2SF_V2SF, MASK_PAIRED_SINGLE_FLOAT),
  DIRECT_BUILTIN (pul_ps, MIPS_V2SF_FTYPE_V2SF_V2SF, MASK_PAIRED_SINGLE_FLOAT),
  DIRECT_BUILTIN (plu_ps, MIPS_V2SF_FTYPE_V2SF_V2SF, MASK_PAIRED_SINGLE_FLOAT),
  DIRECT_BUILTIN (puu_ps, MIPS_V2SF_FTYPE_V2SF_V2SF, MASK_PAIRED_SINGLE_FLOAT),
  DIRECT_BUILTIN (cvt_ps_s, MIPS_V2SF_FTYPE_SF_SF, MASK_PAIRED_SINGLE_FLOAT),
  DIRECT_BUILTIN (cvt_s_pl, MIPS_SF_FTYPE_V2SF, MASK_PAIRED_SINGLE_FLOAT),
  DIRECT_BUILTIN (cvt_s_pu, MIPS_SF_FTYPE_V2SF, MASK_PAIRED_SINGLE_FLOAT),
  DIRECT_BUILTIN (abs_ps, MIPS_V2SF_FTYPE_V2SF, MASK_PAIRED_SINGLE_FLOAT),

  DIRECT_BUILTIN (alnv_ps, MIPS_V2SF_FTYPE_V2SF_V2SF_INT,
		  MASK_PAIRED_SINGLE_FLOAT),
  DIRECT_BUILTIN (addr_ps, MIPS_V2SF_FTYPE_V2SF_V2SF, MASK_MIPS3D),
  DIRECT_BUILTIN (mulr_ps, MIPS_V2SF_FTYPE_V2SF_V2SF, MASK_MIPS3D),
  DIRECT_BUILTIN (cvt_pw_ps, MIPS_V2SF_FTYPE_V2SF, MASK_MIPS3D),
  DIRECT_BUILTIN (cvt_ps_pw, MIPS_V2SF_FTYPE_V2SF, MASK_MIPS3D),

  DIRECT_BUILTIN (recip1_s, MIPS_SF_FTYPE_SF, MASK_MIPS3D),
  DIRECT_BUILTIN (recip1_d, MIPS_DF_FTYPE_DF, MASK_MIPS3D),
  DIRECT_BUILTIN (recip1_ps, MIPS_V2SF_FTYPE_V2SF, MASK_MIPS3D),
  DIRECT_BUILTIN (recip2_s, MIPS_SF_FTYPE_SF_SF, MASK_MIPS3D),
  DIRECT_BUILTIN (recip2_d, MIPS_DF_FTYPE_DF_DF, MASK_MIPS3D),
  DIRECT_BUILTIN (recip2_ps, MIPS_V2SF_FTYPE_V2SF_V2SF, MASK_MIPS3D),

  DIRECT_BUILTIN (rsqrt1_s, MIPS_SF_FTYPE_SF, MASK_MIPS3D),
  DIRECT_BUILTIN (rsqrt1_d, MIPS_DF_FTYPE_DF, MASK_MIPS3D),
  DIRECT_BUILTIN (rsqrt1_ps, MIPS_V2SF_FTYPE_V2SF, MASK_MIPS3D),
  DIRECT_BUILTIN (rsqrt2_s, MIPS_SF_FTYPE_SF_SF, MASK_MIPS3D),
  DIRECT_BUILTIN (rsqrt2_d, MIPS_DF_FTYPE_DF_DF, MASK_MIPS3D),
  DIRECT_BUILTIN (rsqrt2_ps, MIPS_V2SF_FTYPE_V2SF_V2SF, MASK_MIPS3D),

  MIPS_FP_CONDITIONS (CMP_BUILTINS)
};

/* Builtin functions for the SB-1 processor.  */

#define CODE_FOR_mips_sqrt_ps CODE_FOR_sqrtv2sf2

static const struct builtin_description sb1_bdesc[] =
{
  DIRECT_BUILTIN (sqrt_ps, MIPS_V2SF_FTYPE_V2SF, MASK_PAIRED_SINGLE_FLOAT)
};

/* Builtin functions for DSP ASE.  */

#define CODE_FOR_mips_addq_ph CODE_FOR_addv2hi3
#define CODE_FOR_mips_addu_qb CODE_FOR_addv4qi3
#define CODE_FOR_mips_subq_ph CODE_FOR_subv2hi3
#define CODE_FOR_mips_subu_qb CODE_FOR_subv4qi3

/* Define a MIPS_BUILTIN_DIRECT_NO_TARGET function for instruction
   CODE_FOR_mips_<INSN>.  FUNCTION_TYPE and TARGET_FLAGS are
   builtin_description fields.  */
#define DIRECT_NO_TARGET_BUILTIN(INSN, FUNCTION_TYPE, TARGET_FLAGS)	\
  { CODE_FOR_mips_ ## INSN, 0, "__builtin_mips_" #INSN,			\
    MIPS_BUILTIN_DIRECT_NO_TARGET, FUNCTION_TYPE, TARGET_FLAGS }

/* Define __builtin_mips_bposge<VALUE>.  <VALUE> is 32 for the MIPS32 DSP
   branch instruction.  TARGET_FLAGS is a builtin_description field.  */
#define BPOSGE_BUILTIN(VALUE, TARGET_FLAGS)				\
  { CODE_FOR_mips_bposge, 0, "__builtin_mips_bposge" #VALUE,		\
    MIPS_BUILTIN_BPOSGE ## VALUE, MIPS_SI_FTYPE_VOID, TARGET_FLAGS }

static const struct builtin_description dsp_bdesc[] =
{
  DIRECT_BUILTIN (addq_ph, MIPS_V2HI_FTYPE_V2HI_V2HI, MASK_DSP),
  DIRECT_BUILTIN (addq_s_ph, MIPS_V2HI_FTYPE_V2HI_V2HI, MASK_DSP),
  DIRECT_BUILTIN (addq_s_w, MIPS_SI_FTYPE_SI_SI, MASK_DSP),
  DIRECT_BUILTIN (addu_qb, MIPS_V4QI_FTYPE_V4QI_V4QI, MASK_DSP),
  DIRECT_BUILTIN (addu_s_qb, MIPS_V4QI_FTYPE_V4QI_V4QI, MASK_DSP),
  DIRECT_BUILTIN (subq_ph, MIPS_V2HI_FTYPE_V2HI_V2HI, MASK_DSP),
  DIRECT_BUILTIN (subq_s_ph, MIPS_V2HI_FTYPE_V2HI_V2HI, MASK_DSP),
  DIRECT_BUILTIN (subq_s_w, MIPS_SI_FTYPE_SI_SI, MASK_DSP),
  DIRECT_BUILTIN (subu_qb, MIPS_V4QI_FTYPE_V4QI_V4QI, MASK_DSP),
  DIRECT_BUILTIN (subu_s_qb, MIPS_V4QI_FTYPE_V4QI_V4QI, MASK_DSP),
  DIRECT_BUILTIN (addsc, MIPS_SI_FTYPE_SI_SI, MASK_DSP),
  DIRECT_BUILTIN (addwc, MIPS_SI_FTYPE_SI_SI, MASK_DSP),
  DIRECT_BUILTIN (modsub, MIPS_SI_FTYPE_SI_SI, MASK_DSP),
  DIRECT_BUILTIN (raddu_w_qb, MIPS_SI_FTYPE_V4QI, MASK_DSP),
  DIRECT_BUILTIN (absq_s_ph, MIPS_V2HI_FTYPE_V2HI, MASK_DSP),
  DIRECT_BUILTIN (absq_s_w, MIPS_SI_FTYPE_SI, MASK_DSP),
  DIRECT_BUILTIN (precrq_qb_ph, MIPS_V4QI_FTYPE_V2HI_V2HI, MASK_DSP),
  DIRECT_BUILTIN (precrq_ph_w, MIPS_V2HI_FTYPE_SI_SI, MASK_DSP),
  DIRECT_BUILTIN (precrq_rs_ph_w, MIPS_V2HI_FTYPE_SI_SI, MASK_DSP),
  DIRECT_BUILTIN (precrqu_s_qb_ph, MIPS_V4QI_FTYPE_V2HI_V2HI, MASK_DSP),
  DIRECT_BUILTIN (preceq_w_phl, MIPS_SI_FTYPE_V2HI, MASK_DSP),
  DIRECT_BUILTIN (preceq_w_phr, MIPS_SI_FTYPE_V2HI, MASK_DSP),
  DIRECT_BUILTIN (precequ_ph_qbl, MIPS_V2HI_FTYPE_V4QI, MASK_DSP),
  DIRECT_BUILTIN (precequ_ph_qbr, MIPS_V2HI_FTYPE_V4QI, MASK_DSP),
  DIRECT_BUILTIN (precequ_ph_qbla, MIPS_V2HI_FTYPE_V4QI, MASK_DSP),
  DIRECT_BUILTIN (precequ_ph_qbra, MIPS_V2HI_FTYPE_V4QI, MASK_DSP),
  DIRECT_BUILTIN (preceu_ph_qbl, MIPS_V2HI_FTYPE_V4QI, MASK_DSP),
  DIRECT_BUILTIN (preceu_ph_qbr, MIPS_V2HI_FTYPE_V4QI, MASK_DSP),
  DIRECT_BUILTIN (preceu_ph_qbla, MIPS_V2HI_FTYPE_V4QI, MASK_DSP),
  DIRECT_BUILTIN (preceu_ph_qbra, MIPS_V2HI_FTYPE_V4QI, MASK_DSP),
  DIRECT_BUILTIN (shll_qb, MIPS_V4QI_FTYPE_V4QI_SI, MASK_DSP),
  DIRECT_BUILTIN (shll_ph, MIPS_V2HI_FTYPE_V2HI_SI, MASK_DSP),
  DIRECT_BUILTIN (shll_s_ph, MIPS_V2HI_FTYPE_V2HI_SI, MASK_DSP),
  DIRECT_BUILTIN (shll_s_w, MIPS_SI_FTYPE_SI_SI, MASK_DSP),
  DIRECT_BUILTIN (shrl_qb, MIPS_V4QI_FTYPE_V4QI_SI, MASK_DSP),
  DIRECT_BUILTIN (shra_ph, MIPS_V2HI_FTYPE_V2HI_SI, MASK_DSP),
  DIRECT_BUILTIN (shra_r_ph, MIPS_V2HI_FTYPE_V2HI_SI, MASK_DSP),
  DIRECT_BUILTIN (shra_r_w, MIPS_SI_FTYPE_SI_SI, MASK_DSP),
  DIRECT_BUILTIN (muleu_s_ph_qbl, MIPS_V2HI_FTYPE_V4QI_V2HI, MASK_DSP),
  DIRECT_BUILTIN (muleu_s_ph_qbr, MIPS_V2HI_FTYPE_V4QI_V2HI, MASK_DSP),
  DIRECT_BUILTIN (mulq_rs_ph, MIPS_V2HI_FTYPE_V2HI_V2HI, MASK_DSP),
  DIRECT_BUILTIN (muleq_s_w_phl, MIPS_SI_FTYPE_V2HI_V2HI, MASK_DSP),
  DIRECT_BUILTIN (muleq_s_w_phr, MIPS_SI_FTYPE_V2HI_V2HI, MASK_DSP),
  DIRECT_BUILTIN (dpau_h_qbl, MIPS_DI_FTYPE_DI_V4QI_V4QI, MASK_DSP),
  DIRECT_BUILTIN (dpau_h_qbr, MIPS_DI_FTYPE_DI_V4QI_V4QI, MASK_DSP),
  DIRECT_BUILTIN (dpsu_h_qbl, MIPS_DI_FTYPE_DI_V4QI_V4QI, MASK_DSP),
  DIRECT_BUILTIN (dpsu_h_qbr, MIPS_DI_FTYPE_DI_V4QI_V4QI, MASK_DSP),
  DIRECT_BUILTIN (dpaq_s_w_ph, MIPS_DI_FTYPE_DI_V2HI_V2HI, MASK_DSP),
  DIRECT_BUILTIN (dpsq_s_w_ph, MIPS_DI_FTYPE_DI_V2HI_V2HI, MASK_DSP),
  DIRECT_BUILTIN (mulsaq_s_w_ph, MIPS_DI_FTYPE_DI_V2HI_V2HI, MASK_DSP),
  DIRECT_BUILTIN (dpaq_sa_l_w, MIPS_DI_FTYPE_DI_SI_SI, MASK_DSP),
  DIRECT_BUILTIN (dpsq_sa_l_w, MIPS_DI_FTYPE_DI_SI_SI, MASK_DSP),
  DIRECT_BUILTIN (maq_s_w_phl, MIPS_DI_FTYPE_DI_V2HI_V2HI, MASK_DSP),
  DIRECT_BUILTIN (maq_s_w_phr, MIPS_DI_FTYPE_DI_V2HI_V2HI, MASK_DSP),
  DIRECT_BUILTIN (maq_sa_w_phl, MIPS_DI_FTYPE_DI_V2HI_V2HI, MASK_DSP),
  DIRECT_BUILTIN (maq_sa_w_phr, MIPS_DI_FTYPE_DI_V2HI_V2HI, MASK_DSP),
  DIRECT_BUILTIN (bitrev, MIPS_SI_FTYPE_SI, MASK_DSP),
  DIRECT_BUILTIN (insv, MIPS_SI_FTYPE_SI_SI, MASK_DSP),
  DIRECT_BUILTIN (repl_qb, MIPS_V4QI_FTYPE_SI, MASK_DSP),
  DIRECT_BUILTIN (repl_ph, MIPS_V2HI_FTYPE_SI, MASK_DSP),
  DIRECT_NO_TARGET_BUILTIN (cmpu_eq_qb, MIPS_VOID_FTYPE_V4QI_V4QI, MASK_DSP),
  DIRECT_NO_TARGET_BUILTIN (cmpu_lt_qb, MIPS_VOID_FTYPE_V4QI_V4QI, MASK_DSP),
  DIRECT_NO_TARGET_BUILTIN (cmpu_le_qb, MIPS_VOID_FTYPE_V4QI_V4QI, MASK_DSP),
  DIRECT_BUILTIN (cmpgu_eq_qb, MIPS_SI_FTYPE_V4QI_V4QI, MASK_DSP),
  DIRECT_BUILTIN (cmpgu_lt_qb, MIPS_SI_FTYPE_V4QI_V4QI, MASK_DSP),
  DIRECT_BUILTIN (cmpgu_le_qb, MIPS_SI_FTYPE_V4QI_V4QI, MASK_DSP),
  DIRECT_NO_TARGET_BUILTIN (cmp_eq_ph, MIPS_VOID_FTYPE_V2HI_V2HI, MASK_DSP),
  DIRECT_NO_TARGET_BUILTIN (cmp_lt_ph, MIPS_VOID_FTYPE_V2HI_V2HI, MASK_DSP),
  DIRECT_NO_TARGET_BUILTIN (cmp_le_ph, MIPS_VOID_FTYPE_V2HI_V2HI, MASK_DSP),
  DIRECT_BUILTIN (pick_qb, MIPS_V4QI_FTYPE_V4QI_V4QI, MASK_DSP),
  DIRECT_BUILTIN (pick_ph, MIPS_V2HI_FTYPE_V2HI_V2HI, MASK_DSP),
  DIRECT_BUILTIN (packrl_ph, MIPS_V2HI_FTYPE_V2HI_V2HI, MASK_DSP),
  DIRECT_BUILTIN (extr_w, MIPS_SI_FTYPE_DI_SI, MASK_DSP),
  DIRECT_BUILTIN (extr_r_w, MIPS_SI_FTYPE_DI_SI, MASK_DSP),
  DIRECT_BUILTIN (extr_rs_w, MIPS_SI_FTYPE_DI_SI, MASK_DSP),
  DIRECT_BUILTIN (extr_s_h, MIPS_SI_FTYPE_DI_SI, MASK_DSP),
  DIRECT_BUILTIN (extp, MIPS_SI_FTYPE_DI_SI, MASK_DSP),
  DIRECT_BUILTIN (extpdp, MIPS_SI_FTYPE_DI_SI, MASK_DSP),
  DIRECT_BUILTIN (shilo, MIPS_DI_FTYPE_DI_SI, MASK_DSP),
  DIRECT_BUILTIN (mthlip, MIPS_DI_FTYPE_DI_SI, MASK_DSP),
  DIRECT_NO_TARGET_BUILTIN (wrdsp, MIPS_VOID_FTYPE_SI_SI, MASK_DSP),
  DIRECT_BUILTIN (rddsp, MIPS_SI_FTYPE_SI, MASK_DSP),
  DIRECT_BUILTIN (lbux, MIPS_SI_FTYPE_PTR_SI, MASK_DSP),
  DIRECT_BUILTIN (lhx, MIPS_SI_FTYPE_PTR_SI, MASK_DSP),
  DIRECT_BUILTIN (lwx, MIPS_SI_FTYPE_PTR_SI, MASK_DSP),
  BPOSGE_BUILTIN (32, MASK_DSP)
};

/* This helps provide a mapping from builtin function codes to bdesc
   arrays.  */

struct bdesc_map
{
  /* The builtin function table that this entry describes.  */
  const struct builtin_description *bdesc;

  /* The number of entries in the builtin function table.  */
  unsigned int size;

  /* The target processor that supports these builtin functions.
     PROCESSOR_MAX means we enable them for all processors.  */
  enum processor_type proc;
};

static const struct bdesc_map bdesc_arrays[] =
{
  { mips_bdesc, ARRAY_SIZE (mips_bdesc), PROCESSOR_MAX },
  { sb1_bdesc, ARRAY_SIZE (sb1_bdesc), PROCESSOR_SB1 },
  { dsp_bdesc, ARRAY_SIZE (dsp_bdesc), PROCESSOR_MAX }
};

/* Take the head of argument list *ARGLIST and convert it into a form
   suitable for input operand OP of instruction ICODE.  Return the value
   and point *ARGLIST at the next element of the list.  */

static rtx
mips_prepare_builtin_arg (enum insn_code icode,
			  unsigned int op, tree *arglist)
{
  rtx value;
  enum machine_mode mode;

  value = expand_normal (TREE_VALUE (*arglist));
  mode = insn_data[icode].operand[op].mode;
  if (!insn_data[icode].operand[op].predicate (value, mode))
    {
      value = copy_to_mode_reg (mode, value);
      /* Check the predicate again.  */
      if (!insn_data[icode].operand[op].predicate (value, mode))
	{
	  error ("invalid argument to builtin function");
	  return const0_rtx;
	}
    }

  *arglist = TREE_CHAIN (*arglist);
  return value;
}

/* Return an rtx suitable for output operand OP of instruction ICODE.
   If TARGET is non-null, try to use it where possible.  */

static rtx
mips_prepare_builtin_target (enum insn_code icode, unsigned int op, rtx target)
{
  enum machine_mode mode;

  mode = insn_data[icode].operand[op].mode;
  if (target == 0 || !insn_data[icode].operand[op].predicate (target, mode))
    target = gen_reg_rtx (mode);

  return target;
}

/* Expand builtin functions.  This is called from TARGET_EXPAND_BUILTIN.  */

rtx
mips_expand_builtin (tree exp, rtx target, rtx subtarget ATTRIBUTE_UNUSED,
		     enum machine_mode mode ATTRIBUTE_UNUSED,
		     int ignore ATTRIBUTE_UNUSED)
{
  enum insn_code icode;
  enum mips_builtin_type type;
  tree fndecl, arglist;
  unsigned int fcode;
  const struct builtin_description *bdesc;
  const struct bdesc_map *m;

  fndecl = TREE_OPERAND (TREE_OPERAND (exp, 0), 0);
  arglist = TREE_OPERAND (exp, 1);
  fcode = DECL_FUNCTION_CODE (fndecl);

  bdesc = NULL;
  for (m = bdesc_arrays; m < &bdesc_arrays[ARRAY_SIZE (bdesc_arrays)]; m++)
    {
      if (fcode < m->size)
	{
	  bdesc = m->bdesc;
	  icode = bdesc[fcode].icode;
	  type = bdesc[fcode].builtin_type;
	  break;
	}
      fcode -= m->size;
    }
  if (bdesc == NULL)
    return 0;

  switch (type)
    {
    case MIPS_BUILTIN_DIRECT:
      return mips_expand_builtin_direct (icode, target, arglist, true);

    case MIPS_BUILTIN_DIRECT_NO_TARGET:
      return mips_expand_builtin_direct (icode, target, arglist, false);

    case MIPS_BUILTIN_MOVT:
    case MIPS_BUILTIN_MOVF:
      return mips_expand_builtin_movtf (type, icode, bdesc[fcode].cond,
					target, arglist);

    case MIPS_BUILTIN_CMP_ANY:
    case MIPS_BUILTIN_CMP_ALL:
    case MIPS_BUILTIN_CMP_UPPER:
    case MIPS_BUILTIN_CMP_LOWER:
    case MIPS_BUILTIN_CMP_SINGLE:
      return mips_expand_builtin_compare (type, icode, bdesc[fcode].cond,
					  target, arglist);

    case MIPS_BUILTIN_BPOSGE32:
      return mips_expand_builtin_bposge (type, target);

    default:
      return 0;
    }
}

/* Init builtin functions.  This is called from TARGET_INIT_BUILTIN.  */

void
mips_init_builtins (void)
{
  const struct builtin_description *d;
  const struct bdesc_map *m;
  tree types[(int) MIPS_MAX_FTYPE_MAX];
  tree V2SF_type_node;
  tree V2HI_type_node;
  tree V4QI_type_node;
  unsigned int offset;

  /* We have only builtins for -mpaired-single, -mips3d and -mdsp.  */
  if (!TARGET_PAIRED_SINGLE_FLOAT && !TARGET_DSP)
    return;

  if (TARGET_PAIRED_SINGLE_FLOAT)
    {
      V2SF_type_node = build_vector_type_for_mode (float_type_node, V2SFmode);

      types[MIPS_V2SF_FTYPE_V2SF]
	= build_function_type_list (V2SF_type_node, V2SF_type_node, NULL_TREE);

      types[MIPS_V2SF_FTYPE_V2SF_V2SF]
	= build_function_type_list (V2SF_type_node,
				    V2SF_type_node, V2SF_type_node, NULL_TREE);

      types[MIPS_V2SF_FTYPE_V2SF_V2SF_INT]
	= build_function_type_list (V2SF_type_node,
				    V2SF_type_node, V2SF_type_node,
				    integer_type_node, NULL_TREE);

      types[MIPS_V2SF_FTYPE_V2SF_V2SF_V2SF_V2SF]
	= build_function_type_list (V2SF_type_node,
				    V2SF_type_node, V2SF_type_node,
				    V2SF_type_node, V2SF_type_node, NULL_TREE);

      types[MIPS_V2SF_FTYPE_SF_SF]
	= build_function_type_list (V2SF_type_node,
				    float_type_node, float_type_node, NULL_TREE);

      types[MIPS_INT_FTYPE_V2SF_V2SF]
	= build_function_type_list (integer_type_node,
				    V2SF_type_node, V2SF_type_node, NULL_TREE);

      types[MIPS_INT_FTYPE_V2SF_V2SF_V2SF_V2SF]
	= build_function_type_list (integer_type_node,
				    V2SF_type_node, V2SF_type_node,
				    V2SF_type_node, V2SF_type_node, NULL_TREE);

      types[MIPS_INT_FTYPE_SF_SF]
	= build_function_type_list (integer_type_node,
				    float_type_node, float_type_node, NULL_TREE);

      types[MIPS_INT_FTYPE_DF_DF]
	= build_function_type_list (integer_type_node,
				    double_type_node, double_type_node, NULL_TREE);

      types[MIPS_SF_FTYPE_V2SF]
	= build_function_type_list (float_type_node, V2SF_type_node, NULL_TREE);

      types[MIPS_SF_FTYPE_SF]
	= build_function_type_list (float_type_node,
				    float_type_node, NULL_TREE);

      types[MIPS_SF_FTYPE_SF_SF]
	= build_function_type_list (float_type_node,
				    float_type_node, float_type_node, NULL_TREE);

      types[MIPS_DF_FTYPE_DF]
	= build_function_type_list (double_type_node,
				    double_type_node, NULL_TREE);

      types[MIPS_DF_FTYPE_DF_DF]
	= build_function_type_list (double_type_node,
				    double_type_node, double_type_node, NULL_TREE);
    }

  if (TARGET_DSP)
    {
      V2HI_type_node = build_vector_type_for_mode (intHI_type_node, V2HImode);
      V4QI_type_node = build_vector_type_for_mode (intQI_type_node, V4QImode);

      types[MIPS_V2HI_FTYPE_V2HI_V2HI]
	= build_function_type_list (V2HI_type_node,
				    V2HI_type_node, V2HI_type_node,
				    NULL_TREE);

      types[MIPS_SI_FTYPE_SI_SI]
	= build_function_type_list (intSI_type_node,
				    intSI_type_node, intSI_type_node,
				    NULL_TREE);

      types[MIPS_V4QI_FTYPE_V4QI_V4QI]
	= build_function_type_list (V4QI_type_node,
				    V4QI_type_node, V4QI_type_node,
				    NULL_TREE);

      types[MIPS_SI_FTYPE_V4QI]
	= build_function_type_list (intSI_type_node,
				    V4QI_type_node,
				    NULL_TREE);

      types[MIPS_V2HI_FTYPE_V2HI]
	= build_function_type_list (V2HI_type_node,
				    V2HI_type_node,
				    NULL_TREE);

      types[MIPS_SI_FTYPE_SI]
	= build_function_type_list (intSI_type_node,
				    intSI_type_node,
				    NULL_TREE);

      types[MIPS_V4QI_FTYPE_V2HI_V2HI]
	= build_function_type_list (V4QI_type_node,
				    V2HI_type_node, V2HI_type_node,
				    NULL_TREE);

      types[MIPS_V2HI_FTYPE_SI_SI]
	= build_function_type_list (V2HI_type_node,
				    intSI_type_node, intSI_type_node,
				    NULL_TREE);

      types[MIPS_SI_FTYPE_V2HI]
	= build_function_type_list (intSI_type_node,
				    V2HI_type_node,
				    NULL_TREE);

      types[MIPS_V2HI_FTYPE_V4QI]
	= build_function_type_list (V2HI_type_node,
				    V4QI_type_node,
				    NULL_TREE);

      types[MIPS_V4QI_FTYPE_V4QI_SI]
	= build_function_type_list (V4QI_type_node,
				    V4QI_type_node, intSI_type_node,
				    NULL_TREE);

      types[MIPS_V2HI_FTYPE_V2HI_SI]
	= build_function_type_list (V2HI_type_node,
				    V2HI_type_node, intSI_type_node,
				    NULL_TREE);

      types[MIPS_V2HI_FTYPE_V4QI_V2HI]
	= build_function_type_list (V2HI_type_node,
				    V4QI_type_node, V2HI_type_node,
				    NULL_TREE);

      types[MIPS_SI_FTYPE_V2HI_V2HI]
	= build_function_type_list (intSI_type_node,
				    V2HI_type_node, V2HI_type_node,
				    NULL_TREE);

      types[MIPS_DI_FTYPE_DI_V4QI_V4QI]
	= build_function_type_list (intDI_type_node,
				    intDI_type_node, V4QI_type_node, V4QI_type_node,
				    NULL_TREE);

      types[MIPS_DI_FTYPE_DI_V2HI_V2HI]
	= build_function_type_list (intDI_type_node,
				    intDI_type_node, V2HI_type_node, V2HI_type_node,
				    NULL_TREE);

      types[MIPS_DI_FTYPE_DI_SI_SI]
	= build_function_type_list (intDI_type_node,
				    intDI_type_node, intSI_type_node, intSI_type_node,
				    NULL_TREE);

      types[MIPS_V4QI_FTYPE_SI]
	= build_function_type_list (V4QI_type_node,
				    intSI_type_node,
				    NULL_TREE);

      types[MIPS_V2HI_FTYPE_SI]
	= build_function_type_list (V2HI_type_node,
				    intSI_type_node,
				    NULL_TREE);

      types[MIPS_VOID_FTYPE_V4QI_V4QI]
	= build_function_type_list (void_type_node,
				    V4QI_type_node, V4QI_type_node,
				    NULL_TREE);

      types[MIPS_SI_FTYPE_V4QI_V4QI]
	= build_function_type_list (intSI_type_node,
				    V4QI_type_node, V4QI_type_node,
				    NULL_TREE);

      types[MIPS_VOID_FTYPE_V2HI_V2HI]
	= build_function_type_list (void_type_node,
				    V2HI_type_node, V2HI_type_node,
				    NULL_TREE);

      types[MIPS_SI_FTYPE_DI_SI]
	= build_function_type_list (intSI_type_node,
				    intDI_type_node, intSI_type_node,
				    NULL_TREE);

      types[MIPS_DI_FTYPE_DI_SI]
	= build_function_type_list (intDI_type_node,
				    intDI_type_node, intSI_type_node,
				    NULL_TREE);

      types[MIPS_VOID_FTYPE_SI_SI]
	= build_function_type_list (void_type_node,
				    intSI_type_node, intSI_type_node,
				    NULL_TREE);

      types[MIPS_SI_FTYPE_PTR_SI]
	= build_function_type_list (intSI_type_node,
				    ptr_type_node, intSI_type_node,
				    NULL_TREE);

      types[MIPS_SI_FTYPE_VOID]
	= build_function_type (intSI_type_node, void_list_node);
    }

  /* Iterate through all of the bdesc arrays, initializing all of the
     builtin functions.  */

  offset = 0;
  for (m = bdesc_arrays; m < &bdesc_arrays[ARRAY_SIZE (bdesc_arrays)]; m++)
    {
      if (m->proc == PROCESSOR_MAX || (m->proc == mips_arch))
	for (d = m->bdesc; d < &m->bdesc[m->size]; d++)
	  if ((d->target_flags & target_flags) == d->target_flags)
	    lang_hooks.builtin_function (d->name, types[d->function_type],
					 d - m->bdesc + offset,
					 BUILT_IN_MD, NULL, NULL);
      offset += m->size;
    }
}

/* Expand a MIPS_BUILTIN_DIRECT function.  ICODE is the code of the
   .md pattern and ARGLIST is the list of function arguments.  TARGET,
   if nonnull, suggests a good place to put the result.
   HAS_TARGET indicates the function must return something.  */

static rtx
mips_expand_builtin_direct (enum insn_code icode, rtx target, tree arglist,
			    bool has_target)
{
  rtx ops[MAX_RECOG_OPERANDS];
  int i = 0;

  if (has_target)
    {
      /* We save target to ops[0].  */
      ops[0] = mips_prepare_builtin_target (icode, 0, target);
      i = 1;
    }

  /* We need to test if arglist is not zero.  Some instructions have extra
     clobber registers.  */
  for (; i < insn_data[icode].n_operands && arglist != 0; i++)
    ops[i] = mips_prepare_builtin_arg (icode, i, &arglist);

  switch (i)
    {
    case 2:
      emit_insn (GEN_FCN (icode) (ops[0], ops[1]));
      break;

    case 3:
      emit_insn (GEN_FCN (icode) (ops[0], ops[1], ops[2]));
      break;

    case 4:
      emit_insn (GEN_FCN (icode) (ops[0], ops[1], ops[2], ops[3]));
      break;

    default:
      gcc_unreachable ();
    }
  return target;
}

/* Expand a __builtin_mips_movt_*_ps() or __builtin_mips_movf_*_ps()
   function (TYPE says which).  ARGLIST is the list of arguments to the
   function, ICODE is the instruction that should be used to compare
   the first two arguments, and COND is the condition it should test.
   TARGET, if nonnull, suggests a good place to put the result.  */

static rtx
mips_expand_builtin_movtf (enum mips_builtin_type type,
			   enum insn_code icode, enum mips_fp_condition cond,
			   rtx target, tree arglist)
{
  rtx cmp_result, op0, op1;

  cmp_result = mips_prepare_builtin_target (icode, 0, 0);
  op0 = mips_prepare_builtin_arg (icode, 1, &arglist);
  op1 = mips_prepare_builtin_arg (icode, 2, &arglist);
  emit_insn (GEN_FCN (icode) (cmp_result, op0, op1, GEN_INT (cond)));

  icode = CODE_FOR_mips_cond_move_tf_ps;
  target = mips_prepare_builtin_target (icode, 0, target);
  if (type == MIPS_BUILTIN_MOVT)
    {
      op1 = mips_prepare_builtin_arg (icode, 2, &arglist);
      op0 = mips_prepare_builtin_arg (icode, 1, &arglist);
    }
  else
    {
      op0 = mips_prepare_builtin_arg (icode, 1, &arglist);
      op1 = mips_prepare_builtin_arg (icode, 2, &arglist);
    }
  emit_insn (gen_mips_cond_move_tf_ps (target, op0, op1, cmp_result));
  return target;
}

/* Move VALUE_IF_TRUE into TARGET if CONDITION is true; move VALUE_IF_FALSE
   into TARGET otherwise.  Return TARGET.  */

static rtx
mips_builtin_branch_and_move (rtx condition, rtx target,
			      rtx value_if_true, rtx value_if_false)
{
  rtx true_label, done_label;

  true_label = gen_label_rtx ();
  done_label = gen_label_rtx ();

  /* First assume that CONDITION is false.  */
  emit_move_insn (target, value_if_false);

  /* Branch to TRUE_LABEL if CONDITION is true and DONE_LABEL otherwise.  */
  emit_jump_insn (gen_condjump (condition, true_label));
  emit_jump_insn (gen_jump (done_label));
  emit_barrier ();

  /* Fix TARGET if CONDITION is true.  */
  emit_label (true_label);
  emit_move_insn (target, value_if_true);

  emit_label (done_label);
  return target;
}

/* Expand a comparison builtin of type BUILTIN_TYPE.  ICODE is the code
   of the comparison instruction and COND is the condition it should test.
   ARGLIST is the list of function arguments and TARGET, if nonnull,
   suggests a good place to put the boolean result.  */

static rtx
mips_expand_builtin_compare (enum mips_builtin_type builtin_type,
			     enum insn_code icode, enum mips_fp_condition cond,
			     rtx target, tree arglist)
{
  rtx offset, condition, cmp_result, ops[MAX_RECOG_OPERANDS];
  int i;

  if (target == 0 || GET_MODE (target) != SImode)
    target = gen_reg_rtx (SImode);

  /* Prepare the operands to the comparison.  */
  cmp_result = mips_prepare_builtin_target (icode, 0, 0);
  for (i = 1; i < insn_data[icode].n_operands - 1; i++)
    ops[i] = mips_prepare_builtin_arg (icode, i, &arglist);

  switch (insn_data[icode].n_operands)
    {
    case 4:
      emit_insn (GEN_FCN (icode) (cmp_result, ops[1], ops[2], GEN_INT (cond)));
      break;

    case 6:
      emit_insn (GEN_FCN (icode) (cmp_result, ops[1], ops[2],
				  ops[3], ops[4], GEN_INT (cond)));
      break;

    default:
      gcc_unreachable ();
    }

  /* If the comparison sets more than one register, we define the result
     to be 0 if all registers are false and -1 if all registers are true.
     The value of the complete result is indeterminate otherwise.  */
  switch (builtin_type)
    {
    case MIPS_BUILTIN_CMP_ALL:
      condition = gen_rtx_NE (VOIDmode, cmp_result, constm1_rtx);
      return mips_builtin_branch_and_move (condition, target,
					   const0_rtx, const1_rtx);

    case MIPS_BUILTIN_CMP_UPPER:
    case MIPS_BUILTIN_CMP_LOWER:
      offset = GEN_INT (builtin_type == MIPS_BUILTIN_CMP_UPPER);
      condition = gen_single_cc (cmp_result, offset);
      return mips_builtin_branch_and_move (condition, target,
					   const1_rtx, const0_rtx);

    default:
      condition = gen_rtx_NE (VOIDmode, cmp_result, const0_rtx);
      return mips_builtin_branch_and_move (condition, target,
					   const1_rtx, const0_rtx);
    }
}

/* Expand a bposge builtin of type BUILTIN_TYPE.  TARGET, if nonnull,
   suggests a good place to put the boolean result.  */

static rtx
mips_expand_builtin_bposge (enum mips_builtin_type builtin_type, rtx target)
{
  rtx condition, cmp_result;
  int cmp_value;

  if (target == 0 || GET_MODE (target) != SImode)
    target = gen_reg_rtx (SImode);

  cmp_result = gen_rtx_REG (CCDSPmode, CCDSP_PO_REGNUM);

  if (builtin_type == MIPS_BUILTIN_BPOSGE32)
    cmp_value = 32;
  else
    gcc_assert (0);

  condition = gen_rtx_GE (VOIDmode, cmp_result, GEN_INT (cmp_value));
  return mips_builtin_branch_and_move (condition, target,
				       const1_rtx, const0_rtx);
}

/* Set SYMBOL_REF_FLAGS for the SYMBOL_REF inside RTL, which belongs to DECL.
   FIRST is true if this is the first time handling this decl.  */

static void
mips_encode_section_info (tree decl, rtx rtl, int first)
{
  default_encode_section_info (decl, rtl, first);

  if (TREE_CODE (decl) == FUNCTION_DECL
      && lookup_attribute ("long_call", TYPE_ATTRIBUTES (TREE_TYPE (decl))))
    {
      rtx symbol = XEXP (rtl, 0);
      SYMBOL_REF_FLAGS (symbol) |= SYMBOL_FLAG_LONG_CALL;
    }
}

/* Implement TARGET_EXTRA_LIVE_ON_ENTRY.  PIC_FUNCTION_ADDR_REGNUM is live
   on entry to a function when generating -mshared abicalls code.  */

static void
mips_extra_live_on_entry (bitmap regs)
{
  if (TARGET_ABICALLS && !TARGET_ABSOLUTE_ABICALLS)
    bitmap_set_bit (regs, PIC_FUNCTION_ADDR_REGNUM);
}

/* SImode values are represented as sign-extended to DImode.  */

int
mips_mode_rep_extended (enum machine_mode mode, enum machine_mode mode_rep)
{
  if (TARGET_64BIT && mode == SImode && mode_rep == DImode)
    return SIGN_EXTEND;

  return UNKNOWN;
}

#include "gt-mips.h"
