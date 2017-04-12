/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		The IP to API glue.
 *
 * Authors:	see ip.c
 *
 * Fixes:
 *		Many		:	Split from ip.c , see ip.c for history.
 *		Martin Mares	:	TOS setting fixed.
 *		Alan Cox	:	Fixed a couple of oopses in Martin's
 *					TOS tweaks.
 *		Mike McLagan	:	Routing by source
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/icmp.h>
#include <linux/inetdevice.h>
#include <linux/netdevice.h>
#include <linux/slab.h>
#include <net/sock.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <net/tcp_states.h>
#include <linux/udp.h>
#include <linux/igmp.h>
#include <linux/netfilter.h>
#include <linux/route.h>
#include <linux/mroute.h>
#include <net/inet_ecn.h>
#include <net/route.h>
#include <net/xfrm.h>
#include <net/compat.h>
#include <net/checksum.h>
#if IS_ENABLED(CONFIG_IPV6)
#include <net/transp_v6.h>
#endif
#include <net/ip_fib.h>

#include <linux/errqueue.h>
#include <asm/uaccess.h>

/*
 *	SOL_IP control messages.
 */

static void ip_cmsg_recv_pktinfo(struct msghdr *msg, struct sk_buff *skb)
{
	struct in_pktinfo info = *PKTINFO_SKB_CB(skb);

	info.ipi_addr.s_addr = ip_hdr(skb)->daddr;

	put_cmsg(msg, SOL_IP, IP_PKTINFO, sizeof(info), &info);
}

static void ip_cmsg_recv_ttl(struct msghdr *msg, struct sk_buff *skb)
{
	int ttl = ip_hdr(skb)->ttl;
	put_cmsg(msg, SOL_IP, IP_TTL, sizeof(int), &ttl);
}

static void ip_cmsg_recv_tos(struct msghdr *msg, struct sk_buff *skb)
{
	put_cmsg(msg, SOL_IP, IP_TOS, 1, &ip_hdr(skb)->tos);
}

static void ip_cmsg_recv_opts(struct msghdr *msg, struct sk_buff *skb)
{
	if (IPCB(skb)->opt.optlen == 0)
		return;

	put_cmsg(msg, SOL_IP, IP_RECVOPTS, IPCB(skb)->opt.optlen,
		 ip_hdr(skb) + 1);
}


static void ip_cmsg_recv_retopts(struct msghdr *msg, struct sk_buff *skb)
{
	unsigned char optbuf[sizeof(struct ip_options) + 40];
	struct ip_options *opt = (struct ip_options *)optbuf;

	if (IPCB(skb)->opt.optlen == 0)
		return;

	if (ip_options_echo(opt, skb)) {
		msg->msg_flags |= MSG_CTRUNC;
		return;
	}
	ip_options_undo(opt);

	put_cmsg(msg, SOL_IP, IP_RETOPTS, opt->optlen, opt->__data);
}

static void ip_cmsg_recv_checksum(struct msghdr *msg, struct sk_buff *skb,
				  int tlen, int offset)
{
	__wsum csum = skb->csum;

	if (skb->ip_summed != CHECKSUM_COMPLETE)
		return;

	if (offset != 0) {
		int tend_off = skb_transport_offset(skb) + tlen;
		csum = csum_sub(csum, skb_checksum(skb, tend_off, offset, 0));
	}

	put_cmsg(msg, SOL_IP, IP_CHECKSUM, sizeof(__wsum), &csum);
}

static void ip_cmsg_recv_security(struct msghdr *msg, struct sk_buff *skb)
{
	char *secdata;
	u32 seclen, secid;
	int err;

	err = security_socket_getpeersec_dgram(NULL, skb, &secid);
	if (err)
		return;

	err = security_secid_to_secctx(secid, &secdata, &seclen);
	if (err)
		return;

	put_cmsg(msg, SOL_IP, SCM_SECURITY, seclen, secdata);
	security_release_secctx(secdata, seclen);
}

static void ip_cmsg_recv_dstaddr(struct msghdr *msg, struct sk_buff *skb)
{
	struct sockaddr_in sin;
	const struct iphdr *iph = ip_hdr(skb);
	__be16 *ports = (__be16 *)skb_transport_header(skb);

	if (skb_transport_offset(skb) + 4 > (int)skb->len)
		return;

	/* All current transport protocols have the port numbers in the
	 * first four bytes of the transport header and this function is
	 * written with this assumption in mind.
	 */

	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = iph->daddr;
	sin.sin_port = ports[1];
	memset(sin.sin_zero, 0, sizeof(sin.sin_zero));

	put_cmsg(msg, SOL_IP, IP_ORIGDSTADDR, sizeof(sin), &sin);
}

void ip_cmsg_recv_offset(struct msghdr *msg, struct sk_buff *skb,
			 int tlen, int offset)
{
	struct inet_sock *inet = inet_sk(skb->sk);
	unsigned int flags = inet->cmsg_flags;

	/* Ordered by supposed usage frequency */
	if (flags & IP_CMSG_PKTINFO) {
		ip_cmsg_recv_pktinfo(msg, skb);

		flags &= ~IP_CMSG_PKTINFO;
		if (!flags)
			return;
	}

	if (flags & IP_CMSG_TTL) {
		ip_cmsg_recv_ttl(msg, skb);

		flags &= ~IP_CMSG_TTL;
		if (!flags)
			return;
	}

	if (flags & IP_CMSG_TOS) {
		ip_cmsg_recv_tos(msg, skb);

		flags &= ~IP_CMSG_TOS;
		if (!flags)
			return;
	}

	if (flags & IP_CMSG_RECVOPTS) {
		ip_cmsg_recv_opts(msg, skb);

		flags &= ~IP_CMSG_RECVOPTS;
		if (!flags)
			return;
	}

	if (flags & IP_CMSG_RETOPTS) {
		ip_cmsg_recv_retopts(msg, skb);

		flags &= ~IP_CMSG_RETOPTS;
		if (!flags)
			return;
	}

	if (flags & IP_CMSG_PASSSEC) {
		ip_cmsg_recv_security(msg, skb);

		flags &= ~IP_CMSG_PASSSEC;
		if (!flags)
			return;
	}

	if (flags & IP_CMSG_ORIGDSTADDR) {
		ip_cmsg_recv_dstaddr(msg, skb);

		flags &= ~IP_CMSG_ORIGDSTADDR;
		if (!flags)
			return;
	}

	if (flags & IP_CMSG_CHECKSUM)
		ip_cmsg_recv_checksum(msg, skb, tlen, offset);
}
EXPORT_SYMBOL(ip_cmsg_recv_offset);

