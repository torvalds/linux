/*
 * Copyright (c) 2014 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com> and was subsequently ported,
 * modified and enhanced for FreeBSD by Michael Gmelin <freebsd@grem.de>.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * CYAPA - Cypress APA trackpad with I2C Interface driver
 *
 * Based on DragonFlyBSD's cyapa driver, which referenced the linux
 * cyapa.c driver to figure out the bootstrapping and commands.
 *
 * Unable to locate any datasheet for the device.
 *
 *
 * Trackpad layout:
 *
 *                2/3               1/3
 *       +--------------------+------------+
 *       |                    |   Middle   |
 *       |                    |   Button   |
 *       |       Left         |            |
 *       |      Button        +------------+
 *       |                    |   Right    |
 *       |                    |   Button   |
 *       +--------------------+............|
 *       |     Thumb/Button Area           | 15%
 *       +---------------------------------+
 *
 *
 *                             FEATURES
 *
 * IMPS/2 emulation       - Emulates the IntelliMouse protocol.
 *
 * Jitter supression      - Implements 2-pixel hysteresis with memory.
 *
 * Jump detecion          - Detect jumps caused by touchpad.
 *
 * Two finger scrolling   - Use two fingers for Z axis scrolling.
 *
 * Button down/2nd finger - While one finger clicks and holds down the
 *                          touchpad, the second one can be used to move
 *                          the mouse cursor. Useful for drawing or
 *                          selecting text.
 *
 * Thumb/Button Area      - The lower 15%* of the trackpad will not affect
 *                          the mouse cursor position. This allows for high
 *                          precision clicking, by controlling the cursor
 *                          with the index finger and pushing/holding the
 *                          pad down with the thumb.
 *                          * can be changed using sysctl
 *
 * Track-pad button       - Push physical button. Left 2/3rds of the pad
 *                          will issue a LEFT button event, upper right
 *                          corner will issue a MIDDLE button event,
 *                          lower right corner will issue a RIGHT button
 *                          event. Optional tap support can be enabled
 *                          and configured using sysctl.
 *
 *                              WARNINGS
 *
 * These trackpads get confused when three or more fingers are down on the
 * same horizontal axis and will start to glitch the finger detection.
 * Removing your hand for a few seconds will allow the trackpad to
 * recalibrate.  Generally speaking, when using three or more fingers
 * please try to place at least one finger off-axis (a little above or
 * below) the other two.
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/event.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/lock.h>
#include <sys/lockmgr.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/mouse.h>
#include <sys/mutex.h>
#include <sys/poll.h>
#include <sys/selinfo.h>
#include <sys/sysctl.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/vnode.h>

#include <dev/iicbus/iiconf.h>
#include <dev/iicbus/iicbus.h>
#include <dev/cyapa/cyapa.h>

#include "iicbus_if.h"
#include "bus_if.h"
#include "device_if.h"

#define CYAPA_BUFSIZE	128			/* power of 2 */
#define CYAPA_BUFMASK	(CYAPA_BUFSIZE - 1)

#define ZSCALE		15

#define TIME_TO_IDLE	(hz * 10)
#define TIME_TO_RESET	(hz * 3)

static MALLOC_DEFINE(M_CYAPA, "cyapa", "CYAPA device data");

struct cyapa_fifo {
	int	rindex;
	int	windex;
	char	buf[CYAPA_BUFSIZE];
};

struct cyapa_softc {
	device_t dev;
	int	count;			/* >0 if device opened */
	struct cdev *devnode;
	struct selinfo selinfo;
	struct mtx mutex;

	int	cap_resx;
	int	cap_resy;
	int	cap_phyx;
	int	cap_phyy;
	uint8_t	cap_buttons;

	int	detaching;		/* driver is detaching */
	int	poll_thread_running;	/* poll thread is running */

	/* PS/2 mouse emulation */
	int	track_x;		/* current tracking */
	int	track_y;
	int	track_z;
	int	track_z_ticks;
	uint16_t track_but;
	char	track_id;		/* first finger id */
	int	track_nfingers;
	int	delta_x;		/* accumulation -> report */
	int	delta_y;
	int	delta_z;
	int	fuzz_x;
	int	fuzz_y;
	int	fuzz_z;
	int	touch_x;		/* touch down coordinates */
	int	touch_y;
	int	touch_z;
	int	finger1_ticks;
	int	finger2_ticks;
	int	finger3_ticks;
	uint16_t reported_but;

	struct cyapa_fifo rfifo;	/* device->host */
	struct cyapa_fifo wfifo;	/* host->device */
	uint8_t	ps2_cmd;		/* active p2_cmd waiting for data */
	uint8_t ps2_acked;
	int	active_tick;
	int	data_signal;
	int	blocked;
	int	isselect;
	int	reporting_mode;		/* 0=disabled 1=enabled */
	int	scaling_mode;		/* 0=1:1 1=2:1 */
	int	remote_mode;		/* 0 for streaming mode */
	int	zenabled;		/* z-axis enabled (mode 1 or 2) */
	mousehw_t hw;			/* hardware information */
	mousemode_t mode;		/* mode */
	int	poll_ticks;
};

struct cyapa_cdevpriv {
	struct cyapa_softc *sc;
};

#define CYPOLL_SHUTDOWN	0x0001

static void cyapa_poll_thread(void *arg);
static int cyapa_raw_input(struct cyapa_softc *sc, struct cyapa_regs *regs,
    int freq);
static void cyapa_set_power_mode(struct cyapa_softc *sc, int mode);

static int fifo_empty(struct cyapa_softc *sc, struct cyapa_fifo *fifo);
static size_t fifo_ready(struct cyapa_softc *sc, struct cyapa_fifo *fifo);
static char *fifo_read(struct cyapa_softc *sc, struct cyapa_fifo *fifo,
    size_t n);
static char *fifo_write(struct cyapa_softc *sc, struct cyapa_fifo *fifo,
    size_t n);
static uint8_t fifo_read_char(struct cyapa_softc *sc,
    struct cyapa_fifo *fifo);
static void fifo_write_char(struct cyapa_softc *sc, struct cyapa_fifo *fifo,
    uint8_t c);
static size_t fifo_space(struct cyapa_softc *sc, struct cyapa_fifo *fifo);
static void fifo_reset(struct cyapa_softc *sc, struct cyapa_fifo *fifo);

static int cyapa_fuzz(int delta, int *fuzz);

static int cyapa_idle_freq = 1;
SYSCTL_INT(_debug, OID_AUTO, cyapa_idle_freq, CTLFLAG_RW,
	    &cyapa_idle_freq, 0, "Scan frequency in idle mode");
static int cyapa_slow_freq = 20;
SYSCTL_INT(_debug, OID_AUTO, cyapa_slow_freq, CTLFLAG_RW,
	    &cyapa_slow_freq, 0, "Scan frequency in slow mode ");
