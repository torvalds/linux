/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		"Ping" sockets
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Based on ipv4/ping.c code.
 *
 * Authors:	Lorenzo Colitti (IPv6 support)
 *		Vasiliy Kulikov / Openwall (IPv4 implementation, for Linux 2.6),
 *		Pavel Kankovsky (IPv4 implementation, for Linux 2.4.32)
 *
 */

#include <net/addrconf.h>
#include <net/ipv6.h>
#include <net/ip6_route.h>
#include <net/protocol.h>
#include <net/udp.h>
#include <net/transp_v6.h>
#include <net/ping.h>

struct proto pingv6_prot = {
	.name =		"PINGv6",
	.owner =	THIS_MODULE,
	.init =		ping_init_sock,
	.close =	ping_close,
	.connect =	ip6_datagram_connect,
	.disconnect =	udp_disconnect,
	.setsockopt =	ipv6_setsockopt,
	.getsockopt =	ipv6_getsockopt,
	.sendmsg =	ping_v6_sendmsg,
	.recvmsg =	ping_recvmsg,
	.bind =		ping_bind,
	.backlog_rcv =	ping_queue_rcv_skb,
	.hash =		ping_hash,
	.unhash =	ping_unhash,
	.get_port =	ping_get_port,
	.obj_size =	sizeof(struct raw6_sock),
};
EXPORT_SYMBOL_GPL(pingv6_prot);

static struct inet_protosw pingv6_protosw = {
	.type =      SOCK_DGRAM,
	.protocol =  IPPROTO_ICMPV6,
	.prot =      &pingv6_prot,
	.ops =       &inet6_dgram_ops,
	.no_check =  UDP_CSUM_DEFAULT,
	.flags =     INET_PROTOSW_REUSE,
};


/* Compatibility glue so we can support IPv6 when it's compiled as a module */
int dummy_ipv6_recv_error(struct sock *sk, struct msghdr *msg, int len)
{
	return -EAFNOSUPPORT;
}
int dummy_ip6_datagram_recv_ctl(struct sock *sk, struct msghdr *msg,
				 struct sk_buff *skb)
{
	return -EAFNOSUPPORT;
}
int dummy_icmpv6_err_convert(u8 type, u8 code, int *err)
{
	return -EAFNOSUPPORT;
}
void dummy_ipv6_icmp_error(struct sock *sk, struct sk_buff *skb, int err,
			    __be16 port, u32 info, u8 *payload) {}
int dummy_ipv6_chk_addr(struct net *net, const struct in6_addr *addr,
			const struct net_device *dev, int strict)
{
	return 0;
}

int __init pingv6_init(void)
{
	pingv6_ops.ipv6_recv_error = ipv6_recv_error;
	pingv6_ops.ip6_datagram_recv_ctl = ip6_datagram_recv_ctl;
	pingv6_ops.icmpv6_err_convert = icmpv6_err_convert;
	pingv6_ops.ipv6_icmp_error = ipv6_icmp_error;
	pingv6_ops.ipv6_chk_addr = ipv6_chk_addr;
	return inet6_register_protosw(&pingv6_protosw);
}

/* This never gets called because it's not possible to unload the ipv6 module,
 * but just in case.
 */
void pingv6_exit(void)
{
	pingv6_ops.ipv6_recv_error = dummy_ipv6_recv_error;
	pingv6_ops.ip6_datagram_recv_ctl = dummy_ip6_datagram_recv_ctl;
	pingv6_ops.icmpv6_err_convert = dummy_icmpv6_err_convert;
	pingv6_ops.ipv6_icmp_error = dummy_ipv6_icmp_error;
	pingv6_ops.ipv6_chk_addr = dummy_ipv6_chk_addr;
	inet6_unregister_protosw(&pingv6_protosw);
}

