// SPDX-License-Identifier: GPL-2.0
/*
 * Shadow Call Stack support.
 *
 * Copyright (C) 2019 Google LLC
 */

#include <linux/cpuhotplug.h>
#include <linux/kasan.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/scs.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/vmstat.h>
#include <asm/scs.h>

static inline void *__scs_base(struct task_struct *tsk)
{
	/*
	 * To minimize risk the of exposure, architectures may clear a
	 * task's thread_info::shadow_call_stack while that task is
	 * running, and only save/restore the active shadow call stack
	 * pointer when the usual register may be clobbered (e.g. across
	 * context switches).
	 *
	 * The shadow call stack is aligned to SCS_SIZE, and grows
	 * upwards, so we can mask out the low bits to extract the base
	 * when the task is not running.
	 */
	return (void *)((unsigned long)task_scs(tsk) & ~(SCS_SIZE - 1));
}

static inline unsigned long *scs_magic(void *s)
{
	return (unsigned long *)(s + SCS_SIZE) - 1;
}

static inline void scs_set_magic(void *s)
{
	*scs_magic(s) = SCS_END_MAGIC;
}

#ifdef CONFIG_SHADOW_CALL_STACK_VMAP

/* Matches NR_CACHED_STACKS for VMAP_STACK */
#define NR_CACHED_SCS 2
static DEFINE_PER_CPU(void *, scs_cache[NR_CACHED_SCS]);

static void *scs_alloc(int node)
{
	int i;
	void *s;

	for (i = 0; i < NR_CACHED_SCS; i++) {
		s = this_cpu_xchg(scs_cache[i], NULL);
		if (s) {
			memset(s, 0, SCS_SIZE);
			goto out;
		}
	}

	/*
	 * We allocate a full page for the shadow stack, which should be
	 * more than we need. Check the assumption nevertheless.
	 */
	BUILD_BUG_ON(SCS_SIZE > PAGE_SIZE);

	s = __vmalloc_node_range(PAGE_SIZE, SCS_SIZE,
				 VMALLOC_START, VMALLOC_END,
				 GFP_SCS, PAGE_KERNEL, 0,
				 node, __builtin_return_address(0));

out:
	if (s)
		scs_set_magic(s);
	/* TODO: poison for KASAN, unpoison in scs_free */

	return s;
}

static void scs_free(void *s)
{
	int i;

	for (i = 0; i < NR_CACHED_SCS; i++)
		if (this_cpu_cmpxchg(scs_cache[i], 0, s) == NULL)
			return;

	vfree_atomic(s);
}

static struct page *__scs_page(struct task_struct *tsk)
{
	return vmalloc_to_page(__scs_base(tsk));
}

static int scs_cleanup(unsigned int cpu)
{
	int i;
	void **cache = per_cpu_ptr(scs_cache, cpu);

	for (i = 0; i < NR_CACHED_SCS; i++) {
		vfree(cache[i]);
		cache[i] = NULL;
	}

	return 0;
}

void __init scs_init(void)
{
	WARN_ON(cpuhp_setup_state(CPUHP_BP_PREPARE_DYN, "scs:scs_cache", NULL,
			scs_cleanup) < 0);
}

#else /* !CONFIG_SHADOW_CALL_STACK_VMAP */

static struct kmem_cache *scs_cache;

static inline void *scs_alloc(int node)
{
	void *s;

	s = kmem_cache_alloc_node(scs_cache, GFP_SCS, node);
	if (s) {
		scs_set_magic(s);
		/*
		 * Poison the allocation to catch unintentional accesses to
		 * the shadow stack when KASAN is enabled.
		 */
		kasan_poison_object_data(scs_cache, s);
	}

	return s;
}

static inline void scs_free(void *s)
{
	kasan_unpoison_object_data(scs_cache, s);
	kmem_cache_free(scs_cache, s);
}

static struct page *__scs_page(struct task_struct *tsk)
{
	return virt_to_page(__scs_base(tsk));
}

void __init scs_init(void)
{
	scs_cache = kmem_cache_create("scs_cache", SCS_SIZE, SCS_SIZE,
				0, NULL);
	WARN_ON(!scs_cache);
}

#endif /* CONFIG_SHADOW_CALL_STACK_VMAP */

void scs_task_reset(struct task_struct *tsk)
{
	/*
	 * Reset the shadow stack to the base address in case the task
	 * is reused.
	 */
	task_set_scs(tsk, __scs_base(tsk));
}

static void scs_account(struct task_struct *tsk, int account)
{
	mod_zone_page_state(page_zone(__scs_page(tsk)), NR_KERNEL_SCS_BYTES,
		account * SCS_SIZE);
}

int scs_prepare(struct task_struct *tsk, int node)
{
	void *s;

	s = scs_alloc(node);
	if (!s)
		return -ENOMEM;

	task_set_scs(tsk, s);
	scs_account(tsk, 1);

	return 0;
}

#ifdef CONFIG_DEBUG_STACK_USAGE
static void scs_check_usage(struct task_struct *tsk)
{
	static unsigned long highest;

	unsigned long *p = __scs_base(tsk);
	unsigned long *end = scs_magic(p);
	unsigned long prev, curr = highest, used = 0;

	for (; p < end; ++p) {
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
#else
static inline void scs_check_usage(struct task_struct *tsk)
{
}
#endif

bool scs_corrupted(struct task_struct *tsk)
{
	unsigned long *magic = scs_magic(__scs_base(tsk));

	return READ_ONCE_NOCHECK(*magic) != SCS_END_MAGIC;
}

void scs_release(struct task_struct *tsk)
{
	void *s;

	s = __scs_base(tsk);
	if (!s)
		return;

	WARN_ON(scs_corrupted(tsk));
	scs_check_usage(tsk);

	scs_account(tsk, -1);
	task_set_scs(tsk, NULL);
	scs_free(s);
}
