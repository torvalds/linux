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

#include "opt_capsicum.h"
#include "opt_printf.h"

#include <sys/param.h>
#include <sys/capsicum.h>
#include <sys/conf.h>
#include <sys/cons.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/filio.h>
#ifdef COMPAT_43TTY
#include <sys/ioctl_compat.h>
#endif /* COMPAT_43TTY */
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/poll.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/serial.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <sys/sx.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/tty.h>
#include <sys/ttycom.h>
#define TTYDEFCHARS
#include <sys/ttydefaults.h>
#undef TTYDEFCHARS
#include <sys/ucred.h>
#include <sys/vnode.h>

#include <machine/stdarg.h>

static MALLOC_DEFINE(M_TTY, "tty", "tty device");

static void tty_rel_free(struct tty *tp);

static TAILQ_HEAD(, tty) tty_list = TAILQ_HEAD_INITIALIZER(tty_list);
static struct sx tty_list_sx;
SX_SYSINIT(tty_list, &tty_list_sx, "tty list");
static unsigned int tty_list_count = 0;

/* Character device of /dev/console. */
static struct cdev	*dev_console;
static const char	*dev_console_filename;

/*
 * Flags that are supported and stored by this implementation.
 */
#define TTYSUP_IFLAG	(IGNBRK|BRKINT|IGNPAR|PARMRK|INPCK|ISTRIP|\
			INLCR|IGNCR|ICRNL|IXON|IXOFF|IXANY|IMAXBEL)
#define TTYSUP_OFLAG	(OPOST|ONLCR|TAB3|ONOEOT|OCRNL|ONOCR|ONLRET)
#define TTYSUP_LFLAG	(ECHOKE|ECHOE|ECHOK|ECHO|ECHONL|ECHOPRT|\
			ECHOCTL|ISIG|ICANON|ALTWERASE|IEXTEN|TOSTOP|\
			FLUSHO|NOKERNINFO|NOFLSH)
#define TTYSUP_CFLAG	(CIGNORE|CSIZE|CSTOPB|CREAD|PARENB|PARODD|\
			HUPCL|CLOCAL|CCTS_OFLOW|CRTS_IFLOW|CDTR_IFLOW|\
			CDSR_OFLOW|CCAR_OFLOW)

#define	TTY_CALLOUT(tp,d) (dev2unit(d) & TTYUNIT_CALLOUT)

static int  tty_drainwait = 5 * 60;
SYSCTL_INT(_kern, OID_AUTO, tty_drainwait, CTLFLAG_RWTUN,
    &tty_drainwait, 0, "Default output drain timeout in seconds");

/*
 * Set TTY buffer sizes.
 */

#define	TTYBUF_MAX	65536

#ifdef PRINTF_BUFR_SIZE
#define	TTY_PRBUF_SIZE	PRINTF_BUFR_SIZE
#else
#define	TTY_PRBUF_SIZE	256
#endif

/*
 * Allocate buffer space if necessary, and set low watermarks, based on speed.
 * Note that the ttyxxxq_setsize() functions may drop and then reacquire the tty
 * lock during memory allocation.  They will return ENXIO if the tty disappears
 * while unlocked.
 */
static int
tty_watermarks(struct tty *tp)
{
	size_t bs = 0;
	int error;

	/* Provide an input buffer for 2 seconds of data. */
	if (tp->t_termios.c_cflag & CREAD)
		bs = MIN(tp->t_termios.c_ispeed / 5, TTYBUF_MAX);
	error = ttyinq_setsize(&tp->t_inq, tp, bs);
	if (error != 0)
		return (error);

	/* Set low watermark at 10% (when 90% is available). */
	tp->t_inlow = (ttyinq_getallocatedsize(&tp->t_inq) * 9) / 10;

	/* Provide an output buffer for 2 seconds of data. */
	bs = MIN(tp->t_termios.c_ospeed / 5, TTYBUF_MAX);
	error = ttyoutq_setsize(&tp->t_outq, tp, bs);
	if (error != 0)
		return (error);

	/* Set low watermark at 10% (when 90% is available). */
	tp->t_outlow = (ttyoutq_getallocatedsize(&tp->t_outq) * 9) / 10;

	return (0);
}

static int
tty_drain(struct tty *tp, int leaving)
{
	sbintime_t timeout_at;
	size_t bytes;
	int error;

	if (ttyhook_hashook(tp, getc_inject))
		/* buffer is inaccessible */
		return (0);

	/*
	 * For close(), use the recent historic timeout of "1 second without
	 * making progress".  For tcdrain(), use t_drainwait as the timeout,
	 * with zero meaning "no timeout" which gives POSIX behavior.
	 */
	if (leaving)
		timeout_at = getsbinuptime() + SBT_1S;
	else if (tp->t_drainwait != 0)
		timeout_at = getsbinuptime() + SBT_1S * tp->t_drainwait;
	else
		timeout_at = 0;

	/*
	 * Poll the output buffer and the hardware for completion, at 10 Hz.
	 * Polling is required for devices which are not able to signal an
	 * interrupt when the transmitter becomes idle (most USB serial devs).
	 * The unusual structure of this loop ensures we check for busy one more
	 * time after tty_timedwait() returns EWOULDBLOCK, so that success has
	 * higher priority than timeout if the IO completed in the last 100mS.
	 */
	error = 0;
	bytes = ttyoutq_bytesused(&tp->t_outq);
	for (;;) {
		if (ttyoutq_bytesused(&tp->t_outq) == 0 && !ttydevsw_busy(tp))
			return (0);
		if (error != 0)
			return (error);
		ttydevsw_outwakeup(tp);
		error = tty_timedwait(tp, &tp->t_outwait, hz / 10);
		if (error != 0 && error != EWOULDBLOCK)
			return (error);
		else if (timeout_at == 0 || getsbinuptime() < timeout_at)
			error = 0;
		else if (leaving && ttyoutq_bytesused(&tp->t_outq) < bytes) {
			/* In close, making progress, grant an extra second. */
			error = 0;
			timeout_at += SBT_1S;
			bytes = ttyoutq_bytesused(&tp->t_outq);
		}
	}
}

/*
 * Though ttydev_enter() and ttydev_leave() seem to be related, they
 * don't have to be used together. ttydev_enter() is used by the cdev
 * operations to prevent an actual operation from being processed when
 * the TTY has been abandoned. ttydev_leave() is used by ttydev_open()
 * and ttydev_close() to determine whether per-TTY data should be
 * deallocated.
 */

static __inline int
ttydev_enter(struct tty *tp)
{

	tty_lock(tp);

	if (tty_gone(tp) || !tty_opened(tp)) {
		/* Device is already gone. */
		tty_unlock(tp);
		return (ENXIO);
	}

	return (0);
}

static void
ttydev_leave(struct tty *tp)
{

	tty_lock_assert(tp, MA_OWNED);

	if (tty_opened(tp) || tp->t_flags & TF_OPENCLOSE) {
		/* Device is still opened somewhere. */
		tty_unlock(tp);
		return;
	}

	tp->t_flags |= TF_OPENCLOSE;

	/* Stop asynchronous I/O. */
	funsetown(&tp->t_sigio);

	/* Remove console TTY. */
	if (constty == tp)
		constty_clear();

	/* Drain any output. */
	if (!tty_gone(tp))
		tty_drain(tp, 1);

	ttydisc_close(tp);

	/* Free i/o queues now since they might be large. */
	ttyinq_free(&tp->t_inq);
	tp->t_inlow = 0;
	ttyoutq_free(&tp->t_outq);
	tp->t_outlow = 0;

	knlist_clear(&tp->t_inpoll.si_note, 1);
	knlist_clear(&tp->t_outpoll.si_note, 1);

	if (!tty_gone(tp))
		ttydevsw_close(tp);

	tp->t_flags &= ~TF_OPENCLOSE;
	cv_broadcast(&tp->t_dcdwait);
	tty_rel_free(tp);
}

/*
 * Operations that are exposed through the character device in /dev.
 */
static int
ttydev_open(struct cdev *dev, int oflags, int devtype __unused,
    struct thread *td)
{
	struct tty *tp;
	int error;

	tp = dev->si_drv1;
	error = 0;
	tty_lock(tp);
	if (tty_gone(tp)) {
		/* Device is already gone. */
		tty_unlock(tp);
		return (ENXIO);
	}

	/*
	 * Block when other processes are currently opening or closing
	 * the TTY.
	 */
	while (tp->t_flags & TF_OPENCLOSE) {
		error = tty_wait(tp, &tp->t_dcdwait);
		if (error != 0) {
			tty_unlock(tp);
			return (error);
		}
	}
	tp->t_flags |= TF_OPENCLOSE;

	/*
	 * Make sure the "tty" and "cua" device cannot be opened at the
	 * same time.  The console is a "tty" device.
	 */
	if (TTY_CALLOUT(tp, dev)) {
		if (tp->t_flags & (TF_OPENED_CONS | TF_OPENED_IN)) {
			error = EBUSY;
			goto done;
		}
	} else {
		if (tp->t_flags & TF_OPENED_OUT) {
			error = EBUSY;
			goto done;
		}
	}

	if (tp->t_flags & TF_EXCLUDE && priv_check(td, PRIV_TTY_EXCLUSIVE)) {
		error = EBUSY;
		goto done;
	}

