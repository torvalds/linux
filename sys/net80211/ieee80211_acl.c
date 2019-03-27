/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2008 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * IEEE 802.11 MAC ACL support.
 *
 * When this module is loaded the sender address of each auth mgt
 * frame is passed to the iac_check method and the module indicates
 * if the frame should be accepted or rejected.  If the policy is
 * set to ACL_POLICY_OPEN then all frames are accepted w/o checking
 * the address.  Otherwise, the address is looked up in the database
 * and if found the frame is either accepted (ACL_POLICY_ALLOW)
 * or rejected (ACL_POLICY_DENT).
 */
#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h> 
#include <sys/malloc.h>   
#include <sys/mbuf.h>   
#include <sys/module.h>
#include <sys/queue.h>

#include <sys/socket.h>

#include <net/if.h>
#include <net/if_media.h>
#include <net/ethernet.h>
#include <net/route.h>

#include <net80211/ieee80211_var.h>

enum {
	ACL_POLICY_OPEN		= 0,	/* open, don't check ACL's */
	ACL_POLICY_ALLOW	= 1,	/* allow traffic from MAC */
	ACL_POLICY_DENY		= 2,	/* deny traffic from MAC */
	/*
	 * NB: ACL_POLICY_RADIUS must be the same value as
	 *     IEEE80211_MACCMD_POLICY_RADIUS because of the way
	 *     acl_getpolicy() works.
	 */
	ACL_POLICY_RADIUS	= 7,	/* defer to RADIUS ACL server */
};

#define	ACL_HASHSIZE	32

struct acl {
	TAILQ_ENTRY(acl)	acl_list;
	LIST_ENTRY(acl)		acl_hash;
	uint8_t			acl_macaddr[IEEE80211_ADDR_LEN];
};
struct aclstate {
	acl_lock_t		as_lock;
	int			as_policy;
	uint32_t		as_nacls;
	TAILQ_HEAD(, acl)	as_list;	/* list of all ACL's */
	LIST_HEAD(, acl)	as_hash[ACL_HASHSIZE];
	struct ieee80211vap	*as_vap;
};

/* simple hash is enough for variation of macaddr */
#define	ACL_HASH(addr)	\
	(((const uint8_t *)(addr))[IEEE80211_ADDR_LEN - 1] % ACL_HASHSIZE)

static MALLOC_DEFINE(M_80211_ACL, "acl", "802.11 station acl");

static	int acl_free_all(struct ieee80211vap *);

/* number of references from net80211 layer */
static	int nrefs = 0;

static int
acl_attach(struct ieee80211vap *vap)
{
	struct aclstate *as;

	as = (struct aclstate *) IEEE80211_MALLOC(sizeof(struct aclstate),
		M_80211_ACL, IEEE80211_M_NOWAIT | IEEE80211_M_ZERO);
	if (as == NULL)
		return 0;
	ACL_LOCK_INIT(as, "acl");
	TAILQ_INIT(&as->as_list);
	as->as_policy = ACL_POLICY_OPEN;
	as->as_vap = vap;
	vap->iv_as = as;
	nrefs++;			/* NB: we assume caller locking */
	return 1;
}

static void
acl_detach(struct ieee80211vap *vap)
{
	struct aclstate *as = vap->iv_as;

	KASSERT(nrefs > 0, ("imbalanced attach/detach"));
	nrefs--;			/* NB: we assume caller locking */

	acl_free_all(vap);
	vap->iv_as = NULL;
	ACL_LOCK_DESTROY(as);
	IEEE80211_FREE(as, M_80211_ACL);
}

static __inline struct acl *
_find_acl(struct aclstate *as, const uint8_t *macaddr)
{
	struct acl *acl;
	int hash;

	hash = ACL_HASH(macaddr);
	LIST_FOREACH(acl, &as->as_hash[hash], acl_hash) {
		if (IEEE80211_ADDR_EQ(acl->acl_macaddr, macaddr))
			return acl;
	}
	return NULL;
}

static void
_acl_free(struct aclstate *as, struct acl *acl)
{
	ACL_LOCK_ASSERT(as);

	TAILQ_REMOVE(&as->as_list, acl, acl_list);
	LIST_REMOVE(acl, acl_hash);
	IEEE80211_FREE(acl, M_80211_ACL);
	as->as_nacls--;
}

static int
acl_check(struct ieee80211vap *vap, const struct ieee80211_frame *wh)
{
	struct aclstate *as = vap->iv_as;

	switch (as->as_policy) {
	case ACL_POLICY_OPEN:
	case ACL_POLICY_RADIUS:
		return 1;
	case ACL_POLICY_ALLOW:
		return _find_acl(as, wh->i_addr2) != NULL;
	case ACL_POLICY_DENY:
		return _find_acl(as, wh->i_addr2) == NULL;
	}
	return 0;		/* should not happen */
}

