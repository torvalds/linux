// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 * Author: Andrey Ryabinin <a.ryabinin@samsung.com>
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/kasan.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/vmalloc.h>

#include <asm/page.h>

#include <kunit/test.h>

#include "../mm/kasan/kasan.h"

#define OOB_TAG_OFF (IS_ENABLED(CONFIG_KASAN_GENERIC) ? 0 : KASAN_GRANULE_SIZE)

/*
 * Some tests use these global variables to store return values from function
 * calls that could otherwise be eliminated by the compiler as dead code.
 */
void *kasan_ptr_result;
int kasan_int_result;

static struct kunit_resource resource;
static struct kunit_kasan_expectation fail_data;
static bool multishot;

/*
 * Temporarily enable multi-shot mode. Otherwise, KASAN would only report the
 * first detected bug and panic the kernel if panic_on_warn is enabled. For
 * hardware tag-based KASAN also allow tag checking to be reenabled for each
 * test, see the comment for KUNIT_EXPECT_KASAN_FAIL().
 */
static int kasan_test_init(struct kunit *test)
{
	if (!kasan_enabled()) {
		kunit_err(test, "can't run KASAN tests with KASAN disabled");
		return -1;
	}

	multishot = kasan_save_enable_multi_shot();
	kasan_set_tagging_report_once(false);
	return 0;
}

static void kasan_test_exit(struct kunit *test)
{
	kasan_set_tagging_report_once(true);
	kasan_restore_multi_shot(multishot);
}

/**
 * KUNIT_EXPECT_KASAN_FAIL() - check that the executed expression produces a
 * KASAN report; causes a test failure otherwise. This relies on a KUnit
 * resource named "kasan_data". Do not use this name for KUnit resources
 * outside of KASAN tests.
 *
 * For hardware tag-based KASAN in sync mode, when a tag fault happens, tag
 * checking is auto-disabled. When this happens, this test handler reenables
 * tag checking. As tag checking can be only disabled or enabled per CPU,
 * this handler disables migration (preemption).
 *
 * Since the compiler doesn't see that the expression can change the fail_data
 * fields, it can reorder or optimize away the accesses to those fields.
 * Use READ/WRITE_ONCE() for the accesses and compiler barriers around the
 * expression to prevent that.
 */
#define KUNIT_EXPECT_KASAN_FAIL(test, expression) do {		\
	if (IS_ENABLED(CONFIG_KASAN_HW_TAGS) &&			\
	    !kasan_async_mode_enabled())			\
		migrate_disable();				\
	WRITE_ONCE(fail_data.report_expected, true);		\
	WRITE_ONCE(fail_data.report_found, false);		\
	kunit_add_named_resource(test,				\
				NULL,				\
				NULL,				\
				&resource,			\
				"kasan_data", &fail_data);	\
	barrier();						\
	expression;						\
	barrier();						\
	if (kasan_async_mode_enabled())				\
		kasan_force_async_fault();			\
	barrier();						\
	KUNIT_EXPECT_EQ(test,					\
			READ_ONCE(fail_data.report_expected),	\
			READ_ONCE(fail_data.report_found));	\
	if (IS_ENABLED(CONFIG_KASAN_HW_TAGS) &&			\
	    !kasan_async_mode_enabled()) {			\
		if (READ_ONCE(fail_data.report_found))		\
			kasan_enable_tagging_sync();		\
		migrate_enable();				\
	}							\
} while (0)

#define KASAN_TEST_NEEDS_CONFIG_ON(test, config) do {			\
	if (!IS_ENABLED(config)) {					\
		kunit_info((test), "skipping, " #config " required");	\
		return;							\
	}								\
} while (0)

#define KASAN_TEST_NEEDS_CONFIG_OFF(test, config) do {			\
	if (IS_ENABLED(config)) {					\
		kunit_info((test), "skipping, " #config " enabled");	\
		return;							\
	}								\
} while (0)

static void kmalloc_oob_right(struct kunit *test)
{
	char *ptr;
	size_t size = 123;

	ptr = kmalloc(size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr);

	KUNIT_EXPECT_KASAN_FAIL(test, ptr[size + OOB_TAG_OFF] = 'x');
	kfree(ptr);
}

static void kmalloc_oob_left(struct kunit *test)
{
	char *ptr;
	size_t size = 15;

	ptr = kmalloc(size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr);

	KUNIT_EXPECT_KASAN_FAIL(test, *ptr = *(ptr - 1));
	kfree(ptr);
}

static void kmalloc_node_oob_right(struct kunit *test)
{
	char *ptr;
	size_t size = 4096;

	ptr = kmalloc_node(size, GFP_KERNEL, 0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr);

	KUNIT_EXPECT_KASAN_FAIL(test, ptr[size] = 0);
	kfree(ptr);
}

