/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PART_STAT_H
#define _LINUX_PART_STAT_H

#include <linux/genhd.h>

/*
 * Macros to operate on percpu disk statistics:
 *
 * {disk|part|all}_stat_{add|sub|inc|dec}() modify the stat counters
 * and should be called between disk_stat_lock() and
 * disk_stat_unlock().
 *
 * part_stat_read() can be called at any time.
 *
 * part_stat_{add|set_all}() and {init|free}_part_stats are for
 * internal use only.
 */
#ifdef	CONFIG_SMP
#define part_stat_lock()	({ rcu_read_lock(); get_cpu(); })
#define part_stat_unlock()	do { put_cpu(); rcu_read_unlock(); } while (0)

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

static inline int init_part_stats(struct hd_struct *part)
{
	part->dkstats = alloc_percpu(struct disk_stats);
	if (!part->dkstats)
		return 0;
	return 1;
}

static inline void free_part_stats(struct hd_struct *part)
{
	free_percpu(part->dkstats);
}

#else /* !CONFIG_SMP */
#define part_stat_lock()	({ rcu_read_lock(); 0; })
#define part_stat_unlock()	rcu_read_unlock()

#define part_stat_get(part, field)		((part)->dkstats.field)
#define part_stat_get_cpu(part, field, cpu)	part_stat_get(part, field)
#define part_stat_read(part, field)		part_stat_get(part, field)

static inline void part_stat_set_all(struct hd_struct *part, int value)
{
	memset(&part->dkstats, value, sizeof(struct disk_stats));
}

static inline int init_part_stats(struct hd_struct *part)
{
	return 1;
}

static inline void free_part_stats(struct hd_struct *part)
{
}

#endif /* CONFIG_SMP */

#define part_stat_read_accum(part, field)				\
	(part_stat_read(part, field[STAT_READ]) +			\
	 part_stat_read(part, field[STAT_WRITE]) +			\
	 part_stat_read(part, field[STAT_DISCARD]))

#define __part_stat_add(part, field, addnd)				\
	(part_stat_get(part, field) += (addnd))

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
