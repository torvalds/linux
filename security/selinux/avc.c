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
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License version 2,
 *	as published by the Free Software Foundation.
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
	struct avc_xperms_node	*xp_node;
};

struct avc_node {
	struct avc_entry	ae;
	struct hlist_node	list; /* anchored in avc_cache->slots[i] */
	struct rcu_head		rhead;
};

struct avc_xperms_decision_node {
	struct extended_perms_decision xpd;
	struct list_head xpd_list; /* list of extended_perms_decision */
};

struct avc_xperms_node {
	struct extended_perms xp;
	struct list_head xpd_head; /* list head of extended_perms_decision */
};

struct avc_cache {
	struct hlist_head	slots[AVC_CACHE_SLOTS]; /* head for avc_node->list */
	spinlock_t		slots_lock[AVC_CACHE_SLOTS]; /* lock for writes */
	atomic_t		lru_hint;	/* LRU hint for reclaim scan */
	atomic_t		active_nodes;
	u32			latest_notif;	/* latest revocation notification */
};

struct avc_callback_node {
	int (*callback) (u32 event);
	u32 events;
	struct avc_callback_node *next;
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
	atomic_set(&selinux_avc.avc_cache.active_nodes, 0);
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

static struct avc_callback_node *avc_callbacks;
static struct kmem_cache *avc_node_cachep;
static struct kmem_cache *avc_xperms_data_cachep;
static struct kmem_cache *avc_xperms_decision_cachep;
static struct kmem_cache *avc_xperms_cachep;

static inline int avc_hash(u32 ssid, u32 tsid, u16 tclass)
{
	return (ssid ^ (tsid<<2) ^ (tclass<<4)) & (AVC_CACHE_SLOTS - 1);
}

/**
 * avc_dump_av - Display an access vector in human-readable form.
 * @tclass: target security class
 * @av: access vector
 */
static void avc_dump_av(struct audit_buffer *ab, u16 tclass, u32 av)
{
	const char **perms;
	int i, perm;

	if (av == 0) {
		audit_log_format(ab, " null");
		return;
	}

	BUG_ON(!tclass || tclass >= ARRAY_SIZE(secclass_map));
	perms = secclass_map[tclass-1].perms;

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

	audit_log_format(ab, " }");
}

/**
 * avc_dump_query - Display a SID pair and a class in human-readable form.
 * @ssid: source security identifier
 * @tsid: target security identifier
 * @tclass: target security class
 */
static void avc_dump_query(struct audit_buffer *ab, struct selinux_state *state,
			   u32 ssid, u32 tsid, u16 tclass)
{
	int rc;
	char *scontext;
	u32 scontext_len;

	rc = security_sid_to_context(state, ssid, &scontext, &scontext_len);
	if (rc)
		audit_log_format(ab, "ssid=%d", ssid);
	else {
		audit_log_format(ab, "scontext=%s", scontext);
		kfree(scontext);
	}

	rc = security_sid_to_context(state, tsid, &scontext, &scontext_len);
	if (rc)
		audit_log_format(ab, " tsid=%d", tsid);
	else {
		audit_log_format(ab, " tcontext=%s", scontext);
		kfree(scontext);
	}

	BUG_ON(!tclass || tclass >= ARRAY_SIZE(secclass_map));
	audit_log_format(ab, " tclass=%s", secclass_map[tclass-1].name);
}

/**
 * avc_init - Initialize the AVC.
 *
 * Initialize the access vector cache.
 */
void __init avc_init(void)
{
	avc_node_cachep = kmem_cache_create("avc_node", sizeof(struct avc_node),
					0, SLAB_PANIC, NULL);
	avc_xperms_cachep = kmem_cache_create("avc_xperms_node",
					sizeof(struct avc_xperms_node),
					0, SLAB_PANIC, NULL);
	avc_xperms_decision_cachep = kmem_cache_create(
					"avc_xperms_decision_node",
					sizeof(struct avc_xperms_decision_node),
					0, SLAB_PANIC, NULL);
	avc_xperms_data_cachep = kmem_cache_create("avc_xperms_data",
					sizeof(struct extended_perms_data),
					0, SLAB_PANIC, NULL);
}

int avc_get_hash_stats(struct selinux_avc *avc, char *page)
{
	int i, chain_len, max_chain_len, slots_used;
	struct avc_node *node;
	struct hlist_head *head;

	rcu_read_lock();

	slots_used = 0;
	max_chain_len = 0;
	for (i = 0; i < AVC_CACHE_SLOTS; i++) {
		head = &avc->avc_cache.slots[i];
		if (!hlist_empty(head)) {
			slots_used++;
			chain_len = 0;
			hlist_for_each_entry_rcu(node, head, list)
				chain_len++;
			if (chain_len > max_chain_len)
				max_chain_len = chain_len;
		}
	}

	rcu_read_unlock();

	return scnprintf(page, PAGE_SIZE, "entries: %d\nbuckets used: %d/%d\n"
			 "longest chain: %d\n",
			 atomic_read(&avc->avc_cache.active_nodes),
			 slots_used, AVC_CACHE_SLOTS, max_chain_len);
}

/*
 * using a linked list for extended_perms_decision lookup because the list is
 * always small. i.e. less than 5, typically 1
 */
static struct extended_perms_decision *avc_xperms_decision_lookup(u8 driver,
					struct avc_xperms_node *xp_node)
{
	struct avc_xperms_decision_node *xpd_node;

