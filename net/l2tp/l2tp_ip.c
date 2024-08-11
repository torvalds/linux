// SPDX-License-Identifier: GPL-2.0-or-later
/* L2TPv3 IP encapsulation support
 *
 * Copyright (c) 2008,2009,2010 Katalix Systems Ltd
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <asm/ioctls.h>
#include <linux/icmp.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/random.h>
#include <linux/socket.h>
#include <linux/l2tp.h>
#include <linux/in.h>
#include <net/sock.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <net/udp.h>
#include <net/inet_common.h>
#include <net/tcp_states.h>
#include <net/protocol.h>
#include <net/xfrm.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>

#include "l2tp_core.h"

/* per-net private data for this module */
static unsigned int l2tp_ip_net_id;
struct l2tp_ip_net {
	rwlock_t l2tp_ip_lock;
	struct hlist_head l2tp_ip_table;
	struct hlist_head l2tp_ip_bind_table;
};

struct l2tp_ip_sock {
	/* inet_sock has to be the first member of l2tp_ip_sock */
	struct inet_sock	inet;

	u32			conn_id;
	u32			peer_conn_id;
};

static struct l2tp_ip_sock *l2tp_ip_sk(const struct sock *sk)
{
	return (struct l2tp_ip_sock *)sk;
}

static struct l2tp_ip_net *l2tp_ip_pernet(const struct net *net)
{
	return net_generic(net, l2tp_ip_net_id);
}

