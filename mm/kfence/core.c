// SPDX-License-Identifier: GPL-2.0
/*
 * KFENCE guarded object allocator and fault handling.
 *
 * Copyright (C) 2020, Google LLC.
 */

#define pr_fmt(fmt) "kfence: " fmt

#include <linux/atomic.h>
#include <linux/bug.h>
#include <linux/debugfs.h>
#include <linux/hash.h>
#include <linux/irq_work.h>
#include <linux/jhash.h>
#include <linux/kcsan-checks.h>
#include <linux/kfence.h>
#include <linux/kmemleak.h>
#include <linux/list.h>
#include <linux/lockdep.h>
#include <linux/log2.h>
#include <linux/memblock.h>
#include <linux/moduleparam.h>
#include <linux/notifier.h>
#include <linux/panic_notifier.h>
#include <linux/random.h>
#include <linux/rcupdate.h>
#include <linux/sched/clock.h>
#include <linux/sched/sysctl.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>

#include <asm/kfence.h>

#include "kfence.h"

/* Disables KFENCE on the first warning assuming an irrecoverable error. */
#define KFENCE_WARN_ON(cond)                                                   \
	({                                                                     \
		const bool __cond = WARN_ON(cond);                             \
		if (unlikely(__cond)) {                                        \
			WRITE_ONCE(kfence_enabled, false);                     \
			disabled_by_warn = true;                               \
		}                                                              \
		__cond;                                                        \
	})

/* === Data ================================================================= */

static bool kfence_enabled __read_mostly;
static bool disabled_by_warn __read_mostly;

unsigned long kfence_sample_interval __read_mostly = CONFIG_KFENCE_SAMPLE_INTERVAL;
EXPORT_SYMBOL_GPL(kfence_sample_interval); /* Export for test modules. */

#ifdef MODULE_PARAM_PREFIX
#undef MODULE_PARAM_PREFIX
#endif
#define MODULE_PARAM_PREFIX "kfence."

static int kfence_enable_late(void);
static int param_set_sample_interval(const char *val, const struct kernel_param *kp)
{
	unsigned long num;
	int ret = kstrtoul(val, 0, &num);

	if (ret < 0)
		return ret;

	/* Using 0 to indicate KFENCE is disabled. */
	if (!num && READ_ONCE(kfence_enabled)) {
		pr_info("disabled\n");
		WRITE_ONCE(kfence_enabled, false);
	}

	*((unsigned long *)kp->arg) = num;

	if (num && !READ_ONCE(kfence_enabled) && system_state != SYSTEM_BOOTING)
		return disabled_by_warn ? -EINVAL : kfence_enable_late();
	return 0;
}

static int param_get_sample_interval(char *buffer, const struct kernel_param *kp)
{
	if (!READ_ONCE(kfence_enabled))
		return sprintf(buffer, "0\n");

	return param_get_ulong(buffer, kp);
}

static const struct kernel_param_ops sample_interval_param_ops = {
	.set = param_set_sample_interval,
	.get = param_get_sample_interval,
};
module_param_cb(sample_interval, &sample_interval_param_ops, &kfence_sample_interval, 0600);

/* Pool usage% threshold when currently covered allocations are skipped. */
static unsigned long kfence_skip_covered_thresh __read_mostly = 75;
module_param_named(skip_covered_thresh, kfence_skip_covered_thresh, ulong, 0644);

/* If true, use a deferrable timer. */
static bool kfence_deferrable __read_mostly = IS_ENABLED(CONFIG_KFENCE_DEFERRABLE);
module_param_named(deferrable, kfence_deferrable, bool, 0444);

/* If true, check all canary bytes on panic. */
static bool kfence_check_on_panic __read_mostly;
module_param_named(check_on_panic, kfence_check_on_panic, bool, 0444);

/* The pool of pages used for guard pages and objects. */
char *__kfence_pool __read_mostly;
EXPORT_SYMBOL(__kfence_pool); /* Export for test modules. */

/*
 * Per-object metadata, with one-to-one mapping of object metadata to
 * backing pages (in __kfence_pool).
 */
static_assert(CONFIG_KFENCE_NUM_OBJECTS > 0);
struct kfence_metadata kfence_metadata[CONFIG_KFENCE_NUM_OBJECTS];

/* Freelist with available objects. */
static struct list_head kfence_freelist = LIST_HEAD_INIT(kfence_freelist);
static DEFINE_RAW_SPINLOCK(kfence_freelist_lock); /* Lock protecting freelist. */

/*
 * The static key to set up a KFENCE allocation; or if static keys are not used
 * to gate allocations, to avoid a load and compare if KFENCE is disabled.
 */
DEFINE_STATIC_KEY_FALSE(kfence_allocation_key);

/* Gates the allocation, ensuring only one succeeds in a given period. */
atomic_t kfence_allocation_gate = ATOMIC_INIT(1);

/*
 * A Counting Bloom filter of allocation coverage: limits currently covered
 * allocations of the same source filling up the pool.
 *
 * Assuming a range of 15%-85% unique allocations in the pool at any point in
 * time, the below parameters provide a probablity of 0.02-0.33 for false
 * positive hits respectively:
 *
 *	P(alloc_traces) = (1 - e^(-HNUM * (alloc_traces / SIZE)) ^ HNUM
 */
