/*	$NetBSD: tty.c,v 1.59 2016/03/22 01:34:32 christos Exp $	*/

/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Christos Zoulas of Cornell University.
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

#include "config.h"
#if !defined(lint) && !defined(SCCSID)
#if 0
static char sccsid[] = "@(#)tty.c	8.1 (Berkeley) 6/4/93";
#else
__RCSID("$NetBSD: tty.c,v 1.59 2016/03/22 01:34:32 christos Exp $");
#endif
#endif /* not lint && not SCCSID */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * tty.c: tty interface stuff
 */
#include <assert.h>
#include <errno.h>
#include <stdlib.h>	/* for abort */
#include <string.h>
#include <strings.h>	/* for ffs */
#include <unistd.h>	/* for isatty */

#include "el.h"
#include "parse.h"

typedef struct ttymodes_t {
	const char *m_name;
	unsigned int m_value;
	int m_type;
}          ttymodes_t;

typedef struct ttymap_t {
	wint_t nch, och;	/* Internal and termio rep of chars */
	el_action_t bind[3];	/* emacs, vi, and vi-cmd */
} ttymap_t;


private const ttyperm_t ttyperm = {
	{
		{"iflag:", ICRNL, (INLCR | IGNCR)},
		{"oflag:", (OPOST | ONLCR), ONLRET},
		{"cflag:", 0, 0},
		{"lflag:", (ISIG | ICANON | ECHO | ECHOE | ECHOCTL | IEXTEN),
		(NOFLSH | ECHONL | EXTPROC | FLUSHO)},
		{"chars:", 0, 0},
	},
	{
		{"iflag:", (INLCR | ICRNL), IGNCR},
		{"oflag:", (OPOST | ONLCR), ONLRET},
		{"cflag:", 0, 0},
		{"lflag:", ISIG,
		(NOFLSH | ICANON | ECHO | ECHOK | ECHONL | EXTPROC | IEXTEN | FLUSHO)},
		{"chars:", (C_SH(C_MIN) | C_SH(C_TIME) | C_SH(C_SWTCH) | C_SH(C_DSWTCH) |
			    C_SH(C_SUSP) | C_SH(C_DSUSP) | C_SH(C_EOL) | C_SH(C_DISCARD) |
		    C_SH(C_PGOFF) | C_SH(C_PAGE) | C_SH(C_STATUS)), 0}
	},
	{
		{"iflag:", 0, IXON | IXOFF | INLCR | ICRNL},
		{"oflag:", 0, 0},
		{"cflag:", 0, 0},
		{"lflag:", 0, ISIG | IEXTEN},
		{"chars:", 0, 0},
	}
};

private const ttychar_t ttychar = {
	{
		CINTR, CQUIT, CERASE, CKILL,
		CEOF, CEOL, CEOL2, CSWTCH,
		CDSWTCH, CERASE2, CSTART, CSTOP,
		CWERASE, CSUSP, CDSUSP, CREPRINT,
		CDISCARD, CLNEXT, CSTATUS, CPAGE,
		CPGOFF, CKILL2, CBRK, CMIN,
		CTIME
	},
	{
		CINTR, CQUIT, CERASE, CKILL,
		_POSIX_VDISABLE, _POSIX_VDISABLE, _POSIX_VDISABLE, _POSIX_VDISABLE,
		_POSIX_VDISABLE, CERASE2, CSTART, CSTOP,
		_POSIX_VDISABLE, CSUSP, _POSIX_VDISABLE, _POSIX_VDISABLE,
		CDISCARD, _POSIX_VDISABLE, _POSIX_VDISABLE, _POSIX_VDISABLE,
		_POSIX_VDISABLE, _POSIX_VDISABLE, _POSIX_VDISABLE, 1,
		0
	},
	{
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0
	}
};

private const ttymap_t tty_map[] = {
#ifdef VERASE
	{C_ERASE, VERASE,
	{EM_DELETE_PREV_CHAR, VI_DELETE_PREV_CHAR, ED_PREV_CHAR}},
#endif /* VERASE */
#ifdef VERASE2
	{C_ERASE2, VERASE2,
	{EM_DELETE_PREV_CHAR, VI_DELETE_PREV_CHAR, ED_PREV_CHAR}},
#endif /* VERASE2 */
#ifdef VKILL
	{C_KILL, VKILL,
	{EM_KILL_LINE, VI_KILL_LINE_PREV, ED_UNASSIGNED}},
#endif /* VKILL */
#ifdef VKILL2
	{C_KILL2, VKILL2,
	{EM_KILL_LINE, VI_KILL_LINE_PREV, ED_UNASSIGNED}},
#endif /* VKILL2 */
#ifdef VEOF
	{C_EOF, VEOF,
	{EM_DELETE_OR_LIST, VI_LIST_OR_EOF, ED_UNASSIGNED}},
#endif /* VEOF */
#ifdef VWERASE
	{C_WERASE, VWERASE,
	{ED_DELETE_PREV_WORD, ED_DELETE_PREV_WORD, ED_PREV_WORD}},
#endif /* VWERASE */
#ifdef VREPRINT
	{C_REPRINT, VREPRINT,
	{ED_REDISPLAY, ED_INSERT, ED_REDISPLAY}},
#endif /* VREPRINT */
#ifdef VLNEXT
	{C_LNEXT, VLNEXT,
	{ED_QUOTED_INSERT, ED_QUOTED_INSERT, ED_UNASSIGNED}},
#endif /* VLNEXT */
	{(wint_t)-1, (wint_t)-1,
	{ED_UNASSIGNED, ED_UNASSIGNED, ED_UNASSIGNED}}
};

