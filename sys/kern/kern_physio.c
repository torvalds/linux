/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1994 John S. Dyson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Absolutely no warranty of function or purpose is made by the author
 *    John S. Dyson.
 * 4. Modifications may be freely made to this file if the above conditions
 *    are met.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/racct.h>
#include <sys/uio.h>
#include <geom/geom.h>

#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/vm_extern.h>
#include <vm/vm_map.h>

int
physio(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct cdevsw *csw;
	struct buf *pbuf;
	struct bio *bp;
	struct vm_page **pages;
	caddr_t sa;
	u_int iolen, poff;
	int error, i, npages, maxpages;
	vm_prot_t prot;

	csw = dev->si_devsw;
	npages = 0;
	sa = NULL;
	/* check if character device is being destroyed */
	if (csw == NULL)
		return (ENXIO);

	/* XXX: sanity check */
	if(dev->si_iosize_max < PAGE_SIZE) {
		printf("WARNING: %s si_iosize_max=%d, using DFLTPHYS.\n",
		    devtoname(dev), dev->si_iosize_max);
		dev->si_iosize_max = DFLTPHYS;
	}

	/*
	 * If the driver does not want I/O to be split, that means that we
	 * need to reject any requests that will not fit into one buffer.
	 */
	if (dev->si_flags & SI_NOSPLIT &&
	    (uio->uio_resid > dev->si_iosize_max || uio->uio_resid > MAXPHYS ||
	    uio->uio_iovcnt > 1)) {
		/*
		 * Tell the user why his I/O was rejected.
		 */
		if (uio->uio_resid > dev->si_iosize_max)
			uprintf("%s: request size=%zd > si_iosize_max=%d; "
			    "cannot split request\n", devtoname(dev),
			    uio->uio_resid, dev->si_iosize_max);
		if (uio->uio_resid > MAXPHYS)
			uprintf("%s: request size=%zd > MAXPHYS=%d; "
			    "cannot split request\n", devtoname(dev),
			    uio->uio_resid, MAXPHYS);
		if (uio->uio_iovcnt > 1)
			uprintf("%s: request vectors=%d > 1; "
			    "cannot split request\n", devtoname(dev),
			    uio->uio_iovcnt);
		return (EFBIG);
	}

	/*
	 * Keep the process UPAGES from being swapped.  Processes swapped
	 * out while holding pbufs, used by swapper, may lead to deadlock.
	 */
	PHOLD(curproc);

	bp = g_alloc_bio();
	if (uio->uio_segflg != UIO_USERSPACE) {
		pbuf = NULL;
		pages = NULL;
	} else if ((dev->si_flags & SI_UNMAPPED) && unmapped_buf_allowed) {
		pbuf = NULL;
		maxpages = btoc(MIN(uio->uio_resid, MAXPHYS)) + 1;
		pages = malloc(sizeof(*pages) * maxpages, M_DEVBUF, M_WAITOK);
	} else {
		pbuf = uma_zalloc(pbuf_zone, M_WAITOK);
		sa = pbuf->b_data;
		maxpages = btoc(MAXPHYS);
		pages = pbuf->b_pages;
	}
	prot = VM_PROT_READ;
	if (uio->uio_rw == UIO_READ)
		prot |= VM_PROT_WRITE;	/* Less backwards than it looks */
	error = 0;
	for (i = 0; i < uio->uio_iovcnt; i++) {
#ifdef RACCT
		if (racct_enable) {
			PROC_LOCK(curproc);
			if (uio->uio_rw == UIO_READ) {
				racct_add_force(curproc, RACCT_READBPS,
				    uio->uio_iov[i].iov_len);
				racct_add_force(curproc, RACCT_READIOPS, 1);
			} else {
				racct_add_force(curproc, RACCT_WRITEBPS,
				    uio->uio_iov[i].iov_len);
				racct_add_force(curproc, RACCT_WRITEIOPS, 1);
			}
			PROC_UNLOCK(curproc);
		}
#endif /* RACCT */

		while (uio->uio_iov[i].iov_len) {
			g_reset_bio(bp);
			if (uio->uio_rw == UIO_READ) {
				bp->bio_cmd = BIO_READ;
				curthread->td_ru.ru_inblock++;
			} else {
				bp->bio_cmd = BIO_WRITE;
				curthread->td_ru.ru_oublock++;
			}
			bp->bio_offset = uio->uio_offset;
			bp->bio_data = uio->uio_iov[i].iov_base;
			bp->bio_length = uio->uio_iov[i].iov_len;
			if (bp->bio_length > dev->si_iosize_max)
				bp->bio_length = dev->si_iosize_max;
			if (bp->bio_length > MAXPHYS)
				bp->bio_length = MAXPHYS;

			/*
			 * Make sure the pbuf can map the request.
			 * The pbuf has kvasize = MAXPHYS, so a request
			 * larger than MAXPHYS - PAGE_SIZE must be
			 * page aligned or it will be fragmented.
			 */
			poff = (vm_offset_t)bp->bio_data & PAGE_MASK;
			if (pbuf && bp->bio_length + poff > pbuf->b_kvasize) {
				if (dev->si_flags & SI_NOSPLIT) {
					uprintf("%s: request ptr %p is not "
					    "on a page boundary; cannot split "
					    "request\n", devtoname(dev),
					    bp->bio_data);
					error = EFBIG;
					goto doerror;
				}
				bp->bio_length = pbuf->b_kvasize;
				if (poff != 0)
					bp->bio_length -= PAGE_SIZE;
			}

			bp->bio_bcount = bp->bio_length;
			bp->bio_dev = dev;

			if (pages) {
				if ((npages = vm_fault_quick_hold_pages(
				    &curproc->p_vmspace->vm_map,
				    (vm_offset_t)bp->bio_data, bp->bio_length,
				    prot, pages, maxpages)) < 0) {
					error = EFAULT;
					goto doerror;
				}
				if (pbuf && sa) {
					pmap_qenter((vm_offset_t)sa,
					    pages, npages);
					bp->bio_data = sa + poff;
				} else {
					bp->bio_ma = pages;
					bp->bio_ma_n = npages;
					bp->bio_ma_offset = poff;
					bp->bio_data = unmapped_buf;
					bp->bio_flags |= BIO_UNMAPPED;
				}
			}

			csw->d_strategy(bp);
			if (uio->uio_rw == UIO_READ)
				biowait(bp, "physrd");
			else
				biowait(bp, "physwr");

			if (pages) {
				if (pbuf)
					pmap_qremove((vm_offset_t)sa, npages);
				vm_page_unhold_pages(pages, npages);
			}

			iolen = bp->bio_length - bp->bio_resid;
			if (iolen == 0 && !(bp->bio_flags & BIO_ERROR))
				goto doerror;	/* EOF */
			uio->uio_iov[i].iov_len -= iolen;
			uio->uio_iov[i].iov_base =
			    (char *)uio->uio_iov[i].iov_base + iolen;
			uio->uio_resid -= iolen;
			uio->uio_offset += iolen;
			if (bp->bio_flags & BIO_ERROR) {
				error = bp->bio_error;
				goto doerror;
			}
		}
	}
doerror:
	if (pbuf)
		uma_zfree(pbuf_zone, pbuf);
	else if (pages)
		free(pages, M_DEVBUF);
	g_destroy_bio(bp);
	PRELE(curproc);
	return (error);
}
