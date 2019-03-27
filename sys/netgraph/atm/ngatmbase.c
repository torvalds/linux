/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 * 	All rights reserved.
 *
 * Author: Hartmut Brandt <harti@freebsd.org>
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
 *
 * In-kernel UNI stack message functions.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/mbuf.h>
#include <machine/stdarg.h>
#include <netnatm/unimsg.h>
#include <netgraph/atm/ngatmbase.h>

#define NGATMBASE_VERSION	1

static int ngatm_handler(module_t, int, void *);

static moduledata_t ngatm_data = {
	"ngatmbase",
	ngatm_handler,
	0
};

MODULE_VERSION(ngatmbase, NGATMBASE_VERSION);
DECLARE_MODULE(ngatmbase, ngatm_data, SI_SUB_EXEC, SI_ORDER_ANY);

/*********************************************************************/
/*
 * UNI Stack message handling functions
 */
static MALLOC_DEFINE(M_UNIMSG, "unimsg", "uni message buffers");
static MALLOC_DEFINE(M_UNIMSGHDR, "unimsghdr", "uni message headers");

#define EXTRA	128

/* mutex to protect the free list (and the used list if debugging) */
static struct mtx ngatm_unilist_mtx;

/*
 * Initialize UNI message subsystem
 */
static void
uni_msg_init(void)
{
	mtx_init(&ngatm_unilist_mtx, "netgraph UNI msg header lists", NULL,
	    MTX_DEF);
}

/*
 * Ensure, that the message can be extended by at least s bytes.
 * Re-allocate the message (not the header). If that failes,
 * free the entire message and return ENOMEM. Free space at the start of
 * the message is retained.
 */
int
uni_msg_extend(struct uni_msg *m, size_t s)
{
	u_char *b;
	size_t len, lead;

	lead = uni_msg_leading(m);
	len = uni_msg_len(m);
	s += lead + len + EXTRA;
	if ((b = malloc(s, M_UNIMSG, M_NOWAIT)) == NULL) {
		uni_msg_destroy(m);
		return (ENOMEM);
	}

	bcopy(m->b_rptr, b + lead, len);
	free(m->b_buf, M_UNIMSG);

	m->b_buf = b;
	m->b_rptr = m->b_buf + lead;
	m->b_wptr = m->b_rptr + len;
	m->b_lim = m->b_buf + s;

	return (0);
}

/*
 * Append a buffer to the message, making space if needed.
 * If reallocation files, ENOMEM is returned and the message freed.
 */
int
uni_msg_append(struct uni_msg *m, void *buf, size_t size)
{
	int error;

	if ((error = uni_msg_ensure(m, size)))
		return (error);
	bcopy(buf, m->b_wptr, size);
	m->b_wptr += size;

	return (0);
}

/*
 * Pack/unpack data from/into mbufs. Assume, that the (optional) header
 * fits into the first mbuf, ie. hdrlen < MHLEN. Note, that the message
 * can be NULL, but hdrlen should not be 0 in this case.
 */
struct mbuf *
uni_msg_pack_mbuf(struct uni_msg *msg, void *hdr, size_t hdrlen)
{
	struct mbuf *m, *m0, *last;
	size_t n;

	MGETHDR(m0, M_NOWAIT, MT_DATA);
	if (m0 == NULL)
		return (NULL);

	KASSERT(hdrlen <= MHLEN, ("uni_msg_pack_mbuf: hdrlen > MHLEN"));

	if (hdrlen != 0) {
		bcopy(hdr, m0->m_data, hdrlen);
		m0->m_len = hdrlen;
		m0->m_pkthdr.len = hdrlen;

	} else {
		if ((n = uni_msg_len(msg)) > MHLEN) {
			if (!(MCLGET(m0, M_NOWAIT)))
				goto drop;
			if (n > MCLBYTES)
				n = MCLBYTES;
		}

		bcopy(msg->b_rptr, m0->m_data, n);
		msg->b_rptr += n;
		m0->m_len = n;
		m0->m_pkthdr.len = n;
	}

	last = m0;
	while (msg != NULL && (n = uni_msg_len(msg)) != 0) {
		MGET(m, M_NOWAIT, MT_DATA);
		if (m == NULL)
			goto drop;
		last->m_next = m;
		last = m;

		if (n > MLEN) {
			if (!(MCLGET(m, M_NOWAIT)))
				goto drop;
			if (n > MCLBYTES)
				n = MCLBYTES;
		}

		bcopy(msg->b_rptr, m->m_data, n);
		msg->b_rptr += n;
		m->m_len = n;
		m0->m_pkthdr.len += n;
	}

	return (m0);

  drop:
	m_freem(m0);
	return (NULL);
}

