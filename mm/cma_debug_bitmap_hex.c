// SPDX-License-Identifier: GPL-2.0
/*
 * CMA DebugFS Interface
 *
 * Copyright (c) 2015 Sasha Levin <sasha.levin@oracle.com>
 *
 * Copyright (C) 2022 Rockchip Electronics Co. Ltd.
 *
 */

#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/cma.h>
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm_types.h>

#include "cma.h"

static size_t
u32_format_array_hex(char *buf, size_t bufsize, u32 *array, int array_size)
{
	int i = 0;

	while (--array_size >= 0) {
		size_t len;
		char term = (array_size && (++i % 8)) ? ' ' : '\n';

		len = snprintf(buf, bufsize, "%08X%c", *array++, term);
		buf += len;
		bufsize -= len;
	}

	return 0;
}

static int u32_array_open_hex(struct inode *inode, struct file *file)
{
	struct debugfs_u32_array *data = inode->i_private;
	int size, elements = data->n_elements;
	char *buf;

	/*
	 * Max size:
	 *  - 8 digits + ' '/'\n' = 9 bytes per number
	 *  - terminating NUL character
	 */
	size = elements * 9;
	buf = kmalloc(size + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	buf[size] = 0;

	file->private_data = buf;
	u32_format_array_hex(buf, size + 1, data->array, data->n_elements);

	return nonseekable_open(inode, file);
}

static ssize_t
u32_array_read(struct file *file, char __user *buf, size_t len, loff_t *ppos)
{
	size_t size = strlen(file->private_data);

	return simple_read_from_buffer(buf, len, ppos,
					file->private_data, size);
}

static int u32_array_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);

	return 0;
}

static const struct file_operations u32_array_hex_fops = {
	.owner	 = THIS_MODULE,
	.open	 = u32_array_open_hex,
	.release = u32_array_release,
	.read	 = u32_array_read,
	.llseek  = no_llseek,
};

static void debugfs_create_u32_array_hex(const char *name, umode_t mode,
				  struct dentry *parent,
				  struct debugfs_u32_array *array)
{
	debugfs_create_file_unsafe(name, mode, parent, array, &u32_array_hex_fops);
}

static int cma_debugfs_add_one(struct cma *cma, struct dentry *root_dentry)
{
	struct dentry *tmp;
	char name[16];

	scnprintf(name, sizeof(name), "cma-%s", cma->name);

	tmp = debugfs_lookup(name, root_dentry);
	if (!tmp)
		return -EPROBE_DEFER;

	debugfs_create_u32_array_hex("bitmap_hex", 0444, tmp, &cma->dfs_bitmap);

	return 0;
}

static int __init rk_cma_debugfs_init(void)
{
	struct dentry *cma_debugfs_root;
	int ret;
	int i;

	cma_debugfs_root = debugfs_lookup("cma", NULL);
	if (!cma_debugfs_root)
		return -EPROBE_DEFER;

	for (i = 0; i < cma_area_count; i++) {
		ret = cma_debugfs_add_one(&cma_areas[i], cma_debugfs_root);
		if (ret)
			return ret;
	}

	return 0;
}
late_initcall(rk_cma_debugfs_init);
