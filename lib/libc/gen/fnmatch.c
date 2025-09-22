/*	$OpenBSD: fnmatch.c,v 1.23 2020/10/13 04:42:28 guenther Exp $	*/

/* Copyright (c) 2011, VMware, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the VMware, Inc. nor the names of its contributors
 *       may be used to endorse or promote products derived from this software
 *       without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL VMWARE, INC. OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 2008, 2016 Todd C. Miller <millert@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* Authored by William A. Rowe Jr. <wrowe; apache.org, vmware.com>, April 2011
 *
 * Derived from The Open Group Base Specifications Issue 7, IEEE Std 1003.1-2008
 * as described in;
 *   http://pubs.opengroup.org/onlinepubs/9699919799/functions/fnmatch.html
 *
 * Filename pattern matches defined in section 2.13, "Pattern Matching Notation"
 * from chapter 2. "Shell Command Language"
 *   http://pubs.opengroup.org/onlinepubs/9699919799/utilities/V3_chap02.html#tag_18_13
 * where; 1. A bracket expression starting with an unquoted <circumflex> '^'
 * character CONTINUES to specify a non-matching list; 2. an explicit <period> '.'
 * in a bracket expression matching list, e.g. "[.abc]" does NOT match a leading
 * <period> in a filename; 3. a <left-square-bracket> '[' which does not introduce
 * a valid bracket expression is treated as an ordinary character; 4. a differing
 * number of consecutive slashes within pattern and string will NOT match;
 * 5. a trailing '\' in FNM_ESCAPE mode is treated as an ordinary '\' character.
 *
 * Bracket expansion defined in section 9.3.5, "RE Bracket Expression",
 * from chapter 9, "Regular Expressions"
 *   http://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap09.html#tag_09_03_05
 * with no support for collating symbols, equivalence class expressions or
 * character class expressions.  A partial range expression with a leading
 * hyphen following a valid range expression will match only the ordinary
 * <hyphen> and the ending character (e.g. "[a-m-z]" will match characters
 * 'a' through 'm', a <hyphen> '-', or a 'z').
 *
 * Supports BSD extensions FNM_LEADING_DIR to match pattern to the end of one
 * path segment of string, and FNM_CASEFOLD to ignore alpha case.
 *
 * NOTE: Only POSIX/C single byte locales are correctly supported at this time.
 * Notably, non-POSIX locales with FNM_CASEFOLD produce undefined results,
 * particularly in ranges of mixed case (e.g. "[A-z]") or spanning alpha and
 * nonalpha characters within a range.
 *
 * XXX comments below indicate porting required for multi-byte character sets
 * and non-POSIX locale collation orders; requires mbr* APIs to track shift
 * state of pattern and string (rewinding pattern and string repeatedly).
 *
 * Certain parts of the code assume 0x00-0x3F are unique with any MBCS (e.g.
 * UTF-8, SHIFT-JIS, etc).  Any implementation allowing '\' as an alternate
 * path delimiter must be aware that 0x5C is NOT unique within SHIFT-JIS.
 */

#include <fnmatch.h>
#include <string.h>
#include <ctype.h>

#include "charclass.h"

#define	RANGE_MATCH	1
#define	RANGE_NOMATCH	0
#define	RANGE_ERROR	(-1)

static int
classmatch(const char *pattern, char test, int foldcase, const char **ep)
{
	const char * const mismatch = pattern;
	const char *colon;
	const struct cclass *cc;
	int rval = RANGE_NOMATCH;
	size_t len;

	if (pattern[0] != '[' || pattern[1] != ':') {
		*ep = mismatch;
		return RANGE_ERROR;
	}
	pattern += 2;

	if ((colon = strchr(pattern, ':')) == NULL || colon[1] != ']') {
		*ep = mismatch;
		return RANGE_ERROR;
	}
	*ep = colon + 2;
	len = (size_t)(colon - pattern);

	if (foldcase && strncmp(pattern, "upper:]", 7) == 0)
		pattern = "lower:]";
	for (cc = cclasses; cc->name != NULL; cc++) {
		if (!strncmp(pattern, cc->name, len) && cc->name[len] == '\0') {
			if (cc->isctype((unsigned char)test))
				rval = RANGE_MATCH;
			break;
		}
	}
	if (cc->name == NULL) {
		/* invalid character class, treat as normal text */
		*ep = mismatch;
		rval = RANGE_ERROR;
	}
	return rval;
}

/* Most MBCS/collation/case issues handled here.  Wildcard '*' is not handled.
 * EOS '\0' and the FNM_PATHNAME '/' delimiters are not advanced over,
 * however the "\/" sequence is advanced to '/'.
 *
 * Both pattern and string are **char to support pointer increment of arbitrary
 * multibyte characters for the given locale, in a later iteration of this code
 */
