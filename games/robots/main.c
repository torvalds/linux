/*	$OpenBSD: main.c,v 1.31 2022/12/04 23:50:45 cheloha Exp $	*/
/*	$NetBSD: main.c,v 1.5 1995/04/22 10:08:54 cgd Exp $	*/

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
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "robots.h"

void
usage(void)
{
	fprintf(stderr, "usage: %s [-ajrst] [scorefile]\n", getprogname());
	exit(1);
}

int
main(int ac, char *av[])
{
	bool		show_only;
	extern char	Scorefile[PATH_MAX];
	int		score_wfd;     /* high score writable file descriptor */
	int		score_err = 0; /* hold errno from score file open */
	int		ch;
	int		ret;
	char		*home;
#ifdef FANCY
	char		*sp;
#endif

	home = getenv("HOME");
	if (home == NULL || *home == '\0')
		err(1, "getenv");

	ret = snprintf(Scorefile, sizeof(Scorefile), "%s/%s", home,
	    ".robots.scores");
	if (ret < 0 || ret >= PATH_MAX)
		errc(1, ENAMETOOLONG, "%s/%s", home, ".robots.scores");

	if ((score_wfd = open(Scorefile, O_RDWR | O_CREAT, 0666)) == -1)
		score_err = errno;

	show_only = FALSE;
	while ((ch = getopt(ac, av, "srajt")) != -1)
		switch (ch) {
		case 's':
			show_only = TRUE;
			break;
		case 'r':
			Real_time = TRUE;
			/* Could be a command-line option */
			tv.tv_sec = 3;
			break;
		case 'a':
			Start_level = 4;
			break;
		case 'j':
			Jump = TRUE;
			break;
		case 't':
			Teleport = TRUE;
			break;
		default:
			usage();
		}
	ac -= optind;
	av += optind;

	if (ac > 1)
		usage();
	if (ac == 1) {
		if (strlcpy(Scorefile, av[0], sizeof(Scorefile)) >=
		    sizeof(Scorefile))
			errc(1, ENAMETOOLONG, "%s", av[0]);
		if (score_wfd >= 0)
			close(score_wfd);
		/* This file requires no special privileges. */
		if ((score_wfd = open(Scorefile, O_RDWR | O_CREAT, 0666)) == -1)
			score_err = errno;
#ifdef	FANCY
		sp = strrchr(Scorefile, '/');
		if (sp == NULL)
			sp = Scorefile;
		if (strcmp(sp, "pattern_roll") == 0)
			Pattern_roll = TRUE;
		else if (strcmp(sp, "stand_still") == 0)
			Stand_still = TRUE;
		if (Pattern_roll || Stand_still)
			Teleport = TRUE;
#endif
	}

	if (show_only) {
		show_score();
		return 0;
	}

	if (score_wfd < 0) {
		warnx("%s: %s; no scores will be saved", Scorefile,
			strerror(score_err));
		sleep(1);
	}

	initscr();

	if (pledge("stdio tty", NULL) == -1)
		err(1, "pledge");

	signal(SIGINT, quit);
	cbreak();
	noecho();
	nonl();
	if (LINES != Y_SIZE || COLS != X_SIZE) {
		if (LINES < Y_SIZE || COLS < X_SIZE) {
			endwin();
			errx(1, "Need at least a %dx%d screen", Y_SIZE, X_SIZE);
		}
		delwin(stdscr);
		stdscr = newwin(Y_SIZE, X_SIZE, 0, 0);
	}

	do {
		init_field();
		for (Level = Start_level; !Dead; Level++) {
			make_level();
			play_level();
		}
		if (My_pos.x > X_FIELDSIZE - 16)
			move(My_pos.y, X_FIELDSIZE - 16);
		else
			move(My_pos.y, My_pos.x);
		printw("AARRrrgghhhh....");
		refresh();
		score(score_wfd);
	} while (another());
	quit(0);
}

/*
 * quit:
 *	Leave the program elegantly.
 */
void
quit(int dummy)
{
	endwin();
	exit(0);
}

/*
 * another:
 *	See if another game is desired
 */
bool
another(void)
{
	int	y;

#ifdef	FANCY
	if ((Stand_still || Pattern_roll) && !Newscore)
		return TRUE;
#endif

	if (query("Another game?")) {
		if (Full_clear) {
			for (y = 1; y <= Num_scores; y++) {
				move(y, 1);
				clrtoeol();
			}
			refresh();
		}
		return TRUE;
	}
	return FALSE;
}
