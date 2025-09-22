/* Prototypes for exported functions defined in avr.c
   
   Copyright (C) 2000, 2001, 2002, 2003, 2004, 2006
   Free Software Foundation, Inc.
   Contributed by Denis Chertykov (denisc@overta.ru)

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


extern int function_arg_regno_p (int r);
extern void avr_init_once (void);
extern void avr_override_options (void);
extern void avr_optimization_options (int level, int size);
extern char *avr_change_section (char *sect_name);
extern int avr_ret_register (void);
extern enum reg_class class_likely_spilled_p (int c);
extern enum reg_class avr_regno_reg_class (int r);
extern enum reg_class avr_reg_class_from_letter (int c);
extern int frame_pointer_required_p (void);
extern void asm_globalize_label (FILE *file, const char *name);
extern void order_regs_for_local_alloc (void);
extern int initial_elimination_offset (int from, int to);
extern int avr_simple_epilogue (void);
extern int mask_one_bit_p (HOST_WIDE_INT mask);
extern void gas_output_limited_string (FILE *file, const char *str);
extern void gas_output_ascii (FILE *file, const char *str, size_t length);

#ifdef TREE_CODE
extern void asm_output_external (FILE *file, tree decl, char *name);
extern int avr_progmem_p (tree decl, tree attributes);

#ifdef RTX_CODE /* inside TREE_CODE */
extern rtx avr_function_value (tree type, tree func);
extern void init_cumulative_args (CUMULATIVE_ARGS *cum, tree fntype,
				  rtx libname, tree fndecl);
extern rtx function_arg (CUMULATIVE_ARGS *cum, enum machine_mode mode,
			 tree type, int named);
#endif /* RTX_CODE inside TREE_CODE */

#ifdef HAVE_MACHINE_MODES /* inside TREE_CODE */
extern void function_arg_advance (CUMULATIVE_ARGS *cum,
				  enum machine_mode mode, tree type,
				  int named);
#endif /* HAVE_MACHINE_MODES inside TREE_CODE*/
#endif /* TREE_CODE */

#ifdef RTX_CODE
extern void asm_output_external_libcall (FILE *file, rtx symref);
extern int legitimate_address_p (enum machine_mode mode, rtx x,	int strict);
extern int compare_diff_p (rtx insn);
extern const char *output_movqi (rtx insn, rtx operands[], int *l);
extern const char *output_movhi (rtx insn, rtx operands[], int *l);
extern const char *out_movqi_r_mr (rtx insn, rtx op[], int *l);
extern const char *out_movqi_mr_r (rtx insn, rtx op[], int *l);
extern const char *out_movhi_r_mr (rtx insn, rtx op[], int *l);
extern const char *out_movhi_mr_r (rtx insn, rtx op[], int *l);
extern const char *out_movsi_r_mr (rtx insn, rtx op[], int *l);
extern const char *out_movsi_mr_r (rtx insn, rtx op[], int *l);
extern const char *output_movsisf (rtx insn, rtx operands[], int *l);
extern const char *out_tstsi (rtx insn, int *l);
extern const char *out_tsthi (rtx insn, int *l);
extern const char *ret_cond_branch (rtx x, int len, int reverse);

extern const char *ashlqi3_out (rtx insn, rtx operands[], int *len);
extern const char *ashlhi3_out (rtx insn, rtx operands[], int *len);
extern const char *ashlsi3_out (rtx insn, rtx operands[], int *len);

extern const char *ashrqi3_out (rtx insn, rtx operands[], int *len);
extern const char *ashrhi3_out (rtx insn, rtx operands[], int *len);
extern const char *ashrsi3_out (rtx insn, rtx operands[], int *len);

extern const char *lshrqi3_out (rtx insn, rtx operands[], int *len);
extern const char *lshrhi3_out (rtx insn, rtx operands[], int *len);
extern const char *lshrsi3_out (rtx insn, rtx operands[], int *len);

extern void avr_output_bld (rtx operands[], int bit_nr);
extern void avr_output_addr_vec_elt (FILE *stream, int value);
extern const char *avr_out_sbxx_branch (rtx insn, rtx operands[]);

extern enum reg_class preferred_reload_class (rtx x, enum reg_class class);
extern int extra_constraint_Q (rtx x);
extern rtx legitimize_address (rtx x, rtx oldx, enum machine_mode mode);
extern int adjust_insn_length (rtx insn, int len);
extern rtx avr_libcall_value (enum machine_mode mode);
extern const char *output_reload_inhi (rtx insn, rtx *operands, int *len);
extern const char *output_reload_insisf (rtx insn, rtx *operands, int *len);
extern enum reg_class secondary_input_reload_class (enum reg_class,
						    enum machine_mode,
						    rtx);
extern void notice_update_cc (rtx body, rtx insn);
extern void print_operand (FILE *file, rtx x, int code);
extern void print_operand_address (FILE *file, rtx addr);
extern int reg_unused_after (rtx insn, rtx reg);
extern int _reg_unused_after (rtx insn, rtx reg);
extern int avr_jump_mode (rtx x, rtx insn);
extern int byte_immediate_operand (rtx op, enum machine_mode mode);
extern int test_hard_reg_class (enum reg_class class, rtx x);
extern int jump_over_one_insn_p (rtx insn, rtx dest);

extern int avr_hard_regno_mode_ok (int regno, enum machine_mode mode);
extern int call_insn_operand (rtx op, enum machine_mode mode);
extern void final_prescan_insn (rtx insn, rtx *operand, int num_operands);
extern int avr_simplify_comparison_p (enum machine_mode mode,
				      RTX_CODE operator, rtx x);
extern RTX_CODE avr_normalize_condition (RTX_CODE condition);
extern int compare_eq_p (rtx insn);
extern void out_shift_with_cnt (const char *template, rtx insn,
				rtx operands[], int *len, int t_len);
extern int avr_io_address_p (rtx x, int size);
extern int const_int_pow2_p (rtx x);
extern int avr_peep2_scratch_safe (rtx reg_rtx);
#endif /* RTX_CODE */

#ifdef HAVE_MACHINE_MODES
extern int class_max_nregs (enum reg_class class, enum machine_mode mode);
#endif /* HAVE_MACHINE_MODES */

#ifdef REAL_VALUE_TYPE
extern void asm_output_float (FILE *file, REAL_VALUE_TYPE n);
#endif
