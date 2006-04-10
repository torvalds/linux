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
#include <asm/semaphore.h>
#include <linux/security.h>

struct flow_cache_entry {
	struct flow_cache_entry	*next;
	u16			family;
	u8			dir;
	struct flowi		key;
	u32			genid;
	u32			sk_sid;
	void			*object;
	atomic_t		*object_ref;
};

atomic_t flow_cache_genid = ATOMIC_INIT(0);

static u32 flow_hash_shift;
#define flow_hash_size	(1 << flow_hash_shift)
static DEFINE_PER_CPU(struct flow_cache_entry **, flow_tables) = { NULL };

#define flow_table(cpu) (per_cpu(flow_tables, cpu))

static kmem_cache_t *flow_cachep __read_mostly;

static int flow_lwm, flow_hwm;

struct flow_percpu_info {
	int hash_rnd_recalc;
	u32 hash_rnd;
	int count;
} ____cacheline_aligned;
static DEFINE_PER_CPU(struct flow_percpu_info, flow_hash_info) = { 0 };

#define flow_hash_rnd_recalc(cpu) \
	(per_cpu(flow_hash_info, cpu).hash_rnd_recalc)
#define flow_hash_rnd(cpu) \
	(per_cpu(flow_hash_info, cpu).hash_rnd)
#define flow_count(cpu) \
	(per_cpu(flow_hash_info, cpu).count)

static struct timer_list flow_hash_rnd_timer;

#define FLOW_HASH_RND_PERIOD	(10 * 60 * HZ)

struct flow_flush_info {
	atomic_t cpuleft;
	struct completion completion;
};
static DEFINE_PER_CPU(struct tasklet_struct, flow_flush_tasklets) = { NULL };

#define flow_flush_tasklet(cpu) (&per_cpu(flow_flush_tasklets, cpu))

static void flow_cache_new_hashrnd(unsigned long arg)
{
	int i;

	for_each_cpu(i)
		flow_hash_rnd_recalc(i) = 1;

	flow_hash_rnd_timer.expires = jiffies + FLOW_HASH_RND_PERIOD;
	add_timer(&flow_hash_rnd_timer);
}

static void __flow_cache_shrink(int cpu, int shrink_to)
{
	struct flow_cache_entry *fle, **flp;
	int i;

	for (i = 0; i < flow_hash_size; i++) {
		int k = 0;

		flp = &flow_table(cpu)[i];
		while ((fle = *flp) != NULL && k < shrink_to) {
			k++;
			flp = &fle->next;
		}
		while ((fle = *flp) != NULL) {
			*flp = fle->next;
			if (fle->object)
				atomic_dec(fle->object_ref);
			kmem_cache_free(flow_cachep, fle);
			flow_count(cpu)--;
		}
	}
}

static void flow_cache_shrink(int cpu)
{
	int shrink_to = flow_lwm / flow_hash_size;

	__flow_cache_shrink(cpu, shrink_to);
}

static void flow_new_hash_rnd(int cpu)
{
	get_random_bytes(&flow_hash_rnd(cpu), sizeof(u32));
	flow_hash_rnd_recalc(cpu) = 0;

	__flow_cache_shrink(cpu, 0);
}

static u32 flow_hash_code(struct flowi *key, int cpu)
{
	u32 *k = (u32 *) key;

	return (jhash2(k, (sizeof(*key) / sizeof(u32)), flow_hash_rnd(cpu)) &
		(flow_hash_size - 1));
}

#if (BITS_PER_LONG == 64)
typedef u64 flow_compare_t;
#else
typedef u32 flow_compare_t;
#endif

extern void flowi_is_missized(void);

/* I hear what you're saying, use memcmp.  But memcmp cannot make
 * important assumptions that we can here, such as alignment and
 * constant size.
 */
static int flow_key_compare(struct flowi *key1, struct flowi *key2)
{
	flow_compare_t *k1, *k1_lim, *k2;
	const int n_elem = sizeof(struct flowi) / sizeof(flow_compare_t);

	if (sizeof(struct flowi) % sizeof(flow_compare_t))
		flowi_is_missized();

	k1 = (flow_compare_t *) key1;
	k1_lim = k1 + n_elem;

	k2 = (flow_compare_t *) key2;

	do {
		if (*k1++ != *k2++)
			return 1;
	} while (k1 < k1_lim);

	return 0;
}