int ip_cmsg_send(struct sock *sk, struct msghdr *msg, struct ipcm_cookie *ipc,
		 bool allow_ipv6)
{
	int err, val;
	struct cmsghdr *cmsg;
	struct net *net = sock_net(sk);

	for_each_cmsghdr(cmsg, msg) {
		if (!CMSG_OK(msg, cmsg))
			return -EINVAL;
#if IS_ENABLED(CONFIG_IPV6)
		if (allow_ipv6 &&
		    cmsg->cmsg_level == SOL_IPV6 &&
		    cmsg->cmsg_type == IPV6_PKTINFO) {
			struct in6_pktinfo *src_info;

			if (cmsg->cmsg_len < CMSG_LEN(sizeof(*src_info)))
				return -EINVAL;
			src_info = (struct in6_pktinfo *)CMSG_DATA(cmsg);
			if (!ipv6_addr_v4mapped(&src_info->ipi6_addr))
				return -EINVAL;
			ipc->oif = src_info->ipi6_ifindex;
			ipc->addr = src_info->ipi6_addr.s6_addr32[3];
			continue;
		}
#endif
		if (cmsg->cmsg_level == SOL_SOCKET) {
			err = __sock_cmsg_send(sk, msg, cmsg, &ipc->sockc);
			if (err)
				return err;
			continue;
		}

		if (cmsg->cmsg_level != SOL_IP)
			continue;
		switch (cmsg->cmsg_type) {
		case IP_RETOPTS:
			err = cmsg->cmsg_len - CMSG_ALIGN(sizeof(struct cmsghdr));

			/* Our caller is responsible for freeing ipc->opt */
			err = ip_options_get(net, &ipc->opt, CMSG_DATA(cmsg),
					     err < 40 ? err : 40);
			if (err)
				return err;
			break;
		case IP_PKTINFO:
		{
			struct in_pktinfo *info;
			if (cmsg->cmsg_len != CMSG_LEN(sizeof(struct in_pktinfo)))
				return -EINVAL;
			info = (struct in_pktinfo *)CMSG_DATA(cmsg);
			ipc->oif = info->ipi_ifindex;
			ipc->addr = info->ipi_spec_dst.s_addr;
			break;
		}
		case IP_TTL:
			if (cmsg->cmsg_len != CMSG_LEN(sizeof(int)))
				return -EINVAL;
			val = *(int *)CMSG_DATA(cmsg);
			if (val < 1 || val > 255)
				return -EINVAL;
			ipc->ttl = val;
			break;
		case IP_TOS:
			if (cmsg->cmsg_len == CMSG_LEN(sizeof(int)))
				val = *(int *)CMSG_DATA(cmsg);
			else if (cmsg->cmsg_len == CMSG_LEN(sizeof(u8)))
				val = *(u8 *)CMSG_DATA(cmsg);
			else
				return -EINVAL;
			if (val < 0 || val > 255)
				return -EINVAL;
			ipc->tos = val;
			ipc->priority = rt_tos2priority(ipc->tos);
			break;

		default:
			return -EINVAL;
		}
	}
	return 0;
}


/* Special input handler for packets caught by router alert option.
   They are selected only by protocol field, and then processed likely
   local ones; but only if someone wants them! Otherwise, router
   not running rsvpd will kill RSVP.

   It is user level problem, what it will make with them.
   I have no idea, how it will masquearde or NAT them (it is joke, joke :-)),
   but receiver should be enough clever f.e. to forward mtrace requests,
   sent to multicast group to reach destination designated router.
 */
struct ip_ra_chain __rcu *ip_ra_chain;
static DEFINE_SPINLOCK(ip_ra_lock);


static void ip_ra_destroy_rcu(struct rcu_head *head)
{
	struct ip_ra_chain *ra = container_of(head, struct ip_ra_chain, rcu);

	sock_put(ra->saved_sk);
	kfree(ra);
}

int ip_ra_control(struct sock *sk, unsigned char on,
		  void (*destructor)(struct sock *))
{
	struct ip_ra_chain *ra, *new_ra;
	struct ip_ra_chain __rcu **rap;

	if (sk->sk_type != SOCK_RAW || inet_sk(sk)->inet_num == IPPROTO_RAW)
		return -EINVAL;

	new_ra = on ? kmalloc(sizeof(*new_ra), GFP_KERNEL) : NULL;

	spin_lock_bh(&ip_ra_lock);
	for (rap = &ip_ra_chain;
	     (ra = rcu_dereference_protected(*rap,
			lockdep_is_held(&ip_ra_lock))) != NULL;
	     rap = &ra->next) {
		if (ra->sk == sk) {
			if (on) {
				spin_unlock_bh(&ip_ra_lock);
				kfree(new_ra);
				return -EADDRINUSE;
			}
			/* dont let ip_call_ra_chain() use sk again */
			ra->sk = NULL;
			RCU_INIT_POINTER(*rap, ra->next);
			spin_unlock_bh(&ip_ra_lock);

			if (ra->destructor)
				ra->destructor(sk);
			/*
			 * Delay sock_put(sk) and kfree(ra) after one rcu grace
			 * period. This guarantee ip_call_ra_chain() dont need
			 * to mess with socket refcounts.
			 */
			ra->saved_sk = sk;
			call_rcu(&ra->rcu, ip_ra_destroy_rcu);
			return 0;
		}
	}
	if (!new_ra) {
		spin_unlock_bh(&ip_ra_lock);
		return -ENOBUFS;
	}
	new_ra->sk = sk;
	new_ra->destructor = destructor;

	RCU_INIT_POINTER(new_ra->next, ra);
	rcu_assign_pointer(*rap, new_ra);
	sock_hold(sk);
	spin_unlock_bh(&ip_ra_lock);

	return 0;
}

void ip_icmp_error(struct sock *sk, struct sk_buff *skb, int err,
		   __be16 port, u32 info, u8 *payload)
{
	struct sock_exterr_skb *serr;

	skb = skb_clone(skb, GFP_ATOMIC);
	if (!skb)
		return;

	serr = SKB_EXT_ERR(skb);
	serr->ee.ee_errno = err;
	serr->ee.ee_origin = SO_EE_ORIGIN_ICMP;
	serr->ee.ee_type = icmp_hdr(skb)->type;
	serr->ee.ee_code = icmp_hdr(skb)->code;
	serr->ee.ee_pad = 0;
	serr->ee.ee_info = info;
	serr->ee.ee_data = 0;
	serr->addr_offset = (u8 *)&(((struct iphdr *)(icmp_hdr(skb) + 1))->daddr) -
				   skb_network_header(skb);
	serr->port = port;

	if (skb_pull(skb, payload - skb->data)) {
		skb_reset_transport_header(skb);
		if (sock_queue_err_skb(sk, skb) == 0)
			return;
	}
	kfree_skb(skb);
}

