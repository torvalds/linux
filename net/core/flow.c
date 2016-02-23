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
#include <linux/atomic.h>
#include <linux/security.h>
#include <net/net_namespace.h>

struct flow_cache_entry {
	union {
		struct hlist_node	hlist;
		struct list_head	gc_list;
	} u;
	struct net			*net;
	u16				family;
	u8				dir;
	u32				genid;
	struct flowi			key;
	struct flow_cache_object	*object;
};

struct flow_flush_info {
	struct flow_cache		*cache;
	atomic_t			cpuleft;
	struct completion		completion;
};

static struct kmem_cache *flow_cachep __read_mostly;

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

static int flow_entry_valid(struct flow_cache_entry *fle,
				struct netns_xfrm *xfrm)
{
	if (atomic_read(&xfrm->flow_cache_genid) != fle->genid)
		return 0;
	if (fle->object && !fle->object->ops->check(fle->object))
		return 0;
	return 1;
}

static void flow_entry_kill(struct flow_cache_entry *fle,
				struct netns_xfrm *xfrm)
{
	if (fle->object)
		fle->object->ops->delete(fle->object);
	kmem_cache_free(flow_cachep, fle);
}

static void flow_cache_gc_task(struct work_struct *work)
{
	struct list_head gc_list;
	struct flow_cache_entry *fce, *n;
	struct netns_xfrm *xfrm = container_of(work, struct netns_xfrm,
						flow_cache_gc_work);

	INIT_LIST_HEAD(&gc_list);
	spin_lock_bh(&xfrm->flow_cache_gc_lock);
	list_splice_tail_init(&xfrm->flow_cache_gc_list, &gc_list);
	spin_unlock_bh(&xfrm->flow_cache_gc_lock);

	list_for_each_entry_safe(fce, n, &gc_list, u.gc_list)
		flow_entry_kill(fce, xfrm);
}

static void flow_cache_queue_garbage(struct flow_cache_percpu *fcp,
				     int deleted, struct list_head *gc_list,
				     struct netns_xfrm *xfrm)
{
	if (deleted) {
		fcp->hash_count -= deleted;
		spin_lock_bh(&xfrm->flow_cache_gc_lock);
		list_splice_tail(gc_list, &xfrm->flow_cache_gc_list);
		spin_unlock_bh(&xfrm->flow_cache_gc_lock);
		schedule_work(&xfrm->flow_cache_gc_work);
	}
}

static void __flow_cache_shrink(struct flow_cache *fc,
				struct flow_cache_percpu *fcp,
				int shrink_to)
{
	struct flow_cache_entry *fle;
	struct hlist_node *tmp;
	LIST_HEAD(gc_list);
	int i, deleted = 0;
	struct netns_xfrm *xfrm = container_of(fc, struct netns_xfrm,
						flow_cache_global);

	for (i = 0; i < flow_cache_hash_size(fc); i++) {
		int saved = 0;

		hlist_for_each_entry_safe(fle, tmp,
					  &fcp->hash_table[i], u.hlist) {
			if (saved < shrink_to &&
			    flow_entry_valid(fle, xfrm)) {
				saved++;
			} else {
				deleted++;
				hlist_del(&fle->u.hlist);
				list_add_tail(&fle->u.gc_list, &gc_list);
			}
		}
	}

	flow_cache_queue_garbage(fcp, deleted, &gc_list, xfrm);
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
			  const struct flowi *key,
			  size_t keysize)
{
	const u32 *k = (const u32 *) key;
	const u32 length = keysize * sizeof(flow_compare_t) / sizeof(u32);

	return jhash2(k, length, fcp->hash_rnd)
		& (flow_cache_hash_size(fc) - 1);
}

/* I hear what you're saying, use memcmp.  But memcmp cannot make
 * important assumptions that we can here, such as alignment.
 */
static int flow_key_compare(const struct flowi *key1, const struct flowi *key2,
			    size_t keysize)
{
	const flow_compare_t *k1, *k1_lim, *k2;

	k1 = (const flow_compare_t *) key1;
	k1_lim = k1 + keysize;

	k2 = (const flow_compare_t *) key2;

	do {
		if (*k1++ != *k2++)
			return 1;
	} while (k1 < k1_lim);

	return 0;
}

