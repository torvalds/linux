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

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/cpu.h>
#include <linux/sched.h>

#ifdef CONFIG_VM_EVENT_COUNTERS
DEFINE_PER_CPU(struct vm_event_state, vm_event_states) = {{0}};
EXPORT_PER_CPU_SYMBOL(vm_event_states);

static void sum_vm_events(unsigned long *ret, cpumask_t *cpumask)
{
	int cpu = 0;
	int i;

	memset(ret, 0, NR_VM_EVENT_ITEMS * sizeof(unsigned long));

	cpu = first_cpu(*cpumask);
	while (cpu < NR_CPUS) {
		struct vm_event_state *this = &per_cpu(vm_event_states, cpu);

		cpu = next_cpu(cpu, *cpumask);

		if (cpu < NR_CPUS)
			prefetch(&per_cpu(vm_event_states, cpu));


		for (i = 0; i < NR_VM_EVENT_ITEMS; i++)
			ret[i] += this->event[i];
	}
}

/*
 * Accumulate the vm event counters across all CPUs.
 * The result is unavoidably approximate - it can change
 * during and after execution of this function.
*/
void all_vm_events(unsigned long *ret)
{
	sum_vm_events(ret, &cpu_online_map);
}
EXPORT_SYMBOL_GPL(all_vm_events);

#ifdef CONFIG_HOTPLUG
/*
 * Fold the foreign cpu events into our own.
 *
 * This is adding to the events on one processor
 * but keeps the global counts constant.
 */
void vm_events_fold_cpu(int cpu)
{
	struct vm_event_state *fold_state = &per_cpu(vm_event_states, cpu);
	int i;

	for (i = 0; i < NR_VM_EVENT_ITEMS; i++) {
		count_vm_events(i, fold_state->event[i]);
		fold_state->event[i] = 0;
	}
}
#endif /* CONFIG_HOTPLUG */

#endif /* CONFIG_VM_EVENT_COUNTERS */

/*
 * Manage combined zone based / global counters
 *
 * vm_stat contains the global counters
 */
atomic_long_t vm_stat[NR_VM_ZONE_STAT_ITEMS];
EXPORT_SYMBOL(vm_stat);

#ifdef CONFIG_SMP

static int calculate_threshold(struct zone *zone)
{
	int threshold;
	int mem;	/* memory in 128 MB units */

	/*
	 * The threshold scales with the number of processors and the amount
	 * of memory per zone. More memory means that we can defer updates for
	 * longer, more processors could lead to more contention.
 	 * fls() is used to have a cheap way of logarithmic scaling.
	 *
	 * Some sample thresholds:
	 *
	 * Threshold	Processors	(fls)	Zonesize	fls(mem+1)
	 * ------------------------------------------------------------------
	 * 8		1		1	0.9-1 GB	4
	 * 16		2		2	0.9-1 GB	4
	 * 20 		2		2	1-2 GB		5
	 * 24		2		2	2-4 GB		6
	 * 28		2		2	4-8 GB		7
	 * 32		2		2	8-16 GB		8
	 * 4		2		2	<128M		1
	 * 30		4		3	2-4 GB		5
	 * 48		4		3	8-16 GB		8
	 * 32		8		4	1-2 GB		4
	 * 32		8		4	0.9-1GB		4
	 * 10		16		5	<128M		1
	 * 40		16		5	900M		4
	 * 70		64		7	2-4 GB		5
	 * 84		64		7	4-8 GB		6
	 * 108		512		9	4-8 GB		6
	 * 125		1024		10	8-16 GB		8
	 * 125		1024		10	16-32 GB	9
	 */

	mem = zone->present_pages >> (27 - PAGE_SHIFT);

	threshold = 2 * fls(num_online_cpus()) * (1 + fls(mem));

	/*
	 * Maximum threshold is 125
	 */
	threshold = min(125, threshold);

	return threshold;
}

/*
 * Refresh the thresholds for each zone.
 */
static void refresh_zone_stat_thresholds(void)
{
	struct zone *zone;
	int cpu;
	int threshold;

	for_each_zone(zone) {

		if (!zone->present_pages)
			continue;

		threshold = calculate_threshold(zone);

		for_each_online_cpu(cpu)
			zone_pcp(zone, cpu)->stat_threshold = threshold;
	}
}

