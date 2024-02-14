// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kernel module for testing copy_to/from_user infrastructure.
 *
 * Copyright 2013 Google Inc. All Rights Reserved
 *
 * Authors:
 *      Kees Cook       <keescook@chromium.org>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/mman.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

/*
 * Several 32-bit architectures support 64-bit {get,put}_user() calls.
 * As there doesn't appear to be anything that can safely determine
 * their capability at compile-time, we just have to opt-out certain archs.
 */
#if BITS_PER_LONG == 64 || (!(defined(CONFIG_ARM) && !defined(MMU)) && \
			    !defined(CONFIG_M68K) &&		\
			    !defined(CONFIG_MICROBLAZE) &&	\
			    !defined(CONFIG_NIOS2) &&		\
			    !defined(CONFIG_PPC32) &&		\
			    !defined(CONFIG_SUPERH))
# define TEST_U64
#endif

#define test(condition, msg, ...)					\
({									\
	int cond = (condition);						\
	if (cond)							\
		pr_warn("[%d] " msg "\n", __LINE__, ##__VA_ARGS__);	\
	cond;								\
})

static bool is_zeroed(void *from, size_t size)
{
	return memchr_inv(from, 0x0, size) == NULL;
}

static int test_check_nonzero_user(char *kmem, char __user *umem, size_t size)
{
	int ret = 0;
	size_t start, end, i, zero_start, zero_end;

	if (test(size < 2 * PAGE_SIZE, "buffer too small"))
		return -EINVAL;

	/*
	 * We want to cross a page boundary to exercise the code more
	 * effectively. We also don't want to make the size we scan too large,
	 * otherwise the test can take a long time and cause soft lockups. So
	 * scan a 1024 byte region across the page boundary.
	 */
	size = 1024;
	start = PAGE_SIZE - (size / 2);

	kmem += start;
	umem += start;

	zero_start = size / 4;
	zero_end = size - zero_start;

	/*
	 * We conduct a series of check_nonzero_user() tests on a block of
	 * memory with the following byte-pattern (trying every possible
	 * [start,end] pair):
	 *
	 *   [ 00 ff 00 ff ... 00 00 00 00 ... ff 00 ff 00 ]
	 *
	 * And we verify that check_nonzero_user() acts identically to
	 * memchr_inv().
	 */

	memset(kmem, 0x0, size);
	for (i = 1; i < zero_start; i += 2)
		kmem[i] = 0xff;
	for (i = zero_end; i < size; i += 2)
		kmem[i] = 0xff;

	ret |= test(copy_to_user(umem, kmem, size),
		    "legitimate copy_to_user failed");

	for (start = 0; start <= size; start++) {
		for (end = start; end <= size; end++) {
			size_t len = end - start;
			int retval = check_zeroed_user(umem + start, len);
			int expected = is_zeroed(kmem + start, len);

			ret |= test(retval != expected,
				    "check_nonzero_user(=%d) != memchr_inv(=%d) mismatch (start=%zu, end=%zu)",
				    retval, expected, start, end);
		}
	}

	return ret;
}

static int test_copy_struct_from_user(char *kmem, char __user *umem,
				      size_t size)
{
	int ret = 0;
	char *umem_src = NULL, *expected = NULL;
	size_t ksize, usize;

	umem_src = kmalloc(size, GFP_KERNEL);
	ret = test(umem_src == NULL, "kmalloc failed");
	if (ret)
		goto out_free;

	expected = kmalloc(size, GFP_KERNEL);
	ret = test(expected == NULL, "kmalloc failed");
	if (ret)
		goto out_free;

	/* Fill umem with a fixed byte pattern. */
	memset(umem_src, 0x3e, size);
	ret |= test(copy_to_user(umem, umem_src, size),
		    "legitimate copy_to_user failed");

	/* Check basic case -- (usize == ksize). */
	ksize = size;
	usize = size;

	memcpy(expected, umem_src, ksize);

	memset(kmem, 0x0, size);
	ret |= test(copy_struct_from_user(kmem, ksize, umem, usize),
		    "copy_struct_from_user(usize == ksize) failed");
	ret |= test(memcmp(kmem, expected, ksize),
		    "copy_struct_from_user(usize == ksize) gives unexpected copy");

	/* Old userspace case -- (usize < ksize). */
	ksize = size;
	usize = size / 2;

	memcpy(expected, umem_src, usize);
	memset(expected + usize, 0x0, ksize - usize);

	memset(kmem, 0x0, size);
	ret |= test(copy_struct_from_user(kmem, ksize, umem, usize),
		    "copy_struct_from_user(usize < ksize) failed");
	ret |= test(memcmp(kmem, expected, ksize),
		    "copy_struct_from_user(usize < ksize) gives unexpected copy");

	/* New userspace (-E2BIG) case -- (usize > ksize). */
	ksize = size / 2;
	usize = size;

	memset(kmem, 0x0, size);
	ret |= test(copy_struct_from_user(kmem, ksize, umem, usize) != -E2BIG,
		    "copy_struct_from_user(usize > ksize) didn't give E2BIG");

	/* New userspace (success) case -- (usize > ksize). */
	ksize = size / 2;
	usize = size;

	memcpy(expected, umem_src, ksize);
	ret |= test(clear_user(umem + ksize, usize - ksize),
		    "legitimate clear_user failed");

	memset(kmem, 0x0, size);
	ret |= test(copy_struct_from_user(kmem, ksize, umem, usize),
		    "copy_struct_from_user(usize > ksize) failed");
	ret |= test(memcmp(kmem, expected, ksize),
		    "copy_struct_from_user(usize > ksize) gives unexpected copy");

out_free:
	kfree(expected);
	kfree(umem_src);
	return ret;
}

static int __init test_user_copy_init(void)
{
	int ret = 0;
	char *kmem;
	char __user *usermem;
	char *bad_usermem;
	unsigned long user_addr;
	u8 val_u8;
	u16 val_u16;
	u32 val_u32;
#ifdef TEST_U64
	u64 val_u64;
#endif

	kmem = kmalloc(PAGE_SIZE * 2, GFP_KERNEL);
	if (!kmem)
		return -ENOMEM;

	user_addr = vm_mmap(NULL, 0, PAGE_SIZE * 2,
			    PROT_READ | PROT_WRITE | PROT_EXEC,
			    MAP_ANONYMOUS | MAP_PRIVATE, 0);
	if (user_addr >= (unsigned long)(TASK_SIZE)) {
		pr_warn("Failed to allocate user memory\n");
		kfree(kmem);
		return -ENOMEM;
	}

	usermem = (char __user *)user_addr;
	bad_usermem = (char *)user_addr;

	/*
	 * Legitimate usage: none of these copies should fail.
	 */
	memset(kmem, 0x3a, PAGE_SIZE * 2);
	ret |= test(copy_to_user(usermem, kmem, PAGE_SIZE),
		    "legitimate copy_to_user failed");
	memset(kmem, 0x0, PAGE_SIZE);
	ret |= test(copy_from_user(kmem, usermem, PAGE_SIZE),
		    "legitimate copy_from_user failed");
	ret |= test(memcmp(kmem, kmem + PAGE_SIZE, PAGE_SIZE),
		    "legitimate usercopy failed to copy data");

#define test_legit(size, check)						  \
	do {								  \
		val_##size = check;					  \
		ret |= test(put_user(val_##size, (size __user *)usermem), \
		    "legitimate put_user (" #size ") failed");		  \
		val_##size = 0;						  \
		ret |= test(get_user(val_##size, (size __user *)usermem), \
		    "legitimate get_user (" #size ") failed");		  \
		ret |= test(val_##size != check,			  \
		    "legitimate get_user (" #size ") failed to do copy"); \
		if (val_##size != check) {				  \
			pr_info("0x%llx != 0x%llx\n",			  \
				(unsigned long long)val_##size,		  \
				(unsigned long long)check);		  \
		}							  \
	} while (0)

	test_legit(u8,  0x5a);
	test_legit(u16, 0x5a5b);
	test_legit(u32, 0x5a5b5c5d);
#ifdef TEST_U64
	test_legit(u64, 0x5a5b5c5d6a6b6c6d);
#endif
#undef test_legit

	/* Test usage of check_nonzero_user(). */
	ret |= test_check_nonzero_user(kmem, usermem, 2 * PAGE_SIZE);
	/* Test usage of copy_struct_from_user(). */
	ret |= test_copy_struct_from_user(kmem, usermem, 2 * PAGE_SIZE);

	/*
	 * Invalid usage: none of these copies should succeed.
	 */

	/* Prepare kernel memory with check values. */
	memset(kmem, 0x5a, PAGE_SIZE);
	memset(kmem + PAGE_SIZE, 0, PAGE_SIZE);

	/* Reject kernel-to-kernel copies through copy_from_user(). */
	ret |= test(!copy_from_user(kmem, (char __user *)(kmem + PAGE_SIZE),
				    PAGE_SIZE),
		    "illegal all-kernel copy_from_user passed");

	/* Destination half of buffer should have been zeroed. */
	ret |= test(memcmp(kmem + PAGE_SIZE, kmem, PAGE_SIZE),
		    "zeroing failure for illegal all-kernel copy_from_user");

#if 0
	/*
	 * When running with SMAP/PAN/etc, this will Oops the kernel
	 * due to the zeroing of userspace memory on failure. This needs
	 * to be tested in LKDTM instead, since this test module does not
	 * expect to explode.
	 */
	ret |= test(!copy_from_user(bad_usermem, (char __user *)kmem,
				    PAGE_SIZE),
		    "illegal reversed copy_from_user passed");
#endif
	ret |= test(!copy_to_user((char __user *)kmem, kmem + PAGE_SIZE,
				  PAGE_SIZE),
		    "illegal all-kernel copy_to_user passed");
	ret |= test(!copy_to_user((char __user *)kmem, bad_usermem,
				  PAGE_SIZE),
		    "illegal reversed copy_to_user passed");

#define test_illegal(size, check)					    \
	do {								    \
		val_##size = (check);					    \
		ret |= test(!get_user(val_##size, (size __user *)kmem),	    \
		    "illegal get_user (" #size ") passed");		    \
		ret |= test(val_##size != (size)0,			    \
		    "zeroing failure for illegal get_user (" #size ")");    \
		if (val_##size != (size)0) {				    \
			pr_info("0x%llx != 0\n",			    \
				(unsigned long long)val_##size);	    \
		}							    \
		ret |= test(!put_user(val_##size, (size __user *)kmem),	    \
		    "illegal put_user (" #size ") passed");		    \
	} while (0)

	test_illegal(u8,  0x5a);
	test_illegal(u16, 0x5a5b);
	test_illegal(u32, 0x5a5b5c5d);
#ifdef TEST_U64
	test_illegal(u64, 0x5a5b5c5d6a6b6c6d);
#endif
#undef test_illegal

	vm_munmap(user_addr, PAGE_SIZE * 2);
	kfree(kmem);

	if (ret == 0) {
		pr_info("tests passed.\n");
		return 0;
	}

	return -EINVAL;
}

module_init(test_user_copy_init);

static void __exit test_user_copy_exit(void)
{
	pr_info("unloaded.\n");
}

module_exit(test_user_copy_exit);

MODULE_AUTHOR("Kees Cook <keescook@chromium.org>");
MODULE_LICENSE("GPL");