struct flow_cache_object *
flow_cache_lookup(struct net *net, const struct flowi *key, u16 family, u8 dir,
		  flow_resolve_t resolver, void *ctx)
{
	struct flow_cache *fc = &net->xfrm.flow_cache_global;
	struct flow_cache_percpu *fcp;
	struct flow_cache_entry *fle, *tfle;
	struct flow_cache_object *flo;
	size_t keysize;
	unsigned int hash;

	local_bh_disable();
	fcp = this_cpu_ptr(fc->percpu);

	fle = NULL;
	flo = NULL;

	keysize = flow_key_size(family);
	if (!keysize)
		goto nocache;

	/* Packet really early in init?  Making flow_cache_init a
	 * pre-smp initcall would solve this.  --RR */
	if (!fcp->hash_table)
		goto nocache;

	if (fcp->hash_rnd_recalc)
		flow_new_hash_rnd(fc, fcp);

	hash = flow_hash_code(fc, fcp, key, keysize);
	hlist_for_each_entry(tfle, &fcp->hash_table[hash], u.hlist) {
		if (tfle->net == net &&
		    tfle->family == family &&
		    tfle->dir == dir &&
		    flow_key_compare(key, &tfle->key, keysize) == 0) {
			fle = tfle;
			break;
		}
	}

	if (unlikely(!fle)) {
		if (fcp->hash_count > fc->high_watermark)
			flow_cache_shrink(fc, fcp);

		fle = kmem_cache_alloc(flow_cachep, GFP_ATOMIC);
		if (fle) {
			fle->net = net;
			fle->family = family;
			fle->dir = dir;
			memcpy(&fle->key, key, keysize * sizeof(flow_compare_t));
			fle->object = NULL;
			hlist_add_head(&fle->u.hlist, &fcp->hash_table[hash]);
			fcp->hash_count++;
		}
	} else if (likely(fle->genid == atomic_read(&net->xfrm.flow_cache_genid))) {
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
		fle->genid = atomic_read(&net->xfrm.flow_cache_genid);
		if (!IS_ERR(flo))
			fle->object = flo;
		else
			fle->genid--;
	} else {
		if (!IS_ERR_OR_NULL(flo))
			flo->ops->delete(flo);
	}
ret_object:
	local_bh_enable();
	return flo;
}
EXPORT_SYMBOL(flow_cache_lookup);

static void flow_cache_flush_tasklet(unsigned long data)
{
	struct flow_flush_info *info = (void *)data;
	struct flow_cache *fc = info->cache;
	struct flow_cache_percpu *fcp;
	struct flow_cache_entry *fle;
	struct hlist_node *tmp;
	LIST_HEAD(gc_list);
	int i, deleted = 0;
	struct netns_xfrm *xfrm = container_of(fc, struct netns_xfrm,
						flow_cache_global);

	fcp = this_cpu_ptr(fc->percpu);
	for (i = 0; i < flow_cache_hash_size(fc); i++) {
		hlist_for_each_entry_safe(fle, tmp,
					  &fcp->hash_table[i], u.hlist) {
			if (flow_entry_valid(fle, xfrm))
				continue;

			deleted++;
			hlist_del(&fle->u.hlist);
			list_add_tail(&fle->u.gc_list, &gc_list);
		}
	}

	flow_cache_queue_garbage(fcp, deleted, &gc_list, xfrm);

	if (atomic_dec_and_test(&info->cpuleft))
		complete(&info->completion);
}

/*
 * Return whether a cpu needs flushing.  Conservatively, we assume
 * the presence of any entries means the core may require flushing,
 * since the flow_cache_ops.check() function may assume it's running
 * on the same core as the per-cpu cache component.
 */
static int flow_cache_percpu_empty(struct flow_cache *fc, int cpu)
{
	struct flow_cache_percpu *fcp;
	int i;

	fcp = per_cpu_ptr(fc->percpu, cpu);
	for (i = 0; i < flow_cache_hash_size(fc); i++)
		if (!hlist_empty(&fcp->hash_table[i]))
			return 0;
	return 1;
}

static void flow_cache_flush_per_cpu(void *data)
{
	struct flow_flush_info *info = data;
	struct tasklet_struct *tasklet;

	tasklet = &this_cpu_ptr(info->cache->percpu)->flush_tasklet;
	tasklet->data = (unsigned long)info;
	tasklet_schedule(tasklet);
}

void flow_cache_flush(struct net *net)
{
	struct flow_flush_info info;
	cpumask_var_t mask;
	int i, self;

	/* Track which cpus need flushing to avoid disturbing all cores. */
	if (!alloc_cpumask_var(&mask, GFP_KERNEL))
		return;
	cpumask_clear(mask);

	/* Don't want cpus going down or up during this. */
	get_online_cpus();
	mutex_lock(&net->xfrm.flow_flush_sem);
	info.cache = &net->xfrm.flow_cache_global;
	for_each_online_cpu(i)
		if (!flow_cache_percpu_empty(info.cache, i))
			cpumask_set_cpu(i, mask);
	atomic_set(&info.cpuleft, cpumask_weight(mask));
	if (atomic_read(&info.cpuleft) == 0)
		goto done;

	init_completion(&info.completion);

	local_bh_disable();
	self = cpumask_test_and_clear_cpu(smp_processor_id(), mask);
	on_each_cpu_mask(mask, flow_cache_flush_per_cpu, &info, 0);
	if (self)
		flow_cache_flush_tasklet((unsigned long)&info);
	local_bh_enable();

	wait_for_completion(&info.completion);

done:
	mutex_unlock(&net->xfrm.flow_flush_sem);
	put_online_cpus();
	free_cpumask_var(mask);
}

