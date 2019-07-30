/*
 * Bridge multicast support.
 *
 * Copyright (c) 2010 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <linux/err.h>
#include <linux/export.h>
#include <linux/if_ether.h>
#include <linux/igmp.h>
#include <linux/jhash.h>
#include <linux/kernel.h>
#include <linux/log2.h>
#include <linux/netdevice.h>
#include <linux/netfilter_bridge.h>
#include <linux/random.h>
#include <linux/rculist.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/inetdevice.h>
#include <linux/mroute.h>
#include <net/ip.h>
#include <net/switchdev.h>
#if IS_ENABLED(CONFIG_IPV6)
#include <net/ipv6.h>
#include <net/mld.h>
#include <net/ip6_checksum.h>
#include <net/addrconf.h>
#endif

#include "br_private.h"

static void br_multicast_start_querier(struct net_bridge *br,
				       struct bridge_mcast_own_query *query);
static void br_multicast_add_router(struct net_bridge *br,
				    struct net_bridge_port *port);
static void br_ip4_multicast_leave_group(struct net_bridge *br,
					 struct net_bridge_port *port,
					 __be32 group,
					 __u16 vid,
					 const unsigned char *src);

static void __del_port_router(struct net_bridge_port *p);
#if IS_ENABLED(CONFIG_IPV6)
static void br_ip6_multicast_leave_group(struct net_bridge *br,
					 struct net_bridge_port *port,
					 const struct in6_addr *group,
					 __u16 vid, const unsigned char *src);
#endif
unsigned int br_mdb_rehash_seq;

static inline int br_ip_equal(const struct br_ip *a, const struct br_ip *b)
{
	if (a->proto != b->proto)
		return 0;
	if (a->vid != b->vid)
		return 0;
	switch (a->proto) {
	case htons(ETH_P_IP):
		return a->u.ip4 == b->u.ip4;
#if IS_ENABLED(CONFIG_IPV6)
	case htons(ETH_P_IPV6):
		return ipv6_addr_equal(&a->u.ip6, &b->u.ip6);
#endif
	}
	return 0;
}

static inline int __br_ip4_hash(struct net_bridge_mdb_htable *mdb, __be32 ip,
				__u16 vid)
{
	return jhash_2words((__force u32)ip, vid, mdb->secret) & (mdb->max - 1);
}

#if IS_ENABLED(CONFIG_IPV6)
static inline int __br_ip6_hash(struct net_bridge_mdb_htable *mdb,
				const struct in6_addr *ip,
				__u16 vid)
{
	return jhash_2words(ipv6_addr_hash(ip), vid,
			    mdb->secret) & (mdb->max - 1);
}
#endif

static inline int br_ip_hash(struct net_bridge_mdb_htable *mdb,
			     struct br_ip *ip)
{
	switch (ip->proto) {
	case htons(ETH_P_IP):
		return __br_ip4_hash(mdb, ip->u.ip4, ip->vid);
#if IS_ENABLED(CONFIG_IPV6)
	case htons(ETH_P_IPV6):
		return __br_ip6_hash(mdb, &ip->u.ip6, ip->vid);
#endif
	}
	return 0;
}

static struct net_bridge_mdb_entry *__br_mdb_ip_get(
	struct net_bridge_mdb_htable *mdb, struct br_ip *dst, int hash)
{
	struct net_bridge_mdb_entry *mp;

	hlist_for_each_entry_rcu(mp, &mdb->mhash[hash], hlist[mdb->ver]) {
		if (br_ip_equal(&mp->addr, dst))
			return mp;
	}

	return NULL;
}

struct net_bridge_mdb_entry *br_mdb_ip_get(struct net_bridge_mdb_htable *mdb,
					   struct br_ip *dst)
{
	if (!mdb)
		return NULL;

	return __br_mdb_ip_get(mdb, dst, br_ip_hash(mdb, dst));
}

static struct net_bridge_mdb_entry *br_mdb_ip4_get(
	struct net_bridge_mdb_htable *mdb, __be32 dst, __u16 vid)
{
	struct br_ip br_dst;

	br_dst.u.ip4 = dst;
	br_dst.proto = htons(ETH_P_IP);
	br_dst.vid = vid;

	return br_mdb_ip_get(mdb, &br_dst);
}

#if IS_ENABLED(CONFIG_IPV6)
static struct net_bridge_mdb_entry *br_mdb_ip6_get(
	struct net_bridge_mdb_htable *mdb, const struct in6_addr *dst,
	__u16 vid)
{
	struct br_ip br_dst;

	br_dst.u.ip6 = *dst;
	br_dst.proto = htons(ETH_P_IPV6);
	br_dst.vid = vid;

	return br_mdb_ip_get(mdb, &br_dst);
}
#endif

struct net_bridge_mdb_entry *br_mdb_get(struct net_bridge *br,
					struct sk_buff *skb, u16 vid)
{
	struct net_bridge_mdb_htable *mdb = rcu_dereference(br->mdb);
	struct br_ip ip;

	if (br->multicast_disabled)
		return NULL;

	if (BR_INPUT_SKB_CB(skb)->igmp)
		return NULL;

	ip.proto = skb->protocol;
	ip.vid = vid;

	switch (skb->protocol) {
	case htons(ETH_P_IP):
		ip.u.ip4 = ip_hdr(skb)->daddr;
		break;
#if IS_ENABLED(CONFIG_IPV6)
	case htons(ETH_P_IPV6):
		ip.u.ip6 = ipv6_hdr(skb)->daddr;
		break;
#endif
	default:
		return NULL;
	}

	return br_mdb_ip_get(mdb, &ip);
}

static void br_mdb_free(struct rcu_head *head)
{
	struct net_bridge_mdb_htable *mdb =
		container_of(head, struct net_bridge_mdb_htable, rcu);
	struct net_bridge_mdb_htable *old = mdb->old;

	mdb->old = NULL;
	kfree(old->mhash);
	kfree(old);
}

static int br_mdb_copy(struct net_bridge_mdb_htable *new,
		       struct net_bridge_mdb_htable *old,
		       int elasticity)
{
	struct net_bridge_mdb_entry *mp;
	int maxlen;
	int len;
	int i;

	for (i = 0; i < old->max; i++)
		hlist_for_each_entry(mp, &old->mhash[i], hlist[old->ver])
			hlist_add_head(&mp->hlist[new->ver],
				       &new->mhash[br_ip_hash(new, &mp->addr)]);

	if (!elasticity)
		return 0;

	maxlen = 0;
	for (i = 0; i < new->max; i++) {
		len = 0;
		hlist_for_each_entry(mp, &new->mhash[i], hlist[new->ver])
			len++;
		if (len > maxlen)
			maxlen = len;
	}

	return maxlen > elasticity ? -EINVAL : 0;
}

void br_multicast_free_pg(struct rcu_head *head)
{
	struct net_bridge_port_group *p =
		container_of(head, struct net_bridge_port_group, rcu);

	kfree(p);
}

static void br_multicast_free_group(struct rcu_head *head)
{
	struct net_bridge_mdb_entry *mp =
		container_of(head, struct net_bridge_mdb_entry, rcu);

	kfree(mp);
}

static void br_multicast_group_expired(struct timer_list *t)
{
	struct net_bridge_mdb_entry *mp = from_timer(mp, t, timer);
	struct net_bridge *br = mp->br;
	struct net_bridge_mdb_htable *mdb;

	spin_lock(&br->multicast_lock);
	if (!netif_running(br->dev) || timer_pending(&mp->timer))
		goto out;

	mp->host_joined = false;
	br_mdb_notify(br->dev, NULL, &mp->addr, RTM_DELMDB, 0);

	if (mp->ports)
		goto out;

	mdb = mlock_dereference(br->mdb, br);

	hlist_del_rcu(&mp->hlist[mdb->ver]);
	mdb->size--;

	call_rcu_bh(&mp->rcu, br_multicast_free_group);

out:
	spin_unlock(&br->multicast_lock);
}

static void br_multicast_del_pg(struct net_bridge *br,
				struct net_bridge_port_group *pg)
{
	struct net_bridge_mdb_htable *mdb;
	struct net_bridge_mdb_entry *mp;
	struct net_bridge_port_group *p;
	struct net_bridge_port_group __rcu **pp;

	mdb = mlock_dereference(br->mdb, br);

	mp = br_mdb_ip_get(mdb, &pg->addr);
	if (WARN_ON(!mp))
		return;

	for (pp = &mp->ports;
	     (p = mlock_dereference(*pp, br)) != NULL;
	     pp = &p->next) {
		if (p != pg)
			continue;

		rcu_assign_pointer(*pp, p->next);
		hlist_del_init(&p->mglist);
		del_timer(&p->timer);
		br_mdb_notify(br->dev, p->port, &pg->addr, RTM_DELMDB,
			      p->flags);
		call_rcu_bh(&p->rcu, br_multicast_free_pg);

		if (!mp->ports && !mp->host_joined &&
		    netif_running(br->dev))
			mod_timer(&mp->timer, jiffies);

		return;
	}

	WARN_ON(1);
}

static void br_multicast_port_group_expired(struct timer_list *t)
{
	struct net_bridge_port_group *pg = from_timer(pg, t, timer);
	struct net_bridge *br = pg->port->br;

	spin_lock(&br->multicast_lock);
	if (!netif_running(br->dev) || timer_pending(&pg->timer) ||
	    hlist_unhashed(&pg->mglist) || pg->flags & MDB_PG_FLAGS_PERMANENT)
		goto out;

	br_multicast_del_pg(br, pg);

out:
	spin_unlock(&br->multicast_lock);
}

static int br_mdb_rehash(struct net_bridge_mdb_htable __rcu **mdbp, int max,
			 int elasticity)
{
	struct net_bridge_mdb_htable *old = rcu_dereference_protected(*mdbp, 1);
	struct net_bridge_mdb_htable *mdb;
	int err;

	mdb = kmalloc(sizeof(*mdb), GFP_ATOMIC);
	if (!mdb)
		return -ENOMEM;

	mdb->max = max;
	mdb->old = old;

	mdb->mhash = kcalloc(max, sizeof(*mdb->mhash), GFP_ATOMIC);
	if (!mdb->mhash) {
		kfree(mdb);
		return -ENOMEM;
	}

	mdb->size = old ? old->size : 0;
	mdb->ver = old ? old->ver ^ 1 : 0;

	if (!old || elasticity)
		get_random_bytes(&mdb->secret, sizeof(mdb->secret));
	else
		mdb->secret = old->secret;

	if (!old)
		goto out;

	err = br_mdb_copy(mdb, old, elasticity);
	if (err) {
		kfree(mdb->mhash);
		kfree(mdb);
		return err;
	}

	br_mdb_rehash_seq++;
	call_rcu_bh(&mdb->rcu, br_mdb_free);

out:
	rcu_assign_pointer(*mdbp, mdb);

	return 0;
}

static struct sk_buff *br_ip4_multicast_alloc_query(struct net_bridge *br,
						    __be32 group,
						    u8 *igmp_type)
{
	struct igmpv3_query *ihv3;
	size_t igmp_hdr_size;
	struct sk_buff *skb;
	struct igmphdr *ih;
	struct ethhdr *eth;
	struct iphdr *iph;

	igmp_hdr_size = sizeof(*ih);
	if (br->multicast_igmp_version == 3)
		igmp_hdr_size = sizeof(*ihv3);
	skb = netdev_alloc_skb_ip_align(br->dev, sizeof(*eth) + sizeof(*iph) +
						 igmp_hdr_size + 4);
	if (!skb)
		goto out;

	skb->protocol = htons(ETH_P_IP);

	skb_reset_mac_header(skb);
	eth = eth_hdr(skb);

	ether_addr_copy(eth->h_source, br->dev->dev_addr);
	eth->h_dest[0] = 1;
	eth->h_dest[1] = 0;
	eth->h_dest[2] = 0x5e;
	eth->h_dest[3] = 0;
	eth->h_dest[4] = 0;
	eth->h_dest[5] = 1;
	eth->h_proto = htons(ETH_P_IP);
	skb_put(skb, sizeof(*eth));

	skb_set_network_header(skb, skb->len);
	iph = ip_hdr(skb);

	iph->version = 4;
	iph->ihl = 6;
	iph->tos = 0xc0;
	iph->tot_len = htons(sizeof(*iph) + igmp_hdr_size + 4);
	iph->id = 0;
	iph->frag_off = htons(IP_DF);
	iph->ttl = 1;
	iph->protocol = IPPROTO_IGMP;
	iph->saddr = br->multicast_query_use_ifaddr ?
		     inet_select_addr(br->dev, 0, RT_SCOPE_LINK) : 0;
	iph->daddr = htonl(INADDR_ALLHOSTS_GROUP);
	((u8 *)&iph[1])[0] = IPOPT_RA;
	((u8 *)&iph[1])[1] = 4;
	((u8 *)&iph[1])[2] = 0;
	((u8 *)&iph[1])[3] = 0;
	ip_send_check(iph);
	skb_put(skb, 24);

	skb_set_transport_header(skb, skb->len);
	*igmp_type = IGMP_HOST_MEMBERSHIP_QUERY;

	switch (br->multicast_igmp_version) {
	case 2:
		ih = igmp_hdr(skb);
		ih->type = IGMP_HOST_MEMBERSHIP_QUERY;
		ih->code = (group ? br->multicast_last_member_interval :
				    br->multicast_query_response_interval) /
			   (HZ / IGMP_TIMER_SCALE);
		ih->group = group;
		ih->csum = 0;
		ih->csum = ip_compute_csum((void *)ih, sizeof(*ih));
		break;
	case 3:
		ihv3 = igmpv3_query_hdr(skb);
		ihv3->type = IGMP_HOST_MEMBERSHIP_QUERY;
		ihv3->code = (group ? br->multicast_last_member_interval :
				      br->multicast_query_response_interval) /
			     (HZ / IGMP_TIMER_SCALE);
		ihv3->group = group;
		ihv3->qqic = br->multicast_query_interval / HZ;
		ihv3->nsrcs = 0;
		ihv3->resv = 0;
		ihv3->suppress = 0;
		ihv3->qrv = 2;
		ihv3->csum = 0;
		ihv3->csum = ip_compute_csum((void *)ihv3, sizeof(*ihv3));
		break;
	}

	skb_put(skb, igmp_hdr_size);
	__skb_pull(skb, sizeof(*eth));

out:
	return skb;
}

#if IS_ENABLED(CONFIG_IPV6)
static struct sk_buff *br_ip6_multicast_alloc_query(struct net_bridge *br,
						    const struct in6_addr *grp,
						    u8 *igmp_type)
{
	struct mld2_query *mld2q;
	unsigned long interval;
	struct ipv6hdr *ip6h;
	struct mld_msg *mldq;
	size_t mld_hdr_size;
	struct sk_buff *skb;
	struct ethhdr *eth;
	u8 *hopopt;

	mld_hdr_size = sizeof(*mldq);
	if (br->multicast_mld_version == 2)
		mld_hdr_size = sizeof(*mld2q);
	skb = netdev_alloc_skb_ip_align(br->dev, sizeof(*eth) + sizeof(*ip6h) +
						 8 + mld_hdr_size);
	if (!skb)
		goto out;

	skb->protocol = htons(ETH_P_IPV6);

	/* Ethernet header */
	skb_reset_mac_header(skb);
	eth = eth_hdr(skb);

	ether_addr_copy(eth->h_source, br->dev->dev_addr);
	eth->h_proto = htons(ETH_P_IPV6);
	skb_put(skb, sizeof(*eth));

	/* IPv6 header + HbH option */
	skb_set_network_header(skb, skb->len);
	ip6h = ipv6_hdr(skb);

	*(__force __be32 *)ip6h = htonl(0x60000000);
	ip6h->payload_len = htons(8 + mld_hdr_size);
	ip6h->nexthdr = IPPROTO_HOPOPTS;
	ip6h->hop_limit = 1;
	ipv6_addr_set(&ip6h->daddr, htonl(0xff020000), 0, 0, htonl(1));
	if (ipv6_dev_get_saddr(dev_net(br->dev), br->dev, &ip6h->daddr, 0,
			       &ip6h->saddr)) {
		kfree_skb(skb);
		br->has_ipv6_addr = 0;
		return NULL;
	}

	br->has_ipv6_addr = 1;
	ipv6_eth_mc_map(&ip6h->daddr, eth->h_dest);

	hopopt = (u8 *)(ip6h + 1);
	hopopt[0] = IPPROTO_ICMPV6;		/* next hdr */
	hopopt[1] = 0;				/* length of HbH */
	hopopt[2] = IPV6_TLV_ROUTERALERT;	/* Router Alert */
	hopopt[3] = 2;				/* Length of RA Option */
	hopopt[4] = 0;				/* Type = 0x0000 (MLD) */
	hopopt[5] = 0;
	hopopt[6] = IPV6_TLV_PAD1;		/* Pad1 */
	hopopt[7] = IPV6_TLV_PAD1;		/* Pad1 */

	skb_put(skb, sizeof(*ip6h) + 8);

	/* ICMPv6 */
	skb_set_transport_header(skb, skb->len);
	interval = ipv6_addr_any(grp) ?
			br->multicast_query_response_interval :
			br->multicast_last_member_interval;
	*igmp_type = ICMPV6_MGM_QUERY;
	switch (br->multicast_mld_version) {
	case 1:
		mldq = (struct mld_msg *)icmp6_hdr(skb);
		mldq->mld_type = ICMPV6_MGM_QUERY;
		mldq->mld_code = 0;
		mldq->mld_cksum = 0;
		mldq->mld_maxdelay = htons((u16)jiffies_to_msecs(interval));
		mldq->mld_reserved = 0;
		mldq->mld_mca = *grp;
		mldq->mld_cksum = csum_ipv6_magic(&ip6h->saddr, &ip6h->daddr,
						  sizeof(*mldq), IPPROTO_ICMPV6,
						  csum_partial(mldq,
							       sizeof(*mldq),
							       0));
		break;
	case 2:
		mld2q = (struct mld2_query *)icmp6_hdr(skb);
		mld2q->mld2q_mrc = htons((u16)jiffies_to_msecs(interval));
		mld2q->mld2q_type = ICMPV6_MGM_QUERY;
		mld2q->mld2q_code = 0;
		mld2q->mld2q_cksum = 0;
		mld2q->mld2q_resv1 = 0;
		mld2q->mld2q_resv2 = 0;
		mld2q->mld2q_suppress = 0;
		mld2q->mld2q_qrv = 2;
		mld2q->mld2q_nsrcs = 0;
		mld2q->mld2q_qqic = br->multicast_query_interval / HZ;
		mld2q->mld2q_mca = *grp;
		mld2q->mld2q_cksum = csum_ipv6_magic(&ip6h->saddr, &ip6h->daddr,
						     sizeof(*mld2q),
						     IPPROTO_ICMPV6,
						     csum_partial(mld2q,
								  sizeof(*mld2q),
								  0));
		break;
	}
	skb_put(skb, mld_hdr_size);

	__skb_pull(skb, sizeof(*eth));

