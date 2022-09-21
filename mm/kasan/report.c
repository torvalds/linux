// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains common KASAN error reporting code.
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 * Author: Andrey Ryabinin <ryabinin.a.a@gmail.com>
 *
 * Some code borrowed from https://github.com/xairy/kasan-prototype by
 *        Andrey Konovalov <andreyknvl@gmail.com>
 */

#include <linux/bitops.h>
#include <linux/ftrace.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/lockdep.h>
#include <linux/mm.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/stackdepot.h>
#include <linux/stacktrace.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/kasan.h>
#include <linux/module.h>
#include <linux/sched/task_stack.h>
#include <linux/uaccess.h>
#include <trace/events/error_report.h>

#include <asm/sections.h>

#include <kunit/test.h>

#include "kasan.h"
#include "../slab.h"

static unsigned long kasan_flags;

#define KASAN_BIT_REPORTED	0
#define KASAN_BIT_MULTI_SHOT	1

enum kasan_arg_fault {
	KASAN_ARG_FAULT_DEFAULT,
	KASAN_ARG_FAULT_REPORT,
	KASAN_ARG_FAULT_PANIC,
};

static enum kasan_arg_fault kasan_arg_fault __ro_after_init = KASAN_ARG_FAULT_DEFAULT;

/* kasan.fault=report/panic */
static int __init early_kasan_fault(char *arg)
{
	if (!arg)
		return -EINVAL;

	if (!strcmp(arg, "report"))
		kasan_arg_fault = KASAN_ARG_FAULT_REPORT;
	else if (!strcmp(arg, "panic"))
		kasan_arg_fault = KASAN_ARG_FAULT_PANIC;
	else
		return -EINVAL;

	return 0;
}
early_param("kasan.fault", early_kasan_fault);

static int __init kasan_set_multi_shot(char *str)
{
	set_bit(KASAN_BIT_MULTI_SHOT, &kasan_flags);
	return 1;
}
__setup("kasan_multi_shot", kasan_set_multi_shot);

/*
 * Used to suppress reports within kasan_disable/enable_current() critical
 * sections, which are used for marking accesses to slab metadata.
 */
static bool report_suppressed(void)
{
#if defined(CONFIG_KASAN_GENERIC) || defined(CONFIG_KASAN_SW_TAGS)
	if (current->kasan_depth)
		return true;
#endif
	return false;
}

/*
 * Used to avoid reporting more than one KASAN bug unless kasan_multi_shot
 * is enabled. Note that KASAN tests effectively enable kasan_multi_shot
 * for their duration.
 */
static bool report_enabled(void)
{
	if (test_bit(KASAN_BIT_MULTI_SHOT, &kasan_flags))
		return true;
	return !test_and_set_bit(KASAN_BIT_REPORTED, &kasan_flags);
}

#if IS_ENABLED(CONFIG_KASAN_KUNIT_TEST) || IS_ENABLED(CONFIG_KASAN_MODULE_TEST)

bool kasan_save_enable_multi_shot(void)
{
	return test_and_set_bit(KASAN_BIT_MULTI_SHOT, &kasan_flags);
}
EXPORT_SYMBOL_GPL(kasan_save_enable_multi_shot);

void kasan_restore_multi_shot(bool enabled)
{
	if (!enabled)
		clear_bit(KASAN_BIT_MULTI_SHOT, &kasan_flags);
}
EXPORT_SYMBOL_GPL(kasan_restore_multi_shot);

#endif

#if IS_ENABLED(CONFIG_KASAN_KUNIT_TEST)
static void update_kunit_status(bool sync)
{
	struct kunit *test;
	struct kunit_resource *resource;
	struct kunit_kasan_status *status;

	test = current->kunit_test;
	if (!test)
		return;

	resource = kunit_find_named_resource(test, "kasan_status");
	if (!resource) {
		kunit_set_failure(test);
		return;
	}

	status = (struct kunit_kasan_status *)resource->data;
	WRITE_ONCE(status->report_found, true);
	WRITE_ONCE(status->sync_fault, sync);

	kunit_put_resource(resource);
}
#else
static void update_kunit_status(bool sync) { }
#endif

static DEFINE_SPINLOCK(report_lock);

static void start_report(unsigned long *flags, bool sync)
{
	/* Respect the /proc/sys/kernel/traceoff_on_warning interface. */
	disable_trace_on_warning();
	/* Update status of the currently running KASAN test. */
	update_kunit_status(sync);
	/* Do not allow LOCKDEP mangling KASAN reports. */
	lockdep_off();
	/* Make sure we don't end up in loop. */
	kasan_disable_current();
	spin_lock_irqsave(&report_lock, *flags);
	pr_err("==================================================================\n");
}

