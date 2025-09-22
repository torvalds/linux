/* complete.c -- filename completion for readline. */

/* Copyright (C) 1987, 1989, 1992 Free Software Foundation, Inc.

   This file is part of the GNU Readline Library, a library for
   reading lines of text with interactive input and history editing.

   The GNU Readline Library is free software; you can redistribute it
   and/or modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2, or
   (at your option) any later version.

   The GNU Readline Library is distributed in the hope that it will be
   useful, but WITHOUT ANY WARRANTY; without even the implied warranty
   of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   The GNU General Public License is often shipped with GNU software, and
   is generally kept in a file called COPYING or LICENSE.  If you do not
   have a copy of the license, write to the Free Software Foundation,
   59 Temple Place, Suite 330, Boston, MA 02111 USA. */
#define READLINE_LIBRARY

#if defined (HAVE_CONFIG_H)
#  include <config.h>
#endif

#include <sys/types.h>
#include <fcntl.h>
#if defined (HAVE_SYS_FILE_H)
#include <sys/file.h>
#endif

#if defined (HAVE_UNISTD_H)
#  include <unistd.h>
#endif /* HAVE_UNISTD_H */

#if defined (HAVE_STDLIB_H)
#  include <stdlib.h>
#else
#  include "ansi_stdlib.h"
#endif /* HAVE_STDLIB_H */

#include <stdio.h>

#include <errno.h>
#if !defined (errno)
extern int errno;
#endif /* !errno */

#include <pwd.h>

#include "posixdir.h"
#include "posixstat.h"

/* System-specific feature definitions and include files. */
#include "rldefs.h"
#include "rlmbutil.h"

/* Some standard library routines. */
#include "readline.h"
#include "xmalloc.h"
#include "rlprivate.h"

#ifdef __STDC__
typedef int QSFUNC (const void *, const void *);
#else
typedef int QSFUNC ();
#endif

#ifdef HAVE_LSTAT
#  define LSTAT lstat
#else
#  define LSTAT stat
#endif

/* Unix version of a hidden file.  Could be different on other systems. */
#define HIDDEN_FILE(fname)	((fname)[0] == '.')

/* Most systems don't declare getpwent in <pwd.h> if _POSIX_SOURCE is
   defined. */
#if !defined (HAVE_GETPW_DECLS) || defined (_POSIX_SOURCE)
extern struct passwd *getpwent PARAMS((void));
#endif /* !HAVE_GETPW_DECLS || _POSIX_SOURCE */

/* If non-zero, then this is the address of a function to call when
   completing a word would normally display the list of possible matches.
   This function is called instead of actually doing the display.
   It takes three arguments: (char **matches, int num_matches, int max_length)
   where MATCHES is the array of strings that matched, NUM_MATCHES is the
   number of strings in that array, and MAX_LENGTH is the length of the
   longest string in that array. */
rl_compdisp_func_t *rl_completion_display_matches_hook = (rl_compdisp_func_t *)NULL;

#if defined (VISIBLE_STATS)
#  if !defined (X_OK)
#    define X_OK 1
#  endif
static int stat_char PARAMS((char *));
#endif

static char *rl_quote_filename PARAMS((char *, int, char *));

static void set_completion_defaults PARAMS((int));
static int get_y_or_n PARAMS((int));
static int _rl_internal_pager PARAMS((int));
static char *printable_part PARAMS((char *));
static int print_filename PARAMS((char *, char *));

static char **gen_completion_matches PARAMS((char *, int, int, rl_compentry_func_t *, int, int));

static char **remove_duplicate_matches PARAMS((char **));
static void insert_match PARAMS((char *, int, int, char *));
static int append_to_match PARAMS((char *, int, int, int));
static void insert_all_matches PARAMS((char **, int, char *));
static void display_matches PARAMS((char **));
static int compute_lcd_of_matches PARAMS((char **, int, const char *));
static int postprocess_matches PARAMS((char ***, int));

static char *make_quoted_replacement PARAMS((char *, int, char *));

/* **************************************************************** */
/*								    */
/*	Completion matching, from readline's point of view.	    */
/*								    */
/* **************************************************************** */

/* Variables known only to the readline library. */

/* If non-zero, non-unique completions always show the list of matches. */
int _rl_complete_show_all = 0;

/* If non-zero, completed directory names have a slash appended. */
int _rl_complete_mark_directories = 1;

/* If non-zero, the symlinked directory completion behavior introduced in
   readline-4.2a is disabled, and symlinks that point to directories have
   a slash appended (subject to the value of _rl_complete_mark_directories).
   This is user-settable via the mark-symlinked-directories variable. */
int _rl_complete_mark_symlink_dirs = 0;

/* If non-zero, completions are printed horizontally in alphabetical order,
   like `ls -x'. */
int _rl_print_completions_horizontally;

/* Non-zero means that case is not significant in filename completion. */
#if defined (__MSDOS__) && !defined (__DJGPP__)
int _rl_completion_case_fold = 1;
#else
int _rl_completion_case_fold;
#endif

/* If non-zero, don't match hidden files (filenames beginning with a `.' on
   Unix) when doing filename completion. */
int _rl_match_hidden_files = 1;

/* Global variables available to applications using readline. */

#if defined (VISIBLE_STATS)
/* Non-zero means add an additional character to each filename displayed
   during listing completion iff rl_filename_completion_desired which helps
   to indicate the type of file being listed. */
int rl_visible_stats = 0;
#endif /* VISIBLE_STATS */

/* If non-zero, then this is the address of a function to call when
   completing on a directory name.  The function is called with
   the address of a string (the current directory name) as an arg. */
rl_icppfunc_t *rl_directory_completion_hook = (rl_icppfunc_t *)NULL;

rl_icppfunc_t *rl_directory_rewrite_hook = (rl_icppfunc_t *)NULL;

/* Non-zero means readline completion functions perform tilde expansion. */
int rl_complete_with_tilde_expansion = 0;

/* Pointer to the generator function for completion_matches ().
   NULL means to use rl_filename_completion_function (), the default filename
   completer. */
rl_compentry_func_t *rl_completion_entry_function = (rl_compentry_func_t *)NULL;

/* Pointer to alternative function to create matches.
   Function is called with TEXT, START, and END.
   START and END are indices in RL_LINE_BUFFER saying what the boundaries
   of TEXT are.
   If this function exists and returns NULL then call the value of
   rl_completion_entry_function to try to match, otherwise use the
   array of strings returned. */
rl_completion_func_t *rl_attempted_completion_function = (rl_completion_func_t *)NULL;

/* Non-zero means to suppress normal filename completion after the
   user-specified completion function has been called. */
int rl_attempted_completion_over = 0;

/* Set to a character indicating the type of completion being performed
   by rl_complete_internal, available for use by application completion
   functions. */
int rl_completion_type = 0;

/* Up to this many items will be displayed in response to a
   possible-completions call.  After that, we ask the user if
   she is sure she wants to see them all. */
int rl_completion_query_items = 100;

int _rl_page_completions = 1;

/* The basic list of characters that signal a break between words for the
   completer routine.  The contents of this variable is what breaks words
   in the shell, i.e. " \t\n\"\\'`@$><=" */
const char *rl_basic_word_break_characters = " \t\n\"\\'`@$><=;|&{("; /* }) */

/* List of basic quoting characters. */
const char *rl_basic_quote_characters = "\"'";

/* The list of characters that signal a break between words for
   rl_complete_internal.  The default list is the contents of
   rl_basic_word_break_characters.  */
const char *rl_completer_word_break_characters = (const char *)NULL;

