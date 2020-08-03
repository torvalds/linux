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
#include <linux/module.h>
#include <linux/cpu.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/debugfs.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/page.h>
#include <linux/memcontrol.h>

#define CREATE_TRACE_POINTS
#include <trace/events/kmem.h>

#include "slab.h"

enum slab_state slab_state;
LIST_HEAD(slab_caches);
DEFINE_MUTEX(slab_mutex);
struct kmem_cache *kmem_cache;

#ifdef CONFIG_HARDENED_USERCOPY
bool usercopy_fallback __ro_after_init =
		IS_ENABLED(CONFIG_HARDENED_USERCOPY_FALLBACK);
module_param(usercopy_fallback, bool, 0400);
MODULE_PARM_DESC(usercopy_fallback,
		"WARN instead of reject usercopy whitelist violations");
#endif

static LIST_HEAD(slab_caches_to_rcu_destroy);
static void slab_caches_to_rcu_destroy_workfn(struct work_struct *work);
static DECLARE_WORK(slab_caches_to_rcu_destroy_work,
		    slab_caches_to_rcu_destroy_workfn);

/*
 * Set of flags that will prevent slab merging
 */
#define SLAB_NEVER_MERGE (SLAB_RED_ZONE | SLAB_POISON | SLAB_STORE_USER | \
		SLAB_TRACE | SLAB_TYPESAFE_BY_RCU | SLAB_NOLEAKTRACE | \
		SLAB_FAILSLAB | SLAB_KASAN)

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

#ifdef CONFIG_SLUB
__setup_param("slub_nomerge", slub_nomerge, setup_slab_nomerge, 0);
#endif

__setup("slab_nomerge", setup_slab_nomerge);

/*
 * Determine the size of a slab object
 */
unsigned int kmem_cache_size(struct kmem_cache *s)
{
	return s->object_size;
}
EXPORT_SYMBOL(kmem_cache_size);

#ifdef CONFIG_DEBUG_VM
static int kmem_cache_sanity_check(const char *name, unsigned int size)
{
	if (!name || in_interrupt() || size < sizeof(void *) ||
		size > KMALLOC_MAX_SIZE) {
		pr_err("kmem_cache_create(%s) integrity check failed\n", name);
		return -EINVAL;
	}

	WARN_ON(strchr(name, ' '));	/* It confuses parsers */
	return 0;
}
#else
static inline int kmem_cache_sanity_check(const char *name, unsigned int size)
{
	return 0;
}
#endif

void __kmem_cache_free_bulk(struct kmem_cache *s, size_t nr, void **p)
{
	size_t i;

	for (i = 0; i < nr; i++) {
		if (s)
			kmem_cache_free(s, p[i]);
		else
			kfree(p[i]);
	}
}

int __kmem_cache_alloc_bulk(struct kmem_cache *s, gfp_t flags, size_t nr,
								void **p)
{
	size_t i;

	for (i = 0; i < nr; i++) {
		void *x = p[i] = kmem_cache_alloc(s, flags);
		if (!x) {
			__kmem_cache_free_bulk(s, i, p);
			return 0;
		}
	}
	return i;
}

#ifdef CONFIG_MEMCG_KMEM

LIST_HEAD(slab_root_caches);
static DEFINE_SPINLOCK(memcg_kmem_wq_lock);

static void kmemcg_cache_shutdown(struct percpu_ref *percpu_ref);

void slab_init_memcg_params(struct kmem_cache *s)
{
	s->memcg_params.root_cache = NULL;
	RCU_INIT_POINTER(s->memcg_params.memcg_caches, NULL);
	INIT_LIST_HEAD(&s->memcg_params.children);
	s->memcg_params.dying = false;
}

static int init_memcg_params(struct kmem_cache *s,
			     struct kmem_cache *root_cache)
{
	struct memcg_cache_array *arr;

	if (root_cache) {
		int ret = percpu_ref_init(&s->memcg_params.refcnt,
					  kmemcg_cache_shutdown,
					  0, GFP_KERNEL);
		if (ret)
			return ret;

		s->memcg_params.root_cache = root_cache;
		INIT_LIST_HEAD(&s->memcg_params.children_node);
		INIT_LIST_HEAD(&s->memcg_params.kmem_caches_node);
		return 0;
	}

	slab_init_memcg_params(s);

	if (!memcg_nr_cache_ids)
		return 0;

	arr = kvzalloc(sizeof(struct memcg_cache_array) +
		       memcg_nr_cache_ids * sizeof(void *),
		       GFP_KERNEL);
	if (!arr)
		return -ENOMEM;

	RCU_INIT_POINTER(s->memcg_params.memcg_caches, arr);
	return 0;
}

static void destroy_memcg_params(struct kmem_cache *s)
{
	if (is_root_cache(s)) {
		kvfree(rcu_access_pointer(s->memcg_params.memcg_caches));
	} else {
		mem_cgroup_put(s->memcg_params.memcg);
		WRITE_ONCE(s->memcg_params.memcg, NULL);
		percpu_ref_exit(&s->memcg_params.refcnt);
	}
}

static void free_memcg_params(struct rcu_head *rcu)
{
	struct memcg_cache_array *old;

	old = container_of(rcu, struct memcg_cache_array, rcu);
	kvfree(old);
}

static int update_memcg_params(struct kmem_cache *s, int new_array_size)
{
	struct memcg_cache_array *old, *new;

	new = kvzalloc(sizeof(struct memcg_cache_array) +
		       new_array_size * sizeof(void *), GFP_KERNEL);
	if (!new)
		return -ENOMEM;

	old = rcu_dereference_protected(s->memcg_params.memcg_caches,
					lockdep_is_held(&slab_mutex));
	if (old)
		memcpy(new->entries, old->entries,
		       memcg_nr_cache_ids * sizeof(void *));

	rcu_assign_pointer(s->memcg_params.memcg_caches, new);
	if (old)
		call_rcu(&old->rcu, free_memcg_params);
	return 0;
}

int memcg_update_all_caches(int num_memcgs)
{
	struct kmem_cache *s;
	int ret = 0;

	mutex_lock(&slab_mutex);
	list_for_each_entry(s, &slab_root_caches, root_caches_node) {
		ret = update_memcg_params(s, num_memcgs);
		/*
		 * Instead of freeing the memory, we'll just leave the caches
		 * up to this point in an updated state.
		 */
		if (ret)
			break;
	}
	mutex_unlock(&slab_mutex);
	return ret;
}

