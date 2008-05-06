/*
 * Generic infrastructure for lifetime debugging of objects.
 *
 * Started by Thomas Gleixner
 *
 * Copyright (C) 2008, Thomas Gleixner <tglx@linutronix.de>
 *
 * For licencing details see kernel-base/COPYING
 */
#include <linux/debugobjects.h>
#include <linux/interrupt.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/hash.h>

#define ODEBUG_HASH_BITS	14
#define ODEBUG_HASH_SIZE	(1 << ODEBUG_HASH_BITS)

#define ODEBUG_POOL_SIZE	512
#define ODEBUG_POOL_MIN_LEVEL	256

#define ODEBUG_CHUNK_SHIFT	PAGE_SHIFT
#define ODEBUG_CHUNK_SIZE	(1 << ODEBUG_CHUNK_SHIFT)
#define ODEBUG_CHUNK_MASK	(~(ODEBUG_CHUNK_SIZE - 1))

struct debug_bucket {
	struct hlist_head	list;
	spinlock_t		lock;
};

static struct debug_bucket	obj_hash[ODEBUG_HASH_SIZE];

static struct debug_obj		obj_static_pool[ODEBUG_POOL_SIZE];

static DEFINE_SPINLOCK(pool_lock);

static HLIST_HEAD(obj_pool);

static int			obj_pool_min_free = ODEBUG_POOL_SIZE;
static int			obj_pool_free = ODEBUG_POOL_SIZE;
static int			obj_pool_used;
static int			obj_pool_max_used;
static struct kmem_cache	*obj_cache;

static int			debug_objects_maxchain __read_mostly;
static int			debug_objects_fixups __read_mostly;
static int			debug_objects_warnings __read_mostly;
static int			debug_objects_enabled __read_mostly;
static struct debug_obj_descr	*descr_test  __read_mostly;

static int __init enable_object_debug(char *str)
{
	debug_objects_enabled = 1;
	return 0;
}
early_param("debug_objects", enable_object_debug);

static const char *obj_states[ODEBUG_STATE_MAX] = {
	[ODEBUG_STATE_NONE]		= "none",
	[ODEBUG_STATE_INIT]		= "initialized",
	[ODEBUG_STATE_INACTIVE]		= "inactive",
	[ODEBUG_STATE_ACTIVE]		= "active",
	[ODEBUG_STATE_DESTROYED]	= "destroyed",
	[ODEBUG_STATE_NOTAVAILABLE]	= "not available",
};

static int fill_pool(void)
{
	gfp_t gfp = GFP_ATOMIC | __GFP_NORETRY | __GFP_NOWARN;
	struct debug_obj *new;

	if (likely(obj_pool_free >= ODEBUG_POOL_MIN_LEVEL))
		return obj_pool_free;

	if (unlikely(!obj_cache))
		return obj_pool_free;

	while (obj_pool_free < ODEBUG_POOL_MIN_LEVEL) {

		new = kmem_cache_zalloc(obj_cache, gfp);
		if (!new)
			return obj_pool_free;

		spin_lock(&pool_lock);
		hlist_add_head(&new->node, &obj_pool);
		obj_pool_free++;
		spin_unlock(&pool_lock);
	}
	return obj_pool_free;
}

/*
 * Lookup an object in the hash bucket.
 */
static struct debug_obj *lookup_object(void *addr, struct debug_bucket *b)
{
	struct hlist_node *node;
	struct debug_obj *obj;
	int cnt = 0;

	hlist_for_each_entry(obj, node, &b->list, node) {
		cnt++;
		if (obj->object == addr)
			return obj;
	}
	if (cnt > debug_objects_maxchain)
		debug_objects_maxchain = cnt;

	return NULL;
}

/*
 * Allocate a new object. If the pool is empty and no refill possible,
 * switch off the debugger.
 */
static struct debug_obj *
alloc_object(void *addr, struct debug_bucket *b, struct debug_obj_descr *descr)
{
	struct debug_obj *obj = NULL;
	int retry = 0;

repeat:
	spin_lock(&pool_lock);
	if (obj_pool.first) {
		obj	    = hlist_entry(obj_pool.first, typeof(*obj), node);

		obj->object = addr;
		obj->descr  = descr;
		obj->state  = ODEBUG_STATE_NONE;
		hlist_del(&obj->node);

		hlist_add_head(&obj->node, &b->list);

		obj_pool_used++;
		if (obj_pool_used > obj_pool_max_used)
			obj_pool_max_used = obj_pool_used;

		obj_pool_free--;
		if (obj_pool_free < obj_pool_min_free)
			obj_pool_min_free = obj_pool_free;
	}
	spin_unlock(&pool_lock);

	if (fill_pool() && !obj && !retry++)
		goto repeat;

	return obj;
}