	if (!tty_opened(tp)) {
		/* Set proper termios flags. */
		if (TTY_CALLOUT(tp, dev))
			tp->t_termios = tp->t_termios_init_out;
		else
			tp->t_termios = tp->t_termios_init_in;
		ttydevsw_param(tp, &tp->t_termios);
		/* Prevent modem control on callout devices and /dev/console. */
		if (TTY_CALLOUT(tp, dev) || dev == dev_console)
			tp->t_termios.c_cflag |= CLOCAL;

		ttydevsw_modem(tp, SER_DTR|SER_RTS, 0);

		error = ttydevsw_open(tp);
		if (error != 0)
			goto done;

		ttydisc_open(tp);
		error = tty_watermarks(tp);
		if (error != 0)
			goto done;
	}

	/* Wait for Carrier Detect. */
	if ((oflags & O_NONBLOCK) == 0 &&
	    (tp->t_termios.c_cflag & CLOCAL) == 0) {
		while ((ttydevsw_modem(tp, 0, 0) & SER_DCD) == 0) {
			error = tty_wait(tp, &tp->t_dcdwait);
			if (error != 0)
				goto done;
		}
	}

	if (dev == dev_console)
		tp->t_flags |= TF_OPENED_CONS;
	else if (TTY_CALLOUT(tp, dev))
		tp->t_flags |= TF_OPENED_OUT;
	else
		tp->t_flags |= TF_OPENED_IN;
	MPASS((tp->t_flags & (TF_OPENED_CONS | TF_OPENED_IN)) == 0 ||
	    (tp->t_flags & TF_OPENED_OUT) == 0);

done:	tp->t_flags &= ~TF_OPENCLOSE;
	cv_broadcast(&tp->t_dcdwait);
	ttydev_leave(tp);

	return (error);
}

static int
ttydev_close(struct cdev *dev, int fflag, int devtype __unused,
    struct thread *td __unused)
{
	struct tty *tp = dev->si_drv1;

	tty_lock(tp);

	/*
	 * Don't actually close the device if it is being used as the
	 * console.
	 */
	MPASS((tp->t_flags & (TF_OPENED_CONS | TF_OPENED_IN)) == 0 ||
	    (tp->t_flags & TF_OPENED_OUT) == 0);
	if (dev == dev_console)
		tp->t_flags &= ~TF_OPENED_CONS;
	else
		tp->t_flags &= ~(TF_OPENED_IN|TF_OPENED_OUT);

	if (tp->t_flags & TF_OPENED) {
		tty_unlock(tp);
		return (0);
	}

	/* If revoking, flush output now to avoid draining it later. */
	if (fflag & FREVOKE)
		tty_flush(tp, FWRITE);

	tp->t_flags &= ~TF_EXCLUDE;

	/* Properly wake up threads that are stuck - revoke(). */
	tp->t_revokecnt++;
	tty_wakeup(tp, FREAD|FWRITE);
	cv_broadcast(&tp->t_bgwait);
	cv_broadcast(&tp->t_dcdwait);

	ttydev_leave(tp);

	return (0);
}

static __inline int
tty_is_ctty(struct tty *tp, struct proc *p)
{

	tty_lock_assert(tp, MA_OWNED);

	return (p->p_session == tp->t_session && p->p_flag & P_CONTROLT);
}

int
tty_wait_background(struct tty *tp, struct thread *td, int sig)
{
	struct proc *p = td->td_proc;
	struct pgrp *pg;
	ksiginfo_t ksi;
	int error;

	MPASS(sig == SIGTTIN || sig == SIGTTOU);
	tty_lock_assert(tp, MA_OWNED);

	for (;;) {
		PROC_LOCK(p);
		/*
		 * The process should only sleep, when:
		 * - This terminal is the controlling terminal
		 * - Its process group is not the foreground process
		 *   group
		 * - The parent process isn't waiting for the child to
		 *   exit
		 * - the signal to send to the process isn't masked
		 */
		if (!tty_is_ctty(tp, p) || p->p_pgrp == tp->t_pgrp) {
			/* Allow the action to happen. */
			PROC_UNLOCK(p);
			return (0);
		}

		if (SIGISMEMBER(p->p_sigacts->ps_sigignore, sig) ||
		    SIGISMEMBER(td->td_sigmask, sig)) {
			/* Only allow them in write()/ioctl(). */
			PROC_UNLOCK(p);
			return (sig == SIGTTOU ? 0 : EIO);
		}

		pg = p->p_pgrp;
		if (p->p_flag & P_PPWAIT || pg->pg_jobc == 0) {
			/* Don't allow the action to happen. */
			PROC_UNLOCK(p);
			return (EIO);
		}
		PROC_UNLOCK(p);

		/*
		 * Send the signal and sleep until we're the new
		 * foreground process group.
		 */
		if (sig != 0) {
			ksiginfo_init(&ksi);
			ksi.ksi_code = SI_KERNEL;
			ksi.ksi_signo = sig;
			sig = 0;
		}
		PGRP_LOCK(pg);
		pgsignal(pg, ksi.ksi_signo, 1, &ksi);
		PGRP_UNLOCK(pg);

		error = tty_wait(tp, &tp->t_bgwait);
		if (error)
			return (error);
	}
}

static int
ttydev_read(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct tty *tp = dev->si_drv1;
	int error;

	error = ttydev_enter(tp);
	if (error)
		goto done;
	error = ttydisc_read(tp, uio, ioflag);
	tty_unlock(tp);

	/*
	 * The read() call should not throw an error when the device is
	 * being destroyed. Silently convert it to an EOF.
	 */
done:	if (error == ENXIO)
		error = 0;
	return (error);
}

static int
ttydev_write(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct tty *tp = dev->si_drv1;
	int error;

	error = ttydev_enter(tp);
	if (error)
		return (error);

	if (tp->t_termios.c_lflag & TOSTOP) {
		error = tty_wait_background(tp, curthread, SIGTTOU);
		if (error)
			goto done;
	}

	if (ioflag & IO_NDELAY && tp->t_flags & TF_BUSY_OUT) {
		/* Allow non-blocking writes to bypass serialization. */
		error = ttydisc_write(tp, uio, ioflag);
	} else {
		/* Serialize write() calls. */
		while (tp->t_flags & TF_BUSY_OUT) {
			error = tty_wait(tp, &tp->t_outserwait);
			if (error)
				goto done;
		}

		tp->t_flags |= TF_BUSY_OUT;
		error = ttydisc_write(tp, uio, ioflag);
		tp->t_flags &= ~TF_BUSY_OUT;
		cv_signal(&tp->t_outserwait);
	}

done:	tty_unlock(tp);
	return (error);
}

static int
ttydev_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
	struct tty *tp = dev->si_drv1;
	int error;

	error = ttydev_enter(tp);
	if (error)
		return (error);

	switch (cmd) {
	case TIOCCBRK:
	case TIOCCONS:
	case TIOCDRAIN:
	case TIOCEXCL:
	case TIOCFLUSH:
	case TIOCNXCL:
	case TIOCSBRK:
	case TIOCSCTTY:
	case TIOCSETA:
	case TIOCSETAF:
	case TIOCSETAW:
	case TIOCSPGRP:
	case TIOCSTART:
	case TIOCSTAT:
	case TIOCSTI:
	case TIOCSTOP:
	case TIOCSWINSZ:
#if 0
	case TIOCSDRAINWAIT:
	case TIOCSETD:
#endif
#ifdef COMPAT_43TTY
	case  TIOCLBIC:
	case  TIOCLBIS:
	case  TIOCLSET:
	case  TIOCSETC:
	case OTIOCSETD:
	case  TIOCSETN:
	case  TIOCSETP:
	case  TIOCSLTC:
#endif /* COMPAT_43TTY */
		/*
		 * If the ioctl() causes the TTY to be modified, let it
		 * wait in the background.
		 */
		error = tty_wait_background(tp, curthread, SIGTTOU);
		if (error)
			goto done;
	}

	if (cmd == TIOCSETA || cmd == TIOCSETAW || cmd == TIOCSETAF) {
		struct termios *old = &tp->t_termios;
		struct termios *new = (struct termios *)data;
		struct termios *lock = TTY_CALLOUT(tp, dev) ?
		    &tp->t_termios_lock_out : &tp->t_termios_lock_in;
		int cc;

		/*
		 * Lock state devices.  Just overwrite the values of the
		 * commands that are currently in use.
		 */
		new->c_iflag = (old->c_iflag & lock->c_iflag) |
		    (new->c_iflag & ~lock->c_iflag);
		new->c_oflag = (old->c_oflag & lock->c_oflag) |
		    (new->c_oflag & ~lock->c_oflag);
		new->c_cflag = (old->c_cflag & lock->c_cflag) |
		    (new->c_cflag & ~lock->c_cflag);
		new->c_lflag = (old->c_lflag & lock->c_lflag) |
		    (new->c_lflag & ~lock->c_lflag);
		for (cc = 0; cc < NCCS; ++cc)
			if (lock->c_cc[cc])
				new->c_cc[cc] = old->c_cc[cc];
		if (lock->c_ispeed)
			new->c_ispeed = old->c_ispeed;
		if (lock->c_ospeed)
			new->c_ospeed = old->c_ospeed;
	}

	error = tty_ioctl(tp, cmd, data, fflag, td);
