/*	$OpenBSD: display.c,v 1.8 2016/01/07 21:37:53 mestre Exp $	*/

/*
 * Display abstraction.
 * David Leonard <d@openbsd.org>, 1999. Public domain.
 */

#include <curses.h>

void
display_open(void)
{
        initscr();
        (void) noecho();
        (void) cbreak();
}

void
display_beep(void)
{
	beep();
}

void
display_refresh(void)
{
	refresh();
}

void
display_clear_eol(void)
{
	clrtoeol();
}

void
display_put_ch(char c)
{
	addch(c);
}

void
display_put_str(char *s)
{
	addstr(s);
}

void
display_clear_the_screen(void)
{
        clear();
        move(0, 0);
        display_refresh();
}

void
display_move(int y, int x)
{
	move(y, x);
}

void
display_getyx(int *yp, int *xp)
{
	getyx(stdscr, *yp, *xp);
}

void
display_end(void)
{
	endwin();
}

char
display_atyx(int y, int x)
{
	int oy, ox;
	char c;

	display_getyx(&oy, &ox);
	c = mvwinch(stdscr, y, x) & 0x7f;
	display_move(oy, ox);
	return (c);
}

void
display_redraw_screen(void)
{
	clearok(stdscr, TRUE);
	touchwin(stdscr);
}

int
display_iserasechar(char ch)
{
	return ch == erasechar();
}

int
display_iskillchar(char ch)
{
	return ch == killchar();
}
