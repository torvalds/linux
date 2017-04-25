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
 *	page->freelist(index): links together all component pages of a zspage
 *		For the huge page, this is always 0, so we use this field
 *		to store handle.
 *	page->units: first object offset in a subpage of zspage
 *
 * Usage of struct page flags:
 *	PG_private: identifies the first component page
 *	PG_owner_priv_1: identifies the huge component page
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/magic.h>
#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/highmem.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <asm/tlbflush.h>
#include <asm/pgtable.h>
#include <linux/cpumask.h>
#include <linux/cpu.h>
#include <linux/vmalloc.h>
#include <linux/preempt.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/debugfs.h>
#include <linux/zsmalloc.h>
#include <linux/zpool.h>
#include <linux/mount.h>
#include <linux/migrate.h>
#include <linux/pagemap.h>

#define ZSPAGE_MAGIC	0x58

/*
 * This must be power of 2 and greater than of equal to sizeof(link_free).
 * These two conditions ensure that any 'struct link_free' itself doesn't
 * span more than 1 page which avoids complex case of mapping 2 pages simply
 * to restore link_free pointer values.
 */
#define ZS_ALIGN		8

/*
 * A single 'zspage' is composed of up to 2^N discontiguous 0-order (single)
 * pages. ZS_MAX_ZSPAGE_ORDER defines upper limit on N.
 */
#define ZS_MAX_ZSPAGE_ORDER 2
#define ZS_MAX_PAGES_PER_ZSPAGE (_AC(1, UL) << ZS_MAX_ZSPAGE_ORDER)

#define ZS_HANDLE_SIZE (sizeof(unsigned long))

/*
 * Object location (<PFN>, <obj_idx>) is encoded as
 * as single (unsigned long) handle value.
 *
 * Note that object index <obj_idx> starts from 0.
 *
 * This is made more complicated by various memory models and PAE.
 */

#ifndef MAX_PHYSMEM_BITS
#ifdef CONFIG_HIGHMEM64G
#define MAX_PHYSMEM_BITS 36
#else /* !CONFIG_HIGHMEM64G */
/*
 * If this definition of MAX_PHYSMEM_BITS is used, OBJ_INDEX_BITS will just
 * be PAGE_SHIFT
 */
#define MAX_PHYSMEM_BITS BITS_PER_LONG
#endif
#endif
#define _PFN_BITS		(MAX_PHYSMEM_BITS - PAGE_SHIFT)

/*
 * Memory for allocating for handle keeps object position by
 * encoding <page, obj_idx> and the encoded value has a room
 * in least bit(ie, look at obj_to_location).
 * We use the bit to synchronize between object access by
 * user and migration.
 */
#define HANDLE_PIN_BIT	0

/*
 * Head in allocated object should have OBJ_ALLOCATED_TAG
 * to identify the object was allocated or not.
 * It's okay to add the status bit in the least bit because
 * header keeps handle which is 4byte-aligned address so we
 * have room for two bit at least.
 */
#define OBJ_ALLOCATED_TAG 1
#define OBJ_TAG_BITS 1
#define OBJ_INDEX_BITS	(BITS_PER_LONG - _PFN_BITS - OBJ_TAG_BITS)
#define OBJ_INDEX_MASK	((_AC(1, UL) << OBJ_INDEX_BITS) - 1)

#define MAX(a, b) ((a) >= (b) ? (a) : (b))
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

enum fullness_group {
	ZS_EMPTY,
	ZS_ALMOST_EMPTY,
	ZS_ALMOST_FULL,
	ZS_FULL,
	NR_ZS_FULLNESS,
};

enum zs_stat_type {
	CLASS_EMPTY,
	CLASS_ALMOST_EMPTY,
	CLASS_ALMOST_FULL,
	CLASS_FULL,
	OBJ_ALLOCATED,
	OBJ_USED,
	NR_ZS_STAT_TYPE,
};

struct zs_size_stat {
	unsigned long objs[NR_ZS_STAT_TYPE];
};

#ifdef CONFIG_ZSMALLOC_STAT
static struct dentry *zs_stat_root;
#endif

#ifdef CONFIG_COMPACTION
static struct vfsmount *zsmalloc_mnt;
#endif

/*
 * number of size_classes
 */
static int zs_size_classes;

/*
 * We assign a page to ZS_ALMOST_EMPTY fullness group when:
 *	n <= N / f, where
 * n = number of allocated objects
 * N = total number of objects zspage can store
 * f = fullness_threshold_frac
 *
 * Similarly, we assign zspage to:
 *	ZS_ALMOST_FULL	when n > N / f
 *	ZS_EMPTY	when n == 0
 *	ZS_FULL		when n == N
 *
 * (see: fix_fullness_group())
 */
static const int fullness_threshold_frac = 4;

struct size_class {
	spinlock_t lock;
	struct list_head fullness_list[NR_ZS_FULLNESS];
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

/* huge object: pages_per_zspage == 1 && maxobj_per_zspage == 1 */
static void SetPageHugeObject(struct page *page)
{
	SetPageOwnerPriv1(page);
}

static void ClearPageHugeObject(struct page *page)
{
	ClearPageOwnerPriv1(page);
}

static int PageHugeObject(struct page *page)
{
	return PageOwnerPriv1(page);
}

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

	struct size_class **size_class;
	struct kmem_cache *handle_cachep;
	struct kmem_cache *zspage_cachep;

	atomic_long_t pages_allocated;

	struct zs_pool_stats stats;

	/* Compact classes */
	struct shrinker shrinker;
	/*
	 * To signify that register_shrinker() was successful
	 * and unregister_shrinker() will not Oops.
	 */
	bool shrinker_enabled;
#ifdef CONFIG_ZSMALLOC_STAT
	struct dentry *stat_dentry;
#endif
#ifdef CONFIG_COMPACTION
	struct inode *inode;
	struct work_struct free_work;
#endif
};

#define FULLNESS_BITS	2
#define CLASS_BITS	8
#define ISOLATED_BITS	3
#define MAGIC_VAL_BITS	8

struct zspage {
	struct {
		unsigned int fullness:FULLNESS_BITS;
		unsigned int class:CLASS_BITS;
		unsigned int isolated:ISOLATED_BITS;
		unsigned int magic:MAGIC_VAL_BITS;
	};
	unsigned int inuse;
	unsigned int freeobj;
	struct page *first_page;
	struct list_head list; /* fullness list */
#ifdef CONFIG_COMPACTION
	rwlock_t lock;
#endif
};

struct mapping_area {
#ifdef CONFIG_PGTABLE_MAPPING
	struct vm_struct *vm; /* vm area for mapping object that span pages */
#else
	char *vm_buf; /* copy buffer for objects that span pages */
#endif
	char *vm_addr; /* address of kmap_atomic()'ed pages */
	enum zs_mapmode vm_mm; /* mapping mode */
};

