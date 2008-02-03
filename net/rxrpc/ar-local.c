/* AF_RXRPC local endpoint management
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
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include "ar-internal.h"

static LIST_HEAD(rxrpc_locals);
DEFINE_RWLOCK(rxrpc_local_lock);
static DECLARE_RWSEM(rxrpc_local_sem);
static DECLARE_WAIT_QUEUE_HEAD(rxrpc_local_wq);

static void rxrpc_destroy_local(struct work_struct *work);

/*
 * allocate a new local
 */
static
struct rxrpc_local *rxrpc_alloc_local(struct sockaddr_rxrpc *srx)
{
	struct rxrpc_local *local;

	local = kzalloc(sizeof(struct rxrpc_local), GFP_KERNEL);
	if (local) {
		INIT_WORK(&local->destroyer, &rxrpc_destroy_local);
		INIT_WORK(&local->acceptor, &rxrpc_accept_incoming_calls);
		INIT_WORK(&local->rejecter, &rxrpc_reject_packets);
		INIT_LIST_HEAD(&local->services);
		INIT_LIST_HEAD(&local->link);
		init_rwsem(&local->defrag_sem);
		skb_queue_head_init(&local->accept_queue);
		skb_queue_head_init(&local->reject_queue);
		spin_lock_init(&local->lock);
		rwlock_init(&local->services_lock);
		atomic_set(&local->usage, 1);
		local->debug_id = atomic_inc_return(&rxrpc_debug_id);
		memcpy(&local->srx, srx, sizeof(*srx));
	}

	_leave(" = %p", local);
	return local;
}

/*
 * create the local socket
 * - must be called with rxrpc_local_sem writelocked
 */
static int rxrpc_create_local(struct rxrpc_local *local)
{
	struct sock *sock;
	int ret, opt;

	_enter("%p{%d}", local, local->srx.transport_type);

	/* create a socket to represent the local endpoint */
	ret = sock_create_kern(PF_INET, local->srx.transport_type, IPPROTO_UDP,
			       &local->socket);
	if (ret < 0) {
		_leave(" = %d [socket]", ret);
		return ret;
	}

	/* if a local address was supplied then bind it */
	if (local->srx.transport_len > sizeof(sa_family_t)) {
		_debug("bind");
		ret = kernel_bind(local->socket,
				  (struct sockaddr *) &local->srx.transport,
				  local->srx.transport_len);
		if (ret < 0) {
			_debug("bind failed");
			goto error;
		}
	}

	/* we want to receive ICMP errors */
	opt = 1;
	ret = kernel_setsockopt(local->socket, SOL_IP, IP_RECVERR,
				(char *) &opt, sizeof(opt));
	if (ret < 0) {
		_debug("setsockopt failed");
		goto error;
	}

	/* we want to set the don't fragment bit */
	opt = IP_PMTUDISC_DO;
	ret = kernel_setsockopt(local->socket, SOL_IP, IP_MTU_DISCOVER,
				(char *) &opt, sizeof(opt));
	if (ret < 0) {
		_debug("setsockopt failed");
		goto error;
	}

	write_lock_bh(&rxrpc_local_lock);
	list_add(&local->link, &rxrpc_locals);
	write_unlock_bh(&rxrpc_local_lock);

	/* set the socket up */
	sock = local->socket->sk;
	sock->sk_user_data	= local;
	sock->sk_data_ready	= rxrpc_data_ready;
	sock->sk_error_report	= rxrpc_UDP_error_report;
	_leave(" = 0");
	return 0;

error:
	kernel_sock_shutdown(local->socket, SHUT_RDWR);
	local->socket->sk->sk_user_data = NULL;
	sock_release(local->socket);
	local->socket = NULL;

	_leave(" = %d", ret);
	return ret;
}

/*
 * create a new local endpoint using the specified UDP address
 */
struct rxrpc_local *rxrpc_lookup_local(struct sockaddr_rxrpc *srx)
{
	struct rxrpc_local *local;
	int ret;

	_enter("{%d,%u,%u.%u.%u.%u+%hu}",
	       srx->transport_type,
	       srx->transport.family,
	       NIPQUAD(srx->transport.sin.sin_addr),
	       ntohs(srx->transport.sin.sin_port));

	down_write(&rxrpc_local_sem);

	/* see if we have a suitable local local endpoint already */
	read_lock_bh(&rxrpc_local_lock);

