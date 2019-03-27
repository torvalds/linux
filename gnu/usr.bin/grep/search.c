/* search.c - searching subroutines using dfa, kwset and regex for grep.
   Copyright 1992, 1998, 2000 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA.  */

/* Written August 1992 by Mike Haertel. */

/* $FreeBSD$ */

#ifndef _GNU_SOURCE
# define _GNU_SOURCE 1
#endif
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <assert.h>
#include <sys/types.h>
#if defined HAVE_WCTYPE_H && defined HAVE_WCHAR_H && defined HAVE_MBRTOWC
/* We can handle multibyte string.  */
# define MBS_SUPPORT
# include <wchar.h>
# include <wctype.h>
#endif

#include "system.h"
#include "grep.h"
#include "regex.h"
#include "dfa.h"
#include "kwset.h"
#include "error.h"
#include "xalloc.h"
#ifdef HAVE_LIBPCRE
# include <pcre.h>
#endif
#ifdef HAVE_LANGINFO_CODESET
# include <langinfo.h>
#endif

#define NCHAR (UCHAR_MAX + 1)

/* For -w, we also consider _ to be word constituent.  */
#define WCHAR(C) (ISALNUM(C) || (C) == '_')

/* DFA compiled regexp. */
static struct dfa dfa;

/* The Regex compiled patterns.  */
static struct patterns
{
  /* Regex compiled regexp. */
  struct re_pattern_buffer regexbuf;
  struct re_registers regs; /* This is here on account of a BRAIN-DEAD
			       Q@#%!# library interface in regex.c.  */
} patterns0;

struct patterns *patterns;
size_t pcount;

/* KWset compiled pattern.  For Ecompile and Gcompile, we compile
   a list of strings, at least one of which is known to occur in
   any string matching the regexp. */
static kwset_t kwset;

/* Number of compiled fixed strings known to exactly match the regexp.
   If kwsexec returns < kwset_exact_matches, then we don't need to
   call the regexp matcher at all. */
static int kwset_exact_matches;

/* UTF-8 encoding allows some optimizations that we can't otherwise
   assume in a multibyte encoding. */
static int using_utf8;

static void kwsinit PARAMS ((void));
static void kwsmusts PARAMS ((void));
static void Gcompile PARAMS ((char const *, size_t));
static void Ecompile PARAMS ((char const *, size_t));
static size_t EGexecute PARAMS ((char const *, size_t, size_t *, int ));
static void Fcompile PARAMS ((char const *, size_t));
static size_t Fexecute PARAMS ((char const *, size_t, size_t *, int));
static void Pcompile PARAMS ((char const *, size_t ));
static size_t Pexecute PARAMS ((char const *, size_t, size_t *, int));

void
check_utf8 (void)
{
#ifdef HAVE_LANGINFO_CODESET
  if (strcmp (nl_langinfo (CODESET), "UTF-8") == 0)
    using_utf8 = 1;
#endif
}

void
dfaerror (char const *mesg)
{
  error (2, 0, mesg);
}

static void
kwsinit (void)
{
  static char trans[NCHAR];
  size_t i;

  if (match_icase)
    for (i = 0; i < NCHAR; ++i)
      trans[i] = TOLOWER (i);

  if (!(kwset = kwsalloc (match_icase ? trans : (char *) 0)))
    error (2, 0, _("memory exhausted"));
}

/* If the DFA turns out to have some set of fixed strings one of
   which must occur in the match, then we build a kwset matcher
   to find those strings, and thus quickly filter out impossible
   matches. */
static void
kwsmusts (void)
{
  struct dfamust const *dm;
  char const *err;

  if (dfa.musts)
    {
      kwsinit ();
      /* First, we compile in the substrings known to be exact
	 matches.  The kwset matcher will return the index
	 of the matching string that it chooses. */
      for (dm = dfa.musts; dm; dm = dm->next)
	{
	  if (!dm->exact)
	    continue;
	  ++kwset_exact_matches;
	  if ((err = kwsincr (kwset, dm->must, strlen (dm->must))) != 0)
	    error (2, 0, err);
	}
      /* Now, we compile the substrings that will require
	 the use of the regexp matcher.  */
      for (dm = dfa.musts; dm; dm = dm->next)
	{
	  if (dm->exact)
	    continue;
	  if ((err = kwsincr (kwset, dm->must, strlen (dm->must))) != 0)
	    error (2, 0, err);
	}
      if ((err = kwsprep (kwset)) != 0)
	error (2, 0, err);
    }
}

