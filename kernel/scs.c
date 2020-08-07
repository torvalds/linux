// SPDX-License-Identifier: GPL-2.0
/*
 * Shadow Call Stack support.
 *
 * Copyright (C) 2019 Google LLC
 */

#include <linux/kasan.h>
#include <linux/mm.h>
#include <linux/scs.h>
#include <linux/slab.h>
#include <linux/vmstat.h>

static struct kmem_cache *scs_cache;

static void __scs_account(void *s, int account)
{
	struct page *scs_page = virt_to_page(s);

	mod_node_page_state(page_pgdat(scs_page), NR_KERNEL_SCS_KB,
			    account * (SCS_SIZE / SZ_1K));
}

static void *scs_alloc(int node)
{
	void *s = kmem_cache_alloc_node(scs_cache, GFP_SCS, node);

	if (!s)
		return NULL;

	*__scs_magic(s) = SCS_END_MAGIC;

	/*
	 * Poison the allocation to catch unintentional accesses to
	 * the shadow stack when KASAN is enabled.
	 */
	kasan_poison_object_data(scs_cache, s);
	__scs_account(s, 1);
	return s;
}

static void scs_free(void *s)
{
	__scs_account(s, -1);
	kasan_unpoison_object_data(scs_cache, s);
	kmem_cache_free(scs_cache, s);
}

void __init scs_init(void)
{
	scs_cache = kmem_cache_create("scs_cache", SCS_SIZE, 0, 0, NULL);
}

int scs_prepare(struct task_struct *tsk, int node)
{
	void *s = scs_alloc(node);

	if (!s)
		return -ENOMEM;

	task_scs(tsk) = task_scs_sp(tsk) = s;
	return 0;
}

static void scs_check_usage(struct task_struct *tsk)
{
	static unsigned long highest;

	unsigned long *p, prev, curr = highest, used = 0;

	if (!IS_ENABLED(CONFIG_DEBUG_STACK_USAGE))
		return;

	for (p = task_scs(tsk); p < __scs_magic(tsk); ++p) {
		if (!READ_ONCE_NOCHECK(*p))
			break;
		used += sizeof(*p);
	}

	while (used > curr) {
		prev = cmpxchg_relaxed(&highest, curr, used);

		if (prev == curr) {
			pr_info("%s (%d): highest shadow stack usage: %lu bytes\n",
				tsk->comm, task_pid_nr(tsk), used);
			break;
		}

		curr = prev;
	}
}

void scs_release(struct task_struct *tsk)
{
	void *s = task_scs(tsk);

	if (!s)
		return;

	WARN(task_scs_end_corrupted(tsk),
	     "corrupted shadow stack detected when freeing task\n");
	scs_check_usage(tsk);
	scs_free(s);
}
