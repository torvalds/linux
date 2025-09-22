/*	$OpenBSD: bdisp.c,v 1.13 2016/01/08 21:38:33 mestre Exp $	*/
/*
 * Copyright (c) 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ralph Campbell.
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
#include <string.h>

#include "gomoku.h"

#define	SCRNH		24		/* assume 24 lines for the moment */
#define	SCRNW		80		/* assume 80 chars for the moment */

static	int	lastline;
static	char	pcolor[] = "*O.?";

/*
 * Initialize screen display.
 */
void
cursinit(void)
{
	initscr();
	if ((LINES < SCRNH) || (COLS < SCRNW)) {
		endwin();
		errx(1,"Screen too small (need %dx%d)",SCRNW,SCRNH);
	}
#ifdef KEY_MIN
	keypad(stdscr, TRUE);
#endif /* KEY_MIN */
	nonl();
	noecho();
	cbreak();

#ifdef NCURSES_MOUSE_VERSION
	mousemask(BUTTON1_CLICKED, (mmask_t *)NULL);
#endif /* NCURSES_MOUSE_VERSION*/
}

/*
 * Restore screen display.
 */
void
cursfini(void)
{
	move(BSZ4, 0);
	clrtoeol();
	refresh();
	echo();
	endwin();
}

/*
 * Initialize board display.
 */
void
bdisp_init(void)
{
	int i, j;

	/* top border */
	for (i = 1; i < BSZ1; i++) {
		move(0, 2 * i + 1);
		addch(letters[i]);
	}
	/* left and right edges */
	for (j = BSZ1; --j > 0; ) {
		move(20 - j, 0);
		printw("%2d ", j);
		move(20 - j, 2 * BSZ1 + 1);
		printw("%d ", j);
	}
	/* bottom border */
	for (i = 1; i < BSZ1; i++) {
		move(20, 2 * i + 1);
		addch(letters[i]);
	}
	bdwho(0);
	move(0, 47);
	addstr("#  black  white");
	lastline = 0;
	bdisp();
}

/*
 * Update who is playing whom.
 */
void
bdwho(int update)
{
	int i, j;
	extern char *plyr[];

	move(21, 0);
	printw("                                              ");
	i = strlen(plyr[BLACK]);
	j = strlen(plyr[WHITE]);
	if (i + j <= 20) {
		move(21, 10 - (i + j)/2);
		printw("BLACK/%s (*) vs. WHITE/%s (O)",
		    plyr[BLACK], plyr[WHITE]);
	} else {
		move(21, 0);
		if (i <= 10)
			j = 20 - i;
		else if (j <= 10)
			i = 20 - j;
		else
			i = j = 10;
		printw("BLACK/%.*s (*) vs. WHITE/%.*s (O)",
		    i, plyr[BLACK], j, plyr[WHITE]);
	}
	if (update)
		refresh();
}

/*
 * Update the board display after a move.
 */
void
bdisp(void)
{
	int i, j, c;
	struct spotstr *sp;

	for (j = BSZ1; --j > 0; ) {
		for (i = 1; i < BSZ1; i++) {
			move(BSZ1 - j, 2 * i + 1);
			sp = &board[i + j * BSZ1];
			if (debug > 1 && sp->s_occ == EMPTY) {
				if (sp->s_flg & IFLAGALL)
					c = '+';
				else if (sp->s_flg & CFLAGALL)
					c = '-';
				else
					c = '.';
			} else
				c = pcolor[sp->s_occ];
			addch(c);
		}
	}
	refresh();
}

#ifdef DEBUG
/*
 * Dump board display to a file.
 */
void bdump(FILE *fp)
{
	int i, j, c;
	struct spotstr *sp;

	/* top border */
	fprintf(fp, "   A B C D E F G H J K L M N O P Q R S T\n");

	for (j = BSZ1; --j > 0; ) {
		/* left edge */
		fprintf(fp, "%2d ", j);
		for (i = 1; i < BSZ1; i++) {
			sp = &board[i + j * BSZ1];
			if (debug > 1 && sp->s_occ == EMPTY) {
				if (sp->s_flg & IFLAGALL)
					c = '+';
				else if (sp->s_flg & CFLAGALL)
					c = '-';
				else
					c = '.';
			} else
				c = pcolor[sp->s_occ];
			putc(c, fp);
			putc(' ', fp);
		}
		/* right edge */
		fprintf(fp, "%d\n", j);
	}

	/* bottom border */
	fprintf(fp, "   A B C D E F G H J K L M N O P Q R S T\n");
}
#endif /* DEBUG */

/*
 * Display a transcript entry
 */
void
dislog(char *str)
{

	if (++lastline >= SCRNH - 1) {
		/* move 'em up */
		lastline = 1;
	}
	if (strlen(str) >= SCRNW - (2 * BSZ4))
		str[SCRNW - (2 * BSZ4) - 1] = '\0';
	move(lastline, (2 * BSZ4));
	addstr(str);
	clrtoeol();
	move(lastline + 1, (2 * BSZ4));
	clrtoeol();
}

