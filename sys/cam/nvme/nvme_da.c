/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015 Netflix, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
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
 * Derived from ata_da.c:
 * Copyright (c) 2009 Alexander Motin <mav@FreeBSD.org>
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>

#ifdef _KERNEL
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bio.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/conf.h>
#include <sys/devicestat.h>
#include <sys/eventhandler.h>
#include <sys/malloc.h>
#include <sys/cons.h>
#include <sys/proc.h>
#include <sys/reboot.h>
#include <geom/geom_disk.h>
#endif /* _KERNEL */

#ifndef _KERNEL
#include <stdio.h>
#include <string.h>
#endif /* _KERNEL */

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_periph.h>
#include <cam/cam_xpt_periph.h>
#include <cam/cam_sim.h>
#include <cam/cam_iosched.h>

#include <cam/nvme/nvme_all.h>

typedef enum {
	NDA_STATE_NORMAL
} nda_state;

typedef enum {
	NDA_FLAG_OPEN		= 0x0001,
	NDA_FLAG_DIRTY		= 0x0002,
	NDA_FLAG_SCTX_INIT	= 0x0004,
} nda_flags;

typedef enum {
	NDA_Q_4K   = 0x01,
	NDA_Q_NONE = 0x00,
} nda_quirks;
	
#define NDA_Q_BIT_STRING	\
	"\020"			\
	"\001Bit 0"

typedef enum {
	NDA_CCB_BUFFER_IO	= 0x01,
	NDA_CCB_DUMP            = 0x02,
	NDA_CCB_TRIM            = 0x03,
	NDA_CCB_TYPE_MASK	= 0x0F,
} nda_ccb_state;

/* Offsets into our private area for storing information */
#define ccb_state	ccb_h.ppriv_field0
#define ccb_bp		ccb_h.ppriv_ptr1	/* For NDA_CCB_BUFFER_IO */
#define ccb_trim	ccb_h.ppriv_ptr1	/* For NDA_CCB_TRIM */

struct nda_softc {
	struct   cam_iosched_softc *cam_iosched;
	int			outstanding_cmds;	/* Number of active commands */
	int			refcount;		/* Active xpt_action() calls */
	nda_state		state;
	nda_flags		flags;
	nda_quirks		quirks;
	int			unmappedio;
	quad_t			deletes;
	uint32_t		nsid;			/* Namespace ID for this nda device */
	struct disk		*disk;
	struct task		sysctl_task;
	struct sysctl_ctx_list	sysctl_ctx;
	struct sysctl_oid	*sysctl_tree;
	uint64_t		trim_count;
	uint64_t		trim_ranges;
	uint64_t		trim_lbas;
#ifdef CAM_TEST_FAILURE
	int			force_read_error;
	int			force_write_error;
	int			periodic_read_error;
	int			periodic_read_count;
#endif
#ifdef CAM_IO_STATS
	struct sysctl_ctx_list	sysctl_stats_ctx;
	struct sysctl_oid	*sysctl_stats_tree;
	u_int			timeouts;
	u_int			errors;
	u_int			invalidations;
#endif
};

struct nda_trim_request {
	union {
		struct nvme_dsm_range dsm;
		uint8_t		data[NVME_MAX_DSM_TRIM];
	};
	TAILQ_HEAD(, bio) bps;
};

/* Need quirk table */

static	disk_strategy_t	ndastrategy;
static	dumper_t	ndadump;
static	periph_init_t	ndainit;
static	void		ndaasync(void *callback_arg, u_int32_t code,
				struct cam_path *path, void *arg);
static	void		ndasysctlinit(void *context, int pending);
static	periph_ctor_t	ndaregister;
static	periph_dtor_t	ndacleanup;
static	periph_start_t	ndastart;
static	periph_oninv_t	ndaoninvalidate;
static	void		ndadone(struct cam_periph *periph,
			       union ccb *done_ccb);
static  int		ndaerror(union ccb *ccb, u_int32_t cam_flags,
				u_int32_t sense_flags);
static void		ndashutdown(void *arg, int howto);
static void		ndasuspend(void *arg);

#ifndef	NDA_DEFAULT_SEND_ORDERED
#define	NDA_DEFAULT_SEND_ORDERED	1
#endif
#ifndef NDA_DEFAULT_TIMEOUT
#define NDA_DEFAULT_TIMEOUT 30	/* Timeout in seconds */
#endif
#ifndef	NDA_DEFAULT_RETRY
#define	NDA_DEFAULT_RETRY	4
#endif
#ifndef NDA_MAX_TRIM_ENTRIES
#define NDA_MAX_TRIM_ENTRIES  (NVME_MAX_DSM_TRIM / sizeof(struct nvme_dsm_range))/* Number of DSM trims to use, max 256 */
#endif

static SYSCTL_NODE(_kern_cam, OID_AUTO, nda, CTLFLAG_RD, 0,
            "CAM Direct Access Disk driver");

//static int nda_retry_count = NDA_DEFAULT_RETRY;
static int nda_send_ordered = NDA_DEFAULT_SEND_ORDERED;
static int nda_default_timeout = NDA_DEFAULT_TIMEOUT;
static int nda_max_trim_entries = NDA_MAX_TRIM_ENTRIES;
SYSCTL_INT(_kern_cam_nda, OID_AUTO, max_trim, CTLFLAG_RDTUN,
    &nda_max_trim_entries, NDA_MAX_TRIM_ENTRIES,
    "Maximum number of BIO_DELETE to send down as a DSM TRIM.");