/* List of characters which can be used to quote a substring of the line.
   Completion occurs on the entire substring, and within the substring
   rl_completer_word_break_characters are treated as any other character,
   unless they also appear within this list. */
const char *rl_completer_quote_characters = (const char *)NULL;

/* List of characters that should be quoted in filenames by the completer. */
const char *rl_filename_quote_characters = (const char *)NULL;

/* List of characters that are word break characters, but should be left
   in TEXT when it is passed to the completion function.  The shell uses
   this to help determine what kind of completing to do. */
const char *rl_special_prefixes = (const char *)NULL;

/* If non-zero, then disallow duplicates in the matches. */
int rl_ignore_completion_duplicates = 1;

/* Non-zero means that the results of the matches are to be treated
   as filenames.  This is ALWAYS zero on entry, and can only be changed
   within a completion entry finder function. */
int rl_filename_completion_desired = 0;

/* Non-zero means that the results of the matches are to be quoted using
   double quotes (or an application-specific quoting mechanism) if the
   filename contains any characters in rl_filename_quote_chars.  This is
   ALWAYS non-zero on entry, and can only be changed within a completion
   entry finder function. */
int rl_filename_quoting_desired = 1;

/* This function, if defined, is called by the completer when real
   filename completion is done, after all the matching names have been
   generated. It is passed a (char**) known as matches in the code below.
   It consists of a NULL-terminated array of pointers to potential
   matching strings.  The 1st element (matches[0]) is the maximal
   substring that is common to all matches. This function can re-arrange
   the list of matches as required, but all elements of the array must be
   free()'d if they are deleted. The main intent of this function is
   to implement FIGNORE a la SunOS csh. */
rl_compignore_func_t *rl_ignore_some_completions_function = (rl_compignore_func_t *)NULL;

/* Set to a function to quote a filename in an application-specific fashion.
   Called with the text to quote, the type of match found (single or multiple)
   and a pointer to the quoting character to be used, which the function can
   reset if desired. */
rl_quote_func_t *rl_filename_quoting_function = rl_quote_filename;

/* Function to call to remove quoting characters from a filename.  Called
   before completion is attempted, so the embedded quotes do not interfere
   with matching names in the file system.  Readline doesn't do anything
   with this; it's set only by applications. */
rl_dequote_func_t *rl_filename_dequoting_function = (rl_dequote_func_t *)NULL;

/* Function to call to decide whether or not a word break character is
   quoted.  If a character is quoted, it does not break words for the
   completer. */
rl_linebuf_func_t *rl_char_is_quoted_p = (rl_linebuf_func_t *)NULL;

/* If non-zero, the completion functions don't append anything except a
   possible closing quote.  This is set to 0 by rl_complete_internal and
   may be changed by an application-specific completion function. */
int rl_completion_suppress_append = 0;

/* Character appended to completed words when at the end of the line.  The
   default is a space. */
int rl_completion_append_character = ' ';

/* If non-zero, a slash will be appended to completed filenames that are
   symbolic links to directory names, subject to the value of the
   mark-directories variable (which is user-settable).  This exists so
   that application completion functions can override the user's preference
   (set via the mark-symlinked-directories variable) if appropriate.
   It's set to the value of _rl_complete_mark_symlink_dirs in
   rl_complete_internal before any application-specific completion
   function is called, so without that function doing anything, the user's
   preferences are honored. */
int rl_completion_mark_symlink_dirs;

/* If non-zero, inhibit completion (temporarily). */
int rl_inhibit_completion;

/* Variables local to this file. */

/* Local variable states what happened during the last completion attempt. */
static int completion_changed_buffer;

/*************************************/
/*				     */
/*    Bindable completion functions  */
/*				     */
/*************************************/

/* Complete the word at or before point.  You have supplied the function
   that does the initial simple matching selection algorithm (see
   rl_completion_matches ()).  The default is to do filename completion. */
int
rl_complete (ignore, invoking_key)
     int ignore, invoking_key;
{
  if (rl_inhibit_completion)
    return (_rl_insert_char (ignore, invoking_key));
  else if (rl_last_func == rl_complete && !completion_changed_buffer)
    return (rl_complete_internal ('?'));
  else if (_rl_complete_show_all)
    return (rl_complete_internal ('!'));
  else
    return (rl_complete_internal (TAB));
}

/* List the possible completions.  See description of rl_complete (). */
int
rl_possible_completions (ignore, invoking_key)
     int ignore, invoking_key;
{
  return (rl_complete_internal ('?'));
}

int
rl_insert_completions (ignore, invoking_key)
     int ignore, invoking_key;
{
  return (rl_complete_internal ('*'));
}

/* Return the correct value to pass to rl_complete_internal performing
   the same tests as rl_complete.  This allows consecutive calls to an
   application's completion function to list possible completions and for
   an application-specific completion function to honor the
   show-all-if-ambiguous readline variable. */
int
rl_completion_mode (cfunc)
     rl_command_func_t *cfunc;
{
  if (rl_last_func == cfunc && !completion_changed_buffer)
    return '?';
  else if (_rl_complete_show_all)
    return '!';
  else
    return TAB;
}

/************************************/
/*				    */
/*    Completion utility functions  */
/*				    */
/************************************/

/* Set default values for readline word completion.  These are the variables
   that application completion functions can change or inspect. */
static void
set_completion_defaults (what_to_do)
     int what_to_do;
{
  /* Only the completion entry function can change these. */
  rl_filename_completion_desired = 0;
  rl_filename_quoting_desired = 1;
  rl_completion_type = what_to_do;
  rl_completion_suppress_append = 0;

  /* The completion entry function may optionally change this. */
  rl_completion_mark_symlink_dirs = _rl_complete_mark_symlink_dirs;
}

/* The user must press "y" or "n". Non-zero return means "y" pressed. */
static int
get_y_or_n (for_pager)
     int for_pager;
{
  int c;

  for (;;)
    {
      RL_SETSTATE(RL_STATE_MOREINPUT);
      c = rl_read_key ();
      RL_UNSETSTATE(RL_STATE_MOREINPUT);

      if (c == 'y' || c == 'Y' || c == ' ')
	return (1);
      if (c == 'n' || c == 'N' || c == RUBOUT)
	return (0);
      if (c == ABORT_CHAR)
	_rl_abort_internal ();
      if (for_pager && (c == NEWLINE || c == RETURN))
	return (2);
      if (for_pager && (c == 'q' || c == 'Q'))
	return (0);
      rl_ding ();
    }
}

static int
_rl_internal_pager (lines)
     int lines;
{
  int i;

  fprintf (rl_outstream, "--More--");
  fflush (rl_outstream);
  i = get_y_or_n (1);
  _rl_erase_entire_line ();
  if (i == 0)
    return -1;
  else if (i == 2)
    return (lines - 1);
  else
    return 0;
}

#if defined (VISIBLE_STATS)
/* Return the character which best describes FILENAME.
     `@' for symbolic links
     `/' for directories
     `*' for executables
     `=' for sockets
     `|' for FIFOs
     `%' for character special devices
     `#' for block special devices */
static int
stat_char (filename)
     char *filename;
{
  struct stat finfo;
  int character, r;

#if defined (HAVE_LSTAT) && defined (S_ISLNK)
  r = lstat (filename, &finfo);
#else
  r = stat (filename, &finfo);
#endif

  if (r == -1)
    return (0);

  character = 0;
  if (S_ISDIR (finfo.st_mode))
    character = '/';
#if defined (S_ISCHR)
  else if (S_ISCHR (finfo.st_mode))
    character = '%';
#endif /* S_ISCHR */
#if defined (S_ISBLK)
  else if (S_ISBLK (finfo.st_mode))
    character = '#';
#endif /* S_ISBLK */
#if defined (S_ISLNK)
  else if (S_ISLNK (finfo.st_mode))
    character = '@';
#endif /* S_ISLNK */
#if defined (S_ISSOCK)
  else if (S_ISSOCK (finfo.st_mode))
    character = '=';
#endif /* S_ISSOCK */
#if defined (S_ISFIFO)
  else if (S_ISFIFO (finfo.st_mode))
    character = '|';
#endif
  else if (S_ISREG (finfo.st_mode))
    {
      if (access (filename, X_OK) == 0)
	character = '*';
    }
  return (character);
}
#endif /* VISIBLE_STATS */

