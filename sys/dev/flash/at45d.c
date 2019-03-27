/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 M. Warner Losh
 * Copyright (c) 2011-2012 Ian Lepore
 * Copyright (c) 2012 Marius Strobl <marius@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bio.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/lock.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <geom/geom_disk.h>

#include <dev/spibus/spi.h>
#include "spibus_if.h"

#include "opt_platform.h"

#ifdef FDT
#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/openfirm.h>

static struct ofw_compat_data compat_data[] = {
	{ "atmel,at45",		1 },
	{ "atmel,dataflash",	1 },
	{ NULL,			0 },
};
#endif

/* This is the information returned by the MANUFACTURER_ID command. */
struct at45d_mfg_info {
	uint32_t	jedec_id; /* Mfg ID, DevId1, DevId2, ExtLen */
	uint16_t	ext_id;   /* ExtId1, ExtId2 */
};

/*
 * This is an entry in our table of metadata describing the chips.  We match on
 * both jedec id and extended id info returned by the MANUFACTURER_ID command.
 */
struct at45d_flash_ident
{
	const char	*name;
	uint32_t	jedec;
	uint16_t	extid;
	uint16_t	extmask;
	uint16_t	pagecount;
	uint16_t	pageoffset;
	uint16_t	pagesize;
	uint16_t	pagesize2n;
};

struct at45d_softc
{
	struct bio_queue_head	bio_queue;
	struct mtx		sc_mtx;
	struct disk		*disk;
	struct proc		*p;
	device_t		dev;
	u_int			taskstate;
	uint16_t		pagecount;
	uint16_t		pageoffset;
	uint16_t		pagesize;
	void			*dummybuf;
};

#define	TSTATE_STOPPED	0
#define	TSTATE_STOPPING	1
#define	TSTATE_RUNNING	2

#define	AT45D_LOCK(_sc)			mtx_lock(&(_sc)->sc_mtx)
#define	AT45D_UNLOCK(_sc)		mtx_unlock(&(_sc)->sc_mtx)
#define	AT45D_LOCK_INIT(_sc) \
	mtx_init(&_sc->sc_mtx, device_get_nameunit(_sc->dev), \
	    "at45d", MTX_DEF)
#define	AT45D_LOCK_DESTROY(_sc)		mtx_destroy(&_sc->sc_mtx);
#define	AT45D_ASSERT_LOCKED(_sc)	mtx_assert(&_sc->sc_mtx, MA_OWNED);
#define	AT45D_ASSERT_UNLOCKED(_sc)	mtx_assert(&_sc->sc_mtx, MA_NOTOWNED);

/* bus entry points */
static device_attach_t at45d_attach;
static device_detach_t at45d_detach;
static device_probe_t at45d_probe;

/* disk routines */
static int at45d_close(struct disk *dp);
static int at45d_open(struct disk *dp);
static int at45d_getattr(struct bio *bp);
static void at45d_strategy(struct bio *bp);
static void at45d_task(void *arg);

/* helper routines */
static void at45d_delayed_attach(void *xsc);
static int at45d_get_mfg_info(device_t dev, struct at45d_mfg_info *resp);
static int at45d_get_status(device_t dev, uint8_t *status);
static int at45d_wait_ready(device_t dev, uint8_t *status);

#define	PAGE_TO_BUFFER_TRANSFER		0x53
#define	PAGE_TO_BUFFER_COMPARE		0x60
#define	PROGRAM_THROUGH_BUFFER		0x82
#define	MANUFACTURER_ID			0x9f
#define	STATUS_REGISTER_READ		0xd7
#define	CONTINUOUS_ARRAY_READ		0xe8

#define	STATUS_READY			(1u << 7)
#define	STATUS_CMPFAIL			(1u << 6)
#define	STATUS_PAGE2N			(1u << 0)

