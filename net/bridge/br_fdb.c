// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	Forwarding database
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Lennert Buytenhek		<buytenh@gnu.org>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/rculist.h>
#include <linux/spinlock.h>
#include <linux/times.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/jhash.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/atomic.h>
#include <asm/unaligned.h>
#include <linux/if_vlan.h>
#include <net/switchdev.h>
#include <trace/events/bridge.h>
#include "br_private.h"

static const struct rhashtable_params br_fdb_rht_params = {
	.head_offset = offsetof(struct net_bridge_fdb_entry, rhnode),
	.key_offset = offsetof(struct net_bridge_fdb_entry, key),
	.key_len = sizeof(struct net_bridge_fdb_key),
	.automatic_shrinking = true,
};

static struct kmem_cache *br_fdb_cache __read_mostly;

int __init br_fdb_init(void)
{
	br_fdb_cache = KMEM_CACHE(net_bridge_fdb_entry, SLAB_HWCACHE_ALIGN);
	if (!br_fdb_cache)
		return -ENOMEM;

	return 0;
}

void br_fdb_fini(void)
{
	kmem_cache_destroy(br_fdb_cache);
}

int br_fdb_hash_init(struct net_bridge *br)
{
	return rhashtable_init(&br->fdb_hash_tbl, &br_fdb_rht_params);
}

void br_fdb_hash_fini(struct net_bridge *br)
{
	rhashtable_destroy(&br->fdb_hash_tbl);
}

/* if topology_changing then use forward_delay (default 15 sec)
 * otherwise keep longer (default 5 minutes)
 */
static inline unsigned long hold_time(const struct net_bridge *br)
{
	return br->topology_change ? br->forward_delay : br->ageing_time;
}

static inline int has_expired(const struct net_bridge *br,
				  const struct net_bridge_fdb_entry *fdb)
{
	return !test_bit(BR_FDB_STATIC, &fdb->flags) &&
	       !test_bit(BR_FDB_ADDED_BY_EXT_LEARN, &fdb->flags) &&
	       time_before_eq(fdb->updated + hold_time(br), jiffies);
}

static void fdb_rcu_free(struct rcu_head *head)
{
	struct net_bridge_fdb_entry *ent
		= container_of(head, struct net_bridge_fdb_entry, rcu);
	kmem_cache_free(br_fdb_cache, ent);
}

static int fdb_to_nud(const struct net_bridge *br,
		      const struct net_bridge_fdb_entry *fdb)
{
	if (test_bit(BR_FDB_LOCAL, &fdb->flags))
		return NUD_PERMANENT;
	else if (test_bit(BR_FDB_STATIC, &fdb->flags))
		return NUD_NOARP;
	else if (has_expired(br, fdb))
		return NUD_STALE;
	else
		return NUD_REACHABLE;
}

