/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file contains constants and methods used by both NFS client and server.
 */
#ifndef _LINUX_NFS_COMMON_H
#define _LINUX_NFS_COMMON_H

#include <linux/errno.h>
#include <uapi/linux/nfs.h>

/* Mapping from NFS error code to "errno" error code. */

int nfs_stat_to_errno(enum nfs_stat status);
int nfs4_stat_to_errno(int stat);

__u32 nfs_localio_errno_to_nfs4_stat(int errno);

#endif /* _LINUX_NFS_COMMON_H */
