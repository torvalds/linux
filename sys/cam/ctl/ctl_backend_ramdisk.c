/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003, 2008 Silicon Graphics International Corp.
 * Copyright (c) 2012 The FreeBSD Foundation
 * Copyright (c) 2014-2017 Alexander Motin <mav@FreeBSD.org>
 * All rights reserved.
 *
 * Portions of this software were developed by Edward Tomasz Napierala
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * $Id: //depot/users/kenm/FreeBSD-test2/sys/cam/ctl/ctl_backend_ramdisk.c#3 $
 */
/*
 * CAM Target Layer black hole and RAM disk backend.
 *
 * Author: Ken Merry <ken@FreeBSD.org>
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/condvar.h>
#include <sys/types.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/malloc.h>
#include <sys/sx.h>
#include <sys/taskqueue.h>
#include <sys/time.h>
#include <sys/queue.h>
#include <sys/conf.h>
#include <sys/ioccom.h>
#include <sys/module.h>
#include <sys/sysctl.h>
#include <sys/nv.h>
#include <sys/dnv.h>

#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_da.h>
#include <cam/ctl/ctl_io.h>
#include <cam/ctl/ctl.h>
#include <cam/ctl/ctl_util.h>
#include <cam/ctl/ctl_backend.h>
#include <cam/ctl/ctl_debug.h>
#include <cam/ctl/ctl_ioctl.h>
#include <cam/ctl/ctl_ha.h>
#include <cam/ctl/ctl_private.h>
#include <cam/ctl/ctl_error.h>

#define PRIV(io)	\
    ((struct ctl_ptr_len_flags *)&(io)->io_hdr.ctl_private[CTL_PRIV_BACKEND])
#define ARGS(io)	\
    ((struct ctl_lba_len_flags *)&(io)->io_hdr.ctl_private[CTL_PRIV_LBA_LEN])

#define	PPP	(PAGE_SIZE / sizeof(uint8_t **))
#ifdef __LP64__
#define	PPPS	(PAGE_SHIFT - 3)
#else
#define	PPPS	(PAGE_SHIFT - 2)
#endif
#define	SGPP	(PAGE_SIZE / sizeof(struct ctl_sg_entry))

#define	P_UNMAPPED	NULL			/* Page is unmapped. */
#define	P_ANCHORED	((void *)(uintptr_t)1)	/* Page is anchored. */

typedef enum {
	GP_READ,	/* Return data page or zero page. */
	GP_WRITE,	/* Return data page, try allocate if none. */
	GP_ANCHOR,	/* Return data page, try anchor if none. */
	GP_OTHER,	/* Return what present, do not allocate/anchor. */
} getpage_op_t;

typedef enum {
	CTL_BE_RAMDISK_LUN_UNCONFIGURED	= 0x01,
	CTL_BE_RAMDISK_LUN_CONFIG_ERR	= 0x02,
	CTL_BE_RAMDISK_LUN_WAITING	= 0x04
} ctl_be_ramdisk_lun_flags;

struct ctl_be_ramdisk_lun {
	struct ctl_lun_create_params params;
	char			lunname[32];
	int			indir;
	uint8_t			**pages;
	uint8_t			*zero_page;
	struct sx		page_lock;
	u_int			pblocksize;
	u_int			pblockmul;
	uint64_t		size_bytes;
	uint64_t		size_blocks;
	uint64_t		cap_bytes;
	uint64_t		cap_used;
	struct ctl_be_ramdisk_softc *softc;
	ctl_be_ramdisk_lun_flags flags;
	STAILQ_ENTRY(ctl_be_ramdisk_lun) links;
	struct ctl_be_lun	cbe_lun;
	struct taskqueue	*io_taskqueue;
	struct task		io_task;
	STAILQ_HEAD(, ctl_io_hdr) cont_queue;
	struct mtx_padalign	queue_lock;
};

struct ctl_be_ramdisk_softc {
	struct mtx lock;
	int num_luns;
	STAILQ_HEAD(, ctl_be_ramdisk_lun) lun_list;
};

static struct ctl_be_ramdisk_softc rd_softc;
extern struct ctl_softc *control_softc;

static int ctl_backend_ramdisk_init(void);
static int ctl_backend_ramdisk_shutdown(void);
static int ctl_backend_ramdisk_move_done(union ctl_io *io);
static void ctl_backend_ramdisk_compare(union ctl_io *io);
static void ctl_backend_ramdisk_rw(union ctl_io *io);
static int ctl_backend_ramdisk_submit(union ctl_io *io);
static void ctl_backend_ramdisk_worker(void *context, int pending);
static int ctl_backend_ramdisk_config_read(union ctl_io *io);
static int ctl_backend_ramdisk_config_write(union ctl_io *io);
static uint64_t ctl_backend_ramdisk_lun_attr(void *be_lun, const char *attrname);
static int ctl_backend_ramdisk_ioctl(struct cdev *dev, u_long cmd,
				     caddr_t addr, int flag, struct thread *td);
static int ctl_backend_ramdisk_rm(struct ctl_be_ramdisk_softc *softc,
				  struct ctl_lun_req *req);
static int ctl_backend_ramdisk_create(struct ctl_be_ramdisk_softc *softc,
				      struct ctl_lun_req *req);
static int ctl_backend_ramdisk_modify(struct ctl_be_ramdisk_softc *softc,
				  struct ctl_lun_req *req);
static void ctl_backend_ramdisk_lun_shutdown(void *be_lun);
static void ctl_backend_ramdisk_lun_config_status(void *be_lun,
						  ctl_lun_config_status status);

static struct ctl_backend_driver ctl_be_ramdisk_driver = 
{
	.name = "ramdisk",
	.flags = CTL_BE_FLAG_HAS_CONFIG,
	.init = ctl_backend_ramdisk_init,
	.shutdown = ctl_backend_ramdisk_shutdown,
	.data_submit = ctl_backend_ramdisk_submit,
	.data_move_done = ctl_backend_ramdisk_move_done,
	.config_read = ctl_backend_ramdisk_config_read,
	.config_write = ctl_backend_ramdisk_config_write,
	.ioctl = ctl_backend_ramdisk_ioctl,
	.lun_attr = ctl_backend_ramdisk_lun_attr,
};

MALLOC_DEFINE(M_RAMDISK, "ramdisk", "Memory used for CTL RAMdisk");
CTL_BACKEND_DECLARE(cbr, ctl_be_ramdisk_driver);

static int
ctl_backend_ramdisk_init(void)
{
	struct ctl_be_ramdisk_softc *softc = &rd_softc;

	memset(softc, 0, sizeof(*softc));
	mtx_init(&softc->lock, "ctlramdisk", NULL, MTX_DEF);
	STAILQ_INIT(&softc->lun_list);
	return (0);
}