/*
 * Put the object back into the pool or give it back to kmem_cache:
 */
static void free_object(struct debug_obj *obj)
{
	unsigned long idx = (unsigned long)(obj - obj_static_pool);

	if (obj_pool_free < ODEBUG_POOL_SIZE || idx < ODEBUG_POOL_SIZE) {
		spin_lock(&pool_lock);
		hlist_add_head(&obj->node, &obj_pool);
		obj_pool_free++;
		obj_pool_used--;
		spin_unlock(&pool_lock);
	} else {
		spin_lock(&pool_lock);
		obj_pool_used--;
		spin_unlock(&pool_lock);
		kmem_cache_free(obj_cache, obj);
	}
}

/*
 * We run out of memory. That means we probably have tons of objects
 * allocated.
 */
static void debug_objects_oom(void)
{
	struct debug_bucket *db = obj_hash;
	struct hlist_node *node, *tmp;
	struct debug_obj *obj;
	unsigned long flags;
	int i;

	printk(KERN_WARNING "ODEBUG: Out of memory. ODEBUG disabled\n");

	for (i = 0; i < ODEBUG_HASH_SIZE; i++, db++) {
		spin_lock_irqsave(&db->lock, flags);
		hlist_for_each_entry_safe(obj, node, tmp, &db->list, node) {
			hlist_del(&obj->node);
			free_object(obj);
		}
		spin_unlock_irqrestore(&db->lock, flags);
	}
}

/*
 * We use the pfn of the address for the hash. That way we can check
 * for freed objects simply by checking the affected bucket.
 */
static struct debug_bucket *get_bucket(unsigned long addr)
{
	unsigned long hash;

	hash = hash_long((addr >> ODEBUG_CHUNK_SHIFT), ODEBUG_HASH_BITS);
	return &obj_hash[hash];
}

static void debug_print_object(struct debug_obj *obj, char *msg)
{
	static int limit;

	if (limit < 5 && obj->descr != descr_test) {
		limit++;
		printk(KERN_ERR "ODEBUG: %s %s object type: %s\n", msg,
		       obj_states[obj->state], obj->descr->name);
		WARN_ON(1);
	}
	debug_objects_warnings++;
}

/*
 * Try to repair the damage, so we have a better chance to get useful
 * debug output.
 */
static void
debug_object_fixup(int (*fixup)(void *addr, enum debug_obj_state state),
		   void * addr, enum debug_obj_state state)
{
	if (fixup)
		debug_objects_fixups += fixup(addr, state);
}

static void debug_object_is_on_stack(void *addr, int onstack)
{
	void *stack = current->stack;
	int is_on_stack;
	static int limit;

	if (limit > 4)
		return;

	is_on_stack = (addr >= stack && addr < (stack + THREAD_SIZE));

	if (is_on_stack == onstack)
		return;

	limit++;
	if (is_on_stack)
		printk(KERN_WARNING
		       "ODEBUG: object is on stack, but not annotated\n");
	else
		printk(KERN_WARNING
		       "ODEBUG: object is not on stack, but annotated\n");
	WARN_ON(1);
}

static void
__debug_object_init(void *addr, struct debug_obj_descr *descr, int onstack)
{
	enum debug_obj_state state;
	struct debug_bucket *db;
	struct debug_obj *obj;
	unsigned long flags;

	db = get_bucket((unsigned long) addr);

	spin_lock_irqsave(&db->lock, flags);

	obj = lookup_object(addr, db);
	if (!obj) {
		obj = alloc_object(addr, db, descr);
		if (!obj) {
			debug_objects_enabled = 0;
			spin_unlock_irqrestore(&db->lock, flags);
			debug_objects_oom();
			return;
		}
		debug_object_is_on_stack(addr, onstack);
	}

	switch (obj->state) {
	case ODEBUG_STATE_NONE:
	case ODEBUG_STATE_INIT:
	case ODEBUG_STATE_INACTIVE:
		obj->state = ODEBUG_STATE_INIT;
		break;

	case ODEBUG_STATE_ACTIVE:
		debug_print_object(obj, "init");
		state = obj->state;
		spin_unlock_irqrestore(&db->lock, flags);
		debug_object_fixup(descr->fixup_init, addr, state);
		return;

	case ODEBUG_STATE_DESTROYED:
		debug_print_object(obj, "init");
		break;
	default:
		break;
	}

	spin_unlock_irqrestore(&db->lock, flags);
}

