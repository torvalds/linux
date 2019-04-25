/*
 * CFI (Control Flow Integrity) error and slowpath handling
 *
 * Copyright (C) 2017 Google, Inc.
 */

#include <linux/gfp.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/ratelimit.h>
#include <linux/rcupdate.h>
#include <linux/spinlock.h>
#include <asm/bug.h>
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
#ifdef CONFIG_CFI_PERMISSIVE
	WARN_RATELIMIT(1, "CFI failure (target: %pF):\n", ptr);
#else
	pr_err("CFI failure (target: %pF):\n", ptr);
	BUG();
#endif
}

#ifdef CONFIG_MODULES
#ifdef CONFIG_CFI_CLANG_SHADOW
struct shadow_range {
	/* Module address range */
	unsigned long mod_min_addr;
	unsigned long mod_max_addr;
	/* Module page range */
	unsigned long min_page;
	unsigned long max_page;
};

#define SHADOW_ORDER	1
#define SHADOW_PAGES	(1 << SHADOW_ORDER)
#define SHADOW_SIZE \
	((SHADOW_PAGES * PAGE_SIZE - sizeof(struct shadow_range)) / sizeof(u16))
#define SHADOW_INVALID	0xFFFF

struct cfi_shadow {
	/* Page range covered by the shadow */
	struct shadow_range r;
	/* Page offsets to __cfi_check functions in modules */
	u16 shadow[SHADOW_SIZE];
};

static DEFINE_SPINLOCK(shadow_update_lock);
static struct cfi_shadow __rcu *cfi_shadow __read_mostly = NULL;

static inline int ptr_to_shadow(const struct cfi_shadow *s, unsigned long ptr)
{
	unsigned long index;
	unsigned long page = ptr >> PAGE_SHIFT;

	if (unlikely(page < s->r.min_page))
		return -1; /* Outside of module area */

	index = page - s->r.min_page;

	if (index >= SHADOW_SIZE)
		return -1; /* Cannot be addressed with shadow */

	return (int)index;
}

static inline unsigned long shadow_to_ptr(const struct cfi_shadow *s,
	int index)
{
	BUG_ON(index < 0 || index >= SHADOW_SIZE);

	if (unlikely(s->shadow[index] == SHADOW_INVALID))
		return 0;

	return (s->r.min_page + s->shadow[index]) << PAGE_SHIFT;
}

static inline unsigned long shadow_to_page(const struct cfi_shadow *s,
	int index)
{
	BUG_ON(index < 0 || index >= SHADOW_SIZE);

	return (s->r.min_page + index) << PAGE_SHIFT;
}

static void prepare_next_shadow(const struct cfi_shadow __rcu *prev,
		struct cfi_shadow *next)
{
	int i, index, check;

	/* Mark everything invalid */
	memset(next->shadow, 0xFF, sizeof(next->shadow));

	if (!prev)
		return; /* No previous shadow */

	/* If the base address didn't change, update is not needed */
	if (prev->r.min_page == next->r.min_page) {
		memcpy(next->shadow, prev->shadow, sizeof(next->shadow));
		return;
	}

	/* Convert the previous shadow to the new address range */
	for (i = 0; i < SHADOW_SIZE; ++i) {
		if (prev->shadow[i] == SHADOW_INVALID)
			continue;

		index = ptr_to_shadow(next, shadow_to_page(prev, i));
		if (index < 0)
			continue;

		check = ptr_to_shadow(next,
				shadow_to_ptr(prev, prev->shadow[i]));
		if (check < 0)
			continue;

		next->shadow[index] = (u16)check;
	}
}

static void add_module_to_shadow(struct cfi_shadow *s, struct module *mod)
{
	unsigned long ptr;
	unsigned long min_page_addr;
	unsigned long max_page_addr;
	unsigned long check = (unsigned long)mod->cfi_check;
	int check_index = ptr_to_shadow(s, check);

	BUG_ON((check & PAGE_MASK) != check); /* Must be page aligned */

	if (check_index < 0)
		return; /* Module not addressable with shadow */

	min_page_addr = (unsigned long)mod->core_layout.base & PAGE_MASK;
	max_page_addr = (unsigned long)mod->core_layout.base +
				       mod->core_layout.text_size;
	max_page_addr &= PAGE_MASK;

	/* For each page, store the check function index in the shadow */
	for (ptr = min_page_addr; ptr <= max_page_addr; ptr += PAGE_SIZE) {
		int index = ptr_to_shadow(s, ptr);
		if (index >= 0) {
			/* Assume a page only contains code for one module */
			BUG_ON(s->shadow[index] != SHADOW_INVALID);
			s->shadow[index] = (u16)check_index;
		}
	}
}

