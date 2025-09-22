/* Definitions for code generation pass of GNU compiler.
   Copyright (C) 2001, 2002, 2003, 2004, 2005, 2006 
   Free Software Foundation, Inc.

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

#ifndef GCC_OPTABS_H
#define GCC_OPTABS_H

#include "insn-codes.h"

/* Optabs are tables saying how to generate insn bodies
   for various machine modes and numbers of operands.
   Each optab applies to one operation.
   For example, add_optab applies to addition.

   The insn_code slot is the enum insn_code that says how to
   generate an insn for this operation on a particular machine mode.
   It is CODE_FOR_nothing if there is no such insn on the target machine.

   The `lib_call' slot is the name of the library function that
   can be used to perform the operation.

   A few optabs, such as move_optab and cmp_optab, are used
   by special code.  */

struct optab_handlers GTY(())
{
  enum insn_code insn_code;
  rtx libfunc;
};

struct optab GTY(())
{
  enum rtx_code code;
  struct optab_handlers handlers[NUM_MACHINE_MODES];
};
typedef struct optab * optab;

/* A convert_optab is for some sort of conversion operation between
   modes.  The first array index is the destination mode, the second
   is the source mode.  */
struct convert_optab GTY(())
{
  enum rtx_code code;
  struct optab_handlers handlers[NUM_MACHINE_MODES][NUM_MACHINE_MODES];
};
typedef struct convert_optab *convert_optab;

/* Given an enum insn_code, access the function to construct
   the body of that kind of insn.  */
#define GEN_FCN(CODE) (insn_data[CODE].genfun)

/* Enumeration of valid indexes into optab_table.  */
enum optab_index
{
  OTI_add,
  OTI_addv,
  OTI_sub,
  OTI_subv,

  /* Signed and fp multiply */
  OTI_smul,
  OTI_smulv,
  /* Signed multiply, return high word */
  OTI_smul_highpart,
  OTI_umul_highpart,
  /* Signed multiply with result one machine mode wider than args */
  OTI_smul_widen,
  OTI_umul_widen,
  /* Widening multiply of one unsigned and one signed operand.  */
  OTI_usmul_widen,

  /* Signed divide */
  OTI_sdiv,
  OTI_sdivv,
  /* Signed divide-and-remainder in one */
  OTI_sdivmod,
  OTI_udiv,
  OTI_udivmod,
  /* Signed remainder */
  OTI_smod,
  OTI_umod,
  /* Floating point remainder functions */
  OTI_fmod,
  OTI_drem,
  /* Convert float to integer in float fmt */
  OTI_ftrunc,

  /* Logical and */
  OTI_and,
  /* Logical or */
  OTI_ior,
  /* Logical xor */
  OTI_xor,

  /* Arithmetic shift left */
  OTI_ashl,
  /* Logical shift right */
  OTI_lshr,
  /* Arithmetic shift right */
  OTI_ashr,
  /* Rotate left */
  OTI_rotl,
  /* Rotate right */
  OTI_rotr,
  /* Signed and floating-point minimum value */
  OTI_smin,
  /* Signed and floating-point maximum value */
  OTI_smax,
  /* Unsigned minimum value */
  OTI_umin,
  /* Unsigned maximum value */
  OTI_umax,
  /* Power */
  OTI_pow,
  /* Arc tangent of y/x */
  OTI_atan2,

  /* Move instruction.  */
  OTI_mov,
  /* Move, preserving high part of register.  */
  OTI_movstrict,
  /* Move, with a misaligned memory.  */
  OTI_movmisalign,

  /* Unary operations */
  /* Negation */
  OTI_neg,
  OTI_negv,
  /* Abs value */
  OTI_abs,
  OTI_absv,
  /* Bitwise not */
  OTI_one_cmpl,
  /* Bit scanning and counting */
  OTI_ffs,
  OTI_clz,
  OTI_ctz,
  OTI_popcount,
  OTI_parity,
  /* Square root */
  OTI_sqrt,
  /* Sine-Cosine */
  OTI_sincos,
  /* Sine */
  OTI_sin,
  /* Inverse sine */
  OTI_asin,
  /* Cosine */
  OTI_cos,
  /* Inverse cosine */
  OTI_acos,
  /* Exponential */
  OTI_exp,
  /* Base-10 Exponential */
  OTI_exp10,
  /* Base-2 Exponential */
  OTI_exp2,
  /* Exponential - 1*/
  OTI_expm1,
  /* Load exponent of a floating point number */
  OTI_ldexp,
  /* Radix-independent exponent */
  OTI_logb,
  OTI_ilogb,
  /* Natural Logarithm */
  OTI_log,
  /* Base-10 Logarithm */
  OTI_log10,
  /* Base-2 Logarithm */
  OTI_log2,
  /* logarithm of 1 plus argument */
  OTI_log1p,
  /* Rounding functions */
  OTI_floor,
  OTI_lfloor,
  OTI_ceil,
  OTI_lceil,
  OTI_btrunc,
  OTI_round,
  OTI_nearbyint,
  OTI_rint,
  OTI_lrint,
  /* Tangent */
  OTI_tan,
  /* Inverse tangent */
  OTI_atan,
  /* Copy sign */
  OTI_copysign,

