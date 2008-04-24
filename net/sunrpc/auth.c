/*
 * linux/net/sunrpc/auth.c
 *
 * Generic RPC client authentication API.
 *
 * Copyright (C) 1996, Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/hash.h>
#include <linux/sunrpc/clnt.h>
#include <linux/spinlock.h>

#ifdef RPC_DEBUG
# define RPCDBG_FACILITY	RPCDBG_AUTH
#endif

static DEFINE_SPINLOCK(rpc_authflavor_lock);
static const struct rpc_authops *auth_flavors[RPC_AUTH_MAXFLAVOR] = {
	&authnull_ops,		/* AUTH_NULL */
	&authunix_ops,		/* AUTH_UNIX */
	NULL,			/* others can be loadable modules */
};

static LIST_HEAD(cred_unused);
static unsigned long number_cred_unused;

static u32
pseudoflavor_to_flavor(u32 flavor) {
	if (flavor >= RPC_AUTH_MAXFLAVOR)
		return RPC_AUTH_GSS;
	return flavor;
}

int
rpcauth_register(const struct rpc_authops *ops)
{
	rpc_authflavor_t flavor;
	int ret = -EPERM;

	if ((flavor = ops->au_flavor) >= RPC_AUTH_MAXFLAVOR)
		return -EINVAL;
	spin_lock(&rpc_authflavor_lock);
	if (auth_flavors[flavor] == NULL) {
		auth_flavors[flavor] = ops;
		ret = 0;
	}
	spin_unlock(&rpc_authflavor_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(rpcauth_register);

int
rpcauth_unregister(const struct rpc_authops *ops)
{
	rpc_authflavor_t flavor;
	int ret = -EPERM;

	if ((flavor = ops->au_flavor) >= RPC_AUTH_MAXFLAVOR)
		return -EINVAL;
	spin_lock(&rpc_authflavor_lock);
	if (auth_flavors[flavor] == ops) {
		auth_flavors[flavor] = NULL;
		ret = 0;
	}
	spin_unlock(&rpc_authflavor_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(rpcauth_unregister);

struct rpc_auth *
rpcauth_create(rpc_authflavor_t pseudoflavor, struct rpc_clnt *clnt)
{
	struct rpc_auth		*auth;
	const struct rpc_authops *ops;
	u32			flavor = pseudoflavor_to_flavor(pseudoflavor);

	auth = ERR_PTR(-EINVAL);
	if (flavor >= RPC_AUTH_MAXFLAVOR)
		goto out;

#ifdef CONFIG_KMOD
	if ((ops = auth_flavors[flavor]) == NULL)
		request_module("rpc-auth-%u", flavor);
#endif
	spin_lock(&rpc_authflavor_lock);
	ops = auth_flavors[flavor];
	if (ops == NULL || !try_module_get(ops->owner)) {
		spin_unlock(&rpc_authflavor_lock);
		goto out;
	}
	spin_unlock(&rpc_authflavor_lock);
	auth = ops->create(clnt, pseudoflavor);
	module_put(ops->owner);
	if (IS_ERR(auth))
		return auth;
	if (clnt->cl_auth)
		rpcauth_release(clnt->cl_auth);
	clnt->cl_auth = auth;

out:
	return auth;
}
EXPORT_SYMBOL_GPL(rpcauth_create);

void
rpcauth_release(struct rpc_auth *auth)
{
	if (!atomic_dec_and_test(&auth->au_count))
		return;
	auth->au_ops->destroy(auth);
}

static DEFINE_SPINLOCK(rpc_credcache_lock);

static void
rpcauth_unhash_cred_locked(struct rpc_cred *cred)
{
	hlist_del_rcu(&cred->cr_hash);
	smp_mb__before_clear_bit();
	clear_bit(RPCAUTH_CRED_HASHED, &cred->cr_flags);
}

static void
rpcauth_unhash_cred(struct rpc_cred *cred)
{
	spinlock_t *cache_lock;

	cache_lock = &cred->cr_auth->au_credcache->lock;
	spin_lock(cache_lock);
	if (atomic_read(&cred->cr_count) == 0)
		rpcauth_unhash_cred_locked(cred);
	spin_unlock(cache_lock);
}

/*
 * Initialize RPC credential cache
 */
int
rpcauth_init_credcache(struct rpc_auth *auth)
{
	struct rpc_cred_cache *new;
	int i;

	new = kmalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		return -ENOMEM;
	for (i = 0; i < RPC_CREDCACHE_NR; i++)
		INIT_HLIST_HEAD(&new->hashtable[i]);
	spin_lock_init(&new->lock);
	auth->au_credcache = new;
	return 0;
}
EXPORT_SYMBOL_GPL(rpcauth_init_credcache);

/*
 * Destroy a list of credentials
 */
static inline
void rpcauth_destroy_credlist(struct list_head *head)
{
	struct rpc_cred *cred;

	while (!list_empty(head)) {
		cred = list_entry(head->next, struct rpc_cred, cr_lru);
		list_del_init(&cred->cr_lru);
		put_rpccred(cred);
	}
}

/*
 * Clear the RPC credential cache, and delete those credentials
 * that are not referenced.
 */
void
rpcauth_clear_credcache(struct rpc_cred_cache *cache)
{
	LIST_HEAD(free);
	struct hlist_head *head;
	struct rpc_cred	*cred;
	int		i;

	spin_lock(&rpc_credcache_lock);
	spin_lock(&cache->lock);
	for (i = 0; i < RPC_CREDCACHE_NR; i++) {
		head = &cache->hashtable[i];
		while (!hlist_empty(head)) {
			cred = hlist_entry(head->first, struct rpc_cred, cr_hash);
			get_rpccred(cred);
			if (!list_empty(&cred->cr_lru)) {
				list_del(&cred->cr_lru);
				number_cred_unused--;
			}
			list_add_tail(&cred->cr_lru, &free);
			rpcauth_unhash_cred_locked(cred);
		}
	}
	spin_unlock(&cache->lock);
	spin_unlock(&rpc_credcache_lock);
	rpcauth_destroy_credlist(&free);
}

/*
 * Destroy the RPC credential cache
 */
void
rpcauth_destroy_credcache(struct rpc_auth *auth)
{
	struct rpc_cred_cache *cache = auth->au_credcache;

	if (cache) {
		auth->au_credcache = NULL;
		rpcauth_clear_credcache(cache);
		kfree(cache);
	}
}
EXPORT_SYMBOL_GPL(rpcauth_destroy_credcache);


#define RPC_AUTH_EXPIRY_MORATORIUM (60 * HZ)

/*
 * Remove stale credentials. Avoid sleeping inside the loop.
 */
static int
rpcauth_prune_expired(struct list_head *free, int nr_to_scan)
{
	spinlock_t *cache_lock;
	struct rpc_cred *cred;
	unsigned long expired = jiffies - RPC_AUTH_EXPIRY_MORATORIUM;

	while (!list_empty(&cred_unused)) {
		cred = list_entry(cred_unused.next, struct rpc_cred, cr_lru);
		list_del_init(&cred->cr_lru);
		number_cred_unused--;
		if (atomic_read(&cred->cr_count) != 0)
			continue;
		/* Enforce a 5 second garbage collection moratorium */
		if (time_in_range(cred->cr_expire, expired, jiffies) &&
		    test_bit(RPCAUTH_CRED_UPTODATE, &cred->cr_flags) != 0)
			continue;
		cache_lock = &cred->cr_auth->au_credcache->lock;
		spin_lock(cache_lock);
		if (atomic_read(&cred->cr_count) == 0) {
			get_rpccred(cred);
			list_add_tail(&cred->cr_lru, free);
			rpcauth_unhash_cred_locked(cred);
			nr_to_scan--;
		}
		spin_unlock(cache_lock);
		if (nr_to_scan == 0)
			break;
	}
	return nr_to_scan;
}

/*
 * Run memory cache shrinker.
 */
static int
rpcauth_cache_shrinker(int nr_to_scan, gfp_t gfp_mask)
{
	LIST_HEAD(free);
	int res;

	if (list_empty(&cred_unused))
		return 0;
	spin_lock(&rpc_credcache_lock);
	nr_to_scan = rpcauth_prune_expired(&free, nr_to_scan);
	res = (number_cred_unused / 100) * sysctl_vfs_cache_pressure;
	spin_unlock(&rpc_credcache_lock);
	rpcauth_destroy_credlist(&free);
	return res;
}

/*
 * Look up a process' credentials in the authentication cache
 */
struct rpc_cred *
rpcauth_lookup_credcache(struct rpc_auth *auth, struct auth_cred * acred,
		int flags)
{
	LIST_HEAD(free);
	struct rpc_cred_cache *cache = auth->au_credcache;
	struct hlist_node *pos;
	struct rpc_cred	*cred = NULL,
			*entry, *new;
	unsigned int nr;

	nr = hash_long(acred->uid, RPC_CREDCACHE_HASHBITS);

	rcu_read_lock();
	hlist_for_each_entry_rcu(entry, pos, &cache->hashtable[nr], cr_hash) {
		if (!entry->cr_ops->crmatch(acred, entry, flags))
			continue;
		spin_lock(&cache->lock);
		if (test_bit(RPCAUTH_CRED_HASHED, &entry->cr_flags) == 0) {
			spin_unlock(&cache->lock);
			continue;
		}
		cred = get_rpccred(entry);
		spin_unlock(&cache->lock);
		break;
	}
	rcu_read_unlock();

	if (cred != NULL)
		goto found;

	new = auth->au_ops->crcreate(auth, acred, flags);
	if (IS_ERR(new)) {
		cred = new;
		goto out;
	}

	spin_lock(&cache->lock);
	hlist_for_each_entry(entry, pos, &cache->hashtable[nr], cr_hash) {
		if (!entry->cr_ops->crmatch(acred, entry, flags))
			continue;
		cred = get_rpccred(entry);
		break;
	}
	if (cred == NULL) {
		cred = new;
		set_bit(RPCAUTH_CRED_HASHED, &cred->cr_flags);
		hlist_add_head_rcu(&cred->cr_hash, &cache->hashtable[nr]);
	} else
		list_add_tail(&new->cr_lru, &free);
	spin_unlock(&cache->lock);
found:
	if (test_bit(RPCAUTH_CRED_NEW, &cred->cr_flags)
			&& cred->cr_ops->cr_init != NULL
			&& !(flags & RPCAUTH_LOOKUP_NEW)) {
		int res = cred->cr_ops->cr_init(auth, cred);
		if (res < 0) {
			put_rpccred(cred);
			cred = ERR_PTR(res);
		}
	}
	rpcauth_destroy_credlist(&free);
out:
	return cred;
}
EXPORT_SYMBOL_GPL(rpcauth_lookup_credcache);

struct rpc_cred *
rpcauth_lookupcred(struct rpc_auth *auth, int flags)
{
	struct auth_cred acred = {
		.uid = current->fsuid,
		.gid = current->fsgid,
		.group_info = current->group_info,
	};
	struct rpc_cred *ret;

	dprintk("RPC:       looking up %s cred\n",
		auth->au_ops->au_name);
	get_group_info(acred.group_info);
	ret = auth->au_ops->lookup_cred(auth, &acred, flags);
	put_group_info(acred.group_info);
	return ret;
}

void
rpcauth_init_cred(struct rpc_cred *cred, const struct auth_cred *acred,
		  struct rpc_auth *auth, const struct rpc_credops *ops)
{
	INIT_HLIST_NODE(&cred->cr_hash);
	INIT_LIST_HEAD(&cred->cr_lru);
	atomic_set(&cred->cr_count, 1);
	cred->cr_auth = auth;
	cred->cr_ops = ops;
	cred->cr_expire = jiffies;
#ifdef RPC_DEBUG
	cred->cr_magic = RPCAUTH_CRED_MAGIC;
#endif
	cred->cr_uid = acred->uid;
}
EXPORT_SYMBOL_GPL(rpcauth_init_cred);

void
rpcauth_generic_bind_cred(struct rpc_task *task, struct rpc_cred *cred)
{
	task->tk_msg.rpc_cred = get_rpccred(cred);
	dprintk("RPC: %5u holding %s cred %p\n", task->tk_pid,
			cred->cr_auth->au_ops->au_name, cred);
}
EXPORT_SYMBOL_GPL(rpcauth_generic_bind_cred);

static void
rpcauth_bind_root_cred(struct rpc_task *task)
{
	struct rpc_auth *auth = task->tk_client->cl_auth;
	struct auth_cred acred = {
		.uid = 0,
		.gid = 0,
	};
	struct rpc_cred *ret;

	dprintk("RPC: %5u looking up %s cred\n",
		task->tk_pid, task->tk_client->cl_auth->au_ops->au_name);
	ret = auth->au_ops->lookup_cred(auth, &acred, 0);
	if (!IS_ERR(ret))
		task->tk_msg.rpc_cred = ret;
	else
		task->tk_status = PTR_ERR(ret);
}

static void
rpcauth_bind_new_cred(struct rpc_task *task)
{
	struct rpc_auth *auth = task->tk_client->cl_auth;
	struct rpc_cred *ret;

	dprintk("RPC: %5u looking up %s cred\n",
		task->tk_pid, auth->au_ops->au_name);
	ret = rpcauth_lookupcred(auth, 0);
	if (!IS_ERR(ret))
		task->tk_msg.rpc_cred = ret;
	else
		task->tk_status = PTR_ERR(ret);
}

void
rpcauth_bindcred(struct rpc_task *task, struct rpc_cred *cred, int flags)
{
	if (cred != NULL)
		cred->cr_ops->crbind(task, cred);
	else if (flags & RPC_TASK_ROOTCREDS)
		rpcauth_bind_root_cred(task);
	else
		rpcauth_bind_new_cred(task);
}

void
put_rpccred(struct rpc_cred *cred)
{
	/* Fast path for unhashed credentials */
	if (test_bit(RPCAUTH_CRED_HASHED, &cred->cr_flags) != 0)
		goto need_lock;

	if (!atomic_dec_and_test(&cred->cr_count))
		return;
	goto out_destroy;
need_lock:
	if (!atomic_dec_and_lock(&cred->cr_count, &rpc_credcache_lock))
		return;
	if (!list_empty(&cred->cr_lru)) {
		number_cred_unused--;
		list_del_init(&cred->cr_lru);
	}
	if (test_bit(RPCAUTH_CRED_UPTODATE, &cred->cr_flags) == 0)
		rpcauth_unhash_cred(cred);
	else if (test_bit(RPCAUTH_CRED_HASHED, &cred->cr_flags) != 0) {
		cred->cr_expire = jiffies;
		list_add_tail(&cred->cr_lru, &cred_unused);
		number_cred_unused++;
		spin_unlock(&rpc_credcache_lock);
		return;
	}
	spin_unlock(&rpc_credcache_lock);
out_destroy:
	cred->cr_ops->crdestroy(cred);
}
EXPORT_SYMBOL_GPL(put_rpccred);

void
rpcauth_unbindcred(struct rpc_task *task)
{
	struct rpc_cred	*cred = task->tk_msg.rpc_cred;

	dprintk("RPC: %5u releasing %s cred %p\n",
		task->tk_pid, cred->cr_auth->au_ops->au_name, cred);

	put_rpccred(cred);
	task->tk_msg.rpc_cred = NULL;
}

__be32 *
rpcauth_marshcred(struct rpc_task *task, __be32 *p)
{
	struct rpc_cred	*cred = task->tk_msg.rpc_cred;

	dprintk("RPC: %5u marshaling %s cred %p\n",
		task->tk_pid, cred->cr_auth->au_ops->au_name, cred);

	return cred->cr_ops->crmarshal(task, p);
}

__be32 *
rpcauth_checkverf(struct rpc_task *task, __be32 *p)
{
	struct rpc_cred	*cred = task->tk_msg.rpc_cred;

	dprintk("RPC: %5u validating %s cred %p\n",
		task->tk_pid, cred->cr_auth->au_ops->au_name, cred);

	return cred->cr_ops->crvalidate(task, p);
}

int
rpcauth_wrap_req(struct rpc_task *task, kxdrproc_t encode, void *rqstp,
		__be32 *data, void *obj)
{
	struct rpc_cred *cred = task->tk_msg.rpc_cred;

	dprintk("RPC: %5u using %s cred %p to wrap rpc data\n",
			task->tk_pid, cred->cr_ops->cr_name, cred);
	if (cred->cr_ops->crwrap_req)
		return cred->cr_ops->crwrap_req(task, encode, rqstp, data, obj);
	/* By default, we encode the arguments normally. */
	return rpc_call_xdrproc(encode, rqstp, data, obj);
}

int
rpcauth_unwrap_resp(struct rpc_task *task, kxdrproc_t decode, void *rqstp,
		__be32 *data, void *obj)
{
	struct rpc_cred *cred = task->tk_msg.rpc_cred;

	dprintk("RPC: %5u using %s cred %p to unwrap rpc data\n",
			task->tk_pid, cred->cr_ops->cr_name, cred);
	if (cred->cr_ops->crunwrap_resp)
		return cred->cr_ops->crunwrap_resp(task, decode, rqstp,
						   data, obj);
	/* By default, we decode the arguments normally. */
	return rpc_call_xdrproc(decode, rqstp, data, obj);
}

int
rpcauth_refreshcred(struct rpc_task *task)
{
	struct rpc_cred	*cred = task->tk_msg.rpc_cred;
	int err;

	dprintk("RPC: %5u refreshing %s cred %p\n",
		task->tk_pid, cred->cr_auth->au_ops->au_name, cred);

	err = cred->cr_ops->crrefresh(task);
	if (err < 0)
		task->tk_status = err;
	return err;
}

void
rpcauth_invalcred(struct rpc_task *task)
{
	struct rpc_cred *cred = task->tk_msg.rpc_cred;

	dprintk("RPC: %5u invalidating %s cred %p\n",
		task->tk_pid, cred->cr_auth->au_ops->au_name, cred);
	if (cred)
		clear_bit(RPCAUTH_CRED_UPTODATE, &cred->cr_flags);
}

int
rpcauth_uptodatecred(struct rpc_task *task)
{
	struct rpc_cred *cred = task->tk_msg.rpc_cred;

	return cred == NULL ||
		test_bit(RPCAUTH_CRED_UPTODATE, &cred->cr_flags) != 0;
}

static struct shrinker rpc_cred_shrinker = {
	.shrink = rpcauth_cache_shrinker,
	.seeks = DEFAULT_SEEKS,
};

void __init rpcauth_init_module(void)
{
	rpc_init_authunix();
	rpc_init_generic_auth();
	register_shrinker(&rpc_cred_shrinker);
}

void __exit rpcauth_remove_module(void)
{
	unregister_shrinker(&rpc_cred_shrinker);
}
