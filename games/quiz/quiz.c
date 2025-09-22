/*	$OpenBSD: quiz.c,v 1.32 2022/08/08 17:54:08 op Exp $	*/
/*	$NetBSD: quiz.c,v 1.9 1995/04/22 10:16:58 cgd Exp $	*/

/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jim R. Oldroyd at The Instruction Set and Keith Gabryelski at
 * Commodore Business Machines.
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

#include <ctype.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pathnames.h"
#include "quiz.h"

static QE qlist;
static int catone, cattwo, tflag;
static u_int qsize;

void	 downcase(char *);
void	 get_cats(char *, char *);
void	 get_file(const char *);
const char	*next_cat(const char *);
void	 quiz(void);
void	 score(u_int, u_int, u_int);
void	 show_index(void);
__dead void	usage(void);

int
main(int argc, char *argv[])
{
	int ch;
	const char *indexfile;

	if (pledge("stdio rpath proc exec", NULL) == -1)
		err(1, "pledge");

	indexfile = _PATH_QUIZIDX;
	while ((ch = getopt(argc, argv, "hi:t")) != -1)
		switch(ch) {
		case 'i':
			indexfile = optarg;
			break;
		case 't':
			tflag = 1;
			break;
		case 'h':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	switch(argc) {
	case 0:
		get_file(indexfile);
		show_index();
		break;
	case 2:
		if (pledge("stdio rpath", NULL) == -1)
			err(1, "pledge");
		get_file(indexfile);
		get_cats(argv[0], argv[1]);

		if (pledge("stdio", NULL) == -1)
			err(1, "pledge");

		quiz();
		break;
	default:
		usage();
	}
	return 0;
}

void
get_file(const char *file)
{
	FILE *fp;
	QE *qp;
	ssize_t len;
	size_t qlen, size;
	char *lp;

	if ((fp = fopen(file, "r")) == NULL)
		err(1, "%s", file);

	/*
	 * XXX
	 * Should really free up space from any earlier read list
	 * but there are no reverse pointers to do so with.
	 */
	qp = &qlist;
	qsize = 0;
	qlen = 0;
	lp = NULL;
	size = 0;
	while ((len = getline(&lp, &size, fp)) != -1) {
		if (lp[len - 1] == '\n')
			lp[--len] = '\0';
		if (qp->q_text)
			qlen = strlen(qp->q_text);
		if (qlen > 0 && qp->q_text[qlen - 1] == '\\') {
			qp->q_text[--qlen] = '\0';
			qlen += len;
			qp->q_text = realloc(qp->q_text, qlen + 1);
			if (qp->q_text == NULL)
				errx(1, "realloc");
			strlcat(qp->q_text, lp, qlen + 1);
		} else {
			if ((qp->q_next = malloc(sizeof(QE))) == NULL)
				errx(1, "malloc");
			qp = qp->q_next;
			qp->q_text = strdup(lp);
			if (qp->q_text == NULL)
				errx(1, "strdup");
			qp->q_asked = qp->q_answered = FALSE;
			qp->q_next = NULL;
			++qsize;
		}
	}
	free(lp);
	if (ferror(fp))
		err(1, "getline");
	(void)fclose(fp);
}

void
show_index(void)
{
	QE *qp;
	const char *p, *s;
	FILE *pf;
	const char *pager;

	if (!isatty(1))
		pager = "/bin/cat";
	else if (!(pager = getenv("PAGER")) || (*pager == 0))
			pager = _PATH_PAGER;
	if ((pf = popen(pager, "w")) == NULL)
		err(1, "%s", pager);
	(void)fprintf(pf, "Subjects:\n\n");
	for (qp = qlist.q_next; qp; qp = qp->q_next) {
		for (s = next_cat(qp->q_text); s; s = next_cat(s)) {
			if (!rxp_compile(s))
				errx(1, "%s", rxperr);
			if ((p = rxp_expand()))
				(void)fprintf(pf, "%s ", p);
		}
		(void)fprintf(pf, "\n");
	}
	(void)fprintf(pf, "\n%s\n%s\n%s\n",
"For example, \"quiz victim killer\" prints a victim's name and you reply",
"with the killer, and \"quiz killer victim\" works the other way around.",
"Type an empty line to get the correct answer.");
	(void)pclose(pf);
}

void
get_cats(char *cat1, char *cat2)
{
	QE *qp;
	int i;
	const char *s;

	downcase(cat1);
	downcase(cat2);
	for (qp = qlist.q_next; qp; qp = qp->q_next) {
		s = next_cat(qp->q_text);
		catone = cattwo = i = 0;
		while (s) {
			if (!rxp_compile(s))
				errx(1, "%s", rxperr);
			i++;
			if (rxp_match(cat1))
				catone = i;
			if (rxp_match(cat2))
				cattwo = i;
			s = next_cat(s);
		}
		if (catone && cattwo && catone != cattwo) {
			if (!rxp_compile(qp->q_text))
				errx(1, "%s", rxperr);
			get_file(rxp_expand());
			return;
		}
	}
	errx(1, "invalid categories");
}

void
quiz(void)
{
	QE *qp;
	int i;
	size_t size;
	ssize_t len;
	u_int guesses, rights, wrongs;
	int next;
	char *answer, *t, question[LINE_SZ];
	const char *s;

	size = 0;
	answer = NULL;

	guesses = rights = wrongs = 0;
	for (;;) {
		if (qsize == 0)
			break;
		next = arc4random_uniform(qsize);
		qp = qlist.q_next;
		for (i = 0; i < next; i++)
			qp = qp->q_next;
		while (qp && qp->q_answered)
			qp = qp->q_next;
		if (!qp) {
			qsize = next;
			continue;
		}
		if (tflag && arc4random_uniform(100) > 20) {
			/* repeat questions in tutorial mode */
			while (qp && (!qp->q_asked || qp->q_answered))
				qp = qp->q_next;
			if (!qp)
				continue;
		}
		s = qp->q_text;
		for (i = 0; i < catone - 1; i++)
			s = next_cat(s);
		if (!rxp_compile(s))
			errx(1, "%s", rxperr);
		t = rxp_expand();
		if (!t || *t == '\0') {
			qp->q_answered = TRUE;
			continue;
		}
		(void)strlcpy(question, t, sizeof question);
		s = qp->q_text;
		for (i = 0; i < cattwo - 1; i++)
			s = next_cat(s);
		if (s == NULL)
			errx(1, "too few fields in data file, line \"%s\"",
			    qp->q_text);
		if (!rxp_compile(s))
			errx(1, "%s", rxperr);
		t = rxp_expand();
		if (!t || *t == '\0') {
			qp->q_answered = TRUE;
			continue;
		}
		qp->q_asked = TRUE;
		(void)printf("%s?\n", question);
		for (;; ++guesses) {
			if ((len = getline(&answer, &size, stdin)) == -1 ||
			    answer[len - 1] != '\n') {
				score(rights, wrongs, guesses);
				exit(0);
			}
			answer[len - 1] = '\0';
			downcase(answer);
			if (rxp_match(answer)) {
				(void)printf("Right!\n");
				++rights;
				qp->q_answered = TRUE;
				break;
			}
			if (*answer == '\0') {
				(void)printf("%s\n", t);
				++wrongs;
				if (!tflag)
					qp->q_answered = TRUE;
				break;
			}
			(void)printf("What?\n");
		}
	}
	score(rights, wrongs, guesses);
	free(answer);
}

const char *
next_cat(const char *s)
{
	int esc;

	if (s == NULL)
		return (NULL);
	esc = 0;
	for (;;)
		switch (*s++) {
		case '\0':
			return (NULL);
		case '\\':
			esc = 1;
			break;
		case ':':
			if (!esc)
				return (s);
		default:
			esc = 0;
			break;
		}
}

void
score(u_int r, u_int w, u_int g)
{
	(void)printf("Rights %d, wrongs %d,", r, w);
	if (g)
		(void)printf(" extra guesses %d,", g);
	(void)printf(" score %d%%\n", (r + w + g) ? r * 100 / (r + w + g) : 0);
}

void
downcase(char *p)
{
	int ch;

	for (; (ch = *p) != '\0'; ++p)
		if (isascii(ch) && isupper(ch))
			*p = tolower(ch);
}

void
usage(void)
{
	(void)fprintf(stderr,
	    "usage: %s [-t] [-i file] category1 category2\n", getprogname());
	exit(1);
}
