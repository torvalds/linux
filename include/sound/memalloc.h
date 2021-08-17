/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *                   Takashi Iwai <tiwai@suse.de>
 * 
 *  Generic memory allocators
 */

#ifndef __SOUND_MEMALLOC_H
#define __SOUND_MEMALLOC_H

#include <asm/page.h>

struct device;
struct vm_area_struct;

/*
 * buffer device info
 */
struct snd_dma_device {
	int type;			/* SNDRV_DMA_TYPE_XXX */
	struct device *dev;		/* generic device */
};

#define snd_dma_continuous_data(x)	((struct device *)(__force unsigned long)(x))


/*
 * buffer types
 */
#define SNDRV_DMA_TYPE_UNKNOWN		0	/* not defined */
#define SNDRV_DMA_TYPE_CONTINUOUS	1	/* continuous no-DMA memory */
#define SNDRV_DMA_TYPE_DEV		2	/* generic device continuous */
#define SNDRV_DMA_TYPE_DEV_UC		5	/* continuous non-cahced */
#ifdef CONFIG_SND_DMA_SGBUF
#define SNDRV_DMA_TYPE_DEV_SG		3	/* generic device SG-buffer */
#define SNDRV_DMA_TYPE_DEV_UC_SG	6	/* SG non-cached */
#else
#define SNDRV_DMA_TYPE_DEV_SG	SNDRV_DMA_TYPE_DEV /* no SG-buf support */
#define SNDRV_DMA_TYPE_DEV_UC_SG	SNDRV_DMA_TYPE_DEV_UC
#endif
#ifdef CONFIG_GENERIC_ALLOCATOR
#define SNDRV_DMA_TYPE_DEV_IRAM		4	/* generic device iram-buffer */
#else
#define SNDRV_DMA_TYPE_DEV_IRAM	SNDRV_DMA_TYPE_DEV
#endif
#define SNDRV_DMA_TYPE_VMALLOC		7	/* vmalloc'ed buffer */

/*
 * info for buffer allocation
 */
struct snd_dma_buffer {
	struct snd_dma_device dev;	/* device type */
	unsigned char *area;	/* virtual pointer */
	dma_addr_t addr;	/* physical address */
	size_t bytes;		/* buffer size in bytes */
	void *private_data;	/* private for allocator; don't touch */
};

/*
 * return the pages matching with the given byte size
 */
static inline unsigned int snd_sgbuf_aligned_pages(size_t size)
{
	return (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
}

/* allocate/release a buffer */
int snd_dma_alloc_pages(int type, struct device *dev, size_t size,
			struct snd_dma_buffer *dmab);
int snd_dma_alloc_pages_fallback(int type, struct device *dev, size_t size,
                                 struct snd_dma_buffer *dmab);
void snd_dma_free_pages(struct snd_dma_buffer *dmab);
int snd_dma_buffer_mmap(struct snd_dma_buffer *dmab,
			struct vm_area_struct *area);

dma_addr_t snd_sgbuf_get_addr(struct snd_dma_buffer *dmab, size_t offset);
struct page *snd_sgbuf_get_page(struct snd_dma_buffer *dmab, size_t offset);
unsigned int snd_sgbuf_get_chunk_size(struct snd_dma_buffer *dmab,
				      unsigned int ofs, unsigned int size);

#endif /* __SOUND_MEMALLOC_H */

