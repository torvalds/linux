// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 * Copyright (C) Alan Cox GW4PTS (alan@lxorguk.ukuu.org.uk)
 * Copyright (C) Jonathan Naylor G4KLX (g4klx@g4klx.demon.co.uk)
 * Copyright (C) Joerg Reuter DL1BKE (jreuter@yaina.de)
 * Copyright (C) Hans-Joachim Hetscher DD8NE (dd8ne@bnv-bamberg.de)
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
 *	Given a fragment, queue it on the fragment queue and if the fragment
 *	is complete, send it back to ax25_rx_iframe.
 */
static int ax25_rx_fragment(ax25_cb *ax25, struct sk_buff *skb)
{
	struct sk_buff *skbn, *skbo;

	if (ax25->fragno != 0) {
		if (!(*skb->data & AX25_SEG_FIRST)) {
			if ((ax25->fragno - 1) == (*skb->data & AX25_SEG_REM)) {
				/* Enqueue fragment */
				ax25->fragno = *skb->data & AX25_SEG_REM;
				skb_pull(skb, 1);	/* skip fragno */
				ax25->fraglen += skb->len;
				skb_queue_tail(&ax25->frag_queue, skb);

				/* Last fragment received ? */
				if (ax25->fragno == 0) {
					skbn = alloc_skb(AX25_MAX_HEADER_LEN +
							 ax25->fraglen,
							 GFP_ATOMIC);
					if (!skbn) {
						skb_queue_purge(&ax25->frag_queue);
						return 1;
					}

					skb_reserve(skbn, AX25_MAX_HEADER_LEN);

					skbn->dev   = ax25->ax25_dev->dev;
					skb_reset_network_header(skbn);
					skb_reset_transport_header(skbn);

					/* Copy data from the fragments */
					while ((skbo = skb_dequeue(&ax25->frag_queue)) != NULL) {
						skb_copy_from_linear_data(skbo,
							  skb_put(skbn, skbo->len),
									  skbo->len);
						kfree_skb(skbo);
					}

					ax25->fraglen = 0;

					if (ax25_rx_iframe(ax25, skbn) == 0)
						kfree_skb(skbn);
				}

				return 1;
			}
		}
	} else {
		/* First fragment received */
		if (*skb->data & AX25_SEG_FIRST) {
			skb_queue_purge(&ax25->frag_queue);
			ax25->fragno = *skb->data & AX25_SEG_REM;
			skb_pull(skb, 1);		/* skip fragno */
			ax25->fraglen = skb->len;
			skb_queue_tail(&ax25->frag_queue, skb);
			return 1;
		}
	}

	return 0;
}

/*
 *	This is where all valid I frames are sent to, to be dispatched to
 *	whichever protocol requires them.
 */
int ax25_rx_iframe(ax25_cb *ax25, struct sk_buff *skb)
{
	int (*func)(struct sk_buff *, ax25_cb *);
	unsigned char pid;
	int queued = 0;

	if (skb == NULL) return 0;

	ax25_start_idletimer(ax25);

	pid = *skb->data;

	if (pid == AX25_P_IP) {
		/* working around a TCP bug to keep additional listeners
		 * happy. TCP re-uses the buffer and destroys the original
		 * content.
		 */
		struct sk_buff *skbn = skb_copy(skb, GFP_ATOMIC);
		if (skbn != NULL) {
			kfree_skb(skb);
			skb = skbn;
		}

		skb_pull(skb, 1);	/* Remove PID */
		skb->mac_header = skb->network_header;
		skb_reset_network_header(skb);
		skb->dev      = ax25->ax25_dev->dev;
		skb->pkt_type = PACKET_HOST;
		skb->protocol = htons(ETH_P_IP);
		netif_rx(skb);
		return 1;
	}
	if (pid == AX25_P_SEGMENT) {
		skb_pull(skb, 1);	/* Remove PID */
		return ax25_rx_fragment(ax25, skb);
	}

	if ((func = ax25_protocol_function(pid)) != NULL) {
		skb_pull(skb, 1);	/* Remove PID */
		return (*func)(skb, ax25);
	}

	if (ax25->sk != NULL && ax25->ax25_dev->values[AX25_VALUES_CONMODE] == 2) {
		if ((!ax25->pidincl && ax25->sk->sk_protocol == pid) ||
		    ax25->pidincl) {
			if (sock_queue_rcv_skb(ax25->sk, skb) == 0)
				queued = 1;
			else
				ax25->condition |= AX25_COND_OWN_RX_BUSY;
		}
	}

	return queued;
}

/*
 *	Higher level upcall for a LAPB frame
 */
