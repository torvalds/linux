/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011, Bryan Venteicher <bryanv@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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

/* Driver for VirtIO block devices. */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bio.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/sglist.h>
#include <sys/sysctl.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/queue.h>

#include <geom/geom.h>
#include <geom/geom_disk.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <dev/virtio/virtio.h>
#include <dev/virtio/virtqueue.h>
#include <dev/virtio/block/virtio_blk.h>

#include "virtio_if.h"

struct vtblk_request {
	struct virtio_blk_outhdr	 vbr_hdr;
	struct bio			*vbr_bp;
	uint8_t				 vbr_ack;
	TAILQ_ENTRY(vtblk_request)	 vbr_link;
};

enum vtblk_cache_mode {
	VTBLK_CACHE_WRITETHROUGH,
	VTBLK_CACHE_WRITEBACK,
	VTBLK_CACHE_MAX
};

struct vtblk_softc {
	device_t		 vtblk_dev;
	struct mtx		 vtblk_mtx;
	uint64_t		 vtblk_features;
	uint32_t		 vtblk_flags;
#define VTBLK_FLAG_INDIRECT	0x0001
#define VTBLK_FLAG_READONLY	0x0002
#define VTBLK_FLAG_DETACH	0x0004
#define VTBLK_FLAG_SUSPEND	0x0008
#define VTBLK_FLAG_BARRIER	0x0010
#define VTBLK_FLAG_WC_CONFIG	0x0020

	struct virtqueue	*vtblk_vq;
	struct sglist		*vtblk_sglist;
	struct disk		*vtblk_disk;

	struct bio_queue_head	 vtblk_bioq;
	TAILQ_HEAD(, vtblk_request)
				 vtblk_req_free;
	TAILQ_HEAD(, vtblk_request)
				 vtblk_req_ready;
	struct vtblk_request	*vtblk_req_ordered;

	int			 vtblk_max_nsegs;
	int			 vtblk_request_count;
	enum vtblk_cache_mode	 vtblk_write_cache;

	struct bio_queue	 vtblk_dump_queue;
	struct vtblk_request	 vtblk_dump_request;
};

static struct virtio_feature_desc vtblk_feature_desc[] = {
	{ VIRTIO_BLK_F_BARRIER,		"HostBarrier"	},
	{ VIRTIO_BLK_F_SIZE_MAX,	"MaxSegSize"	},
	{ VIRTIO_BLK_F_SEG_MAX,		"MaxNumSegs"	},
	{ VIRTIO_BLK_F_GEOMETRY,	"DiskGeometry"	},
	{ VIRTIO_BLK_F_RO,		"ReadOnly"	},
	{ VIRTIO_BLK_F_BLK_SIZE,	"BlockSize"	},
	{ VIRTIO_BLK_F_SCSI,		"SCSICmds"	},
	{ VIRTIO_BLK_F_WCE,		"WriteCache"	},
	{ VIRTIO_BLK_F_TOPOLOGY,	"Topology"	},
	{ VIRTIO_BLK_F_CONFIG_WCE,	"ConfigWCE"	},

	{ 0, NULL }
};

static int	vtblk_modevent(module_t, int, void *);

static int	vtblk_probe(device_t);
static int	vtblk_attach(device_t);
static int	vtblk_detach(device_t);
static int	vtblk_suspend(device_t);
static int	vtblk_resume(device_t);
static int	vtblk_shutdown(device_t);
static int	vtblk_config_change(device_t);

static int	vtblk_open(struct disk *);
static int	vtblk_close(struct disk *);
static int	vtblk_ioctl(struct disk *, u_long, void *, int,
		    struct thread *);
static int	vtblk_dump(void *, void *, vm_offset_t, off_t, size_t);
static void	vtblk_strategy(struct bio *);

static void	vtblk_negotiate_features(struct vtblk_softc *);
static void	vtblk_setup_features(struct vtblk_softc *);
static int	vtblk_maximum_segments(struct vtblk_softc *,
		    struct virtio_blk_config *);
static int	vtblk_alloc_virtqueue(struct vtblk_softc *);
static void	vtblk_resize_disk(struct vtblk_softc *, uint64_t);
static void	vtblk_alloc_disk(struct vtblk_softc *,
		    struct virtio_blk_config *);
static void	vtblk_create_disk(struct vtblk_softc *);

static int	vtblk_request_prealloc(struct vtblk_softc *);
static void	vtblk_request_free(struct vtblk_softc *);
static struct vtblk_request *
		vtblk_request_dequeue(struct vtblk_softc *);
static void	vtblk_request_enqueue(struct vtblk_softc *,
		    struct vtblk_request *);
static struct vtblk_request *
		vtblk_request_next_ready(struct vtblk_softc *);
static void	vtblk_request_requeue_ready(struct vtblk_softc *,
		    struct vtblk_request *);
static struct vtblk_request *
		vtblk_request_next(struct vtblk_softc *);
static struct vtblk_request *
		vtblk_request_bio(struct vtblk_softc *);
static int	vtblk_request_execute(struct vtblk_softc *,
		    struct vtblk_request *);
static int	vtblk_request_error(struct vtblk_request *);

static void	vtblk_queue_completed(struct vtblk_softc *,
		    struct bio_queue *);
static void	vtblk_done_completed(struct vtblk_softc *,
		    struct bio_queue *);
static void	vtblk_drain_vq(struct vtblk_softc *);
static void	vtblk_drain(struct vtblk_softc *);

static void	vtblk_startio(struct vtblk_softc *);
static void	vtblk_bio_done(struct vtblk_softc *, struct bio *, int);

