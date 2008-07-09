/*
 *	Neighbour Discovery for IPv6
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *	Mike Shaver		<shaver@ingenia.com>
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

/*
 *	Changes:
 *
 *	Pierre Ynard			:	export userland ND options
 *						through netlink (RDNSS support)
 *	Lars Fenneberg			:	fixed MTU setting on receipt
 *						of an RA.
 *	Janos Farkas			:	kmalloc failure checks
 *	Alexey Kuznetsov		:	state machine reworked
 *						and moved to net/core.
 *	Pekka Savola			:	RFC2461 validation
 *	YOSHIFUJI Hideaki @USAGI	:	Verify ND options properly
 */

/* Set to 3 to get tracing... */
#define ND_DEBUG 1

#define ND_PRINTK(fmt, args...) do { if (net_ratelimit()) { printk(fmt, ## args); } } while(0)
#define ND_NOPRINTK(x...) do { ; } while(0)
#define ND_PRINTK0 ND_PRINTK
#define ND_PRINTK1 ND_NOPRINTK
#define ND_PRINTK2 ND_NOPRINTK
#define ND_PRINTK3 ND_NOPRINTK
#if ND_DEBUG >= 1
#undef ND_PRINTK1
#define ND_PRINTK1 ND_PRINTK
#endif
#if ND_DEBUG >= 2
#undef ND_PRINTK2
#define ND_PRINTK2 ND_PRINTK
#endif
#if ND_DEBUG >= 3
#undef ND_PRINTK3
#define ND_PRINTK3 ND_PRINTK
#endif

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/sched.h>
#include <linux/net.h>
#include <linux/in6.h>
#include <linux/route.h>
#include <linux/init.h>
#include <linux/rcupdate.h>
#ifdef CONFIG_SYSCTL
#include <linux/sysctl.h>
#endif

#include <linux/if_addr.h>
#include <linux/if_arp.h>
#include <linux/ipv6.h>
#include <linux/icmpv6.h>
#include <linux/jhash.h>

#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/protocol.h>
#include <net/ndisc.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>
#include <net/icmp.h>

#include <net/netlink.h>
#include <linux/rtnetlink.h>

#include <net/flow.h>
#include <net/ip6_checksum.h>
#include <net/inet_common.h>
#include <linux/proc_fs.h>

#include <linux/netfilter.h>
#include <linux/netfilter_ipv6.h>

static u32 ndisc_hash(const void *pkey, const struct net_device *dev);
static int ndisc_constructor(struct neighbour *neigh);
static void ndisc_solicit(struct neighbour *neigh, struct sk_buff *skb);
static void ndisc_error_report(struct neighbour *neigh, struct sk_buff *skb);
static int pndisc_constructor(struct pneigh_entry *n);
static void pndisc_destructor(struct pneigh_entry *n);
static void pndisc_redo(struct sk_buff *skb);

static struct neigh_ops ndisc_generic_ops = {
	.family =		AF_INET6,
	.solicit =		ndisc_solicit,
	.error_report =		ndisc_error_report,
	.output =		neigh_resolve_output,
	.connected_output =	neigh_connected_output,
	.hh_output =		dev_queue_xmit,
	.queue_xmit =		dev_queue_xmit,
};

static struct neigh_ops ndisc_hh_ops = {
	.family =		AF_INET6,
	.solicit =		ndisc_solicit,
	.error_report =		ndisc_error_report,
	.output =		neigh_resolve_output,
	.connected_output =	neigh_resolve_output,
	.hh_output =		dev_queue_xmit,
	.queue_xmit =		dev_queue_xmit,
};


static struct neigh_ops ndisc_direct_ops = {
	.family =		AF_INET6,
	.output =		dev_queue_xmit,
	.connected_output =	dev_queue_xmit,
	.hh_output =		dev_queue_xmit,
	.queue_xmit =		dev_queue_xmit,
};

struct neigh_table nd_tbl = {
	.family =	AF_INET6,
	.entry_size =	sizeof(struct neighbour) + sizeof(struct in6_addr),
	.key_len =	sizeof(struct in6_addr),
	.hash =		ndisc_hash,
	.constructor =	ndisc_constructor,
	.pconstructor =	pndisc_constructor,
	.pdestructor =	pndisc_destructor,
	.proxy_redo =	pndisc_redo,
	.id =		"ndisc_cache",
	.parms = {
		.tbl =			&nd_tbl,
		.base_reachable_time =	30 * HZ,
		.retrans_time =	 1 * HZ,
		.gc_staletime =	60 * HZ,
		.reachable_time =		30 * HZ,
		.delay_probe_time =	 5 * HZ,
		.queue_len =		 3,
		.ucast_probes =	 3,
		.mcast_probes =	 3,
		.anycast_delay =	 1 * HZ,
		.proxy_delay =		(8 * HZ) / 10,
		.proxy_qlen =		64,
	},
	.gc_interval =	  30 * HZ,
	.gc_thresh1 =	 128,
	.gc_thresh2 =	 512,
	.gc_thresh3 =	1024,
};

/* ND options */
struct ndisc_options {
	struct nd_opt_hdr *nd_opt_array[__ND_OPT_ARRAY_MAX];
#ifdef CONFIG_IPV6_ROUTE_INFO
	struct nd_opt_hdr *nd_opts_ri;
	struct nd_opt_hdr *nd_opts_ri_end;
#endif
	struct nd_opt_hdr *nd_useropts;
	struct nd_opt_hdr *nd_useropts_end;
};

#define nd_opts_src_lladdr	nd_opt_array[ND_OPT_SOURCE_LL_ADDR]
#define nd_opts_tgt_lladdr	nd_opt_array[ND_OPT_TARGET_LL_ADDR]
#define nd_opts_pi		nd_opt_array[ND_OPT_PREFIX_INFO]
#define nd_opts_pi_end		nd_opt_array[__ND_OPT_PREFIX_INFO_END]
#define nd_opts_rh		nd_opt_array[ND_OPT_REDIRECT_HDR]
#define nd_opts_mtu		nd_opt_array[ND_OPT_MTU]

#define NDISC_OPT_SPACE(len) (((len)+2+7)&~7)

/*
 * Return the padding between the option length and the start of the
 * link addr.  Currently only IP-over-InfiniBand needs this, although
 * if RFC 3831 IPv6-over-Fibre Channel is ever implemented it may
 * also need a pad of 2.
 */
static int ndisc_addr_option_pad(unsigned short type)
{
	switch (type) {
	case ARPHRD_INFINIBAND: return 2;
	default:                return 0;
	}
}

static inline int ndisc_opt_addr_space(struct net_device *dev)
{
	return NDISC_OPT_SPACE(dev->addr_len + ndisc_addr_option_pad(dev->type));
}

static u8 *ndisc_fill_addr_option(u8 *opt, int type, void *data, int data_len,
				  unsigned short addr_type)
{
	int space = NDISC_OPT_SPACE(data_len);
	int pad   = ndisc_addr_option_pad(addr_type);

	opt[0] = type;
	opt[1] = space>>3;

	memset(opt + 2, 0, pad);
	opt   += pad;
	space -= pad;

	memcpy(opt+2, data, data_len);
	data_len += 2;
	opt += data_len;
	if ((space -= data_len) > 0)
		memset(opt, 0, space);
	return opt + space;
}

static struct nd_opt_hdr *ndisc_next_option(struct nd_opt_hdr *cur,
					    struct nd_opt_hdr *end)
{
	int type;
	if (!cur || !end || cur >= end)
		return NULL;
	type = cur->nd_opt_type;
	do {
		cur = ((void *)cur) + (cur->nd_opt_len << 3);
	} while(cur < end && cur->nd_opt_type != type);
	return (cur <= end && cur->nd_opt_type == type ? cur : NULL);
}

static inline int ndisc_is_useropt(struct nd_opt_hdr *opt)
{
	return (opt->nd_opt_type == ND_OPT_RDNSS);
}

static struct nd_opt_hdr *ndisc_next_useropt(struct nd_opt_hdr *cur,
					     struct nd_opt_hdr *end)
{
	if (!cur || !end || cur >= end)
		return NULL;
	do {
		cur = ((void *)cur) + (cur->nd_opt_len << 3);
	} while(cur < end && !ndisc_is_useropt(cur));
	return (cur <= end && ndisc_is_useropt(cur) ? cur : NULL);
}

