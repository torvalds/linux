/*
 *	Forwarding database
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Lennert Buytenhek		<buytenh@gnu.org>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
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
#include <asm/atomic.h>
#include <asm/unaligned.h>
#include "br_private.h"

static struct kmem_cache *br_fdb_cache __read_mostly;
static int fdb_insert(struct net_bridge *br, struct net_bridge_port *source,
		      const unsigned char *addr);
static void fdb_notify(const struct net_bridge_fdb_entry *, int);

static u32 fdb_salt __read_mostly;

int __init br_fdb_init(void)
{
	br_fdb_cache = kmem_cache_create("bridge_fdb_cache",
					 sizeof(struct net_bridge_fdb_entry),
					 0,
					 SLAB_HWCACHE_ALIGN, NULL);
	if (!br_fdb_cache)
		return -ENOMEM;

	get_random_bytes(&fdb_salt, sizeof(fdb_salt));
	return 0;
}

void br_fdb_fini(void)
{
	kmem_cache_destroy(br_fdb_cache);
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
	return !fdb->is_static &&
		time_before_eq(fdb->updated + hold_time(br), jiffies);
}

static inline int br_mac_hash(const unsigned char *mac)
{
	/* use 1 byte of OUI cnd 3 bytes of NIC */
	u32 key = get_unaligned((u32 *)(mac + 2));
	return jhash_1word(key, fdb_salt) & (BR_HASH_SIZE - 1);
}

static void fdb_rcu_free(struct rcu_head *head)
{
	struct net_bridge_fdb_entry *ent
		= container_of(head, struct net_bridge_fdb_entry, rcu);
	kmem_cache_free(br_fdb_cache, ent);
}

static inline void fdb_delete(struct net_bridge_fdb_entry *f)
{
	fdb_notify(f, RTM_DELNEIGH);
	hlist_del_rcu(&f->hlist);
	call_rcu(&f->rcu, fdb_rcu_free);
}

void br_fdb_changeaddr(struct net_bridge_port *p, const unsigned char *newaddr)
{
	struct net_bridge *br = p->br;
	int i;

	spin_lock_bh(&br->hash_lock);

	/* Search all chains since old address/hash is unknown */
	for (i = 0; i < BR_HASH_SIZE; i++) {
		struct hlist_node *h;
		hlist_for_each(h, &br->hash[i]) {
			struct net_bridge_fdb_entry *f;

			f = hlist_entry(h, struct net_bridge_fdb_entry, hlist);
			if (f->dst == p && f->is_local) {
				/* maybe another port has same hw addr? */
				struct net_bridge_port *op;
				list_for_each_entry(op, &br->port_list, list) {
					if (op != p &&
					    !compare_ether_addr(op->dev->dev_addr,
								f->addr.addr)) {
						f->dst = op;
						goto insert;
					}
				}

				/* delete old one */
				fdb_delete(f);
				goto insert;
			}
		}
	}
 insert:
	/* insert new address,  may fail if invalid address or dup. */
	fdb_insert(br, p, newaddr);

	spin_unlock_bh(&br->hash_lock);
}

void br_fdb_cleanup(unsigned long _data)
{
	struct net_bridge *br = (struct net_bridge *)_data;
	unsigned long delay = hold_time(br);
	unsigned long next_timer = jiffies + br->ageing_time;
	int i;

	spin_lock_bh(&br->hash_lock);
	for (i = 0; i < BR_HASH_SIZE; i++) {
		struct net_bridge_fdb_entry *f;
		struct hlist_node *h, *n;

		hlist_for_each_entry_safe(f, h, n, &br->hash[i], hlist) {
			unsigned long this_timer;
			if (f->is_static)
				continue;
			this_timer = f->updated + delay;
			if (time_before_eq(this_timer, jiffies))
				fdb_delete(f);
			else if (time_before(this_timer, next_timer))
				next_timer = this_timer;
		}
	}
	spin_unlock_bh(&br->hash_lock);

	mod_timer(&br->gc_timer, round_jiffies_up(next_timer));
}

