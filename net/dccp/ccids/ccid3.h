/*
 *  net/dccp/ccids/ccid3.h
 *
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

/* Maximum backoff interval t_mbi (RFC 3448, 4.3) */
#define TFRC_T_MBI		   (64 * USEC_PER_SEC)

/*
 * The t_delta parameter (RFC 3448, 4.6): delays of less than %USEC_PER_MSEC are
 * rounded down to 0, since sk_reset_timer() here uses millisecond granularity.
 * Hence we can use a constant t_delta = %USEC_PER_MSEC when HZ >= 500. A coarse
 * resolution of HZ < 500 means that the error is below one timer tick (t_gran)
 * when using the constant t_delta  =  t_gran / 2  =  %USEC_PER_SEC / (2 * HZ).
 */
#if (HZ >= 500)
# define TFRC_T_DELTA		   USEC_PER_MSEC
#else
# define TFRC_T_DELTA		   (USEC_PER_SEC / (2 * HZ))
#warning Coarse CONFIG_HZ resolution -- higher value recommended for TFRC.
#endif

enum ccid3_options {
	TFRC_OPT_LOSS_EVENT_RATE = 192,
	TFRC_OPT_LOSS_INTERVALS	 = 193,
	TFRC_OPT_RECEIVE_RATE	 = 194,
};

/** struct ccid3_hc_tx_sock - CCID3 sender half-connection socket
 *
 * @x - Current sending rate in 64 * bytes per second
 * @x_recv - Receive rate    in 64 * bytes per second
 * @x_calc - Calculated rate in bytes per second
 * @rtt - Estimate of current round trip time in usecs
 * @r_sqmean - Estimate of long-term RTT (RFC 3448, 4.5)
 * @p - Current loss event rate (0-1) scaled by 1000000
 * @s - Packet size in bytes
 * @t_rto - Nofeedback Timer setting in usecs
 * @t_ipi - Interpacket (send) interval (RFC 3448, 4.6) in usecs
 * @feedback - Whether feedback has been received or not
 * @last_win_count - Last window counter sent
 * @t_last_win_count - Timestamp of earliest packet with
 *                     last_win_count value sent
 * @no_feedback_timer - Handle to no feedback timer
 * @t_ld - Time last doubled during slow start
 * @t_nom - Nominal send time of next packet
 * @hist - Packet history
 */
struct ccid3_hc_tx_sock {
	u64				x;
	u64				x_recv;
	u32				x_calc;
	u32				rtt;
	u16				r_sqmean;
	u32				p;
	u32				t_rto;
	u32				t_ipi;
	u16				s;
	bool				feedback:1;
	u8				last_win_count;
	ktime_t				t_last_win_count;
	struct timer_list		no_feedback_timer;
	ktime_t				t_ld;
	ktime_t				t_nom;
	struct tfrc_tx_hist_entry	*hist;
};

static inline struct ccid3_hc_tx_sock *ccid3_hc_tx_sk(const struct sock *sk)
{
    struct ccid3_hc_tx_sock *hctx = ccid_priv(dccp_sk(sk)->dccps_hc_tx_ccid);
    BUG_ON(hctx == NULL);
    return hctx;
}


enum ccid3_fback_type {
	CCID3_FBACK_NONE = 0,
	CCID3_FBACK_INITIAL,
	CCID3_FBACK_PERIODIC,
	CCID3_FBACK_PARAM_CHANGE
};

/** struct ccid3_hc_rx_sock - CCID3 receiver half-connection socket
 *
 *  @last_counter  -  Tracks window counter (RFC 4342, 8.1)
 *  @feedback  -  The type of the feedback last sent
 *  @x_recv  -  Receiver estimate of send rate (RFC 3448, sec. 4.3)
 *  @tstamp_last_feedback  -  Time at which last feedback was sent
 *  @hist  -  Packet history (loss detection + RTT sampling)
 *  @li_hist  -  Loss Interval database
 *  @p_inverse  -  Inverse of Loss Event Rate (RFC 4342, sec. 8.5)
 */
struct ccid3_hc_rx_sock {
	u8				last_counter:4;
	enum ccid3_fback_type		feedback:4;
	u32				x_recv;
	ktime_t				tstamp_last_feedback;
	struct tfrc_rx_hist		hist;
	struct tfrc_loss_hist		li_hist;
#define p_inverse			li_hist.i_mean
};

static inline struct ccid3_hc_rx_sock *ccid3_hc_rx_sk(const struct sock *sk)
{
    struct ccid3_hc_rx_sock *hcrx = ccid_priv(dccp_sk(sk)->dccps_hc_rx_ccid);
    BUG_ON(hcrx == NULL);
    return hcrx;
}

#endif /* _DCCP_CCID3_H_ */
