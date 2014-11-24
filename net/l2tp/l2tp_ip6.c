/*
 * L2TPv3 IP encapsulation support for IPv6
 *
 * Copyright (c) 2012 Katalix Systems Ltd
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/icmp.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/random.h>
#include <linux/socket.h>
#include <linux/l2tp.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <net/sock.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <net/udp.h>
#include <net/inet_common.h>
#include <net/inet_hashtables.h>
#include <net/tcp_states.h>
#include <net/protocol.h>
#include <net/xfrm.h>

#include <net/transp_v6.h>
#include <net/addrconf.h>
#include <net/ip6_route.h>

#include "l2tp_core.h"

struct l2tp_ip6_sock {
	/* inet_sock has to be the first member of l2tp_ip6_sock */
	struct inet_sock	inet;

	u32			conn_id;
	u32			peer_conn_id;

	/* ipv6_pinfo has to be the last member of l2tp_ip6_sock, see
	   inet6_sk_generic */
	struct ipv6_pinfo	inet6;
};

static DEFINE_RWLOCK(l2tp_ip6_lock);
static struct hlist_head l2tp_ip6_table;
static struct hlist_head l2tp_ip6_bind_table;

static inline struct l2tp_ip6_sock *l2tp_ip6_sk(const struct sock *sk)
{
	return (struct l2tp_ip6_sock *)sk;
}

static struct sock *__l2tp_ip6_bind_lookup(struct net *net,
					   struct in6_addr *laddr,
					   int dif, u32 tunnel_id)
{
	struct sock *sk;

	sk_for_each_bound(sk, &l2tp_ip6_bind_table) {
		const struct in6_addr *addr = inet6_rcv_saddr(sk);
		struct l2tp_ip6_sock *l2tp = l2tp_ip6_sk(sk);

		if (l2tp == NULL)
			continue;

		if ((l2tp->conn_id == tunnel_id) &&
		    net_eq(sock_net(sk), net) &&
		    !(addr && ipv6_addr_equal(addr, laddr)) &&
		    !(sk->sk_bound_dev_if && sk->sk_bound_dev_if != dif))
			goto found;
	}

	sk = NULL;
found:
	return sk;
}

static inline struct sock *l2tp_ip6_bind_lookup(struct net *net,
						struct in6_addr *laddr,
						int dif, u32 tunnel_id)
{
	struct sock *sk = __l2tp_ip6_bind_lookup(net, laddr, dif, tunnel_id);
	if (sk)
		sock_hold(sk);

	return sk;
}

/* When processing receive frames, there are two cases to
 * consider. Data frames consist of a non-zero session-id and an
 * optional cookie. Control frames consist of a regular L2TP header
 * preceded by 32-bits of zeros.
 *
 * L2TPv3 Session Header Over IP
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                           Session ID                          |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |               Cookie (optional, maximum 64 bits)...
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *                                                                 |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * L2TPv3 Control Message Header Over IP
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                      (32 bits of zeros)                       |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |T|L|x|x|S|x|x|x|x|x|x|x|  Ver  |             Length            |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                     Control Connection ID                     |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |               Ns              |               Nr              |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * All control frames are passed to userspace.
 */
