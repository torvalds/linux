/* SCTP kernel reference Implementation
 * (C) Copyright IBM Corp. 2002, 2004
 * Copyright (c) 2001 Nokia, Inc.
 * Copyright (c) 2001 La Monte H.P. Yarroll
 * Copyright (c) 2002-2003 Intel Corp.
 *
 * This file is part of the SCTP kernel reference Implementation
 *
 * SCTP over IPv6.
 *
 * The SCTP reference implementation is free software;
 * you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * The SCTP reference implementation is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *		   ************************
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <lksctp-developers@lists.sourceforge.net>
 *
 * Or submit a bug report through the following website:
 *    http://www.sf.net/projects/lksctp
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
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/sched.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/netdevice.h>
#include <linux/init.h>
#include <linux/ipsec.h>

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

#include <asm/uaccess.h>

static struct notifier_block sctp_inet6addr_notifier = {
	.notifier_call = sctp_inetaddr_event,
};

/* ICMP error handler. */
SCTP_STATIC void sctp_v6_err(struct sk_buff *skb, struct inet6_skb_parm *opt,
			     int type, int code, int offset, __be32 info)
{
	struct inet6_dev *idev;
	struct ipv6hdr *iph = (struct ipv6hdr *)skb->data;
	struct sctphdr *sh = (struct sctphdr *)(skb->data + offset);
	struct sock *sk;
	struct sctp_association *asoc;
	struct sctp_transport *transport;
	struct ipv6_pinfo *np;
	char *saveip, *savesctp;
	int err;

	idev = in6_dev_get(skb->dev);

	/* Fix up skb to look at the embedded net header. */
	saveip = skb->nh.raw;
	savesctp  = skb->h.raw;
	skb->nh.ipv6h = iph;
	skb->h.raw = (char *)sh;
	sk = sctp_err_lookup(AF_INET6, skb, sh, &asoc, &transport);
	/* Put back, the original pointers. */
	skb->nh.raw = saveip;
	skb->h.raw = savesctp;
	if (!sk) {
		ICMP6_INC_STATS_BH(idev, ICMP6_MIB_INERRORS);
		goto out;
	}

	/* Warning:  The sock lock is held.  Remember to call
	 * sctp_err_finish!
	 */

	switch (type) {
	case ICMPV6_PKT_TOOBIG:
		sctp_icmp_frag_needed(sk, asoc, transport, ntohl(info));
		goto out_unlock;
	case ICMPV6_PARAMPROB:
		if (ICMPV6_UNK_NEXTHDR == code) {
			sctp_icmp_proto_unreachable(sk, asoc, transport);
			goto out_unlock;
		}
		break;
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
	sctp_err_finish(sk, asoc);
out:
	if (likely(idev != NULL))
		in6_dev_put(idev);
}

/* Based on tcp_v6_xmit() in tcp_ipv6.c. */
static int sctp_v6_xmit(struct sk_buff *skb, struct sctp_transport *transport,
			int ipfragok)
{
	struct sock *sk = skb->sk;
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct flowi fl;

	memset(&fl, 0, sizeof(fl));

	fl.proto = sk->sk_protocol;

	/* Fill in the dest address from the route entry passed with the skb
	 * and the source address from the transport.
	 */
	ipv6_addr_copy(&fl.fl6_dst, &transport->ipaddr_h.v6.sin6_addr);
	ipv6_addr_copy(&fl.fl6_src, &transport->saddr_h.v6.sin6_addr);

	fl.fl6_flowlabel = np->flow_label;
	IP6_ECN_flow_xmit(sk, fl.fl6_flowlabel);
	if (ipv6_addr_type(&fl.fl6_src) & IPV6_ADDR_LINKLOCAL)
		fl.oif = transport->saddr_h.v6.sin6_scope_id;
	else
		fl.oif = sk->sk_bound_dev_if;
	fl.fl_ip_sport = inet_sk(sk)->sport;
	fl.fl_ip_dport = transport->ipaddr_h.v6.sin6_port;

	if (np->opt && np->opt->srcrt) {
		struct rt0_hdr *rt0 = (struct rt0_hdr *) np->opt->srcrt;
		ipv6_addr_copy(&fl.fl6_dst, rt0->addr);
	}

	SCTP_DEBUG_PRINTK("%s: skb:%p, len:%d, "
			  "src:" NIP6_FMT " dst:" NIP6_FMT "\n",
			  __FUNCTION__, skb, skb->len,
			  NIP6(fl.fl6_src), NIP6(fl.fl6_dst));

	SCTP_INC_STATS(SCTP_MIB_OUTSCTPPACKS);

	return ip6_xmit(sk, skb, &fl, np->opt, ipfragok);
}

/* Returns the dst cache entry for the given source and destination ip
 * addresses.
 */
static struct dst_entry *sctp_v6_get_dst(struct sctp_association *asoc,
					 union sctp_addr *daddr,
					 union sctp_addr *saddr)
{
	struct dst_entry *dst;
	struct flowi fl;

	memset(&fl, 0, sizeof(fl));
	ipv6_addr_copy(&fl.fl6_dst, &daddr->v6.sin6_addr);
	if (ipv6_addr_type(&daddr->v6.sin6_addr) & IPV6_ADDR_LINKLOCAL)
		fl.oif = daddr->v6.sin6_scope_id;
	

	SCTP_DEBUG_PRINTK("%s: DST=" NIP6_FMT " ",
			  __FUNCTION__, NIP6(fl.fl6_dst));

	if (saddr) {
		ipv6_addr_copy(&fl.fl6_src, &saddr->v6.sin6_addr);
		SCTP_DEBUG_PRINTK(
			"SRC=" NIP6_FMT " - ",
			NIP6(fl.fl6_src));
	}

	dst = ip6_route_output(NULL, &fl);
	if (!dst->error) {
		struct rt6_info *rt;
		rt = (struct rt6_info *)dst;
		SCTP_DEBUG_PRINTK(
			"rt6_dst:" NIP6_FMT " rt6_src:" NIP6_FMT "\n",
			NIP6(rt->rt6i_dst.addr), NIP6(rt->rt6i_src.addr));
		return dst;
	}
	SCTP_DEBUG_PRINTK("NO ROUTE\n");
	dst_release(dst);
	return NULL;
}

/* Returns the number of consecutive initial bits that match in the 2 ipv6
 * addresses.
 */
static inline int sctp_v6_addr_match_len(union sctp_addr *s1,
					 union sctp_addr *s2)
{
	struct in6_addr *a1 = &s1->v6.sin6_addr;
	struct in6_addr *a2 = &s2->v6.sin6_addr;
	int i, j;

	for (i = 0; i < 4 ; i++) {
		__be32 a1xora2;

		a1xora2 = a1->s6_addr32[i] ^ a2->s6_addr32[i];

		if ((j = fls(ntohl(a1xora2))))
			return (i * 32 + 32 - j);
	}

	return (i*32);
}

/* Fills in the source address(saddr) based on the destination address(daddr)
 * and asoc's bind address list.
 */
static void sctp_v6_get_saddr(struct sctp_association *asoc,
			      struct dst_entry *dst,
			      union sctp_addr *daddr,
			      union sctp_addr *saddr)
{
	struct sctp_bind_addr *bp;
	rwlock_t *addr_lock;
	struct sctp_sockaddr_entry *laddr;
	struct list_head *pos;
	sctp_scope_t scope;
	union sctp_addr *baddr = NULL;
	__u8 matchlen = 0;
	__u8 bmatchlen;

	SCTP_DEBUG_PRINTK("%s: asoc:%p dst:%p "
			  "daddr:" NIP6_FMT " ",
			  __FUNCTION__, asoc, dst, NIP6(daddr->v6.sin6_addr));

	if (!asoc) {
		ipv6_get_saddr(dst, &daddr->v6.sin6_addr,&saddr->v6.sin6_addr);
		SCTP_DEBUG_PRINTK("saddr from ipv6_get_saddr: " NIP6_FMT "\n",
				  NIP6(saddr->v6.sin6_addr));
		return;
	}

	scope = sctp_scope(daddr);

	bp = &asoc->base.bind_addr;
	addr_lock = &asoc->base.addr_lock;

	/* Go through the bind address list and find the best source address
	 * that matches the scope of the destination address.
	 */
	sctp_read_lock(addr_lock);
	list_for_each(pos, &bp->address_list) {
		laddr = list_entry(pos, struct sctp_sockaddr_entry, list);
		if ((laddr->use_as_src) &&
		    (laddr->a_h.sa.sa_family == AF_INET6) &&
		    (scope <= sctp_scope(&laddr->a_h))) {
			bmatchlen = sctp_v6_addr_match_len(daddr, &laddr->a_h);
			if (!baddr || (matchlen < bmatchlen)) {
				baddr = &laddr->a_h;
				matchlen = bmatchlen;
			}
		}
	}

	if (baddr) {
		memcpy(saddr, baddr, sizeof(union sctp_addr));
		SCTP_DEBUG_PRINTK("saddr: " NIP6_FMT "\n",
				  NIP6(saddr->v6.sin6_addr));
	} else {
		printk(KERN_ERR "%s: asoc:%p Could not find a valid source "
		       "address for the dest:" NIP6_FMT "\n",
		       __FUNCTION__, asoc, NIP6(daddr->v6.sin6_addr));
	}

	sctp_read_unlock(addr_lock);
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

	read_lock(&in6_dev->lock);
	for (ifp = in6_dev->addr_list; ifp; ifp = ifp->if_next) {
		/* Add the address to the local list.  */
		addr = t_new(struct sctp_sockaddr_entry, GFP_ATOMIC);
		if (addr) {
			addr->a.v6.sin6_family = AF_INET6;
			addr->a.v6.sin6_port = 0;
			addr->a.v6.sin6_addr = ifp->addr;
			addr->a.v6.sin6_scope_id = dev->ifindex;
			addr->a_h = addr->a;
			INIT_LIST_HEAD(&addr->list);
			list_add_tail(&addr->list, addrlist);
		}
	}

	read_unlock(&in6_dev->lock);
	rcu_read_unlock();
}

/* Initialize a sockaddr_storage from in incoming skb. */
static void sctp_v6_from_skb(union sctp_addr *addr,struct sk_buff *skb,
			     int is_saddr)
{
	void *from;
	__be16 *port;
	struct sctphdr *sh;

	port = &addr->v6.sin6_port;
	addr->v6.sin6_family = AF_INET6;
	addr->v6.sin6_flowinfo = 0; /* FIXME */
	addr->v6.sin6_scope_id = ((struct inet6_skb_parm *)skb->cb)->iif;

	sh = (struct sctphdr *) skb->h.raw;
	if (is_saddr) {
		*port  = sh->source;
		from = &skb->nh.ipv6h->saddr;
	} else {
		*port = sh->dest;
		from = &skb->nh.ipv6h->daddr;
	}
	ipv6_addr_copy(&addr->v6.sin6_addr, from);
}

/* Initialize an sctp_addr from a socket. */
static void sctp_v6_from_sk(union sctp_addr *addr, struct sock *sk)
{
	addr->v6.sin6_family = AF_INET6;
	addr->v6.sin6_port = inet_sk(sk)->num;
	addr->v6.sin6_addr = inet6_sk(sk)->rcv_saddr;
}

/* Initialize sk->sk_rcv_saddr from sctp_addr. */
static void sctp_v6_to_sk_saddr(union sctp_addr *addr, struct sock *sk)
{
	if (addr->sa.sa_family == AF_INET && sctp_sk(sk)->v4mapped) {
		inet6_sk(sk)->rcv_saddr.s6_addr32[0] = 0;
		inet6_sk(sk)->rcv_saddr.s6_addr32[1] = 0;
		inet6_sk(sk)->rcv_saddr.s6_addr32[2] = htonl(0x0000ffff);
		inet6_sk(sk)->rcv_saddr.s6_addr32[3] =
			addr->v4.sin_addr.s_addr;
	} else {
		inet6_sk(sk)->rcv_saddr = addr->v6.sin6_addr;
	}
}

/* Initialize sk->sk_daddr from sctp_addr. */
static void sctp_v6_to_sk_daddr(union sctp_addr *addr, struct sock *sk)
{
	if (addr->sa.sa_family == AF_INET && sctp_sk(sk)->v4mapped) {
		inet6_sk(sk)->daddr.s6_addr32[0] = 0;
		inet6_sk(sk)->daddr.s6_addr32[1] = 0;
		inet6_sk(sk)->daddr.s6_addr32[2] = htonl(0x0000ffff);
		inet6_sk(sk)->daddr.s6_addr32[3] = addr->v4.sin_addr.s_addr;
	} else {
		inet6_sk(sk)->daddr = addr->v6.sin6_addr;
	}
}

/* Initialize a sctp_addr from an address parameter. */
static void sctp_v6_from_addr_param(union sctp_addr *addr,
				    union sctp_addr_param *param,
				    __u16 port, int iif)
{
	addr->v6.sin6_family = AF_INET6;
	addr->v6.sin6_port = port;
	addr->v6.sin6_flowinfo = 0; /* BUG */
	ipv6_addr_copy(&addr->v6.sin6_addr, &param->v6.addr);
	addr->v6.sin6_scope_id = iif;
}

/* Initialize an address parameter from a sctp_addr and return the length
 * of the address parameter.
 */
static int sctp_v6_to_addr_param(const union sctp_addr *addr,
				 union sctp_addr_param *param)
{
	int length = sizeof(sctp_ipv6addr_param_t);

	param->v6.param_hdr.type = SCTP_PARAM_IPV6_ADDRESS;
	param->v6.param_hdr.length = htons(length);
	ipv6_addr_copy(&param->v6.addr, &addr->v6.sin6_addr);

	return length;
}

/* Initialize a sctp_addr from a dst_entry. */
static void sctp_v6_dst_saddr(union sctp_addr *addr, struct dst_entry *dst,
			      __be16 port)
{
	struct rt6_info *rt = (struct rt6_info *)dst;
	addr->sa.sa_family = AF_INET6;
	addr->v6.sin6_port = port;
	ipv6_addr_copy(&addr->v6.sin6_addr, &rt->rt6i_src.addr);
}

/* Compare addresses exactly.
 * v4-mapped-v6 is also in consideration.
 */
static int sctp_v6_cmp_addr(const union sctp_addr *addr1,
			    const union sctp_addr *addr2)
{
	if (addr1->sa.sa_family != addr2->sa.sa_family) {
		if (addr1->sa.sa_family == AF_INET &&
		    addr2->sa.sa_family == AF_INET6 &&
		    IPV6_ADDR_MAPPED == ipv6_addr_type(&addr2->v6.sin6_addr)) {
			if (addr2->v6.sin6_port == addr1->v4.sin_port &&
			    addr2->v6.sin6_addr.s6_addr32[3] ==
			    addr1->v4.sin_addr.s_addr)
				return 1;
		}
		if (addr2->sa.sa_family == AF_INET &&
		    addr1->sa.sa_family == AF_INET6 &&
		    IPV6_ADDR_MAPPED == ipv6_addr_type(&addr1->v6.sin6_addr)) {
			if (addr1->v6.sin6_port == addr2->v4.sin_port &&
			    addr1->v6.sin6_addr.s6_addr32[3] ==
			    addr2->v4.sin_addr.s_addr)
				return 1;
		}
		return 0;
	}
	if (!ipv6_addr_equal(&addr1->v6.sin6_addr, &addr2->v6.sin6_addr))
		return 0;
	/* If this is a linklocal address, compare the scope_id. */
	if (ipv6_addr_type(&addr1->v6.sin6_addr) & IPV6_ADDR_LINKLOCAL) {
		if (addr1->v6.sin6_scope_id && addr2->v6.sin6_scope_id &&
		    (addr1->v6.sin6_scope_id != addr2->v6.sin6_scope_id)) {
			return 0;
		}
	}

	return 1;
}

/* Initialize addr struct to INADDR_ANY. */
static void sctp_v6_inaddr_any(union sctp_addr *addr, unsigned short port)
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
	struct in6_addr *in6 = (struct in6_addr *)&addr->v6.sin6_addr;

	type = ipv6_addr_type(in6);
	if (IPV6_ADDR_ANY == type)
		return 1;
	if (type == IPV6_ADDR_MAPPED) {
		if (sp && !sp->v4mapped)
			return 0;
		if (sp && ipv6_only_sock(sctp_opt2sk(sp)))
			return 0;
		sctp_v6_map_v4(addr);
		return sctp_get_af_specific(AF_INET)->available(addr, sp);
	}
	if (!(type & IPV6_ADDR_UNICAST))
		return 0;

	return ipv6_chk_addr(in6, NULL, 0);
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
		if (!sp || !sp->v4mapped)
			return 0;
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
static sctp_scope_t sctp_v6_scope(union sctp_addr *addr)
{
	int v6scope;
	sctp_scope_t retval;

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
	};

	return retval;
}