#ifdef CONFIG_COMPACTION
static int zs_register_migration(struct zs_pool *pool);
static void zs_unregister_migration(struct zs_pool *pool);
static void migrate_lock_init(struct zspage *zspage);
static void migrate_read_lock(struct zspage *zspage);
static void migrate_read_unlock(struct zspage *zspage);
static void kick_deferred_free(struct zs_pool *pool);
static void init_deferred_free(struct zs_pool *pool);
static void SetZsPageMovable(struct zs_pool *pool, struct zspage *zspage);
#else
static int zsmalloc_mount(void) { return 0; }
static void zsmalloc_unmount(void) {}
static int zs_register_migration(struct zs_pool *pool) { return 0; }
static void zs_unregister_migration(struct zs_pool *pool) {}
static void migrate_lock_init(struct zspage *zspage) {}
static void migrate_read_lock(struct zspage *zspage) {}
static void migrate_read_unlock(struct zspage *zspage) {}
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
	return kmem_cache_alloc(pool->zspage_cachep,
			flags & ~(__GFP_HIGHMEM|__GFP_MOVABLE));
}

static void cache_free_zspage(struct zs_pool *pool, struct zspage *zspage)
{
	kmem_cache_free(pool->zspage_cachep, zspage);
}

static void record_obj(unsigned long handle, unsigned long obj)
{
	/*
	 * lsb of @obj represents handle lock while other bits
	 * represent object value the handle is pointing so
	 * updating shouldn't do store tearing.
	 */
	WRITE_ONCE(*(unsigned long *)handle, obj);
}

/* zpool driver */

#ifdef CONFIG_ZPOOL

static void *zs_zpool_create(const char *name, gfp_t gfp,
			     const struct zpool_ops *zpool_ops,
			     struct zpool *zpool)
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
	return *handle ? 0 : -1;
}
static void zs_zpool_free(void *pool, unsigned long handle)
{
	zs_free(pool, handle);
}

