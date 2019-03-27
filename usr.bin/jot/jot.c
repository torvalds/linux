/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1993
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
 */

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)jot.c	8.1 (Berkeley) 6/6/93";
#endif
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * jot - print sequential or random data
 *
 * Author:  John Kunze, Office of Comp. Affairs, UCB
 */

#include <sys/capsicum.h>
#include <capsicum_helpers.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Defaults */
#define	REPS_DEF	100
#define	BEGIN_DEF	1
#define	ENDER_DEF	100
#define	STEP_DEF	1

/* Flags of options that have been set */
#define HAVE_STEP	1
#define HAVE_ENDER	2
#define HAVE_BEGIN	4
#define HAVE_REPS	8

#define	is_default(s)	(*(s) == 0 || strcmp((s), "-") == 0)

static bool	boring;
static int	prec = -1;
static bool	longdata;
static bool	intdata;
static bool	chardata;
static bool	nosign;
static const	char *sepstring = "\n";
static char	format[BUFSIZ];

static void	getformat(void);
static int	getprec(const char *);
static int	putdata(double, bool);
static void	usage(void);

int
main(int argc, char **argv)
{
	cap_rights_t rights;
	bool	have_format = false;
	bool	infinity = false;
	bool	nofinalnl = false;
	bool	randomize = false;
	bool	use_random = false;
	int	ch;
	int	mask = 0;
	int	n = 0;
	double	begin = BEGIN_DEF;
	double	divisor;
	double	ender = ENDER_DEF;
	double	s = STEP_DEF;
	double	x, y;
	long	i;
	long	reps = REPS_DEF;

	if (caph_limit_stdio() < 0)
		err(1, "unable to limit rights for stdio");
	cap_rights_init(&rights);
	if (caph_rights_limit(STDIN_FILENO, &rights) < 0)
		err(1, "unable to limit rights for stdin");

	/*
	 * Cache NLS data, for strerror, for err(3), before entering capability
	 * mode.
	 */
	caph_cache_catpages();

	if (caph_enter() < 0)
		err(1, "unable to enter capability mode");

	while ((ch = getopt(argc, argv, "b:cnp:rs:w:")) != -1)
		switch (ch) {
		case 'b':
			boring = true;
			/* FALLTHROUGH */
		case 'w':
			if (strlcpy(format, optarg, sizeof(format)) >=
			    sizeof(format))
				errx(1, "-%c word too long", ch);
			have_format = true;
			break;
		case 'c':
			chardata = true;
			break;
		case 'n':
			nofinalnl = true;
			break;
		case 'p':
			prec = atoi(optarg);
			if (prec < 0)
				errx(1, "bad precision value");
			have_format = true;
			break;
		case 'r':
			randomize = true;
			break;
		case 's':
			sepstring = optarg;
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	switch (argc) {	/* examine args right to left, falling thru cases */
	case 4:
		if (!is_default(argv[3])) {
			if (!sscanf(argv[3], "%lf", &s))
				errx(1, "bad s value: %s", argv[3]);
			mask |= HAVE_STEP;
			if (randomize)
				use_random = true;
		}
		/* FALLTHROUGH */
	case 3:
		if (!is_default(argv[2])) {
			if (!sscanf(argv[2], "%lf", &ender))
				ender = argv[2][strlen(argv[2])-1];
			mask |= HAVE_ENDER;
			if (prec < 0)
				n = getprec(argv[2]);
		}
		/* FALLTHROUGH */
	case 2:
		if (!is_default(argv[1])) {
			if (!sscanf(argv[1], "%lf", &begin))
				begin = argv[1][strlen(argv[1])-1];
			mask |= HAVE_BEGIN;
			if (prec < 0)
				prec = getprec(argv[1]);
			if (n > prec)		/* maximum precision */
				prec = n;
		}
		/* FALLTHROUGH */
	case 1:
		if (!is_default(argv[0])) {
			if (!sscanf(argv[0], "%ld", &reps))
				errx(1, "bad reps value: %s", argv[0]);
			mask |= HAVE_REPS;
		}
		break;
	case 0:
		usage();
	default:
		errx(1, "too many arguments.  What do you mean by %s?",
		    argv[4]);
	}
	getformat();

	if (prec == -1)
		prec = 0;

	while (mask)	/* 4 bit mask has 1's where last 4 args were given */
		switch (mask) {	/* fill in the 0's by default or computation */
		case HAVE_STEP:
		case HAVE_ENDER:
		case HAVE_ENDER | HAVE_STEP:
		case HAVE_BEGIN:
		case HAVE_BEGIN | HAVE_STEP:
			reps = REPS_DEF;
			mask |= HAVE_REPS;
			break;
		case HAVE_BEGIN | HAVE_ENDER:
			s = ender > begin ? 1 : -1;
			mask |= HAVE_STEP;
			break;
		case HAVE_BEGIN | HAVE_ENDER | HAVE_STEP:
			if (randomize)
				reps = REPS_DEF;
			else if (s == 0.0)
				reps = 0;
			else
				reps = (ender - begin + s) / s;
			if (reps <= 0)
				errx(1, "impossible stepsize");
			mask = 0;
			break;
		case HAVE_REPS:
		case HAVE_REPS | HAVE_STEP:
			begin = BEGIN_DEF;
			mask |= HAVE_BEGIN;
			break;
		case HAVE_REPS | HAVE_ENDER:
			s = STEP_DEF;
			mask = HAVE_REPS | HAVE_ENDER | HAVE_STEP;
			break;
		case HAVE_REPS | HAVE_ENDER | HAVE_STEP:
			if (randomize)
				begin = BEGIN_DEF;
			else if (reps == 0)
				errx(1, "must specify begin if reps == 0");
			begin = ender - reps * s + s;
			mask = 0;
			break;
		case HAVE_REPS | HAVE_BEGIN:
			s = STEP_DEF;
			mask = HAVE_REPS | HAVE_BEGIN | HAVE_STEP;
			break;
		case HAVE_REPS | HAVE_BEGIN | HAVE_STEP:
			if (randomize)
				ender = ENDER_DEF;
			else
				ender = begin + reps * s - s;
			mask = 0;
			break;
		case HAVE_REPS | HAVE_BEGIN | HAVE_ENDER:
			if (reps == 0)
				errx(1, "infinite sequences cannot be bounded");
			else if (reps == 1)
				s = 0.0;
			else
				s = (ender - begin) / (reps - 1);
			mask = 0;
			break;
		case HAVE_REPS | HAVE_BEGIN | HAVE_ENDER | HAVE_STEP:
			/* if reps given and implied, */
			if (!randomize && s != 0.0) {
				long t = (ender - begin + s) / s;
				if (t <= 0)
					errx(1, "impossible stepsize");
				if (t < reps)		/* take lesser */
					reps = t;
			}
			mask = 0;
			break;
		default:
			errx(1, "bad mask");
		}
	if (reps == 0)
		infinity = true;
	if (randomize) {
		if (use_random) {
			srandom((unsigned long)s);
			divisor = (double)INT32_MAX + 1;
		} else
			divisor = (double)UINT32_MAX + 1;

		/*
		 * Attempt to DWIM when the user has specified an
		 * integer range within that of the random number
		 * generator: distribute the numbers equally in
		 * the range [begin .. ender].  Jot's default %.0f
		 * format would make the appearance of the first and
		 * last specified value half as likely as the rest.
		 */
		if (!have_format && prec == 0 &&
		    begin >= 0 && begin < divisor &&
		    ender >= 0 && ender < divisor) {
			if (begin <= ender)
				ender += 1;
			else
				begin += 1;
			nosign = true;
			intdata = true;
			(void)strlcpy(format,
			    chardata ? "%c" : "%u", sizeof(format));
		}
		x = ender - begin;
		for (i = 1; i <= reps || infinity; i++) {
			if (use_random)
				y = random() / divisor;
			else
				y = arc4random() / divisor;
			if (putdata(y * x + begin, !(reps - i)))
				errx(1, "range error in conversion");
		}
	} else
		for (i = 1, x = begin; i <= reps || infinity; i++, x += s)
			if (putdata(x, !(reps - i)))
				errx(1, "range error in conversion");
	if (!nofinalnl)
		putchar('\n');
	exit(0);
}

/*
 * Send x to stdout using the specified format.
 * Last is  true if this is the set's last value.
 * Return 0 if OK, or a positive number if the number passed was
 * outside the range specified by the various flags.
 */
static int
putdata(double x, bool last)
{

	if (boring)
		printf("%s", format);
	else if (longdata && nosign) {
		if (x <= (double)ULONG_MAX && x >= (double)0)
			printf(format, (unsigned long)x);
		else
			return (1);
	} else if (longdata) {
		if (x <= (double)LONG_MAX && x >= (double)LONG_MIN)
			printf(format, (long)x);
		else
			return (1);
	} else if (chardata || (intdata && !nosign)) {
		if (x <= (double)INT_MAX && x >= (double)INT_MIN)
			printf(format, (int)x);
		else
			return (1);
	} else if (intdata) {
		if (x <= (double)UINT_MAX && x >= (double)0)
			printf(format, (unsigned int)x);
		else
			return (1);

	} else
		printf(format, x);
	if (!last)
		fputs(sepstring, stdout);

	return (0);
}

static void
usage(void)
{
	fprintf(stderr, "%s\n%s\n",
	"usage: jot [-cnr] [-b word] [-w word] [-s string] [-p precision]",
	"           [reps [begin [end [s]]]]");
	exit(1);
}

/* 
 * Return the number of digits following the number's decimal point.
 * Return 0 if no decimal point is found.
 */
static int
getprec(const char *str)
{
	const char	*p;
	const char	*q;

	for (p = str; *p; p++)
		if (*p == '.')
			break;
	if (!*p)
		return (0);
	for (q = ++p; *p; p++)
		if (!isdigit((unsigned char)*p))
			break;
	return (p - q);
}

/*
 * Set format, intdata, chardata, longdata, and nosign
 * based on the command line arguments.
 */
static void
getformat(void)
{
	char	*p, *p2;
	int dot, hash, space, sign, numbers = 0;
	size_t sz;

	if (boring)				/* no need to bother */
		return;
	for (p = format; *p; p++)		/* look for '%' */
		if (*p == '%') {
			if (p[1] == '%')
				p++;		/* leave %% alone */
			else
				break;
		}
	sz = sizeof(format) - strlen(format) - 1;
	if (!*p && !chardata) {
		if (snprintf(p, sz, "%%.%df", prec) >= (int)sz)
			errx(1, "-w word too long");
	} else if (!*p && chardata) {
		if (strlcpy(p, "%c", sz) >= sz)
			errx(1, "-w word too long");
		intdata = true;
	} else if (!*(p+1)) {
		if (sz <= 0)
			errx(1, "-w word too long");
		strcat(format, "%");		/* cannot end in single '%' */
	} else {
		/*
		 * Allow conversion format specifiers of the form
		 * %[#][ ][{+,-}][0-9]*[.[0-9]*]? where ? must be one of
		 * [l]{d,i,o,u,x} or {f,e,g,E,G,d,o,x,D,O,U,X,c,u}
		 */
		p2 = p++;
		dot = hash = space = sign = numbers = 0;
		while (!isalpha((unsigned char)*p)) {
			if (isdigit((unsigned char)*p)) {
				numbers++;
				p++;
			} else if ((*p == '#' && !(numbers|dot|sign|space|
			    hash++)) ||
			    (*p == ' ' && !(numbers|dot|space++)) ||
			    ((*p == '+' || *p == '-') && !(numbers|dot|sign++))
			    || (*p == '.' && !(dot++)))
				p++;
			else
				goto fmt_broken;
		}
		if (*p == 'l') {
			longdata = true;
			if (*++p == 'l') {
				if (p[1] != '\0')
					p++;
				goto fmt_broken;
			}
		}
		switch (*p) {
		case 'o': case 'u': case 'x': case 'X':
			intdata = nosign = true;
			break;
		case 'd': case 'i':
			intdata = true;
			break;
		case 'D':
			if (!longdata) {
				intdata = true;
				break;
			}
		case 'O': case 'U':
			if (!longdata) {
				intdata = nosign = true;
				break;
			}
		case 'c':
			if (!(intdata | longdata)) {
				chardata = true;
				break;
			}
		case 'h': case 'n': case 'p': case 'q': case 's': case 'L':
		case '$': case '*':
			goto fmt_broken;
		case 'f': case 'e': case 'g': case 'E': case 'G':
			if (!longdata)
				break;
			/* FALLTHROUGH */
		default:
fmt_broken:
			*++p = '\0';
			errx(1, "illegal or unsupported format '%s'", p2);
			/* NOTREACHED */
		}
		while (*++p)
			if (*p == '%' && *(p+1) && *(p+1) != '%')
				errx(1, "too many conversions");
			else if (*p == '%' && *(p+1) == '%')
				p++;
			else if (*p == '%' && !*(p+1)) {
				if (strlcat(format, "%", sizeof(format)) >=
				    sizeof(format))
					errx(1, "-w word too long");
				break;
			}
	}
}
