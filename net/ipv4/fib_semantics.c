/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		IPv4 Forwarding Information Base: semantics.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <linux/uaccess.h>
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
#include <linux/inetdevice.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/proc_fs.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/netlink.h>

#include <net/arp.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/route.h>
#include <net/tcp.h>
#include <net/sock.h>
#include <net/ip_fib.h>
#include <net/netlink.h>
#include <net/nexthop.h>
#include <net/lwtunnel.h>
#include <net/fib_notifier.h>

#include "fib_lookup.h"

static DEFINE_SPINLOCK(fib_info_lock);
static struct hlist_head *fib_info_hash;
static struct hlist_head *fib_info_laddrhash;
static unsigned int fib_info_hash_size;
static unsigned int fib_info_cnt;

#define DEVINDEX_HASHBITS 8
#define DEVINDEX_HASHSIZE (1U << DEVINDEX_HASHBITS)
static struct hlist_head fib_info_devhash[DEVINDEX_HASHSIZE];

#ifdef CONFIG_IP_ROUTE_MULTIPATH

#define for_nexthops(fi) {						\
	int nhsel; const struct fib_nh *nh;				\
	for (nhsel = 0, nh = (fi)->fib_nh;				\
	     nhsel < (fi)->fib_nhs;					\
	     nh++, nhsel++)

#define change_nexthops(fi) {						\
	int nhsel; struct fib_nh *nexthop_nh;				\
	for (nhsel = 0,	nexthop_nh = (struct fib_nh *)((fi)->fib_nh);	\
	     nhsel < (fi)->fib_nhs;					\
	     nexthop_nh++, nhsel++)

#else /* CONFIG_IP_ROUTE_MULTIPATH */

/* Hope, that gcc will optimize it to get rid of dummy loop */

#define for_nexthops(fi) {						\
	int nhsel; const struct fib_nh *nh = (fi)->fib_nh;		\
	for (nhsel = 0; nhsel < 1; nhsel++)

#define change_nexthops(fi) {						\
	int nhsel;							\
	struct fib_nh *nexthop_nh = (struct fib_nh *)((fi)->fib_nh);	\
	for (nhsel = 0; nhsel < 1; nhsel++)

#endif /* CONFIG_IP_ROUTE_MULTIPATH */

#define endfor_nexthops(fi) }


const struct fib_prop fib_props[RTN_MAX + 1] = {
	[RTN_UNSPEC] = {
		.error	= 0,
		.scope	= RT_SCOPE_NOWHERE,
	},
	[RTN_UNICAST] = {
		.error	= 0,
		.scope	= RT_SCOPE_UNIVERSE,
	},
	[RTN_LOCAL] = {
		.error	= 0,
		.scope	= RT_SCOPE_HOST,
	},
	[RTN_BROADCAST] = {
		.error	= 0,
		.scope	= RT_SCOPE_LINK,
	},
	[RTN_ANYCAST] = {
		.error	= 0,
		.scope	= RT_SCOPE_LINK,
	},
	[RTN_MULTICAST] = {
		.error	= 0,
		.scope	= RT_SCOPE_UNIVERSE,
	},
	[RTN_BLACKHOLE] = {
		.error	= -EINVAL,
		.scope	= RT_SCOPE_UNIVERSE,
	},
	[RTN_UNREACHABLE] = {
		.error	= -EHOSTUNREACH,
		.scope	= RT_SCOPE_UNIVERSE,
	},
	[RTN_PROHIBIT] = {
		.error	= -EACCES,
		.scope	= RT_SCOPE_UNIVERSE,
	},
	[RTN_THROW] = {
		.error	= -EAGAIN,
		.scope	= RT_SCOPE_UNIVERSE,
	},
	[RTN_NAT] = {
		.error	= -EINVAL,
		.scope	= RT_SCOPE_NOWHERE,
	},
	[RTN_XRESOLVE] = {
		.error	= -EINVAL,
		.scope	= RT_SCOPE_NOWHERE,
	},
};

static void rt_fibinfo_free(struct rtable __rcu **rtp)
{
	struct rtable *rt = rcu_dereference_protected(*rtp, 1);

	if (!rt)
		return;

	/* Not even needed : RCU_INIT_POINTER(*rtp, NULL);
	 * because we waited an RCU grace period before calling
	 * free_fib_info_rcu()
	 */

	dst_dev_put(&rt->dst);
	dst_release_immediate(&rt->dst);
}

static void free_nh_exceptions(struct fib_nh *nh)
{
	struct fnhe_hash_bucket *hash;
	int i;

	hash = rcu_dereference_protected(nh->nh_exceptions, 1);
	if (!hash)
		return;
	for (i = 0; i < FNHE_HASH_SIZE; i++) {
		struct fib_nh_exception *fnhe;

		fnhe = rcu_dereference_protected(hash[i].chain, 1);
		while (fnhe) {
			struct fib_nh_exception *next;

			next = rcu_dereference_protected(fnhe->fnhe_next, 1);

			rt_fibinfo_free(&fnhe->fnhe_rth_input);
			rt_fibinfo_free(&fnhe->fnhe_rth_output);

			kfree(fnhe);

			fnhe = next;
		}
	}
	kfree(hash);
}

static void rt_fibinfo_free_cpus(struct rtable __rcu * __percpu *rtp)
{
	int cpu;

	if (!rtp)
		return;

	for_each_possible_cpu(cpu) {
		struct rtable *rt;

		rt = rcu_dereference_protected(*per_cpu_ptr(rtp, cpu), 1);
		if (rt) {
			dst_dev_put(&rt->dst);
			dst_release_immediate(&rt->dst);
		}
	}
	free_percpu(rtp);
}

/* Release a nexthop info record */
static void free_fib_info_rcu(struct rcu_head *head)
{
	struct fib_info *fi = container_of(head, struct fib_info, rcu);

	change_nexthops(fi) {
		if (nexthop_nh->nh_dev)
			dev_put(nexthop_nh->nh_dev);
		lwtstate_put(nexthop_nh->nh_lwtstate);
		free_nh_exceptions(nexthop_nh);
		rt_fibinfo_free_cpus(nexthop_nh->nh_pcpu_rth_output);
		rt_fibinfo_free(&nexthop_nh->nh_rth_input);
	} endfor_nexthops(fi);

	ip_fib_metrics_put(fi->fib_metrics);

	kfree(fi);
}

void free_fib_info(struct fib_info *fi)
{
	if (fi->fib_dead == 0) {
		pr_warn("Freeing alive fib_info %p\n", fi);
		return;
	}
	fib_info_cnt--;
#ifdef CONFIG_IP_ROUTE_CLASSID
	change_nexthops(fi) {
		if (nexthop_nh->nh_tclassid)
			fi->fib_net->ipv4.fib_num_tclassid_users--;
	} endfor_nexthops(fi);
#endif
	call_rcu(&fi->rcu, free_fib_info_rcu);
}
EXPORT_SYMBOL_GPL(free_fib_info);

void fib_release_info(struct fib_info *fi)
{
	spin_lock_bh(&fib_info_lock);
	if (fi && --fi->fib_treeref == 0) {
		hlist_del(&fi->fib_hash);
		if (fi->fib_prefsrc)
			hlist_del(&fi->fib_lhash);
		change_nexthops(fi) {
			if (!nexthop_nh->nh_dev)
				continue;
			hlist_del(&nexthop_nh->nh_hash);
		} endfor_nexthops(fi)
		fi->fib_dead = 1;
		fib_info_put(fi);
	}
	spin_unlock_bh(&fib_info_lock);
}

