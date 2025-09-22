/* histexpand.c -- history expansion. */

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

#define READLINE_LIBRARY

#if defined (HAVE_CONFIG_H)
#  include <config.h>
#endif

#include <stdio.h>

#if defined (HAVE_STDLIB_H)
#  include <stdlib.h>
#else
#  include "ansi_stdlib.h"
#endif /* HAVE_STDLIB_H */

#if defined (HAVE_UNISTD_H)
#  ifndef _MINIX
#    include <sys/types.h>
#  endif
#  include <unistd.h>
#endif

#include "rlmbutil.h"

#include "history.h"
#include "histlib.h"

#include "rlshell.h"
#include "xmalloc.h"

#define HISTORY_WORD_DELIMITERS		" \t\n;&()|<>"
#define HISTORY_QUOTE_CHARACTERS	"\"'`"

typedef int _hist_search_func_t PARAMS((const char *, int));

extern int rl_byte_oriented;	/* declared in mbutil.c */

static char error_pointer;

static char *subst_lhs;
static char *subst_rhs;
static int subst_lhs_len;
static int subst_rhs_len;

static char *get_history_word_specifier PARAMS((char *, char *, int *));
static char *history_find_word PARAMS((char *, int));

static char *quote_breaks PARAMS((char *));

/* Variables exported by this file. */
/* The character that represents the start of a history expansion
   request.  This is usually `!'. */
char history_expansion_char = '!';

/* The character that invokes word substitution if found at the start of
   a line.  This is usually `^'. */
char history_subst_char = '^';

/* During tokenization, if this character is seen as the first character
   of a word, then it, and all subsequent characters upto a newline are
   ignored.  For a Bourne shell, this should be '#'.  Bash special cases
   the interactive comment character to not be a comment delimiter. */
char history_comment_char = '\0';

/* The list of characters which inhibit the expansion of text if found
   immediately following history_expansion_char. */
char *history_no_expand_chars = " \t\n\r=";

/* If set to a non-zero value, single quotes inhibit history expansion.
   The default is 0. */
int history_quotes_inhibit_expansion = 0;

/* Used to split words by history_tokenize_internal. */
char *history_word_delimiters = HISTORY_WORD_DELIMITERS;

/* If set, this points to a function that is called to verify that a
   particular history expansion should be performed. */
rl_linebuf_func_t *history_inhibit_expansion_function;

/* **************************************************************** */
/*								    */
/*			History Expansion			    */
/*								    */
/* **************************************************************** */

/* Hairy history expansion on text, not tokens.  This is of general
   use, and thus belongs in this library. */

/* The last string searched for by a !?string? search. */
static char *search_string;

/* The last string matched by a !?string? search. */
static char *search_match;

/* Return the event specified at TEXT + OFFSET modifying OFFSET to
   point to after the event specifier.  Just a pointer to the history
   line is returned; NULL is returned in the event of a bad specifier.
   You pass STRING with *INDEX equal to the history_expansion_char that
   begins this specification.
   DELIMITING_QUOTE is a character that is allowed to end the string
   specification for what to search for in addition to the normal
   characters `:', ` ', `\t', `\n', and sometimes `?'.
   So you might call this function like:
   line = get_history_event ("!echo:p", &index, 0);  */
