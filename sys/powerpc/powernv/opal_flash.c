/*-
 * Copyright (c) 2019 Justin Hibbits
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
#include <sys/systm.h>
#include <sys/bio.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <geom/geom_disk.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/openfirm.h>
#include "opal.h"

/*
 * OPAL System flash driver, using OPAL firmware calls to access the device.
 *
 * This just presents the base block interface.  The fdt_slicer can be used on
 * top to present the partitions listed in the fdt.
 *
 * There are three OPAL methods used: OPAL_FLASH_READ, OPAL_FLASH_WRITE, and
 * OPAL_FLASH_ERASE.  At the firmware layer, READ and WRITE can be on arbitrary
 * boundaries, but ERASE is only at flash-block-size block alignments and sizes.
 * To account for this, the following restrictions are in place:
 *
 * - Reads are on a 512-byte block boundary and size
 * - Writes and Erases are aligned and sized on flash-block-size bytes.
 *
 * In order to support the fdt_slicer we present a type attribute of
 * NAND::device.
 */
struct opalflash_softc {
	device_t		 sc_dev;
	struct mtx		 sc_mtx;
	struct disk		*sc_disk;
	struct proc		*sc_p;
	struct bio_queue_head	 sc_bio_queue;
	int		 	 sc_opal_id;
};

#define	OPALFLASH_LOCK(sc)		mtx_lock(&(sc)->sc_mtx)
#define	OPALFLASH_UNLOCK(sc)		mtx_unlock(&(sc)->sc_mtx)
#define	OPALFLASH_LOCK_INIT(sc) \
	mtx_init(&(sc)->sc_mtx, device_get_nameunit((sc)->sc_dev), \
	    "opalflash", MTX_DEF)

#define	FLASH_BLOCKSIZE			512

static int	opalflash_probe(device_t);
static int	opalflash_attach(device_t);

static device_method_t  opalflash_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		opalflash_probe),
	DEVMETHOD(device_attach,	opalflash_attach),

	DEVMETHOD_END
};

static driver_t opalflash_driver = {
	"opalflash",
	opalflash_methods,
	sizeof(struct opalflash_softc)
};

static devclass_t opalflash_devclass;

DRIVER_MODULE(opalflash, opal, opalflash_driver, opalflash_devclass, 0, 0);

/* GEOM Disk interfaces. */
static int
opalflash_open(struct disk *dp)
{

	return (0);
}

static int
opalflash_close(struct disk *dp)
{

	return (0);
}

static int
opalflash_ioctl(struct disk *dp, u_long cmd, void *data, int fflag,
	struct thread *td)
{

	return (EINVAL);
}

/* Handle the one attribute we need to play nice with geom_flashmap. */
static int
opalflash_getattr(struct bio *bp)
{
	struct opalflash_softc *sc;
	device_t dev;

	if (bp->bio_disk == NULL || bp->bio_disk->d_drv1 == NULL)
		return (ENXIO);

	sc = bp->bio_disk->d_drv1;
	dev = sc->sc_dev;

	if (strcmp(bp->bio_attribute, "NAND::device") == 0) {
		if (bp->bio_length != sizeof(dev))
			return (EFAULT);
		bcopy(&dev, bp->bio_data, sizeof(dev));
	} else
		return (-1);
	return (0);
}

static void
opalflash_strategy(struct bio *bp)
{
	struct opalflash_softc *sc;

	sc = (struct opalflash_softc *)bp->bio_disk->d_drv1;
	OPALFLASH_LOCK(sc);
	bioq_disksort(&sc->sc_bio_queue, bp);
	wakeup(sc);
	OPALFLASH_UNLOCK(sc);
}

static int
opalflash_read(struct opalflash_softc *sc, off_t off,
    caddr_t data, off_t count)
{
	struct opal_msg msg;
	int rv, size, token;

	/* Ensure we write aligned to a full block size. */
	if (off % sc->sc_disk->d_sectorsize != 0 ||
	    count % sc->sc_disk->d_sectorsize != 0)
		return (EIO);

	token = opal_alloc_async_token();

	/*
	 * Read one page at a time.  It's not guaranteed that the buffer is
	 * physically contiguous.
	 */
	while (count > 0) {
		size = MIN(count, PAGE_SIZE);
		rv = opal_call(OPAL_FLASH_READ, sc->sc_opal_id, off,
		    vtophys(data), size, token);
		if (rv == OPAL_ASYNC_COMPLETION)
			rv = opal_wait_completion(&msg, sizeof(msg), token);
		if (rv != OPAL_SUCCESS)
			break;
		count -= size;
		off += size;
	}
	opal_free_async_token(token);
	if (rv == OPAL_SUCCESS)
		rv = 0;
	else
		rv = EIO;

	return (rv);
}

