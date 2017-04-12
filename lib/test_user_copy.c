/*
 * Kernel module for testing copy_to/from_user infrastructure.
 *
 * Copyright 2013 Google Inc. All Rights Reserved
 *
 * Authors:
 *      Kees Cook       <keescook@chromium.org>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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
			    !defined(CONFIG_AVR32) &&		\
			    !defined(CONFIG_BLACKFIN) &&	\
			    !defined(CONFIG_M32R) &&		\
			    !defined(CONFIG_M68K) &&		\
			    !defined(CONFIG_MICROBLAZE) &&	\
			    !defined(CONFIG_MN10300) &&		\
			    !defined(CONFIG_NIOS2) &&		\
			    !defined(CONFIG_PPC32) &&		\
			    !defined(CONFIG_SUPERH))
# define TEST_U64
#endif

#define test(condition, msg)		\
({					\
	int cond = (condition);		\
	if (cond)			\
		pr_warn("%s\n", msg);	\
	cond;				\
})

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
