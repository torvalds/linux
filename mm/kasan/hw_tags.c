// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains core hardware tag-based KASAN code.
 *
 * Copyright (c) 2020 Google, Inc.
 * Author: Andrey Konovalov <andreyknvl@google.com>
 */

#define pr_fmt(fmt) "kasan: " fmt

#include <linux/init.h>
#include <linux/kasan.h>
#include <linux/kernel.h>
#include <linux/memory.h>
#include <linux/mm.h>
#include <linux/static_key.h>
#include <linux/string.h>
#include <linux/types.h>

#include "kasan.h"

enum kasan_arg {
	KASAN_ARG_DEFAULT,
	KASAN_ARG_OFF,
	KASAN_ARG_ON,
};

enum kasan_arg_mode {
	KASAN_ARG_MODE_DEFAULT,
	KASAN_ARG_MODE_SYNC,
	KASAN_ARG_MODE_ASYNC,
	KASAN_ARG_MODE_ASYMM,
};

enum kasan_arg_vmalloc {
	KASAN_ARG_VMALLOC_DEFAULT,
	KASAN_ARG_VMALLOC_OFF,
	KASAN_ARG_VMALLOC_ON,
};

static enum kasan_arg kasan_arg __ro_after_init;
static enum kasan_arg_mode kasan_arg_mode __ro_after_init;
static enum kasan_arg_vmalloc kasan_arg_vmalloc __initdata;

/*
 * Whether KASAN is enabled at all.
 * The value remains false until KASAN is initialized by kasan_init_hw_tags().
 */
DEFINE_STATIC_KEY_FALSE(kasan_flag_enabled);
EXPORT_SYMBOL(kasan_flag_enabled);

/*
 * Whether the selected mode is synchronous, asynchronous, or asymmetric.
 * Defaults to KASAN_MODE_SYNC.
 */
enum kasan_mode kasan_mode __ro_after_init;
EXPORT_SYMBOL_GPL(kasan_mode);

/* Whether to enable vmalloc tagging. */
DEFINE_STATIC_KEY_TRUE(kasan_flag_vmalloc);

/* kasan=off/on */
static int __init early_kasan_flag(char *arg)
{
	if (!arg)
		return -EINVAL;

	if (!strcmp(arg, "off"))
		kasan_arg = KASAN_ARG_OFF;
	else if (!strcmp(arg, "on"))
		kasan_arg = KASAN_ARG_ON;
	else
		return -EINVAL;

	return 0;
}
early_param("kasan", early_kasan_flag);

/* kasan.mode=sync/async/asymm */
static int __init early_kasan_mode(char *arg)
{
	if (!arg)
		return -EINVAL;

	if (!strcmp(arg, "sync"))
		kasan_arg_mode = KASAN_ARG_MODE_SYNC;
	else if (!strcmp(arg, "async"))
		kasan_arg_mode = KASAN_ARG_MODE_ASYNC;
	else if (!strcmp(arg, "asymm"))
		kasan_arg_mode = KASAN_ARG_MODE_ASYMM;
	else
		return -EINVAL;

	return 0;
}
early_param("kasan.mode", early_kasan_mode);

/* kasan.vmalloc=off/on */
static int __init early_kasan_flag_vmalloc(char *arg)
{
	if (!arg)
		return -EINVAL;

	if (!strcmp(arg, "off"))
		kasan_arg_vmalloc = KASAN_ARG_VMALLOC_OFF;
	else if (!strcmp(arg, "on"))
		kasan_arg_vmalloc = KASAN_ARG_VMALLOC_ON;
	else
		return -EINVAL;

	return 0;
}
early_param("kasan.vmalloc", early_kasan_flag_vmalloc);

static inline const char *kasan_mode_info(void)
{
	if (kasan_mode == KASAN_MODE_ASYNC)
		return "async";
	else if (kasan_mode == KASAN_MODE_ASYMM)
		return "asymm";
	else
		return "sync";
}

/*
 * kasan_init_hw_tags_cpu() is called for each CPU.
 * Not marked as __init as a CPU can be hot-plugged after boot.
 */
void kasan_init_hw_tags_cpu(void)
{
	/*
	 * There's no need to check that the hardware is MTE-capable here,
	 * as this function is only called for MTE-capable hardware.
	 */

	/*
	 * If KASAN is disabled via command line, don't initialize it.
	 * When this function is called, kasan_flag_enabled is not yet
	 * set by kasan_init_hw_tags(). Thus, check kasan_arg instead.
	 */
	if (kasan_arg == KASAN_ARG_OFF)
		return;

	/*
	 * Enable async or asymm modes only when explicitly requested
	 * through the command line.
	 */
	kasan_enable_tagging();
}

/* kasan_init_hw_tags() is called once on boot CPU. */
void __init kasan_init_hw_tags(void)
{
	/* If hardware doesn't support MTE, don't initialize KASAN. */
	if (!system_supports_mte())
		return;

	/* If KASAN is disabled via command line, don't initialize it. */
	if (kasan_arg == KASAN_ARG_OFF)
		return;

	switch (kasan_arg_mode) {
	case KASAN_ARG_MODE_DEFAULT:
		/* Default is specified by kasan_mode definition. */
		break;
	case KASAN_ARG_MODE_SYNC:
		kasan_mode = KASAN_MODE_SYNC;
		break;
	case KASAN_ARG_MODE_ASYNC:
		kasan_mode = KASAN_MODE_ASYNC;
		break;
	case KASAN_ARG_MODE_ASYMM:
		kasan_mode = KASAN_MODE_ASYMM;
		break;
	}

	switch (kasan_arg_vmalloc) {
	case KASAN_ARG_VMALLOC_DEFAULT:
		/* Default is specified by kasan_flag_vmalloc definition. */
		break;
	case KASAN_ARG_VMALLOC_OFF:
		static_branch_disable(&kasan_flag_vmalloc);
		break;
	case KASAN_ARG_VMALLOC_ON:
		static_branch_enable(&kasan_flag_vmalloc);
		break;
	}

	kasan_init_tags();

	/* KASAN is now initialized, enable it. */
	static_branch_enable(&kasan_flag_enabled);

	pr_info("KernelAddressSanitizer initialized (hw-tags, mode=%s, vmalloc=%s, stacktrace=%s)\n",
		kasan_mode_info(),
		kasan_vmalloc_enabled() ? "on" : "off",
		kasan_stack_collection_enabled() ? "on" : "off");
}

