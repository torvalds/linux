/* Kasan quarantine */

#include <linux/gfp.h>
#include <linux/hash.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/percpu.h>
#include <linux/printk.h>
#include <linux/shrinker.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>

#include "../slab.h"
#include "kasan.h"

/* Data structure and operations for quarantine queues */

/* Each queue is a signled-linked list, which also stores the total size of
 * objects inside of it */
struct qlist {
	void **head;
	void **tail;
	size_t bytes;
};

#define QLIST_INIT { NULL, NULL, 0 }

static inline bool empty_qlist(struct qlist *q)
{
	return !q->head;
}

static inline void init_qlist(struct qlist *q)
{
	q->head = q->tail = NULL;
	q->bytes = 0;
}

static inline void qlist_put(struct qlist *q, void **qlink, size_t size)
{
	if (unlikely(empty_qlist(q)))
		q->head = qlink;
	else
		*q->tail = qlink;
	q->tail = qlink;
	*qlink = NULL;
	q->bytes += size;
}

static inline void** qlist_remove(struct qlist *q, void ***prev,
				 size_t size) {
	void **qlink = *prev;

	*prev = *qlink;
	if (q->tail == qlink) {
		if (q->head == qlink)
			q->tail = NULL;
		else
			q->tail = (void**) prev;
	}
	q->bytes -= size;

	return qlink;
}

static inline void qlist_move_all(struct qlist *from, struct qlist *to)
{
	if (unlikely(empty_qlist(from)))
		return;

	if (empty_qlist(to)) {
		*to = *from;
		init_qlist(from);
		return;
	}

	*to->tail = from->head;
	to->tail = from->tail;
	to->bytes += from->bytes;

	init_qlist(from);
}

static inline void qlist_move(struct qlist *from, void **last, struct qlist *to,
			  size_t size)
{
	if (unlikely(last == from->tail)) {
		qlist_move_all(from, to);
		return;
	}
	if (empty_qlist(to))
		to->head = from->head;
	else
		*to->tail = from->head;
	to->tail = last;
	from->head = *last;
	*last = NULL;
	from->bytes -= size;
	to->bytes += size;
}


/* The object quarantine consists of per-cpu queues and a global queue,
 * guarded by quarantine_lock */

static DEFINE_PER_CPU(struct qlist, cpu_quarantine);

static struct qlist global_quarantine;
static DEFINE_SPINLOCK(quarantine_lock);

static volatile unsigned long quarantine_size;  /* The maximum size of global
						   queue */

/* The fraction of physical memory the quarantine is allowed to occupy. */
#define QUARANTINE_FRACTION 32 /* Quarantine doesn't support memory shrinker
				  with SLAB allocator, so we keep the ratio
				  low to avoid OOM. */

#define QUARANTINE_LOW_SIZE (quarantine_size * 3 / 4)
#define QUARANTINE_PERCPU_SIZE (1 << 20)

static inline struct kmem_cache *qlink_to_cache(void **qlink)
{
	return virt_to_head_page(qlink)->slab_cache;
}

static inline void *qlink_to_object(void **qlink, struct kmem_cache *cache)
{
	struct kasan_free_meta *free_info =
		container_of((void ***)qlink, struct kasan_free_meta,
			     quarantine_link);

	return ((void *)free_info) - cache->kasan_info.free_meta_offset;
}

static inline void qlink_free(void **qlink, struct kmem_cache *cache)
{
	void *object = qlink_to_object(qlink, cache);
	struct kasan_alloc_meta *alloc_info = get_alloc_info(cache, object);
	unsigned long flags;

	local_irq_save(flags);
	alloc_info->state = KASAN_STATE_FREE;
	nokasan_free(cache, object, _THIS_IP_);
	local_irq_restore(flags);
}

