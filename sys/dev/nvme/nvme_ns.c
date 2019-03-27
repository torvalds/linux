/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2012-2013 Intel Corporation
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bio.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/disk.h>
#include <sys/fcntl.h>
#include <sys/ioccom.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/systm.h>

#include <dev/pci/pcivar.h>

#include <geom/geom.h>

#include "nvme_private.h"

static void		nvme_bio_child_inbed(struct bio *parent, int bio_error);
static void		nvme_bio_child_done(void *arg,
					    const struct nvme_completion *cpl);
static uint32_t		nvme_get_num_segments(uint64_t addr, uint64_t size,
					      uint32_t alignment);
static void		nvme_free_child_bios(int num_bios,
					     struct bio **child_bios);
static struct bio **	nvme_allocate_child_bios(int num_bios);
static struct bio **	nvme_construct_child_bios(struct bio *bp,
						  uint32_t alignment,
						  int *num_bios);
static int		nvme_ns_split_bio(struct nvme_namespace *ns,
					  struct bio *bp,
					  uint32_t alignment);

static int
nvme_ns_ioctl(struct cdev *cdev, u_long cmd, caddr_t arg, int flag,
    struct thread *td)
{
	struct nvme_namespace			*ns;
	struct nvme_controller			*ctrlr;
	struct nvme_pt_command			*pt;

	ns = cdev->si_drv1;
	ctrlr = ns->ctrlr;

	switch (cmd) {
	case NVME_IO_TEST:
	case NVME_BIO_TEST:
		nvme_ns_test(ns, cmd, arg);
		break;
	case NVME_PASSTHROUGH_CMD:
		pt = (struct nvme_pt_command *)arg;
		return (nvme_ctrlr_passthrough_cmd(ctrlr, pt, ns->id, 
		    1 /* is_user_buffer */, 0 /* is_admin_cmd */));
	case DIOCGMEDIASIZE:
		*(off_t *)arg = (off_t)nvme_ns_get_size(ns);
		break;
	case DIOCGSECTORSIZE:
		*(u_int *)arg = nvme_ns_get_sector_size(ns);
		break;
	default:
		return (ENOTTY);
	}

	return (0);
}

static int
nvme_ns_open(struct cdev *dev __unused, int flags, int fmt __unused,
    struct thread *td)
{
	int error = 0;

	if (flags & FWRITE)
		error = securelevel_gt(td->td_ucred, 0);

	return (error);
}

static int
nvme_ns_close(struct cdev *dev __unused, int flags, int fmt __unused,
    struct thread *td)
{

	return (0);
}

static void
nvme_ns_strategy_done(void *arg, const struct nvme_completion *cpl)
{
	struct bio *bp = arg;

	/*
	 * TODO: add more extensive translation of NVMe status codes
	 *  to different bio error codes (i.e. EIO, EINVAL, etc.)
	 */
	if (nvme_completion_is_error(cpl)) {
		bp->bio_error = EIO;
		bp->bio_flags |= BIO_ERROR;
		bp->bio_resid = bp->bio_bcount;
	} else
		bp->bio_resid = 0;

	biodone(bp);
}

static void
nvme_ns_strategy(struct bio *bp)
{
	struct nvme_namespace	*ns;
	int			err;

	ns = bp->bio_dev->si_drv1;
	err = nvme_ns_bio_process(ns, bp, nvme_ns_strategy_done);

	if (err) {
		bp->bio_error = err;
		bp->bio_flags |= BIO_ERROR;
		bp->bio_resid = bp->bio_bcount;
		biodone(bp);
	}

}

static struct cdevsw nvme_ns_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	D_DISK,
	.d_read =	physread,
	.d_write =	physwrite,
	.d_open =	nvme_ns_open,
	.d_close =	nvme_ns_close,
	.d_strategy =	nvme_ns_strategy,
	.d_ioctl =	nvme_ns_ioctl
};

uint32_t
nvme_ns_get_max_io_xfer_size(struct nvme_namespace *ns)
{
	return ns->ctrlr->max_xfer_size;
}

uint32_t
nvme_ns_get_sector_size(struct nvme_namespace *ns)
{
	uint8_t flbas_fmt, lbads;

	flbas_fmt = (ns->data.flbas >> NVME_NS_DATA_FLBAS_FORMAT_SHIFT) &
		NVME_NS_DATA_FLBAS_FORMAT_MASK;
	lbads = (ns->data.lbaf[flbas_fmt] >> NVME_NS_DATA_LBAF_LBADS_SHIFT) &
		NVME_NS_DATA_LBAF_LBADS_MASK;

	return (1 << lbads);
}

