/* Part of CPP library.  File handling.
   Copyright (C) 1986, 1987, 1989, 1992, 1993, 1994, 1995, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
   Written by Per Bothner, 1994.
   Based on CCCP program by Paul Rubin, June 1986
   Adapted to ANSI C, Richard Stallman, Jan 1987
   Split out of cpplib.c, Zack Weinberg, Oct 1998
   Reimplemented, Neil Booth, Jul 2003

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
Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */

#include "config.h"
#include "system.h"
#include "cpplib.h"
#include "internal.h"
#include "mkdeps.h"
#include "hashtab.h"
#include "md5.h"
#include <dirent.h>

/* Variable length record files on VMS will have a stat size that includes
   record control characters that won't be included in the read size.  */
#ifdef VMS
# define FAB_C_VAR 2 /* variable length records (see Starlet fabdef.h) */
# define STAT_SIZE_RELIABLE(ST) ((ST).st_fab_rfm != FAB_C_VAR)
#else
# define STAT_SIZE_RELIABLE(ST) true
#endif

#ifdef __DJGPP__
#include <io.h>
  /* For DJGPP redirected input is opened in text mode.  */
#  define set_stdin_to_binary_mode() \
     if (! isatty (0)) setmode (0, O_BINARY)
#else
#  define set_stdin_to_binary_mode() /* Nothing */
#endif

/* This structure represents a file searched for by CPP, whether it
   exists or not.  An instance may be pointed to by more than one
   file_hash_entry; at present no reference count is kept.  */
struct _cpp_file
{
  /* Filename as given to #include or command line switch.  */
  const char *name;

  /* The full path used to find the file.  */
  const char *path;

  /* The full path of the pch file.  */
  const char *pchname;

  /* The file's path with the basename stripped.  NULL if it hasn't
     been calculated yet.  */
  const char *dir_name;

  /* Chain through all files.  */
  struct _cpp_file *next_file;

  /* The contents of NAME after calling read_file().  */
  const uchar *buffer;

  /* The macro, if any, preventing re-inclusion.  */
  const cpp_hashnode *cmacro;

  /* The directory in the search path where FILE was found.  Used for
     #include_next and determining whether a header is a system
     header.  */
  cpp_dir *dir;

  /* As filled in by stat(2) for the file.  */
  struct stat st;

  /* File descriptor.  Invalid if -1, otherwise open.  */
  int fd;

  /* Zero if this file was successfully opened and stat()-ed,
     otherwise errno obtained from failure.  */
  int err_no;

  /* Number of times the file has been stacked for preprocessing.  */
  unsigned short stack_count;

  /* If opened with #import or contains #pragma once.  */
  bool once_only;

  /* If read() failed before.  */
  bool dont_read;

  /* If this file is the main file.  */
  bool main_file;

  /* If BUFFER above contains the true contents of the file.  */
  bool buffer_valid;

  /* File is a PCH (on return from find_include_file).  */
  bool pch;
};

/* A singly-linked list for all searches for a given file name, with
   its head pointed to by a slot in FILE_HASH.  The file name is what
   appeared between the quotes in a #include directive; it can be
   determined implicitly from the hash table location or explicitly
   from FILE->name.

   FILE is a structure containing details about the file that was
   found with that search, or details of how the search failed.

   START_DIR is the starting location of the search in the include
   chain.  The current directories for "" includes are also hashed in
   the hash table and therefore unique.  Files that are looked up
   without using a search path, such as absolute filenames and file
   names from the command line share a special starting directory so
   they don't cause cache hits with normal include-chain lookups.

   If START_DIR is NULL then the entry is for a directory, not a file,
   and the directory is in DIR.  Since the starting point in a file
   lookup chain is never NULL, this means that simple pointer
   comparisons against START_DIR can be made to determine cache hits
   in file lookups.

   If a cache lookup fails because of e.g. an extra "./" in the path,
   then nothing will break.  It is just less efficient as CPP will
   have to do more work re-preprocessing the file, and/or comparing
   its contents against earlier once-only files.
*/
struct file_hash_entry
{
  struct file_hash_entry *next;
  cpp_dir *start_dir;
  union
  {
    _cpp_file *file;
    cpp_dir *dir;
  } u;
};

static bool open_file (_cpp_file *file);
static bool pch_open_file (cpp_reader *pfile, _cpp_file *file,
			   bool *invalid_pch);
static bool find_file_in_dir (cpp_reader *pfile, _cpp_file *file,
			      bool *invalid_pch);
static bool read_file_guts (cpp_reader *pfile, _cpp_file *file);
static bool read_file (cpp_reader *pfile, _cpp_file *file);
static bool should_stack_file (cpp_reader *, _cpp_file *file, bool import);
static struct cpp_dir *search_path_head (cpp_reader *, const char *fname,
				 int angle_brackets, enum include_type);
static const char *dir_name_of_file (_cpp_file *file);
static void open_file_failed (cpp_reader *pfile, _cpp_file *file, int);
static struct file_hash_entry *search_cache (struct file_hash_entry *head,
					     const cpp_dir *start_dir);
static _cpp_file *make_cpp_file (cpp_reader *, cpp_dir *, const char *fname);
static void destroy_cpp_file (_cpp_file *);
static cpp_dir *make_cpp_dir (cpp_reader *, const char *dir_name, int sysp);
static void allocate_file_hash_entries (cpp_reader *pfile);
static struct file_hash_entry *new_file_hash_entry (cpp_reader *pfile);
static int report_missing_guard (void **slot, void *b);
static hashval_t file_hash_hash (const void *p);
static int file_hash_eq (const void *p, const void *q);
static char *read_filename_string (int ch, FILE *f);
static void read_name_map (cpp_dir *dir);
static char *remap_filename (cpp_reader *pfile, _cpp_file *file);
static char *append_file_to_dir (const char *fname, cpp_dir *dir);
static bool validate_pch (cpp_reader *, _cpp_file *file, const char *pchname);
static int pchf_save_compare (const void *e1, const void *e2);
static int pchf_compare (const void *d_p, const void *e_p);
static bool check_file_against_entries (cpp_reader *, _cpp_file *, bool);

