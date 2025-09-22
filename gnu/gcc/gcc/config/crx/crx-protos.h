/* Prototypes for exported functions defined in crx.c
   Copyright (C) 1991, 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001,
   2002, 2003, 2004  Free Software Foundation, Inc.

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
   along with GCC; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

#ifndef GCC_CRX_PROTOS_H
#define GCC_CRX_PROTOS_H


/* Register usage. */
extern enum reg_class crx_regno_reg_class (int);
extern int crx_hard_regno_mode_ok (int regno, enum machine_mode);
#ifdef RTX_CODE
extern enum reg_class crx_secondary_reload_class (enum reg_class, enum machine_mode, rtx);
#endif /* RTX_CODE */

/* Passing function arguments.  */
extern int crx_function_arg_regno_p (int);
#ifdef TREE_CODE
extern void crx_function_arg_advance (CUMULATIVE_ARGS *, enum machine_mode, tree, int);
#ifdef RTX_CODE
extern void crx_init_cumulative_args (CUMULATIVE_ARGS *, tree, rtx);
extern rtx crx_function_arg (struct cumulative_args *, enum machine_mode, tree, int);
#endif /* RTX_CODE */
#endif /* TREE_CODE */

#ifdef RTX_CODE
/* Addressing Modes.  */
struct crx_address
{
  rtx base, index, disp, side_effect;
  int scale;
};

enum crx_addrtype
{
  CRX_INVALID, CRX_REG_REL, CRX_POST_INC, CRX_SCALED_INDX, CRX_ABSOLUTE
};

extern enum crx_addrtype crx_decompose_address (rtx addr, struct crx_address *out);
extern int crx_legitimate_address_p (enum machine_mode, rtx, int);

extern int crx_const_double_ok (rtx op);

/* Instruction output.  */
extern void crx_print_operand (FILE *, rtx, int);
extern void crx_print_operand_address (FILE *, rtx);

/* Misc functions called from crx.md.  */
extern rtx crx_expand_compare (enum rtx_code, enum machine_mode);
extern void crx_expand_branch (enum rtx_code, rtx);
extern void crx_expand_scond (enum rtx_code, rtx);

extern void crx_expand_movmem_single (rtx, rtx, rtx, rtx, rtx, unsigned HOST_WIDE_INT *);
extern int crx_expand_movmem (rtx, rtx, rtx, rtx);
#endif /* RTX_CODE */

/* Routines to compute costs.  */
extern int crx_memory_move_cost (enum machine_mode, enum reg_class, int);

/* Prologue/Epilogue functions.  */
extern int crx_initial_elimination_offset (int, int);
extern char *crx_prepare_push_pop_string (int);
extern void crx_expand_prologue (void);
extern void crx_expand_epilogue (void);


/* Handling the "interrupt" attribute */
extern int crx_interrupt_function_p (void);

#endif /* GCC_CRX_PROTOS_H */
