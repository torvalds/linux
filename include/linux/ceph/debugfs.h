/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _FS_CEPH_DEBUGFS_H
#define _FS_CEPH_DEBUGFS_H

#include <linux/ceph/types.h>

/* debugfs.c */
extern void ceph_debugfs_init(void);
extern void ceph_debugfs_cleanup(void);
extern void ceph_debugfs_client_init(struct ceph_client *client);
extern void ceph_debugfs_client_cleanup(struct ceph_client *client);

#endif