/* Given a filename in FILE->PATH, with the empty string interpreted
   as <stdin>, open it.

   On success FILE contains an open file descriptor and stat
   information for the file.  On failure the file descriptor is -1 and
   the appropriate errno is also stored in FILE.  Returns TRUE iff
   successful.

   We used to open files in nonblocking mode, but that caused more
   problems than it solved.  Do take care not to acquire a controlling
   terminal by mistake (this can't happen on sane systems, but
   paranoia is a virtue).

   Use the three-argument form of open even though we aren't
   specifying O_CREAT, to defend against broken system headers.

   O_BINARY tells some runtime libraries (notably DJGPP) not to do
   newline translation; we can handle DOS line breaks just fine
   ourselves.  */
static bool
open_file (_cpp_file *file)
{
  if (file->path[0] == '\0')
    {
      file->fd = 0;
      set_stdin_to_binary_mode ();
    }
  else
    file->fd = open (file->path, O_RDONLY | O_NOCTTY | O_BINARY, 0666);

  if (file->fd != -1)
    {
      if (fstat (file->fd, &file->st) == 0)
	{
	  if (!S_ISDIR (file->st.st_mode))
	    {
	      file->err_no = 0;
	      return true;
	    }

	  /* Ignore a directory and continue the search.  The file we're
	     looking for may be elsewhere in the search path.  */
	  errno = ENOENT;
	}

      close (file->fd);
      file->fd = -1;
    }
  else if (errno == ENOTDIR)
    errno = ENOENT;

  file->err_no = errno;

  return false;
}

/* Temporary PCH intercept of opening a file.  Try to find a PCH file
   based on FILE->name and FILE->dir, and test those found for
   validity using PFILE->cb.valid_pch.  Return true iff a valid file is
   found.  Set *INVALID_PCH if a PCH file is found but wasn't valid.  */

static bool
pch_open_file (cpp_reader *pfile, _cpp_file *file, bool *invalid_pch)
{
  static const char extension[] = ".gch";
  const char *path = file->path;
  size_t len, flen;
  char *pchname;
  struct stat st;
  bool valid = false;

  /* No PCH on <stdin> or if not requested.  */
  if (file->name[0] == '\0' || !pfile->cb.valid_pch)
    return false;

  flen = strlen (path);
  len = flen + sizeof (extension);
  pchname = XNEWVEC (char, len);
  memcpy (pchname, path, flen);
  memcpy (pchname + flen, extension, sizeof (extension));

  if (stat (pchname, &st) == 0)
    {
      DIR *pchdir;
      struct dirent *d;
      size_t dlen, plen = len;

      if (!S_ISDIR (st.st_mode))
	valid = validate_pch (pfile, file, pchname);
      else if ((pchdir = opendir (pchname)) != NULL)
	{
	  pchname[plen - 1] = '/';
	  while ((d = readdir (pchdir)) != NULL)
	    {
	      dlen = strlen (d->d_name) + 1;
	      if ((strcmp (d->d_name, ".") == 0)
		  || (strcmp (d->d_name, "..") == 0))
		continue;
	      if (dlen + plen > len)
		{
		  len += dlen + 64;
		  pchname = XRESIZEVEC (char, pchname, len);
		}
	      memcpy (pchname + plen, d->d_name, dlen);
	      valid = validate_pch (pfile, file, pchname);
	      if (valid)
		break;
	    }
	  closedir (pchdir);
	}
      if (valid)
	file->pch = true;
      else
	*invalid_pch = true;
    }

  if (valid)
    file->pchname = pchname;
  else
    free (pchname);

  return valid;
}

/* Try to open the path FILE->name appended to FILE->dir.  This is
   where remap and PCH intercept the file lookup process.  Return true
   if the file was found, whether or not the open was successful.
   Set *INVALID_PCH to true if a PCH file is found but wasn't valid.  */

static bool
find_file_in_dir (cpp_reader *pfile, _cpp_file *file, bool *invalid_pch)
{
  char *path;

  if (CPP_OPTION (pfile, remap) && (path = remap_filename (pfile, file)))
    ;
  else
    if (file->dir->construct)
      path = file->dir->construct (file->name, file->dir);
    else
      path = append_file_to_dir (file->name, file->dir);

  if (path)
    {
      file->path = path;
      if (pch_open_file (pfile, file, invalid_pch))
	return true;

      if (open_file (file))
	return true;

      if (file->err_no != ENOENT)
	{
	  open_file_failed (pfile, file, 0);
	  return true;
	}

      free (path);
      file->path = file->name;
    }
  else
    {
      file->err_no = ENOENT; 
      file->path = NULL;
    }

  return false;
}

/* Return tue iff the missing_header callback found the given HEADER.  */
static bool
search_path_exhausted (cpp_reader *pfile, const char *header, _cpp_file *file)
{
  missing_header_cb func = pfile->cb.missing_header;

  /* When the regular search path doesn't work, try context dependent
     headers search paths.  */
  if (func
      && file->dir == NULL)
    {
      if ((file->path = func (pfile, header, &file->dir)) != NULL)
	{
	  if (open_file (file))
	    return true;
	  free ((void *)file->path);
	}
      file->path = file->name;
    }

  return false;
}

bool
_cpp_find_failed (_cpp_file *file)
{
  return file->err_no != 0;
}