static int
ctl_backend_ramdisk_shutdown(void)
{
	struct ctl_be_ramdisk_softc *softc = &rd_softc;
	struct ctl_be_ramdisk_lun *lun, *next_lun;

	mtx_lock(&softc->lock);
	STAILQ_FOREACH_SAFE(lun, &softc->lun_list, links, next_lun) {
		/*
		 * Drop our lock here.  Since ctl_invalidate_lun() can call
		 * back into us, this could potentially lead to a recursive
		 * lock of the same mutex, which would cause a hang.
		 */
		mtx_unlock(&softc->lock);
		ctl_disable_lun(&lun->cbe_lun);
		ctl_invalidate_lun(&lun->cbe_lun);
		mtx_lock(&softc->lock);
	}
	mtx_unlock(&softc->lock);
	mtx_destroy(&softc->lock);
	return (0);
}

static uint8_t *
ctl_backend_ramdisk_getpage(struct ctl_be_ramdisk_lun *be_lun, off_t pn,
    getpage_op_t op)
{
	uint8_t **p, ***pp;
	off_t i;
	int s;

	if (be_lun->cap_bytes == 0) {
		switch (op) {
		case GP_READ:
			return (be_lun->zero_page);
		case GP_WRITE:
			return ((uint8_t *)be_lun->pages);
		case GP_ANCHOR:
			return (P_ANCHORED);
		default:
			return (P_UNMAPPED);
		}
	}
	if (op == GP_WRITE || op == GP_ANCHOR) {
		sx_xlock(&be_lun->page_lock);
		pp = &be_lun->pages;
		for (s = (be_lun->indir - 1) * PPPS; s >= 0; s -= PPPS) {
			if (*pp == NULL) {
				*pp = malloc(PAGE_SIZE, M_RAMDISK,
				    M_WAITOK|M_ZERO);
			}
			i = pn >> s;
			pp = (uint8_t ***)&(*pp)[i];
			pn -= i << s;
		}
		if (*pp == P_UNMAPPED && be_lun->cap_used < be_lun->cap_bytes) {
			if (op == GP_WRITE) {
				*pp = malloc(be_lun->pblocksize, M_RAMDISK,
				    M_WAITOK|M_ZERO);
			} else
				*pp = P_ANCHORED;
			be_lun->cap_used += be_lun->pblocksize;
		} else if (*pp == P_ANCHORED && op == GP_WRITE) {
			*pp = malloc(be_lun->pblocksize, M_RAMDISK,
			    M_WAITOK|M_ZERO);
		}
		sx_xunlock(&be_lun->page_lock);
		return ((uint8_t *)*pp);
	} else {
		sx_slock(&be_lun->page_lock);
		p = be_lun->pages;
		for (s = (be_lun->indir - 1) * PPPS; s >= 0; s -= PPPS) {
			if (p == NULL)
				break;
			i = pn >> s;
			p = (uint8_t **)p[i];
			pn -= i << s;
		}
		sx_sunlock(&be_lun->page_lock);
		if ((p == P_UNMAPPED || p == P_ANCHORED) && op == GP_READ)
			return (be_lun->zero_page);
		return ((uint8_t *)p);
	}
};

static void
ctl_backend_ramdisk_unmappage(struct ctl_be_ramdisk_lun *be_lun, off_t pn)
{
	uint8_t ***pp;
	off_t i;
	int s;

	if (be_lun->cap_bytes == 0)
		return;
	sx_xlock(&be_lun->page_lock);
	pp = &be_lun->pages;
	for (s = (be_lun->indir - 1) * PPPS; s >= 0; s -= PPPS) {
		if (*pp == NULL)
			goto noindir;
		i = pn >> s;
		pp = (uint8_t ***)&(*pp)[i];
		pn -= i << s;
	}
	if (*pp == P_ANCHORED) {
		be_lun->cap_used -= be_lun->pblocksize;
		*pp = P_UNMAPPED;
	} else if (*pp != P_UNMAPPED) {
		free(*pp, M_RAMDISK);
		be_lun->cap_used -= be_lun->pblocksize;
		*pp = P_UNMAPPED;
	}
noindir:
	sx_xunlock(&be_lun->page_lock);
};

static void
ctl_backend_ramdisk_anchorpage(struct ctl_be_ramdisk_lun *be_lun, off_t pn)
{
	uint8_t ***pp;
	off_t i;
	int s;

	if (be_lun->cap_bytes == 0)
		return;
	sx_xlock(&be_lun->page_lock);
	pp = &be_lun->pages;
	for (s = (be_lun->indir - 1) * PPPS; s >= 0; s -= PPPS) {
		if (*pp == NULL)
			goto noindir;
		i = pn >> s;
		pp = (uint8_t ***)&(*pp)[i];
		pn -= i << s;
	}
	if (*pp == P_UNMAPPED && be_lun->cap_used < be_lun->cap_bytes) {
		be_lun->cap_used += be_lun->pblocksize;
		*pp = P_ANCHORED;
	} else if (*pp != P_ANCHORED) {
		free(*pp, M_RAMDISK);
		*pp = P_ANCHORED;
	}
noindir:
	sx_xunlock(&be_lun->page_lock);
};

static void
ctl_backend_ramdisk_freeallpages(uint8_t **p, int indir)
{
	int i;

	if (p == NULL)
		return;
	if (indir == 0) {
		free(p, M_RAMDISK);
		return;
	}
	for (i = 0; i < PPP; i++) {
		if (p[i] == NULL)
			continue;
		ctl_backend_ramdisk_freeallpages((uint8_t **)p[i], indir - 1);
	}
	free(p, M_RAMDISK);
};

static size_t
cmp(uint8_t *a, uint8_t *b, size_t size)
{
	size_t i;

	for (i = 0; i < size; i++) {
		if (a[i] != b[i])
			break;
	}
	return (i);
}

static int
ctl_backend_ramdisk_cmp(union ctl_io *io)
{
	struct ctl_be_lun *cbe_lun = CTL_BACKEND_LUN(io);
	struct ctl_be_ramdisk_lun *be_lun = cbe_lun->be_lun;
	uint8_t *page;
	uint8_t info[8];
	uint64_t lba;
	u_int lbaoff, lbas, res, off;

	lbas = io->scsiio.kern_data_len / cbe_lun->blocksize;
	lba = ARGS(io)->lba + PRIV(io)->len - lbas;
	off = 0;
	for (; lbas > 0; lbas--, lba++) {
		page = ctl_backend_ramdisk_getpage(be_lun,
		    lba >> cbe_lun->pblockexp, GP_READ);
		lbaoff = lba & ~(UINT_MAX << cbe_lun->pblockexp);
		page += lbaoff * cbe_lun->blocksize;
		res = cmp(io->scsiio.kern_data_ptr + off, page,
		    cbe_lun->blocksize);
		off += res;
		if (res < cbe_lun->blocksize)
			break;
	}
	if (lbas > 0) {
		off += io->scsiio.kern_rel_offset - io->scsiio.kern_data_len;
		scsi_u64to8b(off, info);
		ctl_set_sense(&io->scsiio, /*current_error*/ 1,
		    /*sense_key*/ SSD_KEY_MISCOMPARE,
		    /*asc*/ 0x1D, /*ascq*/ 0x00,
		    /*type*/ SSD_ELEM_INFO,
		    /*size*/ sizeof(info), /*data*/ &info,
		    /*type*/ SSD_ELEM_NONE);
		return (1);
	}
	return (0);
}

