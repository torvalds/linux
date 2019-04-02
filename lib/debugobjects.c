/*
 * Generic infrastructure for lifetime deging of objects.
 *
 * Started by Thomas Gleixner
 *
 * Copyright (C) 2008, Thomas Gleixner <tglx@linutronix.de>
 *
 * For licencing details see kernel-base/COPYING
 */

#define pr_fmt(fmt) "ODE: " fmt

#include <linux/deobjects.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/seq_file.h>
#include <linux/defs.h>
#include <linux/slab.h>
#include <linux/hash.h>
#include <linux/kmemleak.h>

#define ODE_HASH_BITS	14
#define ODE_HASH_SIZE	(1 << ODE_HASH_BITS)

#define ODE_POOL_SIZE	1024
#define ODE_POOL_MIN_LEVEL	256

#define ODE_CHUNK_SHIFT	PAGE_SHIFT
#define ODE_CHUNK_SIZE	(1 << ODE_CHUNK_SHIFT)
#define ODE_CHUNK_MASK	(~(ODE_CHUNK_SIZE - 1))

struct de_bucket {
	struct hlist_head	list;
	raw_spinlock_t		lock;
};

static struct de_bucket	obj_hash[ODE_HASH_SIZE];

static struct de_obj		obj_static_pool[ODE_POOL_SIZE] __initdata;

static DEFINE_RAW_SPINLOCK(pool_lock);

static HLIST_HEAD(obj_pool);
static HLIST_HEAD(obj_to_free);

static int			obj_pool_min_free = ODE_POOL_SIZE;
static int			obj_pool_free = ODE_POOL_SIZE;
static int			obj_pool_used;
static int			obj_pool_max_used;
/* The number of objs on the global free list */
static int			obj_nr_tofree;
static struct kmem_cache	*obj_cache;

static int			de_objects_maxchain __read_mostly;
static int __maybe_unused	de_objects_maxchecked __read_mostly;
static int			de_objects_fixups __read_mostly;
static int			de_objects_warnings __read_mostly;
static int			de_objects_enabled __read_mostly
				= CONFIG_DE_OBJECTS_ENABLE_DEFAULT;
static int			de_objects_pool_size __read_mostly
				= ODE_POOL_SIZE;
static int			de_objects_pool_min_level __read_mostly
				= ODE_POOL_MIN_LEVEL;
static struct de_obj_descr	*descr_test  __read_mostly;

/*
 * Track numbers of kmem_cache_alloc()/free() calls done.
 */
static int			de_objects_allocated;
static int			de_objects_freed;

static void free_obj_work(struct work_struct *work);
static DECLARE_WORK(de_obj_work, free_obj_work);

static int __init enable_object_de(char *str)
{
	de_objects_enabled = 1;
	return 0;
}

static int __init disable_object_de(char *str)
{
	de_objects_enabled = 0;
	return 0;
}

early_param("de_objects", enable_object_de);
early_param("no_de_objects", disable_object_de);

static const char *obj_states[ODE_STATE_MAX] = {
	[ODE_STATE_NONE]		= "none",
	[ODE_STATE_INIT]		= "initialized",
	[ODE_STATE_INACTIVE]		= "inactive",
	[ODE_STATE_ACTIVE]		= "active",
	[ODE_STATE_DESTROYED]	= "destroyed",
	[ODE_STATE_NOTAVAILABLE]	= "not available",
};

static void fill_pool(void)
{
	gfp_t gfp = GFP_ATOMIC | __GFP_NORETRY | __GFP_NOWARN;
	struct de_obj *new, *obj;
	unsigned long flags;

	if (likely(obj_pool_free >= de_objects_pool_min_level))
		return;

	/*
	 * Reuse objs from the global free list; they will be reinitialized
	 * when allocating.
	 */
	while (obj_nr_tofree && (obj_pool_free < obj_pool_min_free)) {
		raw_spin_lock_irqsave(&pool_lock, flags);
		/*
		 * Recheck with the lock held as the worker thread might have
		 * won the race and freed the global free list already.
		 */
		if (obj_nr_tofree) {
			obj = hlist_entry(obj_to_free.first, typeof(*obj), node);
			hlist_del(&obj->node);
			obj_nr_tofree--;
			hlist_add_head(&obj->node, &obj_pool);
			obj_pool_free++;
		}
		raw_spin_unlock_irqrestore(&pool_lock, flags);
	}

	if (unlikely(!obj_cache))
		return;

	while (obj_pool_free < de_objects_pool_min_level) {

		new = kmem_cache_zalloc(obj_cache, gfp);
		if (!new)
			return;

		raw_spin_lock_irqsave(&pool_lock, flags);
		hlist_add_head(&new->node, &obj_pool);
		de_objects_allocated++;
		obj_pool_free++;
		raw_spin_unlock_irqrestore(&pool_lock, flags);
	}
}

