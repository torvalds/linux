/*
 *	common UDP/RAW code
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in6.h>
#include <linux/ipv6.h>
#include <linux/route.h>

#include <net/ipv6.h>
#include <net/ndisc.h>
#include <net/addrconf.h>
#include <net/transp_v6.h>
#include <net/ip6_route.h>
#include <net/tcp_states.h>

#include <linux/errqueue.h>
#include <asm/uaccess.h>

int ip6_datagram_connect(struct sock *sk, struct sockaddr *uaddr, int addr_len)
{
	struct sockaddr_in6	*usin = (struct sockaddr_in6 *) uaddr;
	struct inet_sock      	*inet = inet_sk(sk);
	struct ipv6_pinfo      	*np = inet6_sk(sk);
	struct in6_addr		*daddr, *final_p = NULL, final;
	struct dst_entry	*dst;
	struct flowi		fl;
	struct ip6_flowlabel	*flowlabel = NULL;
	int			addr_type;
	int			err;

	if (usin->sin6_family == AF_INET) {
		if (__ipv6_only_sock(sk))
			return -EAFNOSUPPORT;
		err = ip4_datagram_connect(sk, uaddr, addr_len);
		goto ipv4_connected;
	}

	if (addr_len < SIN6_LEN_RFC2133)
		return -EINVAL;

	if (usin->sin6_family != AF_INET6)
		return -EAFNOSUPPORT;

	memset(&fl, 0, sizeof(fl));
	if (np->sndflow) {
		fl.fl6_flowlabel = usin->sin6_flowinfo&IPV6_FLOWINFO_MASK;
		if (fl.fl6_flowlabel&IPV6_FLOWLABEL_MASK) {
			flowlabel = fl6_sock_lookup(sk, fl.fl6_flowlabel);
			if (flowlabel == NULL)
				return -EINVAL;
			ipv6_addr_copy(&usin->sin6_addr, &flowlabel->dst);
		}
	}

	addr_type = ipv6_addr_type(&usin->sin6_addr);

	if (addr_type == IPV6_ADDR_ANY) {
		/*
		 *	connect to self
		 */
		usin->sin6_addr.s6_addr[15] = 0x01;
	}

	daddr = &usin->sin6_addr;

	if (addr_type == IPV6_ADDR_MAPPED) {
		struct sockaddr_in sin;

		if (__ipv6_only_sock(sk)) {
			err = -ENETUNREACH;
			goto out;
		}
		sin.sin_family = AF_INET;
		sin.sin_addr.s_addr = daddr->s6_addr32[3];
		sin.sin_port = usin->sin6_port;

		err = ip4_datagram_connect(sk,
					   (struct sockaddr*) &sin,
					   sizeof(sin));

ipv4_connected:
		if (err)
			goto out;

		ipv6_addr_set(&np->daddr, 0, 0, htonl(0x0000ffff), inet->daddr);

		if (ipv6_addr_any(&np->saddr)) {
			ipv6_addr_set(&np->saddr, 0, 0, htonl(0x0000ffff),
				      inet->saddr);
		}

		if (ipv6_addr_any(&np->rcv_saddr)) {
			ipv6_addr_set(&np->rcv_saddr, 0, 0, htonl(0x0000ffff),
				      inet->rcv_saddr);
		}
		goto out;
	}

	if (addr_type&IPV6_ADDR_LINKLOCAL) {
		if (addr_len >= sizeof(struct sockaddr_in6) &&
		    usin->sin6_scope_id) {
			if (sk->sk_bound_dev_if &&
			    sk->sk_bound_dev_if != usin->sin6_scope_id) {
				err = -EINVAL;
				goto out;
			}
			sk->sk_bound_dev_if = usin->sin6_scope_id;
		}

		if (!sk->sk_bound_dev_if && (addr_type & IPV6_ADDR_MULTICAST))
			sk->sk_bound_dev_if = np->mcast_oif;

		/* Connect to link-local address requires an interface */
		if (!sk->sk_bound_dev_if) {
			err = -EINVAL;
			goto out;
		}
	}

	ipv6_addr_copy(&np->daddr, daddr);
	np->flow_label = fl.fl6_flowlabel;

	inet->dport = usin->sin6_port;

	/*
	 *	Check for a route to destination an obtain the
	 *	destination cache for it.
	 */

	fl.proto = sk->sk_protocol;
	ipv6_addr_copy(&fl.fl6_dst, &np->daddr);
	ipv6_addr_copy(&fl.fl6_src, &np->saddr);
	fl.oif = sk->sk_bound_dev_if;
	fl.fl_ip_dport = inet->dport;
	fl.fl_ip_sport = inet->sport;

	if (!fl.oif && (addr_type&IPV6_ADDR_MULTICAST))
		fl.oif = np->mcast_oif;

	security_sk_classify_flow(sk, &fl);

	if (flowlabel) {
		if (flowlabel->opt && flowlabel->opt->srcrt) {
			struct rt0_hdr *rt0 = (struct rt0_hdr *) flowlabel->opt->srcrt;
			ipv6_addr_copy(&final, &fl.fl6_dst);
			ipv6_addr_copy(&fl.fl6_dst, rt0->addr);
			final_p = &final;
		}
	} else if (np->opt && np->opt->srcrt) {
		struct rt0_hdr *rt0 = (struct rt0_hdr *)np->opt->srcrt;
		ipv6_addr_copy(&final, &fl.fl6_dst);
		ipv6_addr_copy(&fl.fl6_dst, rt0->addr);
		final_p = &final;
	}

	err = ip6_dst_lookup(sk, &dst, &fl);
	if (err)
		goto out;
	if (final_p)
		ipv6_addr_copy(&fl.fl6_dst, final_p);

	err = __xfrm_lookup(sock_net(sk), &dst, &fl, sk, XFRM_LOOKUP_WAIT);
	if (err < 0) {
		if (err == -EREMOTE)
			err = ip6_dst_blackhole(sk, &dst, &fl);
		if (err < 0)
			goto out;
	}

	/* source address lookup done in ip6_dst_lookup */

	if (ipv6_addr_any(&np->saddr))
		ipv6_addr_copy(&np->saddr, &fl.fl6_src);

	if (ipv6_addr_any(&np->rcv_saddr)) {
		ipv6_addr_copy(&np->rcv_saddr, &fl.fl6_src);
		inet->rcv_saddr = LOOPBACK4_IPV6;
	}

	ip6_dst_store(sk, dst,
		      ipv6_addr_equal(&fl.fl6_dst, &np->daddr) ?
		      &np->daddr : NULL,
#ifdef CONFIG_IPV6_SUBTREES
		      ipv6_addr_equal(&fl.fl6_src, &np->saddr) ?
		      &np->saddr :
#endif
		      NULL);

	sk->sk_state = TCP_ESTABLISHED;
