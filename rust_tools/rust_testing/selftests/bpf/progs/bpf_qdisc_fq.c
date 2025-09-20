// SPDX-License-Identifier: GPL-2.0

/* bpf_fq is intended for testing the bpf qdisc infrastructure and not a direct
 * copy of sch_fq. bpf_fq implements the scheduling algorithm of sch_fq before
 * 29f834aa326e ("net_sched: sch_fq: add 3 bands and WRR scheduling") was
 * introduced. It gives each flow a fair chance to transmit packets in a
 * round-robin fashion. Note that for flow pacing, bpf_fq currently only
 * respects skb->tstamp but not skb->sk->sk_pacing_rate. In addition, if there
 * are multiple bpf_fq instances, they will have a shared view of flows and
 * configuration since some key data structure such as fq_prio_flows,
 * fq_nonprio_flows, and fq_bpf_data are global.
 *
 * To use bpf_fq alone without running selftests, use the following commands.
 *
 * 1. Register bpf_fq to the kernel
 *     bpftool struct_ops register bpf_qdisc_fq.bpf.o /sys/fs/bpf
 * 2. Add bpf_fq to an interface
 *     tc qdisc add dev <interface name> root handle <handle> bpf_fq
 * 3. Delete bpf_fq attached to the interface
 *     tc qdisc delete dev <interface name> root
 * 4. Unregister bpf_fq
 *     bpftool struct_ops unregister name fq
 *
 * The qdisc name, bpf_fq, used in tc commands is defined by Qdisc_ops.id.
 * The struct_ops_map_name, fq, used in the bpftool command is the name of the
 * Qdisc_ops.
 *
 * SEC(".struct_ops")
 * struct Qdisc_ops fq = {
 *         ...
 *         .id        = "bpf_fq",
 * };
 */

#include <vmlinux.h>
#include <errno.h>
#include <bpf/bpf_helpers.h>
#include "bpf_experimental.h"
#include "bpf_qdisc_common.h"

char _license[] SEC("license") = "GPL";

#define NSEC_PER_USEC 1000L
#define NSEC_PER_SEC 1000000000L

#define NUM_QUEUE (1 << 20)

struct fq_bpf_data {
	u32 quantum;
	u32 initial_quantum;
	u32 flow_refill_delay;
	u32 flow_plimit;
	u64 horizon;
	u32 orphan_mask;
	u32 timer_slack;
	u64 time_next_delayed_flow;
	u64 unthrottle_latency_ns;
	u8 horizon_drop;
	u32 new_flow_cnt;
	u32 old_flow_cnt;
	u64 ktime_cache;
};

enum {
	CLS_RET_PRIO	= 0,
	CLS_RET_NONPRIO = 1,
	CLS_RET_ERR	= 2,
};

struct skb_node {
	u64 tstamp;
	struct sk_buff __kptr * skb;
	struct bpf_rb_node node;
};

struct fq_flow_node {
	int credit;
	u32 qlen;
	u64 age;
	u64 time_next_packet;
	struct bpf_list_node list_node;
	struct bpf_rb_node rb_node;
	struct bpf_rb_root queue __contains(skb_node, node);
	struct bpf_spin_lock lock;
	struct bpf_refcount refcount;
};

struct dequeue_nonprio_ctx {
	bool stop_iter;
	u64 expire;
	u64 now;
};

struct remove_flows_ctx {
	bool gc_only;
	u32 reset_cnt;
	u32 reset_max;
};

struct unset_throttled_flows_ctx {
	bool unset_all;
	u64 now;
};

struct fq_stashed_flow {
	struct fq_flow_node __kptr * flow;
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, __u64);
	__type(value, struct fq_stashed_flow);
	__uint(max_entries, NUM_QUEUE);
} fq_nonprio_flows SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, __u64);
	__type(value, struct fq_stashed_flow);
	__uint(max_entries, 1);
} fq_prio_flows SEC(".maps");

private(A) struct bpf_spin_lock fq_delayed_lock;
private(A) struct bpf_rb_root fq_delayed __contains(fq_flow_node, rb_node);

private(B) struct bpf_spin_lock fq_new_flows_lock;
private(B) struct bpf_list_head fq_new_flows __contains(fq_flow_node, list_node);

