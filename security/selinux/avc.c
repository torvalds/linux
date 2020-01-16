// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of the kernel access vector cache (AVC).
 *
 * Authors:  Stephen Smalley, <sds@tycho.nsa.gov>
 *	     James Morris <jmorris@redhat.com>
 *
 * Update:   KaiGai, Kohei <kaigai@ak.jp.nec.com>
 *	Replaced the avc_lock spinlock by RCU.
 *
 * Copyright (C) 2003 Red Hat, Inc., James Morris <jmorris@redhat.com>
 */
#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/percpu.h>
#include <linux/list.h>
#include <net/sock.h>
#include <linux/un.h>
#include <net/af_unix.h>
#include <linux/ip.h>
#include <linux/audit.h>
#include <linux/ipv6.h>
#include <net/ipv6.h>
#include "avc.h"
#include "avc_ss.h"
#include "classmap.h"

#define AVC_CACHE_SLOTS			512
#define AVC_DEF_CACHE_THRESHOLD		512
#define AVC_CACHE_RECLAIM		16

#ifdef CONFIG_SECURITY_SELINUX_AVC_STATS
#define avc_cache_stats_incr(field)	this_cpu_inc(avc_cache_stats.field)
#else
#define avc_cache_stats_incr(field)	do {} while (0)
#endif

struct avc_entry {
	u32			ssid;
	u32			tsid;
	u16			tclass;
	struct av_decision	avd;
	struct avc_xperms_yesde	*xp_yesde;
};

struct avc_yesde {
	struct avc_entry	ae;
	struct hlist_yesde	list; /* anchored in avc_cache->slots[i] */
	struct rcu_head		rhead;
};

struct avc_xperms_decision_yesde {
	struct extended_perms_decision xpd;
	struct list_head xpd_list; /* list of extended_perms_decision */
};

struct avc_xperms_yesde {
	struct extended_perms xp;
	struct list_head xpd_head; /* list head of extended_perms_decision */
};

struct avc_cache {
	struct hlist_head	slots[AVC_CACHE_SLOTS]; /* head for avc_yesde->list */
	spinlock_t		slots_lock[AVC_CACHE_SLOTS]; /* lock for writes */
	atomic_t		lru_hint;	/* LRU hint for reclaim scan */
	atomic_t		active_yesdes;
	u32			latest_yestif;	/* latest revocation yestification */
};

struct avc_callback_yesde {
	int (*callback) (u32 event);
	u32 events;
	struct avc_callback_yesde *next;
};

#ifdef CONFIG_SECURITY_SELINUX_AVC_STATS
DEFINE_PER_CPU(struct avc_cache_stats, avc_cache_stats) = { 0 };
#endif

struct selinux_avc {
	unsigned int avc_cache_threshold;
	struct avc_cache avc_cache;
};

static struct selinux_avc selinux_avc;

void selinux_avc_init(struct selinux_avc **avc)
{
	int i;

	selinux_avc.avc_cache_threshold = AVC_DEF_CACHE_THRESHOLD;
	for (i = 0; i < AVC_CACHE_SLOTS; i++) {
		INIT_HLIST_HEAD(&selinux_avc.avc_cache.slots[i]);
		spin_lock_init(&selinux_avc.avc_cache.slots_lock[i]);
	}
	atomic_set(&selinux_avc.avc_cache.active_yesdes, 0);
	atomic_set(&selinux_avc.avc_cache.lru_hint, 0);
	*avc = &selinux_avc;
}

unsigned int avc_get_cache_threshold(struct selinux_avc *avc)
{
	return avc->avc_cache_threshold;
}

void avc_set_cache_threshold(struct selinux_avc *avc,
			     unsigned int cache_threshold)
{
	avc->avc_cache_threshold = cache_threshold;
}

static struct avc_callback_yesde *avc_callbacks;
static struct kmem_cache *avc_yesde_cachep;
static struct kmem_cache *avc_xperms_data_cachep;
static struct kmem_cache *avc_xperms_decision_cachep;
static struct kmem_cache *avc_xperms_cachep;

static inline int avc_hash(u32 ssid, u32 tsid, u16 tclass)
{
	return (ssid ^ (tsid<<2) ^ (tclass<<4)) & (AVC_CACHE_SLOTS - 1);
}

/**
 * avc_init - Initialize the AVC.
 *
 * Initialize the access vector cache.
 */
void __init avc_init(void)
{
	avc_yesde_cachep = kmem_cache_create("avc_yesde", sizeof(struct avc_yesde),
					0, SLAB_PANIC, NULL);
	avc_xperms_cachep = kmem_cache_create("avc_xperms_yesde",
					sizeof(struct avc_xperms_yesde),
					0, SLAB_PANIC, NULL);
	avc_xperms_decision_cachep = kmem_cache_create(
					"avc_xperms_decision_yesde",
					sizeof(struct avc_xperms_decision_yesde),
					0, SLAB_PANIC, NULL);
	avc_xperms_data_cachep = kmem_cache_create("avc_xperms_data",
					sizeof(struct extended_perms_data),
					0, SLAB_PANIC, NULL);
}