static int l2tp_ip6_recv(struct sk_buff *skb)
{
	struct sock *sk;
	u32 session_id;
	u32 tunnel_id;
	unsigned char *ptr, *optr;
	struct l2tp_session *session;
	struct l2tp_tunnel *tunnel = NULL;
	int length;

	/* Point to L2TP header */
	optr = ptr = skb->data;

	if (!pskb_may_pull(skb, 4))
		goto discard;

	session_id = ntohl(*((__be32 *) ptr));
	ptr += 4;

	/* RFC3931: L2TP/IP packets have the first 4 bytes containing
	 * the session_id. If it is 0, the packet is a L2TP control
	 * frame and the session_id value can be discarded.
	 */
	if (session_id == 0) {
		__skb_pull(skb, 4);
		goto pass_up;
	}

	/* Ok, this is a data packet. Lookup the session. */
	session = l2tp_session_find(&init_net, NULL, session_id);
	if (session == NULL)
		goto discard;

	tunnel = session->tunnel;
	if (tunnel == NULL)
		goto discard;

	/* Trace packet contents, if enabled */
	if (tunnel->debug & L2TP_MSG_DATA) {
		length = min(32u, skb->len);
		if (!pskb_may_pull(skb, length))
			goto discard;

		pr_debug("%s: ip recv\n", tunnel->name);
		print_hex_dump_bytes("", DUMP_PREFIX_OFFSET, ptr, length);
	}

	l2tp_recv_common(session, skb, ptr, optr, 0, skb->len,
			 tunnel->recv_payload_hook);
	return 0;

pass_up:
	/* Get the tunnel_id from the L2TP header */
	if (!pskb_may_pull(skb, 12))
		goto discard;

	if ((skb->data[0] & 0xc0) != 0xc0)
		goto discard;

	tunnel_id = ntohl(*(__be32 *) &skb->data[4]);
	tunnel = l2tp_tunnel_find(&init_net, tunnel_id);
	if (tunnel != NULL)
		sk = tunnel->sock;
	else {
		struct ipv6hdr *iph = ipv6_hdr(skb);

		read_lock_bh(&l2tp_ip6_lock);
		sk = __l2tp_ip6_bind_lookup(&init_net, &iph->daddr,
					    0, tunnel_id);
		read_unlock_bh(&l2tp_ip6_lock);
	}

	if (sk == NULL)
		goto discard;

	sock_hold(sk);

	if (!xfrm6_policy_check(sk, XFRM_POLICY_IN, skb))
		goto discard_put;

	nf_reset(skb);

	return sk_receive_skb(sk, skb, 1);

discard_put:
	sock_put(sk);

discard:
	kfree_skb(skb);
	return 0;
}

static int l2tp_ip6_open(struct sock *sk)
{
	/* Prevent autobind. We don't have ports. */
	inet_sk(sk)->inet_num = IPPROTO_L2TP;

	write_lock_bh(&l2tp_ip6_lock);
	sk_add_node(sk, &l2tp_ip6_table);
	write_unlock_bh(&l2tp_ip6_lock);

	return 0;
}

static void l2tp_ip6_close(struct sock *sk, long timeout)
{
	write_lock_bh(&l2tp_ip6_lock);
	hlist_del_init(&sk->sk_bind_node);
	sk_del_node_init(sk);
	write_unlock_bh(&l2tp_ip6_lock);

	sk_common_release(sk);
}

static void l2tp_ip6_destroy_sock(struct sock *sk)
{
	struct l2tp_tunnel *tunnel = l2tp_sock_to_tunnel(sk);

	lock_sock(sk);
	ip6_flush_pending_frames(sk);
	release_sock(sk);

	if (tunnel) {
		l2tp_tunnel_closeall(tunnel);
		sock_put(sk);
	}

	inet6_destroy_sock(sk);
}

static int l2tp_ip6_bind(struct sock *sk, struct sockaddr *uaddr, int addr_len)
{
	struct inet_sock *inet = inet_sk(sk);
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct sockaddr_l2tpip6 *addr = (struct sockaddr_l2tpip6 *) uaddr;
	__be32 v4addr = 0;
	int addr_type;
	int err;

	if (!sock_flag(sk, SOCK_ZAPPED))
		return -EINVAL;
	if (addr->l2tp_family != AF_INET6)
		return -EINVAL;
	if (addr_len < sizeof(*addr))
		return -EINVAL;

	addr_type = ipv6_addr_type(&addr->l2tp_addr);

	/* l2tp_ip6 sockets are IPv6 only */
	if (addr_type == IPV6_ADDR_MAPPED)
		return -EADDRNOTAVAIL;

	/* L2TP is point-point, not multicast */
	if (addr_type & IPV6_ADDR_MULTICAST)
		return -EADDRNOTAVAIL;

	err = -EADDRINUSE;
	read_lock_bh(&l2tp_ip6_lock);
	if (__l2tp_ip6_bind_lookup(&init_net, &addr->l2tp_addr,
				   sk->sk_bound_dev_if, addr->l2tp_conn_id))
		goto out_in_use;
	read_unlock_bh(&l2tp_ip6_lock);

	lock_sock(sk);

	err = -EINVAL;
	if (sk->sk_state != TCP_CLOSE)
		goto out_unlock;

	/* Check if the address belongs to the host. */
	rcu_read_lock();
	if (addr_type != IPV6_ADDR_ANY) {
		struct net_device *dev = NULL;

		if (addr_type & IPV6_ADDR_LINKLOCAL) {
			if (addr_len >= sizeof(struct sockaddr_in6) &&
			    addr->l2tp_scope_id) {
				/* Override any existing binding, if another
				 * one is supplied by user.
				 */
				sk->sk_bound_dev_if = addr->l2tp_scope_id;
			}

			/* Binding to link-local address requires an
			   interface */
			if (!sk->sk_bound_dev_if)
				goto out_unlock_rcu;

			err = -ENODEV;
			dev = dev_get_by_index_rcu(sock_net(sk),
						   sk->sk_bound_dev_if);
			if (!dev)
				goto out_unlock_rcu;
		}

		/* ipv4 addr of the socket is invalid.  Only the
		 * unspecified and mapped address have a v4 equivalent.
		 */
		v4addr = LOOPBACK4_IPV6;
		err = -EADDRNOTAVAIL;
		if (!ipv6_chk_addr(sock_net(sk), &addr->l2tp_addr, dev, 0))
			goto out_unlock_rcu;
	}
	rcu_read_unlock();

	inet->inet_rcv_saddr = inet->inet_saddr = v4addr;
	sk->sk_v6_rcv_saddr = addr->l2tp_addr;
	np->saddr = addr->l2tp_addr;

	l2tp_ip6_sk(sk)->conn_id = addr->l2tp_conn_id;

	write_lock_bh(&l2tp_ip6_lock);
	sk_add_bind_node(sk, &l2tp_ip6_bind_table);
	sk_del_node_init(sk);
	write_unlock_bh(&l2tp_ip6_lock);

	sock_reset_flag(sk, SOCK_ZAPPED);
	release_sock(sk);
	return 0;

out_unlock_rcu:
	rcu_read_unlock();
out_unlock:
	release_sock(sk);
	return err;

out_in_use:
	read_unlock_bh(&l2tp_ip6_lock);
	return err;
}