/*
 * All NVMe media is non-rotational, so all nvme device instances
 * share this to implement the sysctl.
 */
static int nda_rotating_media = 0;

static struct periph_driver ndadriver =
{
	ndainit, "nda",
	TAILQ_HEAD_INITIALIZER(ndadriver.units), /* generation */ 0
};

PERIPHDRIVER_DECLARE(nda, ndadriver);

static MALLOC_DEFINE(M_NVMEDA, "nvme_da", "nvme_da buffers");

/*
 * nice wrappers. Maybe these belong in nvme_all.c instead of
 * here, but this is the only place that uses these. Should
 * we ever grow another NVME periph, we should move them
 * all there wholesale.
 */

static void
nda_nvme_flush(struct nda_softc *softc, struct ccb_nvmeio *nvmeio)
{
	cam_fill_nvmeio(nvmeio,
	    0,			/* retries */
	    ndadone,		/* cbfcnp */
	    CAM_DIR_NONE,	/* flags */
	    NULL,		/* data_ptr */
	    0,			/* dxfer_len */
	    nda_default_timeout * 1000); /* timeout 30s */
	nvme_ns_flush_cmd(&nvmeio->cmd, softc->nsid);
}

static void
nda_nvme_trim(struct nda_softc *softc, struct ccb_nvmeio *nvmeio,
    void *payload, uint32_t num_ranges)
{
	cam_fill_nvmeio(nvmeio,
	    0,			/* retries */
	    ndadone,		/* cbfcnp */
	    CAM_DIR_OUT,	/* flags */
	    payload,		/* data_ptr */
	    num_ranges * sizeof(struct nvme_dsm_range), /* dxfer_len */
	    nda_default_timeout * 1000); /* timeout 30s */
	nvme_ns_trim_cmd(&nvmeio->cmd, softc->nsid, num_ranges);
}

static void
nda_nvme_write(struct nda_softc *softc, struct ccb_nvmeio *nvmeio,
    void *payload, uint64_t lba, uint32_t len, uint32_t count)
{
	cam_fill_nvmeio(nvmeio,
	    0,			/* retries */
	    ndadone,		/* cbfcnp */
	    CAM_DIR_OUT,	/* flags */
	    payload,		/* data_ptr */
	    len,		/* dxfer_len */
	    nda_default_timeout * 1000); /* timeout 30s */
	nvme_ns_write_cmd(&nvmeio->cmd, softc->nsid, lba, count);
}

static void
nda_nvme_rw_bio(struct nda_softc *softc, struct ccb_nvmeio *nvmeio,
    struct bio *bp, uint32_t rwcmd)
{
	int flags = rwcmd == NVME_OPC_READ ? CAM_DIR_IN : CAM_DIR_OUT;
	void *payload;
	uint64_t lba;
	uint32_t count;

	if (bp->bio_flags & BIO_UNMAPPED) {
		flags |= CAM_DATA_BIO;
		payload = bp;
	} else {
		payload = bp->bio_data;
	}

	lba = bp->bio_pblkno;
	count = bp->bio_bcount / softc->disk->d_sectorsize;

	cam_fill_nvmeio(nvmeio,
	    0,			/* retries */
	    ndadone,		/* cbfcnp */
	    flags,		/* flags */
	    payload,		/* data_ptr */
	    bp->bio_bcount,	/* dxfer_len */
	    nda_default_timeout * 1000); /* timeout 30s */
	nvme_ns_rw_cmd(&nvmeio->cmd, rwcmd, softc->nsid, lba, count);
}

static int
ndaopen(struct disk *dp)
{
	struct cam_periph *periph;
	struct nda_softc *softc;
	int error;

	periph = (struct cam_periph *)dp->d_drv1;
	if (cam_periph_acquire(periph) != 0) {
		return(ENXIO);
	}

	cam_periph_lock(periph);
	if ((error = cam_periph_hold(periph, PRIBIO|PCATCH)) != 0) {
		cam_periph_unlock(periph);
		cam_periph_release(periph);
		return (error);
	}

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE | CAM_DEBUG_PERIPH,
	    ("ndaopen\n"));

	softc = (struct nda_softc *)periph->softc;
	softc->flags |= NDA_FLAG_OPEN;

	cam_periph_unhold(periph);
	cam_periph_unlock(periph);
	return (0);
}

static int
ndaclose(struct disk *dp)
{
	struct	cam_periph *periph;
	struct	nda_softc *softc;
	union ccb *ccb;
	int error;

	periph = (struct cam_periph *)dp->d_drv1;
	softc = (struct nda_softc *)periph->softc;
	cam_periph_lock(periph);

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE | CAM_DEBUG_PERIPH,
	    ("ndaclose\n"));

	if ((softc->flags & NDA_FLAG_DIRTY) != 0 &&
	    (periph->flags & CAM_PERIPH_INVALID) == 0 &&
	    cam_periph_hold(periph, PRIBIO) == 0) {

		ccb = cam_periph_getccb(periph, CAM_PRIORITY_NORMAL);
		nda_nvme_flush(softc, &ccb->nvmeio);
		error = cam_periph_runccb(ccb, ndaerror, /*cam_flags*/0,
		    /*sense_flags*/0, softc->disk->d_devstat);

		if (error != 0)
			xpt_print(periph->path, "Synchronize cache failed\n");
		else
			softc->flags &= ~NDA_FLAG_DIRTY;
		xpt_release_ccb(ccb);
		cam_periph_unhold(periph);
	}

	softc->flags &= ~NDA_FLAG_OPEN;

	while (softc->refcount != 0)
		cam_periph_sleep(periph, &softc->refcount, PRIBIO, "ndaclose", 1);
	KASSERT(softc->outstanding_cmds == 0,
	    ("nda %d outstanding commands", softc->outstanding_cmds));
	cam_periph_unlock(periph);
	cam_periph_release(periph);
	return (0);	
}

