/* bpf/cpumap.c
 *
 * Copyright (c) 2017 Jesper Dangaard Brouer, Red Hat Inc.
 * Released under terms in GPL version 2.  See COPYING.
 */

/* The 'cpumap' is primarily used as a backend map for XDP BPF helper
 * call bpf_redirect_map() and XDP_REDIRECT action, like 'devmap'.
 *
 * Unlike devmap which redirects XDP frames out another NIC device,
 * this map type redirects raw XDP frames to another CPU.  The remote
 * CPU will do SKB-allocation and call the normal network stack.
 *
 * This is a scalability and isolation mechanism, that allow
 * separating the early driver network XDP layer, from the rest of the
 * netstack, and assigning dedicated CPUs for this stage.  This
 * basically allows for 10G wirespeed pre-filtering via bpf.
 */
#include <linux/bpf.h>
#include <linux/filter.h>
#include <linux/ptr_ring.h>

#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/capability.h>
#include <trace/events/xdp.h>

#include <linux/netdevice.h>   /* netif_receive_skb_core */
#include <linux/etherdevice.h> /* eth_type_trans */

/* General idea: XDP packets getting XDP redirected to another CPU,
 * will maximum be stored/queued for one driver ->poll() call.  It is
 * guaranteed that setting flush bit and flush operation happen on
 * same CPU.  Thus, cpu_map_flush operation can deduct via this_cpu_ptr()
 * which queue in bpf_cpu_map_entry contains packets.
 */

#define CPU_MAP_BULK_SIZE 8  /* 8 == one cacheline on 64-bit archs */
struct xdp_bulk_queue {
	void *q[CPU_MAP_BULK_SIZE];
	unsigned int count;
};

/* Struct for every remote "destination" CPU in map */
struct bpf_cpu_map_entry {
	u32 cpu;    /* kthread CPU and map index */
	int map_id; /* Back reference to map */
	u32 qsize;  /* Queue size placeholder for map lookup */

	/* XDP can run multiple RX-ring queues, need __percpu enqueue store */
	struct xdp_bulk_queue __percpu *bulkq;

	/* Queue with potential multi-producers, and single-consumer kthread */
	struct ptr_ring *queue;
	struct task_struct *kthread;
	struct work_struct kthread_stop_wq;

	atomic_t refcnt; /* Control when this struct can be free'ed */
	struct rcu_head rcu;
};

struct bpf_cpu_map {
	struct bpf_map map;
	/* Below members specific for map type */
	struct bpf_cpu_map_entry **cpu_map;
	unsigned long __percpu *flush_needed;
};

static int bq_flush_to_queue(struct bpf_cpu_map_entry *rcpu,
			     struct xdp_bulk_queue *bq);

static u64 cpu_map_bitmap_size(const union bpf_attr *attr)
{
	return BITS_TO_LONGS(attr->max_entries) * sizeof(unsigned long);
}

static struct bpf_map *cpu_map_alloc(union bpf_attr *attr)
{
	struct bpf_cpu_map *cmap;
	int err = -ENOMEM;
	u64 cost;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return ERR_PTR(-EPERM);

	/* check sanity of attributes */
	if (attr->max_entries == 0 || attr->key_size != 4 ||
	    attr->value_size != 4 || attr->map_flags & ~BPF_F_NUMA_NODE)
		return ERR_PTR(-EINVAL);

	cmap = kzalloc(sizeof(*cmap), GFP_USER);
	if (!cmap)
		return ERR_PTR(-ENOMEM);

	bpf_map_init_from_attr(&cmap->map, attr);

	/* Pre-limit array size based on NR_CPUS, not final CPU check */
	if (cmap->map.max_entries > NR_CPUS) {
		err = -E2BIG;
		goto free_cmap;
	}

	/* make sure page count doesn't overflow */
	cost = (u64) cmap->map.max_entries * sizeof(struct bpf_cpu_map_entry *);
	cost += cpu_map_bitmap_size(attr) * num_possible_cpus();
	if (cost >= U32_MAX - PAGE_SIZE)
		goto free_cmap;
	cmap->map.pages = round_up(cost, PAGE_SIZE) >> PAGE_SHIFT;

	/* Notice returns -EPERM on if map size is larger than memlock limit */
	ret = bpf_map_precharge_memlock(cmap->map.pages);
	if (ret) {
		err = ret;
		goto free_cmap;
	}

