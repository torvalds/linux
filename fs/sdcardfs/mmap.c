/*
 * fs/sdcardfs/mmap.c
 *
 * Copyright (c) 2013 Samsung Electronics Co. Ltd
 *   Authors: Daeho Jeong, Woojoong Lee, Seunghwan Hyun,
 *               Sunghwan Yun, Sungjong Seo
 *
 * This program has been developed as a stackable file system based on
 * the WrapFS which written by
 *
 * Copyright (c) 1998-2011 Erez Zadok
 * Copyright (c) 2009     Shrikar Archak
 * Copyright (c) 2003-2011 Stony Brook University
 * Copyright (c) 2003-2011 The Research Foundation of SUNY
 *
 * This file is dual licensed.  It may be redistributed and/or modified
 * under the terms of the Apache 2.0 License OR version 2 of the GNU
 * General Public License.
 */

#include "sdcardfs.h"

static vm_fault_t sdcardfs_fault(struct vm_fault *vmf)
{
	vm_fault_t err;
	struct file *file;
	const struct vm_operations_struct *lower_vm_ops;

	file = (struct file *)vmf->vma->vm_private_data;
	lower_vm_ops = SDCARDFS_F(file)->lower_vm_ops;
	BUG_ON(!lower_vm_ops);

	err = lower_vm_ops->fault(vmf);
	return err;
}

static void sdcardfs_vm_open(struct vm_area_struct *vma)
{
	struct file *file = (struct file *)vma->vm_private_data;

	get_file(file);
}

static void sdcardfs_vm_close(struct vm_area_struct *vma)
{
	struct file *file = (struct file *)vma->vm_private_data;

	fput(file);
}

static vm_fault_t sdcardfs_page_mkwrite(struct vm_fault *vmf)
{
	vm_fault_t err = 0;
	struct file *file;
	const struct vm_operations_struct *lower_vm_ops;

	file = (struct file *)vmf->vma->vm_private_data;
	lower_vm_ops = SDCARDFS_F(file)->lower_vm_ops;
	BUG_ON(!lower_vm_ops);
	if (!lower_vm_ops->page_mkwrite)
		goto out;

	err = lower_vm_ops->page_mkwrite(vmf);
out:
	return err;
}

static ssize_t sdcardfs_direct_IO(struct kiocb *iocb, struct iov_iter *iter)
{
	/*
	 * This function should never be called directly.  We need it
	 * to exist, to get past a check in open_check_o_direct(),
	 * which is called from do_last().
	 */
	return -EINVAL;
}

const struct address_space_operations sdcardfs_aops = {
	.direct_IO	= sdcardfs_direct_IO,
};

const struct vm_operations_struct sdcardfs_vm_ops = {
	.fault		= sdcardfs_fault,
	.page_mkwrite	= sdcardfs_page_mkwrite,
	.open		= sdcardfs_vm_open,
	.close		= sdcardfs_vm_close,
};