/*
 * Metadata for supported chips.
 *
 * The jedec id in this table includes the extended id length byte.  A match is
 * based on both jedec id and extended id matching.  The chip's extended id (not
 * present in most chips) is ANDed with ExtMask and the result is compared to
 * ExtId.  If a chip only returns 1 ext id byte it will be in the upper 8 bits
 * of ExtId in this table.
 *
 * A sectorsize2n != 0 is used to indicate that a device optionally supports
 * 2^N byte pages.  If support for the latter is enabled, the sector offset
 * has to be reduced by one.
 */
static const struct at45d_flash_ident at45d_flash_devices[] = {
	/* Part Name    Jedec ID    ExtId   ExtMask PgCnt Offs PgSz PgSz2n */
	{ "AT45DB011B", 0x1f220000, 0x0000, 0x0000,   512,  9,  264,  256 },
	{ "AT45DB021B", 0x1f230000, 0x0000, 0x0000,  1024,  9,  264,  256 },
	{ "AT45DB041x", 0x1f240000, 0x0000, 0x0000,  2028,  9,  264,  256 },
	{ "AT45DB081B", 0x1f250000, 0x0000, 0x0000,  4096,  9,  264,  256 },
	{ "AT45DB161x", 0x1f260000, 0x0000, 0x0000,  4096, 10,  528,  512 },
	{ "AT45DB321x", 0x1f270000, 0x0000, 0x0000,  8192, 10,  528,    0 },
	{ "AT45DB321x", 0x1f270100, 0x0000, 0x0000,  8192, 10,  528,  512 },
	{ "AT45DB641E", 0x1f280001, 0x0000, 0xff00, 32768,  9,  264,  256 },
	{ "AT45DB642x", 0x1f280000, 0x0000, 0x0000,  8192, 11, 1056, 1024 },
};

static int
at45d_get_status(device_t dev, uint8_t *status)
{
	uint8_t rxBuf[8], txBuf[8];
	struct spi_command cmd;
	int err;

	memset(&cmd, 0, sizeof(cmd));
	memset(txBuf, 0, sizeof(txBuf));
	memset(rxBuf, 0, sizeof(rxBuf));

	txBuf[0] = STATUS_REGISTER_READ;
	cmd.tx_cmd = txBuf;
	cmd.rx_cmd = rxBuf;
	cmd.rx_cmd_sz = cmd.tx_cmd_sz = 2;
	err = SPIBUS_TRANSFER(device_get_parent(dev), dev, &cmd);
	*status = rxBuf[1];
	return (err);
}

static int
at45d_get_mfg_info(device_t dev, struct at45d_mfg_info *resp)
{
	uint8_t rxBuf[8], txBuf[8];
	struct spi_command cmd;
	int err;

	memset(&cmd, 0, sizeof(cmd));
	memset(txBuf, 0, sizeof(txBuf));
	memset(rxBuf, 0, sizeof(rxBuf));

	txBuf[0] = MANUFACTURER_ID;
	cmd.tx_cmd = &txBuf;
	cmd.rx_cmd = &rxBuf;
	cmd.tx_cmd_sz = cmd.rx_cmd_sz = 7;
	err = SPIBUS_TRANSFER(device_get_parent(dev), dev, &cmd);
	if (err)
		return (err);

	resp->jedec_id = be32dec(rxBuf + 1);
	resp->ext_id   = be16dec(rxBuf + 5);

	return (0);
}

static int
at45d_wait_ready(device_t dev, uint8_t *status)
{
	struct timeval now, tout;
	int err;

	getmicrouptime(&tout);
	tout.tv_sec += 3;
	do {
		getmicrouptime(&now);
		if (now.tv_sec > tout.tv_sec)
			err = ETIMEDOUT;
		else
			err = at45d_get_status(dev, status);
	} while (err == 0 && !(*status & STATUS_READY));
	return (err);
}

static int
at45d_probe(device_t dev)
{
	int rv;

#ifdef FDT
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	rv = BUS_PROBE_DEFAULT;
#else
	rv = BUS_PROBE_NOWILDCARD;
#endif

	device_set_desc(dev, "AT45D Flash Family");
	return (rv);
}