  /* Compare insn; two operands.  */
  OTI_cmp,
  /* Used only for libcalls for unsigned comparisons.  */
  OTI_ucmp,
  /* tst insn; compare one operand against 0 */
  OTI_tst,

  /* Floating point comparison optabs - used primarily for libfuncs */
  OTI_eq,
  OTI_ne,
  OTI_gt,
  OTI_ge,
  OTI_lt,
  OTI_le,
  OTI_unord,

  /* String length */
  OTI_strlen,

  /* Combined compare & jump/store flags/move operations.  */
  OTI_cbranch,
  OTI_cmov,
  OTI_cstore,

  /* Push instruction.  */
  OTI_push,

  /* Conditional add instruction.  */
  OTI_addcc,

  /* Reduction operations on a vector operand.  */
  OTI_reduc_smax,
  OTI_reduc_umax,
  OTI_reduc_smin,
  OTI_reduc_umin,
  OTI_reduc_splus,
  OTI_reduc_uplus,

  /* Summation, with result machine mode one or more wider than args.  */
  OTI_ssum_widen,
  OTI_usum_widen,

  /* Dot product, with result machine mode one or more wider than args.  */
  OTI_sdot_prod,
  OTI_udot_prod,

  /* Set specified field of vector operand.  */
  OTI_vec_set,
  /* Extract specified field of vector operand.  */
  OTI_vec_extract,
  /* Initialize vector operand.  */
  OTI_vec_init,
  /* Whole vector shift. The shift amount is in bits.  */
  OTI_vec_shl,
  OTI_vec_shr,
  /* Extract specified elements from vectors, for vector load.  */
  OTI_vec_realign_load,

  /* Perform a raise to the power of integer.  */
  OTI_powi,

  OTI_MAX
};

extern GTY(()) optab optab_table[OTI_MAX];

#define add_optab (optab_table[OTI_add])
#define sub_optab (optab_table[OTI_sub])
#define smul_optab (optab_table[OTI_smul])
#define addv_optab (optab_table[OTI_addv])
#define subv_optab (optab_table[OTI_subv])
#define smul_highpart_optab (optab_table[OTI_smul_highpart])
#define umul_highpart_optab (optab_table[OTI_umul_highpart])
#define smul_widen_optab (optab_table[OTI_smul_widen])
#define umul_widen_optab (optab_table[OTI_umul_widen])
#define usmul_widen_optab (optab_table[OTI_usmul_widen])
#define sdiv_optab (optab_table[OTI_sdiv])
#define smulv_optab (optab_table[OTI_smulv])
#define sdivv_optab (optab_table[OTI_sdivv])
#define sdivmod_optab (optab_table[OTI_sdivmod])
#define udiv_optab (optab_table[OTI_udiv])
#define udivmod_optab (optab_table[OTI_udivmod])
#define smod_optab (optab_table[OTI_smod])
#define umod_optab (optab_table[OTI_umod])
#define fmod_optab (optab_table[OTI_fmod])
#define drem_optab (optab_table[OTI_drem])
#define ftrunc_optab (optab_table[OTI_ftrunc])
#define and_optab (optab_table[OTI_and])
#define ior_optab (optab_table[OTI_ior])
#define xor_optab (optab_table[OTI_xor])
#define ashl_optab (optab_table[OTI_ashl])
#define lshr_optab (optab_table[OTI_lshr])
#define ashr_optab (optab_table[OTI_ashr])
#define rotl_optab (optab_table[OTI_rotl])
#define rotr_optab (optab_table[OTI_rotr])
#define smin_optab (optab_table[OTI_smin])
#define smax_optab (optab_table[OTI_smax])
#define umin_optab (optab_table[OTI_umin])
#define umax_optab (optab_table[OTI_umax])
#define pow_optab (optab_table[OTI_pow])
#define atan2_optab (optab_table[OTI_atan2])

#define mov_optab (optab_table[OTI_mov])
#define movstrict_optab (optab_table[OTI_movstrict])
#define movmisalign_optab (optab_table[OTI_movmisalign])