static void
Gcompile (char const *pattern, size_t size)
{
  const char *err;
  char const *sep;
  size_t total = size;
  char const *motif = pattern;

  check_utf8 ();
  re_set_syntax (RE_SYNTAX_GREP | RE_HAT_LISTS_NOT_NEWLINE | (match_icase ? RE_ICASE : 0));
  dfasyntax (RE_SYNTAX_GREP | RE_HAT_LISTS_NOT_NEWLINE, match_icase, eolbyte);

  /* For GNU regex compiler we have to pass the patterns separately to detect
     errors like "[\nallo\n]\n".  The patterns here are "[", "allo" and "]"
     GNU regex should have raise a syntax error.  The same for backref, where
     the backref should have been local to each pattern.  */
  do
    {
      size_t len;
      sep = memchr (motif, '\n', total);
      if (sep)
	{
	  len = sep - motif;
	  sep++;
	  total -= (len + 1);
	}
      else
	{
	  len = total;
	  total = 0;
	}

      patterns = realloc (patterns, (pcount + 1) * sizeof (*patterns));
      if (patterns == NULL)
	error (2, errno, _("memory exhausted"));

      patterns[pcount] = patterns0;

      if ((err = re_compile_pattern (motif, len,
				    &(patterns[pcount].regexbuf))) != 0)
	error (2, 0, err);
      pcount++;

      motif = sep;
    } while (sep && total != 0);

  /* In the match_words and match_lines cases, we use a different pattern
     for the DFA matcher that will quickly throw out cases that won't work.
     Then if DFA succeeds we do some hairy stuff using the regex matcher
     to decide whether the match should really count. */
  if (match_words || match_lines)
    {
      /* In the whole-word case, we use the pattern:
	 \(^\|[^[:alnum:]_]\)\(userpattern\)\([^[:alnum:]_]|$\).
	 In the whole-line case, we use the pattern:
	 ^\(userpattern\)$.  */

      static char const line_beg[] = "^\\(";
      static char const line_end[] = "\\)$";
      static char const word_beg[] = "\\(^\\|[^[:alnum:]_]\\)\\(";
      static char const word_end[] = "\\)\\([^[:alnum:]_]\\|$\\)";
      char *n = xmalloc (sizeof word_beg - 1 + size + sizeof word_end);
      size_t i;
      strcpy (n, match_lines ? line_beg : word_beg);
      i = strlen (n);
      memcpy (n + i, pattern, size);
      i += size;
      strcpy (n + i, match_lines ? line_end : word_end);
      i += strlen (n + i);
      pattern = n;
      size = i;
    }

  dfacomp (pattern, size, &dfa, 1);
  kwsmusts ();
}

static void
Ecompile (char const *pattern, size_t size)
{
  const char *err;
  const char *sep;
  size_t total = size;
  char const *motif = pattern;

  check_utf8 ();
  if (strcmp (matcher, "awk") == 0)
    {
      re_set_syntax (RE_SYNTAX_AWK | (match_icase ? RE_ICASE : 0));
      dfasyntax (RE_SYNTAX_AWK, match_icase, eolbyte);
    }
  else
    {
      re_set_syntax (RE_SYNTAX_POSIX_EGREP | (match_icase ? RE_ICASE : 0));
      dfasyntax (RE_SYNTAX_POSIX_EGREP, match_icase, eolbyte);
    }

  /* For GNU regex compiler we have to pass the patterns separately to detect
     errors like "[\nallo\n]\n".  The patterns here are "[", "allo" and "]"
     GNU regex should have raise a syntax error.  The same for backref, where
     the backref should have been local to each pattern.  */
  do
    {
      size_t len;
      sep = memchr (motif, '\n', total);
      if (sep)
	{
	  len = sep - motif;
	  sep++;
	  total -= (len + 1);
	}
      else
	{
	  len = total;
	  total = 0;
	}

      patterns = realloc (patterns, (pcount + 1) * sizeof (*patterns));
      if (patterns == NULL)
	error (2, errno, _("memory exhausted"));
      patterns[pcount] = patterns0;

      if ((err = re_compile_pattern (motif, len,
				    &(patterns[pcount].regexbuf))) != 0)
	error (2, 0, err);
      pcount++;

      motif = sep;
    } while (sep && total != 0);

  /* In the match_words and match_lines cases, we use a different pattern
     for the DFA matcher that will quickly throw out cases that won't work.
     Then if DFA succeeds we do some hairy stuff using the regex matcher
     to decide whether the match should really count. */
  if (match_words || match_lines)
    {
      /* In the whole-word case, we use the pattern:
	 (^|[^[:alnum:]_])(userpattern)([^[:alnum:]_]|$).
	 In the whole-line case, we use the pattern:
	 ^(userpattern)$.  */

      static char const line_beg[] = "^(";
      static char const line_end[] = ")$";
      static char const word_beg[] = "(^|[^[:alnum:]_])(";
      static char const word_end[] = ")([^[:alnum:]_]|$)";
      char *n = xmalloc (sizeof word_beg - 1 + size + sizeof word_end);
      size_t i;
      strcpy (n, match_lines ? line_beg : word_beg);
      i = strlen(n);
      memcpy (n + i, pattern, size);
      i += size;
      strcpy (n + i, match_lines ? line_end : word_end);
      i += strlen (n + i);
      pattern = n;
      size = i;
    }

  dfacomp (pattern, size, &dfa, 1);
  kwsmusts ();
}