/*
 * These kmalloc_pagealloc_* tests try allocating a memory chunk that doesn't
 * fit into a slab cache and therefore is allocated via the page allocator
 * fallback. Since this kind of fallback is only implemented for SLUB, these
 * tests are limited to that allocator.
 */
static void kmalloc_pagealloc_oob_right(struct kunit *test)
{
	char *ptr;
	size_t size = KMALLOC_MAX_CACHE_SIZE + 10;

	KASAN_TEST_NEEDS_CONFIG_ON(test, CONFIG_SLUB);

	ptr = kmalloc(size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr);

	KUNIT_EXPECT_KASAN_FAIL(test, ptr[size + OOB_TAG_OFF] = 0);

	kfree(ptr);
}

static void kmalloc_pagealloc_uaf(struct kunit *test)
{
	char *ptr;
	size_t size = KMALLOC_MAX_CACHE_SIZE + 10;

	KASAN_TEST_NEEDS_CONFIG_ON(test, CONFIG_SLUB);

	ptr = kmalloc(size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr);
	kfree(ptr);

	KUNIT_EXPECT_KASAN_FAIL(test, ptr[0] = 0);
}

static void kmalloc_pagealloc_invalid_free(struct kunit *test)
{
	char *ptr;
	size_t size = KMALLOC_MAX_CACHE_SIZE + 10;

	KASAN_TEST_NEEDS_CONFIG_ON(test, CONFIG_SLUB);

	ptr = kmalloc(size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr);

	KUNIT_EXPECT_KASAN_FAIL(test, kfree(ptr + 1));
}

static void pagealloc_oob_right(struct kunit *test)
{
	char *ptr;
	struct page *pages;
	size_t order = 4;
	size_t size = (1UL << (PAGE_SHIFT + order));

	/*
	 * With generic KASAN page allocations have no redzones, thus
	 * out-of-bounds detection is not guaranteed.
	 * See https://bugzilla.kernel.org/show_bug.cgi?id=210503.
	 */
	KASAN_TEST_NEEDS_CONFIG_OFF(test, CONFIG_KASAN_GENERIC);

	pages = alloc_pages(GFP_KERNEL, order);
	ptr = page_address(pages);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr);

	KUNIT_EXPECT_KASAN_FAIL(test, ptr[size] = 0);
	free_pages((unsigned long)ptr, order);
}

static void pagealloc_uaf(struct kunit *test)
{
	char *ptr;
	struct page *pages;
	size_t order = 4;

	pages = alloc_pages(GFP_KERNEL, order);
	ptr = page_address(pages);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr);
	free_pages((unsigned long)ptr, order);

	KUNIT_EXPECT_KASAN_FAIL(test, ptr[0] = 0);
}

static void kmalloc_large_oob_right(struct kunit *test)
{
	char *ptr;
	size_t size = KMALLOC_MAX_CACHE_SIZE - 256;

	/*
	 * Allocate a chunk that is large enough, but still fits into a slab
	 * and does not trigger the page allocator fallback in SLUB.
	 */
	ptr = kmalloc(size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr);

	KUNIT_EXPECT_KASAN_FAIL(test, ptr[size] = 0);
	kfree(ptr);
}

static void krealloc_more_oob_helper(struct kunit *test,
					size_t size1, size_t size2)
{
	char *ptr1, *ptr2;
	size_t middle;

	KUNIT_ASSERT_LT(test, size1, size2);
	middle = size1 + (size2 - size1) / 2;

	ptr1 = kmalloc(size1, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr1);

	ptr2 = krealloc(ptr1, size2, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr2);

	/* All offsets up to size2 must be accessible. */
	ptr2[size1 - 1] = 'x';
	ptr2[size1] = 'x';
	ptr2[middle] = 'x';
	ptr2[size2 - 1] = 'x';

	/* Generic mode is precise, so unaligned size2 must be inaccessible. */
	if (IS_ENABLED(CONFIG_KASAN_GENERIC))
		KUNIT_EXPECT_KASAN_FAIL(test, ptr2[size2] = 'x');

	/* For all modes first aligned offset after size2 must be inaccessible. */
	KUNIT_EXPECT_KASAN_FAIL(test,
		ptr2[round_up(size2, KASAN_GRANULE_SIZE)] = 'x');

	kfree(ptr2);
}