/**
 * debug_object_init - debug checks when an object is initialized
 * @addr:	address of the object
 * @descr:	pointer to an object specific debug description structure
 */
void debug_object_init(void *addr, struct debug_obj_descr *descr)
{
	if (!debug_objects_enabled)
		return;

	__debug_object_init(addr, descr, 0);
}

/**
 * debug_object_init_on_stack - debug checks when an object on stack is
 *				initialized
 * @addr:	address of the object
 * @descr:	pointer to an object specific debug description structure
 */
void debug_object_init_on_stack(void *addr, struct debug_obj_descr *descr)
{
	if (!debug_objects_enabled)
		return;

	__debug_object_init(addr, descr, 1);
}

/**
 * debug_object_activate - debug checks when an object is activated
 * @addr:	address of the object
 * @descr:	pointer to an object specific debug description structure
 */
void debug_object_activate(void *addr, struct debug_obj_descr *descr)
{
	enum debug_obj_state state;
	struct debug_bucket *db;
	struct debug_obj *obj;
	unsigned long flags;

	if (!debug_objects_enabled)
		return;

	db = get_bucket((unsigned long) addr);

	spin_lock_irqsave(&db->lock, flags);

	obj = lookup_object(addr, db);
	if (obj) {
		switch (obj->state) {
		case ODEBUG_STATE_INIT:
		case ODEBUG_STATE_INACTIVE:
			obj->state = ODEBUG_STATE_ACTIVE;
			break;

		case ODEBUG_STATE_ACTIVE:
			debug_print_object(obj, "activate");
			state = obj->state;
			spin_unlock_irqrestore(&db->lock, flags);
			debug_object_fixup(descr->fixup_activate, addr, state);
			return;

		case ODEBUG_STATE_DESTROYED:
			debug_print_object(obj, "activate");
			break;
		default:
			break;
		}
		spin_unlock_irqrestore(&db->lock, flags);
		return;
	}

	spin_unlock_irqrestore(&db->lock, flags);
	/*
	 * This happens when a static object is activated. We
	 * let the type specific code decide whether this is
	 * true or not.
	 */
	debug_object_fixup(descr->fixup_activate, addr,
			   ODEBUG_STATE_NOTAVAILABLE);
}

/**
 * debug_object_deactivate - debug checks when an object is deactivated
 * @addr:	address of the object
 * @descr:	pointer to an object specific debug description structure
 */
void debug_object_deactivate(void *addr, struct debug_obj_descr *descr)
{
	struct debug_bucket *db;
	struct debug_obj *obj;
	unsigned long flags;

	if (!debug_objects_enabled)
		return;

	db = get_bucket((unsigned long) addr);

	spin_lock_irqsave(&db->lock, flags);

	obj = lookup_object(addr, db);
	if (obj) {
		switch (obj->state) {
		case ODEBUG_STATE_INIT:
		case ODEBUG_STATE_INACTIVE:
		case ODEBUG_STATE_ACTIVE:
			obj->state = ODEBUG_STATE_INACTIVE;
			break;

		case ODEBUG_STATE_DESTROYED:
			debug_print_object(obj, "deactivate");
			break;
		default:
			break;
		}
	} else {
		struct debug_obj o = { .object = addr,
				       .state = ODEBUG_STATE_NOTAVAILABLE,
				       .descr = descr };

		debug_print_object(&o, "deactivate");
	}

	spin_unlock_irqrestore(&db->lock, flags);
}

/**
 * debug_object_destroy - debug checks when an object is destroyed
 * @addr:	address of the object
 * @descr:	pointer to an object specific debug description structure
 */
void debug_object_destroy(void *addr, struct debug_obj_descr *descr)
{
	enum debug_obj_state state;
	struct debug_bucket *db;
	struct debug_obj *obj;
	unsigned long flags;

	if (!debug_objects_enabled)
		return;

	db = get_bucket((unsigned long) addr);

	spin_lock_irqsave(&db->lock, flags);

	obj = lookup_object(addr, db);
	if (!obj)
		goto out_unlock;

	switch (obj->state) {
	case ODEBUG_STATE_NONE:
	case ODEBUG_STATE_INIT:
	case ODEBUG_STATE_INACTIVE:
		obj->state = ODEBUG_STATE_DESTROYED;
		break;
	case ODEBUG_STATE_ACTIVE:
		debug_print_object(obj, "destroy");
		state = obj->state;
		spin_unlock_irqrestore(&db->lock, flags);
		debug_object_fixup(descr->fixup_destroy, addr, state);
		return;

	case ODEBUG_STATE_DESTROYED:
		debug_print_object(obj, "destroy");
		break;
	default:
		break;
	}
out_unlock:
	spin_unlock_irqrestore(&db->lock, flags);
}

