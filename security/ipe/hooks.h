/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2024 Microsoft Corporation. All rights reserved.
 */
#ifndef _IPE_HOOKS_H
#define _IPE_HOOKS_H

#include <linux/fs.h>
#include <linux/binfmts.h>
#include <linux/security.h>
#include <linux/blk_types.h>
#include <linux/fsverity.h>

enum ipe_hook_type {
	IPE_HOOK_BPRM_CHECK = 0,
	IPE_HOOK_MMAP,
	IPE_HOOK_MPROTECT,
	IPE_HOOK_KERNEL_READ,
	IPE_HOOK_KERNEL_LOAD,
	__IPE_HOOK_MAX
};

#define IPE_HOOK_INVALID __IPE_HOOK_MAX

int ipe_bprm_check_security(struct linux_binprm *bprm);

int ipe_mmap_file(struct file *f, unsigned long reqprot, unsigned long prot,
		  unsigned long flags);

int ipe_file_mprotect(struct vm_area_struct *vma, unsigned long reqprot,
		      unsigned long prot);

int ipe_kernel_read_file(struct file *file, enum kernel_read_file_id id,
			 bool contents);

int ipe_kernel_load_data(enum kernel_load_data_id id, bool contents);

void ipe_unpack_initramfs(void);

#ifdef CONFIG_IPE_PROP_DM_VERITY
void ipe_bdev_free_security(struct block_device *bdev);

int ipe_bdev_setintegrity(struct block_device *bdev, enum lsm_integrity_type type,
			  const void *value, size_t len);
#endif /* CONFIG_IPE_PROP_DM_VERITY */

#ifdef CONFIG_IPE_PROP_FS_VERITY_BUILTIN_SIG
int ipe_inode_setintegrity(const struct inode *inode, enum lsm_integrity_type type,
			   const void *value, size_t size);
#endif /* CONFIG_IPE_PROP_FS_VERITY_BUILTIN_SIG */

#endif /* _IPE_HOOKS_H */