#define ALLOC_COVERED_HNUM	2
#define ALLOC_COVERED_ORDER	(const_ilog2(CONFIG_KFENCE_NUM_OBJECTS) + 2)
#define ALLOC_COVERED_SIZE	(1 << ALLOC_COVERED_ORDER)
#define ALLOC_COVERED_HNEXT(h)	hash_32(h, ALLOC_COVERED_ORDER)
#define ALLOC_COVERED_MASK	(ALLOC_COVERED_SIZE - 1)
static atomic_t alloc_covered[ALLOC_COVERED_SIZE];

/* Stack depth used to determine uniqueness of an allocation. */
#define UNIQUE_ALLOC_STACK_DEPTH ((size_t)8)

/*
 * Randomness for stack hashes, making the same collisions across reboots and
 * different machines less likely.
 */
static u32 stack_hash_seed __ro_after_init;

/* Statistics counters for debugfs. */
enum kfence_counter_id {
	KFENCE_COUNTER_ALLOCATED,
	KFENCE_COUNTER_ALLOCS,
	KFENCE_COUNTER_FREES,
	KFENCE_COUNTER_ZOMBIES,
	KFENCE_COUNTER_BUGS,
	KFENCE_COUNTER_SKIP_INCOMPAT,
	KFENCE_COUNTER_SKIP_CAPACITY,
	KFENCE_COUNTER_SKIP_COVERED,
	KFENCE_COUNTER_COUNT,
};
static atomic_long_t counters[KFENCE_COUNTER_COUNT];
static const char *const counter_names[] = {
	[KFENCE_COUNTER_ALLOCATED]	= "currently allocated",
	[KFENCE_COUNTER_ALLOCS]		= "total allocations",
	[KFENCE_COUNTER_FREES]		= "total frees",
	[KFENCE_COUNTER_ZOMBIES]	= "zombie allocations",
	[KFENCE_COUNTER_BUGS]		= "total bugs",
	[KFENCE_COUNTER_SKIP_INCOMPAT]	= "skipped allocations (incompatible)",
	[KFENCE_COUNTER_SKIP_CAPACITY]	= "skipped allocations (capacity)",
	[KFENCE_COUNTER_SKIP_COVERED]	= "skipped allocations (covered)",
};
static_assert(ARRAY_SIZE(counter_names) == KFENCE_COUNTER_COUNT);

/* === Internals ============================================================ */

static inline bool should_skip_covered(void)
{
	unsigned long thresh = (CONFIG_KFENCE_NUM_OBJECTS * kfence_skip_covered_thresh) / 100;

	return atomic_long_read(&counters[KFENCE_COUNTER_ALLOCATED]) > thresh;
}

static u32 get_alloc_stack_hash(unsigned long *stack_entries, size_t num_entries)
{
	num_entries = min(num_entries, UNIQUE_ALLOC_STACK_DEPTH);
	num_entries = filter_irq_stacks(stack_entries, num_entries);
	return jhash(stack_entries, num_entries * sizeof(stack_entries[0]), stack_hash_seed);
}

/*
 * Adds (or subtracts) count @val for allocation stack trace hash
 * @alloc_stack_hash from Counting Bloom filter.
 */
static void alloc_covered_add(u32 alloc_stack_hash, int val)
{
	int i;

	for (i = 0; i < ALLOC_COVERED_HNUM; i++) {
		atomic_add(val, &alloc_covered[alloc_stack_hash & ALLOC_COVERED_MASK]);
		alloc_stack_hash = ALLOC_COVERED_HNEXT(alloc_stack_hash);
	}
}

/*
 * Returns true if the allocation stack trace hash @alloc_stack_hash is
 * currently contained (non-zero count) in Counting Bloom filter.
 */
static bool alloc_covered_contains(u32 alloc_stack_hash)
{
	int i;

	for (i = 0; i < ALLOC_COVERED_HNUM; i++) {
		if (!atomic_read(&alloc_covered[alloc_stack_hash & ALLOC_COVERED_MASK]))
			return false;
		alloc_stack_hash = ALLOC_COVERED_HNEXT(alloc_stack_hash);
	}

	return true;
}

static bool kfence_protect(unsigned long addr)
{
	return !KFENCE_WARN_ON(!kfence_protect_page(ALIGN_DOWN(addr, PAGE_SIZE), true));
}

static bool kfence_unprotect(unsigned long addr)
{
	return !KFENCE_WARN_ON(!kfence_protect_page(ALIGN_DOWN(addr, PAGE_SIZE), false));
}

static inline unsigned long metadata_to_pageaddr(const struct kfence_metadata *meta)
{
	unsigned long offset = (meta - kfence_metadata + 1) * PAGE_SIZE * 2;
	unsigned long pageaddr = (unsigned long)&__kfence_pool[offset];

	/* The checks do not affect performance; only called from slow-paths. */

	/* Only call with a pointer into kfence_metadata. */
	if (KFENCE_WARN_ON(meta < kfence_metadata ||
			   meta >= kfence_metadata + CONFIG_KFENCE_NUM_OBJECTS))
		return 0;

	/*
	 * This metadata object only ever maps to 1 page; verify that the stored
	 * address is in the expected range.
	 */
	if (KFENCE_WARN_ON(ALIGN_DOWN(meta->addr, PAGE_SIZE) != pageaddr))
		return 0;

	return pageaddr;
}

