/*
 * Generic RPC credential
 *
 * Copyright (C) 2008, Trond Myklebust <Trond.Myklebust@netapp.com>
 */

#include <linux/err.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sunrpc/auth.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/debug.h>
#include <linux/sunrpc/sched.h>

#ifdef RPC_DEBUG
# define RPCDBG_FACILITY	RPCDBG_AUTH
#endif

#define RPC_MACHINE_CRED_USERID		((uid_t)0)
#define RPC_MACHINE_CRED_GROUPID	((gid_t)0)

struct generic_cred {
	struct rpc_cred gc_base;
	struct auth_cred acred;
};

static struct rpc_auth generic_auth;
static struct rpc_cred_cache generic_cred_cache;
static const struct rpc_credops generic_credops;

/*
 * Public call interface
 */
struct rpc_cred *rpc_lookup_cred(void)
{
	return rpcauth_lookupcred(&generic_auth, 0);
}
EXPORT_SYMBOL_GPL(rpc_lookup_cred);

/*
 * Public call interface for looking up machine creds.
 */
struct rpc_cred *rpc_lookup_machine_cred(void)
{
	struct auth_cred acred = {
		.uid = RPC_MACHINE_CRED_USERID,
		.gid = RPC_MACHINE_CRED_GROUPID,
		.machine_cred = 1,
	};

	dprintk("RPC:       looking up machine cred\n");
	return generic_auth.au_ops->lookup_cred(&generic_auth, &acred, 0);
}
EXPORT_SYMBOL_GPL(rpc_lookup_machine_cred);

static void
generic_bind_cred(struct rpc_task *task, struct rpc_cred *cred, int lookupflags)
{
	struct rpc_auth *auth = task->tk_client->cl_auth;
	struct auth_cred *acred = &container_of(cred, struct generic_cred, gc_base)->acred;
	struct rpc_cred *ret;

	ret = auth->au_ops->lookup_cred(auth, acred, lookupflags);
	if (!IS_ERR(ret))
		task->tk_msg.rpc_cred = ret;
	else
		task->tk_status = PTR_ERR(ret);
}

/*
 * Lookup generic creds for current process
 */
static struct rpc_cred *
generic_lookup_cred(struct rpc_auth *auth, struct auth_cred *acred, int flags)
{
	return rpcauth_lookup_credcache(&generic_auth, acred, flags);
}

static struct rpc_cred *
generic_create_cred(struct rpc_auth *auth, struct auth_cred *acred, int flags)
{
	struct generic_cred *gcred;

	gcred = kmalloc(sizeof(*gcred), GFP_KERNEL);
	if (gcred == NULL)
		return ERR_PTR(-ENOMEM);

	rpcauth_init_cred(&gcred->gc_base, acred, &generic_auth, &generic_credops);
	gcred->gc_base.cr_flags = 1UL << RPCAUTH_CRED_UPTODATE;

	gcred->acred.uid = acred->uid;
	gcred->acred.gid = acred->gid;
	gcred->acred.group_info = acred->group_info;
	if (gcred->acred.group_info != NULL)
		get_group_info(gcred->acred.group_info);
	gcred->acred.machine_cred = acred->machine_cred;

	dprintk("RPC:       allocated %s cred %p for uid %d gid %d\n",
			gcred->acred.machine_cred ? "machine" : "generic",
			gcred, acred->uid, acred->gid);
	return &gcred->gc_base;
}

static void
generic_free_cred(struct rpc_cred *cred)
{
	struct generic_cred *gcred = container_of(cred, struct generic_cred, gc_base);

	dprintk("RPC:       generic_free_cred %p\n", gcred);
	if (gcred->acred.group_info != NULL)
		put_group_info(gcred->acred.group_info);
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

/*
 * Match credentials against current process creds.
 */
static int
generic_match(struct auth_cred *acred, struct rpc_cred *cred, int flags)
{
	struct generic_cred *gcred = container_of(cred, struct generic_cred, gc_base);
	int i;

	if (gcred->acred.uid != acred->uid ||
	    gcred->acred.gid != acred->gid ||
	    gcred->acred.machine_cred != acred->machine_cred)
		goto out_nomatch;

	/* Optimisation in the case where pointers are identical... */
	if (gcred->acred.group_info == acred->group_info)
		goto out_match;

	/* Slow path... */
	if (gcred->acred.group_info->ngroups != acred->group_info->ngroups)
		goto out_nomatch;
	for (i = 0; i < gcred->acred.group_info->ngroups; i++) {
		if (GROUP_AT(gcred->acred.group_info, i) !=
				GROUP_AT(acred->group_info, i))
			goto out_nomatch;
	}
out_match:
	return 1;
out_nomatch:
	return 0;
}

void __init rpc_init_generic_auth(void)
{
	spin_lock_init(&generic_cred_cache.lock);
}

void __exit rpc_destroy_generic_auth(void)
{
	rpcauth_clear_credcache(&generic_cred_cache);
}

static struct rpc_cred_cache generic_cred_cache = {
	{{ NULL, },},
};

static const struct rpc_authops generic_auth_ops = {
	.owner = THIS_MODULE,
	.au_name = "Generic",
	.lookup_cred = generic_lookup_cred,
	.crcreate = generic_create_cred,
};

static struct rpc_auth generic_auth = {
	.au_ops = &generic_auth_ops,
	.au_count = ATOMIC_INIT(0),
	.au_credcache = &generic_cred_cache,
};

static const struct rpc_credops generic_credops = {
	.cr_name = "Generic cred",
	.crdestroy = generic_destroy_cred,
	.crbind = generic_bind_cred,
	.crmatch = generic_match,
};
