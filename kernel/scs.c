// SPDX-License-Identifier: GPL-2.0
/*
 * Shadow Call Stack support.
 *
 * Copyright (C) 2019 Google LLC
 */

#include <linux/kasan.h>
#include <linux/scs.h>
#include <linux/slab.h>
#include <asm/scs.h>

static struct kmem_cache *scs_cache;

static void *scs_alloc(int node)
{
	void *s;

	s = kmem_cache_alloc_node(scs_cache, GFP_SCS, node);
	if (s) {
		*__scs_magic(s) = SCS_END_MAGIC;
		/*
		 * Poison the allocation to catch unintentional accesses to
		 * the shadow stack when KASAN is enabled.
		 */
		kasan_poison_object_data(scs_cache, s);
	}

	return s;
}

static void scs_free(void *s)
{
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

	task_scs(tsk) = s;
	task_scs_offset(tsk) = 0;

	return 0;
}

void scs_release(struct task_struct *tsk)
{
	void *s = task_scs(tsk);

	if (!s)
		return;

	WARN(scs_corrupted(tsk), "corrupted shadow stack detected when freeing task\n");
	scs_free(s);
}