static size_t
EGexecute (char const *buf, size_t size, size_t *match_size, int exact)
{
  register char const *buflim, *beg, *end;
  char eol = eolbyte;
  int backref;
  ptrdiff_t start, len;
  struct kwsmatch kwsm;
  size_t i, ret_val;
  static int use_dfa;
  static int use_dfa_checked = 0;
#ifdef MBS_SUPPORT
  const char *last_char = NULL;
  int mb_cur_max = MB_CUR_MAX;
  mbstate_t mbs;
  memset (&mbs, '\0', sizeof (mbstate_t));
#endif /* MBS_SUPPORT */

  if (!use_dfa_checked)
    {
      char *grep_use_dfa = getenv ("GREP_USE_DFA");
      if (!grep_use_dfa)
	{
#ifdef MBS_SUPPORT
	  /* Turn off DFA when processing multibyte input. */
	  use_dfa = (MB_CUR_MAX == 1);
#else
	  use_dfa = 1;
#endif /* MBS_SUPPORT */
	}
      else
	{
	  use_dfa = atoi (grep_use_dfa);
	}

      use_dfa_checked = 1;
    }

  buflim = buf + size;

  for (beg = end = buf; end < buflim; beg = end)
    {
      if (!exact)
	{
	  if (kwset)
	    {
	      /* Find a possible match using the KWset matcher. */
#ifdef MBS_SUPPORT
	      size_t bytes_left = 0;
#endif /* MBS_SUPPORT */
	      size_t offset;
#ifdef MBS_SUPPORT
	      /* kwsexec doesn't work with match_icase and multibyte input. */
	      if (match_icase && mb_cur_max > 1)
		/* Avoid kwset */
		offset = 0;
	      else
#endif /* MBS_SUPPORT */
	      offset = kwsexec (kwset, beg, buflim - beg, &kwsm);
	      if (offset == (size_t) -1)
	        goto failure;
#ifdef MBS_SUPPORT
	      if (mb_cur_max > 1 && !using_utf8)
		{
		  bytes_left = offset;
		  while (bytes_left)
		    {
		      size_t mlen = mbrlen (beg, bytes_left, &mbs);

		      last_char = beg;
		      if (mlen == (size_t) -1 || mlen == 0)
			{
			  /* Incomplete character: treat as single-byte. */
			  memset (&mbs, '\0', sizeof (mbstate_t));
			  beg++;
			  bytes_left--;
			  continue;
			}

		      if (mlen == (size_t) -2)
			{
			  /* Offset points inside multibyte character:
			   * no good. */
			  memset (&mbs, '\0', sizeof (mbstate_t));
			  break;
			}

		      beg += mlen;
		      bytes_left -= mlen;
		    }
		}
	      else
#endif /* MBS_SUPPORT */
	      beg += offset;
	      /* Narrow down to the line containing the candidate, and
		 run it through DFA. */
	      end = memchr(beg, eol, buflim - beg);
	      end++;
#ifdef MBS_SUPPORT
	      if (mb_cur_max > 1 && bytes_left)
		continue;
#endif /* MBS_SUPPORT */
	      while (beg > buf && beg[-1] != eol)
		--beg;
	      if (
#ifdef MBS_SUPPORT
		  !(match_icase && mb_cur_max > 1) &&
#endif /* MBS_SUPPORT */
		  (kwsm.index < kwset_exact_matches))
		goto success_in_beg_and_end;
	      if (use_dfa &&
		  dfaexec (&dfa, beg, end - beg, &backref) == (size_t) -1)
		continue;
	    }
	  else
	    {
	      /* No good fixed strings; start with DFA. */
#ifdef MBS_SUPPORT
	      size_t bytes_left = 0;
#endif /* MBS_SUPPORT */
	      size_t offset = 0;
	      if (use_dfa)
		offset = dfaexec (&dfa, beg, buflim - beg, &backref);
	      if (offset == (size_t) -1)
		break;
	      /* Narrow down to the line we've found. */
#ifdef MBS_SUPPORT
	      if (mb_cur_max > 1 && !using_utf8)
		{
		  bytes_left = offset;
		  while (bytes_left)
		    {
		      size_t mlen = mbrlen (beg, bytes_left, &mbs);

		      last_char = beg;
		      if (mlen == (size_t) -1 || mlen == 0)
			{
			  /* Incomplete character: treat as single-byte. */
			  memset (&mbs, '\0', sizeof (mbstate_t));
			  beg++;
			  bytes_left--;
			  continue;
			}

		      if (mlen == (size_t) -2)
			{
			  /* Offset points inside multibyte character:
			   * no good. */
			  memset (&mbs, '\0', sizeof (mbstate_t));
			  break;
			}

		      beg += mlen;
		      bytes_left -= mlen;
		    }
		}
	      else
#endif /* MBS_SUPPORT */
	      beg += offset;
	      end = memchr (beg, eol, buflim - beg);
	      end++;
#ifdef MBS_SUPPORT
	      if (mb_cur_max > 1 && bytes_left)
		continue;
#endif /* MBS_SUPPORT */
	      while (beg > buf && beg[-1] != eol)
		--beg;
	    }
	  /* Successful, no backreferences encountered! */
	  if (use_dfa && !backref)
	    goto success_in_beg_and_end;
	}
      else
	end = beg + size;

      /* If we've made it to this point, this means DFA has seen
	 a probable match, and we need to run it through Regex. */
      for (i = 0; i < pcount; i++)
	{
	  patterns[i].regexbuf.not_eol = 0;
	  if (0 <= (start = re_search (&(patterns[i].regexbuf), beg,
				       end - beg - 1, 0,
				       end - beg - 1, &(patterns[i].regs))))
	    {
	      len = patterns[i].regs.end[0] - start;
	      if (exact && !match_words)
	        goto success_in_start_and_len;
	      if ((!match_lines && !match_words)
		  || (match_lines && len == end - beg - 1))
		goto success_in_beg_and_end;
	      /* If -w, check if the match aligns with word boundaries.
		 We do this iteratively because:
		 (a) the line may contain more than one occurence of the
		 pattern, and
		 (b) Several alternatives in the pattern might be valid at a
		 given point, and we may need to consider a shorter one to
		 find a word boundary.  */
	      if (match_words)
		while (start >= 0)
		  {
		    int lword_match = 0;
		    if (start == 0)
		      lword_match = 1;
		    else
		      {
			assert (start > 0);
#ifdef MBS_SUPPORT
			if (mb_cur_max > 1)
			  {
			    const char *s;
			    size_t mr;
			    wchar_t pwc;

			    /* Locate the start of the multibyte character
			       before the match position (== beg + start).  */
			    if (using_utf8)
			      {
				/* UTF-8 is a special case: scan backwards
				   until we find a 7-bit character or a
				   lead byte.  */
				s = beg + start - 1;
				while (s > buf
				       && (unsigned char) *s >= 0x80
				       && (unsigned char) *s <= 0xbf)
				  --s;
			      }
			    else
			      {
				/* Scan forwards to find the start of the
				   last complete character before the
				   match position.  */
				size_t bytes_left = start - 1;
				s = beg;
				while (bytes_left > 0)
				  {
				    mr = mbrlen (s, bytes_left, &mbs);
				    if (mr == (size_t) -1 || mr == 0)
				      {
					memset (&mbs, '\0', sizeof (mbs));
					s++;
					bytes_left--;
					continue;
				      }
				    if (mr == (size_t) -2)
				      {
					memset (&mbs, '\0', sizeof (mbs));
					break;
				      }
				    s += mr;
				    bytes_left -= mr;
				  }
			      }
			    mr = mbrtowc (&pwc, s, beg + start - s, &mbs);
			    if (mr == (size_t) -2 || mr == (size_t) -1 ||
				mr == 0)
			      {
				memset (&mbs, '\0', sizeof (mbstate_t));
				lword_match = 1;
			      }
			    else if (!(iswalnum (pwc) || pwc == L'_')
				     && mr == beg + start - s)
			      lword_match = 1;
			  }
			else
#endif /* MBS_SUPPORT */
			if (!WCHAR ((unsigned char) beg[start - 1]))
			  lword_match = 1;
		      }

		    if (lword_match)
		      {
			int rword_match = 0;
			if (start + len == end - beg - 1)
			  rword_match = 1;
			else
			  {
#ifdef MBS_SUPPORT
			    if (mb_cur_max > 1)
			      {
				wchar_t nwc;
				int mr;

				mr = mbtowc (&nwc, beg + start + len,
					     end - beg - start - len - 1);
				if (mr <= 0)
				  {
				    memset (&mbs, '\0', sizeof (mbstate_t));
				    rword_match = 1;
				  }
				else if (!iswalnum (nwc) && nwc != L'_')
				  rword_match = 1;
			      }
			    else
#endif /* MBS_SUPPORT */
			    if (!WCHAR ((unsigned char) beg[start + len]))
			      rword_match = 1;
			  }

			if (rword_match)
			  {
			    if (!exact)
			      /* Returns the whole line. */
			      goto success_in_beg_and_end;
			    else
			      /* Returns just this word match. */
			      goto success_in_start_and_len;
			  }
		      }
		    if (len > 0)
		      {
			/* Try a shorter length anchored at the same place. */
			--len;
			patterns[i].regexbuf.not_eol = 1;
			len = re_match (&(patterns[i].regexbuf), beg,
					start + len, start,
					&(patterns[i].regs));
		      }
		    if (len <= 0)
		      {
			/* Try looking further on. */
			if (start == end - beg - 1)
			  break;
			++start;
			patterns[i].regexbuf.not_eol = 0;
			start = re_search (&(patterns[i].regexbuf), beg,
					   end - beg - 1,
					   start, end - beg - 1 - start,
					   &(patterns[i].regs));
			len = patterns[i].regs.end[0] - start;
		      }
		  }
	    }
	} /* for Regex patterns.  */
    } /* for (beg = end ..) */

 failure:
  return (size_t) -1;

 success_in_beg_and_end:
  len = end - beg;
  start = beg - buf;
  /* FALLTHROUGH */

 success_in_start_and_len:
  *match_size = len;
  return start;
}

