/*	$OpenBSD: tty_pty.c,v 1.115 2024/11/05 06:03:19 jsg Exp $	*/
/*	$NetBSD: tty_pty.c,v 1.33.4.1 1996/06/02 09:08:11 mrg Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1989, 1993
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
 *
 *	@(#)tty_pty.c	8.4 (Berkeley) 2/20/95
 */

/*
 * Pseudo-teletype Driver
 * (Actually two drivers, requiring two entries in 'cdevsw')
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/namei.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <sys/proc.h>
#include <sys/tty.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/uio.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/vnode.h>
#include <sys/signalvar.h>
#include <sys/conf.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/pledge.h>
#include <sys/rwlock.h>

#define BUFSIZ 100		/* Chunk size iomoved to/from user */

/*
 * pts == /dev/tty[p-zP-T][0-9a-zA-Z]
 * ptc == /dev/pty[p-zP-T][0-9a-zA-Z]
 */

/* XXX this needs to come from somewhere sane, and work with MAKEDEV */
#define TTY_LETTERS "pqrstuvwxyzPQRST"
#define TTY_SUFFIX "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"

static int pts_major;

struct	pt_softc {
	struct	tty *pt_tty;
	int	pt_flags;
	struct	selinfo pt_selr, pt_selw;
	u_char	pt_send;
	u_char	pt_ucntl;
	char	pty_pn[11];
	char	pty_sn[11];
};

#define	NPTY_MIN		8	/* number of initial ptys */
#define NPTY_MAX		992	/* maximum number of ptys supported */

static struct pt_softc **pt_softc = NULL;	/* pty array */
static int npty = 0;				/* size of pty array */
static int maxptys = NPTY_MAX;			/* maximum number of ptys */
/* for pty array */
struct rwlock pt_softc_lock = RWLOCK_INITIALIZER("ptarrlk");

#define	PF_PKT		0x08		/* packet mode */
#define	PF_STOPPED	0x10		/* user told stopped */
#define	PF_REMOTE	0x20		/* remote and flow controlled input */
#define	PF_NOSTOP	0x40
#define PF_UCNTL	0x80		/* user control mode */

void	ptyattach(int);
void	ptcwakeup(struct tty *, int);
struct tty *ptytty(dev_t);
void	ptsstart(struct tty *);
int	sysctl_pty(int *, u_int, void *, size_t *, void *, size_t);

void	filt_ptcrdetach(struct knote *);
int	filt_ptcread(struct knote *, long);
void	filt_ptcwdetach(struct knote *);
int	filt_ptcwrite(struct knote *, long);
int	filt_ptcexcept(struct knote *, long);

static struct pt_softc **ptyarralloc(int);
static int check_pty(int);

static gid_t tty_gid = TTY_GID;

void	ptydevname(int, struct pt_softc *);
dev_t	pty_getfree(void);

void	ptmattach(int);
int	ptmopen(dev_t, int, int, struct proc *);
int	ptmclose(dev_t, int, int, struct proc *);
int	ptmioctl(dev_t, u_long, caddr_t, int, struct proc *p);
static int ptm_vn_open(struct nameidata *);

void
ptydevname(int minor, struct pt_softc *pti)
{
	char buf[11] = "/dev/XtyXX";
	int i, j;

	i = minor / (sizeof(TTY_SUFFIX) - 1);
	j = minor % (sizeof(TTY_SUFFIX) - 1);
	if (i >= sizeof(TTY_LETTERS) - 1) {
		pti->pty_pn[0] = '\0';
		pti->pty_sn[0] = '\0';
		return;
	}
	buf[5] = 'p';
	buf[8] = TTY_LETTERS[i];
	buf[9] = TTY_SUFFIX[j];
	memcpy(pti->pty_pn, buf, sizeof(buf));
	buf[5] = 't';
	memcpy(pti->pty_sn, buf, sizeof(buf));
}

/*
 * Allocate and zero array of nelem elements.
 */
struct pt_softc **
ptyarralloc(int nelem)
{
	struct pt_softc **pt;

	pt = mallocarray(nelem, sizeof(struct pt_softc *), M_DEVBUF,
	    M_WAITOK|M_ZERO);
	return pt;
}

/*
 * Check if the minor is correct and ensure necessary structures
 * are properly allocated.
 */