static int fdb_fill_info(struct sk_buff *skb, const struct net_bridge *br,
			 const struct net_bridge_fdb_entry *fdb,
			 u32 portid, u32 seq, int type, unsigned int flags)
{
	const struct net_bridge_port *dst = READ_ONCE(fdb->dst);
	unsigned long now = jiffies;
	struct nda_cacheinfo ci;
	struct nlmsghdr *nlh;
	struct ndmsg *ndm;
	u32 ext_flags = 0;

	nlh = nlmsg_put(skb, portid, seq, type, sizeof(*ndm), flags);
	if (nlh == NULL)
		return -EMSGSIZE;

	ndm = nlmsg_data(nlh);
	ndm->ndm_family	 = AF_BRIDGE;
	ndm->ndm_pad1    = 0;
	ndm->ndm_pad2    = 0;
	ndm->ndm_flags	 = 0;
	ndm->ndm_type	 = 0;
	ndm->ndm_ifindex = dst ? dst->dev->ifindex : br->dev->ifindex;
	ndm->ndm_state   = fdb_to_nud(br, fdb);

	if (test_bit(BR_FDB_OFFLOADED, &fdb->flags))
		ndm->ndm_flags |= NTF_OFFLOADED;
	if (test_bit(BR_FDB_ADDED_BY_EXT_LEARN, &fdb->flags))
		ndm->ndm_flags |= NTF_EXT_LEARNED;
	if (test_bit(BR_FDB_STICKY, &fdb->flags))
		ndm->ndm_flags |= NTF_STICKY;
	if (test_bit(BR_FDB_LOCKED, &fdb->flags))
		ext_flags |= NTF_EXT_LOCKED;

	if (nla_put(skb, NDA_LLADDR, ETH_ALEN, &fdb->key.addr))
		goto nla_put_failure;
	if (nla_put_u32(skb, NDA_MASTER, br->dev->ifindex))
		goto nla_put_failure;
	if (nla_put_u32(skb, NDA_FLAGS_EXT, ext_flags))
		goto nla_put_failure;

	ci.ndm_used	 = jiffies_to_clock_t(now - fdb->used);
	ci.ndm_confirmed = 0;
	ci.ndm_updated	 = jiffies_to_clock_t(now - fdb->updated);
	ci.ndm_refcnt	 = 0;
	if (nla_put(skb, NDA_CACHEINFO, sizeof(ci), &ci))
		goto nla_put_failure;

	if (fdb->key.vlan_id && nla_put(skb, NDA_VLAN, sizeof(u16),
					&fdb->key.vlan_id))
		goto nla_put_failure;

	if (test_bit(BR_FDB_NOTIFY, &fdb->flags)) {
		struct nlattr *nest = nla_nest_start(skb, NDA_FDB_EXT_ATTRS);
		u8 notify_bits = FDB_NOTIFY_BIT;

		if (!nest)
			goto nla_put_failure;
		if (test_bit(BR_FDB_NOTIFY_INACTIVE, &fdb->flags))
			notify_bits |= FDB_NOTIFY_INACTIVE_BIT;

		if (nla_put_u8(skb, NFEA_ACTIVITY_NOTIFY, notify_bits)) {
			nla_nest_cancel(skb, nest);
			goto nla_put_failure;
		}

		nla_nest_end(skb, nest);
	}

	nlmsg_end(skb, nlh);
	return 0;

nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static inline size_t fdb_nlmsg_size(void)
{
	return NLMSG_ALIGN(sizeof(struct ndmsg))
		+ nla_total_size(ETH_ALEN) /* NDA_LLADDR */
		+ nla_total_size(sizeof(u32)) /* NDA_MASTER */
		+ nla_total_size(sizeof(u32)) /* NDA_FLAGS_EXT */
		+ nla_total_size(sizeof(u16)) /* NDA_VLAN */
		+ nla_total_size(sizeof(struct nda_cacheinfo))
		+ nla_total_size(0) /* NDA_FDB_EXT_ATTRS */
		+ nla_total_size(sizeof(u8)); /* NFEA_ACTIVITY_NOTIFY */
}

static void fdb_notify(struct net_bridge *br,
		       const struct net_bridge_fdb_entry *fdb, int type,
		       bool swdev_notify)
{
	struct net *net = dev_net(br->dev);
	struct sk_buff *skb;
	int err = -ENOBUFS;

	if (swdev_notify)
		br_switchdev_fdb_notify(br, fdb, type);

	skb = nlmsg_new(fdb_nlmsg_size(), GFP_ATOMIC);
	if (skb == NULL)
		goto errout;

	err = fdb_fill_info(skb, br, fdb, 0, 0, type, 0);
	if (err < 0) {
		/* -EMSGSIZE implies BUG in fdb_nlmsg_size() */
		WARN_ON(err == -EMSGSIZE);
		kfree_skb(skb);
		goto errout;
	}
	rtnl_notify(skb, net, 0, RTNLGRP_NEIGH, NULL, GFP_ATOMIC);
	return;
errout:
	rtnl_set_sk_err(net, RTNLGRP_NEIGH, err);
}

static struct net_bridge_fdb_entry *fdb_find_rcu(struct rhashtable *tbl,
						 const unsigned char *addr,
						 __u16 vid)
{
	struct net_bridge_fdb_key key;

	WARN_ON_ONCE(!rcu_read_lock_held());

	key.vlan_id = vid;
	memcpy(key.addr.addr, addr, sizeof(key.addr.addr));

	return rhashtable_lookup(tbl, &key, br_fdb_rht_params);
}

/* requires bridge hash_lock */
static struct net_bridge_fdb_entry *br_fdb_find(struct net_bridge *br,
						const unsigned char *addr,
						__u16 vid)
{
	struct net_bridge_fdb_entry *fdb;

	lockdep_assert_held_once(&br->hash_lock);

	rcu_read_lock();
	fdb = fdb_find_rcu(&br->fdb_hash_tbl, addr, vid);
	rcu_read_unlock();

	return fdb;
}

struct net_device *br_fdb_find_port(const struct net_device *br_dev,
				    const unsigned char *addr,
				    __u16 vid)
{
	struct net_bridge_fdb_entry *f;
	struct net_device *dev = NULL;
	struct net_bridge *br;

	ASSERT_RTNL();

	if (!netif_is_bridge_master(br_dev))
		return NULL;

	br = netdev_priv(br_dev);
	rcu_read_lock();
	f = br_fdb_find_rcu(br, addr, vid);
	if (f && f->dst)
		dev = f->dst->dev;
	rcu_read_unlock();

	return dev;
}
EXPORT_SYMBOL_GPL(br_fdb_find_port);

struct net_bridge_fdb_entry *br_fdb_find_rcu(struct net_bridge *br,
					     const unsigned char *addr,
					     __u16 vid)
{
	return fdb_find_rcu(&br->fdb_hash_tbl, addr, vid);
}

/* When a static FDB entry is added, the mac address from the entry is
 * added to the bridge private HW address list and all required ports
 * are then updated with the new information.
 * Called under RTNL.
 */
static void fdb_add_hw_addr(struct net_bridge *br, const unsigned char *addr)
{
	int err;
	struct net_bridge_port *p;

	ASSERT_RTNL();

	list_for_each_entry(p, &br->port_list, list) {
		if (!br_promisc_port(p)) {
			err = dev_uc_add(p->dev, addr);
			if (err)
				goto undo;
		}
	}

	return;
undo:
	list_for_each_entry_continue_reverse(p, &br->port_list, list) {
		if (!br_promisc_port(p))
			dev_uc_del(p->dev, addr);
	}
}

/* When a static FDB entry is deleted, the HW address from that entry is
 * also removed from the bridge private HW address list and updates all
 * the ports with needed information.
 * Called under RTNL.
 */
static void fdb_del_hw_addr(struct net_bridge *br, const unsigned char *addr)
{
	struct net_bridge_port *p;

	ASSERT_RTNL();

	list_for_each_entry(p, &br->port_list, list) {
		if (!br_promisc_port(p))
			dev_uc_del(p->dev, addr);
	}
}

static void fdb_delete(struct net_bridge *br, struct net_bridge_fdb_entry *f,
		       bool swdev_notify)
{
	trace_fdb_delete(br, f);

	if (test_bit(BR_FDB_STATIC, &f->flags))
		fdb_del_hw_addr(br, f->key.addr.addr);

	hlist_del_init_rcu(&f->fdb_node);
	rhashtable_remove_fast(&br->fdb_hash_tbl, &f->rhnode,
			       br_fdb_rht_params);
	if (test_and_clear_bit(BR_FDB_DYNAMIC_LEARNED, &f->flags))
		atomic_dec(&br->fdb_n_learned);
	fdb_notify(br, f, RTM_DELNEIGH, swdev_notify);
	call_rcu(&f->rcu, fdb_rcu_free);
}

/* Delete a local entry if no other port had the same address.
 *
 * This function should only be called on entries with BR_FDB_LOCAL set,
 * so even with BR_FDB_ADDED_BY_USER cleared we never need to increase
 * the accounting for dynamically learned entries again.
 */
static void fdb_delete_local(struct net_bridge *br,
			     const struct net_bridge_port *p,
			     struct net_bridge_fdb_entry *f)
{
	const unsigned char *addr = f->key.addr.addr;
	struct net_bridge_vlan_group *vg;
	const struct net_bridge_vlan *v;
	struct net_bridge_port *op;
	u16 vid = f->key.vlan_id;

	/* Maybe another port has same hw addr? */
	list_for_each_entry(op, &br->port_list, list) {
		vg = nbp_vlan_group(op);
		if (op != p && ether_addr_equal(op->dev->dev_addr, addr) &&
		    (!vid || br_vlan_find(vg, vid))) {
			f->dst = op;
			clear_bit(BR_FDB_ADDED_BY_USER, &f->flags);
			return;
		}
	}

	vg = br_vlan_group(br);
	v = br_vlan_find(vg, vid);
	/* Maybe bridge device has same hw addr? */
	if (p && ether_addr_equal(br->dev->dev_addr, addr) &&
	    (!vid || (v && br_vlan_should_use(v)))) {
		f->dst = NULL;
		clear_bit(BR_FDB_ADDED_BY_USER, &f->flags);
		return;
	}

	fdb_delete(br, f, true);
}

void br_fdb_find_delete_local(struct net_bridge *br,
			      const struct net_bridge_port *p,
			      const unsigned char *addr, u16 vid)
{
	struct net_bridge_fdb_entry *f;

	spin_lock_bh(&br->hash_lock);
	f = br_fdb_find(br, addr, vid);
	if (f && test_bit(BR_FDB_LOCAL, &f->flags) &&
	    !test_bit(BR_FDB_ADDED_BY_USER, &f->flags) && f->dst == p)
		fdb_delete_local(br, p, f);
	spin_unlock_bh(&br->hash_lock);
}

static struct net_bridge_fdb_entry *fdb_create(struct net_bridge *br,
					       struct net_bridge_port *source,
					       const unsigned char *addr,
					       __u16 vid,
					       unsigned long flags)
{
	bool learned = !test_bit(BR_FDB_ADDED_BY_USER, &flags) &&
		       !test_bit(BR_FDB_LOCAL, &flags);
	u32 max_learned = READ_ONCE(br->fdb_max_learned);
	struct net_bridge_fdb_entry *fdb;
	int err;

	if (likely(learned)) {
		int n_learned = atomic_read(&br->fdb_n_learned);

		if (unlikely(max_learned && n_learned >= max_learned))
			return NULL;
		__set_bit(BR_FDB_DYNAMIC_LEARNED, &flags);
	}

	fdb = kmem_cache_alloc(br_fdb_cache, GFP_ATOMIC);
	if (!fdb)
		return NULL;

	memcpy(fdb->key.addr.addr, addr, ETH_ALEN);
	WRITE_ONCE(fdb->dst, source);
	fdb->key.vlan_id = vid;
	fdb->flags = flags;
	fdb->updated = fdb->used = jiffies;
	err = rhashtable_lookup_insert_fast(&br->fdb_hash_tbl, &fdb->rhnode,
					    br_fdb_rht_params);
	if (err) {
		kmem_cache_free(br_fdb_cache, fdb);
		return NULL;
	}

	if (likely(learned))
		atomic_inc(&br->fdb_n_learned);

	hlist_add_head_rcu(&fdb->fdb_node, &br->fdb_list);

	return fdb;
}

static int fdb_add_local(struct net_bridge *br, struct net_bridge_port *source,
			 const unsigned char *addr, u16 vid)
{
	struct net_bridge_fdb_entry *fdb;

	if (!is_valid_ether_addr(addr))
		return -EINVAL;

	fdb = br_fdb_find(br, addr, vid);
	if (fdb) {
		/* it is okay to have multiple ports with same
		 * address, just use the first one.
		 */
		if (test_bit(BR_FDB_LOCAL, &fdb->flags))
			return 0;
		br_warn(br, "adding interface %s with same address as a received packet (addr:%pM, vlan:%u)\n",
			source ? source->dev->name : br->dev->name, addr, vid);
		fdb_delete(br, fdb, true);
	}

	fdb = fdb_create(br, source, addr, vid,
			 BIT(BR_FDB_LOCAL) | BIT(BR_FDB_STATIC));
	if (!fdb)
		return -ENOMEM;

	fdb_add_hw_addr(br, addr);
	fdb_notify(br, fdb, RTM_NEWNEIGH, true);
	return 0;
}

void br_fdb_changeaddr(struct net_bridge_port *p, const unsigned char *newaddr)
{
	struct net_bridge_vlan_group *vg;
	struct net_bridge_fdb_entry *f;
	struct net_bridge *br = p->br;
	struct net_bridge_vlan *v;

	spin_lock_bh(&br->hash_lock);
	vg = nbp_vlan_group(p);
	hlist_for_each_entry(f, &br->fdb_list, fdb_node) {
		if (f->dst == p && test_bit(BR_FDB_LOCAL, &f->flags) &&
		    !test_bit(BR_FDB_ADDED_BY_USER, &f->flags)) {
			/* delete old one */
			fdb_delete_local(br, p, f);

			/* if this port has no vlan information
			 * configured, we can safely be done at
			 * this point.
			 */
			if (!vg || !vg->num_vlans)
				goto insert;
		}
	}

insert:
	/* insert new address,  may fail if invalid address or dup. */
	fdb_add_local(br, p, newaddr, 0);

	if (!vg || !vg->num_vlans)
		goto done;

	/* Now add entries for every VLAN configured on the port.
	 * This function runs under RTNL so the bitmap will not change
	 * from under us.
	 */
	list_for_each_entry(v, &vg->vlan_list, vlist)
		fdb_add_local(br, p, newaddr, v->vid);

done:
	spin_unlock_bh(&br->hash_lock);
}

void br_fdb_change_mac_address(struct net_bridge *br, const u8 *newaddr)
{
	struct net_bridge_vlan_group *vg;
	struct net_bridge_fdb_entry *f;
	struct net_bridge_vlan *v;

	spin_lock_bh(&br->hash_lock);

	/* If old entry was unassociated with any port, then delete it. */
	f = br_fdb_find(br, br->dev->dev_addr, 0);
	if (f && test_bit(BR_FDB_LOCAL, &f->flags) &&
	    !f->dst && !test_bit(BR_FDB_ADDED_BY_USER, &f->flags))
		fdb_delete_local(br, NULL, f);

	fdb_add_local(br, NULL, newaddr, 0);
	vg = br_vlan_group(br);
	if (!vg || !vg->num_vlans)
		goto out;
	/* Now remove and add entries for every VLAN configured on the
	 * bridge.  This function runs under RTNL so the bitmap will not
	 * change from under us.
	 */
	list_for_each_entry(v, &vg->vlan_list, vlist) {
		if (!br_vlan_should_use(v))
			continue;
		f = br_fdb_find(br, br->dev->dev_addr, v->vid);
		if (f && test_bit(BR_FDB_LOCAL, &f->flags) &&
		    !f->dst && !test_bit(BR_FDB_ADDED_BY_USER, &f->flags))
			fdb_delete_local(br, NULL, f);
		fdb_add_local(br, NULL, newaddr, v->vid);
	}
out:
	spin_unlock_bh(&br->hash_lock);
}

void br_fdb_cleanup(struct work_struct *work)
{
	struct net_bridge *br = container_of(work, struct net_bridge,
					     gc_work.work);
	struct net_bridge_fdb_entry *f = NULL;
	unsigned long delay = hold_time(br);
	unsigned long work_delay = delay;
	unsigned long now = jiffies;

	/* this part is tricky, in order to avoid blocking learning and
	 * consequently forwarding, we rely on rcu to delete objects with
	 * delayed freeing allowing us to continue traversing
	 */
	rcu_read_lock();
	hlist_for_each_entry_rcu(f, &br->fdb_list, fdb_node) {
		unsigned long this_timer = f->updated + delay;

		if (test_bit(BR_FDB_STATIC, &f->flags) ||
		    test_bit(BR_FDB_ADDED_BY_EXT_LEARN, &f->flags)) {
			if (test_bit(BR_FDB_NOTIFY, &f->flags)) {
				if (time_after(this_timer, now))
					work_delay = min(work_delay,
							 this_timer - now);
				else if (!test_and_set_bit(BR_FDB_NOTIFY_INACTIVE,
							   &f->flags))
					fdb_notify(br, f, RTM_NEWNEIGH, false);
			}
			continue;
		}

		if (time_after(this_timer, now)) {
			work_delay = min(work_delay, this_timer - now);
		} else {
			spin_lock_bh(&br->hash_lock);
			if (!hlist_unhashed(&f->fdb_node))
				fdb_delete(br, f, true);
			spin_unlock_bh(&br->hash_lock);
		}
	}
	rcu_read_unlock();

	/* Cleanup minimum 10 milliseconds apart */
	work_delay = max_t(unsigned long, work_delay, msecs_to_jiffies(10));
	mod_delayed_work(system_long_wq, &br->gc_work, work_delay);
}

static bool __fdb_flush_matches(const struct net_bridge *br,
				const struct net_bridge_fdb_entry *f,
				const struct net_bridge_fdb_flush_desc *desc)
{
	const struct net_bridge_port *dst = READ_ONCE(f->dst);
	int port_ifidx = dst ? dst->dev->ifindex : br->dev->ifindex;

	if (desc->vlan_id && desc->vlan_id != f->key.vlan_id)
		return false;
	if (desc->port_ifindex && desc->port_ifindex != port_ifidx)
		return false;
	if (desc->flags_mask && (f->flags & desc->flags_mask) != desc->flags)
		return false;

	return true;
}

/* Flush forwarding database entries matching the description */
void br_fdb_flush(struct net_bridge *br,
		  const struct net_bridge_fdb_flush_desc *desc)
{
	struct net_bridge_fdb_entry *f;

	rcu_read_lock();
	hlist_for_each_entry_rcu(f, &br->fdb_list, fdb_node) {
		if (!__fdb_flush_matches(br, f, desc))
			continue;

		spin_lock_bh(&br->hash_lock);
		if (!hlist_unhashed(&f->fdb_node))
			fdb_delete(br, f, true);
		spin_unlock_bh(&br->hash_lock);
	}
	rcu_read_unlock();
}

static unsigned long __ndm_state_to_fdb_flags(u16 ndm_state)
{
	unsigned long flags = 0;

	if (ndm_state & NUD_PERMANENT)
		__set_bit(BR_FDB_LOCAL, &flags);
	if (ndm_state & NUD_NOARP)
		__set_bit(BR_FDB_STATIC, &flags);

	return flags;
}

static unsigned long __ndm_flags_to_fdb_flags(u8 ndm_flags)
{
	unsigned long flags = 0;

	if (ndm_flags & NTF_USE)
		__set_bit(BR_FDB_ADDED_BY_USER, &flags);
	if (ndm_flags & NTF_EXT_LEARNED)
		__set_bit(BR_FDB_ADDED_BY_EXT_LEARN, &flags);
	if (ndm_flags & NTF_OFFLOADED)
		__set_bit(BR_FDB_OFFLOADED, &flags);
	if (ndm_flags & NTF_STICKY)
		__set_bit(BR_FDB_STICKY, &flags);

	return flags;
}

static int __fdb_flush_validate_ifindex(const struct net_bridge *br,
					int ifindex,
					struct netlink_ext_ack *extack)
{
	const struct net_device *dev;

	dev = __dev_get_by_index(dev_net(br->dev), ifindex);
	if (!dev) {
		NL_SET_ERR_MSG_MOD(extack, "Unknown flush device ifindex");
		return -ENODEV;
	}
	if (!netif_is_bridge_master(dev) && !netif_is_bridge_port(dev)) {
		NL_SET_ERR_MSG_MOD(extack, "Flush device is not a bridge or bridge port");
		return -EINVAL;
	}
	if (netif_is_bridge_master(dev) && dev != br->dev) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Flush bridge device does not match target bridge device");
		return -EINVAL;
	}
	if (netif_is_bridge_port(dev)) {
		struct net_bridge_port *p = br_port_get_rtnl(dev);

		if (p->br != br) {
			NL_SET_ERR_MSG_MOD(extack, "Port belongs to a different bridge device");
			return -EINVAL;
		}
	}

	return 0;
}

