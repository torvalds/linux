// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/net/sunrpc/auth.c
 *
 * Generic RPC client authentication API.
 *
 * Copyright (C) 1996, Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/hash.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/gss_api.h>
#include <linux/spinlock.h>

#include <trace/events/sunrpc.h>

#define RPC_CREDCACHE_DEFAULT_HASHBITS	(4)
struct rpc_cred_cache {
	struct hlist_head	*hashtable;
	unsigned int		hashbits;
	spinlock_t		lock;
};

static unsigned int auth_hashbits = RPC_CREDCACHE_DEFAULT_HASHBITS;

static const struct rpc_authops __rcu *auth_flavors[RPC_AUTH_MAXFLAVOR] = {
	[RPC_AUTH_NULL] = (const struct rpc_authops __force __rcu *)&authnull_ops,
	[RPC_AUTH_UNIX] = (const struct rpc_authops __force __rcu *)&authunix_ops,
	NULL,			/* others can be loadable modules */
};

static LIST_HEAD(cred_unused);
static unsigned long number_cred_unused;

static struct cred machine_cred = {
	.usage = ATOMIC_INIT(1),
#ifdef CONFIG_DEBUG_CREDENTIALS
	.magic = CRED_MAGIC,
#endif
};

/*
 * Return the machine_cred pointer to be used whenever
 * the a generic machine credential is needed.
 */
const struct cred *rpc_machine_cred(void)
{
	return &machine_cred;
}
EXPORT_SYMBOL_GPL(rpc_machine_cred);

#define MAX_HASHTABLE_BITS (14)
static int param_set_hashtbl_sz(const char *val, const struct kernel_param *kp)
{
	unsigned long num;
	unsigned int nbits;
	int ret;

	if (!val)
		goto out_inval;
	ret = kstrtoul(val, 0, &num);
	if (ret)
		goto out_inval;
	nbits = fls(num - 1);
	if (nbits > MAX_HASHTABLE_BITS || nbits < 2)
		goto out_inval;
	*(unsigned int *)kp->arg = nbits;
	return 0;
out_inval:
	return -EINVAL;
}

static int param_get_hashtbl_sz(char *buffer, const struct kernel_param *kp)
{
	unsigned int nbits;

	nbits = *(unsigned int *)kp->arg;
	return sprintf(buffer, "%u\n", 1U << nbits);
}

#define param_check_hashtbl_sz(name, p) __param_check(name, p, unsigned int);

static const struct kernel_param_ops param_ops_hashtbl_sz = {
	.set = param_set_hashtbl_sz,
	.get = param_get_hashtbl_sz,
};

module_param_named(auth_hashtable_size, auth_hashbits, hashtbl_sz, 0644);
MODULE_PARM_DESC(auth_hashtable_size, "RPC credential cache hashtable size");

static unsigned long auth_max_cred_cachesize = ULONG_MAX;
module_param(auth_max_cred_cachesize, ulong, 0644);
MODULE_PARM_DESC(auth_max_cred_cachesize, "RPC credential maximum total cache size");

static u32
pseudoflavor_to_flavor(u32 flavor) {
	if (flavor > RPC_AUTH_MAXFLAVOR)
		return RPC_AUTH_GSS;
	return flavor;
}

int
rpcauth_register(const struct rpc_authops *ops)
{
	const struct rpc_authops *old;
	rpc_authflavor_t flavor;

	if ((flavor = ops->au_flavor) >= RPC_AUTH_MAXFLAVOR)
		return -EINVAL;
	old = cmpxchg((const struct rpc_authops ** __force)&auth_flavors[flavor], NULL, ops);
	if (old == NULL || old == ops)
		return 0;
	return -EPERM;
}
EXPORT_SYMBOL_GPL(rpcauth_register);

int
rpcauth_unregister(const struct rpc_authops *ops)
{
	const struct rpc_authops *old;
	rpc_authflavor_t flavor;

	if ((flavor = ops->au_flavor) >= RPC_AUTH_MAXFLAVOR)
		return -EINVAL;

	old = cmpxchg((const struct rpc_authops ** __force)&auth_flavors[flavor], ops, NULL);
	if (old == ops || old == NULL)
		return 0;
	return -EPERM;
}
EXPORT_SYMBOL_GPL(rpcauth_unregister);