static void	vtblk_read_config(struct vtblk_softc *,
		    struct virtio_blk_config *);
static void	vtblk_ident(struct vtblk_softc *);
static int	vtblk_poll_request(struct vtblk_softc *,
		    struct vtblk_request *);
static int	vtblk_quiesce(struct vtblk_softc *);
static void	vtblk_vq_intr(void *);
static void	vtblk_stop(struct vtblk_softc *);

static void	vtblk_dump_quiesce(struct vtblk_softc *);
static int	vtblk_dump_write(struct vtblk_softc *, void *, off_t, size_t);
static int	vtblk_dump_flush(struct vtblk_softc *);
static void	vtblk_dump_complete(struct vtblk_softc *);

static void	vtblk_set_write_cache(struct vtblk_softc *, int);
static int	vtblk_write_cache_enabled(struct vtblk_softc *sc,
		    struct virtio_blk_config *);
static int	vtblk_write_cache_sysctl(SYSCTL_HANDLER_ARGS);

static void	vtblk_setup_sysctl(struct vtblk_softc *);
static int	vtblk_tunable_int(struct vtblk_softc *, const char *, int);

/* Tunables. */
static int vtblk_no_ident = 0;
TUNABLE_INT("hw.vtblk.no_ident", &vtblk_no_ident);
static int vtblk_writecache_mode = -1;
TUNABLE_INT("hw.vtblk.writecache_mode", &vtblk_writecache_mode);

/* Features desired/implemented by this driver. */
#define VTBLK_FEATURES \
    (VIRTIO_BLK_F_BARRIER		| \
     VIRTIO_BLK_F_SIZE_MAX		| \
     VIRTIO_BLK_F_SEG_MAX		| \
     VIRTIO_BLK_F_GEOMETRY		| \
     VIRTIO_BLK_F_RO			| \
     VIRTIO_BLK_F_BLK_SIZE		| \
     VIRTIO_BLK_F_WCE			| \
     VIRTIO_BLK_F_TOPOLOGY		| \
     VIRTIO_BLK_F_CONFIG_WCE		| \
     VIRTIO_RING_F_INDIRECT_DESC)

#define VTBLK_MTX(_sc)		&(_sc)->vtblk_mtx
#define VTBLK_LOCK_INIT(_sc, _name) \
				mtx_init(VTBLK_MTX((_sc)), (_name), \
				    "VirtIO Block Lock", MTX_DEF)
#define VTBLK_LOCK(_sc)		mtx_lock(VTBLK_MTX((_sc)))
#define VTBLK_UNLOCK(_sc)	mtx_unlock(VTBLK_MTX((_sc)))
#define VTBLK_LOCK_DESTROY(_sc)	mtx_destroy(VTBLK_MTX((_sc)))
#define VTBLK_LOCK_ASSERT(_sc)	mtx_assert(VTBLK_MTX((_sc)), MA_OWNED)
#define VTBLK_LOCK_ASSERT_NOTOWNED(_sc) \
				mtx_assert(VTBLK_MTX((_sc)), MA_NOTOWNED)

#define VTBLK_DISK_NAME		"vtbd"
#define VTBLK_QUIESCE_TIMEOUT	(30 * hz)

/*
 * Each block request uses at least two segments - one for the header
 * and one for the status.
 */
#define VTBLK_MIN_SEGMENTS	2

static device_method_t vtblk_methods[] = {
	/* Device methods. */
	DEVMETHOD(device_probe,		vtblk_probe),
	DEVMETHOD(device_attach,	vtblk_attach),
	DEVMETHOD(device_detach,	vtblk_detach),
	DEVMETHOD(device_suspend,	vtblk_suspend),
	DEVMETHOD(device_resume,	vtblk_resume),
	DEVMETHOD(device_shutdown,	vtblk_shutdown),

	/* VirtIO methods. */
	DEVMETHOD(virtio_config_change,	vtblk_config_change),

	DEVMETHOD_END
};

static driver_t vtblk_driver = {
	"vtblk",
	vtblk_methods,
	sizeof(struct vtblk_softc)
};
static devclass_t vtblk_devclass;

DRIVER_MODULE(virtio_blk, virtio_mmio, vtblk_driver, vtblk_devclass,
    vtblk_modevent, 0);
DRIVER_MODULE(virtio_blk, virtio_pci, vtblk_driver, vtblk_devclass,
    vtblk_modevent, 0);
MODULE_VERSION(virtio_blk, 1);
MODULE_DEPEND(virtio_blk, virtio, 1, 1, 1);

static int
vtblk_modevent(module_t mod, int type, void *unused)
{
	int error;

	error = 0;

	switch (type) {
	case MOD_LOAD:
	case MOD_QUIESCE:
	case MOD_UNLOAD:
	case MOD_SHUTDOWN:
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}

	return (error);
}

static int
vtblk_probe(device_t dev)
{

	if (virtio_get_device_type(dev) != VIRTIO_ID_BLOCK)
		return (ENXIO);

	device_set_desc(dev, "VirtIO Block Adapter");

	return (BUS_PROBE_DEFAULT);
}

