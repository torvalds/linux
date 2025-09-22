/* Definitions of target machine for GNU compiler.  TMS320C[34]x
   Copyright (C) 1994, 1995, 1996, 1997, 1998, 1999, 2003, 2004, 2005
   Free Software Foundation, Inc.

   Contributed by Michael Hayes (m.hayes@elec.canterbury.ac.nz)
              and Herman Ten Brugge (Haj.Ten.Brugge@net.HCC.nl).

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

#ifndef GCC_C4X_PROTOS_H
#define GCC_C4X_PROTOS_H

extern void c4x_override_options (void);

extern void c4x_optimization_options (int, int);

extern void c4x_output_ascii (FILE *, const char *, int);

extern int c4x_interrupt_function_p (void);

extern void c4x_expand_prologue (void);

extern void c4x_expand_epilogue (void);

extern int c4x_null_epilogue_p (void);

extern void c4x_global_label (const char *);

extern void c4x_external_ref (const char *);

#ifdef TREE_CODE
extern void c4x_function_arg_advance (CUMULATIVE_ARGS *, 
				      enum machine_mode, tree, int);

extern struct rtx_def *c4x_function_arg (CUMULATIVE_ARGS *,
					 enum machine_mode, tree, int);

#endif /* TREE_CODE */


#if defined(RTX_CODE) && defined(TREE_CODE)
extern void c4x_init_cumulative_args (CUMULATIVE_ARGS *c, tree, rtx);

extern rtx c4x_expand_builtin (tree, rtx, rtx, enum machine_mode, int);

extern void c4x_init_builtins (void);

#endif /* TREE_CODE and RTX_CODE*/


#ifdef RTX_CODE
extern struct rtx_def *c4x_gen_compare_reg (enum rtx_code, rtx, rtx);

extern int c4x_legitimate_address_p (enum machine_mode, rtx, int);

extern int c4x_hard_regno_mode_ok (unsigned int, enum machine_mode);

extern int c4x_hard_regno_rename_ok (unsigned int, unsigned int);

extern struct rtx_def *c4x_legitimize_address (rtx, enum machine_mode);

extern void c4x_print_operand (FILE *, rtx, int);

extern void c4x_print_operand_address (FILE *, rtx);

extern enum reg_class c4x_preferred_reload_class (rtx, enum reg_class);

extern struct rtx_def *c4x_operand_subword (rtx, int, int, enum machine_mode);

extern char *c4x_output_cbranch (const char *, rtx);

extern int c4x_label_conflict (rtx, rtx, rtx);

extern int c4x_address_conflict (rtx, rtx, int, int);

extern void c4x_rptb_insert (rtx insn);

extern int c4x_rptb_nop_p (rtx);

extern int c4x_rptb_rpts_p (rtx, rtx);

extern int c4x_check_laj_p (rtx);

extern int c4x_autoinc_operand (rtx, enum machine_mode);

extern int reg_or_const_operand (rtx, enum machine_mode);

extern int mixed_subreg_operand (rtx, enum machine_mode);

extern int reg_imm_operand (rtx, enum machine_mode);

extern int ar0_reg_operand (rtx, enum machine_mode);

extern int ar0_mem_operand (rtx, enum machine_mode);

extern int ar1_reg_operand (rtx, enum machine_mode);

extern int ar1_mem_operand (rtx, enum machine_mode);

extern int ar2_reg_operand (rtx, enum machine_mode);

extern int ar2_mem_operand (rtx, enum machine_mode);

extern int ar3_reg_operand (rtx, enum machine_mode);

extern int ar3_mem_operand (rtx, enum machine_mode);

extern int ar4_reg_operand (rtx, enum machine_mode);

extern int ar4_mem_operand (rtx, enum machine_mode);

extern int ar5_reg_operand (rtx, enum machine_mode);

extern int ar5_mem_operand (rtx, enum machine_mode);

extern int ar6_reg_operand (rtx, enum machine_mode);

extern int ar6_mem_operand (rtx, enum machine_mode);

extern int ar7_reg_operand (rtx, enum machine_mode);

extern int ar7_mem_operand (rtx, enum machine_mode);

