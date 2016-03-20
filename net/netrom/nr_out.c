/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Copyright Jonathan Naylor G4KLX (g4klx@g4klx.demon.co.uk)
 * Copyright Darryl Miles G7LED (dlm@g7led.demon.co.uk)
 */
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/slab.h>
#include <net/ax25.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <linux/uaccess.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <net/netrom.h>

/*
 *	This is where all NET/ROM frames pass, except for IP-over-NET/ROM which
 *	cannot be fragmented in this manner.
 */
void nr_output(struct sock *sk, struct sk_buff *skb)
{
	struct sk_buff *skbn;
	unsigned char transport[NR_TRANSPORT_LEN];
	int err, frontlen, len;

	if (skb->len - NR_TRANSPORT_LEN > NR_MAX_PACKET_SIZE) {
		/* Save a copy of the Transport Header */
		skb_copy_from_linear_data(skb, transport, NR_TRANSPORT_LEN);
		skb_pull(skb, NR_TRANSPORT_LEN);

		frontlen = skb_headroom(skb);

		while (skb->len > 0) {
			if ((skbn = sock_alloc_send_skb(sk, frontlen + NR_MAX_PACKET_SIZE, 0, &err)) == NULL)
				return;

			skb_reserve(skbn, frontlen);

			len = (NR_MAX_PACKET_SIZE > skb->len) ? skb->len : NR_MAX_PACKET_SIZE;

			/* Copy the user data */
			skb_copy_from_linear_data(skb, skb_put(skbn, len), len);
			skb_pull(skb, len);

			/* Duplicate the Transport Header */
			skb_push(skbn, NR_TRANSPORT_LEN);
			skb_copy_to_linear_data(skbn, transport,
						NR_TRANSPORT_LEN);
			if (skb->len > 0)
				skbn->data[4] |= NR_MORE_FLAG;

			skb_queue_tail(&sk->sk_write_queue, skbn); /* Throw it on the queue */
		}

		kfree_skb(skb);
	} else {
		skb_queue_tail(&sk->sk_write_queue, skb);		/* Throw it on the queue */
	}

	nr_kick(sk);
}

/*
 *	This procedure is passed a buffer descriptor for an iframe. It builds
 *	the rest of the control part of the frame and then writes it out.
 */
static void nr_send_iframe(struct sock *sk, struct sk_buff *skb)
{
	struct nr_sock *nr = nr_sk(sk);

	if (skb == NULL)
		return;

	skb->data[2] = nr->vs;
	skb->data[3] = nr->vr;

	if (nr->condition & NR_COND_OWN_RX_BUSY)
		skb->data[4] |= NR_CHOKE_FLAG;

	nr_start_idletimer(sk);

	nr_transmit_buffer(sk, skb);
}

void nr_send_nak_frame(struct sock *sk)
{
	struct sk_buff *skb, *skbn;
	struct nr_sock *nr = nr_sk(sk);

	if ((skb = skb_peek(&nr->ack_queue)) == NULL)
		return;

	if ((skbn = skb_clone(skb, GFP_ATOMIC)) == NULL)
		return;

	skbn->data[2] = nr->va;
	skbn->data[3] = nr->vr;

	if (nr->condition & NR_COND_OWN_RX_BUSY)
		skbn->data[4] |= NR_CHOKE_FLAG;

	nr_transmit_buffer(sk, skbn);

	nr->condition &= ~NR_COND_ACK_PENDING;
	nr->vl         = nr->vr;

	nr_stop_t1timer(sk);
}

