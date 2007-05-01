/* AF_RXRPC implementation
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include "ar-internal.h"

MODULE_DESCRIPTION("RxRPC network protocol");
MODULE_AUTHOR("Red Hat, Inc.");
MODULE_LICENSE("GPL");
MODULE_ALIAS_NETPROTO(PF_RXRPC);

unsigned rxrpc_debug; // = RXRPC_DEBUG_KPROTO;
module_param_named(debug, rxrpc_debug, uint, S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(rxrpc_debug, "RxRPC debugging mask");

static int sysctl_rxrpc_max_qlen __read_mostly = 10;

static struct proto rxrpc_proto;
static const struct proto_ops rxrpc_rpc_ops;

/* local epoch for detecting local-end reset */
__be32 rxrpc_epoch;

/* current debugging ID */
atomic_t rxrpc_debug_id;

/* count of skbs currently in use */
atomic_t rxrpc_n_skbs;

struct workqueue_struct *rxrpc_workqueue;

static void rxrpc_sock_destructor(struct sock *);

/*
 * see if an RxRPC socket is currently writable
 */
static inline int rxrpc_writable(struct sock *sk)
{
	return atomic_read(&sk->sk_wmem_alloc) < (size_t) sk->sk_sndbuf;
}

/*
 * wait for write bufferage to become available
 */
static void rxrpc_write_space(struct sock *sk)
{
	_enter("%p", sk);
	read_lock(&sk->sk_callback_lock);
	if (rxrpc_writable(sk)) {
		if (sk->sk_sleep && waitqueue_active(sk->sk_sleep))
			wake_up_interruptible(sk->sk_sleep);
		sk_wake_async(sk, 2, POLL_OUT);
	}
	read_unlock(&sk->sk_callback_lock);
}

/*
 * validate an RxRPC address
 */
static int rxrpc_validate_address(struct rxrpc_sock *rx,
				  struct sockaddr_rxrpc *srx,
				  int len)
{
	if (len < sizeof(struct sockaddr_rxrpc))
		return -EINVAL;

	if (srx->srx_family != AF_RXRPC)
		return -EAFNOSUPPORT;

	if (srx->transport_type != SOCK_DGRAM)
		return -ESOCKTNOSUPPORT;

	len -= offsetof(struct sockaddr_rxrpc, transport);
	if (srx->transport_len < sizeof(sa_family_t) ||
	    srx->transport_len > len)
		return -EINVAL;

	if (srx->transport.family != rx->proto)
		return -EAFNOSUPPORT;

	switch (srx->transport.family) {
	case AF_INET:
		_debug("INET: %x @ %u.%u.%u.%u",
		       ntohs(srx->transport.sin.sin_port),
		       NIPQUAD(srx->transport.sin.sin_addr));
		if (srx->transport_len > 8)
			memset((void *)&srx->transport + 8, 0,
			       srx->transport_len - 8);
		break;

	case AF_INET6:
	default:
		return -EAFNOSUPPORT;
	}

	return 0;
}

/*
 * bind a local address to an RxRPC socket
 */
