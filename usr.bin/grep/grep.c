/*	$NetBSD: grep.c,v 1.6 2011/04/18 03:48:23 joerg Exp $	*/
/* 	$FreeBSD$	*/
/*	$OpenBSD: grep.c,v 1.42 2010/07/02 22:18:03 tedu Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 James Howard and Dag-Erling Coïdan Smørgrav
 * Copyright (C) 2008-2009 Gabor Kovesdan <gabor@FreeBSD.org>
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
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <libgen.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "grep.h"

const char	*errstr[] = {
	"",
/* 1*/	"(standard input)",
/* 2*/	"unknown %s option",
/* 3*/	"usage: %s [-abcDEFGHhIiLlmnOoPqRSsUVvwxz] [-A num] [-B num] [-C[num]]\n",
/* 4*/	"\t[-e pattern] [-f file] [--binary-files=value] [--color=when]\n",
/* 5*/	"\t[--context[=num]] [--directories=action] [--label] [--line-buffered]\n",
/* 6*/	"\t[--null] [pattern] [file ...]\n",
/* 7*/	"Binary file %s matches\n",
/* 8*/	"%s (BSD grep) %s\n",
/* 9*/	"%s (BSD grep, GNU compatible) %s\n",
};

/* Flags passed to regcomp() and regexec() */
int		 cflags = REG_NOSUB | REG_NEWLINE;
int		 eflags = REG_STARTEND;

/* XXX TODO: Get rid of this flag.
 * matchall is a gross hack that means that an empty pattern was passed to us.
 * It is a necessary evil at the moment because our regex(3) implementation
 * does not allow for empty patterns, as supported by POSIX's definition of
 * grammar for BREs/EREs. When libregex becomes available, it would be wise
 * to remove this and let regex(3) handle the dirty details of empty patterns.
 */
bool		 matchall;

/* Searching patterns */
unsigned int	 patterns;
static unsigned int pattern_sz;
struct pat	*pattern;
regex_t		*r_pattern;

/* Filename exclusion/inclusion patterns */
unsigned int	fpatterns, dpatterns;
static unsigned int fpattern_sz, dpattern_sz;
struct epat	*dpattern, *fpattern;

/* For regex errors  */
char	 re_error[RE_ERROR_BUF + 1];

/* Command-line flags */
long long Aflag;	/* -A x: print x lines trailing each match */
long long Bflag;	/* -B x: print x lines leading each match */
bool	 Hflag;		/* -H: always print file name */
bool	 Lflag;		/* -L: only show names of files with no matches */
bool	 bflag;		/* -b: show block numbers for each match */
bool	 cflag;		/* -c: only show a count of matching lines */
bool	 hflag;		/* -h: don't print filename headers */
bool	 iflag;		/* -i: ignore case */
bool	 lflag;		/* -l: only show names of files with matches */
bool	 mflag;		/* -m x: stop reading the files after x matches */
long long mcount;	/* count for -m */
long long mlimit;	/* requested value for -m */
char	 fileeol;	/* indicator for eol */
bool	 nflag;		/* -n: show line numbers in front of matching lines */
bool	 oflag;		/* -o: print only matching part */
bool	 qflag;		/* -q: quiet mode (don't output anything) */
bool	 sflag;		/* -s: silent mode (ignore errors) */
bool	 vflag;		/* -v: only show non-matching lines */
bool	 wflag;		/* -w: pattern must start and end on word boundaries */
bool	 xflag;		/* -x: pattern must match entire line */
bool	 lbflag;	/* --line-buffered */
bool	 nullflag;	/* --null */
char	*label;		/* --label */
const char *color;	/* --color */
int	 grepbehave = GREP_BASIC;	/* -EFGP: type of the regex */
int	 binbehave = BINFILE_BIN;	/* -aIU: handling of binary files */
int	 filebehave = FILE_STDIO;
int	 devbehave = DEV_READ;		/* -D: handling of devices */
int	 dirbehave = DIR_READ;		/* -dRr: handling of directories */
int	 linkbehave = LINK_READ;	/* -OpS: handling of symlinks */

bool	 dexclude, dinclude;	/* --exclude-dir and --include-dir */
bool	 fexclude, finclude;	/* --exclude and --include */

enum {
	BIN_OPT = CHAR_MAX + 1,
	COLOR_OPT,
	HELP_OPT,
	MMAP_OPT,
	LINEBUF_OPT,
	LABEL_OPT,
	NULL_OPT,
	R_EXCLUDE_OPT,
	R_INCLUDE_OPT,
	R_DEXCLUDE_OPT,
	R_DINCLUDE_OPT
};

