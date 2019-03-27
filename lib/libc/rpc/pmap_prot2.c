/*	$NetBSD: pmap_prot2.c,v 1.14 2000/07/06 03:10:34 christos Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2009, Sun Microsystems, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met:
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution.
 * - Neither the name of Sun Microsystems, Inc. nor the names of its 
 *   contributors may be used to endorse or promote products derived 
 *   from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE 
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
 * POSSIBILITY OF SUCH DAMAGE.
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char *sccsid2 = "@(#)pmap_prot2.c 1.3 87/08/11 Copyr 1984 Sun Micro";
static char *sccsid = "@(#)pmap_prot2.c	2.1 88/07/29 4.0 RPCSRC";
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * pmap_prot2.c
 * Protocol for the local binder service, or pmap.
 *
 * Copyright (C) 1984, Sun Microsystems, Inc.
 */

#include "namespace.h"
#include <assert.h>

#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/pmap_prot.h>
#include "un-namespace.h"


/*
 * What is going on with linked lists? (!)
 * First recall the link list declaration from pmap_prot.h:
 *
 * struct pmaplist {
 *	struct pmap pml_map;
 *	struct pmaplist *pml_map;
 * };
 *
 * Compare that declaration with a corresponding xdr declaration that
 * is (a) pointer-less, and (b) recursive:
 *
 * typedef union switch (bool_t) {
 *
 *	case TRUE: struct {
 *		struct pmap;
 * 		pmaplist_t foo;
 *	};
 *
 *	case FALSE: struct {};
 * } pmaplist_t;
 *
 * Notice that the xdr declaration has no nxt pointer while
 * the C declaration has no bool_t variable.  The bool_t can be
 * interpreted as ``more data follows me''; if FALSE then nothing
 * follows this bool_t; if TRUE then the bool_t is followed by
 * an actual struct pmap, and then (recursively) by the
 * xdr union, pamplist_t.
 *
 * This could be implemented via the xdr_union primitive, though this
 * would cause a one recursive call per element in the list.  Rather than do
 * that we can ``unwind'' the recursion
 * into a while loop and do the union arms in-place.
 *
 * The head of the list is what the C programmer wishes to past around
 * the net, yet is the data that the pointer points to which is interesting;
 * this sounds like a job for xdr_reference!
 */
bool_t
xdr_pmaplist(XDR *xdrs, struct pmaplist **rp)
{
	/*
	 * more_elements is pre-computed in case the direction is
	 * XDR_ENCODE or XDR_FREE.  more_elements is overwritten by
	 * xdr_bool when the direction is XDR_DECODE.
	 */
	bool_t more_elements;
	int freeing;
	struct pmaplist **next	= NULL; /* pacify gcc */

	assert(xdrs != NULL);
	assert(rp != NULL);

	freeing = (xdrs->x_op == XDR_FREE);

	for (;;) {
		more_elements = (bool_t)(*rp != NULL);
		if (! xdr_bool(xdrs, &more_elements))
			return (FALSE);
		if (! more_elements)
			return (TRUE);  /* we are done */
		/*
		 * the unfortunate side effect of non-recursion is that in
		 * the case of freeing we must remember the next object
		 * before we free the current object ...
		 */
		if (freeing)
			next = &((*rp)->pml_next); 
		if (! xdr_reference(xdrs, (caddr_t *)rp,
		    (u_int)sizeof(struct pmaplist), (xdrproc_t)xdr_pmap))
			return (FALSE);
		rp = (freeing) ? next : &((*rp)->pml_next);
	}
}


/*
 * xdr_pmaplist_ptr() is specified to take a PMAPLIST *, but is identical in
 * functionality to xdr_pmaplist().
 */
bool_t
xdr_pmaplist_ptr(XDR *xdrs, struct pmaplist *rp)
{
	return xdr_pmaplist(xdrs, (struct pmaplist **)(void *)rp);
}