static inline int nh_comp(const struct fib_info *fi, const struct fib_info *ofi)
{
	const struct fib_nh *onh = ofi->fib_nh;

	for_nexthops(fi) {
		if (nh->nh_oif != onh->nh_oif ||
		    nh->nh_gw  != onh->nh_gw ||
		    nh->nh_scope != onh->nh_scope ||
#ifdef CONFIG_IP_ROUTE_MULTIPATH
		    nh->nh_weight != onh->nh_weight ||
#endif
#ifdef CONFIG_IP_ROUTE_CLASSID
		    nh->nh_tclassid != onh->nh_tclassid ||
#endif
		    lwtunnel_cmp_encap(nh->nh_lwtstate, onh->nh_lwtstate) ||
		    ((nh->nh_flags ^ onh->nh_flags) & ~RTNH_COMPARE_MASK))
			return -1;
		onh++;
	} endfor_nexthops(fi);
	return 0;
}

static inline unsigned int fib_devindex_hashfn(unsigned int val)
{
	unsigned int mask = DEVINDEX_HASHSIZE - 1;

	return (val ^
		(val >> DEVINDEX_HASHBITS) ^
		(val >> (DEVINDEX_HASHBITS * 2))) & mask;
}

static inline unsigned int fib_info_hashfn(const struct fib_info *fi)
{
	unsigned int mask = (fib_info_hash_size - 1);
	unsigned int val = fi->fib_nhs;

	val ^= (fi->fib_protocol << 8) | fi->fib_scope;
	val ^= (__force u32)fi->fib_prefsrc;
	val ^= fi->fib_priority;
	for_nexthops(fi) {
		val ^= fib_devindex_hashfn(nh->nh_oif);
	} endfor_nexthops(fi)

	return (val ^ (val >> 7) ^ (val >> 12)) & mask;
}

static struct fib_info *fib_find_info(const struct fib_info *nfi)
{
	struct hlist_head *head;
	struct fib_info *fi;
	unsigned int hash;

	hash = fib_info_hashfn(nfi);
	head = &fib_info_hash[hash];

	hlist_for_each_entry(fi, head, fib_hash) {
		if (!net_eq(fi->fib_net, nfi->fib_net))
			continue;
		if (fi->fib_nhs != nfi->fib_nhs)
			continue;
		if (nfi->fib_protocol == fi->fib_protocol &&
		    nfi->fib_scope == fi->fib_scope &&
		    nfi->fib_prefsrc == fi->fib_prefsrc &&
		    nfi->fib_priority == fi->fib_priority &&
		    nfi->fib_type == fi->fib_type &&
		    memcmp(nfi->fib_metrics, fi->fib_metrics,
			   sizeof(u32) * RTAX_MAX) == 0 &&
		    !((nfi->fib_flags ^ fi->fib_flags) & ~RTNH_COMPARE_MASK) &&
		    (nfi->fib_nhs == 0 || nh_comp(fi, nfi) == 0))
			return fi;
	}

	return NULL;
}

/* Check, that the gateway is already configured.
 * Used only by redirect accept routine.
 */
int ip_fib_check_default(__be32 gw, struct net_device *dev)
{
	struct hlist_head *head;
	struct fib_nh *nh;
	unsigned int hash;

	spin_lock(&fib_info_lock);

	hash = fib_devindex_hashfn(dev->ifindex);
	head = &fib_info_devhash[hash];
	hlist_for_each_entry(nh, head, nh_hash) {
		if (nh->nh_dev == dev &&
		    nh->nh_gw == gw &&
		    !(nh->nh_flags & RTNH_F_DEAD)) {
			spin_unlock(&fib_info_lock);
			return 0;
		}
	}

	spin_unlock(&fib_info_lock);

	return -1;
}

static inline size_t fib_nlmsg_size(struct fib_info *fi)
{
	size_t payload = NLMSG_ALIGN(sizeof(struct rtmsg))
			 + nla_total_size(4) /* RTA_TABLE */
			 + nla_total_size(4) /* RTA_DST */
			 + nla_total_size(4) /* RTA_PRIORITY */
			 + nla_total_size(4) /* RTA_PREFSRC */
			 + nla_total_size(TCP_CA_NAME_MAX); /* RTAX_CC_ALGO */

	/* space for nested metrics */
	payload += nla_total_size((RTAX_MAX * nla_total_size(4)));

	if (fi->fib_nhs) {
		size_t nh_encapsize = 0;
		/* Also handles the special case fib_nhs == 1 */

		/* each nexthop is packed in an attribute */
		size_t nhsize = nla_total_size(sizeof(struct rtnexthop));

		/* may contain flow and gateway attribute */
		nhsize += 2 * nla_total_size(4);

		/* grab encap info */
		for_nexthops(fi) {
			if (nh->nh_lwtstate) {
				/* RTA_ENCAP_TYPE */
				nh_encapsize += lwtunnel_get_encap_size(
						nh->nh_lwtstate);
				/* RTA_ENCAP */
				nh_encapsize +=  nla_total_size(2);
			}
		} endfor_nexthops(fi);

		/* all nexthops are packed in a nested attribute */
		payload += nla_total_size((fi->fib_nhs * nhsize) +
					  nh_encapsize);

	}

	return payload;
}

void rtmsg_fib(int event, __be32 key, struct fib_alias *fa,
	       int dst_len, u32 tb_id, const struct nl_info *info,
	       unsigned int nlm_flags)
{
	struct sk_buff *skb;
	u32 seq = info->nlh ? info->nlh->nlmsg_seq : 0;
	int err = -ENOBUFS;

	skb = nlmsg_new(fib_nlmsg_size(fa->fa_info), GFP_KERNEL);
	if (!skb)
		goto errout;

	err = fib_dump_info(skb, info->portid, seq, event, tb_id,
			    fa->fa_type, key, dst_len,
			    fa->fa_tos, fa->fa_info, nlm_flags);
	if (err < 0) {
		/* -EMSGSIZE implies BUG in fib_nlmsg_size() */
		WARN_ON(err == -EMSGSIZE);
		kfree_skb(skb);
		goto errout;
	}
	rtnl_notify(skb, info->nl_net, info->portid, RTNLGRP_IPV4_ROUTE,
		    info->nlh, GFP_KERNEL);
	return;
errout:
	if (err < 0)
		rtnl_set_sk_err(info->nl_net, RTNLGRP_IPV4_ROUTE, err);
}

static int fib_detect_death(struct fib_info *fi, int order,
			    struct fib_info **last_resort, int *last_idx,
			    int dflt)
{
	struct neighbour *n;
	int state = NUD_NONE;

	n = neigh_lookup(&arp_tbl, &fi->fib_nh[0].nh_gw, fi->fib_dev);
	if (n) {
		state = n->nud_state;
		neigh_release(n);
	} else {
		return 0;
	}
	if (state == NUD_REACHABLE)
		return 0;
	if ((state & NUD_VALID) && order != dflt)
		return 0;
	if ((state & NUD_VALID) ||
	    (*last_idx < 0 && order > dflt && state != NUD_INCOMPLETE)) {
		*last_resort = fi;
		*last_idx = order;
	}
	return 1;
}

int fib_nh_init(struct net *net, struct fib_nh *nh,
		struct fib_config *cfg, int nh_weight,
		struct netlink_ext_ack *extack)
{
	int err = -ENOMEM;

