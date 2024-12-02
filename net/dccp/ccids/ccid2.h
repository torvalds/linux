/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Copyright (c) 2005 Andrea Bittau <a.bittau@cs.ucl.ac.uk>
 */
#ifndef _DCCP_CCID2_H_
#define _DCCP_CCID2_H_

#include <linux/timer.h>
#include <linux/types.h>
#include "../ccid.h"
#include "../dccp.h"

/*
 * CCID-2 timestamping faces the same issues as TCP timestamping.
 * Hence we reuse/share as much of the code as possible.
 */
#define ccid2_jiffies32	((u32)jiffies)

/* NUMDUPACK parameter from RFC 4341, p. 6 */
#define NUMDUPACK	3

struct ccid2_seq {
	u64			ccid2s_seq;
	u32			ccid2s_sent;
	int			ccid2s_acked;
	struct ccid2_seq	*ccid2s_prev;
	struct ccid2_seq	*ccid2s_next;
};

#define CCID2_SEQBUF_LEN 1024
#define CCID2_SEQBUF_MAX 128

/*
 * Multiple of congestion window to keep the sequence window at
 * (RFC 4340 7.5.2)
 */
#define CCID2_WIN_CHANGE_FACTOR 5

/**
 * struct ccid2_hc_tx_sock - CCID2 TX half connection
 * @tx_{cwnd,ssthresh,pipe}: as per RFC 4341, section 5
 * @tx_packets_acked:	     Ack counter for deriving cwnd growth (RFC 3465)
 * @tx_srtt:		     smoothed RTT estimate, scaled by 2^3
 * @tx_mdev:		     smoothed RTT variation, scaled by 2^2
 * @tx_mdev_max:	     maximum of @mdev during one flight
 * @tx_rttvar:		     moving average/maximum of @mdev_max
 * @tx_rto:		     RTO value deriving from SRTT and RTTVAR (RFC 2988)
 * @tx_rtt_seq:		     to decay RTTVAR at most once per flight
 * @tx_cwnd_used:	     actually used cwnd, W_used of RFC 2861
 * @tx_expected_wnd:	     moving average of @tx_cwnd_used
 * @tx_cwnd_stamp:	     to track idle periods in CWV
 * @tx_lsndtime:	     last time (in jiffies) a data packet was sent
 * @tx_rpseq:		     last consecutive seqno
 * @tx_rpdupack:	     dupacks since rpseq
 * @tx_av_chunks:	     list of Ack Vectors received on current skb
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

	/* RTT measurement: variables/principles are the same as in TCP */
	u32			tx_srtt,
				tx_mdev,
				tx_mdev_max,
				tx_rttvar,
				tx_rto;
	u64			tx_rtt_seq:48;
	struct timer_list	tx_rtotimer;
	struct sock		*sk;

	/* Congestion Window validation (optional, RFC 2861) */
	u32			tx_cwnd_used,
				tx_expected_wnd,
				tx_cwnd_stamp,
				tx_lsndtime;

	u64			tx_rpseq;
	int			tx_rpdupack;
	u32			tx_last_cong;
	u64			tx_high_ack;
	struct list_head	tx_av_chunks;
};

static inline bool ccid2_cwnd_network_limited(struct ccid2_hc_tx_sock *hc)
{
	return hc->tx_pipe >= hc->tx_cwnd;
}

/*
 * Convert RFC 3390 larger initial window into an equivalent number of packets.
 * This is based on the numbers specified in RFC 5681, 3.1.
 */
static inline u32 rfc3390_bytes_to_packets(const u32 smss)
{
	return smss <= 1095 ? 4 : (smss > 2190 ? 2 : 3);
}

/**
 * struct ccid2_hc_rx_sock  -  Receiving end of CCID-2 half-connection
 * @rx_num_data_pkts: number of data packets received since last feedback
 */
struct ccid2_hc_rx_sock {
	u32	rx_num_data_pkts;
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
