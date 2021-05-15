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
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/vmalloc.h>

#include <asm/page.h>

#include <kunit/test.h>

#include "../mm/kasan/kasan.h"

#define OOB_TAG_OFF (IS_ENABLED(CONFIG_KASAN_GENERIC) ? 0 : KASAN_SHADOW_SCALE_SIZE)

/*
 * We assign some test results to these globals to make sure the tests
 * are not eliminated as dead code.
 */

void *kasan_ptr_result;
int kasan_int_result;

static struct kunit_resource resource;
static struct kunit_kasan_expectation fail_data;
static bool multishot;

static int kasan_test_init(struct kunit *test)
{
	/*
	 * Temporarily enable multi-shot mode and set panic_on_warn=0.
	 * Otherwise, we'd only get a report for the first case.
	 */
	multishot = kasan_save_enable_multi_shot();

	return 0;
}

static void kasan_test_exit(struct kunit *test)
{
	kasan_restore_multi_shot(multishot);
}

/**
 * KUNIT_EXPECT_KASAN_FAIL() - Causes a test failure when the expression does
 * not cause a KASAN error. This uses a KUnit resource named "kasan_data." Do
 * Do not use this name for a KUnit resource outside here.
 *
 */
#define KUNIT_EXPECT_KASAN_FAIL(test, condition) do { \
	fail_data.report_expected = true; \
	fail_data.report_found = false; \
	kunit_add_named_resource(test, \
				NULL, \
				NULL, \
				&resource, \
				"kasan_data", &fail_data); \
	condition; \
	KUNIT_EXPECT_EQ(test, \
			fail_data.report_expected, \
			fail_data.report_found); \
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

static void kmalloc_pagealloc_oob_right(struct kunit *test)
{
	char *ptr;
	size_t size = KMALLOC_MAX_CACHE_SIZE + 10;

	if (!IS_ENABLED(CONFIG_SLUB)) {
		kunit_info(test, "CONFIG_SLUB is not enabled.");
		return;
	}

	/* Allocate a chunk that does not fit into a SLUB cache to trigger
	 * the page allocator fallback.
	 */
	ptr = kmalloc(size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr);

	KUNIT_EXPECT_KASAN_FAIL(test, ptr[size + OOB_TAG_OFF] = 0);
	kfree(ptr);
}

static void kmalloc_pagealloc_uaf(struct kunit *test)
{
	char *ptr;
	size_t size = KMALLOC_MAX_CACHE_SIZE + 10;

	if (!IS_ENABLED(CONFIG_SLUB)) {
		kunit_info(test, "CONFIG_SLUB is not enabled.");
		return;
	}

	ptr = kmalloc(size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr);

	kfree(ptr);
	KUNIT_EXPECT_KASAN_FAIL(test, ptr[0] = 0);
}

static void kmalloc_pagealloc_invalid_free(struct kunit *test)
{
	char *ptr;
	size_t size = KMALLOC_MAX_CACHE_SIZE + 10;

	if (!IS_ENABLED(CONFIG_SLUB)) {
		kunit_info(test, "CONFIG_SLUB is not enabled.");
		return;
	}

	ptr = kmalloc(size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr);

	KUNIT_EXPECT_KASAN_FAIL(test, kfree(ptr + 1));
}

static void kmalloc_large_oob_right(struct kunit *test)
{
	char *ptr;
	size_t size = KMALLOC_MAX_CACHE_SIZE - 256;
	/* Allocate a chunk that is large enough, but still fits into a slab
	 * and does not trigger the page allocator fallback in SLUB.
	 */
	ptr = kmalloc(size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr);

	KUNIT_EXPECT_KASAN_FAIL(test, ptr[size] = 0);
	kfree(ptr);
}

static void kmalloc_oob_krealloc_more(struct kunit *test)
{
	char *ptr1, *ptr2;
	size_t size1 = 17;
	size_t size2 = 19;

	ptr1 = kmalloc(size1, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr1);

	ptr2 = krealloc(ptr1, size2, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr2);

	KUNIT_EXPECT_KASAN_FAIL(test, ptr2[size2 + OOB_TAG_OFF] = 'x');
	kfree(ptr2);
}

static void kmalloc_oob_krealloc_less(struct kunit *test)
{
	char *ptr1, *ptr2;
	size_t size1 = 17;
	size_t size2 = 15;

	ptr1 = kmalloc(size1, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr1);

	ptr2 = krealloc(ptr1, size2, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr2);

	KUNIT_EXPECT_KASAN_FAIL(test, ptr2[size2 + OOB_TAG_OFF] = 'x');
	kfree(ptr2);
}

static void kmalloc_oob_16(struct kunit *test)
{
	struct {
		u64 words[2];
	} *ptr1, *ptr2;

	/* This test is specifically crafted for the generic mode. */
	if (!IS_ENABLED(CONFIG_KASAN_GENERIC)) {
		kunit_info(test, "CONFIG_KASAN_GENERIC required\n");
		return;
	}

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

	ptr1 = kmalloc(size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr1);

	kfree(ptr1);

	ptr2 = kmalloc(size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr2);

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
	struct kmem_cache *cache = kmem_cache_create("test_cache",
						size, 0,
						0, NULL);
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

static void memcg_accounted_kmem_cache(struct kunit *test)
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
	if (!IS_ENABLED(CONFIG_KASAN_GENERIC)) {
		kunit_info(test, "CONFIG_KASAN_GENERIC required");
		return;
	}

	KUNIT_EXPECT_KASAN_FAIL(test, *(volatile char *)p);
}

