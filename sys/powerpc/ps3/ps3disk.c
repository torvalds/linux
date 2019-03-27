/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2011 glevand (geoffrey.levand@mail.ru)
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
#include <sys/sysctl.h>
#include <sys/disk.h>
#include <sys/bio.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/pio.h>
#include <machine/bus.h>
#include <machine/platform.h>
#include <machine/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <geom/geom_disk.h>

#include "ps3bus.h"
#include "ps3-hvcall.h"

#define PS3DISK_LOCK_INIT(_sc)		\
	mtx_init(&_sc->sc_mtx, device_get_nameunit(_sc->sc_dev), "ps3disk", MTX_DEF)
#define PS3DISK_LOCK_DESTROY(_sc)	mtx_destroy(&_sc->sc_mtx);
#define PS3DISK_LOCK(_sc)		mtx_lock(&(_sc)->sc_mtx)
#define	PS3DISK_UNLOCK(_sc)		mtx_unlock(&(_sc)->sc_mtx)
#define PS3DISK_ASSERT_LOCKED(_sc)	mtx_assert(&_sc->sc_mtx, MA_OWNED);
#define PS3DISK_ASSERT_UNLOCKED(_sc)	mtx_assert(&_sc->sc_mtx, MA_NOTOWNED);

#define LV1_STORAGE_ATA_HDDOUT 		0x23

static SYSCTL_NODE(_hw, OID_AUTO, ps3disk, CTLFLAG_RD, 0,
    "PS3 Disk driver parameters");

#ifdef PS3DISK_DEBUG
static int ps3disk_debug = 0;
SYSCTL_INT(_hw_ps3disk, OID_AUTO, debug, CTLFLAG_RW, &ps3disk_debug,
	0, "control debugging printfs");
TUNABLE_INT("hw.ps3disk.debug", &ps3disk_debug);
enum {
	PS3DISK_DEBUG_INTR	= 0x00000001,
	PS3DISK_DEBUG_TASK	= 0x00000002,
	PS3DISK_DEBUG_READ	= 0x00000004,
	PS3DISK_DEBUG_WRITE	= 0x00000008,
	PS3DISK_DEBUG_FLUSH	= 0x00000010,
	PS3DISK_DEBUG_ANY	= 0xffffffff
};
#define	DPRINTF(sc, m, fmt, ...)				\
do {								\
	if (sc->sc_debug & (m))					\
		printf(fmt, __VA_ARGS__);			\
} while (0)
#else
#define	DPRINTF(sc, m, fmt, ...)
#endif

struct ps3disk_region {
	uint64_t r_id;
	uint64_t r_start;
	uint64_t r_size;
	uint64_t r_flags;
};

struct ps3disk_softc {
	device_t sc_dev;

	struct mtx sc_mtx;

	uint64_t sc_blksize;
	uint64_t sc_nblocks;

	uint64_t sc_nregs;
	struct ps3disk_region *sc_reg;

	int sc_irqid;
	struct resource	*sc_irq;
	void *sc_irqctx;

	struct disk **sc_disk;

	struct bio_queue_head sc_bioq;
	struct bio_queue_head sc_deferredq;
	struct proc *sc_task;	

	bus_dma_tag_t sc_dmatag;

	int sc_running;
	int sc_debug;
};

static int ps3disk_open(struct disk *dp);
static int ps3disk_close(struct disk *dp);
static void ps3disk_strategy(struct bio *bp);

static void ps3disk_task(void *arg);
static void ps3disk_intr(void *arg);
static int ps3disk_get_disk_geometry(struct ps3disk_softc *sc);
static int ps3disk_enum_regions(struct ps3disk_softc *sc);
static void ps3disk_transfer(void *arg, bus_dma_segment_t *segs, int nsegs,
    int error);

static void ps3disk_sysctlattach(struct ps3disk_softc *sc);

static MALLOC_DEFINE(M_PS3DISK, "ps3disk", "PS3 Disk");

static int
ps3disk_probe(device_t dev)
{
	if (ps3bus_get_bustype(dev) != PS3_BUSTYPE_STORAGE ||
	    ps3bus_get_devtype(dev) != PS3_DEVTYPE_DISK)
		return (ENXIO);

	device_set_desc(dev, "Playstation 3 Disk");

	return (BUS_PROBE_SPECIFIC);
}