/*
 * Update the object's metadata state, including updating the alloc/free stacks
 * depending on the state transition.
 */
static noinline void
metadata_update_state(struct kfence_metadata *meta, enum kfence_object_state next,
		      unsigned long *stack_entries, size_t num_stack_entries)
{
	struct kfence_track *track =
		next == KFENCE_OBJECT_FREED ? &meta->free_track : &meta->alloc_track;

	lockdep_assert_held(&meta->lock);

	if (stack_entries) {
		memcpy(track->stack_entries, stack_entries,
		       num_stack_entries * sizeof(stack_entries[0]));
	} else {
		/*
		 * Skip over 1 (this) functions; noinline ensures we do not
		 * accidentally skip over the caller by never inlining.
		 */
		num_stack_entries = stack_trace_save(track->stack_entries, KFENCE_STACK_DEPTH, 1);
	}
	track->num_stack_entries = num_stack_entries;
	track->pid = task_pid_nr(current);
	track->cpu = raw_smp_processor_id();
	track->ts_nsec = local_clock(); /* Same source as printk timestamps. */

	/*
	 * Pairs with READ_ONCE() in
	 *	kfence_shutdown_cache(),
	 *	kfence_handle_page_fault().
	 */
	WRITE_ONCE(meta->state, next);
}

/* Write canary byte to @addr. */
static inline bool set_canary_byte(u8 *addr)
{
	*addr = KFENCE_CANARY_PATTERN(addr);
	return true;
}

/* Check canary byte at @addr. */
static inline bool check_canary_byte(u8 *addr)
{
	struct kfence_metadata *meta;
	unsigned long flags;

	if (likely(*addr == KFENCE_CANARY_PATTERN(addr)))
		return true;

	atomic_long_inc(&counters[KFENCE_COUNTER_BUGS]);

	meta = addr_to_metadata((unsigned long)addr);
	raw_spin_lock_irqsave(&meta->lock, flags);
	kfence_report_error((unsigned long)addr, false, NULL, meta, KFENCE_ERROR_CORRUPTION);
	raw_spin_unlock_irqrestore(&meta->lock, flags);

	return false;
}

/* __always_inline this to ensure we won't do an indirect call to fn. */
static __always_inline void for_each_canary(const struct kfence_metadata *meta, bool (*fn)(u8 *))
{
	const unsigned long pageaddr = ALIGN_DOWN(meta->addr, PAGE_SIZE);
	unsigned long addr;

	/*
	 * We'll iterate over each canary byte per-side until fn() returns
	 * false. However, we'll still iterate over the canary bytes to the
	 * right of the object even if there was an error in the canary bytes to
	 * the left of the object. Specifically, if check_canary_byte()
	 * generates an error, showing both sides might give more clues as to
	 * what the error is about when displaying which bytes were corrupted.
	 */

	/* Apply to left of object. */
	for (addr = pageaddr; addr < meta->addr; addr++) {
		if (!fn((u8 *)addr))
			break;
	}

	/* Apply to right of object. */
	for (addr = meta->addr + meta->size; addr < pageaddr + PAGE_SIZE; addr++) {
		if (!fn((u8 *)addr))
			break;
	}
}

static void *kfence_guarded_alloc(struct kmem_cache *cache, size_t size, gfp_t gfp,
				  unsigned long *stack_entries, size_t num_stack_entries,
				  u32 alloc_stack_hash)
{
	struct kfence_metadata *meta = NULL;
	unsigned long flags;
	struct slab *slab;
	void *addr;
	const bool random_right_allocate = prandom_u32_max(2);
	const bool random_fault = CONFIG_KFENCE_STRESS_TEST_FAULTS &&
				  !prandom_u32_max(CONFIG_KFENCE_STRESS_TEST_FAULTS);

	/* Try to obtain a free object. */
	raw_spin_lock_irqsave(&kfence_freelist_lock, flags);
	if (!list_empty(&kfence_freelist)) {
		meta = list_entry(kfence_freelist.next, struct kfence_metadata, list);
		list_del_init(&meta->list);
	}
	raw_spin_unlock_irqrestore(&kfence_freelist_lock, flags);
	if (!meta) {
		atomic_long_inc(&counters[KFENCE_COUNTER_SKIP_CAPACITY]);
		return NULL;
	}

	if (unlikely(!raw_spin_trylock_irqsave(&meta->lock, flags))) {
		/*
		 * This is extremely unlikely -- we are reporting on a
		 * use-after-free, which locked meta->lock, and the reporting
		 * code via printk calls kmalloc() which ends up in
		 * kfence_alloc() and tries to grab the same object that we're
		 * reporting on. While it has never been observed, lockdep does
		 * report that there is a possibility of deadlock. Fix it by
		 * using trylock and bailing out gracefully.
		 */
		raw_spin_lock_irqsave(&kfence_freelist_lock, flags);
		/* Put the object back on the freelist. */
		list_add_tail(&meta->list, &kfence_freelist);
		raw_spin_unlock_irqrestore(&kfence_freelist_lock, flags);

		return NULL;
	}

	meta->addr = metadata_to_pageaddr(meta);
	/* Unprotect if we're reusing this page. */
	if (meta->state == KFENCE_OBJECT_FREED)
		kfence_unprotect(meta->addr);

	/*
	 * Note: for allocations made before RNG initialization, will always
	 * return zero. We still benefit from enabling KFENCE as early as
	 * possible, even when the RNG is not yet available, as this will allow
	 * KFENCE to detect bugs due to earlier allocations. The only downside
	 * is that the out-of-bounds accesses detected are deterministic for
	 * such allocations.
	 */
	if (random_right_allocate) {
		/* Allocate on the "right" side, re-calculate address. */
		meta->addr += PAGE_SIZE - size;
		meta->addr = ALIGN_DOWN(meta->addr, cache->align);
	}

	addr = (void *)meta->addr;

	/* Update remaining metadata. */
	metadata_update_state(meta, KFENCE_OBJECT_ALLOCATED, stack_entries, num_stack_entries);
	/* Pairs with READ_ONCE() in kfence_shutdown_cache(). */
	WRITE_ONCE(meta->cache, cache);
	meta->size = size;
	meta->alloc_stack_hash = alloc_stack_hash;
	raw_spin_unlock_irqrestore(&meta->lock, flags);

	alloc_covered_add(alloc_stack_hash, 1);

	/* Set required slab fields. */
	slab = virt_to_slab((void *)meta->addr);
	slab->slab_cache = cache;
#if defined(CONFIG_SLUB)
	slab->objects = 1;
#elif defined(CONFIG_SLAB)
	slab->s_mem = addr;
#endif

	/* Memory initialization. */
	for_each_canary(meta, set_canary_byte);

	/*
	 * We check slab_want_init_on_alloc() ourselves, rather than letting
	 * SL*B do the initialization, as otherwise we might overwrite KFENCE's
	 * redzone.
	 */
	if (unlikely(slab_want_init_on_alloc(gfp, cache)))
		memzero_explicit(addr, size);
	if (cache->ctor)
		cache->ctor(addr);

	if (random_fault)
		kfence_protect(meta->addr); /* Random "faults" by protecting the object. */

	atomic_long_inc(&counters[KFENCE_COUNTER_ALLOCATED]);
	atomic_long_inc(&counters[KFENCE_COUNTER_ALLOCS]);

	return addr;
}