#ifdef CONFIG_KASAN_VMALLOC

static void unpoison_vmalloc_pages(const void *addr, u8 tag)
{
	struct vm_struct *area;
	int i;

	/*
	 * As hardware tag-based KASAN only tags VM_ALLOC vmalloc allocations
	 * (see the comment in __kasan_unpoison_vmalloc), all of the pages
	 * should belong to a single area.
	 */
	area = find_vm_area((void *)addr);
	if (WARN_ON(!area))
		return;

	for (i = 0; i < area->nr_pages; i++) {
		struct page *page = area->pages[i];

		page_kasan_tag_set(page, tag);
	}
}

static void init_vmalloc_pages(const void *start, unsigned long size)
{
	const void *addr;

	for (addr = start; addr < start + size; addr += PAGE_SIZE) {
		struct page *page = vmalloc_to_page(addr);

		clear_highpage_kasan_tagged(page);
	}
}

void *__kasan_unpoison_vmalloc(const void *start, unsigned long size,
				kasan_vmalloc_flags_t flags)
{
	u8 tag;
	unsigned long redzone_start, redzone_size;

	if (!kasan_vmalloc_enabled()) {
		if (flags & KASAN_VMALLOC_INIT)
			init_vmalloc_pages(start, size);
		return (void *)start;
	}

	/*
	 * Don't tag non-VM_ALLOC mappings, as:
	 *
	 * 1. Unlike the software KASAN modes, hardware tag-based KASAN only
	 *    supports tagging physical memory. Therefore, it can only tag a
	 *    single mapping of normal physical pages.
	 * 2. Hardware tag-based KASAN can only tag memory mapped with special
	 *    mapping protection bits, see arch_vmap_pgprot_tagged().
	 *    As non-VM_ALLOC mappings can be mapped outside of vmalloc code,
	 *    providing these bits would require tracking all non-VM_ALLOC
	 *    mappers.
	 *
	 * Thus, for VM_ALLOC mappings, hardware tag-based KASAN only tags
	 * the first virtual mapping, which is created by vmalloc().
	 * Tagging the page_alloc memory backing that vmalloc() allocation is
	 * skipped, see ___GFP_SKIP_KASAN_UNPOISON.
	 *
	 * For non-VM_ALLOC allocations, page_alloc memory is tagged as usual.
	 */
	if (!(flags & KASAN_VMALLOC_VM_ALLOC)) {
		WARN_ON(flags & KASAN_VMALLOC_INIT);
		return (void *)start;
	}

	/*
	 * Don't tag executable memory.
	 * The kernel doesn't tolerate having the PC register tagged.
	 */
	if (!(flags & KASAN_VMALLOC_PROT_NORMAL)) {
		WARN_ON(flags & KASAN_VMALLOC_INIT);
		return (void *)start;
	}

	tag = kasan_random_tag();
	start = set_tag(start, tag);

	/* Unpoison and initialize memory up to size. */
	kasan_unpoison(start, size, flags & KASAN_VMALLOC_INIT);

	/*
	 * Explicitly poison and initialize the in-page vmalloc() redzone.
	 * Unlike software KASAN modes, hardware tag-based KASAN doesn't
	 * unpoison memory when populating shadow for vmalloc() space.
	 */
	redzone_start = round_up((unsigned long)start + size,
				 KASAN_GRANULE_SIZE);
	redzone_size = round_up(redzone_start, PAGE_SIZE) - redzone_start;
	kasan_poison((void *)redzone_start, redzone_size, KASAN_TAG_INVALID,
		     flags & KASAN_VMALLOC_INIT);

	/*
	 * Set per-page tag flags to allow accessing physical memory for the
	 * vmalloc() mapping through page_address(vmalloc_to_page()).
	 */
	unpoison_vmalloc_pages(start, tag);

	return (void *)start;
}

void __kasan_poison_vmalloc(const void *start, unsigned long size)
{
	/*
	 * No tagging here.
	 * The physical pages backing the vmalloc() allocation are poisoned
	 * through the usual page_alloc paths.
	 */
}

#endif

void kasan_enable_tagging(void)
{
	if (kasan_arg_mode == KASAN_ARG_MODE_ASYNC)
		hw_enable_tagging_async();
	else if (kasan_arg_mode == KASAN_ARG_MODE_ASYMM)
		hw_enable_tagging_asymm();
	else
		hw_enable_tagging_sync();
}

#if IS_ENABLED(CONFIG_KASAN_KUNIT_TEST)

EXPORT_SYMBOL_GPL(kasan_enable_tagging);

void kasan_force_async_fault(void)
{
	hw_force_async_tag_fault();
}
EXPORT_SYMBOL_GPL(kasan_force_async_fault);

#endif
