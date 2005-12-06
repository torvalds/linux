#ifndef _DCCP_LI_HIST_
#define _DCCP_LI_HIST_
/*
 *  net/dccp/ccids/lib/loss_interval.h
 *
 *  Copyright (c) 2005 The University of Waikato, Hamilton, New Zealand.
 *  Copyright (c) 2005 Ian McDonald <iam4@cs.waikato.ac.nz>
 *  Copyright (c) 2005 Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 */

#include <linux/config.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/time.h>

#define DCCP_LI_HIST_IVAL_F_LENGTH  8

struct dccp_li_hist {
	kmem_cache_t *dccplih_slab;
};

extern struct dccp_li_hist *dccp_li_hist_new(const char *name);
extern void dccp_li_hist_delete(struct dccp_li_hist *hist);

struct dccp_li_hist_entry {
	struct list_head dccplih_node;
	u64		 dccplih_seqno:48,
			 dccplih_win_count:4;
	u32		 dccplih_interval;
};

static inline struct dccp_li_hist_entry *
		dccp_li_hist_entry_new(struct dccp_li_hist *hist,
				       const gfp_t prio)
{
	return kmem_cache_alloc(hist->dccplih_slab, prio);
}

static inline void dccp_li_hist_entry_delete(struct dccp_li_hist *hist,
					     struct dccp_li_hist_entry *entry)
{
	if (entry != NULL)
		kmem_cache_free(hist->dccplih_slab, entry);
}

extern void dccp_li_hist_purge(struct dccp_li_hist *hist,
			       struct list_head *list);

extern u32 dccp_li_hist_calc_i_mean(struct list_head *list);

extern struct dccp_li_hist_entry *
			dccp_li_hist_interval_new(struct dccp_li_hist *hist,
						  struct list_head *list,
						  const u64 seq_loss,
						  const u8 win_loss);
#endif /* _DCCP_LI_HIST_ */