static int
opalflash_erase(struct opalflash_softc *sc, off_t off, off_t count)
{
	struct opal_msg msg;
	int rv, token;

	/* Ensure we write aligned to a full block size. */
	if (off % sc->sc_disk->d_stripesize != 0 ||
	    count % sc->sc_disk->d_stripesize != 0)
		return (EIO);

	token = opal_alloc_async_token();

	rv = opal_call(OPAL_FLASH_ERASE, sc->sc_opal_id, off, count, token);
	if (rv == OPAL_ASYNC_COMPLETION)
		rv = opal_wait_completion(&msg, sizeof(msg), token);
	opal_free_async_token(token);

	if (rv == OPAL_SUCCESS)
		rv = 0;
	else
		rv = EIO;

	return (rv);
}

static int
opalflash_write(struct opalflash_softc *sc, off_t off,
    caddr_t data, off_t count)
{
	struct opal_msg msg;
	int rv, size, token;

	/* Ensure we write aligned to a full block size. */
	if (off % sc->sc_disk->d_stripesize != 0 ||
	    count % sc->sc_disk->d_stripesize != 0)
		return (EIO);

	/* Erase the full block first, then write in page chunks. */
	rv = opalflash_erase(sc, off, count);
	if (rv != 0)
		return (rv);

	token = opal_alloc_async_token();

	/*
	 * Write one page at a time.  It's not guaranteed that the buffer is
	 * physically contiguous.
	 */
	while (count > 0) {
		size = MIN(count, PAGE_SIZE);
		rv = opal_call(OPAL_FLASH_WRITE, sc->sc_opal_id, off,
		    vtophys(data), size, token);
		if (rv == OPAL_ASYNC_COMPLETION)
			rv = opal_wait_completion(&msg, sizeof(msg), token);
		if (rv != OPAL_SUCCESS)
			break;
		count -= size;
		off += size;
	}
	opal_free_async_token(token);

	if (rv == OPAL_SUCCESS)
		rv = 0;
	else
		rv = EIO;

	return (rv);
}

/* Main flash handling task. */
static void
opalflash_task(void *arg)
{
	struct opalflash_softc *sc;
	struct bio *bp;
	device_t dev;

	sc = arg;

	for (;;) {
		dev = sc->sc_dev;
		OPALFLASH_LOCK(sc);
		do {
			bp = bioq_first(&sc->sc_bio_queue);
			if (bp == NULL)
				msleep(sc, &sc->sc_mtx, PRIBIO, "opalflash", 0);
		} while (bp == NULL);
		bioq_remove(&sc->sc_bio_queue, bp);
		OPALFLASH_UNLOCK(sc);

		switch (bp->bio_cmd) {
		case BIO_DELETE:
			bp->bio_error = opalflash_erase(sc, bp->bio_offset,
			    bp->bio_bcount);
			break;
		case BIO_READ:
			bp->bio_error = opalflash_read(sc, bp->bio_offset,
			    bp->bio_data, bp->bio_bcount);
			break;
		case BIO_WRITE:
			bp->bio_error = opalflash_write(sc, bp->bio_offset,
			    bp->bio_data, bp->bio_bcount);
			break;
		default:
			bp->bio_error = EINVAL;
		}
		biodone(bp);
	}
}


/* Device driver interfaces. */

static int
opalflash_probe(device_t dev)
{
	if (!ofw_bus_is_compatible(dev, "ibm,opal-flash"))
		return (ENXIO);

	device_set_desc(dev, "OPAL System Flash");

	return (BUS_PROBE_GENERIC);
}

static int
opalflash_attach(device_t dev)
{
	struct opalflash_softc *sc;
	phandle_t node;
	cell_t flash_blocksize, opal_id;
	uint32_t regs[2];

	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	node = ofw_bus_get_node(dev);
	OF_getencprop(node, "ibm,opal-id", &opal_id, sizeof(opal_id));
	sc->sc_opal_id = opal_id;

	if (OF_getencprop(node, "ibm,flash-block-size",
	    &flash_blocksize, sizeof(flash_blocksize)) < 0) {
		device_printf(dev, "Cannot determine flash block size.\n");
		return (ENXIO);
	}

	OPALFLASH_LOCK_INIT(sc);

	if (OF_getencprop(node, "reg", regs, sizeof(regs)) < 0) {
		device_printf(dev, "Unable to get flash size.\n");
		return (ENXIO);
	}

	sc->sc_disk = disk_alloc();
	sc->sc_disk->d_name = "opalflash";
	sc->sc_disk->d_open = opalflash_open;
	sc->sc_disk->d_close = opalflash_close;
	sc->sc_disk->d_strategy = opalflash_strategy;
	sc->sc_disk->d_ioctl = opalflash_ioctl;
	sc->sc_disk->d_getattr = opalflash_getattr;
	sc->sc_disk->d_drv1 = sc;
	sc->sc_disk->d_maxsize = DFLTPHYS;
	sc->sc_disk->d_mediasize = regs[1];
	sc->sc_disk->d_unit = device_get_unit(sc->sc_dev);
	sc->sc_disk->d_sectorsize = FLASH_BLOCKSIZE;
	    sc->sc_disk->d_stripesize = flash_blocksize;
	sc->sc_disk->d_dump = NULL;

	disk_create(sc->sc_disk, DISK_VERSION);
	bioq_init(&sc->sc_bio_queue);

	kproc_create(&opalflash_task, sc, &sc->sc_p, 0, 0, "task: OPAL Flash");

	return (0);
}