	list_for_each_entry(xpd_node, &xp_node->xpd_head, xpd_list) {
		if (xpd_node->xpd.driver == driver)
			return &xpd_node->xpd;
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

static void avc_xperms_allow_perm(struct avc_xperms_node *xp_node,
				u8 driver, u8 perm)
{
	struct extended_perms_decision *xpd;
	security_xperm_set(xp_node->xp.drivers.p, driver);
	xpd = avc_xperms_decision_lookup(driver, xp_node);
	if (xpd && xpd->allowed)
		security_xperm_set(xpd->allowed->p, perm);
}

static void avc_xperms_decision_free(struct avc_xperms_decision_node *xpd_node)
{
	struct extended_perms_decision *xpd;

	xpd = &xpd_node->xpd;
	if (xpd->allowed)
		kmem_cache_free(avc_xperms_data_cachep, xpd->allowed);
	if (xpd->auditallow)
		kmem_cache_free(avc_xperms_data_cachep, xpd->auditallow);
	if (xpd->dontaudit)
		kmem_cache_free(avc_xperms_data_cachep, xpd->dontaudit);
	kmem_cache_free(avc_xperms_decision_cachep, xpd_node);
}

static void avc_xperms_free(struct avc_xperms_node *xp_node)
{
	struct avc_xperms_decision_node *xpd_node, *tmp;

	if (!xp_node)
		return;

	list_for_each_entry_safe(xpd_node, tmp, &xp_node->xpd_head, xpd_list) {
		list_del(&xpd_node->xpd_list);
		avc_xperms_decision_free(xpd_node);
	}
	kmem_cache_free(avc_xperms_cachep, xp_node);
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

static struct avc_xperms_decision_node
		*avc_xperms_decision_alloc(u8 which)
{
	struct avc_xperms_decision_node *xpd_node;
	struct extended_perms_decision *xpd;

	xpd_node = kmem_cache_zalloc(avc_xperms_decision_cachep, GFP_NOWAIT);
	if (!xpd_node)
		return NULL;

	xpd = &xpd_node->xpd;
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
	return xpd_node;
error:
	avc_xperms_decision_free(xpd_node);
	return NULL;
}

static int avc_add_xperms_decision(struct avc_node *node,
			struct extended_perms_decision *src)
{
	struct avc_xperms_decision_node *dest_xpd;

	node->ae.xp_node->xp.len++;
	dest_xpd = avc_xperms_decision_alloc(src->used);
	if (!dest_xpd)
		return -ENOMEM;
	avc_copy_xperms_decision(&dest_xpd->xpd, src);
	list_add(&dest_xpd->xpd_list, &node->ae.xp_node->xpd_head);
	return 0;
}

static struct avc_xperms_node *avc_xperms_alloc(void)
{
	struct avc_xperms_node *xp_node;

	xp_node = kmem_cache_zalloc(avc_xperms_cachep, GFP_NOWAIT);
	if (!xp_node)
		return xp_node;
	INIT_LIST_HEAD(&xp_node->xpd_head);
	return xp_node;
}

static int avc_xperms_populate(struct avc_node *node,
				struct avc_xperms_node *src)
{
	struct avc_xperms_node *dest;
	struct avc_xperms_decision_node *dest_xpd;
	struct avc_xperms_decision_node *src_xpd;

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
	node->ae.xp_node = dest;
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
			audited, denied, result, ad);
}

static void avc_node_free(struct rcu_head *rhead)
{
	struct avc_node *node = container_of(rhead, struct avc_node, rhead);
	avc_xperms_free(node->ae.xp_node);
	kmem_cache_free(avc_node_cachep, node);
	avc_cache_stats_incr(frees);
}

static void avc_node_delete(struct selinux_avc *avc, struct avc_node *node)
{
	hlist_del_rcu(&node->list);
	call_rcu(&node->rhead, avc_node_free);
	atomic_dec(&avc->avc_cache.active_nodes);
}

static void avc_node_kill(struct selinux_avc *avc, struct avc_node *node)
{
	avc_xperms_free(node->ae.xp_node);
	kmem_cache_free(avc_node_cachep, node);
	avc_cache_stats_incr(frees);
	atomic_dec(&avc->avc_cache.active_nodes);
}

static void avc_node_replace(struct selinux_avc *avc,
			     struct avc_node *new, struct avc_node *old)
{
	hlist_replace_rcu(&old->list, &new->list);
	call_rcu(&old->rhead, avc_node_free);
	atomic_dec(&avc->avc_cache.active_nodes);
}

static inline int avc_reclaim_node(struct selinux_avc *avc)
{
	struct avc_node *node;
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
		hlist_for_each_entry(node, head, list) {
			avc_node_delete(avc, node);
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

static struct avc_node *avc_alloc_node(struct selinux_avc *avc)
{
	struct avc_node *node;

	node = kmem_cache_zalloc(avc_node_cachep, GFP_NOWAIT);
	if (!node)
		goto out;

	INIT_HLIST_NODE(&node->list);
	avc_cache_stats_incr(allocations);

	if (atomic_inc_return(&avc->avc_cache.active_nodes) >
	    avc->avc_cache_threshold)
		avc_reclaim_node(avc);

out:
	return node;
}

static void avc_node_populate(struct avc_node *node, u32 ssid, u32 tsid, u16 tclass, struct av_decision *avd)
{
	node->ae.ssid = ssid;
	node->ae.tsid = tsid;
	node->ae.tclass = tclass;
	memcpy(&node->ae.avd, avd, sizeof(node->ae.avd));
}

static inline struct avc_node *avc_search_node(struct selinux_avc *avc,
					       u32 ssid, u32 tsid, u16 tclass)
{
	struct avc_node *node, *ret = NULL;
	int hvalue;
	struct hlist_head *head;

	hvalue = avc_hash(ssid, tsid, tclass);
	head = &avc->avc_cache.slots[hvalue];
	hlist_for_each_entry_rcu(node, head, list) {
		if (ssid == node->ae.ssid &&
		    tclass == node->ae.tclass &&
		    tsid == node->ae.tsid) {
			ret = node;
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
 * then this function returns the avc_node.
 * Otherwise, this function returns NULL.
 */
static struct avc_node *avc_lookup(struct selinux_avc *avc,
				   u32 ssid, u32 tsid, u16 tclass)
{
	struct avc_node *node;

	avc_cache_stats_incr(lookups);
	node = avc_search_node(avc, ssid, tsid, tclass);

	if (node)
		return node;

	avc_cache_stats_incr(misses);
	return NULL;
}

static int avc_latest_notif_update(struct selinux_avc *avc,
				   int seqno, int is_insert)
{
	int ret = 0;
	static DEFINE_SPINLOCK(notif_lock);
	unsigned long flag;

	spin_lock_irqsave(&notif_lock, flag);
	if (is_insert) {
		if (seqno < avc->avc_cache.latest_notif) {
			pr_warn("SELinux: avc:  seqno %d < latest_notif %d\n",
			       seqno, avc->avc_cache.latest_notif);
			ret = -EAGAIN;
		}
	} else {
		if (seqno > avc->avc_cache.latest_notif)
			avc->avc_cache.latest_notif = seqno;
	}
	spin_unlock_irqrestore(&notif_lock, flag);

	return ret;
}

/**
 * avc_insert - Insert an AVC entry.
 * @ssid: source security identifier
 * @tsid: target security identifier
 * @tclass: target security class
 * @avd: resulting av decision
 * @xp_node: resulting extended permissions
 *
 * Insert an AVC entry for the SID pair
 * (@ssid, @tsid) and class @tclass.
 * The access vectors and the sequence number are
 * normally provided by the security server in
 * response to a security_compute_av() call.  If the
 * sequence number @avd->seqno is not less than the latest
 * revocation notification, then the function copies
 * the access vectors into a cache entry, returns
 * avc_node inserted. Otherwise, this function returns NULL.
 */
static struct avc_node *avc_insert(struct selinux_avc *avc,
				   u32 ssid, u32 tsid, u16 tclass,
				   struct av_decision *avd,
				   struct avc_xperms_node *xp_node)
{
	struct avc_node *pos, *node = NULL;
	int hvalue;
	unsigned long flag;
	spinlock_t *lock;
	struct hlist_head *head;

	if (avc_latest_notif_update(avc, avd->seqno, 1))
		return NULL;

	node = avc_alloc_node(avc);
	if (!node)
		return NULL;

	avc_node_populate(node, ssid, tsid, tclass, avd);
	if (avc_xperms_populate(node, xp_node)) {
		avc_node_kill(avc, node);
		return NULL;
	}

	hvalue = avc_hash(ssid, tsid, tclass);
	head = &avc->avc_cache.slots[hvalue];
	lock = &avc->avc_cache.slots_lock[hvalue];
	spin_lock_irqsave(lock, flag);
	hlist_for_each_entry(pos, head, list) {
		if (pos->ae.ssid == ssid &&
			pos->ae.tsid == tsid &&
			pos->ae.tclass == tclass) {
			avc_node_replace(avc, node, pos);
			goto found;
		}
	}
	hlist_add_head_rcu(&node->list, head);
found:
	spin_unlock_irqrestore(lock, flag);
	return node;
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
	audit_log_format(ab, "avc:  %s ",
			 ad->selinux_audit_data->denied ? "denied" : "granted");
	avc_dump_av(ab, ad->selinux_audit_data->tclass,
			ad->selinux_audit_data->audited);
	audit_log_format(ab, " for ");
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
	audit_log_format(ab, " ");
	avc_dump_query(ab, ad->selinux_audit_data->state,
		       ad->selinux_audit_data->ssid,
		       ad->selinux_audit_data->tsid,
		       ad->selinux_audit_data->tclass);
	if (ad->selinux_audit_data->denied) {
		audit_log_format(ab, " permissive=%u",
				 ad->selinux_audit_data->result ? 0 : 1);
	}
}

/* This is the slow part of avc audit with big stack footprint */
noinline int slow_avc_audit(struct selinux_state *state,
			    u32 ssid, u32 tsid, u16 tclass,
			    u32 requested, u32 audited, u32 denied, int result,
			    struct common_audit_data *a)
{
	struct common_audit_data stack_data;
	struct selinux_audit_data sad;

	if (!a) {
		a = &stack_data;
		a->type = LSM_AUDIT_DATA_NONE;
	}

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
	struct avc_callback_node *c;
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
 * avc_update_node Update an AVC entry
 * @event : Updating event
 * @perms : Permission mask bits
 * @ssid,@tsid,@tclass : identifier of an AVC entry
 * @seqno : sequence number when decision was made
 * @xpd: extended_perms_decision to be added to the node
 * @flags: the AVC_* flags, e.g. AVC_NONBLOCKING, AVC_EXTENDED_PERMS, or 0.
 *
 * if a valid AVC entry doesn't exist,this function returns -ENOENT.
 * if kmalloc() called internal returns NULL, this function returns -ENOMEM.
 * otherwise, this function updates the AVC entry. The original AVC-entry object
 * will release later by RCU.
 */
static int avc_update_node(struct selinux_avc *avc,
			   u32 event, u32 perms, u8 driver, u8 xperm, u32 ssid,
			   u32 tsid, u16 tclass, u32 seqno,
			   struct extended_perms_decision *xpd,
			   u32 flags)
{
	int hvalue, rc = 0;
	unsigned long flag;
	struct avc_node *pos, *node, *orig = NULL;
	struct hlist_head *head;
	spinlock_t *lock;

	/*
	 * If we are in a non-blocking code path, e.g. VFS RCU walk,
	 * then we must not add permissions to a cache entry
	 * because we will not audit the denial.  Otherwise,
	 * during the subsequent blocking retry (e.g. VFS ref walk), we
	 * will find the permissions already granted in the cache entry
	 * and won't audit anything at all, leading to silent denials in
	 * permissive mode that only appear when in enforcing mode.
	 *
	 * See the corresponding handling of MAY_NOT_BLOCK in avc_audit()
	 * and selinux_inode_permission().
	 */
	if (flags & AVC_NONBLOCKING)
		return 0;

	node = avc_alloc_node(avc);
	if (!node) {
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
		    seqno == pos->ae.avd.seqno){
			orig = pos;
			break;
		}
	}

	if (!orig) {
		rc = -ENOENT;
		avc_node_kill(avc, node);
		goto out_unlock;
	}

	/*
	 * Copy and replace original node.
	 */

	avc_node_populate(node, ssid, tsid, tclass, &orig->ae.avd);

	if (orig->ae.xp_node) {
		rc = avc_xperms_populate(node, orig->ae.xp_node);
		if (rc) {
			kmem_cache_free(avc_node_cachep, node);
			goto out_unlock;
		}
	}

	switch (event) {
	case AVC_CALLBACK_GRANT:
		node->ae.avd.allowed |= perms;
		if (node->ae.xp_node && (flags & AVC_EXTENDED_PERMS))
			avc_xperms_allow_perm(node->ae.xp_node, driver, xperm);
		break;
	case AVC_CALLBACK_TRY_REVOKE:
	case AVC_CALLBACK_REVOKE:
		node->ae.avd.allowed &= ~perms;
		break;
	case AVC_CALLBACK_AUDITALLOW_ENABLE:
		node->ae.avd.auditallow |= perms;
		break;
	case AVC_CALLBACK_AUDITALLOW_DISABLE:
		node->ae.avd.auditallow &= ~perms;
		break;
	case AVC_CALLBACK_AUDITDENY_ENABLE:
		node->ae.avd.auditdeny |= perms;
		break;
	case AVC_CALLBACK_AUDITDENY_DISABLE:
		node->ae.avd.auditdeny &= ~perms;
		break;
	case AVC_CALLBACK_ADD_XPERMS:
		avc_add_xperms_decision(node, xpd);
		break;
	}
	avc_node_replace(avc, node, orig);
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
	struct avc_node *node;
	spinlock_t *lock;
	unsigned long flag;
	int i;

	for (i = 0; i < AVC_CACHE_SLOTS; i++) {
		head = &avc->avc_cache.slots[i];
		lock = &avc->avc_cache.slots_lock[i];

		spin_lock_irqsave(lock, flag);
		/*
		 * With preemptable RCU, the outer spinlock does not
		 * prevent RCU grace periods from ending.
		 */
		rcu_read_lock();
		hlist_for_each_entry(node, head, list)
			avc_node_delete(avc, node);
		rcu_read_unlock();
		spin_unlock_irqrestore(lock, flag);
	}
}

/**
 * avc_ss_reset - Flush the cache and revalidate migrated permissions.
 * @seqno: policy sequence number
 */
int avc_ss_reset(struct selinux_avc *avc, u32 seqno)
{
	struct avc_callback_node *c;
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

	avc_latest_notif_update(avc, seqno, 0);
	return rc;
}

/*
 * Slow-path helper function for avc_has_perm_noaudit,
 * when the avc_node lookup fails. We get called with
 * the RCU read lock held, and need to return with it
 * still held, but drop if for the security compute.
 *
 * Don't inline this, since it's the slow-path and just
 * results in a bigger stack frame.
 */
static noinline
struct avc_node *avc_compute_av(struct selinux_state *state,
				u32 ssid, u32 tsid,
				u16 tclass, struct av_decision *avd,
				struct avc_xperms_node *xp_node)
{
	rcu_read_unlock();
	INIT_LIST_HEAD(&xp_node->xpd_head);
	security_compute_av(state, ssid, tsid, tclass, avd, &xp_node->xp);
	rcu_read_lock();
	return avc_insert(state->avc, ssid, tsid, tclass, avd, xp_node);
}

static noinline int avc_denied(struct selinux_state *state,
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

	avc_update_node(state->avc, AVC_CALLBACK_GRANT, requested, driver,
			xperm, ssid, tsid, tclass, avd->seqno, NULL, flags);
	return 0;
}

/*
 * The avc extended permissions logic adds an additional 256 bits of
 * permissions to an avc node when extended permissions for that node are
 * specified in the avtab. If the additional 256 permissions is not adequate,
 * as-is the case with ioctls, then multiple may be chained together and the
 * driver field is used to specify which set contains the permission.
 */
int avc_has_extended_perms(struct selinux_state *state,
			   u32 ssid, u32 tsid, u16 tclass, u32 requested,
			   u8 driver, u8 xperm, struct common_audit_data *ad)
{
	struct avc_node *node;
	struct av_decision avd;
	u32 denied;
	struct extended_perms_decision local_xpd;
	struct extended_perms_decision *xpd = NULL;
	struct extended_perms_data allowed;
	struct extended_perms_data auditallow;
	struct extended_perms_data dontaudit;
	struct avc_xperms_node local_xp_node;
	struct avc_xperms_node *xp_node;
	int rc = 0, rc2;

	xp_node = &local_xp_node;
	BUG_ON(!requested);

	rcu_read_lock();

	node = avc_lookup(state->avc, ssid, tsid, tclass);
	if (unlikely(!node)) {
		node = avc_compute_av(state, ssid, tsid, tclass, &avd, xp_node);
	} else {
		memcpy(&avd, &node->ae.avd, sizeof(avd));
		xp_node = node->ae.xp_node;
	}
	/* if extended permissions are not defined, only consider av_decision */
	if (!xp_node || !xp_node->xp.len)
		goto decision;

	local_xpd.allowed = &allowed;
	local_xpd.auditallow = &auditallow;
	local_xpd.dontaudit = &dontaudit;

	xpd = avc_xperms_decision_lookup(driver, xp_node);
	if (unlikely(!xpd)) {
		/*
		 * Compute the extended_perms_decision only if the driver
		 * is flagged
		 */
		if (!security_xperm_test(xp_node->xp.drivers.p, driver)) {
			avd.allowed &= ~requested;
			goto decision;
		}
		rcu_read_unlock();
		security_compute_xperms_decision(state, ssid, tsid, tclass,
						 driver, &local_xpd);
		rcu_read_lock();
		avc_update_node(state->avc, AVC_CALLBACK_ADD_XPERMS, requested,
				driver, xperm, ssid, tsid, tclass, avd.seqno,
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
 * avc_has_perm_noaudit - Check permissions but perform no auditing.
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
 * -%EACCES if any permissions are denied, or another -errno upon
 * other errors.  This function is typically called by avc_has_perm(),
 * but may also be called directly to separate permission checking from
 * auditing, e.g. in cases where a lock must be held for the check but
 * should be released for the auditing.
 */
inline int avc_has_perm_noaudit(struct selinux_state *state,
				u32 ssid, u32 tsid,
				u16 tclass, u32 requested,
				unsigned int flags,
				struct av_decision *avd)
{
	struct avc_node *node;
	struct avc_xperms_node xp_node;
	int rc = 0;
	u32 denied;

	BUG_ON(!requested);

	rcu_read_lock();

	node = avc_lookup(state->avc, ssid, tsid, tclass);
	if (unlikely(!node))
		node = avc_compute_av(state, ssid, tsid, tclass, avd, &xp_node);
	else
		memcpy(avd, &node->ae.avd, sizeof(*avd));

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
 * another -errno upon other errors.
 */
int avc_has_perm(struct selinux_state *state, u32 ssid, u32 tsid, u16 tclass,
		 u32 requested, struct common_audit_data *auditdata)
{
	struct av_decision avd;
	int rc, rc2;

	rc = avc_has_perm_noaudit(state, ssid, tsid, tclass, requested, 0,
				  &avd);

	rc2 = avc_audit(state, ssid, tsid, tclass, requested, &avd, rc,
			auditdata, 0);
	if (rc2)
		return rc2;
	return rc;
}

int avc_has_perm_flags(struct selinux_state *state,
		       u32 ssid, u32 tsid, u16 tclass, u32 requested,
		       struct common_audit_data *auditdata,
		       int flags)
{
	struct av_decision avd;
	int rc, rc2;

	rc = avc_has_perm_noaudit(state, ssid, tsid, tclass, requested,
				  (flags & MAY_NOT_BLOCK) ? AVC_NONBLOCKING : 0,
				  &avd);

	rc2 = avc_audit(state, ssid, tsid, tclass, requested, &avd, rc,
			auditdata, flags);
	if (rc2)
		return rc2;
	return rc;
}

u32 avc_policy_seqno(struct selinux_state *state)
{
	return state->avc->avc_cache.latest_notif;
}

void avc_disable(void)
{
	/*
	 * If you are looking at this because you have realized that we are
	 * not destroying the avc_node_cachep it might be easy to fix, but
	 * I don't know the memory barrier semantics well enough to know.  It's
	 * possible that some other task dereferenced security_ops when
	 * it still pointed to selinux operations.  If that is the case it's
	 * possible that it is about to use the avc and is about to need the
	 * avc_node_cachep.  I know I could wrap the security.c security_ops call
	 * in an rcu_lock, but seriously, it's not worth it.  Instead I just flush
	 * the cache and get that memory back.
	 */
	if (avc_node_cachep) {
		avc_flush(selinux_state.avc);
		/* kmem_cache_destroy(avc_node_cachep); */
	}
}