/* Given a filename FNAME search for such a file in the include path
   starting from START_DIR.  If FNAME is the empty string it is
   interpreted as STDIN if START_DIR is PFILE->no_search_path.

   If the file is not found in the file cache fall back to the O/S and
   add the result to our cache.

   If the file was not found in the filesystem, or there was an error
   opening it, then ERR_NO is nonzero and FD is -1.  If the file was
   found, then ERR_NO is zero and FD could be -1 or an open file
   descriptor.  FD can be -1 if the file was found in the cache and
   had previously been closed.  To open it again pass the return value
   to open_file().
*/
_cpp_file *
_cpp_find_file (cpp_reader *pfile, const char *fname, cpp_dir *start_dir, bool fake, int angle_brackets)
{
  struct file_hash_entry *entry, **hash_slot;
  _cpp_file *file;
  bool invalid_pch = false;

  /* Ensure we get no confusion between cached files and directories.  */
  if (start_dir == NULL)
    cpp_error (pfile, CPP_DL_ICE, "NULL directory in find_file");

  hash_slot = (struct file_hash_entry **)
    htab_find_slot_with_hash (pfile->file_hash, fname,
			      htab_hash_string (fname),
			      INSERT);

  /* First check the cache before we resort to memory allocation.  */
  entry = search_cache (*hash_slot, start_dir);
  if (entry)
    return entry->u.file;

  file = make_cpp_file (pfile, start_dir, fname);

  /* Try each path in the include chain.  */
  for (; !fake ;)
    {
      if (find_file_in_dir (pfile, file, &invalid_pch))
	break;

      file->dir = file->dir->next;
      if (file->dir == NULL)
	{
	  if (search_path_exhausted (pfile, fname, file))
	    {
	      /* Although this file must not go in the cache, because
		 the file found might depend on things (like the current file)
		 that aren't represented in the cache, it still has to go in
		 the list of all files so that #import works.  */
	      file->next_file = pfile->all_files;
	      pfile->all_files = file;
	      return file;
	    }

	  open_file_failed (pfile, file, angle_brackets);
	  if (invalid_pch)
	    {
	      cpp_error (pfile, CPP_DL_ERROR,
	       "one or more PCH files were found, but they were invalid");
	      if (!cpp_get_options (pfile)->warn_invalid_pch)
		cpp_error (pfile, CPP_DL_ERROR,
			   "use -Winvalid-pch for more information");
	    }
	  break;
	}

      /* Only check the cache for the starting location (done above)
	 and the quote and bracket chain heads because there are no
	 other possible starting points for searches.  */
      if (file->dir != pfile->bracket_include
	  && file->dir != pfile->quote_include)
	continue;

      entry = search_cache (*hash_slot, file->dir);
      if (entry)
	break;
    }

  if (entry)
    {
      /* Cache for START_DIR too, sharing the _cpp_file structure.  */
      free ((char *) file->name);
      free (file);
      file = entry->u.file;
    }
  else
    {
      /* This is a new file; put it in the list.  */
      file->next_file = pfile->all_files;
      pfile->all_files = file;
    }

  /* Store this new result in the hash table.  */
  entry = new_file_hash_entry (pfile);
  entry->next = *hash_slot;
  entry->start_dir = start_dir;
  entry->u.file = file;
  *hash_slot = entry;

  return file;
}

/* Read a file into FILE->buffer, returning true on success.

   If FILE->fd is something weird, like a block device, we don't want
   to read it at all.  Don't even try to figure out what something is,
   except for plain files and block devices, since there is no
   reliable portable way of doing this.

   FIXME: Flush file cache and try again if we run out of memory.  */
static bool
read_file_guts (cpp_reader *pfile, _cpp_file *file)
{
  ssize_t size, total, count;
  uchar *buf;
  bool regular;

  if (S_ISBLK (file->st.st_mode))
    {
      cpp_error (pfile, CPP_DL_ERROR, "%s is a block device", file->path);
      return false;
    }

  regular = S_ISREG (file->st.st_mode);
  if (regular)
    {
      /* off_t might have a wider range than ssize_t - in other words,
	 the max size of a file might be bigger than the address
	 space.  We can't handle a file that large.  (Anyone with
	 a single source file bigger than 2GB needs to rethink
	 their coding style.)  Some systems (e.g. AIX 4.1) define
	 SSIZE_MAX to be much smaller than the actual range of the
	 type.  Use INTTYPE_MAXIMUM unconditionally to ensure this
	 does not bite us.  */
      if (file->st.st_size > INTTYPE_MAXIMUM (ssize_t))
	{
	  cpp_error (pfile, CPP_DL_ERROR, "%s is too large", file->path);
	  return false;
	}

      size = file->st.st_size;
    }
  else
    /* 8 kilobytes is a sensible starting size.  It ought to be bigger
       than the kernel pipe buffer, and it's definitely bigger than
       the majority of C source files.  */
    size = 8 * 1024;

  buf = XNEWVEC (uchar, size + 1);
  total = 0;
  while ((count = read (file->fd, buf + total, size - total)) > 0)
    {
      total += count;

      if (total == size)
	{
	  if (regular)
	    break;
	  size *= 2;
	  buf = XRESIZEVEC (uchar, buf, size + 1);
	}
    }

  if (count < 0)
    {
      cpp_errno (pfile, CPP_DL_ERROR, file->path);
      return false;
    }

  if (regular && total != size && STAT_SIZE_RELIABLE (file->st))
    cpp_error (pfile, CPP_DL_WARNING,
	       "%s is shorter than expected", file->path);

  file->buffer = _cpp_convert_input (pfile, CPP_OPTION (pfile, input_charset),
				     buf, size, total, &file->st.st_size);
  file->buffer_valid = true;

  return true;
}

/* Convenience wrapper around read_file_guts that opens the file if
   necessary and closes the file descriptor after reading.  FILE must
   have been passed through find_file() at some stage.  */
static bool
read_file (cpp_reader *pfile, _cpp_file *file)
{
  /* If we already have its contents in memory, succeed immediately.  */
  if (file->buffer_valid)
    return true;

  /* If an earlier read failed for some reason don't try again.  */
  if (file->dont_read || file->err_no)
    return false;

  if (file->fd == -1 && !open_file (file))
    {
      open_file_failed (pfile, file, 0);
      return false;
    }

  file->dont_read = !read_file_guts (pfile, file);
  close (file->fd);
  file->fd = -1;

  return !file->dont_read;
}

