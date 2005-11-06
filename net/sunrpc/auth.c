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
#include <linux/sunrpc/clnt.h>
#include <linux/spinlock.h>

#ifdef RPC_DEBUG
# define RPCDBG_FACILITY	RPCDBG_AUTH
#endif

static struct rpc_authops *	auth_flavors[RPC_AUTH_MAXFLAVOR] = {
	&authnull_ops,		/* AUTH_NULL */
	&authunix_ops,		/* AUTH_UNIX */
	NULL,			/* others can be loadable modules */
};

static u32
pseudoflavor_to_flavor(u32 flavor) {
	if (flavor >= RPC_AUTH_MAXFLAVOR)
		return RPC_AUTH_GSS;
	return flavor;
}

int
rpcauth_register(struct rpc_authops *ops)
{
	rpc_authflavor_t flavor;

	if ((flavor = ops->au_flavor) >= RPC_AUTH_MAXFLAVOR)
		return -EINVAL;
	if (auth_flavors[flavor] != NULL)
		return -EPERM;		/* what else? */
	auth_flavors[flavor] = ops;
	return 0;
}

int
rpcauth_unregister(struct rpc_authops *ops)
{
	rpc_authflavor_t flavor;

	if ((flavor = ops->au_flavor) >= RPC_AUTH_MAXFLAVOR)
		return -EINVAL;
	if (auth_flavors[flavor] != ops)
		return -EPERM;		/* what else? */
	auth_flavors[flavor] = NULL;
	return 0;
}

struct rpc_auth *
rpcauth_create(rpc_authflavor_t pseudoflavor, struct rpc_clnt *clnt)
{
	struct rpc_auth		*auth;
	struct rpc_authops	*ops;
	u32			flavor = pseudoflavor_to_flavor(pseudoflavor);

	if (flavor >= RPC_AUTH_MAXFLAVOR || !(ops = auth_flavors[flavor]))
		return ERR_PTR(-EINVAL);
	auth = ops->create(clnt, pseudoflavor);
	if (IS_ERR(auth))
		return auth;
	if (clnt->cl_auth)
		rpcauth_destroy(clnt->cl_auth);
	clnt->cl_auth = auth;
	return auth;
}

void
rpcauth_destroy(struct rpc_auth *auth)
{
	if (!atomic_dec_and_test(&auth->au_count))
		return;
	auth->au_ops->destroy(auth);
}

static DEFINE_SPINLOCK(rpc_credcache_lock);

/*
 * Initialize RPC credential cache
 */
int
rpcauth_init_credcache(struct rpc_auth *auth, unsigned long expire)
{
	struct rpc_cred_cache *new;
	int i;

	new = (struct rpc_cred_cache *)kmalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		return -ENOMEM;
	for (i = 0; i < RPC_CREDCACHE_NR; i++)
		INIT_HLIST_HEAD(&new->hashtable[i]);
	new->expire = expire;
	new->nextgc = jiffies + (expire >> 1);
	auth->au_credcache = new;
	return 0;
}

/*
 * Destroy a list of credentials
 */
static inline
void rpcauth_destroy_credlist(struct hlist_head *head)
{
	struct rpc_cred *cred;

	while (!hlist_empty(head)) {
		cred = hlist_entry(head->first, struct rpc_cred, cr_hash);
		hlist_del_init(&cred->cr_hash);
		put_rpccred(cred);
	}
}

/*
 * Clear the RPC credential cache, and delete those credentials
 * that are not referenced.
 */
void
rpcauth_free_credcache(struct rpc_auth *auth)
{
	struct rpc_cred_cache *cache = auth->au_credcache;
	HLIST_HEAD(free);
	struct hlist_node *pos, *next;
	struct rpc_cred	*cred;
	int		i;

	spin_lock(&rpc_credcache_lock);
	for (i = 0; i < RPC_CREDCACHE_NR; i++) {
		hlist_for_each_safe(pos, next, &cache->hashtable[i]) {
			cred = hlist_entry(pos, struct rpc_cred, cr_hash);
			__hlist_del(&cred->cr_hash);
			hlist_add_head(&cred->cr_hash, &free);
		}
	}
	spin_unlock(&rpc_credcache_lock);
	rpcauth_destroy_credlist(&free);
}