static void
ndaschedule(struct cam_periph *periph)
{
	struct nda_softc *softc = (struct nda_softc *)periph->softc;

	if (softc->state != NDA_STATE_NORMAL)
		return;

	cam_iosched_schedule(softc->cam_iosched, periph);
}

/*
 * Actually translate the requested transfer into one the physical driver
 * can understand.  The transfer is described by a buf and will include
 * only one physical transfer.
 */
static void
ndastrategy(struct bio *bp)
{
	struct cam_periph *periph;
	struct nda_softc *softc;
	
	periph = (struct cam_periph *)bp->bio_disk->d_drv1;
	softc = (struct nda_softc *)periph->softc;

	cam_periph_lock(periph);

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("ndastrategy(%p)\n", bp));

	/*
	 * If the device has been made invalid, error out
	 */
	if ((periph->flags & CAM_PERIPH_INVALID) != 0) {
		cam_periph_unlock(periph);
		biofinish(bp, NULL, ENXIO);
		return;
	}
	
	if (bp->bio_cmd == BIO_DELETE)
		softc->deletes++;

	/*
	 * Place it in the queue of disk activities for this disk
	 */
	cam_iosched_queue_work(softc->cam_iosched, bp);

	/*
	 * Schedule ourselves for performing the work.
	 */
	ndaschedule(periph);
	cam_periph_unlock(periph);

	return;
}

static int
ndadump(void *arg, void *virtual, vm_offset_t physical, off_t offset, size_t length)
{
	struct	    cam_periph *periph;
	struct	    nda_softc *softc;
	u_int	    secsize;
	struct ccb_nvmeio nvmeio;
	struct	    disk *dp;
	uint64_t    lba;
	uint32_t    count;
	int	    error = 0;

	dp = arg;
	periph = dp->d_drv1;
	softc = (struct nda_softc *)periph->softc;
	secsize = softc->disk->d_sectorsize;
	lba = offset / secsize;
	count = length / secsize;
	
	if ((periph->flags & CAM_PERIPH_INVALID) != 0)
		return (ENXIO);

	/* xpt_get_ccb returns a zero'd allocation for the ccb, mimic that here */
	memset(&nvmeio, 0, sizeof(nvmeio));
	if (length > 0) {
		xpt_setup_ccb(&nvmeio.ccb_h, periph->path, CAM_PRIORITY_NORMAL);
		nvmeio.ccb_state = NDA_CCB_DUMP;
		nda_nvme_write(softc, &nvmeio, virtual, lba, length, count);
		error = cam_periph_runccb((union ccb *)&nvmeio, cam_periph_error,
		    0, SF_NO_RECOVERY | SF_NO_RETRY, NULL);
		if (error != 0)
			printf("Aborting dump due to I/O error %d.\n", error);

		return (error);
	}
	
	/* Flush */
	xpt_setup_ccb(&nvmeio.ccb_h, periph->path, CAM_PRIORITY_NORMAL);

	nvmeio.ccb_state = NDA_CCB_DUMP;
	nda_nvme_flush(softc, &nvmeio);
	error = cam_periph_runccb((union ccb *)&nvmeio, cam_periph_error,
	    0, SF_NO_RECOVERY | SF_NO_RETRY, NULL);
	if (error != 0)
		xpt_print(periph->path, "flush cmd failed\n");
	return (error);
}

static void
ndainit(void)
{
	cam_status status;

	/*
	 * Install a global async callback.  This callback will
	 * receive async callbacks like "new device found".
	 */
	status = xpt_register_async(AC_FOUND_DEVICE, ndaasync, NULL, NULL);

	if (status != CAM_REQ_CMP) {
		printf("nda: Failed to attach master async callback "
		       "due to status 0x%x!\n", status);
	} else if (nda_send_ordered) {

		/* Register our event handlers */
		if ((EVENTHANDLER_REGISTER(power_suspend, ndasuspend,
					   NULL, EVENTHANDLER_PRI_LAST)) == NULL)
		    printf("ndainit: power event registration failed!\n");
		if ((EVENTHANDLER_REGISTER(shutdown_post_sync, ndashutdown,
					   NULL, SHUTDOWN_PRI_DEFAULT)) == NULL)
		    printf("ndainit: shutdown event registration failed!\n");
	}
}

/*
 * Callback from GEOM, called when it has finished cleaning up its
 * resources.
 */
static void
ndadiskgonecb(struct disk *dp)
{
	struct cam_periph *periph;

	periph = (struct cam_periph *)dp->d_drv1;

	cam_periph_release(periph);
}

static void
ndaoninvalidate(struct cam_periph *periph)
{
	struct nda_softc *softc;

	softc = (struct nda_softc *)periph->softc;

	/*
	 * De-register any async callbacks.
	 */
	xpt_register_async(0, ndaasync, periph, periph->path);
#ifdef CAM_IO_STATS
	softc->invalidations++;
#endif

	/*
	 * Return all queued I/O with ENXIO.
	 * XXX Handle any transactions queued to the card
	 *     with XPT_ABORT_CCB.
	 */
	cam_iosched_flush(softc->cam_iosched, NULL, ENXIO);

	disk_gone(softc->disk);
}