static void krealloc_less_oob_helper(struct kunit *test,
					size_t size1, size_t size2)
{
	char *ptr1, *ptr2;
	size_t middle;

	KUNIT_ASSERT_LT(test, size2, size1);
	middle = size2 + (size1 - size2) / 2;

	ptr1 = kmalloc(size1, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr1);

	ptr2 = krealloc(ptr1, size2, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr2);

	/* Must be accessible for all modes. */
	ptr2[size2 - 1] = 'x';

	/* Generic mode is precise, so unaligned size2 must be inaccessible. */
	if (IS_ENABLED(CONFIG_KASAN_GENERIC))
		KUNIT_EXPECT_KASAN_FAIL(test, ptr2[size2] = 'x');

	/* For all modes first aligned offset after size2 must be inaccessible. */
	KUNIT_EXPECT_KASAN_FAIL(test,
		ptr2[round_up(size2, KASAN_GRANULE_SIZE)] = 'x');

	/*
	 * For all modes all size2, middle, and size1 should land in separate
	 * granules and thus the latter two offsets should be inaccessible.
	 */
	KUNIT_EXPECT_LE(test, round_up(size2, KASAN_GRANULE_SIZE),
				round_down(middle, KASAN_GRANULE_SIZE));
	KUNIT_EXPECT_LE(test, round_up(middle, KASAN_GRANULE_SIZE),
				round_down(size1, KASAN_GRANULE_SIZE));
	KUNIT_EXPECT_KASAN_FAIL(test, ptr2[middle] = 'x');
	KUNIT_EXPECT_KASAN_FAIL(test, ptr2[size1 - 1] = 'x');
	KUNIT_EXPECT_KASAN_FAIL(test, ptr2[size1] = 'x');

	kfree(ptr2);
}

static void krealloc_more_oob(struct kunit *test)
{
	krealloc_more_oob_helper(test, 201, 235);
}

static void krealloc_less_oob(struct kunit *test)
{
	krealloc_less_oob_helper(test, 235, 201);
}

static void krealloc_pagealloc_more_oob(struct kunit *test)
{
	/* page_alloc fallback in only implemented for SLUB. */
	KASAN_TEST_NEEDS_CONFIG_ON(test, CONFIG_SLUB);

	krealloc_more_oob_helper(test, KMALLOC_MAX_CACHE_SIZE + 201,
					KMALLOC_MAX_CACHE_SIZE + 235);
}

static void krealloc_pagealloc_less_oob(struct kunit *test)
{
	/* page_alloc fallback in only implemented for SLUB. */
	KASAN_TEST_NEEDS_CONFIG_ON(test, CONFIG_SLUB);

	krealloc_less_oob_helper(test, KMALLOC_MAX_CACHE_SIZE + 235,
					KMALLOC_MAX_CACHE_SIZE + 201);
}

/*
 * Check that krealloc() detects a use-after-free, returns NULL,
 * and doesn't unpoison the freed object.
 */
static void krealloc_uaf(struct kunit *test)
{
	char *ptr1, *ptr2;
	int size1 = 201;
	int size2 = 235;

	ptr1 = kmalloc(size1, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr1);
	kfree(ptr1);

	KUNIT_EXPECT_KASAN_FAIL(test, ptr2 = krealloc(ptr1, size2, GFP_KERNEL));
	KUNIT_ASSERT_PTR_EQ(test, (void *)ptr2, NULL);
	KUNIT_EXPECT_KASAN_FAIL(test, *(volatile char *)ptr1);
}

static void kmalloc_oob_16(struct kunit *test)
{
	struct {
		u64 words[2];
	} *ptr1, *ptr2;

	/* This test is specifically crafted for the generic mode. */
	KASAN_TEST_NEEDS_CONFIG_ON(test, CONFIG_KASAN_GENERIC);

	ptr1 = kmalloc(sizeof(*ptr1) - 3, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr1);

	ptr2 = kmalloc(sizeof(*ptr2), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr2);

	KUNIT_EXPECT_KASAN_FAIL(test, *ptr1 = *ptr2);
	kfree(ptr1);
	kfree(ptr2);
}

static void kmalloc_uaf_16(struct kunit *test)
{
	struct {
		u64 words[2];
	} *ptr1, *ptr2;

	ptr1 = kmalloc(sizeof(*ptr1), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr1);

	ptr2 = kmalloc(sizeof(*ptr2), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr2);
	kfree(ptr2);

	KUNIT_EXPECT_KASAN_FAIL(test, *ptr1 = *ptr2);
	kfree(ptr1);
}

static void kmalloc_oob_memset_2(struct kunit *test)
{
	char *ptr;
	size_t size = 8;

	ptr = kmalloc(size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr);

	KUNIT_EXPECT_KASAN_FAIL(test, memset(ptr + 7 + OOB_TAG_OFF, 0, 2));
	kfree(ptr);
}

static void kmalloc_oob_memset_4(struct kunit *test)
{
	char *ptr;
	size_t size = 8;

	ptr = kmalloc(size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr);

	KUNIT_EXPECT_KASAN_FAIL(test, memset(ptr + 5 + OOB_TAG_OFF, 0, 4));
	kfree(ptr);
}


static void kmalloc_oob_memset_8(struct kunit *test)
{
	char *ptr;
	size_t size = 8;

	ptr = kmalloc(size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr);

	KUNIT_EXPECT_KASAN_FAIL(test, memset(ptr + 1 + OOB_TAG_OFF, 0, 8));
	kfree(ptr);
}

