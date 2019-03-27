/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *            Copyright 1994-2009 The FreeBSD Project.
 *            All rights reserved.
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 *    THIS SOFTWARE IS PROVIDED BY THE FREEBSD PROJECT``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FREEBSD PROJECT OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY,OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION)HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * official policies,either expressed or implied, of the FreeBSD Project.
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

static int	mfi_syspd_probe(device_t dev);
static int	mfi_syspd_attach(device_t dev);
static int	mfi_syspd_detach(device_t dev);

static disk_open_t	mfi_syspd_open;
static disk_close_t	mfi_syspd_close;
static disk_strategy_t	mfi_syspd_strategy;
static dumper_t		mfi_syspd_dump;

static devclass_t	mfi_syspd_devclass;

static device_method_t mfi_syspd_methods[] = {
	DEVMETHOD(device_probe,		mfi_syspd_probe),
	DEVMETHOD(device_attach,	mfi_syspd_attach),
	DEVMETHOD(device_detach,	mfi_syspd_detach),
	{ 0, 0 }
};

static driver_t mfi_syspd_driver = {
	"mfisyspd",
	mfi_syspd_methods,
	sizeof(struct mfi_system_pd)
};

DRIVER_MODULE(mfisyspd, mfi, mfi_syspd_driver, mfi_syspd_devclass, 0, 0);

static int
mfi_syspd_probe(device_t dev)
{
	return (0);
}

static int
mfi_syspd_attach(device_t dev)
{
	struct mfi_system_pd *sc;
	struct mfi_pd_info *pd_info;
	struct mfi_system_pending *syspd_pend;
	uint64_t sectors;
	uint32_t secsize;

	sc = device_get_softc(dev);
	pd_info = device_get_ivars(dev);
	sc->pd_dev = dev;
	sc->pd_id = pd_info->ref.v.device_id;
	sc->pd_unit = device_get_unit(dev);
	sc->pd_info = pd_info;
	sc->pd_controller = device_get_softc(device_get_parent(dev));
	sc->pd_flags = 0;

	sectors = pd_info->raw_size;
	secsize = MFI_SECTOR_LEN;
	mtx_lock(&sc->pd_controller->mfi_io_lock);
	TAILQ_INSERT_TAIL(&sc->pd_controller->mfi_syspd_tqh, sc, pd_link);
	TAILQ_FOREACH(syspd_pend, &sc->pd_controller->mfi_syspd_pend_tqh,
	    pd_link) {
		TAILQ_REMOVE(&sc->pd_controller->mfi_syspd_pend_tqh,
		    syspd_pend, pd_link);
		free(syspd_pend, M_MFIBUF);
		break;
	}
	mtx_unlock(&sc->pd_controller->mfi_io_lock);
	device_printf(dev, "%juMB (%ju sectors) SYSPD volume (deviceid: %d)\n",
		      sectors / (1024 * 1024 / secsize), sectors, sc->pd_id);
	sc->pd_disk = disk_alloc();
	sc->pd_disk->d_drv1 = sc;
	sc->pd_disk->d_maxsize = min(sc->pd_controller->mfi_max_io * secsize,
		(sc->pd_controller->mfi_max_sge - 1) * PAGE_SIZE);
	sc->pd_disk->d_name = "mfisyspd";
	sc->pd_disk->d_open = mfi_syspd_open;
	sc->pd_disk->d_close = mfi_syspd_close;
	sc->pd_disk->d_strategy = mfi_syspd_strategy;
	sc->pd_disk->d_dump = mfi_syspd_dump;
	sc->pd_disk->d_unit = sc->pd_unit;
	sc->pd_disk->d_sectorsize = secsize;
	sc->pd_disk->d_mediasize = sectors * secsize;
	if (sc->pd_disk->d_mediasize >= (1 * 1024 * 1024)) {
		sc->pd_disk->d_fwheads = 255;
		sc->pd_disk->d_fwsectors = 63;
	} else {
		sc->pd_disk->d_fwheads = 64;
		sc->pd_disk->d_fwsectors = 32;
	}
	sc->pd_disk->d_flags = DISKFLAG_UNMAPPED_BIO;
	disk_create(sc->pd_disk, DISK_VERSION);

	device_printf(dev, " SYSPD volume attached\n");

	return (0);
}