/*
 * Lookup an object in the hash bucket.
 */
static struct de_obj *lookup_object(void *addr, struct de_bucket *b)
{
	struct de_obj *obj;
	int cnt = 0;

	hlist_for_each_entry(obj, &b->list, node) {
		cnt++;
		if (obj->object == addr)
			return obj;
	}
	if (cnt > de_objects_maxchain)
		de_objects_maxchain = cnt;

	return NULL;
}

/*
 * Allocate a new object. If the pool is empty, switch off the deger.
 * Must be called with interrupts disabled.
 */
static struct de_obj *
alloc_object(void *addr, struct de_bucket *b, struct de_obj_descr *descr)
{
	struct de_obj *obj = NULL;

	raw_spin_lock(&pool_lock);
	if (obj_pool.first) {
		obj	    = hlist_entry(obj_pool.first, typeof(*obj), node);

		obj->object = addr;
		obj->descr  = descr;
		obj->state  = ODE_STATE_NONE;
		obj->astate = 0;
		hlist_del(&obj->node);

		hlist_add_head(&obj->node, &b->list);

		obj_pool_used++;
		if (obj_pool_used > obj_pool_max_used)
			obj_pool_max_used = obj_pool_used;

		obj_pool_free--;
		if (obj_pool_free < obj_pool_min_free)
			obj_pool_min_free = obj_pool_free;
	}
	raw_spin_unlock(&pool_lock);

	return obj;
}

/*
 * workqueue function to free objects.
 *
 * To reduce contention on the global pool_lock, the actual freeing of
 * de objects will be delayed if the pool_lock is busy.
 */
static void free_obj_work(struct work_struct *work)
{
	struct hlist_node *tmp;
	struct de_obj *obj;
	unsigned long flags;
	HLIST_HEAD(tofree);

	if (!raw_spin_trylock_irqsave(&pool_lock, flags))
		return;

	/*
	 * The objs on the pool list might be allocated before the work is
	 * run, so recheck if pool list it full or not, if not fill pool
	 * list from the global free list
	 */
	while (obj_nr_tofree && obj_pool_free < de_objects_pool_size) {
		obj = hlist_entry(obj_to_free.first, typeof(*obj), node);
		hlist_del(&obj->node);
		hlist_add_head(&obj->node, &obj_pool);
		obj_pool_free++;
		obj_nr_tofree--;
	}

	/*
	 * Pool list is already full and there are still objs on the free
	 * list. Move remaining free objs to a temporary list to free the
	 * memory outside the pool_lock held region.
	 */
	if (obj_nr_tofree) {
		hlist_move_list(&obj_to_free, &tofree);
		de_objects_freed += obj_nr_tofree;
		obj_nr_tofree = 0;
	}
	raw_spin_unlock_irqrestore(&pool_lock, flags);

	hlist_for_each_entry_safe(obj, tmp, &tofree, node) {
		hlist_del(&obj->node);
		kmem_cache_free(obj_cache, obj);
	}
}

static bool __free_object(struct de_obj *obj)
{
	unsigned long flags;
	bool work;

	raw_spin_lock_irqsave(&pool_lock, flags);
	work = (obj_pool_free > de_objects_pool_size) && obj_cache;
	obj_pool_used--;

	if (work) {
		obj_nr_tofree++;
		hlist_add_head(&obj->node, &obj_to_free);
	} else {
		obj_pool_free++;
		hlist_add_head(&obj->node, &obj_pool);
	}
	raw_spin_unlock_irqrestore(&pool_lock, flags);
	return work;
}

/*
 * Put the object back into the pool and schedule work to free objects
 * if necessary.
 */
static void free_object(struct de_obj *obj)
{
	if (__free_object(obj))
		schedule_work(&de_obj_work);
}

/*
 * We run out of memory. That means we probably have tons of objects
 * allocated.
 */