static int
vtblk_attach(device_t dev)
{
	struct vtblk_softc *sc;
	struct virtio_blk_config blkcfg;
	int error;

	virtio_set_feature_desc(dev, vtblk_feature_desc);

	sc = device_get_softc(dev);
	sc->vtblk_dev = dev;
	VTBLK_LOCK_INIT(sc, device_get_nameunit(dev));
	bioq_init(&sc->vtblk_bioq);
	TAILQ_INIT(&sc->vtblk_dump_queue);
	TAILQ_INIT(&sc->vtblk_req_free);
	TAILQ_INIT(&sc->vtblk_req_ready);

	vtblk_setup_sysctl(sc);
	vtblk_setup_features(sc);

	vtblk_read_config(sc, &blkcfg);

	/*
	 * With the current sglist(9) implementation, it is not easy
	 * for us to support a maximum segment size as adjacent
	 * segments are coalesced. For now, just make sure it's larger
	 * than the maximum supported transfer size.
	 */
	if (virtio_with_feature(dev, VIRTIO_BLK_F_SIZE_MAX)) {
		if (blkcfg.size_max < MAXPHYS) {
			error = ENOTSUP;
			device_printf(dev, "host requires unsupported "
			    "maximum segment size feature\n");
			goto fail;
		}
	}

	sc->vtblk_max_nsegs = vtblk_maximum_segments(sc, &blkcfg);
	if (sc->vtblk_max_nsegs <= VTBLK_MIN_SEGMENTS) {
		error = EINVAL;
		device_printf(dev, "fewer than minimum number of segments "
		    "allowed: %d\n", sc->vtblk_max_nsegs);
		goto fail;
	}

	sc->vtblk_sglist = sglist_alloc(sc->vtblk_max_nsegs, M_NOWAIT);
	if (sc->vtblk_sglist == NULL) {
		error = ENOMEM;
		device_printf(dev, "cannot allocate sglist\n");
		goto fail;
	}

	error = vtblk_alloc_virtqueue(sc);
	if (error) {
		device_printf(dev, "cannot allocate virtqueue\n");
		goto fail;
	}

	error = vtblk_request_prealloc(sc);
	if (error) {
		device_printf(dev, "cannot preallocate requests\n");
		goto fail;
	}

	vtblk_alloc_disk(sc, &blkcfg);

	error = virtio_setup_intr(dev, INTR_TYPE_BIO | INTR_ENTROPY);
	if (error) {
		device_printf(dev, "cannot setup virtqueue interrupt\n");
		goto fail;
	}

	vtblk_create_disk(sc);

	virtqueue_enable_intr(sc->vtblk_vq);

fail:
	if (error)
		vtblk_detach(dev);

	return (error);
}

static int
vtblk_detach(device_t dev)
{
	struct vtblk_softc *sc;

	sc = device_get_softc(dev);

	VTBLK_LOCK(sc);
	sc->vtblk_flags |= VTBLK_FLAG_DETACH;
	if (device_is_attached(dev))
		vtblk_stop(sc);
	VTBLK_UNLOCK(sc);

	vtblk_drain(sc);

	if (sc->vtblk_disk != NULL) {
		disk_destroy(sc->vtblk_disk);
		sc->vtblk_disk = NULL;
	}

	if (sc->vtblk_sglist != NULL) {
		sglist_free(sc->vtblk_sglist);
		sc->vtblk_sglist = NULL;
	}

	VTBLK_LOCK_DESTROY(sc);

	return (0);
}

static int
vtblk_suspend(device_t dev)
{
	struct vtblk_softc *sc;
	int error;

	sc = device_get_softc(dev);

	VTBLK_LOCK(sc);
	sc->vtblk_flags |= VTBLK_FLAG_SUSPEND;
	/* XXX BMV: virtio_stop(), etc needed here? */
	error = vtblk_quiesce(sc);
	if (error)
		sc->vtblk_flags &= ~VTBLK_FLAG_SUSPEND;
	VTBLK_UNLOCK(sc);

	return (error);
}

static int
vtblk_resume(device_t dev)
{
	struct vtblk_softc *sc;

	sc = device_get_softc(dev);

	VTBLK_LOCK(sc);
	/* XXX BMV: virtio_reinit(), etc needed here? */
	sc->vtblk_flags &= ~VTBLK_FLAG_SUSPEND;
	vtblk_startio(sc);
	VTBLK_UNLOCK(sc);

	return (0);
}

static int
vtblk_shutdown(device_t dev)
{

	return (0);
}

static int
vtblk_config_change(device_t dev)
{
	struct vtblk_softc *sc;
	struct virtio_blk_config blkcfg;
	uint64_t capacity;

	sc = device_get_softc(dev);

	vtblk_read_config(sc, &blkcfg);

	/* Capacity is always in 512-byte units. */
	capacity = blkcfg.capacity * 512;

	if (sc->vtblk_disk->d_mediasize != capacity)
		vtblk_resize_disk(sc, capacity);

	return (0);
}

static int
vtblk_open(struct disk *dp)
{
	struct vtblk_softc *sc;

	if ((sc = dp->d_drv1) == NULL)
		return (ENXIO);

	return (sc->vtblk_flags & VTBLK_FLAG_DETACH ? ENXIO : 0);
}

static int
vtblk_close(struct disk *dp)
{
	struct vtblk_softc *sc;

	if ((sc = dp->d_drv1) == NULL)
		return (ENXIO);

	return (0);
}

static int
vtblk_ioctl(struct disk *dp, u_long cmd, void *addr, int flag,
    struct thread *td)
{
	struct vtblk_softc *sc;

	if ((sc = dp->d_drv1) == NULL)
		return (ENXIO);

	return (ENOTTY);
}

static int
vtblk_dump(void *arg, void *virtual, vm_offset_t physical, off_t offset,
    size_t length)
{
	struct disk *dp;
	struct vtblk_softc *sc;
	int error;

	dp = arg;
	error = 0;

	if ((sc = dp->d_drv1) == NULL)
		return (ENXIO);

	VTBLK_LOCK(sc);

	vtblk_dump_quiesce(sc);

	if (length > 0)
		error = vtblk_dump_write(sc, virtual, offset, length);
	if (error || (virtual == NULL && offset == 0))
		vtblk_dump_complete(sc);

	VTBLK_UNLOCK(sc);

	return (error);
}

