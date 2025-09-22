/*	$OpenBSD: bcd.c,v 1.26 2018/01/23 07:06:55 otto Exp $	*/
/*	$NetBSD: bcd.c,v 1.6 1995/04/24 12:22:23 cgd Exp $	*/

/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Steve Hayman of the Indiana University Computer Science Dept.
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

/*
 * bcd --
 *
 * Read one line of standard input and produce something that looks like a
 * punch card.  An attempt to reimplement /usr/games/bcd.  All I looked at
 * was the man page.
 *
 * I couldn't find a BCD table handy so I wrote a shell script to deduce what
 * the patterns were that the old bcd was using for each possible 8-bit
 * character.  These are the results -- the low order 12 bits represent the
 * holes.  (A 1 bit is a hole.)  These may be wrong, but they match the old
 * program!
 *
 * Steve Hayman
 * sahayman@iuvax.cs.indiana.edu
 * 1989 11 30
 *
 *
 * I found an error in the table. The same error is found in the SunOS 4.1.1
 * version of bcd. It has apparently been around a long time. The error caused
 * 'Q' and 'R' to have the same punch code. I only noticed the error due to
 * someone pointing it out to me when the program was used to print a cover
 * for an APA!  The table was wrong in 4 places. The other error was masked
 * by the fact that the input is converted to upper case before lookup.
 *
 * Dyane Bruce
 * db@diana.ocunix.on.ca
 * Nov 5, 1993
 */

#include <err.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

u_short holes[256] = {
    0x0,	 0x0,	  0x0,	   0x0,	    0x0,     0x0,     0x0,     0x0,
    0x0,	 0x0,	  0x0,	   0x0,	    0x0,     0x0,     0x0,     0x0,
    0x0,	 0x0,	  0x0,	   0x0,	    0x0,     0x0,     0x0,     0x0,
    0x0,	 0x0,	  0x0,	   0x0,	    0x0,     0x0,     0x0,     0x0,
    0x0,	 0x206,	  0x20a,   0x042,   0x442,   0x222,   0x800,   0x406,
    0x812,	 0x412,	  0x422,   0xa00,   0x242,   0x400,   0x842,   0x300,
    0x200,	 0x100,	  0x080,   0x040,   0x020,   0x010,   0x008,   0x004,
    0x002,	 0x001,	  0x012,   0x40a,   0x80a,   0x212,   0x00a,   0x006,
    0x022,	 0x900,	  0x880,   0x840,   0x820,   0x810,   0x808,   0x804,
    0x802,	 0x801,	  0x500,   0x480,   0x440,   0x420,   0x410,   0x408,
    0x404,	 0x402,	  0x401,   0x280,   0x240,   0x220,   0x210,   0x208,
    0x204,	 0x202,	  0x201,   0x082,   0x806,   0x822,   0x600,   0x282,
    0x022,	 0x900,	  0x880,   0x840,   0x820,   0x810,   0x808,   0x804,
    0x802,	 0x801,	  0x500,   0x480,   0x440,   0x420,   0x410,   0x408,
    0x404,	 0x402,	  0x401,   0x280,   0x240,   0x220,   0x210,   0x208,
    0x204,	 0x202,	  0x201,   0x082,   0x806,   0x822,   0x600,   0x282,
    0x0,	 0x0,	  0x0,	   0x0,	    0x0,     0x0,     0x0,     0x0,
    0x0,	 0x0,	  0x0,	   0x0,	    0x0,     0x0,     0x0,     0x0,
    0x0,	 0x0,	  0x0,	   0x0,	    0x0,     0x0,     0x0,     0x0,
    0x0,	 0x0,	  0x0,	   0x0,	    0x0,     0x0,     0x0,     0x0,
    0x0,	 0x206,	  0x20a,   0x042,   0x442,   0x222,   0x800,   0x406,
    0x812,	 0x412,	  0x422,   0xa00,   0x242,   0x400,   0x842,   0x300,
    0x200,	 0x100,	  0x080,   0x040,   0x020,   0x010,   0x008,   0x004,
    0x002,	 0x001,	  0x012,   0x40a,   0x80a,   0x212,   0x00a,   0x006,
    0x022,	 0x900,	  0x880,   0x840,   0x820,   0x810,   0x808,   0x804,
    0x802,	 0x801,	  0x500,   0x480,   0x440,   0x420,   0x410,   0x408,
    0x404,	 0x402,	  0x401,   0x280,   0x240,   0x220,   0x210,   0x208,
    0x204,	 0x202,	  0x201,   0x082,   0x806,   0x822,   0x600,   0x282,
    0x022,	 0x900,	  0x880,   0x840,   0x820,   0x810,   0x808,   0x804,
    0x802,	 0x801,	  0x500,   0x480,   0x440,   0x420,   0x410,   0x408,
    0x404,	 0x402,	  0x401,   0x280,   0x240,   0x220,   0x210,   0x208,
    0x204,	 0x202,	  0x201,   0x082,   0x806,   0x822,   0x600,   0x282,
};