/*
 * For use when we know that interrupts are disabled.
 */
void __mod_zone_page_state(struct zone *zone, enum zone_stat_item item,
				int delta)
{
	struct per_cpu_pageset *pcp = zone_pcp(zone, smp_processor_id());
	s8 *p = pcp->vm_stat_diff + item;
	long x;

	x = delta + *p;

	if (unlikely(x > pcp->stat_threshold || x < -pcp->stat_threshold)) {
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
 * The increment or decrement is known and therefore one boundary check can
 * be omitted.
 *
 * NOTE: These functions are very performance sensitive. Change only
 * with care.
 *
 * Some processors have inc/dec instructions that are atomic vs an interrupt.
 * However, the code must first determine the differential location in a zone
 * based on the processor number and then inc/dec the counter. There is no
 * guarantee without disabling preemption that the processor will not change
 * in between and therefore the atomicity vs. interrupt cannot be exploited
 * in a useful way here.
 */
void __inc_zone_state(struct zone *zone, enum zone_stat_item item)
{
	struct per_cpu_pageset *pcp = zone_pcp(zone, smp_processor_id());
	s8 *p = pcp->vm_stat_diff + item;

	(*p)++;

	if (unlikely(*p > pcp->stat_threshold)) {
		int overstep = pcp->stat_threshold / 2;

		zone_page_state_add(*p + overstep, zone, item);
		*p = -overstep;
	}
}

void __inc_zone_page_state(struct page *page, enum zone_stat_item item)
{
	__inc_zone_state(page_zone(page), item);
}
EXPORT_SYMBOL(__inc_zone_page_state);

void __dec_zone_state(struct zone *zone, enum zone_stat_item item)
{
	struct per_cpu_pageset *pcp = zone_pcp(zone, smp_processor_id());
	s8 *p = pcp->vm_stat_diff + item;

	(*p)--;

	if (unlikely(*p < - pcp->stat_threshold)) {
		int overstep = pcp->stat_threshold / 2;

		zone_page_state_add(*p - overstep, zone, item);
		*p = overstep;
	}
}

void __dec_zone_page_state(struct page *page, enum zone_stat_item item)
{
	__dec_zone_state(page_zone(page), item);
}
EXPORT_SYMBOL(__dec_zone_page_state);

void inc_zone_state(struct zone *zone, enum zone_stat_item item)
{
	unsigned long flags;

	local_irq_save(flags);
	__inc_zone_state(zone, item);
	local_irq_restore(flags);
}

void inc_zone_page_state(struct page *page, enum zone_stat_item item)
{
	unsigned long flags;
	struct zone *zone;

	zone = page_zone(page);
	local_irq_save(flags);
	__inc_zone_state(zone, item);
	local_irq_restore(flags);
}
EXPORT_SYMBOL(inc_zone_page_state);

void dec_zone_page_state(struct page *page, enum zone_stat_item item)
{
	unsigned long flags;

	local_irq_save(flags);
	__dec_zone_page_state(page, item);
	local_irq_restore(flags);
}
EXPORT_SYMBOL(dec_zone_page_state);

/*
 * Update the zone counters for one cpu.
 *
 * Note that refresh_cpu_vm_stats strives to only access
 * node local memory. The per cpu pagesets on remote zones are placed
 * in the memory local to the processor using that pageset. So the
 * loop over all zones will access a series of cachelines local to
 * the processor.
 *
 * The call to zone_page_state_add updates the cachelines with the
 * statistics in the remote zone struct as well as the global cachelines
 * with the global counters. These could cause remote node cache line
 * bouncing and will have to be only done when necessary.
 */
void refresh_cpu_vm_stats(int cpu)
{
	struct zone *zone;
	int i;
	unsigned long flags;

	for_each_zone(zone) {
		struct per_cpu_pageset *p;

		if (!populated_zone(zone))
			continue;

		p = zone_pcp(zone, cpu);

		for (i = 0; i < NR_VM_ZONE_STAT_ITEMS; i++)
			if (p->vm_stat_diff[i]) {
				local_irq_save(flags);
				zone_page_state_add(p->vm_stat_diff[i],
					zone, i);
				p->vm_stat_diff[i] = 0;
#ifdef CONFIG_NUMA
				/* 3 seconds idle till flush */
				p->expire = 3;
#endif
				local_irq_restore(flags);
			}
#ifdef CONFIG_NUMA
		/*
		 * Deal with draining the remote pageset of this
		 * processor
		 *
		 * Check if there are pages remaining in this pageset
		 * if not then there is nothing to expire.
		 */
		if (!p->expire || (!p->pcp[0].count && !p->pcp[1].count))
			continue;

		/*
		 * We never drain zones local to this processor.
		 */
		if (zone_to_nid(zone) == numa_node_id()) {
			p->expire = 0;
			continue;
		}

		p->expire--;
		if (p->expire)
			continue;

		if (p->pcp[0].count)
			drain_zone_pages(zone, p->pcp + 0);

		if (p->pcp[1].count)
			drain_zone_pages(zone, p->pcp + 1);
#endif
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

#ifdef CONFIG_NUMA
/*
 * zonelist = the list of zones passed to the allocator
 * z 	    = the zone from which the allocation occurred.
 *
 * Must be called with interrupts disabled.
 */
void zone_statistics(struct zonelist *zonelist, struct zone *z)
{
	if (z->zone_pgdat == zonelist->zones[0]->zone_pgdat) {
		__inc_zone_state(z, NUMA_HIT);
	} else {
		__inc_zone_state(z, NUMA_MISS);
		__inc_zone_state(zonelist->zones[0], NUMA_FOREIGN);
	}
	if (z->node == numa_node_id())
		__inc_zone_state(z, NUMA_LOCAL);
	else
		__inc_zone_state(z, NUMA_OTHER);
}
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

const struct seq_operations fragmentation_op = {
	.start	= frag_start,
	.next	= frag_next,
	.stop	= frag_stop,
	.show	= frag_show,
};

#ifdef CONFIG_ZONE_DMA
#define TEXT_FOR_DMA(xx) xx "_dma",
#else
#define TEXT_FOR_DMA(xx)
#endif

#ifdef CONFIG_ZONE_DMA32
#define TEXT_FOR_DMA32(xx) xx "_dma32",
#else
#define TEXT_FOR_DMA32(xx)
#endif

#ifdef CONFIG_HIGHMEM
#define TEXT_FOR_HIGHMEM(xx) xx "_high",
#else
#define TEXT_FOR_HIGHMEM(xx)
#endif

#define TEXTS_FOR_ZONES(xx) TEXT_FOR_DMA(xx) TEXT_FOR_DMA32(xx) xx "_normal", \
					TEXT_FOR_HIGHMEM(xx)

static const char * const vmstat_text[] = {
	/* Zoned VM counters */
	"nr_free_pages",
	"nr_active",
	"nr_inactive",
	"nr_anon_pages",
	"nr_mapped",
	"nr_file_pages",
	"nr_dirty",
	"nr_writeback",
	"nr_slab_reclaimable",
	"nr_slab_unreclaimable",
	"nr_page_table_pages",
	"nr_unstable",
	"nr_bounce",
	"nr_vmscan_write",

#ifdef CONFIG_NUMA
	"numa_hit",
	"numa_miss",
	"numa_foreign",
	"numa_interleave",
	"numa_local",
	"numa_other",
#endif

#ifdef CONFIG_VM_EVENT_COUNTERS
	"pgpgin",
	"pgpgout",
	"pswpin",
	"pswpout",

	TEXTS_FOR_ZONES("pgalloc")

	"pgfree",
	"pgactivate",
	"pgdeactivate",

	"pgfault",
	"pgmajfault",

	TEXTS_FOR_ZONES("pgrefill")
	TEXTS_FOR_ZONES("pgsteal")
	TEXTS_FOR_ZONES("pgscan_kswapd")
	TEXTS_FOR_ZONES("pgscan_direct")

	"pginodesteal",
	"slabs_scanned",
	"kswapd_steal",
	"kswapd_inodesteal",
	"pageoutrun",
	"allocstall",

	"pgrotated",
#endif
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
			   "\n        scanned  %lu (a: %lu i: %lu)"
			   "\n        spanned  %lu"
			   "\n        present  %lu",
			   zone_page_state(zone, NR_FREE_PAGES),
			   zone->pages_min,
			   zone->pages_low,
			   zone->pages_high,
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
#ifdef CONFIG_SMP
			seq_printf(m, "\n  vm stats threshold: %d",
					pageset->stat_threshold);
#endif
		}
		seq_printf(m,
			   "\n  all_unreclaimable: %u"
			   "\n  prev_priority:     %i"
			   "\n  start_pfn:         %lu",
			   zone->all_unreclaimable,
			   zone->prev_priority,
			   zone->zone_start_pfn);
		spin_unlock_irqrestore(&zone->lock, flags);
		seq_putc(m, '\n');
	}
	return 0;
}

const struct seq_operations zoneinfo_op = {
	.start	= frag_start, /* iterate over all zones. The same as in
			       * fragmentation. */
	.next	= frag_next,
	.stop	= frag_stop,
	.show	= zoneinfo_show,
};

static void *vmstat_start(struct seq_file *m, loff_t *pos)
{
	unsigned long *v;
#ifdef CONFIG_VM_EVENT_COUNTERS
	unsigned long *e;
#endif
	int i;

	if (*pos >= ARRAY_SIZE(vmstat_text))
		return NULL;

#ifdef CONFIG_VM_EVENT_COUNTERS
	v = kmalloc(NR_VM_ZONE_STAT_ITEMS * sizeof(unsigned long)
			+ sizeof(struct vm_event_state), GFP_KERNEL);
#else
	v = kmalloc(NR_VM_ZONE_STAT_ITEMS * sizeof(unsigned long),
			GFP_KERNEL);
#endif
	m->private = v;
	if (!v)
		return ERR_PTR(-ENOMEM);
	for (i = 0; i < NR_VM_ZONE_STAT_ITEMS; i++)
		v[i] = global_page_state(i);
#ifdef CONFIG_VM_EVENT_COUNTERS
	e = v + NR_VM_ZONE_STAT_ITEMS;
	all_vm_events(e);
	e[PGPGIN] /= 2;		/* sectors -> kbytes */
	e[PGPGOUT] /= 2;
#endif
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

const struct seq_operations vmstat_op = {
	.start	= vmstat_start,
	.next	= vmstat_next,
	.stop	= vmstat_stop,
	.show	= vmstat_show,
};

#endif /* CONFIG_PROC_FS */

#ifdef CONFIG_SMP
static DEFINE_PER_CPU(struct delayed_work, vmstat_work);
int sysctl_stat_interval __read_mostly = HZ;

static void vmstat_update(struct work_struct *w)
{
	refresh_cpu_vm_stats(smp_processor_id());
	schedule_delayed_work(&__get_cpu_var(vmstat_work),
		sysctl_stat_interval);
}

static void __devinit start_cpu_timer(int cpu)
{
	struct delayed_work *vmstat_work = &per_cpu(vmstat_work, cpu);

	INIT_DELAYED_WORK_DEFERRABLE(vmstat_work, vmstat_update);
	schedule_delayed_work_on(cpu, vmstat_work, HZ + cpu);
}

/*
 * Use the cpu notifier to insure that the thresholds are recalculated
 * when necessary.
 */
static int __cpuinit vmstat_cpuup_callback(struct notifier_block *nfb,
		unsigned long action,
		void *hcpu)
{
	long cpu = (long)hcpu;

	switch (action) {
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
		start_cpu_timer(cpu);
		break;
	case CPU_DOWN_PREPARE:
	case CPU_DOWN_PREPARE_FROZEN:
		cancel_rearming_delayed_work(&per_cpu(vmstat_work, cpu));
		per_cpu(vmstat_work, cpu).work.func = NULL;
		break;
	case CPU_DOWN_FAILED:
	case CPU_DOWN_FAILED_FROZEN:
		start_cpu_timer(cpu);
		break;
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		refresh_zone_stat_thresholds();
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block __cpuinitdata vmstat_notifier =
	{ &vmstat_cpuup_callback, NULL, 0 };

int __init setup_vmstat(void)
{
	int cpu;

	refresh_zone_stat_thresholds();
	register_cpu_notifier(&vmstat_notifier);

	for_each_online_cpu(cpu)
		start_cpu_timer(cpu);
	return 0;
}
module_init(setup_vmstat)
#endif