static int cyapa_norm_freq = 100;
SYSCTL_INT(_debug, OID_AUTO, cyapa_norm_freq, CTLFLAG_RW,
	    &cyapa_norm_freq, 0, "Normal scan frequency");
static int cyapa_minpressure = 12;
SYSCTL_INT(_debug, OID_AUTO, cyapa_minpressure, CTLFLAG_RW,
	    &cyapa_minpressure, 0, "Minimum pressure to detect finger");
static int cyapa_enable_tapclick = 0;
SYSCTL_INT(_debug, OID_AUTO, cyapa_enable_tapclick, CTLFLAG_RW,
	    &cyapa_enable_tapclick, 0, "Enable tap to click");
static int cyapa_tapclick_min_ticks = 1;
SYSCTL_INT(_debug, OID_AUTO, cyapa_tapclick_min_ticks, CTLFLAG_RW,
	    &cyapa_tapclick_min_ticks, 0, "Minimum tap duration for click");
static int cyapa_tapclick_max_ticks = 8;
SYSCTL_INT(_debug, OID_AUTO, cyapa_tapclick_max_ticks, CTLFLAG_RW,
	    &cyapa_tapclick_max_ticks, 0, "Maximum tap duration for click");
static int cyapa_move_min_ticks = 4;
SYSCTL_INT(_debug, OID_AUTO, cyapa_move_min_ticks, CTLFLAG_RW,
	    &cyapa_move_min_ticks, 0,
	    "Minimum ticks before cursor position is changed");
static int cyapa_scroll_wait_ticks = 0;
SYSCTL_INT(_debug, OID_AUTO, cyapa_scroll_wait_ticks, CTLFLAG_RW,
	    &cyapa_scroll_wait_ticks, 0,
	    "Wait N ticks before starting to scroll");
static int cyapa_scroll_stick_ticks = 15;
SYSCTL_INT(_debug, OID_AUTO, cyapa_scroll_stick_ticks, CTLFLAG_RW,
	    &cyapa_scroll_stick_ticks, 0,
	    "Prevent cursor move on single finger for N ticks after scroll");
static int cyapa_thumbarea_percent = 15;
SYSCTL_INT(_debug, OID_AUTO, cyapa_thumbarea_percent, CTLFLAG_RW,
	    &cyapa_thumbarea_percent, 0,
	    "Size of bottom thumb area in percent");

static int cyapa_debug = 0;
SYSCTL_INT(_debug, OID_AUTO, cyapa_debug, CTLFLAG_RW,
	    &cyapa_debug, 0, "Enable debugging");
static int cyapa_reset = 0;
SYSCTL_INT(_debug, OID_AUTO, cyapa_reset, CTLFLAG_RW,
	    &cyapa_reset, 0, "Reset track pad");

static int
cyapa_read_bytes(device_t dev, uint8_t reg, uint8_t *val, int cnt)
{
	uint16_t addr = iicbus_get_addr(dev);
	struct iic_msg msgs[] = {
	     { addr, IIC_M_WR | IIC_M_NOSTOP, 1, &reg },
	     { addr, IIC_M_RD, cnt, val },
	};

	return (iicbus_transfer(dev, msgs, nitems(msgs)));
}

static int
cyapa_write_bytes(device_t dev, uint8_t reg, const uint8_t *val, int cnt)
{
	uint16_t addr = iicbus_get_addr(dev);
	struct iic_msg msgs[] = {
	     { addr, IIC_M_WR | IIC_M_NOSTOP, 1, &reg },
	     { addr, IIC_M_WR | IIC_M_NOSTART, cnt, __DECONST(uint8_t *, val) },
	};

	return (iicbus_transfer(dev, msgs, nitems(msgs)));
}

static void
cyapa_lock(struct cyapa_softc *sc)
{

	mtx_lock(&sc->mutex);
}

static void
cyapa_unlock(struct cyapa_softc *sc)
{

	mtx_unlock(&sc->mutex);
}

#define	CYAPA_LOCK_ASSERT(sc)	mtx_assert(&(sc)->mutex, MA_OWNED);

/*
 * Notify if possible receive data ready.  Must be called
 * with sc->mutex held (cyapa_lock(sc)).
 */
static void
cyapa_notify(struct cyapa_softc *sc)
{

	CYAPA_LOCK_ASSERT(sc);

	if (sc->data_signal || !fifo_empty(sc, &sc->rfifo)) {
		KNOTE_LOCKED(&sc->selinfo.si_note, 0);
		if (sc->blocked || sc->isselect) {
			if (sc->blocked) {
			    sc->blocked = 0;
			    wakeup(&sc->blocked);
			}
			if (sc->isselect) {
			    sc->isselect = 0;
			    selwakeup(&sc->selinfo);
			}
		}
	}
}

/*
 * Initialize the device
 */
static int
init_device(device_t dev, struct cyapa_cap *cap, int probe)
{
	static char bl_exit[] = {
		0x00, 0xff, 0xa5, 0x00, 0x01,
		0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };
	static char bl_deactivate[] = {
		0x00, 0xff, 0x3b, 0x00, 0x01,
		0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };
	struct cyapa_boot_regs boot;
	int error;
	int retries;

	/* Get status */
	error = cyapa_read_bytes(dev, CMD_BOOT_STATUS,
	    (void *)&boot, sizeof(boot));
	if (error)
		goto done;

	/*
	 * Bootstrap the device if necessary.  It can take up to 2 seconds
	 * for the device to fully initialize.
	 */
	retries = 20;
	while ((boot.stat & CYAPA_STAT_RUNNING) == 0 && retries > 0) {
		if (boot.boot & CYAPA_BOOT_BUSY) {
			/* Busy, wait loop. */
		} else if (boot.error & CYAPA_ERROR_BOOTLOADER) {
			/* Magic */
			error = cyapa_write_bytes(dev, CMD_BOOT_STATUS,
			    bl_deactivate, sizeof(bl_deactivate));
			if (error)
				goto done;
		} else {
			/* Magic */
			error = cyapa_write_bytes(dev, CMD_BOOT_STATUS,
			    bl_exit, sizeof(bl_exit));
			if (error)
				goto done;
		}
		pause("cyapab1", (hz * 2) / 10);
		--retries;
		error = cyapa_read_bytes(dev, CMD_BOOT_STATUS,
		    (void *)&boot, sizeof(boot));
		if (error)
			goto done;
	}

	if (retries == 0) {
		device_printf(dev, "Unable to bring device out of bootstrap\n");
		error = ENXIO;
		goto done;
	}

	/* Check identity */
	if (cap) {
		error = cyapa_read_bytes(dev, CMD_QUERY_CAPABILITIES,
		    (void *)cap, sizeof(*cap));

		if (strncmp(cap->prod_ida, "CYTRA", 5) != 0) {
			device_printf(dev, "Product ID \"%5.5s\" mismatch\n",
			    cap->prod_ida);
			error = ENXIO;
		}
	}
	error = cyapa_read_bytes(dev, CMD_BOOT_STATUS,
	    (void *)&boot, sizeof(boot));

	if (probe == 0)		/* official init */
		device_printf(dev, "cyapa init status %02x\n", boot.stat);
	else if (probe == 2)
		device_printf(dev, "cyapa reset status %02x\n", boot.stat);

done:
	if (error)
		device_printf(dev, "Unable to initialize\n");
	return (error);
}

