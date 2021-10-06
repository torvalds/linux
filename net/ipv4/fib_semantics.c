// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		IPv4 Forwarding Information Base: semantics.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
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
#include <net/ip6_fib.h>
#include <net/nexthop.h>
#include <net/netlink.h>
#include <net/rtnh.h>
#include <net/lwtunnel.h>
#include <net/fib_notifier.h>
#include <net/addrconf.h>

#include "fib_lookup.h"

static DEFINE_SPINLOCK(fib_info_lock);
static struct hlist_head *fib_info_hash;
static struct hlist_head *fib_info_laddrhash;
static unsigned int fib_info_hash_size;
static unsigned int fib_info_cnt;

#define DEVINDEX_HASHBITS 8
#define DEVINDEX_HASHSIZE (1U << DEVINDEX_HASHBITS)
static struct hlist_head fib_info_devhash[DEVINDEX_HASHSIZE];

/* for_nexthops and change_nexthops only used when nexthop object
 * is not set in a fib_info. The logic within can reference fib_nh.
 */
#ifdef CONFIG_IP_ROUTE_MULTIPATH

#define for_nexthops(fi) {						\
	int nhsel; const struct fib_nh *nh;				\
	for (nhsel = 0, nh = (fi)->fib_nh;				\
	     nhsel < fib_info_num_path((fi));				\
	     nh++, nhsel++)

