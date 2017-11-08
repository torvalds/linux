/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_QUICKLIST_H
#define LINUX_QUICKLIST_H
/*
 * Fast allocations and disposal of pages. Pages must be in the condition
 * as needed after allocation when they are freed. Per cpu lists of pages
 * are kept that only contain node local pages.
 *
 * (C) 2007, SGI. Christoph Lameter <cl@linux.com>
 */
#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/percpu.h>

#ifdef CONFIG_QUICKLIST

struct quicklist {
	void *page;
	int nr_pages;
};

DECLARE_PER_CPU(struct quicklist, quicklist)[CONFIG_NR_QUICK];

/*
 * The two key functions quicklist_alloc and quicklist_free are inline so
 * that they may be custom compiled for the platform.
 * Specifying a NULL ctor can remove constructor support. Specifying
 * a constant quicklist allows the determination of the exact address
 * in the per cpu area.
 *
 * The fast patch in quicklist_alloc touched only a per cpu cacheline and
 * the first cacheline of the page itself. There is minmal overhead involved.
 */
static inline void *quicklist_alloc(int nr, gfp_t flags, void (*ctor)(void *))
{
	struct quicklist *q;
	void **p = NULL;

	q =&get_cpu_var(quicklist)[nr];
	p = q->page;
	if (likely(p)) {
		q->page = p[0];
		p[0] = NULL;
		q->nr_pages--;
	}
	put_cpu_var(quicklist);
	if (likely(p))
		return p;

	p = (void *)__get_free_page(flags | __GFP_ZERO);
	if (ctor && p)
		ctor(p);
	return p;
}

static inline void __quicklist_free(int nr, void (*dtor)(void *), void *p,
	struct page *page)
{
	struct quicklist *q;

	q = &get_cpu_var(quicklist)[nr];
	*(void **)p = q->page;
	q->page = p;
	q->nr_pages++;
	put_cpu_var(quicklist);
}

static inline void quicklist_free(int nr, void (*dtor)(void *), void *pp)
{
	__quicklist_free(nr, dtor, pp, virt_to_page(pp));
}

static inline void quicklist_free_page(int nr, void (*dtor)(void *),
							struct page *page)
{
	__quicklist_free(nr, dtor, page_address(page), page);
}

void quicklist_trim(int nr, void (*dtor)(void *),
	unsigned long min_pages, unsigned long max_free);

unsigned long quicklist_total_size(void);

#else

static inline unsigned long quicklist_total_size(void)
{
	return 0;
}

#endif

#endif /* LINUX_QUICKLIST_H */

