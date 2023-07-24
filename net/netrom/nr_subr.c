// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 * Copyright Jonathan Naylor G4KLX (g4klx@g4klx.demon.co.uk)
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
#include <net/netrom.h>

/*
 *	This routine purges all of the queues of frames.
 */
void nr_clear_queues(struct sock *sk)
{
	struct nr_sock *nr = nr_sk(sk);

	skb_queue_purge(&sk->sk_write_queue);
	skb_queue_purge(&nr->ack_queue);
	skb_queue_purge(&nr->reseq_queue);
	skb_queue_purge(&nr->frag_queue);
}

/*
 * This routine purges the input queue of those frames that have been
 * acknowledged. This replaces the boxes labelled "V(a) <- N(r)" on the
 * SDL diagram.
 */
void nr_frames_acked(struct sock *sk, unsigned short nr)
{
	struct nr_sock *nrom = nr_sk(sk);
	struct sk_buff *skb;

	/*
	 * Remove all the ack-ed frames from the ack queue.
	 */
	if (nrom->va != nr) {
		while (skb_peek(&nrom->ack_queue) != NULL && nrom->va != nr) {
			skb = skb_dequeue(&nrom->ack_queue);
			kfree_skb(skb);
			nrom->va = (nrom->va + 1) % NR_MODULUS;
		}
	}
}

/*
 * Requeue all the un-ack-ed frames on the output queue to be picked
 * up by nr_kick called from the timer. This arrangement handles the
 * possibility of an empty output queue.
 */
void nr_requeue_frames(struct sock *sk)
{
	struct sk_buff *skb, *skb_prev = NULL;

	while ((skb = skb_dequeue(&nr_sk(sk)->ack_queue)) != NULL) {
		if (skb_prev == NULL)
			skb_queue_head(&sk->sk_write_queue, skb);
		else
			skb_append(skb_prev, skb, &sk->sk_write_queue);
		skb_prev = skb;
	}
}

/*
 *	Validate that the value of nr is between va and vs. Return true or
 *	false for testing.
 */
int nr_validate_nr(struct sock *sk, unsigned short nr)
{
	struct nr_sock *nrom = nr_sk(sk);
	unsigned short vc = nrom->va;

	while (vc != nrom->vs) {
		if (nr == vc) return 1;
		vc = (vc + 1) % NR_MODULUS;
	}

	return nr == nrom->vs;
}

/*
 *	Check that ns is within the receive window.
 */
int nr_in_rx_window(struct sock *sk, unsigned short ns)
{
	struct nr_sock *nr = nr_sk(sk);
	unsigned short vc = nr->vr;
	unsigned short vt = (nr->vl + nr->window) % NR_MODULUS;

	while (vc != vt) {
		if (ns == vc) return 1;
		vc = (vc + 1) % NR_MODULUS;
	}

	return 0;
}

/*
 *  This routine is called when the HDLC layer internally generates a
 *  control frame.
 */
