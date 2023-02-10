/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Stack depot - a stack trace storage that avoids duplication.
 *
 * Author: Alexander Potapenko <glider@google.com>
 * Copyright (C) 2016 Google, Inc.
 *
 * Based on the code by Dmitry Chernenkov.
 */

#ifndef _LINUX_STACKDEPOT_H
#define _LINUX_STACKDEPOT_H

#include <linux/gfp.h>

typedef u32 depot_stack_handle_t;

/*
 * Number of bits in the handle that stack depot doesn't use. Users may store
 * information in them via stack_depot_set/get_extra_bits.
 */
#define STACK_DEPOT_EXTRA_BITS 5

/*
 * Using stack depot requires its initialization, which can be done in 3 ways:
 *
 * 1. Selecting CONFIG_STACKDEPOT_ALWAYS_INIT. This option is suitable in
 *    scenarios where it's known at compile time that stack depot will be used.
 *    Enabling this config makes the kernel initialize stack depot in mm_init().
 *
 * 2. Calling stack_depot_request_early_init() during early boot, before
 *    stack_depot_early_init() in mm_init() completes. For example, this can
 *    be done when evaluating kernel boot parameters.
 *
 * 3. Calling stack_depot_init(). Possible after boot is complete. This option
 *    is recommended for modules initialized later in the boot process, after
 *    mm_init() completes.
 *
 * stack_depot_init() and stack_depot_request_early_init() can be called
 * regardless of whether CONFIG_STACKDEPOT is enabled and are no-op when this
 * config is disabled. The save/fetch/print stack depot functions can only be
 * called from the code that makes sure CONFIG_STACKDEPOT is enabled _and_
 * initializes stack depot via one of the ways listed above.
 */
#ifdef CONFIG_STACKDEPOT
int stack_depot_init(void);

void __init stack_depot_request_early_init(void);

/* Must be only called from mm_init(). */
int __init stack_depot_early_init(void);
#else
static inline int stack_depot_init(void) { return 0; }

static inline void stack_depot_request_early_init(void) { }

static inline int stack_depot_early_init(void)	{ return 0; }
#endif

depot_stack_handle_t __stack_depot_save(unsigned long *entries,
					unsigned int nr_entries,
					gfp_t gfp_flags, bool can_alloc);

depot_stack_handle_t stack_depot_save(unsigned long *entries,
				      unsigned int nr_entries, gfp_t gfp_flags);

unsigned int stack_depot_fetch(depot_stack_handle_t handle,
			       unsigned long **entries);

void stack_depot_print(depot_stack_handle_t stack);

int stack_depot_snprint(depot_stack_handle_t handle, char *buf, size_t size,
		       int spaces);

depot_stack_handle_t __must_check stack_depot_set_extra_bits(
			depot_stack_handle_t handle, unsigned int extra_bits);

unsigned int stack_depot_get_extra_bits(depot_stack_handle_t handle);

#endif
