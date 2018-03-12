/*
 * Copyright (C) 2017 Thomas Gleixner <tglx@linutronix.de>
 *
 * SPDX-License-Identifier: GPL-2.0
 */
#include <linux/spinlock.h>
#include <linux/seq_file.h>
#include <linux/bitmap.h>
#include <linux/percpu.h>
#include <linux/cpu.h>
#include <linux/irq.h>

#define IRQ_MATRIX_SIZE	(BITS_TO_LONGS(IRQ_MATRIX_BITS) * sizeof(unsigned long))

struct cpumap {
	unsigned int		available;
	unsigned int		allocated;
	unsigned int		managed;
	bool			initialized;
	bool			online;
	unsigned long		alloc_map[IRQ_MATRIX_SIZE];
	unsigned long		managed_map[IRQ_MATRIX_SIZE];
};

struct irq_matrix {
	unsigned int		matrix_bits;
	unsigned int		alloc_start;
	unsigned int		alloc_end;
	unsigned int		alloc_size;
	unsigned int		global_available;
	unsigned int		global_reserved;
	unsigned int		systembits_inalloc;
	unsigned int		total_allocated;
	unsigned int		online_maps;
	struct cpumap __percpu	*maps;
	unsigned long		scratch_map[IRQ_MATRIX_SIZE];
	unsigned long		system_map[IRQ_MATRIX_SIZE];
};

#define CREATE_TRACE_POINTS
#include <trace/events/irq_matrix.h>

/**
 * irq_alloc_matrix - Allocate a irq_matrix structure and initialize it
 * @matrix_bits:	Number of matrix bits must be <= IRQ_MATRIX_BITS
 * @alloc_start:	From which bit the allocation search starts
 * @alloc_end:		At which bit the allocation search ends, i.e first
 *			invalid bit
 */
__init struct irq_matrix *irq_alloc_matrix(unsigned int matrix_bits,
					   unsigned int alloc_start,
					   unsigned int alloc_end)
{
	struct irq_matrix *m;

	if (matrix_bits > IRQ_MATRIX_BITS)
		return NULL;

	m = kzalloc(sizeof(*m), GFP_KERNEL);
	if (!m)
		return NULL;

	m->matrix_bits = matrix_bits;
	m->alloc_start = alloc_start;
	m->alloc_end = alloc_end;
	m->alloc_size = alloc_end - alloc_start;
	m->maps = alloc_percpu(*m->maps);
	if (!m->maps) {
		kfree(m);
		return NULL;
	}
	return m;
}

/**
 * irq_matrix_online - Bring the local CPU matrix online
 * @m:		Matrix pointer
 */
void irq_matrix_online(struct irq_matrix *m)
{
	struct cpumap *cm = this_cpu_ptr(m->maps);

	BUG_ON(cm->online);

	if (!cm->initialized) {
		cm->available = m->alloc_size;
		cm->available -= cm->managed + m->systembits_inalloc;
		cm->initialized = true;
	}
	m->global_available += cm->available;
	cm->online = true;
	m->online_maps++;
	trace_irq_matrix_online(m);
}

/**
 * irq_matrix_offline - Bring the local CPU matrix offline
 * @m:		Matrix pointer
 */
void irq_matrix_offline(struct irq_matrix *m)
{
	struct cpumap *cm = this_cpu_ptr(m->maps);

	/* Update the global available size */
	m->global_available -= cm->available;
	cm->online = false;
	m->online_maps--;
	trace_irq_matrix_offline(m);
}

static unsigned int matrix_alloc_area(struct irq_matrix *m, struct cpumap *cm,
				      unsigned int num, bool managed)
{
	unsigned int area, start = m->alloc_start;
	unsigned int end = m->alloc_end;

	bitmap_or(m->scratch_map, cm->managed_map, m->system_map, end);
	bitmap_or(m->scratch_map, m->scratch_map, cm->alloc_map, end);
	area = bitmap_find_next_zero_area(m->scratch_map, end, start, num, 0);
	if (area >= end)
		return area;
	if (managed)
		bitmap_set(cm->managed_map, area, num);
	else
		bitmap_set(cm->alloc_map, area, num);
	return area;
}

/**
 * irq_matrix_assign_system - Assign system wide entry in the matrix
 * @m:		Matrix pointer
 * @bit:	Which bit to reserve
 * @replace:	Replace an already allocated vector with a system
 *		vector at the same bit position.
 *
 * The BUG_ON()s below are on purpose. If this goes wrong in the
 * early boot process, then the chance to survive is about zero.
 * If this happens when the system is life, it's not much better.
 */
void irq_matrix_assign_system(struct irq_matrix *m, unsigned int bit,
			      bool replace)
{
	struct cpumap *cm = this_cpu_ptr(m->maps);

	BUG_ON(bit > m->matrix_bits);
	BUG_ON(m->online_maps > 1 || (m->online_maps && !replace));

	set_bit(bit, m->system_map);
	if (replace) {
		BUG_ON(!test_and_clear_bit(bit, cm->alloc_map));
		cm->allocated--;
		m->total_allocated--;
	}
	if (bit >= m->alloc_start && bit < m->alloc_end)
		m->systembits_inalloc++;

	trace_irq_matrix_assign_system(bit, m);
}

