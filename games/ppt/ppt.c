/*	$OpenBSD: ppt.c,v 1.17 2016/03/07 12:07:56 mestre Exp $	*/
/*	$NetBSD: ppt.c,v 1.4 1995/03/23 08:35:40 cgd Exp $	*/

/*
 * Copyright (c) 1988, 1993
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

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vis.h>


#define	EDGE	"___________"

__dead void	usage(void);
void	putppt(int);
int	getppt(const char *buf);

void
usage(void)
{
	extern char *__progname;
	fprintf(stderr, "usage: %s [string ...]\n", __progname);
	fprintf(stderr, "usage: %s -d [-b]\n", __progname);
	exit(1);
}

int
main(int argc, char **argv)
{
	char *p, buf[132];
	int c, start, seenl, dflag, bflag;

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	dflag = bflag = 0;
	while ((c = getopt(argc, argv, "bdh")) != -1)
		switch(c) {
		case 'd':
			dflag = 1;
			break;
		case 'b':
			bflag = 1;
			break;
		case 'h':
		default:
			usage();
		}
	if (bflag && !dflag)
		usage();
	argc -= optind;
	argv += optind;

	if (dflag) {
		if (argc > 0)
			usage();

		seenl = start = 0;
		while (fgets(buf, sizeof(buf), stdin) != NULL) {
			c = getppt(buf);
			if (c == -2)
				continue;
			if (c == -1) {
				if (start)
					/* lost sync */
					putchar('x');
				continue;
			}
			start = 1;
			if (bflag)
				putchar(c);
			else {
				char vbuf[5];
				vis(vbuf, c, VIS_NOSLASH, 0);
				fputs(vbuf, stdout);
			}
			seenl = (c == '\n');
		}
		if (!feof(stdin))
			err(1, "fgets");
		if (!seenl && !bflag)
			putchar('\n');
	} else {
		(void) puts(EDGE);
		if (argc > 0)
			while ((p = *argv++)) {
				for (; *p; ++p)
					putppt((int)*p);
				if (*argv)
					putppt((int)' ');
			}
		else while ((c = getchar()) != EOF)
			putppt(c);
		(void) puts(EDGE);
	}
	return 0;
}

void
putppt(int c)
{
	int i;

	(void) putchar('|');
	for (i = 7; i >= 0; i--) {
		if (i == 2)
			(void) putchar('.');	/* feed hole */
		if ((c&(1<<i)) != 0)
			(void) putchar('o');
		else
			(void) putchar(' ');
	}
	(void) putchar('|');
	(void) putchar('\n');
}

int
getppt(const char *buf)
{
	int c;

	/* Demand left-aligned paper tape, but allow comments to the right */
	if (strncmp(buf, EDGE, strlen(EDGE)) == 0)
	    return (-2);
	if (strlen(buf) < 12 || buf[0] != '|' || buf[10] != '|' ||
	    buf[6] != '.' || strspn(buf, "| o.") < 11)
		return (-1);

	c = 0;
	if (buf[1] != ' ')
		c |= 0200;
	if (buf[2] != ' ')
		c |= 0100;
	if (buf[3] != ' ')
		c |= 0040;
	if (buf[4] != ' ')
		c |= 0020;
	if (buf[5] != ' ')
		c |= 0010;
	if (buf[7] != ' ')
		c |= 0004;
	if (buf[8] != ' ')
		c |= 0002;
	if (buf[9] != ' ')
		c |= 0001;

	return (c);
}
