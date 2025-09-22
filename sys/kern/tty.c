/*	$OpenBSD: tty.c,v 1.180 2025/06/12 20:37:58 deraadt Exp $	*/
/*	$NetBSD: tty.c,v 1.68.4.2 1996/06/06 16:04:52 thorpej Exp $	*/

/*-
 * Copyright (c) 1982, 1986, 1990, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 *	@(#)tty.c	8.8 (Berkeley) 1/21/94
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/ioctl.h>
#include <sys/proc.h>
#define	TTYDEFCHARS
#include <sys/tty.h>
#undef	TTYDEFCHARS
#include <sys/fcntl.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/kernel.h>
#include <sys/vnode.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/msgbuf.h>
#include <sys/signalvar.h>
#include <sys/resourcevar.h>
#include <sys/sysctl.h>
#include <sys/pool.h>
#include <sys/unistd.h>
#include <sys/pledge.h>

#include <sys/namei.h>

#include <uvm/uvm_extern.h>

#include <dev/cons.h>

#include "pty.h"

static int ttnread(struct tty *);
static void ttyblock(struct tty *);
void ttyunblock(struct tty *);
static void ttyecho(int, struct tty *);
static void ttyrubo(struct tty *, int);
int	filt_ttyread(struct knote *kn, long hint);
void 	filt_ttyrdetach(struct knote *kn);
int	filt_ttywrite(struct knote *kn, long hint);
void 	filt_ttywdetach(struct knote *kn);
int	filt_ttyexcept(struct knote *kn, long hint);
void	ttystats_init(struct itty **, int *, size_t *);
int	ttywait_nsec(struct tty *, uint64_t);
int	ttysleep_nsec(struct tty *, void *, int, char *, uint64_t);

/* Symbolic sleep message strings. */
char ttclos[]	= "ttycls";
char ttopen[]	= "ttyopn";
char ttybg[]	= "ttybg";
char ttyin[]	= "ttyin";
char ttyout[]	= "ttyout";

/*
 * Table with character classes and parity. The 8th bit indicates parity,
 * the 7th bit indicates the character is an alphameric or underscore (for
 * ALTWERASE), and the low 6 bits indicate delay type.  If the low 6 bits
 * are 0 then the character needs no special processing on output; classes
 * other than 0 might be translated or (not currently) require delays.
 */
#define	E	0x00	/* Even parity. */
#define	O	0x80	/* Odd parity. */
#define	PARITY(c)	(char_type[c] & O)

#define	ALPHA	0x40	/* Alpha or underscore. */
#define	ISALPHA(c)	(char_type[(c) & TTY_CHARMASK] & ALPHA)

#define	CCLASSMASK	0x3f
#define	CCLASS(c)	(char_type[c] & CCLASSMASK)

#define	BS	BACKSPACE
#define	CC	CONTROL
#define	CR	RETURN
#define	NA	ORDINARY | ALPHA
#define	NL	NEWLINE
#define	NO	ORDINARY
#define	TB	TAB
#define	VT	VTAB

u_char const char_type[] = {
	E|CC, O|CC, O|CC, E|CC, O|CC, E|CC, E|CC, O|CC,	/* nul - bel */
	O|BS, E|TB, E|NL, O|CC, E|VT, O|CR, O|CC, E|CC, /* bs - si */
	O|CC, E|CC, E|CC, O|CC, E|CC, O|CC, O|CC, E|CC, /* dle - etb */
	E|CC, O|CC, O|CC, E|CC, O|CC, E|CC, E|CC, O|CC, /* can - us */
	O|NO, E|NO, E|NO, O|NO, E|NO, O|NO, O|NO, E|NO, /* sp - ' */
	E|NO, O|NO, O|NO, E|NO, O|NO, E|NO, E|NO, O|NO, /* ( - / */
	E|NA, O|NA, O|NA, E|NA, O|NA, E|NA, E|NA, O|NA, /* 0 - 7 */
	O|NA, E|NA, E|NO, O|NO, E|NO, O|NO, O|NO, E|NO, /* 8 - ? */
	O|NO, E|NA, E|NA, O|NA, E|NA, O|NA, O|NA, E|NA, /* @ - G */
	E|NA, O|NA, O|NA, E|NA, O|NA, E|NA, E|NA, O|NA, /* H - O */
	E|NA, O|NA, O|NA, E|NA, O|NA, E|NA, E|NA, O|NA, /* P - W */
	O|NA, E|NA, E|NA, O|NO, E|NO, O|NO, O|NO, O|NA, /* X - _ */
	E|NO, O|NA, O|NA, E|NA, O|NA, E|NA, E|NA, O|NA, /* ` - g */
	O|NA, E|NA, E|NA, O|NA, E|NA, O|NA, O|NA, E|NA, /* h - o */
	O|NA, E|NA, E|NA, O|NA, E|NA, O|NA, O|NA, E|NA, /* p - w */
	E|NA, O|NA, O|NA, E|NO, O|NO, E|NO, E|NO, O|CC, /* x - del */
	/*
	 * Meta chars; should be settable per character set;
	 * for now, treat them all as normal characters.
	 */
	NA,   NA,   NA,   NA,   NA,   NA,   NA,   NA,
	NA,   NA,   NA,   NA,   NA,   NA,   NA,   NA,
	NA,   NA,   NA,   NA,   NA,   NA,   NA,   NA,
	NA,   NA,   NA,   NA,   NA,   NA,   NA,   NA,
	NA,   NA,   NA,   NA,   NA,   NA,   NA,   NA,
	NA,   NA,   NA,   NA,   NA,   NA,   NA,   NA,
	NA,   NA,   NA,   NA,   NA,   NA,   NA,   NA,
	NA,   NA,   NA,   NA,   NA,   NA,   NA,   NA,
	NA,   NA,   NA,   NA,   NA,   NA,   NA,   NA,
	NA,   NA,   NA,   NA,   NA,   NA,   NA,   NA,
	NA,   NA,   NA,   NA,   NA,   NA,   NA,   NA,
	NA,   NA,   NA,   NA,   NA,   NA,   NA,   NA,
	NA,   NA,   NA,   NA,   NA,   NA,   NA,   NA,
	NA,   NA,   NA,   NA,   NA,   NA,   NA,   NA,
	NA,   NA,   NA,   NA,   NA,   NA,   NA,   NA,
	NA,   NA,   NA,   NA,   NA,   NA,   NA,   NA,
};
#undef	BS
#undef	CC
#undef	CR
#undef	NA
#undef	NL
#undef	NO
#undef	TB
#undef	VT

#define	islower(c)	((c) >= 'a' && (c) <= 'z')
#define	isupper(c)	((c) >= 'A' && (c) <= 'Z')

#define	tolower(c)	((c) - 'A' + 'a')
#define	toupper(c)	((c) - 'a' + 'A')

struct ttylist_head ttylist;	/* TAILQ_HEAD */
int tty_count;
struct rwlock ttylist_lock = RWLOCK_INITIALIZER("ttylist");

int64_t tk_cancc, tk_nin, tk_nout, tk_rawcc;

/*
 * Initial open of tty, or (re)entry to standard tty line discipline.
 */
int
ttyopen(dev_t device, struct tty *tp, struct proc *p)
{
	int s;

	s = spltty();
	tp->t_dev = device;
	if (!ISSET(tp->t_state, TS_ISOPEN)) {
		SET(tp->t_state, TS_ISOPEN);
		memset(&tp->t_winsize, 0, sizeof(tp->t_winsize));
		tp->t_column = 0;
	}
	CLR(tp->t_state, TS_WOPEN);
	splx(s);
	return (0);
}

/*
 * Handle close() on a tty line: flush and set to initial state,
 * bumping generation number so that pending read/write calls
 * can detect recycling of the tty.
 */
int
ttyclose(struct tty *tp)
{
	if (constty == tp)
		constty = NULL;

	ttyflush(tp, FREAD | FWRITE);

	tp->t_gen++;
	tp->t_pgrp = NULL;
	if (tp->t_session)
		SESSRELE(tp->t_session);
	tp->t_session = NULL;
	tp->t_state = 0;
	return (0);
}

#define	FLUSHQ(q) {							\
	if ((q)->c_cc)							\
		ndflush(q, (q)->c_cc);					\
}

/* Is 'c' a line delimiter ("break" character)? */
#define	TTBREAKC(c, lflag)						\
	((c) == '\n' || (((c) == cc[VEOF] || (c) == cc[VEOL] ||		\
	((c) == cc[VEOL2] && (lflag & IEXTEN))) && (c) != _POSIX_VDISABLE))


/*
 * Process input of a single character received on a tty.  Returns 0 normally,
 * 1 if a costly operation was reached.
 */