void ip_local_error(struct sock *sk, int err, __be32 daddr, __be16 port, u32 info)
{
	struct inet_sock *inet = inet_sk(sk);
	struct sock_exterr_skb *serr;
	struct iphdr *iph;
	struct sk_buff *skb;

	if (!inet->recverr)
		return;

	skb = alloc_skb(sizeof(struct iphdr), GFP_ATOMIC);
	if (!skb)
		return;

	skb_put(skb, sizeof(struct iphdr));
	skb_reset_network_header(skb);
	iph = ip_hdr(skb);
	iph->daddr = daddr;

	serr = SKB_EXT_ERR(skb);
	serr->ee.ee_errno = err;
	serr->ee.ee_origin = SO_EE_ORIGIN_LOCAL;
	serr->ee.ee_type = 0;
	serr->ee.ee_code = 0;
	serr->ee.ee_pad = 0;
	serr->ee.ee_info = info;
	serr->ee.ee_data = 0;
	serr->addr_offset = (u8 *)&iph->daddr - skb_network_header(skb);
	serr->port = port;

	__skb_pull(skb, skb_tail_pointer(skb) - skb->data);
	skb_reset_transport_header(skb);

	if (sock_queue_err_skb(sk, skb))
		kfree_skb(skb);
}

/* For some errors we have valid addr_offset even with zero payload and
 * zero port. Also, addr_offset should be supported if port is set.
 */
static inline bool ipv4_datagram_support_addr(struct sock_exterr_skb *serr)
{
	return serr->ee.ee_origin == SO_EE_ORIGIN_ICMP ||
	       serr->ee.ee_origin == SO_EE_ORIGIN_LOCAL || serr->port;
}

/* IPv4 supports cmsg on all imcp errors and some timestamps
 *
 * Timestamp code paths do not initialize the fields expected by cmsg:
 * the PKTINFO fields in skb->cb[]. Fill those in here.
 */
static bool ipv4_datagram_support_cmsg(const struct sock *sk,
				       struct sk_buff *skb,
				       int ee_origin)
{
	struct in_pktinfo *info;

	if (ee_origin == SO_EE_ORIGIN_ICMP)
		return true;

	if (ee_origin == SO_EE_ORIGIN_LOCAL)
		return false;

	/* Support IP_PKTINFO on tstamp packets if requested, to correlate
	 * timestamp with egress dev. Not possible for packets without iif
	 * or without payload (SOF_TIMESTAMPING_OPT_TSONLY).
	 */
	info = PKTINFO_SKB_CB(skb);
	if (!(sk->sk_tsflags & SOF_TIMESTAMPING_OPT_CMSG) ||
	    !info->ipi_ifindex)
		return false;

	info->ipi_spec_dst.s_addr = ip_hdr(skb)->saddr;
	return true;
}

/*
 *	Handle MSG_ERRQUEUE
 */
int ip_recv_error(struct sock *sk, struct msghdr *msg, int len, int *addr_len)
{
	struct sock_exterr_skb *serr;
	struct sk_buff *skb;
	DECLARE_SOCKADDR(struct sockaddr_in *, sin, msg->msg_name);
	struct {
		struct sock_extended_err ee;
		struct sockaddr_in	 offender;
	} errhdr;
	int err;
	int copied;

	WARN_ON_ONCE(sk->sk_family == AF_INET6);

	err = -EAGAIN;
	skb = sock_dequeue_err_skb(sk);
	if (!skb)
		goto out;

	copied = skb->len;
	if (copied > len) {
		msg->msg_flags |= MSG_TRUNC;
		copied = len;
	}
	err = skb_copy_datagram_msg(skb, 0, msg, copied);
	if (unlikely(err)) {
		kfree_skb(skb);
		return err;
	}
	sock_recv_timestamp(msg, sk, skb);

	serr = SKB_EXT_ERR(skb);

	if (sin && ipv4_datagram_support_addr(serr)) {
		sin->sin_family = AF_INET;
		sin->sin_addr.s_addr = *(__be32 *)(skb_network_header(skb) +
						   serr->addr_offset);
		sin->sin_port = serr->port;
		memset(&sin->sin_zero, 0, sizeof(sin->sin_zero));
		*addr_len = sizeof(*sin);
	}

	memcpy(&errhdr.ee, &serr->ee, sizeof(struct sock_extended_err));
	sin = &errhdr.offender;
	memset(sin, 0, sizeof(*sin));

	if (ipv4_datagram_support_cmsg(sk, skb, serr->ee.ee_origin)) {
		sin->sin_family = AF_INET;
		sin->sin_addr.s_addr = ip_hdr(skb)->saddr;
		if (inet_sk(sk)->cmsg_flags)
			ip_cmsg_recv(msg, skb);
	}

	put_cmsg(msg, SOL_IP, IP_RECVERR, sizeof(errhdr), &errhdr);

	/* Now we could try to dump offended packet options */

	msg->msg_flags |= MSG_ERRQUEUE;
	err = copied;

	consume_skb(skb);
out:
	return err;
}


/*
 *	Socket option code for IP. This is the end of the line after any
 *	TCP,UDP etc options on an IP socket.
 */
static bool setsockopt_needs_rtnl(int optname)
{
	switch (optname) {
	case IP_ADD_MEMBERSHIP:
	case IP_ADD_SOURCE_MEMBERSHIP:
	case IP_BLOCK_SOURCE:
	case IP_DROP_MEMBERSHIP:
	case IP_DROP_SOURCE_MEMBERSHIP:
	case IP_MSFILTER:
	case IP_UNBLOCK_SOURCE:
	case MCAST_BLOCK_SOURCE:
	case MCAST_MSFILTER:
	case MCAST_JOIN_GROUP:
	case MCAST_JOIN_SOURCE_GROUP:
	case MCAST_LEAVE_GROUP:
	case MCAST_LEAVE_SOURCE_GROUP:
	case MCAST_UNBLOCK_SOURCE:
		return true;
	}
	return false;
}