static void
ndacleanup(struct cam_periph *periph)
{
	struct nda_softc *softc;

	softc = (struct nda_softc *)periph->softc;

	cam_periph_unlock(periph);

	cam_iosched_fini(softc->cam_iosched);

	/*
	 * If we can't free the sysctl tree, oh well...
	 */
	if ((softc->flags & NDA_FLAG_SCTX_INIT) != 0) {
#ifdef CAM_IO_STATS
		if (sysctl_ctx_free(&softc->sysctl_stats_ctx) != 0)
			xpt_print(periph->path,
			    "can't remove sysctl stats context\n");
#endif
		if (sysctl_ctx_free(&softc->sysctl_ctx) != 0)
			xpt_print(periph->path,
			    "can't remove sysctl context\n");
	}

	disk_destroy(softc->disk);
	free(softc, M_DEVBUF);
	cam_periph_lock(periph);
}

static void
ndaasync(void *callback_arg, u_int32_t code,
	struct cam_path *path, void *arg)
{
	struct cam_periph *periph;

	periph = (struct cam_periph *)callback_arg;
	switch (code) {
	case AC_FOUND_DEVICE:
	{
		struct ccb_getdev *cgd;
		cam_status status;
 
		cgd = (struct ccb_getdev *)arg;
		if (cgd == NULL)
			break;

		if (cgd->protocol != PROTO_NVME)
			break;

		/*
		 * Allocate a peripheral instance for
		 * this device and start the probe
		 * process.
		 */
		status = cam_periph_alloc(ndaregister, ndaoninvalidate,
					  ndacleanup, ndastart,
					  "nda", CAM_PERIPH_BIO,
					  path, ndaasync,
					  AC_FOUND_DEVICE, cgd);

		if (status != CAM_REQ_CMP
		 && status != CAM_REQ_INPROG)
			printf("ndaasync: Unable to attach to new device "
				"due to status 0x%x\n", status);
		break;
	}
	case AC_ADVINFO_CHANGED:
	{
		uintptr_t buftype;

		buftype = (uintptr_t)arg;
		if (buftype == CDAI_TYPE_PHYS_PATH) {
			struct nda_softc *softc;

			softc = periph->softc;
			disk_attr_changed(softc->disk, "GEOM::physpath",
					  M_NOWAIT);
		}
		break;
	}
	case AC_LOST_DEVICE:
	default:
		cam_periph_async(periph, code, path, arg);
		break;
	}
}

static void
ndasysctlinit(void *context, int pending)
{
	struct cam_periph *periph;
	struct nda_softc *softc;
	char tmpstr[32], tmpstr2[16];

	periph = (struct cam_periph *)context;

	/* periph was held for us when this task was enqueued */
	if ((periph->flags & CAM_PERIPH_INVALID) != 0) {
		cam_periph_release(periph);
		return;
	}

	softc = (struct nda_softc *)periph->softc;
	snprintf(tmpstr, sizeof(tmpstr), "CAM NDA unit %d", periph->unit_number);
	snprintf(tmpstr2, sizeof(tmpstr2), "%d", periph->unit_number);

	sysctl_ctx_init(&softc->sysctl_ctx);
	softc->flags |= NDA_FLAG_SCTX_INIT;
	softc->sysctl_tree = SYSCTL_ADD_NODE_WITH_LABEL(&softc->sysctl_ctx,
		SYSCTL_STATIC_CHILDREN(_kern_cam_nda), OID_AUTO, tmpstr2,
		CTLFLAG_RD, 0, tmpstr, "device_index");
	if (softc->sysctl_tree == NULL) {
		printf("ndasysctlinit: unable to allocate sysctl tree\n");
		cam_periph_release(periph);
		return;
	}

	SYSCTL_ADD_INT(&softc->sysctl_ctx, SYSCTL_CHILDREN(softc->sysctl_tree),
	    OID_AUTO, "unmapped_io", CTLFLAG_RD,
	    &softc->unmappedio, 0, "Unmapped I/O leaf");

	SYSCTL_ADD_QUAD(&softc->sysctl_ctx, SYSCTL_CHILDREN(softc->sysctl_tree),
	    OID_AUTO, "deletes", CTLFLAG_RD,
	    &softc->deletes, "Number of BIO_DELETE requests");

	SYSCTL_ADD_UQUAD(&softc->sysctl_ctx,
		SYSCTL_CHILDREN(softc->sysctl_tree), OID_AUTO,
		"trim_count", CTLFLAG_RD, &softc->trim_count,
		"Total number of unmap/dsm commands sent");
	SYSCTL_ADD_UQUAD(&softc->sysctl_ctx,
		SYSCTL_CHILDREN(softc->sysctl_tree), OID_AUTO,
		"trim_ranges", CTLFLAG_RD, &softc->trim_ranges,
		"Total number of ranges in unmap/dsm commands");
	SYSCTL_ADD_UQUAD(&softc->sysctl_ctx,
		SYSCTL_CHILDREN(softc->sysctl_tree), OID_AUTO,
		"trim_lbas", CTLFLAG_RD, &softc->trim_lbas,
		"Total lbas in the unmap/dsm commands sent");

	SYSCTL_ADD_INT(&softc->sysctl_ctx, SYSCTL_CHILDREN(softc->sysctl_tree),
	    OID_AUTO, "rotating", CTLFLAG_RD, &nda_rotating_media, 1,
	    "Rotating media");

#ifdef CAM_IO_STATS
	softc->sysctl_stats_tree = SYSCTL_ADD_NODE(&softc->sysctl_stats_ctx,
		SYSCTL_CHILDREN(softc->sysctl_tree), OID_AUTO, "stats",
		CTLFLAG_RD, 0, "Statistics");
	if (softc->sysctl_stats_tree == NULL) {
		printf("ndasysctlinit: unable to allocate sysctl tree for stats\n");
		cam_periph_release(periph);
		return;
	}
	SYSCTL_ADD_INT(&softc->sysctl_stats_ctx,
		SYSCTL_CHILDREN(softc->sysctl_stats_tree),
		OID_AUTO, "timeouts", CTLFLAG_RD,
		&softc->timeouts, 0,
		"Device timeouts reported by the SIM");
	SYSCTL_ADD_INT(&softc->sysctl_stats_ctx,
		SYSCTL_CHILDREN(softc->sysctl_stats_tree),
		OID_AUTO, "errors", CTLFLAG_RD,
		&softc->errors, 0,
		"Transport errors reported by the SIM.");
	SYSCTL_ADD_INT(&softc->sysctl_stats_ctx,
		SYSCTL_CHILDREN(softc->sysctl_stats_tree),
		OID_AUTO, "pack_invalidations", CTLFLAG_RD,
		&softc->invalidations, 0,
		"Device pack invalidations.");
#endif

#ifdef CAM_TEST_FAILURE
	SYSCTL_ADD_PROC(&softc->sysctl_ctx, SYSCTL_CHILDREN(softc->sysctl_tree),
		OID_AUTO, "invalidate", CTLTYPE_U64 | CTLFLAG_RW | CTLFLAG_MPSAFE,
		periph, 0, cam_periph_invalidate_sysctl, "I",
		"Write 1 to invalidate the drive immediately");
#endif

	cam_iosched_sysctl_init(softc->cam_iosched, &softc->sysctl_ctx,
	    softc->sysctl_tree);

	cam_periph_release(periph);
}

