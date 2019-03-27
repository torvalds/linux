/*	$NetBSD: util.c,v 1.9 2011/02/27 17:33:37 joerg Exp $	*/
/*	$FreeBSD$	*/
/*	$OpenBSD: util.c,v 1.39 2010/07/02 22:18:03 tedu Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 James Howard and Dag-Erling Coïdan Smørgrav
 * Copyright (C) 2008-2010 Gabor Kovesdan <gabor@FreeBSD.org>
 * Copyright (C) 2017 Kyle Evans <kevans@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/stat.h>
#include <sys/types.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fnmatch.h>
#include <fts.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

#include "grep.h"

static bool	 first_match = true;

/*
 * Match printing context
 */
struct mprintc {
	long long	tail;		/* Number of trailing lines to record */
	int		last_outed;	/* Number of lines since last output */
	bool		doctx;		/* Printing context? */
	bool		printmatch;	/* Printing matches? */
	bool		same_file;	/* Same file as previously printed? */
};

static void procmatch_match(struct mprintc *mc, struct parsec *pc);
static void procmatch_nomatch(struct mprintc *mc, struct parsec *pc);
static bool procmatches(struct mprintc *mc, struct parsec *pc, bool matched);
#ifdef WITH_INTERNAL_NOSPEC
static int litexec(const struct pat *pat, const char *string,
    size_t nmatch, regmatch_t pmatch[]);
#endif
static bool procline(struct parsec *pc);
static void printline(struct parsec *pc, int sep);
static void printline_metadata(struct str *line, int sep);

bool
file_matching(const char *fname)
{
	char *fname_base, *fname_buf;
	bool ret;

	ret = finclude ? false : true;
	fname_buf = strdup(fname);
	if (fname_buf == NULL)
		err(2, "strdup");
	fname_base = basename(fname_buf);

	for (unsigned int i = 0; i < fpatterns; ++i) {
		if (fnmatch(fpattern[i].pat, fname, 0) == 0 ||
		    fnmatch(fpattern[i].pat, fname_base, 0) == 0)
			/*
			 * The last pattern matched wins exclusion/inclusion
			 * rights, so we can't reasonably bail out early here.
			 */
			ret = (fpattern[i].mode != EXCL_PAT);
	}
	free(fname_buf);
	return (ret);
}

static inline bool
dir_matching(const char *dname)
{
	bool ret;

	ret = dinclude ? false : true;

	for (unsigned int i = 0; i < dpatterns; ++i) {
		if (dname != NULL && fnmatch(dpattern[i].pat, dname, 0) == 0)
			/*
			 * The last pattern matched wins exclusion/inclusion
			 * rights, so we can't reasonably bail out early here.
			 */
			ret = (dpattern[i].mode != EXCL_PAT);
	}
	return (ret);
}

/*
 * Processes a directory when a recursive search is performed with
 * the -R option.  Each appropriate file is passed to procfile().
 */
bool
grep_tree(char **argv)
{
	FTS *fts;
	FTSENT *p;
	int fts_flags;
	bool matched, ok;
	const char *wd[] = { ".", NULL };

	matched = false;

	/* This switch effectively initializes 'fts_flags' */
	switch(linkbehave) {
	case LINK_EXPLICIT:
		fts_flags = FTS_COMFOLLOW;
		break;
	case LINK_SKIP:
		fts_flags = FTS_PHYSICAL;
		break;
	default:
		fts_flags = FTS_LOGICAL;
	}

	fts_flags |= FTS_NOSTAT | FTS_NOCHDIR;

	fts = fts_open((argv[0] == NULL) ?
	    __DECONST(char * const *, wd) : argv, fts_flags, NULL);
	if (fts == NULL)
		err(2, "fts_open");
	while ((p = fts_read(fts)) != NULL) {
		switch (p->fts_info) {
		case FTS_DNR:
			/* FALLTHROUGH */
		case FTS_ERR:
			file_err = true;
			if(!sflag)
				warnx("%s: %s", p->fts_path, strerror(p->fts_errno));
			break;
		case FTS_D:
			/* FALLTHROUGH */
		case FTS_DP:
			if (dexclude || dinclude)
				if (!dir_matching(p->fts_name) ||
				    !dir_matching(p->fts_path))
					fts_set(fts, p, FTS_SKIP);
			break;
		case FTS_DC:
			/* Print a warning for recursive directory loop */
			warnx("warning: %s: recursive directory loop",
			    p->fts_path);
			break;
		default:
			/* Check for file exclusion/inclusion */
			ok = true;
			if (fexclude || finclude)
				ok &= file_matching(p->fts_path);

			if (ok && procfile(p->fts_path))
				matched = true;
			break;
		}
	}

	fts_close(fts);
	return (matched);
}

