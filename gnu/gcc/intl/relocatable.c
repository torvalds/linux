/* Provide relocatable packages.
   Copyright (C) 2003 Free Software Foundation, Inc.
   Written by Bruno Haible <bruno@clisp.org>, 2003.

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU Library General Public License as published
   by the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301,
   USA.  */


/* Tell glibc's <stdio.h> to provide a prototype for getline().
   This must come before <config.h> because <config.h> may include
   <features.h>, and once <features.h> has been included, it's too late.  */
#ifndef _GNU_SOURCE
# define _GNU_SOURCE	1
#endif

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

/* Specification.  */
#include "relocatable.h"

#if ENABLE_RELOCATABLE

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef NO_XMALLOC
# define xmalloc malloc
#else
# include "xmalloc.h"
#endif

#if DEPENDS_ON_LIBCHARSET
# include <libcharset.h>
#endif
#if DEPENDS_ON_LIBICONV && HAVE_ICONV
# include <iconv.h>
#endif
#if DEPENDS_ON_LIBINTL && ENABLE_NLS
# include <libintl.h>
#endif

/* Faked cheap 'bool'.  */
#undef bool
#undef false
#undef true
#define bool int
#define false 0
#define true 1

/* Pathname support.
   ISSLASH(C)           tests whether C is a directory separator character.
   IS_PATH_WITH_DIR(P)  tests whether P contains a directory specification.
 */
#if defined _WIN32 || defined __WIN32__ || defined __EMX__ || defined __DJGPP__
  /* Win32, OS/2, DOS */
# define ISSLASH(C) ((C) == '/' || (C) == '\\')
# define HAS_DEVICE(P) \
    ((((P)[0] >= 'A' && (P)[0] <= 'Z') || ((P)[0] >= 'a' && (P)[0] <= 'z')) \
     && (P)[1] == ':')
# define IS_PATH_WITH_DIR(P) \
    (strchr (P, '/') != NULL || strchr (P, '\\') != NULL || HAS_DEVICE (P))
# define FILESYSTEM_PREFIX_LEN(P) (HAS_DEVICE (P) ? 2 : 0)
#else
  /* Unix */
# define ISSLASH(C) ((C) == '/')
# define IS_PATH_WITH_DIR(P) (strchr (P, '/') != NULL)
# define FILESYSTEM_PREFIX_LEN(P) 0
#endif

/* Original installation prefix.  */
static char *orig_prefix;
static size_t orig_prefix_len;
/* Current installation prefix.  */
static char *curr_prefix;
static size_t curr_prefix_len;
/* These prefixes do not end in a slash.  Anything that will be concatenated
   to them must start with a slash.  */

/* Sets the original and the current installation prefix of this module.
   Relocation simply replaces a pathname starting with the original prefix
   by the corresponding pathname with the current prefix instead.  Both
   prefixes should be directory names without trailing slash (i.e. use ""
   instead of "/").  */
static void
set_this_relocation_prefix (const char *orig_prefix_arg,
			    const char *curr_prefix_arg)
{
  if (orig_prefix_arg != NULL && curr_prefix_arg != NULL
      /* Optimization: if orig_prefix and curr_prefix are equal, the
	 relocation is a nop.  */
      && strcmp (orig_prefix_arg, curr_prefix_arg) != 0)
    {
      /* Duplicate the argument strings.  */
      char *memory;

      orig_prefix_len = strlen (orig_prefix_arg);
      curr_prefix_len = strlen (curr_prefix_arg);
      memory = (char *) xmalloc (orig_prefix_len + 1 + curr_prefix_len + 1);
#ifdef NO_XMALLOC
      if (memory != NULL)
#endif
	{
	  memcpy (memory, orig_prefix_arg, orig_prefix_len + 1);
	  orig_prefix = memory;
	  memory += orig_prefix_len + 1;
	  memcpy (memory, curr_prefix_arg, curr_prefix_len + 1);
	  curr_prefix = memory;
	  return;
	}
    }
  orig_prefix = NULL;
  curr_prefix = NULL;
  /* Don't worry about wasted memory here - this function is usually only
     called once.  */
}

/* Sets the original and the current installation prefix of the package.
   Relocation simply replaces a pathname starting with the original prefix
   by the corresponding pathname with the current prefix instead.  Both
   prefixes should be directory names without trailing slash (i.e. use ""
   instead of "/").  */