static void end_report(unsigned long *flags, void *addr)
{
	if (addr)
		trace_error_report_end(ERROR_DETECTOR_KASAN,
				       (unsigned long)addr);
	pr_err("==================================================================\n");
	spin_unlock_irqrestore(&report_lock, *flags);
	if (panic_on_warn && !test_bit(KASAN_BIT_MULTI_SHOT, &kasan_flags))
		panic("panic_on_warn set ...\n");
	if (kasan_arg_fault == KASAN_ARG_FAULT_PANIC)
		panic("kasan.fault=panic set ...\n");
	add_taint(TAINT_BAD_PAGE, LOCKDEP_NOW_UNRELIABLE);
	lockdep_on();
	kasan_enable_current();
}

static void print_error_description(struct kasan_report_info *info)
{
	if (info->type == KASAN_REPORT_INVALID_FREE) {
		pr_err("BUG: KASAN: invalid-free in %pS\n", (void *)info->ip);
		return;
	}

	if (info->type == KASAN_REPORT_DOUBLE_FREE) {
		pr_err("BUG: KASAN: double-free in %pS\n", (void *)info->ip);
		return;
	}

	pr_err("BUG: KASAN: %s in %pS\n",
		kasan_get_bug_type(info), (void *)info->ip);
	if (info->access_size)
		pr_err("%s of size %zu at addr %px by task %s/%d\n",
			info->is_write ? "Write" : "Read", info->access_size,
			info->access_addr, current->comm, task_pid_nr(current));
	else
		pr_err("%s at addr %px by task %s/%d\n",
			info->is_write ? "Write" : "Read",
			info->access_addr, current->comm, task_pid_nr(current));
}

static void print_track(struct kasan_track *track, const char *prefix)
{
	pr_err("%s by task %u:\n", prefix, track->pid);
	if (track->stack) {
		stack_depot_print(track->stack);
	} else {
		pr_err("(stack is not available)\n");
	}
}

struct page *kasan_addr_to_page(const void *addr)
{
	if ((addr >= (void *)PAGE_OFFSET) &&
			(addr < high_memory))
		return virt_to_head_page(addr);
	return NULL;
}

struct slab *kasan_addr_to_slab(const void *addr)
{
	if ((addr >= (void *)PAGE_OFFSET) &&
			(addr < high_memory))
		return virt_to_slab(addr);
	return NULL;
}

static void describe_object_addr(struct kmem_cache *cache, void *object,
				const void *addr)
{
	unsigned long access_addr = (unsigned long)addr;
	unsigned long object_addr = (unsigned long)object;
	const char *rel_type;
	int rel_bytes;

	pr_err("The buggy address belongs to the object at %px\n"
	       " which belongs to the cache %s of size %d\n",
		object, cache->name, cache->object_size);

	if (access_addr < object_addr) {
		rel_type = "to the left";
		rel_bytes = object_addr - access_addr;
	} else if (access_addr >= object_addr + cache->object_size) {
		rel_type = "to the right";
		rel_bytes = access_addr - (object_addr + cache->object_size);
	} else {
		rel_type = "inside";
		rel_bytes = access_addr - object_addr;
	}

	pr_err("The buggy address is located %d bytes %s of\n"
	       " %d-byte region [%px, %px)\n",
		rel_bytes, rel_type, cache->object_size, (void *)object_addr,
		(void *)(object_addr + cache->object_size));
}

static void describe_object_stacks(struct kmem_cache *cache, void *object,
					const void *addr, u8 tag)
{
	struct kasan_alloc_meta *alloc_meta;
	struct kasan_track *free_track;

	alloc_meta = kasan_get_alloc_meta(cache, object);
	if (alloc_meta) {
		print_track(&alloc_meta->alloc_track, "Allocated");
		pr_err("\n");
	}

	free_track = kasan_get_free_track(cache, object, tag);
	if (free_track) {
		print_track(free_track, "Freed");
		pr_err("\n");
	}

#ifdef CONFIG_KASAN_GENERIC
	if (!alloc_meta)
		return;
	if (alloc_meta->aux_stack[0]) {
		pr_err("Last potentially related work creation:\n");
		stack_depot_print(alloc_meta->aux_stack[0]);
		pr_err("\n");
	}
	if (alloc_meta->aux_stack[1]) {
		pr_err("Second to last potentially related work creation:\n");
		stack_depot_print(alloc_meta->aux_stack[1]);
		pr_err("\n");
	}
#endif
}

