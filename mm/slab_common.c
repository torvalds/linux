// SPDX-License-Identifier: GPL-2.0
/*
 * Slab allocator functions that are independent of the allocator strategy
 *
 * (C) 2012 Christoph Lameter <cl@linux.com>
 */
#include <linux/slab.h>

#include <linux/mm.h>
#include <linux/poison.h>
#include <linux/interrupt.h>
#include <linux/memory.h>
#include <linux/cache.h>
#include <linux/compiler.h>
#include <linux/kfence.h>
#include <linux/module.h>
#include <linux/cpu.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>
#include <linux/dma-mapping.h>
#include <linux/swiotlb.h>
#include <linux/proc_fs.h>
#include <linux/debugfs.h>
#include <linux/kmemleak.h>
#include <linux/kasan.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/page.h>
#include <linux/memcontrol.h>
#include <linux/stackdepot.h>

#include "internal.h"
#include "slab.h"

#define CREATE_TRACE_POINTS
#include <trace/events/kmem.h>

enum slab_state slab_state;
LIST_HEAD(slab_caches);
DEFINE_MUTEX(slab_mutex);
struct kmem_cache *kmem_cache;

/*
 * Set of flags that will prevent slab merging
 */
#define SLAB_NEVER_MERGE (SLAB_RED_ZONE | SLAB_POISON | SLAB_STORE_USER | \
		SLAB_TRACE | SLAB_TYPESAFE_BY_RCU | SLAB_NOLEAKTRACE | \
		SLAB_FAILSLAB | SLAB_NO_MERGE)

#define SLAB_MERGE_SAME (SLAB_RECLAIM_ACCOUNT | SLAB_CACHE_DMA | \
			 SLAB_CACHE_DMA32 | SLAB_ACCOUNT)

/*
 * Merge control. If this is set then no merging of slab caches will occur.
 */
static bool slab_nomerge = !IS_ENABLED(CONFIG_SLAB_MERGE_DEFAULT);

static int __init setup_slab_nomerge(char *str)
{
	slab_nomerge = true;
	return 1;
}

static int __init setup_slab_merge(char *str)
{
	slab_nomerge = false;
	return 1;
}

__setup_param("slub_nomerge", slub_nomerge, setup_slab_nomerge, 0);
__setup_param("slub_merge", slub_merge, setup_slab_merge, 0);

__setup("slab_nomerge", setup_slab_nomerge);
__setup("slab_merge", setup_slab_merge);

/*
 * Determine the size of a slab object
 */
unsigned int kmem_cache_size(struct kmem_cache *s)
{
	return s->object_size;
}
EXPORT_SYMBOL(kmem_cache_size);

#ifdef CONFIG_DEBUG_VM

static bool kmem_cache_is_duplicate_name(const char *name)
{
	struct kmem_cache *s;

	list_for_each_entry(s, &slab_caches, list) {
		if (!strcmp(s->name, name))
			return true;
	}

	return false;
}

static int kmem_cache_sanity_check(const char *name, unsigned int size)
{
	if (!name || in_interrupt() || size > KMALLOC_MAX_SIZE) {
		pr_err("kmem_cache_create(%s) integrity check failed\n", name);
		return -EINVAL;
	}

	/* Duplicate names will confuse slabtop, et al */
	WARN(kmem_cache_is_duplicate_name(name),
			"kmem_cache of name '%s' already exists\n", name);

	WARN_ON(strchr(name, ' '));	/* It confuses parsers */
	return 0;
}
#else
static inline int kmem_cache_sanity_check(const char *name, unsigned int size)
{
	return 0;
}
#endif

/*
 * Figure out what the alignment of the objects will be given a set of
 * flags, a user specified alignment and the size of the objects.
 */
static unsigned int calculate_alignment(slab_flags_t flags,
		unsigned int align, unsigned int size)
{
	/*
	 * If the user wants hardware cache aligned objects then follow that
	 * suggestion if the object is sufficiently large.
	 *
	 * The hardware cache alignment cannot override the specified
	 * alignment though. If that is greater then use it.
	 */
	if (flags & SLAB_HWCACHE_ALIGN) {
		unsigned int ralign;

		ralign = cache_line_size();
		while (size <= ralign / 2)
			ralign /= 2;
		align = max(align, ralign);
	}

	align = max(align, arch_slab_minalign());

	return ALIGN(align, sizeof(void *));
}

/*
 * Find a mergeable slab cache
 */
int slab_unmergeable(struct kmem_cache *s)
{
	if (slab_nomerge || (s->flags & SLAB_NEVER_MERGE))
		return 1;

	if (s->ctor)
		return 1;

#ifdef CONFIG_HARDENED_USERCOPY
	if (s->usersize)
		return 1;
#endif

	/*
	 * We may have set a slab to be unmergeable during bootstrap.
	 */
	if (s->refcount < 0)
		return 1;

	return 0;
}

struct kmem_cache *find_mergeable(unsigned int size, unsigned int align,
		slab_flags_t flags, const char *name, void (*ctor)(void *))
{
	struct kmem_cache *s;

	if (slab_nomerge)
		return NULL;

	if (ctor)
		return NULL;

	flags = kmem_cache_flags(flags, name);

	if (flags & SLAB_NEVER_MERGE)
		return NULL;

	size = ALIGN(size, sizeof(void *));
	align = calculate_alignment(flags, align, size);
	size = ALIGN(size, align);

	list_for_each_entry_reverse(s, &slab_caches, list) {
		if (slab_unmergeable(s))
			continue;

		if (size > s->size)
			continue;

		if ((flags & SLAB_MERGE_SAME) != (s->flags & SLAB_MERGE_SAME))
			continue;
		/*
		 * Check if alignment is compatible.
		 * Courtesy of Adrian Drzewiecki
		 */
		if ((s->size & ~(align - 1)) != s->size)
			continue;

		if (s->size - size >= sizeof(void *))
			continue;

		return s;
	}
	return NULL;
}