void memcg_link_cache(struct kmem_cache *s, struct mem_cgroup *memcg)
{
	if (is_root_cache(s)) {
		list_add(&s->root_caches_node, &slab_root_caches);
	} else {
		css_get(&memcg->css);
		s->memcg_params.memcg = memcg;
		list_add(&s->memcg_params.children_node,
			 &s->memcg_params.root_cache->memcg_params.children);
		list_add(&s->memcg_params.kmem_caches_node,
			 &s->memcg_params.memcg->kmem_caches);
	}
}

static void memcg_unlink_cache(struct kmem_cache *s)
{
	if (is_root_cache(s)) {
		list_del(&s->root_caches_node);
	} else {
		list_del(&s->memcg_params.children_node);
		list_del(&s->memcg_params.kmem_caches_node);
	}
}
#else
static inline int init_memcg_params(struct kmem_cache *s,
				    struct kmem_cache *root_cache)
{
	return 0;
}

static inline void destroy_memcg_params(struct kmem_cache *s)
{
}

static inline void memcg_unlink_cache(struct kmem_cache *s)
{
}
#endif /* CONFIG_MEMCG_KMEM */

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

	if (align < ARCH_SLAB_MINALIGN)
		align = ARCH_SLAB_MINALIGN;

	return ALIGN(align, sizeof(void *));
}

/*
 * Find a mergeable slab cache
 */
int slab_unmergeable(struct kmem_cache *s)
{
	if (slab_nomerge || (s->flags & SLAB_NEVER_MERGE))
		return 1;

	if (!is_root_cache(s))
		return 1;

	if (s->ctor)
		return 1;

	if (s->usersize)
		return 1;

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

	size = ALIGN(size, sizeof(void *));
	align = calculate_alignment(flags, align, size);
	size = ALIGN(size, align);
	flags = kmem_cache_flags(size, flags, name, NULL);

	if (flags & SLAB_NEVER_MERGE)
		return NULL;

	list_for_each_entry_reverse(s, &slab_root_caches, root_caches_node) {
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

		if (IS_ENABLED(CONFIG_SLAB) && align &&
			(align > s->align || s->align % align))
			continue;

		return s;
	}
	return NULL;
}

static struct kmem_cache *create_cache(const char *name,
		unsigned int object_size, unsigned int align,
		slab_flags_t flags, unsigned int useroffset,
		unsigned int usersize, void (*ctor)(void *),
		struct mem_cgroup *memcg, struct kmem_cache *root_cache)
{
	struct kmem_cache *s;
	int err;

	if (WARN_ON(useroffset + usersize > object_size))
		useroffset = usersize = 0;

	err = -ENOMEM;
	s = kmem_cache_zalloc(kmem_cache, GFP_KERNEL);
	if (!s)
		goto out;

	s->name = name;
	s->size = s->object_size = object_size;
	s->align = align;
	s->ctor = ctor;
	s->useroffset = useroffset;
	s->usersize = usersize;

	err = init_memcg_params(s, root_cache);
	if (err)
		goto out_free_cache;

	err = __kmem_cache_create(s, flags);
	if (err)
		goto out_free_cache;

	s->refcount = 1;
	list_add(&s->list, &slab_caches);
	memcg_link_cache(s, memcg);
out:
	if (err)
		return ERR_PTR(err);
	return s;

out_free_cache:
	destroy_memcg_params(s);
	kmem_cache_free(kmem_cache, s);
	goto out;
}

/**
 * kmem_cache_create_usercopy - Create a cache with a region suitable
 * for copying to userspace
 * @name: A string which is used in /proc/slabinfo to identify this cache.
 * @size: The size of objects to be created in this cache.
 * @align: The required alignment for the objects.
 * @flags: SLAB flags
 * @useroffset: Usercopy region offset
 * @usersize: Usercopy region size
 * @ctor: A constructor for the objects.
 *
 * Cannot be called within a interrupt, but can be interrupted.
 * The @ctor is run when new pages are allocated by the cache.
 *
 * The flags are
 *
 * %SLAB_POISON - Poison the slab with a known test pattern (a5a5a5a5)
 * to catch references to uninitialised memory.
 *
 * %SLAB_RED_ZONE - Insert `Red` zones around the allocated memory to check
 * for buffer overruns.
 *
 * %SLAB_HWCACHE_ALIGN - Align the objects in this cache to a hardware
 * cacheline.  This can be beneficial if you're counting cycles as closely
 * as davem.
 *
 * Return: a pointer to the cache on success, NULL on failure.
 */
struct kmem_cache *
kmem_cache_create_usercopy(const char *name,
		  unsigned int size, unsigned int align,
		  slab_flags_t flags,
		  unsigned int useroffset, unsigned int usersize,
		  void (*ctor)(void *))
{
	struct kmem_cache *s = NULL;
	const char *cache_name;
	int err;

	get_online_cpus();
	get_online_mems();
	memcg_get_cache_ids();

	mutex_lock(&slab_mutex);

	err = kmem_cache_sanity_check(name, size);
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
	if (WARN_ON(!usersize && useroffset) ||
	    WARN_ON(size < usersize || size - usersize < useroffset))
		usersize = useroffset = 0;

	if (!usersize)
		s = __kmem_cache_alias(name, size, align, flags, ctor);
	if (s)
		goto out_unlock;

	cache_name = kstrdup_const(name, GFP_KERNEL);
	if (!cache_name) {
		err = -ENOMEM;
		goto out_unlock;
	}

	s = create_cache(cache_name, size,
			 calculate_alignment(flags, align, size),
			 flags, useroffset, usersize, ctor, NULL, NULL);
	if (IS_ERR(s)) {
		err = PTR_ERR(s);
		kfree_const(cache_name);
	}

out_unlock:
	mutex_unlock(&slab_mutex);

	memcg_put_cache_ids();
	put_online_mems();
	put_online_cpus();

	if (err) {
		if (flags & SLAB_PANIC)
			panic("kmem_cache_create: Failed to create slab '%s'. Error %d\n",
				name, err);
		else {
			pr_warn("kmem_cache_create(%s) failed with error %d\n",
				name, err);
			dump_stack();
		}
		return NULL;
	}
	return s;
}
EXPORT_SYMBOL(kmem_cache_create_usercopy);

