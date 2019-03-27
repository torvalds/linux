/*-
 * Copyright (c) 2006-2007 Daniel Roethlisberger <daniel@roe.ch>
 * Copyright (c) 2000-2004 OMNIKEY GmbH (www.omnikey.com)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

/*
 * OMNIKEY CardMan 4040 a.k.a. CardMan eXtended (cmx) driver.
 * This is a PCMCIA based smartcard reader which seems to work
 * like an I/O port mapped USB CCID smartcard device.
 *
 * I/O originally based on Linux driver version 1.1.0 by OMNIKEY.
 * Dual GPL/BSD.  Almost all of the code has been rewritten.
 * $Omnikey: cm4040_cs.c,v 1.7 2004/10/04 09:08:50 jp Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/poll.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/uio.h>
#include <sys/selinfo.h>

#include <sys/module.h>
#include <sys/bus.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <dev/cmx/cmxvar.h>
#include <dev/cmx/cmxreg.h>

#ifdef CMX_DEBUG
#define	DEBUG_printf(dev, fmt, args...) \
	device_printf(dev, "%s: " fmt, __FUNCTION__, ##args)
#else
#define	DEBUG_printf(dev, fmt, args...)
#endif

#define	SPIN_COUNT				1000
#define	WAIT_TICKS				(hz/100)
#define	POLL_TICKS				(hz/10)

/* possibly bogus */
#define	CCID_DRIVER_BULK_DEFAULT_TIMEOUT	(150*hz)
#define	CCID_DRIVER_ASYNC_POWERUP_TIMEOUT	(35*hz)
#define	CCID_DRIVER_MINIMUM_TIMEOUT		(3*hz)

#ifdef CMX_DEBUG
static char	BSRBITS[] = "\020"
	"\01BULK_OUT_FULL"		/* 0x01 */
	"\02BULK_IN_FULL"		/* 0x02 */
	"\03(0x04)";			/* 0x04 */
#ifdef CMX_INTR
static char	SCRBITS[] = "\020"
	"\01POWER_DOWN"			/* 0x01 */
	"\02PULSE_INTERRUPT"		/* 0x02 */
	"\03HOST_TO_READER_DONE"	/* 0x04 */
	"\04READER_TO_HOST_DONE"	/* 0x08 */
	"\05ACK_NOTIFY"			/* 0x10 */
	"\06EN_NOTIFY"			/* 0x20 */
	"\07ABORT"			/* 0x40 */
	"\10HOST_TO_READER_START";	/* 0x80 */
#endif /* CMX_INTR */
static char	POLLBITS[] = "\020"
	"\01POLLIN"			/* 0x0001 */
	"\02POLLPRI"			/* 0x0002 */
	"\03POLLOUT"			/* 0x0004 */
	"\04POLLERR"			/* 0x0008 */
	"\05POLLHUP"			/* 0x0010 */
	"\06POLLINVAL"			/* 0x0020 */
	"\07POLLRDNORM"			/* 0x0040 */
	"\10POLLRDBAND"			/* 0x0080 */
	"\11POLLWRBAND";		/* 0x0100 */
static char	MODEBITS[] = "\020"
	"\01READ"			/* 0x0001 */
	"\02WRITE"			/* 0x0002 */
	"\03NONBLOCK"			/* 0x0004 */
	"\04APPEND"			/* 0x0008 */
	"\05SHLOCK"			/* 0x0010 */
	"\06EXLOCK"			/* 0x0020 */
	"\07ASYNC"			/* 0x0040 */
	"\10FSYNC"			/* 0x0080 */
	"\11NOFOLLOW"			/* 0x0100 */
	"\12CREAT"			/* 0x0200 */
	"\13TRUNK"			/* 0x0400 */
	"\14EXCL"			/* 0x0800 */
	"\15(0x1000)"			/* 0x1000 */
	"\16(0x2000)"			/* 0x2000 */
	"\17HASLOCK"			/* 0x4000 */
	"\20NOCTTY"			/* 0x8000 */
	"\21DIRECT";			/* 0x00010000 */
#endif /* CMX_DEBUG */

devclass_t cmx_devclass;

static d_open_t		cmx_open;
static d_close_t	cmx_close;
static d_read_t		cmx_read;
static d_write_t	cmx_write;
static d_poll_t		cmx_poll;
#ifdef CMX_INTR
static void		cmx_intr(void *arg);
#endif

static struct cdevsw cmx_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	cmx_open,
	.d_close =	cmx_close,
	.d_read =	cmx_read,
	.d_write =	cmx_write,
	.d_poll =	cmx_poll,
	.d_name =	"cmx",
};

