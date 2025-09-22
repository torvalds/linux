/* Generate SDB debugging info.
   Copyright (C) 2003, 2004 Free Software Foundation, Inc.

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

/* Note that no configuration uses sdb as its preferred format.  */

#define SDB_DEBUGGING_INFO 1

/* Forward references to tags are allowed.  */
#define SDB_ALLOW_FORWARD_REFERENCES

/* Unknown tags are also allowed.  */
#define SDB_ALLOW_UNKNOWN_REFERENCES

/* Block start/end next label #.  */
extern int sdb_label_count;

/* Starting line of current function.  */
extern int sdb_begin_function_line;

/* For block start and end, we create labels, so that
   later we can figure out where the correct offset is.
   The normal .ent/.end serve well enough for functions,
   so those are just commented out.  */

#define PUT_SDB_BLOCK_START(LINE)			\
do {							\
  fprintf (asm_out_file,				\
	   "%sLb%d:\n\t.begin\t%sLb%d\t%d\n",		\
	   LOCAL_LABEL_PREFIX,				\
	   sdb_label_count,				\
	   LOCAL_LABEL_PREFIX,				\
	   sdb_label_count,				\
	   (LINE));					\
  sdb_label_count++;					\
} while (0)

#define PUT_SDB_BLOCK_END(LINE)				\
do {							\
  fprintf (asm_out_file,				\
	   "%sLe%d:\n\t.bend\t%sLe%d\t%d\n",		\
	   LOCAL_LABEL_PREFIX,				\
	   sdb_label_count,				\
	   LOCAL_LABEL_PREFIX,				\
	   sdb_label_count,				\
	   (LINE));					\
  sdb_label_count++;					\
} while (0)

#define PUT_SDB_FUNCTION_START(LINE)

#define PUT_SDB_FUNCTION_END(LINE)			\
do {							\
  SDB_OUTPUT_SOURCE_LINE (asm_out_file, LINE + sdb_begin_function_line); \
} while (0)

#define PUT_SDB_EPILOGUE_END(NAME)

/* We need to use .esize and .etype instead of .size and .type to
   avoid conflicting with ELF directives.  */
#undef PUT_SDB_SIZE
#define PUT_SDB_SIZE(a)					\
do {							\
  fprintf (asm_out_file, "\t.esize\t" HOST_WIDE_INT_PRINT_DEC ";", \
 	   (HOST_WIDE_INT) (a));			\
} while (0)

#undef PUT_SDB_TYPE
#define PUT_SDB_TYPE(a)					\
do {							\
  fprintf (asm_out_file, "\t.etype\t0x%x;", (a));	\
} while (0)