static int
ctl_backend_ramdisk_move_done(union ctl_io *io)
{
	struct ctl_be_lun *cbe_lun = CTL_BACKEND_LUN(io);
	struct ctl_be_ramdisk_lun *be_lun = cbe_lun->be_lun;
#ifdef CTL_TIME_IO
	struct bintime cur_bt;
#endif

	CTL_DEBUG_PRINT(("ctl_backend_ramdisk_move_done\n"));
#ifdef CTL_TIME_IO
	getbinuptime(&cur_bt);
	bintime_sub(&cur_bt, &io->io_hdr.dma_start_bt);
	bintime_add(&io->io_hdr.dma_bt, &cur_bt);
#endif
	io->io_hdr.num_dmas++;
	if (io->scsiio.kern_sg_entries > 0)
		free(io->scsiio.kern_data_ptr, M_RAMDISK);
	io->scsiio.kern_rel_offset += io->scsiio.kern_data_len;
	if (io->io_hdr.flags & CTL_FLAG_ABORT) {
		;
	} else if (io->io_hdr.port_status != 0 &&
	    ((io->io_hdr.status & CTL_STATUS_MASK) == CTL_STATUS_NONE ||
	     (io->io_hdr.status & CTL_STATUS_MASK) == CTL_SUCCESS)) {
		ctl_set_internal_failure(&io->scsiio, /*sks_valid*/ 1,
		    /*retry_count*/ io->io_hdr.port_status);
	} else if (io->scsiio.kern_data_resid != 0 &&
	    (io->io_hdr.flags & CTL_FLAG_DATA_MASK) == CTL_FLAG_DATA_OUT &&
	    ((io->io_hdr.status & CTL_STATUS_MASK) == CTL_STATUS_NONE ||
	     (io->io_hdr.status & CTL_STATUS_MASK) == CTL_SUCCESS)) {
		ctl_set_invalid_field_ciu(&io->scsiio);
	} else if ((io->io_hdr.port_status == 0) &&
	    ((io->io_hdr.status & CTL_STATUS_MASK) == CTL_STATUS_NONE)) {
		if (ARGS(io)->flags & CTL_LLF_COMPARE) {
			/* We have data block ready for comparison. */
			if (ctl_backend_ramdisk_cmp(io))
				goto done;
		}
		if (ARGS(io)->len > PRIV(io)->len) {
			mtx_lock(&be_lun->queue_lock);
			STAILQ_INSERT_TAIL(&be_lun->cont_queue,
			    &io->io_hdr, links);
			mtx_unlock(&be_lun->queue_lock);
			taskqueue_enqueue(be_lun->io_taskqueue,
			    &be_lun->io_task);
			return (0);
		}
		ctl_set_success(&io->scsiio);
	}
done:
	ctl_data_submit_done(io);
	return(0);
}

static void
ctl_backend_ramdisk_compare(union ctl_io *io)
{
	struct ctl_be_lun *cbe_lun = CTL_BACKEND_LUN(io);
	u_int lbas, len;

	lbas = ARGS(io)->len - PRIV(io)->len;
	lbas = MIN(lbas, 131072 / cbe_lun->blocksize);
	len = lbas * cbe_lun->blocksize;

	io->scsiio.be_move_done = ctl_backend_ramdisk_move_done;
	io->scsiio.kern_data_ptr = malloc(len, M_RAMDISK, M_WAITOK);
	io->scsiio.kern_data_len = len;
	io->scsiio.kern_sg_entries = 0;
	io->io_hdr.flags |= CTL_FLAG_ALLOCATED;
	PRIV(io)->len += lbas;
#ifdef CTL_TIME_IO
	getbinuptime(&io->io_hdr.dma_start_bt);
#endif
	ctl_datamove(io);
}

static void
ctl_backend_ramdisk_rw(union ctl_io *io)
{
	struct ctl_be_lun *cbe_lun = CTL_BACKEND_LUN(io);
	struct ctl_be_ramdisk_lun *be_lun = cbe_lun->be_lun;
	struct ctl_sg_entry *sg_entries;
	uint8_t *page;
	uint64_t lba;
	u_int i, len, lbaoff, lbas, sgs, off;
	getpage_op_t op;

	lba = ARGS(io)->lba + PRIV(io)->len;
	lbaoff = lba & ~(UINT_MAX << cbe_lun->pblockexp);
	lbas = ARGS(io)->len - PRIV(io)->len;
	lbas = MIN(lbas, (SGPP << cbe_lun->pblockexp) - lbaoff);
	sgs = (lbas + lbaoff + be_lun->pblockmul - 1) >> cbe_lun->pblockexp;
	off = lbaoff * cbe_lun->blocksize;
	op = (ARGS(io)->flags & CTL_LLF_WRITE) ? GP_WRITE : GP_READ;
	if (sgs > 1) {
		io->scsiio.kern_data_ptr = malloc(sizeof(struct ctl_sg_entry) *
		    sgs, M_RAMDISK, M_WAITOK);
		sg_entries = (struct ctl_sg_entry *)io->scsiio.kern_data_ptr;
		len = lbas * cbe_lun->blocksize;
		for (i = 0; i < sgs; i++) {
			page = ctl_backend_ramdisk_getpage(be_lun,
			    (lba >> cbe_lun->pblockexp) + i, op);
			if (page == P_UNMAPPED || page == P_ANCHORED) {
				free(io->scsiio.kern_data_ptr, M_RAMDISK);
nospc:
				ctl_set_space_alloc_fail(&io->scsiio);
				ctl_data_submit_done(io);
				return;
			}
			sg_entries[i].addr = page + off;
			sg_entries[i].len = MIN(len, be_lun->pblocksize - off);
			len -= sg_entries[i].len;
			off = 0;
		}
	} else {
		page = ctl_backend_ramdisk_getpage(be_lun,
		    lba >> cbe_lun->pblockexp, op);
		if (page == P_UNMAPPED || page == P_ANCHORED)
			goto nospc;
		sgs = 0;
		io->scsiio.kern_data_ptr = page + off;
	}

	io->scsiio.be_move_done = ctl_backend_ramdisk_move_done;
	io->scsiio.kern_data_len = lbas * cbe_lun->blocksize;
	io->scsiio.kern_sg_entries = sgs;
	io->io_hdr.flags |= CTL_FLAG_ALLOCATED;
	PRIV(io)->len += lbas;
	if ((ARGS(io)->flags & CTL_LLF_READ) &&
	    ARGS(io)->len <= PRIV(io)->len) {
		ctl_set_success(&io->scsiio);
		ctl_serseq_done(io);
	}
#ifdef CTL_TIME_IO
	getbinuptime(&io->io_hdr.dma_start_bt);
#endif
	ctl_datamove(io);
}