/* Returns TRUE if FILE's contents have been successfully placed in
   FILE->buffer and the file should be stacked, otherwise false.  */
static bool
should_stack_file (cpp_reader *pfile, _cpp_file *file, bool import)
{
  _cpp_file *f;

  /* Skip once-only files.  */
  if (file->once_only)
    return false;

  /* We must mark the file once-only if #import now, before header
     guard checks.  Otherwise, undefining the header guard might
     cause the file to be re-stacked.  */
  if (import)
    {
      _cpp_mark_file_once_only (pfile, file);

      /* Don't stack files that have been stacked before.  */
      if (file->stack_count)
	return false;
    }

  /* Skip if the file had a header guard and the macro is defined.
     PCH relies on this appearing before the PCH handler below.  */
  if (file->cmacro && file->cmacro->type == NT_MACRO)
    return false;

  /* Handle PCH files immediately; don't stack them.  */
  if (file->pch)
    {
      pfile->cb.read_pch (pfile, file->pchname, file->fd, file->path);
      close (file->fd);
      file->fd = -1;
      return false;
    }

  if (!read_file (pfile, file))
    return false;

  /* Check the file against the PCH file.  This is done before
     checking against files we've already seen, since it may save on
     I/O.  */
  if (check_file_against_entries (pfile, file, import))
    {
      /* If this isn't a #import, but yet we can't include the file,
	 that means that it was #import-ed in the PCH file,
	 so we can never include it again.  */
      if (! import)
	_cpp_mark_file_once_only (pfile, file);
      return false;
    }

  /* Now we've read the file's contents, we can stack it if there
     are no once-only files.  */
  if (!pfile->seen_once_only)
    return true;

  /* We may have read the file under a different name.  Look
     for likely candidates and compare file contents to be sure.  */
  for (f = pfile->all_files; f; f = f->next_file)
    {
      if (f == file)
	continue;

      if ((import || f->once_only)
	  && f->err_no == 0
	  && f->st.st_mtime == file->st.st_mtime
	  && f->st.st_size == file->st.st_size)
	{
	  _cpp_file *ref_file;
	  bool same_file_p = false;

	  if (f->buffer && !f->buffer_valid)
	    {
	      /* We already have a buffer but it is not valid, because
		 the file is still stacked.  Make a new one.  */
	      ref_file = make_cpp_file (pfile, f->dir, f->name);
	      ref_file->path = f->path;
	    }
	  else
	    /* The file is not stacked anymore.  We can reuse it.  */
	    ref_file = f;

	  same_file_p = read_file (pfile, ref_file)
			/* Size might have changed in read_file().  */
			&& ref_file->st.st_size == file->st.st_size
			&& !memcmp (ref_file->buffer,
				    file->buffer,
				    file->st.st_size);

	  if (f->buffer && !f->buffer_valid)
	    {
	      ref_file->path = 0;
	      destroy_cpp_file (ref_file);
	    }

	  if (same_file_p)
	    break;
	}
    }

  return f == NULL;
}

/* Place the file referenced by FILE into a new buffer on the buffer
   stack if possible.  IMPORT is true if this stacking attempt is
   because of a #import directive.  Returns true if a buffer is
   stacked.  */
bool
_cpp_stack_file (cpp_reader *pfile, _cpp_file *file, bool import)
{
  cpp_buffer *buffer;
  int sysp;

  if (!should_stack_file (pfile, file, import))
      return false;

  if (pfile->buffer == NULL || file->dir == NULL)
    sysp = 0;
  else
    sysp = MAX (pfile->buffer->sysp,  file->dir->sysp);

  /* Add the file to the dependencies on its first inclusion.  */
  if (CPP_OPTION (pfile, deps.style) > !!sysp && !file->stack_count)
    {
      if (!file->main_file || !CPP_OPTION (pfile, deps.ignore_main_file))
	deps_add_dep (pfile->deps, file->path);
    }

  /* Clear buffer_valid since _cpp_clean_line messes it up.  */
  file->buffer_valid = false;
  file->stack_count++;

  /* Stack the buffer.  */
  buffer = cpp_push_buffer (pfile, file->buffer, file->st.st_size,
			    CPP_OPTION (pfile, preprocessed));
  buffer->file = file;
  buffer->sysp = sysp;

  /* Initialize controlling macro state.  */
  pfile->mi_valid = true;
  pfile->mi_cmacro = 0;

  /* Generate the call back.  */
  _cpp_do_file_change (pfile, LC_ENTER, file->path, 1, sysp);

  return true;
}

/* Mark FILE to be included once only.  */
void
_cpp_mark_file_once_only (cpp_reader *pfile, _cpp_file *file)
{
  pfile->seen_once_only = true;
  file->once_only = true;
}

/* Return the directory from which searching for FNAME should start,
   considering the directive TYPE and ANGLE_BRACKETS.  If there is
   nothing left in the path, returns NULL.  */
static struct cpp_dir *
search_path_head (cpp_reader *pfile, const char *fname, int angle_brackets,
		  enum include_type type)
{
  cpp_dir *dir;
  _cpp_file *file;

  if (IS_ABSOLUTE_PATH (fname))
    return &pfile->no_search_path;

  /* pfile->buffer is NULL when processing an -include command-line flag.  */
  file = pfile->buffer == NULL ? pfile->main_file : pfile->buffer->file;

  /* For #include_next, skip in the search path past the dir in which
     the current file was found, but if it was found via an absolute
     path use the normal search logic.  */
  if (type == IT_INCLUDE_NEXT && file->dir)
    dir = file->dir->next;
  else if (angle_brackets)
    dir = pfile->bracket_include;
  else if (type == IT_CMDLINE)
    /* -include and -imacros use the #include "" chain with the
       preprocessor's cwd prepended.  */
    return make_cpp_dir (pfile, "./", false);
  else if (pfile->quote_ignores_source_dir)
    dir = pfile->quote_include;
  else
    return make_cpp_dir (pfile, dir_name_of_file (file),
			 pfile->buffer ? pfile->buffer->sysp : 0);

