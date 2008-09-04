/*
 *  Packet RX/TX history data structures and routines for TFRC-based protocols.
 *
 *  Copyright (c) 2007   The University of Aberdeen, Scotland, UK
 *  Copyright (c) 2005-6 The University of Waikato, Hamilton, New Zealand.
 *
 *  This code has been developed by the University of Waikato WAND
 *  research group. For further information please see http://www.wand.net.nz/
 *  or e-mail Ian McDonald - ian.mcdonald@jandi.co.nz
 *
 *  This code also uses code from Lulea University, rereleased as GPL by its
 *  authors:
 *  Copyright (c) 2003 Nils-Erik Mattsson, Joacim Haggmark, Magnus Erixzon
 *
 *  Changes to meet Linux coding standards, to make it meet latest ccid3 draft
 *  and to make it work as a loadable module in the DCCP stack written by
 *  Arnaldo Carvalho de Melo <acme@conectiva.com.br>.
 *
 *  Copyright (c) 2005 Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _DCCP_PKT_HIST_
#define _DCCP_PKT_HIST_

#include <linux/list.h>
#include <linux/slab.h>
#include "tfrc.h"

/**
 *  tfrc_tx_hist_entry  -  Simple singly-linked TX history list
 *  @next:  next oldest entry (LIFO order)
 *  @seqno: sequence number of this entry
 *  @stamp: send time of packet with sequence number @seqno
 */
struct tfrc_tx_hist_entry {
	struct tfrc_tx_hist_entry *next;
	u64			  seqno;
	ktime_t			  stamp;
};

static inline struct tfrc_tx_hist_entry *
	tfrc_tx_hist_find_entry(struct tfrc_tx_hist_entry *head, u64 seqno)
{
	while (head != NULL && head->seqno != seqno)
		head = head->next;
	return head;
}

extern int  tfrc_tx_hist_add(struct tfrc_tx_hist_entry **headp, u64 seqno);
extern void tfrc_tx_hist_purge(struct tfrc_tx_hist_entry **headp);

/* Subtraction a-b modulo-16, respects circular wrap-around */
#define SUB16(a, b) (((a) + 16 - (b)) & 0xF)

/* Number of packets to wait after a missing packet (RFC 4342, 6.1) */
#define TFRC_NDUPACK 3

/**
 * tfrc_rx_hist_entry - Store information about a single received packet
 * @tfrchrx_seqno:	DCCP packet sequence number
 * @tfrchrx_ccval:	window counter value of packet (RFC 4342, 8.1)
 * @tfrchrx_ndp:	the NDP count (if any) of the packet
 * @tfrchrx_tstamp:	actual receive time of packet
 */
struct tfrc_rx_hist_entry {
	u64		 tfrchrx_seqno:48,
			 tfrchrx_ccval:4,
			 tfrchrx_type:4;
	u64		 tfrchrx_ndp:48;
	ktime_t		 tfrchrx_tstamp;
};

/**
 * tfrc_rx_hist  -  RX history structure for TFRC-based protocols
 *
 * @ring:		Packet history for RTT sampling and loss detection
 * @loss_count:		Number of entries in circular history
 * @loss_start:		Movable index (for loss detection)
 * @rtt_sample_prev:	Used during RTT sampling, points to candidate entry
 * @rtt_estimate:	Receiver RTT estimate
 * @packet_size:	Packet size in bytes (as per RFC 3448, 3.1)
 * @bytes_recvd:	Number of bytes received since @bytes_start
 * @bytes_start:	Start time for counting @bytes_recvd
 */
struct tfrc_rx_hist {
	struct tfrc_rx_hist_entry *ring[TFRC_NDUPACK + 1];
	u8			  loss_count:2,
				  loss_start:2;
	/* Receiver RTT sampling */
#define rtt_sample_prev		  loss_start
	u32			  rtt_estimate;
	/* Receiver sampling of application payload lengths */
	u32			  packet_size,
				  bytes_recvd;
	ktime_t			  bytes_start;
};

/**
 * tfrc_rx_hist_index - index to reach n-th entry after loss_start
 */
static inline u8 tfrc_rx_hist_index(const struct tfrc_rx_hist *h, const u8 n)
{
	return (h->loss_start + n) & TFRC_NDUPACK;
}

/**
 * tfrc_rx_hist_last_rcv - entry with highest-received-seqno so far
 */
static inline struct tfrc_rx_hist_entry *
			tfrc_rx_hist_last_rcv(const struct tfrc_rx_hist *h)
{
	return h->ring[tfrc_rx_hist_index(h, h->loss_count)];
}

/**
 * tfrc_rx_hist_entry - return the n-th history entry after loss_start
 */
static inline struct tfrc_rx_hist_entry *
			tfrc_rx_hist_entry(const struct tfrc_rx_hist *h, const u8 n)
{
	return h->ring[tfrc_rx_hist_index(h, n)];
}

/**
 * tfrc_rx_hist_loss_prev - entry with highest-received-seqno before loss was detected
 */
static inline struct tfrc_rx_hist_entry *
			tfrc_rx_hist_loss_prev(const struct tfrc_rx_hist *h)
{
	return h->ring[h->loss_start];
}

/* indicate whether previously a packet was detected missing */
static inline bool tfrc_rx_hist_loss_pending(const struct tfrc_rx_hist *h)
{
	return h->loss_count > 0;
}

/*
 * Accessor functions to retrieve parameters sampled by the RX history
 */
static inline u32 tfrc_rx_hist_packet_size(const struct tfrc_rx_hist *h)
{
	if (h->packet_size == 0) {
		DCCP_WARN("No sample for s, using fallback\n");
		return TCP_MIN_RCVMSS;
	}
	return h->packet_size;

}
static inline u32 tfrc_rx_hist_rtt(const struct tfrc_rx_hist *h)
{
	if (h->rtt_estimate == 0) {
		DCCP_WARN("No RTT estimate available, using fallback RTT\n");
		return  DCCP_FALLBACK_RTT;
	}
	return h->rtt_estimate;
}

static inline void tfrc_rx_hist_restart_byte_counter(struct tfrc_rx_hist *h)
{
	h->bytes_recvd = 0;
	h->bytes_start = ktime_get_real();
}

extern u32  tfrc_rx_hist_x_recv(struct tfrc_rx_hist *h, const u32 last_x_recv);


extern void tfrc_rx_hist_add_packet(struct tfrc_rx_hist *h,
				    const struct sk_buff *skb, const u64 ndp);

extern int tfrc_rx_hist_duplicate(struct tfrc_rx_hist *h, struct sk_buff *skb);

struct tfrc_loss_hist;
extern int  tfrc_rx_handle_loss(struct tfrc_rx_hist *h,
				struct tfrc_loss_hist *lh,
				struct sk_buff *skb, const u64 ndp,
				u32 (*first_li)(struct sock *sk),
				struct sock *sk);
extern void tfrc_rx_hist_sample_rtt(struct tfrc_rx_hist *h,
				    const struct sk_buff *skb);
extern int  tfrc_rx_hist_init(struct tfrc_rx_hist *h, struct sock *sk);
extern void tfrc_rx_hist_purge(struct tfrc_rx_hist *h);

#endif /* _DCCP_PKT_HIST_ */