char *
get_history_event (string, caller_index, delimiting_quote)
     const char *string;
     int *caller_index;
     int delimiting_quote;
{
  register int i;
  register char c;
  HIST_ENTRY *entry;
  int which, sign, local_index, substring_okay;
  _hist_search_func_t *search_func;
  char *temp;

  /* The event can be specified in a number of ways.

     !!   the previous command
     !n   command line N
     !-n  current command-line minus N
     !str the most recent command starting with STR
     !?str[?]
	  the most recent command containing STR

     All values N are determined via HISTORY_BASE. */

  i = *caller_index;

  if (string[i] != history_expansion_char)
    return ((char *)NULL);

  /* Move on to the specification. */
  i++;

  sign = 1;
  substring_okay = 0;

#define RETURN_ENTRY(e, w) \
	return ((e = history_get (w)) ? e->line : (char *)NULL)

  /* Handle !! case. */
  if (string[i] == history_expansion_char)
    {
      i++;
      which = history_base + (history_length - 1);
      *caller_index = i;
      RETURN_ENTRY (entry, which);
    }

  /* Hack case of numeric line specification. */
  if (string[i] == '-')
    {
      sign = -1;
      i++;
    }

  if (_rl_digit_p (string[i]))
    {
      /* Get the extent of the digits and compute the value. */
      for (which = 0; _rl_digit_p (string[i]); i++)
	which = (which * 10) + _rl_digit_value (string[i]);

      *caller_index = i;

      if (sign < 0)
	which = (history_length + history_base) - which;

      RETURN_ENTRY (entry, which);
    }

  /* This must be something to search for.  If the spec begins with
     a '?', then the string may be anywhere on the line.  Otherwise,
     the string must be found at the start of a line. */
  if (string[i] == '?')
    {
      substring_okay++;
      i++;
    }

  /* Only a closing `?' or a newline delimit a substring search string. */
  for (local_index = i; (c = string[i]); i++)
#if defined (HANDLE_MULTIBYTE)
    if (MB_CUR_MAX > 1 && rl_byte_oriented == 0)
      {
	int v;
	mbstate_t ps;

	memset (&ps, 0, sizeof (mbstate_t));
	/* These produce warnings because we're passing a const string to a
	   function that takes a non-const string. */
	_rl_adjust_point (string, i, &ps);
	if ((v = _rl_get_char_len (string + i, &ps)) > 1)
	  {
	    i += v - 1;
	    continue;
	  }
      }
    else
#endif /* HANDLE_MULTIBYTE */
      if ((!substring_okay && (whitespace (c) || c == ':' ||
	  (history_search_delimiter_chars && member (c, history_search_delimiter_chars)) ||
	  string[i] == delimiting_quote)) ||
	  string[i] == '\n' ||
	  (substring_okay && string[i] == '?'))
	break;

  which = i - local_index;
  temp = (char *)xmalloc (1 + which);
  if (which)
    strncpy (temp, string + local_index, which);
  temp[which] = '\0';

  if (substring_okay && string[i] == '?')
    i++;

  *caller_index = i;

#define FAIL_SEARCH() \
  do { \
    history_offset = history_length; free (temp) ; return (char *)NULL; \
  } while (0)

  /* If there is no search string, try to use the previous search string,
     if one exists.  If not, fail immediately. */
  if (*temp == '\0' && substring_okay)
    {
      if (search_string)
        {
          free (temp);
          temp = savestring (search_string);
        }
      else
        FAIL_SEARCH ();
    }

  search_func = substring_okay ? history_search : history_search_prefix;
  while (1)
    {
      local_index = (*search_func) (temp, -1);

      if (local_index < 0)
	FAIL_SEARCH ();

      if (local_index == 0 || substring_okay)
	{
	  entry = current_history ();
	  history_offset = history_length;

	  /* If this was a substring search, then remember the
	     string that we matched for word substitution. */
	  if (substring_okay)
	    {
	      FREE (search_string);
	      search_string = temp;

	      FREE (search_match);
	      search_match = history_find_word (entry->line, local_index);
	    }
	  else
	    free (temp);

	  return (entry->line);
	}

      if (history_offset)
	history_offset--;
      else
	FAIL_SEARCH ();
    }
#undef FAIL_SEARCH
#undef RETURN_ENTRY
}

/* Function for extracting single-quoted strings.  Used for inhibiting
   history expansion within single quotes. */

/* Extract the contents of STRING as if it is enclosed in single quotes.
   SINDEX, when passed in, is the offset of the character immediately
   following the opening single quote; on exit, SINDEX is left pointing
   to the closing single quote. */
static void
hist_string_extract_single_quoted (string, sindex)
     char *string;
     int *sindex;
{
  register int i;

  for (i = *sindex; string[i] && string[i] != '\''; i++)
    ;

  *sindex = i;
}

static char *
quote_breaks (s)
     char *s;
{
  register char *p, *r;
  char *ret;
  int len = 3;

  for (p = s; p && *p; p++, len++)
    {
      if (*p == '\'')
	len += 3;
      else if (whitespace (*p) || *p == '\n')
	len += 2;
    }

  r = ret = (char *)xmalloc (len);
  *r++ = '\'';
  for (p = s; p && *p; )
    {
      if (*p == '\'')
	{
	  *r++ = '\'';
	  *r++ = '\\';
	  *r++ = '\'';
	  *r++ = '\'';
	  p++;
	}
      else if (whitespace (*p) || *p == '\n')
	{
	  *r++ = '\'';
	  *r++ = *p++;
	  *r++ = '\'';
	}
      else
	*r++ = *p++;
    }
  *r++ = '\'';
  *r = '\0';
  return ret;
}

static char *
hist_error(s, start, current, errtype)
      char *s;
      int start, current, errtype;
{
  char *temp;
  const char *emsg;
  int ll, elen, len;

  ll = current - start;

  switch (errtype)
    {
    case EVENT_NOT_FOUND:
      emsg = "event not found";
      elen = 15;
      break;
    case BAD_WORD_SPEC:
      emsg = "bad word specifier";
      elen = 18;
      break;
    case SUBST_FAILED:
      emsg = "substitution failed";
      elen = 19;
      break;
    case BAD_MODIFIER:
      emsg = "unrecognized history modifier";
      elen = 29;
      break;
    case NO_PREV_SUBST:
      emsg = "no previous substitution";
      elen = 24;
      break;
    default:
      emsg = "unknown expansion error";
      elen = 23;
      break;
    }

  len = ll + elen + 3;
  temp = (char *)xmalloc (len);
  strncpy (temp, s + start, ll);
  strlcat (temp, ": ", len);
  strlcat (temp, emsg, len);
  return (temp);
}

