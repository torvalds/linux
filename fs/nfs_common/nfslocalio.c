// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Mike Snitzer <snitzer@hammerspace.com>
 * Copyright (C) 2024 NeilBrown <neilb@suse.de>
 */

#include <linux/module.h>
#include <linux/list.h>
#include <linux/nfslocalio.h>
#include <linux/nfs3.h>
#include <linux/nfs4.h>
#include <linux/nfs_fs.h>
#include <net/netns/generic.h>

#include "localio_trace.h"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("NFS localio protocol bypass support");

static DEFINE_SPINLOCK(nfs_uuids_lock);

/*
 * Global list of nfs_uuid_t instances
 * that is protected by nfs_uuids_lock.
 */
static LIST_HEAD(nfs_uuids);

/*
 * Lock ordering:
 * 1: nfs_uuid->lock
 * 2: nfs_uuids_lock
 * 3: nfs_uuid->list_lock (aka nn->local_clients_lock)
 *
 * May skip locks in select cases, but never hold multiple
 * locks out of order.
 */

void nfs_uuid_init(nfs_uuid_t *nfs_uuid)
{
	RCU_INIT_POINTER(nfs_uuid->net, NULL);
	nfs_uuid->dom = NULL;
	nfs_uuid->list_lock = NULL;
	INIT_LIST_HEAD(&nfs_uuid->list);
	INIT_LIST_HEAD(&nfs_uuid->files);
	spin_lock_init(&nfs_uuid->lock);
	nfs_uuid->nfs3_localio_probe_count = 0;
}
EXPORT_SYMBOL_GPL(nfs_uuid_init);

bool nfs_uuid_begin(nfs_uuid_t *nfs_uuid)
{
	spin_lock(&nfs_uuid->lock);
	if (rcu_access_pointer(nfs_uuid->net)) {
		/* This nfs_uuid is already in use */
		spin_unlock(&nfs_uuid->lock);
		return false;
	}

	spin_lock(&nfs_uuids_lock);
	if (!list_empty(&nfs_uuid->list)) {
		/* This nfs_uuid is already in use */
		spin_unlock(&nfs_uuids_lock);
		spin_unlock(&nfs_uuid->lock);
		return false;
	}
	list_add_tail(&nfs_uuid->list, &nfs_uuids);
	spin_unlock(&nfs_uuids_lock);

	uuid_gen(&nfs_uuid->uuid);
	spin_unlock(&nfs_uuid->lock);

	return true;
}
EXPORT_SYMBOL_GPL(nfs_uuid_begin);

void nfs_uuid_end(nfs_uuid_t *nfs_uuid)
{
	if (!rcu_access_pointer(nfs_uuid->net)) {
		spin_lock(&nfs_uuid->lock);
		if (!rcu_access_pointer(nfs_uuid->net)) {
			/* Not local, remove from nfs_uuids */
			spin_lock(&nfs_uuids_lock);
			list_del_init(&nfs_uuid->list);
			spin_unlock(&nfs_uuids_lock);
		}
		spin_unlock(&nfs_uuid->lock);
        }
}
EXPORT_SYMBOL_GPL(nfs_uuid_end);

static nfs_uuid_t * nfs_uuid_lookup_locked(const uuid_t *uuid)
{
	nfs_uuid_t *nfs_uuid;

	list_for_each_entry(nfs_uuid, &nfs_uuids, list)
		if (uuid_equal(&nfs_uuid->uuid, uuid))
			return nfs_uuid;

	return NULL;
}

static struct module *nfsd_mod;

void nfs_uuid_is_local(const uuid_t *uuid, struct list_head *list,
		       spinlock_t *list_lock, struct net *net,
		       struct auth_domain *dom, struct module *mod)
{
	nfs_uuid_t *nfs_uuid;

	spin_lock(&nfs_uuids_lock);
	nfs_uuid = nfs_uuid_lookup_locked(uuid);
	if (!nfs_uuid) {
		spin_unlock(&nfs_uuids_lock);
		return;
	}

	/*
	 * We don't hold a ref on the net, but instead put
	 * ourselves on @list (nn->local_clients) so the net
	 * pointer can be invalidated.
	 */
	spin_lock(list_lock); /* list_lock is nn->local_clients_lock */
	list_move(&nfs_uuid->list, list);
	spin_unlock(list_lock);

