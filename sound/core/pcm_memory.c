// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Digital Audio (PCM) abstract layer
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 */

#include <linux/io.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/moduleparam.h>
#include <linux/vmalloc.h>
#include <linux/export.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/info.h>
#include <sound/initval.h>

static int preallocate_dma = 1;
module_param(preallocate_dma, int, 0444);
MODULE_PARM_DESC(preallocate_dma, "Preallocate DMA memory when the PCM devices are initialized.");

static int maximum_substreams = 4;
module_param(maximum_substreams, int, 0444);
MODULE_PARM_DESC(maximum_substreams, "Maximum substreams with preallocated DMA memory.");

static const size_t snd_minimum_buffer = 16384;


/*
 * try to allocate as the large pages as possible.
 * stores the resultant memory size in *res_size.
 *
 * the minimum size is snd_minimum_buffer.  it should be power of 2.
 */
static int preallocate_pcm_pages(struct snd_pcm_substream *substream, size_t size)
{
	struct snd_dma_buffer *dmab = &substream->dma_buffer;
	size_t orig_size = size;
	int err;

	do {
		if ((err = snd_dma_alloc_pages(dmab->dev.type, dmab->dev.dev,
					       size, dmab)) < 0) {
			if (err != -ENOMEM)
				return err; /* fatal error */
		} else
			return 0;
		size >>= 1;
	} while (size >= snd_minimum_buffer);
	dmab->bytes = 0; /* tell error */
	pr_warn("ALSA pcmC%dD%d%c,%d:%s: cannot preallocate for size %zu\n",
		substream->pcm->card->number, substream->pcm->device,
		substream->stream ? 'c' : 'p', substream->number,
		substream->pcm->name, orig_size);
	return 0;
}

/*
 * release the preallocated buffer if not yet done.
 */
static void snd_pcm_lib_preallocate_dma_free(struct snd_pcm_substream *substream)
{
	if (substream->dma_buffer.area == NULL)
		return;
	snd_dma_free_pages(&substream->dma_buffer);
	substream->dma_buffer.area = NULL;
}

/**
 * snd_pcm_lib_preallocate_free - release the preallocated buffer of the specified substream.
 * @substream: the pcm substream instance
 *
 * Releases the pre-allocated buffer of the given substream.
 */
void snd_pcm_lib_preallocate_free(struct snd_pcm_substream *substream)
{
	snd_pcm_lib_preallocate_dma_free(substream);
}

/**
 * snd_pcm_lib_preallocate_free_for_all - release all pre-allocated buffers on the pcm
 * @pcm: the pcm instance
 *
 * Releases all the pre-allocated buffers on the given pcm.
 */
void snd_pcm_lib_preallocate_free_for_all(struct snd_pcm *pcm)
{
	struct snd_pcm_substream *substream;
	int stream;

	for (stream = 0; stream < 2; stream++)
		for (substream = pcm->streams[stream].substream; substream; substream = substream->next)
			snd_pcm_lib_preallocate_free(substream);
}
EXPORT_SYMBOL(snd_pcm_lib_preallocate_free_for_all);

#ifdef CONFIG_SND_VERBOSE_PROCFS
/*
 * read callback for prealloc proc file
 *
 * prints the current allocated size in kB.
 */
static void snd_pcm_lib_preallocate_proc_read(struct snd_info_entry *entry,
					      struct snd_info_buffer *buffer)
{
	struct snd_pcm_substream *substream = entry->private_data;
	snd_iprintf(buffer, "%lu\n", (unsigned long) substream->dma_buffer.bytes / 1024);
}

/*
 * read callback for prealloc_max proc file
 *
 * prints the maximum allowed size in kB.
 */
static void snd_pcm_lib_preallocate_max_proc_read(struct snd_info_entry *entry,
						  struct snd_info_buffer *buffer)
{
	struct snd_pcm_substream *substream = entry->private_data;
	snd_iprintf(buffer, "%lu\n", (unsigned long) substream->dma_max / 1024);
}

/*
 * write callback for prealloc proc file
 *
 * accepts the preallocation size in kB.
 */
static void snd_pcm_lib_preallocate_proc_write(struct snd_info_entry *entry,
					       struct snd_info_buffer *buffer)
{
	struct snd_pcm_substream *substream = entry->private_data;
	char line[64], str[64];
	size_t size;
	struct snd_dma_buffer new_dmab;

	if (substream->runtime) {
		buffer->error = -EBUSY;
		return;
	}
	if (!snd_info_get_line(buffer, line, sizeof(line))) {
		snd_info_get_str(str, line, sizeof(str));
		size = simple_strtoul(str, NULL, 10) * 1024;
		if ((size != 0 && size < 8192) || size > substream->dma_max) {
			buffer->error = -EINVAL;
			return;
		}
		if (substream->dma_buffer.bytes == size)
			return;
		memset(&new_dmab, 0, sizeof(new_dmab));
		new_dmab.dev = substream->dma_buffer.dev;
		if (size > 0) {
			if (snd_dma_alloc_pages(substream->dma_buffer.dev.type,
						substream->dma_buffer.dev.dev,
						size, &new_dmab) < 0) {
				buffer->error = -ENOMEM;
				return;
			}
			substream->buffer_bytes_max = size;
		} else {
			substream->buffer_bytes_max = UINT_MAX;
		}
		if (substream->dma_buffer.area)
			snd_dma_free_pages(&substream->dma_buffer);
		substream->dma_buffer = new_dmab;
	} else {
		buffer->error = -EINVAL;
	}
}

