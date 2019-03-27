/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 IronPort Systems
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

#include "opt_mfi.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/selinfo.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/uio.h>

#include <sys/bio.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/disk.h>
#include <geom/geom_disk.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/md_var.h>
#include <machine/bus.h>
#include <sys/rman.h>

#include <dev/mfi/mfireg.h>
#include <dev/mfi/mfi_ioctl.h>
#include <dev/mfi/mfivar.h>

static int	mfi_disk_probe(device_t dev);
static int	mfi_disk_attach(device_t dev);
static int	mfi_disk_detach(device_t dev);

static disk_open_t	mfi_disk_open;
static disk_close_t	mfi_disk_close;
static disk_strategy_t	mfi_disk_strategy;
static dumper_t		mfi_disk_dump;

static devclass_t	mfi_disk_devclass;

static device_method_t mfi_disk_methods[] = {
	DEVMETHOD(device_probe,		mfi_disk_probe),
	DEVMETHOD(device_attach,	mfi_disk_attach),
	DEVMETHOD(device_detach,	mfi_disk_detach),
	{ 0, 0 }
};

static driver_t mfi_disk_driver = {
	"mfid",
	mfi_disk_methods,
	sizeof(struct mfi_disk)
};

DRIVER_MODULE(mfid, mfi, mfi_disk_driver, mfi_disk_devclass, 0, 0);

static int
mfi_disk_probe(device_t dev)
{

	return (0);
}

static int
mfi_disk_attach(device_t dev)
{
	struct mfi_disk *sc;
	struct mfi_ld_info *ld_info;
	struct mfi_disk_pending *ld_pend;
	uint64_t sectors;
	uint32_t secsize;
	char *state;

	sc = device_get_softc(dev);
	ld_info = device_get_ivars(dev);

	sc->ld_dev = dev;
	sc->ld_id = ld_info->ld_config.properties.ld.v.target_id;
	sc->ld_unit = device_get_unit(dev);
	sc->ld_info = ld_info;
	sc->ld_controller = device_get_softc(device_get_parent(dev));
	sc->ld_flags = 0;

	sectors = ld_info->size;
	secsize = MFI_SECTOR_LEN;
	mtx_lock(&sc->ld_controller->mfi_io_lock);
	TAILQ_INSERT_TAIL(&sc->ld_controller->mfi_ld_tqh, sc, ld_link);
	TAILQ_FOREACH(ld_pend, &sc->ld_controller->mfi_ld_pend_tqh,
	    ld_link) {
		TAILQ_REMOVE(&sc->ld_controller->mfi_ld_pend_tqh,
		    ld_pend, ld_link);
		free(ld_pend, M_MFIBUF);
		break;
	}
	mtx_unlock(&sc->ld_controller->mfi_io_lock);

	switch (ld_info->ld_config.params.state) {
	case MFI_LD_STATE_OFFLINE:
		state = "offline";
		break;
	case MFI_LD_STATE_PARTIALLY_DEGRADED:
		state = "partially degraded";
		break;
	case MFI_LD_STATE_DEGRADED:
		state = "degraded";
		break;
	case MFI_LD_STATE_OPTIMAL:
		state = "optimal";
		break;
	default:
		state = "unknown";
		break;
	}

	if ( strlen(ld_info->ld_config.properties.name) == 0 ) {
		device_printf(dev,
		      "%juMB (%ju sectors) RAID volume (no label) is %s\n",
		       sectors / (1024 * 1024 / secsize), sectors, state);
	} else {
		device_printf(dev,
		      "%juMB (%ju sectors) RAID volume '%s' is %s\n",
		      sectors / (1024 * 1024 / secsize), sectors,
		      ld_info->ld_config.properties.name, state);
	}

	sc->ld_disk = disk_alloc();
	sc->ld_disk->d_drv1 = sc;
	sc->ld_disk->d_maxsize = min(sc->ld_controller->mfi_max_io * secsize,
	    (sc->ld_controller->mfi_max_sge - 1) * PAGE_SIZE);
	sc->ld_disk->d_name = "mfid";
	sc->ld_disk->d_open = mfi_disk_open;
	sc->ld_disk->d_close = mfi_disk_close;
	sc->ld_disk->d_strategy = mfi_disk_strategy;
	sc->ld_disk->d_dump = mfi_disk_dump;
	sc->ld_disk->d_unit = sc->ld_unit;
	sc->ld_disk->d_sectorsize = secsize;
	sc->ld_disk->d_mediasize = sectors * secsize;
	if (sc->ld_disk->d_mediasize >= (1 * 1024 * 1024)) {
		sc->ld_disk->d_fwheads = 255;
		sc->ld_disk->d_fwsectors = 63;
	} else {
		sc->ld_disk->d_fwheads = 64;
		sc->ld_disk->d_fwsectors = 32;
	}
	sc->ld_disk->d_flags = DISKFLAG_UNMAPPED_BIO;
	disk_create(sc->ld_disk, DISK_VERSION);

	return (0);
}

