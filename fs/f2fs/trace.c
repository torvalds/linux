/*
 * f2fs IO tracer
 *
 * Copyright (c) 2014 Motorola Mobility
 * Copyright (c) 2014 Jaegeuk Kim <jaegeuk@kernel.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/fs.h>
#include <linux/f2fs_fs.h>
#include <linux/sched.h>

#include "f2fs.h"
#include "trace.h"

RADIX_TREE(pids, GFP_NOIO);
struct last_io_info last_io;

static inline void __print_last_io(void)
{
	if (!last_io.len)
		return;

	trace_printk("%3x:%3x %4x %-16s %2x %5x %12x %4x\n",
			last_io.major, last_io.minor,
			last_io.pid, "----------------",
			last_io.type,
			last_io.fio.rw, last_io.fio.blk_addr,
			last_io.len);
	memset(&last_io, 0, sizeof(last_io));
}

static int __file_type(struct inode *inode, pid_t pid)
{
	if (f2fs_is_atomic_file(inode))
		return __ATOMIC_FILE;
	else if (f2fs_is_volatile_file(inode))
		return __VOLATILE_FILE;
	else if (S_ISDIR(inode->i_mode))
		return __DIR_FILE;
	else if (inode->i_ino == F2FS_NODE_INO(F2FS_I_SB(inode)))
		return __NODE_FILE;
	else if (inode->i_ino == F2FS_META_INO(F2FS_I_SB(inode)))
		return __META_FILE;
	else if (pid)
		return __NORMAL_FILE;
	else
		return __MISC_FILE;
}

void f2fs_trace_pid(struct page *page)
{
	struct inode *inode = page->mapping->host;
	pid_t pid = task_pid_nr(current);
	void *p;

	page->private = pid;

	p = radix_tree_lookup(&pids, pid);
	if (p == current)
		return;
	if (p)
		radix_tree_delete(&pids, pid);

	f2fs_radix_tree_insert(&pids, pid, current);

	trace_printk("%3x:%3x %4x %-16s\n",
			MAJOR(inode->i_sb->s_dev), MINOR(inode->i_sb->s_dev),
			pid, current->comm);

}

void f2fs_trace_ios(struct page *page, struct f2fs_io_info *fio, int flush)
{
	struct inode *inode;
	pid_t pid;
	int major, minor;

	if (flush) {
		__print_last_io();
		return;
	}

	inode = page->mapping->host;
	pid = page_private(page);

	major = MAJOR(inode->i_sb->s_dev);
	minor = MINOR(inode->i_sb->s_dev);

	if (last_io.major == major && last_io.minor == minor &&
			last_io.pid == pid &&
			last_io.type == __file_type(inode, pid) &&
			last_io.fio.rw == fio->rw &&
			last_io.fio.blk_addr + last_io.len == fio->blk_addr) {
		last_io.len++;
		return;
	}

	__print_last_io();

	last_io.major = major;
	last_io.minor = minor;
	last_io.pid = pid;
	last_io.type = __file_type(inode, pid);
	last_io.fio = *fio;
	last_io.len = 1;
	return;
}