static const struct rpc_authops *
rpcauth_get_authops(rpc_authflavor_t flavor)
{
	const struct rpc_authops *ops;

	if (flavor >= RPC_AUTH_MAXFLAVOR)
		return NULL;

	rcu_read_lock();
	ops = rcu_dereference(auth_flavors[flavor]);
	if (ops == NULL) {
		rcu_read_unlock();
		request_module("rpc-auth-%u", flavor);
		rcu_read_lock();
		ops = rcu_dereference(auth_flavors[flavor]);
		if (ops == NULL)
			goto out;
	}
	if (!try_module_get(ops->owner))
		ops = NULL;
out:
	rcu_read_unlock();
	return ops;
}

static void
rpcauth_put_authops(const struct rpc_authops *ops)
{
	module_put(ops->owner);
}

/**
 * rpcauth_get_pseudoflavor - check if security flavor is supported
 * @flavor: a security flavor
 * @info: a GSS mech OID, quality of protection, and service value
 *
 * Verifies that an appropriate kernel module is available or already loaded.
 * Returns an equivalent pseudoflavor, or RPC_AUTH_MAXFLAVOR if "flavor" is
 * not supported locally.
 */
rpc_authflavor_t
rpcauth_get_pseudoflavor(rpc_authflavor_t flavor, struct rpcsec_gss_info *info)
{
	const struct rpc_authops *ops = rpcauth_get_authops(flavor);
	rpc_authflavor_t pseudoflavor;

	if (!ops)
		return RPC_AUTH_MAXFLAVOR;
	pseudoflavor = flavor;
	if (ops->info2flavor != NULL)
		pseudoflavor = ops->info2flavor(info);

	rpcauth_put_authops(ops);
	return pseudoflavor;
}
EXPORT_SYMBOL_GPL(rpcauth_get_pseudoflavor);

/**
 * rpcauth_get_gssinfo - find GSS tuple matching a GSS pseudoflavor
 * @pseudoflavor: GSS pseudoflavor to match
 * @info: rpcsec_gss_info structure to fill in
 *
 * Returns zero and fills in "info" if pseudoflavor matches a
 * supported mechanism.
 */
int
rpcauth_get_gssinfo(rpc_authflavor_t pseudoflavor, struct rpcsec_gss_info *info)
{
	rpc_authflavor_t flavor = pseudoflavor_to_flavor(pseudoflavor);
	const struct rpc_authops *ops;
	int result;

	ops = rpcauth_get_authops(flavor);
	if (ops == NULL)
		return -ENOENT;

	result = -ENOENT;
	if (ops->flavor2info != NULL)
		result = ops->flavor2info(pseudoflavor, info);

	rpcauth_put_authops(ops);
	return result;
}
EXPORT_SYMBOL_GPL(rpcauth_get_gssinfo);

struct rpc_auth *
rpcauth_create(const struct rpc_auth_create_args *args, struct rpc_clnt *clnt)
{
	struct rpc_auth	*auth = ERR_PTR(-EINVAL);
	const struct rpc_authops *ops;
	u32 flavor = pseudoflavor_to_flavor(args->pseudoflavor);

	ops = rpcauth_get_authops(flavor);
	if (ops == NULL)
		goto out;

	auth = ops->create(args, clnt);

	rpcauth_put_authops(ops);
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
	if (!refcount_dec_and_test(&auth->au_count))
		return;
	auth->au_ops->destroy(auth);
}

static DEFINE_SPINLOCK(rpc_credcache_lock);

/*
 * On success, the caller is responsible for freeing the reference
 * held by the hashtable
 */
static bool
rpcauth_unhash_cred_locked(struct rpc_cred *cred)
{
	if (!test_and_clear_bit(RPCAUTH_CRED_HASHED, &cred->cr_flags))
		return false;
	hlist_del_rcu(&cred->cr_hash);
	return true;
}

static bool
rpcauth_unhash_cred(struct rpc_cred *cred)
{
	spinlock_t *cache_lock;
	bool ret;

	if (!test_bit(RPCAUTH_CRED_HASHED, &cred->cr_flags))
		return false;
	cache_lock = &cred->cr_auth->au_credcache->lock;
	spin_lock(cache_lock);
	ret = rpcauth_unhash_cred_locked(cred);
	spin_unlock(cache_lock);
	return ret;
}

/*
 * Initialize RPC credential cache
 */
