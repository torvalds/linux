/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		IPv4 Forwarding Information Base: semantics.
 *
 * Version:	$Id: fib_semantics.c,v 1.19 2002/01/12 07:54:56 davem Exp $
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/bitops.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/proc_fs.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/init.h>

#include <net/ip.h>
#include <net/protocol.h>
#include <net/route.h>
#include <net/tcp.h>
#include <net/sock.h>
#include <net/ip_fib.h>
#include <net/ip_mp_alg.h>

#include "fib_lookup.h"

#define FSprintk(a...)

static DEFINE_RWLOCK(fib_info_lock);
static struct hlist_head *fib_info_hash;
static struct hlist_head *fib_info_laddrhash;
static unsigned int fib_hash_size;
static unsigned int fib_info_cnt;

#define DEVINDEX_HASHBITS 8
#define DEVINDEX_HASHSIZE (1U << DEVINDEX_HASHBITS)
static struct hlist_head fib_info_devhash[DEVINDEX_HASHSIZE];

#ifdef CONFIG_IP_ROUTE_MULTIPATH

static DEFINE_SPINLOCK(fib_multipath_lock);

#define for_nexthops(fi) { int nhsel; const struct fib_nh * nh; \
for (nhsel=0, nh = (fi)->fib_nh; nhsel < (fi)->fib_nhs; nh++, nhsel++)

#define change_nexthops(fi) { int nhsel; struct fib_nh * nh; \
for (nhsel=0, nh = (struct fib_nh*)((fi)->fib_nh); nhsel < (fi)->fib_nhs; nh++, nhsel++)

#else /* CONFIG_IP_ROUTE_MULTIPATH */

/* Hope, that gcc will optimize it to get rid of dummy loop */

#define for_nexthops(fi) { int nhsel=0; const struct fib_nh * nh = (fi)->fib_nh; \
for (nhsel=0; nhsel < 1; nhsel++)

#define change_nexthops(fi) { int nhsel=0; struct fib_nh * nh = (struct fib_nh*)((fi)->fib_nh); \
for (nhsel=0; nhsel < 1; nhsel++)

#endif /* CONFIG_IP_ROUTE_MULTIPATH */

#define endfor_nexthops(fi) }


static struct 
{
	int	error;
	u8	scope;
} fib_props[RTA_MAX + 1] = {
        {
		.error	= 0,
		.scope	= RT_SCOPE_NOWHERE,
	},	/* RTN_UNSPEC */
	{
		.error	= 0,
		.scope	= RT_SCOPE_UNIVERSE,
	},	/* RTN_UNICAST */
	{
		.error	= 0,
		.scope	= RT_SCOPE_HOST,
	},	/* RTN_LOCAL */
	{
		.error	= 0,
		.scope	= RT_SCOPE_LINK,
	},	/* RTN_BROADCAST */
	{
		.error	= 0,
		.scope	= RT_SCOPE_LINK,
	},	/* RTN_ANYCAST */
	{
		.error	= 0,
		.scope	= RT_SCOPE_UNIVERSE,
	},	/* RTN_MULTICAST */
	{
		.error	= -EINVAL,
		.scope	= RT_SCOPE_UNIVERSE,
	},	/* RTN_BLACKHOLE */
	{
		.error	= -EHOSTUNREACH,
		.scope	= RT_SCOPE_UNIVERSE,
	},	/* RTN_UNREACHABLE */
	{
		.error	= -EACCES,
		.scope	= RT_SCOPE_UNIVERSE,
	},	/* RTN_PROHIBIT */
	{
		.error	= -EAGAIN,
		.scope	= RT_SCOPE_UNIVERSE,
	},	/* RTN_THROW */
	{
		.error	= -EINVAL,
		.scope	= RT_SCOPE_NOWHERE,
	},	/* RTN_NAT */
	{
		.error	= -EINVAL,
		.scope	= RT_SCOPE_NOWHERE,
	},	/* RTN_XRESOLVE */
};


/* Release a nexthop info record */

void free_fib_info(struct fib_info *fi)
{
	if (fi->fib_dead == 0) {
		printk("Freeing alive fib_info %p\n", fi);
		return;
	}
	change_nexthops(fi) {
		if (nh->nh_dev)
			dev_put(nh->nh_dev);
		nh->nh_dev = NULL;
	} endfor_nexthops(fi);
	fib_info_cnt--;
	kfree(fi);
}

void fib_release_info(struct fib_info *fi)
{
	write_lock(&fib_info_lock);
	if (fi && --fi->fib_treeref == 0) {
		hlist_del(&fi->fib_hash);
		if (fi->fib_prefsrc)
			hlist_del(&fi->fib_lhash);
		change_nexthops(fi) {
			if (!nh->nh_dev)
				continue;
			hlist_del(&nh->nh_hash);
		} endfor_nexthops(fi)
		fi->fib_dead = 1;
		fib_info_put(fi);
	}
	write_unlock(&fib_info_lock);
}

static __inline__ int nh_comp(const struct fib_info *fi, const struct fib_info *ofi)
{
	const struct fib_nh *onh = ofi->fib_nh;

	for_nexthops(fi) {
		if (nh->nh_oif != onh->nh_oif ||
		    nh->nh_gw  != onh->nh_gw ||
		    nh->nh_scope != onh->nh_scope ||
#ifdef CONFIG_IP_ROUTE_MULTIPATH
		    nh->nh_weight != onh->nh_weight ||
#endif
#ifdef CONFIG_NET_CLS_ROUTE
		    nh->nh_tclassid != onh->nh_tclassid ||
#endif
		    ((nh->nh_flags^onh->nh_flags)&~RTNH_F_DEAD))
			return -1;
		onh++;
	} endfor_nexthops(fi);
	return 0;
}

