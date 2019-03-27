/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Nathan Whitehorn.  All rights reserved.
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
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bio.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <geom/geom_disk.h>

#include <powerpc/mambo/mambocall.h>

struct mambodisk_softc {
	device_t dev;
	struct mtx sc_mtx;
	struct disk *disk;
	struct proc *p;
	struct bio_queue_head bio_queue;
	int running;
	int maxblocks;
};

#define MAMBO_DISK_READ		116
#define	MAMBO_DISK_WRITE	117
#define MAMBO_DISK_INFO		118

#define MAMBO_INFO_STATUS	1
#define MAMBO_INFO_BLKSZ	2
#define MAMBO_INFO_DEVSZ	3

/* bus entry points */
static void mambodisk_identify(driver_t *driver, device_t parent);
static int mambodisk_probe(device_t dev);
static int mambodisk_attach(device_t dev);

/* disk routines */
static int mambodisk_open(struct disk *dp);
static int mambodisk_close(struct disk *dp);
static void mambodisk_strategy(struct bio *bp);
static void mambodisk_task(void *arg);

#define MBODISK_LOCK(_sc)		mtx_lock(&(_sc)->sc_mtx)
#define	MBODISK_UNLOCK(_sc)	mtx_unlock(&(_sc)->sc_mtx)
#define MBODISK_LOCK_INIT(_sc) \
	mtx_init(&_sc->sc_mtx, device_get_nameunit(_sc->dev), \
	    "mambodisk", MTX_DEF)
#define MBODISK_LOCK_DESTROY(_sc)	mtx_destroy(&_sc->sc_mtx);
#define MBODISK_ASSERT_LOCKED(_sc) mtx_assert(&_sc->sc_mtx, MA_OWNED);
#define MBODISK_ASSERT_UNLOCKED(_sc) mtx_assert(&_sc->sc_mtx, MA_NOTOWNED);

static void
mambodisk_identify(driver_t *driver, device_t parent)
{
	int i = 0;

	for (i = 0; mambocall(MAMBO_DISK_INFO,MAMBO_INFO_DEVSZ,i) > 0; i++)
		BUS_ADD_CHILD(parent,0,"mambodisk",i);
}

static int
mambodisk_probe(device_t dev)
{
	device_set_desc(dev, "Mambo Simulated Block Device");
	return (0);
}

static int
mambodisk_attach(device_t dev)
{
	struct mambodisk_softc *sc;
	struct disk *d;
	intmax_t mb;
	char unit;

	sc = device_get_softc(dev);
	sc->dev = dev;
	MBODISK_LOCK_INIT(sc);

	d = sc->disk = disk_alloc();
	d->d_open = mambodisk_open;
	d->d_close = mambodisk_close;
	d->d_strategy = mambodisk_strategy;
	d->d_name = "mambodisk";
	d->d_drv1 = sc;
	d->d_maxsize = MAXPHYS;		/* Maybe ask bridge? */

	d->d_sectorsize = 512;
	sc->maxblocks = mambocall(MAMBO_DISK_INFO,MAMBO_INFO_BLKSZ,d->d_unit)
	    / 512;

	d->d_unit = device_get_unit(dev);
	d->d_mediasize = mambocall(MAMBO_DISK_INFO,MAMBO_INFO_DEVSZ,d->d_unit)
	    * 1024ULL; /* Mambo gives size in KB */

	mb = d->d_mediasize >> 20;	/* 1MiB == 1 << 20 */
	unit = 'M';
	if (mb >= 10240) {		/* 1GiB = 1024 MiB */
		unit = 'G';
		mb /= 1024;
	}
	device_printf(dev, "%ju%cB, %d byte sectors\n", mb, unit, 
	    d->d_sectorsize);
	disk_create(d, DISK_VERSION);
	bioq_init(&sc->bio_queue);

	sc->running = 1;
	kproc_create(&mambodisk_task, sc, &sc->p, 0, 0, "task: mambo hd");

	return (0);
}