static struct kmem_cache *create_cache(const char *name,
				       unsigned int object_size,
				       struct kmem_cache_args *args,
				       slab_flags_t flags)
{
	struct kmem_cache *s;
	int err;

	if (WARN_ON(args->useroffset + args->usersize > object_size))
		args->useroffset = args->usersize = 0;

	/* If a custom freelist pointer is requested make sure it's sane. */
	err = -EINVAL;
	if (args->use_freeptr_offset &&
	    (args->freeptr_offset >= object_size ||
	     !(flags & SLAB_TYPESAFE_BY_RCU) ||
	     !IS_ALIGNED(args->freeptr_offset, sizeof(freeptr_t))))
		goto out;

	err = -ENOMEM;
	s = kmem_cache_zalloc(kmem_cache, GFP_KERNEL);
	if (!s)
		goto out;
	err = do_kmem_cache_create(s, name, object_size, args, flags);
	if (err)
		goto out_free_cache;

	s->refcount = 1;
	list_add(&s->list, &slab_caches);
	return s;

out_free_cache:
	kmem_cache_free(kmem_cache, s);
out:
	return ERR_PTR(err);
}

/**
 * __kmem_cache_create_args - Create a kmem cache.
 * @name: A string which is used in /proc/slabinfo to identify this cache.
 * @object_size: The size of objects to be created in this cache.
 * @args: Additional arguments for the cache creation (see
 *        &struct kmem_cache_args).
 * @flags: See %SLAB_* flags for an explanation of individual @flags.
 *
 * Not to be called directly, use the kmem_cache_create() wrapper with the same
 * parameters.
 *
 * Context: Cannot be called within a interrupt, but can be interrupted.
 *
 * Return: a pointer to the cache on success, NULL on failure.
 */
struct kmem_cache *__kmem_cache_create_args(const char *name,
					    unsigned int object_size,
					    struct kmem_cache_args *args,
					    slab_flags_t flags)
{
	struct kmem_cache *s = NULL;
	const char *cache_name;
	int err;

#ifdef CONFIG_SLUB_DEBUG
	/*
	 * If no slab_debug was enabled globally, the static key is not yet
	 * enabled by setup_slub_debug(). Enable it if the cache is being
	 * created with any of the debugging flags passed explicitly.
	 * It's also possible that this is the first cache created with
	 * SLAB_STORE_USER and we should init stack_depot for it.
	 */
	if (flags & SLAB_DEBUG_FLAGS)
		static_branch_enable(&slub_debug_enabled);
	if (flags & SLAB_STORE_USER)
		stack_depot_init();
#endif

	mutex_lock(&slab_mutex);

	err = kmem_cache_sanity_check(name, object_size);
	if (err) {
		goto out_unlock;
	}

	/* Refuse requests with allocator specific flags */
	if (flags & ~SLAB_FLAGS_PERMITTED) {
		err = -EINVAL;
		goto out_unlock;
	}

	/*
	 * Some allocators will constraint the set of valid flags to a subset
	 * of all flags. We expect them to define CACHE_CREATE_MASK in this
	 * case, and we'll just provide them with a sanitized version of the
	 * passed flags.
	 */
	flags &= CACHE_CREATE_MASK;

	/* Fail closed on bad usersize of useroffset values. */
	if (!IS_ENABLED(CONFIG_HARDENED_USERCOPY) ||
	    WARN_ON(!args->usersize && args->useroffset) ||
	    WARN_ON(object_size < args->usersize ||
		    object_size - args->usersize < args->useroffset))
		args->usersize = args->useroffset = 0;

	if (!args->usersize)
		s = __kmem_cache_alias(name, object_size, args->align, flags,
				       args->ctor);
	if (s)
		goto out_unlock;

	cache_name = kstrdup_const(name, GFP_KERNEL);
	if (!cache_name) {
		err = -ENOMEM;
		goto out_unlock;
	}

	args->align = calculate_alignment(flags, args->align, object_size);
	s = create_cache(cache_name, object_size, args, flags);
	if (IS_ERR(s)) {
		err = PTR_ERR(s);
		kfree_const(cache_name);
	}

out_unlock:
	mutex_unlock(&slab_mutex);

	if (err) {
		if (flags & SLAB_PANIC)
			panic("%s: Failed to create slab '%s'. Error %d\n",
				__func__, name, err);
		else {
			pr_warn("%s(%s) failed with error %d\n",
				__func__, name, err);
			dump_stack();
		}
		return NULL;
	}
	return s;
}
EXPORT_SYMBOL(__kmem_cache_create_args);

static struct kmem_cache *kmem_buckets_cache __ro_after_init;

/**
 * kmem_buckets_create - Create a set of caches that handle dynamic sized
 *			 allocations via kmem_buckets_alloc()
 * @name: A prefix string which is used in /proc/slabinfo to identify this
 *	  cache. The individual caches with have their sizes as the suffix.
 * @flags: SLAB flags (see kmem_cache_create() for details).
 * @useroffset: Starting offset within an allocation that may be copied
 *		to/from userspace.
 * @usersize: How many bytes, starting at @useroffset, may be copied
 *		to/from userspace.
 * @ctor: A constructor for the objects, run when new allocations are made.
 *
 * Cannot be called within an interrupt, but can be interrupted.
 *
 * Return: a pointer to the cache on success, NULL on failure. When
 * CONFIG_SLAB_BUCKETS is not enabled, ZERO_SIZE_PTR is returned, and
 * subsequent calls to kmem_buckets_alloc() will fall back to kmalloc().
 * (i.e. callers only need to check for NULL on failure.)
 */