#ifdef NGATM_DEBUG

/*
 * Prepend a debugging header to each message
 */
struct ngatm_msg {
	LIST_ENTRY(ngatm_msg) link;
	const char *file;
	int line;
	struct uni_msg msg;
};

/*
 * These are the lists of free and used message headers.
 */
static LIST_HEAD(, ngatm_msg) ngatm_freeuni =
    LIST_HEAD_INITIALIZER(ngatm_freeuni);
static LIST_HEAD(, ngatm_msg) ngatm_useduni =
    LIST_HEAD_INITIALIZER(ngatm_useduni);

/*
 * Clean-up UNI message subsystem
 */
static void
uni_msg_fini(void)
{
	struct ngatm_msg *h;

	/* free all free message headers */
	while ((h = LIST_FIRST(&ngatm_freeuni)) != NULL) {
		LIST_REMOVE(h, link);
		free(h, M_UNIMSGHDR);
	}

	/* forget about still used messages */
	LIST_FOREACH(h, &ngatm_useduni, link)
		printf("unimsg header in use: %p (%s, %d)\n",
		    &h->msg, h->file, h->line);

	mtx_destroy(&ngatm_unilist_mtx);
}

/*
 * Allocate a message, that can hold at least s bytes.
 */
struct uni_msg *
_uni_msg_alloc(size_t s, const char *file, int line)
{
	struct ngatm_msg *m;

	mtx_lock(&ngatm_unilist_mtx);
	if ((m = LIST_FIRST(&ngatm_freeuni)) != NULL)
		LIST_REMOVE(m, link);
	mtx_unlock(&ngatm_unilist_mtx);

	if (m == NULL &&
	    (m = malloc(sizeof(*m), M_UNIMSGHDR, M_NOWAIT)) == NULL)
		return (NULL);

	s += EXTRA;
	if((m->msg.b_buf = malloc(s, M_UNIMSG, M_NOWAIT | M_ZERO)) == NULL) {
		mtx_lock(&ngatm_unilist_mtx);
		LIST_INSERT_HEAD(&ngatm_freeuni, m, link);
		mtx_unlock(&ngatm_unilist_mtx);
		return (NULL);
	}
	m->msg.b_rptr = m->msg.b_wptr = m->msg.b_buf;
	m->msg.b_lim = m->msg.b_buf + s;
	m->file = file;
	m->line = line;

	mtx_lock(&ngatm_unilist_mtx);
	LIST_INSERT_HEAD(&ngatm_useduni, m, link);
	mtx_unlock(&ngatm_unilist_mtx);
	return (&m->msg);
}

/*
 * Destroy a UNI message.
 * The header is inserted into the free header list.
 */
void
_uni_msg_destroy(struct uni_msg *m, const char *file, int line)
{
	struct ngatm_msg *h, *d;

	d = (struct ngatm_msg *)((char *)m - offsetof(struct ngatm_msg, msg));

	mtx_lock(&ngatm_unilist_mtx);
	LIST_FOREACH(h, &ngatm_useduni, link)
		if (h == d)
			break;

	if (h == NULL) {
		/*
		 * Not on used list. Ups.
		 */
		LIST_FOREACH(h, &ngatm_freeuni, link)
			if (h == d)
				break;

		if (h == NULL)
			printf("uni_msg %p was never allocated; found "
			    "in %s:%u\n", m, file, line);
		else
			printf("uni_msg %p was already destroyed in %s,%d; "
			    "found in %s:%u\n", m, h->file, h->line,
			    file, line);
	} else {
		free(m->b_buf, M_UNIMSG);

		LIST_REMOVE(d, link);
		LIST_INSERT_HEAD(&ngatm_freeuni, d, link);

		d->file = file;
		d->line = line;
	}

	mtx_unlock(&ngatm_unilist_mtx);
}

#else /* !NGATM_DEBUG */

/*
 * This assumes, that sizeof(struct uni_msg) >= sizeof(struct ngatm_msg)
 * and the alignment requirements of are the same.
 */
struct ngatm_msg {
	LIST_ENTRY(ngatm_msg) link;
};

/* Lists of free message headers.  */
static LIST_HEAD(, ngatm_msg) ngatm_freeuni =
    LIST_HEAD_INITIALIZER(ngatm_freeuni);

/*
 * Clean-up UNI message subsystem
 */
