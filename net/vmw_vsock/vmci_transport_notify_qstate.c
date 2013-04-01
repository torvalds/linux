/*
 * VMware vSockets Driver
 *
 * Copyright (C) 2009-2013 VMware, Inc. All rights reserved.
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

#include <linux/types.h>
#include <linux/socket.h>
#include <linux/stddef.h>
#include <net/sock.h>

#include "vmci_transport_notify.h"

#define PKT_FIELD(vsk, field_name) \
	(vmci_trans(vsk)->notify.pkt_q_state.field_name)

static bool vmci_transport_notify_waiting_write(struct vsock_sock *vsk)
{
	bool retval;
	u64 notify_limit;

	if (!PKT_FIELD(vsk, peer_waiting_write))
		return false;

	/* When the sender blocks, we take that as a sign that the sender is
	 * faster than the receiver. To reduce the transmit rate of the sender,
	 * we delay the sending of the read notification by decreasing the
	 * write_notify_window. The notification is delayed until the number of
	 * bytes used in the queue drops below the write_notify_window.
	 */

	if (!PKT_FIELD(vsk, peer_waiting_write_detected)) {
		PKT_FIELD(vsk, peer_waiting_write_detected) = true;
		if (PKT_FIELD(vsk, write_notify_window) < PAGE_SIZE) {
			PKT_FIELD(vsk, write_notify_window) =
			    PKT_FIELD(vsk, write_notify_min_window);
		} else {
			PKT_FIELD(vsk, write_notify_window) -= PAGE_SIZE;
			if (PKT_FIELD(vsk, write_notify_window) <
			    PKT_FIELD(vsk, write_notify_min_window))
				PKT_FIELD(vsk, write_notify_window) =
				    PKT_FIELD(vsk, write_notify_min_window);

		}
	}
	notify_limit = vmci_trans(vsk)->consume_size -
		PKT_FIELD(vsk, write_notify_window);

	/* The notify_limit is used to delay notifications in the case where
	 * flow control is enabled. Below the test is expressed in terms of
	 * free space in the queue: if free_space > ConsumeSize -
	 * write_notify_window then notify An alternate way of expressing this
	 * is to rewrite the expression to use the data ready in the receive
	 * queue: if write_notify_window > bufferReady then notify as
	 * free_space == ConsumeSize - bufferReady.
	 */

	retval = vmci_qpair_consume_free_space(vmci_trans(vsk)->qpair) >
		notify_limit;

	if (retval) {
		/* Once we notify the peer, we reset the detected flag so the
		 * next wait will again cause a decrease in the window size.
		 */

		PKT_FIELD(vsk, peer_waiting_write_detected) = false;
	}
	return retval;
}

static void
vmci_transport_handle_read(struct sock *sk,
			   struct vmci_transport_packet *pkt,
			   bool bottom_half,
			   struct sockaddr_vm *dst, struct sockaddr_vm *src)
{
	sk->sk_write_space(sk);
}

static void
vmci_transport_handle_wrote(struct sock *sk,
			    struct vmci_transport_packet *pkt,
			    bool bottom_half,
			    struct sockaddr_vm *dst, struct sockaddr_vm *src)
{
	sk->sk_data_ready(sk, 0);
}

static void vsock_block_update_write_window(struct sock *sk)
{
	struct vsock_sock *vsk = vsock_sk(sk);

	if (PKT_FIELD(vsk, write_notify_window) < vmci_trans(vsk)->consume_size)
		PKT_FIELD(vsk, write_notify_window) =
		    min(PKT_FIELD(vsk, write_notify_window) + PAGE_SIZE,
			vmci_trans(vsk)->consume_size);
}

static int vmci_transport_send_read_notification(struct sock *sk)
{
	struct vsock_sock *vsk;
	bool sent_read;
	unsigned int retries;
	int err;

	vsk = vsock_sk(sk);
	sent_read = false;
	retries = 0;
	err = 0;

	if (vmci_transport_notify_waiting_write(vsk)) {
		/* Notify the peer that we have read, retrying the send on
		 * failure up to our maximum value.  XXX For now we just log
		 * the failure, but later we should schedule a work item to
		 * handle the resend until it succeeds.  That would require
		 * keeping track of work items in the vsk and cleaning them up
		 * upon socket close.
		 */
		while (!(vsk->peer_shutdown & RCV_SHUTDOWN) &&
		       !sent_read &&
		       retries < VMCI_TRANSPORT_MAX_DGRAM_RESENDS) {
			err = vmci_transport_send_read(sk);
			if (err >= 0)
				sent_read = true;

			retries++;
		}

		if (retries >= VMCI_TRANSPORT_MAX_DGRAM_RESENDS && !sent_read)
			pr_err("%p unable to send read notification to peer\n",
			       sk);
		else
			PKT_FIELD(vsk, peer_waiting_write) = false;

	}
	return err;
}