/* Return the portion of PATHNAME that should be output when listing
   possible completions.  If we are hacking filename completion, we
   are only interested in the basename, the portion following the
   final slash.  Otherwise, we return what we were passed.  Since
   printing empty strings is not very informative, if we're doing
   filename completion, and the basename is the empty string, we look
   for the previous slash and return the portion following that.  If
   there's no previous slash, we just return what we were passed. */
static char *
printable_part (pathname)
      char *pathname;
{
  char *temp, *x;

  if (rl_filename_completion_desired == 0)	/* don't need to do anything */
    return (pathname);

  temp = strrchr (pathname, '/');
#if defined (__MSDOS__)
  if (temp == 0 && ISALPHA ((unsigned char)pathname[0]) && pathname[1] == ':')
    temp = pathname + 1;
#endif

  if (temp == 0 || *temp == '\0')
    return (pathname);
  /* If the basename is NULL, we might have a pathname like '/usr/src/'.
     Look for a previous slash and, if one is found, return the portion
     following that slash.  If there's no previous slash, just return the
     pathname we were passed. */
  else if (temp[1] == '\0')
    {
      for (x = temp - 1; x > pathname; x--)
        if (*x == '/')
          break;
      return ((*x == '/') ? x + 1 : pathname);
    }
  else
    return ++temp;
}

/* Output TO_PRINT to rl_outstream.  If VISIBLE_STATS is defined and we
   are using it, check for and output a single character for `special'
   filenames.  Return the number of characters we output. */

#define PUTX(c) \
    do { \
      if (CTRL_CHAR (c)) \
        { \
          putc ('^', rl_outstream); \
          putc (UNCTRL (c), rl_outstream); \
          printed_len += 2; \
        } \
      else if (c == RUBOUT) \
	{ \
	  putc ('^', rl_outstream); \
	  putc ('?', rl_outstream); \
	  printed_len += 2; \
	} \
      else \
	{ \
	  putc (c, rl_outstream); \
	  printed_len++; \
	} \
    } while (0)

static int
print_filename (to_print, full_pathname)
     char *to_print, *full_pathname;
{
  int printed_len = 0;
#if !defined (VISIBLE_STATS)
  char *s;

  for (s = to_print; *s; s++)
    {
      PUTX (*s);
    }
#else
  char *s, c, *new_full_pathname;
  int extension_char;

  for (s = to_print; *s; s++)
    {
      PUTX (*s);
    }

 if (rl_filename_completion_desired && rl_visible_stats)
    {
      /* If to_print != full_pathname, to_print is the basename of the
	 path passed.  In this case, we try to expand the directory
	 name before checking for the stat character. */
      if (to_print != full_pathname)
	{
	  /* Terminate the directory name. */
	  c = to_print[-1];
	  to_print[-1] = '\0';

	  /* If setting the last slash in full_pathname to a NUL results in
	     full_pathname being the empty string, we are trying to complete
	     files in the root directory.  If we pass a null string to the
	     bash directory completion hook, for example, it will expand it
	     to the current directory.  We just want the `/'. */
	  s = tilde_expand (full_pathname && *full_pathname ? full_pathname : "/");
	  if (rl_directory_completion_hook)
	    (*rl_directory_completion_hook) (&s);
	  if (asprintf(&new_full_pathname, "%s/%s", s, to_print) == -1)
		  memory_error_and_abort("asprintf");
	  extension_char = stat_char (new_full_pathname);
	  free (new_full_pathname);
	  to_print[-1] = c;
	}
      else
	{
	  s = tilde_expand (full_pathname);
	  extension_char = stat_char (s);
	}

      free (s);
      if (extension_char)
	{
	  putc (extension_char, rl_outstream);
	  printed_len++;
	}
    }
#endif /* VISIBLE_STATS */
  return printed_len;
}

static char *
rl_quote_filename (s, rtype, qcp)
     char *s;
     int rtype;
     char *qcp;
{
  char *r;
  int len = strlen(s) + 2;

  r = (char *)xmalloc (len);
  *r = *rl_completer_quote_characters;
  strlcpy (r + 1, s, len - 1);
  if (qcp)
    *qcp = *rl_completer_quote_characters;
  return r;
}

/* Find the bounds of the current word for completion purposes, and leave
   rl_point set to the end of the word.  This function skips quoted
   substrings (characters between matched pairs of characters in
   rl_completer_quote_characters).  First we try to find an unclosed
   quoted substring on which to do matching.  If one is not found, we use
   the word break characters to find the boundaries of the current word.
   We call an application-specific function to decide whether or not a
   particular word break character is quoted; if that function returns a
   non-zero result, the character does not break a word.  This function
   returns the opening quote character if we found an unclosed quoted
   substring, '\0' otherwise.  FP, if non-null, is set to a value saying
   which (shell-like) quote characters we found (single quote, double
   quote, or backslash) anywhere in the string.  DP, if non-null, is set to
   the value of the delimiter character that caused a word break. */

char
_rl_find_completion_word (fp, dp)
     int *fp, *dp;
{
  int scan, end, found_quote, delimiter, pass_next, isbrk;
  char quote_char;

  end = rl_point;
  found_quote = delimiter = 0;
  quote_char = '\0';

  if (rl_completer_quote_characters)
    {
      /* We have a list of characters which can be used in pairs to
	 quote substrings for the completer.  Try to find the start
	 of an unclosed quoted substring. */
      /* FOUND_QUOTE is set so we know what kind of quotes we found. */
      for (scan = pass_next = 0; scan < end; scan++)
	{
	  if (pass_next)
	    {
	      pass_next = 0;
	      continue;
	    }

	  /* Shell-like semantics for single quotes -- don't allow backslash
	     to quote anything in single quotes, especially not the closing
	     quote.  If you don't like this, take out the check on the value
	     of quote_char. */
	  if (quote_char != '\'' && rl_line_buffer[scan] == '\\')
	    {
	      pass_next = 1;
	      found_quote |= RL_QF_BACKSLASH;
	      continue;
	    }

	  if (quote_char != '\0')
	    {
	      /* Ignore everything until the matching close quote char. */
	      if (rl_line_buffer[scan] == quote_char)
		{
		  /* Found matching close.  Abandon this substring. */
		  quote_char = '\0';
		  rl_point = end;
		}
	    }
	  else if (strchr (rl_completer_quote_characters, rl_line_buffer[scan]))
	    {
	      /* Found start of a quoted substring. */
	      quote_char = rl_line_buffer[scan];
	      rl_point = scan + 1;
	      /* Shell-like quoting conventions. */
	      if (quote_char == '\'')
		found_quote |= RL_QF_SINGLE_QUOTE;
	      else if (quote_char == '"')
		found_quote |= RL_QF_DOUBLE_QUOTE;
	      else
		found_quote |= RL_QF_OTHER_QUOTE;
	    }
	}
    }

  if (rl_point == end && quote_char == '\0')
    {
      /* We didn't find an unclosed quoted substring upon which to do
         completion, so use the word break characters to find the
         substring on which to complete. */
#if defined (HANDLE_MULTIBYTE)
      while (rl_point = _rl_find_prev_mbchar (rl_line_buffer, rl_point, MB_FIND_ANY))
#else
      while (--rl_point)
#endif
	{
	  scan = rl_line_buffer[rl_point];

	  if (strchr (rl_completer_word_break_characters, scan) == 0)
	    continue;

	  /* Call the application-specific function to tell us whether
	     this word break character is quoted and should be skipped. */
	  if (rl_char_is_quoted_p && found_quote &&
	      (*rl_char_is_quoted_p) (rl_line_buffer, rl_point))
	    continue;

	  /* Convoluted code, but it avoids an n^2 algorithm with calls
	     to char_is_quoted. */
	  break;
	}
    }

  /* If we are at an unquoted word break, then advance past it. */
  scan = rl_line_buffer[rl_point];

  /* If there is an application-specific function to say whether or not
     a character is quoted and we found a quote character, let that
     function decide whether or not a character is a word break, even
     if it is found in rl_completer_word_break_characters.  Don't bother
     if we're at the end of the line, though. */
  if (scan)
    {
      if (rl_char_is_quoted_p)
	isbrk = (found_quote == 0 ||
		(*rl_char_is_quoted_p) (rl_line_buffer, rl_point) == 0) &&
		strchr (rl_completer_word_break_characters, scan) != 0;
      else
	isbrk = strchr (rl_completer_word_break_characters, scan) != 0;

      if (isbrk)
	{
	  /* If the character that caused the word break was a quoting
	     character, then remember it as the delimiter. */
	  if (rl_basic_quote_characters &&
	      strchr (rl_basic_quote_characters, scan) &&
	      (end - rl_point) > 1)
	    delimiter = scan;

	  /* If the character isn't needed to determine something special
	     about what kind of completion to perform, then advance past it. */
	  if (rl_special_prefixes == 0 || strchr (rl_special_prefixes, scan) == 0)
	    rl_point++;
	}
    }

  if (fp)
    *fp = found_quote;
  if (dp)
    *dp = delimiter;

  return (quote_char);
}

