/*
 *  net/dccp/ccids/lib/loss_interval.c
 *
 *  Copyright (c) 2005 The University of Waikato, Hamilton, New Zealand.
 *  Copyright (c) 2005-6 Ian McDonald <ian.mcdonald@jandi.co.nz>
 *  Copyright (c) 2005 Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include <linux/module.h>
#include <net/sock.h>
#include "../../dccp.h"
#include "loss_interval.h"

struct dccp_li_hist *dccp_li_hist_new(const char *name)
{
	struct dccp_li_hist *hist = kmalloc(sizeof(*hist), GFP_ATOMIC);
	static const char dccp_li_hist_mask[] = "li_hist_%s";
	char *slab_name;

	if (hist == NULL)
		goto out;

	slab_name = kmalloc(strlen(name) + sizeof(dccp_li_hist_mask) - 1,
			    GFP_ATOMIC);
	if (slab_name == NULL)
		goto out_free_hist;

	sprintf(slab_name, dccp_li_hist_mask, name);
	hist->dccplih_slab = kmem_cache_create(slab_name,
					     sizeof(struct dccp_li_hist_entry),
					       0, SLAB_HWCACHE_ALIGN,
					       NULL, NULL);
	if (hist->dccplih_slab == NULL)
		goto out_free_slab_name;
out:
	return hist;
out_free_slab_name:
	kfree(slab_name);
out_free_hist:
	kfree(hist);
	hist = NULL;
	goto out;
}

EXPORT_SYMBOL_GPL(dccp_li_hist_new);

void dccp_li_hist_delete(struct dccp_li_hist *hist)
{
	const char* name = kmem_cache_name(hist->dccplih_slab);

	kmem_cache_destroy(hist->dccplih_slab);
	kfree(name);
	kfree(hist);
}

EXPORT_SYMBOL_GPL(dccp_li_hist_delete);

void dccp_li_hist_purge(struct dccp_li_hist *hist, struct list_head *list)
{
	struct dccp_li_hist_entry *entry, *next;

	list_for_each_entry_safe(entry, next, list, dccplih_node) {
		list_del_init(&entry->dccplih_node);
		kmem_cache_free(hist->dccplih_slab, entry);
	}
}

EXPORT_SYMBOL_GPL(dccp_li_hist_purge);

/* Weights used to calculate loss event rate */
/*
 * These are integers as per section 8 of RFC3448. We can then divide by 4 *
 * when we use it.
 */
static const int dccp_li_hist_w[DCCP_LI_HIST_IVAL_F_LENGTH] = {
	4, 4, 4, 4, 3, 2, 1, 1,
};

u32 dccp_li_hist_calc_i_mean(struct list_head *list)
{
	struct dccp_li_hist_entry *li_entry, *li_next;
	int i = 0;
	u32 i_tot;
	u32 i_tot0 = 0;
	u32 i_tot1 = 0;
	u32 w_tot  = 0;

	list_for_each_entry_safe(li_entry, li_next, list, dccplih_node) {
		if (li_entry->dccplih_interval != ~0) {
			i_tot0 += li_entry->dccplih_interval * dccp_li_hist_w[i];
			w_tot  += dccp_li_hist_w[i];
			if (i != 0)
				i_tot1 += li_entry->dccplih_interval * dccp_li_hist_w[i - 1];
		}


		if (++i > DCCP_LI_HIST_IVAL_F_LENGTH)
			break;
	}

	if (i != DCCP_LI_HIST_IVAL_F_LENGTH)
		return 0;

	i_tot = max(i_tot0, i_tot1);

	if (!w_tot) {
		DCCP_WARN("w_tot = 0\n");
		return 1;
	}

	return i_tot / w_tot;
}

EXPORT_SYMBOL_GPL(dccp_li_hist_calc_i_mean);

int dccp_li_hist_interval_new(struct dccp_li_hist *hist,
   struct list_head *list, const u64 seq_loss, const u8 win_loss)
{
	struct dccp_li_hist_entry *entry;
	int i;

	for (i = 0; i < DCCP_LI_HIST_IVAL_F_LENGTH; i++) {
		entry = dccp_li_hist_entry_new(hist, SLAB_ATOMIC);
		if (entry == NULL) {
			dccp_li_hist_purge(hist, list);
			DCCP_BUG("loss interval list entry is NULL");
			return 0;
		}
		entry->dccplih_interval = ~0;
		list_add(&entry->dccplih_node, list);
	}

	entry->dccplih_seqno     = seq_loss;
	entry->dccplih_win_count = win_loss;
	return 1;
}

EXPORT_SYMBOL_GPL(dccp_li_hist_interval_new);