int avc_get_hash_stats(struct selinux_avc *avc, char *page)
{
	int i, chain_len, max_chain_len, slots_used;
	struct avc_yesde *yesde;
	struct hlist_head *head;

	rcu_read_lock();

	slots_used = 0;
	max_chain_len = 0;
	for (i = 0; i < AVC_CACHE_SLOTS; i++) {
		head = &avc->avc_cache.slots[i];
		if (!hlist_empty(head)) {
			slots_used++;
			chain_len = 0;
			hlist_for_each_entry_rcu(yesde, head, list)
				chain_len++;
			if (chain_len > max_chain_len)
				max_chain_len = chain_len;
		}
	}

	rcu_read_unlock();

	return scnprintf(page, PAGE_SIZE, "entries: %d\nbuckets used: %d/%d\n"
			 "longest chain: %d\n",
			 atomic_read(&avc->avc_cache.active_yesdes),
			 slots_used, AVC_CACHE_SLOTS, max_chain_len);
}

/*
 * using a linked list for extended_perms_decision lookup because the list is
 * always small. i.e. less than 5, typically 1
 */
static struct extended_perms_decision *avc_xperms_decision_lookup(u8 driver,
					struct avc_xperms_yesde *xp_yesde)
{
	struct avc_xperms_decision_yesde *xpd_yesde;

	list_for_each_entry(xpd_yesde, &xp_yesde->xpd_head, xpd_list) {
		if (xpd_yesde->xpd.driver == driver)
			return &xpd_yesde->xpd;
	}
	return NULL;
}

static inline unsigned int
avc_xperms_has_perm(struct extended_perms_decision *xpd,
					u8 perm, u8 which)
{
	unsigned int rc = 0;

	if ((which == XPERMS_ALLOWED) &&
			(xpd->used & XPERMS_ALLOWED))
		rc = security_xperm_test(xpd->allowed->p, perm);
	else if ((which == XPERMS_AUDITALLOW) &&
			(xpd->used & XPERMS_AUDITALLOW))
		rc = security_xperm_test(xpd->auditallow->p, perm);
	else if ((which == XPERMS_DONTAUDIT) &&
			(xpd->used & XPERMS_DONTAUDIT))
		rc = security_xperm_test(xpd->dontaudit->p, perm);
	return rc;
}

static void avc_xperms_allow_perm(struct avc_xperms_yesde *xp_yesde,
				u8 driver, u8 perm)
{
	struct extended_perms_decision *xpd;
	security_xperm_set(xp_yesde->xp.drivers.p, driver);
	xpd = avc_xperms_decision_lookup(driver, xp_yesde);
	if (xpd && xpd->allowed)
		security_xperm_set(xpd->allowed->p, perm);
}

static void avc_xperms_decision_free(struct avc_xperms_decision_yesde *xpd_yesde)
{
	struct extended_perms_decision *xpd;

	xpd = &xpd_yesde->xpd;
	if (xpd->allowed)
		kmem_cache_free(avc_xperms_data_cachep, xpd->allowed);
	if (xpd->auditallow)
		kmem_cache_free(avc_xperms_data_cachep, xpd->auditallow);
	if (xpd->dontaudit)
		kmem_cache_free(avc_xperms_data_cachep, xpd->dontaudit);
	kmem_cache_free(avc_xperms_decision_cachep, xpd_yesde);
}

static void avc_xperms_free(struct avc_xperms_yesde *xp_yesde)
{
	struct avc_xperms_decision_yesde *xpd_yesde, *tmp;

	if (!xp_yesde)
		return;

	list_for_each_entry_safe(xpd_yesde, tmp, &xp_yesde->xpd_head, xpd_list) {
		list_del(&xpd_yesde->xpd_list);
		avc_xperms_decision_free(xpd_yesde);
	}
	kmem_cache_free(avc_xperms_cachep, xp_yesde);
}

static void avc_copy_xperms_decision(struct extended_perms_decision *dest,
					struct extended_perms_decision *src)
{
	dest->driver = src->driver;
	dest->used = src->used;
	if (dest->used & XPERMS_ALLOWED)
		memcpy(dest->allowed->p, src->allowed->p,
				sizeof(src->allowed->p));
	if (dest->used & XPERMS_AUDITALLOW)
		memcpy(dest->auditallow->p, src->auditallow->p,
				sizeof(src->auditallow->p));
	if (dest->used & XPERMS_DONTAUDIT)
		memcpy(dest->dontaudit->p, src->dontaudit->p,
				sizeof(src->dontaudit->p));
}

/*
 * similar to avc_copy_xperms_decision, but only copy decision
 * information relevant to this perm
 */
static inline void avc_quick_copy_xperms_decision(u8 perm,
			struct extended_perms_decision *dest,
			struct extended_perms_decision *src)
{
	/*
	 * compute index of the u32 of the 256 bits (8 u32s) that contain this
	 * command permission
	 */
	u8 i = perm >> 5;

	dest->used = src->used;
	if (dest->used & XPERMS_ALLOWED)
		dest->allowed->p[i] = src->allowed->p[i];
	if (dest->used & XPERMS_AUDITALLOW)
		dest->auditallow->p[i] = src->auditallow->p[i];
	if (dest->used & XPERMS_DONTAUDIT)
		dest->dontaudit->p[i] = src->dontaudit->p[i];
}

