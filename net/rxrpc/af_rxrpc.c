// SPDX-License-Identifier: GPL-2.0-or-later
/* AF_RXRPC implementation
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
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
module_param_named(debug, rxrpc_debug, uint, 0644);
MODULE_PARM_DESC(debug, "RxRPC debugging mask");

static struct proto rxrpc_proto;
static const struct proto_ops rxrpc_rpc_ops;

/* current debugging ID */
atomic_t rxrpc_debug_id;
EXPORT_SYMBOL(rxrpc_debug_id);

/* count of skbs currently in use */
atomic_t rxrpc_n_rx_skbs;

struct workqueue_struct *rxrpc_workqueue;

static void rxrpc_sock_destructor(struct sock *);

/*
 * see if an RxRPC socket is currently writable
 */
static inline int rxrpc_writable(struct sock *sk)
{
	return refcount_read(&sk->sk_wmem_alloc) < (size_t) sk->sk_sndbuf;
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
		sk_wake_async_rcu(sk, SOCK_WAKE_SPACE, POLL_OUT);
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

	switch (srx->transport.family) {
	case AF_INET:
		if (rx->family != AF_INET &&
		    rx->family != AF_INET6)
			return -EAFNOSUPPORT;
		if (srx->transport_len < sizeof(struct sockaddr_in))
			return -EINVAL;
		tail = offsetof(struct sockaddr_rxrpc, transport.sin.__pad);
		break;

#ifdef CONFIG_AF_RXRPC_IPV6
	case AF_INET6:
		if (rx->family != AF_INET6)
			return -EAFNOSUPPORT;
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
	u16 service_id;
	int ret;

	_enter("%p,%p,%d", rx, saddr, len);

	ret = rxrpc_validate_address(rx, srx, len);
	if (ret < 0)
		goto error;
	service_id = srx->srx_service;

	lock_sock(&rx->sk);

	switch (rx->sk.sk_state) {
	case RXRPC_UNBOUND:
		rx->srx = *srx;
		local = rxrpc_lookup_local(sock_net(&rx->sk), &rx->srx);
		if (IS_ERR(local)) {
			ret = PTR_ERR(local);
			goto error_unlock;
		}

		if (service_id) {
			write_lock(&local->services_lock);
			if (local->service)
				goto service_in_use;
			rx->local = local;
			local->service = rx;
			write_unlock(&local->services_lock);

			rx->sk.sk_state = RXRPC_SERVER_BOUND;
		} else {
			rx->local = local;
			rx->sk.sk_state = RXRPC_CLIENT_BOUND;
		}
		break;

	case RXRPC_SERVER_BOUND:
		ret = -EINVAL;
		if (service_id == 0)
			goto error_unlock;
		ret = -EADDRINUSE;
		if (service_id == rx->srx.srx_service)
			goto error_unlock;
		ret = -EINVAL;
		srx->srx_service = rx->srx.srx_service;
		if (memcmp(srx, &rx->srx, sizeof(*srx)) != 0)
			goto error_unlock;
		rx->second_service = service_id;
		rx->sk.sk_state = RXRPC_SERVER_BOUND2;
		break;

	default:
		ret = -EINVAL;
		goto error_unlock;
	}

	release_sock(&rx->sk);
	_leave(" = 0");
	return 0;

service_in_use:
	write_unlock(&local->services_lock);
	rxrpc_unuse_local(local, rxrpc_local_unuse_bind);
	rxrpc_put_local(local, rxrpc_local_put_bind);
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
	case RXRPC_SERVER_BOUND2:
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
		fallthrough;
	default:
		ret = -EBUSY;
		break;
	}

	release_sock(&rx->sk);
	_leave(" = %d", ret);
	return ret;
}

/**
 * rxrpc_kernel_lookup_peer - Obtain remote transport endpoint for an address
 * @sock: The socket through which it will be accessed
 * @srx: The network address
 * @gfp: Allocation flags
 *
 * Lookup or create a remote transport endpoint record for the specified
 * address and return it with a ref held.
 */