static int rxrpc_bind(struct socket *sock, struct sockaddr *saddr, int len)
{
	struct sockaddr_rxrpc *srx = (struct sockaddr_rxrpc *) saddr;
	struct sock *sk = sock->sk;
	struct rxrpc_local *local;
	struct rxrpc_sock *rx = rxrpc_sk(sk), *prx;
	__be16 service_id;
	int ret;

	_enter("%p,%p,%d", rx, saddr, len);

	ret = rxrpc_validate_address(rx, srx, len);
	if (ret < 0)
		goto error;

	lock_sock(&rx->sk);

	if (rx->sk.sk_state != RXRPC_UNCONNECTED) {
		ret = -EINVAL;
		goto error_unlock;
	}

	memcpy(&rx->srx, srx, sizeof(rx->srx));

	/* find a local transport endpoint if we don't have one already */
	local = rxrpc_lookup_local(&rx->srx);
	if (IS_ERR(local)) {
		ret = PTR_ERR(local);
		goto error_unlock;
	}

	rx->local = local;
	if (srx->srx_service) {
		service_id = htons(srx->srx_service);
		write_lock_bh(&local->services_lock);
		list_for_each_entry(prx, &local->services, listen_link) {
			if (prx->service_id == service_id)
				goto service_in_use;
		}

		rx->service_id = service_id;
		list_add_tail(&rx->listen_link, &local->services);
		write_unlock_bh(&local->services_lock);

		rx->sk.sk_state = RXRPC_SERVER_BOUND;
	} else {
		rx->sk.sk_state = RXRPC_CLIENT_BOUND;
	}

	release_sock(&rx->sk);
	_leave(" = 0");
	return 0;

service_in_use:
	ret = -EADDRINUSE;
	write_unlock_bh(&local->services_lock);
error_unlock:
	release_sock(&rx->sk);
error:
	_leave(" = %d", ret);
	return ret;
}

/*
 * set the number of pending calls permitted on a listening socket
 */
static int rxrpc_listen(struct socket *sock, int backlog)
{
	struct sock *sk = sock->sk;
	struct rxrpc_sock *rx = rxrpc_sk(sk);
	int ret;

	_enter("%p,%d", rx, backlog);

	lock_sock(&rx->sk);

	switch (rx->sk.sk_state) {
	case RXRPC_UNCONNECTED:
		ret = -EADDRNOTAVAIL;
		break;
	case RXRPC_CLIENT_BOUND:
	case RXRPC_CLIENT_CONNECTED:
	default:
		ret = -EBUSY;
		break;
	case RXRPC_SERVER_BOUND:
		ASSERT(rx->local != NULL);
		sk->sk_max_ack_backlog = backlog;
		rx->sk.sk_state = RXRPC_SERVER_LISTENING;
		ret = 0;
		break;
	}

	release_sock(&rx->sk);
	_leave(" = %d", ret);
	return ret;
}

/*
 * find a transport by address
 */
static struct rxrpc_transport *rxrpc_name_to_transport(struct socket *sock,
						       struct sockaddr *addr,
						       int addr_len, int flags,
						       gfp_t gfp)
{
	struct sockaddr_rxrpc *srx = (struct sockaddr_rxrpc *) addr;
	struct rxrpc_transport *trans;
	struct rxrpc_sock *rx = rxrpc_sk(sock->sk);
	struct rxrpc_peer *peer;

	_enter("%p,%p,%d,%d", rx, addr, addr_len, flags);

	ASSERT(rx->local != NULL);
	ASSERT(rx->sk.sk_state > RXRPC_UNCONNECTED);

	if (rx->srx.transport_type != srx->transport_type)
		return ERR_PTR(-ESOCKTNOSUPPORT);
	if (rx->srx.transport.family != srx->transport.family)
		return ERR_PTR(-EAFNOSUPPORT);

	/* find a remote transport endpoint from the local one */
	peer = rxrpc_get_peer(srx, gfp);
	if (IS_ERR(peer))
		return ERR_PTR(PTR_ERR(peer));

	/* find a transport */
	trans = rxrpc_get_transport(rx->local, peer, gfp);
	rxrpc_put_peer(peer);
	_leave(" = %p", trans);
	return trans;
}

/**
 * rxrpc_kernel_begin_call - Allow a kernel service to begin a call
 * @sock: The socket on which to make the call
 * @srx: The address of the peer to contact (defaults to socket setting)
 * @key: The security context to use (defaults to socket setting)
 * @user_call_ID: The ID to use
 *
 * Allow a kernel service to begin a call on the nominated socket.  This just
 * sets up all the internal tracking structures and allocates connection and
 * call IDs as appropriate.  The call to be used is returned.
 *
 * The default socket destination address and security may be overridden by
 * supplying @srx and @key.
 */
