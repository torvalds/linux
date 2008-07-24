/*
 * mm_init.c - Memory initialisation verification and debugging
 *
 * Copyright 2008 IBM Corporation, 2008
 * Author Mel Gorman <mel@csn.ul.ie>
 *
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include "internal.h"

int __meminitdata mminit_loglevel;

void __init mminit_verify_pageflags_layout(void)
{
	int shift, width;
	unsigned long or_mask, add_mask;

	shift = 8 * sizeof(unsigned long);
	width = shift - SECTIONS_WIDTH - NODES_WIDTH - ZONES_WIDTH;
	mminit_dprintk(MMINIT_TRACE, "pageflags_layout_widths",
		"Section %d Node %d Zone %d Flags %d\n",
		SECTIONS_WIDTH,
		NODES_WIDTH,
		ZONES_WIDTH,
		NR_PAGEFLAGS);
	mminit_dprintk(MMINIT_TRACE, "pageflags_layout_shifts",
		"Section %d Node %d Zone %d\n",
#ifdef SECTIONS_SHIFT
		SECTIONS_SHIFT,
#else
		0,
#endif
		NODES_SHIFT,
		ZONES_SHIFT);
	mminit_dprintk(MMINIT_TRACE, "pageflags_layout_offsets",
		"Section %lu Node %lu Zone %lu\n",
		(unsigned long)SECTIONS_PGSHIFT,
		(unsigned long)NODES_PGSHIFT,
		(unsigned long)ZONES_PGSHIFT);
	mminit_dprintk(MMINIT_TRACE, "pageflags_layout_zoneid",
		"Zone ID: %lu -> %lu\n",
		(unsigned long)ZONEID_PGOFF,
		(unsigned long)(ZONEID_PGOFF + ZONEID_SHIFT));
	mminit_dprintk(MMINIT_TRACE, "pageflags_layout_usage",
		"location: %d -> %d unused %d -> %d flags %d -> %d\n",
		shift, width, width, NR_PAGEFLAGS, NR_PAGEFLAGS, 0);
#ifdef NODE_NOT_IN_PAGE_FLAGS
	mminit_dprintk(MMINIT_TRACE, "pageflags_layout_nodeflags",
		"Node not in page flags");
#endif

	if (SECTIONS_WIDTH) {
		shift -= SECTIONS_WIDTH;
		BUG_ON(shift != SECTIONS_PGSHIFT);
	}
	if (NODES_WIDTH) {
		shift -= NODES_WIDTH;
		BUG_ON(shift != NODES_PGSHIFT);
	}
	if (ZONES_WIDTH) {
		shift -= ZONES_WIDTH;
		BUG_ON(shift != ZONES_PGSHIFT);
	}

	/* Check for bitmask overlaps */
	or_mask = (ZONES_MASK << ZONES_PGSHIFT) |
			(NODES_MASK << NODES_PGSHIFT) |
			(SECTIONS_MASK << SECTIONS_PGSHIFT);
	add_mask = (ZONES_MASK << ZONES_PGSHIFT) +
			(NODES_MASK << NODES_PGSHIFT) +
			(SECTIONS_MASK << SECTIONS_PGSHIFT);
	BUG_ON(or_mask != add_mask);
}

void __meminit mminit_verify_page_links(struct page *page, enum zone_type zone,
			unsigned long nid, unsigned long pfn)
{
	BUG_ON(page_to_nid(page) != nid);
	BUG_ON(page_zonenum(page) != zone);
	BUG_ON(page_to_pfn(page) != pfn);
}

static __init int set_mminit_loglevel(char *str)
{
	get_option(&str, &mminit_loglevel);
	return 0;
}
early_param("mminit_loglevel", set_mminit_loglevel);
