/* Utility to update paths from internal to external forms.
   Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005
   Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU Library General Public License as published by
the Free Software Foundation; either version 2 of the License, or (at
your option) any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.

You should have received a copy of the GNU Library General Public
License along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

/* This file contains routines to update a path, both to canonicalize
   the directory format and to handle any prefix translation.

   This file must be compiled with -DPREFIX= to specify the "prefix"
   value used by configure.  If a filename does not begin with this
   prefix, it will not be affected other than by directory canonicalization.

   Each caller of 'update_path' may specify both a filename and
   a translation prefix and consist of the name of the package that contains
   the file ("@GCC", "@BINUTIL", "@GNU", etc).

   If the prefix is not specified, the filename will only undergo
   directory canonicalization.

   If it is specified, the string given by PREFIX will be replaced
   by the specified prefix (with a '@' in front unless the prefix begins
   with a '$') and further translation will be done as follows
   until none of the two conditions below are met:

   1) If the filename begins with '@', the string between the '@' and
   the end of the name or the first '/' or directory separator will
   be considered a "key" and looked up as follows:

   -- If this is a Win32 OS, then the Registry will be examined for
      an entry of "key" in

      HKEY_LOCAL_MACHINE\SOFTWARE\Free Software Foundation\<KEY>

      if found, that value will be used. <KEY> defaults to GCC version
      string, but can be overridden at configuration time.

   -- If not found (or not a Win32 OS), the environment variable
      key_ROOT (the value of "key" concatenated with the constant "_ROOT")
      is tried.  If that fails, then PREFIX (see above) is used.

   2) If the filename begins with a '$', the rest of the string up
   to the end or the first '/' or directory separator will be used
   as an environment variable, whose value will be returned.

   Once all this is done, any '/' will be converted to DIR_SEPARATOR,
   if they are different.

   NOTE:  using resolve_keyed_path under Win32 requires linking with
   advapi32.dll.  */


#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#if defined(_WIN32) && defined(ENABLE_WIN32_REGISTRY)
#include <windows.h>
#endif
#include "prefix.h"

static const char *std_prefix = PREFIX;

static const char *get_key_value (char *);
static char *translate_name (char *);
static char *save_string (const char *, int);
static void tr (char *, int, int);

#if defined(_WIN32) && defined(ENABLE_WIN32_REGISTRY)
static char *lookup_key (char *);
static HKEY reg_key = (HKEY) INVALID_HANDLE_VALUE;
#endif

/* Given KEY, as above, return its value.  */

static const char *
get_key_value (char *key)
{
  const char *prefix = 0;
  char *temp = 0;

#if defined(_WIN32) && defined(ENABLE_WIN32_REGISTRY)
  prefix = lookup_key (key);
#endif

  if (prefix == 0)
    prefix = getenv (temp = concat (key, "_ROOT", NULL));

  if (prefix == 0)
    prefix = std_prefix;

  if (temp)
    free (temp);

  return prefix;
}

/* Return a copy of a string that has been placed in the heap.  */

static char *
save_string (const char *s, int len)
{
  char *result = XNEWVEC (char, len + 1);

  memcpy (result, s, len);
  result[len] = 0;
  return result;
}

#if defined(_WIN32) && defined(ENABLE_WIN32_REGISTRY)

#ifndef WIN32_REGISTRY_KEY
# define WIN32_REGISTRY_KEY BASEVER
#endif

/* Look up "key" in the registry, as above.  */

static char *
lookup_key (char *key)
{
  char *dst;
  DWORD size;
  DWORD type;
  LONG res;

  if (reg_key == (HKEY) INVALID_HANDLE_VALUE)
    {
      res = RegOpenKeyExA (HKEY_LOCAL_MACHINE, "SOFTWARE", 0,
			   KEY_READ, &reg_key);

      if (res == ERROR_SUCCESS)
	res = RegOpenKeyExA (reg_key, "Free Software Foundation", 0,
			     KEY_READ, &reg_key);

      if (res == ERROR_SUCCESS)
	res = RegOpenKeyExA (reg_key, WIN32_REGISTRY_KEY, 0,
			     KEY_READ, &reg_key);

      if (res != ERROR_SUCCESS)
	{
	  reg_key = (HKEY) INVALID_HANDLE_VALUE;
	  return 0;
	}
    }

  size = 32;
  dst = xmalloc (size);

  res = RegQueryValueExA (reg_key, key, 0, &type, (LPBYTE) dst, &size);
  if (res == ERROR_MORE_DATA && type == REG_SZ)
    {
      dst = xrealloc (dst, size);
      res = RegQueryValueExA (reg_key, key, 0, &type, (LPBYTE) dst, &size);
    }

  if (type != REG_SZ || res != ERROR_SUCCESS)
    {
      free (dst);
      dst = 0;
    }

  return dst;
}
#endif