private(C) struct bpf_spin_lock fq_old_flows_lock;
private(C) struct bpf_list_head fq_old_flows __contains(fq_flow_node, list_node);

private(D) struct fq_bpf_data q;

/* Wrapper for bpf_kptr_xchg that expects NULL dst */
static void bpf_kptr_xchg_back(void *map_val, void *ptr)
{
	void *ret;

	ret = bpf_kptr_xchg(map_val, ptr);
	if (ret)
		bpf_obj_drop(ret);
}

static bool skbn_tstamp_less(struct bpf_rb_node *a, const struct bpf_rb_node *b)
{
	struct skb_node *skbn_a;
	struct skb_node *skbn_b;

	skbn_a = container_of(a, struct skb_node, node);
	skbn_b = container_of(b, struct skb_node, node);

	return skbn_a->tstamp < skbn_b->tstamp;
}

static bool fn_time_next_packet_less(struct bpf_rb_node *a, const struct bpf_rb_node *b)
{
	struct fq_flow_node *flow_a;
	struct fq_flow_node *flow_b;

	flow_a = container_of(a, struct fq_flow_node, rb_node);
	flow_b = container_of(b, struct fq_flow_node, rb_node);

	return flow_a->time_next_packet < flow_b->time_next_packet;
}

static void
fq_flows_add_head(struct bpf_list_head *head, struct bpf_spin_lock *lock,
		  struct fq_flow_node *flow, u32 *flow_cnt)
{
	bpf_spin_lock(lock);
	bpf_list_push_front(head, &flow->list_node);
	bpf_spin_unlock(lock);
	*flow_cnt += 1;
}

static void
fq_flows_add_tail(struct bpf_list_head *head, struct bpf_spin_lock *lock,
		  struct fq_flow_node *flow, u32 *flow_cnt)
{
	bpf_spin_lock(lock);
	bpf_list_push_back(head, &flow->list_node);
	bpf_spin_unlock(lock);
	*flow_cnt += 1;
}

static void
fq_flows_remove_front(struct bpf_list_head *head, struct bpf_spin_lock *lock,
		      struct bpf_list_node **node, u32 *flow_cnt)
{
	bpf_spin_lock(lock);
	*node = bpf_list_pop_front(head);
	bpf_spin_unlock(lock);
	*flow_cnt -= 1;
}

static bool
fq_flows_is_empty(struct bpf_list_head *head, struct bpf_spin_lock *lock)
{
	struct bpf_list_node *node;

	bpf_spin_lock(lock);
	node = bpf_list_pop_front(head);
	if (node) {
		bpf_list_push_front(head, node);
		bpf_spin_unlock(lock);
		return false;
	}
	bpf_spin_unlock(lock);

	return true;
}

/* flow->age is used to denote the state of the flow (not-detached, detached, throttled)
 * as well as the timestamp when the flow is detached.
 *
 * 0: not-detached
 * 1 - (~0ULL-1): detached
 * ~0ULL: throttled
 */
static void fq_flow_set_detached(struct fq_flow_node *flow)
{
	flow->age = bpf_jiffies64();
}

static bool fq_flow_is_detached(struct fq_flow_node *flow)
{
	return flow->age != 0 && flow->age != ~0ULL;
}

static bool sk_listener(struct sock *sk)
{
	return (1 << sk->__sk_common.skc_state) & (TCPF_LISTEN | TCPF_NEW_SYN_RECV);
}

static void fq_gc(void);

static int fq_new_flow(void *flow_map, struct fq_stashed_flow **sflow, u64 hash)
{
	struct fq_stashed_flow tmp = {};
	struct fq_flow_node *flow;
	int ret;

	flow = bpf_obj_new(typeof(*flow));
	if (!flow)
		return -ENOMEM;

	flow->credit = q.initial_quantum,
	flow->qlen = 0,
	flow->age = 1,
	flow->time_next_packet = 0,

	ret = bpf_map_update_elem(flow_map, &hash, &tmp, 0);
	if (ret == -ENOMEM || ret == -E2BIG) {
		fq_gc();
		bpf_map_update_elem(&fq_nonprio_flows, &hash, &tmp, 0);
	}

	*sflow = bpf_map_lookup_elem(flow_map, &hash);
	if (!*sflow) {
		bpf_obj_drop(flow);
		return -ENOMEM;
	}

	bpf_kptr_xchg_back(&(*sflow)->flow, flow);
	return 0;
}