int
rpcauth_init_credcache(struct rpc_auth *auth)
{
	struct rpc_cred_cache *new;
	unsigned int hashsize;

	new = kmalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		goto out_nocache;
	new->hashbits = auth_hashbits;
	hashsize = 1U << new->hashbits;
	new->hashtable = kcalloc(hashsize, sizeof(new->hashtable[0]), GFP_KERNEL);
	if (!new->hashtable)
		goto out_nohashtbl;
	spin_lock_init(&new->lock);
	auth->au_credcache = new;
	return 0;
out_nohashtbl:
	kfree(new);
out_nocache:
	return -ENOMEM;
}
EXPORT_SYMBOL_GPL(rpcauth_init_credcache);

char *
rpcauth_stringify_acceptor(struct rpc_cred *cred)
{
	if (!cred->cr_ops->crstringify_acceptor)
		return NULL;
	return cred->cr_ops->crstringify_acceptor(cred);
}
EXPORT_SYMBOL_GPL(rpcauth_stringify_acceptor);

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

static void
rpcauth_lru_add_locked(struct rpc_cred *cred)
{
	if (!list_empty(&cred->cr_lru))
		return;
	number_cred_unused++;
	list_add_tail(&cred->cr_lru, &cred_unused);
}

static void
rpcauth_lru_add(struct rpc_cred *cred)
{
	if (!list_empty(&cred->cr_lru))
		return;
	spin_lock(&rpc_credcache_lock);
	rpcauth_lru_add_locked(cred);
	spin_unlock(&rpc_credcache_lock);
}

static void
rpcauth_lru_remove_locked(struct rpc_cred *cred)
{
	if (list_empty(&cred->cr_lru))
		return;
	number_cred_unused--;
	list_del_init(&cred->cr_lru);
}

