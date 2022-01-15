// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *  GUS's memory allocation routines / bottom layer
 */

#include <linux/slab.h>
#include <linux/string.h>
#include <sound/core.h>
#include <sound/gus.h>
#include <sound/info.h>

#ifdef CONFIG_SND_DEBUG
static void snd_gf1_mem_info_read(struct snd_info_entry *entry, 
				  struct snd_info_buffer *buffer);
#endif

void snd_gf1_mem_lock(struct snd_gf1_mem * alloc, int xup)
{
	if (!xup) {
		mutex_lock(&alloc->memory_mutex);
	} else {
		mutex_unlock(&alloc->memory_mutex);
	}
}

static struct snd_gf1_mem_block *snd_gf1_mem_xalloc(struct snd_gf1_mem * alloc,
					       struct snd_gf1_mem_block * block)
{
	struct snd_gf1_mem_block *pblock, *nblock;

	nblock = kmalloc(sizeof(struct snd_gf1_mem_block), GFP_KERNEL);
	if (nblock == NULL)
		return NULL;
	*nblock = *block;
	pblock = alloc->first;
	while (pblock) {
		if (pblock->ptr > nblock->ptr) {
			nblock->prev = pblock->prev;
			nblock->next = pblock;
			pblock->prev = nblock;
			if (pblock == alloc->first)
				alloc->first = nblock;
			else
				nblock->prev->next = nblock;
			mutex_unlock(&alloc->memory_mutex);
			return NULL;
		}
		pblock = pblock->next;
	}
	nblock->next = NULL;
	if (alloc->last == NULL) {
		nblock->prev = NULL;
		alloc->first = alloc->last = nblock;
	} else {
		nblock->prev = alloc->last;
		alloc->last->next = nblock;
		alloc->last = nblock;
	}
	return nblock;
}

int snd_gf1_mem_xfree(struct snd_gf1_mem * alloc, struct snd_gf1_mem_block * block)
{
	if (block->share) {	/* ok.. shared block */
		block->share--;
		mutex_unlock(&alloc->memory_mutex);
		return 0;
	}
	if (alloc->first == block) {
		alloc->first = block->next;
		if (block->next)
			block->next->prev = NULL;
	} else {
		block->prev->next = block->next;
		if (block->next)
			block->next->prev = block->prev;
	}
	if (alloc->last == block) {
		alloc->last = block->prev;
		if (block->prev)
			block->prev->next = NULL;
	} else {
		block->next->prev = block->prev;
		if (block->prev)
			block->prev->next = block->next;
	}
	kfree(block->name);
	kfree(block);
	return 0;
}

static struct snd_gf1_mem_block *snd_gf1_mem_look(struct snd_gf1_mem * alloc,
					     unsigned int address)
{
	struct snd_gf1_mem_block *block;

	for (block = alloc->first; block; block = block->next) {
		if (block->ptr == address) {
			return block;
		}
	}
	return NULL;
}

static struct snd_gf1_mem_block *snd_gf1_mem_share(struct snd_gf1_mem * alloc,
					      unsigned int *share_id)
{
	struct snd_gf1_mem_block *block;

	if (!share_id[0] && !share_id[1] &&
	    !share_id[2] && !share_id[3])
		return NULL;
	for (block = alloc->first; block; block = block->next)
		if (!memcmp(share_id, block->share_id,
				sizeof(block->share_id)))
			return block;
	return NULL;
}