static void kmalloc_oob_memset_16(struct kunit *test)
{
	char *ptr;
	size_t size = 16;

	ptr = kmalloc(size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr);

	KUNIT_EXPECT_KASAN_FAIL(test, memset(ptr + 1 + OOB_TAG_OFF, 0, 16));
	kfree(ptr);
}

static void kmalloc_oob_in_memset(struct kunit *test)
{
	char *ptr;
	size_t size = 666;

	ptr = kmalloc(size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr);

	KUNIT_EXPECT_KASAN_FAIL(test, memset(ptr, 0, size + 5 + OOB_TAG_OFF));
	kfree(ptr);
}

static void kmalloc_memmove_invalid_size(struct kunit *test)
{
	char *ptr;
	size_t size = 64;
	volatile size_t invalid_size = -2;

	ptr = kmalloc(size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr);

	memset((char *)ptr, 0, 64);

	KUNIT_EXPECT_KASAN_FAIL(test,
		memmove((char *)ptr, (char *)ptr + 4, invalid_size));
	kfree(ptr);
}

static void kmalloc_uaf(struct kunit *test)
{
	char *ptr;
	size_t size = 10;

	ptr = kmalloc(size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr);

	kfree(ptr);
	KUNIT_EXPECT_KASAN_FAIL(test, *(ptr + 8) = 'x');
}

static void kmalloc_uaf_memset(struct kunit *test)
{
	char *ptr;
	size_t size = 33;

	ptr = kmalloc(size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr);

	kfree(ptr);
	KUNIT_EXPECT_KASAN_FAIL(test, memset(ptr, 0, size));
}

static void kmalloc_uaf2(struct kunit *test)
{
	char *ptr1, *ptr2;
	size_t size = 43;
	int counter = 0;

again:
	ptr1 = kmalloc(size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr1);

	kfree(ptr1);

	ptr2 = kmalloc(size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr2);

	/*
	 * For tag-based KASAN ptr1 and ptr2 tags might happen to be the same.
	 * Allow up to 16 attempts at generating different tags.
	 */
	if (!IS_ENABLED(CONFIG_KASAN_GENERIC) && ptr1 == ptr2 && counter++ < 16) {
		kfree(ptr2);
		goto again;
	}

	KUNIT_EXPECT_KASAN_FAIL(test, ptr1[40] = 'x');
	KUNIT_EXPECT_PTR_NE(test, ptr1, ptr2);

	kfree(ptr2);
}

static void kfree_via_page(struct kunit *test)
{
	char *ptr;
	size_t size = 8;
	struct page *page;
	unsigned long offset;

	ptr = kmalloc(size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr);

	page = virt_to_page(ptr);
	offset = offset_in_page(ptr);
	kfree(page_address(page) + offset);
}

static void kfree_via_phys(struct kunit *test)
{
	char *ptr;
	size_t size = 8;
	phys_addr_t phys;

	ptr = kmalloc(size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr);

	phys = virt_to_phys(ptr);
	kfree(phys_to_virt(phys));
}

static void kmem_cache_oob(struct kunit *test)
{
	char *p;
	size_t size = 200;
	struct kmem_cache *cache;

	cache = kmem_cache_create("test_cache", size, 0, 0, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, cache);

	p = kmem_cache_alloc(cache, GFP_KERNEL);
	if (!p) {
		kunit_err(test, "Allocation failed: %s\n", __func__);
		kmem_cache_destroy(cache);
		return;
	}

	KUNIT_EXPECT_KASAN_FAIL(test, *p = p[size + OOB_TAG_OFF]);

	kmem_cache_free(cache, p);
	kmem_cache_destroy(cache);
}

static void kmem_cache_accounted(struct kunit *test)
{
	int i;
	char *p;
	size_t size = 200;
	struct kmem_cache *cache;

	cache = kmem_cache_create("test_cache", size, 0, SLAB_ACCOUNT, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, cache);

	/*
	 * Several allocations with a delay to allow for lazy per memcg kmem
	 * cache creation.
	 */
	for (i = 0; i < 5; i++) {
		p = kmem_cache_alloc(cache, GFP_KERNEL);
		if (!p)
			goto free_cache;

		kmem_cache_free(cache, p);
		msleep(100);
	}

free_cache:
	kmem_cache_destroy(cache);
}

static void kmem_cache_bulk(struct kunit *test)
{
	struct kmem_cache *cache;
	size_t size = 200;
	char *p[10];
	bool ret;
	int i;

	cache = kmem_cache_create("test_cache", size, 0, 0, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, cache);

	ret = kmem_cache_alloc_bulk(cache, GFP_KERNEL, ARRAY_SIZE(p), (void **)&p);
	if (!ret) {
		kunit_err(test, "Allocation failed: %s\n", __func__);
		kmem_cache_destroy(cache);
		return;
	}

	for (i = 0; i < ARRAY_SIZE(p); i++)
		p[i][0] = p[i][size - 1] = 42;

	kmem_cache_free_bulk(cache, ARRAY_SIZE(p), (void **)&p);
	kmem_cache_destroy(cache);
}