static int do_ip_setsockopt(struct sock *sk, int level,
			    int optname, char __user *optval, unsigned int optlen)
{
	struct inet_sock *inet = inet_sk(sk);
	struct net *net = sock_net(sk);
	int val = 0, err;
	bool needs_rtnl = setsockopt_needs_rtnl(optname);

	switch (optname) {
	case IP_PKTINFO:
	case IP_RECVTTL:
	case IP_RECVOPTS:
	case IP_RECVTOS:
	case IP_RETOPTS:
	case IP_TOS:
	case IP_TTL:
	case IP_HDRINCL:
	case IP_MTU_DISCOVER:
	case IP_RECVERR:
	case IP_ROUTER_ALERT:
	case IP_FREEBIND:
	case IP_PASSSEC:
	case IP_TRANSPARENT:
	case IP_MINTTL:
	case IP_NODEFRAG:
	case IP_BIND_ADDRESS_NO_PORT:
	case IP_UNICAST_IF:
	case IP_MULTICAST_TTL:
	case IP_MULTICAST_ALL:
	case IP_MULTICAST_LOOP:
	case IP_RECVORIGDSTADDR:
	case IP_CHECKSUM:
		if (optlen >= sizeof(int)) {
			if (get_user(val, (int __user *) optval))
				return -EFAULT;
		} else if (optlen >= sizeof(char)) {
			unsigned char ucval;

			if (get_user(ucval, (unsigned char __user *) optval))
				return -EFAULT;
			val = (int) ucval;
		}
	}

	/* If optlen==0, it is equivalent to val == 0 */

	if (ip_mroute_opt(optname))
		return ip_mroute_setsockopt(sk, optname, optval, optlen);

	err = 0;
	if (needs_rtnl)
		rtnl_lock();
	lock_sock(sk);

	switch (optname) {
	case IP_OPTIONS:
	{
		struct ip_options_rcu *old, *opt = NULL;

		if (optlen > 40)
			goto e_inval;
		err = ip_options_get_from_user(sock_net(sk), &opt,
					       optval, optlen);
		if (err)
			break;
		old = rcu_dereference_protected(inet->inet_opt,
						lockdep_sock_is_held(sk));
		if (inet->is_icsk) {
			struct inet_connection_sock *icsk = inet_csk(sk);
#if IS_ENABLED(CONFIG_IPV6)
			if (sk->sk_family == PF_INET ||
			    (!((1 << sk->sk_state) &
			       (TCPF_LISTEN | TCPF_CLOSE)) &&
			     inet->inet_daddr != LOOPBACK4_IPV6)) {
#endif
				if (old)
					icsk->icsk_ext_hdr_len -= old->opt.optlen;
				if (opt)
					icsk->icsk_ext_hdr_len += opt->opt.optlen;
				icsk->icsk_sync_mss(sk, icsk->icsk_pmtu_cookie);
#if IS_ENABLED(CONFIG_IPV6)
			}
#endif
		}
		rcu_assign_pointer(inet->inet_opt, opt);
		if (old)
			kfree_rcu(old, rcu);
		break;
	}
	case IP_PKTINFO:
		if (val)
			inet->cmsg_flags |= IP_CMSG_PKTINFO;
		else
			inet->cmsg_flags &= ~IP_CMSG_PKTINFO;
		break;
	case IP_RECVTTL:
		if (val)
			inet->cmsg_flags |=  IP_CMSG_TTL;
		else
			inet->cmsg_flags &= ~IP_CMSG_TTL;
		break;
	case IP_RECVTOS:
		if (val)
			inet->cmsg_flags |=  IP_CMSG_TOS;
		else
			inet->cmsg_flags &= ~IP_CMSG_TOS;
		break;
	case IP_RECVOPTS:
		if (val)
			inet->cmsg_flags |=  IP_CMSG_RECVOPTS;
		else
			inet->cmsg_flags &= ~IP_CMSG_RECVOPTS;
		break;
	case IP_RETOPTS:
		if (val)
			inet->cmsg_flags |= IP_CMSG_RETOPTS;
		else
			inet->cmsg_flags &= ~IP_CMSG_RETOPTS;
		break;
	case IP_PASSSEC:
		if (val)
			inet->cmsg_flags |= IP_CMSG_PASSSEC;
		else
			inet->cmsg_flags &= ~IP_CMSG_PASSSEC;
		break;
	case IP_RECVORIGDSTADDR:
		if (val)
			inet->cmsg_flags |= IP_CMSG_ORIGDSTADDR;
		else
			inet->cmsg_flags &= ~IP_CMSG_ORIGDSTADDR;
		break;
	case IP_CHECKSUM:
		if (val) {
			if (!(inet->cmsg_flags & IP_CMSG_CHECKSUM)) {
				inet_inc_convert_csum(sk);
				inet->cmsg_flags |= IP_CMSG_CHECKSUM;
			}
		} else {
			if (inet->cmsg_flags & IP_CMSG_CHECKSUM) {
				inet_dec_convert_csum(sk);
				inet->cmsg_flags &= ~IP_CMSG_CHECKSUM;
			}
		}
		break;
	case IP_TOS:	/* This sets both TOS and Precedence */
		if (sk->sk_type == SOCK_STREAM) {
			val &= ~INET_ECN_MASK;
			val |= inet->tos & INET_ECN_MASK;
		}
		if (inet->tos != val) {
			inet->tos = val;
			sk->sk_priority = rt_tos2priority(val);
			sk_dst_reset(sk);
		}
		break;
	case IP_TTL:
		if (optlen < 1)
			goto e_inval;
		if (val != -1 && (val < 1 || val > 255))
			goto e_inval;
		inet->uc_ttl = val;
		break;
	case IP_HDRINCL:
		if (sk->sk_type != SOCK_RAW) {
			err = -ENOPROTOOPT;
			break;
		}
		inet->hdrincl = val ? 1 : 0;
		break;
	case IP_NODEFRAG:
		if (sk->sk_type != SOCK_RAW) {
			err = -ENOPROTOOPT;
			break;
		}
		inet->nodefrag = val ? 1 : 0;
		break;
	case IP_BIND_ADDRESS_NO_PORT:
		inet->bind_address_no_port = val ? 1 : 0;
		break;
	case IP_MTU_DISCOVER:
		if (val < IP_PMTUDISC_DONT || val > IP_PMTUDISC_OMIT)
			goto e_inval;
		inet->pmtudisc = val;
		break;
	case IP_RECVERR:
		inet->recverr = !!val;
		if (!val)
			skb_queue_purge(&sk->sk_error_queue);
		break;
	case IP_MULTICAST_TTL:
		if (sk->sk_type == SOCK_STREAM)
			goto e_inval;
		if (optlen < 1)
			goto e_inval;
		if (val == -1)
			val = 1;
		if (val < 0 || val > 255)
			goto e_inval;
		inet->mc_ttl = val;
		break;
	case IP_MULTICAST_LOOP:
		if (optlen < 1)
			goto e_inval;
		inet->mc_loop = !!val;
		break;
	case IP_UNICAST_IF:
	{
		struct net_device *dev = NULL;
		int ifindex;

		if (optlen != sizeof(int))
			goto e_inval;

		ifindex = (__force int)ntohl((__force __be32)val);
		if (ifindex == 0) {
			inet->uc_index = 0;
			err = 0;
			break;
		}

		dev = dev_get_by_index(sock_net(sk), ifindex);
		err = -EADDRNOTAVAIL;
		if (!dev)
			break;
		dev_put(dev);

		err = -EINVAL;
		if (sk->sk_bound_dev_if)
			break;

		inet->uc_index = ifindex;
		err = 0;
		break;
	}
	case IP_MULTICAST_IF:
	{
		struct ip_mreqn mreq;
		struct net_device *dev = NULL;

		if (sk->sk_type == SOCK_STREAM)
			goto e_inval;
		/*
		 *	Check the arguments are allowable
		 */

		if (optlen < sizeof(struct in_addr))
			goto e_inval;

		err = -EFAULT;
		if (optlen >= sizeof(struct ip_mreqn)) {
			if (copy_from_user(&mreq, optval, sizeof(mreq)))
				break;
		} else {
			memset(&mreq, 0, sizeof(mreq));
			if (optlen >= sizeof(struct ip_mreq)) {
				if (copy_from_user(&mreq, optval,
						   sizeof(struct ip_mreq)))
					break;
			} else if (optlen >= sizeof(struct in_addr)) {
				if (copy_from_user(&mreq.imr_address, optval,
						   sizeof(struct in_addr)))
					break;
			}
		}

		if (!mreq.imr_ifindex) {
			if (mreq.imr_address.s_addr == htonl(INADDR_ANY)) {
				inet->mc_index = 0;
				inet->mc_addr  = 0;
				err = 0;
				break;
			}
			dev = ip_dev_find(sock_net(sk), mreq.imr_address.s_addr);
			if (dev)
				mreq.imr_ifindex = dev->ifindex;
		} else
			dev = dev_get_by_index(sock_net(sk), mreq.imr_ifindex);


		err = -EADDRNOTAVAIL;
		if (!dev)
			break;
		dev_put(dev);

		err = -EINVAL;
		if (sk->sk_bound_dev_if &&
		    mreq.imr_ifindex != sk->sk_bound_dev_if)
			break;

		inet->mc_index = mreq.imr_ifindex;
		inet->mc_addr  = mreq.imr_address.s_addr;
		err = 0;
		break;
	}

	case IP_ADD_MEMBERSHIP:
	case IP_DROP_MEMBERSHIP:
	{
		struct ip_mreqn mreq;

		err = -EPROTO;
		if (inet_sk(sk)->is_icsk)
			break;

		if (optlen < sizeof(struct ip_mreq))
			goto e_inval;
		err = -EFAULT;
		if (optlen >= sizeof(struct ip_mreqn)) {
			if (copy_from_user(&mreq, optval, sizeof(mreq)))
				break;
		} else {
			memset(&mreq, 0, sizeof(mreq));
			if (copy_from_user(&mreq, optval, sizeof(struct ip_mreq)))
				break;
		}

		if (optname == IP_ADD_MEMBERSHIP)
			err = ip_mc_join_group(sk, &mreq);
		else
			err = ip_mc_leave_group(sk, &mreq);
		break;
	}
	case IP_MSFILTER:
	{
		struct ip_msfilter *msf;

		if (optlen < IP_MSFILTER_SIZE(0))
			goto e_inval;
		if (optlen > sysctl_optmem_max) {
			err = -ENOBUFS;
			break;
		}
		msf = kmalloc(optlen, GFP_KERNEL);
		if (!msf) {
			err = -ENOBUFS;
			break;
		}
		err = -EFAULT;
		if (copy_from_user(msf, optval, optlen)) {
			kfree(msf);
			break;
		}
		/* numsrc >= (1G-4) overflow in 32 bits */
		if (msf->imsf_numsrc >= 0x3ffffffcU ||
		    msf->imsf_numsrc > net->ipv4.sysctl_igmp_max_msf) {
			kfree(msf);
			err = -ENOBUFS;
			break;
		}
		if (IP_MSFILTER_SIZE(msf->imsf_numsrc) > optlen) {
			kfree(msf);
			err = -EINVAL;
			break;
		}
		err = ip_mc_msfilter(sk, msf, 0);
		kfree(msf);
		break;
	}
	case IP_BLOCK_SOURCE:
	case IP_UNBLOCK_SOURCE:
	case IP_ADD_SOURCE_MEMBERSHIP:
	case IP_DROP_SOURCE_MEMBERSHIP:
	{
		struct ip_mreq_source mreqs;
		int omode, add;

		if (optlen != sizeof(struct ip_mreq_source))
			goto e_inval;
		if (copy_from_user(&mreqs, optval, sizeof(mreqs))) {
			err = -EFAULT;
			break;
		}
		if (optname == IP_BLOCK_SOURCE) {
			omode = MCAST_EXCLUDE;
			add = 1;
		} else if (optname == IP_UNBLOCK_SOURCE) {
			omode = MCAST_EXCLUDE;
			add = 0;
		} else if (optname == IP_ADD_SOURCE_MEMBERSHIP) {
			struct ip_mreqn mreq;

			mreq.imr_multiaddr.s_addr = mreqs.imr_multiaddr;
			mreq.imr_address.s_addr = mreqs.imr_interface;
			mreq.imr_ifindex = 0;
			err = ip_mc_join_group(sk, &mreq);
			if (err && err != -EADDRINUSE)
				break;
			omode = MCAST_INCLUDE;
			add = 1;
		} else /* IP_DROP_SOURCE_MEMBERSHIP */ {
			omode = MCAST_INCLUDE;
			add = 0;
		}
		err = ip_mc_source(add, omode, sk, &mreqs, 0);
		break;
	}
	case MCAST_JOIN_GROUP:
	case MCAST_LEAVE_GROUP:
	{
		struct group_req greq;
		struct sockaddr_in *psin;
		struct ip_mreqn mreq;

		if (optlen < sizeof(struct group_req))
			goto e_inval;
		err = -EFAULT;
		if (copy_from_user(&greq, optval, sizeof(greq)))
			break;
		psin = (struct sockaddr_in *)&greq.gr_group;
		if (psin->sin_family != AF_INET)
			goto e_inval;
		memset(&mreq, 0, sizeof(mreq));
		mreq.imr_multiaddr = psin->sin_addr;
		mreq.imr_ifindex = greq.gr_interface;

		if (optname == MCAST_JOIN_GROUP)
			err = ip_mc_join_group(sk, &mreq);
		else
			err = ip_mc_leave_group(sk, &mreq);
		break;
	}
	case MCAST_JOIN_SOURCE_GROUP:
	case MCAST_LEAVE_SOURCE_GROUP:
	case MCAST_BLOCK_SOURCE:
	case MCAST_UNBLOCK_SOURCE:
	{
		struct group_source_req greqs;
		struct ip_mreq_source mreqs;
		struct sockaddr_in *psin;
		int omode, add;

		if (optlen != sizeof(struct group_source_req))
			goto e_inval;
		if (copy_from_user(&greqs, optval, sizeof(greqs))) {
			err = -EFAULT;
			break;
		}
		if (greqs.gsr_group.ss_family != AF_INET ||
		    greqs.gsr_source.ss_family != AF_INET) {
			err = -EADDRNOTAVAIL;
			break;
		}
		psin = (struct sockaddr_in *)&greqs.gsr_group;
		mreqs.imr_multiaddr = psin->sin_addr.s_addr;
		psin = (struct sockaddr_in *)&greqs.gsr_source;
		mreqs.imr_sourceaddr = psin->sin_addr.s_addr;
		mreqs.imr_interface = 0; /* use index for mc_source */

		if (optname == MCAST_BLOCK_SOURCE) {
			omode = MCAST_EXCLUDE;
			add = 1;
		} else if (optname == MCAST_UNBLOCK_SOURCE) {
			omode = MCAST_EXCLUDE;
			add = 0;
		} else if (optname == MCAST_JOIN_SOURCE_GROUP) {
			struct ip_mreqn mreq;

			psin = (struct sockaddr_in *)&greqs.gsr_group;
			mreq.imr_multiaddr = psin->sin_addr;
			mreq.imr_address.s_addr = 0;
			mreq.imr_ifindex = greqs.gsr_interface;
			err = ip_mc_join_group(sk, &mreq);
			if (err && err != -EADDRINUSE)
				break;
			greqs.gsr_interface = mreq.imr_ifindex;
			omode = MCAST_INCLUDE;
			add = 1;
		} else /* MCAST_LEAVE_SOURCE_GROUP */ {
			omode = MCAST_INCLUDE;
			add = 0;
		}
		err = ip_mc_source(add, omode, sk, &mreqs,
				   greqs.gsr_interface);
		break;
	}
	case MCAST_MSFILTER:
	{
		struct sockaddr_in *psin;
		struct ip_msfilter *msf = NULL;
		struct group_filter *gsf = NULL;
		int msize, i, ifindex;

		if (optlen < GROUP_FILTER_SIZE(0))
			goto e_inval;
		if (optlen > sysctl_optmem_max) {
			err = -ENOBUFS;
			break;
		}
		gsf = kmalloc(optlen, GFP_KERNEL);
		if (!gsf) {
			err = -ENOBUFS;
			break;
		}
		err = -EFAULT;
		if (copy_from_user(gsf, optval, optlen))
			goto mc_msf_out;

		/* numsrc >= (4G-140)/128 overflow in 32 bits */
		if (gsf->gf_numsrc >= 0x1ffffff ||
		    gsf->gf_numsrc > net->ipv4.sysctl_igmp_max_msf) {
			err = -ENOBUFS;
			goto mc_msf_out;
		}
		if (GROUP_FILTER_SIZE(gsf->gf_numsrc) > optlen) {
			err = -EINVAL;
			goto mc_msf_out;
		}
		msize = IP_MSFILTER_SIZE(gsf->gf_numsrc);
		msf = kmalloc(msize, GFP_KERNEL);
		if (!msf) {
			err = -ENOBUFS;
			goto mc_msf_out;
		}
		ifindex = gsf->gf_interface;
		psin = (struct sockaddr_in *)&gsf->gf_group;
		if (psin->sin_family != AF_INET) {
			err = -EADDRNOTAVAIL;
			goto mc_msf_out;
		}
		msf->imsf_multiaddr = psin->sin_addr.s_addr;
		msf->imsf_interface = 0;
		msf->imsf_fmode = gsf->gf_fmode;
		msf->imsf_numsrc = gsf->gf_numsrc;
		err = -EADDRNOTAVAIL;
		for (i = 0; i < gsf->gf_numsrc; ++i) {
			psin = (struct sockaddr_in *)&gsf->gf_slist[i];

			if (psin->sin_family != AF_INET)
				goto mc_msf_out;
			msf->imsf_slist[i] = psin->sin_addr.s_addr;
		}
		kfree(gsf);
		gsf = NULL;

		err = ip_mc_msfilter(sk, msf, ifindex);
mc_msf_out:
		kfree(msf);
		kfree(gsf);
		break;
	}
	case IP_MULTICAST_ALL:
		if (optlen < 1)
			goto e_inval;
		if (val != 0 && val != 1)
			goto e_inval;
		inet->mc_all = val;
		break;
	case IP_ROUTER_ALERT:
		err = ip_ra_control(sk, val ? 1 : 0, NULL);
		break;

	case IP_FREEBIND:
		if (optlen < 1)
			goto e_inval;
		inet->freebind = !!val;
		break;

	case IP_IPSEC_POLICY:
	case IP_XFRM_POLICY:
		err = -EPERM;
		if (!ns_capable(sock_net(sk)->user_ns, CAP_NET_ADMIN))
			break;
		err = xfrm_user_policy(sk, optname, optval, optlen);
		break;

	case IP_TRANSPARENT:
		if (!!val && !ns_capable(sock_net(sk)->user_ns, CAP_NET_RAW) &&
		    !ns_capable(sock_net(sk)->user_ns, CAP_NET_ADMIN)) {
			err = -EPERM;
			break;
		}
		if (optlen < 1)
			goto e_inval;
		inet->transparent = !!val;
		break;

	case IP_MINTTL:
		if (optlen < 1)
			goto e_inval;
		if (val < 0 || val > 255)
			goto e_inval;
		inet->min_ttl = val;
		break;

	default:
		err = -ENOPROTOOPT;
		break;
	}
	release_sock(sk);
	if (needs_rtnl)
		rtnl_unlock();
	return err;

e_inval:
	release_sock(sk);
	if (needs_rtnl)
		rtnl_unlock();
	return -EINVAL;
}

