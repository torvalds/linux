/*	$OpenBSD: move.c,v 1.13 2016/08/27 02:02:44 guenther Exp $	*/
/*	$NetBSD: move.c,v 1.4 1995/04/22 10:08:58 cgd Exp $	*/

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

#include <sys/time.h>
#include <ctype.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include "robots.h"

#define	ESC	'\033'

/*
 * get_move:
 *	Get and execute a move from the player
 */
void
get_move(void)
{
	int	c;
	int retval;
	struct timespec t, tn;
#ifdef FANCY
	int lastmove;
#endif

	if (Waiting)
		return;

#ifdef	FANCY
	if (Pattern_roll) {
		if (Next_move >= Move_list)
			lastmove = *Next_move;
		else
			lastmove = -1;	/* flag for "first time in" */
	}
#endif
	if (Real_time) {
		t = tv;
		clock_gettime(CLOCK_MONOTONIC, &tn);
	}
	for (;;) {
		if (Teleport && must_telep())
			goto teleport;
		if (Running)
			c = Run_ch;
		else if (Count != 0)
			c = Cnt_move;
#ifdef	FANCY
		else if (Num_robots > 1 && Stand_still)
			c = '>';
		else if (Num_robots > 1 && Pattern_roll) {
			if (*++Next_move == '\0') {
				if (lastmove < 0)
					goto over;
				Next_move = Move_list;
			}
			c = *Next_move;
			mvaddch(0, 0, c);
			if (c == lastmove)
				goto over;
		}
#endif
		else {
over:
			if (Real_time) {
				struct pollfd pfd[1];

				pfd[0].fd = STDIN_FILENO;
				pfd[0].events = POLLIN;
				retval = ppoll(pfd, 1, &t, NULL);
				if (retval > 0)
					c = getchar();
				else	/* Don't move if timed out or error */
					c = ' ';
			} else {
				c = getchar();
				/* Can't use digits in real time mode, or digit/ESC
				 * is an effective way to stop the game.
				 */
				if (isdigit(c)) {
					Count = (c - '0');
					while (isdigit(c = getchar()))
						Count = Count * 10 + (c - '0');
					if (c == ESC)
						goto over;
					Cnt_move = c;
					if (Count)
						leaveok(stdscr, TRUE);
				}
			}
		}

		switch (c) {
		  case ' ':
		  case '.':
			if (do_move(0, 0))
				goto ret;
			break;
		  case 'y':
			if (do_move(-1, -1))
				goto ret;
			break;
		  case 'k':
			if (do_move(-1, 0))
				goto ret;
			break;
		  case 'u':
			if (do_move(-1, 1))
				goto ret;
			break;
		  case 'h':
			if (do_move(0, -1))
				goto ret;
			break;
		  case 'l':
			if (do_move(0, 1))
				goto ret;
			break;
		  case 'b':
			if (do_move(1, -1))
				goto ret;
			break;
		  case 'j':
			if (do_move(1, 0))
				goto ret;
			break;
		  case 'n':
			if (do_move(1, 1))
				goto ret;
			break;
		  case 'Y': case 'U': case 'H': case 'J':
		  case 'K': case 'L': case 'B': case 'N':
		  case '>':
			Running = TRUE;
			if (c == '>')
				Run_ch = ' ';
			else
				Run_ch = tolower(c);
			leaveok(stdscr, TRUE);
			break;
		  case 'q':
		  case 'Q':
			if (query("Really quit?"))
				quit(0);
			refresh();
			break;
		  case 'w':
		  case 'W':
			Waiting = TRUE;
			leaveok(stdscr, TRUE);
#ifndef NCURSES_VERSION
			flushok(stdscr, FALSE);
#endif
			goto ret;
		  case 't':
		  case 'T':
teleport:
			Running = FALSE;
			mvaddch(My_pos.y, My_pos.x, ' ');
			My_pos = *rnd_pos();
			mvaddch(My_pos.y, My_pos.x, PLAYER);
			leaveok(stdscr, FALSE);
			refresh();
			flushinp();
			goto ret;
		  case CTRL('L'):
			wrefresh(curscr);
			break;
		  case EOF:
			quit(0);
			break;
		  default:
			beep();
			reset_count();
			break;
		}
		if (Real_time) {
			/* Update current time. */
			clock_gettime(CLOCK_MONOTONIC, &t);

			/* Check whether tv time has passed. */
			timespecadd(&tn, &tv, &tn);
			if (timespeccmp(&tn, &t, <))
				goto ret;

			/* Keep the difference otherwise. */
			timespecsub(&tn, &t, &t);
		}
	}
ret:
	if (Count > 0)
		if (--Count == 0)
			leaveok(stdscr, FALSE);
}

/*
 * must_telep:
 *	Must I teleport; i.e., is there anywhere I can move without
 * being eaten?
 */
bool
must_telep(void)
{
	int		x, y;
	static COORD	newpos;

#ifdef	FANCY
	if (Stand_still && Num_robots > 1 && eaten(&My_pos))
		return TRUE;
#endif

	for (y = -1; y <= 1; y++) {
		newpos.y = My_pos.y + y;
		if (newpos.y <= 0 || newpos.y >= Y_FIELDSIZE)
			continue;
		for (x = -1; x <= 1; x++) {
			newpos.x = My_pos.x + x;
			if (newpos.x <= 0 || newpos.x >= X_FIELDSIZE)
				continue;
			if (Field[newpos.y][newpos.x] > 0)
				continue;
			if (!eaten(&newpos))
				return FALSE;
		}
	}
	return TRUE;
}

/*
 * do_move:
 *	Execute a move
 */
bool
do_move(int dy, int dx)
{
	static COORD	newpos;

	newpos.y = My_pos.y + dy;
	newpos.x = My_pos.x + dx;
	if (newpos.y <= 0 || newpos.y >= Y_FIELDSIZE ||
	    newpos.x <= 0 || newpos.x >= X_FIELDSIZE ||
	    Field[newpos.y][newpos.x] > 0 || eaten(&newpos)) {
		if (Running) {
			Running = FALSE;
			leaveok(stdscr, FALSE);
			move(My_pos.y, My_pos.x);
			refresh();
		} else {
			beep();
			reset_count();
		}
		return FALSE;
	}
	else if (dy == 0 && dx == 0)
		return TRUE;
	mvaddch(My_pos.y, My_pos.x, ' ');
	My_pos = newpos;
	mvaddch(My_pos.y, My_pos.x, PLAYER);
	if (!jumping())
		refresh();
	return TRUE;
}

/*
 * eaten:
 *	Player would get eaten at this place
 */
bool
eaten(COORD *pos)
{
	int	x, y;

	for (y = pos->y - 1; y <= pos->y + 1; y++) {
		if (y <= 0 || y >= Y_FIELDSIZE)
			continue;
		for (x = pos->x - 1; x <= pos->x + 1; x++) {
			if (x <= 0 || x >= X_FIELDSIZE)
				continue;
			if (Field[y][x] == 1)
				return TRUE;
		}
	}
	return FALSE;
}

/*
 * reset_count:
 *	Reset the count variables
 */
void
reset_count(void)
{
	Count = 0;
	Running = FALSE;
	leaveok(stdscr, FALSE);
	refresh();
}

/*
 * jumping:
 *	See if we are jumping, i.e., we should not refresh.
 */
bool
jumping(void)
{
	return (Jump && (Count || Running || Waiting));
}
