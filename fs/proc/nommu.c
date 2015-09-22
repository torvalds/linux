/* nommu.c: mmu-less memory info files
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/mman.h>
#include <linux/proc_fs.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/smp.h>
#include <linux/seq_file.h>
#include <linux/hugetlb.h>
#include <linux/vmalloc.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/tlb.h>
#include <asm/div64.h>
#include "internal.h"

/*
 * display a single region to a sequenced file
 */
static int nommu_region_show(struct seq_file *m, struct vm_region *region)
{
	unsigned long ino = 0;
	struct file *file;
	dev_t dev = 0;
	int flags;

	flags = region->vm_flags;
	file = region->vm_file;

	if (file) {
		struct inode *inode = file_inode(region->vm_file);
		dev = inode->i_sb->s_dev;
		ino = inode->i_ino;
	}

	seq_setwidth(m, 25 + sizeof(void *) * 6 - 1);
	seq_printf(m,
		   "%08lx-%08lx %c%c%c%c %08llx %02x:%02x %lu ",
		   region->vm_start,
		   region->vm_end,
		   flags & VM_READ ? 'r' : '-',
		   flags & VM_WRITE ? 'w' : '-',
		   flags & VM_EXEC ? 'x' : '-',
		   flags & VM_MAYSHARE ? flags & VM_SHARED ? 'S' : 's' : 'p',
		   ((loff_t)region->vm_pgoff) << PAGE_SHIFT,
		   MAJOR(dev), MINOR(dev), ino);

	if (file) {
		seq_pad(m, ' ');
		seq_file_path(m, file, "");
	}

	seq_putc(m, '\n');
	return 0;
}

/*
 * display a list of all the REGIONs the kernel knows about
 * - nommu kernels have a single flat list
 */
static int nommu_region_list_show(struct seq_file *m, void *_p)
{
	struct rb_node *p = _p;

	return nommu_region_show(m, rb_entry(p, struct vm_region, vm_rb));
}

static void *nommu_region_list_start(struct seq_file *m, loff_t *_pos)
{
	struct rb_node *p;
	loff_t pos = *_pos;

	down_read(&nommu_region_sem);

	for (p = rb_first(&nommu_region_tree); p; p = rb_next(p))
		if (pos-- == 0)
			return p;
	return NULL;
}

static void nommu_region_list_stop(struct seq_file *m, void *v)
{
	up_read(&nommu_region_sem);
}

static void *nommu_region_list_next(struct seq_file *m, void *v, loff_t *pos)
{
	(*pos)++;
	return rb_next((struct rb_node *) v);
}

static const struct seq_operations proc_nommu_region_list_seqop = {
	.start	= nommu_region_list_start,
	.next	= nommu_region_list_next,
	.stop	= nommu_region_list_stop,
	.show	= nommu_region_list_show
};

static int proc_nommu_region_list_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &proc_nommu_region_list_seqop);
}

static const struct file_operations proc_nommu_region_list_operations = {
	.open    = proc_nommu_region_list_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
};

static int __init proc_nommu_init(void)
{
	proc_create("maps", S_IRUGO, NULL, &proc_nommu_region_list_operations);
	return 0;
}

fs_initcall(proc_nommu_init);