static char **
gen_completion_matches (text, start, end, our_func, found_quote, quote_char)
     char *text;
     int start, end;
     rl_compentry_func_t *our_func;
     int found_quote, quote_char;
{
  char **matches, *temp;

  /* If the user wants to TRY to complete, but then wants to give
     up and use the default completion function, they set the
     variable rl_attempted_completion_function. */
  if (rl_attempted_completion_function)
    {
      matches = (*rl_attempted_completion_function) (text, start, end);

      if (matches || rl_attempted_completion_over)
	{
	  rl_attempted_completion_over = 0;
	  return (matches);
	}
    }

  /* Beware -- we're stripping the quotes here.  Do this only if we know
     we are doing filename completion and the application has defined a
     filename dequoting function. */
  temp = (char *)NULL;

  if (found_quote && our_func == rl_filename_completion_function &&
      rl_filename_dequoting_function)
    {
      /* delete single and double quotes */
      temp = (*rl_filename_dequoting_function) (text, quote_char);
      text = temp;	/* not freeing text is not a memory leak */
    }

  matches = rl_completion_matches (text, our_func);
  FREE (temp);
  return matches;
}

/* Filter out duplicates in MATCHES.  This frees up the strings in
   MATCHES. */
static char **
remove_duplicate_matches (matches)
     char **matches;
{
  char *lowest_common;
  int i, j, newlen;
  char dead_slot;
  char **temp_array;

  /* Sort the items. */
  for (i = 0; matches[i]; i++)
    ;

  /* Sort the array without matches[0], since we need it to
     stay in place no matter what. */
  if (i)
    qsort (matches+1, i-1, sizeof (char *), (QSFUNC *)_rl_qsort_string_compare);

  /* Remember the lowest common denominator for it may be unique. */
  lowest_common = savestring (matches[0]);

  for (i = newlen = 0; matches[i + 1]; i++)
    {
      if (strcmp (matches[i], matches[i + 1]) == 0)
	{
	  free (matches[i]);
	  matches[i] = (char *)&dead_slot;
	}
      else
	newlen++;
    }

  /* We have marked all the dead slots with (char *)&dead_slot.
     Copy all the non-dead entries into a new array. */
  temp_array = (char **)xmalloc ((3 + newlen) * sizeof (char *));
  for (i = j = 1; matches[i]; i++)
    {
      if (matches[i] != (char *)&dead_slot)
	temp_array[j++] = matches[i];
    }
  temp_array[j] = (char *)NULL;

  if (matches[0] != (char *)&dead_slot)
    free (matches[0]);

  /* Place the lowest common denominator back in [0]. */
  temp_array[0] = lowest_common;

  /* If there is one string left, and it is identical to the
     lowest common denominator, then the LCD is the string to
     insert. */
  if (j == 2 && strcmp (temp_array[0], temp_array[1]) == 0)
    {
      free (temp_array[1]);
      temp_array[1] = (char *)NULL;
    }
  return (temp_array);
}

/* Find the common prefix of the list of matches, and put it into
   matches[0]. */
static int
compute_lcd_of_matches (match_list, matches, text)
     char **match_list;
     int matches;
     const char *text;
{
  register int i, c1, c2, si;
  int low;		/* Count of max-matched characters. */
#if defined (HANDLE_MULTIBYTE)
  int v;
  mbstate_t ps1, ps2;
  wchar_t wc1, wc2;
#endif

  /* If only one match, just use that.  Otherwise, compare each
     member of the list with the next, finding out where they
     stop matching. */
  if (matches == 1)
    {
      match_list[0] = match_list[1];
      match_list[1] = (char *)NULL;
      return 1;
    }

  for (i = 1, low = 100000; i < matches; i++)
    {
#if defined (HANDLE_MULTIBYTE)
      if (MB_CUR_MAX > 1 && rl_byte_oriented == 0)
	{
	  memset (&ps1, 0, sizeof (mbstate_t));
	  memset (&ps2, 0, sizeof (mbstate_t));
	}
#endif
      if (_rl_completion_case_fold)
	{
	  for (si = 0;
	       (c1 = _rl_to_lower(match_list[i][si])) &&
	       (c2 = _rl_to_lower(match_list[i + 1][si]));
	       si++)
#if defined (HANDLE_MULTIBYTE)
	    if (MB_CUR_MAX > 1 && rl_byte_oriented == 0)
	      {
		v = mbrtowc (&wc1, match_list[i]+si, strlen (match_list[i]+si), &ps1);
		mbrtowc (&wc2, match_list[i+1]+si, strlen (match_list[i+1]+si), &ps2);
		wc1 = towlower (wc1);
		wc2 = towlower (wc2);
		if (wc1 != wc2)
		  break;
		else if (v > 1)
		  si += v - 1;
	      }
	    else
#endif
	    if (c1 != c2)
	      break;
	}
      else
	{
	  for (si = 0;
	       (c1 = match_list[i][si]) &&
	       (c2 = match_list[i + 1][si]);
	       si++)
#if defined (HANDLE_MULTIBYTE)
	    if (MB_CUR_MAX > 1 && rl_byte_oriented == 0)
	      {
		mbstate_t ps_back = ps1;
		if (!_rl_compare_chars (match_list[i], si, &ps1, match_list[i+1], si, &ps2))
		  break;
		else if ((v = _rl_get_char_len (&match_list[i][si], &ps_back)) > 1)
		  si += v - 1;
	      }
	    else
#endif
	    if (c1 != c2)
	      break;
	}

      if (low > si)
	low = si;
    }

  /* If there were multiple matches, but none matched up to even the
     first character, and the user typed something, use that as the
     value of matches[0]. */
  if (low == 0 && text && *text)
    {
      match_list[0] = strdup(text);
      if (match_list[0] == NULL)
	      memory_error_and_abort("strdup");
    }
  else
    {
      match_list[0] = (char *)xmalloc (low + 1);

      /* XXX - this might need changes in the presence of multibyte chars */

      /* If we are ignoring case, try to preserve the case of the string
	 the user typed in the face of multiple matches differing in case. */
      if (_rl_completion_case_fold)
	{
	  /* sort the list to get consistent answers. */
	  qsort (match_list+1, matches, sizeof(char *), (QSFUNC *)_rl_qsort_string_compare);

	  si = strlen (text);
	  if (si <= low)
	    {
	      for (i = 1; i <= matches; i++)
		if (strncmp (match_list[i], text, si) == 0)
		  {
		    strncpy (match_list[0], match_list[i], low);
		    break;
		  }
	      /* no casematch, use first entry */
	      if (i > matches)
		strncpy (match_list[0], match_list[1], low);
	    }
	  else
	    /* otherwise, just use the text the user typed. */
	    strncpy (match_list[0], text, low);
	}
      else
        strncpy (match_list[0], match_list[1], low);

      match_list[0][low] = '\0';
    }

  return matches;
}

