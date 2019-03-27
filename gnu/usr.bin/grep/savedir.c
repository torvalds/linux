/* savedir.c -- save the list of files in a directory in a string
   Copyright (C) 1990, 1997, 1998, 1999, 2000, 2001 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

/* Written by David MacKenzie <djm@gnu.ai.mit.edu>. */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <sys/types.h>

#if HAVE_UNISTD_H
# include <unistd.h>
#endif

#if HAVE_DIRENT_H
# include <dirent.h>
# define NAMLEN(dirent) strlen((dirent)->d_name)
#else
# define dirent direct
# define NAMLEN(dirent) (dirent)->d_namlen
# if HAVE_SYS_NDIR_H
#  include <sys/ndir.h>
# endif
# if HAVE_SYS_DIR_H
#  include <sys/dir.h>
# endif
# if HAVE_NDIR_H
#  include <ndir.h>
# endif
#endif

#ifdef CLOSEDIR_VOID
/* Fake a return value. */
# define CLOSEDIR(d) (closedir (d), 0)
#else
# define CLOSEDIR(d) closedir (d)
#endif

#ifdef STDC_HEADERS
# include <stdlib.h>
# include <string.h>
#else
char *malloc ();
char *realloc ();
#endif
#ifndef NULL
# define NULL 0
#endif

#ifndef stpcpy
char *stpcpy ();
#endif

#include <fnmatch.h>
#include "savedir.h"
#include "system.h"

char *path;
size_t pathlen;

static int
isdir1 (const char *dir, const char *file)
{
  int status;
  int slash;
  size_t dirlen = strlen (dir);
  size_t filelen = strlen (file);
  if ((dirlen + filelen + 2) > pathlen)
    {
      path = calloc (dirlen + 1 + filelen + 1, sizeof (*path));
      pathlen = dirlen + filelen + 2;
    }
  strcpy (path, dir);
  slash = (path[dirlen] != '/');
  path[dirlen] = '/';
  strcpy (path + dirlen + slash , file);
  status  = isdir (path);
  return status;
}

/* Return a freshly allocated string containing the filenames
   in directory DIR, separated by '\0' characters;
   the end is marked by two '\0' characters in a row.
   NAME_SIZE is the number of bytes to initially allocate
   for the string; it will be enlarged as needed.
   Return NULL if DIR cannot be opened or if out of memory. */
char *
savedir (const char *dir, off_t name_size, struct exclude *included_patterns,
	 struct exclude *excluded_patterns)
{
  DIR *dirp;
  struct dirent *dp;
  char *name_space;
  char *namep;

  dirp = opendir (dir);
  if (dirp == NULL)
    return NULL;

  /* Be sure name_size is at least `1' so there's room for
     the final NUL byte.  */
  if (name_size <= 0)
    name_size = 1;

  name_space = (char *) malloc (name_size);
  if (name_space == NULL)
    {
      closedir (dirp);
      return NULL;
    }
  namep = name_space;

  while ((dp = readdir (dirp)) != NULL)
    {
      /* Skip "." and ".." (some NFS filesystems' directories lack them). */
      if (dp->d_name[0] != '.'
	  || (dp->d_name[1] != '\0'
	      && (dp->d_name[1] != '.' || dp->d_name[2] != '\0')))
	{
	  off_t size_needed = (namep - name_space) + NAMLEN (dp) + 2;

	  if ((included_patterns || excluded_patterns)
	      && !isdir1 (dir, dp->d_name))
	    {
	      if (included_patterns
		  && !excluded_filename (included_patterns, path, 0))
		continue;
	      if (excluded_patterns
		  && excluded_filename (excluded_patterns, path, 0))
		continue;
	    }

	  if (size_needed > name_size)
	    {
	      char *new_name_space;

	      while (size_needed > name_size)
		name_size += 1024;

	      new_name_space = realloc (name_space, name_size);
	      if (new_name_space == NULL)
		{
		  closedir (dirp);
		  return NULL;
		}
	      namep += new_name_space - name_space;
	      name_space = new_name_space;
	    }
	  namep = stpcpy (namep, dp->d_name) + 1;
	}
    }
  *namep = '\0';
  if (CLOSEDIR (dirp))
    {
      free (name_space);
      return NULL;
    }
  if (path)
    {
      free (path);
      path = NULL;
      pathlen = 0;
    }
  return name_space;
}
