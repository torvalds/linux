// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		PF_INET protocol family socket handler.
 *
 * Authors:	Ross Biro
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Florian La Roche, <flla@stud.uni-sb.de>
 *		Alan Cox, <A.Cox@swansea.ac.uk>
 *
 * Changes (see also sock.c)
 *
 *		piggy,
 *		Karl Knutson	:	Socket protocol table
 *		A.N.Kuznetsov	:	Socket death error in accept().
 *		John Richardson :	Fix non blocking error in connect()
 *					so sockets that fail to connect
 *					don't return -EINPROGRESS.
 *		Alan Cox	:	Asynchronous I/O support
 *		Alan Cox	:	Keep correct socket pointer on sock
 *					structures
 *					when accept() ed
 *		Alan Cox	:	Semantics of SO_LINGER aren't state
 *					moved to close when you look carefully.
 *					With this fixed and the accept bug fixed
 *					some RPC stuff seems happier.
 *		Niibe Yutaka	:	4.4BSD style write async I/O
 *		Alan Cox,
 *		Tony Gale 	:	Fixed reuse semantics.
 *		Alan Cox	:	bind() shouldn't abort existing but dead
 *					sockets. Stops FTP netin:.. I hope.
 *		Alan Cox	:	bind() works correctly for RAW sockets.
 *					Note that FreeBSD at least was broken
 *					in this respect so be careful with
 *					compatibility tests...
 *		Alan Cox	:	routing cache support
 *		Alan Cox	:	memzero the socket structure for
 *					compactness.
 *		Matt Day	:	nonblock connect error handler
 *		Alan Cox	:	Allow large numbers of pending sockets
 *					(eg for big web sites), but only if
 *					specifically application requested.
 *		Alan Cox	:	New buffering throughout IP. Used
 *					dumbly.
 *		Alan Cox	:	New buffering now used smartly.
 *		Alan Cox	:	BSD rather than common sense
 *					interpretation of listen.
 *		Germano Caronni	:	Assorted small races.
 *		Alan Cox	:	sendmsg/recvmsg basic support.
 *		Alan Cox	:	Only sendmsg/recvmsg now supported.
 *		Alan Cox	:	Locked down bind (see security list).
 *		Alan Cox	:	Loosened bind a little.
 *		Mike McLagan	:	ADD/DEL DLCI Ioctls
 *	Willy Konynenberg	:	Transparent proxying support.
 *		David S. Miller	:	New socket lookup architecture.
 *					Some other random speedups.
 *		Cyrus Durgin	:	Cleaned up file for kmod hacks.
 *		Andi Kleen	:	Fix inet_stream_connect TCP race.
 */

#define pr_fmt(fmt) "IPv4: " fmt

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/kmod.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/capability.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/netfilter_ipv4.h>
#include <linux/random.h>
#include <linux/slab.h>

#include <linux/uaccess.h>

#include <linux/inet.h>
#include <linux/igmp.h>
#include <linux/inetdevice.h>
#include <linux/netdevice.h>
#include <net/checksum.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/arp.h>
#include <net/route.h>
#include <net/ip_fib.h>
#include <net/inet_connection_sock.h>
#include <net/gro.h>
#include <net/gso.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/udplite.h>
#include <net/ping.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/raw.h>
#include <net/icmp.h>
#include <net/inet_common.h>
#include <net/ip_tunnels.h>
#include <net/xfrm.h>
#include <net/net_namespace.h>
#include <net/secure_seq.h>
#ifdef CONFIG_IP_MROUTE
#include <linux/mroute.h>
#endif
#include <net/l3mdev.h>
#include <net/compat.h>

#include <trace/events/sock.h>

/* The inetsw table contains everything that inet_create needs to
 * build a new socket.
 */
static struct list_head inetsw[SOCK_MAX];
static DEFINE_SPINLOCK(inetsw_lock);

/* New destruction routine */

void inet_sock_destruct(struct sock *sk)
{
	struct inet_sock *inet = inet_sk(sk);

	__skb_queue_purge(&sk->sk_receive_queue);
	__skb_queue_purge(&sk->sk_error_queue);

	sk_mem_reclaim_final(sk);

	if (sk->sk_type == SOCK_STREAM && sk->sk_state != TCP_CLOSE) {
		pr_err("Attempt to release TCP socket in state %d %p\n",
		       sk->sk_state, sk);
		return;
	}
	if (!sock_flag(sk, SOCK_DEAD)) {
		pr_err("Attempt to release alive inet socket %p\n", sk);
		return;
	}

	WARN_ON_ONCE(atomic_read(&sk->sk_rmem_alloc));
	WARN_ON_ONCE(refcount_read(&sk->sk_wmem_alloc));
	WARN_ON_ONCE(sk->sk_wmem_queued);
	WARN_ON_ONCE(sk_forward_alloc_get(sk));

	kfree(rcu_dereference_protected(inet->inet_opt, 1));
	dst_release(rcu_dereference_protected(sk->sk_dst_cache, 1));
	dst_release(rcu_dereference_protected(sk->sk_rx_dst, 1));
}
EXPORT_SYMBOL(inet_sock_destruct);

/*
 *	The routines beyond this point handle the behaviour of an AF_INET
 *	socket object. Mostly it punts to the subprotocols of IP to do
 *	the work.
 */

/*
 *	Automatically bind an unbound socket.
 */

static int inet_autobind(struct sock *sk)
{
	struct inet_sock *inet;
	/* We may need to bind the socket. */
	lock_sock(sk);
	inet = inet_sk(sk);
	if (!inet->inet_num) {
		if (sk->sk_prot->get_port(sk, 0)) {
			release_sock(sk);
			return -EAGAIN;
		}
		inet->inet_sport = htons(inet->inet_num);
	}
	release_sock(sk);
	return 0;
}

int __inet_listen_sk(struct sock *sk, int backlog)
{
	unsigned char old_state = sk->sk_state;
	int err, tcp_fastopen;

	if (!((1 << old_state) & (TCPF_CLOSE | TCPF_LISTEN)))
		return -EINVAL;

	WRITE_ONCE(sk->sk_max_ack_backlog, backlog);
	/* Really, if the socket is already in listen state
	 * we can only allow the backlog to be adjusted.
	 */
	if (old_state != TCP_LISTEN) {
		/* Enable TFO w/o requiring TCP_FASTOPEN socket option.
		 * Note that only TCP sockets (SOCK_STREAM) will reach here.
		 * Also fastopen backlog may already been set via the option
		 * because the socket was in TCP_LISTEN state previously but
		 * was shutdown() rather than close().
		 */
		tcp_fastopen = READ_ONCE(sock_net(sk)->ipv4.sysctl_tcp_fastopen);
		if ((tcp_fastopen & TFO_SERVER_WO_SOCKOPT1) &&
		    (tcp_fastopen & TFO_SERVER_ENABLE) &&
		    !inet_csk(sk)->icsk_accept_queue.fastopenq.max_qlen) {
			fastopen_queue_tune(sk, backlog);
			tcp_fastopen_init_key_once(sock_net(sk));
		}

		err = inet_csk_listen_start(sk);
		if (err)
			return err;

		tcp_call_bpf(sk, BPF_SOCK_OPS_TCP_LISTEN_CB, 0, NULL);
	}
	return 0;
}

/*
 *	Move a socket into listening state.
 */
int inet_listen(struct socket *sock, int backlog)
{
	struct sock *sk = sock->sk;
	int err = -EINVAL;

	lock_sock(sk);

	if (sock->state != SS_UNCONNECTED || sock->type != SOCK_STREAM)
		goto out;

	err = __inet_listen_sk(sk, backlog);

out:
	release_sock(sk);
	return err;
}
EXPORT_SYMBOL(inet_listen);

/*
 *	Create an inet socket.
 */

