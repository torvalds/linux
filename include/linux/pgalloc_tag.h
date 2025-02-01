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
extern unsigned long alloc_tag_ref_mask;
extern int alloc_tag_ref_offs;
extern struct alloc_tag_kernel_section kernel_tags;

DECLARE_STATIC_KEY_FALSE(mem_profiling_compressed);

typedef u16	pgalloc_tag_idx;

union pgtag_ref_handle {
	union codetag_ref *ref;	/* reference in page extension */
	struct page *page;	/* reference in page flags */
};

/* Reserved indexes */
#define CODETAG_ID_NULL		0
#define CODETAG_ID_EMPTY	1
#define CODETAG_ID_FIRST	2

#ifdef CONFIG_MODULES

extern struct alloc_tag_module_section module_tags;

static inline struct alloc_tag *module_idx_to_tag(pgalloc_tag_idx idx)
{
	return &module_tags.first_tag[idx - kernel_tags.count];
}

static inline pgalloc_tag_idx module_tag_to_idx(struct alloc_tag *tag)
{
	return CODETAG_ID_FIRST + kernel_tags.count + (tag - module_tags.first_tag);
}

#else /* CONFIG_MODULES */

static inline struct alloc_tag *module_idx_to_tag(pgalloc_tag_idx idx)
{
	pr_warn("invalid page tag reference %lu\n", (unsigned long)idx);
	return NULL;
}

static inline pgalloc_tag_idx module_tag_to_idx(struct alloc_tag *tag)
{
	pr_warn("invalid page tag 0x%lx\n", (unsigned long)tag);
	return CODETAG_ID_NULL;
}

#endif /* CONFIG_MODULES */

static inline void idx_to_ref(pgalloc_tag_idx idx, union codetag_ref *ref)
{
	switch (idx) {
	case (CODETAG_ID_NULL):
		ref->ct = NULL;
		break;
	case (CODETAG_ID_EMPTY):
		set_codetag_empty(ref);
		break;
	default:
		idx -= CODETAG_ID_FIRST;
		ref->ct = idx < kernel_tags.count ?
			&kernel_tags.first_tag[idx].ct :
			&module_idx_to_tag(idx)->ct;
		break;
	}
}

static inline pgalloc_tag_idx ref_to_idx(union codetag_ref *ref)
{
	struct alloc_tag *tag;

	if (!ref->ct)
		return CODETAG_ID_NULL;

	if (is_codetag_empty(ref))
		return CODETAG_ID_EMPTY;

	tag = ct_to_alloc_tag(ref->ct);
	if (tag >= kernel_tags.first_tag && tag < kernel_tags.first_tag + kernel_tags.count)
		return CODETAG_ID_FIRST + (tag - kernel_tags.first_tag);

	return module_tag_to_idx(tag);
}



/* Should be called only if mem_alloc_profiling_enabled() */
static inline bool get_page_tag_ref(struct page *page, union codetag_ref *ref,
				    union pgtag_ref_handle *handle)
{
	if (!page)
		return false;

	if (static_key_enabled(&mem_profiling_compressed)) {
		pgalloc_tag_idx idx;

		idx = (page->flags >> alloc_tag_ref_offs) & alloc_tag_ref_mask;
		idx_to_ref(idx, ref);
		handle->page = page;
	} else {
		struct page_ext *page_ext;
		union codetag_ref *tmp;

		page_ext = page_ext_get(page);
		if (!page_ext)
			return false;

		tmp = (union codetag_ref *)page_ext_data(page_ext, &page_alloc_tagging_ops);
		ref->ct = tmp->ct;
		handle->ref = tmp;
	}

	return true;
}

static inline void put_page_tag_ref(union pgtag_ref_handle handle)
{
	if (WARN_ON(!handle.ref))
		return;

	if (!static_key_enabled(&mem_profiling_compressed))
		page_ext_put((void *)handle.ref - page_alloc_tagging_ops.offset);
}

static inline void update_page_tag_ref(union pgtag_ref_handle handle, union codetag_ref *ref)
{
	if (static_key_enabled(&mem_profiling_compressed)) {
		struct page *page = handle.page;
		unsigned long old_flags;
		unsigned long flags;
		unsigned long idx;

		if (WARN_ON(!page || !ref))
			return;

		idx = (unsigned long)ref_to_idx(ref);
		idx = (idx & alloc_tag_ref_mask) << alloc_tag_ref_offs;
		do {
			old_flags = READ_ONCE(page->flags);
			flags = old_flags;
			flags &= ~(alloc_tag_ref_mask << alloc_tag_ref_offs);
			flags |= idx;
		} while (unlikely(!try_cmpxchg(&page->flags, &old_flags, flags)));
	} else {
		if (WARN_ON(!handle.ref || !ref))
			return;

		handle.ref->ct = ref->ct;
	}
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

/* Should be called only if mem_alloc_profiling_enabled() */
static inline struct alloc_tag *__pgalloc_tag_get(struct page *page)
{
	struct alloc_tag *tag = NULL;
	union pgtag_ref_handle handle;
	union codetag_ref ref;

	if (get_page_tag_ref(page, &ref, &handle)) {
		alloc_tag_sub_check(&ref);
		if (ref.ct)
			tag = ct_to_alloc_tag(ref.ct);
		put_page_tag_ref(handle);
	}

	return tag;
}

static inline void pgalloc_tag_sub_pages(struct page *page, unsigned int nr)
{
	struct alloc_tag *tag;

	if (!mem_alloc_profiling_enabled())
		return;

	tag = __pgalloc_tag_get(page);
	if (tag)
		this_cpu_sub(tag->counters->bytes, PAGE_SIZE * nr);
}

void pgalloc_tag_split(struct folio *folio, int old_order, int new_order);
void pgalloc_tag_swap(struct folio *new, struct folio *old);

void __init alloc_tag_sec_init(void);

#else /* CONFIG_MEM_ALLOC_PROFILING */

static inline void clear_page_tag_ref(struct page *page) {}
static inline void pgalloc_tag_add(struct page *page, struct task_struct *task,
				   unsigned int nr) {}
static inline void pgalloc_tag_sub(struct page *page, unsigned int nr) {}
static inline void pgalloc_tag_sub_pages(struct page *page, unsigned int nr) {}
static inline void alloc_tag_sec_init(void) {}
static inline void pgalloc_tag_split(struct folio *folio, int old_order, int new_order) {}
static inline void pgalloc_tag_swap(struct folio *new, struct folio *old) {}

#endif /* CONFIG_MEM_ALLOC_PROFILING */

#endif /* _LINUX_PGALLOC_TAG_H */
