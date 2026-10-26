/* SPDX-License-Identifier: GPL-2.0 */
/*
 * include/linux/nfsd_ssc.h
 *
 * NFSv4.2 server-to-server copy, NFS server side APIs
 *
 * Author: Dai Ngo <dai.ngo@oracle.com>
 *
 * Copyright (c) 2020, Oracle and/or its affiliates.
 */

#ifndef _LINUX_NFSD_SSC_H
#define _LINUX_NFSD_SSC_H

#include <linux/nfs_fh.h>
#include <linux/nfs4.h>

struct file;
struct vfsmount;

#if IS_ENABLED(CONFIG_NFS_V4_2_SSC_HELPER)
struct file *nfsd42_ssc_open(struct vfsmount *ss_mnt, struct nfs_fh *src_fh,
			     nfs4_stateid *stateid);
void nfsd42_ssc_close(struct file *filp);
#else
static inline struct file *nfsd42_ssc_open(struct vfsmount *ss_mnt,
					   struct nfs_fh *src_fh,
					   nfs4_stateid *stateid)
{
	return ERR_PTR(-EIO);
}

static inline void nfsd42_ssc_close(struct file *filp)
{
}
#endif

#endif /* _LINUX_NFSD_SSC_H */
