/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _MEMBLOCK_TEST_H
#define _MEMBLOCK_TEST_H

#include <stdlib.h>
#include <assert.h>
#include <linux/types.h>
#include <linux/seq_file.h>
#include <linux/memblock.h>
#include <linux/sizes.h>
#include <linux/printk.h>
#include <../selftests/kselftest.h>

#define MEM_SIZE		SZ_16K
#define NUMA_NODES		8

enum test_flags {
	/* No special request. */
	TEST_F_NONE = 0x0,
	/* Perform raw allocations (no zeroing of memory). */
	TEST_F_RAW = 0x1,
};

/**
 * ASSERT_EQ():
 * Check the condition
 * @_expected == @_seen
 * If false, print failed test message (if running with --verbose) and then
 * assert.
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
 * If false, print failed test message (if running with --verbose) and then
 * assert.
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
 * If false, print failed test message (if running with --verbose) and then
 * assert.
 */
#define ASSERT_LT(_expected, _seen) do { \
	if ((_expected) >= (_seen)) \
		test_fail(); \
	assert((_expected) < (_seen)); \
} while (0)

/**
 * ASSERT_LE():
 * Check the condition
 * @_expected <= @_seen
 * If false, print failed test message (if running with --verbose) and then
 * assert.
 */
#define ASSERT_LE(_expected, _seen) do { \
	if ((_expected) > (_seen)) \
		test_fail(); \
	assert((_expected) <= (_seen)); \
} while (0)

/**
 * ASSERT_MEM_EQ():
 * Check that the first @_size bytes of @_seen are all equal to @_expected.
 * If false, print failed test message (if running with --verbose) and then
 * assert.
 */
#define ASSERT_MEM_EQ(_seen, _expected, _size) do { \
	for (int _i = 0; _i < (_size); _i++) { \
		ASSERT_EQ(((char *)_seen)[_i], (_expected)); \
	} \
} while (0)

/**
 * ASSERT_MEM_NE():
 * Check that none of the first @_size bytes of @_seen are equal to @_expected.
 * If false, print failed test message (if running with --verbose) and then
 * assert.
 */
#define ASSERT_MEM_NE(_seen, _expected, _size) do { \
	for (int _i = 0; _i < (_size); _i++) { \
		ASSERT_NE(((char *)_seen)[_i], (_expected)); \
	} \
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

static inline phys_addr_t __maybe_unused region_end(struct memblock_region *rgn)
{
	return rgn->base + rgn->size;
}

void reset_memblock_regions(void);
void reset_memblock_attributes(void);
void setup_memblock(void);
void setup_numa_memblock(const unsigned int node_fracs[]);
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

static inline void run_top_down(int (*func)())
{
	memblock_set_bottom_up(false);
	prefix_push("top-down");
	func();
	prefix_pop();
}

static inline void run_bottom_up(int (*func)())
{
	memblock_set_bottom_up(true);
	prefix_push("bottom-up");
	func();
	prefix_pop();
}

static inline void assert_mem_content(void *mem, int size, int flags)
{
	if (flags & TEST_F_RAW)
		ASSERT_MEM_NE(mem, 0, size);
	else
		ASSERT_MEM_EQ(mem, 0, size);
}

#endif