static inline unsigned int fib_info_hashfn(const struct fib_info *fi)
{
	unsigned int mask = (fib_hash_size - 1);
	unsigned int val = fi->fib_nhs;

	val ^= fi->fib_protocol;
	val ^= fi->fib_prefsrc;
	val ^= fi->fib_priority;

	return (val ^ (val >> 7) ^ (val >> 12)) & mask;
}

static struct fib_info *fib_find_info(const struct fib_info *nfi)
{
	struct hlist_head *head;
	struct hlist_node *node;
	struct fib_info *fi;
	unsigned int hash;

	hash = fib_info_hashfn(nfi);
	head = &fib_info_hash[hash];

	hlist_for_each_entry(fi, node, head, fib_hash) {
		if (fi->fib_nhs != nfi->fib_nhs)
			continue;
		if (nfi->fib_protocol == fi->fib_protocol &&
		    nfi->fib_prefsrc == fi->fib_prefsrc &&
		    nfi->fib_priority == fi->fib_priority &&
		    memcmp(nfi->fib_metrics, fi->fib_metrics,
			   sizeof(fi->fib_metrics)) == 0 &&
		    ((nfi->fib_flags^fi->fib_flags)&~RTNH_F_DEAD) == 0 &&
		    (nfi->fib_nhs == 0 || nh_comp(fi, nfi) == 0))
			return fi;
	}

	return NULL;
}

static inline unsigned int fib_devindex_hashfn(unsigned int val)
{
	unsigned int mask = DEVINDEX_HASHSIZE - 1;

	return (val ^
		(val >> DEVINDEX_HASHBITS) ^
		(val >> (DEVINDEX_HASHBITS * 2))) & mask;
}

/* Check, that the gateway is already configured.
   Used only by redirect accept routine.
 */

int ip_fib_check_default(u32 gw, struct net_device *dev)
{
	struct hlist_head *head;
	struct hlist_node *node;
	struct fib_nh *nh;
	unsigned int hash;

	read_lock(&fib_info_lock);

	hash = fib_devindex_hashfn(dev->ifindex);
	head = &fib_info_devhash[hash];
	hlist_for_each_entry(nh, node, head, nh_hash) {
		if (nh->nh_dev == dev &&
		    nh->nh_gw == gw &&
		    !(nh->nh_flags&RTNH_F_DEAD)) {
			read_unlock(&fib_info_lock);
			return 0;
		}
	}

	read_unlock(&fib_info_lock);

	return -1;
}

void rtmsg_fib(int event, u32 key, struct fib_alias *fa,
	       int z, int tb_id,
	       struct nlmsghdr *n, struct netlink_skb_parms *req)
{
	struct sk_buff *skb;
	u32 pid = req ? req->pid : n->nlmsg_pid;
	int size = NLMSG_SPACE(sizeof(struct rtmsg)+256);

	skb = alloc_skb(size, GFP_KERNEL);
	if (!skb)
		return;

	if (fib_dump_info(skb, pid, n->nlmsg_seq, event, tb_id,
			  fa->fa_type, fa->fa_scope, &key, z,
			  fa->fa_tos,
			  fa->fa_info, 0) < 0) {
		kfree_skb(skb);
		return;
	}
	NETLINK_CB(skb).dst_group = RTNLGRP_IPV4_ROUTE;
	if (n->nlmsg_flags&NLM_F_ECHO)
		atomic_inc(&skb->users);
	netlink_broadcast(rtnl, skb, pid, RTNLGRP_IPV4_ROUTE, GFP_KERNEL);
	if (n->nlmsg_flags&NLM_F_ECHO)
		netlink_unicast(rtnl, skb, pid, MSG_DONTWAIT);
}

/* Return the first fib alias matching TOS with
 * priority less than or equal to PRIO.
 */
struct fib_alias *fib_find_alias(struct list_head *fah, u8 tos, u32 prio)
{
	if (fah) {
		struct fib_alias *fa;
		list_for_each_entry(fa, fah, fa_list) {
			if (fa->fa_tos > tos)
				continue;
			if (fa->fa_info->fib_priority >= prio ||
			    fa->fa_tos < tos)
				return fa;
		}
	}
	return NULL;
}

int fib_detect_death(struct fib_info *fi, int order,
		     struct fib_info **last_resort, int *last_idx, int *dflt)
{
	struct neighbour *n;
	int state = NUD_NONE;

	n = neigh_lookup(&arp_tbl, &fi->fib_nh[0].nh_gw, fi->fib_dev);
	if (n) {
		state = n->nud_state;
		neigh_release(n);
	}
	if (state==NUD_REACHABLE)
		return 0;
	if ((state&NUD_VALID) && order != *dflt)
		return 0;
	if ((state&NUD_VALID) ||
	    (*last_idx<0 && order > *dflt)) {
		*last_resort = fi;
		*last_idx = order;
	}
	return 1;
}

#ifdef CONFIG_IP_ROUTE_MULTIPATH

static u32 fib_get_attr32(struct rtattr *attr, int attrlen, int type)
{
	while (RTA_OK(attr,attrlen)) {
		if (attr->rta_type == type)
			return *(u32*)RTA_DATA(attr);
		attr = RTA_NEXT(attr, attrlen);
	}
	return 0;
}

static int
fib_count_nexthops(struct rtattr *rta)
{
	int nhs = 0;
	struct rtnexthop *nhp = RTA_DATA(rta);
	int nhlen = RTA_PAYLOAD(rta);

	while (nhlen >= (int)sizeof(struct rtnexthop)) {
		if ((nhlen -= nhp->rtnh_len) < 0)
			return 0;
		nhs++;
		nhp = RTNH_NEXT(nhp);
	};
	return nhs;
}