static void
procmatch_match(struct mprintc *mc, struct parsec *pc)
{

	if (mc->doctx) {
		if (!first_match && (!mc->same_file || mc->last_outed > 0))
			printf("--\n");
		if (Bflag > 0)
			printqueue();
		mc->tail = Aflag;
	}

	/* Print the matching line, but only if not quiet/binary */
	if (mc->printmatch) {
		printline(pc, ':');
		while (pc->matchidx >= MAX_MATCHES) {
			/* Reset matchidx and try again */
			pc->matchidx = 0;
			if (procline(pc))
				printline(pc, ':');
			else
				break;
		}
		first_match = false;
		mc->same_file = true;
		mc->last_outed = 0;
	}
}

static void
procmatch_nomatch(struct mprintc *mc, struct parsec *pc)
{

	/* Deal with any -A context as needed */
	if (mc->tail > 0) {
		grep_printline(&pc->ln, '-');
		mc->tail--;
		if (Bflag > 0)
			clearqueue();
	} else if (Bflag == 0 || (Bflag > 0 && enqueue(&pc->ln)))
		/*
		 * Enqueue non-matching lines for -B context. If we're not
		 * actually doing -B context or if the enqueue resulted in a
		 * line being rotated out, then go ahead and increment
		 * last_outed to signify a gap between context/match.
		 */
		++mc->last_outed;
}

/*
 * Process any matches in the current parsing context, return a boolean
 * indicating whether we should halt any further processing or not. 'true' to
 * continue processing, 'false' to halt.
 */
static bool
procmatches(struct mprintc *mc, struct parsec *pc, bool matched)
{

	/*
	 * XXX TODO: This should loop over pc->matches and handle things on a
	 * line-by-line basis, setting up a `struct str` as needed.
	 */
	/* Deal with any -B context or context separators */
	if (matched) {
		procmatch_match(mc, pc);

		/* Count the matches if we have a match limit */
		if (mflag) {
			/* XXX TODO: Decrement by number of matched lines */
			mcount -= 1;
			if (mcount <= 0)
				return (false);
		}
	} else if (mc->doctx)
		procmatch_nomatch(mc, pc);

	return (true);
}

/*
 * Opens a file and processes it.  Each file is processed line-by-line
 * passing the lines to procline().
 */
