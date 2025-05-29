/* SPDX-License-Identifier: GPL-2.0 */
/*
 * allocation tagging
 */
#ifndef _LINUX_ALLOC_TAG_H
#define _LINUX_ALLOC_TAG_H

#include <linux/bug.h>
#include <linux/codetag.h>
#include <linux/container_of.h>
#include <linux/preempt.h>
#include <asm/percpu.h>
#include <linux/cpumask.h>
#include <linux/smp.h>
#include <linux/static_key.h>
#include <linux/irqflags.h>

struct alloc_tag_counters {
	u64 bytes;
	u64 calls;
};

/*
 * An instance of this structure is created in a special ELF section at every
 * allocation callsite. At runtime, the special section is treated as
 * an array of these. Embedded codetag utilizes codetag framework.
 */
struct alloc_tag {
	struct codetag			ct;
	struct alloc_tag_counters __percpu	*counters;
} __aligned(8);

struct alloc_tag_kernel_section {
	struct alloc_tag *first_tag;
	unsigned long count;
};

struct alloc_tag_module_section {
	union {
		unsigned long start_addr;
		struct alloc_tag *first_tag;
	};
	unsigned long end_addr;
	/* used size */
	unsigned long size;
};

#ifdef CONFIG_MEM_ALLOC_PROFILING_DEBUG

#define CODETAG_EMPTY	((void *)1)

static inline bool is_codetag_empty(union codetag_ref *ref)
{
	return ref->ct == CODETAG_EMPTY;
}

static inline void set_codetag_empty(union codetag_ref *ref)
{
	if (ref)
		ref->ct = CODETAG_EMPTY;
}

#else /* CONFIG_MEM_ALLOC_PROFILING_DEBUG */

static inline bool is_codetag_empty(union codetag_ref *ref) { return false; }

static inline void set_codetag_empty(union codetag_ref *ref)
{
	if (ref)
		ref->ct = NULL;
}

#endif /* CONFIG_MEM_ALLOC_PROFILING_DEBUG */

#ifdef CONFIG_MEM_ALLOC_PROFILING

#define ALLOC_TAG_SECTION_NAME	"alloc_tags"

struct codetag_bytes {
	struct codetag *ct;
	s64 bytes;
};

size_t alloc_tag_top_users(struct codetag_bytes *tags, size_t count, bool can_sleep);

static inline struct alloc_tag *ct_to_alloc_tag(struct codetag *ct)
{
	return container_of(ct, struct alloc_tag, ct);
}

#ifdef ARCH_NEEDS_WEAK_PER_CPU
/*
 * When percpu variables are required to be defined as weak, static percpu
 * variables can't be used inside a function (see comments for DECLARE_PER_CPU_SECTION).
 * Instead we will account all module allocations to a single counter.
 */
DECLARE_PER_CPU(struct alloc_tag_counters, _shared_alloc_tag);

#define DEFINE_ALLOC_TAG(_alloc_tag)						\
	static struct alloc_tag _alloc_tag __used __aligned(8)			\
	__section(ALLOC_TAG_SECTION_NAME) = {					\
		.ct = CODE_TAG_INIT,						\
		.counters = &_shared_alloc_tag };

#else /* ARCH_NEEDS_WEAK_PER_CPU */

#ifdef MODULE

#define DEFINE_ALLOC_TAG(_alloc_tag)						\
	static struct alloc_tag _alloc_tag __used __aligned(8)			\
	__section(ALLOC_TAG_SECTION_NAME) = {					\
		.ct = CODE_TAG_INIT,						\
		.counters = NULL };

#else  /* MODULE */

#define DEFINE_ALLOC_TAG(_alloc_tag)						\
	static DEFINE_PER_CPU(struct alloc_tag_counters, _alloc_tag_cntr);	\
	static struct alloc_tag _alloc_tag __used __aligned(8)			\
	__section(ALLOC_TAG_SECTION_NAME) = {					\
		.ct = CODE_TAG_INIT,						\
		.counters = &_alloc_tag_cntr };

#endif /* MODULE */

#endif /* ARCH_NEEDS_WEAK_PER_CPU */

DECLARE_STATIC_KEY_MAYBE(CONFIG_MEM_ALLOC_PROFILING_ENABLED_BY_DEFAULT,
			mem_alloc_profiling_key);

