/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 Hammerspace Inc
 */

#ifndef __NFS_SYSFS_H
#define __NFS_SYSFS_H

#define CONTAINER_ID_MAXLEN (64)

struct nfs_netns_client {
	struct kobject kobject;
	struct kobject nfs_net_kobj;
	struct net *net;
	const char __rcu *identifier;
};

extern struct kobject *nfs_net_kobj;

extern int nfs_sysfs_init(void);
extern void nfs_sysfs_exit(void);

void nfs_netns_sysfs_setup(struct nfs_net *netns, struct net *net);
void nfs_netns_sysfs_destroy(struct nfs_net *netns);

void nfs_sysfs_link_rpc_client(struct nfs_server *server,
			struct rpc_clnt *clnt, const char *sysfs_prefix);
void nfs_sysfs_add_server(struct nfs_server *s);
void nfs_sysfs_move_server_to_sb(struct super_block *s);
void nfs_sysfs_move_sb_to_server(struct nfs_server *s);
void nfs_sysfs_remove_server(struct nfs_server *s);

#endif
