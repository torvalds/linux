/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Tim J. Robbins.
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

/*
 * tabs -- set terminal tabs
 *
 * This utility displays a series of characters that clears the terminal
 * hardware tab settings, then initialises them to specified values,
 * and optionally sets a soft margin.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/tty.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <term.h>
#include <unistd.h>

/* Maximum number of tab stops allowed in table. */
#define NSTOPS		20

#define NELEMS(a) (sizeof(a) / sizeof(a[0]))

/* Predefined formats, taken from IEEE Std 1003.1-2001. */
static const struct {
	const char	*name;		/* Format name used on cmd. line */
	long		stops[NSTOPS];	/* Column positions */
} formats[] = {
	{ "a",	{ 1, 10, 16, 36, 72 } },
	{ "a2",	{ 1, 10, 16, 40, 72 } },
	{ "c",	{ 1, 8, 12, 16, 20, 55 } },
	{ "c2",	{ 1, 6, 10, 14, 49 } },
	{ "c3", { 1, 6, 10, 14, 18, 22, 26, 30, 34, 38, 42, 46, 50, 54, 58,
		62, 67 } },
	{ "f",	{ 1, 7, 11, 15, 19, 23 } },
	{ "p",	{ 1, 5, 9, 13, 17, 21, 25, 29, 33, 37, 41, 45, 49, 53, 57,
		61 } },
	{ "s",	{ 1, 10, 55 } },
	{ "u",	{ 1, 12, 20, 44 } }
};

static void	 gettabs(char *, long *, long *);
static int	 ttywidth(void);
static void	 usage(void);

int
main(int argc __unused, char *argv[])
{
	long cols, i, inc, j, margin, nstops, stops[NSTOPS];
	const char *cr, *ct, *st, *ML;
	char area[1024], *ap, *arg, *end;

	setlocale(LC_ALL, "");

	inc = 8;
	margin = 0;
	nstops = -1;
	while ((arg = *++argv) != NULL && (*arg == '-' || *arg == '+')) {
		if (*arg == '+') {
			/* +m[n] or +[n] */
			if (*++arg == 'm')
				arg++;
			if (*arg != '\0') {
				errno = 0;
				margin = strtol(arg, &end, 10);
				if (errno != 0 || *end != '\0' || margin < 0)
					errx(1, "%s: invalid margin width",
					    arg);
			} else
				margin = 10;
		} else if (isdigit(arg[1])) {
			/* -n */
			errno = 0;
			inc = strtol(arg + 1, &end, 10);
			if (errno != 0 || *end != '\0' || inc < 0)
				errx(1, "%s: invalid increment", arg + 1);
		} else if (arg[1] == 'T') {
			/* -Ttype or -T type */
			if (arg[2] != '\0')
				setenv("TERM", arg + 2, 1);
			else {
				if ((arg = *++argv) == NULL)
					usage();
				setenv("TERM", arg, 1);
			}
		} else if (arg[1] == '-') {
			arg = *++argv;
			break;
		} else {
			/* Predefined format */
			for (i = 0; i < (int)NELEMS(formats); i++)
				if (strcmp(formats[i].name, arg + 1) == 0)
					break;
			if (i == NELEMS(formats))
				usage();
			for (j = nstops = 0; j < NSTOPS &&
			    formats[i].stops[j] != 0; j++)
				stops[nstops++] = formats[i].stops[j];
		}
	}	
	
	if (arg != NULL) {
		if (nstops != -1)
			usage();
		gettabs(arg, stops, &nstops);
	}

	/* Initialise terminal, get the strings we need */
	setupterm(NULL, 1, NULL);
	ap = area;
	if ((ct = tgetstr("ct", &ap)) == NULL)
		errx(1, "terminal cannot clear tabs");
	if ((st = tgetstr("st", &ap)) == NULL)
		errx(1, "terminal cannot set tabs");
	if ((cr = tgetstr("cr", &ap)) == NULL)
		cr = "\r";
	ML = tgetstr("ML", &ap);
	cols = ttywidth();

	/* Clear all tabs. */
	putp(cr);
	putp(ct);

	/*
	 * Set soft margin.
	 * XXX Does this actually work?
	 */
	if (ML != NULL) {
		printf("%*s", (int)margin, "");
		putp(ML);
	} else if (margin != 0)
		warnx("terminal cannot set left margin");

	/* Optionally output new tab stops. */
	if (nstops >= 0) {
		printf("%*s", (int)stops[0] - 1, "");
		putp(st);
		for (i = 1; i < nstops; i++) {
			printf("%*s", (int)(stops[i] - stops[i - 1]), "");
			putp(st);
		}
	} else if (inc > 0) {
		for (i = 0; i < cols / inc; i++) {
			putp(st);
			printf("%*s", (int)inc, "");
		}
		putp(st);
	}
	putp(cr);

	exit(0);
}

static void
usage(void)
{

	fprintf(stderr,
"usage: tabs [-n|-a|-a2|-c|-c2|-c3|-f|-p|-s|-u] [+m[n]] [-T type]\n");
	fprintf(stderr,
"       tabs [-T type] [+[n]] n1,[n2,...]\n");
	exit(1);
}

static void
gettabs(char *arg, long stops[], long *nstops)
{
	char *tok, *end;
	long last, stop;

	for (last = *nstops = 0, tok = strtok(arg, ","); tok != NULL;
	    tok = strtok(NULL, ",")) {
		if (*nstops >= NSTOPS)
			errx(1, "too many tab stops (limit %d)", NSTOPS);
		errno = 0;
		stop = strtol(tok, &end, 10);
		if (errno != 0 || *end != '\0' || stop <= 0)
			errx(1, "%s: invalid tab stop", tok);
		if (*tok == '+') {
			if (tok == arg)
				errx(1, "%s: first tab may not be relative",
				    tok);
			stop += last;
		}
		if (last > stop)
			errx(1, "cannot go backwards");
		last = stops[(*nstops)++] = stop;
	}
}

static int
ttywidth(void)
{
	struct winsize ws;
	int width;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1)
		width = ws.ws_col;
	else if ((width = tgetnum("co")) == 0) {
		width = 80;
		warnx("cannot find terminal width; defaulted to %d", width);
	}

	return (width);
}
