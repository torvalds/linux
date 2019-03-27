/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Ed Schouten <ed@FreeBSD.org>
 * All rights reserved.
 *
 * Portions of this software were developed under sponsorship from Snow
 * B.V., the Netherlands.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include <sys/param.h>
#include <sys/fcntl.h>
#include <sys/filio.h>
#include <sys/kernel.h>
#include <sys/signal.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/tty.h>
#include <sys/ttycom.h>
#include <sys/ttydefaults.h>
#include <sys/uio.h>
#include <sys/vnode.h>

/*
 * Standard TTYDISC `termios' line discipline.
 */

/* Statistics. */
static unsigned long tty_nin = 0;
SYSCTL_ULONG(_kern, OID_AUTO, tty_nin, CTLFLAG_RD,
	&tty_nin, 0, "Total amount of bytes received");
static unsigned long tty_nout = 0;
SYSCTL_ULONG(_kern, OID_AUTO, tty_nout, CTLFLAG_RD,
	&tty_nout, 0, "Total amount of bytes transmitted");

/* termios comparison macro's. */
#define	CMP_CC(v,c) (tp->t_termios.c_cc[v] != _POSIX_VDISABLE && \
			tp->t_termios.c_cc[v] == (c))
#define	CMP_FLAG(field,opt) (tp->t_termios.c_ ## field ## flag & (opt))

/* Characters that cannot be modified through c_cc. */
#define CTAB	'\t'
#define CNL	'\n'
#define CCR	'\r'

/* Character is a control character. */
#define CTL_VALID(c)	((c) == 0x7f || (unsigned char)(c) < 0x20)
/* Control character should be processed on echo. */
#define CTL_ECHO(c,q)	(!(q) && ((c) == CERASE2 || (c) == CTAB || \
    (c) == CNL || (c) == CCR))
/* Control character should be printed using ^X notation. */
#define CTL_PRINT(c,q)	((c) == 0x7f || ((unsigned char)(c) < 0x20 && \
    ((q) || ((c) != CTAB && (c) != CNL))))
