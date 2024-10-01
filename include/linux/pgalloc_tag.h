/* SPDX-License-Identifier: GPL-2.0 */
/*
 * page allocation tagging
 */
#ifndef _LINUX_PGALLOC_TAG_H
#define _LINUX_PGALLOC_TAG_H

#include <linux/alloc_tag.h>

#ifdef CONFIG_MEM_ALLOC_PROFILING

#include <linux/page_ext.h>

extern struct page_ext_operations page_alloc_tagging_ops;

static inline union codetag_ref *codetag_ref_from_page_ext(struct page_ext *page_ext)
{
	return (union codetag_ref *)page_ext_data(page_ext, &page_alloc_tagging_ops);
}

static inline struct page_ext *page_ext_from_codetag_ref(union codetag_ref *ref)
{
	return (void *)ref - page_alloc_tagging_ops.offset;
}

/* Should be called only if mem_alloc_profiling_enabled() */
static inline union codetag_ref *get_page_tag_ref(struct page *page)
{
	if (page) {
		struct page_ext *page_ext = page_ext_get(page);

		if (page_ext)
			return codetag_ref_from_page_ext(page_ext);
	}
	return NULL;
}

static inline void put_page_tag_ref(union codetag_ref *ref)
{
	if (WARN_ON(!ref))
		return;

	page_ext_put(page_ext_from_codetag_ref(ref));
}

static inline void clear_page_tag_ref(struct page *page)
{
	if (mem_alloc_profiling_enabled()) {
		union codetag_ref *ref = get_page_tag_ref(page);

		if (ref) {
			set_codetag_empty(ref);
			put_page_tag_ref(ref);
		}
	}
}

static inline void pgalloc_tag_add(struct page *page, struct task_struct *task,
				   unsigned int nr)
{
	if (mem_alloc_profiling_enabled()) {
		union codetag_ref *ref = get_page_tag_ref(page);

		if (ref) {
			alloc_tag_add(ref, task->alloc_tag, PAGE_SIZE * nr);
			put_page_tag_ref(ref);
		}
	}
}

static inline void pgalloc_tag_sub(struct page *page, unsigned int nr)
{
	if (mem_alloc_profiling_enabled()) {
		union codetag_ref *ref = get_page_tag_ref(page);

		if (ref) {
			alloc_tag_sub(ref, PAGE_SIZE * nr);
			put_page_tag_ref(ref);
		}
	}
}

static inline struct alloc_tag *pgalloc_tag_get(struct page *page)
{
	struct alloc_tag *tag = NULL;

	if (mem_alloc_profiling_enabled()) {
		union codetag_ref *ref = get_page_tag_ref(page);

		alloc_tag_sub_check(ref);
		if (ref) {
			if (ref->ct)
				tag = ct_to_alloc_tag(ref->ct);
			put_page_tag_ref(ref);
		}
	}

	return tag;
}

static inline void pgalloc_tag_sub_pages(struct alloc_tag *tag, unsigned int nr)
{
	if (mem_alloc_profiling_enabled() && tag)
		this_cpu_sub(tag->counters->bytes, PAGE_SIZE * nr);
}

#else /* CONFIG_MEM_ALLOC_PROFILING */

static inline union codetag_ref *get_page_tag_ref(struct page *page) { return NULL; }
static inline void put_page_tag_ref(union codetag_ref *ref) {}
static inline void clear_page_tag_ref(struct page *page) {}
static inline void pgalloc_tag_add(struct page *page, struct task_struct *task,
				   unsigned int nr) {}
static inline void pgalloc_tag_sub(struct page *page, unsigned int nr) {}
static inline struct alloc_tag *pgalloc_tag_get(struct page *page) { return NULL; }
static inline void pgalloc_tag_sub_pages(struct alloc_tag *tag, unsigned int nr) {}

#endif /* CONFIG_MEM_ALLOC_PROFILING */

#endif /* _LINUX_PGALLOC_TAG_H */
