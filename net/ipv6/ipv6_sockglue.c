// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	IPv6 BSD socket options interface
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *
 *	Based on linux/net/ipv4/ip_sockglue.c
 *
 *	FIXME: Make the setsockopt code POSIX compliant: That is
 *
 *	o	Truncate getsockopt returns
 *	o	Return an optlen of the truncated length if need be
 *
 *	Changes:
 *	David L Stevens <dlstevens@us.ibm.com>:
 *		- added multicast source filtering API for MLDv2
 */

#include <linux/module.h>
#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/in6.h>
#include <linux/mroute6.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/init.h>
#include <linux/sysctl.h>
#include <linux/netfilter.h>
#include <linux/slab.h>

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
#include <net/udplite.h>
#include <net/xfrm.h>
#include <net/compat.h>
#include <net/seg6.h>

#include <linux/uaccess.h>

struct ip6_ra_chain *ip6_ra_chain;
DEFINE_RWLOCK(ip6_ra_lock);

int ip6_ra_control(struct sock *sk, int sel)
{
	struct ip6_ra_chain *ra, *new_ra, **rap;

	/* RA packet may be delivered ONLY to IPPROTO_RAW socket */
	if (sk->sk_type != SOCK_RAW || inet_sk(sk)->inet_num != IPPROTO_RAW)
		return -ENOPROTOOPT;

	new_ra = (sel >= 0) ? kmalloc(sizeof(*new_ra), GFP_KERNEL) : NULL;
	if (sel >= 0 && !new_ra)
		return -ENOMEM;

	write_lock_bh(&ip6_ra_lock);
	for (rap = &ip6_ra_chain; (ra = *rap) != NULL; rap = &ra->next) {
		if (ra->sk == sk) {
			if (sel >= 0) {
				write_unlock_bh(&ip6_ra_lock);
				kfree(new_ra);
				return -EADDRINUSE;
			}

			*rap = ra->next;
			write_unlock_bh(&ip6_ra_lock);

			sock_put(sk);
			kfree(ra);
			return 0;
		}
	}
	if (!new_ra) {
		write_unlock_bh(&ip6_ra_lock);
		return -ENOBUFS;
	}
	new_ra->sk = sk;
	new_ra->sel = sel;
	new_ra->next = ra;
	*rap = new_ra;
	sock_hold(sk);
	write_unlock_bh(&ip6_ra_lock);
	return 0;
}

struct ipv6_txoptions *ipv6_update_options(struct sock *sk,
					   struct ipv6_txoptions *opt)
{
	if (inet_sk(sk)->is_icsk) {
		if (opt &&
		    !((1 << sk->sk_state) & (TCPF_LISTEN | TCPF_CLOSE)) &&
		    inet_sk(sk)->inet_daddr != LOOPBACK4_IPV6) {
			struct inet_connection_sock *icsk = inet_csk(sk);
			icsk->icsk_ext_hdr_len = opt->opt_flen + opt->opt_nflen;
			icsk->icsk_sync_mss(sk, icsk->icsk_pmtu_cookie);
		}
	}
	opt = xchg((__force struct ipv6_txoptions **)&inet6_sk(sk)->opt,
		   opt);
	sk_dst_reset(sk);

	return opt;
}

static bool setsockopt_needs_rtnl(int optname)
{
	switch (optname) {
	case IPV6_ADDRFORM:
	case IPV6_ADD_MEMBERSHIP:
	case IPV6_DROP_MEMBERSHIP:
	case IPV6_JOIN_ANYCAST:
	case IPV6_LEAVE_ANYCAST:
	case MCAST_JOIN_GROUP:
	case MCAST_LEAVE_GROUP:
	case MCAST_JOIN_SOURCE_GROUP:
	case MCAST_LEAVE_SOURCE_GROUP:
	case MCAST_BLOCK_SOURCE:
	case MCAST_UNBLOCK_SOURCE:
	case MCAST_MSFILTER:
		return true;
	}
	return false;
}

static int copy_group_source_from_sockptr(struct group_source_req *greqs,
		sockptr_t optval, int optlen)
{
	if (in_compat_syscall()) {
		struct compat_group_source_req gr32;

		if (optlen < sizeof(gr32))
			return -EINVAL;
		if (copy_from_sockptr(&gr32, optval, sizeof(gr32)))
			return -EFAULT;
		greqs->gsr_interface = gr32.gsr_interface;
		greqs->gsr_group = gr32.gsr_group;
		greqs->gsr_source = gr32.gsr_source;
	} else {
		if (optlen < sizeof(*greqs))
			return -EINVAL;
		if (copy_from_sockptr(greqs, optval, sizeof(*greqs)))
			return -EFAULT;
	}

	return 0;
}

static int do_ipv6_mcast_group_source(struct sock *sk, int optname,
		sockptr_t optval, int optlen)
{
	struct group_source_req greqs;
	int omode, add;
	int ret;

	ret = copy_group_source_from_sockptr(&greqs, optval, optlen);
	if (ret)
		return ret;

	if (greqs.gsr_group.ss_family != AF_INET6 ||
	    greqs.gsr_source.ss_family != AF_INET6)
		return -EADDRNOTAVAIL;

	if (optname == MCAST_BLOCK_SOURCE) {
		omode = MCAST_EXCLUDE;
		add = 1;
	} else if (optname == MCAST_UNBLOCK_SOURCE) {
		omode = MCAST_EXCLUDE;
		add = 0;
	} else if (optname == MCAST_JOIN_SOURCE_GROUP) {
		struct sockaddr_in6 *psin6;
		int retv;

		psin6 = (struct sockaddr_in6 *)&greqs.gsr_group;
		retv = ipv6_sock_mc_join_ssm(sk, greqs.gsr_interface,
					     &psin6->sin6_addr,
					     MCAST_INCLUDE);
		/* prior join w/ different source is ok */
		if (retv && retv != -EADDRINUSE)
			return retv;
		omode = MCAST_INCLUDE;
		add = 1;
	} else /* MCAST_LEAVE_SOURCE_GROUP */ {
		omode = MCAST_INCLUDE;
		add = 0;
	}
	return ip6_mc_source(add, omode, sk, &greqs);
}