static int inet_create(struct net *net, struct socket *sock, int protocol,
		       int kern)
{
	struct sock *sk;
	struct inet_protosw *answer;
	struct inet_sock *inet;
	struct proto *answer_prot;
	unsigned char answer_flags;
	int try_loading_module = 0;
	int err;

	if (protocol < 0 || protocol >= IPPROTO_MAX)
		return -EINVAL;

	sock->state = SS_UNCONNECTED;

	/* Look for the requested type/protocol pair. */
lookup_protocol:
	err = -ESOCKTNOSUPPORT;
	rcu_read_lock();
	list_for_each_entry_rcu(answer, &inetsw[sock->type], list) {

		err = 0;
		/* Check the non-wild match. */
		if (protocol == answer->protocol) {
			if (protocol != IPPROTO_IP)
				break;
		} else {
			/* Check for the two wild cases. */
			if (IPPROTO_IP == protocol) {
				protocol = answer->protocol;
				break;
			}
			if (IPPROTO_IP == answer->protocol)
				break;
		}
		err = -EPROTONOSUPPORT;
	}

	if (unlikely(err)) {
		if (try_loading_module < 2) {
			rcu_read_unlock();
			/*
			 * Be more specific, e.g. net-pf-2-proto-132-type-1
			 * (net-pf-PF_INET-proto-IPPROTO_SCTP-type-SOCK_STREAM)
			 */
			if (++try_loading_module == 1)
				request_module("net-pf-%d-proto-%d-type-%d",
					       PF_INET, protocol, sock->type);
			/*
			 * Fall back to generic, e.g. net-pf-2-proto-132
			 * (net-pf-PF_INET-proto-IPPROTO_SCTP)
			 */
			else
				request_module("net-pf-%d-proto-%d",
					       PF_INET, protocol);
			goto lookup_protocol;
		} else
			goto out_rcu_unlock;
	}

	err = -EPERM;
	if (sock->type == SOCK_RAW && !kern &&
	    !ns_capable(net->user_ns, CAP_NET_RAW))
		goto out_rcu_unlock;

	sock->ops = answer->ops;
	answer_prot = answer->prot;
	answer_flags = answer->flags;
	rcu_read_unlock();

	WARN_ON(!answer_prot->slab);

	err = -ENOMEM;
	sk = sk_alloc(net, PF_INET, GFP_KERNEL, answer_prot, kern);
	if (!sk)
		goto out;

	err = 0;
	if (INET_PROTOSW_REUSE & answer_flags)
		sk->sk_reuse = SK_CAN_REUSE;

	inet = inet_sk(sk);
	inet_assign_bit(IS_ICSK, sk, INET_PROTOSW_ICSK & answer_flags);

	inet_clear_bit(NODEFRAG, sk);

	if (SOCK_RAW == sock->type) {
		inet->inet_num = protocol;
		if (IPPROTO_RAW == protocol)
			inet_set_bit(HDRINCL, sk);
	}

	if (READ_ONCE(net->ipv4.sysctl_ip_no_pmtu_disc))
		inet->pmtudisc = IP_PMTUDISC_DONT;
	else
		inet->pmtudisc = IP_PMTUDISC_WANT;

	atomic_set(&inet->inet_id, 0);

	sock_init_data(sock, sk);

	sk->sk_destruct	   = inet_sock_destruct;
	sk->sk_protocol	   = protocol;
	sk->sk_backlog_rcv = sk->sk_prot->backlog_rcv;
	sk->sk_txrehash = READ_ONCE(net->core.sysctl_txrehash);

	inet->uc_ttl	= -1;
	inet_set_bit(MC_LOOP, sk);
	inet->mc_ttl	= 1;
	inet_set_bit(MC_ALL, sk);
	inet->mc_index	= 0;
	inet->mc_list	= NULL;
	inet->rcv_tos	= 0;

	if (inet->inet_num) {
		/* It assumes that any protocol which allows
		 * the user to assign a number at socket
		 * creation time automatically
		 * shares.
		 */
		inet->inet_sport = htons(inet->inet_num);
		/* Add to protocol hash chains. */
		err = sk->sk_prot->hash(sk);
		if (err) {
			sk_common_release(sk);
			goto out;
		}
	}

	if (sk->sk_prot->init) {
		err = sk->sk_prot->init(sk);
		if (err) {
			sk_common_release(sk);
			goto out;
		}
	}

	if (!kern) {
		err = BPF_CGROUP_RUN_PROG_INET_SOCK(sk);
		if (err) {
			sk_common_release(sk);
			goto out;
		}
	}
out:
	return err;
out_rcu_unlock:
	rcu_read_unlock();
	goto out;
}


/*
 *	The peer socket should always be NULL (or else). When we call this
 *	function we are destroying the object and from then on nobody
 *	should refer to it.
 */
int inet_release(struct socket *sock)
{
	struct sock *sk = sock->sk;

	if (sk) {
		long timeout;

		if (!sk->sk_kern_sock)
			BPF_CGROUP_RUN_PROG_INET_SOCK_RELEASE(sk);

		/* Applications forget to leave groups before exiting */
		ip_mc_drop_socket(sk);

		/* If linger is set, we don't return until the close
		 * is complete.  Otherwise we return immediately. The
		 * actually closing is done the same either way.
		 *
		 * If the close is due to the process exiting, we never
		 * linger..
		 */
		timeout = 0;
		if (sock_flag(sk, SOCK_LINGER) &&
		    !(current->flags & PF_EXITING))
			timeout = sk->sk_lingertime;
		sk->sk_prot->close(sk, timeout);
		sock->sk = NULL;
	}
	return 0;
}
EXPORT_SYMBOL(inet_release);

int inet_bind_sk(struct sock *sk, struct sockaddr *uaddr, int addr_len)
{
	u32 flags = BIND_WITH_LOCK;
	int err;

	/* If the socket has its own bind function then use it. (RAW) */
	if (sk->sk_prot->bind) {
		return sk->sk_prot->bind(sk, uaddr, addr_len);
	}
	if (addr_len < sizeof(struct sockaddr_in))
		return -EINVAL;

	/* BPF prog is run before any checks are done so that if the prog
	 * changes context in a wrong way it will be caught.
	 */
	err = BPF_CGROUP_RUN_PROG_INET_BIND_LOCK(sk, uaddr, &addr_len,
						 CGROUP_INET4_BIND, &flags);
	if (err)
		return err;

	return __inet_bind(sk, uaddr, addr_len, flags);
}

int inet_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	return inet_bind_sk(sock->sk, uaddr, addr_len);
}
EXPORT_SYMBOL(inet_bind);

int __inet_bind(struct sock *sk, struct sockaddr *uaddr, int addr_len,
		u32 flags)
{
	struct sockaddr_in *addr = (struct sockaddr_in *)uaddr;
	struct inet_sock *inet = inet_sk(sk);
	struct net *net = sock_net(sk);
	unsigned short snum;
	int chk_addr_ret;
	u32 tb_id = RT_TABLE_LOCAL;
	int err;

	if (addr->sin_family != AF_INET) {
		/* Compatibility games : accept AF_UNSPEC (mapped to AF_INET)
		 * only if s_addr is INADDR_ANY.
		 */
		err = -EAFNOSUPPORT;
		if (addr->sin_family != AF_UNSPEC ||
		    addr->sin_addr.s_addr != htonl(INADDR_ANY))
			goto out;
	}

	tb_id = l3mdev_fib_table_by_index(net, sk->sk_bound_dev_if) ? : tb_id;
	chk_addr_ret = inet_addr_type_table(net, addr->sin_addr.s_addr, tb_id);

	/* Not specified by any standard per-se, however it breaks too
	 * many applications when removed.  It is unfortunate since
	 * allowing applications to make a non-local bind solves
	 * several problems with systems using dynamic addressing.
	 * (ie. your servers still start up even if your ISDN link
	 *  is temporarily down)
	 */
	err = -EADDRNOTAVAIL;
	if (!inet_addr_valid_or_nonlocal(net, inet, addr->sin_addr.s_addr,
	                                 chk_addr_ret))
		goto out;

	snum = ntohs(addr->sin_port);
	err = -EACCES;
	if (!(flags & BIND_NO_CAP_NET_BIND_SERVICE) &&
	    snum && inet_port_requires_bind_service(net, snum) &&
	    !ns_capable(net->user_ns, CAP_NET_BIND_SERVICE))
		goto out;

	/*      We keep a pair of addresses. rcv_saddr is the one
	 *      used by hash lookups, and saddr is used for transmit.
	 *
	 *      In the BSD API these are the same except where it
	 *      would be illegal to use them (multicast/broadcast) in
	 *      which case the sending device address is used.
	 */
	if (flags & BIND_WITH_LOCK)
		lock_sock(sk);

	/* Check these errors (active socket, double bind). */
	err = -EINVAL;
	if (sk->sk_state != TCP_CLOSE || inet->inet_num)
		goto out_release_sock;

	inet->inet_rcv_saddr = inet->inet_saddr = addr->sin_addr.s_addr;
	if (chk_addr_ret == RTN_MULTICAST || chk_addr_ret == RTN_BROADCAST)
		inet->inet_saddr = 0;  /* Use device */

	/* Make sure we are allowed to bind here. */
	if (snum || !(inet_test_bit(BIND_ADDRESS_NO_PORT, sk) ||
		      (flags & BIND_FORCE_ADDRESS_NO_PORT))) {
		err = sk->sk_prot->get_port(sk, snum);
		if (err) {
			inet->inet_saddr = inet->inet_rcv_saddr = 0;
			goto out_release_sock;
		}
		if (!(flags & BIND_FROM_BPF)) {
			err = BPF_CGROUP_RUN_PROG_INET4_POST_BIND(sk);
			if (err) {
				inet->inet_saddr = inet->inet_rcv_saddr = 0;
				if (sk->sk_prot->put_port)
					sk->sk_prot->put_port(sk);
				goto out_release_sock;
			}
		}
	}

	if (inet->inet_rcv_saddr)
		sk->sk_userlocks |= SOCK_BINDADDR_LOCK;
	if (snum)
		sk->sk_userlocks |= SOCK_BINDPORT_LOCK;
	inet->inet_sport = htons(inet->inet_num);
	inet->inet_daddr = 0;
	inet->inet_dport = 0;
	sk_dst_reset(sk);
	err = 0;
out_release_sock:
	if (flags & BIND_WITH_LOCK)
		release_sock(sk);
out:
	return err;
}