static int
acl_add(struct ieee80211vap *vap, const uint8_t mac[IEEE80211_ADDR_LEN])
{
	struct aclstate *as = vap->iv_as;
	struct acl *acl, *new;
	int hash;

	new = (struct acl *) IEEE80211_MALLOC(sizeof(struct acl),
	    M_80211_ACL, IEEE80211_M_NOWAIT | IEEE80211_M_ZERO);
	if (new == NULL) {
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_ACL,
			"ACL: add %s failed, no memory\n", ether_sprintf(mac));
		/* XXX statistic */
		return ENOMEM;
	}

	ACL_LOCK(as);
	hash = ACL_HASH(mac);
	LIST_FOREACH(acl, &as->as_hash[hash], acl_hash) {
		if (IEEE80211_ADDR_EQ(acl->acl_macaddr, mac)) {
			ACL_UNLOCK(as);
			IEEE80211_FREE(new, M_80211_ACL);
			IEEE80211_DPRINTF(vap, IEEE80211_MSG_ACL,
				"ACL: add %s failed, already present\n",
				ether_sprintf(mac));
			return EEXIST;
		}
	}
	IEEE80211_ADDR_COPY(new->acl_macaddr, mac);
	TAILQ_INSERT_TAIL(&as->as_list, new, acl_list);
	LIST_INSERT_HEAD(&as->as_hash[hash], new, acl_hash);
	as->as_nacls++;
	ACL_UNLOCK(as);

	IEEE80211_DPRINTF(vap, IEEE80211_MSG_ACL,
		"ACL: add %s\n", ether_sprintf(mac));
	return 0;
}

static int
acl_remove(struct ieee80211vap *vap, const uint8_t mac[IEEE80211_ADDR_LEN])
{
	struct aclstate *as = vap->iv_as;
	struct acl *acl;

	ACL_LOCK(as);
	acl = _find_acl(as, mac);
	if (acl != NULL)
		_acl_free(as, acl);
	ACL_UNLOCK(as);

	IEEE80211_DPRINTF(vap, IEEE80211_MSG_ACL,
		"ACL: remove %s%s\n", ether_sprintf(mac),
		acl == NULL ? ", not present" : "");

	return (acl == NULL ? ENOENT : 0);
}

static int
acl_free_all(struct ieee80211vap *vap)
{
	struct aclstate *as = vap->iv_as;
	struct acl *acl;

	IEEE80211_DPRINTF(vap, IEEE80211_MSG_ACL, "ACL: %s\n", "free all");

	ACL_LOCK(as);
	while ((acl = TAILQ_FIRST(&as->as_list)) != NULL)
		_acl_free(as, acl);
	ACL_UNLOCK(as);

	return 0;
}

static int
acl_setpolicy(struct ieee80211vap *vap, int policy)
{
	struct aclstate *as = vap->iv_as;

	IEEE80211_DPRINTF(vap, IEEE80211_MSG_ACL,
		"ACL: set policy to %u\n", policy);

	switch (policy) {
	case IEEE80211_MACCMD_POLICY_OPEN:
		as->as_policy = ACL_POLICY_OPEN;
		break;
	case IEEE80211_MACCMD_POLICY_ALLOW:
		as->as_policy = ACL_POLICY_ALLOW;
		break;
	case IEEE80211_MACCMD_POLICY_DENY:
		as->as_policy = ACL_POLICY_DENY;
		break;
	case IEEE80211_MACCMD_POLICY_RADIUS:
		as->as_policy = ACL_POLICY_RADIUS;
		break;
	default:
		return EINVAL;
	}
	return 0;
}

static int
acl_getpolicy(struct ieee80211vap *vap)
{
	struct aclstate *as = vap->iv_as;

	return as->as_policy;
}

static int
acl_setioctl(struct ieee80211vap *vap, struct ieee80211req *ireq)
{

	return EINVAL;
}

static int
acl_getioctl(struct ieee80211vap *vap, struct ieee80211req *ireq)
{
	struct aclstate *as = vap->iv_as;
	struct acl *acl;
	struct ieee80211req_maclist *ap;
	int error;
	uint32_t i, space;

	switch (ireq->i_val) {
	case IEEE80211_MACCMD_POLICY:
		ireq->i_val = as->as_policy;
		return 0;
	case IEEE80211_MACCMD_LIST:
		space = as->as_nacls * IEEE80211_ADDR_LEN;
		if (ireq->i_len == 0) {
			ireq->i_len = space;	/* return required space */
			return 0;		/* NB: must not error */
		}
		ap = (struct ieee80211req_maclist *) IEEE80211_MALLOC(space,
		    M_TEMP, IEEE80211_M_NOWAIT);
		if (ap == NULL)
			return ENOMEM;
		i = 0;
		ACL_LOCK(as);
		TAILQ_FOREACH(acl, &as->as_list, acl_list) {
			IEEE80211_ADDR_COPY(ap[i].ml_macaddr, acl->acl_macaddr);
			i++;
		}
		ACL_UNLOCK(as);
		if (ireq->i_len >= space) {
			error = copyout(ap, ireq->i_data, space);
			ireq->i_len = space;
		} else
			error = copyout(ap, ireq->i_data, ireq->i_len);
		IEEE80211_FREE(ap, M_TEMP);
		return error;
	}
	return EINVAL;
}

static const struct ieee80211_aclator mac = {
	.iac_name	= "mac",
	.iac_attach	= acl_attach,
	.iac_detach	= acl_detach,
	.iac_check	= acl_check,
	.iac_add	= acl_add,
	.iac_remove	= acl_remove,
	.iac_flush	= acl_free_all,
	.iac_setpolicy	= acl_setpolicy,
	.iac_getpolicy	= acl_getpolicy,
	.iac_setioctl	= acl_setioctl,
	.iac_getioctl	= acl_getioctl,
};
IEEE80211_ACL_MODULE(wlan_acl, mac, 1);