static int
fib_get_nhs(struct fib_info *fi, const struct rtattr *rta, const struct rtmsg *r)
{
	struct rtnexthop *nhp = RTA_DATA(rta);
	int nhlen = RTA_PAYLOAD(rta);

	change_nexthops(fi) {
		int attrlen = nhlen - sizeof(struct rtnexthop);
		if (attrlen < 0 || (nhlen -= nhp->rtnh_len) < 0)
			return -EINVAL;
		nh->nh_flags = (r->rtm_flags&~0xFF) | nhp->rtnh_flags;
		nh->nh_oif = nhp->rtnh_ifindex;
		nh->nh_weight = nhp->rtnh_hops + 1;
		if (attrlen) {
			nh->nh_gw = fib_get_attr32(RTNH_DATA(nhp), attrlen, RTA_GATEWAY);
#ifdef CONFIG_NET_CLS_ROUTE
			nh->nh_tclassid = fib_get_attr32(RTNH_DATA(nhp), attrlen, RTA_FLOW);
#endif
		}
		nhp = RTNH_NEXT(nhp);
	} endfor_nexthops(fi);
	return 0;
}

#endif

int fib_nh_match(struct rtmsg *r, struct nlmsghdr *nlh, struct kern_rta *rta,
		 struct fib_info *fi)
{
#ifdef CONFIG_IP_ROUTE_MULTIPATH
	struct rtnexthop *nhp;
	int nhlen;
#endif

	if (rta->rta_priority &&
	    *rta->rta_priority != fi->fib_priority)
		return 1;

	if (rta->rta_oif || rta->rta_gw) {
		if ((!rta->rta_oif || *rta->rta_oif == fi->fib_nh->nh_oif) &&
		    (!rta->rta_gw  || memcmp(rta->rta_gw, &fi->fib_nh->nh_gw, 4) == 0))
			return 0;
		return 1;
	}

#ifdef CONFIG_IP_ROUTE_MULTIPATH
	if (rta->rta_mp == NULL)
		return 0;
	nhp = RTA_DATA(rta->rta_mp);
	nhlen = RTA_PAYLOAD(rta->rta_mp);
	
	for_nexthops(fi) {
		int attrlen = nhlen - sizeof(struct rtnexthop);
		u32 gw;

		if (attrlen < 0 || (nhlen -= nhp->rtnh_len) < 0)
			return -EINVAL;
		if (nhp->rtnh_ifindex && nhp->rtnh_ifindex != nh->nh_oif)
			return 1;
		if (attrlen) {
			gw = fib_get_attr32(RTNH_DATA(nhp), attrlen, RTA_GATEWAY);
			if (gw && gw != nh->nh_gw)
				return 1;
#ifdef CONFIG_NET_CLS_ROUTE
			gw = fib_get_attr32(RTNH_DATA(nhp), attrlen, RTA_FLOW);
			if (gw && gw != nh->nh_tclassid)
				return 1;
#endif
		}
		nhp = RTNH_NEXT(nhp);
	} endfor_nexthops(fi);
#endif
	return 0;
}


/*
   Picture
   -------

   Semantics of nexthop is very messy by historical reasons.
   We have to take into account, that:
   a) gateway can be actually local interface address,
      so that gatewayed route is direct.
   b) gateway must be on-link address, possibly
      described not by an ifaddr, but also by a direct route.
   c) If both gateway and interface are specified, they should not
      contradict.
   d) If we use tunnel routes, gateway could be not on-link.

   Attempt to reconcile all of these (alas, self-contradictory) conditions
   results in pretty ugly and hairy code with obscure logic.

   I chose to generalized it instead, so that the size
   of code does not increase practically, but it becomes
   much more general.
   Every prefix is assigned a "scope" value: "host" is local address,
   "link" is direct route,
   [ ... "site" ... "interior" ... ]
   and "universe" is true gateway route with global meaning.

   Every prefix refers to a set of "nexthop"s (gw, oif),
   where gw must have narrower scope. This recursion stops
   when gw has LOCAL scope or if "nexthop" is declared ONLINK,
   which means that gw is forced to be on link.

   Code is still hairy, but now it is apparently logically
   consistent and very flexible. F.e. as by-product it allows
   to co-exists in peace independent exterior and interior
   routing processes.

   Normally it looks as following.

   {universe prefix}  -> (gw, oif) [scope link]
                          |
			  |-> {link prefix} -> (gw, oif) [scope local]
			                        |
						|-> {local prefix} (terminal node)
 */

