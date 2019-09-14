// SPDX-License-Identifier: GPL-2.0-or-later
/* SCTP kernel implementation
 * (C) Copyright IBM Corp. 2002, 2004
 * Copyright (c) 2001 Nokia, Inc.
 * Copyright (c) 2001 La Monte H.P. Yarroll
 * Copyright (c) 2002-2003 Intel Corp.
 *
 * This file is part of the SCTP kernel implementation
 *
 * SCTP over IPv6.
 *
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <linux-sctp@vger.kernel.org>
 *
 * Written or modified by:
 *    Le Yanqun		    <yanqun.le@nokia.com>
 *    Hui Huang		    <hui.huang@nokia.com>
 *    La Monte H.P. Yarroll <piggy@acm.org>
 *    Sridhar Samudrala	    <sri@us.ibm.com>
 *    Jon Grimm		    <jgrimm@us.ibm.com>
 *    Ardelle Fan	    <ardelle.fan@intel.com>
 *
 * Based on:
 *	linux/net/ipv6/tcp_ipv6.c
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/netdevice.h>
#include <linux/init.h>
#include <linux/ipsec.h>
#include <linux/slab.h>

#include <linux/ipv6.h>
#include <linux/icmpv6.h>
#include <linux/random.h>
#include <linux/seq_file.h>

#include <net/protocol.h>
#include <net/ndisc.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/transp_v6.h>
#include <net/addrconf.h>
#include <net/ip6_route.h>
#include <net/inet_common.h>
#include <net/inet_ecn.h>
#include <net/sctp/sctp.h>

#include <linux/uaccess.h>

static inline int sctp_v6_addr_match_len(union sctp_addr *s1,
					 union sctp_addr *s2);
static void sctp_v6_to_addr(union sctp_addr *addr, struct in6_addr *saddr,
			      __be16 port);
static int sctp_v6_cmp_addr(const union sctp_addr *addr1,
			    const union sctp_addr *addr2);

/* Event handler for inet6 address addition/deletion events.
 * The sctp_local_addr_list needs to be protocted by a spin lock since
 * multiple notifiers (say IPv4 and IPv6) may be running at the same
 * time and thus corrupt the list.
 * The reader side is protected with RCU.
 */