struct rxrpc_call *rxrpc_kernel_begin_call(struct socket *sock,
					   struct sockaddr_rxrpc *srx,
					   struct key *key,
					   unsigned long user_call_ID,
					   gfp_t gfp)
{
	struct rxrpc_conn_bundle *bundle;
	struct rxrpc_transport *trans;
	struct rxrpc_call *call;
	struct rxrpc_sock *rx = rxrpc_sk(sock->sk);
	__be16 service_id;

	_enter(",,%x,%lx", key_serial(key), user_call_ID);

	lock_sock(&rx->sk);

	if (srx) {
		trans = rxrpc_name_to_transport(sock, (struct sockaddr *) srx,
						sizeof(*srx), 0, gfp);
		if (IS_ERR(trans)) {
			call = ERR_PTR(PTR_ERR(trans));
			trans = NULL;
			goto out;
		}
	} else {
		trans = rx->trans;
		if (!trans) {
			call = ERR_PTR(-ENOTCONN);
			goto out;
		}
		atomic_inc(&trans->usage);
	}

	service_id = rx->service_id;
	if (srx)
		service_id = htons(srx->srx_service);

	if (!key)
		key = rx->key;
	if (key && !key->payload.data)
		key = NULL; /* a no-security key */

	bundle = rxrpc_get_bundle(rx, trans, key, service_id, gfp);
	if (IS_ERR(bundle)) {
		call = ERR_PTR(PTR_ERR(bundle));
		goto out;
	}

	call = rxrpc_get_client_call(rx, trans, bundle, user_call_ID, true,
				     gfp);
	rxrpc_put_bundle(trans, bundle);
out:
	rxrpc_put_transport(trans);
	release_sock(&rx->sk);
	_leave(" = %p", call);
	return call;
}

EXPORT_SYMBOL(rxrpc_kernel_begin_call);

/**
 * rxrpc_kernel_end_call - Allow a kernel service to end a call it was using
 * @call: The call to end
 *
 * Allow a kernel service to end a call it was using.  The call must be
 * complete before this is called (the call should be aborted if necessary).
 */
void rxrpc_kernel_end_call(struct rxrpc_call *call)
{
	_enter("%d{%d}", call->debug_id, atomic_read(&call->usage));
	rxrpc_remove_user_ID(call->socket, call);
	rxrpc_put_call(call);
}

EXPORT_SYMBOL(rxrpc_kernel_end_call);

/**
 * rxrpc_kernel_intercept_rx_messages - Intercept received RxRPC messages
 * @sock: The socket to intercept received messages on
 * @interceptor: The function to pass the messages to
 *
 * Allow a kernel service to intercept messages heading for the Rx queue on an
 * RxRPC socket.  They get passed to the specified function instead.
 * @interceptor should free the socket buffers it is given.  @interceptor is
 * called with the socket receive queue spinlock held and softirqs disabled -
 * this ensures that the messages will be delivered in the right order.
 */
void rxrpc_kernel_intercept_rx_messages(struct socket *sock,
					rxrpc_interceptor_t interceptor)
{
	struct rxrpc_sock *rx = rxrpc_sk(sock->sk);

	_enter("");
	rx->interceptor = interceptor;
}

EXPORT_SYMBOL(rxrpc_kernel_intercept_rx_messages);

/*
 * connect an RxRPC socket
 * - this just targets it at a specific destination; no actual connection
 *   negotiation takes place
 */
