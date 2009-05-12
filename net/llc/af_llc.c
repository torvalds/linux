/*
 * af_llc.c - LLC User Interface SAPs
 * Description:
 *   Functions in this module are implementation of socket based llc
 *   communications for the Linux operating system. Support of llc class
 *   one and class two is provided via SOCK_DGRAM and SOCK_STREAM
 *   respectively.
 *
 *   An llc2 connection is (mac + sap), only one llc2 sap connection
 *   is allowed per mac. Though one sap may have multiple mac + sap
 *   connections.
 *
 * Copyright (c) 2001 by Jay Schulist <jschlst@samba.org>
 *		 2002-2003 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * This program can be redistributed or modified under the terms of the
 * GNU General Public License as published by the Free Software Foundation.
 * This program is distributed without any warranty or implied warranty
 * of merchantability or fitness for a particular purpose.
 *
 * See the GNU General Public License for more details.
 */
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rtnetlink.h>
#include <linux/init.h>
#include <net/llc.h>
#include <net/llc_sap.h>
#include <net/llc_pdu.h>
#include <net/llc_conn.h>
#include <net/tcp_states.h>

/* remember: uninitialized global data is zeroed because its in .bss */
static u16 llc_ui_sap_last_autoport = LLC_SAP_DYN_START;
static u16 llc_ui_sap_link_no_max[256];
static struct sockaddr_llc llc_ui_addrnull;
static const struct proto_ops llc_ui_ops;

static int llc_ui_wait_for_conn(struct sock *sk, long timeout);
static int llc_ui_wait_for_disc(struct sock *sk, long timeout);
static int llc_ui_wait_for_busy_core(struct sock *sk, long timeout);

#if 0
#define dprintk(args...) printk(KERN_DEBUG args)
#else
#define dprintk(args...)
#endif

/**
 *	llc_ui_next_link_no - return the next unused link number for a sap
 *	@sap: Address of sap to get link number from.
 *
 *	Return the next unused link number for a given sap.
 */
static inline u16 llc_ui_next_link_no(int sap)
{
	return llc_ui_sap_link_no_max[sap]++;
}

/**
 *	llc_proto_type - return eth protocol for ARP header type
 *	@arphrd: ARP header type.
 *
 *	Given an ARP header type return the corresponding ethernet protocol.
 */
static inline __be16 llc_proto_type(u16 arphrd)
{
	return arphrd == ARPHRD_IEEE802_TR ?
			 htons(ETH_P_TR_802_2) : htons(ETH_P_802_2);
}

/**
 *	llc_ui_addr_null - determines if a address structure is null
 *	@addr: Address to test if null.
 */
static inline u8 llc_ui_addr_null(struct sockaddr_llc *addr)
{
	return !memcmp(addr, &llc_ui_addrnull, sizeof(*addr));
}

/**
 *	llc_ui_header_len - return length of llc header based on operation
 *	@sk: Socket which contains a valid llc socket type.
 *	@addr: Complete sockaddr_llc structure received from the user.
 *
 *	Provide the length of the llc header depending on what kind of
 *	operation the user would like to perform and the type of socket.
 *	Returns the correct llc header length.
 */
static inline u8 llc_ui_header_len(struct sock *sk, struct sockaddr_llc *addr)
{
	u8 rc = LLC_PDU_LEN_U;

	if (addr->sllc_test || addr->sllc_xid)
		rc = LLC_PDU_LEN_U;
	else if (sk->sk_type == SOCK_STREAM)
		rc = LLC_PDU_LEN_I;
	return rc;
}

/**
 *	llc_ui_send_data - send data via reliable llc2 connection
 *	@sk: Connection the socket is using.
 *	@skb: Data the user wishes to send.
 *	@noblock: can we block waiting for data?
 *
 *	Send data via reliable llc2 connection.
 *	Returns 0 upon success, non-zero if action did not succeed.
 */
static int llc_ui_send_data(struct sock* sk, struct sk_buff *skb, int noblock)
{
	struct llc_sock* llc = llc_sk(sk);
	int rc = 0;

	if (unlikely(llc_data_accept_state(llc->state) ||
		     llc->remote_busy_flag ||
		     llc->p_flag)) {
		long timeout = sock_sndtimeo(sk, noblock);

		rc = llc_ui_wait_for_busy_core(sk, timeout);
	}
	if (unlikely(!rc))
		rc = llc_build_and_send_pkt(sk, skb);
	return rc;
}

static void llc_ui_sk_init(struct socket *sock, struct sock *sk)
{
	sock_graft(sk, sock);
	sk->sk_type	= sock->type;
	sock->ops	= &llc_ui_ops;
}

static struct proto llc_proto = {
	.name	  = "LLC",
	.owner	  = THIS_MODULE,
	.obj_size = sizeof(struct llc_sock),
};

/**
 *	llc_ui_create - alloc and init a new llc_ui socket
 *	@sock: Socket to initialize and attach allocated sk to.
 *	@protocol: Unused.
 *
 *	Allocate and initialize a new llc_ui socket, validate the user wants a
 *	socket type we have available.
 *	Returns 0 upon success, negative upon failure.
 */
