/* Dependency generator for Makefile fragments.
   Copyright (C) 2000, 2001, 2003, 2007 Free Software Foundation, Inc.
   Contributed by Zack Weinberg, Mar 2000

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

 In other words, you are welcome to use, share and improve this program.
 You are forbidden to forbid anyone else to use, share and improve
 what you give them.   Help stamp out software-hoarding!  */

#include "config.h"
#include "system.h"
#include "mkdeps.h"

/* Keep this structure local to this file, so clients don't find it
   easy to start making assumptions.  */
struct deps
{
  const char **targetv;
  unsigned int ntargets;	/* number of slots actually occupied */
  unsigned int targets_size;	/* amt of allocated space - in words */

  const char **depv;
  unsigned int ndeps;
  unsigned int deps_size;

  const char **vpathv;
  size_t *vpathlv;
  unsigned int nvpaths;
  unsigned int vpaths_size;
};

static const char *munge (const char *);

/* Given a filename, quote characters in that filename which are
   significant to Make.  Note that it's not possible to quote all such
   characters - e.g. \n, %, *, ?, [, \ (in some contexts), and ~ are
   not properly handled.  It isn't possible to get this right in any
   current version of Make.  (??? Still true?  Old comment referred to
   3.76.1.)  */

static const char *
munge (const char *filename)
{
  int len;
  const char *p, *q;
  char *dst, *buffer;

  for (p = filename, len = 0; *p; p++, len++)
    {
      switch (*p)
	{
	case ' ':
	case '\t':
	  /* GNU make uses a weird quoting scheme for white space.
	     A space or tab preceded by 2N+1 backslashes represents
	     N backslashes followed by space; a space or tab
	     preceded by 2N backslashes represents N backslashes at
	     the end of a file name; and backslashes in other
	     contexts should not be doubled.  */
	  for (q = p - 1; filename <= q && *q == '\\';  q--)
	    len++;
	  len++;
	  break;

	case '$':
	  /* '$' is quoted by doubling it.  */
	  len++;
	  break;
	}
    }

  /* Now we know how big to make the buffer.  */
  buffer = XNEWVEC (char, len + 1);

  for (p = filename, dst = buffer; *p; p++, dst++)
    {
      switch (*p)
	{
	case ' ':
	case '\t':
	  for (q = p - 1; filename <= q && *q == '\\';  q--)
	    *dst++ = '\\';
	  *dst++ = '\\';
	  break;

	case '$':
	  *dst++ = '$';
	  break;

	default:
	  /* nothing */;
	}
      *dst = *p;
    }

  *dst = '\0';
  return buffer;
}

/* If T begins with any of the partial pathnames listed in d->vpathv,
   then advance T to point beyond that pathname.  */
static const char *
apply_vpath (struct deps *d, const char *t)
{
  if (d->vpathv)
    {
      unsigned int i;
      for (i = 0; i < d->nvpaths; i++)
	{
	  if (!strncmp (d->vpathv[i], t, d->vpathlv[i]))
	    {
	      const char *p = t + d->vpathlv[i];
	      if (!IS_DIR_SEPARATOR (*p))
		goto not_this_one;

	      /* Do not simplify $(vpath)/../whatever.  ??? Might not
		 be necessary. */
	      if (p[1] == '.' && p[2] == '.' && IS_DIR_SEPARATOR (p[3]))
		goto not_this_one;

	      /* found a match */
	      t = t + d->vpathlv[i] + 1;
	      break;
	    }
	not_this_one:;
	}
    }

  /* Remove leading ./ in any case.  */
  while (t[0] == '.' && IS_DIR_SEPARATOR (t[1]))
    {
      t += 2;
      /* If we removed a leading ./, then also remove any /s after the
	 first.  */
      while (IS_DIR_SEPARATOR (t[0]))
	++t;
    }

  return t;
}

/* Public routines.  */

struct deps *
deps_init (void)
{
  return XCNEW (struct deps);
}

void
deps_free (struct deps *d)
{
  unsigned int i;

  if (d->targetv)
    {
      for (i = 0; i < d->ntargets; i++)
	free ((void *) d->targetv[i]);
      free (d->targetv);
    }

  if (d->depv)
    {
      for (i = 0; i < d->ndeps; i++)
	free ((void *) d->depv[i]);
      free (d->depv);
    }

  if (d->vpathv)
    {
      for (i = 0; i < d->nvpaths; i++)
	free ((void *) d->vpathv[i]);
      free (d->vpathv);
      free (d->vpathlv);
    }

  free (d);
}

/* Adds a target T.  We make a copy, so it need not be a permanent
   string.  QUOTE is true if the string should be quoted.  */
void
deps_add_target (struct deps *d, const char *t, int quote)
{
  if (d->ntargets == d->targets_size)
    {
      d->targets_size = d->targets_size * 2 + 4;
      d->targetv = XRESIZEVEC (const char *, d->targetv, d->targets_size);
    }

  t = apply_vpath (d, t);
  if (quote)
    t = munge (t);  /* Also makes permanent copy.  */
  else
    t = xstrdup (t);

  d->targetv[d->ntargets++] = t;
}

/* Sets the default target if none has been given already.  An empty
   string as the default target in interpreted as stdin.  The string
   is quoted for MAKE.  */