static int snd_gf1_mem_find(struct snd_gf1_mem * alloc,
			    struct snd_gf1_mem_block * block,
			    unsigned int size, int w_16, int align)
{
	struct snd_gf1_bank_info *info = w_16 ? alloc->banks_16 : alloc->banks_8;
	unsigned int idx, boundary;
	int size1;
	struct snd_gf1_mem_block *pblock;
	unsigned int ptr1, ptr2;

	if (w_16 && align < 2)
		align = 2;
	block->flags = w_16 ? SNDRV_GF1_MEM_BLOCK_16BIT : 0;
	block->owner = SNDRV_GF1_MEM_OWNER_DRIVER;
	block->share = 0;
	block->share_id[0] = block->share_id[1] =
	block->share_id[2] = block->share_id[3] = 0;
	block->name = NULL;
	block->prev = block->next = NULL;
	for (pblock = alloc->first, idx = 0; pblock; pblock = pblock->next) {
		while (pblock->ptr >= (boundary = info[idx].address + info[idx].size))
			idx++;
		while (pblock->ptr + pblock->size >= (boundary = info[idx].address + info[idx].size))
			idx++;
		ptr2 = boundary;
		if (pblock->next) {
			if (pblock->ptr + pblock->size == pblock->next->ptr)
				continue;
			if (pblock->next->ptr < boundary)
				ptr2 = pblock->next->ptr;
		}
		ptr1 = ALIGN(pblock->ptr + pblock->size, align);
		if (ptr1 >= ptr2)
			continue;
		size1 = ptr2 - ptr1;
		if ((int)size <= size1) {
			block->ptr = ptr1;
			block->size = size;
			return 0;
		}
	}
	while (++idx < 4) {
		if (size <= info[idx].size) {
			/* I assume that bank address is already aligned.. */
			block->ptr = info[idx].address;
			block->size = size;
			return 0;
		}
	}
	return -ENOMEM;
}

struct snd_gf1_mem_block *snd_gf1_mem_alloc(struct snd_gf1_mem * alloc, int owner,
				       char *name, int size, int w_16, int align,
				       unsigned int *share_id)
{
	struct snd_gf1_mem_block block, *nblock;

	snd_gf1_mem_lock(alloc, 0);
	if (share_id != NULL) {
		nblock = snd_gf1_mem_share(alloc, share_id);
		if (nblock != NULL) {
			if (size != (int)nblock->size) {
				/* TODO: remove in the future */
				snd_printk(KERN_ERR "snd_gf1_mem_alloc - share: sizes differ\n");
				goto __std;
			}
			nblock->share++;
			snd_gf1_mem_lock(alloc, 1);
			return NULL;
		}
	}
      __std:
	if (snd_gf1_mem_find(alloc, &block, size, w_16, align) < 0) {
		snd_gf1_mem_lock(alloc, 1);
		return NULL;
	}
	if (share_id != NULL)
		memcpy(&block.share_id, share_id, sizeof(block.share_id));
	block.owner = owner;
	block.name = kstrdup(name, GFP_KERNEL);
	nblock = snd_gf1_mem_xalloc(alloc, &block);
	snd_gf1_mem_lock(alloc, 1);
	return nblock;
}

int snd_gf1_mem_free(struct snd_gf1_mem * alloc, unsigned int address)
{
	int result;
	struct snd_gf1_mem_block *block;

	snd_gf1_mem_lock(alloc, 0);
	block = snd_gf1_mem_look(alloc, address);
	if (block) {
		result = snd_gf1_mem_xfree(alloc, block);
		snd_gf1_mem_lock(alloc, 1);
		return result;
	}
	snd_gf1_mem_lock(alloc, 1);
	return -EINVAL;
}

int snd_gf1_mem_init(struct snd_gus_card * gus)
{
	struct snd_gf1_mem *alloc;
	struct snd_gf1_mem_block block;

	alloc = &gus->gf1.mem_alloc;
	mutex_init(&alloc->memory_mutex);
	alloc->first = alloc->last = NULL;
	if (!gus->gf1.memory)
		return 0;

	memset(&block, 0, sizeof(block));
	block.owner = SNDRV_GF1_MEM_OWNER_DRIVER;
	if (gus->gf1.enh_mode) {
		block.ptr = 0;
		block.size = 1024;
		block.name = kstrdup("InterWave LFOs", GFP_KERNEL);
		if (snd_gf1_mem_xalloc(alloc, &block) == NULL)
			return -ENOMEM;
	}
	block.ptr = gus->gf1.default_voice_address;
	block.size = 4;
	block.name = kstrdup("Voice default (NULL's)", GFP_KERNEL);
	if (snd_gf1_mem_xalloc(alloc, &block) == NULL)
		return -ENOMEM;
#ifdef CONFIG_SND_DEBUG
	snd_card_ro_proc_new(gus->card, "gusmem", gus, snd_gf1_mem_info_read);
#endif
	return 0;
}

