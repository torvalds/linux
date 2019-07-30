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
#include <linux/export.h>
#include <sound/info.h>

static int snd_opl4_mem_proc_open(struct snd_info_entry *entry,
				  unsigned short mode, void **file_private_data)
{
	struct snd_opl4 *opl4 = entry->private_data;

	mutex_lock(&opl4->access_mutex);
	if (opl4->memory_access) {
		mutex_unlock(&opl4->access_mutex);
		return -EBUSY;
	}
	opl4->memory_access++;
	mutex_unlock(&opl4->access_mutex);
	return 0;
}

static int snd_opl4_mem_proc_release(struct snd_info_entry *entry,
				     unsigned short mode, void *file_private_data)
{
	struct snd_opl4 *opl4 = entry->private_data;

	mutex_lock(&opl4->access_mutex);
	opl4->memory_access--;
	mutex_unlock(&opl4->access_mutex);
	return 0;
}

static ssize_t snd_opl4_mem_proc_read(struct snd_info_entry *entry,
				      void *file_private_data,
				      struct file *file, char __user *_buf,
				      size_t count, loff_t pos)
{
	struct snd_opl4 *opl4 = entry->private_data;
	char* buf;

	buf = vmalloc(count);
	if (!buf)
		return -ENOMEM;
	snd_opl4_read_memory(opl4, buf, pos, count);
	if (copy_to_user(_buf, buf, count)) {
		vfree(buf);
		return -EFAULT;
	}
	vfree(buf);
	return count;
}

static ssize_t snd_opl4_mem_proc_write(struct snd_info_entry *entry,
				       void *file_private_data,
				       struct file *file,
				       const char __user *_buf,
				       size_t count, loff_t pos)
{
	struct snd_opl4 *opl4 = entry->private_data;
	char *buf;

	buf = vmalloc(count);
	if (!buf)
		return -ENOMEM;
	if (copy_from_user(buf, _buf, count)) {
		vfree(buf);
		return -EFAULT;
	}
	snd_opl4_write_memory(opl4, buf, pos, count);
	vfree(buf);
	return count;
}

static struct snd_info_entry_ops snd_opl4_mem_proc_ops = {
	.open = snd_opl4_mem_proc_open,
	.release = snd_opl4_mem_proc_release,
	.read = snd_opl4_mem_proc_read,
	.write = snd_opl4_mem_proc_write,
};

int snd_opl4_create_proc(struct snd_opl4 *opl4)
{
	struct snd_info_entry *entry;

	entry = snd_info_create_card_entry(opl4->card, "opl4-mem", opl4->card->proc_root);
	if (entry) {
		if (opl4->hardware < OPL3_HW_OPL4_ML) {
			/* OPL4 can access 4 MB external ROM/SRAM */
			entry->mode |= 0200;
			entry->size = 4 * 1024 * 1024;
		} else {
			/* OPL4-ML has 1 MB internal ROM */
			entry->size = 1 * 1024 * 1024;
		}
		entry->content = SNDRV_INFO_CONTENT_DATA;
		entry->c.ops = &snd_opl4_mem_proc_ops;
		entry->module = THIS_MODULE;
		entry->private_data = opl4;
	}
	opl4->proc_entry = entry;
	return 0;
}

void snd_opl4_free_proc(struct snd_opl4 *opl4)
{
	snd_info_free_entry(opl4->proc_entry);
}