static void kfence_guarded_free(void *addr, struct kfence_metadata *meta, bool zombie)
{
	struct kcsan_scoped_access assert_page_exclusive;
	unsigned long flags;
	bool init;

	raw_spin_lock_irqsave(&meta->lock, flags);

	if (meta->state != KFENCE_OBJECT_ALLOCATED || meta->addr != (unsigned long)addr) {
		/* Invalid or double-free, bail out. */
		atomic_long_inc(&counters[KFENCE_COUNTER_BUGS]);
		kfence_report_error((unsigned long)addr, false, NULL, meta,
				    KFENCE_ERROR_INVALID_FREE);
		raw_spin_unlock_irqrestore(&meta->lock, flags);
		return;
	}

	/* Detect racy use-after-free, or incorrect reallocation of this page by KFENCE. */
	kcsan_begin_scoped_access((void *)ALIGN_DOWN((unsigned long)addr, PAGE_SIZE), PAGE_SIZE,
				  KCSAN_ACCESS_SCOPED | KCSAN_ACCESS_WRITE | KCSAN_ACCESS_ASSERT,
				  &assert_page_exclusive);

	if (CONFIG_KFENCE_STRESS_TEST_FAULTS)
		kfence_unprotect((unsigned long)addr); /* To check canary bytes. */

	/* Restore page protection if there was an OOB access. */
	if (meta->unprotected_page) {
		memzero_explicit((void *)ALIGN_DOWN(meta->unprotected_page, PAGE_SIZE), PAGE_SIZE);
		kfence_protect(meta->unprotected_page);
		meta->unprotected_page = 0;
	}

	/* Mark the object as freed. */
	metadata_update_state(meta, KFENCE_OBJECT_FREED, NULL, 0);
	init = slab_want_init_on_free(meta->cache);
	raw_spin_unlock_irqrestore(&meta->lock, flags);

	alloc_covered_add(meta->alloc_stack_hash, -1);

	/* Check canary bytes for memory corruption. */
	for_each_canary(meta, check_canary_byte);

	/*
	 * Clear memory if init-on-free is set. While we protect the page, the
	 * data is still there, and after a use-after-free is detected, we
	 * unprotect the page, so the data is still accessible.
	 */
	if (!zombie && unlikely(init))
		memzero_explicit(addr, meta->size);

	/* Protect to detect use-after-frees. */
	kfence_protect((unsigned long)addr);

	kcsan_end_scoped_access(&assert_page_exclusive);
	if (!zombie) {
		/* Add it to the tail of the freelist for reuse. */
		raw_spin_lock_irqsave(&kfence_freelist_lock, flags);
		KFENCE_WARN_ON(!list_empty(&meta->list));
		list_add_tail(&meta->list, &kfence_freelist);
		raw_spin_unlock_irqrestore(&kfence_freelist_lock, flags);

		atomic_long_dec(&counters[KFENCE_COUNTER_ALLOCATED]);
		atomic_long_inc(&counters[KFENCE_COUNTER_FREES]);
	} else {
		/* See kfence_shutdown_cache(). */
		atomic_long_inc(&counters[KFENCE_COUNTER_ZOMBIES]);
	}
}