/* Get a history substitution string from STR starting at *IPTR
   and return it.  The length is returned in LENPTR.

   A backslash can quote the delimiter.  If the string is the
   empty string, the previous pattern is used.  If there is
   no previous pattern for the lhs, the last history search
   string is used.

   If IS_RHS is 1, we ignore empty strings and set the pattern
   to "" anyway.  subst_lhs is not changed if the lhs is empty;
   subst_rhs is allowed to be set to the empty string. */

static char *
get_subst_pattern (str, iptr, delimiter, is_rhs, lenptr)
     char *str;
     int *iptr, delimiter, is_rhs, *lenptr;
{
  register int si, i, j, k;
  char *s;
#if defined (HANDLE_MULTIBYTE)
  mbstate_t ps;
#endif

  s = (char *)NULL;
  i = *iptr;

#if defined (HANDLE_MULTIBYTE)
  memset (&ps, 0, sizeof (mbstate_t));
  _rl_adjust_point (str, i, &ps);
#endif

  for (si = i; str[si] && str[si] != delimiter; si++)
#if defined (HANDLE_MULTIBYTE)
    if (MB_CUR_MAX > 1 && rl_byte_oriented == 0)
      {
	int v;
	if ((v = _rl_get_char_len (str + si, &ps)) > 1)
	  si += v - 1;
	else if (str[si] == '\\' && str[si + 1] == delimiter)
	  si++;
      }
    else
#endif /* HANDLE_MULTIBYTE */
      if (str[si] == '\\' && str[si + 1] == delimiter)
	si++;

  if (si > i || is_rhs)
    {
      s = (char *)xmalloc (si - i + 1);
      for (j = 0, k = i; k < si; j++, k++)
	{
	  /* Remove a backslash quoting the search string delimiter. */
	  if (str[k] == '\\' && str[k + 1] == delimiter)
	    k++;
	  s[j] = str[k];
	}
      s[j] = '\0';
      if (lenptr)
	*lenptr = j;
    }

  i = si;
  if (str[i])
    i++;
  *iptr = i;

  return s;
}

static void
postproc_subst_rhs ()
{
  char *new;
  int i, j, new_size;

  new = (char *)xmalloc (new_size = subst_rhs_len + subst_lhs_len);
  for (i = j = 0; i < subst_rhs_len; i++)
    {
      if (subst_rhs[i] == '&')
	{
	  if (j + subst_lhs_len >= new_size)
	    new = (char *)xrealloc (new, (new_size = new_size * 2 + subst_lhs_len));
	  strlcpy (new + j, subst_lhs, new_size - j);
	  j += subst_lhs_len;
	}
      else
	{
	  /* a single backslash protects the `&' from lhs interpolation */
	  if (subst_rhs[i] == '\\' && subst_rhs[i + 1] == '&')
	    i++;
	  if (j >= new_size)
	    new = (char *)xrealloc (new, new_size *= 2);
	  new[j++] = subst_rhs[i];
	}
    }
  new[j] = '\0';
  free (subst_rhs);
  subst_rhs = new;
  subst_rhs_len = j;
}

/* Expand the bulk of a history specifier starting at STRING[START].
   Returns 0 if everything is OK, -1 if an error occurred, and 1
   if the `p' modifier was supplied and the caller should just print
   the returned string.  Returns the new index into string in
   *END_INDEX_PTR, and the expanded specifier in *RET_STRING. */
