/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2024 Microsoft Corporation. All rights reserved.
 */
#ifndef _IPE_HOOKS_H
#define _IPE_HOOKS_H

#include <linux/fs.h>
#include <linux/binfmts.h>
#include <linux/security.h>

int ipe_bprm_check_security(struct linux_binprm *bprm);

int ipe_mmap_file(struct file *f, unsigned long reqprot, unsigned long prot,
		  unsigned long flags);

int ipe_file_mprotect(struct vm_area_struct *vma, unsigned long reqprot,
		      unsigned long prot);

int ipe_kernel_read_file(struct file *file, enum kernel_read_file_id id,
			 bool contents);

int ipe_kernel_load_data(enum kernel_load_data_id id, bool contents);

void ipe_unpack_initramfs(void);

#endif /* _IPE_HOOKS_H */