static void rcu_guarded_free(struct rcu_head *h)
{
	struct kfence_metadata *meta = container_of(h, struct kfence_metadata, rcu_head);

	kfence_guarded_free((void *)meta->addr, meta, false);
}

/*
 * Initialization of the KFENCE pool after its allocation.
 * Returns 0 on success; otherwise returns the address up to
 * which partial initialization succeeded.
 */
static unsigned long kfence_init_pool(void)
{
	unsigned long addr = (unsigned long)__kfence_pool;
	struct page *pages;
	int i;

	if (!arch_kfence_init_pool())
		return addr;

	pages = virt_to_page(__kfence_pool);

	/*
	 * Set up object pages: they must have PG_slab set, to avoid freeing
	 * these as real pages.
	 *
	 * We also want to avoid inserting kfence_free() in the kfree()
	 * fast-path in SLUB, and therefore need to ensure kfree() correctly
	 * enters __slab_free() slow-path.
	 */
	for (i = 0; i < KFENCE_POOL_SIZE / PAGE_SIZE; i++) {
		struct slab *slab = page_slab(&pages[i]);

		if (!i || (i % 2))
			continue;

		/* Verify we do not have a compound head page. */
		if (WARN_ON(compound_head(&pages[i]) != &pages[i]))
			return addr;

		__folio_set_slab(slab_folio(slab));
#ifdef CONFIG_MEMCG
		slab->memcg_data = (unsigned long)&kfence_metadata[i / 2 - 1].objcg |
				   MEMCG_DATA_OBJCGS;
#endif
	}

	/*
	 * Protect the first 2 pages. The first page is mostly unnecessary, and
	 * merely serves as an extended guard page. However, adding one
	 * additional page in the beginning gives us an even number of pages,
	 * which simplifies the mapping of address to metadata index.
	 */
	for (i = 0; i < 2; i++) {
		if (unlikely(!kfence_protect(addr)))
			return addr;

		addr += PAGE_SIZE;
	}

	for (i = 0; i < CONFIG_KFENCE_NUM_OBJECTS; i++) {
		struct kfence_metadata *meta = &kfence_metadata[i];

		/* Initialize metadata. */
		INIT_LIST_HEAD(&meta->list);
		raw_spin_lock_init(&meta->lock);
		meta->state = KFENCE_OBJECT_UNUSED;
		meta->addr = addr; /* Initialize for validation in metadata_to_pageaddr(). */
		list_add_tail(&meta->list, &kfence_freelist);

		/* Protect the right redzone. */
		if (unlikely(!kfence_protect(addr + PAGE_SIZE)))
			return addr;

		addr += 2 * PAGE_SIZE;
	}

	return 0;
}

static bool __init kfence_init_pool_early(void)
{
	unsigned long addr;

	if (!__kfence_pool)
		return false;

	addr = kfence_init_pool();

	if (!addr) {
		/*
		 * The pool is live and will never be deallocated from this point on.
		 * Ignore the pool object from the kmemleak phys object tree, as it would
		 * otherwise overlap with allocations returned by kfence_alloc(), which
		 * are registered with kmemleak through the slab post-alloc hook.
		 */
		kmemleak_ignore_phys(__pa(__kfence_pool));
		return true;
	}

	/*
	 * Only release unprotected pages, and do not try to go back and change
	 * page attributes due to risk of failing to do so as well. If changing
	 * page attributes for some pages fails, it is very likely that it also
	 * fails for the first page, and therefore expect addr==__kfence_pool in
	 * most failure cases.
	 */
	for (char *p = (char *)addr; p < __kfence_pool + KFENCE_POOL_SIZE; p += PAGE_SIZE) {
		struct slab *slab = virt_to_slab(p);

		if (!slab)
			continue;
#ifdef CONFIG_MEMCG
		slab->memcg_data = 0;
#endif
		__folio_clear_slab(slab_folio(slab));
	}
	memblock_free_late(__pa(addr), KFENCE_POOL_SIZE - (addr - (unsigned long)__kfence_pool));
	__kfence_pool = NULL;
	return false;
}

static bool kfence_init_pool_late(void)
{
	unsigned long addr, free_size;

	addr = kfence_init_pool();

	if (!addr)
		return true;

	/* Same as above. */
	free_size = KFENCE_POOL_SIZE - (addr - (unsigned long)__kfence_pool);
#ifdef CONFIG_CONTIG_ALLOC
	free_contig_range(page_to_pfn(virt_to_page((void *)addr)), free_size / PAGE_SIZE);
#else
	free_pages_exact((void *)addr, free_size);
#endif
	__kfence_pool = NULL;
	return false;
}

/* === DebugFS Interface ==================================================== */