static int
mambodisk_detach(device_t dev)
{
	struct mambodisk_softc *sc = device_get_softc(dev);

	/* kill thread */
	MBODISK_LOCK(sc);
	sc->running = 0;
	wakeup(sc);
	MBODISK_UNLOCK(sc);

	/* wait for thread to finish.  XXX probably want timeout.  -sorbo */
	MBODISK_LOCK(sc);
	while (sc->running != -1)
		msleep(sc, &sc->sc_mtx, PRIBIO, "detach", 0);
	MBODISK_UNLOCK(sc);

	/* kill disk */
	disk_destroy(sc->disk);
	/* XXX destroy anything in queue */

	MBODISK_LOCK_DESTROY(sc);

	return (0);
}

static int
mambodisk_open(struct disk *dp)
{
	return (0);
}

static int
mambodisk_close(struct disk *dp)
{
	return (0);
}

static void
mambodisk_strategy(struct bio *bp)
{
	struct mambodisk_softc *sc;

	sc = (struct mambodisk_softc *)bp->bio_disk->d_drv1;
	MBODISK_LOCK(sc);
	bioq_disksort(&sc->bio_queue, bp);
	wakeup(sc);
	MBODISK_UNLOCK(sc);
}

static void
mambodisk_task(void *arg)
{
	struct mambodisk_softc *sc = (struct mambodisk_softc*)arg;
	struct bio *bp;
	size_t sz;
	int result;
	daddr_t block, end;
	device_t dev;
	u_long unit;

	dev = sc->dev;
	unit = device_get_unit(dev);

	while (sc->running) {
		MBODISK_LOCK(sc);
		do {
			bp = bioq_first(&sc->bio_queue);
			if (bp == NULL)
				msleep(sc, &sc->sc_mtx, PRIBIO, "jobqueue", 0);
		} while (bp == NULL && sc->running);
		if (bp)
			bioq_remove(&sc->bio_queue, bp);
		MBODISK_UNLOCK(sc);
		if (!sc->running)
			break;
		sz = sc->disk->d_sectorsize;
		end = bp->bio_pblkno + (bp->bio_bcount / sz);
		for (block = bp->bio_pblkno; block < end;) {
			u_long numblocks;
			char *vaddr = bp->bio_data + 
			    (block - bp->bio_pblkno) * sz;

			numblocks = end - block;
			if (numblocks > sc->maxblocks)
				numblocks = sc->maxblocks;

			if (bp->bio_cmd == BIO_READ) {
				result = mambocall(MAMBO_DISK_READ, vaddr, 
				  (u_long)block, (numblocks << 16) | unit);
			} else if (bp->bio_cmd == BIO_WRITE) {
				result = mambocall(MAMBO_DISK_WRITE, vaddr, 
				  (u_long)block, (numblocks << 16) | unit);
			} else {
				result = 1;
			}
		
			if (result)
				break;

			block += numblocks;
		}
		if (block < end) {
			bp->bio_error = EIO;
			bp->bio_resid = (end - block) * sz;
			bp->bio_flags |= BIO_ERROR;
		}
		biodone(bp);
	}

	/* tell parent we're done */
	MBODISK_LOCK(sc);
	sc->running = -1;
	wakeup(sc);
	MBODISK_UNLOCK(sc);

	kproc_exit(0);
}

static device_method_t mambodisk_methods[] = {
	DEVMETHOD(device_identify,	mambodisk_identify),
	DEVMETHOD(device_probe,		mambodisk_probe),
	DEVMETHOD(device_attach,	mambodisk_attach),
	DEVMETHOD(device_detach,	mambodisk_detach),
	{0, 0},
};

static driver_t mambodisk_driver = {
	"mambodisk",
	mambodisk_methods,
	sizeof(struct mambodisk_softc),
};
static devclass_t mambodisk_devclass;

DRIVER_MODULE(mambodisk, mambo, mambodisk_driver, mambodisk_devclass, 0, 0);
