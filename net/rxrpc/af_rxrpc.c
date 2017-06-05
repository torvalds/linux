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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/net.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/random.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/key-type.h>
#include <net/net_namespace.h>
#include <net/sock.h>
#include <net/af_rxrpc.h>
#define CREATE_TRACE_POINTS
#include "ar-internal.h"

MODULE_DESCRIPTION("RxRPC network protocol");
MODULE_AUTHOR("Red Hat, Inc.");
MODULE_LICENSE("GPL");
MODULE_ALIAS_NETPROTO(PF_RXRPC);

unsigned int rxrpc_debug; // = RXRPC_DEBUG_KPROTO;
module_param_named(debug, rxrpc_debug, uint, S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(debug, "RxRPC debugging mask");

static struct proto rxrpc_proto;
static const struct proto_ops rxrpc_rpc_ops;

/* current debugging ID */
atomic_t rxrpc_debug_id;

/* count of skbs currently in use */
atomic_t rxrpc_n_tx_skbs, rxrpc_n_rx_skbs;

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
	rcu_read_lock();
	if (rxrpc_writable(sk)) {
		struct socket_wq *wq = rcu_dereference(sk->sk_wq);

		if (skwq_has_sleeper(wq))
			wake_up_interruptible(&wq->wait);
		sk_wake_async(sk, SOCK_WAKE_SPACE, POLL_OUT);
	}
	rcu_read_unlock();
}

/*
 * validate an RxRPC address
 */
static int rxrpc_validate_address(struct rxrpc_sock *rx,
				  struct sockaddr_rxrpc *srx,
				  int len)
{
	unsigned int tail;

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

	if (srx->transport.family != rx->family)
		return -EAFNOSUPPORT;

	switch (srx->transport.family) {
	case AF_INET:
		if (srx->transport_len < sizeof(struct sockaddr_in))
			return -EINVAL;
		tail = offsetof(struct sockaddr_rxrpc, transport.sin.__pad);
		break;

#ifdef CONFIG_AF_RXRPC_IPV6
	case AF_INET6:
		if (srx->transport_len < sizeof(struct sockaddr_in6))
			return -EINVAL;
		tail = offsetof(struct sockaddr_rxrpc, transport) +
			sizeof(struct sockaddr_in6);
		break;
#endif

	default:
		return -EAFNOSUPPORT;
	}

	if (tail < len)
		memset((void *)srx + tail, 0, len - tail);
	_debug("INET: %pISp", &srx->transport);
	return 0;
}

/*
 * bind a local address to an RxRPC socket
 */
static int rxrpc_bind(struct socket *sock, struct sockaddr *saddr, int len)
{
	struct sockaddr_rxrpc *srx = (struct sockaddr_rxrpc *)saddr;
	struct rxrpc_local *local;
	struct rxrpc_sock *rx = rxrpc_sk(sock->sk);
	u16 service_id = srx->srx_service;
	int ret;

	_enter("%p,%p,%d", rx, saddr, len);

	ret = rxrpc_validate_address(rx, srx, len);
	if (ret < 0)
		goto error;

	lock_sock(&rx->sk);

	if (rx->sk.sk_state != RXRPC_UNBOUND) {
		ret = -EINVAL;
		goto error_unlock;
	}

	memcpy(&rx->srx, srx, sizeof(rx->srx));

	local = rxrpc_lookup_local(sock_net(&rx->sk), &rx->srx);
	if (IS_ERR(local)) {
		ret = PTR_ERR(local);
		goto error_unlock;
	}

	if (service_id) {
		write_lock(&local->services_lock);
		if (rcu_access_pointer(local->service))
			goto service_in_use;
		rx->local = local;
		rcu_assign_pointer(local->service, rx);
		write_unlock(&local->services_lock);

		rx->sk.sk_state = RXRPC_SERVER_BOUND;
	} else {
		rx->local = local;
		rx->sk.sk_state = RXRPC_CLIENT_BOUND;
	}

	release_sock(&rx->sk);
	_leave(" = 0");
	return 0;

service_in_use:
	write_unlock(&local->services_lock);
	rxrpc_put_local(local);
	ret = -EADDRINUSE;
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
	unsigned int max, old;
	int ret;

	_enter("%p,%d", rx, backlog);

	lock_sock(&rx->sk);

	switch (rx->sk.sk_state) {
	case RXRPC_UNBOUND:
		ret = -EADDRNOTAVAIL;
		break;
	case RXRPC_SERVER_BOUND:
		ASSERT(rx->local != NULL);
		max = READ_ONCE(rxrpc_max_backlog);
		ret = -EINVAL;
		if (backlog == INT_MAX)
			backlog = max;
		else if (backlog < 0 || backlog > max)
			break;
		old = sk->sk_max_ack_backlog;
		sk->sk_max_ack_backlog = backlog;
		ret = rxrpc_service_prealloc(rx, GFP_KERNEL);
		if (ret == 0)
			rx->sk.sk_state = RXRPC_SERVER_LISTENING;
		else
			sk->sk_max_ack_backlog = old;
		break;
	case RXRPC_SERVER_LISTENING:
		if (backlog == 0) {
			rx->sk.sk_state = RXRPC_SERVER_LISTEN_DISABLED;
			sk->sk_max_ack_backlog = 0;
			rxrpc_discard_prealloc(rx);
			ret = 0;
			break;
		}
	default:
		ret = -EBUSY;
		break;
	}

	release_sock(&rx->sk);
	_leave(" = %d", ret);
	return ret;
}