static void
vtblk_strategy(struct bio *bp)
{
	struct vtblk_softc *sc;

	if ((sc = bp->bio_disk->d_drv1) == NULL) {
		vtblk_bio_done(NULL, bp, EINVAL);
		return;
	}

	/*
	 * Fail any write if RO. Unfortunately, there does not seem to
	 * be a better way to report our readonly'ness to GEOM above.
	 */
	if (sc->vtblk_flags & VTBLK_FLAG_READONLY &&
	    (bp->bio_cmd == BIO_WRITE || bp->bio_cmd == BIO_FLUSH)) {
		vtblk_bio_done(sc, bp, EROFS);
		return;
	}

	VTBLK_LOCK(sc);

	if (sc->vtblk_flags & VTBLK_FLAG_DETACH) {
		VTBLK_UNLOCK(sc);
		vtblk_bio_done(sc, bp, ENXIO);
		return;
	}

	bioq_insert_tail(&sc->vtblk_bioq, bp);
	vtblk_startio(sc);

	VTBLK_UNLOCK(sc);
}

static void
vtblk_negotiate_features(struct vtblk_softc *sc)
{
	device_t dev;
	uint64_t features;

	dev = sc->vtblk_dev;
	features = VTBLK_FEATURES;

	sc->vtblk_features = virtio_negotiate_features(dev, features);
}

static void
vtblk_setup_features(struct vtblk_softc *sc)
{
	device_t dev;

	dev = sc->vtblk_dev;

	vtblk_negotiate_features(sc);

	if (virtio_with_feature(dev, VIRTIO_RING_F_INDIRECT_DESC))
		sc->vtblk_flags |= VTBLK_FLAG_INDIRECT;
	if (virtio_with_feature(dev, VIRTIO_BLK_F_RO))
		sc->vtblk_flags |= VTBLK_FLAG_READONLY;
	if (virtio_with_feature(dev, VIRTIO_BLK_F_BARRIER))
		sc->vtblk_flags |= VTBLK_FLAG_BARRIER;
	if (virtio_with_feature(dev, VIRTIO_BLK_F_CONFIG_WCE))
		sc->vtblk_flags |= VTBLK_FLAG_WC_CONFIG;
}

static int
vtblk_maximum_segments(struct vtblk_softc *sc,
    struct virtio_blk_config *blkcfg)
{
	device_t dev;
	int nsegs;

	dev = sc->vtblk_dev;
	nsegs = VTBLK_MIN_SEGMENTS;

	if (virtio_with_feature(dev, VIRTIO_BLK_F_SEG_MAX)) {
		nsegs += MIN(blkcfg->seg_max, MAXPHYS / PAGE_SIZE + 1);
		if (sc->vtblk_flags & VTBLK_FLAG_INDIRECT)
			nsegs = MIN(nsegs, VIRTIO_MAX_INDIRECT);
	} else
		nsegs += 1;

	return (nsegs);
}

static int
vtblk_alloc_virtqueue(struct vtblk_softc *sc)
{
	device_t dev;
	struct vq_alloc_info vq_info;

	dev = sc->vtblk_dev;

	VQ_ALLOC_INFO_INIT(&vq_info, sc->vtblk_max_nsegs,
	    vtblk_vq_intr, sc, &sc->vtblk_vq,
	    "%s request", device_get_nameunit(dev));

	return (virtio_alloc_virtqueues(dev, 0, 1, &vq_info));
}

static void
vtblk_resize_disk(struct vtblk_softc *sc, uint64_t new_capacity)
{
	device_t dev;
	struct disk *dp;
	int error;

	dev = sc->vtblk_dev;
	dp = sc->vtblk_disk;

	dp->d_mediasize = new_capacity;
	if (bootverbose) {
		device_printf(dev, "resized to %juMB (%ju %u byte sectors)\n",
		    (uintmax_t) dp->d_mediasize >> 20,
		    (uintmax_t) dp->d_mediasize / dp->d_sectorsize,
		    dp->d_sectorsize);
	}

	error = disk_resize(dp, M_NOWAIT);
	if (error) {
		device_printf(dev,
		    "disk_resize(9) failed, error: %d\n", error);
	}
}

