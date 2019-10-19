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

struct compat_nfs_string {
	compat_uint_t len;
	compat_uptr_t data;
};

static inline void compat_nfs_string(struct nfs_string *dst,
				     struct compat_nfs_string *src)
{
	dst->data = compat_ptr(src->data);
	dst->len = src->len;
}

struct compat_nfs4_mount_data_v1 {
	compat_int_t version;
	compat_int_t flags;
	compat_int_t rsize;
	compat_int_t wsize;
	compat_int_t timeo;
	compat_int_t retrans;
	compat_int_t acregmin;
	compat_int_t acregmax;
	compat_int_t acdirmin;
	compat_int_t acdirmax;
	struct compat_nfs_string client_addr;
	struct compat_nfs_string mnt_path;
	struct compat_nfs_string hostname;
	compat_uint_t host_addrlen;
	compat_uptr_t host_addr;
	compat_int_t proto;
	compat_int_t auth_flavourlen;
	compat_uptr_t auth_flavours;
};

static int do_nfs4_super_data_conv(void *raw_data)
{
	int version = *(compat_uint_t *) raw_data;

	if (version == 1) {
		struct compat_nfs4_mount_data_v1 *raw = raw_data;
		struct nfs4_mount_data *real = raw_data;

		/* copy the fields backwards */
		real->auth_flavours = compat_ptr(raw->auth_flavours);
		real->auth_flavourlen = raw->auth_flavourlen;
		real->proto = raw->proto;
		real->host_addr = compat_ptr(raw->host_addr);
		real->host_addrlen = raw->host_addrlen;
		compat_nfs_string(&real->hostname, &raw->hostname);
		compat_nfs_string(&real->mnt_path, &raw->mnt_path);
		compat_nfs_string(&real->client_addr, &raw->client_addr);
		real->acdirmax = raw->acdirmax;
		real->acdirmin = raw->acdirmin;
		real->acregmax = raw->acregmax;
		real->acregmin = raw->acregmin;
		real->retrans = raw->retrans;
		real->timeo = raw->timeo;
		real->wsize = raw->wsize;
		real->rsize = raw->rsize;
		real->flags = raw->flags;
		real->version = raw->version;
	}

	return 0;
}

#define NFS4_NAME	"nfs4"

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

	if (kernel_type && options) {
		if (!strcmp(kernel_type, NFS4_NAME)) {
			retval = -EINVAL;
			if (do_nfs4_super_data_conv(options))
				goto out3;
		}
	}

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