bool
procfile(const char *fn)
{
	struct parsec pc;
	struct mprintc mc;
	struct file *f;
	struct stat sb;
	mode_t s;
	int lines;
	bool line_matched;

	if (strcmp(fn, "-") == 0) {
		fn = label != NULL ? label : errstr[1];
		f = grep_open(NULL);
	} else {
		if (stat(fn, &sb) == 0) {
			/* Check if we need to process the file */
			s = sb.st_mode & S_IFMT;
			if (dirbehave == DIR_SKIP && s == S_IFDIR)
				return (false);
			if (devbehave == DEV_SKIP && (s == S_IFIFO ||
			    s == S_IFCHR || s == S_IFBLK || s == S_IFSOCK))
				return (false);
		}
		f = grep_open(fn);
	}
	if (f == NULL) {
		file_err = true;
		if (!sflag)
			warn("%s", fn);
		return (false);
	}

	pc.ln.file = grep_strdup(fn);
	pc.ln.line_no = 0;
	pc.ln.len = 0;
	pc.ln.boff = 0;
	pc.ln.off = -1;
	pc.binary = f->binary;
	pc.cntlines = false;
	memset(&mc, 0, sizeof(mc));
	mc.printmatch = true;
	if ((pc.binary && binbehave == BINFILE_BIN) || cflag || qflag ||
	    lflag || Lflag)
		mc.printmatch = false;
	if (mc.printmatch && (Aflag != 0 || Bflag != 0))
		mc.doctx = true;
	if (mc.printmatch && (Aflag != 0 || Bflag != 0 || mflag || nflag))
		pc.cntlines = true;
	mcount = mlimit;

	for (lines = 0; lines == 0 || !(lflag || qflag); ) {
		/*
		 * XXX TODO: We need to revisit this in a chunking world. We're
		 * not going to be doing per-line statistics because of the
		 * overhead involved. procmatches can figure that stuff out as
		 * needed. */
		/* Reset per-line statistics */
		pc.printed = 0;
		pc.matchidx = 0;
		pc.lnstart = 0;
		pc.ln.boff = 0;
		pc.ln.off += pc.ln.len + 1;
		/* XXX TODO: Grab a chunk */
		if ((pc.ln.dat = grep_fgetln(f, &pc)) == NULL ||
		    pc.ln.len == 0)
			break;

		if (pc.ln.len > 0 && pc.ln.dat[pc.ln.len - 1] == fileeol)
			--pc.ln.len;
		pc.ln.line_no++;

		/* Return if we need to skip a binary file */
		if (pc.binary && binbehave == BINFILE_SKIP) {
			grep_close(f);
			free(pc.ln.file);
			free(f);
			return (0);
		}

		line_matched = procline(&pc);
		if (line_matched)
			++lines;

		/* Halt processing if we hit our match limit */
		if (!procmatches(&mc, &pc, line_matched))
			break;
	}
	if (Bflag > 0)
		clearqueue();
	grep_close(f);

	if (cflag) {
		if (!hflag)
			printf("%s:", pc.ln.file);
		printf("%u\n", lines);
	}
	if (lflag && !qflag && lines != 0)
		printf("%s%c", fn, nullflag ? 0 : '\n');
	if (Lflag && !qflag && lines == 0)
		printf("%s%c", fn, nullflag ? 0 : '\n');
	if (lines != 0 && !cflag && !lflag && !Lflag &&
	    binbehave == BINFILE_BIN && f->binary && !qflag)
		printf(errstr[7], fn);

	free(pc.ln.file);
	free(f);
	return (lines != 0);
}

#ifdef WITH_INTERNAL_NOSPEC
/*
 * Internal implementation of literal string search within a string, modeled
 * after regexec(3), for use when the regex(3) implementation doesn't offer
 * either REG_NOSPEC or REG_LITERAL. This does not apply in the default FreeBSD
 * config, but in other scenarios such as building against libgnuregex or on
 * some non-FreeBSD OSes.
 */
static int
litexec(const struct pat *pat, const char *string, size_t nmatch,
    regmatch_t pmatch[])
{
	char *(*strstr_fn)(const char *, const char *);
	char *sub, *subject;
	const char *search;
	size_t idx, n, ofs, stringlen;

	if (cflags & REG_ICASE)
		strstr_fn = strcasestr;
	else
		strstr_fn = strstr;
	idx = 0;
	ofs = pmatch[0].rm_so;
	stringlen = pmatch[0].rm_eo;
	if (ofs >= stringlen)
		return (REG_NOMATCH);
	subject = strndup(string, stringlen);
	if (subject == NULL)
		return (REG_ESPACE);
	for (n = 0; ofs < stringlen;) {
		search = (subject + ofs);
		if ((unsigned long)pat->len > strlen(search))
			break;
		sub = strstr_fn(search, pat->pat);
		/*
		 * Ignoring the empty string possibility due to context: grep optimizes
		 * for empty patterns and will never reach this point.
		 */
		if (sub == NULL)
			break;
		++n;
		/* Fill in pmatch if necessary */
		if (nmatch > 0) {
			pmatch[idx].rm_so = ofs + (sub - search);
			pmatch[idx].rm_eo = pmatch[idx].rm_so + pat->len;
			if (++idx == nmatch)
				break;
			ofs = pmatch[idx].rm_so + 1;
		} else
			/* We only needed to know if we match or not */
			break;
	}
	free(subject);
	if (n > 0 && nmatch > 0)
		for (n = idx; n < nmatch; ++n)
			pmatch[n].rm_so = pmatch[n].rm_eo = -1;

	return (n > 0 ? 0 : REG_NOMATCH);
}
#endif /* WITH_INTERNAL_NOSPEC */

#define iswword(x)	(iswalnum((x)) || (x) == L'_')

/*
 * Processes a line comparing it with the specified patterns.  Each pattern
 * is looped to be compared along with the full string, saving each and every
 * match, which is necessary to colorize the output and to count the
 * matches.  The matching lines are passed to printline() to display the
 * appropriate output.
 */
