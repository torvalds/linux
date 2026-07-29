// SPDX-License-Identifier: GPL-2.0
#include <kunit/test.h>
#include <kunit/test-bug.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/rcupdate.h>
#include <linux/delay.h>
#include <linux/perf_event.h>
#include <linux/kprobes.h>
#include "../mm/slab.h"

static struct kunit_resource resource;
static int slab_errors;

/*
 * Wrapper function for kmem_cache_create(), which reduces 2 parameters:
 * 'align' and 'ctor', and sets SLAB_SKIP_KFENCE flag to avoid getting an
 * object from kfence pool, where the operation could be caught by both
 * our test and kfence sanity check.
 */
static struct kmem_cache *test_kmem_cache_create(const char *name,
				unsigned int size, slab_flags_t flags)
{
	struct kmem_cache *s = kmem_cache_create(name, size, 0,
					(flags | SLAB_NO_USER_FLAGS), NULL);
	s->flags |= SLAB_SKIP_KFENCE;
	return s;
}

static void test_clobber_zone(struct kunit *test)
{
	struct kmem_cache *s = test_kmem_cache_create("TestSlub_RZ_alloc", 64,
							SLAB_RED_ZONE);
	u8 *p = kmem_cache_alloc(s, GFP_KERNEL);

	kasan_disable_current();
	p[64] = 0x12;

	validate_slab_cache(s);
	KUNIT_EXPECT_EQ(test, 2, slab_errors);

	kasan_enable_current();
	kmem_cache_free(s, p);
	kmem_cache_destroy(s);
}

#ifndef CONFIG_KASAN
static void test_next_pointer(struct kunit *test)
{
	struct kmem_cache *s = test_kmem_cache_create("TestSlub_next_ptr_free",
							64, SLAB_POISON);
	u8 *p = kmem_cache_alloc(s, GFP_KERNEL);
	unsigned long tmp;
	unsigned long *ptr_addr;

	kmem_cache_free(s, p);

	ptr_addr = (unsigned long *)(p + s->offset);
	tmp = *ptr_addr;
	p[s->offset] = ~p[s->offset];

	/*
	 * Expecting three errors.
	 * One for the corrupted freechain and the other one for the wrong
	 * count of objects in use. The third error is fixing broken cache.
	 */
	validate_slab_cache(s);
	KUNIT_EXPECT_EQ(test, 3, slab_errors);

	/*
	 * Try to repair corrupted freepointer.
	 * Still expecting two errors. The first for the wrong count
	 * of objects in use.
	 * The second error is for fixing broken cache.
	 */
	*ptr_addr = tmp;
	slab_errors = 0;

	validate_slab_cache(s);
	KUNIT_EXPECT_EQ(test, 2, slab_errors);

	/*
	 * Previous validation repaired the count of objects in use.
	 * Now expecting no error.
	 */
	slab_errors = 0;
	validate_slab_cache(s);
	KUNIT_EXPECT_EQ(test, 0, slab_errors);

	kmem_cache_destroy(s);
}

static void test_first_word(struct kunit *test)
{
	struct kmem_cache *s = test_kmem_cache_create("TestSlub_1th_word_free",
							64, SLAB_POISON);
	u8 *p = kmem_cache_alloc(s, GFP_KERNEL);

	kmem_cache_free(s, p);
	*p = 0x78;

	validate_slab_cache(s);
	KUNIT_EXPECT_EQ(test, 2, slab_errors);

	kmem_cache_destroy(s);
}

static void test_clobber_50th_byte(struct kunit *test)
{
	struct kmem_cache *s = test_kmem_cache_create("TestSlub_50th_word_free",
							64, SLAB_POISON);
	u8 *p = kmem_cache_alloc(s, GFP_KERNEL);

	kmem_cache_free(s, p);
	p[50] = 0x9a;

	validate_slab_cache(s);
	KUNIT_EXPECT_EQ(test, 2, slab_errors);

	kmem_cache_destroy(s);
}
#endif

static void test_clobber_redzone_free(struct kunit *test)
{
	struct kmem_cache *s = test_kmem_cache_create("TestSlub_RZ_free", 64,
							SLAB_RED_ZONE);
	u8 *p = kmem_cache_alloc(s, GFP_KERNEL);

	kasan_disable_current();
	kmem_cache_free(s, p);
	p[64] = 0xab;

	validate_slab_cache(s);
	KUNIT_EXPECT_EQ(test, 2, slab_errors);

	kasan_enable_current();
	kmem_cache_destroy(s);
}