int
check_pty(int dev)
{
	struct pt_softc *pti;
	int minor = minor(dev);

	rw_enter_write(&pt_softc_lock);
	if (minor >= npty) {
		struct pt_softc **newpt;
		int newnpty;

		/* check if the requested pty can be granted */
		if (minor >= maxptys)
			goto limit_reached;

		/* grow pty array by powers of two, up to maxptys */
		for (newnpty = npty; newnpty <= minor; newnpty *= 2)
			;

		if (newnpty > maxptys)
			newnpty = maxptys;
		newpt = ptyarralloc(newnpty);

		memcpy(newpt, pt_softc, npty * sizeof(struct pt_softc *));
		free(pt_softc, M_DEVBUF, npty * sizeof(struct pt_softc *));
		pt_softc = newpt;
		npty = newnpty;
	}

	/*
	 * If the entry is not yet allocated, allocate one.
	 */
	if (!pt_softc[minor]) {
		pti = malloc(sizeof(struct pt_softc), M_DEVBUF,
		    M_WAITOK|M_ZERO);
		pti->pt_tty = ttymalloc(1000000);
		pti->pt_tty->t_dev = dev;
		ptydevname(minor, pti);
		pt_softc[minor] = pti;
	}
	rw_exit_write(&pt_softc_lock);
	return (0);
limit_reached:
	rw_exit_write(&pt_softc_lock);
	tablefull("pty");
	return (ENXIO);
}

/*
 * Establish n (or default if n is 1) ptys in the system.
 */
void
ptyattach(int n)
{
	/* maybe should allow 0 => none? */
	if (n <= 1)
		n = NPTY_MIN;
	pt_softc = ptyarralloc(n);
	npty = n;

	/*
	 * If we have pty, we need ptm too.
	 */
	ptmattach(1);
}

int
ptsopen(dev_t dev, int flag, int devtype, struct proc *p)
{
	struct pt_softc *pti;
	struct tty *tp;
	int error;

	if ((error = check_pty(dev)))
		return (error);

	pti = pt_softc[minor(dev)];
	tp = pti->pt_tty;
	if ((tp->t_state & TS_ISOPEN) == 0) {
		tp->t_state |= TS_WOPEN;
		ttychars(tp);		/* Set up default chars */
		tp->t_iflag = TTYDEF_IFLAG;
		tp->t_oflag = TTYDEF_OFLAG;
		tp->t_lflag = TTYDEF_LFLAG;
		tp->t_cflag = TTYDEF_CFLAG;
		tp->t_ispeed = tp->t_ospeed = B115200;
		ttsetwater(tp);		/* would be done in xxparam() */
	} else if (tp->t_state & TS_XCLUDE && suser(p) != 0)
		return (EBUSY);
	if (tp->t_oproc)			/* Ctrlr still around. */
		tp->t_state |= TS_CARR_ON;
	while ((tp->t_state & TS_CARR_ON) == 0) {
		tp->t_state |= TS_WOPEN;
		if (flag & FNONBLOCK)
			break;
		error = ttysleep(tp, &tp->t_rawq, TTIPRI | PCATCH, ttopen);
		if (error)
			return (error);
	}
	error = (*linesw[tp->t_line].l_open)(dev, tp, p);
	ptcwakeup(tp, FREAD|FWRITE);
	return (error);
}

int
ptsclose(dev_t dev, int flag, int mode, struct proc *p)
{
	struct pt_softc *pti = pt_softc[minor(dev)];
	struct tty *tp = pti->pt_tty;
	int error;

	error = (*linesw[tp->t_line].l_close)(tp, flag, p);
	error |= ttyclose(tp);
	ptcwakeup(tp, FREAD|FWRITE);
	return (error);
}

int
ptsread(dev_t dev, struct uio *uio, int flag)
{
	struct proc *p = curproc;
	struct process *pr = p->p_p;
	struct pt_softc *pti = pt_softc[minor(dev)];
	struct tty *tp = pti->pt_tty;
	int error = 0;

again:
	if (pti->pt_flags & PF_REMOTE) {
		while (isbackground(pr, tp)) {
			if (sigismasked(p, SIGTTIN) ||
			    pr->ps_pgrp->pg_jobc == 0 ||
			    pr->ps_flags & PS_PPWAIT)
				return (EIO);
			pgsignal(pr->ps_pgrp, SIGTTIN, 1);
			error = ttysleep(tp, &lbolt, TTIPRI | PCATCH, ttybg);
			if (error)
				return (error);
		}
		if (tp->t_canq.c_cc == 0) {
			if (flag & IO_NDELAY)
				return (EWOULDBLOCK);
			error = ttysleep(tp, &tp->t_canq,
			    TTIPRI | PCATCH, ttyin);
			if (error)
				return (error);
			goto again;
		}
		while (tp->t_canq.c_cc > 1 && uio->uio_resid > 0)
			if (ureadc(getc(&tp->t_canq), uio) < 0) {
				error = EFAULT;
				break;
			}
		if (tp->t_canq.c_cc == 1)
			(void) getc(&tp->t_canq);
		if (tp->t_canq.c_cc)
			return (error);
	} else
		if (tp->t_oproc)
			error = (*linesw[tp->t_line].l_read)(tp, uio, flag);
	ptcwakeup(tp, FWRITE);
	return (error);
}

