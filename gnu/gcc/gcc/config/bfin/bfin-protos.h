/* Prototypes for Blackfin functions used in the md file & elsewhere.
   Copyright (C) 2005 Free Software Foundation, Inc.

   This file is part of GNU CC.

   GNU CC is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GNU CC is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GNU CC; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

/* Function prototypes that cannot exist in bfin.h due to dependency
   complications.  */
#ifndef GCC_BFIN_PROTOS_H
#define GCC_BFIN_PROTOS_H

#define Mmode enum machine_mode

extern rtx function_arg (CUMULATIVE_ARGS *, Mmode, tree, int);
extern void function_arg_advance (CUMULATIVE_ARGS *, Mmode, tree, int);
extern bool function_arg_regno_p (int);

extern const char *output_load_immediate (rtx *);
extern const char *output_casesi_internal (rtx *);
extern char *bfin_asm_long (void);
extern char *bfin_asm_short (void);
extern int log2constp (unsigned HOST_WIDE_INT);

extern rtx legitimize_address (rtx, rtx, Mmode);
extern int hard_regno_mode_ok (int, Mmode);
extern void init_cumulative_args (CUMULATIVE_ARGS *, tree, rtx);	  
extern int bfin_frame_pointer_required (void);
extern HOST_WIDE_INT bfin_initial_elimination_offset (int, int);

extern int effective_address_32bit_p (rtx, Mmode);
extern int symbolic_reference_mentioned_p (rtx);
extern rtx bfin_gen_compare (rtx, Mmode);
extern void expand_move (rtx *, Mmode);
extern void bfin_expand_call (rtx, rtx, rtx, rtx, int);
extern bool bfin_longcall_p (rtx, int);
extern bool bfin_dsp_memref_p (rtx);
extern bool bfin_expand_strmov (rtx, rtx, rtx, rtx);

extern void conditional_register_usage (void);
extern int bfin_register_move_cost (enum machine_mode, enum reg_class,
				    enum reg_class);
extern int bfin_memory_move_cost (enum machine_mode, enum reg_class, int in);
extern enum reg_class secondary_input_reload_class (enum reg_class, Mmode,
						    rtx);
extern enum reg_class secondary_output_reload_class (enum reg_class, Mmode,
						     rtx);
extern char *section_asm_op_1 (SECT_ENUM_T);
extern char *section_asm_op (SECT_ENUM_T);
extern void override_options (void);
extern void print_operand (FILE *,  rtx, char);
extern void print_address_operand (FILE *, rtx);
extern void split_di (rtx [], int, rtx [], rtx []);
extern int split_load_immediate (rtx []);
extern void emit_pic_move (rtx *, Mmode);
extern void override_options (void);
extern void asm_conditional_branch (rtx, rtx *, int, int);
extern rtx bfin_gen_compare (rtx, Mmode);

extern int bfin_return_in_memory (tree);
extern void initialize_trampoline (rtx, rtx, rtx);
extern bool bfin_legitimate_address_p (Mmode, rtx, int);
extern rtx bfin_va_arg (tree, tree);

extern void bfin_expand_prologue (void);
extern void bfin_expand_epilogue (int, int);
extern int push_multiple_operation (rtx, Mmode);
extern int pop_multiple_operation (rtx, Mmode);
extern void output_push_multiple (rtx, rtx *);
extern void output_pop_multiple (rtx, rtx *);
extern int bfin_hard_regno_rename_ok (unsigned int, unsigned int);
extern rtx bfin_return_addr_rtx (int);
extern void bfin_hardware_loop (void);
#undef  Mmode 

#endif

