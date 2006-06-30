/*
 *  linux/mm/vmstat.c
 *
 *  Manages VM statistics
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 */

#include <linux/config.h>
#include <linux/mm.h>

/*
 * Accumulate the page_state information across all CPUs.
 * The result is unavoidably approximate - it can change
 * during and after execution of this function.
 */
DEFINE_PER_CPU(struct page_state, page_states) = {0};

atomic_t nr_pagecache = ATOMIC_INIT(0);
EXPORT_SYMBOL(nr_pagecache);
#ifdef CONFIG_SMP
DEFINE_PER_CPU(long, nr_pagecache_local) = 0;
#endif

static void __get_page_state(struct page_state *ret, int nr, cpumask_t *cpumask)
{
	unsigned cpu;

	memset(ret, 0, nr * sizeof(unsigned long));
	cpus_and(*cpumask, *cpumask, cpu_online_map);

	for_each_cpu_mask(cpu, *cpumask) {
		unsigned long *in;
		unsigned long *out;
		unsigned off;
		unsigned next_cpu;

		in = (unsigned long *)&per_cpu(page_states, cpu);

		next_cpu = next_cpu(cpu, *cpumask);
		if (likely(next_cpu < NR_CPUS))
			prefetch(&per_cpu(page_states, next_cpu));

		out = (unsigned long *)ret;
		for (off = 0; off < nr; off++)
			*out++ += *in++;
	}
}

void get_page_state_node(struct page_state *ret, int node)
{
	int nr;
	cpumask_t mask = node_to_cpumask(node);

	nr = offsetof(struct page_state, GET_PAGE_STATE_LAST);
	nr /= sizeof(unsigned long);

	__get_page_state(ret, nr+1, &mask);
}

void get_page_state(struct page_state *ret)
{
	int nr;
	cpumask_t mask = CPU_MASK_ALL;

	nr = offsetof(struct page_state, GET_PAGE_STATE_LAST);
	nr /= sizeof(unsigned long);

	__get_page_state(ret, nr + 1, &mask);
}

void get_full_page_state(struct page_state *ret)
{
	cpumask_t mask = CPU_MASK_ALL;

	__get_page_state(ret, sizeof(*ret) / sizeof(unsigned long), &mask);
}

unsigned long read_page_state_offset(unsigned long offset)
{
	unsigned long ret = 0;
	int cpu;

	for_each_online_cpu(cpu) {
		unsigned long in;

		in = (unsigned long)&per_cpu(page_states, cpu) + offset;
		ret += *((unsigned long *)in);
	}
	return ret;
}

void __mod_page_state_offset(unsigned long offset, unsigned long delta)
{
	void *ptr;

	ptr = &__get_cpu_var(page_states);
	*(unsigned long *)(ptr + offset) += delta;
}
EXPORT_SYMBOL(__mod_page_state_offset);

void mod_page_state_offset(unsigned long offset, unsigned long delta)
{
	unsigned long flags;
	void *ptr;

	local_irq_save(flags);
	ptr = &__get_cpu_var(page_states);
	*(unsigned long *)(ptr + offset) += delta;
	local_irq_restore(flags);
}
EXPORT_SYMBOL(mod_page_state_offset);

void __get_zone_counts(unsigned long *active, unsigned long *inactive,
			unsigned long *free, struct pglist_data *pgdat)
{
	struct zone *zones = pgdat->node_zones;
	int i;

	*active = 0;
	*inactive = 0;
	*free = 0;
	for (i = 0; i < MAX_NR_ZONES; i++) {
		*active += zones[i].nr_active;
		*inactive += zones[i].nr_inactive;
		*free += zones[i].free_pages;
	}
}

void get_zone_counts(unsigned long *active,
		unsigned long *inactive, unsigned long *free)
{
	struct pglist_data *pgdat;

	*active = 0;
	*inactive = 0;
	*free = 0;
	for_each_online_pgdat(pgdat) {
		unsigned long l, m, n;
		__get_zone_counts(&l, &m, &n, pgdat);
		*active += l;
		*inactive += m;
		*free += n;
	}
}