static char global_array[10];

static void kasan_global_oob(struct kunit *test)
{
	/*
	 * Deliberate out-of-bounds access. To prevent CONFIG_UBSAN_LOCAL_BOUNDS
	 * from failing here and panicing the kernel, access the array via a
	 * volatile pointer, which will prevent the compiler from being able to
	 * determine the array bounds.
	 *
	 * This access uses a volatile pointer to char (char *volatile) rather
	 * than the more conventional pointer to volatile char (volatile char *)
	 * because we want to prevent the compiler from making inferences about
	 * the pointer itself (i.e. its array bounds), not the data that it
	 * refers to.
	 */
	char *volatile array = global_array;
	char *p = &array[ARRAY_SIZE(global_array) + 3];

	/* Only generic mode instruments globals. */
	KASAN_TEST_NEEDS_CONFIG_ON(test, CONFIG_KASAN_GENERIC);

	KUNIT_EXPECT_KASAN_FAIL(test, *(volatile char *)p);
}

/* Check that ksize() makes the whole object accessible. */
static void ksize_unpoisons_memory(struct kunit *test)
{
	char *ptr;
	size_t size = 123, real_size;

	ptr = kmalloc(size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr);
	real_size = ksize(ptr);

	/* This access shouldn't trigger a KASAN report. */
	ptr[size] = 'x';

	/* This one must. */
	KUNIT_EXPECT_KASAN_FAIL(test, ptr[real_size] = 'y');

	kfree(ptr);
}

/*
 * Check that a use-after-free is detected by ksize() and via normal accesses
 * after it.
 */
static void ksize_uaf(struct kunit *test)
{
	char *ptr;
	int size = 128 - KASAN_GRANULE_SIZE;

	ptr = kmalloc(size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr);
	kfree(ptr);

	KUNIT_EXPECT_KASAN_FAIL(test, ksize(ptr));
	KUNIT_EXPECT_KASAN_FAIL(test, kasan_int_result = *ptr);
	KUNIT_EXPECT_KASAN_FAIL(test, kasan_int_result = *(ptr + size));
}

static void kasan_stack_oob(struct kunit *test)
{
	char stack_array[10];
	/* See comment in kasan_global_oob. */
	char *volatile array = stack_array;
	char *p = &array[ARRAY_SIZE(stack_array) + OOB_TAG_OFF];

	KASAN_TEST_NEEDS_CONFIG_ON(test, CONFIG_KASAN_STACK);

	KUNIT_EXPECT_KASAN_FAIL(test, *(volatile char *)p);
}

static void kasan_alloca_oob_left(struct kunit *test)
{
	volatile int i = 10;
	char alloca_array[i];
	/* See comment in kasan_global_oob. */
	char *volatile array = alloca_array;
	char *p = array - 1;

	/* Only generic mode instruments dynamic allocas. */
	KASAN_TEST_NEEDS_CONFIG_ON(test, CONFIG_KASAN_GENERIC);
	KASAN_TEST_NEEDS_CONFIG_ON(test, CONFIG_KASAN_STACK);

	KUNIT_EXPECT_KASAN_FAIL(test, *(volatile char *)p);
}

static void kasan_alloca_oob_right(struct kunit *test)
{
	volatile int i = 10;
	char alloca_array[i];
	/* See comment in kasan_global_oob. */
	char *volatile array = alloca_array;
	char *p = array + i;

	/* Only generic mode instruments dynamic allocas. */
	KASAN_TEST_NEEDS_CONFIG_ON(test, CONFIG_KASAN_GENERIC);
	KASAN_TEST_NEEDS_CONFIG_ON(test, CONFIG_KASAN_STACK);

	KUNIT_EXPECT_KASAN_FAIL(test, *(volatile char *)p);
}

static void kmem_cache_double_free(struct kunit *test)
{
	char *p;
	size_t size = 200;
	struct kmem_cache *cache;

	cache = kmem_cache_create("test_cache", size, 0, 0, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, cache);

	p = kmem_cache_alloc(cache, GFP_KERNEL);
	if (!p) {
		kunit_err(test, "Allocation failed: %s\n", __func__);
		kmem_cache_destroy(cache);
		return;
	}

	kmem_cache_free(cache, p);
	KUNIT_EXPECT_KASAN_FAIL(test, kmem_cache_free(cache, p));
	kmem_cache_destroy(cache);
}