/**
 * rxrpc_kernel_begin_call - Allow a kernel service to begin a call
 * @sock: The socket on which to make the call
 * @srx: The address of the peer to contact
 * @key: The security context to use (defaults to socket setting)
 * @user_call_ID: The ID to use
 * @gfp: The allocation constraints
 * @notify_rx: Where to send notifications instead of socket queue
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
					   gfp_t gfp,
					   rxrpc_notify_rx_t notify_rx)
{
	struct rxrpc_conn_parameters cp;
	struct rxrpc_call *call;
	struct rxrpc_sock *rx = rxrpc_sk(sock->sk);
	int ret;

	_enter(",,%x,%lx", key_serial(key), user_call_ID);

	ret = rxrpc_validate_address(rx, srx, sizeof(*srx));
	if (ret < 0)
		return ERR_PTR(ret);

	lock_sock(&rx->sk);

	if (!key)
		key = rx->key;
	if (key && !key->payload.data[0])
		key = NULL; /* a no-security key */

	memset(&cp, 0, sizeof(cp));
	cp.local		= rx->local;
	cp.key			= key;
	cp.security_level	= 0;
	cp.exclusive		= false;
	cp.service_id		= srx->srx_service;
	call = rxrpc_new_client_call(rx, &cp, srx, user_call_ID, gfp);
	/* The socket has been unlocked. */
	if (!IS_ERR(call))
		call->notify_rx = notify_rx;

	mutex_unlock(&call->user_mutex);
	_leave(" = %p", call);
	return call;
}
EXPORT_SYMBOL(rxrpc_kernel_begin_call);

/**
 * rxrpc_kernel_end_call - Allow a kernel service to end a call it was using
 * @sock: The socket the call is on
 * @call: The call to end
 *
 * Allow a kernel service to end a call it was using.  The call must be
 * complete before this is called (the call should be aborted if necessary).
 */
void rxrpc_kernel_end_call(struct socket *sock, struct rxrpc_call *call)
{
	_enter("%d{%d}", call->debug_id, atomic_read(&call->usage));

	mutex_lock(&call->user_mutex);
	rxrpc_release_call(rxrpc_sk(sock->sk), call);
	mutex_unlock(&call->user_mutex);
	rxrpc_put_call(call, rxrpc_call_put_kernel);
}
EXPORT_SYMBOL(rxrpc_kernel_end_call);

/**
 * rxrpc_kernel_new_call_notification - Get notifications of new calls
 * @sock: The socket to intercept received messages on
 * @notify_new_call: Function to be called when new calls appear
 * @discard_new_call: Function to discard preallocated calls
 *
 * Allow a kernel service to be given notifications about new calls.
 */
void rxrpc_kernel_new_call_notification(
	struct socket *sock,
	rxrpc_notify_new_call_t notify_new_call,
	rxrpc_discard_new_call_t discard_new_call)
{
	struct rxrpc_sock *rx = rxrpc_sk(sock->sk);

	rx->notify_new_call = notify_new_call;
	rx->discard_new_call = discard_new_call;
}
EXPORT_SYMBOL(rxrpc_kernel_new_call_notification);