out:
	return skb;
}
#endif

static struct sk_buff *br_multicast_alloc_query(struct net_bridge *br,
						struct br_ip *addr,
						u8 *igmp_type)
{
	switch (addr->proto) {
	case htons(ETH_P_IP):
		return br_ip4_multicast_alloc_query(br, addr->u.ip4, igmp_type);
#if IS_ENABLED(CONFIG_IPV6)
	case htons(ETH_P_IPV6):
		return br_ip6_multicast_alloc_query(br, &addr->u.ip6,
						    igmp_type);
#endif
	}
	return NULL;
}

static struct net_bridge_mdb_entry *br_multicast_get_group(
	struct net_bridge *br, struct net_bridge_port *port,
	struct br_ip *group, int hash)
{
	struct net_bridge_mdb_htable *mdb;
	struct net_bridge_mdb_entry *mp;
	unsigned int count = 0;
	unsigned int max;
	int elasticity;
	int err;

	mdb = rcu_dereference_protected(br->mdb, 1);
	hlist_for_each_entry(mp, &mdb->mhash[hash], hlist[mdb->ver]) {
		count++;
		if (unlikely(br_ip_equal(group, &mp->addr)))
			return mp;
	}

	elasticity = 0;
	max = mdb->max;

	if (unlikely(count > br->hash_elasticity && count)) {
		if (net_ratelimit())
			br_info(br, "Multicast hash table "
				"chain limit reached: %s\n",
				port ? port->dev->name : br->dev->name);

		elasticity = br->hash_elasticity;
	}

	if (mdb->size >= max) {
		max *= 2;
		if (unlikely(max > br->hash_max)) {
			br_warn(br, "Multicast hash table maximum of %d "
				"reached, disabling snooping: %s\n",
				br->hash_max,
				port ? port->dev->name : br->dev->name);
			err = -E2BIG;
disable:
			br->multicast_disabled = 1;
			goto err;
		}
	}

