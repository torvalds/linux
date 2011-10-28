/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *                   Takashi Iwai <tiwai@suse.de>
 * 
 *  Generic memory allocators
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/seq_file.h>
#include <asm/uaccess.h>
#include <linux/dma-mapping.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <sound/memalloc.h>


MODULE_AUTHOR("Takashi Iwai <tiwai@suse.de>, Jaroslav Kysela <perex@perex.cz>");
MODULE_DESCRIPTION("Memory allocator for ALSA system.");
MODULE_LICENSE("GPL");


/*
 */

static DEFINE_MUTEX(list_mutex);
static LIST_HEAD(mem_list_head);

/* buffer preservation list */
struct snd_mem_list {
	struct snd_dma_buffer buffer;
	unsigned int id;
	struct list_head list;
};

/* id for pre-allocated buffers */
#define SNDRV_DMA_DEVICE_UNUSED (unsigned int)-1

/*
 *
 *  Generic memory allocators
 *
 */

static long snd_allocated_pages; /* holding the number of allocated pages */

static inline void inc_snd_pages(int order)
{
	snd_allocated_pages += 1 << order;
}

static inline void dec_snd_pages(int order)
{
	snd_allocated_pages -= 1 << order;
}

/**
 * snd_malloc_pages - allocate pages with the given size
 * @size: the size to allocate in bytes
 * @gfp_flags: the allocation conditions, GFP_XXX
 *
 * Allocates the physically contiguous pages with the given size.
 *
 * Returns the pointer of the buffer, or NULL if no enoguh memory.
 */
void *snd_malloc_pages(size_t size, gfp_t gfp_flags)
{
	int pg;
	void *res;

	if (WARN_ON(!size))
		return NULL;
	if (WARN_ON(!gfp_flags))
		return NULL;
	gfp_flags |= __GFP_COMP;	/* compound page lets parts be mapped */
	pg = get_order(size);
	if ((res = (void *) __get_free_pages(gfp_flags, pg)) != NULL)
		inc_snd_pages(pg);
	return res;
}

/**
 * snd_free_pages - release the pages
 * @ptr: the buffer pointer to release
 * @size: the allocated buffer size
 *
 * Releases the buffer allocated via snd_malloc_pages().
 */
void snd_free_pages(void *ptr, size_t size)
{
	int pg;

	if (ptr == NULL)
		return;
	pg = get_order(size);
	dec_snd_pages(pg);
	free_pages((unsigned long) ptr, pg);
}

/*
 *
 *  Bus-specific memory allocators
 *
 */

#ifdef CONFIG_HAS_DMA
/* allocate the coherent DMA pages */
static void *snd_malloc_dev_pages(struct device *dev, size_t size, dma_addr_t *dma)
{
	int pg;
	void *res;
	gfp_t gfp_flags;

	if (WARN_ON(!dma))
		return NULL;
	pg = get_order(size);
	gfp_flags = GFP_KERNEL
		| __GFP_COMP	/* compound page lets parts be mapped */
		| __GFP_NORETRY /* don't trigger OOM-killer */
		| __GFP_NOWARN; /* no stack trace print - this call is non-critical */
	res = dma_alloc_coherent(dev, PAGE_SIZE << pg, dma, gfp_flags);
	if (res != NULL)
		inc_snd_pages(pg);

	return res;
}

/* free the coherent DMA pages */
static void snd_free_dev_pages(struct device *dev, size_t size, void *ptr,
			       dma_addr_t dma)
{
	int pg;

	if (ptr == NULL)
		return;
	pg = get_order(size);
	dec_snd_pages(pg);
	dma_free_coherent(dev, PAGE_SIZE << pg, ptr, dma);
}
#endif /* CONFIG_HAS_DMA */

/*
 *
 *  ALSA generic memory management
 *
 */


/**
 * snd_dma_alloc_pages - allocate the buffer area according to the given type
 * @type: the DMA buffer type
 * @device: the device pointer
 * @size: the buffer size to allocate
 * @dmab: buffer allocation record to store the allocated data
 *
 * Calls the memory-allocator function for the corresponding
 * buffer type.
 * 
 * Returns zero if the buffer with the given size is allocated successfuly,
 * other a negative value at error.
 */