static int ipv6_set_mcast_msfilter(struct sock *sk, sockptr_t optval,
		int optlen)
{
	struct group_filter *gsf;
	int ret;

	if (optlen < GROUP_FILTER_SIZE(0))
		return -EINVAL;
	if (optlen > READ_ONCE(sysctl_optmem_max))
		return -ENOBUFS;

	gsf = memdup_sockptr(optval, optlen);
	if (IS_ERR(gsf))
		return PTR_ERR(gsf);

	/* numsrc >= (4G-140)/128 overflow in 32 bits */
	ret = -ENOBUFS;
	if (gsf->gf_numsrc >= 0x1ffffffU ||
	    gsf->gf_numsrc > sysctl_mld_max_msf)
		goto out_free_gsf;

	ret = -EINVAL;
	if (GROUP_FILTER_SIZE(gsf->gf_numsrc) > optlen)
		goto out_free_gsf;

	ret = ip6_mc_msfilter(sk, gsf, gsf->gf_slist_flex);
out_free_gsf:
	kfree(gsf);
	return ret;
}

static int compat_ipv6_set_mcast_msfilter(struct sock *sk, sockptr_t optval,
		int optlen)
{
	const int size0 = offsetof(struct compat_group_filter, gf_slist_flex);
	struct compat_group_filter *gf32;
	void *p;
	int ret;
	int n;

	if (optlen < size0)
		return -EINVAL;
	if (optlen > READ_ONCE(sysctl_optmem_max) - 4)
		return -ENOBUFS;

	p = kmalloc(optlen + 4, GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	gf32 = p + 4; /* we want ->gf_group and ->gf_slist_flex aligned */
	ret = -EFAULT;
	if (copy_from_sockptr(gf32, optval, optlen))
		goto out_free_p;

	/* numsrc >= (4G-140)/128 overflow in 32 bits */
	ret = -ENOBUFS;
	n = gf32->gf_numsrc;
	if (n >= 0x1ffffffU || n > sysctl_mld_max_msf)
		goto out_free_p;

	ret = -EINVAL;
	if (offsetof(struct compat_group_filter, gf_slist_flex[n]) > optlen)
		goto out_free_p;

	ret = ip6_mc_msfilter(sk, &(struct group_filter){
			.gf_interface = gf32->gf_interface,
			.gf_group = gf32->gf_group,
			.gf_fmode = gf32->gf_fmode,
			.gf_numsrc = gf32->gf_numsrc}, gf32->gf_slist_flex);

out_free_p:
	kfree(p);
	return ret;
}

static int ipv6_mcast_join_leave(struct sock *sk, int optname,
		sockptr_t optval, int optlen)
{
	struct sockaddr_in6 *psin6;
	struct group_req greq;

	if (optlen < sizeof(greq))
		return -EINVAL;
	if (copy_from_sockptr(&greq, optval, sizeof(greq)))
		return -EFAULT;

	if (greq.gr_group.ss_family != AF_INET6)
		return -EADDRNOTAVAIL;
	psin6 = (struct sockaddr_in6 *)&greq.gr_group;
	if (optname == MCAST_JOIN_GROUP)
		return ipv6_sock_mc_join(sk, greq.gr_interface,
					 &psin6->sin6_addr);
	return ipv6_sock_mc_drop(sk, greq.gr_interface, &psin6->sin6_addr);
}

static int compat_ipv6_mcast_join_leave(struct sock *sk, int optname,
		sockptr_t optval, int optlen)
{
	struct compat_group_req gr32;
	struct sockaddr_in6 *psin6;

	if (optlen < sizeof(gr32))
		return -EINVAL;
	if (copy_from_sockptr(&gr32, optval, sizeof(gr32)))
		return -EFAULT;

	if (gr32.gr_group.ss_family != AF_INET6)
		return -EADDRNOTAVAIL;
	psin6 = (struct sockaddr_in6 *)&gr32.gr_group;
	if (optname == MCAST_JOIN_GROUP)
		return ipv6_sock_mc_join(sk, gr32.gr_interface,
					&psin6->sin6_addr);
	return ipv6_sock_mc_drop(sk, gr32.gr_interface, &psin6->sin6_addr);
}

static int ipv6_set_opt_hdr(struct sock *sk, int optname, sockptr_t optval,
		int optlen)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct ipv6_opt_hdr *new = NULL;
	struct net *net = sock_net(sk);
	struct ipv6_txoptions *opt;
	int err;

	/* hop-by-hop / destination options are privileged option */
	if (optname != IPV6_RTHDR && !ns_capable(net->user_ns, CAP_NET_RAW))
		return -EPERM;

	/* remove any sticky options header with a zero option
	 * length, per RFC3542.
	 */
	if (optlen > 0) {
		if (sockptr_is_null(optval))
			return -EINVAL;
		if (optlen < sizeof(struct ipv6_opt_hdr) ||
		    optlen & 0x7 ||
		    optlen > 8 * 255)
			return -EINVAL;

		new = memdup_sockptr(optval, optlen);
		if (IS_ERR(new))
			return PTR_ERR(new);
		if (unlikely(ipv6_optlen(new) > optlen)) {
			kfree(new);
			return -EINVAL;
		}
	}

	opt = rcu_dereference_protected(np->opt, lockdep_sock_is_held(sk));
	opt = ipv6_renew_options(sk, opt, optname, new);
	kfree(new);
	if (IS_ERR(opt))
		return PTR_ERR(opt);

	/* routing header option needs extra check */
	err = -EINVAL;
	if (optname == IPV6_RTHDR && opt && opt->srcrt) {
		struct ipv6_rt_hdr *rthdr = opt->srcrt;
		switch (rthdr->type) {
#if IS_ENABLED(CONFIG_IPV6_MIP6)
		case IPV6_SRCRT_TYPE_2:
			if (rthdr->hdrlen != 2 || rthdr->segments_left != 1)
				goto sticky_done;
			break;
#endif
		case IPV6_SRCRT_TYPE_4:
		{
			struct ipv6_sr_hdr *srh =
				(struct ipv6_sr_hdr *)opt->srcrt;

			if (!seg6_validate_srh(srh, optlen, false))
				goto sticky_done;
			break;
		}
		default:
			goto sticky_done;
		}
	}

	err = 0;
	opt = ipv6_update_options(sk, opt);
sticky_done:
	if (opt) {
		atomic_sub(opt->tot_len, &sk->sk_omem_alloc);
		txopt_put(opt);
	}
	return err;
}

