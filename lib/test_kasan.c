// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 * Author: Andrey Ryabinin <a.ryabinin@samsung.com>
 */

#define pr_fmt(fmt) "kasan test: %s " fmt, __func__

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

/*
 * We assign some test results to these globals to make sure the tests
 * are not eliminated as dead code.
 */

int kasan_int_result;
void *kasan_ptr_result;

/*
 * Note: test functions are marked noinline so that their names appear in
 * reports.
 */

static noinline void __init kmalloc_oob_right(void)
{
	char *ptr;
	size_t size = 123;

	pr_info("out-of-bounds to right\n");
	ptr = kmalloc(size, GFP_KERNEL);
	if (!ptr) {
		pr_err("Allocation failed\n");
		return;
	}

	ptr[size] = 'x';
	kfree(ptr);
}

static noinline void __init kmalloc_oob_left(void)
{
	char *ptr;
	size_t size = 15;

	pr_info("out-of-bounds to left\n");
	ptr = kmalloc(size, GFP_KERNEL);
	if (!ptr) {
		pr_err("Allocation failed\n");
		return;
	}

	*ptr = *(ptr - 1);
	kfree(ptr);
}

static noinline void __init kmalloc_node_oob_right(void)
{
	char *ptr;
	size_t size = 4096;

	pr_info("kmalloc_node(): out-of-bounds to right\n");
	ptr = kmalloc_node(size, GFP_KERNEL, 0);
	if (!ptr) {
		pr_err("Allocation failed\n");
		return;
	}

	ptr[size] = 0;
	kfree(ptr);
}

#ifdef CONFIG_SLUB
static noinline void __init kmalloc_pagealloc_oob_right(void)
{
	char *ptr;
	size_t size = KMALLOC_MAX_CACHE_SIZE + 10;

	/* Allocate a chunk that does not fit into a SLUB cache to trigger
	 * the page allocator fallback.
	 */
	pr_info("kmalloc pagealloc allocation: out-of-bounds to right\n");
	ptr = kmalloc(size, GFP_KERNEL);
	if (!ptr) {
		pr_err("Allocation failed\n");
		return;
	}

	ptr[size] = 0;
	kfree(ptr);
}

static noinline void __init kmalloc_pagealloc_uaf(void)
{
	char *ptr;
	size_t size = KMALLOC_MAX_CACHE_SIZE + 10;

	pr_info("kmalloc pagealloc allocation: use-after-free\n");
	ptr = kmalloc(size, GFP_KERNEL);
	if (!ptr) {
		pr_err("Allocation failed\n");
		return;
	}

	kfree(ptr);
	ptr[0] = 0;
}

static noinline void __init kmalloc_pagealloc_invalid_free(void)
{
	char *ptr;
	size_t size = KMALLOC_MAX_CACHE_SIZE + 10;

	pr_info("kmalloc pagealloc allocation: invalid-free\n");
	ptr = kmalloc(size, GFP_KERNEL);
	if (!ptr) {
		pr_err("Allocation failed\n");
		return;
	}

	kfree(ptr + 1);
}
#endif

static noinline void __init kmalloc_large_oob_right(void)
{
	char *ptr;
	size_t size = KMALLOC_MAX_CACHE_SIZE - 256;
	/* Allocate a chunk that is large enough, but still fits into a slab
	 * and does not trigger the page allocator fallback in SLUB.
	 */
	pr_info("kmalloc large allocation: out-of-bounds to right\n");
	ptr = kmalloc(size, GFP_KERNEL);
	if (!ptr) {
		pr_err("Allocation failed\n");
		return;
	}

	ptr[size] = 0;
	kfree(ptr);
}

static noinline void __init kmalloc_oob_krealloc_more(void)
{
	char *ptr1, *ptr2;
	size_t size1 = 17;
	size_t size2 = 19;

	pr_info("out-of-bounds after krealloc more\n");
	ptr1 = kmalloc(size1, GFP_KERNEL);
	ptr2 = krealloc(ptr1, size2, GFP_KERNEL);
	if (!ptr1 || !ptr2) {
		pr_err("Allocation failed\n");
		kfree(ptr1);
		kfree(ptr2);
		return;
	}

	ptr2[size2] = 'x';
	kfree(ptr2);
}