static void
rpcauth_lru_remove(struct rpc_cred *cred)
{
	if (list_empty(&cred->cr_lru))
		return;
	spin_lock(&rpc_credcache_lock);
	rpcauth_lru_remove_locked(cred);
	spin_unlock(&rpc_credcache_lock);
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
	unsigned int hashsize = 1U << cache->hashbits;
	int		i;

	spin_lock(&rpc_credcache_lock);
	spin_lock(&cache->lock);
	for (i = 0; i < hashsize; i++) {
		head = &cache->hashtable[i];
		while (!hlist_empty(head)) {
			cred = hlist_entry(head->first, struct rpc_cred, cr_hash);
			rpcauth_unhash_cred_locked(cred);
			/* Note: We now hold a reference to cred */
			rpcauth_lru_remove_locked(cred);
			list_add_tail(&cred->cr_lru, &free);
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
		kfree(cache->hashtable);
		kfree(cache);
	}
}
EXPORT_SYMBOL_GPL(rpcauth_destroy_credcache);


#define RPC_AUTH_EXPIRY_MORATORIUM (60 * HZ)

/*
 * Remove stale credentials. Avoid sleeping inside the loop.
 */
static long
rpcauth_prune_expired(struct list_head *free, int nr_to_scan)
{
	struct rpc_cred *cred, *next;
	unsigned long expired = jiffies - RPC_AUTH_EXPIRY_MORATORIUM;
	long freed = 0;

	list_for_each_entry_safe(cred, next, &cred_unused, cr_lru) {

		if (nr_to_scan-- == 0)
			break;
		if (refcount_read(&cred->cr_count) > 1) {
			rpcauth_lru_remove_locked(cred);
			continue;
		}
		/*
		 * Enforce a 60 second garbage collection moratorium
		 * Note that the cred_unused list must be time-ordered.
		 */
		if (time_in_range(cred->cr_expire, expired, jiffies))
			continue;
		if (!rpcauth_unhash_cred(cred))
			continue;

		rpcauth_lru_remove_locked(cred);
		freed++;
		list_add_tail(&cred->cr_lru, free);
	}
	return freed ? freed : SHRINK_STOP;
}

static unsigned long
rpcauth_cache_do_shrink(int nr_to_scan)
{
	LIST_HEAD(free);
	unsigned long freed;

	spin_lock(&rpc_credcache_lock);
	freed = rpcauth_prune_expired(&free, nr_to_scan);
	spin_unlock(&rpc_credcache_lock);
	rpcauth_destroy_credlist(&free);

	return freed;
}

/*
 * Run memory cache shrinker.
 */
static unsigned long
rpcauth_cache_shrink_scan(struct shrinker *shrink, struct shrink_control *sc)

{
	if ((sc->gfp_mask & GFP_KERNEL) != GFP_KERNEL)
		return SHRINK_STOP;

	/* nothing left, don't come back */
	if (list_empty(&cred_unused))
		return SHRINK_STOP;

	return rpcauth_cache_do_shrink(sc->nr_to_scan);
}

static unsigned long
rpcauth_cache_shrink_count(struct shrinker *shrink, struct shrink_control *sc)

{
	return number_cred_unused * sysctl_vfs_cache_pressure / 100;
}

static void
rpcauth_cache_enforce_limit(void)
{
	unsigned long diff;
	unsigned int nr_to_scan;

	if (number_cred_unused <= auth_max_cred_cachesize)
		return;
	diff = number_cred_unused - auth_max_cred_cachesize;
	nr_to_scan = 100;
	if (diff < nr_to_scan)
		nr_to_scan = diff;
	rpcauth_cache_do_shrink(nr_to_scan);
}

/*
 * Look up a process' credentials in the authentication cache
 */
struct rpc_cred *
rpcauth_lookup_credcache(struct rpc_auth *auth, struct auth_cred * acred,
		int flags, gfp_t gfp)
{
	LIST_HEAD(free);
	struct rpc_cred_cache *cache = auth->au_credcache;
	struct rpc_cred	*cred = NULL,
			*entry, *new;
	unsigned int nr;

	nr = auth->au_ops->hash_cred(acred, cache->hashbits);

	rcu_read_lock();
	hlist_for_each_entry_rcu(entry, &cache->hashtable[nr], cr_hash) {
		if (!entry->cr_ops->crmatch(acred, entry, flags))
			continue;
		cred = get_rpccred(entry);
		if (cred)
			break;
	}
	rcu_read_unlock();

	if (cred != NULL)
		goto found;

	new = auth->au_ops->crcreate(auth, acred, flags, gfp);
	if (IS_ERR(new)) {
		cred = new;
		goto out;
	}

	spin_lock(&cache->lock);
	hlist_for_each_entry(entry, &cache->hashtable[nr], cr_hash) {
		if (!entry->cr_ops->crmatch(acred, entry, flags))
			continue;
		cred = get_rpccred(entry);
		if (cred)
			break;
	}
	if (cred == NULL) {
		cred = new;
		set_bit(RPCAUTH_CRED_HASHED, &cred->cr_flags);
		refcount_inc(&cred->cr_count);
		hlist_add_head_rcu(&cred->cr_hash, &cache->hashtable[nr]);
	} else
		list_add_tail(&new->cr_lru, &free);
	spin_unlock(&cache->lock);
	rpcauth_cache_enforce_limit();
found:
	if (test_bit(RPCAUTH_CRED_NEW, &cred->cr_flags) &&
	    cred->cr_ops->cr_init != NULL &&
	    !(flags & RPCAUTH_LOOKUP_NEW)) {
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
	struct auth_cred acred;
	struct rpc_cred *ret;
	const struct cred *cred = current_cred();

	memset(&acred, 0, sizeof(acred));
	acred.cred = cred;
	ret = auth->au_ops->lookup_cred(auth, &acred, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(rpcauth_lookupcred);

void
rpcauth_init_cred(struct rpc_cred *cred, const struct auth_cred *acred,
		  struct rpc_auth *auth, const struct rpc_credops *ops)
{
	INIT_HLIST_NODE(&cred->cr_hash);
	INIT_LIST_HEAD(&cred->cr_lru);
	refcount_set(&cred->cr_count, 1);
	cred->cr_auth = auth;
	cred->cr_flags = 0;
	cred->cr_ops = ops;
	cred->cr_expire = jiffies;
	cred->cr_cred = get_cred(acred->cred);
}
EXPORT_SYMBOL_GPL(rpcauth_init_cred);

static struct rpc_cred *
rpcauth_bind_root_cred(struct rpc_task *task, int lookupflags)
{
	struct rpc_auth *auth = task->tk_client->cl_auth;
	struct auth_cred acred = {
		.cred = get_task_cred(&init_task),
	};
	struct rpc_cred *ret;

	ret = auth->au_ops->lookup_cred(auth, &acred, lookupflags);
	put_cred(acred.cred);
	return ret;
}

static struct rpc_cred *
rpcauth_bind_machine_cred(struct rpc_task *task, int lookupflags)
{
	struct rpc_auth *auth = task->tk_client->cl_auth;
	struct auth_cred acred = {
		.principal = task->tk_client->cl_principal,
		.cred = init_task.cred,
	};

	if (!acred.principal)
		return NULL;
	return auth->au_ops->lookup_cred(auth, &acred, lookupflags);
}

static struct rpc_cred *
rpcauth_bind_new_cred(struct rpc_task *task, int lookupflags)
{
	struct rpc_auth *auth = task->tk_client->cl_auth;

	return rpcauth_lookupcred(auth, lookupflags);
}

static int
rpcauth_bindcred(struct rpc_task *task, const struct cred *cred, int flags)
{
	struct rpc_rqst *req = task->tk_rqstp;
	struct rpc_cred *new = NULL;
	int lookupflags = 0;
	struct rpc_auth *auth = task->tk_client->cl_auth;
	struct auth_cred acred = {
		.cred = cred,
	};

	if (flags & RPC_TASK_ASYNC)
		lookupflags |= RPCAUTH_LOOKUP_NEW;
	if (task->tk_op_cred)
		/* Task must use exactly this rpc_cred */
		new = get_rpccred(task->tk_op_cred);
	else if (cred != NULL && cred != &machine_cred)
		new = auth->au_ops->lookup_cred(auth, &acred, lookupflags);
	else if (cred == &machine_cred)
		new = rpcauth_bind_machine_cred(task, lookupflags);

	/* If machine cred couldn't be bound, try a root cred */
	if (new)
		;
	else if (cred == &machine_cred || (flags & RPC_TASK_ROOTCREDS))
		new = rpcauth_bind_root_cred(task, lookupflags);
	else if (flags & RPC_TASK_NULLCREDS)
		new = authnull_ops.lookup_cred(NULL, NULL, 0);
	else
		new = rpcauth_bind_new_cred(task, lookupflags);
	if (IS_ERR(new))
		return PTR_ERR(new);
	put_rpccred(req->rq_cred);
	req->rq_cred = new;
	return 0;
}

void
put_rpccred(struct rpc_cred *cred)
{
	if (cred == NULL)
		return;
	rcu_read_lock();
	if (refcount_dec_and_test(&cred->cr_count))
		goto destroy;
	if (refcount_read(&cred->cr_count) != 1 ||
	    !test_bit(RPCAUTH_CRED_HASHED, &cred->cr_flags))
		goto out;
	if (test_bit(RPCAUTH_CRED_UPTODATE, &cred->cr_flags) != 0) {
		cred->cr_expire = jiffies;
		rpcauth_lru_add(cred);
		/* Race breaker */
		if (unlikely(!test_bit(RPCAUTH_CRED_HASHED, &cred->cr_flags)))
			rpcauth_lru_remove(cred);
	} else if (rpcauth_unhash_cred(cred)) {
		rpcauth_lru_remove(cred);
		if (refcount_dec_and_test(&cred->cr_count))
			goto destroy;
	}
out:
	rcu_read_unlock();
	return;
destroy:
	rcu_read_unlock();
	cred->cr_ops->crdestroy(cred);
}
EXPORT_SYMBOL_GPL(put_rpccred);

/**
 * rpcauth_marshcred - Append RPC credential to end of @xdr
 * @task: controlling RPC task
 * @xdr: xdr_stream containing initial portion of RPC Call header
 *
 * On success, an appropriate verifier is added to @xdr, @xdr is
 * updated to point past the verifier, and zero is returned.
 * Otherwise, @xdr is in an undefined state and a negative errno
 * is returned.
 */
int rpcauth_marshcred(struct rpc_task *task, struct xdr_stream *xdr)
{
	const struct rpc_credops *ops = task->tk_rqstp->rq_cred->cr_ops;

	return ops->crmarshal(task, xdr);
}

/**
 * rpcauth_wrap_req_encode - XDR encode the RPC procedure
 * @task: controlling RPC task
 * @xdr: stream where on-the-wire bytes are to be marshalled
 *
 * On success, @xdr contains the encoded and wrapped message.
 * Otherwise, @xdr is in an undefined state.
 */
int rpcauth_wrap_req_encode(struct rpc_task *task, struct xdr_stream *xdr)
{
	kxdreproc_t encode = task->tk_msg.rpc_proc->p_encode;

	encode(task->tk_rqstp, xdr, task->tk_msg.rpc_argp);
	return 0;
}
EXPORT_SYMBOL_GPL(rpcauth_wrap_req_encode);

/**
 * rpcauth_wrap_req - XDR encode and wrap the RPC procedure
 * @task: controlling RPC task
 * @xdr: stream where on-the-wire bytes are to be marshalled
 *
 * On success, @xdr contains the encoded and wrapped message,
 * and zero is returned. Otherwise, @xdr is in an undefined
 * state and a negative errno is returned.
 */
int rpcauth_wrap_req(struct rpc_task *task, struct xdr_stream *xdr)
{
	const struct rpc_credops *ops = task->tk_rqstp->rq_cred->cr_ops;

	return ops->crwrap_req(task, xdr);
}

/**
 * rpcauth_checkverf - Validate verifier in RPC Reply header
 * @task: controlling RPC task
 * @xdr: xdr_stream containing RPC Reply header
 *
 * On success, @xdr is updated to point past the verifier and
 * zero is returned. Otherwise, @xdr is in an undefined state
 * and a negative errno is returned.
 */
int
rpcauth_checkverf(struct rpc_task *task, struct xdr_stream *xdr)
{
	const struct rpc_credops *ops = task->tk_rqstp->rq_cred->cr_ops;

	return ops->crvalidate(task, xdr);
}

/**
 * rpcauth_unwrap_resp_decode - Invoke XDR decode function
 * @task: controlling RPC task
 * @xdr: stream where the Reply message resides
 *
 * Returns zero on success; otherwise a negative errno is returned.
 */
int
rpcauth_unwrap_resp_decode(struct rpc_task *task, struct xdr_stream *xdr)
{
	kxdrdproc_t decode = task->tk_msg.rpc_proc->p_decode;

	return decode(task->tk_rqstp, xdr, task->tk_msg.rpc_resp);
}
EXPORT_SYMBOL_GPL(rpcauth_unwrap_resp_decode);

/**
 * rpcauth_unwrap_resp - Invoke unwrap and decode function for the cred
 * @task: controlling RPC task
 * @xdr: stream where the Reply message resides
 *
 * Returns zero on success; otherwise a negative errno is returned.
 */
int
rpcauth_unwrap_resp(struct rpc_task *task, struct xdr_stream *xdr)
{
	const struct rpc_credops *ops = task->tk_rqstp->rq_cred->cr_ops;

	return ops->crunwrap_resp(task, xdr);
}

bool
rpcauth_xmit_need_reencode(struct rpc_task *task)
{
	struct rpc_cred *cred = task->tk_rqstp->rq_cred;

	if (!cred || !cred->cr_ops->crneed_reencode)
		return false;
	return cred->cr_ops->crneed_reencode(task);
}

int
rpcauth_refreshcred(struct rpc_task *task)
{
	struct rpc_cred	*cred;
	int err;

	cred = task->tk_rqstp->rq_cred;
	if (cred == NULL) {
		err = rpcauth_bindcred(task, task->tk_msg.rpc_cred, task->tk_flags);
		if (err < 0)
			goto out;
		cred = task->tk_rqstp->rq_cred;
	}

	err = cred->cr_ops->crrefresh(task);
out:
	if (err < 0)
		task->tk_status = err;
	return err;
}

void
rpcauth_invalcred(struct rpc_task *task)
{
	struct rpc_cred *cred = task->tk_rqstp->rq_cred;

	if (cred)
		clear_bit(RPCAUTH_CRED_UPTODATE, &cred->cr_flags);
}

int
rpcauth_uptodatecred(struct rpc_task *task)
{
	struct rpc_cred *cred = task->tk_rqstp->rq_cred;

	return cred == NULL ||
		test_bit(RPCAUTH_CRED_UPTODATE, &cred->cr_flags) != 0;
}

static struct shrinker rpc_cred_shrinker = {
	.count_objects = rpcauth_cache_shrink_count,
	.scan_objects = rpcauth_cache_shrink_scan,
	.seeks = DEFAULT_SEEKS,
};

int __init rpcauth_init_module(void)
{
	int err;

	err = rpc_init_authunix();
	if (err < 0)
		goto out1;
	err = register_shrinker(&rpc_cred_shrinker);
	if (err < 0)
		goto out2;
	return 0;
out2:
	rpc_destroy_authunix();
out1:
	return err;
}

void rpcauth_remove_module(void)
{
	rpc_destroy_authunix();
	unregister_shrinker(&rpc_cred_shrinker);
}