static int
fq_classify(struct sk_buff *skb, struct fq_stashed_flow **sflow)
{
	struct sock *sk = skb->sk;
	int ret = CLS_RET_NONPRIO;
	u64 hash = 0;

	if ((skb->priority & TC_PRIO_MAX) == TC_PRIO_CONTROL) {
		*sflow = bpf_map_lookup_elem(&fq_prio_flows, &hash);
		ret = CLS_RET_PRIO;
	} else {
		if (!sk || sk_listener(sk)) {
			hash = bpf_skb_get_hash(skb) & q.orphan_mask;
			/* Avoid collision with an existing flow hash, which
			 * only uses the lower 32 bits of hash, by setting the
			 * upper half of hash to 1.
			 */
			hash |= (1ULL << 32);
		} else if (sk->__sk_common.skc_state == TCP_CLOSE) {
			hash = bpf_skb_get_hash(skb) & q.orphan_mask;
			hash |= (1ULL << 32);
		} else {
			hash = sk->__sk_common.skc_hash;
		}
		*sflow = bpf_map_lookup_elem(&fq_nonprio_flows, &hash);
	}

	if (!*sflow)
		ret = fq_new_flow(&fq_nonprio_flows, sflow, hash) < 0 ?
		      CLS_RET_ERR : CLS_RET_NONPRIO;

	return ret;
}

static bool fq_packet_beyond_horizon(struct sk_buff *skb)
{
	return (s64)skb->tstamp > (s64)(q.ktime_cache + q.horizon);
}

SEC("struct_ops/bpf_fq_enqueue")
int BPF_PROG(bpf_fq_enqueue, struct sk_buff *skb, struct Qdisc *sch,
	     struct bpf_sk_buff_ptr *to_free)
{
	struct fq_flow_node *flow = NULL, *flow_copy;
	struct fq_stashed_flow *sflow;
	u64 time_to_send, jiffies;
	struct skb_node *skbn;
	int ret;

	if (sch->q.qlen >= sch->limit)
		goto drop;

	if (!skb->tstamp) {
		time_to_send = q.ktime_cache = bpf_ktime_get_ns();
	} else {
		if (fq_packet_beyond_horizon(skb)) {
			q.ktime_cache = bpf_ktime_get_ns();
			if (fq_packet_beyond_horizon(skb)) {
				if (q.horizon_drop)
					goto drop;

				skb->tstamp = q.ktime_cache + q.horizon;
			}
		}
		time_to_send = skb->tstamp;
	}

	ret = fq_classify(skb, &sflow);
	if (ret == CLS_RET_ERR)
		goto drop;

	flow = bpf_kptr_xchg(&sflow->flow, flow);
	if (!flow)
		goto drop;

	if (ret == CLS_RET_NONPRIO) {
		if (flow->qlen >= q.flow_plimit) {
			bpf_kptr_xchg_back(&sflow->flow, flow);
			goto drop;
		}

		if (fq_flow_is_detached(flow)) {
			flow_copy = bpf_refcount_acquire(flow);

			jiffies = bpf_jiffies64();
			if ((s64)(jiffies - (flow_copy->age + q.flow_refill_delay)) > 0) {
				if (flow_copy->credit < q.quantum)
					flow_copy->credit = q.quantum;
			}
			flow_copy->age = 0;
			fq_flows_add_tail(&fq_new_flows, &fq_new_flows_lock, flow_copy,
					  &q.new_flow_cnt);
		}
	}

	skbn = bpf_obj_new(typeof(*skbn));
	if (!skbn) {
		bpf_kptr_xchg_back(&sflow->flow, flow);
		goto drop;
	}

	skbn->tstamp = skb->tstamp = time_to_send;

	sch->qstats.backlog += qdisc_pkt_len(skb);

	skb = bpf_kptr_xchg(&skbn->skb, skb);
	if (skb)
		bpf_qdisc_skb_drop(skb, to_free);

	bpf_spin_lock(&flow->lock);
	bpf_rbtree_add(&flow->queue, &skbn->node, skbn_tstamp_less);
	bpf_spin_unlock(&flow->lock);

