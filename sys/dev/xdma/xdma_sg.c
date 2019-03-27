/*-
 * Copyright (c) 2018 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract FA8750-10-C-0237
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

#include "opt_platform.h"
#include <sys/param.h>
#include <sys/conf.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/sx.h>

#include <machine/bus.h>

#ifdef FDT
#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#endif

#include <dev/xdma/xdma.h>

#include <xdma_if.h>

struct seg_load_request {
	struct bus_dma_segment *seg;
	uint32_t nsegs;
	uint32_t error;
};

static int
_xchan_bufs_alloc(xdma_channel_t *xchan)
{
	xdma_controller_t *xdma;
	struct xdma_request *xr;
	int i;

	xdma = xchan->xdma;

	for (i = 0; i < xchan->xr_num; i++) {
		xr = &xchan->xr_mem[i];
		xr->buf.cbuf = contigmalloc(xchan->maxsegsize,
		    M_XDMA, 0, 0, ~0, PAGE_SIZE, 0);
		if (xr->buf.cbuf == NULL) {
			device_printf(xdma->dev,
			    "%s: Can't allocate contiguous kernel"
			    " physical memory\n", __func__);
			return (-1);
		}
	}

	return (0);
}

static int
_xchan_bufs_alloc_busdma(xdma_channel_t *xchan)
{
	xdma_controller_t *xdma;
	struct xdma_request *xr;
	int err;
	int i;

	xdma = xchan->xdma;

	/* Create bus_dma tag */
	err = bus_dma_tag_create(
	    bus_get_dma_tag(xdma->dev),	/* Parent tag. */
	    xchan->alignment,		/* alignment */
	    xchan->boundary,		/* boundary */
	    xchan->lowaddr,		/* lowaddr */
	    xchan->highaddr,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    xchan->maxsegsize * xchan->maxnsegs, /* maxsize */
	    xchan->maxnsegs,		/* nsegments */
	    xchan->maxsegsize,		/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &xchan->dma_tag_bufs);
	if (err != 0) {
		device_printf(xdma->dev,
		    "%s: Can't create bus_dma tag.\n", __func__);
		return (-1);
	}

	for (i = 0; i < xchan->xr_num; i++) {
		xr = &xchan->xr_mem[i];
		err = bus_dmamap_create(xchan->dma_tag_bufs, 0,
		    &xr->buf.map);
		if (err != 0) {
			device_printf(xdma->dev,
			    "%s: Can't create buf DMA map.\n", __func__);

			/* Cleanup. */
			bus_dma_tag_destroy(xchan->dma_tag_bufs);

			return (-1);
		}
	}

	return (0);
}

static int
xchan_bufs_alloc(xdma_channel_t *xchan)
{
	xdma_controller_t *xdma;
	int ret;

	xdma = xchan->xdma;

	if (xdma == NULL) {
		device_printf(xdma->dev,
		    "%s: Channel was not allocated properly.\n", __func__);
		return (-1);
	}

	if (xchan->caps & XCHAN_CAP_BUSDMA)
		ret = _xchan_bufs_alloc_busdma(xchan);
	else
		ret = _xchan_bufs_alloc(xchan);
	if (ret != 0) {
		device_printf(xdma->dev,
		    "%s: Can't allocate bufs.\n", __func__);
		return (-1);
	}

	xchan->flags |= XCHAN_BUFS_ALLOCATED;

	return (0);
}

static int
xchan_bufs_free(xdma_channel_t *xchan)
{
	struct xdma_request *xr;
	struct xchan_buf *b;
	int i;

	if ((xchan->flags & XCHAN_BUFS_ALLOCATED) == 0)
		return (-1);

	if (xchan->caps & XCHAN_CAP_BUSDMA) {
		for (i = 0; i < xchan->xr_num; i++) {
			xr = &xchan->xr_mem[i];
			b = &xr->buf;
			bus_dmamap_destroy(xchan->dma_tag_bufs, b->map);
		}
		bus_dma_tag_destroy(xchan->dma_tag_bufs);
	} else {
		for (i = 0; i < xchan->xr_num; i++) {
			xr = &xchan->xr_mem[i];
			contigfree(xr->buf.cbuf, xchan->maxsegsize, M_XDMA);
		}
	}

	xchan->flags &= ~XCHAN_BUFS_ALLOCATED;

	return (0);
}