int inet_dgram_connect(struct socket *sock, struct sockaddr *uaddr,
		       int addr_len, int flags)
{
	struct sock *sk = sock->sk;
	const struct proto *prot;
	int err;

	if (addr_len < sizeof(uaddr->sa_family))
		return -EINVAL;

	/* IPV6_ADDRFORM can change sk->sk_prot under us. */
	prot = READ_ONCE(sk->sk_prot);

	if (uaddr->sa_family == AF_UNSPEC)
		return prot->disconnect(sk, flags);

	if (BPF_CGROUP_PRE_CONNECT_ENABLED(sk)) {
		err = prot->pre_connect(sk, uaddr, addr_len);
		if (err)
			return err;
	}

	if (data_race(!inet_sk(sk)->inet_num) && inet_autobind(sk))
		return -EAGAIN;
	return prot->connect(sk, uaddr, addr_len);
}
EXPORT_SYMBOL(inet_dgram_connect);

static long inet_wait_for_connect(struct sock *sk, long timeo, int writebias)
{
	DEFINE_WAIT_FUNC(wait, woken_wake_function);

	add_wait_queue(sk_sleep(sk), &wait);
	sk->sk_write_pending += writebias;

	/* Basic assumption: if someone sets sk->sk_err, he _must_
	 * change state of the socket from TCP_SYN_*.
	 * Connect() does not allow to get error notifications
	 * without closing the socket.
	 */
	while ((1 << sk->sk_state) & (TCPF_SYN_SENT | TCPF_SYN_RECV)) {
		release_sock(sk);
		timeo = wait_woken(&wait, TASK_INTERRUPTIBLE, timeo);
		lock_sock(sk);
		if (signal_pending(current) || !timeo)
			break;
	}
	remove_wait_queue(sk_sleep(sk), &wait);
	sk->sk_write_pending -= writebias;
	return timeo;
}

/*
 *	Connect to a remote host. There is regrettably still a little
 *	TCP 'magic' in here.
 */
int __inet_stream_connect(struct socket *sock, struct sockaddr *uaddr,
			  int addr_len, int flags, int is_sendmsg)
{
	struct sock *sk = sock->sk;
	int err;
	long timeo;

	/*
	 * uaddr can be NULL and addr_len can be 0 if:
	 * sk is a TCP fastopen active socket and
	 * TCP_FASTOPEN_CONNECT sockopt is set and
	 * we already have a valid cookie for this socket.
	 * In this case, user can call write() after connect().
	 * write() will invoke tcp_sendmsg_fastopen() which calls
	 * __inet_stream_connect().
	 */
	if (uaddr) {
		if (addr_len < sizeof(uaddr->sa_family))
			return -EINVAL;

		if (uaddr->sa_family == AF_UNSPEC) {
			sk->sk_disconnects++;
			err = sk->sk_prot->disconnect(sk, flags);
			sock->state = err ? SS_DISCONNECTING : SS_UNCONNECTED;
			goto out;
		}
	}

	switch (sock->state) {
	default:
		err = -EINVAL;
		goto out;
	case SS_CONNECTED:
		err = -EISCONN;
		goto out;
	case SS_CONNECTING:
		if (inet_test_bit(DEFER_CONNECT, sk))
			err = is_sendmsg ? -EINPROGRESS : -EISCONN;
		else
			err = -EALREADY;
		/* Fall out of switch with err, set for this state */
		break;
	case SS_UNCONNECTED:
		err = -EISCONN;
		if (sk->sk_state != TCP_CLOSE)
			goto out;

		if (BPF_CGROUP_PRE_CONNECT_ENABLED(sk)) {
			err = sk->sk_prot->pre_connect(sk, uaddr, addr_len);
			if (err)
				goto out;
		}

		err = sk->sk_prot->connect(sk, uaddr, addr_len);
		if (err < 0)
			goto out;

		sock->state = SS_CONNECTING;

		if (!err && inet_test_bit(DEFER_CONNECT, sk))
			goto out;

		/* Just entered SS_CONNECTING state; the only
		 * difference is that return value in non-blocking
		 * case is EINPROGRESS, rather than EALREADY.
		 */
		err = -EINPROGRESS;
		break;
	}

	timeo = sock_sndtimeo(sk, flags & O_NONBLOCK);

	if ((1 << sk->sk_state) & (TCPF_SYN_SENT | TCPF_SYN_RECV)) {
		int writebias = (sk->sk_protocol == IPPROTO_TCP) &&
				tcp_sk(sk)->fastopen_req &&
				tcp_sk(sk)->fastopen_req->data ? 1 : 0;
		int dis = sk->sk_disconnects;

		/* Error code is set above */
		if (!timeo || !inet_wait_for_connect(sk, timeo, writebias))
			goto out;

		err = sock_intr_errno(timeo);
		if (signal_pending(current))
			goto out;

		if (dis != sk->sk_disconnects) {
			err = -EPIPE;
			goto out;
		}
	}

	/* Connection was closed by RST, timeout, ICMP error
	 * or another process disconnected us.
	 */
	if (sk->sk_state == TCP_CLOSE)
		goto sock_error;

	/* sk->sk_err may be not zero now, if RECVERR was ordered by user
	 * and error was received after socket entered established state.
	 * Hence, it is handled normally after connect() return successfully.
	 */

	sock->state = SS_CONNECTED;
	err = 0;
out:
	return err;

sock_error:
	err = sock_error(sk) ? : -ECONNABORTED;
	sock->state = SS_UNCONNECTED;
	sk->sk_disconnects++;
	if (sk->sk_prot->disconnect(sk, flags))
		sock->state = SS_DISCONNECTING;
	goto out;
}
EXPORT_SYMBOL(__inet_stream_connect);

int inet_stream_connect(struct socket *sock, struct sockaddr *uaddr,
			int addr_len, int flags)
{
	int err;

	lock_sock(sock->sk);
	err = __inet_stream_connect(sock, uaddr, addr_len, flags, 0);
	release_sock(sock->sk);
	return err;
}
EXPORT_SYMBOL(inet_stream_connect);

void __inet_accept(struct socket *sock, struct socket *newsock, struct sock *newsk)
{
	sock_rps_record_flow(newsk);
	WARN_ON(!((1 << newsk->sk_state) &
		  (TCPF_ESTABLISHED | TCPF_SYN_RECV |
		  TCPF_CLOSE_WAIT | TCPF_CLOSE)));

	if (test_bit(SOCK_SUPPORT_ZC, &sock->flags))
		set_bit(SOCK_SUPPORT_ZC, &newsock->flags);
	sock_graft(newsk, newsock);

	newsock->state = SS_CONNECTED;
}

/*
 *	Accept a pending connection. The TCP layer now gives BSD semantics.
 */

int inet_accept(struct socket *sock, struct socket *newsock, int flags,
		bool kern)
{
	struct sock *sk1 = sock->sk, *sk2;
	int err = -EINVAL;

	/* IPV6_ADDRFORM can change sk->sk_prot under us. */
	sk2 = READ_ONCE(sk1->sk_prot)->accept(sk1, flags, &err, kern);
	if (!sk2)
		return err;

	lock_sock(sk2);
	__inet_accept(sock, newsock, sk2);
	release_sock(sk2);
	return 0;
}
EXPORT_SYMBOL(inet_accept);

/*
 *	This does both peername and sockname.
 */
