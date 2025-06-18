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
#include <kunit/test.h>

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
			    !defined(CONFIG_SPARC32) &&		\
			    !defined(CONFIG_SUPERH))
# define TEST_U64
#endif

struct usercopy_test_priv {
	char *kmem;
	char __user *umem;
	size_t size;
};

static bool is_zeroed(void *from, size_t size)
{
	return memchr_inv(from, 0x0, size) == NULL;
}

/* Test usage of check_nonzero_user(). */
static void usercopy_test_check_nonzero_user(struct kunit *test)
{
	size_t start, end, i, zero_start, zero_end;
	struct usercopy_test_priv *priv = test->priv;
	char __user *umem = priv->umem;
	char *kmem = priv->kmem;
	size_t size = priv->size;

	KUNIT_ASSERT_GE_MSG(test, size, 2 * PAGE_SIZE, "buffer too small");

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

	KUNIT_EXPECT_EQ_MSG(test, copy_to_user(umem, kmem, size), 0,
		"legitimate copy_to_user failed");

	for (start = 0; start <= size; start++) {
		for (end = start; end <= size; end++) {
			size_t len = end - start;
			int retval = check_zeroed_user(umem + start, len);
			int expected = is_zeroed(kmem + start, len);

			KUNIT_ASSERT_EQ_MSG(test, retval, expected,
				"check_nonzero_user(=%d) != memchr_inv(=%d) mismatch (start=%zu, end=%zu)",
				retval, expected, start, end);
		}
	}
}

/* Test usage of copy_struct_from_user(). */
static void usercopy_test_copy_struct_from_user(struct kunit *test)
{
	char *umem_src = NULL, *expected = NULL;
	struct usercopy_test_priv *priv = test->priv;
	char __user *umem = priv->umem;
	char *kmem = priv->kmem;
	size_t size = priv->size;
	size_t ksize, usize;

	umem_src = kunit_kmalloc(test, size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, umem_src);

	expected = kunit_kmalloc(test, size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, expected);

	/* Fill umem with a fixed byte pattern. */
	memset(umem_src, 0x3e, size);
	KUNIT_ASSERT_EQ_MSG(test, copy_to_user(umem, umem_src, size), 0,
		    "legitimate copy_to_user failed");

	/* Check basic case -- (usize == ksize). */
	ksize = size;
	usize = size;

	memcpy(expected, umem_src, ksize);

	memset(kmem, 0x0, size);
	KUNIT_EXPECT_EQ_MSG(test, copy_struct_from_user(kmem, ksize, umem, usize), 0,
		    "copy_struct_from_user(usize == ksize) failed");
	KUNIT_EXPECT_MEMEQ_MSG(test, kmem, expected, ksize,
		    "copy_struct_from_user(usize == ksize) gives unexpected copy");

	/* Old userspace case -- (usize < ksize). */
	ksize = size;
	usize = size / 2;

	memcpy(expected, umem_src, usize);
	memset(expected + usize, 0x0, ksize - usize);

	memset(kmem, 0x0, size);
	KUNIT_EXPECT_EQ_MSG(test, copy_struct_from_user(kmem, ksize, umem, usize), 0,
		    "copy_struct_from_user(usize < ksize) failed");
	KUNIT_EXPECT_MEMEQ_MSG(test, kmem, expected, ksize,
		    "copy_struct_from_user(usize < ksize) gives unexpected copy");

	/* New userspace (-E2BIG) case -- (usize > ksize). */
	ksize = size / 2;
	usize = size;

	memset(kmem, 0x0, size);
	KUNIT_EXPECT_EQ_MSG(test, copy_struct_from_user(kmem, ksize, umem, usize), -E2BIG,
		    "copy_struct_from_user(usize > ksize) didn't give E2BIG");

	/* New userspace (success) case -- (usize > ksize). */
	ksize = size / 2;
	usize = size;

	memcpy(expected, umem_src, ksize);
	KUNIT_EXPECT_EQ_MSG(test, clear_user(umem + ksize, usize - ksize), 0,
		    "legitimate clear_user failed");

	memset(kmem, 0x0, size);
	KUNIT_EXPECT_EQ_MSG(test, copy_struct_from_user(kmem, ksize, umem, usize), 0,
		    "copy_struct_from_user(usize > ksize) failed");
	KUNIT_EXPECT_MEMEQ_MSG(test, kmem, expected, ksize,
		    "copy_struct_from_user(usize > ksize) gives unexpected copy");
}