/*
 * Write to pseudo-tty.
 * Wakeups of controlling tty will happen
 * indirectly, when tty driver calls ptsstart.
 */
int
ptswrite(dev_t dev, struct uio *uio, int flag)
{
	struct pt_softc *pti = pt_softc[minor(dev)];
	struct tty *tp = pti->pt_tty;

	if (tp->t_oproc == NULL)
		return (EIO);
	return ((*linesw[tp->t_line].l_write)(tp, uio, flag));
}

/*
 * Start output on pseudo-tty.
 * Wake up process polling or sleeping for input from controlling tty.
 */
void
ptsstart(struct tty *tp)
{
	struct pt_softc *pti = pt_softc[minor(tp->t_dev)];

	if (tp->t_state & TS_TTSTOP)
		return;
	if (pti->pt_flags & PF_STOPPED) {
		pti->pt_flags &= ~PF_STOPPED;
		pti->pt_send = TIOCPKT_START;
	}
	ptcwakeup(tp, FREAD);
}

int
ptsstop(struct tty *tp, int flush)
{
	struct pt_softc *pti = pt_softc[minor(tp->t_dev)];
	int flag;

	/* note: FLUSHREAD and FLUSHWRITE already ok */
	if (flush == 0) {
		flush = TIOCPKT_STOP;
		pti->pt_flags |= PF_STOPPED;
	} else
		pti->pt_flags &= ~PF_STOPPED;
	pti->pt_send |= flush;
	/* change of perspective */
	flag = 0;
	if (flush & FREAD)
		flag |= FWRITE;
	if (flush & FWRITE)
		flag |= FREAD;
	ptcwakeup(tp, flag);
	return 0;
}

void
ptcwakeup(struct tty *tp, int flag)
{
	struct pt_softc *pti = pt_softc[minor(tp->t_dev)];

	if (flag & FREAD) {
		selwakeup(&pti->pt_selr);
		wakeup(&tp->t_outq.c_cf);
	}
	if (flag & FWRITE) {
		selwakeup(&pti->pt_selw);
		wakeup(&tp->t_rawq.c_cf);
	}
}

int ptcopen(dev_t, int, int, struct proc *);

int
ptcopen(dev_t dev, int flag, int devtype, struct proc *p)
{
	struct pt_softc *pti;
	struct tty *tp;
	int error;

	if ((error = check_pty(dev)))
		return (error);

	pti = pt_softc[minor(dev)];
	tp = pti->pt_tty;
	if (tp->t_oproc)
		return (EIO);
	tp->t_oproc = ptsstart;
	(void)(*linesw[tp->t_line].l_modem)(tp, 1);
	tp->t_lflag &= ~EXTPROC;
	pti->pt_flags = 0;
	pti->pt_send = 0;
	pti->pt_ucntl = 0;
	return (0);
}

int
ptcclose(dev_t dev, int flag, int devtype, struct proc *p)
{
	struct pt_softc *pti = pt_softc[minor(dev)];
	struct tty *tp = pti->pt_tty;

	(void)(*linesw[tp->t_line].l_modem)(tp, 0);
	tp->t_state &= ~TS_CARR_ON;
	tp->t_oproc = NULL;		/* mark closed */
	return (0);
}

int
ptcread(dev_t dev, struct uio *uio, int flag)
{
	struct pt_softc *pti = pt_softc[minor(dev)];
	struct tty *tp = pti->pt_tty;
	char buf[BUFSIZ];
	int error = 0, cc, bufcc = 0;

	/*
	 * We want to block until the slave
	 * is open, and there's something to read;
	 * but if we lost the slave or we're NBIO,
	 * then return the appropriate error instead.
	 */
	for (;;) {
		if (tp->t_state & TS_ISOPEN) {
			if (pti->pt_flags & PF_PKT && pti->pt_send) {
				error = ureadc((int)pti->pt_send, uio);
				if (error)
					return (error);
				if (pti->pt_send & TIOCPKT_IOCTL) {
					cc = MIN(uio->uio_resid,
						sizeof(tp->t_termios));
					error = uiomove(&tp->t_termios, cc, uio);
					if (error)
						return (error);
				}
				pti->pt_send = 0;
				return (0);
			}
			if (pti->pt_flags & PF_UCNTL && pti->pt_ucntl) {
				error = ureadc((int)pti->pt_ucntl, uio);
				if (error)
					return (error);
				pti->pt_ucntl = 0;
				return (0);
			}
			if (tp->t_outq.c_cc && (tp->t_state & TS_TTSTOP) == 0)
				break;
		}
		if ((tp->t_state & TS_CARR_ON) == 0)
			return (0);	/* EOF */
		if (flag & IO_NDELAY)
			return (EWOULDBLOCK);
		error = tsleep_nsec(&tp->t_outq.c_cf, TTIPRI | PCATCH, ttyin,
		    INFSLP);
		if (error)
			return (error);
	}
	if (pti->pt_flags & (PF_PKT|PF_UCNTL))
		error = ureadc(0, uio);
	while (uio->uio_resid > 0 && error == 0) {
		cc = MIN(uio->uio_resid, BUFSIZ);
		cc = q_to_b(&tp->t_outq, buf, cc);
		if (cc > bufcc)
			bufcc = cc;
		if (cc <= 0)
			break;
		error = uiomove(buf, cc, uio);
	}
	ttwakeupwr(tp);
	if (bufcc)
		explicit_bzero(buf, bufcc);
	return (error);
}


