/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002, 2003 Sam Leffler, Errno Consulting
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
 * $FreeBSD$
 */

/*
 * IPsec-specific mbuf routines.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>

#include <net/vnet.h>
#include <netinet/in.h>
#include <netipsec/ipsec.h>

/*
 * Make space for a new header of length hlen at skip bytes
 * into the packet.  When doing this we allocate new mbufs only
 * when absolutely necessary.  The mbuf where the new header
 * is to go is returned together with an offset into the mbuf.
 * If NULL is returned then the mbuf chain may have been modified;
 * the caller is assumed to always free the chain.
 */
struct mbuf *
m_makespace(struct mbuf *m0, int skip, int hlen, int *off)
{
	struct mbuf *m;
	unsigned remain;

	IPSEC_ASSERT(m0 != NULL, ("null mbuf"));
	IPSEC_ASSERT(hlen < MHLEN, ("hlen too big: %u", hlen));

	for (m = m0; m && skip > m->m_len; m = m->m_next)
		skip -= m->m_len;
	if (m == NULL)
		return (NULL);
	/*
	 * At this point skip is the offset into the mbuf m
	 * where the new header should be placed.  Figure out
	 * if there's space to insert the new header.  If so,
	 * and copying the remainder makes sense then do so.
	 * Otherwise insert a new mbuf in the chain, splitting
	 * the contents of m as needed.
	 */
	remain = m->m_len - skip;		/* data to move */
	if (remain > skip &&
	    hlen + max_linkhdr < M_LEADINGSPACE(m)) {
		/*
		 * mbuf has enough free space at the beginning.
		 * XXX: which operation is the most heavy - copying of
		 *	possible several hundred of bytes or allocation
		 *	of new mbuf? We can remove max_linkhdr check
		 *	here, but it is possible that this will lead
		 *	to allocation of new mbuf in Layer 2 code.
		 */
		m->m_data -= hlen;
		bcopy(mtodo(m, hlen), mtod(m, caddr_t), skip);
		m->m_len += hlen;
		*off = skip;
	} else if (hlen > M_TRAILINGSPACE(m)) {
		struct mbuf *n0, *n, **np;
		int todo, len, done, alloc;

		n0 = NULL;
		np = &n0;
		alloc = 0;
		done = 0;
		todo = remain;
		while (todo > 0) {
			if (todo > MHLEN) {
				n = m_getcl(M_NOWAIT, m->m_type, 0);
				len = MCLBYTES;
			}
			else {
				n = m_get(M_NOWAIT, m->m_type);
				len = MHLEN;
			}
			if (n == NULL) {
				m_freem(n0);
				return NULL;
			}
			*np = n;
			np = &n->m_next;
			alloc++;
			len = min(todo, len);
			memcpy(n->m_data, mtod(m, char *) + skip + done, len);
			n->m_len = len;
			done += len;
			todo -= len;
		}

		if (hlen <= M_TRAILINGSPACE(m) + remain) {
			m->m_len = skip + hlen;
			*off = skip;
			if (n0 != NULL) {
				*np = m->m_next;
				m->m_next = n0;
			}
		}
		else {
			n = m_get(M_NOWAIT, m->m_type);
			if (n == NULL) {
				m_freem(n0);
				return NULL;
			}
			alloc++;

			if ((n->m_next = n0) == NULL)
				np = &n->m_next;
			n0 = n;

			*np = m->m_next;
			m->m_next = n0;

			n->m_len = hlen;
			m->m_len = skip;

			m = n;			/* header is at front ... */
			*off = 0;		/* ... of new mbuf */
		}
		IPSECSTAT_INC(ips_mbinserted);
	} else {
		/*
		 * Copy the remainder to the back of the mbuf
		 * so there's space to write the new header.
		 */
		bcopy(mtod(m, caddr_t) + skip,
		    mtod(m, caddr_t) + skip + hlen, remain);
		m->m_len += hlen;
		*off = skip;
	}
	m0->m_pkthdr.len += hlen;		/* adjust packet length */
	return m;
}

/*
 * m_pad(m, n) pads <m> with <n> bytes at the end. The packet header
 * length is updated, and a pointer to the first byte of the padding
 * (which is guaranteed to be all in one mbuf) is returned.
 */