/*
 * connect an RxRPC socket
 * - this just targets it at a specific destination; no actual connection
 *   negotiation takes place
 */
static int rxrpc_connect(struct socket *sock, struct sockaddr *addr,
			 int addr_len, int flags)
{
	struct sockaddr_rxrpc *srx = (struct sockaddr_rxrpc *)addr;
	struct rxrpc_sock *rx = rxrpc_sk(sock->sk);
	int ret;

	_enter("%p,%p,%d,%d", rx, addr, addr_len, flags);

	ret = rxrpc_validate_address(rx, srx, addr_len);
	if (ret < 0) {
		_leave(" = %d [bad addr]", ret);
		return ret;
	}

	lock_sock(&rx->sk);

	ret = -EISCONN;
	if (test_bit(RXRPC_SOCK_CONNECTED, &rx->flags))
		goto error;

	switch (rx->sk.sk_state) {
	case RXRPC_UNBOUND:
		rx->sk.sk_state = RXRPC_CLIENT_UNBOUND;
	case RXRPC_CLIENT_UNBOUND:
	case RXRPC_CLIENT_BOUND:
		break;
	default:
		ret = -EBUSY;
		goto error;
	}

	rx->connect_srx = *srx;
	set_bit(RXRPC_SOCK_CONNECTED, &rx->flags);
	ret = 0;

error:
	release_sock(&rx->sk);
	return ret;
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
static int rxrpc_sendmsg(struct socket *sock, struct msghdr *m, size_t len)
{
	struct rxrpc_local *local;
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

	lock_sock(&rx->sk);

	switch (rx->sk.sk_state) {
	case RXRPC_UNBOUND:
		rx->srx.srx_family = AF_RXRPC;
		rx->srx.srx_service = 0;
		rx->srx.transport_type = SOCK_DGRAM;
		rx->srx.transport.family = rx->family;
		switch (rx->family) {
		case AF_INET:
			rx->srx.transport_len = sizeof(struct sockaddr_in);
			break;
#ifdef CONFIG_AF_RXRPC_IPV6
		case AF_INET6:
			rx->srx.transport_len = sizeof(struct sockaddr_in6);
			break;
#endif
		default:
			ret = -EAFNOSUPPORT;
			goto error_unlock;
		}
		local = rxrpc_lookup_local(sock_net(sock->sk), &rx->srx);
		if (IS_ERR(local)) {
			ret = PTR_ERR(local);
			goto error_unlock;
		}

		rx->local = local;
		rx->sk.sk_state = RXRPC_CLIENT_UNBOUND;
		/* Fall through */

	case RXRPC_CLIENT_UNBOUND:
	case RXRPC_CLIENT_BOUND:
		if (!m->msg_name &&
		    test_bit(RXRPC_SOCK_CONNECTED, &rx->flags)) {
			m->msg_name = &rx->connect_srx;
			m->msg_namelen = sizeof(rx->connect_srx);
		}
	case RXRPC_SERVER_BOUND:
	case RXRPC_SERVER_LISTENING:
		ret = rxrpc_do_sendmsg(rx, m, len);
		/* The socket has been unlocked */
		goto out;
	default:
		ret = -EINVAL;
		goto error_unlock;
	}

error_unlock:
	release_sock(&rx->sk);
out:
	_leave(" = %d", ret);
	return ret;
}

/*
 * set RxRPC socket options
 */
static int rxrpc_setsockopt(struct socket *sock, int level, int optname,
			    char __user *optval, unsigned int optlen)
{
	struct rxrpc_sock *rx = rxrpc_sk(sock->sk);
	unsigned int min_sec_level;
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
			if (rx->sk.sk_state != RXRPC_UNBOUND)
				goto error;
			rx->exclusive = true;
			goto success;

		case RXRPC_SECURITY_KEY:
			ret = -EINVAL;
			if (rx->key)
				goto error;
			ret = -EISCONN;
			if (rx->sk.sk_state != RXRPC_UNBOUND)
				goto error;
			ret = rxrpc_request_key(rx, optval, optlen);
			goto error;

		case RXRPC_SECURITY_KEYRING:
			ret = -EINVAL;
			if (rx->key)
				goto error;
			ret = -EISCONN;
			if (rx->sk.sk_state != RXRPC_UNBOUND)
				goto error;
			ret = rxrpc_server_keyring(rx, optval, optlen);
			goto error;

		case RXRPC_MIN_SECURITY_LEVEL:
			ret = -EINVAL;
			if (optlen != sizeof(unsigned int))
				goto error;
			ret = -EISCONN;
			if (rx->sk.sk_state != RXRPC_UNBOUND)
				goto error;
			ret = get_user(min_sec_level,
				       (unsigned int __user *) optval);
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
	struct sock *sk = sock->sk;
	struct rxrpc_sock *rx = rxrpc_sk(sk);
	unsigned int mask;

	sock_poll_wait(file, sk_sleep(sk), wait);
	mask = 0;

	/* the socket is readable if there are any messages waiting on the Rx
	 * queue */
	if (!list_empty(&rx->recvmsg_q))
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
static int rxrpc_create(struct net *net, struct socket *sock, int protocol,
			int kern)
{
	struct rxrpc_sock *rx;
	struct sock *sk;

	_enter("%p,%d", sock, protocol);

	/* we support transport protocol UDP/UDP6 only */
	if (protocol != PF_INET &&
	    IS_ENABLED(CONFIG_AF_RXRPC_IPV6) && protocol != PF_INET6)
		return -EPROTONOSUPPORT;

	if (sock->type != SOCK_DGRAM)
		return -ESOCKTNOSUPPORT;

	sock->ops = &rxrpc_rpc_ops;
	sock->state = SS_UNCONNECTED;

	sk = sk_alloc(net, PF_RXRPC, GFP_KERNEL, &rxrpc_proto, kern);
	if (!sk)
		return -ENOMEM;

	sock_init_data(sock, sk);
	sock_set_flag(sk, SOCK_RCU_FREE);
	sk->sk_state		= RXRPC_UNBOUND;
	sk->sk_write_space	= rxrpc_write_space;
	sk->sk_max_ack_backlog	= 0;
	sk->sk_destruct		= rxrpc_sock_destructor;

	rx = rxrpc_sk(sk);
	rx->family = protocol;
	rx->calls = RB_ROOT;

	spin_lock_init(&rx->incoming_lock);
	INIT_LIST_HEAD(&rx->sock_calls);
	INIT_LIST_HEAD(&rx->to_be_accepted);
	INIT_LIST_HEAD(&rx->recvmsg_q);
	rwlock_init(&rx->recvmsg_lock);
	rwlock_init(&rx->call_lock);
	memset(&rx->srx, 0, sizeof(rx->srx));

	_leave(" = 0 [%p]", rx);
	return 0;
}

/*
 * Kill all the calls on a socket and shut it down.
 */
static int rxrpc_shutdown(struct socket *sock, int flags)
{
	struct sock *sk = sock->sk;
	struct rxrpc_sock *rx = rxrpc_sk(sk);
	int ret = 0;

	_enter("%p,%d", sk, flags);

	if (flags != SHUT_RDWR)
		return -EOPNOTSUPP;
	if (sk->sk_state == RXRPC_CLOSE)
		return -ESHUTDOWN;

	lock_sock(sk);

	spin_lock_bh(&sk->sk_receive_queue.lock);
	if (sk->sk_state < RXRPC_CLOSE) {
		sk->sk_state = RXRPC_CLOSE;
		sk->sk_shutdown = SHUTDOWN_MASK;
	} else {
		ret = -ESHUTDOWN;
	}
	spin_unlock_bh(&sk->sk_receive_queue.lock);

	rxrpc_discard_prealloc(rx);

	release_sock(sk);
	return ret;
}

/*
 * RxRPC socket destructor
 */
static void rxrpc_sock_destructor(struct sock *sk)
{
	_enter("%p", sk);

	rxrpc_purge_queue(&sk->sk_receive_queue);

	WARN_ON(atomic_read(&sk->sk_wmem_alloc));
	WARN_ON(!sk_unhashed(sk));
	WARN_ON(sk->sk_socket);

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

	if (rx->local && rcu_access_pointer(rx->local->service) == rx) {
		write_lock(&rx->local->services_lock);
		rcu_assign_pointer(rx->local->service, NULL);
		write_unlock(&rx->local->services_lock);
	}

	/* try to flush out this socket */
	rxrpc_discard_prealloc(rx);
	rxrpc_release_calls_on_socket(rx);
	flush_workqueue(rxrpc_workqueue);
	rxrpc_purge_queue(&sk->sk_receive_queue);

	rxrpc_put_local(rx->local);
	rx->local = NULL;
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
	.family		= PF_RXRPC,
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
	.shutdown	= rxrpc_shutdown,
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
	.max_header	= sizeof(struct rxrpc_wire_header),
};

static const struct net_proto_family rxrpc_family_ops = {
	.family	= PF_RXRPC,
	.create = rxrpc_create,
	.owner	= THIS_MODULE,
};

/*
 * initialise and register the RxRPC protocol
 */
static int __init af_rxrpc_init(void)
{
	int ret = -1;
	unsigned int tmp;

	BUILD_BUG_ON(sizeof(struct rxrpc_skb_priv) > FIELD_SIZEOF(struct sk_buff, cb));

	get_random_bytes(&tmp, sizeof(tmp));
	tmp &= 0x3fffffff;
	if (tmp == 0)
		tmp = 1;
	idr_set_cursor(&rxrpc_client_conn_ids, tmp);

	ret = -ENOMEM;
	rxrpc_call_jar = kmem_cache_create(
		"rxrpc_call_jar", sizeof(struct rxrpc_call), 0,
		SLAB_HWCACHE_ALIGN, NULL);
	if (!rxrpc_call_jar) {
		pr_notice("Failed to allocate call jar\n");
		goto error_call_jar;
	}

	rxrpc_workqueue = alloc_workqueue("krxrpcd", 0, 1);
	if (!rxrpc_workqueue) {
		pr_notice("Failed to allocate work queue\n");
		goto error_work_queue;
	}

	ret = rxrpc_init_security();
	if (ret < 0) {
		pr_crit("Cannot initialise security\n");
		goto error_security;
	}

	ret = register_pernet_subsys(&rxrpc_net_ops);
	if (ret)
		goto error_pernet;

	ret = proto_register(&rxrpc_proto, 1);
	if (ret < 0) {
		pr_crit("Cannot register protocol\n");
		goto error_proto;
	}

	ret = sock_register(&rxrpc_family_ops);
	if (ret < 0) {
		pr_crit("Cannot register socket family\n");
		goto error_sock;
	}

	ret = register_key_type(&key_type_rxrpc);
	if (ret < 0) {
		pr_crit("Cannot register client key type\n");
		goto error_key_type;
	}

	ret = register_key_type(&key_type_rxrpc_s);
	if (ret < 0) {
		pr_crit("Cannot register server key type\n");
		goto error_key_type_s;
	}

	ret = rxrpc_sysctl_init();
	if (ret < 0) {
		pr_crit("Cannot register sysctls\n");
		goto error_sysctls;
	}

	return 0;

error_sysctls:
	unregister_key_type(&key_type_rxrpc_s);
error_key_type_s:
	unregister_key_type(&key_type_rxrpc);
error_key_type:
	sock_unregister(PF_RXRPC);
error_sock:
	proto_unregister(&rxrpc_proto);
error_proto:
	unregister_pernet_subsys(&rxrpc_net_ops);
error_pernet:
	rxrpc_exit_security();
error_security:
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
	rxrpc_sysctl_exit();
	unregister_key_type(&key_type_rxrpc_s);
	unregister_key_type(&key_type_rxrpc);
	sock_unregister(PF_RXRPC);
	proto_unregister(&rxrpc_proto);
	unregister_pernet_subsys(&rxrpc_net_ops);
	ASSERTCMP(atomic_read(&rxrpc_n_tx_skbs), ==, 0);
	ASSERTCMP(atomic_read(&rxrpc_n_rx_skbs), ==, 0);

	/* Make sure the local and peer records pinned by any dying connections
	 * are released.
	 */
	rcu_barrier();
	rxrpc_destroy_client_conn_ids();

	destroy_workqueue(rxrpc_workqueue);
	rxrpc_exit_security();
	kmem_cache_destroy(rxrpc_call_jar);
	_leave("");
}

module_init(af_rxrpc_init);
module_exit(af_rxrpc_exit);
