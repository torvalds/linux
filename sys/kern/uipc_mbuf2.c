/*	$OpenBSD: uipc_mbuf2.c,v 1.50 2025/06/25 20:26:32 miod Exp $	*/
/*	$KAME: uipc_mbuf2.c,v 1.29 2001/02/14 13:42:10 itojun Exp $	*/
/*	$NetBSD: uipc_mbuf.c,v 1.40 1999/04/01 00:23:25 thorpej Exp $	*/

/*
 * Copyright (C) 1999 WIDE Project.
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
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Copyright (c) 1982, 1986, 1988, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)uipc_mbuf.c	8.4 (Berkeley) 2/14/95
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/pool.h>
#include <sys/percpu.h>
#include <sys/mbuf.h>

extern struct pool mtagpool;

/* can't call it m_dup(), as freebsd[34] uses m_dup() with different arg */
static struct mbuf *m_dup1(struct mbuf *, int, int, int);

/*
 * ensure that [off, off + len] is contiguous on the mbuf chain "m".
 * packet chain before "off" is kept untouched.
 * if offp == NULL, the target will start at <retval, 0> on resulting chain.
 * if offp != NULL, the target will start at <retval, *offp> on resulting chain.
 *
 * on error return (NULL return value), original "m" will be freed.
 *
 * XXX m_trailingspace/m_leadingspace on shared cluster (sharedcluster)
 */
struct mbuf *
m_pulldown(struct mbuf *m, int off, int len, int *offp)
{
	struct mbuf *n, *o;
	int hlen, tlen, olen;
	int sharedcluster;

	/* check invalid arguments. */
	if (m == NULL)
		panic("m == NULL in m_pulldown()");

	if ((n = m_getptr(m, off, &off)) == NULL) {
		m_freem(m);
		return (NULL);	/* mbuf chain too short */
	}

	sharedcluster = M_READONLY(n);

	/*
	 * the target data is on <n, off>.
	 * if we got enough data on the mbuf "n", we're done.
	 */
	if ((off == 0 || offp) && len <= n->m_len - off && !sharedcluster)
		goto ok;

	/*
	 * when len <= n->m_len - off and off != 0, it is a special case.
	 * len bytes from <n, off> sits in single mbuf, but the caller does
	 * not like the starting position (off).
	 * chop the current mbuf into two pieces, set off to 0.
	 */
	if (len <= n->m_len - off) {
		struct mbuf *mlast;

		mbstat_inc(mbs_pulldown_alloc);
		o = m_dup1(n, off, n->m_len - off, M_DONTWAIT);
		if (o == NULL) {
			m_freem(m);
			return (NULL);	/* ENOBUFS */
		}
		for (mlast = o; mlast->m_next != NULL; mlast = mlast->m_next)
			;
		n->m_len = off;
		mlast->m_next = n->m_next;
		n->m_next = o;
		n = o;
		off = 0;
		goto ok;
	}

	/*
	 * we need to take hlen from <n, off> and tlen from <n->m_next, 0>,
	 * and construct contiguous mbuf with m_len == len.
	 * note that hlen + tlen == len, and tlen > 0.
	 */
	hlen = n->m_len - off;
	tlen = len - hlen;

	/*
	 * ensure that we have enough trailing data on mbuf chain.
	 * if not, we can do nothing about the chain.
	 */
	olen = 0;
	for (o = n->m_next; o != NULL; o = o->m_next)
		olen += o->m_len;
	if (hlen + olen < len) {
		m_freem(m);
		return (NULL);	/* mbuf chain too short */
	}

	/*
	 * easy cases first.
	 * we need to use m_copydata() to get data from <n->m_next, 0>.
	 */
	if ((off == 0 || offp) && m_trailingspace(n) >= tlen &&
	    !sharedcluster) {
		mbstat_inc(mbs_pulldown_copy);
		m_copydata(n->m_next, 0, tlen, mtod(n, caddr_t) + n->m_len);
		n->m_len += tlen;
		m_adj(n->m_next, tlen);
		goto ok;
	}
	if ((off == 0 || offp) && m_leadingspace(n->m_next) >= hlen &&
	    !sharedcluster && n->m_next->m_len >= tlen) {
		n->m_next->m_data -= hlen;
		n->m_next->m_len += hlen;
		mbstat_inc(mbs_pulldown_copy);
		memcpy(mtod(n->m_next, caddr_t), mtod(n, caddr_t) + off, hlen);
		n->m_len -= hlen;
		n = n->m_next;
		off = 0;
		goto ok;
	}

	/*
	 * now, we need to do the hard way.  don't m_copym as there's no room
	 * on both ends.
	 */
	if (len > MAXMCLBYTES) {
		m_freem(m);
		return (NULL);
	}
	mbstat_inc(mbs_pulldown_alloc);
	MGET(o, M_DONTWAIT, m->m_type);
	if (o && len > MLEN) {
		MCLGETL(o, M_DONTWAIT, len);
		if ((o->m_flags & M_EXT) == 0) {
			m_free(o);
			o = NULL;
		}
	}
	if (!o) {
		m_freem(m);
		return (NULL);	/* ENOBUFS */
	}
	/* get hlen from <n, off> into <o, 0> */
	o->m_len = hlen;
	memcpy(mtod(o, caddr_t), mtod(n, caddr_t) + off, hlen);
	n->m_len -= hlen;
	/* get tlen from <n->m_next, 0> into <o, hlen> */
	m_copydata(n->m_next, 0, tlen, mtod(o, caddr_t) + o->m_len);
	o->m_len += tlen;
	m_adj(n->m_next, tlen);
	o->m_next = n->m_next;
	n->m_next = o;
	n = o;
	off = 0;

ok:
	KASSERT(n->m_len >= off + len);
	if (offp)
		*offp = off;
	return (n);
}

