// SPDX-License-Identifier: GPL-2.0
/*
 * KFENCE reporting.
 *
 * Copyright (C) 2020, Google LLC.
 */

#include <stdarg.h>

#include <linux/kernel.h>
#include <linux/lockdep.h>
#include <linux/printk.h>
#include <linux/seq_file.h>
#include <linux/stacktrace.h>
#include <linux/string.h>

#include <asm/kfence.h>

#include "kfence.h"

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
	bool is_access_fault = false;

	if (type) {
		/* Depending on error type, find different stack entries. */
		switch (*type) {
		case KFENCE_ERROR_UAF:
		case KFENCE_ERROR_OOB:
		case KFENCE_ERROR_INVALID:
			is_access_fault = true;
			break;
		case KFENCE_ERROR_CORRUPTION:
		case KFENCE_ERROR_INVALID_FREE:
			break;
		}
	}

	for (skipnr = 0; skipnr < num_entries; skipnr++) {
		int len = scnprintf(buf, sizeof(buf), "%ps", (void *)stack_entries[skipnr]);

		if (is_access_fault) {
			if (!strncmp(buf, KFENCE_SKIP_ARCH_FAULT_HANDLER, len))
				goto found;
		} else {
			if (str_has_prefix(buf, "kfence_") || str_has_prefix(buf, "__kfence_") ||
			    !strncmp(buf, "__slab_free", len)) {
				/*
				 * In case of tail calls from any of the below
				 * to any of the above.
				 */
				fallback = skipnr + 1;
			}

			/* Also the *_bulk() variants by only checking prefixes. */
			if (str_has_prefix(buf, "kfree") ||
			    str_has_prefix(buf, "kmem_cache_free") ||
			    str_has_prefix(buf, "__kmalloc") ||
			    str_has_prefix(buf, "kmem_cache_alloc"))
				goto found;
		}
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
		seq_con_printf(seq, "kfence-#%zd unused\n", meta - kfence_metadata);
		return;
	}

	seq_con_printf(seq,
		       "kfence-#%zd [0x" PTR_FMT "-0x" PTR_FMT
		       ", size=%d, cache=%s] allocated by task %d:\n",
		       meta - kfence_metadata, (void *)start, (void *)(start + size - 1), size,
		       (cache && cache->name) ? cache->name : "<destroyed>", meta->alloc_track.pid);
	kfence_print_stack(seq, meta, true);

	if (meta->state == KFENCE_OBJECT_FREED) {
		seq_con_printf(seq, "\nfreed by task %d:\n", meta->free_track.pid);
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
		if (*cur == KFENCE_CANARY_PATTERN(cur))
			pr_cont(" .");
		else if (IS_ENABLED(CONFIG_DEBUG_KERNEL))
			pr_cont(" 0x%02x", *cur);
		else /* Do not leak kernel memory in non-debug builds. */
			pr_cont(" !");
	}
	pr_cont(" ]");
}

void kfence_report_error(unsigned long address, const struct kfence_metadata *meta,
			 enum kfence_error_type type)
{
	unsigned long stack_entries[KFENCE_STACK_DEPTH] = { 0 };
	int num_stack_entries = stack_trace_save(stack_entries, KFENCE_STACK_DEPTH, 1);
	int skipnr = get_stack_skipnr(stack_entries, num_stack_entries, &type);
	const ptrdiff_t object_index = meta ? meta - kfence_metadata : -1;

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

		pr_err("BUG: KFENCE: out-of-bounds in %pS\n\n", (void *)stack_entries[skipnr]);
		pr_err("Out-of-bounds access at 0x" PTR_FMT " (%luB %s of kfence-#%zd):\n",
		       (void *)address,
		       left_of_object ? meta->addr - address : address - meta->addr,
		       left_of_object ? "left" : "right", object_index);
		break;
	}
	case KFENCE_ERROR_UAF:
		pr_err("BUG: KFENCE: use-after-free in %pS\n\n", (void *)stack_entries[skipnr]);
		pr_err("Use-after-free access at 0x" PTR_FMT " (in kfence-#%zd):\n",
		       (void *)address, object_index);
		break;
	case KFENCE_ERROR_CORRUPTION:
		pr_err("BUG: KFENCE: memory corruption in %pS\n\n", (void *)stack_entries[skipnr]);
		pr_err("Corrupted memory at 0x" PTR_FMT " ", (void *)address);
		print_diff_canary(address, 16, meta);
		pr_cont(" (in kfence-#%zd):\n", object_index);
		break;
	case KFENCE_ERROR_INVALID:
		pr_err("BUG: KFENCE: invalid access in %pS\n\n", (void *)stack_entries[skipnr]);
		pr_err("Invalid access at 0x" PTR_FMT ":\n", (void *)address);
		break;
	case KFENCE_ERROR_INVALID_FREE:
		pr_err("BUG: KFENCE: invalid free in %pS\n\n", (void *)stack_entries[skipnr]);
		pr_err("Invalid free of 0x" PTR_FMT " (in kfence-#%zd):\n", (void *)address,
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
	dump_stack_print_info(KERN_ERR);
	pr_err("==================================================================\n");

	lockdep_on();

	if (panic_on_warn)
		panic("panic_on_warn set ...\n");

	/* We encountered a memory unsafety error, taint the kernel! */
	add_taint(TAINT_BAD_PAGE, LOCKDEP_STILL_OK);
}
