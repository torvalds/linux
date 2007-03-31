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
#include <net/ax25.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/tcp_states.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <net/netrom.h>

static int nr_queue_rx_frame(struct sock *sk, struct sk_buff *skb, int more)
{
	struct sk_buff *skbo, *skbn = skb;
	struct nr_sock *nr = nr_sk(sk);

	skb_pull(skb, NR_NETWORK_LEN + NR_TRANSPORT_LEN);

	nr_start_idletimer(sk);

	if (more) {
		nr->fraglen += skb->len;
		skb_queue_tail(&nr->frag_queue, skb);
		return 0;
	}

	if (!more && nr->fraglen > 0) {	/* End of fragment */
		nr->fraglen += skb->len;
		skb_queue_tail(&nr->frag_queue, skb);

		if ((skbn = alloc_skb(nr->fraglen, GFP_ATOMIC)) == NULL)
			return 1;

		skb_reset_transport_header(skbn);

		while ((skbo = skb_dequeue(&nr->frag_queue)) != NULL) {
			skb_copy_from_linear_data(skbo,
						  skb_put(skbn, skbo->len),
						  skbo->len);
			kfree_skb(skbo);
		}

		nr->fraglen = 0;
	}

	return sock_queue_rcv_skb(sk, skbn);
}

/*
 * State machine for state 1, Awaiting Connection State.
 * The handling of the timer(s) is in file nr_timer.c.
 * Handling of state 0 and connection release is in netrom.c.
 */
static int nr_state1_machine(struct sock *sk, struct sk_buff *skb,
	int frametype)
{
	switch (frametype) {
	case NR_CONNACK: {
		struct nr_sock *nr = nr_sk(sk);

		nr_stop_t1timer(sk);
		nr_start_idletimer(sk);
		nr->your_index = skb->data[17];
		nr->your_id    = skb->data[18];
		nr->vs	       = 0;
		nr->va	       = 0;
		nr->vr	       = 0;
		nr->vl	       = 0;
		nr->state      = NR_STATE_3;
		nr->n2count    = 0;
		nr->window     = skb->data[20];
		sk->sk_state   = TCP_ESTABLISHED;
		if (!sock_flag(sk, SOCK_DEAD))
			sk->sk_state_change(sk);
		break;
	}

	case NR_CONNACK | NR_CHOKE_FLAG:
		nr_disconnect(sk, ECONNREFUSED);
		break;

	case NR_RESET:
		if (sysctl_netrom_reset_circuit)
			nr_disconnect(sk, ECONNRESET);
		break;

	default:
		break;
	}
	return 0;
}

/*
 * State machine for state 2, Awaiting Release State.
 * The handling of the timer(s) is in file nr_timer.c
 * Handling of state 0 and connection release is in netrom.c.
 */
static int nr_state2_machine(struct sock *sk, struct sk_buff *skb,
	int frametype)
{
	switch (frametype) {
	case NR_CONNACK | NR_CHOKE_FLAG:
		nr_disconnect(sk, ECONNRESET);
		break;

	case NR_DISCREQ:
		nr_write_internal(sk, NR_DISCACK);

	case NR_DISCACK:
		nr_disconnect(sk, 0);
		break;

	case NR_RESET:
		if (sysctl_netrom_reset_circuit)
			nr_disconnect(sk, ECONNRESET);
		break;

	default:
		break;
	}
	return 0;
}

/*
 * State machine for state 3, Connected State.
 * The handling of the timer(s) is in file nr_timer.c
 * Handling of state 0 and connection release is in netrom.c.
 */