int
ttyinput(int c, struct tty *tp)
{
	int iflag, lflag;
	u_char *cc;
	int i, error, ret = 0;
	int s;

	enqueue_randomness(tp->t_dev << 8 | c);
	/*
	 * If receiver is not enabled, drop it.
	 */
	if (!ISSET(tp->t_cflag, CREAD))
		return (0);

	/*
	 * If input is pending take it first.
	 */
	lflag = tp->t_lflag;
	s = spltty();
	if (ISSET(lflag, PENDIN))
		ttypend(tp);
	splx(s);
	/*
	 * Gather stats.
	 */
	if (ISSET(lflag, ICANON)) {
		++tk_cancc;
		++tp->t_cancc;
	} else {
		++tk_rawcc;
		++tp->t_rawcc;
	}
	++tk_nin;

	/* Handle exceptional conditions (break, parity, framing). */
	cc = tp->t_cc;
	iflag = tp->t_iflag;
	if ((error = (ISSET(c, TTY_ERRORMASK))) != 0) {
		CLR(c, TTY_ERRORMASK);
		if (ISSET(error, TTY_FE) && !c) {	/* Break. */
			if (ISSET(iflag, IGNBRK))
				return (0);
			ttyflush(tp, FREAD | FWRITE);
			if (ISSET(iflag, BRKINT)) {
			    pgsignal(tp->t_pgrp, SIGINT, 1);
			    goto endcase;
			}
			else if (ISSET(iflag, PARMRK))
				goto parmrk;
		} else if ((ISSET(error, TTY_PE) && ISSET(iflag, INPCK)) ||
		    ISSET(error, TTY_FE)) {
			if (ISSET(iflag, IGNPAR))
				goto endcase;
			else if (ISSET(iflag, PARMRK)) {
parmrk:				(void)putc(0377 | TTY_QUOTE, &tp->t_rawq);
				if (ISSET(iflag, ISTRIP) || c != 0377)
					(void)putc(0 | TTY_QUOTE, &tp->t_rawq);
				(void)putc(c | TTY_QUOTE, &tp->t_rawq);
				goto endcase;
			} else
				c = 0;
		}
	}
	if (c == 0377 && !ISSET(iflag, ISTRIP) && ISSET(iflag, PARMRK))
		goto parmrk;

	/*
	 * In tandem mode, check high water mark.
	 */
	if (ISSET(iflag, IXOFF) || ISSET(tp->t_cflag, CHWFLOW))
		ttyblock(tp);
	if (!ISSET(tp->t_state, TS_TYPEN) && ISSET(iflag, ISTRIP))
		CLR(c, 0x80);
	if (!ISSET(lflag, EXTPROC)) {
		/*
		 * Check for literal nexting very first
		 */
		if (ISSET(tp->t_state, TS_LNCH)) {
			SET(c, TTY_QUOTE);
			CLR(tp->t_state, TS_LNCH);
		}
		/*
		 * Scan for special characters.  This code
		 * is really just a big case statement with
		 * non-constant cases.  The bottom of the
		 * case statement is labeled ``endcase'', so goto
		 * it after a case match, or similar.
		 */

		/*
		 * Control chars which aren't controlled
		 * by ICANON, ISIG, or IXON.
		 */
		if (ISSET(lflag, IEXTEN)) {
			if (CCEQ(cc[VLNEXT], c)) {
				if (ISSET(lflag, ECHO)) {
					if (ISSET(lflag, ECHOE)) {
						(void)ttyoutput('^', tp);
						(void)ttyoutput('\b', tp);
					} else
						ttyecho(c, tp);
				}
				SET(tp->t_state, TS_LNCH);
				goto endcase;
			}
			if (CCEQ(cc[VDISCARD], c)) {
				if (ISSET(lflag, FLUSHO))
					CLR(tp->t_lflag, FLUSHO);
				else {
					ttyflush(tp, FWRITE);
					ttyecho(c, tp);
					if (tp->t_rawq.c_cc + tp->t_canq.c_cc)
						ret = ttyretype(tp);
					SET(tp->t_lflag, FLUSHO);
				}
				goto startoutput;
			}
		}
		/*
		 * Signals.
		 */
		if (ISSET(lflag, ISIG)) {
			if (CCEQ(cc[VINTR], c) || CCEQ(cc[VQUIT], c)) {
				if (!ISSET(lflag, NOFLSH))
					ttyflush(tp, FREAD | FWRITE);
				ttyecho(c, tp);
				pgsignal(tp->t_pgrp,
				    CCEQ(cc[VINTR], c) ? SIGINT : SIGQUIT, 1);
				goto endcase;
			}
			if (CCEQ(cc[VSUSP], c)) {
				if (!ISSET(lflag, NOFLSH))
					ttyflush(tp, FREAD);
				ttyecho(c, tp);
				pgsignal(tp->t_pgrp, SIGTSTP, 1);
				goto endcase;
			}
		}
		/*
		 * Handle start/stop characters.
		 */
		if (ISSET(iflag, IXON)) {
			if (CCEQ(cc[VSTOP], c)) {
				if (!ISSET(tp->t_state, TS_TTSTOP)) {
					SET(tp->t_state, TS_TTSTOP);
					(*cdevsw[major(tp->t_dev)].d_stop)(tp,
					   0);
					return (0);
				}
				if (!CCEQ(cc[VSTART], c))
					return (0);
				/*
				 * if VSTART == VSTOP then toggle
				 */
				goto endcase;
			}
			if (CCEQ(cc[VSTART], c))
				goto restartoutput;
		}
		/*
		 * IGNCR, ICRNL, & INLCR
		 */
		if (c == '\r') {
			if (ISSET(iflag, IGNCR))
				goto endcase;
			else if (ISSET(iflag, ICRNL))
				c = '\n';
		} else if (c == '\n' && ISSET(iflag, INLCR))
			c = '\r';
	}
	if (!ISSET(tp->t_lflag, EXTPROC) && ISSET(lflag, ICANON)) {
		/*
		 * From here on down canonical mode character
		 * processing takes place.
		 */
		/*
		 * upper case or specials with IUCLC and XCASE
		 */
		if (ISSET(lflag, XCASE) && ISSET(iflag, IUCLC)) {
			if (ISSET(tp->t_state, TS_BKSL)) {
				CLR(tp->t_state, TS_BKSL);
				switch (c) {
				case '\'':
					c = '`';
					break;
				case '!':
					c = '|';
					break;
				case '^':
					c = '~';
					break;
				case '(':
					c = '{';
					break;
				case ')':
					c = '}';
					break;
				}
			}
			else if (c == '\\') {
				SET(tp->t_state, TS_BKSL);
				goto endcase;
			}
			else if (isupper(c))
				c = tolower(c);
		}
		else if (ISSET(iflag, IUCLC) && isupper(c))
			c = tolower(c);
		/*
		 * erase (^H / ^?)
		 */
		if (CCEQ(cc[VERASE], c)) {
			if (tp->t_rawq.c_cc)
				ret = ttyrub(unputc(&tp->t_rawq), tp);
			goto endcase;
		}
		/*
		 * kill (^U)
		 */
		if (CCEQ(cc[VKILL], c)) {
			if (ISSET(lflag, ECHOKE) &&
			    tp->t_rawq.c_cc == tp->t_rocount &&
			    !ISSET(lflag, ECHOPRT)) {
				while (tp->t_rawq.c_cc)
					if (ttyrub(unputc(&tp->t_rawq), tp))
						ret = 1;
			} else {
				ttyecho(c, tp);
				if (ISSET(lflag, ECHOK) ||
				    ISSET(lflag, ECHOKE))
					ttyecho('\n', tp);
				FLUSHQ(&tp->t_rawq);
				tp->t_rocount = 0;
			}
			CLR(tp->t_state, TS_LOCAL);
			goto endcase;
		}
		/*
		 * word erase (^W)
		 */
		if (CCEQ(cc[VWERASE], c) && ISSET(lflag, IEXTEN)) {
			int alt = ISSET(lflag, ALTWERASE);
			int ctype;

			/*
			 * erase whitespace
			 */
			while ((c = unputc(&tp->t_rawq)) == ' ' || c == '\t')
				if (ttyrub(c, tp))
					ret = 1;
			if (c == -1)
				goto endcase;
			/*
			 * erase last char of word and remember the
			 * next chars type (for ALTWERASE)
			 */
			if (ttyrub(c, tp))
				ret = 1;
			c = unputc(&tp->t_rawq);
			if (c == -1)
				goto endcase;
			if (c == ' ' || c == '\t') {
				(void)putc(c, &tp->t_rawq);
				goto endcase;
			}
			ctype = ISALPHA(c);
			/*
			 * erase rest of word
			 */
			do {
				if (ttyrub(c, tp))
					ret = 1;
				c = unputc(&tp->t_rawq);
				if (c == -1)
					goto endcase;
			} while (c != ' ' && c != '\t' &&
			    (alt == 0 || ISALPHA(c) == ctype));
			(void)putc(c, &tp->t_rawq);
			goto endcase;
		}
		/*
		 * reprint line (^R)
		 */
		if (CCEQ(cc[VREPRINT], c) && ISSET(lflag, IEXTEN)) {
			ret = ttyretype(tp);
			goto endcase;
		}
		/*
		 * ^T - kernel info and generate SIGINFO
		 */
		if (CCEQ(cc[VSTATUS], c) && ISSET(lflag, IEXTEN)) {
			if (ISSET(lflag, ISIG))
				pgsignal(tp->t_pgrp, SIGINFO, 1);
			if (!ISSET(lflag, NOKERNINFO))
				ttyinfo(tp);
			goto endcase;
		}
	}
	/*
	 * Check for input buffer overflow
	 */
	if (tp->t_rawq.c_cc + tp->t_canq.c_cc >= TTYHOG(tp)) {
		if (ISSET(iflag, IMAXBEL)) {
			if (tp->t_outq.c_cc < tp->t_hiwat)
				(void)ttyoutput(CTRL('g'), tp);
		} else
			ttyflush(tp, FREAD | FWRITE);
		goto endcase;
	}
	/*
	 * Put data char in q for user and
	 * wakeup on seeing a line delimiter.
	 */
	if (putc(c, &tp->t_rawq) >= 0) {
		if (!ISSET(lflag, ICANON)) {
			ttwakeup(tp);
			ttyecho(c, tp);
			goto endcase;
		}
		if (TTBREAKC(c, lflag)) {
			tp->t_rocount = 0;
			catq(&tp->t_rawq, &tp->t_canq);
			ttwakeup(tp);
		} else if (tp->t_rocount++ == 0)
			tp->t_rocol = tp->t_column;
		if (ISSET(tp->t_state, TS_ERASE)) {
			/*
			 * end of prterase \.../
			 */
			CLR(tp->t_state, TS_ERASE);
			(void)ttyoutput('/', tp);
		}
		i = tp->t_column;
		ttyecho(c, tp);
		if (CCEQ(cc[VEOF], c) && ISSET(lflag, ECHO)) {
			/*
			 * Place the cursor over the '^' of the ^D.
			 */
			i = min(2, tp->t_column - i);
			while (i > 0) {
				(void)ttyoutput('\b', tp);
				i--;
			}
		}
	}
endcase:
	/*
	 * IXANY means allow any character to restart output.
	 */
	if (ISSET(tp->t_state, TS_TTSTOP) &&
	    !ISSET(iflag, IXANY) && cc[VSTART] != cc[VSTOP])
		return (ret);
restartoutput:
	CLR(tp->t_lflag, FLUSHO);
	CLR(tp->t_state, TS_TTSTOP);
startoutput:
	ttstart(tp);
	return (ret);
}

