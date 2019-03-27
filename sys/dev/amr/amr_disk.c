/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Jonathan Lemon
 * Copyright (c) 1999, 2000 Michael Smith
 * Copyright (c) 2000 BSDi
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
/*-
 * Copyright (c) 2002 Eric Moore
 * Copyright (c) 2002 LSI Logic Corporation
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
 * 3. The party using or redistributing the source code and binary forms
 *    agrees to the disclaimer below and the terms and conditions set forth
 *    herein.
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

/*
 * Disk driver for AMI MegaRaid controllers
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include <sys/bio.h>
#include <sys/bus.h>
#include <sys/conf.h>

#include <machine/bus.h>
#include <sys/rman.h>

#include <dev/amr/amrio.h>
#include <dev/amr/amrreg.h>
#include <dev/amr/amrvar.h>
#include <dev/amr/amr_tables.h>

/* prototypes */
static int amrd_probe(device_t dev);
static int amrd_attach(device_t dev);
static int amrd_detach(device_t dev);

static	disk_open_t	amrd_open;
static	disk_strategy_t	amrd_strategy;

static devclass_t	amrd_devclass;
#ifdef FREEBSD_4
int			amr_disks_registered = 0;
#endif

static device_method_t amrd_methods[] = {
    DEVMETHOD(device_probe,	amrd_probe),
    DEVMETHOD(device_attach,	amrd_attach),
    DEVMETHOD(device_detach,	amrd_detach),
    { 0, 0 }
};

static driver_t amrd_driver = {
    "amrd",
    amrd_methods,
    sizeof(struct amrd_softc)
};

DRIVER_MODULE(amrd, amr, amrd_driver, amrd_devclass, 0, 0);

static int
amrd_open(struct disk *dp)
{
    struct amrd_softc	*sc = (struct amrd_softc *)dp->d_drv1;

    debug_called(1);

    if (sc == NULL)
	return (ENXIO);

    /* controller not active? */
    if (sc->amrd_controller->amr_state & AMR_STATE_SHUTDOWN)
	return(ENXIO);

    return (0);
}
/********************************************************************************
 * System crashdump support
 */

static int
amrd_dump(void *arg, void *virtual, vm_offset_t physical, off_t offset, size_t length)
{

    struct amrd_softc	*amrd_sc;
    struct amr_softc	*amr_sc;
    int			error;
    struct disk		*dp;

    dp = arg;
    amrd_sc = (struct amrd_softc *)dp->d_drv1;
    if (amrd_sc == NULL)
	return(ENXIO);
    amr_sc  = (struct amr_softc *)amrd_sc->amrd_controller;

    if (length > 0) {
	int	driveno = amrd_sc->amrd_drive - amr_sc->amr_drive;
	if ((error = amr_dump_blocks(amr_sc,driveno,offset / AMR_BLKSIZE ,(void *)virtual,(int) length / AMR_BLKSIZE  )) != 0)
	    	return(error);

    }
    return(0);
}

/*
 * Read/write routine for a buffer.  Finds the proper unit, range checks
 * arguments, and schedules the transfer.  Does not wait for the transfer
 * to complete.  Multi-page transfers are supported.  All I/O requests must
 * be a multiple of a sector in length.
 */
static void
amrd_strategy(struct bio *bio)
{
    struct amrd_softc	*sc = (struct amrd_softc *)bio->bio_disk->d_drv1;

    /* bogus disk? */
    if (sc == NULL) {
	bio->bio_error = EINVAL;
	goto bad;
    }

    amr_submit_bio(sc->amrd_controller, bio);
    return;

 bad:
    bio->bio_flags |= BIO_ERROR;

    /*
     * Correctly set the buf to indicate a completed transfer
     */
    bio->bio_resid = bio->bio_bcount;
    biodone(bio);
    return;
}

void
amrd_intr(void *data)
{
    struct bio *bio = (struct bio *)data;

    debug_called(2);

    if (bio->bio_flags & BIO_ERROR) {
	bio->bio_error = EIO;
	debug(1, "i/o error\n");
    } else {
	bio->bio_resid = 0;
    }

    biodone(bio);
}

static int
amrd_probe(device_t dev)
{

    debug_called(1);

    device_set_desc(dev, "LSILogic MegaRAID logical drive");
    return (0);
}

static int
amrd_attach(device_t dev)
{
    struct amrd_softc	*sc = (struct amrd_softc *)device_get_softc(dev);
    device_t		parent;
    
    debug_called(1);

    parent = device_get_parent(dev);
    sc->amrd_controller = (struct amr_softc *)device_get_softc(parent);
    sc->amrd_unit = device_get_unit(dev);
    sc->amrd_drive = device_get_ivars(dev);
    sc->amrd_dev = dev;

    device_printf(dev, "%uMB (%u sectors) RAID %d (%s)\n",
		  sc->amrd_drive->al_size / ((1024 * 1024) / AMR_BLKSIZE),
		  sc->amrd_drive->al_size, sc->amrd_drive->al_properties & AMR_DRV_RAID_MASK, 
		  amr_describe_code(amr_table_drvstate, AMR_DRV_CURSTATE(sc->amrd_drive->al_state)));

    sc->amrd_disk = disk_alloc();
    sc->amrd_disk->d_drv1 = sc;
    sc->amrd_disk->d_maxsize = (AMR_NSEG - 1) * PAGE_SIZE;
    sc->amrd_disk->d_open = amrd_open;
    sc->amrd_disk->d_strategy = amrd_strategy;
    sc->amrd_disk->d_name = "amrd";
    sc->amrd_disk->d_dump = (dumper_t *)amrd_dump;
    sc->amrd_disk->d_unit = sc->amrd_unit;
    sc->amrd_disk->d_flags = DISKFLAG_CANFLUSHCACHE;
    sc->amrd_disk->d_sectorsize = AMR_BLKSIZE;
    sc->amrd_disk->d_mediasize = (off_t)sc->amrd_drive->al_size * AMR_BLKSIZE;
    sc->amrd_disk->d_fwsectors = sc->amrd_drive->al_sectors;
    sc->amrd_disk->d_fwheads = sc->amrd_drive->al_heads;
    disk_create(sc->amrd_disk, DISK_VERSION);

    return (0);
}

static int
amrd_detach(device_t dev)
{
    struct amrd_softc *sc = (struct amrd_softc *)device_get_softc(dev);

    debug_called(1);

    if (sc->amrd_disk->d_flags & DISKFLAG_OPEN)
	return(EBUSY);

#ifdef FREEBSD_4
    if (--amr_disks_registered == 0)
	cdevsw_remove(&amrddisk_cdevsw);
#else
    disk_destroy(sc->amrd_disk);
#endif
    return(0);
}

