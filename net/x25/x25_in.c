/*
 *	X.25 Packet Layer release 002
 *
 *	This is ALPHA test software. This code may break your machine,
 *	randomly fail to work with new releases, misbehave and/or generally
 *	screw up. It might even work.
 *
 *	This code REQUIRES 2.1.15 or higher
 *
 *	This module:
 *		This module is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	History
 *	X.25 001	Jonathan Naylor	  Started coding.
 *	X.25 002	Jonathan Naylor	  Centralised disconnection code.
 *					  New timer architecture.
 *	2000-03-20	Daniela Squassoni Disabling/enabling of facilities
 *					  negotiation.
 *	2000-11-10	Henner Eisen	  Check and reset for out-of-sequence
 *					  i-frames.
 */

#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/tcp_states.h>
#include <net/x25.h>

static int x25_queue_rx_frame(struct sock *sk, struct sk_buff *skb, int more)
{
	struct sk_buff *skbo, *skbn = skb;
	struct x25_sock *x25 = x25_sk(sk);

	if (more) {
		x25->fraglen += skb->len;
		skb_queue_tail(&x25->fragment_queue, skb);
		skb_set_owner_r(skb, sk);
		return 0;
	}

	if (!more && x25->fraglen > 0) {	/* End of fragment */
		int len = x25->fraglen + skb->len;

		if ((skbn = alloc_skb(len, GFP_ATOMIC)) == NULL){
			kfree_skb(skb);
			return 1;
		}

		skb_queue_tail(&x25->fragment_queue, skb);

		skb_reset_transport_header(skbn);

		skbo = skb_dequeue(&x25->fragment_queue);
		skb_copy_from_linear_data(skbo, skb_put(skbn, skbo->len),
					  skbo->len);
		kfree_skb(skbo);

		while ((skbo =
			skb_dequeue(&x25->fragment_queue)) != NULL) {
			skb_pull(skbo, (x25->neighbour->extended) ?
					X25_EXT_MIN_LEN : X25_STD_MIN_LEN);
			skb_copy_from_linear_data(skbo,
						  skb_put(skbn, skbo->len),
						  skbo->len);
			kfree_skb(skbo);
		}

		x25->fraglen = 0;
	}

	skb_set_owner_r(skbn, sk);
	skb_queue_tail(&sk->sk_receive_queue, skbn);
	if (!sock_flag(sk, SOCK_DEAD))
		sk->sk_data_ready(sk, skbn->len);

	return 0;
}

/*
 * State machine for state 1, Awaiting Call Accepted State.
 * The handling of the timer(s) is in file x25_timer.c.
 * Handling of state 0 and connection release is in af_x25.c.
 */
static int x25_state1_machine(struct sock *sk, struct sk_buff *skb, int frametype)
{
	struct x25_address source_addr, dest_addr;
	int len;
	struct x25_sock *x25 = x25_sk(sk);

	switch (frametype) {
		case X25_CALL_ACCEPTED: {

			x25_stop_timer(sk);
			x25->condition = 0x00;
			x25->vs        = 0;
			x25->va        = 0;
			x25->vr        = 0;
			x25->vl        = 0;
			x25->state     = X25_STATE_3;
			sk->sk_state   = TCP_ESTABLISHED;
			/*
			 *	Parse the data in the frame.
			 */
			skb_pull(skb, X25_STD_MIN_LEN);

			len = x25_parse_address_block(skb, &source_addr,
						&dest_addr);
			if (len > 0)
				skb_pull(skb, len);
			else if (len < 0)
				goto out_clear;

			len = x25_parse_facilities(skb, &x25->facilities,
						&x25->dte_facilities,
						&x25->vc_facil_mask);
			if (len > 0)
				skb_pull(skb, len);
			else if (len < 0)
				goto out_clear;
			/*
			 *	Copy any Call User Data.
			 */
			if (skb->len > 0) {
				skb_copy_from_linear_data(skb,
					      x25->calluserdata.cuddata,
					      skb->len);
				x25->calluserdata.cudlength = skb->len;
			}
			if (!sock_flag(sk, SOCK_DEAD))
				sk->sk_state_change(sk);
			break;
		}
		case X25_CLEAR_REQUEST:
			x25_write_internal(sk, X25_CLEAR_CONFIRMATION);
			x25_disconnect(sk, ECONNREFUSED, skb->data[3], skb->data[4]);
			break;

		default:
			break;
	}

	return 0;

out_clear:
	x25_write_internal(sk, X25_CLEAR_REQUEST);
	x25->state = X25_STATE_2;
	x25_start_t23timer(sk);
	return 0;
}