int
ptcwrite(dev_t dev, struct uio *uio, int flag)
{
	struct pt_softc *pti = pt_softc[minor(dev)];
	struct tty *tp = pti->pt_tty;
	u_char *cp = NULL;
	int cc = 0, bufcc = 0;
	u_char buf[BUFSIZ];
	size_t cnt = 0;
	int error = 0;

again:
	if ((tp->t_state & TS_ISOPEN) == 0)
		goto block;
	if (pti->pt_flags & PF_REMOTE) {
		if (tp->t_canq.c_cc)
			goto block;
		while (uio->uio_resid > 0 && tp->t_canq.c_cc < TTYHOG(tp) - 1) {
			if (cc == 0) {
				cc = MIN(uio->uio_resid, BUFSIZ);
				cc = min(cc, TTYHOG(tp) - 1 - tp->t_canq.c_cc);
				if (cc > bufcc)
					bufcc = cc;
				cp = buf;
				error = uiomove(cp, cc, uio);
				if (error)
					goto done;
				/* check again for safety */
				if ((tp->t_state & TS_ISOPEN) == 0) {
					error = EIO;
					goto done;
				}
			}
			if (cc)
				(void) b_to_q((char *)cp, cc, &tp->t_canq);
			cc = 0;
		}
		(void) putc(0, &tp->t_canq);
		ttwakeup(tp);
		wakeup(&tp->t_canq);
		goto done;
	}
	do {
		if (cc == 0) {
			cc = MIN(uio->uio_resid, BUFSIZ);
			if (cc > bufcc)
				bufcc = cc;
			cp = buf;
			error = uiomove(cp, cc, uio);
			if (error)
				goto done;
			/* check again for safety */
			if ((tp->t_state & TS_ISOPEN) == 0) {
				error = EIO;
				goto done;
			}
		}
		bufcc = cc;
		while (cc > 0) {
			if ((tp->t_rawq.c_cc + tp->t_canq.c_cc) >= TTYHOG(tp) - 2 &&
			   (tp->t_canq.c_cc > 0 || !ISSET(tp->t_lflag, ICANON))) {
				wakeup(&tp->t_rawq);
				goto block;
			}
			if ((*linesw[tp->t_line].l_rint)(*cp++, tp) == 1 &&
			    tsleep(tp, TTIPRI | PCATCH, "ttyretype", 1) == EINTR)
				goto interrupt;
			cnt++;
			cc--;
		}
		cc = 0;
	} while (uio->uio_resid > 0);
	goto done;
block:
	/*
	 * Come here to wait for slave to open, for space
	 * in outq, or space in rawq.
	 */
	if ((tp->t_state & TS_CARR_ON) == 0) {
		error = EIO;
		goto done;
	}
	if (flag & IO_NDELAY) {
		/* adjust for data copied in but not written */
		uio->uio_resid += cc;
		if (cnt == 0)
			error = EWOULDBLOCK;
		goto done;
	}
	error = tsleep_nsec(&tp->t_rawq.c_cf, TTOPRI | PCATCH, ttyout, INFSLP);
	if (error == 0)
		goto again;

interrupt:
	/* adjust for data copied in but not written */
	uio->uio_resid += cc;
done:
	if (bufcc)
		explicit_bzero(buf, bufcc);
	return (error);
}

void
filt_ptcrdetach(struct knote *kn)
{
	struct pt_softc *pti = (struct pt_softc *)kn->kn_hook;
	int s;

	s = spltty();
	klist_remove_locked(&pti->pt_selr.si_note, kn);
	splx(s);
}