static int rxrpc_connect(struct socket *sock, struct sockaddr *addr,
			 int addr_len, int flags)
{
	struct sockaddr_rxrpc *srx = (struct sockaddr_rxrpc *) addr;
	struct sock *sk = sock->sk;
	struct rxrpc_transport *trans;
	struct rxrpc_local *local;
	struct rxrpc_sock *rx = rxrpc_sk(sk);
	int ret;

	_enter("%p,%p,%d,%d", rx, addr, addr_len, flags);

	ret = rxrpc_validate_address(rx, srx, addr_len);
	if (ret < 0) {
		_leave(" = %d [bad addr]", ret);
		return ret;
	}

	lock_sock(&rx->sk);

	switch (rx->sk.sk_state) {
	case RXRPC_UNCONNECTED:
		/* find a local transport endpoint if we don't have one already */
		ASSERTCMP(rx->local, ==, NULL);
		rx->srx.srx_family = AF_RXRPC;
		rx->srx.srx_service = 0;
		rx->srx.transport_type = srx->transport_type;
		rx->srx.transport_len = sizeof(sa_family_t);
		rx->srx.transport.family = srx->transport.family;
		local = rxrpc_lookup_local(&rx->srx);
		if (IS_ERR(local)) {
			release_sock(&rx->sk);
			return PTR_ERR(local);
		}
		rx->local = local;
		rx->sk.sk_state = RXRPC_CLIENT_BOUND;
	case RXRPC_CLIENT_BOUND:
		break;
	case RXRPC_CLIENT_CONNECTED:
		release_sock(&rx->sk);
		return -EISCONN;
	default:
		release_sock(&rx->sk);
		return -EBUSY; /* server sockets can't connect as well */
	}

	trans = rxrpc_name_to_transport(sock, addr, addr_len, flags,
					GFP_KERNEL);
	if (IS_ERR(trans)) {
		release_sock(&rx->sk);
		_leave(" = %ld", PTR_ERR(trans));
		return PTR_ERR(trans);
	}

	rx->trans = trans;
	rx->service_id = htons(srx->srx_service);
	rx->sk.sk_state = RXRPC_CLIENT_CONNECTED;

	release_sock(&rx->sk);
	return 0;
}

/*
 * send a message through an RxRPC socket
 * - in a client this does a number of things:
 *   - finds/sets up a connection for the security specified (if any)
 *   - initiates a call (ID in control data)
 *   - ends the request phase of a call (if MSG_MORE is not set)
 *   - sends a call data packet
 *   - may send an abort (abort code in control data)
 */
static int rxrpc_sendmsg(struct kiocb *iocb, struct socket *sock,
			 struct msghdr *m, size_t len)
{
	struct rxrpc_transport *trans;
	struct rxrpc_sock *rx = rxrpc_sk(sock->sk);
	int ret;

	_enter(",{%d},,%zu", rx->sk.sk_state, len);

	if (m->msg_flags & MSG_OOB)
		return -EOPNOTSUPP;

	if (m->msg_name) {
		ret = rxrpc_validate_address(rx, m->msg_name, m->msg_namelen);
		if (ret < 0) {
			_leave(" = %d [bad addr]", ret);
			return ret;
		}
	}

	trans = NULL;
	lock_sock(&rx->sk);

	if (m->msg_name) {
		ret = -EISCONN;
		trans = rxrpc_name_to_transport(sock, m->msg_name,
						m->msg_namelen, 0, GFP_KERNEL);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			trans = NULL;
			goto out;
		}
	} else {
		trans = rx->trans;
		if (trans)
			atomic_inc(&trans->usage);
	}

	switch (rx->sk.sk_state) {
	case RXRPC_SERVER_LISTENING:
		if (!m->msg_name) {
			ret = rxrpc_server_sendmsg(iocb, rx, m, len);
			break;
		}
	case RXRPC_SERVER_BOUND:
	case RXRPC_CLIENT_BOUND:
		if (!m->msg_name) {
			ret = -ENOTCONN;
			break;
		}
	case RXRPC_CLIENT_CONNECTED:
		ret = rxrpc_client_sendmsg(iocb, rx, trans, m, len);
		break;
	default:
		ret = -ENOTCONN;
		break;
	}

out:
	release_sock(&rx->sk);
	if (trans)
		rxrpc_put_transport(trans);
	_leave(" = %d", ret);
	return ret;
}

/*
 * set RxRPC socket options
 */