uint64_t
nvme_ns_get_num_sectors(struct nvme_namespace *ns)
{
	return (ns->data.nsze);
}

uint64_t
nvme_ns_get_size(struct nvme_namespace *ns)
{
	return (nvme_ns_get_num_sectors(ns) * nvme_ns_get_sector_size(ns));
}

uint32_t
nvme_ns_get_flags(struct nvme_namespace *ns)
{
	return (ns->flags);
}

const char *
nvme_ns_get_serial_number(struct nvme_namespace *ns)
{
	return ((const char *)ns->ctrlr->cdata.sn);
}

const char *
nvme_ns_get_model_number(struct nvme_namespace *ns)
{
	return ((const char *)ns->ctrlr->cdata.mn);
}

const struct nvme_namespace_data *
nvme_ns_get_data(struct nvme_namespace *ns)
{

	return (&ns->data);
}

uint32_t
nvme_ns_get_stripesize(struct nvme_namespace *ns)
{

	return (ns->stripesize);
}

static void
nvme_ns_bio_done(void *arg, const struct nvme_completion *status)
{
	struct bio	*bp = arg;
	nvme_cb_fn_t	bp_cb_fn;

	bp_cb_fn = bp->bio_driver1;

	if (bp->bio_driver2)
		free(bp->bio_driver2, M_NVME);

	if (nvme_completion_is_error(status)) {
		bp->bio_flags |= BIO_ERROR;
		if (bp->bio_error == 0)
			bp->bio_error = EIO;
	}

	if ((bp->bio_flags & BIO_ERROR) == 0)
		bp->bio_resid = 0;
	else
		bp->bio_resid = bp->bio_bcount;

	bp_cb_fn(bp, status);
}

static void
nvme_bio_child_inbed(struct bio *parent, int bio_error)
{
	struct nvme_completion	parent_cpl;
	int			children, inbed;

	if (bio_error != 0) {
		parent->bio_flags |= BIO_ERROR;
		parent->bio_error = bio_error;
	}

	/*
	 * atomic_fetchadd will return value before adding 1, so we still
	 *  must add 1 to get the updated inbed number.  Save bio_children
	 *  before incrementing to guard against race conditions when
	 *  two children bios complete on different queues.
	 */
	children = atomic_load_acq_int(&parent->bio_children);
	inbed = atomic_fetchadd_int(&parent->bio_inbed, 1) + 1;
	if (inbed == children) {
		bzero(&parent_cpl, sizeof(parent_cpl));
		if (parent->bio_flags & BIO_ERROR) {
			parent_cpl.status &= ~(NVME_STATUS_SC_MASK << NVME_STATUS_SC_SHIFT);
			parent_cpl.status |= (NVME_SC_DATA_TRANSFER_ERROR) << NVME_STATUS_SC_SHIFT;
		}
		nvme_ns_bio_done(parent, &parent_cpl);
	}
}

static void
nvme_bio_child_done(void *arg, const struct nvme_completion *cpl)
{
	struct bio		*child = arg;
	struct bio		*parent;
	int			bio_error;

	parent = child->bio_parent;
	g_destroy_bio(child);
	bio_error = nvme_completion_is_error(cpl) ? EIO : 0;
	nvme_bio_child_inbed(parent, bio_error);
}

static uint32_t
nvme_get_num_segments(uint64_t addr, uint64_t size, uint32_t align)
{
	uint32_t	num_segs, offset, remainder;

	if (align == 0)
		return (1);

	KASSERT((align & (align - 1)) == 0, ("alignment not power of 2\n"));

	num_segs = size / align;
	remainder = size & (align - 1);
	offset = addr & (align - 1);
	if (remainder > 0 || offset > 0)
		num_segs += 1 + (remainder + offset - 1) / align;
	return (num_segs);
}

static void
nvme_free_child_bios(int num_bios, struct bio **child_bios)
{
	int i;

	for (i = 0; i < num_bios; i++) {
		if (child_bios[i] != NULL)
			g_destroy_bio(child_bios[i]);
	}

	free(child_bios, M_NVME);
}

static struct bio **
nvme_allocate_child_bios(int num_bios)
{
	struct bio **child_bios;
	int err = 0, i;

	child_bios = malloc(num_bios * sizeof(struct bio *), M_NVME, M_NOWAIT);
	if (child_bios == NULL)
		return (NULL);

	for (i = 0; i < num_bios; i++) {
		child_bios[i] = g_new_bio();
		if (child_bios[i] == NULL)
			err = ENOMEM;
	}

	if (err == ENOMEM) {
		nvme_free_child_bios(num_bios, child_bios);
		return (NULL);
	}

	return (child_bios);
}

