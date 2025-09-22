/* Prototypes for m32r.c functions used in the md file & elsewhere.
   Copyright (C) 1999, 2000, 2001, 2002, 2003, 2004, 2005
   Free Software Foundation, Inc.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 2, or (at your
   option) any later version.

   GCC is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

/* Function prototypes that cannot exist in v850.h due to dependency
   complications.  */
#define Mmode enum machine_mode

extern void   m32r_init (void);
extern void   m32r_init_expanders (void);
extern unsigned m32r_compute_frame_size (int);
extern void   m32r_expand_prologue (void);
extern int    direct_return (void);
extern void   m32r_load_pic_register (void);

#ifdef TREE_CODE
extern enum m32r_function_type m32r_compute_function_type (tree);
#endif /* TREE_CODE */

#ifdef RTX_CODE
extern int    easy_di_const (rtx);
extern int    easy_df_const (rtx);
extern rtx    gen_compare (enum rtx_code, rtx, rtx, int);
extern rtx    gen_split_move_double (rtx *);
extern int    m32r_address_code (rtx);
extern void   m32r_initialize_trampoline (rtx, rtx, rtx);
extern int    zero_and_one (rtx, rtx);
extern char * emit_cond_move (rtx *, rtx);
extern void   m32r_output_block_move (rtx, rtx *);
extern int    m32r_expand_block_move (rtx *);
extern void   m32r_print_operand (FILE *, rtx, int);
extern void   m32r_print_operand_address (FILE *, rtx);
extern int    m32r_not_same_reg (rtx, rtx);
extern int    m32r_hard_regno_rename_ok (unsigned int, unsigned int);
extern int    m32r_legitimate_pic_operand_p (rtx);
extern rtx    m32r_legitimize_pic_address (rtx, rtx);
extern rtx    m32r_return_addr (int);
extern rtx    m32r_function_symbol (const char *);

#ifdef HAVE_MACHINE_MODES
extern int    call_operand (rtx, Mmode);
extern int    small_data_operand (rtx, Mmode);
extern int    addr24_operand (rtx, Mmode);
extern int    addr32_operand (rtx, Mmode);
extern int    call26_operand (rtx, Mmode);
extern int    memreg_operand (rtx, Mmode);
extern int    small_insn_p (rtx, Mmode);

#endif /* HAVE_MACHINE_MODES */

#endif /* RTX_CODE */

#undef  Mmode
