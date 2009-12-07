/*
 *  Copyright (c) 2005-7 The University of Waikato, Hamilton, New Zealand.
 *  Copyright (c) 2007   The University of Aberdeen, Scotland, UK
 *
 *  An implementation of the DCCP protocol
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
#ifndef _DCCP_CCID3_H_
#define _DCCP_CCID3_H_

#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/tfrc.h>
#include "lib/tfrc.h"
#include "../ccid.h"

/* Two seconds as per RFC 3448 4.2 */
#define TFRC_INITIAL_TIMEOUT	   (2 * USEC_PER_SEC)

/* In usecs - half the scheduling granularity as per RFC3448 4.6 */
#define TFRC_OPSYS_HALF_TIME_GRAN  (USEC_PER_SEC / (2 * HZ))

/* Parameter t_mbi from [RFC 3448, 4.3]: backoff interval in seconds */
#define TFRC_T_MBI		   64

enum ccid3_options {
	TFRC_OPT_LOSS_EVENT_RATE = 192,
	TFRC_OPT_LOSS_INTERVALS	 = 193,
	TFRC_OPT_RECEIVE_RATE	 = 194,
};

struct ccid3_options_received {
	u64 ccid3or_seqno:48,
	    ccid3or_loss_intervals_idx:16;
	u16 ccid3or_loss_intervals_len;
	u32 ccid3or_loss_event_rate;
	u32 ccid3or_receive_rate;
};

/* TFRC sender states */
enum ccid3_hc_tx_states {
	TFRC_SSTATE_NO_SENT = 1,
	TFRC_SSTATE_NO_FBACK,
	TFRC_SSTATE_FBACK,
	TFRC_SSTATE_TERM,
};

/**
 * struct ccid3_hc_tx_sock - CCID3 sender half-connection socket
 * @ccid3hctx_x - Current sending rate in 64 * bytes per second
 * @ccid3hctx_x_recv - Receive rate    in 64 * bytes per second
 * @ccid3hctx_x_calc - Calculated rate in bytes per second
 * @ccid3hctx_rtt - Estimate of current round trip time in usecs
 * @ccid3hctx_p - Current loss event rate (0-1) scaled by 1000000
 * @ccid3hctx_s - Packet size in bytes
 * @ccid3hctx_t_rto - Nofeedback Timer setting in usecs
 * @ccid3hctx_t_ipi - Interpacket (send) interval (RFC 3448, 4.6) in usecs
 * @ccid3hctx_state - Sender state, one of %ccid3_hc_tx_states
 * @ccid3hctx_last_win_count - Last window counter sent
 * @ccid3hctx_t_last_win_count - Timestamp of earliest packet
 *				 with last_win_count value sent
 * @ccid3hctx_no_feedback_timer - Handle to no feedback timer
 * @ccid3hctx_t_ld - Time last doubled during slow start
 * @ccid3hctx_t_nom - Nominal send time of next packet
 * @ccid3hctx_delta - Send timer delta (RFC 3448, 4.6) in usecs
 * @ccid3hctx_hist - Packet history
 * @ccid3hctx_options_received - Parsed set of retrieved options
 */
struct ccid3_hc_tx_sock {
	struct tfrc_tx_info		ccid3hctx_tfrc;
#define ccid3hctx_x			ccid3hctx_tfrc.tfrctx_x
#define ccid3hctx_x_recv		ccid3hctx_tfrc.tfrctx_x_recv
#define ccid3hctx_x_calc		ccid3hctx_tfrc.tfrctx_x_calc
#define ccid3hctx_rtt			ccid3hctx_tfrc.tfrctx_rtt
#define ccid3hctx_p			ccid3hctx_tfrc.tfrctx_p
#define ccid3hctx_t_rto			ccid3hctx_tfrc.tfrctx_rto
#define ccid3hctx_t_ipi			ccid3hctx_tfrc.tfrctx_ipi
	u16				ccid3hctx_s;
	enum ccid3_hc_tx_states		ccid3hctx_state:8;
	u8				ccid3hctx_last_win_count;
	ktime_t				ccid3hctx_t_last_win_count;
	struct timer_list		ccid3hctx_no_feedback_timer;
	ktime_t				ccid3hctx_t_ld;
	ktime_t				ccid3hctx_t_nom;
	u32				ccid3hctx_delta;
	struct tfrc_tx_hist_entry	*ccid3hctx_hist;
	struct ccid3_options_received	ccid3hctx_options_received;
};

static inline struct ccid3_hc_tx_sock *ccid3_hc_tx_sk(const struct sock *sk)
{
	struct ccid3_hc_tx_sock *hctx = ccid_priv(dccp_sk(sk)->dccps_hc_tx_ccid);
	BUG_ON(hctx == NULL);
	return hctx;
}

/* TFRC receiver states */
enum ccid3_hc_rx_states {
	TFRC_RSTATE_NO_DATA = 1,
	TFRC_RSTATE_DATA,
	TFRC_RSTATE_TERM    = 127,
};

/**
 * struct ccid3_hc_rx_sock - CCID3 receiver half-connection socket
 * @ccid3hcrx_x_recv  -  Receiver estimate of send rate (RFC 3448 4.3)
 * @ccid3hcrx_rtt  -  Receiver estimate of rtt (non-standard)
 * @ccid3hcrx_p  -  Current loss event rate (RFC 3448 5.4)
 * @ccid3hcrx_last_counter  -  Tracks window counter (RFC 4342, 8.1)
 * @ccid3hcrx_state  -  Receiver state, one of %ccid3_hc_rx_states
 * @ccid3hcrx_bytes_recv  -  Total sum of DCCP payload bytes
 * @ccid3hcrx_x_recv  -  Receiver estimate of send rate (RFC 3448, sec. 4.3)
 * @ccid3hcrx_rtt  -  Receiver estimate of RTT
 * @ccid3hcrx_tstamp_last_feedback  -  Time at which last feedback was sent
 * @ccid3hcrx_tstamp_last_ack  -  Time at which last feedback was sent
 * @ccid3hcrx_hist  -  Packet history (loss detection + RTT sampling)
 * @ccid3hcrx_li_hist  -  Loss Interval database
 * @ccid3hcrx_s  -  Received packet size in bytes
 * @ccid3hcrx_pinv  -  Inverse of Loss Event Rate (RFC 4342, sec. 8.5)
 */
struct ccid3_hc_rx_sock {
	u8				ccid3hcrx_last_counter:4;
	enum ccid3_hc_rx_states		ccid3hcrx_state:8;
	u32				ccid3hcrx_bytes_recv;
	u32				ccid3hcrx_x_recv;
	u32				ccid3hcrx_rtt;
	ktime_t				ccid3hcrx_tstamp_last_feedback;
	struct tfrc_rx_hist		ccid3hcrx_hist;
	struct tfrc_loss_hist		ccid3hcrx_li_hist;
	u16				ccid3hcrx_s;
#define ccid3hcrx_pinv			ccid3hcrx_li_hist.i_mean
};

static inline struct ccid3_hc_rx_sock *ccid3_hc_rx_sk(const struct sock *sk)
{
	struct ccid3_hc_rx_sock *hcrx = ccid_priv(dccp_sk(sk)->dccps_hc_rx_ccid);
	BUG_ON(hcrx == NULL);
	return hcrx;
}

#endif /* _DCCP_CCID3_H_ */