/* Completely flush all dynamic entries in forwarding database.*/
void br_fdb_flush(struct net_bridge *br)
{
	int i;

	spin_lock_bh(&br->hash_lock);
	for (i = 0; i < BR_HASH_SIZE; i++) {
		struct net_bridge_fdb_entry *f;
		struct hlist_node *h, *n;
		hlist_for_each_entry_safe(f, h, n, &br->hash[i], hlist) {
			if (!f->is_static)
				fdb_delete(f);
		}
	}
	spin_unlock_bh(&br->hash_lock);
}

/* Flush all entries referring to a specific port.
 * if do_all is set also flush static entries
 */
void br_fdb_delete_by_port(struct net_bridge *br,
			   const struct net_bridge_port *p,
			   int do_all)
{
	int i;

	spin_lock_bh(&br->hash_lock);
	for (i = 0; i < BR_HASH_SIZE; i++) {
		struct hlist_node *h, *g;

		hlist_for_each_safe(h, g, &br->hash[i]) {
			struct net_bridge_fdb_entry *f
				= hlist_entry(h, struct net_bridge_fdb_entry, hlist);
			if (f->dst != p)
				continue;

			if (f->is_static && !do_all)
				continue;
			/*
			 * if multiple ports all have the same device address
			 * then when one port is deleted, assign
			 * the local entry to other port
			 */
			if (f->is_local) {
				struct net_bridge_port *op;
				list_for_each_entry(op, &br->port_list, list) {
					if (op != p &&
					    !compare_ether_addr(op->dev->dev_addr,
								f->addr.addr)) {
						f->dst = op;
						goto skip_delete;
					}
				}
			}

			fdb_delete(f);
		skip_delete: ;
		}
	}
	spin_unlock_bh(&br->hash_lock);
}

/* No locking or refcounting, assumes caller has rcu_read_lock */
struct net_bridge_fdb_entry *__br_fdb_get(struct net_bridge *br,
					  const unsigned char *addr)
{
	struct hlist_node *h;
	struct net_bridge_fdb_entry *fdb;

	hlist_for_each_entry_rcu(fdb, h, &br->hash[br_mac_hash(addr)], hlist) {
		if (!compare_ether_addr(fdb->addr.addr, addr)) {
			if (unlikely(has_expired(br, fdb)))
				break;
			return fdb;
		}
	}

	return NULL;
}

#if defined(CONFIG_ATM_LANE) || defined(CONFIG_ATM_LANE_MODULE)
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
		fdb = __br_fdb_get(port->br, addr);
		ret = fdb && fdb->dst->dev != dev &&
			fdb->dst->state == BR_STATE_FORWARDING;
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
	struct __fdb_entry *fe = buf;
	int i, num = 0;
	struct hlist_node *h;
	struct net_bridge_fdb_entry *f;

	memset(buf, 0, maxnum*sizeof(struct __fdb_entry));

	rcu_read_lock();
	for (i = 0; i < BR_HASH_SIZE; i++) {
		hlist_for_each_entry_rcu(f, h, &br->hash[i], hlist) {
			if (num >= maxnum)
				goto out;

			if (has_expired(br, f))
				continue;

			if (skip) {
				--skip;
				continue;
			}

			/* convert from internal format to API */
			memcpy(fe->mac_addr, f->addr.addr, ETH_ALEN);

			/* due to ABI compat need to split into hi/lo */
			fe->port_no = f->dst->port_no;
			fe->port_hi = f->dst->port_no >> 8;

			fe->is_local = f->is_local;
			if (!f->is_static)
				fe->ageing_timer_value = jiffies_to_clock_t(jiffies - f->updated);
			++fe;
			++num;
		}
	}

 out:
	rcu_read_unlock();

	return num;
}

static struct net_bridge_fdb_entry *fdb_find(struct hlist_head *head,
					     const unsigned char *addr)
{
	struct hlist_node *h;
	struct net_bridge_fdb_entry *fdb;

	hlist_for_each_entry(fdb, h, head, hlist) {
		if (!compare_ether_addr(fdb->addr.addr, addr))
			return fdb;
	}
	return NULL;
}