static inline bool mem_alloc_profiling_enabled(void)
{
	return static_branch_maybe(CONFIG_MEM_ALLOC_PROFILING_ENABLED_BY_DEFAULT,
				   &mem_alloc_profiling_key);
}

static inline struct alloc_tag_counters alloc_tag_read(struct alloc_tag *tag)
{
	struct alloc_tag_counters v = { 0, 0 };
	struct alloc_tag_counters *counter;
	int cpu;

	for_each_possible_cpu(cpu) {
		counter = per_cpu_ptr(tag->counters, cpu);
		v.bytes += counter->bytes;
		v.calls += counter->calls;
	}

	return v;
}

#ifdef CONFIG_MEM_ALLOC_PROFILING_DEBUG
static inline void alloc_tag_add_check(union codetag_ref *ref, struct alloc_tag *tag)
{
	WARN_ONCE(ref && ref->ct && !is_codetag_empty(ref),
		  "alloc_tag was not cleared (got tag for %s:%u)\n",
		  ref->ct->filename, ref->ct->lineno);

	WARN_ONCE(!tag, "current->alloc_tag not set\n");
}

static inline void alloc_tag_sub_check(union codetag_ref *ref)
{
	WARN_ONCE(ref && !ref->ct, "alloc_tag was not set\n");
}
#else
static inline void alloc_tag_add_check(union codetag_ref *ref, struct alloc_tag *tag) {}
static inline void alloc_tag_sub_check(union codetag_ref *ref) {}
#endif

/* Caller should verify both ref and tag to be valid */
static inline bool __alloc_tag_ref_set(union codetag_ref *ref, struct alloc_tag *tag)
{
	alloc_tag_add_check(ref, tag);
	if (!ref || !tag)
		return false;

	ref->ct = &tag->ct;
	return true;
}

static inline bool alloc_tag_ref_set(union codetag_ref *ref, struct alloc_tag *tag)
{
	if (unlikely(!__alloc_tag_ref_set(ref, tag)))
		return false;

	/*
	 * We need in increment the call counter every time we have a new
	 * allocation or when we split a large allocation into smaller ones.
	 * Each new reference for every sub-allocation needs to increment call
	 * counter because when we free each part the counter will be decremented.
	 */
	this_cpu_inc(tag->counters->calls);
	return true;
}

static inline void alloc_tag_add(union codetag_ref *ref, struct alloc_tag *tag, size_t bytes)
{
	if (likely(alloc_tag_ref_set(ref, tag)))
		this_cpu_add(tag->counters->bytes, bytes);
}

static inline void alloc_tag_sub(union codetag_ref *ref, size_t bytes)
{
	struct alloc_tag *tag;

	alloc_tag_sub_check(ref);
	if (!ref || !ref->ct)
		return;

	if (is_codetag_empty(ref)) {
		ref->ct = NULL;
		return;
	}

	tag = ct_to_alloc_tag(ref->ct);

	this_cpu_sub(tag->counters->bytes, bytes);
	this_cpu_dec(tag->counters->calls);

	ref->ct = NULL;
}

#define alloc_tag_record(p)	((p) = current->alloc_tag)

#else /* CONFIG_MEM_ALLOC_PROFILING */

#define DEFINE_ALLOC_TAG(_alloc_tag)
static inline bool mem_alloc_profiling_enabled(void) { return false; }
static inline void alloc_tag_add(union codetag_ref *ref, struct alloc_tag *tag,
				 size_t bytes) {}
static inline void alloc_tag_sub(union codetag_ref *ref, size_t bytes) {}
#define alloc_tag_record(p)	do {} while (0)

#endif /* CONFIG_MEM_ALLOC_PROFILING */

#define alloc_hooks_tag(_tag, _do_alloc)				\
({									\
	typeof(_do_alloc) _res;						\
	if (mem_alloc_profiling_enabled()) {				\
		struct alloc_tag * __maybe_unused _old;			\
		_old = alloc_tag_save(_tag);				\
		_res = _do_alloc;					\
		alloc_tag_restore(_tag, _old);				\
	} else								\
		_res = _do_alloc;					\
	_res;								\
})

#define alloc_hooks(_do_alloc)						\
({									\
	DEFINE_ALLOC_TAG(_alloc_tag);					\
	alloc_hooks_tag(&_alloc_tag, _do_alloc);			\
})

#endif /* _LINUX_ALLOC_TAG_H */
