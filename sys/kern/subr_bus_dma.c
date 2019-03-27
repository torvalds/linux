/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 EMC Corp.
 * All rights reserved.
 *
 * Copyright (c) 1997, 1998 Justin T. Gibbs.
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

#include "opt_bus.h"

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/systm.h>
#include <sys/bio.h>
#include <sys/bus.h>
#include <sys/callout.h>
#include <sys/mbuf.h>
#include <sys/memdesc.h>
#include <sys/proc.h>
#include <sys/uio.h>

#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/pmap.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>

#include <machine/bus.h>

/*
 * Load up data starting at offset within a region specified by a
 * list of virtual address ranges until either length or the region
 * are exhausted.
 */
static int
_bus_dmamap_load_vlist(bus_dma_tag_t dmat, bus_dmamap_t map,
    bus_dma_segment_t *list, int sglist_cnt, struct pmap *pmap, int *nsegs,
    int flags, size_t offset, size_t length)
{
	int error;

	error = 0;
	for (; sglist_cnt > 0 && length != 0; sglist_cnt--, list++) {
		char *addr;
		size_t ds_len;

		KASSERT((offset < list->ds_len),
		    ("Invalid mid-segment offset"));
		addr = (char *)(uintptr_t)list->ds_addr + offset;
		ds_len = list->ds_len - offset;
		offset = 0;
		if (ds_len > length)
			ds_len = length;
		length -= ds_len;
		KASSERT((ds_len != 0), ("Segment length is zero"));
		error = _bus_dmamap_load_buffer(dmat, map, addr, ds_len, pmap,
		    flags, NULL, nsegs);
		if (error)
			break;
	}
	return (error);
}

/*
 * Load a list of physical addresses.
 */
static int
_bus_dmamap_load_plist(bus_dma_tag_t dmat, bus_dmamap_t map,
    bus_dma_segment_t *list, int sglist_cnt, int *nsegs, int flags)
{
	int error;

	error = 0;
	for (; sglist_cnt > 0; sglist_cnt--, list++) {
		error = _bus_dmamap_load_phys(dmat, map,
		    (vm_paddr_t)list->ds_addr, list->ds_len, flags, NULL,
		    nsegs);
		if (error)
			break;
	}
	return (error);
}

/*
 * Load an mbuf chain.
 */
static int
_bus_dmamap_load_mbuf_sg(bus_dma_tag_t dmat, bus_dmamap_t map,
    struct mbuf *m0, bus_dma_segment_t *segs, int *nsegs, int flags)
{
	struct mbuf *m;
	int error;

	error = 0;
	for (m = m0; m != NULL && error == 0; m = m->m_next) {
		if (m->m_len > 0) {
			error = _bus_dmamap_load_buffer(dmat, map, m->m_data,
			    m->m_len, kernel_pmap, flags | BUS_DMA_LOAD_MBUF,
			    segs, nsegs);
		}
	}
	CTR5(KTR_BUSDMA, "%s: tag %p tag flags 0x%x error %d nsegs %d",
	    __func__, dmat, flags, error, *nsegs);
	return (error);
}

/*
 * Load from block io.
 */
static int
_bus_dmamap_load_bio(bus_dma_tag_t dmat, bus_dmamap_t map, struct bio *bio,
    int *nsegs, int flags)
{

	if ((bio->bio_flags & BIO_VLIST) != 0) {
		bus_dma_segment_t *segs = (bus_dma_segment_t *)bio->bio_data;
		return (_bus_dmamap_load_vlist(dmat, map, segs, bio->bio_ma_n,
		    kernel_pmap, nsegs, flags, bio->bio_ma_offset,
		    bio->bio_bcount));
	}

	if ((bio->bio_flags & BIO_UNMAPPED) != 0)
		return (_bus_dmamap_load_ma(dmat, map, bio->bio_ma,
		    bio->bio_bcount, bio->bio_ma_offset, flags, NULL, nsegs));

	return (_bus_dmamap_load_buffer(dmat, map, bio->bio_data,
	    bio->bio_bcount, kernel_pmap, flags, NULL, nsegs));
}

int
bus_dmamap_load_ma_triv(bus_dma_tag_t dmat, bus_dmamap_t map,
    struct vm_page **ma, bus_size_t tlen, int ma_offs, int flags,
    bus_dma_segment_t *segs, int *segp)
{
	vm_paddr_t paddr;
	bus_size_t len;
	int error, i;

	error = 0;
	for (i = 0; tlen > 0; i++, tlen -= len) {
		len = min(PAGE_SIZE - ma_offs, tlen);
		paddr = VM_PAGE_TO_PHYS(ma[i]) + ma_offs;
		error = _bus_dmamap_load_phys(dmat, map, paddr, len,
		    flags, segs, segp);
		if (error != 0)
			break;
		ma_offs = 0;
	}
	return (error);
}

