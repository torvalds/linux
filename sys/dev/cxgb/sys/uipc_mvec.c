/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007-2008 Kip Macy <kmacy@freebsd.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/ktr.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/bus.h>

#include <cxgb_include.h>
#include <sys/mvec.h>

#ifdef INVARIANTS
#define M_SANITY m_sanity
#else
#define M_SANITY(a, b)
#endif

int
busdma_map_sg_collapse(bus_dma_tag_t tag, bus_dmamap_t map,
	struct mbuf **m, bus_dma_segment_t *segs, int *nsegs)
{
	struct mbuf *n = *m;
	int seg_count, defragged = 0, err = 0;
	bus_dma_segment_t *psegs;
	
	KASSERT(n->m_pkthdr.len, ("packet has zero header len"));
	if (n->m_pkthdr.len <= PIO_LEN)
		return (0);
retry:
	psegs = segs;
	seg_count = 0;
	if (n->m_next == NULL) {
		busdma_map_mbuf_fast(tag, map, n, segs);
		*nsegs = 1;
		return (0);
	}
#if defined(__i386__) || defined(__amd64__)
	while (n && seg_count < TX_MAX_SEGS) {
		/*
		 * firmware doesn't like empty segments
		 */
		if (__predict_true(n->m_len != 0)) {
			seg_count++;
			busdma_map_mbuf_fast(tag, map, n, psegs);
			psegs++;
		}
		n = n->m_next;
	}
#else
	err = bus_dmamap_load_mbuf_sg(tag, map, *m, segs, &seg_count, 0);
#endif	
	if (seg_count == 0) {
		if (cxgb_debug)
			printf("empty segment chain\n");
		err = EFBIG;
		goto err_out;
	}  else if (err == EFBIG || seg_count >= TX_MAX_SEGS) {
		if (cxgb_debug)
			printf("mbuf chain too long: %d max allowed %d\n",
			    seg_count, TX_MAX_SEGS);
		if (!defragged) {
			n = m_defrag(*m, M_NOWAIT);
			if (n == NULL) {
				err = ENOBUFS;
				goto err_out;
			}
			*m = n;
			defragged = 1;
			goto retry;
		}
		err = EFBIG;
		goto err_out;
	}

	*nsegs = seg_count;
err_out:	
	return (err);
}

void
busdma_map_sg_vec(bus_dma_tag_t tag, bus_dmamap_t map,
    struct mbuf *m, bus_dma_segment_t *segs, int *nsegs)
{

	for (*nsegs = 0; m != NULL ; segs++, *nsegs += 1, m = m->m_nextpkt)
		busdma_map_mbuf_fast(tag, map, m, segs);
}

