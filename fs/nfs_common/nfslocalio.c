// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Mike Snitzer <snitzer@hammerspace.com>
 * Copyright (C) 2024 NeilBrown <neilb@suse.de>
 */

#include <linux/module.h>
#include <linux/list.h>
#include <linux/nfslocalio.h>
#include <net/netns/generic.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("NFS localio protocol bypass support");

static DEFINE_SPINLOCK(nfs_uuid_lock);

/*
 * Global list of nfs_uuid_t instances
 * that is protected by nfs_uuid_lock.
 */
static LIST_HEAD(nfs_uuids);

void nfs_uuid_init(nfs_uuid_t *nfs_uuid)
{
	nfs_uuid->net = NULL;
	nfs_uuid->dom = NULL;
	INIT_LIST_HEAD(&nfs_uuid->list);
}
EXPORT_SYMBOL_GPL(nfs_uuid_init);

bool nfs_uuid_begin(nfs_uuid_t *nfs_uuid)
{
	spin_lock(&nfs_uuid_lock);
	/* Is this nfs_uuid already in use? */
	if (!list_empty(&nfs_uuid->list)) {
		spin_unlock(&nfs_uuid_lock);
		return false;
	}
	uuid_gen(&nfs_uuid->uuid);
	list_add_tail(&nfs_uuid->list, &nfs_uuids);
	spin_unlock(&nfs_uuid_lock);

	return true;
}
EXPORT_SYMBOL_GPL(nfs_uuid_begin);

void nfs_uuid_end(nfs_uuid_t *nfs_uuid)
{
	if (nfs_uuid->net == NULL) {
		spin_lock(&nfs_uuid_lock);
		if (nfs_uuid->net == NULL)
			list_del_init(&nfs_uuid->list);
		spin_unlock(&nfs_uuid_lock);
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
		       struct net *net, struct auth_domain *dom,
		       struct module *mod)
{
	nfs_uuid_t *nfs_uuid;

	spin_lock(&nfs_uuid_lock);
	nfs_uuid = nfs_uuid_lookup_locked(uuid);
	if (nfs_uuid) {
		kref_get(&dom->ref);
		nfs_uuid->dom = dom;
		/*
		 * We don't hold a ref on the net, but instead put
		 * ourselves on a list so the net pointer can be
		 * invalidated.
		 */
		list_move(&nfs_uuid->list, list);
		rcu_assign_pointer(nfs_uuid->net, net);

		__module_get(mod);
		nfsd_mod = mod;
	}
	spin_unlock(&nfs_uuid_lock);
}
EXPORT_SYMBOL_GPL(nfs_uuid_is_local);

static void nfs_uuid_put_locked(nfs_uuid_t *nfs_uuid)
{
	if (nfs_uuid->net) {
		module_put(nfsd_mod);
		nfs_uuid->net = NULL;
	}
	if (nfs_uuid->dom) {
		auth_domain_put(nfs_uuid->dom);
		nfs_uuid->dom = NULL;
	}
	list_del_init(&nfs_uuid->list);
}

void nfs_uuid_invalidate_clients(struct list_head *list)
{
	nfs_uuid_t *nfs_uuid, *tmp;

	spin_lock(&nfs_uuid_lock);
	list_for_each_entry_safe(nfs_uuid, tmp, list, list)
		nfs_uuid_put_locked(nfs_uuid);
	spin_unlock(&nfs_uuid_lock);
}
EXPORT_SYMBOL_GPL(nfs_uuid_invalidate_clients);

void nfs_uuid_invalidate_one_client(nfs_uuid_t *nfs_uuid)
{
	if (nfs_uuid->net) {
		spin_lock(&nfs_uuid_lock);
		nfs_uuid_put_locked(nfs_uuid);
		spin_unlock(&nfs_uuid_lock);
	}
}
EXPORT_SYMBOL_GPL(nfs_uuid_invalidate_one_client);

struct nfsd_file *nfs_open_local_fh(nfs_uuid_t *uuid,
		   struct rpc_clnt *rpc_clnt, const struct cred *cred,
		   const struct nfs_fh *nfs_fh, const fmode_t fmode)
{
	struct net *net;
	struct nfsd_file *localio;

	/*
	 * Not running in nfsd context, so must safely get reference on nfsd_serv.
	 * But the server may already be shutting down, if so disallow new localio.
	 * uuid->net is NOT a counted reference, but rcu_read_lock() ensures that
	 * if uuid->net is not NULL, then calling nfsd_serv_try_get() is safe
	 * and if it succeeds we will have an implied reference to the net.
	 *
	 * Otherwise NFS may not have ref on NFSD and therefore cannot safely
	 * make 'nfs_to' calls.
	 */
	rcu_read_lock();
	net = rcu_dereference(uuid->net);
	if (!net || !nfs_to->nfsd_serv_try_get(net)) {
		rcu_read_unlock();
		return ERR_PTR(-ENXIO);
	}
	rcu_read_unlock();
	/* We have an implied reference to net thanks to nfsd_serv_try_get */
	localio = nfs_to->nfsd_open_local_fh(net, uuid->dom, rpc_clnt,
					     cred, nfs_fh, fmode);
	if (IS_ERR(localio)) {
		rcu_read_lock();
		nfs_to->nfsd_serv_put(net);
		rcu_read_unlock();
	}
	return localio;
}
EXPORT_SYMBOL_GPL(nfs_open_local_fh);

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
