/* Definitions of target machine for GNU compiler for Renesas / SuperH SH.
   Copyright (C) 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2003,
   2004, 2005
   Free Software Foundation, Inc.
   Contributed by Steve Chamberlain (sac@cygnus.com).
   Improved by Jim Wilson (wilson@cygnus.com).

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

#ifndef GCC_SH_PROTOS_H
#define GCC_SH_PROTOS_H

enum sh_function_kind {
  /* A function with normal C ABI  */
  FUNCTION_ORDINARY,
  /* A special function that guarantees that some otherwise call-clobbered
     registers are not clobbered.  These can't go through the SH5 resolver,
     because it only saves argument passing registers.  */
  SFUNC_GOT,
  /* A special function that should be linked statically.  These are typically
     smaller or not much larger than a PLT entry.
     Some also have a non-standard ABI which precludes dynamic linking.  */
  SFUNC_STATIC
};

#ifdef RTX_CODE
extern rtx sh_fsca_sf2int (void);
extern rtx sh_fsca_df2int (void);
extern rtx sh_fsca_int2sf (void);
extern struct rtx_def *prepare_scc_operands (enum rtx_code);

/* Declare functions defined in sh.c and used in templates.  */

extern const char *output_branch (int, rtx, rtx *);
extern const char *output_ieee_ccmpeq (rtx, rtx *);
extern const char *output_branchy_insn (enum rtx_code, const char *, rtx, rtx *);
extern const char *output_movedouble (rtx, rtx[], enum machine_mode);
extern const char *output_movepcrel (rtx, rtx[], enum machine_mode);
extern const char *output_far_jump (rtx, rtx);

extern struct rtx_def *sfunc_uses_reg (rtx);
extern int barrier_align (rtx);
extern int sh_loop_align (rtx);
extern int fp_zero_operand (rtx);
extern int fp_one_operand (rtx);
extern int fp_int_operand (rtx);
extern rtx get_fpscr_rtx (void);
extern rtx legitimize_pic_address (rtx, enum machine_mode, rtx);
extern int nonpic_symbol_mentioned_p (rtx);
extern void emit_sf_insn (rtx);
extern void emit_df_insn (rtx);
extern void print_operand_address (FILE *, rtx);
extern void print_operand (FILE *, rtx, int);
extern void output_pic_addr_const (FILE *, rtx);
extern int expand_block_move (rtx *);
extern int prepare_move_operands (rtx[], enum machine_mode mode);
extern void from_compare (rtx *, int);
extern int shift_insns_rtx (rtx);
extern void gen_ashift (int, int, rtx);
extern void gen_ashift_hi (int, int, rtx);
extern void gen_shifty_op (int, rtx *);
extern void gen_shifty_hi_op (int, rtx *);
extern int expand_ashiftrt (rtx *);
extern int sh_dynamicalize_shift_p (rtx);
extern int shl_and_kind (rtx, rtx, int *);
extern int shl_and_length (rtx);
extern int shl_and_scr_length (rtx);
extern int gen_shl_and (rtx, rtx, rtx, rtx);
extern int shl_sext_kind (rtx, rtx, int *);
extern int shl_sext_length (rtx);
extern int gen_shl_sext (rtx, rtx, rtx, rtx);
extern rtx gen_datalabel_ref (rtx);
extern int regs_used (rtx, int);
extern void fixup_addr_diff_vecs (rtx);
extern int get_dest_uid (rtx, int);
extern void final_prescan_insn (rtx, rtx *, int);
extern int symbol_ref_operand (rtx, enum machine_mode);
extern int tls_symbolic_operand (rtx, enum machine_mode);
extern int system_reg_operand (rtx, enum machine_mode);
extern int general_movsrc_operand (rtx, enum machine_mode);
extern int general_movdst_operand (rtx, enum machine_mode);
extern int arith_reg_operand (rtx, enum machine_mode);
extern int fp_arith_reg_operand (rtx, enum machine_mode);
extern int arith_operand (rtx, enum machine_mode);
extern int arith_reg_or_0_operand (rtx, enum machine_mode);
extern int logical_operand (rtx, enum machine_mode);
extern int tertiary_reload_operand (rtx, enum machine_mode);
extern int fpscr_operand (rtx, enum machine_mode);
extern int fpul_operand (rtx, enum machine_mode);
extern int commutative_float_operator (rtx, enum machine_mode);
extern int noncommutative_float_operator (rtx, enum machine_mode);
extern int reg_unused_after (rtx, rtx);
extern void expand_sf_unop (rtx (*)(rtx, rtx, rtx), rtx *);
extern void expand_sf_binop (rtx (*)(rtx, rtx, rtx, rtx), rtx *);
extern void expand_df_unop (rtx (*)(rtx, rtx, rtx), rtx *);
extern void expand_df_binop (rtx (*)(rtx, rtx, rtx, rtx), rtx *);
extern void expand_fp_branch (rtx (*)(void), rtx (*)(void));
extern int sh_insn_length_adjustment (rtx);
extern int sh_can_redirect_branch (rtx, rtx);
extern void sh_expand_unop_v2sf (enum rtx_code, rtx, rtx);
extern void sh_expand_binop_v2sf (enum rtx_code, rtx, rtx, rtx);
extern int sh_expand_t_scc (enum rtx_code code, rtx target);
extern rtx sh_gen_truncate (enum machine_mode, rtx, int);
extern bool sh_vector_mode_supported_p (enum machine_mode);
#ifdef TREE_CODE
extern void sh_va_start (tree, rtx);
#endif /* TREE_CODE */
#endif /* RTX_CODE */

