// SPDX-License-Identifier: GPL-2.0-or-later
/* Local endpoint object management
 *
 * Copyright (C) 2016 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/udp.h>
#include <linux/ip.h>
#include <linux/hashtable.h>
#include <net/sock.h>
#include <net/udp.h>
#include <net/udp_tunnel.h>
#include <net/af_rxrpc.h>
#include "ar-internal.h"

static void rxrpc_local_processor(struct work_struct *);
static void rxrpc_local_rcu(struct rcu_head *);

/*
 * Compare a local to an address.  Return -ve, 0 or +ve to indicate less than,
 * same or greater than.
 *
 * We explicitly don't compare the RxRPC service ID as we want to reject
 * conflicting uses by differing services.  Further, we don't want to share
 * addresses with different options (IPv6), so we don't compare those bits
 * either.
 */
static long rxrpc_local_cmp_key(const struct rxrpc_local *local,
				const struct sockaddr_rxrpc *srx)
{
	long diff;

	diff = ((local->srx.transport_type - srx->transport_type) ?:
		(local->srx.transport_len - srx->transport_len) ?:
		(local->srx.transport.family - srx->transport.family));
	if (diff != 0)
		return diff;

	switch (srx->transport.family) {
	case AF_INET:
		/* If the choice of UDP port is left up to the transport, then
		 * the endpoint record doesn't match.
		 */
		return ((u16 __force)local->srx.transport.sin.sin_port -
			(u16 __force)srx->transport.sin.sin_port) ?:
			memcmp(&local->srx.transport.sin.sin_addr,
			       &srx->transport.sin.sin_addr,
			       sizeof(struct in_addr));
#ifdef CONFIG_AF_RXRPC_IPV6
	case AF_INET6:
		/* If the choice of UDP6 port is left up to the transport, then
		 * the endpoint record doesn't match.
		 */
		return ((u16 __force)local->srx.transport.sin6.sin6_port -
			(u16 __force)srx->transport.sin6.sin6_port) ?:
			memcmp(&local->srx.transport.sin6.sin6_addr,
			       &srx->transport.sin6.sin6_addr,
			       sizeof(struct in6_addr));
#endif
	default:
		BUG();
	}
}

/*
 * Allocate a new local endpoint.
 */
static struct rxrpc_local *rxrpc_alloc_local(struct rxrpc_net *rxnet,
					     const struct sockaddr_rxrpc *srx)
{
	struct rxrpc_local *local;

	local = kzalloc(sizeof(struct rxrpc_local), GFP_KERNEL);
	if (local) {
		refcount_set(&local->ref, 1);
		atomic_set(&local->active_users, 1);
		local->rxnet = rxnet;
		INIT_HLIST_NODE(&local->link);
		INIT_WORK(&local->processor, rxrpc_local_processor);
		init_rwsem(&local->defrag_sem);
		skb_queue_head_init(&local->reject_queue);
		skb_queue_head_init(&local->event_queue);
		local->client_bundles = RB_ROOT;
		spin_lock_init(&local->client_bundles_lock);
		spin_lock_init(&local->lock);
		rwlock_init(&local->services_lock);
		local->debug_id = atomic_inc_return(&rxrpc_debug_id);
		memcpy(&local->srx, srx, sizeof(*srx));
		local->srx.srx_service = 0;
		trace_rxrpc_local(local->debug_id, rxrpc_local_new, 1, NULL);
	}

	_leave(" = %p", local);
	return local;
}

/*
 * create the local socket
 * - must be called with rxrpc_local_mutex locked
 */
