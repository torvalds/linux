/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Ed Schouten <ed@FreeBSD.org>
 * All rights reserved.
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
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/filio.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/poll.h>
#include <sys/proc.h>
#include <sys/snoop.h>
#include <sys/sx.h>
#include <sys/systm.h>
#include <sys/tty.h>
#include <sys/uio.h>

static struct cdev	*snp_dev;
static MALLOC_DEFINE(M_SNP, "snp", "tty snoop device");

/* XXX: should be mtx, but TTY can be locked by Giant. */
#if 0
static struct mtx	snp_register_lock;
MTX_SYSINIT(snp_register_lock, &snp_register_lock,
    "tty snoop registration", MTX_DEF);
#define	SNP_LOCK()	mtx_lock(&snp_register_lock)
#define	SNP_UNLOCK()	mtx_unlock(&snp_register_lock)
#else
static struct sx	snp_register_lock;
SX_SYSINIT(snp_register_lock, &snp_register_lock,
    "tty snoop registration");
#define	SNP_LOCK()	sx_xlock(&snp_register_lock)
#define	SNP_UNLOCK()	sx_xunlock(&snp_register_lock)
#endif

#define	SNPGTYY_32DEV	_IOR('T', 89, uint32_t)

/*
 * There is no need to have a big input buffer. In most typical setups,
 * we won't inject much data into the TTY, because users can't type
 * really fast.
 */
#define SNP_INPUT_BUFSIZE	16
/*
 * The output buffer has to be really big. Right now we don't support
 * any form of flow control, which means we lost any data we can't
 * accept. We set the output buffer size to about twice the size of a
 * pseudo-terminal/virtual console's output buffer.
 */
#define SNP_OUTPUT_BUFSIZE	16384

static d_open_t		snp_open;
static d_read_t		snp_read;
static d_write_t	snp_write;
static d_ioctl_t	snp_ioctl;
static d_poll_t		snp_poll;

static struct cdevsw snp_cdevsw = {
	.d_version	= D_VERSION,
	.d_open		= snp_open,
	.d_read		= snp_read,
	.d_write	= snp_write,
	.d_ioctl	= snp_ioctl,
	.d_poll		= snp_poll,
	.d_name		= "snp",
};

static th_getc_capture_t	snp_getc_capture;

static struct ttyhook snp_hook = {
	.th_getc_capture	= snp_getc_capture,
};

/*
 * Per-instance structure.
 *
 * List of locks
 * (r)	locked by snp_register_lock on assignment
 * (t)	locked by tty_lock
 */
struct snp_softc {
	struct tty	*snp_tty;	/* (r) TTY we're snooping. */
	struct ttyoutq	snp_outq;	/* (t) Output queue. */
	struct cv	snp_outwait;	/* (t) Output wait queue. */
	struct selinfo	snp_outpoll;	/* (t) Output polling. */
};

static void
snp_dtor(void *data)
{
	struct snp_softc *ss = data;
	struct tty *tp;

	tp = ss->snp_tty;
	if (tp != NULL) {
		tty_lock(tp);
		ttyoutq_free(&ss->snp_outq);
		ttyhook_unregister(tp);
	}

	cv_destroy(&ss->snp_outwait);
	free(ss, M_SNP);
}

/*
 * Snoop device node routines.
 */

static int
snp_open(struct cdev *dev, int flag, int mode, struct thread *td)
{
	struct snp_softc *ss;

	/* Allocate per-snoop data. */
	ss = malloc(sizeof(struct snp_softc), M_SNP, M_WAITOK|M_ZERO);
	cv_init(&ss->snp_outwait, "snp out");

	devfs_set_cdevpriv(ss, snp_dtor);

	return (0);
}

static int
snp_read(struct cdev *dev, struct uio *uio, int flag)
{
	int error, oresid = uio->uio_resid;
	struct snp_softc *ss;
	struct tty *tp;

	if (uio->uio_resid == 0)
		return (0);

	error = devfs_get_cdevpriv((void **)&ss);
	if (error != 0)
		return (error);

	tp = ss->snp_tty;
	if (tp == NULL || tty_gone(tp))
		return (EIO);

	tty_lock(tp);
	for (;;) {
		error = ttyoutq_read_uio(&ss->snp_outq, tp, uio);
		if (error != 0 || uio->uio_resid != oresid)
			break;

		/* Wait for more data. */
		if (flag & O_NONBLOCK) {
			error = EWOULDBLOCK;
			break;
		}
		error = cv_wait_sig(&ss->snp_outwait, tp->t_mtx);
		if (error != 0)
			break;
		if (tty_gone(tp)) {
			error = EIO;
			break;
		}
	}
	tty_unlock(tp);

	return (error);
}