#ifdef MBS_SUPPORT
static int f_i_multibyte; /* whether we're using the new -Fi MB method */
static struct
{
  wchar_t **patterns;
  size_t count, maxlen;
  unsigned char *match;
} Fimb;
#endif

static void
Fcompile (char const *pattern, size_t size)
{
  int mb_cur_max = MB_CUR_MAX;
  char const *beg, *lim, *err;

  check_utf8 ();
#ifdef MBS_SUPPORT
  /* Support -F -i for UTF-8 input. */
  if (match_icase && mb_cur_max > 1)
    {
      mbstate_t mbs;
      wchar_t *wcpattern = xmalloc ((size + 1) * sizeof (wchar_t));
      const char *patternend = pattern;
      size_t wcsize;
      kwset_t fimb_kwset = NULL;
      char *starts = NULL;
      wchar_t *wcbeg, *wclim;
      size_t allocated = 0;

      memset (&mbs, '\0', sizeof (mbs));
# ifdef __GNU_LIBRARY__
      wcsize = mbsnrtowcs (wcpattern, &patternend, size, size, &mbs);
      if (patternend != pattern + size)
	wcsize = (size_t) -1;
# else
      {
	char *patterncopy = xmalloc (size + 1);

	memcpy (patterncopy, pattern, size);
	patterncopy[size] = '\0';
	patternend = patterncopy;
	wcsize = mbsrtowcs (wcpattern, &patternend, size, &mbs);
	if (patternend != patterncopy + size)
	  wcsize = (size_t) -1;
	free (patterncopy);
      }
# endif
      if (wcsize + 2 <= 2)
	{
fimb_fail:
	  free (wcpattern);
	  free (starts);
	  if (fimb_kwset)
	    kwsfree (fimb_kwset);
	  free (Fimb.patterns);
	  Fimb.patterns = NULL;
	}
      else
	{
	  if (!(fimb_kwset = kwsalloc (NULL)))
	    error (2, 0, _("memory exhausted"));

	  starts = xmalloc (mb_cur_max * 3);
	  wcbeg = wcpattern;
	  do
	    {
	      int i;
	      size_t wclen;

	      if (Fimb.count >= allocated)
		{
		  if (allocated == 0)
		    allocated = 128;
		  else
		    allocated *= 2;
		  Fimb.patterns = xrealloc (Fimb.patterns,
					    sizeof (wchar_t *) * allocated);
		}
	      Fimb.patterns[Fimb.count++] = wcbeg;
	      for (wclim = wcbeg;
		   wclim < wcpattern + wcsize && *wclim != L'\n'; ++wclim)
		*wclim = towlower (*wclim);
	      *wclim = L'\0';
	      wclen = wclim - wcbeg;
	      if (wclen > Fimb.maxlen)
		Fimb.maxlen = wclen;
	      if (wclen > 3)
		wclen = 3;
	      if (wclen == 0)
		{
		  if ((err = kwsincr (fimb_kwset, "", 0)) != 0)
		    error (2, 0, err);
		}
	      else
		for (i = 0; i < (1 << wclen); i++)
		  {
		    char *p = starts;
		    int j, k;

		    for (j = 0; j < wclen; ++j)
		      {
			wchar_t wc = wcbeg[j];
			if (i & (1 << j))
			  {
			    wc = towupper (wc);
			    if (wc == wcbeg[j])
			      continue;
			  }
			k = wctomb (p, wc);
			if (k <= 0)
			  goto fimb_fail;
			p += k;
		      }
		    if ((err = kwsincr (fimb_kwset, starts, p - starts)) != 0)
		      error (2, 0, err);
		  }
	      if (wclim < wcpattern + wcsize)
		++wclim;
	      wcbeg = wclim;
	    }
	  while (wcbeg < wcpattern + wcsize);
	  f_i_multibyte = 1;
	  kwset = fimb_kwset;
	  free (starts);
	  Fimb.match = xmalloc (Fimb.count);
	  if ((err = kwsprep (kwset)) != 0)
	    error (2, 0, err);
	  return;
	}
    }
#endif /* MBS_SUPPORT */


  kwsinit ();
  beg = pattern;
  do
    {
      for (lim = beg; lim < pattern + size && *lim != '\n'; ++lim)
	;
      if ((err = kwsincr (kwset, beg, lim - beg)) != 0)
	error (2, 0, err);
      if (lim < pattern + size)
	++lim;
      beg = lim;
    }
  while (beg < pattern + size);

  if ((err = kwsprep (kwset)) != 0)
    error (2, 0, err);
}