static int rxrpc_setsockopt(struct socket *sock, int level, int optname,
			    char __user *optval, int optlen)
{
	struct rxrpc_sock *rx = rxrpc_sk(sock->sk);
	unsigned min_sec_level;
	int ret;

	_enter(",%d,%d,,%d", level, optname, optlen);

	lock_sock(&rx->sk);
	ret = -EOPNOTSUPP;

	if (level == SOL_RXRPC) {
		switch (optname) {
		case RXRPC_EXCLUSIVE_CONNECTION:
			ret = -EINVAL;
			if (optlen != 0)
				goto error;
			ret = -EISCONN;
			if (rx->sk.sk_state != RXRPC_UNCONNECTED)
				goto error;
			set_bit(RXRPC_SOCK_EXCLUSIVE_CONN, &rx->flags);
			goto success;

		case RXRPC_SECURITY_KEY:
			ret = -EINVAL;
			if (rx->key)
				goto error;
			ret = -EISCONN;
			if (rx->sk.sk_state != RXRPC_UNCONNECTED)
				goto error;
			ret = rxrpc_request_key(rx, optval, optlen);
			goto error;

		case RXRPC_SECURITY_KEYRING:
			ret = -EINVAL;
			if (rx->key)
				goto error;
			ret = -EISCONN;
			if (rx->sk.sk_state != RXRPC_UNCONNECTED)
				goto error;
			ret = rxrpc_server_keyring(rx, optval, optlen);
			goto error;

		case RXRPC_MIN_SECURITY_LEVEL:
			ret = -EINVAL;
			if (optlen != sizeof(unsigned))
				goto error;
			ret = -EISCONN;
			if (rx->sk.sk_state != RXRPC_UNCONNECTED)
				goto error;
			ret = get_user(min_sec_level,
				       (unsigned __user *) optval);
			if (ret < 0)
				goto error;
			ret = -EINVAL;
			if (min_sec_level > RXRPC_SECURITY_MAX)
				goto error;
			rx->min_sec_level = min_sec_level;
			goto success;

		default:
			break;
		}
	}

success:
	ret = 0;
error:
	release_sock(&rx->sk);
	return ret;
}

/*
 * permit an RxRPC socket to be polled
 */
static unsigned int rxrpc_poll(struct file *file, struct socket *sock,
			       poll_table *wait)
{
	unsigned int mask;
	struct sock *sk = sock->sk;

	poll_wait(file, sk->sk_sleep, wait);
	mask = 0;

	/* the socket is readable if there are any messages waiting on the Rx
	 * queue */
	if (!skb_queue_empty(&sk->sk_receive_queue))
		mask |= POLLIN | POLLRDNORM;

	/* the socket is writable if there is space to add new data to the
	 * socket; there is no guarantee that any particular call in progress
	 * on the socket may have space in the Tx ACK window */
	if (rxrpc_writable(sk))
		mask |= POLLOUT | POLLWRNORM;

	return mask;
}

/*
 * create an RxRPC socket
 */
static int rxrpc_create(struct socket *sock, int protocol)
{
	struct rxrpc_sock *rx;
	struct sock *sk;

	_enter("%p,%d", sock, protocol);

	/* we support transport protocol UDP only */
	if (protocol != PF_INET)
		return -EPROTONOSUPPORT;

	if (sock->type != SOCK_DGRAM)
		return -ESOCKTNOSUPPORT;

	sock->ops = &rxrpc_rpc_ops;
	sock->state = SS_UNCONNECTED;

	sk = sk_alloc(PF_RXRPC, GFP_KERNEL, &rxrpc_proto, 1);
	if (!sk)
		return -ENOMEM;

	sock_init_data(sock, sk);
	sk->sk_state		= RXRPC_UNCONNECTED;
	sk->sk_write_space	= rxrpc_write_space;
	sk->sk_max_ack_backlog	= sysctl_rxrpc_max_qlen;
	sk->sk_destruct		= rxrpc_sock_destructor;

	rx = rxrpc_sk(sk);
	rx->proto = protocol;
	rx->calls = RB_ROOT;

	INIT_LIST_HEAD(&rx->listen_link);
	INIT_LIST_HEAD(&rx->secureq);
	INIT_LIST_HEAD(&rx->acceptq);
	rwlock_init(&rx->call_lock);
	memset(&rx->srx, 0, sizeof(rx->srx));

	_leave(" = 0 [%p]", rx);
	return 0;
}

