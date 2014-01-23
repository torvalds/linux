/*
 * mm_init.c - Memory initialisation verification and debugging
 *
 * Copyright 2008 IBM Corporation, 2008
 * Author Mel Gorman <mel@csn.ul.ie>
 *
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/export.h>
#include <linux/memory.h>
#include <linux/notifier.h>
#include "internal.h"

#ifdef CONFIG_DEBUG_MEMORY_INIT
int mminit_loglevel;

#ifndef SECTIONS_SHIFT
#define SECTIONS_SHIFT	0
#endif

/* The zonelists are simply reported, validation is manual. */
void mminit_verify_zonelist(void)
{
	int nid;

	if (mminit_loglevel < MMINIT_VERIFY)
		return;

	for_each_online_node(nid) {
		pg_data_t *pgdat = NODE_DATA(nid);
		struct zone *zone;
		struct zoneref *z;
		struct zonelist *zonelist;
		int i, listid, zoneid;

		BUG_ON(MAX_ZONELISTS > 2);
		for (i = 0; i < MAX_ZONELISTS * MAX_NR_ZONES; i++) {

			/* Identify the zone and nodelist */
			zoneid = i % MAX_NR_ZONES;
			listid = i / MAX_NR_ZONES;
			zonelist = &pgdat->node_zonelists[listid];
			zone = &pgdat->node_zones[zoneid];
			if (!populated_zone(zone))
				continue;

			/* Print information about the zonelist */
			printk(KERN_DEBUG "mminit::zonelist %s %d:%s = ",
				listid > 0 ? "thisnode" : "general", nid,
				zone->name);

			/* Iterate the zonelist */
			for_each_zone_zonelist(zone, z, zonelist, zoneid) {
#ifdef CONFIG_NUMA
				printk(KERN_CONT "%d:%s ",
					zone->node, zone->name);
#else
				printk(KERN_CONT "0:%s ", zone->name);
#endif /* CONFIG_NUMA */
			}
			printk(KERN_CONT "\n");
		}
	}
}

void __init mminit_verify_pageflags_layout(void)
{
	int shift, width;
	unsigned long or_mask, add_mask;

	shift = 8 * sizeof(unsigned long);
	width = shift - SECTIONS_WIDTH - NODES_WIDTH - ZONES_WIDTH - LAST_CPUPID_SHIFT;
	mminit_dprintk(MMINIT_TRACE, "pageflags_layout_widths",
		"Section %d Node %d Zone %d Lastcpupid %d Flags %d\n",
		SECTIONS_WIDTH,
		NODES_WIDTH,
		ZONES_WIDTH,
		LAST_CPUPID_WIDTH,
		NR_PAGEFLAGS);
	mminit_dprintk(MMINIT_TRACE, "pageflags_layout_shifts",
		"Section %d Node %d Zone %d Lastcpupid %d\n",
		SECTIONS_SHIFT,
		NODES_SHIFT,
		ZONES_SHIFT,
		LAST_CPUPID_SHIFT);
	mminit_dprintk(MMINIT_TRACE, "pageflags_layout_pgshifts",
		"Section %lu Node %lu Zone %lu Lastcpupid %lu\n",
		(unsigned long)SECTIONS_PGSHIFT,
		(unsigned long)NODES_PGSHIFT,
		(unsigned long)ZONES_PGSHIFT,
		(unsigned long)LAST_CPUPID_PGSHIFT);
	mminit_dprintk(MMINIT_TRACE, "pageflags_layout_nodezoneid",
		"Node/Zone ID: %lu -> %lu\n",
		(unsigned long)(ZONEID_PGOFF + ZONEID_SHIFT),
		(unsigned long)ZONEID_PGOFF);
	mminit_dprintk(MMINIT_TRACE, "pageflags_layout_usage",
		"location: %d -> %d layout %d -> %d unused %d -> %d page-flags\n",
		shift, width, width, NR_PAGEFLAGS, NR_PAGEFLAGS, 0);
#ifdef NODE_NOT_IN_PAGE_FLAGS
	mminit_dprintk(MMINIT_TRACE, "pageflags_layout_nodeflags",
		"Node not in page flags");
#endif
#ifdef LAST_CPUPID_NOT_IN_PAGE_FLAGS
	mminit_dprintk(MMINIT_TRACE, "pageflags_layout_nodeflags",
		"Last cpupid not in page flags");
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
#endif /* CONFIG_DEBUG_MEMORY_INIT */

struct kobject *mm_kobj;
EXPORT_SYMBOL_GPL(mm_kobj);

#ifdef CONFIG_SMP
s32 vm_committed_as_batch = 32;

static void __meminit mm_compute_batch(void)
{
	u64 memsized_batch;
	s32 nr = num_present_cpus();
	s32 batch = max_t(s32, nr*2, 32);

	/* batch size set to 0.4% of (total memory/#cpus), or max int32 */
	memsized_batch = min_t(u64, (totalram_pages/nr)/256, 0x7fffffff);

	vm_committed_as_batch = max_t(s32, memsized_batch, batch);
}

static int __meminit mm_compute_batch_notifier(struct notifier_block *self,
					unsigned long action, void *arg)
{
	switch (action) {
	case MEM_ONLINE:
	case MEM_OFFLINE:
		mm_compute_batch();
	default:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block compute_batch_nb __meminitdata = {
	.notifier_call = mm_compute_batch_notifier,
	.priority = IPC_CALLBACK_PRI, /* use lowest priority */
};

static int __init mm_compute_batch_init(void)
{
	mm_compute_batch();
	register_hotmemory_notifier(&compute_batch_nb);

	return 0;
}

__initcall(mm_compute_batch_init);

#endif

static int __init mm_sysfs_init(void)
{
	mm_kobj = kobject_create_and_add("mm", kernel_kobj);
	if (!mm_kobj)
		return -ENOMEM;

	return 0;
}
pure_initcall(mm_sysfs_init);
