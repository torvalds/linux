/*	$OpenBSD: dma_alloc.c,v 1.13 2016/09/15 02:00:16 dlg Exp $	 */

/*
 * Copyright (c) 2010 Theo de Raadt <deraadt@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/pool.h>

#include <uvm/uvm_extern.h>

static __inline int	 dma_alloc_index(size_t size);

/* Create dma pools from objects sized 2^4 to 2^16 */
#define DMA_PAGE_SHIFT		16
#define DMA_BUCKET_OFFSET	4
static char dmanames[DMA_PAGE_SHIFT - DMA_BUCKET_OFFSET + 1][10];
struct pool dmapools[DMA_PAGE_SHIFT - DMA_BUCKET_OFFSET + 1];

void
dma_alloc_init(void)
{
	int i;

	for (i = 0; i < nitems(dmapools); i++) {
		snprintf(dmanames[i], sizeof(dmanames[0]), "dma%d",
		    1 << (i + DMA_BUCKET_OFFSET));
		pool_init(&dmapools[i], 1 << (i + DMA_BUCKET_OFFSET), 0,
		    IPL_VM, 0, dmanames[i], NULL);
		pool_set_constraints(&dmapools[i], &kp_dma_contig);
		/* XXX need pool_setlowat(&dmapools[i], dmalowat); */
	}
}

static __inline int
dma_alloc_index(size_t sz)
{
	int b;

	for (b = 0; b < nitems(dmapools); b++)
		if (sz <= (1 << (b + DMA_BUCKET_OFFSET)))
			return (b);
#ifdef DEBUG
	printf("dma_alloc/free: object %zd too large\n", sz);
#endif
	return (-1);
}

void *
dma_alloc(size_t size, int prflags)
{
	int pi = dma_alloc_index(size);

	if (pi == -1)
		return (NULL);
	return pool_get(&dmapools[pi], prflags);
}


void
dma_free(void *m, size_t size)
{
	int pi = dma_alloc_index(size);

	if (pi == -1)
		return;
	pool_put(&dmapools[pi], m);
}