/*
 * Initialize the softc structure.  Must be called from
 * the bus specific device allocation routine.
 */
void
cmx_init_softc(device_t dev)
{
	struct cmx_softc *sc = device_get_softc(dev);
	sc->dev = dev;
	sc->timeout = CCID_DRIVER_MINIMUM_TIMEOUT;
}

/*
 * Allocate driver resources.  Must be called from the
 * bus specific device allocation routine.  Caller must
 * ensure to call cmx_release_resources to free the
 * resources when detaching.
 * Return zero if successful, and ENOMEM if the resources
 * could not be allocated.
 */
int
cmx_alloc_resources(device_t dev)
{
	struct cmx_softc *sc = device_get_softc(dev);
#ifdef CMX_INTR
	int rv;
#endif

	sc->ioport = bus_alloc_resource_any(dev, SYS_RES_IOPORT,
			&sc->ioport_rid, RF_ACTIVE);
	if (!sc->ioport) {
		device_printf(dev, "failed to allocate io port\n");
		return ENOMEM;
	}
	sc->bst = rman_get_bustag(sc->ioport);
	sc->bsh = rman_get_bushandle(sc->ioport);

#ifdef CMX_INTR
	sc->irq = bus_alloc_resource_any(dev, SYS_RES_IRQ,
			&sc->irq_rid, RF_ACTIVE);
	if (!sc->irq) {
		device_printf(dev, "failed to allocate irq\n");
		return ENOMEM;
	}
	if ((rv = bus_setup_intr(dev, sc->irq, INTR_TYPE_TTY,
			cmx_intr, sc, &sc->ih)) != 0) {
		device_printf(dev, "failed to set up irq\n");
		return ENOMEM;
	}
#endif

	mtx_init(&sc->mtx, device_get_nameunit(dev),
			"cmx softc lock",
			MTX_DEF | MTX_RECURSE);
	callout_init_mtx(&sc->ch, &sc->mtx, 0);

	return 0;
}

/*
 * Release the resources allocated by cmx_allocate_resources.
 */
void
cmx_release_resources(device_t dev)
{
	struct cmx_softc *sc = device_get_softc(dev);

	mtx_destroy(&sc->mtx);

#ifdef CMX_INTR
	if (sc->ih) {
		bus_teardown_intr(dev, sc->irq, sc->ih);
		sc->ih = NULL;
	}
	if (sc->irq) {
		bus_release_resource(dev, SYS_RES_IRQ,
				sc->irq_rid, sc->irq);
		sc->irq = NULL;
	}
#endif

	if (sc->ioport) {
		bus_deactivate_resource(dev, SYS_RES_IOPORT,
				sc->ioport_rid, sc->ioport);
		bus_release_resource(dev, SYS_RES_IOPORT,
				sc->ioport_rid, sc->ioport);
		sc->ioport = NULL;
	}
	return;
}

/*
 * Bus independent device attachment routine.  Creates the
 * character device node.
 */
int
cmx_attach(device_t dev)
{
	struct cmx_softc *sc = device_get_softc(dev);

	if (!sc || sc->dying)
		return ENXIO;

	sc->cdev = make_dev(&cmx_cdevsw, 0, UID_ROOT, GID_WHEEL, 0600,
	                    "cmx%d", device_get_unit(dev));
	if (!sc->cdev) {
		device_printf(dev, "failed to create character device\n");
		return ENOMEM;
	}
	sc->cdev->si_drv1 = sc;

	return 0;
}

/*
 * Bus independent device detachment routine.  Makes sure all
 * allocated resources are freed, callouts disabled and waiting
 * processes unblocked.
 */
int
cmx_detach(device_t dev)
{
	struct cmx_softc *sc = device_get_softc(dev);

	DEBUG_printf(dev, "called\n");

	sc->dying = 1;

	CMX_LOCK(sc);
	if (sc->polling) {
		DEBUG_printf(sc->dev, "disabling polling\n");
		callout_stop(&sc->ch);
		sc->polling = 0;
		CMX_UNLOCK(sc);
		callout_drain(&sc->ch);
		selwakeuppri(&sc->sel, PZERO);
	} else {
		CMX_UNLOCK(sc);
	}

	wakeup(sc);
	destroy_dev(sc->cdev);

	DEBUG_printf(dev, "releasing resources\n");
	cmx_release_resources(dev);
	return 0;
}