static int rxrpc_open_socket(struct rxrpc_local *local, struct net *net)
{
	struct udp_tunnel_sock_cfg tuncfg = {NULL};
	struct sockaddr_rxrpc *srx = &local->srx;
	struct udp_port_cfg udp_conf = {0};
	struct sock *usk;
	int ret;

	_enter("%p{%d,%d}",
	       local, srx->transport_type, srx->transport.family);

	udp_conf.family = srx->transport.family;
	udp_conf.use_udp_checksums = true;
	if (udp_conf.family == AF_INET) {
		udp_conf.local_ip = srx->transport.sin.sin_addr;
		udp_conf.local_udp_port = srx->transport.sin.sin_port;
#if IS_ENABLED(CONFIG_AF_RXRPC_IPV6)
	} else {
		udp_conf.local_ip6 = srx->transport.sin6.sin6_addr;
		udp_conf.local_udp_port = srx->transport.sin6.sin6_port;
		udp_conf.use_udp6_tx_checksums = true;
		udp_conf.use_udp6_rx_checksums = true;
#endif
	}
	ret = udp_sock_create(net, &udp_conf, &local->socket);
	if (ret < 0) {
		_leave(" = %d [socket]", ret);
		return ret;
	}

	tuncfg.encap_type = UDP_ENCAP_RXRPC;
	tuncfg.encap_rcv = rxrpc_input_packet;
	tuncfg.encap_err_rcv = rxrpc_encap_err_rcv;
	tuncfg.sk_user_data = local;
	setup_udp_tunnel_sock(net, local->socket, &tuncfg);

	/* set the socket up */
	usk = local->socket->sk;
	usk->sk_error_report = rxrpc_error_report;

	switch (srx->transport.family) {
	case AF_INET6:
		/* we want to receive ICMPv6 errors */
		ip6_sock_set_recverr(usk);

		/* Fall through and set IPv4 options too otherwise we don't get
		 * errors from IPv4 packets sent through the IPv6 socket.
		 */
		fallthrough;
	case AF_INET:
		/* we want to receive ICMP errors */
		ip_sock_set_recverr(usk);

		/* we want to set the don't fragment bit */
		ip_sock_set_mtu_discover(usk, IP_PMTUDISC_DO);

		/* We want receive timestamps. */
		sock_enable_timestamps(usk);
		break;

	default:
		BUG();
	}

	_leave(" = 0");
	return 0;
}

/*
 * Look up or create a new local endpoint using the specified local address.
 */
struct rxrpc_local *rxrpc_lookup_local(struct net *net,
				       const struct sockaddr_rxrpc *srx)
{
	struct rxrpc_local *local;
	struct rxrpc_net *rxnet = rxrpc_net(net);
	struct hlist_node *cursor;
	const char *age;
	long diff;
	int ret;

	_enter("{%d,%d,%pISp}",
	       srx->transport_type, srx->transport.family, &srx->transport);

	mutex_lock(&rxnet->local_mutex);

	hlist_for_each(cursor, &rxnet->local_endpoints) {
		local = hlist_entry(cursor, struct rxrpc_local, link);

		diff = rxrpc_local_cmp_key(local, srx);
		if (diff != 0)
			continue;

		/* Services aren't allowed to share transport sockets, so
		 * reject that here.  It is possible that the object is dying -
		 * but it may also still have the local transport address that
		 * we want bound.
		 */
		if (srx->srx_service) {
			local = NULL;
			goto addr_in_use;
		}

		/* Found a match.  We want to replace a dying object.
		 * Attempting to bind the transport socket may still fail if
		 * we're attempting to use a local address that the dying
		 * object is still using.
		 */
		if (!rxrpc_use_local(local))
			break;

		age = "old";
		goto found;
	}

	local = rxrpc_alloc_local(rxnet, srx);
	if (!local)
		goto nomem;

	ret = rxrpc_open_socket(local, net);
	if (ret < 0)
		goto sock_error;

	if (cursor) {
		hlist_replace_rcu(cursor, &local->link);
		cursor->pprev = NULL;
	} else {
		hlist_add_head_rcu(&local->link, &rxnet->local_endpoints);
	}
	age = "new";

found:
	mutex_unlock(&rxnet->local_mutex);

	_net("LOCAL %s %d {%pISp}",
	     age, local->debug_id, &local->srx.transport);

	_leave(" = %p", local);
	return local;

nomem:
	ret = -ENOMEM;
sock_error:
	mutex_unlock(&rxnet->local_mutex);
	if (local)
		call_rcu(&local->rcu, rxrpc_local_rcu);
	_leave(" = %d", ret);
	return ERR_PTR(ret);

addr_in_use:
	mutex_unlock(&rxnet->local_mutex);
	_leave(" = -EADDRINUSE");
	return ERR_PTR(-EADDRINUSE);
}

/*
 * Get a ref on a local endpoint.
 */
struct rxrpc_local *rxrpc_get_local(struct rxrpc_local *local)
{
	const void *here = __builtin_return_address(0);
	int r;

	__refcount_inc(&local->ref, &r);
	trace_rxrpc_local(local->debug_id, rxrpc_local_got, r + 1, here);
	return local;
}

/*
 * Get a ref on a local endpoint unless its usage has already reached 0.
 */
struct rxrpc_local *rxrpc_get_local_maybe(struct rxrpc_local *local)
{
	const void *here = __builtin_return_address(0);
	int r;

	if (local) {
		if (__refcount_inc_not_zero(&local->ref, &r))
			trace_rxrpc_local(local->debug_id, rxrpc_local_got,
					  r + 1, here);
		else
			local = NULL;
	}
	return local;
}

/*
 * Queue a local endpoint and pass the caller's reference to the work item.
 */
