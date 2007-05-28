#ifndef _DCCP_LI_HIST_
#define _DCCP_LI_HIST_
/*
 *  net/dccp/ccids/lib/loss_interval.h
 *
 *  Copyright (c) 2005-7 The University of Waikato, Hamilton, New Zealand.
 *  Copyright (c) 2005-7 Ian McDonald <ian.mcdonald@jandi.co.nz>
 *  Copyright (c) 2005 Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 */

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/time.h>

#define DCCP_LI_HIST_IVAL_F_LENGTH  8

struct dccp_li_hist {
	struct kmem_cache *dccplih_slab;
};

extern struct dccp_li_hist *dccp_li_hist_new(const char *name);
extern void dccp_li_hist_delete(struct dccp_li_hist *hist);

struct dccp_li_hist_entry {
	struct list_head dccplih_node;
	u64		 dccplih_seqno:48,
			 dccplih_win_count:4;
	u32		 dccplih_interval;
};

extern void dccp_li_hist_purge(struct dccp_li_hist *hist,
			       struct list_head *list);

extern u32 dccp_li_hist_calc_i_mean(struct list_head *list);

extern void dccp_li_update_li(struct sock *sk, struct dccp_li_hist *li_hist,
			      struct list_head *li_hist_list,
			      struct list_head *hist_list,
			      struct timeval *last_feedback, u16 s,
			      u32 bytes_recv, u32 previous_x_recv,
			      u64 seq_loss, u8 win_loss);
#endif /* _DCCP_LI_HIST_ */