static int
ctl_backend_ramdisk_submit(union ctl_io *io)
{
	struct ctl_lba_len_flags *lbalen = ARGS(io);

	if (lbalen->flags & CTL_LLF_VERIFY) {
		ctl_set_success(&io->scsiio);
		ctl_data_submit_done(io);
		return (CTL_RETVAL_COMPLETE);
	}
	PRIV(io)->len = 0;
	if (lbalen->flags & CTL_LLF_COMPARE)
		ctl_backend_ramdisk_compare(io);
	else
		ctl_backend_ramdisk_rw(io);
	return (CTL_RETVAL_COMPLETE);
}

static void
ctl_backend_ramdisk_worker(void *context, int pending)
{
	struct ctl_be_ramdisk_lun *be_lun;
	union ctl_io *io;

	be_lun = (struct ctl_be_ramdisk_lun *)context;
	mtx_lock(&be_lun->queue_lock);
	for (;;) {
		io = (union ctl_io *)STAILQ_FIRST(&be_lun->cont_queue);
		if (io != NULL) {
			STAILQ_REMOVE(&be_lun->cont_queue, &io->io_hdr,
				      ctl_io_hdr, links);
			mtx_unlock(&be_lun->queue_lock);
			if (ARGS(io)->flags & CTL_LLF_COMPARE)
				ctl_backend_ramdisk_compare(io);
			else
				ctl_backend_ramdisk_rw(io);
			mtx_lock(&be_lun->queue_lock);
			continue;
		}

		/*
		 * If we get here, there is no work left in the queues, so
		 * just break out and let the task queue go to sleep.
		 */
		break;
	}
	mtx_unlock(&be_lun->queue_lock);
}

static int
ctl_backend_ramdisk_gls(union ctl_io *io)
{
	struct ctl_be_lun *cbe_lun = CTL_BACKEND_LUN(io);
	struct ctl_be_ramdisk_lun *be_lun = cbe_lun->be_lun;
	struct scsi_get_lba_status_data *data;
	uint8_t *page;
	u_int lbaoff;

	data = (struct scsi_get_lba_status_data *)io->scsiio.kern_data_ptr;
	scsi_u64to8b(ARGS(io)->lba, data->descr[0].addr);
	lbaoff = ARGS(io)->lba & ~(UINT_MAX << cbe_lun->pblockexp);
	scsi_ulto4b(be_lun->pblockmul - lbaoff, data->descr[0].length);
	page = ctl_backend_ramdisk_getpage(be_lun,
	    ARGS(io)->lba >> cbe_lun->pblockexp, GP_OTHER);
	if (page == P_UNMAPPED)
		data->descr[0].status = 1;
	else if (page == P_ANCHORED)
		data->descr[0].status = 2;
	else
		data->descr[0].status = 0;
	ctl_config_read_done(io);
	return (CTL_RETVAL_COMPLETE);
}

static int
ctl_backend_ramdisk_config_read(union ctl_io *io)
{
	int retval = 0;

	switch (io->scsiio.cdb[0]) {
	case SERVICE_ACTION_IN:
		if (io->scsiio.cdb[1] == SGLS_SERVICE_ACTION) {
			retval = ctl_backend_ramdisk_gls(io);
			break;
		}
		ctl_set_invalid_field(&io->scsiio,
				      /*sks_valid*/ 1,
				      /*command*/ 1,
				      /*field*/ 1,
				      /*bit_valid*/ 1,
				      /*bit*/ 4);
		ctl_config_read_done(io);
		retval = CTL_RETVAL_COMPLETE;
		break;
	default:
		ctl_set_invalid_opcode(&io->scsiio);
		ctl_config_read_done(io);
		retval = CTL_RETVAL_COMPLETE;
		break;
	}
	return (retval);
}

static void
ctl_backend_ramdisk_delete(struct ctl_be_lun *cbe_lun, off_t lba, off_t len,
    int anchor)
{
	struct ctl_be_ramdisk_lun *be_lun = cbe_lun->be_lun;
	uint8_t *page;
	uint64_t p, lp;
	u_int lbaoff;
	getpage_op_t op = anchor ? GP_ANCHOR : GP_OTHER;

	/* Partially zero first partial page. */
	p = lba >> cbe_lun->pblockexp;
	lbaoff = lba & ~(UINT_MAX << cbe_lun->pblockexp);
	if (lbaoff != 0) {
		page = ctl_backend_ramdisk_getpage(be_lun, p, op);
		if (page != P_UNMAPPED && page != P_ANCHORED) {
			memset(page + lbaoff * cbe_lun->blocksize, 0,
			    min(len, be_lun->pblockmul - lbaoff) *
			    cbe_lun->blocksize);
		}
		p++;
	}

	/* Partially zero last partial page. */
	lp = (lba + len) >> cbe_lun->pblockexp;
	lbaoff = (lba + len) & ~(UINT_MAX << cbe_lun->pblockexp);
	if (p <= lp && lbaoff != 0) {
		page = ctl_backend_ramdisk_getpage(be_lun, lp, op);
		if (page != P_UNMAPPED && page != P_ANCHORED)
			memset(page, 0, lbaoff * cbe_lun->blocksize);
	}

	/* Delete remaining full pages. */
	if (anchor) {
		for (; p < lp; p++)
			ctl_backend_ramdisk_anchorpage(be_lun, p);
	} else {
		for (; p < lp; p++)
			ctl_backend_ramdisk_unmappage(be_lun, p);
	}
}

