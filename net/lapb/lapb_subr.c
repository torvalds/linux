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
 *	This routine purges all the queues of frames.
 */
void lapb_clear_queues(struct lapb_cb *lapb)
{
	skb_queue_purge(&lapb->write_queue);
	skb_queue_purge(&lapb->ack_queue);
}

/*
 * This routine purges the input queue of those frames that have been
 * acknowledged. This replaces the boxes labelled "V(a) <- N(r)" on the
 * SDL diagram.
 */
void lapb_frames_acked(struct lapb_cb *lapb, unsigned short nr)
{
	struct sk_buff *skb;
	int modulus;

	modulus = (lapb->mode & LAPB_EXTENDED) ? LAPB_EMODULUS : LAPB_SMODULUS;

	/*
	 * Remove all the ack-ed frames from the ack queue.
	 */
	if (lapb->va != nr)
		while (skb_peek(&lapb->ack_queue) && lapb->va != nr) {
			skb = skb_dequeue(&lapb->ack_queue);
			kfree_skb(skb);
			lapb->va = (lapb->va + 1) % modulus;
		}
}

void lapb_requeue_frames(struct lapb_cb *lapb)
{
	struct sk_buff *skb, *skb_prev = NULL;

	/*
	 * Requeue all the un-ack-ed frames on the output queue to be picked
	 * up by lapb_kick called from the timer. This arrangement handles the
	 * possibility of an empty output queue.
	 */
	while ((skb = skb_dequeue(&lapb->ack_queue)) != NULL) {
		if (!skb_prev)
			skb_queue_head(&lapb->write_queue, skb);
		else
			skb_append(skb_prev, skb, &lapb->write_queue);
		skb_prev = skb;
	}
}

/*
 *	Validate that the value of nr is between va and vs. Return true or
 *	false for testing.
 */
int lapb_validate_nr(struct lapb_cb *lapb, unsigned short nr)
{
	unsigned short vc = lapb->va;
	int modulus;

	modulus = (lapb->mode & LAPB_EXTENDED) ? LAPB_EMODULUS : LAPB_SMODULUS;

	while (vc != lapb->vs) {
		if (nr == vc)
			return 1;
		vc = (vc + 1) % modulus;
	}

	return nr == lapb->vs;
}

/*
 *	This routine is the centralised routine for parsing the control
 *	information for the different frame formats.
 */
int lapb_decode(struct lapb_cb *lapb, struct sk_buff *skb,
		struct lapb_frame *frame)
{
	frame->type = LAPB_ILLEGAL;

#if LAPB_DEBUG > 2
	printk(KERN_DEBUG "lapb: (%p) S%d RX %02X %02X %02X\n",
	       lapb->dev, lapb->state,
	       skb->data[0], skb->data[1], skb->data[2]);
#endif

	/* We always need to look at 2 bytes, sometimes we need
	 * to look at 3 and those cases are handled below.
	 */
	if (!pskb_may_pull(skb, 2))
		return -1;

	if (lapb->mode & LAPB_MLP) {
		if (lapb->mode & LAPB_DCE) {
			if (skb->data[0] == LAPB_ADDR_D)
				frame->cr = LAPB_COMMAND;
			if (skb->data[0] == LAPB_ADDR_C)
				frame->cr = LAPB_RESPONSE;
		} else {
			if (skb->data[0] == LAPB_ADDR_C)
				frame->cr = LAPB_COMMAND;
			if (skb->data[0] == LAPB_ADDR_D)
				frame->cr = LAPB_RESPONSE;
		}
	} else {
		if (lapb->mode & LAPB_DCE) {
			if (skb->data[0] == LAPB_ADDR_B)
				frame->cr = LAPB_COMMAND;
			if (skb->data[0] == LAPB_ADDR_A)
				frame->cr = LAPB_RESPONSE;
		} else {
			if (skb->data[0] == LAPB_ADDR_A)
				frame->cr = LAPB_COMMAND;
			if (skb->data[0] == LAPB_ADDR_B)
				frame->cr = LAPB_RESPONSE;
		}
	}

	skb_pull(skb, 1);

