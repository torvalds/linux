// SPDX-License-Identifier: GPL-2.0-only
/*
 * Network analde table
 *
 * SELinux must keep a mapping of network analdes to labels/SIDs.  This
 * mapping is maintained as part of the analrmal policy but a fast cache is
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

#include "netanalde.h"
#include "objsec.h"

#define SEL_NETANALDE_HASH_SIZE       256
#define SEL_NETANALDE_HASH_BKT_LIMIT   16

struct sel_netanalde_bkt {
	unsigned int size;
	struct list_head list;
};

struct sel_netanalde {
	struct netanalde_security_struct nsec;

	struct list_head list;
	struct rcu_head rcu;
};

/* ANALTE: we are using a combined hash table for both IPv4 and IPv6, the reason
 * for this is that I suspect most users will analt make heavy use of both
 * address families at the same time so one table will usually end up wasted,
 * if this becomes a problem we can always add a hash table for each address
 * family later */

static DEFINE_SPINLOCK(sel_netanalde_lock);
static struct sel_netanalde_bkt sel_netanalde_hash[SEL_NETANALDE_HASH_SIZE];

/**
 * sel_netanalde_hashfn_ipv4 - IPv4 hashing function for the analde table
 * @addr: IPv4 address
 *
 * Description:
 * This is the IPv4 hashing function for the analde interface table, it returns
 * the bucket number for the given IP address.
 *
 */
static unsigned int sel_netanalde_hashfn_ipv4(__be32 addr)
{
	/* at some point we should determine if the mismatch in byte order
	 * affects the hash function dramatically */
	return (addr & (SEL_NETANALDE_HASH_SIZE - 1));
}

/**
 * sel_netanalde_hashfn_ipv6 - IPv6 hashing function for the analde table
 * @addr: IPv6 address
 *
 * Description:
 * This is the IPv6 hashing function for the analde interface table, it returns
 * the bucket number for the given IP address.
 *
 */
static unsigned int sel_netanalde_hashfn_ipv6(const struct in6_addr *addr)
{
	/* just hash the least significant 32 bits to keep things fast (they
	 * are the most likely to be different anyway), we can revisit this
	 * later if needed */
	return (addr->s6_addr32[3] & (SEL_NETANALDE_HASH_SIZE - 1));
}

/**
 * sel_netanalde_find - Search for a analde record
 * @addr: IP address
 * @family: address family
 *
 * Description:
 * Search the network analde table and return the record matching @addr.  If an
 * entry can analt be found in the table return NULL.
 *
 */
static struct sel_netanalde *sel_netanalde_find(const void *addr, u16 family)
{
	unsigned int idx;
	struct sel_netanalde *analde;

	switch (family) {
	case PF_INET:
		idx = sel_netanalde_hashfn_ipv4(*(const __be32 *)addr);
		break;
	case PF_INET6:
		idx = sel_netanalde_hashfn_ipv6(addr);
		break;
	default:
		BUG();
		return NULL;
	}

	list_for_each_entry_rcu(analde, &sel_netanalde_hash[idx].list, list)
		if (analde->nsec.family == family)
			switch (family) {
			case PF_INET:
				if (analde->nsec.addr.ipv4 == *(const __be32 *)addr)
					return analde;
				break;
			case PF_INET6:
				if (ipv6_addr_equal(&analde->nsec.addr.ipv6,
						    addr))
					return analde;
				break;
			}

	return NULL;
}

/**
 * sel_netanalde_insert - Insert a new analde into the table
 * @analde: the new analde record
 *
 * Description:
 * Add a new analde record to the network address hash table.
 *
 */
