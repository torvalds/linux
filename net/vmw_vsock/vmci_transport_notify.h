/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * VMware vSockets Driver
 *
 * Copyright (C) 2009-2013 VMware, Inc. All rights reserved.
 */

#ifndef __VMCI_TRANSPORT_NOTIFY_H__
#define __VMCI_TRANSPORT_NOTIFY_H__

#include <linux/types.h>
#include <linux/vmw_vmci_defs.h>
#include <linux/vmw_vmci_api.h>

#include "vmci_transport.h"

/* Comment this out to compare with old protocol. */
#define VSOCK_OPTIMIZATION_WAITING_NOTIFY 1
#if defined(VSOCK_OPTIMIZATION_WAITING_NOTIFY)
/* Comment this out to remove flow control for "new" protocol */
#define VSOCK_OPTIMIZATION_FLOW_CONTROL 1
#endif

#define VMCI_TRANSPORT_MAX_DGRAM_RESENDS       10

struct vmci_transport_recv_notify_data {
	u64 consume_head;
	u64 produce_tail;
	bool notify_on_block;
};

struct vmci_transport_send_notify_data {
	u64 consume_head;
	u64 produce_tail;
};

/* Socket notification callbacks. */
struct vmci_transport_notify_ops {
	void (*socket_init) (struct sock *sk);
	void (*socket_destruct) (struct vsock_sock *vsk);
	int (*poll_in) (struct sock *sk, size_t target,
			  bool *data_ready_now);
	int (*poll_out) (struct sock *sk, size_t target,
			   bool *space_avail_now);
	void (*handle_notify_pkt) (struct sock *sk,
				   struct vmci_transport_packet *pkt,
				   bool bottom_half, struct sockaddr_vm *dst,
				   struct sockaddr_vm *src,
				   bool *pkt_processed);
	int (*recv_init) (struct sock *sk, size_t target,
			  struct vmci_transport_recv_notify_data *data);
	int (*recv_pre_block) (struct sock *sk, size_t target,
			       struct vmci_transport_recv_notify_data *data);
	int (*recv_pre_dequeue) (struct sock *sk, size_t target,
				 struct vmci_transport_recv_notify_data *data);
	int (*recv_post_dequeue) (struct sock *sk, size_t target,
				  ssize_t copied, bool data_read,
				  struct vmci_transport_recv_notify_data *data);
	int (*send_init) (struct sock *sk,
			  struct vmci_transport_send_notify_data *data);
	int (*send_pre_block) (struct sock *sk,
			       struct vmci_transport_send_notify_data *data);
	int (*send_pre_enqueue) (struct sock *sk,
				 struct vmci_transport_send_notify_data *data);
	int (*send_post_enqueue) (struct sock *sk, ssize_t written,
				  struct vmci_transport_send_notify_data *data);
	void (*process_request) (struct sock *sk);
	void (*process_negotiate) (struct sock *sk);
};

extern const struct vmci_transport_notify_ops vmci_transport_notify_pkt_ops;
extern const
struct vmci_transport_notify_ops vmci_transport_notify_pkt_q_state_ops;

#endif /* __VMCI_TRANSPORT_NOTIFY_H__ */