static struct ndisc_options *ndisc_parse_options(u8 *opt, int opt_len,
						 struct ndisc_options *ndopts)
{
	struct nd_opt_hdr *nd_opt = (struct nd_opt_hdr *)opt;

	if (!nd_opt || opt_len < 0 || !ndopts)
		return NULL;
	memset(ndopts, 0, sizeof(*ndopts));
	while (opt_len) {
		int l;
		if (opt_len < sizeof(struct nd_opt_hdr))
			return NULL;
		l = nd_opt->nd_opt_len << 3;
		if (opt_len < l || l == 0)
			return NULL;
		switch (nd_opt->nd_opt_type) {
		case ND_OPT_SOURCE_LL_ADDR:
		case ND_OPT_TARGET_LL_ADDR:
		case ND_OPT_MTU:
		case ND_OPT_REDIRECT_HDR:
			if (ndopts->nd_opt_array[nd_opt->nd_opt_type]) {
				ND_PRINTK2(KERN_WARNING
					   "%s(): duplicated ND6 option found: type=%d\n",
					   __func__,
					   nd_opt->nd_opt_type);
			} else {
				ndopts->nd_opt_array[nd_opt->nd_opt_type] = nd_opt;
			}
			break;
		case ND_OPT_PREFIX_INFO:
			ndopts->nd_opts_pi_end = nd_opt;
			if (!ndopts->nd_opt_array[nd_opt->nd_opt_type])
				ndopts->nd_opt_array[nd_opt->nd_opt_type] = nd_opt;
			break;
#ifdef CONFIG_IPV6_ROUTE_INFO
		case ND_OPT_ROUTE_INFO:
			ndopts->nd_opts_ri_end = nd_opt;
			if (!ndopts->nd_opts_ri)
				ndopts->nd_opts_ri = nd_opt;
			break;
#endif
		default:
			if (ndisc_is_useropt(nd_opt)) {
				ndopts->nd_useropts_end = nd_opt;
				if (!ndopts->nd_useropts)
					ndopts->nd_useropts = nd_opt;
			} else {
				/*
				 * Unknown options must be silently ignored,
				 * to accommodate future extension to the
				 * protocol.
				 */
				ND_PRINTK2(KERN_NOTICE
					   "%s(): ignored unsupported option; type=%d, len=%d\n",
					   __func__,
					   nd_opt->nd_opt_type, nd_opt->nd_opt_len);
			}
		}
		opt_len -= l;
		nd_opt = ((void *)nd_opt) + l;
	}
	return ndopts;
}

static inline u8 *ndisc_opt_addr_data(struct nd_opt_hdr *p,
				      struct net_device *dev)
{
	u8 *lladdr = (u8 *)(p + 1);
	int lladdrlen = p->nd_opt_len << 3;
	int prepad = ndisc_addr_option_pad(dev->type);
	if (lladdrlen != NDISC_OPT_SPACE(dev->addr_len + prepad))
		return NULL;
	return (lladdr + prepad);
}

int ndisc_mc_map(struct in6_addr *addr, char *buf, struct net_device *dev, int dir)
{
	switch (dev->type) {
	case ARPHRD_ETHER:
	case ARPHRD_IEEE802:	/* Not sure. Check it later. --ANK */
	case ARPHRD_FDDI:
		ipv6_eth_mc_map(addr, buf);
		return 0;
	case ARPHRD_IEEE802_TR:
		ipv6_tr_mc_map(addr,buf);
		return 0;
	case ARPHRD_ARCNET:
		ipv6_arcnet_mc_map(addr, buf);
		return 0;
	case ARPHRD_INFINIBAND:
		ipv6_ib_mc_map(addr, dev->broadcast, buf);
		return 0;
	default:
		if (dir) {
			memcpy(buf, dev->broadcast, dev->addr_len);
			return 0;
		}
	}
	return -EINVAL;
}

EXPORT_SYMBOL(ndisc_mc_map);

static u32 ndisc_hash(const void *pkey, const struct net_device *dev)
{
	const u32 *p32 = pkey;
	u32 addr_hash, i;

	addr_hash = 0;
	for (i = 0; i < (sizeof(struct in6_addr) / sizeof(u32)); i++)
		addr_hash ^= *p32++;

	return jhash_2words(addr_hash, dev->ifindex, nd_tbl.hash_rnd);
}

static int ndisc_constructor(struct neighbour *neigh)
{
	struct in6_addr *addr = (struct in6_addr*)&neigh->primary_key;
	struct net_device *dev = neigh->dev;
	struct inet6_dev *in6_dev;
	struct neigh_parms *parms;
	int is_multicast = ipv6_addr_is_multicast(addr);

	rcu_read_lock();
	in6_dev = in6_dev_get(dev);
	if (in6_dev == NULL) {
		rcu_read_unlock();
		return -EINVAL;
	}

	parms = in6_dev->nd_parms;
	__neigh_parms_put(neigh->parms);
	neigh->parms = neigh_parms_clone(parms);
	rcu_read_unlock();

	neigh->type = is_multicast ? RTN_MULTICAST : RTN_UNICAST;
	if (!dev->header_ops) {
		neigh->nud_state = NUD_NOARP;
		neigh->ops = &ndisc_direct_ops;
		neigh->output = neigh->ops->queue_xmit;
	} else {
		if (is_multicast) {
			neigh->nud_state = NUD_NOARP;
			ndisc_mc_map(addr, neigh->ha, dev, 1);
		} else if (dev->flags&(IFF_NOARP|IFF_LOOPBACK)) {
			neigh->nud_state = NUD_NOARP;
			memcpy(neigh->ha, dev->dev_addr, dev->addr_len);
			if (dev->flags&IFF_LOOPBACK)
				neigh->type = RTN_LOCAL;
		} else if (dev->flags&IFF_POINTOPOINT) {
			neigh->nud_state = NUD_NOARP;
			memcpy(neigh->ha, dev->broadcast, dev->addr_len);
		}
		if (dev->header_ops->cache)
			neigh->ops = &ndisc_hh_ops;
		else
			neigh->ops = &ndisc_generic_ops;
		if (neigh->nud_state&NUD_VALID)
			neigh->output = neigh->ops->connected_output;
		else
			neigh->output = neigh->ops->output;
	}
	in6_dev_put(in6_dev);
	return 0;
}

static int pndisc_constructor(struct pneigh_entry *n)
{
	struct in6_addr *addr = (struct in6_addr*)&n->key;
	struct in6_addr maddr;
	struct net_device *dev = n->dev;

	if (dev == NULL || __in6_dev_get(dev) == NULL)
		return -EINVAL;
	addrconf_addr_solict_mult(addr, &maddr);
	ipv6_dev_mc_inc(dev, &maddr);
	return 0;
}

static void pndisc_destructor(struct pneigh_entry *n)
{
	struct in6_addr *addr = (struct in6_addr*)&n->key;
	struct in6_addr maddr;
	struct net_device *dev = n->dev;

	if (dev == NULL || __in6_dev_get(dev) == NULL)
		return;
	addrconf_addr_solict_mult(addr, &maddr);
	ipv6_dev_mc_dec(dev, &maddr);
}

/*
 *	Send a Neighbour Advertisement
 */