static int l2tp_ip6_connect(struct sock *sk, struct sockaddr *uaddr,
			    int addr_len)
{
	struct sockaddr_l2tpip6 *lsa = (struct sockaddr_l2tpip6 *) uaddr;
	struct sockaddr_in6	*usin = (struct sockaddr_in6 *) uaddr;
	struct in6_addr	*daddr;
	int	addr_type;
	int rc;

	if (sock_flag(sk, SOCK_ZAPPED)) /* Must bind first - autobinding does not work */
		return -EINVAL;

	if (addr_len < sizeof(*lsa))
		return -EINVAL;

	if (usin->sin6_family != AF_INET6)
		return -EINVAL;

	addr_type = ipv6_addr_type(&usin->sin6_addr);
	if (addr_type & IPV6_ADDR_MULTICAST)
		return -EINVAL;

	if (addr_type & IPV6_ADDR_MAPPED) {
		daddr = &usin->sin6_addr;
		if (ipv4_is_multicast(daddr->s6_addr32[3]))
			return -EINVAL;
	}

	rc = ip6_datagram_connect(sk, uaddr, addr_len);

	lock_sock(sk);

	l2tp_ip6_sk(sk)->peer_conn_id = lsa->l2tp_conn_id;

	write_lock_bh(&l2tp_ip6_lock);
	hlist_del_init(&sk->sk_bind_node);
	sk_add_bind_node(sk, &l2tp_ip6_bind_table);
	write_unlock_bh(&l2tp_ip6_lock);

	release_sock(sk);

	return rc;
}

static int l2tp_ip6_disconnect(struct sock *sk, int flags)
{
	if (sock_flag(sk, SOCK_ZAPPED))
		return 0;

	return udp_disconnect(sk, flags);
}

static int l2tp_ip6_getname(struct socket *sock, struct sockaddr *uaddr,
			    int *uaddr_len, int peer)
{
	struct sockaddr_l2tpip6 *lsa = (struct sockaddr_l2tpip6 *)uaddr;
	struct sock *sk = sock->sk;
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct l2tp_ip6_sock *lsk = l2tp_ip6_sk(sk);

	lsa->l2tp_family = AF_INET6;
	lsa->l2tp_flowinfo = 0;
	lsa->l2tp_scope_id = 0;
	lsa->l2tp_unused = 0;
	if (peer) {
		if (!lsk->peer_conn_id)
			return -ENOTCONN;
		lsa->l2tp_conn_id = lsk->peer_conn_id;
		lsa->l2tp_addr = sk->sk_v6_daddr;
		if (np->sndflow)
			lsa->l2tp_flowinfo = np->flow_label;
	} else {
		if (ipv6_addr_any(&sk->sk_v6_rcv_saddr))
			lsa->l2tp_addr = np->saddr;
		else
			lsa->l2tp_addr = sk->sk_v6_rcv_saddr;

		lsa->l2tp_conn_id = lsk->conn_id;
	}
	if (ipv6_addr_type(&lsa->l2tp_addr) & IPV6_ADDR_LINKLOCAL)
		lsa->l2tp_scope_id = sk->sk_bound_dev_if;
	*uaddr_len = sizeof(*lsa);
	return 0;
}