int snd_gf1_mem_done(struct snd_gus_card * gus)
{
	struct snd_gf1_mem *alloc;
	struct snd_gf1_mem_block *block, *nblock;

	alloc = &gus->gf1.mem_alloc;
	block = alloc->first;
	while (block) {
		nblock = block->next;
		snd_gf1_mem_xfree(alloc, block);
		block = nblock;
	}
	return 0;
}

#ifdef CONFIG_SND_DEBUG
static void snd_gf1_mem_info_read(struct snd_info_entry *entry, 
				  struct snd_info_buffer *buffer)
{
	struct snd_gus_card *gus;
	struct snd_gf1_mem *alloc;
	struct snd_gf1_mem_block *block;
	unsigned int total, used;
	int i;

	gus = entry->private_data;
	alloc = &gus->gf1.mem_alloc;
	mutex_lock(&alloc->memory_mutex);
	snd_iprintf(buffer, "8-bit banks       : \n    ");
	for (i = 0; i < 4; i++)
		snd_iprintf(buffer, "0x%06x (%04ik)%s", alloc->banks_8[i].address, alloc->banks_8[i].size >> 10, i + 1 < 4 ? "," : "");
	snd_iprintf(buffer, "\n"
		    "16-bit banks      : \n    ");
	for (i = total = 0; i < 4; i++) {
		snd_iprintf(buffer, "0x%06x (%04ik)%s", alloc->banks_16[i].address, alloc->banks_16[i].size >> 10, i + 1 < 4 ? "," : "");
		total += alloc->banks_16[i].size;
	}
	snd_iprintf(buffer, "\n");
	used = 0;
	for (block = alloc->first, i = 0; block; block = block->next, i++) {
		used += block->size;
		snd_iprintf(buffer, "Block %i onboard 0x%x size %i (0x%x):\n", i, block->ptr, block->size, block->size);
		if (block->share ||
		    block->share_id[0] || block->share_id[1] ||
		    block->share_id[2] || block->share_id[3])
			snd_iprintf(buffer, "  Share           : %i [id0 0x%x] [id1 0x%x] [id2 0x%x] [id3 0x%x]\n",
				block->share,
				block->share_id[0], block->share_id[1],
				block->share_id[2], block->share_id[3]);
		snd_iprintf(buffer, "  Flags           :%s\n",
		block->flags & SNDRV_GF1_MEM_BLOCK_16BIT ? " 16-bit" : "");
		snd_iprintf(buffer, "  Owner           : ");
		switch (block->owner) {
		case SNDRV_GF1_MEM_OWNER_DRIVER:
			snd_iprintf(buffer, "driver - %s\n", block->name);
			break;
		case SNDRV_GF1_MEM_OWNER_WAVE_SIMPLE:
			snd_iprintf(buffer, "SIMPLE wave\n");
			break;
		case SNDRV_GF1_MEM_OWNER_WAVE_GF1:
			snd_iprintf(buffer, "GF1 wave\n");
			break;
		case SNDRV_GF1_MEM_OWNER_WAVE_IWFFFF:
			snd_iprintf(buffer, "IWFFFF wave\n");
			break;
		default:
			snd_iprintf(buffer, "unknown\n");
		}
	}
	snd_iprintf(buffer, "  Total: memory = %i, used = %i, free = %i\n",
		    total, used, total - used);
	mutex_unlock(&alloc->memory_mutex);
#if 0
	ultra_iprintf(buffer, "  Verify: free = %i, max 8-bit block = %i, max 16-bit block = %i\n",
		      ultra_memory_free_size(card, &card->gf1.mem_alloc),
		  ultra_memory_free_block(card, &card->gf1.mem_alloc, 0),
		 ultra_memory_free_block(card, &card->gf1.mem_alloc, 1));
#endif
}
#endif
