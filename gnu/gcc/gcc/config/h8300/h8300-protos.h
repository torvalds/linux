/* Definitions of target machine for GNU compiler.
   Renesas H8/300 version
   Copyright (C) 2000, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Steve Chamberlain (sac@cygnus.com),
   Jim Wilson (wilson@cygnus.com), and Doug Evans (dje@cygnus.com).

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

#ifndef GCC_H8300_PROTOS_H
#define GCC_H8300_PROTOS_H

/* Declarations for functions used in insn-output.c.  */
#ifdef RTX_CODE
extern unsigned int compute_mov_length (rtx *);
extern const char *output_plussi (rtx *);
extern unsigned int compute_plussi_length (rtx *);
extern int compute_plussi_cc (rtx *);
extern const char *output_a_shift (rtx *);
extern unsigned int compute_a_shift_length (rtx, rtx *);
extern int compute_a_shift_cc (rtx, rtx *);
extern const char *output_a_rotate (enum rtx_code, rtx *);
extern unsigned int compute_a_rotate_length (rtx *);
extern const char *output_simode_bld (int, rtx[]);
extern void print_operand_address (FILE *, rtx);
extern void print_operand (FILE *, rtx, int);
extern void final_prescan_insn (rtx, rtx *, int);
extern int h8300_expand_movsi (rtx[]);
extern void notice_update_cc (rtx, rtx);
extern const char *output_logical_op (enum machine_mode, rtx *);
extern unsigned int compute_logical_op_length (enum machine_mode,
					       rtx *);
extern int compute_logical_op_cc (enum machine_mode, rtx *);
extern void h8300_expand_branch (enum rtx_code, rtx);
extern bool expand_a_shift (enum machine_mode, int, rtx[]);
extern int h8300_shift_needs_scratch_p (int, enum machine_mode);
extern int expand_a_rotate (rtx[]);
extern int fix_bit_operand (rtx *, enum rtx_code);
extern int h8300_adjust_insn_length (rtx, int);
extern void split_adds_subs (enum machine_mode, rtx[]);

extern int h8300_eightbit_constant_address_p (rtx);
extern int h8300_tiny_constant_address_p (rtx);
extern int byte_accesses_mergeable_p (rtx, rtx);
extern int same_cmp_preceding_p (rtx);
extern int same_cmp_following_p (rtx);

extern int h8300_legitimate_constant_p (rtx);
extern int h8300_legitimate_address_p (enum machine_mode, rtx, int);

/* Used in builtins.c */
extern rtx h8300_return_addr_rtx (int, rtx);

/* Classifies an h8sx shift operation.

   H8SX_SHIFT_NONE
	The shift cannot be done in a single instruction.

   H8SX_SHIFT_UNARY
	The shift is effectively a unary operation.  The instruction will
	allow any sort of destination operand and have a format similar
	to neg and not.  This is true of certain power-of-2 shifts.

   H8SX_SHIFT_BINARY
	The shift is a binary operation.  The destination must be a
	register and the source can be a register or a constant.  */
enum h8sx_shift_type {
  H8SX_SHIFT_NONE,
  H8SX_SHIFT_UNARY,
  H8SX_SHIFT_BINARY
};

extern enum h8sx_shift_type h8sx_classify_shift (enum machine_mode, enum rtx_code, rtx);
extern int h8300_ldm_stm_parallel (rtvec, int, int);
#endif /* RTX_CODE */

#ifdef TREE_CODE
extern struct rtx_def *function_arg (CUMULATIVE_ARGS *,
				     enum machine_mode, tree, int);
extern int h8300_funcvec_function_p (tree);
extern int h8300_eightbit_data_p (tree);
extern int h8300_tiny_data_p (tree);
#endif /* TREE_CODE */

extern void h8300_init_once (void);
extern int h8300_can_use_return_insn_p (void);
extern void h8300_expand_prologue (void);
extern void h8300_expand_epilogue (void);
extern int h8300_current_function_interrupt_function_p (void);
extern int h8300_initial_elimination_offset (int, int);
extern int h8300_regs_ok_for_stm (int, rtx[]);
extern int h8300_hard_regno_rename_ok (unsigned int, unsigned int);
extern int h8300_hard_regno_nregs (int, enum machine_mode);
extern int h8300_hard_regno_mode_ok (int, enum machine_mode);

struct cpp_reader;
extern void h8300_pr_interrupt (struct cpp_reader *);
extern void h8300_pr_saveall (struct cpp_reader *);
extern enum reg_class  h8300_reg_class_from_letter (int);
extern rtx             h8300_get_index (rtx, enum machine_mode mode, int *);
extern unsigned int    h8300_insn_length_from_table (rtx, rtx *);
extern const char *    output_h8sx_shift (rtx *, int, int);
extern bool            h8300_operands_match_p (rtx *);
extern bool            h8sx_mergeable_memrefs_p (rtx, rtx);
extern bool            h8sx_emit_movmd (rtx, rtx, rtx, HOST_WIDE_INT);
extern void            h8300_swap_into_er6 (rtx);
extern void            h8300_swap_out_of_er6 (rtx);

#endif /* ! GCC_H8300_PROTOS_H */
