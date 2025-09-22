/*	$OpenBSD: pl_7.c,v 1.13 2016/01/08 20:26:33 mestre Exp $	*/
/*	$NetBSD: pl_7.c,v 1.6 1995/04/22 10:37:17 cgd Exp $	*/

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

#include <err.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"
#include "machdep.h"
#include "player.h"

/*
 * Display interface
 */

static char sc_hasprompt;
static const char *sc_prompt;
static const char *sc_buf;
static int sc_line;

WINDOW *view_w;
WINDOW *slot_w;
WINDOW *scroll_w;
WINDOW *stat_w;
WINDOW *turn_w;

char done_curses;
char loaded, fired, changed, repaired;
char dont_adjust;
int viewrow, viewcol;
char movebuf[sizeof SHIP(0)->file->movebuf];
int player;
struct ship *ms;		/* memorial structure, &cc->ship[player] */
struct File *mf;		/* ms->file */
struct shipspecs *mc;		/* ms->specs */

void
initscreen(void)
{
	if (!SCREENTEST())
		errx(1, "can't sail on this terminal.");
	/* initscr() already done in SCREENTEST() */
	view_w = newwin(VIEW_Y, VIEW_X, VIEW_T, VIEW_L);
	slot_w = newwin(SLOT_Y, SLOT_X, SLOT_T, SLOT_L);
	scroll_w = newwin(SCROLL_Y, SCROLL_X, SCROLL_T, SCROLL_L);
	stat_w = newwin(STAT_Y, STAT_X, STAT_T, STAT_L);
	turn_w = newwin(TURN_Y, TURN_X, TURN_T, TURN_L);
	done_curses++;
	(void) leaveok(view_w, 1);
	(void) leaveok(slot_w, 1);
	(void) leaveok(stat_w, 1);
	(void) leaveok(turn_w, 1);
	noecho();
	cbreak();
}

void
cleanupscreen(void)
{
	/* alarm already turned off */
	if (done_curses) {
		(void) wmove(scroll_w, SCROLL_Y - 1, 0);
		(void) wclrtoeol(scroll_w);
		draw_screen();
		endwin();
	}
}

void
newturn(int n)
{
	repaired = loaded = fired = changed = 0;
	movebuf[0] = '\0';

	(void) alarm(0);
	if (mf->readyL & R_LOADING) {
		if (mf->readyL & R_DOUBLE)
			mf->readyL = R_LOADING;
		else
			mf->readyL = R_LOADED;
	}
	if (mf->readyR & R_LOADING) {
		if (mf->readyR & R_DOUBLE)
			mf->readyR = R_LOADING;
		else
			mf->readyR = R_LOADED;
	}
	if (!hasdriver)
		Write(W_DDEAD, SHIP(0), 0, 0, 0, 0);

	if (sc_hasprompt) {
		(void) wmove(scroll_w, sc_line, 0);
		(void) wclrtoeol(scroll_w);
	}
	if (Sync() < 0)
		leave(LEAVE_SYNC);
	if (!hasdriver)
		leave(LEAVE_DRIVER);
	if (sc_hasprompt)
		(void) wprintw(scroll_w, "%s%s", sc_prompt, sc_buf);

	if (turn % 50 == 0)
		Write(W_ALIVE, SHIP(0), 0, 0, 0, 0);
	if (mf->FS && (!mc->rig1 || windspeed == 6))
		Write(W_FS, ms, 0, 0, 0, 0);
	if (mf->FS == 1)
		Write(W_FS, ms, 2, 0, 0, 0);

	if (mf->struck)
		leave(LEAVE_QUIT);
	if (mf->captured != 0)
		leave(LEAVE_CAPTURED);
	if (windspeed == 7)
		leave(LEAVE_HURRICAN);

	adjustview();
	draw_screen();

	(void) signal(SIGALRM, newturn);
	(void) alarm(7);
}

void
Signal(char *fmt, struct ship *ship, ...)
{
	va_list ap;
	char format[BUFSIZ];

	va_start(ap, ship);
	if (!done_curses) {
		va_end(ap);
		return;
	}
	if (*fmt == '\7')
		putchar(*fmt++);
	fmtship(format, sizeof(format), fmt, ship);
	(void) vw_printw(scroll_w, format, ap);
	va_end(ap);
	Scroll();
}

void
Msg(char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);

	if (!done_curses) {
		va_end(ap);
		return;
	}
	if (*fmt == '\7')
		putchar(*fmt++);
	(void) vw_printw(scroll_w, fmt, ap);
	va_end(ap);
	Scroll();
}

void
Scroll(void)
{
	if (++sc_line >= SCROLL_Y)
		sc_line = 0;
	(void) wmove(scroll_w, sc_line, 0);
	(void) wclrtoeol(scroll_w);
}

void
prompt(const char *p, struct ship *ship)
{
	static char buf[BUFSIZ];

	fmtship(buf, sizeof(buf), p, ship);
	sc_prompt = buf;
	sc_buf = "";
	sc_hasprompt = 1;
	(void) waddstr(scroll_w, buf);
}