static int cyapa_probe(device_t);
static int cyapa_attach(device_t);
static int cyapa_detach(device_t);
static void cyapa_cdevpriv_dtor(void*);

static devclass_t cyapa_devclass;

static device_method_t cyapa_methods[] = {
	/* device interface */
	DEVMETHOD(device_probe,		cyapa_probe),
	DEVMETHOD(device_attach,	cyapa_attach),
	DEVMETHOD(device_detach,	cyapa_detach),

	DEVMETHOD_END
};

static driver_t cyapa_driver = {
	"cyapa",
	cyapa_methods,
	sizeof(struct cyapa_softc),
};

static	d_open_t	cyapaopen;
static	d_ioctl_t	cyapaioctl;
static	d_read_t	cyaparead;
static	d_write_t	cyapawrite;
static	d_kqfilter_t	cyapakqfilter;
static	d_poll_t	cyapapoll;

static struct cdevsw cyapa_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	cyapaopen,
	.d_ioctl =	cyapaioctl,
	.d_read =	cyaparead,
	.d_write =	cyapawrite,
	.d_kqfilter =	cyapakqfilter,
	.d_poll =	cyapapoll,
};

static int
cyapa_probe(device_t dev)
{
	struct cyapa_cap cap;
	int addr;
	int error;

	addr = iicbus_get_addr(dev);

	/*
	 * 0x67 - cypress trackpad on the acer c720
	 * (other devices might use other ids).
	 */
	if (addr != 0xce)
		return (ENXIO);

	error = init_device(dev, &cap, 1);
	if (error != 0)
		return (ENXIO);

	device_set_desc(dev, "Cypress APA I2C Trackpad");

	return (BUS_PROBE_VENDOR);
}

static int
cyapa_attach(device_t dev)
{
	struct cyapa_softc *sc;
	struct cyapa_cap cap;
	int unit;
	int addr;

	sc = device_get_softc(dev);
	sc->reporting_mode = 1;

	unit = device_get_unit(dev);
	addr = iicbus_get_addr(dev);

	if (init_device(dev, &cap, 0))
		return (ENXIO);

	mtx_init(&sc->mutex, "cyapa", NULL, MTX_DEF);

	sc->dev = dev;

	knlist_init_mtx(&sc->selinfo.si_note, &sc->mutex);

	sc->cap_resx = ((cap.max_abs_xy_high << 4) & 0x0F00) |
	    cap.max_abs_x_low;
	sc->cap_resy = ((cap.max_abs_xy_high << 8) & 0x0F00) |
	    cap.max_abs_y_low;
	sc->cap_phyx = ((cap.phy_siz_xy_high << 4) & 0x0F00) |
	    cap.phy_siz_x_low;
	sc->cap_phyy = ((cap.phy_siz_xy_high << 8) & 0x0F00) |
	    cap.phy_siz_y_low;
	sc->cap_buttons = cap.buttons;

	device_printf(dev, "%5.5s-%6.6s-%2.2s buttons=%c%c%c res=%dx%d\n",
	    cap.prod_ida, cap.prod_idb, cap.prod_idc,
	    ((cap.buttons & CYAPA_FNGR_LEFT) ? 'L' : '-'),
	    ((cap.buttons & CYAPA_FNGR_MIDDLE) ? 'M' : '-'),
	    ((cap.buttons & CYAPA_FNGR_RIGHT) ? 'R' : '-'),
	    sc->cap_resx, sc->cap_resy);

	sc->hw.buttons = 5;
	sc->hw.iftype = MOUSE_IF_PS2;
	sc->hw.type = MOUSE_MOUSE;
	sc->hw.model = MOUSE_MODEL_INTELLI;
	sc->hw.hwid = addr;

	sc->mode.protocol = MOUSE_PROTO_PS2;
	sc->mode.rate = 100;
	sc->mode.resolution = 4;
	sc->mode.accelfactor = 1;
	sc->mode.level = 0;
	sc->mode.packetsize = MOUSE_PS2_PACKETSIZE;

	/* Setup input event tracking */
	cyapa_set_power_mode(sc, CMD_POWER_MODE_IDLE);

	/* Start the polling thread */
	 kthread_add(cyapa_poll_thread, sc, NULL, NULL,
	    0, 0, "cyapa-poll");

	sc->devnode = make_dev(&cyapa_cdevsw, unit,
	    UID_ROOT, GID_WHEEL, 0600, "cyapa%d", unit);

	sc->devnode->si_drv1 = sc;

	return (0);
}

static int
cyapa_detach(device_t dev)
{
	struct cyapa_softc *sc;

	sc = device_get_softc(dev);

	/* Cleanup poller thread */
	cyapa_lock(sc);
	while (sc->poll_thread_running) {
		sc->detaching = 1;
		mtx_sleep(&sc->detaching, &sc->mutex, PCATCH, "cyapadet", hz);
	}
	cyapa_unlock(sc);

	destroy_dev(sc->devnode);

	knlist_clear(&sc->selinfo.si_note, 0);
	seldrain(&sc->selinfo);
	knlist_destroy(&sc->selinfo.si_note);

	mtx_destroy(&sc->mutex);

	return (0);
}

/*
 * USER DEVICE I/O FUNCTIONS
 */
static int
cyapaopen(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	struct cyapa_cdevpriv *priv;
	int error;

	priv = malloc(sizeof(*priv), M_CYAPA, M_WAITOK | M_ZERO);
	priv->sc = dev->si_drv1;

	error = devfs_set_cdevpriv(priv, cyapa_cdevpriv_dtor);
	if (error == 0) {
		cyapa_lock(priv->sc);
		priv->sc->count++;
		cyapa_unlock(priv->sc);
	}
	else
		free(priv, M_CYAPA);

	return (error);
}

static void
cyapa_cdevpriv_dtor(void *data)
{
	struct cyapa_cdevpriv *priv;

	priv = data;
	KASSERT(priv != NULL, ("cyapa cdevpriv should not be NULL!"));

	cyapa_lock(priv->sc);
	priv->sc->count--;
	cyapa_unlock(priv->sc);

	free(priv, M_CYAPA);
}