/**
 * ipv4_pktinfo_prepare - transfer some info from rtable to skb
 * @sk: socket
 * @skb: buffer
 *
 * To support IP_CMSG_PKTINFO option, we store rt_iif and specific
 * destination in skb->cb[] before dst drop.
 * This way, receiver doesn't make cache line misses to read rtable.
 */
void ipv4_pktinfo_prepare(const struct sock *sk, struct sk_buff *skb)
{
	struct in_pktinfo *pktinfo = PKTINFO_SKB_CB(skb);
	bool prepare = (inet_sk(sk)->cmsg_flags & IP_CMSG_PKTINFO) ||
		       ipv6_sk_rxinfo(sk);

	if (prepare && skb_rtable(skb)) {
		/* skb->cb is overloaded: prior to this point it is IP{6}CB
		 * which has interface index (iif) as the first member of the
		 * underlying inet{6}_skb_parm struct. This code then overlays
		 * PKTINFO_SKB_CB and in_pktinfo also has iif as the first
		 * element so the iif is picked up from the prior IPCB. If iif
		 * is the loopback interface, then return the sending interface
		 * (e.g., process binds socket to eth0 for Tx which is
		 * redirected to loopback in the rtable/dst).
		 */
		if (pktinfo->ipi_ifindex == LOOPBACK_IFINDEX)
			pktinfo->ipi_ifindex = inet_iif(skb);

		pktinfo->ipi_spec_dst.s_addr = fib_compute_spec_dst(skb);
	} else {
		pktinfo->ipi_ifindex = 0;
		pktinfo->ipi_spec_dst.s_addr = 0;
	}
	/* We need to keep the dst for __ip_options_echo()
	 * We could restrict the test to opt.ts_needtime || opt.srr,
	 * but the following is good enough as IP options are not often used.
	 */
	if (unlikely(IPCB(skb)->opt.optlen))
		skb_dst_force(skb);
	else
		skb_dst_drop(skb);
}

