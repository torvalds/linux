/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2012-2016 Intel Corporation
 * All rights reserved.
 * Copyright (C) 2018 Alexander Motin <mav@FreeBSD.org>
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
#include <sys/bio.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/queue.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/taskqueue.h>
#include <machine/atomic.h>

#include <geom/geom.h>
#include <geom/geom_disk.h>

#include <dev/nvme/nvme.h>

#define NVD_STR		"nvd"

struct nvd_disk;
struct nvd_controller;

static disk_ioctl_t nvd_ioctl;
static disk_strategy_t nvd_strategy;
static dumper_t nvd_dump;

static void nvd_done(void *arg, const struct nvme_completion *cpl);
static void nvd_gone(struct nvd_disk *ndisk);

static void *nvd_new_disk(struct nvme_namespace *ns, void *ctrlr);

static void *nvd_new_controller(struct nvme_controller *ctrlr);
static void nvd_controller_fail(void *ctrlr);

static int nvd_load(void);
static void nvd_unload(void);

MALLOC_DEFINE(M_NVD, "nvd", "nvd(4) allocations");

struct nvme_consumer *consumer_handle;

struct nvd_disk {
	struct nvd_controller	*ctrlr;

	struct bio_queue_head	bioq;
	struct task		bioqtask;
	struct mtx		bioqlock;

	struct disk		*disk;
	struct taskqueue	*tq;
	struct nvme_namespace	*ns;

	uint32_t		cur_depth;
#define	NVD_ODEPTH	(1 << 30)
	uint32_t		ordered_in_flight;
	u_int			unit;

	TAILQ_ENTRY(nvd_disk)	global_tailq;
	TAILQ_ENTRY(nvd_disk)	ctrlr_tailq;
};

struct nvd_controller {

	TAILQ_ENTRY(nvd_controller)	tailq;
	TAILQ_HEAD(, nvd_disk)		disk_head;
};

static struct mtx			nvd_lock;
static TAILQ_HEAD(, nvd_controller)	ctrlr_head;
static TAILQ_HEAD(disk_list, nvd_disk)	disk_head;

static SYSCTL_NODE(_hw, OID_AUTO, nvd, CTLFLAG_RD, 0, "nvd driver parameters");
/*
 * The NVMe specification does not define a maximum or optimal delete size, so
 *  technically max delete size is min(full size of the namespace, 2^32 - 1
 *  LBAs).  A single delete for a multi-TB NVMe namespace though may take much
 *  longer to complete than the nvme(4) I/O timeout period.  So choose a sensible
 *  default here that is still suitably large to minimize the number of overall
 *  delete operations.
 */
static uint64_t nvd_delete_max = (1024 * 1024 * 1024);  /* 1GB */
SYSCTL_UQUAD(_hw_nvd, OID_AUTO, delete_max, CTLFLAG_RDTUN, &nvd_delete_max, 0,
	     "nvd maximum BIO_DELETE size in bytes");

static int nvd_modevent(module_t mod, int type, void *arg)
{
	int error = 0;

	switch (type) {
	case MOD_LOAD:
		error = nvd_load();
		break;
	case MOD_UNLOAD:
		nvd_unload();
		break;
	default:
		break;
	}

	return (error);
}

moduledata_t nvd_mod = {
	NVD_STR,
	(modeventhand_t)nvd_modevent,
	0
};

DECLARE_MODULE(nvd, nvd_mod, SI_SUB_DRIVERS, SI_ORDER_ANY);
MODULE_VERSION(nvd, 1);
MODULE_DEPEND(nvd, nvme, 1, 1, 1);

static int
nvd_load()
{
	if (!nvme_use_nvd)
		return 0;

	mtx_init(&nvd_lock, "nvd_lock", NULL, MTX_DEF);
	TAILQ_INIT(&ctrlr_head);
	TAILQ_INIT(&disk_head);

	consumer_handle = nvme_register_consumer(nvd_new_disk,
	    nvd_new_controller, NULL, nvd_controller_fail);

	return (consumer_handle != NULL ? 0 : -1);
}

