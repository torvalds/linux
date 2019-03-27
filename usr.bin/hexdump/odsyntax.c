/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1990, 1993
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
#if 0
static char sccsid[] = "@(#)odsyntax.c	8.2 (Berkeley) 5/4/95";
#endif
#endif /* not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hexdump.h"

#define PADDING	"         "

int odmode;

static void odadd(const char *);
static void odformat(const char *);
static const char *odformatfp(char, const char *);
static const char *odformatint(char, const char *);
static void odoffset(int, char ***);
static void odusage(void);

void
oldsyntax(int argc, char ***argvp)
{
	static char empty[] = "", padding[] = PADDING;
	int ch;
	char **argv, *end;

	/* Add initial (default) address format. -A may change it later. */
#define	TYPE_OFFSET	7
	add("\"%07.7_Ao\n\"");
	add("\"%07.7_ao  \"");

	odmode = 1;
	argv = *argvp;
	while ((ch = getopt(argc, argv, "A:aBbcDdeFfHhIij:LlN:Oost:vXx")) != -1)
		switch (ch) {
		case 'A':
			switch (*optarg) {
			case 'd': case 'o': case 'x':
				fshead->nextfu->fmt[TYPE_OFFSET] = *optarg;
				fshead->nextfs->nextfu->fmt[TYPE_OFFSET] =
				    *optarg;
				break;
			case 'n':
				fshead->nextfu->fmt = empty;
				fshead->nextfs->nextfu->fmt = padding;
				break;
			default:
				errx(1, "%s: invalid address base", optarg);
			}
			break;
		case 'a':
			odformat("a");
			break;
		case 'B':
		case 'o':
			odformat("o2");
			break;
		case 'b':
			odformat("o1");
			break;
		case 'c':
			odformat("c");
			break;
		case 'd':
			odformat("u2");
			break;
		case 'D':
			odformat("u4");
			break;
		case 'e':		/* undocumented in od */
		case 'F':
			odformat("fD");
			break;
		case 'f':
			odformat("fF");
			break;
		case 'H':
		case 'X':
			odformat("x4");
			break;
		case 'h':
		case 'x':
			odformat("x2");
			break;
		case 'I':
		case 'L':
		case 'l':
			odformat("dL");
			break;
		case 'i':
			odformat("dI");
			break;
		case 'j':
			errno = 0;
			skip = strtoll(optarg, &end, 0);
			if (*end == 'b')
				skip *= 512;
			else if (*end == 'k')
				skip *= 1024;
			else if (*end == 'm')
				skip *= 1048576L;
			if (errno != 0 || skip < 0 || strlen(end) > 1)
				errx(1, "%s: invalid skip amount", optarg);
			break;
		case 'N':
			if ((length = atoi(optarg)) <= 0)
				errx(1, "%s: invalid length", optarg);
			break;
		case 'O':
			odformat("o4");
			break;
		case 's':
			odformat("d2");
			break;
		case 't':
			odformat(optarg);
			break;
		case 'v':
			vflag = ALL;
			break;
		case '?':
		default:
			odusage();
		}

	if (fshead->nextfs->nextfs == NULL)
		odformat("oS");

	argc -= optind;
	*argvp += optind;

	if (argc)
		odoffset(argc, argvp);
}

static void
odusage(void)
{

	fprintf(stderr,
"usage: od [-aBbcDdeFfHhIiLlOosvXx] [-A base] [-j skip] [-N length] [-t type]\n");
	fprintf(stderr,
"          [[+]offset[.][Bb]] [file ...]\n");
	exit(1);
}

static void
odoffset(int argc, char ***argvp)
{
	char *p, *num, *end;
	int base;

	/*
	 * The offset syntax of od(1) was genuinely bizarre.  First, if
	 * it started with a plus it had to be an offset.  Otherwise, if
	 * there were at least two arguments, a number or lower-case 'x'
	 * followed by a number makes it an offset.  By default it was
	 * octal; if it started with 'x' or '0x' it was hex.  If it ended
	 * in a '.', it was decimal.  If a 'b' or 'B' was appended, it
	 * multiplied the number by 512 or 1024 byte units.  There was
	 * no way to assign a block count to a hex offset.
	 *
	 * We assume it's a file if the offset is bad.
	 */
	p = argc == 1 ? (*argvp)[0] : (*argvp)[1];

	if (*p != '+' && (argc < 2 ||
	    (!isdigit(p[0]) && (p[0] != 'x' || !isxdigit(p[1])))))
		return;

	base = 0;
	/*
	 * skip over leading '+', 'x[0-9a-fA-f]' or '0x', and
	 * set base.
	 */
	if (p[0] == '+')
		++p;
	if (p[0] == 'x' && isxdigit(p[1])) {
		++p;
		base = 16;
	} else if (p[0] == '0' && p[1] == 'x') {
		p += 2;
		base = 16;
	}

	/* skip over the number */
	if (base == 16)
		for (num = p; isxdigit(*p); ++p);
	else
		for (num = p; isdigit(*p); ++p);

	/* check for no number */
	if (num == p)
		return;

	/* if terminates with a '.', base is decimal */
	if (*p == '.') {
		if (base)
			return;
		base = 10;
	}

	skip = strtoll(num, &end, base ? base : 8);

	/* if end isn't the same as p, we got a non-octal digit */
	if (end != p) {
		skip = 0;
		return;
	}

	if (*p) {
		if (*p == 'B') {
			skip *= 1024;
			++p;
		} else if (*p == 'b') {
			skip *= 512;
			++p;
		}
	}

	if (*p) {
		skip = 0;
		return;
	}

	/*
	 * If the offset uses a non-octal base, the base of the offset
	 * is changed as well.  This isn't pretty, but it's easy.
	 */
	if (base == 16) {
		fshead->nextfu->fmt[TYPE_OFFSET] = 'x';
		fshead->nextfs->nextfu->fmt[TYPE_OFFSET] = 'x';
	} else if (base == 10) {
		fshead->nextfu->fmt[TYPE_OFFSET] = 'd';
		fshead->nextfs->nextfu->fmt[TYPE_OFFSET] = 'd';
	}

	/* Terminate file list. */
	(*argvp)[1] = NULL;
}