	if (max > mdb->max || elasticity) {
		if (mdb->old) {
			if (net_ratelimit())
				br_info(br, "Multicast hash table "
					"on fire: %s\n",
					port ? port->dev->name : br->dev->name);
			err = -EEXIST;
			goto err;
		}

		err = br_mdb_rehash(&br->mdb, max, elasticity);
		if (err) {
			br_warn(br, "Cannot rehash multicast "
				"hash table, disabling snooping: %s, %d, %d\n",
				port ? port->dev->name : br->dev->name,
				mdb->size, err);
			goto disable;
		}

		err = -EAGAIN;
		goto err;
	}

	return NULL;

err:
	mp = ERR_PTR(err);
	return mp;
}

struct net_bridge_mdb_entry *br_multicast_new_group(struct net_bridge *br,
						    struct net_bridge_port *p,
						    struct br_ip *group)
{
	struct net_bridge_mdb_htable *mdb;
	struct net_bridge_mdb_entry *mp;
	int hash;
	int err;

	mdb = rcu_dereference_protected(br->mdb, 1);
	if (!mdb) {
		err = br_mdb_rehash(&br->mdb, BR_HASH_SIZE, 0);
		if (err)
			return ERR_PTR(err);
		goto rehash;
	}

	hash = br_ip_hash(mdb, group);
	mp = br_multicast_get_group(br, p, group, hash);
	switch (PTR_ERR(mp)) {
	case 0:
		break;

	case -EAGAIN:
rehash:
		mdb = rcu_dereference_protected(br->mdb, 1);
		hash = br_ip_hash(mdb, group);
		break;

	default:
		goto out;
	}

	mp = kzalloc(sizeof(*mp), GFP_ATOMIC);
	if (unlikely(!mp))
		return ERR_PTR(-ENOMEM);

	mp->br = br;
	mp->addr = *group;
	timer_setup(&mp->timer, br_multicast_group_expired, 0);

	hlist_add_head_rcu(&mp->hlist[mdb->ver], &mdb->mhash[hash]);
	mdb->size++;

out:
	return mp;
}

struct net_bridge_port_group *br_multicast_new_port_group(
			struct net_bridge_port *port,
			struct br_ip *group,
			struct net_bridge_port_group __rcu *next,
			unsigned char flags,
			const unsigned char *src)
{
	struct net_bridge_port_group *p;

	p = kzalloc(sizeof(*p), GFP_ATOMIC);
	if (unlikely(!p))
		return NULL;

	p->addr = *group;
	p->port = port;
	p->flags = flags;
	rcu_assign_pointer(p->next, next);
	hlist_add_head(&p->mglist, &port->mglist);
	timer_setup(&p->timer, br_multicast_port_group_expired, 0);

	if (src)
		memcpy(p->eth_addr, src, ETH_ALEN);
	else
		memset(p->eth_addr, 0xff, ETH_ALEN);

	return p;
}

static bool br_port_group_equal(struct net_bridge_port_group *p,
				struct net_bridge_port *port,
				const unsigned char *src)
{
	if (p->port != port)
		return false;

	if (!(port->flags & BR_MULTICAST_TO_UNICAST))
		return true;

	return ether_addr_equal(src, p->eth_addr);
}

static int br_multicast_add_group(struct net_bridge *br,
				  struct net_bridge_port *port,
				  struct br_ip *group,
				  const unsigned char *src)
{
	struct net_bridge_port_group __rcu **pp;
	struct net_bridge_port_group *p;
	struct net_bridge_mdb_entry *mp;
	unsigned long now = jiffies;
	int err;

	spin_lock(&br->multicast_lock);
	if (!netif_running(br->dev) ||
	    (port && port->state == BR_STATE_DISABLED))
		goto out;

	mp = br_multicast_new_group(br, port, group);
	err = PTR_ERR(mp);
	if (IS_ERR(mp))
		goto err;

	if (!port) {
		if (!mp->host_joined) {
			mp->host_joined = true;
			br_mdb_notify(br->dev, NULL, &mp->addr, RTM_NEWMDB, 0);
		}
		mod_timer(&mp->timer, now + br->multicast_membership_interval);
		goto out;
	}

	for (pp = &mp->ports;
	     (p = mlock_dereference(*pp, br)) != NULL;
	     pp = &p->next) {
		if (br_port_group_equal(p, port, src))
			goto found;
		if ((unsigned long)p->port < (unsigned long)port)
			break;
	}

	p = br_multicast_new_port_group(port, group, *pp, 0, src);
	if (unlikely(!p))
		goto err;
	rcu_assign_pointer(*pp, p);
	br_mdb_notify(br->dev, port, group, RTM_NEWMDB, 0);

found:
	mod_timer(&p->timer, now + br->multicast_membership_interval);
out:
	err = 0;

err:
	spin_unlock(&br->multicast_lock);
	return err;
}

static int br_ip4_multicast_add_group(struct net_bridge *br,
				      struct net_bridge_port *port,
				      __be32 group,
				      __u16 vid,
				      const unsigned char *src)
{
	struct br_ip br_group;

	if (ipv4_is_local_multicast(group))
		return 0;

	br_group.u.ip4 = group;
	br_group.proto = htons(ETH_P_IP);
	br_group.vid = vid;

	return br_multicast_add_group(br, port, &br_group, src);
}

#if IS_ENABLED(CONFIG_IPV6)
static int br_ip6_multicast_add_group(struct net_bridge *br,
				      struct net_bridge_port *port,
				      const struct in6_addr *group,
				      __u16 vid,
				      const unsigned char *src)
{
	struct br_ip br_group;

	if (ipv6_addr_is_ll_all_nodes(group))
		return 0;

	br_group.u.ip6 = *group;
	br_group.proto = htons(ETH_P_IPV6);
	br_group.vid = vid;

	return br_multicast_add_group(br, port, &br_group, src);
}
#endif

static void br_multicast_router_expired(struct timer_list *t)
{
	struct net_bridge_port *port =
			from_timer(port, t, multicast_router_timer);
	struct net_bridge *br = port->br;

	spin_lock(&br->multicast_lock);
	if (port->multicast_router == MDB_RTR_TYPE_DISABLED ||
	    port->multicast_router == MDB_RTR_TYPE_PERM ||
	    timer_pending(&port->multicast_router_timer))
		goto out;

	__del_port_router(port);
out:
	spin_unlock(&br->multicast_lock);
}

static void br_mc_router_state_change(struct net_bridge *p,
				      bool is_mc_router)
{
	struct switchdev_attr attr = {
		.orig_dev = p->dev,
		.id = SWITCHDEV_ATTR_ID_BRIDGE_MROUTER,
		.flags = SWITCHDEV_F_DEFER,
		.u.mrouter = is_mc_router,
	};

	switchdev_port_attr_set(p->dev, &attr);
}

static void br_multicast_local_router_expired(struct timer_list *t)
{
	struct net_bridge *br = from_timer(br, t, multicast_router_timer);

	spin_lock(&br->multicast_lock);
	if (br->multicast_router == MDB_RTR_TYPE_DISABLED ||
	    br->multicast_router == MDB_RTR_TYPE_PERM ||
	    timer_pending(&br->multicast_router_timer))
		goto out;

	br_mc_router_state_change(br, false);
out:
	spin_unlock(&br->multicast_lock);
}

static void br_multicast_querier_expired(struct net_bridge *br,
					 struct bridge_mcast_own_query *query)
{
	spin_lock(&br->multicast_lock);
	if (!netif_running(br->dev) || br->multicast_disabled)
		goto out;

	br_multicast_start_querier(br, query);

out:
	spin_unlock(&br->multicast_lock);
}

static void br_ip4_multicast_querier_expired(struct timer_list *t)
{
	struct net_bridge *br = from_timer(br, t, ip4_other_query.timer);

	br_multicast_querier_expired(br, &br->ip4_own_query);
}

#if IS_ENABLED(CONFIG_IPV6)
static void br_ip6_multicast_querier_expired(struct timer_list *t)
{
	struct net_bridge *br = from_timer(br, t, ip6_other_query.timer);

	br_multicast_querier_expired(br, &br->ip6_own_query);
}
#endif

static void br_multicast_select_own_querier(struct net_bridge *br,
					    struct br_ip *ip,
					    struct sk_buff *skb)
{
	if (ip->proto == htons(ETH_P_IP))
		br->ip4_querier.addr.u.ip4 = ip_hdr(skb)->saddr;
#if IS_ENABLED(CONFIG_IPV6)
	else
		br->ip6_querier.addr.u.ip6 = ipv6_hdr(skb)->saddr;
#endif
}

static void __br_multicast_send_query(struct net_bridge *br,
				      struct net_bridge_port *port,
				      struct br_ip *ip)
{
	struct sk_buff *skb;
	u8 igmp_type;

	skb = br_multicast_alloc_query(br, ip, &igmp_type);
	if (!skb)
		return;

	if (port) {
		skb->dev = port->dev;
		br_multicast_count(br, port, skb, igmp_type,
				   BR_MCAST_DIR_TX);
		NF_HOOK(NFPROTO_BRIDGE, NF_BR_LOCAL_OUT,
			dev_net(port->dev), NULL, skb, NULL, skb->dev,
			br_dev_queue_push_xmit);
	} else {
		br_multicast_select_own_querier(br, ip, skb);
		br_multicast_count(br, port, skb, igmp_type,
				   BR_MCAST_DIR_RX);
		netif_rx(skb);
	}
}

static void br_multicast_send_query(struct net_bridge *br,
				    struct net_bridge_port *port,
				    struct bridge_mcast_own_query *own_query)
{
	struct bridge_mcast_other_query *other_query = NULL;
	struct br_ip br_group;
	unsigned long time;

	if (!netif_running(br->dev) || br->multicast_disabled ||
	    !br->multicast_querier)
		return;

	memset(&br_group.u, 0, sizeof(br_group.u));

	if (port ? (own_query == &port->ip4_own_query) :
		   (own_query == &br->ip4_own_query)) {
		other_query = &br->ip4_other_query;
		br_group.proto = htons(ETH_P_IP);
#if IS_ENABLED(CONFIG_IPV6)
	} else {
		other_query = &br->ip6_other_query;
		br_group.proto = htons(ETH_P_IPV6);
#endif
	}

	if (!other_query || timer_pending(&other_query->timer))
		return;

	__br_multicast_send_query(br, port, &br_group);