void nr_kick(struct sock *sk)
{
	struct nr_sock *nr = nr_sk(sk);
	struct sk_buff *skb, *skbn;
	unsigned short start, end;

	if (nr->state != NR_STATE_3)
		return;

	if (nr->condition & NR_COND_PEER_RX_BUSY)
		return;

	if (!skb_peek(&sk->sk_write_queue))
		return;

	start = (skb_peek(&nr->ack_queue) == NULL) ? nr->va : nr->vs;
	end   = (nr->va + nr->window) % NR_MODULUS;

	if (start == end)
		return;

	nr->vs = start;

	/*
	 * Transmit data until either we're out of data to send or
	 * the window is full.
	 */

	/*
	 * Dequeue the frame and copy it.
	 */
	skb = skb_dequeue(&sk->sk_write_queue);

	do {
		if ((skbn = skb_clone(skb, GFP_ATOMIC)) == NULL) {
			skb_queue_head(&sk->sk_write_queue, skb);
			break;
		}

		skb_set_owner_w(skbn, sk);

		/*
		 * Transmit the frame copy.
		 */
		nr_send_iframe(sk, skbn);

		nr->vs = (nr->vs + 1) % NR_MODULUS;

		/*
		 * Requeue the original data frame.
		 */
		skb_queue_tail(&nr->ack_queue, skb);

	} while (nr->vs != end &&
		 (skb = skb_dequeue(&sk->sk_write_queue)) != NULL);

	nr->vl         = nr->vr;
	nr->condition &= ~NR_COND_ACK_PENDING;

	if (!nr_t1timer_running(sk))
		nr_start_t1timer(sk);
}

void nr_transmit_buffer(struct sock *sk, struct sk_buff *skb)
{
	struct nr_sock *nr = nr_sk(sk);
	unsigned char *dptr;

	/*
	 *	Add the protocol byte and network header.
	 */
	dptr = skb_push(skb, NR_NETWORK_LEN);

	memcpy(dptr, &nr->source_addr, AX25_ADDR_LEN);
	dptr[6] &= ~AX25_CBIT;
	dptr[6] &= ~AX25_EBIT;
	dptr[6] |= AX25_SSSID_SPARE;
	dptr += AX25_ADDR_LEN;

	memcpy(dptr, &nr->dest_addr, AX25_ADDR_LEN);
	dptr[6] &= ~AX25_CBIT;
	dptr[6] |= AX25_EBIT;
	dptr[6] |= AX25_SSSID_SPARE;
	dptr += AX25_ADDR_LEN;

	*dptr++ = sysctl_netrom_network_ttl_initialiser;

	if (!nr_route_frame(skb, NULL)) {
		kfree_skb(skb);
		nr_disconnect(sk, ENETUNREACH);
	}
}

/*
 * The following routines are taken from page 170 of the 7th ARRL Computer
 * Networking Conference paper, as is the whole state machine.
 */

void nr_establish_data_link(struct sock *sk)
{
	struct nr_sock *nr = nr_sk(sk);

	nr->condition = 0x00;
	nr->n2count   = 0;

	nr_write_internal(sk, NR_CONNREQ);

	nr_stop_t2timer(sk);
	nr_stop_t4timer(sk);
	nr_stop_idletimer(sk);
	nr_start_t1timer(sk);
}

/*
 * Never send a NAK when we are CHOKEd.
 */
void nr_enquiry_response(struct sock *sk)
{
	struct nr_sock *nr = nr_sk(sk);
	int frametype = NR_INFOACK;

	if (nr->condition & NR_COND_OWN_RX_BUSY) {
		frametype |= NR_CHOKE_FLAG;
	} else {
		if (skb_peek(&nr->reseq_queue) != NULL)
			frametype |= NR_NAK_FLAG;
	}

	nr_write_internal(sk, frametype);

	nr->vl         = nr->vr;
	nr->condition &= ~NR_COND_ACK_PENDING;
}

void nr_check_iframes_acked(struct sock *sk, unsigned short nr)
{
	struct nr_sock *nrom = nr_sk(sk);

	if (nrom->vs == nr) {
		nr_frames_acked(sk, nr);
		nr_stop_t1timer(sk);
		nrom->n2count = 0;
	} else {
		if (nrom->va != nr) {
			nr_frames_acked(sk, nr);
			nr_start_t1timer(sk);
		}
	}
}
