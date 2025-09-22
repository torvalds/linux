/* Handle aliases for locale names.
   Copyright (C) 1995-1999, 2000-2001, 2003 Free Software Foundation, Inc.

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

/* Tell glibc's <string.h> to provide a prototype for mempcpy().
   This must come before <config.h> because <config.h> may include
   <features.h>, and once <features.h> has been included, it's too late.  */
#ifndef _GNU_SOURCE
# define _GNU_SOURCE    1
#endif

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <ctype.h>
#include <stdio.h>
#if defined _LIBC || defined HAVE___FSETLOCKING
# include <stdio_ext.h>
#endif
#include <sys/types.h>

#ifdef __GNUC__
# undef alloca
# define alloca __builtin_alloca
# define HAVE_ALLOCA 1
#else
# ifdef _MSC_VER
#  include <malloc.h>
#  define alloca _alloca
# else
#  if defined HAVE_ALLOCA_H || defined _LIBC
#   include <alloca.h>
#  else
#   ifdef _AIX
 #pragma alloca
#   else
#    ifndef alloca
char *alloca ();
#    endif
#   endif
#  endif
# endif
#endif

#include <stdlib.h>
#include <string.h>

#include "gettextP.h"

#if ENABLE_RELOCATABLE
# include "relocatable.h"
#else
# define relocate(pathname) (pathname)
#endif

/* @@ end of prolog @@ */

#ifdef _LIBC
/* Rename the non ANSI C functions.  This is required by the standard
   because some ANSI C functions will require linking with this object
   file and the name space must not be polluted.  */
# define strcasecmp __strcasecmp

# ifndef mempcpy
#  define mempcpy __mempcpy
# endif
# define HAVE_MEMPCPY	1
# define HAVE___FSETLOCKING	1

/* We need locking here since we can be called from different places.  */
# include <bits/libc-lock.h>

__libc_lock_define_initialized (static, lock);
#endif

#ifndef internal_function
# define internal_function
#endif

/* Some optimizations for glibc.  */
#ifdef _LIBC
# define FEOF(fp)		feof_unlocked (fp)
# define FGETS(buf, n, fp)	fgets_unlocked (buf, n, fp)
#else
# define FEOF(fp)		feof (fp)
# define FGETS(buf, n, fp)	fgets (buf, n, fp)
#endif

/* For those losing systems which don't have `alloca' we have to add
   some additional code emulating it.  */
#ifdef HAVE_ALLOCA
# define freea(p) /* nothing */
#else
# define alloca(n) malloc (n)
# define freea(p) free (p)
#endif

#if defined _LIBC_REENTRANT || defined HAVE_FGETS_UNLOCKED
# undef fgets
# define fgets(buf, len, s) fgets_unlocked (buf, len, s)
#endif
#if defined _LIBC_REENTRANT || defined HAVE_FEOF_UNLOCKED
# undef feof
# define feof(s) feof_unlocked (s)
#endif


struct alias_map
{
  const char *alias;
  const char *value;
};


#ifndef _LIBC
# define libc_freeres_ptr(decl) decl
#endif

libc_freeres_ptr (static char *string_space);
static size_t string_space_act;
static size_t string_space_max;
libc_freeres_ptr (static struct alias_map *map);
static size_t nmap;
static size_t maxmap;


/* Prototypes for local functions.  */
static size_t read_alias_file PARAMS ((const char *fname, int fname_len))
     internal_function;
static int extend_alias_table PARAMS ((void));
static int alias_compare PARAMS ((const struct alias_map *map1,
				  const struct alias_map *map2));


const char *
_nl_expand_alias (name)
    const char *name;
{
  static const char *locale_alias_path;
  struct alias_map *retval;
  const char *result = NULL;
  size_t added;

#ifdef _LIBC
  __libc_lock_lock (lock);
#endif

  if (locale_alias_path == NULL)
    locale_alias_path = LOCALE_ALIAS_PATH;

  do
    {
      struct alias_map item;

      item.alias = name;

      if (nmap > 0)
	retval = (struct alias_map *) bsearch (&item, map, nmap,
					       sizeof (struct alias_map),
					       (int (*) PARAMS ((const void *,
								 const void *))
						) alias_compare);
      else
	retval = NULL;

      /* We really found an alias.  Return the value.  */
      if (retval != NULL)
	{
	  result = retval->value;
	  break;
	}

      /* Perhaps we can find another alias file.  */
      added = 0;
      while (added == 0 && locale_alias_path[0] != '\0')
	{
	  const char *start;

	  while (locale_alias_path[0] == PATH_SEPARATOR)
	    ++locale_alias_path;
	  start = locale_alias_path;

	  while (locale_alias_path[0] != '\0'
		 && locale_alias_path[0] != PATH_SEPARATOR)
	    ++locale_alias_path;

	  if (start < locale_alias_path)
	    added = read_alias_file (start, locale_alias_path - start);
	}
    }
  while (added != 0);

#ifdef _LIBC
  __libc_lock_unlock (lock);
#endif

  return result;
}