	if (lapb->mode & LAPB_EXTENDED) {
		if (!(skb->data[0] & LAPB_S)) {
			if (!pskb_may_pull(skb, 2))
				return -1;
			/*
			 * I frame - carries NR/NS/PF
			 */
			frame->type       = LAPB_I;
			frame->ns         = (skb->data[0] >> 1) & 0x7F;
			frame->nr         = (skb->data[1] >> 1) & 0x7F;
			frame->pf         = skb->data[1] & LAPB_EPF;
			frame->control[0] = skb->data[0];
			frame->control[1] = skb->data[1];
			skb_pull(skb, 2);
		} else if ((skb->data[0] & LAPB_U) == 1) {
			if (!pskb_may_pull(skb, 2))
				return -1;
			/*
			 * S frame - take out PF/NR
			 */
			frame->type       = skb->data[0] & 0x0F;
			frame->nr         = (skb->data[1] >> 1) & 0x7F;
			frame->pf         = skb->data[1] & LAPB_EPF;
			frame->control[0] = skb->data[0];
			frame->control[1] = skb->data[1];
			skb_pull(skb, 2);
		} else if ((skb->data[0] & LAPB_U) == 3) {
			/*
			 * U frame - take out PF
			 */
			frame->type       = skb->data[0] & ~LAPB_SPF;
			frame->pf         = skb->data[0] & LAPB_SPF;
			frame->control[0] = skb->data[0];
			frame->control[1] = 0x00;
			skb_pull(skb, 1);
		}
	} else {
		if (!(skb->data[0] & LAPB_S)) {
			/*
			 * I frame - carries NR/NS/PF
			 */
			frame->type = LAPB_I;
			frame->ns   = (skb->data[0] >> 1) & 0x07;
			frame->nr   = (skb->data[0] >> 5) & 0x07;
			frame->pf   = skb->data[0] & LAPB_SPF;
		} else if ((skb->data[0] & LAPB_U) == 1) {
			/*
			 * S frame - take out PF/NR
			 */
			frame->type = skb->data[0] & 0x0F;
			frame->nr   = (skb->data[0] >> 5) & 0x07;
			frame->pf   = skb->data[0] & LAPB_SPF;
		} else if ((skb->data[0] & LAPB_U) == 3) {
			/*
			 * U frame - take out PF
			 */
			frame->type = skb->data[0] & ~LAPB_SPF;
			frame->pf   = skb->data[0] & LAPB_SPF;
		}

		frame->control[0] = skb->data[0];

		skb_pull(skb, 1);
	}

	return 0;
}

/*
 *	This routine is called when the HDLC layer internally  generates a
 *	command or  response  for  the remote machine ( eg. RR, UA etc. ).
 *	Only supervisory or unnumbered frames are processed, FRMRs are handled
 *	by lapb_transmit_frmr below.
 */
void lapb_send_control(struct lapb_cb *lapb, int frametype,
		       int poll_bit, int type)
{
	struct sk_buff *skb;
	unsigned char  *dptr;

	if ((skb = alloc_skb(LAPB_HEADER_LEN + 3, GFP_ATOMIC)) == NULL)
		return;

	skb_reserve(skb, LAPB_HEADER_LEN + 1);

	if (lapb->mode & LAPB_EXTENDED) {
		if ((frametype & LAPB_U) == LAPB_U) {
			dptr   = skb_put(skb, 1);
			*dptr  = frametype;
			*dptr |= poll_bit ? LAPB_SPF : 0;
		} else {
			dptr     = skb_put(skb, 2);
			dptr[0]  = frametype;
			dptr[1]  = (lapb->vr << 1);
			dptr[1] |= poll_bit ? LAPB_EPF : 0;
		}
	} else {
		dptr   = skb_put(skb, 1);
		*dptr  = frametype;
		*dptr |= poll_bit ? LAPB_SPF : 0;
		if ((frametype & LAPB_U) == LAPB_S)	/* S frames carry NR */
			*dptr |= (lapb->vr << 5);
	}

	lapb_transmit_buffer(lapb, skb, type);
}

/*
 *	This routine generates FRMRs based on information previously stored in
 *	the LAPB control block.
 */
void lapb_transmit_frmr(struct lapb_cb *lapb)
{
	struct sk_buff *skb;
	unsigned char  *dptr;

	if ((skb = alloc_skb(LAPB_HEADER_LEN + 7, GFP_ATOMIC)) == NULL)
		return;

	skb_reserve(skb, LAPB_HEADER_LEN + 1);

	if (lapb->mode & LAPB_EXTENDED) {
		dptr    = skb_put(skb, 6);
		*dptr++ = LAPB_FRMR;
		*dptr++ = lapb->frmr_data.control[0];
		*dptr++ = lapb->frmr_data.control[1];
		*dptr++ = (lapb->vs << 1) & 0xFE;
		*dptr   = (lapb->vr << 1) & 0xFE;
		if (lapb->frmr_data.cr == LAPB_RESPONSE)
			*dptr |= 0x01;
		dptr++;
		*dptr++ = lapb->frmr_type;

#if LAPB_DEBUG > 1
	printk(KERN_DEBUG "lapb: (%p) S%d TX FRMR %02X %02X %02X %02X %02X\n",
	       lapb->dev, lapb->state,
	       skb->data[1], skb->data[2], skb->data[3],
	       skb->data[4], skb->data[5]);
#endif
	} else {
		dptr    = skb_put(skb, 4);
		*dptr++ = LAPB_FRMR;
		*dptr++ = lapb->frmr_data.control[0];
		*dptr   = (lapb->vs << 1) & 0x0E;
		*dptr  |= (lapb->vr << 5) & 0xE0;
		if (lapb->frmr_data.cr == LAPB_RESPONSE)
			*dptr |= 0x10;
		dptr++;
		*dptr++ = lapb->frmr_type;

#if LAPB_DEBUG > 1
	printk(KERN_DEBUG "lapb: (%p) S%d TX FRMR %02X %02X %02X\n",
	       lapb->dev, lapb->state, skb->data[1],
	       skb->data[2], skb->data[3]);
#endif
	}

	lapb_transmit_buffer(lapb, skb, LAPB_RESPONSE);
}