/**
 * irq_matrix_reserve_managed - Reserve a managed interrupt in a CPU map
 * @m:		Matrix pointer
 * @msk:	On which CPUs the bits should be reserved.
 *
 * Can be called for offline CPUs. Note, this will only reserve one bit
 * on all CPUs in @msk, but it's not guaranteed that the bits are at the
 * same offset on all CPUs
 */
int irq_matrix_reserve_managed(struct irq_matrix *m, const struct cpumask *msk)
{
	unsigned int cpu, failed_cpu;

	for_each_cpu(cpu, msk) {
		struct cpumap *cm = per_cpu_ptr(m->maps, cpu);
		unsigned int bit;

		bit = matrix_alloc_area(m, cm, 1, true);
		if (bit >= m->alloc_end)
			goto cleanup;
		cm->managed++;
		if (cm->online) {
			cm->available--;
			m->global_available--;
		}
		trace_irq_matrix_reserve_managed(bit, cpu, m, cm);
	}
	return 0;
cleanup:
	failed_cpu = cpu;
	for_each_cpu(cpu, msk) {
		if (cpu == failed_cpu)
			break;
		irq_matrix_remove_managed(m, cpumask_of(cpu));
	}
	return -ENOSPC;
}

/**
 * irq_matrix_remove_managed - Remove managed interrupts in a CPU map
 * @m:		Matrix pointer
 * @msk:	On which CPUs the bits should be removed
 *
 * Can be called for offline CPUs
 *
 * This removes not allocated managed interrupts from the map. It does
 * not matter which one because the managed interrupts free their
 * allocation when they shut down. If not, the accounting is screwed,
 * but all what can be done at this point is warn about it.
 */
void irq_matrix_remove_managed(struct irq_matrix *m, const struct cpumask *msk)
{
	unsigned int cpu;

	for_each_cpu(cpu, msk) {
		struct cpumap *cm = per_cpu_ptr(m->maps, cpu);
		unsigned int bit, end = m->alloc_end;

		if (WARN_ON_ONCE(!cm->managed))
			continue;

		/* Get managed bit which are not allocated */
		bitmap_andnot(m->scratch_map, cm->managed_map, cm->alloc_map, end);

		bit = find_first_bit(m->scratch_map, end);
		if (WARN_ON_ONCE(bit >= end))
			continue;

		clear_bit(bit, cm->managed_map);

		cm->managed--;
		if (cm->online) {
			cm->available++;
			m->global_available++;
		}
		trace_irq_matrix_remove_managed(bit, cpu, m, cm);
	}
}

/**
 * irq_matrix_alloc_managed - Allocate a managed interrupt in a CPU map
 * @m:		Matrix pointer
 * @cpu:	On which CPU the interrupt should be allocated
 */
int irq_matrix_alloc_managed(struct irq_matrix *m, unsigned int cpu)
{
	struct cpumap *cm = per_cpu_ptr(m->maps, cpu);
	unsigned int bit, end = m->alloc_end;

	/* Get managed bit which are not allocated */
	bitmap_andnot(m->scratch_map, cm->managed_map, cm->alloc_map, end);
	bit = find_first_bit(m->scratch_map, end);
	if (bit >= end)
		return -ENOSPC;
	set_bit(bit, cm->alloc_map);
	cm->allocated++;
	m->total_allocated++;
	trace_irq_matrix_alloc_managed(bit, cpu, m, cm);
	return bit;
}

/**
 * irq_matrix_assign - Assign a preallocated interrupt in the local CPU map
 * @m:		Matrix pointer
 * @bit:	Which bit to mark
 *
 * This should only be used to mark preallocated vectors
 */
void irq_matrix_assign(struct irq_matrix *m, unsigned int bit)
{
	struct cpumap *cm = this_cpu_ptr(m->maps);

	if (WARN_ON_ONCE(bit < m->alloc_start || bit >= m->alloc_end))
		return;
	if (WARN_ON_ONCE(test_and_set_bit(bit, cm->alloc_map)))
		return;
	cm->allocated++;
	m->total_allocated++;
	cm->available--;
	m->global_available--;
	trace_irq_matrix_assign(bit, smp_processor_id(), m, cm);
}

/**
 * irq_matrix_reserve - Reserve interrupts
 * @m:		Matrix pointer
 *
 * This is merily a book keeping call. It increments the number of globally
 * reserved interrupt bits w/o actually allocating them. This allows to
 * setup interrupt descriptors w/o assigning low level resources to it.
 * The actual allocation happens when the interrupt gets activated.
 */
void irq_matrix_reserve(struct irq_matrix *m)
{
	if (m->global_reserved <= m->global_available &&
	    m->global_reserved + 1 > m->global_available)
		pr_warn("Interrupt reservation exceeds available resources\n");

	m->global_reserved++;
	trace_irq_matrix_reserve(m);
}

/**
 * irq_matrix_remove_reserved - Remove interrupt reservation
 * @m:		Matrix pointer
 *
 * This is merily a book keeping call. It decrements the number of globally
 * reserved interrupt bits. This is used to undo irq_matrix_reserve() when the
 * interrupt was never in use and a real vector allocated, which undid the
 * reservation.
 */