int
filt_ptcread(struct knote *kn, long hint)
{
	struct pt_softc *pti = (struct pt_softc *)kn->kn_hook;
	struct tty *tp;
	int active;

	tp = pti->pt_tty;
	kn->kn_data = 0;

	if (ISSET(tp->t_state, TS_ISOPEN)) {
		if (!ISSET(tp->t_state, TS_TTSTOP))
			kn->kn_data = tp->t_outq.c_cc;
		if (((pti->pt_flags & PF_PKT) && pti->pt_send) ||
		    ((pti->pt_flags & PF_UCNTL) && pti->pt_ucntl))
			kn->kn_data++;
	}
	active = (kn->kn_data > 0);

	if (!ISSET(tp->t_state, TS_CARR_ON)) {
		kn->kn_flags |= EV_EOF;
		if (kn->kn_flags & __EV_POLL)
			kn->kn_flags |= __EV_HUP;
		active = 1;
	} else {
		kn->kn_flags &= ~(EV_EOF | __EV_HUP);
	}

	return (active);
}

void
filt_ptcwdetach(struct knote *kn)
{
	struct pt_softc *pti = (struct pt_softc *)kn->kn_hook;
	int s;

	s = spltty();
	klist_remove_locked(&pti->pt_selw.si_note, kn);
	splx(s);
}

int
filt_ptcwrite(struct knote *kn, long hint)
{
	struct pt_softc *pti = (struct pt_softc *)kn->kn_hook;
	struct tty *tp;
	int active;

	tp = pti->pt_tty;
	kn->kn_data = 0;

	if (ISSET(tp->t_state, TS_ISOPEN)) {
		if (ISSET(pti->pt_flags, PF_REMOTE)) {
			if (tp->t_canq.c_cc == 0)
				kn->kn_data = tp->t_canq.c_cn;
		} else if ((tp->t_rawq.c_cc + tp->t_canq.c_cc < TTYHOG(tp)-2) ||
		    (tp->t_canq.c_cc == 0 && ISSET(tp->t_lflag, ICANON)))
			kn->kn_data = tp->t_canq.c_cn -
			    (tp->t_rawq.c_cc + tp->t_canq.c_cc);
	}
	active = (kn->kn_data > 0);

	/* Write-side HUP condition is only for poll(2) and select(2). */
	if (kn->kn_flags & (__EV_POLL | __EV_SELECT)) {
		if (!ISSET(tp->t_state, TS_CARR_ON)) {
			kn->kn_flags |= __EV_HUP;
			active = 1;
		} else {
			kn->kn_flags &= ~__EV_HUP;
		}
	}

	return (active);
}

int
filt_ptcexcept(struct knote *kn, long hint)
{
	struct pt_softc *pti = (struct pt_softc *)kn->kn_hook;
	struct tty *tp;
	int active = 0;

	tp = pti->pt_tty;

	if (kn->kn_sfflags & NOTE_OOB) {
		/* If in packet or user control mode, check for data. */
		if (((pti->pt_flags & PF_PKT) && pti->pt_send) ||
		    ((pti->pt_flags & PF_UCNTL) && pti->pt_ucntl)) {
			kn->kn_fflags |= NOTE_OOB;
			kn->kn_data = 1;
			active = 1;
		}
	}

	if (kn->kn_flags & __EV_POLL) {
		if (!ISSET(tp->t_state, TS_CARR_ON)) {
			kn->kn_flags |= __EV_HUP;
			active = 1;
		} else {
			kn->kn_flags &= ~__EV_HUP;
		}
	}

	return (active);
}

const struct filterops ptcread_filtops = {
	.f_flags	= FILTEROP_ISFD,
	.f_attach	= NULL,
	.f_detach	= filt_ptcrdetach,
	.f_event	= filt_ptcread,
};

const struct filterops ptcwrite_filtops = {
	.f_flags	= FILTEROP_ISFD,
	.f_attach	= NULL,
	.f_detach	= filt_ptcwdetach,
	.f_event	= filt_ptcwrite,
};

const struct filterops ptcexcept_filtops = {
	.f_flags	= FILTEROP_ISFD,
	.f_attach	= NULL,
	.f_detach	= filt_ptcrdetach,
	.f_event	= filt_ptcexcept,
};

int
ptckqfilter(dev_t dev, struct knote *kn)
{
	struct pt_softc *pti = pt_softc[minor(dev)];
	struct klist *klist;
	int s;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		klist = &pti->pt_selr.si_note;
		kn->kn_fop = &ptcread_filtops;
		break;
	case EVFILT_WRITE:
		klist = &pti->pt_selw.si_note;
		kn->kn_fop = &ptcwrite_filtops;
		break;
	case EVFILT_EXCEPT:
		klist = &pti->pt_selr.si_note;
		kn->kn_fop = &ptcexcept_filtops;
		break;
	default:
		return (EINVAL);
	}

	kn->kn_hook = (caddr_t)pti;

	s = spltty();
	klist_insert_locked(klist, kn);
	splx(s);

	return (0);
}