/* Create and initialize a new sk for the socket to be returned by accept(). */
static struct sock *sctp_v6_create_accept_sk(struct sock *sk,
					     struct sctp_association *asoc)
{
	struct inet_sock *inet = inet_sk(sk);
	struct sock *newsk;
	struct inet_sock *newinet;
	struct ipv6_pinfo *newnp, *np = inet6_sk(sk);
	struct sctp6_sock *newsctp6sk;

	newsk = sk_alloc(PF_INET6, GFP_KERNEL, sk->sk_prot, 1);
	if (!newsk)
		goto out;

	sock_init_data(NULL, newsk);

	newsk->sk_type = SOCK_STREAM;

	newsk->sk_prot = sk->sk_prot;
	newsk->sk_no_check = sk->sk_no_check;
	newsk->sk_reuse = sk->sk_reuse;

	newsk->sk_destruct = inet_sock_destruct;
	newsk->sk_family = PF_INET6;
	newsk->sk_protocol = IPPROTO_SCTP;
	newsk->sk_backlog_rcv = sk->sk_prot->backlog_rcv;
	newsk->sk_shutdown = sk->sk_shutdown;
	sock_reset_flag(sk, SOCK_ZAPPED);

	newsctp6sk = (struct sctp6_sock *)newsk;
	inet_sk(newsk)->pinet6 = &newsctp6sk->inet6;