/*
 * Legitimate usage: none of these copies should fail.
 */
static void usercopy_test_valid(struct kunit *test)
{
	struct usercopy_test_priv *priv = test->priv;
	char __user *usermem = priv->umem;
	char *kmem = priv->kmem;

	memset(kmem, 0x3a, PAGE_SIZE * 2);
	KUNIT_EXPECT_EQ_MSG(test, 0, copy_to_user(usermem, kmem, PAGE_SIZE),
	     "legitimate copy_to_user failed");
	memset(kmem, 0x0, PAGE_SIZE);
	KUNIT_EXPECT_EQ_MSG(test, 0, copy_from_user(kmem, usermem, PAGE_SIZE),
	     "legitimate copy_from_user failed");
	KUNIT_EXPECT_MEMEQ_MSG(test, kmem, kmem + PAGE_SIZE, PAGE_SIZE,
	     "legitimate usercopy failed to copy data");

#define test_legit(size, check)						\
	do {								\
		size val_##size = (check);				\
		KUNIT_EXPECT_EQ_MSG(test, 0,				\
			put_user(val_##size, (size __user *)usermem),	\
			"legitimate put_user (" #size ") failed");	\
		val_##size = 0;						\
		KUNIT_EXPECT_EQ_MSG(test, 0,				\
			get_user(val_##size, (size __user *)usermem),	\
			"legitimate get_user (" #size ") failed");	\
		KUNIT_EXPECT_EQ_MSG(test, val_##size, check,		\
			"legitimate get_user (" #size ") failed to do copy"); \
	} while (0)

	test_legit(u8,  0x5a);
	test_legit(u16, 0x5a5b);
	test_legit(u32, 0x5a5b5c5d);
#ifdef TEST_U64
	test_legit(u64, 0x5a5b5c5d6a6b6c6d);
#endif
#undef test_legit
}

/*
 * Invalid usage: none of these copies should succeed.
 */
static void usercopy_test_invalid(struct kunit *test)
{
	struct usercopy_test_priv *priv = test->priv;
	char __user *usermem = priv->umem;
	char *bad_usermem = (char *)usermem;
	char *kmem = priv->kmem;
	u64 *kmem_u64 = (u64 *)kmem;

	if (IS_ENABLED(CONFIG_ALTERNATE_USER_ADDRESS_SPACE) ||
	    !IS_ENABLED(CONFIG_MMU)) {
		kunit_skip(test, "Testing for kernel/userspace address confusion is only sensible on architectures with a shared address space");
		return;
	}

	/* Prepare kernel memory with check values. */
	memset(kmem, 0x5a, PAGE_SIZE);
	memset(kmem + PAGE_SIZE, 0, PAGE_SIZE);

	/* Reject kernel-to-kernel copies through copy_from_user(). */
	KUNIT_EXPECT_NE_MSG(test, copy_from_user(kmem, (char __user *)(kmem + PAGE_SIZE),
						 PAGE_SIZE), 0,
		    "illegal all-kernel copy_from_user passed");

	/* Destination half of buffer should have been zeroed. */
	KUNIT_EXPECT_MEMEQ_MSG(test, kmem + PAGE_SIZE, kmem, PAGE_SIZE,
		    "zeroing failure for illegal all-kernel copy_from_user");

#if 0
	/*
	 * When running with SMAP/PAN/etc, this will Oops the kernel
	 * due to the zeroing of userspace memory on failure. This needs
	 * to be tested in LKDTM instead, since this test module does not
	 * expect to explode.
	 */
	KUNIT_EXPECT_NE_MSG(test, copy_from_user(bad_usermem, (char __user *)kmem,
						 PAGE_SIZE), 0,
		    "illegal reversed copy_from_user passed");
#endif
	KUNIT_EXPECT_NE_MSG(test, copy_to_user((char __user *)kmem, kmem + PAGE_SIZE,
					       PAGE_SIZE), 0,
		    "illegal all-kernel copy_to_user passed");

	KUNIT_EXPECT_NE_MSG(test, copy_to_user((char __user *)kmem, bad_usermem,
					       PAGE_SIZE), 0,
		    "illegal reversed copy_to_user passed");

#define test_illegal(size, check)							\
	do {										\
		size val_##size = (check);						\
		/* get_user() */							\
		KUNIT_EXPECT_NE_MSG(test, get_user(val_##size, (size __user *)kmem), 0,	\
		    "illegal get_user (" #size ") passed");				\
		KUNIT_EXPECT_EQ_MSG(test, val_##size, 0,				\
		    "zeroing failure for illegal get_user (" #size ")");		\
		/* put_user() */							\
		*kmem_u64 = 0xF09FA4AFF09FA4AF;						\
		KUNIT_EXPECT_NE_MSG(test, put_user(val_##size, (size __user *)kmem), 0,	\
		    "illegal put_user (" #size ") passed");				\
		KUNIT_EXPECT_EQ_MSG(test, *kmem_u64, 0xF09FA4AFF09FA4AF,		\
		    "illegal put_user (" #size ") wrote to kernel memory!");		\
	} while (0)

	test_illegal(u8,  0x5a);
	test_illegal(u16, 0x5a5b);
	test_illegal(u32, 0x5a5b5c5d);
#ifdef TEST_U64
	test_illegal(u64, 0x5a5b5c5d6a6b6c6d);
#endif
#undef test_illegal
}

static int usercopy_test_init(struct kunit *test)
{
	struct usercopy_test_priv *priv;
	unsigned long user_addr;

	if (!IS_ENABLED(CONFIG_MMU)) {
		kunit_skip(test, "Userspace allocation testing not available on non-MMU systems");
		return 0;
	}

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv);
	test->priv = priv;
	priv->size = PAGE_SIZE * 2;

	priv->kmem = kunit_kmalloc(test, priv->size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv->kmem);

	user_addr = kunit_vm_mmap(test, NULL, 0, priv->size,
			    PROT_READ | PROT_WRITE | PROT_EXEC,
			    MAP_ANONYMOUS | MAP_PRIVATE, 0);
	KUNIT_ASSERT_NE_MSG(test, user_addr, 0,
		"Could not create userspace mm");
	KUNIT_ASSERT_LT_MSG(test, user_addr, (unsigned long)TASK_SIZE,
		"Failed to allocate user memory");
	priv->umem = (char __user *)user_addr;

	return 0;
}

static struct kunit_case usercopy_test_cases[] = {
	KUNIT_CASE(usercopy_test_valid),
	KUNIT_CASE(usercopy_test_invalid),
	KUNIT_CASE(usercopy_test_check_nonzero_user),
	KUNIT_CASE(usercopy_test_copy_struct_from_user),
	{}
};

static struct kunit_suite usercopy_test_suite = {
	.name = "usercopy",
	.init = usercopy_test_init,
	.test_cases = usercopy_test_cases,
};

kunit_test_suites(&usercopy_test_suite);
MODULE_AUTHOR("Kees Cook <kees@kernel.org>");
MODULE_DESCRIPTION("Kernel module for testing copy_to/from_user infrastructure");
MODULE_LICENSE("GPL");