	time = jiffies;
	time += own_query->startup_sent < br->multicast_startup_query_count ?
		br->multicast_startup_query_interval :
		br->multicast_query_interval;
	mod_timer(&own_query->timer, time);
}

static void
br_multicast_port_query_expired(struct net_bridge_port *port,
				struct bridge_mcast_own_query *query)
{
	struct net_bridge *br = port->br;

	spin_lock(&br->multicast_lock);
	if (port->state == BR_STATE_DISABLED ||
	    port->state == BR_STATE_BLOCKING)
		goto out;

	if (query->startup_sent < br->multicast_startup_query_count)
		query->startup_sent++;

	br_multicast_send_query(port->br, port, query);

out:
	spin_unlock(&br->multicast_lock);
}

static void br_ip4_multicast_port_query_expired(struct timer_list *t)
{
	struct net_bridge_port *port = from_timer(port, t, ip4_own_query.timer);

	br_multicast_port_query_expired(port, &port->ip4_own_query);
}

#if IS_ENABLED(CONFIG_IPV6)
static void br_ip6_multicast_port_query_expired(struct timer_list *t)
{
	struct net_bridge_port *port = from_timer(port, t, ip6_own_query.timer);

	br_multicast_port_query_expired(port, &port->ip6_own_query);
}
#endif

static void br_mc_disabled_update(struct net_device *dev, bool value)
{
	struct switchdev_attr attr = {
		.orig_dev = dev,
		.id = SWITCHDEV_ATTR_ID_BRIDGE_MC_DISABLED,
		.flags = SWITCHDEV_F_DEFER,
		.u.mc_disabled = value,
	};

	switchdev_port_attr_set(dev, &attr);
}

int br_multicast_add_port(struct net_bridge_port *port)
{
	port->multicast_router = MDB_RTR_TYPE_TEMP_QUERY;

	timer_setup(&port->multicast_router_timer,
		    br_multicast_router_expired, 0);
	timer_setup(&port->ip4_own_query.timer,
		    br_ip4_multicast_port_query_expired, 0);
#if IS_ENABLED(CONFIG_IPV6)
	timer_setup(&port->ip6_own_query.timer,
		    br_ip6_multicast_port_query_expired, 0);
#endif
	br_mc_disabled_update(port->dev, port->br->multicast_disabled);

	port->mcast_stats = netdev_alloc_pcpu_stats(struct bridge_mcast_stats);
	if (!port->mcast_stats)
		return -ENOMEM;

	return 0;
}

void br_multicast_del_port(struct net_bridge_port *port)
{
	struct net_bridge *br = port->br;
	struct net_bridge_port_group *pg;
	struct hlist_node *n;

	/* Take care of the remaining groups, only perm ones should be left */
	spin_lock_bh(&br->multicast_lock);
	hlist_for_each_entry_safe(pg, n, &port->mglist, mglist)
		br_multicast_del_pg(br, pg);
	spin_unlock_bh(&br->multicast_lock);
	del_timer_sync(&port->multicast_router_timer);
	free_percpu(port->mcast_stats);
}

static void br_multicast_enable(struct bridge_mcast_own_query *query)
{
	query->startup_sent = 0;

	if (try_to_del_timer_sync(&query->timer) >= 0 ||
	    del_timer(&query->timer))
		mod_timer(&query->timer, jiffies);
}

static void __br_multicast_enable_port(struct net_bridge_port *port)
{
	struct net_bridge *br = port->br;

	if (br->multicast_disabled || !netif_running(br->dev))
		return;

	br_multicast_enable(&port->ip4_own_query);
#if IS_ENABLED(CONFIG_IPV6)
	br_multicast_enable(&port->ip6_own_query);
#endif
	if (port->multicast_router == MDB_RTR_TYPE_PERM &&
	    hlist_unhashed(&port->rlist))
		br_multicast_add_router(br, port);
}

void br_multicast_enable_port(struct net_bridge_port *port)
{
	struct net_bridge *br = port->br;

	spin_lock(&br->multicast_lock);
	__br_multicast_enable_port(port);
	spin_unlock(&br->multicast_lock);
}

void br_multicast_disable_port(struct net_bridge_port *port)
{
	struct net_bridge *br = port->br;
	struct net_bridge_port_group *pg;
	struct hlist_node *n;

	spin_lock(&br->multicast_lock);
	hlist_for_each_entry_safe(pg, n, &port->mglist, mglist)
		if (!(pg->flags & MDB_PG_FLAGS_PERMANENT))
			br_multicast_del_pg(br, pg);

	__del_port_router(port);

	del_timer(&port->multicast_router_timer);
	del_timer(&port->ip4_own_query.timer);
#if IS_ENABLED(CONFIG_IPV6)
	del_timer(&port->ip6_own_query.timer);
#endif
	spin_unlock(&br->multicast_lock);
}

static int br_ip4_multicast_igmp3_report(struct net_bridge *br,
					 struct net_bridge_port *port,
					 struct sk_buff *skb,
					 u16 vid)
{
	const unsigned char *src;
	struct igmpv3_report *ih;
	struct igmpv3_grec *grec;
	int i;
	int len;
	int num;
	int type;
	int err = 0;
	__be32 group;
	u16 nsrcs;

	ih = igmpv3_report_hdr(skb);
	num = ntohs(ih->ngrec);
	len = skb_transport_offset(skb) + sizeof(*ih);

	for (i = 0; i < num; i++) {
		len += sizeof(*grec);
		if (!pskb_may_pull(skb, len))
			return -EINVAL;

		grec = (void *)(skb->data + len - sizeof(*grec));
		group = grec->grec_mca;
		type = grec->grec_type;
		nsrcs = ntohs(grec->grec_nsrcs);

		len += nsrcs * 4;
		if (!pskb_may_pull(skb, len))
			return -EINVAL;

		/* We treat this as an IGMPv2 report for now. */
		switch (type) {
		case IGMPV3_MODE_IS_INCLUDE:
		case IGMPV3_MODE_IS_EXCLUDE:
		case IGMPV3_CHANGE_TO_INCLUDE:
		case IGMPV3_CHANGE_TO_EXCLUDE:
		case IGMPV3_ALLOW_NEW_SOURCES:
		case IGMPV3_BLOCK_OLD_SOURCES:
			break;

		default:
			continue;
		}

		src = eth_hdr(skb)->h_source;
		if ((type == IGMPV3_CHANGE_TO_INCLUDE ||
		     type == IGMPV3_MODE_IS_INCLUDE) &&
		    nsrcs == 0) {
			br_ip4_multicast_leave_group(br, port, group, vid, src);
		} else {
			err = br_ip4_multicast_add_group(br, port, group, vid,
							 src);
			if (err)
				break;
		}
	}

	return err;
}

#if IS_ENABLED(CONFIG_IPV6)
static int br_ip6_multicast_mld2_report(struct net_bridge *br,
					struct net_bridge_port *port,
					struct sk_buff *skb,
					u16 vid)
{
	const unsigned char *src;
	struct icmp6hdr *icmp6h;
	struct mld2_grec *grec;
	int i;
	int len;
	int num;
	int err = 0;

	if (!pskb_may_pull(skb, sizeof(*icmp6h)))
		return -EINVAL;

	icmp6h = icmp6_hdr(skb);
	num = ntohs(icmp6h->icmp6_dataun.un_data16[1]);
	len = skb_transport_offset(skb) + sizeof(*icmp6h);

	for (i = 0; i < num; i++) {
		__be16 *_nsrcs, __nsrcs;
		u16 nsrcs;

		_nsrcs = skb_header_pointer(skb,
					    len + offsetof(struct mld2_grec,
							   grec_nsrcs),
					    sizeof(__nsrcs), &__nsrcs);
		if (!_nsrcs)
			return -EINVAL;

		nsrcs = ntohs(*_nsrcs);

		if (!pskb_may_pull(skb,
				   len + sizeof(*grec) +
				   sizeof(struct in6_addr) * nsrcs))
			return -EINVAL;

		grec = (struct mld2_grec *)(skb->data + len);
		len += sizeof(*grec) +
		       sizeof(struct in6_addr) * nsrcs;

		/* We treat these as MLDv1 reports for now. */
		switch (grec->grec_type) {
		case MLD2_MODE_IS_INCLUDE:
		case MLD2_MODE_IS_EXCLUDE:
		case MLD2_CHANGE_TO_INCLUDE:
		case MLD2_CHANGE_TO_EXCLUDE:
		case MLD2_ALLOW_NEW_SOURCES:
		case MLD2_BLOCK_OLD_SOURCES:
			break;

		default:
			continue;
		}

		src = eth_hdr(skb)->h_source;
		if ((grec->grec_type == MLD2_CHANGE_TO_INCLUDE ||
		     grec->grec_type == MLD2_MODE_IS_INCLUDE) &&
		    nsrcs == 0) {
			br_ip6_multicast_leave_group(br, port, &grec->grec_mca,
						     vid, src);
		} else {
			err = br_ip6_multicast_add_group(br, port,
							 &grec->grec_mca, vid,
							 src);
			if (err)
				break;
		}
	}

	return err;
}
#endif

static bool br_ip4_multicast_select_querier(struct net_bridge *br,
					    struct net_bridge_port *port,
					    __be32 saddr)
{
	if (!timer_pending(&br->ip4_own_query.timer) &&
	    !timer_pending(&br->ip4_other_query.timer))
		goto update;

	if (!br->ip4_querier.addr.u.ip4)
		goto update;

	if (ntohl(saddr) <= ntohl(br->ip4_querier.addr.u.ip4))
		goto update;

	return false;

update:
	br->ip4_querier.addr.u.ip4 = saddr;

	/* update protected by general multicast_lock by caller */
	rcu_assign_pointer(br->ip4_querier.port, port);

	return true;
}

#if IS_ENABLED(CONFIG_IPV6)
static bool br_ip6_multicast_select_querier(struct net_bridge *br,
					    struct net_bridge_port *port,
					    struct in6_addr *saddr)
{
	if (!timer_pending(&br->ip6_own_query.timer) &&
	    !timer_pending(&br->ip6_other_query.timer))
		goto update;

	if (ipv6_addr_cmp(saddr, &br->ip6_querier.addr.u.ip6) <= 0)
		goto update;

	return false;

update:
	br->ip6_querier.addr.u.ip6 = *saddr;

	/* update protected by general multicast_lock by caller */
	rcu_assign_pointer(br->ip6_querier.port, port);

	return true;
}
#endif