	/* A per cpu bitfield with a bit per possible CPU in map  */
	cmap->flush_needed = __alloc_percpu(cpu_map_bitmap_size(attr),
					    __alignof__(unsigned long));
	if (!cmap->flush_needed)
		goto free_cmap;

	/* Alloc array for possible remote "destination" CPUs */
	cmap->cpu_map = bpf_map_area_alloc(cmap->map.max_entries *
					   sizeof(struct bpf_cpu_map_entry *),
					   cmap->map.numa_node);
	if (!cmap->cpu_map)
		goto free_percpu;

	return &cmap->map;
free_percpu:
	free_percpu(cmap->flush_needed);
free_cmap:
	kfree(cmap);
	return ERR_PTR(err);
}

static void __cpu_map_queue_destructor(void *ptr)
{
	/* The tear-down procedure should have made sure that queue is
	 * empty.  See __cpu_map_entry_replace() and work-queue
	 * invoked cpu_map_kthread_stop(). Catch any broken behaviour
	 * gracefully and warn once.
	 */
	if (WARN_ON_ONCE(ptr))
		page_frag_free(ptr);
}

static void put_cpu_map_entry(struct bpf_cpu_map_entry *rcpu)
{
	if (atomic_dec_and_test(&rcpu->refcnt)) {
		/* The queue should be empty at this point */
		ptr_ring_cleanup(rcpu->queue, __cpu_map_queue_destructor);
		kfree(rcpu->queue);
		kfree(rcpu);
	}
}

static void get_cpu_map_entry(struct bpf_cpu_map_entry *rcpu)
{
	atomic_inc(&rcpu->refcnt);
}

/* called from workqueue, to workaround syscall using preempt_disable */
static void cpu_map_kthread_stop(struct work_struct *work)
{
	struct bpf_cpu_map_entry *rcpu;

	rcpu = container_of(work, struct bpf_cpu_map_entry, kthread_stop_wq);

	/* Wait for flush in __cpu_map_entry_free(), via full RCU barrier,
	 * as it waits until all in-flight call_rcu() callbacks complete.
	 */
	rcu_barrier();

	/* kthread_stop will wake_up_process and wait for it to complete */
	kthread_stop(rcpu->kthread);
}

/* For now, xdp_pkt is a cpumap internal data structure, with info
 * carried between enqueue to dequeue. It is mapped into the top
 * headroom of the packet, to avoid allocating separate mem.
 */
struct xdp_pkt {
	void *data;
	u16 len;
	u16 headroom;
	u16 metasize;
	struct net_device *dev_rx;
};

/* Convert xdp_buff to xdp_pkt */
static struct xdp_pkt *convert_to_xdp_pkt(struct xdp_buff *xdp)
{
	struct xdp_pkt *xdp_pkt;
	int metasize;
	int headroom;

	/* Assure headroom is available for storing info */
	headroom = xdp->data - xdp->data_hard_start;
	metasize = xdp->data - xdp->data_meta;
	metasize = metasize > 0 ? metasize : 0;
	if (unlikely((headroom - metasize) < sizeof(*xdp_pkt)))
		return NULL;

	/* Store info in top of packet */
	xdp_pkt = xdp->data_hard_start;

	xdp_pkt->data = xdp->data;
	xdp_pkt->len  = xdp->data_end - xdp->data;
	xdp_pkt->headroom = headroom - sizeof(*xdp_pkt);
	xdp_pkt->metasize = metasize;

	return xdp_pkt;
}

static struct sk_buff *cpu_map_build_skb(struct bpf_cpu_map_entry *rcpu,
					 struct xdp_pkt *xdp_pkt)
{
	unsigned int frame_size;
	void *pkt_data_start;
	struct sk_buff *skb;

	/* build_skb need to place skb_shared_info after SKB end, and
	 * also want to know the memory "truesize".  Thus, need to
	 * know the memory frame size backing xdp_buff.
	 *
	 * XDP was designed to have PAGE_SIZE frames, but this
	 * assumption is not longer true with ixgbe and i40e.  It
	 * would be preferred to set frame_size to 2048 or 4096
	 * depending on the driver.
	 *   frame_size = 2048;
	 *   frame_len  = frame_size - sizeof(*xdp_pkt);
	 *
	 * Instead, with info avail, skb_shared_info in placed after
	 * packet len.  This, unfortunately fakes the truesize.
	 * Another disadvantage of this approach, the skb_shared_info
	 * is not at a fixed memory location, with mixed length
	 * packets, which is bad for cache-line hotness.
	 */
	frame_size = SKB_DATA_ALIGN(xdp_pkt->len) + xdp_pkt->headroom +
		SKB_DATA_ALIGN(sizeof(struct skb_shared_info));

