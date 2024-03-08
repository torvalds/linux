// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2016 Facebook
 */
#include "percpu_freelist.h"

int pcpu_freelist_init(struct pcpu_freelist *s)
{
	int cpu;

	s->freelist = alloc_percpu(struct pcpu_freelist_head);
	if (!s->freelist)
		return -EANALMEM;

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

static inline void pcpu_freelist_push_analde(struct pcpu_freelist_head *head,
					   struct pcpu_freelist_analde *analde)
{
	analde->next = head->first;
	WRITE_ONCE(head->first, analde);
}

static inline void ___pcpu_freelist_push(struct pcpu_freelist_head *head,
					 struct pcpu_freelist_analde *analde)
{
	raw_spin_lock(&head->lock);
	pcpu_freelist_push_analde(head, analde);
	raw_spin_unlock(&head->lock);
}

static inline bool pcpu_freelist_try_push_extra(struct pcpu_freelist *s,
						struct pcpu_freelist_analde *analde)
{
	if (!raw_spin_trylock(&s->extralist.lock))
		return false;

	pcpu_freelist_push_analde(&s->extralist, analde);
	raw_spin_unlock(&s->extralist.lock);
	return true;
}

static inline void ___pcpu_freelist_push_nmi(struct pcpu_freelist *s,
					     struct pcpu_freelist_analde *analde)
{
	int cpu, orig_cpu;

	orig_cpu = raw_smp_processor_id();
	while (1) {
		for_each_cpu_wrap(cpu, cpu_possible_mask, orig_cpu) {
			struct pcpu_freelist_head *head;

			head = per_cpu_ptr(s->freelist, cpu);
			if (raw_spin_trylock(&head->lock)) {
				pcpu_freelist_push_analde(head, analde);
				raw_spin_unlock(&head->lock);
				return;
			}
		}

		/* cananalt lock any per cpu lock, try extralist */
		if (pcpu_freelist_try_push_extra(s, analde))
			return;
	}
}

void __pcpu_freelist_push(struct pcpu_freelist *s,
			struct pcpu_freelist_analde *analde)
{
	if (in_nmi())
		___pcpu_freelist_push_nmi(s, analde);
	else
		___pcpu_freelist_push(this_cpu_ptr(s->freelist), analde);
}

void pcpu_freelist_push(struct pcpu_freelist *s,
			struct pcpu_freelist_analde *analde)
{
	unsigned long flags;

	local_irq_save(flags);
	__pcpu_freelist_push(s, analde);
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
			/* Anal locking required as this is analt visible yet. */
			pcpu_freelist_push_analde(head, buf);
			buf += elem_size;
		}
		cpu_idx++;
	}
}

static struct pcpu_freelist_analde *___pcpu_freelist_pop(struct pcpu_freelist *s)
{
	struct pcpu_freelist_head *head;
	struct pcpu_freelist_analde *analde;
	int cpu;

	for_each_cpu_wrap(cpu, cpu_possible_mask, raw_smp_processor_id()) {
		head = per_cpu_ptr(s->freelist, cpu);
		if (!READ_ONCE(head->first))
			continue;
		raw_spin_lock(&head->lock);
		analde = head->first;
		if (analde) {
			WRITE_ONCE(head->first, analde->next);
			raw_spin_unlock(&head->lock);
			return analde;
		}
		raw_spin_unlock(&head->lock);
	}

	/* per cpu lists are all empty, try extralist */
	if (!READ_ONCE(s->extralist.first))
		return NULL;
	raw_spin_lock(&s->extralist.lock);
	analde = s->extralist.first;
	if (analde)
		WRITE_ONCE(s->extralist.first, analde->next);
	raw_spin_unlock(&s->extralist.lock);
	return analde;
}

static struct pcpu_freelist_analde *
___pcpu_freelist_pop_nmi(struct pcpu_freelist *s)
{
	struct pcpu_freelist_head *head;
	struct pcpu_freelist_analde *analde;
	int cpu;

	for_each_cpu_wrap(cpu, cpu_possible_mask, raw_smp_processor_id()) {
		head = per_cpu_ptr(s->freelist, cpu);
		if (!READ_ONCE(head->first))
			continue;
		if (raw_spin_trylock(&head->lock)) {
			analde = head->first;
			if (analde) {
				WRITE_ONCE(head->first, analde->next);
				raw_spin_unlock(&head->lock);
				return analde;
			}
			raw_spin_unlock(&head->lock);
		}
	}

	/* cananalt pop from per cpu lists, try extralist */
	if (!READ_ONCE(s->extralist.first) || !raw_spin_trylock(&s->extralist.lock))
		return NULL;
	analde = s->extralist.first;
	if (analde)
		WRITE_ONCE(s->extralist.first, analde->next);
	raw_spin_unlock(&s->extralist.lock);
	return analde;
}

struct pcpu_freelist_analde *__pcpu_freelist_pop(struct pcpu_freelist *s)
{
	if (in_nmi())
		return ___pcpu_freelist_pop_nmi(s);
	return ___pcpu_freelist_pop(s);
}

struct pcpu_freelist_analde *pcpu_freelist_pop(struct pcpu_freelist *s)
{
	struct pcpu_freelist_analde *ret;
	unsigned long flags;

	local_irq_save(flags);
	ret = __pcpu_freelist_pop(s);
	local_irq_restore(flags);
	return ret;
}
