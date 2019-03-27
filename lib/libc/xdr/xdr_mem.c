/*	$NetBSD: xdr_mem.c,v 1.15 2000/01/22 22:19:18 mycroft Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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

#if defined(LIBC_SCCS) && !defined(lint)
static char *sccsid2 = "@(#)xdr_mem.c 1.19 87/08/11 Copyr 1984 Sun Micro";
static char *sccsid = "@(#)xdr_mem.c	2.1 88/07/29 4.0 RPCSRC";
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * xdr_mem.h, XDR implementation using memory buffers.
 *
 * If you have some data to be interpreted as external data representation
 * or to be converted to external data representation in a memory buffer,
 * then this is the package for you.
 *
 */

#include "namespace.h"
#include <sys/types.h>

#include <netinet/in.h>

#include <string.h>

#include <rpc/types.h>
#include <rpc/xdr.h>
#include "un-namespace.h"

static void xdrmem_destroy(XDR *);
static bool_t xdrmem_getlong_aligned(XDR *, long *);
static bool_t xdrmem_putlong_aligned(XDR *, const long *);
static bool_t xdrmem_getlong_unaligned(XDR *, long *);
static bool_t xdrmem_putlong_unaligned(XDR *, const long *);
static bool_t xdrmem_getbytes(XDR *, char *, u_int);
static bool_t xdrmem_putbytes(XDR *, const char *, u_int);
/* XXX: w/64-bit pointers, u_int not enough! */
static u_int xdrmem_getpos(XDR *);
static bool_t xdrmem_setpos(XDR *, u_int);
static int32_t *xdrmem_inline_aligned(XDR *, u_int);
static int32_t *xdrmem_inline_unaligned(XDR *, u_int);

static const struct	xdr_ops xdrmem_ops_aligned = {
	xdrmem_getlong_aligned,
	xdrmem_putlong_aligned,
	xdrmem_getbytes,
	xdrmem_putbytes,
	xdrmem_getpos,
	xdrmem_setpos,
	xdrmem_inline_aligned,
	xdrmem_destroy
};

static const struct	xdr_ops xdrmem_ops_unaligned = {
	xdrmem_getlong_unaligned,
	xdrmem_putlong_unaligned,
	xdrmem_getbytes,
	xdrmem_putbytes,
	xdrmem_getpos,
	xdrmem_setpos,
	xdrmem_inline_unaligned,
	xdrmem_destroy
};

/*
 * The procedure xdrmem_create initializes a stream descriptor for a
 * memory buffer.
 */
void
xdrmem_create(XDR *xdrs, char *addr, u_int size, enum xdr_op op)
{

	xdrs->x_op = op;
	xdrs->x_ops = ((unsigned long)addr & (sizeof(int32_t) - 1))
	    ? &xdrmem_ops_unaligned : &xdrmem_ops_aligned;
	xdrs->x_private = xdrs->x_base = addr;
	xdrs->x_handy = size;
}

/*ARGSUSED*/
static void
xdrmem_destroy(XDR *xdrs)
{

}

static bool_t
xdrmem_getlong_aligned(XDR *xdrs, long *lp)
{

	if (xdrs->x_handy < sizeof(int32_t))
		return (FALSE);
	xdrs->x_handy -= sizeof(int32_t);
	*lp = ntohl(*(u_int32_t *)xdrs->x_private);
	xdrs->x_private = (char *)xdrs->x_private + sizeof(int32_t);
	return (TRUE);
}

static bool_t
xdrmem_putlong_aligned(XDR *xdrs, const long *lp)
{

	if (xdrs->x_handy < sizeof(int32_t))
		return (FALSE);
	xdrs->x_handy -= sizeof(int32_t);
	*(u_int32_t *)xdrs->x_private = htonl((u_int32_t)*lp);
	xdrs->x_private = (char *)xdrs->x_private + sizeof(int32_t);
	return (TRUE);
}

static bool_t
xdrmem_getlong_unaligned(XDR *xdrs, long *lp)
{
	u_int32_t l;

	if (xdrs->x_handy < sizeof(int32_t))
		return (FALSE);
	xdrs->x_handy -= sizeof(int32_t);
	memmove(&l, xdrs->x_private, sizeof(int32_t));
	*lp = ntohl(l);
	xdrs->x_private = (char *)xdrs->x_private + sizeof(int32_t);
	return (TRUE);
}

static bool_t
xdrmem_putlong_unaligned(XDR *xdrs, const long *lp)
{
	u_int32_t l;

	if (xdrs->x_handy < sizeof(int32_t))
		return (FALSE);
	xdrs->x_handy -= sizeof(int32_t);
	l = htonl((u_int32_t)*lp);
	memmove(xdrs->x_private, &l, sizeof(int32_t));
	xdrs->x_private = (char *)xdrs->x_private + sizeof(int32_t);
	return (TRUE);
}

static bool_t
xdrmem_getbytes(XDR *xdrs, char *addr, u_int len)
{

	if (xdrs->x_handy < len)
		return (FALSE);
	xdrs->x_handy -= len;
	memmove(addr, xdrs->x_private, len);
	xdrs->x_private = (char *)xdrs->x_private + len;
	return (TRUE);
}

static bool_t
xdrmem_putbytes(XDR *xdrs, const char *addr, u_int len)
{

	if (xdrs->x_handy < len)
		return (FALSE);
	xdrs->x_handy -= len;
	memmove(xdrs->x_private, addr, len);
	xdrs->x_private = (char *)xdrs->x_private + len;
	return (TRUE);
}

static u_int
xdrmem_getpos(XDR *xdrs)
{

	/* XXX w/64-bit pointers, u_int not enough! */
	return (u_int)((u_long)xdrs->x_private - (u_long)xdrs->x_base);
}

static bool_t
xdrmem_setpos(XDR *xdrs, u_int pos)
{
	char *newaddr = xdrs->x_base + pos;
	char *lastaddr = (char *)xdrs->x_private + xdrs->x_handy;

	if (newaddr > lastaddr)
		return (FALSE);
	xdrs->x_private = newaddr;
	xdrs->x_handy = (u_int)(lastaddr - newaddr); /* XXX sizeof(u_int) <? sizeof(ptrdiff_t) */
	return (TRUE);
}

static int32_t *
xdrmem_inline_aligned(XDR *xdrs, u_int len)
{
	int32_t *buf = NULL;

	if (xdrs->x_handy >= len) {
		xdrs->x_handy -= len;
		buf = (int32_t *)xdrs->x_private;
		xdrs->x_private = (char *)xdrs->x_private + len;
	}
	return (buf);
}

/* ARGSUSED */
static int32_t *
xdrmem_inline_unaligned(XDR *xdrs, u_int len)
{

	return (0);
}
