/* flow.c: Generic flow cache.
 *
 * Copyright (C) 2003 Alexey N. Kuznetsov (kuznet@ms2.inr.ac.ru)
 * Copyright (C) 2003 David S. Miller (davem@redhat.com)
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/jhash.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/random.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/completion.h>
#include <linux/percpu.h>
#include <linux/bitops.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/mutex.h>
#include <net/flow.h>
#include <asm/atomic.h>
#include <linux/security.h>

struct flow_cache_entry {
	union {
		struct hlist_node	hlist;
		struct list_head	gc_list;
	} u;
	u16				family;
	u8				dir;
	u32				genid;
	struct flowi			key;
	struct flow_cache_object	*object;
};

struct flow_cache_percpu {
	struct hlist_head		*hash_table;
	int				hash_count;
	u32				hash_rnd;
	int				hash_rnd_recalc;
	struct tasklet_struct		flush_tasklet;
};

struct flow_flush_info {
	struct flow_cache		*cache;
	atomic_t			cpuleft;
	struct completion		completion;
};

struct flow_cache {
	u32				hash_shift;
	unsigned long			order;
	struct flow_cache_percpu	*percpu;
	struct notifier_block		hotcpu_notifier;
	int				low_watermark;
	int				high_watermark;
	struct timer_list		rnd_timer;
};

atomic_t flow_cache_genid = ATOMIC_INIT(0);
static struct flow_cache flow_cache_global;
static struct kmem_cache *flow_cachep;

static DEFINE_SPINLOCK(flow_cache_gc_lock);
static LIST_HEAD(flow_cache_gc_list);

#define flow_cache_hash_size(cache)	(1 << (cache)->hash_shift)
#define FLOW_HASH_RND_PERIOD		(10 * 60 * HZ)

static void flow_cache_new_hashrnd(unsigned long arg)
{
	struct flow_cache *fc = (void *) arg;
	int i;

	for_each_possible_cpu(i)
		per_cpu_ptr(fc->percpu, i)->hash_rnd_recalc = 1;

	fc->rnd_timer.expires = jiffies + FLOW_HASH_RND_PERIOD;
	add_timer(&fc->rnd_timer);
}

static int flow_entry_valid(struct flow_cache_entry *fle)
{
	if (atomic_read(&flow_cache_genid) != fle->genid)
		return 0;
	if (fle->object && !fle->object->ops->check(fle->object))
		return 0;
	return 1;
}

static void flow_entry_kill(struct flow_cache_entry *fle)
{
	if (fle->object)
		fle->object->ops->delete(fle->object);
	kmem_cache_free(flow_cachep, fle);
}

static void flow_cache_gc_task(struct work_struct *work)
{
	struct list_head gc_list;
	struct flow_cache_entry *fce, *n;

	INIT_LIST_HEAD(&gc_list);
	spin_lock_bh(&flow_cache_gc_lock);
	list_splice_tail_init(&flow_cache_gc_list, &gc_list);
	spin_unlock_bh(&flow_cache_gc_lock);

	list_for_each_entry_safe(fce, n, &gc_list, u.gc_list)
		flow_entry_kill(fce);
}
static DECLARE_WORK(flow_cache_gc_work, flow_cache_gc_task);

static void flow_cache_queue_garbage(struct flow_cache_percpu *fcp,
				     int deleted, struct list_head *gc_list)
{
	if (deleted) {
		fcp->hash_count -= deleted;
		spin_lock_bh(&flow_cache_gc_lock);
		list_splice_tail(gc_list, &flow_cache_gc_list);
		spin_unlock_bh(&flow_cache_gc_lock);
		schedule_work(&flow_cache_gc_work);
	}
}

static void __flow_cache_shrink(struct flow_cache *fc,
				struct flow_cache_percpu *fcp,
				int shrink_to)
{
	struct flow_cache_entry *fle;
	struct hlist_node *entry, *tmp;
	LIST_HEAD(gc_list);
	int i, deleted = 0;

	for (i = 0; i < flow_cache_hash_size(fc); i++) {
		int saved = 0;

		hlist_for_each_entry_safe(fle, entry, tmp,
					  &fcp->hash_table[i], u.hlist) {
			if (saved < shrink_to &&
			    flow_entry_valid(fle)) {
				saved++;
			} else {
				deleted++;
				hlist_del(&fle->u.hlist);
				list_add_tail(&fle->u.gc_list, &gc_list);
			}
		}
	}

	flow_cache_queue_garbage(fcp, deleted, &gc_list);
}

static void flow_cache_shrink(struct flow_cache *fc,
			      struct flow_cache_percpu *fcp)
{
	int shrink_to = fc->low_watermark / flow_cache_hash_size(fc);

	__flow_cache_shrink(fc, fcp, shrink_to);
}

static void flow_new_hash_rnd(struct flow_cache *fc,
			      struct flow_cache_percpu *fcp)
{
	get_random_bytes(&fcp->hash_rnd, sizeof(u32));
	fcp->hash_rnd_recalc = 0;
	__flow_cache_shrink(fc, fcp, 0);
}

static u32 flow_hash_code(struct flow_cache *fc,
			  struct flow_cache_percpu *fcp,
			  struct flowi *key)
{
	u32 *k = (u32 *) key;

	return (jhash2(k, (sizeof(*key) / sizeof(u32)), fcp->hash_rnd)
		& (flow_cache_hash_size(fc) - 1));
}

#if (BITS_PER_LONG == 64)
typedef u64 flow_compare_t;
#else
typedef u32 flow_compare_t;
#endif

/* I hear what you're saying, use memcmp.  But memcmp cannot make
 * important assumptions that we can here, such as alignment and
 * constant size.
 */