static int
snp_write(struct cdev *dev, struct uio *uio, int flag)
{
	struct snp_softc *ss;
	struct tty *tp;
	int error, len;
	char in[SNP_INPUT_BUFSIZE];

	error = devfs_get_cdevpriv((void **)&ss);
	if (error != 0)
		return (error);

	tp = ss->snp_tty;
	if (tp == NULL || tty_gone(tp))
		return (EIO);

	while (uio->uio_resid > 0) {
		/* Read new data. */
		len = imin(uio->uio_resid, sizeof in);
		error = uiomove(in, len, uio);
		if (error != 0)
			return (error);

		tty_lock(tp);

		/* Driver could have abandoned the TTY in the mean time. */
		if (tty_gone(tp)) {
			tty_unlock(tp);
			return (ENXIO);
		}

		/*
		 * Deliver data to the TTY. Ignore errors for now,
		 * because we shouldn't bail out when we're running
		 * close to the watermarks.
		 */
		ttydisc_rint_simple(tp, in, len);
		ttydisc_rint_done(tp);

		tty_unlock(tp);
	}

	return (0);
}

static int
snp_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int flags,
    struct thread *td)
{
	struct snp_softc *ss;
	struct tty *tp;
	int error;

	error = devfs_get_cdevpriv((void **)&ss);
	if (error != 0)
		return (error);

	switch (cmd) {
	case SNPSTTY:
		/* Bind TTY to snoop instance. */
		SNP_LOCK();
		if (ss->snp_tty != NULL) {
			SNP_UNLOCK();
			return (EBUSY);
		}
		/*
		 * XXXRW / XXXJA: no capability check here.
		 */
		error = ttyhook_register(&ss->snp_tty, td->td_proc,
		    *(int *)data, &snp_hook, ss);
		SNP_UNLOCK();
		if (error != 0)
			return (error);

		/* Now that went okay, allocate a buffer for the queue. */
		tp = ss->snp_tty;
		tty_lock(tp);
		ttyoutq_setsize(&ss->snp_outq, tp, SNP_OUTPUT_BUFSIZE);
		tty_unlock(tp);

		return (0);
	case SNPGTTY:
		/* Obtain device number of associated TTY. */
		if (ss->snp_tty == NULL)
			*(dev_t *)data = NODEV;
		else
			*(dev_t *)data = tty_udev(ss->snp_tty);
		return (0);
	case SNPGTYY_32DEV:
		if (ss->snp_tty == NULL)
			*(uint32_t *)data = -1;
		else
			*(uint32_t *)data = tty_udev(ss->snp_tty); /* trunc */
		return (0);
	case FIONREAD:
		tp = ss->snp_tty;
		if (tp != NULL) {
			tty_lock(tp);
			*(int *)data = ttyoutq_bytesused(&ss->snp_outq);
			tty_unlock(tp);
		} else {
			*(int *)data = 0;
		}
		return (0);
	default:
		return (ENOTTY);
	}
}

static int
snp_poll(struct cdev *dev, int events, struct thread *td)
{
	struct snp_softc *ss;
	struct tty *tp;
	int revents;

	if (devfs_get_cdevpriv((void **)&ss) != 0)
		return (events &
		    (POLLHUP|POLLIN|POLLRDNORM|POLLOUT|POLLWRNORM));

	revents = 0;

	if (events & (POLLIN | POLLRDNORM)) {
		tp = ss->snp_tty;
		if (tp != NULL) {
			tty_lock(tp);
			if (ttyoutq_bytesused(&ss->snp_outq) > 0)
				revents |= events & (POLLIN | POLLRDNORM);
			tty_unlock(tp);
		}
	}

	if (revents == 0)
		selrecord(td, &ss->snp_outpoll);

	return (revents);
}

/*
 * TTY hook events.
 */

static int
snp_modevent(module_t mod, int type, void *data)
{

	switch (type) {
	case MOD_LOAD:
		snp_dev = make_dev(&snp_cdevsw, 0,
		    UID_ROOT, GID_WHEEL, 0600, "snp");
		return (0);
	case MOD_UNLOAD:
		/* XXX: Make existing users leave. */
		destroy_dev(snp_dev);
		return (0);
	default:
		return (EOPNOTSUPP);
	}
}

static void
snp_getc_capture(struct tty *tp, const void *buf, size_t len)
{
	struct snp_softc *ss = ttyhook_softc(tp);

	ttyoutq_write(&ss->snp_outq, buf, len);

	cv_broadcast(&ss->snp_outwait);
	selwakeup(&ss->snp_outpoll);
}

static moduledata_t snp_mod = {
	"snp",
	snp_modevent,
	NULL
};

DECLARE_MODULE(snp, snp_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