static void describe_object(struct kmem_cache *cache, void *object,
				const void *addr, u8 tag)
{
	if (kasan_stack_collection_enabled())
		describe_object_stacks(cache, object, addr, tag);
	describe_object_addr(cache, object, addr);
}

static inline bool kernel_or_module_addr(const void *addr)
{
	if (is_kernel((unsigned long)addr))
		return true;
	if (is_module_address((unsigned long)addr))
		return true;
	return false;
}

static inline bool init_task_stack_addr(const void *addr)
{
	return addr >= (void *)&init_thread_union.stack &&
		(addr <= (void *)&init_thread_union.stack +
			sizeof(init_thread_union.stack));
}

static void print_address_description(void *addr, u8 tag)
{
	struct page *page = kasan_addr_to_page(addr);

	dump_stack_lvl(KERN_ERR);
	pr_err("\n");

	if (page && PageSlab(page)) {
		struct slab *slab = page_slab(page);
		struct kmem_cache *cache = slab->slab_cache;
		void *object = nearest_obj(cache, slab,	addr);

		describe_object(cache, object, addr, tag);
		pr_err("\n");
	}

	if (kernel_or_module_addr(addr) && !init_task_stack_addr(addr)) {
		pr_err("The buggy address belongs to the variable:\n");
		pr_err(" %pS\n", addr);
		pr_err("\n");
	}

	if (object_is_on_stack(addr)) {
		/*
		 * Currently, KASAN supports printing frame information only
		 * for accesses to the task's own stack.
		 */
		kasan_print_address_stack_frame(addr);
		pr_err("\n");
	}

	if (is_vmalloc_addr(addr)) {
		struct vm_struct *va = find_vm_area(addr);

		if (va) {
			pr_err("The buggy address belongs to the virtual mapping at\n"
			       " [%px, %px) created by:\n"
			       " %pS\n",
			       va->addr, va->addr + va->size, va->caller);
			pr_err("\n");

			page = vmalloc_to_page(addr);
		}
	}

	if (page) {
		pr_err("The buggy address belongs to the physical page:\n");
		dump_page(page, "kasan: bad access detected");
		pr_err("\n");
	}
}

static bool meta_row_is_guilty(const void *row, const void *addr)
{
	return (row <= addr) && (addr < row + META_MEM_BYTES_PER_ROW);
}

static int meta_pointer_offset(const void *row, const void *addr)
{
	/*
	 * Memory state around the buggy address:
	 *  ff00ff00ff00ff00: 00 00 00 05 fe fe fe fe fe fe fe fe fe fe fe fe
	 *  ...
	 *
	 * The length of ">ff00ff00ff00ff00: " is
	 *    3 + (BITS_PER_LONG / 8) * 2 chars.
	 * The length of each granule metadata is 2 bytes
	 *    plus 1 byte for space.
	 */
	return 3 + (BITS_PER_LONG / 8) * 2 +
		(addr - row) / KASAN_GRANULE_SIZE * 3 + 1;
}

static void print_memory_metadata(const void *addr)
{
	int i;
	void *row;

	row = (void *)round_down((unsigned long)addr, META_MEM_BYTES_PER_ROW)
			- META_ROWS_AROUND_ADDR * META_MEM_BYTES_PER_ROW;

	pr_err("Memory state around the buggy address:\n");

	for (i = -META_ROWS_AROUND_ADDR; i <= META_ROWS_AROUND_ADDR; i++) {
		char buffer[4 + (BITS_PER_LONG / 8) * 2];
		char metadata[META_BYTES_PER_ROW];

		snprintf(buffer, sizeof(buffer),
				(i == 0) ? ">%px: " : " %px: ", row);

		/*
		 * We should not pass a shadow pointer to generic
		 * function, because generic functions may try to
		 * access kasan mapping for the passed address.
		 */
		kasan_metadata_fetch_row(&metadata[0], row);

		print_hex_dump(KERN_ERR, buffer,
			DUMP_PREFIX_NONE, META_BYTES_PER_ROW, 1,
			metadata, META_BYTES_PER_ROW, 0);

		if (meta_row_is_guilty(row, addr))
			pr_err("%*c\n", meta_pointer_offset(row, addr), '^');

		row += META_MEM_BYTES_PER_ROW;
	}
}

