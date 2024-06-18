/*
 * zsmalloc memory allocator
 *
 * Copyright (C) 2011  Nitin Gupta
 * Copyright (C) 2012, 2013 Minchan Kim
 *
 * This code is released using a dual license strategy: BSD/GPL
 * You can choose the license that better fits your requirements.
 *
 * Released under the terms of 3-clause BSD License
 * Released under the terms of GNU General Public License Version 2.0
 */

/*
 * Following is how we use various fields and flags of underlying
 * struct page(s) to form a zspage.
 *
 * Usage of struct page fields:
 *	page->private: points to zspage
 *	page->index: links together all component pages of a zspage
 *		For the huge page, this is always 0, so we use this field
 *		to store handle.
 *	page->page_type: first object offset in a subpage of zspage
 *
 * Usage of struct page flags:
 *	PG_private: identifies the first component page
 *	PG_owner_priv_1: identifies the huge component page
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

/*
 * lock ordering:
 *	page_lock
 *	pool->lock
 *	zspage->lock
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/highmem.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/pgtable.h>
#include <asm/tlbflush.h>
#include <linux/cpumask.h>
#include <linux/cpu.h>
#include <linux/vmalloc.h>
#include <linux/preempt.h>
#include <linux/spinlock.h>
#include <linux/shrinker.h>
#include <linux/types.h>
#include <linux/debugfs.h>
#include <linux/zsmalloc.h>
#include <linux/zpool.h>
#include <linux/migrate.h>
#include <linux/wait.h>
#include <linux/pagemap.h>
#include <linux/fs.h>
#include <linux/local_lock.h>

#define ZSPAGE_MAGIC	0x58

/*
 * This must be power of 2 and greater than or equal to sizeof(link_free).
 * These two conditions ensure that any 'struct link_free' itself doesn't
 * span more than 1 page which avoids complex case of mapping 2 pages simply
 * to restore link_free pointer values.
 */
#define ZS_ALIGN		8

#define ZS_HANDLE_SIZE (sizeof(unsigned long))

/*
 * Object location (<PFN>, <obj_idx>) is encoded as
 * a single (unsigned long) handle value.
 *
 * Note that object index <obj_idx> starts from 0.
 *
 * This is made more complicated by various memory models and PAE.
 */

#ifndef MAX_POSSIBLE_PHYSMEM_BITS
#ifdef MAX_PHYSMEM_BITS
#define MAX_POSSIBLE_PHYSMEM_BITS MAX_PHYSMEM_BITS
#else
/*
 * If this definition of MAX_PHYSMEM_BITS is used, OBJ_INDEX_BITS will just
 * be PAGE_SHIFT
 */
#define MAX_POSSIBLE_PHYSMEM_BITS BITS_PER_LONG
#endif
#endif

#define _PFN_BITS		(MAX_POSSIBLE_PHYSMEM_BITS - PAGE_SHIFT)

/*
 * Head in allocated object should have OBJ_ALLOCATED_TAG
 * to identify the object was allocated or not.
 * It's okay to add the status bit in the least bit because
 * header keeps handle which is 4byte-aligned address so we
 * have room for two bit at least.
 */
#define OBJ_ALLOCATED_TAG 1

#define OBJ_TAG_BITS	1
#define OBJ_TAG_MASK	OBJ_ALLOCATED_TAG

#define OBJ_INDEX_BITS	(BITS_PER_LONG - _PFN_BITS)
#define OBJ_INDEX_MASK	((_AC(1, UL) << OBJ_INDEX_BITS) - 1)

#define HUGE_BITS	1
#define FULLNESS_BITS	4
#define CLASS_BITS	8
#define MAGIC_VAL_BITS	8

#define MAX(a, b) ((a) >= (b) ? (a) : (b))

#define ZS_MAX_PAGES_PER_ZSPAGE	(_AC(CONFIG_ZSMALLOC_CHAIN_SIZE, UL))

/* ZS_MIN_ALLOC_SIZE must be multiple of ZS_ALIGN */
#define ZS_MIN_ALLOC_SIZE \
	MAX(32, (ZS_MAX_PAGES_PER_ZSPAGE << PAGE_SHIFT >> OBJ_INDEX_BITS))
/* each chunk includes extra space to keep handle */
#define ZS_MAX_ALLOC_SIZE	PAGE_SIZE

/*
 * On systems with 4K page size, this gives 255 size classes! There is a
 * trader-off here:
 *  - Large number of size classes is potentially wasteful as free page are
 *    spread across these classes
 *  - Small number of size classes causes large internal fragmentation
 *  - Probably its better to use specific size classes (empirically
 *    determined). NOTE: all those class sizes must be set as multiple of
 *    ZS_ALIGN to make sure link_free itself never has to span 2 pages.
 *
 *  ZS_MIN_ALLOC_SIZE and ZS_SIZE_CLASS_DELTA must be multiple of ZS_ALIGN
 *  (reason above)
 */
#define ZS_SIZE_CLASS_DELTA	(PAGE_SIZE >> CLASS_BITS)
#define ZS_SIZE_CLASSES	(DIV_ROUND_UP(ZS_MAX_ALLOC_SIZE - ZS_MIN_ALLOC_SIZE, \
				      ZS_SIZE_CLASS_DELTA) + 1)

/*
 * Pages are distinguished by the ratio of used memory (that is the ratio
 * of ->inuse objects to all objects that page can store). For example,
 * INUSE_RATIO_10 means that the ratio of used objects is > 0% and <= 10%.
 *
 * The number of fullness groups is not random. It allows us to keep
 * difference between the least busy page in the group (minimum permitted
 * number of ->inuse objects) and the most busy page (maximum permitted
 * number of ->inuse objects) at a reasonable value.
 */
enum fullness_group {
	ZS_INUSE_RATIO_0,
	ZS_INUSE_RATIO_10,
	/* NOTE: 8 more fullness groups here */
	ZS_INUSE_RATIO_99       = 10,
	ZS_INUSE_RATIO_100,
	NR_FULLNESS_GROUPS,
};

enum class_stat_type {
	/* NOTE: stats for 12 fullness groups here: from inuse 0 to 100 */
	ZS_OBJS_ALLOCATED       = NR_FULLNESS_GROUPS,
	ZS_OBJS_INUSE,
	NR_CLASS_STAT_TYPES,
};

struct zs_size_stat {
	unsigned long objs[NR_CLASS_STAT_TYPES];
};

#ifdef CONFIG_ZSMALLOC_STAT
static struct dentry *zs_stat_root;
#endif

static size_t huge_class_size;

struct size_class {
	struct list_head fullness_list[NR_FULLNESS_GROUPS];
	/*
	 * Size of objects stored in this class. Must be multiple
	 * of ZS_ALIGN.
	 */
	int size;
	int objs_per_zspage;
	/* Number of PAGE_SIZE sized pages to combine to form a 'zspage' */
	int pages_per_zspage;

	unsigned int index;
	struct zs_size_stat stats;
};

/*
 * Placed within free objects to form a singly linked list.
 * For every zspage, zspage->freeobj gives head of this list.
 *
 * This must be power of 2 and less than or equal to ZS_ALIGN
 */
struct link_free {
	union {
		/*
		 * Free object index;
		 * It's valid for non-allocated object
		 */
		unsigned long next;
		/*
		 * Handle of allocated object.
		 */
		unsigned long handle;
	};
};

struct zs_pool {
	const char *name;

	struct size_class *size_class[ZS_SIZE_CLASSES];
	struct kmem_cache *handle_cachep;
	struct kmem_cache *zspage_cachep;

	atomic_long_t pages_allocated;

	struct zs_pool_stats stats;

	/* Compact classes */
	struct shrinker *shrinker;

#ifdef CONFIG_ZSMALLOC_STAT
	struct dentry *stat_dentry;
#endif
#ifdef CONFIG_COMPACTION
	struct work_struct free_work;
#endif
	spinlock_t lock;
	atomic_t compaction_in_progress;
};

struct zspage {
	struct {
		unsigned int huge:HUGE_BITS;
		unsigned int fullness:FULLNESS_BITS;
		unsigned int class:CLASS_BITS + 1;
		unsigned int magic:MAGIC_VAL_BITS;
	};
	unsigned int inuse;
	unsigned int freeobj;
	struct page *first_page;
	struct list_head list; /* fullness list */
	struct zs_pool *pool;
	rwlock_t lock;
};