static struct net_bridge_fdb_entry *fdb_find_rcu(struct hlist_head *head,
						 const unsigned char *addr)
{
	struct hlist_node *h;
	struct net_bridge_fdb_entry *fdb;

	hlist_for_each_entry_rcu(fdb, h, head, hlist) {
		if (!compare_ether_addr(fdb->addr.addr, addr))
			return fdb;
	}
	return NULL;
}

static struct net_bridge_fdb_entry *fdb_create(struct hlist_head *head,
					       struct net_bridge_port *source,
					       const unsigned char *addr)
{
	struct net_bridge_fdb_entry *fdb;

	fdb = kmem_cache_alloc(br_fdb_cache, GFP_ATOMIC);
	if (fdb) {
		memcpy(fdb->addr.addr, addr, ETH_ALEN);
		fdb->dst = source;
		fdb->is_local = 0;
		fdb->is_static = 0;
		fdb->updated = fdb->used = jiffies;
		hlist_add_head_rcu(&fdb->hlist, head);
		fdb_notify(fdb, RTM_NEWNEIGH);
	}
	return fdb;
}

static int fdb_insert(struct net_bridge *br, struct net_bridge_port *source,
		  const unsigned char *addr)
{
	struct hlist_head *head = &br->hash[br_mac_hash(addr)];
	struct net_bridge_fdb_entry *fdb;

	if (!is_valid_ether_addr(addr))
		return -EINVAL;

	fdb = fdb_find(head, addr);
	if (fdb) {
		/* it is okay to have multiple ports with same
		 * address, just use the first one.
		 */
		if (fdb->is_local)
			return 0;
		br_warn(br, "adding interface %s with same address "
		       "as a received packet\n",
		       source->dev->name);
		fdb_delete(fdb);
	}

	fdb = fdb_create(head, source, addr);
	if (!fdb)
		return -ENOMEM;

	fdb->is_local = fdb->is_static = 1;
	return 0;
}

/* Add entry for local address of interface */
int br_fdb_insert(struct net_bridge *br, struct net_bridge_port *source,
		  const unsigned char *addr)
{
	int ret;

	spin_lock_bh(&br->hash_lock);
	ret = fdb_insert(br, source, addr);
	spin_unlock_bh(&br->hash_lock);
	return ret;
}

void br_fdb_update(struct net_bridge *br, struct net_bridge_port *source,
		   const unsigned char *addr)
{
	struct hlist_head *head = &br->hash[br_mac_hash(addr)];
	struct net_bridge_fdb_entry *fdb;

	/* some users want to always flood. */
	if (hold_time(br) == 0)
		return;

	/* ignore packets unless we are using this port */
	if (!(source->state == BR_STATE_LEARNING ||
	      source->state == BR_STATE_FORWARDING))
		return;

	fdb = fdb_find_rcu(head, addr);
	if (likely(fdb)) {
		/* attempt to update an entry for a local interface */
		if (unlikely(fdb->is_local)) {
			if (net_ratelimit())
				br_warn(br, "received packet on %s with "
					"own address as source address\n",
					source->dev->name);
		} else {
			/* fastpath: update of existing entry */
			fdb->dst = source;
			fdb->updated = jiffies;
		}
	} else {
		spin_lock(&br->hash_lock);
		if (likely(!fdb_find(head, addr)))
			fdb_create(head, source, addr);

		/* else  we lose race and someone else inserts
		 * it first, don't bother updating
		 */
		spin_unlock(&br->hash_lock);
	}
}

static int fdb_to_nud(const struct net_bridge_fdb_entry *fdb)
{
	if (fdb->is_local)
		return NUD_PERMANENT;
	else if (fdb->is_static)
		return NUD_NOARP;
	else if (has_expired(fdb->dst->br, fdb))
		return NUD_STALE;
	else
		return NUD_REACHABLE;
}