#define neg_optab (optab_table[OTI_neg])
#define negv_optab (optab_table[OTI_negv])
#define abs_optab (optab_table[OTI_abs])
#define absv_optab (optab_table[OTI_absv])
#define one_cmpl_optab (optab_table[OTI_one_cmpl])
#define ffs_optab (optab_table[OTI_ffs])
#define clz_optab (optab_table[OTI_clz])
#define ctz_optab (optab_table[OTI_ctz])
#define popcount_optab (optab_table[OTI_popcount])
#define parity_optab (optab_table[OTI_parity])
#define sqrt_optab (optab_table[OTI_sqrt])
#define sincos_optab (optab_table[OTI_sincos])
#define sin_optab (optab_table[OTI_sin])
#define asin_optab (optab_table[OTI_asin])
#define cos_optab (optab_table[OTI_cos])
#define acos_optab (optab_table[OTI_acos])
#define exp_optab (optab_table[OTI_exp])
#define exp10_optab (optab_table[OTI_exp10])
#define exp2_optab (optab_table[OTI_exp2])
#define expm1_optab (optab_table[OTI_expm1])
#define ldexp_optab (optab_table[OTI_ldexp])
#define logb_optab (optab_table[OTI_logb])
#define ilogb_optab (optab_table[OTI_ilogb])
#define log_optab (optab_table[OTI_log])
#define log10_optab (optab_table[OTI_log10])
#define log2_optab (optab_table[OTI_log2])
#define log1p_optab (optab_table[OTI_log1p])
#define floor_optab (optab_table[OTI_floor])
#define lfloor_optab (optab_table[OTI_lfloor])
#define ceil_optab (optab_table[OTI_ceil])
#define lceil_optab (optab_table[OTI_lceil])
#define btrunc_optab (optab_table[OTI_btrunc])
#define round_optab (optab_table[OTI_round])
#define nearbyint_optab (optab_table[OTI_nearbyint])
#define rint_optab (optab_table[OTI_rint])
#define lrint_optab (optab_table[OTI_lrint])
#define tan_optab (optab_table[OTI_tan])
#define atan_optab (optab_table[OTI_atan])
#define copysign_optab (optab_table[OTI_copysign])

#define cmp_optab (optab_table[OTI_cmp])
#define ucmp_optab (optab_table[OTI_ucmp])
#define tst_optab (optab_table[OTI_tst])

#define eq_optab (optab_table[OTI_eq])
#define ne_optab (optab_table[OTI_ne])
#define gt_optab (optab_table[OTI_gt])
#define ge_optab (optab_table[OTI_ge])
#define lt_optab (optab_table[OTI_lt])
#define le_optab (optab_table[OTI_le])
#define unord_optab (optab_table[OTI_unord])

#define strlen_optab (optab_table[OTI_strlen])

#define cbranch_optab (optab_table[OTI_cbranch])
#define cmov_optab (optab_table[OTI_cmov])
#define cstore_optab (optab_table[OTI_cstore])
#define push_optab (optab_table[OTI_push])
#define addcc_optab (optab_table[OTI_addcc])

#define reduc_smax_optab (optab_table[OTI_reduc_smax])
#define reduc_umax_optab (optab_table[OTI_reduc_umax])
#define reduc_smin_optab (optab_table[OTI_reduc_smin])
#define reduc_umin_optab (optab_table[OTI_reduc_umin])
#define reduc_splus_optab (optab_table[OTI_reduc_splus])
#define reduc_uplus_optab (optab_table[OTI_reduc_uplus])
                                                                                
#define ssum_widen_optab (optab_table[OTI_ssum_widen])
#define usum_widen_optab (optab_table[OTI_usum_widen])
#define sdot_prod_optab (optab_table[OTI_sdot_prod])
#define udot_prod_optab (optab_table[OTI_udot_prod])

#define vec_set_optab (optab_table[OTI_vec_set])
#define vec_extract_optab (optab_table[OTI_vec_extract])
#define vec_init_optab (optab_table[OTI_vec_init])
#define vec_shl_optab (optab_table[OTI_vec_shl])
#define vec_shr_optab (optab_table[OTI_vec_shr])
#define vec_realign_load_optab (optab_table[OTI_vec_realign_load])

#define powi_optab (optab_table[OTI_powi])

/* Conversion optabs have their own table and indexes.  */
enum convert_optab_index
{
  COI_sext,
  COI_zext,
  COI_trunc,

  COI_sfix,
  COI_ufix,

  COI_sfixtrunc,
  COI_ufixtrunc,

