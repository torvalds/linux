/* SPDX-License-Identifier: GPL-2.0 */
/*
 * page allocation tagging
 */
#ifndef _LINUX_PGALLOC_TAG_H
#define _LINUX_PGALLOC_TAG_H

#include <linux/alloc_tag.h>

#ifdef CONFIG_MEM_ALLOC_PROFILING

#include <linux/page_ext.h>

union pgtag_ref_handle {
	union codetag_ref *ref;	/* reference in page extension */
};

extern struct page_ext_operations page_alloc_tagging_ops;

/* Should be called only if mem_alloc_profiling_enabled() */
static inline bool get_page_tag_ref(struct page *page, union codetag_ref *ref,
				    union pgtag_ref_handle *handle)
{
	struct page_ext *page_ext;
	union codetag_ref *tmp;

	if (!page)
		return false;

	page_ext = page_ext_get(page);
	if (!page_ext)
		return false;

	tmp = (union codetag_ref *)page_ext_data(page_ext, &page_alloc_tagging_ops);
	ref->ct = tmp->ct;
	handle->ref = tmp;
	return true;
}

static inline void put_page_tag_ref(union pgtag_ref_handle handle)
{
	if (WARN_ON(!handle.ref))
		return;

	page_ext_put((void *)handle.ref - page_alloc_tagging_ops.offset);
}

static inline void update_page_tag_ref(union pgtag_ref_handle handle,
				       union codetag_ref *ref)
{
	if (WARN_ON(!handle.ref || !ref))
		return;

	handle.ref->ct = ref->ct;
}

static inline void clear_page_tag_ref(struct page *page)
{
	if (mem_alloc_profiling_enabled()) {
		union pgtag_ref_handle handle;
		union codetag_ref ref;

		if (get_page_tag_ref(page, &ref, &handle)) {
			set_codetag_empty(&ref);
			update_page_tag_ref(handle, &ref);
			put_page_tag_ref(handle);
		}
	}
}

static inline void pgalloc_tag_add(struct page *page, struct task_struct *task,
				   unsigned int nr)
{
	if (mem_alloc_profiling_enabled()) {
		union pgtag_ref_handle handle;
		union codetag_ref ref;

		if (get_page_tag_ref(page, &ref, &handle)) {
			alloc_tag_add(&ref, task->alloc_tag, PAGE_SIZE * nr);
			update_page_tag_ref(handle, &ref);
			put_page_tag_ref(handle);
		}
	}
}

static inline void pgalloc_tag_sub(struct page *page, unsigned int nr)
{
	if (mem_alloc_profiling_enabled()) {
		union pgtag_ref_handle handle;
		union codetag_ref ref;

		if (get_page_tag_ref(page, &ref, &handle)) {
			alloc_tag_sub(&ref, PAGE_SIZE * nr);
			update_page_tag_ref(handle, &ref);
			put_page_tag_ref(handle);
		}
	}
}

static inline struct alloc_tag *pgalloc_tag_get(struct page *page)
{
	struct alloc_tag *tag = NULL;

	if (mem_alloc_profiling_enabled()) {
		union pgtag_ref_handle handle;
		union codetag_ref ref;

		if (get_page_tag_ref(page, &ref, &handle)) {
			alloc_tag_sub_check(&ref);
			if (ref.ct)
				tag = ct_to_alloc_tag(ref.ct);
			put_page_tag_ref(handle);
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

static inline void clear_page_tag_ref(struct page *page) {}
static inline void pgalloc_tag_add(struct page *page, struct task_struct *task,
				   unsigned int nr) {}
static inline void pgalloc_tag_sub(struct page *page, unsigned int nr) {}
static inline struct alloc_tag *pgalloc_tag_get(struct page *page) { return NULL; }
static inline void pgalloc_tag_sub_pages(struct alloc_tag *tag, unsigned int nr) {}

#endif /* CONFIG_MEM_ALLOC_PROFILING */

#endif /* _LINUX_PGALLOC_TAG_H */