int inet_getname(struct socket *sock, struct sockaddr *uaddr,
		 int peer)
{
	struct sock *sk		= sock->sk;
	struct inet_sock *inet	= inet_sk(sk);
	DECLARE_SOCKADDR(struct sockaddr_in *, sin, uaddr);
	int sin_addr_len = sizeof(*sin);

	sin->sin_family = AF_INET;
	lock_sock(sk);
	if (peer) {
		if (!inet->inet_dport ||
		    (((1 << sk->sk_state) & (TCPF_CLOSE | TCPF_SYN_SENT)) &&
		     peer == 1)) {
			release_sock(sk);
			return -ENOTCONN;
		}
		sin->sin_port = inet->inet_dport;
		sin->sin_addr.s_addr = inet->inet_daddr;
		BPF_CGROUP_RUN_SA_PROG(sk, (struct sockaddr *)sin, &sin_addr_len,
				       CGROUP_INET4_GETPEERNAME);
	} else {
		__be32 addr = inet->inet_rcv_saddr;
		if (!addr)
			addr = inet->inet_saddr;
		sin->sin_port = inet->inet_sport;
		sin->sin_addr.s_addr = addr;
		BPF_CGROUP_RUN_SA_PROG(sk, (struct sockaddr *)sin, &sin_addr_len,
				       CGROUP_INET4_GETSOCKNAME);
	}
	release_sock(sk);
	memset(sin->sin_zero, 0, sizeof(sin->sin_zero));
	return sin_addr_len;
}
EXPORT_SYMBOL(inet_getname);

int inet_send_prepare(struct sock *sk)
{
	sock_rps_record_flow(sk);

	/* We may need to bind the socket. */
	if (data_race(!inet_sk(sk)->inet_num) && !sk->sk_prot->no_autobind &&
	    inet_autobind(sk))
		return -EAGAIN;

	return 0;
}
EXPORT_SYMBOL_GPL(inet_send_prepare);

int inet_sendmsg(struct socket *sock, struct msghdr *msg, size_t size)
{
	struct sock *sk = sock->sk;

	if (unlikely(inet_send_prepare(sk)))
		return -EAGAIN;

	return INDIRECT_CALL_2(sk->sk_prot->sendmsg, tcp_sendmsg, udp_sendmsg,
			       sk, msg, size);
}
EXPORT_SYMBOL(inet_sendmsg);

void inet_splice_eof(struct socket *sock)
{
	const struct proto *prot;
	struct sock *sk = sock->sk;

	if (unlikely(inet_send_prepare(sk)))
		return;

	/* IPV6_ADDRFORM can change sk->sk_prot under us. */
	prot = READ_ONCE(sk->sk_prot);
	if (prot->splice_eof)
		prot->splice_eof(sock);
}
EXPORT_SYMBOL_GPL(inet_splice_eof);

INDIRECT_CALLABLE_DECLARE(int udp_recvmsg(struct sock *, struct msghdr *,
					  size_t, int, int *));
int inet_recvmsg(struct socket *sock, struct msghdr *msg, size_t size,
		 int flags)
{
	struct sock *sk = sock->sk;
	int addr_len = 0;
	int err;

	if (likely(!(flags & MSG_ERRQUEUE)))
		sock_rps_record_flow(sk);

	err = INDIRECT_CALL_2(sk->sk_prot->recvmsg, tcp_recvmsg, udp_recvmsg,
			      sk, msg, size, flags, &addr_len);
	if (err >= 0)
		msg->msg_namelen = addr_len;
	return err;
}
EXPORT_SYMBOL(inet_recvmsg);

int inet_shutdown(struct socket *sock, int how)
{
	struct sock *sk = sock->sk;
	int err = 0;

	/* This should really check to make sure
	 * the socket is a TCP socket. (WHY AC...)
	 */
	how++; /* maps 0->1 has the advantage of making bit 1 rcvs and
		       1->2 bit 2 snds.
		       2->3 */
	if ((how & ~SHUTDOWN_MASK) || !how)	/* MAXINT->0 */
		return -EINVAL;

	lock_sock(sk);
	if (sock->state == SS_CONNECTING) {
		if ((1 << sk->sk_state) &
		    (TCPF_SYN_SENT | TCPF_SYN_RECV | TCPF_CLOSE))
			sock->state = SS_DISCONNECTING;
		else
			sock->state = SS_CONNECTED;
	}

	switch (sk->sk_state) {
	case TCP_CLOSE:
		err = -ENOTCONN;
		/* Hack to wake up other listeners, who can poll for
		   EPOLLHUP, even on eg. unconnected UDP sockets -- RR */
		fallthrough;
	default:
		WRITE_ONCE(sk->sk_shutdown, sk->sk_shutdown | how);
		if (sk->sk_prot->shutdown)
			sk->sk_prot->shutdown(sk, how);
		break;

	/* Remaining two branches are temporary solution for missing
	 * close() in multithreaded environment. It is _not_ a good idea,
	 * but we have no choice until close() is repaired at VFS level.
	 */
	case TCP_LISTEN:
		if (!(how & RCV_SHUTDOWN))
			break;
		fallthrough;
	case TCP_SYN_SENT:
		err = sk->sk_prot->disconnect(sk, O_NONBLOCK);
		sock->state = err ? SS_DISCONNECTING : SS_UNCONNECTED;
		break;
	}

	/* Wake up anyone sleeping in poll. */
	sk->sk_state_change(sk);
	release_sock(sk);
	return err;
}
EXPORT_SYMBOL(inet_shutdown);

/*
 *	ioctl() calls you can issue on an INET socket. Most of these are
 *	device configuration and stuff and very rarely used. Some ioctls
 *	pass on to the socket itself.
 *
 *	NOTE: I like the idea of a module for the config stuff. ie ifconfig
 *	loads the devconfigure module does its configuring and unloads it.
 *	There's a good 20K of config code hanging around the kernel.
 */

int inet_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct sock *sk = sock->sk;
	int err = 0;
	struct net *net = sock_net(sk);
	void __user *p = (void __user *)arg;
	struct ifreq ifr;
	struct rtentry rt;

	switch (cmd) {
	case SIOCADDRT:
	case SIOCDELRT:
		if (copy_from_user(&rt, p, sizeof(struct rtentry)))
			return -EFAULT;
		err = ip_rt_ioctl(net, cmd, &rt);
		break;
	case SIOCRTMSG:
		err = -EINVAL;
		break;
	case SIOCDARP:
	case SIOCGARP:
	case SIOCSARP:
		err = arp_ioctl(net, cmd, (void __user *)arg);
		break;
	case SIOCGIFADDR:
	case SIOCGIFBRDADDR:
	case SIOCGIFNETMASK:
	case SIOCGIFDSTADDR:
	case SIOCGIFPFLAGS:
		if (get_user_ifreq(&ifr, NULL, p))
			return -EFAULT;
		err = devinet_ioctl(net, cmd, &ifr);
		if (!err && put_user_ifreq(&ifr, p))
			err = -EFAULT;
		break;

	case SIOCSIFADDR:
	case SIOCSIFBRDADDR:
	case SIOCSIFNETMASK:
	case SIOCSIFDSTADDR:
	case SIOCSIFPFLAGS:
	case SIOCSIFFLAGS:
		if (get_user_ifreq(&ifr, NULL, p))
			return -EFAULT;
		err = devinet_ioctl(net, cmd, &ifr);
		break;
	default:
		if (sk->sk_prot->ioctl)
			err = sk_ioctl(sk, cmd, (void __user *)arg);
		else
			err = -ENOIOCTLCMD;
		break;
	}
	return err;
}
EXPORT_SYMBOL(inet_ioctl);

#ifdef CONFIG_COMPAT
static int inet_compat_routing_ioctl(struct sock *sk, unsigned int cmd,
		struct compat_rtentry __user *ur)
{
	compat_uptr_t rtdev;
	struct rtentry rt;

	if (copy_from_user(&rt.rt_dst, &ur->rt_dst,
			3 * sizeof(struct sockaddr)) ||
	    get_user(rt.rt_flags, &ur->rt_flags) ||
	    get_user(rt.rt_metric, &ur->rt_metric) ||
	    get_user(rt.rt_mtu, &ur->rt_mtu) ||
	    get_user(rt.rt_window, &ur->rt_window) ||
	    get_user(rt.rt_irtt, &ur->rt_irtt) ||
	    get_user(rtdev, &ur->rt_dev))
		return -EFAULT;

	rt.rt_dev = compat_ptr(rtdev);
	return ip_rt_ioctl(sock_net(sk), cmd, &rt);
}

static int inet_compat_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	void __user *argp = compat_ptr(arg);
	struct sock *sk = sock->sk;

	switch (cmd) {
	case SIOCADDRT:
	case SIOCDELRT:
		return inet_compat_routing_ioctl(sk, cmd, argp);
	default:
		if (!sk->sk_prot->compat_ioctl)
			return -ENOIOCTLCMD;
		return sk->sk_prot->compat_ioctl(sk, cmd, arg);
	}
}
#endif /* CONFIG_COMPAT */