static inline const char	*init_color(const char *);

/* Housekeeping */
bool	 file_err;	/* file reading error */

/*
 * Prints usage information and returns 2.
 */
static void
usage(void)
{
	fprintf(stderr, errstr[3], getprogname());
	fprintf(stderr, "%s", errstr[4]);
	fprintf(stderr, "%s", errstr[5]);
	fprintf(stderr, "%s", errstr[6]);
	exit(2);
}

static const char	*optstr = "0123456789A:B:C:D:EFGHILOPSRUVabcd:e:f:hilm:nopqrsuvwxyz";

static const struct option long_options[] =
{
	{"binary-files",	required_argument,	NULL, BIN_OPT},
	{"help",		no_argument,		NULL, HELP_OPT},
	{"mmap",		no_argument,		NULL, MMAP_OPT},
	{"line-buffered",	no_argument,		NULL, LINEBUF_OPT},
	{"label",		required_argument,	NULL, LABEL_OPT},
	{"null",		no_argument,		NULL, NULL_OPT},
	{"color",		optional_argument,	NULL, COLOR_OPT},
	{"colour",		optional_argument,	NULL, COLOR_OPT},
	{"exclude",		required_argument,	NULL, R_EXCLUDE_OPT},
	{"include",		required_argument,	NULL, R_INCLUDE_OPT},
	{"exclude-dir",		required_argument,	NULL, R_DEXCLUDE_OPT},
	{"include-dir",		required_argument,	NULL, R_DINCLUDE_OPT},
	{"after-context",	required_argument,	NULL, 'A'},
	{"text",		no_argument,		NULL, 'a'},
	{"before-context",	required_argument,	NULL, 'B'},
	{"byte-offset",		no_argument,		NULL, 'b'},
	{"context",		optional_argument,	NULL, 'C'},
	{"count",		no_argument,		NULL, 'c'},
	{"devices",		required_argument,	NULL, 'D'},
        {"directories",		required_argument,	NULL, 'd'},
	{"extended-regexp",	no_argument,		NULL, 'E'},
	{"regexp",		required_argument,	NULL, 'e'},
	{"fixed-strings",	no_argument,		NULL, 'F'},
	{"file",		required_argument,	NULL, 'f'},
	{"basic-regexp",	no_argument,		NULL, 'G'},
	{"no-filename",		no_argument,		NULL, 'h'},
	{"with-filename",	no_argument,		NULL, 'H'},
	{"ignore-case",		no_argument,		NULL, 'i'},
	{"files-with-matches",	no_argument,		NULL, 'l'},
	{"files-without-match", no_argument,            NULL, 'L'},
	{"max-count",		required_argument,	NULL, 'm'},
	{"line-number",		no_argument,		NULL, 'n'},
	{"only-matching",	no_argument,		NULL, 'o'},
	{"quiet",		no_argument,		NULL, 'q'},
	{"silent",		no_argument,		NULL, 'q'},
	{"recursive",		no_argument,		NULL, 'r'},
	{"no-messages",		no_argument,		NULL, 's'},
	{"binary",		no_argument,		NULL, 'U'},
	{"unix-byte-offsets",	no_argument,		NULL, 'u'},
	{"invert-match",	no_argument,		NULL, 'v'},
	{"version",		no_argument,		NULL, 'V'},
	{"word-regexp",		no_argument,		NULL, 'w'},
	{"line-regexp",		no_argument,		NULL, 'x'},
	{"null-data",		no_argument,		NULL, 'z'},
	{NULL,			no_argument,		NULL, 0}
};

/*
 * Adds a searching pattern to the internal array.
 */
static void
add_pattern(char *pat, size_t len)
{

	/* Do not add further pattern is we already match everything */
	if (matchall)
	  return;

	/* Check if we can do a shortcut */
	if (len == 0) {
		matchall = true;
		for (unsigned int i = 0; i < patterns; i++) {
			free(pattern[i].pat);
		}
		pattern = grep_realloc(pattern, sizeof(struct pat));
		pattern[0].pat = NULL;
		pattern[0].len = 0;
		patterns = 1;
		return;
	}
	/* Increase size if necessary */
	if (patterns == pattern_sz) {
		pattern_sz *= 2;
		pattern = grep_realloc(pattern, ++pattern_sz *
		    sizeof(struct pat));
	}
	if (len > 0 && pat[len - 1] == '\n')
		--len;
	/* pat may not be NUL-terminated */
	pattern[patterns].pat = grep_malloc(len + 1);
	memcpy(pattern[patterns].pat, pat, len);
	pattern[patterns].len = len;
	pattern[patterns].pat[len] = '\0';
	++patterns;
}