static int nr_state3_machine(struct sock *sk, struct sk_buff *skb, int frametype)
{
	struct nr_sock *nrom = nr_sk(sk);
	struct sk_buff_head temp_queue;
	struct sk_buff *skbn;
	unsigned short save_vr;
	unsigned short nr, ns;
	int queued = 0;

	nr = skb->data[18];
	ns = skb->data[17];

	switch (frametype) {
	case NR_CONNREQ:
		nr_write_internal(sk, NR_CONNACK);
		break;

	case NR_DISCREQ:
		nr_write_internal(sk, NR_DISCACK);
		nr_disconnect(sk, 0);
		break;

	case NR_CONNACK | NR_CHOKE_FLAG:
	case NR_DISCACK:
		nr_disconnect(sk, ECONNRESET);
		break;

	case NR_INFOACK:
	case NR_INFOACK | NR_CHOKE_FLAG:
	case NR_INFOACK | NR_NAK_FLAG:
	case NR_INFOACK | NR_NAK_FLAG | NR_CHOKE_FLAG:
		if (frametype & NR_CHOKE_FLAG) {
			nrom->condition |= NR_COND_PEER_RX_BUSY;
			nr_start_t4timer(sk);
		} else {
			nrom->condition &= ~NR_COND_PEER_RX_BUSY;
			nr_stop_t4timer(sk);
		}
		if (!nr_validate_nr(sk, nr)) {
			break;
		}
		if (frametype & NR_NAK_FLAG) {
			nr_frames_acked(sk, nr);
			nr_send_nak_frame(sk);
		} else {
			if (nrom->condition & NR_COND_PEER_RX_BUSY) {
				nr_frames_acked(sk, nr);
			} else {
				nr_check_iframes_acked(sk, nr);
			}
		}
		break;

	case NR_INFO:
	case NR_INFO | NR_NAK_FLAG:
	case NR_INFO | NR_CHOKE_FLAG:
	case NR_INFO | NR_MORE_FLAG:
	case NR_INFO | NR_NAK_FLAG | NR_CHOKE_FLAG:
	case NR_INFO | NR_CHOKE_FLAG | NR_MORE_FLAG:
	case NR_INFO | NR_NAK_FLAG | NR_MORE_FLAG:
	case NR_INFO | NR_NAK_FLAG | NR_CHOKE_FLAG | NR_MORE_FLAG:
		if (frametype & NR_CHOKE_FLAG) {
			nrom->condition |= NR_COND_PEER_RX_BUSY;
			nr_start_t4timer(sk);
		} else {
			nrom->condition &= ~NR_COND_PEER_RX_BUSY;
			nr_stop_t4timer(sk);
		}
		if (nr_validate_nr(sk, nr)) {
			if (frametype & NR_NAK_FLAG) {
				nr_frames_acked(sk, nr);
				nr_send_nak_frame(sk);
			} else {
				if (nrom->condition & NR_COND_PEER_RX_BUSY) {
					nr_frames_acked(sk, nr);
				} else {
					nr_check_iframes_acked(sk, nr);
				}
			}
		}
		queued = 1;
		skb_queue_head(&nrom->reseq_queue, skb);
		if (nrom->condition & NR_COND_OWN_RX_BUSY)
			break;
		skb_queue_head_init(&temp_queue);
		do {
			save_vr = nrom->vr;
			while ((skbn = skb_dequeue(&nrom->reseq_queue)) != NULL) {
				ns = skbn->data[17];
				if (ns == nrom->vr) {
					if (nr_queue_rx_frame(sk, skbn, frametype & NR_MORE_FLAG) == 0) {
						nrom->vr = (nrom->vr + 1) % NR_MODULUS;
					} else {
						nrom->condition |= NR_COND_OWN_RX_BUSY;
						skb_queue_tail(&temp_queue, skbn);
					}
				} else if (nr_in_rx_window(sk, ns)) {
					skb_queue_tail(&temp_queue, skbn);
				} else {
					kfree_skb(skbn);
				}
			}
			while ((skbn = skb_dequeue(&temp_queue)) != NULL) {
				skb_queue_tail(&nrom->reseq_queue, skbn);
			}
		} while (save_vr != nrom->vr);
		/*
		 * Window is full, ack it immediately.
		 */
		if (((nrom->vl + nrom->window) % NR_MODULUS) == nrom->vr) {
			nr_enquiry_response(sk);
		} else {
			if (!(nrom->condition & NR_COND_ACK_PENDING)) {
				nrom->condition |= NR_COND_ACK_PENDING;
				nr_start_t2timer(sk);
			}
		}
		break;

	case NR_RESET:
		if (sysctl_netrom_reset_circuit)
			nr_disconnect(sk, ECONNRESET);
		break;

	default:
		break;
	}
	return queued;
}

/* Higher level upcall for a LAPB frame - called with sk locked */
int nr_process_rx_frame(struct sock *sk, struct sk_buff *skb)
{
	struct nr_sock *nr = nr_sk(sk);
	int queued = 0, frametype;

	if (nr->state == NR_STATE_0)
		return 0;

	frametype = skb->data[19];

	switch (nr->state) {
	case NR_STATE_1:
		queued = nr_state1_machine(sk, skb, frametype);
		break;
	case NR_STATE_2:
		queued = nr_state2_machine(sk, skb, frametype);
		break;
	case NR_STATE_3:
		queued = nr_state3_machine(sk, skb, frametype);
		break;
	}

	nr_kick(sk);

	return queued;
}
