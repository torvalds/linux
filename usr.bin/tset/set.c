/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1991, 1993
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

#include <sys/cdefs.h>

__FBSDID("$FreeBSD$");

#ifndef lint
static const char sccsid[] = "@(#)set.c	8.2 (Berkeley) 2/28/94";
#endif

#include <stdio.h>
#include <termcap.h>
#include <termios.h>
#include <unistd.h>

#include "extern.h"

#define	CHK(val, dft)	(val <= 0 ? dft : val)

int	set_tabs(void);

/*
 * Reset the terminal mode bits to a sensible state.  Very useful after
 * a child program dies in raw mode.
 */
void
reset_mode(void)
{
	tcgetattr(STDERR_FILENO, &mode);

#if defined(VDISCARD) && defined(CDISCARD)
	mode.c_cc[VDISCARD] = CHK(mode.c_cc[VDISCARD], CDISCARD);
#endif
	mode.c_cc[VEOF] = CHK(mode.c_cc[VEOF], CEOF);
	mode.c_cc[VERASE] = CHK(mode.c_cc[VERASE], CERASE);
#if defined(VFLUSH) && defined(CFLUSH)
	mode.c_cc[VFLUSH] = CHK(mode.c_cc[VFLUSH], CFLUSH);
#endif
	mode.c_cc[VINTR] = CHK(mode.c_cc[VINTR], CINTR);
	mode.c_cc[VKILL] = CHK(mode.c_cc[VKILL], CKILL);
#if defined(VLNEXT) && defined(CLNEXT)
	mode.c_cc[VLNEXT] = CHK(mode.c_cc[VLNEXT], CLNEXT);
#endif
	mode.c_cc[VQUIT] = CHK(mode.c_cc[VQUIT], CQUIT);
#if defined(VREPRINT) && defined(CRPRNT)
	mode.c_cc[VREPRINT] = CHK(mode.c_cc[VREPRINT], CRPRNT);
#endif
	mode.c_cc[VSTART] = CHK(mode.c_cc[VSTART], CSTART);
	mode.c_cc[VSTOP] = CHK(mode.c_cc[VSTOP], CSTOP);
	mode.c_cc[VSUSP] = CHK(mode.c_cc[VSUSP], CSUSP);
#if defined(VWERASE) && defined(CWERASE)
	mode.c_cc[VWERASE] = CHK(mode.c_cc[VWERASE], CWERASE);
#endif

	mode.c_iflag &= ~(IGNBRK | PARMRK | INPCK | ISTRIP | INLCR | IGNCR
#ifdef IUCLC
			  | IUCLC
#endif
#ifdef IXANY
			  | IXANY
#endif
			  | IXOFF);

	mode.c_iflag |= (BRKINT | IGNPAR | ICRNL | IXON
#ifdef IMAXBEL
			 | IMAXBEL
#endif
			 );

	mode.c_oflag &= ~(0
#ifdef OLCUC
			  | OLCUC
#endif
#ifdef OCRNL
			  | OCRNL
#endif
#ifdef ONOCR
			  | ONOCR
#endif
#ifdef ONLRET
			  | ONLRET
#endif
#ifdef OFILL
			  | OFILL
#endif
#ifdef OFDEL
			  | OFDEL
#endif
#ifdef NLDLY
			  | NLDLY | CRDLY | TABDLY | BSDLY | VTDLY | FFDLY
#endif
			  );

	mode.c_oflag |= (OPOST
#ifdef ONLCR
			 | ONLCR
#endif
			 );

	mode.c_cflag &= ~(CSIZE | CSTOPB | PARENB | PARODD | CLOCAL);
	mode.c_cflag |= (CS8 | CREAD);
	mode.c_lflag &= ~(ECHONL | NOFLSH | TOSTOP
#ifdef ECHOPTR
			  | ECHOPRT
#endif
#ifdef XCASE
			  | XCASE
#endif
			  );

	mode.c_lflag |= (ISIG | ICANON | ECHO | ECHOE | ECHOK
#ifdef ECHOCTL
			 | ECHOCTL
#endif
#ifdef ECHOKE
			 | ECHOKE
#endif
 			 );

	tcsetattr(STDERR_FILENO, TCSADRAIN, &mode);
}

/*
 * Determine the erase, interrupt, and kill characters from the termcap
 * entry and command line and update their values in 'mode'.
 */
void
set_control_chars(void)
{
	char *bp, *p, bs_char, buf[1024];

	bp = buf;
	p = tgetstr("kb", &bp);
	if (p == NULL || p[1] != '\0')
		p = tgetstr("bc", &bp);
	if (p != NULL && p[1] == '\0')
		bs_char = p[0];
	else if (tgetflag("bs"))
		bs_char = CTRL('h');
	else
		bs_char = 0;

	if (erasech == 0 && bs_char != 0 && !tgetflag("os"))
		erasech = -1;
	if (erasech < 0)
		erasech = (bs_char != 0) ? bs_char : CTRL('h');

	if (mode.c_cc[VERASE] == 0 || erasech != 0)
		mode.c_cc[VERASE] = erasech ? erasech : CERASE;

	if (mode.c_cc[VINTR] == 0 || intrchar != 0)
		 mode.c_cc[VINTR] = intrchar ? intrchar : CINTR;

	if (mode.c_cc[VKILL] == 0 || killch != 0)
		mode.c_cc[VKILL] = killch ? killch : CKILL;
}