static const struct nla_policy br_fdb_del_bulk_policy[NDA_MAX + 1] = {
	[NDA_VLAN]	= NLA_POLICY_RANGE(NLA_U16, 1, VLAN_N_VID - 2),
	[NDA_IFINDEX]	= NLA_POLICY_MIN(NLA_S32, 1),
	[NDA_NDM_STATE_MASK]	= { .type = NLA_U16 },
	[NDA_NDM_FLAGS_MASK]	= { .type = NLA_U8 },
};

int br_fdb_delete_bulk(struct nlmsghdr *nlh, struct net_device *dev,
		       struct netlink_ext_ack *extack)
{
	struct net_bridge_fdb_flush_desc desc = {};
	struct ndmsg *ndm = nlmsg_data(nlh);
	struct net_bridge_port *p = NULL;
	struct nlattr *tb[NDA_MAX + 1];
	struct net_bridge *br;
	u8 ndm_flags;
	int err;

	ndm_flags = ndm->ndm_flags & ~FDB_FLUSH_IGNORED_NDM_FLAGS;

	err = nlmsg_parse(nlh, sizeof(*ndm), tb, NDA_MAX,
			  br_fdb_del_bulk_policy, extack);
	if (err)
		return err;

	if (netif_is_bridge_master(dev)) {
		br = netdev_priv(dev);
	} else {
		p = br_port_get_rtnl(dev);
		if (!p) {
			NL_SET_ERR_MSG_MOD(extack, "Device is not a bridge port");
			return -EINVAL;
		}
		br = p->br;
	}