static void
nvd_unload()
{
	struct nvd_controller	*ctrlr;
	struct nvd_disk		*ndisk;

	if (!nvme_use_nvd)
		return;

	mtx_lock(&nvd_lock);
	while ((ctrlr = TAILQ_FIRST(&ctrlr_head)) != NULL) {
		TAILQ_REMOVE(&ctrlr_head, ctrlr, tailq);
		TAILQ_FOREACH(ndisk, &ctrlr->disk_head, ctrlr_tailq)
			nvd_gone(ndisk);
		while (!TAILQ_EMPTY(&ctrlr->disk_head))
			msleep(&ctrlr->disk_head, &nvd_lock, 0, "nvd_unload",0);
		free(ctrlr, M_NVD);
	}
	mtx_unlock(&nvd_lock);

	nvme_unregister_consumer(consumer_handle);

	mtx_destroy(&nvd_lock);
}

static void
nvd_bio_submit(struct nvd_disk *ndisk, struct bio *bp)
{
	int err;

	bp->bio_driver1 = NULL;
	if (__predict_false(bp->bio_flags & BIO_ORDERED))
		atomic_add_int(&ndisk->cur_depth, NVD_ODEPTH);
	else
		atomic_add_int(&ndisk->cur_depth, 1);
	err = nvme_ns_bio_process(ndisk->ns, bp, nvd_done);
	if (err) {
		if (__predict_false(bp->bio_flags & BIO_ORDERED)) {
			atomic_add_int(&ndisk->cur_depth, -NVD_ODEPTH);
			atomic_add_int(&ndisk->ordered_in_flight, -1);
			wakeup(&ndisk->cur_depth);
		} else {
			if (atomic_fetchadd_int(&ndisk->cur_depth, -1) == 1 &&
			    __predict_false(ndisk->ordered_in_flight != 0))
				wakeup(&ndisk->cur_depth);
		}
		bp->bio_error = err;
		bp->bio_flags |= BIO_ERROR;
		bp->bio_resid = bp->bio_bcount;
		biodone(bp);
	}
}

static void
nvd_strategy(struct bio *bp)
{
	struct nvd_disk *ndisk = (struct nvd_disk *)bp->bio_disk->d_drv1;

	/*
	 * bio with BIO_ORDERED flag must be executed after all previous
	 * bios in the queue, and before any successive bios.
	 */
	if (__predict_false(bp->bio_flags & BIO_ORDERED)) {
		if (atomic_fetchadd_int(&ndisk->ordered_in_flight, 1) == 0 &&
		    ndisk->cur_depth == 0 && bioq_first(&ndisk->bioq) == NULL) {
			nvd_bio_submit(ndisk, bp);
			return;
		}
	} else if (__predict_true(ndisk->ordered_in_flight == 0)) {
		nvd_bio_submit(ndisk, bp);
		return;
	}

	/*
	 * There are ordered bios in flight, so we need to submit
	 *  bios through the task queue to enforce ordering.
	 */
	mtx_lock(&ndisk->bioqlock);
	bioq_insert_tail(&ndisk->bioq, bp);
	mtx_unlock(&ndisk->bioqlock);
	taskqueue_enqueue(ndisk->tq, &ndisk->bioqtask);
}

static void
nvd_gone(struct nvd_disk *ndisk)
{
	struct bio	*bp;

	printf(NVD_STR"%u: detached\n", ndisk->unit);
	mtx_lock(&ndisk->bioqlock);
	disk_gone(ndisk->disk);
	while ((bp = bioq_takefirst(&ndisk->bioq)) != NULL) {
		if (__predict_false(bp->bio_flags & BIO_ORDERED))
			atomic_add_int(&ndisk->ordered_in_flight, -1);
		bp->bio_error = ENXIO;
		bp->bio_flags |= BIO_ERROR;
		bp->bio_resid = bp->bio_bcount;
		biodone(bp);
	}
	mtx_unlock(&ndisk->bioqlock);
}