static bool br_multicast_select_querier(struct net_bridge *br,
					struct net_bridge_port *port,
					struct br_ip *saddr)
{
	switch (saddr->proto) {
	case htons(ETH_P_IP):
		return br_ip4_multicast_select_querier(br, port, saddr->u.ip4);
#if IS_ENABLED(CONFIG_IPV6)
	case htons(ETH_P_IPV6):
		return br_ip6_multicast_select_querier(br, port, &saddr->u.ip6);
#endif
	}

	return false;
}

static void
br_multicast_update_query_timer(struct net_bridge *br,
				struct bridge_mcast_other_query *query,
				unsigned long max_delay)
{
	if (!timer_pending(&query->timer))
		query->delay_time = jiffies + max_delay;

	mod_timer(&query->timer, jiffies + br->multicast_querier_interval);
}

static void br_port_mc_router_state_change(struct net_bridge_port *p,
					   bool is_mc_router)
{
	struct switchdev_attr attr = {
		.orig_dev = p->dev,
		.id = SWITCHDEV_ATTR_ID_PORT_MROUTER,
		.flags = SWITCHDEV_F_DEFER,
		.u.mrouter = is_mc_router,
	};

	switchdev_port_attr_set(p->dev, &attr);
}

/*
 * Add port to router_list
 *  list is maintained ordered by pointer value
 *  and locked by br->multicast_lock and RCU
 */
static void br_multicast_add_router(struct net_bridge *br,
				    struct net_bridge_port *port)
{
	struct net_bridge_port *p;
	struct hlist_node *slot = NULL;

	if (!hlist_unhashed(&port->rlist))
		return;

	hlist_for_each_entry(p, &br->router_list, rlist) {
		if ((unsigned long) port >= (unsigned long) p)
			break;
		slot = &p->rlist;
	}

	if (slot)
		hlist_add_behind_rcu(&port->rlist, slot);
	else
		hlist_add_head_rcu(&port->rlist, &br->router_list);
	br_rtr_notify(br->dev, port, RTM_NEWMDB);
	br_port_mc_router_state_change(port, true);
}

static void br_multicast_mark_router(struct net_bridge *br,
				     struct net_bridge_port *port)
{
	unsigned long now = jiffies;

	if (!port) {
		if (br->multicast_router == MDB_RTR_TYPE_TEMP_QUERY) {
			if (!timer_pending(&br->multicast_router_timer))
				br_mc_router_state_change(br, true);
			mod_timer(&br->multicast_router_timer,
				  now + br->multicast_querier_interval);
		}
		return;
	}

	if (port->multicast_router == MDB_RTR_TYPE_DISABLED ||
	    port->multicast_router == MDB_RTR_TYPE_PERM)
		return;

	br_multicast_add_router(br, port);

	mod_timer(&port->multicast_router_timer,
		  now + br->multicast_querier_interval);
}

static void br_multicast_query_received(struct net_bridge *br,
					struct net_bridge_port *port,
					struct bridge_mcast_other_query *query,
					struct br_ip *saddr,
					unsigned long max_delay)
{
	if (!br_multicast_select_querier(br, port, saddr))
		return;

	br_multicast_update_query_timer(br, query, max_delay);
	br_multicast_mark_router(br, port);
}

static void br_ip4_multicast_query(struct net_bridge *br,
				   struct net_bridge_port *port,
				   struct sk_buff *skb,
				   u16 vid)
{
	const struct iphdr *iph = ip_hdr(skb);
	struct igmphdr *ih = igmp_hdr(skb);
	struct net_bridge_mdb_entry *mp;
	struct igmpv3_query *ih3;
	struct net_bridge_port_group *p;
	struct net_bridge_port_group __rcu **pp;
	struct br_ip saddr;
	unsigned long max_delay;
	unsigned long now = jiffies;
	unsigned int offset = skb_transport_offset(skb);
	__be32 group;

	spin_lock(&br->multicast_lock);
	if (!netif_running(br->dev) ||
	    (port && port->state == BR_STATE_DISABLED))
		goto out;

	group = ih->group;

	if (skb->len == offset + sizeof(*ih)) {
		max_delay = ih->code * (HZ / IGMP_TIMER_SCALE);

		if (!max_delay) {
			max_delay = 10 * HZ;
			group = 0;
		}
	} else if (skb->len >= offset + sizeof(*ih3)) {
		ih3 = igmpv3_query_hdr(skb);
		if (ih3->nsrcs)
			goto out;

		max_delay = ih3->code ?
			    IGMPV3_MRC(ih3->code) * (HZ / IGMP_TIMER_SCALE) : 1;
	} else {
		goto out;
	}

	if (!group) {
		saddr.proto = htons(ETH_P_IP);
		saddr.u.ip4 = iph->saddr;

		br_multicast_query_received(br, port, &br->ip4_other_query,
					    &saddr, max_delay);
		goto out;
	}

	mp = br_mdb_ip4_get(mlock_dereference(br->mdb, br), group, vid);
	if (!mp)
		goto out;

	max_delay *= br->multicast_last_member_count;

	if (mp->host_joined &&
	    (timer_pending(&mp->timer) ?
	     time_after(mp->timer.expires, now + max_delay) :
	     try_to_del_timer_sync(&mp->timer) >= 0))
		mod_timer(&mp->timer, now + max_delay);

	for (pp = &mp->ports;
	     (p = mlock_dereference(*pp, br)) != NULL;
	     pp = &p->next) {
		if (timer_pending(&p->timer) ?
		    time_after(p->timer.expires, now + max_delay) :
		    try_to_del_timer_sync(&p->timer) >= 0)
			mod_timer(&p->timer, now + max_delay);
	}

out:
	spin_unlock(&br->multicast_lock);
}

#if IS_ENABLED(CONFIG_IPV6)
static int br_ip6_multicast_query(struct net_bridge *br,
				  struct net_bridge_port *port,
				  struct sk_buff *skb,
				  u16 vid)
{
	struct mld_msg *mld;
	struct net_bridge_mdb_entry *mp;
	struct mld2_query *mld2q;
	struct net_bridge_port_group *p;
	struct net_bridge_port_group __rcu **pp;
	struct br_ip saddr;
	unsigned long max_delay;
	unsigned long now = jiffies;
	unsigned int offset = skb_transport_offset(skb);
	const struct in6_addr *group = NULL;
	bool is_general_query;
	int err = 0;

	spin_lock(&br->multicast_lock);
	if (!netif_running(br->dev) ||
	    (port && port->state == BR_STATE_DISABLED))
		goto out;

	if (skb->len == offset + sizeof(*mld)) {
		if (!pskb_may_pull(skb, offset + sizeof(*mld))) {
			err = -EINVAL;
			goto out;
		}
		mld = (struct mld_msg *) icmp6_hdr(skb);
		max_delay = msecs_to_jiffies(ntohs(mld->mld_maxdelay));
		if (max_delay)
			group = &mld->mld_mca;
	} else {
		if (!pskb_may_pull(skb, offset + sizeof(*mld2q))) {
			err = -EINVAL;
			goto out;
		}
		mld2q = (struct mld2_query *)icmp6_hdr(skb);
		if (!mld2q->mld2q_nsrcs)
			group = &mld2q->mld2q_mca;

		max_delay = max(msecs_to_jiffies(mldv2_mrc(mld2q)), 1UL);
	}

	is_general_query = group && ipv6_addr_any(group);

	if (is_general_query) {
		saddr.proto = htons(ETH_P_IPV6);
		saddr.u.ip6 = ipv6_hdr(skb)->saddr;

		br_multicast_query_received(br, port, &br->ip6_other_query,
					    &saddr, max_delay);
		goto out;
	} else if (!group) {
		goto out;
	}

	mp = br_mdb_ip6_get(mlock_dereference(br->mdb, br), group, vid);
	if (!mp)
		goto out;

	max_delay *= br->multicast_last_member_count;
	if (mp->host_joined &&
	    (timer_pending(&mp->timer) ?
	     time_after(mp->timer.expires, now + max_delay) :
	     try_to_del_timer_sync(&mp->timer) >= 0))
		mod_timer(&mp->timer, now + max_delay);

	for (pp = &mp->ports;
	     (p = mlock_dereference(*pp, br)) != NULL;
	     pp = &p->next) {
		if (timer_pending(&p->timer) ?
		    time_after(p->timer.expires, now + max_delay) :
		    try_to_del_timer_sync(&p->timer) >= 0)
			mod_timer(&p->timer, now + max_delay);
	}

out:
	spin_unlock(&br->multicast_lock);
	return err;
}
#endif

static void
br_multicast_leave_group(struct net_bridge *br,
			 struct net_bridge_port *port,
			 struct br_ip *group,
			 struct bridge_mcast_other_query *other_query,
			 struct bridge_mcast_own_query *own_query,
			 const unsigned char *src)
{
	struct net_bridge_mdb_htable *mdb;
	struct net_bridge_mdb_entry *mp;
	struct net_bridge_port_group *p;
	unsigned long now;
	unsigned long time;

	spin_lock(&br->multicast_lock);
	if (!netif_running(br->dev) ||
	    (port && port->state == BR_STATE_DISABLED))
		goto out;

	mdb = mlock_dereference(br->mdb, br);
	mp = br_mdb_ip_get(mdb, group);
	if (!mp)
		goto out;

	if (port && (port->flags & BR_MULTICAST_FAST_LEAVE)) {
		struct net_bridge_port_group __rcu **pp;

		for (pp = &mp->ports;
		     (p = mlock_dereference(*pp, br)) != NULL;
		     pp = &p->next) {
			if (!br_port_group_equal(p, port, src))
				continue;

			if (p->flags & MDB_PG_FLAGS_PERMANENT)
				break;

			rcu_assign_pointer(*pp, p->next);
			hlist_del_init(&p->mglist);
			del_timer(&p->timer);
			call_rcu_bh(&p->rcu, br_multicast_free_pg);
			br_mdb_notify(br->dev, port, group, RTM_DELMDB,
				      p->flags);

			if (!mp->ports && !mp->host_joined &&
			    netif_running(br->dev))
				mod_timer(&mp->timer, jiffies);
		}
		goto out;
	}

	if (timer_pending(&other_query->timer))
		goto out;

	if (br->multicast_querier) {
		__br_multicast_send_query(br, port, &mp->addr);

		time = jiffies + br->multicast_last_member_count *
				 br->multicast_last_member_interval;

		mod_timer(&own_query->timer, time);

		for (p = mlock_dereference(mp->ports, br);
		     p != NULL;
		     p = mlock_dereference(p->next, br)) {
			if (!br_port_group_equal(p, port, src))
				continue;

			if (!hlist_unhashed(&p->mglist) &&
			    (timer_pending(&p->timer) ?
			     time_after(p->timer.expires, time) :
			     try_to_del_timer_sync(&p->timer) >= 0)) {
				mod_timer(&p->timer, time);
			}

			break;
		}
	}

	now = jiffies;
	time = now + br->multicast_last_member_count *
		     br->multicast_last_member_interval;

	if (!port) {
		if (mp->host_joined &&
		    (timer_pending(&mp->timer) ?
		     time_after(mp->timer.expires, time) :
		     try_to_del_timer_sync(&mp->timer) >= 0)) {
			mod_timer(&mp->timer, time);
		}

		goto out;
	}

	for (p = mlock_dereference(mp->ports, br);
	     p != NULL;
	     p = mlock_dereference(p->next, br)) {
		if (p->port != port)
			continue;

		if (!hlist_unhashed(&p->mglist) &&
		    (timer_pending(&p->timer) ?
		     time_after(p->timer.expires, time) :
		     try_to_del_timer_sync(&p->timer) >= 0)) {
			mod_timer(&p->timer, time);
		}

		break;
	}
out:
	spin_unlock(&br->multicast_lock);
}