static int fib_check_nh(const struct rtmsg *r, struct fib_info *fi, struct fib_nh *nh)
{
	int err;

	if (nh->nh_gw) {
		struct fib_result res;

#ifdef CONFIG_IP_ROUTE_PERVASIVE
		if (nh->nh_flags&RTNH_F_PERVASIVE)
			return 0;
#endif
		if (nh->nh_flags&RTNH_F_ONLINK) {
			struct net_device *dev;

			if (r->rtm_scope >= RT_SCOPE_LINK)
				return -EINVAL;
			if (inet_addr_type(nh->nh_gw) != RTN_UNICAST)
				return -EINVAL;
			if ((dev = __dev_get_by_index(nh->nh_oif)) == NULL)
				return -ENODEV;
			if (!(dev->flags&IFF_UP))
				return -ENETDOWN;
			nh->nh_dev = dev;
			dev_hold(dev);
			nh->nh_scope = RT_SCOPE_LINK;
			return 0;
		}
		{
			struct flowi fl = { .nl_u = { .ip4_u =
						      { .daddr = nh->nh_gw,
							.scope = r->rtm_scope + 1 } },
					    .oif = nh->nh_oif };

			/* It is not necessary, but requires a bit of thinking */
			if (fl.fl4_scope < RT_SCOPE_LINK)
				fl.fl4_scope = RT_SCOPE_LINK;
			if ((err = fib_lookup(&fl, &res)) != 0)
				return err;
		}
		err = -EINVAL;
		if (res.type != RTN_UNICAST && res.type != RTN_LOCAL)
			goto out;
		nh->nh_scope = res.scope;
		nh->nh_oif = FIB_RES_OIF(res);
		if ((nh->nh_dev = FIB_RES_DEV(res)) == NULL)
			goto out;
		dev_hold(nh->nh_dev);
		err = -ENETDOWN;
		if (!(nh->nh_dev->flags & IFF_UP))
			goto out;
		err = 0;
out:
		fib_res_put(&res);
		return err;
	} else {
		struct in_device *in_dev;

		if (nh->nh_flags&(RTNH_F_PERVASIVE|RTNH_F_ONLINK))
			return -EINVAL;

		in_dev = inetdev_by_index(nh->nh_oif);
		if (in_dev == NULL)
			return -ENODEV;
		if (!(in_dev->dev->flags&IFF_UP)) {
			in_dev_put(in_dev);
			return -ENETDOWN;
		}
		nh->nh_dev = in_dev->dev;
		dev_hold(nh->nh_dev);
		nh->nh_scope = RT_SCOPE_HOST;
		in_dev_put(in_dev);
	}
	return 0;
}

static inline unsigned int fib_laddr_hashfn(u32 val)
{
	unsigned int mask = (fib_hash_size - 1);

	return (val ^ (val >> 7) ^ (val >> 14)) & mask;
}

static struct hlist_head *fib_hash_alloc(int bytes)
{
	if (bytes <= PAGE_SIZE)
		return kmalloc(bytes, GFP_KERNEL);
	else
		return (struct hlist_head *)
			__get_free_pages(GFP_KERNEL, get_order(bytes));
}

static void fib_hash_free(struct hlist_head *hash, int bytes)
{
	if (!hash)
		return;

	if (bytes <= PAGE_SIZE)
		kfree(hash);
	else
		free_pages((unsigned long) hash, get_order(bytes));
}

static void fib_hash_move(struct hlist_head *new_info_hash,
			  struct hlist_head *new_laddrhash,
			  unsigned int new_size)
{
	struct hlist_head *old_info_hash, *old_laddrhash;
	unsigned int old_size = fib_hash_size;
	unsigned int i, bytes;

	write_lock(&fib_info_lock);
	old_info_hash = fib_info_hash;
	old_laddrhash = fib_info_laddrhash;
	fib_hash_size = new_size;

	for (i = 0; i < old_size; i++) {
		struct hlist_head *head = &fib_info_hash[i];
		struct hlist_node *node, *n;
		struct fib_info *fi;

		hlist_for_each_entry_safe(fi, node, n, head, fib_hash) {
			struct hlist_head *dest;
			unsigned int new_hash;

			hlist_del(&fi->fib_hash);

			new_hash = fib_info_hashfn(fi);
			dest = &new_info_hash[new_hash];
			hlist_add_head(&fi->fib_hash, dest);
		}
	}
	fib_info_hash = new_info_hash;

	for (i = 0; i < old_size; i++) {
		struct hlist_head *lhead = &fib_info_laddrhash[i];
		struct hlist_node *node, *n;
		struct fib_info *fi;

		hlist_for_each_entry_safe(fi, node, n, lhead, fib_lhash) {
			struct hlist_head *ldest;
			unsigned int new_hash;

			hlist_del(&fi->fib_lhash);

			new_hash = fib_laddr_hashfn(fi->fib_prefsrc);
			ldest = &new_laddrhash[new_hash];
			hlist_add_head(&fi->fib_lhash, ldest);
		}
	}
	fib_info_laddrhash = new_laddrhash;

	write_unlock(&fib_info_lock);

	bytes = old_size * sizeof(struct hlist_head *);
	fib_hash_free(old_info_hash, bytes);
	fib_hash_free(old_laddrhash, bytes);
}

