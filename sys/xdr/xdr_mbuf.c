/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Isilon Inc http://www.isilon.com/
 * Authors: Doug Rabson <dfr@rabson.org>
 * Developed with Red Inc: Alfred Perlstein <alfred@freebsd.org>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>

#include <rpc/types.h>
#include <rpc/xdr.h>

static void xdrmbuf_destroy(XDR *);
static bool_t xdrmbuf_getlong(XDR *, long *);
static bool_t xdrmbuf_putlong(XDR *, const long *);
static bool_t xdrmbuf_getbytes(XDR *, char *, u_int);
static bool_t xdrmbuf_putbytes(XDR *, const char *, u_int);
/* XXX: w/64-bit pointers, u_int not enough! */
static u_int xdrmbuf_getpos(XDR *);
static bool_t xdrmbuf_setpos(XDR *, u_int);
static int32_t *xdrmbuf_inline(XDR *, u_int);

static const struct	xdr_ops xdrmbuf_ops = {
	xdrmbuf_getlong,
	xdrmbuf_putlong,
	xdrmbuf_getbytes,
	xdrmbuf_putbytes,
	xdrmbuf_getpos,
	xdrmbuf_setpos,
	xdrmbuf_inline,
	xdrmbuf_destroy
};

/*
 * The procedure xdrmbuf_create initializes a stream descriptor for a
 * mbuf.
 */
void
xdrmbuf_create(XDR *xdrs, struct mbuf *m, enum xdr_op op)
{

	KASSERT(m != NULL, ("xdrmbuf_create with NULL mbuf chain"));
	xdrs->x_op = op;
	xdrs->x_ops = &xdrmbuf_ops;
	xdrs->x_base = (char *) m;
	if (op == XDR_ENCODE) {
		m = m_last(m);
		xdrs->x_private = m;
		xdrs->x_handy = m->m_len;
	} else {
		xdrs->x_private = m;
		xdrs->x_handy = 0;
	}
}

void
xdrmbuf_append(XDR *xdrs, struct mbuf *madd)
{
	struct mbuf *m;

	KASSERT(xdrs->x_ops == &xdrmbuf_ops && xdrs->x_op == XDR_ENCODE,
	    ("xdrmbuf_append: invalid XDR stream"));

	if (m_length(madd, NULL) == 0) {
		m_freem(madd);
		return;
	}
	
	m = (struct mbuf *) xdrs->x_private;
	m->m_next = madd;

	m = m_last(madd);
	xdrs->x_private = m;
	xdrs->x_handy = m->m_len;
}

struct mbuf *
xdrmbuf_getall(XDR *xdrs)
{
	struct mbuf *m0, *m;

	KASSERT(xdrs->x_ops == &xdrmbuf_ops && xdrs->x_op == XDR_DECODE,
	    ("xdrmbuf_append: invalid XDR stream"));

	m0 = (struct mbuf *) xdrs->x_base;
	m = (struct mbuf *) xdrs->x_private;
	if (m0 != m) {
		while (m0->m_next != m)
			m0 = m0->m_next;
		m0->m_next = NULL;
		xdrs->x_private = NULL;
	} else {
		xdrs->x_base = NULL;
		xdrs->x_private = NULL;
	}

	if (m)
		m_adj(m, xdrs->x_handy);
	else
		m = m_get(M_WAITOK, MT_DATA);
	return (m);
}

static void
xdrmbuf_destroy(XDR *xdrs)
{

	if (xdrs->x_op == XDR_DECODE && xdrs->x_base) {
		m_freem((struct mbuf *) xdrs->x_base);
		xdrs->x_base = NULL;
		xdrs->x_private = NULL;
	}
}

static bool_t
xdrmbuf_getlong(XDR *xdrs, long *lp)
{
	int32_t *p;
	int32_t t;

	p = xdrmbuf_inline(xdrs, sizeof(int32_t));
	if (p) {
		t = *p;
	} else {
		xdrmbuf_getbytes(xdrs, (char *) &t, sizeof(int32_t));
	}

	*lp = ntohl(t);
	return (TRUE);
}

