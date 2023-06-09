/* SPDX-License-Identifier: GPL-2.0-or-later */
/* RxRPC kernel service interface definitions
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#ifndef _NET_RXRPC_H
#define _NET_RXRPC_H

#include <linux/rxrpc.h>
#include <linux/ktime.h>

struct key;
struct sock;
struct socket;
struct rxrpc_call;
enum rxrpc_abort_reason;

enum rxrpc_interruptibility {
	RXRPC_INTERRUPTIBLE,	/* Call is interruptible */
	RXRPC_PREINTERRUPTIBLE,	/* Call can be cancelled whilst waiting for a slot */
	RXRPC_UNINTERRUPTIBLE,	/* Call should not be interruptible at all */
};

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
struct rxrpc_call *rxrpc_kernel_begin_call(struct socket *sock,
					   struct sockaddr_rxrpc *srx,
					   struct key *key,
					   unsigned long user_call_ID,
					   s64 tx_total_len,
					   u32 hard_timeout,
					   gfp_t gfp,
					   rxrpc_notify_rx_t notify_rx,
					   bool upgrade,
					   enum rxrpc_interruptibility interruptibility,
					   unsigned int debug_id);
int rxrpc_kernel_send_data(struct socket *, struct rxrpc_call *,
			   struct msghdr *, size_t,
			   rxrpc_notify_end_tx_t);
int rxrpc_kernel_recv_data(struct socket *, struct rxrpc_call *,
			   struct iov_iter *, size_t *, bool, u32 *, u16 *);
bool rxrpc_kernel_abort_call(struct socket *, struct rxrpc_call *,
			     u32, int, enum rxrpc_abort_reason);
void rxrpc_kernel_shutdown_call(struct socket *sock, struct rxrpc_call *call);
void rxrpc_kernel_put_call(struct socket *sock, struct rxrpc_call *call);
void rxrpc_kernel_get_peer(struct socket *, struct rxrpc_call *,
			   struct sockaddr_rxrpc *);
bool rxrpc_kernel_get_srtt(struct socket *, struct rxrpc_call *, u32 *);
int rxrpc_kernel_charge_accept(struct socket *, rxrpc_notify_rx_t,
			       rxrpc_user_attach_call_t, unsigned long, gfp_t,
			       unsigned int);
void rxrpc_kernel_set_tx_length(struct socket *, struct rxrpc_call *, s64);
bool rxrpc_kernel_check_life(const struct socket *, const struct rxrpc_call *);
u32 rxrpc_kernel_get_epoch(struct socket *, struct rxrpc_call *);
void rxrpc_kernel_set_max_life(struct socket *, struct rxrpc_call *,
			       unsigned long);

int rxrpc_sock_set_min_security_level(struct sock *sk, unsigned int val);
int rxrpc_sock_set_security_keyring(struct sock *, struct key *);

#endif /* _NET_RXRPC_H */