/**
 * kmem_cache_create - Create a cache.
 * @name: A string which is used in /proc/slabinfo to identify this cache.
 * @size: The size of objects to be created in this cache.
 * @align: The required alignment for the objects.
 * @flags: SLAB flags
 * @ctor: A constructor for the objects.
 *
 * Cannot be called within a interrupt, but can be interrupted.
 * The @ctor is run when new pages are allocated by the cache.
 *
 * The flags are
 *
 * %SLAB_POISON - Poison the slab with a known test pattern (a5a5a5a5)
 * to catch references to uninitialised memory.
 *
 * %SLAB_RED_ZONE - Insert `Red` zones around the allocated memory to check
 * for buffer overruns.
 *
 * %SLAB_HWCACHE_ALIGN - Align the objects in this cache to a hardware
 * cacheline.  This can be beneficial if you're counting cycles as closely
 * as davem.
 *
 * Return: a pointer to the cache on success, NULL on failure.
 */
struct kmem_cache *
kmem_cache_create(const char *name, unsigned int size, unsigned int align,
		slab_flags_t flags, void (*ctor)(void *))
{
	return kmem_cache_create_usercopy(name, size, align, flags, 0, 0,
					  ctor);
}
EXPORT_SYMBOL(kmem_cache_create);

static void slab_caches_to_rcu_destroy_workfn(struct work_struct *work)
{
	LIST_HEAD(to_destroy);
	struct kmem_cache *s, *s2;

	/*
	 * On destruction, SLAB_TYPESAFE_BY_RCU kmem_caches are put on the
	 * @slab_caches_to_rcu_destroy list.  The slab pages are freed
	 * through RCU and and the associated kmem_cache are dereferenced
	 * while freeing the pages, so the kmem_caches should be freed only
	 * after the pending RCU operations are finished.  As rcu_barrier()
	 * is a pretty slow operation, we batch all pending destructions
	 * asynchronously.
	 */
	mutex_lock(&slab_mutex);
	list_splice_init(&slab_caches_to_rcu_destroy, &to_destroy);
	mutex_unlock(&slab_mutex);

	if (list_empty(&to_destroy))
		return;

	rcu_barrier();

	list_for_each_entry_safe(s, s2, &to_destroy, list) {
#ifdef SLAB_SUPPORTS_SYSFS
		sysfs_slab_release(s);
#else
		slab_kmem_cache_release(s);
#endif
	}
}

static int shutdown_cache(struct kmem_cache *s)
{
	/* free asan quarantined objects */
	kasan_cache_shutdown(s);

	if (__kmem_cache_shutdown(s) != 0)
		return -EBUSY;

	memcg_unlink_cache(s);
	list_del(&s->list);

	if (s->flags & SLAB_TYPESAFE_BY_RCU) {
#ifdef SLAB_SUPPORTS_SYSFS
		sysfs_slab_unlink(s);
#endif
		list_add_tail(&s->list, &slab_caches_to_rcu_destroy);
		schedule_work(&slab_caches_to_rcu_destroy_work);
	} else {
#ifdef SLAB_SUPPORTS_SYSFS
		sysfs_slab_unlink(s);
		sysfs_slab_release(s);
#else
		slab_kmem_cache_release(s);
#endif
	}

	return 0;
}

#ifdef CONFIG_MEMCG_KMEM
/*
 * memcg_create_kmem_cache - Create a cache for a memory cgroup.
 * @memcg: The memory cgroup the new cache is for.
 * @root_cache: The parent of the new cache.
 *
 * This function attempts to create a kmem cache that will serve allocation
 * requests going from @memcg to @root_cache. The new cache inherits properties
 * from its parent.
 */
void memcg_create_kmem_cache(struct mem_cgroup *memcg,
			     struct kmem_cache *root_cache)
{
	static char memcg_name_buf[NAME_MAX + 1]; /* protected by slab_mutex */
	struct cgroup_subsys_state *css = &memcg->css;
	struct memcg_cache_array *arr;
	struct kmem_cache *s = NULL;
	char *cache_name;
	int idx;

	get_online_cpus();
	get_online_mems();

	mutex_lock(&slab_mutex);

	/*
	 * The memory cgroup could have been offlined while the cache
	 * creation work was pending.
	 */
	if (memcg->kmem_state != KMEM_ONLINE)
		goto out_unlock;

	idx = memcg_cache_id(memcg);
	arr = rcu_dereference_protected(root_cache->memcg_params.memcg_caches,
					lockdep_is_held(&slab_mutex));

	/*
	 * Since per-memcg caches are created asynchronously on first
	 * allocation (see memcg_kmem_get_cache()), several threads can try to
	 * create the same cache, but only one of them may succeed.
	 */
	if (arr->entries[idx])
		goto out_unlock;

	cgroup_name(css->cgroup, memcg_name_buf, sizeof(memcg_name_buf));
	cache_name = kasprintf(GFP_KERNEL, "%s(%llu:%s)", root_cache->name,
			       css->serial_nr, memcg_name_buf);
	if (!cache_name)
		goto out_unlock;

	s = create_cache(cache_name, root_cache->object_size,
			 root_cache->align,
			 root_cache->flags & CACHE_CREATE_MASK,
			 root_cache->useroffset, root_cache->usersize,
			 root_cache->ctor, memcg, root_cache);
	/*
	 * If we could not create a memcg cache, do not complain, because
	 * that's not critical at all as we can always proceed with the root
	 * cache.
	 */
	if (IS_ERR(s)) {
		kfree(cache_name);
		goto out_unlock;
	}

	/*
	 * Since readers won't lock (see memcg_kmem_get_cache()), we need a
	 * barrier here to ensure nobody will see the kmem_cache partially
	 * initialized.
	 */
	smp_wmb();
	arr->entries[idx] = s;

out_unlock:
	mutex_unlock(&slab_mutex);

	put_online_mems();
	put_online_cpus();
}

static void kmemcg_workfn(struct work_struct *work)
{
	struct kmem_cache *s = container_of(work, struct kmem_cache,
					    memcg_params.work);

	get_online_cpus();
	get_online_mems();

	mutex_lock(&slab_mutex);
	s->memcg_params.work_fn(s);
	mutex_unlock(&slab_mutex);

	put_online_mems();
	put_online_cpus();
}