/**
 * debug_object_free - debug checks when an object is freed
 * @addr:	address of the object
 * @descr:	pointer to an object specific debug description structure
 */
void debug_object_free(void *addr, struct debug_obj_descr *descr)
{
	enum debug_obj_state state;
	struct debug_bucket *db;
	struct debug_obj *obj;
	unsigned long flags;

	if (!debug_objects_enabled)
		return;

	db = get_bucket((unsigned long) addr);

	spin_lock_irqsave(&db->lock, flags);

	obj = lookup_object(addr, db);
	if (!obj)
		goto out_unlock;

	switch (obj->state) {
	case ODEBUG_STATE_ACTIVE:
		debug_print_object(obj, "free");
		state = obj->state;
		spin_unlock_irqrestore(&db->lock, flags);
		debug_object_fixup(descr->fixup_free, addr, state);
		return;
	default:
		hlist_del(&obj->node);
		free_object(obj);
		break;
	}
out_unlock:
	spin_unlock_irqrestore(&db->lock, flags);
}

#ifdef CONFIG_DEBUG_OBJECTS_FREE
static void __debug_check_no_obj_freed(const void *address, unsigned long size)
{
	unsigned long flags, oaddr, saddr, eaddr, paddr, chunks;
	struct hlist_node *node, *tmp;
	struct debug_obj_descr *descr;
	enum debug_obj_state state;
	struct debug_bucket *db;
	struct debug_obj *obj;
	int cnt;

	saddr = (unsigned long) address;
	eaddr = saddr + size;
	paddr = saddr & ODEBUG_CHUNK_MASK;
	chunks = ((eaddr - paddr) + (ODEBUG_CHUNK_SIZE - 1));
	chunks >>= ODEBUG_CHUNK_SHIFT;

	for (;chunks > 0; chunks--, paddr += ODEBUG_CHUNK_SIZE) {
		db = get_bucket(paddr);

repeat:
		cnt = 0;
		spin_lock_irqsave(&db->lock, flags);
		hlist_for_each_entry_safe(obj, node, tmp, &db->list, node) {
			cnt++;
			oaddr = (unsigned long) obj->object;
			if (oaddr < saddr || oaddr >= eaddr)
				continue;

			switch (obj->state) {
			case ODEBUG_STATE_ACTIVE:
				debug_print_object(obj, "free");
				descr = obj->descr;
				state = obj->state;
				spin_unlock_irqrestore(&db->lock, flags);
				debug_object_fixup(descr->fixup_free,
						   (void *) oaddr, state);
				goto repeat;
			default:
				hlist_del(&obj->node);
				free_object(obj);
				break;
			}
		}
		spin_unlock_irqrestore(&db->lock, flags);
		if (cnt > debug_objects_maxchain)
			debug_objects_maxchain = cnt;
	}
}

void debug_check_no_obj_freed(const void *address, unsigned long size)
{
	if (debug_objects_enabled)
		__debug_check_no_obj_freed(address, size);
}
#endif

#ifdef CONFIG_DEBUG_FS

static int debug_stats_show(struct seq_file *m, void *v)
{
	seq_printf(m, "max_chain     :%d\n", debug_objects_maxchain);
	seq_printf(m, "warnings      :%d\n", debug_objects_warnings);
	seq_printf(m, "fixups        :%d\n", debug_objects_fixups);
	seq_printf(m, "pool_free     :%d\n", obj_pool_free);
	seq_printf(m, "pool_min_free :%d\n", obj_pool_min_free);
	seq_printf(m, "pool_used     :%d\n", obj_pool_used);
	seq_printf(m, "pool_max_used :%d\n", obj_pool_max_used);
	return 0;
}

static int debug_stats_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, debug_stats_show, NULL);
}