static int do_ipv6_setsockopt(struct sock *sk, int level, int optname,
		   sockptr_t optval, unsigned int optlen)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct net *net = sock_net(sk);
	int val, valbool;
	int retv = -ENOPROTOOPT;
	bool needs_rtnl = setsockopt_needs_rtnl(optname);

	if (sockptr_is_null(optval))
		val = 0;
	else {
		if (optlen >= sizeof(int)) {
			if (copy_from_sockptr(&val, optval, sizeof(val)))
				return -EFAULT;
		} else
			val = 0;
	}

	valbool = (val != 0);

	if (ip6_mroute_opt(optname))
		return ip6_mroute_setsockopt(sk, optname, optval, optlen);

	if (needs_rtnl)
		rtnl_lock();
	lock_sock(sk);

	/* Another thread has converted the socket into IPv4 with
	 * IPV6_ADDRFORM concurrently.
	 */
	if (unlikely(sk->sk_family != AF_INET6))
		goto unlock;

	switch (optname) {

	case IPV6_ADDRFORM:
		if (optlen < sizeof(int))
			goto e_inval;
		if (val == PF_INET) {
			struct ipv6_txoptions *opt;
			struct sk_buff *pktopt;

			if (sk->sk_type == SOCK_RAW)
				break;

			if (sk->sk_protocol == IPPROTO_UDP ||
			    sk->sk_protocol == IPPROTO_UDPLITE) {
				struct udp_sock *up = udp_sk(sk);
				if (up->pending == AF_INET6) {
					retv = -EBUSY;
					break;
				}
			} else if (sk->sk_protocol == IPPROTO_TCP) {
				if (sk->sk_prot != &tcpv6_prot) {
					retv = -EBUSY;
					break;
				}
			} else {
				break;
			}

			if (sk->sk_state != TCP_ESTABLISHED) {
				retv = -ENOTCONN;
				break;
			}

			if (ipv6_only_sock(sk) ||
			    !ipv6_addr_v4mapped(&sk->sk_v6_daddr)) {
				retv = -EADDRNOTAVAIL;
				break;
			}

			fl6_free_socklist(sk);
			__ipv6_sock_mc_close(sk);
			__ipv6_sock_ac_close(sk);

			/*
			 * Sock is moving from IPv6 to IPv4 (sk_prot), so
			 * remove it from the refcnt debug socks count in the
			 * original family...
			 */
			sk_refcnt_debug_dec(sk);

			if (sk->sk_protocol == IPPROTO_TCP) {
				struct inet_connection_sock *icsk = inet_csk(sk);
				local_bh_disable();
				sock_prot_inuse_add(net, sk->sk_prot, -1);
				sock_prot_inuse_add(net, &tcp_prot, 1);
				local_bh_enable();
				sk->sk_prot = &tcp_prot;
				icsk->icsk_af_ops = &ipv4_specific;
				sk->sk_socket->ops = &inet_stream_ops;
				sk->sk_family = PF_INET;
				tcp_sync_mss(sk, icsk->icsk_pmtu_cookie);
			} else {
				struct proto *prot = &udp_prot;

				if (sk->sk_protocol == IPPROTO_UDPLITE)
					prot = &udplite_prot;
				local_bh_disable();
				sock_prot_inuse_add(net, sk->sk_prot, -1);
				sock_prot_inuse_add(net, prot, 1);
				local_bh_enable();
				sk->sk_prot = prot;
				sk->sk_socket->ops = &inet_dgram_ops;
				sk->sk_family = PF_INET;
			}
			opt = xchg((__force struct ipv6_txoptions **)&np->opt,
				   NULL);
			if (opt) {
				atomic_sub(opt->tot_len, &sk->sk_omem_alloc);
				txopt_put(opt);
			}
			pktopt = xchg(&np->pktoptions, NULL);
			kfree_skb(pktopt);

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
		if (optlen < sizeof(int) ||
		    inet_sk(sk)->inet_num)
			goto e_inval;
		sk->sk_ipv6only = valbool;
		retv = 0;
		break;

	case IPV6_RECVPKTINFO:
		if (optlen < sizeof(int))
			goto e_inval;
		np->rxopt.bits.rxinfo = valbool;
		retv = 0;
		break;

	case IPV6_2292PKTINFO:
		if (optlen < sizeof(int))
			goto e_inval;
		np->rxopt.bits.rxoinfo = valbool;
		retv = 0;
		break;

	case IPV6_RECVHOPLIMIT:
		if (optlen < sizeof(int))
			goto e_inval;
		np->rxopt.bits.rxhlim = valbool;
		retv = 0;
		break;

	case IPV6_2292HOPLIMIT:
		if (optlen < sizeof(int))
			goto e_inval;
		np->rxopt.bits.rxohlim = valbool;
		retv = 0;
		break;

	case IPV6_RECVRTHDR:
		if (optlen < sizeof(int))
			goto e_inval;
		np->rxopt.bits.srcrt = valbool;
		retv = 0;
		break;

	case IPV6_2292RTHDR:
		if (optlen < sizeof(int))
			goto e_inval;
		np->rxopt.bits.osrcrt = valbool;
		retv = 0;
		break;

	case IPV6_RECVHOPOPTS:
		if (optlen < sizeof(int))
			goto e_inval;
		np->rxopt.bits.hopopts = valbool;
		retv = 0;
		break;

	case IPV6_2292HOPOPTS:
		if (optlen < sizeof(int))
			goto e_inval;
		np->rxopt.bits.ohopopts = valbool;
		retv = 0;
		break;

	case IPV6_RECVDSTOPTS:
		if (optlen < sizeof(int))
			goto e_inval;
		np->rxopt.bits.dstopts = valbool;
		retv = 0;
		break;

	case IPV6_2292DSTOPTS:
		if (optlen < sizeof(int))
			goto e_inval;
		np->rxopt.bits.odstopts = valbool;
		retv = 0;
		break;

	case IPV6_TCLASS:
		if (optlen < sizeof(int))
			goto e_inval;
		if (val < -1 || val > 0xff)
			goto e_inval;
		/* RFC 3542, 6.5: default traffic class of 0x0 */
		if (val == -1)
			val = 0;
		np->tclass = val;
		retv = 0;
		break;

	case IPV6_RECVTCLASS:
		if (optlen < sizeof(int))
			goto e_inval;
		np->rxopt.bits.rxtclass = valbool;
		retv = 0;
		break;

	case IPV6_FLOWINFO:
		if (optlen < sizeof(int))
			goto e_inval;
		np->rxopt.bits.rxflow = valbool;
		retv = 0;
		break;

	case IPV6_RECVPATHMTU:
		if (optlen < sizeof(int))
			goto e_inval;
		np->rxopt.bits.rxpmtu = valbool;
		retv = 0;
		break;

	case IPV6_TRANSPARENT:
		if (valbool && !ns_capable(net->user_ns, CAP_NET_RAW) &&
		    !ns_capable(net->user_ns, CAP_NET_ADMIN)) {
			retv = -EPERM;
			break;
		}
		if (optlen < sizeof(int))
			goto e_inval;
		/* we don't have a separate transparent bit for IPV6 we use the one in the IPv4 socket */
		inet_sk(sk)->transparent = valbool;
		retv = 0;
		break;

	case IPV6_FREEBIND:
		if (optlen < sizeof(int))
			goto e_inval;
		/* we also don't have a separate freebind bit for IPV6 */
		inet_sk(sk)->freebind = valbool;
		retv = 0;
		break;

	case IPV6_RECVORIGDSTADDR:
		if (optlen < sizeof(int))
			goto e_inval;
		np->rxopt.bits.rxorigdstaddr = valbool;
		retv = 0;
		break;

	case IPV6_HOPOPTS:
	case IPV6_RTHDRDSTOPTS:
	case IPV6_RTHDR:
	case IPV6_DSTOPTS:
		retv = ipv6_set_opt_hdr(sk, optname, optval, optlen);
		break;

	case IPV6_PKTINFO:
	{
		struct in6_pktinfo pkt;

		if (optlen == 0)
			goto e_inval;
		else if (optlen < sizeof(struct in6_pktinfo) ||
			 sockptr_is_null(optval))
			goto e_inval;

		if (copy_from_sockptr(&pkt, optval, sizeof(pkt))) {
			retv = -EFAULT;
			break;
		}
		if (!sk_dev_equal_l3scope(sk, pkt.ipi6_ifindex))
			goto e_inval;

		np->sticky_pktinfo.ipi6_ifindex = pkt.ipi6_ifindex;
		np->sticky_pktinfo.ipi6_addr = pkt.ipi6_addr;
		retv = 0;
		break;
	}

	case IPV6_2292PKTOPTIONS:
	{
		struct ipv6_txoptions *opt = NULL;
		struct msghdr msg;
		struct flowi6 fl6;
		struct ipcm6_cookie ipc6;

		memset(&fl6, 0, sizeof(fl6));
		fl6.flowi6_oif = sk->sk_bound_dev_if;
		fl6.flowi6_mark = sk->sk_mark;

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
		if (!opt)
			break;

		memset(opt, 0, sizeof(*opt));
		refcount_set(&opt->refcnt, 1);
		opt->tot_len = sizeof(*opt) + optlen;
		retv = -EFAULT;
		if (copy_from_sockptr(opt + 1, optval, optlen))
			goto done;

		msg.msg_controllen = optlen;
		msg.msg_control = (void *)(opt+1);
		ipc6.opt = opt;

		retv = ip6_datagram_send_ctl(net, sk, &msg, &fl6, &ipc6);
		if (retv)
			goto done;
update:
		retv = 0;
		opt = ipv6_update_options(sk, opt);
done:
		if (opt) {
			atomic_sub(opt->tot_len, &sk->sk_omem_alloc);
			txopt_put(opt);
		}
		break;
	}
	case IPV6_UNICAST_HOPS:
		if (optlen < sizeof(int))
			goto e_inval;
		if (val > 255 || val < -1)
			goto e_inval;
		np->hop_limit = val;
		retv = 0;
		break;

	case IPV6_MULTICAST_HOPS:
		if (sk->sk_type == SOCK_STREAM)
			break;
		if (optlen < sizeof(int))
			goto e_inval;
		if (val > 255 || val < -1)
			goto e_inval;
		np->mcast_hops = (val == -1 ? IPV6_DEFAULT_MCASTHOPS : val);
		retv = 0;
		break;

	case IPV6_MULTICAST_LOOP:
		if (optlen < sizeof(int))
			goto e_inval;
		if (val != valbool)
			goto e_inval;
		np->mc_loop = valbool;
		retv = 0;
		break;

	case IPV6_UNICAST_IF:
	{
		struct net_device *dev = NULL;
		int ifindex;

		if (optlen != sizeof(int))
			goto e_inval;

		ifindex = (__force int)ntohl((__force __be32)val);
		if (ifindex == 0) {
			np->ucast_oif = 0;
			retv = 0;
			break;
		}

		dev = dev_get_by_index(net, ifindex);
		retv = -EADDRNOTAVAIL;
		if (!dev)
			break;
		dev_put(dev);

		retv = -EINVAL;
		if (sk->sk_bound_dev_if)
			break;

		np->ucast_oif = ifindex;
		retv = 0;
		break;
	}

	case IPV6_MULTICAST_IF:
		if (sk->sk_type == SOCK_STREAM)
			break;
		if (optlen < sizeof(int))
			goto e_inval;

		if (val) {
			struct net_device *dev;
			int midx;

			rcu_read_lock();

			dev = dev_get_by_index_rcu(net, val);
			if (!dev) {
				rcu_read_unlock();
				retv = -ENODEV;
				break;
			}
			midx = l3mdev_master_ifindex_rcu(dev);

			rcu_read_unlock();

			if (sk->sk_bound_dev_if &&
			    sk->sk_bound_dev_if != val &&
			    (!midx || midx != sk->sk_bound_dev_if))
				goto e_inval;
		}
		np->mcast_oif = val;
		retv = 0;
		break;
	case IPV6_ADD_MEMBERSHIP:
	case IPV6_DROP_MEMBERSHIP:
	{
		struct ipv6_mreq mreq;

		if (optlen < sizeof(struct ipv6_mreq))
			goto e_inval;

		retv = -EPROTO;
		if (inet_sk(sk)->is_icsk)
			break;

		retv = -EFAULT;
		if (copy_from_sockptr(&mreq, optval, sizeof(struct ipv6_mreq)))
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

		if (optlen < sizeof(struct ipv6_mreq))
			goto e_inval;

		retv = -EFAULT;
		if (copy_from_sockptr(&mreq, optval, sizeof(struct ipv6_mreq)))
			break;

		if (optname == IPV6_JOIN_ANYCAST)
			retv = ipv6_sock_ac_join(sk, mreq.ipv6mr_ifindex, &mreq.ipv6mr_acaddr);
		else
			retv = ipv6_sock_ac_drop(sk, mreq.ipv6mr_ifindex, &mreq.ipv6mr_acaddr);
		break;
	}
	case IPV6_MULTICAST_ALL:
		if (optlen < sizeof(int))
			goto e_inval;
		np->mc_all = valbool;
		retv = 0;
		break;

	case MCAST_JOIN_GROUP:
	case MCAST_LEAVE_GROUP:
		if (in_compat_syscall())
			retv = compat_ipv6_mcast_join_leave(sk, optname, optval,
							    optlen);
		else
			retv = ipv6_mcast_join_leave(sk, optname, optval,
						     optlen);
		break;
	case MCAST_JOIN_SOURCE_GROUP:
	case MCAST_LEAVE_SOURCE_GROUP:
	case MCAST_BLOCK_SOURCE:
	case MCAST_UNBLOCK_SOURCE:
		retv = do_ipv6_mcast_group_source(sk, optname, optval, optlen);
		break;
	case MCAST_MSFILTER:
		if (in_compat_syscall())
			retv = compat_ipv6_set_mcast_msfilter(sk, optval,
							      optlen);
		else
			retv = ipv6_set_mcast_msfilter(sk, optval, optlen);
		break;
	case IPV6_ROUTER_ALERT:
		if (optlen < sizeof(int))
			goto e_inval;
		retv = ip6_ra_control(sk, val);
		break;
	case IPV6_ROUTER_ALERT_ISOLATE:
		if (optlen < sizeof(int))
			goto e_inval;
		np->rtalert_isolate = valbool;
		retv = 0;
		break;
	case IPV6_MTU_DISCOVER:
		if (optlen < sizeof(int))
			goto e_inval;
		if (val < IPV6_PMTUDISC_DONT || val > IPV6_PMTUDISC_OMIT)
			goto e_inval;
		np->pmtudisc = val;
		retv = 0;
		break;
	case IPV6_MTU:
		if (optlen < sizeof(int))
			goto e_inval;
		if (val && val < IPV6_MIN_MTU)
			goto e_inval;
		np->frag_size = val;
		retv = 0;
		break;
	case IPV6_RECVERR:
		if (optlen < sizeof(int))
			goto e_inval;
		np->recverr = valbool;
		if (!val)
			skb_queue_purge(&sk->sk_error_queue);
		retv = 0;
		break;
	case IPV6_FLOWINFO_SEND:
		if (optlen < sizeof(int))
			goto e_inval;
		np->sndflow = valbool;
		retv = 0;
		break;
	case IPV6_FLOWLABEL_MGR:
		retv = ipv6_flowlabel_opt(sk, optval, optlen);
		break;
	case IPV6_IPSEC_POLICY:
	case IPV6_XFRM_POLICY:
		retv = -EPERM;
		if (!ns_capable(net->user_ns, CAP_NET_ADMIN))
			break;
		retv = xfrm_user_policy(sk, optname, optval, optlen);
		break;

	case IPV6_ADDR_PREFERENCES:
		if (optlen < sizeof(int))
			goto e_inval;
		retv = __ip6_sock_set_addr_preferences(sk, val);
		break;
	case IPV6_MINHOPCOUNT:
		if (optlen < sizeof(int))
			goto e_inval;
		if (val < 0 || val > 255)
			goto e_inval;
		np->min_hopcount = val;
		retv = 0;
		break;
	case IPV6_DONTFRAG:
		np->dontfrag = valbool;
		retv = 0;
		break;
	case IPV6_AUTOFLOWLABEL:
		np->autoflowlabel = valbool;
		np->autoflowlabel_set = 1;
		retv = 0;
		break;
	case IPV6_RECVFRAGSIZE:
		np->rxopt.bits.recvfragsize = valbool;
		retv = 0;
		break;
	case IPV6_RECVERR_RFC4884:
		if (optlen < sizeof(int))
			goto e_inval;
		if (val < 0 || val > 1)
			goto e_inval;
		np->recverr_rfc4884 = valbool;
		retv = 0;
		break;
	}

unlock:
	release_sock(sk);
	if (needs_rtnl)
		rtnl_unlock();

	return retv;

e_inval:
	release_sock(sk);
	if (needs_rtnl)
		rtnl_unlock();
	return -EINVAL;
}