static int
ps3disk_attach(device_t dev)
{
	struct ps3disk_softc *sc;
	struct disk *d;
	intmax_t mb;
	uint64_t junk;
	char unit;
	int i, err;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	PS3DISK_LOCK_INIT(sc);

	err = ps3disk_get_disk_geometry(sc);
	if (err) {
		device_printf(dev, "Could not get disk geometry\n");
		err = ENXIO;
		goto fail_destroy_lock;
	}

	device_printf(dev, "block size %lu total blocks %lu\n",
	    sc->sc_blksize, sc->sc_nblocks);

	err = ps3disk_enum_regions(sc);
	if (err) {
		device_printf(dev, "Could not enumerate disk regions\n");
		err = ENXIO;
		goto fail_destroy_lock;
	}

	device_printf(dev, "Found %lu regions\n", sc->sc_nregs);

	if (!sc->sc_nregs) {
		err = ENXIO;
		goto fail_destroy_lock;
	}

	/* Setup interrupt handler */
	sc->sc_irqid = 0;
	sc->sc_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->sc_irqid,
	    RF_ACTIVE);
	if (!sc->sc_irq) {
		device_printf(dev, "Could not allocate IRQ\n");
		err = ENXIO;
		goto fail_free_regions;
	}

	err = bus_setup_intr(dev, sc->sc_irq,
	    INTR_TYPE_BIO | INTR_MPSAFE | INTR_ENTROPY,
	    NULL, ps3disk_intr, sc, &sc->sc_irqctx);
	if (err) {
		device_printf(dev, "Could not setup IRQ\n");
		err = ENXIO;
		goto fail_release_intr;
	}

	/* Setup DMA */
	err = bus_dma_tag_create(bus_get_dma_tag(dev), 4096, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL,
	    BUS_SPACE_UNRESTRICTED, 1, PAGE_SIZE, 0,
	    busdma_lock_mutex, &sc->sc_mtx, &sc->sc_dmatag);
	if (err) {
		device_printf(dev, "Could not create DMA tag\n");
		err = ENXIO;
		goto fail_teardown_intr;
	}

	/* Setup disks */

	sc->sc_disk = malloc(sc->sc_nregs * sizeof(struct disk *),
	    M_PS3DISK, M_ZERO | M_WAITOK);
	if (!sc->sc_disk) {
		device_printf(dev, "Could not allocate disk(s)\n");
		err = ENOMEM;
		goto fail_teardown_intr;
	}

	for (i = 0; i < sc->sc_nregs; i++) {
		struct ps3disk_region *rp = &sc->sc_reg[i];

		d = sc->sc_disk[i] = disk_alloc();
		d->d_open = ps3disk_open;
		d->d_close = ps3disk_close;
		d->d_strategy = ps3disk_strategy;
		d->d_name = "ps3disk";
		d->d_drv1 = sc;
		d->d_maxsize = PAGE_SIZE;
		d->d_sectorsize = sc->sc_blksize;
		d->d_unit = i;
		d->d_mediasize = sc->sc_reg[i].r_size * sc->sc_blksize;
		d->d_flags |= DISKFLAG_CANFLUSHCACHE;

		mb = d->d_mediasize >> 20;
		unit = 'M';
		if (mb >= 10240) {
			unit = 'G';
			mb /= 1024;
		}

		/* Test to see if we can read this region */
		err = lv1_storage_read(ps3bus_get_device(dev), d->d_unit,
		    0, 0, rp->r_flags, 0, &junk);
		device_printf(dev, "region %d %ju%cB%s\n", i, mb, unit,
		    (err == LV1_DENIED_BY_POLICY) ?  " (hypervisor protected)"
		    : "");

		if (err != LV1_DENIED_BY_POLICY)
			disk_create(d, DISK_VERSION);
	}
	err = 0;

	bioq_init(&sc->sc_bioq);
	bioq_init(&sc->sc_deferredq);
	kproc_create(&ps3disk_task, sc, &sc->sc_task, 0, 0, "ps3disk");

	ps3disk_sysctlattach(sc);
	sc->sc_running = 1;
	return (0);

fail_teardown_intr:
	bus_teardown_intr(dev, sc->sc_irq, sc->sc_irqctx);
fail_release_intr:
	bus_release_resource(dev, SYS_RES_IRQ, sc->sc_irqid, sc->sc_irq);
fail_free_regions:
	free(sc->sc_reg, M_PS3DISK);
fail_destroy_lock:
	PS3DISK_LOCK_DESTROY(sc);
	return (err);
}