	nh->nh_pcpu_rth_output = alloc_percpu(struct rtable __rcu *);
	if (!nh->nh_pcpu_rth_output)
		goto err_out;

	if (cfg->fc_encap) {
		struct lwtunnel_state *lwtstate;

		err = -EINVAL;
		if (cfg->fc_encap_type == LWTUNNEL_ENCAP_NONE) {
			NL_SET_ERR_MSG(extack, "LWT encap type not specified");
			goto lwt_failure;
		}
		err = lwtunnel_build_state(cfg->fc_encap_type,
					   cfg->fc_encap, AF_INET, cfg,
					   &lwtstate, extack);
		if (err)
			goto lwt_failure;

		nh->nh_lwtstate = lwtstate_get(lwtstate);
	}

	nh->nh_oif = cfg->fc_oif;
	nh->nh_gw = cfg->fc_gw;
	nh->nh_flags = cfg->fc_flags;

#ifdef CONFIG_IP_ROUTE_CLASSID
	nh->nh_tclassid = cfg->fc_flow;
	if (nh->nh_tclassid)
		net->ipv4.fib_num_tclassid_users++;
#endif
#ifdef CONFIG_IP_ROUTE_MULTIPATH
	nh->nh_weight = nh_weight;
#endif
	return 0;

lwt_failure:
	rt_fibinfo_free_cpus(nh->nh_pcpu_rth_output);
	nh->nh_pcpu_rth_output = NULL;
err_out:
	return err;
}

#ifdef CONFIG_IP_ROUTE_MULTIPATH

static int fib_count_nexthops(struct rtnexthop *rtnh, int remaining,
			      struct netlink_ext_ack *extack)
{
	int nhs = 0;

	while (rtnh_ok(rtnh, remaining)) {
		nhs++;
		rtnh = rtnh_next(rtnh, &remaining);
	}

	/* leftover implies invalid nexthop configuration, discard it */
	if (remaining > 0) {
		NL_SET_ERR_MSG(extack,
			       "Invalid nexthop configuration - extra data after nexthops");
		nhs = 0;
	}

	return nhs;
}

static int fib_get_nhs(struct fib_info *fi, struct rtnexthop *rtnh,
		       int remaining, struct fib_config *cfg,
		       struct netlink_ext_ack *extack)
{
	struct net *net = fi->fib_net;
	struct fib_config fib_cfg;
	int ret;

	change_nexthops(fi) {
		int attrlen;

		memset(&fib_cfg, 0, sizeof(fib_cfg));

		if (!rtnh_ok(rtnh, remaining)) {
			NL_SET_ERR_MSG(extack,
				       "Invalid nexthop configuration - extra data after nexthop");
			return -EINVAL;
		}

		if (rtnh->rtnh_flags & (RTNH_F_DEAD | RTNH_F_LINKDOWN)) {
			NL_SET_ERR_MSG(extack,
				       "Invalid flags for nexthop - can not contain DEAD or LINKDOWN");
			return -EINVAL;
		}

		fib_cfg.fc_flags = (cfg->fc_flags & ~0xFF) | rtnh->rtnh_flags;
		fib_cfg.fc_oif = rtnh->rtnh_ifindex;

		attrlen = rtnh_attrlen(rtnh);
		if (attrlen > 0) {
			struct nlattr *nla, *attrs = rtnh_attrs(rtnh);

			nla = nla_find(attrs, attrlen, RTA_GATEWAY);
			if (nla)
				fib_cfg.fc_gw = nla_get_in_addr(nla);

			nla = nla_find(attrs, attrlen, RTA_FLOW);
			if (nla)
				fib_cfg.fc_flow = nla_get_u32(nla);

			fib_cfg.fc_encap = nla_find(attrs, attrlen, RTA_ENCAP);
			nla = nla_find(attrs, attrlen, RTA_ENCAP_TYPE);
			if (nla)
				fib_cfg.fc_encap_type = nla_get_u16(nla);
		}

		ret = fib_nh_init(net, nexthop_nh, &fib_cfg,
				  rtnh->rtnh_hops + 1, extack);
		if (ret)
			goto errout;

		rtnh = rtnh_next(rtnh, &remaining);
	} endfor_nexthops(fi);

	ret = -EINVAL;
	if (cfg->fc_oif && fi->fib_nh->nh_oif != cfg->fc_oif) {
		NL_SET_ERR_MSG(extack,
			       "Nexthop device index does not match RTA_OIF");
		goto errout;
	}
	if (cfg->fc_gw && fi->fib_nh->nh_gw != cfg->fc_gw) {
		NL_SET_ERR_MSG(extack,
			       "Nexthop gateway does not match RTA_GATEWAY");
		goto errout;
	}
#ifdef CONFIG_IP_ROUTE_CLASSID
	if (cfg->fc_flow && fi->fib_nh->nh_tclassid != cfg->fc_flow) {
		NL_SET_ERR_MSG(extack,
			       "Nexthop class id does not match RTA_FLOW");
		goto errout;
	}
#endif
	ret = 0;
errout:
	return ret;
}

static void fib_rebalance(struct fib_info *fi)
{
	int total;
	int w;

	if (fi->fib_nhs < 2)
		return;

	total = 0;
	for_nexthops(fi) {
		if (nh->nh_flags & RTNH_F_DEAD)
			continue;

		if (ip_ignore_linkdown(nh->nh_dev) &&
		    nh->nh_flags & RTNH_F_LINKDOWN)
			continue;

		total += nh->nh_weight;
	} endfor_nexthops(fi);

	w = 0;
	change_nexthops(fi) {
		int upper_bound;

		if (nexthop_nh->nh_flags & RTNH_F_DEAD) {
			upper_bound = -1;
		} else if (ip_ignore_linkdown(nexthop_nh->nh_dev) &&
			   nexthop_nh->nh_flags & RTNH_F_LINKDOWN) {
			upper_bound = -1;
		} else {
			w += nexthop_nh->nh_weight;
			upper_bound = DIV_ROUND_CLOSEST_ULL((u64)w << 31,
							    total) - 1;
		}

		atomic_set(&nexthop_nh->nh_upper_bound, upper_bound);
	} endfor_nexthops(fi);
}
#else /* CONFIG_IP_ROUTE_MULTIPATH */

static int fib_get_nhs(struct fib_info *fi, struct rtnexthop *rtnh,
		       int remaining, struct fib_config *cfg,
		       struct netlink_ext_ack *extack)
{
	NL_SET_ERR_MSG(extack, "Multipath support not enabled in kernel");

	return -EINVAL;
}

#define fib_rebalance(fi) do { } while (0)

#endif /* CONFIG_IP_ROUTE_MULTIPATH */

static int fib_encap_match(u16 encap_type,
			   struct nlattr *encap,
			   const struct fib_nh *nh,
			   const struct fib_config *cfg,
			   struct netlink_ext_ack *extack)
{
	struct lwtunnel_state *lwtstate;
	int ret, result = 0;

	if (encap_type == LWTUNNEL_ENCAP_NONE)
		return 0;

	ret = lwtunnel_build_state(encap_type, encap, AF_INET,
				   cfg, &lwtstate, extack);
	if (!ret) {
		result = lwtunnel_cmp_encap(lwtstate, nh->nh_lwtstate);
		lwtstate_free(lwtstate);
	}

	return result;
}