/*
 * Wait for buffer status register events.  If test is non-zero,
 * wait until flags are set, otherwise wait until flags are unset.
 * Will spin SPIN_COUNT times, then sleep until timeout is reached.
 * Returns zero if event happened, EIO if the timeout was reached,
 * and ENXIO if the device was detached in the meantime.  When that
 * happens, the caller must quit immediately, since a detach is
 * in progress.
 */
static inline int
cmx_wait_BSR(struct cmx_softc *sc, uint8_t flags, int test)
{
	int rv;

	for (int i = 0; i < SPIN_COUNT; i++) {
		if (cmx_test_BSR(sc, flags, test))
			return 0;
	}

	for (int i = 0; i * WAIT_TICKS < sc->timeout; i++) {
		if (cmx_test_BSR(sc, flags, test))
			return 0;
		rv = tsleep(sc, PWAIT|PCATCH, "cmx", WAIT_TICKS);
		/*
		 * Currently, the only reason for waking up with
		 * rv == 0 is when we are detaching, in which
		 * case sc->dying is always 1.
		 */
		if (sc->dying)
			return ENXIO;
		if (rv != EAGAIN)
			return rv;
	}

	/* timeout */
	return EIO;
}

/*
 * Set the sync control register to val.  Before and after writing
 * to the SCR, we wait for the BSR to not signal BULK_OUT_FULL.
 * Returns zero if successful, or whatever errors cmx_wait_BSR can
 * return.  ENXIO signals that the device has been detached in the
 * meantime, and that we should leave the kernel immediately.
 */
static inline int
cmx_sync_write_SCR(struct cmx_softc *sc, uint8_t val)
{
	int rv = 0;

	if ((rv = cmx_wait_BSR(sc, BSR_BULK_OUT_FULL, 0)) != 0) {
		return rv;
	}

	cmx_write_SCR(sc, val);

	if ((rv = cmx_wait_BSR(sc, BSR_BULK_OUT_FULL, 0)) != 0) {
		return rv;
	}

	return 0;
}

/*
 * Returns a suitable timeout value based on the given command byte.
 * Some commands appear to need longer timeout values than others.
 */
static inline unsigned long
cmx_timeout_by_cmd(uint8_t cmd)
{
	switch (cmd) {
	case CMD_PC_TO_RDR_XFRBLOCK:
	case CMD_PC_TO_RDR_SECURE:
	case CMD_PC_TO_RDR_TEST_SECURE:
	case CMD_PC_TO_RDR_OK_SECURE:
		return CCID_DRIVER_BULK_DEFAULT_TIMEOUT;

	case CMD_PC_TO_RDR_ICCPOWERON:
		return CCID_DRIVER_ASYNC_POWERUP_TIMEOUT;

	case CMD_PC_TO_RDR_GETSLOTSTATUS:
	case CMD_PC_TO_RDR_ICCPOWEROFF:
	case CMD_PC_TO_RDR_GETPARAMETERS:
	case CMD_PC_TO_RDR_RESETPARAMETERS:
	case CMD_PC_TO_RDR_SETPARAMETERS:
	case CMD_PC_TO_RDR_ESCAPE:
	case CMD_PC_TO_RDR_ICCCLOCK:
	default:
		return CCID_DRIVER_MINIMUM_TIMEOUT;
	}
}

/*
 * Periodical callout routine, polling the reader for data
 * availability.  If the reader signals data ready for reading,
 * wakes up the processes which are waiting in select()/poll().
 * Otherwise, reschedules itself with a delay of POLL_TICKS.
 */
static void
cmx_tick(void *xsc)
{
	struct cmx_softc *sc = xsc;
	uint8_t bsr;

	CMX_LOCK(sc);
	if (sc->polling && !sc->dying) {
		bsr = cmx_read_BSR(sc);
		DEBUG_printf(sc->dev, "BSR=%b\n", bsr, BSRBITS);
		if (cmx_test(bsr, BSR_BULK_IN_FULL, 1)) {
			sc->polling = 0;
			selwakeuppri(&sc->sel, PZERO);
		} else {
			callout_reset(&sc->ch, POLL_TICKS, cmx_tick, sc);
		}
	}
	CMX_UNLOCK(sc);
}

/*
 * Open the character device.  Only a single process may open the
 * device at a time.
 */
static int
cmx_open(struct cdev *cdev, int flags, int fmt, struct thread *td)
{
	struct cmx_softc *sc = cdev->si_drv1;

	if (sc == NULL || sc->dying)
		return ENXIO;

	CMX_LOCK(sc);
	if (sc->open) {
		CMX_UNLOCK(sc);
		return EBUSY;
	}
	sc->open = 1;
	CMX_UNLOCK(sc);

	DEBUG_printf(sc->dev, "open (flags=%b thread=%p)\n",
			flags, MODEBITS, td);
	return 0;
}

