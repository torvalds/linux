/*
 * Network node table
 *
 * SELinux must keep a mapping of network nodes to labels/SIDs.  This
 * mapping is maintained as part of the normal policy but a fast cache is
 * needed to reduce the lookup overhead since most of these queries happen on
 * a per-packet basis.
 *
 * Author: Paul Moore <paul.moore@hp.com>
 *
 * This code is heavily based on the "netif" concept originally developed by
 * James Morris <jmorris@redhat.com>
 *   (see security/selinux/netif.c for more information)
 *
 */

/*
 * (c) Copyright Hewlett-Packard Development Company, L.P., 2007
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/types.h>
#include <linux/rcupdate.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <asm/bug.h>

#include "objsec.h"

#define SEL_NETNODE_HASH_SIZE       256
#define SEL_NETNODE_HASH_BKT_LIMIT   16

struct sel_netnode {
	struct netnode_security_struct nsec;

	struct list_head list;
	struct rcu_head rcu;
};

/* NOTE: we are using a combined hash table for both IPv4 and IPv6, the reason
 * for this is that I suspect most users will not make heavy use of both
 * address families at the same time so one table will usually end up wasted,
 * if this becomes a problem we can always add a hash table for each address
 * family later */

static LIST_HEAD(sel_netnode_list);
static DEFINE_SPINLOCK(sel_netnode_lock);
static struct list_head sel_netnode_hash[SEL_NETNODE_HASH_SIZE];

/**
 * sel_netnode_free - Frees a node entry
 * @p: the entry's RCU field
 *
 * Description:
 * This function is designed to be used as a callback to the call_rcu()
 * function so that memory allocated to a hash table node entry can be
 * released safely.
 *
 */
static void sel_netnode_free(struct rcu_head *p)
{
	struct sel_netnode *node = container_of(p, struct sel_netnode, rcu);
	kfree(node);
}

/**
 * sel_netnode_hashfn_ipv4 - IPv4 hashing function for the node table
 * @addr: IPv4 address
 *
 * Description:
 * This is the IPv4 hashing function for the node interface table, it returns
 * the bucket number for the given IP address.
 *
 */
static u32 sel_netnode_hashfn_ipv4(__be32 addr)
{
	/* at some point we should determine if the mismatch in byte order
	 * affects the hash function dramatically */
	return (addr & (SEL_NETNODE_HASH_SIZE - 1));
}

/**
 * sel_netnode_hashfn_ipv6 - IPv6 hashing function for the node table
 * @addr: IPv6 address
 *
 * Description:
 * This is the IPv6 hashing function for the node interface table, it returns
 * the bucket number for the given IP address.
 *
 */
static u32 sel_netnode_hashfn_ipv6(const struct in6_addr *addr)
{
	/* just hash the least significant 32 bits to keep things fast (they
	 * are the most likely to be different anyway), we can revisit this
	 * later if needed */
	return (addr->s6_addr32[3] & (SEL_NETNODE_HASH_SIZE - 1));
}

/**
 * sel_netnode_find - Search for a node record
 * @addr: IP address
 * @family: address family
 *
 * Description:
 * Search the network node table and return the record matching @addr.  If an
 * entry can not be found in the table return NULL.
 *
 */
static struct sel_netnode *sel_netnode_find(const void *addr, u16 family)
{
	u32 idx;
	struct sel_netnode *node;

	switch (family) {
	case PF_INET:
		idx = sel_netnode_hashfn_ipv4(*(__be32 *)addr);
		break;
	case PF_INET6:
		idx = sel_netnode_hashfn_ipv6(addr);
		break;
	default:
		BUG();
	}

	list_for_each_entry_rcu(node, &sel_netnode_hash[idx], list)
		if (node->nsec.family == family)
			switch (family) {
			case PF_INET:
				if (node->nsec.addr.ipv4 == *(__be32 *)addr)
					return node;
				break;
			case PF_INET6:
				if (ipv6_addr_equal(&node->nsec.addr.ipv6,
						    addr))
					return node;
				break;
			}

	return NULL;
}

/**
 * sel_netnode_insert - Insert a new node into the table
 * @node: the new node record
 *
 * Description:
 * Add a new node record to the network address hash table.  Returns zero on
 * success, negative values on failure.
 *
 */
static int sel_netnode_insert(struct sel_netnode *node)
{
	u32 idx;
	u32 count = 0;
	struct sel_netnode *iter;

	switch (node->nsec.family) {
	case PF_INET:
		idx = sel_netnode_hashfn_ipv4(node->nsec.addr.ipv4);
		break;
	case PF_INET6:
		idx = sel_netnode_hashfn_ipv6(&node->nsec.addr.ipv6);
		break;
	default:
		BUG();
	}
	list_add_rcu(&node->list, &sel_netnode_hash[idx]);

	/* we need to impose a limit on the growth of the hash table so check
	 * this bucket to make sure it is within the specified bounds */
	list_for_each_entry(iter, &sel_netnode_hash[idx], list)
		if (++count > SEL_NETNODE_HASH_BKT_LIMIT) {
			list_del_rcu(&iter->list);
			call_rcu(&iter->rcu, sel_netnode_free);
			break;
		}

	return 0;
}