static struct mbuf *
m_dup1(struct mbuf *m, int off, int len, int wait)
{
	struct mbuf *n;
	int l;

	if (len > MAXMCLBYTES)
		return (NULL);
	if (off == 0 && (m->m_flags & M_PKTHDR) != 0) {
		MGETHDR(n, wait, m->m_type);
		if (n == NULL)
			return (NULL);
		if (m_dup_pkthdr(n, m, wait)) {
			m_free(n);
			return (NULL);
		}
		l = MHLEN;
	} else {
		MGET(n, wait, m->m_type);
		l = MLEN;
	}
	if (n && len > l) {
		MCLGETL(n, wait, len);
		if ((n->m_flags & M_EXT) == 0) {
			m_free(n);
			n = NULL;
		}
	}
	if (!n)
		return (NULL);

	m_copydata(m, off, len, mtod(n, caddr_t));
	n->m_len = len;

	return (n);
}

/* Get a packet tag structure along with specified data following. */
struct m_tag *
m_tag_get(int type, int len, int wait)
{
	struct m_tag *t;

	if (len < 0)
		return (NULL);
	if (len > PACKET_TAG_MAXSIZE)
		panic("requested tag size for pool %#x is too big", type);
	t = pool_get(&mtagpool, wait == M_WAITOK ? PR_WAITOK : PR_NOWAIT);
	if (t == NULL)
		return (NULL);
	t->m_tag_id = type;
	t->m_tag_len = len;
	return (t);
}

/* Prepend a packet tag. */
void
m_tag_prepend(struct mbuf *m, struct m_tag *t)
{
	SLIST_INSERT_HEAD(&m->m_pkthdr.ph_tags, t, m_tag_link);
	m->m_pkthdr.ph_tagsset |= t->m_tag_id;
}

/* Unlink and free a packet tag. */
void
m_tag_delete(struct mbuf *m, struct m_tag *t)
{
	u_int32_t	 ph_tagsset = 0;
	struct m_tag	*p;

	SLIST_REMOVE(&m->m_pkthdr.ph_tags, t, m_tag, m_tag_link);
	pool_put(&mtagpool, t);

	SLIST_FOREACH(p, &m->m_pkthdr.ph_tags, m_tag_link)
		ph_tagsset |= p->m_tag_id;
	m->m_pkthdr.ph_tagsset = ph_tagsset;

}

/* Unlink and free a packet tag chain. */
void
m_tag_delete_chain(struct mbuf *m)
{
	struct m_tag *p;

	while ((p = SLIST_FIRST(&m->m_pkthdr.ph_tags)) != NULL) {
		SLIST_REMOVE_HEAD(&m->m_pkthdr.ph_tags, m_tag_link);
		pool_put(&mtagpool, p);
	}
	m->m_pkthdr.ph_tagsset = 0;
}

/* Find a tag, starting from a given position. */
struct m_tag *
m_tag_find(struct mbuf *m, int type, struct m_tag *t)
{
	struct m_tag *p;

	if (!(m->m_pkthdr.ph_tagsset & type))
		return (NULL);

	if (t == NULL)
		p = SLIST_FIRST(&m->m_pkthdr.ph_tags);
	else
		p = SLIST_NEXT(t, m_tag_link);
	while (p != NULL) {
		if (p->m_tag_id == type)
			return (p);
		p = SLIST_NEXT(p, m_tag_link);
	}
	return (NULL);
}

/* Copy a single tag. */
struct m_tag *
m_tag_copy(struct m_tag *t, int wait)
{
	struct m_tag *p;

	p = m_tag_get(t->m_tag_id, t->m_tag_len, wait);
	if (p == NULL)
		return (NULL);
	memcpy(p + 1, t + 1, t->m_tag_len); /* Copy the data */
	return (p);
}

/*
 * Copy two tag chains. The destination mbuf (to) loses any attached
 * tags even if the operation fails. This should not be a problem, as
 * m_tag_copy_chain() is typically called with a newly-allocated
 * destination mbuf.
 */
int
m_tag_copy_chain(struct mbuf *to, struct mbuf *from, int wait)
{
	struct m_tag *p, *t, *tprev = NULL;

	m_tag_delete_chain(to);
	SLIST_FOREACH(p, &from->m_pkthdr.ph_tags, m_tag_link) {
		t = m_tag_copy(p, wait);
		if (t == NULL) {
			m_tag_delete_chain(to);
			return (ENOBUFS);
		}
		if (tprev == NULL)
			SLIST_INSERT_HEAD(&to->m_pkthdr.ph_tags, t, m_tag_link);
		else
			SLIST_INSERT_AFTER(tprev, t, m_tag_link);
		tprev = t;
		to->m_pkthdr.ph_tagsset |= t->m_tag_id;
	}
	return (0);
}

/* Get first tag in chain. */
struct m_tag *
m_tag_first(struct mbuf *m)
{
	return (SLIST_FIRST(&m->m_pkthdr.ph_tags));
}

/* Get next tag in chain. */
struct m_tag *
m_tag_next(struct mbuf *m, struct m_tag *t)
{
	return (SLIST_NEXT(t, m_tag_link));
}
