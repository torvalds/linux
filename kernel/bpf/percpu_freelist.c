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

		raw_spin_lock_init(&head->lock);
		head->first = NULL;
	}
	raw_spin_lock_init(&s->extralist.lock);
	s->extralist.first = NULL;
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
	head->first = node;
}

static inline void ___pcpu_freelist_push(struct pcpu_freelist_head *head,
					 struct pcpu_freelist_node *node)
{
	raw_spin_lock(&head->lock);
	pcpu_freelist_push_node(head, node);
	raw_spin_unlock(&head->lock);
}

static inline bool pcpu_freelist_try_push_extra(struct pcpu_freelist *s,
						struct pcpu_freelist_node *node)
{
	if (!raw_spin_trylock(&s->extralist.lock))
		return false;

	pcpu_freelist_push_node(&s->extralist, node);
	raw_spin_unlock(&s->extralist.lock);
	return true;
}

static inline void ___pcpu_freelist_push_nmi(struct pcpu_freelist *s,
					     struct pcpu_freelist_node *node)
{
	int cpu, orig_cpu;

	orig_cpu = cpu = raw_smp_processor_id();
	while (1) {
		struct pcpu_freelist_head *head;

		head = per_cpu_ptr(s->freelist, cpu);
		if (raw_spin_trylock(&head->lock)) {
			pcpu_freelist_push_node(head, node);
			raw_spin_unlock(&head->lock);
			return;
		}
		cpu = cpumask_next(cpu, cpu_possible_mask);
		if (cpu >= nr_cpu_ids)
			cpu = 0;

		/* cannot lock any per cpu lock, try extralist */
		if (cpu == orig_cpu &&
		    pcpu_freelist_try_push_extra(s, node))
			return;
	}
}

void __pcpu_freelist_push(struct pcpu_freelist *s,
			struct pcpu_freelist_node *node)
{
	if (in_nmi())
		___pcpu_freelist_push_nmi(s, node);
	else
		___pcpu_freelist_push(this_cpu_ptr(s->freelist), node);
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
	struct pcpu_freelist_head *head;
	struct pcpu_freelist_node *node;
	int orig_cpu, cpu;

	orig_cpu = cpu = raw_smp_processor_id();
	while (1) {
		head = per_cpu_ptr(s->freelist, cpu);
		raw_spin_lock(&head->lock);
		node = head->first;
		if (node) {
			head->first = node->next;
			raw_spin_unlock(&head->lock);
			return node;
		}
		raw_spin_unlock(&head->lock);
		cpu = cpumask_next(cpu, cpu_possible_mask);
		if (cpu >= nr_cpu_ids)
			cpu = 0;
		if (cpu == orig_cpu)
			break;
	}

	/* per cpu lists are all empty, try extralist */
	raw_spin_lock(&s->extralist.lock);
	node = s->extralist.first;
	if (node)
		s->extralist.first = node->next;
	raw_spin_unlock(&s->extralist.lock);
	return node;
}

static struct pcpu_freelist_node *
___pcpu_freelist_pop_nmi(struct pcpu_freelist *s)
{
	struct pcpu_freelist_head *head;
	struct pcpu_freelist_node *node;
	int orig_cpu, cpu;

	orig_cpu = cpu = raw_smp_processor_id();
	while (1) {
		head = per_cpu_ptr(s->freelist, cpu);
		if (raw_spin_trylock(&head->lock)) {
			node = head->first;
			if (node) {
				head->first = node->next;
				raw_spin_unlock(&head->lock);
				return node;
			}
			raw_spin_unlock(&head->lock);
		}
		cpu = cpumask_next(cpu, cpu_possible_mask);
		if (cpu >= nr_cpu_ids)
			cpu = 0;
		if (cpu == orig_cpu)
			break;
	}

	/* cannot pop from per cpu lists, try extralist */
	if (!raw_spin_trylock(&s->extralist.lock))
		return NULL;
	node = s->extralist.first;
	if (node)
		s->extralist.first = node->next;
	raw_spin_unlock(&s->extralist.lock);
	return node;
}

struct pcpu_freelist_node *__pcpu_freelist_pop(struct pcpu_freelist *s)
{
	if (in_nmi())
		return ___pcpu_freelist_pop_nmi(s);
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
