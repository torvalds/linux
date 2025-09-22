/*	$OpenBSD: main.c,v 1.20 2021/10/23 11:22:49 mestre Exp $	*/
/*	$NetBSD: main.c,v 1.3 1995/03/23 08:32:50 cgd Exp $	*/

/*
 * Copyright (c) 1983, 1993
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

#include <curses.h>
#include <err.h>
#include <paths.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#include "hangman.h"

__dead void	usage(void);

/*
 * This game written by Ken Arnold.
 */
int
main(int argc, char *argv[])
{
	int ch;

	while ((ch = getopt(argc, argv, "d:hk")) != -1) {
		switch (ch) {
		case 'd':
			if (syms)
				usage();
			else
				Dict_name = optarg;
			break;
		case 'k':
			syms = 1;
			Dict_name = _PATH_KSYMS;
			break;
		case 'h':
		default:
			usage();
		}
	}

	initscr();
	if (COLS < 50 || LINES < 14) {
		endwin();
		errx(1, "screen too small (must be at least 50x14)");
	}
	signal(SIGINT, die);
	setup();

	if (pledge("stdio tty", NULL) == -1)
		err(1, "pledge");

	for (;;) {
		Wordnum++;
		playgame();
		Average = (Average * (Wordnum - 1) + Errors) / Wordnum;
	}
}

/*
 * die:
 *	Die properly.
 */
void
die(int dummy)
{
	mvcur(0, COLS - 1, LINES - 1, 0);
	endwin();
	putchar('\n');
	exit(0);
}

__dead void
usage(void)
{
	(void)fprintf(stderr, "usage: %s [-k] [-d wordlist]\n", getprogname());
	exit(1);
}