static void
vtblk_alloc_disk(struct vtblk_softc *sc, struct virtio_blk_config *blkcfg)
{
	device_t dev;
	struct disk *dp;

	dev = sc->vtblk_dev;

	sc->vtblk_disk = dp = disk_alloc();
	dp->d_open = vtblk_open;
	dp->d_close = vtblk_close;
	dp->d_ioctl = vtblk_ioctl;
	dp->d_strategy = vtblk_strategy;
	dp->d_name = VTBLK_DISK_NAME;
	dp->d_unit = device_get_unit(dev);
	dp->d_drv1 = sc;
	dp->d_flags = DISKFLAG_CANFLUSHCACHE | DISKFLAG_UNMAPPED_BIO |
	    DISKFLAG_DIRECT_COMPLETION;
	dp->d_hba_vendor = virtio_get_vendor(dev);
	dp->d_hba_device = virtio_get_device(dev);
	dp->d_hba_subvendor = virtio_get_subvendor(dev);
	dp->d_hba_subdevice = virtio_get_subdevice(dev);

	if ((sc->vtblk_flags & VTBLK_FLAG_READONLY) == 0)
		dp->d_dump = vtblk_dump;

	/* Capacity is always in 512-byte units. */
	dp->d_mediasize = blkcfg->capacity * 512;

	if (virtio_with_feature(dev, VIRTIO_BLK_F_BLK_SIZE))
		dp->d_sectorsize = blkcfg->blk_size;
	else
		dp->d_sectorsize = 512;

	/*
	 * The VirtIO maximum I/O size is given in terms of segments.
	 * However, FreeBSD limits I/O size by logical buffer size, not
	 * by physically contiguous pages. Therefore, we have to assume
	 * no pages are contiguous. This may impose an artificially low
	 * maximum I/O size. But in practice, since QEMU advertises 128
	 * segments, this gives us a maximum IO size of 125 * PAGE_SIZE,
	 * which is typically greater than MAXPHYS. Eventually we should
	 * just advertise MAXPHYS and split buffers that are too big.
	 *
	 * Note we must subtract one additional segment in case of non
	 * page aligned buffers.
	 */
	dp->d_maxsize = (sc->vtblk_max_nsegs - VTBLK_MIN_SEGMENTS - 1) *
	    PAGE_SIZE;
	if (dp->d_maxsize < PAGE_SIZE)
		dp->d_maxsize = PAGE_SIZE; /* XXX */

	if (virtio_with_feature(dev, VIRTIO_BLK_F_GEOMETRY)) {
		dp->d_fwsectors = blkcfg->geometry.sectors;
		dp->d_fwheads = blkcfg->geometry.heads;
	}

	if (virtio_with_feature(dev, VIRTIO_BLK_F_TOPOLOGY) &&
	    blkcfg->topology.physical_block_exp > 0) {
		dp->d_stripesize = dp->d_sectorsize *
		    (1 << blkcfg->topology.physical_block_exp);
		dp->d_stripeoffset = (dp->d_stripesize -
		    blkcfg->topology.alignment_offset * dp->d_sectorsize) %
		    dp->d_stripesize;
	}

	if (vtblk_write_cache_enabled(sc, blkcfg) != 0)
		sc->vtblk_write_cache = VTBLK_CACHE_WRITEBACK;
	else
		sc->vtblk_write_cache = VTBLK_CACHE_WRITETHROUGH;
}

static void
vtblk_create_disk(struct vtblk_softc *sc)
{
	struct disk *dp;

	dp = sc->vtblk_disk;

	vtblk_ident(sc);

	device_printf(sc->vtblk_dev, "%juMB (%ju %u byte sectors)\n",
	    (uintmax_t) dp->d_mediasize >> 20,
	    (uintmax_t) dp->d_mediasize / dp->d_sectorsize,
	    dp->d_sectorsize);

	disk_create(dp, DISK_VERSION);
}

static int
vtblk_request_prealloc(struct vtblk_softc *sc)
{
	struct vtblk_request *req;
	int i, nreqs;

	nreqs = virtqueue_size(sc->vtblk_vq);

	/*
	 * Preallocate sufficient requests to keep the virtqueue full. Each
	 * request consumes VTBLK_MIN_SEGMENTS or more descriptors so reduce
	 * the number allocated when indirect descriptors are not available.
	 */
	if ((sc->vtblk_flags & VTBLK_FLAG_INDIRECT) == 0)
		nreqs /= VTBLK_MIN_SEGMENTS;

	for (i = 0; i < nreqs; i++) {
		req = malloc(sizeof(struct vtblk_request), M_DEVBUF, M_NOWAIT);
		if (req == NULL)
			return (ENOMEM);

		MPASS(sglist_count(&req->vbr_hdr, sizeof(req->vbr_hdr)) == 1);
		MPASS(sglist_count(&req->vbr_ack, sizeof(req->vbr_ack)) == 1);

		sc->vtblk_request_count++;
		vtblk_request_enqueue(sc, req);
	}

	return (0);
}

static void
vtblk_request_free(struct vtblk_softc *sc)
{
	struct vtblk_request *req;

	MPASS(TAILQ_EMPTY(&sc->vtblk_req_ready));

	while ((req = vtblk_request_dequeue(sc)) != NULL) {
		sc->vtblk_request_count--;
		free(req, M_DEVBUF);
	}

	KASSERT(sc->vtblk_request_count == 0,
	    ("%s: leaked %d requests", __func__, sc->vtblk_request_count));
}

static struct vtblk_request *
vtblk_request_dequeue(struct vtblk_softc *sc)
{
	struct vtblk_request *req;

	req = TAILQ_FIRST(&sc->vtblk_req_free);
	if (req != NULL) {
		TAILQ_REMOVE(&sc->vtblk_req_free, req, vbr_link);
		bzero(req, sizeof(struct vtblk_request));
	}

	return (req);
}

static void
vtblk_request_enqueue(struct vtblk_softc *sc, struct vtblk_request *req)
{

	TAILQ_INSERT_HEAD(&sc->vtblk_req_free, req, vbr_link);
}

static struct vtblk_request *
vtblk_request_next_ready(struct vtblk_softc *sc)
{
	struct vtblk_request *req;

	req = TAILQ_FIRST(&sc->vtblk_req_ready);
	if (req != NULL)
		TAILQ_REMOVE(&sc->vtblk_req_ready, req, vbr_link);

	return (req);
}

static void
vtblk_request_requeue_ready(struct vtblk_softc *sc, struct vtblk_request *req)
{

	/* NOTE: Currently, there will be at most one request in the queue. */
	TAILQ_INSERT_HEAD(&sc->vtblk_req_ready, req, vbr_link);
}

static struct vtblk_request *
vtblk_request_next(struct vtblk_softc *sc)
{
	struct vtblk_request *req;

	req = vtblk_request_next_ready(sc);
	if (req != NULL)
		return (req);

	return (vtblk_request_bio(sc));
}

