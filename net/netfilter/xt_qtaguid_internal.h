/*
 * Kernel iptables module to track stats for packets based on user tags.
 *
 * (C) 2011 Google, Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __XT_QTAGUID_INTERNAL_H__
#define __XT_QTAGUID_INTERNAL_H__

#include <linux/types.h>
#include <linux/rbtree.h>
#include <linux/spinlock_types.h>
#include <linux/workqueue.h>

/* Define/comment out these *DEBUG to compile in/out the pr_debug calls. */
/* Iface handling */
#define IDEBUG
/* Iptable Matching. Per packet. */
#define MDEBUG
/* Red-black tree handling. Per packet. */
#define RDEBUG
/* procfs ctrl/stats handling */
#define CDEBUG
/* dev and resource tracking */
#define DDEBUG

/* E.g (IDEBUG_MASK | CDEBUG_MASK | DDEBUG_MASK) */
#define DEFAULT_DEBUG_MASK 0


#define IDEBUG_MASK (1<<0)
#define MDEBUG_MASK (1<<1)
#define RDEBUG_MASK (1<<2)
#define CDEBUG_MASK (1<<3)
#define DDEBUG_MASK (1<<4)

#define MSK_DEBUG(mask, ...) do {                       \
		if (unlikely(debug_mask & (mask)))      \
			pr_debug(__VA_ARGS__);          \
	} while (0)
#ifdef IDEBUG
#define IF_DEBUG(...) MSK_DEBUG(IDEBUG_MASK, __VA_ARGS__)
#else
#define IF_DEBUG(...) no_printk(__VA_ARGS__)
#endif
#ifdef MDEBUG
#define MT_DEBUG(...) MSK_DEBUG(MDEBUG_MASK, __VA_ARGS__)
#else
#define MT_DEBUG(...) no_printk(__VA_ARGS__)
#endif
#ifdef RDEBUG
#define RB_DEBUG(...) MSK_DEBUG(RDEBUG_MASK, __VA_ARGS__)
#else
#define RB_DEBUG(...) no_printk(__VA_ARGS__)
#endif
#ifdef CDEBUG
#define CT_DEBUG(...) MSK_DEBUG(CDEBUG_MASK, __VA_ARGS__)
#else
#define CT_DEBUG(...) no_printk(__VA_ARGS__)
#endif
#ifdef DDEBUG
#define DR_DEBUG(...) MSK_DEBUG(DDEBUG_MASK, __VA_ARGS__)
#else
#define DR_DEBUG(...) no_printk(__VA_ARGS__)
#endif

extern uint debug_mask;

/*---------------------------------------------------------------------------*/
/*
 * Tags:
 *
 * They represent what the data usage counters will be tracked against.
 * By default a tag is just based on the UID.
 * The UID is used as the base for policing, and can not be ignored.
 * So a tag will always at least represent a UID (uid_tag).
 *
 * A tag can be augmented with an "accounting tag" which is associated
 * with a UID.
 * User space can set the acct_tag portion of the tag which is then used
 * with sockets: all data belonging to that socket will be counted against the
 * tag. The policing is then based on the tag's uid_tag portion,
 * and stats are collected for the acct_tag portion separately.
 *
 * There could be
 * a:  {acct_tag=1, uid_tag=10003}
 * b:  {acct_tag=2, uid_tag=10003}
 * c:  {acct_tag=3, uid_tag=10003}
 * d:  {acct_tag=0, uid_tag=10003}
 * a, b, and c represent tags associated with specific sockets.
 * d is for the totals for that uid, including all untagged traffic.
 * Typically d is used with policing/quota rules.
 *
 * We want tag_t big enough to distinguish uid_t and acct_tag.
 * It might become a struct if needed.
 * Nothing should be using it as an int.
 */
typedef uint64_t tag_t;  /* Only used via accessors */

#define TAG_UID_MASK 0xFFFFFFFFULL
#define TAG_ACCT_MASK (~0xFFFFFFFFULL)

static inline int tag_compare(tag_t t1, tag_t t2)
{
	return t1 < t2 ? -1 : t1 == t2 ? 0 : 1;
}

static inline tag_t combine_atag_with_uid(tag_t acct_tag, uid_t uid)
{
	return acct_tag | uid;
}
static inline tag_t make_tag_from_uid(uid_t uid)
{
	return uid;
}
static inline uid_t get_uid_from_tag(tag_t tag)
{
	return tag & TAG_UID_MASK;
}
static inline tag_t get_utag_from_tag(tag_t tag)
{
	return tag & TAG_UID_MASK;
}
static inline tag_t get_atag_from_tag(tag_t tag)
{
	return tag & TAG_ACCT_MASK;
}

static inline bool valid_atag(tag_t tag)
{
	return !(tag & TAG_UID_MASK);
}
static inline tag_t make_atag_from_value(uint32_t value)
{
	return (uint64_t)value << 32;
}
/*---------------------------------------------------------------------------*/

/*
 * Maximum number of socket tags that a UID is allowed to have active.
 * Multiple processes belonging to the same UID contribute towards this limit.
 * Special UIDs that can impersonate a UID also contribute (e.g. download
 * manager, ...)
 */
