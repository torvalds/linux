/*
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *  GUS's memory access via proc filesystem
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

#include <sound/driver.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/gus.h>
#include <sound/info.h>

typedef struct gus_proc_private {
	int rom;		/* data are in ROM */
	unsigned int address;
	unsigned int size;
	snd_gus_card_t * gus;
} gus_proc_private_t;

static long snd_gf1_mem_proc_dump(snd_info_entry_t *entry, void *file_private_data,
			          struct file *file, char __user *buf,
			          unsigned long count, unsigned long pos)
{
	long size;
	gus_proc_private_t *priv = entry->private_data;
	snd_gus_card_t *gus = priv->gus;
	int err;

	size = count;
	if (pos + size > priv->size)
		size = (long)priv->size - pos;
	if (size > 0) {
		if ((err = snd_gus_dram_read(gus, buf, pos, size, priv->rom)) < 0)
			return err;
		return size;
	}
	return 0;
}			

static long long snd_gf1_mem_proc_llseek(snd_info_entry_t *entry,
					void *private_file_data,
					struct file *file,
					long long offset,
					int orig)
{
	gus_proc_private_t *priv = entry->private_data;

	switch (orig) {
	case 0:	/* SEEK_SET */
		file->f_pos = offset;
		break;
	case 1:	/* SEEK_CUR */
		file->f_pos += offset;
		break;
	case 2: /* SEEK_END, offset is negative */
		file->f_pos = priv->size + offset;
		break;
	default:
		return -EINVAL;
	}
	if (file->f_pos > priv->size)
		file->f_pos = priv->size;
	return file->f_pos;
}

static void snd_gf1_mem_proc_free(snd_info_entry_t *entry)
{
	gus_proc_private_t *priv = entry->private_data;
	kfree(priv);
}

static struct snd_info_entry_ops snd_gf1_mem_proc_ops = {
	.read = snd_gf1_mem_proc_dump,
	.llseek = snd_gf1_mem_proc_llseek,
};

int snd_gf1_mem_proc_init(snd_gus_card_t * gus)
{
	int idx;
	char name[16];
	gus_proc_private_t *priv;
	snd_info_entry_t *entry;

	for (idx = 0; idx < 4; idx++) {
		if (gus->gf1.mem_alloc.banks_8[idx].size > 0) {
			priv = kzalloc(sizeof(*priv), GFP_KERNEL);
			if (priv == NULL)
				return -ENOMEM;
			priv->gus = gus;
			sprintf(name, "gus-ram-%i", idx);
			if (! snd_card_proc_new(gus->card, name, &entry)) {
				entry->content = SNDRV_INFO_CONTENT_DATA;
				entry->private_data = priv;
				entry->private_free = snd_gf1_mem_proc_free;
				entry->c.ops = &snd_gf1_mem_proc_ops;
				priv->address = gus->gf1.mem_alloc.banks_8[idx].address;
				priv->size = entry->size = gus->gf1.mem_alloc.banks_8[idx].size;
			}
		}
	}
	for (idx = 0; idx < 4; idx++) {
		if (gus->gf1.rom_present & (1 << idx)) {
			priv = kzalloc(sizeof(*priv), GFP_KERNEL);
			if (priv == NULL)
				return -ENOMEM;
			priv->rom = 1;
			priv->gus = gus;
			sprintf(name, "gus-rom-%i", idx);
			if (! snd_card_proc_new(gus->card, name, &entry)) {
				entry->content = SNDRV_INFO_CONTENT_DATA;
				entry->private_data = priv;
				entry->private_free = snd_gf1_mem_proc_free;
				entry->c.ops = &snd_gf1_mem_proc_ops;
				priv->address = idx * 4096 * 1024;
				priv->size = entry->size = gus->gf1.rom_memory;
			}
		}
	}
	return 0;
}
