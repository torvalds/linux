/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _MEMBLOCK_TEST_H
#define _MEMBLOCK_TEST_H

#include <stdlib.h>
#include <assert.h>
#include <linux/types.h>
#include <linux/memblock.h>
#include <linux/sizes.h>
#include <linux/printk.h>
#include <../selftests/kselftest.h>

#define MEM_SIZE SZ_16K

/**
 * ASSERT_EQ():
 * Check the condition
 * @_expected == @_seen
 * If false, print failed test message (if in VERBOSE mode) and then assert
 */
#define ASSERT_EQ(_expected, _seen) do { \
	if ((_expected) != (_seen)) \
		test_fail(); \
	assert((_expected) == (_seen)); \
} while (0)

/**
 * ASSERT_NE():
 * Check the condition
 * @_expected != @_seen
 * If false, print failed test message (if in VERBOSE mode) and then assert
 */
#define ASSERT_NE(_expected, _seen) do { \
	if ((_expected) == (_seen)) \
		test_fail(); \
	assert((_expected) != (_seen)); \
} while (0)

/**
 * ASSERT_LT():
 * Check the condition
 * @_expected < @_seen
 * If false, print failed test message (if in VERBOSE mode) and then assert
 */
#define ASSERT_LT(_expected, _seen) do { \
	if ((_expected) >= (_seen)) \
		test_fail(); \
	assert((_expected) < (_seen)); \
} while (0)

#define PREFIX_PUSH() prefix_push(__func__)

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
void parse_args(int argc, char **argv);

void test_fail(void);
void test_pass(void);
void test_print(const char *fmt, ...);
void prefix_reset(void);
void prefix_push(const char *prefix);
void prefix_pop(void);

static inline void test_pass_pop(void)
{
	test_pass();
	prefix_pop();
}

#endif
