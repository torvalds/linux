/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Michael Smith
 * Copyright (c) 2001 Scott Long
 * Copyright (c) 2000 BSDi
 * Copyright (c) 2001 Adaptec, Inc.
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

#include "opt_aac.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/disk.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/md_var.h>
#include <machine/bus.h>
#include <sys/rman.h>

#include <dev/aac/aacreg.h>
#include <sys/aac_ioctl.h>
#include <dev/aac/aacvar.h>

/*
 * Interface to parent.
 */
static int aac_disk_probe(device_t dev);
static int aac_disk_attach(device_t dev);
static int aac_disk_detach(device_t dev);

/*
 * Interface to the device switch.
 */
static	disk_open_t	aac_disk_open;
static	disk_close_t	aac_disk_close;
static	disk_strategy_t	aac_disk_strategy;
static	dumper_t	aac_disk_dump;

static devclass_t	aac_disk_devclass;

static device_method_t aac_disk_methods[] = {
	DEVMETHOD(device_probe,	aac_disk_probe),
	DEVMETHOD(device_attach,	aac_disk_attach),
	DEVMETHOD(device_detach,	aac_disk_detach),
	DEVMETHOD_END
};

static driver_t aac_disk_driver = {
	"aacd",
	aac_disk_methods,
	sizeof(struct aac_disk)
};

DRIVER_MODULE(aacd, aac, aac_disk_driver, aac_disk_devclass, NULL, NULL);

/*
 * Handle open from generic layer.
 *
 * This is called by the diskslice code on first open in order to get the
 * basic device geometry parameters.
 */