done:	tty_unlock(tp);

	return (error);
}

static int
ttydev_poll(struct cdev *dev, int events, struct thread *td)
{
	struct tty *tp = dev->si_drv1;
	int error, revents = 0;

	error = ttydev_enter(tp);
	if (error)
		return ((events & (POLLIN|POLLRDNORM)) | POLLHUP);

	if (events & (POLLIN|POLLRDNORM)) {
		/* See if we can read something. */
		if (ttydisc_read_poll(tp) > 0)
			revents |= events & (POLLIN|POLLRDNORM);
	}

	if (tp->t_flags & TF_ZOMBIE) {
		/* Hangup flag on zombie state. */
		revents |= POLLHUP;
	} else if (events & (POLLOUT|POLLWRNORM)) {
		/* See if we can write something. */
		if (ttydisc_write_poll(tp) > 0)
			revents |= events & (POLLOUT|POLLWRNORM);
	}

	if (revents == 0) {
		if (events & (POLLIN|POLLRDNORM))
			selrecord(td, &tp->t_inpoll);
		if (events & (POLLOUT|POLLWRNORM))
			selrecord(td, &tp->t_outpoll);
	}

	tty_unlock(tp);

	return (revents);
}

static int
ttydev_mmap(struct cdev *dev, vm_ooffset_t offset, vm_paddr_t *paddr,
    int nprot, vm_memattr_t *memattr)
{
	struct tty *tp = dev->si_drv1;
	int error;

	/* Handle mmap() through the driver. */

	error = ttydev_enter(tp);
	if (error)
		return (-1);
	error = ttydevsw_mmap(tp, offset, paddr, nprot, memattr);
	tty_unlock(tp);

	return (error);
}

/*
 * kqueue support.
 */

static void
tty_kqops_read_detach(struct knote *kn)
{
	struct tty *tp = kn->kn_hook;

	knlist_remove(&tp->t_inpoll.si_note, kn, 0);
}

static int
tty_kqops_read_event(struct knote *kn, long hint __unused)
{
	struct tty *tp = kn->kn_hook;

	tty_lock_assert(tp, MA_OWNED);

	if (tty_gone(tp) || tp->t_flags & TF_ZOMBIE) {
		kn->kn_flags |= EV_EOF;
		return (1);
	} else {
		kn->kn_data = ttydisc_read_poll(tp);
		return (kn->kn_data > 0);
	}
}

static void
tty_kqops_write_detach(struct knote *kn)
{
	struct tty *tp = kn->kn_hook;

	knlist_remove(&tp->t_outpoll.si_note, kn, 0);
}

static int
tty_kqops_write_event(struct knote *kn, long hint __unused)
{
	struct tty *tp = kn->kn_hook;

	tty_lock_assert(tp, MA_OWNED);

	if (tty_gone(tp)) {
		kn->kn_flags |= EV_EOF;
		return (1);
	} else {
		kn->kn_data = ttydisc_write_poll(tp);
		return (kn->kn_data > 0);
	}
}

static struct filterops tty_kqops_read = {
	.f_isfd = 1,
	.f_detach = tty_kqops_read_detach,
	.f_event = tty_kqops_read_event,
};

static struct filterops tty_kqops_write = {
	.f_isfd = 1,
	.f_detach = tty_kqops_write_detach,
	.f_event = tty_kqops_write_event,
};

static int
ttydev_kqfilter(struct cdev *dev, struct knote *kn)
{
	struct tty *tp = dev->si_drv1;
	int error;

	error = ttydev_enter(tp);
	if (error)
		return (error);

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_hook = tp;
		kn->kn_fop = &tty_kqops_read;
		knlist_add(&tp->t_inpoll.si_note, kn, 1);
		break;
	case EVFILT_WRITE:
		kn->kn_hook = tp;
		kn->kn_fop = &tty_kqops_write;
		knlist_add(&tp->t_outpoll.si_note, kn, 1);
		break;
	default:
		error = EINVAL;
		break;
	}

	tty_unlock(tp);
	return (error);
}

static struct cdevsw ttydev_cdevsw = {
	.d_version	= D_VERSION,
	.d_open		= ttydev_open,
	.d_close	= ttydev_close,
	.d_read		= ttydev_read,
	.d_write	= ttydev_write,
	.d_ioctl	= ttydev_ioctl,
	.d_kqfilter	= ttydev_kqfilter,
	.d_poll		= ttydev_poll,
	.d_mmap		= ttydev_mmap,
	.d_name		= "ttydev",
	.d_flags	= D_TTY,
};

/*
 * Init/lock-state devices
 */

static int
ttyil_open(struct cdev *dev, int oflags __unused, int devtype __unused,
    struct thread *td)
{
	struct tty *tp;
	int error;

	tp = dev->si_drv1;
	error = 0;
	tty_lock(tp);
	if (tty_gone(tp))
		error = ENODEV;
	tty_unlock(tp);

	return (error);
}

static int
ttyil_close(struct cdev *dev __unused, int flag __unused, int mode __unused,
    struct thread *td __unused)
{

	return (0);
}

static int
ttyil_rdwr(struct cdev *dev __unused, struct uio *uio __unused,
    int ioflag __unused)
{

	return (ENODEV);
}

static int
ttyil_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
	struct tty *tp = dev->si_drv1;
	int error;

	tty_lock(tp);
	if (tty_gone(tp)) {
		error = ENODEV;
		goto done;
	}

	error = ttydevsw_cioctl(tp, dev2unit(dev), cmd, data, td);
	if (error != ENOIOCTL)
		goto done;
	error = 0;

	switch (cmd) {
	case TIOCGETA:
		/* Obtain terminal flags through tcgetattr(). */
		*(struct termios*)data = *(struct termios*)dev->si_drv2;
		break;
	case TIOCSETA:
		/* Set terminal flags through tcsetattr(). */
		error = priv_check(td, PRIV_TTY_SETA);
		if (error)
			break;
		*(struct termios*)dev->si_drv2 = *(struct termios*)data;
		break;
	case TIOCGETD:
		*(int *)data = TTYDISC;
		break;
	case TIOCGWINSZ:
		bzero(data, sizeof(struct winsize));
		break;
	default:
		error = ENOTTY;
	}

done:	tty_unlock(tp);
	return (error);
}

static struct cdevsw ttyil_cdevsw = {
	.d_version	= D_VERSION,
	.d_open		= ttyil_open,
	.d_close	= ttyil_close,
	.d_read		= ttyil_rdwr,
	.d_write	= ttyil_rdwr,
	.d_ioctl	= ttyil_ioctl,
	.d_name		= "ttyil",
	.d_flags	= D_TTY,
};

static void
tty_init_termios(struct tty *tp)
{
	struct termios *t = &tp->t_termios_init_in;

	t->c_cflag = TTYDEF_CFLAG;
	t->c_iflag = TTYDEF_IFLAG;
	t->c_lflag = TTYDEF_LFLAG;
	t->c_oflag = TTYDEF_OFLAG;
	t->c_ispeed = TTYDEF_SPEED;
	t->c_ospeed = TTYDEF_SPEED;
	memcpy(&t->c_cc, ttydefchars, sizeof ttydefchars);

	tp->t_termios_init_out = *t;
}

void
tty_init_console(struct tty *tp, speed_t s)
{
	struct termios *ti = &tp->t_termios_init_in;
	struct termios *to = &tp->t_termios_init_out;

	if (s != 0) {
		ti->c_ispeed = ti->c_ospeed = s;
		to->c_ispeed = to->c_ospeed = s;
	}

	ti->c_cflag |= CLOCAL;
	to->c_cflag |= CLOCAL;
}

/*
 * Standard device routine implementations, mostly meant for
 * pseudo-terminal device drivers. When a driver creates a new terminal
 * device class, missing routines are patched.
 */

static int
ttydevsw_defopen(struct tty *tp __unused)
{

	return (0);
}

static void
ttydevsw_defclose(struct tty *tp __unused)
{

}

static void
ttydevsw_defoutwakeup(struct tty *tp __unused)
{

	panic("Terminal device has output, while not implemented");
}

static void
ttydevsw_definwakeup(struct tty *tp __unused)
{

}

static int
ttydevsw_defioctl(struct tty *tp __unused, u_long cmd __unused,
    caddr_t data __unused, struct thread *td __unused)
{

	return (ENOIOCTL);
}

static int
ttydevsw_defcioctl(struct tty *tp __unused, int unit __unused,
    u_long cmd __unused, caddr_t data __unused, struct thread *td __unused)
{

	return (ENOIOCTL);
}

static int
ttydevsw_defparam(struct tty *tp __unused, struct termios *t)
{

	/*
	 * Allow the baud rate to be adjusted for pseudo-devices, but at
	 * least restrict it to 115200 to prevent excessive buffer
	 * usage.  Also disallow 0, to prevent foot shooting.
	 */
	if (t->c_ispeed < B50)
		t->c_ispeed = B50;
	else if (t->c_ispeed > B115200)
		t->c_ispeed = B115200;
	if (t->c_ospeed < B50)
		t->c_ospeed = B50;
	else if (t->c_ospeed > B115200)
		t->c_ospeed = B115200;
	t->c_cflag |= CREAD;

	return (0);
}