static void __ndisc_send(struct net_device *dev,
			 struct neighbour *neigh,
			 const struct in6_addr *daddr,
			 const struct in6_addr *saddr,
			 struct icmp6hdr *icmp6h, const struct in6_addr *target,
			 int llinfo)
{
	struct flowi fl;
	struct dst_entry *dst;
	struct net *net = dev_net(dev);
	struct sock *sk = net->ipv6.ndisc_sk;
	struct sk_buff *skb;
	struct icmp6hdr *hdr;
	struct inet6_dev *idev;
	int len;
	int err;
	u8 *opt, type;

	type = icmp6h->icmp6_type;

	icmpv6_flow_init(sk, &fl, type, saddr, daddr, dev->ifindex);

	dst = icmp6_dst_alloc(dev, neigh, daddr);
	if (!dst)
		return;

	err = xfrm_lookup(&dst, &fl, NULL, 0);
	if (err < 0)
		return;

	if (!dev->addr_len)
		llinfo = 0;

	len = sizeof(struct icmp6hdr) + (target ? sizeof(*target) : 0);
	if (llinfo)
		len += ndisc_opt_addr_space(dev);

	skb = sock_alloc_send_skb(sk,
				  (MAX_HEADER + sizeof(struct ipv6hdr) +
				   len + LL_ALLOCATED_SPACE(dev)),
				  1, &err);
	if (!skb) {
		ND_PRINTK0(KERN_ERR
			   "ICMPv6 ND: %s() failed to allocate an skb.\n",
			   __func__);
		dst_release(dst);
		return;
	}

	skb_reserve(skb, LL_RESERVED_SPACE(dev));
	ip6_nd_hdr(sk, skb, dev, saddr, daddr, IPPROTO_ICMPV6, len);

	skb->transport_header = skb->tail;
	skb_put(skb, len);

	hdr = (struct icmp6hdr *)skb_transport_header(skb);
	memcpy(hdr, icmp6h, sizeof(*hdr));

	opt = skb_transport_header(skb) + sizeof(struct icmp6hdr);
	if (target) {
		ipv6_addr_copy((struct in6_addr *)opt, target);
		opt += sizeof(*target);
	}

	if (llinfo)
		ndisc_fill_addr_option(opt, llinfo, dev->dev_addr,
				       dev->addr_len, dev->type);

	hdr->icmp6_cksum = csum_ipv6_magic(saddr, daddr, len,
					   IPPROTO_ICMPV6,
					   csum_partial((__u8 *) hdr,
							len, 0));

	skb->dst = dst;

	idev = in6_dev_get(dst->dev);
	IP6_INC_STATS(idev, IPSTATS_MIB_OUTREQUESTS);

	err = NF_HOOK(PF_INET6, NF_INET_LOCAL_OUT, skb, NULL, dst->dev,
		      dst_output);
	if (!err) {
		ICMP6MSGOUT_INC_STATS(idev, type);
		ICMP6_INC_STATS(idev, ICMP6_MIB_OUTMSGS);
	}

	if (likely(idev != NULL))
		in6_dev_put(idev);
}

static void ndisc_send_na(struct net_device *dev, struct neighbour *neigh,
			  const struct in6_addr *daddr,
			  const struct in6_addr *solicited_addr,
			  int router, int solicited, int override, int inc_opt)
{
	struct in6_addr tmpaddr;
	struct inet6_ifaddr *ifp;
	const struct in6_addr *src_addr;
	struct icmp6hdr icmp6h = {
		.icmp6_type = NDISC_NEIGHBOUR_ADVERTISEMENT,
	};

	/* for anycast or proxy, solicited_addr != src_addr */
	ifp = ipv6_get_ifaddr(dev_net(dev), solicited_addr, dev, 1);
	if (ifp) {
		src_addr = solicited_addr;
		if (ifp->flags & IFA_F_OPTIMISTIC)
			override = 0;
		in6_ifa_put(ifp);
	} else {
		if (ipv6_dev_get_saddr(dev, daddr,
				       inet6_sk(dev_net(dev)->ipv6.ndisc_sk)->srcprefs,
				       &tmpaddr))
			return;
		src_addr = &tmpaddr;
	}

	icmp6h.icmp6_router = router;
	icmp6h.icmp6_solicited = solicited;
	icmp6h.icmp6_override = override;

	__ndisc_send(dev, neigh, daddr, src_addr,
		     &icmp6h, solicited_addr,
		     inc_opt ? ND_OPT_TARGET_LL_ADDR : 0);
}

void ndisc_send_ns(struct net_device *dev, struct neighbour *neigh,
		   const struct in6_addr *solicit,
		   const struct in6_addr *daddr, const struct in6_addr *saddr)
{
	struct in6_addr addr_buf;
	struct icmp6hdr icmp6h = {
		.icmp6_type = NDISC_NEIGHBOUR_SOLICITATION,
	};

	if (saddr == NULL) {
		if (ipv6_get_lladdr(dev, &addr_buf,
				   (IFA_F_TENTATIVE|IFA_F_OPTIMISTIC)))
			return;
		saddr = &addr_buf;
	}

	__ndisc_send(dev, neigh, daddr, saddr,
		     &icmp6h, solicit,
		     !ipv6_addr_any(saddr) ? ND_OPT_SOURCE_LL_ADDR : 0);
}

void ndisc_send_rs(struct net_device *dev, const struct in6_addr *saddr,
		   const struct in6_addr *daddr)
{
	struct icmp6hdr icmp6h = {
		.icmp6_type = NDISC_ROUTER_SOLICITATION,
	};
	int send_sllao = dev->addr_len;

#ifdef CONFIG_IPV6_OPTIMISTIC_DAD
	/*
	 * According to section 2.2 of RFC 4429, we must not
	 * send router solicitations with a sllao from
	 * optimistic addresses, but we may send the solicitation
	 * if we don't include the sllao.  So here we check
	 * if our address is optimistic, and if so, we
	 * suppress the inclusion of the sllao.
	 */
	if (send_sllao) {
		struct inet6_ifaddr *ifp = ipv6_get_ifaddr(dev_net(dev), saddr,
							   dev, 1);
		if (ifp) {
			if (ifp->flags & IFA_F_OPTIMISTIC)  {
				send_sllao = 0;
			}
			in6_ifa_put(ifp);
		} else {
			send_sllao = 0;
		}
	}
#endif
	__ndisc_send(dev, NULL, daddr, saddr,
		     &icmp6h, NULL,
		     send_sllao ? ND_OPT_SOURCE_LL_ADDR : 0);
}


static void ndisc_error_report(struct neighbour *neigh, struct sk_buff *skb)
{
	/*
	 *	"The sender MUST return an ICMP
	 *	 destination unreachable"
	 */
	dst_link_failure(skb);
	kfree_skb(skb);
}

/* Called with locked neigh: either read or both */

static void ndisc_solicit(struct neighbour *neigh, struct sk_buff *skb)
{
	struct in6_addr *saddr = NULL;
	struct in6_addr mcaddr;
	struct net_device *dev = neigh->dev;
	struct in6_addr *target = (struct in6_addr *)&neigh->primary_key;
	int probes = atomic_read(&neigh->probes);

	if (skb && ipv6_chk_addr(dev_net(dev), &ipv6_hdr(skb)->saddr, dev, 1))
		saddr = &ipv6_hdr(skb)->saddr;

	if ((probes -= neigh->parms->ucast_probes) < 0) {
		if (!(neigh->nud_state & NUD_VALID)) {
			ND_PRINTK1(KERN_DEBUG
				   "%s(): trying to ucast probe in NUD_INVALID: "
				   NIP6_FMT "\n",
				   __func__,
				   NIP6(*target));
		}
		ndisc_send_ns(dev, neigh, target, target, saddr);
	} else if ((probes -= neigh->parms->app_probes) < 0) {
#ifdef CONFIG_ARPD
		neigh_app_ns(neigh);
#endif
	} else {
		addrconf_addr_solict_mult(target, &mcaddr);
		ndisc_send_ns(dev, NULL, target, &mcaddr, saddr);
	}
}

static int pndisc_is_router(const void *pkey,
			    struct net_device *dev)
{
	struct pneigh_entry *n;
	int ret = -1;

	read_lock_bh(&nd_tbl.lock);
	n = __pneigh_lookup(&nd_tbl, dev_net(dev), pkey, dev);
	if (n)
		ret = !!(n->flags & NTF_ROUTER);
	read_unlock_bh(&nd_tbl.lock);

	return ret;
}