struct rxrpc_peer *rxrpc_kernel_lookup_peer(struct socket *sock,
					    struct sockaddr_rxrpc *srx, gfp_t gfp)
{
	struct rxrpc_sock *rx = rxrpc_sk(sock->sk);
	int ret;

	ret = rxrpc_validate_address(rx, srx, sizeof(*srx));
	if (ret < 0)
		return ERR_PTR(ret);

	return rxrpc_lookup_peer(rx->local, srx, gfp);
}
EXPORT_SYMBOL(rxrpc_kernel_lookup_peer);

/**
 * rxrpc_kernel_get_peer - Get a reference on a peer
 * @peer: The peer to get a reference on.
 *
 * Get a record for the remote peer in a call.
 */
struct rxrpc_peer *rxrpc_kernel_get_peer(struct rxrpc_peer *peer)
{
	return peer ? rxrpc_get_peer(peer, rxrpc_peer_get_application) : NULL;
}
EXPORT_SYMBOL(rxrpc_kernel_get_peer);

/**
 * rxrpc_kernel_put_peer - Allow a kernel app to drop a peer reference
 * @peer: The peer to drop a ref on
 */
void rxrpc_kernel_put_peer(struct rxrpc_peer *peer)
{
	rxrpc_put_peer(peer, rxrpc_peer_put_application);
}
EXPORT_SYMBOL(rxrpc_kernel_put_peer);

/**
 * rxrpc_kernel_begin_call - Allow a kernel service to begin a call
 * @sock: The socket on which to make the call
 * @peer: The peer to contact
 * @key: The security context to use (defaults to socket setting)
 * @user_call_ID: The ID to use
 * @tx_total_len: Total length of data to transmit during the call (or -1)
 * @hard_timeout: The maximum lifespan of the call in sec
 * @gfp: The allocation constraints
 * @notify_rx: Where to send notifications instead of socket queue
 * @service_id: The ID of the service to contact
 * @upgrade: Request service upgrade for call
 * @interruptibility: The call is interruptible, or can be canceled.
 * @debug_id: The debug ID for tracing to be assigned to the call
 *
 * Allow a kernel service to begin a call on the nominated socket.  This just
 * sets up all the internal tracking structures and allocates connection and
 * call IDs as appropriate.  The call to be used is returned.
 *
 * The default socket destination address and security may be overridden by
 * supplying @srx and @key.
 */
struct rxrpc_call *rxrpc_kernel_begin_call(struct socket *sock,
					   struct rxrpc_peer *peer,
					   struct key *key,
					   unsigned long user_call_ID,
					   s64 tx_total_len,
					   u32 hard_timeout,
					   gfp_t gfp,
					   rxrpc_notify_rx_t notify_rx,
					   u16 service_id,
					   bool upgrade,
					   enum rxrpc_interruptibility interruptibility,
					   unsigned int debug_id)
{
	struct rxrpc_conn_parameters cp;
	struct rxrpc_call_params p;
	struct rxrpc_call *call;
	struct rxrpc_sock *rx = rxrpc_sk(sock->sk);

	_enter(",,%x,%lx", key_serial(key), user_call_ID);

	if (WARN_ON_ONCE(peer->local != rx->local))
		return ERR_PTR(-EIO);

	lock_sock(&rx->sk);

	if (!key)
		key = rx->key;
	if (key && !key->payload.data[0])
		key = NULL; /* a no-security key */

	memset(&p, 0, sizeof(p));
	p.user_call_ID		= user_call_ID;
	p.tx_total_len		= tx_total_len;
	p.interruptibility	= interruptibility;
	p.kernel		= true;
	p.timeouts.hard		= hard_timeout;

	memset(&cp, 0, sizeof(cp));
	cp.local		= rx->local;
	cp.peer			= peer;
	cp.key			= key;
	cp.security_level	= rx->min_sec_level;
	cp.exclusive		= false;
	cp.upgrade		= upgrade;
	cp.service_id		= service_id;
	call = rxrpc_new_client_call(rx, &cp, &p, gfp, debug_id);
	/* The socket has been unlocked. */
	if (!IS_ERR(call)) {
		call->notify_rx = notify_rx;
		mutex_unlock(&call->user_mutex);
	}

	_leave(" = %p", call);
	return call;
}
EXPORT_SYMBOL(rxrpc_kernel_begin_call);

