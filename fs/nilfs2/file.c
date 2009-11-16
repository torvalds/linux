/*
 * file.c - NILFS regular file handling primitives including fsync().
 *
 * Copyright (C) 2005-2008 Nippon Telegraph and Telephone Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Written by Amagai Yoshiji <amagai@osrg.net>,
 *            Ryusuke Konishi <ryusuke@osrg.net>
 */

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/writeback.h>
#include "nilfs.h"
#include "segment.h"

int nilfs_sync_file(struct file *file, struct dentry *dentry, int datasync)
{
	/*
	 * Called from fsync() system call
	 * This is the only entry point that can catch write and synch
	 * timing for both data blocks and intermediate blocks.
	 *
	 * This function should be implemented when the writeback function
	 * will be implemented.
	 */
	struct inode *inode = dentry->d_inode;
	int err;

	if (!nilfs_inode_dirty(inode))
		return 0;

	if (datasync)
		err = nilfs_construct_dsync_segment(inode->i_sb, inode, 0,
						    LLONG_MAX);
	else
		err = nilfs_construct_segment(inode->i_sb);

	return err;
}

static int nilfs_page_mkwrite(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct page *page = vmf->page;
	struct inode *inode = vma->vm_file->f_dentry->d_inode;
	struct nilfs_transaction_info ti;
	int ret;

	if (unlikely(nilfs_near_disk_full(NILFS_SB(inode->i_sb)->s_nilfs)))
		return VM_FAULT_SIGBUS; /* -ENOSPC */

	lock_page(page);
	if (page->mapping != inode->i_mapping ||
	    page_offset(page) >= i_size_read(inode) || !PageUptodate(page)) {
		unlock_page(page);
		return VM_FAULT_NOPAGE; /* make the VM retry the fault */
	}

	/*
	 * check to see if the page is mapped already (no holes)
	 */
	if (PageMappedToDisk(page)) {
		unlock_page(page);
		goto mapped;
	}
	if (page_has_buffers(page)) {
		struct buffer_head *bh, *head;
		int fully_mapped = 1;

		bh = head = page_buffers(page);
		do {
			if (!buffer_mapped(bh)) {
				fully_mapped = 0;
				break;
			}
		} while (bh = bh->b_this_page, bh != head);

		if (fully_mapped) {
			SetPageMappedToDisk(page);
			unlock_page(page);
			goto mapped;
		}
	}
	unlock_page(page);

	/*
	 * fill hole blocks
	 */
	ret = nilfs_transaction_begin(inode->i_sb, &ti, 1);
	/* never returns -ENOMEM, but may return -ENOSPC */
	if (unlikely(ret))
		return VM_FAULT_SIGBUS;

	ret = block_page_mkwrite(vma, vmf, nilfs_get_block);
	if (unlikely(ret)) {
		nilfs_transaction_abort(inode->i_sb);
		return ret;
	}
	nilfs_transaction_commit(inode->i_sb);

 mapped:
	SetPageChecked(page);
	wait_on_page_writeback(page);
	return 0;
}

static const struct vm_operations_struct nilfs_file_vm_ops = {
	.fault		= filemap_fault,
	.page_mkwrite	= nilfs_page_mkwrite,
};

static int nilfs_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	file_accessed(file);
	vma->vm_ops = &nilfs_file_vm_ops;
	vma->vm_flags |= VM_CAN_NONLINEAR;
	return 0;
}

/*
 * We have mostly NULL's here: the current defaults are ok for
 * the nilfs filesystem.
 */
const struct file_operations nilfs_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= do_sync_read,
	.write		= do_sync_write,
	.aio_read	= generic_file_aio_read,
	.aio_write	= generic_file_aio_write,
	.unlocked_ioctl	= nilfs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= nilfs_ioctl,
#endif	/* CONFIG_COMPAT */
	.mmap		= nilfs_file_mmap,
	.open		= generic_file_open,
	/* .release	= nilfs_release_file, */
	.fsync		= nilfs_sync_file,
	.splice_read	= generic_file_splice_read,
};

const struct inode_operations nilfs_file_inode_operations = {
	.truncate	= nilfs_truncate,
	.setattr	= nilfs_setattr,
	.permission     = nilfs_permission,
};

/* end of file */