int snd_dma_alloc_pages(int type, struct device *device, size_t size,
			struct snd_dma_buffer *dmab)
{
	if (WARN_ON(!size))
		return -ENXIO;
	if (WARN_ON(!dmab))
		return -ENXIO;

	dmab->dev.type = type;
	dmab->dev.dev = device;
	dmab->bytes = 0;
	switch (type) {
	case SNDRV_DMA_TYPE_CONTINUOUS:
		dmab->area = snd_malloc_pages(size,
					(__force gfp_t)(unsigned long)device);
		dmab->addr = 0;
		break;
#ifdef CONFIG_HAS_DMA
	case SNDRV_DMA_TYPE_DEV:
		dmab->area = snd_malloc_dev_pages(device, size, &dmab->addr);
		break;
#endif
#ifdef CONFIG_SND_DMA_SGBUF
	case SNDRV_DMA_TYPE_DEV_SG:
		snd_malloc_sgbuf_pages(device, size, dmab, NULL);
		break;
#endif
	default:
		printk(KERN_ERR "snd-malloc: invalid device type %d\n", type);
		dmab->area = NULL;
		dmab->addr = 0;
		return -ENXIO;
	}
	if (! dmab->area)
		return -ENOMEM;
	dmab->bytes = size;
	return 0;
}

/**
 * snd_dma_alloc_pages_fallback - allocate the buffer area according to the given type with fallback
 * @type: the DMA buffer type
 * @device: the device pointer
 * @size: the buffer size to allocate
 * @dmab: buffer allocation record to store the allocated data
 *
 * Calls the memory-allocator function for the corresponding
 * buffer type.  When no space is left, this function reduces the size and
 * tries to allocate again.  The size actually allocated is stored in
 * res_size argument.
 * 
 * Returns zero if the buffer with the given size is allocated successfuly,
 * other a negative value at error.
 */
int snd_dma_alloc_pages_fallback(int type, struct device *device, size_t size,
				 struct snd_dma_buffer *dmab)
{
	int err;

	while ((err = snd_dma_alloc_pages(type, device, size, dmab)) < 0) {
		size_t aligned_size;
		if (err != -ENOMEM)
			return err;
		if (size <= PAGE_SIZE)
			return -ENOMEM;
		aligned_size = PAGE_SIZE << get_order(size);
		if (size != aligned_size)
			size = aligned_size;
		else
			size >>= 1;
	}
	if (! dmab->area)
		return -ENOMEM;
	return 0;
}


/**
 * snd_dma_free_pages - release the allocated buffer
 * @dmab: the buffer allocation record to release
 *
 * Releases the allocated buffer via snd_dma_alloc_pages().
 */
void snd_dma_free_pages(struct snd_dma_buffer *dmab)
{
	switch (dmab->dev.type) {
	case SNDRV_DMA_TYPE_CONTINUOUS:
		snd_free_pages(dmab->area, dmab->bytes);
		break;
#ifdef CONFIG_HAS_DMA
	case SNDRV_DMA_TYPE_DEV:
		snd_free_dev_pages(dmab->dev.dev, dmab->bytes, dmab->area, dmab->addr);
		break;
#endif
#ifdef CONFIG_SND_DMA_SGBUF
	case SNDRV_DMA_TYPE_DEV_SG:
		snd_free_sgbuf_pages(dmab);
		break;
#endif
	default:
		printk(KERN_ERR "snd-malloc: invalid device type %d\n", dmab->dev.type);
	}
}


/**
 * snd_dma_get_reserved - get the reserved buffer for the given device
 * @dmab: the buffer allocation record to store
 * @id: the buffer id
 *
 * Looks for the reserved-buffer list and re-uses if the same buffer
 * is found in the list.  When the buffer is found, it's removed from the free list.
 *
 * Returns the size of buffer if the buffer is found, or zero if not found.
 */