static void br_ip4_multicast_leave_group(struct net_bridge *br,
					 struct net_bridge_port *port,
					 __be32 group,
					 __u16 vid,
					 const unsigned char *src)
{
	struct br_ip br_group;
	struct bridge_mcast_own_query *own_query;

	if (ipv4_is_local_multicast(group))
		return;

	own_query = port ? &port->ip4_own_query : &br->ip4_own_query;

	br_group.u.ip4 = group;
	br_group.proto = htons(ETH_P_IP);
	br_group.vid = vid;

	br_multicast_leave_group(br, port, &br_group, &br->ip4_other_query,
				 own_query, src);
}

#if IS_ENABLED(CONFIG_IPV6)
static void br_ip6_multicast_leave_group(struct net_bridge *br,
					 struct net_bridge_port *port,
					 const struct in6_addr *group,
					 __u16 vid,
					 const unsigned char *src)
{
	struct br_ip br_group;
	struct bridge_mcast_own_query *own_query;

	if (ipv6_addr_is_ll_all_nodes(group))
		return;

	own_query = port ? &port->ip6_own_query : &br->ip6_own_query;

	br_group.u.ip6 = *group;
	br_group.proto = htons(ETH_P_IPV6);
	br_group.vid = vid;

	br_multicast_leave_group(br, port, &br_group, &br->ip6_other_query,
				 own_query, src);
}
#endif

static void br_multicast_err_count(const struct net_bridge *br,
				   const struct net_bridge_port *p,
				   __be16 proto)
{
	struct bridge_mcast_stats __percpu *stats;
	struct bridge_mcast_stats *pstats;

	if (!br->multicast_stats_enabled)
		return;

	if (p)
		stats = p->mcast_stats;
	else
		stats = br->mcast_stats;
	if (WARN_ON(!stats))
		return;

	pstats = this_cpu_ptr(stats);

	u64_stats_update_begin(&pstats->syncp);
	switch (proto) {
	case htons(ETH_P_IP):
		pstats->mstats.igmp_parse_errors++;
		break;
#if IS_ENABLED(CONFIG_IPV6)
	case htons(ETH_P_IPV6):
		pstats->mstats.mld_parse_errors++;
		break;
#endif
	}
	u64_stats_update_end(&pstats->syncp);
}

static void br_multicast_pim(struct net_bridge *br,
			     struct net_bridge_port *port,
			     const struct sk_buff *skb)
{
	unsigned int offset = skb_transport_offset(skb);
	struct pimhdr *pimhdr, _pimhdr;

	pimhdr = skb_header_pointer(skb, offset, sizeof(_pimhdr), &_pimhdr);
	if (!pimhdr || pim_hdr_version(pimhdr) != PIM_VERSION ||
	    pim_hdr_type(pimhdr) != PIM_TYPE_HELLO)
		return;

	br_multicast_mark_router(br, port);
}

static int br_multicast_ipv4_rcv(struct net_bridge *br,
				 struct net_bridge_port *port,
				 struct sk_buff *skb,
				 u16 vid)
{
	struct sk_buff *skb_trimmed = NULL;
	const unsigned char *src;
	struct igmphdr *ih;
	int err;

	err = ip_mc_check_igmp(skb, &skb_trimmed);

	if (err == -ENOMSG) {
		if (!ipv4_is_local_multicast(ip_hdr(skb)->daddr)) {
			BR_INPUT_SKB_CB(skb)->mrouters_only = 1;
		} else if (pim_ipv4_all_pim_routers(ip_hdr(skb)->daddr)) {
			if (ip_hdr(skb)->protocol == IPPROTO_PIM)
				br_multicast_pim(br, port, skb);
		}
		return 0;
	} else if (err < 0) {
		br_multicast_err_count(br, port, skb->protocol);
		return err;
	}

	ih = igmp_hdr(skb);
	src = eth_hdr(skb)->h_source;
	BR_INPUT_SKB_CB(skb)->igmp = ih->type;

	switch (ih->type) {
	case IGMP_HOST_MEMBERSHIP_REPORT:
	case IGMPV2_HOST_MEMBERSHIP_REPORT:
		BR_INPUT_SKB_CB(skb)->mrouters_only = 1;
		err = br_ip4_multicast_add_group(br, port, ih->group, vid, src);
		break;
	case IGMPV3_HOST_MEMBERSHIP_REPORT:
		err = br_ip4_multicast_igmp3_report(br, port, skb_trimmed, vid);
		break;
	case IGMP_HOST_MEMBERSHIP_QUERY:
		br_ip4_multicast_query(br, port, skb_trimmed, vid);
		break;
	case IGMP_HOST_LEAVE_MESSAGE:
		br_ip4_multicast_leave_group(br, port, ih->group, vid, src);
		break;
	}

	if (skb_trimmed && skb_trimmed != skb)
		kfree_skb(skb_trimmed);

	br_multicast_count(br, port, skb, BR_INPUT_SKB_CB(skb)->igmp,
			   BR_MCAST_DIR_RX);

	return err;
}

#if IS_ENABLED(CONFIG_IPV6)
static int br_multicast_ipv6_rcv(struct net_bridge *br,
				 struct net_bridge_port *port,
				 struct sk_buff *skb,
				 u16 vid)
{
	struct sk_buff *skb_trimmed = NULL;
	const unsigned char *src;
	struct mld_msg *mld;
	int err;

	err = ipv6_mc_check_mld(skb, &skb_trimmed);

	if (err == -ENOMSG) {
		if (!ipv6_addr_is_ll_all_nodes(&ipv6_hdr(skb)->daddr))
			BR_INPUT_SKB_CB(skb)->mrouters_only = 1;
		return 0;
	} else if (err < 0) {
		br_multicast_err_count(br, port, skb->protocol);
		return err;
	}

	mld = (struct mld_msg *)skb_transport_header(skb);
	BR_INPUT_SKB_CB(skb)->igmp = mld->mld_type;

	switch (mld->mld_type) {
	case ICMPV6_MGM_REPORT:
		src = eth_hdr(skb)->h_source;
		BR_INPUT_SKB_CB(skb)->mrouters_only = 1;
		err = br_ip6_multicast_add_group(br, port, &mld->mld_mca, vid,
						 src);
		break;
	case ICMPV6_MLD2_REPORT:
		err = br_ip6_multicast_mld2_report(br, port, skb_trimmed, vid);
		break;
	case ICMPV6_MGM_QUERY:
		err = br_ip6_multicast_query(br, port, skb_trimmed, vid);
		break;
	case ICMPV6_MGM_REDUCTION:
		src = eth_hdr(skb)->h_source;
		br_ip6_multicast_leave_group(br, port, &mld->mld_mca, vid, src);
		break;
	}

	if (skb_trimmed && skb_trimmed != skb)
		kfree_skb(skb_trimmed);

	br_multicast_count(br, port, skb, BR_INPUT_SKB_CB(skb)->igmp,
			   BR_MCAST_DIR_RX);

	return err;
}
#endif

int br_multicast_rcv(struct net_bridge *br, struct net_bridge_port *port,
		     struct sk_buff *skb, u16 vid)
{
	int ret = 0;

	BR_INPUT_SKB_CB(skb)->igmp = 0;
	BR_INPUT_SKB_CB(skb)->mrouters_only = 0;

	if (br->multicast_disabled)
		return 0;

	switch (skb->protocol) {
	case htons(ETH_P_IP):
		ret = br_multicast_ipv4_rcv(br, port, skb, vid);
		break;
#if IS_ENABLED(CONFIG_IPV6)
	case htons(ETH_P_IPV6):
		ret = br_multicast_ipv6_rcv(br, port, skb, vid);
		break;
#endif
	}

	return ret;
}

static void br_multicast_query_expired(struct net_bridge *br,
				       struct bridge_mcast_own_query *query,
				       struct bridge_mcast_querier *querier)
{
	spin_lock(&br->multicast_lock);
	if (query->startup_sent < br->multicast_startup_query_count)
		query->startup_sent++;

	RCU_INIT_POINTER(querier->port, NULL);
	br_multicast_send_query(br, NULL, query);
	spin_unlock(&br->multicast_lock);
}

static void br_ip4_multicast_query_expired(struct timer_list *t)
{
	struct net_bridge *br = from_timer(br, t, ip4_own_query.timer);

	br_multicast_query_expired(br, &br->ip4_own_query, &br->ip4_querier);
}

#if IS_ENABLED(CONFIG_IPV6)
static void br_ip6_multicast_query_expired(struct timer_list *t)
{
	struct net_bridge *br = from_timer(br, t, ip6_own_query.timer);

	br_multicast_query_expired(br, &br->ip6_own_query, &br->ip6_querier);
}
#endif