/*
 * Output a single character on a tty, doing output processing
 * as needed (expanding tabs, newline processing, etc.).
 * Returns < 0 if succeeds, otherwise returns char to resend.
 * Must be recursive.
 */
int
ttyoutput(int c, struct tty *tp)
{
	long oflag;
	int col, notout, s, c2;

	oflag = tp->t_oflag;
	if (!ISSET(oflag, OPOST)) {
		tk_nout++;
		tp->t_outcc++;
		if (!ISSET(tp->t_lflag, FLUSHO) && putc(c, &tp->t_outq))
			return (c);
		return (-1);
	}
	/*
	 * Do tab expansion if OXTABS is set.  Special case if we external
	 * processing, we don't do the tab expansion because we'll probably
	 * get it wrong.  If tab expansion needs to be done, let it happen
	 * externally.
	 */
	CLR(c, ~TTY_CHARMASK);
	if (c == '\t' &&
	    ISSET(oflag, OXTABS) && !ISSET(tp->t_lflag, EXTPROC)) {
		c = 8 - (tp->t_column & 7);
		if (ISSET(tp->t_lflag, FLUSHO)) {
			notout = 0;
		} else {
			s = spltty();		/* Don't interrupt tabs. */
			notout = b_to_q("        ", c, &tp->t_outq);
			c -= notout;
			tk_nout += c;
			tp->t_outcc += c;
			splx(s);
		}
		tp->t_column += c;
		return (notout ? '\t' : -1);
	}
	if (c == CEOT && ISSET(oflag, ONOEOT))
		return (-1);

	/*
	 * Newline translation: if ONLCR is set,
	 * translate newline into "\r\n".  If OCRNL
	 * is set, translate '\r' into '\n'.
	 */
	if (c == '\n' && ISSET(tp->t_oflag, ONLCR)) {
		tk_nout++;
		tp->t_outcc++;
		if (!ISSET(tp->t_lflag, FLUSHO) && putc('\r', &tp->t_outq))
			return (c);
		tp->t_column = 0;
	}
	else if (c == '\r' && ISSET(tp->t_oflag, OCRNL))
		c = '\n';

	if (ISSET(tp->t_oflag, OLCUC) && islower(c))
		c = toupper(c);
	else if (ISSET(tp->t_oflag, OLCUC) && ISSET(tp->t_lflag, XCASE)) {
		c2 = c;
		switch (c) {
		case '`':
			c2 = '\'';
			break;
		case '|':
			c2 = '!';
			break;
		case '~':
			c2 = '^';
			break;
		case '{':
			c2 = '(';
			break;
		case '}':
			c2 = ')';
			break;
		}
		if (c == '\\' || isupper(c) || c != c2) {
			tk_nout++;
			tp->t_outcc++;
			if (putc('\\', &tp->t_outq))
				return (c);
			c = c2;
		}
	}
	if (ISSET(tp->t_oflag, ONOCR) && c == '\r' && tp->t_column == 0)
		return (-1);

	tk_nout++;
	tp->t_outcc++;
	if (!ISSET(tp->t_lflag, FLUSHO) && putc(c, &tp->t_outq))
		return (c);

	col = tp->t_column;
	switch (CCLASS(c)) {
	case BACKSPACE:
		if (col > 0)
			--col;
		break;
	case CONTROL:
		break;
	case NEWLINE:
		if (ISSET(tp->t_oflag, ONLRET) || ISSET(tp->t_oflag, OCRNL))
			col = 0;
		break;
	case RETURN:
		col = 0;
		break;
	case ORDINARY:
		++col;
		break;
	case TAB:
		col = (col + 8) & ~7;
		break;
	}
	tp->t_column = col;
	return (-1);
}

/*
 * Ioctls for all tty devices.  Called after line-discipline specific ioctl
 * has been called to do discipline-specific functions and/or reject any
 * of these ioctl commands.
 */