const struct proto_ops inet_stream_ops = {
	.family		   = PF_INET,
	.owner		   = THIS_MODULE,
	.release	   = inet_release,
	.bind		   = inet_bind,
	.connect	   = inet_stream_connect,
	.socketpair	   = sock_no_socketpair,
	.accept		   = inet_accept,
	.getname	   = inet_getname,
	.poll		   = tcp_poll,
	.ioctl		   = inet_ioctl,
	.gettstamp	   = sock_gettstamp,
	.listen		   = inet_listen,
	.shutdown	   = inet_shutdown,
	.setsockopt	   = sock_common_setsockopt,
	.getsockopt	   = sock_common_getsockopt,
	.sendmsg	   = inet_sendmsg,
	.recvmsg	   = inet_recvmsg,
#ifdef CONFIG_MMU
	.mmap		   = tcp_mmap,
#endif
	.splice_eof	   = inet_splice_eof,
	.splice_read	   = tcp_splice_read,
	.read_sock	   = tcp_read_sock,
	.read_skb	   = tcp_read_skb,
	.sendmsg_locked    = tcp_sendmsg_locked,
	.peek_len	   = tcp_peek_len,
#ifdef CONFIG_COMPAT
	.compat_ioctl	   = inet_compat_ioctl,
#endif
	.set_rcvlowat	   = tcp_set_rcvlowat,
};
EXPORT_SYMBOL(inet_stream_ops);

const struct proto_ops inet_dgram_ops = {
	.family		   = PF_INET,
	.owner		   = THIS_MODULE,
	.release	   = inet_release,
	.bind		   = inet_bind,
	.connect	   = inet_dgram_connect,
	.socketpair	   = sock_no_socketpair,
	.accept		   = sock_no_accept,
	.getname	   = inet_getname,
	.poll		   = udp_poll,
	.ioctl		   = inet_ioctl,
	.gettstamp	   = sock_gettstamp,
	.listen		   = sock_no_listen,
	.shutdown	   = inet_shutdown,
	.setsockopt	   = sock_common_setsockopt,
	.getsockopt	   = sock_common_getsockopt,
	.sendmsg	   = inet_sendmsg,
	.read_skb	   = udp_read_skb,
	.recvmsg	   = inet_recvmsg,
	.mmap		   = sock_no_mmap,
	.splice_eof	   = inet_splice_eof,
	.set_peek_off	   = sk_set_peek_off,
#ifdef CONFIG_COMPAT
	.compat_ioctl	   = inet_compat_ioctl,
#endif
};
EXPORT_SYMBOL(inet_dgram_ops);

/*
 * For SOCK_RAW sockets; should be the same as inet_dgram_ops but without
 * udp_poll
 */
static const struct proto_ops inet_sockraw_ops = {
	.family		   = PF_INET,
	.owner		   = THIS_MODULE,
	.release	   = inet_release,
	.bind		   = inet_bind,
	.connect	   = inet_dgram_connect,
	.socketpair	   = sock_no_socketpair,
	.accept		   = sock_no_accept,
	.getname	   = inet_getname,
	.poll		   = datagram_poll,
	.ioctl		   = inet_ioctl,
	.gettstamp	   = sock_gettstamp,
	.listen		   = sock_no_listen,
	.shutdown	   = inet_shutdown,
	.setsockopt	   = sock_common_setsockopt,
	.getsockopt	   = sock_common_getsockopt,
	.sendmsg	   = inet_sendmsg,
	.recvmsg	   = inet_recvmsg,
	.mmap		   = sock_no_mmap,
	.splice_eof	   = inet_splice_eof,
#ifdef CONFIG_COMPAT
	.compat_ioctl	   = inet_compat_ioctl,
#endif
};

static const struct net_proto_family inet_family_ops = {
	.family = PF_INET,
	.create = inet_create,
	.owner	= THIS_MODULE,
};

/* Upon startup we insert all the elements in inetsw_array[] into
 * the linked list inetsw.
 */
static struct inet_protosw inetsw_array[] =
{
	{
		.type =       SOCK_STREAM,
		.protocol =   IPPROTO_TCP,
		.prot =       &tcp_prot,
		.ops =        &inet_stream_ops,
		.flags =      INET_PROTOSW_PERMANENT |
			      INET_PROTOSW_ICSK,
	},

	{
		.type =       SOCK_DGRAM,
		.protocol =   IPPROTO_UDP,
		.prot =       &udp_prot,
		.ops =        &inet_dgram_ops,
		.flags =      INET_PROTOSW_PERMANENT,
       },

       {
		.type =       SOCK_DGRAM,
		.protocol =   IPPROTO_ICMP,
		.prot =       &ping_prot,
		.ops =        &inet_sockraw_ops,
		.flags =      INET_PROTOSW_REUSE,
       },

       {
	       .type =       SOCK_RAW,
	       .protocol =   IPPROTO_IP,	/* wild card */
	       .prot =       &raw_prot,
	       .ops =        &inet_sockraw_ops,
	       .flags =      INET_PROTOSW_REUSE,
       }
};

#define INETSW_ARRAY_LEN ARRAY_SIZE(inetsw_array)

void inet_register_protosw(struct inet_protosw *p)
{
	struct list_head *lh;
	struct inet_protosw *answer;
	int protocol = p->protocol;
	struct list_head *last_perm;

	spin_lock_bh(&inetsw_lock);

	if (p->type >= SOCK_MAX)
		goto out_illegal;

	/* If we are trying to override a permanent protocol, bail. */
	last_perm = &inetsw[p->type];
	list_for_each(lh, &inetsw[p->type]) {
		answer = list_entry(lh, struct inet_protosw, list);
		/* Check only the non-wild match. */
		if ((INET_PROTOSW_PERMANENT & answer->flags) == 0)
			break;
		if (protocol == answer->protocol)
			goto out_permanent;
		last_perm = lh;
	}

	/* Add the new entry after the last permanent entry if any, so that
	 * the new entry does not override a permanent entry when matched with
	 * a wild-card protocol. But it is allowed to override any existing
	 * non-permanent entry.  This means that when we remove this entry, the
	 * system automatically returns to the old behavior.
	 */
	list_add_rcu(&p->list, last_perm);
out:
	spin_unlock_bh(&inetsw_lock);

	return;

out_permanent:
	pr_err("Attempt to override permanent protocol %d\n", protocol);
	goto out;

out_illegal:
	pr_err("Ignoring attempt to register invalid socket type %d\n",
	       p->type);
	goto out;
}
EXPORT_SYMBOL(inet_register_protosw);

void inet_unregister_protosw(struct inet_protosw *p)
{
	if (INET_PROTOSW_PERMANENT & p->flags) {
		pr_err("Attempt to unregister permanent protocol %d\n",
		       p->protocol);
	} else {
		spin_lock_bh(&inetsw_lock);
		list_del_rcu(&p->list);
		spin_unlock_bh(&inetsw_lock);

		synchronize_net();
	}
}
EXPORT_SYMBOL(inet_unregister_protosw);

static int inet_sk_reselect_saddr(struct sock *sk)
{
	struct inet_sock *inet = inet_sk(sk);
	__be32 old_saddr = inet->inet_saddr;
	__be32 daddr = inet->inet_daddr;
	struct flowi4 *fl4;
	struct rtable *rt;
	__be32 new_saddr;
	struct ip_options_rcu *inet_opt;
	int err;

	inet_opt = rcu_dereference_protected(inet->inet_opt,
					     lockdep_sock_is_held(sk));
	if (inet_opt && inet_opt->opt.srr)
		daddr = inet_opt->opt.faddr;

	/* Query new route. */
	fl4 = &inet->cork.fl.u.ip4;
	rt = ip_route_connect(fl4, daddr, 0, sk->sk_bound_dev_if,
			      sk->sk_protocol, inet->inet_sport,
			      inet->inet_dport, sk);
	if (IS_ERR(rt))
		return PTR_ERR(rt);

	new_saddr = fl4->saddr;

	if (new_saddr == old_saddr) {
		sk_setup_caps(sk, &rt->dst);
		return 0;
	}

	err = inet_bhash2_update_saddr(sk, &new_saddr, AF_INET);
	if (err) {
		ip_rt_put(rt);
		return err;
	}

	sk_setup_caps(sk, &rt->dst);

	if (READ_ONCE(sock_net(sk)->ipv4.sysctl_ip_dynaddr) > 1) {
		pr_info("%s(): shifting inet->saddr from %pI4 to %pI4\n",
			__func__, &old_saddr, &new_saddr);
	}

	/*
	 * XXX The only one ugly spot where we need to
	 * XXX really change the sockets identity after
	 * XXX it has entered the hashes. -DaveM
	 *
	 * Besides that, it does not check for connection
	 * uniqueness. Wait for troubles.
	 */
	return __sk_prot_rehash(sk);
}

