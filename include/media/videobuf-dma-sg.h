/*
 * helper functions for SG DMA video4linux capture buffers
 *
 * The functions expect the hardware being able to scatter gather
 * (i.e. the buffers are not linear in physical memory, but fragmented
 * into PAGE_SIZE chunks).  They also assume the driver does not need
 * to touch the video data.
 *
 * (c) 2007 Mauro Carvalho Chehab, <mchehab@infradead.org>
 *
 * Highly based on video-buf written originally by:
 * (c) 2001,02 Gerd Knorr <kraxel@bytesex.org>
 * (c) 2006 Mauro Carvalho Chehab, <mchehab@infradead.org>
 * (c) 2006 Ted Walther and John Sokol
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2
 */
#ifndef _VIDEOBUF_DMA_SG_H
#define _VIDEOBUF_DMA_SG_H

#include <media/videobuf-core.h>

/* --------------------------------------------------------------------- */

/*
 * Return a scatterlist for some page-aligned vmalloc()'ed memory
 * block (NULL on errors).  Memory for the scatterlist is allocated
 * using kmalloc.  The caller must free the memory.
 */
struct scatterlist *videobuf_vmalloc_to_sg(unsigned char *virt, int nr_pages);

/*
 * Return a scatterlist for a an array of userpages (NULL on errors).
 * Memory for the scatterlist is allocated using kmalloc.  The caller
 * must free the memory.
 */
struct scatterlist *videobuf_pages_to_sg(struct page **pages, int nr_pages,
					 int offset);

/* --------------------------------------------------------------------- */

/*
 * A small set of helper functions to manage buffers (both userland
 * and kernel) for DMA.
 *
 * videobuf_dma_init_*()
 *	creates a buffer.  The userland version takes a userspace
 *	pointer + length.  The kernel version just wants the size and
 *	does memory allocation too using vmalloc_32().
 *
 * videobuf_dma_*()
 *	see Documentation/PCI/PCI-DMA-mapping.txt, these functions to
 *	basically the same.  The map function does also build a
 *	scatterlist for the buffer (and unmap frees it ...)
 *
 * videobuf_dma_free()
 *	no comment ...
 *
 */

struct videobuf_dmabuf {
	u32                 magic;

	/* for userland buffer */
	int                 offset;
	struct page         **pages;

	/* for kernel buffers */
	void                *vmalloc;

	/* for overlay buffers (pci-pci dma) */
	dma_addr_t          bus_addr;

	/* common */
	struct scatterlist  *sglist;
	int                 sglen;
	int                 nr_pages;
	int                 direction;
};

struct videobuf_dma_sg_memory {
	u32                 magic;

	/* for mmap'ed buffers */
	struct videobuf_dmabuf  dma;
};

void videobuf_dma_init(struct videobuf_dmabuf *dma);
int videobuf_dma_init_user(struct videobuf_dmabuf *dma, int direction,
			   unsigned long data, unsigned long size);
int videobuf_dma_init_kernel(struct videobuf_dmabuf *dma, int direction,
			     int nr_pages);
int videobuf_dma_init_overlay(struct videobuf_dmabuf *dma, int direction,
			      dma_addr_t addr, int nr_pages);
int videobuf_dma_free(struct videobuf_dmabuf *dma);

int videobuf_dma_map(struct videobuf_queue *q, struct videobuf_dmabuf *dma);
int videobuf_dma_unmap(struct videobuf_queue *q, struct videobuf_dmabuf *dma);
struct videobuf_dmabuf *videobuf_to_dma(struct videobuf_buffer *buf);

void *videobuf_sg_alloc(size_t size);

void videobuf_queue_sg_init(struct videobuf_queue *q,
			 const struct videobuf_queue_ops *ops,
			 struct device *dev,
			 spinlock_t *irqlock,
			 enum v4l2_buf_type type,
			 enum v4l2_field field,
			 unsigned int msize,
			 void *priv);

/*FIXME: these variants are used only on *-alsa code, where videobuf is
 * used without queue
 */
int videobuf_sg_dma_map(struct device *dev, struct videobuf_dmabuf *dma);
int videobuf_sg_dma_unmap(struct device *dev, struct videobuf_dmabuf *dma);

#endif /* _VIDEOBUF_DMA_SG_H */