/*
 * RxRPC socket destructor
 */
static void rxrpc_sock_destructor(struct sock *sk)
{
	_enter("%p", sk);

	rxrpc_purge_queue(&sk->sk_receive_queue);

	BUG_TRAP(!atomic_read(&sk->sk_wmem_alloc));
	BUG_TRAP(sk_unhashed(sk));
	BUG_TRAP(!sk->sk_socket);

	if (!sock_flag(sk, SOCK_DEAD)) {
		printk("Attempt to release alive rxrpc socket: %p\n", sk);
		return;
	}
}

/*
 * release an RxRPC socket
 */
static int rxrpc_release_sock(struct sock *sk)
{
	struct rxrpc_sock *rx = rxrpc_sk(sk);

	_enter("%p{%d,%d}", sk, sk->sk_state, atomic_read(&sk->sk_refcnt));

	/* declare the socket closed for business */
	sock_orphan(sk);
	sk->sk_shutdown = SHUTDOWN_MASK;

	spin_lock_bh(&sk->sk_receive_queue.lock);
	sk->sk_state = RXRPC_CLOSE;
	spin_unlock_bh(&sk->sk_receive_queue.lock);

	ASSERTCMP(rx->listen_link.next, !=, LIST_POISON1);

	if (!list_empty(&rx->listen_link)) {
		write_lock_bh(&rx->local->services_lock);
		list_del(&rx->listen_link);
		write_unlock_bh(&rx->local->services_lock);
	}

	/* try to flush out this socket */
	rxrpc_release_calls_on_socket(rx);
	flush_workqueue(rxrpc_workqueue);
	rxrpc_purge_queue(&sk->sk_receive_queue);

	if (rx->conn) {
		rxrpc_put_connection(rx->conn);
		rx->conn = NULL;
	}

	if (rx->bundle) {
		rxrpc_put_bundle(rx->trans, rx->bundle);
		rx->bundle = NULL;
	}
	if (rx->trans) {
		rxrpc_put_transport(rx->trans);
		rx->trans = NULL;
	}
	if (rx->local) {
		rxrpc_put_local(rx->local);
		rx->local = NULL;
	}

	key_put(rx->key);
	rx->key = NULL;
	key_put(rx->securities);
	rx->securities = NULL;
	sock_put(sk);

	_leave(" = 0");
	return 0;
}

/*
 * release an RxRPC BSD socket on close() or equivalent
 */
static int rxrpc_release(struct socket *sock)
{
	struct sock *sk = sock->sk;

	_enter("%p{%p}", sock, sk);

	if (!sk)
		return 0;

	sock->sk = NULL;

	return rxrpc_release_sock(sk);
}

/*
 * RxRPC network protocol
 */
static const struct proto_ops rxrpc_rpc_ops = {
	.family		= PF_UNIX,
	.owner		= THIS_MODULE,
	.release	= rxrpc_release,
	.bind		= rxrpc_bind,
	.connect	= rxrpc_connect,
	.socketpair	= sock_no_socketpair,
	.accept		= sock_no_accept,
	.getname	= sock_no_getname,
	.poll		= rxrpc_poll,
	.ioctl		= sock_no_ioctl,
	.listen		= rxrpc_listen,
	.shutdown	= sock_no_shutdown,
	.setsockopt	= rxrpc_setsockopt,
	.getsockopt	= sock_no_getsockopt,
	.sendmsg	= rxrpc_sendmsg,
	.recvmsg	= rxrpc_recvmsg,
	.mmap		= sock_no_mmap,
	.sendpage	= sock_no_sendpage,
};

