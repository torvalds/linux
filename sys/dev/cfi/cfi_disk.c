/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Sam Leffler, Errno Consulting
 * Copyright (c) 2012-2013, SRI International
 * All rights reserved.
 *
 * Portions of this software were developed by SRI International and the
 * University of Cambridge Computer Laboratory under DARPA/AFRL contract
 * (FA8750-10-C-0237) ("CTSRD"), as part of the DARPA CRASH research
 * programme.
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
#include <sys/kernel.h>
#include <sys/malloc.h>   
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>

#include <machine/bus.h>

#include <dev/cfi/cfi_var.h>

#include <geom/geom.h>
#include <geom/geom_disk.h>

struct cfi_disk_softc {
	struct cfi_softc *parent;
	struct disk	*disk;
	int		flags;
#define	CFI_DISK_OPEN	0x0001
	struct bio_queue_head bioq;	/* bio queue */
	struct mtx	qlock;		/* bioq lock */
	struct taskqueue *tq;		/* private task queue for i/o request */
	struct task	iotask;		/* i/o processing */
};

#define	CFI_DISK_SECSIZE	512
#define	CFI_DISK_MAXIOSIZE	65536

static int cfi_disk_detach(device_t);
static int cfi_disk_open(struct disk *);
static int cfi_disk_close(struct disk *);
static void cfi_io_proc(void *, int);
static int cfi_disk_getattr(struct bio *);
static void cfi_disk_strategy(struct bio *);
static int cfi_disk_ioctl(struct disk *, u_long, void *, int, struct thread *);

static int
cfi_disk_probe(device_t dev)
{
	return 0;
}

static int
cfi_disk_attach(device_t dev)
{
	struct cfi_disk_softc *sc = device_get_softc(dev);

	sc->parent = device_get_softc(device_get_parent(dev));
	/* validate interface width; assumed by other code */
	if (sc->parent->sc_width != 1 &&
	    sc->parent->sc_width != 2 &&
	    sc->parent->sc_width != 4)
		return EINVAL;

	sc->disk = disk_alloc();
	if (sc->disk == NULL)
		return ENOMEM;
	sc->disk->d_name = "cfid";
	sc->disk->d_unit = device_get_unit(dev);
	sc->disk->d_open = cfi_disk_open;
	sc->disk->d_close = cfi_disk_close;
	sc->disk->d_strategy = cfi_disk_strategy;
	sc->disk->d_ioctl = cfi_disk_ioctl;
	sc->disk->d_dump = NULL;		/* NB: no dumps */
	sc->disk->d_getattr = cfi_disk_getattr;
	sc->disk->d_sectorsize = CFI_DISK_SECSIZE;
	sc->disk->d_mediasize = sc->parent->sc_size;
	sc->disk->d_maxsize = CFI_DISK_MAXIOSIZE;
	/* NB: use stripesize to hold the erase/region size */
	if (sc->parent->sc_regions) {
		/*
		 * Multiple regions, use the last one.  This is a
		 * total hack as it's (presently) used only by
		 * geom_redboot to locate the FIS directory which
		 * lies at the start of the last erase region.
		 */
		sc->disk->d_stripesize =
		    sc->parent->sc_region[sc->parent->sc_regions-1].r_blksz;
	} else
		sc->disk->d_stripesize = sc->disk->d_mediasize;
	sc->disk->d_drv1 = sc;
	disk_create(sc->disk, DISK_VERSION);

	mtx_init(&sc->qlock, "CFID I/O lock", NULL, MTX_DEF);
	bioq_init(&sc->bioq);

	sc->tq = taskqueue_create("cfid_taskq", M_NOWAIT,
		taskqueue_thread_enqueue, &sc->tq);
	taskqueue_start_threads(&sc->tq, 1, PI_DISK, "cfid taskq");

	TASK_INIT(&sc->iotask, 0, cfi_io_proc, sc);

	return 0;
}

static int
cfi_disk_detach(device_t dev)
{
	struct cfi_disk_softc *sc = device_get_softc(dev);

	if (sc->flags & CFI_DISK_OPEN)
		return EBUSY;
	taskqueue_free(sc->tq);
	/* XXX drain bioq */
	disk_destroy(sc->disk);
	mtx_destroy(&sc->qlock);
	return 0;
}

static int
cfi_disk_open(struct disk *dp)
{
	struct cfi_disk_softc *sc = dp->d_drv1;

	/* XXX no interlock with /dev/cfi */
	sc->flags |= CFI_DISK_OPEN;
	return 0;
}

static int
cfi_disk_close(struct disk *dp)
{
	struct cfi_disk_softc *sc = dp->d_drv1;

	sc->flags &= ~CFI_DISK_OPEN;
	return 0;
}