static int
mfi_disk_detach(device_t dev)
{
	struct mfi_disk *sc;

	sc = device_get_softc(dev);

	mtx_lock(&sc->ld_controller->mfi_io_lock);
	if (((sc->ld_disk->d_flags & DISKFLAG_OPEN) ||
	    (sc->ld_flags & MFI_DISK_FLAGS_OPEN)) &&
	    (sc->ld_controller->mfi_keep_deleted_volumes ||
	    sc->ld_controller->mfi_detaching)) {
		mtx_unlock(&sc->ld_controller->mfi_io_lock);
		return (EBUSY);
	}
	mtx_unlock(&sc->ld_controller->mfi_io_lock);

	disk_destroy(sc->ld_disk);
	mtx_lock(&sc->ld_controller->mfi_io_lock);
	TAILQ_REMOVE(&sc->ld_controller->mfi_ld_tqh, sc, ld_link);
	mtx_unlock(&sc->ld_controller->mfi_io_lock);
	free(sc->ld_info, M_MFIBUF);
	return (0);
}

static int
mfi_disk_open(struct disk *dp)
{
	struct mfi_disk *sc;
	int error;

	sc = dp->d_drv1;
	mtx_lock(&sc->ld_controller->mfi_io_lock);
	if (sc->ld_flags & MFI_DISK_FLAGS_DISABLED)
		error = ENXIO;
	else {
		sc->ld_flags |= MFI_DISK_FLAGS_OPEN;
		error = 0;
	}
	mtx_unlock(&sc->ld_controller->mfi_io_lock);

	return (error);
}

static int
mfi_disk_close(struct disk *dp)
{
	struct mfi_disk *sc;

	sc = dp->d_drv1;
	mtx_lock(&sc->ld_controller->mfi_io_lock);
	sc->ld_flags &= ~MFI_DISK_FLAGS_OPEN;
	mtx_unlock(&sc->ld_controller->mfi_io_lock);

	return (0);
}

int
mfi_disk_disable(struct mfi_disk *sc)
{

	mtx_assert(&sc->ld_controller->mfi_io_lock, MA_OWNED);
	if (sc->ld_flags & MFI_DISK_FLAGS_OPEN) {
		if (sc->ld_controller->mfi_delete_busy_volumes)
			return (0);
		device_printf(sc->ld_dev, "Unable to delete busy ld device\n");
		return (EBUSY);
	}
	sc->ld_flags |= MFI_DISK_FLAGS_DISABLED;
	return (0);
}

void
mfi_disk_enable(struct mfi_disk *sc)
{

	mtx_assert(&sc->ld_controller->mfi_io_lock, MA_OWNED);
	sc->ld_flags &= ~MFI_DISK_FLAGS_DISABLED;
}

static void
mfi_disk_strategy(struct bio *bio)
{
	struct mfi_disk *sc;
	struct mfi_softc *controller;

	sc = bio->bio_disk->d_drv1;

	if (sc == NULL) {
		bio->bio_error = EINVAL;
		bio->bio_flags |= BIO_ERROR;
		bio->bio_resid = bio->bio_bcount;
		biodone(bio);
		return;
	}

	controller = sc->ld_controller;
	if (controller->adpreset) {
		bio->bio_error = EBUSY;
		return;
	}

	if (controller->hw_crit_error) {
		bio->bio_error = EBUSY;
		return;
	}

	if (controller->issuepend_done == 0) {
		bio->bio_error = EBUSY;
		return;
	}

	bio->bio_driver1 = (void *)(uintptr_t)sc->ld_id;
	/* Mark it as LD IO */
	bio->bio_driver2 = (void *)MFI_LD_IO;
	mtx_lock(&controller->mfi_io_lock);
	mfi_enqueue_bio(controller, bio);
	mfi_startio(controller);
	mtx_unlock(&controller->mfi_io_lock);
	return;
}

void
mfi_disk_complete(struct bio *bio)
{
	struct mfi_disk *sc;
	struct mfi_frame_header *hdr;

	sc = bio->bio_disk->d_drv1;
	hdr = bio->bio_driver1;

	if (bio->bio_flags & BIO_ERROR) {
		bio->bio_resid = bio->bio_bcount;
		if (bio->bio_error == 0)
			bio->bio_error = EIO;
		disk_err(bio, "hard error", -1, 1);
	} else {
		bio->bio_resid = 0;
	}
	biodone(bio);
}

static int
mfi_disk_dump(void *arg, void *virt, vm_offset_t phys, off_t offset, size_t len)
{
	struct mfi_disk *sc;
	struct mfi_softc *parent_sc;
	struct disk *dp;
	int error;

	dp = arg;
	sc = dp->d_drv1;
	parent_sc = sc->ld_controller;

	if (len > 0) {
		if ((error = mfi_dump_blocks(parent_sc, sc->ld_id, offset /
		    MFI_SECTOR_LEN, virt, len)) != 0)
			return (error);
	} else {
		/* mfi_sync_cache(parent_sc, sc->ld_id); */
	}

	return (0);
}