	flow->qlen++;
	bpf_kptr_xchg_back(&sflow->flow, flow);

	sch->q.qlen++;
	return NET_XMIT_SUCCESS;

drop:
	bpf_qdisc_skb_drop(skb, to_free);
	sch->qstats.drops++;
	return NET_XMIT_DROP;
}

static int fq_unset_throttled_flows(u32 index, struct unset_throttled_flows_ctx *ctx)
{
	struct bpf_rb_node *node = NULL;
	struct fq_flow_node *flow;

	bpf_spin_lock(&fq_delayed_lock);

	node = bpf_rbtree_first(&fq_delayed);
	if (!node) {
		bpf_spin_unlock(&fq_delayed_lock);
		return 1;
	}

	flow = container_of(node, struct fq_flow_node, rb_node);
	if (!ctx->unset_all && flow->time_next_packet > ctx->now) {
		q.time_next_delayed_flow = flow->time_next_packet;
		bpf_spin_unlock(&fq_delayed_lock);
		return 1;
	}

	node = bpf_rbtree_remove(&fq_delayed, &flow->rb_node);

	bpf_spin_unlock(&fq_delayed_lock);

	if (!node)
		return 1;

	flow = container_of(node, struct fq_flow_node, rb_node);
	flow->age = 0;
	fq_flows_add_tail(&fq_old_flows, &fq_old_flows_lock, flow, &q.old_flow_cnt);

	return 0;
}

static void fq_flow_set_throttled(struct fq_flow_node *flow)
{
	flow->age = ~0ULL;

	if (q.time_next_delayed_flow > flow->time_next_packet)
		q.time_next_delayed_flow = flow->time_next_packet;

	bpf_spin_lock(&fq_delayed_lock);
	bpf_rbtree_add(&fq_delayed, &flow->rb_node, fn_time_next_packet_less);
	bpf_spin_unlock(&fq_delayed_lock);
}

static void fq_check_throttled(u64 now)
{
	struct unset_throttled_flows_ctx ctx = {
		.unset_all = false,
		.now = now,
	};
	unsigned long sample;

	if (q.time_next_delayed_flow > now)
		return;

	sample = (unsigned long)(now - q.time_next_delayed_flow);
	q.unthrottle_latency_ns -= q.unthrottle_latency_ns >> 3;
	q.unthrottle_latency_ns += sample >> 3;

	q.time_next_delayed_flow = ~0ULL;
	bpf_loop(NUM_QUEUE, fq_unset_throttled_flows, &ctx, 0);
}

static struct sk_buff*
fq_dequeue_nonprio_flows(u32 index, struct dequeue_nonprio_ctx *ctx)
{
	u64 time_next_packet, time_to_send;
	struct bpf_rb_node *rb_node;
	struct sk_buff *skb = NULL;
	struct bpf_list_head *head;
	struct bpf_list_node *node;
	struct bpf_spin_lock *lock;
	struct fq_flow_node *flow;
	struct skb_node *skbn;
	bool is_empty;
	u32 *cnt;

	if (q.new_flow_cnt) {
		head = &fq_new_flows;
		lock = &fq_new_flows_lock;
		cnt = &q.new_flow_cnt;
	} else if (q.old_flow_cnt) {
		head = &fq_old_flows;
		lock = &fq_old_flows_lock;
		cnt = &q.old_flow_cnt;
	} else {
		if (q.time_next_delayed_flow != ~0ULL)
			ctx->expire = q.time_next_delayed_flow;
		goto break_loop;
	}

	fq_flows_remove_front(head, lock, &node, cnt);
	if (!node)
		goto break_loop;

	flow = container_of(node, struct fq_flow_node, list_node);
	if (flow->credit <= 0) {
		flow->credit += q.quantum;
		fq_flows_add_tail(&fq_old_flows, &fq_old_flows_lock, flow, &q.old_flow_cnt);
		return NULL;
	}

	bpf_spin_lock(&flow->lock);
	rb_node = bpf_rbtree_first(&flow->queue);
	if (!rb_node) {
		bpf_spin_unlock(&flow->lock);
		is_empty = fq_flows_is_empty(&fq_old_flows, &fq_old_flows_lock);
		if (head == &fq_new_flows && !is_empty) {
			fq_flows_add_tail(&fq_old_flows, &fq_old_flows_lock, flow, &q.old_flow_cnt);
		} else {
			fq_flow_set_detached(flow);
			bpf_obj_drop(flow);
		}
		return NULL;
	}

	skbn = container_of(rb_node, struct skb_node, node);
	time_to_send = skbn->tstamp;

	time_next_packet = (time_to_send > flow->time_next_packet) ?
		time_to_send : flow->time_next_packet;
	if (ctx->now < time_next_packet) {
		bpf_spin_unlock(&flow->lock);
		flow->time_next_packet = time_next_packet;
		fq_flow_set_throttled(flow);
		return NULL;
	}

	rb_node = bpf_rbtree_remove(&flow->queue, rb_node);
	bpf_spin_unlock(&flow->lock);

	if (!rb_node)
		goto add_flow_and_break;

	skbn = container_of(rb_node, struct skb_node, node);
	skb = bpf_kptr_xchg(&skbn->skb, skb);
	bpf_obj_drop(skbn);

	if (!skb)
		goto add_flow_and_break;

	flow->credit -= qdisc_skb_cb(skb)->pkt_len;
	flow->qlen--;

add_flow_and_break:
	fq_flows_add_head(head, lock, flow, cnt);

break_loop:
	ctx->stop_iter = true;
	return skb;
}

