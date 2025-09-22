/* Target Prototypes for R8C/M16C/M32C
   Copyright (C) 2005
   Free Software Foundation, Inc.
   Contributed by Red Hat.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 2, or (at your
   option) any later version.

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.  */

#define MM enum machine_mode
#define UINT unsigned int

int  m32c_class_likely_spilled_p (int);
void m32c_conditional_register_usage (void);
int  m32c_const_ok_for_constraint_p (HOST_WIDE_INT, char, const char *);
UINT m32c_dwarf_frame_regnum (int);
int  m32c_eh_return_data_regno (int);
void m32c_emit_epilogue (void);
void m32c_emit_prologue (void);
int  m32c_epilogue_uses (int);
int  m32c_extra_address_constraint (char, const char *);
int  m32c_extra_memory_constraint (char, const char *);
int  m32c_function_arg_regno_p (int);
void m32c_init_expanders (void);
int  m32c_initial_elimination_offset (int, int);
void m32c_output_reg_pop (FILE *, int);
void m32c_output_reg_push (FILE *, int);
void m32c_override_options (void);
int  m32c_print_operand_punct_valid_p (int);
int  m32c_push_rounding (int);
int  m32c_reg_class_from_constraint (char, const char *);
void m32c_register_pragmas (void);
int  m32c_regno_ok_for_base_p (int);
int  m32c_trampoline_alignment (void);
int  m32c_trampoline_size (void);
void m32c_unpend_compare (void);

#if defined(RTX_CODE) && defined(TREE_CODE)

rtx  m32c_function_arg (CUMULATIVE_ARGS *, MM, tree, int);
rtx  m32c_function_value (tree, tree);

#endif

#ifdef RTX_CODE

int  m32c_cannot_change_mode_class (MM, MM, int);
int  m32c_class_max_nregs (int, MM);
rtx  m32c_cmp_flg_0 (rtx);
rtx  m32c_eh_return_stackadj_rtx (void);
void m32c_emit_eh_epilogue (rtx);
int  m32c_expand_cmpstr (rtx *);
int  m32c_expand_insv (rtx *);
int  m32c_expand_movcc (rtx *);
int  m32c_expand_movmemhi (rtx *);
int  m32c_expand_movstr (rtx *);
void m32c_expand_neg_mulpsi3 (rtx *);
int  m32c_expand_setmemhi (rtx *);
void m32c_expand_scc (int, rtx *);
int  m32c_extra_constraint_p (rtx, char, const char *);
int  m32c_extra_constraint_p2 (rtx, char, const char *);
int  m32c_hard_regno_nregs (int, MM);
int  m32c_hard_regno_ok (int, MM);
bool m32c_immd_dbl_mov (rtx *, MM);
rtx  m32c_incoming_return_addr_rtx (void);
void m32c_initialize_trampoline (rtx, rtx, rtx);
int  m32c_legitimate_address_p (MM, rtx, int);
int  m32c_legitimate_constant_p (rtx);
int  m32c_legitimize_address (rtx *, rtx, MM);
int  m32c_legitimize_reload_address (rtx *, MM, int, int, int);
rtx  m32c_libcall_value (MM);
int  m32c_limit_reload_class (MM, int);
int  m32c_memory_move_cost (MM, int, int);
int  m32c_mode_dependent_address (rtx);
int  m32c_modes_tieable_p (MM, MM);
bool m32c_mov_ok (rtx *, MM);
char * m32c_output_compare (rtx, rtx *);
void m32c_pend_compare (rtx *);
int  m32c_preferred_output_reload_class (rtx, int);
int  m32c_preferred_reload_class (rtx, int);
int  m32c_prepare_move (rtx *, MM);
int  m32c_prepare_shift (rtx *, int, int);
void m32c_print_operand (FILE *, rtx, int);
void m32c_print_operand_address (FILE *, rtx);
int  m32c_reg_ok_for_base_p (rtx, int);
int  m32c_register_move_cost (MM, int, int);
MM   m32c_regno_reg_class (int);
rtx  m32c_return_addr_rtx (int);
const char *m32c_scc_pattern (rtx *, RTX_CODE);
int  m32c_secondary_reload_class (int, MM, rtx);
int  m32c_split_move (rtx *, MM, int);
int  m32c_split_psi_p (rtx *);

#endif

#ifdef TREE_CODE

void m32c_function_arg_advance (CUMULATIVE_ARGS *, MM, tree, int);
tree m32c_gimplify_va_arg_expr (tree, tree, tree *, tree *);
void m32c_init_cumulative_args (CUMULATIVE_ARGS *, tree, rtx, tree, int);
bool m32c_promote_function_return (tree);

#endif

#undef MM
#undef UINT