static void ksize_unpoisons_memory(struct kunit *test)
{
	char *ptr;
	size_t size = 123, real_size;

	ptr = kmalloc(size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ptr);
	real_size = ksize(ptr);
	/* This access doesn't trigger an error. */
	ptr[size] = 'x';
	/* This one does. */
	KUNIT_EXPECT_KASAN_FAIL(test, ptr[real_size] = 'y');
	kfree(ptr);
}

static void kasan_stack_oob(struct kunit *test)
{
	char stack_array[10];
	/* See comment in kasan_global_oob. */
	char *volatile array = stack_array;
	char *p = &array[ARRAY_SIZE(stack_array) + OOB_TAG_OFF];

	if (!IS_ENABLED(CONFIG_KASAN_STACK)) {
		kunit_info(test, "CONFIG_KASAN_STACK is not enabled");
		return;
	}

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
	if (!IS_ENABLED(CONFIG_KASAN_GENERIC)) {
		kunit_info(test, "CONFIG_KASAN_GENERIC required");
		return;
	}

	if (!IS_ENABLED(CONFIG_KASAN_STACK)) {
		kunit_info(test, "CONFIG_KASAN_STACK is not enabled");
		return;
	}

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
	if (!IS_ENABLED(CONFIG_KASAN_GENERIC)) {
		kunit_info(test, "CONFIG_KASAN_GENERIC required");
		return;
	}

	if (!IS_ENABLED(CONFIG_KASAN_STACK)) {
		kunit_info(test, "CONFIG_KASAN_STACK is not enabled");
		return;
	}

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

	/* Trigger invalid free, the object doesn't get freed */
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

	/* See https://bugzilla.kernel.org/show_bug.cgi?id=206337 */
	if (IS_ENABLED(CONFIG_AMD_MEM_ENCRYPT)) {
		kunit_info(test,
			"str* functions are not instrumented with CONFIG_AMD_MEM_ENCRYPT");
		return;
	}

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

	/* See https://bugzilla.kernel.org/show_bug.cgi?id=206337 */
	if (IS_ENABLED(CONFIG_AMD_MEM_ENCRYPT)) {
		kunit_info(test,
			"str* functions are not instrumented with CONFIG_AMD_MEM_ENCRYPT");
		return;
	}

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

	/* See https://bugzilla.kernel.org/show_bug.cgi?id=206337 */
	if (IS_ENABLED(CONFIG_AMD_MEM_ENCRYPT)) {
		kunit_info(test,
			"str* functions are not instrumented with CONFIG_AMD_MEM_ENCRYPT");
		return;
	}

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
	if (!IS_ENABLED(CONFIG_KASAN_GENERIC)) {
		kunit_info(test, "CONFIG_KASAN_GENERIC required\n");
		return;
	}

	/*
	 * Allocate 1 more byte, which causes kzalloc to round up to 16-bytes;
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

	/* This test is specifically crafted for the tag-based mode. */
	if (IS_ENABLED(CONFIG_KASAN_GENERIC)) {
		kunit_info(test, "CONFIG_KASAN_SW_TAGS required\n");
		return;
	}

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

	if (!IS_ENABLED(CONFIG_KASAN_VMALLOC)) {
		kunit_info(test, "CONFIG_KASAN_VMALLOC is not enabled.");
		return;
	}

	/*
	 * We have to be careful not to hit the guard page.
	 * The MMU will catch that and crash us.
	 */
	area = vmalloc(3000);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, area);

	KUNIT_EXPECT_KASAN_FAIL(test, ((volatile char *)area)[3100]);
	vfree(area);
}

static struct kunit_case kasan_kunit_test_cases[] = {
	KUNIT_CASE(kmalloc_oob_right),
	KUNIT_CASE(kmalloc_oob_left),
	KUNIT_CASE(kmalloc_node_oob_right),
	KUNIT_CASE(kmalloc_pagealloc_oob_right),
	KUNIT_CASE(kmalloc_pagealloc_uaf),
	KUNIT_CASE(kmalloc_pagealloc_invalid_free),
	KUNIT_CASE(kmalloc_large_oob_right),
	KUNIT_CASE(kmalloc_oob_krealloc_more),
	KUNIT_CASE(kmalloc_oob_krealloc_less),
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
	KUNIT_CASE(memcg_accounted_kmem_cache),
	KUNIT_CASE(kasan_global_oob),
	KUNIT_CASE(kasan_stack_oob),
	KUNIT_CASE(kasan_alloca_oob_left),
	KUNIT_CASE(kasan_alloca_oob_right),
	KUNIT_CASE(ksize_unpoisons_memory),
	KUNIT_CASE(kmem_cache_double_free),
	KUNIT_CASE(kmem_cache_invalid_free),
	KUNIT_CASE(kasan_memchr),
	KUNIT_CASE(kasan_memcmp),
	KUNIT_CASE(kasan_strings),
	KUNIT_CASE(kasan_bitops_generic),
	KUNIT_CASE(kasan_bitops_tags),
	KUNIT_CASE(kmalloc_double_kzfree),
	KUNIT_CASE(vmalloc_oob),
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
