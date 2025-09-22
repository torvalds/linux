/* Prototypes for alpha.c functions used in the md file & elsewhere.
   Copyright (C) 1999, 2000, 2001, 2002, 2003, 2004, 2005
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

extern int alpha_next_sequence_number;

extern void literal_section (void);
extern void override_options (void);
extern int zap_mask (HOST_WIDE_INT);
extern int direct_return (void);

extern int alpha_sa_size (void);
extern HOST_WIDE_INT alpha_initial_elimination_offset (unsigned int,
						       unsigned int);
extern int alpha_pv_save_size (void);
extern int alpha_using_fp (void);
extern void alpha_expand_prologue (void);
extern void alpha_expand_epilogue (void);
extern void alpha_output_filename (FILE *, const char *);

extern bool alpha_const_ok_for_letter_p (HOST_WIDE_INT, int);
extern bool alpha_const_double_ok_for_letter_p (rtx, int);
extern bool alpha_extra_constraint (rtx, int);

extern rtx alpha_tablejump_addr_vec (rtx);
extern rtx alpha_tablejump_best_label (rtx);

extern bool alpha_legitimate_constant_p (rtx);
extern bool alpha_legitimate_address_p (enum machine_mode, rtx, int);
extern rtx alpha_legitimize_address (rtx, rtx, enum machine_mode);
extern rtx alpha_legitimize_reload_address (rtx, enum machine_mode,
					    int, int, int);

extern rtx split_small_symbolic_operand (rtx);

extern void get_aligned_mem (rtx, rtx *, rtx *);
extern rtx get_unaligned_address (rtx);
extern rtx get_unaligned_offset (rtx, HOST_WIDE_INT);
extern enum reg_class alpha_preferred_reload_class (rtx, enum reg_class);
extern enum reg_class alpha_secondary_reload_class (enum reg_class,
						    enum machine_mode, rtx,
						    int);

extern void alpha_set_memflags (rtx, rtx);
extern bool alpha_split_const_mov (enum machine_mode, rtx *);
extern bool alpha_expand_mov (enum machine_mode, rtx *);
extern bool alpha_expand_mov_nobwx (enum machine_mode, rtx *);
extern void alpha_expand_movmisalign (enum machine_mode, rtx *);
extern void alpha_emit_floatuns (rtx[]);
extern rtx alpha_emit_conditional_move (rtx, enum machine_mode);
extern void alpha_split_tmode_pair (rtx[], enum machine_mode, bool);
extern void alpha_split_tfmode_frobsign (rtx[], rtx (*)(rtx, rtx, rtx));
extern void alpha_expand_unaligned_load (rtx, rtx, HOST_WIDE_INT,
					 HOST_WIDE_INT, int);
extern void alpha_expand_unaligned_store (rtx, rtx, HOST_WIDE_INT,
					  HOST_WIDE_INT);
extern int alpha_expand_block_move (rtx []);
extern int alpha_expand_block_clear (rtx []);
extern rtx alpha_expand_zap_mask (HOST_WIDE_INT);
extern void alpha_expand_builtin_vector_binop (rtx (*)(rtx, rtx, rtx),
					       enum machine_mode,
					       rtx, rtx, rtx);
extern rtx alpha_return_addr (int, rtx);
extern rtx alpha_gp_save_rtx (void);
extern void print_operand (FILE *, rtx, int);
extern void print_operand_address (FILE *, rtx);
extern void alpha_initialize_trampoline (rtx, rtx, rtx, int, int, int);

extern void alpha_va_start (tree, rtx);
extern rtx alpha_va_arg (tree, tree);
extern rtx function_arg (CUMULATIVE_ARGS, enum machine_mode, tree, int);
extern rtx function_value (tree, tree, enum machine_mode);

extern void alpha_start_function (FILE *, const char *, tree);
extern void alpha_end_function (FILE *, const char *, tree);

extern int alpha_find_lo_sum_using_gp (rtx);

#ifdef REAL_VALUE_TYPE
extern int check_float_value (enum machine_mode, REAL_VALUE_TYPE *, int);
#endif

#ifdef RTX_CODE
extern rtx alpha_emit_conditional_branch (enum rtx_code);
extern rtx alpha_emit_setcc (enum rtx_code);
extern int alpha_split_conditional_move (enum rtx_code, rtx, rtx, rtx, rtx);
extern void alpha_emit_xfloating_arith (enum rtx_code, rtx[]);
extern void alpha_emit_xfloating_cvt (enum rtx_code, rtx[]);
extern void alpha_split_atomic_op (enum rtx_code, rtx, rtx, rtx, rtx, rtx);
extern void alpha_split_compare_and_swap (rtx, rtx, rtx, rtx, rtx);
extern void alpha_expand_compare_and_swap_12 (rtx, rtx, rtx, rtx);
extern void alpha_split_compare_and_swap_12 (enum machine_mode, rtx, rtx,
					     rtx, rtx, rtx, rtx, rtx);
extern void alpha_split_lock_test_and_set (rtx, rtx, rtx, rtx);
extern void alpha_expand_lock_test_and_set_12 (rtx, rtx, rtx);
extern void alpha_split_lock_test_and_set_12 (enum machine_mode, rtx, rtx,
					      rtx, rtx, rtx);
#endif

extern rtx alpha_need_linkage (const char *, int);
extern rtx alpha_use_linkage (rtx, tree, int, int);

#if TARGET_ABI_OPEN_VMS
extern enum avms_arg_type alpha_arg_type (enum machine_mode);
extern rtx alpha_arg_info_reg_val (CUMULATIVE_ARGS);
#endif

extern rtx unicosmk_add_call_info_word (rtx);

#if TARGET_ABI_UNICOSMK
extern void unicosmk_defer_case_vector (rtx, rtx);
extern void unicosmk_add_extern (const char *);
extern void unicosmk_output_align (FILE *, int);
extern void unicosmk_output_common (FILE *, const char *, int, int);
extern int unicosmk_initial_elimination_offset (int, int);
#endif

extern int some_small_symbolic_operand_int (rtx *, void *);
extern int tls_symbolic_operand_1 (rtx, int, int);
extern rtx resolve_reload_operand (rtx);
