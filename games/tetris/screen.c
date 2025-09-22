/*	$OpenBSD: screen.c,v 1.19 2019/06/28 13:32:52 deraadt Exp $	*/
/*	$NetBSD: screen.c,v 1.4 1995/04/29 01:11:36 mycroft Exp $	*/

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
 *	@(#)screen.c	8.1 (Berkeley) 5/31/93
 */

/*
 * Tetris screen control.
 */

#include <sys/ioctl.h>

#include <err.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <term.h>
#include <unistd.h>

#include "screen.h"
#include "tetris.h"

static cell curscreen[B_SIZE];	/* 1 => standout (or otherwise marked) */
static int curscore;
static int isset;		/* true => terminal is in game mode */
static struct termios oldtt;
static void (*tstp)(int);

static void	scr_stop(int);
static void	stopset(int);

/*
 * Capabilities from TERMCAP.
 */
extern char	PC, *BC, *UP;		/* tgoto requires globals: ugh! */

static char
	*bcstr,			/* backspace char */
	*CEstr,			/* clear to end of line */
	*CLstr,			/* clear screen */
	*CMstr,			/* cursor motion string */
#ifdef unneeded
	*CRstr,			/* "\r" equivalent */
#endif
	*HOstr,			/* cursor home */
	*LLstr,			/* last line, first column */
	*pcstr,			/* pad character */
	*TEstr,			/* end cursor motion mode */
	*TIstr,			/* begin cursor motion mode */
	*VIstr,			/* make cursor invisible */
	*VEstr;			/* make cursor appear normal */
char
	*SEstr,			/* end standout mode */
	*SOstr;			/* begin standout mode */
static int
	COnum,			/* co# value */
	LInum,			/* li# value */
	MSflag;			/* can move in standout mode */


struct tcsinfo {		/* termcap string info; some abbrevs above */
	char tcname[3];
	char **tcaddr;
} tcstrings[] = {
	{"bc", &bcstr},
	{"ce", &CEstr},
	{"cl", &CLstr},
	{"cm", &CMstr},
#ifdef unneeded
	{"cr", &CRstr},
#endif
	{"le", &BC},		/* move cursor left one space */
	{"pc", &pcstr},
	{"se", &SEstr},
	{"so", &SOstr},
	{"te", &TEstr},
	{"ti", &TIstr},
	{"vi", &VIstr},
	{"ve", &VEstr},
	{"up", &UP},		/* cursor up */
	{ {0}, NULL}
};

/* This is where we will actually stuff the information */

static char combuf[1024], tbuf[1024];


/*
 * Routine used by tputs().
 */
int
put(int c)
{

	return (putchar(c));
}

/*
 * putstr() is for unpadded strings (either as in termcap(5) or
 * simply literal strings); putpad() is for padded strings with
 * count=1.  (See screen.h for putpad().)
 */
#define	putstr(s)	(void)fputs(s, stdout)
#define	moveto(r, c)	putpad(tgoto(CMstr, c, r))

/*
 * Set up from termcap.
 */
void
scr_init(void)
{
	static int bsflag, xsflag, sgnum;
#ifdef unneeded
	static int ncflag;
#endif
	char *term, *fill;
	static struct tcninfo {	/* termcap numeric and flag info */
		char tcname[3];
		int *tcaddr;
	} tcflags[] = {
		{"bs", &bsflag},
		{"ms", &MSflag},
#ifdef unneeded
		{"nc", &ncflag},
#endif
		{"xs", &xsflag},
		{ {0}, NULL}
	}, tcnums[] = {
		{"co", &COnum},
		{"li", &LInum},
		{"sg", &sgnum},
		{ {0}, NULL}
	};
	
	if ((term = getenv("TERM")) == NULL)
		stop("you must set the TERM environment variable");
	if (tgetent(tbuf, term) <= 0)
		stop("cannot find your termcap");
	fill = combuf;
	{
		struct tcsinfo *p;

		for (p = tcstrings; p->tcaddr; p++)
			*p->tcaddr = tgetstr(p->tcname, &fill);
	}
	if (classic)
		SOstr = SEstr = NULL;
	{
		struct tcninfo *p;

		for (p = tcflags; p->tcaddr; p++)
			*p->tcaddr = tgetflag(p->tcname);
		for (p = tcnums; p->tcaddr; p++)
			*p->tcaddr = tgetnum(p->tcname);
	}
	if (bsflag)
		BC = "\b";
	else if (BC == NULL && bcstr != NULL)
		BC = bcstr;
	if (CLstr == NULL)
		stop("cannot clear screen");
	if (CMstr == NULL || UP == NULL || BC == NULL)
		stop("cannot do random cursor positioning via tgoto()");
	PC = pcstr ? *pcstr : 0;
	if (sgnum > 0 || xsflag)
		SOstr = SEstr = NULL;
#ifdef unneeded
	if (ncflag)
		CRstr = NULL;
	else if (CRstr == NULL)
		CRstr = "\r";
#endif
}

/* this foolery is needed to modify tty state `atomically' */
static jmp_buf scr_onstop;

static void
stopset(int sig)
{
	sigset_t sigset;

	(void) signal(sig, SIG_DFL);
	(void) kill(getpid(), sig);
	sigemptyset(&sigset);
	sigaddset(&sigset, sig);
	(void) sigprocmask(SIG_UNBLOCK, &sigset, (sigset_t *)0);
	longjmp(scr_onstop, 1);
}

static void
scr_stop(int sig)
{
	sigset_t sigset;

	scr_end();
	(void) kill(getpid(), sig);
	sigemptyset(&sigset);
	sigaddset(&sigset, sig);
	(void) sigprocmask(SIG_UNBLOCK, &sigset, (sigset_t *)0);
	scr_set();
	scr_msg(key_msg, 1);
}

/*
 * Set up screen mode.
 */
void
scr_set(void)
{
	struct winsize ws;
	struct termios newtt;
	sigset_t sigset, osigset;
	void (*ttou)(int);

	sigemptyset(&sigset);
	sigaddset(&sigset, SIGTSTP);
	sigaddset(&sigset, SIGTTOU);
	(void) sigprocmask(SIG_BLOCK, &sigset, &osigset);
	if ((tstp = signal(SIGTSTP, stopset)) == SIG_IGN)
		(void) signal(SIGTSTP, SIG_IGN);
	if ((ttou = signal(SIGTTOU, stopset)) == SIG_IGN)
		(void) signal(SIGTTOU, SIG_IGN);
	/*
	 * At last, we are ready to modify the tty state.  If
	 * we stop while at it, stopset() above will longjmp back
	 * to the setjmp here and we will start over.
	 */
	(void) setjmp(scr_onstop);
	(void) sigprocmask(SIG_SETMASK, &osigset, (sigset_t *)0);
	Rows = 0, Cols = 0;
	if (ioctl(0, TIOCGWINSZ, &ws) == 0) {
		Rows = ws.ws_row;
		Cols = ws.ws_col;
	}
	if (Rows == 0)
		Rows = LInum;
	if (Cols == 0)
	Cols = COnum;
	if (Rows < MINROWS || Cols < MINCOLS) {
		char smallscr[55];

		(void)snprintf(smallscr, sizeof(smallscr),
		    "the screen is too small (must be at least %dx%d)",
		    MINROWS, MINCOLS);
		stop(smallscr);
	}
	if (tcgetattr(0, &oldtt) == -1)
		stop("tcgetattr() fails");
	newtt = oldtt;
	newtt.c_lflag &= ~(ICANON|ECHO);
	newtt.c_oflag &= ~OXTABS;
	if (tcsetattr(0, TCSADRAIN, &newtt) == -1)
		stop("tcsetattr() fails");
	(void) sigprocmask(SIG_BLOCK, &sigset, &osigset);

	/*
	 * We made it.  We are now in screen mode, modulo TIstr
	 * (which we will fix immediately).
	 */
	if (TIstr)
		putstr(TIstr);	/* termcap(5) says this is not padded */
	if (VIstr)
		putstr(VIstr);	/* termcap(5) says this is not padded */
	if (tstp != SIG_IGN)
		(void) signal(SIGTSTP, scr_stop);
	if (ttou != SIG_IGN)
		(void) signal(SIGTTOU, ttou);

	isset = 1;
	(void) sigprocmask(SIG_SETMASK, &osigset, (sigset_t *)0);
	scr_clear();
}

/*
 * End screen mode.
 */
void
scr_end(void)
{
	sigset_t sigset, osigset;

	sigemptyset(&sigset);
	sigaddset(&sigset, SIGTSTP);
	sigaddset(&sigset, SIGTTOU);
	(void) sigprocmask(SIG_BLOCK, &sigset, &osigset);
	/* move cursor to last line */
	if (LLstr)
		putstr(LLstr);	/* termcap(5) says this is not padded */
	else
		moveto(Rows - 1, 0);
	/* exit screen mode */
	if (TEstr)
		putstr(TEstr);	/* termcap(5) says this is not padded */
	if (VEstr)
		putstr(VEstr);	/* termcap(5) says this is not padded */
	(void) fflush(stdout);
	(void) tcsetattr(0, TCSADRAIN, &oldtt);
	isset = 0;
	/* restore signals */
	(void) signal(SIGTSTP, tstp);
	(void) sigprocmask(SIG_SETMASK, &osigset, (sigset_t *)0);
}

void
stop(char *why)
{

	if (isset)
		scr_end();
	errx(1, "aborting: %s", why);
}

/*
 * Clear the screen, forgetting the current contents in the process.
 */
void
scr_clear(void)
{

	putpad(CLstr);
	curscore = -1;
	memset((char *)curscreen, 0, sizeof(curscreen));
}

typedef cell regcell;

/*
 * Update the screen.
 */
void
scr_update(void)
{
	cell *bp, *sp;
	regcell so, cur_so = 0;
	int i, ccol, j;
	sigset_t sigset, osigset;
	static const struct shape *lastshape;

	sigemptyset(&sigset);
	sigaddset(&sigset, SIGTSTP);
	(void) sigprocmask(SIG_BLOCK, &sigset, &osigset);

	/* always leave cursor after last displayed point */
	curscreen[D_LAST * B_COLS - 1] = -1;

	if (score != curscore) {
		if (HOstr)
			putpad(HOstr);
		else
			moveto(0, 0);
		(void) printf("Score: %d", score);
		curscore = score;
	}

	/* draw preview of next pattern */
	if (showpreview && (nextshape != lastshape)) {
		static int r=5, c=2;
		int tr, tc, t;

		lastshape = nextshape;

		/* clean */
		putpad(SEstr);
		moveto(r-1, c-1); putstr("          ");
		moveto(r,   c-1); putstr("          ");
		moveto(r+1, c-1); putstr("          ");
		moveto(r+2, c-1); putstr("          ");

		moveto(r-3, c-2);
		putstr("Next shape:");

		/* draw */
		if (SOstr)
			putpad(SOstr);
		moveto(r, 2 * c);
		putstr(SOstr ? "  " : "[]");
		for (i = 0; i < 3; i++) {
			t = c + r * B_COLS;
			t += nextshape->off[i];

			tr = t / B_COLS;
			tc = t % B_COLS;

			moveto(tr, 2*tc);
			putstr(SOstr ? "  " : "[]");
		}
		putpad(SEstr);
	}

	bp = &board[D_FIRST * B_COLS];
	sp = &curscreen[D_FIRST * B_COLS];
	for (j = D_FIRST; j < D_LAST; j++) {
		ccol = -1;
		for (i = 0; i < B_COLS; bp++, sp++, i++) {
			if (*sp == (so = *bp))
				continue;
			*sp = so;
			if (i != ccol) {
				if (cur_so && MSflag) {
					putpad(SEstr);
					cur_so = 0;
				}
				moveto(RTOD(j), CTOD(i));
			}
			if (SOstr) {
				if (so != cur_so) {
					putpad(so ? SOstr : SEstr);
					cur_so = so;
				}
				putstr("  ");
			} else
				putstr(so ? "[]" : "  ");
			ccol = i + 1;
			/*
			 * Look ahead a bit, to avoid extra motion if
			 * we will be redrawing the cell after the next.
			 * Motion probably takes four or more characters,
			 * so we save even if we rewrite two cells
			 * `unnecessarily'.  Skip it all, though, if
			 * the next cell is a different color.
			 */
#define	STOP (B_COLS - 3)
			if (i > STOP || sp[1] != bp[1] || so != bp[1])
				continue;
			if (sp[2] != bp[2])
				sp[1] = -1;
			else if (i < STOP && so == bp[2] && sp[3] != bp[3]) {
				sp[2] = -1;
				sp[1] = -1;
			}
		}
	}
	if (cur_so)
		putpad(SEstr);
	(void) fflush(stdout);
	(void) sigprocmask(SIG_SETMASK, &osigset, (sigset_t *)0);
}

/*
 * Write a message (set!=0), or clear the same message (set==0).
 * (We need its length in case we have to overwrite with blanks.)
 */
void
scr_msg(char *s, int set)
{
	
	if (set || CEstr == NULL) {
		int l = strlen(s);

		moveto(Rows - 2, ((Cols - l) >> 1) - 1);
		if (set)
			putstr(s);
		else
			while (--l >= 0)
				(void) putchar(' ');
	} else {
		moveto(Rows - 2, 0);
		putpad(CEstr);
	}
}