static void
rpcauth_prune_expired(struct rpc_auth *auth, struct rpc_cred *cred, struct hlist_head *free)
{
	if (atomic_read(&cred->cr_count) != 1)
	       return;
	if (time_after(jiffies, cred->cr_expire + auth->au_credcache->expire))
		cred->cr_flags &= ~RPCAUTH_CRED_UPTODATE;
	if (!(cred->cr_flags & RPCAUTH_CRED_UPTODATE)) {
		__hlist_del(&cred->cr_hash);
		hlist_add_head(&cred->cr_hash, free);
	}
}

/*
 * Remove stale credentials. Avoid sleeping inside the loop.
 */
static void
rpcauth_gc_credcache(struct rpc_auth *auth, struct hlist_head *free)
{
	struct rpc_cred_cache *cache = auth->au_credcache;
	struct hlist_node *pos, *next;
	struct rpc_cred	*cred;
	int		i;

	dprintk("RPC: gc'ing RPC credentials for auth %p\n", auth);
	for (i = 0; i < RPC_CREDCACHE_NR; i++) {
		hlist_for_each_safe(pos, next, &cache->hashtable[i]) {
			cred = hlist_entry(pos, struct rpc_cred, cr_hash);
			rpcauth_prune_expired(auth, cred, free);
		}
	}
	cache->nextgc = jiffies + cache->expire;
}

/*
 * Look up a process' credentials in the authentication cache
 */
struct rpc_cred *
rpcauth_lookup_credcache(struct rpc_auth *auth, struct auth_cred * acred,
		int taskflags)
{
	struct rpc_cred_cache *cache = auth->au_credcache;
	HLIST_HEAD(free);
	struct hlist_node *pos, *next;
	struct rpc_cred	*new = NULL,
			*cred = NULL;
	int		nr = 0;

	if (!(taskflags & RPC_TASK_ROOTCREDS))
		nr = acred->uid & RPC_CREDCACHE_MASK;
retry:
	spin_lock(&rpc_credcache_lock);
	if (time_before(cache->nextgc, jiffies))
		rpcauth_gc_credcache(auth, &free);
	hlist_for_each_safe(pos, next, &cache->hashtable[nr]) {
		struct rpc_cred *entry;
	       	entry = hlist_entry(pos, struct rpc_cred, cr_hash);
		if (entry->cr_ops->crmatch(acred, entry, taskflags)) {
			hlist_del(&entry->cr_hash);
			cred = entry;
			break;
		}
		rpcauth_prune_expired(auth, entry, &free);
	}
	if (new) {
		if (cred)
			hlist_add_head(&new->cr_hash, &free);
		else
			cred = new;
	}
	if (cred) {
		hlist_add_head(&cred->cr_hash, &cache->hashtable[nr]);
		get_rpccred(cred);
	}
	spin_unlock(&rpc_credcache_lock);

	rpcauth_destroy_credlist(&free);

	if (!cred) {
		new = auth->au_ops->crcreate(auth, acred, taskflags);
		if (!IS_ERR(new)) {
#ifdef RPC_DEBUG
			new->cr_magic = RPCAUTH_CRED_MAGIC;
#endif
			goto retry;
		} else
			cred = new;
	}

	return (struct rpc_cred *) cred;
}

struct rpc_cred *
rpcauth_lookupcred(struct rpc_auth *auth, int taskflags)
{
	struct auth_cred acred = {
		.uid = current->fsuid,
		.gid = current->fsgid,
		.group_info = current->group_info,
	};
	struct rpc_cred *ret;

	dprintk("RPC:     looking up %s cred\n",
		auth->au_ops->au_name);
	get_group_info(acred.group_info);
	ret = auth->au_ops->lookup_cred(auth, &acred, taskflags);
	put_group_info(acred.group_info);
	return ret;
}

struct rpc_cred *
rpcauth_bindcred(struct rpc_task *task)
{
	struct rpc_auth *auth = task->tk_auth;
	struct auth_cred acred = {
		.uid = current->fsuid,
		.gid = current->fsgid,
		.group_info = current->group_info,
	};
	struct rpc_cred *ret;

	dprintk("RPC: %4d looking up %s cred\n",
		task->tk_pid, task->tk_auth->au_ops->au_name);
	get_group_info(acred.group_info);
	ret = auth->au_ops->lookup_cred(auth, &acred, task->tk_flags);
	if (!IS_ERR(ret))
		task->tk_msg.rpc_cred = ret;
	else
		task->tk_status = PTR_ERR(ret);
	put_group_info(acred.group_info);
	return ret;
}