static size_t
internal_function
read_alias_file (fname, fname_len)
     const char *fname;
     int fname_len;
{
  FILE *fp;
  char *full_fname;
  size_t added;
  static const char aliasfile[] = "/locale.alias";

  full_fname = (char *) alloca (fname_len + sizeof aliasfile);
#ifdef HAVE_MEMPCPY
  mempcpy (mempcpy (full_fname, fname, fname_len),
	   aliasfile, sizeof aliasfile);
#else
  memcpy (full_fname, fname, fname_len);
  memcpy (&full_fname[fname_len], aliasfile, sizeof aliasfile);
#endif

  fp = fopen (relocate (full_fname), "r");
  freea (full_fname);
  if (fp == NULL)
    return 0;

#ifdef HAVE___FSETLOCKING
  /* No threads present.  */
  __fsetlocking (fp, FSETLOCKING_BYCALLER);
#endif

  added = 0;
  while (!FEOF (fp))
    {
      /* It is a reasonable approach to use a fix buffer here because
	 a) we are only interested in the first two fields
	 b) these fields must be usable as file names and so must not
	    be that long
	 We avoid a multi-kilobyte buffer here since this would use up
	 stack space which we might not have if the program ran out of
	 memory.  */
      char buf[400];
      char *alias;
      char *value;
      char *cp;

      if (FGETS (buf, sizeof buf, fp) == NULL)
	/* EOF reached.  */
	break;

      cp = buf;
      /* Ignore leading white space.  */
      while (isspace ((unsigned char) cp[0]))
	++cp;

      /* A leading '#' signals a comment line.  */
      if (cp[0] != '\0' && cp[0] != '#')
	{
	  alias = cp++;
	  while (cp[0] != '\0' && !isspace ((unsigned char) cp[0]))
	    ++cp;
	  /* Terminate alias name.  */
	  if (cp[0] != '\0')
	    *cp++ = '\0';

	  /* Now look for the beginning of the value.  */
	  while (isspace ((unsigned char) cp[0]))
	    ++cp;

	  if (cp[0] != '\0')
	    {
	      size_t alias_len;
	      size_t value_len;

	      value = cp++;
	      while (cp[0] != '\0' && !isspace ((unsigned char) cp[0]))
		++cp;
	      /* Terminate value.  */
	      if (cp[0] == '\n')
		{
		  /* This has to be done to make the following test
		     for the end of line possible.  We are looking for
		     the terminating '\n' which do not overwrite here.  */
		  *cp++ = '\0';
		  *cp = '\n';
		}
	      else if (cp[0] != '\0')
		*cp++ = '\0';

	      if (nmap >= maxmap)
		if (__builtin_expect (extend_alias_table (), 0))
		  return added;

	      alias_len = strlen (alias) + 1;
	      value_len = strlen (value) + 1;

	      if (string_space_act + alias_len + value_len > string_space_max)
		{
		  /* Increase size of memory pool.  */
		  size_t new_size = (string_space_max
				     + (alias_len + value_len > 1024
					? alias_len + value_len : 1024));
		  char *new_pool = (char *) realloc (string_space, new_size);
		  if (new_pool == NULL)
		    return added;

		  if (__builtin_expect (string_space != new_pool, 0))
		    {
		      size_t i;

		      for (i = 0; i < nmap; i++)
			{
			  map[i].alias += new_pool - string_space;
			  map[i].value += new_pool - string_space;
			}
		    }

		  string_space = new_pool;
		  string_space_max = new_size;
		}

	      map[nmap].alias = memcpy (&string_space[string_space_act],
					alias, alias_len);
	      string_space_act += alias_len;

	      map[nmap].value = memcpy (&string_space[string_space_act],
					value, value_len);
	      string_space_act += value_len;

	      ++nmap;
	      ++added;
	    }
	}

      /* Possibly not the whole line fits into the buffer.  Ignore
	 the rest of the line.  */
      while (strchr (buf, '\n') == NULL)
	if (FGETS (buf, sizeof buf, fp) == NULL)
	  /* Make sure the inner loop will be left.  The outer loop
	     will exit at the `feof' test.  */
	  break;
    }

  /* Should we test for ferror()?  I think we have to silently ignore
     errors.  --drepper  */
  fclose (fp);

  if (added > 0)
    qsort (map, nmap, sizeof (struct alias_map),
	   (int (*) PARAMS ((const void *, const void *))) alias_compare);

  return added;
}


static int
extend_alias_table ()
{
  size_t new_size;
  struct alias_map *new_map;

  new_size = maxmap == 0 ? 100 : 2 * maxmap;
  new_map = (struct alias_map *) realloc (map, (new_size
						* sizeof (struct alias_map)));
  if (new_map == NULL)
    /* Simply don't extend: we don't have any more core.  */
    return -1;

  map = new_map;
  maxmap = new_size;
  return 0;
}


static int
alias_compare (map1, map2)
     const struct alias_map *map1;
     const struct alias_map *map2;
{
#if defined _LIBC || defined HAVE_STRCASECMP
  return strcasecmp (map1->alias, map2->alias);
#else
  const unsigned char *p1 = (const unsigned char *) map1->alias;
  const unsigned char *p2 = (const unsigned char *) map2->alias;
  unsigned char c1, c2;

  if (p1 == p2)
    return 0;

  do
    {
      /* I know this seems to be odd but the tolower() function in
	 some systems libc cannot handle nonalpha characters.  */
      c1 = isupper (*p1) ? tolower (*p1) : *p1;
      c2 = isupper (*p2) ? tolower (*p2) : *p2;
      if (c1 == '\0')
	break;
      ++p1;
      ++p2;
    }
  while (c1 == c2);

  return c1 - c2;
#endif
}