private const ttymodes_t ttymodes[] = {
#ifdef	IGNBRK
	{"ignbrk", IGNBRK, MD_INP},
#endif /* IGNBRK */
#ifdef	BRKINT
	{"brkint", BRKINT, MD_INP},
#endif /* BRKINT */
#ifdef	IGNPAR
	{"ignpar", IGNPAR, MD_INP},
#endif /* IGNPAR */
#ifdef	PARMRK
	{"parmrk", PARMRK, MD_INP},
#endif /* PARMRK */
#ifdef	INPCK
	{"inpck", INPCK, MD_INP},
#endif /* INPCK */
#ifdef	ISTRIP
	{"istrip", ISTRIP, MD_INP},
#endif /* ISTRIP */
#ifdef	INLCR
	{"inlcr", INLCR, MD_INP},
#endif /* INLCR */
#ifdef	IGNCR
	{"igncr", IGNCR, MD_INP},
#endif /* IGNCR */
#ifdef	ICRNL
	{"icrnl", ICRNL, MD_INP},
#endif /* ICRNL */
#ifdef	IUCLC
	{"iuclc", IUCLC, MD_INP},
#endif /* IUCLC */
#ifdef	IXON
	{"ixon", IXON, MD_INP},
#endif /* IXON */
#ifdef	IXANY
	{"ixany", IXANY, MD_INP},
#endif /* IXANY */
#ifdef	IXOFF
	{"ixoff", IXOFF, MD_INP},
#endif /* IXOFF */
#ifdef  IMAXBEL
	{"imaxbel", IMAXBEL, MD_INP},
#endif /* IMAXBEL */

#ifdef	OPOST
	{"opost", OPOST, MD_OUT},
#endif /* OPOST */
#ifdef	OLCUC
	{"olcuc", OLCUC, MD_OUT},
#endif /* OLCUC */
#ifdef	ONLCR
	{"onlcr", ONLCR, MD_OUT},
#endif /* ONLCR */
#ifdef	OCRNL
	{"ocrnl", OCRNL, MD_OUT},
#endif /* OCRNL */
#ifdef	ONOCR
	{"onocr", ONOCR, MD_OUT},
#endif /* ONOCR */
#ifdef ONOEOT
	{"onoeot", ONOEOT, MD_OUT},
#endif /* ONOEOT */
#ifdef	ONLRET
	{"onlret", ONLRET, MD_OUT},
#endif /* ONLRET */
#ifdef	OFILL
	{"ofill", OFILL, MD_OUT},
#endif /* OFILL */
#ifdef	OFDEL
	{"ofdel", OFDEL, MD_OUT},
#endif /* OFDEL */
#ifdef	NLDLY
	{"nldly", NLDLY, MD_OUT},
#endif /* NLDLY */
#ifdef	CRDLY
	{"crdly", CRDLY, MD_OUT},
#endif /* CRDLY */
#ifdef	TABDLY
	{"tabdly", TABDLY, MD_OUT},
#endif /* TABDLY */
#ifdef	XTABS
	{"xtabs", XTABS, MD_OUT},
#endif /* XTABS */
#ifdef	BSDLY
	{"bsdly", BSDLY, MD_OUT},
#endif /* BSDLY */
#ifdef	VTDLY
	{"vtdly", VTDLY, MD_OUT},
#endif /* VTDLY */
#ifdef	FFDLY
	{"ffdly", FFDLY, MD_OUT},
#endif /* FFDLY */
#ifdef	PAGEOUT
	{"pageout", PAGEOUT, MD_OUT},
#endif /* PAGEOUT */
#ifdef	WRAP
	{"wrap", WRAP, MD_OUT},
#endif /* WRAP */

#ifdef	CIGNORE
	{"cignore", CIGNORE, MD_CTL},
#endif /* CBAUD */
#ifdef	CBAUD
	{"cbaud", CBAUD, MD_CTL},
#endif /* CBAUD */
#ifdef	CSTOPB
	{"cstopb", CSTOPB, MD_CTL},
#endif /* CSTOPB */
#ifdef	CREAD
	{"cread", CREAD, MD_CTL},
#endif /* CREAD */
#ifdef	PARENB
	{"parenb", PARENB, MD_CTL},
#endif /* PARENB */
#ifdef	PARODD
	{"parodd", PARODD, MD_CTL},
#endif /* PARODD */
#ifdef	HUPCL
	{"hupcl", HUPCL, MD_CTL},
#endif /* HUPCL */
#ifdef	CLOCAL
	{"clocal", CLOCAL, MD_CTL},
#endif /* CLOCAL */
#ifdef	LOBLK
	{"loblk", LOBLK, MD_CTL},
#endif /* LOBLK */
#ifdef	CIBAUD
	{"cibaud", CIBAUD, MD_CTL},
#endif /* CIBAUD */
#ifdef CRTSCTS
#ifdef CCTS_OFLOW
	{"ccts_oflow", CCTS_OFLOW, MD_CTL},
#else
	{"crtscts", CRTSCTS, MD_CTL},
#endif /* CCTS_OFLOW */
#endif /* CRTSCTS */
#ifdef CRTS_IFLOW
	{"crts_iflow", CRTS_IFLOW, MD_CTL},
#endif /* CRTS_IFLOW */
#ifdef CDTRCTS
	{"cdtrcts", CDTRCTS, MD_CTL},
#endif /* CDTRCTS */
#ifdef MDMBUF
	{"mdmbuf", MDMBUF, MD_CTL},
#endif /* MDMBUF */
#ifdef RCV1EN
	{"rcv1en", RCV1EN, MD_CTL},
#endif /* RCV1EN */
#ifdef XMT1EN
	{"xmt1en", XMT1EN, MD_CTL},
#endif /* XMT1EN */

#ifdef	ISIG
	{"isig", ISIG, MD_LIN},
#endif /* ISIG */
#ifdef	ICANON
	{"icanon", ICANON, MD_LIN},
#endif /* ICANON */
#ifdef	XCASE
	{"xcase", XCASE, MD_LIN},
#endif /* XCASE */
#ifdef	ECHO
	{"echo", ECHO, MD_LIN},
#endif /* ECHO */
#ifdef	ECHOE
	{"echoe", ECHOE, MD_LIN},
#endif /* ECHOE */
#ifdef	ECHOK
	{"echok", ECHOK, MD_LIN},
#endif /* ECHOK */
#ifdef	ECHONL
	{"echonl", ECHONL, MD_LIN},
#endif /* ECHONL */
#ifdef	NOFLSH
	{"noflsh", NOFLSH, MD_LIN},
#endif /* NOFLSH */
#ifdef	TOSTOP
	{"tostop", TOSTOP, MD_LIN},
#endif /* TOSTOP */
#ifdef	ECHOCTL
	{"echoctl", ECHOCTL, MD_LIN},
#endif /* ECHOCTL */
#ifdef	ECHOPRT
	{"echoprt", ECHOPRT, MD_LIN},
#endif /* ECHOPRT */
#ifdef	ECHOKE
	{"echoke", ECHOKE, MD_LIN},
#endif /* ECHOKE */
#ifdef	DEFECHO
	{"defecho", DEFECHO, MD_LIN},
#endif /* DEFECHO */
#ifdef	FLUSHO
	{"flusho", FLUSHO, MD_LIN},
#endif /* FLUSHO */
#ifdef	PENDIN
	{"pendin", PENDIN, MD_LIN},
#endif /* PENDIN */
#ifdef	IEXTEN
	{"iexten", IEXTEN, MD_LIN},
#endif /* IEXTEN */
#ifdef	NOKERNINFO
	{"nokerninfo", NOKERNINFO, MD_LIN},
#endif /* NOKERNINFO */
#ifdef	ALTWERASE
	{"altwerase", ALTWERASE, MD_LIN},
#endif /* ALTWERASE */
#ifdef	EXTPROC
	{"extproc", EXTPROC, MD_LIN},
#endif /* EXTPROC */

#if defined(VINTR)
	{"intr", C_SH(C_INTR), MD_CHAR},
#endif /* VINTR */
#if defined(VQUIT)
	{"quit", C_SH(C_QUIT), MD_CHAR},
#endif /* VQUIT */
#if defined(VERASE)
	{"erase", C_SH(C_ERASE), MD_CHAR},
#endif /* VERASE */
#if defined(VKILL)
	{"kill", C_SH(C_KILL), MD_CHAR},
#endif /* VKILL */
#if defined(VEOF)
	{"eof", C_SH(C_EOF), MD_CHAR},
#endif /* VEOF */
#if defined(VEOL)
	{"eol", C_SH(C_EOL), MD_CHAR},
#endif /* VEOL */
#if defined(VEOL2)
	{"eol2", C_SH(C_EOL2), MD_CHAR},
#endif /* VEOL2 */
#if defined(VSWTCH)
	{"swtch", C_SH(C_SWTCH), MD_CHAR},
#endif /* VSWTCH */
#if defined(VDSWTCH)
	{"dswtch", C_SH(C_DSWTCH), MD_CHAR},
#endif /* VDSWTCH */
#if defined(VERASE2)
	{"erase2", C_SH(C_ERASE2), MD_CHAR},
#endif /* VERASE2 */
#if defined(VSTART)
	{"start", C_SH(C_START), MD_CHAR},
#endif /* VSTART */
#if defined(VSTOP)
	{"stop", C_SH(C_STOP), MD_CHAR},
#endif /* VSTOP */
#if defined(VWERASE)
	{"werase", C_SH(C_WERASE), MD_CHAR},
#endif /* VWERASE */
#if defined(VSUSP)
	{"susp", C_SH(C_SUSP), MD_CHAR},
#endif /* VSUSP */
#if defined(VDSUSP)
	{"dsusp", C_SH(C_DSUSP), MD_CHAR},
#endif /* VDSUSP */
#if defined(VREPRINT)
	{"reprint", C_SH(C_REPRINT), MD_CHAR},
#endif /* VREPRINT */
#if defined(VDISCARD)
	{"discard", C_SH(C_DISCARD), MD_CHAR},
#endif /* VDISCARD */
#if defined(VLNEXT)
	{"lnext", C_SH(C_LNEXT), MD_CHAR},
#endif /* VLNEXT */
#if defined(VSTATUS)
	{"status", C_SH(C_STATUS), MD_CHAR},
#endif /* VSTATUS */
#if defined(VPAGE)
	{"page", C_SH(C_PAGE), MD_CHAR},
#endif /* VPAGE */
#if defined(VPGOFF)
	{"pgoff", C_SH(C_PGOFF), MD_CHAR},
#endif /* VPGOFF */
#if defined(VKILL2)
	{"kill2", C_SH(C_KILL2), MD_CHAR},
#endif /* VKILL2 */
#if defined(VBRK)
	{"brk", C_SH(C_BRK), MD_CHAR},
#endif /* VBRK */
#if defined(VMIN)
	{"min", C_SH(C_MIN), MD_CHAR},
#endif /* VMIN */
#if defined(VTIME)
	{"time", C_SH(C_TIME), MD_CHAR},
#endif /* VTIME */
	{NULL, 0, -1},
};