	if (tb[NDA_VLAN])
		desc.vlan_id = nla_get_u16(tb[NDA_VLAN]);

	if (ndm_flags & ~FDB_FLUSH_ALLOWED_NDM_FLAGS) {
		NL_SET_ERR_MSG(extack, "Unsupported fdb flush ndm flag bits set");
		return -EINVAL;
	}
	if (ndm->ndm_state & ~FDB_FLUSH_ALLOWED_NDM_STATES) {
		NL_SET_ERR_MSG(extack, "Unsupported fdb flush ndm state bits set");
		return -EINVAL;
	}

	desc.flags |= __ndm_state_to_fdb_flags(ndm->ndm_state);
	desc.flags |= __ndm_flags_to_fdb_flags(ndm_flags);
	if (tb[NDA_NDM_STATE_MASK]) {
		u16 ndm_state_mask = nla_get_u16(tb[NDA_NDM_STATE_MASK]);

		desc.flags_mask |= __ndm_state_to_fdb_flags(ndm_state_mask);
	}
	if (tb[NDA_NDM_FLAGS_MASK]) {
		u8 ndm_flags_mask = nla_get_u8(tb[NDA_NDM_FLAGS_MASK]);

		desc.flags_mask |= __ndm_flags_to_fdb_flags(ndm_flags_mask);
	}
	if (tb[NDA_IFINDEX]) {
		int ifidx = nla_get_s32(tb[NDA_IFINDEX]);

		err = __fdb_flush_validate_ifindex(br, ifidx, extack);
		if (err)
			return err;
		desc.port_ifindex = ifidx;
	} else if (p) {
		/* flush was invoked with port device and NTF_MASTER */
		desc.port_ifindex = p->dev->ifindex;
	}