/*
 * Load a cam control block.
 */
static int
_bus_dmamap_load_ccb(bus_dma_tag_t dmat, bus_dmamap_t map, union ccb *ccb,
		    int *nsegs, int flags)
{
	struct ccb_hdr *ccb_h;
	void *data_ptr;
	int error;
	uint32_t dxfer_len;
	uint16_t sglist_cnt;

	error = 0;
	ccb_h = &ccb->ccb_h;
	switch (ccb_h->func_code) {
	case XPT_SCSI_IO: {
		struct ccb_scsiio *csio;

		csio = &ccb->csio;
		data_ptr = csio->data_ptr;
		dxfer_len = csio->dxfer_len;
		sglist_cnt = csio->sglist_cnt;
		break;
	}
	case XPT_CONT_TARGET_IO: {
		struct ccb_scsiio *ctio;

		ctio = &ccb->ctio;
		data_ptr = ctio->data_ptr;
		dxfer_len = ctio->dxfer_len;
		sglist_cnt = ctio->sglist_cnt;
		break;
	}
	case XPT_ATA_IO: {
		struct ccb_ataio *ataio;

		ataio = &ccb->ataio;
		data_ptr = ataio->data_ptr;
		dxfer_len = ataio->dxfer_len;
		sglist_cnt = 0;
		break;
	}
	case XPT_NVME_IO:
	case XPT_NVME_ADMIN: {
		struct ccb_nvmeio *nvmeio;

		nvmeio = &ccb->nvmeio;
		data_ptr = nvmeio->data_ptr;
		dxfer_len = nvmeio->dxfer_len;
		sglist_cnt = nvmeio->sglist_cnt;
		break;
	}
	default:
		panic("_bus_dmamap_load_ccb: Unsupported func code %d",
		    ccb_h->func_code);
	}

	switch ((ccb_h->flags & CAM_DATA_MASK)) {
	case CAM_DATA_VADDR:
		error = _bus_dmamap_load_buffer(dmat, map, data_ptr, dxfer_len,
		    kernel_pmap, flags, NULL, nsegs);
		break;
	case CAM_DATA_PADDR:
		error = _bus_dmamap_load_phys(dmat, map,
		    (vm_paddr_t)(uintptr_t)data_ptr, dxfer_len, flags, NULL,
		    nsegs);
		break;
	case CAM_DATA_SG:
		error = _bus_dmamap_load_vlist(dmat, map,
		    (bus_dma_segment_t *)data_ptr, sglist_cnt, kernel_pmap,
		    nsegs, flags, 0, dxfer_len);
		break;
	case CAM_DATA_SG_PADDR:
		error = _bus_dmamap_load_plist(dmat, map,
		    (bus_dma_segment_t *)data_ptr, sglist_cnt, nsegs, flags);
		break;
	case CAM_DATA_BIO:
		error = _bus_dmamap_load_bio(dmat, map, (struct bio *)data_ptr,
		    nsegs, flags);
		break;
	default:
		panic("_bus_dmamap_load_ccb: flags 0x%X unimplemented",
		    ccb_h->flags);
	}
	return (error);
}

/*
 * Load a uio.
 */
static int
_bus_dmamap_load_uio(bus_dma_tag_t dmat, bus_dmamap_t map, struct uio *uio,
    int *nsegs, int flags)
{
	bus_size_t resid;
	bus_size_t minlen;
	struct iovec *iov;
	pmap_t pmap;
	caddr_t addr;
	int error, i;

	if (uio->uio_segflg == UIO_USERSPACE) {
		KASSERT(uio->uio_td != NULL,
			("bus_dmamap_load_uio: USERSPACE but no proc"));
		pmap = vmspace_pmap(uio->uio_td->td_proc->p_vmspace);
	} else
		pmap = kernel_pmap;
	resid = uio->uio_resid;
	iov = uio->uio_iov;
	error = 0;

	for (i = 0; i < uio->uio_iovcnt && resid != 0 && !error; i++) {
		/*
		 * Now at the first iovec to load.  Load each iovec
		 * until we have exhausted the residual count.
		 */

		addr = (caddr_t) iov[i].iov_base;
		minlen = resid < iov[i].iov_len ? resid : iov[i].iov_len;
		if (minlen > 0) {
			error = _bus_dmamap_load_buffer(dmat, map, addr,
			    minlen, pmap, flags, NULL, nsegs);
			resid -= minlen;
		}
	}

	return (error);
}

/*
 * Map the buffer buf into bus space using the dmamap map.
 */
