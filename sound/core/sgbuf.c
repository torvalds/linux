/*
 * Scatter-Gather buffer
 *
 *  Copyright (c) by Takashi Iwai <tiwai@suse.de>
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

#include <linux/config.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <sound/memalloc.h>


/* table entries are align to 32 */
#define SGBUF_TBL_ALIGN		32
#define sgbuf_align_table(tbl)	((((tbl) + SGBUF_TBL_ALIGN - 1) / SGBUF_TBL_ALIGN) * SGBUF_TBL_ALIGN)

int snd_free_sgbuf_pages(struct snd_dma_buffer *dmab)
{
	struct snd_sg_buf *sgbuf = dmab->private_data;
	struct snd_dma_buffer tmpb;
	int i;

	if (! sgbuf)
		return -EINVAL;

	tmpb.dev.type = SNDRV_DMA_TYPE_DEV;
	tmpb.dev.dev = sgbuf->dev;
	for (i = 0; i < sgbuf->pages; i++) {
		tmpb.area = sgbuf->table[i].buf;
		tmpb.addr = sgbuf->table[i].addr;
		tmpb.bytes = PAGE_SIZE;
		snd_dma_free_pages(&tmpb);
	}
	if (dmab->area)
		vunmap(dmab->area);
	dmab->area = NULL;

	kfree(sgbuf->table);
	kfree(sgbuf->page_table);
	kfree(sgbuf);
	dmab->private_data = NULL;
	
	return 0;
}

void *snd_malloc_sgbuf_pages(struct device *device,
			     size_t size, struct snd_dma_buffer *dmab,
			     size_t *res_size)
{
	struct snd_sg_buf *sgbuf;
	unsigned int i, pages;
	struct snd_dma_buffer tmpb;

	dmab->area = NULL;
	dmab->addr = 0;
	dmab->private_data = sgbuf = kmalloc(sizeof(*sgbuf), GFP_KERNEL);
	if (! sgbuf)
		return NULL;
	memset(sgbuf, 0, sizeof(*sgbuf));
	sgbuf->dev = device;
	pages = snd_sgbuf_aligned_pages(size);
	sgbuf->tblsize = sgbuf_align_table(pages);
	sgbuf->table = kmalloc(sizeof(*sgbuf->table) * sgbuf->tblsize, GFP_KERNEL);
	if (! sgbuf->table)
		goto _failed;
	memset(sgbuf->table, 0, sizeof(*sgbuf->table) * sgbuf->tblsize);
	sgbuf->page_table = kmalloc(sizeof(*sgbuf->page_table) * sgbuf->tblsize, GFP_KERNEL);
	if (! sgbuf->page_table)
		goto _failed;
	memset(sgbuf->page_table, 0, sizeof(*sgbuf->page_table) * sgbuf->tblsize);

	/* allocate each page */
	for (i = 0; i < pages; i++) {
		if (snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, device, PAGE_SIZE, &tmpb) < 0) {
			if (res_size == NULL)
				goto _failed;
			*res_size = size = sgbuf->pages * PAGE_SIZE;
			break;
		}
		sgbuf->table[i].buf = tmpb.area;
		sgbuf->table[i].addr = tmpb.addr;
		sgbuf->page_table[i] = virt_to_page(tmpb.area);
		sgbuf->pages++;
	}

	sgbuf->size = size;
	dmab->area = vmap(sgbuf->page_table, sgbuf->pages, VM_MAP, PAGE_KERNEL);
	if (! dmab->area)
		goto _failed;
	return dmab->area;

 _failed:
	snd_free_sgbuf_pages(dmab); /* free the table */
	return NULL;
}