/* If NAME, a malloc-ed string, starts with a '@' or '$', apply the
   translation rules above and return a newly malloc-ed name.
   Otherwise, return the given name.  */

static char *
translate_name (char *name)
{
  char code;
  char *key, *old_name;
  const char *prefix;
  int keylen;

  for (;;)
    {
      code = name[0];
      if (code != '@' && code != '$')
	break;

      for (keylen = 0;
	   (name[keylen + 1] != 0 && !IS_DIR_SEPARATOR (name[keylen + 1]));
	   keylen++)
	;

      key = (char *) alloca (keylen + 1);
      strncpy (key, &name[1], keylen);
      key[keylen] = 0;

      if (code == '@')
	{
	  prefix = get_key_value (key);
	  if (prefix == 0)
	    prefix = std_prefix;
	}
      else
	prefix = getenv (key);

      if (prefix == 0)
	prefix = PREFIX;

      /* We used to strip trailing DIR_SEPARATORs here, but that can
	 sometimes yield a result with no separator when one was coded
	 and intended by the user, causing two path components to run
	 together.  */

      old_name = name;
      name = concat (prefix, &name[keylen + 1], NULL);
      free (old_name);
    }

  return name;
}

/* In a NUL-terminated STRING, replace character C1 with C2 in-place.  */
static void
tr (char *string, int c1, int c2)
{
  do
    {
      if (*string == c1)
	*string = c2;
    }
  while (*string++);
}

/* Update PATH using KEY if PATH starts with PREFIX as a directory.
   The returned string is always malloc-ed, and the caller is
   responsible for freeing it.  */

char *
update_path (const char *path, const char *key)
{
  char *result, *p;
  const int len = strlen (std_prefix);

  if (! strncmp (path, std_prefix, len)
      && (IS_DIR_SEPARATOR(path[len])
          || path[len] == '\0')
      && key != 0)
    {
      bool free_key = false;

      if (key[0] != '$')
	{
	  key = concat ("@", key, NULL);
	  free_key = true;
	}

      result = concat (key, &path[len], NULL);
      if (free_key)
	free ((char *) key);
      result = translate_name (result);
    }
  else
    result = xstrdup (path);

#ifndef ALWAYS_STRIP_DOTDOT
#define ALWAYS_STRIP_DOTDOT 0
#endif

  p = result;
  while (1)
    {
      char *src, *dest;

      p = strchr (p, '.');
      if (p == NULL)
	break;
      /* Look for `/../'  */
      if (p[1] == '.'
	  && IS_DIR_SEPARATOR (p[2])
	  && (p != result && IS_DIR_SEPARATOR (p[-1])))
	{
	  *p = 0;
	  if (!ALWAYS_STRIP_DOTDOT && access (result, X_OK) == 0)
	    {
	      *p = '.';
	      break;
	    }
	  else
	    {
	      /* We can't access the dir, so we won't be able to
		 access dir/.. either.  Strip out `dir/../'.  If `dir'
		 turns out to be `.', strip one more path component.  */
	      dest = p;
	      do
		{
		  --dest;
		  while (dest != result && IS_DIR_SEPARATOR (*dest))
		    --dest;
		  while (dest != result && !IS_DIR_SEPARATOR (dest[-1]))
		    --dest;
		}
	      while (dest != result && *dest == '.');
	      /* If we have something like `./..' or `/..', don't
		 strip anything more.  */
	      if (*dest == '.' || IS_DIR_SEPARATOR (*dest))
		{
		  *p = '.';
		  break;
		}
	      src = p + 3;
	      while (IS_DIR_SEPARATOR (*src))
		++src;
	      p = dest;
	      while ((*dest++ = *src++) != 0)
		;
	    }
	}
      else
	++p;
    }

#ifdef UPDATE_PATH_HOST_CANONICALIZE
  /* Perform host dependent canonicalization when needed.  */
  UPDATE_PATH_HOST_CANONICALIZE (result);
#endif

#ifdef DIR_SEPARATOR_2
  /* Convert DIR_SEPARATOR_2 to DIR_SEPARATOR.  */
  if (DIR_SEPARATOR_2 != DIR_SEPARATOR)
    tr (result, DIR_SEPARATOR_2, DIR_SEPARATOR);
#endif

#if defined (DIR_SEPARATOR) && !defined (DIR_SEPARATOR_2)
  if (DIR_SEPARATOR != '/')
    tr (result, '/', DIR_SEPARATOR);
#endif

  return result;
}

/* Reset the standard prefix.  */
void
set_std_prefix (const char *prefix, int len)
{
  std_prefix = save_string (prefix, len);
}
