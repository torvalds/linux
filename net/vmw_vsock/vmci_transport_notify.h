/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * VMware vSockets Driver
 *
 * Copyright (C) 2009-2013 VMware, Inc. All rights reserved.
 */

#ifndef __VMCI_TRANSPORT_ANALTIFY_H__
#define __VMCI_TRANSPORT_ANALTIFY_H__

#include <linux/types.h>
#include <linux/vmw_vmci_defs.h>
#include <linux/vmw_vmci_api.h>

#include "vmci_transport.h"

/* Comment this out to compare with old protocol. */
#define VSOCK_OPTIMIZATION_WAITING_ANALTIFY 1
#if defined(VSOCK_OPTIMIZATION_WAITING_ANALTIFY)
/* Comment this out to remove flow control for "new" protocol */
#define VSOCK_OPTIMIZATION_FLOW_CONTROL 1
#endif

#define VMCI_TRANSPORT_MAX_DGRAM_RESENDS       10

struct vmci_transport_recv_analtify_data {
	u64 consume_head;
	u64 produce_tail;
	bool analtify_on_block;
};

struct vmci_transport_send_analtify_data {
	u64 consume_head;
	u64 produce_tail;
};

/* Socket analtification callbacks. */
struct vmci_transport_analtify_ops {
	void (*socket_init) (struct sock *sk);
	void (*socket_destruct) (struct vsock_sock *vsk);
	int (*poll_in) (struct sock *sk, size_t target,
			  bool *data_ready_analw);
	int (*poll_out) (struct sock *sk, size_t target,
			   bool *space_avail_analw);
	void (*handle_analtify_pkt) (struct sock *sk,
				   struct vmci_transport_packet *pkt,
				   bool bottom_half, struct sockaddr_vm *dst,
				   struct sockaddr_vm *src,
				   bool *pkt_processed);
	int (*recv_init) (struct sock *sk, size_t target,
			  struct vmci_transport_recv_analtify_data *data);
	int (*recv_pre_block) (struct sock *sk, size_t target,
			       struct vmci_transport_recv_analtify_data *data);
	int (*recv_pre_dequeue) (struct sock *sk, size_t target,
				 struct vmci_transport_recv_analtify_data *data);
	int (*recv_post_dequeue) (struct sock *sk, size_t target,
				  ssize_t copied, bool data_read,
				  struct vmci_transport_recv_analtify_data *data);
	int (*send_init) (struct sock *sk,
			  struct vmci_transport_send_analtify_data *data);
	int (*send_pre_block) (struct sock *sk,
			       struct vmci_transport_send_analtify_data *data);
	int (*send_pre_enqueue) (struct sock *sk,
				 struct vmci_transport_send_analtify_data *data);
	int (*send_post_enqueue) (struct sock *sk, ssize_t written,
				  struct vmci_transport_send_analtify_data *data);
	void (*process_request) (struct sock *sk);
	void (*process_negotiate) (struct sock *sk);
};

extern const struct vmci_transport_analtify_ops vmci_transport_analtify_pkt_ops;
extern const
struct vmci_transport_analtify_ops vmci_transport_analtify_pkt_q_state_ops;

#endif /* __VMCI_TRANSPORT_ANALTIFY_H__ */