static void
ctl_backend_ramdisk_ws(union ctl_io *io)
{
	struct ctl_be_lun *cbe_lun = CTL_BACKEND_LUN(io);
	struct ctl_be_ramdisk_lun *be_lun = cbe_lun->be_lun;
	struct ctl_lba_len_flags *lbalen = ARGS(io);
	uint8_t *page;
	uint64_t lba;
	u_int lbaoff, lbas;

	if (lbalen->flags & ~(SWS_LBDATA | SWS_UNMAP | SWS_ANCHOR | SWS_NDOB)) {
		ctl_set_invalid_field(&io->scsiio,
				      /*sks_valid*/ 1,
				      /*command*/ 1,
				      /*field*/ 1,
				      /*bit_valid*/ 0,
				      /*bit*/ 0);
		ctl_config_write_done(io);
		return;
	}
	if (lbalen->flags & SWS_UNMAP) {
		ctl_backend_ramdisk_delete(cbe_lun, lbalen->lba, lbalen->len,
		    (lbalen->flags & SWS_ANCHOR) != 0);
		ctl_set_success(&io->scsiio);
		ctl_config_write_done(io);
		return;
	}

	for (lba = lbalen->lba, lbas = lbalen->len; lbas > 0; lba++, lbas--) {
		page = ctl_backend_ramdisk_getpage(be_lun,
		    lba >> cbe_lun->pblockexp, GP_WRITE);
		if (page == P_UNMAPPED || page == P_ANCHORED) {
			ctl_set_space_alloc_fail(&io->scsiio);
			ctl_data_submit_done(io);
			return;
		}
		lbaoff = lba & ~(UINT_MAX << cbe_lun->pblockexp);
		page += lbaoff * cbe_lun->blocksize;
		if (lbalen->flags & SWS_NDOB) {
			memset(page, 0, cbe_lun->blocksize);
		} else {
			memcpy(page, io->scsiio.kern_data_ptr,
			    cbe_lun->blocksize);
		}
		if (lbalen->flags & SWS_LBDATA)
			scsi_ulto4b(lba, page);
	}
	ctl_set_success(&io->scsiio);
	ctl_config_write_done(io);
}

static void
ctl_backend_ramdisk_unmap(union ctl_io *io)
{
	struct ctl_be_lun *cbe_lun = CTL_BACKEND_LUN(io);
	struct ctl_ptr_len_flags *ptrlen = (struct ctl_ptr_len_flags *)ARGS(io);
	struct scsi_unmap_desc *buf, *end;

	if ((ptrlen->flags & ~SU_ANCHOR) != 0) {
		ctl_set_invalid_field(&io->scsiio,
				      /*sks_valid*/ 0,
				      /*command*/ 0,
				      /*field*/ 0,
				      /*bit_valid*/ 0,
				      /*bit*/ 0);
		ctl_config_write_done(io);
		return;
	}

	buf = (struct scsi_unmap_desc *)ptrlen->ptr;
	end = buf + ptrlen->len / sizeof(*buf);
	for (; buf < end; buf++) {
		ctl_backend_ramdisk_delete(cbe_lun,
		    scsi_8btou64(buf->lba), scsi_4btoul(buf->length),
		    (ptrlen->flags & SU_ANCHOR) != 0);
	}

	ctl_set_success(&io->scsiio);
	ctl_config_write_done(io);
}

static int
ctl_backend_ramdisk_config_write(union ctl_io *io)
{
	struct ctl_be_lun *cbe_lun = CTL_BACKEND_LUN(io);
	int retval = 0;

	switch (io->scsiio.cdb[0]) {
	case SYNCHRONIZE_CACHE:
	case SYNCHRONIZE_CACHE_16:
		/* We have no cache to flush. */
		ctl_set_success(&io->scsiio);
		ctl_config_write_done(io);
		break;
	case START_STOP_UNIT: {
		struct scsi_start_stop_unit *cdb;

		cdb = (struct scsi_start_stop_unit *)io->scsiio.cdb;
		if ((cdb->how & SSS_PC_MASK) != 0) {
			ctl_set_success(&io->scsiio);
			ctl_config_write_done(io);
			break;
		}
		if (cdb->how & SSS_START) {
			if (cdb->how & SSS_LOEJ)
				ctl_lun_has_media(cbe_lun);
			ctl_start_lun(cbe_lun);
		} else {
			ctl_stop_lun(cbe_lun);
			if (cdb->how & SSS_LOEJ)
				ctl_lun_ejected(cbe_lun);
		}
		ctl_set_success(&io->scsiio);
		ctl_config_write_done(io);
		break;
	}
	case PREVENT_ALLOW:
		ctl_set_success(&io->scsiio);
		ctl_config_write_done(io);
		break;
	case WRITE_SAME_10:
	case WRITE_SAME_16:
		ctl_backend_ramdisk_ws(io);
		break;
	case UNMAP:
		ctl_backend_ramdisk_unmap(io);
		break;
	default:
		ctl_set_invalid_opcode(&io->scsiio);
		ctl_config_write_done(io);
		retval = CTL_RETVAL_COMPLETE;
		break;
	}

	return (retval);
}

static uint64_t
ctl_backend_ramdisk_lun_attr(void *arg, const char *attrname)
{
	struct ctl_be_ramdisk_lun *be_lun = arg;
	uint64_t		val;

	val = UINT64_MAX;
	if (be_lun->cap_bytes == 0)
		return (val);
	sx_slock(&be_lun->page_lock);
	if (strcmp(attrname, "blocksused") == 0) {
		val = be_lun->cap_used / be_lun->cbe_lun.blocksize;
	} else if (strcmp(attrname, "blocksavail") == 0) {
		val = (be_lun->cap_bytes - be_lun->cap_used) /
		    be_lun->cbe_lun.blocksize;
	}
	sx_sunlock(&be_lun->page_lock);
	return (val);
}

static int
ctl_backend_ramdisk_ioctl(struct cdev *dev, u_long cmd, caddr_t addr,
			  int flag, struct thread *td)
{
	struct ctl_be_ramdisk_softc *softc = &rd_softc;
	struct ctl_lun_req *lun_req;
	int retval;

	retval = 0;
	switch (cmd) {
	case CTL_LUN_REQ:
		lun_req = (struct ctl_lun_req *)addr;
		switch (lun_req->reqtype) {
		case CTL_LUNREQ_CREATE:
			retval = ctl_backend_ramdisk_create(softc, lun_req);
			break;
		case CTL_LUNREQ_RM:
			retval = ctl_backend_ramdisk_rm(softc, lun_req);
			break;
		case CTL_LUNREQ_MODIFY:
			retval = ctl_backend_ramdisk_modify(softc, lun_req);
			break;
		default:
			lun_req->status = CTL_LUN_ERROR;
			snprintf(lun_req->error_str, sizeof(lun_req->error_str),
				 "%s: invalid LUN request type %d", __func__,
				 lun_req->reqtype);
			break;
		}
		break;
	default:
		retval = ENOTTY;
		break;
	}

	return (retval);
}

static int
ctl_backend_ramdisk_rm(struct ctl_be_ramdisk_softc *softc,
		       struct ctl_lun_req *req)
{
	struct ctl_be_ramdisk_lun *be_lun;
	struct ctl_lun_rm_params *params;
	int retval;