	newinet = inet_sk(newsk);
	newnp = inet6_sk(newsk);

	memcpy(newnp, np, sizeof(struct ipv6_pinfo));

	/* Initialize sk's sport, dport, rcv_saddr and daddr for getsockname()
	 * and getpeername().
	 */
	newinet->sport = inet->sport;
	newnp->saddr = np->saddr;
	newnp->rcv_saddr = np->rcv_saddr;
	newinet->dport = htons(asoc->peer.port);
	sctp_v6_to_sk_daddr(&asoc->peer.primary_addr, newsk);

	/* Init the ipv4 part of the socket since we can have sockets
	 * using v6 API for ipv4.
	 */
	newinet->uc_ttl = -1;
	newinet->mc_loop = 1;
	newinet->mc_ttl = 1;
	newinet->mc_index = 0;
	newinet->mc_list = NULL;

	if (ipv4_config.no_pmtu_disc)
		newinet->pmtudisc = IP_PMTUDISC_DONT;
	else
		newinet->pmtudisc = IP_PMTUDISC_WANT;

	sk_refcnt_debug_inc(newsk);

	if (newsk->sk_prot->init(newsk)) {
		sk_common_release(newsk);
		newsk = NULL;
	}

out:
	return newsk;
}

/* Map v4 address to mapped v6 address */
static void sctp_v6_addr_v4map(struct sctp_sock *sp, union sctp_addr *addr)
{
	if (sp->v4mapped && AF_INET == addr->sa.sa_family)
		sctp_v4_map_v6(addr);
}