static int
postprocess_matches (matchesp, matching_filenames)
     char ***matchesp;
     int matching_filenames;
{
  char *t, **matches, **temp_matches;
  int nmatch, i;

  matches = *matchesp;

  if (matches == 0)
    return 0;

  /* It seems to me that in all the cases we handle we would like
     to ignore duplicate possiblilities.  Scan for the text to
     insert being identical to the other completions. */
  if (rl_ignore_completion_duplicates)
    {
      temp_matches = remove_duplicate_matches (matches);
      free (matches);
      matches = temp_matches;
    }

  /* If we are matching filenames, then here is our chance to
     do clever processing by re-examining the list.  Call the
     ignore function with the array as a parameter.  It can
     munge the array, deleting matches as it desires. */
  if (rl_ignore_some_completions_function && matching_filenames)
    {
      for (nmatch = 1; matches[nmatch]; nmatch++)
	;
      (void)(*rl_ignore_some_completions_function) (matches);
      if (matches == 0 || matches[0] == 0)
	{
	  FREE (matches);
	  *matchesp = (char **)0;
	  return 0;
        }
      else
	{
	  /* If we removed some matches, recompute the common prefix. */
	  for (i = 1; matches[i]; i++)
	    ;
	  if (i > 1 && i < nmatch)
	    {
	      t = matches[0];
	      compute_lcd_of_matches (matches, i - 1, t);
	      FREE (t);
	    }
	}
    }

  *matchesp = matches;
  return (1);
}

/* A convenience function for displaying a list of strings in
   columnar format on readline's output stream.  MATCHES is the list
   of strings, in argv format, LEN is the number of strings in MATCHES,
   and MAX is the length of the longest string in MATCHES. */
void
rl_display_match_list (matches, len, max)
     char **matches;
     int len, max;
{
  int count, limit, printed_len, lines;
  int i, j, k, l;
  char *temp;

  /* How many items of MAX length can we fit in the screen window? */
  max += 2;
  limit = _rl_screenwidth / max;
  if (limit != 1 && (limit * max == _rl_screenwidth))
    limit--;

  /* Avoid a possible floating exception.  If max > _rl_screenwidth,
     limit will be 0 and a divide-by-zero fault will result. */
  if (limit == 0)
    limit = 1;

  /* How many iterations of the printing loop? */
  count = (len + (limit - 1)) / limit;

  /* Watch out for special case.  If LEN is less than LIMIT, then
     just do the inner printing loop.
	   0 < len <= limit  implies  count = 1. */

  /* Sort the items if they are not already sorted. */
  if (rl_ignore_completion_duplicates == 0)
    qsort (matches + 1, len, sizeof (char *), (QSFUNC *)_rl_qsort_string_compare);

  rl_crlf ();

  lines = 0;
  if (_rl_print_completions_horizontally == 0)
    {
      /* Print the sorted items, up-and-down alphabetically, like ls. */
      for (i = 1; i <= count; i++)
	{
	  for (j = 0, l = i; j < limit; j++)
	    {
	      if (l > len || matches[l] == 0)
		break;
	      else
		{
		  temp = printable_part (matches[l]);
		  printed_len = print_filename (temp, matches[l]);

		  if (j + 1 < limit)
		    for (k = 0; k < max - printed_len; k++)
		      putc (' ', rl_outstream);
		}
	      l += count;
	    }
	  rl_crlf ();
	  lines++;
	  if (_rl_page_completions && lines >= (_rl_screenheight - 1) && i < count)
	    {
	      lines = _rl_internal_pager (lines);
	      if (lines < 0)
		return;
	    }
	}
    }
  else
    {
      /* Print the sorted items, across alphabetically, like ls -x. */
      for (i = 1; matches[i]; i++)
	{
	  temp = printable_part (matches[i]);
	  printed_len = print_filename (temp, matches[i]);
	  /* Have we reached the end of this line? */
	  if (matches[i+1])
	    {
	      if (i && (limit > 1) && (i % limit) == 0)
		{
		  rl_crlf ();
		  lines++;
		  if (_rl_page_completions && lines >= _rl_screenheight - 1)
		    {
		      lines = _rl_internal_pager (lines);
		      if (lines < 0)
			return;
		    }
		}
	      else
		for (k = 0; k < max - printed_len; k++)
		  putc (' ', rl_outstream);
	    }
	}
      rl_crlf ();
    }
}

/* Display MATCHES, a list of matching filenames in argv format.  This
   handles the simple case -- a single match -- first.  If there is more
   than one match, we compute the number of strings in the list and the
   length of the longest string, which will be needed by the display
   function.  If the application wants to handle displaying the list of
   matches itself, it sets RL_COMPLETION_DISPLAY_MATCHES_HOOK to the
   address of a function, and we just call it.  If we're handling the
   display ourselves, we just call rl_display_match_list.  We also check
   that the list of matches doesn't exceed the user-settable threshold,
   and ask the user if he wants to see the list if there are more matches
   than RL_COMPLETION_QUERY_ITEMS. */
static void
display_matches (matches)
     char **matches;
{
  int len, max, i;
  char *temp;

  /* Move to the last visible line of a possibly-multiple-line command. */
  _rl_move_vert (_rl_vis_botlin);

  /* Handle simple case first.  What if there is only one answer? */
  if (matches[1] == 0)
    {
      temp = printable_part (matches[0]);
      rl_crlf ();
      print_filename (temp, matches[0]);
      rl_crlf ();

      rl_forced_update_display ();
      rl_display_fixed = 1;

      return;
    }

  /* There is more than one answer.  Find out how many there are,
     and find the maximum printed length of a single entry. */
  for (max = 0, i = 1; matches[i]; i++)
    {
      temp = printable_part (matches[i]);
      len = strlen (temp);

      if (len > max)
	max = len;
    }

  len = i - 1;

  /* If the caller has defined a display hook, then call that now. */
  if (rl_completion_display_matches_hook)
    {
      (*rl_completion_display_matches_hook) (matches, len, max);
      return;
    }

  /* If there are many items, then ask the user if she really wants to
     see them all. */
  if (len >= rl_completion_query_items)
    {
      rl_crlf ();
      fprintf (rl_outstream, "Display all %d possibilities? (y or n)", len);
      fflush (rl_outstream);
      if (get_y_or_n (0) == 0)
	{
	  rl_crlf ();

	  rl_forced_update_display ();
	  rl_display_fixed = 1;

	  return;
	}
    }

  rl_display_match_list (matches, len, max);

  rl_forced_update_display ();
  rl_display_fixed = 1;
}