void
set_relocation_prefix (const char *orig_prefix_arg, const char *curr_prefix_arg)
{
  set_this_relocation_prefix (orig_prefix_arg, curr_prefix_arg);

  /* Now notify all dependent libraries.  */
#if DEPENDS_ON_LIBCHARSET
  libcharset_set_relocation_prefix (orig_prefix_arg, curr_prefix_arg);
#endif
#if DEPENDS_ON_LIBICONV && HAVE_ICONV && _LIBICONV_VERSION >= 0x0109
  libiconv_set_relocation_prefix (orig_prefix_arg, curr_prefix_arg);
#endif
#if DEPENDS_ON_LIBINTL && ENABLE_NLS && defined libintl_set_relocation_prefix
  libintl_set_relocation_prefix (orig_prefix_arg, curr_prefix_arg);
#endif
}

/* Convenience function:
   Computes the current installation prefix, based on the original
   installation prefix, the original installation directory of a particular
   file, and the current pathname of this file.  Returns NULL upon failure.  */
#ifdef IN_LIBRARY
#define compute_curr_prefix local_compute_curr_prefix
static
#endif
const char *
compute_curr_prefix (const char *orig_installprefix,
		     const char *orig_installdir,
		     const char *curr_pathname)
{
  const char *curr_installdir;
  const char *rel_installdir;

  if (curr_pathname == NULL)
    return NULL;

  /* Determine the relative installation directory, relative to the prefix.
     This is simply the difference between orig_installprefix and
     orig_installdir.  */
  if (strncmp (orig_installprefix, orig_installdir, strlen (orig_installprefix))
      != 0)
    /* Shouldn't happen - nothing should be installed outside $(prefix).  */
    return NULL;
  rel_installdir = orig_installdir + strlen (orig_installprefix);

  /* Determine the current installation directory.  */
  {
    const char *p_base = curr_pathname + FILESYSTEM_PREFIX_LEN (curr_pathname);
    const char *p = curr_pathname + strlen (curr_pathname);
    char *q;

    while (p > p_base)
      {
	p--;
	if (ISSLASH (*p))
	  break;
      }

    q = (char *) xmalloc (p - curr_pathname + 1);
#ifdef NO_XMALLOC
    if (q == NULL)
      return NULL;
#endif
    memcpy (q, curr_pathname, p - curr_pathname);
    q[p - curr_pathname] = '\0';
    curr_installdir = q;
  }

  /* Compute the current installation prefix by removing the trailing
     rel_installdir from it.  */
  {
    const char *rp = rel_installdir + strlen (rel_installdir);
    const char *cp = curr_installdir + strlen (curr_installdir);
    const char *cp_base =
      curr_installdir + FILESYSTEM_PREFIX_LEN (curr_installdir);

    while (rp > rel_installdir && cp > cp_base)
      {
	bool same = false;
	const char *rpi = rp;
	const char *cpi = cp;

	while (rpi > rel_installdir && cpi > cp_base)
	  {
	    rpi--;
	    cpi--;
	    if (ISSLASH (*rpi) || ISSLASH (*cpi))
	      {
		if (ISSLASH (*rpi) && ISSLASH (*cpi))
		  same = true;
		break;
	      }
#if defined _WIN32 || defined __WIN32__ || defined __EMX__ || defined __DJGPP__
	    /* Win32, OS/2, DOS - case insignificant filesystem */
	    if ((*rpi >= 'a' && *rpi <= 'z' ? *rpi - 'a' + 'A' : *rpi)
		!= (*cpi >= 'a' && *cpi <= 'z' ? *cpi - 'a' + 'A' : *cpi))
	      break;
#else
	    if (*rpi != *cpi)
	      break;
#endif
	  }
	if (!same)
	  break;
	/* The last pathname component was the same.  opi and cpi now point
	   to the slash before it.  */
	rp = rpi;
	cp = cpi;
      }

    if (rp > rel_installdir)
      /* Unexpected: The curr_installdir does not end with rel_installdir.  */
      return NULL;

    {
      size_t curr_prefix_len = cp - curr_installdir;
      char *curr_prefix;

      curr_prefix = (char *) xmalloc (curr_prefix_len + 1);
#ifdef NO_XMALLOC
      if (curr_prefix == NULL)
	return NULL;
#endif
      memcpy (curr_prefix, curr_installdir, curr_prefix_len);
      curr_prefix[curr_prefix_len] = '\0';

      return curr_prefix;
    }
  }
}

#if defined PIC && defined INSTALLDIR

/* Full pathname of shared library, or NULL.  */
static char *shared_library_fullname;

#if defined _WIN32 || defined __WIN32__

/* Determine the full pathname of the shared library when it is loaded.  */