  if (dir == NULL)
    cpp_error (pfile, CPP_DL_ERROR,
	       "no include path in which to search for %s", fname);

  return dir;
}

/* Strip the basename from the file's path.  It ends with a slash if
   of nonzero length.  Note that this procedure also works for
   <stdin>, which is represented by the empty string.  */
static const char *
dir_name_of_file (_cpp_file *file)
{
  if (!file->dir_name)
    {
      size_t len = lbasename (file->path) - file->path;
      char *dir_name = XNEWVEC (char, len + 1);

      memcpy (dir_name, file->path, len);
      dir_name[len] = '\0';
      file->dir_name = dir_name;
    }

  return file->dir_name;
}

/* Handles #include-family directives (distinguished by TYPE),
   including HEADER, and the command line -imacros and -include.
   Returns true if a buffer was stacked.  */
bool
_cpp_stack_include (cpp_reader *pfile, const char *fname, int angle_brackets,
		    enum include_type type)
{
  struct cpp_dir *dir;
  _cpp_file *file;

  dir = search_path_head (pfile, fname, angle_brackets, type);
  if (!dir)
    return false;

  file = _cpp_find_file (pfile, fname, dir, false, angle_brackets);

  /* Compensate for the increment in linemap_add.  In the case of a
     normal #include, we're currently at the start of the line
     *following* the #include.  A separate source_location for this
     location makes no sense (until we do the LC_LEAVE), and
     complicates LAST_SOURCE_LINE_LOCATION.  This does not apply if we
     found a PCH file (in which case linemap_add is not called) or we
     were included from the command-line.  */
  if (! file->pch && file->err_no == 0 && type != IT_CMDLINE)
    pfile->line_table->highest_location--;

  return _cpp_stack_file (pfile, file, type == IT_IMPORT);
}

/* Could not open FILE.  The complication is dependency output.  */
static void
open_file_failed (cpp_reader *pfile, _cpp_file *file, int angle_brackets)
{
  int sysp = pfile->line_table->highest_line > 1 && pfile->buffer ? pfile->buffer->sysp : 0;
  bool print_dep = CPP_OPTION (pfile, deps.style) > (angle_brackets || !!sysp);

  errno = file->err_no;
  if (print_dep && CPP_OPTION (pfile, deps.missing_files) && errno == ENOENT)
    deps_add_dep (pfile->deps, file->name);
  else
    {
      /* If we are outputting dependencies but not for this file then
	 don't error because we can still produce correct output.  */
      if (CPP_OPTION (pfile, deps.style) && ! print_dep)
	cpp_errno (pfile, CPP_DL_WARNING, file->path);
      else
	cpp_errno (pfile, CPP_DL_ERROR, file->path);
    }
}

/* Search in the chain beginning at HEAD for a file whose search path
   started at START_DIR != NULL.  */
static struct file_hash_entry *
search_cache (struct file_hash_entry *head, const cpp_dir *start_dir)
{
  while (head && head->start_dir != start_dir)
    head = head->next;

  return head;
}

/* Allocate a new _cpp_file structure.  */
static _cpp_file *
make_cpp_file (cpp_reader *pfile, cpp_dir *dir, const char *fname)
{
  _cpp_file *file;

  file = XCNEW (_cpp_file);
  file->main_file = !pfile->buffer;
  file->fd = -1;
  file->dir = dir;
  file->name = xstrdup (fname);

  return file;
}

/* Release a _cpp_file structure.  */
static void
destroy_cpp_file (_cpp_file *file)
{
  if (file->buffer)
    free ((void *) file->buffer);
  free ((void *) file->name);
  free (file);
}

/* A hash of directory names.  The directory names are the path names
   of files which contain a #include "", the included file name is
   appended to this directories.

   To avoid duplicate entries we follow the convention that all
   non-empty directory names should end in a '/'.  DIR_NAME must be
   stored in permanently allocated memory.  */
static cpp_dir *
make_cpp_dir (cpp_reader *pfile, const char *dir_name, int sysp)
{
  struct file_hash_entry *entry, **hash_slot;
  cpp_dir *dir;

  hash_slot = (struct file_hash_entry **)
    htab_find_slot_with_hash (pfile->dir_hash, dir_name,
			      htab_hash_string (dir_name),
			      INSERT);

  /* Have we already hashed this directory?  */
  for (entry = *hash_slot; entry; entry = entry->next)
    if (entry->start_dir == NULL)
      return entry->u.dir;

  dir = XCNEW (cpp_dir);
  dir->next = pfile->quote_include;
  dir->name = (char *) dir_name;
  dir->len = strlen (dir_name);
  dir->sysp = sysp;
  dir->construct = 0;

  /* Store this new result in the hash table.  */
  entry = new_file_hash_entry (pfile);
  entry->next = *hash_slot;
  entry->start_dir = NULL;
  entry->u.dir = dir;
  *hash_slot = entry;

  return dir;
}

/* Create a new block of memory for file hash entries.  */
static void
allocate_file_hash_entries (cpp_reader *pfile)
{
  pfile->file_hash_entries_used = 0;
  pfile->file_hash_entries_allocated = 127;
  pfile->file_hash_entries = XNEWVEC (struct file_hash_entry,
                                      pfile->file_hash_entries_allocated);
}

/* Return a new file hash entry.  */
static struct file_hash_entry *
new_file_hash_entry (cpp_reader *pfile)
{
  if (pfile->file_hash_entries_used == pfile->file_hash_entries_allocated)
    allocate_file_hash_entries (pfile);

  return &pfile->file_hash_entries[pfile->file_hash_entries_used++];
}

/* Returns TRUE if a file FNAME has ever been successfully opened.
   This routine is not intended to correctly handle filenames aliased
   by links or redundant . or .. traversals etc.  */