static inline void preallocate_info_init(struct snd_pcm_substream *substream)
{
	struct snd_info_entry *entry;

	entry = snd_info_create_card_entry(substream->pcm->card, "prealloc",
					   substream->proc_root);
	if (entry) {
		snd_info_set_text_ops(entry, substream,
				      snd_pcm_lib_preallocate_proc_read);
		entry->c.text.write = snd_pcm_lib_preallocate_proc_write;
		entry->mode |= 0200;
	}
	entry = snd_info_create_card_entry(substream->pcm->card, "prealloc_max",
					   substream->proc_root);
	if (entry)
		snd_info_set_text_ops(entry, substream,
				      snd_pcm_lib_preallocate_max_proc_read);
}

#else /* !CONFIG_SND_VERBOSE_PROCFS */
#define preallocate_info_init(s)
#endif /* CONFIG_SND_VERBOSE_PROCFS */

/*
 * pre-allocate the buffer and create a proc file for the substream
 */
static void snd_pcm_lib_preallocate_pages1(struct snd_pcm_substream *substream,
					  size_t size, size_t max)
{

	if (size > 0 && preallocate_dma && substream->number < maximum_substreams)
		preallocate_pcm_pages(substream, size);

	if (substream->dma_buffer.bytes > 0)
		substream->buffer_bytes_max = substream->dma_buffer.bytes;
	substream->dma_max = max;
	preallocate_info_init(substream);
}


/**
 * snd_pcm_lib_preallocate_pages - pre-allocation for the given DMA type
 * @substream: the pcm substream instance
 * @type: DMA type (SNDRV_DMA_TYPE_*)
 * @data: DMA type dependent data
 * @size: the requested pre-allocation size in bytes
 * @max: the max. allowed pre-allocation size
 *
 * Do pre-allocation for the given DMA buffer type.
 */
void snd_pcm_lib_preallocate_pages(struct snd_pcm_substream *substream,
				  int type, struct device *data,
				  size_t size, size_t max)
{
	if (snd_BUG_ON(substream->dma_buffer.dev.type))
		return;
	substream->dma_buffer.dev.type = type;
	substream->dma_buffer.dev.dev = data;
	snd_pcm_lib_preallocate_pages1(substream, size, max);
}
EXPORT_SYMBOL(snd_pcm_lib_preallocate_pages);

/**
 * snd_pcm_lib_preallocate_pages_for_all - pre-allocation for continuous memory type (all substreams)
 * @pcm: the pcm instance
 * @type: DMA type (SNDRV_DMA_TYPE_*)
 * @data: DMA type dependent data
 * @size: the requested pre-allocation size in bytes
 * @max: the max. allowed pre-allocation size
 *
 * Do pre-allocation to all substreams of the given pcm for the
 * specified DMA type.
 */
void snd_pcm_lib_preallocate_pages_for_all(struct snd_pcm *pcm,
					  int type, void *data,
					  size_t size, size_t max)
{
	struct snd_pcm_substream *substream;
	int stream;

	for (stream = 0; stream < 2; stream++)
		for (substream = pcm->streams[stream].substream; substream; substream = substream->next)
			snd_pcm_lib_preallocate_pages(substream, type, data, size, max);
}
EXPORT_SYMBOL(snd_pcm_lib_preallocate_pages_for_all);

#ifdef CONFIG_SND_DMA_SGBUF
/**
 * snd_pcm_sgbuf_ops_page - get the page struct at the given offset
 * @substream: the pcm substream instance
 * @offset: the buffer offset
 *
 * Used as the page callback of PCM ops.
 *
 * Return: The page struct at the given buffer offset. %NULL on failure.
 */
struct page *snd_pcm_sgbuf_ops_page(struct snd_pcm_substream *substream, unsigned long offset)
{
	struct snd_sg_buf *sgbuf = snd_pcm_substream_sgbuf(substream);

	unsigned int idx = offset >> PAGE_SHIFT;
	if (idx >= (unsigned int)sgbuf->pages)
		return NULL;
	return sgbuf->page_table[idx];
}
EXPORT_SYMBOL(snd_pcm_sgbuf_ops_page);
#endif /* CONFIG_SND_DMA_SGBUF */