static struct bio **
nvme_construct_child_bios(struct bio *bp, uint32_t alignment, int *num_bios)
{
	struct bio	**child_bios;
	struct bio	*child;
	uint64_t	cur_offset;
	caddr_t		data;
	uint32_t	rem_bcount;
	int		i;
	struct vm_page	**ma;
	uint32_t	ma_offset;

	*num_bios = nvme_get_num_segments(bp->bio_offset, bp->bio_bcount,
	    alignment);
	child_bios = nvme_allocate_child_bios(*num_bios);
	if (child_bios == NULL)
		return (NULL);

	bp->bio_children = *num_bios;
	bp->bio_inbed = 0;
	cur_offset = bp->bio_offset;
	rem_bcount = bp->bio_bcount;
	data = bp->bio_data;
	ma_offset = bp->bio_ma_offset;
	ma = bp->bio_ma;

	for (i = 0; i < *num_bios; i++) {
		child = child_bios[i];
		child->bio_parent = bp;
		child->bio_cmd = bp->bio_cmd;
		child->bio_offset = cur_offset;
		child->bio_bcount = min(rem_bcount,
		    alignment - (cur_offset & (alignment - 1)));
		child->bio_flags = bp->bio_flags;
		if (bp->bio_flags & BIO_UNMAPPED) {
			child->bio_ma_offset = ma_offset;
			child->bio_ma = ma;
			child->bio_ma_n =
			    nvme_get_num_segments(child->bio_ma_offset,
				child->bio_bcount, PAGE_SIZE);
			ma_offset = (ma_offset + child->bio_bcount) &
			    PAGE_MASK;
			ma += child->bio_ma_n;
			if (ma_offset != 0)
				ma -= 1;
		} else {
			child->bio_data = data;
			data += child->bio_bcount;
		}
		cur_offset += child->bio_bcount;
		rem_bcount -= child->bio_bcount;
	}

	return (child_bios);
}

static int
nvme_ns_split_bio(struct nvme_namespace *ns, struct bio *bp,
    uint32_t alignment)
{
	struct bio	*child;
	struct bio	**child_bios;
	int		err, i, num_bios;

	child_bios = nvme_construct_child_bios(bp, alignment, &num_bios);
	if (child_bios == NULL)
		return (ENOMEM);

	for (i = 0; i < num_bios; i++) {
		child = child_bios[i];
		err = nvme_ns_bio_process(ns, child, nvme_bio_child_done);
		if (err != 0) {
			nvme_bio_child_inbed(bp, err);
			g_destroy_bio(child);
		}
	}

	free(child_bios, M_NVME);
	return (0);
}

int
nvme_ns_bio_process(struct nvme_namespace *ns, struct bio *bp,
	nvme_cb_fn_t cb_fn)
{
	struct nvme_dsm_range	*dsm_range;
	uint32_t		num_bios;
	int			err;

	bp->bio_driver1 = cb_fn;

	if (ns->stripesize > 0 &&
	    (bp->bio_cmd == BIO_READ || bp->bio_cmd == BIO_WRITE)) {
		num_bios = nvme_get_num_segments(bp->bio_offset,
		    bp->bio_bcount, ns->stripesize);
		if (num_bios > 1)
			return (nvme_ns_split_bio(ns, bp, ns->stripesize));
	}

	switch (bp->bio_cmd) {
	case BIO_READ:
		err = nvme_ns_cmd_read_bio(ns, bp, nvme_ns_bio_done, bp);
		break;
	case BIO_WRITE:
		err = nvme_ns_cmd_write_bio(ns, bp, nvme_ns_bio_done, bp);
		break;
	case BIO_FLUSH:
		err = nvme_ns_cmd_flush(ns, nvme_ns_bio_done, bp);
		break;
	case BIO_DELETE:
		dsm_range =
		    malloc(sizeof(struct nvme_dsm_range), M_NVME,
		    M_ZERO | M_WAITOK);
		if (!dsm_range) {
			err = ENOMEM;
			break;
		}
		dsm_range->length =
		    htole32(bp->bio_bcount/nvme_ns_get_sector_size(ns));
		dsm_range->starting_lba =
		    htole64(bp->bio_offset/nvme_ns_get_sector_size(ns));
		bp->bio_driver2 = dsm_range;
		err = nvme_ns_cmd_deallocate(ns, dsm_range, 1,
			nvme_ns_bio_done, bp);
		if (err != 0)
			free(dsm_range, M_NVME);
		break;
	default:
		err = EIO;
		break;
	}

	return (err);
}

