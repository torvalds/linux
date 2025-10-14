// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	Neighbour Discovery for IPv6
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *	Mike Shaver		<shaver@ingenia.com>
 */

/*
 *	Changes:
 *
 *	Alexey I. Froloff		:	RFC6106 (DNSSL) support
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

#define pr_fmt(fmt) "ICMPv6: " fmt

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
#include <linux/slab.h>
#ifdef CONFIG_SYSCTL
#include <linux/sysctl.h>
#endif

#include <linux/if_addr.h>
#include <linux/if_ether.h>
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

static u32 ndisc_hash(const void *pkey,
		      const struct net_device *dev,
		      __u32 *hash_rnd);
static bool ndisc_key_eq(const struct neighbour *neigh, const void *pkey);
static bool ndisc_allow_add(const struct net_device *dev,
			    struct netlink_ext_ack *extack);
static int ndisc_constructor(struct neighbour *neigh);
static void ndisc_solicit(struct neighbour *neigh, struct sk_buff *skb);
static void ndisc_error_report(struct neighbour *neigh, struct sk_buff *skb);
static int pndisc_constructor(struct pneigh_entry *n);
static void pndisc_destructor(struct pneigh_entry *n);
static void pndisc_redo(struct sk_buff *skb);
static int ndisc_is_multicast(const void *pkey);

static const struct neigh_ops ndisc_generic_ops = {
	.family =		AF_INET6,
	.solicit =		ndisc_solicit,
	.error_report =		ndisc_error_report,
	.output =		neigh_resolve_output,
	.connected_output =	neigh_connected_output,
};

static const struct neigh_ops ndisc_hh_ops = {
	.family =		AF_INET6,
	.solicit =		ndisc_solicit,
	.error_report =		ndisc_error_report,
	.output =		neigh_resolve_output,
	.connected_output =	neigh_resolve_output,
};


static const struct neigh_ops ndisc_direct_ops = {
	.family =		AF_INET6,
	.output =		neigh_direct_output,
	.connected_output =	neigh_direct_output,
};

struct neigh_table nd_tbl = {
	.family =	AF_INET6,
	.key_len =	sizeof(struct in6_addr),
	.protocol =	cpu_to_be16(ETH_P_IPV6),
	.hash =		ndisc_hash,
	.key_eq =	ndisc_key_eq,
	.constructor =	ndisc_constructor,
	.pconstructor =	pndisc_constructor,
	.pdestructor =	pndisc_destructor,
	.proxy_redo =	pndisc_redo,
	.is_multicast =	ndisc_is_multicast,
	.allow_add  =   ndisc_allow_add,
	.id =		"ndisc_cache",
	.parms = {
		.tbl			= &nd_tbl,
		.reachable_time		= ND_REACHABLE_TIME,
		.data = {
			[NEIGH_VAR_MCAST_PROBES] = 3,
			[NEIGH_VAR_UCAST_PROBES] = 3,
			[NEIGH_VAR_RETRANS_TIME] = ND_RETRANS_TIMER,
			[NEIGH_VAR_BASE_REACHABLE_TIME] = ND_REACHABLE_TIME,
			[NEIGH_VAR_DELAY_PROBE_TIME] = 5 * HZ,
			[NEIGH_VAR_INTERVAL_PROBE_TIME_MS] = 5 * HZ,
			[NEIGH_VAR_GC_STALETIME] = 60 * HZ,
			[NEIGH_VAR_QUEUE_LEN_BYTES] = SK_WMEM_DEFAULT,
			[NEIGH_VAR_PROXY_QLEN] = 64,
			[NEIGH_VAR_ANYCAST_DELAY] = 1 * HZ,
			[NEIGH_VAR_PROXY_DELAY] = (8 * HZ) / 10,
		},
	},
	.gc_interval =	  30 * HZ,
	.gc_thresh1 =	 128,
	.gc_thresh2 =	 512,
	.gc_thresh3 =	1024,
};
EXPORT_SYMBOL_GPL(nd_tbl);

void __ndisc_fill_addr_option(struct sk_buff *skb, int type, const void *data,
			      int data_len, int pad)
{
	int space = __ndisc_opt_addr_space(data_len, pad);
	u8 *opt = skb_put(skb, space);

	opt[0] = type;
	opt[1] = space>>3;

	memset(opt + 2, 0, pad);
	opt   += pad;
	space -= pad;

	memcpy(opt+2, data, data_len);
	data_len += 2;
	opt += data_len;
	space -= data_len;
	if (space > 0)
		memset(opt, 0, space);
}
EXPORT_SYMBOL_GPL(__ndisc_fill_addr_option);

static inline void ndisc_fill_addr_option(struct sk_buff *skb, int type,
					  const void *data, u8 icmp6_type)
{
	__ndisc_fill_addr_option(skb, type, data, skb->dev->addr_len,
				 ndisc_addr_option_pad(skb->dev->type));
	ndisc_ops_fill_addr_option(skb->dev, skb, icmp6_type);
}

static inline void ndisc_fill_redirect_addr_option(struct sk_buff *skb,
						   void *ha,
						   const u8 *ops_data)
{
	ndisc_fill_addr_option(skb, ND_OPT_TARGET_LL_ADDR, ha, NDISC_REDIRECT);
	ndisc_ops_fill_redirect_addr_option(skb->dev, skb, ops_data);
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
	} while (cur < end && cur->nd_opt_type != type);
	return cur <= end && cur->nd_opt_type == type ? cur : NULL;
}

static inline int ndisc_is_useropt(const struct net_device *dev,
				   struct nd_opt_hdr *opt)
{
	return opt->nd_opt_type == ND_OPT_PREFIX_INFO ||
		opt->nd_opt_type == ND_OPT_RDNSS ||
		opt->nd_opt_type == ND_OPT_DNSSL ||
		opt->nd_opt_type == ND_OPT_6CO ||
		opt->nd_opt_type == ND_OPT_CAPTIVE_PORTAL ||
		opt->nd_opt_type == ND_OPT_PREF64;
}

static struct nd_opt_hdr *ndisc_next_useropt(const struct net_device *dev,
					     struct nd_opt_hdr *cur,
					     struct nd_opt_hdr *end)
{
	if (!cur || !end || cur >= end)
		return NULL;
	do {
		cur = ((void *)cur) + (cur->nd_opt_len << 3);
	} while (cur < end && !ndisc_is_useropt(dev, cur));
	return cur <= end && ndisc_is_useropt(dev, cur) ? cur : NULL;
}

struct ndisc_options *ndisc_parse_options(const struct net_device *dev,
					  u8 *opt, int opt_len,
					  struct ndisc_options *ndopts)
{
	struct nd_opt_hdr *nd_opt = (struct nd_opt_hdr *)opt;

	if (!nd_opt || opt_len < 0 || !ndopts)
		return NULL;
	memset(ndopts, 0, sizeof(*ndopts));
	while (opt_len) {
		bool unknown = false;
		int l;
		if (opt_len < sizeof(struct nd_opt_hdr))
			return NULL;
		l = nd_opt->nd_opt_len << 3;
		if (opt_len < l || l == 0)
			return NULL;
		if (ndisc_ops_parse_options(dev, nd_opt, ndopts))
			goto next_opt;
		switch (nd_opt->nd_opt_type) {
		case ND_OPT_SOURCE_LL_ADDR:
		case ND_OPT_TARGET_LL_ADDR:
		case ND_OPT_MTU:
		case ND_OPT_NONCE:
		case ND_OPT_REDIRECT_HDR:
			if (ndopts->nd_opt_array[nd_opt->nd_opt_type]) {
				net_dbg_ratelimited("%s: duplicated ND6 option found: type=%d\n",
						    __func__, nd_opt->nd_opt_type);
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
			unknown = true;
		}
		if (ndisc_is_useropt(dev, nd_opt)) {
			ndopts->nd_useropts_end = nd_opt;
			if (!ndopts->nd_useropts)
				ndopts->nd_useropts = nd_opt;
		} else if (unknown) {
			/*
			 * Unknown options must be silently ignored,
			 * to accommodate future extension to the
			 * protocol.
			 */
			net_dbg_ratelimited("%s: ignored unsupported option; type=%d, len=%d\n",
					    __func__, nd_opt->nd_opt_type, nd_opt->nd_opt_len);
		}
next_opt:
		opt_len -= l;
		nd_opt = ((void *)nd_opt) + l;
	}
	return ndopts;
}