static void test_kmalloc_redzone_access(struct kunit *test)
{
	struct kmem_cache *s = test_kmem_cache_create("TestSlub_RZ_kmalloc", 32,
				SLAB_KMALLOC|SLAB_STORE_USER|SLAB_RED_ZONE);
	u8 *p = alloc_hooks(__kmalloc_cache_noprof(s, GFP_KERNEL, 18));

	kasan_disable_current();

	/* Suppress the -Warray-bounds warning */
	OPTIMIZER_HIDE_VAR(p);
	p[18] = 0xab;
	p[19] = 0xab;

	validate_slab_cache(s);
	KUNIT_EXPECT_EQ(test, 2, slab_errors);

	kasan_enable_current();
	kmem_cache_free(s, p);
	kmem_cache_destroy(s);
}

struct test_kfree_rcu_struct {
	union {
		struct rcu_head rcu;
		struct kvfree_rcu_head kvrcu;
	};
};

static void test_kfree_rcu(struct kunit *test)
{
	struct kmem_cache *s;
	struct test_kfree_rcu_struct *p;

	if (IS_BUILTIN(CONFIG_SLUB_KUNIT_TEST))
		kunit_skip(test, "can't do kfree_rcu() when test is built-in");

	s = test_kmem_cache_create("TestSlub_kfree_rcu",
				   sizeof(struct test_kfree_rcu_struct),
				   SLAB_NO_MERGE);
	p = kmem_cache_alloc(s, GFP_KERNEL);

	kfree_rcu(p, rcu);
	kmem_cache_destroy(s);

	KUNIT_EXPECT_EQ(test, 0, slab_errors);
}

struct cache_destroy_work {
	struct work_struct work;
	struct kmem_cache *s;
};

static void cache_destroy_workfn(struct work_struct *w)
{
	struct cache_destroy_work *cdw;

	cdw = container_of(w, struct cache_destroy_work, work);
	kmem_cache_destroy(cdw->s);
}

#define KMEM_CACHE_DESTROY_NR 10

static void test_kfree_rcu_wq_destroy(struct kunit *test)
{
	struct test_kfree_rcu_struct *p;
	struct cache_destroy_work cdw;
	struct workqueue_struct *wq;
	struct kmem_cache *s;
	unsigned int delay;
	int i;

	if (IS_BUILTIN(CONFIG_SLUB_KUNIT_TEST))
		kunit_skip(test, "can't do kfree_rcu() when test is built-in");

	INIT_WORK_ONSTACK(&cdw.work, cache_destroy_workfn);
	wq = alloc_workqueue("test_kfree_rcu_destroy_wq",
			WQ_HIGHPRI | WQ_UNBOUND | WQ_MEM_RECLAIM, 0);

	if (!wq)
		kunit_skip(test, "failed to alloc wq");

	for (i = 0; i < KMEM_CACHE_DESTROY_NR; i++) {
		s = test_kmem_cache_create("TestSlub_kfree_rcu_wq_destroy",
				sizeof(struct test_kfree_rcu_struct),
				SLAB_NO_MERGE);

		if (!s)
			kunit_skip(test, "failed to create cache");

		delay = get_random_u8();
		p = kmem_cache_alloc(s, GFP_KERNEL);
		kfree_rcu(p, rcu);

		cdw.s = s;

		msleep(delay);
		queue_work(wq, &cdw.work);
		flush_work(&cdw.work);
	}

	destroy_workqueue(wq);
	KUNIT_EXPECT_EQ(test, 0, slab_errors);
}

static void test_leak_destroy(struct kunit *test)
{
	struct kmem_cache *s = test_kmem_cache_create("TestSlub_leak_destroy",
							64, SLAB_NO_MERGE);
	kmem_cache_alloc(s, GFP_KERNEL);

	kmem_cache_destroy(s);

	KUNIT_EXPECT_EQ(test, 2, slab_errors);
}