static void kmem_cache_invalid_free(struct kunit *test)
{
	char *p;
	size_t size = 200;
	struct kmem_cache *cache;

	cache = kmem_cache_create("test_cache", size, 0, SLAB_TYPESAFE_BY_RCU,
				  NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, cache);

	p = kmem_cache_alloc(cache, GFP_KERNEL);
	if (!p) {
		kunit_err(test, "Allocation failed: %s\n", __func__);
		kmem_cache_destroy(cache);
		return;
	}

	/* Trigger invalid free, the object doesn't get freed. */
	KUNIT_EXPECT_KASAN_FAIL(test, kmem_cache_free(cache, p + 1));

	/*
	 * Properly free the object to prevent the "Objects remaining in
	 * test_cache on __kmem_cache_shutdown" BUG failure.
	 */
	kmem_cache_free(cache, p);

	kmem_cache_destroy(cache);
}

static void kasan_memchr(struct kunit *test)
{
	char *ptr;
	size_t size = 24;

	/*
	 * str* functions are not instrumented with CONFIG_AMD_MEM_ENCRYPT.
	 * See https://bugzilla.kernel.org/show_bug.cgi?id=206337 for details.
	 */
	KASAN_TEST_NEEDS_CONFIG_OFF(test, CONFIG_AMD_MEM_ENCRYPT);

	if (OOB_TAG_OFF)
		size = round_up(size, OOB_TAG_OFF);

	ptr = kmalloc(size, GFP_KERNEL | __GFP_ZERO);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr);

	KUNIT_EXPECT_KASAN_FAIL(test,
		kasan_ptr_result = memchr(ptr, '1', size + 1));

	kfree(ptr);
}

static void kasan_memcmp(struct kunit *test)
{
	char *ptr;
	size_t size = 24;
	int arr[9];

	/*
	 * str* functions are not instrumented with CONFIG_AMD_MEM_ENCRYPT.
	 * See https://bugzilla.kernel.org/show_bug.cgi?id=206337 for details.
	 */
	KASAN_TEST_NEEDS_CONFIG_OFF(test, CONFIG_AMD_MEM_ENCRYPT);

	if (OOB_TAG_OFF)
		size = round_up(size, OOB_TAG_OFF);

	ptr = kmalloc(size, GFP_KERNEL | __GFP_ZERO);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr);
	memset(arr, 0, sizeof(arr));

	KUNIT_EXPECT_KASAN_FAIL(test,
		kasan_int_result = memcmp(ptr, arr, size+1));
	kfree(ptr);
}

static void kasan_strings(struct kunit *test)
{
	char *ptr;
	size_t size = 24;

	/*
	 * str* functions are not instrumented with CONFIG_AMD_MEM_ENCRYPT.
	 * See https://bugzilla.kernel.org/show_bug.cgi?id=206337 for details.
	 */
	KASAN_TEST_NEEDS_CONFIG_OFF(test, CONFIG_AMD_MEM_ENCRYPT);

	ptr = kmalloc(size, GFP_KERNEL | __GFP_ZERO);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr);

	kfree(ptr);

	/*
	 * Try to cause only 1 invalid access (less spam in dmesg).
	 * For that we need ptr to point to zeroed byte.
	 * Skip metadata that could be stored in freed object so ptr
	 * will likely point to zeroed byte.
	 */
	ptr += 16;
	KUNIT_EXPECT_KASAN_FAIL(test, kasan_ptr_result = strchr(ptr, '1'));

	KUNIT_EXPECT_KASAN_FAIL(test, kasan_ptr_result = strrchr(ptr, '1'));

	KUNIT_EXPECT_KASAN_FAIL(test, kasan_int_result = strcmp(ptr, "2"));

	KUNIT_EXPECT_KASAN_FAIL(test, kasan_int_result = strncmp(ptr, "2", 1));

	KUNIT_EXPECT_KASAN_FAIL(test, kasan_int_result = strlen(ptr));

	KUNIT_EXPECT_KASAN_FAIL(test, kasan_int_result = strnlen(ptr, 1));
}

static void kasan_bitops_modify(struct kunit *test, int nr, void *addr)
{
	KUNIT_EXPECT_KASAN_FAIL(test, set_bit(nr, addr));
	KUNIT_EXPECT_KASAN_FAIL(test, __set_bit(nr, addr));
	KUNIT_EXPECT_KASAN_FAIL(test, clear_bit(nr, addr));
	KUNIT_EXPECT_KASAN_FAIL(test, __clear_bit(nr, addr));
	KUNIT_EXPECT_KASAN_FAIL(test, clear_bit_unlock(nr, addr));
	KUNIT_EXPECT_KASAN_FAIL(test, __clear_bit_unlock(nr, addr));
	KUNIT_EXPECT_KASAN_FAIL(test, change_bit(nr, addr));
	KUNIT_EXPECT_KASAN_FAIL(test, __change_bit(nr, addr));
}