int fib_nh_match(struct fib_config *cfg, struct fib_info *fi,
		 struct netlink_ext_ack *extack)
{
#ifdef CONFIG_IP_ROUTE_MULTIPATH
	struct rtnexthop *rtnh;
	int remaining;
#endif

	if (cfg->fc_priority && cfg->fc_priority != fi->fib_priority)
		return 1;

	if (cfg->fc_oif || cfg->fc_gw) {
		if (cfg->fc_encap) {
			if (fib_encap_match(cfg->fc_encap_type, cfg->fc_encap,
					    fi->fib_nh, cfg, extack))
				return 1;
		}
#ifdef CONFIG_IP_ROUTE_CLASSID
		if (cfg->fc_flow &&
		    cfg->fc_flow != fi->fib_nh->nh_tclassid)
			return 1;
#endif
		if ((!cfg->fc_oif || cfg->fc_oif == fi->fib_nh->nh_oif) &&
		    (!cfg->fc_gw  || cfg->fc_gw == fi->fib_nh->nh_gw))
			return 0;
		return 1;
	}

#ifdef CONFIG_IP_ROUTE_MULTIPATH
	if (!cfg->fc_mp)
		return 0;

	rtnh = cfg->fc_mp;
	remaining = cfg->fc_mp_len;

	for_nexthops(fi) {
		int attrlen;

		if (!rtnh_ok(rtnh, remaining))
			return -EINVAL;

		if (rtnh->rtnh_ifindex && rtnh->rtnh_ifindex != nh->nh_oif)
			return 1;

		attrlen = rtnh_attrlen(rtnh);
		if (attrlen > 0) {
			struct nlattr *nla, *attrs = rtnh_attrs(rtnh);

			nla = nla_find(attrs, attrlen, RTA_GATEWAY);
			if (nla && nla_get_in_addr(nla) != nh->nh_gw)
				return 1;
#ifdef CONFIG_IP_ROUTE_CLASSID
			nla = nla_find(attrs, attrlen, RTA_FLOW);
			if (nla && nla_get_u32(nla) != nh->nh_tclassid)
				return 1;
#endif
		}

		rtnh = rtnh_next(rtnh, &remaining);
	} endfor_nexthops(fi);
#endif
	return 0;
}

bool fib_metrics_match(struct fib_config *cfg, struct fib_info *fi)
{
	struct nlattr *nla;
	int remaining;

	if (!cfg->fc_mx)
		return true;

	nla_for_each_attr(nla, cfg->fc_mx, cfg->fc_mx_len, remaining) {
		int type = nla_type(nla);
		u32 fi_val, val;

		if (!type)
			continue;
		if (type > RTAX_MAX)
			return false;

		if (type == RTAX_CC_ALGO) {
			char tmp[TCP_CA_NAME_MAX];
			bool ecn_ca = false;

			nla_strlcpy(tmp, nla, sizeof(tmp));
			val = tcp_ca_get_key_by_name(fi->fib_net, tmp, &ecn_ca);
		} else {
			if (nla_len(nla) != sizeof(u32))
				return false;
			val = nla_get_u32(nla);
		}

		fi_val = fi->fib_metrics->metrics[type - 1];
		if (type == RTAX_FEATURES)
			fi_val &= ~DST_FEATURE_ECN_CA;

		if (fi_val != val)
			return false;
	}

	return true;
}


/*
 * Picture
 * -------
 *
 * Semantics of nexthop is very messy by historical reasons.
 * We have to take into account, that:
 * a) gateway can be actually local interface address,
 *    so that gatewayed route is direct.
 * b) gateway must be on-link address, possibly
 *    described not by an ifaddr, but also by a direct route.
 * c) If both gateway and interface are specified, they should not
 *    contradict.
 * d) If we use tunnel routes, gateway could be not on-link.
 *
 * Attempt to reconcile all of these (alas, self-contradictory) conditions
 * results in pretty ugly and hairy code with obscure logic.
 *
 * I chose to generalized it instead, so that the size
 * of code does not increase practically, but it becomes
 * much more general.
 * Every prefix is assigned a "scope" value: "host" is local address,
 * "link" is direct route,
 * [ ... "site" ... "interior" ... ]
 * and "universe" is true gateway route with global meaning.
 *
 * Every prefix refers to a set of "nexthop"s (gw, oif),
 * where gw must have narrower scope. This recursion stops
 * when gw has LOCAL scope or if "nexthop" is declared ONLINK,
 * which means that gw is forced to be on link.
 *
 * Code is still hairy, but now it is apparently logically
 * consistent and very flexible. F.e. as by-product it allows
 * to co-exists in peace independent exterior and interior
 * routing processes.
 *
 * Normally it looks as following.
 *
 * {universe prefix}  -> (gw, oif) [scope link]
 *		  |
 *		  |-> {link prefix} -> (gw, oif) [scope local]
 *					|
 *					|-> {local prefix} (terminal node)
 */
static int fib_check_nh(struct fib_config *cfg, struct fib_nh *nh,
			struct netlink_ext_ack *extack)
{
	int err = 0;
	struct net *net;
	struct net_device *dev;

	net = cfg->fc_nlinfo.nl_net;
	if (nh->nh_gw) {
		struct fib_result res;

		if (nh->nh_flags & RTNH_F_ONLINK) {
			unsigned int addr_type;

			if (cfg->fc_scope >= RT_SCOPE_LINK) {
				NL_SET_ERR_MSG(extack,
					       "Nexthop has invalid scope");
				return -EINVAL;
			}
			dev = __dev_get_by_index(net, nh->nh_oif);
			if (!dev) {
				NL_SET_ERR_MSG(extack, "Nexthop device required for onlink");
				return -ENODEV;
			}
			if (!(dev->flags & IFF_UP)) {
				NL_SET_ERR_MSG(extack,
					       "Nexthop device is not up");
				return -ENETDOWN;
			}
			addr_type = inet_addr_type_dev_table(net, dev, nh->nh_gw);
			if (addr_type != RTN_UNICAST) {
				NL_SET_ERR_MSG(extack,
					       "Nexthop has invalid gateway");
				return -EINVAL;
			}
			if (!netif_carrier_ok(dev))
				nh->nh_flags |= RTNH_F_LINKDOWN;
			nh->nh_dev = dev;
			dev_hold(dev);
			nh->nh_scope = RT_SCOPE_LINK;
			return 0;
		}
		rcu_read_lock();
		{
			struct fib_table *tbl = NULL;
			struct flowi4 fl4 = {
				.daddr = nh->nh_gw,
				.flowi4_scope = cfg->fc_scope + 1,
				.flowi4_oif = nh->nh_oif,
				.flowi4_iif = LOOPBACK_IFINDEX,
			};

			/* It is not necessary, but requires a bit of thinking */
			if (fl4.flowi4_scope < RT_SCOPE_LINK)
				fl4.flowi4_scope = RT_SCOPE_LINK;

			if (cfg->fc_table)
				tbl = fib_get_table(net, cfg->fc_table);

			if (tbl)
				err = fib_table_lookup(tbl, &fl4, &res,
						       FIB_LOOKUP_IGNORE_LINKSTATE |
						       FIB_LOOKUP_NOREF);

			/* on error or if no table given do full lookup. This
			 * is needed for example when nexthops are in the local
			 * table rather than the given table
			 */
			if (!tbl || err) {
				err = fib_lookup(net, &fl4, &res,
						 FIB_LOOKUP_IGNORE_LINKSTATE);
			}

			if (err) {
				NL_SET_ERR_MSG(extack,
					       "Nexthop has invalid gateway");
				rcu_read_unlock();
				return err;
			}
		}
		err = -EINVAL;
		if (res.type != RTN_UNICAST && res.type != RTN_LOCAL) {
			NL_SET_ERR_MSG(extack, "Nexthop has invalid gateway");
			goto out;
		}
		nh->nh_scope = res.scope;
		nh->nh_oif = FIB_RES_OIF(res);
		nh->nh_dev = dev = FIB_RES_DEV(res);
		if (!dev) {
			NL_SET_ERR_MSG(extack,
				       "No egress device for nexthop gateway");
			goto out;
		}
		dev_hold(dev);
		if (!netif_carrier_ok(dev))
			nh->nh_flags |= RTNH_F_LINKDOWN;
		err = (dev->flags & IFF_UP) ? 0 : -ENETDOWN;
	} else {
		struct in_device *in_dev;

		if (nh->nh_flags & (RTNH_F_PERVASIVE | RTNH_F_ONLINK)) {
			NL_SET_ERR_MSG(extack,
				       "Invalid flags for nexthop - PERVASIVE and ONLINK can not be set");
			return -EINVAL;
		}
		rcu_read_lock();
		err = -ENODEV;
		in_dev = inetdev_by_index(net, nh->nh_oif);
		if (!in_dev)
			goto out;
		err = -ENETDOWN;
		if (!(in_dev->dev->flags & IFF_UP)) {
			NL_SET_ERR_MSG(extack, "Device for nexthop is not up");
			goto out;
		}
		nh->nh_dev = in_dev->dev;
		dev_hold(nh->nh_dev);
		nh->nh_scope = RT_SCOPE_HOST;
		if (!netif_carrier_ok(nh->nh_dev))
			nh->nh_flags |= RTNH_F_LINKDOWN;
		err = 0;
	}
out:
	rcu_read_unlock();
	return err;
}