/*
 * i'th bit of w.
 */
#define	bit(w,i)	((w)&(1<<(i)))

void	printonecard(char *, size_t);
void	printcard(char *);
int	decode(char *buf);

int	columns	= 48;

int
main(int argc, char *argv[])
{
	char cardline[1024];
	extern char *__progname;
	int dflag = 0;
	int ch;

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	while ((ch = getopt(argc, argv, "dl")) != -1) {
		switch (ch) {
		case 'd':
			dflag = 1;
			break;
		case 'l':
			columns = 80;
			break;
		default:
			fprintf(stderr, "usage: %s [-l] [string ...]\n",
			    __progname);
			fprintf(stderr, "usage: %s -d [-l]\n", __progname);
			return 1;
		}
	}
	argc -= optind;
	argv += optind;

	if (dflag) {
		while (decode(cardline) == 0) {
			printf("%s\n", cardline);
		}
		return 0;
	}


	/*
	 * The original bcd prompts with a "%" when reading from stdin,
	 * but this seems kind of silly.  So this one doesn't.
	 */
	if (argc > 0) {
		while (argc--) {
			printcard(*argv);
			argv++;
		}
	} else {
		while (fgets(cardline, sizeof(cardline), stdin))
			printcard(cardline);
	}
	return 0;
}

void
printcard(char *str)
{
	size_t len = strlen(str);

	while (len > 0) {
		size_t amt = len > columns ? columns : len;
		printonecard(str, amt);
		str += amt;
		len -= amt;
	}
}

void
printonecard(char *str, size_t len)
{
	static const char rowchars[] = "   123456789";
	int	i, row;
	char	*p, *end;

	end = str + len;

	/* make string upper case. */
	for (p = str; p < end; ++p)
		*p = toupper((unsigned char)*p);

	/* top of card */
	putchar(' ');
	for (i = 1; i <= columns; ++i)
		putchar('_');
	putchar('\n');

	/*
	 * line of text.  Leave a blank if the character doesn't have
	 * a hole pattern.
	 */
	p = str;
	putchar('/');
	for (i = 1; p < end; i++, p++)
		if (holes[(unsigned char)*p])
			putchar(*p);
		else
			putchar(' ');
	while (i++ <= columns)
		putchar(' ');
	putchar('|');
	putchar('\n');

	/*
	 * 12 rows of potential holes; output a ']', which looks kind of
	 * like a hole, if the appropriate bit is set in the holes[] table.
	 * The original bcd output a '[', a backspace, five control A's,
	 * and then a ']'.  This seems a little excessive.
	 */
	for (row = 0; row <= 11; ++row) {
		putchar('|');
		for (i = 0, p = str; p < end; i++, p++) {
			if (bit(holes[(unsigned char)*p], 11 - row))
				putchar(']');
			else
				putchar(rowchars[row]);
		}
		while (i++ < columns)
			putchar(rowchars[row]);
		putchar('|');
		putchar('\n');
	}

	/* bottom of card */
	putchar('|');
	for (i = 1; i <= columns; i++)
		putchar('_');
	putchar('|');
	putchar('\n');
}

#define LINES 12

int
decode(char *buf)
{
	int col, i;
	char lines[LINES][1024];
	char tmp[1024];

	/* top of card; if missing signal no more input */
	if (fgets(tmp, sizeof(tmp), stdin) == NULL)
		return 1;
	/* text line, ignored */
	if (fgets(tmp, sizeof(tmp), stdin) == NULL)
		return -1;
	/* twelve lines of data */
	for (i = 0; i < LINES; i++)
		if (fgets(lines[i], sizeof(lines[i]), stdin) == NULL)
			return -1;
	/* bottom of card */
	if (fgets(tmp, sizeof(tmp), stdin) == NULL)
		return -1;

	for (i = 0; i < LINES; i++) {
		if (strlen(lines[i]) < columns + 2)
			return -1;
		if (lines[i][0] != '|' || lines[i][columns + 1] != '|')
			return -1;
		memmove(&lines[i][0], &lines[i][1], columns);
		lines[i][columns] = 0;
	}
	for (col = 0; col < columns; col++) {
		unsigned int val = 0;
		for (i = 0; i < LINES; i++)
			if (lines[i][col] == ']')
				val |= 1 << (11 - i);
		buf[col] = ' ';
		for (i = 0; i < 256; i++)
			if (holes[i] == val && holes[i]) {
				buf[col] = i;
				break;
			}
	}
	buf[col] = 0;
	for (col = columns - 1; col >= 0; col--) {
		if (buf[col] == ' ')
			buf[col] = '\0';
		else
			break;
	}
	return 0;
}
