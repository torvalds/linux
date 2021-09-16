// SPDX-License-Identifier: GPL-2.0-only
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
#include <linux/module.h>
#include <linux/sunrpc/types.h>
#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/svcsock.h>
#include <linux/sunrpc/svcauth.h>
#include <linux/err.h>
#include <linux/hash.h>

#include <trace/events/sunrpc.h>

#include "sunrpc.h"

#define RPCDBG_FACILITY	RPCDBG_AUTH


/*
 * Table of authenticators
 */
extern struct auth_ops svcauth_null;
extern struct auth_ops svcauth_unix;

static struct auth_ops __rcu *authtab[RPC_AUTH_MAXFLAVOR] = {
	[RPC_AUTH_NULL] = (struct auth_ops __force __rcu *)&svcauth_null,
	[RPC_AUTH_UNIX] = (struct auth_ops __force __rcu *)&svcauth_unix,
};

static struct auth_ops *
svc_get_auth_ops(rpc_authflavor_t flavor)
{
	struct auth_ops		*aops;

	if (flavor >= RPC_AUTH_MAXFLAVOR)
		return NULL;
	rcu_read_lock();
	aops = rcu_dereference(authtab[flavor]);
	if (aops != NULL && !try_module_get(aops->owner))
		aops = NULL;
	rcu_read_unlock();
	return aops;
}

static void
svc_put_auth_ops(struct auth_ops *aops)
{
	module_put(aops->owner);
}

int
svc_authenticate(struct svc_rqst *rqstp)
{
	rpc_authflavor_t	flavor;
	struct auth_ops		*aops;

	rqstp->rq_auth_stat = rpc_auth_ok;

	flavor = svc_getnl(&rqstp->rq_arg.head[0]);

	dprintk("svc: svc_authenticate (%d)\n", flavor);

	aops = svc_get_auth_ops(flavor);
	if (aops == NULL) {
		rqstp->rq_auth_stat = rpc_autherr_badcred;
		return SVC_DENIED;
	}

	rqstp->rq_auth_slack = 0;
	init_svc_cred(&rqstp->rq_cred);

	rqstp->rq_authop = aops;
	return aops->accept(rqstp);
}
EXPORT_SYMBOL_GPL(svc_authenticate);

int svc_set_client(struct svc_rqst *rqstp)
{
	rqstp->rq_client = NULL;
	return rqstp->rq_authop->set_client(rqstp);
}
EXPORT_SYMBOL_GPL(svc_set_client);

/* A request, which was authenticated, has now executed.
 * Time to finalise the credentials and verifier
 * and release and resources
 */
int svc_authorise(struct svc_rqst *rqstp)
{
	struct auth_ops *aops = rqstp->rq_authop;
	int rv = 0;

	rqstp->rq_authop = NULL;

	if (aops) {
		rv = aops->release(rqstp);
		svc_put_auth_ops(aops);
	}
	return rv;
}

int
svc_auth_register(rpc_authflavor_t flavor, struct auth_ops *aops)
{
	struct auth_ops *old;
	int rv = -EINVAL;

	if (flavor < RPC_AUTH_MAXFLAVOR) {
		old = cmpxchg((struct auth_ops ** __force)&authtab[flavor], NULL, aops);
		if (old == NULL || old == aops)
			rv = 0;
	}
	return rv;
}
EXPORT_SYMBOL_GPL(svc_auth_register);

void
svc_auth_unregister(rpc_authflavor_t flavor)
{
	if (flavor < RPC_AUTH_MAXFLAVOR)
		rcu_assign_pointer(authtab[flavor], NULL);
}
EXPORT_SYMBOL_GPL(svc_auth_unregister);

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

static struct hlist_head	auth_domain_table[DN_HASHMAX];
static DEFINE_SPINLOCK(auth_domain_lock);

static void auth_domain_release(struct kref *kref)
	__releases(&auth_domain_lock)
{
	struct auth_domain *dom = container_of(kref, struct auth_domain, ref);

	hlist_del_rcu(&dom->hash);
	dom->flavour->domain_release(dom);
	spin_unlock(&auth_domain_lock);
}

void auth_domain_put(struct auth_domain *dom)
{
	kref_put_lock(&dom->ref, auth_domain_release, &auth_domain_lock);
}
EXPORT_SYMBOL_GPL(auth_domain_put);

struct auth_domain *
auth_domain_lookup(char *name, struct auth_domain *new)
{
	struct auth_domain *hp;
	struct hlist_head *head;

	head = &auth_domain_table[hash_str(name, DN_HASHBITS)];

	spin_lock(&auth_domain_lock);

	hlist_for_each_entry(hp, head, hash) {
		if (strcmp(hp->name, name)==0) {
			kref_get(&hp->ref);
			spin_unlock(&auth_domain_lock);
			return hp;
		}
	}
	if (new)
		hlist_add_head_rcu(&new->hash, head);
	spin_unlock(&auth_domain_lock);
	return new;
}
EXPORT_SYMBOL_GPL(auth_domain_lookup);

struct auth_domain *auth_domain_find(char *name)
{
	struct auth_domain *hp;
	struct hlist_head *head;

	head = &auth_domain_table[hash_str(name, DN_HASHBITS)];

	rcu_read_lock();
	hlist_for_each_entry_rcu(hp, head, hash) {
		if (strcmp(hp->name, name)==0) {
			if (!kref_get_unless_zero(&hp->ref))
				hp = NULL;
			rcu_read_unlock();
			return hp;
		}
	}
	rcu_read_unlock();
	return NULL;
}
EXPORT_SYMBOL_GPL(auth_domain_find);

/**
 * auth_domain_cleanup - check that the auth_domain table is empty
 *
 * On module unload the auth_domain_table must be empty.  To make it
 * easier to catch bugs which don't clean up domains properly, we
 * warn if anything remains in the table at cleanup time.
 *
 * Note that we cannot proactively remove the domains at this stage.
 * The ->release() function might be in a module that has already been
 * unloaded.
 */

void auth_domain_cleanup(void)
{
	int h;
	struct auth_domain *hp;

	for (h = 0; h < DN_HASHMAX; h++)
		hlist_for_each_entry(hp, &auth_domain_table[h], hash)
			pr_warn("svc: domain %s still present at module unload.\n",
				hp->name);
}