int
ttioctl(struct tty *tp, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	extern int nlinesw;
	struct process *pr = p->p_p;
	int s, error;

	/* If the ioctl involves modification, hang if in the background. */
	switch (cmd) {
	case  FIOSETOWN:
	case  TIOCFLUSH:
	case  TIOCDRAIN:
	case  TIOCSBRK:
	case  TIOCCBRK:
	case  TIOCSETA:
	case  TIOCSETD:
	case  TIOCSETAF:
	case  TIOCSETAW:
	case  TIOCSPGRP:
	case  TIOCSTAT:
	case  TIOCSWINSZ:
		while (isbackground(pr, tp) &&
		    (pr->ps_flags & PS_PPWAIT) == 0 &&
		    !sigismasked(p, SIGTTOU)) {
			if (pr->ps_pgrp->pg_jobc == 0)
				return (EIO);
			pgsignal(pr->ps_pgrp, SIGTTOU, 1);
			error = ttysleep(tp, &lbolt, TTOPRI | PCATCH,
			    ttybg);
			if (error)
				return (error);
		}
		break;
	}

	switch (cmd) {			/* Process the ioctl. */
	case FIOASYNC:			/* set/clear async i/o */
		s = spltty();
		if (*(int *)data)
			SET(tp->t_state, TS_ASYNC);
		else
			CLR(tp->t_state, TS_ASYNC);
		splx(s);
		break;
	case FIONREAD:			/* get # bytes to read */
		s = spltty();
		*(int *)data = ttnread(tp);
		splx(s);
		break;
	case TIOCEXCL:			/* set exclusive use of tty */
		s = spltty();
		SET(tp->t_state, TS_XCLUDE);
		splx(s);
		break;
	case TIOCFLUSH: {		/* flush buffers */
		int flags = *(int *)data;

		if (flags == 0)
			flags = FREAD | FWRITE;
		else
			flags &= FREAD | FWRITE;
		ttyflush(tp, flags);
		break;
	}
	case TIOCCONS: {		/* become virtual console */
		if (*(int *)data) {
			struct nameidata nid;

			if (constty != NULL && constty != tp &&
			    ISSET(constty->t_state, TS_CARR_ON | TS_ISOPEN) ==
			    (TS_CARR_ON | TS_ISOPEN))
				return (EBUSY);

			/* ensure user can open the real console */
			NDINIT(&nid, LOOKUP, FOLLOW, UIO_SYSSPACE, "/dev/console", p);
			nid.ni_pledge = PLEDGE_RPATH | PLEDGE_WPATH;
			nid.ni_unveil = UNVEIL_READ | UNVEIL_WRITE;
			error = namei(&nid);
			if (error)
				return (error);
			vn_lock(nid.ni_vp, LK_EXCLUSIVE | LK_RETRY);
			error = VOP_ACCESS(nid.ni_vp, VREAD, p->p_ucred, p);
			VOP_UNLOCK(nid.ni_vp);
			vrele(nid.ni_vp);
			if (error)
				return (error);

			constty = tp;
		} else if (tp == constty)
			constty = NULL;
		break;
	}
	case TIOCDRAIN:			/* wait till output drained */
		if ((error = ttywait(tp)) != 0)
			return (error);
		break;
	case TIOCGETA: {		/* get termios struct */
		struct termios *t = (struct termios *)data;

		memcpy(t, &tp->t_termios, sizeof(struct termios));
		break;
	}
	case TIOCGETD:			/* get line discipline */
		*(int *)data = tp->t_line;
		break;
	case TIOCGWINSZ:		/* get window size */
		*(struct winsize *)data = tp->t_winsize;
		break;
	case TIOCGTSTAMP:
		s = spltty();
		*(struct timeval *)data = tp->t_tv;
		splx(s);
		break;
	case FIOGETOWN:			/* get pgrp of tty */
		if (!isctty(pr, tp) && suser(p))
			return (ENOTTY);
		*(int *)data = tp->t_pgrp ? -tp->t_pgrp->pg_id : 0;
		break;
	case TIOCGPGRP:			/* get pgrp of tty */
		if (!isctty(pr, tp) && suser(p))
			return (ENOTTY);
		*(int *)data = tp->t_pgrp ? tp->t_pgrp->pg_id : NO_PID;
		break;
	case TIOCGSID:			/* get sid of tty */
		if (!isctty(pr, tp))
			return (ENOTTY);
		*(int *)data = tp->t_session->s_leader->ps_pid;
		break;
	case TIOCNXCL:			/* reset exclusive use of tty */
		s = spltty();
		CLR(tp->t_state, TS_XCLUDE);
		splx(s);
		break;
	case TIOCOUTQ:			/* output queue size */
		*(int *)data = tp->t_outq.c_cc;
		break;
	case TIOCSETA:			/* set termios struct */
	case TIOCSETAW:			/* drain output, set */
	case TIOCSETAF: {		/* drn out, fls in, set */
		struct termios *t = (struct termios *)data;

		s = spltty();
		if (cmd == TIOCSETAW || cmd == TIOCSETAF) {
			if ((error = ttywait(tp)) != 0) {
				splx(s);
				return (error);
			}
			if (cmd == TIOCSETAF)
				ttyflush(tp, FREAD);
		}
		if (!ISSET(t->c_cflag, CIGNORE)) {
			/*
			 * Some minor validation is necessary.
			 */
			if (t->c_ispeed < 0 || t->c_ospeed < 0) {
				splx(s);
				return (EINVAL);
			}
			/*
			 * Set device hardware.
			 */
			if (tp->t_param && (error = (*tp->t_param)(tp, t))) {
				splx(s);
				return (error);
			} else {
				if (!ISSET(tp->t_state, TS_CARR_ON) &&
				    ISSET(tp->t_cflag, CLOCAL) &&
				    !ISSET(t->c_cflag, CLOCAL)) {
					CLR(tp->t_state, TS_ISOPEN);
					SET(tp->t_state, TS_WOPEN);
					ttwakeup(tp);
				}
				tp->t_cflag = t->c_cflag;
				tp->t_ispeed = t->c_ispeed;
				tp->t_ospeed = t->c_ospeed;
				if (t->c_ospeed == 0 && tp->t_session &&
				    tp->t_session->s_leader)
					prsignal(tp->t_session->s_leader,
					    SIGHUP);
			}
			ttsetwater(tp);
		}
		if (cmd != TIOCSETAF) {
			if (ISSET(t->c_lflag, ICANON) !=
			    ISSET(tp->t_lflag, ICANON)) {
				if (ISSET(t->c_lflag, ICANON)) {
					SET(tp->t_lflag, PENDIN);
					ttwakeup(tp);
				} else {
					struct clist tq;

					catq(&tp->t_rawq, &tp->t_canq);
					tq = tp->t_rawq;
					tp->t_rawq = tp->t_canq;
					tp->t_canq = tq;
					CLR(tp->t_lflag, PENDIN);
				}
			}
		}
		tp->t_iflag = t->c_iflag;
		tp->t_oflag = t->c_oflag;
		/*
		 * Make the EXTPROC bit read only.
		 */
		if (ISSET(tp->t_lflag, EXTPROC))
			SET(t->c_lflag, EXTPROC);
		else
			CLR(t->c_lflag, EXTPROC);
		tp->t_lflag = t->c_lflag | ISSET(tp->t_lflag, PENDIN);
		memcpy(tp->t_cc, t->c_cc, sizeof(t->c_cc));
		splx(s);
		break;
	}
	case TIOCSETD: {		/* set line discipline */
		int t = *(int *)data;
		dev_t device = tp->t_dev;

		if ((u_int)t >= nlinesw)
			return (ENXIO);
		if (t != tp->t_line) {
			s = spltty();
			(*linesw[tp->t_line].l_close)(tp, flag, p);
			error = (*linesw[t].l_open)(device, tp, p);
			if (error) {
				(*linesw[tp->t_line].l_open)(device, tp, p);
				splx(s);
				return (error);
			}
			tp->t_line = t;
			splx(s);
		}
		break;
	}
	case TIOCSTART:			/* start output, like ^Q */
		s = spltty();
		if (ISSET(tp->t_state, TS_TTSTOP) ||
		    ISSET(tp->t_lflag, FLUSHO)) {
			CLR(tp->t_lflag, FLUSHO);
			CLR(tp->t_state, TS_TTSTOP);
			ttstart(tp);
		}
		splx(s);
		break;
	case TIOCSTOP:			/* stop output, like ^S */
		s = spltty();
		if (!ISSET(tp->t_state, TS_TTSTOP)) {
			SET(tp->t_state, TS_TTSTOP);
			(*cdevsw[major(tp->t_dev)].d_stop)(tp, 0);
		}
		splx(s);
		break;
	case TIOCSCTTY:			/* become controlling tty */
		/* Session ctty vnode pointer set in vnode layer. */
		if (!SESS_LEADER(pr) ||
		    ((pr->ps_session->s_ttyvp || tp->t_session) &&
		     (tp->t_session != pr->ps_session)))
			return (EPERM);
		if (tp->t_session)
			SESSRELE(tp->t_session);
		SESSHOLD(pr->ps_session);
		tp->t_session = pr->ps_session;
		tp->t_pgrp = pr->ps_pgrp;
		pr->ps_session->s_ttyp = tp;
		atomic_setbits_int(&pr->ps_flags, PS_CONTROLT);
		break;
	case FIOSETOWN: {		/* set pgrp of tty */
		struct pgrp *pgrp;
		struct process *pr1;
		pid_t pgid = *(int *)data;

		if (!isctty(pr, tp))
			return (ENOTTY);
		if (pgid < 0) {
			pgrp = pgfind(-pgid);
		} else {
			pr1 = prfind(pgid);
			if (pr1 == NULL)
				return (ESRCH);
			pgrp = pr1->ps_pgrp;
		}
		if (pgrp == NULL)
			return (EINVAL);
		else if (pgrp->pg_session != pr->ps_session)
			return (EPERM);
		tp->t_pgrp = pgrp;
		break;
	}
	case TIOCSPGRP: {		/* set pgrp of tty */
		struct pgrp *pgrp = pgfind(*(int *)data);

		if (!isctty(pr, tp))
			return (ENOTTY);
		else if (pgrp == NULL)
			return (EINVAL);
		else if (pgrp->pg_session != pr->ps_session)
			return (EPERM);
		tp->t_pgrp = pgrp;
		break;
	}
	case TIOCSTAT:			/* get load avg stats */
		ttyinfo(tp);
		break;
	case TIOCSWINSZ:		/* set window size */
		if (bcmp((caddr_t)&tp->t_winsize, data,
		    sizeof (struct winsize))) {
			tp->t_winsize = *(struct winsize *)data;
			pgsignal(tp->t_pgrp, SIGWINCH, 1);
		}
		break;
	case TIOCSTSTAMP: {
		struct tstamps *ts = (struct tstamps *)data;

		s = spltty();
		CLR(tp->t_flags, TS_TSTAMPDCDSET);
		CLR(tp->t_flags, TS_TSTAMPCTSSET);
		CLR(tp->t_flags, TS_TSTAMPDCDCLR);
		CLR(tp->t_flags, TS_TSTAMPCTSCLR);
		if (ISSET(ts->ts_set, TIOCM_CAR))
			SET(tp->t_flags, TS_TSTAMPDCDSET);
		if (ISSET(ts->ts_set, TIOCM_CTS))
			SET(tp->t_flags, TS_TSTAMPCTSSET);
		if (ISSET(ts->ts_clr, TIOCM_CAR))
			SET(tp->t_flags, TS_TSTAMPDCDCLR);
		if (ISSET(ts->ts_clr, TIOCM_CTS))
			SET(tp->t_flags, TS_TSTAMPCTSCLR);
		splx(s);
		break;
	}
	default:
		return (-1);
	}
	return (0);
}

const struct filterops ttyread_filtops = {
	.f_flags	= FILTEROP_ISFD,
	.f_attach	= NULL,
	.f_detach	= filt_ttyrdetach,
	.f_event	= filt_ttyread,
};

const struct filterops ttywrite_filtops = {
	.f_flags	= FILTEROP_ISFD,
	.f_attach	= NULL,
	.f_detach	= filt_ttywdetach,
	.f_event	= filt_ttywrite,
};

const struct filterops ttyexcept_filtops = {
	.f_flags	= FILTEROP_ISFD,
	.f_attach	= NULL,
	.f_detach	= filt_ttyrdetach,
	.f_event	= filt_ttyexcept,
};

int
ttkqfilter(dev_t dev, struct knote *kn)
{
	struct tty *tp = (*cdevsw[major(dev)].d_tty)(dev);
	struct klist *klist;
	int s;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		klist = &tp->t_rsel.si_note;
		kn->kn_fop = &ttyread_filtops;
		break;
	case EVFILT_WRITE:
		klist = &tp->t_wsel.si_note;
		kn->kn_fop = &ttywrite_filtops;
		break;
	case EVFILT_EXCEPT:
		if (kn->kn_flags & __EV_SELECT) {
			/* Prevent triggering exceptfds. */
			return (EPERM);
		}
		if ((kn->kn_flags & __EV_POLL) == 0) {
			/* Disallow usage through kevent(2). */
			return (EINVAL);
		}
		klist = &tp->t_rsel.si_note;
		kn->kn_fop = &ttyexcept_filtops;
		break;
	default:
		return (EINVAL);
	}

	kn->kn_hook = tp;

	s = spltty();
	klist_insert_locked(klist, kn);
	splx(s);

	return (0);
}

void
filt_ttyrdetach(struct knote *kn)
{
	struct tty *tp = kn->kn_hook;
	int s;

	s = spltty();
	klist_remove_locked(&tp->t_rsel.si_note, kn);
	splx(s);
}

int
filt_ttyread(struct knote *kn, long hint)
{
	struct tty *tp = kn->kn_hook;
	int active, s;

	s = spltty();
	kn->kn_data = ttnread(tp);
	active = (kn->kn_data > 0);
	if (!ISSET(tp->t_cflag, CLOCAL) && !ISSET(tp->t_state, TS_CARR_ON)) {
		kn->kn_flags |= EV_EOF;
		if (kn->kn_flags & __EV_POLL)
			kn->kn_flags |= __EV_HUP;
		active = 1;
	} else {
		kn->kn_flags &= ~(EV_EOF | __EV_HUP);
	}
	splx(s);
	return (active);
}

void
filt_ttywdetach(struct knote *kn)
{
	struct tty *tp = kn->kn_hook;
	int s;

	s = spltty();
	klist_remove_locked(&tp->t_wsel.si_note, kn);
	splx(s);
}

int
filt_ttywrite(struct knote *kn, long hint)
{
	struct tty *tp = kn->kn_hook;
	int active, s;

	s = spltty();
	kn->kn_data = tp->t_outq.c_cn - tp->t_outq.c_cc;
	active = (tp->t_outq.c_cc <= tp->t_lowat);

	/* Write-side HUP condition is only for poll(2) and select(2). */
	if (kn->kn_flags & (__EV_POLL | __EV_SELECT)) {
		if (!ISSET(tp->t_cflag, CLOCAL) &&
		    !ISSET(tp->t_state, TS_CARR_ON)) {
			kn->kn_flags |= __EV_HUP;
			active = 1;
		} else {
			kn->kn_flags &= ~__EV_HUP;
		}
	}
	splx(s);
	return (active);
}

int
filt_ttyexcept(struct knote *kn, long hint)
{
	struct tty *tp = kn->kn_hook;
	int active = 0;
	int s;

	s = spltty();
	if (kn->kn_flags & __EV_POLL) {
		if (!ISSET(tp->t_cflag, CLOCAL) &&
		    !ISSET(tp->t_state, TS_CARR_ON)) {
			kn->kn_flags |= __EV_HUP;
			active = 1;
		} else {
			kn->kn_flags &= ~__EV_HUP;
		}
	}
	splx(s);
	return (active);
}