static int
cyaparead(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct cyapa_softc *sc;
	int error;
	int didread;
	size_t n;
	char* ptr;

	sc = dev->si_drv1;
	/* If buffer is empty, load a new event if it is ready */
	cyapa_lock(sc);
again:
	if (fifo_empty(sc, &sc->rfifo) &&
	    (sc->data_signal || sc->delta_x || sc->delta_y ||
	     sc->track_but != sc->reported_but)) {
		uint8_t c0;
		uint16_t but;
		int delta_x;
		int delta_y;
		int delta_z;

		/* Accumulate delta_x, delta_y */
		sc->data_signal = 0;
		delta_x = sc->delta_x;
		delta_y = sc->delta_y;
		delta_z = sc->delta_z;
		if (delta_x > 255) {
			delta_x = 255;
			sc->data_signal = 1;
		}
		if (delta_x < -256) {
			delta_x = -256;
			sc->data_signal = 1;
		}
		if (delta_y > 255) {
			delta_y = 255;
			sc->data_signal = 1;
		}
		if (delta_y < -256) {
			delta_y = -256;
			sc->data_signal = 1;
		}
		if (delta_z > 255) {
			delta_z = 255;
			sc->data_signal = 1;
		}
		if (delta_z < -256) {
			delta_z = -256;
			sc->data_signal = 1;
		}
		but = sc->track_but;

		/* Adjust baseline for next calculation */
		sc->delta_x -= delta_x;
		sc->delta_y -= delta_y;
		sc->delta_z -= delta_z;
		sc->reported_but = but;

		/*
		 * Fuzz reduces movement jitter by introducing some
		 * hysteresis.  It operates without cumulative error so
		 * if you swish around quickly and return your finger to
		 * where it started, so to will the mouse.
		 */
		delta_x = cyapa_fuzz(delta_x, &sc->fuzz_x);
		delta_y = cyapa_fuzz(delta_y, &sc->fuzz_y);
		delta_z = cyapa_fuzz(delta_z, &sc->fuzz_z);

		/*
		 * Generate report
		 */
		c0 = 0;
		if (delta_x < 0)
			c0 |= 0x10;
		if (delta_y < 0)
			c0 |= 0x20;
		c0 |= 0x08;
		if (but & CYAPA_FNGR_LEFT)
			c0 |= 0x01;
		if (but & CYAPA_FNGR_MIDDLE)
			c0 |= 0x04;
		if (but & CYAPA_FNGR_RIGHT)
			c0 |= 0x02;

		fifo_write_char(sc, &sc->rfifo, c0);
		fifo_write_char(sc, &sc->rfifo, (uint8_t)delta_x);
		fifo_write_char(sc, &sc->rfifo, (uint8_t)delta_y);
		switch(sc->zenabled) {
		case 1:
			/* Z axis all 8 bits */
			fifo_write_char(sc, &sc->rfifo, (uint8_t)delta_z);
			break;
		case 2:
			/*
			 * Z axis low 4 bits + 4th button and 5th button
			 * (high 2 bits must be left 0).  Auto-scale
			 * delta_z to fit to avoid a wrong-direction
			 * overflow (don't try to retain the remainder).
			 */
			while (delta_z > 7 || delta_z < -8)
				delta_z >>= 1;
			c0 = (uint8_t)delta_z & 0x0F;
			fifo_write_char(sc, &sc->rfifo, c0);
			break;
		default:
			/* basic PS/2 */
			break;
		}
		cyapa_notify(sc);
	}

	/* Blocking / Non-blocking */
	error = 0;
	didread = (uio->uio_resid == 0);

	while ((ioflag & IO_NDELAY) == 0 && fifo_empty(sc, &sc->rfifo)) {
		if (sc->data_signal)
			goto again;
		sc->blocked = 1;
		error = mtx_sleep(&sc->blocked, &sc->mutex, PCATCH, "cyablk", 0);
		if (error)
			break;
	}

	/* Return any buffered data */
	while (error == 0 && uio->uio_resid &&
	    (n = fifo_ready(sc, &sc->rfifo)) > 0) {
		if (n > uio->uio_resid)
			n = uio->uio_resid;
		ptr = fifo_read(sc, &sc->rfifo, 0);
		cyapa_unlock(sc);
		error = uiomove(ptr, n, uio);
		cyapa_lock(sc);
		if (error)
			break;
		fifo_read(sc, &sc->rfifo, n);
		didread = 1;
	}
	cyapa_unlock(sc);

	if (error == 0 && didread == 0) {
		error = EWOULDBLOCK;
	}
	return (didread ? 0 : error);
}

