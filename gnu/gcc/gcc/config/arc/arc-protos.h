/* Definitions of target machine for GNU compiler, Argonaut ARC cpu.
   Copyright (C) 2000, 2004 Free Software Foundation, Inc.

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

extern void arc_va_start (tree, rtx);

extern enum machine_mode arc_select_cc_mode (enum rtx_code, rtx, rtx);

/* Define the function that build the compare insn for scc and bcc.  */
extern struct rtx_def *gen_compare_reg (enum rtx_code, rtx, rtx);

/* Declarations for various fns used in the .md file.  */
extern const char *output_shift (rtx *);

extern int symbolic_operand (rtx, enum machine_mode);
extern int arc_double_limm_p (rtx);
extern int arc_eligible_for_epilogue_delay (rtx, int);
extern void arc_initialize_trampoline (rtx, rtx, rtx);
extern void arc_print_operand (FILE *, rtx, int);
extern void arc_print_operand_address (FILE *, rtx);
extern void arc_final_prescan_insn (rtx, rtx *, int);
extern int call_address_operand (rtx, enum machine_mode);
extern int call_operand (rtx, enum machine_mode);
extern int symbolic_memory_operand (rtx, enum machine_mode);
extern int short_immediate_operand (rtx, enum machine_mode);
extern int long_immediate_operand (rtx, enum machine_mode);
extern int long_immediate_loadstore_operand (rtx, enum machine_mode);
extern int move_src_operand (rtx, enum machine_mode);
extern int move_double_src_operand (rtx, enum machine_mode);
extern int move_dest_operand (rtx, enum machine_mode);
extern int load_update_operand (rtx, enum machine_mode);
extern int store_update_operand (rtx, enum machine_mode);
extern int nonvol_nonimm_operand (rtx, enum machine_mode);
extern int const_sint32_operand (rtx, enum machine_mode);
extern int const_uint32_operand (rtx, enum machine_mode);
extern int proper_comparison_operator (rtx, enum machine_mode);
extern int shift_operator (rtx, enum machine_mode);

extern enum arc_function_type arc_compute_function_type (tree);


extern void arc_init (void);
extern unsigned int arc_compute_frame_size (int);
extern void arc_save_restore (FILE *, const char *, unsigned int,
			      unsigned int, const char *);
extern int arc_delay_slots_for_epilogue (void);
extern void arc_ccfsm_at_label (const char *, int);
extern int arc_ccfsm_branch_deleted_p (void);
extern void arc_ccfsm_record_branch_deleted (void);