static void kmemcg_rcufn(struct rcu_head *head)
{
	struct kmem_cache *s = container_of(head, struct kmem_cache,
					    memcg_params.rcu_head);

	/*
	 * We need to grab blocking locks.  Bounce to ->work.  The
	 * work item shares the space with the RCU head and can't be
	 * initialized earlier.
	 */
	INIT_WORK(&s->memcg_params.work, kmemcg_workfn);
	queue_work(memcg_kmem_cache_wq, &s->memcg_params.work);
}

static void kmemcg_cache_shutdown_fn(struct kmem_cache *s)
{
	WARN_ON(shutdown_cache(s));
}

static void kmemcg_cache_shutdown(struct percpu_ref *percpu_ref)
{
	struct kmem_cache *s = container_of(percpu_ref, struct kmem_cache,
					    memcg_params.refcnt);
	unsigned long flags;

	spin_lock_irqsave(&memcg_kmem_wq_lock, flags);
	if (s->memcg_params.root_cache->memcg_params.dying)
		goto unlock;

	s->memcg_params.work_fn = kmemcg_cache_shutdown_fn;
	INIT_WORK(&s->memcg_params.work, kmemcg_workfn);
	queue_work(memcg_kmem_cache_wq, &s->memcg_params.work);

unlock:
	spin_unlock_irqrestore(&memcg_kmem_wq_lock, flags);
}

static void kmemcg_cache_deactivate_after_rcu(struct kmem_cache *s)
{
	__kmemcg_cache_deactivate_after_rcu(s);
	percpu_ref_kill(&s->memcg_params.refcnt);
}

static void kmemcg_cache_deactivate(struct kmem_cache *s)
{
	if (WARN_ON_ONCE(is_root_cache(s)))
		return;

	__kmemcg_cache_deactivate(s);
	s->flags |= SLAB_DEACTIVATED;

	/*
	 * memcg_kmem_wq_lock is used to synchronize memcg_params.dying
	 * flag and make sure that no new kmem_cache deactivation tasks
	 * are queued (see flush_memcg_workqueue() ).
	 */
	spin_lock_irq(&memcg_kmem_wq_lock);
	if (s->memcg_params.root_cache->memcg_params.dying)
		goto unlock;

	s->memcg_params.work_fn = kmemcg_cache_deactivate_after_rcu;
	call_rcu(&s->memcg_params.rcu_head, kmemcg_rcufn);
unlock:
	spin_unlock_irq(&memcg_kmem_wq_lock);
}

void memcg_deactivate_kmem_caches(struct mem_cgroup *memcg,
				  struct mem_cgroup *parent)
{
	int idx;
	struct memcg_cache_array *arr;
	struct kmem_cache *s, *c;
	unsigned int nr_reparented;

	idx = memcg_cache_id(memcg);

	get_online_cpus();
	get_online_mems();

	mutex_lock(&slab_mutex);
	list_for_each_entry(s, &slab_root_caches, root_caches_node) {
		arr = rcu_dereference_protected(s->memcg_params.memcg_caches,
						lockdep_is_held(&slab_mutex));
		c = arr->entries[idx];
		if (!c)
			continue;

		kmemcg_cache_deactivate(c);
		arr->entries[idx] = NULL;
	}
	nr_reparented = 0;
	list_for_each_entry(s, &memcg->kmem_caches,
			    memcg_params.kmem_caches_node) {
		WRITE_ONCE(s->memcg_params.memcg, parent);
		css_put(&memcg->css);
		nr_reparented++;
	}
	if (nr_reparented) {
		list_splice_init(&memcg->kmem_caches,
				 &parent->kmem_caches);
		css_get_many(&parent->css, nr_reparented);
	}
	mutex_unlock(&slab_mutex);

	put_online_mems();
	put_online_cpus();
}

static int shutdown_memcg_caches(struct kmem_cache *s)
{
	struct memcg_cache_array *arr;
	struct kmem_cache *c, *c2;
	LIST_HEAD(busy);
	int i;

	BUG_ON(!is_root_cache(s));

	/*
	 * First, shutdown active caches, i.e. caches that belong to online
	 * memory cgroups.
	 */
	arr = rcu_dereference_protected(s->memcg_params.memcg_caches,
					lockdep_is_held(&slab_mutex));
	for_each_memcg_cache_index(i) {
		c = arr->entries[i];
		if (!c)
			continue;
		if (shutdown_cache(c))
			/*
			 * The cache still has objects. Move it to a temporary
			 * list so as not to try to destroy it for a second
			 * time while iterating over inactive caches below.
			 */
			list_move(&c->memcg_params.children_node, &busy);
		else
			/*
			 * The cache is empty and will be destroyed soon. Clear
			 * the pointer to it in the memcg_caches array so that
			 * it will never be accessed even if the root cache
			 * stays alive.
			 */
			arr->entries[i] = NULL;
	}

	/*
	 * Second, shutdown all caches left from memory cgroups that are now
	 * offline.
	 */
	list_for_each_entry_safe(c, c2, &s->memcg_params.children,
				 memcg_params.children_node)
		shutdown_cache(c);

	list_splice(&busy, &s->memcg_params.children);

	/*
	 * A cache being destroyed must be empty. In particular, this means
	 * that all per memcg caches attached to it must be empty too.
	 */
	if (!list_empty(&s->memcg_params.children))
		return -EBUSY;
	return 0;
}

static void flush_memcg_workqueue(struct kmem_cache *s)
{
	spin_lock_irq(&memcg_kmem_wq_lock);
	s->memcg_params.dying = true;
	spin_unlock_irq(&memcg_kmem_wq_lock);

	/*
	 * SLAB and SLUB deactivate the kmem_caches through call_rcu. Make
	 * sure all registered rcu callbacks have been invoked.
	 */
	rcu_barrier();

	/*
	 * SLAB and SLUB create memcg kmem_caches through workqueue and SLUB
	 * deactivates the memcg kmem_caches through workqueue. Make sure all
	 * previous workitems on workqueue are processed.
	 */
	if (likely(memcg_kmem_cache_wq))
		flush_workqueue(memcg_kmem_cache_wq);

	/*
	 * If we're racing with children kmem_cache deactivation, it might
	 * take another rcu grace period to complete their destruction.
	 * At this moment the corresponding percpu_ref_kill() call should be
	 * done, but it might take another rcu grace period to complete
	 * switching to the atomic mode.
	 * Please, note that we check without grabbing the slab_mutex. It's safe
	 * because at this moment the children list can't grow.
	 */
	if (!list_empty(&s->memcg_params.children))
		rcu_barrier();
}
#else
static inline int shutdown_memcg_caches(struct kmem_cache *s)
{
	return 0;
}