static inline void qlist_free_all(struct qlist *q, struct kmem_cache *cache)
{
	void **qlink;

	if (unlikely(empty_qlist(q)))
		return;

	qlink = q->head;
	while (qlink) {
		struct kmem_cache *obj_cache =
			cache ? cache :	qlink_to_cache(qlink);
		void **next = *qlink;

		qlink_free(qlink, obj_cache);
		qlink = next;
	}
	init_qlist(q);
}

void quarantine_put(struct kasan_free_meta *info, struct kmem_cache *cache)
{
	unsigned long flags;
	struct qlist *q;
	struct qlist temp = QLIST_INIT;

	local_irq_save(flags);

	q = this_cpu_ptr(&cpu_quarantine);
	qlist_put(q, (void **) &info->quarantine_link, cache->size);
	if (unlikely(q->bytes > QUARANTINE_PERCPU_SIZE))
		qlist_move_all(q, &temp);

	local_irq_restore(flags);

	if (unlikely(!empty_qlist(&temp))) {
		spin_lock_irqsave(&quarantine_lock, flags);
		qlist_move_all(&temp, &global_quarantine);
		spin_unlock_irqrestore(&quarantine_lock, flags);
	}
}

void quarantine_reduce(void)
{
	size_t new_quarantine_size;
	unsigned long flags;
	struct qlist to_free = QLIST_INIT;
	size_t size_to_free = 0;
	void **last;

	if (likely(ACCESS_ONCE(global_quarantine.bytes) <=
		   smp_load_acquire(&quarantine_size)))
		return;

	spin_lock_irqsave(&quarantine_lock, flags);

	/* Update quarantine size in case of hotplug. Allocate fraction of installed
	 * memory to quarantine minus per-cpu queue limits. */
	new_quarantine_size = (ACCESS_ONCE(totalram_pages) << PAGE_SHIFT) /
		QUARANTINE_FRACTION;
	new_quarantine_size -= QUARANTINE_PERCPU_SIZE * num_online_cpus();
	smp_store_release(&quarantine_size, new_quarantine_size);

	last = global_quarantine.head;
	while (last) {
		struct kmem_cache *cache = qlink_to_cache(last);

		size_to_free += cache->size;
		if (!*last || size_to_free >
		    global_quarantine.bytes - QUARANTINE_LOW_SIZE)
			break;
		last = (void **) *last;
	}
	qlist_move(&global_quarantine, last, &to_free, size_to_free);

	spin_unlock_irqrestore(&quarantine_lock, flags);

	qlist_free_all(&to_free, NULL);
}

static inline void qlist_move_cache(struct qlist *from,
				   struct qlist *to,
				   struct kmem_cache *cache)
{
	void ***prev;

	if (unlikely(empty_qlist(from)))
		return;

	prev = &from->head;
	while (*prev) {
		void **qlink = *prev;
		struct kmem_cache *obj_cache = qlink_to_cache(qlink);

		if (obj_cache == cache) {
			if (unlikely(from->tail == qlink))
				from->tail = (void **) prev;
			*prev = (void **) *qlink;
			from->bytes -= cache->size;
			qlist_put(to, qlink, cache->size);
		} else
			prev = (void ***) *prev;
	}
}

static void per_cpu_remove_cache(void *arg)
{
	struct kmem_cache *cache = arg;
	struct qlist to_free = QLIST_INIT;
	struct qlist *q;
	unsigned long flags;

	local_irq_save(flags);
	q = this_cpu_ptr(&cpu_quarantine);
	qlist_move_cache(q, &to_free, cache);
	local_irq_restore(flags);

	qlist_free_all(&to_free, cache);
}

void quarantine_remove_cache(struct kmem_cache *cache)
{
	unsigned long flags;
	struct qlist to_free = QLIST_INIT;

	on_each_cpu(per_cpu_remove_cache, cache, 0);

	spin_lock_irqsave(&quarantine_lock, flags);
	qlist_move_cache(&global_quarantine, &to_free, cache);
	spin_unlock_irqrestore(&quarantine_lock, flags);

	qlist_free_all(&to_free, cache);
}
