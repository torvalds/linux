/*
 * Generic RPC credential
 *
 * Copyright (C) 2008, Trond Myklebust <Trond.Myklebust@netapp.com>
 */

#include <linux/err.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sunrpc/auth.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/debug.h>
#include <linux/sunrpc/sched.h>

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
# define RPCDBG_FACILITY	RPCDBG_AUTH
#endif

struct generic_cred {
	struct rpc_cred gc_base;
	struct auth_cred acred;
};

static struct rpc_auth generic_auth;
static const struct rpc_credops generic_credops;

/*
 * Public call interface
 */
struct rpc_cred *rpc_lookup_cred(void)
{
	return rpcauth_lookupcred(&generic_auth, 0);
}
EXPORT_SYMBOL_GPL(rpc_lookup_cred);

struct rpc_cred *
rpc_lookup_generic_cred(struct auth_cred *acred, int flags, gfp_t gfp)
{
	return rpcauth_lookup_credcache(&generic_auth, acred, flags, gfp);
}
EXPORT_SYMBOL_GPL(rpc_lookup_generic_cred);

struct rpc_cred *rpc_lookup_cred_nonblock(void)
{
	return rpcauth_lookupcred(&generic_auth, RPCAUTH_LOOKUP_RCU);
}
EXPORT_SYMBOL_GPL(rpc_lookup_cred_nonblock);

static struct rpc_cred *generic_bind_cred(struct rpc_task *task,
		struct rpc_cred *cred, int lookupflags)
{
	struct rpc_auth *auth = task->tk_client->cl_auth;
	struct auth_cred *acred = &container_of(cred, struct generic_cred, gc_base)->acred;

	return auth->au_ops->lookup_cred(auth, acred, lookupflags);
}

static int
generic_hash_cred(struct auth_cred *acred, unsigned int hashbits)
{
	return hash_64(from_kgid(&init_user_ns, acred->cred->fsgid) |
		((u64)from_kuid(&init_user_ns, acred->cred->fsuid) <<
			(sizeof(gid_t) * 8)), hashbits);
}

/*
 * Lookup generic creds for current process
 */
static struct rpc_cred *
generic_lookup_cred(struct rpc_auth *auth, struct auth_cred *acred, int flags)
{
	return rpcauth_lookup_credcache(&generic_auth, acred, flags, GFP_KERNEL);
}

static struct rpc_cred *
generic_create_cred(struct rpc_auth *auth, struct auth_cred *acred, int flags, gfp_t gfp)
{
	struct generic_cred *gcred;

	gcred = kmalloc(sizeof(*gcred), gfp);
	if (gcred == NULL)
		return ERR_PTR(-ENOMEM);

	rpcauth_init_cred(&gcred->gc_base, acred, &generic_auth, &generic_credops);
	gcred->gc_base.cr_flags = 1UL << RPCAUTH_CRED_UPTODATE;

	gcred->acred.cred = gcred->gc_base.cr_cred;
	gcred->acred.principal = acred->principal;

	dprintk("RPC:       allocated %s cred %p for uid %d gid %d\n",
			gcred->acred.principal ? "machine" : "generic",
			gcred,
			from_kuid(&init_user_ns, acred->cred->fsuid),
			from_kgid(&init_user_ns, acred->cred->fsgid));
	return &gcred->gc_base;
}

static void
generic_free_cred(struct rpc_cred *cred)
{
	struct generic_cred *gcred = container_of(cred, struct generic_cred, gc_base);

	dprintk("RPC:       generic_free_cred %p\n", gcred);
	put_cred(cred->cr_cred);
	kfree(gcred);
}

static void
generic_free_cred_callback(struct rcu_head *head)
{
	struct rpc_cred *cred = container_of(head, struct rpc_cred, cr_rcu);
	generic_free_cred(cred);
}

static void
generic_destroy_cred(struct rpc_cred *cred)
{
	call_rcu(&cred->cr_rcu, generic_free_cred_callback);
}

static int
machine_cred_match(struct auth_cred *acred, struct generic_cred *gcred, int flags)
{
	if (!gcred->acred.principal ||
	    gcred->acred.principal != acred->principal ||
	    !uid_eq(gcred->acred.cred->fsuid, acred->cred->fsuid) ||
	    !gid_eq(gcred->acred.cred->fsgid, acred->cred->fsgid))
		return 0;
	return 1;
}

/*
 * Match credentials against current process creds.
 */
static int
generic_match(struct auth_cred *acred, struct rpc_cred *cred, int flags)
{
	struct generic_cred *gcred = container_of(cred, struct generic_cred, gc_base);
	int i;
	struct group_info *a, *g;

	if (acred->principal)
		return machine_cred_match(acred, gcred, flags);

	if (!uid_eq(gcred->acred.cred->fsuid, acred->cred->fsuid) ||
	    !gid_eq(gcred->acred.cred->fsgid, acred->cred->fsgid) ||
	    gcred->acred.principal != NULL)
		goto out_nomatch;

	a = acred->cred->group_info;
	g = gcred->acred.cred->group_info;
	/* Optimisation in the case where pointers are identical... */
	if (a == g)
		goto out_match;

	/* Slow path... */
	if (g->ngroups != a->ngroups)
		goto out_nomatch;
	for (i = 0; i < g->ngroups; i++) {
		if (!gid_eq(g->gid[i], a->gid[i]))
			goto out_nomatch;
	}
out_match:
	return 1;
out_nomatch:
	return 0;
}

int __init rpc_init_generic_auth(void)
{
	return rpcauth_init_credcache(&generic_auth);
}

void rpc_destroy_generic_auth(void)
{
	rpcauth_destroy_credcache(&generic_auth);
}

static const struct rpc_authops generic_auth_ops = {
	.owner = THIS_MODULE,
	.au_name = "Generic",
	.hash_cred = generic_hash_cred,
	.lookup_cred = generic_lookup_cred,
	.crcreate = generic_create_cred,
};

static struct rpc_auth generic_auth = {
	.au_ops = &generic_auth_ops,
	.au_count = REFCOUNT_INIT(1),
};

static const struct rpc_credops generic_credops = {
	.cr_name = "Generic cred",
	.crdestroy = generic_destroy_cred,
	.crbind = generic_bind_cred,
	.crmatch = generic_match,
};