	params = &req->reqdata.rm;
	mtx_lock(&softc->lock);
	STAILQ_FOREACH(be_lun, &softc->lun_list, links) {
		if (be_lun->cbe_lun.lun_id == params->lun_id)
			break;
	}
	mtx_unlock(&softc->lock);
	if (be_lun == NULL) {
		snprintf(req->error_str, sizeof(req->error_str),
			 "%s: LUN %u is not managed by the ramdisk backend",
			 __func__, params->lun_id);
		goto bailout_error;
	}

	retval = ctl_disable_lun(&be_lun->cbe_lun);
	if (retval != 0) {
		snprintf(req->error_str, sizeof(req->error_str),
			 "%s: error %d returned from ctl_disable_lun() for "
			 "LUN %d", __func__, retval, params->lun_id);
		goto bailout_error;
	}

	/*
	 * Set the waiting flag before we invalidate the LUN.  Our shutdown
	 * routine can be called any time after we invalidate the LUN,
	 * and can be called from our context.
	 *
	 * This tells the shutdown routine that we're waiting, or we're
	 * going to wait for the shutdown to happen.
	 */
	mtx_lock(&softc->lock);
	be_lun->flags |= CTL_BE_RAMDISK_LUN_WAITING;
	mtx_unlock(&softc->lock);

	retval = ctl_invalidate_lun(&be_lun->cbe_lun);
	if (retval != 0) {
		snprintf(req->error_str, sizeof(req->error_str),
			 "%s: error %d returned from ctl_invalidate_lun() for "
			 "LUN %d", __func__, retval, params->lun_id);
		mtx_lock(&softc->lock);
		be_lun->flags &= ~CTL_BE_RAMDISK_LUN_WAITING;
		mtx_unlock(&softc->lock);
		goto bailout_error;
	}

	mtx_lock(&softc->lock);
	while ((be_lun->flags & CTL_BE_RAMDISK_LUN_UNCONFIGURED) == 0) {
		retval = msleep(be_lun, &softc->lock, PCATCH, "ctlram", 0);
		if (retval == EINTR)
			break;
	}
	be_lun->flags &= ~CTL_BE_RAMDISK_LUN_WAITING;

	/*
	 * We only remove this LUN from the list and free it (below) if
	 * retval == 0.  If the user interrupted the wait, we just bail out
	 * without actually freeing the LUN.  We let the shutdown routine
	 * free the LUN if that happens.
	 */
	if (retval == 0) {
		STAILQ_REMOVE(&softc->lun_list, be_lun, ctl_be_ramdisk_lun,
			      links);
		softc->num_luns--;
	}

	mtx_unlock(&softc->lock);

	if (retval == 0) {
		taskqueue_drain_all(be_lun->io_taskqueue);
		taskqueue_free(be_lun->io_taskqueue);
		nvlist_destroy(be_lun->cbe_lun.options);
		free(be_lun->zero_page, M_RAMDISK);
		ctl_backend_ramdisk_freeallpages(be_lun->pages, be_lun->indir);
		sx_destroy(&be_lun->page_lock);
		mtx_destroy(&be_lun->queue_lock);
		free(be_lun, M_RAMDISK);
	}

	req->status = CTL_LUN_OK;
	return (retval);

bailout_error:
	req->status = CTL_LUN_ERROR;
	return (0);
}

static int
ctl_backend_ramdisk_create(struct ctl_be_ramdisk_softc *softc,
			   struct ctl_lun_req *req)
{
	struct ctl_be_ramdisk_lun *be_lun;
	struct ctl_be_lun *cbe_lun;
	struct ctl_lun_create_params *params;
	const char *value;
	char tmpstr[32];
	uint64_t t;
	int retval;

	retval = 0;
	params = &req->reqdata.create;

	be_lun = malloc(sizeof(*be_lun), M_RAMDISK, M_ZERO | M_WAITOK);
	cbe_lun = &be_lun->cbe_lun;
	cbe_lun->be_lun = be_lun;
	cbe_lun->options = nvlist_clone(req->args_nvl);
	be_lun->params = req->reqdata.create;
	be_lun->softc = softc;
	sprintf(be_lun->lunname, "cram%d", softc->num_luns);

	if (params->flags & CTL_LUN_FLAG_DEV_TYPE)
		cbe_lun->lun_type = params->device_type;
	else
		cbe_lun->lun_type = T_DIRECT;
	be_lun->flags = CTL_BE_RAMDISK_LUN_UNCONFIGURED;
	cbe_lun->flags = 0;
	value = dnvlist_get_string(cbe_lun->options, "ha_role", NULL);
	if (value != NULL) {
		if (strcmp(value, "primary") == 0)
			cbe_lun->flags |= CTL_LUN_FLAG_PRIMARY;
	} else if (control_softc->flags & CTL_FLAG_ACTIVE_SHELF)
		cbe_lun->flags |= CTL_LUN_FLAG_PRIMARY;

	be_lun->pblocksize = PAGE_SIZE;
	value = dnvlist_get_string(cbe_lun->options, "pblocksize", NULL);
	if (value != NULL) {
		ctl_expand_number(value, &t);
		be_lun->pblocksize = t;
	}
	if (be_lun->pblocksize < 512 || be_lun->pblocksize > 131072) {
		snprintf(req->error_str, sizeof(req->error_str),
			 "%s: unsupported pblocksize %u", __func__,
			 be_lun->pblocksize);
		goto bailout_error;
	}

	if (cbe_lun->lun_type == T_DIRECT ||
	    cbe_lun->lun_type == T_CDROM) {
		if (params->blocksize_bytes != 0)
			cbe_lun->blocksize = params->blocksize_bytes;
		else if (cbe_lun->lun_type == T_CDROM)
			cbe_lun->blocksize = 2048;
		else
			cbe_lun->blocksize = 512;
		be_lun->pblockmul = be_lun->pblocksize / cbe_lun->blocksize;
		if (be_lun->pblockmul < 1 || !powerof2(be_lun->pblockmul)) {
			snprintf(req->error_str, sizeof(req->error_str),
				 "%s: pblocksize %u not exp2 of blocksize %u",
				 __func__,
				 be_lun->pblocksize, cbe_lun->blocksize);
			goto bailout_error;
		}
		if (params->lun_size_bytes < cbe_lun->blocksize) {
			snprintf(req->error_str, sizeof(req->error_str),
				 "%s: LUN size %ju < blocksize %u", __func__,
				 params->lun_size_bytes, cbe_lun->blocksize);
			goto bailout_error;
		}
		be_lun->size_blocks = params->lun_size_bytes / cbe_lun->blocksize;
		be_lun->size_bytes = be_lun->size_blocks * cbe_lun->blocksize;
		be_lun->indir = 0;
		t = be_lun->size_bytes / be_lun->pblocksize;
		while (t > 1) {
			t /= PPP;
			be_lun->indir++;
		}
		cbe_lun->maxlba = be_lun->size_blocks - 1;
		cbe_lun->pblockexp = fls(be_lun->pblockmul) - 1;
		cbe_lun->pblockoff = 0;
		cbe_lun->ublockexp = cbe_lun->pblockexp;
		cbe_lun->ublockoff = 0;
		cbe_lun->atomicblock = be_lun->pblocksize;
		cbe_lun->opttxferlen = SGPP * be_lun->pblocksize;
		value = dnvlist_get_string(cbe_lun->options, "capacity", NULL);
		if (value != NULL)
			ctl_expand_number(value, &be_lun->cap_bytes);
	} else {
		be_lun->pblockmul = 1;
		cbe_lun->pblockexp = 0;
	}

