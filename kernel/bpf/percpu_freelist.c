// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2016 Facebook
 */
#include "percpu_freelist.h"

int pcpu_freelist_init(struct pcpu_freelist *s)
{
	int cpu;

	s->freelist = alloc_percpu(struct pcpu_freelist_head);
	if (!s->freelist)
		return -ENOMEM;

	for_each_possible_cpu(cpu) {
		struct pcpu_freelist_head *head = per_cpu_ptr(s->freelist, cpu);

		raw_res_spin_lock_init(&head->lock);
		head->first = NULL;
	}
	return 0;
}

void pcpu_freelist_destroy(struct pcpu_freelist *s)
{
	free_percpu(s->freelist);
}

static inline void pcpu_freelist_push_node(struct pcpu_freelist_head *head,
					   struct pcpu_freelist_node *node)
{
	node->next = head->first;
	WRITE_ONCE(head->first, node);
}

static inline bool ___pcpu_freelist_push(struct pcpu_freelist_head *head,
					 struct pcpu_freelist_node *node)
{
	if (raw_res_spin_lock(&head->lock))
		return false;
	pcpu_freelist_push_node(head, node);
	raw_res_spin_unlock(&head->lock);
	return true;
}

void __pcpu_freelist_push(struct pcpu_freelist *s,
			struct pcpu_freelist_node *node)
{
	struct pcpu_freelist_head *head;
	int cpu;

	if (___pcpu_freelist_push(this_cpu_ptr(s->freelist), node))
		return;

	while (true) {
		for_each_cpu_wrap(cpu, cpu_possible_mask, raw_smp_processor_id()) {
			if (cpu == raw_smp_processor_id())
				continue;
			head = per_cpu_ptr(s->freelist, cpu);
			if (raw_res_spin_lock(&head->lock))
				continue;
			pcpu_freelist_push_node(head, node);
			raw_res_spin_unlock(&head->lock);
			return;
		}
	}
}

void pcpu_freelist_push(struct pcpu_freelist *s,
			struct pcpu_freelist_node *node)
{
	unsigned long flags;

	local_irq_save(flags);
	__pcpu_freelist_push(s, node);
	local_irq_restore(flags);
}

void pcpu_freelist_populate(struct pcpu_freelist *s, void *buf, u32 elem_size,
			    u32 nr_elems)
{
	struct pcpu_freelist_head *head;
	unsigned int cpu, cpu_idx, i, j, n, m;

	n = nr_elems / num_possible_cpus();
	m = nr_elems % num_possible_cpus();

	cpu_idx = 0;
	for_each_possible_cpu(cpu) {
		head = per_cpu_ptr(s->freelist, cpu);
		j = n + (cpu_idx < m ? 1 : 0);
		for (i = 0; i < j; i++) {
			/* No locking required as this is not visible yet. */
			pcpu_freelist_push_node(head, buf);
			buf += elem_size;
		}
		cpu_idx++;
	}
}

static struct pcpu_freelist_node *___pcpu_freelist_pop(struct pcpu_freelist *s)
{
	struct pcpu_freelist_node *node = NULL;
	struct pcpu_freelist_head *head;
	int cpu;

	for_each_cpu_wrap(cpu, cpu_possible_mask, raw_smp_processor_id()) {
		head = per_cpu_ptr(s->freelist, cpu);
		if (!READ_ONCE(head->first))
			continue;
		if (raw_res_spin_lock(&head->lock))
			continue;
		node = head->first;
		if (node) {
			WRITE_ONCE(head->first, node->next);
			raw_res_spin_unlock(&head->lock);
			return node;
		}
		raw_res_spin_unlock(&head->lock);
	}
	return node;
}

struct pcpu_freelist_node *__pcpu_freelist_pop(struct pcpu_freelist *s)
{
	return ___pcpu_freelist_pop(s);
}

struct pcpu_freelist_node *pcpu_freelist_pop(struct pcpu_freelist *s)
{
	struct pcpu_freelist_node *ret;
	unsigned long flags;

	local_irq_save(flags);
	ret = __pcpu_freelist_pop(s);
	local_irq_restore(flags);
	return ret;
}