static int
ttnread(struct tty *tp)
{
	int nread;

	splassert(IPL_TTY);

	if (ISSET(tp->t_lflag, PENDIN))
		ttypend(tp);
	nread = tp->t_canq.c_cc;
	if (!ISSET(tp->t_lflag, ICANON)) {
		nread += tp->t_rawq.c_cc;
		if (nread < tp->t_cc[VMIN] && !tp->t_cc[VTIME])
			nread = 0;
	}
	return (nread);
}

/*
 * Wait for output to drain, or if this times out, flush it.
 */
int
ttywait_nsec(struct tty *tp, uint64_t nsecs)
{
	int error, s;

	error = 0;
	s = spltty();
	while ((tp->t_outq.c_cc || ISSET(tp->t_state, TS_BUSY)) &&
	    (ISSET(tp->t_state, TS_CARR_ON) || ISSET(tp->t_cflag, CLOCAL)) &&
	    tp->t_oproc) {
		(*tp->t_oproc)(tp);
		if ((tp->t_outq.c_cc || ISSET(tp->t_state, TS_BUSY)) &&
		    (ISSET(tp->t_state, TS_CARR_ON) || ISSET(tp->t_cflag, CLOCAL))
		    && tp->t_oproc) {
			SET(tp->t_state, TS_ASLEEP);
			error = ttysleep_nsec(tp, &tp->t_outq, TTOPRI | PCATCH,
			    ttyout, nsecs);
			if (error == EWOULDBLOCK)
				ttyflush(tp, FWRITE);
			if (error)
				break;
		} else
			break;
	}
	splx(s);
	return (error);
}

int
ttywait(struct tty *tp)
{
	return (ttywait_nsec(tp, INFSLP));
}

/*
 * Flush if successfully wait.
 */
int
ttywflush(struct tty *tp)
{
	int error;

	error = ttywait_nsec(tp, SEC_TO_NSEC(5));
	if (error == 0 || error == EWOULDBLOCK)
		ttyflush(tp, FREAD);
	return (error);
}

/*
 * Flush tty read and/or write queues, notifying anyone waiting.
 */
void
ttyflush(struct tty *tp, int rw)
{
	int s;

	s = spltty();
	if (rw & FREAD) {
		FLUSHQ(&tp->t_canq);
		FLUSHQ(&tp->t_rawq);
		tp->t_rocount = 0;
		tp->t_rocol = 0;
		CLR(tp->t_state, TS_LOCAL);
		ttyunblock(tp);
		ttwakeup(tp);
	}
	if (rw & FWRITE) {
		CLR(tp->t_state, TS_TTSTOP);
		(*cdevsw[major(tp->t_dev)].d_stop)(tp, rw);
		FLUSHQ(&tp->t_outq);
		wakeup((caddr_t)&tp->t_outq);
		selwakeup(&tp->t_wsel);
	}
	splx(s);
}

/*
 * Copy in the default termios characters.
 */
void
ttychars(struct tty *tp)
{

	memcpy(tp->t_cc, ttydefchars, sizeof(ttydefchars));
}

/*
 * Send stop character on input overflow.
 */
static void
ttyblock(struct tty *tp)
{
	int total;

	total = tp->t_rawq.c_cc + tp->t_canq.c_cc;
	if (tp->t_rawq.c_cc > TTYHOG(tp)) {
		ttyflush(tp, FREAD | FWRITE);
		CLR(tp->t_state, TS_TBLOCK);
	}
	/*
	 * Block further input iff: current input > threshold
	 * AND input is available to user program.
	 */
	if ((total >= TTYHOG(tp) / 2 &&
	     !ISSET(tp->t_state, TS_TBLOCK) &&
	     !ISSET(tp->t_lflag, ICANON)) || tp->t_canq.c_cc > 0) {
		if (ISSET(tp->t_iflag, IXOFF) &&
		    tp->t_cc[VSTOP] != _POSIX_VDISABLE &&
		    putc(tp->t_cc[VSTOP], &tp->t_outq) == 0) {
			SET(tp->t_state, TS_TBLOCK);
			ttstart(tp);
		}
		/* Try to block remote output via hardware flow control. */
		if (ISSET(tp->t_cflag, CHWFLOW) && tp->t_hwiflow &&
		    (*tp->t_hwiflow)(tp, 1) != 0)
			SET(tp->t_state, TS_TBLOCK);
	}
}

void
ttrstrt(void *arg)
{
	struct tty *tp = (struct tty *)arg;
	int s;

#ifdef DIAGNOSTIC
	if (tp == NULL)
		panic("ttrstrt");
#endif
	s = spltty();
	CLR(tp->t_state, TS_TIMEOUT);
	ttstart(tp);
	splx(s);
}

int
ttstart(struct tty *tp)
{

	if (tp->t_oproc != NULL)	/* XXX: Kludge for pty. */
		(*tp->t_oproc)(tp);
	return (0);
}

/*
 * "close" a line discipline
 */
int
ttylclose(struct tty *tp, int flag, struct proc *p)
{

	if (flag & FNONBLOCK)
		ttyflush(tp, FREAD | FWRITE);
	else
		ttywflush(tp);
	return (0);
}

/*
 * Handle modem control transition on a tty.
 * Flag indicates new state of carrier.
 * Returns 0 if the line should be turned off, otherwise 1.
 */
int
ttymodem(struct tty *tp, int flag)
{

	if (!ISSET(tp->t_state, TS_WOPEN) && ISSET(tp->t_cflag, MDMBUF)) {
		/*
		 * MDMBUF: do flow control according to carrier flag
		 */
		if (flag) {
			CLR(tp->t_state, TS_TTSTOP);
			ttstart(tp);
		} else if (!ISSET(tp->t_state, TS_TTSTOP)) {
			SET(tp->t_state, TS_TTSTOP);
			(*cdevsw[major(tp->t_dev)].d_stop)(tp, 0);
		}
	} else if (flag == 0) {
		/*
		 * Lost carrier.
		 */
		CLR(tp->t_state, TS_CARR_ON);
		if (ISSET(tp->t_state, TS_ISOPEN) &&
		    !ISSET(tp->t_cflag, CLOCAL)) {
			if (tp->t_session && tp->t_session->s_leader)
				prsignal(tp->t_session->s_leader, SIGHUP);
			ttyflush(tp, FREAD | FWRITE);
			return (0);
		}
	} else {
		/*
		 * Carrier now on.
		 */
		SET(tp->t_state, TS_CARR_ON);
		ttwakeup(tp);
	}
	return (1);
}

/*
 * Default modem control routine (for other line disciplines).
 * Return argument flag, to turn off device on carrier drop.
 */
int
nullmodem(struct tty *tp, int flag)
{

	if (flag)
		SET(tp->t_state, TS_CARR_ON);
	else {
		CLR(tp->t_state, TS_CARR_ON);
		if (ISSET(tp->t_state, TS_ISOPEN) &&
		    !ISSET(tp->t_cflag, CLOCAL)) {
			if (tp->t_session && tp->t_session->s_leader)
				prsignal(tp->t_session->s_leader, SIGHUP);
			ttyflush(tp, FREAD | FWRITE);
			return (0);
		}
	}
	return (1);
}

/*
 * Reinput pending characters after state switch
 * call at spltty().
 */
void
ttypend(struct tty *tp)
{
	struct clist tq;
	int c;

	splassert(IPL_TTY);

	CLR(tp->t_lflag, PENDIN);
	SET(tp->t_state, TS_TYPEN);
	tq = tp->t_rawq;
	tp->t_rawq.c_cc = 0;
	tp->t_rawq.c_cf = tp->t_rawq.c_cl = NULL;
	while ((c = getc(&tq)) >= 0)
		ttyinput(c, tp);
	CLR(tp->t_state, TS_TYPEN);
}

void ttvtimeout(void *);

void
ttvtimeout(void *arg)
{
	struct tty *tp = (struct tty *)arg;

	wakeup(&tp->t_rawq);
}

/*
 * Process a read call on a tty device.
 */