/*
 * Adds a file include/exclude pattern to the internal array.
 */
static void
add_fpattern(const char *pat, int mode)
{

	/* Increase size if necessary */
	if (fpatterns == fpattern_sz) {
		fpattern_sz *= 2;
		fpattern = grep_realloc(fpattern, ++fpattern_sz *
		    sizeof(struct epat));
	}
	fpattern[fpatterns].pat = grep_strdup(pat);
	fpattern[fpatterns].mode = mode;
	++fpatterns;
}

/*
 * Adds a directory include/exclude pattern to the internal array.
 */
static void
add_dpattern(const char *pat, int mode)
{

	/* Increase size if necessary */
	if (dpatterns == dpattern_sz) {
		dpattern_sz *= 2;
		dpattern = grep_realloc(dpattern, ++dpattern_sz *
		    sizeof(struct epat));
	}
	dpattern[dpatterns].pat = grep_strdup(pat);
	dpattern[dpatterns].mode = mode;
	++dpatterns;
}

/*
 * Reads searching patterns from a file and adds them with add_pattern().
 */
static void
read_patterns(const char *fn)
{
	struct stat st;
	FILE *f;
	char *line;
	size_t len;
	ssize_t rlen;

	if (strcmp(fn, "-") == 0)
		f = stdin;
	else if ((f = fopen(fn, "r")) == NULL)
		err(2, "%s", fn);
	if ((fstat(fileno(f), &st) == -1) || (S_ISDIR(st.st_mode))) {
		fclose(f);
		return;
	}
	len = 0;
	line = NULL;
	while ((rlen = getline(&line, &len, f)) != -1) {
		if (line[0] == '\0')
			continue;
		add_pattern(line, line[0] == '\n' ? 0 : (size_t)rlen);
	}

	free(line);
	if (ferror(f))
		err(2, "%s", fn);
	if (strcmp(fn, "-") != 0)
		fclose(f);
}

static inline const char *
init_color(const char *d)
{
	char *c;

	c = getenv("GREP_COLOR");
	return (c != NULL && c[0] != '\0' ? c : d);
}