static inline unsigned int fib_laddr_hashfn(__be32 val)
{
	unsigned int mask = (fib_info_hash_size - 1);

	return ((__force u32)val ^
		((__force u32)val >> 7) ^
		((__force u32)val >> 14)) & mask;
}

static struct hlist_head *fib_info_hash_alloc(int bytes)
{
	if (bytes <= PAGE_SIZE)
		return kzalloc(bytes, GFP_KERNEL);
	else
		return (struct hlist_head *)
			__get_free_pages(GFP_KERNEL | __GFP_ZERO,
					 get_order(bytes));
}

static void fib_info_hash_free(struct hlist_head *hash, int bytes)
{
	if (!hash)
		return;

	if (bytes <= PAGE_SIZE)
		kfree(hash);
	else
		free_pages((unsigned long) hash, get_order(bytes));
}

static void fib_info_hash_move(struct hlist_head *new_info_hash,
			       struct hlist_head *new_laddrhash,
			       unsigned int new_size)
{
	struct hlist_head *old_info_hash, *old_laddrhash;
	unsigned int old_size = fib_info_hash_size;
	unsigned int i, bytes;

	spin_lock_bh(&fib_info_lock);
	old_info_hash = fib_info_hash;
	old_laddrhash = fib_info_laddrhash;
	fib_info_hash_size = new_size;

	for (i = 0; i < old_size; i++) {
		struct hlist_head *head = &fib_info_hash[i];
		struct hlist_node *n;
		struct fib_info *fi;

		hlist_for_each_entry_safe(fi, n, head, fib_hash) {
			struct hlist_head *dest;
			unsigned int new_hash;

			new_hash = fib_info_hashfn(fi);
			dest = &new_info_hash[new_hash];
			hlist_add_head(&fi->fib_hash, dest);
		}
	}
	fib_info_hash = new_info_hash;

	for (i = 0; i < old_size; i++) {
		struct hlist_head *lhead = &fib_info_laddrhash[i];
		struct hlist_node *n;
		struct fib_info *fi;

		hlist_for_each_entry_safe(fi, n, lhead, fib_lhash) {
			struct hlist_head *ldest;
			unsigned int new_hash;

			new_hash = fib_laddr_hashfn(fi->fib_prefsrc);
			ldest = &new_laddrhash[new_hash];
			hlist_add_head(&fi->fib_lhash, ldest);
		}
	}
	fib_info_laddrhash = new_laddrhash;

	spin_unlock_bh(&fib_info_lock);

	bytes = old_size * sizeof(struct hlist_head *);
	fib_info_hash_free(old_info_hash, bytes);
	fib_info_hash_free(old_laddrhash, bytes);
}

__be32 fib_info_update_nh_saddr(struct net *net, struct fib_nh *nh)
{
	nh->nh_saddr = inet_select_addr(nh->nh_dev,
					nh->nh_gw,
					nh->nh_parent->fib_scope);
	nh->nh_saddr_genid = atomic_read(&net->ipv4.dev_addr_genid);

	return nh->nh_saddr;
}

static bool fib_valid_prefsrc(struct fib_config *cfg, __be32 fib_prefsrc)
{
	if (cfg->fc_type != RTN_LOCAL || !cfg->fc_dst ||
	    fib_prefsrc != cfg->fc_dst) {
		u32 tb_id = cfg->fc_table;
		int rc;

		if (tb_id == RT_TABLE_MAIN)
			tb_id = RT_TABLE_LOCAL;

		rc = inet_addr_type_table(cfg->fc_nlinfo.nl_net,
					  fib_prefsrc, tb_id);

		if (rc != RTN_LOCAL && tb_id != RT_TABLE_LOCAL) {
			rc = inet_addr_type_table(cfg->fc_nlinfo.nl_net,
						  fib_prefsrc, RT_TABLE_LOCAL);
		}

		if (rc != RTN_LOCAL)
			return false;
	}
	return true;
}

struct fib_info *fib_create_info(struct fib_config *cfg,
				 struct netlink_ext_ack *extack)
{
	int err;
	struct fib_info *fi = NULL;
	struct fib_info *ofi;
	int nhs = 1;
	struct net *net = cfg->fc_nlinfo.nl_net;

	if (cfg->fc_type > RTN_MAX)
		goto err_inval;

	/* Fast check to catch the most weird cases */
	if (fib_props[cfg->fc_type].scope > cfg->fc_scope) {
		NL_SET_ERR_MSG(extack, "Invalid scope");
		goto err_inval;
	}

	if (cfg->fc_flags & (RTNH_F_DEAD | RTNH_F_LINKDOWN)) {
		NL_SET_ERR_MSG(extack,
			       "Invalid rtm_flags - can not contain DEAD or LINKDOWN");
		goto err_inval;
	}

#ifdef CONFIG_IP_ROUTE_MULTIPATH
	if (cfg->fc_mp) {
		nhs = fib_count_nexthops(cfg->fc_mp, cfg->fc_mp_len, extack);
		if (nhs == 0)
			goto err_inval;
	}
#endif

	err = -ENOBUFS;
	if (fib_info_cnt >= fib_info_hash_size) {
		unsigned int new_size = fib_info_hash_size << 1;
		struct hlist_head *new_info_hash;
		struct hlist_head *new_laddrhash;
		unsigned int bytes;

		if (!new_size)
			new_size = 16;
		bytes = new_size * sizeof(struct hlist_head *);
		new_info_hash = fib_info_hash_alloc(bytes);
		new_laddrhash = fib_info_hash_alloc(bytes);
		if (!new_info_hash || !new_laddrhash) {
			fib_info_hash_free(new_info_hash, bytes);
			fib_info_hash_free(new_laddrhash, bytes);
		} else
			fib_info_hash_move(new_info_hash, new_laddrhash, new_size);

		if (!fib_info_hash_size)
			goto failure;
	}