#ifdef CONFIG_PROC_FS

#include <linux/seq_file.h>

static void *frag_start(struct seq_file *m, loff_t *pos)
{
	pg_data_t *pgdat;
	loff_t node = *pos;
	for (pgdat = first_online_pgdat();
	     pgdat && node;
	     pgdat = next_online_pgdat(pgdat))
		--node;

	return pgdat;
}

static void *frag_next(struct seq_file *m, void *arg, loff_t *pos)
{
	pg_data_t *pgdat = (pg_data_t *)arg;

	(*pos)++;
	return next_online_pgdat(pgdat);
}

static void frag_stop(struct seq_file *m, void *arg)
{
}

/*
 * This walks the free areas for each zone.
 */
static int frag_show(struct seq_file *m, void *arg)
{
	pg_data_t *pgdat = (pg_data_t *)arg;
	struct zone *zone;
	struct zone *node_zones = pgdat->node_zones;
	unsigned long flags;
	int order;

	for (zone = node_zones; zone - node_zones < MAX_NR_ZONES; ++zone) {
		if (!populated_zone(zone))
			continue;

		spin_lock_irqsave(&zone->lock, flags);
		seq_printf(m, "Node %d, zone %8s ", pgdat->node_id, zone->name);
		for (order = 0; order < MAX_ORDER; ++order)
			seq_printf(m, "%6lu ", zone->free_area[order].nr_free);
		spin_unlock_irqrestore(&zone->lock, flags);
		seq_putc(m, '\n');
	}
	return 0;
}

struct seq_operations fragmentation_op = {
	.start	= frag_start,
	.next	= frag_next,
	.stop	= frag_stop,
	.show	= frag_show,
};

static char *vmstat_text[] = {
	"nr_dirty",
	"nr_writeback",
	"nr_unstable",
	"nr_page_table_pages",
	"nr_mapped",
	"nr_slab",

	"pgpgin",
	"pgpgout",
	"pswpin",
	"pswpout",

	"pgalloc_high",
	"pgalloc_normal",
	"pgalloc_dma32",
	"pgalloc_dma",

	"pgfree",
	"pgactivate",
	"pgdeactivate",

	"pgfault",
	"pgmajfault",

	"pgrefill_high",
	"pgrefill_normal",
	"pgrefill_dma32",
	"pgrefill_dma",

	"pgsteal_high",
	"pgsteal_normal",
	"pgsteal_dma32",
	"pgsteal_dma",

	"pgscan_kswapd_high",
	"pgscan_kswapd_normal",
	"pgscan_kswapd_dma32",
	"pgscan_kswapd_dma",

	"pgscan_direct_high",
	"pgscan_direct_normal",
	"pgscan_direct_dma32",
	"pgscan_direct_dma",

	"pginodesteal",
	"slabs_scanned",
	"kswapd_steal",
	"kswapd_inodesteal",
	"pageoutrun",
	"allocstall",

	"pgrotated",
	"nr_bounce",
};

/*
 * Output information about zones in @pgdat.
 */
