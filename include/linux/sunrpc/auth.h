/* SPDX-License-Identifier: GPL-2.0 */
/*
 * linux/include/linux/sunrpc/auth.h
 *
 * Declarations for the RPC client authentication machinery.
 *
 * Copyright (C) 1996, Olaf Kirch <okir@monad.swb.de>
 */

#ifndef _LINUX_SUNRPC_AUTH_H
#define _LINUX_SUNRPC_AUTH_H

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
#define UNX_NGROUPS	16

struct rpcsec_gss_info;

struct auth_cred {
	const struct cred *cred;
	const char *principal;	/* If present, this is a machine credential */
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
	unsigned long		cr_expire;	/* when to gc */
	unsigned long		cr_flags;	/* various flags */
	refcount_t		cr_count;	/* ref count */
	const struct cred	*cr_cred;

	/* per-flavor data */
};
#define RPCAUTH_CRED_NEW	0
#define RPCAUTH_CRED_UPTODATE	1
#define RPCAUTH_CRED_HASHED	2
#define RPCAUTH_CRED_NEGATIVE	3

const struct cred *rpc_machine_cred(void);

/*
 * Client authentication handle
 */
struct rpc_cred_cache;
struct rpc_authops;
struct rpc_auth {
	unsigned int		au_cslack;	/* call cred size estimate */
	unsigned int		au_rslack;	/* reply cred size estimate */
	unsigned int		au_verfsize;	/* size of reply verifier */
	unsigned int		au_ralign;	/* words before UL header */

	unsigned int		au_flags;
	const struct rpc_authops *au_ops;
	rpc_authflavor_t	au_flavor;	/* pseudoflavor (note may
						 * differ from the flavor in
						 * au_ops->au_flavor in gss
						 * case) */
	refcount_t		au_count;	/* Reference counter */

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

/*
 * Client authentication ops
 */
struct rpc_authops {
	struct module		*owner;
	rpc_authflavor_t	au_flavor;	/* flavor (RPC_AUTH_*) */
	char *			au_name;
	struct rpc_auth *	(*create)(const struct rpc_auth_create_args *,
					  struct rpc_clnt *);
	void			(*destroy)(struct rpc_auth *);

	int			(*hash_cred)(struct auth_cred *, unsigned int);
	struct rpc_cred *	(*lookup_cred)(struct rpc_auth *, struct auth_cred *, int);
	struct rpc_cred *	(*crcreate)(struct rpc_auth*, struct auth_cred *, int, gfp_t);
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
	int			(*crmarshal)(struct rpc_task *task,
					     struct xdr_stream *xdr);
	int			(*crrefresh)(struct rpc_task *);
	int			(*crvalidate)(struct rpc_task *task,
					      struct xdr_stream *xdr);
	int			(*crwrap_req)(struct rpc_task *task,
					      struct xdr_stream *xdr);
	int			(*crunwrap_resp)(struct rpc_task *task,
						 struct xdr_stream *xdr);
	int			(*crkey_timeout)(struct rpc_cred *);
	char *			(*crstringify_acceptor)(struct rpc_cred *);
	bool			(*crneed_reencode)(struct rpc_task *);
};

extern const struct rpc_authops	authunix_ops;
extern const struct rpc_authops	authnull_ops;

int __init		rpc_init_authunix(void);
int __init		rpcauth_init_module(void);
void			rpcauth_remove_module(void);
void 			rpc_destroy_authunix(void);

int			rpcauth_register(const struct rpc_authops *);
int			rpcauth_unregister(const struct rpc_authops *);
struct rpc_auth *	rpcauth_create(const struct rpc_auth_create_args *,
				struct rpc_clnt *);
void			rpcauth_release(struct rpc_auth *);
rpc_authflavor_t	rpcauth_get_pseudoflavor(rpc_authflavor_t,
				struct rpcsec_gss_info *);
int			rpcauth_get_gssinfo(rpc_authflavor_t,
				struct rpcsec_gss_info *);
struct rpc_cred *	rpcauth_lookup_credcache(struct rpc_auth *, struct auth_cred *, int, gfp_t);
void			rpcauth_init_cred(struct rpc_cred *, const struct auth_cred *, struct rpc_auth *, const struct rpc_credops *);
struct rpc_cred *	rpcauth_lookupcred(struct rpc_auth *, int);
void			put_rpccred(struct rpc_cred *);
int			rpcauth_marshcred(struct rpc_task *task,
					  struct xdr_stream *xdr);
int			rpcauth_checkverf(struct rpc_task *task,
					  struct xdr_stream *xdr);
int			rpcauth_wrap_req_encode(struct rpc_task *task,
						struct xdr_stream *xdr);
int			rpcauth_wrap_req(struct rpc_task *task,
					 struct xdr_stream *xdr);
int			rpcauth_unwrap_resp_decode(struct rpc_task *task,
						   struct xdr_stream *xdr);
int			rpcauth_unwrap_resp(struct rpc_task *task,
					    struct xdr_stream *xdr);
bool			rpcauth_xmit_need_reencode(struct rpc_task *task);
int			rpcauth_refreshcred(struct rpc_task *);
void			rpcauth_invalcred(struct rpc_task *);
int			rpcauth_uptodatecred(struct rpc_task *);
int			rpcauth_init_credcache(struct rpc_auth *);
void			rpcauth_destroy_credcache(struct rpc_auth *);
void			rpcauth_clear_credcache(struct rpc_cred_cache *);
char *			rpcauth_stringify_acceptor(struct rpc_cred *);

static inline
struct rpc_cred *get_rpccred(struct rpc_cred *cred)
{
	if (cred != NULL && refcount_inc_not_zero(&cred->cr_count))
		return cred;
	return NULL;
}

#endif /* _LINUX_SUNRPC_AUTH_H */