void
deps_add_default_target (struct deps *d, const char *tgt)
{
  /* Only if we have no targets.  */
  if (d->ntargets)
    return;

  if (tgt[0] == '\0')
    deps_add_target (d, "-", 1);
  else
    {
#ifndef TARGET_OBJECT_SUFFIX
# define TARGET_OBJECT_SUFFIX ".o"
#endif
      const char *start = lbasename (tgt);
      char *o = (char *) alloca (strlen (start)
                                 + strlen (TARGET_OBJECT_SUFFIX) + 1);
      char *suffix;

      strcpy (o, start);

      suffix = strrchr (o, '.');
      if (!suffix)
        suffix = o + strlen (o);
      strcpy (suffix, TARGET_OBJECT_SUFFIX);

      deps_add_target (d, o, 1);
    }
}

void
deps_add_dep (struct deps *d, const char *t)
{
  t = munge (apply_vpath (d, t));  /* Also makes permanent copy.  */

  if (d->ndeps == d->deps_size)
    {
      d->deps_size = d->deps_size * 2 + 8;
      d->depv = XRESIZEVEC (const char *, d->depv, d->deps_size);
    }
  d->depv[d->ndeps++] = t;
}

void
deps_add_vpath (struct deps *d, const char *vpath)
{
  const char *elem, *p;
  char *copy;
  size_t len;

  for (elem = vpath; *elem; elem = p)
    {
      for (p = elem; *p && *p != ':'; p++);
      len = p - elem;
      copy = XNEWVEC (char, len + 1);
      memcpy (copy, elem, len);
      copy[len] = '\0';
      if (*p == ':')
	p++;

      if (d->nvpaths == d->vpaths_size)
	{
	  d->vpaths_size = d->vpaths_size * 2 + 8;
	  d->vpathv = XRESIZEVEC (const char *, d->vpathv, d->vpaths_size);
	  d->vpathlv = XRESIZEVEC (size_t, d->vpathlv, d->vpaths_size);
	}
      d->vpathv[d->nvpaths] = copy;
      d->vpathlv[d->nvpaths] = len;
      d->nvpaths++;
    }
}

void
deps_write (const struct deps *d, FILE *fp, unsigned int colmax)
{
  unsigned int size, i, column;

  column = 0;
  if (colmax && colmax < 34)
    colmax = 34;

  for (i = 0; i < d->ntargets; i++)
    {
      size = strlen (d->targetv[i]);
      column += size;
      if (colmax && column > colmax)
	{
	  fputs (" \\\n ", fp);
	  column = 1 + size;
	}
      if (i)
	{
	  putc (' ', fp);
	  column++;
	}
      fputs (d->targetv[i], fp);
    }

  putc (':', fp);
  putc (' ', fp);
  column += 2;

  for (i = 0; i < d->ndeps; i++)
    {
      size = strlen (d->depv[i]);
      column += size;
      if (colmax && column > colmax)
	{
	  fputs (" \\\n ", fp);
	  column = 1 + size;
	}
      if (i)
	{
	  putc (' ', fp);
	  column++;
	}
      fputs (d->depv[i], fp);
    }
  putc ('\n', fp);
}

void
deps_phony_targets (const struct deps *d, FILE *fp)
{
  unsigned int i;

  for (i = 1; i < d->ndeps; i++)
    {
      putc ('\n', fp);
      fputs (d->depv[i], fp);
      putc (':', fp);
      putc ('\n', fp);
    }
}

/* Write out a deps buffer to a file, in a form that can be read back
   with deps_restore.  Returns nonzero on error, in which case the
   error number will be in errno.  */

int
deps_save (struct deps *deps, FILE *f)
{
  unsigned int i;

  /* The cppreader structure contains makefile dependences.  Write out this
     structure.  */

  /* The number of dependences.  */
  if (fwrite (&deps->ndeps, sizeof (deps->ndeps), 1, f) != 1)
      return -1;
  /* The length of each dependence followed by the string.  */
  for (i = 0; i < deps->ndeps; i++)
    {
      size_t num_to_write = strlen (deps->depv[i]);
      if (fwrite (&num_to_write, sizeof (size_t), 1, f) != 1)
          return -1;
      if (fwrite (deps->depv[i], num_to_write, 1, f) != 1)
          return -1;
    }

  return 0;
}

/* Read back dependency information written with deps_save into
   the deps buffer.  The third argument may be NULL, in which case
   the dependency information is just skipped, or it may be a filename,
   in which case that filename is skipped.  */

int
deps_restore (struct deps *deps, FILE *fd, const char *self)
{
  unsigned int i, count;
  size_t num_to_read;
  size_t buf_size = 512;
  char *buf = XNEWVEC (char, buf_size);

  /* Number of dependences.  */
  if (fread (&count, 1, sizeof (count), fd) != sizeof (count))
    return -1;

  /* The length of each dependence string, followed by the string.  */
  for (i = 0; i < count; i++)
    {
      /* Read in # bytes in string.  */
      if (fread (&num_to_read, 1, sizeof (size_t), fd) != sizeof (size_t))
	return -1;
      if (buf_size < num_to_read + 1)
	{
	  buf_size = num_to_read + 1 + 127;
	  buf = XRESIZEVEC (char, buf, buf_size);
	}
      if (fread (buf, 1, num_to_read, fd) != num_to_read)
	return -1;
      buf[num_to_read] = '\0';

      /* Generate makefile dependencies from .pch if -nopch-deps.  */
      if (self != NULL && strcmp (buf, self) != 0)
        deps_add_dep (deps, buf);
    }

  free (buf);
  return 0;
}
