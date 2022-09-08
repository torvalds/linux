// SPDX-License-Identifier: GPL-2.0
/*
 * Clang Control Flow Integrity (CFI) error and slowpath handling.
 *
 * Copyright (C) 2021 Google LLC
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
#else
#define cfi_failure_handler	__ubsan_handle_cfi_check_fail_abort
#endif

static inline void handle_cfi_failure(void *ptr)
{
	if (IS_ENABLED(CONFIG_CFI_PERMISSIVE))
		WARN_RATELIMIT(1, "CFI failure (target: %pS):\n", ptr);
	else
		panic("CFI failure (target: %pS)\n", ptr);
}

#ifdef CONFIG_MODULES

static inline cfi_check_fn find_module_check_fn(unsigned long ptr)
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
	cfi_check_fn fn = NULL;
	unsigned long flags;
	bool rcu_idle;

	if (is_kernel_text(ptr))
		return __cfi_check;

	/*
	 * Indirect call checks can happen when RCU is not watching. Both
	 * the shadow and __module_address use RCU, so we need to wake it
	 * up if necessary.
	 */
	rcu_idle = !rcu_is_watching();
	if (rcu_idle) {
		local_irq_save(flags);
		ct_irq_enter();
	}

	fn = find_module_check_fn(ptr);

	if (rcu_idle) {
		ct_irq_exit();
		local_irq_restore(flags);
	}

	return fn;
}

void __cfi_slowpath_diag(uint64_t id, void *ptr, void *diag)
{
	cfi_check_fn fn = find_check_fn((unsigned long)ptr);

	if (likely(fn))
		fn(id, ptr, diag);
	else /* Don't allow unchecked modules */
		handle_cfi_failure(ptr);
}
EXPORT_SYMBOL(__cfi_slowpath_diag);

#else /* !CONFIG_MODULES */

void __cfi_slowpath_diag(uint64_t id, void *ptr, void *diag)
{
	handle_cfi_failure(ptr); /* No modules */
}
EXPORT_SYMBOL(__cfi_slowpath_diag);

#endif /* CONFIG_MODULES */

void cfi_failure_handler(void *data, void *ptr, void *vtable)
{
	handle_cfi_failure(ptr);
}
EXPORT_SYMBOL(cfi_failure_handler);