static int
ps3disk_detach(device_t dev)
{
	struct ps3disk_softc *sc = device_get_softc(dev);
	int i;

	for (i = 0; i < sc->sc_nregs; i++)
		disk_destroy(sc->sc_disk[i]);

	bus_dma_tag_destroy(sc->sc_dmatag);

	bus_teardown_intr(dev, sc->sc_irq, sc->sc_irqctx);
	bus_release_resource(dev, SYS_RES_IRQ, sc->sc_irqid, sc->sc_irq);

	free(sc->sc_disk, M_PS3DISK);
	free(sc->sc_reg, M_PS3DISK);

	PS3DISK_LOCK_DESTROY(sc);

	return (0);
}

static int
ps3disk_open(struct disk *dp)
{
	return (0);
}

static int
ps3disk_close(struct disk *dp)
{
	return (0);
}

/* Process deferred blocks */
static void
ps3disk_task(void *arg)
{
	struct ps3disk_softc *sc = (struct ps3disk_softc *) arg;
	struct bio *bp;

	
	while (1) {
		kproc_suspend_check(sc->sc_task);
		tsleep(&sc->sc_deferredq, PRIBIO, "ps3disk", 10);

		PS3DISK_LOCK(sc);
		bp = bioq_takefirst(&sc->sc_deferredq);
		PS3DISK_UNLOCK(sc);

		if (bp == NULL)
			continue;

		if (bp->bio_driver1 != NULL) {
			bus_dmamap_unload(sc->sc_dmatag, (bus_dmamap_t)
			    bp->bio_driver1);
			bus_dmamap_destroy(sc->sc_dmatag, (bus_dmamap_t)
			    bp->bio_driver1);
		}

		ps3disk_strategy(bp);
	}

	kproc_exit(0);
}

static void
ps3disk_strategy(struct bio *bp)
{
	struct ps3disk_softc *sc = (struct ps3disk_softc *)bp->bio_disk->d_drv1;
	int err;

	if (sc == NULL) {
		bp->bio_flags |= BIO_ERROR;
		bp->bio_error = EINVAL;
		biodone(bp);
		return;
	}

	PS3DISK_LOCK(sc);
	bp->bio_resid = bp->bio_bcount;
	bioq_insert_tail(&sc->sc_bioq, bp);

	DPRINTF(sc, PS3DISK_DEBUG_TASK, "%s: bio_cmd 0x%02x\n",
	    __func__, bp->bio_cmd);

	err = 0;
	if (bp->bio_cmd == BIO_FLUSH) {
		bp->bio_driver1 = 0;
		err = lv1_storage_send_device_command(
		    ps3bus_get_device(sc->sc_dev), LV1_STORAGE_ATA_HDDOUT,
		    0, 0, 0, 0, (uint64_t *)&bp->bio_driver2);
		if (err == LV1_BUSY)
			err = EAGAIN;
	} else if (bp->bio_cmd == BIO_READ || bp->bio_cmd == BIO_WRITE) {
		if (bp->bio_bcount % sc->sc_blksize != 0) {
			err = EINVAL;
		} else {
			bus_dmamap_create(sc->sc_dmatag, BUS_DMA_COHERENT,
			    (bus_dmamap_t *)(&bp->bio_driver1));
			err = bus_dmamap_load(sc->sc_dmatag,
			    (bus_dmamap_t)(bp->bio_driver1), bp->bio_data,
			    bp->bio_bcount, ps3disk_transfer, bp, 0);
			if (err == EINPROGRESS)
				err = 0;
		}
	} else {
		err = EINVAL;
	}

	if (err == EAGAIN) {
		bioq_remove(&sc->sc_bioq, bp);
		bioq_insert_tail(&sc->sc_deferredq, bp);
	} else if (err != 0) {
		bp->bio_error = err;
		bp->bio_flags |= BIO_ERROR;
		bioq_remove(&sc->sc_bioq, bp);
		disk_err(bp, "hard error", -1, 1);
		biodone(bp);
	}

	PS3DISK_UNLOCK(sc);
}

