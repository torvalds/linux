/*
 *  linux/mm/vmstat.c
 *
 *  Manages VM statistics
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  zoned VM statistics
 *  Copyright (C) 2006 Silicon Graphics, Inc.,
 *		Christoph Lameter <christoph@lameter.com>
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/module.h>

/*
 * Accumulate the page_state information across all CPUs.
 * The result is unavoidably approximate - it can change
 * during and after execution of this function.
 */
DEFINE_PER_CPU(struct page_state, page_states) = {0};

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

/*
 * Manage combined zone based / global counters
 *
 * vm_stat contains the global counters
 */
atomic_long_t vm_stat[NR_VM_ZONE_STAT_ITEMS];
EXPORT_SYMBOL(vm_stat);

#ifdef CONFIG_SMP

#define STAT_THRESHOLD 32

/*
 * Determine pointer to currently valid differential byte given a zone and
 * the item number.
 *
 * Preemption must be off
 */
static inline s8 *diff_pointer(struct zone *zone, enum zone_stat_item item)
{
	return &zone_pcp(zone, smp_processor_id())->vm_stat_diff[item];
}

/*
 * For use when we know that interrupts are disabled.
 */
void __mod_zone_page_state(struct zone *zone, enum zone_stat_item item,
				int delta)
{
	s8 *p;
	long x;

	p = diff_pointer(zone, item);
	x = delta + *p;

	if (unlikely(x > STAT_THRESHOLD || x < -STAT_THRESHOLD)) {
		zone_page_state_add(x, zone, item);
		x = 0;
	}

	*p = x;
}
EXPORT_SYMBOL(__mod_zone_page_state);

/*
 * For an unknown interrupt state
 */
void mod_zone_page_state(struct zone *zone, enum zone_stat_item item,
					int delta)
{
	unsigned long flags;

	local_irq_save(flags);
	__mod_zone_page_state(zone, item, delta);
	local_irq_restore(flags);
}
EXPORT_SYMBOL(mod_zone_page_state);

/*
 * Optimized increment and decrement functions.
 *
 * These are only for a single page and therefore can take a struct page *
 * argument instead of struct zone *. This allows the inclusion of the code
 * generated for page_zone(page) into the optimized functions.
 *
 * No overflow check is necessary and therefore the differential can be
 * incremented or decremented in place which may allow the compilers to
 * generate better code.
 *
 * The increment or decrement is known and therefore one boundary check can
 * be omitted.
 *
 * Some processors have inc/dec instructions that are atomic vs an interrupt.
 * However, the code must first determine the differential location in a zone
 * based on the processor number and then inc/dec the counter. There is no
 * guarantee without disabling preemption that the processor will not change
 * in between and therefore the atomicity vs. interrupt cannot be exploited
 * in a useful way here.
 */
void __inc_zone_page_state(struct page *page, enum zone_stat_item item)
{
	struct zone *zone = page_zone(page);
	s8 *p = diff_pointer(zone, item);

	(*p)++;

	if (unlikely(*p > STAT_THRESHOLD)) {
		zone_page_state_add(*p, zone, item);
		*p = 0;
	}
}
EXPORT_SYMBOL(__inc_zone_page_state);

void __dec_zone_page_state(struct page *page, enum zone_stat_item item)
{
	struct zone *zone = page_zone(page);
	s8 *p = diff_pointer(zone, item);

	(*p)--;

	if (unlikely(*p < -STAT_THRESHOLD)) {
		zone_page_state_add(*p, zone, item);
		*p = 0;
	}
}
EXPORT_SYMBOL(__dec_zone_page_state);

void inc_zone_page_state(struct page *page, enum zone_stat_item item)
{
	unsigned long flags;
	struct zone *zone;
	s8 *p;

	zone = page_zone(page);
	local_irq_save(flags);
	p = diff_pointer(zone, item);

	(*p)++;

	if (unlikely(*p > STAT_THRESHOLD)) {
		zone_page_state_add(*p, zone, item);
		*p = 0;
	}
	local_irq_restore(flags);
}
EXPORT_SYMBOL(inc_zone_page_state);

void dec_zone_page_state(struct page *page, enum zone_stat_item item)
{
	unsigned long flags;
	struct zone *zone;
	s8 *p;

	zone = page_zone(page);
	local_irq_save(flags);
	p = diff_pointer(zone, item);

	(*p)--;

	if (unlikely(*p < -STAT_THRESHOLD)) {
		zone_page_state_add(*p, zone, item);
		*p = 0;
	}
	local_irq_restore(flags);
}
EXPORT_SYMBOL(dec_zone_page_state);

/*
 * Update the zone counters for one cpu.
 */
void refresh_cpu_vm_stats(int cpu)
{
	struct zone *zone;
	int i;
	unsigned long flags;

	for_each_zone(zone) {
		struct per_cpu_pageset *pcp;

		pcp = zone_pcp(zone, cpu);

		for (i = 0; i < NR_VM_ZONE_STAT_ITEMS; i++)
			if (pcp->vm_stat_diff[i]) {
				local_irq_save(flags);
				zone_page_state_add(pcp->vm_stat_diff[i],
					zone, i);
				pcp->vm_stat_diff[i] = 0;
				local_irq_restore(flags);
			}
	}
}

static void __refresh_cpu_vm_stats(void *dummy)
{
	refresh_cpu_vm_stats(smp_processor_id());
}

/*
 * Consolidate all counters.
 *
 * Note that the result is less inaccurate but still inaccurate
 * if concurrent processes are allowed to run.
 */
void refresh_vm_stats(void)
{
	on_each_cpu(__refresh_cpu_vm_stats, NULL, 0, 1);
}
EXPORT_SYMBOL(refresh_vm_stats);

#endif

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
	/* Zoned VM counters */
	"nr_anon_pages",
	"nr_mapped",
	"nr_file_pages",
	"nr_slab",
	"nr_page_table_pages",
	"nr_dirty",

	/* Page state */
	"nr_writeback",
	"nr_unstable",

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

		for (i = 0; i < NR_VM_ZONE_STAT_ITEMS; i++)
			seq_printf(m, "\n    %-12s %lu", vmstat_text[i],
					zone_page_state(zone, i));

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
	unsigned long *v;
	struct page_state *ps;
	int i;

	if (*pos >= ARRAY_SIZE(vmstat_text))
		return NULL;

	v = kmalloc(NR_VM_ZONE_STAT_ITEMS * sizeof(unsigned long)
			+ sizeof(*ps), GFP_KERNEL);
	m->private = v;
	if (!v)
		return ERR_PTR(-ENOMEM);
	for (i = 0; i < NR_VM_ZONE_STAT_ITEMS; i++)
		v[i] = global_page_state(i);
	ps = (struct page_state *)(v + NR_VM_ZONE_STAT_ITEMS);
	get_full_page_state(ps);
	ps->pgpgin /= 2;		/* sectors -> kbytes */
	ps->pgpgout /= 2;
	return v + *pos;
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

