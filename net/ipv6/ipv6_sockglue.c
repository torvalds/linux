/*
 *	IPv6 BSD socket options interface
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	Based on linux/net/ipv4/ip_sockglue.c
 *
 *	$Id: ipv6_sockglue.c,v 1.41 2002/02/01 22:01:04 davem Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 *	FIXME: Make the setsockopt code POSIX compliant: That is
 *
 *	o	Return -EINVAL for setsockopt of short lengths
 *	o	Truncate getsockopt returns
 *	o	Return an optlen of the truncated length if need be
 *
 *	Changes:
 *	David L Stevens <dlstevens@us.ibm.com>:
 *		- added multicast source filtering API for MLDv2
 */

#include <linux/module.h>
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/sched.h>
#include <linux/net.h>
#include <linux/in6.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/init.h>
#include <linux/sysctl.h>
#include <linux/netfilter.h>

#include <net/sock.h>
#include <net/snmp.h>
#include <net/ipv6.h>
#include <net/ndisc.h>
#include <net/protocol.h>
#include <net/transp_v6.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>
#include <net/inet_common.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/xfrm.h>

#include <asm/uaccess.h>

DEFINE_SNMP_STAT(struct ipstats_mib, ipv6_statistics) __read_mostly;

static struct packet_type ipv6_packet_type = {
	.type = __constant_htons(ETH_P_IPV6), 
	.func = ipv6_rcv,
};

struct ip6_ra_chain *ip6_ra_chain;
DEFINE_RWLOCK(ip6_ra_lock);

int ip6_ra_control(struct sock *sk, int sel, void (*destructor)(struct sock *))
{
	struct ip6_ra_chain *ra, *new_ra, **rap;

	/* RA packet may be delivered ONLY to IPPROTO_RAW socket */
	if (sk->sk_type != SOCK_RAW || inet_sk(sk)->num != IPPROTO_RAW)
		return -EINVAL;

	new_ra = (sel>=0) ? kmalloc(sizeof(*new_ra), GFP_KERNEL) : NULL;

	write_lock_bh(&ip6_ra_lock);
	for (rap = &ip6_ra_chain; (ra=*rap) != NULL; rap = &ra->next) {
		if (ra->sk == sk) {
			if (sel>=0) {
				write_unlock_bh(&ip6_ra_lock);
				if (new_ra)
					kfree(new_ra);
				return -EADDRINUSE;
			}

			*rap = ra->next;
			write_unlock_bh(&ip6_ra_lock);

			if (ra->destructor)
				ra->destructor(sk);
			sock_put(sk);
			kfree(ra);
			return 0;
		}
	}
	if (new_ra == NULL) {
		write_unlock_bh(&ip6_ra_lock);
		return -ENOBUFS;
	}
	new_ra->sk = sk;
	new_ra->sel = sel;
	new_ra->destructor = destructor;
	new_ra->next = ra;
	*rap = new_ra;
	sock_hold(sk);
	write_unlock_bh(&ip6_ra_lock);
	return 0;
}

