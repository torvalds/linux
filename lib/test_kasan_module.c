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

#include "../mm/kasan/kasan.h"

#define OOB_TAG_OFF (IS_ENABLED(CONFIG_KASAN_GENERIC) ? 0 : KASAN_GRANULE_SIZE)

static noinline void __init copy_user_test(void)
{
	char *kmem;
	char __user *usermem;
	size_t size = 10;
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

	pr_info("out-of-bounds in copy_from_user()\n");
	unused = copy_from_user(kmem, usermem, size + 1 + OOB_TAG_OFF);

	pr_info("out-of-bounds in copy_to_user()\n");
	unused = copy_to_user(usermem, kmem, size + 1 + OOB_TAG_OFF);

	pr_info("out-of-bounds in __copy_from_user()\n");
	unused = __copy_from_user(kmem, usermem, size + 1 + OOB_TAG_OFF);

	pr_info("out-of-bounds in __copy_to_user()\n");
	unused = __copy_to_user(usermem, kmem, size + 1 + OOB_TAG_OFF);

	pr_info("out-of-bounds in __copy_from_user_inatomic()\n");
	unused = __copy_from_user_inatomic(kmem, usermem, size + 1 + OOB_TAG_OFF);

	pr_info("out-of-bounds in __copy_to_user_inatomic()\n");
	unused = __copy_to_user_inatomic(usermem, kmem, size + 1 + OOB_TAG_OFF);

	pr_info("out-of-bounds in strncpy_from_user()\n");
	unused = strncpy_from_user(kmem, usermem, size + 1 + OOB_TAG_OFF);

	vm_munmap((unsigned long)usermem, PAGE_SIZE);
	kfree(kmem);
}

static struct kasan_rcu_info {
	int i;
	struct rcu_head rcu;
} *global_rcu_ptr;

static noinline void __init kasan_rcu_reclaim(struct rcu_head *rp)
{
	struct kasan_rcu_info *fp = container_of(rp,
						struct kasan_rcu_info, rcu);

	kfree(fp);
	fp->i = 1;
}

static noinline void __init kasan_rcu_uaf(void)
{
	struct kasan_rcu_info *ptr;

	pr_info("use-after-free in kasan_rcu_reclaim\n");
	ptr = kmalloc(sizeof(struct kasan_rcu_info), GFP_KERNEL);
	if (!ptr) {
		pr_err("Allocation failed\n");
		return;
	}

	global_rcu_ptr = rcu_dereference_protected(ptr, NULL);
	call_rcu(&global_rcu_ptr->rcu, kasan_rcu_reclaim);
}

static noinline void __init kasan_workqueue_work(struct work_struct *work)
{
	kfree(work);
}

static noinline void __init kasan_workqueue_uaf(void)
{
	struct workqueue_struct *workqueue;
	struct work_struct *work;

	workqueue = create_workqueue("kasan_wq_test");
	if (!workqueue) {
		pr_err("Allocation failed\n");
		return;
	}
	work = kmalloc(sizeof(struct work_struct), GFP_KERNEL);
	if (!work) {
		pr_err("Allocation failed\n");
		return;
	}

	INIT_WORK(work, kasan_workqueue_work);
	queue_work(workqueue, work);
	destroy_workqueue(workqueue);

	pr_info("use-after-free on workqueue\n");
	((volatile struct work_struct *)work)->data;
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
	kasan_rcu_uaf();
	kasan_workqueue_uaf();

	kasan_restore_multi_shot(multishot);
	return -EAGAIN;
}

module_init(test_kasan_module_init);
MODULE_LICENSE("GPL");