static struct sock *__l2tp_ip_bind_lookup(const struct net *net, __be32 laddr,
					  __be32 raddr, int dif, u32 tunnel_id)
{
	struct l2tp_ip_net *pn = l2tp_ip_pernet(net);
	struct sock *sk;

	sk_for_each_bound(sk, &pn->l2tp_ip_bind_table) {
		const struct l2tp_ip_sock *l2tp = l2tp_ip_sk(sk);
		const struct inet_sock *inet = inet_sk(sk);
		int bound_dev_if;

		if (!net_eq(sock_net(sk), net))
			continue;

		bound_dev_if = READ_ONCE(sk->sk_bound_dev_if);
		if (bound_dev_if && dif && bound_dev_if != dif)
			continue;

		if (inet->inet_rcv_saddr && laddr &&
		    inet->inet_rcv_saddr != laddr)
			continue;

		if (inet->inet_daddr && raddr && inet->inet_daddr != raddr)
			continue;

		if (l2tp->conn_id != tunnel_id)
			continue;

		goto found;
	}

	sk = NULL;
found:
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
static int l2tp_ip_recv(struct sk_buff *skb)
{
	struct net *net = dev_net(skb->dev);
	struct l2tp_ip_net *pn;
	struct sock *sk;
	u32 session_id;
	u32 tunnel_id;
	unsigned char *ptr, *optr;
	struct l2tp_session *session;
	struct l2tp_tunnel *tunnel = NULL;
	struct iphdr *iph;

	pn = l2tp_ip_pernet(net);

	if (!pskb_may_pull(skb, 4))
		goto discard;

	/* Point to L2TP header */
	optr = skb->data;
	ptr = skb->data;
	session_id = ntohl(*((__be32 *)ptr));
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
	session = l2tp_v3_session_get(net, NULL, session_id);
	if (!session)
		goto discard;

	tunnel = session->tunnel;
	if (!tunnel)
		goto discard_sess;

	if (l2tp_v3_ensure_opt_in_linear(session, skb, &ptr, &optr))
		goto discard_sess;

	l2tp_recv_common(session, skb, ptr, optr, 0, skb->len);
	l2tp_session_put(session);

	return 0;

pass_up:
	/* Get the tunnel_id from the L2TP header */
	if (!pskb_may_pull(skb, 12))
		goto discard;

	if ((skb->data[0] & 0xc0) != 0xc0)
		goto discard;

	tunnel_id = ntohl(*(__be32 *)&skb->data[4]);
	iph = (struct iphdr *)skb_network_header(skb);

	read_lock_bh(&pn->l2tp_ip_lock);
	sk = __l2tp_ip_bind_lookup(net, iph->daddr, iph->saddr, inet_iif(skb),
				   tunnel_id);
	if (!sk) {
		read_unlock_bh(&pn->l2tp_ip_lock);
		goto discard;
	}
	sock_hold(sk);
	read_unlock_bh(&pn->l2tp_ip_lock);

	if (!xfrm4_policy_check(sk, XFRM_POLICY_IN, skb))
		goto discard_put;

	nf_reset_ct(skb);

	return sk_receive_skb(sk, skb, 1);

discard_sess:
	l2tp_session_put(session);
	goto discard;

discard_put:
	sock_put(sk);

discard:
	kfree_skb(skb);
	return 0;
}

static int l2tp_ip_hash(struct sock *sk)
{
	struct l2tp_ip_net *pn = l2tp_ip_pernet(sock_net(sk));

	if (sk_unhashed(sk)) {
		write_lock_bh(&pn->l2tp_ip_lock);
		sk_add_node(sk, &pn->l2tp_ip_table);
		write_unlock_bh(&pn->l2tp_ip_lock);
	}
	return 0;
}

static void l2tp_ip_unhash(struct sock *sk)
{
	struct l2tp_ip_net *pn = l2tp_ip_pernet(sock_net(sk));

	if (sk_unhashed(sk))
		return;
	write_lock_bh(&pn->l2tp_ip_lock);
	sk_del_node_init(sk);
	write_unlock_bh(&pn->l2tp_ip_lock);
}

static int l2tp_ip_open(struct sock *sk)
{
	/* Prevent autobind. We don't have ports. */
	inet_sk(sk)->inet_num = IPPROTO_L2TP;

	l2tp_ip_hash(sk);
	return 0;
}

static void l2tp_ip_close(struct sock *sk, long timeout)
{
	struct l2tp_ip_net *pn = l2tp_ip_pernet(sock_net(sk));

	write_lock_bh(&pn->l2tp_ip_lock);
	hlist_del_init(&sk->sk_bind_node);
	sk_del_node_init(sk);
	write_unlock_bh(&pn->l2tp_ip_lock);
	sk_common_release(sk);
}

static void l2tp_ip_destroy_sock(struct sock *sk)
{
	struct l2tp_tunnel *tunnel;

	lock_sock(sk);
	ip_flush_pending_frames(sk);
	release_sock(sk);

	tunnel = l2tp_sk_to_tunnel(sk);
	if (tunnel) {
		l2tp_tunnel_delete(tunnel);
		l2tp_tunnel_put(tunnel);
	}
}

static int l2tp_ip_bind(struct sock *sk, struct sockaddr *uaddr, int addr_len)
{
	struct inet_sock *inet = inet_sk(sk);
	struct sockaddr_l2tpip *addr = (struct sockaddr_l2tpip *)uaddr;
	struct net *net = sock_net(sk);
	struct l2tp_ip_net *pn;
	int ret;
	int chk_addr_ret;

	if (addr_len < sizeof(struct sockaddr_l2tpip))
		return -EINVAL;
	if (addr->l2tp_family != AF_INET)
		return -EINVAL;

	lock_sock(sk);

	ret = -EINVAL;
	if (!sock_flag(sk, SOCK_ZAPPED))
		goto out;

	if (sk->sk_state != TCP_CLOSE)
		goto out;

	chk_addr_ret = inet_addr_type(net, addr->l2tp_addr.s_addr);
	ret = -EADDRNOTAVAIL;
	if (addr->l2tp_addr.s_addr && chk_addr_ret != RTN_LOCAL &&
	    chk_addr_ret != RTN_MULTICAST && chk_addr_ret != RTN_BROADCAST)
		goto out;

	if (addr->l2tp_addr.s_addr) {
		inet->inet_rcv_saddr = addr->l2tp_addr.s_addr;
		inet->inet_saddr = addr->l2tp_addr.s_addr;
	}
	if (chk_addr_ret == RTN_MULTICAST || chk_addr_ret == RTN_BROADCAST)
		inet->inet_saddr = 0;  /* Use device */

	pn = l2tp_ip_pernet(net);
	write_lock_bh(&pn->l2tp_ip_lock);
	if (__l2tp_ip_bind_lookup(net, addr->l2tp_addr.s_addr, 0,
				  sk->sk_bound_dev_if, addr->l2tp_conn_id)) {
		write_unlock_bh(&pn->l2tp_ip_lock);
		ret = -EADDRINUSE;
		goto out;
	}

	sk_dst_reset(sk);
	l2tp_ip_sk(sk)->conn_id = addr->l2tp_conn_id;

	sk_add_bind_node(sk, &pn->l2tp_ip_bind_table);
	sk_del_node_init(sk);
	write_unlock_bh(&pn->l2tp_ip_lock);

	ret = 0;
	sock_reset_flag(sk, SOCK_ZAPPED);

out:
	release_sock(sk);

	return ret;
}

static int l2tp_ip_connect(struct sock *sk, struct sockaddr *uaddr, int addr_len)
{
	struct sockaddr_l2tpip *lsa = (struct sockaddr_l2tpip *)uaddr;
	struct l2tp_ip_net *pn = l2tp_ip_pernet(sock_net(sk));
	int rc;

	if (addr_len < sizeof(*lsa))
		return -EINVAL;

	if (ipv4_is_multicast(lsa->l2tp_addr.s_addr))
		return -EINVAL;

	lock_sock(sk);

	/* Must bind first - autobinding does not work */
	if (sock_flag(sk, SOCK_ZAPPED)) {
		rc = -EINVAL;
		goto out_sk;
	}

	rc = __ip4_datagram_connect(sk, uaddr, addr_len);
	if (rc < 0)
		goto out_sk;

	l2tp_ip_sk(sk)->peer_conn_id = lsa->l2tp_conn_id;

	write_lock_bh(&pn->l2tp_ip_lock);
	hlist_del_init(&sk->sk_bind_node);
	sk_add_bind_node(sk, &pn->l2tp_ip_bind_table);
	write_unlock_bh(&pn->l2tp_ip_lock);

out_sk:
	release_sock(sk);

	return rc;
}

static int l2tp_ip_disconnect(struct sock *sk, int flags)
{
	if (sock_flag(sk, SOCK_ZAPPED))
		return 0;

	return __udp_disconnect(sk, flags);
}

static int l2tp_ip_getname(struct socket *sock, struct sockaddr *uaddr,
			   int peer)
{
	struct sock *sk		= sock->sk;
	struct inet_sock *inet	= inet_sk(sk);
	struct l2tp_ip_sock *lsk = l2tp_ip_sk(sk);
	struct sockaddr_l2tpip *lsa = (struct sockaddr_l2tpip *)uaddr;

	memset(lsa, 0, sizeof(*lsa));
	lsa->l2tp_family = AF_INET;
	if (peer) {
		if (!inet->inet_dport)
			return -ENOTCONN;
		lsa->l2tp_conn_id = lsk->peer_conn_id;
		lsa->l2tp_addr.s_addr = inet->inet_daddr;
	} else {
		__be32 addr = inet->inet_rcv_saddr;

		if (!addr)
			addr = inet->inet_saddr;
		lsa->l2tp_conn_id = lsk->conn_id;
		lsa->l2tp_addr.s_addr = addr;
	}
	return sizeof(*lsa);
}

static int l2tp_ip_backlog_recv(struct sock *sk, struct sk_buff *skb)
{
	int rc;

	/* Charge it to the socket, dropping if the queue is full. */
	rc = sock_queue_rcv_skb(sk, skb);
	if (rc < 0)
		goto drop;

	return 0;

drop:
	IP_INC_STATS(sock_net(sk), IPSTATS_MIB_INDISCARDS);
	kfree_skb(skb);
	return 0;
}

/* Userspace will call sendmsg() on the tunnel socket to send L2TP
 * control frames.
 */
static int l2tp_ip_sendmsg(struct sock *sk, struct msghdr *msg, size_t len)
{
	struct sk_buff *skb;
	int rc;
	struct inet_sock *inet = inet_sk(sk);
	struct rtable *rt = NULL;
	struct flowi4 *fl4;
	int connected = 0;
	__be32 daddr;

	lock_sock(sk);

	rc = -ENOTCONN;
	if (sock_flag(sk, SOCK_DEAD))
		goto out;

	/* Get and verify the address. */
	if (msg->msg_name) {
		DECLARE_SOCKADDR(struct sockaddr_l2tpip *, lip, msg->msg_name);

		rc = -EINVAL;
		if (msg->msg_namelen < sizeof(*lip))
			goto out;

		if (lip->l2tp_family != AF_INET) {
			rc = -EAFNOSUPPORT;
			if (lip->l2tp_family != AF_UNSPEC)
				goto out;
		}

		daddr = lip->l2tp_addr.s_addr;
	} else {
		rc = -EDESTADDRREQ;
		if (sk->sk_state != TCP_ESTABLISHED)
			goto out;

		daddr = inet->inet_daddr;
		connected = 1;
	}

	/* Allocate a socket buffer */
	rc = -ENOMEM;
	skb = sock_wmalloc(sk, 2 + NET_SKB_PAD + sizeof(struct iphdr) +
			   4 + len, 0, GFP_KERNEL);
	if (!skb)
		goto error;

	/* Reserve space for headers, putting IP header on 4-byte boundary. */
	skb_reserve(skb, 2 + NET_SKB_PAD);
	skb_reset_network_header(skb);
	skb_reserve(skb, sizeof(struct iphdr));
	skb_reset_transport_header(skb);

	/* Insert 0 session_id */
	*((__be32 *)skb_put(skb, 4)) = 0;

	/* Copy user data into skb */
	rc = memcpy_from_msg(skb_put(skb, len), msg, len);
	if (rc < 0) {
		kfree_skb(skb);
		goto error;
	}

	fl4 = &inet->cork.fl.u.ip4;
	if (connected)
		rt = dst_rtable(__sk_dst_check(sk, 0));

	rcu_read_lock();
	if (!rt) {
		const struct ip_options_rcu *inet_opt;

		inet_opt = rcu_dereference(inet->inet_opt);

		/* Use correct destination address if we have options. */
		if (inet_opt && inet_opt->opt.srr)
			daddr = inet_opt->opt.faddr;

		/* If this fails, retransmit mechanism of transport layer will
		 * keep trying until route appears or the connection times
		 * itself out.
		 */
		rt = ip_route_output_ports(sock_net(sk), fl4, sk,
					   daddr, inet->inet_saddr,
					   inet->inet_dport, inet->inet_sport,
					   sk->sk_protocol, ip_sock_rt_tos(sk),
					   sk->sk_bound_dev_if);
		if (IS_ERR(rt))
			goto no_route;
		if (connected) {
			sk_setup_caps(sk, &rt->dst);
		} else {
			skb_dst_set(skb, &rt->dst);
			goto xmit;
		}
	}

	/* We don't need to clone dst here, it is guaranteed to not disappear.
	 *  __dev_xmit_skb() might force a refcount if needed.
	 */
	skb_dst_set_noref(skb, &rt->dst);

xmit:
	/* Queue the packet to IP for output */
	rc = ip_queue_xmit(sk, skb, &inet->cork.fl);
	rcu_read_unlock();

error:
	if (rc >= 0)
		rc = len;

out:
	release_sock(sk);
	return rc;

no_route:
	rcu_read_unlock();
	IP_INC_STATS(sock_net(sk), IPSTATS_MIB_OUTNOROUTES);
	kfree_skb(skb);
	rc = -EHOSTUNREACH;
	goto out;
}

static int l2tp_ip_recvmsg(struct sock *sk, struct msghdr *msg,
			   size_t len, int flags, int *addr_len)
{
	struct inet_sock *inet = inet_sk(sk);
	size_t copied = 0;
	int err = -EOPNOTSUPP;
	DECLARE_SOCKADDR(struct sockaddr_in *, sin, msg->msg_name);
	struct sk_buff *skb;

	if (flags & MSG_OOB)
		goto out;

	skb = skb_recv_datagram(sk, flags, &err);
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
	if (sin) {
		sin->sin_family = AF_INET;
		sin->sin_addr.s_addr = ip_hdr(skb)->saddr;
		sin->sin_port = 0;
		memset(&sin->sin_zero, 0, sizeof(sin->sin_zero));
		*addr_len = sizeof(*sin);
	}
	if (inet_cmsg_flags(inet))
		ip_cmsg_recv(msg, skb);
	if (flags & MSG_TRUNC)
		copied = skb->len;
done:
	skb_free_datagram(sk, skb);
out:
	return err ? err : copied;
}

int l2tp_ioctl(struct sock *sk, int cmd, int *karg)
{
	struct sk_buff *skb;

	switch (cmd) {
	case SIOCOUTQ:
		*karg = sk_wmem_alloc_get(sk);
		break;
	case SIOCINQ:
		spin_lock_bh(&sk->sk_receive_queue.lock);
		skb = skb_peek(&sk->sk_receive_queue);
		*karg = skb ? skb->len : 0;
		spin_unlock_bh(&sk->sk_receive_queue.lock);
		break;

	default:
		return -ENOIOCTLCMD;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(l2tp_ioctl);

static struct proto l2tp_ip_prot = {
	.name		   = "L2TP/IP",
	.owner		   = THIS_MODULE,
	.init		   = l2tp_ip_open,
	.close		   = l2tp_ip_close,
	.bind		   = l2tp_ip_bind,
	.connect	   = l2tp_ip_connect,
	.disconnect	   = l2tp_ip_disconnect,
	.ioctl		   = l2tp_ioctl,
	.destroy	   = l2tp_ip_destroy_sock,
	.setsockopt	   = ip_setsockopt,
	.getsockopt	   = ip_getsockopt,
	.sendmsg	   = l2tp_ip_sendmsg,
	.recvmsg	   = l2tp_ip_recvmsg,
	.backlog_rcv	   = l2tp_ip_backlog_recv,
	.hash		   = l2tp_ip_hash,
	.unhash		   = l2tp_ip_unhash,
	.obj_size	   = sizeof(struct l2tp_ip_sock),
};

static const struct proto_ops l2tp_ip_ops = {
	.family		   = PF_INET,
	.owner		   = THIS_MODULE,
	.release	   = inet_release,
	.bind		   = inet_bind,
	.connect	   = inet_dgram_connect,
	.socketpair	   = sock_no_socketpair,
	.accept		   = sock_no_accept,
	.getname	   = l2tp_ip_getname,
	.poll		   = datagram_poll,
	.ioctl		   = inet_ioctl,
	.gettstamp	   = sock_gettstamp,
	.listen		   = sock_no_listen,
	.shutdown	   = inet_shutdown,
	.setsockopt	   = sock_common_setsockopt,
	.getsockopt	   = sock_common_getsockopt,
	.sendmsg	   = inet_sendmsg,
	.recvmsg	   = sock_common_recvmsg,
	.mmap		   = sock_no_mmap,
};

static struct inet_protosw l2tp_ip_protosw = {
	.type		= SOCK_DGRAM,
	.protocol	= IPPROTO_L2TP,
	.prot		= &l2tp_ip_prot,
	.ops		= &l2tp_ip_ops,
};

static struct net_protocol l2tp_ip_protocol __read_mostly = {
	.handler	= l2tp_ip_recv,
};

static __net_init int l2tp_ip_init_net(struct net *net)
{
	struct l2tp_ip_net *pn = net_generic(net, l2tp_ip_net_id);

	rwlock_init(&pn->l2tp_ip_lock);
	INIT_HLIST_HEAD(&pn->l2tp_ip_table);
	INIT_HLIST_HEAD(&pn->l2tp_ip_bind_table);
	return 0;
}

static __net_exit void l2tp_ip_exit_net(struct net *net)
{
	struct l2tp_ip_net *pn = l2tp_ip_pernet(net);

	write_lock_bh(&pn->l2tp_ip_lock);
	WARN_ON_ONCE(hlist_count_nodes(&pn->l2tp_ip_table) != 0);
	WARN_ON_ONCE(hlist_count_nodes(&pn->l2tp_ip_bind_table) != 0);
	write_unlock_bh(&pn->l2tp_ip_lock);
}

static struct pernet_operations l2tp_ip_net_ops = {
	.init = l2tp_ip_init_net,
	.exit = l2tp_ip_exit_net,
	.id   = &l2tp_ip_net_id,
	.size = sizeof(struct l2tp_ip_net),
};

static int __init l2tp_ip_init(void)
{
	int err;

	pr_info("L2TP IP encapsulation support (L2TPv3)\n");

	err = register_pernet_device(&l2tp_ip_net_ops);
	if (err)
		goto out;

	err = proto_register(&l2tp_ip_prot, 1);
	if (err != 0)
		goto out1;

	err = inet_add_protocol(&l2tp_ip_protocol, IPPROTO_L2TP);
	if (err)
		goto out2;

	inet_register_protosw(&l2tp_ip_protosw);
	return 0;

out2:
	proto_unregister(&l2tp_ip_prot);
out1:
	unregister_pernet_device(&l2tp_ip_net_ops);
out:
	return err;
}

static void __exit l2tp_ip_exit(void)
{
	inet_unregister_protosw(&l2tp_ip_protosw);
	inet_del_protocol(&l2tp_ip_protocol, IPPROTO_L2TP);
	proto_unregister(&l2tp_ip_prot);
	unregister_pernet_device(&l2tp_ip_net_ops);
}

module_init(l2tp_ip_init);
module_exit(l2tp_ip_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("James Chapman <jchapman@katalix.com>");
MODULE_DESCRIPTION("L2TP over IP");
MODULE_VERSION("1.0");

/* Use the values of SOCK_DGRAM (2) as type and IPPROTO_L2TP (115) as protocol,
 * because __stringify doesn't like enums
 */
MODULE_ALIAS_NET_PF_PROTO_TYPE(PF_INET, 115, 2);
MODULE_ALIAS_NET_PF_PROTO(PF_INET, 115);