int
bus_dmamap_load(bus_dma_tag_t dmat, bus_dmamap_t map, void *buf,
    bus_size_t buflen, bus_dmamap_callback_t *callback,
    void *callback_arg, int flags)
{
	bus_dma_segment_t *segs;
	struct memdesc mem;
	int error;
	int nsegs;

	if ((flags & BUS_DMA_NOWAIT) == 0) {
		mem = memdesc_vaddr(buf, buflen);
		_bus_dmamap_waitok(dmat, map, &mem, callback, callback_arg);
	}

	nsegs = -1;
	error = _bus_dmamap_load_buffer(dmat, map, buf, buflen, kernel_pmap,
	    flags, NULL, &nsegs);
	nsegs++;

	CTR5(KTR_BUSDMA, "%s: tag %p tag flags 0x%x error %d nsegs %d",
	    __func__, dmat, flags, error, nsegs);

	if (error == EINPROGRESS)
		return (error);

	segs = _bus_dmamap_complete(dmat, map, NULL, nsegs, error);
	if (error)
		(*callback)(callback_arg, segs, 0, error);
	else
		(*callback)(callback_arg, segs, nsegs, 0);

	/*
	 * Return ENOMEM to the caller so that it can pass it up the stack.
	 * This error only happens when NOWAIT is set, so deferral is disabled.
	 */
	if (error == ENOMEM)
		return (error);

	return (0);
}

int
bus_dmamap_load_mbuf(bus_dma_tag_t dmat, bus_dmamap_t map, struct mbuf *m0,
    bus_dmamap_callback2_t *callback, void *callback_arg, int flags)
{
	bus_dma_segment_t *segs;
	int nsegs, error;

	M_ASSERTPKTHDR(m0);

	flags |= BUS_DMA_NOWAIT;
	nsegs = -1;
	error = _bus_dmamap_load_mbuf_sg(dmat, map, m0, NULL, &nsegs, flags);
	++nsegs;

	segs = _bus_dmamap_complete(dmat, map, NULL, nsegs, error);
	if (error)
		(*callback)(callback_arg, segs, 0, 0, error);
	else
		(*callback)(callback_arg, segs, nsegs, m0->m_pkthdr.len, error);

	CTR5(KTR_BUSDMA, "%s: tag %p tag flags 0x%x error %d nsegs %d",
	    __func__, dmat, flags, error, nsegs);
	return (error);
}

int
bus_dmamap_load_mbuf_sg(bus_dma_tag_t dmat, bus_dmamap_t map, struct mbuf *m0,
    bus_dma_segment_t *segs, int *nsegs, int flags)
{
	int error;

	flags |= BUS_DMA_NOWAIT;
	*nsegs = -1;
	error = _bus_dmamap_load_mbuf_sg(dmat, map, m0, segs, nsegs, flags);
	++*nsegs;
	_bus_dmamap_complete(dmat, map, segs, *nsegs, error);
	return (error);
}

int
bus_dmamap_load_uio(bus_dma_tag_t dmat, bus_dmamap_t map, struct uio *uio,
    bus_dmamap_callback2_t *callback, void *callback_arg, int flags)
{
	bus_dma_segment_t *segs;
	int nsegs, error;

	flags |= BUS_DMA_NOWAIT;
	nsegs = -1;
	error = _bus_dmamap_load_uio(dmat, map, uio, &nsegs, flags);
	nsegs++;

	segs = _bus_dmamap_complete(dmat, map, NULL, nsegs, error);
	if (error)
		(*callback)(callback_arg, segs, 0, 0, error);
	else
		(*callback)(callback_arg, segs, nsegs, uio->uio_resid, error);

	CTR5(KTR_BUSDMA, "%s: tag %p tag flags 0x%x error %d nsegs %d",
	    __func__, dmat, flags, error, nsegs);
	return (error);
}

int
bus_dmamap_load_ccb(bus_dma_tag_t dmat, bus_dmamap_t map, union ccb *ccb,
		    bus_dmamap_callback_t *callback, void *callback_arg,
		    int flags)
{
	bus_dma_segment_t *segs;
	struct ccb_hdr *ccb_h;
	struct memdesc mem;
	int error;
	int nsegs;

	ccb_h = &ccb->ccb_h;
	if ((ccb_h->flags & CAM_DIR_MASK) == CAM_DIR_NONE) {
		callback(callback_arg, NULL, 0, 0);
		return (0);
	}
	if ((flags & BUS_DMA_NOWAIT) == 0) {
		mem = memdesc_ccb(ccb);
		_bus_dmamap_waitok(dmat, map, &mem, callback, callback_arg);
	}
	nsegs = -1;
	error = _bus_dmamap_load_ccb(dmat, map, ccb, &nsegs, flags);
	nsegs++;

	CTR5(KTR_BUSDMA, "%s: tag %p tag flags 0x%x error %d nsegs %d",
	    __func__, dmat, flags, error, nsegs);

	if (error == EINPROGRESS)
		return (error);

	segs = _bus_dmamap_complete(dmat, map, NULL, nsegs, error);
	if (error)
		(*callback)(callback_arg, segs, 0, error);
	else
		(*callback)(callback_arg, segs, nsegs, error);
	/*
	 * Return ENOMEM to the caller so that it can pass it up the stack.
	 * This error only happens when NOWAIT is set, so deferral is disabled.
	 */
	if (error == ENOMEM)
		return (error);

	return (0);
}