static noinline void __init kmalloc_oob_krealloc_less(void)
{
	char *ptr1, *ptr2;
	size_t size1 = 17;
	size_t size2 = 15;

	pr_info("out-of-bounds after krealloc less\n");
	ptr1 = kmalloc(size1, GFP_KERNEL);
	ptr2 = krealloc(ptr1, size2, GFP_KERNEL);
	if (!ptr1 || !ptr2) {
		pr_err("Allocation failed\n");
		kfree(ptr1);
		return;
	}
	ptr2[size2] = 'x';
	kfree(ptr2);
}

static noinline void __init kmalloc_oob_16(void)
{
	struct {
		u64 words[2];
	} *ptr1, *ptr2;

	pr_info("kmalloc out-of-bounds for 16-bytes access\n");
	ptr1 = kmalloc(sizeof(*ptr1) - 3, GFP_KERNEL);
	ptr2 = kmalloc(sizeof(*ptr2), GFP_KERNEL);
	if (!ptr1 || !ptr2) {
		pr_err("Allocation failed\n");
		kfree(ptr1);
		kfree(ptr2);
		return;
	}
	*ptr1 = *ptr2;
	kfree(ptr1);
	kfree(ptr2);
}

static noinline void __init kmalloc_oob_memset_2(void)
{
	char *ptr;
	size_t size = 8;

	pr_info("out-of-bounds in memset2\n");
	ptr = kmalloc(size, GFP_KERNEL);
	if (!ptr) {
		pr_err("Allocation failed\n");
		return;
	}

	memset(ptr+7, 0, 2);
	kfree(ptr);
}

static noinline void __init kmalloc_oob_memset_4(void)
{
	char *ptr;
	size_t size = 8;

	pr_info("out-of-bounds in memset4\n");
	ptr = kmalloc(size, GFP_KERNEL);
	if (!ptr) {
		pr_err("Allocation failed\n");
		return;
	}

	memset(ptr+5, 0, 4);
	kfree(ptr);
}


static noinline void __init kmalloc_oob_memset_8(void)
{
	char *ptr;
	size_t size = 8;

	pr_info("out-of-bounds in memset8\n");
	ptr = kmalloc(size, GFP_KERNEL);
	if (!ptr) {
		pr_err("Allocation failed\n");
		return;
	}

	memset(ptr+1, 0, 8);
	kfree(ptr);
}

static noinline void __init kmalloc_oob_memset_16(void)
{
	char *ptr;
	size_t size = 16;

	pr_info("out-of-bounds in memset16\n");
	ptr = kmalloc(size, GFP_KERNEL);
	if (!ptr) {
		pr_err("Allocation failed\n");
		return;
	}

	memset(ptr+1, 0, 16);
	kfree(ptr);
}

static noinline void __init kmalloc_oob_in_memset(void)
{
	char *ptr;
	size_t size = 666;

	pr_info("out-of-bounds in memset\n");
	ptr = kmalloc(size, GFP_KERNEL);
	if (!ptr) {
		pr_err("Allocation failed\n");
		return;
	}

	memset(ptr, 0, size+5);
	kfree(ptr);
}

static noinline void __init kmalloc_memmove_invalid_size(void)
{
	char *ptr;
	size_t size = 64;
	volatile size_t invalid_size = -2;

	pr_info("invalid size in memmove\n");
	ptr = kmalloc(size, GFP_KERNEL);
	if (!ptr) {
		pr_err("Allocation failed\n");
		return;
	}

	memset((char *)ptr, 0, 64);
	memmove((char *)ptr, (char *)ptr + 4, invalid_size);
	kfree(ptr);
}