static void
ps3disk_intr(void *arg)
{
	struct ps3disk_softc *sc = (struct ps3disk_softc *) arg;
	device_t dev = sc->sc_dev;
	uint64_t devid = ps3bus_get_device(dev);
	struct bio *bp;
	uint64_t tag, status;

	if (lv1_storage_get_async_status(devid, &tag, &status) != 0)
		return;
	
	PS3DISK_LOCK(sc);

	DPRINTF(sc, PS3DISK_DEBUG_INTR, "%s: tag 0x%016lx "
	    "status 0x%016lx\n", __func__, tag, status);

	/* Locate the matching request */
	TAILQ_FOREACH(bp, &sc->sc_bioq.queue, bio_queue) {
		if ((uint64_t)bp->bio_driver2 != tag)
			continue;

		if (status != 0) {
			device_printf(sc->sc_dev, "%s error (%#lx)\n",
			    (bp->bio_cmd == BIO_READ) ? "Read" : "Write",
			    status);
			bp->bio_error = EIO;
			bp->bio_flags |= BIO_ERROR;
		} else {
			bp->bio_error = 0;
			bp->bio_resid = 0;
			bp->bio_flags |= BIO_DONE;
		}

		if (bp->bio_driver1 != NULL) {
			if (bp->bio_cmd == BIO_READ)
				bus_dmamap_sync(sc->sc_dmatag, (bus_dmamap_t)
				    bp->bio_driver1, BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->sc_dmatag, (bus_dmamap_t)
			    bp->bio_driver1);
			bus_dmamap_destroy(sc->sc_dmatag, (bus_dmamap_t)
			    bp->bio_driver1);
		}

		bioq_remove(&sc->sc_bioq, bp);
		biodone(bp);
		break;
	}

	if (bioq_first(&sc->sc_deferredq) != NULL)
		wakeup(&sc->sc_deferredq);

	PS3DISK_UNLOCK(sc);
}

static int
ps3disk_get_disk_geometry(struct ps3disk_softc *sc)
{
	device_t dev = sc->sc_dev;
	uint64_t bus_index = ps3bus_get_busidx(dev);
	uint64_t dev_index = ps3bus_get_devidx(dev);
	uint64_t junk;
	int err;

	err = lv1_get_repository_node_value(PS3_LPAR_ID_PME,
	    (lv1_repository_string("bus") >> 32) | bus_index,
	    lv1_repository_string("dev") | dev_index,
	    lv1_repository_string("blk_size"), 0, &sc->sc_blksize, &junk);
	if (err) {
		device_printf(dev, "Could not get block size (0x%08x)\n", err);
		return (ENXIO);
	}

	err = lv1_get_repository_node_value(PS3_LPAR_ID_PME,
	    (lv1_repository_string("bus") >> 32) | bus_index,
	    lv1_repository_string("dev") | dev_index,
	    lv1_repository_string("n_blocks"), 0, &sc->sc_nblocks, &junk);
	if (err) {
		device_printf(dev, "Could not get total number of blocks "
		    "(0x%08x)\n", err);
		err = ENXIO;
	}

	return (err);
}

static int
ps3disk_enum_regions(struct ps3disk_softc *sc)
{
	device_t dev = sc->sc_dev;
	uint64_t bus_index = ps3bus_get_busidx(dev);
	uint64_t dev_index = ps3bus_get_devidx(dev);
	uint64_t junk;
	int i, err;

	/* Read number of regions */

	err = lv1_get_repository_node_value(PS3_LPAR_ID_PME,
	    (lv1_repository_string("bus") >> 32) | bus_index,
	    lv1_repository_string("dev") | dev_index,
	    lv1_repository_string("n_regs"), 0, &sc->sc_nregs, &junk);
	if (err) {
		device_printf(dev, "Could not get number of regions (0x%08x)\n",
		    err);
		err = ENXIO;
		goto fail;
	}

	if (!sc->sc_nregs)
		return 0;

	sc->sc_reg = malloc(sc->sc_nregs * sizeof(struct ps3disk_region),
	    M_PS3DISK, M_ZERO | M_WAITOK);
	if (!sc->sc_reg) {
		err = ENOMEM;
		goto fail;
	}

	/* Setup regions */

	for (i = 0; i < sc->sc_nregs; i++) {
		err = lv1_get_repository_node_value(PS3_LPAR_ID_PME,
		    (lv1_repository_string("bus") >> 32) | bus_index,
		    lv1_repository_string("dev") | dev_index,
		    lv1_repository_string("region") | i,
		    lv1_repository_string("id"), &sc->sc_reg[i].r_id, &junk);
		if (err) {
			device_printf(dev, "Could not get region id (0x%08x)\n",
			    err);
			err = ENXIO;
			goto fail;
		}

		err = lv1_get_repository_node_value(PS3_LPAR_ID_PME,
		    (lv1_repository_string("bus") >> 32) | bus_index,
		    lv1_repository_string("dev") | dev_index,
		    lv1_repository_string("region") | i,
		    lv1_repository_string("start"), &sc->sc_reg[i].r_start,
		    &junk);
		if (err) {
			device_printf(dev, "Could not get region start "
			    "(0x%08x)\n", err);
			err = ENXIO;
			goto fail;
		}

		err = lv1_get_repository_node_value(PS3_LPAR_ID_PME,
		    (lv1_repository_string("bus") >> 32) | bus_index,
		    lv1_repository_string("dev") | dev_index,
		    lv1_repository_string("region") | i,
		    lv1_repository_string("size"), &sc->sc_reg[i].r_size,
		    &junk);
		if (err) {
			device_printf(dev, "Could not get region size "
			    "(0x%08x)\n", err);
			err = ENXIO;
			goto fail;
		}

		if (i == 0)
			sc->sc_reg[i].r_flags = 0x2;
		else
			sc->sc_reg[i].r_flags = 0;
	}

	return (0);

fail:

	sc->sc_nregs = 0;
	if (sc->sc_reg)
		free(sc->sc_reg, M_PS3DISK);

	return (err);
}

