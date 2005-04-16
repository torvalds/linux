/* transport.h: Rx transport management
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_RXRPC_TRANSPORT_H
#define _LINUX_RXRPC_TRANSPORT_H

#include <rxrpc/types.h>
#include <rxrpc/krxiod.h>
#include <rxrpc/rxrpc.h>
#include <linux/skbuff.h>
#include <linux/rwsem.h>

typedef int (*rxrpc_newcall_fnx_t)(struct rxrpc_call *call);

extern wait_queue_head_t rxrpc_krxiod_wq;

/*****************************************************************************/
/*
 * Rx operation specification
 * - tables of these must be sorted by op ID so that they can be binary-chop searched
 */
struct rxrpc_operation
{
	unsigned		id;		/* operation ID */
	size_t			asize;		/* minimum size of argument block */
	const char		*name;		/* name of operation */
	void			*user;		/* initial user data */
};

/*****************************************************************************/
/*
 * Rx transport service record
 */
struct rxrpc_service
{
	struct list_head	link;		/* link in services list on transport */
	struct module		*owner;		/* owner module */
	rxrpc_newcall_fnx_t	new_call;	/* new call handler function */
	const char		*name;		/* name of service */
	unsigned short		service_id;	/* Rx service ID */
	rxrpc_call_attn_func_t	attn_func;	/* call requires attention callback */
	rxrpc_call_error_func_t	error_func;	/* call error callback */
	rxrpc_call_aemap_func_t	aemap_func;	/* abort -> errno mapping callback */

	const struct rxrpc_operation	*ops_begin;	/* beginning of operations table */
	const struct rxrpc_operation	*ops_end;	/* end of operations table */
};

/*****************************************************************************/
/*
 * Rx transport endpoint record
 */
struct rxrpc_transport
{
	atomic_t		usage;
	struct socket		*socket;	/* my UDP socket */
	struct list_head	services;	/* services listening on this socket */
	struct list_head	link;		/* link in transport list */
	struct list_head	proc_link;	/* link in transport proc list */
	struct list_head	krxiodq_link;	/* krxiod attention queue link */
	spinlock_t		lock;		/* access lock */
	struct list_head	peer_active;	/* active peers connected to over this socket */
	struct list_head	peer_graveyard;	/* inactive peer list */
	spinlock_t		peer_gylock;	/* peer graveyard lock */
	wait_queue_head_t	peer_gy_waitq;	/* wait queue hit when peer graveyard is empty */
	rwlock_t		peer_lock;	/* peer list access lock */
	atomic_t		peer_count;	/* number of peers */
	struct rxrpc_peer_ops	*peer_ops;	/* default peer operations */
	unsigned short		port;		/* port upon which listening */
	volatile char		error_rcvd;	/* T if received ICMP error outstanding */
};

extern int rxrpc_create_transport(unsigned short port,
				  struct rxrpc_transport **_trans);

static inline void rxrpc_get_transport(struct rxrpc_transport *trans)
{
	BUG_ON(atomic_read(&trans->usage) <= 0);
	atomic_inc(&trans->usage);
	//printk("rxrpc_get_transport(%p{u=%d})\n",
	//       trans, atomic_read(&trans->usage));
}

extern void rxrpc_put_transport(struct rxrpc_transport *trans);

extern int rxrpc_add_service(struct rxrpc_transport *trans,
			     struct rxrpc_service *srv);

extern void rxrpc_del_service(struct rxrpc_transport *trans,
			      struct rxrpc_service *srv);

extern void rxrpc_trans_receive_packet(struct rxrpc_transport *trans);

extern int rxrpc_trans_immediate_abort(struct rxrpc_transport *trans,
				       struct rxrpc_message *msg,
				       int error);

#endif /* _LINUX_RXRPC_TRANSPORT_H */
