// SPDX-License-Identifier: GPL-2.0-only
/*
 * Pkey table
 *
 * SELinux must keep a mapping of Infinband PKEYs to labels/SIDs.  This
 * mapping is maintained as part of the normal policy but a fast cache is
 * needed to reduce the lookup overhead.
 *
 * This code is heavily based on the "netif" and "netport" concept originally
 * developed by
 * James Morris <jmorris@redhat.com> and
 * Paul Moore <paul@paul-moore.com>
 *   (see security/selinux/netif.c and security/selinux/netport.c for more
 *   information)
 */

/*
 * (c) Mellanox Technologies, 2016
 */

#include <linux/types.h>
#include <linux/rcupdate.h>
#include <linux/list.h>
#include <linux/spinlock.h>

#include "ibpkey.h"
#include "objsec.h"

#define SEL_PKEY_HASH_SIZE       256
#define SEL_PKEY_HASH_BKT_LIMIT   16

struct sel_ib_pkey_bkt {
	int size;
	struct list_head list;
};

struct sel_ib_pkey {
	struct pkey_security_struct psec;
	struct list_head list;
	struct rcu_head rcu;
};

static LIST_HEAD(sel_ib_pkey_list);
static DEFINE_SPINLOCK(sel_ib_pkey_lock);
static struct sel_ib_pkey_bkt sel_ib_pkey_hash[SEL_PKEY_HASH_SIZE];

/**
 * sel_ib_pkey_hashfn - Hashing function for the pkey table
 * @pkey: pkey number
 *
 * Description:
 * This is the hashing function for the pkey table, it returns the bucket
 * number for the given pkey.
 *
 */
static unsigned int sel_ib_pkey_hashfn(u16 pkey)
{
	return (pkey & (SEL_PKEY_HASH_SIZE - 1));
}

/**
 * sel_ib_pkey_find - Search for a pkey record
 * @subnet_prefix: subnet_prefix
 * @pkey_num: pkey_num
 *
 * Description:
 * Search the pkey table and return the matching record.  If an entry
 * can not be found in the table return NULL.
 *
 */
static struct sel_ib_pkey *sel_ib_pkey_find(u64 subnet_prefix, u16 pkey_num)
{
	unsigned int idx;
	struct sel_ib_pkey *pkey;

	idx = sel_ib_pkey_hashfn(pkey_num);
	list_for_each_entry_rcu(pkey, &sel_ib_pkey_hash[idx].list, list) {
		if (pkey->psec.pkey == pkey_num &&
		    pkey->psec.subnet_prefix == subnet_prefix)
			return pkey;
	}

	return NULL;
}

/**
 * sel_ib_pkey_insert - Insert a new pkey into the table
 * @pkey: the new pkey record
 *
 * Description:
 * Add a new pkey record to the hash table.
 *
 */
static void sel_ib_pkey_insert(struct sel_ib_pkey *pkey)
{
	unsigned int idx;

	/* we need to impose a limit on the growth of the hash table so check
	 * this bucket to make sure it is within the specified bounds
	 */
	idx = sel_ib_pkey_hashfn(pkey->psec.pkey);
	list_add_rcu(&pkey->list, &sel_ib_pkey_hash[idx].list);
	if (sel_ib_pkey_hash[idx].size == SEL_PKEY_HASH_BKT_LIMIT) {
		struct sel_ib_pkey *tail;

		tail = list_entry(
			rcu_dereference_protected(
				sel_ib_pkey_hash[idx].list.prev,
				lockdep_is_held(&sel_ib_pkey_lock)),
			struct sel_ib_pkey, list);
		list_del_rcu(&tail->list);
		kfree_rcu(tail, rcu);
	} else {
		sel_ib_pkey_hash[idx].size++;
	}
}

/**
 * sel_ib_pkey_sid_slow - Lookup the SID of a pkey using the policy
 * @subnet_prefix: subnet prefix
 * @pkey_num: pkey number
 * @sid: pkey SID
 *
 * Description:
 * This function determines the SID of a pkey by querying the security
 * policy.  The result is added to the pkey table to speedup future
 * queries.  Returns zero on success, negative values on failure.
 *
 */
static int sel_ib_pkey_sid_slow(u64 subnet_prefix, u16 pkey_num, u32 *sid)
{
	int ret;
	struct sel_ib_pkey *pkey;
	struct sel_ib_pkey *new = NULL;
	unsigned long flags;

	spin_lock_irqsave(&sel_ib_pkey_lock, flags);
	pkey = sel_ib_pkey_find(subnet_prefix, pkey_num);
	if (pkey) {
		*sid = pkey->psec.sid;
		spin_unlock_irqrestore(&sel_ib_pkey_lock, flags);
		return 0;
	}

	ret = security_ib_pkey_sid(&selinux_state, subnet_prefix, pkey_num,
				   sid);
	if (ret)
		goto out;

	/* If this memory allocation fails still return 0. The SID
	 * is valid, it just won't be added to the cache.
	 */
	new = kzalloc(sizeof(*new), GFP_ATOMIC);
	if (!new) {
		ret = -ENOMEM;
		goto out;
	}

	new->psec.subnet_prefix = subnet_prefix;
	new->psec.pkey = pkey_num;
	new->psec.sid = *sid;
	sel_ib_pkey_insert(new);

out:
	spin_unlock_irqrestore(&sel_ib_pkey_lock, flags);
	return ret;
}

/**
 * sel_ib_pkey_sid - Lookup the SID of a PKEY
 * @subnet_prefix: subnet_prefix
 * @pkey_num: pkey number
 * @sid: pkey SID
 *
 * Description:
 * This function determines the SID of a PKEY using the fastest method
 * possible.  First the pkey table is queried, but if an entry can't be found
 * then the policy is queried and the result is added to the table to speedup
 * future queries.  Returns zero on success, negative values on failure.
 *
 */
int sel_ib_pkey_sid(u64 subnet_prefix, u16 pkey_num, u32 *sid)
{
	struct sel_ib_pkey *pkey;

	rcu_read_lock();
	pkey = sel_ib_pkey_find(subnet_prefix, pkey_num);
	if (pkey) {
		*sid = pkey->psec.sid;
		rcu_read_unlock();
		return 0;
	}
	rcu_read_unlock();

	return sel_ib_pkey_sid_slow(subnet_prefix, pkey_num, sid);
}

/**
 * sel_ib_pkey_flush - Flush the entire pkey table
 *
 * Description:
 * Remove all entries from the pkey table
 *
 */
void sel_ib_pkey_flush(void)
{
	unsigned int idx;
	struct sel_ib_pkey *pkey, *pkey_tmp;
	unsigned long flags;

	spin_lock_irqsave(&sel_ib_pkey_lock, flags);
	for (idx = 0; idx < SEL_PKEY_HASH_SIZE; idx++) {
		list_for_each_entry_safe(pkey, pkey_tmp,
					 &sel_ib_pkey_hash[idx].list, list) {
			list_del_rcu(&pkey->list);
			kfree_rcu(pkey, rcu);
		}
		sel_ib_pkey_hash[idx].size = 0;
	}
	spin_unlock_irqrestore(&sel_ib_pkey_lock, flags);
}

static __init int sel_ib_pkey_init(void)
{
	int iter;

	if (!selinux_enabled_boot)
		return 0;

	for (iter = 0; iter < SEL_PKEY_HASH_SIZE; iter++) {
		INIT_LIST_HEAD(&sel_ib_pkey_hash[iter].list);
		sel_ib_pkey_hash[iter].size = 0;
	}

	return 0;
}

subsys_initcall(sel_ib_pkey_init);