/*
 * Display a question.
 */
void
ask(char *str)
{
	int len = strlen(str);

	move(BSZ4, 0);
	addstr(str);
	clrtoeol();
	move(BSZ4, len);
	refresh();
}

int
get_line(char *buf, int size)
{
	char *cp, *end;
	int c = EOF;
	extern int interactive;

	cp = buf;
	end = buf + size - 1;	/* save room for the '\0' */
	while (cp < end && (c = getchar()) != EOF && c != '\n' && c != '\r') {
		*cp++ = c;
		if (interactive) {
			switch (c) {
			case 0x0c: /* ^L */
				wrefresh(curscr);
				cp--;
				continue;
			case 0x15: /* ^U */
			case 0x18: /* ^X */
				while (cp > buf) {
					cp--;
					addch('\b');
				}
				clrtoeol();
				break;
			case '\b':
			case 0x7f: /* DEL */
				if (cp == buf + 1) {
					cp--;
					continue;
				}
				cp -= 2;
				addch('\b');
				c = ' ';
				/* FALLTHROUGH */
			default:
				addch(c);
			}
			refresh();
		}
	}
	*cp = '\0';
	return(c != EOF);
}


/* Decent (n)curses interface for the game, based on Eric S. Raymond's
 * modifications to the battleship (bs) user interface.
 */
int getcoord(void)
{
	static int curx = BSZ / 2;
	static int cury = BSZ / 2;
	int ny, nx, c;

	BGOTO(cury,curx);
	refresh();
	nx = curx; ny = cury;
	for (;;) {
		mvprintw(BSZ3, (BSZ -6)/2, "(%c %d)", 
				'A'+ ((curx > 7) ? (curx+1) : curx), cury + 1);
		BGOTO(cury, curx);

		switch(c = getch()) {
		case 'k': case '8':
#ifdef KEY_MIN
		case KEY_UP:
#endif /* KEY_MIN */
			ny = cury + 1;       nx = curx;
			break;
		case 'j': case '2':
#ifdef KEY_MIN
		case KEY_DOWN:
#endif /* KEY_MIN */
			ny = BSZ + cury - 1; nx = curx;
			break;
		case 'h': case '4':
#ifdef KEY_MIN
		case KEY_LEFT:
#endif /* KEY_MIN */
			ny = cury;          nx = BSZ + curx - 1;
			break;
		case 'l': case '6':
#ifdef KEY_MIN
		case KEY_RIGHT:
#endif /* KEY_MIN */
			ny = cury;          nx = curx + 1;
			break;
		case 'y': case '7':
#ifdef KEY_MIN
		case KEY_A1:
#endif /* KEY_MIN */
			ny = cury + 1;        nx = BSZ + curx - 1;
			break;
		case 'b': case '1':
#ifdef KEY_MIN
		case KEY_C1:
#endif /* KEY_MIN */
			ny = BSZ + cury - 1; nx = BSZ + curx - 1;
			break;
		case 'u': case '9':
#ifdef KEY_MIN
		case KEY_A3:
#endif /* KEY_MIN */
			ny = cury + 1;        nx = curx + 1;
			break;
		case 'n': case '3':
#ifdef KEY_MIN
		case KEY_C3:
#endif /* KEY_MIN */
			ny = BSZ + cury - 1; nx = curx + 1;
			break;
		case 'K':
			ny = cury + 5;       nx = curx;
			break;
		case 'J':
			ny = BSZ + cury - 5; nx = curx;
			break;
		case 'H':
			ny = cury;          nx = BSZ + curx - 5;
			break;
		case 'L':
			ny = cury;          nx = curx + 5;
			break;
		case 'Y':
			ny = cury + 5;        nx = BSZ + curx - 5;
			break;
		case 'B':
			ny = BSZ + cury - 5; nx = BSZ + curx - 5;
			break;
		case 'U':
			ny = cury + 5;        nx = curx + 5;
			break;
		case 'N':
			ny = BSZ + cury - 5; nx = curx + 5;
			break;
		case FF:
			nx = curx; ny = cury;
			(void)clearok(stdscr, TRUE);
			(void)refresh();
			break;
#ifdef NCURSES_MOUSE_VERSION
		case KEY_MOUSE:
		{
			MEVENT	myevent;

			getmouse(&myevent);
			if (myevent.y >= 1 && myevent.y <= BSZ1
				&& myevent.x >= 3 && myevent.x <= (2 * BSZ + 1))
			{
				curx = (myevent.x - 3) / 2;
				cury = BSZ - myevent.y;
				return(PT(curx,cury));
			}
			else
				beep();
		}
		break;
#endif /* NCURSES_MOUSE_VERSION */
		case 'Q':
			return(RESIGN);
			break;
		case 'S':
			return(SAVE);
			break;
		case ' ':
		case '\015':  /* return */
			(void) mvaddstr(BSZ3, (BSZ -6)/2, "      ");
			return(PT(curx+1,cury+1));
			break;
	}

	curx = nx % BSZ;
	cury = ny % BSZ;
    }
}
