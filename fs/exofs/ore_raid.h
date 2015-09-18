/*
 * Copyright (C) from 2011
 * Boaz Harrosh <ooo@electrozaur.com>
 *
 * This file is part of the objects raid engine (ore).
 *
 * It is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * You should have received a copy of the GNU General Public License
 * along with "ore". If not, write to the Free Software Foundation, Inc:
 *	"Free Software Foundation <info@fsf.org>"
 */

#include <scsi/osd_ore.h>

#define ORE_ERR(fmt, a...) printk(KERN_ERR "ore: " fmt, ##a)

#ifdef CONFIG_EXOFS_DEBUG
#define ORE_DBGMSG(fmt, a...) \
	printk(KERN_NOTICE "ore @%s:%d: " fmt, __func__, __LINE__, ##a)
#else
#define ORE_DBGMSG(fmt, a...) \
	do { if (0) printk(fmt, ##a); } while (0)
#endif

/* u64 has problems with printk this will cast it to unsigned long long */
#define _LLU(x) (unsigned long long)(x)

#define ORE_DBGMSG2(M...) do {} while (0)
/* #define ORE_DBGMSG2 ORE_DBGMSG */

/* ios_raid.c stuff needed by ios.c */
int _ore_post_alloc_raid_stuff(struct ore_io_state *ios);
void _ore_free_raid_stuff(struct ore_io_state *ios);

void _ore_add_sg_seg(struct ore_per_dev_state *per_dev, unsigned cur_len,
		 bool not_last);
int _ore_add_parity_unit(struct ore_io_state *ios, struct ore_striping_info *si,
		     struct ore_per_dev_state *per_dev, unsigned cur_len,
		     bool do_xor);
void _ore_add_stripe_page(struct __stripe_pages_2d *sp2d,
		       struct ore_striping_info *si, struct page *page);
static inline void _add_stripe_page(struct __stripe_pages_2d *sp2d,
				struct ore_striping_info *si, struct page *page)
{
	if (!sp2d) /* Inline the fast path */
		return; /* Hay no raid stuff */
	_ore_add_stripe_page(sp2d, si, page);
}

/* ios.c stuff needed by ios_raid.c */
int  _ore_get_io_state(struct ore_layout *layout,
			struct ore_components *oc, unsigned numdevs,
			unsigned sgs_per_dev, unsigned num_par_pages,
			struct ore_io_state **pios);
int _ore_add_stripe_unit(struct ore_io_state *ios,  unsigned *cur_pg,
		unsigned pgbase, struct page **pages,
		struct ore_per_dev_state *per_dev, int cur_len);
int _ore_read_mirror(struct ore_io_state *ios, unsigned cur_comp);
int ore_io_execute(struct ore_io_state *ios);