static void remove_module_from_shadow(struct cfi_shadow *s, struct module *mod)
{
	unsigned long ptr;
	unsigned long min_page_addr;
	unsigned long max_page_addr;

	min_page_addr = (unsigned long)mod->core_layout.base & PAGE_MASK;
	max_page_addr = (unsigned long)mod->core_layout.base +
				       mod->core_layout.text_size;
	max_page_addr &= PAGE_MASK;

	for (ptr = min_page_addr; ptr <= max_page_addr; ptr += PAGE_SIZE) {
		int index = ptr_to_shadow(s, ptr);
		if (index >= 0)
			s->shadow[index] = SHADOW_INVALID;
	}
}

typedef void (*update_shadow_fn)(struct cfi_shadow *, struct module *);

static void update_shadow(struct module *mod, unsigned long min_addr,
	unsigned long max_addr, update_shadow_fn fn)
{
	struct cfi_shadow *prev;
	struct cfi_shadow *next = (struct cfi_shadow *)
		__get_free_pages(GFP_KERNEL, SHADOW_ORDER);

	BUG_ON(!next);

	next->r.mod_min_addr = min_addr;
	next->r.mod_max_addr = max_addr;
	next->r.min_page = min_addr >> PAGE_SHIFT;
	next->r.max_page = max_addr >> PAGE_SHIFT;

	spin_lock(&shadow_update_lock);
	prev = rcu_dereference_protected(cfi_shadow, 1);
	prepare_next_shadow(prev, next);

	fn(next, mod);
	set_memory_ro((unsigned long)next, SHADOW_PAGES);
	rcu_assign_pointer(cfi_shadow, next);

	spin_unlock(&shadow_update_lock);
	synchronize_rcu();

	if (prev) {
		set_memory_rw((unsigned long)prev, SHADOW_PAGES);
		free_pages((unsigned long)prev, SHADOW_ORDER);
	}
}

void cfi_module_add(struct module *mod, unsigned long min_addr,
	unsigned long max_addr)
{
	update_shadow(mod, min_addr, max_addr, add_module_to_shadow);
}
EXPORT_SYMBOL(cfi_module_add);

void cfi_module_remove(struct module *mod, unsigned long min_addr,
	unsigned long max_addr)
{
	update_shadow(mod, min_addr, max_addr, remove_module_from_shadow);
}
EXPORT_SYMBOL(cfi_module_remove);

static inline cfi_check_fn ptr_to_check_fn(const struct cfi_shadow __rcu *s,
	unsigned long ptr)
{
	int index;

	if (unlikely(!s))
		return NULL; /* No shadow available */

	if (ptr < s->r.mod_min_addr || ptr > s->r.mod_max_addr)
		return NULL; /* Not in a mapped module */

	index = ptr_to_shadow(s, ptr);
	if (index < 0)
		return NULL; /* Cannot be addressed with shadow */

	return (cfi_check_fn)shadow_to_ptr(s, index);
}
#endif /* CONFIG_CFI_CLANG_SHADOW */

static inline cfi_check_fn find_module_cfi_check(void *ptr)
{
	struct module *mod;

	preempt_disable();
	mod = __module_address((unsigned long)ptr);
	preempt_enable();

	if (mod)
		return mod->cfi_check;

	return CFI_CHECK_FN;
}

static inline cfi_check_fn find_cfi_check(void *ptr)
{
#ifdef CONFIG_CFI_CLANG_SHADOW
	cfi_check_fn f;

	if (!rcu_access_pointer(cfi_shadow))
		return CFI_CHECK_FN; /* No loaded modules */

	/* Look up the __cfi_check function to use */
	rcu_read_lock();
	f = ptr_to_check_fn(rcu_dereference(cfi_shadow), (unsigned long)ptr);
	rcu_read_unlock();

	if (f)
		return f;

	/*
	 * Fall back to find_module_cfi_check, which works also for a larger
	 * module address space, but is slower.
	 */
#endif /* CONFIG_CFI_CLANG_SHADOW */

	return find_module_cfi_check(ptr);
}

void cfi_slowpath_handler(uint64_t id, void *ptr, void *diag)
{
	cfi_check_fn check = find_cfi_check(ptr);

	if (likely(check))
		check(id, ptr, diag);
	else /* Don't allow unchecked modules */
		handle_cfi_failure(ptr);
}
EXPORT_SYMBOL(cfi_slowpath_handler);
#endif /* CONFIG_MODULES */

void cfi_failure_handler(void *data, void *ptr, void *vtable)
{
	handle_cfi_failure(ptr);
}
EXPORT_SYMBOL(cfi_failure_handler);

void __cfi_check_fail(void *data, void *ptr)
{
	handle_cfi_failure(ptr);
}
