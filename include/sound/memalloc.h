/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *                   Takashi Iwai <tiwai@suse.de>
 * 
 *  Generic memory allocators
 */

#ifndef __SOUND_MEMALLOC_H
#define __SOUND_MEMALLOC_H

#include <linux/dma-direction.h>
#include <asm/page.h>

struct device;
struct vm_area_struct;
struct sg_table;

/*
 * buffer device info
 */
struct snd_dma_device {
	int type;			/* SNDRV_DMA_TYPE_XXX */
	enum dma_data_direction dir;	/* DMA direction */
	bool need_sync;			/* explicit sync needed? */
	struct device *dev;		/* generic device */
};

#define snd_dma_continuous_data(x)	((struct device *)(__force unsigned long)(x))


/*
 * buffer types
 */
#define SNDRV_DMA_TYPE_UNKNOWN		0	/* not defined */
#define SNDRV_DMA_TYPE_CONTINUOUS	1	/* continuous no-DMA memory */
#define SNDRV_DMA_TYPE_DEV		2	/* generic device continuous */
#define SNDRV_DMA_TYPE_DEV_WC		5	/* continuous write-combined */
#ifdef CONFIG_GENERIC_ALLOCATOR
#define SNDRV_DMA_TYPE_DEV_IRAM		4	/* generic device iram-buffer */
#else
#define SNDRV_DMA_TYPE_DEV_IRAM	SNDRV_DMA_TYPE_DEV
#endif
#define SNDRV_DMA_TYPE_VMALLOC		7	/* vmalloc'ed buffer */
#define SNDRV_DMA_TYPE_NONCONTIG	8	/* non-coherent SG buffer */
#define SNDRV_DMA_TYPE_NONCOHERENT	9	/* non-coherent buffer */
#ifdef CONFIG_SND_DMA_SGBUF
#define SNDRV_DMA_TYPE_DEV_SG		SNDRV_DMA_TYPE_NONCONTIG
#define SNDRV_DMA_TYPE_DEV_WC_SG	6	/* SG write-combined */
#else
#define SNDRV_DMA_TYPE_DEV_SG	SNDRV_DMA_TYPE_DEV /* no SG-buf support */
#define SNDRV_DMA_TYPE_DEV_WC_SG	SNDRV_DMA_TYPE_DEV_WC
#endif
/* fallback types, don't use those directly */
#ifdef CONFIG_SND_DMA_SGBUF
#define SNDRV_DMA_TYPE_DEV_SG_FALLBACK		10
#define SNDRV_DMA_TYPE_DEV_WC_SG_FALLBACK	11
#endif

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
int snd_dma_alloc_dir_pages(int type, struct device *dev,
			    enum dma_data_direction dir, size_t size,
			    struct snd_dma_buffer *dmab);

static inline int snd_dma_alloc_pages(int type, struct device *dev,
				      size_t size, struct snd_dma_buffer *dmab)
{
	return snd_dma_alloc_dir_pages(type, dev, DMA_BIDIRECTIONAL, size, dmab);
}

int snd_dma_alloc_pages_fallback(int type, struct device *dev, size_t size,
                                 struct snd_dma_buffer *dmab);
void snd_dma_free_pages(struct snd_dma_buffer *dmab);
int snd_dma_buffer_mmap(struct snd_dma_buffer *dmab,
			struct vm_area_struct *area);

enum snd_dma_sync_mode { SNDRV_DMA_SYNC_CPU, SNDRV_DMA_SYNC_DEVICE };
#ifdef CONFIG_HAS_DMA
void snd_dma_buffer_sync(struct snd_dma_buffer *dmab,
			 enum snd_dma_sync_mode mode);
#else
static inline void snd_dma_buffer_sync(struct snd_dma_buffer *dmab,
				       enum snd_dma_sync_mode mode) {}
#endif

dma_addr_t snd_sgbuf_get_addr(struct snd_dma_buffer *dmab, size_t offset);
struct page *snd_sgbuf_get_page(struct snd_dma_buffer *dmab, size_t offset);
unsigned int snd_sgbuf_get_chunk_size(struct snd_dma_buffer *dmab,
				      unsigned int ofs, unsigned int size);

/* device-managed memory allocator */
struct snd_dma_buffer *snd_devm_alloc_dir_pages(struct device *dev, int type,
						enum dma_data_direction dir,
						size_t size);

static inline struct snd_dma_buffer *
snd_devm_alloc_pages(struct device *dev, int type, size_t size)
{
	return snd_devm_alloc_dir_pages(dev, type, DMA_BIDIRECTIONAL, size);
}

static inline struct sg_table *
snd_dma_noncontig_sg_table(struct snd_dma_buffer *dmab)
{
	return dmab->private_data;
}

#endif /* __SOUND_MEMALLOC_H */