	/* Tell the user the blocksize we ended up using */
	params->blocksize_bytes = cbe_lun->blocksize;
	params->lun_size_bytes = be_lun->size_bytes;

	value = dnvlist_get_string(cbe_lun->options, "unmap", NULL);
	if (value != NULL && strcmp(value, "off") != 0)
		cbe_lun->flags |= CTL_LUN_FLAG_UNMAP;
	value = dnvlist_get_string(cbe_lun->options, "readonly", NULL);
	if (value != NULL) {
		if (strcmp(value, "on") == 0)
			cbe_lun->flags |= CTL_LUN_FLAG_READONLY;
	} else if (cbe_lun->lun_type != T_DIRECT)
		cbe_lun->flags |= CTL_LUN_FLAG_READONLY;
	cbe_lun->serseq = CTL_LUN_SERSEQ_OFF;
	value = dnvlist_get_string(cbe_lun->options, "serseq", NULL);
	if (value != NULL && strcmp(value, "on") == 0)
		cbe_lun->serseq = CTL_LUN_SERSEQ_ON;
	else if (value != NULL && strcmp(value, "read") == 0)
		cbe_lun->serseq = CTL_LUN_SERSEQ_READ;
	else if (value != NULL && strcmp(value, "off") == 0)
		cbe_lun->serseq = CTL_LUN_SERSEQ_OFF;

	if (params->flags & CTL_LUN_FLAG_ID_REQ) {
		cbe_lun->req_lun_id = params->req_lun_id;
		cbe_lun->flags |= CTL_LUN_FLAG_ID_REQ;
	} else
		cbe_lun->req_lun_id = 0;

	cbe_lun->lun_shutdown = ctl_backend_ramdisk_lun_shutdown;
	cbe_lun->lun_config_status = ctl_backend_ramdisk_lun_config_status;
	cbe_lun->be = &ctl_be_ramdisk_driver;
	if ((params->flags & CTL_LUN_FLAG_SERIAL_NUM) == 0) {
		snprintf(tmpstr, sizeof(tmpstr), "MYSERIAL%04d",
			 softc->num_luns);
		strncpy((char *)cbe_lun->serial_num, tmpstr,
			MIN(sizeof(cbe_lun->serial_num), sizeof(tmpstr)));

		/* Tell the user what we used for a serial number */
		strncpy((char *)params->serial_num, tmpstr,
			MIN(sizeof(params->serial_num), sizeof(tmpstr)));
	} else { 
		strncpy((char *)cbe_lun->serial_num, params->serial_num,
			MIN(sizeof(cbe_lun->serial_num),
			    sizeof(params->serial_num)));
	}
	if ((params->flags & CTL_LUN_FLAG_DEVID) == 0) {
		snprintf(tmpstr, sizeof(tmpstr), "MYDEVID%04d", softc->num_luns);
		strncpy((char *)cbe_lun->device_id, tmpstr,
			MIN(sizeof(cbe_lun->device_id), sizeof(tmpstr)));

		/* Tell the user what we used for a device ID */
		strncpy((char *)params->device_id, tmpstr,
			MIN(sizeof(params->device_id), sizeof(tmpstr)));
	} else {
		strncpy((char *)cbe_lun->device_id, params->device_id,
			MIN(sizeof(cbe_lun->device_id),
			    sizeof(params->device_id)));
	}

	STAILQ_INIT(&be_lun->cont_queue);
	sx_init(&be_lun->page_lock, "cram page lock");
	if (be_lun->cap_bytes == 0) {
		be_lun->indir = 0;
		be_lun->pages = malloc(be_lun->pblocksize, M_RAMDISK, M_WAITOK);
	}
	be_lun->zero_page = malloc(be_lun->pblocksize, M_RAMDISK,
	    M_WAITOK|M_ZERO);
	mtx_init(&be_lun->queue_lock, "cram queue lock", NULL, MTX_DEF);
	TASK_INIT(&be_lun->io_task, /*priority*/0, ctl_backend_ramdisk_worker,
	    be_lun);

	be_lun->io_taskqueue = taskqueue_create(be_lun->lunname, M_WAITOK,
	    taskqueue_thread_enqueue, /*context*/&be_lun->io_taskqueue);
	if (be_lun->io_taskqueue == NULL) {
		snprintf(req->error_str, sizeof(req->error_str),
			 "%s: Unable to create taskqueue", __func__);
		goto bailout_error;
	}

	retval = taskqueue_start_threads(&be_lun->io_taskqueue,
					 /*num threads*/1,
					 /*priority*/PUSER,
					 /*thread name*/
					 "%s taskq", be_lun->lunname);
	if (retval != 0)
		goto bailout_error;

	mtx_lock(&softc->lock);
	softc->num_luns++;
	STAILQ_INSERT_TAIL(&softc->lun_list, be_lun, links);
	mtx_unlock(&softc->lock);

	retval = ctl_add_lun(&be_lun->cbe_lun);
	if (retval != 0) {
		mtx_lock(&softc->lock);
		STAILQ_REMOVE(&softc->lun_list, be_lun, ctl_be_ramdisk_lun,
			      links);
		softc->num_luns--;
		mtx_unlock(&softc->lock);
		snprintf(req->error_str, sizeof(req->error_str),
			 "%s: ctl_add_lun() returned error %d, see dmesg for "
			"details", __func__, retval);
		retval = 0;
		goto bailout_error;
	}

	mtx_lock(&softc->lock);

	/*
	 * Tell the config_status routine that we're waiting so it won't
	 * clean up the LUN in the event of an error.
	 */
	be_lun->flags |= CTL_BE_RAMDISK_LUN_WAITING;

	while (be_lun->flags & CTL_BE_RAMDISK_LUN_UNCONFIGURED) {
		retval = msleep(be_lun, &softc->lock, PCATCH, "ctlram", 0);
		if (retval == EINTR)
			break;
	}
	be_lun->flags &= ~CTL_BE_RAMDISK_LUN_WAITING;