static void vmci_transport_notify_pkt_socket_init(struct sock *sk)
{
	struct vsock_sock *vsk = vsock_sk(sk);

	PKT_FIELD(vsk, write_notify_window) = PAGE_SIZE;
	PKT_FIELD(vsk, write_notify_min_window) = PAGE_SIZE;
	PKT_FIELD(vsk, peer_waiting_write) = false;
	PKT_FIELD(vsk, peer_waiting_write_detected) = false;
}

static void vmci_transport_notify_pkt_socket_destruct(struct vsock_sock *vsk)
{
	PKT_FIELD(vsk, write_notify_window) = PAGE_SIZE;
	PKT_FIELD(vsk, write_notify_min_window) = PAGE_SIZE;
	PKT_FIELD(vsk, peer_waiting_write) = false;
	PKT_FIELD(vsk, peer_waiting_write_detected) = false;
}

static int
vmci_transport_notify_pkt_poll_in(struct sock *sk,
				  size_t target, bool *data_ready_now)
{
	struct vsock_sock *vsk = vsock_sk(sk);

	if (vsock_stream_has_data(vsk)) {
		*data_ready_now = true;
	} else {
		/* We can't read right now because there is nothing in the
		 * queue. Ask for notifications when there is something to
		 * read.
		 */
		if (sk->sk_state == SS_CONNECTED)
			vsock_block_update_write_window(sk);
		*data_ready_now = false;
	}

	return 0;
}

static int
vmci_transport_notify_pkt_poll_out(struct sock *sk,
				   size_t target, bool *space_avail_now)
{
	s64 produce_q_free_space;
	struct vsock_sock *vsk = vsock_sk(sk);

	produce_q_free_space = vsock_stream_has_space(vsk);
	if (produce_q_free_space > 0) {
		*space_avail_now = true;
		return 0;
	} else if (produce_q_free_space == 0) {
		/* This is a connected socket but we can't currently send data.
		 * Nothing else to do.
		 */
		*space_avail_now = false;
	}

	return 0;
}

static int
vmci_transport_notify_pkt_recv_init(
				struct sock *sk,
				size_t target,
				struct vmci_transport_recv_notify_data *data)
{
	struct vsock_sock *vsk = vsock_sk(sk);

	data->consume_head = 0;
	data->produce_tail = 0;
	data->notify_on_block = false;

	if (PKT_FIELD(vsk, write_notify_min_window) < target + 1) {
		PKT_FIELD(vsk, write_notify_min_window) = target + 1;
		if (PKT_FIELD(vsk, write_notify_window) <
		    PKT_FIELD(vsk, write_notify_min_window)) {
			/* If the current window is smaller than the new
			 * minimal window size, we need to reevaluate whether
			 * we need to notify the sender. If the number of ready
			 * bytes are smaller than the new window, we need to
			 * send a notification to the sender before we block.
			 */

			PKT_FIELD(vsk, write_notify_window) =
			    PKT_FIELD(vsk, write_notify_min_window);
			data->notify_on_block = true;
		}
	}

	return 0;
}

static int
vmci_transport_notify_pkt_recv_pre_block(
				struct sock *sk,
				size_t target,
				struct vmci_transport_recv_notify_data *data)
{
	int err = 0;

	vsock_block_update_write_window(sk);

	if (data->notify_on_block) {
		err = vmci_transport_send_read_notification(sk);
		if (err < 0)
			return err;
		data->notify_on_block = false;
	}

	return err;
}

static int
vmci_transport_notify_pkt_recv_post_dequeue(
				struct sock *sk,
				size_t target,
				ssize_t copied,
				bool data_read,
				struct vmci_transport_recv_notify_data *data)
{
	struct vsock_sock *vsk;
	int err;
	bool was_full = false;
	u64 free_space;

	vsk = vsock_sk(sk);
	err = 0;

	if (data_read) {
		smp_mb();

		free_space =
			vmci_qpair_consume_free_space(vmci_trans(vsk)->qpair);
		was_full = free_space == copied;

		if (was_full)
			PKT_FIELD(vsk, peer_waiting_write) = true;

		err = vmci_transport_send_read_notification(sk);
		if (err < 0)
			return err;

		/* See the comment in
		 * vmci_transport_notify_pkt_send_post_enqueue().
		 */
		sk->sk_data_ready(sk, 0);
	}

	return err;
}