int ip_setsockopt(struct sock *sk, int level,
		int optname, char __user *optval, unsigned int optlen)
{
	int err;

	if (level != SOL_IP)
		return -ENOPROTOOPT;

	err = do_ip_setsockopt(sk, level, optname, optval, optlen);
#ifdef CONFIG_NETFILTER
	/* we need to exclude all possible ENOPROTOOPTs except default case */
	if (err == -ENOPROTOOPT && optname != IP_HDRINCL &&
			optname != IP_IPSEC_POLICY &&
			optname != IP_XFRM_POLICY &&
			!ip_mroute_opt(optname)) {
		lock_sock(sk);
		err = nf_setsockopt(sk, PF_INET, optname, optval, optlen);
		release_sock(sk);
	}
#endif
	return err;
}
EXPORT_SYMBOL(ip_setsockopt);

#ifdef CONFIG_COMPAT
int compat_ip_setsockopt(struct sock *sk, int level, int optname,
			 char __user *optval, unsigned int optlen)
{
	int err;

	if (level != SOL_IP)
		return -ENOPROTOOPT;

	if (optname >= MCAST_JOIN_GROUP && optname <= MCAST_MSFILTER)
		return compat_mc_setsockopt(sk, level, optname, optval, optlen,
			ip_setsockopt);

	err = do_ip_setsockopt(sk, level, optname, optval, optlen);
#ifdef CONFIG_NETFILTER
	/* we need to exclude all possible ENOPROTOOPTs except default case */
	if (err == -ENOPROTOOPT && optname != IP_HDRINCL &&
			optname != IP_IPSEC_POLICY &&
			optname != IP_XFRM_POLICY &&
			!ip_mroute_opt(optname)) {
		lock_sock(sk);
		err = compat_nf_setsockopt(sk, PF_INET, optname,
					   optval, optlen);
		release_sock(sk);
	}
#endif
	return err;
}
EXPORT_SYMBOL(compat_ip_setsockopt);
#endif