	list_for_each_entry(local, &rxrpc_locals, link) {
		_debug("CMP {%d,%u,%u.%u.%u.%u+%hu}",
		       local->srx.transport_type,
		       local->srx.transport.family,
		       NIPQUAD(local->srx.transport.sin.sin_addr),
		       ntohs(local->srx.transport.sin.sin_port));

		if (local->srx.transport_type != srx->transport_type ||
		    local->srx.transport.family != srx->transport.family)
			continue;

		switch (srx->transport.family) {
		case AF_INET:
			if (local->srx.transport.sin.sin_port !=
			    srx->transport.sin.sin_port)
				continue;
			if (memcmp(&local->srx.transport.sin.sin_addr,
				   &srx->transport.sin.sin_addr,
				   sizeof(struct in_addr)) != 0)
				continue;
			goto found_local;

		default:
			BUG();
		}
	}

	read_unlock_bh(&rxrpc_local_lock);

	/* we didn't find one, so we need to create one */
	local = rxrpc_alloc_local(srx);
	if (!local) {
		up_write(&rxrpc_local_sem);
		return ERR_PTR(-ENOMEM);
	}

	ret = rxrpc_create_local(local);
	if (ret < 0) {
		up_write(&rxrpc_local_sem);
		kfree(local);
		_leave(" = %d", ret);
		return ERR_PTR(ret);
	}

	up_write(&rxrpc_local_sem);

	_net("LOCAL new %d {%d,%u,%u.%u.%u.%u+%hu}",
	     local->debug_id,
	     local->srx.transport_type,
	     local->srx.transport.family,
	     NIPQUAD(local->srx.transport.sin.sin_addr),
	     ntohs(local->srx.transport.sin.sin_port));

	_leave(" = %p [new]", local);
	return local;

found_local:
	rxrpc_get_local(local);
	read_unlock_bh(&rxrpc_local_lock);
	up_write(&rxrpc_local_sem);

	_net("LOCAL old %d {%d,%u,%u.%u.%u.%u+%hu}",
	     local->debug_id,
	     local->srx.transport_type,
	     local->srx.transport.family,
	     NIPQUAD(local->srx.transport.sin.sin_addr),
	     ntohs(local->srx.transport.sin.sin_port));

	_leave(" = %p [reuse]", local);
	return local;
}

/*
 * release a local endpoint
 */
void rxrpc_put_local(struct rxrpc_local *local)
{
	_enter("%p{u=%d}", local, atomic_read(&local->usage));

	ASSERTCMP(atomic_read(&local->usage), >, 0);

	/* to prevent a race, the decrement and the dequeue must be effectively
	 * atomic */
	write_lock_bh(&rxrpc_local_lock);
	if (unlikely(atomic_dec_and_test(&local->usage))) {
		_debug("destroy local");
		rxrpc_queue_work(&local->destroyer);
	}
	write_unlock_bh(&rxrpc_local_lock);
	_leave("");
}

/*
 * destroy a local endpoint
 */
static void rxrpc_destroy_local(struct work_struct *work)
{
	struct rxrpc_local *local =
		container_of(work, struct rxrpc_local, destroyer);

	_enter("%p{%d}", local, atomic_read(&local->usage));

	down_write(&rxrpc_local_sem);

	write_lock_bh(&rxrpc_local_lock);
	if (atomic_read(&local->usage) > 0) {
		write_unlock_bh(&rxrpc_local_lock);
		up_read(&rxrpc_local_sem);
		_leave(" [resurrected]");
		return;
	}

	list_del(&local->link);
	local->socket->sk->sk_user_data = NULL;
	write_unlock_bh(&rxrpc_local_lock);

	downgrade_write(&rxrpc_local_sem);

	ASSERT(list_empty(&local->services));
	ASSERT(!work_pending(&local->acceptor));
	ASSERT(!work_pending(&local->rejecter));

	/* finish cleaning up the local descriptor */
	rxrpc_purge_queue(&local->accept_queue);
	rxrpc_purge_queue(&local->reject_queue);
	kernel_sock_shutdown(local->socket, SHUT_RDWR);
	sock_release(local->socket);

	up_read(&rxrpc_local_sem);

	_net("DESTROY LOCAL %d", local->debug_id);
	kfree(local);

	if (list_empty(&rxrpc_locals))
		wake_up_all(&rxrpc_local_wq);

	_leave("");
}

/*
 * preemptively destroy all local local endpoint rather than waiting for
 * them to be destroyed
 */
void __exit rxrpc_destroy_all_locals(void)
{
	DECLARE_WAITQUEUE(myself,current);

	_enter("");

	/* we simply have to wait for them to go away */
	if (!list_empty(&rxrpc_locals)) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		add_wait_queue(&rxrpc_local_wq, &myself);

		while (!list_empty(&rxrpc_locals)) {
			schedule();
			set_current_state(TASK_UNINTERRUPTIBLE);
		}

		remove_wait_queue(&rxrpc_local_wq, &myself);
		set_current_state(TASK_RUNNING);
	}

	_leave("");
}
