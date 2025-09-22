/* tilde.c -- Tilde expansion code (~/foo := $HOME/foo). */

/* Copyright (C) 1988,1989 Free Software Foundation, Inc.

   This file is part of GNU Readline, a library for reading lines
   of text with interactive input and history editing.

   Readline is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2, or (at your option) any
   later version.

   Readline is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Readline; see the file COPYING.  If not, write to the Free
   Software Foundation, 59 Temple Place, Suite 330, Boston, MA 02111 USA. */

#if defined (HAVE_CONFIG_H)
#  include <config.h>
#endif

#if defined (HAVE_UNISTD_H)
#  ifdef _MINIX
#    include <sys/types.h>
#  endif
#  include <unistd.h>
#endif

#if defined (HAVE_STRING_H)
#  include <string.h>
#else /* !HAVE_STRING_H */
#  include <strings.h>
#endif /* !HAVE_STRING_H */

#if defined (HAVE_STDLIB_H)
#  include <stdlib.h>
#else
#  include "ansi_stdlib.h"
#endif /* HAVE_STDLIB_H */

#include <sys/types.h>
#include <pwd.h>

#include "tilde.h"

#if defined (TEST) || defined (STATIC_MALLOC)
static void *xmalloc (), *xrealloc ();
#else
#  include "xmalloc.h"
#endif /* TEST || STATIC_MALLOC */

#if !defined (HAVE_GETPW_DECLS)
extern struct passwd *getpwuid PARAMS((uid_t));
extern struct passwd *getpwnam PARAMS((const char *));
#endif /* !HAVE_GETPW_DECLS */

#if !defined (savestring)
#include <stdio.h>
static char *
xstrdup(const char *s)
{
	char * cp;
	cp = strdup(s);
	if (cp == NULL) {
		fprintf (stderr, "xstrdup: out of virtual memory\n");
		exit (2);
	}
	return(cp);
}
#define savestring(x) xstrdup(x)
#endif /* !savestring  */

#if !defined (NULL)
#  if defined (__STDC__)
#    define NULL ((void *) 0)
#  else
#    define NULL 0x0
#  endif /* !__STDC__ */
#endif /* !NULL */

/* If being compiled as part of bash, these will be satisfied from
   variables.o.  If being compiled as part of readline, they will
   be satisfied from shell.o. */
extern char *sh_get_home_dir PARAMS((void));
extern char *sh_get_env_value PARAMS((const char *));

/* The default value of tilde_additional_prefixes.  This is set to
   whitespace preceding a tilde so that simple programs which do not
   perform any word separation get desired behaviour. */
static const char *default_prefixes[] =
  { " ~", "\t~", (const char *)NULL };

/* The default value of tilde_additional_suffixes.  This is set to
   whitespace or newline so that simple programs which do not
   perform any word separation get desired behaviour. */
static const char *default_suffixes[] =
  { " ", "\n", (const char *)NULL };

/* If non-null, this contains the address of a function that the application
   wants called before trying the standard tilde expansions.  The function
   is called with the text sans tilde, and returns a malloc()'ed string
   which is the expansion, or a NULL pointer if the expansion fails. */
tilde_hook_func_t *tilde_expansion_preexpansion_hook = (tilde_hook_func_t *)NULL;

/* If non-null, this contains the address of a function to call if the
   standard meaning for expanding a tilde fails.  The function is called
   with the text (sans tilde, as in "foo"), and returns a malloc()'ed string
   which is the expansion, or a NULL pointer if there is no expansion. */
tilde_hook_func_t *tilde_expansion_failure_hook = (tilde_hook_func_t *)NULL;

/* When non-null, this is a NULL terminated array of strings which
   are duplicates for a tilde prefix.  Bash uses this to expand
   `=~' and `:~'. */
char **tilde_additional_prefixes = (char **)default_prefixes;

/* When non-null, this is a NULL terminated array of strings which match
   the end of a username, instead of just "/".  Bash sets this to
   `:' and `=~'. */
char **tilde_additional_suffixes = (char **)default_suffixes;

static int tilde_find_prefix PARAMS((const char *, int *));
static int tilde_find_suffix PARAMS((const char *));
static char *isolate_tilde_prefix PARAMS((const char *, int *));
static char *glue_prefix_and_suffix PARAMS((char *, const char *, int));

/* Find the start of a tilde expansion in STRING, and return the index of
   the tilde which starts the expansion.  Place the length of the text
   which identified this tilde starter in LEN, excluding the tilde itself. */
