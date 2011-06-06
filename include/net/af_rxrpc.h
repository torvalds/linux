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

struct rxrpc_call;

/*
 * the mark applied to socket buffers that may be intercepted
 */
enum {
	RXRPC_SKB_MARK_DATA,		/* data message */
	RXRPC_SKB_MARK_FINAL_ACK,	/* final ACK received message */
	RXRPC_SKB_MARK_BUSY,		/* server busy message */
	RXRPC_SKB_MARK_REMOTE_ABORT,	/* remote abort message */
	RXRPC_SKB_MARK_NET_ERROR,	/* network error message */
	RXRPC_SKB_MARK_LOCAL_ERROR,	/* local error message */
	RXRPC_SKB_MARK_NEW_CALL,	/* local error message */
};

typedef void (*rxrpc_interceptor_t)(struct sock *, unsigned long,
				    struct sk_buff *);
extern void rxrpc_kernel_intercept_rx_messages(struct socket *,
					       rxrpc_interceptor_t);
extern struct rxrpc_call *rxrpc_kernel_begin_call(struct socket *,
						  struct sockaddr_rxrpc *,
						  struct key *,
						  unsigned long,
						  gfp_t);
extern int rxrpc_kernel_send_data(struct rxrpc_call *, struct msghdr *,
				  size_t);
extern void rxrpc_kernel_abort_call(struct rxrpc_call *, u32);
extern void rxrpc_kernel_end_call(struct rxrpc_call *);
extern bool rxrpc_kernel_is_data_last(struct sk_buff *);
extern u32 rxrpc_kernel_get_abort_code(struct sk_buff *);
extern int rxrpc_kernel_get_error_number(struct sk_buff *);
extern void rxrpc_kernel_data_delivered(struct sk_buff *);
extern void rxrpc_kernel_free_skb(struct sk_buff *);
extern struct rxrpc_call *rxrpc_kernel_accept_call(struct socket *,
						   unsigned long);
extern int rxrpc_kernel_reject_call(struct socket *);

#endif /* _NET_RXRPC_H */