static void
cfi_disk_read(struct cfi_softc *sc, struct bio *bp)
{
	long resid;

	KASSERT(sc->sc_width == 1 || sc->sc_width == 2 || sc->sc_width == 4,
	    ("sc_width %d", sc->sc_width));

	if (sc->sc_writing) {
		bp->bio_error = cfi_block_finish(sc);
		if (bp->bio_error) {
			bp->bio_flags |= BIO_ERROR;
			goto done;
		}
	}
	if (bp->bio_offset > sc->sc_size) {
		bp->bio_flags |= BIO_ERROR;
		bp->bio_error = EIO;
		goto done;
	}
	resid = bp->bio_bcount;
	if (sc->sc_width == 1) {
		uint8_t *dp = (uint8_t *)bp->bio_data;
		while (resid > 0 && bp->bio_offset < sc->sc_size) {
			*dp++ = cfi_read_raw(sc, bp->bio_offset);
			bp->bio_offset += 1, resid -= 1;
		}
	} else if (sc->sc_width == 2) {
		uint16_t *dp = (uint16_t *)bp->bio_data;
		while (resid > 0 && bp->bio_offset < sc->sc_size) {
			*dp++ = cfi_read_raw(sc, bp->bio_offset);
			bp->bio_offset += 2, resid -= 2;
		}
	} else {
		uint32_t *dp = (uint32_t *)bp->bio_data;
		while (resid > 0 && bp->bio_offset < sc->sc_size) {
			*dp++ = cfi_read_raw(sc, bp->bio_offset);
			bp->bio_offset += 4, resid -= 4;
		}
	}
	bp->bio_resid = resid;
done:
	biodone(bp);
}

static void
cfi_disk_write(struct cfi_softc *sc, struct bio *bp)
{
	long resid;
	u_int top;

	KASSERT(sc->sc_width == 1 || sc->sc_width == 2 || sc->sc_width == 4,
	    ("sc_width %d", sc->sc_width));

	if (bp->bio_offset > sc->sc_size) {
		bp->bio_flags |= BIO_ERROR;
		bp->bio_error = EIO;
		goto done;
	}
	resid = bp->bio_bcount;
	while (resid > 0) {
		/*
		 * Finish the current block if we're about to write
		 * to a different block.
		 */
		if (sc->sc_writing) {
			top = sc->sc_wrofs + sc->sc_wrbufsz;
			if (bp->bio_offset < sc->sc_wrofs ||
			    bp->bio_offset >= top)
				cfi_block_finish(sc);
		}

		/* Start writing to a (new) block if applicable. */
		if (!sc->sc_writing) {
			bp->bio_error = cfi_block_start(sc, bp->bio_offset);
			if (bp->bio_error) {
				bp->bio_flags |= BIO_ERROR;
				goto done;
			}
		}

		top = sc->sc_wrofs + sc->sc_wrbufsz;
		bcopy(bp->bio_data,
		    sc->sc_wrbuf + bp->bio_offset - sc->sc_wrofs,
		    MIN(top - bp->bio_offset, resid));
		resid -= MIN(top - bp->bio_offset, resid);
	}
	bp->bio_resid = resid;
done:
	biodone(bp);
}

static void
cfi_io_proc(void *arg, int pending)
{
	struct cfi_disk_softc *sc = arg;
	struct cfi_softc *cfi = sc->parent;
	struct bio *bp;

	for (;;) {
		mtx_lock(&sc->qlock);
		bp = bioq_takefirst(&sc->bioq);
		mtx_unlock(&sc->qlock);
		if (bp == NULL)
			break;

		switch (bp->bio_cmd) {
		case BIO_READ:
			cfi_disk_read(cfi, bp);
			break;
		case BIO_WRITE:
			cfi_disk_write(cfi, bp);
			break;
		}
	}
}

static int
cfi_disk_getattr(struct bio *bp)
{
	struct cfi_disk_softc *dsc;
	struct cfi_softc *sc;
	device_t dev;

	if (bp->bio_disk == NULL || bp->bio_disk->d_drv1 == NULL)
		return (ENXIO);

	dsc = bp->bio_disk->d_drv1;
	sc = dsc->parent;
	dev = sc->sc_dev;

	if (strcmp(bp->bio_attribute, "CFI::device") == 0) {
		if (bp->bio_length != sizeof(dev))
			return (EFAULT);
		bcopy(&dev, bp->bio_data, sizeof(dev));
	} else
		return (-1);
	return (0);
}


static void
cfi_disk_strategy(struct bio *bp)
{
	struct cfi_disk_softc *sc = bp->bio_disk->d_drv1;

	if (sc == NULL)
		goto invalid;
	if (bp->bio_bcount == 0) {
		bp->bio_resid = bp->bio_bcount;
		biodone(bp);
		return;
	}
	switch (bp->bio_cmd) {
	case BIO_READ:
	case BIO_WRITE:
		mtx_lock(&sc->qlock);
		/* no value in sorting requests? */
		bioq_insert_tail(&sc->bioq, bp);
		mtx_unlock(&sc->qlock);
		taskqueue_enqueue(sc->tq, &sc->iotask);
		return;
	}
	/* fall thru... */
invalid:
	bp->bio_flags |= BIO_ERROR;
	bp->bio_error = EINVAL;
	biodone(bp);
}

static int
cfi_disk_ioctl(struct disk *dp, u_long cmd, void *data, int fflag,
	struct thread *td)
{
	return EINVAL;
}

static device_method_t cfi_disk_methods[] = {
	DEVMETHOD(device_probe,		cfi_disk_probe),
	DEVMETHOD(device_attach,	cfi_disk_attach),
	DEVMETHOD(device_detach,	cfi_disk_detach),

	{ 0, 0 }
};
static driver_t cfi_disk_driver = {
	"cfid",
	cfi_disk_methods,
	sizeof(struct cfi_disk_softc),
};
DRIVER_MODULE(cfid, cfi, cfi_disk_driver, cfi_diskclass, 0, NULL);