static struct proto rxrpc_proto = {
	.name		= "RXRPC",
	.owner		= THIS_MODULE,
	.obj_size	= sizeof(struct rxrpc_sock),
	.max_header	= sizeof(struct rxrpc_header),
};

static struct net_proto_family rxrpc_family_ops = {
	.family	= PF_RXRPC,
	.create = rxrpc_create,
	.owner	= THIS_MODULE,
};

/*
 * initialise and register the RxRPC protocol
 */
static int __init af_rxrpc_init(void)
{
	struct sk_buff *dummy_skb;
	int ret = -1;

	BUILD_BUG_ON(sizeof(struct rxrpc_skb_priv) > sizeof(dummy_skb->cb));

	rxrpc_epoch = htonl(xtime.tv_sec);

	ret = -ENOMEM;
	rxrpc_call_jar = kmem_cache_create(
		"rxrpc_call_jar", sizeof(struct rxrpc_call), 0,
		SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (!rxrpc_call_jar) {
		printk(KERN_NOTICE "RxRPC: Failed to allocate call jar\n");
		goto error_call_jar;
	}

	rxrpc_workqueue = create_workqueue("krxrpcd");
	if (!rxrpc_workqueue) {
		printk(KERN_NOTICE "RxRPC: Failed to allocate work queue\n");
		goto error_work_queue;
	}

	ret = proto_register(&rxrpc_proto, 1);
        if (ret < 0) {
                printk(KERN_CRIT "RxRPC: Cannot register protocol\n");
		goto error_proto;
	}

	ret = sock_register(&rxrpc_family_ops);
	if (ret < 0) {
                printk(KERN_CRIT "RxRPC: Cannot register socket family\n");
		goto error_sock;
	}

	ret = register_key_type(&key_type_rxrpc);
	if (ret < 0) {
                printk(KERN_CRIT "RxRPC: Cannot register client key type\n");
		goto error_key_type;
	}

	ret = register_key_type(&key_type_rxrpc_s);
	if (ret < 0) {
                printk(KERN_CRIT "RxRPC: Cannot register server key type\n");
		goto error_key_type_s;
	}

#ifdef CONFIG_PROC_FS
	proc_net_fops_create("rxrpc_calls", 0, &rxrpc_call_seq_fops);
	proc_net_fops_create("rxrpc_conns", 0, &rxrpc_connection_seq_fops);
#endif
	return 0;

error_key_type_s:
	unregister_key_type(&key_type_rxrpc);
error_key_type:
	sock_unregister(PF_RXRPC);
error_sock:
	proto_unregister(&rxrpc_proto);
error_proto:
	destroy_workqueue(rxrpc_workqueue);
error_work_queue:
	kmem_cache_destroy(rxrpc_call_jar);
error_call_jar:
	return ret;
}

/*
 * unregister the RxRPC protocol
 */
static void __exit af_rxrpc_exit(void)
{
	_enter("");
	unregister_key_type(&key_type_rxrpc_s);
	unregister_key_type(&key_type_rxrpc);
	sock_unregister(PF_RXRPC);
	proto_unregister(&rxrpc_proto);
	rxrpc_destroy_all_calls();
	rxrpc_destroy_all_connections();
	rxrpc_destroy_all_transports();
	rxrpc_destroy_all_peers();
	rxrpc_destroy_all_locals();

	ASSERTCMP(atomic_read(&rxrpc_n_skbs), ==, 0);

	_debug("flush scheduled work");
	flush_workqueue(rxrpc_workqueue);
	proc_net_remove("rxrpc_conns");
	proc_net_remove("rxrpc_calls");
	destroy_workqueue(rxrpc_workqueue);
	kmem_cache_destroy(rxrpc_call_jar);
	_leave("");
}

module_init(af_rxrpc_init);
module_exit(af_rxrpc_exit);
