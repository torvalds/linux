/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 Mike Snitzer <snitzer@hammerspace.com>
 * Copyright (C) 2024 NeilBrown <neilb@suse.de>
 */
#ifndef __LINUX_NFSLOCALIO_H
#define __LINUX_NFSLOCALIO_H

#if IS_ENABLED(CONFIG_NFS_LOCALIO)

#include <linux/module.h>
#include <linux/list.h>
#include <linux/uuid.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/svcauth.h>
#include <linux/nfs.h>
#include <net/net_namespace.h>

/*
 * Useful to allow a client to negotiate if localio
 * possible with its server.
 *
 * See Documentation/filesystems/nfs/localio.rst for more detail.
 */
typedef struct {
	uuid_t uuid;
	struct list_head list;
	struct net __rcu *net; /* nfsd's network namespace */
	struct auth_domain *dom; /* auth_domain for localio */
} nfs_uuid_t;

void nfs_uuid_begin(nfs_uuid_t *);
void nfs_uuid_end(nfs_uuid_t *);
void nfs_uuid_is_local(const uuid_t *, struct list_head *,
		       struct net *, struct auth_domain *, struct module *);
void nfs_uuid_invalidate_clients(struct list_head *list);
void nfs_uuid_invalidate_one_client(nfs_uuid_t *nfs_uuid);

struct nfsd_file;

/* localio needs to map filehandle -> struct nfsd_file */
extern struct nfsd_file *
nfsd_open_local_fh(struct net *, struct auth_domain *, struct rpc_clnt *,
		   const struct cred *, const struct nfs_fh *,
		   const fmode_t) __must_hold(rcu);

struct nfsd_localio_operations {
	bool (*nfsd_serv_try_get)(struct net *);
	void (*nfsd_serv_put)(struct net *);
	struct nfsd_file *(*nfsd_open_local_fh)(struct net *,
						struct auth_domain *,
						struct rpc_clnt *,
						const struct cred *,
						const struct nfs_fh *,
						const fmode_t);
	void (*nfsd_file_put_local)(struct nfsd_file *);
	struct file *(*nfsd_file_file)(struct nfsd_file *);
} ____cacheline_aligned;

extern void nfsd_localio_ops_init(void);
extern const struct nfsd_localio_operations *nfs_to;

#else   /* CONFIG_NFS_LOCALIO */
static inline void nfsd_localio_ops_init(void)
{
}
#endif  /* CONFIG_NFS_LOCALIO */

#endif  /* __LINUX_NFSLOCALIO_H */