int ipv6_setsockopt(struct sock *sk, int level, int optname,
		    char __user *optval, int optlen)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	int val, valbool;
	int retv = -ENOPROTOOPT;

	if (level == SOL_IP && sk->sk_type != SOCK_RAW)
		return udp_prot.setsockopt(sk, level, optname, optval, optlen);

	if(level!=SOL_IPV6)
		goto out;

	if (optval == NULL)
		val=0;
	else if (get_user(val, (int __user *) optval))
		return -EFAULT;

	valbool = (val!=0);

	lock_sock(sk);

	switch (optname) {

	case IPV6_ADDRFORM:
		if (val == PF_INET) {
			struct ipv6_txoptions *opt;
			struct sk_buff *pktopt;

			if (sk->sk_protocol != IPPROTO_UDP &&
			    sk->sk_protocol != IPPROTO_TCP)
				break;

			if (sk->sk_state != TCP_ESTABLISHED) {
				retv = -ENOTCONN;
				break;
			}

			if (ipv6_only_sock(sk) ||
			    !(ipv6_addr_type(&np->daddr) & IPV6_ADDR_MAPPED)) {
				retv = -EADDRNOTAVAIL;
				break;
			}

			fl6_free_socklist(sk);
			ipv6_sock_mc_close(sk);

			/*
			 * Sock is moving from IPv6 to IPv4 (sk_prot), so
			 * remove it from the refcnt debug socks count in the
			 * original family...
			 */
			sk_refcnt_debug_dec(sk);

			if (sk->sk_protocol == IPPROTO_TCP) {
				struct tcp_sock *tp = tcp_sk(sk);

				local_bh_disable();
				sock_prot_dec_use(sk->sk_prot);
				sock_prot_inc_use(&tcp_prot);
				local_bh_enable();
				sk->sk_prot = &tcp_prot;
				tp->af_specific = &ipv4_specific;
				sk->sk_socket->ops = &inet_stream_ops;
				sk->sk_family = PF_INET;
				tcp_sync_mss(sk, tp->pmtu_cookie);
			} else {
				local_bh_disable();
				sock_prot_dec_use(sk->sk_prot);
				sock_prot_inc_use(&udp_prot);
				local_bh_enable();
				sk->sk_prot = &udp_prot;
				sk->sk_socket->ops = &inet_dgram_ops;
				sk->sk_family = PF_INET;
			}
			opt = xchg(&np->opt, NULL);
			if (opt)
				sock_kfree_s(sk, opt, opt->tot_len);
			pktopt = xchg(&np->pktoptions, NULL);
			if (pktopt)
				kfree_skb(pktopt);

			sk->sk_destruct = inet_sock_destruct;
			/*
			 * ... and add it to the refcnt debug socks count
			 * in the new family. -acme
			 */
			sk_refcnt_debug_inc(sk);
			module_put(THIS_MODULE);
			retv = 0;
			break;
		}
		goto e_inval;

	case IPV6_V6ONLY:
		if (inet_sk(sk)->num)
			goto e_inval;
		np->ipv6only = valbool;
		retv = 0;
		break;

	case IPV6_RECVPKTINFO:
		np->rxopt.bits.rxinfo = valbool;
		retv = 0;
		break;
		
	case IPV6_2292PKTINFO:
		np->rxopt.bits.rxoinfo = valbool;
		retv = 0;
		break;

	case IPV6_RECVHOPLIMIT:
		np->rxopt.bits.rxhlim = valbool;
		retv = 0;
		break;

	case IPV6_2292HOPLIMIT:
		np->rxopt.bits.rxohlim = valbool;
		retv = 0;
		break;

	case IPV6_RECVRTHDR:
		if (val < 0 || val > 2)
			goto e_inval;
		np->rxopt.bits.srcrt = val;
		retv = 0;
		break;

	case IPV6_2292RTHDR:
		if (val < 0 || val > 2)
			goto e_inval;
		np->rxopt.bits.osrcrt = val;
		retv = 0;
		break;

	case IPV6_RECVHOPOPTS:
		np->rxopt.bits.hopopts = valbool;
		retv = 0;
		break;

	case IPV6_2292HOPOPTS:
		np->rxopt.bits.ohopopts = valbool;
		retv = 0;
		break;

	case IPV6_RECVDSTOPTS:
		np->rxopt.bits.dstopts = valbool;
		retv = 0;
		break;

	case IPV6_2292DSTOPTS:
		np->rxopt.bits.odstopts = valbool;
		retv = 0;
		break;

	case IPV6_TCLASS:
		if (val < 0 || val > 0xff)
			goto e_inval;
		np->tclass = val;
		retv = 0;
		break;
		
	case IPV6_RECVTCLASS:
		np->rxopt.bits.rxtclass = valbool;
		retv = 0;
		break;

	case IPV6_FLOWINFO:
		np->rxopt.bits.rxflow = valbool;
		retv = 0;
		break;

	case IPV6_HOPOPTS:
	case IPV6_RTHDRDSTOPTS:
	case IPV6_RTHDR:
	case IPV6_DSTOPTS:
	{
		struct ipv6_txoptions *opt;
		if (optlen == 0)
			optval = 0;

		/* hop-by-hop / destination options are privileged option */
		retv = -EPERM;
		if (optname != IPV6_RTHDR && !capable(CAP_NET_RAW))
			break;

		retv = -EINVAL;
		if (optlen & 0x7 || optlen > 8 * 255)
			break;

		opt = ipv6_renew_options(sk, np->opt, optname,
					 (struct ipv6_opt_hdr __user *)optval,
					 optlen);
		if (IS_ERR(opt)) {
			retv = PTR_ERR(opt);
			break;
		}

		/* routing header option needs extra check */
		if (optname == IPV6_RTHDR && opt->srcrt) {
			struct ipv6_rt_hdr *rthdr = opt->srcrt;
			if (rthdr->type)
				goto sticky_done;
			if ((rthdr->hdrlen & 1) ||
			    (rthdr->hdrlen >> 1) != rthdr->segments_left)
				goto sticky_done;
		}

		retv = 0;
		if (sk->sk_type == SOCK_STREAM) {
			if (opt) {
				struct tcp_sock *tp = tcp_sk(sk);
				if (!((1 << sk->sk_state) &
				      (TCPF_LISTEN | TCPF_CLOSE))
				    && inet_sk(sk)->daddr != LOOPBACK4_IPV6) {
					tp->ext_header_len = opt->opt_flen + opt->opt_nflen;
					tcp_sync_mss(sk, tp->pmtu_cookie);
				}
			}
			opt = xchg(&np->opt, opt);
			sk_dst_reset(sk);
		} else {
			write_lock(&sk->sk_dst_lock);
			opt = xchg(&np->opt, opt);
			write_unlock(&sk->sk_dst_lock);
			sk_dst_reset(sk);
		}
sticky_done:
		if (opt)
			sock_kfree_s(sk, opt, opt->tot_len);
		break;
	}

	case IPV6_2292PKTOPTIONS:
	{
		struct ipv6_txoptions *opt = NULL;
		struct msghdr msg;
		struct flowi fl;
		int junk;

		fl.fl6_flowlabel = 0;
		fl.oif = sk->sk_bound_dev_if;

		if (optlen == 0)
			goto update;

		/* 1K is probably excessive
		 * 1K is surely not enough, 2K per standard header is 16K.
		 */
		retv = -EINVAL;
		if (optlen > 64*1024)
			break;

		opt = sock_kmalloc(sk, sizeof(*opt) + optlen, GFP_KERNEL);
		retv = -ENOBUFS;
		if (opt == NULL)
			break;

		memset(opt, 0, sizeof(*opt));
		opt->tot_len = sizeof(*opt) + optlen;
		retv = -EFAULT;
		if (copy_from_user(opt+1, optval, optlen))
			goto done;

		msg.msg_controllen = optlen;
		msg.msg_control = (void*)(opt+1);

		retv = datagram_send_ctl(&msg, &fl, opt, &junk, &junk);
		if (retv)
			goto done;
update:
		retv = 0;
		if (sk->sk_type == SOCK_STREAM) {
			if (opt) {
				struct tcp_sock *tp = tcp_sk(sk);
				if (!((1 << sk->sk_state) &
				      (TCPF_LISTEN | TCPF_CLOSE))
				    && inet_sk(sk)->daddr != LOOPBACK4_IPV6) {
					tp->ext_header_len = opt->opt_flen + opt->opt_nflen;
					tcp_sync_mss(sk, tp->pmtu_cookie);
				}
			}
			opt = xchg(&np->opt, opt);
			sk_dst_reset(sk);
		} else {
			write_lock(&sk->sk_dst_lock);
			opt = xchg(&np->opt, opt);
			write_unlock(&sk->sk_dst_lock);
			sk_dst_reset(sk);
		}

done:
		if (opt)
			sock_kfree_s(sk, opt, opt->tot_len);
		break;
	}
	case IPV6_UNICAST_HOPS:
		if (val > 255 || val < -1)
			goto e_inval;
		np->hop_limit = val;
		retv = 0;
		break;

	case IPV6_MULTICAST_HOPS:
		if (sk->sk_type == SOCK_STREAM)
			goto e_inval;
		if (val > 255 || val < -1)
			goto e_inval;
		np->mcast_hops = val;
		retv = 0;
		break;

	case IPV6_MULTICAST_LOOP:
		np->mc_loop = valbool;
		retv = 0;
		break;

	case IPV6_MULTICAST_IF:
		if (sk->sk_type == SOCK_STREAM)
			goto e_inval;
		if (sk->sk_bound_dev_if && sk->sk_bound_dev_if != val)
			goto e_inval;

		if (__dev_get_by_index(val) == NULL) {
			retv = -ENODEV;
			break;
		}
		np->mcast_oif = val;
		retv = 0;
		break;
	case IPV6_ADD_MEMBERSHIP:
	case IPV6_DROP_MEMBERSHIP:
	{
		struct ipv6_mreq mreq;

		retv = -EFAULT;
		if (copy_from_user(&mreq, optval, sizeof(struct ipv6_mreq)))
			break;

		if (optname == IPV6_ADD_MEMBERSHIP)
			retv = ipv6_sock_mc_join(sk, mreq.ipv6mr_ifindex, &mreq.ipv6mr_multiaddr);
		else
			retv = ipv6_sock_mc_drop(sk, mreq.ipv6mr_ifindex, &mreq.ipv6mr_multiaddr);
		break;
	}
	case IPV6_JOIN_ANYCAST:
	case IPV6_LEAVE_ANYCAST:
	{
		struct ipv6_mreq mreq;

		if (optlen != sizeof(struct ipv6_mreq))
			goto e_inval;

		retv = -EFAULT;
		if (copy_from_user(&mreq, optval, sizeof(struct ipv6_mreq)))
			break;

		if (optname == IPV6_JOIN_ANYCAST)
			retv = ipv6_sock_ac_join(sk, mreq.ipv6mr_ifindex, &mreq.ipv6mr_acaddr);
		else
			retv = ipv6_sock_ac_drop(sk, mreq.ipv6mr_ifindex, &mreq.ipv6mr_acaddr);
		break;
	}
	case MCAST_JOIN_GROUP:
	case MCAST_LEAVE_GROUP:
	{
		struct group_req greq;
		struct sockaddr_in6 *psin6;

		retv = -EFAULT;
		if (copy_from_user(&greq, optval, sizeof(struct group_req)))
			break;
		if (greq.gr_group.ss_family != AF_INET6) {
			retv = -EADDRNOTAVAIL;
			break;
		}
		psin6 = (struct sockaddr_in6 *)&greq.gr_group;
		if (optname == MCAST_JOIN_GROUP)
			retv = ipv6_sock_mc_join(sk, greq.gr_interface,
				&psin6->sin6_addr);
		else
			retv = ipv6_sock_mc_drop(sk, greq.gr_interface,
				&psin6->sin6_addr);
		break;
	}
	case MCAST_JOIN_SOURCE_GROUP:
	case MCAST_LEAVE_SOURCE_GROUP:
	case MCAST_BLOCK_SOURCE:
	case MCAST_UNBLOCK_SOURCE:
	{
		struct group_source_req greqs;
		int omode, add;

		if (optlen != sizeof(struct group_source_req))
			goto e_inval;
		if (copy_from_user(&greqs, optval, sizeof(greqs))) {
			retv = -EFAULT;
			break;
		}
		if (greqs.gsr_group.ss_family != AF_INET6 ||
		    greqs.gsr_source.ss_family != AF_INET6) {
			retv = -EADDRNOTAVAIL;
			break;
		}
		if (optname == MCAST_BLOCK_SOURCE) {
			omode = MCAST_EXCLUDE;
			add = 1;
		} else if (optname == MCAST_UNBLOCK_SOURCE) {
			omode = MCAST_EXCLUDE;
			add = 0;
		} else if (optname == MCAST_JOIN_SOURCE_GROUP) {
			struct sockaddr_in6 *psin6;

			psin6 = (struct sockaddr_in6 *)&greqs.gsr_group;
			retv = ipv6_sock_mc_join(sk, greqs.gsr_interface,
				&psin6->sin6_addr);
			/* prior join w/ different source is ok */
			if (retv && retv != -EADDRINUSE)
				break;
			omode = MCAST_INCLUDE;
			add = 1;
		} else /* MCAST_LEAVE_SOURCE_GROUP */ {
			omode = MCAST_INCLUDE;
			add = 0;
		}
		retv = ip6_mc_source(add, omode, sk, &greqs);
		break;
	}
	case MCAST_MSFILTER:
	{
		extern int sysctl_mld_max_msf;
		struct group_filter *gsf;

		if (optlen < GROUP_FILTER_SIZE(0))
			goto e_inval;
		if (optlen > sysctl_optmem_max) {
			retv = -ENOBUFS;
			break;
		}
		gsf = (struct group_filter *)kmalloc(optlen,GFP_KERNEL);
		if (gsf == 0) {
			retv = -ENOBUFS;
			break;
		}
		retv = -EFAULT;
		if (copy_from_user(gsf, optval, optlen)) {
			kfree(gsf);
			break;
		}
		/* numsrc >= (4G-140)/128 overflow in 32 bits */
		if (gsf->gf_numsrc >= 0x1ffffffU ||
		    gsf->gf_numsrc > sysctl_mld_max_msf) {
			kfree(gsf);
			retv = -ENOBUFS;
			break;
		}
		if (GROUP_FILTER_SIZE(gsf->gf_numsrc) > optlen) {
			kfree(gsf);
			retv = -EINVAL;
			break;
		}
		retv = ip6_mc_msfilter(sk, gsf);
		kfree(gsf);

		break;
	}
	case IPV6_ROUTER_ALERT:
		retv = ip6_ra_control(sk, val, NULL);
		break;
	case IPV6_MTU_DISCOVER:
		if (val<0 || val>2)
			goto e_inval;
		np->pmtudisc = val;
		retv = 0;
		break;
	case IPV6_MTU:
		if (val && val < IPV6_MIN_MTU)
			goto e_inval;
		np->frag_size = val;
		retv = 0;
		break;
	case IPV6_RECVERR:
		np->recverr = valbool;
		if (!val)
			skb_queue_purge(&sk->sk_error_queue);
		retv = 0;
		break;
	case IPV6_FLOWINFO_SEND:
		np->sndflow = valbool;
		retv = 0;
		break;
	case IPV6_FLOWLABEL_MGR:
		retv = ipv6_flowlabel_opt(sk, optval, optlen);
		break;
	case IPV6_IPSEC_POLICY:
	case IPV6_XFRM_POLICY:
		retv = -EPERM;
		if (!capable(CAP_NET_ADMIN))
			break;
		retv = xfrm_user_policy(sk, optname, optval, optlen);
		break;

#ifdef CONFIG_NETFILTER
	default:
		retv = nf_setsockopt(sk, PF_INET6, optname, optval, 
					    optlen);
		break;
#endif

	}
	release_sock(sk);