struct mapping_area {
	local_lock_t lock;
	char *vm_buf; /* copy buffer for objects that span pages */
	char *vm_addr; /* address of kmap_atomic()'ed pages */
	enum zs_mapmode vm_mm; /* mapping mode */
};

/* huge object: pages_per_zspage == 1 && maxobj_per_zspage == 1 */
static void SetZsHugePage(struct zspage *zspage)
{
	zspage->huge = 1;
}

static bool ZsHugePage(struct zspage *zspage)
{
	return zspage->huge;
}

static void migrate_lock_init(struct zspage *zspage);
static void migrate_read_lock(struct zspage *zspage);
static void migrate_read_unlock(struct zspage *zspage);
static void migrate_write_lock(struct zspage *zspage);
static void migrate_write_unlock(struct zspage *zspage);

#ifdef CONFIG_COMPACTION
static void kick_deferred_free(struct zs_pool *pool);
static void init_deferred_free(struct zs_pool *pool);
static void SetZsPageMovable(struct zs_pool *pool, struct zspage *zspage);
#else
static void kick_deferred_free(struct zs_pool *pool) {}
static void init_deferred_free(struct zs_pool *pool) {}
static void SetZsPageMovable(struct zs_pool *pool, struct zspage *zspage) {}
#endif

static int create_cache(struct zs_pool *pool)
{
	pool->handle_cachep = kmem_cache_create("zs_handle", ZS_HANDLE_SIZE,
					0, 0, NULL);
	if (!pool->handle_cachep)
		return 1;

	pool->zspage_cachep = kmem_cache_create("zspage", sizeof(struct zspage),
					0, 0, NULL);
	if (!pool->zspage_cachep) {
		kmem_cache_destroy(pool->handle_cachep);
		pool->handle_cachep = NULL;
		return 1;
	}

	return 0;
}

static void destroy_cache(struct zs_pool *pool)
{
	kmem_cache_destroy(pool->handle_cachep);
	kmem_cache_destroy(pool->zspage_cachep);
}

static unsigned long cache_alloc_handle(struct zs_pool *pool, gfp_t gfp)
{
	return (unsigned long)kmem_cache_alloc(pool->handle_cachep,
			gfp & ~(__GFP_HIGHMEM|__GFP_MOVABLE));
}

static void cache_free_handle(struct zs_pool *pool, unsigned long handle)
{
	kmem_cache_free(pool->handle_cachep, (void *)handle);
}

static struct zspage *cache_alloc_zspage(struct zs_pool *pool, gfp_t flags)
{
	return kmem_cache_zalloc(pool->zspage_cachep,
			flags & ~(__GFP_HIGHMEM|__GFP_MOVABLE));
}

static void cache_free_zspage(struct zs_pool *pool, struct zspage *zspage)
{
	kmem_cache_free(pool->zspage_cachep, zspage);
}

/* pool->lock(which owns the handle) synchronizes races */
static void record_obj(unsigned long handle, unsigned long obj)
{
	*(unsigned long *)handle = obj;
}

/* zpool driver */

#ifdef CONFIG_ZPOOL

static void *zs_zpool_create(const char *name, gfp_t gfp)
{
	/*
	 * Ignore global gfp flags: zs_malloc() may be invoked from
	 * different contexts and its caller must provide a valid
	 * gfp mask.
	 */
	return zs_create_pool(name);
}

static void zs_zpool_destroy(void *pool)
{
	zs_destroy_pool(pool);
}

static int zs_zpool_malloc(void *pool, size_t size, gfp_t gfp,
			unsigned long *handle)
{
	*handle = zs_malloc(pool, size, gfp);

	if (IS_ERR_VALUE(*handle))
		return PTR_ERR((void *)*handle);
	return 0;
}
static void zs_zpool_free(void *pool, unsigned long handle)
{
	zs_free(pool, handle);
}

static void *zs_zpool_map(void *pool, unsigned long handle,
			enum zpool_mapmode mm)
{
	enum zs_mapmode zs_mm;

	switch (mm) {
	case ZPOOL_MM_RO:
		zs_mm = ZS_MM_RO;
		break;
	case ZPOOL_MM_WO:
		zs_mm = ZS_MM_WO;
		break;
	case ZPOOL_MM_RW:
	default:
		zs_mm = ZS_MM_RW;
		break;
	}

	return zs_map_object(pool, handle, zs_mm);
}
static void zs_zpool_unmap(void *pool, unsigned long handle)
{
	zs_unmap_object(pool, handle);
}

static u64 zs_zpool_total_pages(void *pool)
{
	return zs_get_total_pages(pool);
}

static struct zpool_driver zs_zpool_driver = {
	.type =			  "zsmalloc",
	.owner =		  THIS_MODULE,
	.create =		  zs_zpool_create,
	.destroy =		  zs_zpool_destroy,
	.malloc_support_movable = true,
	.malloc =		  zs_zpool_malloc,
	.free =			  zs_zpool_free,
	.map =			  zs_zpool_map,
	.unmap =		  zs_zpool_unmap,
	.total_pages =		  zs_zpool_total_pages,
};

MODULE_ALIAS("zpool-zsmalloc");
#endif /* CONFIG_ZPOOL */

/* per-cpu VM mapping areas for zspage accesses that cross page boundaries */
static DEFINE_PER_CPU(struct mapping_area, zs_map_area) = {
	.lock	= INIT_LOCAL_LOCK(lock),
};

static __maybe_unused int is_first_page(struct page *page)
{
	return PagePrivate(page);
}

/* Protected by pool->lock */
static inline int get_zspage_inuse(struct zspage *zspage)
{
	return zspage->inuse;
}


static inline void mod_zspage_inuse(struct zspage *zspage, int val)
{
	zspage->inuse += val;
}

static inline struct page *get_first_page(struct zspage *zspage)
{
	struct page *first_page = zspage->first_page;

	VM_BUG_ON_PAGE(!is_first_page(first_page), first_page);
	return first_page;
}

static inline unsigned int get_first_obj_offset(struct page *page)
{
	return page->page_type;
}

static inline void set_first_obj_offset(struct page *page, unsigned int offset)
{
	page->page_type = offset;
}

static inline unsigned int get_freeobj(struct zspage *zspage)
{
	return zspage->freeobj;
}

static inline void set_freeobj(struct zspage *zspage, unsigned int obj)
{
	zspage->freeobj = obj;
}

static struct size_class *zspage_class(struct zs_pool *pool,
				       struct zspage *zspage)
{
	return pool->size_class[zspage->class];
}

/*
 * zsmalloc divides the pool into various size classes where each
 * class maintains a list of zspages where each zspage is divided
 * into equal sized chunks. Each allocation falls into one of these
 * classes depending on its size. This function returns index of the
 * size class which has chunk size big enough to hold the given size.
 */
static int get_size_class_index(int size)
{
	int idx = 0;

	if (likely(size > ZS_MIN_ALLOC_SIZE))
		idx = DIV_ROUND_UP(size - ZS_MIN_ALLOC_SIZE,
				ZS_SIZE_CLASS_DELTA);

	return min_t(int, ZS_SIZE_CLASSES - 1, idx);
}

static inline void class_stat_inc(struct size_class *class,
				int type, unsigned long cnt)
{
	class->stats.objs[type] += cnt;
}

static inline void class_stat_dec(struct size_class *class,
				int type, unsigned long cnt)
{
	class->stats.objs[type] -= cnt;
}

static inline unsigned long zs_stat_get(struct size_class *class, int type)
{
	return class->stats.objs[type];
}

#ifdef CONFIG_ZSMALLOC_STAT

static void __init zs_stat_init(void)
{
	if (!debugfs_initialized()) {
		pr_warn("debugfs not available, stat dir not created\n");
		return;
	}

	zs_stat_root = debugfs_create_dir("zsmalloc", NULL);
}

static void __exit zs_stat_exit(void)
{
	debugfs_remove_recursive(zs_stat_root);
}

static unsigned long zs_can_compact(struct size_class *class);