static int
tilde_find_prefix (string, len)
     const char *string;
     int *len;
{
  register int i, j, string_len;
  register char **prefixes;

  prefixes = tilde_additional_prefixes;

  string_len = strlen (string);
  *len = 0;

  if (*string == '\0' || *string == '~')
    return (0);

  if (prefixes)
    {
      for (i = 0; i < string_len; i++)
	{
	  for (j = 0; prefixes[j]; j++)
	    {
	      if (strncmp (string + i, prefixes[j], strlen (prefixes[j])) == 0)
		{
		  *len = strlen (prefixes[j]) - 1;
		  return (i + *len);
		}
	    }
	}
    }
  return (string_len);
}

/* Find the end of a tilde expansion in STRING, and return the index of
   the character which ends the tilde definition.  */
static int
tilde_find_suffix (string)
     const char *string;
{
  register int i, j, string_len;
  register char **suffixes;

  suffixes = tilde_additional_suffixes;
  string_len = strlen (string);

  for (i = 0; i < string_len; i++)
    {
#if defined (__MSDOS__)
      if (string[i] == '/' || string[i] == '\\' /* || !string[i] */)
#else
      if (string[i] == '/' /* || !string[i] */)
#endif
	break;

      for (j = 0; suffixes && suffixes[j]; j++)
	{
	  if (strncmp (string + i, suffixes[j], strlen (suffixes[j])) == 0)
	    return (i);
	}
    }
  return (i);
}

/* Return a new string which is the result of tilde expanding STRING. */
char *
tilde_expand (string)
     const char *string;
{
  char *result;
  int result_size, result_index;

  result_index = result_size = 0;
  if ((result = strchr (string, '~')))
    result = (char *)xmalloc (result_size = (strlen (string) + 16));
  else
    result = (char *)xmalloc (result_size = (strlen (string) + 1));

  /* Scan through STRING expanding tildes as we come to them. */
  while (1)
    {
      register int start, end;
      char *tilde_word, *expansion;
      int len;

      /* Make START point to the tilde which starts the expansion. */
      start = tilde_find_prefix (string, &len);

      /* Copy the skipped text into the result. */
      if ((result_index + start + 1) > result_size)
	result = (char *)xrealloc (result, 1 + (result_size += (start + 20)));

      strncpy (result + result_index, string, start);
      result_index += start;

      /* Advance STRING to the starting tilde. */
      string += start;

      /* Make END be the index of one after the last character of the
	 username. */
      end = tilde_find_suffix (string);

      /* If both START and END are zero, we are all done. */
      if (!start && !end)
	break;

      /* Expand the entire tilde word, and copy it into RESULT. */
      tilde_word = (char *)xmalloc (1 + end);
      strncpy (tilde_word, string, end);
      tilde_word[end] = '\0';
      string += end;

      expansion = tilde_expand_word (tilde_word);
      free (tilde_word);

      len = strlen (expansion);
#ifdef __CYGWIN__
      /* Fix for Cygwin to prevent ~user/xxx from expanding to //xxx when
	 $HOME for `user' is /.  On cygwin, // denotes a network drive. */
      if (len > 1 || *expansion != '/' || *string != '/')
#endif
	{
	  if ((result_index + len + 1) > result_size)
	    result = (char *)xrealloc (result, 1 + (result_size += (len + 20)));

	  strlcpy (result + result_index, expansion,
		   result_size - result_index);
	  result_index += len;
	}
      free (expansion);
    }

  result[result_index] = '\0';

  return (result);
}

/* Take FNAME and return the tilde prefix we want expanded.  If LENP is
   non-null, the index of the end of the prefix into FNAME is returned in
   the location it points to. */
static char *
isolate_tilde_prefix (fname, lenp)
     const char *fname;
     int *lenp;
{
  char *ret;
  int i;

  ret = (char *)xmalloc (strlen (fname));
#if defined (__MSDOS__)
  for (i = 1; fname[i] && fname[i] != '/' && fname[i] != '\\'; i++)
#else
  for (i = 1; fname[i] && fname[i] != '/'; i++)
#endif
    ret[i - 1] = fname[i];
  ret[i - 1] = '\0';
  if (lenp)
    *lenp = i;
  return ret;
}

/* Return a string that is PREFIX concatenated with SUFFIX starting at
   SUFFIND. */