kmem_buckets *kmem_buckets_create(const char *name, slab_flags_t flags,
				  unsigned int useroffset,
				  unsigned int usersize,
				  void (*ctor)(void *))
{
	unsigned long mask = 0;
	unsigned int idx;
	kmem_buckets *b;

	BUILD_BUG_ON(ARRAY_SIZE(kmalloc_caches[KMALLOC_NORMAL]) > BITS_PER_LONG);

	/*
	 * When the separate buckets API is not built in, just return
	 * a non-NULL value for the kmem_buckets pointer, which will be
	 * unused when performing allocations.
	 */
	if (!IS_ENABLED(CONFIG_SLAB_BUCKETS))
		return ZERO_SIZE_PTR;

	if (WARN_ON(!kmem_buckets_cache))
		return NULL;

	b = kmem_cache_alloc(kmem_buckets_cache, GFP_KERNEL|__GFP_ZERO);
	if (WARN_ON(!b))
		return NULL;

	flags |= SLAB_NO_MERGE;

	for (idx = 0; idx < ARRAY_SIZE(kmalloc_caches[KMALLOC_NORMAL]); idx++) {
		char *short_size, *cache_name;
		unsigned int cache_useroffset, cache_usersize;
		unsigned int size, aligned_idx;

		if (!kmalloc_caches[KMALLOC_NORMAL][idx])
			continue;

		size = kmalloc_caches[KMALLOC_NORMAL][idx]->object_size;
		if (!size)
			continue;

		short_size = strchr(kmalloc_caches[KMALLOC_NORMAL][idx]->name, '-');
		if (WARN_ON(!short_size))
			goto fail;

		if (useroffset >= size) {
			cache_useroffset = 0;
			cache_usersize = 0;
		} else {
			cache_useroffset = useroffset;
			cache_usersize = min(size - cache_useroffset, usersize);
		}

		aligned_idx = __kmalloc_index(size, false);
		if (!(*b)[aligned_idx]) {
			cache_name = kasprintf(GFP_KERNEL, "%s-%s", name, short_size + 1);
			if (WARN_ON(!cache_name))
				goto fail;
			(*b)[aligned_idx] = kmem_cache_create_usercopy(cache_name, size,
					0, flags, cache_useroffset,
					cache_usersize, ctor);
			kfree(cache_name);
			if (WARN_ON(!(*b)[aligned_idx]))
				goto fail;
			set_bit(aligned_idx, &mask);
		}
		if (idx != aligned_idx)
			(*b)[idx] = (*b)[aligned_idx];
	}

	return b;

fail:
	for_each_set_bit(idx, &mask, ARRAY_SIZE(kmalloc_caches[KMALLOC_NORMAL]))
		kmem_cache_destroy((*b)[idx]);
	kmem_cache_free(kmem_buckets_cache, b);

	return NULL;
}
EXPORT_SYMBOL(kmem_buckets_create);

/*
 * For a given kmem_cache, kmem_cache_destroy() should only be called
 * once or there will be a use-after-free problem. The actual deletion
 * and release of the kobject does not need slab_mutex or cpu_hotplug_lock
 * protection. So they are now done without holding those locks.
 */
static void kmem_cache_release(struct kmem_cache *s)
{
	kfence_shutdown_cache(s);
	if (__is_defined(SLAB_SUPPORTS_SYSFS) && slab_state >= FULL)
		sysfs_slab_release(s);
	else
		slab_kmem_cache_release(s);
}

void slab_kmem_cache_release(struct kmem_cache *s)
{
	__kmem_cache_release(s);
	kfree_const(s->name);
	kmem_cache_free(kmem_cache, s);
}

void kmem_cache_destroy(struct kmem_cache *s)
{
	int err;

	if (unlikely(!s) || !kasan_check_byte(s))
		return;

	/* in-flight kfree_rcu()'s may include objects from our cache */
	kvfree_rcu_barrier();

	if (IS_ENABLED(CONFIG_SLUB_RCU_DEBUG) &&
	    (s->flags & SLAB_TYPESAFE_BY_RCU)) {
		/*
		 * Under CONFIG_SLUB_RCU_DEBUG, when objects in a
		 * SLAB_TYPESAFE_BY_RCU slab are freed, SLUB will internally
		 * defer their freeing with call_rcu().
		 * Wait for such call_rcu() invocations here before actually
		 * destroying the cache.
		 *
		 * It doesn't matter that we haven't looked at the slab refcount
		 * yet - slabs with SLAB_TYPESAFE_BY_RCU can't be merged, so
		 * the refcount should be 1 here.
		 */
		rcu_barrier();
	}

	cpus_read_lock();
	mutex_lock(&slab_mutex);

	s->refcount--;
	if (s->refcount) {
		mutex_unlock(&slab_mutex);
		cpus_read_unlock();
		return;
	}

	/* free asan quarantined objects */
	kasan_cache_shutdown(s);

	err = __kmem_cache_shutdown(s);
	if (!slab_in_kunit_test())
		WARN(err, "%s %s: Slab cache still has objects when called from %pS",
		     __func__, s->name, (void *)_RET_IP_);

	list_del(&s->list);

	mutex_unlock(&slab_mutex);
	cpus_read_unlock();

	if (slab_state >= FULL)
		sysfs_slab_unlink(s);
	debugfs_slab_release(s);

	if (err)
		return;

	if (s->flags & SLAB_TYPESAFE_BY_RCU)
		rcu_barrier();

	kmem_cache_release(s);
}
EXPORT_SYMBOL(kmem_cache_destroy);

/**
 * kmem_cache_shrink - Shrink a cache.
 * @cachep: The cache to shrink.
 *
 * Releases as many slabs as possible for a cache.
 * To help debugging, a zero exit status indicates all slabs were released.
 *
 * Return: %0 if all slabs were released, non-zero otherwise
 */
int kmem_cache_shrink(struct kmem_cache *cachep)
{
	kasan_cache_shrink(cachep);

	return __kmem_cache_shrink(cachep);
}
EXPORT_SYMBOL(kmem_cache_shrink);

bool slab_is_available(void)
{
	return slab_state >= UP;
}

#ifdef CONFIG_PRINTK
static void kmem_obj_info(struct kmem_obj_info *kpp, void *object, struct slab *slab)
{
	if (__kfence_obj_info(kpp, object, slab))
		return;
	__kmem_obj_info(kpp, object, slab);
}