struct fib_info *
fib_create_info(const struct rtmsg *r, struct kern_rta *rta,
		const struct nlmsghdr *nlh, int *errp)
{
	int err;
	struct fib_info *fi = NULL;
	struct fib_info *ofi;
#ifdef CONFIG_IP_ROUTE_MULTIPATH
	int nhs = 1;
#else
	const int nhs = 1;
#endif
#ifdef CONFIG_IP_ROUTE_MULTIPATH_CACHED
	u32 mp_alg = IP_MP_ALG_NONE;
#endif

	/* Fast check to catch the most weird cases */
	if (fib_props[r->rtm_type].scope > r->rtm_scope)
		goto err_inval;

#ifdef CONFIG_IP_ROUTE_MULTIPATH
	if (rta->rta_mp) {
		nhs = fib_count_nexthops(rta->rta_mp);
		if (nhs == 0)
			goto err_inval;
	}
#endif
#ifdef CONFIG_IP_ROUTE_MULTIPATH_CACHED
	if (rta->rta_mp_alg) {
		mp_alg = *rta->rta_mp_alg;

		if (mp_alg < IP_MP_ALG_NONE ||
		    mp_alg > IP_MP_ALG_MAX)
			goto err_inval;
	}
#endif

	err = -ENOBUFS;
	if (fib_info_cnt >= fib_hash_size) {
		unsigned int new_size = fib_hash_size << 1;
		struct hlist_head *new_info_hash;
		struct hlist_head *new_laddrhash;
		unsigned int bytes;

		if (!new_size)
			new_size = 1;
		bytes = new_size * sizeof(struct hlist_head *);
		new_info_hash = fib_hash_alloc(bytes);
		new_laddrhash = fib_hash_alloc(bytes);
		if (!new_info_hash || !new_laddrhash) {
			fib_hash_free(new_info_hash, bytes);
			fib_hash_free(new_laddrhash, bytes);
		} else {
			memset(new_info_hash, 0, bytes);
			memset(new_laddrhash, 0, bytes);

			fib_hash_move(new_info_hash, new_laddrhash, new_size);
		}

		if (!fib_hash_size)
			goto failure;
	}

	fi = kmalloc(sizeof(*fi)+nhs*sizeof(struct fib_nh), GFP_KERNEL);
	if (fi == NULL)
		goto failure;
	fib_info_cnt++;
	memset(fi, 0, sizeof(*fi)+nhs*sizeof(struct fib_nh));

	fi->fib_protocol = r->rtm_protocol;

	fi->fib_nhs = nhs;
	change_nexthops(fi) {
		nh->nh_parent = fi;
	} endfor_nexthops(fi)

	fi->fib_flags = r->rtm_flags;
	if (rta->rta_priority)
		fi->fib_priority = *rta->rta_priority;
	if (rta->rta_mx) {
		int attrlen = RTA_PAYLOAD(rta->rta_mx);
		struct rtattr *attr = RTA_DATA(rta->rta_mx);

		while (RTA_OK(attr, attrlen)) {
			unsigned flavor = attr->rta_type;
			if (flavor) {
				if (flavor > RTAX_MAX)
					goto err_inval;
				fi->fib_metrics[flavor-1] = *(unsigned*)RTA_DATA(attr);
			}
			attr = RTA_NEXT(attr, attrlen);
		}
	}
	if (rta->rta_prefsrc)
		memcpy(&fi->fib_prefsrc, rta->rta_prefsrc, 4);

	if (rta->rta_mp) {
#ifdef CONFIG_IP_ROUTE_MULTIPATH
		if ((err = fib_get_nhs(fi, rta->rta_mp, r)) != 0)
			goto failure;
		if (rta->rta_oif && fi->fib_nh->nh_oif != *rta->rta_oif)
			goto err_inval;
		if (rta->rta_gw && memcmp(&fi->fib_nh->nh_gw, rta->rta_gw, 4))
			goto err_inval;
#ifdef CONFIG_NET_CLS_ROUTE
		if (rta->rta_flow && memcmp(&fi->fib_nh->nh_tclassid, rta->rta_flow, 4))
			goto err_inval;
#endif
#else
		goto err_inval;
#endif
	} else {
		struct fib_nh *nh = fi->fib_nh;
		if (rta->rta_oif)
			nh->nh_oif = *rta->rta_oif;
		if (rta->rta_gw)
			memcpy(&nh->nh_gw, rta->rta_gw, 4);
#ifdef CONFIG_NET_CLS_ROUTE
		if (rta->rta_flow)
			memcpy(&nh->nh_tclassid, rta->rta_flow, 4);
#endif
		nh->nh_flags = r->rtm_flags;
#ifdef CONFIG_IP_ROUTE_MULTIPATH
		nh->nh_weight = 1;
#endif
	}

#ifdef CONFIG_IP_ROUTE_MULTIPATH_CACHED
	fi->fib_mp_alg = mp_alg;
#endif

	if (fib_props[r->rtm_type].error) {
		if (rta->rta_gw || rta->rta_oif || rta->rta_mp)
			goto err_inval;
		goto link_it;
	}

	if (r->rtm_scope > RT_SCOPE_HOST)
		goto err_inval;

	if (r->rtm_scope == RT_SCOPE_HOST) {
		struct fib_nh *nh = fi->fib_nh;

		/* Local address is added. */
		if (nhs != 1 || nh->nh_gw)
			goto err_inval;
		nh->nh_scope = RT_SCOPE_NOWHERE;
		nh->nh_dev = dev_get_by_index(fi->fib_nh->nh_oif);
		err = -ENODEV;
		if (nh->nh_dev == NULL)
			goto failure;
	} else {
		change_nexthops(fi) {
			if ((err = fib_check_nh(r, fi, nh)) != 0)
				goto failure;
		} endfor_nexthops(fi)
	}

	if (fi->fib_prefsrc) {
		if (r->rtm_type != RTN_LOCAL || rta->rta_dst == NULL ||
		    memcmp(&fi->fib_prefsrc, rta->rta_dst, 4))
			if (inet_addr_type(fi->fib_prefsrc) != RTN_LOCAL)
				goto err_inval;
	}

link_it:
	if ((ofi = fib_find_info(fi)) != NULL) {
		fi->fib_dead = 1;
		free_fib_info(fi);
		ofi->fib_treeref++;
		return ofi;
	}

	fi->fib_treeref++;
	atomic_inc(&fi->fib_clntref);
	write_lock(&fib_info_lock);
	hlist_add_head(&fi->fib_hash,
		       &fib_info_hash[fib_info_hashfn(fi)]);
	if (fi->fib_prefsrc) {
		struct hlist_head *head;

		head = &fib_info_laddrhash[fib_laddr_hashfn(fi->fib_prefsrc)];
		hlist_add_head(&fi->fib_lhash, head);
	}
	change_nexthops(fi) {
		struct hlist_head *head;
		unsigned int hash;

		if (!nh->nh_dev)
			continue;
		hash = fib_devindex_hashfn(nh->nh_dev->ifindex);
		head = &fib_info_devhash[hash];
		hlist_add_head(&nh->nh_hash, head);
	} endfor_nexthops(fi)
	write_unlock(&fib_info_lock);
	return fi;

err_inval:
	err = -EINVAL;

failure:
        *errp = err;
        if (fi) {
		fi->fib_dead = 1;
		free_fib_info(fi);
	}
	return NULL;
}