static noinline void __init kmalloc_uaf(void)
{
	char *ptr;
	size_t size = 10;

	pr_info("use-after-free\n");
	ptr = kmalloc(size, GFP_KERNEL);
	if (!ptr) {
		pr_err("Allocation failed\n");
		return;
	}

	kfree(ptr);
	*(ptr + 8) = 'x';
}

static noinline void __init kmalloc_uaf_memset(void)
{
	char *ptr;
	size_t size = 33;

	pr_info("use-after-free in memset\n");
	ptr = kmalloc(size, GFP_KERNEL);
	if (!ptr) {
		pr_err("Allocation failed\n");
		return;
	}

	kfree(ptr);
	memset(ptr, 0, size);
}

static noinline void __init kmalloc_uaf2(void)
{
	char *ptr1, *ptr2;
	size_t size = 43;

	pr_info("use-after-free after another kmalloc\n");
	ptr1 = kmalloc(size, GFP_KERNEL);
	if (!ptr1) {
		pr_err("Allocation failed\n");
		return;
	}

	kfree(ptr1);
	ptr2 = kmalloc(size, GFP_KERNEL);
	if (!ptr2) {
		pr_err("Allocation failed\n");
		return;
	}

	ptr1[40] = 'x';
	if (ptr1 == ptr2)
		pr_err("Could not detect use-after-free: ptr1 == ptr2\n");
	kfree(ptr2);
}

static noinline void __init kfree_via_page(void)
{
	char *ptr;
	size_t size = 8;
	struct page *page;
	unsigned long offset;

	pr_info("invalid-free false positive (via page)\n");
	ptr = kmalloc(size, GFP_KERNEL);
	if (!ptr) {
		pr_err("Allocation failed\n");
		return;
	}

	page = virt_to_page(ptr);
	offset = offset_in_page(ptr);
	kfree(page_address(page) + offset);
}

static noinline void __init kfree_via_phys(void)
{
	char *ptr;
	size_t size = 8;
	phys_addr_t phys;

	pr_info("invalid-free false positive (via phys)\n");
	ptr = kmalloc(size, GFP_KERNEL);
	if (!ptr) {
		pr_err("Allocation failed\n");
		return;
	}

	phys = virt_to_phys(ptr);
	kfree(phys_to_virt(phys));
}

static noinline void __init kmem_cache_oob(void)
{
	char *p;
	size_t size = 200;
	struct kmem_cache *cache = kmem_cache_create("test_cache",
						size, 0,
						0, NULL);
	if (!cache) {
		pr_err("Cache allocation failed\n");
		return;
	}
	pr_info("out-of-bounds in kmem_cache_alloc\n");
	p = kmem_cache_alloc(cache, GFP_KERNEL);
	if (!p) {
		pr_err("Allocation failed\n");
		kmem_cache_destroy(cache);
		return;
	}

	*p = p[size];
	kmem_cache_free(cache, p);
	kmem_cache_destroy(cache);
}

