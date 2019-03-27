/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999,2000 Jonathan Lemon
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
 *
 * $FreeBSD$
 */

/*
 * Disk driver for Compaq SMART RAID adapters.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include <sys/bio.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/cons.h>

#include <machine/bus.h>
#include <sys/rman.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <machine/md_var.h>

#include <geom/geom_disk.h>

#include <dev/ida/idareg.h>
#include <dev/ida/idavar.h>

/* prototypes */
static int idad_probe(device_t dev);
static int idad_attach(device_t dev);
static int idad_detach(device_t dev);

static	d_strategy_t	idad_strategy;
static	dumper_t	idad_dump;

static devclass_t	idad_devclass;

static device_method_t idad_methods[] = {
	DEVMETHOD(device_probe,		idad_probe),
	DEVMETHOD(device_attach,	idad_attach),
	DEVMETHOD(device_detach,	idad_detach),
	{ 0, 0 }
};

static driver_t idad_driver = {
	"idad",
	idad_methods,
	sizeof(struct idad_softc)
};

DRIVER_MODULE(idad, ida, idad_driver, idad_devclass, 0, 0);

/*
 * Read/write routine for a buffer.  Finds the proper unit, range checks
 * arguments, and schedules the transfer.  Does not wait for the transfer
 * to complete.  Multi-page transfers are supported.  All I/O requests must
 * be a multiple of a sector in length.
 */
static void
idad_strategy(struct bio *bp)
{
	struct idad_softc *drv;

	drv = bp->bio_disk->d_drv1;
	if (drv == NULL) {
    		bp->bio_error = EINVAL;
		goto bad;
	}

	/*
	 * software write protect check
	 */
	if (drv->flags & DRV_WRITEPROT && (bp->bio_cmd == BIO_WRITE)) {
		bp->bio_error = EROFS;
		goto bad;
	}

	bp->bio_driver1 = drv;
	ida_submit_buf(drv->controller, bp);
	return;

bad:
	bp->bio_flags |= BIO_ERROR;

	/*
	 * Correctly set the buf to indicate a completed transfer
	 */
	bp->bio_resid = bp->bio_bcount;
	biodone(bp);
	return;
}

static int
idad_dump(void *arg, void *virtual, vm_offset_t physical, off_t offset, size_t length)
{

	struct idad_softc *drv;
	int error = 0;
	struct disk *dp;

	dp = arg;
	drv = dp->d_drv1;
	if (drv == NULL)
		return (ENXIO);

	drv->controller->flags &= ~IDA_INTERRUPTS;

	if (length > 0) {
		error = ida_command(drv->controller, CMD_WRITE, virtual,
		    length, drv->drive, offset / DEV_BSIZE, DMA_DATA_OUT);
	}
	drv->controller->flags |= IDA_INTERRUPTS;
	return (error);
}

void
idad_intr(struct bio *bp)
{
	struct idad_softc *drv;

	drv = bp->bio_disk->d_drv1;

	if (bp->bio_flags & BIO_ERROR)
		bp->bio_error = EIO;
	else
		bp->bio_resid = 0;

	biodone(bp);
}

static int
idad_probe(device_t dev)
{

	device_set_desc(dev, "Compaq Logical Drive");
	return (0);
}

static int
idad_attach(device_t dev)
{
	struct ida_drive_info dinfo;
	struct idad_softc *drv;
	device_t parent;
	int error;

	drv = (struct idad_softc *)device_get_softc(dev);
	parent = device_get_parent(dev);
	drv->dev = dev;
	drv->controller = (struct ida_softc *)device_get_softc(parent);
	drv->unit = device_get_unit(dev);
	drv->drive = (intptr_t)device_get_ivars(dev);

	mtx_lock(&drv->controller->lock);
	error = ida_command(drv->controller, CMD_GET_LOG_DRV_INFO,
	    &dinfo, sizeof(dinfo), drv->drive, 0, DMA_DATA_IN);
	mtx_unlock(&drv->controller->lock);
	if (error) {
		device_printf(dev, "CMD_GET_LOG_DRV_INFO failed\n");
		return (ENXIO);
	}

	drv->cylinders = dinfo.dp.ncylinders;
	drv->heads = dinfo.dp.nheads;
	drv->sectors = dinfo.dp.nsectors;
	drv->secsize = dinfo.secsize == 0 ? 512 : dinfo.secsize;
	drv->secperunit = dinfo.secperunit;

	/* XXX
	 * other initialization
	 */
	device_printf(dev, "%uMB (%u sectors), blocksize=%d\n",
	    drv->secperunit / ((1024 * 1024) / drv->secsize),
	    drv->secperunit, drv->secsize);

	drv->disk = disk_alloc();
	drv->disk->d_strategy = idad_strategy;
	drv->disk->d_name = "idad";
	drv->disk->d_dump = idad_dump;
	drv->disk->d_sectorsize = drv->secsize;
	drv->disk->d_mediasize = (off_t)drv->secperunit * drv->secsize;
	drv->disk->d_fwsectors = drv->sectors;
	drv->disk->d_fwheads = drv->heads;
	drv->disk->d_drv1 = drv;
	drv->disk->d_maxsize = DFLTPHYS;		/* XXX guess? */
	drv->disk->d_unit = drv->unit;
	disk_create(drv->disk, DISK_VERSION);

	return (0);
}

static int
idad_detach(device_t dev)
{
	struct idad_softc *drv;

	drv = (struct idad_softc *)device_get_softc(dev);
	disk_destroy(drv->disk);
	return (0);
}