static int
ttydevsw_defmodem(struct tty *tp __unused, int sigon __unused,
    int sigoff __unused)
{

	/* Simulate a carrier to make the TTY layer happy. */
	return (SER_DCD);
}

static int
ttydevsw_defmmap(struct tty *tp __unused, vm_ooffset_t offset __unused,
    vm_paddr_t *paddr __unused, int nprot __unused,
    vm_memattr_t *memattr __unused)
{

	return (-1);
}

static void
ttydevsw_defpktnotify(struct tty *tp __unused, char event __unused)
{

}

static void
ttydevsw_deffree(void *softc __unused)
{

	panic("Terminal device freed without a free-handler");
}

static bool
ttydevsw_defbusy(struct tty *tp __unused)
{

	return (FALSE);
}

/*
 * TTY allocation and deallocation. TTY devices can be deallocated when
 * the driver doesn't use it anymore, when the TTY isn't a session's
 * controlling TTY and when the device node isn't opened through devfs.
 */

struct tty *
tty_alloc(struct ttydevsw *tsw, void *sc)
{

	return (tty_alloc_mutex(tsw, sc, NULL));
}

struct tty *
tty_alloc_mutex(struct ttydevsw *tsw, void *sc, struct mtx *mutex)
{
	struct tty *tp;

	/* Make sure the driver defines all routines. */
#define PATCH_FUNC(x) do {				\
	if (tsw->tsw_ ## x == NULL)			\
		tsw->tsw_ ## x = ttydevsw_def ## x;	\
} while (0)
	PATCH_FUNC(open);
	PATCH_FUNC(close);
	PATCH_FUNC(outwakeup);
	PATCH_FUNC(inwakeup);
	PATCH_FUNC(ioctl);
	PATCH_FUNC(cioctl);
	PATCH_FUNC(param);
	PATCH_FUNC(modem);
	PATCH_FUNC(mmap);
	PATCH_FUNC(pktnotify);
	PATCH_FUNC(free);
	PATCH_FUNC(busy);
#undef PATCH_FUNC

	tp = malloc(sizeof(struct tty) + TTY_PRBUF_SIZE, M_TTY,
	    M_WAITOK | M_ZERO);
	tp->t_prbufsz = TTY_PRBUF_SIZE;
	tp->t_devsw = tsw;
	tp->t_devswsoftc = sc;
	tp->t_flags = tsw->tsw_flags;
	tp->t_drainwait = tty_drainwait;

	tty_init_termios(tp);

	cv_init(&tp->t_inwait, "ttyin");
	cv_init(&tp->t_outwait, "ttyout");
	cv_init(&tp->t_outserwait, "ttyosr");
	cv_init(&tp->t_bgwait, "ttybg");
	cv_init(&tp->t_dcdwait, "ttydcd");

	/* Allow drivers to use a custom mutex to lock the TTY. */
	if (mutex != NULL) {
		tp->t_mtx = mutex;
	} else {
		tp->t_mtx = &tp->t_mtxobj;
		mtx_init(&tp->t_mtxobj, "ttymtx", NULL, MTX_DEF);
	}

	knlist_init_mtx(&tp->t_inpoll.si_note, tp->t_mtx);
	knlist_init_mtx(&tp->t_outpoll.si_note, tp->t_mtx);

	return (tp);
}

static void
tty_dealloc(void *arg)
{
	struct tty *tp = arg;

	/*
	 * ttyydev_leave() usually frees the i/o queues earlier, but it is
	 * not always called between queue allocation and here.  The queues
	 * may be allocated by ioctls on a pty control device without the
	 * corresponding pty slave device ever being open, or after it is
	 * closed.
	 */
	ttyinq_free(&tp->t_inq);
	ttyoutq_free(&tp->t_outq);
	seldrain(&tp->t_inpoll);
	seldrain(&tp->t_outpoll);
	knlist_destroy(&tp->t_inpoll.si_note);
	knlist_destroy(&tp->t_outpoll.si_note);

	cv_destroy(&tp->t_inwait);
	cv_destroy(&tp->t_outwait);
	cv_destroy(&tp->t_bgwait);
	cv_destroy(&tp->t_dcdwait);
	cv_destroy(&tp->t_outserwait);

	if (tp->t_mtx == &tp->t_mtxobj)
		mtx_destroy(&tp->t_mtxobj);
	ttydevsw_free(tp);
	free(tp, M_TTY);
}

static void
tty_rel_free(struct tty *tp)
{
	struct cdev *dev;

	tty_lock_assert(tp, MA_OWNED);

#define	TF_ACTIVITY	(TF_GONE|TF_OPENED|TF_HOOK|TF_OPENCLOSE)
	if (tp->t_sessioncnt != 0 || (tp->t_flags & TF_ACTIVITY) != TF_GONE) {
		/* TTY is still in use. */
		tty_unlock(tp);
		return;
	}

	/* TTY can be deallocated. */
	dev = tp->t_dev;
	tp->t_dev = NULL;
	tty_unlock(tp);

	if (dev != NULL) {
		sx_xlock(&tty_list_sx);
		TAILQ_REMOVE(&tty_list, tp, t_list);
		tty_list_count--;
		sx_xunlock(&tty_list_sx);
		destroy_dev_sched_cb(dev, tty_dealloc, tp);
	}
}

void
tty_rel_pgrp(struct tty *tp, struct pgrp *pg)
{

	MPASS(tp->t_sessioncnt > 0);
	tty_lock_assert(tp, MA_OWNED);

	if (tp->t_pgrp == pg)
		tp->t_pgrp = NULL;

	tty_unlock(tp);
}

void
tty_rel_sess(struct tty *tp, struct session *sess)
{

	MPASS(tp->t_sessioncnt > 0);

	/* Current session has left. */
	if (tp->t_session == sess) {
		tp->t_session = NULL;
		MPASS(tp->t_pgrp == NULL);
	}
	tp->t_sessioncnt--;
	tty_rel_free(tp);
}

void
tty_rel_gone(struct tty *tp)
{

	MPASS(!tty_gone(tp));

	/* Simulate carrier removal. */
	ttydisc_modem(tp, 0);

	/* Wake up all blocked threads. */
	tty_wakeup(tp, FREAD|FWRITE);
	cv_broadcast(&tp->t_bgwait);
	cv_broadcast(&tp->t_dcdwait);

	tp->t_flags |= TF_GONE;
	tty_rel_free(tp);
}

/*
 * Exposing information about current TTY's through sysctl
 */

static void
tty_to_xtty(struct tty *tp, struct xtty *xt)
{

	tty_lock_assert(tp, MA_OWNED);

	xt->xt_size = sizeof(struct xtty);
	xt->xt_insize = ttyinq_getsize(&tp->t_inq);
	xt->xt_incc = ttyinq_bytescanonicalized(&tp->t_inq);
	xt->xt_inlc = ttyinq_bytesline(&tp->t_inq);
	xt->xt_inlow = tp->t_inlow;
	xt->xt_outsize = ttyoutq_getsize(&tp->t_outq);
	xt->xt_outcc = ttyoutq_bytesused(&tp->t_outq);
	xt->xt_outlow = tp->t_outlow;
	xt->xt_column = tp->t_column;
	xt->xt_pgid = tp->t_pgrp ? tp->t_pgrp->pg_id : 0;
	xt->xt_sid = tp->t_session ? tp->t_session->s_sid : 0;
	xt->xt_flags = tp->t_flags;
	xt->xt_dev = tp->t_dev ? dev2udev(tp->t_dev) : (uint32_t)NODEV;
}

static int
sysctl_kern_ttys(SYSCTL_HANDLER_ARGS)
{
	unsigned long lsize;
	struct xtty *xtlist, *xt;
	struct tty *tp;
	int error;

	sx_slock(&tty_list_sx);
	lsize = tty_list_count * sizeof(struct xtty);
	if (lsize == 0) {
		sx_sunlock(&tty_list_sx);
		return (0);
	}

	xtlist = xt = malloc(lsize, M_TTY, M_WAITOK);

	TAILQ_FOREACH(tp, &tty_list, t_list) {
		tty_lock(tp);
		tty_to_xtty(tp, xt);
		tty_unlock(tp);
		xt++;
	}
	sx_sunlock(&tty_list_sx);

	error = SYSCTL_OUT(req, xtlist, lsize);
	free(xtlist, M_TTY);
	return (error);
}

SYSCTL_PROC(_kern, OID_AUTO, ttys, CTLTYPE_OPAQUE|CTLFLAG_RD|CTLFLAG_MPSAFE,
	0, 0, sysctl_kern_ttys, "S,xtty", "List of TTYs");

/*
 * Device node creation. Device has been set up, now we can expose it to
 * the user.
 */