static int
history_expand_internal (string, start, end_index_ptr, ret_string, current_line)
     char *string;
     int start, *end_index_ptr;
     char **ret_string;
     char *current_line;	/* for !# */
{
  int i, n, starting_index;
  int substitute_globally, want_quotes, print_only;
  char *event, *temp, *result, *tstr, *t, c, *word_spec;
  int result_len;
#if defined (HANDLE_MULTIBYTE)
  mbstate_t ps;

  memset (&ps, 0, sizeof (mbstate_t));
#endif

  result = (char *)xmalloc (result_len = 128);

  i = start;

  /* If it is followed by something that starts a word specifier,
     then !! is implied as the event specifier. */

  if (member (string[i + 1], ":$*%^"))
    {
      char fake_s[3];
      int fake_i = 0;
      i++;
      fake_s[0] = fake_s[1] = history_expansion_char;
      fake_s[2] = '\0';
      event = get_history_event (fake_s, &fake_i, 0);
    }
  else if (string[i + 1] == '#')
    {
      i += 2;
      event = current_line;
    }
  else
    {
      int quoted_search_delimiter = 0;

      /* If the character before this `!' is a double or single
	 quote, then this expansion takes place inside of the
	 quoted string.  If we have to search for some text ("!foo"),
	 allow the delimiter to end the search string. */
#if defined (HANDLE_MULTIBYTE)
      if (MB_CUR_MAX > 1 && rl_byte_oriented == 0)
	{
	  int c, l;
	  l = _rl_find_prev_mbchar (string, i, MB_FIND_ANY);
	  c = string[l];
	  /* XXX - original patch had i - 1 ???  If i == 0 it would fail. */
	  if (i && (c == '\'' || c == '"'))
	    quoted_search_delimiter = c;
	}
      else
#endif /* HANDLE_MULTIBYTE */
	if (i && (string[i - 1] == '\'' || string[i - 1] == '"'))
	  quoted_search_delimiter = string[i - 1];

      event = get_history_event (string, &i, quoted_search_delimiter);
    }

  if (event == 0)
    {
      *ret_string = hist_error (string, start, i, EVENT_NOT_FOUND);
      free (result);
      return (-1);
    }

  /* If a word specifier is found, then do what that requires. */
  starting_index = i;
  word_spec = get_history_word_specifier (string, event, &i);

  /* There is no such thing as a `malformed word specifier'.  However,
     it is possible for a specifier that has no match.  In that case,
     we complain. */
  if (word_spec == (char *)&error_pointer)
    {
      *ret_string = hist_error (string, starting_index, i, BAD_WORD_SPEC);
      free (result);
      return (-1);
    }

  /* If no word specifier, than the thing of interest was the event. */
  temp = word_spec ? savestring (word_spec) : savestring (event);
  FREE (word_spec);

  /* Perhaps there are other modifiers involved.  Do what they say. */
  want_quotes = substitute_globally = print_only = 0;
  starting_index = i;

  while (string[i] == ':')
    {
      c = string[i + 1];

      if (c == 'g')
	{
	  substitute_globally = 1;
	  i++;
	  c = string[i + 1];
	}

      switch (c)
	{
	default:
	  *ret_string = hist_error (string, i+1, i+2, BAD_MODIFIER);
	  free (result);
	  free (temp);
	  return -1;

	case 'q':
	  want_quotes = 'q';
	  break;

	case 'x':
	  want_quotes = 'x';
	  break;

	  /* :p means make this the last executed line.  So we
	     return an error state after adding this line to the
	     history. */
	case 'p':
	  print_only++;
	  break;

	  /* :t discards all but the last part of the pathname. */
	case 't':
	  tstr = strrchr (temp, '/');
	  if (tstr)
	    {
	      tstr++;
	      t = savestring (tstr);
	      free (temp);
	      temp = t;
	    }
	  break;

	  /* :h discards the last part of a pathname. */
	case 'h':
	  tstr = strrchr (temp, '/');
	  if (tstr)
	    *tstr = '\0';
	  break;

	  /* :r discards the suffix. */
	case 'r':
	  tstr = strrchr (temp, '.');
	  if (tstr)
	    *tstr = '\0';
	  break;

	  /* :e discards everything but the suffix. */
	case 'e':
	  tstr = strrchr (temp, '.');
	  if (tstr)
	    {
	      t = savestring (tstr);
	      free (temp);
	      temp = t;
	    }
	  break;

	/* :s/this/that substitutes `that' for the first
	   occurrence of `this'.  :gs/this/that substitutes `that'
	   for each occurrence of `this'.  :& repeats the last
	   substitution.  :g& repeats the last substitution
	   globally. */

	case '&':
	case 's':
	  {
	    char *new_event;
	    int delimiter, failed, si, l_temp;

	    if (c == 's')
	      {
		if (i + 2 < (int)strlen (string))
		  {
#if defined (HANDLE_MULTIBYTE)
		    if (MB_CUR_MAX > 1 && rl_byte_oriented == 0)
		      {
			_rl_adjust_point (string, i + 2, &ps);
			if (_rl_get_char_len (string + i + 2, &ps) > 1)
			  delimiter = 0;
			else
			  delimiter = string[i + 2];
		      }
		    else
#endif /* HANDLE_MULTIBYTE */
		      delimiter = string[i + 2];
		  }
		else
		  break;	/* no search delimiter */

		i += 3;

		t = get_subst_pattern (string, &i, delimiter, 0, &subst_lhs_len);
		/* An empty substitution lhs with no previous substitution
		   uses the last search string as the lhs. */
		if (t)
		  {
		    FREE (subst_lhs);
		    subst_lhs = t;
		  }
		else if (!subst_lhs)
		  {
		    if (search_string && *search_string)
		      {
			subst_lhs = savestring (search_string);
			subst_lhs_len = strlen (subst_lhs);
		      }
		    else
		      {
			subst_lhs = (char *) NULL;
			subst_lhs_len = 0;
		      }
		  }

		FREE (subst_rhs);
		subst_rhs = get_subst_pattern (string, &i, delimiter, 1, &subst_rhs_len);

		/* If `&' appears in the rhs, it's supposed to be replaced
		   with the lhs. */
		if (member ('&', subst_rhs))
		  postproc_subst_rhs ();
	      }
	    else
	      i += 2;

	    /* If there is no lhs, the substitution can't succeed. */
	    if (subst_lhs_len == 0)
	      {
		*ret_string = hist_error (string, starting_index, i, NO_PREV_SUBST);
		free (result);
		free (temp);
		return -1;
	      }

	    l_temp = strlen (temp);
	    /* Ignore impossible cases. */
	    if (subst_lhs_len > l_temp)
	      {
		*ret_string = hist_error (string, starting_index, i, SUBST_FAILED);
		free (result);
		free (temp);
		return (-1);
	      }

	    /* Find the first occurrence of THIS in TEMP. */
	    si = 0;
	    for (failed = 1; (si + subst_lhs_len) <= l_temp; si++)
	      if (STREQN (temp+si, subst_lhs, subst_lhs_len))
		{
		  int len = subst_rhs_len - subst_lhs_len + l_temp;
		  new_event = (char *)xmalloc (1 + len);
		  strncpy (new_event, temp, si);
		  strncpy (new_event + si, subst_rhs, subst_rhs_len);
		  strncpy (new_event + si + subst_rhs_len,
			   temp + si + subst_lhs_len,
			   l_temp - (si + subst_lhs_len));
		  new_event[len] = '\0';
		  free (temp);
		  temp = new_event;

		  failed = 0;

		  if (substitute_globally)
		    {
		      si += subst_rhs_len;
		      l_temp = strlen (temp);
		      substitute_globally++;
		      continue;
		    }
		  else
		    break;
		}

	    if (substitute_globally > 1)
	      {
		substitute_globally = 0;
		continue;	/* don't want to increment i */
	      }

	    if (failed == 0)
	      continue;		/* don't want to increment i */

	    *ret_string = hist_error (string, starting_index, i, SUBST_FAILED);
	    free (result);
	    free (temp);
	    return (-1);
	  }
	}
      i += 2;
    }
  /* Done with modfiers. */
  /* Believe it or not, we have to back the pointer up by one. */
  --i;

  if (want_quotes)
    {
      char *x;

      if (want_quotes == 'q')
	x = sh_single_quote (temp);
      else if (want_quotes == 'x')
	x = quote_breaks (temp);
      else
	x = savestring (temp);

      free (temp);
      temp = x;
    }

  n = strlen (temp);
  if (n >= result_len)
    result = (char *)xrealloc (result, n + 2);
  strlcpy (result, temp, n + 2);
  free (temp);

  *end_index_ptr = i;
  *ret_string = result;
  return (print_only);
}

