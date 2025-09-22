/*	$OpenBSD: cn30xxfpavar.h,v 1.9 2024/05/20 23:13:33 jsg Exp $	*/
/*
 * Copyright (c) 2007 Internet Initiative Japan, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _CN30XXFPAVAR_H_
#define _CN30XXFPAVAR_H_

struct cn30xxfpa_buf {
	int		fb_poolno;	/* pool # */

	size_t		fb_size;	/* element size */
	size_t		fb_nelems;	/* # of elements */

	paddr_t		fb_paddr;	/* physical address */
	vaddr_t		fb_addr;	/* virtual address */
	size_t		fb_len;		/* total length */

	bus_dma_tag_t	fb_dmat;
	bus_dmamap_t	fb_dmah;
	bus_dma_segment_t
			*fb_dma_segs;
	int		fb_dma_nsegs;
};

int		cn30xxfpa_buf_init(int, size_t, size_t, struct cn30xxfpa_buf **);
void		*cn30xxfpa_buf_get(struct cn30xxfpa_buf *);
uint64_t	cn30xxfpa_query(int);

/*
 * operations
 */

static inline uint64_t
cn30xxfpa_load(uint64_t fpapool)
{
	uint64_t addr;

	addr =
	    (0x1ULL << 48) |
	    (0x5ULL << 43) |
	    (fpapool & 0x07ULL) << 40;

	return octeon_xkphys_read_8(addr);
}

#ifdef notyet
static inline uint64_t
cn30xxfpa_iobdma(struct cn30xxfpa_softc *sc, int srcaddr, int len)
{
	/* XXX */
	return 0ULL;
}
#endif

static inline void
cn30xxfpa_store(uint64_t addr, uint64_t fpapool, uint64_t dwbcount)
{
	uint64_t ptr;

	ptr =
	    (0x1ULL << 48) |
	    (0x5ULL << 43) |
	    (fpapool & 0x07ULL) << 40 |
	    (addr & 0xffffffffffULL);

	mips_sync();
	octeon_xkphys_write_8(ptr, (dwbcount & 0x0ffULL));
}

static inline paddr_t
cn30xxfpa_buf_get_paddr(struct cn30xxfpa_buf *fb)
{
	return cn30xxfpa_load(fb->fb_poolno);
}

static inline void
cn30xxfpa_buf_put_paddr(struct cn30xxfpa_buf *fb, paddr_t paddr)
{
	KASSERT(paddr >= fb->fb_paddr);
	KASSERT(paddr < fb->fb_paddr + fb->fb_len);
	cn30xxfpa_store(paddr, fb->fb_poolno, fb->fb_size / CACHELINESIZE);
}

static inline void
cn30xxfpa_buf_put(struct cn30xxfpa_buf *fb, void *addr)
{
	paddr_t paddr;

	KASSERT((vaddr_t)addr >= fb->fb_addr);
	KASSERT((vaddr_t)addr < fb->fb_addr + fb->fb_len);
	paddr = fb->fb_paddr + (paddr_t/* XXX */)((vaddr_t)addr - fb->fb_addr);
	cn30xxfpa_buf_put_paddr(fb, paddr);
}

#endif