/* Note! fib_semantic_match intentionally uses  RCU list functions. */
int fib_semantic_match(struct list_head *head, const struct flowi *flp,
		       struct fib_result *res, __u32 zone, __u32 mask, 
			int prefixlen)
{
	struct fib_alias *fa;
	int nh_sel = 0;

	list_for_each_entry_rcu(fa, head, fa_list) {
		int err;

		if (fa->fa_tos &&
		    fa->fa_tos != flp->fl4_tos)
			continue;

		if (fa->fa_scope < flp->fl4_scope)
			continue;

		fa->fa_state |= FA_S_ACCESSED;

		err = fib_props[fa->fa_type].error;
		if (err == 0) {
			struct fib_info *fi = fa->fa_info;

			if (fi->fib_flags & RTNH_F_DEAD)
				continue;

			switch (fa->fa_type) {
			case RTN_UNICAST:
			case RTN_LOCAL:
			case RTN_BROADCAST:
			case RTN_ANYCAST:
			case RTN_MULTICAST:
				for_nexthops(fi) {
					if (nh->nh_flags&RTNH_F_DEAD)
						continue;
					if (!flp->oif || flp->oif == nh->nh_oif)
						break;
				}
#ifdef CONFIG_IP_ROUTE_MULTIPATH
				if (nhsel < fi->fib_nhs) {
					nh_sel = nhsel;
					goto out_fill_res;
				}
#else
				if (nhsel < 1) {
					goto out_fill_res;
				}
#endif
				endfor_nexthops(fi);
				continue;

			default:
				printk(KERN_DEBUG "impossible 102\n");
				return -EINVAL;
			};
		}
		return err;
	}
	return 1;

out_fill_res:
	res->prefixlen = prefixlen;
	res->nh_sel = nh_sel;
	res->type = fa->fa_type;
	res->scope = fa->fa_scope;
	res->fi = fa->fa_info;
#ifdef CONFIG_IP_ROUTE_MULTIPATH_CACHED
	res->netmask = mask;
	res->network = zone &
		(0xFFFFFFFF >> (32 - prefixlen));
#endif
	atomic_inc(&res->fi->fib_clntref);
	return 0;
}

/* Find appropriate source address to this destination */

u32 __fib_res_prefsrc(struct fib_result *res)
{
	return inet_select_addr(FIB_RES_DEV(*res), FIB_RES_GW(*res), res->scope);
}

int
fib_dump_info(struct sk_buff *skb, u32 pid, u32 seq, int event,
	      u8 tb_id, u8 type, u8 scope, void *dst, int dst_len, u8 tos,
	      struct fib_info *fi, unsigned int flags)
{
	struct rtmsg *rtm;
	struct nlmsghdr  *nlh;
	unsigned char	 *b = skb->tail;

	nlh = NLMSG_NEW(skb, pid, seq, event, sizeof(*rtm), flags);
	rtm = NLMSG_DATA(nlh);
	rtm->rtm_family = AF_INET;
	rtm->rtm_dst_len = dst_len;
	rtm->rtm_src_len = 0;
	rtm->rtm_tos = tos;
	rtm->rtm_table = tb_id;
	rtm->rtm_type = type;
	rtm->rtm_flags = fi->fib_flags;
	rtm->rtm_scope = scope;
	if (rtm->rtm_dst_len)
		RTA_PUT(skb, RTA_DST, 4, dst);
	rtm->rtm_protocol = fi->fib_protocol;
	if (fi->fib_priority)
		RTA_PUT(skb, RTA_PRIORITY, 4, &fi->fib_priority);
#ifdef CONFIG_NET_CLS_ROUTE
	if (fi->fib_nh[0].nh_tclassid)
		RTA_PUT(skb, RTA_FLOW, 4, &fi->fib_nh[0].nh_tclassid);
#endif
	if (rtnetlink_put_metrics(skb, fi->fib_metrics) < 0)
		goto rtattr_failure;
	if (fi->fib_prefsrc)
		RTA_PUT(skb, RTA_PREFSRC, 4, &fi->fib_prefsrc);
	if (fi->fib_nhs == 1) {
		if (fi->fib_nh->nh_gw)
			RTA_PUT(skb, RTA_GATEWAY, 4, &fi->fib_nh->nh_gw);
		if (fi->fib_nh->nh_oif)
			RTA_PUT(skb, RTA_OIF, sizeof(int), &fi->fib_nh->nh_oif);
	}
#ifdef CONFIG_IP_ROUTE_MULTIPATH
	if (fi->fib_nhs > 1) {
		struct rtnexthop *nhp;
		struct rtattr *mp_head;
		if (skb_tailroom(skb) <= RTA_SPACE(0))
			goto rtattr_failure;
		mp_head = (struct rtattr*)skb_put(skb, RTA_SPACE(0));

		for_nexthops(fi) {
			if (skb_tailroom(skb) < RTA_ALIGN(RTA_ALIGN(sizeof(*nhp)) + 4))
				goto rtattr_failure;
			nhp = (struct rtnexthop*)skb_put(skb, RTA_ALIGN(sizeof(*nhp)));
			nhp->rtnh_flags = nh->nh_flags & 0xFF;
			nhp->rtnh_hops = nh->nh_weight-1;
			nhp->rtnh_ifindex = nh->nh_oif;
			if (nh->nh_gw)
				RTA_PUT(skb, RTA_GATEWAY, 4, &nh->nh_gw);
			nhp->rtnh_len = skb->tail - (unsigned char*)nhp;
		} endfor_nexthops(fi);
		mp_head->rta_type = RTA_MULTIPATH;
		mp_head->rta_len = skb->tail - (u8*)mp_head;
	}
#endif
	nlh->nlmsg_len = skb->tail - b;
	return skb->len;

nlmsg_failure:
rtattr_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}

