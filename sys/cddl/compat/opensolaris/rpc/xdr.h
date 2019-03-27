/*
 * Sun RPC is a product of Sun Microsystems, Inc. and is provided for
 * unrestricted use provided that this legend is included on all tape
 * media and as a part of the software program in whole or part.  Users
 * may copy or modify Sun RPC without charge, but are not authorized
 * to license or distribute it to anyone else except as part of a product or
 * program developed by the user.
 *
 * SUN RPC IS PROVIDED AS IS WITH NO WARRANTIES OF ANY KIND INCLUDING THE
 * WARRANTIES OF DESIGN, MERCHANTIBILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE, OR ARISING FROM A COURSE OF DEALING, USAGE OR TRADE PRACTICE.
 *
 * Sun RPC is provided with no support and without any obligation on the
 * part of Sun Microsystems, Inc. to assist in its use, correction,
 * modification or enhancement.
 *
 * SUN MICROSYSTEMS, INC. SHALL HAVE NO LIABILITY WITH RESPECT TO THE
 * INFRINGEMENT OF COPYRIGHTS, TRADE SECRETS OR ANY PATENTS BY SUN RPC
 * OR ANY PART THEREOF.
 *
 * In no event will Sun Microsystems, Inc. be liable for any lost revenue
 * or profits or other special, indirect and consequential damages, even if
 * Sun has been advised of the possibility of such damages.
 *
 * Sun Microsystems, Inc.
 * 2550 Garcia Avenue
 * Mountain View, California  94043
 */

#ifndef	_OPENSOLARIS_RPC_XDR_H_
#define	_OPENSOLARIS_RPC_XDR_H_

#include_next <rpc/xdr.h>

#ifndef _KERNEL

#include <assert.h>

/*
 * Taken from sys/xdr/xdr_mem.c.
 *
 * FreeBSD's userland XDR doesn't implement control method (only the kernel),
 * but OpenSolaris nvpair still depend on it, so we have to implement it here.
 */
static __inline bool_t
xdrmem_control(XDR *xdrs, int request, void *info)
{
	xdr_bytesrec *xptr;

	switch (request) {
	case XDR_GET_BYTES_AVAIL:
		xptr = (xdr_bytesrec *)info;
		xptr->xc_is_last_record = TRUE;
		xptr->xc_num_avail = xdrs->x_handy;
		return (TRUE);
	default:
		assert(!"unexpected request");
	}
	return (FALSE);
}

#undef XDR_CONTROL
#define	XDR_CONTROL(xdrs, req, op)					\
	(((xdrs)->x_ops->x_control == NULL) ?				\
	    xdrmem_control((xdrs), (req), (op)) :			\
	    (*(xdrs)->x_ops->x_control)(xdrs, req, op))   

#endif	/* !_KERNEL */

#endif	/* !_OPENSOLARIS_RPC_XDR_H_ */