#define	tty__gettabs(td)	((((td)->c_oflag & TAB3) == TAB3) ? 0 : 1)
#define	tty__geteightbit(td)	(((td)->c_cflag & CSIZE) == CS8)
#define	tty__cooked_mode(td)	((td)->c_lflag & ICANON)

private int	tty_getty(EditLine *, struct termios *);
private int	tty_setty(EditLine *, int, const struct termios *);
private int	tty__getcharindex(int);
private void	tty__getchar(struct termios *, unsigned char *);
private void	tty__setchar(struct termios *, unsigned char *);
private speed_t	tty__getspeed(struct termios *);
private int	tty_setup(EditLine *);
private void	tty_setup_flags(EditLine *, struct termios *, int);

#define	t_qu	t_ts

/* tty_getty():
 *	Wrapper for tcgetattr to handle EINTR
 */
private int
tty_getty(EditLine *el, struct termios *t)
{
	int rv;
	while ((rv = tcgetattr(el->el_infd, t)) == -1 && errno == EINTR)
		continue;
	return rv;
}

/* tty_setty():
 *	Wrapper for tcsetattr to handle EINTR
 */
private int
tty_setty(EditLine *el, int action, const struct termios *t)
{
	int rv;
	while ((rv = tcsetattr(el->el_infd, action, t)) == -1 && errno == EINTR)
		continue;
	return rv;
}