/* Character is whitespace. */
#define CTL_WHITE(c)	((c) == ' ' || (c) == CTAB)
/* Character is alphanumeric. */
#define CTL_ALNUM(c)	(((c) >= '0' && (c) <= '9') || \
    ((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z'))

#define	TTY_STACKBUF	256

void
ttydisc_open(struct tty *tp)
{
	ttydisc_optimize(tp);
}

void
ttydisc_close(struct tty *tp)
{

	/* Clean up our flags when leaving the discipline. */
	tp->t_flags &= ~(TF_STOPPED|TF_HIWAT|TF_ZOMBIE);

	/*
	 * POSIX states that we must drain output and flush input on
	 * last close.  Draining has already been done if possible.
	 */
	tty_flush(tp, FREAD | FWRITE);

	if (ttyhook_hashook(tp, close))
		ttyhook_close(tp);
}

static int
ttydisc_read_canonical(struct tty *tp, struct uio *uio, int ioflag)
{
	char breakc[4] = { CNL }; /* enough to hold \n, VEOF and VEOL. */
	int error;
	size_t clen, flen = 0, n = 1;
	unsigned char lastc = _POSIX_VDISABLE;

#define BREAK_ADD(c) do { \
	if (tp->t_termios.c_cc[c] != _POSIX_VDISABLE)	\
		breakc[n++] = tp->t_termios.c_cc[c];	\
} while (0)
	/* Determine which characters we should trigger on. */
	BREAK_ADD(VEOF);
	BREAK_ADD(VEOL);
#undef BREAK_ADD
	breakc[n] = '\0';

	do {
		error = tty_wait_background(tp, curthread, SIGTTIN);
		if (error)
			return (error);

		/*
		 * Quite a tricky case: unlike the old TTY
		 * implementation, this implementation copies data back
		 * to userspace in large chunks. Unfortunately, we can't
		 * calculate the line length on beforehand if it crosses
		 * ttyinq_block boundaries, because multiple reads could
		 * then make this code read beyond the newline.
		 *
		 * This is why we limit the read to:
		 * - The size the user has requested
		 * - The blocksize (done in tty_inq.c)
		 * - The amount of bytes until the newline
		 *
		 * This causes the line length to be recalculated after
		 * each block has been copied to userspace. This will
		 * cause the TTY layer to return data in chunks using
		 * the blocksize (except the first and last blocks).
		 */
		clen = ttyinq_findchar(&tp->t_inq, breakc, uio->uio_resid,
		    &lastc);

		/* No more data. */
		if (clen == 0) {
			if (tp->t_flags & TF_ZOMBIE)
				return (0);
			else if (ioflag & IO_NDELAY)
				return (EWOULDBLOCK);

			error = tty_wait(tp, &tp->t_inwait);
			if (error)
				return (error);
			continue;
		}

		/* Don't send the EOF char back to userspace. */
		if (CMP_CC(VEOF, lastc))
			flen = 1;

		MPASS(flen <= clen);

		/* Read and throw away the EOF character. */
		error = ttyinq_read_uio(&tp->t_inq, tp, uio, clen, flen);
		if (error)
			return (error);

	} while (uio->uio_resid > 0 && lastc == _POSIX_VDISABLE);

	return (0);
}

static int
ttydisc_read_raw_no_timer(struct tty *tp, struct uio *uio, int ioflag)
{
	size_t vmin = tp->t_termios.c_cc[VMIN];
	ssize_t oresid = uio->uio_resid;
	int error;

	MPASS(tp->t_termios.c_cc[VTIME] == 0);

	/*
	 * This routine implements the easy cases of read()s while in
	 * non-canonical mode, namely case B and D, where we don't have
	 * any timers at all.
	 */

	for (;;) {
		error = tty_wait_background(tp, curthread, SIGTTIN);
		if (error)
			return (error);

		error = ttyinq_read_uio(&tp->t_inq, tp, uio,
		    uio->uio_resid, 0);
		if (error)
			return (error);
		if (uio->uio_resid == 0 || (oresid - uio->uio_resid) >= vmin)
			return (0);

		/* We have to wait for more. */
		if (tp->t_flags & TF_ZOMBIE)
			return (0);
		else if (ioflag & IO_NDELAY)
			return (EWOULDBLOCK);

		error = tty_wait(tp, &tp->t_inwait);
		if (error)
			return (error);
	}
}

static int
ttydisc_read_raw_read_timer(struct tty *tp, struct uio *uio, int ioflag,
    int oresid)
{
	size_t vmin = MAX(tp->t_termios.c_cc[VMIN], 1);
	unsigned int vtime = tp->t_termios.c_cc[VTIME];
	struct timeval end, now, left;
	int error, hz;

	MPASS(tp->t_termios.c_cc[VTIME] != 0);

	/* Determine when the read should be expired. */
	end.tv_sec = vtime / 10;
	end.tv_usec = (vtime % 10) * 100000;
	getmicrotime(&now);
	timevaladd(&end, &now);

	for (;;) {
		error = tty_wait_background(tp, curthread, SIGTTIN);
		if (error)
			return (error);

		error = ttyinq_read_uio(&tp->t_inq, tp, uio,
		    uio->uio_resid, 0);
		if (error)
			return (error);
		if (uio->uio_resid == 0 || (oresid - uio->uio_resid) >= vmin)
			return (0);

		/* Calculate how long we should wait. */
		getmicrotime(&now);
		if (timevalcmp(&now, &end, >))
			return (0);
		left = end;
		timevalsub(&left, &now);
		hz = tvtohz(&left);

		/*
		 * We have to wait for more. If the timer expires, we
		 * should return a 0-byte read.
		 */
		if (tp->t_flags & TF_ZOMBIE)
			return (0);
		else if (ioflag & IO_NDELAY)
			return (EWOULDBLOCK);

		error = tty_timedwait(tp, &tp->t_inwait, hz);
		if (error)
			return (error == EWOULDBLOCK ? 0 : error);
	}

	return (0);
}

static int
ttydisc_read_raw_interbyte_timer(struct tty *tp, struct uio *uio, int ioflag)
{
	size_t vmin = tp->t_termios.c_cc[VMIN];
	ssize_t oresid = uio->uio_resid;
	int error;

	MPASS(tp->t_termios.c_cc[VMIN] != 0);
	MPASS(tp->t_termios.c_cc[VTIME] != 0);

	/*
	 * When using the interbyte timer, the timer should be started
	 * after the first byte has been received. We just call into the
	 * generic read timer code after we've received the first byte.
	 */

	for (;;) {
		error = tty_wait_background(tp, curthread, SIGTTIN);
		if (error)
			return (error);

		error = ttyinq_read_uio(&tp->t_inq, tp, uio,
		    uio->uio_resid, 0);
		if (error)
			return (error);
		if (uio->uio_resid == 0 || (oresid - uio->uio_resid) >= vmin)
			return (0);

		/*
		 * Not enough data, but we did receive some, which means
		 * we'll now start using the interbyte timer.
		 */
		if (oresid != uio->uio_resid)
			break;

		/* We have to wait for more. */
		if (tp->t_flags & TF_ZOMBIE)
			return (0);
		else if (ioflag & IO_NDELAY)
			return (EWOULDBLOCK);

		error = tty_wait(tp, &tp->t_inwait);
		if (error)
			return (error);
	}

	return ttydisc_read_raw_read_timer(tp, uio, ioflag, oresid);
}

int
ttydisc_read(struct tty *tp, struct uio *uio, int ioflag)
{
	int error;

	tty_lock_assert(tp, MA_OWNED);

	if (uio->uio_resid == 0)
		return (0);

	if (CMP_FLAG(l, ICANON))
		error = ttydisc_read_canonical(tp, uio, ioflag);
	else if (tp->t_termios.c_cc[VTIME] == 0)
		error = ttydisc_read_raw_no_timer(tp, uio, ioflag);
	else if (tp->t_termios.c_cc[VMIN] == 0)
		error = ttydisc_read_raw_read_timer(tp, uio, ioflag,
		    uio->uio_resid);
	else
		error = ttydisc_read_raw_interbyte_timer(tp, uio, ioflag);

	if (ttyinq_bytesleft(&tp->t_inq) >= tp->t_inlow ||
	    ttyinq_bytescanonicalized(&tp->t_inq) == 0) {
		/* Unset the input watermark when we've got enough space. */
		tty_hiwat_in_unblock(tp);
	}

	return (error);
}

static __inline unsigned int
ttydisc_findchar(const char *obstart, unsigned int oblen)
{
	const char *c = obstart;

	while (oblen--) {
		if (CTL_VALID(*c))
			break;
		c++;
	}

	return (c - obstart);
}

static int
ttydisc_write_oproc(struct tty *tp, char c)
{
	unsigned int scnt, error;

	MPASS(CMP_FLAG(o, OPOST));
	MPASS(CTL_VALID(c));

#define PRINT_NORMAL() ttyoutq_write_nofrag(&tp->t_outq, &c, 1)
	switch (c) {
	case CEOF:
		/* End-of-text dropping. */
		if (CMP_FLAG(o, ONOEOT))
			return (0);
		return PRINT_NORMAL();

	case CERASE2:
		/* Handle backspace to fix tab expansion. */
		if (PRINT_NORMAL() != 0)
			return (-1);
		if (tp->t_column > 0)
			tp->t_column--;
		return (0);

	case CTAB:
		/* Tab expansion. */
		scnt = 8 - (tp->t_column & 7);
		if (CMP_FLAG(o, TAB3)) {
			error = ttyoutq_write_nofrag(&tp->t_outq,
			    "        ", scnt);
		} else {
			error = PRINT_NORMAL();
		}
		if (error)
			return (-1);

		tp->t_column += scnt;
		MPASS((tp->t_column % 8) == 0);
		return (0);

	case CNL:
		/* Newline conversion. */
		if (CMP_FLAG(o, ONLCR)) {
			/* Convert \n to \r\n. */
			error = ttyoutq_write_nofrag(&tp->t_outq, "\r\n", 2);
		} else {
			error = PRINT_NORMAL();
		}
		if (error)
			return (-1);

		if (CMP_FLAG(o, ONLCR|ONLRET)) {
			tp->t_column = tp->t_writepos = 0;
			ttyinq_reprintpos_set(&tp->t_inq);
		}
		return (0);

	case CCR:
		/* Carriage return to newline conversion. */
		if (CMP_FLAG(o, OCRNL))
			c = CNL;
		/* Omit carriage returns on column 0. */
		if (CMP_FLAG(o, ONOCR) && tp->t_column == 0)
			return (0);
		if (PRINT_NORMAL() != 0)
			return (-1);

		tp->t_column = tp->t_writepos = 0;
		ttyinq_reprintpos_set(&tp->t_inq);
		return (0);
	}

	/*
	 * Invisible control character. Print it, but don't
	 * increase the column count.
	 */
	return PRINT_NORMAL();
#undef PRINT_NORMAL
}

/*
 * Just like the old TTY implementation, we need to copy data in chunks
 * into a temporary buffer. One of the reasons why we need to do this,
 * is because output processing (only TAB3 though) may allow the buffer
 * to grow eight times.
 */
int
ttydisc_write(struct tty *tp, struct uio *uio, int ioflag)
{
	char ob[TTY_STACKBUF];
	char *obstart;
	int error = 0;
	unsigned int oblen = 0;

	tty_lock_assert(tp, MA_OWNED);

	if (tp->t_flags & TF_ZOMBIE)
		return (EIO);

	/*
	 * We don't need to check whether the process is the foreground
	 * process group or if we have a carrier. This is already done
	 * in ttydev_write().
	 */

	while (uio->uio_resid > 0) {
		unsigned int nlen;

		MPASS(oblen == 0);

		/* Step 1: read data. */
		obstart = ob;
		nlen = MIN(uio->uio_resid, sizeof ob);
		tty_unlock(tp);
		error = uiomove(ob, nlen, uio);
		tty_lock(tp);
		if (error != 0)
			break;
		oblen = nlen;

		if (tty_gone(tp)) {
			error = ENXIO;
			break;
		}

		MPASS(oblen > 0);

		/* Step 2: process data. */
		do {
			unsigned int plen, wlen;

			/* Search for special characters for post processing. */
			if (CMP_FLAG(o, OPOST)) {
				plen = ttydisc_findchar(obstart, oblen);
			} else {
				plen = oblen;
			}

			if (plen == 0) {
				/*
				 * We're going to process a character
				 * that needs processing
				 */
				if (ttydisc_write_oproc(tp, *obstart) == 0) {
					obstart++;
					oblen--;

					tp->t_writepos = tp->t_column;
					ttyinq_reprintpos_set(&tp->t_inq);
					continue;
				}
			} else {
				/* We're going to write regular data. */
				wlen = ttyoutq_write(&tp->t_outq, obstart, plen);
				obstart += wlen;
				oblen -= wlen;
				tp->t_column += wlen;

				tp->t_writepos = tp->t_column;
				ttyinq_reprintpos_set(&tp->t_inq);

				if (wlen == plen)
					continue;
			}

			/* Watermark reached. Try to sleep. */
			tp->t_flags |= TF_HIWAT_OUT;

			if (ioflag & IO_NDELAY) {
				error = EWOULDBLOCK;
				goto done;
			}

			/*
			 * The driver may write back the data
			 * synchronously. Be sure to check the high
			 * water mark before going to sleep.
			 */
			ttydevsw_outwakeup(tp);
			if ((tp->t_flags & TF_HIWAT_OUT) == 0)
				continue;

			error = tty_wait(tp, &tp->t_outwait);
			if (error)
				goto done;

			if (tp->t_flags & TF_ZOMBIE) {
				error = EIO;
				goto done;
			}
		} while (oblen > 0);
	}

done:
	if (!tty_gone(tp))
		ttydevsw_outwakeup(tp);

	/*
	 * Add the amount of bytes that we didn't process back to the
	 * uio counters. We need to do this to make sure write() doesn't
	 * count the bytes we didn't store in the queue.
	 */
	uio->uio_resid += oblen;
	return (error);
}

void
ttydisc_optimize(struct tty *tp)
{
	tty_lock_assert(tp, MA_OWNED);

	if (ttyhook_hashook(tp, rint_bypass)) {
		tp->t_flags |= TF_BYPASS;
	} else if (ttyhook_hashook(tp, rint)) {
		tp->t_flags &= ~TF_BYPASS;
	} else if (!CMP_FLAG(i, ICRNL|IGNCR|IMAXBEL|INLCR|ISTRIP|IXON) &&
	    (!CMP_FLAG(i, BRKINT) || CMP_FLAG(i, IGNBRK)) &&
	    (!CMP_FLAG(i, PARMRK) ||
		CMP_FLAG(i, IGNPAR|IGNBRK) == (IGNPAR|IGNBRK)) &&
	    !CMP_FLAG(l, ECHO|ICANON|IEXTEN|ISIG|PENDIN)) {
		tp->t_flags |= TF_BYPASS;
	} else {
		tp->t_flags &= ~TF_BYPASS;
	}
}

void
ttydisc_modem(struct tty *tp, int open)
{

	tty_lock_assert(tp, MA_OWNED);

	if (open)
		cv_broadcast(&tp->t_dcdwait);

	/*
	 * Ignore modem status lines when CLOCAL is turned on, but don't
	 * enter the zombie state when the TTY isn't opened, because
	 * that would cause the TTY to be in zombie state after being
	 * opened.
	 */
	if (!tty_opened(tp) || CMP_FLAG(c, CLOCAL))
		return;

	if (open == 0) {
		/*
		 * Lost carrier.
		 */
		tp->t_flags |= TF_ZOMBIE;

		tty_signal_sessleader(tp, SIGHUP);
		tty_flush(tp, FREAD|FWRITE);
	} else {
		/*
		 * Carrier is back again.
		 */

		/* XXX: what should we do here? */
	}
}

static int
ttydisc_echo_force(struct tty *tp, char c, int quote)
{

	if (CMP_FLAG(o, OPOST) && CTL_ECHO(c, quote)) {
		/*
		 * Only perform postprocessing when OPOST is turned on
		 * and the character is an unquoted BS/TB/NL/CR.
		 */
		return ttydisc_write_oproc(tp, c);
	} else if (CMP_FLAG(l, ECHOCTL) && CTL_PRINT(c, quote)) {
		/*
		 * Only use ^X notation when ECHOCTL is turned on and
		 * we've got an quoted control character.
		 *
		 * Print backspaces when echoing an end-of-file.
		 */
		char ob[4] = "^?\b\b";

		/* Print ^X notation. */
		if (c != 0x7f)
			ob[1] = c + 'A' - 1;

		if (!quote && CMP_CC(VEOF, c)) {
			return ttyoutq_write_nofrag(&tp->t_outq, ob, 4);
		} else {
			tp->t_column += 2;
			return ttyoutq_write_nofrag(&tp->t_outq, ob, 2);
		}
	} else {
		/* Can just be printed. */
		tp->t_column++;
		return ttyoutq_write_nofrag(&tp->t_outq, &c, 1);
	}
}

static int
ttydisc_echo(struct tty *tp, char c, int quote)
{

	/*
	 * Only echo characters when ECHO is turned on, or ECHONL when
	 * the character is an unquoted newline.
	 */
	if (!CMP_FLAG(l, ECHO) &&
	    (!CMP_FLAG(l, ECHONL) || c != CNL || quote))
		return (0);

	return ttydisc_echo_force(tp, c, quote);
}

static void
ttydisc_reprint_char(void *d, char c, int quote)
{
	struct tty *tp = d;

	ttydisc_echo(tp, c, quote);
}

static void
ttydisc_reprint(struct tty *tp)
{
	cc_t c;

	/* Print  ^R\n, followed by the line. */
	c = tp->t_termios.c_cc[VREPRINT];
	if (c != _POSIX_VDISABLE)
		ttydisc_echo(tp, c, 0);
	ttydisc_echo(tp, CNL, 0);
	ttyinq_reprintpos_reset(&tp->t_inq);

	ttyinq_line_iterate_from_linestart(&tp->t_inq, ttydisc_reprint_char, tp);
}

struct ttydisc_recalc_length {
	struct tty *tp;
	unsigned int curlen;
};

static void
ttydisc_recalc_charlength(void *d, char c, int quote)
{
	struct ttydisc_recalc_length *data = d;
	struct tty *tp = data->tp;

	if (CTL_PRINT(c, quote)) {
		if (CMP_FLAG(l, ECHOCTL))
			data->curlen += 2;
	} else if (c == CTAB) {
		data->curlen += 8 - (data->curlen & 7);
	} else {
		data->curlen++;
	}
}

static unsigned int
ttydisc_recalc_linelength(struct tty *tp)
{
	struct ttydisc_recalc_length data = { tp, tp->t_writepos };

	ttyinq_line_iterate_from_reprintpos(&tp->t_inq,
	    ttydisc_recalc_charlength, &data);
	return (data.curlen);
}

static int
ttydisc_rubchar(struct tty *tp)
{
	char c;
	int quote;
	unsigned int prevpos, tablen;

	if (ttyinq_peekchar(&tp->t_inq, &c, &quote) != 0)
		return (-1);
	ttyinq_unputchar(&tp->t_inq);

	if (CMP_FLAG(l, ECHO)) {
		/*
		 * Remove the character from the screen. This is even
		 * safe for characters that span multiple characters
		 * (tabs, quoted, etc).
		 */
		if (tp->t_writepos >= tp->t_column) {
			/* Retype the sentence. */
			ttydisc_reprint(tp);
		} else if (CMP_FLAG(l, ECHOE)) {
			if (CTL_PRINT(c, quote)) {
				/* Remove ^X formatted chars. */
				if (CMP_FLAG(l, ECHOCTL)) {
					tp->t_column -= 2;
					ttyoutq_write_nofrag(&tp->t_outq,
					    "\b\b  \b\b", 6);
				}
			} else if (c == ' ') {
				/* Space character needs no rubbing. */
				tp->t_column -= 1;
				ttyoutq_write_nofrag(&tp->t_outq, "\b", 1);
			} else if (c == CTAB) {
				/*
				 * Making backspace work with tabs is
				 * quite hard. Recalculate the length of
				 * this character and remove it.
				 *
				 * Because terminal settings could be
				 * changed while the line is being
				 * inserted, the calculations don't have
				 * to be correct. Make sure we keep the
				 * tab length within proper bounds.
				 */
				prevpos = ttydisc_recalc_linelength(tp);
				if (prevpos >= tp->t_column)
					tablen = 1;
				else
					tablen = tp->t_column - prevpos;
				if (tablen > 8)
					tablen = 8;

				tp->t_column = prevpos;
				ttyoutq_write_nofrag(&tp->t_outq,
				    "\b\b\b\b\b\b\b\b", tablen);
				return (0);
			} else {
				/*
				 * Remove a regular character by
				 * punching a space over it.
				 */
				tp->t_column -= 1;
				ttyoutq_write_nofrag(&tp->t_outq, "\b \b", 3);
			}
		} else {
			/* Don't print spaces. */
			ttydisc_echo(tp, tp->t_termios.c_cc[VERASE], 0);
		}
	}

	return (0);
}

static void
ttydisc_rubword(struct tty *tp)
{
	char c;
	int quote, alnum;

	/* Strip whitespace first. */
	for (;;) {
		if (ttyinq_peekchar(&tp->t_inq, &c, &quote) != 0)
			return;
		if (!CTL_WHITE(c))
			break;
		ttydisc_rubchar(tp);
	}

	/*
	 * Record whether the last character from the previous iteration
	 * was alphanumeric or not. We need this to implement ALTWERASE.
	 */
	alnum = CTL_ALNUM(c);
	for (;;) {
		ttydisc_rubchar(tp);

		if (ttyinq_peekchar(&tp->t_inq, &c, &quote) != 0)
			return;
		if (CTL_WHITE(c))
			return;
		if (CMP_FLAG(l, ALTWERASE) && CTL_ALNUM(c) != alnum)
			return;
	}
}

int
ttydisc_rint(struct tty *tp, char c, int flags)
{
	int signal, quote = 0;
	char ob[3] = { 0xff, 0x00 };
	size_t ol;

	tty_lock_assert(tp, MA_OWNED);

	atomic_add_long(&tty_nin, 1);

	if (ttyhook_hashook(tp, rint))
		return ttyhook_rint(tp, c, flags);

	if (tp->t_flags & TF_BYPASS)
		goto processed;

	if (flags) {
		if (flags & TRE_BREAK) {
			if (CMP_FLAG(i, IGNBRK)) {
				/* Ignore break characters. */
				return (0);
			} else if (CMP_FLAG(i, BRKINT)) {
				/* Generate SIGINT on break. */
				tty_flush(tp, FREAD|FWRITE);
				tty_signal_pgrp(tp, SIGINT);
				return (0);
			} else {
				/* Just print it. */
				goto parmrk;
			}
		} else if (flags & TRE_FRAMING ||
		    (flags & TRE_PARITY && CMP_FLAG(i, INPCK))) {
			if (CMP_FLAG(i, IGNPAR)) {
				/* Ignore bad characters. */
				return (0);
			} else {
				/* Just print it. */
				goto parmrk;
			}
		}
	}

	/* Allow any character to perform a wakeup. */
	if (CMP_FLAG(i, IXANY))
		tp->t_flags &= ~TF_STOPPED;

	/* Remove the top bit. */
	if (CMP_FLAG(i, ISTRIP))
		c &= ~0x80;

	/* Skip input processing when we want to print it literally. */
	if (tp->t_flags & TF_LITERAL) {
		tp->t_flags &= ~TF_LITERAL;
		quote = 1;
		goto processed;
	}

	/* Special control characters that are implementation dependent. */
	if (CMP_FLAG(l, IEXTEN)) {
		/* Accept the next character as literal. */
		if (CMP_CC(VLNEXT, c)) {
			if (CMP_FLAG(l, ECHO)) {
				if (CMP_FLAG(l, ECHOE))
					ttyoutq_write_nofrag(&tp->t_outq, "^\b", 2);
				else
					ttydisc_echo(tp, c, 0);
			}
			tp->t_flags |= TF_LITERAL;
			return (0);
		}
	}

	/*
	 * Handle signal processing.
	 */
	if (CMP_FLAG(l, ISIG)) {
		if (CMP_FLAG(l, ICANON|IEXTEN) == (ICANON|IEXTEN)) {
			if (CMP_CC(VSTATUS, c)) {
				tty_signal_pgrp(tp, SIGINFO);
				return (0);
			}
		}

		/*
		 * When compared to the old implementation, this
		 * implementation also flushes the output queue. POSIX
		 * is really brief about this, but does makes us assume
		 * we have to do so.
		 */
		signal = 0;
		if (CMP_CC(VINTR, c)) {
			signal = SIGINT;
		} else if (CMP_CC(VQUIT, c)) {
			signal = SIGQUIT;
		} else if (CMP_CC(VSUSP, c)) {
			signal = SIGTSTP;
		}

		if (signal != 0) {
			/*
			 * Echo the character before signalling the
			 * processes.
			 */
			if (!CMP_FLAG(l, NOFLSH))
				tty_flush(tp, FREAD|FWRITE);
			ttydisc_echo(tp, c, 0);
			tty_signal_pgrp(tp, signal);
			return (0);
		}
	}

	/*
	 * Handle start/stop characters.
	 */
	if (CMP_FLAG(i, IXON)) {
		if (CMP_CC(VSTOP, c)) {
			/* Stop it if we aren't stopped yet. */
			if ((tp->t_flags & TF_STOPPED) == 0) {
				tp->t_flags |= TF_STOPPED;
				return (0);
			}
			/*
			 * Fallthrough:
			 * When VSTART == VSTOP, we should make this key
			 * toggle it.
			 */
			if (!CMP_CC(VSTART, c))
				return (0);
		}
		if (CMP_CC(VSTART, c)) {
			tp->t_flags &= ~TF_STOPPED;
			return (0);
		}
	}

	/* Conversion of CR and NL. */
	switch (c) {
	case CCR:
		if (CMP_FLAG(i, IGNCR))
			return (0);
		if (CMP_FLAG(i, ICRNL))
			c = CNL;
		break;
	case CNL:
		if (CMP_FLAG(i, INLCR))
			c = CCR;
		break;
	}

	/* Canonical line editing. */
	if (CMP_FLAG(l, ICANON)) {
		if (CMP_CC(VERASE, c) || CMP_CC(VERASE2, c)) {
			ttydisc_rubchar(tp);
			return (0);
		} else if (CMP_CC(VKILL, c)) {
			while (ttydisc_rubchar(tp) == 0);
			return (0);
		} else if (CMP_FLAG(l, IEXTEN)) {
			if (CMP_CC(VWERASE, c)) {
				ttydisc_rubword(tp);
				return (0);
			} else if (CMP_CC(VREPRINT, c)) {
				ttydisc_reprint(tp);
				return (0);
			}
		}
	}

processed:
	if (CMP_FLAG(i, PARMRK) && (unsigned char)c == 0xff) {
		/* Print 0xff 0xff. */
		ob[1] = 0xff;
		ol = 2;
		quote = 1;
	} else {
		ob[0] = c;
		ol = 1;
	}

	goto print;

parmrk:
	if (CMP_FLAG(i, PARMRK)) {
		/* Prepend 0xff 0x00 0x.. */
		ob[2] = c;
		ol = 3;
		quote = 1;
	} else {
		ob[0] = c;
		ol = 1;
	}

print:
	/* See if we can store this on the input queue. */
	if (ttyinq_write_nofrag(&tp->t_inq, ob, ol, quote) != 0) {
		if (CMP_FLAG(i, IMAXBEL))
			ttyoutq_write_nofrag(&tp->t_outq, "\a", 1);

		/*
		 * Prevent a deadlock here. It may be possible that a
		 * user has entered so much data, there is no data
		 * available to read(), but the buffers are full anyway.
		 *
		 * Only enter the high watermark if the device driver
		 * can actually transmit something.
		 */
		if (ttyinq_bytescanonicalized(&tp->t_inq) == 0)
			return (0);

		tty_hiwat_in_block(tp);
		return (-1);
	}

	/*
	 * In raw mode, we canonicalize after receiving a single
	 * character. Otherwise, we canonicalize when we receive a
	 * newline, VEOL or VEOF, but only when it isn't quoted.
	 */
	if (!CMP_FLAG(l, ICANON) ||
	    (!quote && (c == CNL || CMP_CC(VEOL, c) || CMP_CC(VEOF, c)))) {
		ttyinq_canonicalize(&tp->t_inq);
	}

	ttydisc_echo(tp, c, quote);

	return (0);
}

size_t
ttydisc_rint_simple(struct tty *tp, const void *buf, size_t len)
{
	const char *cbuf;

	if (ttydisc_can_bypass(tp))
		return (ttydisc_rint_bypass(tp, buf, len));

	for (cbuf = buf; len-- > 0; cbuf++) {
		if (ttydisc_rint(tp, *cbuf, 0) != 0)
			break;
	}

	return (cbuf - (const char *)buf);
}

size_t
ttydisc_rint_bypass(struct tty *tp, const void *buf, size_t len)
{
	size_t ret;

	tty_lock_assert(tp, MA_OWNED);

	MPASS(tp->t_flags & TF_BYPASS);

	atomic_add_long(&tty_nin, len);

	if (ttyhook_hashook(tp, rint_bypass))
		return ttyhook_rint_bypass(tp, buf, len);

	ret = ttyinq_write(&tp->t_inq, buf, len, 0);
	ttyinq_canonicalize(&tp->t_inq);
	if (ret < len)
		tty_hiwat_in_block(tp);

	return (ret);
}

void
ttydisc_rint_done(struct tty *tp)
{

	tty_lock_assert(tp, MA_OWNED);

	if (ttyhook_hashook(tp, rint_done))
		ttyhook_rint_done(tp);

	/* Wake up readers. */
	tty_wakeup(tp, FREAD);
	/* Wake up driver for echo. */
	ttydevsw_outwakeup(tp);
}

size_t
ttydisc_rint_poll(struct tty *tp)
{
	size_t l;

	tty_lock_assert(tp, MA_OWNED);

	if (ttyhook_hashook(tp, rint_poll))
		return ttyhook_rint_poll(tp);

	/*
	 * XXX: Still allow character input when there's no space in the
	 * buffers, but we haven't entered the high watermark. This is
	 * to allow backspace characters to be inserted when in
	 * canonical mode.
	 */
	l = ttyinq_bytesleft(&tp->t_inq);
	if (l == 0 && (tp->t_flags & TF_HIWAT_IN) == 0)
		return (1);

	return (l);
}

static void
ttydisc_wakeup_watermark(struct tty *tp)
{
	size_t c;

	c = ttyoutq_bytesleft(&tp->t_outq);
	if (tp->t_flags & TF_HIWAT_OUT) {
		/* Only allow us to run when we're below the watermark. */
		if (c < tp->t_outlow)
			return;

		/* Reset the watermark. */
		tp->t_flags &= ~TF_HIWAT_OUT;
	} else {
		/* Only run when we have data at all. */
		if (c == 0)
			return;
	}
	tty_wakeup(tp, FWRITE);
}

size_t
ttydisc_getc(struct tty *tp, void *buf, size_t len)
{

	tty_lock_assert(tp, MA_OWNED);

	if (tp->t_flags & TF_STOPPED)
		return (0);

	if (ttyhook_hashook(tp, getc_inject))
		return ttyhook_getc_inject(tp, buf, len);

	len = ttyoutq_read(&tp->t_outq, buf, len);

	if (ttyhook_hashook(tp, getc_capture))
		ttyhook_getc_capture(tp, buf, len);

	ttydisc_wakeup_watermark(tp);
	atomic_add_long(&tty_nout, len);

	return (len);
}

int
ttydisc_getc_uio(struct tty *tp, struct uio *uio)
{
	int error = 0;
	ssize_t obytes = uio->uio_resid;
	size_t len;
	char buf[TTY_STACKBUF];

	tty_lock_assert(tp, MA_OWNED);

	if (tp->t_flags & TF_STOPPED)
		return (0);

	/*
	 * When a TTY hook is attached, we cannot perform unbuffered
	 * copying to userspace. Just call ttydisc_getc() and
	 * temporarily store data in a shadow buffer.
	 */
	if (ttyhook_hashook(tp, getc_capture) ||
	    ttyhook_hashook(tp, getc_inject)) {
		while (uio->uio_resid > 0) {
			/* Read to shadow buffer. */
			len = ttydisc_getc(tp, buf,
			    MIN(uio->uio_resid, sizeof buf));
			if (len == 0)
				break;

			/* Copy to userspace. */
			tty_unlock(tp);
			error = uiomove(buf, len, uio);
			tty_lock(tp);

			if (error != 0)
				break;
		}
	} else {
		error = ttyoutq_read_uio(&tp->t_outq, tp, uio);

		ttydisc_wakeup_watermark(tp);
		atomic_add_long(&tty_nout, obytes - uio->uio_resid);
	}

	return (error);
}

size_t
ttydisc_getc_poll(struct tty *tp)
{

	tty_lock_assert(tp, MA_OWNED);

	if (tp->t_flags & TF_STOPPED)
		return (0);

	if (ttyhook_hashook(tp, getc_poll))
		return ttyhook_getc_poll(tp);

	return ttyoutq_bytesused(&tp->t_outq);
}

/*
 * XXX: not really related to the TTYDISC, but we'd better put
 * tty_putchar() here, because we need to perform proper output
 * processing.
 */

int
tty_putstrn(struct tty *tp, const char *p, size_t n)
{
	size_t i;

	tty_lock_assert(tp, MA_OWNED);

	if (tty_gone(tp))
		return (-1);

	for (i = 0; i < n; i++)
		ttydisc_echo_force(tp, p[i], 0);

	tp->t_writepos = tp->t_column;
	ttyinq_reprintpos_set(&tp->t_inq);

	ttydevsw_outwakeup(tp);
	return (0);
}

int
tty_putchar(struct tty *tp, char c)
{
	return (tty_putstrn(tp, &c, 1));
}