static int
aac_disk_open(struct disk *dp)
{
	struct aac_disk	*sc;

	fwprintf(NULL, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	sc = (struct aac_disk *)dp->d_drv1;
	
	if (sc == NULL) {
		printf("aac_disk_open: No Softc\n");
		return (ENXIO);
	}

	/* check that the controller is up and running */
	if (sc->ad_controller->aac_state & AAC_STATE_SUSPEND) {
		device_printf(sc->ad_controller->aac_dev,
		    "Controller Suspended controller state = 0x%x\n",
		    sc->ad_controller->aac_state);
		return(ENXIO);
	}

	sc->ad_flags |= AAC_DISK_OPEN;
	return (0);
}

/*
 * Handle last close of the disk device.
 */
static int
aac_disk_close(struct disk *dp)
{
	struct aac_disk	*sc;

	fwprintf(NULL, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	sc = (struct aac_disk *)dp->d_drv1;
	
	if (sc == NULL)
		return (ENXIO);

	sc->ad_flags &= ~AAC_DISK_OPEN;
	return (0);
}

/*
 * Handle an I/O request.
 */
static void
aac_disk_strategy(struct bio *bp)
{
	struct aac_disk	*sc;

	sc = (struct aac_disk *)bp->bio_disk->d_drv1;
	fwprintf(NULL, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	/* bogus disk? */
	if (sc == NULL) {
		bp->bio_flags |= BIO_ERROR;
		bp->bio_error = EINVAL;
		biodone(bp);
		return;
	}

	/* do-nothing operation? */
	if (bp->bio_bcount == 0) {
		bp->bio_resid = bp->bio_bcount;
		biodone(bp);
		return;
	}

	/* perform accounting */

	/* pass the bio to the controller - it can work out who we are */
	mtx_lock(&sc->ad_controller->aac_io_lock);
	aac_submit_bio(bp);
	mtx_unlock(&sc->ad_controller->aac_io_lock);
}

/*
 * Map the S/G elements for doing a dump.
 */
static void
aac_dump_map_sg(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
	struct aac_fib *fib;
	struct aac_blockwrite *bw;
	struct aac_sg_table *sg;
	int i;

	fib = (struct aac_fib *)arg;
	bw = (struct aac_blockwrite *)&fib->data[0];
	sg = &bw->SgMap;

	if (sg != NULL) {
		sg->SgCount = nsegs;
		for (i = 0; i < nsegs; i++) {
			if (segs[i].ds_addr >= BUS_SPACE_MAXADDR_32BIT)
				return;
			sg->SgEntry[i].SgAddress = segs[i].ds_addr;
			sg->SgEntry[i].SgByteCount = segs[i].ds_len;
		}
		fib->Header.Size = nsegs * sizeof(struct aac_sg_entry);
	}
}

/*
 * Map the S/G elements for doing a dump on 64-bit capable devices.
 */
static void
aac_dump_map_sg64(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
	struct aac_fib *fib;
	struct aac_blockwrite64 *bw;
	struct aac_sg_table64 *sg;
	int i;

	fib = (struct aac_fib *)arg;
	bw = (struct aac_blockwrite64 *)&fib->data[0];
	sg = &bw->SgMap64;

	if (sg != NULL) {
		sg->SgCount = nsegs;
		for (i = 0; i < nsegs; i++) {
			sg->SgEntry64[i].SgAddress = segs[i].ds_addr;
			sg->SgEntry64[i].SgByteCount = segs[i].ds_len;
		}
		fib->Header.Size = nsegs * sizeof(struct aac_sg_entry64);
	}
}

/*
 * Dump memory out to an array
 *
 * Send out one command at a time with up to maxio of data.
 */
static int
aac_disk_dump(void *arg, void *virtual, vm_offset_t physical, off_t offset, size_t length)
{
	struct aac_disk *ad;
	struct aac_softc *sc;
	struct aac_fib *fib;
	size_t len, maxio;
	int size;
	static bus_dmamap_t dump_datamap;
	static int first = 0;
	struct disk *dp;
	bus_dmamap_callback_t *callback;
	u_int32_t command;

	dp = arg;
	ad = dp->d_drv1;

	if (ad == NULL)
		return (EINVAL);

	sc= ad->ad_controller;

	if (!first) {
		first = 1;
		if (bus_dmamap_create(sc->aac_buffer_dmat, 0, &dump_datamap)) {
			device_printf(sc->aac_dev,
			    "bus_dmamap_create failed\n");
			return (ENOMEM);
		}
	}

	/* Skip aac_alloc_sync_fib().  We don't want to mess with sleep locks */
	fib = &sc->aac_common->ac_sync_fib;

	while (length > 0) {
		maxio = sc->aac_max_sectors << 9;
		len = (length > maxio) ? maxio : length;
		if ((sc->flags & AAC_FLAGS_SG_64BIT) == 0) {
			struct aac_blockwrite *bw;
			bw = (struct aac_blockwrite *)&fib->data[0];
			bw->Command = VM_CtBlockWrite;
			bw->ContainerId = ad->ad_container->co_mntobj.ObjectId;
			bw->BlockNumber = offset / AAC_BLOCK_SIZE;
			bw->ByteCount = len;
			bw->Stable = CUNSTABLE;
			command = ContainerCommand;
			callback = aac_dump_map_sg;
			size = sizeof(struct aac_blockwrite);
		} else {
			struct aac_blockwrite64 *bw;
			bw = (struct aac_blockwrite64 *)&fib->data[0];
			bw->Command = VM_CtHostWrite64;
			bw->ContainerId = ad->ad_container->co_mntobj.ObjectId;
			bw->BlockNumber = offset / AAC_BLOCK_SIZE;
			bw->SectorCount = len / AAC_BLOCK_SIZE;
			bw->Pad = 0;
			bw->Flags = 0;
			command = ContainerCommand64;
			callback = aac_dump_map_sg64;
			size = sizeof(struct aac_blockwrite64);
		}

		/*
		 * There really isn't any way to recover from errors or
		 * resource shortages here.  Oh well.  Because of that, don't
		 * bother trying to send the command from the callback; there
		 * is too much required context.
		 */
		if (bus_dmamap_load(sc->aac_buffer_dmat, dump_datamap, virtual,
		    len, callback, fib, BUS_DMA_NOWAIT) != 0)
			return (ENOMEM);

		bus_dmamap_sync(sc->aac_buffer_dmat, dump_datamap,
		    BUS_DMASYNC_PREWRITE);

		/* fib->Header.Size is set in aac_dump_map_sg */
		size += fib->Header.Size;

		if (aac_sync_fib(sc, command, 0, fib, size)) {
			device_printf(sc->aac_dev,
			     "Error dumping block 0x%jx\n",
			     (uintmax_t)physical);
			return (EIO);
		}

		bus_dmamap_sync(sc->aac_buffer_dmat, dump_datamap,
		    BUS_DMASYNC_POSTWRITE);

		bus_dmamap_unload(sc->aac_buffer_dmat, dump_datamap);

		length -= len;
		offset += len;
		virtual = (uint8_t *)virtual + len;
	}

	return (0);
}

/*
 * Handle completion of an I/O request.
 */
void
aac_biodone(struct bio *bp)
{
	fwprintf(NULL, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	if (bp->bio_flags & BIO_ERROR) {
		bp->bio_resid = bp->bio_bcount;
		disk_err(bp, "hard error", -1, 1);
	}

	biodone(bp);
}

/*
 * Stub only.
 */
static int
aac_disk_probe(device_t dev)
{

	fwprintf(NULL, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	return (0);
}

/*
 * Attach a unit to the controller.
 */
static int
aac_disk_attach(device_t dev)
{
	struct aac_disk	*sc;
	
	sc = (struct aac_disk *)device_get_softc(dev);
	fwprintf(NULL, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	/* initialise our softc */
	sc->ad_controller =
	    (struct aac_softc *)device_get_softc(device_get_parent(dev));
	sc->ad_container = device_get_ivars(dev);
	sc->ad_dev = dev;

	/*
	 * require that extended translation be enabled - other drivers read the
	 * disk!
	 */
	sc->ad_size = sc->ad_container->co_mntobj.Capacity;
	if (sc->ad_controller->flags & AAC_FLAGS_LBA_64BIT)
		sc->ad_size += (u_int64_t)
			sc->ad_container->co_mntobj.CapacityHigh << 32;
	if (sc->ad_size >= (2 * 1024 * 1024)) {		/* 2GB */
		sc->ad_heads = 255;
		sc->ad_sectors = 63;
	} else if (sc->ad_size >= (1 * 1024 * 1024)) {	/* 1GB */
		sc->ad_heads = 128;
		sc->ad_sectors = 32;
	} else {
		sc->ad_heads = 64;
		sc->ad_sectors = 32;
	}
	sc->ad_cylinders = (sc->ad_size / (sc->ad_heads * sc->ad_sectors));

	device_printf(dev, "%juMB (%ju sectors)\n",
		      (intmax_t)sc->ad_size / ((1024 * 1024) / AAC_BLOCK_SIZE),
		      (intmax_t)sc->ad_size);

	/* attach a generic disk device to ourselves */
	sc->unit = device_get_unit(dev);
	sc->ad_disk = disk_alloc();
	sc->ad_disk->d_drv1 = sc;
	sc->ad_disk->d_flags = DISKFLAG_UNMAPPED_BIO;
	sc->ad_disk->d_name = "aacd";
	sc->ad_disk->d_maxsize = sc->ad_controller->aac_max_sectors << 9;
	sc->ad_disk->d_open = aac_disk_open;
	sc->ad_disk->d_close = aac_disk_close;
	sc->ad_disk->d_strategy = aac_disk_strategy;
	sc->ad_disk->d_dump = aac_disk_dump;
	sc->ad_disk->d_sectorsize = AAC_BLOCK_SIZE;
	sc->ad_disk->d_mediasize = (off_t)sc->ad_size * AAC_BLOCK_SIZE;
	sc->ad_disk->d_fwsectors = sc->ad_sectors;
	sc->ad_disk->d_fwheads = sc->ad_heads;
	sc->ad_disk->d_unit = sc->unit;
	disk_create(sc->ad_disk, DISK_VERSION);

	return (0);
}

/*
 * Disconnect ourselves from the system.
 */
static int
aac_disk_detach(device_t dev)
{
	struct aac_disk *sc;

	sc = (struct aac_disk *)device_get_softc(dev);
	fwprintf(NULL, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	if (sc->ad_flags & AAC_DISK_OPEN)
		return(EBUSY);

	disk_destroy(sc->ad_disk);

	return(0);
}