	fi = kzalloc(struct_size(fi, fib_nh, nhs), GFP_KERNEL);
	if (!fi)
		goto failure;
	fi->fib_metrics = ip_fib_metrics_init(fi->fib_net, cfg->fc_mx,
					      cfg->fc_mx_len, extack);
	if (unlikely(IS_ERR(fi->fib_metrics))) {
		err = PTR_ERR(fi->fib_metrics);
		kfree(fi);
		return ERR_PTR(err);
	}

	fib_info_cnt++;
	fi->fib_net = net;
	fi->fib_protocol = cfg->fc_protocol;
	fi->fib_scope = cfg->fc_scope;
	fi->fib_flags = cfg->fc_flags;
	fi->fib_priority = cfg->fc_priority;
	fi->fib_prefsrc = cfg->fc_prefsrc;
	fi->fib_type = cfg->fc_type;
	fi->fib_tb_id = cfg->fc_table;

	fi->fib_nhs = nhs;
	change_nexthops(fi) {
		nexthop_nh->nh_parent = fi;
	} endfor_nexthops(fi)

	if (cfg->fc_mp)
		err = fib_get_nhs(fi, cfg->fc_mp, cfg->fc_mp_len, cfg, extack);
	else
		err = fib_nh_init(net, fi->fib_nh, cfg, 1, extack);

	if (err != 0)
		goto failure;

	if (fib_props[cfg->fc_type].error) {
		if (cfg->fc_gw || cfg->fc_oif || cfg->fc_mp) {
			NL_SET_ERR_MSG(extack,
				       "Gateway, device and multipath can not be specified for this route type");
			goto err_inval;
		}
		goto link_it;
	} else {
		switch (cfg->fc_type) {
		case RTN_UNICAST:
		case RTN_LOCAL:
		case RTN_BROADCAST:
		case RTN_ANYCAST:
		case RTN_MULTICAST:
			break;
		default:
			NL_SET_ERR_MSG(extack, "Invalid route type");
			goto err_inval;
		}
	}

	if (cfg->fc_scope > RT_SCOPE_HOST) {
		NL_SET_ERR_MSG(extack, "Invalid scope");
		goto err_inval;
	}

	if (cfg->fc_scope == RT_SCOPE_HOST) {
		struct fib_nh *nh = fi->fib_nh;

		/* Local address is added. */
		if (nhs != 1) {
			NL_SET_ERR_MSG(extack,
				       "Route with host scope can not have multiple nexthops");
			goto err_inval;
		}
		if (nh->nh_gw) {
			NL_SET_ERR_MSG(extack,
				       "Route with host scope can not have a gateway");
			goto err_inval;
		}
		nh->nh_scope = RT_SCOPE_NOWHERE;
		nh->nh_dev = dev_get_by_index(net, fi->fib_nh->nh_oif);
		err = -ENODEV;
		if (!nh->nh_dev)
			goto failure;
	} else {
		int linkdown = 0;

		change_nexthops(fi) {
			err = fib_check_nh(cfg, nexthop_nh, extack);
			if (err != 0)
				goto failure;
			if (nexthop_nh->nh_flags & RTNH_F_LINKDOWN)
				linkdown++;
		} endfor_nexthops(fi)
		if (linkdown == fi->fib_nhs)
			fi->fib_flags |= RTNH_F_LINKDOWN;
	}

	if (fi->fib_prefsrc && !fib_valid_prefsrc(cfg, fi->fib_prefsrc)) {
		NL_SET_ERR_MSG(extack, "Invalid prefsrc address");
		goto err_inval;
	}

	change_nexthops(fi) {
		fib_info_update_nh_saddr(net, nexthop_nh);
	} endfor_nexthops(fi)

	fib_rebalance(fi);

link_it:
	ofi = fib_find_info(fi);
	if (ofi) {
		fi->fib_dead = 1;
		free_fib_info(fi);
		ofi->fib_treeref++;
		return ofi;
	}

	fi->fib_treeref++;
	refcount_set(&fi->fib_clntref, 1);
	spin_lock_bh(&fib_info_lock);
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

		if (!nexthop_nh->nh_dev)
			continue;
		hash = fib_devindex_hashfn(nexthop_nh->nh_dev->ifindex);
		head = &fib_info_devhash[hash];
		hlist_add_head(&nexthop_nh->nh_hash, head);
	} endfor_nexthops(fi)
	spin_unlock_bh(&fib_info_lock);
	return fi;

err_inval:
	err = -EINVAL;

failure:
	if (fi) {
		fi->fib_dead = 1;
		free_fib_info(fi);
	}

	return ERR_PTR(err);
}

int fib_dump_info(struct sk_buff *skb, u32 portid, u32 seq, int event,
		  u32 tb_id, u8 type, __be32 dst, int dst_len, u8 tos,
		  struct fib_info *fi, unsigned int flags)
{
	struct nlmsghdr *nlh;
	struct rtmsg *rtm;

	nlh = nlmsg_put(skb, portid, seq, event, sizeof(*rtm), flags);
	if (!nlh)
		return -EMSGSIZE;

	rtm = nlmsg_data(nlh);
	rtm->rtm_family = AF_INET;
	rtm->rtm_dst_len = dst_len;
	rtm->rtm_src_len = 0;
	rtm->rtm_tos = tos;
	if (tb_id < 256)
		rtm->rtm_table = tb_id;
	else
		rtm->rtm_table = RT_TABLE_COMPAT;
	if (nla_put_u32(skb, RTA_TABLE, tb_id))
		goto nla_put_failure;
	rtm->rtm_type = type;
	rtm->rtm_flags = fi->fib_flags;
	rtm->rtm_scope = fi->fib_scope;
	rtm->rtm_protocol = fi->fib_protocol;

	if (rtm->rtm_dst_len &&
	    nla_put_in_addr(skb, RTA_DST, dst))
		goto nla_put_failure;
	if (fi->fib_priority &&
	    nla_put_u32(skb, RTA_PRIORITY, fi->fib_priority))
		goto nla_put_failure;
	if (rtnetlink_put_metrics(skb, fi->fib_metrics->metrics) < 0)
		goto nla_put_failure;