	br_debug(br, "flushing port ifindex: %d vlan id: %u flags: 0x%lx flags mask: 0x%lx\n",
		 desc.port_ifindex, desc.vlan_id, desc.flags, desc.flags_mask);

	br_fdb_flush(br, &desc);

	return 0;
}

/* Flush all entries referring to a specific port.
 * if do_all is set also flush static entries
 * if vid is set delete all entries that match the vlan_id
 */
void br_fdb_delete_by_port(struct net_bridge *br,
			   const struct net_bridge_port *p,
			   u16 vid,
			   int do_all)
{
	struct net_bridge_fdb_entry *f;
	struct hlist_node *tmp;

	spin_lock_bh(&br->hash_lock);
	hlist_for_each_entry_safe(f, tmp, &br->fdb_list, fdb_node) {
		if (f->dst != p)
			continue;

		if (!do_all)
			if (test_bit(BR_FDB_STATIC, &f->flags) ||
			    (test_bit(BR_FDB_ADDED_BY_EXT_LEARN, &f->flags) &&
			     !test_bit(BR_FDB_OFFLOADED, &f->flags)) ||
			    (vid && f->key.vlan_id != vid))
				continue;

		if (test_bit(BR_FDB_LOCAL, &f->flags))
			fdb_delete_local(br, p, f);
		else
			fdb_delete(br, f, true);
	}
	spin_unlock_bh(&br->hash_lock);
}

#if IS_ENABLED(CONFIG_ATM_LANE)
/* Interface used by ATM LANE hook to test
 * if an addr is on some other bridge port */
int br_fdb_test_addr(struct net_device *dev, unsigned char *addr)
{
	struct net_bridge_fdb_entry *fdb;
	struct net_bridge_port *port;
	int ret;

	rcu_read_lock();
	port = br_port_get_rcu(dev);
	if (!port)
		ret = 0;
	else {
		const struct net_bridge_port *dst = NULL;

		fdb = br_fdb_find_rcu(port->br, addr, 0);
		if (fdb)
			dst = READ_ONCE(fdb->dst);

		ret = dst && dst->dev != dev &&
		      dst->state == BR_STATE_FORWARDING;
	}
	rcu_read_unlock();

	return ret;
}
#endif /* CONFIG_ATM_LANE */

/*
 * Fill buffer with forwarding table records in
 * the API format.
 */
int br_fdb_fillbuf(struct net_bridge *br, void *buf,
		   unsigned long maxnum, unsigned long skip)
{
	struct net_bridge_fdb_entry *f;
	struct __fdb_entry *fe = buf;
	int num = 0;

	memset(buf, 0, maxnum*sizeof(struct __fdb_entry));

	rcu_read_lock();
	hlist_for_each_entry_rcu(f, &br->fdb_list, fdb_node) {
		if (num >= maxnum)
			break;

		if (has_expired(br, f))
			continue;

		/* ignore pseudo entry for local MAC address */
		if (!f->dst)
			continue;

		if (skip) {
			--skip;
			continue;
		}

		/* convert from internal format to API */
		memcpy(fe->mac_addr, f->key.addr.addr, ETH_ALEN);

		/* due to ABI compat need to split into hi/lo */
		fe->port_no = f->dst->port_no;
		fe->port_hi = f->dst->port_no >> 8;

		fe->is_local = test_bit(BR_FDB_LOCAL, &f->flags);
		if (!test_bit(BR_FDB_STATIC, &f->flags))
			fe->ageing_timer_value = jiffies_delta_to_clock_t(jiffies - f->updated);
		++fe;
		++num;
	}
	rcu_read_unlock();

	return num;
}

/* Add entry for local address of interface */
int br_fdb_add_local(struct net_bridge *br, struct net_bridge_port *source,
		     const unsigned char *addr, u16 vid)
{
	int ret;

	spin_lock_bh(&br->hash_lock);
	ret = fdb_add_local(br, source, addr, vid);
	spin_unlock_bh(&br->hash_lock);
	return ret;
}

/* returns true if the fdb was modified */
static bool __fdb_mark_active(struct net_bridge_fdb_entry *fdb)
{
	return !!(test_bit(BR_FDB_NOTIFY_INACTIVE, &fdb->flags) &&
		  test_and_clear_bit(BR_FDB_NOTIFY_INACTIVE, &fdb->flags));
}

void br_fdb_update(struct net_bridge *br, struct net_bridge_port *source,
		   const unsigned char *addr, u16 vid, unsigned long flags)
{
	struct net_bridge_fdb_entry *fdb;

	/* some users want to always flood. */
	if (hold_time(br) == 0)
		return;

	fdb = fdb_find_rcu(&br->fdb_hash_tbl, addr, vid);
	if (likely(fdb)) {
		/* attempt to update an entry for a local interface */
		if (unlikely(test_bit(BR_FDB_LOCAL, &fdb->flags))) {
			if (net_ratelimit())
				br_warn(br, "received packet on %s with own address as source address (addr:%pM, vlan:%u)\n",
					source->dev->name, addr, vid);
		} else {
			unsigned long now = jiffies;
			bool fdb_modified = false;

			if (now != fdb->updated) {
				fdb->updated = now;
				fdb_modified = __fdb_mark_active(fdb);
			}

			/* fastpath: update of existing entry */
			if (unlikely(source != READ_ONCE(fdb->dst) &&
				     !test_bit(BR_FDB_STICKY, &fdb->flags))) {
				br_switchdev_fdb_notify(br, fdb, RTM_DELNEIGH);
				WRITE_ONCE(fdb->dst, source);
				fdb_modified = true;
				/* Take over HW learned entry */
				if (unlikely(test_bit(BR_FDB_ADDED_BY_EXT_LEARN,
						      &fdb->flags)))
					clear_bit(BR_FDB_ADDED_BY_EXT_LEARN,
						  &fdb->flags);
				/* Clear locked flag when roaming to an
				 * unlocked port.
				 */
				if (unlikely(test_bit(BR_FDB_LOCKED, &fdb->flags)))
					clear_bit(BR_FDB_LOCKED, &fdb->flags);
			}

			if (unlikely(test_bit(BR_FDB_ADDED_BY_USER, &flags))) {
				set_bit(BR_FDB_ADDED_BY_USER, &fdb->flags);
				if (test_and_clear_bit(BR_FDB_DYNAMIC_LEARNED,
						       &fdb->flags))
					atomic_dec(&br->fdb_n_learned);
			}
			if (unlikely(fdb_modified)) {
				trace_br_fdb_update(br, source, addr, vid, flags);
				fdb_notify(br, fdb, RTM_NEWNEIGH, true);
			}
		}
	} else {
		spin_lock(&br->hash_lock);
		fdb = fdb_create(br, source, addr, vid, flags);
		if (fdb) {
			trace_br_fdb_update(br, source, addr, vid, flags);
			fdb_notify(br, fdb, RTM_NEWNEIGH, true);
		}
		/* else  we lose race and someone else inserts
		 * it first, don't bother updating
		 */
		spin_unlock(&br->hash_lock);
	}
}

