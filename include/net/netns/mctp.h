/* SPDX-License-Identifier: GPL-2.0 */
/*
 * MCTP per-net structures
 */

#ifndef __NETNS_MCTP_H__
#define __NETNS_MCTP_H__

#include <linux/hash.h>
#include <linux/hashtable.h>
#include <linux/mutex.h>
#include <linux/types.h>

#define MCTP_BINDS_BITS 7

struct netns_mctp {
	/* Only updated under RTNL, entries freed via RCU */
	struct list_head routes;

	/* Bound sockets: hash table of sockets, keyed by
	 * (type, src_eid, dest_eid).
	 * Specific src_eid/dest_eid entries also have an entry for
	 * MCTP_ADDR_ANY. This list is updated from non-atomic contexts
	 * (under bind_lock), and read (under rcu) in packet rx.
	 */
	struct mutex bind_lock;
	DECLARE_HASHTABLE(binds, MCTP_BINDS_BITS);

	/* tag allocations. This list is read and updated from atomic contexts,
	 * but elements are free()ed after a RCU grace-period
	 */
	spinlock_t keys_lock;
	struct hlist_head keys;

	/* MCTP network */
	unsigned int default_net;

	/* neighbour table */
	struct mutex neigh_lock;
	struct list_head neighbours;
};

static inline u32 mctp_bind_hash(u8 type, u8 local_addr, u8 peer_addr)
{
	return hash_32(type | (u32)local_addr << 8 | (u32)peer_addr << 16,
		       MCTP_BINDS_BITS);
}

#endif /* __NETNS_MCTP_H__ */
