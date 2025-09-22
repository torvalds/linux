/* Prototypes for pa.c functions used in the md file & elsewhere.
   Copyright (C) 2000, 2001, 2002, 2003, 2004, 2005 Free Software Foundation,
   Inc.

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

#ifdef RTX_CODE
/* Prototype function used in various macros.  */
extern int symbolic_operand (rtx, enum machine_mode);
extern int tls_symbolic_operand (rtx);

/* Used in insn-*.c.  */
extern int following_call (rtx);
extern int function_label_operand (rtx, enum machine_mode);
extern int lhs_lshift_cint_operand (rtx, enum machine_mode);

#ifdef TREE_CODE
extern void hppa_va_start (tree, rtx);
#endif /* TREE_CODE */
extern rtx hppa_legitimize_address (rtx, rtx, enum machine_mode);

/* Define functions in pa.c and used in insn-output.c.  */

extern const char *output_and (rtx *);
extern const char *output_ior (rtx *);
extern const char *output_move_double (rtx *);
extern const char *output_fp_move_double (rtx *);
extern const char *output_block_move (rtx *, int);
extern const char *output_block_clear (rtx *, int);
extern const char *output_cbranch (rtx *, int, rtx);
extern const char *output_lbranch (rtx, rtx, int);
extern const char *output_bb (rtx *, int, rtx, int);
extern const char *output_bvb (rtx *, int, rtx, int);
extern const char *output_dbra (rtx *, rtx, int);
extern const char *output_movb (rtx *, rtx, int, int);
extern const char *output_parallel_movb (rtx *, rtx);
extern const char *output_parallel_addb (rtx *, rtx);
extern const char *output_call (rtx, rtx, int);
extern const char *output_indirect_call (rtx, rtx);
extern const char *output_millicode_call (rtx, rtx);
extern const char *output_mul_insn (int, rtx);
extern const char *output_div_insn (rtx *, int, rtx);
extern const char *output_mod_insn (int, rtx);
extern const char *singlemove_string (rtx *);
extern void output_arg_descriptor (rtx);
extern void output_global_address (FILE *, rtx, int);
extern void print_operand (FILE *, rtx, int);
extern rtx legitimize_pic_address (rtx, enum machine_mode, rtx);
extern struct rtx_def *gen_cmp_fp (enum rtx_code, rtx, rtx);
extern void hppa_encode_label (rtx);
extern int arith11_operand (rtx, enum machine_mode);
extern int adddi3_operand (rtx, enum machine_mode);
extern int indexed_memory_operand (rtx, enum machine_mode);
extern int symbolic_expression_p (rtx);
extern int symbolic_memory_operand (rtx, enum machine_mode);
extern bool pa_tls_referenced_p (rtx);
extern int pa_adjust_insn_length (rtx, int);
extern int int11_operand (rtx, enum machine_mode);
extern int reg_or_cint_move_operand (rtx, enum machine_mode);
extern int arith5_operand (rtx, enum machine_mode);
extern int uint5_operand (rtx, enum machine_mode);
extern int pic_label_operand (rtx, enum machine_mode);
extern int plus_xor_ior_operator (rtx, enum machine_mode);
extern int borx_reg_operand (rtx, enum machine_mode);
extern int shadd_operand (rtx, enum machine_mode);
extern int arith_operand (rtx, enum machine_mode);
extern int read_only_operand (rtx, enum machine_mode);
extern int move_dest_operand (rtx, enum machine_mode);
extern int move_src_operand (rtx, enum machine_mode);
extern int prefetch_cc_operand (rtx, enum machine_mode);
extern int prefetch_nocc_operand (rtx, enum machine_mode);
extern int and_operand (rtx, enum machine_mode);
extern int ior_operand (rtx, enum machine_mode);
extern int arith32_operand (rtx, enum machine_mode);
extern int uint32_operand (rtx, enum machine_mode);
extern int reg_before_reload_operand (rtx, enum machine_mode);
extern int reg_or_0_operand (rtx, enum machine_mode);
extern int reg_or_0_or_nonsymb_mem_operand (rtx, enum machine_mode);
extern int pre_cint_operand (rtx, enum machine_mode);
extern int post_cint_operand (rtx, enum machine_mode);
extern int div_operand (rtx, enum machine_mode);
extern int int5_operand (rtx, enum machine_mode);
extern int movb_comparison_operator (rtx, enum machine_mode);
extern int ireg_or_int5_operand (rtx, enum machine_mode);
extern int fmpyaddoperands (rtx *);
extern int fmpysuboperands (rtx *);
extern int call_operand_address (rtx, enum machine_mode);
extern int ior_operand (rtx, enum machine_mode);
extern void emit_bcond_fp (enum rtx_code, rtx);
extern int emit_move_sequence (rtx *, enum machine_mode, rtx);
extern int emit_hpdiv_const (rtx *, int);
extern int is_function_label_plus_const (rtx);
extern int jump_in_call_delay (rtx);
extern int hppa_fpstore_bypass_p (rtx, rtx);
extern int attr_length_millicode_call (rtx);
extern int attr_length_call (rtx, int);
extern int attr_length_indirect_call (rtx);
extern int attr_length_save_restore_dltp (rtx);