static noinline void __init memcg_accounted_kmem_cache(void)
{
	int i;
	char *p;
	size_t size = 200;
	struct kmem_cache *cache;

	cache = kmem_cache_create("test_cache", size, 0, SLAB_ACCOUNT, NULL);
	if (!cache) {
		pr_err("Cache allocation failed\n");
		return;
	}

	pr_info("allocate memcg accounted object\n");
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

static noinline void __init kasan_global_oob(void)
{
	volatile int i = 3;
	char *p = &global_array[ARRAY_SIZE(global_array) + i];

	pr_info("out-of-bounds global variable\n");
	*(volatile char *)p;
}

static noinline void __init kasan_stack_oob(void)
{
	char stack_array[10];
	volatile int i = 0;
	char *p = &stack_array[ARRAY_SIZE(stack_array) + i];

	pr_info("out-of-bounds on stack\n");
	*(volatile char *)p;
}

static noinline void __init ksize_unpoisons_memory(void)
{
	char *ptr;
	size_t size = 123, real_size;

	pr_info("ksize() unpoisons the whole allocated chunk\n");
	ptr = kmalloc(size, GFP_KERNEL);
	if (!ptr) {
		pr_err("Allocation failed\n");
		return;
	}
	real_size = ksize(ptr);
	/* This access doesn't trigger an error. */
	ptr[size] = 'x';
	/* This one does. */
	ptr[real_size] = 'y';
	kfree(ptr);
}

static noinline void __init copy_user_test(void)
{
	char *kmem;
	char __user *usermem;
	size_t size = 10;
	int unused;

	kmem = kmalloc(size, GFP_KERNEL);
	if (!kmem)
		return;

	usermem = (char __user *)vm_mmap(NULL, 0, PAGE_SIZE,
			    PROT_READ | PROT_WRITE | PROT_EXEC,
			    MAP_ANONYMOUS | MAP_PRIVATE, 0);
	if (IS_ERR(usermem)) {
		pr_err("Failed to allocate user memory\n");
		kfree(kmem);
		return;
	}

	pr_info("out-of-bounds in copy_from_user()\n");
	unused = copy_from_user(kmem, usermem, size + 1);

	pr_info("out-of-bounds in copy_to_user()\n");
	unused = copy_to_user(usermem, kmem, size + 1);

	pr_info("out-of-bounds in __copy_from_user()\n");
	unused = __copy_from_user(kmem, usermem, size + 1);

	pr_info("out-of-bounds in __copy_to_user()\n");
	unused = __copy_to_user(usermem, kmem, size + 1);

	pr_info("out-of-bounds in __copy_from_user_inatomic()\n");
	unused = __copy_from_user_inatomic(kmem, usermem, size + 1);

	pr_info("out-of-bounds in __copy_to_user_inatomic()\n");
	unused = __copy_to_user_inatomic(usermem, kmem, size + 1);

	pr_info("out-of-bounds in strncpy_from_user()\n");
	unused = strncpy_from_user(kmem, usermem, size + 1);

	vm_munmap((unsigned long)usermem, PAGE_SIZE);
	kfree(kmem);
}

static noinline void __init kasan_alloca_oob_left(void)
{
	volatile int i = 10;
	char alloca_array[i];
	char *p = alloca_array - 1;

	pr_info("out-of-bounds to left on alloca\n");
	*(volatile char *)p;
}

static noinline void __init kasan_alloca_oob_right(void)
{
	volatile int i = 10;
	char alloca_array[i];
	char *p = alloca_array + i;

	pr_info("out-of-bounds to right on alloca\n");
	*(volatile char *)p;
}

static noinline void __init kmem_cache_double_free(void)
{
	char *p;
	size_t size = 200;
	struct kmem_cache *cache;

	cache = kmem_cache_create("test_cache", size, 0, 0, NULL);
	if (!cache) {
		pr_err("Cache allocation failed\n");
		return;
	}
	pr_info("double-free on heap object\n");
	p = kmem_cache_alloc(cache, GFP_KERNEL);
	if (!p) {
		pr_err("Allocation failed\n");
		kmem_cache_destroy(cache);
		return;
	}

	kmem_cache_free(cache, p);
	kmem_cache_free(cache, p);
	kmem_cache_destroy(cache);
}

static noinline void __init kmem_cache_invalid_free(void)
{
	char *p;
	size_t size = 200;
	struct kmem_cache *cache;

	cache = kmem_cache_create("test_cache", size, 0, SLAB_TYPESAFE_BY_RCU,
				  NULL);
	if (!cache) {
		pr_err("Cache allocation failed\n");
		return;
	}
	pr_info("invalid-free of heap object\n");
	p = kmem_cache_alloc(cache, GFP_KERNEL);
	if (!p) {
		pr_err("Allocation failed\n");
		kmem_cache_destroy(cache);
		return;
	}

	/* Trigger invalid free, the object doesn't get freed */
	kmem_cache_free(cache, p + 1);

	/*
	 * Properly free the object to prevent the "Objects remaining in
	 * test_cache on __kmem_cache_shutdown" BUG failure.
	 */
	kmem_cache_free(cache, p);

	kmem_cache_destroy(cache);
}

static noinline void __init kasan_memchr(void)
{
	char *ptr;
	size_t size = 24;

	pr_info("out-of-bounds in memchr\n");
	ptr = kmalloc(size, GFP_KERNEL | __GFP_ZERO);
	if (!ptr)
		return;

	kasan_ptr_result = memchr(ptr, '1', size + 1);
	kfree(ptr);
}

static noinline void __init kasan_memcmp(void)
{
	char *ptr;
	size_t size = 24;
	int arr[9];

	pr_info("out-of-bounds in memcmp\n");
	ptr = kmalloc(size, GFP_KERNEL | __GFP_ZERO);
	if (!ptr)
		return;

	memset(arr, 0, sizeof(arr));
	kasan_int_result = memcmp(ptr, arr, size + 1);
	kfree(ptr);
}

static noinline void __init kasan_strings(void)
{
	char *ptr;
	size_t size = 24;

	pr_info("use-after-free in strchr\n");
	ptr = kmalloc(size, GFP_KERNEL | __GFP_ZERO);
	if (!ptr)
		return;

	kfree(ptr);

	/*
	 * Try to cause only 1 invalid access (less spam in dmesg).
	 * For that we need ptr to point to zeroed byte.
	 * Skip metadata that could be stored in freed object so ptr
	 * will likely point to zeroed byte.
	 */
	ptr += 16;
	kasan_ptr_result = strchr(ptr, '1');

	pr_info("use-after-free in strrchr\n");
	kasan_ptr_result = strrchr(ptr, '1');

	pr_info("use-after-free in strcmp\n");
	kasan_int_result = strcmp(ptr, "2");

	pr_info("use-after-free in strncmp\n");
	kasan_int_result = strncmp(ptr, "2", 1);

	pr_info("use-after-free in strlen\n");
	kasan_int_result = strlen(ptr);

	pr_info("use-after-free in strnlen\n");
	kasan_int_result = strnlen(ptr, 1);
}

static noinline void __init kasan_bitops(void)
{
	/*
	 * Allocate 1 more byte, which causes kzalloc to round up to 16-bytes;
	 * this way we do not actually corrupt other memory.
	 */
	long *bits = kzalloc(sizeof(*bits) + 1, GFP_KERNEL);
	if (!bits)
		return;

	/*
	 * Below calls try to access bit within allocated memory; however, the
	 * below accesses are still out-of-bounds, since bitops are defined to
	 * operate on the whole long the bit is in.
	 */
	pr_info("out-of-bounds in set_bit\n");
	set_bit(BITS_PER_LONG, bits);

	pr_info("out-of-bounds in __set_bit\n");
	__set_bit(BITS_PER_LONG, bits);

	pr_info("out-of-bounds in clear_bit\n");
	clear_bit(BITS_PER_LONG, bits);

	pr_info("out-of-bounds in __clear_bit\n");
	__clear_bit(BITS_PER_LONG, bits);

	pr_info("out-of-bounds in clear_bit_unlock\n");
	clear_bit_unlock(BITS_PER_LONG, bits);

	pr_info("out-of-bounds in __clear_bit_unlock\n");
	__clear_bit_unlock(BITS_PER_LONG, bits);

	pr_info("out-of-bounds in change_bit\n");
	change_bit(BITS_PER_LONG, bits);

	pr_info("out-of-bounds in __change_bit\n");
	__change_bit(BITS_PER_LONG, bits);

	/*
	 * Below calls try to access bit beyond allocated memory.
	 */
	pr_info("out-of-bounds in test_and_set_bit\n");
	test_and_set_bit(BITS_PER_LONG + BITS_PER_BYTE, bits);

	pr_info("out-of-bounds in __test_and_set_bit\n");
	__test_and_set_bit(BITS_PER_LONG + BITS_PER_BYTE, bits);

	pr_info("out-of-bounds in test_and_set_bit_lock\n");
	test_and_set_bit_lock(BITS_PER_LONG + BITS_PER_BYTE, bits);

	pr_info("out-of-bounds in test_and_clear_bit\n");
	test_and_clear_bit(BITS_PER_LONG + BITS_PER_BYTE, bits);

	pr_info("out-of-bounds in __test_and_clear_bit\n");
	__test_and_clear_bit(BITS_PER_LONG + BITS_PER_BYTE, bits);

	pr_info("out-of-bounds in test_and_change_bit\n");
	test_and_change_bit(BITS_PER_LONG + BITS_PER_BYTE, bits);

	pr_info("out-of-bounds in __test_and_change_bit\n");
	__test_and_change_bit(BITS_PER_LONG + BITS_PER_BYTE, bits);

	pr_info("out-of-bounds in test_bit\n");
	kasan_int_result = test_bit(BITS_PER_LONG + BITS_PER_BYTE, bits);

#if defined(clear_bit_unlock_is_negative_byte)
	pr_info("out-of-bounds in clear_bit_unlock_is_negative_byte\n");
	kasan_int_result = clear_bit_unlock_is_negative_byte(BITS_PER_LONG +
		BITS_PER_BYTE, bits);
#endif
	kfree(bits);
}

static noinline void __init kmalloc_double_kzfree(void)
{
	char *ptr;
	size_t size = 16;

	pr_info("double-free (kzfree)\n");
	ptr = kmalloc(size, GFP_KERNEL);
	if (!ptr) {
		pr_err("Allocation failed\n");
		return;
	}

	kzfree(ptr);
	kzfree(ptr);
}

#ifdef CONFIG_KASAN_VMALLOC
static noinline void __init vmalloc_oob(void)
{
	void *area;

	pr_info("vmalloc out-of-bounds\n");

	/*
	 * We have to be careful not to hit the guard page.
	 * The MMU will catch that and crash us.
	 */
	area = vmalloc(3000);
	if (!area) {
		pr_err("Allocation failed\n");
		return;
	}

	((volatile char *)area)[3100];
	vfree(area);
}
#else
static void __init vmalloc_oob(void) {}
#endif

static int __init kmalloc_tests_init(void)
{
	/*
	 * Temporarily enable multi-shot mode. Otherwise, we'd only get a
	 * report for the first case.
	 */
	bool multishot = kasan_save_enable_multi_shot();

	kmalloc_oob_right();
	kmalloc_oob_left();
	kmalloc_node_oob_right();
#ifdef CONFIG_SLUB
	kmalloc_pagealloc_oob_right();
	kmalloc_pagealloc_uaf();
	kmalloc_pagealloc_invalid_free();
#endif
	kmalloc_large_oob_right();
	kmalloc_oob_krealloc_more();
	kmalloc_oob_krealloc_less();
	kmalloc_oob_16();
	kmalloc_oob_in_memset();
	kmalloc_oob_memset_2();
	kmalloc_oob_memset_4();
	kmalloc_oob_memset_8();
	kmalloc_oob_memset_16();
	kmalloc_memmove_invalid_size();
	kmalloc_uaf();
	kmalloc_uaf_memset();
	kmalloc_uaf2();
	kfree_via_page();
	kfree_via_phys();
	kmem_cache_oob();
	memcg_accounted_kmem_cache();
	kasan_stack_oob();
	kasan_global_oob();
	kasan_alloca_oob_left();
	kasan_alloca_oob_right();
	ksize_unpoisons_memory();
	copy_user_test();
	kmem_cache_double_free();
	kmem_cache_invalid_free();
	kasan_memchr();
	kasan_memcmp();
	kasan_strings();
	kasan_bitops();
	kmalloc_double_kzfree();
	vmalloc_oob();

	kasan_restore_multi_shot(multishot);

	return -EAGAIN;
}

module_init(kmalloc_tests_init);
MODULE_LICENSE("GPL");
