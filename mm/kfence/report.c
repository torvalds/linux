// SPDX-License-Identifier: GPL-2.0
/*
 * KFENCE reporting.
 *
 * Copyright (C) 2020, Google LLC.
 */

#include <linux/stdarg.h>

#include <linux/kernel.h>
#include <linux/lockdep.h>
#include <linux/math.h>
#include <linux/printk.h>
#include <linux/sched/debug.h>
#include <linux/seq_file.h>
#include <linux/stacktrace.h>
#include <linux/string.h>
#include <trace/events/error_report.h>

#include <asm/kfence.h>

#include "kfence.h"

/* May be overridden by <asm/kfence.h>. */
#ifndef ARCH_FUNC_PREFIX
#define ARCH_FUNC_PREFIX ""
#endif

extern bool no_hash_pointers;

/* Helper function to either print to a seq_file or to console. */
__printf(2, 3)
static void seq_con_printf(struct seq_file *seq, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	if (seq)
		seq_vprintf(seq, fmt, args);
	else
		vprintk(fmt, args);
	va_end(args);
}

/*
 * Get the number of stack entries to skip to get out of MM internals. @type is
 * optional, and if set to NULL, assumes an allocation or free stack.
 */
static int get_stack_skipnr(const unsigned long stack_entries[], int num_entries,
			    const enum kfence_error_type *type)
{
	char buf[64];
	int skipnr, fallback = 0;

	if (type) {
		/* Depending on error type, find different stack entries. */
		switch (*type) {
		case KFENCE_ERROR_UAF:
		case KFENCE_ERROR_OOB:
		case KFENCE_ERROR_INVALID:
			/*
			 * kfence_handle_page_fault() may be called with pt_regs
			 * set to NULL; in that case we'll simply show the full
			 * stack trace.
			 */
			return 0;
		case KFENCE_ERROR_CORRUPTION:
		case KFENCE_ERROR_INVALID_FREE:
			break;
		}
	}

	for (skipnr = 0; skipnr < num_entries; skipnr++) {
		int len = scnprintf(buf, sizeof(buf), "%ps", (void *)stack_entries[skipnr]);

		if (str_has_prefix(buf, ARCH_FUNC_PREFIX "kfence_") ||
		    str_has_prefix(buf, ARCH_FUNC_PREFIX "__kfence_") ||
		    str_has_prefix(buf, ARCH_FUNC_PREFIX "__kmem_cache_free") ||
		    !strncmp(buf, ARCH_FUNC_PREFIX "__slab_free", len)) {
			/*
			 * In case of tail calls from any of the below to any of
			 * the above, optimized by the compiler such that the
			 * stack trace would omit the initial entry point below.
			 */
			fallback = skipnr + 1;
		}

		/*
		 * The below list should only include the initial entry points
		 * into the slab allocators. Includes the *_bulk() variants by
		 * checking prefixes.
		 */
		if (str_has_prefix(buf, ARCH_FUNC_PREFIX "kfree") ||
		    str_has_prefix(buf, ARCH_FUNC_PREFIX "kmem_cache_free") ||
		    str_has_prefix(buf, ARCH_FUNC_PREFIX "__kmalloc") ||
		    str_has_prefix(buf, ARCH_FUNC_PREFIX "kmem_cache_alloc"))
			goto found;
	}
	if (fallback < num_entries)
		return fallback;
found:
	skipnr++;
	return skipnr < num_entries ? skipnr : 0;
}

static void kfence_print_stack(struct seq_file *seq, const struct kfence_metadata *meta,
			       bool show_alloc)
{
	const struct kfence_track *track = show_alloc ? &meta->alloc_track : &meta->free_track;
	u64 ts_sec = track->ts_nsec;
	unsigned long rem_nsec = do_div(ts_sec, NSEC_PER_SEC);

	/* Timestamp matches printk timestamp format. */
	seq_con_printf(seq, "%s by task %d on cpu %d at %lu.%06lus:\n",
		       show_alloc ? "allocated" : "freed", track->pid,
		       track->cpu, (unsigned long)ts_sec, rem_nsec / 1000);

	if (track->num_stack_entries) {
		/* Skip allocation/free internals stack. */
		int i = get_stack_skipnr(track->stack_entries, track->num_stack_entries, NULL);

		/* stack_trace_seq_print() does not exist; open code our own. */
		for (; i < track->num_stack_entries; i++)
			seq_con_printf(seq, " %pS\n", (void *)track->stack_entries[i]);
	} else {
		seq_con_printf(seq, " no %s stack\n", show_alloc ? "allocation" : "deallocation");
	}
}