extern const char *output_jump_label_table (void);
extern int sh_handle_pragma (int (*)(void), void (*)(int), const char *);
extern struct rtx_def *get_fpscr_rtx (void);
extern int sh_media_register_for_return (void);
extern void sh_expand_prologue (void);
extern void sh_expand_epilogue (bool);
extern int sh_need_epilogue (void);
extern void sh_set_return_address (rtx, rtx);
extern int initial_elimination_offset (int, int);
extern int fldi_ok (void);
extern int sh_hard_regno_rename_ok (unsigned int, unsigned int);
extern int sh_cfun_interrupt_handler_p (void);
extern int sh_attr_renesas_p (tree);
extern int sh_cfun_attr_renesas_p (void);
extern void sh_initialize_trampoline (rtx, rtx, rtx);
extern bool sh_cannot_change_mode_class
	      (enum machine_mode, enum machine_mode, enum reg_class);
extern void sh_mark_label (rtx, int);
extern int sh_register_move_cost
  (enum machine_mode mode, enum reg_class, enum reg_class);
extern int check_use_sfunc_addr (rtx, rtx);

#ifdef HARD_CONST
extern void fpscr_set_from_mem (int, HARD_REG_SET);
#endif

extern void sh_pr_interrupt (struct cpp_reader *);
extern void sh_pr_trapa (struct cpp_reader *);
extern void sh_pr_nosave_low_regs (struct cpp_reader *);
extern rtx function_symbol (rtx, const char *, enum sh_function_kind);
extern rtx sh_get_pr_initial_val (void);

extern void sh_override_options (void);

extern rtx sh_function_arg (CUMULATIVE_ARGS *, enum machine_mode, tree, int);
extern void sh_function_arg_advance (CUMULATIVE_ARGS *, enum machine_mode, tree, int);
extern int sh_pass_in_reg_p (CUMULATIVE_ARGS *, enum machine_mode, tree);
extern void sh_init_cumulative_args (CUMULATIVE_ARGS *, tree, rtx, tree, signed int, enum machine_mode);
extern bool sh_promote_prototypes (tree);

extern rtx replace_n_hard_rtx (rtx, rtx *, int , int);
extern int shmedia_cleanup_truncate (rtx *, void *);

extern int sh_contains_memref_p (rtx);
extern rtx shmedia_prepare_call_address (rtx fnaddr, int is_sibcall);
struct secondary_reload_info;
extern enum reg_class sh_secondary_reload (bool, rtx, enum reg_class,
					   enum machine_mode,
					   struct secondary_reload_info *);

#endif /* ! GCC_SH_PROTOS_H */

#ifdef SYMBIAN
extern bool         sh_symbian_dllimport_name_p       (const char *);
extern const char * sh_symbian_strip_name_encoding    (const char *);
extern bool         sh_symbian_dllexport_name_p       (const char *);
extern int          symbian_import_export_class       (tree, int);
#ifdef TREE_CODE
extern bool         sh_symbian_dllexport_p            (tree);
extern tree         sh_symbian_handle_dll_attribute   (tree *, tree, tree, int, bool *);
#ifdef RTX_CODE
extern void         sh_symbian_encode_section_info    (tree, rtx, int);
#endif
#endif
#endif /* SYMBIAN */