void
rpcauth_holdcred(struct rpc_task *task)
{
	dprintk("RPC: %4d holding %s cred %p\n",
		task->tk_pid, task->tk_auth->au_ops->au_name, task->tk_msg.rpc_cred);
	if (task->tk_msg.rpc_cred)
		get_rpccred(task->tk_msg.rpc_cred);
}

void
put_rpccred(struct rpc_cred *cred)
{
	cred->cr_expire = jiffies;
	if (!atomic_dec_and_test(&cred->cr_count))
		return;
	cred->cr_ops->crdestroy(cred);
}

void
rpcauth_unbindcred(struct rpc_task *task)
{
	struct rpc_cred	*cred = task->tk_msg.rpc_cred;

	dprintk("RPC: %4d releasing %s cred %p\n",
		task->tk_pid, task->tk_auth->au_ops->au_name, cred);

	put_rpccred(cred);
	task->tk_msg.rpc_cred = NULL;
}

u32 *
rpcauth_marshcred(struct rpc_task *task, u32 *p)
{
	struct rpc_cred	*cred = task->tk_msg.rpc_cred;

	dprintk("RPC: %4d marshaling %s cred %p\n",
		task->tk_pid, task->tk_auth->au_ops->au_name, cred);

	return cred->cr_ops->crmarshal(task, p);
}

u32 *
rpcauth_checkverf(struct rpc_task *task, u32 *p)
{
	struct rpc_cred	*cred = task->tk_msg.rpc_cred;

	dprintk("RPC: %4d validating %s cred %p\n",
		task->tk_pid, task->tk_auth->au_ops->au_name, cred);

	return cred->cr_ops->crvalidate(task, p);
}

int
rpcauth_wrap_req(struct rpc_task *task, kxdrproc_t encode, void *rqstp,
		u32 *data, void *obj)
{
	struct rpc_cred *cred = task->tk_msg.rpc_cred;

	dprintk("RPC: %4d using %s cred %p to wrap rpc data\n",
			task->tk_pid, cred->cr_ops->cr_name, cred);
	if (cred->cr_ops->crwrap_req)
		return cred->cr_ops->crwrap_req(task, encode, rqstp, data, obj);
	/* By default, we encode the arguments normally. */
	return encode(rqstp, data, obj);
}

int
rpcauth_unwrap_resp(struct rpc_task *task, kxdrproc_t decode, void *rqstp,
		u32 *data, void *obj)
{
	struct rpc_cred *cred = task->tk_msg.rpc_cred;

	dprintk("RPC: %4d using %s cred %p to unwrap rpc data\n",
			task->tk_pid, cred->cr_ops->cr_name, cred);
	if (cred->cr_ops->crunwrap_resp)
		return cred->cr_ops->crunwrap_resp(task, decode, rqstp,
						   data, obj);
	/* By default, we decode the arguments normally. */
	return decode(rqstp, data, obj);
}

int
rpcauth_refreshcred(struct rpc_task *task)
{
	struct rpc_cred	*cred = task->tk_msg.rpc_cred;
	int err;

	dprintk("RPC: %4d refreshing %s cred %p\n",
		task->tk_pid, task->tk_auth->au_ops->au_name, cred);

	err = cred->cr_ops->crrefresh(task);
	if (err < 0)
		task->tk_status = err;
	return err;
}

void
rpcauth_invalcred(struct rpc_task *task)
{
	dprintk("RPC: %4d invalidating %s cred %p\n",
		task->tk_pid, task->tk_auth->au_ops->au_name, task->tk_msg.rpc_cred);
	spin_lock(&rpc_credcache_lock);
	if (task->tk_msg.rpc_cred)
		task->tk_msg.rpc_cred->cr_flags &= ~RPCAUTH_CRED_UPTODATE;
	spin_unlock(&rpc_credcache_lock);
}

int
rpcauth_uptodatecred(struct rpc_task *task)
{
	return !(task->tk_msg.rpc_cred) ||
		(task->tk_msg.rpc_cred->cr_flags & RPCAUTH_CRED_UPTODATE);
}
