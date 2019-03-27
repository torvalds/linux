/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Sascha Schumann. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY SASCHA SCHUMANN ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO
 * EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_pcfclock.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/uio.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/ppbus/ppbconf.h>
#include <dev/ppbus/ppb_msq.h>
#include <dev/ppbus/ppbio.h>

#include "ppbus_if.h"

#define PCFCLOCK_NAME "pcfclock"

struct pcfclock_data {
	device_t dev;
	struct cdev *cdev;
};

static devclass_t pcfclock_devclass;

static	d_open_t		pcfclock_open;
static	d_close_t		pcfclock_close;
static	d_read_t		pcfclock_read;

static struct cdevsw pcfclock_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	pcfclock_open,
	.d_close =	pcfclock_close,
	.d_read =	pcfclock_read,
	.d_name =	PCFCLOCK_NAME,
};

#ifndef PCFCLOCK_MAX_RETRIES
#define PCFCLOCK_MAX_RETRIES 10
#endif

#define AFC_HI 0
#define AFC_LO AUTOFEED

/* AUTO FEED is used as clock */
#define AUTOFEED_CLOCK(val) \
	ctr = (ctr & ~(AUTOFEED)) ^ (val); ppb_wctr(ppbus, ctr)

/* SLCT is used as clock */
#define CLOCK_OK \
	((ppb_rstr(ppbus) & SELECT) == (i & 1 ? SELECT : 0))

/* PE is used as data */
#define BIT_SET (ppb_rstr(ppbus)&PERROR)

/* the first byte sent as reply must be 00001001b */
#define PCFCLOCK_CORRECT_SYNC(buf) (buf[0] == 9)

#define NR(buf, off) (buf[off+1]*10+buf[off])

/* check for correct input values */
#define PCFCLOCK_CORRECT_FORMAT(buf) (\
	NR(buf, 14) <= 99 && \
	NR(buf, 12) <= 12 && \
	NR(buf, 10) <= 31 && \
	NR(buf,  6) <= 23 && \
	NR(buf,  4) <= 59 && \
	NR(buf,  2) <= 59)

#define PCFCLOCK_BATTERY_STATUS_LOW(buf) (buf[8] & 4)

#define PCFCLOCK_CMD_TIME 0		/* send current time */
#define PCFCLOCK_CMD_COPY 7 	/* copy received signal to PC */

static void
pcfclock_identify(driver_t *driver, device_t parent)
{

	device_t dev;

	dev = device_find_child(parent, PCFCLOCK_NAME, -1);
	if (!dev)
		BUS_ADD_CHILD(parent, 0, PCFCLOCK_NAME, -1);
}

static int
pcfclock_probe(device_t dev)
{

	device_set_desc(dev, "PCF-1.0");
	return (0);
}

static int
pcfclock_attach(device_t dev)
{
	struct pcfclock_data *sc = device_get_softc(dev);
	int unit;

	unit = device_get_unit(dev);

	sc->dev = dev;
	sc->cdev = make_dev(&pcfclock_cdevsw, unit,
			UID_ROOT, GID_WHEEL, 0400, PCFCLOCK_NAME "%d", unit);
	if (sc->cdev == NULL) {
		device_printf(dev, "Failed to create character device\n");
		return (ENXIO);
	}
	sc->cdev->si_drv1 = sc;

	return (0);
}

static int
pcfclock_open(struct cdev *dev, int flag, int fms, struct thread *td)
{
	struct pcfclock_data *sc = dev->si_drv1;
	device_t pcfclockdev;
	device_t ppbus;
	int res;

	if (!sc)
		return (ENXIO);
	pcfclockdev = sc->dev;
	ppbus = device_get_parent(pcfclockdev);

	ppb_lock(ppbus);
	res = ppb_request_bus(ppbus, pcfclockdev,
	    (flag & O_NONBLOCK) ? PPB_DONTWAIT : PPB_WAIT);
	ppb_unlock(ppbus);
	return (res);
}

static int
pcfclock_close(struct cdev *dev, int flags, int fmt, struct thread *td)
{
	struct pcfclock_data *sc = dev->si_drv1;
	device_t pcfclockdev = sc->dev;
	device_t ppbus = device_get_parent(pcfclockdev);

	ppb_lock(ppbus);
	ppb_release_bus(ppbus, pcfclockdev);
	ppb_unlock(ppbus);

	return (0);
}