	pkt_data_start = xdp_pkt->data - xdp_pkt->headroom;
	skb = build_skb(pkt_data_start, frame_size);
	if (!skb)
		return NULL;

	skb_reserve(skb, xdp_pkt->headroom);
	__skb_put(skb, xdp_pkt->len);
	if (xdp_pkt->metasize)
		skb_metadata_set(skb, xdp_pkt->metasize);

	/* Essential SKB info: protocol and skb->dev */
	skb->protocol = eth_type_trans(skb, xdp_pkt->dev_rx);

	/* Optional SKB info, currently missing:
	 * - HW checksum info		(skb->ip_summed)
	 * - HW RX hash			(skb_set_hash)
	 * - RX ring dev queue index	(skb_record_rx_queue)
	 */

	return skb;
}

static int cpu_map_kthread_run(void *data)
{
	struct bpf_cpu_map_entry *rcpu = data;

	set_current_state(TASK_INTERRUPTIBLE);

	/* When kthread gives stop order, then rcpu have been disconnected
	 * from map, thus no new packets can enter. Remaining in-flight
	 * per CPU stored packets are flushed to this queue.  Wait honoring
	 * kthread_stop signal until queue is empty.
	 */
	while (!kthread_should_stop() || !__ptr_ring_empty(rcpu->queue)) {
		unsigned int processed = 0, drops = 0, sched = 0;
		struct xdp_pkt *xdp_pkt;

		/* Release CPU reschedule checks */
		if (__ptr_ring_empty(rcpu->queue)) {
			set_current_state(TASK_INTERRUPTIBLE);
			/* Recheck to avoid lost wake-up */
			if (__ptr_ring_empty(rcpu->queue)) {
				schedule();
				sched = 1;
			} else {
				__set_current_state(TASK_RUNNING);
			}
		} else {
			sched = cond_resched();
		}

		/* Process packets in rcpu->queue */
		local_bh_disable();
		/*
		 * The bpf_cpu_map_entry is single consumer, with this
		 * kthread CPU pinned. Lockless access to ptr_ring
		 * consume side valid as no-resize allowed of queue.
		 */
		while ((xdp_pkt = __ptr_ring_consume(rcpu->queue))) {
			struct sk_buff *skb;
			int ret;

			skb = cpu_map_build_skb(rcpu, xdp_pkt);
			if (!skb) {
				page_frag_free(xdp_pkt);
				continue;
			}

			/* Inject into network stack */
			ret = netif_receive_skb_core(skb);
			if (ret == NET_RX_DROP)
				drops++;

			/* Limit BH-disable period */
			if (++processed == 8)
				break;
		}
		/* Feedback loop via tracepoint */
		trace_xdp_cpumap_kthread(rcpu->map_id, processed, drops, sched);

		local_bh_enable(); /* resched point, may call do_softirq() */
	}
	__set_current_state(TASK_RUNNING);

	put_cpu_map_entry(rcpu);
	return 0;
}