#ifdef MBS_SUPPORT
static int
Fimbexec (const char *buf, size_t size, size_t *plen, int exact)
{
  size_t len, letter, i;
  int ret = -1;
  mbstate_t mbs;
  wchar_t wc;
  int patterns_left;

  assert (match_icase && f_i_multibyte == 1);
  assert (MB_CUR_MAX > 1);

  memset (&mbs, '\0', sizeof (mbs));
  memset (Fimb.match, '\1', Fimb.count);
  letter = len = 0;
  patterns_left = 1;
  while (patterns_left && len <= size)
    {
      size_t c;

      patterns_left = 0;
      if (len < size)
	{
	  c = mbrtowc (&wc, buf + len, size - len, &mbs);
	  if (c + 2 <= 2)
	    return ret;

	  wc = towlower (wc);
	}
      else
	{
	  c = 1;
	  wc = L'\0';
	}

      for (i = 0; i < Fimb.count; i++)
	{
	  if (Fimb.match[i])
	    {
	      if (Fimb.patterns[i][letter] == L'\0')
		{
		  /* Found a match. */
		  *plen = len;
		  if (!exact && !match_words)
		    return 0;
		  else
		    {
		      /* For -w or exact look for longest match.  */
		      ret = 0;
		      Fimb.match[i] = '\0';
		      continue;
		    }
		}

	      if (Fimb.patterns[i][letter] == wc)
		patterns_left = 1;
	      else
		Fimb.match[i] = '\0';
	    }
	}

      len += c;
      letter++;
    }

  return ret;
}
#endif /* MBS_SUPPORT */