static void ndisc_recv_ns(struct sk_buff *skb)
{
	struct nd_msg *msg = (struct nd_msg *)skb_transport_header(skb);
	struct in6_addr *saddr = &ipv6_hdr(skb)->saddr;
	struct in6_addr *daddr = &ipv6_hdr(skb)->daddr;
	u8 *lladdr = NULL;
	u32 ndoptlen = skb->tail - (skb->transport_header +
				    offsetof(struct nd_msg, opt));
	struct ndisc_options ndopts;
	struct net_device *dev = skb->dev;
	struct inet6_ifaddr *ifp;
	struct inet6_dev *idev = NULL;
	struct neighbour *neigh;
	int dad = ipv6_addr_any(saddr);
	int inc;
	int is_router = -1;

	if (ipv6_addr_is_multicast(&msg->target)) {
		ND_PRINTK2(KERN_WARNING
			   "ICMPv6 NS: multicast target address");
		return;
	}

	/*
	 * RFC2461 7.1.1:
	 * DAD has to be destined for solicited node multicast address.
	 */
	if (dad &&
	    !(daddr->s6_addr32[0] == htonl(0xff020000) &&
	      daddr->s6_addr32[1] == htonl(0x00000000) &&
	      daddr->s6_addr32[2] == htonl(0x00000001) &&
	      daddr->s6_addr [12] == 0xff )) {
		ND_PRINTK2(KERN_WARNING
			   "ICMPv6 NS: bad DAD packet (wrong destination)\n");
		return;
	}

	if (!ndisc_parse_options(msg->opt, ndoptlen, &ndopts)) {
		ND_PRINTK2(KERN_WARNING
			   "ICMPv6 NS: invalid ND options\n");
		return;
	}

	if (ndopts.nd_opts_src_lladdr) {
		lladdr = ndisc_opt_addr_data(ndopts.nd_opts_src_lladdr, dev);
		if (!lladdr) {
			ND_PRINTK2(KERN_WARNING
				   "ICMPv6 NS: invalid link-layer address length\n");
			return;
		}

		/* RFC2461 7.1.1:
		 *	If the IP source address is the unspecified address,
		 *	there MUST NOT be source link-layer address option
		 *	in the message.
		 */
		if (dad) {
			ND_PRINTK2(KERN_WARNING
				   "ICMPv6 NS: bad DAD packet (link-layer address option)\n");
			return;
		}
	}

	inc = ipv6_addr_is_multicast(daddr);

	ifp = ipv6_get_ifaddr(dev_net(dev), &msg->target, dev, 1);
	if (ifp) {

		if (ifp->flags & (IFA_F_TENTATIVE|IFA_F_OPTIMISTIC)) {
			if (dad) {
				if (dev->type == ARPHRD_IEEE802_TR) {
					const unsigned char *sadr;
					sadr = skb_mac_header(skb);
					if (((sadr[8] ^ dev->dev_addr[0]) & 0x7f) == 0 &&
					    sadr[9] == dev->dev_addr[1] &&
					    sadr[10] == dev->dev_addr[2] &&
					    sadr[11] == dev->dev_addr[3] &&
					    sadr[12] == dev->dev_addr[4] &&
					    sadr[13] == dev->dev_addr[5]) {
						/* looped-back to us */
						goto out;
					}
				}

				/*
				 * We are colliding with another node
				 * who is doing DAD
				 * so fail our DAD process
				 */
				addrconf_dad_failure(ifp);
				return;
			} else {
				/*
				 * This is not a dad solicitation.
				 * If we are an optimistic node,
				 * we should respond.
				 * Otherwise, we should ignore it.
				 */
				if (!(ifp->flags & IFA_F_OPTIMISTIC))
					goto out;
			}
		}

		idev = ifp->idev;
	} else {
		idev = in6_dev_get(dev);
		if (!idev) {
			/* XXX: count this drop? */
			return;
		}

		if (ipv6_chk_acast_addr(dev_net(dev), dev, &msg->target) ||
		    (idev->cnf.forwarding &&
		     (ipv6_devconf.proxy_ndp || idev->cnf.proxy_ndp) &&
		     (is_router = pndisc_is_router(&msg->target, dev)) >= 0)) {
			if (!(NEIGH_CB(skb)->flags & LOCALLY_ENQUEUED) &&
			    skb->pkt_type != PACKET_HOST &&
			    inc != 0 &&
			    idev->nd_parms->proxy_delay != 0) {
				/*
				 * for anycast or proxy,
				 * sender should delay its response
				 * by a random time between 0 and
				 * MAX_ANYCAST_DELAY_TIME seconds.
				 * (RFC2461) -- yoshfuji
				 */
				struct sk_buff *n = skb_clone(skb, GFP_ATOMIC);
				if (n)
					pneigh_enqueue(&nd_tbl, idev->nd_parms, n);
				goto out;
			}
		} else
			goto out;
	}

	if (is_router < 0)
		is_router = !!idev->cnf.forwarding;

	if (dad) {
		ndisc_send_na(dev, NULL, &in6addr_linklocal_allnodes, &msg->target,
			      is_router, 0, (ifp != NULL), 1);
		goto out;
	}

	if (inc)
		NEIGH_CACHE_STAT_INC(&nd_tbl, rcv_probes_mcast);
	else
		NEIGH_CACHE_STAT_INC(&nd_tbl, rcv_probes_ucast);

	/*
	 *	update / create cache entry
	 *	for the source address
	 */
	neigh = __neigh_lookup(&nd_tbl, saddr, dev,
			       !inc || lladdr || !dev->addr_len);
	if (neigh)
		neigh_update(neigh, lladdr, NUD_STALE,
			     NEIGH_UPDATE_F_WEAK_OVERRIDE|
			     NEIGH_UPDATE_F_OVERRIDE);
	if (neigh || !dev->header_ops) {
		ndisc_send_na(dev, neigh, saddr, &msg->target,
			      is_router,
			      1, (ifp != NULL && inc), inc);
		if (neigh)
			neigh_release(neigh);
	}

out:
	if (ifp)
		in6_ifa_put(ifp);
	else
		in6_dev_put(idev);

	return;
}

static void ndisc_recv_na(struct sk_buff *skb)
{
	struct nd_msg *msg = (struct nd_msg *)skb_transport_header(skb);
	struct in6_addr *saddr = &ipv6_hdr(skb)->saddr;
	struct in6_addr *daddr = &ipv6_hdr(skb)->daddr;
	u8 *lladdr = NULL;
	u32 ndoptlen = skb->tail - (skb->transport_header +
				    offsetof(struct nd_msg, opt));
	struct ndisc_options ndopts;
	struct net_device *dev = skb->dev;
	struct inet6_ifaddr *ifp;
	struct neighbour *neigh;

	if (skb->len < sizeof(struct nd_msg)) {
		ND_PRINTK2(KERN_WARNING
			   "ICMPv6 NA: packet too short\n");
		return;
	}

	if (ipv6_addr_is_multicast(&msg->target)) {
		ND_PRINTK2(KERN_WARNING
			   "ICMPv6 NA: target address is multicast.\n");
		return;
	}

	if (ipv6_addr_is_multicast(daddr) &&
	    msg->icmph.icmp6_solicited) {
		ND_PRINTK2(KERN_WARNING
			   "ICMPv6 NA: solicited NA is multicasted.\n");
		return;
	}

	if (!ndisc_parse_options(msg->opt, ndoptlen, &ndopts)) {
		ND_PRINTK2(KERN_WARNING
			   "ICMPv6 NS: invalid ND option\n");
		return;
	}
	if (ndopts.nd_opts_tgt_lladdr) {
		lladdr = ndisc_opt_addr_data(ndopts.nd_opts_tgt_lladdr, dev);
		if (!lladdr) {
			ND_PRINTK2(KERN_WARNING
				   "ICMPv6 NA: invalid link-layer address length\n");
			return;
		}
	}
	ifp = ipv6_get_ifaddr(dev_net(dev), &msg->target, dev, 1);
	if (ifp) {
		if (ifp->flags & IFA_F_TENTATIVE) {
			addrconf_dad_failure(ifp);
			return;
		}
		/* What should we make now? The advertisement
		   is invalid, but ndisc specs say nothing
		   about it. It could be misconfiguration, or
		   an smart proxy agent tries to help us :-)
		 */
		ND_PRINTK1(KERN_WARNING
			   "ICMPv6 NA: someone advertises our address on %s!\n",
			   ifp->idev->dev->name);
		in6_ifa_put(ifp);
		return;
	}
	neigh = neigh_lookup(&nd_tbl, &msg->target, dev);

	if (neigh) {
		u8 old_flags = neigh->flags;

		if (neigh->nud_state & NUD_FAILED)
			goto out;

		/*
		 * Don't update the neighbor cache entry on a proxy NA from
		 * ourselves because either the proxied node is off link or it
		 * has already sent a NA to us.
		 */
		if (lladdr && !memcmp(lladdr, dev->dev_addr, dev->addr_len) &&
		    ipv6_devconf.forwarding && ipv6_devconf.proxy_ndp &&
		    pneigh_lookup(&nd_tbl, dev_net(dev), &msg->target, dev, 0)) {
			/* XXX: idev->cnf.prixy_ndp */
			goto out;
		}

		neigh_update(neigh, lladdr,
			     msg->icmph.icmp6_solicited ? NUD_REACHABLE : NUD_STALE,
			     NEIGH_UPDATE_F_WEAK_OVERRIDE|
			     (msg->icmph.icmp6_override ? NEIGH_UPDATE_F_OVERRIDE : 0)|
			     NEIGH_UPDATE_F_OVERRIDE_ISROUTER|
			     (msg->icmph.icmp6_router ? NEIGH_UPDATE_F_ISROUTER : 0));

		if ((old_flags & ~neigh->flags) & NTF_ROUTER) {
			/*
			 * Change: router to host
			 */
			struct rt6_info *rt;
			rt = rt6_get_dflt_router(saddr, dev);
			if (rt)
				ip6_del_rt(rt);
		}

out:
		neigh_release(neigh);
	}
}