struct tty *
ptytty(dev_t dev)
{
	struct pt_softc *pti = pt_softc[minor(dev)];
	struct tty *tp = pti->pt_tty;

	return (tp);
}

int
ptyioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct pt_softc *pti = pt_softc[minor(dev)];
	struct tty *tp = pti->pt_tty;
	u_char *cc = tp->t_cc;
	int stop, error;

	/*
	 * IF CONTROLLER STTY THEN MUST FLUSH TO PREVENT A HANG.
	 * ttywflush(tp) will hang if there are characters in the outq.
	 */
	if (cmd == TIOCEXT) {
		/*
		 * When the EXTPROC bit is being toggled, we need
		 * to send an TIOCPKT_IOCTL if the packet driver
		 * is turned on.
		 */
		if (*(int *)data) {
			if (pti->pt_flags & PF_PKT) {
				pti->pt_send |= TIOCPKT_IOCTL;
				ptcwakeup(tp, FREAD);
			}
			tp->t_lflag |= EXTPROC;
		} else {
			if ((tp->t_lflag & EXTPROC) &&
			    (pti->pt_flags & PF_PKT)) {
				pti->pt_send |= TIOCPKT_IOCTL;
				ptcwakeup(tp, FREAD);
			}
			tp->t_lflag &= ~EXTPROC;
		}
		return(0);
	} else if (cdevsw[major(dev)].d_open == ptcopen)
		switch (cmd) {

		case TIOCGPGRP:
			/*
			 * We avoid calling ttioctl on the controller since,
			 * in that case, tp must be the controlling terminal.
			 */
			*(int *)data = tp->t_pgrp ? tp->t_pgrp->pg_id : 0;
			return (0);

		case TIOCPKT:
			if (*(int *)data) {
				if (pti->pt_flags & PF_UCNTL)
					return (EINVAL);
				pti->pt_flags |= PF_PKT;
			} else
				pti->pt_flags &= ~PF_PKT;
			return (0);

		case TIOCUCNTL:
			if (*(int *)data) {
				if (pti->pt_flags & PF_PKT)
					return (EINVAL);
				pti->pt_flags |= PF_UCNTL;
			} else
				pti->pt_flags &= ~PF_UCNTL;
			return (0);

		case TIOCREMOTE:
			if (*(int *)data)
				pti->pt_flags |= PF_REMOTE;
			else
				pti->pt_flags &= ~PF_REMOTE;
			ttyflush(tp, FREAD|FWRITE);
			return (0);

		case TIOCSETD:
		case TIOCSETA:
		case TIOCSETAW:
		case TIOCSETAF:
			ndflush(&tp->t_outq, tp->t_outq.c_cc);
			break;

		case TIOCSIG:
			if (*(unsigned int *)data >= NSIG ||
			    *(unsigned int *)data == 0)
				return(EINVAL);
			if ((tp->t_lflag & NOFLSH) == 0)
				ttyflush(tp, FREAD|FWRITE);
			pgsignal(tp->t_pgrp, *(unsigned int *)data, 1);
			if ((*(unsigned int *)data == SIGINFO) &&
			    ((tp->t_lflag & NOKERNINFO) == 0))
				ttyinfo(tp);
			return (0);

		case FIONREAD:
			/*
			 * FIONREAD on the master side must return the amount
			 * in the output queue rather than the input.
			 */
			*(int *)data = tp->t_outq.c_cc;
			return (0);
		}
	error = (*linesw[tp->t_line].l_ioctl)(tp, cmd, data, flag, p);
	if (error < 0)
		 error = ttioctl(tp, cmd, data, flag, p);
	if (error < 0) {
		/*
		 * Translate TIOCSBRK/TIOCCBRK to user mode ioctls to
		 * let the master interpret BREAK conditions.
		 */
		switch (cmd) {
		case TIOCSBRK:
			cmd = UIOCCMD(TIOCUCNTL_SBRK);
			break;
		case TIOCCBRK:
			cmd = UIOCCMD(TIOCUCNTL_CBRK);
			break;
		default:
			break;
		}
		if (pti->pt_flags & PF_UCNTL &&
		    (cmd & ~0xff) == UIOCCMD(0)) {
			if (cmd & 0xff) {
				pti->pt_ucntl = (u_char)cmd;
				ptcwakeup(tp, FREAD);
			}
			return (0);
		}
		error = ENOTTY;
	}
	/*
	 * If external processing and packet mode send ioctl packet.
	 */
	if ((tp->t_lflag & EXTPROC) && (pti->pt_flags & PF_PKT)) {
		switch (cmd) {
		case TIOCSETA:
		case TIOCSETAW:
		case TIOCSETAF:
			pti->pt_send |= TIOCPKT_IOCTL;
			ptcwakeup(tp, FREAD);
		default:
			break;
		}
	}
	stop = (tp->t_iflag & IXON) && CCEQ(cc[VSTOP], CTRL('s')) &&
	    CCEQ(cc[VSTART], CTRL('q'));
	if (pti->pt_flags & PF_NOSTOP) {
		if (stop) {
			pti->pt_send &= ~TIOCPKT_NOSTOP;
			pti->pt_send |= TIOCPKT_DOSTOP;
			pti->pt_flags &= ~PF_NOSTOP;
			ptcwakeup(tp, FREAD);
		}
	} else {
		if (!stop) {
			pti->pt_send &= ~TIOCPKT_DOSTOP;
			pti->pt_send |= TIOCPKT_NOSTOP;
			pti->pt_flags |= PF_NOSTOP;
			ptcwakeup(tp, FREAD);
		}
	}
	return (error);
}