static struct sk_buff *fq_dequeue_prio(void)
{
	struct fq_flow_node *flow = NULL;
	struct fq_stashed_flow *sflow;
	struct bpf_rb_node *rb_node;
	struct sk_buff *skb = NULL;
	struct skb_node *skbn;
	u64 hash = 0;

	sflow = bpf_map_lookup_elem(&fq_prio_flows, &hash);
	if (!sflow)
		return NULL;

	flow = bpf_kptr_xchg(&sflow->flow, flow);
	if (!flow)
		return NULL;

	bpf_spin_lock(&flow->lock);
	rb_node = bpf_rbtree_first(&flow->queue);
	if (!rb_node) {
		bpf_spin_unlock(&flow->lock);
		goto out;
	}

	skbn = container_of(rb_node, struct skb_node, node);
	rb_node = bpf_rbtree_remove(&flow->queue, &skbn->node);
	bpf_spin_unlock(&flow->lock);

	if (!rb_node)
		goto out;

	skbn = container_of(rb_node, struct skb_node, node);
	skb = bpf_kptr_xchg(&skbn->skb, skb);
	bpf_obj_drop(skbn);

out:
	bpf_kptr_xchg_back(&sflow->flow, flow);

	return skb;
}

SEC("struct_ops/bpf_fq_dequeue")
struct sk_buff *BPF_PROG(bpf_fq_dequeue, struct Qdisc *sch)
{
	struct dequeue_nonprio_ctx cb_ctx = {};
	struct sk_buff *skb = NULL;
	int i;

	if (!sch->q.qlen)
		goto out;

	skb = fq_dequeue_prio();
	if (skb)
		goto dequeue;

	q.ktime_cache = cb_ctx.now = bpf_ktime_get_ns();
	fq_check_throttled(q.ktime_cache);
	bpf_for(i, 0, sch->limit) {
		skb = fq_dequeue_nonprio_flows(i, &cb_ctx);
		if (cb_ctx.stop_iter)
			break;
	};

	if (skb) {
dequeue:
		sch->q.qlen--;
		sch->qstats.backlog -= qdisc_pkt_len(skb);
		bpf_qdisc_bstats_update(sch, skb);
		return skb;
	}

	if (cb_ctx.expire)
		bpf_qdisc_watchdog_schedule(sch, cb_ctx.expire, q.timer_slack);
out:
	return NULL;
}

static int fq_remove_flows_in_list(u32 index, void *ctx)
{
	struct bpf_list_node *node;
	struct fq_flow_node *flow;

	bpf_spin_lock(&fq_new_flows_lock);
	node = bpf_list_pop_front(&fq_new_flows);
	bpf_spin_unlock(&fq_new_flows_lock);
	if (!node) {
		bpf_spin_lock(&fq_old_flows_lock);
		node = bpf_list_pop_front(&fq_old_flows);
		bpf_spin_unlock(&fq_old_flows_lock);
		if (!node)
			return 1;
	}

	flow = container_of(node, struct fq_flow_node, list_node);
	bpf_obj_drop(flow);

	return 0;
}

