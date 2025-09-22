/* Definitions of target machine for GNU compiler.  Sun 68000/68020 version.
   Copyright (C) 2000, 2002, 2004, 2005, 2006 Free Software Foundation, Inc.

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

/* Define functions defined in aux-output.c and used in templates.  */

#ifdef RTX_CODE
extern HOST_WIDE_INT m68k_initial_elimination_offset (int from, int to);
extern const char *output_move_const_into_data_reg (rtx *);
extern int valid_mov3q_const (rtx);
extern const char *output_move_simode_const (rtx *);
extern const char *output_move_simode (rtx *);
extern const char *output_move_himode (rtx *);
extern const char *output_move_qimode (rtx *);
extern const char *output_move_stricthi (rtx *);
extern const char *output_move_strictqi (rtx *);
extern const char *output_move_double (rtx *);
extern const char *output_move_const_single (rtx *);
extern const char *output_move_const_double (rtx *);
extern const char *output_btst (rtx *, rtx, rtx, rtx, int);
extern const char *output_scc_di (rtx, rtx, rtx, rtx);
extern const char *output_addsi3 (rtx *);
extern const char *output_andsi3 (rtx *);
extern const char *output_iorsi3 (rtx *);
extern const char *output_xorsi3 (rtx *);
extern void m68k_output_pic_call (rtx dest);
extern void output_dbcc_and_branch (rtx *);
extern int floating_exact_log2 (rtx);
extern bool strict_low_part_peephole_ok (enum machine_mode mode, rtx first_insn, rtx target);

/* Functions from m68k.c used in macros.  */
extern int standard_68881_constant_p (rtx);
extern void print_operand_address (FILE *, rtx);
extern void print_operand (FILE *, rtx, int);
extern void notice_update_cc (rtx, rtx);
extern rtx legitimize_pic_address (rtx, enum machine_mode, rtx);
extern int valid_dbcc_comparison_p_2 (rtx, enum machine_mode);
extern rtx m68k_libcall_value (enum machine_mode);
extern rtx m68k_function_value (tree, tree);
extern int emit_move_sequence (rtx *, enum machine_mode, rtx);

#endif /* RTX_CODE */

extern bool m68k_regno_mode_ok (int, enum machine_mode);
extern int flags_in_68881 (void);
extern bool use_return_insn (void);
extern void override_options (void);
extern void init_68881_table (void);
extern int m68k_hard_regno_rename_ok(unsigned int, unsigned int);