void kfence_print_object(struct seq_file *seq, const struct kfence_metadata *meta)
{
	const int size = abs(meta->size);
	const unsigned long start = meta->addr;
	const struct kmem_cache *const cache = meta->cache;

	lockdep_assert_held(&meta->lock);

	if (meta->state == KFENCE_OBJECT_UNUSED) {
		seq_con_printf(seq, "kfence-#%td unused\n", meta - kfence_metadata);
		return;
	}

	seq_con_printf(seq, "kfence-#%td: 0x%p-0x%p, size=%d, cache=%s\n\n",
		       meta - kfence_metadata, (void *)start, (void *)(start + size - 1),
		       size, (cache && cache->name) ? cache->name : "<destroyed>");

	kfence_print_stack(seq, meta, true);

	if (meta->state == KFENCE_OBJECT_FREED) {
		seq_con_printf(seq, "\n");
		kfence_print_stack(seq, meta, false);
	}
}

/*
 * Show bytes at @addr that are different from the expected canary values, up to
 * @max_bytes.
 */
static void print_diff_canary(unsigned long address, size_t bytes_to_show,
			      const struct kfence_metadata *meta)
{
	const unsigned long show_until_addr = address + bytes_to_show;
	const u8 *cur, *end;

	/* Do not show contents of object nor read into following guard page. */
	end = (const u8 *)(address < meta->addr ? min(show_until_addr, meta->addr)
						: min(show_until_addr, PAGE_ALIGN(address)));

	pr_cont("[");
	for (cur = (const u8 *)address; cur < end; cur++) {
		if (*cur == KFENCE_CANARY_PATTERN_U8(cur))
			pr_cont(" .");
		else if (no_hash_pointers)
			pr_cont(" 0x%02x", *cur);
		else /* Do not leak kernel memory in non-debug builds. */
			pr_cont(" !");
	}
	pr_cont(" ]");
}

static const char *get_access_type(bool is_write)
{
	return is_write ? "write" : "read";
}