/*
 *	Get the options. Note for future reference. The GET of IP options gets
 *	the _received_ ones. The set sets the _sent_ ones.
 */

static bool getsockopt_needs_rtnl(int optname)
{
	switch (optname) {
	case IP_MSFILTER:
	case MCAST_MSFILTER:
		return true;
	}
	return false;
}

static int do_ip_getsockopt(struct sock *sk, int level, int optname,
			    char __user *optval, int __user *optlen, unsigned int flags)
{
	struct inet_sock *inet = inet_sk(sk);
	bool needs_rtnl = getsockopt_needs_rtnl(optname);
	int val, err = 0;
	int len;

	if (level != SOL_IP)
		return -EOPNOTSUPP;

	if (ip_mroute_opt(optname))
		return ip_mroute_getsockopt(sk, optname, optval, optlen);

	if (get_user(len, optlen))
		return -EFAULT;
	if (len < 0)
		return -EINVAL;

	if (needs_rtnl)
		rtnl_lock();
	lock_sock(sk);

	switch (optname) {
	case IP_OPTIONS:
	{
		unsigned char optbuf[sizeof(struct ip_options)+40];
		struct ip_options *opt = (struct ip_options *)optbuf;
		struct ip_options_rcu *inet_opt;

		inet_opt = rcu_dereference_protected(inet->inet_opt,
						     lockdep_sock_is_held(sk));
		opt->optlen = 0;
		if (inet_opt)
			memcpy(optbuf, &inet_opt->opt,
			       sizeof(struct ip_options) +
			       inet_opt->opt.optlen);
		release_sock(sk);

		if (opt->optlen == 0)
			return put_user(0, optlen);

		ip_options_undo(opt);

		len = min_t(unsigned int, len, opt->optlen);
		if (put_user(len, optlen))
			return -EFAULT;
		if (copy_to_user(optval, opt->__data, len))
			return -EFAULT;
		return 0;
	}
	case IP_PKTINFO:
		val = (inet->cmsg_flags & IP_CMSG_PKTINFO) != 0;
		break;
	case IP_RECVTTL:
		val = (inet->cmsg_flags & IP_CMSG_TTL) != 0;
		break;
	case IP_RECVTOS:
		val = (inet->cmsg_flags & IP_CMSG_TOS) != 0;
		break;
	case IP_RECVOPTS:
		val = (inet->cmsg_flags & IP_CMSG_RECVOPTS) != 0;
		break;
	case IP_RETOPTS:
		val = (inet->cmsg_flags & IP_CMSG_RETOPTS) != 0;
		break;
	case IP_PASSSEC:
		val = (inet->cmsg_flags & IP_CMSG_PASSSEC) != 0;
		break;
	case IP_RECVORIGDSTADDR:
		val = (inet->cmsg_flags & IP_CMSG_ORIGDSTADDR) != 0;
		break;
	case IP_CHECKSUM:
		val = (inet->cmsg_flags & IP_CMSG_CHECKSUM) != 0;
		break;
	case IP_TOS:
		val = inet->tos;
		break;
	case IP_TTL:
	{
		struct net *net = sock_net(sk);
		val = (inet->uc_ttl == -1 ?
		       net->ipv4.sysctl_ip_default_ttl :
		       inet->uc_ttl);
		break;
	}
	case IP_HDRINCL:
		val = inet->hdrincl;
		break;
	case IP_NODEFRAG:
		val = inet->nodefrag;
		break;
	case IP_BIND_ADDRESS_NO_PORT:
		val = inet->bind_address_no_port;
		break;
	case IP_MTU_DISCOVER:
		val = inet->pmtudisc;
		break;
	case IP_MTU:
	{
		struct dst_entry *dst;
		val = 0;
		dst = sk_dst_get(sk);
		if (dst) {
			val = dst_mtu(dst);
			dst_release(dst);
		}
		if (!val) {
			release_sock(sk);
			return -ENOTCONN;
		}
		break;
	}
	case IP_RECVERR:
		val = inet->recverr;
		break;
	case IP_MULTICAST_TTL:
		val = inet->mc_ttl;
		break;
	case IP_MULTICAST_LOOP:
		val = inet->mc_loop;
		break;
	case IP_UNICAST_IF:
		val = (__force int)htonl((__u32) inet->uc_index);
		break;
	case IP_MULTICAST_IF:
	{
		struct in_addr addr;
		len = min_t(unsigned int, len, sizeof(struct in_addr));
		addr.s_addr = inet->mc_addr;
		release_sock(sk);

		if (put_user(len, optlen))
			return -EFAULT;
		if (copy_to_user(optval, &addr, len))
			return -EFAULT;
		return 0;
	}
	case IP_MSFILTER:
	{
		struct ip_msfilter msf;

		if (len < IP_MSFILTER_SIZE(0)) {
			err = -EINVAL;
			goto out;
		}
		if (copy_from_user(&msf, optval, IP_MSFILTER_SIZE(0))) {
			err = -EFAULT;
			goto out;
		}
		err = ip_mc_msfget(sk, &msf,
				   (struct ip_msfilter __user *)optval, optlen);
		goto out;
	}
	case MCAST_MSFILTER:
	{
		struct group_filter gsf;

		if (len < GROUP_FILTER_SIZE(0)) {
			err = -EINVAL;
			goto out;
		}
		if (copy_from_user(&gsf, optval, GROUP_FILTER_SIZE(0))) {
			err = -EFAULT;
			goto out;
		}
		err = ip_mc_gsfget(sk, &gsf,
				   (struct group_filter __user *)optval,
				   optlen);
		goto out;
	}
	case IP_MULTICAST_ALL:
		val = inet->mc_all;
		break;
	case IP_PKTOPTIONS:
	{
		struct msghdr msg;

		release_sock(sk);

		if (sk->sk_type != SOCK_STREAM)
			return -ENOPROTOOPT;

		msg.msg_control = (__force void *) optval;
		msg.msg_controllen = len;
		msg.msg_flags = flags;

		if (inet->cmsg_flags & IP_CMSG_PKTINFO) {
			struct in_pktinfo info;

			info.ipi_addr.s_addr = inet->inet_rcv_saddr;
			info.ipi_spec_dst.s_addr = inet->inet_rcv_saddr;
			info.ipi_ifindex = inet->mc_index;
			put_cmsg(&msg, SOL_IP, IP_PKTINFO, sizeof(info), &info);
		}
		if (inet->cmsg_flags & IP_CMSG_TTL) {
			int hlim = inet->mc_ttl;
			put_cmsg(&msg, SOL_IP, IP_TTL, sizeof(hlim), &hlim);
		}
		if (inet->cmsg_flags & IP_CMSG_TOS) {
			int tos = inet->rcv_tos;
			put_cmsg(&msg, SOL_IP, IP_TOS, sizeof(tos), &tos);
		}
		len -= msg.msg_controllen;
		return put_user(len, optlen);
	}
	case IP_FREEBIND:
		val = inet->freebind;
		break;
	case IP_TRANSPARENT:
		val = inet->transparent;
		break;
	case IP_MINTTL:
		val = inet->min_ttl;
		break;
	default:
		release_sock(sk);
		return -ENOPROTOOPT;
	}
	release_sock(sk);

	if (len < sizeof(int) && len > 0 && val >= 0 && val <= 255) {
		unsigned char ucval = (unsigned char)val;
		len = 1;
		if (put_user(len, optlen))
			return -EFAULT;
		if (copy_to_user(optval, &ucval, 1))
			return -EFAULT;
	} else {
		len = min_t(unsigned int, sizeof(int), len);
		if (put_user(len, optlen))
			return -EFAULT;
		if (copy_to_user(optval, &val, len))
			return -EFAULT;
	}
	return 0;

out:
	release_sock(sk);
	if (needs_rtnl)
		rtnl_unlock();
	return err;
}