	if (fi->fib_prefsrc &&
	    nla_put_in_addr(skb, RTA_PREFSRC, fi->fib_prefsrc))
		goto nla_put_failure;
	if (fi->fib_nhs == 1) {
		if (fi->fib_nh->nh_gw &&
		    nla_put_in_addr(skb, RTA_GATEWAY, fi->fib_nh->nh_gw))
			goto nla_put_failure;
		if (fi->fib_nh->nh_oif &&
		    nla_put_u32(skb, RTA_OIF, fi->fib_nh->nh_oif))
			goto nla_put_failure;
		if (fi->fib_nh->nh_flags & RTNH_F_LINKDOWN) {
			rcu_read_lock();
			if (ip_ignore_linkdown(fi->fib_nh->nh_dev))
				rtm->rtm_flags |= RTNH_F_DEAD;
			rcu_read_unlock();
		}
		if (fi->fib_nh->nh_flags & RTNH_F_OFFLOAD)
			rtm->rtm_flags |= RTNH_F_OFFLOAD;
#ifdef CONFIG_IP_ROUTE_CLASSID
		if (fi->fib_nh[0].nh_tclassid &&
		    nla_put_u32(skb, RTA_FLOW, fi->fib_nh[0].nh_tclassid))
			goto nla_put_failure;
#endif
		if (fi->fib_nh->nh_lwtstate &&
		    lwtunnel_fill_encap(skb, fi->fib_nh->nh_lwtstate) < 0)
			goto nla_put_failure;
	}
#ifdef CONFIG_IP_ROUTE_MULTIPATH
	if (fi->fib_nhs > 1) {
		struct rtnexthop *rtnh;
		struct nlattr *mp;

		mp = nla_nest_start(skb, RTA_MULTIPATH);
		if (!mp)
			goto nla_put_failure;

		for_nexthops(fi) {
			rtnh = nla_reserve_nohdr(skb, sizeof(*rtnh));
			if (!rtnh)
				goto nla_put_failure;

			rtnh->rtnh_flags = nh->nh_flags & 0xFF;
			if (nh->nh_flags & RTNH_F_LINKDOWN) {
				rcu_read_lock();
				if (ip_ignore_linkdown(nh->nh_dev))
					rtnh->rtnh_flags |= RTNH_F_DEAD;
				rcu_read_unlock();
			}
			rtnh->rtnh_hops = nh->nh_weight - 1;
			rtnh->rtnh_ifindex = nh->nh_oif;

			if (nh->nh_gw &&
			    nla_put_in_addr(skb, RTA_GATEWAY, nh->nh_gw))
				goto nla_put_failure;
#ifdef CONFIG_IP_ROUTE_CLASSID
			if (nh->nh_tclassid &&
			    nla_put_u32(skb, RTA_FLOW, nh->nh_tclassid))
				goto nla_put_failure;
#endif
			if (nh->nh_lwtstate &&
			    lwtunnel_fill_encap(skb, nh->nh_lwtstate) < 0)
				goto nla_put_failure;

			/* length of rtnetlink header + attributes */
			rtnh->rtnh_len = nlmsg_get_pos(skb) - (void *) rtnh;
		} endfor_nexthops(fi);

		nla_nest_end(skb, mp);
	}
#endif
	nlmsg_end(skb, nlh);
	return 0;

nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

/*
 * Update FIB if:
 * - local address disappeared -> we must delete all the entries
 *   referring to it.
 * - device went down -> we must shutdown all nexthops going via it.
 */
int fib_sync_down_addr(struct net_device *dev, __be32 local)
{
	int ret = 0;
	unsigned int hash = fib_laddr_hashfn(local);
	struct hlist_head *head = &fib_info_laddrhash[hash];
	struct net *net = dev_net(dev);
	int tb_id = l3mdev_fib_table(dev);
	struct fib_info *fi;

	if (!fib_info_laddrhash || local == 0)
		return 0;

	hlist_for_each_entry(fi, head, fib_lhash) {
		if (!net_eq(fi->fib_net, net) ||
		    fi->fib_tb_id != tb_id)
			continue;
		if (fi->fib_prefsrc == local) {
			fi->fib_flags |= RTNH_F_DEAD;
			ret++;
		}
	}
	return ret;
}

static int call_fib_nh_notifiers(struct fib_nh *fib_nh,
				 enum fib_event_type event_type)
{
	bool ignore_link_down = ip_ignore_linkdown(fib_nh->nh_dev);
	struct fib_nh_notifier_info info = {
		.fib_nh = fib_nh,
	};

	switch (event_type) {
	case FIB_EVENT_NH_ADD:
		if (fib_nh->nh_flags & RTNH_F_DEAD)
			break;
		if (ignore_link_down && fib_nh->nh_flags & RTNH_F_LINKDOWN)
			break;
		return call_fib4_notifiers(dev_net(fib_nh->nh_dev), event_type,
					   &info.info);
	case FIB_EVENT_NH_DEL:
		if ((ignore_link_down && fib_nh->nh_flags & RTNH_F_LINKDOWN) ||
		    (fib_nh->nh_flags & RTNH_F_DEAD))
			return call_fib4_notifiers(dev_net(fib_nh->nh_dev),
						   event_type, &info.info);
	default:
		break;
	}