static void flow_cache_flush_task(struct work_struct *work)
{
	struct netns_xfrm *xfrm = container_of(work, struct netns_xfrm,
						flow_cache_flush_work);
	struct net *net = container_of(xfrm, struct net, xfrm);

	flow_cache_flush(net);
}

void flow_cache_flush_deferred(struct net *net)
{
	schedule_work(&net->xfrm.flow_cache_flush_work);
}

static int flow_cache_cpu_prepare(struct flow_cache *fc, int cpu)
{
	struct flow_cache_percpu *fcp = per_cpu_ptr(fc->percpu, cpu);
	size_t sz = sizeof(struct hlist_head) * flow_cache_hash_size(fc);

	if (!fcp->hash_table) {
		fcp->hash_table = kzalloc_node(sz, GFP_KERNEL, cpu_to_node(cpu));
		if (!fcp->hash_table) {
			pr_err("NET: failed to allocate flow cache sz %zu\n", sz);
			return -ENOMEM;
		}
		fcp->hash_rnd_recalc = 1;
		fcp->hash_count = 0;
		tasklet_init(&fcp->flush_tasklet, flow_cache_flush_tasklet, 0);
	}
	return 0;
}

static int flow_cache_cpu(struct notifier_block *nfb,
			  unsigned long action,
			  void *hcpu)
{
	struct flow_cache *fc = container_of(nfb, struct flow_cache,
						hotcpu_notifier);
	int res, cpu = (unsigned long) hcpu;
	struct flow_cache_percpu *fcp = per_cpu_ptr(fc->percpu, cpu);

	switch (action) {
	case CPU_UP_PREPARE:
	case CPU_UP_PREPARE_FROZEN:
		res = flow_cache_cpu_prepare(fc, cpu);
		if (res)
			return notifier_from_errno(res);
		break;
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		__flow_cache_shrink(fc, fcp, 0);
		break;
	}
	return NOTIFY_OK;
}

int flow_cache_init(struct net *net)
{
	int i;
	struct flow_cache *fc = &net->xfrm.flow_cache_global;

	if (!flow_cachep)
		flow_cachep = kmem_cache_create("flow_cache",
						sizeof(struct flow_cache_entry),
						0, SLAB_PANIC, NULL);
	spin_lock_init(&net->xfrm.flow_cache_gc_lock);
	INIT_LIST_HEAD(&net->xfrm.flow_cache_gc_list);
	INIT_WORK(&net->xfrm.flow_cache_gc_work, flow_cache_gc_task);
	INIT_WORK(&net->xfrm.flow_cache_flush_work, flow_cache_flush_task);
	mutex_init(&net->xfrm.flow_flush_sem);

	fc->hash_shift = 10;
	fc->low_watermark = 2 * flow_cache_hash_size(fc);
	fc->high_watermark = 4 * flow_cache_hash_size(fc);

	fc->percpu = alloc_percpu(struct flow_cache_percpu);
	if (!fc->percpu)
		return -ENOMEM;

	cpu_notifier_register_begin();

	for_each_online_cpu(i) {
		if (flow_cache_cpu_prepare(fc, i))
			goto err;
	}
	fc->hotcpu_notifier = (struct notifier_block){
		.notifier_call = flow_cache_cpu,
	};
	__register_hotcpu_notifier(&fc->hotcpu_notifier);

	cpu_notifier_register_done();

	setup_timer(&fc->rnd_timer, flow_cache_new_hashrnd,
		    (unsigned long) fc);
	fc->rnd_timer.expires = jiffies + FLOW_HASH_RND_PERIOD;
	add_timer(&fc->rnd_timer);

	return 0;

err:
	for_each_possible_cpu(i) {
		struct flow_cache_percpu *fcp = per_cpu_ptr(fc->percpu, i);
		kfree(fcp->hash_table);
		fcp->hash_table = NULL;
	}

	cpu_notifier_register_done();

	free_percpu(fc->percpu);
	fc->percpu = NULL;

	return -ENOMEM;
}
EXPORT_SYMBOL(flow_cache_init);

void flow_cache_fini(struct net *net)
{
	int i;
	struct flow_cache *fc = &net->xfrm.flow_cache_global;

	del_timer_sync(&fc->rnd_timer);
	unregister_hotcpu_notifier(&fc->hotcpu_notifier);

	for_each_possible_cpu(i) {
		struct flow_cache_percpu *fcp = per_cpu_ptr(fc->percpu, i);
		kfree(fcp->hash_table);
		fcp->hash_table = NULL;
	}

	free_percpu(fc->percpu);
	fc->percpu = NULL;
}
EXPORT_SYMBOL(flow_cache_fini);