/* Declare functions defined in pa.c and used in templates.  */

extern struct rtx_def *return_addr_rtx (int, rtx);

extern int fp_reg_operand (rtx, enum machine_mode);
extern int arith_double_operand (rtx, enum machine_mode);
extern int ireg_operand (rtx, enum machine_mode);
extern int lhs_lshift_operand (rtx, enum machine_mode);
extern int pc_or_label_operand (rtx, enum machine_mode);
#ifdef ARGS_SIZE_RTX
/* expr.h defines ARGS_SIZE_RTX and `enum direction' */
#ifdef TREE_CODE
extern enum direction function_arg_padding (enum machine_mode, tree);
#endif
#endif /* ARGS_SIZE_RTX */
extern int non_hard_reg_operand (rtx, enum machine_mode);
extern int eq_neq_comparison_operator (rtx, enum machine_mode);
extern int insn_refs_are_delayed (rtx);
extern rtx get_deferred_plabel (rtx);
#endif /* RTX_CODE */

/* Prototype function used in macro CONST_OK_FOR_LETTER_P.  */
extern int zdepi_cint_p (unsigned HOST_WIDE_INT);

extern void override_options (void);
extern void output_ascii (FILE *, const char *, int);
extern HOST_WIDE_INT compute_frame_size (HOST_WIDE_INT, int *);
extern int and_mask_p (unsigned HOST_WIDE_INT);
extern int cint_ok_for_move (HOST_WIDE_INT);
extern void hppa_expand_prologue (void);
extern void hppa_expand_epilogue (void);
extern int hppa_can_use_return_insn_p (void);
extern int ior_mask_p (unsigned HOST_WIDE_INT);
extern void compute_zdepdi_operands (unsigned HOST_WIDE_INT,
				     unsigned *);
#ifdef RTX_CODE
extern const char * output_64bit_and (rtx *);
extern const char * output_64bit_ior (rtx *);
extern int cmpib_comparison_operator (rtx, enum machine_mode);
#endif


/* Miscellaneous functions in pa.c.  */
#ifdef TREE_CODE
extern int reloc_needed (tree);
#ifdef RTX_CODE
extern rtx function_arg (CUMULATIVE_ARGS *, enum machine_mode,
			 tree, int);
extern rtx function_value (tree, tree);
#endif
extern bool pa_return_in_memory (tree, tree);
#endif /* TREE_CODE */

extern void pa_asm_output_aligned_bss (FILE *, const char *,
				       unsigned HOST_WIDE_INT,
				       unsigned int);
extern void pa_asm_output_aligned_common (FILE *, const char *,
					  unsigned HOST_WIDE_INT,
					  unsigned int);
extern void pa_asm_output_aligned_local (FILE *, const char *,
					 unsigned HOST_WIDE_INT,
					 unsigned int);
extern void pa_hpux_asm_output_external (FILE *, tree, const char *);

extern const int magic_milli[];
extern int shadd_constant_p (int);