static int ax25_process_rx_frame(ax25_cb *ax25, struct sk_buff *skb, int type, int dama)
{
	int queued = 0;

	if (ax25->state == AX25_STATE_0)
		return 0;

	switch (ax25->ax25_dev->values[AX25_VALUES_PROTOCOL]) {
	case AX25_PROTO_STD_SIMPLEX:
	case AX25_PROTO_STD_DUPLEX:
		queued = ax25_std_frame_in(ax25, skb, type);
		break;

#ifdef CONFIG_AX25_DAMA_SLAVE
	case AX25_PROTO_DAMA_SLAVE:
		if (dama || ax25->ax25_dev->dama.slave)
			queued = ax25_ds_frame_in(ax25, skb, type);
		else
			queued = ax25_std_frame_in(ax25, skb, type);
		break;
#endif
	}

	return queued;
}

static int ax25_rcv(struct sk_buff *skb, struct net_device *dev,
		    const ax25_address *dev_addr, struct packet_type *ptype)
{
	ax25_address src, dest, *next_digi = NULL;
	int type = 0, mine = 0, dama;
	struct sock *make, *sk;
	ax25_digi dp, reverse_dp;
	ax25_cb *ax25;
	ax25_dev *ax25_dev;

	/*
	 *	Process the AX.25/LAPB frame.
	 */

	skb_reset_transport_header(skb);

	if ((ax25_dev = ax25_dev_ax25dev(dev)) == NULL)
		goto free;

	/*
	 *	Parse the address header.
	 */

	if (ax25_addr_parse(skb->data, skb->len, &src, &dest, &dp, &type, &dama) == NULL)
		goto free;

	/*
	 *	Ours perhaps ?
	 */
	if (dp.lastrepeat + 1 < dp.ndigi)		/* Not yet digipeated completely */
		next_digi = &dp.calls[dp.lastrepeat + 1];

	/*
	 *	Pull of the AX.25 headers leaving the CTRL/PID bytes
	 */
	skb_pull(skb, ax25_addr_size(&dp));

	/* For our port addresses ? */
	if (ax25cmp(&dest, dev_addr) == 0 && dp.lastrepeat + 1 == dp.ndigi)
		mine = 1;

	/* Also match on any registered callsign from L3/4 */
	if (!mine && ax25_listen_mine(&dest, dev) && dp.lastrepeat + 1 == dp.ndigi)
		mine = 1;

	/* UI frame - bypass LAPB processing */
	if ((*skb->data & ~0x10) == AX25_UI && dp.lastrepeat + 1 == dp.ndigi) {
		skb_set_transport_header(skb, 2); /* skip control and pid */

		ax25_send_to_raw(&dest, skb, skb->data[1]);

		if (!mine && ax25cmp(&dest, (ax25_address *)dev->broadcast) != 0)
			goto free;

		/* Now we are pointing at the pid byte */
		switch (skb->data[1]) {
		case AX25_P_IP:
			skb_pull(skb,2);		/* drop PID/CTRL */
			skb_reset_transport_header(skb);
			skb_reset_network_header(skb);
			skb->dev      = dev;
			skb->pkt_type = PACKET_HOST;
			skb->protocol = htons(ETH_P_IP);
			netif_rx(skb);
			break;

		case AX25_P_ARP:
			skb_pull(skb,2);
			skb_reset_transport_header(skb);
			skb_reset_network_header(skb);
			skb->dev      = dev;
			skb->pkt_type = PACKET_HOST;
			skb->protocol = htons(ETH_P_ARP);
			netif_rx(skb);
			break;
		case AX25_P_TEXT:
			/* Now find a suitable dgram socket */
			sk = ax25_get_socket(&dest, &src, SOCK_DGRAM);
			if (sk != NULL) {
				bh_lock_sock(sk);
				if (atomic_read(&sk->sk_rmem_alloc) >=
				    sk->sk_rcvbuf) {
					kfree_skb(skb);
				} else {
					/*
					 *	Remove the control and PID.
					 */
					skb_pull(skb, 2);
					if (sock_queue_rcv_skb(sk, skb) != 0)
						kfree_skb(skb);
				}
				bh_unlock_sock(sk);
				sock_put(sk);
			} else {
				kfree_skb(skb);
			}
			break;

		default:
			kfree_skb(skb);	/* Will scan SOCK_AX25 RAW sockets */
			break;
		}

		return 0;
	}

	/*
	 *	Is connected mode supported on this device ?
	 *	If not, should we DM the incoming frame (except DMs) or
	 *	silently ignore them. For now we stay quiet.
	 */
	if (ax25_dev->values[AX25_VALUES_CONMODE] == 0)
		goto free;

	/* LAPB */

	/* AX.25 state 1-4 */

	ax25_digi_invert(&dp, &reverse_dp);

	if ((ax25 = ax25_find_cb(&dest, &src, &reverse_dp, dev)) != NULL) {
		/*
		 *	Process the frame. If it is queued up internally it
		 *	returns one otherwise we free it immediately. This
		 *	routine itself wakes the user context layers so we do
		 *	no further work
		 */
		if (ax25_process_rx_frame(ax25, skb, type, dama) == 0)
			kfree_skb(skb);

		ax25_cb_put(ax25);
		return 0;
	}

	/* AX.25 state 0 (disconnected) */

	/* a) received not a SABM(E) */

	if ((*skb->data & ~AX25_PF) != AX25_SABM &&
	    (*skb->data & ~AX25_PF) != AX25_SABME) {
		/*
		 *	Never reply to a DM. Also ignore any connects for
		 *	addresses that are not our interfaces and not a socket.
		 */
		if ((*skb->data & ~AX25_PF) != AX25_DM && mine)
			ax25_return_dm(dev, &src, &dest, &dp);

		goto free;
	}

	/* b) received SABM(E) */

	if (dp.lastrepeat + 1 == dp.ndigi)
		sk = ax25_find_listener(&dest, 0, dev, SOCK_SEQPACKET);
	else
		sk = ax25_find_listener(next_digi, 1, dev, SOCK_SEQPACKET);

	if (sk != NULL) {
		bh_lock_sock(sk);
		if (sk_acceptq_is_full(sk) ||
		    (make = ax25_make_new(sk, ax25_dev)) == NULL) {
			if (mine)
				ax25_return_dm(dev, &src, &dest, &dp);
			kfree_skb(skb);
			bh_unlock_sock(sk);
			sock_put(sk);

			return 0;
		}

		ax25 = sk_to_ax25(make);
		skb_set_owner_r(skb, make);
		skb_queue_head(&sk->sk_receive_queue, skb);

		make->sk_state = TCP_ESTABLISHED;

		sk_acceptq_added(sk);
		bh_unlock_sock(sk);
	} else {
		if (!mine)
			goto free;

		if ((ax25 = ax25_create_cb()) == NULL) {
			ax25_return_dm(dev, &src, &dest, &dp);
			goto free;
		}

		ax25_fillin_cb(ax25, ax25_dev);
	}

	ax25->source_addr = dest;
	ax25->dest_addr   = src;

	/*
	 *	Sort out any digipeated paths.
	 */
	if (dp.ndigi && !ax25->digipeat &&
	    (ax25->digipeat = kmalloc(sizeof(ax25_digi), GFP_ATOMIC)) == NULL) {
		kfree_skb(skb);
		ax25_destroy_socket(ax25);
		if (sk)
			sock_put(sk);
		return 0;
	}

	if (dp.ndigi == 0) {
		kfree(ax25->digipeat);
		ax25->digipeat = NULL;
	} else {
		/* Reverse the source SABM's path */
		memcpy(ax25->digipeat, &reverse_dp, sizeof(ax25_digi));
	}

	if ((*skb->data & ~AX25_PF) == AX25_SABME) {
		ax25->modulus = AX25_EMODULUS;
		ax25->window  = ax25_dev->values[AX25_VALUES_EWINDOW];
	} else {
		ax25->modulus = AX25_MODULUS;
		ax25->window  = ax25_dev->values[AX25_VALUES_WINDOW];
	}

	ax25_send_control(ax25, AX25_UA, AX25_POLLON, AX25_RESPONSE);

#ifdef CONFIG_AX25_DAMA_SLAVE
	if (dama && ax25->ax25_dev->values[AX25_VALUES_PROTOCOL] == AX25_PROTO_DAMA_SLAVE)
		ax25_dama_on(ax25);
#endif

	ax25->state = AX25_STATE_3;

	ax25_cb_add(ax25);

	ax25_start_heartbeat(ax25);
	ax25_start_t3timer(ax25);
	ax25_start_idletimer(ax25);

	if (sk) {
		if (!sock_flag(sk, SOCK_DEAD))
			sk->sk_data_ready(sk);
		sock_put(sk);
	} else {
free:
		kfree_skb(skb);
	}
	return 0;
}

/*
 *	Receive an AX.25 frame via a SLIP interface.
 */
int ax25_kiss_rcv(struct sk_buff *skb, struct net_device *dev,
		  struct packet_type *ptype, struct net_device *orig_dev)
{
	skb = skb_share_check(skb, GFP_ATOMIC);
	if (!skb)
		return NET_RX_DROP;

	skb_orphan(skb);

	if (!net_eq(dev_net(dev), &init_net)) {
		kfree_skb(skb);
		return 0;
	}

	if ((*skb->data & 0x0F) != 0) {
		kfree_skb(skb);	/* Not a KISS data frame */
		return 0;
	}

	skb_pull(skb, AX25_KISS_HEADER_LEN);	/* Remove the KISS byte */

	return ax25_rcv(skb, dev, (const ax25_address *)dev->dev_addr, ptype);
}