extern int ir0_reg_operand (rtx, enum machine_mode);

extern int ir0_mem_operand (rtx, enum machine_mode);

extern int ir1_reg_operand (rtx, enum machine_mode);

extern int ir1_mem_operand (rtx, enum machine_mode);

extern int group1_reg_operand (rtx, enum machine_mode);

extern int group1_mem_operand (rtx, enum machine_mode);

extern int arx_reg_operand (rtx, enum machine_mode);

extern int not_rc_reg (rtx, enum machine_mode);

extern int not_modify_reg (rtx, enum machine_mode);

extern int c4x_shiftable_constant (rtx);

extern int c4x_immed_float_p (rtx);

extern int c4x_a_register (rtx);

extern int c4x_x_register (rtx);

extern int c4x_H_constant (rtx);

extern int c4x_I_constant (rtx);

extern int c4x_J_constant (rtx);

extern int c4x_K_constant (rtx);

extern int c4x_L_constant (rtx);

extern int c4x_N_constant (rtx);

extern int c4x_O_constant (rtx);

extern int c4x_Q_constraint (rtx);

extern int c4x_R_constraint (rtx);

extern int c4x_S_indirect (rtx);

extern int c4x_S_constraint (rtx);

extern int c4x_T_constraint (rtx);

extern int c4x_U_constraint (rtx);

extern void c4x_emit_libcall (rtx, enum rtx_code, enum machine_mode,
			      enum machine_mode, int, rtx *);

extern void c4x_emit_libcall3 (rtx, enum rtx_code, enum machine_mode, rtx *);

extern void c4x_emit_libcall_mulhi (rtx, enum rtx_code,
				    enum machine_mode, rtx *);

extern int c4x_emit_move_sequence (rtx *, enum machine_mode);

extern int legitimize_operands (enum rtx_code, rtx *, enum machine_mode);

extern int valid_operands (enum rtx_code, rtx *, enum machine_mode);

extern int valid_parallel_load_store (rtx *, enum machine_mode);

extern int valid_parallel_operands_4 (rtx *, enum machine_mode);

extern int valid_parallel_operands_5 (rtx *, enum machine_mode);

extern int valid_parallel_operands_6 (rtx *, enum machine_mode);

extern GTY(()) rtx smulhi3_libfunc;
extern GTY(()) rtx umulhi3_libfunc;
extern GTY(()) rtx fix_truncqfhi2_libfunc;
extern GTY(()) rtx fixuns_truncqfhi2_libfunc;
extern GTY(()) rtx fix_trunchfhi2_libfunc;
extern GTY(()) rtx fixuns_trunchfhi2_libfunc;
extern GTY(()) rtx floathiqf2_libfunc;
extern GTY(()) rtx floatunshiqf2_libfunc;
extern GTY(()) rtx floathihf2_libfunc;
extern GTY(()) rtx floatunshihf2_libfunc;

extern GTY(()) rtx c4x_compare_op0;	/* Operand 0 for comparisons.  */
extern GTY(()) rtx c4x_compare_op1;	/* Operand 1 for comparisons.  */

#endif /* RTX_CODE */

/* Smallest class containing REGNO.  */
extern enum reg_class c4x_regclass_map[FIRST_PSEUDO_REGISTER];
extern enum machine_mode c4x_caller_save_map[FIRST_PSEUDO_REGISTER];

extern void c4x_pr_CODE_SECTION (struct cpp_reader *);
extern void c4x_pr_DATA_SECTION (struct cpp_reader *);
extern void c4x_pr_FUNC_IS_PURE (struct cpp_reader *);
extern void c4x_pr_FUNC_NEVER_RETURNS (struct cpp_reader *);
extern void c4x_pr_INTERRUPT (struct cpp_reader *);
extern void c4x_pr_ignored (struct cpp_reader *);
extern void c4x_init_pragma (int (*) (tree *));

extern GTY(()) tree code_tree;
extern GTY(()) tree data_tree;
extern GTY(()) tree pure_tree;
extern GTY(()) tree noreturn_tree;
extern GTY(()) tree interrupt_tree;

#endif /* ! GCC_C4X_PROTOS_H */