static void ndisc_recv_rs(struct sk_buff *skb)
{
	struct rs_msg *rs_msg = (struct rs_msg *)skb_transport_header(skb);
	unsigned long ndoptlen = skb->len - sizeof(*rs_msg);
	struct neighbour *neigh;
	struct inet6_dev *idev;
	struct in6_addr *saddr = &ipv6_hdr(skb)->saddr;
	struct ndisc_options ndopts;
	u8 *lladdr = NULL;

	if (skb->len < sizeof(*rs_msg))
		return;

	idev = in6_dev_get(skb->dev);
	if (!idev) {
		if (net_ratelimit())
			ND_PRINTK1("ICMP6 RS: can't find in6 device\n");
		return;
	}

	/* Don't accept RS if we're not in router mode */
	if (!idev->cnf.forwarding)
		goto out;

	/*
	 * Don't update NCE if src = ::;
	 * this implies that the source node has no ip address assigned yet.
	 */
	if (ipv6_addr_any(saddr))
		goto out;

	/* Parse ND options */
	if (!ndisc_parse_options(rs_msg->opt, ndoptlen, &ndopts)) {
		if (net_ratelimit())
			ND_PRINTK2("ICMP6 NS: invalid ND option, ignored\n");
		goto out;
	}

	if (ndopts.nd_opts_src_lladdr) {
		lladdr = ndisc_opt_addr_data(ndopts.nd_opts_src_lladdr,
					     skb->dev);
		if (!lladdr)
			goto out;
	}

	neigh = __neigh_lookup(&nd_tbl, saddr, skb->dev, 1);
	if (neigh) {
		neigh_update(neigh, lladdr, NUD_STALE,
			     NEIGH_UPDATE_F_WEAK_OVERRIDE|
			     NEIGH_UPDATE_F_OVERRIDE|
			     NEIGH_UPDATE_F_OVERRIDE_ISROUTER);
		neigh_release(neigh);
	}
out:
	in6_dev_put(idev);
}

static void ndisc_ra_useropt(struct sk_buff *ra, struct nd_opt_hdr *opt)
{
	struct icmp6hdr *icmp6h = (struct icmp6hdr *)skb_transport_header(ra);
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	struct nduseroptmsg *ndmsg;
	struct net *net = dev_net(ra->dev);
	int err;
	int base_size = NLMSG_ALIGN(sizeof(struct nduseroptmsg)
				    + (opt->nd_opt_len << 3));
	size_t msg_size = base_size + nla_total_size(sizeof(struct in6_addr));

	skb = nlmsg_new(msg_size, GFP_ATOMIC);
	if (skb == NULL) {
		err = -ENOBUFS;
		goto errout;
	}

	nlh = nlmsg_put(skb, 0, 0, RTM_NEWNDUSEROPT, base_size, 0);
	if (nlh == NULL) {
		goto nla_put_failure;
	}

	ndmsg = nlmsg_data(nlh);
	ndmsg->nduseropt_family = AF_INET6;
	ndmsg->nduseropt_ifindex = ra->dev->ifindex;
	ndmsg->nduseropt_icmp_type = icmp6h->icmp6_type;
	ndmsg->nduseropt_icmp_code = icmp6h->icmp6_code;
	ndmsg->nduseropt_opts_len = opt->nd_opt_len << 3;

	memcpy(ndmsg + 1, opt, opt->nd_opt_len << 3);

	NLA_PUT(skb, NDUSEROPT_SRCADDR, sizeof(struct in6_addr),
		&ipv6_hdr(ra)->saddr);
	nlmsg_end(skb, nlh);

	err = rtnl_notify(skb, net, 0, RTNLGRP_ND_USEROPT, NULL,
			  GFP_ATOMIC);
	if (err < 0)
		goto errout;

	return;

nla_put_failure:
	nlmsg_free(skb);
	err = -EMSGSIZE;
errout:
	rtnl_set_sk_err(net, RTNLGRP_ND_USEROPT, err);
}

