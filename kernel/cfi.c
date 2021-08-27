// SPDX-License-Identifier: GPL-2.0
/*
 * Clang Control Flow Integrity (CFI) error and slowpath handling.
 *
 * Copyright (C) 2019 Google LLC
 */

#include <linux/hardirq.h>
#include <linux/kallsyms.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/printk.h>
#include <linux/ratelimit.h>
#include <linux/rcupdate.h>
#include <linux/vmalloc.h>
#include <asm/cacheflush.h>
#include <asm/set_memory.h>

/* Compiler-defined handler names */
#ifdef CONFIG_CFI_PERMISSIVE
#define cfi_failure_handler	__ubsan_handle_cfi_check_fail
#define cfi_slowpath_handler	__cfi_slowpath_diag
#else /* enforcing */
#define cfi_failure_handler	__ubsan_handle_cfi_check_fail_abort
#define cfi_slowpath_handler	__cfi_slowpath
#endif /* CONFIG_CFI_PERMISSIVE */

static inline void handle_cfi_failure(void *ptr)
{
	if (IS_ENABLED(CONFIG_CFI_PERMISSIVE))
		WARN_RATELIMIT(1, "CFI failure (target: %pS):\n", ptr);
	else
		panic("CFI failure (target: %pS)\n", ptr);
}

#ifdef CONFIG_MODULES
#ifdef CONFIG_CFI_CLANG_SHADOW
/*
 * Index type. A 16-bit index can address at most (2^16)-2 pages (taking
 * into account SHADOW_INVALID), i.e. ~256M with 4k pages.
 */
typedef u16 shadow_t;
#define SHADOW_INVALID		((shadow_t)~0UL)

struct cfi_shadow {
	/* Page index for the beginning of the shadow */
	unsigned long base;
	/* An array of __cfi_check locations (as indices to the shadow) */
	shadow_t shadow[1];
} __packed;

/*
 * The shadow covers ~128M from the beginning of the module region. If
 * the region is larger, we fall back to __module_address for the rest.
 */
#define __SHADOW_RANGE		(_UL(SZ_128M) >> PAGE_SHIFT)

/* The in-memory size of struct cfi_shadow, always at least one page */
#define __SHADOW_PAGES		((__SHADOW_RANGE * sizeof(shadow_t)) >> PAGE_SHIFT)
#define SHADOW_PAGES		max(1UL, __SHADOW_PAGES)
#define SHADOW_SIZE		(SHADOW_PAGES << PAGE_SHIFT)

/* The actual size of the shadow array, minus metadata */
#define SHADOW_ARR_SIZE		(SHADOW_SIZE - offsetof(struct cfi_shadow, shadow))
#define SHADOW_ARR_SLOTS	(SHADOW_ARR_SIZE / sizeof(shadow_t))

static DEFINE_MUTEX(shadow_update_lock);
static struct cfi_shadow __rcu *cfi_shadow __read_mostly;

/* Returns the index in the shadow for the given address */
static inline int ptr_to_shadow(const struct cfi_shadow *s, unsigned long ptr)
{
	unsigned long index;
	unsigned long page = ptr >> PAGE_SHIFT;

	if (unlikely(page < s->base))
		return -1; /* Outside of module area */

	index = page - s->base;

	if (index >= SHADOW_ARR_SLOTS)
		return -1; /* Cannot be addressed with shadow */

	return (int)index;
}

/* Returns the page address for an index in the shadow */
static inline unsigned long shadow_to_ptr(const struct cfi_shadow *s,
	int index)
{
	if (unlikely(index < 0 || index >= SHADOW_ARR_SLOTS))
		return 0;

	return (s->base + index) << PAGE_SHIFT;
}

/* Returns the __cfi_check function address for the given shadow location */
static inline unsigned long shadow_to_check_fn(const struct cfi_shadow *s,
	int index)
{
	if (unlikely(index < 0 || index >= SHADOW_ARR_SLOTS))
		return 0;

	if (unlikely(s->shadow[index] == SHADOW_INVALID))
		return 0;

	/* __cfi_check is always page aligned */
	return (s->base + s->shadow[index]) << PAGE_SHIFT;
}

static void prepare_next_shadow(const struct cfi_shadow __rcu *prev,
		struct cfi_shadow *next)
{
	int i, index, check;

	/* Mark everything invalid */
	memset(next->shadow, 0xFF, SHADOW_ARR_SIZE);

	if (!prev)
		return; /* No previous shadow */

	/* If the base address didn't change, an update is not needed */
	if (prev->base == next->base) {
		memcpy(next->shadow, prev->shadow, SHADOW_ARR_SIZE);
		return;
	}

	/* Convert the previous shadow to the new address range */
	for (i = 0; i < SHADOW_ARR_SLOTS; ++i) {
		if (prev->shadow[i] == SHADOW_INVALID)
			continue;

		index = ptr_to_shadow(next, shadow_to_ptr(prev, i));
		if (index < 0)
			continue;

		check = ptr_to_shadow(next,
				shadow_to_check_fn(prev, prev->shadow[i]));
		if (check < 0)
			continue;

		next->shadow[index] = (shadow_t)check;
	}
}

static void add_module_to_shadow(struct cfi_shadow *s, struct module *mod,
			unsigned long min_addr, unsigned long max_addr)
{
	int check_index;
	unsigned long check = (unsigned long)mod->cfi_check;
	unsigned long ptr;

	if (unlikely(!PAGE_ALIGNED(check))) {
		pr_warn("cfi: not using shadow for module %s\n", mod->name);
		return;
	}

	check_index = ptr_to_shadow(s, check);
	if (check_index < 0)
		return; /* Module not addressable with shadow */

	/* For each page, store the check function index in the shadow */
	for (ptr = min_addr; ptr <= max_addr; ptr += PAGE_SIZE) {
		int index = ptr_to_shadow(s, ptr);

		if (index >= 0) {
			/* Each page must only contain one module */
			WARN_ON_ONCE(s->shadow[index] != SHADOW_INVALID);
			s->shadow[index] = (shadow_t)check_index;
		}
	}
}