/* tty_setup():
 *	Get the tty parameters and initialize the editing state
 */
private int
tty_setup(EditLine *el)
{
	int rst = 1;

	if (el->el_flags & EDIT_DISABLED)
		return 0;

	if (el->el_tty.t_initialized)
		return -1;

	if (!isatty(el->el_outfd)) {
#ifdef DEBUG_TTY
		(void) fprintf(el->el_errfile, "%s: isatty: %s\n", __func__,
		    strerror(errno));
#endif /* DEBUG_TTY */
		return -1;
	}
	if (tty_getty(el, &el->el_tty.t_or) == -1) {
#ifdef DEBUG_TTY
		(void) fprintf(el->el_errfile, "%s: tty_getty: %s\n", __func__,
		    strerror(errno));
#endif /* DEBUG_TTY */
		return -1;
	}
	el->el_tty.t_ts = el->el_tty.t_ex = el->el_tty.t_ed = el->el_tty.t_or;

	el->el_tty.t_speed = tty__getspeed(&el->el_tty.t_ex);
	el->el_tty.t_tabs = tty__gettabs(&el->el_tty.t_ex);
	el->el_tty.t_eight = tty__geteightbit(&el->el_tty.t_ex);

	tty_setup_flags(el, &el->el_tty.t_ex, EX_IO);

	/*
         * Reset the tty chars to reasonable defaults
         * If they are disabled, then enable them.
         */
	if (rst) {
		if (tty__cooked_mode(&el->el_tty.t_ts)) {
			tty__getchar(&el->el_tty.t_ts, el->el_tty.t_c[TS_IO]);
			/*
	                 * Don't affect CMIN and CTIME for the editor mode
	                 */
			for (rst = 0; rst < C_NCC - 2; rst++)
				if (el->el_tty.t_c[TS_IO][rst] !=
				      el->el_tty.t_vdisable
				    && el->el_tty.t_c[ED_IO][rst] !=
				      el->el_tty.t_vdisable)
					el->el_tty.t_c[ED_IO][rst] =
					    el->el_tty.t_c[TS_IO][rst];
			for (rst = 0; rst < C_NCC; rst++)
				if (el->el_tty.t_c[TS_IO][rst] !=
				    el->el_tty.t_vdisable)
					el->el_tty.t_c[EX_IO][rst] =
					    el->el_tty.t_c[TS_IO][rst];
		}
		tty__setchar(&el->el_tty.t_ex, el->el_tty.t_c[EX_IO]);
		if (tty_setty(el, TCSADRAIN, &el->el_tty.t_ex) == -1) {
#ifdef DEBUG_TTY
			(void) fprintf(el->el_errfile, "%s: tty_setty: %s\n",
			    __func__, strerror(errno));
#endif /* DEBUG_TTY */
			return -1;
		}
	}

	tty_setup_flags(el, &el->el_tty.t_ed, ED_IO);

	tty__setchar(&el->el_tty.t_ed, el->el_tty.t_c[ED_IO]);
	tty_bind_char(el, 1);
	el->el_tty.t_initialized = 1;
	return 0;
}

protected int
tty_init(EditLine *el)
{

	el->el_tty.t_mode = EX_IO;
	el->el_tty.t_vdisable = _POSIX_VDISABLE;
	el->el_tty.t_initialized = 0;
	(void) memcpy(el->el_tty.t_t, ttyperm, sizeof(ttyperm_t));
	(void) memcpy(el->el_tty.t_c, ttychar, sizeof(ttychar_t));
	return tty_setup(el);
}


/* tty_end():
 *	Restore the tty to its original settings
 */
protected void
/*ARGSUSED*/
tty_end(EditLine *el)
{
	if (el->el_flags & EDIT_DISABLED)
		return;

	if (!el->el_tty.t_initialized)
		return;

	if (tty_setty(el, TCSAFLUSH, &el->el_tty.t_or) == -1) {
#ifdef DEBUG_TTY
		(void) fprintf(el->el_errfile,
		    "%s: tty_setty: %s\n", __func__, strerror(errno));
#endif /* DEBUG_TTY */
	}
}


/* tty__getspeed():
 *	Get the tty speed
 */
private speed_t
tty__getspeed(struct termios *td)
{
	speed_t spd;

	if ((spd = cfgetispeed(td)) == 0)
		spd = cfgetospeed(td);
	return spd;
}

/* tty__getspeed():
 *	Return the index of the asked char in the c_cc array
 */