int
tty_makedevf(struct tty *tp, struct ucred *cred, int flags,
    const char *fmt, ...)
{
	va_list ap;
	struct make_dev_args args;
	struct cdev *dev, *init, *lock, *cua, *cinit, *clock;
	const char *prefix = "tty";
	char name[SPECNAMELEN - 3]; /* for "tty" and "cua". */
	uid_t uid;
	gid_t gid;
	mode_t mode;
	int error;

	/* Remove "tty" prefix from devices like PTY's. */
	if (tp->t_flags & TF_NOPREFIX)
		prefix = "";

	va_start(ap, fmt);
	vsnrprintf(name, sizeof name, 32, fmt, ap);
	va_end(ap);

	if (cred == NULL) {
		/* System device. */
		uid = UID_ROOT;
		gid = GID_WHEEL;
		mode = S_IRUSR|S_IWUSR;
	} else {
		/* User device. */
		uid = cred->cr_ruid;
		gid = GID_TTY;
		mode = S_IRUSR|S_IWUSR|S_IWGRP;
	}

	flags = flags & TTYMK_CLONING ? MAKEDEV_REF : 0;
	flags |= MAKEDEV_CHECKNAME;

	/* Master call-in device. */
	make_dev_args_init(&args);
	args.mda_flags = flags;
	args.mda_devsw = &ttydev_cdevsw;
	args.mda_cr = cred;
	args.mda_uid = uid;
	args.mda_gid = gid;
	args.mda_mode = mode;
	args.mda_si_drv1 = tp;
	error = make_dev_s(&args, &dev, "%s%s", prefix, name);
	if (error != 0)
		return (error);
	tp->t_dev = dev;

	init = lock = cua = cinit = clock = NULL;

	/* Slave call-in devices. */
	if (tp->t_flags & TF_INITLOCK) {
		args.mda_devsw = &ttyil_cdevsw;
		args.mda_unit = TTYUNIT_INIT;
		args.mda_si_drv1 = tp;
		args.mda_si_drv2 = &tp->t_termios_init_in;
		error = make_dev_s(&args, &init, "%s%s.init", prefix, name);
		if (error != 0)
			goto fail;
		dev_depends(dev, init);

		args.mda_unit = TTYUNIT_LOCK;
		args.mda_si_drv2 = &tp->t_termios_lock_in;
		error = make_dev_s(&args, &lock, "%s%s.lock", prefix, name);
		if (error != 0)
			goto fail;
		dev_depends(dev, lock);
	}

	/* Call-out devices. */
	if (tp->t_flags & TF_CALLOUT) {
		make_dev_args_init(&args);
		args.mda_flags = flags;
		args.mda_devsw = &ttydev_cdevsw;
		args.mda_cr = cred;
		args.mda_uid = UID_UUCP;
		args.mda_gid = GID_DIALER;
		args.mda_mode = 0660;
		args.mda_unit = TTYUNIT_CALLOUT;
		args.mda_si_drv1 = tp;
		error = make_dev_s(&args, &cua, "cua%s", name);
		if (error != 0)
			goto fail;
		dev_depends(dev, cua);

		/* Slave call-out devices. */
		if (tp->t_flags & TF_INITLOCK) {
			args.mda_devsw = &ttyil_cdevsw;
			args.mda_unit = TTYUNIT_CALLOUT | TTYUNIT_INIT;
			args.mda_si_drv2 = &tp->t_termios_init_out;
			error = make_dev_s(&args, &cinit, "cua%s.init", name);
			if (error != 0)
				goto fail;
			dev_depends(dev, cinit);

			args.mda_unit = TTYUNIT_CALLOUT | TTYUNIT_LOCK;
			args.mda_si_drv2 = &tp->t_termios_lock_out;
			error = make_dev_s(&args, &clock, "cua%s.lock", name);
			if (error != 0)
				goto fail;
			dev_depends(dev, clock);
		}
	}

	sx_xlock(&tty_list_sx);
	TAILQ_INSERT_TAIL(&tty_list, tp, t_list);
	tty_list_count++;
	sx_xunlock(&tty_list_sx);

	return (0);

fail:
	destroy_dev(dev);
	if (init)
		destroy_dev(init);
	if (lock)
		destroy_dev(lock);
	if (cinit)
		destroy_dev(cinit);
	if (clock)
		destroy_dev(clock);

	return (error);
}

/*
 * Signalling processes.
 */

void
tty_signal_sessleader(struct tty *tp, int sig)
{
	struct proc *p;

	tty_lock_assert(tp, MA_OWNED);
	MPASS(sig >= 1 && sig < NSIG);

	/* Make signals start output again. */
	tp->t_flags &= ~TF_STOPPED;

	if (tp->t_session != NULL && tp->t_session->s_leader != NULL) {
		p = tp->t_session->s_leader;
		PROC_LOCK(p);
		kern_psignal(p, sig);
		PROC_UNLOCK(p);
	}
}

void
tty_signal_pgrp(struct tty *tp, int sig)
{
	ksiginfo_t ksi;

	tty_lock_assert(tp, MA_OWNED);
	MPASS(sig >= 1 && sig < NSIG);

	/* Make signals start output again. */
	tp->t_flags &= ~TF_STOPPED;

	if (sig == SIGINFO && !(tp->t_termios.c_lflag & NOKERNINFO))
		tty_info(tp);
	if (tp->t_pgrp != NULL) {
		ksiginfo_init(&ksi);
		ksi.ksi_signo = sig;
		ksi.ksi_code = SI_KERNEL;
		PGRP_LOCK(tp->t_pgrp);
		pgsignal(tp->t_pgrp, sig, 1, &ksi);
		PGRP_UNLOCK(tp->t_pgrp);
	}
}

void
tty_wakeup(struct tty *tp, int flags)
{

	if (tp->t_flags & TF_ASYNC && tp->t_sigio != NULL)
		pgsigio(&tp->t_sigio, SIGIO, (tp->t_session != NULL));

	if (flags & FWRITE) {
		cv_broadcast(&tp->t_outwait);
		selwakeup(&tp->t_outpoll);
		KNOTE_LOCKED(&tp->t_outpoll.si_note, 0);
	}
	if (flags & FREAD) {
		cv_broadcast(&tp->t_inwait);
		selwakeup(&tp->t_inpoll);
		KNOTE_LOCKED(&tp->t_inpoll.si_note, 0);
	}
}

int
tty_wait(struct tty *tp, struct cv *cv)
{
	int error;
	int revokecnt = tp->t_revokecnt;

	tty_lock_assert(tp, MA_OWNED|MA_NOTRECURSED);
	MPASS(!tty_gone(tp));

	error = cv_wait_sig(cv, tp->t_mtx);

	/* Bail out when the device slipped away. */
	if (tty_gone(tp))
		return (ENXIO);

	/* Restart the system call when we may have been revoked. */
	if (tp->t_revokecnt != revokecnt)
		return (ERESTART);

	return (error);
}

int
tty_timedwait(struct tty *tp, struct cv *cv, int hz)
{
	int error;
	int revokecnt = tp->t_revokecnt;

	tty_lock_assert(tp, MA_OWNED|MA_NOTRECURSED);
	MPASS(!tty_gone(tp));

	error = cv_timedwait_sig(cv, tp->t_mtx, hz);

	/* Bail out when the device slipped away. */
	if (tty_gone(tp))
		return (ENXIO);

	/* Restart the system call when we may have been revoked. */
	if (tp->t_revokecnt != revokecnt)
		return (ERESTART);

	return (error);
}

void
tty_flush(struct tty *tp, int flags)
{

	if (flags & FWRITE) {
		tp->t_flags &= ~TF_HIWAT_OUT;
		ttyoutq_flush(&tp->t_outq);
		tty_wakeup(tp, FWRITE);
		if (!tty_gone(tp)) {
			ttydevsw_outwakeup(tp);
			ttydevsw_pktnotify(tp, TIOCPKT_FLUSHWRITE);
		}
	}
	if (flags & FREAD) {
		tty_hiwat_in_unblock(tp);
		ttyinq_flush(&tp->t_inq);
		tty_wakeup(tp, FREAD);
		if (!tty_gone(tp)) {
			ttydevsw_inwakeup(tp);
			ttydevsw_pktnotify(tp, TIOCPKT_FLUSHREAD);
		}
	}
}

void
tty_set_winsize(struct tty *tp, const struct winsize *wsz)
{

	if (memcmp(&tp->t_winsize, wsz, sizeof(*wsz)) == 0)
		return;
	tp->t_winsize = *wsz;
	tty_signal_pgrp(tp, SIGWINCH);
}

static int
tty_generic_ioctl(struct tty *tp, u_long cmd, void *data, int fflag,
    struct thread *td)
{
	int error;

