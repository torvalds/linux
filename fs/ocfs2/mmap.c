/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * mmap.c
 *
 * Code to deal with the mess that is clustered mmap.
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/uio.h>
#include <linux/signal.h>
#include <linux/rbtree.h>

#include <cluster/masklog.h>

#include "ocfs2.h"

#include "aops.h"
#include "dlmglue.h"
#include "file.h"
#include "inode.h"
#include "mmap.h"
#include "super.h"
#include "ocfs2_trace.h"


static int ocfs2_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	sigset_t oldset;
	int ret;

	ocfs2_block_signals(&oldset);
	ret = filemap_fault(vmf);
	ocfs2_unblock_signals(&oldset);

	trace_ocfs2_fault(OCFS2_I(vma->vm_file->f_mapping->host)->ip_blkno,
			  vma, vmf->page, vmf->pgoff);
	return ret;
}

static int __ocfs2_page_mkwrite(struct file *file, struct buffer_head *di_bh,
				struct page *page)
{
	int ret = VM_FAULT_NOPAGE;
	struct inode *inode = file_inode(file);
	struct address_space *mapping = inode->i_mapping;
	loff_t pos = page_offset(page);
	unsigned int len = PAGE_SIZE;
	pgoff_t last_index;
	struct page *locked_page = NULL;
	void *fsdata;
	loff_t size = i_size_read(inode);

	last_index = (size - 1) >> PAGE_SHIFT;

	/*
	 * There are cases that lead to the page no longer bebongs to the
	 * mapping.
	 * 1) pagecache truncates locally due to memory pressure.
	 * 2) pagecache truncates when another is taking EX lock against 
	 * inode lock. see ocfs2_data_convert_worker.
	 * 
	 * The i_size check doesn't catch the case where nodes truncated and
	 * then re-extended the file. We'll re-check the page mapping after
	 * taking the page lock inside of ocfs2_write_begin_nolock().
	 *
	 * Let VM retry with these cases.
	 */
	if ((page->mapping != inode->i_mapping) ||
	    (!PageUptodate(page)) ||
	    (page_offset(page) >= size))
		goto out;

	/*
	 * Call ocfs2_write_begin() and ocfs2_write_end() to take
	 * advantage of the allocation code there. We pass a write
	 * length of the whole page (chopped to i_size) to make sure
	 * the whole thing is allocated.
	 *
	 * Since we know the page is up to date, we don't have to
	 * worry about ocfs2_write_begin() skipping some buffer reads
	 * because the "write" would invalidate their data.
	 */
	if (page->index == last_index)
		len = ((size - 1) & ~PAGE_MASK) + 1;

	ret = ocfs2_write_begin_nolock(mapping, pos, len, OCFS2_WRITE_MMAP,
				       &locked_page, &fsdata, di_bh, page);
	if (ret) {
		if (ret != -ENOSPC)
			mlog_errno(ret);
		if (ret == -ENOMEM)
			ret = VM_FAULT_OOM;
		else
			ret = VM_FAULT_SIGBUS;
		goto out;
	}

	if (!locked_page) {
		ret = VM_FAULT_NOPAGE;
		goto out;
	}
	ret = ocfs2_write_end_nolock(mapping, pos, len, len, fsdata);
	BUG_ON(ret != len);
	ret = VM_FAULT_LOCKED;
out:
	return ret;
}

static int ocfs2_page_mkwrite(struct vm_fault *vmf)
{
	struct page *page = vmf->page;
	struct inode *inode = file_inode(vmf->vma->vm_file);
	struct buffer_head *di_bh = NULL;
	sigset_t oldset;
	int ret;

	sb_start_pagefault(inode->i_sb);
	ocfs2_block_signals(&oldset);

	/*
	 * The cluster locks taken will block a truncate from another
	 * node. Taking the data lock will also ensure that we don't
	 * attempt page truncation as part of a downconvert.
	 */
	ret = ocfs2_inode_lock(inode, &di_bh, 1);
	if (ret < 0) {
		mlog_errno(ret);
		if (ret == -ENOMEM)
			ret = VM_FAULT_OOM;
		else
			ret = VM_FAULT_SIGBUS;
		goto out;
	}

	/*
	 * The alloc sem should be enough to serialize with
	 * ocfs2_truncate_file() changing i_size as well as any thread
	 * modifying the inode btree.
	 */
	down_write(&OCFS2_I(inode)->ip_alloc_sem);

	ret = __ocfs2_page_mkwrite(vmf->vma->vm_file, di_bh, page);

	up_write(&OCFS2_I(inode)->ip_alloc_sem);

	brelse(di_bh);
	ocfs2_inode_unlock(inode, 1);

out:
	ocfs2_unblock_signals(&oldset);
	sb_end_pagefault(inode->i_sb);
	return ret;
}

static const struct vm_operations_struct ocfs2_file_vm_ops = {
	.fault		= ocfs2_fault,
	.page_mkwrite	= ocfs2_page_mkwrite,
};

int ocfs2_mmap(struct file *file, struct vm_area_struct *vma)
{
	int ret = 0, lock_level = 0;

	ret = ocfs2_inode_lock_atime(file_inode(file),
				    file->f_path.mnt, &lock_level);
	if (ret < 0) {
		mlog_errno(ret);
		goto out;
	}
	ocfs2_inode_unlock(file_inode(file), lock_level);
out:
	vma->vm_ops = &ocfs2_file_vm_ops;
	return 0;
}

