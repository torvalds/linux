// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 * Author: Andrey Ryabinin <a.ryabinin@samsung.com>
 */

#define pr_fmt(fmt) "kasan test: %s " fmt, __func__

#include <linux/mman.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "kasan.h"

static noinline void __init copy_user_test(void)
{
	char *kmem;
	char __user *usermem;
	size_t size = 128 - KASAN_GRANULE_SIZE;
	int __maybe_unused unused;

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

	OPTIMIZER_HIDE_VAR(size);

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

static int __init test_kasan_module_init(void)
{
	/*
	 * Temporarily enable multi-shot mode. Otherwise, KASAN would only
	 * report the first detected bug and panic the kernel if panic_on_warn
	 * is enabled.
	 */
	bool multishot = kasan_save_enable_multi_shot();

	copy_user_test();

	kasan_restore_multi_shot(multishot);
	return -EAGAIN;
}

module_init(test_kasan_module_init);
MODULE_LICENSE("GPL");
