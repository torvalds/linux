/*	$OpenBSD: xdr_mem.c,v 1.18 2022/02/14 03:38:59 guenther Exp $ */

/*
 * Copyright (c) 2010, Oracle America, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials
 *       provided with the distribution.
 *     * Neither the name of the "Oracle America, Inc." nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *   FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *   COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 *   INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 *   GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * xdr_mem.h, XDR implementation using memory buffers.
 *
 * If you have some data to be interpreted as external data representation
 * or to be converted to external data representation in a memory buffer,
 * then this is the package for you.
 *
 */

#include <sys/types.h>
#include <netinet/in.h>
#include <string.h>

#include <rpc/types.h>
#include <rpc/xdr.h>

static bool_t	xdrmem_getlong_aligned(XDR *, long *);
static bool_t	xdrmem_putlong_aligned(XDR *, long *);
static bool_t	xdrmem_getlong_unaligned(XDR *, long *);
static bool_t	xdrmem_putlong_unaligned(XDR *, long *);
static bool_t	xdrmem_getbytes(XDR *, caddr_t, u_int);
static bool_t	xdrmem_putbytes(XDR *, caddr_t, u_int);
static u_int	xdrmem_getpos(XDR *); /* XXX w/64-bit pointers, u_int not enough! */
static bool_t	xdrmem_setpos(XDR *, u_int);
static int32_t *xdrmem_inline_aligned(XDR *, u_int);
static int32_t *xdrmem_inline_unaligned(XDR *, u_int);
static void	xdrmem_destroy(XDR *);

static const struct xdr_ops xdrmem_ops_aligned = {
	xdrmem_getlong_aligned,
	xdrmem_putlong_aligned,
	xdrmem_getbytes,
	xdrmem_putbytes,
	xdrmem_getpos,
	xdrmem_setpos,
	xdrmem_inline_aligned,
	xdrmem_destroy,
	NULL,	/* xdrmem_control */
};

static const struct xdr_ops xdrmem_ops_unaligned = {
	xdrmem_getlong_unaligned,
	xdrmem_putlong_unaligned,
	xdrmem_getbytes,
	xdrmem_putbytes,
	xdrmem_getpos,
	xdrmem_setpos,
	xdrmem_inline_unaligned,
	xdrmem_destroy,
	NULL,	/* xdrmem_control */
};

/*
 * The procedure xdrmem_create initializes a stream descriptor for a
 * memory buffer.  
 */
void
xdrmem_create(XDR *xdrs, caddr_t addr, u_int size, enum xdr_op op)
{

	xdrs->x_op = op;
	xdrs->x_ops = ((size_t)addr & (sizeof(int32_t) - 1))
	    ? &xdrmem_ops_unaligned : &xdrmem_ops_aligned;
	xdrs->x_private = xdrs->x_base = addr;
	xdrs->x_handy = size;
}
DEF_WEAK(xdrmem_create);

static void
xdrmem_destroy(XDR *xdrs)
{
}

static bool_t
xdrmem_getlong_aligned(XDR *xdrs, long int *lp)
{

	if (xdrs->x_handy < sizeof(int32_t))
		return (FALSE);
	xdrs->x_handy -= sizeof(int32_t);
	*lp = ntohl(*(int32_t *)xdrs->x_private);
	xdrs->x_private += sizeof(int32_t);
	return (TRUE);
}

static bool_t
xdrmem_putlong_aligned(XDR *xdrs, long int *lp)
{

	if (xdrs->x_handy < sizeof(int32_t))
		return (FALSE);
	xdrs->x_handy -= sizeof(int32_t);
	*(int32_t *)xdrs->x_private = htonl((u_int32_t)*lp);
	xdrs->x_private += sizeof(int32_t);
	return (TRUE);
}

static bool_t
xdrmem_getlong_unaligned(XDR *xdrs, long int *lp)
{
	int32_t l;

	if (xdrs->x_handy < sizeof(int32_t))
		return (FALSE);
	xdrs->x_handy -= sizeof(int32_t);
	memcpy(&l, xdrs->x_private, sizeof(int32_t));
	*lp = ntohl(l);
	xdrs->x_private += sizeof(int32_t);
	return (TRUE);
}

static bool_t
xdrmem_putlong_unaligned(XDR *xdrs, long int *lp)
{
	int32_t l;

	if (xdrs->x_handy < sizeof(int32_t))
		return (FALSE);
	xdrs->x_handy -= sizeof(int32_t);
	l = htonl((u_int32_t)*lp);
	memcpy(xdrs->x_private, &l, sizeof(int32_t));
	xdrs->x_private += sizeof(int32_t);
	return (TRUE);
}

static bool_t
xdrmem_getbytes(XDR *xdrs, caddr_t addr, u_int len)
{

	if (xdrs->x_handy < len)
		return (FALSE);
	xdrs->x_handy -= len;
	memcpy(addr, xdrs->x_private, len);
	xdrs->x_private += len;
	return (TRUE);
}

static bool_t
xdrmem_putbytes(XDR *xdrs, caddr_t addr, u_int len)
{

	if (xdrs->x_handy < len)
		return (FALSE);
	xdrs->x_handy -= len;
	memcpy(xdrs->x_private, addr, len);
	xdrs->x_private += len;
	return (TRUE);
}

static u_int
xdrmem_getpos(XDR *xdrs)
{

	/* XXX w/64-bit pointers, u_int not enough! */
	return ((u_long)xdrs->x_private - (u_long)xdrs->x_base);
}

static bool_t
xdrmem_setpos(XDR *xdrs, u_int pos)
{
	caddr_t newaddr = xdrs->x_base + pos;
	caddr_t lastaddr = xdrs->x_private + xdrs->x_handy;

	if (newaddr > lastaddr)
		return (FALSE);
	xdrs->x_private = newaddr;
	xdrs->x_handy = (u_int)(lastaddr - newaddr);	/* XXX w/64-bit pointers, u_int not enough! */
	return (TRUE);
}

static int32_t *
xdrmem_inline_aligned(XDR *xdrs, u_int len)
{
	int32_t *buf = 0;

	if (xdrs->x_handy >= len) {
		xdrs->x_handy -= len;
		buf = (int32_t *)xdrs->x_private;
		xdrs->x_private += len;
	}
	return (buf);
}

static int32_t *
xdrmem_inline_unaligned(XDR *xdrs, u_int len)
{

	return (0);
}
