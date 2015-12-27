/*
 * VMware vSockets Driver
 *
 * Copyright (C) 2013 VMware, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation version 2 and no later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef _VMCI_TRANSPORT_H_
#define _VMCI_TRANSPORT_H_

#include <linux/vmw_vmci_defs.h>
#include <linux/vmw_vmci_api.h>

#include <net/vsock_addr.h>
#include <net/af_vsock.h>

/* If the packet format changes in a release then this should change too. */
#define VMCI_TRANSPORT_PACKET_VERSION 1

/* The resource ID on which control packets are sent. */
#define VMCI_TRANSPORT_PACKET_RID 1

/* The resource ID on which control packets are sent to the hypervisor. */
#define VMCI_TRANSPORT_HYPERVISOR_PACKET_RID 15

#define VSOCK_PROTO_INVALID        0
#define VSOCK_PROTO_PKT_ON_NOTIFY (1 << 0)
#define VSOCK_PROTO_ALL_SUPPORTED (VSOCK_PROTO_PKT_ON_NOTIFY)

#define vmci_trans(_vsk) ((struct vmci_transport *)((_vsk)->trans))

enum vmci_transport_packet_type {
	VMCI_TRANSPORT_PACKET_TYPE_INVALID = 0,
	VMCI_TRANSPORT_PACKET_TYPE_REQUEST,
	VMCI_TRANSPORT_PACKET_TYPE_NEGOTIATE,
	VMCI_TRANSPORT_PACKET_TYPE_OFFER,
	VMCI_TRANSPORT_PACKET_TYPE_ATTACH,
	VMCI_TRANSPORT_PACKET_TYPE_WROTE,
	VMCI_TRANSPORT_PACKET_TYPE_READ,
	VMCI_TRANSPORT_PACKET_TYPE_RST,
	VMCI_TRANSPORT_PACKET_TYPE_SHUTDOWN,
	VMCI_TRANSPORT_PACKET_TYPE_WAITING_WRITE,
	VMCI_TRANSPORT_PACKET_TYPE_WAITING_READ,
	VMCI_TRANSPORT_PACKET_TYPE_REQUEST2,
	VMCI_TRANSPORT_PACKET_TYPE_NEGOTIATE2,
	VMCI_TRANSPORT_PACKET_TYPE_MAX
};

struct vmci_transport_waiting_info {
	u64 generation;
	u64 offset;
};

/* Control packet type for STREAM sockets.  DGRAMs have no control packets nor
 * special packet header for data packets, they are just raw VMCI DGRAM
 * messages.  For STREAMs, control packets are sent over the control channel
 * while data is written and read directly from queue pairs with no packet
 * format.
 */
struct vmci_transport_packet {
	struct vmci_datagram dg;
	u8 version;
	u8 type;
	u16 proto;
	u32 src_port;
	u32 dst_port;
	u32 _reserved2;
	union {
		u64 size;
		u64 mode;
		struct vmci_handle handle;
		struct vmci_transport_waiting_info wait;
	} u;
};

struct vmci_transport_notify_pkt {
	u64 write_notify_window;
	u64 write_notify_min_window;
	bool peer_waiting_read;
	bool peer_waiting_write;
	bool peer_waiting_write_detected;
	bool sent_waiting_read;
	bool sent_waiting_write;
	struct vmci_transport_waiting_info peer_waiting_read_info;
	struct vmci_transport_waiting_info peer_waiting_write_info;
	u64 produce_q_generation;
	u64 consume_q_generation;
};

struct vmci_transport_notify_pkt_q_state {
	u64 write_notify_window;
	u64 write_notify_min_window;
	bool peer_waiting_write;
	bool peer_waiting_write_detected;
};

union vmci_transport_notify {
	struct vmci_transport_notify_pkt pkt;
	struct vmci_transport_notify_pkt_q_state pkt_q_state;
};

/* Our transport-specific data. */
struct vmci_transport {
	/* For DGRAMs. */
	struct vmci_handle dg_handle;
	/* For STREAMs. */
	struct vmci_handle qp_handle;
	struct vmci_qp *qpair;
	u64 produce_size;
	u64 consume_size;
	u64 queue_pair_size;
	u64 queue_pair_min_size;
	u64 queue_pair_max_size;
	u32 detach_sub_id;
	union vmci_transport_notify notify;
	struct vmci_transport_notify_ops *notify_ops;
	struct list_head elem;
	struct sock *sk;
	spinlock_t lock; /* protects sk. */
};

int vmci_transport_register(void);
void vmci_transport_unregister(void);

int vmci_transport_send_wrote_bh(struct sockaddr_vm *dst,
				 struct sockaddr_vm *src);
int vmci_transport_send_read_bh(struct sockaddr_vm *dst,
				struct sockaddr_vm *src);
int vmci_transport_send_wrote(struct sock *sk);
int vmci_transport_send_read(struct sock *sk);
int vmci_transport_send_waiting_write(struct sock *sk,
				      struct vmci_transport_waiting_info *wait);
int vmci_transport_send_waiting_read(struct sock *sk,
				     struct vmci_transport_waiting_info *wait);

#endif