#define change_nexthops(fi) {						\
	int nhsel; struct fib_nh *nexthop_nh;				\
	for (nhsel = 0,	nexthop_nh = (struct fib_nh *)((fi)->fib_nh);	\
	     nhsel < fib_info_num_path((fi));				\
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

static void free_nh_exceptions(struct fib_nh_common *nhc)
{
	struct fnhe_hash_bucket *hash;
	int i;

	hash = rcu_dereference_protected(nhc->nhc_exceptions, 1);
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

void fib_nh_common_release(struct fib_nh_common *nhc)
{
	if (nhc->nhc_dev)
		dev_put(nhc->nhc_dev);

	lwtstate_put(nhc->nhc_lwtstate);
	rt_fibinfo_free_cpus(nhc->nhc_pcpu_rth_output);
	rt_fibinfo_free(&nhc->nhc_rth_input);
	free_nh_exceptions(nhc);
}
EXPORT_SYMBOL_GPL(fib_nh_common_release);

void fib_nh_release(struct net *net, struct fib_nh *fib_nh)
{
#ifdef CONFIG_IP_ROUTE_CLASSID
	if (fib_nh->nh_tclassid)
		net->ipv4.fib_num_tclassid_users--;
#endif
	fib_nh_common_release(&fib_nh->nh_common);
}

/* Release a nexthop info record */
static void free_fib_info_rcu(struct rcu_head *head)
{
	struct fib_info *fi = container_of(head, struct fib_info, rcu);

	if (fi->nh) {
		nexthop_put(fi->nh);
	} else {
		change_nexthops(fi) {
			fib_nh_release(fi->fib_net, nexthop_nh);
		} endfor_nexthops(fi);
	}

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
		if (fi->nh) {
			list_del(&fi->nh_list);
		} else {
			change_nexthops(fi) {
				if (!nexthop_nh->fib_nh_dev)
					continue;
				hlist_del(&nexthop_nh->nh_hash);
			} endfor_nexthops(fi)
		}
		fi->fib_dead = 1;
		fib_info_put(fi);
	}
	spin_unlock_bh(&fib_info_lock);
}

static inline int nh_comp(struct fib_info *fi, struct fib_info *ofi)
{
	const struct fib_nh *onh;

	if (fi->nh || ofi->nh)
		return nexthop_cmp(fi->nh, ofi->nh) ? 0 : -1;

	if (ofi->fib_nhs == 0)
		return 0;

	for_nexthops(fi) {
		onh = fib_info_nh(ofi, nhsel);

		if (nh->fib_nh_oif != onh->fib_nh_oif ||
		    nh->fib_nh_gw_family != onh->fib_nh_gw_family ||
		    nh->fib_nh_scope != onh->fib_nh_scope ||
#ifdef CONFIG_IP_ROUTE_MULTIPATH
		    nh->fib_nh_weight != onh->fib_nh_weight ||
#endif
#ifdef CONFIG_IP_ROUTE_CLASSID
		    nh->nh_tclassid != onh->nh_tclassid ||
#endif
		    lwtunnel_cmp_encap(nh->fib_nh_lws, onh->fib_nh_lws) ||
		    ((nh->fib_nh_flags ^ onh->fib_nh_flags) & ~RTNH_COMPARE_MASK))
			return -1;

		if (nh->fib_nh_gw_family == AF_INET &&
		    nh->fib_nh_gw4 != onh->fib_nh_gw4)
			return -1;

		if (nh->fib_nh_gw_family == AF_INET6 &&
		    ipv6_addr_cmp(&nh->fib_nh_gw6, &onh->fib_nh_gw6))
			return -1;
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

static unsigned int fib_info_hashfn_1(int init_val, u8 protocol, u8 scope,
				      u32 prefsrc, u32 priority)
{
	unsigned int val = init_val;

	val ^= (protocol << 8) | scope;
	val ^= prefsrc;
	val ^= priority;

	return val;
}

static unsigned int fib_info_hashfn_result(unsigned int val)
{
	unsigned int mask = (fib_info_hash_size - 1);

	return (val ^ (val >> 7) ^ (val >> 12)) & mask;
}

static inline unsigned int fib_info_hashfn(struct fib_info *fi)
{
	unsigned int val;

	val = fib_info_hashfn_1(fi->fib_nhs, fi->fib_protocol,
				fi->fib_scope, (__force u32)fi->fib_prefsrc,
				fi->fib_priority);

	if (fi->nh) {
		val ^= fib_devindex_hashfn(fi->nh->id);
	} else {
		for_nexthops(fi) {
			val ^= fib_devindex_hashfn(nh->fib_nh_oif);
		} endfor_nexthops(fi)
	}

	return fib_info_hashfn_result(val);
}

/* no metrics, only nexthop id */
static struct fib_info *fib_find_info_nh(struct net *net,
					 const struct fib_config *cfg)
{
	struct hlist_head *head;
	struct fib_info *fi;
	unsigned int hash;

	hash = fib_info_hashfn_1(fib_devindex_hashfn(cfg->fc_nh_id),
				 cfg->fc_protocol, cfg->fc_scope,
				 (__force u32)cfg->fc_prefsrc,
				 cfg->fc_priority);
	hash = fib_info_hashfn_result(hash);
	head = &fib_info_hash[hash];

	hlist_for_each_entry(fi, head, fib_hash) {
		if (!net_eq(fi->fib_net, net))
			continue;
		if (!fi->nh || fi->nh->id != cfg->fc_nh_id)
			continue;
		if (cfg->fc_protocol == fi->fib_protocol &&
		    cfg->fc_scope == fi->fib_scope &&
		    cfg->fc_prefsrc == fi->fib_prefsrc &&
		    cfg->fc_priority == fi->fib_priority &&
		    cfg->fc_type == fi->fib_type &&
		    cfg->fc_table == fi->fib_tb_id &&
		    !((cfg->fc_flags ^ fi->fib_flags) & ~RTNH_COMPARE_MASK))
			return fi;
	}

	return NULL;
}

static struct fib_info *fib_find_info(struct fib_info *nfi)
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
		    nh_comp(fi, nfi) == 0)
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
		if (nh->fib_nh_dev == dev &&
		    nh->fib_nh_gw4 == gw &&
		    !(nh->fib_nh_flags & RTNH_F_DEAD)) {
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
	unsigned int nhs = fib_info_num_path(fi);

	/* space for nested metrics */
	payload += nla_total_size((RTAX_MAX * nla_total_size(4)));

	if (fi->nh)
		payload += nla_total_size(4); /* RTA_NH_ID */

	if (nhs) {
		size_t nh_encapsize = 0;
		/* Also handles the special case nhs == 1 */

		/* each nexthop is packed in an attribute */
		size_t nhsize = nla_total_size(sizeof(struct rtnexthop));
		unsigned int i;

		/* may contain flow and gateway attribute */
		nhsize += 2 * nla_total_size(4);

		/* grab encap info */
		for (i = 0; i < fib_info_num_path(fi); i++) {
			struct fib_nh_common *nhc = fib_info_nhc(fi, i);

			if (nhc->nhc_lwtstate) {
				/* RTA_ENCAP_TYPE */
				nh_encapsize += lwtunnel_get_encap_size(
						nhc->nhc_lwtstate);
				/* RTA_ENCAP */
				nh_encapsize +=  nla_total_size(2);
			}
		}

		/* all nexthops are packed in a nested attribute */
		payload += nla_total_size((nhs * nhsize) + nh_encapsize);

	}

	return payload;
}

void rtmsg_fib(int event, __be32 key, struct fib_alias *fa,
	       int dst_len, u32 tb_id, const struct nl_info *info,
	       unsigned int nlm_flags)
{
	struct fib_rt_info fri;
	struct sk_buff *skb;
	u32 seq = info->nlh ? info->nlh->nlmsg_seq : 0;
	int err = -ENOBUFS;

	skb = nlmsg_new(fib_nlmsg_size(fa->fa_info), GFP_KERNEL);
	if (!skb)
		goto errout;

	fri.fi = fa->fa_info;
	fri.tb_id = tb_id;
	fri.dst = key;
	fri.dst_len = dst_len;
	fri.tos = fa->fa_tos;
	fri.type = fa->fa_type;
	fri.offload = fa->offload;
	fri.trap = fa->trap;
	err = fib_dump_info(skb, info->portid, seq, event, &fri, nlm_flags);
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
	const struct fib_nh_common *nhc = fib_info_nhc(fi, 0);
	struct neighbour *n;
	int state = NUD_NONE;

	if (likely(nhc->nhc_gw_family == AF_INET))
		n = neigh_lookup(&arp_tbl, &nhc->nhc_gw.ipv4, nhc->nhc_dev);
	else if (nhc->nhc_gw_family == AF_INET6)
		n = neigh_lookup(ipv6_stub->nd_tbl, &nhc->nhc_gw.ipv6,
				 nhc->nhc_dev);
	else
		n = NULL;

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

int fib_nh_common_init(struct net *net, struct fib_nh_common *nhc,
		       struct nlattr *encap, u16 encap_type,
		       void *cfg, gfp_t gfp_flags,
		       struct netlink_ext_ack *extack)
{
	int err;

	nhc->nhc_pcpu_rth_output = alloc_percpu_gfp(struct rtable __rcu *,
						    gfp_flags);
	if (!nhc->nhc_pcpu_rth_output)
		return -ENOMEM;

	if (encap) {
		struct lwtunnel_state *lwtstate;

		if (encap_type == LWTUNNEL_ENCAP_NONE) {
			NL_SET_ERR_MSG(extack, "LWT encap type not specified");
			err = -EINVAL;
			goto lwt_failure;
		}
		err = lwtunnel_build_state(net, encap_type, encap,
					   nhc->nhc_family, cfg, &lwtstate,
					   extack);
		if (err)
			goto lwt_failure;

		nhc->nhc_lwtstate = lwtstate_get(lwtstate);
	}

	return 0;

lwt_failure:
	rt_fibinfo_free_cpus(nhc->nhc_pcpu_rth_output);
	nhc->nhc_pcpu_rth_output = NULL;
	return err;
}
EXPORT_SYMBOL_GPL(fib_nh_common_init);

int fib_nh_init(struct net *net, struct fib_nh *nh,
		struct fib_config *cfg, int nh_weight,
		struct netlink_ext_ack *extack)
{
	int err;

	nh->fib_nh_family = AF_INET;

	err = fib_nh_common_init(net, &nh->nh_common, cfg->fc_encap,
				 cfg->fc_encap_type, cfg, GFP_KERNEL, extack);
	if (err)
		return err;

	nh->fib_nh_oif = cfg->fc_oif;
	nh->fib_nh_gw_family = cfg->fc_gw_family;
	if (cfg->fc_gw_family == AF_INET)
		nh->fib_nh_gw4 = cfg->fc_gw4;
	else if (cfg->fc_gw_family == AF_INET6)
		nh->fib_nh_gw6 = cfg->fc_gw6;

	nh->fib_nh_flags = cfg->fc_flags;

#ifdef CONFIG_IP_ROUTE_CLASSID
	nh->nh_tclassid = cfg->fc_flow;
	if (nh->nh_tclassid)
		net->ipv4.fib_num_tclassid_users++;
#endif
#ifdef CONFIG_IP_ROUTE_MULTIPATH
	nh->fib_nh_weight = nh_weight;
#endif
	return 0;
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

/* only called when fib_nh is integrated into fib_info */
static int fib_get_nhs(struct fib_info *fi, struct rtnexthop *rtnh,
		       int remaining, struct fib_config *cfg,
		       struct netlink_ext_ack *extack)
{
	struct net *net = fi->fib_net;
	struct fib_config fib_cfg;
	struct fib_nh *nh;
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
			struct nlattr *nla, *nlav, *attrs = rtnh_attrs(rtnh);

			nla = nla_find(attrs, attrlen, RTA_GATEWAY);
			nlav = nla_find(attrs, attrlen, RTA_VIA);
			if (nla && nlav) {
				NL_SET_ERR_MSG(extack,
					       "Nexthop configuration can not contain both GATEWAY and VIA");
				return -EINVAL;
			}
			if (nla) {
				fib_cfg.fc_gw4 = nla_get_in_addr(nla);
				if (fib_cfg.fc_gw4)
					fib_cfg.fc_gw_family = AF_INET;
			} else if (nlav) {
				ret = fib_gw_from_via(&fib_cfg, nlav, extack);
				if (ret)
					goto errout;
			}

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
	nh = fib_info_nh(fi, 0);
	if (cfg->fc_oif && nh->fib_nh_oif != cfg->fc_oif) {
		NL_SET_ERR_MSG(extack,
			       "Nexthop device index does not match RTA_OIF");
		goto errout;
	}
	if (cfg->fc_gw_family) {
		if (cfg->fc_gw_family != nh->fib_nh_gw_family ||
		    (cfg->fc_gw_family == AF_INET &&
		     nh->fib_nh_gw4 != cfg->fc_gw4) ||
		    (cfg->fc_gw_family == AF_INET6 &&
		     ipv6_addr_cmp(&nh->fib_nh_gw6, &cfg->fc_gw6))) {
			NL_SET_ERR_MSG(extack,
				       "Nexthop gateway does not match RTA_GATEWAY or RTA_VIA");
			goto errout;
		}
	}
#ifdef CONFIG_IP_ROUTE_CLASSID
	if (cfg->fc_flow && nh->nh_tclassid != cfg->fc_flow) {
		NL_SET_ERR_MSG(extack,
			       "Nexthop class id does not match RTA_FLOW");
		goto errout;
	}
#endif
	ret = 0;
errout:
	return ret;
}

/* only called when fib_nh is integrated into fib_info */
static void fib_rebalance(struct fib_info *fi)
{
	int total;
	int w;

	if (fib_info_num_path(fi) < 2)
		return;

	total = 0;
	for_nexthops(fi) {
		if (nh->fib_nh_flags & RTNH_F_DEAD)
			continue;

		if (ip_ignore_linkdown(nh->fib_nh_dev) &&
		    nh->fib_nh_flags & RTNH_F_LINKDOWN)
			continue;

		total += nh->fib_nh_weight;
	} endfor_nexthops(fi);

	w = 0;
	change_nexthops(fi) {
		int upper_bound;

		if (nexthop_nh->fib_nh_flags & RTNH_F_DEAD) {
			upper_bound = -1;
		} else if (ip_ignore_linkdown(nexthop_nh->fib_nh_dev) &&
			   nexthop_nh->fib_nh_flags & RTNH_F_LINKDOWN) {
			upper_bound = -1;
		} else {
			w += nexthop_nh->fib_nh_weight;
			upper_bound = DIV_ROUND_CLOSEST_ULL((u64)w << 31,
							    total) - 1;
		}

		atomic_set(&nexthop_nh->fib_nh_upper_bound, upper_bound);
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

static int fib_encap_match(struct net *net, u16 encap_type,
			   struct nlattr *encap,
			   const struct fib_nh *nh,
			   const struct fib_config *cfg,
			   struct netlink_ext_ack *extack)
{
	struct lwtunnel_state *lwtstate;
	int ret, result = 0;

	if (encap_type == LWTUNNEL_ENCAP_NONE)
		return 0;

	ret = lwtunnel_build_state(net, encap_type, encap, AF_INET,
				   cfg, &lwtstate, extack);
	if (!ret) {
		result = lwtunnel_cmp_encap(lwtstate, nh->fib_nh_lws);
		lwtstate_free(lwtstate);
	}

	return result;
}

int fib_nh_match(struct net *net, struct fib_config *cfg, struct fib_info *fi,
		 struct netlink_ext_ack *extack)
{
#ifdef CONFIG_IP_ROUTE_MULTIPATH
	struct rtnexthop *rtnh;
	int remaining;
#endif

	if (cfg->fc_priority && cfg->fc_priority != fi->fib_priority)
		return 1;

	if (cfg->fc_nh_id) {
		if (fi->nh && cfg->fc_nh_id == fi->nh->id)
			return 0;
		return 1;
	}

	if (cfg->fc_oif || cfg->fc_gw_family) {
		struct fib_nh *nh = fib_info_nh(fi, 0);

		if (cfg->fc_encap) {
			if (fib_encap_match(net, cfg->fc_encap_type,
					    cfg->fc_encap, nh, cfg, extack))
				return 1;
		}
#ifdef CONFIG_IP_ROUTE_CLASSID
		if (cfg->fc_flow &&
		    cfg->fc_flow != nh->nh_tclassid)
			return 1;
#endif
		if ((cfg->fc_oif && cfg->fc_oif != nh->fib_nh_oif) ||
		    (cfg->fc_gw_family &&
		     cfg->fc_gw_family != nh->fib_nh_gw_family))
			return 1;

		if (cfg->fc_gw_family == AF_INET &&
		    cfg->fc_gw4 != nh->fib_nh_gw4)
			return 1;

		if (cfg->fc_gw_family == AF_INET6 &&
		    ipv6_addr_cmp(&cfg->fc_gw6, &nh->fib_nh_gw6))
			return 1;

		return 0;
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

		if (rtnh->rtnh_ifindex && rtnh->rtnh_ifindex != nh->fib_nh_oif)
			return 1;

		attrlen = rtnh_attrlen(rtnh);
		if (attrlen > 0) {
			struct nlattr *nla, *nlav, *attrs = rtnh_attrs(rtnh);

			nla = nla_find(attrs, attrlen, RTA_GATEWAY);
			nlav = nla_find(attrs, attrlen, RTA_VIA);
			if (nla && nlav) {
				NL_SET_ERR_MSG(extack,
					       "Nexthop configuration can not contain both GATEWAY and VIA");
				return -EINVAL;
			}

			if (nla) {
				if (nh->fib_nh_gw_family != AF_INET ||
				    nla_get_in_addr(nla) != nh->fib_nh_gw4)
					return 1;
			} else if (nlav) {
				struct fib_config cfg2;
				int err;

				err = fib_gw_from_via(&cfg2, nlav, extack);
				if (err)
					return err;

				switch (nh->fib_nh_gw_family) {
				case AF_INET:
					if (cfg2.fc_gw_family != AF_INET ||
					    cfg2.fc_gw4 != nh->fib_nh_gw4)
						return 1;
					break;
				case AF_INET6:
					if (cfg2.fc_gw_family != AF_INET6 ||
					    ipv6_addr_cmp(&cfg2.fc_gw6,
							  &nh->fib_nh_gw6))
						return 1;
					break;
				}
			}

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

static int fib_check_nh_v6_gw(struct net *net, struct fib_nh *nh,
			      u32 table, struct netlink_ext_ack *extack)
{
	struct fib6_config cfg = {
		.fc_table = table,
		.fc_flags = nh->fib_nh_flags | RTF_GATEWAY,
		.fc_ifindex = nh->fib_nh_oif,
		.fc_gateway = nh->fib_nh_gw6,
	};
	struct fib6_nh fib6_nh = {};
	int err;

	err = ipv6_stub->fib6_nh_init(net, &fib6_nh, &cfg, GFP_KERNEL, extack);
	if (!err) {
		nh->fib_nh_dev = fib6_nh.fib_nh_dev;
		dev_hold(nh->fib_nh_dev);
		nh->fib_nh_oif = nh->fib_nh_dev->ifindex;
		nh->fib_nh_scope = RT_SCOPE_LINK;

		ipv6_stub->fib6_nh_release(&fib6_nh);
	}

	return err;
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
static int fib_check_nh_v4_gw(struct net *net, struct fib_nh *nh, u32 table,
			      u8 scope, struct netlink_ext_ack *extack)
{
	struct net_device *dev;
	struct fib_result res;
	int err = 0;

	if (nh->fib_nh_flags & RTNH_F_ONLINK) {
		unsigned int addr_type;

		if (scope >= RT_SCOPE_LINK) {
			NL_SET_ERR_MSG(extack, "Nexthop has invalid scope");
			return -EINVAL;
		}
		dev = __dev_get_by_index(net, nh->fib_nh_oif);
		if (!dev) {
			NL_SET_ERR_MSG(extack, "Nexthop device required for onlink");
			return -ENODEV;
		}
		if (!(dev->flags & IFF_UP)) {
			NL_SET_ERR_MSG(extack, "Nexthop device is not up");
			return -ENETDOWN;
		}
		addr_type = inet_addr_type_dev_table(net, dev, nh->fib_nh_gw4);
		if (addr_type != RTN_UNICAST) {
			NL_SET_ERR_MSG(extack, "Nexthop has invalid gateway");
			return -EINVAL;
		}
		if (!netif_carrier_ok(dev))
			nh->fib_nh_flags |= RTNH_F_LINKDOWN;
		nh->fib_nh_dev = dev;
		dev_hold(dev);
		nh->fib_nh_scope = RT_SCOPE_LINK;
		return 0;
	}
	rcu_read_lock();
	{
		struct fib_table *tbl = NULL;
		struct flowi4 fl4 = {
			.daddr = nh->fib_nh_gw4,
			.flowi4_scope = scope + 1,
			.flowi4_oif = nh->fib_nh_oif,
			.flowi4_iif = LOOPBACK_IFINDEX,
		};

		/* It is not necessary, but requires a bit of thinking */
		if (fl4.flowi4_scope < RT_SCOPE_LINK)
			fl4.flowi4_scope = RT_SCOPE_LINK;

		if (table && table != RT_TABLE_MAIN)
			tbl = fib_get_table(net, table);

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
			NL_SET_ERR_MSG(extack, "Nexthop has invalid gateway");
			goto out;
		}
	}

	err = -EINVAL;
	if (res.type != RTN_UNICAST && res.type != RTN_LOCAL) {
		NL_SET_ERR_MSG(extack, "Nexthop has invalid gateway");
		goto out;
	}
	nh->fib_nh_scope = res.scope;
	nh->fib_nh_oif = FIB_RES_OIF(res);
	nh->fib_nh_dev = dev = FIB_RES_DEV(res);
	if (!dev) {
		NL_SET_ERR_MSG(extack,
			       "No egress device for nexthop gateway");
		goto out;
	}
	dev_hold(dev);
	if (!netif_carrier_ok(dev))
		nh->fib_nh_flags |= RTNH_F_LINKDOWN;
	err = (dev->flags & IFF_UP) ? 0 : -ENETDOWN;
out:
	rcu_read_unlock();
	return err;
}

static int fib_check_nh_nongw(struct net *net, struct fib_nh *nh,
			      struct netlink_ext_ack *extack)
{
	struct in_device *in_dev;
	int err;

	if (nh->fib_nh_flags & (RTNH_F_PERVASIVE | RTNH_F_ONLINK)) {
		NL_SET_ERR_MSG(extack,
			       "Invalid flags for nexthop - PERVASIVE and ONLINK can not be set");
		return -EINVAL;
	}

	rcu_read_lock();

	err = -ENODEV;
	in_dev = inetdev_by_index(net, nh->fib_nh_oif);
	if (!in_dev)
		goto out;
	err = -ENETDOWN;
	if (!(in_dev->dev->flags & IFF_UP)) {
		NL_SET_ERR_MSG(extack, "Device for nexthop is not up");
		goto out;
	}

	nh->fib_nh_dev = in_dev->dev;
	dev_hold(nh->fib_nh_dev);
	nh->fib_nh_scope = RT_SCOPE_HOST;
	if (!netif_carrier_ok(nh->fib_nh_dev))
		nh->fib_nh_flags |= RTNH_F_LINKDOWN;
	err = 0;
out:
	rcu_read_unlock();
	return err;
}

int fib_check_nh(struct net *net, struct fib_nh *nh, u32 table, u8 scope,
		 struct netlink_ext_ack *extack)
{
	int err;

	if (nh->fib_nh_gw_family == AF_INET)
		err = fib_check_nh_v4_gw(net, nh, table, scope, extack);
	else if (nh->fib_nh_gw_family == AF_INET6)
		err = fib_check_nh_v6_gw(net, nh, table, extack);
	else
		err = fib_check_nh_nongw(net, nh, extack);

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

__be32 fib_info_update_nhc_saddr(struct net *net, struct fib_nh_common *nhc,
				 unsigned char scope)
{
	struct fib_nh *nh;

	if (nhc->nhc_family != AF_INET)
		return inet_select_addr(nhc->nhc_dev, 0, scope);

	nh = container_of(nhc, struct fib_nh, nh_common);
	nh->nh_saddr = inet_select_addr(nh->fib_nh_dev, nh->fib_nh_gw4, scope);
	nh->nh_saddr_genid = atomic_read(&net->ipv4.dev_addr_genid);

	return nh->nh_saddr;
}

__be32 fib_result_prefsrc(struct net *net, struct fib_result *res)
{
	struct fib_nh_common *nhc = res->nhc;

	if (res->fi->fib_prefsrc)
		return res->fi->fib_prefsrc;

	if (nhc->nhc_family == AF_INET) {
		struct fib_nh *nh;

		nh = container_of(nhc, struct fib_nh, nh_common);
		if (nh->nh_saddr_genid == atomic_read(&net->ipv4.dev_addr_genid))
			return nh->nh_saddr;
	}

	return fib_info_update_nhc_saddr(net, nhc, res->fi->fib_scope);
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
	struct nexthop *nh = NULL;
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

	if (cfg->fc_nh_id) {
		if (!cfg->fc_mx) {
			fi = fib_find_info_nh(net, cfg);
			if (fi) {
				fi->fib_treeref++;
				return fi;
			}
		}

		nh = nexthop_find_by_id(net, cfg->fc_nh_id);
		if (!nh) {
			NL_SET_ERR_MSG(extack, "Nexthop id does not exist");
			goto err_inval;
		}
		nhs = 0;
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
	if (IS_ERR(fi->fib_metrics)) {
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
	if (nh) {
		if (!nexthop_get(nh)) {
			NL_SET_ERR_MSG(extack, "Nexthop has been deleted");
			err = -EINVAL;
		} else {
			err = 0;
			fi->nh = nh;
		}
	} else {
		change_nexthops(fi) {
			nexthop_nh->nh_parent = fi;
		} endfor_nexthops(fi)

		if (cfg->fc_mp)
			err = fib_get_nhs(fi, cfg->fc_mp, cfg->fc_mp_len, cfg,
					  extack);
		else
			err = fib_nh_init(net, fi->fib_nh, cfg, 1, extack);
	}

	if (err != 0)
		goto failure;

	if (fib_props[cfg->fc_type].error) {
		if (cfg->fc_gw_family || cfg->fc_oif || cfg->fc_mp) {
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

	if (fi->nh) {
		err = fib_check_nexthop(fi->nh, cfg->fc_scope, extack);
		if (err)
			goto failure;
	} else if (cfg->fc_scope == RT_SCOPE_HOST) {
		struct fib_nh *nh = fi->fib_nh;

		/* Local address is added. */
		if (nhs != 1) {
			NL_SET_ERR_MSG(extack,
				       "Route with host scope can not have multiple nexthops");
			goto err_inval;
		}
		if (nh->fib_nh_gw_family) {
			NL_SET_ERR_MSG(extack,
				       "Route with host scope can not have a gateway");
			goto err_inval;
		}
		nh->fib_nh_scope = RT_SCOPE_NOWHERE;
		nh->fib_nh_dev = dev_get_by_index(net, nh->fib_nh_oif);
		err = -ENODEV;
		if (!nh->fib_nh_dev)
			goto failure;
	} else {
		int linkdown = 0;

		change_nexthops(fi) {
			err = fib_check_nh(cfg->fc_nlinfo.nl_net, nexthop_nh,
					   cfg->fc_table, cfg->fc_scope,
					   extack);
			if (err != 0)
				goto failure;
			if (nexthop_nh->fib_nh_flags & RTNH_F_LINKDOWN)
				linkdown++;
		} endfor_nexthops(fi)
		if (linkdown == fi->fib_nhs)
			fi->fib_flags |= RTNH_F_LINKDOWN;
	}

	if (fi->fib_prefsrc && !fib_valid_prefsrc(cfg, fi->fib_prefsrc)) {
		NL_SET_ERR_MSG(extack, "Invalid prefsrc address");
		goto err_inval;
	}

	if (!fi->nh) {
		change_nexthops(fi) {
			fib_info_update_nhc_saddr(net, &nexthop_nh->nh_common,
						  fi->fib_scope);
			if (nexthop_nh->fib_nh_gw_family == AF_INET6)
				fi->fib_nh_is_v6 = true;
		} endfor_nexthops(fi)

		fib_rebalance(fi);
	}

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
	if (fi->nh) {
		list_add(&fi->nh_list, &nh->fi_list);
	} else {
		change_nexthops(fi) {
			struct hlist_head *head;
			unsigned int hash;

			if (!nexthop_nh->fib_nh_dev)
				continue;
			hash = fib_devindex_hashfn(nexthop_nh->fib_nh_dev->ifindex);
			head = &fib_info_devhash[hash];
			hlist_add_head(&nexthop_nh->nh_hash, head);
		} endfor_nexthops(fi)
	}
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

int fib_nexthop_info(struct sk_buff *skb, const struct fib_nh_common *nhc,
		     u8 rt_family, unsigned char *flags, bool skip_oif)
{
	if (nhc->nhc_flags & RTNH_F_DEAD)
		*flags |= RTNH_F_DEAD;

	if (nhc->nhc_flags & RTNH_F_LINKDOWN) {
		*flags |= RTNH_F_LINKDOWN;

		rcu_read_lock();
		switch (nhc->nhc_family) {
		case AF_INET:
			if (ip_ignore_linkdown(nhc->nhc_dev))
				*flags |= RTNH_F_DEAD;
			break;
		case AF_INET6:
			if (ip6_ignore_linkdown(nhc->nhc_dev))
				*flags |= RTNH_F_DEAD;
			break;
		}
		rcu_read_unlock();
	}

	switch (nhc->nhc_gw_family) {
	case AF_INET:
		if (nla_put_in_addr(skb, RTA_GATEWAY, nhc->nhc_gw.ipv4))
			goto nla_put_failure;
		break;
	case AF_INET6:
		/* if gateway family does not match nexthop family
		 * gateway is encoded as RTA_VIA
		 */
		if (rt_family != nhc->nhc_gw_family) {
			int alen = sizeof(struct in6_addr);
			struct nlattr *nla;
			struct rtvia *via;

			nla = nla_reserve(skb, RTA_VIA, alen + 2);
			if (!nla)
				goto nla_put_failure;

			via = nla_data(nla);
			via->rtvia_family = AF_INET6;
			memcpy(via->rtvia_addr, &nhc->nhc_gw.ipv6, alen);
		} else if (nla_put_in6_addr(skb, RTA_GATEWAY,
					    &nhc->nhc_gw.ipv6) < 0) {
			goto nla_put_failure;
		}
		break;
	}

	*flags |= (nhc->nhc_flags & RTNH_F_ONLINK);
	if (nhc->nhc_flags & RTNH_F_OFFLOAD)
		*flags |= RTNH_F_OFFLOAD;

	if (!skip_oif && nhc->nhc_dev &&
	    nla_put_u32(skb, RTA_OIF, nhc->nhc_dev->ifindex))
		goto nla_put_failure;

	if (nhc->nhc_lwtstate &&
	    lwtunnel_fill_encap(skb, nhc->nhc_lwtstate,
				RTA_ENCAP, RTA_ENCAP_TYPE) < 0)
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}
EXPORT_SYMBOL_GPL(fib_nexthop_info);

#if IS_ENABLED(CONFIG_IP_ROUTE_MULTIPATH) || IS_ENABLED(CONFIG_IPV6)
int fib_add_nexthop(struct sk_buff *skb, const struct fib_nh_common *nhc,
		    int nh_weight, u8 rt_family, u32 nh_tclassid)
{
	const struct net_device *dev = nhc->nhc_dev;
	struct rtnexthop *rtnh;
	unsigned char flags = 0;

	rtnh = nla_reserve_nohdr(skb, sizeof(*rtnh));
	if (!rtnh)
		goto nla_put_failure;

	rtnh->rtnh_hops = nh_weight - 1;
	rtnh->rtnh_ifindex = dev ? dev->ifindex : 0;

	if (fib_nexthop_info(skb, nhc, rt_family, &flags, true) < 0)
		goto nla_put_failure;

	rtnh->rtnh_flags = flags;

	if (nh_tclassid && nla_put_u32(skb, RTA_FLOW, nh_tclassid))
		goto nla_put_failure;

	/* length of rtnetlink header + attributes */
	rtnh->rtnh_len = nlmsg_get_pos(skb) - (void *)rtnh;

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}
EXPORT_SYMBOL_GPL(fib_add_nexthop);
#endif

#ifdef CONFIG_IP_ROUTE_MULTIPATH
static int fib_add_multipath(struct sk_buff *skb, struct fib_info *fi)
{
	struct nlattr *mp;

	mp = nla_nest_start_noflag(skb, RTA_MULTIPATH);
	if (!mp)
		goto nla_put_failure;

	if (unlikely(fi->nh)) {
		if (nexthop_mpath_fill_node(skb, fi->nh, AF_INET) < 0)
			goto nla_put_failure;
		goto mp_end;
	}

	for_nexthops(fi) {
		u32 nh_tclassid = 0;
#ifdef CONFIG_IP_ROUTE_CLASSID
		nh_tclassid = nh->nh_tclassid;
#endif
		if (fib_add_nexthop(skb, &nh->nh_common, nh->fib_nh_weight,
				    AF_INET, nh_tclassid) < 0)
			goto nla_put_failure;
	} endfor_nexthops(fi);

mp_end:
	nla_nest_end(skb, mp);

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}
#else
static int fib_add_multipath(struct sk_buff *skb, struct fib_info *fi)
{
	return 0;
}
#endif

int fib_dump_info(struct sk_buff *skb, u32 portid, u32 seq, int event,
		  struct fib_rt_info *fri, unsigned int flags)
{
	unsigned int nhs = fib_info_num_path(fri->fi);
	struct fib_info *fi = fri->fi;
	u32 tb_id = fri->tb_id;
	struct nlmsghdr *nlh;
	struct rtmsg *rtm;

	nlh = nlmsg_put(skb, portid, seq, event, sizeof(*rtm), flags);
	if (!nlh)
		return -EMSGSIZE;

	rtm = nlmsg_data(nlh);
	rtm->rtm_family = AF_INET;
	rtm->rtm_dst_len = fri->dst_len;
	rtm->rtm_src_len = 0;
	rtm->rtm_tos = fri->tos;
	if (tb_id < 256)
		rtm->rtm_table = tb_id;
	else
		rtm->rtm_table = RT_TABLE_COMPAT;
	if (nla_put_u32(skb, RTA_TABLE, tb_id))
		goto nla_put_failure;
	rtm->rtm_type = fri->type;
	rtm->rtm_flags = fi->fib_flags;
	rtm->rtm_scope = fi->fib_scope;
	rtm->rtm_protocol = fi->fib_protocol;

	if (rtm->rtm_dst_len &&
	    nla_put_in_addr(skb, RTA_DST, fri->dst))
		goto nla_put_failure;
	if (fi->fib_priority &&
	    nla_put_u32(skb, RTA_PRIORITY, fi->fib_priority))
		goto nla_put_failure;
	if (rtnetlink_put_metrics(skb, fi->fib_metrics->metrics) < 0)
		goto nla_put_failure;

	if (fi->fib_prefsrc &&
	    nla_put_in_addr(skb, RTA_PREFSRC, fi->fib_prefsrc))
		goto nla_put_failure;

	if (fi->nh) {
		if (nla_put_u32(skb, RTA_NH_ID, fi->nh->id))
			goto nla_put_failure;
		if (nexthop_is_blackhole(fi->nh))
			rtm->rtm_type = RTN_BLACKHOLE;
		if (!fi->fib_net->ipv4.sysctl_nexthop_compat_mode)
			goto offload;
	}

	if (nhs == 1) {
		const struct fib_nh_common *nhc = fib_info_nhc(fi, 0);
		unsigned char flags = 0;

		if (fib_nexthop_info(skb, nhc, AF_INET, &flags, false) < 0)
			goto nla_put_failure;

		rtm->rtm_flags = flags;
#ifdef CONFIG_IP_ROUTE_CLASSID
		if (nhc->nhc_family == AF_INET) {
			struct fib_nh *nh;

			nh = container_of(nhc, struct fib_nh, nh_common);
			if (nh->nh_tclassid &&
			    nla_put_u32(skb, RTA_FLOW, nh->nh_tclassid))
				goto nla_put_failure;
		}
#endif
	} else {
		if (fib_add_multipath(skb, fi) < 0)
			goto nla_put_failure;
	}

offload:
	if (fri->offload)
		rtm->rtm_flags |= RTM_F_OFFLOAD;
	if (fri->trap)
		rtm->rtm_flags |= RTM_F_TRAP;

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
	int tb_id = l3mdev_fib_table(dev) ? : RT_TABLE_MAIN;
	struct net *net = dev_net(dev);
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

static int call_fib_nh_notifiers(struct fib_nh *nh,
				 enum fib_event_type event_type)
{
	bool ignore_link_down = ip_ignore_linkdown(nh->fib_nh_dev);
	struct fib_nh_notifier_info info = {
		.fib_nh = nh,
	};

	switch (event_type) {
	case FIB_EVENT_NH_ADD:
		if (nh->fib_nh_flags & RTNH_F_DEAD)
			break;
		if (ignore_link_down && nh->fib_nh_flags & RTNH_F_LINKDOWN)
			break;
		return call_fib4_notifiers(dev_net(nh->fib_nh_dev), event_type,
					   &info.info);
	case FIB_EVENT_NH_DEL:
		if ((ignore_link_down && nh->fib_nh_flags & RTNH_F_LINKDOWN) ||
		    (nh->fib_nh_flags & RTNH_F_DEAD))
			return call_fib4_notifiers(dev_net(nh->fib_nh_dev),
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
void fib_nhc_update_mtu(struct fib_nh_common *nhc, u32 new, u32 orig)
{
	struct fnhe_hash_bucket *bucket;
	int i;

	bucket = rcu_dereference_protected(nhc->nhc_exceptions, 1);
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
		if (nh->fib_nh_dev == dev)
			fib_nhc_update_mtu(&nh->nh_common, dev->mtu, orig_mtu);
	}
}

/* Event              force Flags           Description
 * NETDEV_CHANGE      0     LINKDOWN        Carrier OFF, not for scope host
 * NETDEV_DOWN        0     LINKDOWN|DEAD   Link down, not for scope host
 * NETDEV_DOWN        1     LINKDOWN|DEAD   Last address removed
 * NETDEV_UNREGISTER  1     LINKDOWN|DEAD   Device removed
 *
 * only used when fib_nh is built into fib_info
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
		if (nh->fib_nh_dev != dev || fi == prev_fi)
			continue;
		prev_fi = fi;
		dead = 0;
		change_nexthops(fi) {
			if (nexthop_nh->fib_nh_flags & RTNH_F_DEAD)
				dead++;
			else if (nexthop_nh->fib_nh_dev == dev &&
				 nexthop_nh->fib_nh_scope != scope) {
				switch (event) {
				case NETDEV_DOWN:
				case NETDEV_UNREGISTER:
					nexthop_nh->fib_nh_flags |= RTNH_F_DEAD;
					fallthrough;
				case NETDEV_CHANGE:
					nexthop_nh->fib_nh_flags |= RTNH_F_LINKDOWN;
					break;
				}
				call_fib_nh_notifiers(nexthop_nh,
						      FIB_EVENT_NH_DEL);
				dead++;
			}
#ifdef CONFIG_IP_ROUTE_MULTIPATH
			if (event == NETDEV_UNREGISTER &&
			    nexthop_nh->fib_nh_dev == dev) {
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
				fallthrough;
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
		struct fib_nh_common *nhc;

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

		nhc = fib_info_nhc(next_fi, 0);
		if (!nhc->nhc_gw_family || nhc->nhc_scope != RT_SCOPE_LINK)
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
 *
 * only used when fib_nh is built into fib_info
 */
int fib_sync_up(struct net_device *dev, unsigned char nh_flags)
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
		if (nh->fib_nh_dev != dev || fi == prev_fi)
			continue;

		prev_fi = fi;
		alive = 0;
		change_nexthops(fi) {
			if (!(nexthop_nh->fib_nh_flags & nh_flags)) {
				alive++;
				continue;
			}
			if (!nexthop_nh->fib_nh_dev ||
			    !(nexthop_nh->fib_nh_dev->flags & IFF_UP))
				continue;
			if (nexthop_nh->fib_nh_dev != dev ||
			    !__in_dev_get_rtnl(dev))
				continue;
			alive++;
			nexthop_nh->fib_nh_flags &= ~nh_flags;
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

	if (nh->fib_nh_scope == RT_SCOPE_LINK) {
		struct neighbour *n;

		rcu_read_lock_bh();

		if (likely(nh->fib_nh_gw_family == AF_INET))
			n = __ipv4_neigh_lookup_noref(nh->fib_nh_dev,
						   (__force u32)nh->fib_nh_gw4);
		else if (nh->fib_nh_gw_family == AF_INET6)
			n = __ipv6_neigh_lookup_noref_stub(nh->fib_nh_dev,
							   &nh->fib_nh_gw6);
		else
			n = NULL;
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

	if (unlikely(res->fi->nh)) {
		nexthop_path_fib_result(res, hash);
		return;
	}

	change_nexthops(fi) {
		if (net->ipv4.sysctl_fib_multipath_use_neigh) {
			if (!fib_good_nh(nexthop_nh))
				continue;
			if (!first) {
				res->nh_sel = nhsel;
				res->nhc = &nexthop_nh->nh_common;
				first = true;
			}
		}

		if (hash > atomic_read(&nexthop_nh->fib_nh_upper_bound))
			continue;

		res->nh_sel = nhsel;
		res->nhc = &nexthop_nh->nh_common;
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
	if (fib_info_num_path(res->fi) > 1) {
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
		fl4->saddr = fib_result_prefsrc(net, res);
}