void
xdma_channel_free_sg(xdma_channel_t *xchan)
{

	xchan_bufs_free(xchan);
	xchan_sglist_free(xchan);
	xchan_bank_free(xchan);
}

/*
 * Prepare xchan for a scatter-gather transfer.
 * xr_num - xdma requests queue size,
 * maxsegsize - maximum allowed scatter-gather list element size in bytes
 */
int
xdma_prep_sg(xdma_channel_t *xchan, uint32_t xr_num,
    bus_size_t maxsegsize, bus_size_t maxnsegs,
    bus_size_t alignment, bus_addr_t boundary,
    bus_addr_t lowaddr, bus_addr_t highaddr)
{
	xdma_controller_t *xdma;
	int ret;

	xdma = xchan->xdma;

	KASSERT(xdma != NULL, ("xdma is NULL"));

	if (xchan->flags & XCHAN_CONFIGURED) {
		device_printf(xdma->dev,
		    "%s: Channel is already configured.\n", __func__);
		return (-1);
	}

	xchan->xr_num = xr_num;
	xchan->maxsegsize = maxsegsize;
	xchan->maxnsegs = maxnsegs;
	xchan->alignment = alignment;
	xchan->boundary = boundary;
	xchan->lowaddr = lowaddr;
	xchan->highaddr = highaddr;

	if (xchan->maxnsegs > XDMA_MAX_SEG) {
		device_printf(xdma->dev, "%s: maxnsegs is too big\n",
		    __func__);
		return (-1);
	}

	xchan_bank_init(xchan);

	/* Allocate sglist. */
	ret = xchan_sglist_alloc(xchan);
	if (ret != 0) {
		device_printf(xdma->dev,
		    "%s: Can't allocate sglist.\n", __func__);
		return (-1);
	}

	/* Allocate bufs. */
	ret = xchan_bufs_alloc(xchan);
	if (ret != 0) {
		device_printf(xdma->dev,
		    "%s: Can't allocate bufs.\n", __func__);

		/* Cleanup */
		xchan_sglist_free(xchan);
		xchan_bank_free(xchan);

		return (-1);
	}

	xchan->flags |= (XCHAN_CONFIGURED | XCHAN_TYPE_SG);

	XCHAN_LOCK(xchan);
	ret = XDMA_CHANNEL_PREP_SG(xdma->dma_dev, xchan);
	if (ret != 0) {
		device_printf(xdma->dev,
		    "%s: Can't prepare SG transfer.\n", __func__);
		XCHAN_UNLOCK(xchan);

		return (-1);
	}
	XCHAN_UNLOCK(xchan);

	return (0);
}

void
xchan_seg_done(xdma_channel_t *xchan,
    struct xdma_transfer_status *st)
{
	struct xdma_request *xr;
	xdma_controller_t *xdma;
	struct xchan_buf *b;

	xdma = xchan->xdma;

	xr = TAILQ_FIRST(&xchan->processing);
	if (xr == NULL)
		panic("request not found\n");

	b = &xr->buf;

	atomic_subtract_int(&b->nsegs_left, 1);

	if (b->nsegs_left == 0) {
		if (xchan->caps & XCHAN_CAP_BUSDMA) {
			if (xr->direction == XDMA_MEM_TO_DEV)
				bus_dmamap_sync(xchan->dma_tag_bufs, b->map, 
				    BUS_DMASYNC_POSTWRITE);
			else
				bus_dmamap_sync(xchan->dma_tag_bufs, b->map, 
				    BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(xchan->dma_tag_bufs, b->map);
		}
		xr->status.error = st->error;
		xr->status.transferred = st->transferred;

		QUEUE_PROC_LOCK(xchan);
		TAILQ_REMOVE(&xchan->processing, xr, xr_next);
		QUEUE_PROC_UNLOCK(xchan);

		QUEUE_OUT_LOCK(xchan);
		TAILQ_INSERT_TAIL(&xchan->queue_out, xr, xr_next);
		QUEUE_OUT_UNLOCK(xchan);
	}
}

static void
xdma_dmamap_cb(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
	struct seg_load_request *slr;
	struct bus_dma_segment *seg;
	int i;

	slr = arg;
	seg = slr->seg;

	if (error != 0) {
		slr->error = error;
		return;
	}

	slr->nsegs = nsegs;

	for (i = 0; i < nsegs; i++) {
		seg[i].ds_addr = segs[i].ds_addr;
		seg[i].ds_len = segs[i].ds_len;
	}
}

static int
_xdma_load_data_busdma(xdma_channel_t *xchan, struct xdma_request *xr,
    struct bus_dma_segment *seg)
{
	xdma_controller_t *xdma;
	struct seg_load_request slr;
	uint32_t nsegs;
	void *addr;
	int error;