void
endprompt(int flag)
{
	sc_hasprompt = 0;
	if (flag)
		Scroll();
}

int
sgetch(const char *p, struct ship *ship, int flag)
{
	int c;

	prompt(p, ship);
	blockalarm();
	(void) wrefresh(scroll_w);
	unblockalarm();
	while ((c = wgetch(scroll_w)) == EOF)
		;
	if (flag && c >= ' ' && c < 0x7f)
		(void) waddch(scroll_w, c);
	endprompt(flag);
	return c;
}

void
sgetstr(const char *pr, char *buf, int n)
{
	int c;
	char *p = buf;

	prompt(pr, (struct ship *)0);
	sc_buf = buf;
	for (;;) {
		*p = 0;
		blockalarm();
		(void) wrefresh(scroll_w);
		unblockalarm();
		while ((c = wgetch(scroll_w)) == EOF)
			;
		switch (c) {
		case '\n':
		case '\r':
			endprompt(1);
			return;
		case '\b':
			if (p > buf) {
				(void) waddstr(scroll_w, "\b \b");
				p--;
			}
			break;
		default:
			if (c >= ' ' && c < 0x7f && p < buf + n - 1) {
				*p++ = c;
				(void) waddch(scroll_w, c);
			} else
				(void) putchar('\a');
		}
	}
}

void
draw_screen(void)
{
	draw_view();
	draw_turn();
	draw_stat();
	draw_slot();
	(void) wrefresh(scroll_w);		/* move the cursor */
}

void
draw_view(void)
{
	struct ship *sp;

	(void) werase(view_w);
	foreachship(sp) {
		if (sp->file->dir
		    && sp->file->row > viewrow
		    && sp->file->row < viewrow + VIEW_Y
		    && sp->file->col > viewcol
		    && sp->file->col < viewcol + VIEW_X) {
			(void) wmove(view_w, sp->file->row - viewrow,
				sp->file->col - viewcol);
			(void) waddch(view_w, colours(sp));
			(void) wmove(view_w,
				sternrow(sp) - viewrow,
				sterncol(sp) - viewcol);
			(void) waddch(view_w, sterncolour(sp));
		}
	}
	(void) wrefresh(view_w);
}

void
draw_turn(void)
{
	(void) wmove(turn_w, 0, 0);
	(void) wprintw(turn_w, "%cTurn %d", dont_adjust?'*':'-', turn);
	(void) wrefresh(turn_w);
}

void
draw_stat(void)
{
	(void) wmove(stat_w, STAT_1, 0);
	(void) wprintw(stat_w, "Points  %3d\n", mf->points);
	(void) wprintw(stat_w, "Fouls    %2d\n", fouled(ms));
	(void) wprintw(stat_w, "Grapples %2d\n", grappled(ms));

	(void) wmove(stat_w, STAT_2, 0);
	(void) wprintw(stat_w, "    0 %c(%c)\n",
		maxmove(ms, winddir + 3, -1) + '0',
		maxmove(ms, winddir + 3, 1) + '0');
	(void) waddstr(stat_w, "   \\|/\n");
	(void) wprintw(stat_w, "   -^-%c(%c)\n",
		maxmove(ms, winddir + 2, -1) + '0',
		maxmove(ms, winddir + 2, 1) + '0');
	(void) waddstr(stat_w, "   /|\\\n");
	(void) wprintw(stat_w, "    | %c(%c)\n",
		maxmove(ms, winddir + 1, -1) + '0',
		maxmove(ms, winddir + 1, 1) + '0');
	(void) wprintw(stat_w, "   %c(%c)\n",
		maxmove(ms, winddir, -1) + '0',
		maxmove(ms, winddir, 1) + '0');

	(void) wmove(stat_w, STAT_3, 0);
	(void) wprintw(stat_w, "Load  %c%c %c%c\n",
		loadname[mf->loadL], readyname(mf->readyL),
		loadname[mf->loadR], readyname(mf->readyR));
	(void) wprintw(stat_w, "Hull %2d\n", mc->hull);
	(void) wprintw(stat_w, "Crew %2d %2d %2d\n",
		mc->crew1, mc->crew2, mc->crew3);
	(void) wprintw(stat_w, "Guns %2d %2d\n", mc->gunL, mc->gunR);
	(void) wprintw(stat_w, "Carr %2d %2d\n", mc->carL, mc->carR);
	(void) wprintw(stat_w, "Rigg %d %d %d ", mc->rig1, mc->rig2, mc->rig3);
	if (mc->rig4 < 0)
		(void) waddch(stat_w, '-');
	else
		(void) wprintw(stat_w, "%d", mc->rig4);
	(void) wrefresh(stat_w);
}

