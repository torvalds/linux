/* histfile.c - functions to manipulate the history file. */

/* Copyright (C) 1989, 1992 Free Software Foundation, Inc.

   This file contains the GNU History Library (the Library), a set of
   routines for managing the text of previously typed lines.

   The Library is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   The Library is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   The GNU General Public License is often shipped with GNU software, and
   is generally kept in a file called COPYING or LICENSE.  If you do not
   have a copy of the license, write to the Free Software Foundation,
   59 Temple Place, Suite 330, Boston, MA 02111 USA. */

/* The goal is to make the implementation transparent, so that you
   don't have to know what data types are used, just what functions
   you can call.  I think I have done that. */
#define READLINE_LIBRARY

#if defined (HAVE_CONFIG_H)
#  include <config.h>
#endif

#include <stdio.h>

#include <sys/types.h>
#ifndef _MINIX
#  include <sys/file.h>
#endif
#include "posixstat.h"
#include <fcntl.h>

#if defined (HAVE_STDLIB_H)
#  include <stdlib.h>
#else
#  include "ansi_stdlib.h"
#endif /* HAVE_STDLIB_H */

#if defined (HAVE_UNISTD_H)
#  include <unistd.h>
#endif

#if defined (__EMX__) || defined (__CYGWIN__)
#  undef HAVE_MMAP
#endif

#ifdef HAVE_MMAP
#  include <sys/mman.h>

#  ifdef MAP_FILE
#    define MAP_RFLAGS	(MAP_FILE|MAP_PRIVATE)
#    define MAP_WFLAGS	(MAP_FILE|MAP_SHARED)
#  else
#    define MAP_RFLAGS	MAP_PRIVATE
#    define MAP_WFLAGS	MAP_SHARED
#  endif

#  ifndef MAP_FAILED
#    define MAP_FAILED	((void *)-1)
#  endif

#endif /* HAVE_MMAP */

/* If we're compiling for __EMX__ (OS/2) or __CYGWIN__ (cygwin32 environment
   on win 95/98/nt), we want to open files with O_BINARY mode so that there
   is no \n -> \r\n conversion performed.  On other systems, we don't want to
   mess around with O_BINARY at all, so we ensure that it's defined to 0. */
#if defined (__EMX__) || defined (__CYGWIN__)
#  ifndef O_BINARY
#    define O_BINARY 0
#  endif
#else /* !__EMX__ && !__CYGWIN__ */
#  undef O_BINARY
#  define O_BINARY 0
#endif /* !__EMX__ && !__CYGWIN__ */

#include <errno.h>
#if !defined (errno)
extern int errno;
#endif /* !errno */

#include "history.h"
#include "histlib.h"

#include "rlshell.h"
#include "xmalloc.h"

/* Return the string that should be used in the place of this
   filename.  This only matters when you don't specify the
   filename to read_history (), or write_history (). */
static char *
history_filename (filename)
     const char *filename;
{
  char *return_val;
  const char *home;
  int home_len;
  char dot;

  return_val = filename ? savestring (filename) : (char *)NULL;

  if (return_val)
    return (return_val);

  home = sh_get_env_value ("HOME");

  if (home == 0 || *home == '\0') {
    errno = ENOENT;
    return (NULL);
  }
  home_len = strlen (home);

#if defined (__MSDOS__)
  dot = '_';
#else
  dot = '.';
#endif
  if (asprintf(&return_val, "%s/%c%s", home, dot, "history") == -1)
	  memory_error_and_abort("asprintf");
  return (return_val);
}

/* Add the contents of FILENAME to the history list, a line at a time.
   If FILENAME is NULL, then read from ~/.history.  Returns 0 if
   successful, or errno if not. */
int
read_history (filename)
     const char *filename;
{
  return (read_history_range (filename, 0, -1));
}

/* Read a range of lines from FILENAME, adding them to the history list.
   Start reading at the FROM'th line and end at the TO'th.  If FROM
   is zero, start at the beginning.  If TO is less than FROM, read
   until the end of the file.  If FILENAME is NULL, then read from
   ~/.history.  Returns 0 if successful, or errno if not. */