/*
 * Close the character device.
 */
static int
cmx_close(struct cdev *cdev, int flags, int fmt, struct thread *td)
{
	struct cmx_softc *sc = cdev->si_drv1;

	if (sc == NULL || sc->dying)
		return ENXIO;

	CMX_LOCK(sc);
	if (!sc->open) {
		CMX_UNLOCK(sc);
		return EINVAL;
	}
	if (sc->polling) {
		DEBUG_printf(sc->dev, "disabling polling\n");
		callout_stop(&sc->ch);
		sc->polling = 0;
		CMX_UNLOCK(sc);
		callout_drain(&sc->ch);
		selwakeuppri(&sc->sel, PZERO);
		CMX_LOCK(sc);
	}
	sc->open = 0;
	CMX_UNLOCK(sc);

	DEBUG_printf(sc->dev, "close (flags=%b thread=%p)\n",
			flags, MODEBITS, td);
	return 0;
}

/*
 * Read from the character device.
 * Returns zero if successful, ENXIO if dying, EINVAL if an attempt
 * was made to read less than CMX_MIN_RDLEN bytes or less than the
 * device has available, or any of the errors that cmx_sync_write_SCR
 * can return.  Partial reads are not supported.
 */
static int
cmx_read(struct cdev *cdev, struct uio *uio, int flag)
{
	struct cmx_softc *sc = cdev->si_drv1;
	unsigned long bytes_left;
	uint8_t uc;
	int rv, amnt, offset;

	if (sc == NULL || sc->dying)
		return ENXIO;

	DEBUG_printf(sc->dev, "called (len=%d flag=%b)\n",
		uio->uio_resid, flag, MODEBITS);

	CMX_LOCK(sc);
	if (sc->polling) {
		DEBUG_printf(sc->dev, "disabling polling\n");
		callout_stop(&sc->ch);
		sc->polling = 0;
		CMX_UNLOCK(sc);
		callout_drain(&sc->ch);
		selwakeuppri(&sc->sel, PZERO);
	} else {
		CMX_UNLOCK(sc);
	}

	if (uio->uio_resid == 0) {
		return 0;
	}

	if (uio->uio_resid < CMX_MIN_RDLEN) {
		return EINVAL;
	}

	if (flag & O_NONBLOCK) {
		if (cmx_test_BSR(sc, BSR_BULK_IN_FULL, 0)) {
			return EAGAIN;
		}
	}

	for (int i = 0; i < 5; i++) {
		if ((rv = cmx_wait_BSR(sc, BSR_BULK_IN_FULL, 1)) != 0) {
			return rv;
		}
		sc->buf[i] = cmx_read_DTR(sc);
		DEBUG_printf(sc->dev, "buf[%02x]=%02x\n", i, sc->buf[i]);
	}

	bytes_left = CMX_MIN_RDLEN +
	                (0x000000FF&((char)sc->buf[1])) +
	                (0x0000FF00&((char)sc->buf[2] << 8)) +
	                (0x00FF0000&((char)sc->buf[3] << 16)) +
	                (0xFF000000&((char)sc->buf[4] << 24));
	DEBUG_printf(sc->dev, "msgsz=%lu\n", bytes_left);

	if (uio->uio_resid < bytes_left) {
		return EINVAL;
	}

	offset = 5; /* prefetched header */
	while (bytes_left > 0) {
		amnt = MIN(bytes_left, sizeof(sc->buf));

		for (int i = offset; i < amnt; i++) {
			if ((rv = cmx_wait_BSR(sc, BSR_BULK_IN_FULL, 1))!=0) {
				return rv;
			}
			sc->buf[i] = cmx_read_DTR(sc);
			DEBUG_printf(sc->dev, "buf[%02x]=%02x\n",
					i, sc->buf[i]);
		}

		if ((rv = uiomove(sc->buf, amnt, uio)) != 0) {
			DEBUG_printf(sc->dev, "uiomove failed (%d)\n", rv);
			return rv;
		}

		if (offset)
			offset = 0;
		bytes_left -= amnt;
	}

	if ((rv = cmx_wait_BSR(sc, BSR_BULK_IN_FULL, 1)) != 0) {
		return rv;
	}

	if ((rv = cmx_sync_write_SCR(sc, SCR_READER_TO_HOST_DONE)) != 0) {
		return rv;
	}

	uc = cmx_read_DTR(sc);
	DEBUG_printf(sc->dev, "success (DTR=%02x)\n", uc);
	return 0;
}