out:
	fl6_sock_release(flowlabel);
	return err;
}

void ipv6_icmp_error(struct sock *sk, struct sk_buff *skb, int err,
		     __be16 port, u32 info, u8 *payload)
{
	struct ipv6_pinfo *np  = inet6_sk(sk);
	struct icmp6hdr *icmph = icmp6_hdr(skb);
	struct sock_exterr_skb *serr;

	if (!np->recverr)
		return;

	skb = skb_clone(skb, GFP_ATOMIC);
	if (!skb)
		return;

	serr = SKB_EXT_ERR(skb);
	serr->ee.ee_errno = err;
	serr->ee.ee_origin = SO_EE_ORIGIN_ICMP6;
	serr->ee.ee_type = icmph->icmp6_type;
	serr->ee.ee_code = icmph->icmp6_code;
	serr->ee.ee_pad = 0;
	serr->ee.ee_info = info;
	serr->ee.ee_data = 0;
	serr->addr_offset = (u8 *)&(((struct ipv6hdr *)(icmph + 1))->daddr) -
				  skb_network_header(skb);
	serr->port = port;

	__skb_pull(skb, payload - skb->data);
	skb_reset_transport_header(skb);

	if (sock_queue_err_skb(sk, skb))
		kfree_skb(skb);
}

void ipv6_local_error(struct sock *sk, int err, struct flowi *fl, u32 info)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct sock_exterr_skb *serr;
	struct ipv6hdr *iph;
	struct sk_buff *skb;

	if (!np->recverr)
		return;

	skb = alloc_skb(sizeof(struct ipv6hdr), GFP_ATOMIC);
	if (!skb)
		return;

	skb_put(skb, sizeof(struct ipv6hdr));
	skb_reset_network_header(skb);
	iph = ipv6_hdr(skb);
	ipv6_addr_copy(&iph->daddr, &fl->fl6_dst);

	serr = SKB_EXT_ERR(skb);
	serr->ee.ee_errno = err;
	serr->ee.ee_origin = SO_EE_ORIGIN_LOCAL;
	serr->ee.ee_type = 0;
	serr->ee.ee_code = 0;
	serr->ee.ee_pad = 0;
	serr->ee.ee_info = info;
	serr->ee.ee_data = 0;
	serr->addr_offset = (u8 *)&iph->daddr - skb_network_header(skb);
	serr->port = fl->fl_ip_dport;

	__skb_pull(skb, skb_tail_pointer(skb) - skb->data);
	skb_reset_transport_header(skb);

	if (sock_queue_err_skb(sk, skb))
		kfree_skb(skb);
}

