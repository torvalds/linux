// SPDX-License-Identifier: GPL-2.0
/*
 * linux/net/sunrpc/auth_unix.c
 *
 * UNIX-style authentication; no AUTH_SHORT support
 *
 * Copyright (C) 1996, Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/slab.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/mempool.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/auth.h>
#include <linux/user_namespace.h>


#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
# define RPCDBG_FACILITY	RPCDBG_AUTH
#endif

static struct rpc_auth		unix_auth;
static const struct rpc_credops	unix_credops;
static mempool_t		*unix_pool;

static struct rpc_auth *
unx_create(const struct rpc_auth_create_args *args, struct rpc_clnt *clnt)
{
	dprintk("RPC:       creating UNIX authenticator for client %p\n",
			clnt);
	refcount_inc(&unix_auth.au_count);
	return &unix_auth;
}

static void
unx_destroy(struct rpc_auth *auth)
{
	dprintk("RPC:       destroying UNIX authenticator %p\n", auth);
}

/*
 * Lookup AUTH_UNIX creds for current process
 */
static struct rpc_cred *
unx_lookup_cred(struct rpc_auth *auth, struct auth_cred *acred, int flags)
{
	struct rpc_cred *ret = mempool_alloc(unix_pool, GFP_NOFS);

	dprintk("RPC:       allocating UNIX cred for uid %d gid %d\n",
			from_kuid(&init_user_ns, acred->cred->fsuid),
			from_kgid(&init_user_ns, acred->cred->fsgid));

	rpcauth_init_cred(ret, acred, auth, &unix_credops);
	ret->cr_flags = 1UL << RPCAUTH_CRED_UPTODATE;
	return ret;
}

static void
unx_free_cred_callback(struct rcu_head *head)
{
	struct rpc_cred *rpc_cred = container_of(head, struct rpc_cred, cr_rcu);
	dprintk("RPC:       unx_free_cred %p\n", rpc_cred);
	put_cred(rpc_cred->cr_cred);
	mempool_free(rpc_cred, unix_pool);
}

static void
unx_destroy_cred(struct rpc_cred *cred)
{
	call_rcu(&cred->cr_rcu, unx_free_cred_callback);
}

/*
 * Match credentials against current the auth_cred.
 */
static int
unx_match(struct auth_cred *acred, struct rpc_cred *cred, int flags)
{
	unsigned int groups = 0;
	unsigned int i;

	if (cred->cr_cred == acred->cred)
		return 1;

	if (!uid_eq(cred->cr_cred->fsuid, acred->cred->fsuid) || !gid_eq(cred->cr_cred->fsgid, acred->cred->fsgid))
		return 0;

	if (acred->cred && acred->cred->group_info != NULL)
		groups = acred->cred->group_info->ngroups;
	if (groups > UNX_NGROUPS)
		groups = UNX_NGROUPS;
	if (cred->cr_cred->group_info == NULL)
		return groups == 0;
	if (groups != cred->cr_cred->group_info->ngroups)
		return 0;

	for (i = 0; i < groups ; i++)
		if (!gid_eq(cred->cr_cred->group_info->gid[i], acred->cred->group_info->gid[i]))
			return 0;
	return 1;
}

/*
 * Marshal credentials.
 * Maybe we should keep a cached credential for performance reasons.
 */
static __be32 *
unx_marshal(struct rpc_task *task, __be32 *p)
{
	struct rpc_clnt	*clnt = task->tk_client;
	struct rpc_cred	*cred = task->tk_rqstp->rq_cred;
	__be32		*base, *hold;
	int		i;
	struct group_info *gi = cred->cr_cred->group_info;

	*p++ = htonl(RPC_AUTH_UNIX);
	base = p++;
	*p++ = htonl(jiffies/HZ);

	/*
	 * Copy the UTS nodename captured when the client was created.
	 */
	p = xdr_encode_array(p, clnt->cl_nodename, clnt->cl_nodelen);

	*p++ = htonl((u32) from_kuid(&init_user_ns, cred->cr_cred->fsuid));
	*p++ = htonl((u32) from_kgid(&init_user_ns, cred->cr_cred->fsgid));
	hold = p++;
	if (gi)
		for (i = 0; i < UNX_NGROUPS && i < gi->ngroups; i++)
			*p++ = htonl((u32) from_kgid(&init_user_ns, gi->gid[i]));
	*hold = htonl(p - hold - 1);		/* gid array length */
	*base = htonl((p - base - 1) << 2);	/* cred length */

	*p++ = htonl(RPC_AUTH_NULL);
	*p++ = htonl(0);

	return p;
}

/*
 * Refresh credentials. This is a no-op for AUTH_UNIX
 */
static int
unx_refresh(struct rpc_task *task)
{
	set_bit(RPCAUTH_CRED_UPTODATE, &task->tk_rqstp->rq_cred->cr_flags);
	return 0;
}

static __be32 *
unx_validate(struct rpc_task *task, __be32 *p)
{
	rpc_authflavor_t	flavor;
	u32			size;

	flavor = ntohl(*p++);
	if (flavor != RPC_AUTH_NULL &&
	    flavor != RPC_AUTH_UNIX &&
	    flavor != RPC_AUTH_SHORT) {
		printk("RPC: bad verf flavor: %u\n", flavor);
		return ERR_PTR(-EIO);
	}

	size = ntohl(*p++);
	if (size > RPC_MAX_AUTH_SIZE) {
		printk("RPC: giant verf size: %u\n", size);
		return ERR_PTR(-EIO);
	}
	task->tk_rqstp->rq_cred->cr_auth->au_rslack = (size >> 2) + 2;
	p += (size >> 2);

	return p;
}

int __init rpc_init_authunix(void)
{
	unix_pool = mempool_create_kmalloc_pool(16, sizeof(struct rpc_cred));
	return unix_pool ? 0 : -ENOMEM;
}

void rpc_destroy_authunix(void)
{
	mempool_destroy(unix_pool);
}

const struct rpc_authops authunix_ops = {
	.owner		= THIS_MODULE,
	.au_flavor	= RPC_AUTH_UNIX,
	.au_name	= "UNIX",
	.create		= unx_create,
	.destroy	= unx_destroy,
	.lookup_cred	= unx_lookup_cred,
};

static
struct rpc_auth		unix_auth = {
	.au_cslack	= UNX_CALLSLACK,
	.au_rslack	= NUL_REPLYSLACK,
	.au_ops		= &authunix_ops,
	.au_flavor	= RPC_AUTH_UNIX,
	.au_count	= REFCOUNT_INIT(1),
};

static
const struct rpc_credops unix_credops = {
	.cr_name	= "AUTH_UNIX",
	.crdestroy	= unx_destroy_cred,
	.crmatch	= unx_match,
	.crmarshal	= unx_marshal,
	.crrefresh	= unx_refresh,
	.crvalidate	= unx_validate,
};