static void
ps3disk_transfer(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
	struct bio *bp = (struct bio *)(arg);
	struct ps3disk_softc *sc = (struct ps3disk_softc *)bp->bio_disk->d_drv1;
	struct ps3disk_region *rp = &sc->sc_reg[bp->bio_disk->d_unit];
	uint64_t devid = ps3bus_get_device(sc->sc_dev);
	uint64_t block;
	int i, err;

	/* Locks already held by busdma */
	PS3DISK_ASSERT_LOCKED(sc);

	if (error) {
		bp->bio_error = error;
		bp->bio_flags |= BIO_ERROR;
		bioq_remove(&sc->sc_bioq, bp);
		biodone(bp);
		return;
	}

	block = bp->bio_pblkno;
	for (i = 0; i < nsegs; i++) {
		KASSERT((segs[i].ds_len % sc->sc_blksize) == 0,
		    ("DMA fragments not blocksize multiples"));

		if (bp->bio_cmd == BIO_READ) {
			err = lv1_storage_read(devid, rp->r_id,
			    block, segs[i].ds_len/sc->sc_blksize,
			    rp->r_flags, segs[i].ds_addr,
			    (uint64_t *)&bp->bio_driver2);
		} else {
			bus_dmamap_sync(sc->sc_dmatag,
			    (bus_dmamap_t)bp->bio_driver1,
			    BUS_DMASYNC_PREWRITE);
			err = lv1_storage_write(devid, rp->r_id,
			    block, segs[i].ds_len/sc->sc_blksize,
			    rp->r_flags, segs[i].ds_addr,
			    (uint64_t *)&bp->bio_driver2);
		}

		if (err) {
			if (err == LV1_BUSY) {
				bioq_remove(&sc->sc_bioq, bp);
				bioq_insert_tail(&sc->sc_deferredq, bp);
			} else {
				bus_dmamap_unload(sc->sc_dmatag, (bus_dmamap_t)
				    bp->bio_driver1);
				bus_dmamap_destroy(sc->sc_dmatag, (bus_dmamap_t)
				    bp->bio_driver1);
				device_printf(sc->sc_dev, "Could not read "
				    "sectors (0x%08x)\n", err);
				bp->bio_error = EINVAL;
				bp->bio_flags |= BIO_ERROR;
				bioq_remove(&sc->sc_bioq, bp);
				biodone(bp);
			}

			break;
		}

		DPRINTF(sc, PS3DISK_DEBUG_READ, "%s: tag 0x%016lx\n",
		    __func__, sc->sc_bounce_tag);
	}
}

#ifdef PS3DISK_DEBUG
static int
ps3disk_sysctl_debug(SYSCTL_HANDLER_ARGS)
{
	struct ps3disk_softc *sc = arg1;
	int debug, error;

	debug = sc->sc_debug;

	error = sysctl_handle_int(oidp, &debug, 0, req);
	if (error || !req->newptr)
		return error;

	sc->sc_debug = debug;

	return 0;
}
#endif

static void
ps3disk_sysctlattach(struct ps3disk_softc *sc)
{
#ifdef PS3DISK_DEBUG
	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(sc->sc_dev);
	struct sysctl_oid *tree = device_get_sysctl_tree(sc->sc_dev);

	sc->sc_debug = ps3disk_debug;

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"debug", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
		ps3disk_sysctl_debug, "I", "control debugging printfs");
#endif
}

static device_method_t ps3disk_methods[] = {
	DEVMETHOD(device_probe,		ps3disk_probe),
	DEVMETHOD(device_attach,	ps3disk_attach),
	DEVMETHOD(device_detach,	ps3disk_detach),
	{0, 0},
};

static driver_t ps3disk_driver = {
	"ps3disk",
	ps3disk_methods,
	sizeof(struct ps3disk_softc),
};

static devclass_t ps3disk_devclass;

DRIVER_MODULE(ps3disk, ps3bus, ps3disk_driver, ps3disk_devclass, 0, 0);