static bool
procline(struct parsec *pc)
{
	regmatch_t pmatch, lastmatch, chkmatch;
	wchar_t wbegin, wend;
	size_t st, nst;
	unsigned int i;
	int r = 0, leflags = eflags;
	size_t startm = 0, matchidx;
	unsigned int retry;
	bool lastmatched, matched;

	matchidx = pc->matchidx;

	/* Special case: empty pattern with -w flag, check first character */
	if (matchall && wflag) {
		if (pc->ln.len == 0)
			return (true);
		wend = L' ';
		if (sscanf(&pc->ln.dat[0], "%lc", &wend) != 1 || iswword(wend))
			return (false);
		else
			return (true);
	} else if (matchall)
		return (true);

	matched = false;
	st = pc->lnstart;
	nst = 0;
	/* Initialize to avoid a false positive warning from GCC. */
	lastmatch.rm_so = lastmatch.rm_eo = 0;

	/* Loop to process the whole line */
	while (st <= pc->ln.len) {
		lastmatched = false;
		startm = matchidx;
		retry = 0;
		if (st > 0 && pc->ln.dat[st - 1] != fileeol)
			leflags |= REG_NOTBOL;
		/* Loop to compare with all the patterns */
		for (i = 0; i < patterns; i++) {
			pmatch.rm_so = st;
			pmatch.rm_eo = pc->ln.len;
#ifdef WITH_INTERNAL_NOSPEC
			if (grepbehave == GREP_FIXED)
				r = litexec(&pattern[i], pc->ln.dat, 1, &pmatch);
			else
#endif
			r = regexec(&r_pattern[i], pc->ln.dat, 1, &pmatch,
			    leflags);
			if (r != 0)
				continue;
			/* Check for full match */
			if (xflag && (pmatch.rm_so != 0 ||
			    (size_t)pmatch.rm_eo != pc->ln.len))
				continue;
			/* Check for whole word match */
			if (wflag) {
				wbegin = wend = L' ';
				if (pmatch.rm_so != 0 &&
				    sscanf(&pc->ln.dat[pmatch.rm_so - 1],
				    "%lc", &wbegin) != 1)
					r = REG_NOMATCH;
				else if ((size_t)pmatch.rm_eo !=
				    pc->ln.len &&
				    sscanf(&pc->ln.dat[pmatch.rm_eo],
				    "%lc", &wend) != 1)
					r = REG_NOMATCH;
				else if (iswword(wbegin) ||
				    iswword(wend))
					r = REG_NOMATCH;
				/*
				 * If we're doing whole word matching and we
				 * matched once, then we should try the pattern
				 * again after advancing just past the start of
				 * the earliest match. This allows the pattern
				 * to  match later on in the line and possibly
				 * still match a whole word.
				 */
				if (r == REG_NOMATCH &&
				    (retry == pc->lnstart ||
				    (unsigned int)pmatch.rm_so + 1 < retry))
					retry = pmatch.rm_so + 1;
				if (r == REG_NOMATCH)
					continue;
			}
			lastmatched = true;
			lastmatch = pmatch;

			if (matchidx == 0)
				matched = true;

			/*
			 * Replace previous match if the new one is earlier
			 * and/or longer. This will lead to some amount of
			 * extra work if -o/--color are specified, but it's
			 * worth it from a correctness point of view.
			 */
			if (matchidx > startm) {
				chkmatch = pc->matches[matchidx - 1];
				if (pmatch.rm_so < chkmatch.rm_so ||
				    (pmatch.rm_so == chkmatch.rm_so &&
				    (pmatch.rm_eo - pmatch.rm_so) >
				    (chkmatch.rm_eo - chkmatch.rm_so))) {
					pc->matches[matchidx - 1] = pmatch;
					nst = pmatch.rm_eo;
				}
			} else {
				/* Advance as normal if not */
				pc->matches[matchidx++] = pmatch;
				nst = pmatch.rm_eo;
			}
			/* avoid excessive matching - skip further patterns */
			if ((color == NULL && !oflag) || qflag || lflag ||
			    matchidx >= MAX_MATCHES) {
				pc->lnstart = nst;
				lastmatched = false;
				break;
			}
		}

		/*
		 * Advance to just past the start of the earliest match, try
		 * again just in case we still have a chance to match later in
		 * the string.
		 */
		if (!lastmatched && retry > pc->lnstart) {
			st = retry;
			continue;
		}

		/* XXX TODO: We will need to keep going, since we're chunky */
		/* One pass if we are not recording matches */
		if (!wflag && ((color == NULL && !oflag) || qflag || lflag || Lflag))
			break;

		/* If we didn't have any matches or REG_NOSUB set */
		if (!lastmatched || (cflags & REG_NOSUB))
			nst = pc->ln.len;

		if (!lastmatched)
			/* No matches */
			break;
		else if (st == nst && lastmatch.rm_so == lastmatch.rm_eo)
			/* Zero-length match -- advance one more so we don't get stuck */
			nst++;

		/* Advance st based on previous matches */
		st = nst;
		pc->lnstart = st;
	}

	/* Reflect the new matchidx in the context */
	pc->matchidx = matchidx;
	if (vflag)
		matched = !matched;
	return matched;
}

