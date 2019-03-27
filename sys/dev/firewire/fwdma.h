/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (C) 2003
 * 	Hidetoshi Shimokawa. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *
 *	This product includes software developed by Hidetoshi Shimokawa.
 *
 * 4. Neither the name of the author nor the names of its contributors
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
 * $FreeBSD$
 */

struct fwdma_alloc {
	bus_dma_tag_t	dma_tag;
	bus_dmamap_t	dma_map;
	void 		*v_addr;
	bus_addr_t	bus_addr;
};

struct fwdma_seg {
	bus_dmamap_t	dma_map;
	void 		*v_addr;
	bus_addr_t	bus_addr;
};

struct fwdma_alloc_multi {
	bus_size_t	ssize;
	bus_size_t	esize;
	int		nseg;
	bus_dma_tag_t	dma_tag;
	struct fwdma_seg seg[0];
};

static __inline void *
fwdma_v_addr(struct fwdma_alloc_multi *am, int index)
{
	bus_size_t ssize = am->ssize;
	int offset = am->esize * index;

	return ((caddr_t)am->seg[offset / ssize].v_addr + (offset % ssize));
}

static __inline bus_addr_t
fwdma_bus_addr(struct fwdma_alloc_multi *am, int index)
{
	bus_size_t ssize = am->ssize;
	int offset = am->esize * index;

	return (am->seg[offset / ssize].bus_addr + (offset % ssize));
}

static __inline void
fwdma_sync(struct fwdma_alloc *dma, bus_dmasync_op_t op)
{
	bus_dmamap_sync(dma->dma_tag, dma->dma_map, op);
}

static __inline void
fwdma_sync_multiseg(struct fwdma_alloc_multi *am,
    int start, int end, bus_dmasync_op_t op)
{
	struct fwdma_seg *seg, *eseg;

	seg = &am->seg[am->esize * start / am->ssize];
	eseg = &am->seg[am->esize * end / am->ssize];
	for (; seg <= eseg; seg++)
		bus_dmamap_sync(am->dma_tag, seg->dma_map, op);
}

static __inline void
fwdma_sync_multiseg_all(struct fwdma_alloc_multi *am, bus_dmasync_op_t op)
{
	struct fwdma_seg *seg;
	int i;

	seg = &am->seg[0];
	for (i = 0; i < am->nseg; i++, seg++)
		bus_dmamap_sync(am->dma_tag, seg->dma_map, op);
}

void *fwdma_malloc(struct firewire_comm *, int, bus_size_t, struct fwdma_alloc *, int);
void fwdma_free(struct firewire_comm *, struct fwdma_alloc *);
void *fwdma_malloc_size(bus_dma_tag_t, bus_dmamap_t *, bus_size_t, bus_addr_t *, int);
void fwdma_free_size(bus_dma_tag_t, bus_dmamap_t, void *, bus_size_t);
struct fwdma_alloc_multi *fwdma_malloc_multiseg(struct firewire_comm *,
	int, int, int, int);
void fwdma_free_multiseg(struct fwdma_alloc_multi *);

