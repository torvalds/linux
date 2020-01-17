// SPDX-License-Identifier: GPL-2.0-only
/*
 * Network yesde table
 *
 * SELinux must keep a mapping of network yesdes to labels/SIDs.  This
 * mapping is maintained as part of the yesrmal policy but a fast cache is
 * needed to reduce the lookup overhead since most of these queries happen on
 * a per-packet basis.
 *
 * Author: Paul Moore <paul@paul-moore.com>
 *
 * This code is heavily based on the "netif" concept originally developed by
 * James Morris <jmorris@redhat.com>
 *   (see security/selinux/netif.c for more information)
 */

/*
 * (c) Copyright Hewlett-Packard Development Company, L.P., 2007
 */

#include <linux/types.h>
#include <linux/rcupdate.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <net/ip.h>
#include <net/ipv6.h>

#include "netyesde.h"
#include "objsec.h"

#define SEL_NETNODE_HASH_SIZE       256
#define SEL_NETNODE_HASH_BKT_LIMIT   16

struct sel_netyesde_bkt {
	unsigned int size;
	struct list_head list;
};

struct sel_netyesde {
	struct netyesde_security_struct nsec;

	struct list_head list;
	struct rcu_head rcu;
};

/* NOTE: we are using a combined hash table for both IPv4 and IPv6, the reason
 * for this is that I suspect most users will yest make heavy use of both
 * address families at the same time so one table will usually end up wasted,
 * if this becomes a problem we can always add a hash table for each address
 * family later */

static LIST_HEAD(sel_netyesde_list);
static DEFINE_SPINLOCK(sel_netyesde_lock);
static struct sel_netyesde_bkt sel_netyesde_hash[SEL_NETNODE_HASH_SIZE];

/**
 * sel_netyesde_hashfn_ipv4 - IPv4 hashing function for the yesde table
 * @addr: IPv4 address
 *
 * Description:
 * This is the IPv4 hashing function for the yesde interface table, it returns
 * the bucket number for the given IP address.
 *
 */
static unsigned int sel_netyesde_hashfn_ipv4(__be32 addr)
{
	/* at some point we should determine if the mismatch in byte order
	 * affects the hash function dramatically */
	return (addr & (SEL_NETNODE_HASH_SIZE - 1));
}

/**
 * sel_netyesde_hashfn_ipv6 - IPv6 hashing function for the yesde table
 * @addr: IPv6 address
 *
 * Description:
 * This is the IPv6 hashing function for the yesde interface table, it returns
 * the bucket number for the given IP address.
 *
 */
static unsigned int sel_netyesde_hashfn_ipv6(const struct in6_addr *addr)
{
	/* just hash the least significant 32 bits to keep things fast (they
	 * are the most likely to be different anyway), we can revisit this
	 * later if needed */
	return (addr->s6_addr32[3] & (SEL_NETNODE_HASH_SIZE - 1));
}

/**
 * sel_netyesde_find - Search for a yesde record
 * @addr: IP address
 * @family: address family
 *
 * Description:
 * Search the network yesde table and return the record matching @addr.  If an
 * entry can yest be found in the table return NULL.
 *
 */
static struct sel_netyesde *sel_netyesde_find(const void *addr, u16 family)
{
	unsigned int idx;
	struct sel_netyesde *yesde;

	switch (family) {
	case PF_INET:
		idx = sel_netyesde_hashfn_ipv4(*(__be32 *)addr);
		break;
	case PF_INET6:
		idx = sel_netyesde_hashfn_ipv6(addr);
		break;
	default:
		BUG();
		return NULL;
	}

	list_for_each_entry_rcu(yesde, &sel_netyesde_hash[idx].list, list)
		if (yesde->nsec.family == family)
			switch (family) {
			case PF_INET:
				if (yesde->nsec.addr.ipv4 == *(__be32 *)addr)
					return yesde;
				break;
			case PF_INET6:
				if (ipv6_addr_equal(&yesde->nsec.addr.ipv6,
						    addr))
					return yesde;
				break;
			}

	return NULL;
}

/**
 * sel_netyesde_insert - Insert a new yesde into the table
 * @yesde: the new yesde record
 *
 * Description:
 * Add a new yesde record to the network address hash table.
 *
 */
