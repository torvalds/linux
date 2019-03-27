/*	$OpenBSD: diff3prog.c,v 1.11 2009/10/27 23:59:37 deraadt Exp $	*/

/*
 * Copyright (C) Caldera International Inc.  2001-2002.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code and documentation must retain the above
 *    copyright notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed or owned by Caldera
 *	International, Inc.
 * 4. Neither the name of Caldera International, Inc. nor the names of other
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * USE OF THE SOFTWARE PROVIDED FOR UNDER THIS LICENSE BY CALDERA
 * INTERNATIONAL, INC. AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL CALDERA INTERNATIONAL, INC. BE LIABLE FOR ANY DIRECT,
 * INDIRECT INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)diff3.c	8.1 (Berkeley) 6/6/93
 */

#if 0
#ifndef lint
static char sccsid[] = "@(#)diff3.c	8.1 (Berkeley) 6/6/93";
#endif
#endif /* not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/capsicum.h>
#include <sys/procdesc.h>
#include <sys/types.h>
#include <sys/event.h>
#include <sys/wait.h>

#include <capsicum_helpers.h>
#include <ctype.h>
#include <err.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


/*
 * "from" is first in range of changed lines; "to" is last+1
 * from=to=line after point of insertion for added lines.
 */
struct range {
	int from;
	int to;
};

struct diff {
	struct range old;
	struct range new;
};

static size_t szchanges;

static struct diff *d13;
static struct diff *d23;
/*
 * "de" is used to gather editing scripts.  These are later spewed out in
 * reverse order.  Its first element must be all zero, the "new" component
 * of "de" contains line positions or byte positions depending on when you
 * look (!?).  Array overlap indicates which sections in "de" correspond to
 * lines that are different in all three files.
 */
static struct diff *de;
static char *overlap;
static int  overlapcnt;
static FILE *fp[3];
static int cline[3];		/* # of the last-read line in each file (0-2) */
/*
 * The latest known correspondence between line numbers of the 3 files
 * is stored in last[1-3];
 */
static int last[4];
static int Aflag, eflag, iflag, mflag, Tflag;
static int oflag;		/* indicates whether to mark overlaps (-E or -X)*/
static int strip_cr;
static char *f1mark, *f2mark, *f3mark;

static bool duplicate(struct range *, struct range *);
static int edit(struct diff *, bool, int);
static char *getchange(FILE *);
static char *get_line(FILE *, size_t *);
static int number(char **);
static int readin(int fd, struct diff **);
static int skip(int, int, const char *);
static void change(int, struct range *, bool);
static void keep(int, struct range *);
static void merge(int, int);
static void prange(struct range *);
static void repos(int);
static void edscript(int) __dead2;
static void increase(void);
static void usage(void) __dead2;

enum {
	DIFFPROG_OPT,
	STRIPCR_OPT,
};

#define DIFF_PATH "/usr/bin/diff"

#define OPTIONS "3aAeEiL:mTxX"
static struct option longopts[] = {
	{ "ed",			no_argument,		NULL,	'e' },
	{ "show-overlap",	no_argument,		NULL,	'E' },
	{ "overlap-only",	no_argument,		NULL,	'x' },
	{ "initial-tab",	no_argument,		NULL,	'T' },
	{ "text",		no_argument,		NULL,	'a' },
	{ "strip-trailing-cr",	no_argument,		NULL,	STRIPCR_OPT },
	{ "show-all",		no_argument,		NULL,	'A' },
	{ "easy-only",		no_argument,		NULL,	'3' },
	{ "merge",		no_argument,		NULL,	'm' },
	{ "label",		required_argument,	NULL,	'L' },
	{ "diff-program",	required_argument,	NULL,	DIFFPROG_OPT },
};

static void
usage(void)
{
	fprintf(stderr, "usage: diff3 [-3aAeEimTxX] [-L lable1] [-L label2] "
	    "[ -L label3] file1 file2 file3\n");
	exit (2);
}