static int
at45d_attach(device_t dev)
{
	struct at45d_softc *sc;

	sc = device_get_softc(dev);
	sc->dev = dev;
	AT45D_LOCK_INIT(sc);

	config_intrhook_oneshot(at45d_delayed_attach, sc);
	return (0);
}

static int
at45d_detach(device_t dev)
{
	struct at45d_softc *sc;
	int err;

	sc = device_get_softc(dev);
	err = 0;

	AT45D_LOCK(sc);
	if (sc->taskstate == TSTATE_RUNNING) {
		sc->taskstate = TSTATE_STOPPING;
		wakeup(sc);
		while (err == 0 && sc->taskstate != TSTATE_STOPPED) {
			err = msleep(sc, &sc->sc_mtx, 0, "at45dt", hz * 3);
			if (err != 0) {
				sc->taskstate = TSTATE_RUNNING;
				device_printf(sc->dev,
				    "Failed to stop queue task\n");
			}
		}
	}
	AT45D_UNLOCK(sc);

	if (err == 0 && sc->taskstate == TSTATE_STOPPED) {
		if (sc->disk) {
			disk_destroy(sc->disk);
			bioq_flush(&sc->bio_queue, NULL, ENXIO);
			free(sc->dummybuf, M_DEVBUF);
		}
		AT45D_LOCK_DESTROY(sc);
	}
	return (err);
}

static void
at45d_delayed_attach(void *xsc)
{
	struct at45d_softc *sc;
	struct at45d_mfg_info mfginfo;
	const struct at45d_flash_ident *ident;
	u_int i;
	int sectorsize;
	uint32_t jedec;
	uint16_t pagesize;
	uint8_t status;

	sc = xsc;
	ident = NULL;
	jedec = 0;

	if (at45d_wait_ready(sc->dev, &status) != 0) {
		device_printf(sc->dev, "Error waiting for device-ready.\n");
		return;
	}
	if (at45d_get_mfg_info(sc->dev, &mfginfo) != 0) {
		device_printf(sc->dev, "Failed to get ID.\n");
		return;
	}
	for (i = 0; i < nitems(at45d_flash_devices); i++) {
		ident = &at45d_flash_devices[i];
		if (mfginfo.jedec_id == ident->jedec && 
		    (mfginfo.ext_id & ident->extmask) == ident->extid) {
			break;
		}
	}
	if (i == nitems(at45d_flash_devices)) {
		device_printf(sc->dev, "JEDEC 0x%x not in list.\n", jedec);
		return;
	}

	sc->pagecount = ident->pagecount;
	sc->pageoffset = ident->pageoffset;
	if (ident->pagesize2n != 0 && (status & STATUS_PAGE2N)) {
		sc->pageoffset -= 1;
		pagesize = ident->pagesize2n;
	} else
		pagesize = ident->pagesize;
	sc->pagesize = pagesize;

	/*
	 * By default we set up a disk with a sector size that matches the
	 * device page size.  If there is a device hint or fdt property
	 * requesting a different size, use that, as long as it is a multiple of
	 * the device page size).
	 */
	sectorsize = pagesize;
#ifdef FDT
	{
		pcell_t size;
		if (OF_getencprop(ofw_bus_get_node(sc->dev),
		    "freebsd,sectorsize", &size, sizeof(size)) > 0)
			sectorsize = size;
	}
#endif
	resource_int_value(device_get_name(sc->dev), device_get_unit(sc->dev),
	    "sectorsize", &sectorsize);

	if ((sectorsize % pagesize) != 0) {
		device_printf(sc->dev, "Invalid sectorsize %d, "
		    "must be a multiple of %d\n", sectorsize, pagesize);
		return;
	}

	sc->dummybuf = malloc(pagesize, M_DEVBUF, M_WAITOK | M_ZERO);

	sc->disk = disk_alloc();
	sc->disk->d_open = at45d_open;
	sc->disk->d_close = at45d_close;
	sc->disk->d_strategy = at45d_strategy;
	sc->disk->d_getattr = at45d_getattr;
	sc->disk->d_name = "flash/at45d";
	sc->disk->d_drv1 = sc;
	sc->disk->d_maxsize = DFLTPHYS;
	sc->disk->d_sectorsize = sectorsize;
	sc->disk->d_mediasize = pagesize * ident->pagecount;
	sc->disk->d_unit = device_get_unit(sc->dev);
	disk_create(sc->disk, DISK_VERSION);
	bioq_init(&sc->bio_queue);
	kproc_create(&at45d_task, sc, &sc->p, 0, 0, "task: at45d flash");
	sc->taskstate = TSTATE_RUNNING;
	device_printf(sc->dev,
	    "%s, %d bytes per page, %d pages; %d KBytes; disk sector size %d\n",
	    ident->name, pagesize, ident->pagecount,
	    (pagesize * ident->pagecount) / 1024, sectorsize);
}

