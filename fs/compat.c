// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/fs/compat.c
 *
 *  Kernel compatibililty routines for e.g. 32 bit syscall support
 *  on 64 bit kernels.
 *
 *  Copyright (C) 2002       Stephen Rothwell, IBM Corporation
 *  Copyright (C) 1997-2000  Jakub Jelinek  (jakub@redhat.com)
 *  Copyright (C) 1998       Eddie C. Dost  (ecd@skynet.be)
 *  Copyright (C) 2001,2002  Andi Kleen, SuSE Labs 
 *  Copyright (C) 2003       Pavel Machek (pavel@ucw.cz)
 */

#include <linux/compat.h>
#include <linux/nfs4_mount.h>
#include <linux/syscalls.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "internal.h"

COMPAT_SYSCALL_DEFINE5(mount, const char __user *, dev_name,
		       const char __user *, dir_name,
		       const char __user *, type, compat_ulong_t, flags,
		       const void __user *, data)
{
	char *kernel_type;
	void *options;
	char *kernel_dev;
	int retval;

	kernel_type = copy_mount_string(type);
	retval = PTR_ERR(kernel_type);
	if (IS_ERR(kernel_type))
		goto out;

	kernel_dev = copy_mount_string(dev_name);
	retval = PTR_ERR(kernel_dev);
	if (IS_ERR(kernel_dev))
		goto out1;

	options = copy_mount_options(data);
	retval = PTR_ERR(options);
	if (IS_ERR(options))
		goto out2;

	retval = do_mount(kernel_dev, dir_name, kernel_type, flags, options);

 out3:
	kfree(options);
 out2:
	kfree(kernel_dev);
 out1:
	kfree(kernel_type);
 out:
	return retval;
}
