/*
 *  net/dccp/ccids/ccid2.h
 *
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

/** struct ccid2_hc_tx_sock - CCID2 TX half connection
 *
 * @ccid2hctx_ssacks - ACKs recv in slow start
 * @ccid2hctx_acks - ACKS recv in AI phase
 * @ccid2hctx_sent - packets sent in this window
 * @ccid2hctx_lastrtt -time RTT was last measured
 * @ccid2hctx_arsent - packets sent [ack ratio]
 * @ccid2hctx_ackloss - ack was lost in this win
 * @ccid2hctx_rpseq - last consecutive seqno
 * @ccid2hctx_rpdupack - dupacks since rpseq
*/
struct ccid2_hc_tx_sock {
	u32			ccid2hctx_cwnd;
	int			ccid2hctx_ssacks;
	int			ccid2hctx_acks;
	unsigned int		ccid2hctx_ssthresh;
	int			ccid2hctx_pipe;
	int			ccid2hctx_numdupack;
	struct ccid2_seq	*ccid2hctx_seqbuf[CCID2_SEQBUF_MAX];
	int			ccid2hctx_seqbufc;
	struct ccid2_seq	*ccid2hctx_seqh;
	struct ccid2_seq	*ccid2hctx_seqt;
	long			ccid2hctx_rto;
	long			ccid2hctx_srtt;
	long			ccid2hctx_rttvar;
	int			ccid2hctx_sent;
	unsigned long		ccid2hctx_lastrtt;
	struct timer_list	ccid2hctx_rtotimer;
	unsigned long		ccid2hctx_arsent;
	int			ccid2hctx_ackloss;
	u64			ccid2hctx_rpseq;
	int			ccid2hctx_rpdupack;
	int			ccid2hctx_sendwait;
	unsigned long		ccid2hctx_last_cong;
	u64			ccid2hctx_high_ack;
};

struct ccid2_hc_rx_sock {
	int	ccid2hcrx_data;
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