static void remove_module_from_shadow(struct cfi_shadow *s, struct module *mod,
		unsigned long min_addr, unsigned long max_addr)
{
	unsigned long ptr;

	for (ptr = min_addr; ptr <= max_addr; ptr += PAGE_SIZE) {
		int index = ptr_to_shadow(s, ptr);

		if (index >= 0)
			s->shadow[index] = SHADOW_INVALID;
	}
}

typedef void (*update_shadow_fn)(struct cfi_shadow *, struct module *,
			unsigned long min_addr, unsigned long max_addr);

static void update_shadow(struct module *mod, unsigned long base_addr,
		update_shadow_fn fn)
{
	struct cfi_shadow *prev;
	struct cfi_shadow *next;
	unsigned long min_addr, max_addr;

	next = (struct cfi_shadow *)vmalloc(SHADOW_SIZE);
	WARN_ON(!next);

	mutex_lock(&shadow_update_lock);
	prev = rcu_dereference_protected(cfi_shadow,
					 mutex_is_locked(&shadow_update_lock));

	if (next) {
		next->base = base_addr >> PAGE_SHIFT;
		prepare_next_shadow(prev, next);

		min_addr = (unsigned long)mod->core_layout.base;
		max_addr = min_addr + mod->core_layout.text_size;
		fn(next, mod, min_addr & PAGE_MASK, max_addr & PAGE_MASK);

		set_memory_ro((unsigned long)next, SHADOW_PAGES);
	}

	rcu_assign_pointer(cfi_shadow, next);
	mutex_unlock(&shadow_update_lock);
	synchronize_rcu_expedited();

	if (prev) {
		set_memory_rw((unsigned long)prev, SHADOW_PAGES);
		vfree(prev);
	}
}

void cfi_module_add(struct module *mod, unsigned long base_addr)
{
	update_shadow(mod, base_addr, add_module_to_shadow);
}

void cfi_module_remove(struct module *mod, unsigned long base_addr)
{
	update_shadow(mod, base_addr, remove_module_from_shadow);
}

static inline cfi_check_fn ptr_to_check_fn(const struct cfi_shadow __rcu *s,
	unsigned long ptr)
{
	int index;

	if (unlikely(!s))
		return NULL; /* No shadow available */

	index = ptr_to_shadow(s, ptr);
	if (index < 0)
		return NULL; /* Cannot be addressed with shadow */

	return (cfi_check_fn)shadow_to_check_fn(s, index);
}

static inline cfi_check_fn __find_shadow_check_fn(unsigned long ptr)
{
	cfi_check_fn fn;

	rcu_read_lock_sched_notrace();
	fn = ptr_to_check_fn(rcu_dereference_sched(cfi_shadow), ptr);
	rcu_read_unlock_sched_notrace();

	return fn;
}

#else /* !CONFIG_CFI_CLANG_SHADOW */

static inline cfi_check_fn __find_shadow_check_fn(unsigned long ptr)
{
	return NULL;
}

#endif /* CONFIG_CFI_CLANG_SHADOW */

static inline cfi_check_fn __find_module_check_fn(unsigned long ptr)
{
	cfi_check_fn fn = NULL;
	struct module *mod;

	rcu_read_lock_sched_notrace();
	mod = __module_address(ptr);
	if (mod)
		fn = mod->cfi_check;
	rcu_read_unlock_sched_notrace();

	return fn;
}

static inline cfi_check_fn find_check_fn(unsigned long ptr)
{
	bool rcu;
	cfi_check_fn fn = NULL;

	/*
	 * Indirect call checks can happen when RCU is not watching. Both
	 * the shadow and __module_address use RCU, so we need to wake it
	 * up before proceeding. Use rcu_nmi_enter/exit() as these calls
	 * can happen anywhere.
	 */
	rcu = rcu_is_watching();
	if (!rcu)
		rcu_nmi_enter();

	if (IS_ENABLED(CONFIG_CFI_CLANG_SHADOW)) {
		fn = __find_shadow_check_fn(ptr);
		if (fn)
			goto out;
	}

	if (is_kernel_text(ptr)) {
		fn = __cfi_check;
		goto out;
	}

	fn = __find_module_check_fn(ptr);

out:
	if (!rcu)
		rcu_nmi_exit();

	return fn;
}

void cfi_slowpath_handler(uint64_t id, void *ptr, void *diag)
{
	cfi_check_fn fn = find_check_fn((unsigned long)ptr);

	if (likely(fn))
		fn(id, ptr, diag);
	else /* Don't allow unchecked modules */
		handle_cfi_failure(ptr);
}

#else /* !CONFIG_MODULES */

void cfi_slowpath_handler(uint64_t id, void *ptr, void *diag)
{
	handle_cfi_failure(ptr); /* No modules */
}

#endif /* CONFIG_MODULES */

EXPORT_SYMBOL(cfi_slowpath_handler);

void cfi_failure_handler(void *data, void *ptr, void *vtable)
{
	handle_cfi_failure(ptr);
}
EXPORT_SYMBOL(cfi_failure_handler);

void __cfi_check_fail(void *data, void *ptr)
{
	handle_cfi_failure(ptr);
}
