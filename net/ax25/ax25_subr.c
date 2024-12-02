// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 * Copyright (C) Alan Cox GW4PTS (alan@lxorguk.ukuu.org.uk)
 * Copyright (C) Jonathan Naylor G4KLX (g4klx@g4klx.demon.co.uk)
 * Copyright (C) Joerg Reuter DL1BKE (jreuter@yaina.de)
 * Copyright (C) Frederic Rible F1OAT (frible@teaser.fr)
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
#include <net/tcp_states.h>
#include <linux/uaccess.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>

/*
 *	This routine purges all the queues of frames.
 */
void ax25_clear_queues(ax25_cb *ax25)
{
	skb_queue_purge(&ax25->write_queue);
	skb_queue_purge(&ax25->ack_queue);
	skb_queue_purge(&ax25->reseq_queue);
	skb_queue_purge(&ax25->frag_queue);
}

/*
 * This routine purges the input queue of those frames that have been
 * acknowledged. This replaces the boxes labelled "V(a) <- N(r)" on the
 * SDL diagram.
 */
void ax25_frames_acked(ax25_cb *ax25, unsigned short nr)
{
	struct sk_buff *skb;

	/*
	 * Remove all the ack-ed frames from the ack queue.
	 */
	if (ax25->va != nr) {
		while (skb_peek(&ax25->ack_queue) != NULL && ax25->va != nr) {
			skb = skb_dequeue(&ax25->ack_queue);
			kfree_skb(skb);
			ax25->va = (ax25->va + 1) % ax25->modulus;
		}
	}
}

void ax25_requeue_frames(ax25_cb *ax25)
{
	struct sk_buff *skb;

	/*
	 * Requeue all the un-ack-ed frames on the output queue to be picked
	 * up by ax25_kick called from the timer. This arrangement handles the
	 * possibility of an empty output queue.
	 */
	while ((skb = skb_dequeue_tail(&ax25->ack_queue)) != NULL)
		skb_queue_head(&ax25->write_queue, skb);
}

/*
 *	Validate that the value of nr is between va and vs. Return true or
 *	false for testing.
 */
int ax25_validate_nr(ax25_cb *ax25, unsigned short nr)
{
	unsigned short vc = ax25->va;

	while (vc != ax25->vs) {
		if (nr == vc) return 1;
		vc = (vc + 1) % ax25->modulus;
	}

	if (nr == ax25->vs) return 1;

	return 0;
}

/*
 *	This routine is the centralised routine for parsing the control
 *	information for the different frame formats.
 */
int ax25_decode(ax25_cb *ax25, struct sk_buff *skb, int *ns, int *nr, int *pf)
{
	unsigned char *frame;
	int frametype = AX25_ILLEGAL;

	frame = skb->data;
	*ns = *nr = *pf = 0;

	if (ax25->modulus == AX25_MODULUS) {
		if ((frame[0] & AX25_S) == 0) {
			frametype = AX25_I;			/* I frame - carries NR/NS/PF */
			*ns = (frame[0] >> 1) & 0x07;
			*nr = (frame[0] >> 5) & 0x07;
			*pf = frame[0] & AX25_PF;
		} else if ((frame[0] & AX25_U) == 1) { 	/* S frame - take out PF/NR */
			frametype = frame[0] & 0x0F;
			*nr = (frame[0] >> 5) & 0x07;
			*pf = frame[0] & AX25_PF;
		} else if ((frame[0] & AX25_U) == 3) { 	/* U frame - take out PF */
			frametype = frame[0] & ~AX25_PF;
			*pf = frame[0] & AX25_PF;
		}
		skb_pull(skb, 1);
	} else {
		if ((frame[0] & AX25_S) == 0) {
			frametype = AX25_I;			/* I frame - carries NR/NS/PF */
			*ns = (frame[0] >> 1) & 0x7F;
			*nr = (frame[1] >> 1) & 0x7F;
			*pf = frame[1] & AX25_EPF;
			skb_pull(skb, 2);
		} else if ((frame[0] & AX25_U) == 1) { 	/* S frame - take out PF/NR */
			frametype = frame[0] & 0x0F;
			*nr = (frame[1] >> 1) & 0x7F;
			*pf = frame[1] & AX25_EPF;
			skb_pull(skb, 2);
		} else if ((frame[0] & AX25_U) == 3) { 	/* U frame - take out PF */
			frametype = frame[0] & ~AX25_PF;
			*pf = frame[0] & AX25_PF;
			skb_pull(skb, 1);
		}
	}

	return frametype;
}

/*
 *	This routine is called when the HDLC layer internally  generates a
 *	command or  response  for  the remote machine ( eg. RR, UA etc. ).
 *	Only supervisory or unnumbered frames are processed.
 */