static void de_objects_oom(void)
{
	struct de_bucket *db = obj_hash;
	struct hlist_node *tmp;
	HLIST_HEAD(freelist);
	struct de_obj *obj;
	unsigned long flags;
	int i;

	pr_warn("Out of memory. ODE disabled\n");

	for (i = 0; i < ODE_HASH_SIZE; i++, db++) {
		raw_spin_lock_irqsave(&db->lock, flags);
		hlist_move_list(&db->list, &freelist);
		raw_spin_unlock_irqrestore(&db->lock, flags);

		/* Now free them */
		hlist_for_each_entry_safe(obj, tmp, &freelist, node) {
			hlist_del(&obj->node);
			free_object(obj);
		}
	}
}

/*
 * We use the pfn of the address for the hash. That way we can check
 * for freed objects simply by checking the affected bucket.
 */
static struct de_bucket *get_bucket(unsigned long addr)
{
	unsigned long hash;

	hash = hash_long((addr >> ODE_CHUNK_SHIFT), ODE_HASH_BITS);
	return &obj_hash[hash];
}

static void de_print_object(struct de_obj *obj, char *msg)
{
	struct de_obj_descr *descr = obj->descr;
	static int limit;

	if (limit < 5 && descr != descr_test) {
		void *hint = descr->de_hint ?
			descr->de_hint(obj->object) : NULL;
		limit++;
		WARN(1, KERN_ERR "ODE: %s %s (active state %u) "
				 "object type: %s hint: %pS\n",
			msg, obj_states[obj->state], obj->astate,
			descr->name, hint);
	}
	de_objects_warnings++;
}

/*
 * Try to repair the damage, so we have a better chance to get useful
 * de output.
 */
static bool
de_object_fixup(bool (*fixup)(void *addr, enum de_obj_state state),
		   void * addr, enum de_obj_state state)
{
	if (fixup && fixup(addr, state)) {
		de_objects_fixups++;
		return true;
	}
	return false;
}

static void de_object_is_on_stack(void *addr, int onstack)
{
	int is_on_stack;
	static int limit;

	if (limit > 4)
		return;

	is_on_stack = object_is_on_stack(addr);
	if (is_on_stack == onstack)
		return;

	limit++;
	if (is_on_stack)
		pr_warn("object %p is on stack %p, but NOT annotated.\n", addr,
			 task_stack_page(current));
	else
		pr_warn("object %p is NOT on stack %p, but annotated.\n", addr,
			 task_stack_page(current));

	WARN_ON(1);
}

static void
__de_object_init(void *addr, struct de_obj_descr *descr, int onstack)
{
	enum de_obj_state state;
	struct de_bucket *db;
	struct de_obj *obj;
	unsigned long flags;

	fill_pool();

	db = get_bucket((unsigned long) addr);

	raw_spin_lock_irqsave(&db->lock, flags);

	obj = lookup_object(addr, db);
	if (!obj) {
		obj = alloc_object(addr, db, descr);
		if (!obj) {
			de_objects_enabled = 0;
			raw_spin_unlock_irqrestore(&db->lock, flags);
			de_objects_oom();
			return;
		}
		de_object_is_on_stack(addr, onstack);
	}

	switch (obj->state) {
	case ODE_STATE_NONE:
	case ODE_STATE_INIT:
	case ODE_STATE_INACTIVE:
		obj->state = ODE_STATE_INIT;
		break;

	case ODE_STATE_ACTIVE:
		de_print_object(obj, "init");
		state = obj->state;
		raw_spin_unlock_irqrestore(&db->lock, flags);
		de_object_fixup(descr->fixup_init, addr, state);
		return;

	case ODE_STATE_DESTROYED:
		de_print_object(obj, "init");
		break;
	default:
		break;
	}

	raw_spin_unlock_irqrestore(&db->lock, flags);
}

/**
 * de_object_init - de checks when an object is initialized
 * @addr:	address of the object
 * @descr:	pointer to an object specific de description structure
 */
void de_object_init(void *addr, struct de_obj_descr *descr)
{
	if (!de_objects_enabled)
		return;

	__de_object_init(addr, descr, 0);
}
EXPORT_SYMBOL_GPL(de_object_init);

/**
 * de_object_init_on_stack - de checks when an object on stack is
 *				initialized
 * @addr:	address of the object
 * @descr:	pointer to an object specific de description structure
 */
void de_object_init_on_stack(void *addr, struct de_obj_descr *descr)
{
	if (!de_objects_enabled)
		return;

	__de_object_init(addr, descr, 1);
}
EXPORT_SYMBOL_GPL(de_object_init_on_stack);

/**
 * de_object_activate - de checks when an object is activated
 * @addr:	address of the object
 * @descr:	pointer to an object specific de description structure
 * Returns 0 for success, -EINVAL for check failed.
 */
