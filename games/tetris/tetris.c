/*	$OpenBSD: tetris.c,v 1.35 2021/07/12 15:09:18 beck Exp $	*/
/*	$NetBSD: tetris.c,v 1.2 1995/04/22 07:42:47 cgd Exp $	*/

/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek and Darren F. Provine.
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
 *	@(#)tetris.c	8.1 (Berkeley) 5/31/93
 */

/*
 * Tetris (or however it is spelled).
 */

#include <err.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "input.h"
#include "scores.h"
#include "screen.h"
#include "tetris.h"

#define NUMKEYS 6

cell	board[B_SIZE];
int	Rows, Cols;
const struct shape *curshape;
const struct shape *nextshape;
long	fallrate;
int	score;
char	key_msg[100];
char	scorepath[PATH_MAX];
int	showpreview, classic;

static void		 elide(void);
void			 onintr(int);
const struct shape	*randshape(void);
static void		 setup_board(void);
__dead void		 usage(void);

/*
 * Set up the initial board.  The bottom display row is completely set,
 * along with another (hidden) row underneath that.  Also, the left and
 * right edges are set.
 */
static void
setup_board(void)
{
	int i;
	cell *p;

	p = board;
	for (i = B_SIZE; i; i--)
		*p++ = i <= (2 * B_COLS) || (i % B_COLS) < 2;
}

/*
 * Elide any full active rows.
 */
static void
elide(void)
{
	int rows = 0;
	int i, j, base;
	cell *p;

	for (i = A_FIRST; i < A_LAST; i++) {
		base = i * B_COLS + 1;
		p = &board[base];
		for (j = B_COLS - 2; *p++ != 0;) {
			if (--j <= 0) {
				/* this row is to be elided */
				rows++;
				memset(&board[base], 0, B_COLS - 2);
				scr_update();
				tsleep();
				while (--base != 0)
					board[base + B_COLS] = board[base];
				memset(&board[1], 0, B_COLS - 2);
				scr_update();
				tsleep();
				break;
			}
		}
	}
	switch (rows) {
	case 1:
		score += 10;
		break;
	case 2:
		score += 30;
		break;
	case 3:
		score += 70;
		break;
	case 4:
		score += 150;
		break;
	default:
		break;
	}
}

const struct shape *
randshape(void)
{
	const struct shape *tmp;
	int i, j;

	tmp = &shapes[arc4random_uniform(7)];
	j = arc4random_uniform(4);
	for (i = 0; i < j; i++)
		tmp = &shapes[classic? tmp->rotc : tmp->rot];
	return (tmp);
}

int
main(int argc, char *argv[])
{
	int pos, c;
	char *keys;
	int level = 2, ret;
	char key_write[NUMKEYS][10];
	char *home;
	const char *errstr;
	int ch, i, j;

	home = getenv("HOME");
	if (home == NULL || *home == '\0')
		err(1, "getenv");

	ret = snprintf(scorepath, sizeof(scorepath), "%s/%s", home,
	    ".tetris.scores");
	if (ret < 0 || ret >= PATH_MAX)
		errc(1, ENAMETOOLONG, "%s/%s", home, ".tetris.scores");

	if (pledge("stdio rpath wpath cpath tty unveil", NULL) == -1)
		err(1, "pledge");

	keys = "jkl pq";

	classic = showpreview = 0;
	while ((ch = getopt(argc, argv, "ck:l:ps")) != -1)
		switch(ch) {
		case 'c':
			/*
			 * this means:
			 *	- rotate the other way;
			 *	- no reverse video.
			 */
			classic = 1;
			break;
		case 'k':
			if (strlen(keys = optarg) != NUMKEYS)
				usage();
			break;
		case 'l':
			level = (int)strtonum(optarg, MINLEVEL, MAXLEVEL,
			    &errstr);
			if (errstr)
				errx(1, "level must be from %d to %d",
				    MINLEVEL, MAXLEVEL);
			break;
		case 'p':
			showpreview = 1;
			break;
		case 's':
			showscores(0);
			return 0;
		default:
			usage();
		}

	argc -= optind;
	argv += optind;

	if (argc)
		usage();

	fallrate = 1000000000L / level;

	for (i = 0; i < NUMKEYS; i++) {
		for (j = i+1; j < NUMKEYS; j++) {
			if (keys[i] == keys[j])
				errx(1, "duplicate command keys specified.");
		}
		if (keys[i] == ' ')
			strlcpy(key_write[i], "<space>", sizeof key_write[i]);
		else {
			key_write[i][0] = keys[i];
			key_write[i][1] = '\0';
		}
	}

	snprintf(key_msg, sizeof key_msg,
"%s - left   %s - rotate   %s - right   %s - drop   %s - pause   %s - quit",
		key_write[0], key_write[1], key_write[2], key_write[3],
		key_write[4], key_write[5]);

	(void)signal(SIGINT, onintr);
	scr_init();

	if (unveil(scorepath, "rwc") == -1)
		err(1, "unveil %s", scorepath);

	if (pledge("stdio rpath wpath cpath tty", NULL) == -1)
		err(1, "pledge");

	setup_board();

	scr_set();

	pos = A_FIRST*B_COLS + (B_COLS/2)-1;
	nextshape = randshape();
	curshape = randshape();

	scr_msg(key_msg, 1);

	for (;;) {
		place(curshape, pos, 1);
		scr_update();
		place(curshape, pos, 0);
		c = tgetchar();
		if (c < 0) {
			/*
			 * Timeout.  Move down if possible.
			 */
			if (fits_in(curshape, pos + B_COLS)) {
				pos += B_COLS;
				continue;
			}

			/*
			 * Put up the current shape `permanently',
			 * bump score, and elide any full rows.
			 */
			place(curshape, pos, 1);
			score++;
			elide();

			/*
			 * Choose a new shape.  If it does not fit,
			 * the game is over.
			 */
			curshape = nextshape;
			nextshape = randshape();
			pos = A_FIRST*B_COLS + (B_COLS/2)-1;
			if (!fits_in(curshape, pos))
				break;
			continue;
		}

		/*
		 * Handle command keys.
		 */
		if (c == keys[5]) {
			/* quit */
			break;
		}
		if (c == keys[4]) {
			static char msg[] =
			    "paused - press RETURN to continue";

			place(curshape, pos, 1);
			do {
				scr_update();
				scr_msg(key_msg, 0);
				scr_msg(msg, 1);
				(void) fflush(stdout);
			} while (rwait(NULL) == -1);
			scr_msg(msg, 0);
			scr_msg(key_msg, 1);
			place(curshape, pos, 0);
			continue;
		}
		if (c == keys[0]) {
			/* move left */
			if (fits_in(curshape, pos - 1))
				pos--;
			continue;
		}
		if (c == keys[1]) {
			/* turn */
			const struct shape *new = &shapes[
			    classic? curshape->rotc : curshape->rot];

			if (fits_in(new, pos))
				curshape = new;
			continue;
		}
		if (c == keys[2]) {
			/* move right */
			if (fits_in(curshape, pos + 1))
				pos++;
			continue;
		}
		if (c == keys[3]) {
			/* move to bottom */
			while (fits_in(curshape, pos + B_COLS)) {
				pos += B_COLS;
				score++;
			}
			continue;
		}
		if (c == '\f') {
			scr_clear();
			scr_msg(key_msg, 1);
		}
	}

	scr_clear();
	scr_end();

	if (showpreview == 0)
		(void)printf("Your score:  %d point%s  x  level %d  =  %d\n",
		    score, score == 1 ? "" : "s", level, score * level);
	else {
		(void)printf("Your score:  %d point%s x level %d x preview penalty %0.3f = %d\n",
		    score, score == 1 ? "" : "s", level, (double)PRE_PENALTY,
		    (int)(score * level * PRE_PENALTY));
		score = score * PRE_PENALTY;
	}
	savescore(level);

	printf("\nHit RETURN to see high scores, ^C to skip.\n");

	while ((i = getchar()) != '\n')
		if (i == EOF)
			break;

	showscores(level);

	return 0;
}

void
onintr(int signo)
{
	scr_clear();		/* XXX signal race */
	scr_end();		/* XXX signal race */
	_exit(0);
}

void
usage(void)
{
	(void)fprintf(stderr, "usage: %s [-cps] [-k keys] "
	    "[-l level]\n", getprogname());
	exit(1);
}