/* Expand the string STRING, placing the result into OUTPUT, a pointer
   to a string.  Returns:

  -1) If there was an error in expansion.
   0) If no expansions took place (or, if the only change in
      the text was the de-slashifying of the history expansion
      character)
   1) If expansions did take place
   2) If the `p' modifier was given and the caller should print the result

  If an error ocurred in expansion, then OUTPUT contains a descriptive
  error message. */

#define ADD_STRING(s) \
	do \
	  { \
	    int sl = strlen (s); \
	    j += sl; \
	    if (j >= result_len) \
	      { \
		while (j >= result_len) \
		  result_len += 128; \
		result = (char *)xrealloc (result, result_len); \
	      } \
	    strlcpy (result + j - sl, s, result_len - j + sl); \
	  } \
	while (0)

#define ADD_CHAR(c) \
	do \
	  { \
	    if (j >= result_len - 1) \
	      result = (char *)xrealloc (result, result_len += 64); \
	    result[j++] = c; \
	    result[j] = '\0'; \
	  } \
	while (0)

int
history_expand (hstring, output)
     char *hstring;
     char **output;
{
  register int j;
  int i, r, l, passc, cc, modified, eindex, only_printing;
  char *string;

  /* The output string, and its length. */
  int result_len;
  char *result;

#if defined (HANDLE_MULTIBYTE)
  char mb[MB_LEN_MAX];
  mbstate_t ps;
#endif

  /* Used when adding the string. */
  char *temp;

  if (output == 0)
    return 0;

  /* Setting the history expansion character to 0 inhibits all
     history expansion. */
  if (history_expansion_char == 0)
    {
      *output = savestring (hstring);
      return (0);
    }

  /* Prepare the buffer for printing error messages. */
  result = (char *)xmalloc (result_len = 256);
  result[0] = '\0';

  only_printing = modified = 0;
  l = strlen (hstring);

  /* Grovel the string.  Only backslash and single quotes can quote the
     history escape character.  We also handle arg specifiers. */

  /* Before we grovel forever, see if the history_expansion_char appears
     anywhere within the text. */

  /* The quick substitution character is a history expansion all right.  That
     is to say, "^this^that^" is equivalent to "!!:s^this^that^", and in fact,
     that is the substitution that we do. */
  if (hstring[0] == history_subst_char)
    {
      string = (char *)xmalloc (l + 5);

      string[0] = string[1] = history_expansion_char;
      string[2] = ':';
      string[3] = 's';
      strlcpy (string + 4, hstring, l + 1);
      l += 4;
    }
  else
    {
#if defined (HANDLE_MULTIBYTE)
      memset (&ps, 0, sizeof (mbstate_t));
#endif

      string = hstring;
      /* If not quick substitution, still maybe have to do expansion. */

      /* `!' followed by one of the characters in history_no_expand_chars
	 is NOT an expansion. */
      for (i = 0; string[i]; i++)
	{
#if defined (HANDLE_MULTIBYTE)
	  if (MB_CUR_MAX > 1 && rl_byte_oriented == 0)
	    {
	      int v;
	      v = _rl_get_char_len (string + i, &ps);
	      if (v > 1)
		{
		  i += v - 1;
		  continue;
		}
	    }
#endif /* HANDLE_MULTIBYTE */

	  cc = string[i + 1];
	  /* The history_comment_char, if set, appearing at the beginning
	     of a word signifies that the rest of the line should not have
	     history expansion performed on it.
	     Skip the rest of the line and break out of the loop. */
	  if (history_comment_char && string[i] == history_comment_char &&
	      (i == 0 || member (string[i - 1], history_word_delimiters)))
	    {
	      while (string[i])
		i++;
	      break;
	    }
	  else if (string[i] == history_expansion_char)
	    {
	      if (!cc || member (cc, history_no_expand_chars))
		continue;
	      /* If the calling application has set
		 history_inhibit_expansion_function to a function that checks
		 for special cases that should not be history expanded,
		 call the function and skip the expansion if it returns a
		 non-zero value. */
	      else if (history_inhibit_expansion_function &&
			(*history_inhibit_expansion_function) (string, i))
		continue;
	      else
		break;
	    }
	  /* XXX - at some point, might want to extend this to handle
		   double quotes as well. */
	  else if (history_quotes_inhibit_expansion && string[i] == '\'')
	    {
	      /* If this is bash, single quotes inhibit history expansion. */
	      i++;
	      hist_string_extract_single_quoted (string, &i);
	    }
	  else if (history_quotes_inhibit_expansion && string[i] == '\\')
	    {
	      /* If this is bash, allow backslashes to quote single
		 quotes and the history expansion character. */
	      if (cc == '\'' || cc == history_expansion_char)
		i++;
	    }
	}

      if (string[i] != history_expansion_char)
	{
	  free (result);
	  *output = savestring (string);
	  return (0);
	}
    }

  /* Extract and perform the substitution. */
  for (passc = i = j = 0; i < l; i++)
    {
      int tchar = string[i];

      if (passc)
	{
	  passc = 0;
	  ADD_CHAR (tchar);
	  continue;
	}

#if defined (HANDLE_MULTIBYTE)
      if (MB_CUR_MAX > 1 && rl_byte_oriented == 0)
	{
	  int k, c;

	  c = tchar;
	  memset (mb, 0, sizeof (mb));
	  for (k = 0; k < MB_LEN_MAX; k++)
	    {
	      mb[k] = (char)c;
	      memset (&ps, 0, sizeof (mbstate_t));
	      if (_rl_get_char_len (mb, &ps) == -2)
		c = string[++i];
	      else
		break;
	    }
	  if (strlen (mb) > 1)
	    {
	      ADD_STRING (mb);
	      break;
	    }
	}
#endif /* HANDLE_MULTIBYTE */

      if (tchar == history_expansion_char)
	tchar = -3;
      else if (tchar == history_comment_char)
	tchar = -2;

      switch (tchar)
	{
	default:
	  ADD_CHAR (string[i]);
	  break;

	case '\\':
	  passc++;
	  ADD_CHAR (tchar);
	  break;

	case '\'':
	  {
	    /* If history_quotes_inhibit_expansion is set, single quotes
	       inhibit history expansion. */
	    if (history_quotes_inhibit_expansion)
	      {
		int quote, slen;

		quote = i++;
		hist_string_extract_single_quoted (string, &i);

		slen = i - quote + 2;
		temp = (char *)xmalloc (slen);
		strncpy (temp, string + quote, slen);
		temp[slen - 1] = '\0';
		ADD_STRING (temp);
		free (temp);
	      }
	    else
	      ADD_CHAR (string[i]);
	    break;
	  }

	case -2:		/* history_comment_char */
	  if (i == 0 || member (string[i - 1], history_word_delimiters))
	    {
	      temp = (char *)xmalloc (l - i + 1);
	      strlcpy (temp, string + i, l - i + 1);
	      ADD_STRING (temp);
	      free (temp);
	      i = l;
	    }
	  else
	    ADD_CHAR (string[i]);
	  break;

	case -3:		/* history_expansion_char */
	  cc = string[i + 1];

	  /* If the history_expansion_char is followed by one of the
	     characters in history_no_expand_chars, then it is not a
	     candidate for expansion of any kind. */
	  if (member (cc, history_no_expand_chars))
	    {
	      ADD_CHAR (string[i]);
	      break;
	    }

#if defined (NO_BANG_HASH_MODIFIERS)
	  /* There is something that is listed as a `word specifier' in csh
	     documentation which means `the expanded text to this point'.
	     That is not a word specifier, it is an event specifier.  If we
	     don't want to allow modifiers with `!#', just stick the current
	     output line in again. */
	  if (cc == '#')
	    {
	      if (result)
		{
		  temp = (char *)xmalloc (1 + strlen (result));
		  strlcpy (temp, result, 1 + strlen(result));
		  ADD_STRING (temp);
		  free (temp);
		}
	      i++;
	      break;
	    }
#endif

	  r = history_expand_internal (string, i, &eindex, &temp, result);
	  if (r < 0)
	    {
	      *output = temp;
	      free (result);
	      if (string != hstring)
		free (string);
	      return -1;
	    }
	  else
	    {
	      if (temp)
		{
		  modified++;
		  if (*temp)
		    ADD_STRING (temp);
		  free (temp);
		}
	      only_printing = r == 1;
	      i = eindex;
	    }
	  break;
	}
    }

  *output = result;
  if (string != hstring)
    free (string);

  if (only_printing)
    {
      add_history (result);
      return (2);
    }

  return (modified != 0);
}

