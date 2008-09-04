#ifndef _DCCP_LI_HIST_
#define _DCCP_LI_HIST_
/*
 *  net/dccp/ccids/lib/loss_interval.h
 *
 *  Copyright (c) 2007   The University of Aberdeen, Scotland, UK
 *  Copyright (c) 2005-7 The University of Waikato, Hamilton, New Zealand.
 *  Copyright (c) 2005-7 Ian McDonald <ian.mcdonald@jandi.co.nz>
 *  Copyright (c) 2005 Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 */
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/slab.h>

/*
 * Number of loss intervals (RFC 4342, 8.6.1). The history size is one more than
 * NINTERVAL, since the `open' interval I_0 is always stored as the first entry.
 */
#define NINTERVAL	8
#define LIH_SIZE	(NINTERVAL + 1)

/**
 *  tfrc_loss_interval  -  Loss history record for TFRC-based protocols
 *  @li_seqno:		Highest received seqno before the start of loss
 *  @li_ccval:		The CCVal belonging to @li_seqno
 *  @li_is_closed:	Whether @li_seqno is older than 1 RTT
 *  @li_length:		Loss interval sequence length
 */
struct tfrc_loss_interval {
	u64		 li_seqno:48,
			 li_ccval:4,
			 li_is_closed:1;
	u32		 li_length;
};

/**
 *  tfrc_loss_hist  -  Loss record database
 *  @ring:	Circular queue managed in LIFO manner
 *  @counter:	Current count of entries (can be more than %LIH_SIZE)
 *  @i_mean:	Current Average Loss Interval [RFC 3448, 5.4]
 */
struct tfrc_loss_hist {
	struct tfrc_loss_interval	*ring[LIH_SIZE];
	u8				counter;
	u32				i_mean;
};

static inline void tfrc_lh_init(struct tfrc_loss_hist *lh)
{
	memset(lh, 0, sizeof(struct tfrc_loss_hist));
}

static inline u8 tfrc_lh_is_initialised(struct tfrc_loss_hist *lh)
{
	return lh->counter > 0;
}

static inline u8 tfrc_lh_length(struct tfrc_loss_hist *lh)
{
	return min(lh->counter, (u8)LIH_SIZE);
}

struct tfrc_rx_hist;

extern bool tfrc_lh_interval_add(struct tfrc_loss_hist *, struct tfrc_rx_hist *,
				 u32 (*first_li)(struct sock *), struct sock *);
extern void tfrc_lh_update_i_mean(struct tfrc_loss_hist *lh, struct sk_buff *);
extern void tfrc_lh_cleanup(struct tfrc_loss_hist *lh);

#endif /* _DCCP_LI_HIST_ */