int
ttread(struct tty *tp, struct uio *uio, int flag)
{
	struct timeout *stime = NULL;
	struct proc *p = curproc;
	struct process *pr = p->p_p;
	int s, first, error = 0;
	u_char *cc = tp->t_cc;
	struct clist *qp;
	int last_cc = 0;
	long lflag;
	int c;

loop:	lflag = tp->t_lflag;
	s = spltty();
	/*
	 * take pending input first
	 */
	if (ISSET(lflag, PENDIN))
		ttypend(tp);
	splx(s);

	/*
	 * Hang process if it's in the background.
	 */
	if (isbackground(pr, tp)) {
		if (sigismasked(p, SIGTTIN) ||
		    pr->ps_flags & PS_PPWAIT || pr->ps_pgrp->pg_jobc == 0) {
			error = EIO;
			goto out;
		}
		pgsignal(pr->ps_pgrp, SIGTTIN, 1);
		error = ttysleep(tp, &lbolt, TTIPRI | PCATCH, ttybg);
		if (error)
			goto out;
		goto loop;
	}

	s = spltty();
	if (!ISSET(lflag, ICANON)) {
		int min = cc[VMIN];
		int time = cc[VTIME] * 100;	/* tenths of a second (ms) */

		qp = &tp->t_rawq;
		/*
		 * Check each of the four combinations.
		 * (min > 0 && time == 0) is the normal read case.
		 * It should be fairly efficient, so we check that and its
		 * companion case (min == 0 && time == 0) first.
		 */
		if (time == 0) {
			if (qp->c_cc < min)
				goto sleep;
			goto read;
		}
		if (min > 0) {
			if (qp->c_cc <= 0)
				goto sleep;
			if (qp->c_cc >= min)
				goto read;
			if (stime == NULL) {
alloc_timer:
				stime = malloc(sizeof(*stime), M_TEMP, M_WAITOK);
				timeout_set(stime, ttvtimeout, tp);
				timeout_add_msec(stime, time);
			} else if (qp->c_cc > last_cc) {
				/* got a character, restart timer */
				timeout_add_msec(stime, time);
			}
		} else {	/* min == 0 */
			if (qp->c_cc > 0)
				goto read;
			if (stime == NULL) {
				goto alloc_timer;
			}
		}
		last_cc = qp->c_cc;
		if (stime && !timeout_triggered(stime)) {
			goto sleep;
		}
	} else if ((qp = &tp->t_canq)->c_cc <= 0) {
		int carrier;

sleep:
		/*
		 * If there is no input, sleep on rawq
		 * awaiting hardware receipt and notification.
		 * If we have data, we don't need to check for carrier.
		 */
		carrier = ISSET(tp->t_state, TS_CARR_ON) ||
		    ISSET(tp->t_cflag, CLOCAL);
		if (!carrier && ISSET(tp->t_state, TS_ISOPEN)) {
			splx(s);
			error = 0;
			goto out;
		}
		if (flag & IO_NDELAY) {
			splx(s);
			error = EWOULDBLOCK;
			goto out;
		}
		error = ttysleep(tp, &tp->t_rawq, TTIPRI | PCATCH,
		    carrier ? ttyin : ttopen);
		splx(s);
		if (stime && timeout_triggered(stime))
			error = EWOULDBLOCK;
		if (cc[VMIN] == 0 && error == EWOULDBLOCK) {
			error = 0;
			goto out;
		}
		if (error && error != EWOULDBLOCK)
			goto out;
		error = 0;
		goto loop;
	}
read:
	splx(s);

	/*
	 * Input present, check for input mapping and processing.
	 */
	first = 1;
	while ((c = getc(qp)) >= 0) {
		/*
		 * delayed suspend (^Y)
		 */
		if (CCEQ(cc[VDSUSP], c) &&
		    ISSET(lflag, IEXTEN | ISIG) == (IEXTEN | ISIG)) {
			pgsignal(tp->t_pgrp, SIGTSTP, 1);
			if (first) {
				error = ttysleep(tp, &lbolt, TTIPRI | PCATCH,
				    ttybg);
				if (error)
					break;
				goto loop;
			}
			break;
		}
		/*
		 * Interpret EOF only in canonical mode.
		 */
		if (CCEQ(cc[VEOF], c) && ISSET(lflag, ICANON))
			break;
		/*
		 * Give user character.
		 */
		error = ureadc(c, uio);
		if (error)
			break;
		if (uio->uio_resid == 0)
			break;
		/*
		 * In canonical mode check for a "break character"
		 * marking the end of a "line of input".
		 */
		if (ISSET(lflag, ICANON) && TTBREAKC(c, lflag))
			break;
		first = 0;
	}
	/*
	 * Look to unblock output now that (presumably)
	 * the input queue has gone down.
	 */
	s = spltty();
	if (tp->t_rawq.c_cc < TTYHOG(tp)/5)
		ttyunblock(tp);
	splx(s);

out:
	if (stime) {
		timeout_del(stime);
		free(stime, M_TEMP, sizeof(*stime));
	}
	return (error);
}

/* Call at spltty */
void
ttyunblock(struct tty *tp)
{
	u_char *cc = tp->t_cc;

	splassert(IPL_TTY);

	if (ISSET(tp->t_state, TS_TBLOCK)) {
		if (ISSET(tp->t_iflag, IXOFF) &&
		    cc[VSTART] != _POSIX_VDISABLE &&
		    putc(cc[VSTART], &tp->t_outq) == 0) {
			CLR(tp->t_state, TS_TBLOCK);
			ttstart(tp);
		}
		/* Try to unblock remote output via hardware flow control. */
		if (ISSET(tp->t_cflag, CHWFLOW) && tp->t_hwiflow &&
		    (*tp->t_hwiflow)(tp, 0) != 0)
			CLR(tp->t_state, TS_TBLOCK);
	}
}

/*
 * Check the output queue on tp for space for a kernel message (from uprintf
 * or tprintf).  Allow some space over the normal hiwater mark so we don't
 * lose messages due to normal flow control, but don't let the tty run amok.
 * Sleeps here are not interruptible, but we return prematurely if new signals
 * arrive.
 */
int
ttycheckoutq(struct tty *tp, int wait)
{
	int hiwat, s, oldsig;

	hiwat = tp->t_hiwat;
	s = spltty();
	oldsig = wait ? SIGPENDING(curproc) : 0;
	if (tp->t_outq.c_cc > hiwat + TTHIWATMINSPACE)
		while (tp->t_outq.c_cc > hiwat) {
			ttstart(tp);
			if (wait == 0 || SIGPENDING(curproc) != oldsig) {
				splx(s);
				return (0);
			}
			SET(tp->t_state, TS_ASLEEP);
			tsleep_nsec(&tp->t_outq, PZERO - 1, "ttckoutq",
			    SEC_TO_NSEC(1));
		}
	splx(s);
	return (1);
}

/*
 * Process a write call on a tty device.
 */
int
ttwrite(struct tty *tp, struct uio *uio, int flag)
{
	u_char *cp = NULL;
	int cc, ce, obufcc = 0;
	struct proc *p;
	struct process *pr;
	int hiwat, error, s;
	size_t cnt;
	u_char obuf[OBUFSIZ];

	hiwat = tp->t_hiwat;
	cnt = uio->uio_resid;
	error = 0;
	cc = 0;
loop:
	s = spltty();
	if (!ISSET(tp->t_state, TS_CARR_ON) &&
	    !ISSET(tp->t_cflag, CLOCAL)) {
		if (ISSET(tp->t_state, TS_ISOPEN)) {
			splx(s);
			error = EIO;
			goto done;
		} else if (flag & IO_NDELAY) {
			splx(s);
			error = EWOULDBLOCK;
			goto out;
		} else {
			/* Sleep awaiting carrier. */
			error = ttysleep(tp,
			    &tp->t_rawq, TTIPRI | PCATCH, ttopen);
			splx(s);
			if (error)
				goto out;
			goto loop;
		}
	}
	splx(s);
	/*
	 * Hang the process if it's in the background.
	 */
	p = curproc;
	pr = p->p_p;
	if (isbackground(pr, tp) &&
	    ISSET(tp->t_lflag, TOSTOP) && (pr->ps_flags & PS_PPWAIT) == 0 &&
	    !sigismasked(p, SIGTTOU)) {
		if (pr->ps_pgrp->pg_jobc == 0) {
			error = EIO;
			goto out;
		}
		pgsignal(pr->ps_pgrp, SIGTTOU, 1);
		error = ttysleep(tp, &lbolt, TTIPRI | PCATCH, ttybg);
		if (error)
			goto out;
		goto loop;
	}
	/*
	 * Process the user's data in at most OBUFSIZ chunks.  Perform any
	 * output translation.  Keep track of high water mark, sleep on
	 * overflow awaiting device aid in acquiring new space.
	 */
	while (uio->uio_resid > 0 || cc > 0) {
		if (ISSET(tp->t_lflag, FLUSHO)) {
			uio->uio_resid = 0;
			goto done;
		}
		if (tp->t_outq.c_cc > hiwat)
			goto ovhiwat;
		/*
		 * Grab a hunk of data from the user, unless we have some
		 * leftover from last time.
		 */
		if (cc == 0) {
			cc = MIN(uio->uio_resid, OBUFSIZ);
			cp = obuf;
			error = uiomove(cp, cc, uio);
			if (error) {
				cc = 0;
				break;
			}
			if (cc > obufcc)
				obufcc = cc;

			/* duplicate /dev/console output into console buffer */
			if (consbufp && cn_tab &&
			    cn_tab->cn_dev == tp->t_dev && tp->t_gen == 0) {
				int i;
				for (i = 0; i < cc; i++) {
					char c = cp[i];
					if (c != '\0' && c != '\r' && c != 0177)
						msgbuf_putchar(consbufp, c);
				}
			}
		}
		/*
		 * If nothing fancy need be done, grab those characters we
		 * can handle without any of ttyoutput's processing and
		 * just transfer them to the output q.  For those chars
		 * which require special processing (as indicated by the
		 * bits in char_type), call ttyoutput.  After processing
		 * a hunk of data, look for FLUSHO so ^O's will take effect
		 * immediately.
		 */
		while (cc > 0) {
			int i;
			if (!ISSET(tp->t_oflag, OPOST))
				ce = cc;
			else {
				ce = cc - scanc((u_int)cc, cp, char_type,
				    CCLASSMASK);
				/*
				 * If ce is zero, then we're processing
				 * a special character through ttyoutput.
				 */
				if (ce == 0) {
					tp->t_rocount = 0;
					if (ttyoutput(*cp, tp) >= 0) {
						/* out of space */
						goto ovhiwat;
					}
					cp++;
					cc--;
					if (ISSET(tp->t_lflag, FLUSHO) ||
					    tp->t_outq.c_cc > hiwat)
						goto ovhiwat;
					continue;
				}
			}
			/*
			 * A bunch of normal characters have been found.
			 * Transfer them en masse to the output queue and
			 * continue processing at the top of the loop.
			 * If there are any further characters in this
			 * <= OBUFSIZ chunk, the first should be a character
			 * requiring special handling by ttyoutput.
			 */
			tp->t_rocount = 0;
			i = b_to_q(cp, ce, &tp->t_outq);
			ce -= i;
			tp->t_column += ce;
			cp += ce, cc -= ce, tk_nout += ce;
			tp->t_outcc += ce;
			if (i > 0) {
				/* out of space */
				goto ovhiwat;
			}
			if (ISSET(tp->t_lflag, FLUSHO) ||
			    tp->t_outq.c_cc > hiwat)
				break;
		}
		ttstart(tp);
	}
out:
	/*
	 * If cc is nonzero, we leave the uio structure inconsistent, as the
	 * offset and iov pointers have moved forward, but it doesn't matter
	 * (the call will either return short or restart with a new uio).
	 */
	uio->uio_resid += cc;
done:
	if (obufcc)
		explicit_bzero(obuf, obufcc);
	return (error);

ovhiwat:
	ttstart(tp);
	s = spltty();
	/*
	 * This can only occur if FLUSHO is set in t_lflag,
	 * or if ttstart/oproc is synchronous (or very fast).
	 */
	if (tp->t_outq.c_cc <= hiwat) {
		splx(s);
		goto loop;
	}
	if (flag & IO_NDELAY) {
		splx(s);
		uio->uio_resid += cc;
		if (obufcc)
			explicit_bzero(obuf, obufcc);
		return (uio->uio_resid == cnt ? EWOULDBLOCK : 0);
	}
	SET(tp->t_state, TS_ASLEEP);
	error = ttysleep(tp, &tp->t_outq, TTOPRI | PCATCH, ttyout);
	splx(s);
	if (error)
		goto out;
	goto loop;
}