	xdma = xchan->xdma;

	error = 0;
	nsegs = 0;

	switch (xr->req_type) {
	case XR_TYPE_MBUF:
		error = bus_dmamap_load_mbuf_sg(xchan->dma_tag_bufs,
		    xr->buf.map, xr->m, seg, &nsegs, BUS_DMA_NOWAIT);
		break;
	case XR_TYPE_BIO:
		slr.nsegs = 0;
		slr.error = 0;
		slr.seg = seg;
		error = bus_dmamap_load_bio(xchan->dma_tag_bufs,
		    xr->buf.map, xr->bp, xdma_dmamap_cb, &slr, BUS_DMA_NOWAIT);
		if (slr.error != 0) {
			device_printf(xdma->dma_dev,
			    "%s: bus_dmamap_load failed, err %d\n",
			    __func__, slr.error);
			return (0);
		}
		nsegs = slr.nsegs;
		break;
	case XR_TYPE_VIRT:
		switch (xr->direction) {
		case XDMA_MEM_TO_DEV:
			addr = (void *)xr->src_addr;
			break;
		case XDMA_DEV_TO_MEM:
			addr = (void *)xr->dst_addr;
			break;
		default:
			device_printf(xdma->dma_dev,
			    "%s: Direction is not supported\n", __func__);
			return (0);
		}
		slr.nsegs = 0;
		slr.error = 0;
		slr.seg = seg;
		error = bus_dmamap_load(xchan->dma_tag_bufs, xr->buf.map,
		    addr, (xr->block_len * xr->block_num),
		    xdma_dmamap_cb, &slr, BUS_DMA_NOWAIT);
		if (slr.error != 0) {
			device_printf(xdma->dma_dev,
			    "%s: bus_dmamap_load failed, err %d\n",
			    __func__, slr.error);
			return (0);
		}
		nsegs = slr.nsegs;
		break;
	default:
		break;
	}

	if (error != 0) {
		if (error == ENOMEM) {
			/*
			 * Out of memory. Try again later.
			 * TODO: count errors.
			 */
		} else
			device_printf(xdma->dma_dev,
			    "%s: bus_dmamap_load failed with err %d\n",
			    __func__, error);
		return (0);
	}

	if (xr->direction == XDMA_MEM_TO_DEV)
		bus_dmamap_sync(xchan->dma_tag_bufs, xr->buf.map,
		    BUS_DMASYNC_PREWRITE);
	else
		bus_dmamap_sync(xchan->dma_tag_bufs, xr->buf.map,
		    BUS_DMASYNC_PREREAD);

	return (nsegs);
}

static int
_xdma_load_data(xdma_channel_t *xchan, struct xdma_request *xr,
    struct bus_dma_segment *seg)
{
	xdma_controller_t *xdma;
	struct mbuf *m;
	uint32_t nsegs;

	xdma = xchan->xdma;

	m = xr->m;

	nsegs = 1;

	switch (xr->req_type) {
	case XR_TYPE_MBUF:
		if (xr->direction == XDMA_MEM_TO_DEV) {
			m_copydata(m, 0, m->m_pkthdr.len, xr->buf.cbuf);
			seg[0].ds_addr = (bus_addr_t)xr->buf.cbuf;
			seg[0].ds_len = m->m_pkthdr.len;
		} else {
			seg[0].ds_addr = mtod(m, bus_addr_t);
			seg[0].ds_len = m->m_pkthdr.len;
		}
		break;
	case XR_TYPE_BIO:
	case XR_TYPE_VIRT:
	default:
		panic("implement me\n");
	}

