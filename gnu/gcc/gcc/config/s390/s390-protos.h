/* Definitions of target machine for GNU compiler, for IBM S/390.
   Copyright (C) 2000, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Hartmut Penner (hpenner@de.ibm.com)

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */



/* Prototypes of functions used for constraint evaluation in 
   constraints.c.  */

extern int s390_mem_constraint (const char *str, rtx op);
extern int s390_O_constraint_str (const char c, HOST_WIDE_INT value);
extern int s390_N_constraint_str (const char *str, HOST_WIDE_INT value);
extern int s390_float_const_zero_p (rtx value);



/* Declare functions in s390.c.  */

extern void optimization_options (int, int);
extern void override_options (void);
extern bool s390_can_eliminate (int, int);
extern HOST_WIDE_INT s390_initial_elimination_offset (int, int);
extern void s390_emit_prologue (void);
extern void s390_emit_epilogue (bool);
extern void s390_function_profiler (FILE *, int);
extern void s390_conditional_register_usage (void);
extern void s390_set_has_landing_pad_p (bool);
extern bool s390_hard_regno_mode_ok (unsigned int, enum machine_mode);
extern bool s390_hard_regno_rename_ok (unsigned int, unsigned int);
extern bool s390_class_max_nregs (enum reg_class, enum machine_mode);

#ifdef RTX_CODE
extern int s390_extra_constraint_str (rtx, int, const char *);
extern int s390_const_ok_for_constraint_p (HOST_WIDE_INT, int, const char *);
extern int s390_const_double_ok_for_constraint_p (rtx, int, const char *);
extern int s390_single_part (rtx, enum machine_mode, enum machine_mode, int);
extern unsigned HOST_WIDE_INT s390_extract_part (rtx, enum machine_mode, int);
extern bool s390_split_ok_p (rtx, rtx, enum machine_mode, int);
extern bool s390_overlap_p (rtx, rtx, HOST_WIDE_INT);
extern bool s390_offset_p (rtx, rtx, rtx);
extern int tls_symbolic_operand (rtx);

extern bool s390_match_ccmode (rtx, enum machine_mode);
extern enum machine_mode s390_tm_ccmode (rtx, rtx, bool);
extern enum machine_mode s390_select_ccmode (enum rtx_code, rtx, rtx);
extern void s390_canonicalize_comparison (enum rtx_code *, rtx *, rtx *);
extern rtx s390_emit_compare (enum rtx_code, rtx, rtx);
extern void s390_emit_jump (rtx, rtx);
extern bool symbolic_reference_mentioned_p (rtx);
extern bool tls_symbolic_reference_mentioned_p (rtx);
extern bool legitimate_la_operand_p (rtx);
extern bool preferred_la_operand_p (rtx, rtx);
extern int legitimate_pic_operand_p (rtx);
extern int legitimate_constant_p (rtx);
extern bool legitimate_reload_constant_p (rtx);
extern bool legitimate_address_p (enum machine_mode, rtx, int);
extern rtx legitimize_pic_address (rtx, rtx);
extern rtx legitimize_address (rtx, rtx, enum machine_mode);
extern rtx legitimize_reload_address (rtx, enum machine_mode, int, int);
extern enum reg_class s390_preferred_reload_class (rtx, enum reg_class);
extern enum reg_class s390_secondary_input_reload_class (enum reg_class,
							 enum machine_mode,
							 rtx);
extern enum reg_class s390_secondary_output_reload_class (enum reg_class,
							  enum machine_mode,
							  rtx);
extern void s390_expand_plus_operand (rtx, rtx, rtx);
extern void emit_symbolic_move (rtx *);
extern void s390_load_address (rtx, rtx);
extern void s390_expand_movmem (rtx, rtx, rtx);
extern void s390_expand_setmem (rtx, rtx, rtx);
extern void s390_expand_cmpmem (rtx, rtx, rtx, rtx);
extern bool s390_expand_addcc (enum rtx_code, rtx, rtx, rtx, rtx, rtx);
extern bool s390_expand_insv (rtx, rtx, rtx, rtx);
extern void s390_expand_cs_hqi (enum machine_mode, rtx, rtx, rtx, rtx);
extern void s390_expand_atomic (enum machine_mode, enum rtx_code, 
				rtx, rtx, rtx, bool);
extern rtx s390_return_addr_rtx (int, rtx);
extern rtx s390_back_chain_rtx (void);
extern rtx s390_emit_call (rtx, rtx, rtx, rtx);
extern void s390_expand_logical_operator (enum rtx_code,
					  enum machine_mode, rtx *);
extern bool s390_logical_operator_ok_p (rtx *);
extern void s390_narrow_logical_operator (enum rtx_code, rtx *, rtx *);
extern void s390_split_access_reg (rtx, rtx *, rtx *);

extern bool s390_output_addr_const_extra (FILE*, rtx);
extern void print_operand_address (FILE *, rtx);
extern void print_operand (FILE *, rtx, int);
extern void s390_output_pool_entry (rtx, enum machine_mode, unsigned int);
extern void s390_trampoline_template (FILE *);
extern void s390_initialize_trampoline (rtx, rtx, rtx);
extern rtx s390_gen_rtx_const_DI (int, int);
extern int s390_agen_dep_p (rtx, rtx);
extern rtx s390_load_got (void);
extern rtx s390_get_thread_pointer (void);
extern void s390_emit_tpf_eh_return (rtx);
extern bool s390_legitimate_address_without_index_p (rtx);
extern bool s390_decompose_shift_count (rtx, rtx *, HOST_WIDE_INT *);
extern int s390_branch_condition_mask (rtx);

#endif /* RTX_CODE */

#ifdef TREE_CODE
extern void s390_function_arg_advance (CUMULATIVE_ARGS *, enum machine_mode,
				       tree, int);
#ifdef RTX_CODE
extern rtx s390_function_arg (CUMULATIVE_ARGS *, enum machine_mode, tree, int);
extern rtx s390_function_value (tree, enum machine_mode);
extern void s390_va_start (tree, rtx);
#endif /* RTX_CODE */
#endif /* TREE_CODE */
