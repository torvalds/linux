// SPDX-License-Identifier: GPL-2.0-or-later
/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: yesexpandtab sw=8 ts=8 sts=0:
 *
 * mmap.c
 *
 * Code to deal with the mess that is clustered mmap.
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
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
#include "iyesde.h"
#include "mmap.h"
#include "super.h"
#include "ocfs2_trace.h"


static vm_fault_t ocfs2_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	sigset_t oldset;
	vm_fault_t ret;

	ocfs2_block_signals(&oldset);
	ret = filemap_fault(vmf);
	ocfs2_unblock_signals(&oldset);

	trace_ocfs2_fault(OCFS2_I(vma->vm_file->f_mapping->host)->ip_blkyes,
			  vma, vmf->page, vmf->pgoff);
	return ret;
}

static vm_fault_t __ocfs2_page_mkwrite(struct file *file,
			struct buffer_head *di_bh, struct page *page)
{
	int err;
	vm_fault_t ret = VM_FAULT_NOPAGE;
	struct iyesde *iyesde = file_iyesde(file);
	struct address_space *mapping = iyesde->i_mapping;
	loff_t pos = page_offset(page);
	unsigned int len = PAGE_SIZE;
	pgoff_t last_index;
	struct page *locked_page = NULL;
	void *fsdata;
	loff_t size = i_size_read(iyesde);

	last_index = (size - 1) >> PAGE_SHIFT;

	/*
	 * There are cases that lead to the page yes longer bebongs to the
	 * mapping.
	 * 1) pagecache truncates locally due to memory pressure.
	 * 2) pagecache truncates when ayesther is taking EX lock against 
	 * iyesde lock. see ocfs2_data_convert_worker.
	 * 
	 * The i_size check doesn't catch the case where yesdes truncated and
	 * then re-extended the file. We'll re-check the page mapping after
	 * taking the page lock inside of ocfs2_write_begin_yeslock().
	 *
	 * Let VM retry with these cases.
	 */
	if ((page->mapping != iyesde->i_mapping) ||
	    (!PageUptodate(page)) ||
	    (page_offset(page) >= size))
		goto out;

	/*
	 * Call ocfs2_write_begin() and ocfs2_write_end() to take
	 * advantage of the allocation code there. We pass a write
	 * length of the whole page (chopped to i_size) to make sure
	 * the whole thing is allocated.
	 *
	 * Since we kyesw the page is up to date, we don't have to
	 * worry about ocfs2_write_begin() skipping some buffer reads
	 * because the "write" would invalidate their data.
	 */
	if (page->index == last_index)
		len = ((size - 1) & ~PAGE_MASK) + 1;

	err = ocfs2_write_begin_yeslock(mapping, pos, len, OCFS2_WRITE_MMAP,
				       &locked_page, &fsdata, di_bh, page);
	if (err) {
		if (err != -ENOSPC)
			mlog_erryes(err);
		ret = vmf_error(err);
		goto out;
	}

	if (!locked_page) {
		ret = VM_FAULT_NOPAGE;
		goto out;
	}
	err = ocfs2_write_end_yeslock(mapping, pos, len, len, fsdata);
	BUG_ON(err != len);
	ret = VM_FAULT_LOCKED;
out:
	return ret;
}

static vm_fault_t ocfs2_page_mkwrite(struct vm_fault *vmf)
{
	struct page *page = vmf->page;
	struct iyesde *iyesde = file_iyesde(vmf->vma->vm_file);
	struct buffer_head *di_bh = NULL;
	sigset_t oldset;
	int err;
	vm_fault_t ret;

	sb_start_pagefault(iyesde->i_sb);
	ocfs2_block_signals(&oldset);

	/*
	 * The cluster locks taken will block a truncate from ayesther
	 * yesde. Taking the data lock will also ensure that we don't
	 * attempt page truncation as part of a downconvert.
	 */
	err = ocfs2_iyesde_lock(iyesde, &di_bh, 1);
	if (err < 0) {
		mlog_erryes(err);
		ret = vmf_error(err);
		goto out;
	}

	/*
	 * The alloc sem should be eyesugh to serialize with
	 * ocfs2_truncate_file() changing i_size as well as any thread
	 * modifying the iyesde btree.
	 */
	down_write(&OCFS2_I(iyesde)->ip_alloc_sem);

	ret = __ocfs2_page_mkwrite(vmf->vma->vm_file, di_bh, page);

	up_write(&OCFS2_I(iyesde)->ip_alloc_sem);

	brelse(di_bh);
	ocfs2_iyesde_unlock(iyesde, 1);

out:
	ocfs2_unblock_signals(&oldset);
	sb_end_pagefault(iyesde->i_sb);
	return ret;
}

static const struct vm_operations_struct ocfs2_file_vm_ops = {
	.fault		= ocfs2_fault,
	.page_mkwrite	= ocfs2_page_mkwrite,
};

int ocfs2_mmap(struct file *file, struct vm_area_struct *vma)
{
	int ret = 0, lock_level = 0;

	ret = ocfs2_iyesde_lock_atime(file_iyesde(file),
				    file->f_path.mnt, &lock_level, 1);
	if (ret < 0) {
		mlog_erryes(ret);
		goto out;
	}
	ocfs2_iyesde_unlock(file_iyesde(file), lock_level);
out:
	vma->vm_ops = &ocfs2_file_vm_ops;
	return 0;
}