size_t snd_dma_get_reserved_buf(struct snd_dma_buffer *dmab, unsigned int id)
{
	struct snd_mem_list *mem;

	if (WARN_ON(!dmab))
		return 0;

	mutex_lock(&list_mutex);
	list_for_each_entry(mem, &mem_list_head, list) {
		if (mem->id == id &&
		    (mem->buffer.dev.dev == NULL || dmab->dev.dev == NULL ||
		     ! memcmp(&mem->buffer.dev, &dmab->dev, sizeof(dmab->dev)))) {
			struct device *dev = dmab->dev.dev;
			list_del(&mem->list);
			*dmab = mem->buffer;
			if (dmab->dev.dev == NULL)
				dmab->dev.dev = dev;
			kfree(mem);
			mutex_unlock(&list_mutex);
			return dmab->bytes;
		}
	}
	mutex_unlock(&list_mutex);
	return 0;
}

/**
 * snd_dma_reserve_buf - reserve the buffer
 * @dmab: the buffer to reserve
 * @id: the buffer id
 *
 * Reserves the given buffer as a reserved buffer.
 * 
 * Returns zero if successful, or a negative code at error.
 */
int snd_dma_reserve_buf(struct snd_dma_buffer *dmab, unsigned int id)
{
	struct snd_mem_list *mem;

	if (WARN_ON(!dmab))
		return -EINVAL;
	mem = kmalloc(sizeof(*mem), GFP_KERNEL);
	if (! mem)
		return -ENOMEM;
	mutex_lock(&list_mutex);
	mem->buffer = *dmab;
	mem->id = id;
	list_add_tail(&mem->list, &mem_list_head);
	mutex_unlock(&list_mutex);
	return 0;
}

/*
 * purge all reserved buffers
 */
static void free_all_reserved_pages(void)
{
	struct list_head *p;
	struct snd_mem_list *mem;

	mutex_lock(&list_mutex);
	while (! list_empty(&mem_list_head)) {
		p = mem_list_head.next;
		mem = list_entry(p, struct snd_mem_list, list);
		list_del(p);
		snd_dma_free_pages(&mem->buffer);
		kfree(mem);
	}
	mutex_unlock(&list_mutex);
}


#ifdef CONFIG_PROC_FS
/*
 * proc file interface
 */
#define SND_MEM_PROC_FILE	"driver/snd-page-alloc"
static struct proc_dir_entry *snd_mem_proc;

static int snd_mem_proc_read(struct seq_file *seq, void *offset)
{
	long pages = snd_allocated_pages >> (PAGE_SHIFT-12);
	struct snd_mem_list *mem;
	int devno;
	static char *types[] = { "UNKNOWN", "CONT", "DEV", "DEV-SG" };

	mutex_lock(&list_mutex);
	seq_printf(seq, "pages  : %li bytes (%li pages per %likB)\n",
		   pages * PAGE_SIZE, pages, PAGE_SIZE / 1024);
	devno = 0;
	list_for_each_entry(mem, &mem_list_head, list) {
		devno++;
		seq_printf(seq, "buffer %d : ID %08x : type %s\n",
			   devno, mem->id, types[mem->buffer.dev.type]);
		seq_printf(seq, "  addr = 0x%lx, size = %d bytes\n",
			   (unsigned long)mem->buffer.addr,
			   (int)mem->buffer.bytes);
	}
	mutex_unlock(&list_mutex);
	return 0;
}

static int snd_mem_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, snd_mem_proc_read, NULL);
}

/* FIXME: for pci only - other bus? */
#ifdef CONFIG_PCI
#define gettoken(bufp) strsep(bufp, " \t\n")

