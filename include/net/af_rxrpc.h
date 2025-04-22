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
struct rxrpc_peer;
struct krb5_buffer;
enum rxrpc_abort_reason;

enum rxrpc_interruptibility {
	RXRPC_INTERRUPTIBLE,	/* Call is interruptible */
	RXRPC_PREINTERRUPTIBLE,	/* Call can be cancelled whilst waiting for a slot */
	RXRPC_UNINTERRUPTIBLE,	/* Call should not be interruptible at all */
};

enum rxrpc_oob_type {
	RXRPC_OOB_CHALLENGE,	/* Security challenge for a connection */
};

/*
 * Debug ID counter for tracing.
 */
extern atomic_t rxrpc_debug_id;

/*
 * Operations table for rxrpc to call out to a kernel application (e.g. kAFS).
 */
struct rxrpc_kernel_ops {
	void (*notify_new_call)(struct sock *sk, struct rxrpc_call *call,
				unsigned long user_call_ID);
	void (*discard_new_call)(struct rxrpc_call *call, unsigned long user_call_ID);
	void (*user_attach_call)(struct rxrpc_call *call, unsigned long user_call_ID);
	void (*notify_oob)(struct sock *sk, struct sk_buff *oob);
};

typedef void (*rxrpc_notify_rx_t)(struct sock *, struct rxrpc_call *,
				  unsigned long);
typedef void (*rxrpc_notify_end_tx_t)(struct sock *, struct rxrpc_call *,
				      unsigned long);

void rxrpc_kernel_set_notifications(struct socket *sock,
				    const struct rxrpc_kernel_ops *app_ops);
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
struct rxrpc_peer *rxrpc_kernel_lookup_peer(struct socket *sock,
					    struct sockaddr_rxrpc *srx, gfp_t gfp);
void rxrpc_kernel_put_peer(struct rxrpc_peer *peer);
struct rxrpc_peer *rxrpc_kernel_get_peer(struct rxrpc_peer *peer);
struct rxrpc_peer *rxrpc_kernel_get_call_peer(struct socket *sock, struct rxrpc_call *call);
const struct sockaddr_rxrpc *rxrpc_kernel_remote_srx(const struct rxrpc_peer *peer);
const struct sockaddr *rxrpc_kernel_remote_addr(const struct rxrpc_peer *peer);
unsigned long rxrpc_kernel_set_peer_data(struct rxrpc_peer *peer, unsigned long app_data);
unsigned long rxrpc_kernel_get_peer_data(const struct rxrpc_peer *peer);
unsigned int rxrpc_kernel_get_srtt(const struct rxrpc_peer *);
int rxrpc_kernel_charge_accept(struct socket *sock, rxrpc_notify_rx_t notify_rx,
			       unsigned long user_call_ID, gfp_t gfp,
			       unsigned int debug_id);
void rxrpc_kernel_set_tx_length(struct socket *, struct rxrpc_call *, s64);
bool rxrpc_kernel_check_life(const struct socket *, const struct rxrpc_call *);

int rxrpc_sock_set_min_security_level(struct sock *sk, unsigned int val);
int rxrpc_sock_set_security_keyring(struct sock *, struct key *);
int rxrpc_sock_set_manage_response(struct sock *sk, bool set);

enum rxrpc_oob_type rxrpc_kernel_query_oob(struct sk_buff *oob,
					   struct rxrpc_peer **_peer,
					   unsigned long *_peer_appdata);
struct sk_buff *rxrpc_kernel_dequeue_oob(struct socket *sock,
					 enum rxrpc_oob_type *_type);
void rxrpc_kernel_free_oob(struct sk_buff *oob);
void rxrpc_kernel_query_challenge(struct sk_buff *challenge,
				  struct rxrpc_peer **_peer,
				  unsigned long *_peer_appdata,
				  u16 *_service_id, u8 *_security_index);
int rxrpc_kernel_reject_challenge(struct sk_buff *challenge, u32 abort_code,
				  int error, enum rxrpc_abort_reason why);
int rxkad_kernel_respond_to_challenge(struct sk_buff *challenge);
u32 rxgk_kernel_query_challenge(struct sk_buff *challenge);
int rxgk_kernel_respond_to_challenge(struct sk_buff *challenge,
				     struct krb5_buffer *appdata);
u8 rxrpc_kernel_query_call_security(struct rxrpc_call *call,
				    u16 *_service_id, u32 *_enctype);

#endif /* _NET_RXRPC_H */
