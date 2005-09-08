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
 * cache for domain name to auth_domain
 * Entries are only added by flavours which will normally
 * have a structure that 'inherits' from auth_domain.
 * e.g. when an IP -> domainname is given to  auth_unix,
 * and the domain name doesn't exist, it will create a
 * auth_unix_domain and add it to this hash table.
 * If it finds the name does exist, but isn't AUTH_UNIX,
 * it will complain.
 */

/*
 * Auth auth_domain cache is somewhat different to other caches,
 * largely because the entries are possibly of different types:
 * each auth flavour has it's own type.
 * One consequence of this that DefineCacheLookup cannot
 * allocate a new structure as it cannot know the size.
 * Notice that the "INIT" code fragment is quite different
 * from other caches.  When auth_domain_lookup might be
 * creating a new domain, the new domain is passed in
 * complete and it is used as-is rather than being copied into
 * another structure.
 */
#define	DN_HASHBITS	6
#define	DN_HASHMAX	(1<<DN_HASHBITS)
#define	DN_HASHMASK	(DN_HASHMAX-1)

static struct cache_head	*auth_domain_table[DN_HASHMAX];

static void auth_domain_drop(struct cache_head *item, struct cache_detail *cd)
{
	struct auth_domain *dom = container_of(item, struct auth_domain, h);
	if (cache_put(item,cd))
		authtab[dom->flavour]->domain_release(dom);
}


struct cache_detail auth_domain_cache = {
	.owner		= THIS_MODULE,
	.hash_size	= DN_HASHMAX,
	.hash_table	= auth_domain_table,
	.name		= "auth.domain",
	.cache_put	= auth_domain_drop,
};

void auth_domain_put(struct auth_domain *dom)
{
	auth_domain_drop(&dom->h, &auth_domain_cache);
}

static inline int auth_domain_hash(struct auth_domain *item)
{
	return hash_str(item->name, DN_HASHBITS);
}
static inline int auth_domain_match(struct auth_domain *tmp, struct auth_domain *item)
{
	return strcmp(tmp->name, item->name) == 0;
}

struct auth_domain *
auth_domain_lookup(struct auth_domain *item, int set)
{
	struct auth_domain *tmp = NULL;
	struct cache_head **hp, **head;
	head = &auth_domain_cache.hash_table[auth_domain_hash(item)];

	if (set)
		write_lock(&auth_domain_cache.hash_lock);
	else
		read_lock(&auth_domain_cache.hash_lock);
	for (hp=head; *hp != NULL; hp = &tmp->h.next) {
		tmp = container_of(*hp, struct auth_domain, h);
		if (!auth_domain_match(tmp, item))
			continue;
		if (!set) {
			cache_get(&tmp->h);
			goto out_noset;
		}
		*hp = tmp->h.next;
		tmp->h.next = NULL;
		auth_domain_drop(&tmp->h, &auth_domain_cache);
		goto out_set;
	}
	/* Didn't find anything */
	if (!set)
		goto out_nada;
	auth_domain_cache.entries++;
out_set:
	item->h.next = *head;
	*head = &item->h;
	cache_get(&item->h);
	write_unlock(&auth_domain_cache.hash_lock);
	cache_fresh(&auth_domain_cache, &item->h, item->h.expiry_time);
	cache_get(&item->h);
	return item;
out_nada:
	tmp = NULL;
out_noset:
	read_unlock(&auth_domain_cache.hash_lock);
	return tmp;
}

struct auth_domain *auth_domain_find(char *name)
{
	struct auth_domain *rv, ad;

	ad.name = name;
	rv = auth_domain_lookup(&ad, 0);
	return rv;
}
