/*
 * Implementation of the kernel access vector cache (AVC).
 *
 * Authors:  Stephen Smalley, <sds@epoch.ncsc.mil>
 *           James Morris <jmorris@redhat.com>
 *
 * Update:   KaiGai, Kohei <kaigai@ak.jp.nec.com>
 *     Replaced the avc_lock spinlock by RCU.
 *
 * Copyright (C) 2003 Red Hat, Inc., James Morris <jmorris@redhat.com>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License version 2,
 *      as published by the Free Software Foundation.
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
#include <net/sock.h>
#include <linux/un.h>
#include <net/af_unix.h>
#include <linux/ip.h>
#include <linux/audit.h>
#include <linux/ipv6.h>
#include <net/ipv6.h>
#include "avc.h"
#include "avc_ss.h"

static const struct av_perm_to_string av_perm_to_string[] = {
#define S_(c, v, s) { c, v, s },
#include "av_perm_to_string.h"
#undef S_
};

static const char *class_to_string[] = {
#define S_(s) s,
#include "class_to_string.h"
#undef S_
};

#define TB_(s) static const char * s [] = {
#define TE_(s) };
#define S_(s) s,
#include "common_perm_to_string.h"
#undef TB_
#undef TE_
#undef S_

static const struct av_inherit av_inherit[] = {
#define S_(c, i, b) { c, common_##i##_perm_to_string, b },
#include "av_inherit.h"
#undef S_
};

const struct selinux_class_perm selinux_class_perm = {
	av_perm_to_string,
	ARRAY_SIZE(av_perm_to_string),
	class_to_string,
	ARRAY_SIZE(class_to_string),
	av_inherit,
	ARRAY_SIZE(av_inherit)
};

#define AVC_CACHE_SLOTS			512
#define AVC_DEF_CACHE_THRESHOLD		512
#define AVC_CACHE_RECLAIM		16

#ifdef CONFIG_SECURITY_SELINUX_AVC_STATS
#define avc_cache_stats_incr(field) 				\
do {								\
	per_cpu(avc_cache_stats, get_cpu()).field++;		\
	put_cpu();						\
} while (0)
#else
#define avc_cache_stats_incr(field)	do {} while (0)
#endif

struct avc_entry {
	u32			ssid;
	u32			tsid;
	u16			tclass;
	struct av_decision	avd;
	atomic_t		used;	/* used recently */
};

struct avc_node {
	struct avc_entry	ae;
	struct list_head	list;
	struct rcu_head         rhead;
};

struct avc_cache {
	struct list_head	slots[AVC_CACHE_SLOTS];
	spinlock_t		slots_lock[AVC_CACHE_SLOTS]; /* lock for writes */
	atomic_t		lru_hint;	/* LRU hint for reclaim scan */
	atomic_t		active_nodes;
	u32			latest_notif;	/* latest revocation notification */
};

struct avc_callback_node {
	int (*callback) (u32 event, u32 ssid, u32 tsid,
	                 u16 tclass, u32 perms,
	                 u32 *out_retained);
	u32 events;
	u32 ssid;
	u32 tsid;
	u16 tclass;
	u32 perms;
	struct avc_callback_node *next;
};

/* Exported via selinufs */
unsigned int avc_cache_threshold = AVC_DEF_CACHE_THRESHOLD;

#ifdef CONFIG_SECURITY_SELINUX_AVC_STATS
DEFINE_PER_CPU(struct avc_cache_stats, avc_cache_stats) = { 0 };
#endif

