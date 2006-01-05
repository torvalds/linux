/*
 * Functions for the OPL4 proc file
 * Copyright (c) 2003 by Clemens Ladisch <clemens@ladisch.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include "opl4_local.h"
#include <linux/vmalloc.h>
#include <sound/info.h>

#ifdef CONFIG_PROC_FS

static int snd_opl4_mem_proc_open(struct snd_info_entry *entry,
				  unsigned short mode, void **file_private_data)
{
	struct snd_opl4 *opl4 = entry->private_data;

	down(&opl4->access_mutex);
	if (opl4->memory_access) {
		up(&opl4->access_mutex);
		return -EBUSY;
	}
	opl4->memory_access++;
	up(&opl4->access_mutex);
	return 0;
}

static int snd_opl4_mem_proc_release(struct snd_info_entry *entry,
				     unsigned short mode, void *file_private_data)
{
	struct snd_opl4 *opl4 = entry->private_data;

	down(&opl4->access_mutex);
	opl4->memory_access--;
	up(&opl4->access_mutex);
	return 0;
}

static long snd_opl4_mem_proc_read(struct snd_info_entry *entry, void *file_private_data,
				   struct file *file, char __user *_buf,
				   unsigned long count, unsigned long pos)
{
	struct snd_opl4 *opl4 = entry->private_data;
	long size;
	char* buf;

	size = count;
	if (pos + size > entry->size)
		size = entry->size - pos;
	if (size > 0) {
		buf = vmalloc(size);
		if (!buf)
			return -ENOMEM;
		snd_opl4_read_memory(opl4, buf, pos, size);
		if (copy_to_user(_buf, buf, size)) {
			vfree(buf);
			return -EFAULT;
		}
		vfree(buf);
		return size;
	}
	return 0;
}

static long snd_opl4_mem_proc_write(struct snd_info_entry *entry, void *file_private_data,
				    struct file *file, const char __user *_buf,
				    unsigned long count, unsigned long pos)
{
	struct snd_opl4 *opl4 = entry->private_data;
	long size;
	char *buf;

	size = count;
	if (pos + size > entry->size)
		size = entry->size - pos;
	if (size > 0) {
		buf = vmalloc(size);
		if (!buf)
			return -ENOMEM;
		if (copy_from_user(buf, _buf, size)) {
			vfree(buf);
			return -EFAULT;
		}
		snd_opl4_write_memory(opl4, buf, pos, size);
		vfree(buf);
		return size;
	}
	return 0;
}

static long long snd_opl4_mem_proc_llseek(struct snd_info_entry *entry, void *file_private_data,
					  struct file *file, long long offset, int orig)
{
	switch (orig) {
	case 0: /* SEEK_SET */
		file->f_pos = offset;
		break;
	case 1: /* SEEK_CUR */
		file->f_pos += offset;
		break;
	case 2: /* SEEK_END, offset is negative */
		file->f_pos = entry->size + offset;
		break;
	default:
		return -EINVAL;
	}
	if (file->f_pos > entry->size)
		file->f_pos = entry->size;
	return file->f_pos;
}

static struct snd_info_entry_ops snd_opl4_mem_proc_ops = {
	.open = snd_opl4_mem_proc_open,
	.release = snd_opl4_mem_proc_release,
	.read = snd_opl4_mem_proc_read,
	.write = snd_opl4_mem_proc_write,
	.llseek = snd_opl4_mem_proc_llseek,
};

int snd_opl4_create_proc(struct snd_opl4 *opl4)
{
	struct snd_info_entry *entry;

	entry = snd_info_create_card_entry(opl4->card, "opl4-mem", opl4->card->proc_root);
	if (entry) {
		if (opl4->hardware < OPL3_HW_OPL4_ML) {
			/* OPL4 can access 4 MB external ROM/SRAM */
			entry->mode |= S_IWUSR;
			entry->size = 4 * 1024 * 1024;
		} else {
			/* OPL4-ML has 1 MB internal ROM */
			entry->size = 1 * 1024 * 1024;
		}
		entry->content = SNDRV_INFO_CONTENT_DATA;
		entry->c.ops = &snd_opl4_mem_proc_ops;
		entry->module = THIS_MODULE;
		entry->private_data = opl4;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	opl4->proc_entry = entry;
	return 0;
}

void snd_opl4_free_proc(struct snd_opl4 *opl4)
{
	if (opl4->proc_entry)
		snd_info_unregister(opl4->proc_entry);
}

#endif /* CONFIG_PROC_FS */