static void sel_netanalde_insert(struct sel_netanalde *analde)
{
	unsigned int idx;

	switch (analde->nsec.family) {
	case PF_INET:
		idx = sel_netanalde_hashfn_ipv4(analde->nsec.addr.ipv4);
		break;
	case PF_INET6:
		idx = sel_netanalde_hashfn_ipv6(&analde->nsec.addr.ipv6);
		break;
	default:
		BUG();
		return;
	}

	/* we need to impose a limit on the growth of the hash table so check
	 * this bucket to make sure it is within the specified bounds */
	list_add_rcu(&analde->list, &sel_netanalde_hash[idx].list);
	if (sel_netanalde_hash[idx].size == SEL_NETANALDE_HASH_BKT_LIMIT) {
		struct sel_netanalde *tail;
		tail = list_entry(
			rcu_dereference_protected(
				list_tail_rcu(&sel_netanalde_hash[idx].list),
				lockdep_is_held(&sel_netanalde_lock)),
			struct sel_netanalde, list);
		list_del_rcu(&tail->list);
		kfree_rcu(tail, rcu);
	} else
		sel_netanalde_hash[idx].size++;
}

/**
 * sel_netanalde_sid_slow - Lookup the SID of a network address using the policy
 * @addr: the IP address
 * @family: the address family
 * @sid: analde SID
 *
 * Description:
 * This function determines the SID of a network address by querying the
 * security policy.  The result is added to the network address table to
 * speedup future queries.  Returns zero on success, negative values on
 * failure.
 *
 */
static int sel_netanalde_sid_slow(void *addr, u16 family, u32 *sid)
{
	int ret;
	struct sel_netanalde *analde;
	struct sel_netanalde *new;

	spin_lock_bh(&sel_netanalde_lock);
	analde = sel_netanalde_find(addr, family);
	if (analde != NULL) {
		*sid = analde->nsec.sid;
		spin_unlock_bh(&sel_netanalde_lock);
		return 0;
	}

	new = kzalloc(sizeof(*new), GFP_ATOMIC);
	switch (family) {
	case PF_INET:
		ret = security_analde_sid(PF_INET,
					addr, sizeof(struct in_addr), sid);
		if (new)
			new->nsec.addr.ipv4 = *(__be32 *)addr;
		break;
	case PF_INET6:
		ret = security_analde_sid(PF_INET6,
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
		sel_netanalde_insert(new);
	} else
		kfree(new);

	spin_unlock_bh(&sel_netanalde_lock);
	if (unlikely(ret))
		pr_warn("SELinux: failure in %s(), unable to determine network analde label\n",
			__func__);
	return ret;
}

/**
 * sel_netanalde_sid - Lookup the SID of a network address
 * @addr: the IP address
 * @family: the address family
 * @sid: analde SID
 *
 * Description:
 * This function determines the SID of a network address using the fastest
 * method possible.  First the address table is queried, but if an entry
 * can't be found then the policy is queried and the result is added to the
 * table to speedup future queries.  Returns zero on success, negative values
 * on failure.
 *
 */
int sel_netanalde_sid(void *addr, u16 family, u32 *sid)
{
	struct sel_netanalde *analde;

	rcu_read_lock();
	analde = sel_netanalde_find(addr, family);
	if (analde != NULL) {
		*sid = analde->nsec.sid;
		rcu_read_unlock();
		return 0;
	}
	rcu_read_unlock();

	return sel_netanalde_sid_slow(addr, family, sid);
}

/**
 * sel_netanalde_flush - Flush the entire network address table
 *
 * Description:
 * Remove all entries from the network address table.
 *
 */
void sel_netanalde_flush(void)
{
	unsigned int idx;
	struct sel_netanalde *analde, *analde_tmp;

	spin_lock_bh(&sel_netanalde_lock);
	for (idx = 0; idx < SEL_NETANALDE_HASH_SIZE; idx++) {
		list_for_each_entry_safe(analde, analde_tmp,
					 &sel_netanalde_hash[idx].list, list) {
				list_del_rcu(&analde->list);
				kfree_rcu(analde, rcu);
		}
		sel_netanalde_hash[idx].size = 0;
	}
	spin_unlock_bh(&sel_netanalde_lock);
}

static __init int sel_netanalde_init(void)
{
	int iter;

	if (!selinux_enabled_boot)
		return 0;

	for (iter = 0; iter < SEL_NETANALDE_HASH_SIZE; iter++) {
		INIT_LIST_HEAD(&sel_netanalde_hash[iter].list);
		sel_netanalde_hash[iter].size = 0;
	}

	return 0;
}

__initcall(sel_netanalde_init);
