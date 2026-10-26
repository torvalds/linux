/* SPDX-License-Identifier: GPL-2.0 */
/*
 * include/linux/nfs_ssc.h
 *
 * NFSv4.2 server-to-server copy, NFS client side APIs
 *
 * Author: Dai Ngo <dai.ngo@oracle.com>
 *
 * Copyright (c) 2020, Oracle and/or its affiliates.
 */

#ifndef _LINUX_NFS_SSC_H
#define _LINUX_NFS_SSC_H

#include <linux/nfs_fh.h>
#include <linux/nfs4.h>

struct file;
struct vfsmount;

struct nfs4_ssc_client_ops {
	struct module *owner;
	struct file *(*sco_open)(struct vfsmount *ss_mnt,
		struct nfs_fh *src_fh, nfs4_stateid *stateid);
	void (*sco_close)(struct file *filep);
};

extern void nfs42_ssc_register_ops(void);
extern void nfs42_ssc_unregister_ops(void);

extern void nfs42_ssc_register(const struct nfs4_ssc_client_ops *ops);
extern void nfs42_ssc_unregister(const struct nfs4_ssc_client_ops *ops);

#endif /* _LINUX_NFS_SSC_H */