static inline void flush_memcg_workqueue(struct kmem_cache *s)
{
}
#endif /* CONFIG_MEMCG_KMEM */

void slab_kmem_cache_release(struct kmem_cache *s)
{
	__kmem_cache_release(s);
	destroy_memcg_params(s);
	kfree_const(s->name);
	kmem_cache_free(kmem_cache, s);
}

void kmem_cache_destroy(struct kmem_cache *s)
{
	int err;

	if (unlikely(!s))
		return;

	flush_memcg_workqueue(s);

	get_online_cpus();
	get_online_mems();

	mutex_lock(&slab_mutex);

	s->refcount--;
	if (s->refcount)
		goto out_unlock;

	err = shutdown_memcg_caches(s);
	if (!err)
		err = shutdown_cache(s);

	if (err) {
		pr_err("kmem_cache_destroy %s: Slab cache still has objects\n",
		       s->name);
		dump_stack();
	}
out_unlock:
	mutex_unlock(&slab_mutex);

	put_online_mems();
	put_online_cpus();
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
	int ret;

	get_online_cpus();
	get_online_mems();
	kasan_cache_shrink(cachep);
	ret = __kmem_cache_shrink(cachep);
	put_online_mems();
	put_online_cpus();
	return ret;
}
EXPORT_SYMBOL(kmem_cache_shrink);

/**
 * kmem_cache_shrink_all - shrink a cache and all memcg caches for root cache
 * @s: The cache pointer
 */
void kmem_cache_shrink_all(struct kmem_cache *s)
{
	struct kmem_cache *c;

	if (!IS_ENABLED(CONFIG_MEMCG_KMEM) || !is_root_cache(s)) {
		kmem_cache_shrink(s);
		return;
	}

	get_online_cpus();
	get_online_mems();
	kasan_cache_shrink(s);
	__kmem_cache_shrink(s);

	/*
	 * We have to take the slab_mutex to protect from the memcg list
	 * modification.
	 */
	mutex_lock(&slab_mutex);
	for_each_memcg_cache(c, s) {
		/*
		 * Don't need to shrink deactivated memcg caches.
		 */
		if (s->flags & SLAB_DEACTIVATED)
			continue;
		kasan_cache_shrink(c);
		__kmem_cache_shrink(c);
	}
	mutex_unlock(&slab_mutex);
	put_online_mems();
	put_online_cpus();
}

bool slab_is_available(void)
{
	return slab_state >= UP;
}

#ifndef CONFIG_SLOB
/* Create a cache during boot when no slab services are available yet */
void __init create_boot_cache(struct kmem_cache *s, const char *name,
		unsigned int size, slab_flags_t flags,
		unsigned int useroffset, unsigned int usersize)
{
	int err;
	unsigned int align = ARCH_KMALLOC_MINALIGN;

	s->name = name;
	s->size = s->object_size = size;

	/*
	 * For power of two sizes, guarantee natural alignment for kmalloc
	 * caches, regardless of SL*B debugging options.
	 */
	if (is_power_of_2(size))
		align = max(align, size);
	s->align = calculate_alignment(flags, align, size);

	s->useroffset = useroffset;
	s->usersize = usersize;

	slab_init_memcg_params(s);

	err = __kmem_cache_create(s, flags);

	if (err)
		panic("Creation of kmalloc slab %s size=%u failed. Reason %d\n",
					name, size, err);

	s->refcount = -1;	/* Exempt from merging for now */
}

struct kmem_cache *__init create_kmalloc_cache(const char *name,
		unsigned int size, slab_flags_t flags,
		unsigned int useroffset, unsigned int usersize)
{
	struct kmem_cache *s = kmem_cache_zalloc(kmem_cache, GFP_NOWAIT);

	if (!s)
		panic("Out of memory when creating slab %s\n", name);

	create_boot_cache(s, name, size, flags, useroffset, usersize);
	list_add(&s->list, &slab_caches);
	memcg_link_cache(s, NULL);
	s->refcount = 1;
	return s;
}

struct kmem_cache *
kmalloc_caches[NR_KMALLOC_TYPES][KMALLOC_SHIFT_HIGH + 1] __ro_after_init =
{ /* initialization for https://bugs.llvm.org/show_bug.cgi?id=42570 */ };
EXPORT_SYMBOL(kmalloc_caches);

/*
 * Conversion table for small slabs sizes / 8 to the index in the
 * kmalloc array. This is necessary for slabs < 192 since we have non power
 * of two cache sizes there. The size of larger slabs can be determined using
 * fls.
 */