static int
cyapawrite(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct cyapa_softc *sc;
	int error;
	int cmd_completed;
	size_t n;
	uint8_t c0;
	char* ptr;

	sc = dev->si_drv1;
again:
	/*
	 * Copy data from userland.  This will also cross-over the end
	 * of the fifo and keep filling.
	 */
	cyapa_lock(sc);
	while ((n = fifo_space(sc, &sc->wfifo)) > 0 && uio->uio_resid) {
		if (n > uio->uio_resid)
			n = uio->uio_resid;
		ptr = fifo_write(sc, &sc->wfifo, 0);
		cyapa_unlock(sc);
		error = uiomove(ptr, n, uio);
		cyapa_lock(sc);
		if (error)
			break;
		fifo_write(sc, &sc->wfifo, n);
	}

	/* Handle commands */
	cmd_completed = (fifo_ready(sc, &sc->wfifo) != 0);
	while (fifo_ready(sc, &sc->wfifo) && cmd_completed && error == 0) {
		if (sc->ps2_cmd == 0)
			sc->ps2_cmd = fifo_read_char(sc, &sc->wfifo);
		switch(sc->ps2_cmd) {
		case 0xE6:
			/* SET SCALING 1:1 */
			sc->scaling_mode = 0;
			fifo_write_char(sc, &sc->rfifo, 0xFA);
			break;
		case 0xE7:
			/* SET SCALING 2:1 */
			sc->scaling_mode = 1;
			fifo_write_char(sc, &sc->rfifo, 0xFA);
			break;
		case 0xE8:
			/* SET RESOLUTION +1 byte */
			if (sc->ps2_acked == 0) {
				sc->ps2_acked = 1;
				fifo_write_char(sc, &sc->rfifo, 0xFA);
			}
			if (fifo_ready(sc, &sc->wfifo) == 0) {
				cmd_completed = 0;
				break;
			}
			sc->mode.resolution = fifo_read_char(sc, &sc->wfifo);
			fifo_write_char(sc, &sc->rfifo, 0xFA);
			break;
		case 0xE9:
			/*
			 * STATUS REQUEST
			 *
			 * byte1:
			 *	bit 7	0
			 *	bit 6	Mode	(1=remote mode, 0=stream mode)
			 *	bit 5	Enable	(data reporting enabled)
			 *	bit 4	Scaling	(0=1:1 1=2:1)
			 *	bit 3	0
			 *	bit 2	LEFT BUTTON    (1 if pressed)
			 *	bit 1	MIDDLE BUTTON  (1 if pressed)
			 *	bit 0	RIGHT BUTTON   (1 if pressed)
			 *
			 * byte2: resolution counts/mm
			 * byte3: sample rate
			 */
			c0 = 0;
			if (sc->remote_mode)
				c0 |= 0x40;
			if (sc->reporting_mode)
				c0 |= 0x20;
			if (sc->scaling_mode)
				c0 |= 0x10;
			if (sc->track_but & CYAPA_FNGR_LEFT)
				c0 |= 0x04;
			if (sc->track_but & CYAPA_FNGR_MIDDLE)
				c0 |= 0x02;
			if (sc->track_but & CYAPA_FNGR_RIGHT)
				c0 |= 0x01;
			fifo_write_char(sc, &sc->rfifo, 0xFA);
			fifo_write_char(sc, &sc->rfifo, c0);
			fifo_write_char(sc, &sc->rfifo, 0x00);
			fifo_write_char(sc, &sc->rfifo, 100);
			break;
		case 0xEA:
			/* Set stream mode and reset movement counters */
			sc->remote_mode = 0;
			fifo_write_char(sc, &sc->rfifo, 0xFA);
			sc->delta_x = 0;
			sc->delta_y = 0;
			sc->delta_z = 0;
			break;
		case 0xEB:
			/*
			 * Read Data (if in remote mode).  If not in remote
			 * mode force an event.
			 */
			fifo_write_char(sc, &sc->rfifo, 0xFA);
			sc->data_signal = 1;
			break;
		case 0xEC:
			/* Reset Wrap Mode (ignored) */
			fifo_write_char(sc, &sc->rfifo, 0xFA);
			break;
		case 0xEE:
			/* Set Wrap Mode (ignored) */
			fifo_write_char(sc, &sc->rfifo, 0xFA);
			break;
		case 0xF0:
			/* Set Remote Mode */
			sc->remote_mode = 1;
			fifo_write_char(sc, &sc->rfifo, 0xFA);
			sc->delta_x = 0;
			sc->delta_y = 0;
			sc->delta_z = 0;
			break;
		case 0xF2:
			/*
			 * Get Device ID
			 *
			 * If we send 0x00 - normal PS/2 mouse, no Z-axis
			 *
			 * If we send 0x03 - Intellimouse, data packet has
			 * an additional Z movement byte (8 bits signed).
			 * (also reset movement counters)
			 *
			 * If we send 0x04 - Now includes z-axis and the
			 * 4th and 5th mouse buttons.
			 */
			fifo_write_char(sc, &sc->rfifo, 0xFA);
			switch(sc->zenabled) {
			case 1:
				fifo_write_char(sc, &sc->rfifo, 0x03);
				break;
			case 2:
				fifo_write_char(sc, &sc->rfifo, 0x04);
				break;
			default:
				fifo_write_char(sc, &sc->rfifo, 0x00);
				break;
			}
			sc->delta_x = 0;
			sc->delta_y = 0;
			sc->delta_z = 0;
			break;
		case 0xF3:
			/*
			 * Set Sample Rate
			 *
			 * byte1: the sample rate
			 */
			if (sc->ps2_acked == 0) {
				sc->ps2_acked = 1;
				fifo_write_char(sc, &sc->rfifo, 0xFA);
			}
			if (fifo_ready(sc, &sc->wfifo) == 0) {
				cmd_completed = 0;
				break;
			}
			sc->mode.rate = fifo_read_char(sc, &sc->wfifo);
			fifo_write_char(sc, &sc->rfifo, 0xFA);

			/*
			 * zenabling sequence: 200,100,80 (device id 0x03)
			 *		       200,200,80 (device id 0x04)
			 *
			 * We support id 0x03 (no 4th or 5th button).
			 * We support id 0x04 (w/ 4th and 5th button).
			 */
			if (sc->zenabled == 0 && sc->mode.rate == 200)
				sc->zenabled = -1;
			else if (sc->zenabled == -1 && sc->mode.rate == 100)
				sc->zenabled = -2;
			else if (sc->zenabled == -1 && sc->mode.rate == 200)
				sc->zenabled = -3;
			else if (sc->zenabled == -2 && sc->mode.rate == 80)
				sc->zenabled = 1;	/* z-axis mode */
			else if (sc->zenabled == -3 && sc->mode.rate == 80)
				sc->zenabled = 2;	/* z-axis+but4/5 */
			if (sc->mode.level)
				sc->zenabled = 1;
			break;
		case 0xF4:
			/* Enable data reporting.  Only effects stream mode. */
			fifo_write_char(sc, &sc->rfifo, 0xFA);
			sc->reporting_mode = 1;
			break;
		case 0xF5:
			/*
			 * Disable data reporting.  Only effects stream mode
			 * and is ignored right now.
			 */
			fifo_write_char(sc, &sc->rfifo, 0xFA);
			sc->reporting_mode = 1;
			break;
		case 0xF6:
			/*
			 * SET DEFAULTS
			 *
			 * (reset sampling rate, resolution, scaling and
			 *  enter stream mode)
			 */
			fifo_write_char(sc, &sc->rfifo, 0xFA);
			sc->mode.rate = 100;
			sc->mode.resolution = 4;
			sc->scaling_mode = 0;
			sc->reporting_mode = 1;
			sc->remote_mode = 0;
			sc->delta_x = 0;
			sc->delta_y = 0;
			sc->delta_z = 0;
			/* signal */
			break;
		case 0xFE:
			/*
			 * RESEND
			 *
			 * Force a resend by guaranteeing that reported_but
			 * differs from track_but.
			 */
			fifo_write_char(sc, &sc->rfifo, 0xFA);
			sc->data_signal = 1;
			break;
		case 0xFF:
			/*
			 * RESET
			 */
			fifo_reset(sc, &sc->rfifo);	/* should we do this? */
			fifo_reset(sc, &sc->wfifo);	/* should we do this? */
			fifo_write_char(sc, &sc->rfifo, 0xFA);
			sc->delta_x = 0;
			sc->delta_y = 0;
			sc->delta_z = 0;
			sc->zenabled = 0;
			sc->mode.level = 0;
			break;
		default:
			printf("unknown command %02x\n", sc->ps2_cmd);
			break;
		}
		if (cmd_completed) {
			sc->ps2_cmd = 0;
			sc->ps2_acked = 0;
		}
		cyapa_notify(sc);
	}
	cyapa_unlock(sc);
	if (error == 0 && (cmd_completed || uio->uio_resid))
		goto again;
	return (error);
}

static void cyapafiltdetach(struct knote *);
static int cyapafilt(struct knote *, long);

static struct filterops cyapa_filtops = {
	    .f_isfd = 1,
	    .f_detach = cyapafiltdetach,
	    .f_event = cyapafilt
};

static int
cyapakqfilter(struct cdev *dev, struct knote *kn)
{
	struct cyapa_softc *sc;
	struct knlist *knlist;

	sc = dev->si_drv1;

	switch(kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &cyapa_filtops;
		kn->kn_hook = (void *)sc;
		break;
	default:
		return (EOPNOTSUPP);
	}
	knlist = &sc->selinfo.si_note;
	knlist_add(knlist, kn, 0);

	return (0);
}