/**
 * kmem_dump_obj - Print available slab provenance information
 * @object: slab object for which to find provenance information.
 *
 * This function uses pr_cont(), so that the caller is expected to have
 * printed out whatever preamble is appropriate.  The provenance information
 * depends on the type of object and on how much debugging is enabled.
 * For a slab-cache object, the fact that it is a slab object is printed,
 * and, if available, the slab name, return address, and stack trace from
 * the allocation and last free path of that object.
 *
 * Return: %true if the pointer is to a not-yet-freed object from
 * kmalloc() or kmem_cache_alloc(), either %true or %false if the pointer
 * is to an already-freed object, and %false otherwise.
 */
bool kmem_dump_obj(void *object)
{
	char *cp = IS_ENABLED(CONFIG_MMU) ? "" : "/vmalloc";
	int i;
	struct slab *slab;
	unsigned long ptroffset;
	struct kmem_obj_info kp = { };

	/* Some arches consider ZERO_SIZE_PTR to be a valid address. */
	if (object < (void *)PAGE_SIZE || !virt_addr_valid(object))
		return false;
	slab = virt_to_slab(object);
	if (!slab)
		return false;

	kmem_obj_info(&kp, object, slab);
	if (kp.kp_slab_cache)
		pr_cont(" slab%s %s", cp, kp.kp_slab_cache->name);
	else
		pr_cont(" slab%s", cp);
	if (is_kfence_address(object))
		pr_cont(" (kfence)");
	if (kp.kp_objp)
		pr_cont(" start %px", kp.kp_objp);
	if (kp.kp_data_offset)
		pr_cont(" data offset %lu", kp.kp_data_offset);
	if (kp.kp_objp) {
		ptroffset = ((char *)object - (char *)kp.kp_objp) - kp.kp_data_offset;
		pr_cont(" pointer offset %lu", ptroffset);
	}
	if (kp.kp_slab_cache && kp.kp_slab_cache->object_size)
		pr_cont(" size %u", kp.kp_slab_cache->object_size);
	if (kp.kp_ret)
		pr_cont(" allocated at %pS\n", kp.kp_ret);
	else
		pr_cont("\n");
	for (i = 0; i < ARRAY_SIZE(kp.kp_stack); i++) {
		if (!kp.kp_stack[i])
			break;
		pr_info("    %pS\n", kp.kp_stack[i]);
	}

	if (kp.kp_free_stack[0])
		pr_cont(" Free path:\n");

	for (i = 0; i < ARRAY_SIZE(kp.kp_free_stack); i++) {
		if (!kp.kp_free_stack[i])
			break;
		pr_info("    %pS\n", kp.kp_free_stack[i]);
	}

	return true;
}
EXPORT_SYMBOL_GPL(kmem_dump_obj);
#endif

/* Create a cache during boot when no slab services are available yet */
void __init create_boot_cache(struct kmem_cache *s, const char *name,
		unsigned int size, slab_flags_t flags,
		unsigned int useroffset, unsigned int usersize)
{
	int err;
	unsigned int align = ARCH_KMALLOC_MINALIGN;
	struct kmem_cache_args kmem_args = {};

	/*
	 * kmalloc caches guarantee alignment of at least the largest
	 * power-of-two divisor of the size. For power-of-two sizes,
	 * it is the size itself.
	 */
	if (flags & SLAB_KMALLOC)
		align = max(align, 1U << (ffs(size) - 1));
	kmem_args.align = calculate_alignment(flags, align, size);

#ifdef CONFIG_HARDENED_USERCOPY
	kmem_args.useroffset = useroffset;
	kmem_args.usersize = usersize;
#endif

	err = do_kmem_cache_create(s, name, size, &kmem_args, flags);

	if (err)
		panic("Creation of kmalloc slab %s size=%u failed. Reason %d\n",
					name, size, err);

	s->refcount = -1;	/* Exempt from merging for now */
}

static struct kmem_cache *__init create_kmalloc_cache(const char *name,
						      unsigned int size,
						      slab_flags_t flags)
{
	struct kmem_cache *s = kmem_cache_zalloc(kmem_cache, GFP_NOWAIT);

	if (!s)
		panic("Out of memory when creating slab %s\n", name);

	create_boot_cache(s, name, size, flags | SLAB_KMALLOC, 0, size);
	list_add(&s->list, &slab_caches);
	s->refcount = 1;
	return s;
}

kmem_buckets kmalloc_caches[NR_KMALLOC_TYPES] __ro_after_init =
{ /* initialization for https://llvm.org/pr42570 */ };
EXPORT_SYMBOL(kmalloc_caches);

#ifdef CONFIG_RANDOM_KMALLOC_CACHES
unsigned long random_kmalloc_seed __ro_after_init;
EXPORT_SYMBOL(random_kmalloc_seed);
#endif

/*
 * Conversion table for small slabs sizes / 8 to the index in the
 * kmalloc array. This is necessary for slabs < 192 since we have non power
 * of two cache sizes there. The size of larger slabs can be determined using
 * fls.
 */
u8 kmalloc_size_index[24] __ro_after_init = {
	3,	/* 8 */
	4,	/* 16 */
	5,	/* 24 */
	5,	/* 32 */
	6,	/* 40 */
	6,	/* 48 */
	6,	/* 56 */
	6,	/* 64 */
	1,	/* 72 */
	1,	/* 80 */
	1,	/* 88 */
	1,	/* 96 */
	7,	/* 104 */
	7,	/* 112 */
	7,	/* 120 */
	7,	/* 128 */
	2,	/* 136 */
	2,	/* 144 */
	2,	/* 152 */
	2,	/* 160 */
	2,	/* 168 */
	2,	/* 176 */
	2,	/* 184 */
	2	/* 192 */
};