static int
ndagetattr(struct bio *bp)
{
	int ret;
	struct cam_periph *periph;

	periph = (struct cam_periph *)bp->bio_disk->d_drv1;
	cam_periph_lock(periph);
	ret = xpt_getattr(bp->bio_data, bp->bio_length, bp->bio_attribute,
	    periph->path);
	cam_periph_unlock(periph);
	if (ret == 0)
		bp->bio_completed = bp->bio_length;
	return ret;
}

static cam_status
ndaregister(struct cam_periph *periph, void *arg)
{
	struct nda_softc *softc;
	struct disk *disk;
	struct ccb_pathinq cpi;
	const struct nvme_namespace_data *nsd;
	const struct nvme_controller_data *cd;
	char   announce_buf[80];
	uint8_t flbas_fmt, lbads, vwc_present;
	u_int maxio;
	int quirks;

	nsd = nvme_get_identify_ns(periph);
	cd = nvme_get_identify_cntrl(periph);

	softc = (struct nda_softc *)malloc(sizeof(*softc), M_DEVBUF,
	    M_NOWAIT | M_ZERO);

	if (softc == NULL) {
		printf("ndaregister: Unable to probe new device. "
		    "Unable to allocate softc\n");
		return(CAM_REQ_CMP_ERR);
	}

	if (cam_iosched_init(&softc->cam_iosched, periph) != 0) {
		printf("ndaregister: Unable to probe new device. "
		       "Unable to allocate iosched memory\n");
		free(softc, M_DEVBUF);
		return(CAM_REQ_CMP_ERR);
	}

	/* ident_data parsing */

	periph->softc = softc;

	softc->quirks = NDA_Q_NONE;

	xpt_path_inq(&cpi, periph->path);

	TASK_INIT(&softc->sysctl_task, 0, ndasysctlinit, periph);

	/*
	 * The name space ID is the lun, save it for later I/O
	 */
	softc->nsid = (uint32_t)xpt_path_lun_id(periph->path);

	/*
	 * Register this media as a disk
	 */
	(void)cam_periph_hold(periph, PRIBIO);
	cam_periph_unlock(periph);
	snprintf(announce_buf, sizeof(announce_buf),
	    "kern.cam.nda.%d.quirks", periph->unit_number);
	quirks = softc->quirks;
	TUNABLE_INT_FETCH(announce_buf, &quirks);
	softc->quirks = quirks;
	cam_iosched_set_sort_queue(softc->cam_iosched, 0);
	softc->disk = disk = disk_alloc();
	strlcpy(softc->disk->d_descr, cd->mn,
	    MIN(sizeof(softc->disk->d_descr), sizeof(cd->mn)));
	strlcpy(softc->disk->d_ident, cd->sn,
	    MIN(sizeof(softc->disk->d_ident), sizeof(cd->sn)));
	disk->d_rotation_rate = DISK_RR_NON_ROTATING;
	disk->d_open = ndaopen;
	disk->d_close = ndaclose;
	disk->d_strategy = ndastrategy;
	disk->d_getattr = ndagetattr;
	disk->d_dump = ndadump;
	disk->d_gone = ndadiskgonecb;
	disk->d_name = "nda";
	disk->d_drv1 = periph;
	disk->d_unit = periph->unit_number;
	maxio = cpi.maxio;		/* Honor max I/O size of SIM */
	if (maxio == 0)
		maxio = DFLTPHYS;	/* traditional default */
	else if (maxio > MAXPHYS)
		maxio = MAXPHYS;	/* for safety */
	disk->d_maxsize = maxio;
	flbas_fmt = (nsd->flbas >> NVME_NS_DATA_FLBAS_FORMAT_SHIFT) &
		NVME_NS_DATA_FLBAS_FORMAT_MASK;
	lbads = (nsd->lbaf[flbas_fmt] >> NVME_NS_DATA_LBAF_LBADS_SHIFT) &
		NVME_NS_DATA_LBAF_LBADS_MASK;
	disk->d_sectorsize = 1 << lbads;
	disk->d_mediasize = (off_t)(disk->d_sectorsize * nsd->nsze);
	disk->d_delmaxsize = disk->d_mediasize;
	disk->d_flags = DISKFLAG_DIRECT_COMPLETION;
	if (nvme_ctrlr_has_dataset_mgmt(cd))
		disk->d_flags |= DISKFLAG_CANDELETE;
	vwc_present = (cd->vwc >> NVME_CTRLR_DATA_VWC_PRESENT_SHIFT) &
		NVME_CTRLR_DATA_VWC_PRESENT_MASK;
	if (vwc_present)
		disk->d_flags |= DISKFLAG_CANFLUSHCACHE;
	if ((cpi.hba_misc & PIM_UNMAPPED) != 0) {
		disk->d_flags |= DISKFLAG_UNMAPPED_BIO;
		softc->unmappedio = 1;
	}
	/*
	 * d_ident and d_descr are both far bigger than the length of either
	 *  the serial or model number strings.
	 */
	nvme_strvis(disk->d_descr, cd->mn,
	    sizeof(disk->d_descr), NVME_MODEL_NUMBER_LENGTH);
	nvme_strvis(disk->d_ident, cd->sn,
	    sizeof(disk->d_ident), NVME_SERIAL_NUMBER_LENGTH);
	disk->d_hba_vendor = cpi.hba_vendor;
	disk->d_hba_device = cpi.hba_device;
	disk->d_hba_subvendor = cpi.hba_subvendor;
	disk->d_hba_subdevice = cpi.hba_subdevice;
	disk->d_stripesize = disk->d_sectorsize;
	disk->d_stripeoffset = 0;
	disk->d_devstat = devstat_new_entry(periph->periph_name,
	    periph->unit_number, disk->d_sectorsize,
	    DEVSTAT_ALL_SUPPORTED,
	    DEVSTAT_TYPE_DIRECT | XPORT_DEVSTAT_TYPE(cpi.transport),
	    DEVSTAT_PRIORITY_DISK);
	/*
	 * Add alias for older nvd drives to ease transition.
	 */
	/* disk_add_alias(disk, "nvd"); Have reports of this causing problems */

	/*
	 * Acquire a reference to the periph before we register with GEOM.
	 * We'll release this reference once GEOM calls us back (via
	 * ndadiskgonecb()) telling us that our provider has been freed.
	 */
	if (cam_periph_acquire(periph) != 0) {
		xpt_print(periph->path, "%s: lost periph during "
			  "registration!\n", __func__);
		cam_periph_lock(periph);
		return (CAM_REQ_CMP_ERR);
	}
	disk_create(softc->disk, DISK_VERSION);
	cam_periph_lock(periph);
	cam_periph_unhold(periph);

	snprintf(announce_buf, sizeof(announce_buf),
		"%juMB (%ju %u byte sectors)",
	    (uintmax_t)((uintmax_t)disk->d_mediasize / (1024*1024)),
		(uintmax_t)disk->d_mediasize / disk->d_sectorsize,
		disk->d_sectorsize);
	xpt_announce_periph(periph, announce_buf);
	xpt_announce_quirks(periph, softc->quirks, NDA_Q_BIT_STRING);

	/*
	 * Create our sysctl variables, now that we know
	 * we have successfully attached.
	 */
	if (cam_periph_acquire(periph) == 0)
		taskqueue_enqueue(taskqueue_thread, &softc->sysctl_task);

	/*
	 * Register for device going away and info about the drive
	 * changing (though with NVMe, it can't)
	 */
	xpt_register_async(AC_LOST_DEVICE | AC_ADVINFO_CHANGED,
	    ndaasync, periph, periph->path);

	softc->state = NDA_STATE_NORMAL;
	return(CAM_REQ_CMP);
}