int inet_sk_rebuild_header(struct sock *sk)
{
	struct inet_sock *inet = inet_sk(sk);
	struct rtable *rt = (struct rtable *)__sk_dst_check(sk, 0);
	__be32 daddr;
	struct ip_options_rcu *inet_opt;
	struct flowi4 *fl4;
	int err;

	/* Route is OK, nothing to do. */
	if (rt)
		return 0;

	/* Reroute. */
	rcu_read_lock();
	inet_opt = rcu_dereference(inet->inet_opt);
	daddr = inet->inet_daddr;
	if (inet_opt && inet_opt->opt.srr)
		daddr = inet_opt->opt.faddr;
	rcu_read_unlock();
	fl4 = &inet->cork.fl.u.ip4;
	rt = ip_route_output_ports(sock_net(sk), fl4, sk, daddr, inet->inet_saddr,
				   inet->inet_dport, inet->inet_sport,
				   sk->sk_protocol, RT_CONN_FLAGS(sk),
				   sk->sk_bound_dev_if);
	if (!IS_ERR(rt)) {
		err = 0;
		sk_setup_caps(sk, &rt->dst);
	} else {
		err = PTR_ERR(rt);

		/* Routing failed... */
		sk->sk_route_caps = 0;
		/*
		 * Other protocols have to map its equivalent state to TCP_SYN_SENT.
		 * DCCP maps its DCCP_REQUESTING state to TCP_SYN_SENT. -acme
		 */
		if (!READ_ONCE(sock_net(sk)->ipv4.sysctl_ip_dynaddr) ||
		    sk->sk_state != TCP_SYN_SENT ||
		    (sk->sk_userlocks & SOCK_BINDADDR_LOCK) ||
		    (err = inet_sk_reselect_saddr(sk)) != 0)
			WRITE_ONCE(sk->sk_err_soft, -err);
	}

	return err;
}
EXPORT_SYMBOL(inet_sk_rebuild_header);

void inet_sk_set_state(struct sock *sk, int state)
{
	trace_inet_sock_set_state(sk, sk->sk_state, state);
	sk->sk_state = state;
}
EXPORT_SYMBOL(inet_sk_set_state);

void inet_sk_state_store(struct sock *sk, int newstate)
{
	trace_inet_sock_set_state(sk, sk->sk_state, newstate);
	smp_store_release(&sk->sk_state, newstate);
}

struct sk_buff *inet_gso_segment(struct sk_buff *skb,
				 netdev_features_t features)
{
	bool udpfrag = false, fixedid = false, gso_partial, encap;
	struct sk_buff *segs = ERR_PTR(-EINVAL);
	const struct net_offload *ops;
	unsigned int offset = 0;
	struct iphdr *iph;
	int proto, tot_len;
	int nhoff;
	int ihl;
	int id;

	skb_reset_network_header(skb);
	nhoff = skb_network_header(skb) - skb_mac_header(skb);
	if (unlikely(!pskb_may_pull(skb, sizeof(*iph))))
		goto out;

	iph = ip_hdr(skb);
	ihl = iph->ihl * 4;
	if (ihl < sizeof(*iph))
		goto out;

	id = ntohs(iph->id);
	proto = iph->protocol;

	/* Warning: after this point, iph might be no longer valid */
	if (unlikely(!pskb_may_pull(skb, ihl)))
		goto out;
	__skb_pull(skb, ihl);

	encap = SKB_GSO_CB(skb)->encap_level > 0;
	if (encap)
		features &= skb->dev->hw_enc_features;
	SKB_GSO_CB(skb)->encap_level += ihl;

	skb_reset_transport_header(skb);

	segs = ERR_PTR(-EPROTONOSUPPORT);

	if (!skb->encapsulation || encap) {
		udpfrag = !!(skb_shinfo(skb)->gso_type & SKB_GSO_UDP);
		fixedid = !!(skb_shinfo(skb)->gso_type & SKB_GSO_TCP_FIXEDID);

		/* fixed ID is invalid if DF bit is not set */
		if (fixedid && !(ip_hdr(skb)->frag_off & htons(IP_DF)))
			goto out;
	}

	ops = rcu_dereference(inet_offloads[proto]);
	if (likely(ops && ops->callbacks.gso_segment)) {
		segs = ops->callbacks.gso_segment(skb, features);
		if (!segs)
			skb->network_header = skb_mac_header(skb) + nhoff - skb->head;
	}

	if (IS_ERR_OR_NULL(segs))
		goto out;

	gso_partial = !!(skb_shinfo(segs)->gso_type & SKB_GSO_PARTIAL);

	skb = segs;
	do {
		iph = (struct iphdr *)(skb_mac_header(skb) + nhoff);
		if (udpfrag) {
			iph->frag_off = htons(offset >> 3);
			if (skb->next)
				iph->frag_off |= htons(IP_MF);
			offset += skb->len - nhoff - ihl;
			tot_len = skb->len - nhoff;
		} else if (skb_is_gso(skb)) {
			if (!fixedid) {
				iph->id = htons(id);
				id += skb_shinfo(skb)->gso_segs;
			}

			if (gso_partial)
				tot_len = skb_shinfo(skb)->gso_size +
					  SKB_GSO_CB(skb)->data_offset +
					  skb->head - (unsigned char *)iph;
			else
				tot_len = skb->len - nhoff;
		} else {
			if (!fixedid)
				iph->id = htons(id++);
			tot_len = skb->len - nhoff;
		}
		iph->tot_len = htons(tot_len);
		ip_send_check(iph);
		if (encap)
			skb_reset_inner_headers(skb);
		skb->network_header = (u8 *)iph - skb->head;
		skb_reset_mac_len(skb);
	} while ((skb = skb->next));

out:
	return segs;
}

static struct sk_buff *ipip_gso_segment(struct sk_buff *skb,
					netdev_features_t features)
{
	if (!(skb_shinfo(skb)->gso_type & SKB_GSO_IPXIP4))
		return ERR_PTR(-EINVAL);

	return inet_gso_segment(skb, features);
}

struct sk_buff *inet_gro_receive(struct list_head *head, struct sk_buff *skb)
{
	const struct net_offload *ops;
	struct sk_buff *pp = NULL;
	const struct iphdr *iph;
	struct sk_buff *p;
	unsigned int hlen;
	unsigned int off;
	unsigned int id;
	int flush = 1;
	int proto;

	off = skb_gro_offset(skb);
	hlen = off + sizeof(*iph);
	iph = skb_gro_header(skb, hlen, off);
	if (unlikely(!iph))
		goto out;

	proto = iph->protocol;

	ops = rcu_dereference(inet_offloads[proto]);
	if (!ops || !ops->callbacks.gro_receive)
		goto out;

	if (*(u8 *)iph != 0x45)
		goto out;

	if (ip_is_fragment(iph))
		goto out;

	if (unlikely(ip_fast_csum((u8 *)iph, 5)))
		goto out;

	NAPI_GRO_CB(skb)->proto = proto;
	id = ntohl(*(__be32 *)&iph->id);
	flush = (u16)((ntohl(*(__be32 *)iph) ^ skb_gro_len(skb)) | (id & ~IP_DF));
	id >>= 16;

	list_for_each_entry(p, head, list) {
		struct iphdr *iph2;
		u16 flush_id;

		if (!NAPI_GRO_CB(p)->same_flow)
			continue;

		iph2 = (struct iphdr *)(p->data + off);
		/* The above works because, with the exception of the top
		 * (inner most) layer, we only aggregate pkts with the same
		 * hdr length so all the hdrs we'll need to verify will start
		 * at the same offset.
		 */
		if ((iph->protocol ^ iph2->protocol) |
		    ((__force u32)iph->saddr ^ (__force u32)iph2->saddr) |
		    ((__force u32)iph->daddr ^ (__force u32)iph2->daddr)) {
			NAPI_GRO_CB(p)->same_flow = 0;
			continue;
		}

		/* All fields must match except length and checksum. */
		NAPI_GRO_CB(p)->flush |=
			(iph->ttl ^ iph2->ttl) |
			(iph->tos ^ iph2->tos) |
			((iph->frag_off ^ iph2->frag_off) & htons(IP_DF));

		NAPI_GRO_CB(p)->flush |= flush;

		/* We need to store of the IP ID check to be included later
		 * when we can verify that this packet does in fact belong
		 * to a given flow.
		 */
		flush_id = (u16)(id - ntohs(iph2->id));

		/* This bit of code makes it much easier for us to identify
		 * the cases where we are doing atomic vs non-atomic IP ID
		 * checks.  Specifically an atomic check can return IP ID
		 * values 0 - 0xFFFF, while a non-atomic check can only
		 * return 0 or 0xFFFF.
		 */
		if (!NAPI_GRO_CB(p)->is_atomic ||
		    !(iph->frag_off & htons(IP_DF))) {
			flush_id ^= NAPI_GRO_CB(p)->count;
			flush_id = flush_id ? 0xFFFF : 0;
		}

		/* If the previous IP ID value was based on an atomic
		 * datagram we can overwrite the value and ignore it.
		 */
		if (NAPI_GRO_CB(skb)->is_atomic)
			NAPI_GRO_CB(p)->flush_id = flush_id;
		else
			NAPI_GRO_CB(p)->flush_id |= flush_id;
	}