static int llc_ui_create(struct net *net, struct socket *sock, int protocol)
{
	struct sock *sk;
	int rc = -ESOCKTNOSUPPORT;

	if (!capable(CAP_NET_RAW))
		return -EPERM;

	if (net != &init_net)
		return -EAFNOSUPPORT;

	if (likely(sock->type == SOCK_DGRAM || sock->type == SOCK_STREAM)) {
		rc = -ENOMEM;
		sk = llc_sk_alloc(net, PF_LLC, GFP_KERNEL, &llc_proto);
		if (sk) {
			rc = 0;
			llc_ui_sk_init(sock, sk);
		}
	}
	return rc;
}

/**
 *	llc_ui_release - shutdown socket
 *	@sock: Socket to release.
 *
 *	Shutdown and deallocate an existing socket.
 */
static int llc_ui_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct llc_sock *llc;

	if (unlikely(sk == NULL))
		goto out;
	sock_hold(sk);
	lock_sock(sk);
	llc = llc_sk(sk);
	dprintk("%s: closing local(%02X) remote(%02X)\n", __func__,
		llc->laddr.lsap, llc->daddr.lsap);
	if (!llc_send_disc(sk))
		llc_ui_wait_for_disc(sk, sk->sk_rcvtimeo);
	if (!sock_flag(sk, SOCK_ZAPPED)) {
		llc_sap_put(llc->sap);
		llc_sap_remove_socket(llc->sap, sk);
	}
	release_sock(sk);
	if (llc->dev)
		dev_put(llc->dev);
	sock_put(sk);
	llc_sk_free(sk);
out:
	return 0;
}

/**
 *	llc_ui_autoport - provide dynamically allocate SAP number
 *
 *	Provide the caller with a dynamically allocated SAP number according
 *	to the rules that are set in this function. Returns: 0, upon failure,
 *	SAP number otherwise.
 */
static int llc_ui_autoport(void)
{
	struct llc_sap *sap;
	int i, tries = 0;

	while (tries < LLC_SAP_DYN_TRIES) {
		for (i = llc_ui_sap_last_autoport;
		     i < LLC_SAP_DYN_STOP; i += 2) {
			sap = llc_sap_find(i);
			if (!sap) {
				llc_ui_sap_last_autoport = i + 2;
				goto out;
			}
			llc_sap_put(sap);
		}
		llc_ui_sap_last_autoport = LLC_SAP_DYN_START;
		tries++;
	}
	i = 0;
out:
	return i;
}

/**
 *	llc_ui_autobind - automatically bind a socket to a sap
 *	@sock: socket to bind
 *	@addr: address to connect to
 *
 * 	Used by llc_ui_connect and llc_ui_sendmsg when the user hasn't
 * 	specifically used llc_ui_bind to bind to an specific address/sap
 *
 *	Returns: 0 upon success, negative otherwise.
 */
static int llc_ui_autobind(struct socket *sock, struct sockaddr_llc *addr)
{
	struct sock *sk = sock->sk;
	struct llc_sock *llc = llc_sk(sk);
	struct llc_sap *sap;
	int rc = -EINVAL;

	if (!sock_flag(sk, SOCK_ZAPPED))
		goto out;
	rc = -ENODEV;
	llc->dev = dev_getfirstbyhwtype(&init_net, addr->sllc_arphrd);
	if (!llc->dev)
		goto out;
	rc = -EUSERS;
	llc->laddr.lsap = llc_ui_autoport();
	if (!llc->laddr.lsap)
		goto out;
	rc = -EBUSY; /* some other network layer is using the sap */
	sap = llc_sap_open(llc->laddr.lsap, NULL);
	if (!sap)
		goto out;
	memcpy(llc->laddr.mac, llc->dev->dev_addr, IFHWADDRLEN);
	memcpy(&llc->addr, addr, sizeof(llc->addr));
	/* assign new connection to its SAP */
	llc_sap_add_socket(sap, sk);
	sock_reset_flag(sk, SOCK_ZAPPED);
	rc = 0;
out:
	return rc;
}

/**
 *	llc_ui_bind - bind a socket to a specific address.
 *	@sock: Socket to bind an address to.
 *	@uaddr: Address the user wants the socket bound to.
 *	@addrlen: Length of the uaddr structure.
 *
 *	Bind a socket to a specific address. For llc a user is able to bind to
 *	a specific sap only or mac + sap.
 *	If the user desires to bind to a specific mac + sap, it is possible to
 *	have multiple sap connections via multiple macs.
 *	Bind and autobind for that matter must enforce the correct sap usage
 *	otherwise all hell will break loose.
 *	Returns: 0 upon success, negative otherwise.
 */