static struct bpf_cpu_map_entry *__cpu_map_entry_alloc(u32 qsize, u32 cpu,
						       int map_id)
{
	gfp_t gfp = GFP_ATOMIC|__GFP_NOWARN;
	struct bpf_cpu_map_entry *rcpu;
	int numa, err;

	/* Have map->numa_node, but choose node of redirect target CPU */
	numa = cpu_to_node(cpu);

	rcpu = kzalloc_node(sizeof(*rcpu), gfp, numa);
	if (!rcpu)
		return NULL;

	/* Alloc percpu bulkq */
	rcpu->bulkq = __alloc_percpu_gfp(sizeof(*rcpu->bulkq),
					 sizeof(void *), gfp);
	if (!rcpu->bulkq)
		goto free_rcu;

	/* Alloc queue */
	rcpu->queue = kzalloc_node(sizeof(*rcpu->queue), gfp, numa);
	if (!rcpu->queue)
		goto free_bulkq;

	err = ptr_ring_init(rcpu->queue, qsize, gfp);
	if (err)
		goto free_queue;

	rcpu->cpu    = cpu;
	rcpu->map_id = map_id;
	rcpu->qsize  = qsize;

	/* Setup kthread */
	rcpu->kthread = kthread_create_on_node(cpu_map_kthread_run, rcpu, numa,
					       "cpumap/%d/map:%d", cpu, map_id);
	if (IS_ERR(rcpu->kthread))
		goto free_ptr_ring;

	get_cpu_map_entry(rcpu); /* 1-refcnt for being in cmap->cpu_map[] */
	get_cpu_map_entry(rcpu); /* 1-refcnt for kthread */

	/* Make sure kthread runs on a single CPU */
	kthread_bind(rcpu->kthread, cpu);
	wake_up_process(rcpu->kthread);

	return rcpu;

free_ptr_ring:
	ptr_ring_cleanup(rcpu->queue, NULL);
free_queue:
	kfree(rcpu->queue);
free_bulkq:
	free_percpu(rcpu->bulkq);
free_rcu:
	kfree(rcpu);
	return NULL;
}

static void __cpu_map_entry_free(struct rcu_head *rcu)
{
	struct bpf_cpu_map_entry *rcpu;
	int cpu;

	/* This cpu_map_entry have been disconnected from map and one
	 * RCU graze-period have elapsed.  Thus, XDP cannot queue any
	 * new packets and cannot change/set flush_needed that can
	 * find this entry.
	 */
	rcpu = container_of(rcu, struct bpf_cpu_map_entry, rcu);

	/* Flush remaining packets in percpu bulkq */
	for_each_online_cpu(cpu) {
		struct xdp_bulk_queue *bq = per_cpu_ptr(rcpu->bulkq, cpu);

		/* No concurrent bq_enqueue can run at this point */
		bq_flush_to_queue(rcpu, bq);
	}
	free_percpu(rcpu->bulkq);
	/* Cannot kthread_stop() here, last put free rcpu resources */
	put_cpu_map_entry(rcpu);
}

/* After xchg pointer to bpf_cpu_map_entry, use the call_rcu() to
 * ensure any driver rcu critical sections have completed, but this
 * does not guarantee a flush has happened yet. Because driver side
 * rcu_read_lock/unlock only protects the running XDP program.  The
 * atomic xchg and NULL-ptr check in __cpu_map_flush() makes sure a
 * pending flush op doesn't fail.
 *
 * The bpf_cpu_map_entry is still used by the kthread, and there can
 * still be pending packets (in queue and percpu bulkq).  A refcnt
 * makes sure to last user (kthread_stop vs. call_rcu) free memory
 * resources.
 *
 * The rcu callback __cpu_map_entry_free flush remaining packets in
 * percpu bulkq to queue.  Due to caller map_delete_elem() disable
 * preemption, cannot call kthread_stop() to make sure queue is empty.
 * Instead a work_queue is started for stopping kthread,
 * cpu_map_kthread_stop, which waits for an RCU graze period before
 * stopping kthread, emptying the queue.
 */
static void __cpu_map_entry_replace(struct bpf_cpu_map *cmap,
				    u32 key_cpu, struct bpf_cpu_map_entry *rcpu)
{
	struct bpf_cpu_map_entry *old_rcpu;

	old_rcpu = xchg(&cmap->cpu_map[key_cpu], rcpu);
	if (old_rcpu) {
		call_rcu(&old_rcpu->rcu, __cpu_map_entry_free);
		INIT_WORK(&old_rcpu->kthread_stop_wq, cpu_map_kthread_stop);
		schedule_work(&old_rcpu->kthread_stop_wq);
	}
}

static int cpu_map_delete_elem(struct bpf_map *map, void *key)
{
	struct bpf_cpu_map *cmap = container_of(map, struct bpf_cpu_map, map);
	u32 key_cpu = *(u32 *)key;

	if (key_cpu >= map->max_entries)
		return -EINVAL;

	/* notice caller map_delete_elem() use preempt_disable() */
	__cpu_map_entry_replace(cmap, key_cpu, NULL);
	return 0;
}

