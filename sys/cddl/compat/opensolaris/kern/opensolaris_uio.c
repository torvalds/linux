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

/*      Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T */
/*        All Rights Reserved   */

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
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/uio.h>
#include <sys/vnode.h>

/*
 * same as uiomove() but doesn't modify uio structure.
 * return in cbytes how many bytes were copied.
 */
int
uiocopy(void *p, size_t n, enum uio_rw rw, struct uio *uio, size_t *cbytes)
{
	struct iovec small_iovec[1];
	struct uio small_uio_clone;
	struct uio *uio_clone;
	int error;

	ASSERT3U(uio->uio_rw, ==, rw);
	if (uio->uio_iovcnt == 1) {
		small_uio_clone = *uio;
		small_iovec[0] = *uio->uio_iov;
		small_uio_clone.uio_iov = small_iovec;
		uio_clone = &small_uio_clone;
	} else {
		uio_clone = cloneuio(uio);
	}

	error = vn_io_fault_uiomove(p, n, uio_clone);
	*cbytes = uio->uio_resid - uio_clone->uio_resid;
	if (uio_clone != &small_uio_clone)
		free(uio_clone, M_IOV);
	return (error);
}

/*
 * Drop the next n chars out of *uiop.
 */
void
uioskip(uio_t *uio, size_t n)
{
	enum uio_seg segflg;

	/* For the full compatibility with illumos. */
	if (n > uio->uio_resid)
		return;

	segflg = uio->uio_segflg;
	uio->uio_segflg = UIO_NOCOPY;
	uiomove(NULL, n, uio->uio_rw, uio);
	uio->uio_segflg = segflg;
}