static struct vtblk_request *
vtblk_request_bio(struct vtblk_softc *sc)
{
	struct bio_queue_head *bioq;
	struct vtblk_request *req;
	struct bio *bp;

	bioq = &sc->vtblk_bioq;

	if (bioq_first(bioq) == NULL)
		return (NULL);

	req = vtblk_request_dequeue(sc);
	if (req == NULL)
		return (NULL);

	bp = bioq_takefirst(bioq);
	req->vbr_bp = bp;
	req->vbr_ack = -1;
	req->vbr_hdr.ioprio = 1;

	switch (bp->bio_cmd) {
	case BIO_FLUSH:
		req->vbr_hdr.type = VIRTIO_BLK_T_FLUSH;
		break;
	case BIO_READ:
		req->vbr_hdr.type = VIRTIO_BLK_T_IN;
		req->vbr_hdr.sector = bp->bio_offset / 512;
		break;
	case BIO_WRITE:
		req->vbr_hdr.type = VIRTIO_BLK_T_OUT;
		req->vbr_hdr.sector = bp->bio_offset / 512;
		break;
	default:
		panic("%s: bio with unhandled cmd: %d", __func__, bp->bio_cmd);
	}

	if (bp->bio_flags & BIO_ORDERED)
		req->vbr_hdr.type |= VIRTIO_BLK_T_BARRIER;

	return (req);
}

static int
vtblk_request_execute(struct vtblk_softc *sc, struct vtblk_request *req)
{
	struct virtqueue *vq;
	struct sglist *sg;
	struct bio *bp;
	int ordered, readable, writable, error;

	vq = sc->vtblk_vq;
	sg = sc->vtblk_sglist;
	bp = req->vbr_bp;
	ordered = 0;
	writable = 0;

	/*
	 * Some hosts (such as bhyve) do not implement the barrier feature,
	 * so we emulate it in the driver by allowing the barrier request
	 * to be the only one in flight.
	 */
	if ((sc->vtblk_flags & VTBLK_FLAG_BARRIER) == 0) {
		if (sc->vtblk_req_ordered != NULL)
			return (EBUSY);
		if (bp->bio_flags & BIO_ORDERED) {
			if (!virtqueue_empty(vq))
				return (EBUSY);
			ordered = 1;
			req->vbr_hdr.type &= ~VIRTIO_BLK_T_BARRIER;
		}
	}

	sglist_reset(sg);
	sglist_append(sg, &req->vbr_hdr, sizeof(struct virtio_blk_outhdr));

	if (bp->bio_cmd == BIO_READ || bp->bio_cmd == BIO_WRITE) {
		error = sglist_append_bio(sg, bp);
		if (error || sg->sg_nseg == sg->sg_maxseg) {
			panic("%s: bio %p data buffer too big %d",
			    __func__, bp, error);
		}

		/* BIO_READ means the host writes into our buffer. */
		if (bp->bio_cmd == BIO_READ)
			writable = sg->sg_nseg - 1;
	}

	writable++;
	sglist_append(sg, &req->vbr_ack, sizeof(uint8_t));
	readable = sg->sg_nseg - writable;

	error = virtqueue_enqueue(vq, req, sg, readable, writable);
	if (error == 0 && ordered)
		sc->vtblk_req_ordered = req;

	return (error);
}

static int
vtblk_request_error(struct vtblk_request *req)
{
	int error;

	switch (req->vbr_ack) {
	case VIRTIO_BLK_S_OK:
		error = 0;
		break;
	case VIRTIO_BLK_S_UNSUPP:
		error = ENOTSUP;
		break;
	default:
		error = EIO;
		break;
	}

	return (error);
}

static void
vtblk_queue_completed(struct vtblk_softc *sc, struct bio_queue *queue)
{
	struct vtblk_request *req;
	struct bio *bp;

	while ((req = virtqueue_dequeue(sc->vtblk_vq, NULL)) != NULL) {
		if (sc->vtblk_req_ordered != NULL) {
			MPASS(sc->vtblk_req_ordered == req);
			sc->vtblk_req_ordered = NULL;
		}

		bp = req->vbr_bp;
		bp->bio_error = vtblk_request_error(req);
		TAILQ_INSERT_TAIL(queue, bp, bio_queue);

		vtblk_request_enqueue(sc, req);
	}
}

static void
vtblk_done_completed(struct vtblk_softc *sc, struct bio_queue *queue)
{
	struct bio *bp, *tmp;

	TAILQ_FOREACH_SAFE(bp, queue, bio_queue, tmp) {
		if (bp->bio_error != 0)
			disk_err(bp, "hard error", -1, 1);
		vtblk_bio_done(sc, bp, bp->bio_error);
	}
}

static void
vtblk_drain_vq(struct vtblk_softc *sc)
{
	struct virtqueue *vq;
	struct vtblk_request *req;
	int last;

	vq = sc->vtblk_vq;
	last = 0;

	while ((req = virtqueue_drain(vq, &last)) != NULL) {
		vtblk_bio_done(sc, req->vbr_bp, ENXIO);
		vtblk_request_enqueue(sc, req);
	}

	sc->vtblk_req_ordered = NULL;
	KASSERT(virtqueue_empty(vq), ("virtqueue not empty"));
}