/*
 *	Handle MSG_ERRQUEUE
 */
int ipv6_recv_error(struct sock *sk, struct msghdr *msg, int len)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct sock_exterr_skb *serr;
	struct sk_buff *skb, *skb2;
	struct sockaddr_in6 *sin;
	struct {
		struct sock_extended_err ee;
		struct sockaddr_in6	 offender;
	} errhdr;
	int err;
	int copied;

	err = -EAGAIN;
	skb = skb_dequeue(&sk->sk_error_queue);
	if (skb == NULL)
		goto out;

	copied = skb->len;
	if (copied > len) {
		msg->msg_flags |= MSG_TRUNC;
		copied = len;
	}
	err = skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);
	if (err)
		goto out_free_skb;

	sock_recv_timestamp(msg, sk, skb);

	serr = SKB_EXT_ERR(skb);

	sin = (struct sockaddr_in6 *)msg->msg_name;
	if (sin) {
		const unsigned char *nh = skb_network_header(skb);
		sin->sin6_family = AF_INET6;
		sin->sin6_flowinfo = 0;
		sin->sin6_port = serr->port;
		sin->sin6_scope_id = 0;
		if (serr->ee.ee_origin == SO_EE_ORIGIN_ICMP6) {
			ipv6_addr_copy(&sin->sin6_addr,
				  (struct in6_addr *)(nh + serr->addr_offset));
			if (np->sndflow)
				sin->sin6_flowinfo =
					(*(__be32 *)(nh + serr->addr_offset - 24) &
					 IPV6_FLOWINFO_MASK);
			if (ipv6_addr_type(&sin->sin6_addr) & IPV6_ADDR_LINKLOCAL)
				sin->sin6_scope_id = IP6CB(skb)->iif;
		} else {
			ipv6_addr_set(&sin->sin6_addr, 0, 0,
				      htonl(0xffff),
				      *(__be32 *)(nh + serr->addr_offset));
		}
	}

	memcpy(&errhdr.ee, &serr->ee, sizeof(struct sock_extended_err));
	sin = &errhdr.offender;
	sin->sin6_family = AF_UNSPEC;
	if (serr->ee.ee_origin != SO_EE_ORIGIN_LOCAL) {
		sin->sin6_family = AF_INET6;
		sin->sin6_flowinfo = 0;
		sin->sin6_scope_id = 0;
		if (serr->ee.ee_origin == SO_EE_ORIGIN_ICMP6) {
			ipv6_addr_copy(&sin->sin6_addr, &ipv6_hdr(skb)->saddr);
			if (np->rxopt.all)
				datagram_recv_ctl(sk, msg, skb);
			if (ipv6_addr_type(&sin->sin6_addr) & IPV6_ADDR_LINKLOCAL)
				sin->sin6_scope_id = IP6CB(skb)->iif;
		} else {
			struct inet_sock *inet = inet_sk(sk);

			ipv6_addr_set(&sin->sin6_addr, 0, 0,
				      htonl(0xffff), ip_hdr(skb)->saddr);
			if (inet->cmsg_flags)
				ip_cmsg_recv(msg, skb);
		}
	}

	put_cmsg(msg, SOL_IPV6, IPV6_RECVERR, sizeof(errhdr), &errhdr);

	/* Now we could try to dump offended packet options */

	msg->msg_flags |= MSG_ERRQUEUE;
	err = copied;

	/* Reset and regenerate socket error */
	spin_lock_bh(&sk->sk_error_queue.lock);
	sk->sk_err = 0;
	if ((skb2 = skb_peek(&sk->sk_error_queue)) != NULL) {
		sk->sk_err = SKB_EXT_ERR(skb2)->ee.ee_errno;
		spin_unlock_bh(&sk->sk_error_queue.lock);
		sk->sk_error_report(sk);
	} else {
		spin_unlock_bh(&sk->sk_error_queue.lock);
	}

