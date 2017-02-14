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
	char *zerokmem;
	u8 val_u8;
	u16 val_u16;
	u32 val_u32;
	u64 val_u64;

	kmem = kmalloc(PAGE_SIZE * 2, GFP_KERNEL);
	if (!kmem)
		return -ENOMEM;

	zerokmem = kzalloc(PAGE_SIZE * 2, GFP_KERNEL);
	if (!zerokmem) {
		ret = -ENOMEM;
		goto out_kmem;
	}

	user_addr = vm_mmap(NULL, 0, PAGE_SIZE * 2,
			    PROT_READ | PROT_WRITE | PROT_EXEC,
			    MAP_ANONYMOUS | MAP_PRIVATE, 0);
	if (user_addr >= (unsigned long)(TASK_SIZE)) {
		pr_warn("Failed to allocate user memory\n");
		ret = -ENOMEM;
		goto out_zerokmem;
	}

	usermem = (char __user *)user_addr;
	bad_usermem = (char *)user_addr;

	/*
	 * Legitimate usage: none of these copies should fail.
	 */
	ret |= test(copy_from_user(kmem, usermem, PAGE_SIZE),
		    "legitimate copy_from_user failed");
	ret |= test(copy_to_user(usermem, kmem, PAGE_SIZE),
		    "legitimate copy_to_user failed");

#define test_legit(size)						  \
	do {								  \
		ret |= test(get_user(val_##size, (size __user *)usermem), \
		    "legitimate get_user (" #size ") failed");		  \
		ret |= test(put_user(val_##size, (size __user *)usermem), \
		    "legitimate put_user (" #size ") failed");		  \
	} while (0)

	test_legit(u8);
	test_legit(u16);
	test_legit(u32);
	test_legit(u64);
#undef test_legit

	/*
	 * Invalid usage: none of these copies should succeed.
	 */

	/* Prepare kernel memory with check values. */
	memset(kmem, 0x5A, PAGE_SIZE);
	memset(kmem + PAGE_SIZE, 0x5B, PAGE_SIZE);

	/* Reject kernel-to-kernel copies through copy_from_user(). */
	ret |= test(!copy_from_user(kmem, (char __user *)(kmem + PAGE_SIZE),
				    PAGE_SIZE),
		    "illegal all-kernel copy_from_user passed");

	/* Destination half of buffer should have been zeroed. */
	ret |= test(memcmp(zerokmem, kmem, PAGE_SIZE),
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
		ret |= test(!put_user(val_##size, (size __user *)kmem),	    \
		    "illegal put_user (" #size ") passed");		    \
	} while (0)

	test_illegal(u8,  0x5a);
	test_illegal(u16, 0x5a5b);
	test_illegal(u32, 0x5a5b5c5d);
	test_illegal(u64, 0x5a5b5c5d6a6b6c6d);
#undef test_illegal

	vm_munmap(user_addr, PAGE_SIZE * 2);
out_zerokmem:
	kfree(zerokmem);
out_kmem:
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
