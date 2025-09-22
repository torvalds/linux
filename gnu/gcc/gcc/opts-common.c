/* Command line option handling.
   Copyright (C) 2006 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

#include "config.h"
#include "system.h"
#include "intl.h"
#include "coretypes.h"
#include "opts.h"

/* Perform a binary search to find which option the command-line INPUT
   matches.  Returns its index in the option array, and N_OPTS
   (cl_options_count) on failure.

   This routine is quite subtle.  A normal binary search is not good
   enough because some options can be suffixed with an argument, and
   multiple sub-matches can occur, e.g. input of "-pedantic" matching
   the initial substring of "-pedantic-errors".

   A more complicated example is -gstabs.  It should match "-g" with
   an argument of "stabs".  Suppose, however, that the number and list
   of switches are such that the binary search tests "-gen-decls"
   before having tested "-g".  This doesn't match, and as "-gen-decls"
   is less than "-gstabs", it will become the lower bound of the
   binary search range, and "-g" will never be seen.  To resolve this
   issue, opts.sh makes "-gen-decls" point, via the back_chain member,
   to "-g" so that failed searches that end between "-gen-decls" and
   the lexicographically subsequent switch know to go back and see if
   "-g" causes a match (which it does in this example).

   This search is done in such a way that the longest match for the
   front end in question wins.  If there is no match for the current
   front end, the longest match for a different front end is returned
   (or N_OPTS if none) and the caller emits an error message.  */
size_t
find_opt (const char *input, int lang_mask)
{
  size_t mn, mx, md, opt_len;
  size_t match_wrong_lang;
  int comp;

  mn = 0;
  mx = cl_options_count;

  /* Find mn such this lexicographical inequality holds:
     cl_options[mn] <= input < cl_options[mn + 1].  */
  while (mx - mn > 1)
    {
      md = (mn + mx) / 2;
      opt_len = cl_options[md].opt_len;
      comp = strncmp (input, cl_options[md].opt_text + 1, opt_len);

      if (comp < 0)
	mx = md;
      else
	mn = md;
    }

  /* This is the switch that is the best match but for a different
     front end, or cl_options_count if there is no match at all.  */
  match_wrong_lang = cl_options_count;

  /* Backtrace the chain of possible matches, returning the longest
     one, if any, that fits best.  With current GCC switches, this
     loop executes at most twice.  */
  do
    {
      const struct cl_option *opt = &cl_options[mn];

      /* Is the input either an exact match or a prefix that takes a
	 joined argument?  */
      if (!strncmp (input, opt->opt_text + 1, opt->opt_len)
	  && (input[opt->opt_len] == '\0' || (opt->flags & CL_JOINED)))
	{
	  /* If language is OK, return it.  */
	  if (opt->flags & lang_mask)
	    return mn;

	  /* If we haven't remembered a prior match, remember this
	     one.  Any prior match is necessarily better.  */
	  if (match_wrong_lang == cl_options_count)
	    match_wrong_lang = mn;
	}

      /* Try the next possibility.  This is cl_options_count if there
	 are no more.  */
      mn = opt->back_chain;
    }
  while (mn != cl_options_count);

  /* Return the best wrong match, or cl_options_count if none.  */
  return match_wrong_lang;
}

/* Return true if NEXT_OPT_IDX cancels OPT_IDX.  Return false if the
   next one is the same as ORIG_NEXT_OPT_IDX.  */

static bool
cancel_option (int opt_idx, int next_opt_idx, int orig_next_opt_idx)
{
  /* An option can be canceled by the same option or an option with
     Negative.  */
  if (cl_options [next_opt_idx].neg_index == opt_idx)
    return true;

  if (cl_options [next_opt_idx].neg_index != orig_next_opt_idx)
    return cancel_option (opt_idx, cl_options [next_opt_idx].neg_index,
			  orig_next_opt_idx);
    
  return false;
}

/* Filter out options canceled by the ones after them.  */

void
prune_options (int *argcp, char ***argvp)
{
  int argc = *argcp;
  int *options = xmalloc (argc * sizeof (*options));
  char **argv = xmalloc (argc * sizeof (char *));
  int i, arg_count, need_prune = 0;
  const struct cl_option *option;
  size_t opt_index;

  /* Scan all arguments.  */
  for (i = 1; i < argc; i++)
    {
      int value = 1;
      const char *opt = (*argvp) [i];

      opt_index = find_opt (opt + 1, -1);
      if (opt_index == cl_options_count
	  && (opt[1] == 'W' || opt[1] == 'f' || opt[1] == 'm')
	  && opt[2] == 'n' && opt[3] == 'o' && opt[4] == '-')
	{
	  char *dup;

	  /* Drop the "no-" from negative switches.  */
	  size_t len = strlen (opt) - 3;

	  dup = XNEWVEC (char, len + 1);
	  dup[0] = '-';
	  dup[1] = opt[1];
	  memcpy (dup + 2, opt + 5, len - 2 + 1);
	  opt = dup;
	  value = 0;
	  opt_index = find_opt (opt + 1, -1);
	  free (dup);
	}

      if (opt_index == cl_options_count)
	{
cont:
	  options [i] = 0;
	  continue;
	}

      option = &cl_options[opt_index];
      if (option->neg_index < 0)
	goto cont;

      /* Skip joined switches.  */
      if ((option->flags & CL_JOINED))
	goto cont;

      /* Reject negative form of switches that don't take negatives as
	 unrecognized.  */
      if (!value && (option->flags & CL_REJECT_NEGATIVE))
	goto cont;

      options [i] = (int) opt_index;
      need_prune |= options [i];
    }

  if (!need_prune)
    goto done;

  /* Remove arguments which are negated by others after them.  */
  argv [0] = (*argvp) [0];
  arg_count = 1;
  for (i = 1; i < argc; i++)
    {
      int j, opt_idx;

      opt_idx = options [i];
      if (opt_idx)
	{
	  int next_opt_idx;
	  for (j = i + 1; j < argc; j++)
	    {
	      next_opt_idx = options [j];
	      if (next_opt_idx
		  && cancel_option (opt_idx, next_opt_idx,
				    next_opt_idx))
		break;
	    }
	}
      else
	goto keep;

      if (j == argc)
	{
keep:
	  argv [arg_count] = (*argvp) [i];
	  arg_count++;
	}
    }

  if (arg_count != argc)
    {
      *argcp = arg_count;
      *argvp = argv;
    }
  else
    {
done:
      free (argv);
    }

  free (options);
}