static void
ndastart(struct cam_periph *periph, union ccb *start_ccb)
{
	struct nda_softc *softc = (struct nda_softc *)periph->softc;
	struct ccb_nvmeio *nvmeio = &start_ccb->nvmeio;

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("ndastart\n"));

	switch (softc->state) {
	case NDA_STATE_NORMAL:
	{
		struct bio *bp;

		bp = cam_iosched_next_bio(softc->cam_iosched);
		CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("ndastart: bio %p\n", bp));
		if (bp == NULL) {
			xpt_release_ccb(start_ccb);
			break;
		}

		switch (bp->bio_cmd) {
		case BIO_WRITE:
			softc->flags |= NDA_FLAG_DIRTY;
			/* FALLTHROUGH */
		case BIO_READ:
		{
#ifdef CAM_TEST_FAILURE
			int fail = 0;

			/*
			 * Support the failure ioctls.  If the command is a
			 * read, and there are pending forced read errors, or
			 * if a write and pending write errors, then fail this
			 * operation with EIO.  This is useful for testing
			 * purposes.  Also, support having every Nth read fail.
			 *
			 * This is a rather blunt tool.
			 */
			if (bp->bio_cmd == BIO_READ) {
				if (softc->force_read_error) {
					softc->force_read_error--;
					fail = 1;
				}
				if (softc->periodic_read_error > 0) {
					if (++softc->periodic_read_count >=
					    softc->periodic_read_error) {
						softc->periodic_read_count = 0;
						fail = 1;
					}
				}
			} else {
				if (softc->force_write_error) {
					softc->force_write_error--;
					fail = 1;
				}
			}
			if (fail) {
				biofinish(bp, NULL, EIO);
				xpt_release_ccb(start_ccb);
				ndaschedule(periph);
				return;
			}
#endif
			KASSERT((bp->bio_flags & BIO_UNMAPPED) == 0 ||
			    round_page(bp->bio_bcount + bp->bio_ma_offset) /
			    PAGE_SIZE == bp->bio_ma_n,
			    ("Short bio %p", bp));
			nda_nvme_rw_bio(softc, &start_ccb->nvmeio, bp, bp->bio_cmd == BIO_READ ?
			    NVME_OPC_READ : NVME_OPC_WRITE);
			break;
		}
		case BIO_DELETE:
		{
			struct nvme_dsm_range *dsm_range, *dsm_end;
			struct nda_trim_request *trim;
			struct bio *bp1;
			int ents;
			uint32_t totalcount = 0, ranges = 0;

			trim = malloc(sizeof(*trim), M_NVMEDA, M_ZERO | M_NOWAIT);
			if (trim == NULL) {
				biofinish(bp, NULL, ENOMEM);
				xpt_release_ccb(start_ccb);
				ndaschedule(periph);
				return;
			}
			TAILQ_INIT(&trim->bps);
			bp1 = bp;
			ents = sizeof(trim->data) / sizeof(struct nvme_dsm_range);
			ents = min(ents, nda_max_trim_entries);
			dsm_range = &trim->dsm;
			dsm_end = dsm_range + ents;
			do {
				TAILQ_INSERT_TAIL(&trim->bps, bp1, bio_queue);
				dsm_range->length =
				    htole32(bp1->bio_bcount / softc->disk->d_sectorsize);
				dsm_range->starting_lba =
				    htole64(bp1->bio_offset / softc->disk->d_sectorsize);
				ranges++;
				totalcount += dsm_range->length;
				dsm_range++;
				if (dsm_range >= dsm_end)
					break;
				bp1 = cam_iosched_next_trim(softc->cam_iosched);
				/* XXX -- Could collapse adjacent ranges, but we don't for now */
				/* XXX -- Could limit based on total payload size */
			} while (bp1 != NULL);
			start_ccb->ccb_trim = trim;
			nda_nvme_trim(softc, &start_ccb->nvmeio, &trim->dsm,
			    dsm_range - &trim->dsm);
			start_ccb->ccb_state = NDA_CCB_TRIM;
			softc->trim_count++;
			softc->trim_ranges += ranges;
			softc->trim_lbas += totalcount;
			/*
			 * Note: We can have multiple TRIMs in flight, so we don't call
			 * cam_iosched_submit_trim(softc->cam_iosched);
			 * since that forces the I/O scheduler to only schedule one at a time.
			 * On NVMe drives, this is a performance disaster.
			 */
			goto out;
		}
		case BIO_FLUSH:
			nda_nvme_flush(softc, nvmeio);
			break;
		}
		start_ccb->ccb_state = NDA_CCB_BUFFER_IO;
		start_ccb->ccb_bp = bp;
out:
		start_ccb->ccb_h.flags |= CAM_UNLOCKED;
		softc->outstanding_cmds++;
		softc->refcount++;			/* For submission only */
		cam_periph_unlock(periph);
		xpt_action(start_ccb);
		cam_periph_lock(periph);
		softc->refcount--;			/* Submission done */

		/* May have more work to do, so ensure we stay scheduled */
		ndaschedule(periph);
		break;
		}
	}
}