/*
 * Return pty-related information.
 */
int
sysctl_pty(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen)
{
	if (namelen != 1)
		return (ENOTDIR);

	switch (name[0]) {
	default:
		return (EOPNOTSUPP);
	}
	/* NOTREACHED */
}

/*
 * Check if a pty is free to use.
 */
static int
pty_isfree_locked(int minor)
{
	struct pt_softc *pt = pt_softc[minor];

	return (pt == NULL || pt->pt_tty == NULL ||
	    pt->pt_tty->t_oproc == NULL);
}

static int
pty_isfree(int minor)
{
	int isfree;

	rw_enter_read(&pt_softc_lock);
	isfree = pty_isfree_locked(minor);
	rw_exit_read(&pt_softc_lock);
	return(isfree);
}

dev_t
pty_getfree(void)
{
	int i;

	rw_enter_read(&pt_softc_lock);
	for (i = 0; i < npty; i++) {
		if (pty_isfree_locked(i))
			break;
	}
	rw_exit_read(&pt_softc_lock);
	return (makedev(pts_major, i));
}

/*
 * Hacked up version of vn_open. We _only_ handle ptys and only open
 * them with FREAD|FWRITE and never deal with creat or stuff like that.
 *
 * We need it because we have to fake up root credentials to open the pty.
 */
static int
ptm_vn_open(struct nameidata *ndp)
{
	struct proc *p = ndp->ni_cnd.cn_proc;
	struct ucred *cred;
	struct vattr vattr;
	struct vnode *vp;
	int error;

	if ((error = namei(ndp)) != 0)
		return (error);
	vp = ndp->ni_vp;
	if (vp->v_type != VCHR) {
		error = EINVAL;
		goto bad;
	}

	/*
	 * Get us a fresh cred with root privileges.
	 */
	cred = crget();
	error = VOP_OPEN(vp, FREAD|FWRITE, cred, p);
	if (!error) {
		/* update atime/mtime */
		vattr_null(&vattr);
		getnanotime(&vattr.va_atime);
		vattr.va_mtime = vattr.va_atime;
		vattr.va_vaflags |= VA_UTIMES_NULL;
		(void)VOP_SETATTR(vp, &vattr, p->p_ucred, p);
	}
	crfree(cred);

	if (error)
		goto bad;

	vp->v_writecount++;

	return (0);
bad:
	vput(vp);
	return (error);
}

void
ptmattach(int n)
{
	/* find the major and minor of the pty devices */
	int i;

	for (i = 0; i < nchrdev; i++)
		if (cdevsw[i].d_open == ptsopen)
			break;

	if (i == nchrdev)
		panic("ptmattach: Can't find pty slave in cdevsw");

	pts_major = i;
}

int
ptmopen(dev_t dev, int flag, int mode, struct proc *p)
{
	return(0);
}


int
ptmclose(dev_t dev, int flag, int mode, struct proc *p)
{
	return (0);
}

