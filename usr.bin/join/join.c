/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1991, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Steve Hayman of the Computer Science Department, Indiana University,
 * Michiro Hikida and David Goodenough.
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
 */

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1991, 1993, 1994\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)join.c	8.6 (Berkeley) 5/4/95";
#endif
#endif /* not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>

#include <err.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

/*
 * There's a structure per input file which encapsulates the state of the
 * file.  We repeatedly read lines from each file until we've read in all
 * the consecutive lines from the file with a common join field.  Then we
 * compare the set of lines with an equivalent set from the other file.
 */
typedef struct {
	char *line;		/* line */
	u_long linealloc;	/* line allocated count */
	char **fields;		/* line field(s) */
	u_long fieldcnt;	/* line field(s) count */
	u_long fieldalloc;	/* line field(s) allocated count */
} LINE;

typedef struct {
	FILE *fp;		/* file descriptor */
	u_long joinf;		/* join field (-1, -2, -j) */
	int unpair;		/* output unpairable lines (-a) */
	u_long number;		/* 1 for file 1, 2 for file 2 */

	LINE *set;		/* set of lines with same field */
	int pushbool;		/* if pushback is set */
	u_long pushback;	/* line on the stack */
	u_long setcnt;		/* set count */
	u_long setalloc;	/* set allocated count */
} INPUT;
static INPUT input1 = { NULL, 0, 0, 1, NULL, 0, 0, 0, 0 },
    input2 = { NULL, 0, 0, 2, NULL, 0, 0, 0, 0 };

typedef struct {
	u_long	filenum;	/* file number */
	u_long	fieldno;	/* field number */
} OLIST;
static OLIST *olist;		/* output field list */
static u_long olistcnt;		/* output field list count */
static u_long olistalloc;	/* output field allocated count */

static int joinout = 1;		/* show lines with matched join fields (-v) */
static int needsep;		/* need separator character */
static int spans = 1;		/* span multiple delimiters (-t) */
static char *empty;		/* empty field replacement string (-e) */
static wchar_t default_tabchar[] = L" \t";
static wchar_t *tabchar = default_tabchar; /* delimiter characters (-t) */

static int  cmp(LINE *, u_long, LINE *, u_long);
static void fieldarg(char *);
static void joinlines(INPUT *, INPUT *);
static int  mbscoll(const char *, const char *);
static char *mbssep(char **, const wchar_t *);
static void obsolete(char **);
static void outfield(LINE *, u_long, int);
static void outoneline(INPUT *, LINE *);
static void outtwoline(INPUT *, LINE *, INPUT *, LINE *);
static void slurp(INPUT *);
static wchar_t *towcs(const char *);
static void usage(void);

