/*
 *  Top users/processes display for Unix
 *  Version 3
 *
 *  This program may be freely redistributed,
 *  but this entire comment MUST remain intact.
 *
 *  Copyright (c) 1984, 1989, William LeFebvre, Rice University
 *  Copyright (c) 1989, 1990, 1992, William LeFebvre, Northwestern University
 *
 * $FreeBSD$
 */

/*  This file contains the routines that interface to termcap and stty/gtty.
 *
 *  Paul Vixie, February 1987: converted to use ioctl() instead of stty/gtty.
 *
 *  I put in code to turn on the TOSTOP bit while top was running, but I
 *  didn't really like the results.  If you desire it, turn on the
 *  preprocessor variable "TOStop".   --wnl
 */

#include <sys/ioctl.h>

#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <curses.h>
#include <termcap.h>
#include <unistd.h>

#include "screen.h"
#include "top.h"

int  overstrike;
int  screen_length;
int  screen_width;
char ch_erase;
char ch_kill;
char smart_terminal;
static char termcap_buf[1024];
static char string_buffer[1024];
static char home[15];
static char lower_left[15];
char *clear_line;
static char *clear_screen;
char *clear_to_end;
char *cursor_motion;
static char *start_standout;
static char *end_standout;
static char *terminal_init;
static char *terminal_end;

static struct termios old_settings;
static struct termios new_settings;
static char is_a_terminal = false;

#define NON_INTERACTIVE_MODE_VIRTUAL_SCREEN_WIDTH 1024

void
init_termcap(bool interactive)
{
    char *bufptr;
    char *PCptr;
    char *term_name;
    int status;

    screen_width = 0;
    screen_length = 0;

    if (!interactive)
    {
	/* pretend we have a dumb terminal */
	screen_width = NON_INTERACTIVE_MODE_VIRTUAL_SCREEN_WIDTH;
	smart_terminal = false;
	return;
    }

    /* assume we have a smart terminal until proven otherwise */
    smart_terminal = true;

    /* get the terminal name */
    term_name = getenv("TERM");

    /* if there is no TERM, assume it's a dumb terminal */
    /* patch courtesy of Sam Horrocks at telegraph.ics.uci.edu */
    if (term_name == NULL)
    {
	smart_terminal = false;
	return;
    }

    /* now get the termcap entry */
    if ((status = tgetent(termcap_buf, term_name)) != 1)
    {
	if (status == -1)
	{
	    warnx("can't open termcap file");
	}
	else
	{
	    warnx("no termcap entry for a `%s' terminal", term_name);
	}

	/* pretend it's dumb and proceed */
	smart_terminal = false;
	return;
    }

    /* "hardcopy" immediately indicates a very stupid terminal */
    if (tgetflag("hc"))
    {
	smart_terminal = false;
	return;
    }

    /* set up common terminal capabilities */
    if ((screen_length = tgetnum("li")) <= 0)
    {
	screen_length = smart_terminal = 0;
	return;
    }

    /* screen_width is a little different */
    if ((screen_width = tgetnum("co")) == -1)
    {
	screen_width = 79;
    }
    else
    {
	screen_width -= 1;
    }

    /* terminals that overstrike need special attention */
    overstrike = tgetflag("os");

    /* initialize the pointer into the termcap string buffer */
    bufptr = string_buffer;

    /* get "ce", clear to end */
    if (!overstrike)
    {
		clear_line = tgetstr("ce", &bufptr);
    }

    /* get necessary capabilities */
    if ((clear_screen  = tgetstr("cl", &bufptr)) == NULL ||
	(cursor_motion = tgetstr("cm", &bufptr)) == NULL)
    {
	smart_terminal = false;
	return;
    }

    /* get some more sophisticated stuff -- these are optional */
    clear_to_end   = tgetstr("cd", &bufptr);
    terminal_init  = tgetstr("ti", &bufptr);
    terminal_end   = tgetstr("te", &bufptr);
    start_standout = tgetstr("so", &bufptr);
    end_standout   = tgetstr("se", &bufptr);

    /* pad character */
    PC = (PCptr = tgetstr("pc", &bufptr)) ? *PCptr : 0;

    /* set convenience strings */
    strncpy(home, tgoto(cursor_motion, 0, 0), sizeof(home) - 1);
    home[sizeof(home) - 1] = '\0';
    /* (lower_left is set in get_screensize) */

    /* get the actual screen size with an ioctl, if needed */
    /* This may change screen_width and screen_length, and it always
       sets lower_left. */
    get_screensize();

    /* if stdout is not a terminal, pretend we are a dumb terminal */
    if (tcgetattr(STDOUT_FILENO, &old_settings) == -1)
    {
	smart_terminal = false;
    }
}

void
init_screen(void)
{
    /* get the old settings for safe keeping */
    if (tcgetattr(STDOUT_FILENO, &old_settings) != -1)
    {
	/* copy the settings so we can modify them */
	new_settings = old_settings;

	/* turn off ICANON, character echo and tab expansion */
	new_settings.c_lflag &= ~(ICANON|ECHO);
	new_settings.c_oflag &= ~(TAB3);
	new_settings.c_cc[VMIN] = 1;
	new_settings.c_cc[VTIME] = 0;
	tcsetattr(STDOUT_FILENO, TCSADRAIN, &new_settings);

	/* remember the erase and kill characters */
	ch_erase = old_settings.c_cc[VERASE];
	ch_kill  = old_settings.c_cc[VKILL];

	/* remember that it really is a terminal */
	is_a_terminal = true;

	/* send the termcap initialization string */
	putcap(terminal_init);
    }

    if (!is_a_terminal)
    {
	/* not a terminal at all---consider it dumb */
	smart_terminal = false;
    }
}

void
end_screen(void)
{
    /* move to the lower left, clear the line and send "te" */
    if (smart_terminal)
    {
	putcap(lower_left);
	putcap(clear_line);
	fflush(stdout);
	putcap(terminal_end);
    }

    /* if we have settings to reset, then do so */
    if (is_a_terminal)
    {
	tcsetattr(STDOUT_FILENO, TCSADRAIN, &old_settings);
    }
}

void
reinit_screen(void)
{
    /* install our settings if it is a terminal */
    if (is_a_terminal)
    {
	tcsetattr(STDOUT_FILENO, TCSADRAIN, &new_settings);
    }

    /* send init string */
    if (smart_terminal)
    {
	putcap(terminal_init);
    }
}

void
get_screensize(void)
{
    struct winsize ws;

    if (ioctl (1, TIOCGWINSZ, &ws) != -1)
    {
	if (ws.ws_row != 0)
	{
	    screen_length = ws.ws_row;
	}
	if (ws.ws_col != 0)
	{
	    screen_width = ws.ws_col - 1;
	}
    }


    (void) strncpy(lower_left, tgoto(cursor_motion, 0, screen_length - 1),
	sizeof(lower_left) - 1);
    lower_left[sizeof(lower_left) - 1] = '\0';
}

void
top_standout(const char *msg)
{
    if (smart_terminal)
    {
	putcap(start_standout);
	fputs(msg, stdout);
	putcap(end_standout);
    }
    else
    {
	fputs(msg, stdout);
    }
}

void
top_clear(void)
{
    if (smart_terminal)
    {
	putcap(clear_screen);
    }
}

int
clear_eol(int len)
{
    if (smart_terminal && !overstrike && len > 0)
    {
	if (clear_line)
	{
	    putcap(clear_line);
	    return(0);
	}
	else
	{
	    while (len-- > 0)
	    {
		putchar(' ');
	    }
	    return(1);
	}
    }
    return(-1);
}