caddr_t
m_pad(struct mbuf *m, int n)
{
	struct mbuf *m0, *m1;
	int len, pad;
	caddr_t retval;

	if (n <= 0) {  /* No stupid arguments. */
		DPRINTF(("%s: pad length invalid (%d)\n", __func__, n));
		m_freem(m);
		return NULL;
	}

	len = m->m_pkthdr.len;
	pad = n;
	m0 = m;

	while (m0->m_len < len) {
		len -= m0->m_len;
		m0 = m0->m_next;
	}

	if (m0->m_len != len) {
		DPRINTF(("%s: length mismatch (should be %d instead of %d)\n",
			__func__, m->m_pkthdr.len,
			m->m_pkthdr.len + m0->m_len - len));

		m_freem(m);
		return NULL;
	}

	/* Check for zero-length trailing mbufs, and find the last one. */
	for (m1 = m0; m1->m_next; m1 = m1->m_next) {
		if (m1->m_next->m_len != 0) {
			DPRINTF(("%s: length mismatch (should be %d instead "
				"of %d)\n", __func__,
				m->m_pkthdr.len,
				m->m_pkthdr.len + m1->m_next->m_len));

			m_freem(m);
			return NULL;
		}

		m0 = m1->m_next;
	}

	if (pad > M_TRAILINGSPACE(m0)) {
		/* Add an mbuf to the chain. */
		MGET(m1, M_NOWAIT, MT_DATA);
		if (m1 == NULL) {
			m_freem(m0);
			DPRINTF(("%s: unable to get extra mbuf\n", __func__));
			return NULL;
		}

		m0->m_next = m1;
		m0 = m1;
		m0->m_len = 0;
	}

	retval = m0->m_data + m0->m_len;
	m0->m_len += pad;
	m->m_pkthdr.len += pad;

	return retval;
}

/*
 * Remove hlen data at offset skip in the packet.  This is used by
 * the protocols strip protocol headers and associated data (e.g. IV,
 * authenticator) on input.
 */
int
m_striphdr(struct mbuf *m, int skip, int hlen)
{
	struct mbuf *m1;
	int roff;

	/* Find beginning of header */
	m1 = m_getptr(m, skip, &roff);
	if (m1 == NULL)
		return (EINVAL);

	/* Remove the header and associated data from the mbuf. */
	if (roff == 0) {
		/* The header was at the beginning of the mbuf */
		IPSECSTAT_INC(ips_input_front);
		m_adj(m1, hlen);
		if (m1 != m)
			m->m_pkthdr.len -= hlen;
	} else if (roff + hlen >= m1->m_len) {
		struct mbuf *mo;
		int adjlen;

		/*
		 * Part or all of the header is at the end of this mbuf,
		 * so first let's remove the remainder of the header from
		 * the beginning of the remainder of the mbuf chain, if any.
		 */
		IPSECSTAT_INC(ips_input_end);
		if (roff + hlen > m1->m_len) {
			adjlen = roff + hlen - m1->m_len;

			/* Adjust the next mbuf by the remainder */
			m_adj(m1->m_next, adjlen);

			/* The second mbuf is guaranteed not to have a pkthdr... */
			m->m_pkthdr.len -= adjlen;
		}

		/* Now, let's unlink the mbuf chain for a second...*/
		mo = m1->m_next;
		m1->m_next = NULL;

		/* ...and trim the end of the first part of the chain...sick */
		adjlen = m1->m_len - roff;
		m_adj(m1, -adjlen);
		if (m1 != m)
			m->m_pkthdr.len -= adjlen;

		/* Finally, let's relink */
		m1->m_next = mo;
	} else {
		/*
		 * The header lies in the "middle" of the mbuf; copy
		 * the remainder of the mbuf down over the header.
		 */
		IPSECSTAT_INC(ips_input_middle);
		bcopy(mtod(m1, u_char *) + roff + hlen,
		      mtod(m1, u_char *) + roff,
		      m1->m_len - (roff + hlen));
		m1->m_len -= hlen;
		m->m_pkthdr.len -= hlen;
	}
	return (0);
}

/*
 * Diagnostic routine to check mbuf alignment as required by the
 * crypto device drivers (that use DMA).
 */
void
m_checkalignment(const char* where, struct mbuf *m0, int off, int len)
{
	int roff;
	struct mbuf *m = m_getptr(m0, off, &roff);
	caddr_t addr;

	if (m == NULL)
		return;
	printf("%s (off %u len %u): ", where, off, len);
	addr = mtod(m, caddr_t) + roff;
	do {
		int mlen;

		if (((uintptr_t) addr) & 3) {
			printf("addr misaligned %p,", addr);
			break;
		}
		mlen = m->m_len;
		if (mlen > len)
			mlen = len;
		len -= mlen;
		if (len && (mlen & 3)) {
			printf("len mismatch %u,", mlen);
			break;
		}
		m = m->m_next;
		addr = m ? mtod(m, caddr_t) : NULL;
	} while (m && len > 0);
	for (m = m0; m; m = m->m_next)
		printf(" [%p:%u]", mtod(m, caddr_t), m->m_len);
	printf("\n");
}
