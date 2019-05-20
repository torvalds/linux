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
	refcount_inc(&unix_auth.au_count);
	return &unix_auth;
}

static void
unx_destroy(struct rpc_auth *auth)
{
}

/*
 * Lookup AUTH_UNIX creds for current process
 */
static struct rpc_cred *
unx_lookup_cred(struct rpc_auth *auth, struct auth_cred *acred, int flags)
{
	struct rpc_cred *ret = mempool_alloc(unix_pool, GFP_NOFS);

	rpcauth_init_cred(ret, acred, auth, &unix_credops);
	ret->cr_flags = 1UL << RPCAUTH_CRED_UPTODATE;
	return ret;
}

static void
unx_free_cred_callback(struct rcu_head *head)
{
	struct rpc_cred *rpc_cred = container_of(head, struct rpc_cred, cr_rcu);

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

	if (acred->cred->group_info != NULL)
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
static int
unx_marshal(struct rpc_task *task, struct xdr_stream *xdr)
{
	struct rpc_clnt	*clnt = task->tk_client;
	struct rpc_cred	*cred = task->tk_rqstp->rq_cred;
	__be32		*p, *cred_len, *gidarr_len;
	int		i;
	struct group_info *gi = cred->cr_cred->group_info;
	struct user_namespace *userns = clnt->cl_cred ?
		clnt->cl_cred->user_ns : &init_user_ns;

	/* Credential */

	p = xdr_reserve_space(xdr, 3 * sizeof(*p));
	if (!p)
		goto marshal_failed;
	*p++ = rpc_auth_unix;
	cred_len = p++;
	*p++ = xdr_zero;	/* stamp */
	if (xdr_stream_encode_opaque(xdr, clnt->cl_nodename,
				     clnt->cl_nodelen) < 0)
		goto marshal_failed;
	p = xdr_reserve_space(xdr, 3 * sizeof(*p));
	if (!p)
		goto marshal_failed;
	*p++ = cpu_to_be32(from_kuid_munged(userns, cred->cr_cred->fsuid));
	*p++ = cpu_to_be32(from_kgid_munged(userns, cred->cr_cred->fsgid));

	gidarr_len = p++;
	if (gi)
		for (i = 0; i < UNX_NGROUPS && i < gi->ngroups; i++)
			*p++ = cpu_to_be32(from_kgid_munged(userns, gi->gid[i]));
	*gidarr_len = cpu_to_be32(p - gidarr_len - 1);
	*cred_len = cpu_to_be32((p - cred_len - 1) << 2);
	p = xdr_reserve_space(xdr, (p - gidarr_len - 1) << 2);
	if (!p)
		goto marshal_failed;

	/* Verifier */

	p = xdr_reserve_space(xdr, 2 * sizeof(*p));
	if (!p)
		goto marshal_failed;
	*p++ = rpc_auth_null;
	*p   = xdr_zero;

	return 0;

marshal_failed:
	return -EMSGSIZE;
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

static int
unx_validate(struct rpc_task *task, struct xdr_stream *xdr)
{
	struct rpc_auth *auth = task->tk_rqstp->rq_cred->cr_auth;
	__be32 *p;
	u32 size;

	p = xdr_inline_decode(xdr, 2 * sizeof(*p));
	if (!p)
		return -EIO;
	switch (*p++) {
	case rpc_auth_null:
	case rpc_auth_unix:
	case rpc_auth_short:
		break;
	default:
		return -EIO;
	}
	size = be32_to_cpup(p);
	if (size > RPC_MAX_AUTH_SIZE)
		return -EIO;
	p = xdr_inline_decode(xdr, size);
	if (!p)
		return -EIO;

	auth->au_verfsize = XDR_QUADLEN(size) + 2;
	auth->au_rslack = XDR_QUADLEN(size) + 2;
	auth->au_ralign = XDR_QUADLEN(size) + 2;
	return 0;
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
	.au_verfsize	= NUL_REPLYSLACK,
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
	.crwrap_req	= rpcauth_wrap_req_encode,
	.crrefresh	= unx_refresh,
	.crvalidate	= unx_validate,
	.crunwrap_resp	= rpcauth_unwrap_resp_decode,
};
