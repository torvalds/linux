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

#include <linux/sunrpc/sched.h>
#include <linux/sunrpc/msg_prot.h>
#include <linux/sunrpc/xdr.h>

#include <linux/atomic.h>
#include <linux/rcupdate.h>
#include <linux/uidgid.h>
#include <linux/utsname.h>

/*
 * Maximum size of AUTH_NONE authentication information, in XDR words.
 */
#define NUL_CALLSLACK	(4)
#define NUL_REPLYSLACK	(2)

/*
 * Size of the nodename buffer. RFC1831 specifies a hard limit of 255 bytes,
 * but Linux hostnames are actually limited to __NEW_UTS_LEN bytes.
 */
#define UNX_MAXNODENAME	__NEW_UTS_LEN
#define UNX_CALLSLACK	(21 + XDR_QUADLEN(UNX_MAXNODENAME))

struct rpcsec_gss_info;

/* auth_cred ac_flags bits */
enum {
	RPC_CRED_KEY_EXPIRE_SOON = 1, /* underlying cred key will expire soon */
	RPC_CRED_NOTIFY_TIMEOUT = 2,   /* nofity generic cred when underlying
					key will expire soon */
};

/* Work around the lack of a VFS credential */
struct auth_cred {
	kuid_t	uid;
	kgid_t	gid;
	struct group_info *group_info;
	const char *principal;
	unsigned long ac_flags;
	unsigned char machine_cred : 1;
};

/*
 * Client user credentials
 */
struct rpc_auth;
struct rpc_credops;
struct rpc_cred {
	struct hlist_node	cr_hash;	/* hash chain */
	struct list_head	cr_lru;		/* lru garbage collection */
	struct rcu_head		cr_rcu;
	struct rpc_auth *	cr_auth;
	const struct rpc_credops *cr_ops;
#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
	unsigned long		cr_magic;	/* 0x0f4aa4f0 */
#endif
	unsigned long		cr_expire;	/* when to gc */
	unsigned long		cr_flags;	/* various flags */
	atomic_t		cr_count;	/* ref count */

	kuid_t			cr_uid;

	/* per-flavor data */
};
#define RPCAUTH_CRED_NEW	0
#define RPCAUTH_CRED_UPTODATE	1
#define RPCAUTH_CRED_HASHED	2
#define RPCAUTH_CRED_NEGATIVE	3

#define RPCAUTH_CRED_MAGIC	0x0f4aa4f0

/* rpc_auth au_flags */
#define RPCAUTH_AUTH_NO_CRKEY_TIMEOUT	0x0001 /* underlying cred has no key timeout */

/*
 * Client authentication handle
 */
struct rpc_cred_cache;
struct rpc_authops;
struct rpc_auth {
	unsigned int		au_cslack;	/* call cred size estimate */
				/* guess at number of u32's auth adds before
				 * reply data; normally the verifier size: */
	unsigned int		au_rslack;
				/* for gss, used to calculate au_rslack: */
	unsigned int		au_verfsize;

	unsigned int		au_flags;	/* various flags */
	const struct rpc_authops *au_ops;		/* operations */
	rpc_authflavor_t	au_flavor;	/* pseudoflavor (note may
						 * differ from the flavor in
						 * au_ops->au_flavor in gss
						 * case) */
	atomic_t		au_count;	/* Reference counter */

	struct rpc_cred_cache *	au_credcache;
	/* per-flavor data */
};

/* rpc_auth au_flags */
#define RPCAUTH_AUTH_DATATOUCH	0x00000002

struct rpc_auth_create_args {
	rpc_authflavor_t pseudoflavor;
	const char *target_name;
};

/* Flags for rpcauth_lookupcred() */
#define RPCAUTH_LOOKUP_NEW		0x01	/* Accept an uninitialised cred */
#define RPCAUTH_LOOKUP_RCU		0x02	/* lock-less lookup */

/*
 * Client authentication ops
 */
struct rpc_authops {
	struct module		*owner;
	rpc_authflavor_t	au_flavor;	/* flavor (RPC_AUTH_*) */
	char *			au_name;
	struct rpc_auth *	(*create)(struct rpc_auth_create_args *, struct rpc_clnt *);
	void			(*destroy)(struct rpc_auth *);

	int			(*hash_cred)(struct auth_cred *, unsigned int);
	struct rpc_cred *	(*lookup_cred)(struct rpc_auth *, struct auth_cred *, int);
	struct rpc_cred *	(*crcreate)(struct rpc_auth*, struct auth_cred *, int, gfp_t);
	int			(*list_pseudoflavors)(rpc_authflavor_t *, int);
	rpc_authflavor_t	(*info2flavor)(struct rpcsec_gss_info *);
	int			(*flavor2info)(rpc_authflavor_t,
						struct rpcsec_gss_info *);
	int			(*key_timeout)(struct rpc_auth *,
						struct rpc_cred *);
};

