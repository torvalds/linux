/*
 *	LAPB release 002
 *
 *	This code REQUIRES 2.1.15 or higher/ NET3.038
 *
 *	This module:
 *		This module is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	History
 *	LAPB 001	Jonathan Naylor	Started Coding
 *	LAPB 002	Jonathan Naylor	New timer architecture.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/inet.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <net/sock.h>
#include <asm/uaccess.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <net/lapb.h>

/*
 *  This procedure is passed a buffer descriptor for an iframe. It builds
 *  the rest of the control part of the frame and then writes it out.
 */
static void lapb_send_iframe(struct lapb_cb *lapb, struct sk_buff *skb, int poll_bit)
{
	unsigned char *frame;

	if (!skb)
		return;

	if (lapb->mode & LAPB_EXTENDED) {
		frame = skb_push(skb, 2);

		frame[0] = LAPB_I;
		frame[0] |= lapb->vs << 1;
		frame[1] = poll_bit ? LAPB_EPF : 0;
		frame[1] |= lapb->vr << 1;
	} else {
		frame = skb_push(skb, 1);

		*frame = LAPB_I;
		*frame |= poll_bit ? LAPB_SPF : 0;
		*frame |= lapb->vr << 5;
		*frame |= lapb->vs << 1;
	}

	lapb_dbg(1, "(%p) S%d TX I(%d) S%d R%d\n",
		 lapb->dev, lapb->state, poll_bit, lapb->vs, lapb->vr);

	lapb_transmit_buffer(lapb, skb, LAPB_COMMAND);
}

void lapb_kick(struct lapb_cb *lapb)
{
	struct sk_buff *skb, *skbn;
	unsigned short modulus, start, end;

	modulus = (lapb->mode & LAPB_EXTENDED) ? LAPB_EMODULUS : LAPB_SMODULUS;
	start = !skb_peek(&lapb->ack_queue) ? lapb->va : lapb->vs;
	end   = (lapb->va + lapb->window) % modulus;

	if (!(lapb->condition & LAPB_PEER_RX_BUSY_CONDITION) &&
	    start != end && skb_peek(&lapb->write_queue)) {
		lapb->vs = start;

		/*
		 * Dequeue the frame and copy it.
		 */
		skb = skb_dequeue(&lapb->write_queue);

		do {
			if ((skbn = skb_clone(skb, GFP_ATOMIC)) == NULL) {
				skb_queue_head(&lapb->write_queue, skb);
				break;
			}

			if (skb->sk)
				skb_set_owner_w(skbn, skb->sk);

			/*
			 * Transmit the frame copy.
			 */
			lapb_send_iframe(lapb, skbn, LAPB_POLLOFF);

			lapb->vs = (lapb->vs + 1) % modulus;

			/*
			 * Requeue the original data frame.
			 */
			skb_queue_tail(&lapb->ack_queue, skb);

		} while (lapb->vs != end && (skb = skb_dequeue(&lapb->write_queue)) != NULL);

		lapb->condition &= ~LAPB_ACK_PENDING_CONDITION;

		if (!lapb_t1timer_running(lapb))
			lapb_start_t1timer(lapb);
	}
}

void lapb_transmit_buffer(struct lapb_cb *lapb, struct sk_buff *skb, int type)
{
	unsigned char *ptr;

	ptr = skb_push(skb, 1);

	if (lapb->mode & LAPB_MLP) {
		if (lapb->mode & LAPB_DCE) {
			if (type == LAPB_COMMAND)
				*ptr = LAPB_ADDR_C;
			if (type == LAPB_RESPONSE)
				*ptr = LAPB_ADDR_D;
		} else {
			if (type == LAPB_COMMAND)
				*ptr = LAPB_ADDR_D;
			if (type == LAPB_RESPONSE)
				*ptr = LAPB_ADDR_C;
		}
	} else {
		if (lapb->mode & LAPB_DCE) {
			if (type == LAPB_COMMAND)
				*ptr = LAPB_ADDR_A;
			if (type == LAPB_RESPONSE)
				*ptr = LAPB_ADDR_B;
		} else {
			if (type == LAPB_COMMAND)
				*ptr = LAPB_ADDR_B;
			if (type == LAPB_RESPONSE)
				*ptr = LAPB_ADDR_A;
		}
	}

	lapb_dbg(2, "(%p) S%d TX %02X %02X %02X\n",
		 lapb->dev, lapb->state,
		 skb->data[0], skb->data[1], skb->data[2]);

	if (!lapb_data_transmit(lapb, skb))
		kfree_skb(skb);
}

void lapb_establish_data_link(struct lapb_cb *lapb)
{
	lapb->condition = 0x00;
	lapb->n2count   = 0;

	if (lapb->mode & LAPB_EXTENDED) {
		lapb_dbg(1, "(%p) S%d TX SABME(1)\n", lapb->dev, lapb->state);
		lapb_send_control(lapb, LAPB_SABME, LAPB_POLLON, LAPB_COMMAND);
	} else {
		lapb_dbg(1, "(%p) S%d TX SABM(1)\n", lapb->dev, lapb->state);
		lapb_send_control(lapb, LAPB_SABM, LAPB_POLLON, LAPB_COMMAND);
	}

	lapb_start_t1timer(lapb);
	lapb_stop_t2timer(lapb);
}

void lapb_enquiry_response(struct lapb_cb *lapb)
{
	lapb_dbg(1, "(%p) S%d TX RR(1) R%d\n",
		 lapb->dev, lapb->state, lapb->vr);

	lapb_send_control(lapb, LAPB_RR, LAPB_POLLON, LAPB_RESPONSE);

	lapb->condition &= ~LAPB_ACK_PENDING_CONDITION;
}

void lapb_timeout_response(struct lapb_cb *lapb)
{
	lapb_dbg(1, "(%p) S%d TX RR(0) R%d\n",
		 lapb->dev, lapb->state, lapb->vr);
	lapb_send_control(lapb, LAPB_RR, LAPB_POLLOFF, LAPB_RESPONSE);

	lapb->condition &= ~LAPB_ACK_PENDING_CONDITION;
}

void lapb_check_iframes_acked(struct lapb_cb *lapb, unsigned short nr)
{
	if (lapb->vs == nr) {
		lapb_frames_acked(lapb, nr);
		lapb_stop_t1timer(lapb);
		lapb->n2count = 0;
	} else if (lapb->va != nr) {
		lapb_frames_acked(lapb, nr);
		lapb_start_t1timer(lapb);
	}
}

void lapb_check_need_response(struct lapb_cb *lapb, int type, int pf)
{
	if (type == LAPB_COMMAND && pf)
		lapb_enquiry_response(lapb);
}