int ip_getsockopt(struct sock *sk, int level,
		  int optname, char __user *optval, int __user *optlen)
{
	int err;

	err = do_ip_getsockopt(sk, level, optname, optval, optlen, 0);
#ifdef CONFIG_NETFILTER
	/* we need to exclude all possible ENOPROTOOPTs except default case */
	if (err == -ENOPROTOOPT && optname != IP_PKTOPTIONS &&
			!ip_mroute_opt(optname)) {
		int len;

		if (get_user(len, optlen))
			return -EFAULT;

		lock_sock(sk);
		err = nf_getsockopt(sk, PF_INET, optname, optval,
				&len);
		release_sock(sk);
		if (err >= 0)
			err = put_user(len, optlen);
		return err;
	}
#endif
	return err;
}
EXPORT_SYMBOL(ip_getsockopt);

#ifdef CONFIG_COMPAT
int compat_ip_getsockopt(struct sock *sk, int level, int optname,
			 char __user *optval, int __user *optlen)
{
	int err;

	if (optname == MCAST_MSFILTER)
		return compat_mc_getsockopt(sk, level, optname, optval, optlen,
			ip_getsockopt);

	err = do_ip_getsockopt(sk, level, optname, optval, optlen,
		MSG_CMSG_COMPAT);

#ifdef CONFIG_NETFILTER
	/* we need to exclude all possible ENOPROTOOPTs except default case */
	if (err == -ENOPROTOOPT && optname != IP_PKTOPTIONS &&
			!ip_mroute_opt(optname)) {
		int len;

		if (get_user(len, optlen))
			return -EFAULT;

		lock_sock(sk);
		err = compat_nf_getsockopt(sk, PF_INET, optname, optval, &len);
		release_sock(sk);
		if (err >= 0)
			err = put_user(len, optlen);
		return err;
	}
#endif
	return err;
}
EXPORT_SYMBOL(compat_ip_getsockopt);
#endif