/*
 * Set up various conversions in 'mode', including parity, tabs, returns,
 * echo, and case, according to the termcap entry.  If the program we're
 * running was named with a leading upper-case character, map external
 * uppercase to internal lowercase.
 */
void
set_conversions(int usingupper)
{
	if (tgetflag("UC") || usingupper) {
#ifdef IUCLC
		mode.c_iflag |= IUCLC;
		mode.c_oflag |= OLCUC;
#endif
	} else if (tgetflag("LC")) {
#ifdef IUCLC
		mode.c_iflag &= ~IUCLC;
		mode.c_oflag &= ~OLCUC;
#endif
	}
	mode.c_iflag &= ~(PARMRK | INPCK);
	mode.c_lflag |= ICANON;
	if (tgetflag("EP")) {
		mode.c_cflag |= PARENB;
		mode.c_cflag &= ~PARODD;
	}
	if (tgetflag("OP")) {
		mode.c_cflag |= PARENB;
		mode.c_cflag |= PARODD;
	}

#ifdef ONLCR
	mode.c_oflag |= ONLCR;
#endif
	mode.c_iflag |= ICRNL;
	mode.c_lflag |= ECHO;
	mode.c_oflag |= OXTABS;
	if (tgetflag("NL")) {			/* Newline, not linefeed. */
#ifdef ONLCR
		mode.c_oflag &= ~ONLCR;
#endif
		mode.c_iflag &= ~ICRNL;
	}
	if (tgetflag("HD"))			/* Half duplex. */
		mode.c_lflag &= ~ECHO;
	if (tgetflag("pt"))			/* Print tabs. */
		mode.c_oflag &= ~OXTABS;
	mode.c_lflag |= (ECHOE | ECHOK);
}

/* Output startup string. */
void
set_init(void)
{
	char *bp, buf[1024];
	int settle;

	bp = buf;
	if (tgetstr("pc", &bp) != 0)		/* Get/set pad character. */
		PC = buf[0];

#ifdef TAB3
	if (oldmode.c_oflag & (TAB3 | ONLCR | OCRNL | ONLRET)) {
		oldmode.c_oflag &= (TAB3 | ONLCR | OCRNL | ONLRET);
		tcsetattr(STDERR_FILENO, TCSADRAIN, &oldmode);
	}
#endif
	settle = set_tabs();

	if (isreset) {
		bp = buf;
		if (tgetstr("rs", &bp) != 0 || tgetstr("is", &bp) != 0) {
			tputs(buf, 0, outc);
			settle = 1;
		}
		bp = buf;
		if (tgetstr("rf", &bp) != 0 || tgetstr("if", &bp) != 0) {
			cat(buf);
			settle = 1;
		}
	}

	if (settle) {
		(void)putc('\r', stderr);
		(void)fflush(stderr);
		(void)sleep(1);			/* Settle the terminal. */
	}
}

/*
 * Set the hardware tabs on the terminal, using the ct (clear all tabs),
 * st (set one tab) and ch (horizontal cursor addressing) capabilities.
 * This is done before if and is, so they can patch in case we blow this.
 * Return nonzero if we set any tab stops, zero if not.
 */
int
set_tabs(void)
{
	int c;
	char *capsp, *clear_tabs;
	char *set_column, *set_pos, *Set_tab;
	char caps[1024];
	const char *tg_out;

	capsp = caps;
	Set_tab = tgetstr("st", &capsp);

	if (Set_tab && (clear_tabs = tgetstr("ct", &capsp))) {
		(void)putc('\r', stderr);	/* Force to left margin. */
		tputs(clear_tabs, 0, outc);
	}

	set_column = tgetstr("ch", &capsp);
	set_pos = set_column ? NULL : tgetstr("cm", &capsp);

	if (Set_tab) {
		for (c = 8; c < Columns; c += 8) {
			/*
			 * Get to the right column.  "OOPS" is returned by
			 * tgoto() if it can't do the job.  (*snarl*)
			 */
			tg_out = "OOPS";
			if (set_column)
				tg_out = tgoto(set_column, 0, c);
			if (*tg_out == 'O' && set_pos)
				tg_out = tgoto(set_pos, c, Lines - 1);
			if (*tg_out != 'O')
				tputs(tg_out, 1, outc);
			else
				(void)fprintf(stderr, "%s", "        ");
			/* Set the tab. */
			tputs(Set_tab, 0, outc);
		}
		putc('\r', stderr);
		return (1);
	}
	return (0);
}