static void
odformat(const char *fmt)
{
	char fchar;

	while (*fmt != '\0') {
		switch ((fchar = *fmt++)) {
		case 'a':
			odadd("16/1 \"%3_u \" \"\\n\"");
			break;
		case 'c':
			odadd("16/1 \"%3_c \" \"\\n\"");
			break;
		case 'o': case 'u': case 'd': case 'x':
			fmt = odformatint(fchar, fmt);
			break;
		case 'f':
			fmt = odformatfp(fchar, fmt);
			break;
		default:
			errx(1, "%c: unrecognised format character", fchar);
		}
	}
}

static const char *
odformatfp(char fchar __unused, const char *fmt)
{
	size_t isize;
	int digits;
	char *end, *hdfmt;

	isize = sizeof(double);
	switch (*fmt) {
	case 'F':
		isize = sizeof(float);
		fmt++;
		break;
	case 'D':
		isize = sizeof(double);
		fmt++;
		break;
	case 'L':
		isize = sizeof(long double);
		fmt++;
		break;
	default:
		if (isdigit((unsigned char)*fmt)) {
			errno = 0;
			isize = (size_t)strtoul(fmt, &end, 10);
			if (errno != 0 || isize == 0)
				errx(1, "%s: invalid size", fmt);
			fmt = (const char *)end;
		}
	}
	switch (isize) {
	case sizeof(float):
		digits = FLT_DIG;
		break;
	case sizeof(double):
		digits = DBL_DIG;
		break;
	default:
		if (isize == sizeof(long double))
			digits = LDBL_DIG;
		else
			errx(1, "unsupported floating point size %lu",
			    (u_long)isize);
	}

	asprintf(&hdfmt, "%lu/%lu \" %%%d.%de \" \"\\n\"",
	    16UL / (u_long)isize, (u_long)isize, digits + 8, digits);
	if (hdfmt == NULL)
		err(1, NULL);
	odadd(hdfmt);
	free(hdfmt);

	return (fmt);
}

static const char *
odformatint(char fchar, const char *fmt)
{
	unsigned long long n;
	size_t isize;
	int digits;
	char *end, *hdfmt;

	isize = sizeof(int);
	switch (*fmt) {
	case 'C':
		isize = sizeof(char);
		fmt++;
		break;
	case 'I':
		isize = sizeof(int);
		fmt++;
		break;
	case 'L':
		isize = sizeof(long);
		fmt++;
		break;
	case 'S':
		isize = sizeof(short);
		fmt++;
		break;
	default:
		if (isdigit((unsigned char)*fmt)) {
			errno = 0;
			isize = (size_t)strtoul(fmt, &end, 10);
			if (errno != 0 || isize == 0)
				errx(1, "%s: invalid size", fmt);
			if (isize != sizeof(char) && isize != sizeof(short) &&
			    isize != sizeof(int) && isize != sizeof(long))
				errx(1, "unsupported int size %lu",
				    (u_long)isize);
			fmt = (const char *)end;
		}
	}

	/*
	 * Calculate the maximum number of digits we need to
	 * fit the number. Overestimate for decimal with log
	 * base 8. We need one extra space for signed numbers
	 * to store the sign.
	 */
	n = (1ULL << (8 * isize)) - 1;
	digits = 0;
	while (n != 0) {
		digits++;
		n >>= (fchar == 'x') ? 4 : 3;
	}
	if (fchar == 'd')
		digits++;
	asprintf(&hdfmt, "%lu/%lu \"%*s%%%s%d%c\" \"\\n\"",
	    16UL / (u_long)isize, (u_long)isize, (int)(4 * isize - digits),
	    "", (fchar == 'd' || fchar == 'u') ? "" : "0", digits, fchar);
	if (hdfmt == NULL)
		err(1, NULL);
	odadd(hdfmt);
	free(hdfmt);

	return (fmt);
}

static void
odadd(const char *fmt)
{
	static int needpad;

	if (needpad)
		add("\""PADDING"\"");
	add(fmt);
	needpad = 1;
}