bool
cpp_included (cpp_reader *pfile, const char *fname)
{
  struct file_hash_entry *entry;

  entry = (struct file_hash_entry *)
     htab_find_with_hash (pfile->file_hash, fname, htab_hash_string (fname));

  while (entry && (entry->start_dir == NULL || entry->u.file->err_no))
    entry = entry->next;

  return entry != NULL;
}

/* Calculate the hash value of a file hash entry P.  */

static hashval_t
file_hash_hash (const void *p)
{
  struct file_hash_entry *entry = (struct file_hash_entry *) p;
  const char *hname;
  if (entry->start_dir)
    hname = entry->u.file->name;
  else
    hname = entry->u.dir->name;

  return htab_hash_string (hname);
}

/* Compare a string Q against a file hash entry P.  */
static int
file_hash_eq (const void *p, const void *q)
{
  struct file_hash_entry *entry = (struct file_hash_entry *) p;
  const char *fname = (const char *) q;
  const char *hname;

  if (entry->start_dir)
    hname = entry->u.file->name;
  else
    hname = entry->u.dir->name;

  return strcmp (hname, fname) == 0;
}

/* Initialize everything in this source file.  */
void
_cpp_init_files (cpp_reader *pfile)
{
  pfile->file_hash = htab_create_alloc (127, file_hash_hash, file_hash_eq,
					NULL, xcalloc, free);
  pfile->dir_hash = htab_create_alloc (127, file_hash_hash, file_hash_eq,
					NULL, xcalloc, free);
  allocate_file_hash_entries (pfile);
}

/* Finalize everything in this source file.  */
void
_cpp_cleanup_files (cpp_reader *pfile)
{
  htab_delete (pfile->file_hash);
  htab_delete (pfile->dir_hash);
}

/* Enter a file name in the hash for the sake of cpp_included.  */
void
_cpp_fake_include (cpp_reader *pfile, const char *fname)
{
  _cpp_find_file (pfile, fname, pfile->buffer->file->dir, true, 0);
}

/* Not everyone who wants to set system-header-ness on a buffer can
   see the details of a buffer.  This is an exported interface because
   fix-header needs it.  */
void
cpp_make_system_header (cpp_reader *pfile, int syshdr, int externc)
{
  int flags = 0;
  const struct line_maps *line_table = pfile->line_table;
  const struct line_map *map = &line_table->maps[line_table->used-1];

  /* 1 = system header, 2 = system header to be treated as C.  */
  if (syshdr)
    flags = 1 + (externc != 0);
  pfile->buffer->sysp = flags;
  _cpp_do_file_change (pfile, LC_RENAME, map->to_file,
		       SOURCE_LINE (map, pfile->line_table->highest_line), flags);
}

/* Allow the client to change the current file.  Used by the front end
   to achieve pseudo-file names like <built-in>.
   If REASON is LC_LEAVE, then NEW_NAME must be NULL.  */
void
cpp_change_file (cpp_reader *pfile, enum lc_reason reason,
		 const char *new_name)
{
  _cpp_do_file_change (pfile, reason, new_name, 1, 0);
}

/* Callback function for htab_traverse.  */
static int
report_missing_guard (void **slot, void *b)
{
  struct file_hash_entry *entry = (struct file_hash_entry *) *slot;
  int *bannerp = (int *) b;

  /* Skip directories.  */
  if (entry->start_dir != NULL)
    {
      _cpp_file *file = entry->u.file;

      /* We don't want MI guard advice for the main file.  */
      if (file->cmacro == NULL && file->stack_count == 1 && !file->main_file)
	{
	  if (*bannerp == 0)
	    {
	      fputs (_("Multiple include guards may be useful for:\n"),
		     stderr);
	      *bannerp = 1;
	    }

	  fputs (entry->u.file->path, stderr);
	  putc ('\n', stderr);
	}
    }

  return 0;
}

/* Report on all files that might benefit from a multiple include guard.
   Triggered by -H.  */
void
_cpp_report_missing_guards (cpp_reader *pfile)
{
  int banner = 0;

  htab_traverse (pfile->file_hash, report_missing_guard, &banner);
}

/* Locate HEADER, and determine whether it is newer than the current
   file.  If it cannot be located or dated, return -1, if it is
   newer, return 1, otherwise 0.  */
int
_cpp_compare_file_date (cpp_reader *pfile, const char *fname,
			int angle_brackets)
{
  _cpp_file *file;
  struct cpp_dir *dir;

  dir = search_path_head (pfile, fname, angle_brackets, IT_INCLUDE);
  if (!dir)
    return -1;

  file = _cpp_find_file (pfile, fname, dir, false, angle_brackets);
  if (file->err_no)
    return -1;

  if (file->fd != -1)
    {
      close (file->fd);
      file->fd = -1;
    }

  return file->st.st_mtime > pfile->buffer->file->st.st_mtime;
}

/* Pushes the given file onto the buffer stack.  Returns nonzero if
   successful.  */
bool
cpp_push_include (cpp_reader *pfile, const char *fname)
{
  return _cpp_stack_include (pfile, fname, false, IT_CMDLINE);
}

/* Do appropriate cleanup when a file INC's buffer is popped off the
   input stack.  */
void
_cpp_pop_file_buffer (cpp_reader *pfile, _cpp_file *file)
{
  /* Record the inclusion-preventing macro, which could be NULL
     meaning no controlling macro.  */
  if (pfile->mi_valid && file->cmacro == NULL)
    file->cmacro = pfile->mi_cmacro;

  /* Invalidate control macros in the #including file.  */
  pfile->mi_valid = false;

  if (file->buffer)
    {
      free ((void *) file->buffer);
      file->buffer = NULL;
      file->buffer_valid = false;
    }
}

/* Inteface to file statistics record in _cpp_file structure. */
struct stat *
_cpp_get_file_stat (_cpp_file *file)
{
    return &file->st;
}

/* Set the include chain for "" to QUOTE, for <> to BRACKET.  If
   QUOTE_IGNORES_SOURCE_DIR, then "" includes do not look in the
   directory of the including file.

   If BRACKET does not lie in the QUOTE chain, it is set to QUOTE.  */