int ping_v6_sendmsg(struct kiocb *iocb, struct sock *sk, struct msghdr *msg,
		    size_t len)
{
	struct inet_sock *inet = inet_sk(sk);
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct icmp6hdr user_icmph;
	int addr_type;
	struct in6_addr *daddr;
	int iif = 0;
	struct flowi6 fl6;
	int err;
	int hlimit;
	struct dst_entry *dst;
	struct rt6_info *rt;
	struct pingfakehdr pfh;

	pr_debug("ping_v6_sendmsg(sk=%p,sk->num=%u)\n", inet, inet->inet_num);

	err = ping_common_sendmsg(AF_INET6, msg, len, &user_icmph,
				  sizeof(user_icmph));
	if (err)
		return err;

	if (msg->msg_name) {
		struct sockaddr_in6 *u = (struct sockaddr_in6 *) msg->msg_name;
		if (msg->msg_namelen < sizeof(*u))
			return -EINVAL;
		if (u->sin6_family != AF_INET6) {
			return -EAFNOSUPPORT;
		}
		if (sk->sk_bound_dev_if &&
		    sk->sk_bound_dev_if != u->sin6_scope_id) {
			return -EINVAL;
		}
		daddr = &(u->sin6_addr);
		iif = u->sin6_scope_id;
	} else {
		if (sk->sk_state != TCP_ESTABLISHED)
			return -EDESTADDRREQ;
		daddr = &np->daddr;
	}

	if (!iif)
		iif = sk->sk_bound_dev_if;

	addr_type = ipv6_addr_type(daddr);
	if (__ipv6_addr_needs_scope_id(addr_type) && !iif)
		return -EINVAL;
	if (addr_type & IPV6_ADDR_MAPPED)
		return -EINVAL;

	/* TODO: use ip6_datagram_send_ctl to get options from cmsg */

	memset(&fl6, 0, sizeof(fl6));

	fl6.flowi6_proto = IPPROTO_ICMPV6;
	fl6.saddr = np->saddr;
	fl6.daddr = *daddr;
	fl6.flowi6_mark = sk->sk_mark;
	fl6.flowi6_uid = sock_i_uid(sk);
	fl6.fl6_icmp_type = user_icmph.icmp6_type;
	fl6.fl6_icmp_code = user_icmph.icmp6_code;
	security_sk_classify_flow(sk, flowi6_to_flowi(&fl6));

	if (!fl6.flowi6_oif && ipv6_addr_is_multicast(&fl6.daddr))
		fl6.flowi6_oif = np->mcast_oif;
	else if (!fl6.flowi6_oif)
		fl6.flowi6_oif = np->ucast_oif;

	dst = ip6_sk_dst_lookup_flow(sk, &fl6,  daddr, 1);
	if (IS_ERR(dst))
		return PTR_ERR(dst);
	rt = (struct rt6_info *) dst;

	np = inet6_sk(sk);
	if (!np)
		return -EBADF;

	if (!fl6.flowi6_oif && ipv6_addr_is_multicast(&fl6.daddr))
		fl6.flowi6_oif = np->mcast_oif;
	else if (!fl6.flowi6_oif)
		fl6.flowi6_oif = np->ucast_oif;

	pfh.icmph.type = user_icmph.icmp6_type;
	pfh.icmph.code = user_icmph.icmp6_code;
	pfh.icmph.checksum = 0;
	pfh.icmph.un.echo.id = inet->inet_sport;
	pfh.icmph.un.echo.sequence = user_icmph.icmp6_sequence;
	pfh.iov = msg->msg_iov;
	pfh.wcheck = 0;
	pfh.family = AF_INET6;

	if (ipv6_addr_is_multicast(&fl6.daddr))
		hlimit = np->mcast_hops;
	else
		hlimit = np->hop_limit;
	if (hlimit < 0)
		hlimit = ip6_dst_hoplimit(dst);

	lock_sock(sk);
	err = ip6_append_data(sk, ping_getfrag, &pfh, len,
			      0, hlimit,
			      np->tclass, NULL, &fl6, rt,
			      MSG_DONTWAIT, np->dontfrag);

	if (err) {
		ICMP6_INC_STATS_BH(sock_net(sk), rt->rt6i_idev,
				   ICMP6_MIB_OUTERRORS);
		ip6_flush_pending_frames(sk);
	} else {
		err = icmpv6_push_pending_frames(sk, &fl6,
						 (struct icmp6hdr *) &pfh.icmph,
						 len);
	}
	release_sock(sk);

	if (err)
		return err;

	return len;
}
