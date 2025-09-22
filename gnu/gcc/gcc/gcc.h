/* Header file for modules that link with gcc.c
   Copyright (C) 1999, 2000, 2001, 2003, 2004 Free Software Foundation, Inc.

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

#ifndef GCC_GCC_H
#define GCC_GCC_H

#include "version.h"

/* The mapping of a spec function name to the C function that
   implements it.  */
struct spec_function
{
  const char *name;
  const char *(*func) (int, const char **);
};

/* This defines which switch letters take arguments.  */

#define DEFAULT_SWITCH_TAKES_ARG(CHAR) \
  ((CHAR) == 'D' || (CHAR) == 'U' || (CHAR) == 'o' \
   || (CHAR) == 'e' || (CHAR) == 'T' || (CHAR) == 'u' \
   || (CHAR) == 'I' || (CHAR) == 'm' || (CHAR) == 'x' \
   || (CHAR) == 'L' || (CHAR) == 'A' || (CHAR) == 'V' \
   || (CHAR) == 'B' || (CHAR) == 'b')

/* This defines which multi-letter switches take arguments.  */

#define DEFAULT_WORD_SWITCH_TAKES_ARG(STR)		\
 (!strcmp (STR, "Tdata") || !strcmp (STR, "Ttext")	\
  || !strcmp (STR, "Tbss") || !strcmp (STR, "include")	\
  || !strcmp (STR, "imacros") || !strcmp (STR, "aux-info") \
  || !strcmp (STR, "idirafter") || !strcmp (STR, "iprefix") \
  || !strcmp (STR, "iwithprefix") || !strcmp (STR, "iwithprefixbefore") \
  || !strcmp (STR, "iquote") || !strcmp (STR, "isystem") \
  || !strcmp (STR, "isysroot") \
  || !strcmp (STR, "-param") || !strcmp (STR, "specs") \
  || !strcmp (STR, "MF") || !strcmp (STR, "MT") || !strcmp (STR, "MQ"))


/* These are exported by gcc.c.  */
extern int do_spec (const char *);
extern void record_temp_file (const char *, int, int);
extern void fatal (const char *, ...) ATTRIBUTE_PRINTF_1 ATTRIBUTE_NORETURN;
extern void error (const char *, ...) ATTRIBUTE_PRINTF_1;
extern void pfatal_with_name (const char *) ATTRIBUTE_NORETURN;
extern void set_input (const char *);

/* Spec files linked with gcc.c must provide definitions for these.  */

/* Called before processing to change/add/remove arguments.  */
extern void lang_specific_driver (int *, const char *const **, int *);

/* Called before linking.  Returns 0 on success and -1 on failure.  */
extern int lang_specific_pre_link (void);

extern int n_infiles;

/* Number of extra output files that lang_specific_pre_link may generate.  */
extern int lang_specific_extra_outfiles;

/* A vector of corresponding output files is made up later.  */

extern const char **outfiles;

#endif /* ! GCC_GCC_H */