	switch (cmd) {
	/*
	 * Modem commands.
	 * The SER_* and TIOCM_* flags are the same, but one bit
	 * shifted. I don't know why.
	 */
	case TIOCSDTR:
		ttydevsw_modem(tp, SER_DTR, 0);
		return (0);
	case TIOCCDTR:
		ttydevsw_modem(tp, 0, SER_DTR);
		return (0);
	case TIOCMSET: {
		int bits = *(int *)data;
		ttydevsw_modem(tp,
		    (bits & (TIOCM_DTR | TIOCM_RTS)) >> 1,
		    ((~bits) & (TIOCM_DTR | TIOCM_RTS)) >> 1);
		return (0);
	}
	case TIOCMBIS: {
		int bits = *(int *)data;
		ttydevsw_modem(tp, (bits & (TIOCM_DTR | TIOCM_RTS)) >> 1, 0);
		return (0);
	}
	case TIOCMBIC: {
		int bits = *(int *)data;
		ttydevsw_modem(tp, 0, (bits & (TIOCM_DTR | TIOCM_RTS)) >> 1);
		return (0);
	}
	case TIOCMGET:
		*(int *)data = TIOCM_LE + (ttydevsw_modem(tp, 0, 0) << 1);
		return (0);

	case FIOASYNC:
		if (*(int *)data)
			tp->t_flags |= TF_ASYNC;
		else
			tp->t_flags &= ~TF_ASYNC;
		return (0);
	case FIONBIO:
		/* This device supports non-blocking operation. */
		return (0);
	case FIONREAD:
		*(int *)data = ttyinq_bytescanonicalized(&tp->t_inq);
		return (0);
	case FIONWRITE:
	case TIOCOUTQ:
		*(int *)data = ttyoutq_bytesused(&tp->t_outq);
		return (0);
	case FIOSETOWN:
		if (tp->t_session != NULL && !tty_is_ctty(tp, td->td_proc))
			/* Not allowed to set ownership. */
			return (ENOTTY);

		/* Temporarily unlock the TTY to set ownership. */
		tty_unlock(tp);
		error = fsetown(*(int *)data, &tp->t_sigio);
		tty_lock(tp);
		return (error);
	case FIOGETOWN:
		if (tp->t_session != NULL && !tty_is_ctty(tp, td->td_proc))
			/* Not allowed to set ownership. */
			return (ENOTTY);

		/* Get ownership. */
		*(int *)data = fgetown(&tp->t_sigio);
		return (0);
	case TIOCGETA:
		/* Obtain terminal flags through tcgetattr(). */
		*(struct termios*)data = tp->t_termios;
		return (0);
	case TIOCSETA:
	case TIOCSETAW:
	case TIOCSETAF: {
		struct termios *t = data;

		/*
		 * Who makes up these funny rules? According to POSIX,
		 * input baud rate is set equal to the output baud rate
		 * when zero.
		 */
		if (t->c_ispeed == 0)
			t->c_ispeed = t->c_ospeed;

		/* Discard any unsupported bits. */
		t->c_iflag &= TTYSUP_IFLAG;
		t->c_oflag &= TTYSUP_OFLAG;
		t->c_lflag &= TTYSUP_LFLAG;
		t->c_cflag &= TTYSUP_CFLAG;

		/* Set terminal flags through tcsetattr(). */
		if (cmd == TIOCSETAW || cmd == TIOCSETAF) {
			error = tty_drain(tp, 0);
			if (error)
				return (error);
			if (cmd == TIOCSETAF)
				tty_flush(tp, FREAD);
		}

		/*
		 * Only call param() when the flags really change.
		 */
		if ((t->c_cflag & CIGNORE) == 0 &&
		    (tp->t_termios.c_cflag != t->c_cflag ||
		    ((tp->t_termios.c_iflag ^ t->c_iflag) &
		    (IXON|IXOFF|IXANY)) ||
		    tp->t_termios.c_ispeed != t->c_ispeed ||
		    tp->t_termios.c_ospeed != t->c_ospeed)) {
			error = ttydevsw_param(tp, t);
			if (error)
				return (error);

			/* XXX: CLOCAL? */

			tp->t_termios.c_cflag = t->c_cflag & ~CIGNORE;
			tp->t_termios.c_ispeed = t->c_ispeed;
			tp->t_termios.c_ospeed = t->c_ospeed;

			/* Baud rate has changed - update watermarks. */
			error = tty_watermarks(tp);
			if (error)
				return (error);
		}

		/* Copy new non-device driver parameters. */
		tp->t_termios.c_iflag = t->c_iflag;
		tp->t_termios.c_oflag = t->c_oflag;
		tp->t_termios.c_lflag = t->c_lflag;
		memcpy(&tp->t_termios.c_cc, t->c_cc, sizeof t->c_cc);

		ttydisc_optimize(tp);

		if ((t->c_lflag & ICANON) == 0) {
			/*
			 * When in non-canonical mode, wake up all
			 * readers. Canonicalize any partial input. VMIN
			 * and VTIME could also be adjusted.
			 */
			ttyinq_canonicalize(&tp->t_inq);
			tty_wakeup(tp, FREAD);
		}

		/*
		 * For packet mode: notify the PTY consumer that VSTOP
		 * and VSTART may have been changed.
		 */
		if (tp->t_termios.c_iflag & IXON &&
		    tp->t_termios.c_cc[VSTOP] == CTRL('S') &&
		    tp->t_termios.c_cc[VSTART] == CTRL('Q'))
			ttydevsw_pktnotify(tp, TIOCPKT_DOSTOP);
		else
			ttydevsw_pktnotify(tp, TIOCPKT_NOSTOP);
		return (0);
	}
	case TIOCGETD:
		/* For compatibility - we only support TTYDISC. */
		*(int *)data = TTYDISC;
		return (0);
	case TIOCGPGRP:
		if (!tty_is_ctty(tp, td->td_proc))
			return (ENOTTY);

		if (tp->t_pgrp != NULL)
			*(int *)data = tp->t_pgrp->pg_id;
		else
			*(int *)data = NO_PID;
		return (0);
	case TIOCGSID:
		if (!tty_is_ctty(tp, td->td_proc))
			return (ENOTTY);

		MPASS(tp->t_session);
		*(int *)data = tp->t_session->s_sid;
		return (0);
	case TIOCSCTTY: {
		struct proc *p = td->td_proc;

		/* XXX: This looks awful. */
		tty_unlock(tp);
		sx_xlock(&proctree_lock);
		tty_lock(tp);

		if (!SESS_LEADER(p)) {
			/* Only the session leader may do this. */
			sx_xunlock(&proctree_lock);
			return (EPERM);
		}

		if (tp->t_session != NULL && tp->t_session == p->p_session) {
			/* This is already our controlling TTY. */
			sx_xunlock(&proctree_lock);
			return (0);
		}

		if (p->p_session->s_ttyp != NULL ||
		    (tp->t_session != NULL && tp->t_session->s_ttyvp != NULL &&
		    tp->t_session->s_ttyvp->v_type != VBAD)) {
			/*
			 * There is already a relation between a TTY and
			 * a session, or the caller is not the session
			 * leader.
			 *
			 * Allow the TTY to be stolen when the vnode is
			 * invalid, but the reference to the TTY is
			 * still active.  This allows immediate reuse of
			 * TTYs of which the session leader has been
			 * killed or the TTY revoked.
			 */
			sx_xunlock(&proctree_lock);
			return (EPERM);
		}

		/* Connect the session to the TTY. */
		tp->t_session = p->p_session;
		tp->t_session->s_ttyp = tp;
		tp->t_sessioncnt++;
		sx_xunlock(&proctree_lock);

		/* Assign foreground process group. */
		tp->t_pgrp = p->p_pgrp;
		PROC_LOCK(p);
		p->p_flag |= P_CONTROLT;
		PROC_UNLOCK(p);

		return (0);
	}
	case TIOCSPGRP: {
		struct pgrp *pg;

		/*
		 * XXX: Temporarily unlock the TTY to locate the process
		 * group. This code would be lot nicer if we would ever
		 * decompose proctree_lock.
		 */
		tty_unlock(tp);
		sx_slock(&proctree_lock);
		pg = pgfind(*(int *)data);
		if (pg != NULL)
			PGRP_UNLOCK(pg);
		if (pg == NULL || pg->pg_session != td->td_proc->p_session) {
			sx_sunlock(&proctree_lock);
			tty_lock(tp);
			return (EPERM);
		}
		tty_lock(tp);

		/*
		 * Determine if this TTY is the controlling TTY after
		 * relocking the TTY.
		 */
		if (!tty_is_ctty(tp, td->td_proc)) {
			sx_sunlock(&proctree_lock);
			return (ENOTTY);
		}
		tp->t_pgrp = pg;
		sx_sunlock(&proctree_lock);

		/* Wake up the background process groups. */
		cv_broadcast(&tp->t_bgwait);
		return (0);
	}
	case TIOCFLUSH: {
		int flags = *(int *)data;

		if (flags == 0)
			flags = (FREAD|FWRITE);
		else
			flags &= (FREAD|FWRITE);
		tty_flush(tp, flags);
		return (0);
	}
	case TIOCDRAIN:
		/* Drain TTY output. */
		return tty_drain(tp, 0);
	case TIOCGDRAINWAIT:
		*(int *)data = tp->t_drainwait;
		return (0);
	case TIOCSDRAINWAIT:
		error = priv_check(td, PRIV_TTY_DRAINWAIT);
		if (error == 0)
			tp->t_drainwait = *(int *)data;
		return (error);
	case TIOCCONS:
		/* Set terminal as console TTY. */
		if (*(int *)data) {
			error = priv_check(td, PRIV_TTY_CONSOLE);
			if (error)
				return (error);

			/*
			 * XXX: constty should really need to be locked!
			 * XXX: allow disconnected constty's to be stolen!
			 */

			if (constty == tp)
				return (0);
			if (constty != NULL)
				return (EBUSY);

			tty_unlock(tp);
			constty_set(tp);
			tty_lock(tp);
		} else if (constty == tp) {
			constty_clear();
		}
		return (0);
	case TIOCGWINSZ:
		/* Obtain window size. */
		*(struct winsize*)data = tp->t_winsize;
		return (0);
	case TIOCSWINSZ:
		/* Set window size. */
		tty_set_winsize(tp, data);
		return (0);
	case TIOCEXCL:
		tp->t_flags |= TF_EXCLUDE;
		return (0);
	case TIOCNXCL:
		tp->t_flags &= ~TF_EXCLUDE;
		return (0);
	case TIOCSTOP:
		tp->t_flags |= TF_STOPPED;
		ttydevsw_pktnotify(tp, TIOCPKT_STOP);
		return (0);
	case TIOCSTART:
		tp->t_flags &= ~TF_STOPPED;
		ttydevsw_outwakeup(tp);
		ttydevsw_pktnotify(tp, TIOCPKT_START);
		return (0);
	case TIOCSTAT:
		tty_info(tp);
		return (0);
	case TIOCSTI:
		if ((fflag & FREAD) == 0 && priv_check(td, PRIV_TTY_STI))
			return (EPERM);
		if (!tty_is_ctty(tp, td->td_proc) &&
		    priv_check(td, PRIV_TTY_STI))
			return (EACCES);
		ttydisc_rint(tp, *(char *)data, 0);
		ttydisc_rint_done(tp);
		return (0);
	}

#ifdef COMPAT_43TTY
	return tty_ioctl_compat(tp, cmd, data, fflag, td);
#else /* !COMPAT_43TTY */
	return (ENOIOCTL);
#endif /* COMPAT_43TTY */
}

