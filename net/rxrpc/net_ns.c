// SPDX-License-Identifier: GPL-2.0-or-later
/* rxrpc network namespace handling.
 *
 * Copyright (C) 2017 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/proc_fs.h>
#include "ar-internal.h"

unsigned int rxrpc_net_id;

static void rxrpc_client_conn_reap_timeout(struct timer_list *timer)
{
	struct rxrpc_net *rxnet =
		container_of(timer, struct rxrpc_net, client_conn_reap_timer);

	if (rxnet->live)
		rxrpc_queue_work(&rxnet->client_conn_reaper);
}

static void rxrpc_service_conn_reap_timeout(struct timer_list *timer)
{
	struct rxrpc_net *rxnet =
		container_of(timer, struct rxrpc_net, service_conn_reap_timer);

	if (rxnet->live)
		rxrpc_queue_work(&rxnet->service_conn_reaper);
}

static void rxrpc_peer_keepalive_timeout(struct timer_list *timer)
{
	struct rxrpc_net *rxnet =
		container_of(timer, struct rxrpc_net, peer_keepalive_timer);

	if (rxnet->live)
		rxrpc_queue_work(&rxnet->peer_keepalive_work);
}

/*
 * Initialise a per-network namespace record.
 */
static __net_init int rxrpc_init_net(struct net *net)
{
	struct rxrpc_net *rxnet = rxrpc_net(net);
	int ret, i;

	rxnet->live = true;
	get_random_bytes(&rxnet->epoch, sizeof(rxnet->epoch));
	rxnet->epoch |= RXRPC_RANDOM_EPOCH;

	INIT_LIST_HEAD(&rxnet->calls);
	rwlock_init(&rxnet->call_lock);
	atomic_set(&rxnet->nr_calls, 1);

	atomic_set(&rxnet->nr_conns, 1);
	INIT_LIST_HEAD(&rxnet->conn_proc_list);
	INIT_LIST_HEAD(&rxnet->service_conns);
	rwlock_init(&rxnet->conn_lock);
	INIT_WORK(&rxnet->service_conn_reaper,
		  rxrpc_service_connection_reaper);
	timer_setup(&rxnet->service_conn_reap_timer,
		    rxrpc_service_conn_reap_timeout, 0);

	atomic_set(&rxnet->nr_client_conns, 0);
	rxnet->kill_all_client_conns = false;
	spin_lock_init(&rxnet->client_conn_cache_lock);
	spin_lock_init(&rxnet->client_conn_discard_lock);
	INIT_LIST_HEAD(&rxnet->idle_client_conns);
	INIT_WORK(&rxnet->client_conn_reaper,
		  rxrpc_discard_expired_client_conns);
	timer_setup(&rxnet->client_conn_reap_timer,
		    rxrpc_client_conn_reap_timeout, 0);

	INIT_LIST_HEAD(&rxnet->local_endpoints);
	mutex_init(&rxnet->local_mutex);

	hash_init(rxnet->peer_hash);
	spin_lock_init(&rxnet->peer_hash_lock);
	for (i = 0; i < ARRAY_SIZE(rxnet->peer_keepalive); i++)
		INIT_LIST_HEAD(&rxnet->peer_keepalive[i]);
	INIT_LIST_HEAD(&rxnet->peer_keepalive_new);
	timer_setup(&rxnet->peer_keepalive_timer,
		    rxrpc_peer_keepalive_timeout, 0);
	INIT_WORK(&rxnet->peer_keepalive_work, rxrpc_peer_keepalive_worker);
	rxnet->peer_keepalive_base = ktime_get_seconds();

	ret = -ENOMEM;
	rxnet->proc_net = proc_net_mkdir(net, "rxrpc", net->proc_net);
	if (!rxnet->proc_net)
		goto err_proc;

	proc_create_net("calls", 0444, rxnet->proc_net, &rxrpc_call_seq_ops,
			sizeof(struct seq_net_private));
	proc_create_net("conns", 0444, rxnet->proc_net,
			&rxrpc_connection_seq_ops,
			sizeof(struct seq_net_private));
	proc_create_net("peers", 0444, rxnet->proc_net,
			&rxrpc_peer_seq_ops,
			sizeof(struct seq_net_private));
	return 0;

err_proc:
	rxnet->live = false;
	return ret;
}

/*
 * Clean up a per-network namespace record.
 */
static __net_exit void rxrpc_exit_net(struct net *net)
{
	struct rxrpc_net *rxnet = rxrpc_net(net);

	rxnet->live = false;
	del_timer_sync(&rxnet->peer_keepalive_timer);
	cancel_work_sync(&rxnet->peer_keepalive_work);
	/* Remove the timer again as the worker may have restarted it. */
	del_timer_sync(&rxnet->peer_keepalive_timer);
	rxrpc_destroy_all_calls(rxnet);
	rxrpc_destroy_all_connections(rxnet);
	rxrpc_destroy_all_peers(rxnet);
	rxrpc_destroy_all_locals(rxnet);
	proc_remove(rxnet->proc_net);
}

struct pernet_operations rxrpc_net_ops = {
	.init	= rxrpc_init_net,
	.exit	= rxrpc_exit_net,
	.id	= &rxrpc_net_id,
	.size	= sizeof(struct rxrpc_net),
};
