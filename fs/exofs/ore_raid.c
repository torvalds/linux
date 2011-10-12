/*
 * Copyright (C) 2011
 * Boaz Harrosh <bharrosh@panasas.com>
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

#include <linux/gfp.h>

#include "ore_raid.h"

struct page *_raid_page_alloc(void)
{
	return alloc_page(GFP_KERNEL);
}

void _raid_page_free(struct page *p)
{
	__free_page(p);
}

void _ore_add_sg_seg(struct ore_per_dev_state *per_dev, unsigned cur_len,
		     bool not_last)
{
	struct osd_sg_entry *sge;

	ORE_DBGMSG("dev=%d cur_len=0x%x not_last=%d cur_sg=%d "
		     "offset=0x%llx length=0x%x last_sgs_total=0x%x\n",
		     per_dev->dev, cur_len, not_last, per_dev->cur_sg,
		     _LLU(per_dev->offset), per_dev->length,
		     per_dev->last_sgs_total);

	if (!per_dev->cur_sg) {
		sge = per_dev->sglist;

		/* First time we prepare two entries */
		if (per_dev->length) {
			++per_dev->cur_sg;
			sge->offset = per_dev->offset;
			sge->len = per_dev->length;
		} else {
			/* Here the parity is the first unit of this object.
			 * This happens every time we reach a parity device on
			 * the same stripe as the per_dev->offset. We need to
			 * just skip this unit.
			 */
			per_dev->offset += cur_len;
			return;
		}
	} else {
		/* finalize the last one */
		sge = &per_dev->sglist[per_dev->cur_sg - 1];
		sge->len = per_dev->length - per_dev->last_sgs_total;
	}

	if (not_last) {
		/* Partly prepare the next one */
		struct osd_sg_entry *next_sge = sge + 1;

		++per_dev->cur_sg;
		next_sge->offset = sge->offset + sge->len + cur_len;
		/* Save cur len so we know how mutch was added next time */
		per_dev->last_sgs_total = per_dev->length;
		next_sge->len = 0;
	} else if (!sge->len) {
		/* Optimize for when the last unit is a parity */
		--per_dev->cur_sg;
	}
}

/* In writes @cur_len means length left. .i.e cur_len==0 is the last parity U */
int _ore_add_parity_unit(struct ore_io_state *ios,
			    struct ore_striping_info *si,
			    struct ore_per_dev_state *per_dev,
			    unsigned cur_len)
{
	if (ios->reading) {
		BUG_ON(per_dev->cur_sg >= ios->sgs_per_dev);
		_ore_add_sg_seg(per_dev, cur_len, true);
	} else {
		struct page **pages = ios->parity_pages + ios->cur_par_page;
		unsigned num_pages = ios->layout->stripe_unit / PAGE_SIZE;
		unsigned array_start = 0;
		unsigned i;
		int ret;

		for (i = 0; i < num_pages; i++) {
			pages[i] = _raid_page_alloc();
			if (unlikely(!pages[i]))
				return -ENOMEM;

			++(ios->cur_par_page);
			/* TODO: only read support for now */
			clear_highpage(pages[i]);
		}

		ORE_DBGMSG("writing dev=%d num_pages=%d cur_par_page=%d",
			     per_dev->dev, num_pages, ios->cur_par_page);

		ret = _ore_add_stripe_unit(ios,  &array_start, 0, pages,
					   per_dev, num_pages * PAGE_SIZE);
		if (unlikely(ret))
			return ret;
	}
	return 0;
}

int _ore_post_alloc_raid_stuff(struct ore_io_state *ios)
{
	/*TODO: Only raid writes has stuff to add here */
	return 0;
}

void _ore_free_raid_stuff(struct ore_io_state *ios)
{
	if (ios->parity_pages) { /* writing and raid */
		unsigned i;

		for (i = 0; i < ios->cur_par_page; i++) {
			struct page *page = ios->parity_pages[i];

			if (page)
				_raid_page_free(page);
		}
		if (ios->extra_part_alloc)
			kfree(ios->parity_pages);
	} else {
		/* Will only be set if raid reading && sglist is big */
		if (ios->extra_part_alloc)
			kfree(ios->per_dev[0].sglist);
	}
}
