/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_OBJPOOL_H
#define _LINUX_OBJPOOL_H

#include <linux/types.h>
#include <linux/refcount.h>
#include <linux/atomic.h>
#include <linux/cpumask.h>
#include <linux/irqflags.h>
#include <linux/smp.h>

/*
 * objpool: ring-array based lockless MPMC queue
 *
 * Copyright: wuqiang.matt@bytedance.com,mhiramat@kernel.org
 *
 * objpool is a scalable implementation of high performance queue for
 * object allocation and reclamation, such as kretprobe instances.
 *
 * With leveraging percpu ring-array to mitigate hot spots of memory
 * contention, it delivers near-linear scalability for high parallel
 * scenarios. The objpool is best suited for the following cases:
 * 1) Memory allocation or reclamation are prohibited or too expensive
 * 2) Consumers are of different priorities, such as irqs and threads
 *
 * Limitations:
 * 1) Maximum objects (capacity) is fixed after objpool creation
 * 2) All pre-allocated objects are managed in percpu ring array,
 *    which consumes more memory than linked lists
 */

/**
 * struct objpool_slot - percpu ring array of objpool
 * @head: head sequence of the local ring array (to retrieve at)
 * @tail: tail sequence of the local ring array (to append at)
 * @last: the last sequence number marked as ready for retrieve
 * @mask: bits mask for modulo capacity to compute array indexes
 * @entries: object entries on this slot
 *
 * Represents a cpu-local array-based ring buffer, its size is specialized
 * during initialization of object pool. The percpu objpool node is to be
 * allocated from local memory for NUMA system, and to be kept compact in
 * continuous memory: CPU assigned number of objects are stored just after
 * the body of objpool_node.
 *
 * Real size of the ring array is far too smaller than the value range of
 * head and tail, typed as uint32_t: [0, 2^32), so only lower bits (mask)
 * of head and tail are used as the actual position in the ring array. In
 * general the ring array is acting like a small sliding window, which is
 * always moving forward in the loop of [0, 2^32).
 */
struct objpool_slot {
	uint32_t            head;
	uint32_t            tail;
	uint32_t            last;
	uint32_t            mask;
	void               *entries[];
} __packed;

struct objpool_head;

/*
 * caller-specified callback for object initial setup, it's only called
 * once for each object (just after the memory allocation of the object)
 */
typedef int (*objpool_init_obj_cb)(void *obj, void *context);

/* caller-specified cleanup callback for objpool destruction */
typedef int (*objpool_fini_cb)(struct objpool_head *head, void *context);

/**
 * struct objpool_head - object pooling metadata
 * @obj_size:   object size, aligned to sizeof(void *)
 * @nr_objs:    total objs (to be pre-allocated with objpool)
 * @nr_possible_cpus: cached value of num_possible_cpus()
 * @capacity:   max objs can be managed by one objpool_slot
 * @gfp:        gfp flags for kmalloc & vmalloc
 * @ref:        refcount of objpool
 * @flags:      flags for objpool management
 * @cpu_slots:  pointer to the array of objpool_slot
 * @release:    resource cleanup callback
 * @context:    caller-provided context
 */
struct objpool_head {
	int                     obj_size;
	int                     nr_objs;
	int                     nr_possible_cpus;
	int                     capacity;
	gfp_t                   gfp;
	refcount_t              ref;
	unsigned long           flags;
	struct objpool_slot   **cpu_slots;
	objpool_fini_cb         release;
	void                   *context;
};

#define OBJPOOL_NR_OBJECT_MAX	(1UL << 24) /* maximum numbers of total objects */
#define OBJPOOL_OBJECT_SIZE_MAX	(1UL << 16) /* maximum size of an object */

/**
 * objpool_init() - initialize objpool and pre-allocated objects
 * @pool:    the object pool to be initialized, declared by caller
 * @nr_objs: total objects to be pre-allocated by this object pool
 * @object_size: size of an object (should be > 0)
 * @gfp:     flags for memory allocation (via kmalloc or vmalloc)
 * @context: user context for object initialization callback
 * @objinit: object initialization callback for extra setup
 * @release: cleanup callback for extra cleanup task
 *
 * return value: 0 for success, otherwise error code
 *
 * All pre-allocated objects are to be zeroed after memory allocation.
 * Caller could do extra initialization in objinit callback. objinit()
 * will be called just after slot allocation and called only once for
 * each object. After that the objpool won't touch any content of the
 * objects. It's caller's duty to perform reinitialization after each
 * pop (object allocation) or do clearance before each push (object
 * reclamation).
 */
int objpool_init(struct objpool_head *pool, int nr_objs, int object_size,
		 gfp_t gfp, void *context, objpool_init_obj_cb objinit,
		 objpool_fini_cb release);