static char *
make_quoted_replacement (match, mtype, qc)
     char *match;
     int mtype;
     char *qc;	/* Pointer to quoting character, if any */
{
  int should_quote, do_replace;
  char *replacement;

  /* If we are doing completion on quoted substrings, and any matches
     contain any of the completer_word_break_characters, then auto-
     matically prepend the substring with a quote character (just pick
     the first one from the list of such) if it does not already begin
     with a quote string.  FIXME: Need to remove any such automatically
     inserted quote character when it no longer is necessary, such as
     if we change the string we are completing on and the new set of
     matches don't require a quoted substring. */
  replacement = match;

  should_quote = match && rl_completer_quote_characters &&
			rl_filename_completion_desired &&
			rl_filename_quoting_desired;

  if (should_quote)
    should_quote = should_quote && (!qc || !*qc ||
		     (rl_completer_quote_characters && strchr (rl_completer_quote_characters, *qc)));

  if (should_quote)
    {
      /* If there is a single match, see if we need to quote it.
         This also checks whether the common prefix of several
	 matches needs to be quoted. */
      should_quote = rl_filename_quote_characters
			? (_rl_strpbrk (match, rl_filename_quote_characters) != 0)
			: 0;

      do_replace = should_quote ? mtype : NO_MATCH;
      /* Quote the replacement, since we found an embedded
	 word break character in a potential match. */
      if (do_replace != NO_MATCH && rl_filename_quoting_function)
	replacement = (*rl_filename_quoting_function) (match, do_replace, qc);
    }
  return (replacement);
}

static void
insert_match (match, start, mtype, qc)
     char *match;
     int start, mtype;
     char *qc;
{
  char *replacement;
  char oqc;

  oqc = qc ? *qc : '\0';
  replacement = make_quoted_replacement (match, mtype, qc);

  /* Now insert the match. */
  if (replacement)
    {
      /* Don't double an opening quote character. */
      if (qc && *qc && start && rl_line_buffer[start - 1] == *qc &&
	    replacement[0] == *qc)
	start--;
      /* If make_quoted_replacement changed the quoting character, remove
	 the opening quote and insert the (fully-quoted) replacement. */
      else if (qc && (*qc != oqc) && start && rl_line_buffer[start - 1] == oqc &&
	    replacement[0] != oqc)
	start--;
      _rl_replace_text (replacement, start, rl_point - 1);
      if (replacement != match)
        free (replacement);
    }
}

/* Append any necessary closing quote and a separator character to the
   just-inserted match.  If the user has specified that directories
   should be marked by a trailing `/', append one of those instead.  The
   default trailing character is a space.  Returns the number of characters
   appended.  If NONTRIVIAL_MATCH is set, we test for a symlink (if the OS
   has them) and don't add a suffix for a symlink to a directory.  A
   nontrivial match is one that actually adds to the word being completed.
   The variable rl_completion_mark_symlink_dirs controls this behavior
   (it's initially set to the what the user has chosen, indicated by the
   value of _rl_complete_mark_symlink_dirs, but may be modified by an
   application's completion function). */
static int
append_to_match (text, delimiter, quote_char, nontrivial_match)
     char *text;
     int delimiter, quote_char, nontrivial_match;
{
  char temp_string[4], *filename;
  int temp_string_index, s;
  struct stat finfo;

  temp_string_index = 0;
  if (quote_char && rl_point && rl_line_buffer[rl_point - 1] != quote_char)
    temp_string[temp_string_index++] = quote_char;

  if (delimiter)
    temp_string[temp_string_index++] = delimiter;
  else if (rl_completion_suppress_append == 0 && rl_completion_append_character)
    temp_string[temp_string_index++] = rl_completion_append_character;

  temp_string[temp_string_index++] = '\0';

  if (rl_filename_completion_desired)
    {
      filename = tilde_expand (text);
      s = (nontrivial_match && rl_completion_mark_symlink_dirs == 0)
		? LSTAT (filename, &finfo)
		: stat (filename, &finfo);
      if (s == 0 && S_ISDIR (finfo.st_mode))
	{
	  if (_rl_complete_mark_directories)
	    {
	      /* This is clumsy.  Avoid putting in a double slash if point
		 is at the end of the line and the previous character is a
		 slash. */
	      if (rl_point && rl_line_buffer[rl_point] == '\0' && rl_line_buffer[rl_point - 1] == '/')
		;
	      else if (rl_line_buffer[rl_point] != '/')
		rl_insert_text ("/");
	    }
	}
#ifdef S_ISLNK
      /* Don't add anything if the filename is a symlink and resolves to a
	 directory. */
      else if (s == 0 && S_ISLNK (finfo.st_mode) &&
	       stat (filename, &finfo) == 0 && S_ISDIR (finfo.st_mode))
	;
#endif
      else
	{
	  if (rl_point == rl_end && temp_string_index)
	    rl_insert_text (temp_string);
	}
      free (filename);
    }
  else
    {
      if (rl_point == rl_end && temp_string_index)
	rl_insert_text (temp_string);
    }

  return (temp_string_index);
}

static void
insert_all_matches (matches, point, qc)
     char **matches;
     int point;
     char *qc;
{
  int i;
  char *rp;

  rl_begin_undo_group ();
  /* remove any opening quote character; make_quoted_replacement will add
     it back. */
  if (qc && *qc && point && rl_line_buffer[point - 1] == *qc)
    point--;
  rl_delete_text (point, rl_point);
  rl_point = point;

  if (matches[1])
    {
      for (i = 1; matches[i]; i++)
	{
	  rp = make_quoted_replacement (matches[i], SINGLE_MATCH, qc);
	  rl_insert_text (rp);
	  rl_insert_text (" ");
	  if (rp != matches[i])
	    free (rp);
	}
    }
  else
    {
      rp = make_quoted_replacement (matches[0], SINGLE_MATCH, qc);
      rl_insert_text (rp);
      rl_insert_text (" ");
      if (rp != matches[0])
	free (rp);
    }
  rl_end_undo_group ();
}

void
_rl_free_match_list (matches)
     char **matches;
{
  register int i;

  if (matches == 0)
    return;

  for (i = 0; matches[i]; i++)
    free (matches[i]);
  free (matches);
}

/* Complete the word at or before point.
   WHAT_TO_DO says what to do with the completion.
   `?' means list the possible completions.
   TAB means do standard completion.
   `*' means insert all of the possible completions.
   `!' means to do standard completion, and list all possible completions if
   there is more than one. */