void nr_write_internal(struct sock *sk, int frametype)
{
	struct nr_sock *nr = nr_sk(sk);
	struct sk_buff *skb;
	unsigned char  *dptr;
	int len, timeout;

	len = NR_TRANSPORT_LEN;

	switch (frametype & 0x0F) {
	case NR_CONNREQ:
		len += 17;
		break;
	case NR_CONNACK:
		len += (nr->bpqext) ? 2 : 1;
		break;
	case NR_DISCREQ:
	case NR_DISCACK:
	case NR_INFOACK:
		break;
	default:
		printk(KERN_ERR "NET/ROM: nr_write_internal - invalid frame type %d\n", frametype);
		return;
	}

	skb = alloc_skb(NR_NETWORK_LEN + len, GFP_ATOMIC);
	if (!skb)
		return;

	/*
	 *	Space for AX.25 and NET/ROM network header
	 */
	skb_reserve(skb, NR_NETWORK_LEN);

	dptr = skb_put(skb, len);

	switch (frametype & 0x0F) {
	case NR_CONNREQ:
		timeout  = nr->t1 / HZ;
		*dptr++  = nr->my_index;
		*dptr++  = nr->my_id;
		*dptr++  = 0;
		*dptr++  = 0;
		*dptr++  = frametype;
		*dptr++  = nr->window;
		memcpy(dptr, &nr->user_addr, AX25_ADDR_LEN);
		dptr[6] &= ~AX25_CBIT;
		dptr[6] &= ~AX25_EBIT;
		dptr[6] |= AX25_SSSID_SPARE;
		dptr    += AX25_ADDR_LEN;
		memcpy(dptr, &nr->source_addr, AX25_ADDR_LEN);
		dptr[6] &= ~AX25_CBIT;
		dptr[6] &= ~AX25_EBIT;
		dptr[6] |= AX25_SSSID_SPARE;
		dptr    += AX25_ADDR_LEN;
		*dptr++  = timeout % 256;
		*dptr++  = timeout / 256;
		break;

	case NR_CONNACK:
		*dptr++ = nr->your_index;
		*dptr++ = nr->your_id;
		*dptr++ = nr->my_index;
		*dptr++ = nr->my_id;
		*dptr++ = frametype;
		*dptr++ = nr->window;
		if (nr->bpqext) *dptr++ = sysctl_netrom_network_ttl_initialiser;
		break;

	case NR_DISCREQ:
	case NR_DISCACK:
		*dptr++ = nr->your_index;
		*dptr++ = nr->your_id;
		*dptr++ = 0;
		*dptr++ = 0;
		*dptr++ = frametype;
		break;

	case NR_INFOACK:
		*dptr++ = nr->your_index;
		*dptr++ = nr->your_id;
		*dptr++ = 0;
		*dptr++ = nr->vr;
		*dptr++ = frametype;
		break;
	}

	nr_transmit_buffer(sk, skb);
}

/*
 * This routine is called to send an error reply.
 */
void __nr_transmit_reply(struct sk_buff *skb, int mine, unsigned char cmdflags)
{
	struct sk_buff *skbn;
	unsigned char *dptr;
	int len;

	len = NR_NETWORK_LEN + NR_TRANSPORT_LEN + 1;

	if ((skbn = alloc_skb(len, GFP_ATOMIC)) == NULL)
		return;

	skb_reserve(skbn, 0);

	dptr = skb_put(skbn, NR_NETWORK_LEN + NR_TRANSPORT_LEN);

	skb_copy_from_linear_data_offset(skb, 7, dptr, AX25_ADDR_LEN);
	dptr[6] &= ~AX25_CBIT;
	dptr[6] &= ~AX25_EBIT;
	dptr[6] |= AX25_SSSID_SPARE;
	dptr += AX25_ADDR_LEN;

	skb_copy_from_linear_data(skb, dptr, AX25_ADDR_LEN);
	dptr[6] &= ~AX25_CBIT;
	dptr[6] |= AX25_EBIT;
	dptr[6] |= AX25_SSSID_SPARE;
	dptr += AX25_ADDR_LEN;

	*dptr++ = sysctl_netrom_network_ttl_initialiser;

	if (mine) {
		*dptr++ = 0;
		*dptr++ = 0;
		*dptr++ = skb->data[15];
		*dptr++ = skb->data[16];
	} else {
		*dptr++ = skb->data[15];
		*dptr++ = skb->data[16];
		*dptr++ = 0;
		*dptr++ = 0;
	}

	*dptr++ = cmdflags;
	*dptr++ = 0;

	if (!nr_route_frame(skbn, NULL))
		kfree_skb(skbn);
}

void nr_disconnect(struct sock *sk, int reason)
{
	nr_stop_t1timer(sk);
	nr_stop_t2timer(sk);
	nr_stop_t4timer(sk);
	nr_stop_idletimer(sk);

	nr_clear_queues(sk);

	nr_sk(sk)->state = NR_STATE_0;

	sk->sk_state     = TCP_CLOSE;
	sk->sk_err       = reason;
	sk->sk_shutdown |= SEND_SHUTDOWN;

	if (!sock_flag(sk, SOCK_DEAD)) {
		sk->sk_state_change(sk);
		sock_set_flag(sk, SOCK_DEAD);
	}
}