/*
 * State machine for state 2, Awaiting Clear Confirmation State.
 * The handling of the timer(s) is in file x25_timer.c
 * Handling of state 0 and connection release is in af_x25.c.
 */
static int x25_state2_machine(struct sock *sk, struct sk_buff *skb, int frametype)
{
	switch (frametype) {

		case X25_CLEAR_REQUEST:
			x25_write_internal(sk, X25_CLEAR_CONFIRMATION);
			x25_disconnect(sk, 0, skb->data[3], skb->data[4]);
			break;

		case X25_CLEAR_CONFIRMATION:
			x25_disconnect(sk, 0, 0, 0);
			break;

		default:
			break;
	}

	return 0;
}

/*
 * State machine for state 3, Connected State.
 * The handling of the timer(s) is in file x25_timer.c
 * Handling of state 0 and connection release is in af_x25.c.
 */
static int x25_state3_machine(struct sock *sk, struct sk_buff *skb, int frametype, int ns, int nr, int q, int d, int m)
{
	int queued = 0;
	int modulus;
	struct x25_sock *x25 = x25_sk(sk);

	modulus = (x25->neighbour->extended) ? X25_EMODULUS : X25_SMODULUS;

	switch (frametype) {

		case X25_RESET_REQUEST:
			x25_write_internal(sk, X25_RESET_CONFIRMATION);
			x25_stop_timer(sk);
			x25->condition = 0x00;
			x25->vs        = 0;
			x25->vr        = 0;
			x25->va        = 0;
			x25->vl        = 0;
			x25_requeue_frames(sk);
			break;

		case X25_CLEAR_REQUEST:
			x25_write_internal(sk, X25_CLEAR_CONFIRMATION);
			x25_disconnect(sk, 0, skb->data[3], skb->data[4]);
			break;

		case X25_RR:
		case X25_RNR:
			if (!x25_validate_nr(sk, nr)) {
				x25_clear_queues(sk);
				x25_write_internal(sk, X25_RESET_REQUEST);
				x25_start_t22timer(sk);
				x25->condition = 0x00;
				x25->vs        = 0;
				x25->vr        = 0;
				x25->va        = 0;
				x25->vl        = 0;
				x25->state     = X25_STATE_4;
			} else {
				x25_frames_acked(sk, nr);
				if (frametype == X25_RNR) {
					x25->condition |= X25_COND_PEER_RX_BUSY;
				} else {
					x25->condition &= ~X25_COND_PEER_RX_BUSY;
				}
			}
			break;

		case X25_DATA:	/* XXX */
			x25->condition &= ~X25_COND_PEER_RX_BUSY;
			if ((ns != x25->vr) || !x25_validate_nr(sk, nr)) {
				x25_clear_queues(sk);
				x25_write_internal(sk, X25_RESET_REQUEST);
				x25_start_t22timer(sk);
				x25->condition = 0x00;
				x25->vs        = 0;
				x25->vr        = 0;
				x25->va        = 0;
				x25->vl        = 0;
				x25->state     = X25_STATE_4;
				break;
			}
			x25_frames_acked(sk, nr);
			if (ns == x25->vr) {
				if (x25_queue_rx_frame(sk, skb, m) == 0) {
					x25->vr = (x25->vr + 1) % modulus;
					queued = 1;
				} else {
					/* Should never happen */
					x25_clear_queues(sk);
					x25_write_internal(sk, X25_RESET_REQUEST);
					x25_start_t22timer(sk);
					x25->condition = 0x00;
					x25->vs        = 0;
					x25->vr        = 0;
					x25->va        = 0;
					x25->vl        = 0;
					x25->state     = X25_STATE_4;
					break;
				}
				if (atomic_read(&sk->sk_rmem_alloc) >
				    (sk->sk_rcvbuf >> 1))
					x25->condition |= X25_COND_OWN_RX_BUSY;
			}
			/*
			 *	If the window is full Ack it immediately, else
			 *	start the holdback timer.
			 */
			if (((x25->vl + x25->facilities.winsize_in) % modulus) == x25->vr) {
				x25->condition &= ~X25_COND_ACK_PENDING;
				x25_stop_timer(sk);
				x25_enquiry_response(sk);
			} else {
				x25->condition |= X25_COND_ACK_PENDING;
				x25_start_t2timer(sk);
			}
			break;

		case X25_INTERRUPT_CONFIRMATION:
			clear_bit(X25_INTERRUPT_FLAG, &x25->flags);
			break;

		case X25_INTERRUPT:
			if (sock_flag(sk, SOCK_URGINLINE))
				queued = !sock_queue_rcv_skb(sk, skb);
			else {
				skb_set_owner_r(skb, sk);
				skb_queue_tail(&x25->interrupt_in_queue, skb);
				queued = 1;
			}
			sk_send_sigurg(sk);
			x25_write_internal(sk, X25_INTERRUPT_CONFIRMATION);
			break;

		default:
			printk(KERN_WARNING "x25: unknown %02X in state 3\n", frametype);
			break;
	}

	return queued;
}