static const struct file_operations debug_stats_fops = {
	.open		= debug_stats_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init debug_objects_init_debugfs(void)
{
	struct dentry *dbgdir, *dbgstats;

	if (!debug_objects_enabled)
		return 0;

	dbgdir = debugfs_create_dir("debug_objects", NULL);
	if (!dbgdir)
		return -ENOMEM;

	dbgstats = debugfs_create_file("stats", 0444, dbgdir, NULL,
				       &debug_stats_fops);
	if (!dbgstats)
		goto err;

	return 0;

err:
	debugfs_remove(dbgdir);

	return -ENOMEM;
}
__initcall(debug_objects_init_debugfs);

#else
static inline void debug_objects_init_debugfs(void) { }
#endif

#ifdef CONFIG_DEBUG_OBJECTS_SELFTEST

/* Random data structure for the self test */
struct self_test {
	unsigned long	dummy1[6];
	int		static_init;
	unsigned long	dummy2[3];
};

static __initdata struct debug_obj_descr descr_type_test;

/*
 * fixup_init is called when:
 * - an active object is initialized
 */
static int __init fixup_init(void *addr, enum debug_obj_state state)
{
	struct self_test *obj = addr;

	switch (state) {
	case ODEBUG_STATE_ACTIVE:
		debug_object_deactivate(obj, &descr_type_test);
		debug_object_init(obj, &descr_type_test);
		return 1;
	default:
		return 0;
	}
}

/*
 * fixup_activate is called when:
 * - an active object is activated
 * - an unknown object is activated (might be a statically initialized object)
 */
static int __init fixup_activate(void *addr, enum debug_obj_state state)
{
	struct self_test *obj = addr;

	switch (state) {
	case ODEBUG_STATE_NOTAVAILABLE:
		if (obj->static_init == 1) {
			debug_object_init(obj, &descr_type_test);
			debug_object_activate(obj, &descr_type_test);
			/*
			 * Real code should return 0 here ! This is
			 * not a fixup of some bad behaviour. We
			 * merily call the debug_init function to keep
			 * track of the object.
			 */
			return 1;
		} else {
			/* Real code needs to emit a warning here */
		}
		return 0;

	case ODEBUG_STATE_ACTIVE:
		debug_object_deactivate(obj, &descr_type_test);
		debug_object_activate(obj, &descr_type_test);
		return 1;

	default:
		return 0;
	}
}

/*
 * fixup_destroy is called when:
 * - an active object is destroyed
 */
static int __init fixup_destroy(void *addr, enum debug_obj_state state)
{
	struct self_test *obj = addr;

	switch (state) {
	case ODEBUG_STATE_ACTIVE:
		debug_object_deactivate(obj, &descr_type_test);
		debug_object_destroy(obj, &descr_type_test);
		return 1;
	default:
		return 0;
	}
}

/*
 * fixup_free is called when:
 * - an active object is freed
 */
static int __init fixup_free(void *addr, enum debug_obj_state state)
{
	struct self_test *obj = addr;

	switch (state) {
	case ODEBUG_STATE_ACTIVE:
		debug_object_deactivate(obj, &descr_type_test);
		debug_object_free(obj, &descr_type_test);
		return 1;
	default:
		return 0;
	}
}

static int
check_results(void *addr, enum debug_obj_state state, int fixups, int warnings)
{
	struct debug_bucket *db;
	struct debug_obj *obj;
	unsigned long flags;
	int res = -EINVAL;

	db = get_bucket((unsigned long) addr);

	spin_lock_irqsave(&db->lock, flags);

	obj = lookup_object(addr, db);
	if (!obj && state != ODEBUG_STATE_NONE) {
		printk(KERN_ERR "ODEBUG: selftest object not found\n");
		WARN_ON(1);
		goto out;
	}
	if (obj && obj->state != state) {
		printk(KERN_ERR "ODEBUG: selftest wrong state: %d != %d\n",
		       obj->state, state);
		WARN_ON(1);
		goto out;
	}
	if (fixups != debug_objects_fixups) {
		printk(KERN_ERR "ODEBUG: selftest fixups failed %d != %d\n",
		       fixups, debug_objects_fixups);
		WARN_ON(1);
		goto out;
	}
	if (warnings != debug_objects_warnings) {
		printk(KERN_ERR "ODEBUG: selftest warnings failed %d != %d\n",
		       warnings, debug_objects_warnings);
		WARN_ON(1);
		goto out;
	}
	res = 0;
out:
	spin_unlock_irqrestore(&db->lock, flags);
	if (res)
		debug_objects_enabled = 0;
	return res;
}

static __initdata struct debug_obj_descr descr_type_test = {
	.name			= "selftest",
	.fixup_init		= fixup_init,
	.fixup_activate		= fixup_activate,
	.fixup_destroy		= fixup_destroy,
	.fixup_free		= fixup_free,
};

static __initdata struct self_test obj = { .static_init = 0 };

static void __init debug_objects_selftest(void)
{
	int fixups, oldfixups, warnings, oldwarnings;
	unsigned long flags;

	local_irq_save(flags);

	fixups = oldfixups = debug_objects_fixups;
	warnings = oldwarnings = debug_objects_warnings;
	descr_test = &descr_type_test;

	debug_object_init(&obj, &descr_type_test);
	if (check_results(&obj, ODEBUG_STATE_INIT, fixups, warnings))
		goto out;
	debug_object_activate(&obj, &descr_type_test);
	if (check_results(&obj, ODEBUG_STATE_ACTIVE, fixups, warnings))
		goto out;
	debug_object_activate(&obj, &descr_type_test);
	if (check_results(&obj, ODEBUG_STATE_ACTIVE, ++fixups, ++warnings))
		goto out;
	debug_object_deactivate(&obj, &descr_type_test);
	if (check_results(&obj, ODEBUG_STATE_INACTIVE, fixups, warnings))
		goto out;
	debug_object_destroy(&obj, &descr_type_test);
	if (check_results(&obj, ODEBUG_STATE_DESTROYED, fixups, warnings))
		goto out;
	debug_object_init(&obj, &descr_type_test);
	if (check_results(&obj, ODEBUG_STATE_DESTROYED, fixups, ++warnings))
		goto out;
	debug_object_activate(&obj, &descr_type_test);
	if (check_results(&obj, ODEBUG_STATE_DESTROYED, fixups, ++warnings))
		goto out;
	debug_object_deactivate(&obj, &descr_type_test);
	if (check_results(&obj, ODEBUG_STATE_DESTROYED, fixups, ++warnings))
		goto out;
	debug_object_free(&obj, &descr_type_test);
	if (check_results(&obj, ODEBUG_STATE_NONE, fixups, warnings))
		goto out;

	obj.static_init = 1;
	debug_object_activate(&obj, &descr_type_test);
	if (check_results(&obj, ODEBUG_STATE_ACTIVE, ++fixups, warnings))
		goto out;
	debug_object_init(&obj, &descr_type_test);
	if (check_results(&obj, ODEBUG_STATE_INIT, ++fixups, ++warnings))
		goto out;
	debug_object_free(&obj, &descr_type_test);
	if (check_results(&obj, ODEBUG_STATE_NONE, fixups, warnings))
		goto out;

#ifdef CONFIG_DEBUG_OBJECTS_FREE
	debug_object_init(&obj, &descr_type_test);
	if (check_results(&obj, ODEBUG_STATE_INIT, fixups, warnings))
		goto out;
	debug_object_activate(&obj, &descr_type_test);
	if (check_results(&obj, ODEBUG_STATE_ACTIVE, fixups, warnings))
		goto out;
	__debug_check_no_obj_freed(&obj, sizeof(obj));
	if (check_results(&obj, ODEBUG_STATE_NONE, ++fixups, ++warnings))
		goto out;
#endif
	printk(KERN_INFO "ODEBUG: selftest passed\n");

out:
	debug_objects_fixups = oldfixups;
	debug_objects_warnings = oldwarnings;
	descr_test = NULL;

	local_irq_restore(flags);
}
#else
static inline void debug_objects_selftest(void) { }
#endif

/*
 * Called during early boot to initialize the hash buckets and link
 * the static object pool objects into the poll list. After this call
 * the object tracker is fully operational.
 */
void __init debug_objects_early_init(void)
{
	int i;

	for (i = 0; i < ODEBUG_HASH_SIZE; i++)
		spin_lock_init(&obj_hash[i].lock);

	for (i = 0; i < ODEBUG_POOL_SIZE; i++)
		hlist_add_head(&obj_static_pool[i].node, &obj_pool);
}

/*
 * Called after the kmem_caches are functional to setup a dedicated
 * cache pool, which has the SLAB_DEBUG_OBJECTS flag set. This flag
 * prevents that the debug code is called on kmem_cache_free() for the
 * debug tracker objects to avoid recursive calls.
 */
void __init debug_objects_mem_init(void)
{
	if (!debug_objects_enabled)
		return;

	obj_cache = kmem_cache_create("debug_objects_cache",
				      sizeof (struct debug_obj), 0,
				      SLAB_DEBUG_OBJECTS, NULL);

	if (!obj_cache)
		debug_objects_enabled = 0;
	else
		debug_objects_selftest();
}