void irq_matrix_remove_reserved(struct irq_matrix *m)
{
	m->global_reserved--;
	trace_irq_matrix_remove_reserved(m);
}

/**
 * irq_matrix_alloc - Allocate a regular interrupt in a CPU map
 * @m:		Matrix pointer
 * @msk:	Which CPUs to search in
 * @reserved:	Allocate previously reserved interrupts
 * @mapped_cpu: Pointer to store the CPU for which the irq was allocated
 */
int irq_matrix_alloc(struct irq_matrix *m, const struct cpumask *msk,
		     bool reserved, unsigned int *mapped_cpu)
{
	unsigned int cpu, best_cpu, maxavl = 0;
	struct cpumap *cm;
	unsigned int bit;

	best_cpu = UINT_MAX;
	for_each_cpu(cpu, msk) {
		cm = per_cpu_ptr(m->maps, cpu);

		if (!cm->online || cm->available <= maxavl)
			continue;

		best_cpu = cpu;
		maxavl = cm->available;
	}

	if (maxavl) {
		cm = per_cpu_ptr(m->maps, best_cpu);
		bit = matrix_alloc_area(m, cm, 1, false);
		if (bit < m->alloc_end) {
			cm->allocated++;
			cm->available--;
			m->total_allocated++;
			m->global_available--;
			if (reserved)
				m->global_reserved--;
			*mapped_cpu = best_cpu;
			trace_irq_matrix_alloc(bit, best_cpu, m, cm);
			return bit;
		}
	}
	return -ENOSPC;
}

/**
 * irq_matrix_free - Free allocated interrupt in the matrix
 * @m:		Matrix pointer
 * @cpu:	Which CPU map needs be updated
 * @bit:	The bit to remove
 * @managed:	If true, the interrupt is managed and not accounted
 *		as available.
 */
void irq_matrix_free(struct irq_matrix *m, unsigned int cpu,
		     unsigned int bit, bool managed)
{
	struct cpumap *cm = per_cpu_ptr(m->maps, cpu);

	if (WARN_ON_ONCE(bit < m->alloc_start || bit >= m->alloc_end))
		return;

	clear_bit(bit, cm->alloc_map);
	cm->allocated--;

	if (cm->online)
		m->total_allocated--;

	if (!managed) {
		cm->available++;
		if (cm->online)
			m->global_available++;
	}
	trace_irq_matrix_free(bit, cpu, m, cm);
}

/**
 * irq_matrix_available - Get the number of globally available irqs
 * @m:		Pointer to the matrix to query
 * @cpudown:	If true, the local CPU is about to go down, adjust
 *		the number of available irqs accordingly
 */
unsigned int irq_matrix_available(struct irq_matrix *m, bool cpudown)
{
	struct cpumap *cm = this_cpu_ptr(m->maps);

	if (!cpudown)
		return m->global_available;
	return m->global_available - cm->available;
}

/**
 * irq_matrix_reserved - Get the number of globally reserved irqs
 * @m:		Pointer to the matrix to query
 */
unsigned int irq_matrix_reserved(struct irq_matrix *m)
{
	return m->global_reserved;
}

/**
 * irq_matrix_allocated - Get the number of allocated irqs on the local cpu
 * @m:		Pointer to the matrix to search
 *
 * This returns number of allocated irqs
 */
unsigned int irq_matrix_allocated(struct irq_matrix *m)
{
	struct cpumap *cm = this_cpu_ptr(m->maps);

	return cm->allocated;
}

#ifdef CONFIG_GENERIC_IRQ_DEBUGFS
/**
 * irq_matrix_debug_show - Show detailed allocation information
 * @sf:		Pointer to the seq_file to print to
 * @m:		Pointer to the matrix allocator
 * @ind:	Indentation for the print format
 *
 * Note, this is a lockless snapshot.
 */
void irq_matrix_debug_show(struct seq_file *sf, struct irq_matrix *m, int ind)
{
	unsigned int nsys = bitmap_weight(m->system_map, m->matrix_bits);
	int cpu;

	seq_printf(sf, "Online bitmaps:   %6u\n", m->online_maps);
	seq_printf(sf, "Global available: %6u\n", m->global_available);
	seq_printf(sf, "Global reserved:  %6u\n", m->global_reserved);
	seq_printf(sf, "Total allocated:  %6u\n", m->total_allocated);
	seq_printf(sf, "System: %u: %*pbl\n", nsys, m->matrix_bits,
		   m->system_map);
	seq_printf(sf, "%*s| CPU | avl | man | act | vectors\n", ind, " ");
	cpus_read_lock();
	for_each_online_cpu(cpu) {
		struct cpumap *cm = per_cpu_ptr(m->maps, cpu);

		seq_printf(sf, "%*s %4d  %4u  %4u  %4u  %*pbl\n", ind, " ",
			   cpu, cm->available, cm->managed, cm->allocated,
			   m->matrix_bits, cm->alloc_map);
	}
	cpus_read_unlock();
}
#endif
