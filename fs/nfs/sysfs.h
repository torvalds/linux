// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 Hammerspace Inc
 */

#ifndef __NFS_SYSFS_H
#define __NFS_SYSFS_H

#define CONTAINER_ID_MAXLEN (64)

struct nfs_netns_client {
	struct kobject kobject;
	struct net *net;
	const char *identifier;
};

extern struct kobject *nfs_client_kobj;

extern int nfs_sysfs_init(void);
extern void nfs_sysfs_exit(void);

void nfs_netns_sysfs_setup(struct nfs_net *netns, struct net *net);
void nfs_netns_sysfs_destroy(struct nfs_net *netns);

#endif
