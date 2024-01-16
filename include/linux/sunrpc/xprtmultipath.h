/* SPDX-License-Identifier: GPL-2.0 */
/*
 * RPC client multipathing definitions
 *
 * Copyright (c) 2015, 2016, Primary Data, Inc. All rights reserved.
 *
 * Trond Myklebust <trond.myklebust@primarydata.com>
 */
#ifndef _NET_SUNRPC_XPRTMULTIPATH_H
#define _NET_SUNRPC_XPRTMULTIPATH_H

struct rpc_xprt_iter_ops;
struct rpc_sysfs_xprt_switch;
struct rpc_xprt_switch {
	spinlock_t		xps_lock;
	struct kref		xps_kref;

	unsigned int		xps_id;
	unsigned int		xps_nxprts;
	unsigned int		xps_nactive;
	unsigned int		xps_nunique_destaddr_xprts;
	atomic_long_t		xps_queuelen;
	struct list_head	xps_xprt_list;

	struct net *		xps_net;

	const struct rpc_xprt_iter_ops *xps_iter_ops;

	struct rpc_sysfs_xprt_switch *xps_sysfs;
	struct rcu_head		xps_rcu;
};

struct rpc_xprt_iter {
	struct rpc_xprt_switch __rcu *xpi_xpswitch;
	struct rpc_xprt *	xpi_cursor;

	const struct rpc_xprt_iter_ops *xpi_ops;
};


struct rpc_xprt_iter_ops {
	void (*xpi_rewind)(struct rpc_xprt_iter *);
	struct rpc_xprt *(*xpi_xprt)(struct rpc_xprt_iter *);
	struct rpc_xprt *(*xpi_next)(struct rpc_xprt_iter *);
};

extern struct rpc_xprt_switch *xprt_switch_alloc(struct rpc_xprt *xprt,
		gfp_t gfp_flags);

extern struct rpc_xprt_switch *xprt_switch_get(struct rpc_xprt_switch *xps);
extern void xprt_switch_put(struct rpc_xprt_switch *xps);

extern void rpc_xprt_switch_set_roundrobin(struct rpc_xprt_switch *xps);

extern void rpc_xprt_switch_add_xprt(struct rpc_xprt_switch *xps,
		struct rpc_xprt *xprt);
extern void rpc_xprt_switch_remove_xprt(struct rpc_xprt_switch *xps,
		struct rpc_xprt *xprt, bool offline);

extern void xprt_iter_init(struct rpc_xprt_iter *xpi,
		struct rpc_xprt_switch *xps);

extern void xprt_iter_init_listall(struct rpc_xprt_iter *xpi,
		struct rpc_xprt_switch *xps);

extern void xprt_iter_init_listoffline(struct rpc_xprt_iter *xpi,
		struct rpc_xprt_switch *xps);

extern void xprt_iter_destroy(struct rpc_xprt_iter *xpi);

extern void xprt_iter_rewind(struct rpc_xprt_iter *xpi);

extern struct rpc_xprt_switch *xprt_iter_xchg_switch(
		struct rpc_xprt_iter *xpi,
		struct rpc_xprt_switch *newswitch);

extern struct rpc_xprt *xprt_iter_xprt(struct rpc_xprt_iter *xpi);
extern struct rpc_xprt *xprt_iter_get_xprt(struct rpc_xprt_iter *xpi);
extern struct rpc_xprt *xprt_iter_get_next(struct rpc_xprt_iter *xpi);

extern bool rpc_xprt_switch_has_addr(struct rpc_xprt_switch *xps,
		const struct sockaddr *sap);

extern void xprt_multipath_cleanup_ids(void);

#endif