/*
 * Dummy function used to stop the notifier talking to recvmsg().
 */
static void rxrpc_dummy_notify_rx(struct sock *sk, struct rxrpc_call *rxcall,
				  unsigned long call_user_ID)
{
}

/**
 * rxrpc_kernel_shutdown_call - Allow a kernel service to shut down a call it was using
 * @sock: The socket the call is on
 * @call: The call to end
 *
 * Allow a kernel service to shut down a call it was using.  The call must be
 * complete before this is called (the call should be aborted if necessary).
 */
void rxrpc_kernel_shutdown_call(struct socket *sock, struct rxrpc_call *call)
{
	_enter("%d{%d}", call->debug_id, refcount_read(&call->ref));

	mutex_lock(&call->user_mutex);
	if (!test_bit(RXRPC_CALL_RELEASED, &call->flags)) {
		rxrpc_release_call(rxrpc_sk(sock->sk), call);

		/* Make sure we're not going to call back into a kernel service */
		if (call->notify_rx) {
			spin_lock_irq(&call->notify_lock);
			call->notify_rx = rxrpc_dummy_notify_rx;
			spin_unlock_irq(&call->notify_lock);
		}
	}
	mutex_unlock(&call->user_mutex);
}
EXPORT_SYMBOL(rxrpc_kernel_shutdown_call);

/**
 * rxrpc_kernel_put_call - Release a reference to a call
 * @sock: The socket the call is on
 * @call: The call to put
 *
 * Drop the application's ref on an rxrpc call.
 */
void rxrpc_kernel_put_call(struct socket *sock, struct rxrpc_call *call)
{
	rxrpc_put_call(call, rxrpc_call_put_kernel);
}
EXPORT_SYMBOL(rxrpc_kernel_put_call);

/**
 * rxrpc_kernel_check_life - Check to see whether a call is still alive
 * @sock: The socket the call is on
 * @call: The call to check
 *
 * Allow a kernel service to find out whether a call is still alive - whether
 * it has completed successfully and all received data has been consumed.
 */
bool rxrpc_kernel_check_life(const struct socket *sock,
			     const struct rxrpc_call *call)
{
	if (!rxrpc_call_is_complete(call))
		return true;
	if (call->completion != RXRPC_CALL_SUCCEEDED)
		return false;
	return !skb_queue_empty(&call->recvmsg_queue);
}
EXPORT_SYMBOL(rxrpc_kernel_check_life);

/**
 * rxrpc_kernel_get_epoch - Retrieve the epoch value from a call.
 * @sock: The socket the call is on
 * @call: The call to query
 *
 * Allow a kernel service to retrieve the epoch value from a service call to
 * see if the client at the other end rebooted.
 */
u32 rxrpc_kernel_get_epoch(struct socket *sock, struct rxrpc_call *call)
{
	return call->conn->proto.epoch;
}
EXPORT_SYMBOL(rxrpc_kernel_get_epoch);

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

/**
 * rxrpc_kernel_set_max_life - Set maximum lifespan on a call
 * @sock: The socket the call is on
 * @call: The call to configure
 * @hard_timeout: The maximum lifespan of the call in ms
 *
 * Set the maximum lifespan of a call.  The call will end with ETIME or
 * ETIMEDOUT if it takes longer than this.
 */
void rxrpc_kernel_set_max_life(struct socket *sock, struct rxrpc_call *call,
			       unsigned long hard_timeout)
{
	ktime_t delay = ms_to_ktime(hard_timeout), expect_term_by;

	mutex_lock(&call->user_mutex);

	expect_term_by = ktime_add(ktime_get_real(), delay);
	WRITE_ONCE(call->expect_term_by, expect_term_by);
	trace_rxrpc_timer_set(call, delay, rxrpc_timer_trace_hard);
	rxrpc_poke_call(call, rxrpc_call_poke_set_timeout);

	mutex_unlock(&call->user_mutex);
}
EXPORT_SYMBOL(rxrpc_kernel_set_max_life);

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
		break;
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
	case RXRPC_CLIENT_UNBOUND:
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
		rx->sk.sk_state = RXRPC_CLIENT_BOUND;
		fallthrough;

	case RXRPC_CLIENT_BOUND:
		if (!m->msg_name &&
		    test_bit(RXRPC_SOCK_CONNECTED, &rx->flags)) {
			m->msg_name = &rx->connect_srx;
			m->msg_namelen = sizeof(rx->connect_srx);
		}
		fallthrough;
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

