/*
 * mm/kmemleak-test.c
 *
 * Copyright (C) 2008 ARM Limited
 * Written by Catalin Marinas <catalin.marinas@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#define pr_fmt(fmt) "kmemleak: " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/list.h>
#include <linux/percpu.h>
#include <linux/fdtable.h>

#include <linux/kmemleak.h>

struct test_node {
	long header[25];
	struct list_head list;
	long footer[25];
};

static LIST_HEAD(test_list);
static DEFINE_PER_CPU(void *, kmemleak_test_pointer);

/*
 * Some very simple testing. This function needs to be extended for
 * proper testing.
 */
static int __init kmemleak_test_init(void)
{
	struct test_node *elem;
	int i;

	printk(KERN_INFO "Kmemleak testing\n");

	/* make some orphan objects */
	pr_info("kmalloc(32) = %p\n", kmalloc(32, GFP_KERNEL));
	pr_info("kmalloc(32) = %p\n", kmalloc(32, GFP_KERNEL));
	pr_info("kmalloc(1024) = %p\n", kmalloc(1024, GFP_KERNEL));
	pr_info("kmalloc(1024) = %p\n", kmalloc(1024, GFP_KERNEL));
	pr_info("kmalloc(2048) = %p\n", kmalloc(2048, GFP_KERNEL));
	pr_info("kmalloc(2048) = %p\n", kmalloc(2048, GFP_KERNEL));
	pr_info("kmalloc(4096) = %p\n", kmalloc(4096, GFP_KERNEL));
	pr_info("kmalloc(4096) = %p\n", kmalloc(4096, GFP_KERNEL));
#ifndef CONFIG_MODULES
	pr_info("kmem_cache_alloc(files_cachep) = %p\n",
		kmem_cache_alloc(files_cachep, GFP_KERNEL));
	pr_info("kmem_cache_alloc(files_cachep) = %p\n",
		kmem_cache_alloc(files_cachep, GFP_KERNEL));
#endif
	pr_info("vmalloc(64) = %p\n", vmalloc(64));
	pr_info("vmalloc(64) = %p\n", vmalloc(64));
	pr_info("vmalloc(64) = %p\n", vmalloc(64));
	pr_info("vmalloc(64) = %p\n", vmalloc(64));
	pr_info("vmalloc(64) = %p\n", vmalloc(64));

	/*
	 * Add elements to a list. They should only appear as orphan
	 * after the module is removed.
	 */
	for (i = 0; i < 10; i++) {
		elem = kzalloc(sizeof(*elem), GFP_KERNEL);
		pr_info("kzalloc(sizeof(*elem)) = %p\n", elem);
		if (!elem)
			return -ENOMEM;
		INIT_LIST_HEAD(&elem->list);
		list_add_tail(&elem->list, &test_list);
	}

	for_each_possible_cpu(i) {
		per_cpu(kmemleak_test_pointer, i) = kmalloc(129, GFP_KERNEL);
		pr_info("kmalloc(129) = %p\n",
			per_cpu(kmemleak_test_pointer, i));
	}

	return 0;
}
module_init(kmemleak_test_init);

static void __exit kmemleak_test_exit(void)
{
	struct test_node *elem, *tmp;

	/*
	 * Remove the list elements without actually freeing the
	 * memory.
	 */
	list_for_each_entry_safe(elem, tmp, &test_list, list)
		list_del(&elem->list);
}
module_exit(kmemleak_test_exit);

MODULE_LICENSE("GPL");