void rxrpc_queue_local(struct rxrpc_local *local)
{
	const void *here = __builtin_return_address(0);
	unsigned int debug_id = local->debug_id;
	int r = refcount_read(&local->ref);

	if (rxrpc_queue_work(&local->processor))
		trace_rxrpc_local(debug_id, rxrpc_local_queued, r + 1, here);
	else
		rxrpc_put_local(local);
}

/*
 * Drop a ref on a local endpoint.
 */
void rxrpc_put_local(struct rxrpc_local *local)
{
	const void *here = __builtin_return_address(0);
	unsigned int debug_id;
	bool dead;
	int r;

	if (local) {
		debug_id = local->debug_id;

		dead = __refcount_dec_and_test(&local->ref, &r);
		trace_rxrpc_local(debug_id, rxrpc_local_put, r, here);

		if (dead)
			call_rcu(&local->rcu, rxrpc_local_rcu);
	}
}

/*
 * Start using a local endpoint.
 */
struct rxrpc_local *rxrpc_use_local(struct rxrpc_local *local)
{
	local = rxrpc_get_local_maybe(local);
	if (!local)
		return NULL;

	if (!__rxrpc_use_local(local)) {
		rxrpc_put_local(local);
		return NULL;
	}

	return local;
}

/*
 * Cease using a local endpoint.  Once the number of active users reaches 0, we
 * start the closure of the transport in the work processor.
 */
void rxrpc_unuse_local(struct rxrpc_local *local)
{
	if (local) {
		if (__rxrpc_unuse_local(local)) {
			rxrpc_get_local(local);
			rxrpc_queue_local(local);
		}
	}
}

/*
 * Destroy a local endpoint's socket and then hand the record to RCU to dispose
 * of.
 *
 * Closing the socket cannot be done from bottom half context or RCU callback
 * context because it might sleep.
 */
static void rxrpc_local_destroyer(struct rxrpc_local *local)
{
	struct socket *socket = local->socket;
	struct rxrpc_net *rxnet = local->rxnet;

	_enter("%d", local->debug_id);

	local->dead = true;

	mutex_lock(&rxnet->local_mutex);
	hlist_del_init_rcu(&local->link);
	mutex_unlock(&rxnet->local_mutex);

	rxrpc_clean_up_local_conns(local);
	rxrpc_service_connection_reaper(&rxnet->service_conn_reaper);
	ASSERT(!local->service);

	if (socket) {
		local->socket = NULL;
		kernel_sock_shutdown(socket, SHUT_RDWR);
		socket->sk->sk_user_data = NULL;
		sock_release(socket);
	}

	/* At this point, there should be no more packets coming in to the
	 * local endpoint.
	 */
	rxrpc_purge_queue(&local->reject_queue);
	rxrpc_purge_queue(&local->event_queue);
}

/*
 * Process events on an endpoint.  The work item carries a ref which
 * we must release.
 */
static void rxrpc_local_processor(struct work_struct *work)
{
	struct rxrpc_local *local =
		container_of(work, struct rxrpc_local, processor);
	bool again;

	if (local->dead)
		return;

	trace_rxrpc_local(local->debug_id, rxrpc_local_processing,
			  refcount_read(&local->ref), NULL);

	do {
		again = false;
		if (!__rxrpc_use_local(local)) {
			rxrpc_local_destroyer(local);
			break;
		}

		if (!skb_queue_empty(&local->reject_queue)) {
			rxrpc_reject_packets(local);
			again = true;
		}

		if (!skb_queue_empty(&local->event_queue)) {
			rxrpc_process_local_events(local);
			again = true;
		}

		__rxrpc_unuse_local(local);
	} while (again);

	rxrpc_put_local(local);
}

/*
 * Destroy a local endpoint after the RCU grace period expires.
 */
static void rxrpc_local_rcu(struct rcu_head *rcu)
{
	struct rxrpc_local *local = container_of(rcu, struct rxrpc_local, rcu);

	_enter("%d", local->debug_id);

	ASSERT(!work_pending(&local->processor));

	_net("DESTROY LOCAL %d", local->debug_id);
	kfree(local);
	_leave("");
}

/*
 * Verify the local endpoint list is empty by this point.
 */
void rxrpc_destroy_all_locals(struct rxrpc_net *rxnet)
{
	struct rxrpc_local *local;

	_enter("");

	flush_workqueue(rxrpc_workqueue);

	if (!hlist_empty(&rxnet->local_endpoints)) {
		mutex_lock(&rxnet->local_mutex);
		hlist_for_each_entry(local, &rxnet->local_endpoints, link) {
			pr_err("AF_RXRPC: Leaked local %p {%d}\n",
			       local, refcount_read(&local->ref));
		}
		mutex_unlock(&rxnet->local_mutex);
		BUG();
	}
}