static u8 size_index[24] __ro_after_init = {
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

static inline unsigned int size_index_elem(unsigned int bytes)
{
	return (bytes - 1) / 8;
}

/*
 * Find the kmem_cache structure that serves a given size of
 * allocation
 */
struct kmem_cache *kmalloc_slab(size_t size, gfp_t flags)
{
	unsigned int index;

	if (size <= 192) {
		if (!size)
			return ZERO_SIZE_PTR;

		index = size_index[size_index_elem(size)];
	} else {
		if (WARN_ON_ONCE(size > KMALLOC_MAX_CACHE_SIZE))
			return NULL;
		index = fls(size - 1);
	}

	return kmalloc_caches[kmalloc_type(flags)][index];
}

#ifdef CONFIG_ZONE_DMA
#define INIT_KMALLOC_INFO(__size, __short_size)			\
{								\
	.name[KMALLOC_NORMAL]  = "kmalloc-" #__short_size,	\
	.name[KMALLOC_RECLAIM] = "kmalloc-rcl-" #__short_size,	\
	.name[KMALLOC_DMA]     = "dma-kmalloc-" #__short_size,	\
	.size = __size,						\
}
#else
#define INIT_KMALLOC_INFO(__size, __short_size)			\
{								\
	.name[KMALLOC_NORMAL]  = "kmalloc-" #__short_size,	\
	.name[KMALLOC_RECLAIM] = "kmalloc-rcl-" #__short_size,	\
	.size = __size,						\
}
#endif

/*
 * kmalloc_info[] is to make slub_debug=,kmalloc-xx option work at boot time.
 * kmalloc_index() supports up to 2^26=64MB, so the final entry of the table is
 * kmalloc-67108864.
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
	INIT_KMALLOC_INFO(2097152, 2M),
	INIT_KMALLOC_INFO(4194304, 4M),
	INIT_KMALLOC_INFO(8388608, 8M),
	INIT_KMALLOC_INFO(16777216, 16M),
	INIT_KMALLOC_INFO(33554432, 32M),
	INIT_KMALLOC_INFO(67108864, 64M)
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
		(KMALLOC_MIN_SIZE & (KMALLOC_MIN_SIZE - 1)));

	for (i = 8; i < KMALLOC_MIN_SIZE; i += 8) {
		unsigned int elem = size_index_elem(i);

		if (elem >= ARRAY_SIZE(size_index))
			break;
		size_index[elem] = KMALLOC_SHIFT_LOW;
	}

	if (KMALLOC_MIN_SIZE >= 64) {
		/*
		 * The 96 byte size cache is not used if the alignment
		 * is 64 byte.
		 */
		for (i = 64 + 8; i <= 96; i += 8)
			size_index[size_index_elem(i)] = 7;

	}

	if (KMALLOC_MIN_SIZE >= 128) {
		/*
		 * The 192 byte sized cache is not used if the alignment
		 * is 128 byte. Redirect kmalloc to use the 256 byte cache
		 * instead.
		 */
		for (i = 128 + 8; i <= 192; i += 8)
			size_index[size_index_elem(i)] = 8;
	}
}

static void __init
new_kmalloc_cache(int idx, enum kmalloc_cache_type type, slab_flags_t flags)
{
	if (type == KMALLOC_RECLAIM)
		flags |= SLAB_RECLAIM_ACCOUNT;

	kmalloc_caches[type][idx] = create_kmalloc_cache(
					kmalloc_info[idx].name[type],
					kmalloc_info[idx].size, flags, 0,
					kmalloc_info[idx].size);
}

/*
 * Create the kmalloc array. Some of the regular kmalloc arrays
 * may already have been created because they were needed to
 * enable allocations for slab creation.
 */
void __init create_kmalloc_caches(slab_flags_t flags)
{
	int i;
	enum kmalloc_cache_type type;

	for (type = KMALLOC_NORMAL; type <= KMALLOC_RECLAIM; type++) {
		for (i = KMALLOC_SHIFT_LOW; i <= KMALLOC_SHIFT_HIGH; i++) {
			if (!kmalloc_caches[type][i])
				new_kmalloc_cache(i, type, flags);

			/*
			 * Caches that are not of the two-to-the-power-of size.
			 * These have to be created immediately after the
			 * earlier power of two caches
			 */
			if (KMALLOC_MIN_SIZE <= 32 && i == 6 &&
					!kmalloc_caches[type][1])
				new_kmalloc_cache(1, type, flags);
			if (KMALLOC_MIN_SIZE <= 64 && i == 7 &&
					!kmalloc_caches[type][2])
				new_kmalloc_cache(2, type, flags);
		}
	}

	/* Kmalloc array is now usable */
	slab_state = UP;

#ifdef CONFIG_ZONE_DMA
	for (i = 0; i <= KMALLOC_SHIFT_HIGH; i++) {
		struct kmem_cache *s = kmalloc_caches[KMALLOC_NORMAL][i];

		if (s) {
			kmalloc_caches[KMALLOC_DMA][i] = create_kmalloc_cache(
				kmalloc_info[i].name[KMALLOC_DMA],
				kmalloc_info[i].size,
				SLAB_CACHE_DMA | flags, 0,
				kmalloc_info[i].size);
		}
	}
#endif
}
#endif /* !CONFIG_SLOB */

/*
 * To avoid unnecessary overhead, we pass through large allocation requests
 * directly to the page allocator. We use __GFP_COMP, because we will need to
 * know the allocation order to free the pages properly in kfree.
 */
void *kmalloc_order(size_t size, gfp_t flags, unsigned int order)
{
	void *ret = NULL;
	struct page *page;

	flags |= __GFP_COMP;
	page = alloc_pages(flags, order);
	if (likely(page)) {
		ret = page_address(page);
		mod_node_page_state(page_pgdat(page), NR_SLAB_UNRECLAIMABLE,
				    1 << order);
	}
	ret = kasan_kmalloc_large(ret, size, flags);
	/* As ret might get tagged, call kmemleak hook after KASAN. */
	kmemleak_alloc(ret, size, 1, flags);
	return ret;
}
EXPORT_SYMBOL(kmalloc_order);

#ifdef CONFIG_TRACING
void *kmalloc_order_trace(size_t size, gfp_t flags, unsigned int order)
{
	void *ret = kmalloc_order(size, flags, order);
	trace_kmalloc(_RET_IP_, ret, size, PAGE_SIZE << order, flags);
	return ret;
}
EXPORT_SYMBOL(kmalloc_order_trace);
#endif

#ifdef CONFIG_SLAB_FREELIST_RANDOM
/* Randomize a generic freelist */
static void freelist_randomize(struct rnd_state *state, unsigned int *list,
			       unsigned int count)
{
	unsigned int rand;
	unsigned int i;

	for (i = 0; i < count; i++)
		list[i] = i;

	/* Fisher-Yates shuffle */
	for (i = count - 1; i > 0; i--) {
		rand = prandom_u32_state(state);
		rand %= (i + 1);
		swap(list[i], list[rand]);
	}
}

/* Create a random sequence per cache */
int cache_random_seq_create(struct kmem_cache *cachep, unsigned int count,
				    gfp_t gfp)
{
	struct rnd_state state;

	if (count < 2 || cachep->random_seq)
		return 0;

	cachep->random_seq = kcalloc(count, sizeof(unsigned int), gfp);
	if (!cachep->random_seq)
		return -ENOMEM;

	/* Get best entropy at this stage of boot */
	prandom_seed_state(&state, get_random_long());

	freelist_randomize(&state, cachep->random_seq, count);
	return 0;
}