size_t kmalloc_size_roundup(size_t size)
{
	if (size && size <= KMALLOC_MAX_CACHE_SIZE) {
		/*
		 * The flags don't matter since size_index is common to all.
		 * Neither does the caller for just getting ->object_size.
		 */
		return kmalloc_slab(size, NULL, GFP_KERNEL, 0)->object_size;
	}

	/* Above the smaller buckets, size is a multiple of page size. */
	if (size && size <= KMALLOC_MAX_SIZE)
		return PAGE_SIZE << get_order(size);

	/*
	 * Return 'size' for 0 - kmalloc() returns ZERO_SIZE_PTR
	 * and very large size - kmalloc() may fail.
	 */
	return size;

}
EXPORT_SYMBOL(kmalloc_size_roundup);

#ifdef CONFIG_ZONE_DMA
#define KMALLOC_DMA_NAME(sz)	.name[KMALLOC_DMA] = "dma-kmalloc-" #sz,
#else
#define KMALLOC_DMA_NAME(sz)
#endif

#ifdef CONFIG_MEMCG
#define KMALLOC_CGROUP_NAME(sz)	.name[KMALLOC_CGROUP] = "kmalloc-cg-" #sz,
#else
#define KMALLOC_CGROUP_NAME(sz)
#endif

#ifndef CONFIG_SLUB_TINY
#define KMALLOC_RCL_NAME(sz)	.name[KMALLOC_RECLAIM] = "kmalloc-rcl-" #sz,
#else
#define KMALLOC_RCL_NAME(sz)
#endif

#ifdef CONFIG_RANDOM_KMALLOC_CACHES
#define __KMALLOC_RANDOM_CONCAT(a, b) a ## b
#define KMALLOC_RANDOM_NAME(N, sz) __KMALLOC_RANDOM_CONCAT(KMA_RAND_, N)(sz)
#define KMA_RAND_1(sz)                  .name[KMALLOC_RANDOM_START +  1] = "kmalloc-rnd-01-" #sz,
#define KMA_RAND_2(sz)  KMA_RAND_1(sz)  .name[KMALLOC_RANDOM_START +  2] = "kmalloc-rnd-02-" #sz,
#define KMA_RAND_3(sz)  KMA_RAND_2(sz)  .name[KMALLOC_RANDOM_START +  3] = "kmalloc-rnd-03-" #sz,
#define KMA_RAND_4(sz)  KMA_RAND_3(sz)  .name[KMALLOC_RANDOM_START +  4] = "kmalloc-rnd-04-" #sz,
#define KMA_RAND_5(sz)  KMA_RAND_4(sz)  .name[KMALLOC_RANDOM_START +  5] = "kmalloc-rnd-05-" #sz,
#define KMA_RAND_6(sz)  KMA_RAND_5(sz)  .name[KMALLOC_RANDOM_START +  6] = "kmalloc-rnd-06-" #sz,
#define KMA_RAND_7(sz)  KMA_RAND_6(sz)  .name[KMALLOC_RANDOM_START +  7] = "kmalloc-rnd-07-" #sz,
#define KMA_RAND_8(sz)  KMA_RAND_7(sz)  .name[KMALLOC_RANDOM_START +  8] = "kmalloc-rnd-08-" #sz,
#define KMA_RAND_9(sz)  KMA_RAND_8(sz)  .name[KMALLOC_RANDOM_START +  9] = "kmalloc-rnd-09-" #sz,
#define KMA_RAND_10(sz) KMA_RAND_9(sz)  .name[KMALLOC_RANDOM_START + 10] = "kmalloc-rnd-10-" #sz,
#define KMA_RAND_11(sz) KMA_RAND_10(sz) .name[KMALLOC_RANDOM_START + 11] = "kmalloc-rnd-11-" #sz,
#define KMA_RAND_12(sz) KMA_RAND_11(sz) .name[KMALLOC_RANDOM_START + 12] = "kmalloc-rnd-12-" #sz,
#define KMA_RAND_13(sz) KMA_RAND_12(sz) .name[KMALLOC_RANDOM_START + 13] = "kmalloc-rnd-13-" #sz,
#define KMA_RAND_14(sz) KMA_RAND_13(sz) .name[KMALLOC_RANDOM_START + 14] = "kmalloc-rnd-14-" #sz,
#define KMA_RAND_15(sz) KMA_RAND_14(sz) .name[KMALLOC_RANDOM_START + 15] = "kmalloc-rnd-15-" #sz,
#else // CONFIG_RANDOM_KMALLOC_CACHES
#define KMALLOC_RANDOM_NAME(N, sz)
#endif

#define INIT_KMALLOC_INFO(__size, __short_size)			\
{								\
	.name[KMALLOC_NORMAL]  = "kmalloc-" #__short_size,	\
	KMALLOC_RCL_NAME(__short_size)				\
	KMALLOC_CGROUP_NAME(__short_size)			\
	KMALLOC_DMA_NAME(__short_size)				\
	KMALLOC_RANDOM_NAME(RANDOM_KMALLOC_CACHES_NR, __short_size)	\
	.size = __size,						\
}

/*
 * kmalloc_info[] is to make slab_debug=,kmalloc-xx option work at boot time.
 * kmalloc_index() supports up to 2^21=2MB, so the final entry of the table is
 * kmalloc-2M.
 */
const struct kmalloc_info_struct kmalloc_info[] __initconst = {
	INIT_KMALLOC_INFO(0, 0),
	INIT_KMALLOC_INFO(96, 96),
	INIT_KMALLOC_INFO(192, 192),
	INIT_KMALLOC_INFO(8, 8),
	INIT_KMALLOC_INFO(16, 16),
	INIT_KMALLOC_INFO(32, 32),
	INIT_KMALLOC_INFO(64, 64),
	INIT_KMALLOC_INFO(128, 128),
	INIT_KMALLOC_INFO(256, 256),
	INIT_KMALLOC_INFO(512, 512),
	INIT_KMALLOC_INFO(1024, 1k),
	INIT_KMALLOC_INFO(2048, 2k),
	INIT_KMALLOC_INFO(4096, 4k),
	INIT_KMALLOC_INFO(8192, 8k),
	INIT_KMALLOC_INFO(16384, 16k),
	INIT_KMALLOC_INFO(32768, 32k),
	INIT_KMALLOC_INFO(65536, 64k),
	INIT_KMALLOC_INFO(131072, 128k),
	INIT_KMALLOC_INFO(262144, 256k),
	INIT_KMALLOC_INFO(524288, 512k),
	INIT_KMALLOC_INFO(1048576, 1M),
	INIT_KMALLOC_INFO(2097152, 2M)
};

