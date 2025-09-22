/* Specific flags and argument handling of the C++ front-end.
   Copyright (C) 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004
   Free Software Foundation, Inc.

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
#include "gcc.h"

/* This bit is set if we saw a `-xfoo' language specification.  */
#define LANGSPEC	(1<<1)
/* This bit is set if they did `-lm' or `-lmath'.  */
#define MATHLIB		(1<<2)
/* This bit is set if they did `-lc'.  */
#define WITHLIBC	(1<<3)

#ifndef MATH_LIBRARY
#define MATH_LIBRARY "-lm"
#endif
#ifndef MATH_LIBRARY_PROFILE
#define MATH_LIBRARY_PROFILE MATH_LIBRARY
#endif

#ifndef LIBSTDCXX
#define LIBSTDCXX "-lstdc++"
#endif
#ifndef LIBSTDCXX_PROFILE
#define LIBSTDCXX_PROFILE LIBSTDCXX
#endif

void
lang_specific_driver (int *in_argc, const char *const **in_argv,
		      int *in_added_libraries)
{
  int i, j;

  /* If nonzero, the user gave us the `-p' or `-pg' flag.  */
  int saw_profile_flag = 0;

  /* If nonzero, the user gave us the `-v' flag.  */
  int saw_verbose_flag = 0;

  /* This is a tristate:
     -1 means we should not link in libstdc++
     0  means we should link in libstdc++ if it is needed
     1  means libstdc++ is needed and should be linked in.  */
  int library = 0;

  /* The number of arguments being added to what's in argv, other than
     libraries.  We use this to track the number of times we've inserted
     -xc++/-xnone.  */
  int added = 0;

  /* Used to track options that take arguments, so we don't go wrapping
     those with -xc++/-xnone.  */
  const char *quote = NULL;

  /* The new argument list will be contained in this.  */
  const char **arglist;

  /* Nonzero if we saw a `-xfoo' language specification on the
     command line.  Used to avoid adding our own -xc++ if the user
     already gave a language for the file.  */
  int saw_speclang = 0;

  /* "-lm" or "-lmath" if it appears on the command line.  */
  const char *saw_math = 0;

  /* "-lc" if it appears on the command line.  */
  const char *saw_libc = 0;

  /* An array used to flag each argument that needs a bit set for
     LANGSPEC, MATHLIB, or WITHLIBC.  */
  int *args;

  /* By default, we throw on the math library if we have one.  */
  int need_math = (MATH_LIBRARY[0] != '\0');

  /* True if we should add -shared-libgcc to the command-line.  */
  int shared_libgcc = 1;

  /* The total number of arguments with the new stuff.  */
  int argc;

  /* The argument list.  */
  const char *const *argv;

  /* The number of libraries added in.  */
  int added_libraries;

  /* The total number of arguments with the new stuff.  */
  int num_args = 1;

  argc = *in_argc;
  argv = *in_argv;
  added_libraries = *in_added_libraries;

  args = XCNEWVEC (int, argc);

  for (i = 1; i < argc; i++)
    {
      /* If the previous option took an argument, we swallow it here.  */
      if (quote)
	{
	  quote = NULL;
	  continue;
	}

      /* We don't do this anymore, since we don't get them with minus
	 signs on them.  */
      if (argv[i][0] == '\0' || argv[i][1] == '\0')
	continue;

      if (argv[i][0] == '-')
	{
	  if (strcmp (argv[i], "-nostdlib") == 0
	      || strcmp (argv[i], "-nodefaultlibs") == 0)
	    {
	      library = -1;
	    }
	  else if (strcmp (argv[i], MATH_LIBRARY) == 0)
	    {
	      args[i] |= MATHLIB;
	      need_math = 0;
	    }
	  else if (strcmp (argv[i], "-lc") == 0)
	    args[i] |= WITHLIBC;
	  else if (strcmp (argv[i], "-pg") == 0 || strcmp (argv[i], "-p") == 0)
	    saw_profile_flag++;
	  else if (strcmp (argv[i], "-v") == 0)
	    saw_verbose_flag = 1;
	  else if (strncmp (argv[i], "-x", 2) == 0)
	    {
	      const char * arg;
	      if (argv[i][2] != '\0')
		arg = argv[i]+2;
	      else if ((argv[i+1]) != NULL)
		/* We need to swallow arg on next loop.  */
		quote = arg = argv[i+1];
  	      else  /* Error condition, message will be printed later.  */
		arg = "";
	      if (library == 0
		  && (strcmp (arg, "c++") == 0
		      || strcmp (arg, "c++-cpp-output") == 0))
		library = 1;
		
	      saw_speclang = 1;
	    }
	  /* Arguments that go directly to the linker might be .o files,
	     or something, and so might cause libstdc++ to be needed.  */
	  else if (strcmp (argv[i], "-Xlinker") == 0)
	    {
	      quote = argv[i];
	      if (library == 0)
		library = 1;
	    }
	  else if (strncmp (argv[i], "-Wl,", 4) == 0)
	    library = (library == 0) ? 1 : library;
	  /* Unrecognized libraries (e.g. -lfoo) may require libstdc++.  */
	  else if (strncmp (argv[i], "-l", 2) == 0)
	    library = (library == 0) ? 1 : library;
	  else if (((argv[i][2] == '\0'
		     && strchr ("bBVDUoeTuIYmLiA", argv[i][1]) != NULL)
		    || strcmp (argv[i], "-Tdata") == 0))
	    quote = argv[i];
	  else if ((argv[i][2] == '\0'
		    && strchr ("cSEM", argv[i][1]) != NULL)
		   || strcmp (argv[i], "-MM") == 0
		   || strcmp (argv[i], "-fsyntax-only") == 0)
	    {
	      /* Don't specify libraries if we won't link, since that would
		 cause a warning.  */
	      library = -1;
	    }
	  else if (strcmp (argv[i], "-static-libgcc") == 0
		   || strcmp (argv[i], "-static") == 0)
	    shared_libgcc = 0;
	  else if (DEFAULT_WORD_SWITCH_TAKES_ARG (&argv[i][1]))
	    i++;
	  else
	    /* Pass other options through.  */
	    continue;
	}
      else
	{
	  int len;

	  if (saw_speclang)
	    {
	      saw_speclang = 0;
	      continue;
	    }

	  /* If the filename ends in .[chi], put options around it.
	     But not if a specified -x option is currently active.  */
	  len = strlen (argv[i]);
	  if (len > 2
	      && (argv[i][len - 1] == 'c'
		  || argv[i][len - 1] == 'i'
		  || argv[i][len - 1] == 'h')
	      && argv[i][len - 2] == '.')
	    {
	      args[i] |= LANGSPEC;
	      added += 2;
	    }

	  /* If we don't know that this is a header file, we might
	     need to be linking in the libraries.  */
	  if (library == 0)
	    {
	      if ((len <= 2 || strcmp (argv[i] + (len - 2), ".H") != 0)
		  && (len <= 2 || strcmp (argv[i] + (len - 2), ".h") != 0)
		  && (len <= 3 || strcmp (argv[i] + (len - 3), ".hh") != 0))
		library = 1;
	    }
	}
    }

  if (quote)
    fatal ("argument to '%s' missing\n", quote);

  /* If we know we don't have to do anything, bail now.  */
  if (! added && library <= 0)
    {
      free (args);
      return;
    }

  /* There's no point adding -shared-libgcc if we don't have a shared
     libgcc.  */
#ifndef ENABLE_SHARED_LIBGCC
  shared_libgcc = 0;
#endif

  /* Make sure to have room for the trailing NULL argument.  */
  num_args = argc + added + need_math + shared_libgcc + (library > 0) + 1;
  arglist = XNEWVEC (const char *, num_args);

  i = 0;
  j = 0;

  /* Copy the 0th argument, i.e., the name of the program itself.  */
  arglist[i++] = argv[j++];

  /* NOTE: We start at 1 now, not 0.  */
  while (i < argc)
    {
      arglist[j] = argv[i];

      /* Make sure -lstdc++ is before the math library, since libstdc++
	 itself uses those math routines.  */
      if (!saw_math && (args[i] & MATHLIB) && library > 0)
	{
	  --j;
	  saw_math = argv[i];
	}

      if (!saw_libc && (args[i] & WITHLIBC) && library > 0)
	{
	  --j;
	  saw_libc = argv[i];
	}

      /* Wrap foo.[chi] files in a language specification to
	 force the gcc compiler driver to run cc1plus on them.  */
      if (args[i] & LANGSPEC)
	{
	  int len = strlen (argv[i]);
	  switch (argv[i][len - 1])
	    {
	    case 'c':
	      arglist[j++] = "-xc++";
	      break;
	    case 'i':
	      arglist[j++] = "-xc++-cpp-output";
	      break;
	    case 'h':
	      arglist[j++] = "-xc++-header";
	      break;
	    default:
	      gcc_unreachable ();
	    }
	  arglist[j++] = argv[i];
	  arglist[j] = "-xnone";
	}

      i++;
      j++;
    }

  /* Add `-lstdc++' if we haven't already done so.  */
  if (library > 0)
    {
      arglist[j] = saw_profile_flag ? LIBSTDCXX_PROFILE : LIBSTDCXX;
      if (arglist[j][0] != '-' || arglist[j][1] == 'l')
	added_libraries++;
      j++;
    }
  if (saw_math)
    arglist[j++] = saw_math;
  else if (library > 0 && need_math)
    {
      arglist[j] = saw_profile_flag ? MATH_LIBRARY_PROFILE : MATH_LIBRARY;
      if (arglist[j][0] != '-' || arglist[j][1] == 'l')
	added_libraries++;
      j++;
    }
  if (saw_libc)
    arglist[j++] = saw_libc;
  if (shared_libgcc)
    arglist[j++] = "-shared-libgcc";

  arglist[j] = NULL;

  *in_argc = j;
  *in_argv = arglist;
  *in_added_libraries = added_libraries;
}

/* Called before linking.  Returns 0 on success and -1 on failure.  */
int lang_specific_pre_link (void)  /* Not used for C++.  */
{
  return 0;
}

/* Number of extra output files that lang_specific_pre_link may generate.  */
int lang_specific_extra_outfiles = 0;  /* Not used for C++.  */
