/*	$OpenBSD: save.c,v 1.14 2015/11/30 08:19:25 tb Exp $	*/

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

#include <ctype.h>
#include <limits.h>
#include <errno.h>
#include "back.h"

static const char confirm[] = "Are you sure you want to leave now?";
static const char prompt[] = "Enter a file name:  ";
static const char exist1[] = "The file '";
static const char exist2[] =
	"' already exists.\nAre you sure you want to use this file?";
static const char cantuse[] = "\nCan't use ";
static const char saved[] = "This game has been saved on the file '";
static const char type[] = "'.\nType \"backgammon -s ";
static const char rec[] = "\" to recover your game.\n\n";
static const char cantrec[] = "Can't recover file:  ";

void
save(int n)
{
	int     fdesc;
	char   *fs;
	char    fname[PATH_MAX];
	int     r, c, i;

	if (n) {
		move(20, 0);
		clrtobot();
		addstr(confirm);
		if (!yorn(0))
			return;
	}
	cflag = 1;
	for (;;) {
		addstr(prompt);
		fs = fname;
		while ((i = readc()) != '\n') {
			if (i == KEY_BACKSPACE || i == 0177) {
				if (fs > fname) {
					fs--;
					getyx(stdscr, r, c);
					move(r, c - 1);
				} else
					beep();
				continue;
			}
			if (fs - fname < sizeof(fname) - 1) {
				if (isascii(i)) {
					*fs = i;
					addch(*fs++);
				} else
					beep();
			} else
				beep();
		}
		*fs = '\0';
		if ((fdesc = open(fname, O_RDWR)) == -1 && errno == ENOENT) {
			if ((fdesc = open(fname,
					  O_CREAT | O_TRUNC | O_WRONLY,
					  0600)) != -1)
				break;
		}
		if (fdesc != -1) {
			move(18, 0);
			clrtobot();
			printw("%s%s%s", exist1, fname, exist2);
			cflag = 0;
			close(fdesc);
			if (yorn(0)) {
				unlink(fname);
				fdesc = open(fname,
					     O_CREAT | O_TRUNC | O_WRONLY,
					     0600);
				break;
			} else {
				cflag = 1;
				continue;
			}
		}
		printw("%s%s.\n", cantuse, fname);
		cflag = 1;
	}
	write(fdesc, board, sizeof(board));
	write(fdesc, off, sizeof(off));
	write(fdesc, in, sizeof(in));
	write(fdesc, dice, sizeof(dice));
	write(fdesc, &cturn, sizeof(cturn));
	write(fdesc, &dflag, sizeof(dflag));
	write(fdesc, &dlast, sizeof(dlast));
	write(fdesc, &pnum, sizeof(pnum));
	write(fdesc, &rscore, sizeof(rscore));
	write(fdesc, &wscore, sizeof(wscore));
	write(fdesc, &gvalue, sizeof(gvalue));
	write(fdesc, &raflag, sizeof(raflag));
	close(fdesc);
	move(18, 0);
	printw("%s%s%s%s%s", saved, fname, type, fname, rec);
	clrtobot();
	getout(0);
}

void
recover(const char *s)
{
	int     fdesc;

	if ((fdesc = open(s, O_RDONLY)) == -1)
		norec(s);
	read(fdesc, board, sizeof(board));
	read(fdesc, off, sizeof(off));
	read(fdesc, in, sizeof(in));
	read(fdesc, dice, sizeof(dice));
	read(fdesc, &cturn, sizeof(cturn));
	read(fdesc, &dflag, sizeof(dflag));
	read(fdesc, &dlast, sizeof(dlast));
	read(fdesc, &pnum, sizeof(pnum));
	read(fdesc, &rscore, sizeof(rscore));
	read(fdesc, &wscore, sizeof(wscore));
	read(fdesc, &gvalue, sizeof(gvalue));
	read(fdesc, &raflag, sizeof(raflag));
	close(fdesc);
	rflag = 1;
}

void
norec(const char *s)
{
	const char   *c;

	addstr(cantrec);
	c = s;
	while (*c != '\0')
		addch(*c++);
	getout(0);
}
