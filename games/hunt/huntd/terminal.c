/*	$OpenBSD: terminal.c,v 1.14 2017/01/21 08:22:57 krw Exp $	*/
/*	$NetBSD: terminal.c,v 1.2 1997/10/10 16:34:05 lukem Exp $	*/
/*
 * Copyright (c) 1983-2003, Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * + Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * + Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * + Neither the name of the University of California, San Francisco nor
 *   the names of its contributors may be used to endorse or promote
 *   products derived from this software without specific prior written
 *   permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/select.h>
#include <err.h>
#include <stdarg.h>
#include <syslog.h>
#include <string.h>

#include "conf.h"
#include "hunt.h"
#include "server.h"

#define	TERM_WIDTH	80	/* Assume terminals are 80-char wide */

/*
 * cgoto:
 *	Move the cursor to the given position on the given player's
 *	terminal.
 */
void
cgoto(PLAYER *pp, int y, int x)
{

	if (pp == ALL_PLAYERS) {
		for (pp = Player; pp < End_player; pp++)
			cgoto(pp, y, x);
		for (pp = Monitor; pp < End_monitor; pp++)
			cgoto(pp, y, x);
		return;
	}

	if (x == pp->p_curx && y == pp->p_cury)
		return;

	sendcom(pp, MOVE, y, x);
	pp->p_cury = y;
	pp->p_curx = x;
}

/*
 * outch:
 *	Put out a single character.
 */
void
outch(PLAYER *pp, char ch)
{

	if (pp == ALL_PLAYERS) {
		for (pp = Player; pp < End_player; pp++)
			outch(pp, ch);
		for (pp = Monitor; pp < End_monitor; pp++)
			outch(pp, ch);
		return;
	}

	if (++pp->p_curx >= TERM_WIDTH) {
		pp->p_curx = 0;
		pp->p_cury++;
	}
	(void) putc(ch, pp->p_output);
}

/*
 * outstr:
 *	Put out a string of the given length.
 */
void
outstr(PLAYER *pp, char *str, int len)
{
	if (pp == ALL_PLAYERS) {
		for (pp = Player; pp < End_player; pp++)
			outstr(pp, str, len);
		for (pp = Monitor; pp < End_monitor; pp++)
			outstr(pp, str, len);
		return;
	}

	pp->p_curx += len;
	pp->p_cury += (pp->p_curx / TERM_WIDTH);
	pp->p_curx %= TERM_WIDTH;
	while (len--)
		(void) putc(*str++, pp->p_output);
}

/*
 * outat:
 *	draw a string at a location on the client.
 *	Cursor doesn't move if the location is invalid
 */
void
outyx(PLAYER *pp, int y, int x, const char *fmt, ...)
{
	va_list ap;
	char buf[BUFSIZ];
	int len;

	va_start(ap, fmt);
	len = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (len == -1)
		len = 0;
	if (len >= (int)sizeof(buf))
		len = sizeof(buf) - 1;
	if (y >= 0 && x >= 0)
		cgoto(pp, y, x);
	if (len > 0)
		outstr(pp, buf, len);
}

/*
 * clrscr:
 *	Clear the screen, and reset the current position on the screen.
 */
void
clrscr(PLAYER *pp)
{

	if (pp == ALL_PLAYERS) {
		for (pp = Player; pp < End_player; pp++)
			clrscr(pp);
		for (pp = Monitor; pp < End_monitor; pp++)
			clrscr(pp);
		return;
	}

	sendcom(pp, CLEAR);
	pp->p_cury = 0;
	pp->p_curx = 0;
}

/*
 * ce:
 *	Clear to the end of the line
 */
void
ce(PLAYER *pp)
{
	sendcom(pp, CLRTOEOL);
}

/*
 * sendcom:
 *	Send a command to the given user
 */
void
sendcom(PLAYER *pp, int command, ...)
{
	va_list	ap;
	char	buf[3];
	int	len = 0;

	va_start(ap, command);
	buf[len++] = command;
	switch (command & 0377) {
	case MOVE:
		buf[len++] = va_arg(ap, int);
		buf[len++] = va_arg(ap, int);
		break;
	case ADDCH:
	case READY:
	case ENDWIN:
		buf[len++] = va_arg(ap, int);
		break;
	}
	va_end(ap);

	if (pp == ALL_PLAYERS) {
		for (pp = Player; pp < End_player; pp++)
			fwrite(buf, sizeof buf[0], len, pp->p_output);
		for (pp = Monitor; pp < End_monitor; pp++)
			fwrite(buf, sizeof buf[0], len, pp->p_output);
		return;
	} else
		fwrite(buf, sizeof buf[0], len, pp->p_output);
}

/*
 * sync:
 *	Flush the output buffer to the player
 */
void
flush(PLAYER *pp)
{
	if (pp == ALL_PLAYERS) {
		for (pp = Player; pp < End_player; pp++)
			fflush(pp->p_output);
		for (pp = Monitor; pp < End_monitor; pp++)
			fflush(pp->p_output);
	} else
		fflush(pp->p_output);
}

void
logx(int prio, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (conf_syslog)
		vsyslog(prio, fmt, ap);
	else if (conf_logerr)
	/* if (prio < LOG_NOTICE) */
		vwarnx(fmt, ap);
	va_end(ap);
}

void
logit(int prio, const char *fmt, ...)
{
	va_list ap;
	char fmtm[1024];

	va_start(ap, fmt);
	if (conf_syslog) {
		strlcpy(fmtm, fmt, sizeof fmtm);
		strlcat(fmtm, ": %m", sizeof fmtm);
		vsyslog(prio, fmtm, ap);
	} else if (conf_logerr)
	/* if (prio < LOG_NOTICE) */
		vwarn(fmt, ap);
	va_end(ap);
}
