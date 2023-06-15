// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Anna Schumaker <Anna.Schumaker@Netapp.com>
 */
#ifndef __SUNRPC_SYSFS_H
#define __SUNRPC_SYSFS_H

struct rpc_sysfs_xprt_switch {
	struct kobject kobject;
	struct net *net;
	struct rpc_xprt_switch *xprt_switch;
	struct rpc_xprt *xprt;
};

struct rpc_sysfs_xprt {
	struct kobject kobject;
	struct rpc_xprt *xprt;
	struct rpc_xprt_switch *xprt_switch;
};

int rpc_sysfs_init(void);
void rpc_sysfs_exit(void);

void rpc_sysfs_client_setup(struct rpc_clnt *clnt,
			    struct rpc_xprt_switch *xprt_switch,
			    struct net *net);
void rpc_sysfs_client_destroy(struct rpc_clnt *clnt);
void rpc_sysfs_xprt_switch_setup(struct rpc_xprt_switch *xprt_switch,
				 struct rpc_xprt *xprt, gfp_t gfp_flags);
void rpc_sysfs_xprt_switch_destroy(struct rpc_xprt_switch *xprt);
void rpc_sysfs_xprt_setup(struct rpc_xprt_switch *xprt_switch,
			  struct rpc_xprt *xprt, gfp_t gfp_flags);
void rpc_sysfs_xprt_destroy(struct rpc_xprt *xprt);

#endif