static int sctp_inet6addr_event(struct notifier_block *this, unsigned long ev,
				void *ptr)
{
	struct inet6_ifaddr *ifa = (struct inet6_ifaddr *)ptr;
	struct sctp_sockaddr_entry *addr = NULL;
	struct sctp_sockaddr_entry *temp;
	struct net *net = dev_net(ifa->idev->dev);
	int found = 0;

	switch (ev) {
	case NETDEV_UP:
		addr = kzalloc(sizeof(*addr), GFP_ATOMIC);
		if (addr) {
			addr->a.v6.sin6_family = AF_INET6;
			addr->a.v6.sin6_addr = ifa->addr;
			addr->a.v6.sin6_scope_id = ifa->idev->dev->ifindex;
			addr->valid = 1;
			spin_lock_bh(&net->sctp.local_addr_lock);
			list_add_tail_rcu(&addr->list, &net->sctp.local_addr_list);
			sctp_addr_wq_mgmt(net, addr, SCTP_ADDR_NEW);
			spin_unlock_bh(&net->sctp.local_addr_lock);
		}
		break;
	case NETDEV_DOWN:
		spin_lock_bh(&net->sctp.local_addr_lock);
		list_for_each_entry_safe(addr, temp,
					&net->sctp.local_addr_list, list) {
			if (addr->a.sa.sa_family == AF_INET6 &&
					ipv6_addr_equal(&addr->a.v6.sin6_addr,
						&ifa->addr)) {
				sctp_addr_wq_mgmt(net, addr, SCTP_ADDR_DEL);
				found = 1;
				addr->valid = 0;
				list_del_rcu(&addr->list);
				break;
			}
		}
		spin_unlock_bh(&net->sctp.local_addr_lock);
		if (found)
			kfree_rcu(addr, rcu);
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block sctp_inet6addr_notifier = {
	.notifier_call = sctp_inet6addr_event,
};

/* ICMP error handler. */
static int sctp_v6_err(struct sk_buff *skb, struct inet6_skb_parm *opt,
			u8 type, u8 code, int offset, __be32 info)
{
	struct inet6_dev *idev;
	struct sock *sk;
	struct sctp_association *asoc;
	struct sctp_transport *transport;
	struct ipv6_pinfo *np;
	__u16 saveip, savesctp;
	int err, ret = 0;
	struct net *net = dev_net(skb->dev);

	idev = in6_dev_get(skb->dev);

	/* Fix up skb to look at the embedded net header. */
	saveip	 = skb->network_header;
	savesctp = skb->transport_header;
	skb_reset_network_header(skb);
	skb_set_transport_header(skb, offset);
	sk = sctp_err_lookup(net, AF_INET6, skb, sctp_hdr(skb), &asoc, &transport);
	/* Put back, the original pointers. */
	skb->network_header   = saveip;
	skb->transport_header = savesctp;
	if (!sk) {
		__ICMP6_INC_STATS(net, idev, ICMP6_MIB_INERRORS);
		ret = -ENOENT;
		goto out;
	}

	/* Warning:  The sock lock is held.  Remember to call
	 * sctp_err_finish!
	 */

	switch (type) {
	case ICMPV6_PKT_TOOBIG:
		if (ip6_sk_accept_pmtu(sk))
			sctp_icmp_frag_needed(sk, asoc, transport, ntohl(info));
		goto out_unlock;
	case ICMPV6_PARAMPROB:
		if (ICMPV6_UNK_NEXTHDR == code) {
			sctp_icmp_proto_unreachable(sk, asoc, transport);
			goto out_unlock;
		}
		break;
	case NDISC_REDIRECT:
		sctp_icmp_redirect(sk, transport, skb);
		goto out_unlock;
	default:
		break;
	}

	np = inet6_sk(sk);
	icmpv6_err_convert(type, code, &err);
	if (!sock_owned_by_user(sk) && np->recverr) {
		sk->sk_err = err;
		sk->sk_error_report(sk);
	} else {  /* Only an error on timeout */
		sk->sk_err_soft = err;
	}

out_unlock:
	sctp_err_finish(sk, transport);
out:
	if (likely(idev != NULL))
		in6_dev_put(idev);

	return ret;
}

static int sctp_v6_xmit(struct sk_buff *skb, struct sctp_transport *transport)
{
	struct sock *sk = skb->sk;
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct flowi6 *fl6 = &transport->fl.u.ip6;
	__u8 tclass = np->tclass;
	int res;

	pr_debug("%s: skb:%p, len:%d, src:%pI6 dst:%pI6\n", __func__, skb,
		 skb->len, &fl6->saddr, &fl6->daddr);

	if (transport->dscp & SCTP_DSCP_SET_MASK)
		tclass = transport->dscp & SCTP_DSCP_VAL_MASK;

	if (INET_ECN_is_capable(tclass))
		IP6_ECN_flow_xmit(sk, fl6->flowlabel);

	if (!(transport->param_flags & SPP_PMTUD_ENABLE))
		skb->ignore_df = 1;

	SCTP_INC_STATS(sock_net(sk), SCTP_MIB_OUTSCTPPACKS);

	rcu_read_lock();
	res = ip6_xmit(sk, skb, fl6, sk->sk_mark, rcu_dereference(np->opt),
		       tclass);
	rcu_read_unlock();
	return res;
}

/* Returns the dst cache entry for the given source and destination ip
 * addresses.
 */
static void sctp_v6_get_dst(struct sctp_transport *t, union sctp_addr *saddr,
			    struct flowi *fl, struct sock *sk)
{
	struct sctp_association *asoc = t->asoc;
	struct dst_entry *dst = NULL;
	struct flowi6 *fl6 = &fl->u.ip6;
	struct sctp_bind_addr *bp;
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct sctp_sockaddr_entry *laddr;
	union sctp_addr *daddr = &t->ipaddr;
	union sctp_addr dst_saddr;
	struct in6_addr *final_p, final;
	enum sctp_scope scope;
	__u8 matchlen = 0;

	memset(fl6, 0, sizeof(struct flowi6));
	fl6->daddr = daddr->v6.sin6_addr;
	fl6->fl6_dport = daddr->v6.sin6_port;
	fl6->flowi6_proto = IPPROTO_SCTP;
	if (ipv6_addr_type(&daddr->v6.sin6_addr) & IPV6_ADDR_LINKLOCAL)
		fl6->flowi6_oif = daddr->v6.sin6_scope_id;
	else if (asoc)
		fl6->flowi6_oif = asoc->base.sk->sk_bound_dev_if;
	if (t->flowlabel & SCTP_FLOWLABEL_SET_MASK)
		fl6->flowlabel = htonl(t->flowlabel & SCTP_FLOWLABEL_VAL_MASK);

	if (np->sndflow && (fl6->flowlabel & IPV6_FLOWLABEL_MASK)) {
		struct ip6_flowlabel *flowlabel;

		flowlabel = fl6_sock_lookup(sk, fl6->flowlabel);
		if (IS_ERR(flowlabel))
			goto out;
		fl6_sock_release(flowlabel);
	}

	pr_debug("%s: dst=%pI6 ", __func__, &fl6->daddr);

	if (asoc)
		fl6->fl6_sport = htons(asoc->base.bind_addr.port);

	if (saddr) {
		fl6->saddr = saddr->v6.sin6_addr;
		if (!fl6->fl6_sport)
			fl6->fl6_sport = saddr->v6.sin6_port;

		pr_debug("src=%pI6 - ", &fl6->saddr);
	}

	rcu_read_lock();
	final_p = fl6_update_dst(fl6, rcu_dereference(np->opt), &final);
	rcu_read_unlock();

	dst = ip6_dst_lookup_flow(sk, fl6, final_p);
	if (!asoc || saddr)
		goto out;

	bp = &asoc->base.bind_addr;
	scope = sctp_scope(daddr);
	/* ip6_dst_lookup has filled in the fl6->saddr for us.  Check
	 * to see if we can use it.
	 */
	if (!IS_ERR(dst)) {
		/* Walk through the bind address list and look for a bind
		 * address that matches the source address of the returned dst.
		 */
		sctp_v6_to_addr(&dst_saddr, &fl6->saddr, htons(bp->port));
		rcu_read_lock();
		list_for_each_entry_rcu(laddr, &bp->address_list, list) {
			if (!laddr->valid || laddr->state == SCTP_ADDR_DEL ||
			    (laddr->state != SCTP_ADDR_SRC &&
			     !asoc->src_out_of_asoc_ok))
				continue;

			/* Do not compare against v4 addrs */
			if ((laddr->a.sa.sa_family == AF_INET6) &&
			    (sctp_v6_cmp_addr(&dst_saddr, &laddr->a))) {
				rcu_read_unlock();
				goto out;
			}
		}
		rcu_read_unlock();
		/* None of the bound addresses match the source address of the
		 * dst. So release it.
		 */
		dst_release(dst);
		dst = NULL;
	}

	/* Walk through the bind address list and try to get the
	 * best source address for a given destination.
	 */
	rcu_read_lock();
	list_for_each_entry_rcu(laddr, &bp->address_list, list) {
		struct dst_entry *bdst;
		__u8 bmatchlen;

		if (!laddr->valid ||
		    laddr->state != SCTP_ADDR_SRC ||
		    laddr->a.sa.sa_family != AF_INET6 ||
		    scope > sctp_scope(&laddr->a))
			continue;

		fl6->saddr = laddr->a.v6.sin6_addr;
		fl6->fl6_sport = laddr->a.v6.sin6_port;
		final_p = fl6_update_dst(fl6, rcu_dereference(np->opt), &final);
		bdst = ip6_dst_lookup_flow(sk, fl6, final_p);

		if (IS_ERR(bdst))
			continue;

		if (ipv6_chk_addr(dev_net(bdst->dev),
				  &laddr->a.v6.sin6_addr, bdst->dev, 1)) {
			if (!IS_ERR_OR_NULL(dst))
				dst_release(dst);
			dst = bdst;
			break;
		}

		bmatchlen = sctp_v6_addr_match_len(daddr, &laddr->a);
		if (matchlen > bmatchlen) {
			dst_release(bdst);
			continue;
		}

		if (!IS_ERR_OR_NULL(dst))
			dst_release(dst);
		dst = bdst;
		matchlen = bmatchlen;
	}
	rcu_read_unlock();

out:
	if (!IS_ERR_OR_NULL(dst)) {
		struct rt6_info *rt;

		rt = (struct rt6_info *)dst;
		t->dst = dst;
		t->dst_cookie = rt6_get_cookie(rt);
		pr_debug("rt6_dst:%pI6/%d rt6_src:%pI6\n",
			 &rt->rt6i_dst.addr, rt->rt6i_dst.plen,
			 &fl6->saddr);
	} else {
		t->dst = NULL;

		pr_debug("no route\n");
	}
}

/* Returns the number of consecutive initial bits that match in the 2 ipv6
 * addresses.
 */
static inline int sctp_v6_addr_match_len(union sctp_addr *s1,
					 union sctp_addr *s2)
{
	return ipv6_addr_diff(&s1->v6.sin6_addr, &s2->v6.sin6_addr);
}

/* Fills in the source address(saddr) based on the destination address(daddr)
 * and asoc's bind address list.
 */
static void sctp_v6_get_saddr(struct sctp_sock *sk,
			      struct sctp_transport *t,
			      struct flowi *fl)
{
	struct flowi6 *fl6 = &fl->u.ip6;
	union sctp_addr *saddr = &t->saddr;

	pr_debug("%s: asoc:%p dst:%p\n", __func__, t->asoc, t->dst);

	if (t->dst) {
		saddr->v6.sin6_family = AF_INET6;
		saddr->v6.sin6_addr = fl6->saddr;
	}
}

/* Make a copy of all potential local addresses. */
static void sctp_v6_copy_addrlist(struct list_head *addrlist,
				  struct net_device *dev)
{
	struct inet6_dev *in6_dev;
	struct inet6_ifaddr *ifp;
	struct sctp_sockaddr_entry *addr;

	rcu_read_lock();
	if ((in6_dev = __in6_dev_get(dev)) == NULL) {
		rcu_read_unlock();
		return;
	}

	read_lock_bh(&in6_dev->lock);
	list_for_each_entry(ifp, &in6_dev->addr_list, if_list) {
		/* Add the address to the local list.  */
		addr = kzalloc(sizeof(*addr), GFP_ATOMIC);
		if (addr) {
			addr->a.v6.sin6_family = AF_INET6;
			addr->a.v6.sin6_addr = ifp->addr;
			addr->a.v6.sin6_scope_id = dev->ifindex;
			addr->valid = 1;
			INIT_LIST_HEAD(&addr->list);
			list_add_tail(&addr->list, addrlist);
		}
	}

	read_unlock_bh(&in6_dev->lock);
	rcu_read_unlock();
}

/* Copy over any ip options */
static void sctp_v6_copy_ip_options(struct sock *sk, struct sock *newsk)
{
	struct ipv6_pinfo *newnp, *np = inet6_sk(sk);
	struct ipv6_txoptions *opt;

	newnp = inet6_sk(newsk);

	rcu_read_lock();
	opt = rcu_dereference(np->opt);
	if (opt) {
		opt = ipv6_dup_options(newsk, opt);
		if (!opt)
			pr_err("%s: Failed to copy ip options\n", __func__);
	}
	RCU_INIT_POINTER(newnp->opt, opt);
	rcu_read_unlock();
}

/* Account for the IP options */
static int sctp_v6_ip_options_len(struct sock *sk)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct ipv6_txoptions *opt;
	int len = 0;

	rcu_read_lock();
	opt = rcu_dereference(np->opt);
	if (opt)
		len = opt->opt_flen + opt->opt_nflen;

	rcu_read_unlock();
	return len;
}

/* Initialize a sockaddr_storage from in incoming skb. */
static void sctp_v6_from_skb(union sctp_addr *addr, struct sk_buff *skb,
			     int is_saddr)
{
	/* Always called on head skb, so this is safe */
	struct sctphdr *sh = sctp_hdr(skb);
	struct sockaddr_in6 *sa = &addr->v6;

	addr->v6.sin6_family = AF_INET6;
	addr->v6.sin6_flowinfo = 0; /* FIXME */
	addr->v6.sin6_scope_id = ((struct inet6_skb_parm *)skb->cb)->iif;

	if (is_saddr) {
		sa->sin6_port = sh->source;
		sa->sin6_addr = ipv6_hdr(skb)->saddr;
	} else {
		sa->sin6_port = sh->dest;
		sa->sin6_addr = ipv6_hdr(skb)->daddr;
	}
}

/* Initialize an sctp_addr from a socket. */
static void sctp_v6_from_sk(union sctp_addr *addr, struct sock *sk)
{
	addr->v6.sin6_family = AF_INET6;
	addr->v6.sin6_port = 0;
	addr->v6.sin6_addr = sk->sk_v6_rcv_saddr;
}

/* Initialize sk->sk_rcv_saddr from sctp_addr. */
static void sctp_v6_to_sk_saddr(union sctp_addr *addr, struct sock *sk)
{
	if (addr->sa.sa_family == AF_INET) {
		sk->sk_v6_rcv_saddr.s6_addr32[0] = 0;
		sk->sk_v6_rcv_saddr.s6_addr32[1] = 0;
		sk->sk_v6_rcv_saddr.s6_addr32[2] = htonl(0x0000ffff);
		sk->sk_v6_rcv_saddr.s6_addr32[3] =
			addr->v4.sin_addr.s_addr;
	} else {
		sk->sk_v6_rcv_saddr = addr->v6.sin6_addr;
	}
}

/* Initialize sk->sk_daddr from sctp_addr. */
static void sctp_v6_to_sk_daddr(union sctp_addr *addr, struct sock *sk)
{
	if (addr->sa.sa_family == AF_INET) {
		sk->sk_v6_daddr.s6_addr32[0] = 0;
		sk->sk_v6_daddr.s6_addr32[1] = 0;
		sk->sk_v6_daddr.s6_addr32[2] = htonl(0x0000ffff);
		sk->sk_v6_daddr.s6_addr32[3] = addr->v4.sin_addr.s_addr;
	} else {
		sk->sk_v6_daddr = addr->v6.sin6_addr;
	}
}

/* Initialize a sctp_addr from an address parameter. */
static void sctp_v6_from_addr_param(union sctp_addr *addr,
				    union sctp_addr_param *param,
				    __be16 port, int iif)
{
	addr->v6.sin6_family = AF_INET6;
	addr->v6.sin6_port = port;
	addr->v6.sin6_flowinfo = 0; /* BUG */
	addr->v6.sin6_addr = param->v6.addr;
	addr->v6.sin6_scope_id = iif;
}

/* Initialize an address parameter from a sctp_addr and return the length
 * of the address parameter.
 */
static int sctp_v6_to_addr_param(const union sctp_addr *addr,
				 union sctp_addr_param *param)
{
	int length = sizeof(struct sctp_ipv6addr_param);

	param->v6.param_hdr.type = SCTP_PARAM_IPV6_ADDRESS;
	param->v6.param_hdr.length = htons(length);
	param->v6.addr = addr->v6.sin6_addr;

	return length;
}

/* Initialize a sctp_addr from struct in6_addr. */
static void sctp_v6_to_addr(union sctp_addr *addr, struct in6_addr *saddr,
			      __be16 port)
{
	addr->sa.sa_family = AF_INET6;
	addr->v6.sin6_port = port;
	addr->v6.sin6_flowinfo = 0;
	addr->v6.sin6_addr = *saddr;
	addr->v6.sin6_scope_id = 0;
}

static int __sctp_v6_cmp_addr(const union sctp_addr *addr1,
			      const union sctp_addr *addr2)
{
	if (addr1->sa.sa_family != addr2->sa.sa_family) {
		if (addr1->sa.sa_family == AF_INET &&
		    addr2->sa.sa_family == AF_INET6 &&
		    ipv6_addr_v4mapped(&addr2->v6.sin6_addr) &&
		    addr2->v6.sin6_addr.s6_addr32[3] ==
		    addr1->v4.sin_addr.s_addr)
			return 1;

		if (addr2->sa.sa_family == AF_INET &&
		    addr1->sa.sa_family == AF_INET6 &&
		    ipv6_addr_v4mapped(&addr1->v6.sin6_addr) &&
		    addr1->v6.sin6_addr.s6_addr32[3] ==
		    addr2->v4.sin_addr.s_addr)
			return 1;

		return 0;
	}

	if (!ipv6_addr_equal(&addr1->v6.sin6_addr, &addr2->v6.sin6_addr))
		return 0;

	/* If this is a linklocal address, compare the scope_id. */
	if ((ipv6_addr_type(&addr1->v6.sin6_addr) & IPV6_ADDR_LINKLOCAL) &&
	    addr1->v6.sin6_scope_id && addr2->v6.sin6_scope_id &&
	    addr1->v6.sin6_scope_id != addr2->v6.sin6_scope_id)
		return 0;

	return 1;
}

/* Compare addresses exactly.
 * v4-mapped-v6 is also in consideration.
 */
static int sctp_v6_cmp_addr(const union sctp_addr *addr1,
			    const union sctp_addr *addr2)
{
	return __sctp_v6_cmp_addr(addr1, addr2) &&
	       addr1->v6.sin6_port == addr2->v6.sin6_port;
}

/* Initialize addr struct to INADDR_ANY. */
static void sctp_v6_inaddr_any(union sctp_addr *addr, __be16 port)
{
	memset(addr, 0x00, sizeof(union sctp_addr));
	addr->v6.sin6_family = AF_INET6;
	addr->v6.sin6_port = port;
}

/* Is this a wildcard address? */
static int sctp_v6_is_any(const union sctp_addr *addr)
{
	return ipv6_addr_any(&addr->v6.sin6_addr);
}

/* Should this be available for binding?   */
static int sctp_v6_available(union sctp_addr *addr, struct sctp_sock *sp)
{
	int type;
	struct net *net = sock_net(&sp->inet.sk);
	const struct in6_addr *in6 = (const struct in6_addr *)&addr->v6.sin6_addr;

	type = ipv6_addr_type(in6);
	if (IPV6_ADDR_ANY == type)
		return 1;
	if (type == IPV6_ADDR_MAPPED) {
		if (sp && ipv6_only_sock(sctp_opt2sk(sp)))
			return 0;
		sctp_v6_map_v4(addr);
		return sctp_get_af_specific(AF_INET)->available(addr, sp);
	}
	if (!(type & IPV6_ADDR_UNICAST))
		return 0;

	return sp->inet.freebind || net->ipv6.sysctl.ip_nonlocal_bind ||
		ipv6_chk_addr(net, in6, NULL, 0);
}

/* This function checks if the address is a valid address to be used for
 * SCTP.
 *
 * Output:
 * Return 0 - If the address is a non-unicast or an illegal address.
 * Return 1 - If the address is a unicast.
 */
static int sctp_v6_addr_valid(union sctp_addr *addr,
			      struct sctp_sock *sp,
			      const struct sk_buff *skb)
{
	int ret = ipv6_addr_type(&addr->v6.sin6_addr);

	/* Support v4-mapped-v6 address. */
	if (ret == IPV6_ADDR_MAPPED) {
		/* Note: This routine is used in input, so v4-mapped-v6
		 * are disallowed here when there is no sctp_sock.
		 */
		if (sp && ipv6_only_sock(sctp_opt2sk(sp)))
			return 0;
		sctp_v6_map_v4(addr);
		return sctp_get_af_specific(AF_INET)->addr_valid(addr, sp, skb);
	}

	/* Is this a non-unicast address */
	if (!(ret & IPV6_ADDR_UNICAST))
		return 0;

	return 1;
}

/* What is the scope of 'addr'?  */
static enum sctp_scope sctp_v6_scope(union sctp_addr *addr)
{
	enum sctp_scope retval;
	int v6scope;

	/* The IPv6 scope is really a set of bit fields.
	 * See IFA_* in <net/if_inet6.h>.  Map to a generic SCTP scope.
	 */

	v6scope = ipv6_addr_scope(&addr->v6.sin6_addr);
	switch (v6scope) {
	case IFA_HOST:
		retval = SCTP_SCOPE_LOOPBACK;
		break;
	case IFA_LINK:
		retval = SCTP_SCOPE_LINK;
		break;
	case IFA_SITE:
		retval = SCTP_SCOPE_PRIVATE;
		break;
	default:
		retval = SCTP_SCOPE_GLOBAL;
		break;
	}

	return retval;
}

/* Create and initialize a new sk for the socket to be returned by accept(). */
static struct sock *sctp_v6_create_accept_sk(struct sock *sk,
					     struct sctp_association *asoc,
					     bool kern)
{
	struct sock *newsk;
	struct ipv6_pinfo *newnp, *np = inet6_sk(sk);
	struct sctp6_sock *newsctp6sk;

	newsk = sk_alloc(sock_net(sk), PF_INET6, GFP_KERNEL, sk->sk_prot, kern);
	if (!newsk)
		goto out;

	sock_init_data(NULL, newsk);

	sctp_copy_sock(newsk, sk, asoc);
	sock_reset_flag(sk, SOCK_ZAPPED);

	newsctp6sk = (struct sctp6_sock *)newsk;
	inet_sk(newsk)->pinet6 = &newsctp6sk->inet6;

	sctp_sk(newsk)->v4mapped = sctp_sk(sk)->v4mapped;

	newnp = inet6_sk(newsk);

	memcpy(newnp, np, sizeof(struct ipv6_pinfo));
	newnp->ipv6_mc_list = NULL;
	newnp->ipv6_ac_list = NULL;
	newnp->ipv6_fl_list = NULL;

	sctp_v6_copy_ip_options(sk, newsk);

	/* Initialize sk's sport, dport, rcv_saddr and daddr for getsockname()
	 * and getpeername().
	 */
	sctp_v6_to_sk_daddr(&asoc->peer.primary_addr, newsk);

	newsk->sk_v6_rcv_saddr = sk->sk_v6_rcv_saddr;

	sk_refcnt_debug_inc(newsk);

	if (newsk->sk_prot->init(newsk)) {
		sk_common_release(newsk);
		newsk = NULL;
	}

out:
	return newsk;
}

/* Format a sockaddr for return to user space. This makes sure the return is
 * AF_INET or AF_INET6 depending on the SCTP_I_WANT_MAPPED_V4_ADDR option.
 */
static int sctp_v6_addr_to_user(struct sctp_sock *sp, union sctp_addr *addr)
{
	if (sp->v4mapped) {
		if (addr->sa.sa_family == AF_INET)
			sctp_v4_map_v6(addr);
	} else {
		if (addr->sa.sa_family == AF_INET6 &&
		    ipv6_addr_v4mapped(&addr->v6.sin6_addr))
			sctp_v6_map_v4(addr);
	}

	if (addr->sa.sa_family == AF_INET) {
		memset(addr->v4.sin_zero, 0, sizeof(addr->v4.sin_zero));
		return sizeof(struct sockaddr_in);
	}
	return sizeof(struct sockaddr_in6);
}

/* Where did this skb come from?  */
static int sctp_v6_skb_iif(const struct sk_buff *skb)
{
	return IP6CB(skb)->iif;
}

/* Was this packet marked by Explicit Congestion Notification? */
static int sctp_v6_is_ce(const struct sk_buff *skb)
{
	return *((__u32 *)(ipv6_hdr(skb))) & (__force __u32)htonl(1 << 20);
}

/* Dump the v6 addr to the seq file. */
static void sctp_v6_seq_dump_addr(struct seq_file *seq, union sctp_addr *addr)
{
	seq_printf(seq, "%pI6 ", &addr->v6.sin6_addr);
}

static void sctp_v6_ecn_capable(struct sock *sk)
{
	inet6_sk(sk)->tclass |= INET_ECN_ECT_0;
}

/* Initialize a PF_INET msgname from a ulpevent. */
static void sctp_inet6_event_msgname(struct sctp_ulpevent *event,
				     char *msgname, int *addrlen)
{
	union sctp_addr *addr;
	struct sctp_association *asoc;
	union sctp_addr *paddr;

	if (!msgname)
		return;

	addr = (union sctp_addr *)msgname;
	asoc = event->asoc;
	paddr = &asoc->peer.primary_addr;

	if (paddr->sa.sa_family == AF_INET) {
		addr->v4.sin_family = AF_INET;
		addr->v4.sin_port = htons(asoc->peer.port);
		addr->v4.sin_addr = paddr->v4.sin_addr;
	} else {
		addr->v6.sin6_family = AF_INET6;
		addr->v6.sin6_flowinfo = 0;
		if (ipv6_addr_type(&paddr->v6.sin6_addr) & IPV6_ADDR_LINKLOCAL)
			addr->v6.sin6_scope_id = paddr->v6.sin6_scope_id;
		else
			addr->v6.sin6_scope_id = 0;
		addr->v6.sin6_port = htons(asoc->peer.port);
		addr->v6.sin6_addr = paddr->v6.sin6_addr;
	}

	*addrlen = sctp_v6_addr_to_user(sctp_sk(asoc->base.sk), addr);
}

/* Initialize a msg_name from an inbound skb. */
static void sctp_inet6_skb_msgname(struct sk_buff *skb, char *msgname,
				   int *addr_len)
{
	union sctp_addr *addr;
	struct sctphdr *sh;

	if (!msgname)
		return;

	addr = (union sctp_addr *)msgname;
	sh = sctp_hdr(skb);

	if (ip_hdr(skb)->version == 4) {
		addr->v4.sin_family = AF_INET;
		addr->v4.sin_port = sh->source;
		addr->v4.sin_addr.s_addr = ip_hdr(skb)->saddr;
	} else {
		addr->v6.sin6_family = AF_INET6;
		addr->v6.sin6_flowinfo = 0;
		addr->v6.sin6_port = sh->source;
		addr->v6.sin6_addr = ipv6_hdr(skb)->saddr;
		if (ipv6_addr_type(&addr->v6.sin6_addr) & IPV6_ADDR_LINKLOCAL)
			addr->v6.sin6_scope_id = sctp_v6_skb_iif(skb);
		else
			addr->v6.sin6_scope_id = 0;
	}

	*addr_len = sctp_v6_addr_to_user(sctp_sk(skb->sk), addr);
}

/* Do we support this AF? */
static int sctp_inet6_af_supported(sa_family_t family, struct sctp_sock *sp)
{
	switch (family) {
	case AF_INET6:
		return 1;
	/* v4-mapped-v6 addresses */
	case AF_INET:
		if (!__ipv6_only_sock(sctp_opt2sk(sp)))
			return 1;
		/* fallthru */
	default:
		return 0;
	}
}

/* Address matching with wildcards allowed.  This extra level
 * of indirection lets us choose whether a PF_INET6 should
 * disallow any v4 addresses if we so choose.
 */
static int sctp_inet6_cmp_addr(const union sctp_addr *addr1,
			       const union sctp_addr *addr2,
			       struct sctp_sock *opt)
{
	struct sock *sk = sctp_opt2sk(opt);
	struct sctp_af *af1, *af2;

	af1 = sctp_get_af_specific(addr1->sa.sa_family);
	af2 = sctp_get_af_specific(addr2->sa.sa_family);

	if (!af1 || !af2)
		return 0;

	/* If the socket is IPv6 only, v4 addrs will not match */
	if (__ipv6_only_sock(sk) && af1 != af2)
		return 0;

	/* Today, wildcard AF_INET/AF_INET6. */
	if (sctp_is_any(sk, addr1) || sctp_is_any(sk, addr2))
		return 1;

	if (addr1->sa.sa_family == AF_INET && addr2->sa.sa_family == AF_INET)
		return addr1->v4.sin_addr.s_addr == addr2->v4.sin_addr.s_addr;

	return __sctp_v6_cmp_addr(addr1, addr2);
}

/* Verify that the provided sockaddr looks bindable.   Common verification,
 * has already been taken care of.
 */
static int sctp_inet6_bind_verify(struct sctp_sock *opt, union sctp_addr *addr)
{
	struct sctp_af *af;

	/* ASSERT: address family has already been verified. */
	if (addr->sa.sa_family != AF_INET6)
		af = sctp_get_af_specific(addr->sa.sa_family);
	else {
		int type = ipv6_addr_type(&addr->v6.sin6_addr);
		struct net_device *dev;

		if (type & IPV6_ADDR_LINKLOCAL) {
			struct net *net;
			if (!addr->v6.sin6_scope_id)
				return 0;
			net = sock_net(&opt->inet.sk);
			rcu_read_lock();
			dev = dev_get_by_index_rcu(net, addr->v6.sin6_scope_id);
			if (!dev || !(opt->inet.freebind ||
				      net->ipv6.sysctl.ip_nonlocal_bind ||
				      ipv6_chk_addr(net, &addr->v6.sin6_addr,
						    dev, 0))) {
				rcu_read_unlock();
				return 0;
			}
			rcu_read_unlock();
		}

		af = opt->pf->af;
	}
	return af->available(addr, opt);
}

/* Verify that the provided sockaddr looks sendable.   Common verification,
 * has already been taken care of.
 */
static int sctp_inet6_send_verify(struct sctp_sock *opt, union sctp_addr *addr)
{
	struct sctp_af *af = NULL;

	/* ASSERT: address family has already been verified. */
	if (addr->sa.sa_family != AF_INET6)
		af = sctp_get_af_specific(addr->sa.sa_family);
	else {
		int type = ipv6_addr_type(&addr->v6.sin6_addr);
		struct net_device *dev;

		if (type & IPV6_ADDR_LINKLOCAL) {
			if (!addr->v6.sin6_scope_id)
				return 0;
			rcu_read_lock();
			dev = dev_get_by_index_rcu(sock_net(&opt->inet.sk),
						   addr->v6.sin6_scope_id);
			rcu_read_unlock();
			if (!dev)
				return 0;
		}
		af = opt->pf->af;
	}

	return af != NULL;
}

/* Fill in Supported Address Type information for INIT and INIT-ACK
 * chunks.   Note: In the future, we may want to look at sock options
 * to determine whether a PF_INET6 socket really wants to have IPV4
 * addresses.
 * Returns number of addresses supported.
 */
static int sctp_inet6_supported_addrs(const struct sctp_sock *opt,
				      __be16 *types)
{
	types[0] = SCTP_PARAM_IPV6_ADDRESS;
	if (!opt || !ipv6_only_sock(sctp_opt2sk(opt))) {
		types[1] = SCTP_PARAM_IPV4_ADDRESS;
		return 2;
	}
	return 1;
}

/* Handle SCTP_I_WANT_MAPPED_V4_ADDR for getpeername() and getsockname() */
static int sctp_getname(struct socket *sock, struct sockaddr *uaddr,
			int peer)
{
	int rc;

	rc = inet6_getname(sock, uaddr, peer);

	if (rc < 0)
		return rc;

	rc = sctp_v6_addr_to_user(sctp_sk(sock->sk),
					  (union sctp_addr *)uaddr);

	return rc;
}

static const struct proto_ops inet6_seqpacket_ops = {
	.family		   = PF_INET6,
	.owner		   = THIS_MODULE,
	.release	   = inet6_release,
	.bind		   = inet6_bind,
	.connect	   = sctp_inet_connect,
	.socketpair	   = sock_no_socketpair,
	.accept		   = inet_accept,
	.getname	   = sctp_getname,
	.poll		   = sctp_poll,
	.ioctl		   = inet6_ioctl,
	.gettstamp	   = sock_gettstamp,
	.listen		   = sctp_inet_listen,
	.shutdown	   = inet_shutdown,
	.setsockopt	   = sock_common_setsockopt,
	.getsockopt	   = sock_common_getsockopt,
	.sendmsg	   = inet_sendmsg,
	.recvmsg	   = inet_recvmsg,
	.mmap		   = sock_no_mmap,
#ifdef CONFIG_COMPAT
	.compat_setsockopt = compat_sock_common_setsockopt,
	.compat_getsockopt = compat_sock_common_getsockopt,
#endif
};

static struct inet_protosw sctpv6_seqpacket_protosw = {
	.type          = SOCK_SEQPACKET,
	.protocol      = IPPROTO_SCTP,
	.prot 	       = &sctpv6_prot,
	.ops           = &inet6_seqpacket_ops,
	.flags         = SCTP_PROTOSW_FLAG
};
static struct inet_protosw sctpv6_stream_protosw = {
	.type          = SOCK_STREAM,
	.protocol      = IPPROTO_SCTP,
	.prot 	       = &sctpv6_prot,
	.ops           = &inet6_seqpacket_ops,
	.flags         = SCTP_PROTOSW_FLAG,
};

static int sctp6_rcv(struct sk_buff *skb)
{
	return sctp_rcv(skb) ? -1 : 0;
}

static const struct inet6_protocol sctpv6_protocol = {
	.handler      = sctp6_rcv,
	.err_handler  = sctp_v6_err,
	.flags        = INET6_PROTO_NOPOLICY | INET6_PROTO_FINAL,
};

static struct sctp_af sctp_af_inet6 = {
	.sa_family	   = AF_INET6,
	.sctp_xmit	   = sctp_v6_xmit,
	.setsockopt	   = ipv6_setsockopt,
	.getsockopt	   = ipv6_getsockopt,
	.get_dst	   = sctp_v6_get_dst,
	.get_saddr	   = sctp_v6_get_saddr,
	.copy_addrlist	   = sctp_v6_copy_addrlist,
	.from_skb	   = sctp_v6_from_skb,
	.from_sk	   = sctp_v6_from_sk,
	.from_addr_param   = sctp_v6_from_addr_param,
	.to_addr_param	   = sctp_v6_to_addr_param,
	.cmp_addr	   = sctp_v6_cmp_addr,
	.scope		   = sctp_v6_scope,
	.addr_valid	   = sctp_v6_addr_valid,
	.inaddr_any	   = sctp_v6_inaddr_any,
	.is_any		   = sctp_v6_is_any,
	.available	   = sctp_v6_available,
	.skb_iif	   = sctp_v6_skb_iif,
	.is_ce		   = sctp_v6_is_ce,
	.seq_dump_addr	   = sctp_v6_seq_dump_addr,
	.ecn_capable	   = sctp_v6_ecn_capable,
	.net_header_len	   = sizeof(struct ipv6hdr),
	.sockaddr_len	   = sizeof(struct sockaddr_in6),
	.ip_options_len	   = sctp_v6_ip_options_len,
#ifdef CONFIG_COMPAT
	.compat_setsockopt = compat_ipv6_setsockopt,
	.compat_getsockopt = compat_ipv6_getsockopt,
#endif
};

static struct sctp_pf sctp_pf_inet6 = {
	.event_msgname = sctp_inet6_event_msgname,
	.skb_msgname   = sctp_inet6_skb_msgname,
	.af_supported  = sctp_inet6_af_supported,
	.cmp_addr      = sctp_inet6_cmp_addr,
	.bind_verify   = sctp_inet6_bind_verify,
	.send_verify   = sctp_inet6_send_verify,
	.supported_addrs = sctp_inet6_supported_addrs,
	.create_accept_sk = sctp_v6_create_accept_sk,
	.addr_to_user  = sctp_v6_addr_to_user,
	.to_sk_saddr   = sctp_v6_to_sk_saddr,
	.to_sk_daddr   = sctp_v6_to_sk_daddr,
	.copy_ip_options = sctp_v6_copy_ip_options,
	.af            = &sctp_af_inet6,
};

/* Initialize IPv6 support and register with socket layer.  */
void sctp_v6_pf_init(void)
{
	/* Register the SCTP specific PF_INET6 functions. */
	sctp_register_pf(&sctp_pf_inet6, PF_INET6);

	/* Register the SCTP specific AF_INET6 functions. */
	sctp_register_af(&sctp_af_inet6);
}

void sctp_v6_pf_exit(void)
{
	list_del(&sctp_af_inet6.list);
}

/* Initialize IPv6 support and register with socket layer.  */
int sctp_v6_protosw_init(void)
{
	int rc;

	rc = proto_register(&sctpv6_prot, 1);
	if (rc)
		return rc;

	/* Add SCTPv6(UDP and TCP style) to inetsw6 linked list. */
	inet6_register_protosw(&sctpv6_seqpacket_protosw);
	inet6_register_protosw(&sctpv6_stream_protosw);

	return 0;
}

void sctp_v6_protosw_exit(void)
{
	inet6_unregister_protosw(&sctpv6_seqpacket_protosw);
	inet6_unregister_protosw(&sctpv6_stream_protosw);
	proto_unregister(&sctpv6_prot);
}


/* Register with inet6 layer. */
int sctp_v6_add_protocol(void)
{
	/* Register notifier for inet6 address additions/deletions. */
	register_inet6addr_notifier(&sctp_inet6addr_notifier);

	if (inet6_add_protocol(&sctpv6_protocol, IPPROTO_SCTP) < 0)
		return -EAGAIN;

	return 0;
}

/* Unregister with inet6 layer. */
void sctp_v6_del_protocol(void)
{
	inet6_del_protocol(&sctpv6_protocol, IPPROTO_SCTP);
	unregister_inet6addr_notifier(&sctp_inet6addr_notifier);
}
