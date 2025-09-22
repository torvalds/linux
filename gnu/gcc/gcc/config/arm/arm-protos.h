/* Prototypes for exported functions defined in arm.c and pe.c
   Copyright (C) 1999, 2000, 2001, 2002, 2003, 2004, 2005
   Free Software Foundation, Inc.
   Contributed by Richard Earnshaw (rearnsha@arm.com)
   Minor hacks by Nick Clifton (nickc@cygnus.com)

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

#ifndef GCC_ARM_PROTOS_H
#define GCC_ARM_PROTOS_H

extern void arm_override_options (void);
extern int use_return_insn (int, rtx);
extern int arm_regno_class (int);
extern void arm_load_pic_register (unsigned long);
extern int arm_volatile_func (void);
extern const char *arm_output_epilogue (rtx);
extern void arm_expand_prologue (void);
extern const char *arm_strip_name_encoding (const char *);
extern void arm_asm_output_labelref (FILE *, const char *);
extern unsigned long arm_current_func_type (void);
extern HOST_WIDE_INT arm_compute_initial_elimination_offset (unsigned int,
							     unsigned int);
extern HOST_WIDE_INT thumb_compute_initial_elimination_offset (unsigned int,
							       unsigned int);
extern unsigned int arm_dbx_register_number (unsigned int);
extern void arm_output_fn_unwind (FILE *, bool);
  

#ifdef TREE_CODE
extern int arm_return_in_memory (tree);
extern void arm_encode_call_attribute (tree, int);
#endif
#ifdef RTX_CODE
extern bool arm_vector_mode_supported_p (enum machine_mode);
extern int arm_hard_regno_mode_ok (unsigned int, enum machine_mode);
extern int const_ok_for_arm (HOST_WIDE_INT);
extern int arm_split_constant (RTX_CODE, enum machine_mode, rtx,
			       HOST_WIDE_INT, rtx, rtx, int);
extern RTX_CODE arm_canonicalize_comparison (RTX_CODE, enum machine_mode,
					     rtx *);
extern int legitimate_pic_operand_p (rtx);
extern rtx legitimize_pic_address (rtx, enum machine_mode, rtx);
extern rtx legitimize_tls_address (rtx, rtx);
extern int arm_legitimate_address_p  (enum machine_mode, rtx, RTX_CODE, int);
extern int thumb_legitimate_address_p (enum machine_mode, rtx, int);
extern int thumb_legitimate_offset_p (enum machine_mode, HOST_WIDE_INT);
extern rtx arm_legitimize_address (rtx, rtx, enum machine_mode);
extern rtx thumb_legitimize_address (rtx, rtx, enum machine_mode);
extern rtx thumb_legitimize_reload_address (rtx *, enum machine_mode, int, int,
					    int);
extern int arm_const_double_rtx (rtx);
extern int neg_const_double_rtx_ok_for_fpa (rtx);
extern enum reg_class coproc_secondary_reload_class (enum machine_mode, rtx,
						     bool);
extern bool arm_tls_referenced_p (rtx);

extern int cirrus_memory_offset (rtx);
extern int arm_coproc_mem_operand (rtx, bool);
extern int arm_no_early_store_addr_dep (rtx, rtx);
extern int arm_no_early_alu_shift_dep (rtx, rtx);
extern int arm_no_early_alu_shift_value_dep (rtx, rtx);
extern int arm_no_early_mul_dep (rtx, rtx);

extern int tls_mentioned_p (rtx);
extern int symbol_mentioned_p (rtx);
extern int label_mentioned_p (rtx);
extern RTX_CODE minmax_code (rtx);
extern int adjacent_mem_locations (rtx, rtx);
extern int load_multiple_sequence (rtx *, int, int *, int *, HOST_WIDE_INT *);
extern const char *emit_ldm_seq (rtx *, int);
extern int store_multiple_sequence (rtx *, int, int *, int *, HOST_WIDE_INT *);
extern const char * emit_stm_seq (rtx *, int);
extern rtx arm_gen_load_multiple (int, int, rtx, int, int,
				  rtx, HOST_WIDE_INT *);
extern rtx arm_gen_store_multiple (int, int, rtx, int, int,
				   rtx, HOST_WIDE_INT *);
extern int arm_gen_movmemqi (rtx *);
extern enum machine_mode arm_select_cc_mode (RTX_CODE, rtx, rtx);
extern enum machine_mode arm_select_dominance_cc_mode (rtx, rtx,
						       HOST_WIDE_INT);
extern rtx arm_gen_compare_reg (RTX_CODE, rtx, rtx);
extern rtx arm_gen_return_addr_mask (void);
extern void arm_reload_in_hi (rtx *);
extern void arm_reload_out_hi (rtx *);
extern int arm_const_double_inline_cost (rtx);
extern bool arm_const_double_by_parts (rtx);
extern const char *fp_immediate_constant (rtx);
extern const char *output_call (rtx *);
extern const char *output_call_mem (rtx *);
extern const char *output_mov_long_double_fpa_from_arm (rtx *);
extern const char *output_mov_long_double_arm_from_fpa (rtx *);
extern const char *output_mov_long_double_arm_from_arm (rtx *);
extern const char *output_mov_double_fpa_from_arm (rtx *);
extern const char *output_mov_double_arm_from_fpa (rtx *);
extern const char *output_move_double (rtx *);
extern const char *output_add_immediate (rtx *);
extern const char *arithmetic_instr (rtx, int);
extern void output_ascii_pseudo_op (FILE *, const unsigned char *, int);
extern const char *output_return_instruction (rtx, int, int);
extern void arm_poke_function_name (FILE *, const char *);
extern void arm_print_operand (FILE *, rtx, int);
extern void arm_print_operand_address (FILE *, rtx);
extern void arm_final_prescan_insn (rtx);
extern int arm_go_if_legitimate_address (enum machine_mode, rtx);
extern int arm_debugger_arg_offset (int, rtx);
extern int arm_is_longcall_p (rtx, int, int);
extern int    arm_emit_vector_const (FILE *, rtx);
extern const char * arm_output_load_gr (rtx *);
extern const char *vfp_output_fstmx (rtx *);
extern void arm_set_return_address (rtx, rtx);
extern int arm_eliminable_register (rtx);

extern bool arm_output_addr_const_extra (FILE *, rtx);

#if defined TREE_CODE
extern rtx arm_function_arg (CUMULATIVE_ARGS *, enum machine_mode, tree, int);
extern void arm_init_cumulative_args (CUMULATIVE_ARGS *, tree, rtx, tree);
extern bool arm_pad_arg_upward (enum machine_mode, tree);
extern bool arm_pad_reg_upward (enum machine_mode, tree, int);
extern bool arm_needs_doubleword_align (enum machine_mode, tree);
extern rtx arm_function_value(tree, tree);
#endif
extern int arm_apply_result_size (void);

#if defined AOF_ASSEMBLER
extern rtx aof_pic_entry (rtx);
extern void aof_add_import (const char *);
extern void aof_delete_import (const char *);
extern void zero_init_section (void);
#endif /* AOF_ASSEMBLER */

