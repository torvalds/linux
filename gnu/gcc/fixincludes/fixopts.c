/* Handle options that are passed from environment variables.

   Copyright (C) 2004 Free Software Foundation, Inc.

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

#define _ENV_(v,m,n,t)   tCC* v = NULL;
ENV_TABLE
#undef _ENV_

void
initialize_opts (void)
{
  static const char var_not_found[] =
#ifndef __STDC__
    "fixincl ERROR:  %s environment variable not defined\n"
#else
    "fixincl ERROR:  %s environment variable not defined\n"
    "each of these must be defined:\n"
# define _ENV_(vv,mm,nn,tt) "\t" nn "  - " tt "\n"
  ENV_TABLE
# undef _ENV_
#endif
    ;

#define _ENV_(v,m,n,t)   { tSCC var[] = n;  \
  v = getenv (var); if (m && (v == NULL)) { \
  fprintf (stderr, var_not_found, var);     \
  exit (EXIT_FAILURE); } }

ENV_TABLE

#undef _ENV_
}