void kfence_report_error(unsigned long address, bool is_write, struct pt_regs *regs,
			 const struct kfence_metadata *meta, enum kfence_error_type type)
{
	unsigned long stack_entries[KFENCE_STACK_DEPTH] = { 0 };
	const ptrdiff_t object_index = meta ? meta - kfence_metadata : -1;
	int num_stack_entries;
	int skipnr = 0;

	if (regs) {
		num_stack_entries = stack_trace_save_regs(regs, stack_entries, KFENCE_STACK_DEPTH, 0);
	} else {
		num_stack_entries = stack_trace_save(stack_entries, KFENCE_STACK_DEPTH, 1);
		skipnr = get_stack_skipnr(stack_entries, num_stack_entries, &type);
	}

	/* Require non-NULL meta, except if KFENCE_ERROR_INVALID. */
	if (WARN_ON(type != KFENCE_ERROR_INVALID && !meta))
		return;

	if (meta)
		lockdep_assert_held(&meta->lock);
	/*
	 * Because we may generate reports in printk-unfriendly parts of the
	 * kernel, such as scheduler code, the use of printk() could deadlock.
	 * Until such time that all printing code here is safe in all parts of
	 * the kernel, accept the risk, and just get our message out (given the
	 * system might already behave unpredictably due to the memory error).
	 * As such, also disable lockdep to hide warnings, and avoid disabling
	 * lockdep for the rest of the kernel.
	 */
	lockdep_off();

	pr_err("==================================================================\n");
	/* Print report header. */
	switch (type) {
	case KFENCE_ERROR_OOB: {
		const bool left_of_object = address < meta->addr;

		pr_err("BUG: KFENCE: out-of-bounds %s in %pS\n\n", get_access_type(is_write),
		       (void *)stack_entries[skipnr]);
		pr_err("Out-of-bounds %s at 0x%p (%luB %s of kfence-#%td):\n",
		       get_access_type(is_write), (void *)address,
		       left_of_object ? meta->addr - address : address - meta->addr,
		       left_of_object ? "left" : "right", object_index);
		break;
	}
	case KFENCE_ERROR_UAF:
		pr_err("BUG: KFENCE: use-after-free %s in %pS\n\n", get_access_type(is_write),
		       (void *)stack_entries[skipnr]);
		pr_err("Use-after-free %s at 0x%p (in kfence-#%td):\n",
		       get_access_type(is_write), (void *)address, object_index);
		break;
	case KFENCE_ERROR_CORRUPTION:
		pr_err("BUG: KFENCE: memory corruption in %pS\n\n", (void *)stack_entries[skipnr]);
		pr_err("Corrupted memory at 0x%p ", (void *)address);
		print_diff_canary(address, 16, meta);
		pr_cont(" (in kfence-#%td):\n", object_index);
		break;
	case KFENCE_ERROR_INVALID:
		pr_err("BUG: KFENCE: invalid %s in %pS\n\n", get_access_type(is_write),
		       (void *)stack_entries[skipnr]);
		pr_err("Invalid %s at 0x%p:\n", get_access_type(is_write),
		       (void *)address);
		break;
	case KFENCE_ERROR_INVALID_FREE:
		pr_err("BUG: KFENCE: invalid free in %pS\n\n", (void *)stack_entries[skipnr]);
		pr_err("Invalid free of 0x%p (in kfence-#%td):\n", (void *)address,
		       object_index);
		break;
	}

	/* Print stack trace and object info. */
	stack_trace_print(stack_entries + skipnr, num_stack_entries - skipnr, 0);

	if (meta) {
		pr_err("\n");
		kfence_print_object(NULL, meta);
	}

	/* Print report footer. */
	pr_err("\n");
	if (no_hash_pointers && regs)
		show_regs(regs);
	else
		dump_stack_print_info(KERN_ERR);
	trace_error_report_end(ERROR_DETECTOR_KFENCE, address);
	pr_err("==================================================================\n");

	lockdep_on();

	check_panic_on_warn("KFENCE");

	/* We encountered a memory safety error, taint the kernel! */
	add_taint(TAINT_BAD_PAGE, LOCKDEP_STILL_OK);
}

#ifdef CONFIG_PRINTK
static void kfence_to_kp_stack(const struct kfence_track *track, void **kp_stack)
{
	int i, j;

	i = get_stack_skipnr(track->stack_entries, track->num_stack_entries, NULL);
	for (j = 0; i < track->num_stack_entries && j < KS_ADDRS_COUNT; ++i, ++j)
		kp_stack[j] = (void *)track->stack_entries[i];
	if (j < KS_ADDRS_COUNT)
		kp_stack[j] = NULL;
}

bool __kfence_obj_info(struct kmem_obj_info *kpp, void *object, struct slab *slab)
{
	struct kfence_metadata *meta = addr_to_metadata((unsigned long)object);
	unsigned long flags;

	if (!meta)
		return false;

	/*
	 * If state is UNUSED at least show the pointer requested; the rest
	 * would be garbage data.
	 */
	kpp->kp_ptr = object;

	/* Requesting info an a never-used object is almost certainly a bug. */
	if (WARN_ON(meta->state == KFENCE_OBJECT_UNUSED))
		return true;

	raw_spin_lock_irqsave(&meta->lock, flags);

	kpp->kp_slab = slab;
	kpp->kp_slab_cache = meta->cache;
	kpp->kp_objp = (void *)meta->addr;
	kfence_to_kp_stack(&meta->alloc_track, kpp->kp_stack);
	if (meta->state == KFENCE_OBJECT_FREED)
		kfence_to_kp_stack(&meta->free_track, kpp->kp_free_stack);
	/* get_stack_skipnr() ensures the first entry is outside allocator. */
	kpp->kp_ret = kpp->kp_stack[0];

	raw_spin_unlock_irqrestore(&meta->lock, flags);

	return true;
}
#endif