int de_object_activate(void *addr, struct de_obj_descr *descr)
{
	enum de_obj_state state;
	struct de_bucket *db;
	struct de_obj *obj;
	unsigned long flags;
	int ret;
	struct de_obj o = { .object = addr,
			       .state = ODE_STATE_NOTAVAILABLE,
			       .descr = descr };

	if (!de_objects_enabled)
		return 0;

	db = get_bucket((unsigned long) addr);

	raw_spin_lock_irqsave(&db->lock, flags);

	obj = lookup_object(addr, db);
	if (obj) {
		switch (obj->state) {
		case ODE_STATE_INIT:
		case ODE_STATE_INACTIVE:
			obj->state = ODE_STATE_ACTIVE;
			ret = 0;
			break;

		case ODE_STATE_ACTIVE:
			de_print_object(obj, "activate");
			state = obj->state;
			raw_spin_unlock_irqrestore(&db->lock, flags);
			ret = de_object_fixup(descr->fixup_activate, addr, state);
			return ret ? 0 : -EINVAL;

		case ODE_STATE_DESTROYED:
			de_print_object(obj, "activate");
			ret = -EINVAL;
			break;
		default:
			ret = 0;
			break;
		}
		raw_spin_unlock_irqrestore(&db->lock, flags);
		return ret;
	}

	raw_spin_unlock_irqrestore(&db->lock, flags);
	/*
	 * We are here when a static object is activated. We
	 * let the type specific code confirm whether this is
	 * true or not. if true, we just make sure that the
	 * static object is tracked in the object tracker. If
	 * not, this must be a , so we try to fix it up.
	 */
	if (descr->is_static_object && descr->is_static_object(addr)) {
		/* track this static object */
		de_object_init(addr, descr);
		de_object_activate(addr, descr);
	} else {
		de_print_object(&o, "activate");
		ret = de_object_fixup(descr->fixup_activate, addr,
					ODE_STATE_NOTAVAILABLE);
		return ret ? 0 : -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(de_object_activate);

/**
 * de_object_deactivate - de checks when an object is deactivated
 * @addr:	address of the object
 * @descr:	pointer to an object specific de description structure
 */
void de_object_deactivate(void *addr, struct de_obj_descr *descr)
{
	struct de_bucket *db;
	struct de_obj *obj;
	unsigned long flags;

	if (!de_objects_enabled)
		return;

	db = get_bucket((unsigned long) addr);

	raw_spin_lock_irqsave(&db->lock, flags);

	obj = lookup_object(addr, db);
	if (obj) {
		switch (obj->state) {
		case ODE_STATE_INIT:
		case ODE_STATE_INACTIVE:
		case ODE_STATE_ACTIVE:
			if (!obj->astate)
				obj->state = ODE_STATE_INACTIVE;
			else
				de_print_object(obj, "deactivate");
			break;

		case ODE_STATE_DESTROYED:
			de_print_object(obj, "deactivate");
			break;
		default:
			break;
		}
	} else {
		struct de_obj o = { .object = addr,
				       .state = ODE_STATE_NOTAVAILABLE,
				       .descr = descr };

		de_print_object(&o, "deactivate");
	}

	raw_spin_unlock_irqrestore(&db->lock, flags);
}
EXPORT_SYMBOL_GPL(de_object_deactivate);

/**
 * de_object_destroy - de checks when an object is destroyed
 * @addr:	address of the object
 * @descr:	pointer to an object specific de description structure
 */
void de_object_destroy(void *addr, struct de_obj_descr *descr)
{
	enum de_obj_state state;
	struct de_bucket *db;
	struct de_obj *obj;
	unsigned long flags;

	if (!de_objects_enabled)
		return;

	db = get_bucket((unsigned long) addr);

	raw_spin_lock_irqsave(&db->lock, flags);

	obj = lookup_object(addr, db);
	if (!obj)
		goto out_unlock;

	switch (obj->state) {
	case ODE_STATE_NONE:
	case ODE_STATE_INIT:
	case ODE_STATE_INACTIVE:
		obj->state = ODE_STATE_DESTROYED;
		break;
	case ODE_STATE_ACTIVE:
		de_print_object(obj, "destroy");
		state = obj->state;
		raw_spin_unlock_irqrestore(&db->lock, flags);
		de_object_fixup(descr->fixup_destroy, addr, state);
		return;

	case ODE_STATE_DESTROYED:
		de_print_object(obj, "destroy");
		break;
	default:
		break;
	}
out_unlock:
	raw_spin_unlock_irqrestore(&db->lock, flags);
}
EXPORT_SYMBOL_GPL(de_object_destroy);

/**
 * de_object_free - de checks when an object is freed
 * @addr:	address of the object
 * @descr:	pointer to an object specific de description structure
 */
void de_object_free(void *addr, struct de_obj_descr *descr)
{
	enum de_obj_state state;
	struct de_bucket *db;
	struct de_obj *obj;
	unsigned long flags;

	if (!de_objects_enabled)
		return;

	db = get_bucket((unsigned long) addr);

	raw_spin_lock_irqsave(&db->lock, flags);

	obj = lookup_object(addr, db);
	if (!obj)
		goto out_unlock;

	switch (obj->state) {
	case ODE_STATE_ACTIVE:
		de_print_object(obj, "free");
		state = obj->state;
		raw_spin_unlock_irqrestore(&db->lock, flags);
		de_object_fixup(descr->fixup_free, addr, state);
		return;
	default:
		hlist_del(&obj->node);
		raw_spin_unlock_irqrestore(&db->lock, flags);
		free_object(obj);
		return;
	}
out_unlock:
	raw_spin_unlock_irqrestore(&db->lock, flags);
}
EXPORT_SYMBOL_GPL(de_object_free);

/**
 * de_object_assert_init - de checks when object should be init-ed
 * @addr:	address of the object
 * @descr:	pointer to an object specific de description structure
 */
void de_object_assert_init(void *addr, struct de_obj_descr *descr)
{
	struct de_bucket *db;
	struct de_obj *obj;
	unsigned long flags;

	if (!de_objects_enabled)
		return;

	db = get_bucket((unsigned long) addr);

	raw_spin_lock_irqsave(&db->lock, flags);

	obj = lookup_object(addr, db);
	if (!obj) {
		struct de_obj o = { .object = addr,
				       .state = ODE_STATE_NOTAVAILABLE,
				       .descr = descr };

		raw_spin_unlock_irqrestore(&db->lock, flags);
		/*
		 * Maybe the object is static, and we let the type specific
		 * code confirm. Track this static object if true, else invoke
		 * fixup.
		 */
		if (descr->is_static_object && descr->is_static_object(addr)) {
			/* Track this static object */
			de_object_init(addr, descr);
		} else {
			de_print_object(&o, "assert_init");
			de_object_fixup(descr->fixup_assert_init, addr,
					   ODE_STATE_NOTAVAILABLE);
		}
		return;
	}

	raw_spin_unlock_irqrestore(&db->lock, flags);
}
EXPORT_SYMBOL_GPL(de_object_assert_init);

/**
 * de_object_active_state - de checks object usage state machine
 * @addr:	address of the object
 * @descr:	pointer to an object specific de description structure
 * @expect:	expected state
 * @next:	state to move to if expected state is found
 */
void
de_object_active_state(void *addr, struct de_obj_descr *descr,
			  unsigned int expect, unsigned int next)
{
	struct de_bucket *db;
	struct de_obj *obj;
	unsigned long flags;

	if (!de_objects_enabled)
		return;

	db = get_bucket((unsigned long) addr);

	raw_spin_lock_irqsave(&db->lock, flags);

	obj = lookup_object(addr, db);
	if (obj) {
		switch (obj->state) {
		case ODE_STATE_ACTIVE:
			if (obj->astate == expect)
				obj->astate = next;
			else
				de_print_object(obj, "active_state");
			break;

		default:
			de_print_object(obj, "active_state");
			break;
		}
	} else {
		struct de_obj o = { .object = addr,
				       .state = ODE_STATE_NOTAVAILABLE,
				       .descr = descr };

		de_print_object(&o, "active_state");
	}

	raw_spin_unlock_irqrestore(&db->lock, flags);
}
EXPORT_SYMBOL_GPL(de_object_active_state);

#ifdef CONFIG_DE_OBJECTS_FREE
static void __de_check_no_obj_freed(const void *address, unsigned long size)
{
	unsigned long flags, oaddr, saddr, eaddr, paddr, chunks;
	struct de_obj_descr *descr;
	enum de_obj_state state;
	struct de_bucket *db;
	struct hlist_node *tmp;
	struct de_obj *obj;
	int cnt, objs_checked = 0;
	bool work = false;

	saddr = (unsigned long) address;
	eaddr = saddr + size;
	paddr = saddr & ODE_CHUNK_MASK;
	chunks = ((eaddr - paddr) + (ODE_CHUNK_SIZE - 1));
	chunks >>= ODE_CHUNK_SHIFT;

	for (;chunks > 0; chunks--, paddr += ODE_CHUNK_SIZE) {
		db = get_bucket(paddr);

repeat:
		cnt = 0;
		raw_spin_lock_irqsave(&db->lock, flags);
		hlist_for_each_entry_safe(obj, tmp, &db->list, node) {
			cnt++;
			oaddr = (unsigned long) obj->object;
			if (oaddr < saddr || oaddr >= eaddr)
				continue;

			switch (obj->state) {
			case ODE_STATE_ACTIVE:
				de_print_object(obj, "free");
				descr = obj->descr;
				state = obj->state;
				raw_spin_unlock_irqrestore(&db->lock, flags);
				de_object_fixup(descr->fixup_free,
						   (void *) oaddr, state);
				goto repeat;
			default:
				hlist_del(&obj->node);
				work |= __free_object(obj);
				break;
			}
		}
		raw_spin_unlock_irqrestore(&db->lock, flags);

		if (cnt > de_objects_maxchain)
			de_objects_maxchain = cnt;

		objs_checked += cnt;
	}

	if (objs_checked > de_objects_maxchecked)
		de_objects_maxchecked = objs_checked;

	/* Schedule work to actually kmem_cache_free() objects */
	if (work)
		schedule_work(&de_obj_work);
}

void de_check_no_obj_freed(const void *address, unsigned long size)
{
	if (de_objects_enabled)
		__de_check_no_obj_freed(address, size);
}
#endif

#ifdef CONFIG_DE_FS

static int de_stats_show(struct seq_file *m, void *v)
{
	seq_printf(m, "max_chain     :%d\n", de_objects_maxchain);
	seq_printf(m, "max_checked   :%d\n", de_objects_maxchecked);
	seq_printf(m, "warnings      :%d\n", de_objects_warnings);
	seq_printf(m, "fixups        :%d\n", de_objects_fixups);
	seq_printf(m, "pool_free     :%d\n", obj_pool_free);
	seq_printf(m, "pool_min_free :%d\n", obj_pool_min_free);
	seq_printf(m, "pool_used     :%d\n", obj_pool_used);
	seq_printf(m, "pool_max_used :%d\n", obj_pool_max_used);
	seq_printf(m, "on_free_list  :%d\n", obj_nr_tofree);
	seq_printf(m, "objs_allocated:%d\n", de_objects_allocated);
	seq_printf(m, "objs_freed    :%d\n", de_objects_freed);
	return 0;
}

static int de_stats_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, de_stats_show, NULL);
}