int
bus_dmamap_load_bio(bus_dma_tag_t dmat, bus_dmamap_t map, struct bio *bio,
		    bus_dmamap_callback_t *callback, void *callback_arg,
		    int flags)
{
	bus_dma_segment_t *segs;
	struct memdesc mem;
	int error;
	int nsegs;

	if ((flags & BUS_DMA_NOWAIT) == 0) {
		mem = memdesc_bio(bio);
		_bus_dmamap_waitok(dmat, map, &mem, callback, callback_arg);
	}
	nsegs = -1;
	error = _bus_dmamap_load_bio(dmat, map, bio, &nsegs, flags);
	nsegs++;

	CTR5(KTR_BUSDMA, "%s: tag %p tag flags 0x%x error %d nsegs %d",
	    __func__, dmat, flags, error, nsegs);

	if (error == EINPROGRESS)
		return (error);

	segs = _bus_dmamap_complete(dmat, map, NULL, nsegs, error);
	if (error)
		(*callback)(callback_arg, segs, 0, error);
	else
		(*callback)(callback_arg, segs, nsegs, error);
	/*
	 * Return ENOMEM to the caller so that it can pass it up the stack.
	 * This error only happens when NOWAIT is set, so deferral is disabled.
	 */
	if (error == ENOMEM)
		return (error);

	return (0);
}

int
bus_dmamap_load_mem(bus_dma_tag_t dmat, bus_dmamap_t map,
    struct memdesc *mem, bus_dmamap_callback_t *callback,
    void *callback_arg, int flags)
{
	bus_dma_segment_t *segs;
	int error;
	int nsegs;

	if ((flags & BUS_DMA_NOWAIT) == 0)
		_bus_dmamap_waitok(dmat, map, mem, callback, callback_arg);

	nsegs = -1;
	error = 0;
	switch (mem->md_type) {
	case MEMDESC_VADDR:
		error = _bus_dmamap_load_buffer(dmat, map, mem->u.md_vaddr,
		    mem->md_opaque, kernel_pmap, flags, NULL, &nsegs);
		break;
	case MEMDESC_PADDR:
		error = _bus_dmamap_load_phys(dmat, map, mem->u.md_paddr,
		    mem->md_opaque, flags, NULL, &nsegs);
		break;
	case MEMDESC_VLIST:
		error = _bus_dmamap_load_vlist(dmat, map, mem->u.md_list,
		    mem->md_opaque, kernel_pmap, &nsegs, flags, 0, SIZE_T_MAX);
		break;
	case MEMDESC_PLIST:
		error = _bus_dmamap_load_plist(dmat, map, mem->u.md_list,
		    mem->md_opaque, &nsegs, flags);
		break;
	case MEMDESC_BIO:
		error = _bus_dmamap_load_bio(dmat, map, mem->u.md_bio,
		    &nsegs, flags);
		break;
	case MEMDESC_UIO:
		error = _bus_dmamap_load_uio(dmat, map, mem->u.md_uio,
		    &nsegs, flags);
		break;
	case MEMDESC_MBUF:
		error = _bus_dmamap_load_mbuf_sg(dmat, map, mem->u.md_mbuf,
		    NULL, &nsegs, flags);
		break;
	case MEMDESC_CCB:
		error = _bus_dmamap_load_ccb(dmat, map, mem->u.md_ccb, &nsegs,
		    flags);
		break;
	}
	nsegs++;

	CTR5(KTR_BUSDMA, "%s: tag %p tag flags 0x%x error %d nsegs %d",
	    __func__, dmat, flags, error, nsegs);

	if (error == EINPROGRESS)
		return (error);

	segs = _bus_dmamap_complete(dmat, map, NULL, nsegs, error);
	if (error)
		(*callback)(callback_arg, segs, 0, error);
	else
		(*callback)(callback_arg, segs, nsegs, 0);

	/*
	 * Return ENOMEM to the caller so that it can pass it up the stack.
	 * This error only happens when NOWAIT is set, so deferral is disabled.
	 */
	if (error == ENOMEM)
		return (error);

	return (0);
}
