/* Helper routines for cygwin-specific command-line parsing.
   Contributed by Christopher Faylor (cgf@redhat.com)
   Copyright 2003, 2005 Free Software Foundation, Inc.

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

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include <string.h>

void
mingw_scan (int argc ATTRIBUTE_UNUSED,
            const char *const *argv,
            char **spec_machine)
{
  putenv (xstrdup ("GCC_CYGWIN_MINGW=0"));
 
  while (*++argv)
    if (strcmp (*argv, "-mno-win32") == 0)
      putenv (xstrdup ("GCC_CYGWIN_WIN32=0"));
    else if (strcmp (*argv, "-mwin32") == 0)
      putenv (xstrdup ("GCC_CYGWIN_WIN32=1"));
    else if (strcmp (*argv, "-mno-cygwin") == 0)
      {
	char *p = strstr (*spec_machine, "-cygwin");
	if (p)
	  {
	    int len = p - *spec_machine;
	    char *s = xmalloc (strlen (*spec_machine) + 3);
	    memcpy (s, *spec_machine, len);
	    strcpy (s + len, "-mingw32");
	    *spec_machine = s;
	  }
	putenv (xstrdup ("GCC_CYGWIN_MINGW=1"));
      }
  return;
}