static void kasan_bitops_test_and_modify(struct kunit *test, int nr, void *addr)
{
	KUNIT_EXPECT_KASAN_FAIL(test, test_and_set_bit(nr, addr));
	KUNIT_EXPECT_KASAN_FAIL(test, __test_and_set_bit(nr, addr));
	KUNIT_EXPECT_KASAN_FAIL(test, test_and_set_bit_lock(nr, addr));
	KUNIT_EXPECT_KASAN_FAIL(test, test_and_clear_bit(nr, addr));
	KUNIT_EXPECT_KASAN_FAIL(test, __test_and_clear_bit(nr, addr));
	KUNIT_EXPECT_KASAN_FAIL(test, test_and_change_bit(nr, addr));
	KUNIT_EXPECT_KASAN_FAIL(test, __test_and_change_bit(nr, addr));
	KUNIT_EXPECT_KASAN_FAIL(test, kasan_int_result = test_bit(nr, addr));

#if defined(clear_bit_unlock_is_negative_byte)
	KUNIT_EXPECT_KASAN_FAIL(test, kasan_int_result =
				clear_bit_unlock_is_negative_byte(nr, addr));
#endif
}

static void kasan_bitops_generic(struct kunit *test)
{
	long *bits;

	/* This test is specifically crafted for the generic mode. */
	KASAN_TEST_NEEDS_CONFIG_ON(test, CONFIG_KASAN_GENERIC);

	/*
	 * Allocate 1 more byte, which causes kzalloc to round up to 16 bytes;
	 * this way we do not actually corrupt other memory.
	 */
	bits = kzalloc(sizeof(*bits) + 1, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, bits);

	/*
	 * Below calls try to access bit within allocated memory; however, the
	 * below accesses are still out-of-bounds, since bitops are defined to
	 * operate on the whole long the bit is in.
	 */
	kasan_bitops_modify(test, BITS_PER_LONG, bits);

	/*
	 * Below calls try to access bit beyond allocated memory.
	 */
	kasan_bitops_test_and_modify(test, BITS_PER_LONG + BITS_PER_BYTE, bits);

	kfree(bits);
}

static void kasan_bitops_tags(struct kunit *test)
{
	long *bits;

	/* This test is specifically crafted for tag-based modes. */
	KASAN_TEST_NEEDS_CONFIG_OFF(test, CONFIG_KASAN_GENERIC);

	/* kmalloc-64 cache will be used and the last 16 bytes will be the redzone. */
	bits = kzalloc(48, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, bits);

	/* Do the accesses past the 48 allocated bytes, but within the redone. */
	kasan_bitops_modify(test, BITS_PER_LONG, (void *)bits + 48);
	kasan_bitops_test_and_modify(test, BITS_PER_LONG + BITS_PER_BYTE, (void *)bits + 48);

	kfree(bits);
}

static void kmalloc_double_kzfree(struct kunit *test)
{
	char *ptr;
	size_t size = 16;

	ptr = kmalloc(size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr);

	kfree_sensitive(ptr);
	KUNIT_EXPECT_KASAN_FAIL(test, kfree_sensitive(ptr));
}

static void vmalloc_oob(struct kunit *test)
{
	void *area;

	KASAN_TEST_NEEDS_CONFIG_ON(test, CONFIG_KASAN_VMALLOC);

	/*
	 * We have to be careful not to hit the guard page.
	 * The MMU will catch that and crash us.
	 */
	area = vmalloc(3000);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, area);

	KUNIT_EXPECT_KASAN_FAIL(test, ((volatile char *)area)[3100]);
	vfree(area);
}

/*
 * Check that the assigned pointer tag falls within the [KASAN_TAG_MIN,
 * KASAN_TAG_KERNEL) range (note: excluding the match-all tag) for tag-based
 * modes.
 */
static void match_all_not_assigned(struct kunit *test)
{
	char *ptr;
	struct page *pages;
	int i, size, order;

	KASAN_TEST_NEEDS_CONFIG_OFF(test, CONFIG_KASAN_GENERIC);

	for (i = 0; i < 256; i++) {
		size = (get_random_int() % 1024) + 1;
		ptr = kmalloc(size, GFP_KERNEL);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr);
		KUNIT_EXPECT_GE(test, (u8)get_tag(ptr), (u8)KASAN_TAG_MIN);
		KUNIT_EXPECT_LT(test, (u8)get_tag(ptr), (u8)KASAN_TAG_KERNEL);
		kfree(ptr);
	}

	for (i = 0; i < 256; i++) {
		order = (get_random_int() % 4) + 1;
		pages = alloc_pages(GFP_KERNEL, order);
		ptr = page_address(pages);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr);
		KUNIT_EXPECT_GE(test, (u8)get_tag(ptr), (u8)KASAN_TAG_MIN);
		KUNIT_EXPECT_LT(test, (u8)get_tag(ptr), (u8)KASAN_TAG_KERNEL);
		free_pages((unsigned long)ptr, order);
	}
}