	if (be_lun->flags & CTL_BE_RAMDISK_LUN_CONFIG_ERR) {
		snprintf(req->error_str, sizeof(req->error_str),
			 "%s: LUN configuration error, see dmesg for details",
			 __func__);
		STAILQ_REMOVE(&softc->lun_list, be_lun, ctl_be_ramdisk_lun,
			      links);
		softc->num_luns--;
		mtx_unlock(&softc->lock);
		goto bailout_error;
	} else {
		params->req_lun_id = cbe_lun->lun_id;
	}
	mtx_unlock(&softc->lock);

	req->status = CTL_LUN_OK;
	return (retval);

bailout_error:
	req->status = CTL_LUN_ERROR;
	if (be_lun != NULL) {
		if (be_lun->io_taskqueue != NULL)
			taskqueue_free(be_lun->io_taskqueue);
		nvlist_destroy(cbe_lun->options);
		free(be_lun->zero_page, M_RAMDISK);
		ctl_backend_ramdisk_freeallpages(be_lun->pages, be_lun->indir);
		sx_destroy(&be_lun->page_lock);
		mtx_destroy(&be_lun->queue_lock);
		free(be_lun, M_RAMDISK);
	}
	return (retval);
}

static int
ctl_backend_ramdisk_modify(struct ctl_be_ramdisk_softc *softc,
		       struct ctl_lun_req *req)
{
	struct ctl_be_ramdisk_lun *be_lun;
	struct ctl_be_lun *cbe_lun;
	struct ctl_lun_modify_params *params;
	const char *value;
	uint32_t blocksize;
	int wasprim;

	params = &req->reqdata.modify;

	mtx_lock(&softc->lock);
	STAILQ_FOREACH(be_lun, &softc->lun_list, links) {
		if (be_lun->cbe_lun.lun_id == params->lun_id)
			break;
	}
	mtx_unlock(&softc->lock);
	if (be_lun == NULL) {
		snprintf(req->error_str, sizeof(req->error_str),
			 "%s: LUN %u is not managed by the ramdisk backend",
			 __func__, params->lun_id);
		goto bailout_error;
	}
	cbe_lun = &be_lun->cbe_lun;

	if (params->lun_size_bytes != 0)
		be_lun->params.lun_size_bytes = params->lun_size_bytes;

	nvlist_destroy(cbe_lun->options);
	cbe_lun->options = nvlist_clone(req->args_nvl);

	wasprim = (cbe_lun->flags & CTL_LUN_FLAG_PRIMARY);
	value = dnvlist_get_string(cbe_lun->options, "ha_role", NULL);
	if (value != NULL) {
		if (strcmp(value, "primary") == 0)
			cbe_lun->flags |= CTL_LUN_FLAG_PRIMARY;
		else
			cbe_lun->flags &= ~CTL_LUN_FLAG_PRIMARY;
	} else if (control_softc->flags & CTL_FLAG_ACTIVE_SHELF)
		cbe_lun->flags |= CTL_LUN_FLAG_PRIMARY;
	else
		cbe_lun->flags &= ~CTL_LUN_FLAG_PRIMARY;
	if (wasprim != (cbe_lun->flags & CTL_LUN_FLAG_PRIMARY)) {
		if (cbe_lun->flags & CTL_LUN_FLAG_PRIMARY)
			ctl_lun_primary(cbe_lun);
		else
			ctl_lun_secondary(cbe_lun);
	}

	blocksize = be_lun->cbe_lun.blocksize;
	if (be_lun->params.lun_size_bytes < blocksize) {
		snprintf(req->error_str, sizeof(req->error_str),
			"%s: LUN size %ju < blocksize %u", __func__,
			be_lun->params.lun_size_bytes, blocksize);
		goto bailout_error;
	}
	be_lun->size_blocks = be_lun->params.lun_size_bytes / blocksize;
	be_lun->size_bytes = be_lun->size_blocks * blocksize;
	be_lun->cbe_lun.maxlba = be_lun->size_blocks - 1;
	ctl_lun_capacity_changed(&be_lun->cbe_lun);

	/* Tell the user the exact size we ended up using */
	params->lun_size_bytes = be_lun->size_bytes;

	req->status = CTL_LUN_OK;
	return (0);

bailout_error:
	req->status = CTL_LUN_ERROR;
	return (0);
}

static void
ctl_backend_ramdisk_lun_shutdown(void *be_lun)
{
	struct ctl_be_ramdisk_lun *lun = be_lun;
	struct ctl_be_ramdisk_softc *softc = lun->softc;

	mtx_lock(&softc->lock);
	lun->flags |= CTL_BE_RAMDISK_LUN_UNCONFIGURED;
	if (lun->flags & CTL_BE_RAMDISK_LUN_WAITING) {
		wakeup(lun);
	} else {
		STAILQ_REMOVE(&softc->lun_list, lun, ctl_be_ramdisk_lun,
			      links);
		softc->num_luns--;
		free(be_lun, M_RAMDISK);
	}
	mtx_unlock(&softc->lock);
}

static void
ctl_backend_ramdisk_lun_config_status(void *be_lun,
				      ctl_lun_config_status status)
{
	struct ctl_be_ramdisk_lun *lun;
	struct ctl_be_ramdisk_softc *softc;

	lun = (struct ctl_be_ramdisk_lun *)be_lun;
	softc = lun->softc;

	if (status == CTL_LUN_CONFIG_OK) {
		mtx_lock(&softc->lock);
		lun->flags &= ~CTL_BE_RAMDISK_LUN_UNCONFIGURED;
		if (lun->flags & CTL_BE_RAMDISK_LUN_WAITING)
			wakeup(lun);
		mtx_unlock(&softc->lock);

		/*
		 * We successfully added the LUN, attempt to enable it.
		 */
		if (ctl_enable_lun(&lun->cbe_lun) != 0) {
			printf("%s: ctl_enable_lun() failed!\n", __func__);
			if (ctl_invalidate_lun(&lun->cbe_lun) != 0) {
				printf("%s: ctl_invalidate_lun() failed!\n",
				       __func__);
			}
		}

		return;
	}


	mtx_lock(&softc->lock);
	lun->flags &= ~CTL_BE_RAMDISK_LUN_UNCONFIGURED;

	/*
	 * If we have a user waiting, let him handle the cleanup.  If not,
	 * clean things up here.
	 */
	if (lun->flags & CTL_BE_RAMDISK_LUN_WAITING) {
		lun->flags |= CTL_BE_RAMDISK_LUN_CONFIG_ERR;
		wakeup(lun);
	} else {
		STAILQ_REMOVE(&softc->lun_list, lun, ctl_be_ramdisk_lun,
			      links);
		softc->num_luns--;
		free(lun, M_RAMDISK);
	}
	mtx_unlock(&softc->lock);
}
