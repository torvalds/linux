/* SPDX-License-Identifier: GPL-2.0 */
/* linux/net/inet/arp.h */
#ifndef _ARP_H
#define _ARP_H

#include <linux/if_arp.h>
#include <linux/hash.h>
#include <net/neighbour.h>

static inline struct neigh_table *arp_table(struct net *net)
{
	return net->neigh_tables[NEIGH_ARP_TABLE];
}

static inline u32 arp_hashfn(const void *pkey, const struct net_device *dev, u32 *hash_rnd)
{
	u32 key = *(const u32 *)pkey;
	u32 val = key ^ hash32_ptr(dev);

	return val * hash_rnd[0];
}

#ifdef CONFIG_INET
static inline struct neighbour *__ipv4_neigh_lookup_noref(struct net_device *dev, u32 key)
{
	struct neigh_table *tbl = arp_table(dev_net(dev));

	if (dev->flags & (IFF_LOOPBACK | IFF_POINTOPOINT))
		key = INADDR_ANY;

	return ___neigh_lookup_noref(tbl, neigh_key_eq32, arp_hashfn, &key, dev);
}
#else
static inline
struct neighbour *__ipv4_neigh_lookup_noref(struct net_device *dev, u32 key)
{
	return NULL;
}
#endif

static inline struct neighbour *__ipv4_neigh_lookup(struct net_device *dev, u32 key)
{
	struct neighbour *n;

	rcu_read_lock();
	n = __ipv4_neigh_lookup_noref(dev, key);
	if (n && !refcount_inc_not_zero(&n->refcnt))
		n = NULL;
	rcu_read_unlock();

	return n;
}

static inline struct neighbour *ipv4_neigh_lookup(struct net_device *dev,
						  const void *pkey)
{
	struct neigh_table *tbl = arp_table(dev_net(dev));

	return neigh_lookup(tbl, pkey, dev);
}

static inline struct neighbour *ipv4_neigh_create(struct net_device *dev,
						  const void *pkey)
{
	struct neigh_table *tbl = arp_table(dev_net(dev));

	return neigh_create(tbl, pkey, dev);
}

static inline void __ipv4_confirm_neigh(struct net_device *dev, u32 key)
{
	struct neighbour *n;

	rcu_read_lock();
	n = __ipv4_neigh_lookup_noref(dev, key);
	neigh_confirm(n);
	rcu_read_unlock();
}

void arp_init(void);
int arp_ioctl(struct net *net, unsigned int cmd, void __user *arg);
void arp_send(int type, int ptype, __be32 dest_ip,
	      struct net_device *dev, __be32 src_ip,
	      const unsigned char *dest_hw,
	      const unsigned char *src_hw, const unsigned char *th);
int arp_mc_map(__be32 addr, u8 *haddr, struct net_device *dev, int dir);
void arp_ifdown(struct net_device *dev);
int arp_invalidate(struct net_device *dev, __be32 ip, bool force);

struct sk_buff *arp_create(int type, int ptype, __be32 dest_ip,
			   struct net_device *dev, __be32 src_ip,
			   const unsigned char *dest_hw,
			   const unsigned char *src_hw,
			   const unsigned char *target_hw);
void arp_xmit(struct sk_buff *skb);

#endif	/* _ARP_H */