static int fnmatch_ch(const char **pattern, const char **string, int flags)
{
	const char * const mismatch = *pattern;
	const int nocase = !!(flags & FNM_CASEFOLD);
	const int escape = !(flags & FNM_NOESCAPE);
	const int slash = !!(flags & FNM_PATHNAME);
	int result = FNM_NOMATCH;
	const char *startch;
	int negate;

	if (**pattern == '[') {
		++*pattern;

		/* Handle negation, either leading ! or ^ operators */
		negate = (**pattern == '!') || (**pattern == '^');
		if (negate)
			++*pattern;

		/* ']' is an ordinary char at the start of the range pattern */
		if (**pattern == ']')
			goto leadingclosebrace;

		while (**pattern) {
			if (**pattern == ']') {
				++*pattern;
				/* XXX: Fix for MBCS character width */
				++*string;
				return (result ^ negate);
			}

			if (escape && (**pattern == '\\')) {
				++*pattern;

				/* Patterns must terminate with ']', not EOS */
				if (!**pattern)
					break;
			}

			/* Patterns must terminate with ']' not '/' */
			if (slash && (**pattern == '/'))
				break;

			/* Match character classes. */
			switch (classmatch(*pattern, **string, nocase, pattern)) {
			case RANGE_MATCH:
				result = 0;
				continue;
			case RANGE_NOMATCH:
				/* Valid character class but no match. */
				continue;
			default:
				/* Not a valid character class. */
				break;
			}
			if (!**pattern)
				break;

leadingclosebrace:
			/* Look at only well-formed range patterns;
			 * "x-]" is not allowed unless escaped ("x-\]")
			 * XXX: Fix for locale/MBCS character width
			 */
			if (((*pattern)[1] == '-') && ((*pattern)[2] != ']')) {
				startch = *pattern;
				*pattern += (escape && ((*pattern)[2] == '\\')) ? 3 : 2;

				/*
				 * NOT a properly balanced [expr] pattern, EOS
				 * terminated or ranges containing a slash in
				 * FNM_PATHNAME mode pattern fall out to to the
				 * rewind and test '[' literal code path.
				 */
				if (!**pattern || (slash && (**pattern == '/')))
					break;

				/* XXX: handle locale/MBCS comparison, advance by MBCS char width */
				if ((**string >= *startch) && (**string <= **pattern))
					result = 0;
				else if (nocase &&
				    (isupper((unsigned char)**string) ||
				     isupper((unsigned char)*startch) ||
				     isupper((unsigned char)**pattern)) &&
				    (tolower((unsigned char)**string) >=
				     tolower((unsigned char)*startch)) &&
				    (tolower((unsigned char)**string) <=
				     tolower((unsigned char)**pattern)))
					result = 0;

				++*pattern;
				continue;
			}

			/* XXX: handle locale/MBCS comparison, advance by MBCS char width */
			if ((**string == **pattern))
				result = 0;
			else if (nocase && (isupper((unsigned char)**string) ||
			    isupper((unsigned char)**pattern)) &&
			    (tolower((unsigned char)**string) ==
			    tolower((unsigned char)**pattern)))
				result = 0;

			++*pattern;
		}
		/*
		 * NOT a properly balanced [expr] pattern;
		 * Rewind and reset result to test '[' literal
		 */
		*pattern = mismatch;
		result = FNM_NOMATCH;
	} else if (**pattern == '?') {
		/* Optimize '?' match before unescaping **pattern */
		if (!**string || (slash && (**string == '/')))
			return FNM_NOMATCH;
		result = 0;
		goto fnmatch_ch_success;
	} else if (escape && (**pattern == '\\') && (*pattern)[1]) {
		++*pattern;
	}

	/* XXX: handle locale/MBCS comparison, advance by the MBCS char width */
	if (**string == **pattern)
		result = 0;
	else if (nocase && (isupper((unsigned char)**string) ||
	    isupper((unsigned char)**pattern)) &&
	    (tolower((unsigned char)**string) ==
	    tolower((unsigned char)**pattern)))
		result = 0;

	/* Refuse to advance over trailing slash or NULs */
	if (**string == '\0' || **pattern == '\0' ||
	    (slash && ((**string == '/') || (**pattern == '/'))))
		return result;

fnmatch_ch_success:
	++*pattern;
	++*string;
	return result;
}