void *flow_cache_lookup(struct flowi *key, u32 sk_sid, u16 family, u8 dir,
			flow_resolve_t resolver)
{
	struct flow_cache_entry *fle, **head;
	unsigned int hash;
	int cpu;

	local_bh_disable();
	cpu = smp_processor_id();

	fle = NULL;
	/* Packet really early in init?  Making flow_cache_init a
	 * pre-smp initcall would solve this.  --RR */
	if (!flow_table(cpu))
		goto nocache;

	if (flow_hash_rnd_recalc(cpu))
		flow_new_hash_rnd(cpu);
	hash = flow_hash_code(key, cpu);

	head = &flow_table(cpu)[hash];
	for (fle = *head; fle; fle = fle->next) {
		if (fle->family == family &&
		    fle->dir == dir &&
		    fle->sk_sid == sk_sid &&
		    flow_key_compare(key, &fle->key) == 0) {
			if (fle->genid == atomic_read(&flow_cache_genid)) {
				void *ret = fle->object;

				if (ret)
					atomic_inc(fle->object_ref);
				local_bh_enable();

				return ret;
			}
			break;
		}
	}

	if (!fle) {
		if (flow_count(cpu) > flow_hwm)
			flow_cache_shrink(cpu);

		fle = kmem_cache_alloc(flow_cachep, SLAB_ATOMIC);
		if (fle) {
			fle->next = *head;
			*head = fle;
			fle->family = family;
			fle->dir = dir;
			fle->sk_sid = sk_sid;
			memcpy(&fle->key, key, sizeof(*key));
			fle->object = NULL;
			flow_count(cpu)++;
		}
	}

nocache:
	{
		void *obj;
		atomic_t *obj_ref;

		resolver(key, sk_sid, family, dir, &obj, &obj_ref);

		if (fle) {
			fle->genid = atomic_read(&flow_cache_genid);

			if (fle->object)
				atomic_dec(fle->object_ref);

			fle->object = obj;
			fle->object_ref = obj_ref;
			if (obj)
				atomic_inc(fle->object_ref);
		}
		local_bh_enable();

		return obj;
	}
}

static void flow_cache_flush_tasklet(unsigned long data)
{
	struct flow_flush_info *info = (void *)data;
	int i;
	int cpu;

	cpu = smp_processor_id();
	for (i = 0; i < flow_hash_size; i++) {
		struct flow_cache_entry *fle;

		fle = flow_table(cpu)[i];
		for (; fle; fle = fle->next) {
			unsigned genid = atomic_read(&flow_cache_genid);

			if (!fle->object || fle->genid == genid)
				continue;

			fle->object = NULL;
			atomic_dec(fle->object_ref);
		}
	}

	if (atomic_dec_and_test(&info->cpuleft))
		complete(&info->completion);
}

static void flow_cache_flush_per_cpu(void *) __attribute__((__unused__));
static void flow_cache_flush_per_cpu(void *data)
{
	struct flow_flush_info *info = data;
	int cpu;
	struct tasklet_struct *tasklet;

	cpu = smp_processor_id();

	tasklet = flow_flush_tasklet(cpu);
	tasklet->data = (unsigned long)info;
	tasklet_schedule(tasklet);
}

void flow_cache_flush(void)
{
	struct flow_flush_info info;
	static DEFINE_MUTEX(flow_flush_sem);

	/* Don't want cpus going down or up during this. */
	lock_cpu_hotplug();
	mutex_lock(&flow_flush_sem);
	atomic_set(&info.cpuleft, num_online_cpus());
	init_completion(&info.completion);

	local_bh_disable();
	smp_call_function(flow_cache_flush_per_cpu, &info, 1, 0);
	flow_cache_flush_tasklet((unsigned long)&info);
	local_bh_enable();

	wait_for_completion(&info.completion);
	mutex_unlock(&flow_flush_sem);
	unlock_cpu_hotplug();
}

static void __devinit flow_cache_cpu_prepare(int cpu)
{
	struct tasklet_struct *tasklet;
	unsigned long order;

	for (order = 0;
	     (PAGE_SIZE << order) <
		     (sizeof(struct flow_cache_entry *)*flow_hash_size);
	     order++)
		/* NOTHING */;

	flow_table(cpu) = (struct flow_cache_entry **)
		__get_free_pages(GFP_KERNEL|__GFP_ZERO, order);
	if (!flow_table(cpu))
		panic("NET: failed to allocate flow cache order %lu\n", order);

	flow_hash_rnd_recalc(cpu) = 1;
	flow_count(cpu) = 0;

	tasklet = flow_flush_tasklet(cpu);
	tasklet_init(tasklet, flow_cache_flush_tasklet, 0);
}

#ifdef CONFIG_HOTPLUG_CPU
static int flow_cache_cpu(struct notifier_block *nfb,
			  unsigned long action,
			  void *hcpu)
{
	if (action == CPU_DEAD)
		__flow_cache_shrink((unsigned long)hcpu, 0);
	return NOTIFY_OK;
}
#endif /* CONFIG_HOTPLUG_CPU */

static int __init flow_cache_init(void)
{
	int i;

	flow_cachep = kmem_cache_create("flow_cache",
					sizeof(struct flow_cache_entry),
					0, SLAB_HWCACHE_ALIGN,
					NULL, NULL);

	if (!flow_cachep)
		panic("NET: failed to allocate flow cache slab\n");

	flow_hash_shift = 10;
	flow_lwm = 2 * flow_hash_size;
	flow_hwm = 4 * flow_hash_size;

	init_timer(&flow_hash_rnd_timer);
	flow_hash_rnd_timer.function = flow_cache_new_hashrnd;
	flow_hash_rnd_timer.expires = jiffies + FLOW_HASH_RND_PERIOD;
	add_timer(&flow_hash_rnd_timer);

	for_each_cpu(i)
		flow_cache_cpu_prepare(i);

	hotcpu_notifier(flow_cache_cpu, 0);
	return 0;
}

module_init(flow_cache_init);

EXPORT_SYMBOL(flow_cache_genid);
EXPORT_SYMBOL(flow_cache_lookup);