int
tty_ioctl(struct tty *tp, u_long cmd, void *data, int fflag, struct thread *td)
{
	int error;

	tty_lock_assert(tp, MA_OWNED);

	if (tty_gone(tp))
		return (ENXIO);

	error = ttydevsw_ioctl(tp, cmd, data, td);
	if (error == ENOIOCTL)
		error = tty_generic_ioctl(tp, cmd, data, fflag, td);

	return (error);
}

dev_t
tty_udev(struct tty *tp)
{

	if (tp->t_dev)
		return (dev2udev(tp->t_dev));
	else
		return (NODEV);
}

int
tty_checkoutq(struct tty *tp)
{

	/* 256 bytes should be enough to print a log message. */
	return (ttyoutq_bytesleft(&tp->t_outq) >= 256);
}

void
tty_hiwat_in_block(struct tty *tp)
{

	if ((tp->t_flags & TF_HIWAT_IN) == 0 &&
	    tp->t_termios.c_iflag & IXOFF &&
	    tp->t_termios.c_cc[VSTOP] != _POSIX_VDISABLE) {
		/*
		 * Input flow control. Only enter the high watermark when we
		 * can successfully store the VSTOP character.
		 */
		if (ttyoutq_write_nofrag(&tp->t_outq,
		    &tp->t_termios.c_cc[VSTOP], 1) == 0)
			tp->t_flags |= TF_HIWAT_IN;
	} else {
		/* No input flow control. */
		tp->t_flags |= TF_HIWAT_IN;
	}
}

void
tty_hiwat_in_unblock(struct tty *tp)
{

	if (tp->t_flags & TF_HIWAT_IN &&
	    tp->t_termios.c_iflag & IXOFF &&
	    tp->t_termios.c_cc[VSTART] != _POSIX_VDISABLE) {
		/*
		 * Input flow control. Only leave the high watermark when we
		 * can successfully store the VSTART character.
		 */
		if (ttyoutq_write_nofrag(&tp->t_outq,
		    &tp->t_termios.c_cc[VSTART], 1) == 0)
			tp->t_flags &= ~TF_HIWAT_IN;
	} else {
		/* No input flow control. */
		tp->t_flags &= ~TF_HIWAT_IN;
	}

	if (!tty_gone(tp))
		ttydevsw_inwakeup(tp);
}

/*
 * TTY hooks interface.
 */

static int
ttyhook_defrint(struct tty *tp, char c, int flags)
{

	if (ttyhook_rint_bypass(tp, &c, 1) != 1)
		return (-1);

	return (0);
}

int
ttyhook_register(struct tty **rtp, struct proc *p, int fd, struct ttyhook *th,
    void *softc)
{
	struct tty *tp;
	struct file *fp;
	struct cdev *dev;
	struct cdevsw *cdp;
	struct filedesc *fdp;
	cap_rights_t rights;
	int error, ref;

	/* Validate the file descriptor. */
	fdp = p->p_fd;
	error = fget_unlocked(fdp, fd, cap_rights_init(&rights, CAP_TTYHOOK),
	    &fp, NULL);
	if (error != 0)
		return (error);
	if (fp->f_ops == &badfileops) {
		error = EBADF;
		goto done1;
	}

	/*
	 * Make sure the vnode is bound to a character device.
	 * Unlocked check for the vnode type is ok there, because we
	 * only shall prevent calling devvn_refthread on the file that
	 * never has been opened over a character device.
	 */
	if (fp->f_type != DTYPE_VNODE || fp->f_vnode->v_type != VCHR) {
		error = EINVAL;
		goto done1;
	}

	/* Make sure it is a TTY. */
	cdp = devvn_refthread(fp->f_vnode, &dev, &ref);
	if (cdp == NULL) {
		error = ENXIO;
		goto done1;
	}
	if (dev != fp->f_data) {
		error = ENXIO;
		goto done2;
	}
	if (cdp != &ttydev_cdevsw) {
		error = ENOTTY;
		goto done2;
	}
	tp = dev->si_drv1;

	/* Try to attach the hook to the TTY. */
	error = EBUSY;
	tty_lock(tp);
	MPASS((tp->t_hook == NULL) == ((tp->t_flags & TF_HOOK) == 0));
	if (tp->t_flags & TF_HOOK)
		goto done3;

	tp->t_flags |= TF_HOOK;
	tp->t_hook = th;
	tp->t_hooksoftc = softc;
	*rtp = tp;
	error = 0;

	/* Maybe we can switch into bypass mode now. */
	ttydisc_optimize(tp);

	/* Silently convert rint() calls to rint_bypass() when possible. */
	if (!ttyhook_hashook(tp, rint) && ttyhook_hashook(tp, rint_bypass))
		th->th_rint = ttyhook_defrint;

done3:	tty_unlock(tp);
done2:	dev_relthread(dev, ref);
done1:	fdrop(fp, curthread);
	return (error);
}

void
ttyhook_unregister(struct tty *tp)
{

	tty_lock_assert(tp, MA_OWNED);
	MPASS(tp->t_flags & TF_HOOK);

	/* Disconnect the hook. */
	tp->t_flags &= ~TF_HOOK;
	tp->t_hook = NULL;

	/* Maybe we need to leave bypass mode. */
	ttydisc_optimize(tp);

	/* Maybe deallocate the TTY as well. */
	tty_rel_free(tp);
}

/*
 * /dev/console handling.
 */

static int
ttyconsdev_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	struct tty *tp;

	/* System has no console device. */
	if (dev_console_filename == NULL)
		return (ENXIO);

	/* Look up corresponding TTY by device name. */
	sx_slock(&tty_list_sx);
	TAILQ_FOREACH(tp, &tty_list, t_list) {
		if (strcmp(dev_console_filename, tty_devname(tp)) == 0) {
			dev_console->si_drv1 = tp;
			break;
		}
	}
	sx_sunlock(&tty_list_sx);

	/* System console has no TTY associated. */
	if (dev_console->si_drv1 == NULL)
		return (ENXIO);

	return (ttydev_open(dev, oflags, devtype, td));
}

static int
ttyconsdev_write(struct cdev *dev, struct uio *uio, int ioflag)
{

	log_console(uio);

	return (ttydev_write(dev, uio, ioflag));
}

/*
 * /dev/console is a little different than normal TTY's.  When opened,
 * it determines which TTY to use.  When data gets written to it, it
 * will be logged in the kernel message buffer.
 */
static struct cdevsw ttyconsdev_cdevsw = {
	.d_version	= D_VERSION,
	.d_open		= ttyconsdev_open,
	.d_close	= ttydev_close,
	.d_read		= ttydev_read,
	.d_write	= ttyconsdev_write,
	.d_ioctl	= ttydev_ioctl,
	.d_kqfilter	= ttydev_kqfilter,
	.d_poll		= ttydev_poll,
	.d_mmap		= ttydev_mmap,
	.d_name		= "ttyconsdev",
	.d_flags	= D_TTY,
};

static void
ttyconsdev_init(void *unused __unused)
{

	dev_console = make_dev_credf(MAKEDEV_ETERNAL, &ttyconsdev_cdevsw, 0,
	    NULL, UID_ROOT, GID_WHEEL, 0600, "console");
}

SYSINIT(tty, SI_SUB_DRIVERS, SI_ORDER_FIRST, ttyconsdev_init, NULL);

void
ttyconsdev_select(const char *name)
{

	dev_console_filename = name;
}

/*
 * Debugging routines.
 */

#include "opt_ddb.h"
#ifdef DDB
#include <ddb/ddb.h>
#include <ddb/db_sym.h>