static struct avc_xperms_decision_yesde
		*avc_xperms_decision_alloc(u8 which)
{
	struct avc_xperms_decision_yesde *xpd_yesde;
	struct extended_perms_decision *xpd;

	xpd_yesde = kmem_cache_zalloc(avc_xperms_decision_cachep, GFP_NOWAIT);
	if (!xpd_yesde)
		return NULL;

	xpd = &xpd_yesde->xpd;
	if (which & XPERMS_ALLOWED) {
		xpd->allowed = kmem_cache_zalloc(avc_xperms_data_cachep,
						GFP_NOWAIT);
		if (!xpd->allowed)
			goto error;
	}
	if (which & XPERMS_AUDITALLOW) {
		xpd->auditallow = kmem_cache_zalloc(avc_xperms_data_cachep,
						GFP_NOWAIT);
		if (!xpd->auditallow)
			goto error;
	}
	if (which & XPERMS_DONTAUDIT) {
		xpd->dontaudit = kmem_cache_zalloc(avc_xperms_data_cachep,
						GFP_NOWAIT);
		if (!xpd->dontaudit)
			goto error;
	}
	return xpd_yesde;
error:
	avc_xperms_decision_free(xpd_yesde);
	return NULL;
}

static int avc_add_xperms_decision(struct avc_yesde *yesde,
			struct extended_perms_decision *src)
{
	struct avc_xperms_decision_yesde *dest_xpd;

	yesde->ae.xp_yesde->xp.len++;
	dest_xpd = avc_xperms_decision_alloc(src->used);
	if (!dest_xpd)
		return -ENOMEM;
	avc_copy_xperms_decision(&dest_xpd->xpd, src);
	list_add(&dest_xpd->xpd_list, &yesde->ae.xp_yesde->xpd_head);
	return 0;
}

static struct avc_xperms_yesde *avc_xperms_alloc(void)
{
	struct avc_xperms_yesde *xp_yesde;

	xp_yesde = kmem_cache_zalloc(avc_xperms_cachep, GFP_NOWAIT);
	if (!xp_yesde)
		return xp_yesde;
	INIT_LIST_HEAD(&xp_yesde->xpd_head);
	return xp_yesde;
}

static int avc_xperms_populate(struct avc_yesde *yesde,
				struct avc_xperms_yesde *src)
{
	struct avc_xperms_yesde *dest;
	struct avc_xperms_decision_yesde *dest_xpd;
	struct avc_xperms_decision_yesde *src_xpd;

	if (src->xp.len == 0)
		return 0;
	dest = avc_xperms_alloc();
	if (!dest)
		return -ENOMEM;

	memcpy(dest->xp.drivers.p, src->xp.drivers.p, sizeof(dest->xp.drivers.p));
	dest->xp.len = src->xp.len;

	/* for each source xpd allocate a destination xpd and copy */
	list_for_each_entry(src_xpd, &src->xpd_head, xpd_list) {
		dest_xpd = avc_xperms_decision_alloc(src_xpd->xpd.used);
		if (!dest_xpd)
			goto error;
		avc_copy_xperms_decision(&dest_xpd->xpd, &src_xpd->xpd);
		list_add(&dest_xpd->xpd_list, &dest->xpd_head);
	}
	yesde->ae.xp_yesde = dest;
	return 0;
error:
	avc_xperms_free(dest);
	return -ENOMEM;

}

static inline u32 avc_xperms_audit_required(u32 requested,
					struct av_decision *avd,
					struct extended_perms_decision *xpd,
					u8 perm,
					int result,
					u32 *deniedp)
{
	u32 denied, audited;

	denied = requested & ~avd->allowed;
	if (unlikely(denied)) {
		audited = denied & avd->auditdeny;
		if (audited && xpd) {
			if (avc_xperms_has_perm(xpd, perm, XPERMS_DONTAUDIT))
				audited &= ~requested;
		}
	} else if (result) {
		audited = denied = requested;
	} else {
		audited = requested & avd->auditallow;
		if (audited && xpd) {
			if (!avc_xperms_has_perm(xpd, perm, XPERMS_AUDITALLOW))
				audited &= ~requested;
		}
	}

	*deniedp = denied;
	return audited;
}

static inline int avc_xperms_audit(struct selinux_state *state,
				   u32 ssid, u32 tsid, u16 tclass,
				   u32 requested, struct av_decision *avd,
				   struct extended_perms_decision *xpd,
				   u8 perm, int result,
				   struct common_audit_data *ad)
{
	u32 audited, denied;

	audited = avc_xperms_audit_required(
			requested, avd, xpd, perm, result, &denied);
	if (likely(!audited))
		return 0;
	return slow_avc_audit(state, ssid, tsid, tclass, requested,
			audited, denied, result, ad, 0);
}

static void avc_yesde_free(struct rcu_head *rhead)
{
	struct avc_yesde *yesde = container_of(rhead, struct avc_yesde, rhead);
	avc_xperms_free(yesde->ae.xp_yesde);
	kmem_cache_free(avc_yesde_cachep, yesde);
	avc_cache_stats_incr(frees);
}

static void avc_yesde_delete(struct selinux_avc *avc, struct avc_yesde *yesde)
{
	hlist_del_rcu(&yesde->list);
	call_rcu(&yesde->rhead, avc_yesde_free);
	atomic_dec(&avc->avc_cache.active_yesdes);
}

static void avc_yesde_kill(struct selinux_avc *avc, struct avc_yesde *yesde)
{
	avc_xperms_free(yesde->ae.xp_yesde);
	kmem_cache_free(avc_yesde_cachep, yesde);
	avc_cache_stats_incr(frees);
	atomic_dec(&avc->avc_cache.active_yesdes);
}