/* try to retrieve object from slot */
static inline void *__objpool_try_get_slot(struct objpool_head *pool, int cpu)
{
	struct objpool_slot *slot = pool->cpu_slots[cpu];
	/* load head snapshot, other cpus may change it */
	uint32_t head = smp_load_acquire(&slot->head);

	while (head != READ_ONCE(slot->last)) {
		void *obj;

		/*
		 * data visibility of 'last' and 'head' could be out of
		 * order since memory updating of 'last' and 'head' are
		 * performed in push() and pop() independently
		 *
		 * before any retrieving attempts, pop() must guarantee
		 * 'last' is behind 'head', that is to say, there must
		 * be available objects in slot, which could be ensured
		 * by condition 'last != head && last - head <= nr_objs'
		 * that is equivalent to 'last - head - 1 < nr_objs' as
		 * 'last' and 'head' are both unsigned int32
		 */
		if (READ_ONCE(slot->last) - head - 1 >= pool->nr_objs) {
			head = READ_ONCE(slot->head);
			continue;
		}

		/* obj must be retrieved before moving forward head */
		obj = READ_ONCE(slot->entries[head & slot->mask]);

		/* move head forward to mark it's consumption */
		if (try_cmpxchg_release(&slot->head, &head, head + 1))
			return obj;
	}

	return NULL;
}

/**
 * objpool_pop() - allocate an object from objpool
 * @pool: object pool
 *
 * return value: object ptr or NULL if failed
 */
static inline void *objpool_pop(struct objpool_head *pool)
{
	void *obj = NULL;
	unsigned long flags;
	int start, cpu;

	/* disable local irq to avoid preemption & interruption */
	raw_local_irq_save(flags);

	start = raw_smp_processor_id();
	for_each_possible_cpu_wrap(cpu, start) {
		obj = __objpool_try_get_slot(pool, cpu);
		if (obj)
			break;
	}
	raw_local_irq_restore(flags);

	return obj;
}

/* adding object to slot, abort if the slot was already full */
static inline int
__objpool_try_add_slot(void *obj, struct objpool_head *pool, int cpu)
{
	struct objpool_slot *slot = pool->cpu_slots[cpu];
	uint32_t head, tail;

	/* loading tail and head as a local snapshot, tail first */
	tail = READ_ONCE(slot->tail);

	do {
		head = READ_ONCE(slot->head);
		/* fault caught: something must be wrong */
		WARN_ON_ONCE(tail - head > pool->nr_objs);
	} while (!try_cmpxchg_acquire(&slot->tail, &tail, tail + 1));

	/* now the tail position is reserved for the given obj */
	WRITE_ONCE(slot->entries[tail & slot->mask], obj);
	/* update sequence to make this obj available for pop() */
	smp_store_release(&slot->last, tail + 1);

	return 0;
}

/**
 * objpool_push() - reclaim the object and return back to objpool
 * @obj:  object ptr to be pushed to objpool
 * @pool: object pool
 *
 * return: 0 or error code (it fails only when user tries to push
 * the same object multiple times or wrong "objects" into objpool)
 */
static inline int objpool_push(void *obj, struct objpool_head *pool)
{
	unsigned long flags;
	int rc;

	/* disable local irq to avoid preemption & interruption */
	raw_local_irq_save(flags);
	rc = __objpool_try_add_slot(obj, pool, raw_smp_processor_id());
	raw_local_irq_restore(flags);

	return rc;
}


/**
 * objpool_drop() - discard the object and deref objpool
 * @obj:  object ptr to be discarded
 * @pool: object pool
 *
 * return: 0 if objpool was released; -EAGAIN if there are still
 *         outstanding objects
 *
 * objpool_drop is normally for the release of outstanding objects
 * after objpool cleanup (objpool_fini). Thinking of this example:
 * kretprobe is unregistered and objpool_fini() is called to release
 * all remained objects, but there are still objects being used by
 * unfinished kretprobes (like blockable function: sys_accept). So
 * only when the last outstanding object is dropped could the whole
 * objpool be released along with the call of objpool_drop()
 */
int objpool_drop(void *obj, struct objpool_head *pool);

/**
 * objpool_free() - release objpool forcely (all objects to be freed)
 * @pool: object pool to be released
 */
void objpool_free(struct objpool_head *pool);

/**
 * objpool_fini() - deref object pool (also releasing unused objects)
 * @pool: object pool to be dereferenced
 *
 * objpool_fini() will try to release all remained free objects and
 * then drop an extra reference of the objpool. If all objects are
 * already returned to objpool (so called synchronous use cases),
 * the objpool itself will be freed together. But if there are still
 * outstanding objects (so called asynchronous use cases, such like
 * blockable kretprobe), the objpool won't be released until all
 * the outstanding objects are dropped, but the caller must assure
 * there are no concurrent objpool_push() on the fly. Normally RCU
 * is being required to make sure all ongoing objpool_push() must
 * be finished before calling objpool_fini(), so does test_objpool,
 * kretprobe or rethook
 */
void objpool_fini(struct objpool_head *pool);

#endif /* _LINUX_OBJPOOL_H */