/* Check that 0xff works as a match-all pointer tag for tag-based modes. */
static void match_all_ptr_tag(struct kunit *test)
{
	char *ptr;
	u8 tag;

	KASAN_TEST_NEEDS_CONFIG_OFF(test, CONFIG_KASAN_GENERIC);

	ptr = kmalloc(128, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr);

	/* Backup the assigned tag. */
	tag = get_tag(ptr);
	KUNIT_EXPECT_NE(test, tag, (u8)KASAN_TAG_KERNEL);

	/* Reset the tag to 0xff.*/
	ptr = set_tag(ptr, KASAN_TAG_KERNEL);

	/* This access shouldn't trigger a KASAN report. */
	*ptr = 0;

	/* Recover the pointer tag and free. */
	ptr = set_tag(ptr, tag);
	kfree(ptr);
}

/* Check that there are no match-all memory tags for tag-based modes. */
static void match_all_mem_tag(struct kunit *test)
{
	char *ptr;
	int tag;

	KASAN_TEST_NEEDS_CONFIG_OFF(test, CONFIG_KASAN_GENERIC);

	ptr = kmalloc(128, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr);
	KUNIT_EXPECT_NE(test, (u8)get_tag(ptr), (u8)KASAN_TAG_KERNEL);

	/* For each possible tag value not matching the pointer tag. */
	for (tag = KASAN_TAG_MIN; tag <= KASAN_TAG_KERNEL; tag++) {
		if (tag == get_tag(ptr))
			continue;

		/* Mark the first memory granule with the chosen memory tag. */
		kasan_poison(ptr, KASAN_GRANULE_SIZE, (u8)tag, false);

		/* This access must cause a KASAN report. */
		KUNIT_EXPECT_KASAN_FAIL(test, *ptr = 0);
	}

	/* Recover the memory tag and free. */
	kasan_poison(ptr, KASAN_GRANULE_SIZE, get_tag(ptr), false);
	kfree(ptr);
}

static struct kunit_case kasan_kunit_test_cases[] = {
	KUNIT_CASE(kmalloc_oob_right),
	KUNIT_CASE(kmalloc_oob_left),
	KUNIT_CASE(kmalloc_node_oob_right),
	KUNIT_CASE(kmalloc_pagealloc_oob_right),
	KUNIT_CASE(kmalloc_pagealloc_uaf),
	KUNIT_CASE(kmalloc_pagealloc_invalid_free),
	KUNIT_CASE(pagealloc_oob_right),
	KUNIT_CASE(pagealloc_uaf),
	KUNIT_CASE(kmalloc_large_oob_right),
	KUNIT_CASE(krealloc_more_oob),
	KUNIT_CASE(krealloc_less_oob),
	KUNIT_CASE(krealloc_pagealloc_more_oob),
	KUNIT_CASE(krealloc_pagealloc_less_oob),
	KUNIT_CASE(krealloc_uaf),
	KUNIT_CASE(kmalloc_oob_16),
	KUNIT_CASE(kmalloc_uaf_16),
	KUNIT_CASE(kmalloc_oob_in_memset),
	KUNIT_CASE(kmalloc_oob_memset_2),
	KUNIT_CASE(kmalloc_oob_memset_4),
	KUNIT_CASE(kmalloc_oob_memset_8),
	KUNIT_CASE(kmalloc_oob_memset_16),
	KUNIT_CASE(kmalloc_memmove_invalid_size),
	KUNIT_CASE(kmalloc_uaf),
	KUNIT_CASE(kmalloc_uaf_memset),
	KUNIT_CASE(kmalloc_uaf2),
	KUNIT_CASE(kfree_via_page),
	KUNIT_CASE(kfree_via_phys),
	KUNIT_CASE(kmem_cache_oob),
	KUNIT_CASE(kmem_cache_accounted),
	KUNIT_CASE(kmem_cache_bulk),
	KUNIT_CASE(kasan_global_oob),
	KUNIT_CASE(kasan_stack_oob),
	KUNIT_CASE(kasan_alloca_oob_left),
	KUNIT_CASE(kasan_alloca_oob_right),
	KUNIT_CASE(ksize_unpoisons_memory),
	KUNIT_CASE(ksize_uaf),
	KUNIT_CASE(kmem_cache_double_free),
	KUNIT_CASE(kmem_cache_invalid_free),
	KUNIT_CASE(kasan_memchr),
	KUNIT_CASE(kasan_memcmp),
	KUNIT_CASE(kasan_strings),
	KUNIT_CASE(kasan_bitops_generic),
	KUNIT_CASE(kasan_bitops_tags),
	KUNIT_CASE(kmalloc_double_kzfree),
	KUNIT_CASE(vmalloc_oob),
	KUNIT_CASE(match_all_not_assigned),
	KUNIT_CASE(match_all_ptr_tag),
	KUNIT_CASE(match_all_mem_tag),
	{}
};

static struct kunit_suite kasan_kunit_test_suite = {
	.name = "kasan",
	.init = kasan_test_init,
	.test_cases = kasan_kunit_test_cases,
	.exit = kasan_test_exit,
};

kunit_test_suite(kasan_kunit_test_suite);

MODULE_LICENSE("GPL");