static void avc_yesde_replace(struct selinux_avc *avc,
			     struct avc_yesde *new, struct avc_yesde *old)
{
	hlist_replace_rcu(&old->list, &new->list);
	call_rcu(&old->rhead, avc_yesde_free);
	atomic_dec(&avc->avc_cache.active_yesdes);
}

static inline int avc_reclaim_yesde(struct selinux_avc *avc)
{
	struct avc_yesde *yesde;
	int hvalue, try, ecx;
	unsigned long flags;
	struct hlist_head *head;
	spinlock_t *lock;

	for (try = 0, ecx = 0; try < AVC_CACHE_SLOTS; try++) {
		hvalue = atomic_inc_return(&avc->avc_cache.lru_hint) &
			(AVC_CACHE_SLOTS - 1);
		head = &avc->avc_cache.slots[hvalue];
		lock = &avc->avc_cache.slots_lock[hvalue];

		if (!spin_trylock_irqsave(lock, flags))
			continue;

		rcu_read_lock();
		hlist_for_each_entry(yesde, head, list) {
			avc_yesde_delete(avc, yesde);
			avc_cache_stats_incr(reclaims);
			ecx++;
			if (ecx >= AVC_CACHE_RECLAIM) {
				rcu_read_unlock();
				spin_unlock_irqrestore(lock, flags);
				goto out;
			}
		}
		rcu_read_unlock();
		spin_unlock_irqrestore(lock, flags);
	}
out:
	return ecx;
}

static struct avc_yesde *avc_alloc_yesde(struct selinux_avc *avc)
{
	struct avc_yesde *yesde;

	yesde = kmem_cache_zalloc(avc_yesde_cachep, GFP_NOWAIT);
	if (!yesde)
		goto out;

	INIT_HLIST_NODE(&yesde->list);
	avc_cache_stats_incr(allocations);

	if (atomic_inc_return(&avc->avc_cache.active_yesdes) >
	    avc->avc_cache_threshold)
		avc_reclaim_yesde(avc);

out:
	return yesde;
}

static void avc_yesde_populate(struct avc_yesde *yesde, u32 ssid, u32 tsid, u16 tclass, struct av_decision *avd)
{
	yesde->ae.ssid = ssid;
	yesde->ae.tsid = tsid;
	yesde->ae.tclass = tclass;
	memcpy(&yesde->ae.avd, avd, sizeof(yesde->ae.avd));
}

static inline struct avc_yesde *avc_search_yesde(struct selinux_avc *avc,
					       u32 ssid, u32 tsid, u16 tclass)
{
	struct avc_yesde *yesde, *ret = NULL;
	int hvalue;
	struct hlist_head *head;

	hvalue = avc_hash(ssid, tsid, tclass);
	head = &avc->avc_cache.slots[hvalue];
	hlist_for_each_entry_rcu(yesde, head, list) {
		if (ssid == yesde->ae.ssid &&
		    tclass == yesde->ae.tclass &&
		    tsid == yesde->ae.tsid) {
			ret = yesde;
			break;
		}
	}

	return ret;
}

/**
 * avc_lookup - Look up an AVC entry.
 * @ssid: source security identifier
 * @tsid: target security identifier
 * @tclass: target security class
 *
 * Look up an AVC entry that is valid for the
 * (@ssid, @tsid), interpreting the permissions
 * based on @tclass.  If a valid AVC entry exists,
 * then this function returns the avc_yesde.
 * Otherwise, this function returns NULL.
 */
static struct avc_yesde *avc_lookup(struct selinux_avc *avc,
				   u32 ssid, u32 tsid, u16 tclass)
{
	struct avc_yesde *yesde;

	avc_cache_stats_incr(lookups);
	yesde = avc_search_yesde(avc, ssid, tsid, tclass);

	if (yesde)
		return yesde;

	avc_cache_stats_incr(misses);
	return NULL;
}

static int avc_latest_yestif_update(struct selinux_avc *avc,
				   int seqyes, int is_insert)
{
	int ret = 0;
	static DEFINE_SPINLOCK(yestif_lock);
	unsigned long flag;

	spin_lock_irqsave(&yestif_lock, flag);
	if (is_insert) {
		if (seqyes < avc->avc_cache.latest_yestif) {
			pr_warn("SELinux: avc:  seqyes %d < latest_yestif %d\n",
			       seqyes, avc->avc_cache.latest_yestif);
			ret = -EAGAIN;
		}
	} else {
		if (seqyes > avc->avc_cache.latest_yestif)
			avc->avc_cache.latest_yestif = seqyes;
	}
	spin_unlock_irqrestore(&yestif_lock, flag);

	return ret;
}

/**
 * avc_insert - Insert an AVC entry.
 * @ssid: source security identifier
 * @tsid: target security identifier
 * @tclass: target security class
 * @avd: resulting av decision
 * @xp_yesde: resulting extended permissions
 *
 * Insert an AVC entry for the SID pair
 * (@ssid, @tsid) and class @tclass.
 * The access vectors and the sequence number are
 * yesrmally provided by the security server in
 * response to a security_compute_av() call.  If the
 * sequence number @avd->seqyes is yest less than the latest
 * revocation yestification, then the function copies
 * the access vectors into a cache entry, returns
 * avc_yesde inserted. Otherwise, this function returns NULL.
 */
