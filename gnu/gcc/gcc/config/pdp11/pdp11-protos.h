/* Definitions of target machine for GNU compiler, for the pdp-11
   Copyright (C) 2000, 2003, 2004 Free Software Foundation, Inc.
   Contributed by Michael K. Gschwind (mike@vlsivie.tuwien.ac.at).

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

/* declarations */
#ifdef RTX_CODE
extern int arith_operand (rtx, enum machine_mode);
extern int const_immediate_operand (rtx, enum machine_mode);
extern int expand_shift_operand (rtx, enum machine_mode);
extern int immediate15_operand (rtx, enum machine_mode);
extern int simple_memory_operand (rtx, enum machine_mode);

extern int legitimate_address_p (enum machine_mode, rtx);
extern int legitimate_const_double_p (rtx);
extern void notice_update_cc_on_set (rtx, rtx);
extern void output_addr_const_pdp11 (FILE *, rtx);
extern const char *output_move_double (rtx *);
extern const char *output_move_quad (rtx *);
extern const char *output_block_move (rtx *);
extern void print_operand_address (FILE *, rtx);
extern int register_move_cost (enum reg_class, enum reg_class);
#endif /* RTX_CODE */

extern void output_ascii (FILE *, const char *, int);
extern const char *output_jump (const char *, const char *, int);