#ifndef CONFIG_IP_NOSIOCRT

int
fib_convert_rtentry(int cmd, struct nlmsghdr *nl, struct rtmsg *rtm,
		    struct kern_rta *rta, struct rtentry *r)
{
	int    plen;
	u32    *ptr;

	memset(rtm, 0, sizeof(*rtm));
	memset(rta, 0, sizeof(*rta));

	if (r->rt_dst.sa_family != AF_INET)
		return -EAFNOSUPPORT;

	/* Check mask for validity:
	   a) it must be contiguous.
	   b) destination must have all host bits clear.
	   c) if application forgot to set correct family (AF_INET),
	      reject request unless it is absolutely clear i.e.
	      both family and mask are zero.
	 */
	plen = 32;
	ptr = &((struct sockaddr_in*)&r->rt_dst)->sin_addr.s_addr;
	if (!(r->rt_flags&RTF_HOST)) {
		u32 mask = ((struct sockaddr_in*)&r->rt_genmask)->sin_addr.s_addr;
		if (r->rt_genmask.sa_family != AF_INET) {
			if (mask || r->rt_genmask.sa_family)
				return -EAFNOSUPPORT;
		}
		if (bad_mask(mask, *ptr))
			return -EINVAL;
		plen = inet_mask_len(mask);
	}

	nl->nlmsg_flags = NLM_F_REQUEST;
	nl->nlmsg_pid = current->pid;
	nl->nlmsg_seq = 0;
	nl->nlmsg_len = NLMSG_LENGTH(sizeof(*rtm));
	if (cmd == SIOCDELRT) {
		nl->nlmsg_type = RTM_DELROUTE;
		nl->nlmsg_flags = 0;
	} else {
		nl->nlmsg_type = RTM_NEWROUTE;
		nl->nlmsg_flags = NLM_F_REQUEST|NLM_F_CREATE;
		rtm->rtm_protocol = RTPROT_BOOT;
	}

	rtm->rtm_dst_len = plen;
	rta->rta_dst = ptr;

	if (r->rt_metric) {
		*(u32*)&r->rt_pad3 = r->rt_metric - 1;
		rta->rta_priority = (u32*)&r->rt_pad3;
	}
	if (r->rt_flags&RTF_REJECT) {
		rtm->rtm_scope = RT_SCOPE_HOST;
		rtm->rtm_type = RTN_UNREACHABLE;
		return 0;
	}
	rtm->rtm_scope = RT_SCOPE_NOWHERE;
	rtm->rtm_type = RTN_UNICAST;

	if (r->rt_dev) {
		char *colon;
		struct net_device *dev;
		char   devname[IFNAMSIZ];

		if (copy_from_user(devname, r->rt_dev, IFNAMSIZ-1))
			return -EFAULT;
		devname[IFNAMSIZ-1] = 0;
		colon = strchr(devname, ':');
		if (colon)
			*colon = 0;
		dev = __dev_get_by_name(devname);
		if (!dev)
			return -ENODEV;
		rta->rta_oif = &dev->ifindex;
		if (colon) {
			struct in_ifaddr *ifa;
			struct in_device *in_dev = __in_dev_get_rtnl(dev);
			if (!in_dev)
				return -ENODEV;
			*colon = ':';
			for (ifa = in_dev->ifa_list; ifa; ifa = ifa->ifa_next)
				if (strcmp(ifa->ifa_label, devname) == 0)
					break;
			if (ifa == NULL)
				return -ENODEV;
			rta->rta_prefsrc = &ifa->ifa_local;
		}
	}

	ptr = &((struct sockaddr_in*)&r->rt_gateway)->sin_addr.s_addr;
	if (r->rt_gateway.sa_family == AF_INET && *ptr) {
		rta->rta_gw = ptr;
		if (r->rt_flags&RTF_GATEWAY && inet_addr_type(*ptr) == RTN_UNICAST)
			rtm->rtm_scope = RT_SCOPE_UNIVERSE;
	}

	if (cmd == SIOCDELRT)
		return 0;

	if (r->rt_flags&RTF_GATEWAY && rta->rta_gw == NULL)
		return -EINVAL;

	if (rtm->rtm_scope == RT_SCOPE_NOWHERE)
		rtm->rtm_scope = RT_SCOPE_LINK;

	if (r->rt_flags&(RTF_MTU|RTF_WINDOW|RTF_IRTT)) {
		struct rtattr *rec;
		struct rtattr *mx = kmalloc(RTA_LENGTH(3*RTA_LENGTH(4)), GFP_KERNEL);
		if (mx == NULL)
			return -ENOMEM;
		rta->rta_mx = mx;
		mx->rta_type = RTA_METRICS;
		mx->rta_len  = RTA_LENGTH(0);
		if (r->rt_flags&RTF_MTU) {
			rec = (void*)((char*)mx + RTA_ALIGN(mx->rta_len));
			rec->rta_type = RTAX_ADVMSS;
			rec->rta_len = RTA_LENGTH(4);
			mx->rta_len += RTA_LENGTH(4);
			*(u32*)RTA_DATA(rec) = r->rt_mtu - 40;
		}
		if (r->rt_flags&RTF_WINDOW) {
			rec = (void*)((char*)mx + RTA_ALIGN(mx->rta_len));
			rec->rta_type = RTAX_WINDOW;
			rec->rta_len = RTA_LENGTH(4);
			mx->rta_len += RTA_LENGTH(4);
			*(u32*)RTA_DATA(rec) = r->rt_window;
		}
		if (r->rt_flags&RTF_IRTT) {
			rec = (void*)((char*)mx + RTA_ALIGN(mx->rta_len));
			rec->rta_type = RTAX_RTT;
			rec->rta_len = RTA_LENGTH(4);
			mx->rta_len += RTA_LENGTH(4);
			*(u32*)RTA_DATA(rec) = r->rt_irtt<<3;
		}
	}
	return 0;
}