static bool_t
xdrmbuf_putlong(xdrs, lp)
	XDR *xdrs;
	const long *lp;
{
	int32_t *p;
	int32_t t = htonl(*lp);

	p = xdrmbuf_inline(xdrs, sizeof(int32_t));
	if (p) {
		*p = t;
		return (TRUE);
	} else {
		return (xdrmbuf_putbytes(xdrs, (char *) &t, sizeof(int32_t)));
	}
}

static bool_t
xdrmbuf_getbytes(XDR *xdrs, char *addr, u_int len)
{
	struct mbuf *m = (struct mbuf *) xdrs->x_private;
	size_t sz;

	while (len > 0) {
		/*
		 * Make sure we haven't hit the end.
		 */
		if (!m) {
			return (FALSE);
		}

		/*
		 * See how much we can get from this mbuf.
		 */
		sz = m->m_len - xdrs->x_handy;
		if (sz > len)
			sz = len;
		bcopy(mtod(m, const char *) + xdrs->x_handy, addr, sz);

		addr += sz;
		xdrs->x_handy += sz;
		len -= sz;

		if (xdrs->x_handy == m->m_len) {
			m = m->m_next;
			xdrs->x_private = (void *) m;
			xdrs->x_handy = 0;
		}
	}
	
	return (TRUE);
}

static bool_t
xdrmbuf_putbytes(XDR *xdrs, const char *addr, u_int len)
{
	struct mbuf *m = (struct mbuf *) xdrs->x_private;
	struct mbuf *n;
	size_t sz;

	while (len > 0) {
		sz = M_TRAILINGSPACE(m) + (m->m_len - xdrs->x_handy);
		if (sz > len)
			sz = len;
		bcopy(addr, mtod(m, char *) + xdrs->x_handy, sz);
		addr += sz;
		xdrs->x_handy += sz;
		if (xdrs->x_handy > m->m_len)
			m->m_len = xdrs->x_handy;
		len -= sz;

		if (xdrs->x_handy == m->m_len && M_TRAILINGSPACE(m) == 0) {
			if (!m->m_next) {
				if (m->m_flags & M_EXT)
					n = m_getcl(M_WAITOK, m->m_type, 0);
				else
					n = m_get(M_WAITOK, m->m_type);
				m->m_next = n;
			}
			m = m->m_next;
			xdrs->x_private = (void *) m;
			xdrs->x_handy = 0;
		}
	}
	
	return (TRUE);
}

static u_int
xdrmbuf_getpos(XDR *xdrs)
{
	struct mbuf *m0 = (struct mbuf *) xdrs->x_base;
	struct mbuf *m = (struct mbuf *) xdrs->x_private;
	u_int pos = 0;

	while (m0 && m0 != m) {
		pos += m0->m_len;
		m0 = m0->m_next;
	}
	KASSERT(m0, ("Corrupted mbuf chain"));

	return (pos + xdrs->x_handy);
}

static bool_t
xdrmbuf_setpos(XDR *xdrs, u_int pos)
{
	struct mbuf *m = (struct mbuf *) xdrs->x_base;

	while (m && pos > m->m_len) {
		pos -= m->m_len;
		m = m->m_next;
	}
	KASSERT(m, ("Corrupted mbuf chain"));

	xdrs->x_private = (void *) m;
	xdrs->x_handy = pos;

	return (TRUE);
}

static int32_t *
xdrmbuf_inline(XDR *xdrs, u_int len)
{
	struct mbuf *m = (struct mbuf *) xdrs->x_private;
	size_t available;
	char *p;

	if (!m)
		return (0);
	if (xdrs->x_op == XDR_ENCODE) {
		available = M_TRAILINGSPACE(m) + (m->m_len - xdrs->x_handy);
	} else {
		available = m->m_len - xdrs->x_handy;
	}

	if (available >= len) {
		p = mtod(m, char *) + xdrs->x_handy;
		if (((uintptr_t) p) & (sizeof(int32_t) - 1))
			return (0);
		xdrs->x_handy += len;
		if (xdrs->x_handy > m->m_len)
			m->m_len = xdrs->x_handy;
		return ((int32_t *) p);
	}

	return (0);
}