static int llc_ui_bind(struct socket *sock, struct sockaddr *uaddr, int addrlen)
{
	struct sockaddr_llc *addr = (struct sockaddr_llc *)uaddr;
	struct sock *sk = sock->sk;
	struct llc_sock *llc = llc_sk(sk);
	struct llc_sap *sap;
	int rc = -EINVAL;

	dprintk("%s: binding %02X\n", __func__, addr->sllc_sap);
	if (unlikely(!sock_flag(sk, SOCK_ZAPPED) || addrlen != sizeof(*addr)))
		goto out;
	rc = -EAFNOSUPPORT;
	if (unlikely(addr->sllc_family != AF_LLC))
		goto out;
	rc = -ENODEV;
	rtnl_lock();
	llc->dev = dev_getbyhwaddr(&init_net, addr->sllc_arphrd, addr->sllc_mac);
	rtnl_unlock();
	if (!llc->dev)
		goto out;
	if (!addr->sllc_sap) {
		rc = -EUSERS;
		addr->sllc_sap = llc_ui_autoport();
		if (!addr->sllc_sap)
			goto out;
	}
	sap = llc_sap_find(addr->sllc_sap);
	if (!sap) {
		sap = llc_sap_open(addr->sllc_sap, NULL);
		rc = -EBUSY; /* some other network layer is using the sap */
		if (!sap)
			goto out;
		llc_sap_hold(sap);
	} else {
		struct llc_addr laddr, daddr;
		struct sock *ask;

		memset(&laddr, 0, sizeof(laddr));
		memset(&daddr, 0, sizeof(daddr));
		/*
		 * FIXME: check if the address is multicast,
		 * 	  only SOCK_DGRAM can do this.
		 */
		memcpy(laddr.mac, addr->sllc_mac, IFHWADDRLEN);
		laddr.lsap = addr->sllc_sap;
		rc = -EADDRINUSE; /* mac + sap clash. */
		ask = llc_lookup_established(sap, &daddr, &laddr);
		if (ask) {
			sock_put(ask);
			goto out_put;
		}
	}
	llc->laddr.lsap = addr->sllc_sap;
	memcpy(llc->laddr.mac, addr->sllc_mac, IFHWADDRLEN);
	memcpy(&llc->addr, addr, sizeof(llc->addr));
	/* assign new connection to its SAP */
	llc_sap_add_socket(sap, sk);
	sock_reset_flag(sk, SOCK_ZAPPED);
	rc = 0;
out_put:
	llc_sap_put(sap);
out:
	return rc;
}

/**
 *	llc_ui_shutdown - shutdown a connect llc2 socket.
 *	@sock: Socket to shutdown.
 *	@how: What part of the socket to shutdown.
 *
 *	Shutdown a connected llc2 socket. Currently this function only supports
 *	shutting down both sends and receives (2), we could probably make this
 *	function such that a user can shutdown only half the connection but not
 *	right now.
 *	Returns: 0 upon success, negative otherwise.
 */
static int llc_ui_shutdown(struct socket *sock, int how)
{
	struct sock *sk = sock->sk;
	int rc = -ENOTCONN;

	lock_sock(sk);
	if (unlikely(sk->sk_state != TCP_ESTABLISHED))
		goto out;
	rc = -EINVAL;
	if (how != 2)
		goto out;
	rc = llc_send_disc(sk);
	if (!rc)
		rc = llc_ui_wait_for_disc(sk, sk->sk_rcvtimeo);
	/* Wake up anyone sleeping in poll */
	sk->sk_state_change(sk);
out:
	release_sock(sk);
	return rc;
}

/**
 *	llc_ui_connect - Connect to a remote llc2 mac + sap.
 *	@sock: Socket which will be connected to the remote destination.
 *	@uaddr: Remote and possibly the local address of the new connection.
 *	@addrlen: Size of uaddr structure.
 *	@flags: Operational flags specified by the user.
 *
 *	Connect to a remote llc2 mac + sap. The caller must specify the
 *	destination mac and address to connect to. If the user hasn't previously
 *	called bind(2) with a smac the address of the first interface of the
 *	specified arp type will be used.
 *	This function will autobind if user did not previously call bind.
 *	Returns: 0 upon success, negative otherwise.
 */