static int
mfi_syspd_detach(device_t dev)
{
	struct mfi_system_pd *sc;

	sc = device_get_softc(dev);
	device_printf(dev, "Detaching syspd\n");
	mtx_lock(&sc->pd_controller->mfi_io_lock);
	if (((sc->pd_disk->d_flags & DISKFLAG_OPEN) ||
	    (sc->pd_flags & MFI_DISK_FLAGS_OPEN)) &&
	    (sc->pd_controller->mfi_keep_deleted_volumes ||
	    sc->pd_controller->mfi_detaching)) {
		mtx_unlock(&sc->pd_controller->mfi_io_lock);
		device_printf(dev, "Cant detach syspd\n");
		return (EBUSY);
	}
	mtx_unlock(&sc->pd_controller->mfi_io_lock);

	disk_destroy(sc->pd_disk);
	mtx_lock(&sc->pd_controller->mfi_io_lock);
	TAILQ_REMOVE(&sc->pd_controller->mfi_syspd_tqh, sc, pd_link);
	mtx_unlock(&sc->pd_controller->mfi_io_lock);
	free(sc->pd_info, M_MFIBUF);
	return (0);
}

static int
mfi_syspd_open(struct disk *dp)
{
	struct mfi_system_pd *sc;
	int error;

	sc = dp->d_drv1;
	mtx_lock(&sc->pd_controller->mfi_io_lock);
	if (sc->pd_flags & MFI_DISK_FLAGS_DISABLED)
		error = ENXIO;
	else {
		sc->pd_flags |= MFI_DISK_FLAGS_OPEN;
		error = 0;
	}
	mtx_unlock(&sc->pd_controller->mfi_io_lock);
	return (error);
}

static int
mfi_syspd_close(struct disk *dp)
{
	struct mfi_system_pd *sc;

	sc = dp->d_drv1;
	mtx_lock(&sc->pd_controller->mfi_io_lock);
	sc->pd_flags &= ~MFI_DISK_FLAGS_OPEN;
	mtx_unlock(&sc->pd_controller->mfi_io_lock);

	return (0);
}

int
mfi_syspd_disable(struct mfi_system_pd *sc)
{

	device_printf(sc->pd_dev, "syspd disable \n");
	mtx_assert(&sc->pd_controller->mfi_io_lock, MA_OWNED);
	if (sc->pd_flags & MFI_DISK_FLAGS_OPEN) {
		if (sc->pd_controller->mfi_delete_busy_volumes)
			return (0);
		device_printf(sc->pd_dev,
		    "Unable to delete busy syspd device\n");
		return (EBUSY);
	}
	sc->pd_flags |= MFI_DISK_FLAGS_DISABLED;
	return (0);
}

void
mfi_syspd_enable(struct mfi_system_pd *sc)
{

	device_printf(sc->pd_dev, "syspd enable \n");
	mtx_assert(&sc->pd_controller->mfi_io_lock, MA_OWNED);
	sc->pd_flags &= ~MFI_DISK_FLAGS_DISABLED;
}

static void
mfi_syspd_strategy(struct bio *bio)
{
	struct mfi_system_pd *sc;
	struct mfi_softc *controller;

	sc = bio->bio_disk->d_drv1;

	if (sc == NULL) {
		bio->bio_error = EINVAL;
		bio->bio_flags |= BIO_ERROR;
		bio->bio_resid = bio->bio_bcount;
		biodone(bio);
		return;
	}

	controller = sc->pd_controller;
	bio->bio_driver1 = (void *)(uintptr_t)sc->pd_id;
	/* Mark it as system PD IO */
	bio->bio_driver2 = (void *)MFI_SYS_PD_IO;
	mtx_lock(&controller->mfi_io_lock);
	mfi_enqueue_bio(controller, bio);
	mfi_startio(controller);
	mtx_unlock(&controller->mfi_io_lock);
	return;
}

static int
mfi_syspd_dump(void *arg, void *virt, vm_offset_t phys, off_t offset,
    size_t len)
{
	struct mfi_system_pd *sc;
	struct mfi_softc *parent_sc;
	struct disk *dp;
	int error;

	dp = arg;
	sc = dp->d_drv1;
	parent_sc = sc->pd_controller;

	if (len > 0) {
		if ((error = mfi_dump_syspd_blocks(parent_sc,
		    sc->pd_id, offset / MFI_SECTOR_LEN, virt, len)) != 0)
			return (error);
	} else {
		/* mfi_sync_cache(parent_sc, sc->ld_id); */
	}
	return (0);
}