int ndisc_mc_map(const struct in6_addr *addr, char *buf, struct net_device *dev, int dir)
{
	switch (dev->type) {
	case ARPHRD_ETHER:
	case ARPHRD_IEEE802:	/* Not sure. Check it later. --ANK */
	case ARPHRD_FDDI:
		ipv6_eth_mc_map(addr, buf);
		return 0;
	case ARPHRD_ARCNET:
		ipv6_arcnet_mc_map(addr, buf);
		return 0;
	case ARPHRD_INFINIBAND:
		ipv6_ib_mc_map(addr, dev->broadcast, buf);
		return 0;
	case ARPHRD_IPGRE:
		return ipv6_ipgre_mc_map(addr, dev->broadcast, buf);
	default:
		if (dir) {
			memcpy(buf, dev->broadcast, dev->addr_len);
			return 0;
		}
	}
	return -EINVAL;
}
EXPORT_SYMBOL(ndisc_mc_map);

static u32 ndisc_hash(const void *pkey,
		      const struct net_device *dev,
		      __u32 *hash_rnd)
{
	return ndisc_hashfn(pkey, dev, hash_rnd);
}

static bool ndisc_key_eq(const struct neighbour *n, const void *pkey)
{
	return neigh_key_eq128(n, pkey);
}

static int ndisc_constructor(struct neighbour *neigh)
{
	struct in6_addr *addr = (struct in6_addr *)&neigh->primary_key;
	struct net_device *dev = neigh->dev;
	struct inet6_dev *in6_dev;
	struct neigh_parms *parms;
	bool is_multicast = ipv6_addr_is_multicast(addr);

	in6_dev = in6_dev_get(dev);
	if (!in6_dev) {
		return -EINVAL;
	}

	parms = in6_dev->nd_parms;
	__neigh_parms_put(neigh->parms);
	neigh->parms = neigh_parms_clone(parms);

	neigh->type = is_multicast ? RTN_MULTICAST : RTN_UNICAST;
	if (!dev->header_ops) {
		neigh->nud_state = NUD_NOARP;
		neigh->ops = &ndisc_direct_ops;
		neigh->output = neigh_direct_output;
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
	struct in6_addr *addr = (struct in6_addr *)&n->key;
	struct net_device *dev = n->dev;
	struct in6_addr maddr;

	if (!dev)
		return -EINVAL;

	addrconf_addr_solict_mult(addr, &maddr);
	return ipv6_dev_mc_inc(dev, &maddr);
}

static void pndisc_destructor(struct pneigh_entry *n)
{
	struct in6_addr *addr = (struct in6_addr *)&n->key;
	struct net_device *dev = n->dev;
	struct in6_addr maddr;

	if (!dev)
		return;

	addrconf_addr_solict_mult(addr, &maddr);
	ipv6_dev_mc_dec(dev, &maddr);
}

/* called with rtnl held */
static bool ndisc_allow_add(const struct net_device *dev,
			    struct netlink_ext_ack *extack)
{
	struct inet6_dev *idev = __in6_dev_get(dev);

	if (!idev || idev->cnf.disable_ipv6) {
		NL_SET_ERR_MSG(extack, "IPv6 is disabled on this device");
		return false;
	}

	return true;
}

static struct sk_buff *ndisc_alloc_skb(struct net_device *dev,
				       int len)
{
	int hlen = LL_RESERVED_SPACE(dev);
	int tlen = dev->needed_tailroom;
	struct sk_buff *skb;

	skb = alloc_skb(hlen + sizeof(struct ipv6hdr) + len + tlen, GFP_ATOMIC);
	if (!skb)
		return NULL;

	skb->protocol = htons(ETH_P_IPV6);
	skb->dev = dev;

	skb_reserve(skb, hlen + sizeof(struct ipv6hdr));
	skb_reset_transport_header(skb);

	/* Manually assign socket ownership as we avoid calling
	 * sock_alloc_send_pskb() to bypass wmem buffer limits
	 */
	rcu_read_lock();
	skb_set_owner_w(skb, dev_net_rcu(dev)->ipv6.ndisc_sk);
	rcu_read_unlock();

	return skb;
}

static void ip6_nd_hdr(struct sk_buff *skb,
		       const struct in6_addr *saddr,
		       const struct in6_addr *daddr,
		       int hop_limit, int len)
{
	struct ipv6hdr *hdr;
	struct inet6_dev *idev;
	unsigned tclass;

	rcu_read_lock();
	idev = __in6_dev_get(skb->dev);
	tclass = idev ? READ_ONCE(idev->cnf.ndisc_tclass) : 0;
	rcu_read_unlock();

	skb_push(skb, sizeof(*hdr));
	skb_reset_network_header(skb);
	hdr = ipv6_hdr(skb);

	ip6_flow_hdr(hdr, tclass, 0);

	hdr->payload_len = htons(len);
	hdr->nexthdr = IPPROTO_ICMPV6;
	hdr->hop_limit = hop_limit;

	hdr->saddr = *saddr;
	hdr->daddr = *daddr;
}

void ndisc_send_skb(struct sk_buff *skb, const struct in6_addr *daddr,
		    const struct in6_addr *saddr)
{
	struct icmp6hdr *icmp6h = icmp6_hdr(skb);
	struct dst_entry *dst = skb_dst(skb);
	struct net_device *dev;
	struct inet6_dev *idev;
	struct net *net;
	struct sock *sk;
	int err;
	u8 type;

	type = icmp6h->icmp6_type;

	rcu_read_lock();

	net = dev_net_rcu(skb->dev);
	sk = net->ipv6.ndisc_sk;
	if (!dst) {
		struct flowi6 fl6;
		int oif = skb->dev->ifindex;

		icmpv6_flow_init(sk, &fl6, type, saddr, daddr, oif);
		dst = icmp6_dst_alloc(skb->dev, &fl6);
		if (IS_ERR(dst)) {
			rcu_read_unlock();
			kfree_skb(skb);
			return;
		}

		skb_dst_set(skb, dst);
	}

	icmp6h->icmp6_cksum = csum_ipv6_magic(saddr, daddr, skb->len,
					      IPPROTO_ICMPV6,
					      csum_partial(icmp6h,
							   skb->len, 0));

	ip6_nd_hdr(skb, saddr, daddr, READ_ONCE(inet6_sk(sk)->hop_limit), skb->len);

	dev = dst_dev_rcu(dst);
	idev = __in6_dev_get(dev);
	IP6_INC_STATS(net, idev, IPSTATS_MIB_OUTREQUESTS);

	err = NF_HOOK(NFPROTO_IPV6, NF_INET_LOCAL_OUT,
		      net, sk, skb, NULL, dev,
		      dst_output);
	if (!err) {
		ICMP6MSGOUT_INC_STATS(net, idev, type);
		ICMP6_INC_STATS(net, idev, ICMP6_MIB_OUTMSGS);
	}

	rcu_read_unlock();
}
EXPORT_SYMBOL(ndisc_send_skb);

void ndisc_send_na(struct net_device *dev, const struct in6_addr *daddr,
		   const struct in6_addr *solicited_addr,
		   bool router, bool solicited, bool override, bool inc_opt)
{
	struct sk_buff *skb;
	struct in6_addr tmpaddr;
	struct inet6_ifaddr *ifp;
	const struct in6_addr *src_addr;
	struct nd_msg *msg;
	int optlen = 0;

	/* for anycast or proxy, solicited_addr != src_addr */
	ifp = ipv6_get_ifaddr(dev_net(dev), solicited_addr, dev, 1);
	if (ifp) {
		src_addr = solicited_addr;
		if (ifp->flags & IFA_F_OPTIMISTIC)
			override = false;
		inc_opt |= READ_ONCE(ifp->idev->cnf.force_tllao);
		in6_ifa_put(ifp);
	} else {
		if (ipv6_dev_get_saddr(dev_net(dev), dev, daddr,
				       inet6_sk(dev_net(dev)->ipv6.ndisc_sk)->srcprefs,
				       &tmpaddr))
			return;
		src_addr = &tmpaddr;
	}

	if (!dev->addr_len)
		inc_opt = false;
	if (inc_opt)
		optlen += ndisc_opt_addr_space(dev,
					       NDISC_NEIGHBOUR_ADVERTISEMENT);

	skb = ndisc_alloc_skb(dev, sizeof(*msg) + optlen);
	if (!skb)
		return;

	msg = skb_put(skb, sizeof(*msg));
	*msg = (struct nd_msg) {
		.icmph = {
			.icmp6_type = NDISC_NEIGHBOUR_ADVERTISEMENT,
			.icmp6_router = router,
			.icmp6_solicited = solicited,
			.icmp6_override = override,
		},
		.target = *solicited_addr,
	};

	if (inc_opt)
		ndisc_fill_addr_option(skb, ND_OPT_TARGET_LL_ADDR,
				       dev->dev_addr,
				       NDISC_NEIGHBOUR_ADVERTISEMENT);

	ndisc_send_skb(skb, daddr, src_addr);
}

static void ndisc_send_unsol_na(struct net_device *dev)
{
	struct inet6_dev *idev;
	struct inet6_ifaddr *ifa;

	idev = in6_dev_get(dev);
	if (!idev)
		return;

	read_lock_bh(&idev->lock);
	list_for_each_entry(ifa, &idev->addr_list, if_list) {
		/* skip tentative addresses until dad completes */
		if (ifa->flags & IFA_F_TENTATIVE &&
		    !(ifa->flags & IFA_F_OPTIMISTIC))
			continue;

		ndisc_send_na(dev, &in6addr_linklocal_allnodes, &ifa->addr,
			      /*router=*/ !!idev->cnf.forwarding,
			      /*solicited=*/ false, /*override=*/ true,
			      /*inc_opt=*/ true);
	}
	read_unlock_bh(&idev->lock);

	in6_dev_put(idev);
}

struct sk_buff *ndisc_ns_create(struct net_device *dev, const struct in6_addr *solicit,
				const struct in6_addr *saddr, u64 nonce)
{
	int inc_opt = dev->addr_len;
	struct sk_buff *skb;
	struct nd_msg *msg;
	int optlen = 0;

	if (!saddr)
		return NULL;

	if (ipv6_addr_any(saddr))
		inc_opt = false;
	if (inc_opt)
		optlen += ndisc_opt_addr_space(dev,
					       NDISC_NEIGHBOUR_SOLICITATION);
	if (nonce != 0)
		optlen += 8;

	skb = ndisc_alloc_skb(dev, sizeof(*msg) + optlen);
	if (!skb)
		return NULL;

	msg = skb_put(skb, sizeof(*msg));
	*msg = (struct nd_msg) {
		.icmph = {
			.icmp6_type = NDISC_NEIGHBOUR_SOLICITATION,
		},
		.target = *solicit,
	};

	if (inc_opt)
		ndisc_fill_addr_option(skb, ND_OPT_SOURCE_LL_ADDR,
				       dev->dev_addr,
				       NDISC_NEIGHBOUR_SOLICITATION);
	if (nonce != 0) {
		u8 *opt = skb_put(skb, 8);

		opt[0] = ND_OPT_NONCE;
		opt[1] = 8 >> 3;
		memcpy(opt + 2, &nonce, 6);
	}

	return skb;
}
EXPORT_SYMBOL(ndisc_ns_create);

void ndisc_send_ns(struct net_device *dev, const struct in6_addr *solicit,
		   const struct in6_addr *daddr, const struct in6_addr *saddr,
		   u64 nonce)
{
	struct in6_addr addr_buf;
	struct sk_buff *skb;

	if (!saddr) {
		if (ipv6_get_lladdr(dev, &addr_buf,
				    (IFA_F_TENTATIVE | IFA_F_OPTIMISTIC)))
			return;
		saddr = &addr_buf;
	}

	skb = ndisc_ns_create(dev, solicit, saddr, nonce);

	if (skb)
		ndisc_send_skb(skb, daddr, saddr);
}

void ndisc_send_rs(struct net_device *dev, const struct in6_addr *saddr,
		   const struct in6_addr *daddr)
{
	struct sk_buff *skb;
	struct rs_msg *msg;
	int send_sllao = dev->addr_len;
	int optlen = 0;

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
	if (send_sllao)
		optlen += ndisc_opt_addr_space(dev, NDISC_ROUTER_SOLICITATION);

	skb = ndisc_alloc_skb(dev, sizeof(*msg) + optlen);
	if (!skb)
		return;

	msg = skb_put(skb, sizeof(*msg));
	*msg = (struct rs_msg) {
		.icmph = {
			.icmp6_type = NDISC_ROUTER_SOLICITATION,
		},
	};

	if (send_sllao)
		ndisc_fill_addr_option(skb, ND_OPT_SOURCE_LL_ADDR,
				       dev->dev_addr,
				       NDISC_ROUTER_SOLICITATION);

	ndisc_send_skb(skb, daddr, saddr);
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

	if (skb && ipv6_chk_addr_and_flags(dev_net(dev), &ipv6_hdr(skb)->saddr,
					   dev, false, 1,
					   IFA_F_TENTATIVE|IFA_F_OPTIMISTIC))
		saddr = &ipv6_hdr(skb)->saddr;
	probes -= NEIGH_VAR(neigh->parms, UCAST_PROBES);
	if (probes < 0) {
		if (!(READ_ONCE(neigh->nud_state) & NUD_VALID)) {
			net_dbg_ratelimited("%s: trying to ucast probe in NUD_INVALID: %pI6\n",
					    __func__, target);
		}
		ndisc_send_ns(dev, target, target, saddr, 0);
	} else if ((probes -= NEIGH_VAR(neigh->parms, APP_PROBES)) < 0) {
		neigh_app_ns(neigh);
	} else {
		addrconf_addr_solict_mult(target, &mcaddr);
		ndisc_send_ns(dev, target, &mcaddr, saddr, 0);
	}
}

static int pndisc_is_router(const void *pkey,
			    struct net_device *dev)
{
	struct pneigh_entry *n;
	int ret = -1;

	n = pneigh_lookup(&nd_tbl, dev_net(dev), pkey, dev);
	if (n)
		ret = !!(READ_ONCE(n->flags) & NTF_ROUTER);

	return ret;
}

void ndisc_update(const struct net_device *dev, struct neighbour *neigh,
		  const u8 *lladdr, u8 new, u32 flags, u8 icmp6_type,
		  struct ndisc_options *ndopts)
{
	neigh_update(neigh, lladdr, new, flags, 0);
	/* report ndisc ops about neighbour update */
	ndisc_ops_update(dev, neigh, flags, icmp6_type, ndopts);
}

static enum skb_drop_reason ndisc_recv_ns(struct sk_buff *skb)
{
	struct nd_msg *msg = (struct nd_msg *)skb_transport_header(skb);
	const struct in6_addr *saddr = &ipv6_hdr(skb)->saddr;
	const struct in6_addr *daddr = &ipv6_hdr(skb)->daddr;
	u8 *lladdr = NULL;
	u32 ndoptlen = skb_tail_pointer(skb) - (skb_transport_header(skb) +
				    offsetof(struct nd_msg, opt));
	struct ndisc_options ndopts;
	struct net_device *dev = skb->dev;
	struct inet6_ifaddr *ifp;
	struct inet6_dev *idev = NULL;
	struct neighbour *neigh;
	int dad = ipv6_addr_any(saddr);
	int is_router = -1;
	SKB_DR(reason);
	u64 nonce = 0;
	bool inc;

	if (skb->len < sizeof(struct nd_msg))
		return SKB_DROP_REASON_PKT_TOO_SMALL;

	if (ipv6_addr_is_multicast(&msg->target)) {
		net_dbg_ratelimited("NS: multicast target address\n");
		return reason;
	}

	/*
	 * RFC2461 7.1.1:
	 * DAD has to be destined for solicited node multicast address.
	 */
	if (dad && !ipv6_addr_is_solict_mult(daddr)) {
		net_dbg_ratelimited("NS: bad DAD packet (wrong destination)\n");
		return reason;
	}

	if (!ndisc_parse_options(dev, msg->opt, ndoptlen, &ndopts))
		return SKB_DROP_REASON_IPV6_NDISC_BAD_OPTIONS;

	if (ndopts.nd_opts_src_lladdr) {
		lladdr = ndisc_opt_addr_data(ndopts.nd_opts_src_lladdr, dev);
		if (!lladdr) {
			net_dbg_ratelimited("NS: invalid link-layer address length\n");
			return reason;
		}

		/* RFC2461 7.1.1:
		 *	If the IP source address is the unspecified address,
		 *	there MUST NOT be source link-layer address option
		 *	in the message.
		 */
		if (dad) {
			net_dbg_ratelimited("NS: bad DAD packet (link-layer address option)\n");
			return reason;
		}
	}
	if (ndopts.nd_opts_nonce && ndopts.nd_opts_nonce->nd_opt_len == 1)
		memcpy(&nonce, (u8 *)(ndopts.nd_opts_nonce + 1), 6);

	inc = ipv6_addr_is_multicast(daddr);

	ifp = ipv6_get_ifaddr(dev_net(dev), &msg->target, dev, 1);
	if (ifp) {
have_ifp:
		if (ifp->flags & (IFA_F_TENTATIVE|IFA_F_OPTIMISTIC)) {
			if (dad) {
				if (nonce != 0 && ifp->dad_nonce == nonce) {
					u8 *np = (u8 *)&nonce;
					/* Matching nonce if looped back */
					net_dbg_ratelimited("%s: IPv6 DAD loopback for address %pI6c nonce %pM ignored\n",
							    ifp->idev->dev->name, &ifp->addr, np);
					goto out;
				}
				/*
				 * We are colliding with another node
				 * who is doing DAD
				 * so fail our DAD process
				 */
				addrconf_dad_failure(skb, ifp);
				return reason;
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
		struct net *net = dev_net(dev);

		/* perhaps an address on the master device */
		if (netif_is_l3_slave(dev)) {
			struct net_device *mdev;

			mdev = netdev_master_upper_dev_get_rcu(dev);
			if (mdev) {
				ifp = ipv6_get_ifaddr(net, &msg->target, mdev, 1);
				if (ifp)
					goto have_ifp;
			}
		}

		idev = in6_dev_get(dev);
		if (!idev) {
			/* XXX: count this drop? */
			return reason;
		}

		if (ipv6_chk_acast_addr(net, dev, &msg->target) ||
		    (READ_ONCE(idev->cnf.forwarding) &&
		     (READ_ONCE(net->ipv6.devconf_all->proxy_ndp) ||
		      READ_ONCE(idev->cnf.proxy_ndp)) &&
		     (is_router = pndisc_is_router(&msg->target, dev)) >= 0)) {
			if (!(NEIGH_CB(skb)->flags & LOCALLY_ENQUEUED) &&
			    skb->pkt_type != PACKET_HOST &&
			    inc &&
			    NEIGH_VAR(idev->nd_parms, PROXY_DELAY) != 0) {
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
		} else {
			SKB_DR_SET(reason, IPV6_NDISC_NS_OTHERHOST);
			goto out;
		}
	}

	if (is_router < 0)
		is_router = READ_ONCE(idev->cnf.forwarding);

	if (dad) {
		ndisc_send_na(dev, &in6addr_linklocal_allnodes, &msg->target,
			      !!is_router, false, (ifp != NULL), true);
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
		ndisc_update(dev, neigh, lladdr, NUD_STALE,
			     NEIGH_UPDATE_F_WEAK_OVERRIDE|
			     NEIGH_UPDATE_F_OVERRIDE,
			     NDISC_NEIGHBOUR_SOLICITATION, &ndopts);
	if (neigh || !dev->header_ops) {
		ndisc_send_na(dev, saddr, &msg->target, !!is_router,
			      true, (ifp != NULL && inc), inc);
		if (neigh)
			neigh_release(neigh);
		reason = SKB_CONSUMED;
	}

out:
	if (ifp)
		in6_ifa_put(ifp);
	else
		in6_dev_put(idev);
	return reason;
}

static int accept_untracked_na(struct net_device *dev, struct in6_addr *saddr)
{
	struct inet6_dev *idev = __in6_dev_get(dev);

	switch (READ_ONCE(idev->cnf.accept_untracked_na)) {
	case 0: /* Don't accept untracked na (absent in neighbor cache) */
		return 0;
	case 1: /* Create new entries from na if currently untracked */
		return 1;
	case 2: /* Create new entries from untracked na only if saddr is in the
		 * same subnet as an address configured on the interface that
		 * received the na
		 */
		return !!ipv6_chk_prefix(saddr, dev);
	default:
		return 0;
	}
}

static enum skb_drop_reason ndisc_recv_na(struct sk_buff *skb)
{
	struct nd_msg *msg = (struct nd_msg *)skb_transport_header(skb);
	struct in6_addr *saddr = &ipv6_hdr(skb)->saddr;
	const struct in6_addr *daddr = &ipv6_hdr(skb)->daddr;
	u8 *lladdr = NULL;
	u32 ndoptlen = skb_tail_pointer(skb) - (skb_transport_header(skb) +
				    offsetof(struct nd_msg, opt));
	struct ndisc_options ndopts;
	struct net_device *dev = skb->dev;
	struct inet6_dev *idev = __in6_dev_get(dev);
	struct inet6_ifaddr *ifp;
	struct neighbour *neigh;
	SKB_DR(reason);
	u8 new_state;

	if (skb->len < sizeof(struct nd_msg))
		return SKB_DROP_REASON_PKT_TOO_SMALL;

	if (ipv6_addr_is_multicast(&msg->target)) {
		net_dbg_ratelimited("NA: target address is multicast\n");
		return reason;
	}

	if (ipv6_addr_is_multicast(daddr) &&
	    msg->icmph.icmp6_solicited) {
		net_dbg_ratelimited("NA: solicited NA is multicasted\n");
		return reason;
	}

	/* For some 802.11 wireless deployments (and possibly other networks),
	 * there will be a NA proxy and unsolicitd packets are attacks
	 * and thus should not be accepted.
	 * drop_unsolicited_na takes precedence over accept_untracked_na
	 */
	if (!msg->icmph.icmp6_solicited && idev &&
	    READ_ONCE(idev->cnf.drop_unsolicited_na))
		return reason;

	if (!ndisc_parse_options(dev, msg->opt, ndoptlen, &ndopts))
		return SKB_DROP_REASON_IPV6_NDISC_BAD_OPTIONS;

	if (ndopts.nd_opts_tgt_lladdr) {
		lladdr = ndisc_opt_addr_data(ndopts.nd_opts_tgt_lladdr, dev);
		if (!lladdr) {
			net_dbg_ratelimited("NA: invalid link-layer address length\n");
			return reason;
		}
	}
	ifp = ipv6_get_ifaddr(dev_net(dev), &msg->target, dev, 1);
	if (ifp) {
		if (skb->pkt_type != PACKET_LOOPBACK
		    && (ifp->flags & IFA_F_TENTATIVE)) {
				addrconf_dad_failure(skb, ifp);
				return reason;
		}
		/* What should we make now? The advertisement
		   is invalid, but ndisc specs say nothing
		   about it. It could be misconfiguration, or
		   an smart proxy agent tries to help us :-)

		   We should not print the error if NA has been
		   received from loopback - it is just our own
		   unsolicited advertisement.
		 */
		if (skb->pkt_type != PACKET_LOOPBACK)
			net_warn_ratelimited("NA: %pM advertised our address %pI6c on %s!\n",
					     eth_hdr(skb)->h_source, &ifp->addr,
					     ifp->idev->dev->name);
		in6_ifa_put(ifp);
		return reason;
	}

	neigh = neigh_lookup(&nd_tbl, &msg->target, dev);

	/* RFC 9131 updates original Neighbour Discovery RFC 4861.
	 * NAs with Target LL Address option without a corresponding
	 * entry in the neighbour cache can now create a STALE neighbour
	 * cache entry on routers.
	 *
	 *   entry accept  fwding  solicited        behaviour
	 * ------- ------  ------  ---------    ----------------------
	 * present      X       X         0     Set state to STALE
	 * present      X       X         1     Set state to REACHABLE
	 *  absent      0       X         X     Do nothing
	 *  absent      1       0         X     Do nothing
	 *  absent      1       1         X     Add a new STALE entry
	 *
	 * Note that we don't do a (daddr == all-routers-mcast) check.
	 */
	new_state = msg->icmph.icmp6_solicited ? NUD_REACHABLE : NUD_STALE;
	if (!neigh && lladdr && idev && READ_ONCE(idev->cnf.forwarding)) {
		if (accept_untracked_na(dev, saddr)) {
			neigh = neigh_create(&nd_tbl, &msg->target, dev);
			new_state = NUD_STALE;
		}
	}

	if (neigh && !IS_ERR(neigh)) {
		u8 old_flags = neigh->flags;
		struct net *net = dev_net(dev);

		if (READ_ONCE(neigh->nud_state) & NUD_FAILED)
			goto out;

		/*
		 * Don't update the neighbor cache entry on a proxy NA from
		 * ourselves because either the proxied node is off link or it
		 * has already sent a NA to us.
		 */
		if (lladdr && !memcmp(lladdr, dev->dev_addr, dev->addr_len) &&
		    READ_ONCE(net->ipv6.devconf_all->forwarding) &&
		    READ_ONCE(net->ipv6.devconf_all->proxy_ndp) &&
		    pneigh_lookup(&nd_tbl, net, &msg->target, dev)) {
			/* XXX: idev->cnf.proxy_ndp */
			goto out;
		}

		ndisc_update(dev, neigh, lladdr,
			     new_state,
			     NEIGH_UPDATE_F_WEAK_OVERRIDE|
			     (msg->icmph.icmp6_override ? NEIGH_UPDATE_F_OVERRIDE : 0)|
			     NEIGH_UPDATE_F_OVERRIDE_ISROUTER|
			     (msg->icmph.icmp6_router ? NEIGH_UPDATE_F_ISROUTER : 0),
			     NDISC_NEIGHBOUR_ADVERTISEMENT, &ndopts);

		if ((old_flags & ~neigh->flags) & NTF_ROUTER) {
			/*
			 * Change: router to host
			 */
			rt6_clean_tohost(dev_net(dev),  saddr);
		}
		reason = SKB_CONSUMED;
out:
		neigh_release(neigh);
	}
	return reason;
}

static enum skb_drop_reason ndisc_recv_rs(struct sk_buff *skb)
{
	struct rs_msg *rs_msg = (struct rs_msg *)skb_transport_header(skb);
	unsigned long ndoptlen = skb->len - sizeof(*rs_msg);
	struct neighbour *neigh;
	struct inet6_dev *idev;
	const struct in6_addr *saddr = &ipv6_hdr(skb)->saddr;
	struct ndisc_options ndopts;
	u8 *lladdr = NULL;
	SKB_DR(reason);

	if (skb->len < sizeof(*rs_msg))
		return SKB_DROP_REASON_PKT_TOO_SMALL;

	idev = __in6_dev_get(skb->dev);
	if (!idev) {
		net_err_ratelimited("RS: can't find in6 device\n");
		return reason;
	}

	/* Don't accept RS if we're not in router mode */
	if (!READ_ONCE(idev->cnf.forwarding))
		goto out;

	/*
	 * Don't update NCE if src = ::;
	 * this implies that the source node has no ip address assigned yet.
	 */
	if (ipv6_addr_any(saddr))
		goto out;

	/* Parse ND options */
	if (!ndisc_parse_options(skb->dev, rs_msg->opt, ndoptlen, &ndopts))
		return SKB_DROP_REASON_IPV6_NDISC_BAD_OPTIONS;

	if (ndopts.nd_opts_src_lladdr) {
		lladdr = ndisc_opt_addr_data(ndopts.nd_opts_src_lladdr,
					     skb->dev);
		if (!lladdr)
			goto out;
	}

	neigh = __neigh_lookup(&nd_tbl, saddr, skb->dev, 1);
	if (neigh) {
		ndisc_update(skb->dev, neigh, lladdr, NUD_STALE,
			     NEIGH_UPDATE_F_WEAK_OVERRIDE|
			     NEIGH_UPDATE_F_OVERRIDE|
			     NEIGH_UPDATE_F_OVERRIDE_ISROUTER,
			     NDISC_ROUTER_SOLICITATION, &ndopts);
		neigh_release(neigh);
		reason = SKB_CONSUMED;
	}
out:
	return reason;
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
	if (!skb) {
		err = -ENOBUFS;
		goto errout;
	}

	nlh = nlmsg_put(skb, 0, 0, RTM_NEWNDUSEROPT, base_size, 0);
	if (!nlh) {
		goto nla_put_failure;
	}

	ndmsg = nlmsg_data(nlh);
	ndmsg->nduseropt_family = AF_INET6;
	ndmsg->nduseropt_ifindex = ra->dev->ifindex;
	ndmsg->nduseropt_icmp_type = icmp6h->icmp6_type;
	ndmsg->nduseropt_icmp_code = icmp6h->icmp6_code;
	ndmsg->nduseropt_opts_len = opt->nd_opt_len << 3;

	memcpy(ndmsg + 1, opt, opt->nd_opt_len << 3);

	if (nla_put_in6_addr(skb, NDUSEROPT_SRCADDR, &ipv6_hdr(ra)->saddr))
		goto nla_put_failure;
	nlmsg_end(skb, nlh);

	rtnl_notify(skb, net, 0, RTNLGRP_ND_USEROPT, NULL, GFP_ATOMIC);
	return;

nla_put_failure:
	nlmsg_free(skb);
	err = -EMSGSIZE;
errout:
	rtnl_set_sk_err(net, RTNLGRP_ND_USEROPT, err);
}

static enum skb_drop_reason ndisc_router_discovery(struct sk_buff *skb)
{
	struct ra_msg *ra_msg = (struct ra_msg *)skb_transport_header(skb);
	bool send_ifinfo_notify = false;
	struct neighbour *neigh = NULL;
	struct ndisc_options ndopts;
	struct fib6_info *rt = NULL;
	struct inet6_dev *in6_dev;
	struct fib6_table *table;
	u32 defrtr_usr_metric;
	unsigned int pref = 0;
	__u32 old_if_flags;
	struct net *net;
	SKB_DR(reason);
	int lifetime;
	int optlen;

	__u8 *opt = (__u8 *)(ra_msg + 1);

	optlen = (skb_tail_pointer(skb) - skb_transport_header(skb)) -
		sizeof(struct ra_msg);

	net_dbg_ratelimited("RA: %s, dev: %s\n", __func__, skb->dev->name);
	if (!(ipv6_addr_type(&ipv6_hdr(skb)->saddr) & IPV6_ADDR_LINKLOCAL)) {
		net_dbg_ratelimited("RA: source address is not link-local\n");
		return reason;
	}
	if (optlen < 0)
		return SKB_DROP_REASON_PKT_TOO_SMALL;

#ifdef CONFIG_IPV6_NDISC_NODETYPE
	if (skb->ndisc_nodetype == NDISC_NODETYPE_HOST) {
		net_dbg_ratelimited("RA: from host or unauthorized router\n");
		return reason;
	}
#endif

	in6_dev = __in6_dev_get(skb->dev);
	if (!in6_dev) {
		net_err_ratelimited("RA: can't find inet6 device for %s\n", skb->dev->name);
		return reason;
	}

	if (!ndisc_parse_options(skb->dev, opt, optlen, &ndopts))
		return SKB_DROP_REASON_IPV6_NDISC_BAD_OPTIONS;

	if (!ipv6_accept_ra(in6_dev)) {
		net_dbg_ratelimited("RA: %s, did not accept ra for dev: %s\n", __func__,
				    skb->dev->name);
		goto skip_linkparms;
	}

#ifdef CONFIG_IPV6_NDISC_NODETYPE
	/* skip link-specific parameters from interior routers */
	if (skb->ndisc_nodetype == NDISC_NODETYPE_NODEFAULT) {
		net_dbg_ratelimited("RA: %s, nodetype is NODEFAULT, dev: %s\n", __func__,
				    skb->dev->name);
		goto skip_linkparms;
	}
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
	old_if_flags = in6_dev->if_flags;
	in6_dev->if_flags = (in6_dev->if_flags & ~(IF_RA_MANAGED |
				IF_RA_OTHERCONF)) |
				(ra_msg->icmph.icmp6_addrconf_managed ?
					IF_RA_MANAGED : 0) |
				(ra_msg->icmph.icmp6_addrconf_other ?
					IF_RA_OTHERCONF : 0);

	if (old_if_flags != in6_dev->if_flags)
		send_ifinfo_notify = true;

	if (!READ_ONCE(in6_dev->cnf.accept_ra_defrtr)) {
		net_dbg_ratelimited("RA: %s, defrtr is false for dev: %s\n", __func__,
				    skb->dev->name);
		goto skip_defrtr;
	}

	lifetime = ntohs(ra_msg->icmph.icmp6_rt_lifetime);
	if (lifetime != 0 &&
	    lifetime < READ_ONCE(in6_dev->cnf.accept_ra_min_lft)) {
		net_dbg_ratelimited("RA: router lifetime (%ds) is too short: %s\n", lifetime,
				    skb->dev->name);
		goto skip_defrtr;
	}

	/* Do not accept RA with source-addr found on local machine unless
	 * accept_ra_from_local is set to true.
	 */
	net = dev_net(in6_dev->dev);
	if (!READ_ONCE(in6_dev->cnf.accept_ra_from_local) &&
	    ipv6_chk_addr(net, &ipv6_hdr(skb)->saddr, in6_dev->dev, 0)) {
		net_dbg_ratelimited("RA from local address detected on dev: %s: default router ignored\n",
				    skb->dev->name);
		goto skip_defrtr;
	}

#ifdef CONFIG_IPV6_ROUTER_PREF
	pref = ra_msg->icmph.icmp6_router_pref;
	/* 10b is handled as if it were 00b (medium) */
	if (pref == ICMPV6_ROUTER_PREF_INVALID ||
	    !READ_ONCE(in6_dev->cnf.accept_ra_rtr_pref))
		pref = ICMPV6_ROUTER_PREF_MEDIUM;
#endif
	/* routes added from RAs do not use nexthop objects */
	rt = rt6_get_dflt_router(net, &ipv6_hdr(skb)->saddr, skb->dev);
	if (rt) {
		neigh = ip6_neigh_lookup(&rt->fib6_nh->fib_nh_gw6,
					 rt->fib6_nh->fib_nh_dev, NULL,
					  &ipv6_hdr(skb)->saddr);
		if (!neigh) {
			net_err_ratelimited("RA: %s got default router without neighbour\n",
					    __func__);
			fib6_info_release(rt);
			return reason;
		}
	}
	/* Set default route metric as specified by user */
	defrtr_usr_metric = in6_dev->cnf.ra_defrtr_metric;
	/* delete the route if lifetime is 0 or if metric needs change */
	if (rt && (lifetime == 0 || rt->fib6_metric != defrtr_usr_metric)) {
		ip6_del_rt(net, rt, false);
		rt = NULL;
	}

	net_dbg_ratelimited("RA: rt: %p  lifetime: %d, metric: %d, for dev: %s\n", rt, lifetime,
			    defrtr_usr_metric, skb->dev->name);
	if (!rt && lifetime) {
		net_dbg_ratelimited("RA: adding default router\n");

		if (neigh)
			neigh_release(neigh);

		rt = rt6_add_dflt_router(net, &ipv6_hdr(skb)->saddr,
					 skb->dev, pref, defrtr_usr_metric,
					 lifetime);
		if (!rt) {
			net_err_ratelimited("RA: %s failed to add default route\n", __func__);
			return reason;
		}

		neigh = ip6_neigh_lookup(&rt->fib6_nh->fib_nh_gw6,
					 rt->fib6_nh->fib_nh_dev, NULL,
					  &ipv6_hdr(skb)->saddr);
		if (!neigh) {
			net_err_ratelimited("RA: %s got default router without neighbour\n",
					    __func__);
			fib6_info_release(rt);
			return reason;
		}
		neigh->flags |= NTF_ROUTER;
	} else if (rt && IPV6_EXTRACT_PREF(rt->fib6_flags) != pref) {
		struct nl_info nlinfo = {
			.nl_net = net,
		};
		rt->fib6_flags = (rt->fib6_flags & ~RTF_PREF_MASK) | RTF_PREF(pref);
		inet6_rt_notify(RTM_NEWROUTE, rt, &nlinfo, NLM_F_REPLACE);
	}

	if (rt) {
		table = rt->fib6_table;
		spin_lock_bh(&table->tb6_lock);

		fib6_set_expires(rt, jiffies + (HZ * lifetime));
		fib6_add_gc_list(rt);

		spin_unlock_bh(&table->tb6_lock);
	}
	if (READ_ONCE(in6_dev->cnf.accept_ra_min_hop_limit) < 256 &&
	    ra_msg->icmph.icmp6_hop_limit) {
		if (READ_ONCE(in6_dev->cnf.accept_ra_min_hop_limit) <=
		    ra_msg->icmph.icmp6_hop_limit) {
			WRITE_ONCE(in6_dev->cnf.hop_limit,
				   ra_msg->icmph.icmp6_hop_limit);
			fib6_metric_set(rt, RTAX_HOPLIMIT,
					ra_msg->icmph.icmp6_hop_limit);
		} else {
			net_dbg_ratelimited("RA: Got route advertisement with lower hop_limit than minimum\n");
		}
	}

skip_defrtr:

	/*
	 *	Update Reachable Time and Retrans Timer
	 */

	if (in6_dev->nd_parms) {
		unsigned long rtime = ntohl(ra_msg->retrans_timer);

		if (rtime && rtime/1000 < MAX_SCHEDULE_TIMEOUT/HZ) {
			rtime = (rtime*HZ)/1000;
			if (rtime < HZ/100)
				rtime = HZ/100;
			NEIGH_VAR_SET(in6_dev->nd_parms, RETRANS_TIME, rtime);
			in6_dev->tstamp = jiffies;
			send_ifinfo_notify = true;
		}

		rtime = ntohl(ra_msg->reachable_time);
		if (rtime && rtime/1000 < MAX_SCHEDULE_TIMEOUT/(3*HZ)) {
			rtime = (rtime*HZ)/1000;

			if (rtime < HZ/10)
				rtime = HZ/10;

			if (rtime != NEIGH_VAR(in6_dev->nd_parms, BASE_REACHABLE_TIME)) {
				NEIGH_VAR_SET(in6_dev->nd_parms,
					      BASE_REACHABLE_TIME, rtime);
				NEIGH_VAR_SET(in6_dev->nd_parms,
					      GC_STALETIME, 3 * rtime);
				in6_dev->nd_parms->reachable_time = neigh_rand_reach_time(rtime);
				in6_dev->tstamp = jiffies;
				send_ifinfo_notify = true;
			}
		}
	}

skip_linkparms:

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
				net_dbg_ratelimited("RA: invalid link-layer address length\n");
				goto out;
			}
		}
		ndisc_update(skb->dev, neigh, lladdr, NUD_STALE,
			     NEIGH_UPDATE_F_WEAK_OVERRIDE|
			     NEIGH_UPDATE_F_OVERRIDE|
			     NEIGH_UPDATE_F_OVERRIDE_ISROUTER|
			     NEIGH_UPDATE_F_ISROUTER,
			     NDISC_ROUTER_ADVERTISEMENT, &ndopts);
		reason = SKB_CONSUMED;
	}

	if (!ipv6_accept_ra(in6_dev)) {
		net_dbg_ratelimited("RA: %s, accept_ra is false for dev: %s\n", __func__,
				    skb->dev->name);
		goto out;
	}

#ifdef CONFIG_IPV6_ROUTE_INFO
	if (!READ_ONCE(in6_dev->cnf.accept_ra_from_local) &&
	    ipv6_chk_addr(dev_net(in6_dev->dev), &ipv6_hdr(skb)->saddr,
			  in6_dev->dev, 0)) {
		net_dbg_ratelimited("RA from local address detected on dev: %s: router info ignored.\n",
				    skb->dev->name);
		goto skip_routeinfo;
	}

	if (READ_ONCE(in6_dev->cnf.accept_ra_rtr_pref) && ndopts.nd_opts_ri) {
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
			if (ri->prefix_len == 0 &&
			    !READ_ONCE(in6_dev->cnf.accept_ra_defrtr))
				continue;
			if (ri->lifetime != 0 &&
			    ntohl(ri->lifetime) < READ_ONCE(in6_dev->cnf.accept_ra_min_lft))
				continue;
			if (ri->prefix_len < READ_ONCE(in6_dev->cnf.accept_ra_rt_info_min_plen))
				continue;
			if (ri->prefix_len > READ_ONCE(in6_dev->cnf.accept_ra_rt_info_max_plen))
				continue;
			rt6_route_rcv(skb->dev, (u8 *)p, (p->nd_opt_len) << 3,
				      &ipv6_hdr(skb)->saddr);
		}
	}

skip_routeinfo:
#endif

#ifdef CONFIG_IPV6_NDISC_NODETYPE
	/* skip link-specific ndopts from interior routers */
	if (skb->ndisc_nodetype == NDISC_NODETYPE_NODEFAULT) {
		net_dbg_ratelimited("RA: %s, nodetype is NODEFAULT (interior routes), dev: %s\n",
				    __func__, skb->dev->name);
		goto out;
	}
#endif

	if (READ_ONCE(in6_dev->cnf.accept_ra_pinfo) && ndopts.nd_opts_pi) {
		struct nd_opt_hdr *p;
		for (p = ndopts.nd_opts_pi;
		     p;
		     p = ndisc_next_option(p, ndopts.nd_opts_pi_end)) {
			addrconf_prefix_rcv(skb->dev, (u8 *)p,
					    (p->nd_opt_len) << 3,
					    ndopts.nd_opts_src_lladdr != NULL);
		}
	}

	if (ndopts.nd_opts_mtu && READ_ONCE(in6_dev->cnf.accept_ra_mtu)) {
		__be32 n;
		u32 mtu;

		memcpy(&n, ((u8 *)(ndopts.nd_opts_mtu+1))+2, sizeof(mtu));
		mtu = ntohl(n);

		if (in6_dev->ra_mtu != mtu) {
			in6_dev->ra_mtu = mtu;
			send_ifinfo_notify = true;
		}

		if (mtu < IPV6_MIN_MTU || mtu > skb->dev->mtu) {
			net_dbg_ratelimited("RA: invalid mtu: %d\n", mtu);
		} else if (READ_ONCE(in6_dev->cnf.mtu6) != mtu) {
			WRITE_ONCE(in6_dev->cnf.mtu6, mtu);
			fib6_metric_set(rt, RTAX_MTU, mtu);
			rt6_mtu_change(skb->dev, mtu);
		}
	}

	if (ndopts.nd_useropts) {
		struct nd_opt_hdr *p;
		for (p = ndopts.nd_useropts;
		     p;
		     p = ndisc_next_useropt(skb->dev, p,
					    ndopts.nd_useropts_end)) {
			ndisc_ra_useropt(skb, p);
		}
	}

	if (ndopts.nd_opts_tgt_lladdr || ndopts.nd_opts_rh) {
		net_dbg_ratelimited("RA: invalid RA options\n");
	}
out:
	/* Send a notify if RA changed managed/otherconf flags or
	 * timer settings or ra_mtu value
	 */
	if (send_ifinfo_notify)
		inet6_ifinfo_notify(RTM_NEWLINK, in6_dev);

	fib6_info_release(rt);
	if (neigh)
		neigh_release(neigh);
	return reason;
}

static enum skb_drop_reason ndisc_redirect_rcv(struct sk_buff *skb)
{
	struct rd_msg *msg = (struct rd_msg *)skb_transport_header(skb);
	u32 ndoptlen = skb_tail_pointer(skb) - (skb_transport_header(skb) +
				    offsetof(struct rd_msg, opt));
	struct ndisc_options ndopts;
	SKB_DR(reason);
	u8 *hdr;

#ifdef CONFIG_IPV6_NDISC_NODETYPE
	switch (skb->ndisc_nodetype) {
	case NDISC_NODETYPE_HOST:
	case NDISC_NODETYPE_NODEFAULT:
		net_dbg_ratelimited("Redirect: from host or unauthorized router\n");
		return reason;
	}
#endif

	if (!(ipv6_addr_type(&ipv6_hdr(skb)->saddr) & IPV6_ADDR_LINKLOCAL)) {
		net_dbg_ratelimited("Redirect: source address is not link-local\n");
		return reason;
	}

	if (!ndisc_parse_options(skb->dev, msg->opt, ndoptlen, &ndopts))
		return SKB_DROP_REASON_IPV6_NDISC_BAD_OPTIONS;

	if (!ndopts.nd_opts_rh) {
		ip6_redirect_no_header(skb, dev_net(skb->dev),
					skb->dev->ifindex);
		return reason;
	}

	hdr = (u8 *)ndopts.nd_opts_rh;
	hdr += 8;
	if (!pskb_pull(skb, hdr - skb_transport_header(skb)))
		return SKB_DROP_REASON_PKT_TOO_SMALL;

	return icmpv6_notify(skb, NDISC_REDIRECT, 0, 0);
}

static void ndisc_fill_redirect_hdr_option(struct sk_buff *skb,
					   struct sk_buff *orig_skb,
					   int rd_len)
{
	u8 *opt = skb_put(skb, rd_len);

	memset(opt, 0, 8);
	*(opt++) = ND_OPT_REDIRECT_HDR;
	*(opt++) = (rd_len >> 3);
	opt += 6;

	skb_copy_bits(orig_skb, skb_network_offset(orig_skb), opt,
		      rd_len - 8);
}

void ndisc_send_redirect(struct sk_buff *skb, const struct in6_addr *target)
{
	struct net_device *dev = skb->dev;
	struct net *net = dev_net_rcu(dev);
	struct sock *sk = net->ipv6.ndisc_sk;
	int optlen = 0;
	struct inet_peer *peer;
	struct sk_buff *buff;
	struct rd_msg *msg;
	struct in6_addr saddr_buf;
	struct rt6_info *rt;
	struct dst_entry *dst;
	struct flowi6 fl6;
	int rd_len;
	u8 ha_buf[MAX_ADDR_LEN], *ha = NULL,
	   ops_data_buf[NDISC_OPS_REDIRECT_DATA_SPACE], *ops_data = NULL;
	bool ret;

	if (netif_is_l3_master(dev)) {
		dev = dev_get_by_index_rcu(net, IPCB(skb)->iif);
		if (!dev)
			return;
	}

	if (ipv6_get_lladdr(dev, &saddr_buf, IFA_F_TENTATIVE)) {
		net_dbg_ratelimited("Redirect: no link-local address on %s\n", dev->name);
		return;
	}

	if (!ipv6_addr_equal(&ipv6_hdr(skb)->daddr, target) &&
	    ipv6_addr_type(target) != (IPV6_ADDR_UNICAST|IPV6_ADDR_LINKLOCAL)) {
		net_dbg_ratelimited("Redirect: target address is not link-local unicast\n");
		return;
	}

	icmpv6_flow_init(sk, &fl6, NDISC_REDIRECT,
			 &saddr_buf, &ipv6_hdr(skb)->saddr, dev->ifindex);

	dst = ip6_route_output(net, NULL, &fl6);
	if (dst->error) {
		dst_release(dst);
		return;
	}
	dst = xfrm_lookup(net, dst, flowi6_to_flowi(&fl6), NULL, 0);
	if (IS_ERR(dst))
		return;

	rt = dst_rt6_info(dst);

	if (rt->rt6i_flags & RTF_GATEWAY) {
		net_dbg_ratelimited("Redirect: destination is not a neighbour\n");
		goto release;
	}

	peer = inet_getpeer_v6(net->ipv6.peers, &ipv6_hdr(skb)->saddr);
	ret = inet_peer_xrlim_allow(peer, 1*HZ);

	if (!ret)
		goto release;

	if (dev->addr_len) {
		struct neighbour *neigh = dst_neigh_lookup(skb_dst(skb), target);
		if (!neigh) {
			net_dbg_ratelimited("Redirect: no neigh for target address\n");
			goto release;
		}

		read_lock_bh(&neigh->lock);
		if (neigh->nud_state & NUD_VALID) {
			memcpy(ha_buf, neigh->ha, dev->addr_len);
			read_unlock_bh(&neigh->lock);
			ha = ha_buf;
			optlen += ndisc_redirect_opt_addr_space(dev, neigh,
								ops_data_buf,
								&ops_data);
		} else
			read_unlock_bh(&neigh->lock);

		neigh_release(neigh);
	}

	rd_len = min_t(unsigned int,
		       IPV6_MIN_MTU - sizeof(struct ipv6hdr) - sizeof(*msg) - optlen,
		       skb->len + 8);
	rd_len &= ~0x7;
	optlen += rd_len;

	buff = ndisc_alloc_skb(dev, sizeof(*msg) + optlen);
	if (!buff)
		goto release;

	msg = skb_put(buff, sizeof(*msg));
	*msg = (struct rd_msg) {
		.icmph = {
			.icmp6_type = NDISC_REDIRECT,
		},
		.target = *target,
		.dest = ipv6_hdr(skb)->daddr,
	};

	/*
	 *	include target_address option
	 */

	if (ha)
		ndisc_fill_redirect_addr_option(buff, ha, ops_data);

	/*
	 *	build redirect option and copy skb over to the new packet.
	 */

	if (rd_len)
		ndisc_fill_redirect_hdr_option(buff, skb, rd_len);

	skb_dst_set(buff, dst);
	ndisc_send_skb(buff, &ipv6_hdr(skb)->saddr, &saddr_buf);
	return;

release:
	dst_release(dst);
}

static void pndisc_redo(struct sk_buff *skb)
{
	enum skb_drop_reason reason = ndisc_recv_ns(skb);

	kfree_skb_reason(skb, reason);
}

static int ndisc_is_multicast(const void *pkey)
{
	return ipv6_addr_is_multicast((struct in6_addr *)pkey);
}

static bool ndisc_suppress_frag_ndisc(struct sk_buff *skb)
{
	struct inet6_dev *idev = __in6_dev_get(skb->dev);

	if (!idev)
		return true;
	if (IP6CB(skb)->flags & IP6SKB_FRAGMENTED &&
	    READ_ONCE(idev->cnf.suppress_frag_ndisc)) {
		net_warn_ratelimited("Received fragmented ndisc packet. Carefully consider disabling suppress_frag_ndisc.\n");
		return true;
	}
	return false;
}

enum skb_drop_reason ndisc_rcv(struct sk_buff *skb)
{
	struct nd_msg *msg;
	SKB_DR(reason);

	if (ndisc_suppress_frag_ndisc(skb))
		return SKB_DROP_REASON_IPV6_NDISC_FRAG;

	if (skb_linearize(skb))
		return SKB_DROP_REASON_NOMEM;

	msg = (struct nd_msg *)skb_transport_header(skb);

	__skb_push(skb, skb->data - skb_transport_header(skb));

	if (ipv6_hdr(skb)->hop_limit != 255) {
		net_dbg_ratelimited("NDISC: invalid hop-limit: %d\n", ipv6_hdr(skb)->hop_limit);
		return SKB_DROP_REASON_IPV6_NDISC_HOP_LIMIT;
	}

	if (msg->icmph.icmp6_code != 0) {
		net_dbg_ratelimited("NDISC: invalid ICMPv6 code: %d\n", msg->icmph.icmp6_code);
		return SKB_DROP_REASON_IPV6_NDISC_BAD_CODE;
	}

	switch (msg->icmph.icmp6_type) {
	case NDISC_NEIGHBOUR_SOLICITATION:
		memset(NEIGH_CB(skb), 0, sizeof(struct neighbour_cb));
		reason = ndisc_recv_ns(skb);
		break;

	case NDISC_NEIGHBOUR_ADVERTISEMENT:
		reason = ndisc_recv_na(skb);
		break;

	case NDISC_ROUTER_SOLICITATION:
		reason = ndisc_recv_rs(skb);
		break;

	case NDISC_ROUTER_ADVERTISEMENT:
		reason = ndisc_router_discovery(skb);
		break;

	case NDISC_REDIRECT:
		reason = ndisc_redirect_rcv(skb);
		break;
	}

	return reason;
}

static int ndisc_netdev_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct netdev_notifier_change_info *change_info;
	struct net *net = dev_net(dev);
	struct inet6_dev *idev;
	bool evict_nocarrier;

	switch (event) {
	case NETDEV_CHANGEADDR:
		neigh_changeaddr(&nd_tbl, dev);
		fib6_run_gc(0, net, false);
		fallthrough;
	case NETDEV_UP:
		idev = in6_dev_get(dev);
		if (!idev)
			break;
		if (READ_ONCE(idev->cnf.ndisc_notify) ||
		    READ_ONCE(net->ipv6.devconf_all->ndisc_notify))
			ndisc_send_unsol_na(dev);
		in6_dev_put(idev);
		break;
	case NETDEV_CHANGE:
		idev = in6_dev_get(dev);
		if (!idev)
			evict_nocarrier = true;
		else {
			evict_nocarrier = READ_ONCE(idev->cnf.ndisc_evict_nocarrier) &&
					  READ_ONCE(net->ipv6.devconf_all->ndisc_evict_nocarrier);
			in6_dev_put(idev);
		}

		change_info = ptr;
		if (change_info->flags_changed & IFF_NOARP)
			neigh_changeaddr(&nd_tbl, dev);
		if (evict_nocarrier && !netif_carrier_ok(dev))
			neigh_carrier_down(&nd_tbl, dev);
		break;
	case NETDEV_DOWN:
		neigh_ifdown(&nd_tbl, dev);
		fib6_run_gc(0, net, false);
		break;
	case NETDEV_NOTIFY_PEERS:
		ndisc_send_unsol_na(dev);
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block ndisc_netdev_notifier = {
	.notifier_call = ndisc_netdev_event,
	.priority = ADDRCONF_NOTIFY_PRIORITY - 5,
};

#ifdef CONFIG_SYSCTL
static void ndisc_warn_deprecated_sysctl(const struct ctl_table *ctl,
					 const char *func, const char *dev_name)
{
	static char warncomm[TASK_COMM_LEN];
	static int warned;
	if (strcmp(warncomm, current->comm) && warned < 5) {
		strscpy(warncomm, current->comm);
		pr_warn("process `%s' is using deprecated sysctl (%s) net.ipv6.neigh.%s.%s - use net.ipv6.neigh.%s.%s_ms instead\n",
			warncomm, func,
			dev_name, ctl->procname,
			dev_name, ctl->procname);
		warned++;
	}
}

int ndisc_ifinfo_sysctl_change(const struct ctl_table *ctl, int write, void *buffer,
		size_t *lenp, loff_t *ppos)
{
	struct net_device *dev = ctl->extra1;
	struct inet6_dev *idev;
	int ret;

	if ((strcmp(ctl->procname, "retrans_time") == 0) ||
	    (strcmp(ctl->procname, "base_reachable_time") == 0))
		ndisc_warn_deprecated_sysctl(ctl, "syscall", dev ? dev->name : "default");

	if (strcmp(ctl->procname, "retrans_time") == 0)
		ret = neigh_proc_dointvec(ctl, write, buffer, lenp, ppos);

	else if (strcmp(ctl->procname, "base_reachable_time") == 0)
		ret = neigh_proc_dointvec_jiffies(ctl, write,
						  buffer, lenp, ppos);

	else if ((strcmp(ctl->procname, "retrans_time_ms") == 0) ||
		 (strcmp(ctl->procname, "base_reachable_time_ms") == 0))
		ret = neigh_proc_dointvec_ms_jiffies(ctl, write,
						     buffer, lenp, ppos);
	else
		ret = -1;

	if (write && ret == 0 && dev && (idev = in6_dev_get(dev)) != NULL) {
		if (ctl->data == &NEIGH_VAR(idev->nd_parms, BASE_REACHABLE_TIME))
			idev->nd_parms->reachable_time =
					neigh_rand_reach_time(NEIGH_VAR(idev->nd_parms, BASE_REACHABLE_TIME));
		WRITE_ONCE(idev->tstamp, jiffies);
		inet6_ifinfo_notify(RTM_NEWLINK, idev);
		in6_dev_put(idev);
	}
	return ret;
}


#endif

static int __net_init ndisc_net_init(struct net *net)
{
	struct ipv6_pinfo *np;
	struct sock *sk;
	int err;

	err = inet_ctl_sock_create(&sk, PF_INET6,
				   SOCK_RAW, IPPROTO_ICMPV6, net);
	if (err < 0) {
		net_err_ratelimited("NDISC: Failed to initialize the control socket (err %d)\n",
				    err);
		return err;
	}

	net->ipv6.ndisc_sk = sk;

	np = inet6_sk(sk);
	np->hop_limit = 255;
	/* Do not loopback ndisc messages */
	inet6_clear_bit(MC6_LOOP, sk);

	return 0;
}

static void __net_exit ndisc_net_exit(struct net *net)
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
	neigh_table_init(NEIGH_ND_TABLE, &nd_tbl);

#ifdef CONFIG_SYSCTL
	err = neigh_sysctl_register(NULL, &nd_tbl.parms,
				    ndisc_ifinfo_sysctl_change);
	if (err)
		goto out_unregister_pernet;
out:
#endif
	return err;

#ifdef CONFIG_SYSCTL
out_unregister_pernet:
	unregister_pernet_subsys(&ndisc_net_ops);
	goto out;
#endif
}

int __init ndisc_late_init(void)
{
	return register_netdevice_notifier(&ndisc_netdev_notifier);
}

void ndisc_late_cleanup(void)
{
	unregister_netdevice_notifier(&ndisc_netdev_notifier);
}

void ndisc_cleanup(void)
{
#ifdef CONFIG_SYSCTL
	neigh_sysctl_unregister(&nd_tbl.parms);
#endif
	neigh_table_clear(NEIGH_ND_TABLE, &nd_tbl);
	unregister_pernet_subsys(&ndisc_net_ops);
}