static int cpu_map_update_elem(struct bpf_map *map, void *key, void *value,
			       u64 map_flags)
{
	struct bpf_cpu_map *cmap = container_of(map, struct bpf_cpu_map, map);
	struct bpf_cpu_map_entry *rcpu;

	/* Array index key correspond to CPU number */
	u32 key_cpu = *(u32 *)key;
	/* Value is the queue size */
	u32 qsize = *(u32 *)value;

	if (unlikely(map_flags > BPF_EXIST))
		return -EINVAL;
	if (unlikely(key_cpu >= cmap->map.max_entries))
		return -E2BIG;
	if (unlikely(map_flags == BPF_NOEXIST))
		return -EEXIST;
	if (unlikely(qsize > 16384)) /* sanity limit on qsize */
		return -EOVERFLOW;

	/* Make sure CPU is a valid possible cpu */
	if (!cpu_possible(key_cpu))
		return -ENODEV;

	if (qsize == 0) {
		rcpu = NULL; /* Same as deleting */
	} else {
		/* Updating qsize cause re-allocation of bpf_cpu_map_entry */
		rcpu = __cpu_map_entry_alloc(qsize, key_cpu, map->id);
		if (!rcpu)
			return -ENOMEM;
	}
	rcu_read_lock();
	__cpu_map_entry_replace(cmap, key_cpu, rcpu);
	rcu_read_unlock();
	return 0;
}

static void cpu_map_free(struct bpf_map *map)
{
	struct bpf_cpu_map *cmap = container_of(map, struct bpf_cpu_map, map);
	int cpu;
	u32 i;

	/* At this point bpf_prog->aux->refcnt == 0 and this map->refcnt == 0,
	 * so the bpf programs (can be more than one that used this map) were
	 * disconnected from events. Wait for outstanding critical sections in
	 * these programs to complete. The rcu critical section only guarantees
	 * no further "XDP/bpf-side" reads against bpf_cpu_map->cpu_map.
	 * It does __not__ ensure pending flush operations (if any) are
	 * complete.
	 */
	synchronize_rcu();

	/* To ensure all pending flush operations have completed wait for flush
	 * bitmap to indicate all flush_needed bits to be zero on _all_ cpus.
	 * Because the above synchronize_rcu() ensures the map is disconnected
	 * from the program we can assume no new bits will be set.
	 */
	for_each_online_cpu(cpu) {
		unsigned long *bitmap = per_cpu_ptr(cmap->flush_needed, cpu);

		while (!bitmap_empty(bitmap, cmap->map.max_entries))
			cond_resched();
	}

	/* For cpu_map the remote CPUs can still be using the entries
	 * (struct bpf_cpu_map_entry).
	 */
	for (i = 0; i < cmap->map.max_entries; i++) {
		struct bpf_cpu_map_entry *rcpu;

		rcpu = READ_ONCE(cmap->cpu_map[i]);
		if (!rcpu)
			continue;

		/* bq flush and cleanup happens after RCU graze-period */
		__cpu_map_entry_replace(cmap, i, NULL); /* call_rcu */
	}
	free_percpu(cmap->flush_needed);
	bpf_map_area_free(cmap->cpu_map);
	kfree(cmap);
}

struct bpf_cpu_map_entry *__cpu_map_lookup_elem(struct bpf_map *map, u32 key)
{
	struct bpf_cpu_map *cmap = container_of(map, struct bpf_cpu_map, map);
	struct bpf_cpu_map_entry *rcpu;

	if (key >= map->max_entries)
		return NULL;

	rcpu = READ_ONCE(cmap->cpu_map[key]);
	return rcpu;
}

static void *cpu_map_lookup_elem(struct bpf_map *map, void *key)
{
	struct bpf_cpu_map_entry *rcpu =
		__cpu_map_lookup_elem(map, *(u32 *)key);

	return rcpu ? &rcpu->qsize : NULL;
}

static int cpu_map_get_next_key(struct bpf_map *map, void *key, void *next_key)
{
	struct bpf_cpu_map *cmap = container_of(map, struct bpf_cpu_map, map);
	u32 index = key ? *(u32 *)key : U32_MAX;
	u32 *next = next_key;

	if (index >= cmap->map.max_entries) {
		*next = 0;
		return 0;
	}

	if (index == cmap->map.max_entries - 1)
		return -ENOENT;
	*next = index + 1;
	return 0;
}

