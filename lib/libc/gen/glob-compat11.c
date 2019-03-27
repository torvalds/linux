/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Guido van Rossum.
 *
 * Copyright (c) 2011 The FreeBSD Foundation
 * All rights reserved.
 * Portions of this software were developed by David Chisnall
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * From: @(#)glob.c	8.3 (Berkeley) 10/13/93
 * From: FreeBSD: head/lib/libc/gen/glob.c 317913 2017-05-07 19:52:56Z jilles
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#define	_WANT_FREEBSD11_STAT
#include <sys/stat.h>

#include <ctype.h>
#define	_WANT_FREEBSD11_DIRENT
#include <dirent.h>
#include <errno.h>
#include <glob.h>
#include <limits.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

#include "collate.h"
#include "gen-compat.h"
#include "glob-compat11.h"

/*
 * glob(3) expansion limits. Stop the expansion if any of these limits
 * is reached. This caps the runtime in the face of DoS attacks. See
 * also CVE-2010-2632
 */
#define	GLOB_LIMIT_BRACE	128	/* number of brace calls */
#define	GLOB_LIMIT_PATH		65536	/* number of path elements */
#define	GLOB_LIMIT_READDIR	16384	/* number of readdirs */
#define	GLOB_LIMIT_STAT		1024	/* number of stat system calls */
#define	GLOB_LIMIT_STRING	ARG_MAX	/* maximum total size for paths */

struct glob_limit {
	size_t	l_brace_cnt;
	size_t	l_path_lim;
	size_t	l_readdir_cnt;	
	size_t	l_stat_cnt;	
	size_t	l_string_cnt;
};

#define	DOT		L'.'
#define	EOS		L'\0'
#define	LBRACKET	L'['
#define	NOT		L'!'
#define	QUESTION	L'?'
#define	QUOTE		L'\\'
#define	RANGE		L'-'
#define	RBRACKET	L']'
#define	SEP		L'/'
#define	STAR		L'*'
#define	TILDE		L'~'
#define	LBRACE		L'{'
#define	RBRACE		L'}'
#define	COMMA		L','

#define	M_QUOTE		0x8000000000ULL
#define	M_PROTECT	0x4000000000ULL
#define	M_MASK		0xffffffffffULL
#define	M_CHAR		0x00ffffffffULL

typedef uint_fast64_t Char;

#define	CHAR(c)		((Char)((c)&M_CHAR))
#define	META(c)		((Char)((c)|M_QUOTE))
#define	UNPROT(c)	((c) & ~M_PROTECT)
#define	M_ALL		META(L'*')
#define	M_END		META(L']')
#define	M_NOT		META(L'!')
#define	M_ONE		META(L'?')
#define	M_RNG		META(L'-')
#define	M_SET		META(L'[')
#define	ismeta(c)	(((c)&M_QUOTE) != 0)
#ifdef DEBUG
#define	isprot(c)	(((c)&M_PROTECT) != 0)
#endif

static int	 compare(const void *, const void *);
static int	 g_Ctoc(const Char *, char *, size_t);
static int	 g_lstat(Char *, struct freebsd11_stat *, glob11_t *);
static DIR	*g_opendir(Char *, glob11_t *);
static const Char *g_strchr(const Char *, wchar_t);
#ifdef notdef
static Char	*g_strcat(Char *, const Char *);
#endif
static int	 g_stat(Char *, struct freebsd11_stat *, glob11_t *);
static int	 glob0(const Char *, glob11_t *, struct glob_limit *,
    const char *);
static int	 glob1(Char *, glob11_t *, struct glob_limit *);
static int	 glob2(Char *, Char *, Char *, Char *, glob11_t *,
    struct glob_limit *);
static int	 glob3(Char *, Char *, Char *, Char *, Char *, glob11_t *,
    struct glob_limit *);
static int	 globextend(const Char *, glob11_t *, struct glob_limit *,
    const char *);
static const Char *
		 globtilde(const Char *, Char *, size_t, glob11_t *);
static int	 globexp0(const Char *, glob11_t *, struct glob_limit *,
    const char *);
static int	 globexp1(const Char *, glob11_t *, struct glob_limit *);
static int	 globexp2(const Char *, const Char *, glob11_t *,
    struct glob_limit *);
static int	 globfinal(glob11_t *, struct glob_limit *, size_t,
    const char *);
static int	 match(Char *, Char *, Char *);
static int	 err_nomatch(glob11_t *, struct glob_limit *, const char *);
static int	 err_aborted(glob11_t *, int, char *);
#ifdef DEBUG
static void	 qprintf(const char *, Char *);
#endif

int
freebsd11_glob(const char * __restrict pattern, int flags,
	 int (*errfunc)(const char *, int), glob11_t * __restrict pglob)
{
	struct glob_limit limit = { 0, 0, 0, 0, 0 };
	const char *patnext;
	Char *bufnext, *bufend, patbuf[MAXPATHLEN], prot;
	mbstate_t mbs;
	wchar_t wc;
	size_t clen;
	int too_long;

	patnext = pattern;
	if (!(flags & GLOB_APPEND)) {
		pglob->gl_pathc = 0;
		pglob->gl_pathv = NULL;
		if (!(flags & GLOB_DOOFFS))
			pglob->gl_offs = 0;
	}
	if (flags & GLOB_LIMIT) {
		limit.l_path_lim = pglob->gl_matchc;
		if (limit.l_path_lim == 0)
			limit.l_path_lim = GLOB_LIMIT_PATH;
	}
	pglob->gl_flags = flags & ~GLOB_MAGCHAR;
	pglob->gl_errfunc = errfunc;
	pglob->gl_matchc = 0;

	bufnext = patbuf;
	bufend = bufnext + MAXPATHLEN - 1;
	too_long = 1;
	if (flags & GLOB_NOESCAPE) {
		memset(&mbs, 0, sizeof(mbs));
		while (bufnext <= bufend) {
			clen = mbrtowc(&wc, patnext, MB_LEN_MAX, &mbs);
			if (clen == (size_t)-1 || clen == (size_t)-2)
				return (err_nomatch(pglob, &limit, pattern));
			else if (clen == 0) {
				too_long = 0;
				break;
			}
			*bufnext++ = wc;
			patnext += clen;
		}
	} else {
		/* Protect the quoted characters. */
		memset(&mbs, 0, sizeof(mbs));
		while (bufnext <= bufend) {
			if (*patnext == '\\') {
				if (*++patnext == '\0') {
					*bufnext++ = QUOTE;
					continue;
				}
				prot = M_PROTECT;
			} else
				prot = 0;
			clen = mbrtowc(&wc, patnext, MB_LEN_MAX, &mbs);
			if (clen == (size_t)-1 || clen == (size_t)-2)
				return (err_nomatch(pglob, &limit, pattern));
			else if (clen == 0) {
				too_long = 0;
				break;
			}
			*bufnext++ = wc | prot;
			patnext += clen;
		}
	}
	if (too_long)
		return (err_nomatch(pglob, &limit, pattern));
	*bufnext = EOS;

	if (flags & GLOB_BRACE)
	    return (globexp0(patbuf, pglob, &limit, pattern));
	else
	    return (glob0(patbuf, pglob, &limit, pattern));
}

static int
globexp0(const Char *pattern, glob11_t *pglob, struct glob_limit *limit,
    const char *origpat) {
	int rv;
	size_t oldpathc;

	/* Protect a single {}, for find(1), like csh */
	if (pattern[0] == LBRACE && pattern[1] == RBRACE && pattern[2] == EOS) {
		if ((pglob->gl_flags & GLOB_LIMIT) &&
		    limit->l_brace_cnt++ >= GLOB_LIMIT_BRACE) {
			errno = E2BIG;
			return (GLOB_NOSPACE);
		}
		return (glob0(pattern, pglob, limit, origpat));
	}

	oldpathc = pglob->gl_pathc;

	if ((rv = globexp1(pattern, pglob, limit)) != 0)
		return rv;

	return (globfinal(pglob, limit, oldpathc, origpat));
}

/*
 * Expand recursively a glob {} pattern. When there is no more expansion
 * invoke the standard globbing routine to glob the rest of the magic
 * characters
 */
static int
globexp1(const Char *pattern, glob11_t *pglob, struct glob_limit *limit)
{
	const Char* ptr;

	if ((ptr = g_strchr(pattern, LBRACE)) != NULL) {
		if ((pglob->gl_flags & GLOB_LIMIT) &&
		    limit->l_brace_cnt++ >= GLOB_LIMIT_BRACE) {
			errno = E2BIG;
			return (GLOB_NOSPACE);
		}
		return (globexp2(ptr, pattern, pglob, limit));
	}

	return (glob0(pattern, pglob, limit, NULL));
}


/*
 * Recursive brace globbing helper. Tries to expand a single brace.
 * If it succeeds then it invokes globexp1 with the new pattern.
 * If it fails then it tries to glob the rest of the pattern and returns.
 */
static int
globexp2(const Char *ptr, const Char *pattern, glob11_t *pglob,
    struct glob_limit *limit)
{
	int     i, rv;
	Char   *lm, *ls;
	const Char *pe, *pm, *pm1, *pl;
	Char    patbuf[MAXPATHLEN];

	/* copy part up to the brace */
	for (lm = patbuf, pm = pattern; pm != ptr; *lm++ = *pm++)
		continue;
	*lm = EOS;
	ls = lm;

	/* Find the balanced brace */
	for (i = 0, pe = ++ptr; *pe != EOS; pe++)
		if (*pe == LBRACKET) {
			/* Ignore everything between [] */
			for (pm = pe++; *pe != RBRACKET && *pe != EOS; pe++)
				continue;
			if (*pe == EOS) {
				/*
				 * We could not find a matching RBRACKET.
				 * Ignore and just look for RBRACE
				 */
				pe = pm;
			}
		}
		else if (*pe == LBRACE)
			i++;
		else if (*pe == RBRACE) {
			if (i == 0)
				break;
			i--;
		}

	/* Non matching braces; just glob the pattern */
	if (i != 0 || *pe == EOS)
		return (glob0(pattern, pglob, limit, NULL));

	for (i = 0, pl = pm = ptr; pm <= pe; pm++)
		switch (*pm) {
		case LBRACKET:
			/* Ignore everything between [] */
			for (pm1 = pm++; *pm != RBRACKET && *pm != EOS; pm++)
				continue;
			if (*pm == EOS) {
				/*
				 * We could not find a matching RBRACKET.
				 * Ignore and just look for RBRACE
				 */
				pm = pm1;
			}
			break;

		case LBRACE:
			i++;
			break;

		case RBRACE:
			if (i) {
			    i--;
			    break;
			}
			/* FALLTHROUGH */
		case COMMA:
			if (i && *pm == COMMA)
				break;
			else {
				/* Append the current string */
				for (lm = ls; (pl < pm); *lm++ = *pl++)
					continue;
				/*
				 * Append the rest of the pattern after the
				 * closing brace
				 */
				for (pl = pe + 1; (*lm++ = *pl++) != EOS;)
					continue;

				/* Expand the current pattern */
#ifdef DEBUG
				qprintf("globexp2:", patbuf);
#endif
				rv = globexp1(patbuf, pglob, limit);
				if (rv)
					return (rv);

				/* move after the comma, to the next string */
				pl = pm + 1;
			}
			break;

		default:
			break;
		}
	return (0);
}



/*
 * expand tilde from the passwd file.
 */
static const Char *
globtilde(const Char *pattern, Char *patbuf, size_t patbuf_len, glob11_t *pglob)
{
	struct passwd *pwd;
	char *h, *sc;
	const Char *p;
	Char *b, *eb;
	wchar_t wc;
	wchar_t wbuf[MAXPATHLEN];
	wchar_t *wbufend, *dc;
	size_t clen;
	mbstate_t mbs;
	int too_long;

	if (*pattern != TILDE || !(pglob->gl_flags & GLOB_TILDE))
		return (pattern);

	/* 
	 * Copy up to the end of the string or / 
	 */
	eb = &patbuf[patbuf_len - 1];
	for (p = pattern + 1, b = patbuf;
	    b < eb && *p != EOS && UNPROT(*p) != SEP; *b++ = *p++)
		continue;

	if (*p != EOS && UNPROT(*p) != SEP)
		return (NULL);

	*b = EOS;
	h = NULL;

	if (patbuf[0] == EOS) {
		/*
		 * handle a plain ~ or ~/ by expanding $HOME first (iff
		 * we're not running setuid or setgid) and then trying
		 * the password file
		 */
		if (issetugid() != 0 ||
		    (h = getenv("HOME")) == NULL) {
			if (((h = getlogin()) != NULL &&
			     (pwd = getpwnam(h)) != NULL) ||
			    (pwd = getpwuid(getuid())) != NULL)
				h = pwd->pw_dir;
			else
				return (pattern);
		}
	}
	else {
		/*
		 * Expand a ~user
		 */
		if (g_Ctoc(patbuf, (char *)wbuf, sizeof(wbuf)))
			return (NULL);
		if ((pwd = getpwnam((char *)wbuf)) == NULL)
			return (pattern);
		else
			h = pwd->pw_dir;
	}

	/* Copy the home directory */
	dc = wbuf;
	sc = h;
	wbufend = wbuf + MAXPATHLEN - 1;
	too_long = 1;
	memset(&mbs, 0, sizeof(mbs));
	while (dc <= wbufend) {
		clen = mbrtowc(&wc, sc, MB_LEN_MAX, &mbs);
		if (clen == (size_t)-1 || clen == (size_t)-2) {
			/* XXX See initial comment #2. */
			wc = (unsigned char)*sc;
			clen = 1;
			memset(&mbs, 0, sizeof(mbs));
		}
		if ((*dc++ = wc) == EOS) {
			too_long = 0;
			break;
		}
		sc += clen;
	}
	if (too_long)
		return (NULL);

	dc = wbuf;
	for (b = patbuf; b < eb && *dc != EOS; *b++ = *dc++ | M_PROTECT)
		continue;
	if (*dc != EOS)
		return (NULL);

	/* Append the rest of the pattern */
	if (*p != EOS) {
		too_long = 1;
		while (b <= eb) {
			if ((*b++ = *p++) == EOS) {
				too_long = 0;
				break;
			}
		}
		if (too_long)
			return (NULL);
	} else
		*b = EOS;

	return (patbuf);
}


/*
 * The main glob() routine: compiles the pattern (optionally processing
 * quotes), calls glob1() to do the real pattern matching, and finally
 * sorts the list (unless unsorted operation is requested).  Returns 0
 * if things went well, nonzero if errors occurred.
 */
static int
glob0(const Char *pattern, glob11_t *pglob, struct glob_limit *limit,
    const char *origpat) {
	const Char *qpatnext;
	int err;
	size_t oldpathc;
	Char *bufnext, c, patbuf[MAXPATHLEN];

	qpatnext = globtilde(pattern, patbuf, MAXPATHLEN, pglob);
	if (qpatnext == NULL) {
		errno = E2BIG;
		return (GLOB_NOSPACE);
	}
	oldpathc = pglob->gl_pathc;
	bufnext = patbuf;

	/* We don't need to check for buffer overflow any more. */
	while ((c = *qpatnext++) != EOS) {
		switch (c) {
		case LBRACKET:
			c = *qpatnext;
			if (c == NOT)
				++qpatnext;
			if (*qpatnext == EOS ||
			    g_strchr(qpatnext+1, RBRACKET) == NULL) {
				*bufnext++ = LBRACKET;
				if (c == NOT)
					--qpatnext;
				break;
			}
			*bufnext++ = M_SET;
			if (c == NOT)
				*bufnext++ = M_NOT;
			c = *qpatnext++;
			do {
				*bufnext++ = CHAR(c);
				if (*qpatnext == RANGE &&
				    (c = qpatnext[1]) != RBRACKET) {
					*bufnext++ = M_RNG;
					*bufnext++ = CHAR(c);
					qpatnext += 2;
				}
			} while ((c = *qpatnext++) != RBRACKET);
			pglob->gl_flags |= GLOB_MAGCHAR;
			*bufnext++ = M_END;
			break;
		case QUESTION:
			pglob->gl_flags |= GLOB_MAGCHAR;
			*bufnext++ = M_ONE;
			break;
		case STAR:
			pglob->gl_flags |= GLOB_MAGCHAR;
			/* collapse adjacent stars to one,
			 * to avoid exponential behavior
			 */
			if (bufnext == patbuf || bufnext[-1] != M_ALL)
			    *bufnext++ = M_ALL;
			break;
		default:
			*bufnext++ = CHAR(c);
			break;
		}
	}
	*bufnext = EOS;
#ifdef DEBUG
	qprintf("glob0:", patbuf);
#endif

	if ((err = glob1(patbuf, pglob, limit)) != 0)
		return(err);

	if (origpat != NULL)
		return (globfinal(pglob, limit, oldpathc, origpat));

	return (0);
}

static int
globfinal(glob11_t *pglob, struct glob_limit *limit, size_t oldpathc,
    const char *origpat) {
	if (pglob->gl_pathc == oldpathc)
		return (err_nomatch(pglob, limit, origpat));

	if (!(pglob->gl_flags & GLOB_NOSORT))
		qsort(pglob->gl_pathv + pglob->gl_offs + oldpathc,
		    pglob->gl_pathc - oldpathc, sizeof(char *), compare);

	return (0);
}

static int
compare(const void *p, const void *q)
{
	return (strcoll(*(char **)p, *(char **)q));
}

static int
glob1(Char *pattern, glob11_t *pglob, struct glob_limit *limit)
{
	Char pathbuf[MAXPATHLEN];

	/* A null pathname is invalid -- POSIX 1003.1 sect. 2.4. */
	if (*pattern == EOS)
		return (0);
	return (glob2(pathbuf, pathbuf, pathbuf + MAXPATHLEN - 1,
	    pattern, pglob, limit));
}

/*
 * The functions glob2 and glob3 are mutually recursive; there is one level
 * of recursion for each segment in the pattern that contains one or more
 * meta characters.
 */
static int
glob2(Char *pathbuf, Char *pathend, Char *pathend_last, Char *pattern,
      glob11_t *pglob, struct glob_limit *limit)
{
	struct freebsd11_stat sb;
	Char *p, *q;
	int anymeta;

	/*
	 * Loop over pattern segments until end of pattern or until
	 * segment with meta character found.
	 */
	for (anymeta = 0;;) {
		if (*pattern == EOS) {		/* End of pattern? */
			*pathend = EOS;
			if (g_lstat(pathbuf, &sb, pglob))
				return (0);

			if ((pglob->gl_flags & GLOB_LIMIT) &&
			    limit->l_stat_cnt++ >= GLOB_LIMIT_STAT) {
				errno = E2BIG;
				return (GLOB_NOSPACE);
			}
			if ((pglob->gl_flags & GLOB_MARK) &&
			    UNPROT(pathend[-1]) != SEP &&
			    (S_ISDIR(sb.st_mode) ||
			    (S_ISLNK(sb.st_mode) &&
			    g_stat(pathbuf, &sb, pglob) == 0 &&
			    S_ISDIR(sb.st_mode)))) {
				if (pathend + 1 > pathend_last) {
					errno = E2BIG;
					return (GLOB_NOSPACE);
				}
				*pathend++ = SEP;
				*pathend = EOS;
			}
			++pglob->gl_matchc;
			return (globextend(pathbuf, pglob, limit, NULL));
		}

		/* Find end of next segment, copy tentatively to pathend. */
		q = pathend;
		p = pattern;
		while (*p != EOS && UNPROT(*p) != SEP) {
			if (ismeta(*p))
				anymeta = 1;
			if (q + 1 > pathend_last) {
				errno = E2BIG;
				return (GLOB_NOSPACE);
			}
			*q++ = *p++;
		}

		if (!anymeta) {		/* No expansion, do next segment. */
			pathend = q;
			pattern = p;
			while (UNPROT(*pattern) == SEP) {
				if (pathend + 1 > pathend_last) {
					errno = E2BIG;
					return (GLOB_NOSPACE);
				}
				*pathend++ = *pattern++;
			}
		} else			/* Need expansion, recurse. */
			return (glob3(pathbuf, pathend, pathend_last, pattern,
			    p, pglob, limit));
	}
	/* NOTREACHED */
}

static int
glob3(Char *pathbuf, Char *pathend, Char *pathend_last,
      Char *pattern, Char *restpattern,
      glob11_t *pglob, struct glob_limit *limit)
{
	struct freebsd11_dirent *dp;
	DIR *dirp;
	int err, too_long, saverrno, saverrno2;
	char buf[MAXPATHLEN + MB_LEN_MAX - 1];

	struct freebsd11_dirent *(*readdirfunc)(DIR *);

	if (pathend > pathend_last) {
		errno = E2BIG;
		return (GLOB_NOSPACE);
	}
	*pathend = EOS;
	if (pglob->gl_errfunc != NULL &&
	    g_Ctoc(pathbuf, buf, sizeof(buf))) {
		errno = E2BIG;
		return (GLOB_NOSPACE);
	}

	saverrno = errno;
	errno = 0;
	if ((dirp = g_opendir(pathbuf, pglob)) == NULL) {
		if (errno == ENOENT || errno == ENOTDIR)
			return (0);
		err = err_aborted(pglob, errno, buf);
		if (errno == 0)
			errno = saverrno;
		return (err);
	}

	err = 0;

	/* pglob->gl_readdir takes a void *, fix this manually */
	if (pglob->gl_flags & GLOB_ALTDIRFUNC)
		readdirfunc =
		    (struct freebsd11_dirent *(*)(DIR *))pglob->gl_readdir;
	else
		readdirfunc = freebsd11_readdir;

	errno = 0;
	/* Search directory for matching names. */
	while ((dp = (*readdirfunc)(dirp)) != NULL) {
		char *sc;
		Char *dc;
		wchar_t wc;
		size_t clen;
		mbstate_t mbs;

		if ((pglob->gl_flags & GLOB_LIMIT) &&
		    limit->l_readdir_cnt++ >= GLOB_LIMIT_READDIR) {
			errno = E2BIG;
			err = GLOB_NOSPACE;
			break;
		}

		/* Initial DOT must be matched literally. */
		if (dp->d_name[0] == '.' && UNPROT(*pattern) != DOT) {
			errno = 0;
			continue;
		}
		memset(&mbs, 0, sizeof(mbs));
		dc = pathend;
		sc = dp->d_name;
		too_long = 1;
		while (dc <= pathend_last) {
			clen = mbrtowc(&wc, sc, MB_LEN_MAX, &mbs);
			if (clen == (size_t)-1 || clen == (size_t)-2) {
				/* XXX See initial comment #2. */
				wc = (unsigned char)*sc;
				clen = 1;
				memset(&mbs, 0, sizeof(mbs));
			}
			if ((*dc++ = wc) == EOS) {
				too_long = 0;
				break;
			}
			sc += clen;
		}
		if (too_long && (err = err_aborted(pglob, ENAMETOOLONG,
		    buf))) {
			errno = ENAMETOOLONG;
			break;
		}
		if (too_long || !match(pathend, pattern, restpattern)) {
			*pathend = EOS;
			errno = 0;
			continue;
		}
		if (errno == 0)
			errno = saverrno;
		err = glob2(pathbuf, --dc, pathend_last, restpattern,
		    pglob, limit);
		if (err)
			break;
		errno = 0;
	}

	saverrno2 = errno;
	if (pglob->gl_flags & GLOB_ALTDIRFUNC)
		(*pglob->gl_closedir)(dirp);
	else
		closedir(dirp);
	errno = saverrno2;

	if (err)
		return (err);

	if (dp == NULL && errno != 0 &&
	    (err = err_aborted(pglob, errno, buf)))
		return (err);

	if (errno == 0)
		errno = saverrno;
	return (0);
}


/*
 * Extend the gl_pathv member of a glob11_t structure to accommodate a new item,
 * add the new item, and update gl_pathc.
 *
 * This assumes the BSD realloc, which only copies the block when its size
 * crosses a power-of-two boundary; for v7 realloc, this would cause quadratic
 * behavior.
 *
 * Return 0 if new item added, error code if memory couldn't be allocated.
 *
 * Invariant of the glob11_t structure:
 *	Either gl_pathc is zero and gl_pathv is NULL; or gl_pathc > 0 and
 *	gl_pathv points to (gl_offs + gl_pathc + 1) items.
 */
static int
globextend(const Char *path, glob11_t *pglob, struct glob_limit *limit,
    const char *origpat)
{
	char **pathv;
	size_t i, newn, len;
	char *copy;
	const Char *p;

	if ((pglob->gl_flags & GLOB_LIMIT) &&
	    pglob->gl_matchc > limit->l_path_lim) {
		errno = E2BIG;
		return (GLOB_NOSPACE);
	}

	newn = 2 + pglob->gl_pathc + pglob->gl_offs;
	/* reallocarray(NULL, newn, size) is equivalent to malloc(newn*size). */
	pathv = reallocarray(pglob->gl_pathv, newn, sizeof(*pathv));
	if (pathv == NULL)
		return (GLOB_NOSPACE);

	if (pglob->gl_pathv == NULL && pglob->gl_offs > 0) {
		/* first time around -- clear initial gl_offs items */
		pathv += pglob->gl_offs;
		for (i = pglob->gl_offs + 1; --i > 0; )
			*--pathv = NULL;
	}
	pglob->gl_pathv = pathv;

	if (origpat != NULL)
		copy = strdup(origpat);
	else {
		for (p = path; *p++ != EOS;)
			continue;
		len = MB_CUR_MAX * (size_t)(p - path); /* XXX overallocation */
		if ((copy = malloc(len)) != NULL) {
			if (g_Ctoc(path, copy, len)) {
				free(copy);
				errno = E2BIG;
				return (GLOB_NOSPACE);
			}
		}
	}
	if (copy != NULL) {
		limit->l_string_cnt += strlen(copy) + 1;
		if ((pglob->gl_flags & GLOB_LIMIT) &&
		    limit->l_string_cnt >= GLOB_LIMIT_STRING) {
			free(copy);
			errno = E2BIG;
			return (GLOB_NOSPACE);
		}
		pathv[pglob->gl_offs + pglob->gl_pathc++] = copy;
	}
	pathv[pglob->gl_offs + pglob->gl_pathc] = NULL;
	return (copy == NULL ? GLOB_NOSPACE : 0);
}

/*
 * pattern matching function for filenames.
 */
static int
match(Char *name, Char *pat, Char *patend)
{
	int ok, negate_range;
	Char c, k, *nextp, *nextn;
	struct xlocale_collate *table =
		(struct xlocale_collate*)__get_locale()->components[XLC_COLLATE];

	nextn = NULL;
	nextp = NULL;

	while (1) {
		while (pat < patend) {
			c = *pat++;
			switch (c & M_MASK) {
			case M_ALL:
				if (pat == patend)
					return (1);
				if (*name == EOS)
					return (0);
				nextn = name + 1;
				nextp = pat - 1;
				break;
			case M_ONE:
				if (*name++ == EOS)
					goto fail;
				break;
			case M_SET:
				ok = 0;
				if ((k = *name++) == EOS)
					goto fail;
				negate_range = ((*pat & M_MASK) == M_NOT);
				if (negate_range != 0)
					++pat;
				while (((c = *pat++) & M_MASK) != M_END)
					if ((*pat & M_MASK) == M_RNG) {
						if (table->__collate_load_error ?
						    CHAR(c) <= CHAR(k) &&
						    CHAR(k) <= CHAR(pat[1]) :
						    __wcollate_range_cmp(CHAR(c),
						    CHAR(k)) <= 0 &&
						    __wcollate_range_cmp(CHAR(k),
						    CHAR(pat[1])) <= 0)
							ok = 1;
						pat += 2;
					} else if (c == k)
						ok = 1;
				if (ok == negate_range)
					goto fail;
				break;
			default:
				if (*name++ != c)
					goto fail;
				break;
			}
		}
		if (*name == EOS)
			return (1);

	fail:
		if (nextn == NULL)
			break;
		pat = nextp;
		name = nextn;
	}
	return (0);
}

/* Free allocated data belonging to a glob11_t structure. */
void
freebsd11_globfree(glob11_t *pglob)
{
	size_t i;
	char **pp;

	if (pglob->gl_pathv != NULL) {
		pp = pglob->gl_pathv + pglob->gl_offs;
		for (i = pglob->gl_pathc; i--; ++pp)
			if (*pp)
				free(*pp);
		free(pglob->gl_pathv);
		pglob->gl_pathv = NULL;
	}
}

static DIR *
g_opendir(Char *str, glob11_t *pglob)
{
	char buf[MAXPATHLEN + MB_LEN_MAX - 1];

	if (*str == EOS)
		strcpy(buf, ".");
	else {
		if (g_Ctoc(str, buf, sizeof(buf))) {
			errno = ENAMETOOLONG;
			return (NULL);
		}
	}

	if (pglob->gl_flags & GLOB_ALTDIRFUNC)
		return ((*pglob->gl_opendir)(buf));

	return (opendir(buf));
}

static int
g_lstat(Char *fn, struct freebsd11_stat *sb, glob11_t *pglob)
{
	char buf[MAXPATHLEN + MB_LEN_MAX - 1];

	if (g_Ctoc(fn, buf, sizeof(buf))) {
		errno = ENAMETOOLONG;
		return (-1);
	}
	if (pglob->gl_flags & GLOB_ALTDIRFUNC)
		return((*pglob->gl_lstat)(buf, sb));
	return (freebsd11_lstat(buf, sb));
}

static int
g_stat(Char *fn, struct freebsd11_stat *sb, glob11_t *pglob)
{
	char buf[MAXPATHLEN + MB_LEN_MAX - 1];

	if (g_Ctoc(fn, buf, sizeof(buf))) {
		errno = ENAMETOOLONG;
		return (-1);
	}
	if (pglob->gl_flags & GLOB_ALTDIRFUNC)
		return ((*pglob->gl_stat)(buf, sb));
	return (freebsd11_stat(buf, sb));
}

static const Char *
g_strchr(const Char *str, wchar_t ch)
{

	do {
		if (*str == ch)
			return (str);
	} while (*str++);
	return (NULL);
}

static int
g_Ctoc(const Char *str, char *buf, size_t len)
{
	mbstate_t mbs;
	size_t clen;

	memset(&mbs, 0, sizeof(mbs));
	while (len >= MB_CUR_MAX) {
		clen = wcrtomb(buf, CHAR(*str), &mbs);
		if (clen == (size_t)-1) {
			/* XXX See initial comment #2. */
			*buf = (char)CHAR(*str);
			clen = 1;
			memset(&mbs, 0, sizeof(mbs));
		}
		if (CHAR(*str) == EOS)
			return (0);
		str++;
		buf += clen;
		len -= clen;
	}
	return (1);
}

static int
err_nomatch(glob11_t *pglob, struct glob_limit *limit, const char *origpat) {
	/*
	 * If there was no match we are going to append the origpat
	 * if GLOB_NOCHECK was specified or if GLOB_NOMAGIC was specified
	 * and the origpat did not contain any magic characters
	 * GLOB_NOMAGIC is there just for compatibility with csh.
	 */
	if ((pglob->gl_flags & GLOB_NOCHECK) ||
	    ((pglob->gl_flags & GLOB_NOMAGIC) &&
	    !(pglob->gl_flags & GLOB_MAGCHAR)))
		return (globextend(NULL, pglob, limit, origpat));
	return (GLOB_NOMATCH);
}

static int
err_aborted(glob11_t *pglob, int err, char *buf) {
	if ((pglob->gl_errfunc != NULL && pglob->gl_errfunc(buf, err)) ||
	    (pglob->gl_flags & GLOB_ERR))
		return (GLOB_ABORTED);
	return (0);
}

#ifdef DEBUG
static void
qprintf(const char *str, Char *s)
{
	Char *p;

	(void)printf("%s\n", str);
	if (s != NULL) {
		for (p = s; *p != EOS; p++)
			(void)printf("%c", (char)CHAR(*p));
		(void)printf("\n");
		for (p = s; *p != EOS; p++)
			(void)printf("%c", (isprot(*p) ? '\\' : ' '));
		(void)printf("\n");
		for (p = s; *p != EOS; p++)
			(void)printf("%c", (ismeta(*p) ? '_' : ' '));
		(void)printf("\n");
	}
}
#endif

__sym_compat(glob, freebsd11_glob, FBSD_1.0);
__sym_compat(globfree, freebsd11_globfree, FBSD_1.0);