/* Destroy the per-cache random freelist sequence */
void cache_random_seq_destroy(struct kmem_cache *cachep)
{
	kfree(cachep->random_seq);
	cachep->random_seq = NULL;
}
#endif /* CONFIG_SLAB_FREELIST_RANDOM */

#if defined(CONFIG_SLAB) || defined(CONFIG_SLUB_DEBUG)
#ifdef CONFIG_SLAB
#define SLABINFO_RIGHTS (0600)
#else
#define SLABINFO_RIGHTS (0400)
#endif

static void print_slabinfo_header(struct seq_file *m)
{
	/*
	 * Output format version, so at least we can change it
	 * without _too_ many complaints.
	 */
#ifdef CONFIG_DEBUG_SLAB
	seq_puts(m, "slabinfo - version: 2.1 (statistics)\n");
#else
	seq_puts(m, "slabinfo - version: 2.1\n");
#endif
	seq_puts(m, "# name            <active_objs> <num_objs> <objsize> <objperslab> <pagesperslab>");
	seq_puts(m, " : tunables <limit> <batchcount> <sharedfactor>");
	seq_puts(m, " : slabdata <active_slabs> <num_slabs> <sharedavail>");
#ifdef CONFIG_DEBUG_SLAB
	seq_puts(m, " : globalstat <listallocs> <maxobjs> <grown> <reaped> <error> <maxfreeable> <nodeallocs> <remotefrees> <alienoverflow>");
	seq_puts(m, " : cpustat <allochit> <allocmiss> <freehit> <freemiss>");
#endif
	seq_putc(m, '\n');
}

void *slab_start(struct seq_file *m, loff_t *pos)
{
	mutex_lock(&slab_mutex);
	return seq_list_start(&slab_root_caches, *pos);
}

void *slab_next(struct seq_file *m, void *p, loff_t *pos)
{
	return seq_list_next(p, &slab_root_caches, pos);
}

void slab_stop(struct seq_file *m, void *p)
{
	mutex_unlock(&slab_mutex);
}

static void
memcg_accumulate_slabinfo(struct kmem_cache *s, struct slabinfo *info)
{
	struct kmem_cache *c;
	struct slabinfo sinfo;

	if (!is_root_cache(s))
		return;

	for_each_memcg_cache(c, s) {
		memset(&sinfo, 0, sizeof(sinfo));
		get_slabinfo(c, &sinfo);

		info->active_slabs += sinfo.active_slabs;
		info->num_slabs += sinfo.num_slabs;
		info->shared_avail += sinfo.shared_avail;
		info->active_objs += sinfo.active_objs;
		info->num_objs += sinfo.num_objs;
	}
}

static void cache_show(struct kmem_cache *s, struct seq_file *m)
{
	struct slabinfo sinfo;

	memset(&sinfo, 0, sizeof(sinfo));
	get_slabinfo(s, &sinfo);

	memcg_accumulate_slabinfo(s, &sinfo);

	seq_printf(m, "%-17s %6lu %6lu %6u %4u %4d",
		   cache_name(s), sinfo.active_objs, sinfo.num_objs, s->size,
		   sinfo.objects_per_slab, (1 << sinfo.cache_order));

	seq_printf(m, " : tunables %4u %4u %4u",
		   sinfo.limit, sinfo.batchcount, sinfo.shared);
	seq_printf(m, " : slabdata %6lu %6lu %6lu",
		   sinfo.active_slabs, sinfo.num_slabs, sinfo.shared_avail);
	slabinfo_show_stats(m, s);
	seq_putc(m, '\n');
}

static int slab_show(struct seq_file *m, void *p)
{
	struct kmem_cache *s = list_entry(p, struct kmem_cache, root_caches_node);

	if (p == slab_root_caches.next)
		print_slabinfo_header(m);
	cache_show(s, m);
	return 0;
}

void dump_unreclaimable_slab(void)
{
	struct kmem_cache *s, *s2;
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

	list_for_each_entry_safe(s, s2, &slab_caches, list) {
		if (!is_root_cache(s) || (s->flags & SLAB_RECLAIM_ACCOUNT))
			continue;

		get_slabinfo(s, &sinfo);

		if (sinfo.num_objs > 0)
			pr_info("%-17s %10luKB %10luKB\n", cache_name(s),
				(sinfo.active_objs * s->size) / 1024,
				(sinfo.num_objs * s->size) / 1024);
	}
	mutex_unlock(&slab_mutex);
}

#if defined(CONFIG_MEMCG_KMEM)
void *memcg_slab_start(struct seq_file *m, loff_t *pos)
{
	struct mem_cgroup *memcg = mem_cgroup_from_seq(m);

	mutex_lock(&slab_mutex);
	return seq_list_start(&memcg->kmem_caches, *pos);
}

void *memcg_slab_next(struct seq_file *m, void *p, loff_t *pos)
{
	struct mem_cgroup *memcg = mem_cgroup_from_seq(m);

	return seq_list_next(p, &memcg->kmem_caches, pos);
}

void memcg_slab_stop(struct seq_file *m, void *p)
{
	mutex_unlock(&slab_mutex);
}

int memcg_slab_show(struct seq_file *m, void *p)
{
	struct kmem_cache *s = list_entry(p, struct kmem_cache,
					  memcg_params.kmem_caches_node);
	struct mem_cgroup *memcg = mem_cgroup_from_seq(m);

	if (p == memcg->kmem_caches.next)
		print_slabinfo_header(m);
	cache_show(s, m);
	return 0;
}
#endif

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
	.proc_write	= slabinfo_write,
	.proc_lseek	= seq_lseek,
	.proc_release	= seq_release,
};

static int __init slab_proc_init(void)
{
	proc_create("slabinfo", SLABINFO_RIGHTS, NULL, &slabinfo_proc_ops);
	return 0;
}
module_init(slab_proc_init);

#if defined(CONFIG_DEBUG_FS) && defined(CONFIG_MEMCG_KMEM)
/*
 * Display information about kmem caches that have child memcg caches.
 */