void
draw_slot(void)
{
	if (!boarding(ms, 0)) {
		(void) mvwaddstr(slot_w, 0, 0, "   ");
		(void) mvwaddstr(slot_w, 1, 0, "   ");
	} else
		(void) mvwaddstr(slot_w, 1, 0, "OBP");
	if (!boarding(ms, 1)) {
		(void) mvwaddstr(slot_w, 2, 0, "   ");
		(void) mvwaddstr(slot_w, 3, 0, "   ");
	} else
		(void) mvwaddstr(slot_w, 3, 0, "DBP");

	(void) wmove(slot_w, SLOT_Y-4, 0);
	if (mf->RH)
		(void) wprintw(slot_w, "%dRH", mf->RH);
	else
		(void) waddstr(slot_w, "   ");
	(void) wmove(slot_w, SLOT_Y-3, 0);
	if (mf->RG)
		(void) wprintw(slot_w, "%dRG", mf->RG);
	else
		(void) waddstr(slot_w, "   ");
	(void) wmove(slot_w, SLOT_Y-2, 0);
	if (mf->RR)
		(void) wprintw(slot_w, "%dRR", mf->RR);
	else
		(void) waddstr(slot_w, "   ");

#define Y	(SLOT_Y/2)
	(void) wmove(slot_w, 7, 1);
	(void) wprintw(slot_w,"%d", windspeed);
	(void) mvwaddch(slot_w, Y, 0, ' ');
	(void) mvwaddch(slot_w, Y, 2, ' ');
	(void) mvwaddch(slot_w, Y-1, 0, ' ');
	(void) mvwaddch(slot_w, Y-1, 1, ' ');
	(void) mvwaddch(slot_w, Y-1, 2, ' ');
	(void) mvwaddch(slot_w, Y+1, 0, ' ');
	(void) mvwaddch(slot_w, Y+1, 1, ' ');
	(void) mvwaddch(slot_w, Y+1, 2, ' ');
	(void) wmove(slot_w, Y - dr[winddir], 1 - dc[winddir]);
	switch (winddir) {
	case 1:
	case 5:
		(void) waddch(slot_w, '|');
		break;
	case 2:
	case 6:
		(void) waddch(slot_w, '/');
		break;
	case 3:
	case 7:
		(void) waddch(slot_w, '-');
		break;
	case 4:
	case 8:
		(void) waddch(slot_w, '\\');
		break;
	}
	(void) mvwaddch(slot_w, Y + dr[winddir], 1 + dc[winddir], '+');
	(void) wrefresh(slot_w);
}

void
draw_board(void)
{
	int n;

	(void) clear();
	(void) werase(view_w);
	(void) werase(slot_w);
	(void) werase(scroll_w);
	(void) werase(stat_w);
	(void) werase(turn_w);

	sc_line = 0;

	(void) move(BOX_T, BOX_L);
	for (n = 0; n < BOX_X; n++)
		(void) addch('-');
	(void) move(BOX_B, BOX_L);
	for (n = 0; n < BOX_X; n++)
		(void) addch('-');
	for (n = BOX_T+1; n < BOX_B; n++) {
		(void) mvaddch(n, BOX_L, '|');
		(void) mvaddch(n, BOX_R, '|');
	}
	(void) mvaddch(BOX_T, BOX_L, '+');
	(void) mvaddch(BOX_T, BOX_R, '+');
	(void) mvaddch(BOX_B, BOX_L, '+');
	(void) mvaddch(BOX_B, BOX_R, '+');
	(void) refresh();

#define WSaIM "Wooden Ships & Iron Men"
	(void) wmove(view_w, 2, (VIEW_X - sizeof WSaIM - 1) / 2);
	(void) waddstr(view_w, WSaIM);
	(void) wmove(view_w, 4, (VIEW_X - strlen(cc->name)) / 2);
	(void) waddstr(view_w, cc->name);
	(void) wrefresh(view_w);

	(void) move(LINE_T, LINE_L);
	(void) printw("Class %d %s (%d guns) '%s' (%c%c)",
		mc->class,
		classname[mc->class],
		mc->guns,
		ms->shipname,
		colours(ms),
		sterncolour(ms));
	(void) refresh();
}

void
centerview(void)
{
	viewrow = mf->row - VIEW_Y / 2;
	viewcol = mf->col - VIEW_X / 2;
}

void
upview(void)
{
	viewrow -= VIEW_Y / 3;
}

void
downview(void)
{
	viewrow += VIEW_Y / 3;
}

void
leftview(void)
{
	viewcol -= VIEW_X / 5;
}

void
rightview(void)
{
	viewcol += VIEW_X / 5;
}

void
adjustview(void)
{
	if (dont_adjust)
		return;
	if (mf->row < viewrow + VIEW_Y/4)
		viewrow = mf->row - (VIEW_Y - VIEW_Y/4);
	else if (mf->row > viewrow + (VIEW_Y - VIEW_Y/4))
		viewrow = mf->row - VIEW_Y/4;
	if (mf->col < viewcol + VIEW_X/8)
		viewcol = mf->col - (VIEW_X - VIEW_X/8);
	else if (mf->col > viewcol + (VIEW_X - VIEW_X/8))
		viewcol = mf->col - VIEW_X/8;
}