static int llc_ui_connect(struct socket *sock, struct sockaddr *uaddr,
			  int addrlen, int flags)
{
	struct sock *sk = sock->sk;
	struct llc_sock *llc = llc_sk(sk);
	struct sockaddr_llc *addr = (struct sockaddr_llc *)uaddr;
	int rc = -EINVAL;

	lock_sock(sk);
	if (unlikely(addrlen != sizeof(*addr)))
		goto out;
	rc = -EAFNOSUPPORT;
	if (unlikely(addr->sllc_family != AF_LLC))
		goto out;
	if (unlikely(sk->sk_type != SOCK_STREAM))
		goto out;
	rc = -EALREADY;
	if (unlikely(sock->state == SS_CONNECTING))
		goto out;
	/* bind connection to sap if user hasn't done it. */
	if (sock_flag(sk, SOCK_ZAPPED)) {
		/* bind to sap with null dev, exclusive */
		rc = llc_ui_autobind(sock, addr);
		if (rc)
			goto out;
	}
	llc->daddr.lsap = addr->sllc_sap;
	memcpy(llc->daddr.mac, addr->sllc_mac, IFHWADDRLEN);
	sock->state = SS_CONNECTING;
	sk->sk_state   = TCP_SYN_SENT;
	llc->link   = llc_ui_next_link_no(llc->sap->laddr.lsap);
	rc = llc_establish_connection(sk, llc->dev->dev_addr,
				      addr->sllc_mac, addr->sllc_sap);
	if (rc) {
		dprintk("%s: llc_ui_send_conn failed :-(\n", __func__);
		sock->state  = SS_UNCONNECTED;
		sk->sk_state = TCP_CLOSE;
		goto out;
	}

	if (sk->sk_state == TCP_SYN_SENT) {
		const long timeo = sock_sndtimeo(sk, flags & O_NONBLOCK);

		if (!timeo || !llc_ui_wait_for_conn(sk, timeo))
			goto out;

		rc = sock_intr_errno(timeo);
		if (signal_pending(current))
			goto out;
	}

	if (sk->sk_state == TCP_CLOSE)
		goto sock_error;

	sock->state = SS_CONNECTED;
	rc = 0;
out:
	release_sock(sk);
	return rc;
sock_error:
	rc = sock_error(sk) ? : -ECONNABORTED;
	sock->state = SS_UNCONNECTED;
	goto out;
}

/**
 *	llc_ui_listen - allow a normal socket to accept incoming connections
 *	@sock: Socket to allow incoming connections on.
 *	@backlog: Number of connections to queue.
 *
 *	Allow a normal socket to accept incoming connections.
 *	Returns 0 upon success, negative otherwise.
 */
static int llc_ui_listen(struct socket *sock, int backlog)
{
	struct sock *sk = sock->sk;
	int rc = -EINVAL;

	lock_sock(sk);
	if (unlikely(sock->state != SS_UNCONNECTED))
		goto out;
	rc = -EOPNOTSUPP;
	if (unlikely(sk->sk_type != SOCK_STREAM))
		goto out;
	rc = -EAGAIN;
	if (sock_flag(sk, SOCK_ZAPPED))
		goto out;
	rc = 0;
	if (!(unsigned)backlog)	/* BSDism */
		backlog = 1;
	sk->sk_max_ack_backlog = backlog;
	if (sk->sk_state != TCP_LISTEN) {
		sk->sk_ack_backlog = 0;
		sk->sk_state	   = TCP_LISTEN;
	}
	sk->sk_socket->flags |= __SO_ACCEPTCON;
out:
	release_sock(sk);
	return rc;
}

static int llc_ui_wait_for_disc(struct sock *sk, long timeout)
{
	DEFINE_WAIT(wait);
	int rc = 0;

	while (1) {
		prepare_to_wait(sk->sk_sleep, &wait, TASK_INTERRUPTIBLE);
		if (sk_wait_event(sk, &timeout, sk->sk_state == TCP_CLOSE))
			break;
		rc = -ERESTARTSYS;
		if (signal_pending(current))
			break;
		rc = -EAGAIN;
		if (!timeout)
			break;
		rc = 0;
	}
	finish_wait(sk->sk_sleep, &wait);
	return rc;
}

static int llc_ui_wait_for_conn(struct sock *sk, long timeout)
{
	DEFINE_WAIT(wait);

	while (1) {
		prepare_to_wait(sk->sk_sleep, &wait, TASK_INTERRUPTIBLE);
		if (sk_wait_event(sk, &timeout, sk->sk_state != TCP_SYN_SENT))
			break;
		if (signal_pending(current) || !timeout)
			break;
	}
	finish_wait(sk->sk_sleep, &wait);
	return timeout;
}

static int llc_ui_wait_for_busy_core(struct sock *sk, long timeout)
{
	DEFINE_WAIT(wait);
	struct llc_sock *llc = llc_sk(sk);
	int rc;

	while (1) {
		prepare_to_wait(sk->sk_sleep, &wait, TASK_INTERRUPTIBLE);
		rc = 0;
		if (sk_wait_event(sk, &timeout,
				  (sk->sk_shutdown & RCV_SHUTDOWN) ||
				  (!llc_data_accept_state(llc->state) &&
				   !llc->remote_busy_flag &&
				   !llc->p_flag)))
			break;
		rc = -ERESTARTSYS;
		if (signal_pending(current))
			break;
		rc = -EAGAIN;
		if (!timeout)
			break;
	}
	finish_wait(sk->sk_sleep, &wait);
	return rc;
}

static int llc_wait_data(struct sock *sk, long timeo)
{
	int rc;

	while (1) {
		/*
		 * POSIX 1003.1g mandates this order.
		 */
		rc = sock_error(sk);
		if (rc)
			break;
		rc = 0;
		if (sk->sk_shutdown & RCV_SHUTDOWN)
			break;
		rc = -EAGAIN;
		if (!timeo)
			break;
		rc = sock_intr_errno(timeo);
		if (signal_pending(current))
			break;
		rc = 0;
		if (sk_wait_data(sk, &timeo))
			break;
	}
	return rc;
}