static void
ndadone(struct cam_periph *periph, union ccb *done_ccb)
{
	struct nda_softc *softc;
	struct ccb_nvmeio *nvmeio = &done_ccb->nvmeio;
	struct cam_path *path;
	int state;

	softc = (struct nda_softc *)periph->softc;
	path = done_ccb->ccb_h.path;

	CAM_DEBUG(path, CAM_DEBUG_TRACE, ("ndadone\n"));

	state = nvmeio->ccb_state & NDA_CCB_TYPE_MASK;
	switch (state) {
	case NDA_CCB_BUFFER_IO:
	case NDA_CCB_TRIM:
	{
		int error;

		cam_periph_lock(periph);
		if ((done_ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
			error = ndaerror(done_ccb, 0, 0);
			if (error == ERESTART) {
				/* A retry was scheduled, so just return. */
				cam_periph_unlock(periph);
				return;
			}
			if ((done_ccb->ccb_h.status & CAM_DEV_QFRZN) != 0)
				cam_release_devq(path,
						 /*relsim_flags*/0,
						 /*reduction*/0,
						 /*timeout*/0,
						 /*getcount_only*/0);
		} else {
			if ((done_ccb->ccb_h.status & CAM_DEV_QFRZN) != 0)
				panic("REQ_CMP with QFRZN");
			error = 0;
		}
		if (state == NDA_CCB_BUFFER_IO) {
			struct bio *bp;

			bp = (struct bio *)done_ccb->ccb_bp;
			bp->bio_error = error;
			if (error != 0) {
				bp->bio_resid = bp->bio_bcount;
				bp->bio_flags |= BIO_ERROR;
			} else {
				bp->bio_resid = 0;
			}
			softc->outstanding_cmds--;

			/*
			 * We need to call cam_iosched before we call biodone so that we
			 * don't measure any activity that happens in the completion
			 * routine, which in the case of sendfile can be quite
			 * extensive.
			 */
			cam_iosched_bio_complete(softc->cam_iosched, bp, done_ccb);
			xpt_release_ccb(done_ccb);
			ndaschedule(periph);
			cam_periph_unlock(periph);
			biodone(bp);
		} else { /* state == NDA_CCB_TRIM */
			struct nda_trim_request *trim;
			struct bio *bp1, *bp2;
			TAILQ_HEAD(, bio) queue;

			trim = nvmeio->ccb_trim;
			TAILQ_INIT(&queue);
			TAILQ_CONCAT(&queue, &trim->bps, bio_queue);
			free(trim, M_NVMEDA);

			/*
			 * Since we can have multiple trims in flight, we don't
			 * need to call this here.
			 * cam_iosched_trim_done(softc->cam_iosched);
			 */
			/*
			 * The the I/O scheduler that we're finishing the I/O
			 * so we can keep book. The first one we pass in the CCB
			 * which has the timing information. The rest we pass in NULL
			 * so we can keep proper counts.
			 */
			bp1 = TAILQ_FIRST(&queue);
			cam_iosched_bio_complete(softc->cam_iosched, bp1, done_ccb);
			xpt_release_ccb(done_ccb);
			softc->outstanding_cmds--;
			ndaschedule(periph);
			cam_periph_unlock(periph);
			while ((bp2 = TAILQ_FIRST(&queue)) != NULL) {
				TAILQ_REMOVE(&queue, bp2, bio_queue);
				bp2->bio_error = error;
				if (error != 0) {
					bp2->bio_flags |= BIO_ERROR;
					bp2->bio_resid = bp1->bio_bcount;
				} else
					bp2->bio_resid = 0;
				if (bp1 != bp2)
					cam_iosched_bio_complete(softc->cam_iosched, bp2, NULL);
				biodone(bp2);
			}
		}
		return;
	}
	case NDA_CCB_DUMP:
		/* No-op.  We're polling */
		return;
	default:
		break;
	}
	xpt_release_ccb(done_ccb);
}

static int
ndaerror(union ccb *ccb, u_int32_t cam_flags, u_int32_t sense_flags)
{
	struct nda_softc *softc;
	struct cam_periph *periph;

	periph = xpt_path_periph(ccb->ccb_h.path);
	softc = (struct nda_softc *)periph->softc;

	switch (ccb->ccb_h.status & CAM_STATUS_MASK) {
	case CAM_CMD_TIMEOUT:
#ifdef CAM_IO_STATS
		softc->timeouts++;
#endif
		break;
	case CAM_REQ_ABORTED:
	case CAM_REQ_CMP_ERR:
	case CAM_REQ_TERMIO:
	case CAM_UNREC_HBA_ERROR:
	case CAM_DATA_RUN_ERR:
	case CAM_ATA_STATUS_ERROR:
#ifdef CAM_IO_STATS
		softc->errors++;
#endif
		break;
	default:
		break;
	}

	return(cam_periph_error(ccb, cam_flags, sense_flags));
}

/*
 * Step through all NDA peripheral drivers, and if the device is still open,
 * sync the disk cache to physical media.
 */
static void
ndaflush(void)
{
	struct cam_periph *periph;
	struct nda_softc *softc;
	union ccb *ccb;
	int error;

	CAM_PERIPH_FOREACH(periph, &ndadriver) {
		softc = (struct nda_softc *)periph->softc;

		if (SCHEDULER_STOPPED()) {
			/*
			 * If we paniced with the lock held or the periph is not
			 * open, do not recurse.  Otherwise, call ndadump since
			 * that avoids the sleeping cam_periph_getccb does if no
			 * CCBs are available.
			 */
			if (!cam_periph_owned(periph) &&
			    (softc->flags & NDA_FLAG_OPEN)) {
				ndadump(softc->disk, NULL, 0, 0, 0);
			}
			continue;
		}

		/*
		 * We only sync the cache if the drive is still open
		 */
		cam_periph_lock(periph);
		if ((softc->flags & NDA_FLAG_OPEN) == 0) {
			cam_periph_unlock(periph);
			continue;
		}

		ccb = cam_periph_getccb(periph, CAM_PRIORITY_NORMAL);
		nda_nvme_flush(softc, &ccb->nvmeio);
		error = cam_periph_runccb(ccb, ndaerror, /*cam_flags*/0,
		    /*sense_flags*/ SF_NO_RECOVERY | SF_NO_RETRY,
		    softc->disk->d_devstat);
		if (error != 0)
			xpt_print(periph->path, "Synchronize cache failed\n");
		xpt_release_ccb(ccb);
		cam_periph_unlock(periph);
	}
}

static void
ndashutdown(void *arg, int howto)
{

	ndaflush();
}

static void
ndasuspend(void *arg)
{

	ndaflush();
}