int
rl_complete_internal (what_to_do)
     int what_to_do;
{
  char **matches;
  rl_compentry_func_t *our_func;
  int start, end, delimiter, found_quote, i, nontrivial_lcd;
  char *text, *saved_line_buffer;
  char quote_char;

  RL_SETSTATE(RL_STATE_COMPLETING);

  set_completion_defaults (what_to_do);

  saved_line_buffer = rl_line_buffer ? savestring (rl_line_buffer) : (char *)NULL;
  our_func = rl_completion_entry_function
		? rl_completion_entry_function
		: rl_filename_completion_function;

  /* We now look backwards for the start of a filename/variable word. */
  end = rl_point;
  found_quote = delimiter = 0;
  quote_char = '\0';

  if (rl_point)
    /* This (possibly) changes rl_point.  If it returns a non-zero char,
       we know we have an open quote. */
    quote_char = _rl_find_completion_word (&found_quote, &delimiter);

  start = rl_point;
  rl_point = end;

  text = rl_copy_text (start, end);
  matches = gen_completion_matches (text, start, end, our_func, found_quote, quote_char);
  /* nontrivial_lcd is set if the common prefix adds something to the word
     being completed. */
  nontrivial_lcd = matches && strcmp (text, matches[0]) != 0;
  free (text);

  if (matches == 0)
    {
      rl_ding ();
      FREE (saved_line_buffer);
      completion_changed_buffer = 0;
      RL_UNSETSTATE(RL_STATE_COMPLETING);
      return (0);
    }

  /* If we are matching filenames, the attempted completion function will
     have set rl_filename_completion_desired to a non-zero value.  The basic
     rl_filename_completion_function does this. */
  i = rl_filename_completion_desired;

  if (postprocess_matches (&matches, i) == 0)
    {
      rl_ding ();
      FREE (saved_line_buffer);
      completion_changed_buffer = 0;
      RL_UNSETSTATE(RL_STATE_COMPLETING);
      return (0);
    }

  switch (what_to_do)
    {
    case TAB:
    case '!':
      /* Insert the first match with proper quoting. */
      if (*matches[0])
	insert_match (matches[0], start, matches[1] ? MULT_MATCH : SINGLE_MATCH, &quote_char);

      /* If there are more matches, ring the bell to indicate.
	 If we are in vi mode, Posix.2 says to not ring the bell.
	 If the `show-all-if-ambiguous' variable is set, display
	 all the matches immediately.  Otherwise, if this was the
	 only match, and we are hacking files, check the file to
	 see if it was a directory.  If so, and the `mark-directories'
	 variable is set, add a '/' to the name.  If not, and we
	 are at the end of the line, then add a space.  */
      if (matches[1])
	{
	  if (what_to_do == '!')
	    {
	      display_matches (matches);
	      break;
	    }
	  else if (rl_editing_mode != vi_mode)
	    rl_ding ();	/* There are other matches remaining. */
	}
      else
	append_to_match (matches[0], delimiter, quote_char, nontrivial_lcd);

      break;

    case '*':
      insert_all_matches (matches, start, &quote_char);
      break;

    case '?':
      display_matches (matches);
      break;

    default:
      fprintf (stderr, "\r\nreadline: bad value %d for what_to_do in rl_complete\n", what_to_do);
      rl_ding ();
      FREE (saved_line_buffer);
      RL_UNSETSTATE(RL_STATE_COMPLETING);
      return 1;
    }

  _rl_free_match_list (matches);

  /* Check to see if the line has changed through all of this manipulation. */
  if (saved_line_buffer)
    {
      completion_changed_buffer = strcmp (rl_line_buffer, saved_line_buffer) != 0;
      free (saved_line_buffer);
    }

  RL_UNSETSTATE(RL_STATE_COMPLETING);
  return 0;
}

/***************************************************************/
/*							       */
/*  Application-callable completion match generator functions  */
/*							       */
/***************************************************************/

/* Return an array of (char *) which is a list of completions for TEXT.
   If there are no completions, return a NULL pointer.
   The first entry in the returned array is the substitution for TEXT.
   The remaining entries are the possible completions.
   The array is terminated with a NULL pointer.

   ENTRY_FUNCTION is a function of two args, and returns a (char *).
     The first argument is TEXT.
     The second is a state argument; it should be zero on the first call, and
     non-zero on subsequent calls.  It returns a NULL pointer to the caller
     when there are no more matches.
 */
char **
rl_completion_matches (text, entry_function)
     const char *text;
     rl_compentry_func_t *entry_function;
{
  /* Number of slots in match_list. */
  int match_list_size;

  /* The list of matches. */
  char **match_list;

  /* Number of matches actually found. */
  int matches;

  /* Temporary string binder. */
  char *string;

  matches = 0;
  match_list_size = 10;
  match_list = (char **)xmalloc ((match_list_size + 1) * sizeof (char *));
  match_list[1] = (char *)NULL;

  while ((string = (*entry_function) (text, matches)))
    {
      if (matches + 1 == match_list_size)
	match_list = (char **)xrealloc
	  (match_list, ((match_list_size += 10) + 1) * sizeof (char *));

      match_list[++matches] = string;
      match_list[matches + 1] = (char *)NULL;
    }

  /* If there were any matches, then look through them finding out the
     lowest common denominator.  That then becomes match_list[0]. */
  if (matches)
    compute_lcd_of_matches (match_list, matches, text);
  else				/* There were no matches. */
    {
      free (match_list);
      match_list = (char **)NULL;
    }
  return (match_list);
}

/* A completion function for usernames.
   TEXT contains a partial username preceded by a random
   character (usually `~').  */
char *
rl_username_completion_function (text, state)
     const char *text;
     int state;
{
#if defined (__WIN32__) || defined (__OPENNT)
  return (char *)NULL;
#else /* !__WIN32__ && !__OPENNT) */
  static char *username = (char *)NULL;
  static struct passwd *entry;
  static int namelen, first_char, first_char_loc;
  char *value;

  if (state == 0)
    {
      FREE (username);

      first_char = *text;
      first_char_loc = first_char == '~';

      username = savestring (&text[first_char_loc]);
      namelen = strlen (username);
      setpwent ();
    }

  while ((entry = getpwent ()))
    {
      /* Null usernames should result in all users as possible completions. */
      if (namelen == 0 || (STREQN (username, entry->pw_name, namelen)))
	break;
    }

  if (entry == 0)
    {
      endpwent ();
      return ((char *)NULL);
    }
  else
    {
      int len = 2 + strlen(entry->pw_name);
      value = (char *)xmalloc (len);

      *value = *text;

      strlcpy (value + first_char_loc, entry->pw_name, len - first_char_loc);

      if (first_char == '~')
	rl_filename_completion_desired = 1;

      return (value);
    }
#endif /* !__WIN32__ && !__OPENNT */
}

/* Okay, now we write the entry_function for filename completion.  In the
   general case.  Note that completion in the shell is a little different
   because of all the pathnames that must be followed when looking up the
   completion for a command. */