/*
 * Patch up the size_index table if we have strange large alignment
 * requirements for the kmalloc array. This is only the case for
 * MIPS it seems. The standard arches will not generate any code here.
 *
 * Largest permitted alignment is 256 bytes due to the way we
 * handle the index determination for the smaller caches.
 *
 * Make sure that nothing crazy happens if someone starts tinkering
 * around with ARCH_KMALLOC_MINALIGN
 */
void __init setup_kmalloc_cache_index_table(void)
{
	unsigned int i;

	BUILD_BUG_ON(KMALLOC_MIN_SIZE > 256 ||
		!is_power_of_2(KMALLOC_MIN_SIZE));

	for (i = 8; i < KMALLOC_MIN_SIZE; i += 8) {
		unsigned int elem = size_index_elem(i);

		if (elem >= ARRAY_SIZE(kmalloc_size_index))
			break;
		kmalloc_size_index[elem] = KMALLOC_SHIFT_LOW;
	}

	if (KMALLOC_MIN_SIZE >= 64) {
		/*
		 * The 96 byte sized cache is not used if the alignment
		 * is 64 byte.
		 */
		for (i = 64 + 8; i <= 96; i += 8)
			kmalloc_size_index[size_index_elem(i)] = 7;

	}

	if (KMALLOC_MIN_SIZE >= 128) {
		/*
		 * The 192 byte sized cache is not used if the alignment
		 * is 128 byte. Redirect kmalloc to use the 256 byte cache
		 * instead.
		 */
		for (i = 128 + 8; i <= 192; i += 8)
			kmalloc_size_index[size_index_elem(i)] = 8;
	}
}

static unsigned int __kmalloc_minalign(void)
{
	unsigned int minalign = dma_get_cache_alignment();

	if (IS_ENABLED(CONFIG_DMA_BOUNCE_UNALIGNED_KMALLOC) &&
	    is_swiotlb_allocated())
		minalign = ARCH_KMALLOC_MINALIGN;

	return max(minalign, arch_slab_minalign());
}

static void __init
new_kmalloc_cache(int idx, enum kmalloc_cache_type type)
{
	slab_flags_t flags = 0;
	unsigned int minalign = __kmalloc_minalign();
	unsigned int aligned_size = kmalloc_info[idx].size;
	int aligned_idx = idx;

	if ((KMALLOC_RECLAIM != KMALLOC_NORMAL) && (type == KMALLOC_RECLAIM)) {
		flags |= SLAB_RECLAIM_ACCOUNT;
	} else if (IS_ENABLED(CONFIG_MEMCG) && (type == KMALLOC_CGROUP)) {
		if (mem_cgroup_kmem_disabled()) {
			kmalloc_caches[type][idx] = kmalloc_caches[KMALLOC_NORMAL][idx];
			return;
		}
		flags |= SLAB_ACCOUNT;
	} else if (IS_ENABLED(CONFIG_ZONE_DMA) && (type == KMALLOC_DMA)) {
		flags |= SLAB_CACHE_DMA;
	}

#ifdef CONFIG_RANDOM_KMALLOC_CACHES
	if (type >= KMALLOC_RANDOM_START && type <= KMALLOC_RANDOM_END)
		flags |= SLAB_NO_MERGE;
#endif

	/*
	 * If CONFIG_MEMCG is enabled, disable cache merging for
	 * KMALLOC_NORMAL caches.
	 */
	if (IS_ENABLED(CONFIG_MEMCG) && (type == KMALLOC_NORMAL))
		flags |= SLAB_NO_MERGE;

	if (minalign > ARCH_KMALLOC_MINALIGN) {
		aligned_size = ALIGN(aligned_size, minalign);
		aligned_idx = __kmalloc_index(aligned_size, false);
	}

	if (!kmalloc_caches[type][aligned_idx])
		kmalloc_caches[type][aligned_idx] = create_kmalloc_cache(
					kmalloc_info[aligned_idx].name[type],
					aligned_size, flags);
	if (idx != aligned_idx)
		kmalloc_caches[type][idx] = kmalloc_caches[type][aligned_idx];
}

/*
 * Create the kmalloc array. Some of the regular kmalloc arrays
 * may already have been created because they were needed to
 * enable allocations for slab creation.
 */
void __init create_kmalloc_caches(void)
{
	int i;
	enum kmalloc_cache_type type;

	/*
	 * Including KMALLOC_CGROUP if CONFIG_MEMCG defined
	 */
	for (type = KMALLOC_NORMAL; type < NR_KMALLOC_TYPES; type++) {
		/* Caches that are NOT of the two-to-the-power-of size. */
		if (KMALLOC_MIN_SIZE <= 32)
			new_kmalloc_cache(1, type);
		if (KMALLOC_MIN_SIZE <= 64)
			new_kmalloc_cache(2, type);

		/* Caches that are of the two-to-the-power-of size. */
		for (i = KMALLOC_SHIFT_LOW; i <= KMALLOC_SHIFT_HIGH; i++)
			new_kmalloc_cache(i, type);
	}
#ifdef CONFIG_RANDOM_KMALLOC_CACHES
	random_kmalloc_seed = get_random_u64();
#endif

	/* Kmalloc array is now usable */
	slab_state = UP;

	if (IS_ENABLED(CONFIG_SLAB_BUCKETS))
		kmem_buckets_cache = kmem_cache_create("kmalloc_buckets",
						       sizeof(kmem_buckets),
						       0, SLAB_NO_MERGE, NULL);
}

