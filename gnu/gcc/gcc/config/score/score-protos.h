/* score-protos.h for Sunplus S+CORE processor
   Copyright (C) 2005 Free Software Foundation, Inc.

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

#ifndef __SCORE_PROTOS_H__
#define __SCORE_PROTOS_H__

extern enum reg_class score_char_to_class[];

void score_override_options (void);

void score_init_expanders (void);

int score_hard_regno_mode_ok (unsigned int, enum machine_mode);

int score_reg_class (int regno);

enum reg_class score_preferred_reload_class (rtx x, enum reg_class class);

enum reg_class score_secondary_reload_class (enum reg_class class,
                                             enum machine_mode mode, rtx x);

int score_const_ok_for_letter_p (HOST_WIDE_INT value, char c);

int score_extra_constraint (rtx op, char c);

rtx score_return_addr (int count, rtx frame);

HOST_WIDE_INT score_initial_elimination_offset (int from, int to);

rtx score_function_arg (const CUMULATIVE_ARGS *cum, enum machine_mode mode,
                        tree type, int named);

int score_arg_partial_nregs (const CUMULATIVE_ARGS *cum,
                             enum machine_mode mode, tree type, int named);

void score_init_cumulative_args (CUMULATIVE_ARGS *cum,
                                 tree fntype, rtx libname);

void score_function_arg_advance (CUMULATIVE_ARGS *cum, enum machine_mode mode,
                                 tree type, int named);

rtx score_function_value (tree valtype, tree func, enum machine_mode mode);

rtx score_va_arg (tree va_list, tree type);

void score_initialize_trampoline (rtx ADDR, rtx FUNC, rtx CHAIN);

int score_address_p (enum machine_mode mode, rtx x, int strict);

int score_legitimize_address (rtx *xloc);

int score_regno_mode_ok_for_base_p (int regno, int strict);

int score_register_move_cost (enum machine_mode mode, enum reg_class to,
                              enum reg_class from);

void score_declare_object (FILE *stream, const char *name,
                           const char *directive, const char *fmt, ...);

void score_declare_object_name (FILE *stream, const char *name, tree decl);

int score_output_external (FILE *file, tree decl, const char *name);

void score_print_operand (FILE *file, rtx op, int letter);

void score_print_operand_address (FILE *file, rtx addr);

#ifdef RTX_CODE
enum machine_mode score_select_cc_mode (enum rtx_code op, rtx x, rtx y);
#endif

#include "score-mdaux.h"

#endif /* __SCORE_PROTOS_H__  */