static int zs_stats_size_show(struct seq_file *s, void *v)
{
	int i, fg;
	struct zs_pool *pool = s->private;
	struct size_class *class;
	int objs_per_zspage;
	unsigned long obj_allocated, obj_used, pages_used, freeable;
	unsigned long total_objs = 0, total_used_objs = 0, total_pages = 0;
	unsigned long total_freeable = 0;
	unsigned long inuse_totals[NR_FULLNESS_GROUPS] = {0, };

	seq_printf(s, " %5s %5s %9s %9s %9s %9s %9s %9s %9s %9s %9s %9s %9s %13s %10s %10s %16s %8s\n",
			"class", "size", "10%", "20%", "30%", "40%",
			"50%", "60%", "70%", "80%", "90%", "99%", "100%",
			"obj_allocated", "obj_used", "pages_used",
			"pages_per_zspage", "freeable");

	for (i = 0; i < ZS_SIZE_CLASSES; i++) {

		class = pool->size_class[i];

		if (class->index != i)
			continue;

		spin_lock(&pool->lock);

		seq_printf(s, " %5u %5u ", i, class->size);
		for (fg = ZS_INUSE_RATIO_10; fg < NR_FULLNESS_GROUPS; fg++) {
			inuse_totals[fg] += zs_stat_get(class, fg);
			seq_printf(s, "%9lu ", zs_stat_get(class, fg));
		}

		obj_allocated = zs_stat_get(class, ZS_OBJS_ALLOCATED);
		obj_used = zs_stat_get(class, ZS_OBJS_INUSE);
		freeable = zs_can_compact(class);
		spin_unlock(&pool->lock);

		objs_per_zspage = class->objs_per_zspage;
		pages_used = obj_allocated / objs_per_zspage *
				class->pages_per_zspage;

		seq_printf(s, "%13lu %10lu %10lu %16d %8lu\n",
			   obj_allocated, obj_used, pages_used,
			   class->pages_per_zspage, freeable);

		total_objs += obj_allocated;
		total_used_objs += obj_used;
		total_pages += pages_used;
		total_freeable += freeable;
	}

	seq_puts(s, "\n");
	seq_printf(s, " %5s %5s ", "Total", "");

	for (fg = ZS_INUSE_RATIO_10; fg < NR_FULLNESS_GROUPS; fg++)
		seq_printf(s, "%9lu ", inuse_totals[fg]);

	seq_printf(s, "%13lu %10lu %10lu %16s %8lu\n",
		   total_objs, total_used_objs, total_pages, "",
		   total_freeable);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(zs_stats_size);

static void zs_pool_stat_create(struct zs_pool *pool, const char *name)
{
	if (!zs_stat_root) {
		pr_warn("no root stat dir, not creating <%s> stat dir\n", name);
		return;
	}

	pool->stat_dentry = debugfs_create_dir(name, zs_stat_root);

	debugfs_create_file("classes", S_IFREG | 0444, pool->stat_dentry, pool,
			    &zs_stats_size_fops);
}

static void zs_pool_stat_destroy(struct zs_pool *pool)
{
	debugfs_remove_recursive(pool->stat_dentry);
}

#else /* CONFIG_ZSMALLOC_STAT */
static void __init zs_stat_init(void)
{
}

static void __exit zs_stat_exit(void)
{
}

static inline void zs_pool_stat_create(struct zs_pool *pool, const char *name)
{
}

static inline void zs_pool_stat_destroy(struct zs_pool *pool)
{
}
#endif


/*
 * For each size class, zspages are divided into different groups
 * depending on their usage ratio. This function returns fullness
 * status of the given page.
 */
static int get_fullness_group(struct size_class *class, struct zspage *zspage)
{
	int inuse, objs_per_zspage, ratio;

	inuse = get_zspage_inuse(zspage);
	objs_per_zspage = class->objs_per_zspage;

	if (inuse == 0)
		return ZS_INUSE_RATIO_0;
	if (inuse == objs_per_zspage)
		return ZS_INUSE_RATIO_100;

	ratio = 100 * inuse / objs_per_zspage;
	/*
	 * Take integer division into consideration: a page with one inuse
	 * object out of 127 possible, will end up having 0 usage ratio,
	 * which is wrong as it belongs in ZS_INUSE_RATIO_10 fullness group.
	 */
	return ratio / 10 + 1;
}

/*
 * Each size class maintains various freelists and zspages are assigned
 * to one of these freelists based on the number of live objects they
 * have. This functions inserts the given zspage into the freelist
 * identified by <class, fullness_group>.
 */
static void insert_zspage(struct size_class *class,
				struct zspage *zspage,
				int fullness)
{
	class_stat_inc(class, fullness, 1);
	list_add(&zspage->list, &class->fullness_list[fullness]);
	zspage->fullness = fullness;
}

/*
 * This function removes the given zspage from the freelist identified
 * by <class, fullness_group>.
 */
static void remove_zspage(struct size_class *class, struct zspage *zspage)
{
	int fullness = zspage->fullness;

	VM_BUG_ON(list_empty(&class->fullness_list[fullness]));

	list_del_init(&zspage->list);
	class_stat_dec(class, fullness, 1);
}

/*
 * Each size class maintains zspages in different fullness groups depending
 * on the number of live objects they contain. When allocating or freeing
 * objects, the fullness status of the page can change, for instance, from
 * INUSE_RATIO_80 to INUSE_RATIO_70 when freeing an object. This function
 * checks if such a status change has occurred for the given page and
 * accordingly moves the page from the list of the old fullness group to that
 * of the new fullness group.
 */
static int fix_fullness_group(struct size_class *class, struct zspage *zspage)
{
	int newfg;

	newfg = get_fullness_group(class, zspage);
	if (newfg == zspage->fullness)
		goto out;

	remove_zspage(class, zspage);
	insert_zspage(class, zspage, newfg);
out:
	return newfg;
}

static struct zspage *get_zspage(struct page *page)
{
	struct zspage *zspage = (struct zspage *)page_private(page);

	BUG_ON(zspage->magic != ZSPAGE_MAGIC);
	return zspage;
}

static struct page *get_next_page(struct page *page)
{
	struct zspage *zspage = get_zspage(page);

	if (unlikely(ZsHugePage(zspage)))
		return NULL;

	return (struct page *)page->index;
}

/**
 * obj_to_location - get (<page>, <obj_idx>) from encoded object value
 * @obj: the encoded object value
 * @page: page object resides in zspage
 * @obj_idx: object index
 */
static void obj_to_location(unsigned long obj, struct page **page,
				unsigned int *obj_idx)
{
	*page = pfn_to_page(obj >> OBJ_INDEX_BITS);
	*obj_idx = (obj & OBJ_INDEX_MASK);
}

static void obj_to_page(unsigned long obj, struct page **page)
{
	*page = pfn_to_page(obj >> OBJ_INDEX_BITS);
}

/**
 * location_to_obj - get obj value encoded from (<page>, <obj_idx>)
 * @page: page object resides in zspage
 * @obj_idx: object index
 */
static unsigned long location_to_obj(struct page *page, unsigned int obj_idx)
{
	unsigned long obj;

	obj = page_to_pfn(page) << OBJ_INDEX_BITS;
	obj |= obj_idx & OBJ_INDEX_MASK;

	return obj;
}

static unsigned long handle_to_obj(unsigned long handle)
{
	return *(unsigned long *)handle;
}

static inline bool obj_allocated(struct page *page, void *obj,
				 unsigned long *phandle)
{
	unsigned long handle;
	struct zspage *zspage = get_zspage(page);

	if (unlikely(ZsHugePage(zspage))) {
		VM_BUG_ON_PAGE(!is_first_page(page), page);
		handle = page->index;
	} else
		handle = *(unsigned long *)obj;

	if (!(handle & OBJ_ALLOCATED_TAG))
		return false;

	/* Clear all tags before returning the handle */
	*phandle = handle & ~OBJ_TAG_MASK;
	return true;
}

static void reset_page(struct page *page)
{
	__ClearPageMovable(page);
	ClearPagePrivate(page);
	set_page_private(page, 0);
	page_mapcount_reset(page);
	page->index = 0;
}

static int trylock_zspage(struct zspage *zspage)
{
	struct page *cursor, *fail;

	for (cursor = get_first_page(zspage); cursor != NULL; cursor =
					get_next_page(cursor)) {
		if (!trylock_page(cursor)) {
			fail = cursor;
			goto unlock;
		}
	}

	return 1;
unlock:
	for (cursor = get_first_page(zspage); cursor != fail; cursor =
					get_next_page(cursor))
		unlock_page(cursor);

	return 0;
}

static void __free_zspage(struct zs_pool *pool, struct size_class *class,
				struct zspage *zspage)
{
	struct page *page, *next;

	assert_spin_locked(&pool->lock);

	VM_BUG_ON(get_zspage_inuse(zspage));
	VM_BUG_ON(zspage->fullness != ZS_INUSE_RATIO_0);

	next = page = get_first_page(zspage);
	do {
		VM_BUG_ON_PAGE(!PageLocked(page), page);
		next = get_next_page(page);
		reset_page(page);
		unlock_page(page);
		dec_zone_page_state(page, NR_ZSPAGES);
		put_page(page);
		page = next;
	} while (page != NULL);

	cache_free_zspage(pool, zspage);

	class_stat_dec(class, ZS_OBJS_ALLOCATED, class->objs_per_zspage);
	atomic_long_sub(class->pages_per_zspage, &pool->pages_allocated);
}

static void free_zspage(struct zs_pool *pool, struct size_class *class,
				struct zspage *zspage)
{
	VM_BUG_ON(get_zspage_inuse(zspage));
	VM_BUG_ON(list_empty(&zspage->list));

	/*
	 * Since zs_free couldn't be sleepable, this function cannot call
	 * lock_page. The page locks trylock_zspage got will be released
	 * by __free_zspage.
	 */
	if (!trylock_zspage(zspage)) {
		kick_deferred_free(pool);
		return;
	}

	remove_zspage(class, zspage);
	__free_zspage(pool, class, zspage);
}

/* Initialize a newly allocated zspage */
static void init_zspage(struct size_class *class, struct zspage *zspage)
{
	unsigned int freeobj = 1;
	unsigned long off = 0;
	struct page *page = get_first_page(zspage);

	while (page) {
		struct page *next_page;
		struct link_free *link;
		void *vaddr;

		set_first_obj_offset(page, off);

		vaddr = kmap_atomic(page);
		link = (struct link_free *)vaddr + off / sizeof(*link);

		while ((off += class->size) < PAGE_SIZE) {
			link->next = freeobj++ << OBJ_TAG_BITS;
			link += class->size / sizeof(*link);
		}

		/*
		 * We now come to the last (full or partial) object on this
		 * page, which must point to the first object on the next
		 * page (if present)
		 */
		next_page = get_next_page(page);
		if (next_page) {
			link->next = freeobj++ << OBJ_TAG_BITS;
		} else {
			/*
			 * Reset OBJ_TAG_BITS bit to last link to tell
			 * whether it's allocated object or not.
			 */
			link->next = -1UL << OBJ_TAG_BITS;
		}
		kunmap_atomic(vaddr);
		page = next_page;
		off %= PAGE_SIZE;
	}

	set_freeobj(zspage, 0);
}

static void create_page_chain(struct size_class *class, struct zspage *zspage,
				struct page *pages[])
{
	int i;
	struct page *page;
	struct page *prev_page = NULL;
	int nr_pages = class->pages_per_zspage;

	/*
	 * Allocate individual pages and link them together as:
	 * 1. all pages are linked together using page->index
	 * 2. each sub-page point to zspage using page->private
	 *
	 * we set PG_private to identify the first page (i.e. no other sub-page
	 * has this flag set).
	 */
	for (i = 0; i < nr_pages; i++) {
		page = pages[i];
		set_page_private(page, (unsigned long)zspage);
		page->index = 0;
		if (i == 0) {
			zspage->first_page = page;
			SetPagePrivate(page);
			if (unlikely(class->objs_per_zspage == 1 &&
					class->pages_per_zspage == 1))
				SetZsHugePage(zspage);
		} else {
			prev_page->index = (unsigned long)page;
		}
		prev_page = page;
	}
}

/*
 * Allocate a zspage for the given size class
 */
static struct zspage *alloc_zspage(struct zs_pool *pool,
					struct size_class *class,
					gfp_t gfp)
{
	int i;
	struct page *pages[ZS_MAX_PAGES_PER_ZSPAGE];
	struct zspage *zspage = cache_alloc_zspage(pool, gfp);

	if (!zspage)
		return NULL;

	zspage->magic = ZSPAGE_MAGIC;
	migrate_lock_init(zspage);

	for (i = 0; i < class->pages_per_zspage; i++) {
		struct page *page;

		page = alloc_page(gfp);
		if (!page) {
			while (--i >= 0) {
				dec_zone_page_state(pages[i], NR_ZSPAGES);
				__free_page(pages[i]);
			}
			cache_free_zspage(pool, zspage);
			return NULL;
		}

		inc_zone_page_state(page, NR_ZSPAGES);
		pages[i] = page;
	}

	create_page_chain(class, zspage, pages);
	init_zspage(class, zspage);
	zspage->pool = pool;
	zspage->class = class->index;

	return zspage;
}

static struct zspage *find_get_zspage(struct size_class *class)
{
	int i;
	struct zspage *zspage;

	for (i = ZS_INUSE_RATIO_99; i >= ZS_INUSE_RATIO_0; i--) {
		zspage = list_first_entry_or_null(&class->fullness_list[i],
						  struct zspage, list);
		if (zspage)
			break;
	}

	return zspage;
}

static inline int __zs_cpu_up(struct mapping_area *area)
{
	/*
	 * Make sure we don't leak memory if a cpu UP notification
	 * and zs_init() race and both call zs_cpu_up() on the same cpu
	 */
	if (area->vm_buf)
		return 0;
	area->vm_buf = kmalloc(ZS_MAX_ALLOC_SIZE, GFP_KERNEL);
	if (!area->vm_buf)
		return -ENOMEM;
	return 0;
}

static inline void __zs_cpu_down(struct mapping_area *area)
{
	kfree(area->vm_buf);
	area->vm_buf = NULL;
}

static void *__zs_map_object(struct mapping_area *area,
			struct page *pages[2], int off, int size)
{
	int sizes[2];
	void *addr;
	char *buf = area->vm_buf;

	/* disable page faults to match kmap_atomic() return conditions */
	pagefault_disable();

	/* no read fastpath */
	if (area->vm_mm == ZS_MM_WO)
		goto out;

	sizes[0] = PAGE_SIZE - off;
	sizes[1] = size - sizes[0];

	/* copy object to per-cpu buffer */
	addr = kmap_atomic(pages[0]);
	memcpy(buf, addr + off, sizes[0]);
	kunmap_atomic(addr);
	addr = kmap_atomic(pages[1]);
	memcpy(buf + sizes[0], addr, sizes[1]);
	kunmap_atomic(addr);
out:
	return area->vm_buf;
}

static void __zs_unmap_object(struct mapping_area *area,
			struct page *pages[2], int off, int size)
{
	int sizes[2];
	void *addr;
	char *buf;

	/* no write fastpath */
	if (area->vm_mm == ZS_MM_RO)
		goto out;

	buf = area->vm_buf;
	buf = buf + ZS_HANDLE_SIZE;
	size -= ZS_HANDLE_SIZE;
	off += ZS_HANDLE_SIZE;

	sizes[0] = PAGE_SIZE - off;
	sizes[1] = size - sizes[0];

	/* copy per-cpu buffer to object */
	addr = kmap_atomic(pages[0]);
	memcpy(addr + off, buf, sizes[0]);
	kunmap_atomic(addr);
	addr = kmap_atomic(pages[1]);
	memcpy(addr, buf + sizes[0], sizes[1]);
	kunmap_atomic(addr);

out:
	/* enable page faults to match kunmap_atomic() return conditions */
	pagefault_enable();
}

static int zs_cpu_prepare(unsigned int cpu)
{
	struct mapping_area *area;

	area = &per_cpu(zs_map_area, cpu);
	return __zs_cpu_up(area);
}

static int zs_cpu_dead(unsigned int cpu)
{
	struct mapping_area *area;

	area = &per_cpu(zs_map_area, cpu);
	__zs_cpu_down(area);
	return 0;
}

static bool can_merge(struct size_class *prev, int pages_per_zspage,
					int objs_per_zspage)
{
	if (prev->pages_per_zspage == pages_per_zspage &&
		prev->objs_per_zspage == objs_per_zspage)
		return true;

	return false;
}

static bool zspage_full(struct size_class *class, struct zspage *zspage)
{
	return get_zspage_inuse(zspage) == class->objs_per_zspage;
}

static bool zspage_empty(struct zspage *zspage)
{
	return get_zspage_inuse(zspage) == 0;
}

/**
 * zs_lookup_class_index() - Returns index of the zsmalloc &size_class
 * that hold objects of the provided size.
 * @pool: zsmalloc pool to use
 * @size: object size
 *
 * Context: Any context.
 *
 * Return: the index of the zsmalloc &size_class that hold objects of the
 * provided size.
 */
unsigned int zs_lookup_class_index(struct zs_pool *pool, unsigned int size)
{
	struct size_class *class;

	class = pool->size_class[get_size_class_index(size)];

	return class->index;
}
EXPORT_SYMBOL_GPL(zs_lookup_class_index);

unsigned long zs_get_total_pages(struct zs_pool *pool)
{
	return atomic_long_read(&pool->pages_allocated);
}
EXPORT_SYMBOL_GPL(zs_get_total_pages);

/**
 * zs_map_object - get address of allocated object from handle.
 * @pool: pool from which the object was allocated
 * @handle: handle returned from zs_malloc
 * @mm: mapping mode to use
 *
 * Before using an object allocated from zs_malloc, it must be mapped using
 * this function. When done with the object, it must be unmapped using
 * zs_unmap_object.
 *
 * Only one object can be mapped per cpu at a time. There is no protection
 * against nested mappings.
 *
 * This function returns with preemption and page faults disabled.
 */
void *zs_map_object(struct zs_pool *pool, unsigned long handle,
			enum zs_mapmode mm)
{
	struct zspage *zspage;
	struct page *page;
	unsigned long obj, off;
	unsigned int obj_idx;

	struct size_class *class;
	struct mapping_area *area;
	struct page *pages[2];
	void *ret;

	/*
	 * Because we use per-cpu mapping areas shared among the
	 * pools/users, we can't allow mapping in interrupt context
	 * because it can corrupt another users mappings.
	 */
	BUG_ON(in_interrupt());

	/* It guarantees it can get zspage from handle safely */
	spin_lock(&pool->lock);
	obj = handle_to_obj(handle);
	obj_to_location(obj, &page, &obj_idx);
	zspage = get_zspage(page);

	/*
	 * migration cannot move any zpages in this zspage. Here, pool->lock
	 * is too heavy since callers would take some time until they calls
	 * zs_unmap_object API so delegate the locking from class to zspage
	 * which is smaller granularity.
	 */
	migrate_read_lock(zspage);
	spin_unlock(&pool->lock);

	class = zspage_class(pool, zspage);
	off = offset_in_page(class->size * obj_idx);

	local_lock(&zs_map_area.lock);
	area = this_cpu_ptr(&zs_map_area);
	area->vm_mm = mm;
	if (off + class->size <= PAGE_SIZE) {
		/* this object is contained entirely within a page */
		area->vm_addr = kmap_atomic(page);
		ret = area->vm_addr + off;
		goto out;
	}

	/* this object spans two pages */
	pages[0] = page;
	pages[1] = get_next_page(page);
	BUG_ON(!pages[1]);

	ret = __zs_map_object(area, pages, off, class->size);
out:
	if (likely(!ZsHugePage(zspage)))
		ret += ZS_HANDLE_SIZE;

	return ret;
}
EXPORT_SYMBOL_GPL(zs_map_object);

void zs_unmap_object(struct zs_pool *pool, unsigned long handle)
{
	struct zspage *zspage;
	struct page *page;
	unsigned long obj, off;
	unsigned int obj_idx;

	struct size_class *class;
	struct mapping_area *area;

	obj = handle_to_obj(handle);
	obj_to_location(obj, &page, &obj_idx);
	zspage = get_zspage(page);
	class = zspage_class(pool, zspage);
	off = offset_in_page(class->size * obj_idx);

	area = this_cpu_ptr(&zs_map_area);
	if (off + class->size <= PAGE_SIZE)
		kunmap_atomic(area->vm_addr);
	else {
		struct page *pages[2];

		pages[0] = page;
		pages[1] = get_next_page(page);
		BUG_ON(!pages[1]);

		__zs_unmap_object(area, pages, off, class->size);
	}
	local_unlock(&zs_map_area.lock);

	migrate_read_unlock(zspage);
}
EXPORT_SYMBOL_GPL(zs_unmap_object);

/**
 * zs_huge_class_size() - Returns the size (in bytes) of the first huge
 *                        zsmalloc &size_class.
 * @pool: zsmalloc pool to use
 *
 * The function returns the size of the first huge class - any object of equal
 * or bigger size will be stored in zspage consisting of a single physical
 * page.
 *
 * Context: Any context.
 *
 * Return: the size (in bytes) of the first huge zsmalloc &size_class.
 */
size_t zs_huge_class_size(struct zs_pool *pool)
{
	return huge_class_size;
}
EXPORT_SYMBOL_GPL(zs_huge_class_size);

static unsigned long obj_malloc(struct zs_pool *pool,
				struct zspage *zspage, unsigned long handle)
{
	int i, nr_page, offset;
	unsigned long obj;
	struct link_free *link;
	struct size_class *class;

	struct page *m_page;
	unsigned long m_offset;
	void *vaddr;

	class = pool->size_class[zspage->class];
	handle |= OBJ_ALLOCATED_TAG;
	obj = get_freeobj(zspage);

	offset = obj * class->size;
	nr_page = offset >> PAGE_SHIFT;
	m_offset = offset_in_page(offset);
	m_page = get_first_page(zspage);

	for (i = 0; i < nr_page; i++)
		m_page = get_next_page(m_page);

	vaddr = kmap_atomic(m_page);
	link = (struct link_free *)vaddr + m_offset / sizeof(*link);
	set_freeobj(zspage, link->next >> OBJ_TAG_BITS);
	if (likely(!ZsHugePage(zspage)))
		/* record handle in the header of allocated chunk */
		link->handle = handle;
	else
		/* record handle to page->index */
		zspage->first_page->index = handle;

	kunmap_atomic(vaddr);
	mod_zspage_inuse(zspage, 1);

	obj = location_to_obj(m_page, obj);

	return obj;
}


/**
 * zs_malloc - Allocate block of given size from pool.
 * @pool: pool to allocate from
 * @size: size of block to allocate
 * @gfp: gfp flags when allocating object
 *
 * On success, handle to the allocated object is returned,
 * otherwise an ERR_PTR().
 * Allocation requests with size > ZS_MAX_ALLOC_SIZE will fail.
 */
unsigned long zs_malloc(struct zs_pool *pool, size_t size, gfp_t gfp)
{
	unsigned long handle, obj;
	struct size_class *class;
	int newfg;
	struct zspage *zspage;

	if (unlikely(!size))
		return (unsigned long)ERR_PTR(-EINVAL);

	if (unlikely(size > ZS_MAX_ALLOC_SIZE))
		return (unsigned long)ERR_PTR(-ENOSPC);

	handle = cache_alloc_handle(pool, gfp);
	if (!handle)
		return (unsigned long)ERR_PTR(-ENOMEM);

	/* extra space in chunk to keep the handle */
	size += ZS_HANDLE_SIZE;
	class = pool->size_class[get_size_class_index(size)];

	/* pool->lock effectively protects the zpage migration */
	spin_lock(&pool->lock);
	zspage = find_get_zspage(class);
	if (likely(zspage)) {
		obj = obj_malloc(pool, zspage, handle);
		/* Now move the zspage to another fullness group, if required */
		fix_fullness_group(class, zspage);
		record_obj(handle, obj);
		class_stat_inc(class, ZS_OBJS_INUSE, 1);

		goto out;
	}

	spin_unlock(&pool->lock);

	zspage = alloc_zspage(pool, class, gfp);
	if (!zspage) {
		cache_free_handle(pool, handle);
		return (unsigned long)ERR_PTR(-ENOMEM);
	}

	spin_lock(&pool->lock);
	obj = obj_malloc(pool, zspage, handle);
	newfg = get_fullness_group(class, zspage);
	insert_zspage(class, zspage, newfg);
	record_obj(handle, obj);
	atomic_long_add(class->pages_per_zspage, &pool->pages_allocated);
	class_stat_inc(class, ZS_OBJS_ALLOCATED, class->objs_per_zspage);
	class_stat_inc(class, ZS_OBJS_INUSE, 1);

	/* We completely set up zspage so mark them as movable */
	SetZsPageMovable(pool, zspage);
out:
	spin_unlock(&pool->lock);

	return handle;
}
EXPORT_SYMBOL_GPL(zs_malloc);

static void obj_free(int class_size, unsigned long obj)
{
	struct link_free *link;
	struct zspage *zspage;
	struct page *f_page;
	unsigned long f_offset;
	unsigned int f_objidx;
	void *vaddr;

	obj_to_location(obj, &f_page, &f_objidx);
	f_offset = offset_in_page(class_size * f_objidx);
	zspage = get_zspage(f_page);

	vaddr = kmap_atomic(f_page);
	link = (struct link_free *)(vaddr + f_offset);

	/* Insert this object in containing zspage's freelist */
	if (likely(!ZsHugePage(zspage)))
		link->next = get_freeobj(zspage) << OBJ_TAG_BITS;
	else
		f_page->index = 0;
	set_freeobj(zspage, f_objidx);

	kunmap_atomic(vaddr);
	mod_zspage_inuse(zspage, -1);
}

void zs_free(struct zs_pool *pool, unsigned long handle)
{
	struct zspage *zspage;
	struct page *f_page;
	unsigned long obj;
	struct size_class *class;
	int fullness;

	if (IS_ERR_OR_NULL((void *)handle))
		return;

	/*
	 * The pool->lock protects the race with zpage's migration
	 * so it's safe to get the page from handle.
	 */
	spin_lock(&pool->lock);
	obj = handle_to_obj(handle);
	obj_to_page(obj, &f_page);
	zspage = get_zspage(f_page);
	class = zspage_class(pool, zspage);

	class_stat_dec(class, ZS_OBJS_INUSE, 1);
	obj_free(class->size, obj);

	fullness = fix_fullness_group(class, zspage);
	if (fullness == ZS_INUSE_RATIO_0)
		free_zspage(pool, class, zspage);

	spin_unlock(&pool->lock);
	cache_free_handle(pool, handle);
}
EXPORT_SYMBOL_GPL(zs_free);

static void zs_object_copy(struct size_class *class, unsigned long dst,
				unsigned long src)
{
	struct page *s_page, *d_page;
	unsigned int s_objidx, d_objidx;
	unsigned long s_off, d_off;
	void *s_addr, *d_addr;
	int s_size, d_size, size;
	int written = 0;

	s_size = d_size = class->size;

	obj_to_location(src, &s_page, &s_objidx);
	obj_to_location(dst, &d_page, &d_objidx);

	s_off = offset_in_page(class->size * s_objidx);
	d_off = offset_in_page(class->size * d_objidx);

	if (s_off + class->size > PAGE_SIZE)
		s_size = PAGE_SIZE - s_off;

	if (d_off + class->size > PAGE_SIZE)
		d_size = PAGE_SIZE - d_off;

	s_addr = kmap_atomic(s_page);
	d_addr = kmap_atomic(d_page);

	while (1) {
		size = min(s_size, d_size);
		memcpy(d_addr + d_off, s_addr + s_off, size);
		written += size;

		if (written == class->size)
			break;

		s_off += size;
		s_size -= size;
		d_off += size;
		d_size -= size;

		/*
		 * Calling kunmap_atomic(d_addr) is necessary. kunmap_atomic()
		 * calls must occurs in reverse order of calls to kmap_atomic().
		 * So, to call kunmap_atomic(s_addr) we should first call
		 * kunmap_atomic(d_addr). For more details see
		 * Documentation/mm/highmem.rst.
		 */
		if (s_off >= PAGE_SIZE) {
			kunmap_atomic(d_addr);
			kunmap_atomic(s_addr);
			s_page = get_next_page(s_page);
			s_addr = kmap_atomic(s_page);
			d_addr = kmap_atomic(d_page);
			s_size = class->size - written;
			s_off = 0;
		}

		if (d_off >= PAGE_SIZE) {
			kunmap_atomic(d_addr);
			d_page = get_next_page(d_page);
			d_addr = kmap_atomic(d_page);
			d_size = class->size - written;
			d_off = 0;
		}
	}

	kunmap_atomic(d_addr);
	kunmap_atomic(s_addr);
}

/*
 * Find alloced object in zspage from index object and
 * return handle.
 */
static unsigned long find_alloced_obj(struct size_class *class,
				      struct page *page, int *obj_idx)
{
	unsigned int offset;
	int index = *obj_idx;
	unsigned long handle = 0;
	void *addr = kmap_atomic(page);

	offset = get_first_obj_offset(page);
	offset += class->size * index;

	while (offset < PAGE_SIZE) {
		if (obj_allocated(page, addr + offset, &handle))
			break;

		offset += class->size;
		index++;
	}

	kunmap_atomic(addr);

	*obj_idx = index;

	return handle;
}

static void migrate_zspage(struct zs_pool *pool, struct zspage *src_zspage,
			   struct zspage *dst_zspage)
{
	unsigned long used_obj, free_obj;
	unsigned long handle;
	int obj_idx = 0;
	struct page *s_page = get_first_page(src_zspage);
	struct size_class *class = pool->size_class[src_zspage->class];

	while (1) {
		handle = find_alloced_obj(class, s_page, &obj_idx);
		if (!handle) {
			s_page = get_next_page(s_page);
			if (!s_page)
				break;
			obj_idx = 0;
			continue;
		}

		used_obj = handle_to_obj(handle);
		free_obj = obj_malloc(pool, dst_zspage, handle);
		zs_object_copy(class, free_obj, used_obj);
		obj_idx++;
		record_obj(handle, free_obj);
		obj_free(class->size, used_obj);

		/* Stop if there is no more space */
		if (zspage_full(class, dst_zspage))
			break;

		/* Stop if there are no more objects to migrate */
		if (zspage_empty(src_zspage))
			break;
	}
}

static struct zspage *isolate_src_zspage(struct size_class *class)
{
	struct zspage *zspage;
	int fg;

	for (fg = ZS_INUSE_RATIO_10; fg <= ZS_INUSE_RATIO_99; fg++) {
		zspage = list_first_entry_or_null(&class->fullness_list[fg],
						  struct zspage, list);
		if (zspage) {
			remove_zspage(class, zspage);
			return zspage;
		}
	}

	return zspage;
}

static struct zspage *isolate_dst_zspage(struct size_class *class)
{
	struct zspage *zspage;
	int fg;

	for (fg = ZS_INUSE_RATIO_99; fg >= ZS_INUSE_RATIO_10; fg--) {
		zspage = list_first_entry_or_null(&class->fullness_list[fg],
						  struct zspage, list);
		if (zspage) {
			remove_zspage(class, zspage);
			return zspage;
		}
	}

	return zspage;
}

/*
 * putback_zspage - add @zspage into right class's fullness list
 * @class: destination class
 * @zspage: target page
 *
 * Return @zspage's fullness status
 */
static int putback_zspage(struct size_class *class, struct zspage *zspage)
{
	int fullness;

	fullness = get_fullness_group(class, zspage);
	insert_zspage(class, zspage, fullness);

	return fullness;
}

#ifdef CONFIG_COMPACTION
/*
 * To prevent zspage destroy during migration, zspage freeing should
 * hold locks of all pages in the zspage.
 */
static void lock_zspage(struct zspage *zspage)
{
	struct page *curr_page, *page;

	/*
	 * Pages we haven't locked yet can be migrated off the list while we're
	 * trying to lock them, so we need to be careful and only attempt to
	 * lock each page under migrate_read_lock(). Otherwise, the page we lock
	 * may no longer belong to the zspage. This means that we may wait for
	 * the wrong page to unlock, so we must take a reference to the page
	 * prior to waiting for it to unlock outside migrate_read_lock().
	 */
	while (1) {
		migrate_read_lock(zspage);
		page = get_first_page(zspage);
		if (trylock_page(page))
			break;
		get_page(page);
		migrate_read_unlock(zspage);
		wait_on_page_locked(page);
		put_page(page);
	}

	curr_page = page;
	while ((page = get_next_page(curr_page))) {
		if (trylock_page(page)) {
			curr_page = page;
		} else {
			get_page(page);
			migrate_read_unlock(zspage);
			wait_on_page_locked(page);
			put_page(page);
			migrate_read_lock(zspage);
		}
	}
	migrate_read_unlock(zspage);
}
#endif /* CONFIG_COMPACTION */

static void migrate_lock_init(struct zspage *zspage)
{
	rwlock_init(&zspage->lock);
}

static void migrate_read_lock(struct zspage *zspage) __acquires(&zspage->lock)
{
	read_lock(&zspage->lock);
}

static void migrate_read_unlock(struct zspage *zspage) __releases(&zspage->lock)
{
	read_unlock(&zspage->lock);
}

static void migrate_write_lock(struct zspage *zspage)
{
	write_lock(&zspage->lock);
}

static void migrate_write_unlock(struct zspage *zspage)
{
	write_unlock(&zspage->lock);
}

#ifdef CONFIG_COMPACTION

static const struct movable_operations zsmalloc_mops;

static void replace_sub_page(struct size_class *class, struct zspage *zspage,
				struct page *newpage, struct page *oldpage)
{
	struct page *page;
	struct page *pages[ZS_MAX_PAGES_PER_ZSPAGE] = {NULL, };
	int idx = 0;

	page = get_first_page(zspage);
	do {
		if (page == oldpage)
			pages[idx] = newpage;
		else
			pages[idx] = page;
		idx++;
	} while ((page = get_next_page(page)) != NULL);

	create_page_chain(class, zspage, pages);
	set_first_obj_offset(newpage, get_first_obj_offset(oldpage));
	if (unlikely(ZsHugePage(zspage)))
		newpage->index = oldpage->index;
	__SetPageMovable(newpage, &zsmalloc_mops);
}

static bool zs_page_isolate(struct page *page, isolate_mode_t mode)
{
	/*
	 * Page is locked so zspage couldn't be destroyed. For detail, look at
	 * lock_zspage in free_zspage.
	 */
	VM_BUG_ON_PAGE(PageIsolated(page), page);

	return true;
}

static int zs_page_migrate(struct page *newpage, struct page *page,
		enum migrate_mode mode)
{
	struct zs_pool *pool;
	struct size_class *class;
	struct zspage *zspage;
	struct page *dummy;
	void *s_addr, *d_addr, *addr;
	unsigned int offset;
	unsigned long handle;
	unsigned long old_obj, new_obj;
	unsigned int obj_idx;

	/*
	 * We cannot support the _NO_COPY case here, because copy needs to
	 * happen under the zs lock, which does not work with
	 * MIGRATE_SYNC_NO_COPY workflow.
	 */
	if (mode == MIGRATE_SYNC_NO_COPY)
		return -EINVAL;

	VM_BUG_ON_PAGE(!PageIsolated(page), page);

	/* The page is locked, so this pointer must remain valid */
	zspage = get_zspage(page);
	pool = zspage->pool;

	/*
	 * The pool's lock protects the race between zpage migration
	 * and zs_free.
	 */
	spin_lock(&pool->lock);
	class = zspage_class(pool, zspage);

	/* the migrate_write_lock protects zpage access via zs_map_object */
	migrate_write_lock(zspage);

	offset = get_first_obj_offset(page);
	s_addr = kmap_atomic(page);

	/*
	 * Here, any user cannot access all objects in the zspage so let's move.
	 */
	d_addr = kmap_atomic(newpage);
	copy_page(d_addr, s_addr);
	kunmap_atomic(d_addr);

	for (addr = s_addr + offset; addr < s_addr + PAGE_SIZE;
					addr += class->size) {
		if (obj_allocated(page, addr, &handle)) {

			old_obj = handle_to_obj(handle);
			obj_to_location(old_obj, &dummy, &obj_idx);
			new_obj = (unsigned long)location_to_obj(newpage,
								obj_idx);
			record_obj(handle, new_obj);
		}
	}
	kunmap_atomic(s_addr);

	replace_sub_page(class, zspage, newpage, page);
	/*
	 * Since we complete the data copy and set up new zspage structure,
	 * it's okay to release the pool's lock.
	 */
	spin_unlock(&pool->lock);
	migrate_write_unlock(zspage);

	get_page(newpage);
	if (page_zone(newpage) != page_zone(page)) {
		dec_zone_page_state(page, NR_ZSPAGES);
		inc_zone_page_state(newpage, NR_ZSPAGES);
	}

	reset_page(page);
	put_page(page);

	return MIGRATEPAGE_SUCCESS;
}

static void zs_page_putback(struct page *page)
{
	VM_BUG_ON_PAGE(!PageIsolated(page), page);
}

static const struct movable_operations zsmalloc_mops = {
	.isolate_page = zs_page_isolate,
	.migrate_page = zs_page_migrate,
	.putback_page = zs_page_putback,
};

/*
 * Caller should hold page_lock of all pages in the zspage
 * In here, we cannot use zspage meta data.
 */
static void async_free_zspage(struct work_struct *work)
{
	int i;
	struct size_class *class;
	struct zspage *zspage, *tmp;
	LIST_HEAD(free_pages);
	struct zs_pool *pool = container_of(work, struct zs_pool,
					free_work);

	for (i = 0; i < ZS_SIZE_CLASSES; i++) {
		class = pool->size_class[i];
		if (class->index != i)
			continue;

		spin_lock(&pool->lock);
		list_splice_init(&class->fullness_list[ZS_INUSE_RATIO_0],
				 &free_pages);
		spin_unlock(&pool->lock);
	}

	list_for_each_entry_safe(zspage, tmp, &free_pages, list) {
		list_del(&zspage->list);
		lock_zspage(zspage);

		spin_lock(&pool->lock);
		class = zspage_class(pool, zspage);
		__free_zspage(pool, class, zspage);
		spin_unlock(&pool->lock);
	}
};

static void kick_deferred_free(struct zs_pool *pool)
{
	schedule_work(&pool->free_work);
}

static void zs_flush_migration(struct zs_pool *pool)
{
	flush_work(&pool->free_work);
}

static void init_deferred_free(struct zs_pool *pool)
{
	INIT_WORK(&pool->free_work, async_free_zspage);
}

static void SetZsPageMovable(struct zs_pool *pool, struct zspage *zspage)
{
	struct page *page = get_first_page(zspage);

	do {
		WARN_ON(!trylock_page(page));
		__SetPageMovable(page, &zsmalloc_mops);
		unlock_page(page);
	} while ((page = get_next_page(page)) != NULL);
}
#else
static inline void zs_flush_migration(struct zs_pool *pool) { }
#endif

/*
 *
 * Based on the number of unused allocated objects calculate
 * and return the number of pages that we can free.
 */
static unsigned long zs_can_compact(struct size_class *class)
{
	unsigned long obj_wasted;
	unsigned long obj_allocated = zs_stat_get(class, ZS_OBJS_ALLOCATED);
	unsigned long obj_used = zs_stat_get(class, ZS_OBJS_INUSE);

	if (obj_allocated <= obj_used)
		return 0;

	obj_wasted = obj_allocated - obj_used;
	obj_wasted /= class->objs_per_zspage;

	return obj_wasted * class->pages_per_zspage;
}

static unsigned long __zs_compact(struct zs_pool *pool,
				  struct size_class *class)
{
	struct zspage *src_zspage = NULL;
	struct zspage *dst_zspage = NULL;
	unsigned long pages_freed = 0;

	/*
	 * protect the race between zpage migration and zs_free
	 * as well as zpage allocation/free
	 */
	spin_lock(&pool->lock);
	while (zs_can_compact(class)) {
		int fg;

		if (!dst_zspage) {
			dst_zspage = isolate_dst_zspage(class);
			if (!dst_zspage)
				break;
		}

		src_zspage = isolate_src_zspage(class);
		if (!src_zspage)
			break;

		migrate_write_lock(src_zspage);
		migrate_zspage(pool, src_zspage, dst_zspage);
		migrate_write_unlock(src_zspage);

		fg = putback_zspage(class, src_zspage);
		if (fg == ZS_INUSE_RATIO_0) {
			free_zspage(pool, class, src_zspage);
			pages_freed += class->pages_per_zspage;
		}
		src_zspage = NULL;

		if (get_fullness_group(class, dst_zspage) == ZS_INUSE_RATIO_100
		    || spin_is_contended(&pool->lock)) {
			putback_zspage(class, dst_zspage);
			dst_zspage = NULL;

			spin_unlock(&pool->lock);
			cond_resched();
			spin_lock(&pool->lock);
		}
	}

	if (src_zspage)
		putback_zspage(class, src_zspage);

	if (dst_zspage)
		putback_zspage(class, dst_zspage);

	spin_unlock(&pool->lock);

	return pages_freed;
}

unsigned long zs_compact(struct zs_pool *pool)
{
	int i;
	struct size_class *class;
	unsigned long pages_freed = 0;

	/*
	 * Pool compaction is performed under pool->lock so it is basically
	 * single-threaded. Having more than one thread in __zs_compact()
	 * will increase pool->lock contention, which will impact other
	 * zsmalloc operations that need pool->lock.
	 */
	if (atomic_xchg(&pool->compaction_in_progress, 1))
		return 0;

	for (i = ZS_SIZE_CLASSES - 1; i >= 0; i--) {
		class = pool->size_class[i];
		if (class->index != i)
			continue;
		pages_freed += __zs_compact(pool, class);
	}
	atomic_long_add(pages_freed, &pool->stats.pages_compacted);
	atomic_set(&pool->compaction_in_progress, 0);

	return pages_freed;
}
EXPORT_SYMBOL_GPL(zs_compact);

void zs_pool_stats(struct zs_pool *pool, struct zs_pool_stats *stats)
{
	memcpy(stats, &pool->stats, sizeof(struct zs_pool_stats));
}
EXPORT_SYMBOL_GPL(zs_pool_stats);

static unsigned long zs_shrinker_scan(struct shrinker *shrinker,
		struct shrink_control *sc)
{
	unsigned long pages_freed;
	struct zs_pool *pool = shrinker->private_data;

	/*
	 * Compact classes and calculate compaction delta.
	 * Can run concurrently with a manually triggered
	 * (by user) compaction.
	 */
	pages_freed = zs_compact(pool);

	return pages_freed ? pages_freed : SHRINK_STOP;
}

static unsigned long zs_shrinker_count(struct shrinker *shrinker,
		struct shrink_control *sc)
{
	int i;
	struct size_class *class;
	unsigned long pages_to_free = 0;
	struct zs_pool *pool = shrinker->private_data;

	for (i = ZS_SIZE_CLASSES - 1; i >= 0; i--) {
		class = pool->size_class[i];
		if (class->index != i)
			continue;

		pages_to_free += zs_can_compact(class);
	}

	return pages_to_free;
}

static void zs_unregister_shrinker(struct zs_pool *pool)
{
	shrinker_free(pool->shrinker);
}

static int zs_register_shrinker(struct zs_pool *pool)
{
	pool->shrinker = shrinker_alloc(0, "mm-zspool:%s", pool->name);
	if (!pool->shrinker)
		return -ENOMEM;

	pool->shrinker->scan_objects = zs_shrinker_scan;
	pool->shrinker->count_objects = zs_shrinker_count;
	pool->shrinker->batch = 0;
	pool->shrinker->private_data = pool;

	shrinker_register(pool->shrinker);

	return 0;
}

static int calculate_zspage_chain_size(int class_size)
{
	int i, min_waste = INT_MAX;
	int chain_size = 1;

	if (is_power_of_2(class_size))
		return chain_size;

	for (i = 1; i <= ZS_MAX_PAGES_PER_ZSPAGE; i++) {
		int waste;

		waste = (i * PAGE_SIZE) % class_size;
		if (waste < min_waste) {
			min_waste = waste;
			chain_size = i;
		}
	}

	return chain_size;
}

/**
 * zs_create_pool - Creates an allocation pool to work from.
 * @name: pool name to be created
 *
 * This function must be called before anything when using
 * the zsmalloc allocator.
 *
 * On success, a pointer to the newly created pool is returned,
 * otherwise NULL.
 */
struct zs_pool *zs_create_pool(const char *name)
{
	int i;
	struct zs_pool *pool;
	struct size_class *prev_class = NULL;

	pool = kzalloc(sizeof(*pool), GFP_KERNEL);
	if (!pool)
		return NULL;

	init_deferred_free(pool);
	spin_lock_init(&pool->lock);
	atomic_set(&pool->compaction_in_progress, 0);

	pool->name = kstrdup(name, GFP_KERNEL);
	if (!pool->name)
		goto err;

	if (create_cache(pool))
		goto err;

	/*
	 * Iterate reversely, because, size of size_class that we want to use
	 * for merging should be larger or equal to current size.
	 */
	for (i = ZS_SIZE_CLASSES - 1; i >= 0; i--) {
		int size;
		int pages_per_zspage;
		int objs_per_zspage;
		struct size_class *class;
		int fullness;

		size = ZS_MIN_ALLOC_SIZE + i * ZS_SIZE_CLASS_DELTA;
		if (size > ZS_MAX_ALLOC_SIZE)
			size = ZS_MAX_ALLOC_SIZE;
		pages_per_zspage = calculate_zspage_chain_size(size);
		objs_per_zspage = pages_per_zspage * PAGE_SIZE / size;

		/*
		 * We iterate from biggest down to smallest classes,
		 * so huge_class_size holds the size of the first huge
		 * class. Any object bigger than or equal to that will
		 * endup in the huge class.
		 */
		if (pages_per_zspage != 1 && objs_per_zspage != 1 &&
				!huge_class_size) {
			huge_class_size = size;
			/*
			 * The object uses ZS_HANDLE_SIZE bytes to store the
			 * handle. We need to subtract it, because zs_malloc()
			 * unconditionally adds handle size before it performs
			 * size class search - so object may be smaller than
			 * huge class size, yet it still can end up in the huge
			 * class because it grows by ZS_HANDLE_SIZE extra bytes
			 * right before class lookup.
			 */
			huge_class_size -= (ZS_HANDLE_SIZE - 1);
		}

		/*
		 * size_class is used for normal zsmalloc operation such
		 * as alloc/free for that size. Although it is natural that we
		 * have one size_class for each size, there is a chance that we
		 * can get more memory utilization if we use one size_class for
		 * many different sizes whose size_class have same
		 * characteristics. So, we makes size_class point to
		 * previous size_class if possible.
		 */
		if (prev_class) {
			if (can_merge(prev_class, pages_per_zspage, objs_per_zspage)) {
				pool->size_class[i] = prev_class;
				continue;
			}
		}

		class = kzalloc(sizeof(struct size_class), GFP_KERNEL);
		if (!class)
			goto err;

		class->size = size;
		class->index = i;
		class->pages_per_zspage = pages_per_zspage;
		class->objs_per_zspage = objs_per_zspage;
		pool->size_class[i] = class;

		fullness = ZS_INUSE_RATIO_0;
		while (fullness < NR_FULLNESS_GROUPS) {
			INIT_LIST_HEAD(&class->fullness_list[fullness]);
			fullness++;
		}

		prev_class = class;
	}

	/* debug only, don't abort if it fails */
	zs_pool_stat_create(pool, name);

	/*
	 * Not critical since shrinker is only used to trigger internal
	 * defragmentation of the pool which is pretty optional thing.  If
	 * registration fails we still can use the pool normally and user can
	 * trigger compaction manually. Thus, ignore return code.
	 */
	zs_register_shrinker(pool);

	return pool;

err:
	zs_destroy_pool(pool);
	return NULL;
}
EXPORT_SYMBOL_GPL(zs_create_pool);

void zs_destroy_pool(struct zs_pool *pool)
{
	int i;

	zs_unregister_shrinker(pool);
	zs_flush_migration(pool);
	zs_pool_stat_destroy(pool);

	for (i = 0; i < ZS_SIZE_CLASSES; i++) {
		int fg;
		struct size_class *class = pool->size_class[i];

		if (!class)
			continue;

		if (class->index != i)
			continue;

		for (fg = ZS_INUSE_RATIO_0; fg < NR_FULLNESS_GROUPS; fg++) {
			if (list_empty(&class->fullness_list[fg]))
				continue;

			pr_err("Class-%d fullness group %d is not empty\n",
			       class->size, fg);
		}
		kfree(class);
	}

	destroy_cache(pool);
	kfree(pool->name);
	kfree(pool);
}
EXPORT_SYMBOL_GPL(zs_destroy_pool);

static int __init zs_init(void)
{
	int ret;

	ret = cpuhp_setup_state(CPUHP_MM_ZS_PREPARE, "mm/zsmalloc:prepare",
				zs_cpu_prepare, zs_cpu_dead);
	if (ret)
		goto out;

#ifdef CONFIG_ZPOOL
	zpool_register_driver(&zs_zpool_driver);
#endif

	zs_stat_init();

	return 0;

out:
	return ret;
}

static void __exit zs_exit(void)
{
#ifdef CONFIG_ZPOOL
	zpool_unregister_driver(&zs_zpool_driver);
#endif
	cpuhp_remove_state(CPUHP_MM_ZS_PREPARE);

	zs_stat_exit();
}

module_init(zs_init);
module_exit(zs_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Nitin Gupta <ngupta@vflare.org>");