/*
 * Rubout one character from the rawq of tp
 * as cleanly as possible.
 */
int
ttyrub(int c, struct tty *tp)
{
	u_char *cp;
	int savecol;
	int tabc, s, cc;

	if (!ISSET(tp->t_lflag, ECHO) || ISSET(tp->t_lflag, EXTPROC))
		return 0;
	CLR(tp->t_lflag, FLUSHO);
	if (ISSET(tp->t_lflag, ECHOE)) {
		if (tp->t_rocount == 0) {
			/*
			 * Screwed by ttwrite; retype
			 */
			return ttyretype(tp);
		}
		if (c == ('\t' | TTY_QUOTE) || c == ('\n' | TTY_QUOTE))
			ttyrubo(tp, 2);
		else {
			CLR(c, ~TTY_CHARMASK);
			switch (CCLASS(c)) {
			case ORDINARY:
				ttyrubo(tp, 1);
				break;
			case BACKSPACE:
			case CONTROL:
			case NEWLINE:
			case RETURN:
			case VTAB:
				if (ISSET(tp->t_lflag, ECHOCTL))
					ttyrubo(tp, 2);
				break;
			case TAB:
				if (tp->t_rocount < tp->t_rawq.c_cc)
					return ttyretype(tp);
				s = spltty();
				savecol = tp->t_column;
				SET(tp->t_state, TS_CNTTB);
				SET(tp->t_lflag, FLUSHO);
				tp->t_column = tp->t_rocol;
				for (cp = firstc(&tp->t_rawq, &tabc, &cc); cp;
				    cp = nextc(&tp->t_rawq, cp, &tabc, &cc))
					ttyecho(tabc, tp);
				CLR(tp->t_lflag, FLUSHO);
				CLR(tp->t_state, TS_CNTTB);
				splx(s);

				/* savecol will now be length of the tab. */
				savecol -= tp->t_column;
				tp->t_column += savecol;
				if (savecol > 8)
					savecol = 8;	/* overflow screw */
				while (--savecol >= 0)
					(void)ttyoutput('\b', tp);
				break;
			default:			/* XXX */
#define	PANICSTR	"ttyrub: would panic c = %d, val = %d"
				(void)printf(PANICSTR "\n", c, CCLASS(c));
#ifdef notdef
				panic(PANICSTR, c, CCLASS(c));
#endif
			}
		}
	} else if (ISSET(tp->t_lflag, ECHOPRT)) {
		if (!ISSET(tp->t_state, TS_ERASE)) {
			SET(tp->t_state, TS_ERASE);
			(void)ttyoutput('\\', tp);
		}
		ttyecho(c, tp);
	} else
		ttyecho(tp->t_cc[VERASE], tp);
	--tp->t_rocount;
	return 0;
}

/*
 * Back over cnt characters, erasing them.
 */
static void
ttyrubo(struct tty *tp, int cnt)
{

	while (cnt-- > 0) {
		(void)ttyoutput('\b', tp);
		(void)ttyoutput(' ', tp);
		(void)ttyoutput('\b', tp);
	}
}

/*
 * ttyretype --
 *	Reprint the rawq line.  Note, it is assumed that c_cc has already
 *	been checked.
 */
int
ttyretype(struct tty *tp)
{
	u_char *cp;
	int s, c, cc;

	/* Echo the reprint character. */
	if (tp->t_cc[VREPRINT] != _POSIX_VDISABLE)
		ttyecho(tp->t_cc[VREPRINT], tp);

	(void)ttyoutput('\n', tp);

	s = spltty();
	for (cp = firstc(&tp->t_canq, &c, &cc); cp;
	    cp = nextc(&tp->t_canq, cp, &c, &cc))
		ttyecho(c, tp);
	for (cp = firstc(&tp->t_rawq, &c, &cc); cp;
	    cp = nextc(&tp->t_rawq, cp, &c, &cc))
		ttyecho(c, tp);
	CLR(tp->t_state, TS_ERASE);
	splx(s);

	tp->t_rocount = tp->t_rawq.c_cc;
	tp->t_rocol = 0;
	return (1);
}

/*
 * Echo a typed character to the terminal.
 */
static void
ttyecho(int c, struct tty *tp)
{

	if (!ISSET(tp->t_state, TS_CNTTB))
		CLR(tp->t_lflag, FLUSHO);
	if ((!ISSET(tp->t_lflag, ECHO) &&
	    (!ISSET(tp->t_lflag, ECHONL) || c != '\n')) ||
	    ISSET(tp->t_lflag, EXTPROC))
		return;
	if (((ISSET(tp->t_lflag, ECHOCTL) &&
	     (ISSET(c, TTY_CHARMASK) <= 037 && c != '\t' && c != '\n')) ||
	    ISSET(c, TTY_CHARMASK) == 0177)) {
		(void)ttyoutput('^', tp);
		CLR(c, ~TTY_CHARMASK);
		if (c == 0177)
			c = '?';
		else
			c += 'A' - 1;
	}
	(void)ttyoutput(c, tp);
}

/*
 * Wakeup any writers if necessary.
 */
void
ttwakeupwr(struct tty *tp)
{

	if (tp->t_outq.c_cc <= tp->t_lowat) {
		if (ISSET(tp->t_state, TS_ASLEEP)) {
			CLR(tp->t_state, TS_ASLEEP);
			wakeup(&tp->t_outq);
		}
		selwakeup(&tp->t_wsel);
	}
}

/*
 * Wake up any readers on a tty.
 */
void
ttwakeup(struct tty *tp)
{

	selwakeup(&tp->t_rsel);
	if (ISSET(tp->t_state, TS_ASYNC))
		pgsignal(tp->t_pgrp, SIGIO, 1);
	wakeup((caddr_t)&tp->t_rawq);
}

/*
 * Look up a code for a specified speed in a conversion table;
 * used by drivers to map software speed values to hardware parameters.
 */
int
ttspeedtab(int speed, const struct speedtab *table)
{

	for ( ; table->sp_speed != -1; table++)
		if (table->sp_speed == speed)
			return (table->sp_code);
	return (-1);
}

/*
 * Set tty hi and low water marks.
 *
 * Try to arrange the dynamics so there's about one second
 * from hi to low water.
 */
void
ttsetwater(struct tty *tp)
{
	int cps, x;

#define CLAMP(x, h, l)	((x) > h ? h : ((x) < l) ? l : (x))

	cps = tp->t_ospeed / 10;
	tp->t_lowat = x = CLAMP(cps / 2, TTMAXLOWAT, TTMINLOWAT);
	x += cps;
	tp->t_hiwat = CLAMP(x, tp->t_outq.c_cn - TTHIWATMINSPACE, TTMINHIWAT);
#undef	CLAMP
}

/*
 * Get the total estcpu for a process, summing across threads.
 * Returns true if at least one thread is runnable/running.
 */
static int
process_sum(struct process *pr, fixpt_t *pctcpup)
{
	struct proc *p;
	fixpt_t pctcpu;
	int ret;

	ret = 0;
	pctcpu = 0;
	TAILQ_FOREACH(p, &pr->ps_threads, p_thr_link) {
		if (p->p_stat == SRUN || p->p_stat == SONPROC)
			ret = 1;
		pctcpu += p->p_pctcpu;
	}

	*pctcpup = pctcpu;
	return (ret);
}

/*
 * Report on state of foreground process group.
 */