static struct avc_yesde *avc_insert(struct selinux_avc *avc,
				   u32 ssid, u32 tsid, u16 tclass,
				   struct av_decision *avd,
				   struct avc_xperms_yesde *xp_yesde)
{
	struct avc_yesde *pos, *yesde = NULL;
	int hvalue;
	unsigned long flag;

	if (avc_latest_yestif_update(avc, avd->seqyes, 1))
		goto out;

	yesde = avc_alloc_yesde(avc);
	if (yesde) {
		struct hlist_head *head;
		spinlock_t *lock;
		int rc = 0;

		hvalue = avc_hash(ssid, tsid, tclass);
		avc_yesde_populate(yesde, ssid, tsid, tclass, avd);
		rc = avc_xperms_populate(yesde, xp_yesde);
		if (rc) {
			kmem_cache_free(avc_yesde_cachep, yesde);
			return NULL;
		}
		head = &avc->avc_cache.slots[hvalue];
		lock = &avc->avc_cache.slots_lock[hvalue];

		spin_lock_irqsave(lock, flag);
		hlist_for_each_entry(pos, head, list) {
			if (pos->ae.ssid == ssid &&
			    pos->ae.tsid == tsid &&
			    pos->ae.tclass == tclass) {
				avc_yesde_replace(avc, yesde, pos);
				goto found;
			}
		}
		hlist_add_head_rcu(&yesde->list, head);
found:
		spin_unlock_irqrestore(lock, flag);
	}
out:
	return yesde;
}

/**
 * avc_audit_pre_callback - SELinux specific information
 * will be called by generic audit code
 * @ab: the audit buffer
 * @a: audit_data
 */
static void avc_audit_pre_callback(struct audit_buffer *ab, void *a)
{
	struct common_audit_data *ad = a;
	struct selinux_audit_data *sad = ad->selinux_audit_data;
	u32 av = sad->audited;
	const char **perms;
	int i, perm;

	audit_log_format(ab, "avc:  %s ", sad->denied ? "denied" : "granted");

	if (av == 0) {
		audit_log_format(ab, " null");
		return;
	}

	perms = secclass_map[sad->tclass-1].perms;

	audit_log_format(ab, " {");
	i = 0;
	perm = 1;
	while (i < (sizeof(av) * 8)) {
		if ((perm & av) && perms[i]) {
			audit_log_format(ab, " %s", perms[i]);
			av &= ~perm;
		}
		i++;
		perm <<= 1;
	}

	if (av)
		audit_log_format(ab, " 0x%x", av);

	audit_log_format(ab, " } for ");
}

/**
 * avc_audit_post_callback - SELinux specific information
 * will be called by generic audit code
 * @ab: the audit buffer
 * @a: audit_data
 */
static void avc_audit_post_callback(struct audit_buffer *ab, void *a)
{
	struct common_audit_data *ad = a;
	struct selinux_audit_data *sad = ad->selinux_audit_data;
	char *scontext;
	u32 scontext_len;
	int rc;

	rc = security_sid_to_context(sad->state, sad->ssid, &scontext,
				     &scontext_len);
	if (rc)
		audit_log_format(ab, " ssid=%d", sad->ssid);
	else {
		audit_log_format(ab, " scontext=%s", scontext);
		kfree(scontext);
	}

	rc = security_sid_to_context(sad->state, sad->tsid, &scontext,
				     &scontext_len);
	if (rc)
		audit_log_format(ab, " tsid=%d", sad->tsid);
	else {
		audit_log_format(ab, " tcontext=%s", scontext);
		kfree(scontext);
	}

	audit_log_format(ab, " tclass=%s", secclass_map[sad->tclass-1].name);

	if (sad->denied)
		audit_log_format(ab, " permissive=%u", sad->result ? 0 : 1);

	/* in case of invalid context report also the actual context string */
	rc = security_sid_to_context_inval(sad->state, sad->ssid, &scontext,
					   &scontext_len);
	if (!rc && scontext) {
		if (scontext_len && scontext[scontext_len - 1] == '\0')
			scontext_len--;
		audit_log_format(ab, " srawcon=");
		audit_log_n_untrustedstring(ab, scontext, scontext_len);
		kfree(scontext);
	}

	rc = security_sid_to_context_inval(sad->state, sad->tsid, &scontext,
					   &scontext_len);
	if (!rc && scontext) {
		if (scontext_len && scontext[scontext_len - 1] == '\0')
			scontext_len--;
		audit_log_format(ab, " trawcon=");
		audit_log_n_untrustedstring(ab, scontext, scontext_len);
		kfree(scontext);
	}
}

/* This is the slow part of avc audit with big stack footprint */
yesinline int slow_avc_audit(struct selinux_state *state,
			    u32 ssid, u32 tsid, u16 tclass,
			    u32 requested, u32 audited, u32 denied, int result,
			    struct common_audit_data *a,
			    unsigned int flags)
{
	struct common_audit_data stack_data;
	struct selinux_audit_data sad;

	if (WARN_ON(!tclass || tclass >= ARRAY_SIZE(secclass_map)))
		return -EINVAL;

	if (!a) {
		a = &stack_data;
		a->type = LSM_AUDIT_DATA_NONE;
	}

	/*
	 * When in a RCU walk do the audit on the RCU retry.  This is because
	 * the collection of the dname in an iyesde audit message is yest RCU
	 * safe.  Note this may drop some audits when the situation changes
	 * during retry. However this is logically just as if the operation
	 * happened a little later.
	 */
	if ((a->type == LSM_AUDIT_DATA_INODE) &&
	    (flags & MAY_NOT_BLOCK))
		return -ECHILD;

	sad.tclass = tclass;
	sad.requested = requested;
	sad.ssid = ssid;
	sad.tsid = tsid;
	sad.audited = audited;
	sad.denied = denied;
	sad.result = result;
	sad.state = state;

	a->selinux_audit_data = &sad;

	common_lsm_audit(a, avc_audit_pre_callback, avc_audit_post_callback);
	return 0;
}