static size_t
Fexecute (char const *buf, size_t size, size_t *match_size, int exact)
{
  register char const *beg, *try, *end;
  register size_t len;
  char eol = eolbyte;
  struct kwsmatch kwsmatch;
  size_t ret_val;
#ifdef MBS_SUPPORT
  int mb_cur_max = MB_CUR_MAX;
  mbstate_t mbs;
  memset (&mbs, '\0', sizeof (mbstate_t));
  const char *last_char = NULL;
#endif /* MBS_SUPPORT */

  for (beg = buf; beg <= buf + size; ++beg)
    {
      size_t offset;
      offset = kwsexec (kwset, beg, buf + size - beg, &kwsmatch);

      if (offset == (size_t) -1)
	goto failure;
#ifdef MBS_SUPPORT
      if (mb_cur_max > 1 && !using_utf8)
	{
	  size_t bytes_left = offset;
	  while (bytes_left)
	    {
	      size_t mlen = mbrlen (beg, bytes_left, &mbs);

	      last_char = beg;
	      if (mlen == (size_t) -1 || mlen == 0)
		{
		  /* Incomplete character: treat as single-byte. */
		  memset (&mbs, '\0', sizeof (mbstate_t));
		  beg++;
		  bytes_left--;
		  continue;
		}

	      if (mlen == (size_t) -2)
		{
		  /* Offset points inside multibyte character: no good. */
		  memset (&mbs, '\0', sizeof (mbstate_t));
		  break;
		}

	      beg += mlen;
	      bytes_left -= mlen;
	    }

	  if (bytes_left)
	    {
	      beg += bytes_left;
	      continue;
	    }
	}
      else
#endif /* MBS_SUPPORT */
      beg += offset;
#ifdef MBS_SUPPORT
      /* For f_i_multibyte, the string at beg now matches first 3 chars of
	 one of the search strings (less if there are shorter search strings).
	 See if this is a real match.  */
      if (f_i_multibyte
	  && Fimbexec (beg, buf + size - beg, &kwsmatch.size[0], exact))
	goto next_char;
#endif /* MBS_SUPPORT */
      len = kwsmatch.size[0];
      if (exact && !match_words)
	goto success_in_beg_and_len;
      if (match_lines)
	{
	  if (beg > buf && beg[-1] != eol)
	    goto next_char;
	  if (beg + len < buf + size && beg[len] != eol)
	    goto next_char;
	  goto success;
	}
      else if (match_words)
	{
	  while (1)
	    {
	      int word_match = 0;
	      if (beg > buf)
		{
#ifdef MBS_SUPPORT
		  if (mb_cur_max > 1)
		    {
		      const char *s;
		      int mr;
		      wchar_t pwc;

		      if (using_utf8)
			{
			  s = beg - 1;
			  while (s > buf
				 && (unsigned char) *s >= 0x80
				 && (unsigned char) *s <= 0xbf)
			    --s;
			}
		      else
			s = last_char;
		      mr = mbtowc (&pwc, s, beg - s);
		      if (mr <= 0)
			memset (&mbs, '\0', sizeof (mbstate_t));
		      else if ((iswalnum (pwc) || pwc == L'_')
			       && mr == (int) (beg - s))
			goto next_char;
		    }
		  else
#endif /* MBS_SUPPORT */
		  if (WCHAR ((unsigned char) beg[-1]))
		    goto next_char;
		}
#ifdef MBS_SUPPORT
	      if (mb_cur_max > 1)
		{
		  wchar_t nwc;
		  int mr;

		  mr = mbtowc (&nwc, beg + len, buf + size - beg - len);
		  if (mr <= 0)
		    {
		      memset (&mbs, '\0', sizeof (mbstate_t));
		      word_match = 1;
		    }
		  else if (!iswalnum (nwc) && nwc != L'_')
		    word_match = 1;
		}
	      else
#endif /* MBS_SUPPORT */
		if (beg + len >= buf + size || !WCHAR ((unsigned char) beg[len]))
		  word_match = 1;
	      if (word_match)
		{
		  if (!exact)
		    /* Returns the whole line now we know there's a word match. */
		    goto success;
		  else
		    /* Returns just this word match. */
		    goto success_in_beg_and_len;
		}
	      if (len > 0)
		{
		  /* Try a shorter length anchored at the same place. */
		  --len;
		  offset = kwsexec (kwset, beg, len, &kwsmatch);

		  if (offset == -1)
		    goto next_char; /* Try a different anchor. */
#ifdef MBS_SUPPORT
		  if (mb_cur_max > 1 && !using_utf8)
		    {
		      size_t bytes_left = offset;
		      while (bytes_left)
			{
			  size_t mlen = mbrlen (beg, bytes_left, &mbs);

			  last_char = beg;
			  if (mlen == (size_t) -1 || mlen == 0)
			    {
			      /* Incomplete character: treat as single-byte. */
			      memset (&mbs, '\0', sizeof (mbstate_t));
			      beg++;
			      bytes_left--;
			      continue;
			    }

			  if (mlen == (size_t) -2)
			    {
			      /* Offset points inside multibyte character:
			       * no good. */
			      memset (&mbs, '\0', sizeof (mbstate_t));
			      break;
			    }

			  beg += mlen;
			  bytes_left -= mlen;
			}

		      if (bytes_left)
			{
			  memset (&mbs, '\0', sizeof (mbstate_t));
			  goto next_char; /* Try a different anchor. */
			}
		    }
		  else
#endif /* MBS_SUPPORT */
		  beg += offset;
#ifdef MBS_SUPPORT
		  /* The string at beg now matches first 3 chars of one of
		     the search strings (less if there are shorter search
		     strings).  See if this is a real match.  */
		  if (f_i_multibyte
		      && Fimbexec (beg, len - offset, &kwsmatch.size[0],
				   exact))
		    goto next_char;
#endif /* MBS_SUPPORT */
		  len = kwsmatch.size[0];
		}
	    }
	}
      else
	goto success;
next_char:;
#ifdef MBS_SUPPORT
      /* Advance to next character.  For MB_CUR_MAX == 1 case this is handled
	 by ++beg above.  */
      if (mb_cur_max > 1)
	{
	  if (using_utf8)
	    {
	      unsigned char c = *beg;
	      if (c >= 0xc2)
		{
		  if (c < 0xe0)
		    ++beg;
		  else if (c < 0xf0)
		    beg += 2;
		  else if (c < 0xf8)
		    beg += 3;
		  else if (c < 0xfc)
		    beg += 4;
		  else if (c < 0xfe)
		    beg += 5;
		}
	    }
	  else
	    {
	      size_t l = mbrlen (beg, buf + size - beg, &mbs);

	      last_char = beg;
	      if (l + 2 >= 2)
		beg += l - 1;
	      else
		memset (&mbs, '\0', sizeof (mbstate_t));
	    }
	}
#endif /* MBS_SUPPORT */
    }

 failure:
  return -1;

 success:
#ifdef MBS_SUPPORT
  if (mb_cur_max > 1 && !using_utf8)
    {
      end = beg + len;
      while (end < buf + size)
	{
	  size_t mlen = mbrlen (end, buf + size - end, &mbs);
	  if (mlen == (size_t) -1 || mlen == (size_t) -2 || mlen == 0)
	    {
	      memset (&mbs, '\0', sizeof (mbstate_t));
	      mlen = 1;
	    }
	  if (mlen == 1 && *end == eol)
	    break;

	  end += mlen;
	}
    }
  else
#endif /* MBS_SUPPORT */
  end = memchr (beg + len, eol, (buf + size) - (beg + len));

  end++;
  while (buf < beg && beg[-1] != eol)
    --beg;
  len = end - beg;
  /* FALLTHROUGH */

 success_in_beg_and_len:
  *match_size = len;
  return beg - buf;
}