static int fdb_fill_info(struct sk_buff *skb,
			 const struct net_bridge_fdb_entry *fdb,
			 u32 pid, u32 seq, int type, unsigned int flags)
{
	unsigned long now = jiffies;
	struct nda_cacheinfo ci;
	struct nlmsghdr *nlh;
	struct ndmsg *ndm;

	nlh = nlmsg_put(skb, pid, seq, type, sizeof(*ndm), flags);
	if (nlh == NULL)
		return -EMSGSIZE;


	ndm = nlmsg_data(nlh);
	ndm->ndm_family	 = AF_BRIDGE;
	ndm->ndm_pad1    = 0;
	ndm->ndm_pad2    = 0;
	ndm->ndm_flags	 = 0;
	ndm->ndm_type	 = 0;
	ndm->ndm_ifindex = fdb->dst->dev->ifindex;
	ndm->ndm_state   = fdb_to_nud(fdb);

	NLA_PUT(skb, NDA_LLADDR, ETH_ALEN, &fdb->addr);

	ci.ndm_used	 = jiffies_to_clock_t(now - fdb->used);
	ci.ndm_confirmed = 0;
	ci.ndm_updated	 = jiffies_to_clock_t(now - fdb->updated);
	ci.ndm_refcnt	 = 0;
	NLA_PUT(skb, NDA_CACHEINFO, sizeof(ci), &ci);

	return nlmsg_end(skb, nlh);

nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static inline size_t fdb_nlmsg_size(void)
{
	return NLMSG_ALIGN(sizeof(struct ndmsg))
		+ nla_total_size(ETH_ALEN) /* NDA_LLADDR */
		+ nla_total_size(sizeof(struct nda_cacheinfo));
}

static void fdb_notify(const struct net_bridge_fdb_entry *fdb, int type)
{
	struct net *net = dev_net(fdb->dst->dev);
	struct sk_buff *skb;
	int err = -ENOBUFS;

	skb = nlmsg_new(fdb_nlmsg_size(), GFP_ATOMIC);
	if (skb == NULL)
		goto errout;

	err = fdb_fill_info(skb, fdb, 0, 0, type, 0);
	if (err < 0) {
		/* -EMSGSIZE implies BUG in fdb_nlmsg_size() */
		WARN_ON(err == -EMSGSIZE);
		kfree_skb(skb);
		goto errout;
	}
	rtnl_notify(skb, net, 0, RTNLGRP_NEIGH, NULL, GFP_ATOMIC);
	return;
errout:
	if (err < 0)
		rtnl_set_sk_err(net, RTNLGRP_NEIGH, err);
}

/* Dump information about entries, in response to GETNEIGH */
int br_fdb_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	struct net_device *dev;
	int idx = 0;

	rcu_read_lock();
	for_each_netdev_rcu(net, dev) {
		struct net_bridge *br = netdev_priv(dev);
		int i;

		if (!(dev->priv_flags & IFF_EBRIDGE))
			continue;

		for (i = 0; i < BR_HASH_SIZE; i++) {
			struct hlist_node *h;
			struct net_bridge_fdb_entry *f;

			hlist_for_each_entry_rcu(f, h, &br->hash[i], hlist) {
				if (idx < cb->args[0])
					goto skip;

				if (fdb_fill_info(skb, f,
						  NETLINK_CB(cb->skb).pid,
						  cb->nlh->nlmsg_seq,
						  RTM_NEWNEIGH,
						  NLM_F_MULTI) < 0)
					break;
skip:
				++idx;
			}
		}
	}
	rcu_read_unlock();

	cb->args[0] = idx;

	return skb->len;
}

/* Create new static fdb entry */
static int fdb_add_entry(struct net_bridge_port *source, const __u8 *addr,
			 __u16 state)
{
	struct net_bridge *br = source->br;
	struct hlist_head *head = &br->hash[br_mac_hash(addr)];
	struct net_bridge_fdb_entry *fdb;

	fdb = fdb_find(head, addr);
	if (fdb)
		return -EEXIST;

	fdb = fdb_create(head, source, addr);
	if (!fdb)
		return -ENOMEM;

	if (state & NUD_PERMANENT)
		fdb->is_local = fdb->is_static = 1;
	else if (state & NUD_NOARP)
		fdb->is_static = 1;
	return 0;
}

