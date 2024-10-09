/* SPDX-License-Identifier: GPL-2.0 */
/*
 * linux/include/linux/sunrpc/svcauth.h
 *
 * RPC server-side authentication stuff.
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 */

#ifndef _LINUX_SUNRPC_SVCAUTH_H_
#define _LINUX_SUNRPC_SVCAUTH_H_

#include <linux/string.h>
#include <linux/sunrpc/msg_prot.h>
#include <linux/sunrpc/cache.h>
#include <linux/sunrpc/gss_api.h>
#include <linux/sunrpc/clnt.h>
#include <linux/hash.h>
#include <linux/stringhash.h>
#include <linux/cred.h>

struct svc_cred {
	kuid_t			cr_uid;
	kgid_t			cr_gid;
	struct group_info	*cr_group_info;
	u32			cr_flavor; /* pseudoflavor */
	/* name of form servicetype/hostname@REALM, passed down by
	 * gss-proxy: */
	char			*cr_raw_principal;
	/* name of form servicetype@hostname, passed down by
	 * rpc.svcgssd, or computed from the above: */
	char			*cr_principal;
	char			*cr_targ_princ;
	struct gss_api_mech	*cr_gss_mech;
};

static inline void init_svc_cred(struct svc_cred *cred)
{
	cred->cr_group_info = NULL;
	cred->cr_raw_principal = NULL;
	cred->cr_principal = NULL;
	cred->cr_targ_princ = NULL;
	cred->cr_gss_mech = NULL;
}

static inline void free_svc_cred(struct svc_cred *cred)
{
	if (cred->cr_group_info)
		put_group_info(cred->cr_group_info);
	kfree(cred->cr_raw_principal);
	kfree(cred->cr_principal);
	kfree(cred->cr_targ_princ);
	gss_mech_put(cred->cr_gss_mech);
	init_svc_cred(cred);
}

struct svc_rqst;		/* forward decl */
struct in6_addr;

/* Authentication is done in the context of a domain.
 *
 * Currently, the nfs server uses the auth_domain to stand
 * for the "client" listed in /etc/exports.
 *
 * More generally, a domain might represent a group of clients using
 * a common mechanism for authentication and having a common mapping
 * between local identity (uid) and network identity.  All clients
 * in a domain have similar general access rights.  Each domain can
 * contain multiple principals which will have different specific right
 * based on normal Discretionary Access Control.
 *
 * A domain is created by an authentication flavour module based on name
 * only.  Userspace then fills in detail on demand.
 *
 * In the case of auth_unix and auth_null, the auth_domain is also
 * associated with entries in another cache representing the mapping
 * of ip addresses to the given client.
 */
struct auth_domain {
	struct kref		ref;
	struct hlist_node	hash;
	char			*name;
	struct auth_ops		*flavour;
	struct rcu_head		rcu_head;
};

enum svc_auth_status {
	SVC_GARBAGE = 1,
	SVC_SYSERR,
	SVC_VALID,
	SVC_NEGATIVE,
	SVC_OK,
	SVC_DROP,
	SVC_CLOSE,
	SVC_DENIED,
	SVC_PENDING,
	SVC_COMPLETE,
};

/*
 * Each authentication flavour registers an auth_ops
 * structure.
 * name is simply the name.
 * flavour gives the auth flavour. It determines where the flavour is registered
 * accept() is given a request and should verify it.
 *   It should inspect the authenticator and verifier, and possibly the data.
 *    If there is a problem with the authentication *authp should be set.
 *    The return value of accept() can indicate:
 *      OK - authorised. client and credential are set in rqstp.
 *           reqbuf points to arguments
 *           resbuf points to good place for results.  verfier
 *             is (probably) already in place.  Certainly space is
 *	       reserved for it.
 *      DROP - simply drop the request. It may have been deferred
 *      CLOSE - like SVC_DROP, but request is definitely lost.
 *		If there is a tcp connection, it should be closed.
 *      GARBAGE - rpc garbage_args error
 *      SYSERR - rpc system_err error
 *      DENIED - authp holds reason for denial.
 *      COMPLETE - the reply is encoded already and ready to be sent; no
 *		further processing is necessary.  (This is used for processing
 *		null procedure calls which are used to set up encryption
 *		contexts.)
 *
 *   accept is passed the proc number so that it can accept NULL rpc requests
 *   even if it cannot authenticate the client (as is sometimes appropriate).
 *
 * release() is given a request after the procedure has been run.
 *  It should sign/encrypt the results if needed
 *
 * domain_release()
 *   This call releases a domain.
 *
 * set_client()
 *   Given a pending request (struct svc_rqst), finds and assigns
 *   an appropriate 'auth_domain' as the client.
 *
 * pseudoflavor()
 *   Returns RPC_AUTH pseudoflavor in use by @rqstp.
 */
struct auth_ops {
	char *	name;
	struct module *owner;
	int	flavour;

	enum svc_auth_status	(*accept)(struct svc_rqst *rqstp);
	int			(*release)(struct svc_rqst *rqstp);
	void			(*domain_release)(struct auth_domain *dom);
	enum svc_auth_status	(*set_client)(struct svc_rqst *rqstp);
	rpc_authflavor_t	(*pseudoflavor)(struct svc_rqst *rqstp);
};

struct svc_xprt;

extern rpc_authflavor_t svc_auth_flavor(struct svc_rqst *rqstp);
extern int	svc_authorise(struct svc_rqst *rqstp);
extern enum svc_auth_status svc_set_client(struct svc_rqst *rqstp);
extern int	svc_auth_register(rpc_authflavor_t flavor, struct auth_ops *aops);
extern void	svc_auth_unregister(rpc_authflavor_t flavor);

extern void	svcauth_map_clnt_to_svc_cred_local(struct rpc_clnt *clnt,
						   const struct cred *,
						   struct svc_cred *);

extern struct auth_domain *unix_domain_find(char *name);
extern void auth_domain_put(struct auth_domain *item);
extern struct auth_domain *auth_domain_lookup(char *name, struct auth_domain *new);
extern struct auth_domain *auth_domain_find(char *name);
extern void svcauth_unix_purge(struct net *net);
extern void svcauth_unix_info_release(struct svc_xprt *xpt);
extern enum svc_auth_status svcauth_unix_set_client(struct svc_rqst *rqstp);

extern int unix_gid_cache_create(struct net *net);
extern void unix_gid_cache_destroy(struct net *net);

/*
 * The <stringhash.h> functions are good enough that we don't need to
 * use hash_32() on them; just extracting the high bits is enough.
 */
static inline unsigned long hash_str(char const *name, int bits)
{
	return hashlen_hash(hashlen_string(NULL, name)) >> (32 - bits);
}

static inline unsigned long hash_mem(char const *buf, int length, int bits)
{
	return full_name_hash(NULL, buf, length) >> (32 - bits);
}

#endif /* _LINUX_SUNRPC_SVCAUTH_H_ */