static void
nvd_gonecb(struct disk *dp)
{
	struct nvd_disk *ndisk = (struct nvd_disk *)dp->d_drv1;

	disk_destroy(ndisk->disk);
	mtx_lock(&nvd_lock);
	TAILQ_REMOVE(&disk_head, ndisk, global_tailq);
	TAILQ_REMOVE(&ndisk->ctrlr->disk_head, ndisk, ctrlr_tailq);
	if (TAILQ_EMPTY(&ndisk->ctrlr->disk_head))
		wakeup(&ndisk->ctrlr->disk_head);
	mtx_unlock(&nvd_lock);
	taskqueue_free(ndisk->tq);
	mtx_destroy(&ndisk->bioqlock);
	free(ndisk, M_NVD);
}

static int
nvd_ioctl(struct disk *ndisk, u_long cmd, void *data, int fflag,
    struct thread *td)
{
	int ret = 0;

	switch (cmd) {
	default:
		ret = EIO;
	}

	return (ret);
}

static int
nvd_dump(void *arg, void *virt, vm_offset_t phys, off_t offset, size_t len)
{
	struct disk *dp = arg;
	struct nvd_disk *ndisk = dp->d_drv1;

	return (nvme_ns_dump(ndisk->ns, virt, offset, len));
}

static void
nvd_done(void *arg, const struct nvme_completion *cpl)
{
	struct bio *bp = (struct bio *)arg;
	struct nvd_disk *ndisk = bp->bio_disk->d_drv1;

	if (__predict_false(bp->bio_flags & BIO_ORDERED)) {
		atomic_add_int(&ndisk->cur_depth, -NVD_ODEPTH);
		atomic_add_int(&ndisk->ordered_in_flight, -1);
		wakeup(&ndisk->cur_depth);
	} else {
		if (atomic_fetchadd_int(&ndisk->cur_depth, -1) == 1 &&
		    __predict_false(ndisk->ordered_in_flight != 0))
			wakeup(&ndisk->cur_depth);
	}

	biodone(bp);
}

static void
nvd_bioq_process(void *arg, int pending)
{
	struct nvd_disk *ndisk = arg;
	struct bio *bp;

	for (;;) {
		mtx_lock(&ndisk->bioqlock);
		bp = bioq_takefirst(&ndisk->bioq);
		mtx_unlock(&ndisk->bioqlock);
		if (bp == NULL)
			break;

		if (__predict_false(bp->bio_flags & BIO_ORDERED)) {
			/*
			 * bio with BIO_ORDERED flag set must be executed
			 * after all previous bios.
			 */
			while (ndisk->cur_depth > 0)
				tsleep(&ndisk->cur_depth, 0, "nvdorb", 1);
		} else {
			/*
			 * bio with BIO_ORDERED flag set must be completed
			 * before proceeding with additional bios.
			 */
			while (ndisk->cur_depth >= NVD_ODEPTH)
				tsleep(&ndisk->cur_depth, 0, "nvdora", 1);
		}

		nvd_bio_submit(ndisk, bp);
	}
}

static void *
nvd_new_controller(struct nvme_controller *ctrlr)
{
	struct nvd_controller	*nvd_ctrlr;

	nvd_ctrlr = malloc(sizeof(struct nvd_controller), M_NVD,
	    M_ZERO | M_WAITOK);

	TAILQ_INIT(&nvd_ctrlr->disk_head);
	mtx_lock(&nvd_lock);
	TAILQ_INSERT_TAIL(&ctrlr_head, nvd_ctrlr, tailq);
	mtx_unlock(&nvd_lock);

	return (nvd_ctrlr);
}