private int
tty__getcharindex(int i)
{
	switch (i) {
#ifdef VINTR
	case C_INTR:
		return VINTR;
#endif /* VINTR */
#ifdef VQUIT
	case C_QUIT:
		return VQUIT;
#endif /* VQUIT */
#ifdef VERASE
	case C_ERASE:
		return VERASE;
#endif /* VERASE */
#ifdef VKILL
	case C_KILL:
		return VKILL;
#endif /* VKILL */
#ifdef VEOF
	case C_EOF:
		return VEOF;
#endif /* VEOF */
#ifdef VEOL
	case C_EOL:
		return VEOL;
#endif /* VEOL */
#ifdef VEOL2
	case C_EOL2:
		return VEOL2;
#endif /* VEOL2 */
#ifdef VSWTCH
	case C_SWTCH:
		return VSWTCH;
#endif /* VSWTCH */
#ifdef VDSWTCH
	case C_DSWTCH:
		return VDSWTCH;
#endif /* VDSWTCH */
#ifdef VERASE2
	case C_ERASE2:
		return VERASE2;
#endif /* VERASE2 */
#ifdef VSTART
	case C_START:
		return VSTART;
#endif /* VSTART */
#ifdef VSTOP
	case C_STOP:
		return VSTOP;
#endif /* VSTOP */
#ifdef VWERASE
	case C_WERASE:
		return VWERASE;
#endif /* VWERASE */
#ifdef VSUSP
	case C_SUSP:
		return VSUSP;
#endif /* VSUSP */
#ifdef VDSUSP
	case C_DSUSP:
		return VDSUSP;
#endif /* VDSUSP */
#ifdef VREPRINT
	case C_REPRINT:
		return VREPRINT;
#endif /* VREPRINT */
#ifdef VDISCARD
	case C_DISCARD:
		return VDISCARD;
#endif /* VDISCARD */
#ifdef VLNEXT
	case C_LNEXT:
		return VLNEXT;
#endif /* VLNEXT */
#ifdef VSTATUS
	case C_STATUS:
		return VSTATUS;
#endif /* VSTATUS */
#ifdef VPAGE
	case C_PAGE:
		return VPAGE;
#endif /* VPAGE */
#ifdef VPGOFF
	case C_PGOFF:
		return VPGOFF;
#endif /* VPGOFF */
#ifdef VKILL2
	case C_KILL2:
		return VKILL2;
#endif /* KILL2 */
#ifdef VMIN
	case C_MIN:
		return VMIN;
#endif /* VMIN */
#ifdef VTIME
	case C_TIME:
		return VTIME;
#endif /* VTIME */
	default:
		return -1;
	}
}

/* tty__getchar():
 *	Get the tty characters
 */
private void
tty__getchar(struct termios *td, unsigned char *s)
{

#ifdef VINTR
	s[C_INTR] = td->c_cc[VINTR];
#endif /* VINTR */
#ifdef VQUIT
	s[C_QUIT] = td->c_cc[VQUIT];
#endif /* VQUIT */
#ifdef VERASE
	s[C_ERASE] = td->c_cc[VERASE];
#endif /* VERASE */
#ifdef VKILL
	s[C_KILL] = td->c_cc[VKILL];
#endif /* VKILL */
#ifdef VEOF
	s[C_EOF] = td->c_cc[VEOF];
#endif /* VEOF */
#ifdef VEOL
	s[C_EOL] = td->c_cc[VEOL];
#endif /* VEOL */
#ifdef VEOL2
	s[C_EOL2] = td->c_cc[VEOL2];
#endif /* VEOL2 */
#ifdef VSWTCH
	s[C_SWTCH] = td->c_cc[VSWTCH];
#endif /* VSWTCH */
#ifdef VDSWTCH
	s[C_DSWTCH] = td->c_cc[VDSWTCH];
#endif /* VDSWTCH */
#ifdef VERASE2
	s[C_ERASE2] = td->c_cc[VERASE2];
#endif /* VERASE2 */
#ifdef VSTART
	s[C_START] = td->c_cc[VSTART];
#endif /* VSTART */
#ifdef VSTOP
	s[C_STOP] = td->c_cc[VSTOP];
#endif /* VSTOP */
#ifdef VWERASE
	s[C_WERASE] = td->c_cc[VWERASE];
#endif /* VWERASE */
#ifdef VSUSP
	s[C_SUSP] = td->c_cc[VSUSP];
#endif /* VSUSP */
#ifdef VDSUSP
	s[C_DSUSP] = td->c_cc[VDSUSP];
#endif /* VDSUSP */
#ifdef VREPRINT
	s[C_REPRINT] = td->c_cc[VREPRINT];
#endif /* VREPRINT */
#ifdef VDISCARD
	s[C_DISCARD] = td->c_cc[VDISCARD];
#endif /* VDISCARD */
#ifdef VLNEXT
	s[C_LNEXT] = td->c_cc[VLNEXT];
#endif /* VLNEXT */
#ifdef VSTATUS
	s[C_STATUS] = td->c_cc[VSTATUS];
#endif /* VSTATUS */
#ifdef VPAGE
	s[C_PAGE] = td->c_cc[VPAGE];
#endif /* VPAGE */
#ifdef VPGOFF
	s[C_PGOFF] = td->c_cc[VPGOFF];
#endif /* VPGOFF */
#ifdef VKILL2
	s[C_KILL2] = td->c_cc[VKILL2];
#endif /* KILL2 */
#ifdef VMIN
	s[C_MIN] = td->c_cc[VMIN];
#endif /* VMIN */
#ifdef VTIME
	s[C_TIME] = td->c_cc[VTIME];
#endif /* VTIME */
}				/* tty__getchar */


/* tty__setchar():
 *	Set the tty characters
 */