static char *
glue_prefix_and_suffix (prefix, suffix, suffind)
     char *prefix;
     const char *suffix;
     int suffind;
{
  char *ret;
  int plen, slen;

  plen = (prefix && *prefix) ? strlen (prefix) : 0;
  slen = strlen (suffix + suffind);
  ret = (char *)xmalloc (plen + slen + 1);
  if (plen)
    strlcpy (ret, prefix, plen + slen + 1);
  strlcat (ret, suffix + suffind, plen + slen + 1);
  return ret;
}

/* Do the work of tilde expansion on FILENAME.  FILENAME starts with a
   tilde.  If there is no expansion, call tilde_expansion_failure_hook.
   This always returns a newly-allocated string, never static storage. */
char *
tilde_expand_word (filename)
     const char *filename;
{
  char *dirname, *expansion, *username;
  int user_len;
  struct passwd *user_entry;

  if (filename == 0)
    return ((char *)NULL);

  if (*filename != '~')
    return (savestring (filename));

  /* A leading `~/' or a bare `~' is *always* translated to the value of
     $HOME or the home directory of the current user, regardless of any
     preexpansion hook. */
  if (filename[1] == '\0' || filename[1] == '/')
    {
      /* Prefix $HOME to the rest of the string. */
      expansion = sh_get_env_value ("HOME");

      /* If there is no HOME variable, look up the directory in
	 the password database. */
      if (expansion == 0 || *expansion == '\0')
	expansion = sh_get_home_dir ();

      return (glue_prefix_and_suffix (expansion, filename, 1));
    }

  username = isolate_tilde_prefix (filename, &user_len);

  if (tilde_expansion_preexpansion_hook)
    {
      expansion = (*tilde_expansion_preexpansion_hook) (username);
      if (expansion)
	{
	  dirname = glue_prefix_and_suffix (expansion, filename, user_len);
	  free (username);
	  free (expansion);
	  return (dirname);
	}
    }

  /* No preexpansion hook, or the preexpansion hook failed.  Look in the
     password database. */
  dirname = (char *)NULL;
  user_entry = getpwnam (username);
  if (user_entry == 0)
    {
      /* If the calling program has a special syntax for expanding tildes,
	 and we couldn't find a standard expansion, then let them try. */
      if (tilde_expansion_failure_hook)
	{
	  expansion = (*tilde_expansion_failure_hook) (username);
	  if (expansion)
	    {
	      dirname = glue_prefix_and_suffix (expansion, filename, user_len);
	      free (expansion);
	    }
	}
      free (username);
      /* If we don't have a failure hook, or if the failure hook did not
	 expand the tilde, return a copy of what we were passed. */
      if (dirname == 0)
	dirname = savestring (filename);
    }
  else
    {
      free (username);
      dirname = glue_prefix_and_suffix (user_entry->pw_dir, filename, user_len);
    }

  endpwent ();
  return (dirname);
}


#if defined (TEST)
#undef NULL
#include <stdio.h>

main (argc, argv)
     int argc;
     char **argv;
{
  char *result, line[512];
  int done = 0;

  while (!done)
    {
      printf ("~expand: ");
      fflush (stdout);

      if (!fgets (line, sizeof(line), stdin))
	strlcpy (line, "done", sizeof(line));
      else if (line[strlen(line) - 1] == '\n')
	      line[strlen(line) - 1] = '\0';
      if ((strcmp (line, "done") == 0) ||
	  (strcmp (line, "quit") == 0) ||
	  (strcmp (line, "exit") == 0))
	{
	  done = 1;
	  break;
	}

      result = tilde_expand (line);
      printf ("  --> %s\n", result);
      free (result);
    }
  exit (0);
}

static void memory_error_and_abort ();

static void *
xmalloc (bytes)
     size_t bytes;
{
  void *temp = (char *)malloc (bytes);

  if (!temp)
    memory_error_and_abort ();
  return (temp);
}

static void *
xrealloc (pointer, bytes)
     void *pointer;
     int bytes;
{
  void *temp;

  if (!pointer)
    temp = malloc (bytes);
  else
    temp = realloc (pointer, bytes);

  if (!temp)
    memory_error_and_abort ();

  return (temp);
}

static void
memory_error_and_abort ()
{
  fprintf (stderr, "readline: out of virtual memory\n");
  abort ();
}

/*
 * Local variables:
 * compile-command: "gcc -g -DTEST -o tilde tilde.c"
 * end:
 */
#endif /* TEST */