void
cpp_set_include_chains (cpp_reader *pfile, cpp_dir *quote, cpp_dir *bracket,
			int quote_ignores_source_dir)
{
  pfile->quote_include = quote;
  pfile->bracket_include = quote;
  pfile->quote_ignores_source_dir = quote_ignores_source_dir;

  for (; quote; quote = quote->next)
    {
      quote->name_map = NULL;
      quote->len = strlen (quote->name);
      if (quote == bracket)
	pfile->bracket_include = bracket;
    }
}

/* Append the file name to the directory to create the path, but don't
   turn / into // or // into ///; // may be a namespace escape.  */
static char *
append_file_to_dir (const char *fname, cpp_dir *dir)
{
  size_t dlen, flen;
  char *path;

  dlen = dir->len;
  flen = strlen (fname);
  path = XNEWVEC (char, dlen + 1 + flen + 1);
  memcpy (path, dir->name, dlen);
  if (dlen && path[dlen - 1] != '/')
    path[dlen++] = '/';
  memcpy (&path[dlen], fname, flen + 1);

  return path;
}

/* Read a space delimited string of unlimited length from a stdio
   file F.  */
static char *
read_filename_string (int ch, FILE *f)
{
  char *alloc, *set;
  int len;

  len = 20;
  set = alloc = XNEWVEC (char, len + 1);
  if (! is_space (ch))
    {
      *set++ = ch;
      while ((ch = getc (f)) != EOF && ! is_space (ch))
	{
	  if (set - alloc == len)
	    {
	      len *= 2;
	      alloc = XRESIZEVEC (char, alloc, len + 1);
	      set = alloc + len / 2;
	    }
	  *set++ = ch;
	}
    }
  *set = '\0';
  ungetc (ch, f);
  return alloc;
}

/* Read the file name map file for DIR.  */
static void
read_name_map (cpp_dir *dir)
{
  static const char FILE_NAME_MAP_FILE[] = "header.gcc";
  char *name;
  FILE *f;
  size_t len, count = 0, room = 9;

  len = dir->len;
  name = (char *) alloca (len + sizeof (FILE_NAME_MAP_FILE) + 1);
  memcpy (name, dir->name, len);
  if (len && name[len - 1] != '/')
    name[len++] = '/';
  strcpy (name + len, FILE_NAME_MAP_FILE);
  f = fopen (name, "r");

  dir->name_map = XNEWVEC (const char *, room);

  /* Silently return NULL if we cannot open.  */
  if (f)
    {
      int ch;

      while ((ch = getc (f)) != EOF)
	{
	  char *to;

	  if (is_space (ch))
	    continue;

	  if (count + 2 > room)
	    {
	      room += 8;
	      dir->name_map = XRESIZEVEC (const char *, dir->name_map, room);
	    }

	  dir->name_map[count] = read_filename_string (ch, f);
	  while ((ch = getc (f)) != EOF && is_hspace (ch))
	    ;

	  to = read_filename_string (ch, f);
	  if (IS_ABSOLUTE_PATH (to))
	    dir->name_map[count + 1] = to;
	  else
	    {
	      dir->name_map[count + 1] = append_file_to_dir (to, dir);
	      free (to);
	    }

	  count += 2;
	  while ((ch = getc (f)) != '\n')
	    if (ch == EOF)
	      break;
	}

      fclose (f);
    }

  /* Terminate the list of maps.  */
  dir->name_map[count] = NULL;
}

/* Remap a FILE's name based on the file_name_map, if any, for
   FILE->dir.  If the file name has any directory separators,
   recursively check those directories too.  */
static char *
remap_filename (cpp_reader *pfile, _cpp_file *file)
{
  const char *fname, *p;
  char *new_dir;
  cpp_dir *dir;
  size_t index, len;

  dir = file->dir;
  fname = file->name;

  for (;;)
    {
      if (!dir->name_map)
	read_name_map (dir);

      for (index = 0; dir->name_map[index]; index += 2)
	if (!strcmp (dir->name_map[index], fname))
	    return xstrdup (dir->name_map[index + 1]);

      p = strchr (fname, '/');
      if (!p || p == fname)
	return NULL;

      len = dir->len + (p - fname + 1);
      new_dir = XNEWVEC (char, len + 1);
      memcpy (new_dir, dir->name, dir->len);
      memcpy (new_dir + dir->len, fname, p - fname + 1);
      new_dir[len] = '\0';

      dir = make_cpp_dir (pfile, new_dir, dir->sysp);
      fname = p + 1;
    }
}

/* Returns true if PCHNAME is a valid PCH file for FILE.  */
static bool
validate_pch (cpp_reader *pfile, _cpp_file *file, const char *pchname)
{
  const char *saved_path = file->path;
  bool valid = false;

  file->path = pchname;
  if (open_file (file))
    {
      valid = 1 & pfile->cb.valid_pch (pfile, pchname, file->fd);

      if (!valid)
	{
	  close (file->fd);
	  file->fd = -1;
	}

      if (CPP_OPTION (pfile, print_include_names))
	{
	  unsigned int i;
	  for (i = 1; i < pfile->line_table->depth; i++)
	    putc ('.', stderr);
	  fprintf (stderr, "%c %s\n",
		   valid ? '!' : 'x', pchname);
	}
    }

  file->path = saved_path;
  return valid;
}

/* Get the path associated with the _cpp_file F.  The path includes
   the base name from the include directive and the directory it was
   found in via the search path.  */

const char *
cpp_get_path (struct _cpp_file *f)
{
  return f->path;
}

/* Get the directory associated with the _cpp_file F.  */

cpp_dir *
cpp_get_dir (struct _cpp_file *f)
{
  return f->dir;
}

/* Get the cpp_buffer currently associated with the cpp_reader
   PFILE.  */

cpp_buffer *
cpp_get_buffer (cpp_reader *pfile)
{
  return pfile->buffer;
}