int
main(int argc, char *argv[])
{
	char **aargv, **eargv, *eopts;
	char *ep;
	const char *pn;
	long long l;
	unsigned int aargc, eargc, i;
	int c, lastc, needpattern, newarg, prevoptind;
	bool matched;

	setlocale(LC_ALL, "");

	/* Check what is the program name of the binary.  In this
	   way we can have all the funcionalities in one binary
	   without the need of scripting and using ugly hacks. */
	pn = getprogname();
	if (pn[0] == 'r') {
		dirbehave = DIR_RECURSE;
		Hflag = true;
	}
	switch (pn[0]) {
	case 'e':
		grepbehave = GREP_EXTENDED;
		break;
	case 'f':
		grepbehave = GREP_FIXED;
		break;
	}

	lastc = '\0';
	newarg = 1;
	prevoptind = 1;
	needpattern = 1;
	fileeol = '\n';

	eopts = getenv("GREP_OPTIONS");

	/* support for extra arguments in GREP_OPTIONS */
	eargc = 0;
	if (eopts != NULL && eopts[0] != '\0') {
		char *str;

		/* make an estimation of how many extra arguments we have */
		for (unsigned int j = 0; j < strlen(eopts); j++)
			if (eopts[j] == ' ')
				eargc++;

		eargv = (char **)grep_malloc(sizeof(char *) * (eargc + 1));

		eargc = 0;
		/* parse extra arguments */
		while ((str = strsep(&eopts, " ")) != NULL)
			if (str[0] != '\0')
				eargv[eargc++] = grep_strdup(str);

		aargv = (char **)grep_calloc(eargc + argc + 1,
		    sizeof(char *));

		aargv[0] = argv[0];
		for (i = 0; i < eargc; i++)
			aargv[i + 1] = eargv[i];
		for (int j = 1; j < argc; j++, i++)
			aargv[i + 1] = argv[j];

		aargc = eargc + argc;
	} else {
		aargv = argv;
		aargc = argc;
	}

	while (((c = getopt_long(aargc, aargv, optstr, long_options, NULL)) !=
	    -1)) {
		switch (c) {
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			if (newarg || !isdigit(lastc))
				Aflag = 0;
			else if (Aflag > LLONG_MAX / 10 - 1) {
				errno = ERANGE;
				err(2, NULL);
			}

			Aflag = Bflag = (Aflag * 10) + (c - '0');
			break;
		case 'C':
			if (optarg == NULL) {
				Aflag = Bflag = 2;
				break;
			}
			/* FALLTHROUGH */
		case 'A':
			/* FALLTHROUGH */
		case 'B':
			errno = 0;
			l = strtoll(optarg, &ep, 10);
			if (errno == ERANGE || errno == EINVAL)
				err(2, NULL);
			else if (ep[0] != '\0') {
				errno = EINVAL;
				err(2, NULL);
			} else if (l < 0) {
				errno = EINVAL;
				err(2, "context argument must be non-negative");
			}

			if (c == 'A')
				Aflag = l;
			else if (c == 'B')
				Bflag = l;
			else
				Aflag = Bflag = l;
			break;
		case 'a':
			binbehave = BINFILE_TEXT;
			break;
		case 'b':
			bflag = true;
			break;
		case 'c':
			cflag = true;
			break;
		case 'D':
			if (strcasecmp(optarg, "skip") == 0)
				devbehave = DEV_SKIP;
			else if (strcasecmp(optarg, "read") == 0)
				devbehave = DEV_READ;
			else
				errx(2, errstr[2], "--devices");
			break;
		case 'd':
			if (strcasecmp("recurse", optarg) == 0) {
				Hflag = true;
				dirbehave = DIR_RECURSE;
			} else if (strcasecmp("skip", optarg) == 0)
				dirbehave = DIR_SKIP;
			else if (strcasecmp("read", optarg) == 0)
				dirbehave = DIR_READ;
			else
				errx(2, errstr[2], "--directories");
			break;
		case 'E':
			grepbehave = GREP_EXTENDED;
			break;
		case 'e':
			{
				char *token;
				char *string = optarg;

				while ((token = strsep(&string, "\n")) != NULL)
					add_pattern(token, strlen(token));
			}
			needpattern = 0;
			break;
		case 'F':
			grepbehave = GREP_FIXED;
			break;
		case 'f':
			read_patterns(optarg);
			needpattern = 0;
			break;
		case 'G':
			grepbehave = GREP_BASIC;
			break;
		case 'H':
			Hflag = true;
			break;
		case 'h':
			Hflag = false;
			hflag = true;
			break;
		case 'I':
			binbehave = BINFILE_SKIP;
			break;
		case 'i':
		case 'y':
			iflag =  true;
			cflags |= REG_ICASE;
			break;
		case 'L':
			lflag = false;
			Lflag = true;
			break;
		case 'l':
			Lflag = false;
			lflag = true;
			break;
		case 'm':
			mflag = true;
			errno = 0;
			mlimit = mcount = strtoll(optarg, &ep, 10);
			if (((errno == ERANGE) && (mcount == LLONG_MAX)) ||
			    ((errno == EINVAL) && (mcount == 0)))
				err(2, NULL);
			else if (ep[0] != '\0') {
				errno = EINVAL;
				err(2, NULL);
			}
			break;
		case 'n':
			nflag = true;
			break;
		case 'O':
			linkbehave = LINK_EXPLICIT;
			break;
		case 'o':
			oflag = true;
			cflags &= ~REG_NOSUB;
			break;
		case 'p':
			linkbehave = LINK_SKIP;
			break;
		case 'q':
			qflag = true;
			break;
		case 'S':
			linkbehave = LINK_READ;
			break;
		case 'R':
		case 'r':
			dirbehave = DIR_RECURSE;
			Hflag = true;
			break;
		case 's':
			sflag = true;
			break;
		case 'U':
			binbehave = BINFILE_BIN;
			break;
		case 'u':
		case MMAP_OPT:
			filebehave = FILE_MMAP;
			break;
		case 'V':
#ifdef WITH_GNU
			printf(errstr[9], getprogname(), VERSION);
#else
			printf(errstr[8], getprogname(), VERSION);
#endif
			exit(0);
		case 'v':
			vflag = true;
			break;
		case 'w':
			wflag = true;
			cflags &= ~REG_NOSUB;
			break;
		case 'x':
			xflag = true;
			cflags &= ~REG_NOSUB;
			break;
		case 'z':
			fileeol = '\0';
			break;
		case BIN_OPT:
			if (strcasecmp("binary", optarg) == 0)
				binbehave = BINFILE_BIN;
			else if (strcasecmp("without-match", optarg) == 0)
				binbehave = BINFILE_SKIP;
			else if (strcasecmp("text", optarg) == 0)
				binbehave = BINFILE_TEXT;
			else
				errx(2, errstr[2], "--binary-files");
			break;
		case COLOR_OPT:
			color = NULL;
			if (optarg == NULL || strcasecmp("auto", optarg) == 0 ||
			    strcasecmp("tty", optarg) == 0 ||
			    strcasecmp("if-tty", optarg) == 0) {
				char *term;

				term = getenv("TERM");
				if (isatty(STDOUT_FILENO) && term != NULL &&
				    strcasecmp(term, "dumb") != 0)
					color = init_color("01;31");
			} else if (strcasecmp("always", optarg) == 0 ||
			    strcasecmp("yes", optarg) == 0 ||
			    strcasecmp("force", optarg) == 0) {
				color = init_color("01;31");
			} else if (strcasecmp("never", optarg) != 0 &&
			    strcasecmp("none", optarg) != 0 &&
			    strcasecmp("no", optarg) != 0)
				errx(2, errstr[2], "--color");
			cflags &= ~REG_NOSUB;
			break;
		case LABEL_OPT:
			label = optarg;
			break;
		case LINEBUF_OPT:
			lbflag = true;
			break;
		case NULL_OPT:
			nullflag = true;
			break;
		case R_INCLUDE_OPT:
			finclude = true;
			add_fpattern(optarg, INCL_PAT);
			break;
		case R_EXCLUDE_OPT:
			fexclude = true;
			add_fpattern(optarg, EXCL_PAT);
			break;
		case R_DINCLUDE_OPT:
			dinclude = true;
			add_dpattern(optarg, INCL_PAT);
			break;
		case R_DEXCLUDE_OPT:
			dexclude = true;
			add_dpattern(optarg, EXCL_PAT);
			break;
		case HELP_OPT:
		default:
			usage();
		}
		lastc = c;
		newarg = optind != prevoptind;
		prevoptind = optind;
	}
	aargc -= optind;
	aargv += optind;

	/* Empty pattern file matches nothing */
	if (!needpattern && (patterns == 0))
		exit(1);

	/* Fail if we don't have any pattern */
	if (aargc == 0 && needpattern)
		usage();

	/* Process patterns from command line */
	if (aargc != 0 && needpattern) {
		char *token;
		char *string = *aargv;

		while ((token = strsep(&string, "\n")) != NULL)
			add_pattern(token, strlen(token));
		--aargc;
		++aargv;
	}

	switch (grepbehave) {
	case GREP_BASIC:
		break;
	case GREP_FIXED:
		/*
		 * regex(3) implementations that support fixed-string searches generally
		 * define either REG_NOSPEC or REG_LITERAL. Set the appropriate flag
		 * here. If neither are defined, GREP_FIXED later implies that the
		 * internal literal matcher should be used. Other cflags that have
		 * the same interpretation as REG_NOSPEC and REG_LITERAL should be
		 * similarly added here, and grep.h should be amended to take this into
		 * consideration when defining WITH_INTERNAL_NOSPEC.
		 */
#if defined(REG_NOSPEC)
		cflags |= REG_NOSPEC;
#elif defined(REG_LITERAL)
		cflags |= REG_LITERAL;
#endif
		break;
	case GREP_EXTENDED:
		cflags |= REG_EXTENDED;
		break;
	default:
		/* NOTREACHED */
		usage();
	}

	r_pattern = grep_calloc(patterns, sizeof(*r_pattern));

	/* Don't process any patterns if we have a blank one */
#ifdef WITH_INTERNAL_NOSPEC
	if (!matchall && grepbehave != GREP_FIXED) {
#else
	if (!matchall) {
#endif
		/* Check if cheating is allowed (always is for fgrep). */
		for (i = 0; i < patterns; ++i) {
			c = regcomp(&r_pattern[i], pattern[i].pat, cflags);
			if (c != 0) {
				regerror(c, &r_pattern[i], re_error,
				    RE_ERROR_BUF);
				errx(2, "%s", re_error);
			}
		}
	}

	if (lbflag)
		setlinebuf(stdout);

	if ((aargc == 0 || aargc == 1) && !Hflag)
		hflag = true;

	if (aargc == 0 && dirbehave != DIR_RECURSE)
		exit(!procfile("-"));

	if (dirbehave == DIR_RECURSE)
		matched = grep_tree(aargv);
	else
		for (matched = false; aargc--; ++aargv) {
			if ((finclude || fexclude) && !file_matching(*aargv))
				continue;
			if (procfile(*aargv))
				matched = true;
		}

	/* Find out the correct return value according to the
	   results and the command line option. */
	exit(matched ? (file_err ? (qflag ? 0 : 2) : 0) : (file_err ? 2 : 1));
}