  COI_sfloat,
  COI_ufloat,

  COI_MAX
};

extern GTY(()) convert_optab convert_optab_table[COI_MAX];

#define sext_optab (convert_optab_table[COI_sext])
#define zext_optab (convert_optab_table[COI_zext])
#define trunc_optab (convert_optab_table[COI_trunc])
#define sfix_optab (convert_optab_table[COI_sfix])
#define ufix_optab (convert_optab_table[COI_ufix])
#define sfixtrunc_optab (convert_optab_table[COI_sfixtrunc])
#define ufixtrunc_optab (convert_optab_table[COI_ufixtrunc])
#define sfloat_optab (convert_optab_table[COI_sfloat])
#define ufloat_optab (convert_optab_table[COI_ufloat])

/* These arrays record the insn_code of insns that may be needed to
   perform input and output reloads of special objects.  They provide a
   place to pass a scratch register.  */
extern enum insn_code reload_in_optab[NUM_MACHINE_MODES];
extern enum insn_code reload_out_optab[NUM_MACHINE_MODES];

/* Contains the optab used for each rtx code.  */
extern GTY(()) optab code_to_optab[NUM_RTX_CODE + 1];


typedef rtx (*rtxfun) (rtx);

/* Indexed by the rtx-code for a conditional (e.g. EQ, LT,...)
   gives the gen_function to make a branch to test that condition.  */

extern rtxfun bcc_gen_fctn[NUM_RTX_CODE];

/* Indexed by the rtx-code for a conditional (e.g. EQ, LT,...)
   gives the insn code to make a store-condition insn
   to test that condition.  */

extern enum insn_code setcc_gen_code[NUM_RTX_CODE];

#ifdef HAVE_conditional_move
/* Indexed by the machine mode, gives the insn code to make a conditional
   move insn.  */

extern enum insn_code movcc_gen_code[NUM_MACHINE_MODES];
#endif

/* Indexed by the machine mode, gives the insn code for vector conditional
   operation.  */

extern enum insn_code vcond_gen_code[NUM_MACHINE_MODES];
extern enum insn_code vcondu_gen_code[NUM_MACHINE_MODES];

/* This array records the insn_code of insns to perform block moves.  */
extern enum insn_code movmem_optab[NUM_MACHINE_MODES];

/* This array records the insn_code of insns to perform block sets.  */
extern enum insn_code setmem_optab[NUM_MACHINE_MODES];

/* These arrays record the insn_code of two different kinds of insns
   to perform block compares.  */
extern enum insn_code cmpstr_optab[NUM_MACHINE_MODES];
extern enum insn_code cmpstrn_optab[NUM_MACHINE_MODES];
extern enum insn_code cmpmem_optab[NUM_MACHINE_MODES];

/* Synchronization primitives.  This first set is atomic operation for
   which we don't care about the resulting value.  */
extern enum insn_code sync_add_optab[NUM_MACHINE_MODES];
extern enum insn_code sync_sub_optab[NUM_MACHINE_MODES];
extern enum insn_code sync_ior_optab[NUM_MACHINE_MODES];
extern enum insn_code sync_and_optab[NUM_MACHINE_MODES];
extern enum insn_code sync_xor_optab[NUM_MACHINE_MODES];
extern enum insn_code sync_nand_optab[NUM_MACHINE_MODES];

/* This second set is atomic operations in which we return the value
   that existed in memory before the operation.  */
extern enum insn_code sync_old_add_optab[NUM_MACHINE_MODES];
extern enum insn_code sync_old_sub_optab[NUM_MACHINE_MODES];
extern enum insn_code sync_old_ior_optab[NUM_MACHINE_MODES];
extern enum insn_code sync_old_and_optab[NUM_MACHINE_MODES];
extern enum insn_code sync_old_xor_optab[NUM_MACHINE_MODES];
extern enum insn_code sync_old_nand_optab[NUM_MACHINE_MODES];

/* This third set is atomic operations in which we return the value
   that resulted after performing the operation.  */
extern enum insn_code sync_new_add_optab[NUM_MACHINE_MODES];
extern enum insn_code sync_new_sub_optab[NUM_MACHINE_MODES];
extern enum insn_code sync_new_ior_optab[NUM_MACHINE_MODES];
extern enum insn_code sync_new_and_optab[NUM_MACHINE_MODES];
extern enum insn_code sync_new_xor_optab[NUM_MACHINE_MODES];
extern enum insn_code sync_new_nand_optab[NUM_MACHINE_MODES];