	return NOTIFY_DONE;
}

/* Update the PMTU of exceptions when:
 * - the new MTU of the first hop becomes smaller than the PMTU
 * - the old MTU was the same as the PMTU, and it limited discovery of
 *   larger MTUs on the path. With that limit raised, we can now
 *   discover larger MTUs
 * A special case is locked exceptions, for which the PMTU is smaller
 * than the minimal accepted PMTU:
 * - if the new MTU is greater than the PMTU, don't make any change
 * - otherwise, unlock and set PMTU
 */
static void nh_update_mtu(struct fib_nh *nh, u32 new, u32 orig)
{
	struct fnhe_hash_bucket *bucket;
	int i;

	bucket = rcu_dereference_protected(nh->nh_exceptions, 1);
	if (!bucket)
		return;

	for (i = 0; i < FNHE_HASH_SIZE; i++) {
		struct fib_nh_exception *fnhe;

		for (fnhe = rcu_dereference_protected(bucket[i].chain, 1);
		     fnhe;
		     fnhe = rcu_dereference_protected(fnhe->fnhe_next, 1)) {
			if (fnhe->fnhe_mtu_locked) {
				if (new <= fnhe->fnhe_pmtu) {
					fnhe->fnhe_pmtu = new;
					fnhe->fnhe_mtu_locked = false;
				}
			} else if (new < fnhe->fnhe_pmtu ||
				   orig == fnhe->fnhe_pmtu) {
				fnhe->fnhe_pmtu = new;
			}
		}
	}
}

void fib_sync_mtu(struct net_device *dev, u32 orig_mtu)
{
	unsigned int hash = fib_devindex_hashfn(dev->ifindex);
	struct hlist_head *head = &fib_info_devhash[hash];
	struct fib_nh *nh;

	hlist_for_each_entry(nh, head, nh_hash) {
		if (nh->nh_dev == dev)
			nh_update_mtu(nh, dev->mtu, orig_mtu);
	}
}

/* Event              force Flags           Description
 * NETDEV_CHANGE      0     LINKDOWN        Carrier OFF, not for scope host
 * NETDEV_DOWN        0     LINKDOWN|DEAD   Link down, not for scope host
 * NETDEV_DOWN        1     LINKDOWN|DEAD   Last address removed
 * NETDEV_UNREGISTER  1     LINKDOWN|DEAD   Device removed
 */
int fib_sync_down_dev(struct net_device *dev, unsigned long event, bool force)
{
	int ret = 0;
	int scope = RT_SCOPE_NOWHERE;
	struct fib_info *prev_fi = NULL;
	unsigned int hash = fib_devindex_hashfn(dev->ifindex);
	struct hlist_head *head = &fib_info_devhash[hash];
	struct fib_nh *nh;

	if (force)
		scope = -1;

	hlist_for_each_entry(nh, head, nh_hash) {
		struct fib_info *fi = nh->nh_parent;
		int dead;

		BUG_ON(!fi->fib_nhs);
		if (nh->nh_dev != dev || fi == prev_fi)
			continue;
		prev_fi = fi;
		dead = 0;
		change_nexthops(fi) {
			if (nexthop_nh->nh_flags & RTNH_F_DEAD)
				dead++;
			else if (nexthop_nh->nh_dev == dev &&
				 nexthop_nh->nh_scope != scope) {
				switch (event) {
				case NETDEV_DOWN:
				case NETDEV_UNREGISTER:
					nexthop_nh->nh_flags |= RTNH_F_DEAD;
					/* fall through */
				case NETDEV_CHANGE:
					nexthop_nh->nh_flags |= RTNH_F_LINKDOWN;
					break;
				}
				call_fib_nh_notifiers(nexthop_nh,
						      FIB_EVENT_NH_DEL);
				dead++;
			}
#ifdef CONFIG_IP_ROUTE_MULTIPATH
			if (event == NETDEV_UNREGISTER &&
			    nexthop_nh->nh_dev == dev) {
				dead = fi->fib_nhs;
				break;
			}
#endif
		} endfor_nexthops(fi)
		if (dead == fi->fib_nhs) {
			switch (event) {
			case NETDEV_DOWN:
			case NETDEV_UNREGISTER:
				fi->fib_flags |= RTNH_F_DEAD;
				/* fall through */
			case NETDEV_CHANGE:
				fi->fib_flags |= RTNH_F_LINKDOWN;
				break;
			}
			ret++;
		}

		fib_rebalance(fi);
	}

	return ret;
}

/* Must be invoked inside of an RCU protected region.  */
static void fib_select_default(const struct flowi4 *flp, struct fib_result *res)
{
	struct fib_info *fi = NULL, *last_resort = NULL;
	struct hlist_head *fa_head = res->fa_head;
	struct fib_table *tb = res->table;
	u8 slen = 32 - res->prefixlen;
	int order = -1, last_idx = -1;
	struct fib_alias *fa, *fa1 = NULL;
	u32 last_prio = res->fi->fib_priority;
	u8 last_tos = 0;

	hlist_for_each_entry_rcu(fa, fa_head, fa_list) {
		struct fib_info *next_fi = fa->fa_info;

		if (fa->fa_slen != slen)
			continue;
		if (fa->fa_tos && fa->fa_tos != flp->flowi4_tos)
			continue;
		if (fa->tb_id != tb->tb_id)
			continue;
		if (next_fi->fib_priority > last_prio &&
		    fa->fa_tos == last_tos) {
			if (last_tos)
				continue;
			break;
		}
		if (next_fi->fib_flags & RTNH_F_DEAD)
			continue;
		last_tos = fa->fa_tos;
		last_prio = next_fi->fib_priority;

		if (next_fi->fib_scope != res->scope ||
		    fa->fa_type != RTN_UNICAST)
			continue;
		if (!next_fi->fib_nh[0].nh_gw ||
		    next_fi->fib_nh[0].nh_scope != RT_SCOPE_LINK)
			continue;

		fib_alias_accessed(fa);

		if (!fi) {
			if (next_fi != res->fi)
				break;
			fa1 = fa;
		} else if (!fib_detect_death(fi, order, &last_resort,
					     &last_idx, fa1->fa_default)) {
			fib_result_assign(res, fi);
			fa1->fa_default = order;
			goto out;
		}
		fi = next_fi;
		order++;
	}

	if (order <= 0 || !fi) {
		if (fa1)
			fa1->fa_default = -1;
		goto out;
	}

	if (!fib_detect_death(fi, order, &last_resort, &last_idx,
			      fa1->fa_default)) {
		fib_result_assign(res, fi);
		fa1->fa_default = order;
		goto out;
	}

	if (last_idx >= 0)
		fib_result_assign(res, last_resort);
	fa1->fa_default = last_idx;
out:
	return;
}

/*
 * Dead device goes up. We wake up dead nexthops.
 * It takes sense only on multipath routes.
 */
int fib_sync_up(struct net_device *dev, unsigned int nh_flags)
{
	struct fib_info *prev_fi;
	unsigned int hash;
	struct hlist_head *head;
	struct fib_nh *nh;
	int ret;

	if (!(dev->flags & IFF_UP))
		return 0;

	if (nh_flags & RTNH_F_DEAD) {
		unsigned int flags = dev_get_flags(dev);

		if (flags & (IFF_RUNNING | IFF_LOWER_UP))
			nh_flags |= RTNH_F_LINKDOWN;
	}

	prev_fi = NULL;
	hash = fib_devindex_hashfn(dev->ifindex);
	head = &fib_info_devhash[hash];
	ret = 0;

	hlist_for_each_entry(nh, head, nh_hash) {
		struct fib_info *fi = nh->nh_parent;
		int alive;

		BUG_ON(!fi->fib_nhs);
		if (nh->nh_dev != dev || fi == prev_fi)
			continue;

		prev_fi = fi;
		alive = 0;
		change_nexthops(fi) {
			if (!(nexthop_nh->nh_flags & nh_flags)) {
				alive++;
				continue;
			}
			if (!nexthop_nh->nh_dev ||
			    !(nexthop_nh->nh_dev->flags & IFF_UP))
				continue;
			if (nexthop_nh->nh_dev != dev ||
			    !__in_dev_get_rtnl(dev))
				continue;
			alive++;
			nexthop_nh->nh_flags &= ~nh_flags;
			call_fib_nh_notifiers(nexthop_nh, FIB_EVENT_NH_ADD);
		} endfor_nexthops(fi)

		if (alive > 0) {
			fi->fib_flags &= ~nh_flags;
			ret++;
		}

		fib_rebalance(fi);
	}

	return ret;
}

#ifdef CONFIG_IP_ROUTE_MULTIPATH
static bool fib_good_nh(const struct fib_nh *nh)
{
	int state = NUD_REACHABLE;

	if (nh->nh_scope == RT_SCOPE_LINK) {
		struct neighbour *n;

		rcu_read_lock_bh();

		n = __ipv4_neigh_lookup_noref(nh->nh_dev,
					      (__force u32)nh->nh_gw);
		if (n)
			state = n->nud_state;

		rcu_read_unlock_bh();
	}

	return !!(state & NUD_VALID);
}

void fib_select_multipath(struct fib_result *res, int hash)
{
	struct fib_info *fi = res->fi;
	struct net *net = fi->fib_net;
	bool first = false;

	for_nexthops(fi) {
		if (net->ipv4.sysctl_fib_multipath_use_neigh) {
			if (!fib_good_nh(nh))
				continue;
			if (!first) {
				res->nh_sel = nhsel;
				first = true;
			}
		}

		if (hash > atomic_read(&nh->nh_upper_bound))
			continue;

		res->nh_sel = nhsel;
		return;
	} endfor_nexthops(fi);
}
#endif

void fib_select_path(struct net *net, struct fib_result *res,
		     struct flowi4 *fl4, const struct sk_buff *skb)
{
	if (fl4->flowi4_oif && !(fl4->flowi4_flags & FLOWI_FLAG_SKIP_NH_OIF))
		goto check_saddr;

#ifdef CONFIG_IP_ROUTE_MULTIPATH
	if (res->fi->fib_nhs > 1) {
		int h = fib_multipath_hash(net, fl4, skb, NULL);

		fib_select_multipath(res, h);
	}
	else
#endif
	if (!res->prefixlen &&
	    res->table->tb_num_default > 1 &&
	    res->type == RTN_UNICAST)
		fib_select_default(fl4, res);

check_saddr:
	if (!fl4->saddr)
		fl4->saddr = FIB_RES_PREFSRC(net, *res);
}