void br_multicast_init(struct net_bridge *br)
{
	br->hash_elasticity = 4;
	br->hash_max = 512;

	br->multicast_router = MDB_RTR_TYPE_TEMP_QUERY;
	br->multicast_querier = 0;
	br->multicast_query_use_ifaddr = 0;
	br->multicast_last_member_count = 2;
	br->multicast_startup_query_count = 2;

	br->multicast_last_member_interval = HZ;
	br->multicast_query_response_interval = 10 * HZ;
	br->multicast_startup_query_interval = 125 * HZ / 4;
	br->multicast_query_interval = 125 * HZ;
	br->multicast_querier_interval = 255 * HZ;
	br->multicast_membership_interval = 260 * HZ;

	br->ip4_other_query.delay_time = 0;
	br->ip4_querier.port = NULL;
	br->multicast_igmp_version = 2;
#if IS_ENABLED(CONFIG_IPV6)
	br->multicast_mld_version = 1;
	br->ip6_other_query.delay_time = 0;
	br->ip6_querier.port = NULL;
#endif
	br->has_ipv6_addr = 1;

	spin_lock_init(&br->multicast_lock);
	timer_setup(&br->multicast_router_timer,
		    br_multicast_local_router_expired, 0);
	timer_setup(&br->ip4_other_query.timer,
		    br_ip4_multicast_querier_expired, 0);
	timer_setup(&br->ip4_own_query.timer,
		    br_ip4_multicast_query_expired, 0);
#if IS_ENABLED(CONFIG_IPV6)
	timer_setup(&br->ip6_other_query.timer,
		    br_ip6_multicast_querier_expired, 0);
	timer_setup(&br->ip6_own_query.timer,
		    br_ip6_multicast_query_expired, 0);
#endif
}

static void __br_multicast_open(struct net_bridge *br,
				struct bridge_mcast_own_query *query)
{
	query->startup_sent = 0;

	if (br->multicast_disabled)
		return;

	mod_timer(&query->timer, jiffies);
}

void br_multicast_open(struct net_bridge *br)
{
	__br_multicast_open(br, &br->ip4_own_query);
#if IS_ENABLED(CONFIG_IPV6)
	__br_multicast_open(br, &br->ip6_own_query);
#endif
}

void br_multicast_stop(struct net_bridge *br)
{
	del_timer_sync(&br->multicast_router_timer);
	del_timer_sync(&br->ip4_other_query.timer);
	del_timer_sync(&br->ip4_own_query.timer);
#if IS_ENABLED(CONFIG_IPV6)
	del_timer_sync(&br->ip6_other_query.timer);
	del_timer_sync(&br->ip6_own_query.timer);
#endif
}

void br_multicast_dev_del(struct net_bridge *br)
{
	struct net_bridge_mdb_htable *mdb;
	struct net_bridge_mdb_entry *mp;
	struct hlist_node *n;
	u32 ver;
	int i;

	spin_lock_bh(&br->multicast_lock);
	mdb = mlock_dereference(br->mdb, br);
	if (!mdb)
		goto out;

	br->mdb = NULL;

	ver = mdb->ver;
	for (i = 0; i < mdb->max; i++) {
		hlist_for_each_entry_safe(mp, n, &mdb->mhash[i],
					  hlist[ver]) {
			del_timer(&mp->timer);
			call_rcu_bh(&mp->rcu, br_multicast_free_group);
		}
	}

	if (mdb->old) {
		spin_unlock_bh(&br->multicast_lock);
		rcu_barrier_bh();
		spin_lock_bh(&br->multicast_lock);
		WARN_ON(mdb->old);
	}

	mdb->old = mdb;
	call_rcu_bh(&mdb->rcu, br_mdb_free);

out:
	spin_unlock_bh(&br->multicast_lock);
}

int br_multicast_set_router(struct net_bridge *br, unsigned long val)
{
	int err = -EINVAL;

	spin_lock_bh(&br->multicast_lock);

	switch (val) {
	case MDB_RTR_TYPE_DISABLED:
	case MDB_RTR_TYPE_PERM:
		br_mc_router_state_change(br, val == MDB_RTR_TYPE_PERM);
		del_timer(&br->multicast_router_timer);
		br->multicast_router = val;
		err = 0;
		break;
	case MDB_RTR_TYPE_TEMP_QUERY:
		if (br->multicast_router != MDB_RTR_TYPE_TEMP_QUERY)
			br_mc_router_state_change(br, false);
		br->multicast_router = val;
		err = 0;
		break;
	}

	spin_unlock_bh(&br->multicast_lock);

	return err;
}

static void __del_port_router(struct net_bridge_port *p)
{
	if (hlist_unhashed(&p->rlist))
		return;
	hlist_del_init_rcu(&p->rlist);
	br_rtr_notify(p->br->dev, p, RTM_DELMDB);
	br_port_mc_router_state_change(p, false);

	/* don't allow timer refresh */
	if (p->multicast_router == MDB_RTR_TYPE_TEMP)
		p->multicast_router = MDB_RTR_TYPE_TEMP_QUERY;
}

int br_multicast_set_port_router(struct net_bridge_port *p, unsigned long val)
{
	struct net_bridge *br = p->br;
	unsigned long now = jiffies;
	int err = -EINVAL;

	spin_lock(&br->multicast_lock);
	if (p->multicast_router == val) {
		/* Refresh the temp router port timer */
		if (p->multicast_router == MDB_RTR_TYPE_TEMP)
			mod_timer(&p->multicast_router_timer,
				  now + br->multicast_querier_interval);
		err = 0;
		goto unlock;
	}
	switch (val) {
	case MDB_RTR_TYPE_DISABLED:
		p->multicast_router = MDB_RTR_TYPE_DISABLED;
		__del_port_router(p);
		del_timer(&p->multicast_router_timer);
		break;
	case MDB_RTR_TYPE_TEMP_QUERY:
		p->multicast_router = MDB_RTR_TYPE_TEMP_QUERY;
		__del_port_router(p);
		break;
	case MDB_RTR_TYPE_PERM:
		p->multicast_router = MDB_RTR_TYPE_PERM;
		del_timer(&p->multicast_router_timer);
		br_multicast_add_router(br, p);
		break;
	case MDB_RTR_TYPE_TEMP:
		p->multicast_router = MDB_RTR_TYPE_TEMP;
		br_multicast_mark_router(br, p);
		break;
	default:
		goto unlock;
	}
	err = 0;
unlock:
	spin_unlock(&br->multicast_lock);

	return err;
}

static void br_multicast_start_querier(struct net_bridge *br,
				       struct bridge_mcast_own_query *query)
{
	struct net_bridge_port *port;

	__br_multicast_open(br, query);

	rcu_read_lock();
	list_for_each_entry_rcu(port, &br->port_list, list) {
		if (port->state == BR_STATE_DISABLED ||
		    port->state == BR_STATE_BLOCKING)
			continue;

		if (query == &br->ip4_own_query)
			br_multicast_enable(&port->ip4_own_query);
#if IS_ENABLED(CONFIG_IPV6)
		else
			br_multicast_enable(&port->ip6_own_query);
#endif
	}
	rcu_read_unlock();
}

int br_multicast_toggle(struct net_bridge *br, unsigned long val)
{
	struct net_bridge_mdb_htable *mdb;
	struct net_bridge_port *port;
	int err = 0;

	spin_lock_bh(&br->multicast_lock);
	if (br->multicast_disabled == !val)
		goto unlock;

	br_mc_disabled_update(br->dev, !val);
	br->multicast_disabled = !val;
	if (br->multicast_disabled)
		goto unlock;

	if (!netif_running(br->dev))
		goto unlock;

	mdb = mlock_dereference(br->mdb, br);
	if (mdb) {
		if (mdb->old) {
			err = -EEXIST;
rollback:
			br->multicast_disabled = !!val;
			goto unlock;
		}

		err = br_mdb_rehash(&br->mdb, mdb->max,
				    br->hash_elasticity);
		if (err)
			goto rollback;
	}

	br_multicast_open(br);
	list_for_each_entry(port, &br->port_list, list)
		__br_multicast_enable_port(port);

unlock:
	spin_unlock_bh(&br->multicast_lock);

	return err;
}

bool br_multicast_enabled(const struct net_device *dev)
{
	struct net_bridge *br = netdev_priv(dev);

	return !br->multicast_disabled;
}
EXPORT_SYMBOL_GPL(br_multicast_enabled);

bool br_multicast_router(const struct net_device *dev)
{
	struct net_bridge *br = netdev_priv(dev);
	bool is_router;

	spin_lock_bh(&br->multicast_lock);
	is_router = br_multicast_is_router(br);
	spin_unlock_bh(&br->multicast_lock);
	return is_router;
}
EXPORT_SYMBOL_GPL(br_multicast_router);

int br_multicast_set_querier(struct net_bridge *br, unsigned long val)
{
	unsigned long max_delay;

	val = !!val;

	spin_lock_bh(&br->multicast_lock);
	if (br->multicast_querier == val)
		goto unlock;

	br->multicast_querier = val;
	if (!val)
		goto unlock;

	max_delay = br->multicast_query_response_interval;

	if (!timer_pending(&br->ip4_other_query.timer))
		br->ip4_other_query.delay_time = jiffies + max_delay;

	br_multicast_start_querier(br, &br->ip4_own_query);

#if IS_ENABLED(CONFIG_IPV6)
	if (!timer_pending(&br->ip6_other_query.timer))
		br->ip6_other_query.delay_time = jiffies + max_delay;

	br_multicast_start_querier(br, &br->ip6_own_query);
#endif

unlock:
	spin_unlock_bh(&br->multicast_lock);

	return 0;
}

int br_multicast_set_hash_max(struct net_bridge *br, unsigned long val)
{
	int err = -EINVAL;
	u32 old;
	struct net_bridge_mdb_htable *mdb;

	spin_lock_bh(&br->multicast_lock);
	if (!is_power_of_2(val))
		goto unlock;

	mdb = mlock_dereference(br->mdb, br);
	if (mdb && val < mdb->size)
		goto unlock;

	err = 0;

	old = br->hash_max;
	br->hash_max = val;

	if (mdb) {
		if (mdb->old) {
			err = -EEXIST;
rollback:
			br->hash_max = old;
			goto unlock;
		}

		err = br_mdb_rehash(&br->mdb, br->hash_max,
				    br->hash_elasticity);
		if (err)
			goto rollback;
	}

unlock:
	spin_unlock_bh(&br->multicast_lock);

	return err;
}

int br_multicast_set_igmp_version(struct net_bridge *br, unsigned long val)
{
	/* Currently we support only version 2 and 3 */
	switch (val) {
	case 2:
	case 3:
		break;
	default:
		return -EINVAL;
	}

	spin_lock_bh(&br->multicast_lock);
	br->multicast_igmp_version = val;
	spin_unlock_bh(&br->multicast_lock);

	return 0;
}