private void
tty__setchar(struct termios *td, unsigned char *s)
{

#ifdef VINTR
	td->c_cc[VINTR] = s[C_INTR];
#endif /* VINTR */
#ifdef VQUIT
	td->c_cc[VQUIT] = s[C_QUIT];
#endif /* VQUIT */
#ifdef VERASE
	td->c_cc[VERASE] = s[C_ERASE];
#endif /* VERASE */
#ifdef VKILL
	td->c_cc[VKILL] = s[C_KILL];
#endif /* VKILL */
#ifdef VEOF
	td->c_cc[VEOF] = s[C_EOF];
#endif /* VEOF */
#ifdef VEOL
	td->c_cc[VEOL] = s[C_EOL];
#endif /* VEOL */
#ifdef VEOL2
	td->c_cc[VEOL2] = s[C_EOL2];
#endif /* VEOL2 */
#ifdef VSWTCH
	td->c_cc[VSWTCH] = s[C_SWTCH];
#endif /* VSWTCH */
#ifdef VDSWTCH
	td->c_cc[VDSWTCH] = s[C_DSWTCH];
#endif /* VDSWTCH */
#ifdef VERASE2
	td->c_cc[VERASE2] = s[C_ERASE2];
#endif /* VERASE2 */
#ifdef VSTART
	td->c_cc[VSTART] = s[C_START];
#endif /* VSTART */
#ifdef VSTOP
	td->c_cc[VSTOP] = s[C_STOP];
#endif /* VSTOP */
#ifdef VWERASE
	td->c_cc[VWERASE] = s[C_WERASE];
#endif /* VWERASE */
#ifdef VSUSP
	td->c_cc[VSUSP] = s[C_SUSP];
#endif /* VSUSP */
#ifdef VDSUSP
	td->c_cc[VDSUSP] = s[C_DSUSP];
#endif /* VDSUSP */
#ifdef VREPRINT
	td->c_cc[VREPRINT] = s[C_REPRINT];
#endif /* VREPRINT */
#ifdef VDISCARD
	td->c_cc[VDISCARD] = s[C_DISCARD];
#endif /* VDISCARD */
#ifdef VLNEXT
	td->c_cc[VLNEXT] = s[C_LNEXT];
#endif /* VLNEXT */
#ifdef VSTATUS
	td->c_cc[VSTATUS] = s[C_STATUS];
#endif /* VSTATUS */
#ifdef VPAGE
	td->c_cc[VPAGE] = s[C_PAGE];
#endif /* VPAGE */
#ifdef VPGOFF
	td->c_cc[VPGOFF] = s[C_PGOFF];
#endif /* VPGOFF */
#ifdef VKILL2
	td->c_cc[VKILL2] = s[C_KILL2];
#endif /* VKILL2 */
#ifdef VMIN
	td->c_cc[VMIN] = s[C_MIN];
#endif /* VMIN */
#ifdef VTIME
	td->c_cc[VTIME] = s[C_TIME];
#endif /* VTIME */
}				/* tty__setchar */


/* tty_bind_char():
 *	Rebind the editline functions
 */
protected void
tty_bind_char(EditLine *el, int force)
{

	unsigned char *t_n = el->el_tty.t_c[ED_IO];
	unsigned char *t_o = el->el_tty.t_ed.c_cc;
	Char new[2], old[2];
	const ttymap_t *tp;
	el_action_t *map, *alt;
	const el_action_t *dmap, *dalt;
	new[1] = old[1] = '\0';

	map = el->el_map.key;
	alt = el->el_map.alt;
	if (el->el_map.type == MAP_VI) {
		dmap = el->el_map.vii;
		dalt = el->el_map.vic;
	} else {
		dmap = el->el_map.emacs;
		dalt = NULL;
	}

	for (tp = tty_map; tp->nch != (wint_t)-1; tp++) {
		new[0] = (Char)t_n[tp->nch];
		old[0] = (Char)t_o[tp->och];
		if (new[0] == old[0] && !force)
			continue;
		/* Put the old default binding back, and set the new binding */
		keymacro_clear(el, map, old);
		map[UC(old[0])] = dmap[UC(old[0])];
		keymacro_clear(el, map, new);
		/* MAP_VI == 1, MAP_EMACS == 0... */
		map[UC(new[0])] = tp->bind[el->el_map.type];
		if (dalt) {
			keymacro_clear(el, alt, old);
			alt[UC(old[0])] = dalt[UC(old[0])];
			keymacro_clear(el, alt, new);
			alt[UC(new[0])] = tp->bind[el->el_map.type + 1];
		}
	}
}


private tcflag_t *
tty__get_flag(struct termios *t, int kind) {
	switch (kind) {
	case MD_INP:
		return &t->c_iflag;
	case MD_OUT:
		return &t->c_oflag;
	case MD_CTL:
		return &t->c_cflag;
	case MD_LIN:
		return &t->c_lflag;
	default:
		abort();
		/*NOTREACHED*/
	}
}


private tcflag_t
tty_update_flag(EditLine *el, tcflag_t f, int mode, int kind)
{
	f &= ~el->el_tty.t_t[mode][kind].t_clrmask;
	f |= el->el_tty.t_t[mode][kind].t_setmask;
	return f;
}


private void
tty_update_flags(EditLine *el, int kind)
{
	tcflag_t *tt, *ed, *ex;
	tt = tty__get_flag(&el->el_tty.t_ts, kind);
	ed = tty__get_flag(&el->el_tty.t_ed, kind);
	ex = tty__get_flag(&el->el_tty.t_ex, kind);

	if (*tt != *ex && (kind != MD_CTL || *tt != *ed)) {
		*ed = tty_update_flag(el, *tt, ED_IO, kind);
		*ex = tty_update_flag(el, *tt, EX_IO, kind);
	}
}