int ipv6_setsockopt(struct sock *sk, int level, int optname, sockptr_t optval,
		    unsigned int optlen)
{
	int err;

	if (level == SOL_IP && sk->sk_type != SOCK_RAW)
		return udp_prot.setsockopt(sk, level, optname, optval, optlen);

	if (level != SOL_IPV6)
		return -ENOPROTOOPT;

	err = do_ipv6_setsockopt(sk, level, optname, optval, optlen);
#ifdef CONFIG_NETFILTER
	/* we need to exclude all possible ENOPROTOOPTs except default case */
	if (err == -ENOPROTOOPT && optname != IPV6_IPSEC_POLICY &&
			optname != IPV6_XFRM_POLICY)
		err = nf_setsockopt(sk, PF_INET6, optname, optval, optlen);
#endif
	return err;
}
EXPORT_SYMBOL(ipv6_setsockopt);

static int ipv6_getsockopt_sticky(struct sock *sk, struct ipv6_txoptions *opt,
				  int optname, char __user *optval, int len)
{
	struct ipv6_opt_hdr *hdr;

	if (!opt)
		return 0;

	switch (optname) {
	case IPV6_HOPOPTS:
		hdr = opt->hopopt;
		break;
	case IPV6_RTHDRDSTOPTS:
		hdr = opt->dst0opt;
		break;
	case IPV6_RTHDR:
		hdr = (struct ipv6_opt_hdr *)opt->srcrt;
		break;
	case IPV6_DSTOPTS:
		hdr = opt->dst1opt;
		break;
	default:
		return -EINVAL;	/* should not happen */
	}

	if (!hdr)
		return 0;

	len = min_t(unsigned int, len, ipv6_optlen(hdr));
	if (copy_to_user(optval, hdr, len))
		return -EFAULT;
	return len;
}