	NAPI_GRO_CB(skb)->is_atomic = !!(iph->frag_off & htons(IP_DF));
	NAPI_GRO_CB(skb)->flush |= flush;
	skb_set_network_header(skb, off);
	/* The above will be needed by the transport layer if there is one
	 * immediately following this IP hdr.
	 */

	/* Note : No need to call skb_gro_postpull_rcsum() here,
	 * as we already checked checksum over ipv4 header was 0
	 */
	skb_gro_pull(skb, sizeof(*iph));
	skb_set_transport_header(skb, skb_gro_offset(skb));

	pp = indirect_call_gro_receive(tcp4_gro_receive, udp4_gro_receive,
				       ops->callbacks.gro_receive, head, skb);

out:
	skb_gro_flush_final(skb, pp, flush);

	return pp;
}

static struct sk_buff *ipip_gro_receive(struct list_head *head,
					struct sk_buff *skb)
{
	if (NAPI_GRO_CB(skb)->encap_mark) {
		NAPI_GRO_CB(skb)->flush = 1;
		return NULL;
	}

	NAPI_GRO_CB(skb)->encap_mark = 1;

	return inet_gro_receive(head, skb);
}

#define SECONDS_PER_DAY	86400

/* inet_current_timestamp - Return IP network timestamp
 *
 * Return milliseconds since midnight in network byte order.
 */
__be32 inet_current_timestamp(void)
{
	u32 secs;
	u32 msecs;
	struct timespec64 ts;

	ktime_get_real_ts64(&ts);

	/* Get secs since midnight. */
	(void)div_u64_rem(ts.tv_sec, SECONDS_PER_DAY, &secs);
	/* Convert to msecs. */
	msecs = secs * MSEC_PER_SEC;
	/* Convert nsec to msec. */
	msecs += (u32)ts.tv_nsec / NSEC_PER_MSEC;

	/* Convert to network byte order. */
	return htonl(msecs);
}
EXPORT_SYMBOL(inet_current_timestamp);

int inet_recv_error(struct sock *sk, struct msghdr *msg, int len, int *addr_len)
{
	if (sk->sk_family == AF_INET)
		return ip_recv_error(sk, msg, len, addr_len);
#if IS_ENABLED(CONFIG_IPV6)
	if (sk->sk_family == AF_INET6)
		return pingv6_ops.ipv6_recv_error(sk, msg, len, addr_len);
#endif
	return -EINVAL;
}
EXPORT_SYMBOL(inet_recv_error);

int inet_gro_complete(struct sk_buff *skb, int nhoff)
{
	struct iphdr *iph = (struct iphdr *)(skb->data + nhoff);
	const struct net_offload *ops;
	__be16 totlen = iph->tot_len;
	int proto = iph->protocol;
	int err = -ENOSYS;

	if (skb->encapsulation) {
		skb_set_inner_protocol(skb, cpu_to_be16(ETH_P_IP));
		skb_set_inner_network_header(skb, nhoff);
	}

	iph_set_totlen(iph, skb->len - nhoff);
	csum_replace2(&iph->check, totlen, iph->tot_len);

	ops = rcu_dereference(inet_offloads[proto]);
	if (WARN_ON(!ops || !ops->callbacks.gro_complete))
		goto out;

	/* Only need to add sizeof(*iph) to get to the next hdr below
	 * because any hdr with option will have been flushed in
	 * inet_gro_receive().
	 */
	err = INDIRECT_CALL_2(ops->callbacks.gro_complete,
			      tcp4_gro_complete, udp4_gro_complete,
			      skb, nhoff + sizeof(*iph));

out:
	return err;
}

static int ipip_gro_complete(struct sk_buff *skb, int nhoff)
{
	skb->encapsulation = 1;
	skb_shinfo(skb)->gso_type |= SKB_GSO_IPXIP4;
	return inet_gro_complete(skb, nhoff);
}

int inet_ctl_sock_create(struct sock **sk, unsigned short family,
			 unsigned short type, unsigned char protocol,
			 struct net *net)
{
	struct socket *sock;
	int rc = sock_create_kern(net, family, type, protocol, &sock);

	if (rc == 0) {
		*sk = sock->sk;
		(*sk)->sk_allocation = GFP_ATOMIC;
		(*sk)->sk_use_task_frag = false;
		/*
		 * Unhash it so that IP input processing does not even see it,
		 * we do not wish this socket to see incoming packets.
		 */
		(*sk)->sk_prot->unhash(*sk);
	}
	return rc;
}
EXPORT_SYMBOL_GPL(inet_ctl_sock_create);

unsigned long snmp_fold_field(void __percpu *mib, int offt)
{
	unsigned long res = 0;
	int i;

	for_each_possible_cpu(i)
		res += snmp_get_cpu_field(mib, i, offt);
	return res;
}
EXPORT_SYMBOL_GPL(snmp_fold_field);

#if BITS_PER_LONG==32

u64 snmp_get_cpu_field64(void __percpu *mib, int cpu, int offt,
			 size_t syncp_offset)
{
	void *bhptr;
	struct u64_stats_sync *syncp;
	u64 v;
	unsigned int start;

	bhptr = per_cpu_ptr(mib, cpu);
	syncp = (struct u64_stats_sync *)(bhptr + syncp_offset);
	do {
		start = u64_stats_fetch_begin(syncp);
		v = *(((u64 *)bhptr) + offt);
	} while (u64_stats_fetch_retry(syncp, start));

	return v;
}
EXPORT_SYMBOL_GPL(snmp_get_cpu_field64);

u64 snmp_fold_field64(void __percpu *mib, int offt, size_t syncp_offset)
{
	u64 res = 0;
	int cpu;

	for_each_possible_cpu(cpu) {
		res += snmp_get_cpu_field64(mib, cpu, offt, syncp_offset);
	}
	return res;
}
EXPORT_SYMBOL_GPL(snmp_fold_field64);
#endif

#ifdef CONFIG_IP_MULTICAST
static const struct net_protocol igmp_protocol = {
	.handler =	igmp_rcv,
};
#endif

static const struct net_protocol tcp_protocol = {
	.handler	=	tcp_v4_rcv,
	.err_handler	=	tcp_v4_err,
	.no_policy	=	1,
	.icmp_strict_tag_validation = 1,
};

static const struct net_protocol udp_protocol = {
	.handler =	udp_rcv,
	.err_handler =	udp_err,
	.no_policy =	1,
};

static const struct net_protocol icmp_protocol = {
	.handler =	icmp_rcv,
	.err_handler =	icmp_err,
	.no_policy =	1,
};

static __net_init int ipv4_mib_init_net(struct net *net)
{
	int i;

	net->mib.tcp_statistics = alloc_percpu(struct tcp_mib);
	if (!net->mib.tcp_statistics)
		goto err_tcp_mib;
	net->mib.ip_statistics = alloc_percpu(struct ipstats_mib);
	if (!net->mib.ip_statistics)
		goto err_ip_mib;

	for_each_possible_cpu(i) {
		struct ipstats_mib *af_inet_stats;
		af_inet_stats = per_cpu_ptr(net->mib.ip_statistics, i);
		u64_stats_init(&af_inet_stats->syncp);
	}

	net->mib.net_statistics = alloc_percpu(struct linux_mib);
	if (!net->mib.net_statistics)
		goto err_net_mib;
	net->mib.udp_statistics = alloc_percpu(struct udp_mib);
	if (!net->mib.udp_statistics)
		goto err_udp_mib;
	net->mib.udplite_statistics = alloc_percpu(struct udp_mib);
	if (!net->mib.udplite_statistics)
		goto err_udplite_mib;
	net->mib.icmp_statistics = alloc_percpu(struct icmp_mib);
	if (!net->mib.icmp_statistics)
		goto err_icmp_mib;
	net->mib.icmpmsg_statistics = kzalloc(sizeof(struct icmpmsg_mib),
					      GFP_KERNEL);
	if (!net->mib.icmpmsg_statistics)
		goto err_icmpmsg_mib;

	tcp_mib_init(net);
	return 0;

err_icmpmsg_mib:
	free_percpu(net->mib.icmp_statistics);
err_icmp_mib:
	free_percpu(net->mib.udplite_statistics);
err_udplite_mib:
	free_percpu(net->mib.udp_statistics);
err_udp_mib:
	free_percpu(net->mib.net_statistics);
err_net_mib:
	free_percpu(net->mib.ip_statistics);
err_ip_mib:
	free_percpu(net->mib.tcp_statistics);
err_tcp_mib:
	return -ENOMEM;
}