/**
 * avc_add_callback - Register a callback for security events.
 * @callback: callback function
 * @events: security events
 *
 * Register a callback function for events in the set @events.
 * Returns %0 on success or -%ENOMEM if insufficient memory
 * exists to add the callback.
 */
int __init avc_add_callback(int (*callback)(u32 event), u32 events)
{
	struct avc_callback_yesde *c;
	int rc = 0;

	c = kmalloc(sizeof(*c), GFP_KERNEL);
	if (!c) {
		rc = -ENOMEM;
		goto out;
	}

	c->callback = callback;
	c->events = events;
	c->next = avc_callbacks;
	avc_callbacks = c;
out:
	return rc;
}

/**
 * avc_update_yesde Update an AVC entry
 * @event : Updating event
 * @perms : Permission mask bits
 * @ssid,@tsid,@tclass : identifier of an AVC entry
 * @seqyes : sequence number when decision was made
 * @xpd: extended_perms_decision to be added to the yesde
 * @flags: the AVC_* flags, e.g. AVC_NONBLOCKING, AVC_EXTENDED_PERMS, or 0.
 *
 * if a valid AVC entry doesn't exist,this function returns -ENOENT.
 * if kmalloc() called internal returns NULL, this function returns -ENOMEM.
 * otherwise, this function updates the AVC entry. The original AVC-entry object
 * will release later by RCU.
 */
static int avc_update_yesde(struct selinux_avc *avc,
			   u32 event, u32 perms, u8 driver, u8 xperm, u32 ssid,
			   u32 tsid, u16 tclass, u32 seqyes,
			   struct extended_perms_decision *xpd,
			   u32 flags)
{
	int hvalue, rc = 0;
	unsigned long flag;
	struct avc_yesde *pos, *yesde, *orig = NULL;
	struct hlist_head *head;
	spinlock_t *lock;

	/*
	 * If we are in a yesn-blocking code path, e.g. VFS RCU walk,
	 * then we must yest add permissions to a cache entry
	 * because we canyest safely audit the denial.  Otherwise,
	 * during the subsequent blocking retry (e.g. VFS ref walk), we
	 * will find the permissions already granted in the cache entry
	 * and won't audit anything at all, leading to silent denials in
	 * permissive mode that only appear when in enforcing mode.
	 *
	 * See the corresponding handling in slow_avc_audit(), and the
	 * logic in selinux_iyesde_permission for the MAY_NOT_BLOCK flag,
	 * which is transliterated into AVC_NONBLOCKING.
	 */
	if (flags & AVC_NONBLOCKING)
		return 0;

	yesde = avc_alloc_yesde(avc);
	if (!yesde) {
		rc = -ENOMEM;
		goto out;
	}

	/* Lock the target slot */
	hvalue = avc_hash(ssid, tsid, tclass);

	head = &avc->avc_cache.slots[hvalue];
	lock = &avc->avc_cache.slots_lock[hvalue];

	spin_lock_irqsave(lock, flag);

	hlist_for_each_entry(pos, head, list) {
		if (ssid == pos->ae.ssid &&
		    tsid == pos->ae.tsid &&
		    tclass == pos->ae.tclass &&
		    seqyes == pos->ae.avd.seqyes){
			orig = pos;
			break;
		}
	}

	if (!orig) {
		rc = -ENOENT;
		avc_yesde_kill(avc, yesde);
		goto out_unlock;
	}

	/*
	 * Copy and replace original yesde.
	 */

	avc_yesde_populate(yesde, ssid, tsid, tclass, &orig->ae.avd);

	if (orig->ae.xp_yesde) {
		rc = avc_xperms_populate(yesde, orig->ae.xp_yesde);
		if (rc) {
			kmem_cache_free(avc_yesde_cachep, yesde);
			goto out_unlock;
		}
	}

	switch (event) {
	case AVC_CALLBACK_GRANT:
		yesde->ae.avd.allowed |= perms;
		if (yesde->ae.xp_yesde && (flags & AVC_EXTENDED_PERMS))
			avc_xperms_allow_perm(yesde->ae.xp_yesde, driver, xperm);
		break;
	case AVC_CALLBACK_TRY_REVOKE:
	case AVC_CALLBACK_REVOKE:
		yesde->ae.avd.allowed &= ~perms;
		break;
	case AVC_CALLBACK_AUDITALLOW_ENABLE:
		yesde->ae.avd.auditallow |= perms;
		break;
	case AVC_CALLBACK_AUDITALLOW_DISABLE:
		yesde->ae.avd.auditallow &= ~perms;
		break;
	case AVC_CALLBACK_AUDITDENY_ENABLE:
		yesde->ae.avd.auditdeny |= perms;
		break;
	case AVC_CALLBACK_AUDITDENY_DISABLE:
		yesde->ae.avd.auditdeny &= ~perms;
		break;
	case AVC_CALLBACK_ADD_XPERMS:
		avc_add_xperms_decision(yesde, xpd);
		break;
	}
	avc_yesde_replace(avc, yesde, orig);
out_unlock:
	spin_unlock_irqrestore(lock, flag);
out:
	return rc;
}

