/* Client connection-specific management code.
 *
 * Copyright (C) 2016 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/slab.h>
#include <linux/idr.h>
#include <linux/timer.h>
#include "ar-internal.h"

/*
 * We use machine-unique IDs for our client connections.
 */
DEFINE_IDR(rxrpc_client_conn_ids);
static DEFINE_SPINLOCK(rxrpc_conn_id_lock);

/*
 * Get a connection ID and epoch for a client connection from the global pool.
 * The connection struct pointer is then recorded in the idr radix tree.  The
 * epoch is changed if this wraps.
 *
 * TODO: The IDR tree gets very expensive on memory if the connection IDs are
 * widely scattered throughout the number space, so we shall need to retire
 * connections that have, say, an ID more than four times the maximum number of
 * client conns away from the current allocation point to try and keep the IDs
 * concentrated.  We will also need to retire connections from an old epoch.
 */
int rxrpc_get_client_connection_id(struct rxrpc_connection *conn, gfp_t gfp)
{
	u32 epoch;
	int id;

	_enter("");

	idr_preload(gfp);
	spin_lock(&rxrpc_conn_id_lock);

	epoch = rxrpc_epoch;

	/* We could use idr_alloc_cyclic() here, but we really need to know
	 * when the thing wraps so that we can advance the epoch.
	 */
	if (rxrpc_client_conn_ids.cur == 0)
		rxrpc_client_conn_ids.cur = 1;
	id = idr_alloc(&rxrpc_client_conn_ids, conn,
		       rxrpc_client_conn_ids.cur, 0x40000000, GFP_NOWAIT);
	if (id < 0) {
		if (id != -ENOSPC)
			goto error;
		id = idr_alloc(&rxrpc_client_conn_ids, conn,
			       1, 0x40000000, GFP_NOWAIT);
		if (id < 0)
			goto error;
		epoch++;
		rxrpc_epoch = epoch;
	}
	rxrpc_client_conn_ids.cur = id + 1;

	spin_unlock(&rxrpc_conn_id_lock);
	idr_preload_end();

	conn->proto.epoch = epoch;
	conn->proto.cid = id << RXRPC_CIDSHIFT;
	set_bit(RXRPC_CONN_HAS_IDR, &conn->flags);
	_leave(" [CID %x:%x]", epoch, conn->proto.cid);
	return 0;

error:
	spin_unlock(&rxrpc_conn_id_lock);
	idr_preload_end();
	_leave(" = %d", id);
	return id;
}

/*
 * Release a connection ID for a client connection from the global pool.
 */
void rxrpc_put_client_connection_id(struct rxrpc_connection *conn)
{
	if (test_bit(RXRPC_CONN_HAS_IDR, &conn->flags)) {
		spin_lock(&rxrpc_conn_id_lock);
		idr_remove(&rxrpc_client_conn_ids,
			   conn->proto.cid >> RXRPC_CIDSHIFT);
		spin_unlock(&rxrpc_conn_id_lock);
	}
}

/*
 * Destroy the client connection ID tree.
 */
void rxrpc_destroy_client_conn_ids(void)
{
	struct rxrpc_connection *conn;
	int id;

	if (!idr_is_empty(&rxrpc_client_conn_ids)) {
		idr_for_each_entry(&rxrpc_client_conn_ids, conn, id) {
			pr_err("AF_RXRPC: Leaked client conn %p {%d}\n",
			       conn, atomic_read(&conn->usage));
		}
		BUG();
	}

	idr_destroy(&rxrpc_client_conn_ids);
}
