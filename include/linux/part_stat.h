/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PART_STAT_H
#define _LINUX_PART_STAT_H

#include <linux/genhd.h>

struct disk_stats {
	u64 nsecs[NR_STAT_GROUPS];
	unsigned long sectors[NR_STAT_GROUPS];
	unsigned long ios[NR_STAT_GROUPS];
	unsigned long merges[NR_STAT_GROUPS];
	unsigned long io_ticks;
	local_t in_flight[2];
};

/*
 * Macros to operate on percpu disk statistics:
 *
 * {disk|part|all}_stat_{add|sub|inc|dec}() modify the stat counters and should
 * be called between disk_stat_lock() and disk_stat_unlock().
 *
 * part_stat_read() can be called at any time.
 */
#define part_stat_lock()	preempt_disable()
#define part_stat_unlock()	preempt_enable()

#define part_stat_get_cpu(part, field, cpu)				\
	(per_cpu_ptr((part)->dkstats, (cpu))->field)

#define part_stat_get(part, field)					\
	part_stat_get_cpu(part, field, smp_processor_id())

#define part_stat_read(part, field)					\
({									\
	typeof((part)->dkstats->field) res = 0;				\
	unsigned int _cpu;						\
	for_each_possible_cpu(_cpu)					\
		res += per_cpu_ptr((part)->dkstats, _cpu)->field;	\
	res;								\
})

static inline void part_stat_set_all(struct hd_struct *part, int value)
{
	int i;

	for_each_possible_cpu(i)
		memset(per_cpu_ptr(part->dkstats, i), value,
				sizeof(struct disk_stats));
}

#define part_stat_read_accum(part, field)				\
	(part_stat_read(part, field[STAT_READ]) +			\
	 part_stat_read(part, field[STAT_WRITE]) +			\
	 part_stat_read(part, field[STAT_DISCARD]))

#define __part_stat_add(part, field, addnd)				\
	__this_cpu_add((part)->dkstats->field, addnd)

#define part_stat_add(part, field, addnd)	do {			\
	__part_stat_add((part), field, addnd);				\
	if ((part)->partno)						\
		__part_stat_add(&part_to_disk((part))->part0,		\
				field, addnd);				\
} while (0)

#define part_stat_dec(gendiskp, field)					\
	part_stat_add(gendiskp, field, -1)
#define part_stat_inc(gendiskp, field)					\
	part_stat_add(gendiskp, field, 1)
#define part_stat_sub(gendiskp, field, subnd)				\
	part_stat_add(gendiskp, field, -subnd)

#define part_stat_local_dec(gendiskp, field)				\
	local_dec(&(part_stat_get(gendiskp, field)))
#define part_stat_local_inc(gendiskp, field)				\
	local_inc(&(part_stat_get(gendiskp, field)))
#define part_stat_local_read(gendiskp, field)				\
	local_read(&(part_stat_get(gendiskp, field)))
#define part_stat_local_read_cpu(gendiskp, field, cpu)			\
	local_read(&(part_stat_get_cpu(gendiskp, field, cpu)))

#endif /* _LINUX_PART_STAT_H */