static void
pcfclock_write_cmd(struct cdev *dev, unsigned char command)
{
	struct pcfclock_data *sc = dev->si_drv1;
	device_t pcfclockdev = sc->dev;
	device_t ppbus = device_get_parent(pcfclockdev);
	unsigned char ctr = 14;
	char i;

	for (i = 0; i <= 7; i++) {
		ppb_wdtr(ppbus, i);
		AUTOFEED_CLOCK(i & 1 ? AFC_HI : AFC_LO);
		DELAY(3000);
	}
	ppb_wdtr(ppbus, command);
	AUTOFEED_CLOCK(AFC_LO);
	DELAY(3000);
	AUTOFEED_CLOCK(AFC_HI);
}

static void
pcfclock_display_data(struct cdev *dev, char buf[18])
{
	struct pcfclock_data *sc = dev->si_drv1;
#ifdef PCFCLOCK_VERBOSE
	int year;

	year = NR(buf, 14);
	if (year < 70)
		year += 100;

	device_printf(sc->dev, "%02d.%02d.%4d %02d:%02d:%02d, "
			"battery status: %s\n",
			NR(buf, 10), NR(buf, 12), 1900 + year,
			NR(buf, 6), NR(buf, 4), NR(buf, 2),
			PCFCLOCK_BATTERY_STATUS_LOW(buf) ? "LOW" : "ok");
#else
	if (PCFCLOCK_BATTERY_STATUS_LOW(buf))
		device_printf(sc->dev, "BATTERY STATUS LOW ON\n");
#endif
}

static int
pcfclock_read_data(struct cdev *dev, char *buf, ssize_t bits)
{
	struct pcfclock_data *sc = dev->si_drv1;
	device_t pcfclockdev = sc->dev;
	device_t ppbus = device_get_parent(pcfclockdev);
	int i;
	char waitfor;
	int offset;

	/* one byte per four bits */
	bzero(buf, ((bits + 3) >> 2) + 1);

	waitfor = 100;
	for (i = 0; i <= bits; i++) {
		/* wait for clock, maximum (waitfor*100) usec */
		while (!CLOCK_OK && --waitfor > 0)
			DELAY(100);

		/* timed out? */
		if (!waitfor)
			return (EIO);

		waitfor = 100; /* reload */

		/* give it some time */
		DELAY(500);

		/* calculate offset into buffer */
		offset = i >> 2;
		buf[offset] <<= 1;

		if (BIT_SET)
			buf[offset] |= 1;
	}

	return (0);
}

static int
pcfclock_read_dev(struct cdev *dev, char *buf, int maxretries)
{
	struct pcfclock_data *sc = dev->si_drv1;
	device_t pcfclockdev = sc->dev;
	device_t ppbus = device_get_parent(pcfclockdev);
	int error = 0;

	ppb_set_mode(ppbus, PPB_COMPATIBLE);

	while (--maxretries > 0) {
		pcfclock_write_cmd(dev, PCFCLOCK_CMD_TIME);
		if (pcfclock_read_data(dev, buf, 68))
			continue;

		if (!PCFCLOCK_CORRECT_SYNC(buf))
			continue;

		if (!PCFCLOCK_CORRECT_FORMAT(buf))
			continue;

		break;
	}

	if (!maxretries)
		error = EIO;

	return (error);
}

static int
pcfclock_read(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct pcfclock_data *sc = dev->si_drv1;
	device_t ppbus;
	char buf[18];
	int error = 0;

	if (uio->uio_resid < 18)
		return (ERANGE);

	ppbus = device_get_parent(sc->dev);
	ppb_lock(ppbus);
	error = pcfclock_read_dev(dev, buf, PCFCLOCK_MAX_RETRIES);
	ppb_unlock(ppbus);

	if (error) {
		device_printf(sc->dev, "no PCF found\n");
	} else {
		pcfclock_display_data(dev, buf);

		uiomove(buf, 18, uio);
	}

	return (error);
}

static device_method_t pcfclock_methods[] = {
	/* device interface */
	DEVMETHOD(device_identify,	pcfclock_identify),
	DEVMETHOD(device_probe,		pcfclock_probe),
	DEVMETHOD(device_attach,	pcfclock_attach),

	{ 0, 0 }
};

static driver_t pcfclock_driver = {
	PCFCLOCK_NAME,
	pcfclock_methods,
	sizeof(struct pcfclock_data),
};

DRIVER_MODULE(pcfclock, ppbus, pcfclock_driver, pcfclock_devclass, 0, 0);