static int
cyapapoll(struct cdev *dev, int events, struct thread *td)
{
	struct cyapa_softc *sc;
	int revents;

	sc = dev->si_drv1;
	revents = 0;

	cyapa_lock(sc);
	if (events & (POLLIN | POLLRDNORM)) {
		if (sc->data_signal || !fifo_empty(sc, &sc->rfifo))
			revents = events & (POLLIN | POLLRDNORM);
		else {
			sc->isselect = 1;
			selrecord(td, &sc->selinfo);
		}
	}
	cyapa_unlock(sc);

	return (revents);
}

static void
cyapafiltdetach(struct knote *kn)
{
	struct cyapa_softc *sc;
	struct knlist *knlist;

	sc = (struct cyapa_softc *)kn->kn_hook;

	knlist = &sc->selinfo.si_note;
	knlist_remove(knlist, kn, 0);
}

static int
cyapafilt(struct knote *kn, long hint)
{
	struct cyapa_softc *sc;
	int ready;

	sc = (struct cyapa_softc *)kn->kn_hook;

	cyapa_lock(sc);
	ready = fifo_ready(sc, &sc->rfifo) || sc->data_signal;
	cyapa_unlock(sc);

	return (ready);
}

static int
cyapaioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag, struct thread *td)
{
	struct cyapa_softc *sc;
	int error;

	sc = dev->si_drv1;
	error = 0;

	cyapa_lock(sc);
	switch (cmd) {
	case MOUSE_GETHWINFO:
		*(mousehw_t *)data = sc->hw;
		if (sc->mode.level == 0)
			((mousehw_t *)data)->model = MOUSE_MODEL_GENERIC;
		break;

	case MOUSE_GETMODE:
		*(mousemode_t *)data = sc->mode;
		((mousemode_t *)data)->resolution =
		    MOUSE_RES_LOW - sc->mode.resolution;
		switch (sc->mode.level) {
		case 0:
			((mousemode_t *)data)->protocol = MOUSE_PROTO_PS2;
			((mousemode_t *)data)->packetsize =
			    MOUSE_PS2_PACKETSIZE;
			break;
		case 2:
			((mousemode_t *)data)->protocol = MOUSE_PROTO_PS2;
			((mousemode_t *)data)->packetsize =
			    MOUSE_PS2_PACKETSIZE + 1;
			break;
		}
		break;

	case MOUSE_GETLEVEL:
		*(int *)data = sc->mode.level;
		break;

	case MOUSE_SETLEVEL:
		if ((*(int *)data < 0) &&
		    (*(int *)data > 2)) {
			error = EINVAL;
			break;
		}
		sc->mode.level = *(int *)data ? 2 : 0;
		sc->zenabled = sc->mode.level ? 1 : 0;
		break;

	default:
		error = ENOTTY;
		break;
	}
	cyapa_unlock(sc);

	return (error);
}

/*
 * MAJOR SUPPORT FUNCTIONS
 */
static void
cyapa_poll_thread(void *arg)
{
	struct cyapa_softc *sc;
	struct cyapa_regs regs;
	device_t bus;		/* iicbus */
	int error;
	int freq;
	int isidle;
	int pstate;
	int npstate;
	int last_reset;

	sc = arg;
	freq = cyapa_norm_freq;
	isidle = 0;
	pstate = CMD_POWER_MODE_IDLE;
	last_reset = ticks;

	bus = device_get_parent(sc->dev);

	cyapa_lock(sc);
	sc->poll_thread_running = 1;

	while (!sc->detaching) {
		cyapa_unlock(sc);
		error = iicbus_request_bus(bus, sc->dev, IIC_WAIT);
		if (error == 0) {
			error = cyapa_read_bytes(sc->dev, CMD_DEV_STATUS,
			    (void *)&regs, sizeof(regs));
			if (error == 0) {
				isidle = cyapa_raw_input(sc, &regs, freq);
			}

			/*
			 * For some reason the device can crap-out.  If it
			 * drops back into bootstrap mode try to reinitialize
			 * it.
			 */
			if (cyapa_reset ||
			    ((regs.stat & CYAPA_STAT_RUNNING) == 0 &&
			     (unsigned)(ticks - last_reset) > TIME_TO_RESET)) {
				cyapa_reset = 0;
				last_reset = ticks;
				init_device(sc->dev, NULL, 2);
			}
			iicbus_release_bus(bus, sc->dev);
		}
		pause("cyapw", hz / freq);
		++sc->poll_ticks;

		if (sc->count == 0) {
			freq = cyapa_idle_freq;
			npstate = CMD_POWER_MODE_IDLE;
		} else if (isidle) {
			freq = cyapa_slow_freq;
			npstate = CMD_POWER_MODE_IDLE;
		} else {
			freq = cyapa_norm_freq;
			npstate = CMD_POWER_MODE_FULL;
		}
		if (pstate != npstate) {
			pstate = npstate;
			cyapa_set_power_mode(sc, pstate);
			if (cyapa_debug) {
				switch(pstate) {
				case CMD_POWER_MODE_OFF:
					printf("cyapa: power off\n");
					break;
				case CMD_POWER_MODE_IDLE:
					printf("cyapa: power idle\n");
					break;
				case CMD_POWER_MODE_FULL:
					printf("cyapa: power full\n");
					break;
				}
			}
		}

		cyapa_lock(sc);
	}
	sc->poll_thread_running = 0;
	cyapa_unlock(sc);
	kthread_exit();
}