const struct bpf_map_ops cpu_map_ops = {
	.map_alloc		= cpu_map_alloc,
	.map_free		= cpu_map_free,
	.map_delete_elem	= cpu_map_delete_elem,
	.map_update_elem	= cpu_map_update_elem,
	.map_lookup_elem	= cpu_map_lookup_elem,
	.map_get_next_key	= cpu_map_get_next_key,
};

static int bq_flush_to_queue(struct bpf_cpu_map_entry *rcpu,
			     struct xdp_bulk_queue *bq)
{
	unsigned int processed = 0, drops = 0;
	const int to_cpu = rcpu->cpu;
	struct ptr_ring *q;
	int i;

	if (unlikely(!bq->count))
		return 0;

	q = rcpu->queue;
	spin_lock(&q->producer_lock);

	for (i = 0; i < bq->count; i++) {
		void *xdp_pkt = bq->q[i];
		int err;

		err = __ptr_ring_produce(q, xdp_pkt);
		if (err) {
			drops++;
			page_frag_free(xdp_pkt); /* Free xdp_pkt */
		}
		processed++;
	}
	bq->count = 0;
	spin_unlock(&q->producer_lock);

	/* Feedback loop via tracepoints */
	trace_xdp_cpumap_enqueue(rcpu->map_id, processed, drops, to_cpu);
	return 0;
}

/* Runs under RCU-read-side, plus in softirq under NAPI protection.
 * Thus, safe percpu variable access.
 */
static int bq_enqueue(struct bpf_cpu_map_entry *rcpu, struct xdp_pkt *xdp_pkt)
{
	struct xdp_bulk_queue *bq = this_cpu_ptr(rcpu->bulkq);

	if (unlikely(bq->count == CPU_MAP_BULK_SIZE))
		bq_flush_to_queue(rcpu, bq);

	/* Notice, xdp_buff/page MUST be queued here, long enough for
	 * driver to code invoking us to finished, due to driver
	 * (e.g. ixgbe) recycle tricks based on page-refcnt.
	 *
	 * Thus, incoming xdp_pkt is always queued here (else we race
	 * with another CPU on page-refcnt and remaining driver code).
	 * Queue time is very short, as driver will invoke flush
	 * operation, when completing napi->poll call.
	 */
	bq->q[bq->count++] = xdp_pkt;
	return 0;
}

int cpu_map_enqueue(struct bpf_cpu_map_entry *rcpu, struct xdp_buff *xdp,
		    struct net_device *dev_rx)
{
	struct xdp_pkt *xdp_pkt;

	xdp_pkt = convert_to_xdp_pkt(xdp);
	if (unlikely(!xdp_pkt))
		return -EOVERFLOW;

	/* Info needed when constructing SKB on remote CPU */
	xdp_pkt->dev_rx = dev_rx;

	bq_enqueue(rcpu, xdp_pkt);
	return 0;
}

void __cpu_map_insert_ctx(struct bpf_map *map, u32 bit)
{
	struct bpf_cpu_map *cmap = container_of(map, struct bpf_cpu_map, map);
	unsigned long *bitmap = this_cpu_ptr(cmap->flush_needed);

	__set_bit(bit, bitmap);
}

void __cpu_map_flush(struct bpf_map *map)
{
	struct bpf_cpu_map *cmap = container_of(map, struct bpf_cpu_map, map);
	unsigned long *bitmap = this_cpu_ptr(cmap->flush_needed);
	u32 bit;

	/* The napi->poll softirq makes sure __cpu_map_insert_ctx()
	 * and __cpu_map_flush() happen on same CPU. Thus, the percpu
	 * bitmap indicate which percpu bulkq have packets.
	 */
	for_each_set_bit(bit, bitmap, map->max_entries) {
		struct bpf_cpu_map_entry *rcpu = READ_ONCE(cmap->cpu_map[bit]);
		struct xdp_bulk_queue *bq;

		/* This is possible if entry is removed by user space
		 * between xdp redirect and flush op.
		 */
		if (unlikely(!rcpu))
			continue;

		__clear_bit(bit, bitmap);

		/* Flush all frames in bulkq to real queue */
		bq = this_cpu_ptr(rcpu->bulkq);
		bq_flush_to_queue(rcpu, bq);

		/* If already running, costs spin_lock_irqsave + smb_mb */
		wake_up_process(rcpu->kthread);
	}
}