static int
readin(int fd, struct diff **dd)
{
	int a, b, c, d;
	size_t i;
	char kind, *p;
	FILE *f;

	f = fdopen(fd, "r");
	if (f == NULL)
		err(2, "fdopen");
	for (i=0; (p = getchange(f)); i++) {
		if (i >= szchanges - 1)
			increase();
		a = b = number(&p);
		if (*p == ',') {
			p++;
			b = number(&p);
		}
		kind = *p++;
		c = d = number(&p);
		if (*p==',') {
			p++;
			d = number(&p);
		}
		if (kind == 'a')
			a++;
		if (kind == 'd')
			c++;
		b++;
		d++;
		(*dd)[i].old.from = a;
		(*dd)[i].old.to = b;
		(*dd)[i].new.from = c;
		(*dd)[i].new.to = d;
	}
	if (i) {
		(*dd)[i].old.from = (*dd)[i-1].old.to;
		(*dd)[i].new.from = (*dd)[i-1].new.to;
	}
	fclose(f);
	return (i);
}

static int
diffexec(const char *diffprog, char **diffargv, int fd[])
{
	int pid, pd;

	switch (pid = pdfork(&pd, PD_CLOEXEC)) {
	case 0:
		close(fd[0]);
		if (dup2(fd[1], STDOUT_FILENO) == -1)
			err(2, "child could not duplicate descriptor");
		close(fd[1]);
		execvp(diffprog, diffargv);
		err(2, "could not execute diff: %s", diffprog);
		break;
	case -1:
		err(2, "could not fork");
		break;
	}
	close(fd[1]);
	return (pd);
}

static int
number(char **lc)
{
	int nn;

	nn = 0;
	while (isdigit((unsigned char)(**lc)))
		nn = nn*10 + *(*lc)++ - '0';
	return (nn);
}

static char *
getchange(FILE *b)
{
	char *line;

	while ((line = get_line(b, NULL))) {
		if (isdigit((unsigned char)line[0]))
			return (line);
	}
	return (NULL);
}


static char *
get_line(FILE *b, size_t *n)
{
	char *cp;
	size_t len;
	static char *buf;
	static size_t bufsize;

	if ((cp = fgetln(b, &len)) == NULL)
		return (NULL);

	if (cp[len - 1] != '\n')
		len++;
	if (len + 1 > bufsize) {
		do {
			bufsize += 1024;
		} while (len + 1 > bufsize);
		if ((buf = realloc(buf, bufsize)) == NULL)
			err(EXIT_FAILURE, NULL);
	}
	memcpy(buf, cp, len - 1);
	buf[len - 1] = '\n';
	buf[len] = '\0';
	if (n != NULL)
		*n = len;
	return (buf);
}

static void
merge(int m1, int m2)
{
	struct diff *d1, *d2, *d3;
	int j, t1, t2;
	bool dup = false;

	d1 = d13;
	d2 = d23;
	j = 0;

	while ((t1 = d1 < d13 + m1) | (t2 = d2 < d23 + m2)) {
		/* first file is different from the others */
		if (!t2 || (t1 && d1->new.to < d2->new.from)) {
			/* stuff peculiar to 1st file */
			if (eflag == 0) {
				printf("====1\n");
				change(1, &d1->old, false);
				keep(2, &d1->new);
				change(3, &d1->new, false);
			}
			d1++;
			continue;
		}
		/* second file is different from others */
		if (!t1 || (t2 && d2->new.to < d1->new.from)) {
			if (eflag == 0) {
				printf("====2\n");
				keep(1, &d2->new);
				change(3, &d2->new, false);
				change(2, &d2->old, false);
			}
			d2++;
			continue;
		}
		/*
		 * Merge overlapping changes in first file
		 * this happens after extension (see below).
		 */
		if (d1 + 1 < d13 + m1 && d1->new.to >= d1[1].new.from) {
			d1[1].old.from = d1->old.from;
			d1[1].new.from = d1->new.from;
			d1++;
			continue;
		}

		/* merge overlapping changes in second */
		if (d2 + 1 < d23 + m2 && d2->new.to >= d2[1].new.from) {
			d2[1].old.from = d2->old.from;
			d2[1].new.from = d2->new.from;
			d2++;
			continue;
		}
		/* stuff peculiar to third file or different in all */
		if (d1->new.from == d2->new.from && d1->new.to == d2->new.to) {
			dup = duplicate(&d1->old, &d2->old);
			/*
			 * dup = 0 means all files differ
			 * dup = 1 means files 1 and 2 identical
			 */
			if (eflag == 0) {
				printf("====%s\n", dup ? "3" : "");
				change(1, &d1->old, dup);
				change(2, &d2->old, false);
				d3 = d1->old.to > d1->old.from ? d1 : d2;
				change(3, &d3->new, false);
			} else
				j = edit(d1, dup, j);
			d1++;
			d2++;
			continue;
		}
		/*
		 * Overlapping changes from file 1 and 2; extend changes
		 * appropriately to make them coincide.
		 */
		if (d1->new.from < d2->new.from) {
			d2->old.from -= d2->new.from - d1->new.from;
			d2->new.from = d1->new.from;
		} else if (d2->new.from < d1->new.from) {
			d1->old.from -= d1->new.from - d2->new.from;
			d1->new.from = d2->new.from;
		}
		if (d1->new.to > d2->new.to) {
			d2->old.to += d1->new.to - d2->new.to;
			d2->new.to = d1->new.to;
		} else if (d2->new.to > d1->new.to) {
			d1->old.to += d2->new.to - d1->new.to;
			d1->new.to = d2->new.to;
		}
	}
	if (eflag)
		edscript(j);
}