static int memcg_slabinfo_show(struct seq_file *m, void *unused)
{
	struct kmem_cache *s, *c;
	struct slabinfo sinfo;

	mutex_lock(&slab_mutex);
	seq_puts(m, "# <name> <css_id[:dead|deact]> <active_objs> <num_objs>");
	seq_puts(m, " <active_slabs> <num_slabs>\n");
	list_for_each_entry(s, &slab_root_caches, root_caches_node) {
		/*
		 * Skip kmem caches that don't have any memcg children.
		 */
		if (list_empty(&s->memcg_params.children))
			continue;

		memset(&sinfo, 0, sizeof(sinfo));
		get_slabinfo(s, &sinfo);
		seq_printf(m, "%-17s root       %6lu %6lu %6lu %6lu\n",
			   cache_name(s), sinfo.active_objs, sinfo.num_objs,
			   sinfo.active_slabs, sinfo.num_slabs);

		for_each_memcg_cache(c, s) {
			struct cgroup_subsys_state *css;
			char *status = "";

			css = &c->memcg_params.memcg->css;
			if (!(css->flags & CSS_ONLINE))
				status = ":dead";
			else if (c->flags & SLAB_DEACTIVATED)
				status = ":deact";

			memset(&sinfo, 0, sizeof(sinfo));
			get_slabinfo(c, &sinfo);
			seq_printf(m, "%-17s %4d%-6s %6lu %6lu %6lu %6lu\n",
				   cache_name(c), css->id, status,
				   sinfo.active_objs, sinfo.num_objs,
				   sinfo.active_slabs, sinfo.num_slabs);
		}
	}
	mutex_unlock(&slab_mutex);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(memcg_slabinfo);

static int __init memcg_slabinfo_init(void)
{
	debugfs_create_file("memcg_slabinfo", S_IFREG | S_IRUGO,
			    NULL, NULL, &memcg_slabinfo_fops);
	return 0;
}

late_initcall(memcg_slabinfo_init);
#endif /* CONFIG_DEBUG_FS && CONFIG_MEMCG_KMEM */
#endif /* CONFIG_SLAB || CONFIG_SLUB_DEBUG */

static __always_inline void *__do_krealloc(const void *p, size_t new_size,
					   gfp_t flags)
{
	void *ret;
	size_t ks = 0;

	if (p)
		ks = ksize(p);

	if (ks >= new_size) {
		p = kasan_krealloc((void *)p, new_size, flags);
		return (void *)p;
	}

	ret = kmalloc_track_caller(new_size, flags);
	if (ret && p)
		memcpy(ret, p, ks);

	return ret;
}

/**
 * krealloc - reallocate memory. The contents will remain unchanged.
 * @p: object to reallocate memory for.
 * @new_size: how many bytes of memory are required.
 * @flags: the type of memory to allocate.
 *
 * The contents of the object pointed to are preserved up to the
 * lesser of the new and old sizes.  If @p is %NULL, krealloc()
 * behaves exactly like kmalloc().  If @new_size is 0 and @p is not a
 * %NULL pointer, the object pointed to is freed.
 *
 * Return: pointer to the allocated memory or %NULL in case of error
 */
void *krealloc(const void *p, size_t new_size, gfp_t flags)
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
EXPORT_SYMBOL(krealloc);

/**
 * kzfree - like kfree but zero memory
 * @p: object to free memory of
 *
 * The memory of the object @p points to is zeroed before freed.
 * If @p is %NULL, kzfree() does nothing.
 *
 * Note: this function zeroes the whole allocated buffer which can be a good
 * deal bigger than the requested buffer size passed to kmalloc(). So be
 * careful when using this function in performance sensitive code.
 */
void kzfree(const void *p)
{
	size_t ks;
	void *mem = (void *)p;

	if (unlikely(ZERO_OR_NULL_PTR(mem)))
		return;
	ks = ksize(mem);
	memset(mem, 0, ks);
	kfree(mem);
}
EXPORT_SYMBOL(kzfree);

/**
 * ksize - get the actual amount of memory allocated for a given object
 * @objp: Pointer to the object
 *
 * kmalloc may internally round up allocations and return more memory
 * than requested. ksize() can be used to determine the actual amount of
 * memory allocated. The caller may use this additional memory, even though
 * a smaller amount of memory was initially specified with the kmalloc call.
 * The caller must guarantee that objp points to a valid object previously
 * allocated with either kmalloc() or kmem_cache_alloc(). The object
 * must not be freed during the duration of the call.
 *
 * Return: size of the actual memory used by @objp in bytes
 */
size_t ksize(const void *objp)
{
	size_t size;

	if (WARN_ON_ONCE(!objp))
		return 0;
	/*
	 * We need to check that the pointed to object is valid, and only then
	 * unpoison the shadow memory below. We use __kasan_check_read(), to
	 * generate a more useful report at the time ksize() is called (rather
	 * than later where behaviour is undefined due to potential
	 * use-after-free or double-free).
	 *
	 * If the pointed to memory is invalid we return 0, to avoid users of
	 * ksize() writing to and potentially corrupting the memory region.
	 *
	 * We want to perform the check before __ksize(), to avoid potentially
	 * crashing in __ksize() due to accessing invalid metadata.
	 */
	if (unlikely(objp == ZERO_SIZE_PTR) || !__kasan_check_read(objp, 1))
		return 0;

	size = __ksize(objp);
	/*
	 * We assume that ksize callers could use whole allocated area,
	 * so we need to unpoison this area.
	 */
	kasan_unpoison_shadow(objp, size);
	return size;
}
EXPORT_SYMBOL(ksize);

/* Tracepoints definitions. */
EXPORT_TRACEPOINT_SYMBOL(kmalloc);
EXPORT_TRACEPOINT_SYMBOL(kmem_cache_alloc);
EXPORT_TRACEPOINT_SYMBOL(kmalloc_node);
EXPORT_TRACEPOINT_SYMBOL(kmem_cache_alloc_node);
EXPORT_TRACEPOINT_SYMBOL(kfree);
EXPORT_TRACEPOINT_SYMBOL(kmem_cache_free);

int should_failslab(struct kmem_cache *s, gfp_t gfpflags)
{
	if (__should_failslab(s, gfpflags))
		return -ENOMEM;
	return 0;
}
ALLOW_ERROR_INJECTION(should_failslab, ERRNO);