/*
 * Write to the character device.
 * Returns zero if successful, NXIO if dying, EINVAL if less data
 * written than CMX_MIN_WRLEN, or any of the errors that cmx_sync_SCR
 * can return.
 */
static int
cmx_write(struct cdev *cdev, struct uio *uio, int flag)
{
	struct cmx_softc *sc = cdev->si_drv1;
	int rv, amnt;

	if (sc == NULL || sc->dying)
		return ENXIO;

	DEBUG_printf(sc->dev, "called (len=%d flag=%b)\n",
			uio->uio_resid, flag, MODEBITS);

	if (uio->uio_resid == 0) {
		return 0;
	}

	if (uio->uio_resid < CMX_MIN_WRLEN) {
		return EINVAL;
	}

	if ((rv = cmx_sync_write_SCR(sc, SCR_HOST_TO_READER_START)) != 0) {
		return rv;
	}

	sc->timeout = 0;
	while (uio->uio_resid > 0) {
		amnt = MIN(uio->uio_resid, sizeof(sc->buf));

		if ((rv = uiomove(sc->buf, amnt, uio)) != 0) {
			DEBUG_printf(sc->dev, "uiomove failed (%d)\n", rv);
			/* wildly guessed attempt to notify device */
			sc->timeout = CCID_DRIVER_MINIMUM_TIMEOUT;
			cmx_sync_write_SCR(sc, SCR_HOST_TO_READER_DONE);
			return rv;
		}

		if (sc->timeout == 0) {
			sc->timeout = cmx_timeout_by_cmd(sc->buf[0]);
			DEBUG_printf(sc->dev, "cmd=%02x timeout=%lu\n",
					sc->buf[0], sc->timeout);
		}

		for (int i = 0; i < amnt; i++) {
			if ((rv = cmx_wait_BSR(sc, BSR_BULK_OUT_FULL, 0))!=0) {
				return rv;
			}
			cmx_write_DTR(sc, sc->buf[i]);
			DEBUG_printf(sc->dev, "buf[%02x]=%02x\n",
					i, sc->buf[i]);
		}
	}

	if ((rv = cmx_sync_write_SCR(sc, SCR_HOST_TO_READER_DONE)) != 0) {
		return rv;
	}

	DEBUG_printf(sc->dev, "success\n");
	return 0;
}

/*
 * Poll handler.  Writing is always possible, reading is only possible
 * if BSR_BULK_IN_FULL is set.  Will start the cmx_tick callout and
 * set sc->polling.
 */
static int
cmx_poll(struct cdev *cdev, int events, struct thread *td)
{
	struct cmx_softc *sc = cdev->si_drv1;
	int revents = 0;
	uint8_t bsr = 0;

	if (sc == NULL || sc->dying)
		return ENXIO;

	bsr = cmx_read_BSR(sc);
	DEBUG_printf(sc->dev, "called (events=%b BSR=%b)\n",
			events, POLLBITS, bsr, BSRBITS);

	revents = events & (POLLOUT | POLLWRNORM);
	if (events & (POLLIN | POLLRDNORM)) {
		if (cmx_test(bsr, BSR_BULK_IN_FULL, 1)) {
			revents |= events & (POLLIN | POLLRDNORM);
		} else {
			selrecord(td, &sc->sel);
			CMX_LOCK(sc);
			if (!sc->polling) {
				DEBUG_printf(sc->dev, "enabling polling\n");
				sc->polling = 1;
				callout_reset(&sc->ch, POLL_TICKS,
						cmx_tick, sc);
			} else {
				DEBUG_printf(sc->dev, "already polling\n");
			}
			CMX_UNLOCK(sc);
		}
	}

	DEBUG_printf(sc->dev, "success (revents=%b)\n", revents, POLLBITS);

	return revents;
}

#ifdef CMX_INTR
/*
 * Interrupt handler.  Currently has no function except to
 * print register status (if debugging is also enabled).
 */
static void
cmx_intr(void *arg)
{
	struct cmx_softc *sc = (struct cmx_softc *)arg;

	if (sc == NULL || sc->dying)
		return;

	DEBUG_printf(sc->dev, "received interrupt (SCR=%b BSR=%b)\n",
			cmx_read_SCR(sc), SCRBITS,
			cmx_read_BSR(sc), BSRBITS);

	return;
}
#endif

