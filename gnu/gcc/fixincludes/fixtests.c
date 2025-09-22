
/*

   Test to see if a particular fix should be applied to a header file.

   Copyright (C) 1997, 1998, 1999, 2000 Free Software Foundation, Inc.

= = = = = = = = = = = = = = = = = = = = = = = = =

NOTE TO DEVELOPERS

The routines you write here must work closely with fixincl.c.

Here are the rules:

1.  Every test procedure name must be suffixed with "_test".
    These routines will be referenced from inclhack.def, sans the suffix.

2.  Use the "TEST_FOR_FIX_PROC_HEAD()" macro _with_ the "_test" suffix
    (I cannot use the ## magic from ANSI C) for defining your entry point.

3.  Put your test name into the FIX_TEST_TABLE

4.  Do not write anything to stdout.  It may be closed.

5.  Write to stderr only in the event of a reportable error
    In such an event, call "exit(1)".

= = = = = = = = = = = = = = = = = = = = = = = = =

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

#include "fixlib.h"

#define _ENV_(v,m,n,t)   extern tCC* v;
ENV_TABLE
#undef _ENV_

typedef apply_fix_p_t t_test_proc ( tCC* file, tCC* text );

typedef struct {
    tCC*         test_name;
    t_test_proc* test_proc;
} test_entry_t;

#define FIX_TEST_TABLE							\
  _FT_( "machine_name",     machine_name_test )				\
  _FT_( "stdc_0_in_system_headers",    stdc_0_in_system_headers_test )

#define TEST_FOR_FIX_PROC_HEAD( test ) \
static apply_fix_p_t test ( tCC* fname ATTRIBUTE_UNUSED, \
                            tCC* text  ATTRIBUTE_UNUSED )

TEST_FOR_FIX_PROC_HEAD( machine_name_test )
{
  regex_t *label_re, *name_re;
  regmatch_t match[2];
  tCC *base, *limit;
  IGNORE_ARG(fname);

  if (!mn_get_regexps (&label_re, &name_re, "machine_name_test"))
    return SKIP_FIX;

  for (base = text;
       xregexec (label_re, base, 2, match, 0) == 0;
       base = limit)
    {
      base += match[0].rm_eo;
      /* We're looking at an #if or #ifdef.  Scan forward for the
	 next non-escaped newline.  */
      limit = base;
      do
	{
	  limit++;
	  limit = strchr (limit, '\n');
	  if (!limit)
	    return SKIP_FIX;
	}
      while (limit[-1] == '\\');

      /* If the 'name_pat' matches in between base and limit, we have
	 a bogon.  It is not worth the hassle of excluding comments,
	 because comments on #if/#ifdef/#ifndef lines are rare,
	 and strings on such lines are illegal.

	 REG_NOTBOL means 'base' is not at the beginning of a line, which
	 shouldn't matter since the name_re has no ^ anchor, but let's
	 be accurate anyway.  */

      if (xregexec (name_re, base, 1, match, REG_NOTBOL))
	return SKIP_FIX;  /* No match in file - no fix needed */

      /* Match; is it on the line?  */
      if (match[0].rm_eo <= limit - base)
	return APPLY_FIX;  /* Yup */

      /* Otherwise, keep looking... */
    }
  return SKIP_FIX;
}


TEST_FOR_FIX_PROC_HEAD( stdc_0_in_system_headers_test )
{
#ifdef STDC_0_IN_SYSTEM_HEADERS
  return (pz_machine == NULL) ? APPLY_FIX : SKIP_FIX;
#else
  return APPLY_FIX;
#endif
}


/* = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = =

     test for fix selector

     THIS IS THE ONLY EXPORTED ROUTINE

*/
apply_fix_p_t
run_test( tCC* tname, tCC* fname, tCC* text )
{
#define _FT_(n,p) { n, p },
  static test_entry_t test_table[] = { FIX_TEST_TABLE { NULL, NULL }};
#undef _FT_
#define TEST_TABLE_CT (ARRAY_SIZE (test_table)-1)

  int ct = TEST_TABLE_CT;
  test_entry_t* pte = test_table;

  do
    {
      if (strcmp( pte->test_name, tname ) == 0)
        return (*pte->test_proc)( fname, text );
      pte++;
    } while (--ct > 0);
  fprintf( stderr, "fixincludes error:  the `%s' fix test is unknown\n",
           tname );
  exit( 3 );
}