/* Add new permanent fdb entry with RTM_NEWNEIGH */
int br_fdb_add(struct sk_buff *skb, struct nlmsghdr *nlh, void *arg)
{
	struct net *net = sock_net(skb->sk);
	struct ndmsg *ndm;
	struct nlattr *tb[NDA_MAX+1];
	struct net_device *dev;
	struct net_bridge_port *p;
	const __u8 *addr;
	int err;

	ASSERT_RTNL();
	err = nlmsg_parse(nlh, sizeof(*ndm), tb, NDA_MAX, NULL);
	if (err < 0)
		return err;

	ndm = nlmsg_data(nlh);
	if (ndm->ndm_ifindex == 0) {
		pr_info("bridge: RTM_NEWNEIGH with invalid ifindex\n");
		return -EINVAL;
	}

	dev = __dev_get_by_index(net, ndm->ndm_ifindex);
	if (dev == NULL) {
		pr_info("bridge: RTM_NEWNEIGH with unknown ifindex\n");
		return -ENODEV;
	}

	if (!tb[NDA_LLADDR] || nla_len(tb[NDA_LLADDR]) != ETH_ALEN) {
		pr_info("bridge: RTM_NEWNEIGH with invalid address\n");
		return -EINVAL;
	}

	addr = nla_data(tb[NDA_LLADDR]);
	if (!is_valid_ether_addr(addr)) {
		pr_info("bridge: RTM_NEWNEIGH with invalid ether address\n");
		return -EINVAL;
	}

	p = br_port_get_rtnl(dev);
	if (p == NULL) {
		pr_info("bridge: RTM_NEWNEIGH %s not a bridge port\n",
			dev->name);
		return -EINVAL;
	}

	spin_lock_bh(&p->br->hash_lock);
	err = fdb_add_entry(p, addr, ndm->ndm_state);
	spin_unlock_bh(&p->br->hash_lock);

	return err;
}

static int fdb_delete_by_addr(struct net_bridge_port *p, const u8 *addr)
{
	struct net_bridge *br = p->br;
	struct hlist_head *head = &br->hash[br_mac_hash(addr)];
	struct net_bridge_fdb_entry *fdb;

	fdb = fdb_find(head, addr);
	if (!fdb)
		return -ENOENT;

	fdb_delete(fdb);
	return 0;
}

/* Remove neighbor entry with RTM_DELNEIGH */
int br_fdb_delete(struct sk_buff *skb, struct nlmsghdr *nlh, void *arg)
{
	struct net *net = sock_net(skb->sk);
	struct ndmsg *ndm;
	struct net_bridge_port *p;
	struct nlattr *llattr;
	const __u8 *addr;
	struct net_device *dev;
	int err;

	ASSERT_RTNL();
	if (nlmsg_len(nlh) < sizeof(*ndm))
		return -EINVAL;

	ndm = nlmsg_data(nlh);
	if (ndm->ndm_ifindex == 0) {
		pr_info("bridge: RTM_DELNEIGH with invalid ifindex\n");
		return -EINVAL;
	}

	dev = __dev_get_by_index(net, ndm->ndm_ifindex);
	if (dev == NULL) {
		pr_info("bridge: RTM_DELNEIGH with unknown ifindex\n");
		return -ENODEV;
	}

	llattr = nlmsg_find_attr(nlh, sizeof(*ndm), NDA_LLADDR);
	if (llattr == NULL || nla_len(llattr) != ETH_ALEN) {
		pr_info("bridge: RTM_DELNEIGH with invalid address\n");
		return -EINVAL;
	}

	addr = nla_data(llattr);

	p = br_port_get_rtnl(dev);
	if (p == NULL) {
		pr_info("bridge: RTM_DELNEIGH %s not a bridge port\n",
			dev->name);
		return -EINVAL;
	}

	spin_lock_bh(&p->br->hash_lock);
	err = fdb_delete_by_addr(p, addr);
	spin_unlock_bh(&p->br->hash_lock);

	return err;
}
