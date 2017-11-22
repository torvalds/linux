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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*	Copyright (c) 1983, 1984, 1985, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*
 * University Copyright- Copyright (c) 1982, 1986, 1988
 * The Regents of the University of California
 * All Rights Reserved
 *
 * University Acknowledgment- Portions of this document are derived from
 * software developed by the University of California, Berkeley, and its
 * contributors.
 */


#include <sys/types.h>
#include <sys/systm.h>
#include <sys/pathname.h>
#include <sys/kmem.h>

/*
 * Pathname utilities.
 *
 * In translating file names we copy each argument file
 * name into a pathname structure where we operate on it.
 * Each pathname structure can hold "pn_bufsize" characters
 * including a terminating null, and operations here support
 * allocating and freeing pathname structures, fetching
 * strings from user space, getting the next character from
 * a pathname, combining two pathnames (used in symbolic
 * link processing), and peeling off the first component
 * of a pathname.
 */

/*
 * Allocate contents of pathname structure.  Structure is typically
 * an automatic variable in calling routine for convenience.
 *
 * May sleep in the call to kmem_alloc() and so must not be called
 * from interrupt level.
 */
void
pn_alloc(struct pathname *pnp)
{
	pn_alloc_sz(pnp, MAXPATHLEN);
}
void
pn_alloc_sz(struct pathname *pnp, size_t sz)
{
	pnp->pn_path = pnp->pn_buf = kmem_alloc(sz, KM_SLEEP);
	pnp->pn_pathlen = 0;
	pnp->pn_bufsize = sz;
}

/*
 * Free pathname resources.
 */
void
pn_free(struct pathname *pnp)
{
	/* pn_bufsize is usually MAXPATHLEN, but may not be */
	kmem_free(pnp->pn_buf, pnp->pn_bufsize);
	pnp->pn_path = pnp->pn_buf = NULL;
	pnp->pn_pathlen = pnp->pn_bufsize = 0;
}