static int flow_key_compare(struct flowi *key1, struct flowi *key2)
{
	flow_compare_t *k1, *k1_lim, *k2;
	const int n_elem = sizeof(struct flowi) / sizeof(flow_compare_t);

	BUILD_BUG_ON(sizeof(struct flowi) % sizeof(flow_compare_t));

	k1 = (flow_compare_t *) key1;
	k1_lim = k1 + n_elem;

	k2 = (flow_compare_t *) key2;

	do {
		if (*k1++ != *k2++)
			return 1;
	} while (k1 < k1_lim);

	return 0;
}

struct flow_cache_object *
flow_cache_lookup(struct net *net, struct flowi *key, u16 family, u8 dir,
		  flow_resolve_t resolver, void *ctx)
{
	struct flow_cache *fc = &flow_cache_global;
	struct flow_cache_percpu *fcp;
	struct flow_cache_entry *fle, *tfle;
	struct hlist_node *entry;
	struct flow_cache_object *flo;
	unsigned int hash;

	local_bh_disable();
	fcp = per_cpu_ptr(fc->percpu, smp_processor_id());

	fle = NULL;
	flo = NULL;
	/* Packet really early in init?  Making flow_cache_init a
	 * pre-smp initcall would solve this.  --RR */
	if (!fcp->hash_table)
		goto nocache;

	if (fcp->hash_rnd_recalc)
		flow_new_hash_rnd(fc, fcp);

	hash = flow_hash_code(fc, fcp, key);
	hlist_for_each_entry(tfle, entry, &fcp->hash_table[hash], u.hlist) {
		if (tfle->family == family &&
		    tfle->dir == dir &&
		    flow_key_compare(key, &tfle->key) == 0) {
			fle = tfle;
			break;
		}
	}

	if (unlikely(!fle)) {
		if (fcp->hash_count > fc->high_watermark)
			flow_cache_shrink(fc, fcp);

		fle = kmem_cache_alloc(flow_cachep, GFP_ATOMIC);
		if (fle) {
			fle->family = family;
			fle->dir = dir;
			memcpy(&fle->key, key, sizeof(*key));
			fle->object = NULL;
			hlist_add_head(&fle->u.hlist, &fcp->hash_table[hash]);
			fcp->hash_count++;
		}
	} else if (likely(fle->genid == atomic_read(&flow_cache_genid))) {
		flo = fle->object;
		if (!flo)
			goto ret_object;
		flo = flo->ops->get(flo);
		if (flo)
			goto ret_object;
	} else if (fle->object) {
	        flo = fle->object;
	        flo->ops->delete(flo);
	        fle->object = NULL;
	}

nocache:
	flo = NULL;
	if (fle) {
		flo = fle->object;
		fle->object = NULL;
	}
	flo = resolver(net, key, family, dir, flo, ctx);
	if (fle) {
		fle->genid = atomic_read(&flow_cache_genid);
		if (!IS_ERR(flo))
			fle->object = flo;
		else
			fle->genid--;
	} else {
		if (flo && !IS_ERR(flo))
			flo->ops->delete(flo);
	}
ret_object:
	local_bh_enable();
	return flo;
}