out:
	return retv;

e_inval:
	release_sock(sk);
	return -EINVAL;
}

int ipv6_getsockopt_sticky(struct sock *sk, struct ipv6_opt_hdr *hdr,
			   char __user *optval, int len)
{
	if (!hdr)
		return 0;
	len = min_t(int, len, ipv6_optlen(hdr));
	if (copy_to_user(optval, hdr, ipv6_optlen(hdr)))
		return -EFAULT;
	return len;
}

int ipv6_getsockopt(struct sock *sk, int level, int optname,
		    char __user *optval, int __user *optlen)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	int len;
	int val;

	if (level == SOL_IP && sk->sk_type != SOCK_RAW)
		return udp_prot.getsockopt(sk, level, optname, optval, optlen);
	if(level!=SOL_IPV6)
		return -ENOPROTOOPT;
	if (get_user(len, optlen))
		return -EFAULT;
	switch (optname) {
	case IPV6_ADDRFORM:
		if (sk->sk_protocol != IPPROTO_UDP &&
		    sk->sk_protocol != IPPROTO_TCP)
			return -EINVAL;
		if (sk->sk_state != TCP_ESTABLISHED)
			return -ENOTCONN;
		val = sk->sk_family;
		break;
	case MCAST_MSFILTER:
	{
		struct group_filter gsf;
		int err;

		if (len < GROUP_FILTER_SIZE(0))
			return -EINVAL;
		if (copy_from_user(&gsf, optval, GROUP_FILTER_SIZE(0)))
			return -EFAULT;
		lock_sock(sk);
		err = ip6_mc_msfget(sk, &gsf,
			(struct group_filter __user *)optval, optlen);
		release_sock(sk);
		return err;
	}

	case IPV6_2292PKTOPTIONS:
	{
		struct msghdr msg;
		struct sk_buff *skb;

		if (sk->sk_type != SOCK_STREAM)
			return -ENOPROTOOPT;

		msg.msg_control = optval;
		msg.msg_controllen = len;
		msg.msg_flags = 0;

		lock_sock(sk);
		skb = np->pktoptions;
		if (skb)
			atomic_inc(&skb->users);
		release_sock(sk);

		if (skb) {
			int err = datagram_recv_ctl(sk, &msg, skb);
			kfree_skb(skb);
			if (err)
				return err;
		} else {
			if (np->rxopt.bits.rxinfo) {
				struct in6_pktinfo src_info;
				src_info.ipi6_ifindex = np->mcast_oif;
				ipv6_addr_copy(&src_info.ipi6_addr, &np->daddr);
				put_cmsg(&msg, SOL_IPV6, IPV6_PKTINFO, sizeof(src_info), &src_info);
			}
			if (np->rxopt.bits.rxhlim) {
				int hlim = np->mcast_hops;
				put_cmsg(&msg, SOL_IPV6, IPV6_HOPLIMIT, sizeof(hlim), &hlim);
			}
			if (np->rxopt.bits.rxoinfo) {
				struct in6_pktinfo src_info;
				src_info.ipi6_ifindex = np->mcast_oif;
				ipv6_addr_copy(&src_info.ipi6_addr, &np->daddr);
				put_cmsg(&msg, SOL_IPV6, IPV6_2292PKTINFO, sizeof(src_info), &src_info);
			}
			if (np->rxopt.bits.rxohlim) {
				int hlim = np->mcast_hops;
				put_cmsg(&msg, SOL_IPV6, IPV6_2292HOPLIMIT, sizeof(hlim), &hlim);
			}
		}
		len -= msg.msg_controllen;
		return put_user(len, optlen);
	}
	case IPV6_MTU:
	{
		struct dst_entry *dst;
		val = 0;	
		lock_sock(sk);
		dst = sk_dst_get(sk);
		if (dst) {
			val = dst_mtu(dst);
			dst_release(dst);
		}
		release_sock(sk);
		if (!val)
			return -ENOTCONN;
		break;
	}

	case IPV6_V6ONLY:
		val = np->ipv6only;
		break;

	case IPV6_RECVPKTINFO:
		val = np->rxopt.bits.rxinfo;
		break;

	case IPV6_2292PKTINFO:
		val = np->rxopt.bits.rxoinfo;
		break;

	case IPV6_RECVHOPLIMIT:
		val = np->rxopt.bits.rxhlim;
		break;

	case IPV6_2292HOPLIMIT:
		val = np->rxopt.bits.rxohlim;
		break;

	case IPV6_RECVRTHDR:
		val = np->rxopt.bits.srcrt;
		break;

	case IPV6_2292RTHDR:
		val = np->rxopt.bits.osrcrt;
		break;

	case IPV6_HOPOPTS:
	case IPV6_RTHDRDSTOPTS:
	case IPV6_RTHDR:
	case IPV6_DSTOPTS:
	{

		lock_sock(sk);
		len = ipv6_getsockopt_sticky(sk, np->opt->hopopt,
					     optval, len);
		release_sock(sk);
		return put_user(len, optlen);
	}

	case IPV6_RECVHOPOPTS:
		val = np->rxopt.bits.hopopts;
		break;

	case IPV6_2292HOPOPTS:
		val = np->rxopt.bits.ohopopts;
		break;

	case IPV6_RECVDSTOPTS:
		val = np->rxopt.bits.dstopts;
		break;

	case IPV6_2292DSTOPTS:
		val = np->rxopt.bits.odstopts;
		break;

	case IPV6_TCLASS:
		val = np->tclass;
		break;

	case IPV6_RECVTCLASS:
		val = np->rxopt.bits.rxtclass;
		break;

	case IPV6_FLOWINFO:
		val = np->rxopt.bits.rxflow;
		break;

	case IPV6_UNICAST_HOPS:
		val = np->hop_limit;
		break;

	case IPV6_MULTICAST_HOPS:
		val = np->mcast_hops;
		break;

	case IPV6_MULTICAST_LOOP:
		val = np->mc_loop;
		break;

	case IPV6_MULTICAST_IF:
		val = np->mcast_oif;
		break;

	case IPV6_MTU_DISCOVER:
		val = np->pmtudisc;
		break;

	case IPV6_RECVERR:
		val = np->recverr;
		break;

	case IPV6_FLOWINFO_SEND:
		val = np->sndflow;
		break;

	default:
#ifdef CONFIG_NETFILTER
		lock_sock(sk);
		val = nf_getsockopt(sk, PF_INET6, optname, optval, 
				    &len);
		release_sock(sk);
		if (val >= 0)
			val = put_user(len, optlen);
		return val;
#else
		return -EINVAL;
#endif
	}
	len = min_t(unsigned int, sizeof(int), len);
	if(put_user(len, optlen))
		return -EFAULT;
	if(copy_to_user(optval,&val,len))
		return -EFAULT;
	return 0;
}

void __init ipv6_packet_init(void)
{
	dev_add_pack(&ipv6_packet_type);
}

void ipv6_packet_cleanup(void)
{
	dev_remove_pack(&ipv6_packet_type);
}