/**
 *	llc_ui_accept - accept a new incoming connection.
 *	@sock: Socket which connections arrive on.
 *	@newsock: Socket to move incoming connection to.
 *	@flags: User specified operational flags.
 *
 *	Accept a new incoming connection.
 *	Returns 0 upon success, negative otherwise.
 */
static int llc_ui_accept(struct socket *sock, struct socket *newsock, int flags)
{
	struct sock *sk = sock->sk, *newsk;
	struct llc_sock *llc, *newllc;
	struct sk_buff *skb;
	int rc = -EOPNOTSUPP;

	dprintk("%s: accepting on %02X\n", __func__,
		llc_sk(sk)->laddr.lsap);
	lock_sock(sk);
	if (unlikely(sk->sk_type != SOCK_STREAM))
		goto out;
	rc = -EINVAL;
	if (unlikely(sock->state != SS_UNCONNECTED ||
		     sk->sk_state != TCP_LISTEN))
		goto out;
	/* wait for a connection to arrive. */
	if (skb_queue_empty(&sk->sk_receive_queue)) {
		rc = llc_wait_data(sk, sk->sk_rcvtimeo);
		if (rc)
			goto out;
	}
	dprintk("%s: got a new connection on %02X\n", __func__,
		llc_sk(sk)->laddr.lsap);
	skb = skb_dequeue(&sk->sk_receive_queue);
	rc = -EINVAL;
	if (!skb->sk)
		goto frees;
	rc = 0;
	newsk = skb->sk;
	/* attach connection to a new socket. */
	llc_ui_sk_init(newsock, newsk);
	sock_reset_flag(newsk, SOCK_ZAPPED);
	newsk->sk_state		= TCP_ESTABLISHED;
	newsock->state		= SS_CONNECTED;
	llc			= llc_sk(sk);
	newllc			= llc_sk(newsk);
	memcpy(&newllc->addr, &llc->addr, sizeof(newllc->addr));
	newllc->link = llc_ui_next_link_no(newllc->laddr.lsap);

	/* put original socket back into a clean listen state. */
	sk->sk_state = TCP_LISTEN;
	sk->sk_ack_backlog--;
	dprintk("%s: ok success on %02X, client on %02X\n", __func__,
		llc_sk(sk)->addr.sllc_sap, newllc->daddr.lsap);
frees:
	kfree_skb(skb);
out:
	release_sock(sk);
	return rc;
}

/**
 *	llc_ui_recvmsg - copy received data to the socket user.
 *	@sock: Socket to copy data from.
 *	@msg: Various user space related information.
 *	@len: Size of user buffer.
 *	@flags: User specified flags.
 *
 *	Copy received data to the socket user.
 *	Returns non-negative upon success, negative otherwise.
 */
static int llc_ui_recvmsg(struct kiocb *iocb, struct socket *sock,
			  struct msghdr *msg, size_t len, int flags)
{
	struct sockaddr_llc *uaddr = (struct sockaddr_llc *)msg->msg_name;
	const int nonblock = flags & MSG_DONTWAIT;
	struct sk_buff *skb = NULL;
	struct sock *sk = sock->sk;
	struct llc_sock *llc = llc_sk(sk);
	size_t copied = 0;
	u32 peek_seq = 0;
	u32 *seq;
	unsigned long used;
	int target;	/* Read at least this many bytes */
	long timeo;

	lock_sock(sk);
	copied = -ENOTCONN;
	if (unlikely(sk->sk_type == SOCK_STREAM && sk->sk_state == TCP_LISTEN))
		goto out;

	timeo = sock_rcvtimeo(sk, nonblock);

	seq = &llc->copied_seq;
	if (flags & MSG_PEEK) {
		peek_seq = llc->copied_seq;
		seq = &peek_seq;
	}

	target = sock_rcvlowat(sk, flags & MSG_WAITALL, len);
	copied = 0;

