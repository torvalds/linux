// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Gao Xiang <xiang@kernel.org>
 *
 * For low-latency decompression algorithms (e.g. lz4), reserve consecutive
 * per-CPU virtual memory (in pages) in advance to store such inplace I/O
 * data if inplace decompression is failed (due to unmet inplace margin for
 * example).
 */
#include "internal.h"

struct erofs_pcpubuf {
	raw_spinlock_t lock;
	void *ptr;
	struct page **pages;
	unsigned int nrpages;
};

static DEFINE_PER_CPU(struct erofs_pcpubuf, erofs_pcb);

void *erofs_get_pcpubuf(unsigned int requiredpages)
	__acquires(pcb->lock)
{
	struct erofs_pcpubuf *pcb = &get_cpu_var(erofs_pcb);

	raw_spin_lock(&pcb->lock);
	/* check if the per-CPU buffer is too small */
	if (requiredpages > pcb->nrpages) {
		raw_spin_unlock(&pcb->lock);
		put_cpu_var(erofs_pcb);
		/* (for sparse checker) pretend pcb->lock is still taken */
		__acquire(pcb->lock);
		return NULL;
	}
	return pcb->ptr;
}

void erofs_put_pcpubuf(void *ptr) __releases(pcb->lock)
{
	struct erofs_pcpubuf *pcb = &per_cpu(erofs_pcb, smp_processor_id());

	DBG_BUGON(pcb->ptr != ptr);
	raw_spin_unlock(&pcb->lock);
	put_cpu_var(erofs_pcb);
}

/* the next step: support per-CPU page buffers hotplug */
int erofs_pcpubuf_growsize(unsigned int nrpages)
{
	static DEFINE_MUTEX(pcb_resize_mutex);
	static unsigned int pcb_nrpages;
	struct page *pagepool = NULL;
	int delta, cpu, ret, i;

	mutex_lock(&pcb_resize_mutex);
	delta = nrpages - pcb_nrpages;
	ret = 0;
	/* avoid shrinking pcpubuf, since no idea how many fses rely on */
	if (delta <= 0)
		goto out;

	for_each_possible_cpu(cpu) {
		struct erofs_pcpubuf *pcb = &per_cpu(erofs_pcb, cpu);
		struct page **pages, **oldpages;
		void *ptr, *old_ptr;

		pages = kmalloc_array(nrpages, sizeof(*pages), GFP_KERNEL);
		if (!pages) {
			ret = -ENOMEM;
			break;
		}

		for (i = 0; i < nrpages; ++i) {
			pages[i] = erofs_allocpage(&pagepool, GFP_KERNEL);
			if (!pages[i]) {
				ret = -ENOMEM;
				oldpages = pages;
				goto free_pagearray;
			}
		}
		ptr = vmap(pages, nrpages, VM_MAP, PAGE_KERNEL);
		if (!ptr) {
			ret = -ENOMEM;
			oldpages = pages;
			goto free_pagearray;
		}
		raw_spin_lock(&pcb->lock);
		old_ptr = pcb->ptr;
		pcb->ptr = ptr;
		oldpages = pcb->pages;
		pcb->pages = pages;
		i = pcb->nrpages;
		pcb->nrpages = nrpages;
		raw_spin_unlock(&pcb->lock);

		if (!oldpages) {
			DBG_BUGON(old_ptr);
			continue;
		}

		if (old_ptr)
			vunmap(old_ptr);
free_pagearray:
		while (i)
			erofs_pagepool_add(&pagepool, oldpages[--i]);
		kfree(oldpages);
		if (ret)
			break;
	}
	pcb_nrpages = nrpages;
	erofs_release_pages(&pagepool);
out:
	mutex_unlock(&pcb_resize_mutex);
	return ret;
}

void erofs_pcpubuf_init(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		struct erofs_pcpubuf *pcb = &per_cpu(erofs_pcb, cpu);

		raw_spin_lock_init(&pcb->lock);
	}
}

void erofs_pcpubuf_exit(void)
{
	int cpu, i;

	for_each_possible_cpu(cpu) {
		struct erofs_pcpubuf *pcb = &per_cpu(erofs_pcb, cpu);

		if (pcb->ptr) {
			vunmap(pcb->ptr);
			pcb->ptr = NULL;
		}
		if (!pcb->pages)
			continue;

		for (i = 0; i < pcb->nrpages; ++i)
			if (pcb->pages[i])
				put_page(pcb->pages[i]);
		kfree(pcb->pages);
		pcb->pages = NULL;
	}
}