void ax25_send_control(ax25_cb *ax25, int frametype, int poll_bit, int type)
{
	struct sk_buff *skb;
	unsigned char  *dptr;

	if ((skb = alloc_skb(ax25->ax25_dev->dev->hard_header_len + 2, GFP_ATOMIC)) == NULL)
		return;

	skb_reserve(skb, ax25->ax25_dev->dev->hard_header_len);

	skb_reset_network_header(skb);

	/* Assume a response - address structure for DTE */
	if (ax25->modulus == AX25_MODULUS) {
		dptr = skb_put(skb, 1);
		*dptr = frametype;
		*dptr |= (poll_bit) ? AX25_PF : 0;
		if ((frametype & AX25_U) == AX25_S)		/* S frames carry NR */
			*dptr |= (ax25->vr << 5);
	} else {
		if ((frametype & AX25_U) == AX25_U) {
			dptr = skb_put(skb, 1);
			*dptr = frametype;
			*dptr |= (poll_bit) ? AX25_PF : 0;
		} else {
			dptr = skb_put(skb, 2);
			dptr[0] = frametype;
			dptr[1] = (ax25->vr << 1);
			dptr[1] |= (poll_bit) ? AX25_EPF : 0;
		}
	}

	ax25_transmit_buffer(ax25, skb, type);
}

/*
 *	Send a 'DM' to an unknown connection attempt, or an invalid caller.
 *
 *	Note: src here is the sender, thus it's the target of the DM
 */
void ax25_return_dm(struct net_device *dev, ax25_address *src, ax25_address *dest, ax25_digi *digi)
{
	struct sk_buff *skb;
	char *dptr;
	ax25_digi retdigi;

	if (dev == NULL)
		return;

	if ((skb = alloc_skb(dev->hard_header_len + 1, GFP_ATOMIC)) == NULL)
		return;	/* Next SABM will get DM'd */

	skb_reserve(skb, dev->hard_header_len);
	skb_reset_network_header(skb);

	ax25_digi_invert(digi, &retdigi);

	dptr = skb_put(skb, 1);

	*dptr = AX25_DM | AX25_PF;

	/*
	 *	Do the address ourselves
	 */
	dptr  = skb_push(skb, ax25_addr_size(digi));
	dptr += ax25_addr_build(dptr, dest, src, &retdigi, AX25_RESPONSE, AX25_MODULUS);

	ax25_queue_xmit(skb, dev);
}

/*
 *	Exponential backoff for AX.25
 */
void ax25_calculate_t1(ax25_cb *ax25)
{
	int n, t = 2;

	switch (ax25->backoff) {
	case 0:
		break;

	case 1:
		t += 2 * ax25->n2count;
		break;

	case 2:
		for (n = 0; n < ax25->n2count; n++)
			t *= 2;
		if (t > 8) t = 8;
		break;
	}

	ax25->t1 = t * ax25->rtt;
}

/*
 *	Calculate the Round Trip Time
 */
void ax25_calculate_rtt(ax25_cb *ax25)
{
	if (ax25->backoff == 0)
		return;

	if (ax25_t1timer_running(ax25) && ax25->n2count == 0)
		ax25->rtt = (9 * ax25->rtt + ax25->t1 - ax25_display_timer(&ax25->t1timer)) / 10;

	if (ax25->rtt < AX25_T1CLAMPLO)
		ax25->rtt = AX25_T1CLAMPLO;

	if (ax25->rtt > AX25_T1CLAMPHI)
		ax25->rtt = AX25_T1CLAMPHI;
}

void ax25_disconnect(ax25_cb *ax25, int reason)
{
	ax25_clear_queues(ax25);

	if (reason == ENETUNREACH) {
		del_timer_sync(&ax25->timer);
		del_timer_sync(&ax25->t1timer);
		del_timer_sync(&ax25->t2timer);
		del_timer_sync(&ax25->t3timer);
		del_timer_sync(&ax25->idletimer);
	} else {
		if (ax25->sk && !sock_flag(ax25->sk, SOCK_DESTROY))
			ax25_stop_heartbeat(ax25);
		ax25_stop_t1timer(ax25);
		ax25_stop_t2timer(ax25);
		ax25_stop_t3timer(ax25);
		ax25_stop_idletimer(ax25);
	}

	ax25->state = AX25_STATE_0;

	ax25_link_failed(ax25, reason);

	if (ax25->sk != NULL) {
		local_bh_disable();
		bh_lock_sock(ax25->sk);
		ax25->sk->sk_state     = TCP_CLOSE;
		ax25->sk->sk_err       = reason;
		ax25->sk->sk_shutdown |= SEND_SHUTDOWN;
		if (!sock_flag(ax25->sk, SOCK_DEAD)) {
			ax25->sk->sk_state_change(ax25->sk);
			sock_set_flag(ax25->sk, SOCK_DEAD);
		}
		bh_unlock_sock(ax25->sk);
		local_bh_enable();
	}
}