char *
rl_filename_completion_function (text, state)
     const char *text;
     int state;
{
  static DIR *directory = (DIR *)NULL;
  static char *filename = (char *)NULL;
  static char *dirname = (char *)NULL;
  static char *users_dirname = (char *)NULL;
  static int filename_len;
  char *temp;
  int dirlen;
  struct dirent *entry;

  /* If we don't have any state, then do some initialization. */
  if (state == 0)
    {
      /* If we were interrupted before closing the directory or reading
	 all of its contents, close it. */
      if (directory)
	{
	  closedir (directory);
	  directory = (DIR *)NULL;
	}
      FREE (dirname);
      FREE (filename);
      FREE (users_dirname);

      filename = savestring (text);
      filename_len = strlen(filename) + 1;
      if (*text == 0)
	text = ".";
      dirname = savestring (text);

      temp = strrchr (dirname, '/');

#if defined (__MSDOS__)
      /* special hack for //X/... */
      if (dirname[0] == '/' && dirname[1] == '/' && ISALPHA ((unsigned char)dirname[2]) && dirname[3] == '/')
        temp = strrchr (dirname + 3, '/');
#endif

      if (temp)
	{
	  strlcpy (filename, ++temp, filename_len);
	  *temp = '\0';
	}
#if defined (__MSDOS__)
      /* searches from current directory on the drive */
      else if (ISALPHA ((unsigned char)dirname[0]) && dirname[1] == ':')
        {
	  /* XXX DOS strlcpy anyone? */
          strlcpy (filename, dirname + 2, filename_len);
          dirname[2] = '\0';
        }
#endif
      else
	{
	  dirname[0] = '.';
	  dirname[1] = '\0';
	}

      /* We aren't done yet.  We also support the "~user" syntax. */

      /* Save the version of the directory that the user typed. */
      users_dirname = savestring (dirname);

      if (*dirname == '~')
	{
	  temp = tilde_expand (dirname);
	  free (dirname);
	  dirname = temp;
	}

      if (rl_directory_rewrite_hook)
	(*rl_directory_rewrite_hook) (&dirname);

      if (rl_directory_completion_hook && (*rl_directory_completion_hook) (&dirname))
	{
	  free (users_dirname);
	  users_dirname = savestring (dirname);
	}

      directory = opendir (dirname);
      filename_len = strlen (filename);

      rl_filename_completion_desired = 1;
    }

  /* At this point we should entertain the possibility of hacking wildcarded
     filenames, like /usr/man/man<WILD>/te<TAB>.  If the directory name
     contains globbing characters, then build an array of directories, and
     then map over that list while completing. */
  /* *** UNIMPLEMENTED *** */

  /* Now that we have some state, we can read the directory. */

  entry = (struct dirent *)NULL;
  while (directory && (entry = readdir (directory)))
    {
      /* Special case for no filename.  If the user has disabled the
         `match-hidden-files' variable, skip filenames beginning with `.'.
	 All other entries except "." and ".." match. */
      if (filename_len == 0)
	{
	  if (_rl_match_hidden_files == 0 && HIDDEN_FILE (entry->d_name))
	    continue;

	  if (entry->d_name[0] != '.' ||
	       (entry->d_name[1] &&
		 (entry->d_name[1] != '.' || entry->d_name[2])))
	    break;
	}
      else
	{
	  /* Otherwise, if these match up to the length of filename, then
	     it is a match. */
	  if (_rl_completion_case_fold)
	    {
	      if ((_rl_to_lower (entry->d_name[0]) == _rl_to_lower (filename[0])) &&
		  (((int)D_NAMLEN (entry)) >= filename_len) &&
		  (_rl_strnicmp (filename, entry->d_name, filename_len) == 0))
		break;
	    }
	  else
	    {
	      if ((entry->d_name[0] == filename[0]) &&
		  (((int)D_NAMLEN (entry)) >= filename_len) &&
		  (strncmp (filename, entry->d_name, filename_len) == 0))
		break;
	    }
	}
    }

  if (entry == 0)
    {
      if (directory)
	{
	  closedir (directory);
	  directory = (DIR *)NULL;
	}
      if (dirname)
	{
	  free (dirname);
	  dirname = (char *)NULL;
	}
      if (filename)
	{
	  free (filename);
	  filename = (char *)NULL;
	}
      if (users_dirname)
	{
	  free (users_dirname);
	  users_dirname = (char *)NULL;
	}

      return (char *)NULL;
    }
  else
    {
      /* dirname && (strcmp (dirname, ".") != 0) */
      if (dirname && (dirname[0] != '.' || dirname[1]))
	{
	  int templen;
	  if (rl_complete_with_tilde_expansion && *users_dirname == '~')
	    {
	      dirlen = strlen (dirname);
	      templen = 2 + dirlen + D_NAMLEN (entry);
	      temp = (char *)xmalloc (templen);
	      strlcpy (temp, dirname, templen);
	      /* Canonicalization cuts off any final slash present.  We
		 may need to add it back. */
	      if (dirname[dirlen - 1] != '/')
	        {
	          temp[dirlen++] = '/';
	          temp[dirlen] = '\0';
	        }
	    }
	  else
	    {
	      dirlen = strlen (users_dirname);
	      templen = 2 + dirlen + D_NAMLEN (entry);
	      temp = (char *)xmalloc (templen);
	      strlcpy (temp, users_dirname, templen);
	      /* Make sure that temp has a trailing slash here. */
	      if (users_dirname[dirlen - 1] != '/')
	        {
		  temp[dirlen++] = '/';
	          temp[dirlen] = '\0';
	        }
	    }

	  strlcat (temp, entry->d_name, templen);
	}
      else
	temp = savestring (entry->d_name);

      return (temp);
    }
}

/* An initial implementation of a menu completion function a la tcsh.  The
   first time (if the last readline command was not rl_menu_complete), we
   generate the list of matches.  This code is very similar to the code in
   rl_complete_internal -- there should be a way to combine the two.  Then,
   for each item in the list of matches, we insert the match in an undoable
   fashion, with the appropriate character appended (this happens on the
   second and subsequent consecutive calls to rl_menu_complete).  When we
   hit the end of the match list, we restore the original unmatched text,
   ring the bell, and reset the counter to zero. */
int
rl_menu_complete (count, ignore)
     int count, ignore;
{
  rl_compentry_func_t *our_func;
  int matching_filenames, found_quote;

  static char *orig_text;
  static char **matches = (char **)0;
  static int match_list_index = 0;
  static int match_list_size = 0;
  static int orig_start, orig_end;
  static char quote_char;
  static int delimiter;

  /* The first time through, we generate the list of matches and set things
     up to insert them. */
  if (rl_last_func != rl_menu_complete)
    {
      /* Clean up from previous call, if any. */
      FREE (orig_text);
      if (matches)
	_rl_free_match_list (matches);

      match_list_index = match_list_size = 0;
      matches = (char **)NULL;

      /* Only the completion entry function can change these. */
      set_completion_defaults ('%');

      our_func = rl_completion_entry_function
			? rl_completion_entry_function
			: rl_filename_completion_function;

      /* We now look backwards for the start of a filename/variable word. */
      orig_end = rl_point;
      found_quote = delimiter = 0;
      quote_char = '\0';

      if (rl_point)
	/* This (possibly) changes rl_point.  If it returns a non-zero char,
	   we know we have an open quote. */
	quote_char = _rl_find_completion_word (&found_quote, &delimiter);

      orig_start = rl_point;
      rl_point = orig_end;

      orig_text = rl_copy_text (orig_start, orig_end);
      matches = gen_completion_matches (orig_text, orig_start, orig_end,
					our_func, found_quote, quote_char);

      /* If we are matching filenames, the attempted completion function will
	 have set rl_filename_completion_desired to a non-zero value.  The basic
	 rl_filename_completion_function does this. */
      matching_filenames = rl_filename_completion_desired;

      if (matches == 0 || postprocess_matches (&matches, matching_filenames) == 0)
	{
	  rl_ding ();
	  FREE (matches);
	  matches = (char **)0;
	  FREE (orig_text);
	  orig_text = (char *)0;
	  completion_changed_buffer = 0;
          return (0);
	}

      for (match_list_size = 0; matches[match_list_size]; match_list_size++)
        ;
      /* matches[0] is lcd if match_list_size > 1, but the circular buffer
	 code below should take care of it. */
    }

  /* Now we have the list of matches.  Replace the text between
     rl_line_buffer[orig_start] and rl_line_buffer[rl_point] with
     matches[match_list_index], and add any necessary closing char. */

  if (matches == 0 || match_list_size == 0)
    {
      rl_ding ();
      FREE (matches);
      matches = (char **)0;
      completion_changed_buffer = 0;
      return (0);
    }

  match_list_index = (match_list_index + count) % match_list_size;
  if (match_list_index < 0)
    match_list_index += match_list_size;

  if (match_list_index == 0 && match_list_size > 1)
    {
      rl_ding ();
      insert_match (orig_text, orig_start, MULT_MATCH, &quote_char);
    }
  else
    {
      insert_match (matches[match_list_index], orig_start, SINGLE_MATCH, &quote_char);
      append_to_match (matches[match_list_index], delimiter, quote_char,
		       strcmp (orig_text, matches[match_list_index]));
    }

  completion_changed_buffer = 1;
  return (0);
}