static void
vtblk_drain(struct vtblk_softc *sc)
{
	struct bio_queue queue;
	struct bio_queue_head *bioq;
	struct vtblk_request *req;
	struct bio *bp;

	bioq = &sc->vtblk_bioq;
	TAILQ_INIT(&queue);

	if (sc->vtblk_vq != NULL) {
		vtblk_queue_completed(sc, &queue);
		vtblk_done_completed(sc, &queue);

		vtblk_drain_vq(sc);
	}

	while ((req = vtblk_request_next_ready(sc)) != NULL) {
		vtblk_bio_done(sc, req->vbr_bp, ENXIO);
		vtblk_request_enqueue(sc, req);
	}

	while (bioq_first(bioq) != NULL) {
		bp = bioq_takefirst(bioq);
		vtblk_bio_done(sc, bp, ENXIO);
	}

	vtblk_request_free(sc);
}

static void
vtblk_startio(struct vtblk_softc *sc)
{
	struct virtqueue *vq;
	struct vtblk_request *req;
	int enq;

	VTBLK_LOCK_ASSERT(sc);
	vq = sc->vtblk_vq;
	enq = 0;

	if (sc->vtblk_flags & VTBLK_FLAG_SUSPEND)
		return;

	while (!virtqueue_full(vq)) {
		req = vtblk_request_next(sc);
		if (req == NULL)
			break;

		if (vtblk_request_execute(sc, req) != 0) {
			vtblk_request_requeue_ready(sc, req);
			break;
		}

		enq++;
	}

	if (enq > 0)
		virtqueue_notify(vq);
}

static void
vtblk_bio_done(struct vtblk_softc *sc, struct bio *bp, int error)
{

	/* Because of GEOM direct dispatch, we cannot hold any locks. */
	if (sc != NULL)
		VTBLK_LOCK_ASSERT_NOTOWNED(sc);

	if (error) {
		bp->bio_resid = bp->bio_bcount;
		bp->bio_error = error;
		bp->bio_flags |= BIO_ERROR;
	}

	biodone(bp);
}

#define VTBLK_GET_CONFIG(_dev, _feature, _field, _cfg)			\
	if (virtio_with_feature(_dev, _feature)) {			\
		virtio_read_device_config(_dev,				\
		    offsetof(struct virtio_blk_config, _field),		\
		    &(_cfg)->_field, sizeof((_cfg)->_field));		\
	}

static void
vtblk_read_config(struct vtblk_softc *sc, struct virtio_blk_config *blkcfg)
{
	device_t dev;

	dev = sc->vtblk_dev;

	bzero(blkcfg, sizeof(struct virtio_blk_config));

	/* The capacity is always available. */
	virtio_read_device_config(dev, offsetof(struct virtio_blk_config,
	    capacity), &blkcfg->capacity, sizeof(blkcfg->capacity));

	/* Read the configuration if the feature was negotiated. */
	VTBLK_GET_CONFIG(dev, VIRTIO_BLK_F_SIZE_MAX, size_max, blkcfg);
	VTBLK_GET_CONFIG(dev, VIRTIO_BLK_F_SEG_MAX, seg_max, blkcfg);
	VTBLK_GET_CONFIG(dev, VIRTIO_BLK_F_GEOMETRY, geometry, blkcfg);
	VTBLK_GET_CONFIG(dev, VIRTIO_BLK_F_BLK_SIZE, blk_size, blkcfg);
	VTBLK_GET_CONFIG(dev, VIRTIO_BLK_F_TOPOLOGY, topology, blkcfg);
	VTBLK_GET_CONFIG(dev, VIRTIO_BLK_F_CONFIG_WCE, writeback, blkcfg);
}

#undef VTBLK_GET_CONFIG

static void
vtblk_ident(struct vtblk_softc *sc)
{
	struct bio buf;
	struct disk *dp;
	struct vtblk_request *req;
	int len, error;

	dp = sc->vtblk_disk;
	len = MIN(VIRTIO_BLK_ID_BYTES, DISK_IDENT_SIZE);

	if (vtblk_tunable_int(sc, "no_ident", vtblk_no_ident) != 0)
		return;

	req = vtblk_request_dequeue(sc);
	if (req == NULL)
		return;

	req->vbr_ack = -1;
	req->vbr_hdr.type = VIRTIO_BLK_T_GET_ID;
	req->vbr_hdr.ioprio = 1;
	req->vbr_hdr.sector = 0;

	req->vbr_bp = &buf;
	g_reset_bio(&buf);

	buf.bio_cmd = BIO_READ;
	buf.bio_data = dp->d_ident;
	buf.bio_bcount = len;

	VTBLK_LOCK(sc);
	error = vtblk_poll_request(sc, req);
	VTBLK_UNLOCK(sc);

	vtblk_request_enqueue(sc, req);

	if (error) {
		device_printf(sc->vtblk_dev,
		    "error getting device identifier: %d\n", error);
	}
}

static int
vtblk_poll_request(struct vtblk_softc *sc, struct vtblk_request *req)
{
	struct virtqueue *vq;
	int error;

	vq = sc->vtblk_vq;

	if (!virtqueue_empty(vq))
		return (EBUSY);

	error = vtblk_request_execute(sc, req);
	if (error)
		return (error);

	virtqueue_notify(vq);
	virtqueue_poll(vq, NULL);

	error = vtblk_request_error(req);
	if (error && bootverbose) {
		device_printf(sc->vtblk_dev,
		    "%s: IO error: %d\n", __func__, error);
	}

	return (error);
}

static int
vtblk_quiesce(struct vtblk_softc *sc)
{
	int error;

	VTBLK_LOCK_ASSERT(sc);
	error = 0;

	while (!virtqueue_empty(sc->vtblk_vq)) {
		if (mtx_sleep(&sc->vtblk_vq, VTBLK_MTX(sc), PRIBIO, "vtblkq",
		    VTBLK_QUIESCE_TIMEOUT) == EWOULDBLOCK) {
			error = EBUSY;
			break;
		}
	}

	return (error);
}