static void flow_cache_flush_tasklet(unsigned long data)
{
	struct flow_flush_info *info = (void *)data;
	struct flow_cache *fc = info->cache;
	struct flow_cache_percpu *fcp;
	struct flow_cache_entry *fle;
	struct hlist_node *entry, *tmp;
	LIST_HEAD(gc_list);
	int i, deleted = 0;

	fcp = per_cpu_ptr(fc->percpu, smp_processor_id());
	for (i = 0; i < flow_cache_hash_size(fc); i++) {
		hlist_for_each_entry_safe(fle, entry, tmp,
					  &fcp->hash_table[i], u.hlist) {
			if (flow_entry_valid(fle))
				continue;

			deleted++;
			hlist_del(&fle->u.hlist);
			list_add_tail(&fle->u.gc_list, &gc_list);
		}
	}

	flow_cache_queue_garbage(fcp, deleted, &gc_list);

	if (atomic_dec_and_test(&info->cpuleft))
		complete(&info->completion);
}

static void flow_cache_flush_per_cpu(void *data)
{
	struct flow_flush_info *info = data;
	int cpu;
	struct tasklet_struct *tasklet;

	cpu = smp_processor_id();
	tasklet = &per_cpu_ptr(info->cache->percpu, cpu)->flush_tasklet;
	tasklet->data = (unsigned long)info;
	tasklet_schedule(tasklet);
}

void flow_cache_flush(void)
{
	struct flow_flush_info info;
	static DEFINE_MUTEX(flow_flush_sem);

	/* Don't want cpus going down or up during this. */
	get_online_cpus();
	mutex_lock(&flow_flush_sem);
	info.cache = &flow_cache_global;
	atomic_set(&info.cpuleft, num_online_cpus());
	init_completion(&info.completion);

	local_bh_disable();
	smp_call_function(flow_cache_flush_per_cpu, &info, 0);
	flow_cache_flush_tasklet((unsigned long)&info);
	local_bh_enable();

	wait_for_completion(&info.completion);
	mutex_unlock(&flow_flush_sem);
	put_online_cpus();
}

static void __init flow_cache_cpu_prepare(struct flow_cache *fc,
					  struct flow_cache_percpu *fcp)
{
	fcp->hash_table = (struct hlist_head *)
		__get_free_pages(GFP_KERNEL|__GFP_ZERO, fc->order);
	if (!fcp->hash_table)
		panic("NET: failed to allocate flow cache order %lu\n", fc->order);

	fcp->hash_rnd_recalc = 1;
	fcp->hash_count = 0;
	tasklet_init(&fcp->flush_tasklet, flow_cache_flush_tasklet, 0);
}

static int flow_cache_cpu(struct notifier_block *nfb,
			  unsigned long action,
			  void *hcpu)
{
	struct flow_cache *fc = container_of(nfb, struct flow_cache, hotcpu_notifier);
	int cpu = (unsigned long) hcpu;
	struct flow_cache_percpu *fcp = per_cpu_ptr(fc->percpu, cpu);

	if (action == CPU_DEAD || action == CPU_DEAD_FROZEN)
		__flow_cache_shrink(fc, fcp, 0);
	return NOTIFY_OK;
}

static int flow_cache_init(struct flow_cache *fc)
{
	unsigned long order;
	int i;

	fc->hash_shift = 10;
	fc->low_watermark = 2 * flow_cache_hash_size(fc);
	fc->high_watermark = 4 * flow_cache_hash_size(fc);

	for (order = 0;
	     (PAGE_SIZE << order) <
		     (sizeof(struct hlist_head)*flow_cache_hash_size(fc));
	     order++)
		/* NOTHING */;
	fc->order = order;
	fc->percpu = alloc_percpu(struct flow_cache_percpu);

	setup_timer(&fc->rnd_timer, flow_cache_new_hashrnd,
		    (unsigned long) fc);
	fc->rnd_timer.expires = jiffies + FLOW_HASH_RND_PERIOD;
	add_timer(&fc->rnd_timer);

	for_each_possible_cpu(i)
		flow_cache_cpu_prepare(fc, per_cpu_ptr(fc->percpu, i));

	fc->hotcpu_notifier = (struct notifier_block){
		.notifier_call = flow_cache_cpu,
	};
	register_hotcpu_notifier(&fc->hotcpu_notifier);

	return 0;
}

static int __init flow_cache_init_global(void)
{
	flow_cachep = kmem_cache_create("flow_cache",
					sizeof(struct flow_cache_entry),
					0, SLAB_PANIC, NULL);

	return flow_cache_init(&flow_cache_global);
}

module_init(flow_cache_init_global);

EXPORT_SYMBOL(flow_cache_genid);
EXPORT_SYMBOL(flow_cache_lookup);
