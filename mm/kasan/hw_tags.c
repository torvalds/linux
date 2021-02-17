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

enum kasan_arg_stacktrace {
	KASAN_ARG_STACKTRACE_DEFAULT,
	KASAN_ARG_STACKTRACE_OFF,
	KASAN_ARG_STACKTRACE_ON,
};

enum kasan_arg_fault {
	KASAN_ARG_FAULT_DEFAULT,
	KASAN_ARG_FAULT_REPORT,
	KASAN_ARG_FAULT_PANIC,
};

static enum kasan_arg kasan_arg __ro_after_init;
static enum kasan_arg_stacktrace kasan_arg_stacktrace __ro_after_init;
static enum kasan_arg_fault kasan_arg_fault __ro_after_init;

/* Whether KASAN is enabled at all. */
DEFINE_STATIC_KEY_FALSE(kasan_flag_enabled);
EXPORT_SYMBOL(kasan_flag_enabled);

/* Whether to collect alloc/free stack traces. */
DEFINE_STATIC_KEY_FALSE(kasan_flag_stacktrace);

/* Whether panic or disable tag checking on fault. */
bool kasan_flag_panic __ro_after_init;

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

/* kasan.stacktrace=off/on */
static int __init early_kasan_flag_stacktrace(char *arg)
{
	if (!arg)
		return -EINVAL;

	if (!strcmp(arg, "off"))
		kasan_arg_stacktrace = KASAN_ARG_STACKTRACE_OFF;
	else if (!strcmp(arg, "on"))
		kasan_arg_stacktrace = KASAN_ARG_STACKTRACE_ON;
	else
		return -EINVAL;

	return 0;
}
early_param("kasan.stacktrace", early_kasan_flag_stacktrace);

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

/* kasan_init_hw_tags_cpu() is called for each CPU. */
void kasan_init_hw_tags_cpu(void)
{
	/*
	 * There's no need to check that the hardware is MTE-capable here,
	 * as this function is only called for MTE-capable hardware.
	 */

	/* If KASAN is disabled via command line, don't initialize it. */
	if (kasan_arg == KASAN_ARG_OFF)
		return;

	hw_init_tags(KASAN_TAG_MAX);
	hw_enable_tagging();
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

	/* Enable KASAN. */
	static_branch_enable(&kasan_flag_enabled);

	switch (kasan_arg_stacktrace) {
	case KASAN_ARG_STACKTRACE_DEFAULT:
		/*
		 * Default to enabling stack trace collection for
		 * debug kernels.
		 */
		if (IS_ENABLED(CONFIG_DEBUG_KERNEL))
			static_branch_enable(&kasan_flag_stacktrace);
		break;
	case KASAN_ARG_STACKTRACE_OFF:
		/* Do nothing, kasan_flag_stacktrace keeps its default value. */
		break;
	case KASAN_ARG_STACKTRACE_ON:
		static_branch_enable(&kasan_flag_stacktrace);
		break;
	}

	switch (kasan_arg_fault) {
	case KASAN_ARG_FAULT_DEFAULT:
		/*
		 * Default to no panic on report.
		 * Do nothing, kasan_flag_panic keeps its default value.
		 */
		break;
	case KASAN_ARG_FAULT_REPORT:
		/* Do nothing, kasan_flag_panic keeps its default value. */
		break;
	case KASAN_ARG_FAULT_PANIC:
		/* Enable panic on report. */
		kasan_flag_panic = true;
		break;
	}

	pr_info("KernelAddressSanitizer initialized\n");
}

void kasan_set_free_info(struct kmem_cache *cache,
				void *object, u8 tag)
{
	struct kasan_alloc_meta *alloc_meta;

	alloc_meta = kasan_get_alloc_meta(cache, object);
	if (alloc_meta)
		kasan_set_track(&alloc_meta->free_track[0], GFP_NOWAIT);
}

struct kasan_track *kasan_get_free_track(struct kmem_cache *cache,
				void *object, u8 tag)
{
	struct kasan_alloc_meta *alloc_meta;

	alloc_meta = kasan_get_alloc_meta(cache, object);
	if (!alloc_meta)
		return NULL;

	return &alloc_meta->free_track[0];
}