static int zoneinfo_show(struct seq_file *m, void *arg)
{
	pg_data_t *pgdat = arg;
	struct zone *zone;
	struct zone *node_zones = pgdat->node_zones;
	unsigned long flags;

	for (zone = node_zones; zone - node_zones < MAX_NR_ZONES; zone++) {
		int i;

		if (!populated_zone(zone))
			continue;

		spin_lock_irqsave(&zone->lock, flags);
		seq_printf(m, "Node %d, zone %8s", pgdat->node_id, zone->name);
		seq_printf(m,
			   "\n  pages free     %lu"
			   "\n        min      %lu"
			   "\n        low      %lu"
			   "\n        high     %lu"
			   "\n        active   %lu"
			   "\n        inactive %lu"
			   "\n        scanned  %lu (a: %lu i: %lu)"
			   "\n        spanned  %lu"
			   "\n        present  %lu",
			   zone->free_pages,
			   zone->pages_min,
			   zone->pages_low,
			   zone->pages_high,
			   zone->nr_active,
			   zone->nr_inactive,
			   zone->pages_scanned,
			   zone->nr_scan_active, zone->nr_scan_inactive,
			   zone->spanned_pages,
			   zone->present_pages);
		seq_printf(m,
			   "\n        protection: (%lu",
			   zone->lowmem_reserve[0]);
		for (i = 1; i < ARRAY_SIZE(zone->lowmem_reserve); i++)
			seq_printf(m, ", %lu", zone->lowmem_reserve[i]);
		seq_printf(m,
			   ")"
			   "\n  pagesets");
		for_each_online_cpu(i) {
			struct per_cpu_pageset *pageset;
			int j;

			pageset = zone_pcp(zone, i);
			for (j = 0; j < ARRAY_SIZE(pageset->pcp); j++) {
				if (pageset->pcp[j].count)
					break;
			}
			if (j == ARRAY_SIZE(pageset->pcp))
				continue;
			for (j = 0; j < ARRAY_SIZE(pageset->pcp); j++) {
				seq_printf(m,
					   "\n    cpu: %i pcp: %i"
					   "\n              count: %i"
					   "\n              high:  %i"
					   "\n              batch: %i",
					   i, j,
					   pageset->pcp[j].count,
					   pageset->pcp[j].high,
					   pageset->pcp[j].batch);
			}
#ifdef CONFIG_NUMA
			seq_printf(m,
				   "\n            numa_hit:       %lu"
				   "\n            numa_miss:      %lu"
				   "\n            numa_foreign:   %lu"
				   "\n            interleave_hit: %lu"
				   "\n            local_node:     %lu"
				   "\n            other_node:     %lu",
				   pageset->numa_hit,
				   pageset->numa_miss,
				   pageset->numa_foreign,
				   pageset->interleave_hit,
				   pageset->local_node,
				   pageset->other_node);
#endif
		}
		seq_printf(m,
			   "\n  all_unreclaimable: %u"
			   "\n  prev_priority:     %i"
			   "\n  temp_priority:     %i"
			   "\n  start_pfn:         %lu",
			   zone->all_unreclaimable,
			   zone->prev_priority,
			   zone->temp_priority,
			   zone->zone_start_pfn);
		spin_unlock_irqrestore(&zone->lock, flags);
		seq_putc(m, '\n');
	}
	return 0;
}

struct seq_operations zoneinfo_op = {
	.start	= frag_start, /* iterate over all zones. The same as in
			       * fragmentation. */
	.next	= frag_next,
	.stop	= frag_stop,
	.show	= zoneinfo_show,
};

static void *vmstat_start(struct seq_file *m, loff_t *pos)
{
	struct page_state *ps;

	if (*pos >= ARRAY_SIZE(vmstat_text))
		return NULL;

	ps = kmalloc(sizeof(*ps), GFP_KERNEL);
	m->private = ps;
	if (!ps)
		return ERR_PTR(-ENOMEM);
	get_full_page_state(ps);
	ps->pgpgin /= 2;		/* sectors -> kbytes */
	ps->pgpgout /= 2;
	return (unsigned long *)ps + *pos;
}

static void *vmstat_next(struct seq_file *m, void *arg, loff_t *pos)
{
	(*pos)++;
	if (*pos >= ARRAY_SIZE(vmstat_text))
		return NULL;
	return (unsigned long *)m->private + *pos;
}

static int vmstat_show(struct seq_file *m, void *arg)
{
	unsigned long *l = arg;
	unsigned long off = l - (unsigned long *)m->private;

	seq_printf(m, "%s %lu\n", vmstat_text[off], *l);
	return 0;
}

static void vmstat_stop(struct seq_file *m, void *arg)
{
	kfree(m->private);
	m->private = NULL;
}

struct seq_operations vmstat_op = {
	.start	= vmstat_start,
	.next	= vmstat_next,
	.stop	= vmstat_stop,
	.show	= vmstat_show,
};

#endif /* CONFIG_PROC_FS */