/* Dump information about entries, in response to GETNEIGH */
int br_fdb_dump(struct sk_buff *skb,
		struct netlink_callback *cb,
		struct net_device *dev,
		struct net_device *filter_dev,
		int *idx)
{
	struct net_bridge *br = netdev_priv(dev);
	struct net_bridge_fdb_entry *f;
	int err = 0;

	if (!netif_is_bridge_master(dev))
		return err;

	if (!filter_dev) {
		err = ndo_dflt_fdb_dump(skb, cb, dev, NULL, idx);
		if (err < 0)
			return err;
	}

	rcu_read_lock();
	hlist_for_each_entry_rcu(f, &br->fdb_list, fdb_node) {
		if (*idx < cb->args[2])
			goto skip;
		if (filter_dev && (!f->dst || f->dst->dev != filter_dev)) {
			if (filter_dev != dev)
				goto skip;
			/* !f->dst is a special case for bridge
			 * It means the MAC belongs to the bridge
			 * Therefore need a little more filtering
			 * we only want to dump the !f->dst case
			 */
			if (f->dst)
				goto skip;
		}
		if (!filter_dev && f->dst)
			goto skip;

		err = fdb_fill_info(skb, br, f,
				    NETLINK_CB(cb->skb).portid,
				    cb->nlh->nlmsg_seq,
				    RTM_NEWNEIGH,
				    NLM_F_MULTI);
		if (err < 0)
			break;
skip:
		*idx += 1;
	}
	rcu_read_unlock();

	return err;
}

int br_fdb_get(struct sk_buff *skb,
	       struct nlattr *tb[],
	       struct net_device *dev,
	       const unsigned char *addr,
	       u16 vid, u32 portid, u32 seq,
	       struct netlink_ext_ack *extack)
{
	struct net_bridge *br = netdev_priv(dev);
	struct net_bridge_fdb_entry *f;
	int err = 0;

	rcu_read_lock();
	f = br_fdb_find_rcu(br, addr, vid);
	if (!f) {
		NL_SET_ERR_MSG(extack, "Fdb entry not found");
		err = -ENOENT;
		goto errout;
	}

	err = fdb_fill_info(skb, br, f, portid, seq,
			    RTM_NEWNEIGH, 0);
errout:
	rcu_read_unlock();
	return err;
}

/* returns true if the fdb is modified */
static bool fdb_handle_notify(struct net_bridge_fdb_entry *fdb, u8 notify)
{
	bool modified = false;

	/* allow to mark an entry as inactive, usually done on creation */
	if ((notify & FDB_NOTIFY_INACTIVE_BIT) &&
	    !test_and_set_bit(BR_FDB_NOTIFY_INACTIVE, &fdb->flags))
		modified = true;

	if ((notify & FDB_NOTIFY_BIT) &&
	    !test_and_set_bit(BR_FDB_NOTIFY, &fdb->flags)) {
		/* enabled activity tracking */
		modified = true;
	} else if (!(notify & FDB_NOTIFY_BIT) &&
		   test_and_clear_bit(BR_FDB_NOTIFY, &fdb->flags)) {
		/* disabled activity tracking, clear notify state */
		clear_bit(BR_FDB_NOTIFY_INACTIVE, &fdb->flags);
		modified = true;
	}

	return modified;
}

/* Update (create or replace) forwarding database entry */
static int fdb_add_entry(struct net_bridge *br, struct net_bridge_port *source,
			 const u8 *addr, struct ndmsg *ndm, u16 flags, u16 vid,
			 struct nlattr *nfea_tb[])
{
	bool is_sticky = !!(ndm->ndm_flags & NTF_STICKY);
	bool refresh = !nfea_tb[NFEA_DONT_REFRESH];
	struct net_bridge_fdb_entry *fdb;
	u16 state = ndm->ndm_state;
	bool modified = false;
	u8 notify = 0;

	/* If the port cannot learn allow only local and static entries */
	if (source && !(state & NUD_PERMANENT) && !(state & NUD_NOARP) &&
	    !(source->state == BR_STATE_LEARNING ||
	      source->state == BR_STATE_FORWARDING))
		return -EPERM;

	if (!source && !(state & NUD_PERMANENT)) {
		pr_info("bridge: RTM_NEWNEIGH %s without NUD_PERMANENT\n",
			br->dev->name);
		return -EINVAL;
	}

	if (is_sticky && (state & NUD_PERMANENT))
		return -EINVAL;

	if (nfea_tb[NFEA_ACTIVITY_NOTIFY]) {
		notify = nla_get_u8(nfea_tb[NFEA_ACTIVITY_NOTIFY]);
		if ((notify & ~BR_FDB_NOTIFY_SETTABLE_BITS) ||
		    (notify & BR_FDB_NOTIFY_SETTABLE_BITS) == FDB_NOTIFY_INACTIVE_BIT)
			return -EINVAL;
	}

	fdb = br_fdb_find(br, addr, vid);
	if (fdb == NULL) {
		if (!(flags & NLM_F_CREATE))
			return -ENOENT;

		fdb = fdb_create(br, source, addr, vid,
				 BIT(BR_FDB_ADDED_BY_USER));
		if (!fdb)
			return -ENOMEM;

		modified = true;
	} else {
		if (flags & NLM_F_EXCL)
			return -EEXIST;

		if (READ_ONCE(fdb->dst) != source) {
			WRITE_ONCE(fdb->dst, source);
			modified = true;
		}

		set_bit(BR_FDB_ADDED_BY_USER, &fdb->flags);
		if (test_and_clear_bit(BR_FDB_DYNAMIC_LEARNED, &fdb->flags))
			atomic_dec(&br->fdb_n_learned);
	}

	if (fdb_to_nud(br, fdb) != state) {
		if (state & NUD_PERMANENT) {
			set_bit(BR_FDB_LOCAL, &fdb->flags);
			if (!test_and_set_bit(BR_FDB_STATIC, &fdb->flags))
				fdb_add_hw_addr(br, addr);
		} else if (state & NUD_NOARP) {
			clear_bit(BR_FDB_LOCAL, &fdb->flags);
			if (!test_and_set_bit(BR_FDB_STATIC, &fdb->flags))
				fdb_add_hw_addr(br, addr);
		} else {
			clear_bit(BR_FDB_LOCAL, &fdb->flags);
			if (test_and_clear_bit(BR_FDB_STATIC, &fdb->flags))
				fdb_del_hw_addr(br, addr);
		}

		modified = true;
	}