static int
at45d_open(struct disk *dp)
{

	return (0);
}

static int
at45d_close(struct disk *dp)
{

	return (0);
}

static int
at45d_getattr(struct bio *bp)
{
	struct at45d_softc *sc;

	/*
	 * This function exists to support geom_flashmap and fdt_slicer.
	 */

	if (bp->bio_disk == NULL || bp->bio_disk->d_drv1 == NULL)
		return (ENXIO);
	if (strcmp(bp->bio_attribute, "SPI::device") != 0)
		return (-1);
	sc = bp->bio_disk->d_drv1;
	if (bp->bio_length != sizeof(sc->dev))
		return (EFAULT);
	bcopy(&sc->dev, bp->bio_data, sizeof(sc->dev));
	return (0);
}

static void
at45d_strategy(struct bio *bp)
{
	struct at45d_softc *sc;

	sc = (struct at45d_softc *)bp->bio_disk->d_drv1;
	AT45D_LOCK(sc);
	bioq_disksort(&sc->bio_queue, bp);
	wakeup(sc);
	AT45D_UNLOCK(sc);
}

static void
at45d_task(void *arg)
{
	uint8_t rxBuf[8], txBuf[8];
	struct at45d_softc *sc;
	struct bio *bp;
	struct spi_command cmd;
	device_t dev, pdev;
	caddr_t buf;
	u_long len, resid;
	u_int addr, berr, err, offset, page;
	uint8_t status;

	sc = (struct at45d_softc*)arg;
	dev = sc->dev;
	pdev = device_get_parent(dev);
	memset(&cmd, 0, sizeof(cmd));
	memset(txBuf, 0, sizeof(txBuf));
	memset(rxBuf, 0, sizeof(rxBuf));
	cmd.tx_cmd = txBuf;
	cmd.rx_cmd = rxBuf;

	for (;;) {
		AT45D_LOCK(sc);
		do {
			if (sc->taskstate == TSTATE_STOPPING) {
				sc->taskstate = TSTATE_STOPPED;
				AT45D_UNLOCK(sc);
				wakeup(sc);
				kproc_exit(0);
			}
			bp = bioq_takefirst(&sc->bio_queue);
			if (bp == NULL)
				msleep(sc, &sc->sc_mtx, PRIBIO, "at45dq", 0);
		} while (bp == NULL);
		AT45D_UNLOCK(sc);

		berr = 0;
		buf = bp->bio_data;
		len = resid = bp->bio_bcount;
		page = bp->bio_offset / sc->pagesize;
		offset = bp->bio_offset % sc->pagesize;

		switch (bp->bio_cmd) {
		case BIO_READ:
			txBuf[0] = CONTINUOUS_ARRAY_READ;
			cmd.tx_cmd_sz = cmd.rx_cmd_sz = 8;
			cmd.tx_data = sc->dummybuf;
			cmd.rx_data = buf;
			break;
		case BIO_WRITE:
			cmd.tx_cmd_sz = cmd.rx_cmd_sz = 4;
			cmd.tx_data = buf;
			cmd.rx_data = sc->dummybuf;
			if (resid + offset > sc->pagesize)
				len = sc->pagesize - offset;
			break;
		default:
			berr = EINVAL;
			goto out;
		}

		/*
		 * NB: for BIO_READ, this loop is only traversed once.
		 */
		while (resid > 0) {
			if (page > sc->pagecount) {
				berr = EINVAL;
				goto out;
			}
			addr = page << sc->pageoffset;
			if (bp->bio_cmd == BIO_WRITE) {
				/*
				 * If writing less than a full page, transfer
				 * the existing page to the buffer, so that our
				 * PROGRAM_THROUGH_BUFFER below will preserve
				 * the parts of the page we're not writing.
				 */
				if (len != sc->pagesize) {
					txBuf[0] = PAGE_TO_BUFFER_TRANSFER;
					txBuf[1] = ((addr >> 16) & 0xff);
					txBuf[2] = ((addr >> 8) & 0xff);
					txBuf[3] = 0;
					cmd.tx_data_sz = cmd.rx_data_sz = 0;
					err = SPIBUS_TRANSFER(pdev, dev, &cmd);
					if (err == 0)
						err = at45d_wait_ready(dev,
						    &status);
					if (err != 0) {
						berr = EIO;
						goto out;
					}
				}
				txBuf[0] = PROGRAM_THROUGH_BUFFER;
			}

			addr += offset;
			txBuf[1] = ((addr >> 16) & 0xff);
			txBuf[2] = ((addr >> 8) & 0xff);
			txBuf[3] = (addr & 0xff);
			cmd.tx_data_sz = cmd.rx_data_sz = len;
			err = SPIBUS_TRANSFER(pdev, dev, &cmd);
			if (err == 0 && bp->bio_cmd != BIO_READ)
				err = at45d_wait_ready(dev, &status);
			if (err != 0) {
				berr = EIO;
				goto out;
			}
			if (bp->bio_cmd == BIO_WRITE) {
				addr = page << sc->pageoffset;
				txBuf[0] = PAGE_TO_BUFFER_COMPARE;
				txBuf[1] = ((addr >> 16) & 0xff);
				txBuf[2] = ((addr >> 8) & 0xff);
				txBuf[3] = 0;
				cmd.tx_data_sz = cmd.rx_data_sz = 0;
				err = SPIBUS_TRANSFER(pdev, dev, &cmd);
				if (err == 0)
					err = at45d_wait_ready(dev, &status);
				if (err != 0 || (status & STATUS_CMPFAIL)) {
					device_printf(dev, "comparing page "
					    "%d failed (status=0x%x)\n", page,
					    status);
					berr = EIO;
					goto out;
				}
			}
			page++;
			buf += len;
			offset = 0;
			resid -= len;
			if (resid > sc->pagesize)
				len = sc->pagesize;
			else
				len = resid;
			if (bp->bio_cmd == BIO_READ)
				cmd.rx_data = buf;
			else
				cmd.tx_data = buf;
		}
 out:
		if (berr != 0) {
			bp->bio_flags |= BIO_ERROR;
			bp->bio_error = berr;
		}
		bp->bio_resid = resid;
		biodone(bp);
	}
}

static devclass_t at45d_devclass;

static device_method_t at45d_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		at45d_probe),
	DEVMETHOD(device_attach,	at45d_attach),
	DEVMETHOD(device_detach,	at45d_detach),

	DEVMETHOD_END
};

static driver_t at45d_driver = {
	"at45d",
	at45d_methods,
	sizeof(struct at45d_softc),
};

DRIVER_MODULE(at45d, spibus, at45d_driver, at45d_devclass, NULL, NULL);
MODULE_DEPEND(at45d, spibus, 1, 1, 1);
#ifdef FDT
MODULE_DEPEND(at45d, fdt_slicer, 1, 1, 1);
SPIBUS_PNP_INFO(compat_data);
#endif

