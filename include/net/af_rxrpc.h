/* RxRPC kernel service interface definitions
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _NET_RXRPC_H
#define _NET_RXRPC_H

#include <linux/rxrpc.h>
#include <linux/ktime.h>

struct key;
struct sock;
struct socket;
struct rxrpc_call;

/*
 * Debug ID counter for tracing.
 */
extern atomic_t rxrpc_debug_id;

typedef void (*rxrpc_notify_rx_t)(struct sock *, struct rxrpc_call *,
				  unsigned long);
typedef void (*rxrpc_notify_end_tx_t)(struct sock *, struct rxrpc_call *,
				      unsigned long);
typedef void (*rxrpc_notify_new_call_t)(struct sock *, struct rxrpc_call *,
					unsigned long);
typedef void (*rxrpc_discard_new_call_t)(struct rxrpc_call *, unsigned long);
typedef void (*rxrpc_user_attach_call_t)(struct rxrpc_call *, unsigned long);

void rxrpc_kernel_new_call_notification(struct socket *,
					rxrpc_notify_new_call_t,
					rxrpc_discard_new_call_t);
struct rxrpc_call *rxrpc_kernel_begin_call(struct socket *,
					   struct sockaddr_rxrpc *,
					   struct key *,
					   unsigned long,
					   s64,
					   gfp_t,
					   rxrpc_notify_rx_t,
					   bool,
					   unsigned int);
int rxrpc_kernel_send_data(struct socket *, struct rxrpc_call *,
			   struct msghdr *, size_t,
			   rxrpc_notify_end_tx_t);
int rxrpc_kernel_recv_data(struct socket *, struct rxrpc_call *,
			   struct iov_iter *, bool, u32 *, u16 *);
bool rxrpc_kernel_abort_call(struct socket *, struct rxrpc_call *,
			     u32, int, const char *);
void rxrpc_kernel_end_call(struct socket *, struct rxrpc_call *);
void rxrpc_kernel_get_peer(struct socket *, struct rxrpc_call *,
			   struct sockaddr_rxrpc *);
u64 rxrpc_kernel_get_rtt(struct socket *, struct rxrpc_call *);
int rxrpc_kernel_charge_accept(struct socket *, rxrpc_notify_rx_t,
			       rxrpc_user_attach_call_t, unsigned long, gfp_t,
			       unsigned int);
void rxrpc_kernel_set_tx_length(struct socket *, struct rxrpc_call *, s64);
bool rxrpc_kernel_check_life(const struct socket *, const struct rxrpc_call *,
			     u32 *);
void rxrpc_kernel_probe_life(struct socket *, struct rxrpc_call *);
u32 rxrpc_kernel_get_epoch(struct socket *, struct rxrpc_call *);
bool rxrpc_kernel_get_reply_time(struct socket *, struct rxrpc_call *,
				 ktime_t *);
bool rxrpc_kernel_call_is_complete(struct rxrpc_call *);

#endif /* _NET_RXRPC_H */
