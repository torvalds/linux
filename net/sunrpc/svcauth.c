/*
 * linux/net/sunrpc/svcauth.c
 *
 * The generic interface for RPC authentication on the server side.
 * 
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 *
 * CHANGES
 * 19-Apr-2000 Chris Evans      - Security fix
 */

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/sunrpc/types.h>
#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/svcsock.h>
#include <linux/sunrpc/svcauth.h>
#include <linux/err.h>
#include <linux/hash.h>

#define RPCDBG_FACILITY	RPCDBG_AUTH


/*
 * Table of authenticators
 */
extern struct auth_ops svcauth_null;
extern struct auth_ops svcauth_unix;

static DEFINE_SPINLOCK(authtab_lock);
static struct auth_ops	*authtab[RPC_AUTH_MAXFLAVOR] = {
	[0] = &svcauth_null,
	[1] = &svcauth_unix,
};

int
svc_authenticate(struct svc_rqst *rqstp, u32 *authp)
{
	rpc_authflavor_t	flavor;
	struct auth_ops		*aops;

	*authp = rpc_auth_ok;

	flavor = ntohl(svc_getu32(&rqstp->rq_arg.head[0]));

	dprintk("svc: svc_authenticate (%d)\n", flavor);

	spin_lock(&authtab_lock);
	if (flavor >= RPC_AUTH_MAXFLAVOR || !(aops = authtab[flavor])
			|| !try_module_get(aops->owner)) {
		spin_unlock(&authtab_lock);
		*authp = rpc_autherr_badcred;
		return SVC_DENIED;
	}
	spin_unlock(&authtab_lock);

	rqstp->rq_authop = aops;
	return aops->accept(rqstp, authp);
}

int svc_set_client(struct svc_rqst *rqstp)
{
	return rqstp->rq_authop->set_client(rqstp);
}

/* A request, which was authenticated, has now executed.
 * Time to finalise the the credentials and verifier
 * and release and resources
 */
int svc_authorise(struct svc_rqst *rqstp)
{
	struct auth_ops *aops = rqstp->rq_authop;
	int rv = 0;

	rqstp->rq_authop = NULL;
	
	if (aops) {
		rv = aops->release(rqstp);
		module_put(aops->owner);
	}
	return rv;
}

int
svc_auth_register(rpc_authflavor_t flavor, struct auth_ops *aops)
{
	int rv = -EINVAL;
	spin_lock(&authtab_lock);
	if (flavor < RPC_AUTH_MAXFLAVOR && authtab[flavor] == NULL) {
		authtab[flavor] = aops;
		rv = 0;
	}
	spin_unlock(&authtab_lock);
	return rv;
}

void
svc_auth_unregister(rpc_authflavor_t flavor)
{
	spin_lock(&authtab_lock);
	if (flavor < RPC_AUTH_MAXFLAVOR)
		authtab[flavor] = NULL;
	spin_unlock(&authtab_lock);
}
EXPORT_SYMBOL(svc_auth_unregister);

/**************************************************
 * 'auth_domains' are stored in a hash table indexed by name.
 * When the last reference to an 'auth_domain' is dropped,
 * the object is unhashed and freed.
 * If auth_domain_lookup fails to find an entry, it will return
 * it's second argument 'new'.  If this is non-null, it will
 * have been atomically linked into the table.
 */

#define	DN_HASHBITS	6
#define	DN_HASHMAX	(1<<DN_HASHBITS)
#define	DN_HASHMASK	(DN_HASHMAX-1)

static struct hlist_head	auth_domain_table[DN_HASHMAX];
static spinlock_t	auth_domain_lock = SPIN_LOCK_UNLOCKED;

void auth_domain_put(struct auth_domain *dom)
{
	if (atomic_dec_and_lock(&dom->ref.refcount, &auth_domain_lock)) {
		hlist_del(&dom->hash);
		dom->flavour->domain_release(dom);
	}
}

struct auth_domain *
auth_domain_lookup(char *name, struct auth_domain *new)
{
	struct auth_domain *hp;
	struct hlist_head *head;
	struct hlist_node *np;

	head = &auth_domain_table[hash_str(name, DN_HASHBITS)];

	spin_lock(&auth_domain_lock);

	hlist_for_each_entry(hp, np, head, hash) {
		if (strcmp(hp->name, name)==0) {
			kref_get(&hp->ref);
			spin_unlock(&auth_domain_lock);
			return hp;
		}
	}
	if (new) {
		hlist_add_head(&new->hash, head);
		kref_get(&new->ref);
	}
	spin_unlock(&auth_domain_lock);
	return new;
}

struct auth_domain *auth_domain_find(char *name)
{
	return auth_domain_lookup(name, NULL);
}
