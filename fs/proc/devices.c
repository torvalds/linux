// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/blkdev.h>
#include "internal.h"

static int devinfo_show(struct seq_file *f, void *v)
{
	int i = *(loff_t *) v;

	if (i < CHRDEV_MAJOR_MAX) {
		if (i == 0)
			seq_puts(f, "Character devices:\n");
		chrdev_show(f, i);
	}
#ifdef CONFIG_BLOCK
	else {
		i -= CHRDEV_MAJOR_MAX;
		if (i == 0)
			seq_puts(f, "\nBlock devices:\n");
		blkdev_show(f, i);
	}
#endif
	return 0;
}

static void *devinfo_start(struct seq_file *f, loff_t *pos)
{
	if (*pos < (BLKDEV_MAJOR_MAX + CHRDEV_MAJOR_MAX))
		return pos;
	return NULL;
}

static void *devinfo_next(struct seq_file *f, void *v, loff_t *pos)
{
	(*pos)++;
	if (*pos >= (BLKDEV_MAJOR_MAX + CHRDEV_MAJOR_MAX))
		return NULL;
	return pos;
}

static void devinfo_stop(struct seq_file *f, void *v)
{
	/* Nothing to do */
}

static const struct seq_operations devinfo_ops = {
	.start = devinfo_start,
	.next  = devinfo_next,
	.stop  = devinfo_stop,
	.show  = devinfo_show
};

static int __init proc_devices_init(void)
{
	struct proc_dir_entry *pde;

	pde = proc_create_seq("devices", 0, NULL, &devinfo_ops);
	pde_make_permanent(pde);
	return 0;
}
fs_initcall(proc_devices_init);