static int l2tp_ip6_backlog_recv(struct sock *sk, struct sk_buff *skb)
{
	int rc;

	/* Charge it to the socket, dropping if the queue is full. */
	rc = sock_queue_rcv_skb(sk, skb);
	if (rc < 0)
		goto drop;

	return 0;

drop:
	IP_INC_STATS(&init_net, IPSTATS_MIB_INDISCARDS);
	kfree_skb(skb);
	return -1;
}

static int l2tp_ip6_push_pending_frames(struct sock *sk)
{
	struct sk_buff *skb;
	__be32 *transhdr = NULL;
	int err = 0;

	skb = skb_peek(&sk->sk_write_queue);
	if (skb == NULL)
		goto out;

	transhdr = (__be32 *)skb_transport_header(skb);
	*transhdr = 0;

	err = ip6_push_pending_frames(sk);

out:
	return err;
}

/* Userspace will call sendmsg() on the tunnel socket to send L2TP
 * control frames.
 */
static int l2tp_ip6_sendmsg(struct kiocb *iocb, struct sock *sk,
			    struct msghdr *msg, size_t len)
{
	struct ipv6_txoptions opt_space;
	DECLARE_SOCKADDR(struct sockaddr_l2tpip6 *, lsa, msg->msg_name);
	struct in6_addr *daddr, *final_p, final;
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct ipv6_txoptions *opt = NULL;
	struct ip6_flowlabel *flowlabel = NULL;
	struct dst_entry *dst = NULL;
	struct flowi6 fl6;
	int addr_len = msg->msg_namelen;
	int hlimit = -1;
	int tclass = -1;
	int dontfrag = -1;
	int transhdrlen = 4; /* zero session-id */
	int ulen = len + transhdrlen;
	int err;

	/* Rough check on arithmetic overflow,
	   better check is made in ip6_append_data().
	 */
	if (len > INT_MAX)
		return -EMSGSIZE;

	/* Mirror BSD error message compatibility */
	if (msg->msg_flags & MSG_OOB)
		return -EOPNOTSUPP;

	/*
	 *	Get and verify the address.
	 */
	memset(&fl6, 0, sizeof(fl6));

	fl6.flowi6_mark = sk->sk_mark;

	if (lsa) {
		if (addr_len < SIN6_LEN_RFC2133)
			return -EINVAL;

		if (lsa->l2tp_family && lsa->l2tp_family != AF_INET6)
			return -EAFNOSUPPORT;

		daddr = &lsa->l2tp_addr;
		if (np->sndflow) {
			fl6.flowlabel = lsa->l2tp_flowinfo & IPV6_FLOWINFO_MASK;
			if (fl6.flowlabel&IPV6_FLOWLABEL_MASK) {
				flowlabel = fl6_sock_lookup(sk, fl6.flowlabel);
				if (flowlabel == NULL)
					return -EINVAL;
			}
		}

		/*
		 * Otherwise it will be difficult to maintain
		 * sk->sk_dst_cache.
		 */
		if (sk->sk_state == TCP_ESTABLISHED &&
		    ipv6_addr_equal(daddr, &sk->sk_v6_daddr))
			daddr = &sk->sk_v6_daddr;

		if (addr_len >= sizeof(struct sockaddr_in6) &&
		    lsa->l2tp_scope_id &&
		    ipv6_addr_type(daddr) & IPV6_ADDR_LINKLOCAL)
			fl6.flowi6_oif = lsa->l2tp_scope_id;
	} else {
		if (sk->sk_state != TCP_ESTABLISHED)
			return -EDESTADDRREQ;

		daddr = &sk->sk_v6_daddr;
		fl6.flowlabel = np->flow_label;
	}

	if (fl6.flowi6_oif == 0)
		fl6.flowi6_oif = sk->sk_bound_dev_if;