void
ttyinfo(struct tty *tp)
{
	struct process *pr, *pickpr;
	struct proc *p, *pick;
	struct tusage tu;
	struct timespec utime, stime;
	int tmp;

	if (ttycheckoutq(tp,0) == 0)
		return;

	/* Print load average. */
	tmp = (averunnable.ldavg[0] * 100 + FSCALE / 2) >> FSHIFT;
	ttyprintf(tp, "load: %d.%02d ", tmp / 100, tmp % 100);

	if (tp->t_session == NULL)
		ttyprintf(tp, "not a controlling terminal\n");
	else if (tp->t_pgrp == NULL)
		ttyprintf(tp, "no foreground process group\n");
	else if ((pr = LIST_FIRST(&tp->t_pgrp->pg_members)) == NULL)
empty:		ttyprintf(tp, "empty foreground process group\n");
	else {
		const char *state;
		fixpt_t pctcpu, pctcpu2;
		int run, run2;
		int calc_pctcpu;
		long rss = 0;

		/*
		 * Pick the most active process:
		 *  - prefer at least one running/runnable thread
		 *  - prefer higher total pctcpu
		 *  - prefer non-zombie
		 * Otherwise take the most recently added to this process group
		 */
		pickpr = pr;
		run = process_sum(pickpr, &pctcpu);
		while ((pr = LIST_NEXT(pr, ps_pglist)) != NULL) {
			run2 = process_sum(pr, &pctcpu2);
			if (run) {
				/*
				 * pick is running; is p running w/same or
				 * more cpu?
				 */
				if (run2 && pctcpu2 >= pctcpu)
					goto update_pickpr;
				continue;
			}
			/* pick isn't running; is p running *or* w/more cpu? */
			if (run2 || pctcpu2 > pctcpu)
				goto update_pickpr;

			/* if p has less cpu or is zombie, then it's worse */
			if (pctcpu2 < pctcpu || (pr->ps_flags & PS_ZOMBIE))
				continue;
update_pickpr:
			pickpr = pr;
			run = run2;
			pctcpu = pctcpu2;
		}

		/* Calculate percentage cpu, resident set size. */
		calc_pctcpu = (pctcpu * 10000 + FSCALE / 2) >> FSHIFT;
		if ((pickpr->ps_flags & (PS_EMBRYO | PS_ZOMBIE)) == 0 &&
		    pickpr->ps_vmspace != NULL)
			rss = vm_resident_count(pickpr->ps_vmspace);

		tuagg_get_process(&tu, pickpr);
		calctsru(&tu, &utime, &stime, NULL);

		/* Round up and print user time. */
		utime.tv_nsec += 5000000;
		if (utime.tv_nsec >= 1000000000) {
			utime.tv_sec += 1;
			utime.tv_nsec -= 1000000000;
		}

		/* Round up and print system time. */
		stime.tv_nsec += 5000000;
		if (stime.tv_nsec >= 1000000000) {
			stime.tv_sec += 1;
			stime.tv_nsec -= 1000000000;
		}

		/*
		 * Find the most active thread:
		 *  - prefer runnable
		 *  - prefer higher pctcpu
		 *  - prefer living
		 * Otherwise take the newest thread
		 */
		pick = p = TAILQ_FIRST(&pickpr->ps_threads);
		if (p == NULL)
			goto empty;
		run = p->p_stat == SRUN || p->p_stat == SONPROC;
		pctcpu = p->p_pctcpu;
		while ((p = TAILQ_NEXT(p, p_thr_link)) != NULL) {
			run2 = p->p_stat == SRUN || p->p_stat == SONPROC;
			pctcpu2 = p->p_pctcpu;
			if (run) {
				/*
				 * pick is running; is p running w/same or
				 * more cpu?
				 */
				if (run2 && pctcpu2 >= pctcpu)
					goto update_pick;
				continue;
			}
			/* pick isn't running; is p running *or* w/more cpu? */
			if (run2 || pctcpu2 > pctcpu)
				goto update_pick;

			/* if p has less cpu or is exiting, then it's worse */
			if (pctcpu2 < pctcpu || p->p_flag & P_WEXIT)
				continue;
update_pick:
			pick = p;
			run = run2;
			pctcpu = p->p_pctcpu;
		}
		state = pick->p_stat == SONPROC ? "running" :
		        pick->p_stat == SRUN ? "runnable" :
		        pick->p_wmesg ? pick->p_wmesg : "iowait";

		ttyprintf(tp,
		    " cmd: %s %d [%s] %lld.%02ldu %lld.%02lds %d%% %ldk\n",
		    pickpr->ps_comm, pickpr->ps_pid, state,
		    (long long)utime.tv_sec, utime.tv_nsec / 10000000,
		    (long long)stime.tv_sec, stime.tv_nsec / 10000000,
		    calc_pctcpu / 100, rss);
	}
	tp->t_rocount = 0;	/* so pending input will be retyped if BS */
}

/*
 * Output char to tty; console putchar style.
 */
int
tputchar(int c, struct tty *tp)
{
	int s;

	s = spltty();
	if (ISSET(tp->t_state, TS_ISOPEN) == 0 ||
	    !(ISSET(tp->t_state, TS_CARR_ON) || ISSET(tp->t_cflag, CLOCAL))) {
		splx(s);
		return (-1);
	}
	if (c == '\n')
		(void)ttyoutput('\r', tp);
	(void)ttyoutput(c, tp);
	ttstart(tp);
	splx(s);
	return (0);
}

/*
 * Sleep on chan, returning ERESTART if tty changed while we napped and
 * returning any errors (e.g. EINTR/ETIMEDOUT) reported by tsleep.  If
 * the tty is revoked, restarting a pending call will redo validation done
 * at the start of the call.
 */
int
ttysleep(struct tty *tp, void *chan, int pri, char *wmesg)
{
	return (ttysleep_nsec(tp, chan, pri, wmesg, INFSLP));
}

int
ttysleep_nsec(struct tty *tp, void *chan, int pri, char *wmesg, uint64_t nsecs)
{
	int error;
	short gen;

	gen = tp->t_gen;
	if ((error = tsleep_nsec(chan, pri, wmesg, nsecs)) != 0)
		return (error);
	return (tp->t_gen == gen ? 0 : ERESTART);
}

/*
 * Initialise the global tty list.
 */
void
tty_init(void)
{

	TAILQ_INIT(&ttylist);
	tty_count = 0;
}

/*
 * Allocate a tty structure and its associated buffers, and attach it to the
 * tty list.
 */
struct tty *
ttymalloc(int baud)
{
	struct tty *tp;

	tp = malloc(sizeof(struct tty), M_TTYS, M_WAITOK|M_ZERO);

	if (baud == 0)
		baud = 115200;

	if (baud <= 9600)
		tp->t_qlen = 1024;
	else if (baud <= 115200)
		tp->t_qlen = 4096;
	else
		tp->t_qlen = 8192;
	clalloc(&tp->t_rawq, tp->t_qlen, 1);
	clalloc(&tp->t_canq, tp->t_qlen, 1);
	/* output queue doesn't need quoting */
	clalloc(&tp->t_outq, tp->t_qlen, 0);

	rw_enter_write(&ttylist_lock);
	TAILQ_INSERT_TAIL(&ttylist, tp, tty_link);
	++tty_count;
	rw_exit_write(&ttylist_lock);

	timeout_set(&tp->t_rstrt_to, ttrstrt, tp);

	return(tp);
}


/*
 * Free a tty structure and its buffers, after removing it from the tty list.
 */
void
ttyfree(struct tty *tp)
{
	int s;

	rw_enter_write(&ttylist_lock);
	--tty_count;
#ifdef DIAGNOSTIC
	if (tty_count < 0)
		panic("ttyfree: tty_count < 0");
#endif
	TAILQ_REMOVE(&ttylist, tp, tty_link);
	rw_exit_write(&ttylist_lock);

	s = spltty();
	klist_invalidate(&tp->t_rsel.si_note);
	klist_invalidate(&tp->t_wsel.si_note);
	splx(s);

	clfree(&tp->t_rawq);
	clfree(&tp->t_canq);
	clfree(&tp->t_outq);
	free(tp, M_TTYS, sizeof(*tp));
}

void
ttystats_init(struct itty **ttystats, int *ttycp, size_t *ttystatssiz)
{
	int ntty = 0, ttyc;
	struct itty *itp;
	struct tty *tp;

	ttyc = tty_count;
	*ttystatssiz = ttyc * sizeof(struct itty);
	*ttystats = mallocarray(ttyc, sizeof(struct itty),
	    M_SYSCTL, M_WAITOK|M_ZERO);

	rw_enter_write(&ttylist_lock);
	for (tp = TAILQ_FIRST(&ttylist), itp = *ttystats; tp && ntty++ < ttyc;
	    tp = TAILQ_NEXT(tp, tty_link), itp++) {
		itp->t_dev = tp->t_dev;
		itp->t_rawq_c_cc = tp->t_rawq.c_cc;
		itp->t_canq_c_cc = tp->t_canq.c_cc;
		itp->t_outq_c_cc = tp->t_outq.c_cc;
		itp->t_hiwat = tp->t_hiwat;
		itp->t_lowat = tp->t_lowat;
		if (ISSET(tp->t_oflag, OPOST))
			itp->t_column = tp->t_column;
		itp->t_state = tp->t_state;
		itp->t_session = tp->t_session;
		if (tp->t_pgrp)
			itp->t_pgrp_pg_id = tp->t_pgrp->pg_id;
		else
			itp->t_pgrp_pg_id = 0;
		itp->t_line = tp->t_line;
	}
	rw_exit_write(&ttylist_lock);
	*ttycp = ntty;
}

#ifndef SMALL_KERNEL
/*
 * Return tty-related information.
 */
int
sysctl_tty(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen)
{
	int err;

	if (namelen != 1)
		return (ENOTDIR);

	switch (name[0]) {
	case KERN_TTY_TKNIN:
		return (sysctl_rdquad(oldp, oldlenp, newp, tk_nin));
	case KERN_TTY_TKNOUT:
		return (sysctl_rdquad(oldp, oldlenp, newp, tk_nout));
	case KERN_TTY_TKRAWCC:
		return (sysctl_rdquad(oldp, oldlenp, newp, tk_rawcc));
	case KERN_TTY_TKCANCC:
		return (sysctl_rdquad(oldp, oldlenp, newp, tk_cancc));
	case KERN_TTY_INFO:
	    {
		struct itty *ttystats;
		size_t ttystatssiz;
		int ttyc;

		ttystats_init(&ttystats, &ttyc, &ttystatssiz);
		err = sysctl_rdstruct(oldp, oldlenp, newp, ttystats,
		    ttyc * sizeof(struct itty));
		free(ttystats, M_SYSCTL, ttystatssiz);
		return (err);
	    }
	default:
#if NPTY > 0
		return (sysctl_pty(name, namelen, oldp, oldlenp, newp, newlen));
#else
		return (EOPNOTSUPP);
#endif
	}
	/* NOTREACHED */
}
#endif

void
ttytstamp(struct tty *tp, int octs, int ncts, int odcd, int ndcd)
{
	int doit = 0;

	if (ncts ^ octs)
		doit |= ncts ? ISSET(tp->t_flags, TS_TSTAMPCTSSET) :
		    ISSET(tp->t_flags, TS_TSTAMPCTSCLR);
	if (ndcd ^ odcd)
		doit |= ndcd ? ISSET(tp->t_flags, TS_TSTAMPDCDSET) :
		    ISSET(tp->t_flags, TS_TSTAMPDCDCLR);

	if (doit)
		microtime(&tp->t_tv);
}
