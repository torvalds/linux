/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _MEMBLOCK_TEST_H
#define _MEMBLOCK_TEST_H

#include <stdlib.h>
#include <assert.h>
#include <linux/types.h>
#include <linux/memblock.h>
#include <linux/sizes.h>

#define MEM_SIZE SZ_16K

/*
 * Available memory registered with memblock needs to be valid for allocs
 * test to run. This is a convenience wrapper for memory allocated in
 * dummy_physical_memory_init() that is later registered with memblock
 * in setup_memblock().
 */
struct test_memory {
	void *base;
};

struct region {
	phys_addr_t base;
	phys_addr_t size;
};

void reset_memblock_regions(void);
void reset_memblock_attributes(void);
void setup_memblock(void);
void dummy_physical_memory_init(void);
void dummy_physical_memory_cleanup(void);

#endif
