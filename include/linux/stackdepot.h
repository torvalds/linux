/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * A generic stack depot implementation
 *
 * Author: Alexander Potapenko <glider@google.com>
 * Copyright (C) 2016 Google, Inc.
 *
 * Based on code by Dmitry Chernenkov.
 */

#ifndef _LINUX_STACKDEPOT_H
#define _LINUX_STACKDEPOT_H

#include <linux/gfp.h>

typedef u32 depot_stack_handle_t;

depot_stack_handle_t __stack_depot_save(unsigned long *entries,
					unsigned int nr_entries,
					gfp_t gfp_flags, bool can_alloc);

/*
 * Every user of stack depot has to call stack_depot_init() during its own init
 * when it's decided that it will be calling stack_depot_save() later. This is
 * recommended for e.g. modules initialized later in the boot process, when
 * slab_is_available() is true.
 *
 * The alternative is to select STACKDEPOT_ALWAYS_INIT to have stack depot
 * enabled as part of mm_init(), for subsystems where it's known at compile time
 * that stack depot will be used.
 *
 * Another alternative is to call stack_depot_want_early_init(), when the
 * decision to use stack depot is taken e.g. when evaluating kernel boot
 * parameters, which precedes the enablement point in mm_init().
 *
 * stack_depot_init() and stack_depot_want_early_init() can be called regardless
 * of CONFIG_STACKDEPOT and are no-op when disabled. The actual save/fetch/print
 * functions should only be called from code that makes sure CONFIG_STACKDEPOT
 * is enabled.
 */
#ifdef CONFIG_STACKDEPOT
int stack_depot_init(void);

void __init stack_depot_want_early_init(void);

/* This is supposed to be called only from mm_init() */
int __init stack_depot_early_init(void);
#else
static inline int stack_depot_init(void) { return 0; }

static inline void stack_depot_want_early_init(void) { }

static inline int stack_depot_early_init(void)	{ return 0; }
#endif

depot_stack_handle_t stack_depot_save(unsigned long *entries,
				      unsigned int nr_entries, gfp_t gfp_flags);

unsigned int stack_depot_fetch(depot_stack_handle_t handle,
			       unsigned long **entries);

int stack_depot_snprint(depot_stack_handle_t handle, char *buf, size_t size,
		       int spaces);

void stack_depot_print(depot_stack_handle_t stack);

#endif