	spin_unlock(&nfs_uuids_lock);
	/* Once nfs_uuid is parented to @list, avoid global nfs_uuids_lock */
	spin_lock(&nfs_uuid->lock);

	__module_get(mod);
	nfsd_mod = mod;

	nfs_uuid->list_lock = list_lock;
	kref_get(&dom->ref);
	nfs_uuid->dom = dom;
	rcu_assign_pointer(nfs_uuid->net, net);
	spin_unlock(&nfs_uuid->lock);
}
EXPORT_SYMBOL_GPL(nfs_uuid_is_local);

void nfs_localio_enable_client(struct nfs_client *clp)
{
	/* nfs_uuid_is_local() does the actual enablement */
	trace_nfs_localio_enable_client(clp);
}
EXPORT_SYMBOL_GPL(nfs_localio_enable_client);

/*
 * Cleanup the nfs_uuid_t embedded in an nfs_client.
 * This is the long-form of nfs_uuid_init().
 */
static bool nfs_uuid_put(nfs_uuid_t *nfs_uuid)
{
	LIST_HEAD(local_files);
	struct nfs_file_localio *nfl, *tmp;

	spin_lock(&nfs_uuid->lock);
	if (unlikely(!rcu_access_pointer(nfs_uuid->net))) {
		spin_unlock(&nfs_uuid->lock);
		return false;
	}
	RCU_INIT_POINTER(nfs_uuid->net, NULL);

	if (nfs_uuid->dom) {
		auth_domain_put(nfs_uuid->dom);
		nfs_uuid->dom = NULL;
	}

	list_splice_init(&nfs_uuid->files, &local_files);
	spin_unlock(&nfs_uuid->lock);

	/* Walk list of files and ensure their last references dropped */
	list_for_each_entry_safe(nfl, tmp, &local_files, list) {
		nfs_close_local_fh(nfl);
		cond_resched();
	}

	spin_lock(&nfs_uuid->lock);
	BUG_ON(!list_empty(&nfs_uuid->files));

	/* Remove client from nn->local_clients */
	if (nfs_uuid->list_lock) {
		spin_lock(nfs_uuid->list_lock);
		BUG_ON(list_empty(&nfs_uuid->list));
		list_del_init(&nfs_uuid->list);
		spin_unlock(nfs_uuid->list_lock);
		nfs_uuid->list_lock = NULL;
	}

	module_put(nfsd_mod);
	spin_unlock(&nfs_uuid->lock);

	return true;
}

void nfs_localio_disable_client(struct nfs_client *clp)
{
	if (nfs_uuid_put(&clp->cl_uuid))
		trace_nfs_localio_disable_client(clp);
}
EXPORT_SYMBOL_GPL(nfs_localio_disable_client);

void nfs_localio_invalidate_clients(struct list_head *nn_local_clients,
				    spinlock_t *nn_local_clients_lock)
{
	LIST_HEAD(local_clients);
	nfs_uuid_t *nfs_uuid, *tmp;
	struct nfs_client *clp;

	spin_lock(nn_local_clients_lock);
	list_splice_init(nn_local_clients, &local_clients);
	spin_unlock(nn_local_clients_lock);
	list_for_each_entry_safe(nfs_uuid, tmp, &local_clients, list) {
		if (WARN_ON(nfs_uuid->list_lock != nn_local_clients_lock))
			break;
		clp = container_of(nfs_uuid, struct nfs_client, cl_uuid);
		nfs_localio_disable_client(clp);
	}
}
EXPORT_SYMBOL_GPL(nfs_localio_invalidate_clients);

static void nfs_uuid_add_file(nfs_uuid_t *nfs_uuid, struct nfs_file_localio *nfl)
{
	/* Add nfl to nfs_uuid->files if it isn't already */
	spin_lock(&nfs_uuid->lock);
	if (list_empty(&nfl->list)) {
		rcu_assign_pointer(nfl->nfs_uuid, nfs_uuid);
		list_add_tail(&nfl->list, &nfs_uuid->files);
	}
	spin_unlock(&nfs_uuid->lock);
}

/*
 * Caller is responsible for calling nfsd_net_put and
 * nfsd_file_put (via nfs_to_nfsd_file_put_local).
 */