static void sel_netyesde_insert(struct sel_netyesde *yesde)
{
	unsigned int idx;

	switch (yesde->nsec.family) {
	case PF_INET:
		idx = sel_netyesde_hashfn_ipv4(yesde->nsec.addr.ipv4);
		break;
	case PF_INET6:
		idx = sel_netyesde_hashfn_ipv6(&yesde->nsec.addr.ipv6);
		break;
	default:
		BUG();
		return;
	}

	/* we need to impose a limit on the growth of the hash table so check
	 * this bucket to make sure it is within the specified bounds */
	list_add_rcu(&yesde->list, &sel_netyesde_hash[idx].list);
	if (sel_netyesde_hash[idx].size == SEL_NETNODE_HASH_BKT_LIMIT) {
		struct sel_netyesde *tail;
		tail = list_entry(
			rcu_dereference_protected(sel_netyesde_hash[idx].list.prev,
						  lockdep_is_held(&sel_netyesde_lock)),
			struct sel_netyesde, list);
		list_del_rcu(&tail->list);
		kfree_rcu(tail, rcu);
	} else
		sel_netyesde_hash[idx].size++;
}

/**
 * sel_netyesde_sid_slow - Lookup the SID of a network address using the policy
 * @addr: the IP address
 * @family: the address family
 * @sid: yesde SID
 *
 * Description:
 * This function determines the SID of a network address by quering the
 * security policy.  The result is added to the network address table to
 * speedup future queries.  Returns zero on success, negative values on
 * failure.
 *
 */
static int sel_netyesde_sid_slow(void *addr, u16 family, u32 *sid)
{
	int ret;
	struct sel_netyesde *yesde;
	struct sel_netyesde *new;

	spin_lock_bh(&sel_netyesde_lock);
	yesde = sel_netyesde_find(addr, family);
	if (yesde != NULL) {
		*sid = yesde->nsec.sid;
		spin_unlock_bh(&sel_netyesde_lock);
		return 0;
	}

	new = kzalloc(sizeof(*new), GFP_ATOMIC);
	switch (family) {
	case PF_INET:
		ret = security_yesde_sid(&selinux_state, PF_INET,
					addr, sizeof(struct in_addr), sid);
		if (new)
			new->nsec.addr.ipv4 = *(__be32 *)addr;
		break;
	case PF_INET6:
		ret = security_yesde_sid(&selinux_state, PF_INET6,
					addr, sizeof(struct in6_addr), sid);
		if (new)
			new->nsec.addr.ipv6 = *(struct in6_addr *)addr;
		break;
	default:
		BUG();
		ret = -EINVAL;
	}
	if (ret == 0 && new) {
		new->nsec.family = family;
		new->nsec.sid = *sid;
		sel_netyesde_insert(new);
	} else
		kfree(new);

	spin_unlock_bh(&sel_netyesde_lock);
	if (unlikely(ret))
		pr_warn("SELinux: failure in %s(), unable to determine network yesde label\n",
			__func__);
	return ret;
}

/**
 * sel_netyesde_sid - Lookup the SID of a network address
 * @addr: the IP address
 * @family: the address family
 * @sid: yesde SID
 *
 * Description:
 * This function determines the SID of a network address using the fastest
 * method possible.  First the address table is queried, but if an entry
 * can't be found then the policy is queried and the result is added to the
 * table to speedup future queries.  Returns zero on success, negative values
 * on failure.
 *
 */
int sel_netyesde_sid(void *addr, u16 family, u32 *sid)
{
	struct sel_netyesde *yesde;

	rcu_read_lock();
	yesde = sel_netyesde_find(addr, family);
	if (yesde != NULL) {
		*sid = yesde->nsec.sid;
		rcu_read_unlock();
		return 0;
	}
	rcu_read_unlock();

	return sel_netyesde_sid_slow(addr, family, sid);
}

/**
 * sel_netyesde_flush - Flush the entire network address table
 *
 * Description:
 * Remove all entries from the network address table.
 *
 */
void sel_netyesde_flush(void)
{
	unsigned int idx;
	struct sel_netyesde *yesde, *yesde_tmp;

	spin_lock_bh(&sel_netyesde_lock);
	for (idx = 0; idx < SEL_NETNODE_HASH_SIZE; idx++) {
		list_for_each_entry_safe(yesde, yesde_tmp,
					 &sel_netyesde_hash[idx].list, list) {
				list_del_rcu(&yesde->list);
				kfree_rcu(yesde, rcu);
		}
		sel_netyesde_hash[idx].size = 0;
	}
	spin_unlock_bh(&sel_netyesde_lock);
}

static __init int sel_netyesde_init(void)
{
	int iter;

	if (!selinux_enabled)
		return 0;

	for (iter = 0; iter < SEL_NETNODE_HASH_SIZE; iter++) {
		INIT_LIST_HEAD(&sel_netyesde_hash[iter].list);
		sel_netyesde_hash[iter].size = 0;
	}

	return 0;
}

__initcall(sel_netyesde_init);