/*
 * The range of lines rold.from thru rold.to in file i is to be changed.
 * It is to be printed only if it does not duplicate something to be
 * printed later.
 */
static void
change(int i, struct range *rold, bool dup)
{

	printf("%d:", i);
	last[i] = rold->to;
	prange(rold);
	if (dup)
		return;
	i--;
	skip(i, rold->from, NULL);
	skip(i, rold->to, "  ");
}

/*
 * Print the range of line numbers, rold.from thru rold.to, as n1,n2 or 
 * n1.
 */
static void
prange(struct range *rold)
{

	if (rold->to <= rold->from)
		printf("%da\n", rold->from - 1);
	else {
		printf("%d", rold->from);
		if (rold->to > rold->from+1)
			printf(",%d", rold->to - 1);
		printf("c\n");
	}
}

/*
 * No difference was reported by diff between file 1 (or 2) and file 3,
 * and an artificial dummy difference (trange) must be ginned up to
 * correspond to the change reported in the other file.
 */
static void
keep(int i, struct range *rnew)
{
	int delta;
	struct range trange;

	delta = last[3] - last[i];
	trange.from = rnew->from - delta;
	trange.to = rnew->to - delta;
	change(i, &trange, true);
}

/*
 * skip to just before line number from in file "i".  If "pr" is non-NULL,
 * print all skipped stuff with string pr as a prefix.
 */
static int
skip(int i, int from, const char *pr)
{
	size_t j, n;
	char *line;

	for (n = 0; cline[i] < from - 1; n += j) {
		if ((line = get_line(fp[i], &j)) == NULL)
			errx(EXIT_FAILURE, "logic error");
		if (pr != NULL)
			printf("%s%s", Tflag == 1? "\t" : pr, line);
		cline[i]++;
	}
	return ((int) n);
}

/*
 * Return 1 or 0 according as the old range (in file 1) contains exactly
 * the same data as the new range (in file 2).
 */
static bool
duplicate(struct range *r1, struct range *r2)
{
	int c, d;
	int nchar;
	int nline;

	if (r1->to-r1->from != r2->to-r2->from)
		return (0);
	skip(0, r1->from, NULL);
	skip(1, r2->from, NULL);
	nchar = 0;
	for (nline=0; nline < r1->to - r1->from; nline++) {
		do {
			c = getc(fp[0]);
			d = getc(fp[1]);
			if (c == -1 || d== -1)
				errx(EXIT_FAILURE, "logic error");
			nchar++;
			if (c != d) {
				repos(nchar);
				return (0);
			}
		} while (c != '\n');
	}
	repos(nchar);
	return (1);
}

static void
repos(int nchar)
{
	int i;

	for (i = 0; i < 2; i++)
		(void)fseek(fp[i], (long)-nchar, SEEK_CUR);
}
/*
 * collect an editing script for later regurgitation
 */
