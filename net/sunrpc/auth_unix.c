/*
 * linux/net/sunrpc/auth_unix.c
 *
 * UNIX-style authentication; no AUTH_SHORT support
 *
 * Copyright (C) 1996, Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/auth.h>

#define NFS_NGROUPS	16

struct unx_cred {
	struct rpc_cred		uc_base;
	gid_t			uc_gid;
	gid_t			uc_gids[NFS_NGROUPS];
};
#define uc_uid			uc_base.cr_uid
#define uc_count		uc_base.cr_count
#define uc_flags		uc_base.cr_flags
#define uc_expire		uc_base.cr_expire

#define UNX_CRED_EXPIRE		(60 * HZ)

#define UNX_WRITESLACK		(21 + (UNX_MAXNODENAME >> 2))

#ifdef RPC_DEBUG
# define RPCDBG_FACILITY	RPCDBG_AUTH
#endif

static struct rpc_auth		unix_auth;
static struct rpc_cred_cache	unix_cred_cache;
static struct rpc_credops	unix_credops;

static struct rpc_auth *
unx_create(struct rpc_clnt *clnt, rpc_authflavor_t flavor)
{
	dprintk("RPC: creating UNIX authenticator for client %p\n", clnt);
	if (atomic_inc_return(&unix_auth.au_count) == 0)
		unix_cred_cache.nextgc = jiffies + (unix_cred_cache.expire >> 1);
	return &unix_auth;
}

static void
unx_destroy(struct rpc_auth *auth)
{
	dprintk("RPC: destroying UNIX authenticator %p\n", auth);
	rpcauth_free_credcache(auth);
}

/*
 * Lookup AUTH_UNIX creds for current process
 */
static struct rpc_cred *
unx_lookup_cred(struct rpc_auth *auth, struct auth_cred *acred, int flags)
{
	return rpcauth_lookup_credcache(auth, acred, flags);
}

static struct rpc_cred *
unx_create_cred(struct rpc_auth *auth, struct auth_cred *acred, int flags)
{
	struct unx_cred	*cred;
	int		i;

	dprintk("RPC:      allocating UNIX cred for uid %d gid %d\n",
				acred->uid, acred->gid);

	if (!(cred = kmalloc(sizeof(*cred), GFP_KERNEL)))
		return ERR_PTR(-ENOMEM);

	atomic_set(&cred->uc_count, 1);
	cred->uc_flags = RPCAUTH_CRED_UPTODATE;
	if (flags & RPC_TASK_ROOTCREDS) {
		cred->uc_uid = 0;
		cred->uc_gid = 0;
		cred->uc_gids[0] = NOGROUP;
	} else {
		int groups = acred->group_info->ngroups;
		if (groups > NFS_NGROUPS)
			groups = NFS_NGROUPS;

		cred->uc_uid = acred->uid;
		cred->uc_gid = acred->gid;
		for (i = 0; i < groups; i++)
			cred->uc_gids[i] = GROUP_AT(acred->group_info, i);
		if (i < NFS_NGROUPS)
		  cred->uc_gids[i] = NOGROUP;
	}
	cred->uc_base.cr_ops = &unix_credops;

	return (struct rpc_cred *) cred;
}

static void
unx_destroy_cred(struct rpc_cred *cred)
{
	kfree(cred);
}

/*
 * Match credentials against current process creds.
 * The root_override argument takes care of cases where the caller may
 * request root creds (e.g. for NFS swapping).
 */
static int
unx_match(struct auth_cred *acred, struct rpc_cred *rcred, int taskflags)
{
	struct unx_cred	*cred = (struct unx_cred *) rcred;
	int		i;

	if (!(taskflags & RPC_TASK_ROOTCREDS)) {
		int groups;

		if (cred->uc_uid != acred->uid
		 || cred->uc_gid != acred->gid)
			return 0;

		groups = acred->group_info->ngroups;
		if (groups > NFS_NGROUPS)
			groups = NFS_NGROUPS;
		for (i = 0; i < groups ; i++)
			if (cred->uc_gids[i] != GROUP_AT(acred->group_info, i))
				return 0;
		return 1;
	}
	return (cred->uc_uid == 0
	     && cred->uc_gid == 0
	     && cred->uc_gids[0] == (gid_t) NOGROUP);
}

/*
 * Marshal credentials.
 * Maybe we should keep a cached credential for performance reasons.
 */
static u32 *
unx_marshal(struct rpc_task *task, u32 *p)
{
	struct rpc_clnt	*clnt = task->tk_client;
	struct unx_cred	*cred = (struct unx_cred *) task->tk_msg.rpc_cred;
	u32		*base, *hold;
	int		i;

	*p++ = htonl(RPC_AUTH_UNIX);
	base = p++;
	*p++ = htonl(jiffies/HZ);

	/*
	 * Copy the UTS nodename captured when the client was created.
	 */
	p = xdr_encode_array(p, clnt->cl_nodename, clnt->cl_nodelen);

	*p++ = htonl((u32) cred->uc_uid);
	*p++ = htonl((u32) cred->uc_gid);
	hold = p++;
	for (i = 0; i < 16 && cred->uc_gids[i] != (gid_t) NOGROUP; i++)
		*p++ = htonl((u32) cred->uc_gids[i]);
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
	task->tk_msg.rpc_cred->cr_flags |= RPCAUTH_CRED_UPTODATE;
	return 0;
}

static u32 *
unx_validate(struct rpc_task *task, u32 *p)
{
	rpc_authflavor_t	flavor;
	u32			size;

	flavor = ntohl(*p++);
	if (flavor != RPC_AUTH_NULL &&
	    flavor != RPC_AUTH_UNIX &&
	    flavor != RPC_AUTH_SHORT) {
		printk("RPC: bad verf flavor: %u\n", flavor);
		return NULL;
	}

	size = ntohl(*p++);
	if (size > RPC_MAX_AUTH_SIZE) {
		printk("RPC: giant verf size: %u\n", size);
		return NULL;
	}
	task->tk_auth->au_rslack = (size >> 2) + 2;
	p += (size >> 2);

	return p;
}

struct rpc_authops	authunix_ops = {
	.owner		= THIS_MODULE,
	.au_flavor	= RPC_AUTH_UNIX,
#ifdef RPC_DEBUG
	.au_name	= "UNIX",
#endif
	.create		= unx_create,
	.destroy	= unx_destroy,
	.lookup_cred	= unx_lookup_cred,
	.crcreate	= unx_create_cred,
};

static
struct rpc_cred_cache	unix_cred_cache = {
	.expire		= UNX_CRED_EXPIRE,
};

static
struct rpc_auth		unix_auth = {
	.au_cslack	= UNX_WRITESLACK,
	.au_rslack	= 2,			/* assume AUTH_NULL verf */
	.au_ops		= &authunix_ops,
	.au_count	= ATOMIC_INIT(0),
	.au_credcache	= &unix_cred_cache,
};

static
struct rpc_credops	unix_credops = {
	.cr_name	= "AUTH_UNIX",
	.crdestroy	= unx_destroy_cred,
	.crmatch	= unx_match,
	.crmarshal	= unx_marshal,
	.crrefresh	= unx_refresh,
	.crvalidate	= unx_validate,
};
