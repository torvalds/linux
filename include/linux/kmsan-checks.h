/* SPDX-License-Identifier: GPL-2.0 */
/*
 * KMSAN checks to be used for one-off annotations in subsystems.
 *
 * Copyright (C) 2017-2022 Google LLC
 * Author: Alexander Potapenko <glider@google.com>
 *
 */

#ifndef _LINUX_KMSAN_CHECKS_H
#define _LINUX_KMSAN_CHECKS_H

#include <linux/types.h>

#ifdef CONFIG_KMSAN

/**
 * kmsan_poison_memory() - Mark the memory range as uninitialized.
 * @address: address to start with.
 * @size:    size of buffer to poison.
 * @flags:   GFP flags for allocations done by this function.
 *
 * Until other data is written to this range, KMSAN will treat it as
 * uninitialized. Error reports for this memory will reference the call site of
 * kmsan_poison_memory() as origin.
 */
void kmsan_poison_memory(const void *address, size_t size, gfp_t flags);

/**
 * kmsan_unpoison_memory() -  Mark the memory range as initialized.
 * @address: address to start with.
 * @size:    size of buffer to unpoison.
 *
 * Until other data is written to this range, KMSAN will treat it as
 * initialized.
 */
void kmsan_unpoison_memory(const void *address, size_t size);

/**
 * kmsan_check_memory() - Check the memory range for being initialized.
 * @address: address to start with.
 * @size:    size of buffer to check.
 *
 * If any piece of the given range is marked as uninitialized, KMSAN will report
 * an error.
 */
void kmsan_check_memory(const void *address, size_t size);

#else

static inline void kmsan_poison_memory(const void *address, size_t size,
				       gfp_t flags)
{
}
static inline void kmsan_unpoison_memory(const void *address, size_t size)
{
}
static inline void kmsan_check_memory(const void *address, size_t size)
{
}

#endif

#endif /* _LINUX_KMSAN_CHECKS_H */