static __net_exit void ipv4_mib_exit_net(struct net *net)
{
	kfree(net->mib.icmpmsg_statistics);
	free_percpu(net->mib.icmp_statistics);
	free_percpu(net->mib.udplite_statistics);
	free_percpu(net->mib.udp_statistics);
	free_percpu(net->mib.net_statistics);
	free_percpu(net->mib.ip_statistics);
	free_percpu(net->mib.tcp_statistics);
#ifdef CONFIG_MPTCP
	/* allocated on demand, see mptcp_init_sock() */
	free_percpu(net->mib.mptcp_statistics);
#endif
}

static __net_initdata struct pernet_operations ipv4_mib_ops = {
	.init = ipv4_mib_init_net,
	.exit = ipv4_mib_exit_net,
};

static int __init init_ipv4_mibs(void)
{
	return register_pernet_subsys(&ipv4_mib_ops);
}

static __net_init int inet_init_net(struct net *net)
{
	/*
	 * Set defaults for local port range
	 */
	net->ipv4.ip_local_ports.range = 60999u << 16 | 32768u;

	seqlock_init(&net->ipv4.ping_group_range.lock);
	/*
	 * Sane defaults - nobody may create ping sockets.
	 * Boot scripts should set this to distro-specific group.
	 */
	net->ipv4.ping_group_range.range[0] = make_kgid(&init_user_ns, 1);
	net->ipv4.ping_group_range.range[1] = make_kgid(&init_user_ns, 0);

	/* Default values for sysctl-controlled parameters.
	 * We set them here, in case sysctl is not compiled.
	 */
	net->ipv4.sysctl_ip_default_ttl = IPDEFTTL;
	net->ipv4.sysctl_ip_fwd_update_priority = 1;
	net->ipv4.sysctl_ip_dynaddr = 0;
	net->ipv4.sysctl_ip_early_demux = 1;
	net->ipv4.sysctl_udp_early_demux = 1;
	net->ipv4.sysctl_tcp_early_demux = 1;
	net->ipv4.sysctl_nexthop_compat_mode = 1;
#ifdef CONFIG_SYSCTL
	net->ipv4.sysctl_ip_prot_sock = PROT_SOCK;
#endif

	/* Some igmp sysctl, whose values are always used */
	net->ipv4.sysctl_igmp_max_memberships = 20;
	net->ipv4.sysctl_igmp_max_msf = 10;
	/* IGMP reports for link-local multicast groups are enabled by default */
	net->ipv4.sysctl_igmp_llm_reports = 1;
	net->ipv4.sysctl_igmp_qrv = 2;

	net->ipv4.sysctl_fib_notify_on_flag_change = 0;

	return 0;
}

static __net_initdata struct pernet_operations af_inet_ops = {
	.init = inet_init_net,
};

static int __init init_inet_pernet_ops(void)
{
	return register_pernet_subsys(&af_inet_ops);
}

static int ipv4_proc_init(void);

/*
 *	IP protocol layer initialiser
 */

static struct packet_offload ip_packet_offload __read_mostly = {
	.type = cpu_to_be16(ETH_P_IP),
	.callbacks = {
		.gso_segment = inet_gso_segment,
		.gro_receive = inet_gro_receive,
		.gro_complete = inet_gro_complete,
	},
};

static const struct net_offload ipip_offload = {
	.callbacks = {
		.gso_segment	= ipip_gso_segment,
		.gro_receive	= ipip_gro_receive,
		.gro_complete	= ipip_gro_complete,
	},
};

static int __init ipip_offload_init(void)
{
	return inet_add_offload(&ipip_offload, IPPROTO_IPIP);
}

static int __init ipv4_offload_init(void)
{
	/*
	 * Add offloads
	 */
	if (udpv4_offload_init() < 0)
		pr_crit("%s: Cannot add UDP protocol offload\n", __func__);
	if (tcpv4_offload_init() < 0)
		pr_crit("%s: Cannot add TCP protocol offload\n", __func__);
	if (ipip_offload_init() < 0)
		pr_crit("%s: Cannot add IPIP protocol offload\n", __func__);

	dev_add_offload(&ip_packet_offload);
	return 0;
}

fs_initcall(ipv4_offload_init);

static struct packet_type ip_packet_type __read_mostly = {
	.type = cpu_to_be16(ETH_P_IP),
	.func = ip_rcv,
	.list_func = ip_list_rcv,
};

static int __init inet_init(void)
{
	struct inet_protosw *q;
	struct list_head *r;
	int rc;

	sock_skb_cb_check_size(sizeof(struct inet_skb_parm));

	raw_hashinfo_init(&raw_v4_hashinfo);

	rc = proto_register(&tcp_prot, 1);
	if (rc)
		goto out;

	rc = proto_register(&udp_prot, 1);
	if (rc)
		goto out_unregister_tcp_proto;

	rc = proto_register(&raw_prot, 1);
	if (rc)
		goto out_unregister_udp_proto;

	rc = proto_register(&ping_prot, 1);
	if (rc)
		goto out_unregister_raw_proto;

	/*
	 *	Tell SOCKET that we are alive...
	 */

	(void)sock_register(&inet_family_ops);

#ifdef CONFIG_SYSCTL
	ip_static_sysctl_init();
#endif

	/*
	 *	Add all the base protocols.
	 */

	if (inet_add_protocol(&icmp_protocol, IPPROTO_ICMP) < 0)
		pr_crit("%s: Cannot add ICMP protocol\n", __func__);
	if (inet_add_protocol(&udp_protocol, IPPROTO_UDP) < 0)
		pr_crit("%s: Cannot add UDP protocol\n", __func__);
	if (inet_add_protocol(&tcp_protocol, IPPROTO_TCP) < 0)
		pr_crit("%s: Cannot add TCP protocol\n", __func__);
#ifdef CONFIG_IP_MULTICAST
	if (inet_add_protocol(&igmp_protocol, IPPROTO_IGMP) < 0)
		pr_crit("%s: Cannot add IGMP protocol\n", __func__);
#endif

	/* Register the socket-side information for inet_create. */
	for (r = &inetsw[0]; r < &inetsw[SOCK_MAX]; ++r)
		INIT_LIST_HEAD(r);

	for (q = inetsw_array; q < &inetsw_array[INETSW_ARRAY_LEN]; ++q)
		inet_register_protosw(q);

	/*
	 *	Set the ARP module up
	 */

	arp_init();

	/*
	 *	Set the IP module up
	 */

	ip_init();

	/* Initialise per-cpu ipv4 mibs */
	if (init_ipv4_mibs())
		panic("%s: Cannot init ipv4 mibs\n", __func__);

	/* Setup TCP slab cache for open requests. */
	tcp_init();

	/* Setup UDP memory threshold */
	udp_init();

	/* Add UDP-Lite (RFC 3828) */
	udplite4_register();

	raw_init();

	ping_init();

	/*
	 *	Set the ICMP layer up
	 */

	if (icmp_init() < 0)
		panic("Failed to create the ICMP control socket.\n");

	/*
	 *	Initialise the multicast router
	 */
#if defined(CONFIG_IP_MROUTE)
	if (ip_mr_init())
		pr_crit("%s: Cannot init ipv4 mroute\n", __func__);
#endif

	if (init_inet_pernet_ops())
		pr_crit("%s: Cannot init ipv4 inet pernet ops\n", __func__);

	ipv4_proc_init();

	ipfrag_init();

	dev_add_pack(&ip_packet_type);

	ip_tunnel_core_init();

	rc = 0;
out:
	return rc;
out_unregister_raw_proto:
	proto_unregister(&raw_prot);
out_unregister_udp_proto:
	proto_unregister(&udp_prot);
out_unregister_tcp_proto:
	proto_unregister(&tcp_prot);
	goto out;
}

fs_initcall(inet_init);

/* ------------------------------------------------------------------------ */

#ifdef CONFIG_PROC_FS
static int __init ipv4_proc_init(void)
{
	int rc = 0;

	if (raw_proc_init())
		goto out_raw;
	if (tcp4_proc_init())
		goto out_tcp;
	if (udp4_proc_init())
		goto out_udp;
	if (ping_proc_init())
		goto out_ping;
	if (ip_misc_proc_init())
		goto out_misc;
out:
	return rc;
out_misc:
	ping_proc_exit();
out_ping:
	udp4_proc_exit();
out_udp:
	tcp4_proc_exit();
out_tcp:
	raw_proc_exit();
out_raw:
	rc = -ENOMEM;
	goto out;
}

#else /* CONFIG_PROC_FS */
static int __init ipv4_proc_init(void)
{
	return 0;
}
#endif /* CONFIG_PROC_FS */