private void
tty_update_char(EditLine *el, int mode, int c) {
	if (!((el->el_tty.t_t[mode][MD_CHAR].t_setmask & C_SH(c)))
	    && (el->el_tty.t_c[TS_IO][c] != el->el_tty.t_c[EX_IO][c]))
		el->el_tty.t_c[mode][c] = el->el_tty.t_c[TS_IO][c];
	if (el->el_tty.t_t[mode][MD_CHAR].t_clrmask & C_SH(c))
		el->el_tty.t_c[mode][c] = el->el_tty.t_vdisable;
}


/* tty_rawmode():
 *	Set terminal into 1 character at a time mode.
 */
protected int
tty_rawmode(EditLine *el)
{

	if (el->el_tty.t_mode == ED_IO || el->el_tty.t_mode == QU_IO)
		return 0;

	if (el->el_flags & EDIT_DISABLED)
		return 0;

	if (tty_getty(el, &el->el_tty.t_ts) == -1) {
#ifdef DEBUG_TTY
		(void) fprintf(el->el_errfile, "%s: tty_getty: %s\n", __func__,
		    strerror(errno));
#endif /* DEBUG_TTY */
		return -1;
	}
	/*
         * We always keep up with the eight bit setting and the speed of the
         * tty. But we only believe changes that are made to cooked mode!
         */
	el->el_tty.t_eight = tty__geteightbit(&el->el_tty.t_ts);
	el->el_tty.t_speed = tty__getspeed(&el->el_tty.t_ts);

	if (tty__getspeed(&el->el_tty.t_ex) != el->el_tty.t_speed ||
	    tty__getspeed(&el->el_tty.t_ed) != el->el_tty.t_speed) {
		(void) cfsetispeed(&el->el_tty.t_ex, el->el_tty.t_speed);
		(void) cfsetospeed(&el->el_tty.t_ex, el->el_tty.t_speed);
		(void) cfsetispeed(&el->el_tty.t_ed, el->el_tty.t_speed);
		(void) cfsetospeed(&el->el_tty.t_ed, el->el_tty.t_speed);
	}
	if (tty__cooked_mode(&el->el_tty.t_ts)) {
		int i;

		for (i = MD_INP; i <= MD_LIN; i++)
			tty_update_flags(el, i);

		if (tty__gettabs(&el->el_tty.t_ex) == 0)
			el->el_tty.t_tabs = 0;
		else
			el->el_tty.t_tabs = EL_CAN_TAB ? 1 : 0;

		tty__getchar(&el->el_tty.t_ts, el->el_tty.t_c[TS_IO]);
		/*
		 * Check if the user made any changes.
		 * If he did, then propagate the changes to the
		 * edit and execute data structures.
		 */
		for (i = 0; i < C_NCC; i++)
			if (el->el_tty.t_c[TS_IO][i] !=
			    el->el_tty.t_c[EX_IO][i])
				break;

		if (i != C_NCC) {
			/*
			 * Propagate changes only to the unprotected
			 * chars that have been modified just now.
			 */
			for (i = 0; i < C_NCC; i++)
				tty_update_char(el, ED_IO, i);

			tty_bind_char(el, 0);
			tty__setchar(&el->el_tty.t_ed, el->el_tty.t_c[ED_IO]);

			for (i = 0; i < C_NCC; i++)
				tty_update_char(el, EX_IO, i);

			tty__setchar(&el->el_tty.t_ex, el->el_tty.t_c[EX_IO]);
		}
	}
	if (tty_setty(el, TCSADRAIN, &el->el_tty.t_ed) == -1) {
#ifdef DEBUG_TTY
		(void) fprintf(el->el_errfile, "%s: tty_setty: %s\n", __func__,
		    strerror(errno));
#endif /* DEBUG_TTY */
		return -1;
	}
	el->el_tty.t_mode = ED_IO;
	return 0;
}


/* tty_cookedmode():
 *	Set the tty back to normal mode
 */
protected int
tty_cookedmode(EditLine *el)
{				/* set tty in normal setup */

	if (el->el_tty.t_mode == EX_IO)
		return 0;

	if (el->el_flags & EDIT_DISABLED)
		return 0;

	if (tty_setty(el, TCSADRAIN, &el->el_tty.t_ex) == -1) {
#ifdef DEBUG_TTY
		(void) fprintf(el->el_errfile, "%s: tty_setty: %s\n", __func__,
		    strerror(errno));
#endif /* DEBUG_TTY */
		return -1;
	}
	el->el_tty.t_mode = EX_IO;
	return 0;
}


/* tty_quotemode():
 *	Turn on quote mode
 */
protected int
tty_quotemode(EditLine *el)
{
	if (el->el_tty.t_mode == QU_IO)
		return 0;

	el->el_tty.t_qu = el->el_tty.t_ed;

	tty_setup_flags(el, &el->el_tty.t_qu, QU_IO);

	if (tty_setty(el, TCSADRAIN, &el->el_tty.t_qu) == -1) {
#ifdef DEBUG_TTY
		(void) fprintf(el->el_errfile, "%s: tty_setty: %s\n", __func__,
		    strerror(errno));
#endif /* DEBUG_TTY */
		return -1;
	}
	el->el_tty.t_mode = QU_IO;
	return 0;
}


/* tty_noquotemode():
 *	Turn off quote mode
 */
protected int
tty_noquotemode(EditLine *el)
{

	if (el->el_tty.t_mode != QU_IO)
		return 0;
	if (tty_setty(el, TCSADRAIN, &el->el_tty.t_ed) == -1) {
#ifdef DEBUG_TTY
		(void) fprintf(el->el_errfile, "%s: tty_setty: %s\n", __func__,
		    strerror(errno));
#endif /* DEBUG_TTY */
		return -1;
	}
	el->el_tty.t_mode = ED_IO;
	return 0;
}


/* tty_stty():
 *	Stty builtin
 */
