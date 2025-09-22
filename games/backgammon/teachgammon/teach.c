/*	$OpenBSD: teach.c,v 1.20 2021/10/23 15:08:26 mestre Exp $	*/

/*
 * Copyright (c) 1980, 1993
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
#include <unistd.h>

#include "back.h"
#include "tutor.h"

const char   *const helpm[] = {
	"\nEnter a space or newline to roll, or",
	"     b   to display the board",
	"     d   to double",
	"     q   to quit\n",
	0
};

const char   *const contin[] = {
	"",
	0
};

int
main(int argc, char *argv[])
{
	int     i;

	signal(SIGINT, getout);
	initcurses();

	if (pledge("stdio rpath wpath cpath tty exec", NULL) == -1)
		err(1, "pledge");

	text(hello);
	text(list);
	i = text(contin);
	if (i == 0)
		i = 2;
	init();
	while (i)
		switch (i) {
		case 1:
			leave();	/* Does not return */
			break;

		case 2:
			if ((i = text(intro1)))
				break;
			wrboard();
			if ((i = text(intro2)))
				break;

		case 3:
			if ((i = text(moves)))
				break;

		case 4:
			if ((i = text(removepiece)))
				break;

		case 5:
			if ((i = text(hits)))
				break;

		case 6:
			if ((i = text(endgame)))
				break;

		case 7:
			if ((i = text(doubl)))
				break;

		case 8:
			if ((i = text(stragy)))
				break;

		case 9:
			if ((i = text(prog)))
				break;

		case 10:
			if ((i = text(lastch)))
				break;
		}
	tutor();
	/* NOT REACHED */
}

__dead void
leave(void)
{
	clear();
	endwin();
	execl(EXEC, "backgammon", "-n", (char *)NULL);
	errx(1, "help! Backgammon program is missing!!");
}