/**
 * snd_pcm_lib_malloc_pages - allocate the DMA buffer
 * @substream: the substream to allocate the DMA buffer to
 * @size: the requested buffer size in bytes
 *
 * Allocates the DMA buffer on the BUS type given earlier to
 * snd_pcm_lib_preallocate_xxx_pages().
 *
 * Return: 1 if the buffer is changed, 0 if not changed, or a negative
 * code on failure.
 */
int snd_pcm_lib_malloc_pages(struct snd_pcm_substream *substream, size_t size)
{
	struct snd_pcm_runtime *runtime;
	struct snd_dma_buffer *dmab = NULL;

	if (PCM_RUNTIME_CHECK(substream))
		return -EINVAL;
	if (snd_BUG_ON(substream->dma_buffer.dev.type ==
		       SNDRV_DMA_TYPE_UNKNOWN))
		return -EINVAL;
	runtime = substream->runtime;

	if (runtime->dma_buffer_p) {
		/* perphaps, we might free the large DMA memory region
		   to save some space here, but the actual solution
		   costs us less time */
		if (runtime->dma_buffer_p->bytes >= size) {
			runtime->dma_bytes = size;
			return 0;	/* ok, do not change */
		}
		snd_pcm_lib_free_pages(substream);
	}
	if (substream->dma_buffer.area != NULL &&
	    substream->dma_buffer.bytes >= size) {
		dmab = &substream->dma_buffer; /* use the pre-allocated buffer */
	} else {
		dmab = kzalloc(sizeof(*dmab), GFP_KERNEL);
		if (! dmab)
			return -ENOMEM;
		dmab->dev = substream->dma_buffer.dev;
		if (snd_dma_alloc_pages(substream->dma_buffer.dev.type,
					substream->dma_buffer.dev.dev,
					size, dmab) < 0) {
			kfree(dmab);
			return -ENOMEM;
		}
	}
	snd_pcm_set_runtime_buffer(substream, dmab);
	runtime->dma_bytes = size;
	return 1;			/* area was changed */
}
EXPORT_SYMBOL(snd_pcm_lib_malloc_pages);

/**
 * snd_pcm_lib_free_pages - release the allocated DMA buffer.
 * @substream: the substream to release the DMA buffer
 *
 * Releases the DMA buffer allocated via snd_pcm_lib_malloc_pages().
 *
 * Return: Zero if successful, or a negative error code on failure.
 */
int snd_pcm_lib_free_pages(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime;

	if (PCM_RUNTIME_CHECK(substream))
		return -EINVAL;
	runtime = substream->runtime;
	if (runtime->dma_area == NULL)
		return 0;
	if (runtime->dma_buffer_p != &substream->dma_buffer) {
		/* it's a newly allocated buffer.  release it now. */
		snd_dma_free_pages(runtime->dma_buffer_p);
		kfree(runtime->dma_buffer_p);
	}
	snd_pcm_set_runtime_buffer(substream, NULL);
	return 0;
}
EXPORT_SYMBOL(snd_pcm_lib_free_pages);

int _snd_pcm_lib_alloc_vmalloc_buffer(struct snd_pcm_substream *substream,
				      size_t size, gfp_t gfp_flags)
{
	struct snd_pcm_runtime *runtime;

	if (PCM_RUNTIME_CHECK(substream))
		return -EINVAL;
	runtime = substream->runtime;
	if (runtime->dma_area) {
		if (runtime->dma_bytes >= size)
			return 0; /* already large enough */
		vfree(runtime->dma_area);
	}
	runtime->dma_area = __vmalloc(size, gfp_flags, PAGE_KERNEL);
	if (!runtime->dma_area)
		return -ENOMEM;
	runtime->dma_bytes = size;
	return 1;
}
EXPORT_SYMBOL(_snd_pcm_lib_alloc_vmalloc_buffer);

/**
 * snd_pcm_lib_free_vmalloc_buffer - free vmalloc buffer
 * @substream: the substream with a buffer allocated by
 *	snd_pcm_lib_alloc_vmalloc_buffer()
 *
 * Return: Zero if successful, or a negative error code on failure.
 */
int snd_pcm_lib_free_vmalloc_buffer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime;

	if (PCM_RUNTIME_CHECK(substream))
		return -EINVAL;
	runtime = substream->runtime;
	vfree(runtime->dma_area);
	runtime->dma_area = NULL;
	return 0;
}
EXPORT_SYMBOL(snd_pcm_lib_free_vmalloc_buffer);

/**
 * snd_pcm_lib_get_vmalloc_page - map vmalloc buffer offset to page struct
 * @substream: the substream with a buffer allocated by
 *	snd_pcm_lib_alloc_vmalloc_buffer()
 * @offset: offset in the buffer
 *
 * This function is to be used as the page callback in the PCM ops.
 *
 * Return: The page struct, or %NULL on failure.
 */
struct page *snd_pcm_lib_get_vmalloc_page(struct snd_pcm_substream *substream,
					  unsigned long offset)
{
	return vmalloc_to_page(substream->runtime->dma_area + offset);
}
EXPORT_SYMBOL(snd_pcm_lib_get_vmalloc_page);