static void ndisc_router_discovery(struct sk_buff *skb)
{
	struct ra_msg *ra_msg = (struct ra_msg *)skb_transport_header(skb);
	struct neighbour *neigh = NULL;
	struct inet6_dev *in6_dev;
	struct rt6_info *rt = NULL;
	int lifetime;
	struct ndisc_options ndopts;
	int optlen;
	unsigned int pref = 0;

	__u8 * opt = (__u8 *)(ra_msg + 1);

	optlen = (skb->tail - skb->transport_header) - sizeof(struct ra_msg);

	if (!(ipv6_addr_type(&ipv6_hdr(skb)->saddr) & IPV6_ADDR_LINKLOCAL)) {
		ND_PRINTK2(KERN_WARNING
			   "ICMPv6 RA: source address is not link-local.\n");
		return;
	}
	if (optlen < 0) {
		ND_PRINTK2(KERN_WARNING
			   "ICMPv6 RA: packet too short\n");
		return;
	}

#ifdef CONFIG_IPV6_NDISC_NODETYPE
	if (skb->ndisc_nodetype == NDISC_NODETYPE_HOST) {
		ND_PRINTK2(KERN_WARNING
			   "ICMPv6 RA: from host or unauthorized router\n");
		return;
	}
#endif

	/*
	 *	set the RA_RECV flag in the interface
	 */

	in6_dev = in6_dev_get(skb->dev);
	if (in6_dev == NULL) {
		ND_PRINTK0(KERN_ERR
			   "ICMPv6 RA: can't find inet6 device for %s.\n",
			   skb->dev->name);
		return;
	}
	if (in6_dev->cnf.forwarding || !in6_dev->cnf.accept_ra) {
		in6_dev_put(in6_dev);
		return;
	}

	if (!ndisc_parse_options(opt, optlen, &ndopts)) {
		in6_dev_put(in6_dev);
		ND_PRINTK2(KERN_WARNING
			   "ICMP6 RA: invalid ND options\n");
		return;
	}

#ifdef CONFIG_IPV6_NDISC_NODETYPE
	/* skip link-specific parameters from interior routers */
	if (skb->ndisc_nodetype == NDISC_NODETYPE_NODEFAULT)
		goto skip_linkparms;
#endif

	if (in6_dev->if_flags & IF_RS_SENT) {
		/*
		 *	flag that an RA was received after an RS was sent
		 *	out on this interface.
		 */
		in6_dev->if_flags |= IF_RA_RCVD;
	}

	/*
	 * Remember the managed/otherconf flags from most recently
	 * received RA message (RFC 2462) -- yoshfuji
	 */
	in6_dev->if_flags = (in6_dev->if_flags & ~(IF_RA_MANAGED |
				IF_RA_OTHERCONF)) |
				(ra_msg->icmph.icmp6_addrconf_managed ?
					IF_RA_MANAGED : 0) |
				(ra_msg->icmph.icmp6_addrconf_other ?
					IF_RA_OTHERCONF : 0);

	if (!in6_dev->cnf.accept_ra_defrtr)
		goto skip_defrtr;

	lifetime = ntohs(ra_msg->icmph.icmp6_rt_lifetime);

#ifdef CONFIG_IPV6_ROUTER_PREF
	pref = ra_msg->icmph.icmp6_router_pref;
	/* 10b is handled as if it were 00b (medium) */
	if (pref == ICMPV6_ROUTER_PREF_INVALID ||
	    !in6_dev->cnf.accept_ra_rtr_pref)
		pref = ICMPV6_ROUTER_PREF_MEDIUM;
#endif

	rt = rt6_get_dflt_router(&ipv6_hdr(skb)->saddr, skb->dev);

	if (rt)
		neigh = rt->rt6i_nexthop;

	if (rt && lifetime == 0) {
		neigh_clone(neigh);
		ip6_del_rt(rt);
		rt = NULL;
	}

	if (rt == NULL && lifetime) {
		ND_PRINTK3(KERN_DEBUG
			   "ICMPv6 RA: adding default router.\n");

		rt = rt6_add_dflt_router(&ipv6_hdr(skb)->saddr, skb->dev, pref);
		if (rt == NULL) {
			ND_PRINTK0(KERN_ERR
				   "ICMPv6 RA: %s() failed to add default route.\n",
				   __func__);
			in6_dev_put(in6_dev);
			return;
		}

		neigh = rt->rt6i_nexthop;
		if (neigh == NULL) {
			ND_PRINTK0(KERN_ERR
				   "ICMPv6 RA: %s() got default router without neighbour.\n",
				   __func__);
			dst_release(&rt->u.dst);
			in6_dev_put(in6_dev);
			return;
		}
		neigh->flags |= NTF_ROUTER;
	} else if (rt) {
		rt->rt6i_flags |= (rt->rt6i_flags & ~RTF_PREF_MASK) | RTF_PREF(pref);
	}

	if (rt)
		rt->rt6i_expires = jiffies + (HZ * lifetime);

	if (ra_msg->icmph.icmp6_hop_limit) {
		in6_dev->cnf.hop_limit = ra_msg->icmph.icmp6_hop_limit;
		if (rt)
			rt->u.dst.metrics[RTAX_HOPLIMIT-1] = ra_msg->icmph.icmp6_hop_limit;
	}

skip_defrtr:

	/*
	 *	Update Reachable Time and Retrans Timer
	 */

	if (in6_dev->nd_parms) {
		unsigned long rtime = ntohl(ra_msg->retrans_timer);

		if (rtime && rtime/1000 < MAX_SCHEDULE_TIMEOUT/HZ) {
			rtime = (rtime*HZ)/1000;
			if (rtime < HZ/10)
				rtime = HZ/10;
			in6_dev->nd_parms->retrans_time = rtime;
			in6_dev->tstamp = jiffies;
			inet6_ifinfo_notify(RTM_NEWLINK, in6_dev);
		}

		rtime = ntohl(ra_msg->reachable_time);
		if (rtime && rtime/1000 < MAX_SCHEDULE_TIMEOUT/(3*HZ)) {
			rtime = (rtime*HZ)/1000;

			if (rtime < HZ/10)
				rtime = HZ/10;

			if (rtime != in6_dev->nd_parms->base_reachable_time) {
				in6_dev->nd_parms->base_reachable_time = rtime;
				in6_dev->nd_parms->gc_staletime = 3 * rtime;
				in6_dev->nd_parms->reachable_time = neigh_rand_reach_time(rtime);
				in6_dev->tstamp = jiffies;
				inet6_ifinfo_notify(RTM_NEWLINK, in6_dev);
			}
		}
	}

#ifdef CONFIG_IPV6_NDISC_NODETYPE
skip_linkparms:
#endif

	/*
	 *	Process options.
	 */

	if (!neigh)
		neigh = __neigh_lookup(&nd_tbl, &ipv6_hdr(skb)->saddr,
				       skb->dev, 1);
	if (neigh) {
		u8 *lladdr = NULL;
		if (ndopts.nd_opts_src_lladdr) {
			lladdr = ndisc_opt_addr_data(ndopts.nd_opts_src_lladdr,
						     skb->dev);
			if (!lladdr) {
				ND_PRINTK2(KERN_WARNING
					   "ICMPv6 RA: invalid link-layer address length\n");
				goto out;
			}
		}
		neigh_update(neigh, lladdr, NUD_STALE,
			     NEIGH_UPDATE_F_WEAK_OVERRIDE|
			     NEIGH_UPDATE_F_OVERRIDE|
			     NEIGH_UPDATE_F_OVERRIDE_ISROUTER|
			     NEIGH_UPDATE_F_ISROUTER);
	}

#ifdef CONFIG_IPV6_ROUTE_INFO
	if (in6_dev->cnf.accept_ra_rtr_pref && ndopts.nd_opts_ri) {
		struct nd_opt_hdr *p;
		for (p = ndopts.nd_opts_ri;
		     p;
		     p = ndisc_next_option(p, ndopts.nd_opts_ri_end)) {
			struct route_info *ri = (struct route_info *)p;
#ifdef CONFIG_IPV6_NDISC_NODETYPE
			if (skb->ndisc_nodetype == NDISC_NODETYPE_NODEFAULT &&
			    ri->prefix_len == 0)
				continue;
#endif
			if (ri->prefix_len > in6_dev->cnf.accept_ra_rt_info_max_plen)
				continue;
			rt6_route_rcv(skb->dev, (u8*)p, (p->nd_opt_len) << 3,
				      &ipv6_hdr(skb)->saddr);
		}
	}
#endif

#ifdef CONFIG_IPV6_NDISC_NODETYPE
	/* skip link-specific ndopts from interior routers */
	if (skb->ndisc_nodetype == NDISC_NODETYPE_NODEFAULT)
		goto out;
#endif

	if (in6_dev->cnf.accept_ra_pinfo && ndopts.nd_opts_pi) {
		struct nd_opt_hdr *p;
		for (p = ndopts.nd_opts_pi;
		     p;
		     p = ndisc_next_option(p, ndopts.nd_opts_pi_end)) {
			addrconf_prefix_rcv(skb->dev, (u8*)p, (p->nd_opt_len) << 3);
		}
	}

	if (ndopts.nd_opts_mtu) {
		__be32 n;
		u32 mtu;

		memcpy(&n, ((u8*)(ndopts.nd_opts_mtu+1))+2, sizeof(mtu));
		mtu = ntohl(n);

		if (mtu < IPV6_MIN_MTU || mtu > skb->dev->mtu) {
			ND_PRINTK2(KERN_WARNING
				   "ICMPv6 RA: invalid mtu: %d\n",
				   mtu);
		} else if (in6_dev->cnf.mtu6 != mtu) {
			in6_dev->cnf.mtu6 = mtu;

			if (rt)
				rt->u.dst.metrics[RTAX_MTU-1] = mtu;

			rt6_mtu_change(skb->dev, mtu);
		}
	}

	if (ndopts.nd_useropts) {
		struct nd_opt_hdr *p;
		for (p = ndopts.nd_useropts;
		     p;
		     p = ndisc_next_useropt(p, ndopts.nd_useropts_end)) {
			ndisc_ra_useropt(skb, p);
		}
	}

	if (ndopts.nd_opts_tgt_lladdr || ndopts.nd_opts_rh) {
		ND_PRINTK2(KERN_WARNING
			   "ICMPv6 RA: invalid RA options");
	}
out:
	if (rt)
		dst_release(&rt->u.dst);
	else if (neigh)
		neigh_release(neigh);
	in6_dev_put(in6_dev);
}