static int
vmci_transport_notify_pkt_send_init(
				struct sock *sk,
				struct vmci_transport_send_notify_data *data)
{
	data->consume_head = 0;
	data->produce_tail = 0;

	return 0;
}

static int
vmci_transport_notify_pkt_send_post_enqueue(
				struct sock *sk,
				ssize_t written,
				struct vmci_transport_send_notify_data *data)
{
	int err = 0;
	struct vsock_sock *vsk;
	bool sent_wrote = false;
	bool was_empty;
	int retries = 0;

	vsk = vsock_sk(sk);

	smp_mb();

	was_empty =
		vmci_qpair_produce_buf_ready(vmci_trans(vsk)->qpair) == written;
	if (was_empty) {
		while (!(vsk->peer_shutdown & RCV_SHUTDOWN) &&
		       !sent_wrote &&
		       retries < VMCI_TRANSPORT_MAX_DGRAM_RESENDS) {
			err = vmci_transport_send_wrote(sk);
			if (err >= 0)
				sent_wrote = true;

			retries++;
		}
	}

	if (retries >= VMCI_TRANSPORT_MAX_DGRAM_RESENDS && !sent_wrote) {
		pr_err("%p unable to send wrote notification to peer\n",
		       sk);
		return err;
	}

	return err;
}

static void
vmci_transport_notify_pkt_handle_pkt(
				struct sock *sk,
				struct vmci_transport_packet *pkt,
				bool bottom_half,
				struct sockaddr_vm *dst,
				struct sockaddr_vm *src, bool *pkt_processed)
{
	bool processed = false;

	switch (pkt->type) {
	case VMCI_TRANSPORT_PACKET_TYPE_WROTE:
		vmci_transport_handle_wrote(sk, pkt, bottom_half, dst, src);
		processed = true;
		break;
	case VMCI_TRANSPORT_PACKET_TYPE_READ:
		vmci_transport_handle_read(sk, pkt, bottom_half, dst, src);
		processed = true;
		break;
	}

	if (pkt_processed)
		*pkt_processed = processed;
}

static void vmci_transport_notify_pkt_process_request(struct sock *sk)
{
	struct vsock_sock *vsk = vsock_sk(sk);

	PKT_FIELD(vsk, write_notify_window) = vmci_trans(vsk)->consume_size;
	if (vmci_trans(vsk)->consume_size <
		PKT_FIELD(vsk, write_notify_min_window))
		PKT_FIELD(vsk, write_notify_min_window) =
			vmci_trans(vsk)->consume_size;
}

static void vmci_transport_notify_pkt_process_negotiate(struct sock *sk)
{
	struct vsock_sock *vsk = vsock_sk(sk);

	PKT_FIELD(vsk, write_notify_window) = vmci_trans(vsk)->consume_size;
	if (vmci_trans(vsk)->consume_size <
		PKT_FIELD(vsk, write_notify_min_window))
		PKT_FIELD(vsk, write_notify_min_window) =
			vmci_trans(vsk)->consume_size;
}

static int
vmci_transport_notify_pkt_recv_pre_dequeue(
				struct sock *sk,
				size_t target,
				struct vmci_transport_recv_notify_data *data)
{
	return 0; /* NOP for QState. */
}

static int
vmci_transport_notify_pkt_send_pre_block(
				struct sock *sk,
				struct vmci_transport_send_notify_data *data)
{
	return 0; /* NOP for QState. */
}

static int
vmci_transport_notify_pkt_send_pre_enqueue(
				struct sock *sk,
				struct vmci_transport_send_notify_data *data)
{
	return 0; /* NOP for QState. */
}

/* Socket always on control packet based operations. */
struct vmci_transport_notify_ops vmci_transport_notify_pkt_q_state_ops = {
	vmci_transport_notify_pkt_socket_init,
	vmci_transport_notify_pkt_socket_destruct,
	vmci_transport_notify_pkt_poll_in,
	vmci_transport_notify_pkt_poll_out,
	vmci_transport_notify_pkt_handle_pkt,
	vmci_transport_notify_pkt_recv_init,
	vmci_transport_notify_pkt_recv_pre_block,
	vmci_transport_notify_pkt_recv_pre_dequeue,
	vmci_transport_notify_pkt_recv_post_dequeue,
	vmci_transport_notify_pkt_send_init,
	vmci_transport_notify_pkt_send_pre_block,
	vmci_transport_notify_pkt_send_pre_enqueue,
	vmci_transport_notify_pkt_send_post_enqueue,
	vmci_transport_notify_pkt_process_request,
	vmci_transport_notify_pkt_process_negotiate,
};