	return (nsegs);
}

static int
xdma_load_data(xdma_channel_t *xchan,
    struct xdma_request *xr, struct bus_dma_segment *seg)
{
	xdma_controller_t *xdma;
	int error;
	int nsegs;

	xdma = xchan->xdma;

	error = 0;
	nsegs = 0;

	if (xchan->caps & XCHAN_CAP_BUSDMA)
		nsegs = _xdma_load_data_busdma(xchan, xr, seg);
	else
		nsegs = _xdma_load_data(xchan, xr, seg);
	if (nsegs == 0)
		return (0); /* Try again later. */

	xr->buf.nsegs = nsegs;
	xr->buf.nsegs_left = nsegs;

	return (nsegs);
}

static int
xdma_process(xdma_channel_t *xchan,
    struct xdma_sglist *sg)
{
	struct bus_dma_segment seg[XDMA_MAX_SEG];
	struct xdma_request *xr;
	struct xdma_request *xr_tmp;
	xdma_controller_t *xdma;
	uint32_t capacity;
	uint32_t n;
	uint32_t c;
	int nsegs;
	int ret;

	XCHAN_ASSERT_LOCKED(xchan);

	xdma = xchan->xdma;

	n = 0;

	ret = XDMA_CHANNEL_CAPACITY(xdma->dma_dev, xchan, &capacity);
	if (ret != 0) {
		device_printf(xdma->dev,
		    "%s: Can't get DMA controller capacity.\n", __func__);
		return (-1);
	}

	TAILQ_FOREACH_SAFE(xr, &xchan->queue_in, xr_next, xr_tmp) {
		switch (xr->req_type) {
		case XR_TYPE_MBUF:
			c = xdma_mbuf_defrag(xchan, xr);
			break;
		case XR_TYPE_BIO:
		case XR_TYPE_VIRT:
		default:
			c = 1;
		}

		if (capacity <= (c + n)) {
			/*
			 * No space yet available for the entire
			 * request in the DMA engine.
			 */
			break;
		}

		if ((c + n + xchan->maxnsegs) >= XDMA_SGLIST_MAXLEN) {
			/* Sglist is full. */
			break;
		}

		nsegs = xdma_load_data(xchan, xr, seg);
		if (nsegs == 0)
			break;

		xdma_sglist_add(&sg[n], seg, nsegs, xr);
		n += nsegs;

		QUEUE_IN_LOCK(xchan);
		TAILQ_REMOVE(&xchan->queue_in, xr, xr_next);
		QUEUE_IN_UNLOCK(xchan);

		QUEUE_PROC_LOCK(xchan);
		TAILQ_INSERT_TAIL(&xchan->processing, xr, xr_next);
		QUEUE_PROC_UNLOCK(xchan);
	}

	return (n);
}

int
xdma_queue_submit_sg(xdma_channel_t *xchan)
{
	struct xdma_sglist *sg;
	xdma_controller_t *xdma;
	uint32_t sg_n;
	int ret;

	xdma = xchan->xdma;
	KASSERT(xdma != NULL, ("xdma is NULL"));

	XCHAN_ASSERT_LOCKED(xchan);

	sg = xchan->sg;

	if ((xchan->flags & XCHAN_BUFS_ALLOCATED) == 0) {
		device_printf(xdma->dev,
		    "%s: Can't submit a transfer: no bufs\n",
		    __func__);
		return (-1);
	}

	sg_n = xdma_process(xchan, sg);
	if (sg_n == 0)
		return (0); /* Nothing to submit */

	/* Now submit sglist to DMA engine driver. */
	ret = XDMA_CHANNEL_SUBMIT_SG(xdma->dma_dev, xchan, sg, sg_n);
	if (ret != 0) {
		device_printf(xdma->dev,
		    "%s: Can't submit an sglist.\n", __func__);
		return (-1);
	}

	return (0);
}