/**
 * __ksize -- Report full size of underlying allocation
 * @object: pointer to the object
 *
 * This should only be used internally to query the true size of allocations.
 * It is not meant to be a way to discover the usable size of an allocation
 * after the fact. Instead, use kmalloc_size_roundup(). Using memory beyond
 * the originally requested allocation size may trigger KASAN, UBSAN_BOUNDS,
 * and/or FORTIFY_SOURCE.
 *
 * Return: size of the actual memory used by @object in bytes
 */
size_t __ksize(const void *object)
{
	struct folio *folio;

	if (unlikely(object == ZERO_SIZE_PTR))
		return 0;

	folio = virt_to_folio(object);

	if (unlikely(!folio_test_slab(folio))) {
		if (WARN_ON(folio_size(folio) <= KMALLOC_MAX_CACHE_SIZE))
			return 0;
		if (WARN_ON(object != folio_address(folio)))
			return 0;
		return folio_size(folio);
	}

#ifdef CONFIG_SLUB_DEBUG
	skip_orig_size_check(folio_slab(folio)->slab_cache, object);
#endif

	return slab_ksize(folio_slab(folio)->slab_cache);
}

gfp_t kmalloc_fix_flags(gfp_t flags)
{
	gfp_t invalid_mask = flags & GFP_SLAB_BUG_MASK;

	flags &= ~GFP_SLAB_BUG_MASK;
	pr_warn("Unexpected gfp: %#x (%pGg). Fixing up to gfp: %#x (%pGg). Fix your code!\n",
			invalid_mask, &invalid_mask, flags, &flags);
	dump_stack();

	return flags;
}

#ifdef CONFIG_SLAB_FREELIST_RANDOM
/* Randomize a generic freelist */
static void freelist_randomize(unsigned int *list,
			       unsigned int count)
{
	unsigned int rand;
	unsigned int i;

	for (i = 0; i < count; i++)
		list[i] = i;

	/* Fisher-Yates shuffle */
	for (i = count - 1; i > 0; i--) {
		rand = get_random_u32_below(i + 1);
		swap(list[i], list[rand]);
	}
}

/* Create a random sequence per cache */
int cache_random_seq_create(struct kmem_cache *cachep, unsigned int count,
				    gfp_t gfp)
{

	if (count < 2 || cachep->random_seq)
		return 0;

	cachep->random_seq = kcalloc(count, sizeof(unsigned int), gfp);
	if (!cachep->random_seq)
		return -ENOMEM;

	freelist_randomize(cachep->random_seq, count);
	return 0;
}

/* Destroy the per-cache random freelist sequence */
void cache_random_seq_destroy(struct kmem_cache *cachep)
{
	kfree(cachep->random_seq);
	cachep->random_seq = NULL;
}
#endif /* CONFIG_SLAB_FREELIST_RANDOM */

#ifdef CONFIG_SLUB_DEBUG
#define SLABINFO_RIGHTS (0400)

static void print_slabinfo_header(struct seq_file *m)
{
	/*
	 * Output format version, so at least we can change it
	 * without _too_ many complaints.
	 */
	seq_puts(m, "slabinfo - version: 2.1\n");
	seq_puts(m, "# name            <active_objs> <num_objs> <objsize> <objperslab> <pagesperslab>");
	seq_puts(m, " : tunables <limit> <batchcount> <sharedfactor>");
	seq_puts(m, " : slabdata <active_slabs> <num_slabs> <sharedavail>");
	seq_putc(m, '\n');
}

static void *slab_start(struct seq_file *m, loff_t *pos)
{
	mutex_lock(&slab_mutex);
	return seq_list_start(&slab_caches, *pos);
}

static void *slab_next(struct seq_file *m, void *p, loff_t *pos)
{
	return seq_list_next(p, &slab_caches, pos);
}

static void slab_stop(struct seq_file *m, void *p)
{
	mutex_unlock(&slab_mutex);
}

static void cache_show(struct kmem_cache *s, struct seq_file *m)
{
	struct slabinfo sinfo;

	memset(&sinfo, 0, sizeof(sinfo));
	get_slabinfo(s, &sinfo);

	seq_printf(m, "%-17s %6lu %6lu %6u %4u %4d",
		   s->name, sinfo.active_objs, sinfo.num_objs, s->size,
		   sinfo.objects_per_slab, (1 << sinfo.cache_order));

	seq_printf(m, " : tunables %4u %4u %4u",
		   sinfo.limit, sinfo.batchcount, sinfo.shared);
	seq_printf(m, " : slabdata %6lu %6lu %6lu",
		   sinfo.active_slabs, sinfo.num_slabs, sinfo.shared_avail);
	seq_putc(m, '\n');
}

static int slab_show(struct seq_file *m, void *p)
{
	struct kmem_cache *s = list_entry(p, struct kmem_cache, list);

	if (p == slab_caches.next)
		print_slabinfo_header(m);
	cache_show(s, m);
	return 0;
}

void dump_unreclaimable_slab(void)
{
	struct kmem_cache *s;
	struct slabinfo sinfo;

	/*
	 * Here acquiring slab_mutex is risky since we don't prefer to get
	 * sleep in oom path. But, without mutex hold, it may introduce a
	 * risk of crash.
	 * Use mutex_trylock to protect the list traverse, dump nothing
	 * without acquiring the mutex.
	 */
	if (!mutex_trylock(&slab_mutex)) {
		pr_warn("excessive unreclaimable slab but cannot dump stats\n");
		return;
	}

	pr_info("Unreclaimable slab info:\n");
	pr_info("Name                      Used          Total\n");

	list_for_each_entry(s, &slab_caches, list) {
		if (s->flags & SLAB_RECLAIM_ACCOUNT)
			continue;

		get_slabinfo(s, &sinfo);

		if (sinfo.num_objs > 0)
			pr_info("%-17s %10luKB %10luKB\n", s->name,
				(sinfo.active_objs * s->size) / 1024,
				(sinfo.num_objs * s->size) / 1024);
	}
	mutex_unlock(&slab_mutex);
}