#if IS_ENABLED(CONFIG_IPV6)
int br_multicast_set_mld_version(struct net_bridge *br, unsigned long val)
{
	/* Currently we support version 1 and 2 */
	switch (val) {
	case 1:
	case 2:
		break;
	default:
		return -EINVAL;
	}

	spin_lock_bh(&br->multicast_lock);
	br->multicast_mld_version = val;
	spin_unlock_bh(&br->multicast_lock);

	return 0;
}
#endif

/**
 * br_multicast_list_adjacent - Returns snooped multicast addresses
 * @dev:	The bridge port adjacent to which to retrieve addresses
 * @br_ip_list:	The list to store found, snooped multicast IP addresses in
 *
 * Creates a list of IP addresses (struct br_ip_list) sensed by the multicast
 * snooping feature on all bridge ports of dev's bridge device, excluding
 * the addresses from dev itself.
 *
 * Returns the number of items added to br_ip_list.
 *
 * Notes:
 * - br_ip_list needs to be initialized by caller
 * - br_ip_list might contain duplicates in the end
 *   (needs to be taken care of by caller)
 * - br_ip_list needs to be freed by caller
 */
int br_multicast_list_adjacent(struct net_device *dev,
			       struct list_head *br_ip_list)
{
	struct net_bridge *br;
	struct net_bridge_port *port;
	struct net_bridge_port_group *group;
	struct br_ip_list *entry;
	int count = 0;

	rcu_read_lock();
	if (!br_ip_list || !br_port_exists(dev))
		goto unlock;

	port = br_port_get_rcu(dev);
	if (!port || !port->br)
		goto unlock;

	br = port->br;

	list_for_each_entry_rcu(port, &br->port_list, list) {
		if (!port->dev || port->dev == dev)
			continue;

		hlist_for_each_entry_rcu(group, &port->mglist, mglist) {
			entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
			if (!entry)
				goto unlock;

			entry->addr = group->addr;
			list_add(&entry->list, br_ip_list);
			count++;
		}
	}

unlock:
	rcu_read_unlock();
	return count;
}
EXPORT_SYMBOL_GPL(br_multicast_list_adjacent);

/**
 * br_multicast_has_querier_anywhere - Checks for a querier on a bridge
 * @dev: The bridge port providing the bridge on which to check for a querier
 * @proto: The protocol family to check for: IGMP -> ETH_P_IP, MLD -> ETH_P_IPV6
 *
 * Checks whether the given interface has a bridge on top and if so returns
 * true if a valid querier exists anywhere on the bridged link layer.
 * Otherwise returns false.
 */
bool br_multicast_has_querier_anywhere(struct net_device *dev, int proto)
{
	struct net_bridge *br;
	struct net_bridge_port *port;
	struct ethhdr eth;
	bool ret = false;

	rcu_read_lock();
	if (!br_port_exists(dev))
		goto unlock;

	port = br_port_get_rcu(dev);
	if (!port || !port->br)
		goto unlock;

	br = port->br;

	memset(&eth, 0, sizeof(eth));
	eth.h_proto = htons(proto);

	ret = br_multicast_querier_exists(br, &eth);

unlock:
	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL_GPL(br_multicast_has_querier_anywhere);

/**
 * br_multicast_has_querier_adjacent - Checks for a querier behind a bridge port
 * @dev: The bridge port adjacent to which to check for a querier
 * @proto: The protocol family to check for: IGMP -> ETH_P_IP, MLD -> ETH_P_IPV6
 *
 * Checks whether the given interface has a bridge on top and if so returns
 * true if a selected querier is behind one of the other ports of this
 * bridge. Otherwise returns false.
 */
bool br_multicast_has_querier_adjacent(struct net_device *dev, int proto)
{
	struct net_bridge *br;
	struct net_bridge_port *port;
	bool ret = false;

	rcu_read_lock();
	if (!br_port_exists(dev))
		goto unlock;

	port = br_port_get_rcu(dev);
	if (!port || !port->br)
		goto unlock;

	br = port->br;

	switch (proto) {
	case ETH_P_IP:
		if (!timer_pending(&br->ip4_other_query.timer) ||
		    rcu_dereference(br->ip4_querier.port) == port)
			goto unlock;
		break;
#if IS_ENABLED(CONFIG_IPV6)
	case ETH_P_IPV6:
		if (!timer_pending(&br->ip6_other_query.timer) ||
		    rcu_dereference(br->ip6_querier.port) == port)
			goto unlock;
		break;
#endif
	default:
		goto unlock;
	}

	ret = true;
unlock:
	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL_GPL(br_multicast_has_querier_adjacent);

static void br_mcast_stats_add(struct bridge_mcast_stats __percpu *stats,
			       const struct sk_buff *skb, u8 type, u8 dir)
{
	struct bridge_mcast_stats *pstats = this_cpu_ptr(stats);
	__be16 proto = skb->protocol;
	unsigned int t_len;

	u64_stats_update_begin(&pstats->syncp);
	switch (proto) {
	case htons(ETH_P_IP):
		t_len = ntohs(ip_hdr(skb)->tot_len) - ip_hdrlen(skb);
		switch (type) {
		case IGMP_HOST_MEMBERSHIP_REPORT:
			pstats->mstats.igmp_v1reports[dir]++;
			break;
		case IGMPV2_HOST_MEMBERSHIP_REPORT:
			pstats->mstats.igmp_v2reports[dir]++;
			break;
		case IGMPV3_HOST_MEMBERSHIP_REPORT:
			pstats->mstats.igmp_v3reports[dir]++;
			break;
		case IGMP_HOST_MEMBERSHIP_QUERY:
			if (t_len != sizeof(struct igmphdr)) {
				pstats->mstats.igmp_v3queries[dir]++;
			} else {
				unsigned int offset = skb_transport_offset(skb);
				struct igmphdr *ih, _ihdr;

				ih = skb_header_pointer(skb, offset,
							sizeof(_ihdr), &_ihdr);
				if (!ih)
					break;
				if (!ih->code)
					pstats->mstats.igmp_v1queries[dir]++;
				else
					pstats->mstats.igmp_v2queries[dir]++;
			}
			break;
		case IGMP_HOST_LEAVE_MESSAGE:
			pstats->mstats.igmp_leaves[dir]++;
			break;
		}
		break;
#if IS_ENABLED(CONFIG_IPV6)
	case htons(ETH_P_IPV6):
		t_len = ntohs(ipv6_hdr(skb)->payload_len) +
			sizeof(struct ipv6hdr);
		t_len -= skb_network_header_len(skb);
		switch (type) {
		case ICMPV6_MGM_REPORT:
			pstats->mstats.mld_v1reports[dir]++;
			break;
		case ICMPV6_MLD2_REPORT:
			pstats->mstats.mld_v2reports[dir]++;
			break;
		case ICMPV6_MGM_QUERY:
			if (t_len != sizeof(struct mld_msg))
				pstats->mstats.mld_v2queries[dir]++;
			else
				pstats->mstats.mld_v1queries[dir]++;
			break;
		case ICMPV6_MGM_REDUCTION:
			pstats->mstats.mld_leaves[dir]++;
			break;
		}
		break;
#endif /* CONFIG_IPV6 */
	}
	u64_stats_update_end(&pstats->syncp);
}

void br_multicast_count(struct net_bridge *br, const struct net_bridge_port *p,
			const struct sk_buff *skb, u8 type, u8 dir)
{
	struct bridge_mcast_stats __percpu *stats;

	/* if multicast_disabled is true then igmp type can't be set */
	if (!type || !br->multicast_stats_enabled)
		return;

	if (p)
		stats = p->mcast_stats;
	else
		stats = br->mcast_stats;
	if (WARN_ON(!stats))
		return;

	br_mcast_stats_add(stats, skb, type, dir);
}

int br_multicast_init_stats(struct net_bridge *br)
{
	br->mcast_stats = netdev_alloc_pcpu_stats(struct bridge_mcast_stats);
	if (!br->mcast_stats)
		return -ENOMEM;

	return 0;
}

void br_multicast_uninit_stats(struct net_bridge *br)
{
	free_percpu(br->mcast_stats);
}

static void mcast_stats_add_dir(u64 *dst, u64 *src)
{
	dst[BR_MCAST_DIR_RX] += src[BR_MCAST_DIR_RX];
	dst[BR_MCAST_DIR_TX] += src[BR_MCAST_DIR_TX];
}

void br_multicast_get_stats(const struct net_bridge *br,
			    const struct net_bridge_port *p,
			    struct br_mcast_stats *dest)
{
	struct bridge_mcast_stats __percpu *stats;
	struct br_mcast_stats tdst;
	int i;

	memset(dest, 0, sizeof(*dest));
	if (p)
		stats = p->mcast_stats;
	else
		stats = br->mcast_stats;
	if (WARN_ON(!stats))
		return;

	memset(&tdst, 0, sizeof(tdst));
	for_each_possible_cpu(i) {
		struct bridge_mcast_stats *cpu_stats = per_cpu_ptr(stats, i);
		struct br_mcast_stats temp;
		unsigned int start;

		do {
			start = u64_stats_fetch_begin_irq(&cpu_stats->syncp);
			memcpy(&temp, &cpu_stats->mstats, sizeof(temp));
		} while (u64_stats_fetch_retry_irq(&cpu_stats->syncp, start));

		mcast_stats_add_dir(tdst.igmp_v1queries, temp.igmp_v1queries);
		mcast_stats_add_dir(tdst.igmp_v2queries, temp.igmp_v2queries);
		mcast_stats_add_dir(tdst.igmp_v3queries, temp.igmp_v3queries);
		mcast_stats_add_dir(tdst.igmp_leaves, temp.igmp_leaves);
		mcast_stats_add_dir(tdst.igmp_v1reports, temp.igmp_v1reports);
		mcast_stats_add_dir(tdst.igmp_v2reports, temp.igmp_v2reports);
		mcast_stats_add_dir(tdst.igmp_v3reports, temp.igmp_v3reports);
		tdst.igmp_parse_errors += temp.igmp_parse_errors;

		mcast_stats_add_dir(tdst.mld_v1queries, temp.mld_v1queries);
		mcast_stats_add_dir(tdst.mld_v2queries, temp.mld_v2queries);
		mcast_stats_add_dir(tdst.mld_leaves, temp.mld_leaves);
		mcast_stats_add_dir(tdst.mld_v1reports, temp.mld_v1reports);
		mcast_stats_add_dir(tdst.mld_v2reports, temp.mld_v2reports);
		tdst.mld_parse_errors += temp.mld_parse_errors;
	}
	memcpy(dest, &tdst, sizeof(*dest));
}