/* Return a consed string which is the word specified in SPEC, and found
   in FROM.  NULL is returned if there is no spec.  The address of
   ERROR_POINTER is returned if the word specified cannot be found.
   CALLER_INDEX is the offset in SPEC to start looking; it is updated
   to point to just after the last character parsed. */
static char *
get_history_word_specifier (spec, from, caller_index)
     char *spec, *from;
     int *caller_index;
{
  register int i = *caller_index;
  int first, last;
  int expecting_word_spec = 0;
  char *result;

  /* The range of words to return doesn't exist yet. */
  first = last = 0;
  result = (char *)NULL;

  /* If we found a colon, then this *must* be a word specification.  If
     it isn't, then it is an error. */
  if (spec[i] == ':')
    {
      i++;
      expecting_word_spec++;
    }

  /* Handle special cases first. */

  /* `%' is the word last searched for. */
  if (spec[i] == '%')
    {
      *caller_index = i + 1;
      return (search_match ? savestring (search_match) : savestring (""));
    }

  /* `*' matches all of the arguments, but not the command. */
  if (spec[i] == '*')
    {
      *caller_index = i + 1;
      result = history_arg_extract (1, '$', from);
      return (result ? result : savestring (""));
    }

  /* `$' is last arg. */
  if (spec[i] == '$')
    {
      *caller_index = i + 1;
      return (history_arg_extract ('$', '$', from));
    }

  /* Try to get FIRST and LAST figured out. */

  if (spec[i] == '-')
    first = 0;
  else if (spec[i] == '^')
    first = 1;
  else if (_rl_digit_p (spec[i]) && expecting_word_spec)
    {
      for (first = 0; _rl_digit_p (spec[i]); i++)
	first = (first * 10) + _rl_digit_value (spec[i]);
    }
  else
    return ((char *)NULL);	/* no valid `first' for word specifier */

  if (spec[i] == '^' || spec[i] == '*')
    {
      last = (spec[i] == '^') ? 1 : '$';	/* x* abbreviates x-$ */
      i++;
    }
  else if (spec[i] != '-')
    last = first;
  else
    {
      i++;

      if (_rl_digit_p (spec[i]))
	{
	  for (last = 0; _rl_digit_p (spec[i]); i++)
	    last = (last * 10) + _rl_digit_value (spec[i]);
	}
      else if (spec[i] == '$')
	{
	  i++;
	  last = '$';
	}
#if 0
      else if (!spec[i] || spec[i] == ':')
	/* check against `:' because there could be a modifier separator */
#else
      else
	/* csh seems to allow anything to terminate the word spec here,
	   leaving it as an abbreviation. */
#endif
	last = -1;		/* x- abbreviates x-$ omitting word `$' */
    }

  *caller_index = i;

  if (last >= first || last == '$' || last < 0)
    result = history_arg_extract (first, last, from);

  return (result ? result : (char *)&error_pointer);
}