/*
 * slabinfo_op - iterator that generates /proc/slabinfo
 *
 * Output layout:
 * cache-name
 * num-active-objs
 * total-objs
 * object size
 * num-active-slabs
 * total-slabs
 * num-pages-per-slab
 * + further values on SMP and with statistics enabled
 */
static const struct seq_operations slabinfo_op = {
	.start = slab_start,
	.next = slab_next,
	.stop = slab_stop,
	.show = slab_show,
};

static int slabinfo_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &slabinfo_op);
}

static const struct proc_ops slabinfo_proc_ops = {
	.proc_flags	= PROC_ENTRY_PERMANENT,
	.proc_open	= slabinfo_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= seq_release,
};

static int __init slab_proc_init(void)
{
	proc_create("slabinfo", SLABINFO_RIGHTS, NULL, &slabinfo_proc_ops);
	return 0;
}
module_init(slab_proc_init);

#endif /* CONFIG_SLUB_DEBUG */

static __always_inline __realloc_size(2) void *
__do_krealloc(const void *p, size_t new_size, gfp_t flags)
{
	void *ret;
	size_t ks;

	/* Check for double-free before calling ksize. */
	if (likely(!ZERO_OR_NULL_PTR(p))) {
		if (!kasan_check_byte(p))
			return NULL;
		ks = ksize(p);
	} else
		ks = 0;

	/* If the object still fits, repoison it precisely. */
	if (ks >= new_size) {
		/* Zero out spare memory. */
		if (want_init_on_alloc(flags)) {
			kasan_disable_current();
			memset(kasan_reset_tag(p) + new_size, 0, ks - new_size);
			kasan_enable_current();
		}

		p = kasan_krealloc((void *)p, new_size, flags);
		return (void *)p;
	}

	ret = kmalloc_node_track_caller_noprof(new_size, flags, NUMA_NO_NODE, _RET_IP_);
	if (ret && p) {
		/* Disable KASAN checks as the object's redzone is accessed. */
		kasan_disable_current();
		memcpy(ret, kasan_reset_tag(p), ks);
		kasan_enable_current();
	}

	return ret;
}

/**
 * krealloc - reallocate memory. The contents will remain unchanged.
 * @p: object to reallocate memory for.
 * @new_size: how many bytes of memory are required.
 * @flags: the type of memory to allocate.
 *
 * If @p is %NULL, krealloc() behaves exactly like kmalloc().  If @new_size
 * is 0 and @p is not a %NULL pointer, the object pointed to is freed.
 *
 * If __GFP_ZERO logic is requested, callers must ensure that, starting with the
 * initial memory allocation, every subsequent call to this API for the same
 * memory allocation is flagged with __GFP_ZERO. Otherwise, it is possible that
 * __GFP_ZERO is not fully honored by this API.
 *
 * This is the case, since krealloc() only knows about the bucket size of an
 * allocation (but not the exact size it was allocated with) and hence
 * implements the following semantics for shrinking and growing buffers with
 * __GFP_ZERO.
 *
 *         new             bucket
 * 0       size             size
 * |--------|----------------|
 * |  keep  |      zero      |
 *
 * In any case, the contents of the object pointed to are preserved up to the
 * lesser of the new and old sizes.
 *
 * Return: pointer to the allocated memory or %NULL in case of error
 */
void *krealloc_noprof(const void *p, size_t new_size, gfp_t flags)
{
	void *ret;

	if (unlikely(!new_size)) {
		kfree(p);
		return ZERO_SIZE_PTR;
	}

	ret = __do_krealloc(p, new_size, flags);
	if (ret && kasan_reset_tag(p) != kasan_reset_tag(ret))
		kfree(p);

	return ret;
}
EXPORT_SYMBOL(krealloc_noprof);

/**
 * kfree_sensitive - Clear sensitive information in memory before freeing
 * @p: object to free memory of
 *
 * The memory of the object @p points to is zeroed before freed.
 * If @p is %NULL, kfree_sensitive() does nothing.
 *
 * Note: this function zeroes the whole allocated buffer which can be a good
 * deal bigger than the requested buffer size passed to kmalloc(). So be
 * careful when using this function in performance sensitive code.
 */
void kfree_sensitive(const void *p)
{
	size_t ks;
	void *mem = (void *)p;

	ks = ksize(mem);
	if (ks) {
		kasan_unpoison_range(mem, ks);
		memzero_explicit(mem, ks);
	}
	kfree(mem);
}
EXPORT_SYMBOL(kfree_sensitive);

size_t ksize(const void *objp)
{
	/*
	 * We need to first check that the pointer to the object is valid.
	 * The KASAN report printed from ksize() is more useful, then when
	 * it's printed later when the behaviour could be undefined due to
	 * a potential use-after-free or double-free.
	 *
	 * We use kasan_check_byte(), which is supported for the hardware
	 * tag-based KASAN mode, unlike kasan_check_read/write().
	 *
	 * If the pointed to memory is invalid, we return 0 to avoid users of
	 * ksize() writing to and potentially corrupting the memory region.
	 *
	 * We want to perform the check before __ksize(), to avoid potentially
	 * crashing in __ksize() due to accessing invalid metadata.
	 */
	if (unlikely(ZERO_OR_NULL_PTR(objp)) || !kasan_check_byte(objp))
		return 0;

	return kfence_ksize(objp) ?: __ksize(objp);
}
EXPORT_SYMBOL(ksize);

/* Tracepoints definitions. */
EXPORT_TRACEPOINT_SYMBOL(kmalloc);
EXPORT_TRACEPOINT_SYMBOL(kmem_cache_alloc);
EXPORT_TRACEPOINT_SYMBOL(kfree);
EXPORT_TRACEPOINT_SYMBOL(kmem_cache_free);