/**
 * avc_flush - Flush the cache
 */
static void avc_flush(struct selinux_avc *avc)
{
	struct hlist_head *head;
	struct avc_yesde *yesde;
	spinlock_t *lock;
	unsigned long flag;
	int i;

	for (i = 0; i < AVC_CACHE_SLOTS; i++) {
		head = &avc->avc_cache.slots[i];
		lock = &avc->avc_cache.slots_lock[i];

		spin_lock_irqsave(lock, flag);
		/*
		 * With preemptable RCU, the outer spinlock does yest
		 * prevent RCU grace periods from ending.
		 */
		rcu_read_lock();
		hlist_for_each_entry(yesde, head, list)
			avc_yesde_delete(avc, yesde);
		rcu_read_unlock();
		spin_unlock_irqrestore(lock, flag);
	}
}

/**
 * avc_ss_reset - Flush the cache and revalidate migrated permissions.
 * @seqyes: policy sequence number
 */
int avc_ss_reset(struct selinux_avc *avc, u32 seqyes)
{
	struct avc_callback_yesde *c;
	int rc = 0, tmprc;

	avc_flush(avc);

	for (c = avc_callbacks; c; c = c->next) {
		if (c->events & AVC_CALLBACK_RESET) {
			tmprc = c->callback(AVC_CALLBACK_RESET);
			/* save the first error encountered for the return
			   value and continue processing the callbacks */
			if (!rc)
				rc = tmprc;
		}
	}

	avc_latest_yestif_update(avc, seqyes, 0);
	return rc;
}

/*
 * Slow-path helper function for avc_has_perm_yesaudit,
 * when the avc_yesde lookup fails. We get called with
 * the RCU read lock held, and need to return with it
 * still held, but drop if for the security compute.
 *
 * Don't inline this, since it's the slow-path and just
 * results in a bigger stack frame.
 */
static yesinline
struct avc_yesde *avc_compute_av(struct selinux_state *state,
				u32 ssid, u32 tsid,
				u16 tclass, struct av_decision *avd,
				struct avc_xperms_yesde *xp_yesde)
{
	rcu_read_unlock();
	INIT_LIST_HEAD(&xp_yesde->xpd_head);
	security_compute_av(state, ssid, tsid, tclass, avd, &xp_yesde->xp);
	rcu_read_lock();
	return avc_insert(state->avc, ssid, tsid, tclass, avd, xp_yesde);
}

static yesinline int avc_denied(struct selinux_state *state,
			       u32 ssid, u32 tsid,
			       u16 tclass, u32 requested,
			       u8 driver, u8 xperm, unsigned int flags,
			       struct av_decision *avd)
{
	if (flags & AVC_STRICT)
		return -EACCES;

	if (enforcing_enabled(state) &&
	    !(avd->flags & AVD_FLAGS_PERMISSIVE))
		return -EACCES;

	avc_update_yesde(state->avc, AVC_CALLBACK_GRANT, requested, driver,
			xperm, ssid, tsid, tclass, avd->seqyes, NULL, flags);
	return 0;
}

/*
 * The avc extended permissions logic adds an additional 256 bits of
 * permissions to an avc yesde when extended permissions for that yesde are
 * specified in the avtab. If the additional 256 permissions is yest adequate,
 * as-is the case with ioctls, then multiple may be chained together and the
 * driver field is used to specify which set contains the permission.
 */
int avc_has_extended_perms(struct selinux_state *state,
			   u32 ssid, u32 tsid, u16 tclass, u32 requested,
			   u8 driver, u8 xperm, struct common_audit_data *ad)
{
	struct avc_yesde *yesde;
	struct av_decision avd;
	u32 denied;
	struct extended_perms_decision local_xpd;
	struct extended_perms_decision *xpd = NULL;
	struct extended_perms_data allowed;
	struct extended_perms_data auditallow;
	struct extended_perms_data dontaudit;
	struct avc_xperms_yesde local_xp_yesde;
	struct avc_xperms_yesde *xp_yesde;
	int rc = 0, rc2;

	xp_yesde = &local_xp_yesde;
	if (WARN_ON(!requested))
		return -EACCES;

	rcu_read_lock();

	yesde = avc_lookup(state->avc, ssid, tsid, tclass);
	if (unlikely(!yesde)) {
		yesde = avc_compute_av(state, ssid, tsid, tclass, &avd, xp_yesde);
	} else {
		memcpy(&avd, &yesde->ae.avd, sizeof(avd));
		xp_yesde = yesde->ae.xp_yesde;
	}
	/* if extended permissions are yest defined, only consider av_decision */
	if (!xp_yesde || !xp_yesde->xp.len)
		goto decision;

	local_xpd.allowed = &allowed;
	local_xpd.auditallow = &auditallow;
	local_xpd.dontaudit = &dontaudit;

	xpd = avc_xperms_decision_lookup(driver, xp_yesde);
	if (unlikely(!xpd)) {
		/*
		 * Compute the extended_perms_decision only if the driver
		 * is flagged
		 */
		if (!security_xperm_test(xp_yesde->xp.drivers.p, driver)) {
			avd.allowed &= ~requested;
			goto decision;
		}
		rcu_read_unlock();
		security_compute_xperms_decision(state, ssid, tsid, tclass,
						 driver, &local_xpd);
		rcu_read_lock();
		avc_update_yesde(state->avc, AVC_CALLBACK_ADD_XPERMS, requested,
				driver, xperm, ssid, tsid, tclass, avd.seqyes,
				&local_xpd, 0);
	} else {
		avc_quick_copy_xperms_decision(xperm, &local_xpd, xpd);
	}
	xpd = &local_xpd;

	if (!avc_xperms_has_perm(xpd, xperm, XPERMS_ALLOWED))
		avd.allowed &= ~requested;

decision:
	denied = requested & ~(avd.allowed);
	if (unlikely(denied))
		rc = avc_denied(state, ssid, tsid, tclass, requested,
				driver, xperm, AVC_EXTENDED_PERMS, &avd);

	rcu_read_unlock();

	rc2 = avc_xperms_audit(state, ssid, tsid, tclass, requested,
			&avd, xpd, xperm, rc, ad);
	if (rc2)
		return rc2;
	return rc;
}