/* Extract the args specified, starting at FIRST, and ending at LAST.
   The args are taken from STRING.  If either FIRST or LAST is < 0,
   then make that arg count from the right (subtract from the number of
   tokens, so that FIRST = -1 means the next to last token on the line).
   If LAST is `$' the last arg from STRING is used. */
char *
history_arg_extract (first, last, string)
     int first, last;
     const char *string;
{
  register int i, len;
  char *result;
  int size, offset;
  char **list;

  /* XXX - think about making history_tokenize return a struct array,
     each struct in array being a string and a length to avoid the
     calls to strlen below. */
  if ((list = history_tokenize (string)) == NULL)
    return ((char *)NULL);

  for (len = 0; list[len]; len++)
    ;

  if (last < 0)
    last = len + last - 1;

  if (first < 0)
    first = len + first - 1;

  if (last == '$')
    last = len - 1;

  if (first == '$')
    first = len - 1;

  last++;

  if (first >= len || last > len || first < 0 || last < 0 || first > last)
    result = ((char *)NULL);
  else
    {
      for (size = 0, i = first; i < last; i++)
	size += strlen (list[i]) + 1;
      result = (char *)xmalloc (size + 1);
      result[0] = '\0';

      for (i = first, offset = 0; i < last; i++)
	{
	  strlcpy (result + offset, list[i], size + 1 - offset);
	  offset += strlen (list[i]);
	  if (i + 1 < last)
	    {
	      result[offset++] = ' ';
	      result[offset] = 0;
	    }
	}
    }

  for (i = 0; i < len; i++)
    free (list[i]);
  free (list);

  return (result);
}

