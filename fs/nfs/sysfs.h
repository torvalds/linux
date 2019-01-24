// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 Hammerspace Inc
 */

#ifndef __NFS_SYSFS_H
#define __NFS_SYSFS_H


extern struct kobject *nfs_client_kobj;

extern int nfs_sysfs_init(void);
extern void nfs_sysfs_exit(void);

#endif