/*
 * State machine for state 4, Awaiting Reset Confirmation State.
 * The handling of the timer(s) is in file x25_timer.c
 * Handling of state 0 and connection release is in af_x25.c.
 */
static int x25_state4_machine(struct sock *sk, struct sk_buff *skb, int frametype)
{
	switch (frametype) {

		case X25_RESET_REQUEST:
			x25_write_internal(sk, X25_RESET_CONFIRMATION);
		case X25_RESET_CONFIRMATION: {
			struct x25_sock *x25 = x25_sk(sk);

			x25_stop_timer(sk);
			x25->condition = 0x00;
			x25->va        = 0;
			x25->vr        = 0;
			x25->vs        = 0;
			x25->vl        = 0;
			x25->state     = X25_STATE_3;
			x25_requeue_frames(sk);
			break;
		}
		case X25_CLEAR_REQUEST:
			x25_write_internal(sk, X25_CLEAR_CONFIRMATION);
			x25_disconnect(sk, 0, skb->data[3], skb->data[4]);
			break;

		default:
			break;
	}

	return 0;
}

/* Higher level upcall for a LAPB frame */
int x25_process_rx_frame(struct sock *sk, struct sk_buff *skb)
{
	struct x25_sock *x25 = x25_sk(sk);
	int queued = 0, frametype, ns, nr, q, d, m;

	if (x25->state == X25_STATE_0)
		return 0;

	frametype = x25_decode(sk, skb, &ns, &nr, &q, &d, &m);

	switch (x25->state) {
		case X25_STATE_1:
			queued = x25_state1_machine(sk, skb, frametype);
			break;
		case X25_STATE_2:
			queued = x25_state2_machine(sk, skb, frametype);
			break;
		case X25_STATE_3:
			queued = x25_state3_machine(sk, skb, frametype, ns, nr, q, d, m);
			break;
		case X25_STATE_4:
			queued = x25_state4_machine(sk, skb, frametype);
			break;
	}

	x25_kick(sk);

	return queued;
}

int x25_backlog_rcv(struct sock *sk, struct sk_buff *skb)
{
	int queued = x25_process_rx_frame(sk, skb);

	if (!queued)
		kfree_skb(skb);

	return 0;
}