int
read_history_range (filename, from, to)
     const char *filename;
     int from, to;
{
  register char *line_start, *line_end;
  char *input, *buffer, *bufend;
  int file, current_line, chars_read;
  struct stat finfo;
  size_t file_size;

  buffer = (char *)NULL;
  if ((input = history_filename (filename)))
    file = open (input, O_RDONLY|O_BINARY, 0666);
  else
    file = -1;

  if ((file < 0) || (fstat (file, &finfo) == -1))
    goto error_and_exit;

  file_size = (size_t)finfo.st_size;

  /* check for overflow on very large files */
  if (file_size != finfo.st_size || file_size + 1 < file_size)
    {
#if defined (EFBIG)
      errno = EFBIG;
#elif defined (EOVERFLOW)
      errno = EOVERFLOW;
#endif
      goto error_and_exit;
    }

#ifdef HAVE_MMAP
  /* We map read/write and private so we can change newlines to NULs without
     affecting the underlying object. */
  buffer = (char *)mmap (0, file_size, PROT_READ|PROT_WRITE, MAP_RFLAGS, file, 0);
  if ((void *)buffer == MAP_FAILED)
    goto error_and_exit;
  chars_read = file_size;
#else
  buffer = (char *)malloc (file_size + 1);
  if (buffer == 0)
    goto error_and_exit;

  chars_read = read (file, buffer, file_size);
#endif
  if (chars_read < 0)
    {
  error_and_exit:
      chars_read = errno;
      if (file >= 0)
	close (file);

      FREE (input);
#ifndef HAVE_MMAP
      FREE (buffer);
#endif

      return (chars_read);
    }

  close (file);

  /* Set TO to larger than end of file if negative. */
  if (to < 0)
    to = chars_read;

  /* Start at beginning of file, work to end. */
  bufend = buffer + chars_read;
  current_line = 0;

  /* Skip lines until we are at FROM. */
  for (line_start = line_end = buffer; line_end < bufend && current_line < from; line_end++)
    if (*line_end == '\n')
      {
	current_line++;
	line_start = line_end + 1;
      }

  /* If there are lines left to gobble, then gobble them now. */
  for (line_end = line_start; line_end < bufend; line_end++)
    if (*line_end == '\n')
      {
	*line_end = '\0';

	if (*line_start)
	  add_history (line_start);

	current_line++;

	if (current_line >= to)
	  break;

	line_start = line_end + 1;
      }

  FREE (input);
#ifndef HAVE_MMAP
  FREE (buffer);
#else
  munmap (buffer, file_size);
#endif

  return (0);
}

/* Truncate the history file FNAME, leaving only LINES trailing lines.
   If FNAME is NULL, then use ~/.history.  Returns 0 on success, errno
   on failure. */
int
history_truncate_file (fname, lines)
     const char *fname;
     int lines;
{
  char *buffer, *filename, *bp;
  int file, chars_read, rv;
  struct stat finfo;
  size_t file_size;

  buffer = (char *)NULL;
  if ((filename = history_filename (fname)))
    file = open (filename, O_RDONLY|O_BINARY, 0666);
  else
    file = -1;
  rv = 0;

  /* Don't try to truncate non-regular files. */
  if (file == -1 || fstat (file, &finfo) == -1)
    {
      rv = errno;
      if (file != -1)
	close (file);
      goto truncate_exit;
    }

  if (S_ISREG (finfo.st_mode) == 0)
    {
      close (file);
#ifdef EFTYPE
      rv = EFTYPE;
#else
      rv = EINVAL;
#endif
      goto truncate_exit;
    }

  file_size = (size_t)finfo.st_size;

  /* check for overflow on very large files */
  if (file_size != finfo.st_size || file_size + 1 < file_size)
    {
      close (file);
#if defined (EFBIG)
      rv = errno = EFBIG;
#elif defined (EOVERFLOW)
      rv = errno = EOVERFLOW;
#else
      rv = errno = EINVAL;
#endif
      goto truncate_exit;
    }

  buffer = (char *)malloc (file_size + 1);
  if (buffer == 0)
    {
      close (file);
      goto truncate_exit;
    }

  chars_read = read (file, buffer, file_size);
  close (file);

  if (chars_read <= 0)
    {
      rv = (chars_read < 0) ? errno : 0;
      goto truncate_exit;
    }

  /* Count backwards from the end of buffer until we have passed
     LINES lines. */
  for (bp = buffer + chars_read - 1; lines && bp > buffer; bp--)
    {
      if (*bp == '\n')
	lines--;
    }

  /* If this is the first line, then the file contains exactly the
     number of lines we want to truncate to, so we don't need to do
     anything.  It's the first line if we don't find a newline between
     the current value of i and 0.  Otherwise, write from the start of
     this line until the end of the buffer. */
  for ( ; bp > buffer; bp--)
    if (*bp == '\n')
      {
	bp++;
	break;
      }

  /* Write only if there are more lines in the file than we want to
     truncate to. */
  if (bp > buffer && ((file = open (filename, O_WRONLY|O_TRUNC|O_BINARY, 0600)) != -1))
    {
      write (file, bp, chars_read - (bp - buffer));

#if defined (__BEOS__)
      /* BeOS ignores O_TRUNC. */
      ftruncate (file, chars_read - (bp - buffer));
#endif

      close (file);
    }

 truncate_exit:

  FREE (buffer);

  free (filename);
  return rv;
}