static void ndisc_redirect_rcv(struct sk_buff *skb)
{
	struct inet6_dev *in6_dev;
	struct icmp6hdr *icmph;
	struct in6_addr *dest;
	struct in6_addr *target;	/* new first hop to destination */
	struct neighbour *neigh;
	int on_link = 0;
	struct ndisc_options ndopts;
	int optlen;
	u8 *lladdr = NULL;

#ifdef CONFIG_IPV6_NDISC_NODETYPE
	switch (skb->ndisc_nodetype) {
	case NDISC_NODETYPE_HOST:
	case NDISC_NODETYPE_NODEFAULT:
		ND_PRINTK2(KERN_WARNING
			   "ICMPv6 Redirect: from host or unauthorized router\n");
		return;
	}
#endif

	if (!(ipv6_addr_type(&ipv6_hdr(skb)->saddr) & IPV6_ADDR_LINKLOCAL)) {
		ND_PRINTK2(KERN_WARNING
			   "ICMPv6 Redirect: source address is not link-local.\n");
		return;
	}

	optlen = skb->tail - skb->transport_header;
	optlen -= sizeof(struct icmp6hdr) + 2 * sizeof(struct in6_addr);

	if (optlen < 0) {
		ND_PRINTK2(KERN_WARNING
			   "ICMPv6 Redirect: packet too short\n");
		return;
	}

	icmph = icmp6_hdr(skb);
	target = (struct in6_addr *) (icmph + 1);
	dest = target + 1;

	if (ipv6_addr_is_multicast(dest)) {
		ND_PRINTK2(KERN_WARNING
			   "ICMPv6 Redirect: destination address is multicast.\n");
		return;
	}

	if (ipv6_addr_equal(dest, target)) {
		on_link = 1;
	} else if (ipv6_addr_type(target) !=
		   (IPV6_ADDR_UNICAST|IPV6_ADDR_LINKLOCAL)) {
		ND_PRINTK2(KERN_WARNING
			   "ICMPv6 Redirect: target address is not link-local unicast.\n");
		return;
	}

	in6_dev = in6_dev_get(skb->dev);
	if (!in6_dev)
		return;
	if (in6_dev->cnf.forwarding || !in6_dev->cnf.accept_redirects) {
		in6_dev_put(in6_dev);
		return;
	}

	/* RFC2461 8.1:
	 *	The IP source address of the Redirect MUST be the same as the current
	 *	first-hop router for the specified ICMP Destination Address.
	 */

	if (!ndisc_parse_options((u8*)(dest + 1), optlen, &ndopts)) {
		ND_PRINTK2(KERN_WARNING
			   "ICMPv6 Redirect: invalid ND options\n");
		in6_dev_put(in6_dev);
		return;
	}
	if (ndopts.nd_opts_tgt_lladdr) {
		lladdr = ndisc_opt_addr_data(ndopts.nd_opts_tgt_lladdr,
					     skb->dev);
		if (!lladdr) {
			ND_PRINTK2(KERN_WARNING
				   "ICMPv6 Redirect: invalid link-layer address length\n");
			in6_dev_put(in6_dev);
			return;
		}
	}

	neigh = __neigh_lookup(&nd_tbl, target, skb->dev, 1);
	if (neigh) {
		rt6_redirect(dest, &ipv6_hdr(skb)->daddr,
			     &ipv6_hdr(skb)->saddr, neigh, lladdr,
			     on_link);
		neigh_release(neigh);
	}
	in6_dev_put(in6_dev);
}

void ndisc_send_redirect(struct sk_buff *skb, struct neighbour *neigh,
			 const struct in6_addr *target)
{
	struct net_device *dev = skb->dev;
	struct net *net = dev_net(dev);
	struct sock *sk = net->ipv6.ndisc_sk;
	int len = sizeof(struct icmp6hdr) + 2 * sizeof(struct in6_addr);
	struct sk_buff *buff;
	struct icmp6hdr *icmph;
	struct in6_addr saddr_buf;
	struct in6_addr *addrp;
	struct rt6_info *rt;
	struct dst_entry *dst;
	struct inet6_dev *idev;
	struct flowi fl;
	u8 *opt;
	int rd_len;
	int err;
	u8 ha_buf[MAX_ADDR_LEN], *ha = NULL;

	if (ipv6_get_lladdr(dev, &saddr_buf, IFA_F_TENTATIVE)) {
		ND_PRINTK2(KERN_WARNING
			   "ICMPv6 Redirect: no link-local address on %s\n",
			   dev->name);
		return;
	}

	if (!ipv6_addr_equal(&ipv6_hdr(skb)->daddr, target) &&
	    ipv6_addr_type(target) != (IPV6_ADDR_UNICAST|IPV6_ADDR_LINKLOCAL)) {
		ND_PRINTK2(KERN_WARNING
			"ICMPv6 Redirect: target address is not link-local unicast.\n");
		return;
	}

	icmpv6_flow_init(sk, &fl, NDISC_REDIRECT,
			 &saddr_buf, &ipv6_hdr(skb)->saddr, dev->ifindex);

	dst = ip6_route_output(net, NULL, &fl);
	if (dst == NULL)
		return;

	err = xfrm_lookup(&dst, &fl, NULL, 0);
	if (err)
		return;

	rt = (struct rt6_info *) dst;

	if (rt->rt6i_flags & RTF_GATEWAY) {
		ND_PRINTK2(KERN_WARNING
			   "ICMPv6 Redirect: destination is not a neighbour.\n");
		dst_release(dst);
		return;
	}
	if (!xrlim_allow(dst, 1*HZ)) {
		dst_release(dst);
		return;
	}

	if (dev->addr_len) {
		read_lock_bh(&neigh->lock);
		if (neigh->nud_state & NUD_VALID) {
			memcpy(ha_buf, neigh->ha, dev->addr_len);
			read_unlock_bh(&neigh->lock);
			ha = ha_buf;
			len += ndisc_opt_addr_space(dev);
		} else
			read_unlock_bh(&neigh->lock);
	}

	rd_len = min_t(unsigned int,
		     IPV6_MIN_MTU-sizeof(struct ipv6hdr)-len, skb->len + 8);
	rd_len &= ~0x7;
	len += rd_len;

	buff = sock_alloc_send_skb(sk,
				   (MAX_HEADER + sizeof(struct ipv6hdr) +
				    len + LL_ALLOCATED_SPACE(dev)),
				   1, &err);
	if (buff == NULL) {
		ND_PRINTK0(KERN_ERR
			   "ICMPv6 Redirect: %s() failed to allocate an skb.\n",
			   __func__);
		dst_release(dst);
		return;
	}

	skb_reserve(buff, LL_RESERVED_SPACE(dev));
	ip6_nd_hdr(sk, buff, dev, &saddr_buf, &ipv6_hdr(skb)->saddr,
		   IPPROTO_ICMPV6, len);

	skb_set_transport_header(buff, skb_tail_pointer(buff) - buff->data);
	skb_put(buff, len);
	icmph = icmp6_hdr(buff);

	memset(icmph, 0, sizeof(struct icmp6hdr));
	icmph->icmp6_type = NDISC_REDIRECT;

	/*
	 *	copy target and destination addresses
	 */

	addrp = (struct in6_addr *)(icmph + 1);
	ipv6_addr_copy(addrp, target);
	addrp++;
	ipv6_addr_copy(addrp, &ipv6_hdr(skb)->daddr);

	opt = (u8*) (addrp + 1);

	/*
	 *	include target_address option
	 */

	if (ha)
		opt = ndisc_fill_addr_option(opt, ND_OPT_TARGET_LL_ADDR, ha,
					     dev->addr_len, dev->type);

	/*
	 *	build redirect option and copy skb over to the new packet.
	 */

	memset(opt, 0, 8);
	*(opt++) = ND_OPT_REDIRECT_HDR;
	*(opt++) = (rd_len >> 3);
	opt += 6;

	memcpy(opt, ipv6_hdr(skb), rd_len - 8);

	icmph->icmp6_cksum = csum_ipv6_magic(&saddr_buf, &ipv6_hdr(skb)->saddr,
					     len, IPPROTO_ICMPV6,
					     csum_partial((u8 *) icmph, len, 0));

	buff->dst = dst;
	idev = in6_dev_get(dst->dev);
	IP6_INC_STATS(idev, IPSTATS_MIB_OUTREQUESTS);
	err = NF_HOOK(PF_INET6, NF_INET_LOCAL_OUT, buff, NULL, dst->dev,
		      dst_output);
	if (!err) {
		ICMP6MSGOUT_INC_STATS(idev, NDISC_REDIRECT);
		ICMP6_INC_STATS(idev, ICMP6_MIB_OUTMSGS);
	}

	if (likely(idev != NULL))
		in6_dev_put(idev);
}

static void pndisc_redo(struct sk_buff *skb)
{
	ndisc_recv_ns(skb);
	kfree_skb(skb);
}