	do {
		u32 offset;

		/*
		 * We need to check signals first, to get correct SIGURG
		 * handling. FIXME: Need to check this doesn't impact 1003.1g
		 * and move it down to the bottom of the loop
		 */
		if (signal_pending(current)) {
			if (copied)
				break;
			copied = timeo ? sock_intr_errno(timeo) : -EAGAIN;
			break;
		}

		/* Next get a buffer. */

		skb = skb_peek(&sk->sk_receive_queue);
		if (skb) {
			offset = *seq;
			goto found_ok_skb;
		}
		/* Well, if we have backlog, try to process it now yet. */

		if (copied >= target && !sk->sk_backlog.tail)
			break;

		if (copied) {
			if (sk->sk_err ||
			    sk->sk_state == TCP_CLOSE ||
			    (sk->sk_shutdown & RCV_SHUTDOWN) ||
			    !timeo ||
			    (flags & MSG_PEEK))
				break;
		} else {
			if (sock_flag(sk, SOCK_DONE))
				break;

			if (sk->sk_err) {
				copied = sock_error(sk);
				break;
			}
			if (sk->sk_shutdown & RCV_SHUTDOWN)
				break;

			if (sk->sk_type == SOCK_STREAM && sk->sk_state == TCP_CLOSE) {
				if (!sock_flag(sk, SOCK_DONE)) {
					/*
					 * This occurs when user tries to read
					 * from never connected socket.
					 */
					copied = -ENOTCONN;
					break;
				}
				break;
			}
			if (!timeo) {
				copied = -EAGAIN;
				break;
			}
		}

		if (copied >= target) { /* Do not sleep, just process backlog. */
			release_sock(sk);
			lock_sock(sk);
		} else
			sk_wait_data(sk, &timeo);

		if ((flags & MSG_PEEK) && peek_seq != llc->copied_seq) {
			if (net_ratelimit())
				printk(KERN_DEBUG "LLC(%s:%d): Application "
						  "bug, race in MSG_PEEK.\n",
				       current->comm, task_pid_nr(current));
			peek_seq = llc->copied_seq;
		}
		continue;
	found_ok_skb:
		/* Ok so how much can we use? */
		used = skb->len - offset;
		if (len < used)
			used = len;

		if (!(flags & MSG_TRUNC)) {
			int rc = skb_copy_datagram_iovec(skb, offset,
							 msg->msg_iov, used);
			if (rc) {
				/* Exception. Bailout! */
				if (!copied)
					copied = -EFAULT;
				break;
			}
		}

		*seq += used;
		copied += used;
		len -= used;

		if (!(flags & MSG_PEEK)) {
			sk_eat_skb(sk, skb, 0);
			*seq = 0;
		}

		/* For non stream protcols we get one packet per recvmsg call */
		if (sk->sk_type != SOCK_STREAM)
			goto copy_uaddr;

		/* Partial read */
		if (used + offset < skb->len)
			continue;
	} while (len > 0);

out:
	release_sock(sk);
	return copied;
copy_uaddr:
	if (uaddr != NULL && skb != NULL) {
		memcpy(uaddr, llc_ui_skb_cb(skb), sizeof(*uaddr));
		msg->msg_namelen = sizeof(*uaddr);
	}
	goto out;
}

/**
 *	llc_ui_sendmsg - Transmit data provided by the socket user.
 *	@sock: Socket to transmit data from.
 *	@msg: Various user related information.
 *	@len: Length of data to transmit.
 *
 *	Transmit data provided by the socket user.
 *	Returns non-negative upon success, negative otherwise.
 */
static int llc_ui_sendmsg(struct kiocb *iocb, struct socket *sock,
			  struct msghdr *msg, size_t len)
{
	struct sock *sk = sock->sk;
	struct llc_sock *llc = llc_sk(sk);
	struct sockaddr_llc *addr = (struct sockaddr_llc *)msg->msg_name;
	int flags = msg->msg_flags;
	int noblock = flags & MSG_DONTWAIT;
	struct sk_buff *skb;
	size_t size = 0;
	int rc = -EINVAL, copied = 0, hdrlen;

	dprintk("%s: sending from %02X to %02X\n", __func__,
		llc->laddr.lsap, llc->daddr.lsap);
	lock_sock(sk);
	if (addr) {
		if (msg->msg_namelen < sizeof(*addr))
			goto release;
	} else {
		if (llc_ui_addr_null(&llc->addr))
			goto release;
		addr = &llc->addr;
	}
	/* must bind connection to sap if user hasn't done it. */
	if (sock_flag(sk, SOCK_ZAPPED)) {
		/* bind to sap with null dev, exclusive. */
		rc = llc_ui_autobind(sock, addr);
		if (rc)
			goto release;
	}
	hdrlen = llc->dev->hard_header_len + llc_ui_header_len(sk, addr);
	size = hdrlen + len;
	if (size > llc->dev->mtu)
		size = llc->dev->mtu;
	copied = size - hdrlen;
	release_sock(sk);
	skb = sock_alloc_send_skb(sk, size, noblock, &rc);
	lock_sock(sk);
	if (!skb)
		goto release;
	skb->dev      = llc->dev;
	skb->protocol = llc_proto_type(addr->sllc_arphrd);
	skb_reserve(skb, hdrlen);
	rc = memcpy_fromiovec(skb_put(skb, copied), msg->msg_iov, copied);
	if (rc)
		goto out;
	if (sk->sk_type == SOCK_DGRAM || addr->sllc_ua) {
		llc_build_and_send_ui_pkt(llc->sap, skb, addr->sllc_mac,
					  addr->sllc_sap);
		goto out;
	}
	if (addr->sllc_test) {
		llc_build_and_send_test_pkt(llc->sap, skb, addr->sllc_mac,
					    addr->sllc_sap);
		goto out;
	}
	if (addr->sllc_xid) {
		llc_build_and_send_xid_pkt(llc->sap, skb, addr->sllc_mac,
					   addr->sllc_sap);
		goto out;
	}
	rc = -ENOPROTOOPT;
	if (!(sk->sk_type == SOCK_STREAM && !addr->sllc_ua))
		goto out;
	rc = llc_ui_send_data(sk, skb, noblock);
out:
	if (rc) {
		kfree_skb(skb);
release:
		dprintk("%s: failed sending from %02X to %02X: %d\n",
			__func__, llc->laddr.lsap, llc->daddr.lsap, rc);
	}
	release_sock(sk);
	return rc ? : copied;
}