/* Workhorse function for writing history.  Writes NELEMENT entries
   from the history list to FILENAME.  OVERWRITE is non-zero if you
   wish to replace FILENAME with the entries. */
static int
history_do_write (filename, nelements, overwrite)
     const char *filename;
     int nelements, overwrite;
{
  register int i;
  char *output;
  int file, mode, rv;
#ifdef HAVE_MMAP
  size_t cursize;
#endif

#ifdef HAVE_MMAP
  mode = overwrite ? O_RDWR|O_CREAT|O_TRUNC|O_BINARY : O_RDWR|O_APPEND|O_BINARY;
#else
  mode = overwrite ? O_WRONLY|O_CREAT|O_TRUNC|O_BINARY : O_WRONLY|O_APPEND|O_BINARY;
#endif
  output = history_filename (filename);
  rv = 0;

  if (!output || (file = open (output, mode, 0600)) == -1)
    {
      FREE (output);
      return (errno);
    }

#ifdef HAVE_MMAP
  cursize = overwrite ? 0 : lseek (file, 0, SEEK_END);
#endif

  if (nelements > history_length)
    nelements = history_length;

  /* Build a buffer of all the lines to write, and write them in one syscall.
     Suggested by Peter Ho (peter@robosts.oxford.ac.uk). */
  {
    HIST_ENTRY **the_history;	/* local */
    int buffer_size;
    char *buffer;

    the_history = history_list ();
    /* Calculate the total number of bytes to write. */
    for (buffer_size = 1, i = history_length - nelements; i < history_length; i++)
      buffer_size += 1 + strlen (the_history[i]->line);

    /* Allocate the buffer, and fill it. */
#ifdef HAVE_MMAP
    if (ftruncate (file, buffer_size+cursize) == -1)
      goto mmap_error;
    buffer = (char *)mmap (0, buffer_size, PROT_READ|PROT_WRITE, MAP_WFLAGS, file, cursize);
    if ((void *)buffer == MAP_FAILED)
      {
mmap_error:
	rv = errno;
	FREE (output);
	close (file);
	return rv;
      }
#else
    buffer = (char *)malloc (buffer_size);
    if (buffer == 0)
      {
	rv = errno;
	FREE (output);
	close (file);
	return rv;
      }
#endif
    buffer[0] = '\0';

    for (i = history_length - nelements; i < history_length; i++)
      {
	strlcat (buffer, the_history[i]->line, buffer_size);
	strlcat (buffer, "\n", buffer_size);
      }

#ifdef HAVE_MMAP
    if (msync (buffer, buffer_size, 0) != 0 || munmap (buffer, buffer_size) != 0)
      rv = errno;
#else
    if (write (file, buffer, buffer_size - 1) < 0)
      rv = errno;
    free (buffer);
#endif
  }

  close (file);

  FREE (output);

  return (rv);
}

/* Append NELEMENT entries to FILENAME.  The entries appended are from
   the end of the list minus NELEMENTs up to the end of the list. */
int
append_history (nelements, filename)
     int nelements;
     const char *filename;
{
  return (history_do_write (filename, nelements, HISTORY_APPEND));
}

/* Overwrite FILENAME with the current history.  If FILENAME is NULL,
   then write the history list to ~/.history.  Values returned
   are as in read_history ().*/
int
write_history (filename)
     const char *filename;
{
  return (history_do_write (filename, history_length, HISTORY_OVERWRITE));
}