/**
 * sel_netnode_destroy - Remove a node record from the table
 * @node: the existing node record
 *
 * Description:
 * Remove an existing node record from the network address table.
 *
 */
static void sel_netnode_destroy(struct sel_netnode *node)
{
	list_del_rcu(&node->list);
	call_rcu(&node->rcu, sel_netnode_free);
}

/**
 * sel_netnode_sid_slow - Lookup the SID of a network address using the policy
 * @addr: the IP address
 * @family: the address family
 * @sid: node SID
 *
 * Description:
 * This function determines the SID of a network address by quering the
 * security policy.  The result is added to the network address table to
 * speedup future queries.  Returns zero on success, negative values on
 * failure.
 *
 */
static int sel_netnode_sid_slow(void *addr, u16 family, u32 *sid)
{
	int ret;
	struct sel_netnode *node;
	struct sel_netnode *new = NULL;

	spin_lock_bh(&sel_netnode_lock);
	node = sel_netnode_find(addr, family);
	if (node != NULL) {
		*sid = node->nsec.sid;
		ret = 0;
		goto out;
	}
	new = kzalloc(sizeof(*new), GFP_ATOMIC);
	if (new == NULL) {
		ret = -ENOMEM;
		goto out;
	}
	switch (family) {
	case PF_INET:
		ret = security_node_sid(PF_INET,
					addr, sizeof(struct in_addr),
					&new->nsec.sid);
		new->nsec.addr.ipv4 = *(__be32 *)addr;
		break;
	case PF_INET6:
		ret = security_node_sid(PF_INET6,
					addr, sizeof(struct in6_addr),
					&new->nsec.sid);
		ipv6_addr_copy(&new->nsec.addr.ipv6, addr);
		break;
	default:
		BUG();
	}
	if (ret != 0)
		goto out;
	new->nsec.family = family;
	ret = sel_netnode_insert(new);
	if (ret != 0)
		goto out;
	*sid = new->nsec.sid;

out:
	spin_unlock_bh(&sel_netnode_lock);
	if (unlikely(ret)) {
		printk(KERN_WARNING
		       "SELinux: failure in sel_netnode_sid_slow(),"
		       " unable to determine network node label\n");
		kfree(new);
	}
	return ret;
}

/**
 * sel_netnode_sid - Lookup the SID of a network address
 * @addr: the IP address
 * @family: the address family
 * @sid: node SID
 *
 * Description:
 * This function determines the SID of a network address using the fastest
 * method possible.  First the address table is queried, but if an entry
 * can't be found then the policy is queried and the result is added to the
 * table to speedup future queries.  Returns zero on success, negative values
 * on failure.
 *
 */
int sel_netnode_sid(void *addr, u16 family, u32 *sid)
{
	struct sel_netnode *node;

	rcu_read_lock();
	node = sel_netnode_find(addr, family);
	if (node != NULL) {
		*sid = node->nsec.sid;
		rcu_read_unlock();
		return 0;
	}
	rcu_read_unlock();

	return sel_netnode_sid_slow(addr, family, sid);
}

/**
 * sel_netnode_flush - Flush the entire network address table
 *
 * Description:
 * Remove all entries from the network address table.
 *
 */
static void sel_netnode_flush(void)
{
	u32 idx;
	struct sel_netnode *node;

	spin_lock_bh(&sel_netnode_lock);
	for (idx = 0; idx < SEL_NETNODE_HASH_SIZE; idx++)
		list_for_each_entry(node, &sel_netnode_hash[idx], list)
			sel_netnode_destroy(node);
	spin_unlock_bh(&sel_netnode_lock);
}

static int sel_netnode_avc_callback(u32 event, u32 ssid, u32 tsid,
				    u16 class, u32 perms, u32 *retained)
{
	if (event == AVC_CALLBACK_RESET) {
		sel_netnode_flush();
		synchronize_net();
	}
	return 0;
}

static __init int sel_netnode_init(void)
{
	int iter;
	int ret;

	if (!selinux_enabled)
		return 0;

	for (iter = 0; iter < SEL_NETNODE_HASH_SIZE; iter++)
		INIT_LIST_HEAD(&sel_netnode_hash[iter]);

	ret = avc_add_callback(sel_netnode_avc_callback, AVC_CALLBACK_RESET,
			       SECSID_NULL, SECSID_NULL, SECCLASS_NULL, 0);
	if (ret != 0)
		panic("avc_add_callback() failed, error %d\n", ret);

	return ret;
}

__initcall(sel_netnode_init);