out_free_skb:
	kfree_skb(skb);
out:
	return err;
}



int datagram_recv_ctl(struct sock *sk, struct msghdr *msg, struct sk_buff *skb)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct inet6_skb_parm *opt = IP6CB(skb);
	unsigned char *nh = skb_network_header(skb);

	if (np->rxopt.bits.rxinfo) {
		struct in6_pktinfo src_info;

		src_info.ipi6_ifindex = opt->iif;
		ipv6_addr_copy(&src_info.ipi6_addr, &ipv6_hdr(skb)->daddr);
		put_cmsg(msg, SOL_IPV6, IPV6_PKTINFO, sizeof(src_info), &src_info);
	}

	if (np->rxopt.bits.rxhlim) {
		int hlim = ipv6_hdr(skb)->hop_limit;
		put_cmsg(msg, SOL_IPV6, IPV6_HOPLIMIT, sizeof(hlim), &hlim);
	}

	if (np->rxopt.bits.rxtclass) {
		int tclass = (ntohl(*(__be32 *)ipv6_hdr(skb)) >> 20) & 0xff;
		put_cmsg(msg, SOL_IPV6, IPV6_TCLASS, sizeof(tclass), &tclass);
	}

	if (np->rxopt.bits.rxflow && (*(__be32 *)nh & IPV6_FLOWINFO_MASK)) {
		__be32 flowinfo = *(__be32 *)nh & IPV6_FLOWINFO_MASK;
		put_cmsg(msg, SOL_IPV6, IPV6_FLOWINFO, sizeof(flowinfo), &flowinfo);
	}

	/* HbH is allowed only once */
	if (np->rxopt.bits.hopopts && opt->hop) {
		u8 *ptr = nh + opt->hop;
		put_cmsg(msg, SOL_IPV6, IPV6_HOPOPTS, (ptr[1]+1)<<3, ptr);
	}

	if (opt->lastopt &&
	    (np->rxopt.bits.dstopts || np->rxopt.bits.srcrt)) {
		/*
		 * Silly enough, but we need to reparse in order to
		 * report extension headers (except for HbH)
		 * in order.
		 *
		 * Also note that IPV6_RECVRTHDRDSTOPTS is NOT
		 * (and WILL NOT be) defined because
		 * IPV6_RECVDSTOPTS is more generic. --yoshfuji
		 */
		unsigned int off = sizeof(struct ipv6hdr);
		u8 nexthdr = ipv6_hdr(skb)->nexthdr;

		while (off <= opt->lastopt) {
			unsigned len;
			u8 *ptr = nh + off;

			switch(nexthdr) {
			case IPPROTO_DSTOPTS:
				nexthdr = ptr[0];
				len = (ptr[1] + 1) << 3;
				if (np->rxopt.bits.dstopts)
					put_cmsg(msg, SOL_IPV6, IPV6_DSTOPTS, len, ptr);
				break;
			case IPPROTO_ROUTING:
				nexthdr = ptr[0];
				len = (ptr[1] + 1) << 3;
				if (np->rxopt.bits.srcrt)
					put_cmsg(msg, SOL_IPV6, IPV6_RTHDR, len, ptr);
				break;
			case IPPROTO_AH:
				nexthdr = ptr[0];
				len = (ptr[1] + 2) << 2;
				break;
			default:
				nexthdr = ptr[0];
				len = (ptr[1] + 1) << 3;
				break;
			}

			off += len;
		}
	}

	/* socket options in old style */
	if (np->rxopt.bits.rxoinfo) {
		struct in6_pktinfo src_info;

		src_info.ipi6_ifindex = opt->iif;
		ipv6_addr_copy(&src_info.ipi6_addr, &ipv6_hdr(skb)->daddr);
		put_cmsg(msg, SOL_IPV6, IPV6_2292PKTINFO, sizeof(src_info), &src_info);
	}
	if (np->rxopt.bits.rxohlim) {
		int hlim = ipv6_hdr(skb)->hop_limit;
		put_cmsg(msg, SOL_IPV6, IPV6_2292HOPLIMIT, sizeof(hlim), &hlim);
	}
	if (np->rxopt.bits.ohopopts && opt->hop) {
		u8 *ptr = nh + opt->hop;
		put_cmsg(msg, SOL_IPV6, IPV6_2292HOPOPTS, (ptr[1]+1)<<3, ptr);
	}
	if (np->rxopt.bits.odstopts && opt->dst0) {
		u8 *ptr = nh + opt->dst0;
		put_cmsg(msg, SOL_IPV6, IPV6_2292DSTOPTS, (ptr[1]+1)<<3, ptr);
	}
	if (np->rxopt.bits.osrcrt && opt->srcrt) {
		struct ipv6_rt_hdr *rthdr = (struct ipv6_rt_hdr *)(nh + opt->srcrt);
		put_cmsg(msg, SOL_IPV6, IPV6_2292RTHDR, (rthdr->hdrlen+1) << 3, rthdr);
	}
	if (np->rxopt.bits.odstopts && opt->dst1) {
		u8 *ptr = nh + opt->dst1;
		put_cmsg(msg, SOL_IPV6, IPV6_2292DSTOPTS, (ptr[1]+1)<<3, ptr);
	}
	return 0;
}