static int
edit(struct diff *diff, bool dup, int j)
{

	if (((dup + 1) & eflag) == 0)
		return (j);
	j++;
	overlap[j] = !dup;
	if (!dup)
		overlapcnt++;
	de[j].old.from = diff->old.from;
	de[j].old.to = diff->old.to;
	de[j].new.from = de[j-1].new.to + skip(2, diff->new.from, NULL);
	de[j].new.to = de[j].new.from + skip(2, diff->new.to, NULL);
	return (j);
}

/* regurgitate */
static void
edscript(int n)
{
	int k;
	size_t j;
	char block[BUFSIZ];

	for (; n > 0; n--) {
		if (!oflag || !overlap[n]) {
			prange(&de[n].old);
		} else {
			printf("%da\n", de[n].old.to -1);
			if (Aflag) {
				printf("%s\n", f2mark);
				fseek(fp[1], de[n].old.from, SEEK_SET);
				for (k = de[n].old.to - de[n].old.from; k > 0; k -= j) {
					j = k > BUFSIZ ? BUFSIZ : k;
					if (fread(block, 1, j, fp[1]) != j)
						errx(2, "logic error");
					fwrite(block, 1, j, stdout);
				}
				printf("\n");
			}
			printf("=======\n");
		}
		fseek(fp[2], (long)de[n].new.from, SEEK_SET);
		for (k = de[n].new.to - de[n].new.from; k > 0; k-= j) {
			j = k > BUFSIZ ? BUFSIZ : k;
			if (fread(block, 1, j, fp[2]) != j)
				errx(2, "logic error");
			fwrite(block, 1, j, stdout);
		}
		if (!oflag || !overlap[n])
			printf(".\n");
		else {
			printf("%s\n.\n", f3mark);
			printf("%da\n%s\n.\n", de[n].old.from - 1, f1mark);
		}
	}
	if (iflag)
		printf("w\nq\n");

	exit(eflag == 0 ? overlapcnt : 0);
}

static void
increase(void)
{
	struct diff *p;
	char *q;
	size_t newsz, incr;

	/* are the memset(3) calls needed? */
	newsz = szchanges == 0 ? 64 : 2 * szchanges;
	incr = newsz - szchanges;

	p = realloc(d13, newsz * sizeof(struct diff));
	if (p == NULL)
		err(1, NULL);
	memset(p + szchanges, 0, incr * sizeof(struct diff));
	d13 = p;
	p = realloc(d23, newsz * sizeof(struct diff));
	if (p == NULL)
		err(1, NULL);
	memset(p + szchanges, 0, incr * sizeof(struct diff));
	d23 = p;
	p = realloc(de, newsz * sizeof(struct diff));
	if (p == NULL)
		err(1, NULL);
	memset(p + szchanges, 0, incr * sizeof(struct diff));
	de = p;
	q = realloc(overlap, newsz * sizeof(char));
	if (q == NULL)
		err(1, NULL);
	memset(q + szchanges, 0, incr * sizeof(char));
	overlap = q;
	szchanges = newsz;
}