#if HAVE_LIBPCRE
/* Compiled internal form of a Perl regular expression.  */
static pcre *cre;

/* Additional information about the pattern.  */
static pcre_extra *extra;
#endif

static void
Pcompile (char const *pattern, size_t size)
{
#if !HAVE_LIBPCRE
  error (2, 0, _("The -P option is not supported"));
#else
  int e;
  char const *ep;
  char *re = xmalloc (4 * size + 7);
  int flags = PCRE_MULTILINE | (match_icase ? PCRE_CASELESS : 0);
  char const *patlim = pattern + size;
  char *n = re;
  char const *p;
  char const *pnul;

  /* FIXME: Remove this restriction.  */
  if (eolbyte != '\n')
    error (2, 0, _("The -P and -z options cannot be combined"));

  *n = '\0';
  if (match_lines)
    strcpy (n, "^(");
  if (match_words)
    strcpy (n, "\\b(");
  n += strlen (n);

  /* The PCRE interface doesn't allow NUL bytes in the pattern, so
     replace each NUL byte in the pattern with the four characters
     "\000", removing a preceding backslash if there are an odd
     number of backslashes before the NUL.

     FIXME: This method does not work with some multibyte character
     encodings, notably Shift-JIS, where a multibyte character can end
     in a backslash byte.  */
  for (p = pattern; (pnul = memchr (p, '\0', patlim - p)); p = pnul + 1)
    {
      memcpy (n, p, pnul - p);
      n += pnul - p;
      for (p = pnul; pattern < p && p[-1] == '\\'; p--)
	continue;
      n -= (pnul - p) & 1;
      strcpy (n, "\\000");
      n += 4;
    }

  memcpy (n, p, patlim - p);
  n += patlim - p;
  *n = '\0';
  if (match_words)
    strcpy (n, ")\\b");
  if (match_lines)
    strcpy (n, ")$");

  cre = pcre_compile (re, flags, &ep, &e, pcre_maketables ());
  if (!cre)
    error (2, 0, ep);

  extra = pcre_study (cre, 0, &ep);
  if (ep)
    error (2, 0, ep);

  free (re);
#endif
}