	if (is_sticky != test_bit(BR_FDB_STICKY, &fdb->flags)) {
		change_bit(BR_FDB_STICKY, &fdb->flags);
		modified = true;
	}

	if (test_and_clear_bit(BR_FDB_LOCKED, &fdb->flags))
		modified = true;

	if (fdb_handle_notify(fdb, notify))
		modified = true;

	fdb->used = jiffies;
	if (modified) {
		if (refresh)
			fdb->updated = jiffies;
		fdb_notify(br, fdb, RTM_NEWNEIGH, true);
	}

	return 0;
}

static int __br_fdb_add(struct ndmsg *ndm, struct net_bridge *br,
			struct net_bridge_port *p, const unsigned char *addr,
			u16 nlh_flags, u16 vid, struct nlattr *nfea_tb[],
			struct netlink_ext_ack *extack)
{
	int err = 0;

	if (ndm->ndm_flags & NTF_USE) {
		if (!p) {
			pr_info("bridge: RTM_NEWNEIGH %s with NTF_USE is not supported\n",
				br->dev->name);
			return -EINVAL;
		}
		if (!nbp_state_should_learn(p))
			return 0;

		local_bh_disable();
		rcu_read_lock();
		br_fdb_update(br, p, addr, vid, BIT(BR_FDB_ADDED_BY_USER));
		rcu_read_unlock();
		local_bh_enable();
	} else if (ndm->ndm_flags & NTF_EXT_LEARNED) {
		if (!p && !(ndm->ndm_state & NUD_PERMANENT)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "FDB entry towards bridge must be permanent");
			return -EINVAL;
		}
		err = br_fdb_external_learn_add(br, p, addr, vid, false, true);
	} else {
		spin_lock_bh(&br->hash_lock);
		err = fdb_add_entry(br, p, addr, ndm, nlh_flags, vid, nfea_tb);
		spin_unlock_bh(&br->hash_lock);
	}

	return err;
}

static const struct nla_policy br_nda_fdb_pol[NFEA_MAX + 1] = {
	[NFEA_ACTIVITY_NOTIFY]	= { .type = NLA_U8 },
	[NFEA_DONT_REFRESH]	= { .type = NLA_FLAG },
};

/* Add new permanent fdb entry with RTM_NEWNEIGH */
int br_fdb_add(struct ndmsg *ndm, struct nlattr *tb[],
	       struct net_device *dev,
	       const unsigned char *addr, u16 vid, u16 nlh_flags,
	       struct netlink_ext_ack *extack)
{
	struct nlattr *nfea_tb[NFEA_MAX + 1], *attr;
	struct net_bridge_vlan_group *vg;
	struct net_bridge_port *p = NULL;
	struct net_bridge_vlan *v;
	struct net_bridge *br = NULL;
	u32 ext_flags = 0;
	int err = 0;

	trace_br_fdb_add(ndm, dev, addr, vid, nlh_flags);

	if (!(ndm->ndm_state & (NUD_PERMANENT|NUD_NOARP|NUD_REACHABLE))) {
		pr_info("bridge: RTM_NEWNEIGH with invalid state %#x\n", ndm->ndm_state);
		return -EINVAL;
	}

	if (is_zero_ether_addr(addr)) {
		pr_info("bridge: RTM_NEWNEIGH with invalid ether address\n");
		return -EINVAL;
	}

	if (netif_is_bridge_master(dev)) {
		br = netdev_priv(dev);
		vg = br_vlan_group(br);
	} else {
		p = br_port_get_rtnl(dev);
		if (!p) {
			pr_info("bridge: RTM_NEWNEIGH %s not a bridge port\n",
				dev->name);
			return -EINVAL;
		}
		br = p->br;
		vg = nbp_vlan_group(p);
	}

	if (tb[NDA_FLAGS_EXT])
		ext_flags = nla_get_u32(tb[NDA_FLAGS_EXT]);

	if (ext_flags & NTF_EXT_LOCKED) {
		NL_SET_ERR_MSG_MOD(extack, "Cannot add FDB entry with \"locked\" flag set");
		return -EINVAL;
	}

	if (tb[NDA_FDB_EXT_ATTRS]) {
		attr = tb[NDA_FDB_EXT_ATTRS];
		err = nla_parse_nested(nfea_tb, NFEA_MAX, attr,
				       br_nda_fdb_pol, extack);
		if (err)
			return err;
	} else {
		memset(nfea_tb, 0, sizeof(struct nlattr *) * (NFEA_MAX + 1));
	}

	if (vid) {
		v = br_vlan_find(vg, vid);
		if (!v || !br_vlan_should_use(v)) {
			pr_info("bridge: RTM_NEWNEIGH with unconfigured vlan %d on %s\n", vid, dev->name);
			return -EINVAL;
		}

		/* VID was specified, so use it. */
		err = __br_fdb_add(ndm, br, p, addr, nlh_flags, vid, nfea_tb,
				   extack);
	} else {
		err = __br_fdb_add(ndm, br, p, addr, nlh_flags, 0, nfea_tb,
				   extack);
		if (err || !vg || !vg->num_vlans)
			goto out;

		/* We have vlans configured on this port and user didn't
		 * specify a VLAN.  To be nice, add/update entry for every
		 * vlan on this port.
		 */
		list_for_each_entry(v, &vg->vlan_list, vlist) {
			if (!br_vlan_should_use(v))
				continue;
			err = __br_fdb_add(ndm, br, p, addr, nlh_flags, v->vid,
					   nfea_tb, extack);
			if (err)
				goto out;
		}
	}

out:
	return err;
}

static int fdb_delete_by_addr_and_port(struct net_bridge *br,
				       const struct net_bridge_port *p,
				       const u8 *addr, u16 vlan)
{
	struct net_bridge_fdb_entry *fdb;

	fdb = br_fdb_find(br, addr, vlan);
	if (!fdb || READ_ONCE(fdb->dst) != p)
		return -ENOENT;

	fdb_delete(br, fdb, true);

	return 0;
}

static int __br_fdb_delete(struct net_bridge *br,
			   const struct net_bridge_port *p,
			   const unsigned char *addr, u16 vid)
{
	int err;

	spin_lock_bh(&br->hash_lock);
	err = fdb_delete_by_addr_and_port(br, p, addr, vid);
	spin_unlock_bh(&br->hash_lock);

	return err;
}

/* Remove neighbor entry with RTM_DELNEIGH */
int br_fdb_delete(struct ndmsg *ndm, struct nlattr *tb[],
		  struct net_device *dev,
		  const unsigned char *addr, u16 vid,
		  struct netlink_ext_ack *extack)
{
	struct net_bridge_vlan_group *vg;
	struct net_bridge_port *p = NULL;
	struct net_bridge_vlan *v;
	struct net_bridge *br;
	int err;