/* Where did this skb come from?  */
static int sctp_v6_skb_iif(const struct sk_buff *skb)
{
	struct inet6_skb_parm *opt = (struct inet6_skb_parm *) skb->cb;
	return opt->iif;
}

/* Was this packet marked by Explicit Congestion Notification? */
static int sctp_v6_is_ce(const struct sk_buff *skb)
{
	return *((__u32 *)(skb->nh.ipv6h)) & htonl(1<<20);
}

/* Dump the v6 addr to the seq file. */
static void sctp_v6_seq_dump_addr(struct seq_file *seq, union sctp_addr *addr)
{
	seq_printf(seq, NIP6_FMT " ", NIP6(addr->v6.sin6_addr));
}

/* Initialize a PF_INET6 socket msg_name. */
static void sctp_inet6_msgname(char *msgname, int *addr_len)
{
	struct sockaddr_in6 *sin6;

	sin6 = (struct sockaddr_in6 *)msgname;
	sin6->sin6_family = AF_INET6;
	sin6->sin6_flowinfo = 0;
	sin6->sin6_scope_id = 0; /*FIXME */
	*addr_len = sizeof(struct sockaddr_in6);
}

/* Initialize a PF_INET msgname from a ulpevent. */
static void sctp_inet6_event_msgname(struct sctp_ulpevent *event,
				     char *msgname, int *addrlen)
{
	struct sockaddr_in6 *sin6, *sin6from;

	if (msgname) {
		union sctp_addr *addr;
		struct sctp_association *asoc;

		asoc = event->asoc;
		sctp_inet6_msgname(msgname, addrlen);
		sin6 = (struct sockaddr_in6 *)msgname;
		sin6->sin6_port = htons(asoc->peer.port);
		addr = &asoc->peer.primary_addr;

		/* Note: If we go to a common v6 format, this code
		 * will change.
		 */

		/* Map ipv4 address into v4-mapped-on-v6 address.  */
		if (sctp_sk(asoc->base.sk)->v4mapped &&
		    AF_INET == addr->sa.sa_family) {
			sctp_v4_map_v6((union sctp_addr *)sin6);
			sin6->sin6_addr.s6_addr32[3] =
				addr->v4.sin_addr.s_addr;
			return;
		}

		sin6from = &asoc->peer.primary_addr.v6;
		ipv6_addr_copy(&sin6->sin6_addr, &sin6from->sin6_addr);
		if (ipv6_addr_type(&sin6->sin6_addr) & IPV6_ADDR_LINKLOCAL)
			sin6->sin6_scope_id = sin6from->sin6_scope_id;
	}
}

