/* Specific flags and argument handling of the Treelang front-end.
   Copyright (C) 2005, 2006 Free Software Foundation, Inc.

This file is part of GCC.

GNU CC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GNU CC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU CC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

#include "config.h"
#include "system.h"
#include "gcc.h"

#include "coretypes.h"
#include "tm.h"

void
lang_specific_driver (int *in_argc, const char *const **in_argv,
		      int *in_added_libraries ATTRIBUTE_UNUSED)
{
  int argc = *in_argc, i;
  const char *const *argv = *in_argv;

  for (i = 1; i < argc; ++i)
    {
      if (!strcmp (argv[i], "-fversion"))	/* Really --version!! */
	{
	  printf ("\
GNU Treelang (GCC %s)\n\
Copyright (C) 2006 Free Software Foundation, Inc.\n\
\n\
GNU Treelang comes with NO WARRANTY, to the extent permitted by law.\n\
You may redistribute copies of GNU Treelang\n\
under the terms of the GNU General Public License.\n\
For more information about these matters, see the file named COPYING\n\
", version_string);
	  exit (0);
	}
    }
}

/* Called before linking.  Returns 0 on success and -1 on failure.  */
int
lang_specific_pre_link (void)	/* Not used for Treelang.  */
{
  return 0;
}

/* Number of extra output files that lang_specific_pre_link may generate.  */
int lang_specific_extra_outfiles = 0;	/* Not used for Treelang.  */