static int zs_zpool_shrink(void *pool, unsigned int pages,
			unsigned int *reclaimed)
{
	return -EINVAL;
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
	case ZPOOL_MM_RW: /* fallthru */
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

static u64 zs_zpool_total_size(void *pool)
{
	return zs_get_total_pages(pool) << PAGE_SHIFT;
}

static struct zpool_driver zs_zpool_driver = {
	.type =		"zsmalloc",
	.owner =	THIS_MODULE,
	.create =	zs_zpool_create,
	.destroy =	zs_zpool_destroy,
	.malloc =	zs_zpool_malloc,
	.free =		zs_zpool_free,
	.shrink =	zs_zpool_shrink,
	.map =		zs_zpool_map,
	.unmap =	zs_zpool_unmap,
	.total_size =	zs_zpool_total_size,
};

MODULE_ALIAS("zpool-zsmalloc");
#endif /* CONFIG_ZPOOL */

/* per-cpu VM mapping areas for zspage accesses that cross page boundaries */
static DEFINE_PER_CPU(struct mapping_area, zs_map_area);

static bool is_zspage_isolated(struct zspage *zspage)
{
	return zspage->isolated;
}

static int is_first_page(struct page *page)
{
	return PagePrivate(page);
}

/* Protected by class->lock */
static inline int get_zspage_inuse(struct zspage *zspage)
{
	return zspage->inuse;
}

static inline void set_zspage_inuse(struct zspage *zspage, int val)
{
	zspage->inuse = val;
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

static inline int get_first_obj_offset(struct page *page)
{
	return page->units;
}

static inline void set_first_obj_offset(struct page *page, int offset)
{
	page->units = offset;
}

static inline unsigned int get_freeobj(struct zspage *zspage)
{
	return zspage->freeobj;
}

static inline void set_freeobj(struct zspage *zspage, unsigned int obj)
{
	zspage->freeobj = obj;
}

static void get_zspage_mapping(struct zspage *zspage,
				unsigned int *class_idx,
				enum fullness_group *fullness)
{
	BUG_ON(zspage->magic != ZSPAGE_MAGIC);

	*fullness = zspage->fullness;
	*class_idx = zspage->class;
}

static void set_zspage_mapping(struct zspage *zspage,
				unsigned int class_idx,
				enum fullness_group fullness)
{
	zspage->class = class_idx;
	zspage->fullness = fullness;
}

/*
 * zsmalloc divides the pool into various size classes where each
 * class maintains a list of zspages where each zspage is divided
 * into equal sized chunks. Each allocation falls into one of these
 * classes depending on its size. This function returns index of the
 * size class which has chunk size big enough to hold the give size.
 */
static int get_size_class_index(int size)
{
	int idx = 0;

	if (likely(size > ZS_MIN_ALLOC_SIZE))
		idx = DIV_ROUND_UP(size - ZS_MIN_ALLOC_SIZE,
				ZS_SIZE_CLASS_DELTA);

	return min(zs_size_classes - 1, idx);
}

static inline void zs_stat_inc(struct size_class *class,
				enum zs_stat_type type, unsigned long cnt)
{
	class->stats.objs[type] += cnt;
}

static inline void zs_stat_dec(struct size_class *class,
				enum zs_stat_type type, unsigned long cnt)
{
	class->stats.objs[type] -= cnt;
}

static inline unsigned long zs_stat_get(struct size_class *class,
				enum zs_stat_type type)
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
	if (!zs_stat_root)
		pr_warn("debugfs 'zsmalloc' stat dir creation failed\n");
}

static void __exit zs_stat_exit(void)
{
	debugfs_remove_recursive(zs_stat_root);
}

static unsigned long zs_can_compact(struct size_class *class);

static int zs_stats_size_show(struct seq_file *s, void *v)
{
	int i;
	struct zs_pool *pool = s->private;
	struct size_class *class;
	int objs_per_zspage;
	unsigned long class_almost_full, class_almost_empty;
	unsigned long obj_allocated, obj_used, pages_used, freeable;
	unsigned long total_class_almost_full = 0, total_class_almost_empty = 0;
	unsigned long total_objs = 0, total_used_objs = 0, total_pages = 0;
	unsigned long total_freeable = 0;

	seq_printf(s, " %5s %5s %11s %12s %13s %10s %10s %16s %8s\n",
			"class", "size", "almost_full", "almost_empty",
			"obj_allocated", "obj_used", "pages_used",
			"pages_per_zspage", "freeable");

	for (i = 0; i < zs_size_classes; i++) {
		class = pool->size_class[i];

		if (class->index != i)
			continue;

		spin_lock(&class->lock);
		class_almost_full = zs_stat_get(class, CLASS_ALMOST_FULL);
		class_almost_empty = zs_stat_get(class, CLASS_ALMOST_EMPTY);
		obj_allocated = zs_stat_get(class, OBJ_ALLOCATED);
		obj_used = zs_stat_get(class, OBJ_USED);
		freeable = zs_can_compact(class);
		spin_unlock(&class->lock);

		objs_per_zspage = class->objs_per_zspage;
		pages_used = obj_allocated / objs_per_zspage *
				class->pages_per_zspage;

		seq_printf(s, " %5u %5u %11lu %12lu %13lu"
				" %10lu %10lu %16d %8lu\n",
			i, class->size, class_almost_full, class_almost_empty,
			obj_allocated, obj_used, pages_used,
			class->pages_per_zspage, freeable);

		total_class_almost_full += class_almost_full;
		total_class_almost_empty += class_almost_empty;
		total_objs += obj_allocated;
		total_used_objs += obj_used;
		total_pages += pages_used;
		total_freeable += freeable;
	}

	seq_puts(s, "\n");
	seq_printf(s, " %5s %5s %11lu %12lu %13lu %10lu %10lu %16s %8lu\n",
			"Total", "", total_class_almost_full,
			total_class_almost_empty, total_objs,
			total_used_objs, total_pages, "", total_freeable);

	return 0;
}

static int zs_stats_size_open(struct inode *inode, struct file *file)
{
	return single_open(file, zs_stats_size_show, inode->i_private);
}

static const struct file_operations zs_stat_size_ops = {
	.open           = zs_stats_size_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = single_release,
};

static void zs_pool_stat_create(struct zs_pool *pool, const char *name)
{
	struct dentry *entry;

	if (!zs_stat_root) {
		pr_warn("no root stat dir, not creating <%s> stat dir\n", name);
		return;
	}

	entry = debugfs_create_dir(name, zs_stat_root);
	if (!entry) {
		pr_warn("debugfs dir <%s> creation failed\n", name);
		return;
	}
	pool->stat_dentry = entry;

	entry = debugfs_create_file("classes", S_IFREG | S_IRUGO,
			pool->stat_dentry, pool, &zs_stat_size_ops);
	if (!entry) {
		pr_warn("%s: debugfs file entry <%s> creation failed\n",
				name, "classes");
		debugfs_remove_recursive(pool->stat_dentry);
		pool->stat_dentry = NULL;
	}
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
 * depending on how "full" they are. This was done so that we could
 * easily find empty or nearly empty zspages when we try to shrink
 * the pool (not yet implemented). This function returns fullness
 * status of the given page.
 */
static enum fullness_group get_fullness_group(struct size_class *class,
						struct zspage *zspage)
{
	int inuse, objs_per_zspage;
	enum fullness_group fg;

	inuse = get_zspage_inuse(zspage);
	objs_per_zspage = class->objs_per_zspage;

	if (inuse == 0)
		fg = ZS_EMPTY;
	else if (inuse == objs_per_zspage)
		fg = ZS_FULL;
	else if (inuse <= 3 * objs_per_zspage / fullness_threshold_frac)
		fg = ZS_ALMOST_EMPTY;
	else
		fg = ZS_ALMOST_FULL;

	return fg;
}

/*
 * Each size class maintains various freelists and zspages are assigned
 * to one of these freelists based on the number of live objects they
 * have. This functions inserts the given zspage into the freelist
 * identified by <class, fullness_group>.
 */
static void insert_zspage(struct size_class *class,
				struct zspage *zspage,
				enum fullness_group fullness)
{
	struct zspage *head;

	zs_stat_inc(class, fullness, 1);
	head = list_first_entry_or_null(&class->fullness_list[fullness],
					struct zspage, list);
	/*
	 * We want to see more ZS_FULL pages and less almost empty/full.
	 * Put pages with higher ->inuse first.
	 */
	if (head) {
		if (get_zspage_inuse(zspage) < get_zspage_inuse(head)) {
			list_add(&zspage->list, &head->list);
			return;
		}
	}
	list_add(&zspage->list, &class->fullness_list[fullness]);
}

/*
 * This function removes the given zspage from the freelist identified
 * by <class, fullness_group>.
 */
static void remove_zspage(struct size_class *class,
				struct zspage *zspage,
				enum fullness_group fullness)
{
	VM_BUG_ON(list_empty(&class->fullness_list[fullness]));
	VM_BUG_ON(is_zspage_isolated(zspage));

	list_del_init(&zspage->list);
	zs_stat_dec(class, fullness, 1);
}

/*
 * Each size class maintains zspages in different fullness groups depending
 * on the number of live objects they contain. When allocating or freeing
 * objects, the fullness status of the page can change, say, from ALMOST_FULL
 * to ALMOST_EMPTY when freeing an object. This function checks if such
 * a status change has occurred for the given page and accordingly moves the
 * page from the freelist of the old fullness group to that of the new
 * fullness group.
 */
static enum fullness_group fix_fullness_group(struct size_class *class,
						struct zspage *zspage)
{
	int class_idx;
	enum fullness_group currfg, newfg;

	get_zspage_mapping(zspage, &class_idx, &currfg);
	newfg = get_fullness_group(class, zspage);
	if (newfg == currfg)
		goto out;

	if (!is_zspage_isolated(zspage)) {
		remove_zspage(class, zspage, currfg);
		insert_zspage(class, zspage, newfg);
	}

	set_zspage_mapping(zspage, class_idx, newfg);

out:
	return newfg;
}

/*
 * We have to decide on how many pages to link together
 * to form a zspage for each size class. This is important
 * to reduce wastage due to unusable space left at end of
 * each zspage which is given as:
 *     wastage = Zp % class_size
 *     usage = Zp - wastage
 * where Zp = zspage size = k * PAGE_SIZE where k = 1, 2, ...
 *
 * For example, for size class of 3/8 * PAGE_SIZE, we should
 * link together 3 PAGE_SIZE sized pages to form a zspage
 * since then we can perfectly fit in 8 such objects.
 */
static int get_pages_per_zspage(int class_size)
{
	int i, max_usedpc = 0;
	/* zspage order which gives maximum used size per KB */
	int max_usedpc_order = 1;

	for (i = 1; i <= ZS_MAX_PAGES_PER_ZSPAGE; i++) {
		int zspage_size;
		int waste, usedpc;

		zspage_size = i * PAGE_SIZE;
		waste = zspage_size % class_size;
		usedpc = (zspage_size - waste) * 100 / zspage_size;

		if (usedpc > max_usedpc) {
			max_usedpc = usedpc;
			max_usedpc_order = i;
		}
	}

	return max_usedpc_order;
}

static struct zspage *get_zspage(struct page *page)
{
	struct zspage *zspage = (struct zspage *)page->private;

	BUG_ON(zspage->magic != ZSPAGE_MAGIC);
	return zspage;
}

static struct page *get_next_page(struct page *page)
{
	if (unlikely(PageHugeObject(page)))
		return NULL;

	return page->freelist;
}

/**
 * obj_to_location - get (<page>, <obj_idx>) from encoded object value
 * @page: page object resides in zspage
 * @obj_idx: object index
 */
static void obj_to_location(unsigned long obj, struct page **page,
				unsigned int *obj_idx)
{
	obj >>= OBJ_TAG_BITS;
	*page = pfn_to_page(obj >> OBJ_INDEX_BITS);
	*obj_idx = (obj & OBJ_INDEX_MASK);
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
	obj <<= OBJ_TAG_BITS;

	return obj;
}

static unsigned long handle_to_obj(unsigned long handle)
{
	return *(unsigned long *)handle;
}

static unsigned long obj_to_head(struct page *page, void *obj)
{
	if (unlikely(PageHugeObject(page))) {
		VM_BUG_ON_PAGE(!is_first_page(page), page);
		return page->index;
	} else
		return *(unsigned long *)obj;
}

static inline int testpin_tag(unsigned long handle)
{
	return bit_spin_is_locked(HANDLE_PIN_BIT, (unsigned long *)handle);
}

static inline int trypin_tag(unsigned long handle)
{
	return bit_spin_trylock(HANDLE_PIN_BIT, (unsigned long *)handle);
}

static void pin_tag(unsigned long handle)
{
	bit_spin_lock(HANDLE_PIN_BIT, (unsigned long *)handle);
}

static void unpin_tag(unsigned long handle)
{
	bit_spin_unlock(HANDLE_PIN_BIT, (unsigned long *)handle);
}

static void reset_page(struct page *page)
{
	__ClearPageMovable(page);
	ClearPagePrivate(page);
	set_page_private(page, 0);
	page_mapcount_reset(page);
	ClearPageHugeObject(page);
	page->freelist = NULL;
}

/*
 * To prevent zspage destroy during migration, zspage freeing should
 * hold locks of all pages in the zspage.
 */
void lock_zspage(struct zspage *zspage)
{
	struct page *page = get_first_page(zspage);

	do {
		lock_page(page);
	} while ((page = get_next_page(page)) != NULL);
}

int trylock_zspage(struct zspage *zspage)
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
	enum fullness_group fg;
	unsigned int class_idx;

	get_zspage_mapping(zspage, &class_idx, &fg);

	assert_spin_locked(&class->lock);

	VM_BUG_ON(get_zspage_inuse(zspage));
	VM_BUG_ON(fg != ZS_EMPTY);

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

	zs_stat_dec(class, OBJ_ALLOCATED, class->objs_per_zspage);
	atomic_long_sub(class->pages_per_zspage,
					&pool->pages_allocated);
}

static void free_zspage(struct zs_pool *pool, struct size_class *class,
				struct zspage *zspage)
{
	VM_BUG_ON(get_zspage_inuse(zspage));
	VM_BUG_ON(list_empty(&zspage->list));

	if (!trylock_zspage(zspage)) {
		kick_deferred_free(pool);
		return;
	}

	remove_zspage(class, zspage, ZS_EMPTY);
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
			link->next = -1 << OBJ_TAG_BITS;
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
	 * 1. all pages are linked together using page->freelist
	 * 2. each sub-page point to zspage using page->private
	 *
	 * we set PG_private to identify the first page (i.e. no other sub-page
	 * has this flag set).
	 */
	for (i = 0; i < nr_pages; i++) {
		page = pages[i];
		set_page_private(page, (unsigned long)zspage);
		page->freelist = NULL;
		if (i == 0) {
			zspage->first_page = page;
			SetPagePrivate(page);
			if (unlikely(class->objs_per_zspage == 1 &&
					class->pages_per_zspage == 1))
				SetPageHugeObject(page);
		} else {
			prev_page->freelist = page;
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

	memset(zspage, 0, sizeof(struct zspage));
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

	return zspage;
}

static struct zspage *find_get_zspage(struct size_class *class)
{
	int i;
	struct zspage *zspage;

	for (i = ZS_ALMOST_FULL; i >= ZS_EMPTY; i--) {
		zspage = list_first_entry_or_null(&class->fullness_list[i],
				struct zspage, list);
		if (zspage)
			break;
	}

	return zspage;
}

#ifdef CONFIG_PGTABLE_MAPPING
static inline int __zs_cpu_up(struct mapping_area *area)
{
	/*
	 * Make sure we don't leak memory if a cpu UP notification
	 * and zs_init() race and both call zs_cpu_up() on the same cpu
	 */
	if (area->vm)
		return 0;
	area->vm = alloc_vm_area(PAGE_SIZE * 2, NULL);
	if (!area->vm)
		return -ENOMEM;
	return 0;
}

static inline void __zs_cpu_down(struct mapping_area *area)
{
	if (area->vm)
		free_vm_area(area->vm);
	area->vm = NULL;
}

static inline void *__zs_map_object(struct mapping_area *area,
				struct page *pages[2], int off, int size)
{
	BUG_ON(map_vm_area(area->vm, PAGE_KERNEL, pages));
	area->vm_addr = area->vm->addr;
	return area->vm_addr + off;
}

static inline void __zs_unmap_object(struct mapping_area *area,
				struct page *pages[2], int off, int size)
{
	unsigned long addr = (unsigned long)area->vm_addr;

	unmap_kernel_range(addr, PAGE_SIZE * 2);
}

#else /* CONFIG_PGTABLE_MAPPING */

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

#endif /* CONFIG_PGTABLE_MAPPING */

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

static void __init init_zs_size_classes(void)
{
	int nr;

	nr = (ZS_MAX_ALLOC_SIZE - ZS_MIN_ALLOC_SIZE) / ZS_SIZE_CLASS_DELTA + 1;
	if ((ZS_MAX_ALLOC_SIZE - ZS_MIN_ALLOC_SIZE) % ZS_SIZE_CLASS_DELTA)
		nr += 1;

	zs_size_classes = nr;
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

unsigned long zs_get_total_pages(struct zs_pool *pool)
{
	return atomic_long_read(&pool->pages_allocated);
}
EXPORT_SYMBOL_GPL(zs_get_total_pages);

/**
 * zs_map_object - get address of allocated object from handle.
 * @pool: pool from which the object was allocated
 * @handle: handle returned from zs_malloc
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

	unsigned int class_idx;
	enum fullness_group fg;
	struct size_class *class;
	struct mapping_area *area;
	struct page *pages[2];
	void *ret;

	/*
	 * Because we use per-cpu mapping areas shared among the
	 * pools/users, we can't allow mapping in interrupt context
	 * because it can corrupt another users mappings.
	 */
	WARN_ON_ONCE(in_interrupt());

	/* From now on, migration cannot move the object */
	pin_tag(handle);

	obj = handle_to_obj(handle);
	obj_to_location(obj, &page, &obj_idx);
	zspage = get_zspage(page);

	/* migration cannot move any subpage in this zspage */
	migrate_read_lock(zspage);

	get_zspage_mapping(zspage, &class_idx, &fg);
	class = pool->size_class[class_idx];
	off = (class->size * obj_idx) & ~PAGE_MASK;

	area = &get_cpu_var(zs_map_area);
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
	if (likely(!PageHugeObject(page)))
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

	unsigned int class_idx;
	enum fullness_group fg;
	struct size_class *class;
	struct mapping_area *area;

	obj = handle_to_obj(handle);
	obj_to_location(obj, &page, &obj_idx);
	zspage = get_zspage(page);
	get_zspage_mapping(zspage, &class_idx, &fg);
	class = pool->size_class[class_idx];
	off = (class->size * obj_idx) & ~PAGE_MASK;

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
	put_cpu_var(zs_map_area);

	migrate_read_unlock(zspage);
	unpin_tag(handle);
}
EXPORT_SYMBOL_GPL(zs_unmap_object);

static unsigned long obj_malloc(struct size_class *class,
				struct zspage *zspage, unsigned long handle)
{
	int i, nr_page, offset;
	unsigned long obj;
	struct link_free *link;

	struct page *m_page;
	unsigned long m_offset;
	void *vaddr;

	handle |= OBJ_ALLOCATED_TAG;
	obj = get_freeobj(zspage);

	offset = obj * class->size;
	nr_page = offset >> PAGE_SHIFT;
	m_offset = offset & ~PAGE_MASK;
	m_page = get_first_page(zspage);

	for (i = 0; i < nr_page; i++)
		m_page = get_next_page(m_page);

	vaddr = kmap_atomic(m_page);
	link = (struct link_free *)vaddr + m_offset / sizeof(*link);
	set_freeobj(zspage, link->next >> OBJ_TAG_BITS);
	if (likely(!PageHugeObject(m_page)))
		/* record handle in the header of allocated chunk */
		link->handle = handle;
	else
		/* record handle to page->index */
		zspage->first_page->index = handle;

	kunmap_atomic(vaddr);
	mod_zspage_inuse(zspage, 1);
	zs_stat_inc(class, OBJ_USED, 1);

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
 * otherwise 0.
 * Allocation requests with size > ZS_MAX_ALLOC_SIZE will fail.
 */
unsigned long zs_malloc(struct zs_pool *pool, size_t size, gfp_t gfp)
{
	unsigned long handle, obj;
	struct size_class *class;
	enum fullness_group newfg;
	struct zspage *zspage;

	if (unlikely(!size || size > ZS_MAX_ALLOC_SIZE))
		return 0;

	handle = cache_alloc_handle(pool, gfp);
	if (!handle)
		return 0;

	/* extra space in chunk to keep the handle */
	size += ZS_HANDLE_SIZE;
	class = pool->size_class[get_size_class_index(size)];

	spin_lock(&class->lock);
	zspage = find_get_zspage(class);
	if (likely(zspage)) {
		obj = obj_malloc(class, zspage, handle);
		/* Now move the zspage to another fullness group, if required */
		fix_fullness_group(class, zspage);
		record_obj(handle, obj);
		spin_unlock(&class->lock);

		return handle;
	}

	spin_unlock(&class->lock);

	zspage = alloc_zspage(pool, class, gfp);
	if (!zspage) {
		cache_free_handle(pool, handle);
		return 0;
	}

	spin_lock(&class->lock);
	obj = obj_malloc(class, zspage, handle);
	newfg = get_fullness_group(class, zspage);
	insert_zspage(class, zspage, newfg);
	set_zspage_mapping(zspage, class->index, newfg);
	record_obj(handle, obj);
	atomic_long_add(class->pages_per_zspage,
				&pool->pages_allocated);
	zs_stat_inc(class, OBJ_ALLOCATED, class->objs_per_zspage);

	/* We completely set up zspage so mark them as movable */
	SetZsPageMovable(pool, zspage);
	spin_unlock(&class->lock);

	return handle;
}
EXPORT_SYMBOL_GPL(zs_malloc);

static void obj_free(struct size_class *class, unsigned long obj)
{
	struct link_free *link;
	struct zspage *zspage;
	struct page *f_page;
	unsigned long f_offset;
	unsigned int f_objidx;
	void *vaddr;

	obj &= ~OBJ_ALLOCATED_TAG;
	obj_to_location(obj, &f_page, &f_objidx);
	f_offset = (class->size * f_objidx) & ~PAGE_MASK;
	zspage = get_zspage(f_page);

	vaddr = kmap_atomic(f_page);

	/* Insert this object in containing zspage's freelist */
	link = (struct link_free *)(vaddr + f_offset);
	link->next = get_freeobj(zspage) << OBJ_TAG_BITS;
	kunmap_atomic(vaddr);
	set_freeobj(zspage, f_objidx);
	mod_zspage_inuse(zspage, -1);
	zs_stat_dec(class, OBJ_USED, 1);
}

void zs_free(struct zs_pool *pool, unsigned long handle)
{
	struct zspage *zspage;
	struct page *f_page;
	unsigned long obj;
	unsigned int f_objidx;
	int class_idx;
	struct size_class *class;
	enum fullness_group fullness;
	bool isolated;

	if (unlikely(!handle))
		return;

	pin_tag(handle);
	obj = handle_to_obj(handle);
	obj_to_location(obj, &f_page, &f_objidx);
	zspage = get_zspage(f_page);

	migrate_read_lock(zspage);

	get_zspage_mapping(zspage, &class_idx, &fullness);
	class = pool->size_class[class_idx];

	spin_lock(&class->lock);
	obj_free(class, obj);
	fullness = fix_fullness_group(class, zspage);
	if (fullness != ZS_EMPTY) {
		migrate_read_unlock(zspage);
		goto out;
	}

	isolated = is_zspage_isolated(zspage);
	migrate_read_unlock(zspage);
	/* If zspage is isolated, zs_page_putback will free the zspage */
	if (likely(!isolated))
		free_zspage(pool, class, zspage);
out:

	spin_unlock(&class->lock);
	unpin_tag(handle);
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

	s_off = (class->size * s_objidx) & ~PAGE_MASK;
	d_off = (class->size * d_objidx) & ~PAGE_MASK;

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
	unsigned long head;
	int offset = 0;
	int index = *obj_idx;
	unsigned long handle = 0;
	void *addr = kmap_atomic(page);

	offset = get_first_obj_offset(page);
	offset += class->size * index;

	while (offset < PAGE_SIZE) {
		head = obj_to_head(page, addr + offset);
		if (head & OBJ_ALLOCATED_TAG) {
			handle = head & ~OBJ_ALLOCATED_TAG;
			if (trypin_tag(handle))
				break;
			handle = 0;
		}

		offset += class->size;
		index++;
	}

	kunmap_atomic(addr);

	*obj_idx = index;

	return handle;
}

struct zs_compact_control {
	/* Source spage for migration which could be a subpage of zspage */
	struct page *s_page;
	/* Destination page for migration which should be a first page
	 * of zspage. */
	struct page *d_page;
	 /* Starting object index within @s_page which used for live object
	  * in the subpage. */
	int obj_idx;
};

static int migrate_zspage(struct zs_pool *pool, struct size_class *class,
				struct zs_compact_control *cc)
{
	unsigned long used_obj, free_obj;
	unsigned long handle;
	struct page *s_page = cc->s_page;
	struct page *d_page = cc->d_page;
	int obj_idx = cc->obj_idx;
	int ret = 0;

	while (1) {
		handle = find_alloced_obj(class, s_page, &obj_idx);
		if (!handle) {
			s_page = get_next_page(s_page);
			if (!s_page)
				break;
			obj_idx = 0;
			continue;
		}

		/* Stop if there is no more space */
		if (zspage_full(class, get_zspage(d_page))) {
			unpin_tag(handle);
			ret = -ENOMEM;
			break;
		}

		used_obj = handle_to_obj(handle);
		free_obj = obj_malloc(class, get_zspage(d_page), handle);
		zs_object_copy(class, free_obj, used_obj);
		obj_idx++;
		/*
		 * record_obj updates handle's value to free_obj and it will
		 * invalidate lock bit(ie, HANDLE_PIN_BIT) of handle, which
		 * breaks synchronization using pin_tag(e,g, zs_free) so
		 * let's keep the lock bit.
		 */
		free_obj |= BIT(HANDLE_PIN_BIT);
		record_obj(handle, free_obj);
		unpin_tag(handle);
		obj_free(class, used_obj);
	}

	/* Remember last position in this iteration */
	cc->s_page = s_page;
	cc->obj_idx = obj_idx;

	return ret;
}

static struct zspage *isolate_zspage(struct size_class *class, bool source)
{
	int i;
	struct zspage *zspage;
	enum fullness_group fg[2] = {ZS_ALMOST_EMPTY, ZS_ALMOST_FULL};

	if (!source) {
		fg[0] = ZS_ALMOST_FULL;
		fg[1] = ZS_ALMOST_EMPTY;
	}

	for (i = 0; i < 2; i++) {
		zspage = list_first_entry_or_null(&class->fullness_list[fg[i]],
							struct zspage, list);
		if (zspage) {
			VM_BUG_ON(is_zspage_isolated(zspage));
			remove_zspage(class, zspage, fg[i]);
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
 * Return @zspage's fullness_group
 */
static enum fullness_group putback_zspage(struct size_class *class,
			struct zspage *zspage)
{
	enum fullness_group fullness;

	VM_BUG_ON(is_zspage_isolated(zspage));

	fullness = get_fullness_group(class, zspage);
	insert_zspage(class, zspage, fullness);
	set_zspage_mapping(zspage, class->index, fullness);

	return fullness;
}

#ifdef CONFIG_COMPACTION
static struct dentry *zs_mount(struct file_system_type *fs_type,
				int flags, const char *dev_name, void *data)
{
	static const struct dentry_operations ops = {
		.d_dname = simple_dname,
	};

	return mount_pseudo(fs_type, "zsmalloc:", NULL, &ops, ZSMALLOC_MAGIC);
}

static struct file_system_type zsmalloc_fs = {
	.name		= "zsmalloc",
	.mount		= zs_mount,
	.kill_sb	= kill_anon_super,
};

static int zsmalloc_mount(void)
{
	int ret = 0;

	zsmalloc_mnt = kern_mount(&zsmalloc_fs);
	if (IS_ERR(zsmalloc_mnt))
		ret = PTR_ERR(zsmalloc_mnt);

	return ret;
}

static void zsmalloc_unmount(void)
{
	kern_unmount(zsmalloc_mnt);
}

static void migrate_lock_init(struct zspage *zspage)
{
	rwlock_init(&zspage->lock);
}

static void migrate_read_lock(struct zspage *zspage)
{
	read_lock(&zspage->lock);
}

static void migrate_read_unlock(struct zspage *zspage)
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

/* Number of isolated subpage for *page migration* in this zspage */
static void inc_zspage_isolation(struct zspage *zspage)
{
	zspage->isolated++;
}

static void dec_zspage_isolation(struct zspage *zspage)
{
	zspage->isolated--;
}

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
	if (unlikely(PageHugeObject(oldpage)))
		newpage->index = oldpage->index;
	__SetPageMovable(newpage, page_mapping(oldpage));
}

bool zs_page_isolate(struct page *page, isolate_mode_t mode)
{
	struct zs_pool *pool;
	struct size_class *class;
	int class_idx;
	enum fullness_group fullness;
	struct zspage *zspage;
	struct address_space *mapping;

	/*
	 * Page is locked so zspage couldn't be destroyed. For detail, look at
	 * lock_zspage in free_zspage.
	 */
	VM_BUG_ON_PAGE(!PageMovable(page), page);
	VM_BUG_ON_PAGE(PageIsolated(page), page);

	zspage = get_zspage(page);

	/*
	 * Without class lock, fullness could be stale while class_idx is okay
	 * because class_idx is constant unless page is freed so we should get
	 * fullness again under class lock.
	 */
	get_zspage_mapping(zspage, &class_idx, &fullness);
	mapping = page_mapping(page);
	pool = mapping->private_data;
	class = pool->size_class[class_idx];

	spin_lock(&class->lock);
	if (get_zspage_inuse(zspage) == 0) {
		spin_unlock(&class->lock);
		return false;
	}

	/* zspage is isolated for object migration */
	if (list_empty(&zspage->list) && !is_zspage_isolated(zspage)) {
		spin_unlock(&class->lock);
		return false;
	}

	/*
	 * If this is first time isolation for the zspage, isolate zspage from
	 * size_class to prevent further object allocation from the zspage.
	 */
	if (!list_empty(&zspage->list) && !is_zspage_isolated(zspage)) {
		get_zspage_mapping(zspage, &class_idx, &fullness);
		remove_zspage(class, zspage, fullness);
	}

	inc_zspage_isolation(zspage);
	spin_unlock(&class->lock);

	return true;
}

int zs_page_migrate(struct address_space *mapping, struct page *newpage,
		struct page *page, enum migrate_mode mode)
{
	struct zs_pool *pool;
	struct size_class *class;
	int class_idx;
	enum fullness_group fullness;
	struct zspage *zspage;
	struct page *dummy;
	void *s_addr, *d_addr, *addr;
	int offset, pos;
	unsigned long handle, head;
	unsigned long old_obj, new_obj;
	unsigned int obj_idx;
	int ret = -EAGAIN;

	VM_BUG_ON_PAGE(!PageMovable(page), page);
	VM_BUG_ON_PAGE(!PageIsolated(page), page);

	zspage = get_zspage(page);

	/* Concurrent compactor cannot migrate any subpage in zspage */
	migrate_write_lock(zspage);
	get_zspage_mapping(zspage, &class_idx, &fullness);
	pool = mapping->private_data;
	class = pool->size_class[class_idx];
	offset = get_first_obj_offset(page);

	spin_lock(&class->lock);
	if (!get_zspage_inuse(zspage)) {
		ret = -EBUSY;
		goto unlock_class;
	}

	pos = offset;
	s_addr = kmap_atomic(page);
	while (pos < PAGE_SIZE) {
		head = obj_to_head(page, s_addr + pos);
		if (head & OBJ_ALLOCATED_TAG) {
			handle = head & ~OBJ_ALLOCATED_TAG;
			if (!trypin_tag(handle))
				goto unpin_objects;
		}
		pos += class->size;
	}

	/*
	 * Here, any user cannot access all objects in the zspage so let's move.
	 */
	d_addr = kmap_atomic(newpage);
	memcpy(d_addr, s_addr, PAGE_SIZE);
	kunmap_atomic(d_addr);

	for (addr = s_addr + offset; addr < s_addr + pos;
					addr += class->size) {
		head = obj_to_head(page, addr);
		if (head & OBJ_ALLOCATED_TAG) {
			handle = head & ~OBJ_ALLOCATED_TAG;
			if (!testpin_tag(handle))
				BUG();

			old_obj = handle_to_obj(handle);
			obj_to_location(old_obj, &dummy, &obj_idx);
			new_obj = (unsigned long)location_to_obj(newpage,
								obj_idx);
			new_obj |= BIT(HANDLE_PIN_BIT);
			record_obj(handle, new_obj);
		}
	}

	replace_sub_page(class, zspage, newpage, page);
	get_page(newpage);

	dec_zspage_isolation(zspage);

	/*
	 * Page migration is done so let's putback isolated zspage to
	 * the list if @page is final isolated subpage in the zspage.
	 */
	if (!is_zspage_isolated(zspage))
		putback_zspage(class, zspage);

	reset_page(page);
	put_page(page);
	page = newpage;

	ret = MIGRATEPAGE_SUCCESS;
unpin_objects:
	for (addr = s_addr + offset; addr < s_addr + pos;
						addr += class->size) {
		head = obj_to_head(page, addr);
		if (head & OBJ_ALLOCATED_TAG) {
			handle = head & ~OBJ_ALLOCATED_TAG;
			if (!testpin_tag(handle))
				BUG();
			unpin_tag(handle);
		}
	}
	kunmap_atomic(s_addr);
unlock_class:
	spin_unlock(&class->lock);
	migrate_write_unlock(zspage);

	return ret;
}

void zs_page_putback(struct page *page)
{
	struct zs_pool *pool;
	struct size_class *class;
	int class_idx;
	enum fullness_group fg;
	struct address_space *mapping;
	struct zspage *zspage;

	VM_BUG_ON_PAGE(!PageMovable(page), page);
	VM_BUG_ON_PAGE(!PageIsolated(page), page);

	zspage = get_zspage(page);
	get_zspage_mapping(zspage, &class_idx, &fg);
	mapping = page_mapping(page);
	pool = mapping->private_data;
	class = pool->size_class[class_idx];

	spin_lock(&class->lock);
	dec_zspage_isolation(zspage);
	if (!is_zspage_isolated(zspage)) {
		fg = putback_zspage(class, zspage);
		/*
		 * Due to page_lock, we cannot free zspage immediately
		 * so let's defer.
		 */
		if (fg == ZS_EMPTY)
			schedule_work(&pool->free_work);
	}
	spin_unlock(&class->lock);
}

const struct address_space_operations zsmalloc_aops = {
	.isolate_page = zs_page_isolate,
	.migratepage = zs_page_migrate,
	.putback_page = zs_page_putback,
};

static int zs_register_migration(struct zs_pool *pool)
{
	pool->inode = alloc_anon_inode(zsmalloc_mnt->mnt_sb);
	if (IS_ERR(pool->inode)) {
		pool->inode = NULL;
		return 1;
	}

	pool->inode->i_mapping->private_data = pool;
	pool->inode->i_mapping->a_ops = &zsmalloc_aops;
	return 0;
}

static void zs_unregister_migration(struct zs_pool *pool)
{
	flush_work(&pool->free_work);
	iput(pool->inode);
}

/*
 * Caller should hold page_lock of all pages in the zspage
 * In here, we cannot use zspage meta data.
 */
static void async_free_zspage(struct work_struct *work)
{
	int i;
	struct size_class *class;
	unsigned int class_idx;
	enum fullness_group fullness;
	struct zspage *zspage, *tmp;
	LIST_HEAD(free_pages);
	struct zs_pool *pool = container_of(work, struct zs_pool,
					free_work);

	for (i = 0; i < zs_size_classes; i++) {
		class = pool->size_class[i];
		if (class->index != i)
			continue;

		spin_lock(&class->lock);
		list_splice_init(&class->fullness_list[ZS_EMPTY], &free_pages);
		spin_unlock(&class->lock);
	}


	list_for_each_entry_safe(zspage, tmp, &free_pages, list) {
		list_del(&zspage->list);
		lock_zspage(zspage);

		get_zspage_mapping(zspage, &class_idx, &fullness);
		VM_BUG_ON(fullness != ZS_EMPTY);
		class = pool->size_class[class_idx];
		spin_lock(&class->lock);
		__free_zspage(pool, pool->size_class[class_idx], zspage);
		spin_unlock(&class->lock);
	}
};

static void kick_deferred_free(struct zs_pool *pool)
{
	schedule_work(&pool->free_work);
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
		__SetPageMovable(page, pool->inode->i_mapping);
		unlock_page(page);
	} while ((page = get_next_page(page)) != NULL);
}
#endif

/*
 *
 * Based on the number of unused allocated objects calculate
 * and return the number of pages that we can free.
 */
static unsigned long zs_can_compact(struct size_class *class)
{
	unsigned long obj_wasted;
	unsigned long obj_allocated = zs_stat_get(class, OBJ_ALLOCATED);
	unsigned long obj_used = zs_stat_get(class, OBJ_USED);

	if (obj_allocated <= obj_used)
		return 0;

	obj_wasted = obj_allocated - obj_used;
	obj_wasted /= class->objs_per_zspage;

	return obj_wasted * class->pages_per_zspage;
}

static void __zs_compact(struct zs_pool *pool, struct size_class *class)
{
	struct zs_compact_control cc;
	struct zspage *src_zspage;
	struct zspage *dst_zspage = NULL;

	spin_lock(&class->lock);
	while ((src_zspage = isolate_zspage(class, true))) {

		if (!zs_can_compact(class))
			break;

		cc.obj_idx = 0;
		cc.s_page = get_first_page(src_zspage);

		while ((dst_zspage = isolate_zspage(class, false))) {
			cc.d_page = get_first_page(dst_zspage);
			/*
			 * If there is no more space in dst_page, resched
			 * and see if anyone had allocated another zspage.
			 */
			if (!migrate_zspage(pool, class, &cc))
				break;

			putback_zspage(class, dst_zspage);
		}

		/* Stop if we couldn't find slot */
		if (dst_zspage == NULL)
			break;

		putback_zspage(class, dst_zspage);
		if (putback_zspage(class, src_zspage) == ZS_EMPTY) {
			free_zspage(pool, class, src_zspage);
			pool->stats.pages_compacted += class->pages_per_zspage;
		}
		spin_unlock(&class->lock);
		cond_resched();
		spin_lock(&class->lock);
	}

	if (src_zspage)
		putback_zspage(class, src_zspage);

	spin_unlock(&class->lock);
}

unsigned long zs_compact(struct zs_pool *pool)
{
	int i;
	struct size_class *class;

	for (i = zs_size_classes - 1; i >= 0; i--) {
		class = pool->size_class[i];
		if (!class)
			continue;
		if (class->index != i)
			continue;
		__zs_compact(pool, class);
	}

	return pool->stats.pages_compacted;
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
	struct zs_pool *pool = container_of(shrinker, struct zs_pool,
			shrinker);

	pages_freed = pool->stats.pages_compacted;
	/*
	 * Compact classes and calculate compaction delta.
	 * Can run concurrently with a manually triggered
	 * (by user) compaction.
	 */
	pages_freed = zs_compact(pool) - pages_freed;

	return pages_freed ? pages_freed : SHRINK_STOP;
}

static unsigned long zs_shrinker_count(struct shrinker *shrinker,
		struct shrink_control *sc)
{
	int i;
	struct size_class *class;
	unsigned long pages_to_free = 0;
	struct zs_pool *pool = container_of(shrinker, struct zs_pool,
			shrinker);

	for (i = zs_size_classes - 1; i >= 0; i--) {
		class = pool->size_class[i];
		if (!class)
			continue;
		if (class->index != i)
			continue;

		pages_to_free += zs_can_compact(class);
	}

	return pages_to_free;
}

static void zs_unregister_shrinker(struct zs_pool *pool)
{
	if (pool->shrinker_enabled) {
		unregister_shrinker(&pool->shrinker);
		pool->shrinker_enabled = false;
	}
}

static int zs_register_shrinker(struct zs_pool *pool)
{
	pool->shrinker.scan_objects = zs_shrinker_scan;
	pool->shrinker.count_objects = zs_shrinker_count;
	pool->shrinker.batch = 0;
	pool->shrinker.seeks = DEFAULT_SEEKS;

	return register_shrinker(&pool->shrinker);
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
	pool->size_class = kcalloc(zs_size_classes, sizeof(struct size_class *),
			GFP_KERNEL);
	if (!pool->size_class) {
		kfree(pool);
		return NULL;
	}

	pool->name = kstrdup(name, GFP_KERNEL);
	if (!pool->name)
		goto err;

	if (create_cache(pool))
		goto err;

	/*
	 * Iterate reversely, because, size of size_class that we want to use
	 * for merging should be larger or equal to current size.
	 */
	for (i = zs_size_classes - 1; i >= 0; i--) {
		int size;
		int pages_per_zspage;
		int objs_per_zspage;
		struct size_class *class;
		int fullness = 0;

		size = ZS_MIN_ALLOC_SIZE + i * ZS_SIZE_CLASS_DELTA;
		if (size > ZS_MAX_ALLOC_SIZE)
			size = ZS_MAX_ALLOC_SIZE;
		pages_per_zspage = get_pages_per_zspage(size);
		objs_per_zspage = pages_per_zspage * PAGE_SIZE / size;

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
		spin_lock_init(&class->lock);
		pool->size_class[i] = class;
		for (fullness = ZS_EMPTY; fullness < NR_ZS_FULLNESS;
							fullness++)
			INIT_LIST_HEAD(&class->fullness_list[fullness]);

		prev_class = class;
	}

	/* debug only, don't abort if it fails */
	zs_pool_stat_create(pool, name);

	if (zs_register_migration(pool))
		goto err;

	/*
	 * Not critical, we still can use the pool
	 * and user can trigger compaction manually.
	 */
	if (zs_register_shrinker(pool) == 0)
		pool->shrinker_enabled = true;
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
	zs_unregister_migration(pool);
	zs_pool_stat_destroy(pool);

	for (i = 0; i < zs_size_classes; i++) {
		int fg;
		struct size_class *class = pool->size_class[i];

		if (!class)
			continue;

		if (class->index != i)
			continue;

		for (fg = ZS_EMPTY; fg < NR_ZS_FULLNESS; fg++) {
			if (!list_empty(&class->fullness_list[fg])) {
				pr_info("Freeing non-empty class with size %db, fullness group %d\n",
					class->size, fg);
			}
		}
		kfree(class);
	}

	destroy_cache(pool);
	kfree(pool->size_class);
	kfree(pool->name);
	kfree(pool);
}
EXPORT_SYMBOL_GPL(zs_destroy_pool);

static int __init zs_init(void)
{
	int ret;

	ret = zsmalloc_mount();
	if (ret)
		goto out;

	ret = cpuhp_setup_state(CPUHP_MM_ZS_PREPARE, "mm/zsmalloc:prepare",
				zs_cpu_prepare, zs_cpu_dead);
	if (ret)
		goto hp_setup_fail;

	init_zs_size_classes();

#ifdef CONFIG_ZPOOL
	zpool_register_driver(&zs_zpool_driver);
#endif

	zs_stat_init();

	return 0;

hp_setup_fail:
	zsmalloc_unmount();
out:
	return ret;
}

static void __exit zs_exit(void)
{
#ifdef CONFIG_ZPOOL
	zpool_unregister_driver(&zs_zpool_driver);
#endif
	zsmalloc_unmount();
	cpuhp_remove_state(CPUHP_MM_ZS_PREPARE);

	zs_stat_exit();
}

module_init(zs_init);
module_exit(zs_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Nitin Gupta <ngupta@vflare.org>");
