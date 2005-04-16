/* peer.h: Rx RPC per-transport peer record
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_RXRPC_PEER_H
#define _LINUX_RXRPC_PEER_H

#include <linux/wait.h>
#include <rxrpc/types.h>
#include <rxrpc/krxtimod.h>

struct rxrpc_peer_ops
{
	/* peer record being added */
	int (*adding)(struct rxrpc_peer *peer);

	/* peer record being discarded from graveyard */
	void (*discarding)(struct rxrpc_peer *peer);

	/* change of epoch detected on connection */
	void (*change_of_epoch)(struct rxrpc_connection *conn);
};

/*****************************************************************************/
/*
 * Rx RPC per-transport peer record
 * - peers only retain a refcount on the transport when they are active
 * - peers with refcount==0 are inactive and reside in the transport's graveyard
 */
struct rxrpc_peer
{
	atomic_t		usage;
	struct rxrpc_peer_ops	*ops;		/* operations on this peer */
	struct rxrpc_transport	*trans;		/* owner transport */
	struct rxrpc_timer	timeout;	/* timeout for grave destruction */
	struct list_head	link;		/* link in transport's peer list */
	struct list_head	proc_link;	/* link in /proc list */
	rwlock_t		conn_idlock;	/* lock for connection IDs */
	struct list_head	conn_idlist;	/* list of connections granted IDs */
	uint32_t		conn_idcounter;	/* connection ID counter */
	rwlock_t		conn_lock;	/* lock for active/dead connections */
	struct list_head	conn_active;	/* active connections to/from this peer */
	struct list_head	conn_graveyard;	/* graveyard for inactive connections */
	spinlock_t		conn_gylock;	/* lock for conn_graveyard */
	wait_queue_head_t	conn_gy_waitq;	/* wait queue hit when graveyard is empty */
	atomic_t		conn_count;	/* number of attached connections */
	struct in_addr		addr;		/* remote address */
	size_t			if_mtu;		/* interface MTU for this peer */
	spinlock_t		lock;		/* access lock */

	void			*user;		/* application layer data */

	/* calculated RTT cache */
#define RXRPC_RTT_CACHE_SIZE 32
	suseconds_t		rtt;		/* current RTT estimate (in uS) */
	unsigned		rtt_point;	/* next entry at which to insert */
	unsigned		rtt_usage;	/* amount of cache actually used */
	suseconds_t		rtt_cache[RXRPC_RTT_CACHE_SIZE]; /* calculated RTT cache */
};


extern int rxrpc_peer_lookup(struct rxrpc_transport *trans,
			     __be32 addr,
			     struct rxrpc_peer **_peer);

static inline void rxrpc_get_peer(struct rxrpc_peer *peer)
{
	BUG_ON(atomic_read(&peer->usage)<0);
	atomic_inc(&peer->usage);
	//printk("rxrpc_get_peer(%p{u=%d})\n",peer,atomic_read(&peer->usage));
}

extern void rxrpc_put_peer(struct rxrpc_peer *peer);

#endif /* _LINUX_RXRPC_PEER_H */