struct nfsd_file *nfs_open_local_fh(nfs_uuid_t *uuid,
		   struct rpc_clnt *rpc_clnt, const struct cred *cred,
		   const struct nfs_fh *nfs_fh, struct nfs_file_localio *nfl,
		   const fmode_t fmode)
{
	struct net *net;
	struct nfsd_file *localio;

	/*
	 * Not running in nfsd context, so must safely get reference on nfsd_serv.
	 * But the server may already be shutting down, if so disallow new localio.
	 * uuid->net is NOT a counted reference, but rcu_read_lock() ensures that
	 * if uuid->net is not NULL, then calling nfsd_net_try_get() is safe
	 * and if it succeeds we will have an implied reference to the net.
	 *
	 * Otherwise NFS may not have ref on NFSD and therefore cannot safely
	 * make 'nfs_to' calls.
	 */
	rcu_read_lock();
	net = rcu_dereference(uuid->net);
	if (!net || !nfs_to->nfsd_net_try_get(net)) {
		rcu_read_unlock();
		return ERR_PTR(-ENXIO);
	}
	rcu_read_unlock();
	/* We have an implied reference to net thanks to nfsd_net_try_get */
	localio = nfs_to->nfsd_open_local_fh(net, uuid->dom, rpc_clnt,
					     cred, nfs_fh, fmode);
	if (IS_ERR(localio))
		nfs_to_nfsd_net_put(net);
	else
		nfs_uuid_add_file(uuid, nfl);

	return localio;
}
EXPORT_SYMBOL_GPL(nfs_open_local_fh);

void nfs_close_local_fh(struct nfs_file_localio *nfl)
{
	struct nfsd_file *ro_nf = NULL;
	struct nfsd_file *rw_nf = NULL;
	nfs_uuid_t *nfs_uuid;

	rcu_read_lock();
	nfs_uuid = rcu_dereference(nfl->nfs_uuid);
	if (!nfs_uuid) {
		/* regular (non-LOCALIO) NFS will hammer this */
		rcu_read_unlock();
		return;
	}

	ro_nf = rcu_access_pointer(nfl->ro_file);
	rw_nf = rcu_access_pointer(nfl->rw_file);
	if (ro_nf || rw_nf) {
		spin_lock(&nfs_uuid->lock);
		if (ro_nf)
			ro_nf = rcu_dereference_protected(xchg(&nfl->ro_file, NULL), 1);
		if (rw_nf)
			rw_nf = rcu_dereference_protected(xchg(&nfl->rw_file, NULL), 1);

		/* Remove nfl from nfs_uuid->files list */
		RCU_INIT_POINTER(nfl->nfs_uuid, NULL);
		list_del_init(&nfl->list);
		spin_unlock(&nfs_uuid->lock);
		rcu_read_unlock();

		if (ro_nf)
			nfs_to_nfsd_file_put_local(ro_nf);
		if (rw_nf)
			nfs_to_nfsd_file_put_local(rw_nf);
		return;
	}
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(nfs_close_local_fh);

/*
 * The NFS LOCALIO code needs to call into NFSD using various symbols,
 * but cannot be statically linked, because that will make the NFS
 * module always depend on the NFSD module.
 *
 * 'nfs_to' provides NFS access to NFSD functions needed for LOCALIO,
 * its lifetime is tightly coupled to the NFSD module and will always
 * be available to NFS LOCALIO because any successful client<->server
 * LOCALIO handshake results in a reference on the NFSD module (above),
 * so NFS implicitly holds a reference to the NFSD module and its
 * functions in the 'nfs_to' nfsd_localio_operations cannot disappear.
 *
 * If the last NFS client using LOCALIO disconnects (and its reference
 * on NFSD dropped) then NFSD could be unloaded, resulting in 'nfs_to'
 * functions being invalid pointers. But if NFSD isn't loaded then NFS
 * will not be able to handshake with NFSD and will have no cause to
 * try to call 'nfs_to' function pointers. If/when NFSD is reloaded it
 * will reinitialize the 'nfs_to' function pointers and make LOCALIO
 * possible.
 */
const struct nfsd_localio_operations *nfs_to;
EXPORT_SYMBOL_GPL(nfs_to);