static const struct {
	int flag;
	char val;
} ttystates[] = {
#if 0
	{ TF_NOPREFIX,		'N' },
#endif
	{ TF_INITLOCK,		'I' },
	{ TF_CALLOUT,		'C' },

	/* Keep these together -> 'Oi' and 'Oo'. */
	{ TF_OPENED,		'O' },
	{ TF_OPENED_IN,		'i' },
	{ TF_OPENED_OUT,	'o' },
	{ TF_OPENED_CONS,	'c' },

	{ TF_GONE,		'G' },
	{ TF_OPENCLOSE,		'B' },
	{ TF_ASYNC,		'Y' },
	{ TF_LITERAL,		'L' },

	/* Keep these together -> 'Hi' and 'Ho'. */
	{ TF_HIWAT,		'H' },
	{ TF_HIWAT_IN,		'i' },
	{ TF_HIWAT_OUT,		'o' },

	{ TF_STOPPED,		'S' },
	{ TF_EXCLUDE,		'X' },
	{ TF_BYPASS,		'l' },
	{ TF_ZOMBIE,		'Z' },
	{ TF_HOOK,		's' },

	/* Keep these together -> 'bi' and 'bo'. */
	{ TF_BUSY,		'b' },
	{ TF_BUSY_IN,		'i' },
	{ TF_BUSY_OUT,		'o' },

	{ 0,			'\0'},
};

#define	TTY_FLAG_BITS \
	"\20\1NOPREFIX\2INITLOCK\3CALLOUT\4OPENED_IN" \
	"\5OPENED_OUT\6OPENED_CONS\7GONE\10OPENCLOSE" \
	"\11ASYNC\12LITERAL\13HIWAT_IN\14HIWAT_OUT" \
	"\15STOPPED\16EXCLUDE\17BYPASS\20ZOMBIE" \
	"\21HOOK\22BUSY_IN\23BUSY_OUT"

#define DB_PRINTSYM(name, addr) \
	db_printf("%s  " #name ": ", sep); \
	db_printsym((db_addr_t) addr, DB_STGY_ANY); \
	db_printf("\n");

static void
_db_show_devsw(const char *sep, const struct ttydevsw *tsw)
{

	db_printf("%sdevsw: ", sep);
	db_printsym((db_addr_t)tsw, DB_STGY_ANY);
	db_printf(" (%p)\n", tsw);
	DB_PRINTSYM(open, tsw->tsw_open);
	DB_PRINTSYM(close, tsw->tsw_close);
	DB_PRINTSYM(outwakeup, tsw->tsw_outwakeup);
	DB_PRINTSYM(inwakeup, tsw->tsw_inwakeup);
	DB_PRINTSYM(ioctl, tsw->tsw_ioctl);
	DB_PRINTSYM(param, tsw->tsw_param);
	DB_PRINTSYM(modem, tsw->tsw_modem);
	DB_PRINTSYM(mmap, tsw->tsw_mmap);
	DB_PRINTSYM(pktnotify, tsw->tsw_pktnotify);
	DB_PRINTSYM(free, tsw->tsw_free);
}

static void
_db_show_hooks(const char *sep, const struct ttyhook *th)
{

	db_printf("%shook: ", sep);
	db_printsym((db_addr_t)th, DB_STGY_ANY);
	db_printf(" (%p)\n", th);
	if (th == NULL)
		return;
	DB_PRINTSYM(rint, th->th_rint);
	DB_PRINTSYM(rint_bypass, th->th_rint_bypass);
	DB_PRINTSYM(rint_done, th->th_rint_done);
	DB_PRINTSYM(rint_poll, th->th_rint_poll);
	DB_PRINTSYM(getc_inject, th->th_getc_inject);
	DB_PRINTSYM(getc_capture, th->th_getc_capture);
	DB_PRINTSYM(getc_poll, th->th_getc_poll);
	DB_PRINTSYM(close, th->th_close);
}

static void
_db_show_termios(const char *name, const struct termios *t)
{

	db_printf("%s: iflag 0x%x oflag 0x%x cflag 0x%x "
	    "lflag 0x%x ispeed %u ospeed %u\n", name,
	    t->c_iflag, t->c_oflag, t->c_cflag, t->c_lflag,
	    t->c_ispeed, t->c_ospeed);
}

/* DDB command to show TTY statistics. */
DB_SHOW_COMMAND(tty, db_show_tty)
{
	struct tty *tp;

	if (!have_addr) {
		db_printf("usage: show tty <addr>\n");
		return;
	}
	tp = (struct tty *)addr;

	db_printf("%p: %s\n", tp, tty_devname(tp));
	db_printf("\tmtx: %p\n", tp->t_mtx);
	db_printf("\tflags: 0x%b\n", tp->t_flags, TTY_FLAG_BITS);
	db_printf("\trevokecnt: %u\n", tp->t_revokecnt);

	/* Buffering mechanisms. */
	db_printf("\tinq: %p begin %u linestart %u reprint %u end %u "
	    "nblocks %u quota %u\n", &tp->t_inq, tp->t_inq.ti_begin,
	    tp->t_inq.ti_linestart, tp->t_inq.ti_reprint, tp->t_inq.ti_end,
	    tp->t_inq.ti_nblocks, tp->t_inq.ti_quota);
	db_printf("\toutq: %p begin %u end %u nblocks %u quota %u\n",
	    &tp->t_outq, tp->t_outq.to_begin, tp->t_outq.to_end,
	    tp->t_outq.to_nblocks, tp->t_outq.to_quota);
	db_printf("\tinlow: %zu\n", tp->t_inlow);
	db_printf("\toutlow: %zu\n", tp->t_outlow);
	_db_show_termios("\ttermios", &tp->t_termios);
	db_printf("\twinsize: row %u col %u xpixel %u ypixel %u\n",
	    tp->t_winsize.ws_row, tp->t_winsize.ws_col,
	    tp->t_winsize.ws_xpixel, tp->t_winsize.ws_ypixel);
	db_printf("\tcolumn: %u\n", tp->t_column);
	db_printf("\twritepos: %u\n", tp->t_writepos);
	db_printf("\tcompatflags: 0x%x\n", tp->t_compatflags);

	/* Init/lock-state devices. */
	_db_show_termios("\ttermios_init_in", &tp->t_termios_init_in);
	_db_show_termios("\ttermios_init_out", &tp->t_termios_init_out);
	_db_show_termios("\ttermios_lock_in", &tp->t_termios_lock_in);
	_db_show_termios("\ttermios_lock_out", &tp->t_termios_lock_out);

	/* Hooks */
	_db_show_devsw("\t", tp->t_devsw);
	_db_show_hooks("\t", tp->t_hook);

	/* Process info. */
	db_printf("\tpgrp: %p gid %d jobc %d\n", tp->t_pgrp,
	    tp->t_pgrp ? tp->t_pgrp->pg_id : 0,
	    tp->t_pgrp ? tp->t_pgrp->pg_jobc : 0);
	db_printf("\tsession: %p", tp->t_session);
	if (tp->t_session != NULL)
	    db_printf(" count %u leader %p tty %p sid %d login %s",
		tp->t_session->s_count, tp->t_session->s_leader,
		tp->t_session->s_ttyp, tp->t_session->s_sid,
		tp->t_session->s_login);
	db_printf("\n");
	db_printf("\tsessioncnt: %u\n", tp->t_sessioncnt);
	db_printf("\tdevswsoftc: %p\n", tp->t_devswsoftc);
	db_printf("\thooksoftc: %p\n", tp->t_hooksoftc);
	db_printf("\tdev: %p\n", tp->t_dev);
}

/* DDB command to list TTYs. */
DB_SHOW_ALL_COMMAND(ttys, db_show_all_ttys)
{
	struct tty *tp;
	size_t isiz, osiz;
	int i, j;

	/* Make the output look like `pstat -t'. */
	db_printf("PTR        ");
#if defined(__LP64__)
	db_printf("        ");
#endif
	db_printf("      LINE   INQ  CAN  LIN  LOW  OUTQ  USE  LOW   "
	    "COL  SESS  PGID STATE\n");

	TAILQ_FOREACH(tp, &tty_list, t_list) {
		isiz = tp->t_inq.ti_nblocks * TTYINQ_DATASIZE;
		osiz = tp->t_outq.to_nblocks * TTYOUTQ_DATASIZE;

		db_printf("%p %10s %5zu %4u %4u %4zu %5zu %4u %4zu %5u %5d "
		    "%5d ", tp, tty_devname(tp), isiz,
		    tp->t_inq.ti_linestart - tp->t_inq.ti_begin,
		    tp->t_inq.ti_end - tp->t_inq.ti_linestart,
		    isiz - tp->t_inlow, osiz,
		    tp->t_outq.to_end - tp->t_outq.to_begin,
		    osiz - tp->t_outlow, MIN(tp->t_column, 99999),
		    tp->t_session ? tp->t_session->s_sid : 0,
		    tp->t_pgrp ? tp->t_pgrp->pg_id : 0);

		/* Flag bits. */
		for (i = j = 0; ttystates[i].flag; i++)
			if (tp->t_flags & ttystates[i].flag) {
				db_printf("%c", ttystates[i].val);
				j++;
			}
		if (j == 0)
			db_printf("-");
		db_printf("\n");
	}
}
#endif /* DDB */
