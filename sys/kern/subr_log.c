/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1993
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
 *	@(#)subr_log.c	8.1 (Berkeley) 6/10/93
 */

/*
 * Error log buffer for kernel printf's.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/filio.h>
#include <sys/ttycom.h>
#include <sys/msgbuf.h>
#include <sys/signalvar.h>
#include <sys/kernel.h>
#include <sys/poll.h>
#include <sys/filedesc.h>
#include <sys/sysctl.h>

#define LOG_RDPRI	(PZERO + 1)

#define LOG_ASYNC	0x04

static	d_open_t	logopen;
static	d_close_t	logclose;
static	d_read_t	logread;
static	d_ioctl_t	logioctl;
static	d_poll_t	logpoll;
static	d_kqfilter_t	logkqfilter;

static	void logtimeout(void *arg);

static struct cdevsw log_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	logopen,
	.d_close =	logclose,
	.d_read =	logread,
	.d_ioctl =	logioctl,
	.d_poll =	logpoll,
	.d_kqfilter =	logkqfilter,
	.d_name =	"log",
};

static int	logkqread(struct knote *note, long hint);
static void	logkqdetach(struct knote *note);

static struct filterops log_read_filterops = {
	.f_isfd =	1,
	.f_attach =	NULL,
	.f_detach =	logkqdetach,
	.f_event =	logkqread,
};

static struct logsoftc {
	int	sc_state;		/* see above for possibilities */
	struct	selinfo sc_selp;	/* process waiting on select call */
	struct  sigio *sc_sigio;	/* information for async I/O */
	struct	callout sc_callout;	/* callout to wakeup syslog  */
} logsoftc;

int			log_open;	/* also used in log() */
static struct cv	log_wakeup;
struct mtx		msgbuf_lock;
MTX_SYSINIT(msgbuf_lock, &msgbuf_lock, "msgbuf lock", MTX_DEF);

/* Times per second to check for a pending syslog wakeup. */
static int	log_wakeups_per_second = 5;
SYSCTL_INT(_kern, OID_AUTO, log_wakeups_per_second, CTLFLAG_RW,
    &log_wakeups_per_second, 0, "");

/*ARGSUSED*/
static	int
logopen(struct cdev *dev, int flags, int mode, struct thread *td)
{

	if (log_wakeups_per_second < 1) {
		printf("syslog wakeup is less than one.  Adjusting to 1.\n");
		log_wakeups_per_second = 1;
	}

	mtx_lock(&msgbuf_lock);
	if (log_open) {
		mtx_unlock(&msgbuf_lock);
		return (EBUSY);
	}
	log_open = 1;
	callout_reset_sbt(&logsoftc.sc_callout,
	    SBT_1S / log_wakeups_per_second, 0, logtimeout, NULL, C_PREL(1));
	mtx_unlock(&msgbuf_lock);

	fsetown(td->td_proc->p_pid, &logsoftc.sc_sigio);	/* signal process only */
	return (0);
}

/*ARGSUSED*/
static	int
logclose(struct cdev *dev, int flag, int mode, struct thread *td)
{

	funsetown(&logsoftc.sc_sigio);

	mtx_lock(&msgbuf_lock);
	callout_stop(&logsoftc.sc_callout);
	logsoftc.sc_state = 0;
	log_open = 0;
	mtx_unlock(&msgbuf_lock);

	return (0);
}

/*ARGSUSED*/
static	int
logread(struct cdev *dev, struct uio *uio, int flag)
{
	char buf[128];
	struct msgbuf *mbp = msgbufp;
	int error = 0, l;

	mtx_lock(&msgbuf_lock);
	while (msgbuf_getcount(mbp) == 0) {
		if (flag & IO_NDELAY) {
			mtx_unlock(&msgbuf_lock);
			return (EWOULDBLOCK);
		}
		if ((error = cv_wait_sig(&log_wakeup, &msgbuf_lock)) != 0) {
			mtx_unlock(&msgbuf_lock);
			return (error);
		}
	}

	while (uio->uio_resid > 0) {
		l = imin(sizeof(buf), uio->uio_resid);
		l = msgbuf_getbytes(mbp, buf, l);
		if (l == 0)
			break;
		mtx_unlock(&msgbuf_lock);
		error = uiomove(buf, l, uio);
		if (error || uio->uio_resid == 0)
			return (error);
		mtx_lock(&msgbuf_lock);
	}
	mtx_unlock(&msgbuf_lock);
	return (error);
}