static int ipv6_get_msfilter(struct sock *sk, void __user *optval,
		int __user *optlen, int len)
{
	const int size0 = offsetof(struct group_filter, gf_slist_flex);
	struct group_filter __user *p = optval;
	struct group_filter gsf;
	int num;
	int err;

	if (len < size0)
		return -EINVAL;
	if (copy_from_user(&gsf, p, size0))
		return -EFAULT;
	if (gsf.gf_group.ss_family != AF_INET6)
		return -EADDRNOTAVAIL;
	num = gsf.gf_numsrc;
	lock_sock(sk);
	err = ip6_mc_msfget(sk, &gsf, p->gf_slist_flex);
	if (!err) {
		if (num > gsf.gf_numsrc)
			num = gsf.gf_numsrc;
		if (put_user(GROUP_FILTER_SIZE(num), optlen) ||
		    copy_to_user(p, &gsf, size0))
			err = -EFAULT;
	}
	release_sock(sk);
	return err;
}

static int compat_ipv6_get_msfilter(struct sock *sk, void __user *optval,
		int __user *optlen)
{
	const int size0 = offsetof(struct compat_group_filter, gf_slist_flex);
	struct compat_group_filter __user *p = optval;
	struct compat_group_filter gf32;
	struct group_filter gf;
	int len, err;
	int num;

	if (get_user(len, optlen))
		return -EFAULT;
	if (len < size0)
		return -EINVAL;

	if (copy_from_user(&gf32, p, size0))
		return -EFAULT;
	gf.gf_interface = gf32.gf_interface;
	gf.gf_fmode = gf32.gf_fmode;
	num = gf.gf_numsrc = gf32.gf_numsrc;
	gf.gf_group = gf32.gf_group;

	if (gf.gf_group.ss_family != AF_INET6)
		return -EADDRNOTAVAIL;

	lock_sock(sk);
	err = ip6_mc_msfget(sk, &gf, p->gf_slist_flex);
	release_sock(sk);
	if (err)
		return err;
	if (num > gf.gf_numsrc)
		num = gf.gf_numsrc;
	len = GROUP_FILTER_SIZE(num) - (sizeof(gf)-sizeof(gf32));
	if (put_user(len, optlen) ||
	    put_user(gf.gf_fmode, &p->gf_fmode) ||
	    put_user(gf.gf_numsrc, &p->gf_numsrc))
		return -EFAULT;
	return 0;
}