int fnmatch(const char *pattern, const char *string, int flags)
{
	static const char dummystring[2] = {' ', 0};
	const int escape = !(flags & FNM_NOESCAPE);
	const int slash = !!(flags & FNM_PATHNAME);
	const int leading_dir = !!(flags & FNM_LEADING_DIR);
	const char *dummyptr, *matchptr, *strendseg;
	int wild;
	/* For '*' wild processing only; suppress 'used before initialization'
	 * warnings with dummy initialization values;
	 */
	const char *strstartseg = NULL;
	const char *mismatch = NULL;
	int matchlen = 0;

	if (*pattern == '*')
		goto firstsegment;

	while (*pattern && *string) {
		/*
		 * Pre-decode "\/" which has no special significance, and
		 * match balanced slashes, starting a new segment pattern.
		 */
		if (slash && escape && (*pattern == '\\') && (pattern[1] == '/'))
			++pattern;
		if (slash && (*pattern == '/') && (*string == '/')) {
			++pattern;
			++string;
		}

firstsegment:
		/*
		 * At the beginning of each segment, validate leading period
		 * behavior.
		 */
		if ((flags & FNM_PERIOD) && (*string == '.')) {
		    if (*pattern == '.')
			    ++pattern;
		    else if (escape && (*pattern == '\\') && (pattern[1] == '.'))
			    pattern += 2;
		    else
			    return FNM_NOMATCH;
		    ++string;
		}

		/*
		 * Determine the end of string segment.  Presumes '/'
		 * character is unique, not composite in any MBCS encoding
		 */
		if (slash) {
			strendseg = strchr(string, '/');
			if (!strendseg)
				strendseg = strchr(string, '\0');
		} else {
			strendseg = strchr(string, '\0');
		}

		/*
		 * Allow pattern '*' to be consumed even with no remaining
		 * string to match.
		 */
		while (*pattern) {
			if ((string > strendseg) ||
			    ((string == strendseg) && (*pattern != '*')))
				break;

			if (slash && ((*pattern == '/') ||
			    (escape && (*pattern == '\\') && (pattern[1] == '/'))))
				break;

			/*
			 * Reduce groups of '*' and '?' to n '?' matches
			 * followed by one '*' test for simplicity.
			 */
			for (wild = 0; (*pattern == '*') || (*pattern == '?'); ++pattern) {
				if (*pattern == '*') {
					wild = 1;
				} else if (string < strendseg) {  /* && (*pattern == '?') */
					/* XXX: Advance 1 char for MBCS locale */
					++string;
				}
				else {  /* (string >= strendseg) && (*pattern == '?') */
					return FNM_NOMATCH;
				}
			}

			if (wild) {
				strstartseg = string;
				mismatch = pattern;

				/*
				 * Count fixed (non '*') char matches remaining
				 * in pattern * excluding '/' (or "\/") and '*'.
				 */
				for (matchptr = pattern, matchlen = 0; 1; ++matchlen) {
					if ((*matchptr == '\0') ||
					    (slash && ((*matchptr == '/') ||
					    (escape && (*matchptr == '\\') &&
					    (matchptr[1] == '/'))))) {
						/* Compare precisely this many
						 * trailing string chars, the
						 * resulting match needs no
						 * wildcard loop.
						 */
						/* XXX: Adjust for MBCS */
						if (string + matchlen > strendseg)
							return FNM_NOMATCH;

						string = strendseg - matchlen;
						wild = 0;
						break;
					}

					if (*matchptr == '*') {
						/*
						 * Ensure at least this many
						 * trailing string chars remain
						 * for the first comparison.
						 */
						/* XXX: Adjust for MBCS */
						if (string + matchlen > strendseg)
							return FNM_NOMATCH;

						/*
						 * Begin first wild comparison
						 * at the current position.
						 */
						break;
					}

					/*
					 * Skip forward in pattern by a single
					 * character match Use a dummy
					 * fnmatch_ch() test to count one
					 * "[range]" escape.
					 */
					/* XXX: Adjust for MBCS */
					if (escape && (*matchptr == '\\') &&
					    matchptr[1]) {
						matchptr += 2;
					} else if (*matchptr == '[') {
						dummyptr = dummystring;
						fnmatch_ch(&matchptr, &dummyptr,
						    flags);
					} else {
						++matchptr;
					}
				}
			}

			/* Incrementally match string against the pattern. */
			while (*pattern && (string < strendseg)) {
				/* Success; begin a new wild pattern search. */
				if (*pattern == '*')
					break;

				if (slash && ((*string == '/') ||
				    (*pattern == '/') || (escape &&
				    (*pattern == '\\') && (pattern[1] == '/'))))
					break;

				/*
				 * Compare ch's (the pattern is advanced over
				 * "\/" to the '/', but slashes will mismatch,
				 * and are not consumed).
				 */
				if (!fnmatch_ch(&pattern, &string, flags))
					continue;

				/*
				 * Failed to match, loop against next char
				 * offset of string segment until not enough
				 * string chars remain to match the fixed
				 * pattern.
				 */
				if (wild) {
					/* XXX: Advance 1 char for MBCS locale */
					string = ++strstartseg;
					if (string + matchlen > strendseg)
						return FNM_NOMATCH;

					pattern = mismatch;
					continue;
				} else
					return FNM_NOMATCH;
			}
		}

		if (*string && !((slash || leading_dir) && (*string == '/')))
			return FNM_NOMATCH;

		if (*pattern && !(slash && ((*pattern == '/') ||
		    (escape && (*pattern == '\\') && (pattern[1] == '/')))))
			return FNM_NOMATCH;

		if (leading_dir && !*pattern && *string == '/')
			return 0;
	}

	/* Where both pattern and string are at EOS, declare success.  */
	if (!*string && !*pattern)
		return 0;

	/* Pattern didn't match to the end of string. */
	return FNM_NOMATCH;
}