static int
cyapa_raw_input(struct cyapa_softc *sc, struct cyapa_regs *regs, int freq)
{
	int nfingers;
	int afingers;	/* actual fingers after culling */
	int i;
	int j;
	int k;
	int isidle;
	int thumbarea_begin;
	int seen_thumb;
	int x;
	int y;
	int z;
	int newfinger;
	int lessfingers;
	int click_x;
	int click_y;
	uint16_t but;	/* high bits used for simulated but4/but5 */

	thumbarea_begin = sc->cap_resy -
	    ((sc->cap_resy *  cyapa_thumbarea_percent) / 100);
	click_x = click_y = 0;

	/*
	 * If the device is not running the rest of the status
	 * means something else, set fingers to 0.
	 */
	if ((regs->stat & CYAPA_STAT_RUNNING) == 0) {
		regs->fngr = 0;
	}

	/* Process fingers/movement */
	nfingers = CYAPA_FNGR_NUMFINGERS(regs->fngr);
	afingers = nfingers;

	if (cyapa_debug) {
		printf("stat %02x buttons %c%c%c nfngrs=%d ",
		    regs->stat,
		    ((regs->fngr & CYAPA_FNGR_LEFT) ? 'L' : '-'),
		    ((regs->fngr & CYAPA_FNGR_MIDDLE) ? 'M' : '-'),
		    ((regs->fngr & CYAPA_FNGR_RIGHT) ? 'R' : '-'),
		    nfingers);
	}

	seen_thumb = 0;
	for (i = 0; i < afingers; ) {
		if (cyapa_debug) {
			printf(" [x=%04d y=%04d p=%d i=%d]",
			    CYAPA_TOUCH_X(regs, i),
			    CYAPA_TOUCH_Y(regs, i),
			    CYAPA_TOUCH_P(regs, i),
			    regs->touch[i].id);
		}
		if ((CYAPA_TOUCH_Y(regs, i) > thumbarea_begin && seen_thumb) ||
		     CYAPA_TOUCH_P(regs, i) < cyapa_minpressure) {
			--afingers;
			if (i < afingers) {
			    regs->touch[i] = regs->touch[i+1];
			    continue;
			}
		} else {
			if (CYAPA_TOUCH_Y(regs, i) > thumbarea_begin)
			    seen_thumb = 1;
		}
		++i;
	}
	nfingers = afingers;

	/* Tracking for local solutions */
	cyapa_lock(sc);

	/*
	 * Track timing for finger-downs.  Used to detect false-3-finger
	 * button-down.
	 */
	switch(afingers) {
	case 0:
		break;
	case 1:
		if (sc->track_nfingers == 0)
			sc->finger1_ticks = sc->poll_ticks;
		break;
	case 2:
		if (sc->track_nfingers <= 0)
			sc->finger1_ticks = sc->poll_ticks;
		if (sc->track_nfingers <= 1)
			sc->finger2_ticks = sc->poll_ticks;
		break;
	case 3:
	default:
		if (sc->track_nfingers <= 0)
			sc->finger1_ticks = sc->poll_ticks;
		if (sc->track_nfingers <= 1)
			sc->finger2_ticks = sc->poll_ticks;
		if (sc->track_nfingers <= 2)
			sc->finger3_ticks = sc->poll_ticks;
		break;
	}
	newfinger = sc->track_nfingers < afingers;
	lessfingers = sc->track_nfingers > afingers;
	sc->track_nfingers = afingers;

	/*
	 * Lookup and track finger indexes in the touch[] array.
	 */
	if (afingers == 0) {
		click_x = sc->track_x;
		click_y = sc->track_y;
		sc->track_x = -1;
		sc->track_y = -1;
		sc->track_z = -1;
		sc->fuzz_x = 0;
		sc->fuzz_y = 0;
		sc->fuzz_z = 0;
		sc->touch_x = -1;
		sc->touch_y = -1;
		sc->touch_z = -1;
		sc->track_id = -1;
		sc->track_but = 0;
		i = 0;
		j = 0;
		k = 0;
	} else {
		/*
		 * The id assigned on touch can move around in the array,
		 * find it.  If that finger is lifted up, assign some other
		 * finger for mouse tracking and reset track_x and track_y
		 * to avoid a mouse jump.
		 *
		 * If >= 2 fingers are down be sure not to assign i and
		 * j to the same index.
		 */
		for (i = 0; i < nfingers; ++i) {
			if (sc->track_id == regs->touch[i].id)
				break;
		}
		if (i == nfingers) {
			i = 0;
			sc->track_x = -1;
			sc->track_y = -1;
			sc->track_z = -1;
			while (CYAPA_TOUCH_Y(regs, i) >= thumbarea_begin &&
			    i < nfingers) ++i;
			if (i == nfingers) {
				i = 0;
			}
			sc->track_id = regs->touch[i].id;
		}
		else if ((sc->track_but ||
		     CYAPA_TOUCH_Y(regs, i) >= thumbarea_begin) &&
		    newfinger && afingers == 2) {
			j = regs->touch[0].id == sc->track_id ? 1 : 0;
			if (CYAPA_TOUCH_Y(regs, j) < thumbarea_begin) {
			    i = j;
			    sc->track_x = -1;
			    sc->track_y = -1;
			    sc->track_z = -1;
			    sc->track_id = regs->touch[i].id;
			}
		}
	}

	/* Two finger scrolling - reset after timeout */
	if (sc->track_z != -1 && afingers != 2 &&
	    (sc->poll_ticks - sc->track_z_ticks) > cyapa_scroll_stick_ticks) {
		sc->track_z = -1;
		sc->track_z_ticks = 0;
	}

	/* Initiate two finger scrolling */
	if (!(regs->fngr & CYAPA_FNGR_LEFT) &&
	    ((afingers && sc->track_z != -1) ||
	     (afingers == 2 && CYAPA_TOUCH_Y(regs, 0) < thumbarea_begin &&
	     CYAPA_TOUCH_Y(regs, 1) < thumbarea_begin))) {
		if (afingers == 2 && (sc->poll_ticks - sc->finger2_ticks)
		    > cyapa_scroll_wait_ticks) {
			z = (CYAPA_TOUCH_Y(regs, 0) +
			    CYAPA_TOUCH_Y(regs, 1)) >> 1;
			sc->delta_z += z / ZSCALE - sc->track_z;
			if (sc->track_z == -1) {
			    sc->delta_z = 0;
			}
			if (sc->touch_z == -1)
			    sc->touch_z = z;	/* not used atm */
			sc->track_z = z / ZSCALE;
			sc->track_z_ticks = sc->poll_ticks;
		}
	} else if (afingers) {
		/* Normal pad position reporting */
		x = CYAPA_TOUCH_X(regs, i);
		y = CYAPA_TOUCH_Y(regs, i);
		click_x = x;
		click_y = y;
		if (sc->track_x != -1 && sc->track_y < thumbarea_begin &&
		    (afingers > 1 || (sc->poll_ticks - sc->finger1_ticks)
		    >= cyapa_move_min_ticks || freq < cyapa_norm_freq)) {
			sc->delta_x += x - sc->track_x;
			sc->delta_y -= y - sc->track_y;
			if (sc->delta_x > sc->cap_resx)
				sc->delta_x = sc->cap_resx;
			if (sc->delta_x < -sc->cap_resx)
				sc->delta_x = -sc->cap_resx;
			if (sc->delta_y > sc->cap_resy)
				sc->delta_y = sc->cap_resy;
			if (sc->delta_y < -sc->cap_resy)
				sc->delta_y = -sc->cap_resy;

			if (abs(sc->delta_y) > sc->cap_resy / 2 ||
			    abs(sc->delta_x) > sc->cap_resx / 2) {
				if (cyapa_debug)
					printf("Detected jump by %i %i\n",
					    sc->delta_x, sc->delta_y);
			    sc->delta_x = sc->delta_y = 0;
			}
		}
		if (sc->touch_x == -1) {
			sc->touch_x = x;
			sc->touch_y = y;
		}
		sc->track_x = x;
		sc->track_y = y;
	}

	/* Select finger (L = 2/3x, M = 1/3u, R = 1/3d) */
	int is_tapclick = (cyapa_enable_tapclick && lessfingers &&
	    afingers == 0 && sc->poll_ticks - sc->finger1_ticks
	    >= cyapa_tapclick_min_ticks &&
	    sc->poll_ticks - sc->finger1_ticks < cyapa_tapclick_max_ticks);

	if (regs->fngr & CYAPA_FNGR_LEFT || is_tapclick) {
		if (sc->track_but) {
			but = sc->track_but;
		} else if (afingers == 1) {
			if (click_x < sc->cap_resx * 2 / 3)
				but = CYAPA_FNGR_LEFT;
			else if (click_y < sc->cap_resy / 2)
				but = CYAPA_FNGR_MIDDLE;
			else
				but = CYAPA_FNGR_RIGHT;
		} else if (is_tapclick) {
			if (click_x < sc->cap_resx * 2 / 3 ||
			    cyapa_enable_tapclick < 2)
				but = CYAPA_FNGR_LEFT;
			else if (click_y < sc->cap_resy / 2 &&
			    cyapa_enable_tapclick > 2)
				but = CYAPA_FNGR_MIDDLE;
			else
				but = CYAPA_FNGR_RIGHT;
		} else {
			but = CYAPA_FNGR_LEFT;
		}
	} else {
		but = 0;
	}

	/*
	 * Detect state change from last reported state and
	 * determine if we have gone idle.
	 */
	sc->track_but = but;
	if (sc->delta_x || sc->delta_y || sc->delta_z ||
	    sc->track_but != sc->reported_but) {
		sc->active_tick = ticks;
		if (sc->remote_mode == 0 && sc->reporting_mode)
			sc->data_signal = 1;
		isidle = 0;
	} else if ((unsigned)(ticks - sc->active_tick) >= TIME_TO_IDLE) {
		sc->active_tick = ticks - TIME_TO_IDLE; /* prevent overflow */
		isidle = 1;
	} else {
		isidle = 0;
	}
	cyapa_notify(sc);
	cyapa_unlock(sc);

	if (cyapa_debug)
		printf("%i >> %i << %i\n", isidle, sc->track_id, sc->delta_y);
	return (isidle);
}