static int do_ipv6_getsockopt(struct sock *sk, int level, int optname,
		    char __user *optval, int __user *optlen, unsigned int flags)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	int len;
	int val;

	if (ip6_mroute_opt(optname))
		return ip6_mroute_getsockopt(sk, optname, optval, optlen);

	if (get_user(len, optlen))
		return -EFAULT;
	switch (optname) {
	case IPV6_ADDRFORM:
		if (sk->sk_protocol != IPPROTO_UDP &&
		    sk->sk_protocol != IPPROTO_UDPLITE &&
		    sk->sk_protocol != IPPROTO_TCP)
			return -ENOPROTOOPT;
		if (sk->sk_state != TCP_ESTABLISHED)
			return -ENOTCONN;
		val = sk->sk_family;
		break;
	case MCAST_MSFILTER:
		if (in_compat_syscall())
			return compat_ipv6_get_msfilter(sk, optval, optlen);
		return ipv6_get_msfilter(sk, optval, optlen, len);
	case IPV6_2292PKTOPTIONS:
	{
		struct msghdr msg;
		struct sk_buff *skb;

		if (sk->sk_type != SOCK_STREAM)
			return -ENOPROTOOPT;

		msg.msg_control_user = optval;
		msg.msg_controllen = len;
		msg.msg_flags = flags;
		msg.msg_control_is_user = true;

		lock_sock(sk);
		skb = np->pktoptions;
		if (skb)
			ip6_datagram_recv_ctl(sk, &msg, skb);
		release_sock(sk);
		if (!skb) {
			if (np->rxopt.bits.rxinfo) {
				struct in6_pktinfo src_info;
				src_info.ipi6_ifindex = np->mcast_oif ? np->mcast_oif :
					np->sticky_pktinfo.ipi6_ifindex;
				src_info.ipi6_addr = np->mcast_oif ? sk->sk_v6_daddr : np->sticky_pktinfo.ipi6_addr;
				put_cmsg(&msg, SOL_IPV6, IPV6_PKTINFO, sizeof(src_info), &src_info);
			}
			if (np->rxopt.bits.rxhlim) {
				int hlim = np->mcast_hops;
				put_cmsg(&msg, SOL_IPV6, IPV6_HOPLIMIT, sizeof(hlim), &hlim);
			}
			if (np->rxopt.bits.rxtclass) {
				int tclass = (int)ip6_tclass(np->rcv_flowinfo);

				put_cmsg(&msg, SOL_IPV6, IPV6_TCLASS, sizeof(tclass), &tclass);
			}
			if (np->rxopt.bits.rxoinfo) {
				struct in6_pktinfo src_info;
				src_info.ipi6_ifindex = np->mcast_oif ? np->mcast_oif :
					np->sticky_pktinfo.ipi6_ifindex;
				src_info.ipi6_addr = np->mcast_oif ? sk->sk_v6_daddr :
								     np->sticky_pktinfo.ipi6_addr;
				put_cmsg(&msg, SOL_IPV6, IPV6_2292PKTINFO, sizeof(src_info), &src_info);
			}
			if (np->rxopt.bits.rxohlim) {
				int hlim = np->mcast_hops;
				put_cmsg(&msg, SOL_IPV6, IPV6_2292HOPLIMIT, sizeof(hlim), &hlim);
			}
			if (np->rxopt.bits.rxflow) {
				__be32 flowinfo = np->rcv_flowinfo;

				put_cmsg(&msg, SOL_IPV6, IPV6_FLOWINFO, sizeof(flowinfo), &flowinfo);
			}
		}
		len -= msg.msg_controllen;
		return put_user(len, optlen);
	}
	case IPV6_MTU:
	{
		struct dst_entry *dst;

		val = 0;
		rcu_read_lock();
		dst = __sk_dst_get(sk);
		if (dst)
			val = dst_mtu(dst);
		rcu_read_unlock();
		if (!val)
			return -ENOTCONN;
		break;
	}

	case IPV6_V6ONLY:
		val = sk->sk_ipv6only;
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
		struct ipv6_txoptions *opt;

		lock_sock(sk);
		opt = rcu_dereference_protected(np->opt,
						lockdep_sock_is_held(sk));
		len = ipv6_getsockopt_sticky(sk, opt, optname, optval, len);
		release_sock(sk);
		/* check if ipv6_getsockopt_sticky() returns err code */
		if (len < 0)
			return len;
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

	case IPV6_RECVPATHMTU:
		val = np->rxopt.bits.rxpmtu;
		break;

	case IPV6_PATHMTU:
	{
		struct dst_entry *dst;
		struct ip6_mtuinfo mtuinfo;

		if (len < sizeof(mtuinfo))
			return -EINVAL;

		len = sizeof(mtuinfo);
		memset(&mtuinfo, 0, sizeof(mtuinfo));

		rcu_read_lock();
		dst = __sk_dst_get(sk);
		if (dst)
			mtuinfo.ip6m_mtu = dst_mtu(dst);
		rcu_read_unlock();
		if (!mtuinfo.ip6m_mtu)
			return -ENOTCONN;

		if (put_user(len, optlen))
			return -EFAULT;
		if (copy_to_user(optval, &mtuinfo, len))
			return -EFAULT;

		return 0;
	}

	case IPV6_TRANSPARENT:
		val = inet_sk(sk)->transparent;
		break;

	case IPV6_FREEBIND:
		val = inet_sk(sk)->freebind;
		break;

	case IPV6_RECVORIGDSTADDR:
		val = np->rxopt.bits.rxorigdstaddr;
		break;

	case IPV6_UNICAST_HOPS:
	case IPV6_MULTICAST_HOPS:
	{
		struct dst_entry *dst;

		if (optname == IPV6_UNICAST_HOPS)
			val = np->hop_limit;
		else
			val = np->mcast_hops;

		if (val < 0) {
			rcu_read_lock();
			dst = __sk_dst_get(sk);
			if (dst)
				val = ip6_dst_hoplimit(dst);
			rcu_read_unlock();
		}

		if (val < 0)
			val = sock_net(sk)->ipv6.devconf_all->hop_limit;
		break;
	}

	case IPV6_MULTICAST_LOOP:
		val = np->mc_loop;
		break;

	case IPV6_MULTICAST_IF:
		val = np->mcast_oif;
		break;

	case IPV6_MULTICAST_ALL:
		val = np->mc_all;
		break;

	case IPV6_UNICAST_IF:
		val = (__force int)htonl((__u32) np->ucast_oif);
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

	case IPV6_FLOWLABEL_MGR:
	{
		struct in6_flowlabel_req freq;
		int flags;

		if (len < sizeof(freq))
			return -EINVAL;

		if (copy_from_user(&freq, optval, sizeof(freq)))
			return -EFAULT;

		if (freq.flr_action != IPV6_FL_A_GET)
			return -EINVAL;

		len = sizeof(freq);
		flags = freq.flr_flags;

		memset(&freq, 0, sizeof(freq));

		val = ipv6_flowlabel_opt_get(sk, &freq, flags);
		if (val < 0)
			return val;

		if (put_user(len, optlen))
			return -EFAULT;
		if (copy_to_user(optval, &freq, len))
			return -EFAULT;

		return 0;
	}

	case IPV6_ADDR_PREFERENCES:
		val = 0;

		if (np->srcprefs & IPV6_PREFER_SRC_TMP)
			val |= IPV6_PREFER_SRC_TMP;
		else if (np->srcprefs & IPV6_PREFER_SRC_PUBLIC)
			val |= IPV6_PREFER_SRC_PUBLIC;
		else {
			/* XXX: should we return system default? */
			val |= IPV6_PREFER_SRC_PUBTMP_DEFAULT;
		}

		if (np->srcprefs & IPV6_PREFER_SRC_COA)
			val |= IPV6_PREFER_SRC_COA;
		else
			val |= IPV6_PREFER_SRC_HOME;
		break;

	case IPV6_MINHOPCOUNT:
		val = np->min_hopcount;
		break;

	case IPV6_DONTFRAG:
		val = np->dontfrag;
		break;

	case IPV6_AUTOFLOWLABEL:
		val = ip6_autoflowlabel(sock_net(sk), np);
		break;

	case IPV6_RECVFRAGSIZE:
		val = np->rxopt.bits.recvfragsize;
		break;

	case IPV6_ROUTER_ALERT_ISOLATE:
		val = np->rtalert_isolate;
		break;

	case IPV6_RECVERR_RFC4884:
		val = np->recverr_rfc4884;
		break;

	default:
		return -ENOPROTOOPT;
	}
	len = min_t(unsigned int, sizeof(int), len);
	if (put_user(len, optlen))
		return -EFAULT;
	if (copy_to_user(optval, &val, len))
		return -EFAULT;
	return 0;
}

int ipv6_getsockopt(struct sock *sk, int level, int optname,
		    char __user *optval, int __user *optlen)
{
	int err;

	if (level == SOL_IP && sk->sk_type != SOCK_RAW)
		return udp_prot.getsockopt(sk, level, optname, optval, optlen);

	if (level != SOL_IPV6)
		return -ENOPROTOOPT;

	err = do_ipv6_getsockopt(sk, level, optname, optval, optlen, 0);
#ifdef CONFIG_NETFILTER
	/* we need to exclude all possible ENOPROTOOPTs except default case */
	if (err == -ENOPROTOOPT && optname != IPV6_2292PKTOPTIONS) {
		int len;

		if (get_user(len, optlen))
			return -EFAULT;

		err = nf_getsockopt(sk, PF_INET6, optname, optval, &len);
		if (err >= 0)
			err = put_user(len, optlen);
	}
#endif
	return err;
}
EXPORT_SYMBOL(ipv6_getsockopt);