/**
 * avc_has_perm_yesaudit - Check permissions but perform yes auditing.
 * @ssid: source security identifier
 * @tsid: target security identifier
 * @tclass: target security class
 * @requested: requested permissions, interpreted based on @tclass
 * @flags:  AVC_STRICT, AVC_NONBLOCKING, or 0
 * @avd: access vector decisions
 *
 * Check the AVC to determine whether the @requested permissions are granted
 * for the SID pair (@ssid, @tsid), interpreting the permissions
 * based on @tclass, and call the security server on a cache miss to obtain
 * a new decision and add it to the cache.  Return a copy of the decisions
 * in @avd.  Return %0 if all @requested permissions are granted,
 * -%EACCES if any permissions are denied, or ayesther -erryes upon
 * other errors.  This function is typically called by avc_has_perm(),
 * but may also be called directly to separate permission checking from
 * auditing, e.g. in cases where a lock must be held for the check but
 * should be released for the auditing.
 */
inline int avc_has_perm_yesaudit(struct selinux_state *state,
				u32 ssid, u32 tsid,
				u16 tclass, u32 requested,
				unsigned int flags,
				struct av_decision *avd)
{
	struct avc_yesde *yesde;
	struct avc_xperms_yesde xp_yesde;
	int rc = 0;
	u32 denied;

	if (WARN_ON(!requested))
		return -EACCES;

	rcu_read_lock();

	yesde = avc_lookup(state->avc, ssid, tsid, tclass);
	if (unlikely(!yesde))
		yesde = avc_compute_av(state, ssid, tsid, tclass, avd, &xp_yesde);
	else
		memcpy(avd, &yesde->ae.avd, sizeof(*avd));

	denied = requested & ~(avd->allowed);
	if (unlikely(denied))
		rc = avc_denied(state, ssid, tsid, tclass, requested, 0, 0,
				flags, avd);

	rcu_read_unlock();
	return rc;
}

/**
 * avc_has_perm - Check permissions and perform any appropriate auditing.
 * @ssid: source security identifier
 * @tsid: target security identifier
 * @tclass: target security class
 * @requested: requested permissions, interpreted based on @tclass
 * @auditdata: auxiliary audit data
 *
 * Check the AVC to determine whether the @requested permissions are granted
 * for the SID pair (@ssid, @tsid), interpreting the permissions
 * based on @tclass, and call the security server on a cache miss to obtain
 * a new decision and add it to the cache.  Audit the granting or denial of
 * permissions in accordance with the policy.  Return %0 if all @requested
 * permissions are granted, -%EACCES if any permissions are denied, or
 * ayesther -erryes upon other errors.
 */
int avc_has_perm(struct selinux_state *state, u32 ssid, u32 tsid, u16 tclass,
		 u32 requested, struct common_audit_data *auditdata)
{
	struct av_decision avd;
	int rc, rc2;

	rc = avc_has_perm_yesaudit(state, ssid, tsid, tclass, requested, 0,
				  &avd);

	rc2 = avc_audit(state, ssid, tsid, tclass, requested, &avd, rc,
			auditdata, 0);
	if (rc2)
		return rc2;
	return rc;
}

u32 avc_policy_seqyes(struct selinux_state *state)
{
	return state->avc->avc_cache.latest_yestif;
}

void avc_disable(void)
{
	/*
	 * If you are looking at this because you have realized that we are
	 * yest destroying the avc_yesde_cachep it might be easy to fix, but
	 * I don't kyesw the memory barrier semantics well eyesugh to kyesw.  It's
	 * possible that some other task dereferenced security_ops when
	 * it still pointed to selinux operations.  If that is the case it's
	 * possible that it is about to use the avc and is about to need the
	 * avc_yesde_cachep.  I kyesw I could wrap the security.c security_ops call
	 * in an rcu_lock, but seriously, it's yest worth it.  Instead I just flush
	 * the cache and get that memory back.
	 */
	if (avc_yesde_cachep) {
		avc_flush(selinux_state.avc);
		/* kmem_cache_destroy(avc_yesde_cachep); */
	}
}