int datagram_send_ctl(struct net *net,
		      struct msghdr *msg, struct flowi *fl,
		      struct ipv6_txoptions *opt,
		      int *hlimit, int *tclass)
{
	struct in6_pktinfo *src_info;
	struct cmsghdr *cmsg;
	struct ipv6_rt_hdr *rthdr;
	struct ipv6_opt_hdr *hdr;
	int len;
	int err = 0;

	for (cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg, cmsg)) {
		int addr_type;

		if (!CMSG_OK(msg, cmsg)) {
			err = -EINVAL;
			goto exit_f;
		}

		if (cmsg->cmsg_level != SOL_IPV6)
			continue;

		switch (cmsg->cmsg_type) {
		case IPV6_PKTINFO:
		case IPV6_2292PKTINFO:
		    {
			struct net_device *dev = NULL;

			if (cmsg->cmsg_len < CMSG_LEN(sizeof(struct in6_pktinfo))) {
				err = -EINVAL;
				goto exit_f;
			}

			src_info = (struct in6_pktinfo *)CMSG_DATA(cmsg);

			if (src_info->ipi6_ifindex) {
				if (fl->oif && src_info->ipi6_ifindex != fl->oif)
					return -EINVAL;
				fl->oif = src_info->ipi6_ifindex;
			}

			addr_type = __ipv6_addr_type(&src_info->ipi6_addr);

			if (fl->oif) {
				dev = dev_get_by_index(net, fl->oif);
				if (!dev)
					return -ENODEV;
			} else if (addr_type & IPV6_ADDR_LINKLOCAL)
				return -EINVAL;

			if (addr_type != IPV6_ADDR_ANY) {
				int strict = __ipv6_addr_src_scope(addr_type) <= IPV6_ADDR_SCOPE_LINKLOCAL;
				if (!ipv6_chk_addr(net, &src_info->ipi6_addr,
						   strict ? dev : NULL, 0))
					err = -EINVAL;
				else
					ipv6_addr_copy(&fl->fl6_src, &src_info->ipi6_addr);
			}

			if (dev)
				dev_put(dev);

			if (err)
				goto exit_f;

			break;
		    }

		case IPV6_FLOWINFO:
			if (cmsg->cmsg_len < CMSG_LEN(4)) {
				err = -EINVAL;
				goto exit_f;
			}

			if (fl->fl6_flowlabel&IPV6_FLOWINFO_MASK) {
				if ((fl->fl6_flowlabel^*(__be32 *)CMSG_DATA(cmsg))&~IPV6_FLOWINFO_MASK) {
					err = -EINVAL;
					goto exit_f;
				}
			}
			fl->fl6_flowlabel = IPV6_FLOWINFO_MASK & *(__be32 *)CMSG_DATA(cmsg);
			break;

		case IPV6_2292HOPOPTS:
		case IPV6_HOPOPTS:
			if (opt->hopopt || cmsg->cmsg_len < CMSG_LEN(sizeof(struct ipv6_opt_hdr))) {
				err = -EINVAL;
				goto exit_f;
			}

			hdr = (struct ipv6_opt_hdr *)CMSG_DATA(cmsg);
			len = ((hdr->hdrlen + 1) << 3);
			if (cmsg->cmsg_len < CMSG_LEN(len)) {
				err = -EINVAL;
				goto exit_f;
			}
			if (!capable(CAP_NET_RAW)) {
				err = -EPERM;
				goto exit_f;
			}
			opt->opt_nflen += len;
			opt->hopopt = hdr;
			break;

		case IPV6_2292DSTOPTS:
			if (cmsg->cmsg_len < CMSG_LEN(sizeof(struct ipv6_opt_hdr))) {
				err = -EINVAL;
				goto exit_f;
			}

			hdr = (struct ipv6_opt_hdr *)CMSG_DATA(cmsg);
			len = ((hdr->hdrlen + 1) << 3);
			if (cmsg->cmsg_len < CMSG_LEN(len)) {
				err = -EINVAL;
				goto exit_f;
			}
			if (!capable(CAP_NET_RAW)) {
				err = -EPERM;
				goto exit_f;
			}
			if (opt->dst1opt) {
				err = -EINVAL;
				goto exit_f;
			}
			opt->opt_flen += len;
			opt->dst1opt = hdr;
			break;

		case IPV6_DSTOPTS:
		case IPV6_RTHDRDSTOPTS:
			if (cmsg->cmsg_len < CMSG_LEN(sizeof(struct ipv6_opt_hdr))) {
				err = -EINVAL;
				goto exit_f;
			}

			hdr = (struct ipv6_opt_hdr *)CMSG_DATA(cmsg);
			len = ((hdr->hdrlen + 1) << 3);
			if (cmsg->cmsg_len < CMSG_LEN(len)) {
				err = -EINVAL;
				goto exit_f;
			}
			if (!capable(CAP_NET_RAW)) {
				err = -EPERM;
				goto exit_f;
			}
			if (cmsg->cmsg_type == IPV6_DSTOPTS) {
				opt->opt_flen += len;
				opt->dst1opt = hdr;
			} else {
				opt->opt_nflen += len;
				opt->dst0opt = hdr;
			}
			break;

		case IPV6_2292RTHDR:
		case IPV6_RTHDR:
			if (cmsg->cmsg_len < CMSG_LEN(sizeof(struct ipv6_rt_hdr))) {
				err = -EINVAL;
				goto exit_f;
			}

			rthdr = (struct ipv6_rt_hdr *)CMSG_DATA(cmsg);

			switch (rthdr->type) {
#if defined(CONFIG_IPV6_MIP6) || defined(CONFIG_IPV6_MIP6_MODULE)
			case IPV6_SRCRT_TYPE_2:
				if (rthdr->hdrlen != 2 ||
				    rthdr->segments_left != 1) {
					err = -EINVAL;
					goto exit_f;
				}
				break;
#endif
			default:
				err = -EINVAL;
				goto exit_f;
			}

			len = ((rthdr->hdrlen + 1) << 3);

			if (cmsg->cmsg_len < CMSG_LEN(len)) {
				err = -EINVAL;
				goto exit_f;
			}

			/* segments left must also match */
			if ((rthdr->hdrlen >> 1) != rthdr->segments_left) {
				err = -EINVAL;
				goto exit_f;
			}

			opt->opt_nflen += len;
			opt->srcrt = rthdr;

			if (cmsg->cmsg_type == IPV6_2292RTHDR && opt->dst1opt) {
				int dsthdrlen = ((opt->dst1opt->hdrlen+1)<<3);

				opt->opt_nflen += dsthdrlen;
				opt->dst0opt = opt->dst1opt;
				opt->dst1opt = NULL;
				opt->opt_flen -= dsthdrlen;
			}

			break;

		case IPV6_2292HOPLIMIT:
		case IPV6_HOPLIMIT:
			if (cmsg->cmsg_len != CMSG_LEN(sizeof(int))) {
				err = -EINVAL;
				goto exit_f;
			}

			*hlimit = *(int *)CMSG_DATA(cmsg);
			if (*hlimit < -1 || *hlimit > 0xff) {
				err = -EINVAL;
				goto exit_f;
			}

			break;

		case IPV6_TCLASS:
		    {
			int tc;

			err = -EINVAL;
			if (cmsg->cmsg_len != CMSG_LEN(sizeof(int))) {
				goto exit_f;
			}

			tc = *(int *)CMSG_DATA(cmsg);
			if (tc < -1 || tc > 0xff)
				goto exit_f;

			err = 0;
			*tclass = tc;

			break;
		    }
		default:
			LIMIT_NETDEBUG(KERN_DEBUG "invalid cmsg type: %d\n",
				       cmsg->cmsg_type);
			err = -EINVAL;
			goto exit_f;
		}
	}

exit_f:
	return err;
}