static void *
nvd_new_disk(struct nvme_namespace *ns, void *ctrlr_arg)
{
	uint8_t			descr[NVME_MODEL_NUMBER_LENGTH+1];
	struct nvd_disk		*ndisk, *tnd;
	struct disk		*disk;
	struct nvd_controller	*ctrlr = ctrlr_arg;
	int unit;

	ndisk = malloc(sizeof(struct nvd_disk), M_NVD, M_ZERO | M_WAITOK);
	ndisk->ctrlr = ctrlr;
	ndisk->ns = ns;
	ndisk->cur_depth = 0;
	ndisk->ordered_in_flight = 0;
	mtx_init(&ndisk->bioqlock, "nvd bioq lock", NULL, MTX_DEF);
	bioq_init(&ndisk->bioq);
	TASK_INIT(&ndisk->bioqtask, 0, nvd_bioq_process, ndisk);

	mtx_lock(&nvd_lock);
	unit = 0;
	TAILQ_FOREACH(tnd, &disk_head, global_tailq) {
		if (tnd->unit > unit)
			break;
		unit = tnd->unit + 1;
	}
	ndisk->unit = unit;
	if (tnd != NULL)
		TAILQ_INSERT_BEFORE(tnd, ndisk, global_tailq);
	else
		TAILQ_INSERT_TAIL(&disk_head, ndisk, global_tailq);
	TAILQ_INSERT_TAIL(&ctrlr->disk_head, ndisk, ctrlr_tailq);
	mtx_unlock(&nvd_lock);

	ndisk->tq = taskqueue_create("nvd_taskq", M_WAITOK,
	    taskqueue_thread_enqueue, &ndisk->tq);
	taskqueue_start_threads(&ndisk->tq, 1, PI_DISK, "nvd taskq");

	disk = ndisk->disk = disk_alloc();
	disk->d_strategy = nvd_strategy;
	disk->d_ioctl = nvd_ioctl;
	disk->d_dump = nvd_dump;
	disk->d_gone = nvd_gonecb;
	disk->d_name = NVD_STR;
	disk->d_unit = ndisk->unit;
	disk->d_drv1 = ndisk;

	disk->d_sectorsize = nvme_ns_get_sector_size(ns);
	disk->d_mediasize = (off_t)nvme_ns_get_size(ns);
	disk->d_maxsize = nvme_ns_get_max_io_xfer_size(ns);
	disk->d_delmaxsize = (off_t)nvme_ns_get_size(ns);
	if (disk->d_delmaxsize > nvd_delete_max)
		disk->d_delmaxsize = nvd_delete_max;
	disk->d_stripesize = nvme_ns_get_stripesize(ns);
	disk->d_flags = DISKFLAG_UNMAPPED_BIO | DISKFLAG_DIRECT_COMPLETION;
	if (nvme_ns_get_flags(ns) & NVME_NS_DEALLOCATE_SUPPORTED)
		disk->d_flags |= DISKFLAG_CANDELETE;
	if (nvme_ns_get_flags(ns) & NVME_NS_FLUSH_SUPPORTED)
		disk->d_flags |= DISKFLAG_CANFLUSHCACHE;

	/*
	 * d_ident and d_descr are both far bigger than the length of either
	 *  the serial or model number strings.
	 */
	nvme_strvis(disk->d_ident, nvme_ns_get_serial_number(ns),
	    sizeof(disk->d_ident), NVME_SERIAL_NUMBER_LENGTH);
	nvme_strvis(descr, nvme_ns_get_model_number(ns), sizeof(descr),
	    NVME_MODEL_NUMBER_LENGTH);
	strlcpy(disk->d_descr, descr, sizeof(descr));

	disk->d_rotation_rate = DISK_RR_NON_ROTATING;

	disk_create(disk, DISK_VERSION);

	printf(NVD_STR"%u: <%s> NVMe namespace\n", disk->d_unit, descr);
	printf(NVD_STR"%u: %juMB (%ju %u byte sectors)\n", disk->d_unit,
		(uintmax_t)disk->d_mediasize / (1024*1024),
		(uintmax_t)disk->d_mediasize / disk->d_sectorsize,
		disk->d_sectorsize);

	return (ndisk);
}

static void
nvd_controller_fail(void *ctrlr_arg)
{
	struct nvd_controller	*ctrlr = ctrlr_arg;
	struct nvd_disk		*ndisk;

	mtx_lock(&nvd_lock);
	TAILQ_REMOVE(&ctrlr_head, ctrlr, tailq);
	TAILQ_FOREACH(ndisk, &ctrlr->disk_head, ctrlr_tailq)
		nvd_gone(ndisk);
	while (!TAILQ_EMPTY(&ctrlr->disk_head))
		msleep(&ctrlr->disk_head, &nvd_lock, 0, "nvd_fail", 0);
	mtx_unlock(&nvd_lock);
	free(ctrlr, M_NVD);
}

