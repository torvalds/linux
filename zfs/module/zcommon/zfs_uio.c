/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved	*/

/*
 * University Copyright- Copyright (c) 1982, 1986, 1988
 * The Regents of the University of California
 * All Rights Reserved
 *
 * University Acknowledgment- Portions of this document are derived from
 * software developed by the University of California, Berkeley, and its
 * contributors.
 */
/*
 * Copyright (c) 2015 by Chunwei Chen. All rights reserved.
 */

/*
 * The uio support from OpenSolaris has been added as a short term
 * work around.  The hope is to adopt native Linux type and drop the
 * use of uio's entirely.  Under Linux they only add overhead and
 * when possible we want to use native APIs for the ZPL layer.
 */
#ifdef _KERNEL

#include <sys/types.h>
#include <sys/uio_impl.h>
#include <linux/kmap_compat.h>

/*
 * Move "n" bytes at byte address "p"; "rw" indicates the direction
 * of the move, and the I/O parameters are provided in "uio", which is
 * update to reflect the data which was moved.  Returns 0 on success or
 * a non-zero errno on failure.
 */
static int
uiomove_iov(void *p, size_t n, enum uio_rw rw, struct uio *uio)
{
	const struct iovec *iov = uio->uio_iov;
	size_t skip = uio->uio_skip;
	ulong_t cnt;

	while (n && uio->uio_resid) {
		cnt = MIN(iov->iov_len - skip, n);
		switch (uio->uio_segflg) {
		case UIO_USERSPACE:
		case UIO_USERISPACE:
			/*
			 * p = kernel data pointer
			 * iov->iov_base = user data pointer
			 */
			if (rw == UIO_READ) {
				if (copy_to_user(iov->iov_base+skip, p, cnt))
					return (EFAULT);
			} else {
				if (copy_from_user(p, iov->iov_base+skip, cnt))
					return (EFAULT);
			}
			break;
		case UIO_SYSSPACE:
			if (rw == UIO_READ)
				bcopy(p, iov->iov_base + skip, cnt);
			else
				bcopy(iov->iov_base + skip, p, cnt);
			break;
		default:
			ASSERT(0);
		}
		skip += cnt;
		if (skip == iov->iov_len) {
			skip = 0;
			uio->uio_iov = (++iov);
			uio->uio_iovcnt--;
		}
		uio->uio_skip = skip;
		uio->uio_resid -= cnt;
		uio->uio_loffset += cnt;
		p = (caddr_t)p + cnt;
		n -= cnt;
	}
	return (0);
}

static int
uiomove_bvec(void *p, size_t n, enum uio_rw rw, struct uio *uio)
{
	const struct bio_vec *bv = uio->uio_bvec;
	size_t skip = uio->uio_skip;
	ulong_t cnt;

	while (n && uio->uio_resid) {
		void *paddr;
		cnt = MIN(bv->bv_len - skip, n);

		paddr = zfs_kmap_atomic(bv->bv_page, KM_USER1);
		if (rw == UIO_READ)
			bcopy(p, paddr + bv->bv_offset + skip, cnt);
		else
			bcopy(paddr + bv->bv_offset + skip, p, cnt);
		zfs_kunmap_atomic(paddr, KM_USER1);

		skip += cnt;
		if (skip == bv->bv_len) {
			skip = 0;
			uio->uio_bvec = (++bv);
			uio->uio_iovcnt--;
		}
		uio->uio_skip = skip;
		uio->uio_resid -= cnt;
		uio->uio_loffset += cnt;
		p = (caddr_t)p + cnt;
		n -= cnt;
	}
	return (0);
}

int
uiomove(void *p, size_t n, enum uio_rw rw, struct uio *uio)
{
	if (uio->uio_segflg != UIO_BVEC)
		return (uiomove_iov(p, n, rw, uio));
	else
		return (uiomove_bvec(p, n, rw, uio));
}
EXPORT_SYMBOL(uiomove);

#define	fuword8(uptr, vptr)	get_user((*vptr), (uptr))

/*
 * Fault in the pages of the first n bytes specified by the uio structure.
 * 1 byte in each page is touched and the uio struct is unmodified. Any
 * error will terminate the process as this is only a best attempt to get
 * the pages resident.
 */
void
uio_prefaultpages(ssize_t n, struct uio *uio)
{
	const struct iovec *iov;
	ulong_t cnt, incr;
	caddr_t p;
	uint8_t tmp;
	int iovcnt;
	size_t skip;

	/* no need to fault in kernel pages */
	switch (uio->uio_segflg) {
		case UIO_SYSSPACE:
		case UIO_BVEC:
			return;
		case UIO_USERSPACE:
		case UIO_USERISPACE:
			break;
		default:
			ASSERT(0);
	}

	iov = uio->uio_iov;
	iovcnt = uio->uio_iovcnt;
	skip = uio->uio_skip;

	for (; n > 0 && iovcnt > 0; iov++, iovcnt--, skip = 0) {
		cnt = MIN(iov->iov_len - skip, n);
		/* empty iov */
		if (cnt == 0)
			continue;
		n -= cnt;
		/*
		 * touch each page in this segment.
		 */
		p = iov->iov_base + skip;
		while (cnt) {
			if (fuword8((uint8_t *)p, &tmp))
				return;
			incr = MIN(cnt, PAGESIZE);
			p += incr;
			cnt -= incr;
		}
		/*
		 * touch the last byte in case it straddles a page.
		 */
		p--;
		if (fuword8((uint8_t *)p, &tmp))
			return;
	}
}
EXPORT_SYMBOL(uio_prefaultpages);

/*
 * same as uiomove() but doesn't modify uio structure.
 * return in cbytes how many bytes were copied.
 */
int
uiocopy(void *p, size_t n, enum uio_rw rw, struct uio *uio, size_t *cbytes)
{
	struct uio uio_copy;
	int ret;

	bcopy(uio, &uio_copy, sizeof (struct uio));
	ret = uiomove(p, n, rw, &uio_copy);
	*cbytes = uio->uio_resid - uio_copy.uio_resid;
	return (ret);
}
EXPORT_SYMBOL(uiocopy);

/*
 * Drop the next n chars out of *uiop.
 */
void
uioskip(uio_t *uiop, size_t n)
{
	if (n > uiop->uio_resid)
		return;

	uiop->uio_skip += n;
	if (uiop->uio_segflg != UIO_BVEC) {
		while (uiop->uio_iovcnt &&
		    uiop->uio_skip >= uiop->uio_iov->iov_len) {
			uiop->uio_skip -= uiop->uio_iov->iov_len;
			uiop->uio_iov++;
			uiop->uio_iovcnt--;
		}
	} else {
		while (uiop->uio_iovcnt &&
		    uiop->uio_skip >= uiop->uio_bvec->bv_len) {
			uiop->uio_skip -= uiop->uio_bvec->bv_len;
			uiop->uio_bvec++;
			uiop->uio_iovcnt--;
		}
	}
	uiop->uio_loffset += n;
	uiop->uio_resid -= n;
}
EXPORT_SYMBOL(uioskip);
#endif /* _KERNEL */