int
ptmioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	dev_t newdev;
	struct pt_softc * pti;
	struct nameidata cnd, snd;
	struct filedesc *fdp = p->p_fd;
	struct file *cfp = NULL, *sfp = NULL;
	int cindx, sindx, error;
	uid_t uid;
	gid_t gid;
	struct vattr vattr;
	struct ucred *cred;
	struct ptmget *ptm = (struct ptmget *)data;

	switch (cmd) {
	case PTMGET:
		fdplock(fdp);
		/* Grab two filedescriptors. */
		if ((error = falloc(p, &cfp, &cindx)) != 0) {
			fdpunlock(fdp);
			break;
		}
		if ((error = falloc(p, &sfp, &sindx)) != 0) {
			fdremove(fdp, cindx);
			fdpunlock(fdp);
			closef(cfp, p);
			break;
		}
		fdpunlock(fdp);

retry:
		/* Find and open a free master pty. */
		newdev = pty_getfree();
		if ((error = check_pty(newdev)))
			goto bad;
		pti = pt_softc[minor(newdev)];
		NDINIT(&cnd, LOOKUP, NOFOLLOW|LOCKLEAF|KERNELPATH, UIO_SYSSPACE,
		    pti->pty_pn, p);
		cnd.ni_pledge = PLEDGE_RPATH | PLEDGE_WPATH;
		if ((error = ptm_vn_open(&cnd)) != 0) {
			/*
			 * Check if the master open failed because we lost
			 * the race to grab it.
			 */
			if (error == EIO && !pty_isfree(minor(newdev)))
				goto retry;
			goto bad;
		}
		cfp->f_flag = FREAD|FWRITE;
		cfp->f_type = DTYPE_VNODE;
		cfp->f_ops = &vnops;
		cfp->f_data = (caddr_t) cnd.ni_vp;
		VOP_UNLOCK(cnd.ni_vp);

		/*
		 * Open the slave.
		 * namei -> setattr -> unlock -> revoke -> vrele ->
		 * namei -> open -> unlock
		 * Three stage rocket:
		 * 1. Change the owner and permissions on the slave.
		 * 2. Revoke all the users of the slave.
		 * 3. open the slave.
		 */
		NDINIT(&snd, LOOKUP, NOFOLLOW|LOCKLEAF|KERNELPATH, UIO_SYSSPACE,
		    pti->pty_sn, p);
		snd.ni_pledge = PLEDGE_RPATH | PLEDGE_WPATH;
		if ((error = namei(&snd)) != 0)
			goto bad;
		if ((snd.ni_vp->v_mount->mnt_flag & MNT_RDONLY) == 0) {
			gid = tty_gid;
			/* get real uid */
			uid = p->p_ucred->cr_ruid;

			vattr_null(&vattr);
			vattr.va_uid = uid;
			vattr.va_gid = gid;
			vattr.va_mode = (S_IRUSR|S_IWUSR|S_IWGRP) & ALLPERMS;
			/* Get a fake cred to pretend we're root. */
			cred = crget();
			error = VOP_SETATTR(snd.ni_vp, &vattr, cred, p);
			crfree(cred);
			if (error) {
				vput(snd.ni_vp);
				goto bad;
			}
		}
		VOP_UNLOCK(snd.ni_vp);
		if (snd.ni_vp->v_usecount > 1 ||
		    (snd.ni_vp->v_flag & (VALIASED)))
			VOP_REVOKE(snd.ni_vp, REVOKEALL);

		/*
		 * The vnode is useless after the revoke, we need to
		 * namei again.
		 */
		vrele(snd.ni_vp);

		NDINIT(&snd, LOOKUP, NOFOLLOW|LOCKLEAF|KERNELPATH, UIO_SYSSPACE,
		    pti->pty_sn, p);
		snd.ni_pledge = PLEDGE_RPATH | PLEDGE_WPATH;
		/* now open it */
		if ((error = ptm_vn_open(&snd)) != 0)
			goto bad;
		sfp->f_flag = FREAD|FWRITE;
		sfp->f_type = DTYPE_VNODE;
		sfp->f_ops = &vnops;
		sfp->f_data = (caddr_t) snd.ni_vp;
		VOP_UNLOCK(snd.ni_vp);

		/* now, put the indexen and names into struct ptmget */
		ptm->cfd = cindx;
		ptm->sfd = sindx;
		memcpy(ptm->cn, pti->pty_pn, sizeof(pti->pty_pn));
		memcpy(ptm->sn, pti->pty_sn, sizeof(pti->pty_sn));

		/* insert files now that we've passed all errors */
		fdplock(fdp);
		fdinsert(fdp, cindx, 0, cfp);
		fdinsert(fdp, sindx, 0, sfp);
		fdpunlock(fdp);
		FRELE(cfp, p);
		FRELE(sfp, p);
		break;
	default:
		error = EINVAL;
		break;
	}
	return (error);
bad:
	fdplock(fdp);
	fdremove(fdp, cindx);
	fdremove(fdp, sindx);
	fdpunlock(fdp);
	closef(cfp, p);
	closef(sfp, p);
	return (error);
}