static void
cyapa_set_power_mode(struct cyapa_softc *sc, int mode)
{
	uint8_t data;
	device_t bus;
	int error;

	bus = device_get_parent(sc->dev);
	error = iicbus_request_bus(bus, sc->dev, IIC_WAIT);
	if (error == 0) {
		error = cyapa_read_bytes(sc->dev, CMD_POWER_MODE,
		    &data, 1);
		data = (data & ~0xFC) | mode;
		if (error == 0) {
			error = cyapa_write_bytes(sc->dev, CMD_POWER_MODE,
			    &data, 1);
		}
		iicbus_release_bus(bus, sc->dev);
	}
}

/*
 * FIFO FUNCTIONS
 */

/*
 * Returns non-zero if the fifo is empty
 */
static int
fifo_empty(struct cyapa_softc *sc, struct cyapa_fifo *fifo)
{

	CYAPA_LOCK_ASSERT(sc);

	return (fifo->rindex == fifo->windex);
}

/*
 * Returns the number of characters available for reading from
 * the fifo without wrapping the fifo buffer.
 */
static size_t
fifo_ready(struct cyapa_softc *sc, struct cyapa_fifo *fifo)
{
	size_t n;

	CYAPA_LOCK_ASSERT(sc);

	n = CYAPA_BUFSIZE - (fifo->rindex & CYAPA_BUFMASK);
	if (n > (size_t)(fifo->windex - fifo->rindex))
		n = (size_t)(fifo->windex - fifo->rindex);
	return (n);
}

/*
 * Returns a read pointer into the fifo and then bumps
 * rindex.  The FIFO must have at least 'n' characters in
 * it.  The value (n) can cause the index to wrap but users
 * of the buffer should never supply a value for (n) that wraps
 * the buffer.
 */
static char *
fifo_read(struct cyapa_softc *sc, struct cyapa_fifo *fifo, size_t n)
{
	char *ptr;

	CYAPA_LOCK_ASSERT(sc);
	if (n > (CYAPA_BUFSIZE - (fifo->rindex & CYAPA_BUFMASK))) {
		printf("fifo_read: overflow\n");
		return (fifo->buf);
	}
	ptr = fifo->buf + (fifo->rindex & CYAPA_BUFMASK);
	fifo->rindex += n;

	return (ptr);
}

static uint8_t
fifo_read_char(struct cyapa_softc *sc, struct cyapa_fifo *fifo)
{
	uint8_t c;

	CYAPA_LOCK_ASSERT(sc);

	if (fifo->rindex == fifo->windex) {
		printf("fifo_read_char: overflow\n");
		c = 0;
	} else {
		c = fifo->buf[fifo->rindex & CYAPA_BUFMASK];
		++fifo->rindex;
	}
	return (c);
}


/*
 * Write a character to the FIFO.  The character will be discarded
 * if the FIFO is full.
 */
static void
fifo_write_char(struct cyapa_softc *sc, struct cyapa_fifo *fifo, uint8_t c)
{

	CYAPA_LOCK_ASSERT(sc);

	if (fifo->windex - fifo->rindex < CYAPA_BUFSIZE) {
		fifo->buf[fifo->windex & CYAPA_BUFMASK] = c;
		++fifo->windex;
	}
}

/*
 * Return the amount of space available for writing without wrapping
 * the fifo.
 */
static size_t
fifo_space(struct cyapa_softc *sc, struct cyapa_fifo *fifo)
{
	size_t n;

	CYAPA_LOCK_ASSERT(sc);

	n = CYAPA_BUFSIZE - (fifo->windex & CYAPA_BUFMASK);
	if (n > (size_t)(CYAPA_BUFSIZE - (fifo->windex - fifo->rindex)))
		n = (size_t)(CYAPA_BUFSIZE - (fifo->windex - fifo->rindex));
	return (n);
}

static char *
fifo_write(struct cyapa_softc *sc, struct cyapa_fifo *fifo, size_t n)
{
	char *ptr;

	CYAPA_LOCK_ASSERT(sc);

	ptr = fifo->buf + (fifo->windex & CYAPA_BUFMASK);
	fifo->windex += n;

	return (ptr);
}

static void
fifo_reset(struct cyapa_softc *sc, struct cyapa_fifo *fifo)
{

	CYAPA_LOCK_ASSERT(sc);

	fifo->rindex = 0;
	fifo->windex = 0;
}

/*
 * Fuzz handling
 */
static int
cyapa_fuzz(int delta, int *fuzzp)
{
	int fuzz;

	fuzz = *fuzzp;
	if (fuzz >= 0 && delta < 0) {
		++delta;
		--fuzz;
	} else if (fuzz <= 0 && delta > 0) {
		--delta;
		++fuzz;
	}
	*fuzzp = fuzz;

	return (delta);
}

DRIVER_MODULE(cyapa, iicbus, cyapa_driver, cyapa_devclass, NULL, NULL);
MODULE_DEPEND(cyapa, iicbus, IICBUS_MINVER, IICBUS_PREFVER, IICBUS_MAXVER);
MODULE_VERSION(cyapa, 1);