#endif /* RTX_CODE */

extern int arm_float_words_big_endian (void);

/* Thumb functions.  */
extern void arm_init_expanders (void);
extern const char *thumb_unexpanded_epilogue (void);
extern void thumb_expand_prologue (void);
extern void thumb_expand_epilogue (void);
#ifdef TREE_CODE
extern int is_called_in_ARM_mode (tree);
#endif
extern int thumb_shiftable_const (unsigned HOST_WIDE_INT);
#ifdef RTX_CODE
extern void thumb_final_prescan_insn (rtx);
extern const char *thumb_load_double_from_address (rtx *);
extern const char *thumb_output_move_mem_multiple (int, rtx *);
extern const char *thumb_call_via_reg (rtx);
extern void thumb_expand_movmemqi (rtx *);
extern int thumb_go_if_legitimate_address (enum machine_mode, rtx);
extern rtx arm_return_addr (int, rtx);
extern void thumb_reload_out_hi (rtx *);
extern void thumb_reload_in_hi (rtx *);
extern void thumb_set_return_address (rtx, rtx);
#endif

/* Defined in pe.c.  */
extern int arm_dllexport_name_p (const char *);
extern int arm_dllimport_name_p (const char *);

#ifdef TREE_CODE
extern void arm_pe_unique_section (tree, int);
extern void arm_pe_encode_section_info (tree, rtx, int);
extern int arm_dllexport_p (tree);
extern int arm_dllimport_p (tree);
extern void arm_mark_dllexport (tree);
extern void arm_mark_dllimport (tree);
#endif

extern void arm_pr_long_calls (struct cpp_reader *);
extern void arm_pr_no_long_calls (struct cpp_reader *);
extern void arm_pr_long_calls_off (struct cpp_reader *);

#endif /* ! GCC_ARM_PROTOS_H */