static int stats_show(struct seq_file *seq, void *v)
{
	int i;

	seq_printf(seq, "enabled: %i\n", READ_ONCE(kfence_enabled));
	for (i = 0; i < KFENCE_COUNTER_COUNT; i++)
		seq_printf(seq, "%s: %ld\n", counter_names[i], atomic_long_read(&counters[i]));

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(stats);

/*
 * debugfs seq_file operations for /sys/kernel/debug/kfence/objects.
 * start_object() and next_object() return the object index + 1, because NULL is used
 * to stop iteration.
 */
static void *start_object(struct seq_file *seq, loff_t *pos)
{
	if (*pos < CONFIG_KFENCE_NUM_OBJECTS)
		return (void *)((long)*pos + 1);
	return NULL;
}

static void stop_object(struct seq_file *seq, void *v)
{
}

static void *next_object(struct seq_file *seq, void *v, loff_t *pos)
{
	++*pos;
	if (*pos < CONFIG_KFENCE_NUM_OBJECTS)
		return (void *)((long)*pos + 1);
	return NULL;
}

static int show_object(struct seq_file *seq, void *v)
{
	struct kfence_metadata *meta = &kfence_metadata[(long)v - 1];
	unsigned long flags;

	raw_spin_lock_irqsave(&meta->lock, flags);
	kfence_print_object(seq, meta);
	raw_spin_unlock_irqrestore(&meta->lock, flags);
	seq_puts(seq, "---------------------------------\n");

	return 0;
}

static const struct seq_operations objects_sops = {
	.start = start_object,
	.next = next_object,
	.stop = stop_object,
	.show = show_object,
};
DEFINE_SEQ_ATTRIBUTE(objects);

static int __init kfence_debugfs_init(void)
{
	struct dentry *kfence_dir = debugfs_create_dir("kfence", NULL);

	debugfs_create_file("stats", 0444, kfence_dir, NULL, &stats_fops);
	debugfs_create_file("objects", 0400, kfence_dir, NULL, &objects_fops);
	return 0;
}

late_initcall(kfence_debugfs_init);

/* === Panic Notifier ====================================================== */

static void kfence_check_all_canary(void)
{
	int i;

	for (i = 0; i < CONFIG_KFENCE_NUM_OBJECTS; i++) {
		struct kfence_metadata *meta = &kfence_metadata[i];

		if (meta->state == KFENCE_OBJECT_ALLOCATED)
			for_each_canary(meta, check_canary_byte);
	}
}

static int kfence_check_canary_callback(struct notifier_block *nb,
					unsigned long reason, void *arg)
{
	kfence_check_all_canary();
	return NOTIFY_OK;
}

static struct notifier_block kfence_check_canary_notifier = {
	.notifier_call = kfence_check_canary_callback,
};

/* === Allocation Gate Timer ================================================ */

static struct delayed_work kfence_timer;

#ifdef CONFIG_KFENCE_STATIC_KEYS
/* Wait queue to wake up allocation-gate timer task. */
static DECLARE_WAIT_QUEUE_HEAD(allocation_wait);

static void wake_up_kfence_timer(struct irq_work *work)
{
	wake_up(&allocation_wait);
}
static DEFINE_IRQ_WORK(wake_up_kfence_timer_work, wake_up_kfence_timer);
#endif

/*
 * Set up delayed work, which will enable and disable the static key. We need to
 * use a work queue (rather than a simple timer), since enabling and disabling a
 * static key cannot be done from an interrupt.
 *
 * Note: Toggling a static branch currently causes IPIs, and here we'll end up
 * with a total of 2 IPIs to all CPUs. If this ends up a problem in future (with
 * more aggressive sampling intervals), we could get away with a variant that
 * avoids IPIs, at the cost of not immediately capturing allocations if the
 * instructions remain cached.
 */
static void toggle_allocation_gate(struct work_struct *work)
{
	if (!READ_ONCE(kfence_enabled))
		return;

	atomic_set(&kfence_allocation_gate, 0);
#ifdef CONFIG_KFENCE_STATIC_KEYS
	/* Enable static key, and await allocation to happen. */
	static_branch_enable(&kfence_allocation_key);

	if (sysctl_hung_task_timeout_secs) {
		/*
		 * During low activity with no allocations we might wait a
		 * while; let's avoid the hung task warning.
		 */
		wait_event_idle_timeout(allocation_wait, atomic_read(&kfence_allocation_gate),
					sysctl_hung_task_timeout_secs * HZ / 2);
	} else {
		wait_event_idle(allocation_wait, atomic_read(&kfence_allocation_gate));
	}

	/* Disable static key and reset timer. */
	static_branch_disable(&kfence_allocation_key);
#endif
	queue_delayed_work(system_unbound_wq, &kfence_timer,
			   msecs_to_jiffies(kfence_sample_interval));
}

/* === Public interface ===================================================== */

void __init kfence_alloc_pool(void)
{
	if (!kfence_sample_interval)
		return;

	__kfence_pool = memblock_alloc(KFENCE_POOL_SIZE, PAGE_SIZE);

	if (!__kfence_pool)
		pr_err("failed to allocate pool\n");
}

static void kfence_init_enable(void)
{
	if (!IS_ENABLED(CONFIG_KFENCE_STATIC_KEYS))
		static_branch_enable(&kfence_allocation_key);

	if (kfence_deferrable)
		INIT_DEFERRABLE_WORK(&kfence_timer, toggle_allocation_gate);
	else
		INIT_DELAYED_WORK(&kfence_timer, toggle_allocation_gate);

	if (kfence_check_on_panic)
		atomic_notifier_chain_register(&panic_notifier_list, &kfence_check_canary_notifier);

	WRITE_ONCE(kfence_enabled, true);
	queue_delayed_work(system_unbound_wq, &kfence_timer, 0);

	pr_info("initialized - using %lu bytes for %d objects at 0x%p-0x%p\n", KFENCE_POOL_SIZE,
		CONFIG_KFENCE_NUM_OBJECTS, (void *)__kfence_pool,
		(void *)(__kfence_pool + KFENCE_POOL_SIZE));
}

void __init kfence_init(void)
{
	stack_hash_seed = get_random_u32();

	/* Setting kfence_sample_interval to 0 on boot disables KFENCE. */
	if (!kfence_sample_interval)
		return;

	if (!kfence_init_pool_early()) {
		pr_err("%s failed\n", __func__);
		return;
	}

	kfence_init_enable();
}

static int kfence_init_late(void)
{
	const unsigned long nr_pages = KFENCE_POOL_SIZE / PAGE_SIZE;
#ifdef CONFIG_CONTIG_ALLOC
	struct page *pages;

	pages = alloc_contig_pages(nr_pages, GFP_KERNEL, first_online_node, NULL);
	if (!pages)
		return -ENOMEM;
	__kfence_pool = page_to_virt(pages);
#else
	if (nr_pages > MAX_ORDER_NR_PAGES) {
		pr_warn("KFENCE_NUM_OBJECTS too large for buddy allocator\n");
		return -EINVAL;
	}
	__kfence_pool = alloc_pages_exact(KFENCE_POOL_SIZE, GFP_KERNEL);
	if (!__kfence_pool)
		return -ENOMEM;
#endif

	if (!kfence_init_pool_late()) {
		pr_err("%s failed\n", __func__);
		return -EBUSY;
	}

	kfence_init_enable();
	return 0;
}

static int kfence_enable_late(void)
{
	if (!__kfence_pool)
		return kfence_init_late();

	WRITE_ONCE(kfence_enabled, true);
	queue_delayed_work(system_unbound_wq, &kfence_timer, 0);
	pr_info("re-enabled\n");
	return 0;
}

void kfence_shutdown_cache(struct kmem_cache *s)
{
	unsigned long flags;
	struct kfence_metadata *meta;
	int i;

	for (i = 0; i < CONFIG_KFENCE_NUM_OBJECTS; i++) {
		bool in_use;

		meta = &kfence_metadata[i];

		/*
		 * If we observe some inconsistent cache and state pair where we
		 * should have returned false here, cache destruction is racing
		 * with either kmem_cache_alloc() or kmem_cache_free(). Taking
		 * the lock will not help, as different critical section
		 * serialization will have the same outcome.
		 */
		if (READ_ONCE(meta->cache) != s ||
		    READ_ONCE(meta->state) != KFENCE_OBJECT_ALLOCATED)
			continue;

		raw_spin_lock_irqsave(&meta->lock, flags);
		in_use = meta->cache == s && meta->state == KFENCE_OBJECT_ALLOCATED;
		raw_spin_unlock_irqrestore(&meta->lock, flags);

		if (in_use) {
			/*
			 * This cache still has allocations, and we should not
			 * release them back into the freelist so they can still
			 * safely be used and retain the kernel's default
			 * behaviour of keeping the allocations alive (leak the
			 * cache); however, they effectively become "zombie
			 * allocations" as the KFENCE objects are the only ones
			 * still in use and the owning cache is being destroyed.
			 *
			 * We mark them freed, so that any subsequent use shows
			 * more useful error messages that will include stack
			 * traces of the user of the object, the original
			 * allocation, and caller to shutdown_cache().
			 */
			kfence_guarded_free((void *)meta->addr, meta, /*zombie=*/true);
		}
	}

	for (i = 0; i < CONFIG_KFENCE_NUM_OBJECTS; i++) {
		meta = &kfence_metadata[i];

		/* See above. */
		if (READ_ONCE(meta->cache) != s || READ_ONCE(meta->state) != KFENCE_OBJECT_FREED)
			continue;

		raw_spin_lock_irqsave(&meta->lock, flags);
		if (meta->cache == s && meta->state == KFENCE_OBJECT_FREED)
			meta->cache = NULL;
		raw_spin_unlock_irqrestore(&meta->lock, flags);
	}
}

void *__kfence_alloc(struct kmem_cache *s, size_t size, gfp_t flags)
{
	unsigned long stack_entries[KFENCE_STACK_DEPTH];
	size_t num_stack_entries;
	u32 alloc_stack_hash;

	/*
	 * Perform size check before switching kfence_allocation_gate, so that
	 * we don't disable KFENCE without making an allocation.
	 */
	if (size > PAGE_SIZE) {
		atomic_long_inc(&counters[KFENCE_COUNTER_SKIP_INCOMPAT]);
		return NULL;
	}

	/*
	 * Skip allocations from non-default zones, including DMA. We cannot
	 * guarantee that pages in the KFENCE pool will have the requested
	 * properties (e.g. reside in DMAable memory).
	 */
	if ((flags & GFP_ZONEMASK) ||
	    (s->flags & (SLAB_CACHE_DMA | SLAB_CACHE_DMA32))) {
		atomic_long_inc(&counters[KFENCE_COUNTER_SKIP_INCOMPAT]);
		return NULL;
	}

	/*
	 * Skip allocations for this slab, if KFENCE has been disabled for
	 * this slab.
	 */
	if (s->flags & SLAB_SKIP_KFENCE)
		return NULL;

	if (atomic_inc_return(&kfence_allocation_gate) > 1)
		return NULL;
#ifdef CONFIG_KFENCE_STATIC_KEYS
	/*
	 * waitqueue_active() is fully ordered after the update of
	 * kfence_allocation_gate per atomic_inc_return().
	 */
	if (waitqueue_active(&allocation_wait)) {
		/*
		 * Calling wake_up() here may deadlock when allocations happen
		 * from within timer code. Use an irq_work to defer it.
		 */
		irq_work_queue(&wake_up_kfence_timer_work);
	}
#endif

	if (!READ_ONCE(kfence_enabled))
		return NULL;

	num_stack_entries = stack_trace_save(stack_entries, KFENCE_STACK_DEPTH, 0);

	/*
	 * Do expensive check for coverage of allocation in slow-path after
	 * allocation_gate has already become non-zero, even though it might
	 * mean not making any allocation within a given sample interval.
	 *
	 * This ensures reasonable allocation coverage when the pool is almost
	 * full, including avoiding long-lived allocations of the same source
	 * filling up the pool (e.g. pagecache allocations).
	 */
	alloc_stack_hash = get_alloc_stack_hash(stack_entries, num_stack_entries);
	if (should_skip_covered() && alloc_covered_contains(alloc_stack_hash)) {
		atomic_long_inc(&counters[KFENCE_COUNTER_SKIP_COVERED]);
		return NULL;
	}

	return kfence_guarded_alloc(s, size, flags, stack_entries, num_stack_entries,
				    alloc_stack_hash);
}

size_t kfence_ksize(const void *addr)
{
	const struct kfence_metadata *meta = addr_to_metadata((unsigned long)addr);

	/*
	 * Read locklessly -- if there is a race with __kfence_alloc(), this is
	 * either a use-after-free or invalid access.
	 */
	return meta ? meta->size : 0;
}

void *kfence_object_start(const void *addr)
{
	const struct kfence_metadata *meta = addr_to_metadata((unsigned long)addr);

	/*
	 * Read locklessly -- if there is a race with __kfence_alloc(), this is
	 * either a use-after-free or invalid access.
	 */
	return meta ? (void *)meta->addr : NULL;
}

void __kfence_free(void *addr)
{
	struct kfence_metadata *meta = addr_to_metadata((unsigned long)addr);

#ifdef CONFIG_MEMCG
	KFENCE_WARN_ON(meta->objcg);
#endif
	/*
	 * If the objects of the cache are SLAB_TYPESAFE_BY_RCU, defer freeing
	 * the object, as the object page may be recycled for other-typed
	 * objects once it has been freed. meta->cache may be NULL if the cache
	 * was destroyed.
	 */
	if (unlikely(meta->cache && (meta->cache->flags & SLAB_TYPESAFE_BY_RCU)))
		call_rcu(&meta->rcu_head, rcu_guarded_free);
	else
		kfence_guarded_free(addr, meta, false);
}

bool kfence_handle_page_fault(unsigned long addr, bool is_write, struct pt_regs *regs)
{
	const int page_index = (addr - (unsigned long)__kfence_pool) / PAGE_SIZE;
	struct kfence_metadata *to_report = NULL;
	enum kfence_error_type error_type;
	unsigned long flags;

	if (!is_kfence_address((void *)addr))
		return false;

	if (!READ_ONCE(kfence_enabled)) /* If disabled at runtime ... */
		return kfence_unprotect(addr); /* ... unprotect and proceed. */

	atomic_long_inc(&counters[KFENCE_COUNTER_BUGS]);

	if (page_index % 2) {
		/* This is a redzone, report a buffer overflow. */
		struct kfence_metadata *meta;
		int distance = 0;

		meta = addr_to_metadata(addr - PAGE_SIZE);
		if (meta && READ_ONCE(meta->state) == KFENCE_OBJECT_ALLOCATED) {
			to_report = meta;
			/* Data race ok; distance calculation approximate. */
			distance = addr - data_race(meta->addr + meta->size);
		}

		meta = addr_to_metadata(addr + PAGE_SIZE);
		if (meta && READ_ONCE(meta->state) == KFENCE_OBJECT_ALLOCATED) {
			/* Data race ok; distance calculation approximate. */
			if (!to_report || distance > data_race(meta->addr) - addr)
				to_report = meta;
		}

		if (!to_report)
			goto out;

		raw_spin_lock_irqsave(&to_report->lock, flags);
		to_report->unprotected_page = addr;
		error_type = KFENCE_ERROR_OOB;

		/*
		 * If the object was freed before we took the look we can still
		 * report this as an OOB -- the report will simply show the
		 * stacktrace of the free as well.
		 */
	} else {
		to_report = addr_to_metadata(addr);
		if (!to_report)
			goto out;

		raw_spin_lock_irqsave(&to_report->lock, flags);
		error_type = KFENCE_ERROR_UAF;
		/*
		 * We may race with __kfence_alloc(), and it is possible that a
		 * freed object may be reallocated. We simply report this as a
		 * use-after-free, with the stack trace showing the place where
		 * the object was re-allocated.
		 */
	}

out:
	if (to_report) {
		kfence_report_error(addr, is_write, regs, to_report, error_type);
		raw_spin_unlock_irqrestore(&to_report->lock, flags);
	} else {
		/* This may be a UAF or OOB access, but we can't be sure. */
		kfence_report_error(addr, is_write, regs, NULL, KFENCE_ERROR_INVALID);
	}

	return kfence_unprotect(addr); /* Unprotect and let access proceed. */
}
