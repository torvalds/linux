/*
 *  Copyright (c) 2005 Andrea Bittau <a.bittau@cs.ucl.ac.uk>
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
#ifndef _DCCP_CCID2_H_
#define _DCCP_CCID2_H_

#include <linux/dccp.h>
#include <linux/timer.h>
#include <linux/types.h>
#include "../ccid.h"
/* NUMDUPACK parameter from RFC 4341, p. 6 */
#define NUMDUPACK	3

struct sock;

struct ccid2_seq {
	u64			ccid2s_seq;
	unsigned long		ccid2s_sent;
	int			ccid2s_acked;
	struct ccid2_seq	*ccid2s_prev;
	struct ccid2_seq	*ccid2s_next;
};

#define CCID2_SEQBUF_LEN 1024
#define CCID2_SEQBUF_MAX 128

/**
 * struct ccid2_hc_tx_sock - CCID2 TX half connection
 * @tx_{cwnd,ssthresh,pipe}: as per RFC 4341, section 5
 * @tx_packets_acked:	     Ack counter for deriving cwnd growth (RFC 3465)
 * @tx_lastrtt:		     time RTT was last measured
 * @tx_rpseq:		     last consecutive seqno
 * @tx_rpdupack:	     dupacks since rpseq
 */
struct ccid2_hc_tx_sock {
	u32			tx_cwnd;
	u32			tx_ssthresh;
	u32			tx_pipe;
	u32			tx_packets_acked;
	struct ccid2_seq	*tx_seqbuf[CCID2_SEQBUF_MAX];
	int			tx_seqbufc;
	struct ccid2_seq	*tx_seqh;
	struct ccid2_seq	*tx_seqt;
	long			tx_rto;
	long			tx_srtt;
	long			tx_rttvar;
	unsigned long		tx_lastrtt;
	struct timer_list	tx_rtotimer;
	u64			tx_rpseq;
	int			tx_rpdupack;
	unsigned long		tx_last_cong;
	u64			tx_high_ack;
};

struct ccid2_hc_rx_sock {
	int	rx_data;
};

static inline struct ccid2_hc_tx_sock *ccid2_hc_tx_sk(const struct sock *sk)
{
	return ccid_priv(dccp_sk(sk)->dccps_hc_tx_ccid);
}

static inline struct ccid2_hc_rx_sock *ccid2_hc_rx_sk(const struct sock *sk)
{
	return ccid_priv(dccp_sk(sk)->dccps_hc_rx_ccid);
}
#endif /* _DCCP_CCID2_H_ */
