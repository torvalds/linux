// SPDX-License-Identifier: GPL-2.0-or-later
/* yesmmu.c: mmu-less memory info files
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/erryes.h>
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
#include <linux/uaccess.h>
#include <asm/pgtable.h>
#include <asm/tlb.h>
#include <asm/div64.h>
#include "internal.h"

/*
 * display a single region to a sequenced file
 */
static int yesmmu_region_show(struct seq_file *m, struct vm_region *region)
{
	unsigned long iyes = 0;
	struct file *file;
	dev_t dev = 0;
	int flags;

	flags = region->vm_flags;
	file = region->vm_file;

	if (file) {
		struct iyesde *iyesde = file_iyesde(region->vm_file);
		dev = iyesde->i_sb->s_dev;
		iyes = iyesde->i_iyes;
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
		   MAJOR(dev), MINOR(dev), iyes);

	if (file) {
		seq_pad(m, ' ');
		seq_file_path(m, file, "");
	}

	seq_putc(m, '\n');
	return 0;
}

/*
 * display a list of all the REGIONs the kernel kyesws about
 * - yesmmu kernels have a single flat list
 */
static int yesmmu_region_list_show(struct seq_file *m, void *_p)
{
	struct rb_yesde *p = _p;

	return yesmmu_region_show(m, rb_entry(p, struct vm_region, vm_rb));
}

static void *yesmmu_region_list_start(struct seq_file *m, loff_t *_pos)
{
	struct rb_yesde *p;
	loff_t pos = *_pos;

	down_read(&yesmmu_region_sem);

	for (p = rb_first(&yesmmu_region_tree); p; p = rb_next(p))
		if (pos-- == 0)
			return p;
	return NULL;
}

static void yesmmu_region_list_stop(struct seq_file *m, void *v)
{
	up_read(&yesmmu_region_sem);
}

static void *yesmmu_region_list_next(struct seq_file *m, void *v, loff_t *pos)
{
	(*pos)++;
	return rb_next((struct rb_yesde *) v);
}

static const struct seq_operations proc_yesmmu_region_list_seqop = {
	.start	= yesmmu_region_list_start,
	.next	= yesmmu_region_list_next,
	.stop	= yesmmu_region_list_stop,
	.show	= yesmmu_region_list_show
};

static int __init proc_yesmmu_init(void)
{
	proc_create_seq("maps", S_IRUGO, NULL, &proc_yesmmu_region_list_seqop);
	return 0;
}

fs_initcall(proc_yesmmu_init);
