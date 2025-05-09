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

struct nfs_client;
struct nfs_file_localio;

/*
 * Useful to allow a client to negotiate if localio
 * possible with its server.
 *
 * See Documentation/filesystems/nfs/localio.rst for more detail.
 */
typedef struct {
	uuid_t uuid;
	unsigned nfs3_localio_probe_count;
	/* this struct is over a cacheline, avoid bouncing */
	spinlock_t ____cacheline_aligned lock;
	struct list_head list;
	spinlock_t *list_lock; /* nn->local_clients_lock */
	struct net __rcu *net; /* nfsd's network namespace */
	struct auth_domain *dom; /* auth_domain for localio */
	/* Local files to close when net is shut down or exports change */
	struct list_head files;
} nfs_uuid_t;

void nfs_uuid_init(nfs_uuid_t *);
bool nfs_uuid_begin(nfs_uuid_t *);
void nfs_uuid_end(nfs_uuid_t *);
void nfs_uuid_is_local(const uuid_t *, struct list_head *, spinlock_t *,
		       struct net *, struct auth_domain *, struct module *);

void nfs_localio_enable_client(struct nfs_client *clp);
void nfs_localio_disable_client(struct nfs_client *clp);
void nfs_localio_invalidate_clients(struct list_head *nn_local_clients,
				    spinlock_t *nn_local_clients_lock);

/* localio needs to map filehandle -> struct nfsd_file */
void nfs_close_local_fh(struct nfs_file_localio *);

struct nfsd_localio_operations {
	bool (*nfsd_net_try_get)(struct net *);
	void (*nfsd_net_put)(struct net *);
	struct nfsd_file *(*nfsd_open_local_fh)(struct net *,
						struct auth_domain *,
						struct rpc_clnt *,
						const struct cred *,
						const struct nfs_fh *,
						struct nfsd_file __rcu **pnf,
						const fmode_t);
	struct net *(*nfsd_file_put_local)(struct nfsd_file __rcu **);
	struct nfsd_file *(*nfsd_file_get_local)(struct nfsd_file *);
	struct file *(*nfsd_file_file)(struct nfsd_file *);
} ____cacheline_aligned;

extern void nfsd_localio_ops_init(void);
extern const struct nfsd_localio_operations *nfs_to;

struct nfsd_file *nfs_open_local_fh(nfs_uuid_t *,
		   struct rpc_clnt *, const struct cred *,
		   const struct nfs_fh *, struct nfs_file_localio *,
		   struct nfsd_file __rcu **pnf,
		   const fmode_t);

static inline void nfs_to_nfsd_net_put(struct net *net)
{
	/*
	 * Once reference to net (and associated nfsd_serv) is dropped, NFSD
	 * could be unloaded, so ensure safe return from nfsd_net_put() by
	 * always taking RCU.
	 */
	rcu_read_lock();
	nfs_to->nfsd_net_put(net);
	rcu_read_unlock();
}

static inline void nfs_to_nfsd_file_put_local(struct nfsd_file __rcu **localio)
{
	/*
	 * Either *localio must be guaranteed to be non-NULL, or caller
	 * must prevent nfsd shutdown from completing as nfs_close_local_fh()
	 * does by blocking the nfs_uuid from being finally put.
	 */
	struct net *net;

	net = nfs_to->nfsd_file_put_local(localio);

	if (net)
		nfs_to_nfsd_net_put(net);
}

#else   /* CONFIG_NFS_LOCALIO */

struct nfs_file_localio;
static inline void nfs_close_local_fh(struct nfs_file_localio *nfl)
{
}
static inline void nfsd_localio_ops_init(void)
{
}
struct nfs_client;
static inline void nfs_localio_disable_client(struct nfs_client *clp)
{
}

#endif  /* CONFIG_NFS_LOCALIO */

#endif  /* __LINUX_NFSLOCALIO_H */