/**
 *	llc_ui_getname - return the address info of a socket
 *	@sock: Socket to get address of.
 *	@uaddr: Address structure to return information.
 *	@uaddrlen: Length of address structure.
 *	@peer: Does user want local or remote address information.
 *
 *	Return the address information of a socket.
 */
static int llc_ui_getname(struct socket *sock, struct sockaddr *uaddr,
			  int *uaddrlen, int peer)
{
	struct sockaddr_llc sllc;
	struct sock *sk = sock->sk;
	struct llc_sock *llc = llc_sk(sk);
	int rc = 0;

	lock_sock(sk);
	if (sock_flag(sk, SOCK_ZAPPED))
		goto out;
	*uaddrlen = sizeof(sllc);
	memset(uaddr, 0, *uaddrlen);
	if (peer) {
		rc = -ENOTCONN;
		if (sk->sk_state != TCP_ESTABLISHED)
			goto out;
		if(llc->dev)
			sllc.sllc_arphrd = llc->dev->type;
		sllc.sllc_sap = llc->daddr.lsap;
		memcpy(&sllc.sllc_mac, &llc->daddr.mac, IFHWADDRLEN);
	} else {
		rc = -EINVAL;
		if (!llc->sap)
			goto out;
		sllc.sllc_sap = llc->sap->laddr.lsap;

		if (llc->dev) {
			sllc.sllc_arphrd = llc->dev->type;
			memcpy(&sllc.sllc_mac, llc->dev->dev_addr,
			       IFHWADDRLEN);
		}
	}
	rc = 0;
	sllc.sllc_family = AF_LLC;
	memcpy(uaddr, &sllc, sizeof(sllc));
out:
	release_sock(sk);
	return rc;
}

/**
 *	llc_ui_ioctl - io controls for PF_LLC
 *	@sock: Socket to get/set info
 *	@cmd: command
 *	@arg: optional argument for cmd
 *
 *	get/set info on llc sockets
 */
static int llc_ui_ioctl(struct socket *sock, unsigned int cmd,
			unsigned long arg)
{
	return -ENOIOCTLCMD;
}

/**
 *	llc_ui_setsockopt - set various connection specific parameters.
 *	@sock: Socket to set options on.
 *	@level: Socket level user is requesting operations on.
 *	@optname: Operation name.
 *	@optval User provided operation data.
 *	@optlen: Length of optval.
 *
 *	Set various connection specific parameters.
 */
static int llc_ui_setsockopt(struct socket *sock, int level, int optname,
			     char __user *optval, int optlen)
{
	struct sock *sk = sock->sk;
	struct llc_sock *llc = llc_sk(sk);
	int rc = -EINVAL, opt;

	lock_sock(sk);
	if (unlikely(level != SOL_LLC || optlen != sizeof(int)))
		goto out;
	rc = get_user(opt, (int __user *)optval);
	if (rc)
		goto out;
	rc = -EINVAL;
	switch (optname) {
	case LLC_OPT_RETRY:
		if (opt > LLC_OPT_MAX_RETRY)
			goto out;
		llc->n2 = opt;
		break;
	case LLC_OPT_SIZE:
		if (opt > LLC_OPT_MAX_SIZE)
			goto out;
		llc->n1 = opt;
		break;
	case LLC_OPT_ACK_TMR_EXP:
		if (opt > LLC_OPT_MAX_ACK_TMR_EXP)
			goto out;
		llc->ack_timer.expire = opt * HZ;
		break;
	case LLC_OPT_P_TMR_EXP:
		if (opt > LLC_OPT_MAX_P_TMR_EXP)
			goto out;
		llc->pf_cycle_timer.expire = opt * HZ;
		break;
	case LLC_OPT_REJ_TMR_EXP:
		if (opt > LLC_OPT_MAX_REJ_TMR_EXP)
			goto out;
		llc->rej_sent_timer.expire = opt * HZ;
		break;
	case LLC_OPT_BUSY_TMR_EXP:
		if (opt > LLC_OPT_MAX_BUSY_TMR_EXP)
			goto out;
		llc->busy_state_timer.expire = opt * HZ;
		break;
	case LLC_OPT_TX_WIN:
		if (opt > LLC_OPT_MAX_WIN)
			goto out;
		llc->k = opt;
		break;
	case LLC_OPT_RX_WIN:
		if (opt > LLC_OPT_MAX_WIN)
			goto out;
		llc->rw = opt;
		break;
	default:
		rc = -ENOPROTOOPT;
		goto out;
	}
	rc = 0;
out:
	release_sock(sk);
	return rc;
}