#define slashify_in_quotes "\\`\"$"

/* Parse STRING into tokens and return an array of strings.  If WIND is
   not -1 and INDP is not null, we also want the word surrounding index
   WIND.  The position in the returned array of strings is returned in
   *INDP. */
static char **
history_tokenize_internal (string, wind, indp)
     const char *string;
     int wind, *indp;
{
  char **result;
  register int i, start, result_index, size;
  int len, delimiter;

  /* If we're searching for a string that's not part of a word (e.g., " "),
     make sure we set *INDP to a reasonable value. */
  if (indp && wind != -1)
    *indp = -1;

  /* Get a token, and stuff it into RESULT.  The tokens are split
     exactly where the shell would split them. */
  for (i = result_index = size = 0, result = (char **)NULL; string[i]; )
    {
      delimiter = 0;

      /* Skip leading whitespace. */
      for (; string[i] && whitespace (string[i]); i++)
	;
      if (string[i] == 0 || string[i] == history_comment_char)
	return (result);

      start = i;

      if (member (string[i], "()\n"))
	{
	  i++;
	  goto got_token;
	}

      if (member (string[i], "<>;&|$"))
	{
	  int peek = string[i + 1];

	  if (peek == string[i] && peek != '$')
	    {
	      if (peek == '<' && string[i + 2] == '-')
		i++;
	      i += 2;
	      goto got_token;
	    }
	  else
	    {
	      if ((peek == '&' && (string[i] == '>' || string[i] == '<')) ||
		  ((peek == '>') && (string[i] == '&')) ||
		  ((peek == '(') && (string[i] == '$')))
		{
		  i += 2;
		  goto got_token;
		}
	    }
	  if (string[i] != '$')
	    {
	      i++;
	      goto got_token;
	    }
	}

      /* Get word from string + i; */

      if (member (string[i], HISTORY_QUOTE_CHARACTERS))
	delimiter = string[i++];

      for (; string[i]; i++)
	{
	  if (string[i] == '\\' && string[i + 1] == '\n')
	    {
	      i++;
	      continue;
	    }

	  if (string[i] == '\\' && delimiter != '\'' &&
	      (delimiter != '"' || member (string[i], slashify_in_quotes)))
	    {
	      i++;
	      continue;
	    }

	  if (delimiter && string[i] == delimiter)
	    {
	      delimiter = 0;
	      continue;
	    }

	  if (!delimiter && (member (string[i], history_word_delimiters)))
	    break;

	  if (!delimiter && member (string[i], HISTORY_QUOTE_CHARACTERS))
	    delimiter = string[i];
	}

    got_token:

      /* If we are looking for the word in which the character at a
	 particular index falls, remember it. */
      if (indp && wind != -1 && wind >= start && wind < i)
        *indp = result_index;

      len = i - start;
      if (result_index + 2 >= size)
	result = (char **)xrealloc (result, ((size += 10) * sizeof (char *)));
      result[result_index] = (char *)xmalloc (1 + len);
      strncpy (result[result_index], string + start, len);
      result[result_index][len] = '\0';
      result[++result_index] = (char *)NULL;
    }

  return (result);
}

/* Return an array of tokens, much as the shell might.  The tokens are
   parsed out of STRING. */
char **
history_tokenize (string)
     const char *string;
{
  return (history_tokenize_internal (string, -1, (int *)NULL));
}

/* Find and return the word which contains the character at index IND
   in the history line LINE.  Used to save the word matched by the
   last history !?string? search. */
static char *
history_find_word (line, ind)
     char *line;
     int ind;
{
  char **words, *s;
  int i, wind;

  words = history_tokenize_internal (line, ind, &wind);
  if (wind == -1 || words == 0)
    return ((char *)NULL);
  s = words[wind];
  for (i = 0; i < wind; i++)
    free (words[i]);
  for (i = wind + 1; words[i]; i++)
    free (words[i]);
  free (words);
  return s;
}