protected int
/*ARGSUSED*/
tty_stty(EditLine *el, int argc __attribute__((__unused__)), const Char **argv)
{
	const ttymodes_t *m;
	char x;
	int aflag = 0;
	const Char *s, *d;
        char name[EL_BUFSIZ];
	struct termios *tios = &el->el_tty.t_ex;
	int z = EX_IO;

	if (argv == NULL)
		return -1;
	strncpy(name, ct_encode_string(*argv++, &el->el_scratch), sizeof(name));
        name[sizeof(name) - 1] = '\0';

	while (argv && *argv && argv[0][0] == '-' && argv[0][2] == '\0')
		switch (argv[0][1]) {
		case 'a':
			aflag++;
			argv++;
			break;
		case 'd':
			argv++;
			tios = &el->el_tty.t_ed;
			z = ED_IO;
			break;
		case 'x':
			argv++;
			tios = &el->el_tty.t_ex;
			z = EX_IO;
			break;
		case 'q':
			argv++;
			tios = &el->el_tty.t_ts;
			z = QU_IO;
			break;
		default:
			(void) fprintf(el->el_errfile,
			    "%s: Unknown switch `%lc'.\n",
			    name, (wint_t)argv[0][1]);
			return -1;
		}

	if (!argv || !*argv) {
		int i = -1;
		size_t len = 0, st = 0, cu;
		for (m = ttymodes; m->m_name; m++) {
			if (m->m_type != i) {
				(void) fprintf(el->el_outfile, "%s%s",
				    i != -1 ? "\n" : "",
				    el->el_tty.t_t[z][m->m_type].t_name);
				i = m->m_type;
				st = len =
				    strlen(el->el_tty.t_t[z][m->m_type].t_name);
			}
			if (i != -1) {
			    x = (el->el_tty.t_t[z][i].t_setmask & m->m_value)
				?  '+' : '\0';

			    if (el->el_tty.t_t[z][i].t_clrmask & m->m_value)
				x = '-';
			} else {
			    x = '\0';
			}

			if (x != '\0' || aflag) {

				cu = strlen(m->m_name) + (x != '\0') + 1;

				if (len + cu >=
				    (size_t)el->el_terminal.t_size.h) {
					(void) fprintf(el->el_outfile, "\n%*s",
					    (int)st, "");
					len = st + cu;
				} else
					len += cu;

				if (x != '\0')
					(void) fprintf(el->el_outfile, "%c%s ",
					    x, m->m_name);
				else
					(void) fprintf(el->el_outfile, "%s ",
					    m->m_name);
			}
		}
		(void) fprintf(el->el_outfile, "\n");
		return 0;
	}
	while (argv && (s = *argv++)) {
		const Char *p;
		switch (*s) {
		case '+':
		case '-':
			x = (char)*s++;
			break;
		default:
			x = '\0';
			break;
		}
		d = s;
		p = Strchr(s, '=');
		for (m = ttymodes; m->m_name; m++)
			if ((p ? strncmp(m->m_name, ct_encode_string(d,
			    &el->el_scratch), (size_t)(p - d)) :
			    strcmp(m->m_name, ct_encode_string(d,
			    &el->el_scratch))) == 0 &&
			    (p == NULL || m->m_type == MD_CHAR))
				break;

		if (!m->m_name) {
			(void) fprintf(el->el_errfile,
			    "%s: Invalid argument `" FSTR "'.\n", name, d);
			return -1;
		}
		if (p) {
			int c = ffs((int)m->m_value);
			int v = *++p ? parse__escape(&p) :
			    el->el_tty.t_vdisable;
			assert(c != 0);
			c--;
			c = tty__getcharindex(c);
			assert(c != -1);
			tios->c_cc[c] = (cc_t)v;
			continue;
		}
		switch (x) {
		case '+':
			el->el_tty.t_t[z][m->m_type].t_setmask |= m->m_value;
			el->el_tty.t_t[z][m->m_type].t_clrmask &= ~m->m_value;
			break;
		case '-':
			el->el_tty.t_t[z][m->m_type].t_setmask &= ~m->m_value;
			el->el_tty.t_t[z][m->m_type].t_clrmask |= m->m_value;
			break;
		default:
			el->el_tty.t_t[z][m->m_type].t_setmask &= ~m->m_value;
			el->el_tty.t_t[z][m->m_type].t_clrmask &= ~m->m_value;
			break;
		}
	}

	tty_setup_flags(el, tios, z);
	if (el->el_tty.t_mode == z) {
		if (tty_setty(el, TCSADRAIN, tios) == -1) {
#ifdef DEBUG_TTY
			(void) fprintf(el->el_errfile, "%s: tty_setty: %s\n",
			    __func__, strerror(errno));
#endif /* DEBUG_TTY */
			return -1;
		}
	}

	return 0;
}


#ifdef notyet
/* tty_printchar():
 *	DEbugging routine to print the tty characters
 */
private void
tty_printchar(EditLine *el, unsigned char *s)
{
	ttyperm_t *m;
	int i;

	for (i = 0; i < C_NCC; i++) {
		for (m = el->el_tty.t_t; m->m_name; m++)
			if (m->m_type == MD_CHAR && C_SH(i) == m->m_value)
				break;
		if (m->m_name)
			(void) fprintf(el->el_errfile, "%s ^%c ",
			    m->m_name, s[i] + 'A' - 1);
		if (i % 5 == 0)
			(void) fprintf(el->el_errfile, "\n");
	}
	(void) fprintf(el->el_errfile, "\n");
}
#endif /* notyet */


private void
tty_setup_flags(EditLine *el, struct termios *tios, int mode)
{
	int kind;
	for (kind = MD_INP; kind <= MD_LIN; kind++) {
		tcflag_t *f = tty__get_flag(tios, kind);
		*f = tty_update_flag(el, *f, mode, kind);
	}
}
