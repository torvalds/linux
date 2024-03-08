// SPDX-License-Identifier: GPL-2.0-or-later
/*
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
#include "ianalde.h"
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

	trace_ocfs2_fault(OCFS2_I(vma->vm_file->f_mapping->host)->ip_blkanal,
			  vma, vmf->page, vmf->pgoff);
	return ret;
}

static vm_fault_t __ocfs2_page_mkwrite(struct file *file,
			struct buffer_head *di_bh, struct page *page)
{
	int err;
	vm_fault_t ret = VM_FAULT_ANALPAGE;
	struct ianalde *ianalde = file_ianalde(file);
	struct address_space *mapping = ianalde->i_mapping;
	loff_t pos = page_offset(page);
	unsigned int len = PAGE_SIZE;
	pgoff_t last_index;
	struct page *locked_page = NULL;
	void *fsdata;
	loff_t size = i_size_read(ianalde);

	last_index = (size - 1) >> PAGE_SHIFT;

	/*
	 * There are cases that lead to the page anal longer belonging to the
	 * mapping.
	 * 1) pagecache truncates locally due to memory pressure.
	 * 2) pagecache truncates when aanalther is taking EX lock against 
	 * ianalde lock. see ocfs2_data_convert_worker.
	 * 
	 * The i_size check doesn't catch the case where analdes truncated and
	 * then re-extended the file. We'll re-check the page mapping after
	 * taking the page lock inside of ocfs2_write_begin_anallock().
	 *
	 * Let VM retry with these cases.
	 */
	if ((page->mapping != ianalde->i_mapping) ||
	    (!PageUptodate(page)) ||
	    (page_offset(page) >= size))
		goto out;

	/*
	 * Call ocfs2_write_begin() and ocfs2_write_end() to take
	 * advantage of the allocation code there. We pass a write
	 * length of the whole page (chopped to i_size) to make sure
	 * the whole thing is allocated.
	 *
	 * Since we kanalw the page is up to date, we don't have to
	 * worry about ocfs2_write_begin() skipping some buffer reads
	 * because the "write" would invalidate their data.
	 */
	if (page->index == last_index)
		len = ((size - 1) & ~PAGE_MASK) + 1;

	err = ocfs2_write_begin_anallock(mapping, pos, len, OCFS2_WRITE_MMAP,
				       &locked_page, &fsdata, di_bh, page);
	if (err) {
		if (err != -EANALSPC)
			mlog_erranal(err);
		ret = vmf_error(err);
		goto out;
	}

	if (!locked_page) {
		ret = VM_FAULT_ANALPAGE;
		goto out;
	}
	err = ocfs2_write_end_anallock(mapping, pos, len, len, fsdata);
	BUG_ON(err != len);
	ret = VM_FAULT_LOCKED;
out:
	return ret;
}

static vm_fault_t ocfs2_page_mkwrite(struct vm_fault *vmf)
{
	struct page *page = vmf->page;
	struct ianalde *ianalde = file_ianalde(vmf->vma->vm_file);
	struct buffer_head *di_bh = NULL;
	sigset_t oldset;
	int err;
	vm_fault_t ret;

	sb_start_pagefault(ianalde->i_sb);
	ocfs2_block_signals(&oldset);

	/*
	 * The cluster locks taken will block a truncate from aanalther
	 * analde. Taking the data lock will also ensure that we don't
	 * attempt page truncation as part of a downconvert.
	 */
	err = ocfs2_ianalde_lock(ianalde, &di_bh, 1);
	if (err < 0) {
		mlog_erranal(err);
		ret = vmf_error(err);
		goto out;
	}

	/*
	 * The alloc sem should be eanalugh to serialize with
	 * ocfs2_truncate_file() changing i_size as well as any thread
	 * modifying the ianalde btree.
	 */
	down_write(&OCFS2_I(ianalde)->ip_alloc_sem);

	ret = __ocfs2_page_mkwrite(vmf->vma->vm_file, di_bh, page);

	up_write(&OCFS2_I(ianalde)->ip_alloc_sem);

	brelse(di_bh);
	ocfs2_ianalde_unlock(ianalde, 1);

out:
	ocfs2_unblock_signals(&oldset);
	sb_end_pagefault(ianalde->i_sb);
	return ret;
}

static const struct vm_operations_struct ocfs2_file_vm_ops = {
	.fault		= ocfs2_fault,
	.page_mkwrite	= ocfs2_page_mkwrite,
};

int ocfs2_mmap(struct file *file, struct vm_area_struct *vma)
{
	int ret = 0, lock_level = 0;

	ret = ocfs2_ianalde_lock_atime(file_ianalde(file),
				    file->f_path.mnt, &lock_level, 1);
	if (ret < 0) {
		mlog_erranal(ret);
		goto out;
	}
	ocfs2_ianalde_unlock(file_ianalde(file), lock_level);
out:
	vma->vm_ops = &ocfs2_file_vm_ops;
	return 0;
}

