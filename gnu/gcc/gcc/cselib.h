/* Common subexpression elimination for GNU compiler.
   Copyright (C) 1987, 1988, 1989, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2003, 2004, 2005 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

/* Describe a value.  */
typedef struct cselib_val_struct GTY(())
{
  /* The hash value.  */
  unsigned int value;
  union cselib_val_u
  {
    /* A VALUE rtx that points back to this structure.  */
    rtx GTY ((tag ("1"))) val_rtx;
    /* Used to keep a list of free cselib_val structures.  */
    struct cselib_val_struct * GTY ((skip)) next_free;
  } GTY ((desc ("1"))) u;

  /* All rtl expressions that hold this value at the current time during a
     scan.  */
  struct elt_loc_list *locs;
  /* If this value is used as an address, points to a list of values that
     use it as an address in a MEM.  */
  struct elt_list *addr_list;

  struct cselib_val_struct *next_containing_mem;
} cselib_val;

/* A list of rtl expressions that hold the same value.  */
struct elt_loc_list GTY(())
{
  /* Next element in the list.  */
  struct elt_loc_list *next;
  /* An rtl expression that holds the value.  */
  rtx loc;
  /* The insn that made the equivalence.  */
  rtx setting_insn;
  /* True when setting insn is inside libcall.  */
  bool in_libcall;
};

/* A list of cselib_val structures.  */
struct elt_list GTY(())
{
  struct elt_list *next;
  cselib_val *elt;
};

extern cselib_val *cselib_lookup (rtx, enum machine_mode, int);
extern void cselib_init (bool record_memory);
extern void cselib_clear_table (void);
extern void cselib_finish (void);
extern void cselib_process_insn (rtx);
extern enum machine_mode cselib_reg_set_mode (rtx);
extern int rtx_equal_for_cselib_p (rtx, rtx);
extern int references_value_p (rtx, int);
extern rtx cselib_subst_to_values (rtx);
extern void cselib_invalidate_rtx (rtx);
