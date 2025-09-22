/* Definitions for GCC.  Part of the machine description for CRIS.
   Copyright (C) 1998, 1999, 2000, 2001, 2004, 2005, 2006
   Free Software Foundation, Inc.
   Contributed by Axis Communications.

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

/* Prototypes for the CRIS port.  */

#if defined(FILE) || defined(stdin) || defined(stdout) || defined(getc) || defined(putc)
#define STDIO_INCLUDED
#endif

extern void cris_conditional_register_usage (void);
extern bool cris_simple_epilogue (void);
#ifdef RTX_CODE
extern const char *cris_op_str (rtx);
extern void cris_notice_update_cc (rtx, rtx);
extern bool cris_reload_address_legitimized (rtx, enum machine_mode, int, int, int);
extern void cris_print_operand (FILE *, rtx, int);
extern void cris_print_operand_address (FILE *, rtx);
extern int cris_side_effect_mode_ok (enum rtx_code, rtx *, int, int,
                                     int, int, int);
extern rtx cris_return_addr_rtx (int, rtx);
extern rtx cris_split_movdx (rtx *);
extern int cris_legitimate_pic_operand (rtx);
extern enum cris_pic_symbol_type cris_pic_symbol_type_of (rtx);
extern bool cris_valid_pic_const (rtx);
extern bool cris_store_multiple_op_p (rtx);
extern bool cris_movem_load_rest_p (rtx, int);
extern void cris_asm_output_symbol_ref (FILE *, rtx);
extern bool cris_output_addr_const_extra (FILE *, rtx);
extern int cris_cfun_uses_pic_table (void);
extern rtx cris_gen_movem_load (rtx, rtx, int);
extern rtx cris_emit_movem_store (rtx, rtx, int, bool);
extern void cris_expand_pic_call_address (rtx *);
extern void cris_order_for_addsi3 (rtx *, int);
#endif /* RTX_CODE */
extern void cris_asm_output_label_ref (FILE *, char *);
extern void cris_target_asm_named_section (const char *, unsigned int, tree);
extern void cris_expand_prologue (void);
extern void cris_expand_epilogue (void);
extern void cris_expand_return (bool);
extern bool cris_return_address_on_stack_for_return (void);
extern bool cris_return_address_on_stack (void);
extern void cris_pragma_expand_mul (struct cpp_reader *);

/* Need one that returns an int; usable in expressions.  */
extern int cris_fatal (char *);

extern void cris_override_options (void);

extern int cris_initial_elimination_offset (int, int);

extern void cris_init_expanders (void);