#endif

/*
   Update FIB if:
   - local address disappeared -> we must delete all the entries
     referring to it.
   - device went down -> we must shutdown all nexthops going via it.
 */

int fib_sync_down(u32 local, struct net_device *dev, int force)
{
	int ret = 0;
	int scope = RT_SCOPE_NOWHERE;
	
	if (force)
		scope = -1;

	if (local && fib_info_laddrhash) {
		unsigned int hash = fib_laddr_hashfn(local);
		struct hlist_head *head = &fib_info_laddrhash[hash];
		struct hlist_node *node;
		struct fib_info *fi;

		hlist_for_each_entry(fi, node, head, fib_lhash) {
			if (fi->fib_prefsrc == local) {
				fi->fib_flags |= RTNH_F_DEAD;
				ret++;
			}
		}
	}

	if (dev) {
		struct fib_info *prev_fi = NULL;
		unsigned int hash = fib_devindex_hashfn(dev->ifindex);
		struct hlist_head *head = &fib_info_devhash[hash];
		struct hlist_node *node;
		struct fib_nh *nh;

		hlist_for_each_entry(nh, node, head, nh_hash) {
			struct fib_info *fi = nh->nh_parent;
			int dead;

			BUG_ON(!fi->fib_nhs);
			if (nh->nh_dev != dev || fi == prev_fi)
				continue;
			prev_fi = fi;
			dead = 0;
			change_nexthops(fi) {
				if (nh->nh_flags&RTNH_F_DEAD)
					dead++;
				else if (nh->nh_dev == dev &&
					 nh->nh_scope != scope) {
					nh->nh_flags |= RTNH_F_DEAD;
#ifdef CONFIG_IP_ROUTE_MULTIPATH
					spin_lock_bh(&fib_multipath_lock);
					fi->fib_power -= nh->nh_power;
					nh->nh_power = 0;
					spin_unlock_bh(&fib_multipath_lock);
#endif
					dead++;
				}
#ifdef CONFIG_IP_ROUTE_MULTIPATH
				if (force > 1 && nh->nh_dev == dev) {
					dead = fi->fib_nhs;
					break;
				}
#endif
			} endfor_nexthops(fi)
			if (dead == fi->fib_nhs) {
				fi->fib_flags |= RTNH_F_DEAD;
				ret++;
			}
		}
	}

	return ret;
}

#ifdef CONFIG_IP_ROUTE_MULTIPATH

/*
   Dead device goes up. We wake up dead nexthops.
   It takes sense only on multipath routes.
 */

int fib_sync_up(struct net_device *dev)
{
	struct fib_info *prev_fi;
	unsigned int hash;
	struct hlist_head *head;
	struct hlist_node *node;
	struct fib_nh *nh;
	int ret;

	if (!(dev->flags&IFF_UP))
		return 0;

	prev_fi = NULL;
	hash = fib_devindex_hashfn(dev->ifindex);
	head = &fib_info_devhash[hash];
	ret = 0;

	hlist_for_each_entry(nh, node, head, nh_hash) {
		struct fib_info *fi = nh->nh_parent;
		int alive;

		BUG_ON(!fi->fib_nhs);
		if (nh->nh_dev != dev || fi == prev_fi)
			continue;

		prev_fi = fi;
		alive = 0;
		change_nexthops(fi) {
			if (!(nh->nh_flags&RTNH_F_DEAD)) {
				alive++;
				continue;
			}
			if (nh->nh_dev == NULL || !(nh->nh_dev->flags&IFF_UP))
				continue;
			if (nh->nh_dev != dev || !__in_dev_get_rtnl(dev))
				continue;
			alive++;
			spin_lock_bh(&fib_multipath_lock);
			nh->nh_power = 0;
			nh->nh_flags &= ~RTNH_F_DEAD;
			spin_unlock_bh(&fib_multipath_lock);
		} endfor_nexthops(fi)

		if (alive > 0) {
			fi->fib_flags &= ~RTNH_F_DEAD;
			ret++;
		}
	}

	return ret;
}

/*
   The algorithm is suboptimal, but it provides really
   fair weighted route distribution.
 */

void fib_select_multipath(const struct flowi *flp, struct fib_result *res)
{
	struct fib_info *fi = res->fi;
	int w;

	spin_lock_bh(&fib_multipath_lock);
	if (fi->fib_power <= 0) {
		int power = 0;
		change_nexthops(fi) {
			if (!(nh->nh_flags&RTNH_F_DEAD)) {
				power += nh->nh_weight;
				nh->nh_power = nh->nh_weight;
			}
		} endfor_nexthops(fi);
		fi->fib_power = power;
		if (power <= 0) {
			spin_unlock_bh(&fib_multipath_lock);
			/* Race condition: route has just become dead. */
			res->nh_sel = 0;
			return;
		}
	}


	/* w should be random number [0..fi->fib_power-1],
	   it is pretty bad approximation.
	 */

	w = jiffies % fi->fib_power;

	change_nexthops(fi) {
		if (!(nh->nh_flags&RTNH_F_DEAD) && nh->nh_power) {
			if ((w -= nh->nh_power) <= 0) {
				nh->nh_power--;
				fi->fib_power--;
				res->nh_sel = nhsel;
				spin_unlock_bh(&fib_multipath_lock);
				return;
			}
		}
	} endfor_nexthops(fi);

	/* Race condition: route has just become dead. */
	res->nh_sel = 0;
	spin_unlock_bh(&fib_multipath_lock);
}
#endif