static struct avc_cache avc_cache;
static struct avc_callback_node *avc_callbacks;
static struct kmem_cache *avc_node_cachep;

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
	const char **common_pts = NULL;
	u32 common_base = 0;
	int i, i2, perm;

	if (av == 0) {
		audit_log_format(ab, " null");
		return;
	}

	for (i = 0; i < ARRAY_SIZE(av_inherit); i++) {
		if (av_inherit[i].tclass == tclass) {
			common_pts = av_inherit[i].common_pts;
			common_base = av_inherit[i].common_base;
			break;
		}
	}

	audit_log_format(ab, " {");
	i = 0;
	perm = 1;
	while (perm < common_base) {
		if (perm & av) {
			audit_log_format(ab, " %s", common_pts[i]);
			av &= ~perm;
		}
		i++;
		perm <<= 1;
	}

	while (i < sizeof(av) * 8) {
		if (perm & av) {
			for (i2 = 0; i2 < ARRAY_SIZE(av_perm_to_string); i2++) {
				if ((av_perm_to_string[i2].tclass == tclass) &&
				    (av_perm_to_string[i2].value == perm))
					break;
			}
			if (i2 < ARRAY_SIZE(av_perm_to_string)) {
				audit_log_format(ab, " %s",
						 av_perm_to_string[i2].name);
				av &= ~perm;
			}
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
static void avc_dump_query(struct audit_buffer *ab, u32 ssid, u32 tsid, u16 tclass)
{
	int rc;
	char *scontext;
	u32 scontext_len;

 	rc = security_sid_to_context(ssid, &scontext, &scontext_len);
	if (rc)
		audit_log_format(ab, "ssid=%d", ssid);
	else {
		audit_log_format(ab, "scontext=%s", scontext);
		kfree(scontext);
	}

	rc = security_sid_to_context(tsid, &scontext, &scontext_len);
	if (rc)
		audit_log_format(ab, " tsid=%d", tsid);
	else {
		audit_log_format(ab, " tcontext=%s", scontext);
		kfree(scontext);
	}

	BUG_ON(tclass >= ARRAY_SIZE(class_to_string) || !class_to_string[tclass]);
	audit_log_format(ab, " tclass=%s", class_to_string[tclass]);
}

/**
 * avc_init - Initialize the AVC.
 *
 * Initialize the access vector cache.
 */
void __init avc_init(void)
{
	int i;

	for (i = 0; i < AVC_CACHE_SLOTS; i++) {
		INIT_LIST_HEAD(&avc_cache.slots[i]);
		spin_lock_init(&avc_cache.slots_lock[i]);
	}
	atomic_set(&avc_cache.active_nodes, 0);
	atomic_set(&avc_cache.lru_hint, 0);

	avc_node_cachep = kmem_cache_create("avc_node", sizeof(struct avc_node),
					     0, SLAB_PANIC, NULL);

	audit_log(current->audit_context, GFP_KERNEL, AUDIT_KERNEL, "AVC INITIALIZED\n");
}

int avc_get_hash_stats(char *page)
{
	int i, chain_len, max_chain_len, slots_used;
	struct avc_node *node;

	rcu_read_lock();

	slots_used = 0;
	max_chain_len = 0;
	for (i = 0; i < AVC_CACHE_SLOTS; i++) {
		if (!list_empty(&avc_cache.slots[i])) {
			slots_used++;
			chain_len = 0;
			list_for_each_entry_rcu(node, &avc_cache.slots[i], list)
				chain_len++;
			if (chain_len > max_chain_len)
				max_chain_len = chain_len;
		}
	}

	rcu_read_unlock();

	return scnprintf(page, PAGE_SIZE, "entries: %d\nbuckets used: %d/%d\n"
			 "longest chain: %d\n",
			 atomic_read(&avc_cache.active_nodes),
			 slots_used, AVC_CACHE_SLOTS, max_chain_len);
}

static void avc_node_free(struct rcu_head *rhead)
{
	struct avc_node *node = container_of(rhead, struct avc_node, rhead);
	kmem_cache_free(avc_node_cachep, node);
	avc_cache_stats_incr(frees);
}

static void avc_node_delete(struct avc_node *node)
{
	list_del_rcu(&node->list);
	call_rcu(&node->rhead, avc_node_free);
	atomic_dec(&avc_cache.active_nodes);
}

static void avc_node_kill(struct avc_node *node)
{
	kmem_cache_free(avc_node_cachep, node);
	avc_cache_stats_incr(frees);
	atomic_dec(&avc_cache.active_nodes);
}

static void avc_node_replace(struct avc_node *new, struct avc_node *old)
{
	list_replace_rcu(&old->list, &new->list);
	call_rcu(&old->rhead, avc_node_free);
	atomic_dec(&avc_cache.active_nodes);
}

static inline int avc_reclaim_node(void)
{
	struct avc_node *node;
	int hvalue, try, ecx;
	unsigned long flags;

	for (try = 0, ecx = 0; try < AVC_CACHE_SLOTS; try++ ) {
		hvalue = atomic_inc_return(&avc_cache.lru_hint) & (AVC_CACHE_SLOTS - 1);

		if (!spin_trylock_irqsave(&avc_cache.slots_lock[hvalue], flags))
			continue;

		list_for_each_entry(node, &avc_cache.slots[hvalue], list) {
			if (atomic_dec_and_test(&node->ae.used)) {
				/* Recently Unused */
				avc_node_delete(node);
				avc_cache_stats_incr(reclaims);
				ecx++;
				if (ecx >= AVC_CACHE_RECLAIM) {
					spin_unlock_irqrestore(&avc_cache.slots_lock[hvalue], flags);
					goto out;
				}
			}
		}
		spin_unlock_irqrestore(&avc_cache.slots_lock[hvalue], flags);
	}
out:
	return ecx;
}

static struct avc_node *avc_alloc_node(void)
{
	struct avc_node *node;

	node = kmem_cache_zalloc(avc_node_cachep, GFP_ATOMIC);
	if (!node)
		goto out;

	INIT_RCU_HEAD(&node->rhead);
	INIT_LIST_HEAD(&node->list);
	atomic_set(&node->ae.used, 1);
	avc_cache_stats_incr(allocations);

	if (atomic_inc_return(&avc_cache.active_nodes) > avc_cache_threshold)
		avc_reclaim_node();

out:
	return node;
}

static void avc_node_populate(struct avc_node *node, u32 ssid, u32 tsid, u16 tclass, struct avc_entry *ae)
{
	node->ae.ssid = ssid;
	node->ae.tsid = tsid;
	node->ae.tclass = tclass;
	memcpy(&node->ae.avd, &ae->avd, sizeof(node->ae.avd));
}

static inline struct avc_node *avc_search_node(u32 ssid, u32 tsid, u16 tclass)
{
	struct avc_node *node, *ret = NULL;
	int hvalue;

	hvalue = avc_hash(ssid, tsid, tclass);
	list_for_each_entry_rcu(node, &avc_cache.slots[hvalue], list) {
		if (ssid == node->ae.ssid &&
		    tclass == node->ae.tclass &&
		    tsid == node->ae.tsid) {
			ret = node;
			break;
		}
	}

	if (ret == NULL) {
		/* cache miss */
		goto out;
	}

	/* cache hit */
	if (atomic_read(&ret->ae.used) != 1)
		atomic_set(&ret->ae.used, 1);
out:
	return ret;
}

/**
 * avc_lookup - Look up an AVC entry.
 * @ssid: source security identifier
 * @tsid: target security identifier
 * @tclass: target security class
 * @requested: requested permissions, interpreted based on @tclass
 *
 * Look up an AVC entry that is valid for the
 * @requested permissions between the SID pair
 * (@ssid, @tsid), interpreting the permissions
 * based on @tclass.  If a valid AVC entry exists,
 * then this function return the avc_node.
 * Otherwise, this function returns NULL.
 */
static struct avc_node *avc_lookup(u32 ssid, u32 tsid, u16 tclass, u32 requested)
{
	struct avc_node *node;

	avc_cache_stats_incr(lookups);
	node = avc_search_node(ssid, tsid, tclass);

	if (node && ((node->ae.avd.decided & requested) == requested)) {
		avc_cache_stats_incr(hits);
		goto out;
	}

	node = NULL;
	avc_cache_stats_incr(misses);
out:
	return node;
}

static int avc_latest_notif_update(int seqno, int is_insert)
{
	int ret = 0;
	static DEFINE_SPINLOCK(notif_lock);
	unsigned long flag;

	spin_lock_irqsave(&notif_lock, flag);
	if (is_insert) {
		if (seqno < avc_cache.latest_notif) {
			printk(KERN_WARNING "avc:  seqno %d < latest_notif %d\n",
			       seqno, avc_cache.latest_notif);
			ret = -EAGAIN;
		}
	} else {
		if (seqno > avc_cache.latest_notif)
			avc_cache.latest_notif = seqno;
	}
	spin_unlock_irqrestore(&notif_lock, flag);

	return ret;
}

/**
 * avc_insert - Insert an AVC entry.
 * @ssid: source security identifier
 * @tsid: target security identifier
 * @tclass: target security class
 * @ae: AVC entry
 *
 * Insert an AVC entry for the SID pair
 * (@ssid, @tsid) and class @tclass.
 * The access vectors and the sequence number are
 * normally provided by the security server in
 * response to a security_compute_av() call.  If the
 * sequence number @ae->avd.seqno is not less than the latest
 * revocation notification, then the function copies
 * the access vectors into a cache entry, returns
 * avc_node inserted. Otherwise, this function returns NULL.
 */
static struct avc_node *avc_insert(u32 ssid, u32 tsid, u16 tclass, struct avc_entry *ae)
{
	struct avc_node *pos, *node = NULL;
	int hvalue;
	unsigned long flag;

	if (avc_latest_notif_update(ae->avd.seqno, 1))
		goto out;

	node = avc_alloc_node();
	if (node) {
		hvalue = avc_hash(ssid, tsid, tclass);
		avc_node_populate(node, ssid, tsid, tclass, ae);

		spin_lock_irqsave(&avc_cache.slots_lock[hvalue], flag);
		list_for_each_entry(pos, &avc_cache.slots[hvalue], list) {
			if (pos->ae.ssid == ssid &&
			    pos->ae.tsid == tsid &&
			    pos->ae.tclass == tclass) {
			    	avc_node_replace(node, pos);
				goto found;
			}
		}
		list_add_rcu(&node->list, &avc_cache.slots[hvalue]);
found:
		spin_unlock_irqrestore(&avc_cache.slots_lock[hvalue], flag);
	}
out:
	return node;
}

static inline void avc_print_ipv6_addr(struct audit_buffer *ab,
				       struct in6_addr *addr, __be16 port,
				       char *name1, char *name2)
{
	if (!ipv6_addr_any(addr))
		audit_log_format(ab, " %s=" NIP6_FMT, name1, NIP6(*addr));
	if (port)
		audit_log_format(ab, " %s=%d", name2, ntohs(port));
}

static inline void avc_print_ipv4_addr(struct audit_buffer *ab, __be32 addr,
				       __be16 port, char *name1, char *name2)
{
	if (addr)
		audit_log_format(ab, " %s=" NIPQUAD_FMT, name1, NIPQUAD(addr));
	if (port)
		audit_log_format(ab, " %s=%d", name2, ntohs(port));
}

/**
 * avc_audit - Audit the granting or denial of permissions.
 * @ssid: source security identifier
 * @tsid: target security identifier
 * @tclass: target security class
 * @requested: requested permissions
 * @avd: access vector decisions
 * @result: result from avc_has_perm_noaudit
 * @a:  auxiliary audit data
 *
 * Audit the granting or denial of permissions in accordance
 * with the policy.  This function is typically called by
 * avc_has_perm() after a permission check, but can also be
 * called directly by callers who use avc_has_perm_noaudit()
 * in order to separate the permission check from the auditing.
 * For example, this separation is useful when the permission check must
 * be performed under a lock, to allow the lock to be released
 * before calling the auditing code.
 */
void avc_audit(u32 ssid, u32 tsid,
               u16 tclass, u32 requested,
               struct av_decision *avd, int result, struct avc_audit_data *a)
{
	struct task_struct *tsk = current;
	struct inode *inode = NULL;
	u32 denied, audited;
	struct audit_buffer *ab;

	denied = requested & ~avd->allowed;
	if (denied) {
		audited = denied;
		if (!(audited & avd->auditdeny))
			return;
	} else if (result) {
		audited = denied = requested;
        } else {
		audited = requested;
		if (!(audited & avd->auditallow))
			return;
	}

	ab = audit_log_start(current->audit_context, GFP_ATOMIC, AUDIT_AVC);
	if (!ab)
		return;		/* audit_panic has been called */
	audit_log_format(ab, "avc:  %s ", denied ? "denied" : "granted");
	avc_dump_av(ab, tclass,audited);
	audit_log_format(ab, " for ");
	if (a && a->tsk)
		tsk = a->tsk;
	if (tsk && tsk->pid) {
		audit_log_format(ab, " pid=%d comm=", tsk->pid);
		audit_log_untrustedstring(ab, tsk->comm);
	}
	if (a) {
		switch (a->type) {
		case AVC_AUDIT_DATA_IPC:
			audit_log_format(ab, " key=%d", a->u.ipc_id);
			break;
		case AVC_AUDIT_DATA_CAP:
			audit_log_format(ab, " capability=%d", a->u.cap);
			break;
		case AVC_AUDIT_DATA_FS:
			if (a->u.fs.dentry) {
				struct dentry *dentry = a->u.fs.dentry;
				if (a->u.fs.mnt)
					audit_avc_path(dentry, a->u.fs.mnt);
				audit_log_format(ab, " name=");
				audit_log_untrustedstring(ab, dentry->d_name.name);
				inode = dentry->d_inode;
			} else if (a->u.fs.inode) {
				struct dentry *dentry;
				inode = a->u.fs.inode;
				dentry = d_find_alias(inode);
				if (dentry) {
					audit_log_format(ab, " name=");
					audit_log_untrustedstring(ab, dentry->d_name.name);
					dput(dentry);
				}
			}
			if (inode)
				audit_log_format(ab, " dev=%s ino=%lu",
						 inode->i_sb->s_id,
						 inode->i_ino);
			break;
		case AVC_AUDIT_DATA_NET:
			if (a->u.net.sk) {
				struct sock *sk = a->u.net.sk;
				struct unix_sock *u;
				int len = 0;
				char *p = NULL;

				switch (sk->sk_family) {
				case AF_INET: {
					struct inet_sock *inet = inet_sk(sk);

					avc_print_ipv4_addr(ab, inet->rcv_saddr,
							    inet->sport,
							    "laddr", "lport");
					avc_print_ipv4_addr(ab, inet->daddr,
							    inet->dport,
							    "faddr", "fport");
					break;
				}
				case AF_INET6: {
					struct inet_sock *inet = inet_sk(sk);
					struct ipv6_pinfo *inet6 = inet6_sk(sk);

					avc_print_ipv6_addr(ab, &inet6->rcv_saddr,
							    inet->sport,
							    "laddr", "lport");
					avc_print_ipv6_addr(ab, &inet6->daddr,
							    inet->dport,
							    "faddr", "fport");
					break;
				}
				case AF_UNIX:
					u = unix_sk(sk);
					if (u->dentry) {
						audit_avc_path(u->dentry, u->mnt);
						audit_log_format(ab, " name=");
						audit_log_untrustedstring(ab, u->dentry->d_name.name);
						break;
					}
					if (!u->addr)
						break;
					len = u->addr->len-sizeof(short);
					p = &u->addr->name->sun_path[0];
					audit_log_format(ab, " path=");
					if (*p)
						audit_log_untrustedstring(ab, p);
					else
						audit_log_hex(ab, p, len);
					break;
				}
			}
			
			switch (a->u.net.family) {
			case AF_INET:
				avc_print_ipv4_addr(ab, a->u.net.v4info.saddr,
						    a->u.net.sport,
						    "saddr", "src");
				avc_print_ipv4_addr(ab, a->u.net.v4info.daddr,
						    a->u.net.dport,
						    "daddr", "dest");
				break;
			case AF_INET6:
				avc_print_ipv6_addr(ab, &a->u.net.v6info.saddr,
						    a->u.net.sport,
						    "saddr", "src");
				avc_print_ipv6_addr(ab, &a->u.net.v6info.daddr,
						    a->u.net.dport,
						    "daddr", "dest");
				break;
			}
			if (a->u.net.netif)
				audit_log_format(ab, " netif=%s",
					a->u.net.netif);
			break;
		}
	}
	audit_log_format(ab, " ");
	avc_dump_query(ab, ssid, tsid, tclass);
	audit_log_end(ab);
}

/**
 * avc_add_callback - Register a callback for security events.
 * @callback: callback function
 * @events: security events
 * @ssid: source security identifier or %SECSID_WILD
 * @tsid: target security identifier or %SECSID_WILD
 * @tclass: target security class
 * @perms: permissions
 *
 * Register a callback function for events in the set @events
 * related to the SID pair (@ssid, @tsid) and
 * and the permissions @perms, interpreting
 * @perms based on @tclass.  Returns %0 on success or
 * -%ENOMEM if insufficient memory exists to add the callback.
 */
int avc_add_callback(int (*callback)(u32 event, u32 ssid, u32 tsid,
                                     u16 tclass, u32 perms,
                                     u32 *out_retained),
                     u32 events, u32 ssid, u32 tsid,
                     u16 tclass, u32 perms)
{
	struct avc_callback_node *c;
	int rc = 0;

	c = kmalloc(sizeof(*c), GFP_ATOMIC);
	if (!c) {
		rc = -ENOMEM;
		goto out;
	}

	c->callback = callback;
	c->events = events;
	c->ssid = ssid;
	c->tsid = tsid;
	c->perms = perms;
	c->next = avc_callbacks;
	avc_callbacks = c;
out:
	return rc;
}

static inline int avc_sidcmp(u32 x, u32 y)
{
	return (x == y || x == SECSID_WILD || y == SECSID_WILD);
}

/**
 * avc_update_node Update an AVC entry
 * @event : Updating event
 * @perms : Permission mask bits
 * @ssid,@tsid,@tclass : identifier of an AVC entry
 *
 * if a valid AVC entry doesn't exist,this function returns -ENOENT.
 * if kmalloc() called internal returns NULL, this function returns -ENOMEM.
 * otherwise, this function update the AVC entry. The original AVC-entry object
 * will release later by RCU.
 */
static int avc_update_node(u32 event, u32 perms, u32 ssid, u32 tsid, u16 tclass)
{
	int hvalue, rc = 0;
	unsigned long flag;
	struct avc_node *pos, *node, *orig = NULL;

	node = avc_alloc_node();
	if (!node) {
		rc = -ENOMEM;
		goto out;
	}

	/* Lock the target slot */
	hvalue = avc_hash(ssid, tsid, tclass);
	spin_lock_irqsave(&avc_cache.slots_lock[hvalue], flag);

	list_for_each_entry(pos, &avc_cache.slots[hvalue], list){
		if ( ssid==pos->ae.ssid &&
		     tsid==pos->ae.tsid &&
		     tclass==pos->ae.tclass ){
			orig = pos;
			break;
		}
	}

	if (!orig) {
		rc = -ENOENT;
		avc_node_kill(node);
		goto out_unlock;
	}

	/*
	 * Copy and replace original node.
	 */

	avc_node_populate(node, ssid, tsid, tclass, &orig->ae);

	switch (event) {
	case AVC_CALLBACK_GRANT:
		node->ae.avd.allowed |= perms;
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
	}
	avc_node_replace(node, orig);
out_unlock:
	spin_unlock_irqrestore(&avc_cache.slots_lock[hvalue], flag);
out:
	return rc;
}

/**
 * avc_ss_reset - Flush the cache and revalidate migrated permissions.
 * @seqno: policy sequence number
 */
int avc_ss_reset(u32 seqno)
{
	struct avc_callback_node *c;
	int i, rc = 0, tmprc;
	unsigned long flag;
	struct avc_node *node;

	for (i = 0; i < AVC_CACHE_SLOTS; i++) {
		spin_lock_irqsave(&avc_cache.slots_lock[i], flag);
		list_for_each_entry(node, &avc_cache.slots[i], list)
			avc_node_delete(node);
		spin_unlock_irqrestore(&avc_cache.slots_lock[i], flag);
	}

	for (c = avc_callbacks; c; c = c->next) {
		if (c->events & AVC_CALLBACK_RESET) {
			tmprc = c->callback(AVC_CALLBACK_RESET,
			                    0, 0, 0, 0, NULL);
			/* save the first error encountered for the return
			   value and continue processing the callbacks */
			if (!rc)
				rc = tmprc;
		}
	}

	avc_latest_notif_update(seqno, 0);
	return rc;
}

/**
 * avc_has_perm_noaudit - Check permissions but perform no auditing.
 * @ssid: source security identifier
 * @tsid: target security identifier
 * @tclass: target security class
 * @requested: requested permissions, interpreted based on @tclass
 * @flags:  AVC_STRICT or 0
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
int avc_has_perm_noaudit(u32 ssid, u32 tsid,
			 u16 tclass, u32 requested,
			 unsigned flags,
			 struct av_decision *avd)
{
	struct avc_node *node;
	struct avc_entry entry, *p_ae;
	int rc = 0;
	u32 denied;

	rcu_read_lock();

	node = avc_lookup(ssid, tsid, tclass, requested);
	if (!node) {
		rcu_read_unlock();
		rc = security_compute_av(ssid,tsid,tclass,requested,&entry.avd);
		if (rc)
			goto out;
		rcu_read_lock();
		node = avc_insert(ssid,tsid,tclass,&entry);
	}

	p_ae = node ? &node->ae : &entry;

	if (avd)
		memcpy(avd, &p_ae->avd, sizeof(*avd));

	denied = requested & ~(p_ae->avd.allowed);

	if (!requested || denied) {
		if (selinux_enforcing || (flags & AVC_STRICT))
			rc = -EACCES;
		else
			if (node)
				avc_update_node(AVC_CALLBACK_GRANT,requested,
						ssid,tsid,tclass);
	}

	rcu_read_unlock();
out:
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
int avc_has_perm(u32 ssid, u32 tsid, u16 tclass,
                 u32 requested, struct avc_audit_data *auditdata)
{
	struct av_decision avd;
	int rc;

	rc = avc_has_perm_noaudit(ssid, tsid, tclass, requested, 0, &avd);
	avc_audit(ssid, tsid, tclass, requested, &avd, rc, auditdata);
	return rc;
}