int
nvme_ns_construct(struct nvme_namespace *ns, uint32_t id,
    struct nvme_controller *ctrlr)
{
	struct make_dev_args                    md_args;
	struct nvme_completion_poll_status	status;
	int                                     res;
	int					unit;
	uint8_t					flbas_fmt;
	uint8_t					vwc_present;

	ns->ctrlr = ctrlr;
	ns->id = id;
	ns->stripesize = 0;

	/*
	 * Older Intel devices advertise in vendor specific space an alignment
	 * that improves performance.  If present use for the stripe size.  NVMe
	 * 1.3 standardized this as NOIOB, and newer Intel drives use that.
	 */
	switch (pci_get_devid(ctrlr->dev)) {
	case 0x09538086:		/* Intel DC PC3500 */
	case 0x0a538086:		/* Intel DC PC3520 */
	case 0x0a548086:		/* Intel DC PC4500 */
	case 0x0a558086:		/* Dell Intel P4600 */
		if (ctrlr->cdata.vs[3] != 0)
			ns->stripesize =
			    (1 << ctrlr->cdata.vs[3]) * ctrlr->min_page_size;
		break;
	default:
		break;
	}

	/*
	 * Namespaces are reconstructed after a controller reset, so check
	 *  to make sure we only call mtx_init once on each mtx.
	 *
	 * TODO: Move this somewhere where it gets called at controller
	 *  construction time, which is not invoked as part of each
	 *  controller reset.
	 */
	if (!mtx_initialized(&ns->lock))
		mtx_init(&ns->lock, "nvme ns lock", NULL, MTX_DEF);

	status.done = 0;
	nvme_ctrlr_cmd_identify_namespace(ctrlr, id, &ns->data,
	    nvme_completion_poll_cb, &status);
	while (!atomic_load_acq_int(&status.done))
		pause("nvme", 1);
	if (nvme_completion_is_error(&status.cpl)) {
		nvme_printf(ctrlr, "nvme_identify_namespace failed\n");
		return (ENXIO);
	}

	/* Convert data to host endian */
	nvme_namespace_data_swapbytes(&ns->data);

	/*
	 * If the size of is zero, chances are this isn't a valid
	 * namespace (eg one that's not been configured yet). The
	 * standard says the entire id will be zeros, so this is a
	 * cheap way to test for that.
	 */
	if (ns->data.nsze == 0)
		return (ENXIO);

	flbas_fmt = (ns->data.flbas >> NVME_NS_DATA_FLBAS_FORMAT_SHIFT) &
		NVME_NS_DATA_FLBAS_FORMAT_MASK;
	/*
	 * Note: format is a 0-based value, so > is appropriate here,
	 *  not >=.
	 */
	if (flbas_fmt > ns->data.nlbaf) {
		printf("lba format %d exceeds number supported (%d)\n",
		    flbas_fmt, ns->data.nlbaf + 1);
		return (ENXIO);
	}

	if (nvme_ctrlr_has_dataset_mgmt(&ctrlr->cdata))
		ns->flags |= NVME_NS_DEALLOCATE_SUPPORTED;

	vwc_present = (ctrlr->cdata.vwc >> NVME_CTRLR_DATA_VWC_PRESENT_SHIFT) &
		NVME_CTRLR_DATA_VWC_PRESENT_MASK;
	if (vwc_present)
		ns->flags |= NVME_NS_FLUSH_SUPPORTED;

	/*
	 * cdev may have already been created, if we are reconstructing the
	 *  namespace after a controller-level reset.
	 */
	if (ns->cdev != NULL)
		return (0);

	/*
	 * Namespace IDs start at 1, so we need to subtract 1 to create a
	 *  correct unit number.
	 */
	unit = device_get_unit(ctrlr->dev) * NVME_MAX_NAMESPACES + ns->id - 1;

	make_dev_args_init(&md_args);
	md_args.mda_devsw = &nvme_ns_cdevsw;
	md_args.mda_unit = unit;
	md_args.mda_mode = 0600;
	md_args.mda_si_drv1 = ns;
	res = make_dev_s(&md_args, &ns->cdev, "nvme%dns%d",
	    device_get_unit(ctrlr->dev), ns->id);
	if (res != 0)
		return (ENXIO);

	ns->cdev->si_flags |= SI_UNMAPPED;

	return (0);
}

void nvme_ns_destruct(struct nvme_namespace *ns)
{

	if (ns->cdev != NULL)
		destroy_dev(ns->cdev);
}