struct rpc_credops {
	const char *		cr_name;	/* Name of the auth flavour */
	int			(*cr_init)(struct rpc_auth *, struct rpc_cred *);
	void			(*crdestroy)(struct rpc_cred *);

	int			(*crmatch)(struct auth_cred *, struct rpc_cred *, int);
	struct rpc_cred *	(*crbind)(struct rpc_task *, struct rpc_cred *, int);
	__be32 *		(*crmarshal)(struct rpc_task *, __be32 *);
	int			(*crrefresh)(struct rpc_task *);
	__be32 *		(*crvalidate)(struct rpc_task *, __be32 *);
	int			(*crwrap_req)(struct rpc_task *, kxdreproc_t,
						void *, __be32 *, void *);
	int			(*crunwrap_resp)(struct rpc_task *, kxdrdproc_t,
						void *, __be32 *, void *);
	int			(*crkey_timeout)(struct rpc_cred *);
	bool			(*crkey_to_expire)(struct rpc_cred *);
	char *			(*crstringify_acceptor)(struct rpc_cred *);
};

extern const struct rpc_authops	authunix_ops;
extern const struct rpc_authops	authnull_ops;

int __init		rpc_init_authunix(void);
int __init		rpc_init_generic_auth(void);
int __init		rpcauth_init_module(void);
void			rpcauth_remove_module(void);
void			rpc_destroy_generic_auth(void);
void 			rpc_destroy_authunix(void);

struct rpc_cred *	rpc_lookup_cred(void);
struct rpc_cred *	rpc_lookup_cred_nonblock(void);
struct rpc_cred *	rpc_lookup_generic_cred(struct auth_cred *, int, gfp_t);
struct rpc_cred *	rpc_lookup_machine_cred(const char *service_name);
int			rpcauth_register(const struct rpc_authops *);
int			rpcauth_unregister(const struct rpc_authops *);
struct rpc_auth *	rpcauth_create(struct rpc_auth_create_args *,
				struct rpc_clnt *);
void			rpcauth_release(struct rpc_auth *);
rpc_authflavor_t	rpcauth_get_pseudoflavor(rpc_authflavor_t,
				struct rpcsec_gss_info *);
int			rpcauth_get_gssinfo(rpc_authflavor_t,
				struct rpcsec_gss_info *);
int			rpcauth_list_flavors(rpc_authflavor_t *, int);
struct rpc_cred *	rpcauth_lookup_credcache(struct rpc_auth *, struct auth_cred *, int, gfp_t);
void			rpcauth_init_cred(struct rpc_cred *, const struct auth_cred *, struct rpc_auth *, const struct rpc_credops *);
struct rpc_cred *	rpcauth_lookupcred(struct rpc_auth *, int);
struct rpc_cred *	rpcauth_generic_bind_cred(struct rpc_task *, struct rpc_cred *, int);
void			put_rpccred(struct rpc_cred *);
__be32 *		rpcauth_marshcred(struct rpc_task *, __be32 *);
__be32 *		rpcauth_checkverf(struct rpc_task *, __be32 *);
int			rpcauth_wrap_req(struct rpc_task *task, kxdreproc_t encode, void *rqstp, __be32 *data, void *obj);
int			rpcauth_unwrap_resp(struct rpc_task *task, kxdrdproc_t decode, void *rqstp, __be32 *data, void *obj);
int			rpcauth_refreshcred(struct rpc_task *);
void			rpcauth_invalcred(struct rpc_task *);
int			rpcauth_uptodatecred(struct rpc_task *);
int			rpcauth_init_credcache(struct rpc_auth *);
void			rpcauth_destroy_credcache(struct rpc_auth *);
void			rpcauth_clear_credcache(struct rpc_cred_cache *);
int			rpcauth_key_timeout_notify(struct rpc_auth *,
						struct rpc_cred *);
bool			rpcauth_cred_key_to_expire(struct rpc_auth *, struct rpc_cred *);
char *			rpcauth_stringify_acceptor(struct rpc_cred *);

static inline
struct rpc_cred *	get_rpccred(struct rpc_cred *cred)
{
	if (cred != NULL)
		atomic_inc(&cred->cr_count);
	return cred;
}

/**
 * get_rpccred_rcu - get a reference to a cred using rcu-protected pointer
 * @cred: cred of which to take a reference
 *
 * In some cases, we may have a pointer to a credential to which we
 * want to take a reference, but don't already have one. Because these
 * objects are freed using RCU, we can access the cr_count while its
 * on its way to destruction and only take a reference if it's not already
 * zero.
 */
static inline struct rpc_cred *
get_rpccred_rcu(struct rpc_cred *cred)
{
	if (atomic_inc_not_zero(&cred->cr_count))
		return cred;
	return NULL;
}

#endif /* __KERNEL__ */
#endif /* _LINUX_SUNRPC_AUTH_H */
