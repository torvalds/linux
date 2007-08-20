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

#include <linux/ktime.h>
#include <linux/list.h>

extern void dccp_li_hist_purge(struct list_head *list);

extern u32 dccp_li_hist_calc_i_mean(struct list_head *list);

extern void dccp_li_update_li(struct sock *sk,
			      struct list_head *li_hist_list,
			      struct list_head *hist_list,
			      ktime_t last_feedback, u16 s,
			      u32 bytes_recv, u32 previous_x_recv,
			      u64 seq_loss, u8 win_loss);
#endif /* _DCCP_LI_HIST_ */
