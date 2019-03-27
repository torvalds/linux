/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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
#include <sys/bus.h>
#include <sys/condvar.h>
#include <sys/conf.h>
#include <sys/bio.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <sys/systm.h>
#include <sys/taskqueue.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <geom/geom_disk.h>

#include <dev/altera/sdcard/altera_sdcard.h>

static int
altera_sdcard_disk_dump(void *arg, void *virtual, vm_offset_t physical,
    off_t offset, size_t length)
{

	panic("%s: not yet", __func__);
}

static int
altera_sdcard_disk_ioctl(struct disk *disk, u_long cmd, void *data, int fflag,
    struct thread *td)
{

	/* XXXRW: more here? */
	return (EINVAL);
}

static void
altera_sdcard_disk_strategy(struct bio *bp)
{
	struct altera_sdcard_softc *sc;

	/*
	 * Although the SD Card doesn't need sorting, we don't want to
	 * introduce barriers, so use bioq_disksort().
	 */
	sc = bp->bio_disk->d_drv1;
	ALTERA_SDCARD_LOCK(sc);
	switch (sc->as_state) {
	case ALTERA_SDCARD_STATE_NOCARD:
		device_printf(sc->as_dev, "%s: unexpected I/O on NOCARD",
		    __func__);
		biofinish(bp, NULL, ENXIO);
		break;

	case ALTERA_SDCARD_STATE_BADCARD:
		device_printf(sc->as_dev, "%s: unexpected I/O on BADCARD",
		    __func__);
		biofinish(bp, NULL, ENXIO);
		break;

	case ALTERA_SDCARD_STATE_DETACHED:
		device_printf(sc->as_dev, "%s: unexpected I/O on DETACHED",
		    __func__);
		biofinish(bp, NULL, ENXIO);

	case ALTERA_SDCARD_STATE_IDLE:
		bioq_disksort(&sc->as_bioq, bp);
		altera_sdcard_start(sc);
		break;

	case ALTERA_SDCARD_STATE_IO:
		bioq_disksort(&sc->as_bioq, bp);
		break;

	default:
		panic("%s: invalid state %d", __func__, sc->as_state);
	}
	ALTERA_SDCARD_UNLOCK(sc);
}

void
altera_sdcard_disk_insert(struct altera_sdcard_softc *sc)
{
	struct disk *disk;
	uint64_t size;

	ALTERA_SDCARD_LOCK_ASSERT(sc);

	/*
	 * Because the disk insertion routine occupies the driver instance's
	 * task queue thread, and the disk(9) instance isn't hooked up yet by
	 * definition, the only other source of events of concern is a thread
	 * initiating driver detach.  That thread has to issue a detach
	 * request and await an ACK from the taskqueue thread.  It is
	 * therefore safe to drop the lock here.
	 */
	ALTERA_SDCARD_UNLOCK(sc);
	disk = disk_alloc();
	disk->d_drv1 = sc;
	disk->d_name = "altera_sdcard";
	disk->d_unit = sc->as_unit;
	disk->d_strategy = altera_sdcard_disk_strategy;
	disk->d_dump = altera_sdcard_disk_dump;
	disk->d_ioctl = altera_sdcard_disk_ioctl;
	disk->d_sectorsize = ALTERA_SDCARD_SECTORSIZE;
	disk->d_mediasize = sc->as_mediasize;
	disk->d_maxsize = ALTERA_SDCARD_SECTORSIZE;
	sc->as_disk = disk;
	disk_create(disk, DISK_VERSION);
	ALTERA_SDCARD_LOCK(sc);

	/*
	 * Print a pretty-ish card insertion string.  We could stand to
	 * decorate this further, e.g., with card vendor information.
	 */
	size = sc->as_mediasize / (1000 * 1000);
	device_printf(sc->as_dev, "%juM SD Card inserted\n", (uintmax_t)size);
}

void
altera_sdcard_disk_remove(struct altera_sdcard_softc *sc)
{
	struct disk *disk;

	ALTERA_SDCARD_LOCK_ASSERT(sc);
	KASSERT(sc->as_disk != NULL, ("%s: as_disk NULL", __func__));

	/*
	 * sc->as_state will be updated by the caller.
	 *
	 * XXXRW: Is it OK to call disk_destroy() under the mutex, or should
	 * we be deferring that to the calling context once it is released?
	 */
	disk = sc->as_disk;
	disk_gone(disk);
	disk_destroy(disk);
	sc->as_disk = NULL;

	/*
	 * Cancel all outstanding I/O on the SD Card.
	 */
	if (sc->as_currentbio != NULL) {
		device_printf(sc->as_dev, "%s: SD Card removed during I/O",
		    __func__);
		biofinish(sc->as_currentbio, NULL, ENXIO);
		sc->as_currentbio = NULL;
	}
	bioq_flush(&sc->as_bioq, NULL, ENXIO);
	device_printf(sc->as_dev, "SD Card removed\n");
}