/**
 *	llc_ui_getsockopt - get connection specific socket info
 *	@sock: Socket to get information from.
 *	@level: Socket level user is requesting operations on.
 *	@optname: Operation name.
 *	@optval: Variable to return operation data in.
 *	@optlen: Length of optval.
 *
 *	Get connection specific socket information.
 */
static int llc_ui_getsockopt(struct socket *sock, int level, int optname,
			     char __user *optval, int __user *optlen)
{
	struct sock *sk = sock->sk;
	struct llc_sock *llc = llc_sk(sk);
	int val = 0, len = 0, rc = -EINVAL;

	lock_sock(sk);
	if (unlikely(level != SOL_LLC))
		goto out;
	rc = get_user(len, optlen);
	if (rc)
		goto out;
	rc = -EINVAL;
	if (len != sizeof(int))
		goto out;
	switch (optname) {
	case LLC_OPT_RETRY:
		val = llc->n2;					break;
	case LLC_OPT_SIZE:
		val = llc->n1;					break;
	case LLC_OPT_ACK_TMR_EXP:
		val = llc->ack_timer.expire / HZ;		break;
	case LLC_OPT_P_TMR_EXP:
		val = llc->pf_cycle_timer.expire / HZ;		break;
	case LLC_OPT_REJ_TMR_EXP:
		val = llc->rej_sent_timer.expire / HZ;		break;
	case LLC_OPT_BUSY_TMR_EXP:
		val = llc->busy_state_timer.expire / HZ;	break;
	case LLC_OPT_TX_WIN:
		val = llc->k;				break;
	case LLC_OPT_RX_WIN:
		val = llc->rw;				break;
	default:
		rc = -ENOPROTOOPT;
		goto out;
	}
	rc = 0;
	if (put_user(len, optlen) || copy_to_user(optval, &val, len))
		rc = -EFAULT;
out:
	release_sock(sk);
	return rc;
}

static struct net_proto_family llc_ui_family_ops = {
	.family = PF_LLC,
	.create = llc_ui_create,
	.owner	= THIS_MODULE,
};

static const struct proto_ops llc_ui_ops = {
	.family	     = PF_LLC,
	.owner       = THIS_MODULE,
	.release     = llc_ui_release,
	.bind	     = llc_ui_bind,
	.connect     = llc_ui_connect,
	.socketpair  = sock_no_socketpair,
	.accept      = llc_ui_accept,
	.getname     = llc_ui_getname,
	.poll	     = datagram_poll,
	.ioctl       = llc_ui_ioctl,
	.listen      = llc_ui_listen,
	.shutdown    = llc_ui_shutdown,
	.setsockopt  = llc_ui_setsockopt,
	.getsockopt  = llc_ui_getsockopt,
	.sendmsg     = llc_ui_sendmsg,
	.recvmsg     = llc_ui_recvmsg,
	.mmap	     = sock_no_mmap,
	.sendpage    = sock_no_sendpage,
};

static const char llc_proc_err_msg[] __initconst =
	KERN_CRIT "LLC: Unable to register the proc_fs entries\n";
static const char llc_sysctl_err_msg[] __initconst =
	KERN_CRIT "LLC: Unable to register the sysctl entries\n";
static const char llc_sock_err_msg[] __initconst =
	KERN_CRIT "LLC: Unable to register the network family\n";

static int __init llc2_init(void)
{
	int rc = proto_register(&llc_proto, 0);

	if (rc != 0)
		goto out;

	llc_build_offset_table();
	llc_station_init();
	llc_ui_sap_last_autoport = LLC_SAP_DYN_START;
	rc = llc_proc_init();
	if (rc != 0) {
		printk(llc_proc_err_msg);
		goto out_unregister_llc_proto;
	}
	rc = llc_sysctl_init();
	if (rc) {
		printk(llc_sysctl_err_msg);
		goto out_proc;
	}
	rc = sock_register(&llc_ui_family_ops);
	if (rc) {
		printk(llc_sock_err_msg);
		goto out_sysctl;
	}
	llc_add_pack(LLC_DEST_SAP, llc_sap_handler);
	llc_add_pack(LLC_DEST_CONN, llc_conn_handler);
out:
	return rc;
out_sysctl:
	llc_sysctl_exit();
out_proc:
	llc_proc_exit();
out_unregister_llc_proto:
	proto_unregister(&llc_proto);
	goto out;
}

static void __exit llc2_exit(void)
{
	llc_station_exit();
	llc_remove_pack(LLC_DEST_SAP);
	llc_remove_pack(LLC_DEST_CONN);
	sock_unregister(PF_LLC);
	llc_proc_exit();
	llc_sysctl_exit();
	proto_unregister(&llc_proto);
}

module_init(llc2_init);
module_exit(llc2_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Procom 1997, Jay Schullist 2001, Arnaldo C. Melo 2001-2003");
MODULE_DESCRIPTION("IEEE 802.2 PF_LLC support");
MODULE_ALIAS_NETPROTO(PF_LLC);