int
main(int argc, char *argv[])
{
	INPUT *F1, *F2;
	int aflag, ch, cval, vflag;
	char *end;

	setlocale(LC_ALL, "");

	F1 = &input1;
	F2 = &input2;

	aflag = vflag = 0;
	obsolete(argv);
	while ((ch = getopt(argc, argv, "\01a:e:j:1:2:o:t:v:")) != -1) {
		switch (ch) {
		case '\01':		/* See comment in obsolete(). */
			aflag = 1;
			F1->unpair = F2->unpair = 1;
			break;
		case '1':
			if ((F1->joinf = strtol(optarg, &end, 10)) < 1)
				errx(1, "-1 option field number less than 1");
			if (*end)
				errx(1, "illegal field number -- %s", optarg);
			--F1->joinf;
			break;
		case '2':
			if ((F2->joinf = strtol(optarg, &end, 10)) < 1)
				errx(1, "-2 option field number less than 1");
			if (*end)
				errx(1, "illegal field number -- %s", optarg);
			--F2->joinf;
			break;
		case 'a':
			aflag = 1;
			switch(strtol(optarg, &end, 10)) {
			case 1:
				F1->unpair = 1;
				break;
			case 2:
				F2->unpair = 1;
				break;
			default:
				errx(1, "-a option file number not 1 or 2");
				break;
			}
			if (*end)
				errx(1, "illegal file number -- %s", optarg);
			break;
		case 'e':
			empty = optarg;
			break;
		case 'j':
			if ((F1->joinf = F2->joinf =
			    strtol(optarg, &end, 10)) < 1)
				errx(1, "-j option field number less than 1");
			if (*end)
				errx(1, "illegal field number -- %s", optarg);
			--F1->joinf;
			--F2->joinf;
			break;
		case 'o':
			fieldarg(optarg);
			break;
		case 't':
			spans = 0;
			if (mbrtowc(&tabchar[0], optarg, MB_LEN_MAX, NULL) !=
			    strlen(optarg))
				errx(1, "illegal tab character specification");
			tabchar[1] = L'\0';
			break;
		case 'v':
			vflag = 1;
			joinout = 0;
			switch (strtol(optarg, &end, 10)) {
			case 1:
				F1->unpair = 1;
				break;
			case 2:
				F2->unpair = 1;
				break;
			default:
				errx(1, "-v option file number not 1 or 2");
				break;
			}
			if (*end)
				errx(1, "illegal file number -- %s", optarg);
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (aflag && vflag)
		errx(1, "the -a and -v options are mutually exclusive");

	if (argc != 2)
		usage();

	/* Open the files; "-" means stdin. */
	if (!strcmp(*argv, "-"))
		F1->fp = stdin;
	else if ((F1->fp = fopen(*argv, "r")) == NULL)
		err(1, "%s", *argv);
	++argv;
	if (!strcmp(*argv, "-"))
		F2->fp = stdin;
	else if ((F2->fp = fopen(*argv, "r")) == NULL)
		err(1, "%s", *argv);
	if (F1->fp == stdin && F2->fp == stdin)
		errx(1, "only one input file may be stdin");

	slurp(F1);
	slurp(F2);
	while (F1->setcnt && F2->setcnt) {
		cval = cmp(F1->set, F1->joinf, F2->set, F2->joinf);
		if (cval == 0) {
			/* Oh joy, oh rapture, oh beauty divine! */
			if (joinout)
				joinlines(F1, F2);
			slurp(F1);
			slurp(F2);
		} else if (cval < 0) {
			/* File 1 takes the lead... */
			if (F1->unpair)
				joinlines(F1, NULL);
			slurp(F1);
		} else {
			/* File 2 takes the lead... */
			if (F2->unpair)
				joinlines(F2, NULL);
			slurp(F2);
		}
	}

	/*
	 * Now that one of the files is used up, optionally output any
	 * remaining lines from the other file.
	 */
	if (F1->unpair)
		while (F1->setcnt) {
			joinlines(F1, NULL);
			slurp(F1);
		}
	if (F2->unpair)
		while (F2->setcnt) {
			joinlines(F2, NULL);
			slurp(F2);
		}
	exit(0);
}

static void
slurp(INPUT *F)
{
	LINE *lp, *lastlp, tmp;
	size_t len;
	int cnt;
	char *bp, *fieldp;

	/*
	 * Read all of the lines from an input file that have the same
	 * join field.
	 */
	F->setcnt = 0;
	for (lastlp = NULL;; ++F->setcnt) {
		/*
		 * If we're out of space to hold line structures, allocate
		 * more.  Initialize the structure so that we know that this
		 * is new space.
		 */
		if (F->setcnt == F->setalloc) {
			cnt = F->setalloc;
			F->setalloc += 50;
			if ((F->set = realloc(F->set,
			    F->setalloc * sizeof(LINE))) == NULL)
				err(1, NULL);
			memset(F->set + cnt, 0, 50 * sizeof(LINE));

			/* re-set lastlp in case it moved */
			if (lastlp != NULL)
				lastlp = &F->set[F->setcnt - 1];
		}

		/*
		 * Get any pushed back line, else get the next line.  Allocate
		 * space as necessary.  If taking the line from the stack swap
		 * the two structures so that we don't lose space allocated to
		 * either structure.  This could be avoided by doing another
		 * level of indirection, but it's probably okay as is.
		 */
		lp = &F->set[F->setcnt];
		if (F->setcnt)
			lastlp = &F->set[F->setcnt - 1];
		if (F->pushbool) {
			tmp = F->set[F->setcnt];
			F->set[F->setcnt] = F->set[F->pushback];
			F->set[F->pushback] = tmp;
			F->pushbool = 0;
			continue;
		}
		if ((bp = fgetln(F->fp, &len)) == NULL)
			return;
		if (lp->linealloc <= len + 1) {
			lp->linealloc += MAX(100, len + 1 - lp->linealloc);
			if ((lp->line =
			    realloc(lp->line, lp->linealloc)) == NULL)
				err(1, NULL);
		}
		memmove(lp->line, bp, len);

		/* Replace trailing newline, if it exists. */
		if (bp[len - 1] == '\n')
			lp->line[len - 1] = '\0';
		else
			lp->line[len] = '\0';
		bp = lp->line;

		/* Split the line into fields, allocate space as necessary. */
		lp->fieldcnt = 0;
		while ((fieldp = mbssep(&bp, tabchar)) != NULL) {
			if (spans && *fieldp == '\0')
				continue;
			if (lp->fieldcnt == lp->fieldalloc) {
				lp->fieldalloc += 50;
				if ((lp->fields = realloc(lp->fields,
				    lp->fieldalloc * sizeof(char *))) == NULL)
					err(1, NULL);
			}
			lp->fields[lp->fieldcnt++] = fieldp;
		}

		/* See if the join field value has changed. */
		if (lastlp != NULL && cmp(lp, F->joinf, lastlp, F->joinf)) {
			F->pushbool = 1;
			F->pushback = F->setcnt;
			break;
		}
	}
}

static char *
mbssep(char **stringp, const wchar_t *delim)
{
	char *s, *tok;
	const wchar_t *spanp;
	wchar_t c, sc;
	size_t n;

	if ((s = *stringp) == NULL)
		return (NULL);
	for (tok = s;;) {
		n = mbrtowc(&c, s, MB_LEN_MAX, NULL);
		if (n == (size_t)-1 || n == (size_t)-2)
			errc(1, EILSEQ, NULL);	/* XXX */
		s += n;
		spanp = delim;
		do {
			if ((sc = *spanp++) == c) {
				if (c == 0)
					s = NULL;
				else
					s[-n] = '\0';
				*stringp = s;
				return (tok);
			}
		} while (sc != 0);
	}
}

static int
cmp(LINE *lp1, u_long fieldno1, LINE *lp2, u_long fieldno2)
{
	if (lp1->fieldcnt <= fieldno1)
		return (lp2->fieldcnt <= fieldno2 ? 0 : 1);
	if (lp2->fieldcnt <= fieldno2)
		return (-1);
	return (mbscoll(lp1->fields[fieldno1], lp2->fields[fieldno2]));
}

static int
mbscoll(const char *s1, const char *s2)
{
	wchar_t *w1, *w2;
	int ret;

	if (MB_CUR_MAX == 1)
		return (strcoll(s1, s2));
	if ((w1 = towcs(s1)) == NULL || (w2 = towcs(s2)) == NULL)
		err(1, NULL);	/* XXX */
	ret = wcscoll(w1, w2);
	free(w1);
	free(w2);
	return (ret);
}

static wchar_t *
towcs(const char *s)
{
	wchar_t *wcs;
	size_t n;

	if ((n = mbsrtowcs(NULL, &s, 0, NULL)) == (size_t)-1)
		return (NULL);
	if ((wcs = malloc((n + 1) * sizeof(*wcs))) == NULL)
		return (NULL);
	mbsrtowcs(wcs, &s, n + 1, NULL);
	return (wcs);
}

static void
joinlines(INPUT *F1, INPUT *F2)
{
	u_long cnt1, cnt2;

	/*
	 * Output the results of a join comparison.  The output may be from
	 * either file 1 or file 2 (in which case the first argument is the
	 * file from which to output) or from both.
	 */
	if (F2 == NULL) {
		for (cnt1 = 0; cnt1 < F1->setcnt; ++cnt1)
			outoneline(F1, &F1->set[cnt1]);
		return;
	}
	for (cnt1 = 0; cnt1 < F1->setcnt; ++cnt1)
		for (cnt2 = 0; cnt2 < F2->setcnt; ++cnt2)
			outtwoline(F1, &F1->set[cnt1], F2, &F2->set[cnt2]);
}

static void
outoneline(INPUT *F, LINE *lp)
{
	u_long cnt;

	/*
	 * Output a single line from one of the files, according to the
	 * join rules.  This happens when we are writing unmatched single
	 * lines.  Output empty fields in the right places.
	 */
	if (olist)
		for (cnt = 0; cnt < olistcnt; ++cnt) {
			if (olist[cnt].filenum == (unsigned)F->number)
				outfield(lp, olist[cnt].fieldno, 0);
			else if (olist[cnt].filenum == 0)
				outfield(lp, F->joinf, 0);
			else
				outfield(lp, 0, 1);
		}
	else {
		/*
		 * Output the join field, then the remaining fields.
		 */
		outfield(lp, F->joinf, 0);
		for (cnt = 0; cnt < lp->fieldcnt; ++cnt)
			if (F->joinf != cnt)
				outfield(lp, cnt, 0);
	}
	(void)printf("\n");
	if (ferror(stdout))
		err(1, "stdout");
	needsep = 0;
}

static void
outtwoline(INPUT *F1, LINE *lp1, INPUT *F2, LINE *lp2)
{
	u_long cnt;

	/* Output a pair of lines according to the join list (if any). */
	if (olist)
		for (cnt = 0; cnt < olistcnt; ++cnt)
			if (olist[cnt].filenum == 0) {
				if (lp1->fieldcnt >= F1->joinf)
					outfield(lp1, F1->joinf, 0);
				else
					outfield(lp2, F2->joinf, 0);
			} else if (olist[cnt].filenum == 1)
				outfield(lp1, olist[cnt].fieldno, 0);
			else /* if (olist[cnt].filenum == 2) */
				outfield(lp2, olist[cnt].fieldno, 0);
	else {
		/*
		 * Output the join field, then the remaining fields from F1
		 * and F2.
		 */
		outfield(lp1, F1->joinf, 0);
		for (cnt = 0; cnt < lp1->fieldcnt; ++cnt)
			if (F1->joinf != cnt)
				outfield(lp1, cnt, 0);
		for (cnt = 0; cnt < lp2->fieldcnt; ++cnt)
			if (F2->joinf != cnt)
				outfield(lp2, cnt, 0);
	}
	(void)printf("\n");
	if (ferror(stdout))
		err(1, "stdout");
	needsep = 0;
}

static void
outfield(LINE *lp, u_long fieldno, int out_empty)
{
	if (needsep++)
		(void)printf("%lc", (wint_t)*tabchar);
	if (!ferror(stdout)) {
		if (lp->fieldcnt <= fieldno || out_empty) {
			if (empty != NULL)
				(void)printf("%s", empty);
		} else {
			if (*lp->fields[fieldno] == '\0')
				return;
			(void)printf("%s", lp->fields[fieldno]);
		}
	}
	if (ferror(stdout))
		err(1, "stdout");
}

/*
 * Convert an output list argument "2.1, 1.3, 2.4" into an array of output
 * fields.
 */
static void
fieldarg(char *option)
{
	u_long fieldno, filenum;
	char *end, *token;

	while ((token = strsep(&option, ", \t")) != NULL) {
		if (*token == '\0')
			continue;
		if (token[0] == '0')
			filenum = fieldno = 0;
		else if ((token[0] == '1' || token[0] == '2') &&
		    token[1] == '.') {
			filenum = token[0] - '0';
			fieldno = strtol(token + 2, &end, 10);
			if (*end)
				errx(1, "malformed -o option field");
			if (fieldno == 0)
				errx(1, "field numbers are 1 based");
			--fieldno;
		} else
			errx(1, "malformed -o option field");
		if (olistcnt == olistalloc) {
			olistalloc += 50;
			if ((olist = realloc(olist,
			    olistalloc * sizeof(OLIST))) == NULL)
				err(1, NULL);
		}
		olist[olistcnt].filenum = filenum;
		olist[olistcnt].fieldno = fieldno;
		++olistcnt;
	}
}

static void
obsolete(char **argv)
{
	size_t len;
	char **p, *ap, *t;

	while ((ap = *++argv) != NULL) {
		/* Return if "--". */
		if (ap[0] == '-' && ap[1] == '-')
			return;
		/* skip if not an option */
		if (ap[0] != '-')
			continue;
		switch (ap[1]) {
		case 'a':
			/*
			 * The original join allowed "-a", which meant the
			 * same as -a1 plus -a2.  POSIX 1003.2, Draft 11.2
			 * only specifies this as "-a 1" and "a -2", so we
			 * have to use another option flag, one that is
			 * unlikely to ever be used or accidentally entered
			 * on the command line.  (Well, we could reallocate
			 * the argv array, but that hardly seems worthwhile.)
			 */
			if (ap[2] == '\0' && (argv[1] == NULL ||
			    (strcmp(argv[1], "1") != 0 &&
			    strcmp(argv[1], "2") != 0))) {
				ap[1] = '\01';
				warnx("-a option used without an argument; "
				    "reverting to historical behavior");
			}
			break;
		case 'j':
			/*
			 * The original join allowed "-j[12] arg" and "-j arg".
			 * Convert the former to "-[12] arg".  Don't convert
			 * the latter since getopt(3) can handle it.
			 */
			switch(ap[2]) {
			case '1':
				if (ap[3] != '\0')
					goto jbad;
				ap[1] = '1';
				ap[2] = '\0';
				break;
			case '2':
				if (ap[3] != '\0')
					goto jbad;
				ap[1] = '2';
				ap[2] = '\0';
				break;
			case '\0':
				break;
			default:
jbad:				errx(1, "illegal option -- %s", ap);
				usage();
			}
			break;
		case 'o':
			/*
			 * The original join allowed "-o arg arg".
			 * Convert to "-o arg -o arg".
			 */
			if (ap[2] != '\0')
				break;
			for (p = argv + 2; *p; ++p) {
				if (p[0][0] == '0' || ((p[0][0] != '1' &&
				    p[0][0] != '2') || p[0][1] != '.'))
					break;
				len = strlen(*p);
				if (len - 2 != strspn(*p + 2, "0123456789"))
					break;
				if ((t = malloc(len + 3)) == NULL)
					err(1, NULL);
				t[0] = '-';
				t[1] = 'o';
				memmove(t + 2, *p, len + 1);
				*p = t;
			}
			argv = p - 1;
			break;
		}
	}
}

static void
usage(void)
{
	(void)fprintf(stderr, "%s %s\n%s\n",
	    "usage: join [-a fileno | -v fileno ] [-e string] [-1 field]",
	    "[-2 field]",
		"            [-o list] [-t char] file1 file2");
	exit(1);
}