int ndisc_rcv(struct sk_buff *skb)
{
	struct nd_msg *msg;

	if (!pskb_may_pull(skb, skb->len))
		return 0;

	msg = (struct nd_msg *)skb_transport_header(skb);

	__skb_push(skb, skb->data - skb_transport_header(skb));

	if (ipv6_hdr(skb)->hop_limit != 255) {
		ND_PRINTK2(KERN_WARNING
			   "ICMPv6 NDISC: invalid hop-limit: %d\n",
			   ipv6_hdr(skb)->hop_limit);
		return 0;
	}

	if (msg->icmph.icmp6_code != 0) {
		ND_PRINTK2(KERN_WARNING
			   "ICMPv6 NDISC: invalid ICMPv6 code: %d\n",
			   msg->icmph.icmp6_code);
		return 0;
	}

	memset(NEIGH_CB(skb), 0, sizeof(struct neighbour_cb));

	switch (msg->icmph.icmp6_type) {
	case NDISC_NEIGHBOUR_SOLICITATION:
		ndisc_recv_ns(skb);
		break;

	case NDISC_NEIGHBOUR_ADVERTISEMENT:
		ndisc_recv_na(skb);
		break;

	case NDISC_ROUTER_SOLICITATION:
		ndisc_recv_rs(skb);
		break;

	case NDISC_ROUTER_ADVERTISEMENT:
		ndisc_router_discovery(skb);
		break;

	case NDISC_REDIRECT:
		ndisc_redirect_rcv(skb);
		break;
	}

	return 0;
}

static int ndisc_netdev_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct net_device *dev = ptr;
	struct net *net = dev_net(dev);

	switch (event) {
	case NETDEV_CHANGEADDR:
		neigh_changeaddr(&nd_tbl, dev);
		fib6_run_gc(~0UL, net);
		break;
	case NETDEV_DOWN:
		neigh_ifdown(&nd_tbl, dev);
		fib6_run_gc(~0UL, net);
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block ndisc_netdev_notifier = {
	.notifier_call = ndisc_netdev_event,
};

#ifdef CONFIG_SYSCTL
static void ndisc_warn_deprecated_sysctl(struct ctl_table *ctl,
					 const char *func, const char *dev_name)
{
	static char warncomm[TASK_COMM_LEN];
	static int warned;
	if (strcmp(warncomm, current->comm) && warned < 5) {
		strcpy(warncomm, current->comm);
		printk(KERN_WARNING
			"process `%s' is using deprecated sysctl (%s) "
			"net.ipv6.neigh.%s.%s; "
			"Use net.ipv6.neigh.%s.%s_ms "
			"instead.\n",
			warncomm, func,
			dev_name, ctl->procname,
			dev_name, ctl->procname);
		warned++;
	}
}

int ndisc_ifinfo_sysctl_change(struct ctl_table *ctl, int write, struct file * filp, void __user *buffer, size_t *lenp, loff_t *ppos)
{
	struct net_device *dev = ctl->extra1;
	struct inet6_dev *idev;
	int ret;

	if ((strcmp(ctl->procname, "retrans_time") == 0) ||
	    (strcmp(ctl->procname, "base_reachable_time") == 0))
		ndisc_warn_deprecated_sysctl(ctl, "syscall", dev ? dev->name : "default");

	if (strcmp(ctl->procname, "retrans_time") == 0)
		ret = proc_dointvec(ctl, write, filp, buffer, lenp, ppos);

	else if (strcmp(ctl->procname, "base_reachable_time") == 0)
		ret = proc_dointvec_jiffies(ctl, write,
					    filp, buffer, lenp, ppos);

	else if ((strcmp(ctl->procname, "retrans_time_ms") == 0) ||
		 (strcmp(ctl->procname, "base_reachable_time_ms") == 0))
		ret = proc_dointvec_ms_jiffies(ctl, write,
					       filp, buffer, lenp, ppos);
	else
		ret = -1;

	if (write && ret == 0 && dev && (idev = in6_dev_get(dev)) != NULL) {
		if (ctl->data == &idev->nd_parms->base_reachable_time)
			idev->nd_parms->reachable_time = neigh_rand_reach_time(idev->nd_parms->base_reachable_time);
		idev->tstamp = jiffies;
		inet6_ifinfo_notify(RTM_NEWLINK, idev);
		in6_dev_put(idev);
	}
	return ret;
}

int ndisc_ifinfo_sysctl_strategy(ctl_table *ctl, int __user *name,
				 int nlen, void __user *oldval,
				 size_t __user *oldlenp,
				 void __user *newval, size_t newlen)
{
	struct net_device *dev = ctl->extra1;
	struct inet6_dev *idev;
	int ret;

	if (ctl->ctl_name == NET_NEIGH_RETRANS_TIME ||
	    ctl->ctl_name == NET_NEIGH_REACHABLE_TIME)
		ndisc_warn_deprecated_sysctl(ctl, "procfs", dev ? dev->name : "default");

	switch (ctl->ctl_name) {
	case NET_NEIGH_REACHABLE_TIME:
		ret = sysctl_jiffies(ctl, name, nlen,
				     oldval, oldlenp, newval, newlen);
		break;
	case NET_NEIGH_RETRANS_TIME_MS:
	case NET_NEIGH_REACHABLE_TIME_MS:
		 ret = sysctl_ms_jiffies(ctl, name, nlen,
					 oldval, oldlenp, newval, newlen);
		 break;
	default:
		ret = 0;
	}

	if (newval && newlen && ret > 0 &&
	    dev && (idev = in6_dev_get(dev)) != NULL) {
		if (ctl->ctl_name == NET_NEIGH_REACHABLE_TIME ||
		    ctl->ctl_name == NET_NEIGH_REACHABLE_TIME_MS)
			idev->nd_parms->reachable_time = neigh_rand_reach_time(idev->nd_parms->base_reachable_time);
		idev->tstamp = jiffies;
		inet6_ifinfo_notify(RTM_NEWLINK, idev);
		in6_dev_put(idev);
	}

	return ret;
}

#endif

static int ndisc_net_init(struct net *net)
{
	struct ipv6_pinfo *np;
	struct sock *sk;
	int err;

	err = inet_ctl_sock_create(&sk, PF_INET6,
				   SOCK_RAW, IPPROTO_ICMPV6, net);
	if (err < 0) {
		ND_PRINTK0(KERN_ERR
			   "ICMPv6 NDISC: Failed to initialize the control socket (err %d).\n",
			   err);
		return err;
	}

	net->ipv6.ndisc_sk = sk;

	np = inet6_sk(sk);
	np->hop_limit = 255;
	/* Do not loopback ndisc messages */
	np->mc_loop = 0;

	return 0;
}

static void ndisc_net_exit(struct net *net)
{
	inet_ctl_sock_destroy(net->ipv6.ndisc_sk);
}

static struct pernet_operations ndisc_net_ops = {
	.init = ndisc_net_init,
	.exit = ndisc_net_exit,
};

int __init ndisc_init(void)
{
	int err;

	err = register_pernet_subsys(&ndisc_net_ops);
	if (err)
		return err;
	/*
	 * Initialize the neighbour table
	 */
	neigh_table_init(&nd_tbl);

#ifdef CONFIG_SYSCTL
	err = neigh_sysctl_register(NULL, &nd_tbl.parms, NET_IPV6,
				    NET_IPV6_NEIGH, "ipv6",
				    &ndisc_ifinfo_sysctl_change,
				    &ndisc_ifinfo_sysctl_strategy);
	if (err)
		goto out_unregister_pernet;
#endif
	err = register_netdevice_notifier(&ndisc_netdev_notifier);
	if (err)
		goto out_unregister_sysctl;
out:
	return err;

out_unregister_sysctl:
#ifdef CONFIG_SYSCTL
	neigh_sysctl_unregister(&nd_tbl.parms);
out_unregister_pernet:
#endif
	unregister_pernet_subsys(&ndisc_net_ops);
	goto out;
}

void ndisc_cleanup(void)
{
	unregister_netdevice_notifier(&ndisc_netdev_notifier);
#ifdef CONFIG_SYSCTL
	neigh_sysctl_unregister(&nd_tbl.parms);
#endif
	neigh_table_clear(&nd_tbl);
	unregister_pernet_subsys(&ndisc_net_ops);
}
