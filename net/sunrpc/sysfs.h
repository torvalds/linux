// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Anna Schumaker <Anna.Schumaker@Netapp.com>
 */
#ifndef __SUNRPC_SYSFS_H
#define __SUNRPC_SYSFS_H

struct rpc_sysfs_client {
	struct kobject kobject;
	struct net *net;
};

int rpc_sysfs_init(void);
void rpc_sysfs_exit(void);

void rpc_sysfs_client_setup(struct rpc_clnt *clnt, struct net *net);
void rpc_sysfs_client_destroy(struct rpc_clnt *clnt);

#endif
