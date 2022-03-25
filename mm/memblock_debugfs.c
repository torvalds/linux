// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Debugfs for reserved memory blocks.
 *
 * Copyright (C) 2022 Rockchip Electronics Co. Ltd.
 */

#include <linux/init.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/memblock.h>

#define K(size) ((unsigned long)((size) >> 10))

static int memblock_debugfs_show(struct seq_file *m, void *private)
{
	struct memblock_type *type = m->private;
	struct memblock_region *reg;
	int i;
	phys_addr_t end;
	unsigned long z = 0, t = 0;

	for (i = 0; i < type->cnt; i++) {
		reg = &type->regions[i];
		end = reg->base + reg->size - 1;
		z = (unsigned long)reg->size;
		t += z;

		seq_printf(m, "%4d: ", i);
		seq_printf(m, "%pa..%pa (%10lu %s)\n", &reg->base, &end,
			   (z >= 1024) ? (K(z)) : z,
			   (z >= 1024) ? "KiB" : "Bytes");
	}
	seq_printf(m, "Total: %lu KiB\n", K(t));

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(memblock_debugfs);

static int __init memblock_debugfs_init(void)
{
	struct dentry *root = debugfs_lookup("memblock", NULL);

	if (!root)
		return -EPROBE_DEFER;

	debugfs_create_file("reserved_size", 0444, root,
			    &memblock.reserved, &memblock_debugfs_fops);

	return 0;
}
late_initcall(memblock_debugfs_init);