	if (msg->msg_controllen) {
		opt = &opt_space;
		memset(opt, 0, sizeof(struct ipv6_txoptions));
		opt->tot_len = sizeof(struct ipv6_txoptions);

		err = ip6_datagram_send_ctl(sock_net(sk), sk, msg, &fl6, opt,
					    &hlimit, &tclass, &dontfrag);
		if (err < 0) {
			fl6_sock_release(flowlabel);
			return err;
		}
		if ((fl6.flowlabel & IPV6_FLOWLABEL_MASK) && !flowlabel) {
			flowlabel = fl6_sock_lookup(sk, fl6.flowlabel);
			if (flowlabel == NULL)
				return -EINVAL;
		}
		if (!(opt->opt_nflen|opt->opt_flen))
			opt = NULL;
	}

	if (opt == NULL)
		opt = np->opt;
	if (flowlabel)
		opt = fl6_merge_options(&opt_space, flowlabel, opt);
	opt = ipv6_fixup_options(&opt_space, opt);

	fl6.flowi6_proto = sk->sk_protocol;
	if (!ipv6_addr_any(daddr))
		fl6.daddr = *daddr;
	else
		fl6.daddr.s6_addr[15] = 0x1; /* :: means loopback (BSD'ism) */
	if (ipv6_addr_any(&fl6.saddr) && !ipv6_addr_any(&np->saddr))
		fl6.saddr = np->saddr;

	final_p = fl6_update_dst(&fl6, opt, &final);

	if (!fl6.flowi6_oif && ipv6_addr_is_multicast(&fl6.daddr))
		fl6.flowi6_oif = np->mcast_oif;
	else if (!fl6.flowi6_oif)
		fl6.flowi6_oif = np->ucast_oif;

	security_sk_classify_flow(sk, flowi6_to_flowi(&fl6));

	dst = ip6_dst_lookup_flow(sk, &fl6, final_p);
	if (IS_ERR(dst)) {
		err = PTR_ERR(dst);
		goto out;
	}

	if (hlimit < 0)
		hlimit = ip6_sk_dst_hoplimit(np, &fl6, dst);

	if (tclass < 0)
		tclass = np->tclass;

	if (dontfrag < 0)
		dontfrag = np->dontfrag;

	if (msg->msg_flags & MSG_CONFIRM)
		goto do_confirm;

back_from_confirm:
	lock_sock(sk);
	err = ip6_append_data(sk, ip_generic_getfrag, msg,
			      ulen, transhdrlen, hlimit, tclass, opt,
			      &fl6, (struct rt6_info *)dst,
			      msg->msg_flags, dontfrag);
	if (err)
		ip6_flush_pending_frames(sk);
	else if (!(msg->msg_flags & MSG_MORE))
		err = l2tp_ip6_push_pending_frames(sk);
	release_sock(sk);
done:
	dst_release(dst);
out:
	fl6_sock_release(flowlabel);

	return err < 0 ? err : len;

do_confirm:
	dst_confirm(dst);
	if (!(msg->msg_flags & MSG_PROBE) || len)
		goto back_from_confirm;
	err = 0;
	goto done;
}

static int l2tp_ip6_recvmsg(struct kiocb *iocb, struct sock *sk,
			    struct msghdr *msg, size_t len, int noblock,
			    int flags, int *addr_len)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	DECLARE_SOCKADDR(struct sockaddr_l2tpip6 *, lsa, msg->msg_name);
	size_t copied = 0;
	int err = -EOPNOTSUPP;
	struct sk_buff *skb;

	if (flags & MSG_OOB)
		goto out;

	if (addr_len)
		*addr_len = sizeof(*lsa);

	if (flags & MSG_ERRQUEUE)
		return ipv6_recv_error(sk, msg, len, addr_len);

	skb = skb_recv_datagram(sk, flags, noblock, &err);
	if (!skb)
		goto out;

	copied = skb->len;
	if (len < copied) {
		msg->msg_flags |= MSG_TRUNC;
		copied = len;
	}

	err = skb_copy_datagram_msg(skb, 0, msg, copied);
	if (err)
		goto done;

	sock_recv_timestamp(msg, sk, skb);

	/* Copy the address. */
	if (lsa) {
		lsa->l2tp_family = AF_INET6;
		lsa->l2tp_unused = 0;
		lsa->l2tp_addr = ipv6_hdr(skb)->saddr;
		lsa->l2tp_flowinfo = 0;
		lsa->l2tp_scope_id = 0;
		lsa->l2tp_conn_id = 0;
		if (ipv6_addr_type(&lsa->l2tp_addr) & IPV6_ADDR_LINKLOCAL)
			lsa->l2tp_scope_id = inet6_iif(skb);
	}

	if (np->rxopt.all)
		ip6_datagram_recv_ctl(sk, msg, skb);

	if (flags & MSG_TRUNC)
		copied = skb->len;