/* Atomic compare and swap.  */
extern enum insn_code sync_compare_and_swap[NUM_MACHINE_MODES];
extern enum insn_code sync_compare_and_swap_cc[NUM_MACHINE_MODES];

/* Atomic exchange with acquire semantics.  */
extern enum insn_code sync_lock_test_and_set[NUM_MACHINE_MODES];

/* Atomic clear with release semantics.  */
extern enum insn_code sync_lock_release[NUM_MACHINE_MODES];

/* Define functions given in optabs.c.  */

extern rtx expand_widen_pattern_expr (tree exp, rtx op0, rtx op1, rtx wide_op,
                                      rtx target, int unsignedp);

extern rtx expand_ternary_op (enum machine_mode mode, optab ternary_optab,
			      rtx op0, rtx op1, rtx op2, rtx target,
			      int unsignedp);

/* Expand a binary operation given optab and rtx operands.  */
extern rtx expand_binop (enum machine_mode, optab, rtx, rtx, rtx, int,
			 enum optab_methods);

extern bool force_expand_binop (enum machine_mode, optab, rtx, rtx, rtx, int,
				enum optab_methods);

/* Expand a binary operation with both signed and unsigned forms.  */
extern rtx sign_expand_binop (enum machine_mode, optab, optab, rtx, rtx,
			      rtx, int, enum optab_methods);

/* Generate code to perform an operation on one operand with two results.  */
extern int expand_twoval_unop (optab, rtx, rtx, rtx, int);

/* Generate code to perform an operation on two operands with two results.  */
extern int expand_twoval_binop (optab, rtx, rtx, rtx, rtx, int);

/* Generate code to perform an operation on two operands with two
   results, using a library function.  */
extern bool expand_twoval_binop_libfunc (optab, rtx, rtx, rtx, rtx,
					 enum rtx_code);

/* Expand a unary arithmetic operation given optab rtx operand.  */
extern rtx expand_unop (enum machine_mode, optab, rtx, rtx, int);

/* Expand the absolute value operation.  */
extern rtx expand_abs_nojump (enum machine_mode, rtx, rtx, int);
extern rtx expand_abs (enum machine_mode, rtx, rtx, int, int);

/* Expand the copysign operation.  */
extern rtx expand_copysign (rtx, rtx, rtx);

/* Generate an instruction with a given INSN_CODE with an output and
   an input.  */
extern void emit_unop_insn (int, rtx, rtx, enum rtx_code);

/* Emit code to perform a series of operations on a multi-word quantity, one
   word at a time.  */
extern rtx emit_no_conflict_block (rtx, rtx, rtx, rtx, rtx);

/* Emit one rtl insn to compare two rtx's.  */
extern void emit_cmp_insn (rtx, rtx, enum rtx_code, rtx, enum machine_mode,
			   int);

/* The various uses that a comparison can have; used by can_compare_p:
   jumps, conditional moves, store flag operations.  */
enum can_compare_purpose
{
  ccp_jump,
  ccp_cmov,
  ccp_store_flag
};

/* Return the optab used for computing the given operation on the type
   given by the second argument.  */
extern optab optab_for_tree_code (enum tree_code, tree);

/* Nonzero if a compare of mode MODE can be done straightforwardly
   (without splitting it into pieces).  */
extern int can_compare_p (enum rtx_code, enum machine_mode,
			  enum can_compare_purpose);

/* Return the INSN_CODE to use for an extend operation.  */
extern enum insn_code can_extend_p (enum machine_mode, enum machine_mode, int);

/* Generate the body of an insn to extend Y (with mode MFROM)
   into X (with mode MTO).  Do zero-extension if UNSIGNEDP is nonzero.  */
extern rtx gen_extend_insn (rtx, rtx, enum machine_mode,
			    enum machine_mode, int);

/* Call this to reset the function entry for one optab.  */
extern void set_optab_libfunc (optab, enum machine_mode, const char *);
extern void set_conv_libfunc (convert_optab, enum machine_mode,
			      enum machine_mode, const char *);

/* Generate code for a FLOAT_EXPR.  */
extern void expand_float (rtx, rtx, int);

/* Generate code for a FIX_EXPR.  */
extern void expand_fix (rtx, rtx, int);

/* Return tree if target supports vector operations for COND_EXPR.  */
bool expand_vec_cond_expr_p (tree, enum machine_mode);

/* Generate code for VEC_COND_EXPR.  */
extern rtx expand_vec_cond_expr (tree, rtx);

/* Generate code for VEC_LSHIFT_EXPR and VEC_RSHIFT_EXPR.  */
extern rtx expand_vec_shift_expr (tree, rtx);

#endif /* GCC_OPTABS_H */
