/*
 * linux/include/linux/sunrpc/auth.h
 *
 * Declarations for the RPC client authentication machinery.
 *
 * Copyright (C) 1996, Olaf Kirch <okir@monad.swb.de>
 */

#ifndef _LINUX_SUNRPC_AUTH_H
#define _LINUX_SUNRPC_AUTH_H

#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/sunrpc/sched.h>
#include <linux/sunrpc/msg_prot.h>
#include <linux/sunrpc/xdr.h>

#include <asm/atomic.h>

/* size of the nodename buffer */
#define UNX_MAXNODENAME	32

/* Maximum size (in bytes) of an rpc credential or verifier */
#define RPC_MAX_AUTH_SIZE (400)

/* Work around the lack of a VFS credential */
struct auth_cred {
	uid_t	uid;
	gid_t	gid;
	struct group_info *group_info;
};

/*
 * Client user credentials
 */
struct rpc_cred {
	struct hlist_node	cr_hash;	/* hash chain */
	struct rpc_credops *	cr_ops;
	unsigned long		cr_expire;	/* when to gc */
	atomic_t		cr_count;	/* ref count */
	unsigned short		cr_flags;	/* various flags */
#ifdef RPC_DEBUG
	unsigned long		cr_magic;	/* 0x0f4aa4f0 */
#endif

	uid_t			cr_uid;

	/* per-flavor data */
};
#define RPCAUTH_CRED_LOCKED	0x0001
#define RPCAUTH_CRED_UPTODATE	0x0002

#define RPCAUTH_CRED_MAGIC	0x0f4aa4f0

/*
 * Client authentication handle
 */
#define RPC_CREDCACHE_NR	8
#define RPC_CREDCACHE_MASK	(RPC_CREDCACHE_NR - 1)
struct rpc_cred_cache {
	struct hlist_head	hashtable[RPC_CREDCACHE_NR];
	unsigned long		nextgc;		/* next garbage collection */
	unsigned long		expire;		/* cache expiry interval */
};

struct rpc_auth {
	unsigned int		au_cslack;	/* call cred size estimate */
	unsigned int		au_rslack;	/* reply verf size guess */
	unsigned int		au_flags;	/* various flags */
	struct rpc_authops *	au_ops;		/* operations */
	rpc_authflavor_t	au_flavor;	/* pseudoflavor (note may
						 * differ from the flavor in
						 * au_ops->au_flavor in gss
						 * case) */
	atomic_t		au_count;	/* Reference counter */

	struct rpc_cred_cache *	au_credcache;
	/* per-flavor data */
};
#define RPC_AUTH_PROC_CREDS	0x0010		/* process creds (including
						 * uid/gid, fs[ug]id, gids)
						 */

/*
 * Client authentication ops
 */
struct rpc_authops {
	struct module		*owner;
	rpc_authflavor_t	au_flavor;	/* flavor (RPC_AUTH_*) */
#ifdef RPC_DEBUG
	char *			au_name;
#endif
	struct rpc_auth *	(*create)(struct rpc_clnt *, rpc_authflavor_t);
	void			(*destroy)(struct rpc_auth *);

	struct rpc_cred *	(*lookup_cred)(struct rpc_auth *, struct auth_cred *, int);
	struct rpc_cred *	(*crcreate)(struct rpc_auth*, struct auth_cred *, int);
};

struct rpc_credops {
	const char *		cr_name;	/* Name of the auth flavour */
	void			(*crdestroy)(struct rpc_cred *);

	int			(*crmatch)(struct auth_cred *, struct rpc_cred *, int);
	u32 *			(*crmarshal)(struct rpc_task *, u32 *);
	int			(*crrefresh)(struct rpc_task *);
	u32 *			(*crvalidate)(struct rpc_task *, u32 *);
	int			(*crwrap_req)(struct rpc_task *, kxdrproc_t,
						void *, u32 *, void *);
	int			(*crunwrap_resp)(struct rpc_task *, kxdrproc_t,
						void *, u32 *, void *);
};

extern struct rpc_authops	authunix_ops;
extern struct rpc_authops	authnull_ops;
#ifdef CONFIG_SUNRPC_SECURE
extern struct rpc_authops	authdes_ops;
#endif

int			rpcauth_register(struct rpc_authops *);
int			rpcauth_unregister(struct rpc_authops *);
struct rpc_auth *	rpcauth_create(rpc_authflavor_t, struct rpc_clnt *);
void			rpcauth_destroy(struct rpc_auth *);
struct rpc_cred *	rpcauth_lookup_credcache(struct rpc_auth *, struct auth_cred *, int);
struct rpc_cred *	rpcauth_lookupcred(struct rpc_auth *, int);
struct rpc_cred *	rpcauth_bindcred(struct rpc_task *);
void			rpcauth_holdcred(struct rpc_task *);
void			put_rpccred(struct rpc_cred *);
void			rpcauth_unbindcred(struct rpc_task *);
u32 *			rpcauth_marshcred(struct rpc_task *, u32 *);
u32 *			rpcauth_checkverf(struct rpc_task *, u32 *);
int			rpcauth_wrap_req(struct rpc_task *task, kxdrproc_t encode, void *rqstp, u32 *data, void *obj);
int			rpcauth_unwrap_resp(struct rpc_task *task, kxdrproc_t decode, void *rqstp, u32 *data, void *obj);
int			rpcauth_refreshcred(struct rpc_task *);
void			rpcauth_invalcred(struct rpc_task *);
int			rpcauth_uptodatecred(struct rpc_task *);
int			rpcauth_init_credcache(struct rpc_auth *, unsigned long);
void			rpcauth_free_credcache(struct rpc_auth *);

static inline
struct rpc_cred *	get_rpccred(struct rpc_cred *cred)
{
	atomic_inc(&cred->cr_count);
	return cred;
}

#endif /* __KERNEL__ */
#endif /* _LINUX_SUNRPC_AUTH_H */