static void
vtblk_vq_intr(void *xsc)
{
	struct vtblk_softc *sc;
	struct virtqueue *vq;
	struct bio_queue queue;

	sc = xsc;
	vq = sc->vtblk_vq;
	TAILQ_INIT(&queue);

	VTBLK_LOCK(sc);

again:
	if (sc->vtblk_flags & VTBLK_FLAG_DETACH)
		goto out;

	vtblk_queue_completed(sc, &queue);
	vtblk_startio(sc);

	if (virtqueue_enable_intr(vq) != 0) {
		virtqueue_disable_intr(vq);
		goto again;
	}

	if (sc->vtblk_flags & VTBLK_FLAG_SUSPEND)
		wakeup(&sc->vtblk_vq);

out:
	VTBLK_UNLOCK(sc);
	vtblk_done_completed(sc, &queue);
}

static void
vtblk_stop(struct vtblk_softc *sc)
{

	virtqueue_disable_intr(sc->vtblk_vq);
	virtio_stop(sc->vtblk_dev);
}

static void
vtblk_dump_quiesce(struct vtblk_softc *sc)
{

	/*
	 * Spin here until all the requests in-flight at the time of the
	 * dump are completed and queued. The queued requests will be
	 * biodone'd once the dump is finished.
	 */
	while (!virtqueue_empty(sc->vtblk_vq))
		vtblk_queue_completed(sc, &sc->vtblk_dump_queue);
}

static int
vtblk_dump_write(struct vtblk_softc *sc, void *virtual, off_t offset,
    size_t length)
{
	struct bio buf;
	struct vtblk_request *req;

	req = &sc->vtblk_dump_request;
	req->vbr_ack = -1;
	req->vbr_hdr.type = VIRTIO_BLK_T_OUT;
	req->vbr_hdr.ioprio = 1;
	req->vbr_hdr.sector = offset / 512;

	req->vbr_bp = &buf;
	g_reset_bio(&buf);

	buf.bio_cmd = BIO_WRITE;
	buf.bio_data = virtual;
	buf.bio_bcount = length;

	return (vtblk_poll_request(sc, req));
}

static int
vtblk_dump_flush(struct vtblk_softc *sc)
{
	struct bio buf;
	struct vtblk_request *req;

	req = &sc->vtblk_dump_request;
	req->vbr_ack = -1;
	req->vbr_hdr.type = VIRTIO_BLK_T_FLUSH;
	req->vbr_hdr.ioprio = 1;
	req->vbr_hdr.sector = 0;

	req->vbr_bp = &buf;
	g_reset_bio(&buf);

	buf.bio_cmd = BIO_FLUSH;

	return (vtblk_poll_request(sc, req));
}

static void
vtblk_dump_complete(struct vtblk_softc *sc)
{

	vtblk_dump_flush(sc);

	VTBLK_UNLOCK(sc);
	vtblk_done_completed(sc, &sc->vtblk_dump_queue);
	VTBLK_LOCK(sc);
}

static void
vtblk_set_write_cache(struct vtblk_softc *sc, int wc)
{

	/* Set either writeback (1) or writethrough (0) mode. */
	virtio_write_dev_config_1(sc->vtblk_dev,
	    offsetof(struct virtio_blk_config, writeback), wc);
}

static int
vtblk_write_cache_enabled(struct vtblk_softc *sc,
    struct virtio_blk_config *blkcfg)
{
	int wc;

	if (sc->vtblk_flags & VTBLK_FLAG_WC_CONFIG) {
		wc = vtblk_tunable_int(sc, "writecache_mode",
		    vtblk_writecache_mode);
		if (wc >= 0 && wc < VTBLK_CACHE_MAX)
			vtblk_set_write_cache(sc, wc);
		else
			wc = blkcfg->writeback;
	} else
		wc = virtio_with_feature(sc->vtblk_dev, VIRTIO_BLK_F_WCE);

	return (wc);
}

static int
vtblk_write_cache_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct vtblk_softc *sc;
	int wc, error;

	sc = oidp->oid_arg1;
	wc = sc->vtblk_write_cache;

	error = sysctl_handle_int(oidp, &wc, 0, req);
	if (error || req->newptr == NULL)
		return (error);
	if ((sc->vtblk_flags & VTBLK_FLAG_WC_CONFIG) == 0)
		return (EPERM);
	if (wc < 0 || wc >= VTBLK_CACHE_MAX)
		return (EINVAL);

	VTBLK_LOCK(sc);
	sc->vtblk_write_cache = wc;
	vtblk_set_write_cache(sc, sc->vtblk_write_cache);
	VTBLK_UNLOCK(sc);

	return (0);
}

static void
vtblk_setup_sysctl(struct vtblk_softc *sc)
{
	device_t dev;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;
	struct sysctl_oid_list *child;

	dev = sc->vtblk_dev;
	ctx = device_get_sysctl_ctx(dev);
	tree = device_get_sysctl_tree(dev);
	child = SYSCTL_CHILDREN(tree);

	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "writecache_mode",
	    CTLTYPE_INT | CTLFLAG_RW, sc, 0, vtblk_write_cache_sysctl,
	    "I", "Write cache mode (writethrough (0) or writeback (1))");
}

static int
vtblk_tunable_int(struct vtblk_softc *sc, const char *knob, int def)
{
	char path[64];

	snprintf(path, sizeof(path),
	    "hw.vtblk.%d.%s", device_get_unit(sc->vtblk_dev), knob);
	TUNABLE_INT_FETCH(path, &def);

	return (def);
}