extern unsigned CONFIG_HZ __kconfig;

/* limit number of collected flows per round */
#define FQ_GC_MAX 8
#define FQ_GC_AGE (3*CONFIG_HZ)

static bool fq_gc_candidate(struct fq_flow_node *flow)
{
	u64 jiffies = bpf_jiffies64();

	return fq_flow_is_detached(flow) &&
	       ((s64)(jiffies - (flow->age + FQ_GC_AGE)) > 0);
}

static int
fq_remove_flows(struct bpf_map *flow_map, u64 *hash,
		struct fq_stashed_flow *sflow, struct remove_flows_ctx *ctx)
{
	if (sflow->flow &&
	    (!ctx->gc_only || fq_gc_candidate(sflow->flow))) {
		bpf_map_delete_elem(flow_map, hash);
		ctx->reset_cnt++;
	}

	return ctx->reset_cnt < ctx->reset_max ? 0 : 1;
}

static void fq_gc(void)
{
	struct remove_flows_ctx cb_ctx = {
		.gc_only = true,
		.reset_cnt = 0,
		.reset_max = FQ_GC_MAX,
	};

	bpf_for_each_map_elem(&fq_nonprio_flows, fq_remove_flows, &cb_ctx, 0);
}

SEC("struct_ops/bpf_fq_reset")
void BPF_PROG(bpf_fq_reset, struct Qdisc *sch)
{
	struct unset_throttled_flows_ctx utf_ctx = {
		.unset_all = true,
	};
	struct remove_flows_ctx rf_ctx = {
		.gc_only = false,
		.reset_cnt = 0,
		.reset_max = NUM_QUEUE,
	};
	struct fq_stashed_flow *sflow;
	u64 hash = 0;

	sch->q.qlen = 0;
	sch->qstats.backlog = 0;

	bpf_for_each_map_elem(&fq_nonprio_flows, fq_remove_flows, &rf_ctx, 0);

	rf_ctx.reset_cnt = 0;
	bpf_for_each_map_elem(&fq_prio_flows, fq_remove_flows, &rf_ctx, 0);
	fq_new_flow(&fq_prio_flows, &sflow, hash);

	bpf_loop(NUM_QUEUE, fq_remove_flows_in_list, NULL, 0);
	q.new_flow_cnt = 0;
	q.old_flow_cnt = 0;

	bpf_loop(NUM_QUEUE, fq_unset_throttled_flows, &utf_ctx, 0);
}

SEC("struct_ops/bpf_fq_init")
int BPF_PROG(bpf_fq_init, struct Qdisc *sch, struct nlattr *opt,
	     struct netlink_ext_ack *extack)
{
	struct net_device *dev = sch->dev_queue->dev;
	u32 psched_mtu = dev->mtu + dev->hard_header_len;
	struct fq_stashed_flow *sflow;
	u64 hash = 0;

	if (fq_new_flow(&fq_prio_flows, &sflow, hash) < 0)
		return -ENOMEM;

	sch->limit = 10000;
	q.initial_quantum = 10 * psched_mtu;
	q.quantum = 2 * psched_mtu;
	q.flow_refill_delay = 40;
	q.flow_plimit = 100;
	q.horizon = 10ULL * NSEC_PER_SEC;
	q.horizon_drop = 1;
	q.orphan_mask = 1024 - 1;
	q.timer_slack = 10 * NSEC_PER_USEC;
	q.time_next_delayed_flow = ~0ULL;
	q.unthrottle_latency_ns = 0ULL;
	q.new_flow_cnt = 0;
	q.old_flow_cnt = 0;

	return 0;
}

SEC("struct_ops")
void BPF_PROG(bpf_fq_destroy, struct Qdisc *sch)
{
}

SEC(".struct_ops")
struct Qdisc_ops fq = {
	.enqueue   = (void *)bpf_fq_enqueue,
	.dequeue   = (void *)bpf_fq_dequeue,
	.reset     = (void *)bpf_fq_reset,
	.init      = (void *)bpf_fq_init,
	.destroy   = (void *)bpf_fq_destroy,
	.id        = "bpf_fq",
};