static void test_krealloc_redzone_zeroing(struct kunit *test)
{
	u8 *p;
	int i;
	struct kmem_cache *s = test_kmem_cache_create("TestSlub_krealloc", 64,
				SLAB_KMALLOC|SLAB_STORE_USER|SLAB_RED_ZONE);

	p = alloc_hooks(__kmalloc_cache_noprof(s, GFP_KERNEL, 48));
	memset(p, 0xff, 48);

	kasan_disable_current();
	OPTIMIZER_HIDE_VAR(p);

	/* Test shrink */
	p = krealloc(p, 40, GFP_KERNEL | __GFP_ZERO);
	for (i = 40; i < 64; i++)
		KUNIT_EXPECT_EQ(test, p[i], SLUB_RED_ACTIVE);

	/* Test grow within the same 64B kmalloc object */
	p = krealloc(p, 56, GFP_KERNEL | __GFP_ZERO);
	for (i = 40; i < 56; i++)
		KUNIT_EXPECT_EQ(test, p[i], 0);
	for (i = 56; i < 64; i++)
		KUNIT_EXPECT_EQ(test, p[i], SLUB_RED_ACTIVE);

	validate_slab_cache(s);
	KUNIT_EXPECT_EQ(test, 0, slab_errors);

	memset(p, 0xff, 56);
	/* Test grow with allocating a bigger 128B object */
	p = krealloc(p, 112, GFP_KERNEL | __GFP_ZERO);
	for (i = 0; i < 56; i++)
		KUNIT_EXPECT_EQ(test, p[i], 0xff);
	for (i = 56; i < 112; i++)
		KUNIT_EXPECT_EQ(test, p[i], 0);

	kfree(p);
	kasan_enable_current();
	kmem_cache_destroy(s);
}

#if defined(CONFIG_PERF_EVENTS) || (defined(CONFIG_KPROBES) && defined(CONFIG_SMP))
#define NR_ITERATIONS 1000
#define NR_OBJECTS 1000
static struct test_kfree_rcu_struct *objects[NR_OBJECTS];

struct test_nolock_context {
	struct kunit *test;
	int callback_count;
	int alloc_ok;
	int alloc_fail;
#ifdef CONFIG_PERF_EVENTS
	struct perf_event *event;
#endif
#if defined(CONFIG_KPROBES) && defined(CONFIG_SMP)
	struct kprobe kprobe;
#endif
};

static void test_kmalloc_and_friends(void)
{
	int i, j;
	bool can_use_kfree_rcu = !IS_BUILTIN(CONFIG_SLUB_KUNIT_TEST);

	for (i = 0; i < NR_ITERATIONS; i++) {
		for (j = 0; j < NR_OBJECTS; j++) {
			gfp_t gfp = (i & 1) ? GFP_KERNEL : GFP_KERNEL_ACCOUNT;

			objects[j] = kmalloc_obj(*objects[j], gfp);
			if (!objects[j]) {
				j--;
				while (j >= 0)
					kfree(objects[j--]);
				return;
			}
		}

		for (j = 0; j < NR_OBJECTS; j++) {
			if (can_use_kfree_rcu && (i & 2))
				kfree_rcu(objects[j], rcu);
			else
				kfree(objects[j]);
		}
	}
}

static void test_nolock(struct test_nolock_context *ctx)
{
	struct test_kfree_rcu_struct *objp;
	gfp_t gfp;
	bool can_use_kfree_rcu = !IS_BUILTIN(CONFIG_SLUB_KUNIT_TEST);

	/* __GFP_ACCOUNT to test kmalloc_nolock() in alloc_slab_obj_exts() */
	gfp = (ctx->callback_count & 1) ? 0 : __GFP_ACCOUNT;
	objp = kmalloc_nolock(sizeof(*objp), gfp, NUMA_NO_NODE);

	if (objp)
		ctx->alloc_ok++;
	else
		ctx->alloc_fail++;

	if (can_use_kfree_rcu && (ctx->callback_count & 2))
		kfree_rcu_nolock(objp, kvrcu);
	else
		kfree_nolock(objp);

	ctx->callback_count++;
}
#endif

#ifdef CONFIG_PERF_EVENTS
static struct perf_event_attr hw_attr = {
	.type = PERF_TYPE_HARDWARE,
	.config = PERF_COUNT_HW_CPU_CYCLES,
	.size = sizeof(struct perf_event_attr),
	.pinned = 1,
	.disabled = 1,
	.freq = 1,
	.sample_freq = 100000,
};