static size_t
Pexecute (char const *buf, size_t size, size_t *match_size, int exact)
{
#if !HAVE_LIBPCRE
  abort ();
  return -1;
#else
  /* This array must have at least two elements; everything after that
     is just for performance improvement in pcre_exec.  */
  int sub[300];

  int e = pcre_exec (cre, extra, buf, size, 0, 0,
		     sub, sizeof sub / sizeof *sub);

  if (e <= 0)
    {
      switch (e)
	{
	case PCRE_ERROR_NOMATCH:
	  return -1;

	case PCRE_ERROR_NOMEMORY:
	  error (2, 0, _("Memory exhausted"));

	default:
	  abort ();
	}
    }
  else
    {
      /* Narrow down to the line we've found.  */
      char const *beg = buf + sub[0];
      char const *end = buf + sub[1];
      char const *buflim = buf + size;
      char eol = eolbyte;
      if (!exact)
	{
	  end = memchr (end, eol, buflim - end);
	  end++;
	  while (buf < beg && beg[-1] != eol)
	    --beg;
	}

      *match_size = end - beg;
      return beg - buf;
    }
#endif
}

struct matcher const matchers[] = {
  { "default", Gcompile, EGexecute },
  { "grep", Gcompile, EGexecute },
  { "egrep", Ecompile, EGexecute },
  { "awk", Ecompile, EGexecute },
  { "fgrep", Fcompile, Fexecute },
  { "perl", Pcompile, Pexecute },
  { "", 0, 0 },
};
