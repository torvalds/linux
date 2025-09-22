/* Definitions of target machine for GNU compiler.  VAX version.
   Copyright (C) 2000, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.

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

extern void override_options (void);

extern int legitimate_constant_address_p (rtx);
extern int legitimate_constant_p (rtx);
extern int legitimate_address_p (enum machine_mode, rtx, int);
extern int vax_mode_dependent_address_p (rtx);

#ifdef RTX_CODE
extern const char *rev_cond_name (rtx);
extern void split_quadword_operands (rtx *, rtx *, int);
extern void print_operand_address (FILE *, rtx);
extern int vax_float_literal (rtx);
extern void vax_notice_update_cc (rtx, rtx);
extern const char * vax_output_int_move (rtx, rtx *, enum machine_mode);
extern const char * vax_output_int_add (rtx, rtx *, enum machine_mode);
extern const char * vax_output_conditional_branch (enum rtx_code);
#endif /* RTX_CODE */

#ifdef REAL_VALUE_TYPE
extern int check_float_value (enum machine_mode, REAL_VALUE_TYPE *, int);
#endif /* REAL_VALUE_TYPE */