int
main(int argc, char **argv)
{
	int ch, nblabels, status, m, n, kq, nke, nleft, i;
	char *labels[] = { NULL, NULL, NULL };
	const char *diffprog = DIFF_PATH;
	char *file1, *file2, *file3;
	char *diffargv[6];
	int diffargc = 0;
	int fd13[2], fd23[2];
	int pd13, pd23;
	cap_rights_t rights_ro;
	struct kevent *e;

	nblabels = 0;
	eflag = 0;
	oflag = 0;
	diffargv[diffargc++] = __DECONST(char *, diffprog);
	while ((ch = getopt_long(argc, argv, OPTIONS, longopts, NULL)) != -1) {
		switch (ch) {
		case '3':
			eflag = 2;
			break;
		case 'a':
			diffargv[diffargc++] = __DECONST(char *, "-a");
			break;
		case 'A':
			Aflag = 1;
			break;
		case 'e':
			eflag = 3;
			break;
		case 'E':
			eflag = 3;
			oflag = 1;
			break;
		case 'i':
			iflag = 1;
			break;
		case 'L':
			oflag = 1;
			if (nblabels >= 3)
				errx(2, "too many file label options");
			labels[nblabels++] = optarg;
			break;
		case 'm':
			Aflag = 1;
			oflag = 1;
			mflag = 1;
			break;
		case 'T':
			Tflag = 1;
			break;
		case 'x':
			eflag = 1;
			break;
		case 'X':
			oflag = 1;
			eflag = 1;
			break;
		case DIFFPROG_OPT:
			diffprog = optarg;
			break;
		case STRIPCR_OPT:
			strip_cr = 1;
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if (Aflag) {
		eflag = 3;
		oflag = 1;
	}

	if (argc != 3)
		usage();

	if (caph_limit_stdio() == -1)
		err(2, "unable to limit stdio");

	cap_rights_init(&rights_ro, CAP_READ, CAP_FSTAT, CAP_SEEK);

	kq = kqueue();
	if (kq == -1)
		err(2, "kqueue");

	e = malloc(2 * sizeof(struct kevent));
	if (e == NULL)
		err(2, "malloc");

	/* TODO stdio */
	file1 = argv[0];
	file2 = argv[1];
	file3 = argv[2];

	if (oflag) {
		asprintf(&f1mark, "<<<<<<< %s",
		    labels[0] != NULL ? labels[0] : file1);
		if (f1mark == NULL)
			err(2, "asprintf");
		asprintf(&f2mark, "||||||| %s",
		    labels[1] != NULL ? labels[1] : file2);
		if (f2mark == NULL)
			err(2, "asprintf");
		asprintf(&f3mark, ">>>>>>> %s",
		    labels[2] != NULL ? labels[2] : file3);
		if (f3mark == NULL)
			err(2, "asprintf");
	}
	fp[0] = fopen(file1, "r");
	if (fp[0] == NULL)
		err(2, "Can't open %s", file1);
	if (caph_rights_limit(fileno(fp[0]), &rights_ro) < 0)
		err(2, "unable to limit rights on: %s", file1);

	fp[1] = fopen(file2, "r");
	if (fp[1] == NULL)
		err(2, "Can't open %s", file2);
	if (caph_rights_limit(fileno(fp[1]), &rights_ro) < 0)
		err(2, "unable to limit rights on: %s", file2);

	fp[2] = fopen(file3, "r");
	if (fp[2] == NULL)
		err(2, "Can't open %s", file3);
	if (caph_rights_limit(fileno(fp[2]), &rights_ro) < 0)
		err(2, "unable to limit rights on: %s", file3);

	if (pipe(fd13))
		err(2, "pipe");
	if (pipe(fd23))
		err(2, "pipe");

	diffargv[diffargc] = file1;
	diffargv[diffargc + 1] = file3;
	diffargv[diffargc + 2] = NULL;

	nleft = 0;
	pd13 = diffexec(diffprog, diffargv, fd13);
	EV_SET(e + nleft , pd13, EVFILT_PROCDESC, EV_ADD, NOTE_EXIT, 0, NULL);
	if (kevent(kq, e + nleft, 1, NULL, 0, NULL) == -1)
		err(2, "kevent1");
	nleft++;

	diffargv[diffargc] = file2;
	pd23 = diffexec(diffprog, diffargv, fd23);
	EV_SET(e + nleft , pd23, EVFILT_PROCDESC, EV_ADD, NOTE_EXIT, 0, NULL);
	if (kevent(kq, e + nleft, 1, NULL, 0, NULL) == -1)
		err(2, "kevent2");
	nleft++;

	caph_cache_catpages();
	if (caph_enter() < 0)
		err(2, "unable to enter capability mode");

	/* parse diffs */
	increase();
	m = readin(fd13[0], &d13);
	n = readin(fd23[0], &d23);

	/* waitpid cooked over pdforks */
	while (nleft > 0) {
		nke = kevent(kq, NULL, 0, e, nleft, NULL);
		if (nke == -1)
			err(2, "kevent");
		for (i = 0; i < nke; i++) {
			status = e[i].data;
			if (WIFEXITED(status) && WEXITSTATUS(status) >= 2)
				errx(2, "diff exited abormally");
			else if (WIFSIGNALED(status))
				errx(2, "diff killed by signal %d",
				    WTERMSIG(status));
		}
		nleft -= nke;
	}
	merge(m, n);

	return (EXIT_SUCCESS);
}
