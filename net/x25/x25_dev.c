// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	X.25 Packet Layer release 002
 *
 *	This is ALPHA test software. This code may break your machine, randomly fail to work with new
 *	releases, misbehave and/or generally screw up. It might even work.
 *
 *	This code REQUIRES 2.1.15 or higher
 *
 *	History
 *	X.25 001	Jonathan Naylor	Started coding.
 *      2000-09-04	Henner Eisen	Prevent freeing a dangling skb.
 */

#define pr_fmt(fmt) "X25: " fmt

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <net/sock.h>
#include <linux/if_arp.h>
#include <net/x25.h>
#include <net/x25device.h>

static int x25_receive_data(struct sk_buff *skb, struct x25_neigh *nb)
{
	struct sock *sk;
	unsigned short frametype;
	unsigned int lci;

	if (!pskb_may_pull(skb, X25_STD_MIN_LEN))
		return 0;

	frametype = skb->data[2];
	lci = ((skb->data[0] << 8) & 0xF00) + ((skb->data[1] << 0) & 0x0FF);

	/*
	 *	LCI of zero is always for us, and its always a link control
	 *	frame.
	 */
	if (lci == 0) {
		x25_link_control(skb, nb, frametype);
		return 0;
	}

	/*
	 *	Find an existing socket.
	 */
	if ((sk = x25_find_socket(lci, nb)) != NULL) {
		int queued = 1;

		skb_reset_transport_header(skb);
		bh_lock_sock(sk);
		if (!sock_owned_by_user(sk)) {
			queued = x25_process_rx_frame(sk, skb);
		} else {
			queued = !sk_add_backlog(sk, skb, sk->sk_rcvbuf);
		}
		bh_unlock_sock(sk);
		sock_put(sk);
		return queued;
	}

	/*
	 *	Is is a Call Request ? if so process it.
	 */
	if (frametype == X25_CALL_REQUEST)
		return x25_rx_call_request(skb, nb, lci);

	/*
	 * 	Its not a Call Request, nor is it a control frame.
	 *	Can we forward it?
	 */

	if (x25_forward_data(lci, nb, skb)) {
		if (frametype == X25_CLEAR_CONFIRMATION) {
			x25_clear_forward_by_lci(lci);
		}
		kfree_skb(skb);
		return 1;
	}

/*
	x25_transmit_clear_request(nb, lci, 0x0D);
*/

	if (frametype != X25_CLEAR_CONFIRMATION)
		pr_debug("x25_receive_data(): unknown frame type %2x\n",frametype);

	return 0;
}

int x25_lapb_receive_frame(struct sk_buff *skb, struct net_device *dev,
			   struct packet_type *ptype, struct net_device *orig_dev)
{
	struct sk_buff *nskb;
	struct x25_neigh *nb;

	if (!net_eq(dev_net(dev), &init_net))
		goto drop;

	nskb = skb_copy(skb, GFP_ATOMIC);
	if (!nskb)
		goto drop;
	kfree_skb(skb);
	skb = nskb;

	/*
	 * Packet received from unrecognised device, throw it away.
	 */
	nb = x25_get_neigh(dev);
	if (!nb) {
		pr_debug("unknown neighbour - %s\n", dev->name);
		goto drop;
	}

	if (!pskb_may_pull(skb, 1))
		return 0;

	switch (skb->data[0]) {

	case X25_IFACE_DATA:
		skb_pull(skb, 1);
		if (x25_receive_data(skb, nb)) {
			x25_neigh_put(nb);
			goto out;
		}
		break;

	case X25_IFACE_CONNECT:
		x25_link_established(nb);
		break;

	case X25_IFACE_DISCONNECT:
		x25_link_terminated(nb);
		break;
	}
	x25_neigh_put(nb);
drop:
	kfree_skb(skb);
out:
	return 0;
}

void x25_establish_link(struct x25_neigh *nb)
{
	struct sk_buff *skb;
	unsigned char *ptr;

	switch (nb->dev->type) {
	case ARPHRD_X25:
		if ((skb = alloc_skb(1, GFP_ATOMIC)) == NULL) {
			pr_err("x25_dev: out of memory\n");
			return;
		}
		ptr  = skb_put(skb, 1);
		*ptr = X25_IFACE_CONNECT;
		break;

#if IS_ENABLED(CONFIG_LLC)
	case ARPHRD_ETHER:
		return;
#endif
	default:
		return;
	}

	skb->protocol = htons(ETH_P_X25);
	skb->dev      = nb->dev;

	dev_queue_xmit(skb);
}

void x25_terminate_link(struct x25_neigh *nb)
{
	struct sk_buff *skb;
	unsigned char *ptr;

#if IS_ENABLED(CONFIG_LLC)
	if (nb->dev->type == ARPHRD_ETHER)
		return;
#endif
	if (nb->dev->type != ARPHRD_X25)
		return;

	skb = alloc_skb(1, GFP_ATOMIC);
	if (!skb) {
		pr_err("x25_dev: out of memory\n");
		return;
	}

	ptr  = skb_put(skb, 1);
	*ptr = X25_IFACE_DISCONNECT;

	skb->protocol = htons(ETH_P_X25);
	skb->dev      = nb->dev;
	dev_queue_xmit(skb);
}

void x25_send_frame(struct sk_buff *skb, struct x25_neigh *nb)
{
	unsigned char *dptr;

	skb_reset_network_header(skb);

	switch (nb->dev->type) {
	case ARPHRD_X25:
		dptr  = skb_push(skb, 1);
		*dptr = X25_IFACE_DATA;
		break;

#if IS_ENABLED(CONFIG_LLC)
	case ARPHRD_ETHER:
		kfree_skb(skb);
		return;
#endif
	default:
		kfree_skb(skb);
		return;
	}

	skb->protocol = htons(ETH_P_X25);
	skb->dev      = nb->dev;

	dev_queue_xmit(skb);
}