BOOL WINAPI
DllMain (HINSTANCE module_handle, DWORD event, LPVOID reserved)
{
  (void) reserved;

  if (event == DLL_PROCESS_ATTACH)
    {
      /* The DLL is being loaded into an application's address range.  */
      static char location[MAX_PATH];

      if (!GetModuleFileName (module_handle, location, sizeof (location)))
	/* Shouldn't happen.  */
	return FALSE;

      if (!IS_PATH_WITH_DIR (location))
	/* Shouldn't happen.  */
	return FALSE;

      shared_library_fullname = strdup (location);
    }

  return TRUE;
}

#else /* Unix */

static void
find_shared_library_fullname ()
{
#ifdef __linux__
  FILE *fp;

  /* Open the current process' maps file.  It describes one VMA per line.  */
  fp = fopen ("/proc/self/maps", "r");
  if (fp)
    {
      unsigned long address = (unsigned long) &find_shared_library_fullname;
      for (;;)
	{
	  unsigned long start, end;
	  int c;

	  if (fscanf (fp, "%lx-%lx", &start, &end) != 2)
	    break;
	  if (address >= start && address <= end - 1)
	    {
	      /* Found it.  Now see if this line contains a filename.  */
	      while (c = getc (fp), c != EOF && c != '\n' && c != '/')
		continue;
	      if (c == '/')
		{
		  size_t size;
		  int len;

		  ungetc (c, fp);
		  shared_library_fullname = NULL; size = 0;
		  len = getline (&shared_library_fullname, &size, fp);
		  if (len >= 0)
		    {
		      /* Success: filled shared_library_fullname.  */
		      if (len > 0 && shared_library_fullname[len - 1] == '\n')
			shared_library_fullname[len - 1] = '\0';
		    }
		}
	      break;
	    }
	  while (c = getc (fp), c != EOF && c != '\n')
	    continue;
	}
      fclose (fp);
    }
#endif
}

#endif /* WIN32 / Unix */

/* Return the full pathname of the current shared library.
   Return NULL if unknown.
   Guaranteed to work only on Linux and Woe32.  */
static char *
get_shared_library_fullname ()
{
#if !(defined _WIN32 || defined __WIN32__)
  static bool tried_find_shared_library_fullname;
  if (!tried_find_shared_library_fullname)
    {
      find_shared_library_fullname ();
      tried_find_shared_library_fullname = true;
    }
#endif
  return shared_library_fullname;
}

#endif /* PIC */

/* Returns the pathname, relocated according to the current installation
   directory.  */
const char *
relocate (const char *pathname)
{
#if defined PIC && defined INSTALLDIR
  static int initialized;

  /* Initialization code for a shared library.  */
  if (!initialized)
    {
      /* At this point, orig_prefix and curr_prefix likely have already been
	 set through the main program's set_program_name_and_installdir
	 function.  This is sufficient in the case that the library has
	 initially been installed in the same orig_prefix.  But we can do
	 better, to also cover the cases that 1. it has been installed
	 in a different prefix before being moved to orig_prefix and (later)
	 to curr_prefix, 2. unlike the program, it has not moved away from
	 orig_prefix.  */
      const char *orig_installprefix = INSTALLPREFIX;
      const char *orig_installdir = INSTALLDIR;
      const char *curr_prefix_better;

      curr_prefix_better =
	compute_curr_prefix (orig_installprefix, orig_installdir,
			     get_shared_library_fullname ());
      if (curr_prefix_better == NULL)
	curr_prefix_better = curr_prefix;

      set_relocation_prefix (orig_installprefix, curr_prefix_better);

      initialized = 1;
    }
#endif

  /* Note: It is not necessary to perform case insensitive comparison here,
     even for DOS-like filesystems, because the pathname argument was
     typically created from the same Makefile variable as orig_prefix came
     from.  */
  if (orig_prefix != NULL && curr_prefix != NULL
      && strncmp (pathname, orig_prefix, orig_prefix_len) == 0)
    {
      if (pathname[orig_prefix_len] == '\0')
	/* pathname equals orig_prefix.  */
	return curr_prefix;
      if (ISSLASH (pathname[orig_prefix_len]))
	{
	  /* pathname starts with orig_prefix.  */
	  const char *pathname_tail = &pathname[orig_prefix_len];
	  char *result =
	    (char *) xmalloc (curr_prefix_len + strlen (pathname_tail) + 1);

#ifdef NO_XMALLOC
	  if (result != NULL)
#endif
	    {
	      memcpy (result, curr_prefix, curr_prefix_len);
	      strcpy (result + curr_prefix_len, pathname_tail);
	      return result;
	    }
	}
    }
  /* Nothing to relocate.  */
  return pathname;
}

#endif