static ssize_t snd_mem_proc_write(struct file *file, const char __user * buffer,
				  size_t count, loff_t * ppos)
{
	char buf[128];
	char *token, *p;

	if (count > sizeof(buf) - 1)
		return -EINVAL;
	if (copy_from_user(buf, buffer, count))
		return -EFAULT;
	buf[count] = '\0';

	p = buf;
	token = gettoken(&p);
	if (! token || *token == '#')
		return count;
	if (strcmp(token, "add") == 0) {
		char *endp;
		int vendor, device, size, buffers;
		long mask;
		int i, alloced;
		struct pci_dev *pci;

		if ((token = gettoken(&p)) == NULL ||
		    (vendor = simple_strtol(token, NULL, 0)) <= 0 ||
		    (token = gettoken(&p)) == NULL ||
		    (device = simple_strtol(token, NULL, 0)) <= 0 ||
		    (token = gettoken(&p)) == NULL ||
		    (mask = simple_strtol(token, NULL, 0)) < 0 ||
		    (token = gettoken(&p)) == NULL ||
		    (size = memparse(token, &endp)) < 64*1024 ||
		    size > 16*1024*1024 /* too big */ ||
		    (token = gettoken(&p)) == NULL ||
		    (buffers = simple_strtol(token, NULL, 0)) <= 0 ||
		    buffers > 4) {
			printk(KERN_ERR "snd-page-alloc: invalid proc write format\n");
			return count;
		}
		vendor &= 0xffff;
		device &= 0xffff;

		alloced = 0;
		pci = NULL;
		while ((pci = pci_get_device(vendor, device, pci)) != NULL) {
			if (mask > 0 && mask < 0xffffffff) {
				if (pci_set_dma_mask(pci, mask) < 0 ||
				    pci_set_consistent_dma_mask(pci, mask) < 0) {
					printk(KERN_ERR "snd-page-alloc: cannot set DMA mask %lx for pci %04x:%04x\n", mask, vendor, device);
					pci_dev_put(pci);
					return count;
				}
			}
			for (i = 0; i < buffers; i++) {
				struct snd_dma_buffer dmab;
				memset(&dmab, 0, sizeof(dmab));
				if (snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, snd_dma_pci_data(pci),
							size, &dmab) < 0) {
					printk(KERN_ERR "snd-page-alloc: cannot allocate buffer pages (size = %d)\n", size);
					pci_dev_put(pci);
					return count;
				}
				snd_dma_reserve_buf(&dmab, snd_dma_pci_buf_id(pci));
			}
			alloced++;
		}
		if (! alloced) {
			for (i = 0; i < buffers; i++) {
				struct snd_dma_buffer dmab;
				memset(&dmab, 0, sizeof(dmab));
				/* FIXME: We can allocate only in ZONE_DMA
				 * without a device pointer!
				 */
				if (snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, NULL,
							size, &dmab) < 0) {
					printk(KERN_ERR "snd-page-alloc: cannot allocate buffer pages (size = %d)\n", size);
					break;
				}
				snd_dma_reserve_buf(&dmab, (unsigned int)((vendor << 16) | device));
			}
		}
	} else if (strcmp(token, "erase") == 0)
		/* FIXME: need for releasing each buffer chunk? */
		free_all_reserved_pages();
	else
		printk(KERN_ERR "snd-page-alloc: invalid proc cmd\n");
	return count;
}
#endif /* CONFIG_PCI */

static const struct file_operations snd_mem_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= snd_mem_proc_open,
	.read		= seq_read,
#ifdef CONFIG_PCI
	.write		= snd_mem_proc_write,
#endif
	.llseek		= seq_lseek,
	.release	= single_release,
};

#endif /* CONFIG_PROC_FS */

/*
 * module entry
 */

static int __init snd_mem_init(void)
{
#ifdef CONFIG_PROC_FS
	snd_mem_proc = proc_create(SND_MEM_PROC_FILE, 0644, NULL,
				   &snd_mem_proc_fops);
#endif
	return 0;
}

static void __exit snd_mem_exit(void)
{
	remove_proc_entry(SND_MEM_PROC_FILE, NULL);
	free_all_reserved_pages();
	if (snd_allocated_pages > 0)
		printk(KERN_ERR "snd-malloc: Memory leak?  pages not freed = %li\n", snd_allocated_pages);
}


module_init(snd_mem_init)
module_exit(snd_mem_exit)


/*
 * exports
 */
EXPORT_SYMBOL(snd_dma_alloc_pages);
EXPORT_SYMBOL(snd_dma_alloc_pages_fallback);
EXPORT_SYMBOL(snd_dma_free_pages);

EXPORT_SYMBOL(snd_dma_get_reserved_buf);
EXPORT_SYMBOL(snd_dma_reserve_buf);

EXPORT_SYMBOL(snd_malloc_pages);
EXPORT_SYMBOL(snd_free_pages);