/*
 * Safe malloc() for internal use.
 */
void *
grep_malloc(size_t size)
{
	void *ptr;

	if ((ptr = malloc(size)) == NULL)
		err(2, "malloc");
	return (ptr);
}

/*
 * Safe calloc() for internal use.
 */
void *
grep_calloc(size_t nmemb, size_t size)
{
	void *ptr;

	if ((ptr = calloc(nmemb, size)) == NULL)
		err(2, "calloc");
	return (ptr);
}

/*
 * Safe realloc() for internal use.
 */
void *
grep_realloc(void *ptr, size_t size)
{

	if ((ptr = realloc(ptr, size)) == NULL)
		err(2, "realloc");
	return (ptr);
}

/*
 * Safe strdup() for internal use.
 */
char *
grep_strdup(const char *str)
{
	char *ret;

	if ((ret = strdup(str)) == NULL)
		err(2, "strdup");
	return (ret);
}

/*
 * Print an entire line as-is, there are no inline matches to consider. This is
 * used for printing context.
 */
void grep_printline(struct str *line, int sep) {
	printline_metadata(line, sep);
	fwrite(line->dat, line->len, 1, stdout);
	putchar(fileeol);
}

static void
printline_metadata(struct str *line, int sep)
{
	bool printsep;

	printsep = false;
	if (!hflag) {
		if (!nullflag) {
			fputs(line->file, stdout);
			printsep = true;
		} else {
			printf("%s", line->file);
			putchar(0);
		}
	}
	if (nflag) {
		if (printsep)
			putchar(sep);
		printf("%d", line->line_no);
		printsep = true;
	}
	if (bflag) {
		if (printsep)
			putchar(sep);
		printf("%lld", (long long)(line->off + line->boff));
		printsep = true;
	}
	if (printsep)
		putchar(sep);
}

/*
 * Prints a matching line according to the command line options.
 */
static void
printline(struct parsec *pc, int sep)
{
	size_t a = 0;
	size_t i, matchidx;
	regmatch_t match;

	/* If matchall, everything matches but don't actually print for -o */
	if (oflag && matchall)
		return;

	matchidx = pc->matchidx;

	/* --color and -o */
	if ((oflag || color) && matchidx > 0) {
		/* Only print metadata once per line if --color */
		if (!oflag && pc->printed == 0)
			printline_metadata(&pc->ln, sep);
		for (i = 0; i < matchidx; i++) {
			match = pc->matches[i];
			/* Don't output zero length matches */
			if (match.rm_so == match.rm_eo)
				continue;
			/*
			 * Metadata is printed on a per-line basis, so every
			 * match gets file metadata with the -o flag.
			 */
			if (oflag) {
				pc->ln.boff = match.rm_so;
				printline_metadata(&pc->ln, sep);
			} else
				fwrite(pc->ln.dat + a, match.rm_so - a, 1,
				    stdout);
			if (color)
				fprintf(stdout, "\33[%sm\33[K", color);
			fwrite(pc->ln.dat + match.rm_so,
			    match.rm_eo - match.rm_so, 1, stdout);
			if (color)
				fprintf(stdout, "\33[m\33[K");
			a = match.rm_eo;
			if (oflag)
				putchar('\n');
		}
		if (!oflag) {
			if (pc->ln.len - a > 0)
				fwrite(pc->ln.dat + a, pc->ln.len - a, 1,
				    stdout);
			putchar('\n');
		}
	} else
		grep_printline(&pc->ln, sep);
	pc->printed++;
}
