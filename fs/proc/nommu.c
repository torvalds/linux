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
#include <linux/slab.h>
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
 * display a single VMA to a sequenced file
 */
int nommu_vma_show(struct seq_file *m, struct vm_area_struct *vma)
{
	unsigned long ino = 0;
	struct file *file;
	dev_t dev = 0;
	int flags, len;

	flags = vma->vm_flags;
	file = vma->vm_file;

	if (file) {
		struct inode *inode = vma->vm_file->f_path.dentry->d_inode;
		dev = inode->i_sb->s_dev;
		ino = inode->i_ino;
	}

	seq_printf(m,
		   "%08lx-%08lx %c%c%c%c %08lx %02x:%02x %lu %n",
		   vma->vm_start,
		   vma->vm_end,
		   flags & VM_READ ? 'r' : '-',
		   flags & VM_WRITE ? 'w' : '-',
		   flags & VM_EXEC ? 'x' : '-',
		   flags & VM_MAYSHARE ? flags & VM_SHARED ? 'S' : 's' : 'p',
		   vma->vm_pgoff << PAGE_SHIFT,
		   MAJOR(dev), MINOR(dev), ino, &len);

	if (file) {
		len = 25 + sizeof(void *) * 6 - len;
		if (len < 1)
			len = 1;
		seq_printf(m, "%*c", len, ' ');
		seq_path(m, file->f_path.mnt, file->f_path.dentry, "");
	}

	seq_putc(m, '\n');
	return 0;
}

/*
 * display a list of all the VMAs the kernel knows about
 * - nommu kernals have a single flat list
 */
static int nommu_vma_list_show(struct seq_file *m, void *v)
{
	struct vm_area_struct *vma;

	vma = rb_entry((struct rb_node *) v, struct vm_area_struct, vm_rb);
	return nommu_vma_show(m, vma);
}

static void *nommu_vma_list_start(struct seq_file *m, loff_t *_pos)
{
	struct rb_node *_rb;
	loff_t pos = *_pos;
	void *next = NULL;

	down_read(&nommu_vma_sem);

	for (_rb = rb_first(&nommu_vma_tree); _rb; _rb = rb_next(_rb)) {
		if (pos == 0) {
			next = _rb;
			break;
		}
		pos--;
	}

	return next;
}

static void nommu_vma_list_stop(struct seq_file *m, void *v)
{
	up_read(&nommu_vma_sem);
}

static void *nommu_vma_list_next(struct seq_file *m, void *v, loff_t *pos)
{
	(*pos)++;
	return rb_next((struct rb_node *) v);
}

static const struct seq_operations proc_nommu_vma_list_seqop = {
	.start	= nommu_vma_list_start,
	.next	= nommu_vma_list_next,
	.stop	= nommu_vma_list_stop,
	.show	= nommu_vma_list_show
};

static int proc_nommu_vma_list_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &proc_nommu_vma_list_seqop);
}

static const struct file_operations proc_nommu_vma_list_operations = {
	.open    = proc_nommu_vma_list_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
};

static int __init proc_nommu_init(void)
{
	create_seq_entry("maps", S_IRUGO, &proc_nommu_vma_list_operations);
	return 0;
}

module_init(proc_nommu_init);