static void overflow_handler_test_nolock(struct perf_event *event,
					 struct perf_sample_data *data,
					 struct pt_regs *regs)
{
	struct test_nolock_context *ctx = event->overflow_handler_context;

	test_nolock(ctx);
}

static bool enable_perf_events(struct test_nolock_context *ctx)
{
	struct perf_event *event;

	event = perf_event_create_kernel_counter(&hw_attr, -1, current,
						 overflow_handler_test_nolock,
						 ctx);

	if (IS_ERR(event))
		return false;

	ctx->event = event;
	perf_event_enable(ctx->event);
	return true;
}

static void disable_perf_events(struct test_nolock_context *ctx)
{
	kunit_info(ctx->test, "HW perf events: callback_count: %d, alloc_ok: %d, alloc_fail: %d\n",
		   ctx->callback_count, ctx->alloc_ok, ctx->alloc_fail);

	perf_event_disable(ctx->event);
	perf_event_release_kernel(ctx->event);
}

static void test_kmalloc_nolock_and_friends_perf(struct kunit *test)
{
	struct test_nolock_context ctx = { .test = test };

	if (!enable_perf_events(&ctx))
		kunit_skip(test, "Failed to enable perf event, skipping");

	test_kmalloc_and_friends();

	disable_perf_events(&ctx);
	KUNIT_EXPECT_EQ(test, 0, slab_errors);
}
#endif

#if defined(CONFIG_KPROBES) && defined(CONFIG_SMP)
static int slab_kprobe_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct test_nolock_context *ctx;

	ctx = container_of(p, struct test_nolock_context, kprobe);
	test_nolock(ctx);
	return 0;
}

static bool register_slab_kprobes(struct test_nolock_context *ctx)
{
	ctx->kprobe.symbol_name = "slab_attach_kprobe_locked";
	ctx->kprobe.pre_handler = slab_kprobe_pre_handler;

	if (register_kprobe(&ctx->kprobe))
		return false;
	return true;
}

static void unregister_slab_kprobes(struct test_nolock_context *ctx)
{
	kunit_info(ctx->test, "kprobes: callback_count: %d, alloc_ok: %d, alloc_fail: %d\n",
		   ctx->callback_count, ctx->alloc_ok, ctx->alloc_fail);
	unregister_kprobe(&ctx->kprobe);
}

static void test_kmalloc_nolock_and_friends_kprobe(struct kunit *test)
{
	struct test_nolock_context ctx = { .test = test };

	if (!register_slab_kprobes(&ctx))
		kunit_skip(test, "Failed to register kprobe, skipping");

	test_kmalloc_and_friends();

	unregister_slab_kprobes(&ctx);
	KUNIT_EXPECT_EQ(test, 0, slab_errors);
}
#endif

static int test_init(struct kunit *test)
{
	slab_errors = 0;

	kunit_add_named_resource(test, NULL, NULL, &resource,
					"slab_errors", &slab_errors);
	return 0;
}

static struct kunit_case test_cases[] = {
	KUNIT_CASE(test_clobber_zone),

#ifndef CONFIG_KASAN
	KUNIT_CASE(test_next_pointer),
	KUNIT_CASE(test_first_word),
	KUNIT_CASE(test_clobber_50th_byte),
#endif

	KUNIT_CASE(test_clobber_redzone_free),
	KUNIT_CASE(test_kmalloc_redzone_access),
	KUNIT_CASE(test_kfree_rcu),
	KUNIT_CASE(test_kfree_rcu_wq_destroy),
	KUNIT_CASE(test_leak_destroy),
	KUNIT_CASE(test_krealloc_redzone_zeroing),
#ifdef CONFIG_PERF_EVENTS
	KUNIT_CASE_SLOW(test_kmalloc_nolock_and_friends_perf),
#endif
#if defined(CONFIG_KPROBES) && defined(CONFIG_SMP)
	KUNIT_CASE_SLOW(test_kmalloc_nolock_and_friends_kprobe),
#endif
	{}
};

static struct kunit_suite test_suite = {
	.name = "slub_test",
	.init = test_init,
	.test_cases = test_cases,
};
kunit_test_suite(test_suite);

MODULE_DESCRIPTION("Kunit tests for slub allocator");
MODULE_LICENSE("GPL");