done:
	skb_free_datagram(sk, skb);
out:
	return err ? err : copied;
}

static struct proto l2tp_ip6_prot = {
	.name		   = "L2TP/IPv6",
	.owner		   = THIS_MODULE,
	.init		   = l2tp_ip6_open,
	.close		   = l2tp_ip6_close,
	.bind		   = l2tp_ip6_bind,
	.connect	   = l2tp_ip6_connect,
	.disconnect	   = l2tp_ip6_disconnect,
	.ioctl		   = udp_ioctl,
	.destroy	   = l2tp_ip6_destroy_sock,
	.setsockopt	   = ipv6_setsockopt,
	.getsockopt	   = ipv6_getsockopt,
	.sendmsg	   = l2tp_ip6_sendmsg,
	.recvmsg	   = l2tp_ip6_recvmsg,
	.backlog_rcv	   = l2tp_ip6_backlog_recv,
	.hash		   = inet_hash,
	.unhash		   = inet_unhash,
	.obj_size	   = sizeof(struct l2tp_ip6_sock),
#ifdef CONFIG_COMPAT
	.compat_setsockopt = compat_ipv6_setsockopt,
	.compat_getsockopt = compat_ipv6_getsockopt,
#endif
};

static const struct proto_ops l2tp_ip6_ops = {
	.family		   = PF_INET6,
	.owner		   = THIS_MODULE,
	.release	   = inet6_release,
	.bind		   = inet6_bind,
	.connect	   = inet_dgram_connect,
	.socketpair	   = sock_no_socketpair,
	.accept		   = sock_no_accept,
	.getname	   = l2tp_ip6_getname,
	.poll		   = datagram_poll,
	.ioctl		   = inet6_ioctl,
	.listen		   = sock_no_listen,
	.shutdown	   = inet_shutdown,
	.setsockopt	   = sock_common_setsockopt,
	.getsockopt	   = sock_common_getsockopt,
	.sendmsg	   = inet_sendmsg,
	.recvmsg	   = sock_common_recvmsg,
	.mmap		   = sock_no_mmap,
	.sendpage	   = sock_no_sendpage,
#ifdef CONFIG_COMPAT
	.compat_setsockopt = compat_sock_common_setsockopt,
	.compat_getsockopt = compat_sock_common_getsockopt,
#endif
};

static struct inet_protosw l2tp_ip6_protosw = {
	.type		= SOCK_DGRAM,
	.protocol	= IPPROTO_L2TP,
	.prot		= &l2tp_ip6_prot,
	.ops		= &l2tp_ip6_ops,
};

static struct inet6_protocol l2tp_ip6_protocol __read_mostly = {
	.handler	= l2tp_ip6_recv,
};

static int __init l2tp_ip6_init(void)
{
	int err;

	pr_info("L2TP IP encapsulation support for IPv6 (L2TPv3)\n");

	err = proto_register(&l2tp_ip6_prot, 1);
	if (err != 0)
		goto out;

	err = inet6_add_protocol(&l2tp_ip6_protocol, IPPROTO_L2TP);
	if (err)
		goto out1;

	inet6_register_protosw(&l2tp_ip6_protosw);
	return 0;

out1:
	proto_unregister(&l2tp_ip6_prot);
out:
	return err;
}

static void __exit l2tp_ip6_exit(void)
{
	inet6_unregister_protosw(&l2tp_ip6_protosw);
	inet6_del_protocol(&l2tp_ip6_protocol, IPPROTO_L2TP);
	proto_unregister(&l2tp_ip6_prot);
}

module_init(l2tp_ip6_init);
module_exit(l2tp_ip6_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Chris Elston <celston@katalix.com>");
MODULE_DESCRIPTION("L2TP IP encapsulation for IPv6");
MODULE_VERSION("1.0");

/* Use the value of SOCK_DGRAM (2) directory, because __stringify doesn't like
 * enums
 */
MODULE_ALIAS_NET_PF_PROTO_TYPE(PF_INET6, 2, IPPROTO_L2TP);
