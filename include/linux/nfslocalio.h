/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 Mike Snitzer <snitzer@hammerspace.com>
 * Copyright (C) 2024 NeilBrown <neilb@suse.de>
 */
#ifndef __LINUX_NFSLOCALIO_H
#define __LINUX_NFSLOCALIO_H

/* nfsd_file structure is purposely kept opaque to NFS client */
struct nfsd_file;

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

void nfs_uuid_init(nfs_uuid_t *);
bool nfs_uuid_begin(nfs_uuid_t *);
void nfs_uuid_end(nfs_uuid_t *);
void nfs_uuid_is_local(const uuid_t *, struct list_head *,
		       struct net *, struct auth_domain *, struct module *);
void nfs_uuid_invalidate_clients(struct list_head *list);
void nfs_uuid_invalidate_one_client(nfs_uuid_t *nfs_uuid);

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
	struct net *(*nfsd_file_put_local)(struct nfsd_file *);
	struct file *(*nfsd_file_file)(struct nfsd_file *);
} ____cacheline_aligned;

extern void nfsd_localio_ops_init(void);
extern const struct nfsd_localio_operations *nfs_to;

struct nfsd_file *nfs_open_local_fh(nfs_uuid_t *,
		   struct rpc_clnt *, const struct cred *,
		   const struct nfs_fh *, const fmode_t);

static inline void nfs_to_nfsd_net_put(struct net *net)
{
	/*
	 * Once reference to nfsd_serv is dropped, NFSD could be
	 * unloaded, so ensure safe return from nfsd_file_put_local()
	 * by always taking RCU.
	 */
	rcu_read_lock();
	nfs_to->nfsd_serv_put(net);
	rcu_read_unlock();
}

static inline void nfs_to_nfsd_file_put_local(struct nfsd_file *localio)
{
	/*
	 * Must not hold RCU otherwise nfsd_file_put() can easily trigger:
	 * "Voluntary context switch within RCU read-side critical section!"
	 * by scheduling deep in underlying filesystem (e.g. XFS).
	 */
	struct net *net = nfs_to->nfsd_file_put_local(localio);

	nfs_to_nfsd_net_put(net);
}

#else   /* CONFIG_NFS_LOCALIO */
static inline void nfsd_localio_ops_init(void)
{
}
static inline void nfs_to_nfsd_file_put_local(struct nfsd_file *localio)
{
}
#endif  /* CONFIG_NFS_LOCALIO */

#endif  /* __LINUX_NFSLOCALIO_H */