/* Initialize a msg_name from an inbound skb. */
static void sctp_inet6_skb_msgname(struct sk_buff *skb, char *msgname,
				   int *addr_len)
{
	struct sctphdr *sh;
	struct sockaddr_in6 *sin6;

	if (msgname) {
		sctp_inet6_msgname(msgname, addr_len);
		sin6 = (struct sockaddr_in6 *)msgname;
		sh = (struct sctphdr *)skb->h.raw;
		sin6->sin6_port = sh->source;

		/* Map ipv4 address into v4-mapped-on-v6 address. */
		if (sctp_sk(skb->sk)->v4mapped &&
		    skb->nh.iph->version == 4) {
			sctp_v4_map_v6((union sctp_addr *)sin6);
			sin6->sin6_addr.s6_addr32[3] = skb->nh.iph->saddr;
			return;
		}

		/* Otherwise, just copy the v6 address. */
		ipv6_addr_copy(&sin6->sin6_addr, &skb->nh.ipv6h->saddr);
		if (ipv6_addr_type(&sin6->sin6_addr) & IPV6_ADDR_LINKLOCAL) {
			struct sctp_ulpevent *ev = sctp_skb2event(skb);
			sin6->sin6_scope_id = ev->iif;
		}
	}
}

/* Do we support this AF? */
static int sctp_inet6_af_supported(sa_family_t family, struct sctp_sock *sp)
{
	switch (family) {
	case AF_INET6:
		return 1;
	/* v4-mapped-v6 addresses */
	case AF_INET:
		if (!__ipv6_only_sock(sctp_opt2sk(sp)) && sp->v4mapped)
			return 1;
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
	struct sctp_af *af1, *af2;

	af1 = sctp_get_af_specific(addr1->sa.sa_family);
	af2 = sctp_get_af_specific(addr2->sa.sa_family);

	if (!af1 || !af2)
		return 0;
	/* Today, wildcard AF_INET/AF_INET6. */
	if (sctp_is_any(addr1) || sctp_is_any(addr2))
		return 1;

	if (addr1->sa.sa_family != addr2->sa.sa_family)
		return 0;

	return af1->cmp_addr(addr1, addr2);
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
			if (!addr->v6.sin6_scope_id)
				return 0;
			dev = dev_get_by_index(addr->v6.sin6_scope_id);
			if (!dev)
				return 0;
			dev_put(dev);
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
			dev = dev_get_by_index(addr->v6.sin6_scope_id);
			if (!dev)
				return 0;
			dev_put(dev);
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
				      __u16 *types)
{
	types[0] = SCTP_PARAM_IPV4_ADDRESS;
	types[1] = SCTP_PARAM_IPV6_ADDRESS;
	return 2;
}

static const struct proto_ops inet6_seqpacket_ops = {
	.family		   = PF_INET6,
	.owner		   = THIS_MODULE,
	.release	   = inet6_release,
	.bind		   = inet6_bind,
	.connect	   = inet_dgram_connect,
	.socketpair	   = sock_no_socketpair,
	.accept		   = inet_accept,
	.getname	   = inet6_getname,
	.poll		   = sctp_poll,
	.ioctl		   = inet6_ioctl,
	.listen		   = sctp_inet_listen,
	.shutdown	   = inet_shutdown,
	.setsockopt	   = sock_common_setsockopt,
	.getsockopt	   = sock_common_getsockopt,
	.sendmsg	   = inet_sendmsg,
	.recvmsg	   = sock_common_recvmsg,
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
	.capability    = -1,
	.no_check      = 0,
	.flags         = SCTP_PROTOSW_FLAG
};
static struct inet_protosw sctpv6_stream_protosw = {
	.type          = SOCK_STREAM,
	.protocol      = IPPROTO_SCTP,
	.prot 	       = &sctpv6_prot,
	.ops           = &inet6_seqpacket_ops,
	.capability    = -1,
	.no_check      = 0,
	.flags         = SCTP_PROTOSW_FLAG,
};

static int sctp6_rcv(struct sk_buff **pskb)
{
	return sctp_rcv(*pskb) ? -1 : 0;
}

static struct inet6_protocol sctpv6_protocol = {
	.handler      = sctp6_rcv,
	.err_handler  = sctp_v6_err,
	.flags        = INET6_PROTO_NOPOLICY | INET6_PROTO_FINAL,
};

static struct sctp_af sctp_ipv6_specific = {
	.sa_family	   = AF_INET6,
	.sctp_xmit	   = sctp_v6_xmit,
	.setsockopt	   = ipv6_setsockopt,
	.getsockopt	   = ipv6_getsockopt,
	.get_dst	   = sctp_v6_get_dst,
	.get_saddr	   = sctp_v6_get_saddr,
	.copy_addrlist	   = sctp_v6_copy_addrlist,
	.from_skb	   = sctp_v6_from_skb,
	.from_sk	   = sctp_v6_from_sk,
	.to_sk_saddr	   = sctp_v6_to_sk_saddr,
	.to_sk_daddr	   = sctp_v6_to_sk_daddr,
	.from_addr_param   = sctp_v6_from_addr_param,
	.to_addr_param	   = sctp_v6_to_addr_param,
	.dst_saddr	   = sctp_v6_dst_saddr,
	.cmp_addr	   = sctp_v6_cmp_addr,
	.scope		   = sctp_v6_scope,
	.addr_valid	   = sctp_v6_addr_valid,
	.inaddr_any	   = sctp_v6_inaddr_any,
	.is_any		   = sctp_v6_is_any,
	.available	   = sctp_v6_available,
	.skb_iif	   = sctp_v6_skb_iif,
	.is_ce		   = sctp_v6_is_ce,
	.seq_dump_addr	   = sctp_v6_seq_dump_addr,
	.net_header_len	   = sizeof(struct ipv6hdr),
	.sockaddr_len	   = sizeof(struct sockaddr_in6),
#ifdef CONFIG_COMPAT
	.compat_setsockopt = compat_ipv6_setsockopt,
	.compat_getsockopt = compat_ipv6_getsockopt,
#endif
};

static struct sctp_pf sctp_pf_inet6_specific = {
	.event_msgname = sctp_inet6_event_msgname,
	.skb_msgname   = sctp_inet6_skb_msgname,
	.af_supported  = sctp_inet6_af_supported,
	.cmp_addr      = sctp_inet6_cmp_addr,
	.bind_verify   = sctp_inet6_bind_verify,
	.send_verify   = sctp_inet6_send_verify,
	.supported_addrs = sctp_inet6_supported_addrs,
	.create_accept_sk = sctp_v6_create_accept_sk,
	.addr_v4map    = sctp_v6_addr_v4map,
	.af            = &sctp_ipv6_specific,
};

/* Initialize IPv6 support and register with inet6 stack.  */
int sctp_v6_init(void)
{
	int rc = proto_register(&sctpv6_prot, 1);

	if (rc)
		goto out;
	/* Register inet6 protocol. */
	rc = -EAGAIN;
	if (inet6_add_protocol(&sctpv6_protocol, IPPROTO_SCTP) < 0)
		goto out_unregister_sctp_proto;

	/* Add SCTPv6(UDP and TCP style) to inetsw6 linked list. */
	inet6_register_protosw(&sctpv6_seqpacket_protosw);
	inet6_register_protosw(&sctpv6_stream_protosw);

	/* Register the SCTP specific PF_INET6 functions. */
	sctp_register_pf(&sctp_pf_inet6_specific, PF_INET6);

	/* Register the SCTP specific AF_INET6 functions. */
	sctp_register_af(&sctp_ipv6_specific);

	/* Register notifier for inet6 address additions/deletions. */
	register_inet6addr_notifier(&sctp_inet6addr_notifier);
	rc = 0;
out:
	return rc;
out_unregister_sctp_proto:
	proto_unregister(&sctpv6_prot);
	goto out;
}

/* IPv6 specific exit support. */
void sctp_v6_exit(void)
{
	list_del(&sctp_ipv6_specific.list);
	inet6_del_protocol(&sctpv6_protocol, IPPROTO_SCTP);
	inet6_unregister_protosw(&sctpv6_seqpacket_protosw);
	inet6_unregister_protosw(&sctpv6_stream_protosw);
	unregister_inet6addr_notifier(&sctp_inet6addr_notifier);
	proto_unregister(&sctpv6_prot);
}