static const struct file_operations de_stats_fops = {
	.open		= de_stats_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init de_objects_init_defs(void)
{
	struct dentry *dbgdir, *dbgstats;

	if (!de_objects_enabled)
		return 0;

	dbgdir = defs_create_dir("de_objects", NULL);
	if (!dbgdir)
		return -ENOMEM;

	dbgstats = defs_create_file("stats", 0444, dbgdir, NULL,
				       &de_stats_fops);
	if (!dbgstats)
		goto err;

	return 0;

err:
	defs_remove(dbgdir);

	return -ENOMEM;
}
__initcall(de_objects_init_defs);

#else
static inline void de_objects_init_defs(void) { }
#endif

#ifdef CONFIG_DE_OBJECTS_SELFTEST

/* Random data structure for the self test */
struct self_test {
	unsigned long	dummy1[6];
	int		static_init;
	unsigned long	dummy2[3];
};

static __initdata struct de_obj_descr descr_type_test;

static bool __init is_static_object(void *addr)
{
	struct self_test *obj = addr;

	return obj->static_init;
}

/*
 * fixup_init is called when:
 * - an active object is initialized
 */
static bool __init fixup_init(void *addr, enum de_obj_state state)
{
	struct self_test *obj = addr;

	switch (state) {
	case ODE_STATE_ACTIVE:
		de_object_deactivate(obj, &descr_type_test);
		de_object_init(obj, &descr_type_test);
		return true;
	default:
		return false;
	}
}

/*
 * fixup_activate is called when:
 * - an active object is activated
 * - an unknown non-static object is activated
 */
static bool __init fixup_activate(void *addr, enum de_obj_state state)
{
	struct self_test *obj = addr;

	switch (state) {
	case ODE_STATE_NOTAVAILABLE:
		return true;
	case ODE_STATE_ACTIVE:
		de_object_deactivate(obj, &descr_type_test);
		de_object_activate(obj, &descr_type_test);
		return true;

	default:
		return false;
	}
}

/*
 * fixup_destroy is called when:
 * - an active object is destroyed
 */
static bool __init fixup_destroy(void *addr, enum de_obj_state state)
{
	struct self_test *obj = addr;

	switch (state) {
	case ODE_STATE_ACTIVE:
		de_object_deactivate(obj, &descr_type_test);
		de_object_destroy(obj, &descr_type_test);
		return true;
	default:
		return false;
	}
}

/*
 * fixup_free is called when:
 * - an active object is freed
 */
static bool __init fixup_free(void *addr, enum de_obj_state state)
{
	struct self_test *obj = addr;

	switch (state) {
	case ODE_STATE_ACTIVE:
		de_object_deactivate(obj, &descr_type_test);
		de_object_free(obj, &descr_type_test);
		return true;
	default:
		return false;
	}
}

static int __init
check_results(void *addr, enum de_obj_state state, int fixups, int warnings)
{
	struct de_bucket *db;
	struct de_obj *obj;
	unsigned long flags;
	int res = -EINVAL;

	db = get_bucket((unsigned long) addr);

	raw_spin_lock_irqsave(&db->lock, flags);

	obj = lookup_object(addr, db);
	if (!obj && state != ODE_STATE_NONE) {
		WARN(1, KERN_ERR "ODE: selftest object not found\n");
		goto out;
	}
	if (obj && obj->state != state) {
		WARN(1, KERN_ERR "ODE: selftest wrong state: %d != %d\n",
		       obj->state, state);
		goto out;
	}
	if (fixups != de_objects_fixups) {
		WARN(1, KERN_ERR "ODE: selftest fixups failed %d != %d\n",
		       fixups, de_objects_fixups);
		goto out;
	}
	if (warnings != de_objects_warnings) {
		WARN(1, KERN_ERR "ODE: selftest warnings failed %d != %d\n",
		       warnings, de_objects_warnings);
		goto out;
	}
	res = 0;
out:
	raw_spin_unlock_irqrestore(&db->lock, flags);
	if (res)
		de_objects_enabled = 0;
	return res;
}

static __initdata struct de_obj_descr descr_type_test = {
	.name			= "selftest",
	.is_static_object	= is_static_object,
	.fixup_init		= fixup_init,
	.fixup_activate		= fixup_activate,
	.fixup_destroy		= fixup_destroy,
	.fixup_free		= fixup_free,
};

static __initdata struct self_test obj = { .static_init = 0 };

static void __init de_objects_selftest(void)
{
	int fixups, oldfixups, warnings, oldwarnings;
	unsigned long flags;

	local_irq_save(flags);

	fixups = oldfixups = de_objects_fixups;
	warnings = oldwarnings = de_objects_warnings;
	descr_test = &descr_type_test;

	de_object_init(&obj, &descr_type_test);
	if (check_results(&obj, ODE_STATE_INIT, fixups, warnings))
		goto out;
	de_object_activate(&obj, &descr_type_test);
	if (check_results(&obj, ODE_STATE_ACTIVE, fixups, warnings))
		goto out;
	de_object_activate(&obj, &descr_type_test);
	if (check_results(&obj, ODE_STATE_ACTIVE, ++fixups, ++warnings))
		goto out;
	de_object_deactivate(&obj, &descr_type_test);
	if (check_results(&obj, ODE_STATE_INACTIVE, fixups, warnings))
		goto out;
	de_object_destroy(&obj, &descr_type_test);
	if (check_results(&obj, ODE_STATE_DESTROYED, fixups, warnings))
		goto out;
	de_object_init(&obj, &descr_type_test);
	if (check_results(&obj, ODE_STATE_DESTROYED, fixups, ++warnings))
		goto out;
	de_object_activate(&obj, &descr_type_test);
	if (check_results(&obj, ODE_STATE_DESTROYED, fixups, ++warnings))
		goto out;
	de_object_deactivate(&obj, &descr_type_test);
	if (check_results(&obj, ODE_STATE_DESTROYED, fixups, ++warnings))
		goto out;
	de_object_free(&obj, &descr_type_test);
	if (check_results(&obj, ODE_STATE_NONE, fixups, warnings))
		goto out;

	obj.static_init = 1;
	de_object_activate(&obj, &descr_type_test);
	if (check_results(&obj, ODE_STATE_ACTIVE, fixups, warnings))
		goto out;
	de_object_init(&obj, &descr_type_test);
	if (check_results(&obj, ODE_STATE_INIT, ++fixups, ++warnings))
		goto out;
	de_object_free(&obj, &descr_type_test);
	if (check_results(&obj, ODE_STATE_NONE, fixups, warnings))
		goto out;

#ifdef CONFIG_DE_OBJECTS_FREE
	de_object_init(&obj, &descr_type_test);
	if (check_results(&obj, ODE_STATE_INIT, fixups, warnings))
		goto out;
	de_object_activate(&obj, &descr_type_test);
	if (check_results(&obj, ODE_STATE_ACTIVE, fixups, warnings))
		goto out;
	__de_check_no_obj_freed(&obj, sizeof(obj));
	if (check_results(&obj, ODE_STATE_NONE, ++fixups, ++warnings))
		goto out;
#endif
	pr_info("selftest passed\n");

out:
	de_objects_fixups = oldfixups;
	de_objects_warnings = oldwarnings;
	descr_test = NULL;

	local_irq_restore(flags);
}
#else
static inline void de_objects_selftest(void) { }
#endif

/*
 * Called during early boot to initialize the hash buckets and link
 * the static object pool objects into the poll list. After this call
 * the object tracker is fully operational.
 */
void __init de_objects_early_init(void)
{
	int i;

	for (i = 0; i < ODE_HASH_SIZE; i++)
		raw_spin_lock_init(&obj_hash[i].lock);

	for (i = 0; i < ODE_POOL_SIZE; i++)
		hlist_add_head(&obj_static_pool[i].node, &obj_pool);
}

/*
 * Convert the statically allocated objects to dynamic ones:
 */
static int __init de_objects_replace_static_objects(void)
{
	struct de_bucket *db = obj_hash;
	struct hlist_node *tmp;
	struct de_obj *obj, *new;
	HLIST_HEAD(objects);
	int i, cnt = 0;

	for (i = 0; i < ODE_POOL_SIZE; i++) {
		obj = kmem_cache_zalloc(obj_cache, GFP_KERNEL);
		if (!obj)
			goto free;
		hlist_add_head(&obj->node, &objects);
	}

	/*
	 * de_objects_mem_init() is now called early that only one CPU is up
	 * and interrupts have been disabled, so it is safe to replace the
	 * active object references.
	 */

	/* Remove the statically allocated objects from the pool */
	hlist_for_each_entry_safe(obj, tmp, &obj_pool, node)
		hlist_del(&obj->node);
	/* Move the allocated objects to the pool */
	hlist_move_list(&objects, &obj_pool);

	/* Replace the active object references */
	for (i = 0; i < ODE_HASH_SIZE; i++, db++) {
		hlist_move_list(&db->list, &objects);

		hlist_for_each_entry(obj, &objects, node) {
			new = hlist_entry(obj_pool.first, typeof(*obj), node);
			hlist_del(&new->node);
			/* copy object data */
			*new = *obj;
			hlist_add_head(&new->node, &db->list);
			cnt++;
		}
	}

	pr_de("%d of %d active objects replaced\n",
		 cnt, obj_pool_used);
	return 0;
free:
	hlist_for_each_entry_safe(obj, tmp, &objects, node) {
		hlist_del(&obj->node);
		kmem_cache_free(obj_cache, obj);
	}
	return -ENOMEM;
}

/*
 * Called after the kmem_caches are functional to setup a dedicated
 * cache pool, which has the SLAB_DE_OBJECTS flag set. This flag
 * prevents that the de code is called on kmem_cache_free() for the
 * de tracker objects to avoid recursive calls.
 */
void __init de_objects_mem_init(void)
{
	if (!de_objects_enabled)
		return;

	obj_cache = kmem_cache_create("de_objects_cache",
				      sizeof (struct de_obj), 0,
				      SLAB_DE_OBJECTS | SLAB_NOLEAKTRACE,
				      NULL);

	if (!obj_cache || de_objects_replace_static_objects()) {
		de_objects_enabled = 0;
		kmem_cache_destroy(obj_cache);
		pr_warn("out of memory.\n");
	} else
		de_objects_selftest();

	/*
	 * Increase the thresholds for allocating and freeing objects
	 * according to the number of possible CPUs available in the system.
	 */
	de_objects_pool_size += num_possible_cpus() * 32;
	de_objects_pool_min_level += num_possible_cpus() * 4;
}