int rxrpc_sock_set_min_security_level(struct sock *sk, unsigned int val)
{
	if (sk->sk_state != RXRPC_UNBOUND)
		return -EISCONN;
	if (val > RXRPC_SECURITY_MAX)
		return -EINVAL;
	lock_sock(sk);
	rxrpc_sk(sk)->min_sec_level = val;
	release_sock(sk);
	return 0;
}
EXPORT_SYMBOL(rxrpc_sock_set_min_security_level);

/*
 * set RxRPC socket options
 */
static int rxrpc_setsockopt(struct socket *sock, int level, int optname,
			    sockptr_t optval, unsigned int optlen)
{
	struct rxrpc_sock *rx = rxrpc_sk(sock->sk);
	unsigned int min_sec_level;
	u16 service_upgrade[2];
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
			ret = copy_safe_from_sockptr(&min_sec_level,
						     sizeof(min_sec_level),
						     optval, optlen);
			if (ret)
				goto error;
			ret = -EINVAL;
			if (min_sec_level > RXRPC_SECURITY_MAX)
				goto error;
			rx->min_sec_level = min_sec_level;
			goto success;

		case RXRPC_UPGRADEABLE_SERVICE:
			ret = -EINVAL;
			if (optlen != sizeof(service_upgrade) ||
			    rx->service_upgrade.from != 0)
				goto error;
			ret = -EISCONN;
			if (rx->sk.sk_state != RXRPC_SERVER_BOUND2)
				goto error;
			ret = -EFAULT;
			if (copy_from_sockptr(service_upgrade, optval,
					   sizeof(service_upgrade)) != 0)
				goto error;
			ret = -EINVAL;
			if ((service_upgrade[0] != rx->srx.srx_service ||
			     service_upgrade[1] != rx->second_service) &&
			    (service_upgrade[0] != rx->second_service ||
			     service_upgrade[1] != rx->srx.srx_service))
				goto error;
			rx->service_upgrade.from = service_upgrade[0];
			rx->service_upgrade.to = service_upgrade[1];
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
 * Get socket options.
 */
static int rxrpc_getsockopt(struct socket *sock, int level, int optname,
			    char __user *optval, int __user *_optlen)
{
	int optlen;

	if (level != SOL_RXRPC)
		return -EOPNOTSUPP;

	if (get_user(optlen, _optlen))
		return -EFAULT;

	switch (optname) {
	case RXRPC_SUPPORTED_CMSG:
		if (optlen < sizeof(int))
			return -ETOOSMALL;
		if (put_user(RXRPC__SUPPORTED - 1, (int __user *)optval) ||
		    put_user(sizeof(int), _optlen))
			return -EFAULT;
		return 0;

	default:
		return -EOPNOTSUPP;
	}
}

/*
 * permit an RxRPC socket to be polled
 */
static __poll_t rxrpc_poll(struct file *file, struct socket *sock,
			       poll_table *wait)
{
	struct sock *sk = sock->sk;
	struct rxrpc_sock *rx = rxrpc_sk(sk);
	__poll_t mask;

	sock_poll_wait(file, sock, wait);
	mask = 0;

	/* the socket is readable if there are any messages waiting on the Rx
	 * queue */
	if (!list_empty(&rx->recvmsg_q))
		mask |= EPOLLIN | EPOLLRDNORM;

	/* the socket is writable if there is space to add new data to the
	 * socket; there is no guarantee that any particular call in progress
	 * on the socket may have space in the Tx ACK window */
	if (rxrpc_writable(sk))
		mask |= EPOLLOUT | EPOLLWRNORM;

	return mask;
}

/*
 * create an RxRPC socket
 */
static int rxrpc_create(struct net *net, struct socket *sock, int protocol,
			int kern)
{
	struct rxrpc_net *rxnet;
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
	spin_lock_init(&rx->recvmsg_lock);
	rwlock_init(&rx->call_lock);
	memset(&rx->srx, 0, sizeof(rx->srx));

	rxnet = rxrpc_net(sock_net(&rx->sk));
	timer_reduce(&rxnet->peer_keepalive_timer, jiffies + 1);

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

	if (sk->sk_state < RXRPC_CLOSE) {
		sk->sk_state = RXRPC_CLOSE;
		sk->sk_shutdown = SHUTDOWN_MASK;
	} else {
		ret = -ESHUTDOWN;
	}

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

	WARN_ON(refcount_read(&sk->sk_wmem_alloc));
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

	_enter("%p{%d,%d}", sk, sk->sk_state, refcount_read(&sk->sk_refcnt));

	/* declare the socket closed for business */
	sock_orphan(sk);
	sk->sk_shutdown = SHUTDOWN_MASK;

	/* We want to kill off all connections from a service socket
	 * as fast as possible because we can't share these; client
	 * sockets, on the other hand, can share an endpoint.
	 */
	switch (sk->sk_state) {
	case RXRPC_SERVER_BOUND:
	case RXRPC_SERVER_BOUND2:
	case RXRPC_SERVER_LISTENING:
	case RXRPC_SERVER_LISTEN_DISABLED:
		rx->local->service_closed = true;
		break;
	}

	sk->sk_state = RXRPC_CLOSE;

	if (rx->local && rx->local->service == rx) {
		write_lock(&rx->local->services_lock);
		rx->local->service = NULL;
		write_unlock(&rx->local->services_lock);
	}

	/* try to flush out this socket */
	rxrpc_discard_prealloc(rx);
	rxrpc_release_calls_on_socket(rx);
	flush_workqueue(rxrpc_workqueue);
	rxrpc_purge_queue(&sk->sk_receive_queue);

	rxrpc_unuse_local(rx->local, rxrpc_local_unuse_release_sock);
	rxrpc_put_local(rx->local, rxrpc_local_put_release_sock);
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
	.getsockopt	= rxrpc_getsockopt,
	.sendmsg	= rxrpc_sendmsg,
	.recvmsg	= rxrpc_recvmsg,
	.mmap		= sock_no_mmap,
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

	BUILD_BUG_ON(sizeof(struct rxrpc_skb_priv) > sizeof_field(struct sk_buff, cb));

	ret = -ENOMEM;
	rxrpc_gen_version_string();
	rxrpc_call_jar = kmem_cache_create(
		"rxrpc_call_jar", sizeof(struct rxrpc_call), 0,
		SLAB_HWCACHE_ALIGN, NULL);
	if (!rxrpc_call_jar) {
		pr_notice("Failed to allocate call jar\n");
		goto error_call_jar;
	}

	rxrpc_workqueue = alloc_ordered_workqueue("krxrpcd", WQ_HIGHPRI | WQ_MEM_RECLAIM);
	if (!rxrpc_workqueue) {
		pr_notice("Failed to allocate work queue\n");
		goto error_work_queue;
	}

	ret = rxrpc_init_security();
	if (ret < 0) {
		pr_crit("Cannot initialise security\n");
		goto error_security;
	}

	ret = register_pernet_device(&rxrpc_net_ops);
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
	unregister_pernet_device(&rxrpc_net_ops);
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
	unregister_pernet_device(&rxrpc_net_ops);
	ASSERTCMP(atomic_read(&rxrpc_n_rx_skbs), ==, 0);

	/* Make sure the local and peer records pinned by any dying connections
	 * are released.
	 */
	rcu_barrier();

	destroy_workqueue(rxrpc_workqueue);
	rxrpc_exit_security();
	kmem_cache_destroy(rxrpc_call_jar);
	_leave("");
}

module_init(af_rxrpc_init);
module_exit(af_rxrpc_exit);