#define DEFAULT_MAX_SOCK_TAGS 1024

/*
 * For now we only track 2 sets of counters.
 * The default set is 0.
 * Userspace can activate another set for a given uid being tracked.
 */
#define IFS_MAX_COUNTER_SETS 2

enum ifs_tx_rx {
	IFS_TX,
	IFS_RX,
	IFS_MAX_DIRECTIONS
};

/* For now, TCP, UDP, the rest */
enum ifs_proto {
	IFS_TCP,
	IFS_UDP,
	IFS_PROTO_OTHER,
	IFS_MAX_PROTOS
};

struct byte_packet_counters {
	uint64_t bytes;
	uint64_t packets;
};

struct data_counters {
	struct byte_packet_counters bpc[IFS_MAX_COUNTER_SETS][IFS_MAX_DIRECTIONS][IFS_MAX_PROTOS];
};

/* Generic X based nodes used as a base for rb_tree ops */
struct tag_node {
	struct rb_node node;
	tag_t tag;
};

struct tag_stat {
	struct tag_node tn;
	struct data_counters counters;
	/*
	 * If this tag is acct_tag based, we need to count against the
	 * matching parent uid_tag.
	 */
	struct data_counters *parent_counters;
};

struct iface_stat {
	struct list_head list;  /* in iface_stat_list */
	char *ifname;
	bool active;
	/* net_dev is only valid for active iface_stat */
	struct net_device *net_dev;

	struct byte_packet_counters totals[IFS_MAX_DIRECTIONS];
	/*
	 * We keep the last_known, because some devices reset their counters
	 * just before NETDEV_UP, while some will reset just before
	 * NETDEV_REGISTER (which is more normal).
	 * So now, if the device didn't do a NETDEV_UNREGISTER and we see
	 * its current dev stats smaller that what was previously known, we
	 * assume an UNREGISTER and just use the last_known.
	 */
	struct byte_packet_counters last_known[IFS_MAX_DIRECTIONS];
	/* last_known is usable when last_known_valid is true */
	bool last_known_valid;

	struct proc_dir_entry *proc_ptr;

	struct rb_root tag_stat_tree;
	spinlock_t tag_stat_list_lock;
};

/* This is needed to create proc_dir_entries from atomic context. */
struct iface_stat_work {
	struct work_struct iface_work;
	struct iface_stat *iface_entry;
};

/*
 * Track tag that this socket is transferring data for, and not necessarily
 * the uid that owns the socket.
 * This is the tag against which tag_stat.counters will be billed.
 * These structs need to be looked up by sock and pid.
 */
struct sock_tag {
	struct rb_node sock_node;
	struct sock *sk;  /* Only used as a number, never dereferenced */
	/* The socket is needed for sockfd_put() */
	struct socket *socket;
	/* Used to associate with a given pid */
	struct list_head list;   /* in proc_qtu_data.sock_tag_list */
	pid_t pid;

	tag_t tag;
};

struct qtaguid_event_counts {
	/* Various successful events */
	atomic64_t sockets_tagged;
	atomic64_t sockets_untagged;
	atomic64_t counter_set_changes;
	atomic64_t delete_cmds;
	atomic64_t iface_events;  /* Number of NETDEV_* events handled */
	/*
	 * match_found_sk_*: numbers related to the netfilter matching
	 * function finding a sock for the sk_buff.
	 */
	atomic64_t match_found_sk;   /* An sk was already in the sk_buff. */
	/* The connection tracker had the sk. */
	atomic64_t match_found_sk_in_ct;
	/*
	 * No sk could be found. No apparent owner. Could happen with
	 * unsolicited traffic.
	 */
	atomic64_t match_found_sk_none;
};

/* Track the set active_set for the given tag. */
struct tag_counter_set {
	struct tag_node tn;
	int active_set;
};

/*----------------------------------------------*/
/*
 * The qtu uid data is used to track resources that are created directly or
 * indirectly by processes (uid tracked).
 * It is shared by the processes with the same uid.
 * Some of the resource will be counted to prevent further rogue allocations,
 * some will need freeing once the owner process (uid) exits.
 */
struct uid_tag_data {
	struct rb_node node;
	uid_t uid;

	/*
	 * For the uid, how many accounting tags have been set.
	 */
	int num_active_tags;
	struct rb_root tag_ref_tree;
	/* No tag_node_tree_lock; use uid_tag_data_tree_lock */
};

struct tag_ref {
	struct tag_node tn;

	/*
	 * This tracks the number of active sockets that have a tag on them
	 * which matches this tag_ref.tn.tag.
	 * A tag ref can live on after the sockets are untagged.
	 * A tag ref can only be removed during a tag delete command.
	 */
	int num_sock_tags;
};

struct proc_qtu_data {
	struct rb_node node;
	pid_t pid;

	struct uid_tag_data *parent_tag_data;

	/* Tracks the sock_tags that need freeing upon this proc's death */
	struct list_head sock_tag_list;
	/* No spinlock_t sock_tag_list_lock; use the global one. */
};

/*----------------------------------------------*/
#endif  /* ifndef __XT_QTAGUID_INTERNAL_H__ */