/* Get the _cpp_file associated with the cpp_buffer B.  */

_cpp_file *
cpp_get_file (cpp_buffer *b)
{
  return b->file;
}

/* Get the previous cpp_buffer given a cpp_buffer B.  The previous
   buffer is the buffer that included the given buffer.  */

cpp_buffer *
cpp_get_prev (cpp_buffer *b)
{
  return b->prev;
}

/* This data structure holds the list of header files that were seen
   while the PCH was being built.  The 'entries' field is kept sorted
   in memcmp() order; yes, this means that on little-endian systems,
   it's sorted initially by the least-significant byte of 'size', but
   that's OK.  The code does rely on having entries with the same size
   next to each other.  */

struct pchf_entry {
  /* The size of this file.  This is used to save running a MD5 checksum
     if the sizes don't match.  */
  off_t size;
  /* The MD5 checksum of this file.  */
  unsigned char sum[16];
  /* Is this file to be included only once?  */
  bool once_only;
};

struct pchf_data {
  /* Number of pchf_entry structures.  */
  size_t count;

  /* Are there any values with once_only set?
     This is used as an optimisation, it means we don't have to search
     the structure if we're processing a regular #include.  */
  bool have_once_only;

  struct pchf_entry entries[1];
};

static struct pchf_data *pchf;

/* A qsort ordering function for pchf_entry structures.  */

static int
pchf_save_compare (const void *e1, const void *e2)
{
  return memcmp (e1, e2, sizeof (struct pchf_entry));
}

/* Create and write to F a pchf_data structure.  */

bool
_cpp_save_file_entries (cpp_reader *pfile, FILE *fp)
{
  size_t count = 0;
  struct pchf_data *result;
  size_t result_size;
  _cpp_file *f;

  for (f = pfile->all_files; f; f = f->next_file)
    ++count;

  result_size = (sizeof (struct pchf_data)
		 + sizeof (struct pchf_entry) * (count - 1));
  result = XCNEWVAR (struct pchf_data, result_size);

  result->count = 0;
  result->have_once_only = false;

  for (f = pfile->all_files; f; f = f->next_file)
    {
      size_t count;

      /* This should probably never happen, since if a read error occurred
	 the PCH file shouldn't be written...  */
      if (f->dont_read || f->err_no)
	continue;

      if (f->stack_count == 0)
	continue;

      count = result->count++;

      result->entries[count].once_only = f->once_only;
      /* |= is avoided in the next line because of an HP C compiler bug */
      result->have_once_only = result->have_once_only | f->once_only;
      if (f->buffer_valid)
	md5_buffer ((const char *)f->buffer,
		    f->st.st_size, result->entries[count].sum);
      else
	{
	  FILE *ff;
	  int oldfd = f->fd;

	  if (!open_file (f))
	    {
	      open_file_failed (pfile, f, 0);
	      return false;
	    }
	  ff = fdopen (f->fd, "rb");
	  md5_stream (ff, result->entries[count].sum);
	  fclose (ff);
	  f->fd = oldfd;
	}
      result->entries[count].size = f->st.st_size;
    }

  result_size = (sizeof (struct pchf_data)
                 + sizeof (struct pchf_entry) * (result->count - 1));

  qsort (result->entries, result->count, sizeof (struct pchf_entry),
	 pchf_save_compare);

  return fwrite (result, result_size, 1, fp) == 1;
}

/* Read the pchf_data structure from F.  */

bool
_cpp_read_file_entries (cpp_reader *pfile ATTRIBUTE_UNUSED, FILE *f)
{
  struct pchf_data d;

  if (fread (&d, sizeof (struct pchf_data) - sizeof (struct pchf_entry), 1, f)
       != 1)
    return false;

  pchf = XNEWVAR (struct pchf_data, sizeof (struct pchf_data)
		  + sizeof (struct pchf_entry) * (d.count - 1));
  memcpy (pchf, &d, sizeof (struct pchf_data) - sizeof (struct pchf_entry));
  if (fread (pchf->entries, sizeof (struct pchf_entry), d.count, f)
      != d.count)
    return false;
  return true;
}

/* The parameters for pchf_compare.  */

struct pchf_compare_data
{
  /* The size of the file we're looking for.  */
  off_t size;

  /* The MD5 checksum of the file, if it's been computed.  */
  unsigned char sum[16];

  /* Is SUM valid?  */
  bool sum_computed;

  /* Do we need to worry about entries that don't have ONCE_ONLY set?  */
  bool check_included;

  /* The file that we're searching for.  */
  _cpp_file *f;
};

/* bsearch comparison function; look for D_P in E_P.  */

static int
pchf_compare (const void *d_p, const void *e_p)
{
  const struct pchf_entry *e = (const struct pchf_entry *)e_p;
  struct pchf_compare_data *d = (struct pchf_compare_data *)d_p;
  int result;

  result = memcmp (&d->size, &e->size, sizeof (off_t));
  if (result != 0)
    return result;

  if (! d->sum_computed)
    {
      _cpp_file *const f = d->f;

      md5_buffer ((const char *)f->buffer, f->st.st_size, d->sum);
      d->sum_computed = true;
    }

  result = memcmp (d->sum, e->sum, 16);
  if (result != 0)
    return result;

  if (d->check_included || e->once_only)
    return 0;
  else
    return 1;
}

/* Check that F is not in a list read from a PCH file (if any).
   Assumes that f->buffer_valid is true.  Return TRUE if the file
   should not be read.  */

static bool
check_file_against_entries (cpp_reader *pfile ATTRIBUTE_UNUSED,
			    _cpp_file *f,
			    bool check_included)
{
  struct pchf_compare_data d;

  if (pchf == NULL
      || (! check_included && ! pchf->have_once_only))
    return false;

  d.size = f->st.st_size;
  d.sum_computed = false;
  d.f = f;
  d.check_included = check_included;
  return bsearch (&d, pchf->entries, pchf->count, sizeof (struct pchf_entry),
		  pchf_compare) != NULL;
}