static void print_report(struct kasan_report_info *info)
{
	void *tagged_addr = info->access_addr;
	void *untagged_addr = kasan_reset_tag(tagged_addr);
	u8 tag = get_tag(tagged_addr);

	print_error_description(info);
	if (addr_has_metadata(untagged_addr))
		kasan_print_tags(tag, info->first_bad_addr);
	pr_err("\n");

	if (addr_has_metadata(untagged_addr)) {
		print_address_description(untagged_addr, tag);
		print_memory_metadata(info->first_bad_addr);
	} else {
		dump_stack_lvl(KERN_ERR);
	}
}

void kasan_report_invalid_free(void *ptr, unsigned long ip, enum kasan_report_type type)
{
	unsigned long flags;
	struct kasan_report_info info;

	/*
	 * Do not check report_suppressed(), as an invalid-free cannot be
	 * caused by accessing slab metadata and thus should not be
	 * suppressed by kasan_disable/enable_current() critical sections.
	 */
	if (unlikely(!report_enabled()))
		return;

	start_report(&flags, true);

	info.type = type;
	info.access_addr = ptr;
	info.first_bad_addr = kasan_reset_tag(ptr);
	info.access_size = 0;
	info.is_write = false;
	info.ip = ip;

	print_report(&info);

	end_report(&flags, ptr);
}

/*
 * kasan_report() is the only reporting function that uses
 * user_access_save/restore(): kasan_report_invalid_free() cannot be called
 * from a UACCESS region, and kasan_report_async() is not used on x86.
 */
bool kasan_report(unsigned long addr, size_t size, bool is_write,
			unsigned long ip)
{
	bool ret = true;
	void *ptr = (void *)addr;
	unsigned long ua_flags = user_access_save();
	unsigned long irq_flags;
	struct kasan_report_info info;

	if (unlikely(report_suppressed()) || unlikely(!report_enabled())) {
		ret = false;
		goto out;
	}

	start_report(&irq_flags, true);

	info.type = KASAN_REPORT_ACCESS;
	info.access_addr = ptr;
	info.first_bad_addr = kasan_find_first_bad_addr(ptr, size);
	info.access_size = size;
	info.is_write = is_write;
	info.ip = ip;

	print_report(&info);

	end_report(&irq_flags, ptr);

out:
	user_access_restore(ua_flags);

	return ret;
}

#ifdef CONFIG_KASAN_HW_TAGS
void kasan_report_async(void)
{
	unsigned long flags;

	/*
	 * Do not check report_suppressed(), as kasan_disable/enable_current()
	 * critical sections do not affect Hardware Tag-Based KASAN.
	 */
	if (unlikely(!report_enabled()))
		return;

	start_report(&flags, false);
	pr_err("BUG: KASAN: invalid-access\n");
	pr_err("Asynchronous fault: no details available\n");
	pr_err("\n");
	dump_stack_lvl(KERN_ERR);
	end_report(&flags, NULL);
}
#endif /* CONFIG_KASAN_HW_TAGS */

#ifdef CONFIG_KASAN_INLINE
/*
 * With CONFIG_KASAN_INLINE, accesses to bogus pointers (outside the high
 * canonical half of the address space) cause out-of-bounds shadow memory reads
 * before the actual access. For addresses in the low canonical half of the
 * address space, as well as most non-canonical addresses, that out-of-bounds
 * shadow memory access lands in the non-canonical part of the address space.
 * Help the user figure out what the original bogus pointer was.
 */
void kasan_non_canonical_hook(unsigned long addr)
{
	unsigned long orig_addr;
	const char *bug_type;

	if (addr < KASAN_SHADOW_OFFSET)
		return;

	orig_addr = (addr - KASAN_SHADOW_OFFSET) << KASAN_SHADOW_SCALE_SHIFT;
	/*
	 * For faults near the shadow address for NULL, we can be fairly certain
	 * that this is a KASAN shadow memory access.
	 * For faults that correspond to shadow for low canonical addresses, we
	 * can still be pretty sure - that shadow region is a fairly narrow
	 * chunk of the non-canonical address space.
	 * But faults that look like shadow for non-canonical addresses are a
	 * really large chunk of the address space. In that case, we still
	 * print the decoded address, but make it clear that this is not
	 * necessarily what's actually going on.
	 */
	if (orig_addr < PAGE_SIZE)
		bug_type = "null-ptr-deref";
	else if (orig_addr < TASK_SIZE)
		bug_type = "probably user-memory-access";
	else
		bug_type = "maybe wild-memory-access";
	pr_alert("KASAN: %s in range [0x%016lx-0x%016lx]\n", bug_type,
		 orig_addr, orig_addr + KASAN_GRANULE_SIZE - 1);
}
#endif