static void
uni_msg_fini(void)
{
	struct ngatm_msg *h;

	/* free all free message headers */
	while ((h = LIST_FIRST(&ngatm_freeuni)) != NULL) {
		LIST_REMOVE(h, link);
		free(h, M_UNIMSGHDR);
	}

	mtx_destroy(&ngatm_unilist_mtx);
}

/*
 * Allocate a message, that can hold at least s bytes.
 */
struct uni_msg *
uni_msg_alloc(size_t s)
{
	struct ngatm_msg *a;
	struct uni_msg *m;

	mtx_lock(&ngatm_unilist_mtx);
	if ((a = LIST_FIRST(&ngatm_freeuni)) != NULL)
		LIST_REMOVE(a, link);
	mtx_unlock(&ngatm_unilist_mtx);

	if (a == NULL) {
		if ((m = malloc(sizeof(*m), M_UNIMSGHDR, M_NOWAIT)) == NULL)
			return (NULL);
		a = (struct ngatm_msg *)m;
	} else
		m = (struct uni_msg *)a;

	s += EXTRA;
	if((m->b_buf = malloc(s, M_UNIMSG, M_NOWAIT | M_ZERO)) == NULL) {
		mtx_lock(&ngatm_unilist_mtx);
		LIST_INSERT_HEAD(&ngatm_freeuni, a, link);
		mtx_unlock(&ngatm_unilist_mtx);
		return (NULL);
	}
	m->b_rptr = m->b_wptr = m->b_buf;
	m->b_lim = m->b_buf + s;

	return (m);
}

/*
 * Destroy a UNI message.
 * The header is inserted into the free header list.
 */
void
uni_msg_destroy(struct uni_msg *m)
{
	struct ngatm_msg *a;

	a = (struct ngatm_msg *)m;

	free(m->b_buf, M_UNIMSG);

	mtx_lock(&ngatm_unilist_mtx);
	LIST_INSERT_HEAD(&ngatm_freeuni, a, link);
	mtx_unlock(&ngatm_unilist_mtx);
}

#endif

/*
 * Build a message from a number of buffers. Arguments are pairs
 * of (void *, size_t) ending with a NULL pointer.
 */
#ifdef NGATM_DEBUG
struct uni_msg *
_uni_msg_build(const char *file, int line, void *ptr, ...)
#else
struct uni_msg *
uni_msg_build(void *ptr, ...)
#endif
{
	va_list ap;
	struct uni_msg *m;
	size_t len, n;
	void *p1;

	len = 0;
	va_start(ap, ptr);
	p1 = ptr;
	while (p1 != NULL) {
		n = va_arg(ap, size_t);
		len += n;
		p1 = va_arg(ap, void *);
	}
	va_end(ap);

#ifdef NGATM_DEBUG
	if ((m = _uni_msg_alloc(len, file, line)) == NULL)
#else
	if ((m = uni_msg_alloc(len)) == NULL)
#endif
		return (NULL);

	va_start(ap, ptr);
	p1 = ptr;
	while (p1 != NULL) {
		n = va_arg(ap, size_t);
		bcopy(p1, m->b_wptr, n);
		m->b_wptr += n;
		p1 = va_arg(ap, void *);
	}
	va_end(ap);

	return (m);
}

/*
 * Unpack an mbuf chain into a uni_msg buffer.
 */
#ifdef NGATM_DEBUG
int
_uni_msg_unpack_mbuf(struct mbuf *m, struct uni_msg **pmsg, const char *file,
    int line)
#else
int
uni_msg_unpack_mbuf(struct mbuf *m, struct uni_msg **pmsg)
#endif
{
	if (!(m->m_flags & M_PKTHDR)) {
		printf("%s: bogus packet %p\n", __func__, m);
		return (EINVAL);
	}
#ifdef NGATM_DEBUG
	if ((*pmsg = _uni_msg_alloc(m->m_pkthdr.len, file, line)) == NULL)
#else
	if ((*pmsg = uni_msg_alloc(m->m_pkthdr.len)) == NULL)
#endif
		return (ENOMEM);

	m_copydata(m, 0, m->m_pkthdr.len, (*pmsg)->b_wptr);
	(*pmsg)->b_wptr += m->m_pkthdr.len;

	return (0);
}

/*********************************************************************/

static int
ngatm_handler(module_t mod, int what, void *arg)
{
	int error = 0;

	switch (what) {

	  case MOD_LOAD:
		uni_msg_init();
		break;

	  case MOD_UNLOAD:
		uni_msg_fini();
		break;

	  default:
		error = EOPNOTSUPP;
		break;
	}

	return (error);
}