/*ARGSUSED*/
static	int
logpoll(struct cdev *dev, int events, struct thread *td)
{
	int revents = 0;

	if (events & (POLLIN | POLLRDNORM)) {
		mtx_lock(&msgbuf_lock);
		if (msgbuf_getcount(msgbufp) > 0)
			revents |= events & (POLLIN | POLLRDNORM);
		else
			selrecord(td, &logsoftc.sc_selp);
		mtx_unlock(&msgbuf_lock);
	}
	return (revents);
}

static int
logkqfilter(struct cdev *dev, struct knote *kn)
{

	if (kn->kn_filter != EVFILT_READ)
		return (EINVAL);

	kn->kn_fop = &log_read_filterops;
	kn->kn_hook = NULL;

	mtx_lock(&msgbuf_lock);
	knlist_add(&logsoftc.sc_selp.si_note, kn, 1);
	mtx_unlock(&msgbuf_lock);
	return (0);
}

static int
logkqread(struct knote *kn, long hint)
{

	mtx_assert(&msgbuf_lock, MA_OWNED);
	kn->kn_data = msgbuf_getcount(msgbufp);
	return (kn->kn_data != 0);
}

static void
logkqdetach(struct knote *kn)
{

	mtx_lock(&msgbuf_lock);
	knlist_remove(&logsoftc.sc_selp.si_note, kn, 1);
	mtx_unlock(&msgbuf_lock);
}

static void
logtimeout(void *arg)
{

	if (!log_open)
		return;
	if (msgbuftrigger == 0)
		goto done;
	msgbuftrigger = 0;
	selwakeuppri(&logsoftc.sc_selp, LOG_RDPRI);
	KNOTE_LOCKED(&logsoftc.sc_selp.si_note, 0);
	if ((logsoftc.sc_state & LOG_ASYNC) && logsoftc.sc_sigio != NULL)
		pgsigio(&logsoftc.sc_sigio, SIGIO, 0);
	cv_broadcastpri(&log_wakeup, LOG_RDPRI);
done:
	if (log_wakeups_per_second < 1) {
		printf("syslog wakeup is less than one.  Adjusting to 1.\n");
		log_wakeups_per_second = 1;
	}
	callout_reset_sbt(&logsoftc.sc_callout,
	    SBT_1S / log_wakeups_per_second, 0, logtimeout, NULL, C_PREL(1));
}

/*ARGSUSED*/
static	int
logioctl(struct cdev *dev, u_long com, caddr_t data, int flag, struct thread *td)
{

	switch (com) {

	/* return number of characters immediately available */
	case FIONREAD:
		*(int *)data = msgbuf_getcount(msgbufp);
		break;

	case FIONBIO:
		break;

	case FIOASYNC:
		mtx_lock(&msgbuf_lock);
		if (*(int *)data)
			logsoftc.sc_state |= LOG_ASYNC;
		else
			logsoftc.sc_state &= ~LOG_ASYNC;
		mtx_unlock(&msgbuf_lock);
		break;

	case FIOSETOWN:
		return (fsetown(*(int *)data, &logsoftc.sc_sigio));

	case FIOGETOWN:
		*(int *)data = fgetown(&logsoftc.sc_sigio);
		break;

	/* This is deprecated, FIOSETOWN should be used instead. */
	case TIOCSPGRP:
		return (fsetown(-(*(int *)data), &logsoftc.sc_sigio));

	/* This is deprecated, FIOGETOWN should be used instead */
	case TIOCGPGRP:
		*(int *)data = -fgetown(&logsoftc.sc_sigio);
		break;

	default:
		return (ENOTTY);
	}
	return (0);
}

static void
log_drvinit(void *unused)
{

	cv_init(&log_wakeup, "klog");
	callout_init_mtx(&logsoftc.sc_callout, &msgbuf_lock, 0);
	knlist_init_mtx(&logsoftc.sc_selp.si_note, &msgbuf_lock);
	make_dev_credf(MAKEDEV_ETERNAL, &log_cdevsw, 0, NULL, UID_ROOT,
	    GID_WHEEL, 0600, "klog");
}

SYSINIT(logdev,SI_SUB_DRIVERS,SI_ORDER_MIDDLE,log_drvinit,NULL);