	if (netif_is_bridge_master(dev)) {
		br = netdev_priv(dev);
		vg = br_vlan_group(br);
	} else {
		p = br_port_get_rtnl(dev);
		if (!p) {
			pr_info("bridge: RTM_DELNEIGH %s not a bridge port\n",
				dev->name);
			return -EINVAL;
		}
		vg = nbp_vlan_group(p);
		br = p->br;
	}

	if (vid) {
		v = br_vlan_find(vg, vid);
		if (!v) {
			pr_info("bridge: RTM_DELNEIGH with unconfigured vlan %d on %s\n", vid, dev->name);
			return -EINVAL;
		}

		err = __br_fdb_delete(br, p, addr, vid);
	} else {
		err = -ENOENT;
		err &= __br_fdb_delete(br, p, addr, 0);
		if (!vg || !vg->num_vlans)
			return err;

		list_for_each_entry(v, &vg->vlan_list, vlist) {
			if (!br_vlan_should_use(v))
				continue;
			err &= __br_fdb_delete(br, p, addr, v->vid);
		}
	}

	return err;
}

int br_fdb_sync_static(struct net_bridge *br, struct net_bridge_port *p)
{
	struct net_bridge_fdb_entry *f, *tmp;
	int err = 0;

	ASSERT_RTNL();

	/* the key here is that static entries change only under rtnl */
	rcu_read_lock();
	hlist_for_each_entry_rcu(f, &br->fdb_list, fdb_node) {
		/* We only care for static entries */
		if (!test_bit(BR_FDB_STATIC, &f->flags))
			continue;
		err = dev_uc_add(p->dev, f->key.addr.addr);
		if (err)
			goto rollback;
	}
done:
	rcu_read_unlock();

	return err;

rollback:
	hlist_for_each_entry_rcu(tmp, &br->fdb_list, fdb_node) {
		/* We only care for static entries */
		if (!test_bit(BR_FDB_STATIC, &tmp->flags))
			continue;
		if (tmp == f)
			break;
		dev_uc_del(p->dev, tmp->key.addr.addr);
	}

	goto done;
}

void br_fdb_unsync_static(struct net_bridge *br, struct net_bridge_port *p)
{
	struct net_bridge_fdb_entry *f;

	ASSERT_RTNL();

	rcu_read_lock();
	hlist_for_each_entry_rcu(f, &br->fdb_list, fdb_node) {
		/* We only care for static entries */
		if (!test_bit(BR_FDB_STATIC, &f->flags))
			continue;

		dev_uc_del(p->dev, f->key.addr.addr);
	}
	rcu_read_unlock();
}

int br_fdb_external_learn_add(struct net_bridge *br, struct net_bridge_port *p,
			      const unsigned char *addr, u16 vid, bool locked,
			      bool swdev_notify)
{
	struct net_bridge_fdb_entry *fdb;
	bool modified = false;
	int err = 0;

	trace_br_fdb_external_learn_add(br, p, addr, vid);

	if (locked && (!p || !(p->flags & BR_PORT_MAB)))
		return -EINVAL;

	spin_lock_bh(&br->hash_lock);

	fdb = br_fdb_find(br, addr, vid);
	if (!fdb) {
		unsigned long flags = BIT(BR_FDB_ADDED_BY_EXT_LEARN);

		if (swdev_notify)
			flags |= BIT(BR_FDB_ADDED_BY_USER);

		if (!p)
			flags |= BIT(BR_FDB_LOCAL);

		if (locked)
			flags |= BIT(BR_FDB_LOCKED);

		fdb = fdb_create(br, p, addr, vid, flags);
		if (!fdb) {
			err = -ENOMEM;
			goto err_unlock;
		}
		fdb_notify(br, fdb, RTM_NEWNEIGH, swdev_notify);
	} else {
		if (locked &&
		    (!test_bit(BR_FDB_LOCKED, &fdb->flags) ||
		     READ_ONCE(fdb->dst) != p)) {
			err = -EINVAL;
			goto err_unlock;
		}

		fdb->updated = jiffies;

		if (READ_ONCE(fdb->dst) != p) {
			WRITE_ONCE(fdb->dst, p);
			modified = true;
		}

		if (test_and_set_bit(BR_FDB_ADDED_BY_EXT_LEARN, &fdb->flags)) {
			/* Refresh entry */
			fdb->used = jiffies;
		} else {
			modified = true;
		}

		if (locked != test_bit(BR_FDB_LOCKED, &fdb->flags)) {
			change_bit(BR_FDB_LOCKED, &fdb->flags);
			modified = true;
		}

		if (swdev_notify)
			set_bit(BR_FDB_ADDED_BY_USER, &fdb->flags);

		if (!p)
			set_bit(BR_FDB_LOCAL, &fdb->flags);

		if ((swdev_notify || !p) &&
		    test_and_clear_bit(BR_FDB_DYNAMIC_LEARNED, &fdb->flags))
			atomic_dec(&br->fdb_n_learned);

		if (modified)
			fdb_notify(br, fdb, RTM_NEWNEIGH, swdev_notify);
	}

err_unlock:
	spin_unlock_bh(&br->hash_lock);

	return err;
}

int br_fdb_external_learn_del(struct net_bridge *br, struct net_bridge_port *p,
			      const unsigned char *addr, u16 vid,
			      bool swdev_notify)
{
	struct net_bridge_fdb_entry *fdb;
	int err = 0;

	spin_lock_bh(&br->hash_lock);

	fdb = br_fdb_find(br, addr, vid);
	if (fdb && test_bit(BR_FDB_ADDED_BY_EXT_LEARN, &fdb->flags))
		fdb_delete(br, fdb, swdev_notify);
	else
		err = -ENOENT;

	spin_unlock_bh(&br->hash_lock);

	return err;
}

void br_fdb_offloaded_set(struct net_bridge *br, struct net_bridge_port *p,
			  const unsigned char *addr, u16 vid, bool offloaded)
{
	struct net_bridge_fdb_entry *fdb;

	spin_lock_bh(&br->hash_lock);

	fdb = br_fdb_find(br, addr, vid);
	if (fdb && offloaded != test_bit(BR_FDB_OFFLOADED, &fdb->flags))
		change_bit(BR_FDB_OFFLOADED, &fdb->flags);

	spin_unlock_bh(&br->hash_lock);
}

void br_fdb_clear_offload(const struct net_device *dev, u16 vid)
{
	struct net_bridge_fdb_entry *f;
	struct net_bridge_port *p;

	ASSERT_RTNL();

	p = br_port_get_rtnl(dev);
	if (!p)
		return;

	spin_lock_bh(&p->br->hash_lock);
	hlist_for_each_entry(f, &p->br->fdb_list, fdb_node) {
		if (f->dst == p && f->key.vlan_id == vid)
			clear_bit(BR_FDB_OFFLOADED, &f->flags);
	}
	spin_unlock_bh(&p->br->hash_lock);
}
EXPORT_SYMBOL_GPL(br_fdb_clear_offload);
