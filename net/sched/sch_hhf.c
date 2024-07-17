// SPDX-License-Identifier: GPL-2.0-only
/* net/sched/sch_hhf.c		Heavy-Hitter Filter (HHF)
 *
 * Copyright (C) 2013 Terry Lam <vtlam@google.com>
 * Copyright (C) 2013 Nandita Dukkipati <nanditad@google.com>
 */

#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/vmalloc.h>
#include <linux/siphash.h>
#include <net/pkt_sched.h>
#include <net/sock.h>

/*	Heavy-Hitter Filter (HHF)
 *
 * Principles :
 * Flows are classified into two buckets: non-heavy-hitter and heavy-hitter
 * buckets. Initially, a new flow starts as non-heavy-hitter. Once classified
 * as heavy-hitter, it is immediately switched to the heavy-hitter bucket.
 * The buckets are dequeued by a Weighted Deficit Round Robin (WDRR) scheduler,
 * in which the heavy-hitter bucket is served with less weight.
 * In other words, non-heavy-hitters (e.g., short bursts of critical traffic)
 * are isolated from heavy-hitters (e.g., persistent bulk traffic) and also have
 * higher share of bandwidth.
 *
 * To capture heavy-hitters, we use the "multi-stage filter" algorithm in the
 * following paper:
 * [EV02] C. Estan and G. Varghese, "New Directions in Traffic Measurement and
 * Accounting", in ACM SIGCOMM, 2002.
 *
 * Conceptually, a multi-stage filter comprises k independent hash functions
 * and k counter arrays. Packets are indexed into k counter arrays by k hash
 * functions, respectively. The counters are then increased by the packet sizes.
 * Therefore,
 *    - For a heavy-hitter flow: *all* of its k array counters must be large.
 *    - For a non-heavy-hitter flow: some of its k array counters can be large
 *      due to hash collision with other small flows; however, with high
 *      probability, not *all* k counters are large.
 *
 * By the design of the multi-stage filter algorithm, the false negative rate
 * (heavy-hitters getting away uncaptured) is zero. However, the algorithm is
 * susceptible to false positives (non-heavy-hitters mistakenly classified as
 * heavy-hitters).
 * Therefore, we also implement the following optimizations to reduce false
 * positives by avoiding unnecessary increment of the counter values:
 *    - Optimization O1: once a heavy-hitter is identified, its bytes are not
 *        accounted in the array counters. This technique is called "shielding"
 *        in Section 3.3.1 of [EV02].
 *    - Optimization O2: conservative update of counters
 *                       (Section 3.3.2 of [EV02]),
 *        New counter value = max {old counter value,
 *                                 smallest counter value + packet bytes}
 *
 * Finally, we refresh the counters periodically since otherwise the counter
 * values will keep accumulating.
 *
 * Once a flow is classified as heavy-hitter, we also save its per-flow state
 * in an exact-matching flow table so that its subsequent packets can be
 * dispatched to the heavy-hitter bucket accordingly.
 *
 *
 * At a high level, this qdisc works as follows:
 * Given a packet p:
 *   - If the flow-id of p (e.g., TCP 5-tuple) is already in the exact-matching
 *     heavy-hitter flow table, denoted table T, then send p to the heavy-hitter
 *     bucket.
 *   - Otherwise, forward p to the multi-stage filter, denoted filter F
 *        + If F decides that p belongs to a non-heavy-hitter flow, then send p
 *          to the non-heavy-hitter bucket.
 *        + Otherwise, if F decides that p belongs to a new heavy-hitter flow,
 *          then set up a new flow entry for the flow-id of p in the table T and
 *          send p to the heavy-hitter bucket.
 *
 * In this implementation:
 *   - T is a fixed-size hash-table with 1024 entries. Hash collision is
 *     resolved by linked-list chaining.
 *   - F has four counter arrays, each array containing 1024 32-bit counters.
 *     That means 4 * 1024 * 32 bits = 16KB of memory.
 *   - Since each array in F contains 1024 counters, 10 bits are sufficient to
 *     index into each array.
 *     Hence, instead of having four hash functions, we chop the 32-bit
 *     skb-hash into three 10-bit chunks, and the remaining 10-bit chunk is
 *     computed as XOR sum of those three chunks.
 *   - We need to clear the counter arrays periodically; however, directly
 *     memsetting 16KB of memory can lead to cache eviction and unwanted delay.
 *     So by representing each counter by a valid bit, we only need to reset
 *     4K of 1 bit (i.e. 512 bytes) instead of 16KB of memory.
 *   - The Deficit Round Robin engine is taken from fq_codel implementation
 *     (net/sched/sch_fq_codel.c). Note that wdrr_bucket corresponds to
 *     fq_codel_flow in fq_codel implementation.
 *
 */

/* Non-configurable parameters */
#define HH_FLOWS_CNT	 1024  /* number of entries in exact-matching table T */
#define HHF_ARRAYS_CNT	 4     /* number of arrays in multi-stage filter F */
#define HHF_ARRAYS_LEN	 1024  /* number of counters in each array of F */
#define HHF_BIT_MASK_LEN 10    /* masking 10 bits */
#define HHF_BIT_MASK	 0x3FF /* bitmask of 10 bits */

#define WDRR_BUCKET_CNT  2     /* two buckets for Weighted DRR */
enum wdrr_bucket_idx {
	WDRR_BUCKET_FOR_HH	= 0, /* bucket id for heavy-hitters */
	WDRR_BUCKET_FOR_NON_HH	= 1  /* bucket id for non-heavy-hitters */
};

#define hhf_time_before(a, b)	\
	(typecheck(u32, a) && typecheck(u32, b) && ((s32)((a) - (b)) < 0))

/* Heavy-hitter per-flow state */
struct hh_flow_state {
	u32		 hash_id;	/* hash of flow-id (e.g. TCP 5-tuple) */
	u32		 hit_timestamp;	/* last time heavy-hitter was seen */
	struct list_head flowchain;	/* chaining under hash collision */
};

/* Weighted Deficit Round Robin (WDRR) scheduler */
struct wdrr_bucket {
	struct sk_buff	  *head;
	struct sk_buff	  *tail;
	struct list_head  bucketchain;
	int		  deficit;
};

struct hhf_sched_data {
	struct wdrr_bucket buckets[WDRR_BUCKET_CNT];
	siphash_key_t	   perturbation;   /* hash perturbation */
	u32		   quantum;        /* psched_mtu(qdisc_dev(sch)); */
	u32		   drop_overlimit; /* number of times max qdisc packet
					    * limit was hit
					    */
	struct list_head   *hh_flows;       /* table T (currently active HHs) */
	u32		   hh_flows_limit;            /* max active HH allocs */
	u32		   hh_flows_overlimit; /* num of disallowed HH allocs */
	u32		   hh_flows_total_cnt;          /* total admitted HHs */
	u32		   hh_flows_current_cnt;        /* total current HHs  */
	u32		   *hhf_arrays[HHF_ARRAYS_CNT]; /* HH filter F */
	u32		   hhf_arrays_reset_timestamp;  /* last time hhf_arrays
							 * was reset
							 */
	unsigned long	   *hhf_valid_bits[HHF_ARRAYS_CNT]; /* shadow valid bits
							     * of hhf_arrays
							     */
	/* Similar to the "new_flows" vs. "old_flows" concept in fq_codel DRR */
	struct list_head   new_buckets; /* list of new buckets */
	struct list_head   old_buckets; /* list of old buckets */

	/* Configurable HHF parameters */
	u32		   hhf_reset_timeout; /* interval to reset counter
					       * arrays in filter F
					       * (default 40ms)
					       */
	u32		   hhf_admit_bytes;   /* counter thresh to classify as
					       * HH (default 128KB).
					       * With these default values,
					       * 128KB / 40ms = 25 Mbps
					       * i.e., we expect to capture HHs
					       * sending > 25 Mbps.
					       */
	u32		   hhf_evict_timeout; /* aging threshold to evict idle
					       * HHs out of table T. This should
					       * be large enough to avoid
					       * reordering during HH eviction.
					       * (default 1s)
					       */
	u32		   hhf_non_hh_weight; /* WDRR weight for non-HHs
					       * (default 2,
					       *  i.e., non-HH : HH = 2 : 1)
					       */
};

static u32 hhf_time_stamp(void)
{
	return jiffies;
}

/* Looks up a heavy-hitter flow in a chaining list of table T. */
static struct hh_flow_state *seek_list(const u32 hash,
				       struct list_head *head,
				       struct hhf_sched_data *q)
{
	struct hh_flow_state *flow, *next;
	u32 now = hhf_time_stamp();

	if (list_empty(head))
		return NULL;

	list_for_each_entry_safe(flow, next, head, flowchain) {
		u32 prev = flow->hit_timestamp + q->hhf_evict_timeout;

		if (hhf_time_before(prev, now)) {
			/* Delete expired heavy-hitters, but preserve one entry
			 * to avoid kzalloc() when next time this slot is hit.
			 */
			if (list_is_last(&flow->flowchain, head))
				return NULL;
			list_del(&flow->flowchain);
			kfree(flow);
			q->hh_flows_current_cnt--;
		} else if (flow->hash_id == hash) {
			return flow;
		}
	}
	return NULL;
}

/* Returns a flow state entry for a new heavy-hitter.  Either reuses an expired
 * entry or dynamically alloc a new entry.
 */
static struct hh_flow_state *alloc_new_hh(struct list_head *head,
					  struct hhf_sched_data *q)
{
	struct hh_flow_state *flow;
	u32 now = hhf_time_stamp();

	if (!list_empty(head)) {
		/* Find an expired heavy-hitter flow entry. */
		list_for_each_entry(flow, head, flowchain) {
			u32 prev = flow->hit_timestamp + q->hhf_evict_timeout;

			if (hhf_time_before(prev, now))
				return flow;
		}
	}

	if (q->hh_flows_current_cnt >= q->hh_flows_limit) {
		q->hh_flows_overlimit++;
		return NULL;
	}
	/* Create new entry. */
	flow = kzalloc(sizeof(struct hh_flow_state), GFP_ATOMIC);
	if (!flow)
		return NULL;

	q->hh_flows_current_cnt++;
	INIT_LIST_HEAD(&flow->flowchain);
	list_add_tail(&flow->flowchain, head);

	return flow;
}

/* Assigns packets to WDRR buckets.  Implements a multi-stage filter to
 * classify heavy-hitters.
 */
static enum wdrr_bucket_idx hhf_classify(struct sk_buff *skb, struct Qdisc *sch)
{
	struct hhf_sched_data *q = qdisc_priv(sch);
	u32 tmp_hash, hash;
	u32 xorsum, filter_pos[HHF_ARRAYS_CNT], flow_pos;
	struct hh_flow_state *flow;
	u32 pkt_len, min_hhf_val;
	int i;
	u32 prev;
	u32 now = hhf_time_stamp();

	/* Reset the HHF counter arrays if this is the right time. */
	prev = q->hhf_arrays_reset_timestamp + q->hhf_reset_timeout;
	if (hhf_time_before(prev, now)) {
		for (i = 0; i < HHF_ARRAYS_CNT; i++)
			bitmap_zero(q->hhf_valid_bits[i], HHF_ARRAYS_LEN);
		q->hhf_arrays_reset_timestamp = now;
	}

	/* Get hashed flow-id of the skb. */
	hash = skb_get_hash_perturb(skb, &q->perturbation);

	/* Check if this packet belongs to an already established HH flow. */
	flow_pos = hash & HHF_BIT_MASK;
	flow = seek_list(hash, &q->hh_flows[flow_pos], q);
	if (flow) { /* found its HH flow */
		flow->hit_timestamp = now;
		return WDRR_BUCKET_FOR_HH;
	}

	/* Now pass the packet through the multi-stage filter. */
	tmp_hash = hash;
	xorsum = 0;
	for (i = 0; i < HHF_ARRAYS_CNT - 1; i++) {
		/* Split the skb_hash into three 10-bit chunks. */
		filter_pos[i] = tmp_hash & HHF_BIT_MASK;
		xorsum ^= filter_pos[i];
		tmp_hash >>= HHF_BIT_MASK_LEN;
	}
	/* The last chunk is computed as XOR sum of other chunks. */
	filter_pos[HHF_ARRAYS_CNT - 1] = xorsum ^ tmp_hash;

	pkt_len = qdisc_pkt_len(skb);
	min_hhf_val = ~0U;
	for (i = 0; i < HHF_ARRAYS_CNT; i++) {
		u32 val;

		if (!test_bit(filter_pos[i], q->hhf_valid_bits[i])) {
			q->hhf_arrays[i][filter_pos[i]] = 0;
			__set_bit(filter_pos[i], q->hhf_valid_bits[i]);
		}

		val = q->hhf_arrays[i][filter_pos[i]] + pkt_len;
		if (min_hhf_val > val)
			min_hhf_val = val;
	}

	/* Found a new HH iff all counter values > HH admit threshold. */
	if (min_hhf_val > q->hhf_admit_bytes) {
		/* Just captured a new heavy-hitter. */
		flow = alloc_new_hh(&q->hh_flows[flow_pos], q);
		if (!flow) /* memory alloc problem */
			return WDRR_BUCKET_FOR_NON_HH;
		flow->hash_id = hash;
		flow->hit_timestamp = now;
		q->hh_flows_total_cnt++;

		/* By returning without updating counters in q->hhf_arrays,
		 * we implicitly implement "shielding" (see Optimization O1).
		 */
		return WDRR_BUCKET_FOR_HH;
	}

	/* Conservative update of HHF arrays (see Optimization O2). */
	for (i = 0; i < HHF_ARRAYS_CNT; i++) {
		if (q->hhf_arrays[i][filter_pos[i]] < min_hhf_val)
			q->hhf_arrays[i][filter_pos[i]] = min_hhf_val;
	}
	return WDRR_BUCKET_FOR_NON_HH;
}

/* Removes one skb from head of bucket. */
static struct sk_buff *dequeue_head(struct wdrr_bucket *bucket)
{
	struct sk_buff *skb = bucket->head;

	bucket->head = skb->next;
	skb_mark_not_on_list(skb);
	return skb;
}

/* Tail-adds skb to bucket. */
static void bucket_add(struct wdrr_bucket *bucket, struct sk_buff *skb)
{
	if (bucket->head == NULL)
		bucket->head = skb;
	else
		bucket->tail->next = skb;
	bucket->tail = skb;
	skb->next = NULL;
}

static unsigned int hhf_drop(struct Qdisc *sch, struct sk_buff **to_free)
{
	struct hhf_sched_data *q = qdisc_priv(sch);
	struct wdrr_bucket *bucket;

	/* Always try to drop from heavy-hitters first. */
	bucket = &q->buckets[WDRR_BUCKET_FOR_HH];
	if (!bucket->head)
		bucket = &q->buckets[WDRR_BUCKET_FOR_NON_HH];

	if (bucket->head) {
		struct sk_buff *skb = dequeue_head(bucket);

		sch->q.qlen--;
		qdisc_qstats_backlog_dec(sch, skb);
		qdisc_drop(skb, sch, to_free);
	}

	/* Return id of the bucket from which the packet was dropped. */
	return bucket - q->buckets;
}

static int hhf_enqueue(struct sk_buff *skb, struct Qdisc *sch,
		       struct sk_buff **to_free)
{
	struct hhf_sched_data *q = qdisc_priv(sch);
	enum wdrr_bucket_idx idx;
	struct wdrr_bucket *bucket;
	unsigned int prev_backlog;

	idx = hhf_classify(skb, sch);

	bucket = &q->buckets[idx];
	bucket_add(bucket, skb);
	qdisc_qstats_backlog_inc(sch, skb);

	if (list_empty(&bucket->bucketchain)) {
		unsigned int weight;

		/* The logic of new_buckets vs. old_buckets is the same as
		 * new_flows vs. old_flows in the implementation of fq_codel,
		 * i.e., short bursts of non-HHs should have strict priority.
		 */
		if (idx == WDRR_BUCKET_FOR_HH) {
			/* Always move heavy-hitters to old bucket. */
			weight = 1;
			list_add_tail(&bucket->bucketchain, &q->old_buckets);
		} else {
			weight = q->hhf_non_hh_weight;
			list_add_tail(&bucket->bucketchain, &q->new_buckets);
		}
		bucket->deficit = weight * q->quantum;
	}
	if (++sch->q.qlen <= sch->limit)
		return NET_XMIT_SUCCESS;

	prev_backlog = sch->qstats.backlog;
	q->drop_overlimit++;
	/* Return Congestion Notification only if we dropped a packet from this
	 * bucket.
	 */
	if (hhf_drop(sch, to_free) == idx)
		return NET_XMIT_CN;

	/* As we dropped a packet, better let upper stack know this. */
	qdisc_tree_reduce_backlog(sch, 1, prev_backlog - sch->qstats.backlog);
	return NET_XMIT_SUCCESS;
}

static struct sk_buff *hhf_dequeue(struct Qdisc *sch)
{
	struct hhf_sched_data *q = qdisc_priv(sch);
	struct sk_buff *skb = NULL;
	struct wdrr_bucket *bucket;
	struct list_head *head;

begin:
	head = &q->new_buckets;
	if (list_empty(head)) {
		head = &q->old_buckets;
		if (list_empty(head))
			return NULL;
	}
	bucket = list_first_entry(head, struct wdrr_bucket, bucketchain);

	if (bucket->deficit <= 0) {
		int weight = (bucket - q->buckets == WDRR_BUCKET_FOR_HH) ?
			      1 : q->hhf_non_hh_weight;

		bucket->deficit += weight * q->quantum;
		list_move_tail(&bucket->bucketchain, &q->old_buckets);
		goto begin;
	}

	if (bucket->head) {
		skb = dequeue_head(bucket);
		sch->q.qlen--;
		qdisc_qstats_backlog_dec(sch, skb);
	}

	if (!skb) {
		/* Force a pass through old_buckets to prevent starvation. */
		if ((head == &q->new_buckets) && !list_empty(&q->old_buckets))
			list_move_tail(&bucket->bucketchain, &q->old_buckets);
		else
			list_del_init(&bucket->bucketchain);
		goto begin;
	}
	qdisc_bstats_update(sch, skb);
	bucket->deficit -= qdisc_pkt_len(skb);

	return skb;
}

static void hhf_reset(struct Qdisc *sch)
{
	struct sk_buff *skb;

	while ((skb = hhf_dequeue(sch)) != NULL)
		rtnl_kfree_skbs(skb, skb);
}

static void hhf_destroy(struct Qdisc *sch)
{
	int i;
	struct hhf_sched_data *q = qdisc_priv(sch);

	for (i = 0; i < HHF_ARRAYS_CNT; i++) {
		kvfree(q->hhf_arrays[i]);
		kvfree(q->hhf_valid_bits[i]);
	}

	if (!q->hh_flows)
		return;

	for (i = 0; i < HH_FLOWS_CNT; i++) {
		struct hh_flow_state *flow, *next;
		struct list_head *head = &q->hh_flows[i];

		if (list_empty(head))
			continue;
		list_for_each_entry_safe(flow, next, head, flowchain) {
			list_del(&flow->flowchain);
			kfree(flow);
		}
	}
	kvfree(q->hh_flows);
}

static const struct nla_policy hhf_policy[TCA_HHF_MAX + 1] = {
	[TCA_HHF_BACKLOG_LIMIT]	 = { .type = NLA_U32 },
	[TCA_HHF_QUANTUM]	 = { .type = NLA_U32 },
	[TCA_HHF_HH_FLOWS_LIMIT] = { .type = NLA_U32 },
	[TCA_HHF_RESET_TIMEOUT]	 = { .type = NLA_U32 },
	[TCA_HHF_ADMIT_BYTES]	 = { .type = NLA_U32 },
	[TCA_HHF_EVICT_TIMEOUT]	 = { .type = NLA_U32 },
	[TCA_HHF_NON_HH_WEIGHT]	 = { .type = NLA_U32 },
};

static int hhf_change(struct Qdisc *sch, struct nlattr *opt,
		      struct netlink_ext_ack *extack)
{
	struct hhf_sched_data *q = qdisc_priv(sch);
	struct nlattr *tb[TCA_HHF_MAX + 1];
	unsigned int qlen, prev_backlog;
	int err;
	u64 non_hh_quantum;
	u32 new_quantum = q->quantum;
	u32 new_hhf_non_hh_weight = q->hhf_non_hh_weight;

	err = nla_parse_nested_deprecated(tb, TCA_HHF_MAX, opt, hhf_policy,
					  NULL);
	if (err < 0)
		return err;

	if (tb[TCA_HHF_QUANTUM])
		new_quantum = nla_get_u32(tb[TCA_HHF_QUANTUM]);

	if (tb[TCA_HHF_NON_HH_WEIGHT])
		new_hhf_non_hh_weight = nla_get_u32(tb[TCA_HHF_NON_HH_WEIGHT]);

	non_hh_quantum = (u64)new_quantum * new_hhf_non_hh_weight;
	if (non_hh_quantum == 0 || non_hh_quantum > INT_MAX)
		return -EINVAL;

	sch_tree_lock(sch);

	if (tb[TCA_HHF_BACKLOG_LIMIT])
		WRITE_ONCE(sch->limit, nla_get_u32(tb[TCA_HHF_BACKLOG_LIMIT]));

	WRITE_ONCE(q->quantum, new_quantum);
	WRITE_ONCE(q->hhf_non_hh_weight, new_hhf_non_hh_weight);

	if (tb[TCA_HHF_HH_FLOWS_LIMIT])
		WRITE_ONCE(q->hh_flows_limit,
			   nla_get_u32(tb[TCA_HHF_HH_FLOWS_LIMIT]));

	if (tb[TCA_HHF_RESET_TIMEOUT]) {
		u32 us = nla_get_u32(tb[TCA_HHF_RESET_TIMEOUT]);

		WRITE_ONCE(q->hhf_reset_timeout,
			   usecs_to_jiffies(us));
	}

	if (tb[TCA_HHF_ADMIT_BYTES])
		WRITE_ONCE(q->hhf_admit_bytes,
			   nla_get_u32(tb[TCA_HHF_ADMIT_BYTES]));

	if (tb[TCA_HHF_EVICT_TIMEOUT]) {
		u32 us = nla_get_u32(tb[TCA_HHF_EVICT_TIMEOUT]);

		WRITE_ONCE(q->hhf_evict_timeout,
			   usecs_to_jiffies(us));
	}

	qlen = sch->q.qlen;
	prev_backlog = sch->qstats.backlog;
	while (sch->q.qlen > sch->limit) {
		struct sk_buff *skb = hhf_dequeue(sch);

		rtnl_kfree_skbs(skb, skb);
	}
	qdisc_tree_reduce_backlog(sch, qlen - sch->q.qlen,
				  prev_backlog - sch->qstats.backlog);

	sch_tree_unlock(sch);
	return 0;
}

static int hhf_init(struct Qdisc *sch, struct nlattr *opt,
		    struct netlink_ext_ack *extack)
{
	struct hhf_sched_data *q = qdisc_priv(sch);
	int i;

	sch->limit = 1000;
	q->quantum = psched_mtu(qdisc_dev(sch));
	get_random_bytes(&q->perturbation, sizeof(q->perturbation));
	INIT_LIST_HEAD(&q->new_buckets);
	INIT_LIST_HEAD(&q->old_buckets);

	/* Configurable HHF parameters */
	q->hhf_reset_timeout = HZ / 25; /* 40  ms */
	q->hhf_admit_bytes = 131072;    /* 128 KB */
	q->hhf_evict_timeout = HZ;      /* 1  sec */
	q->hhf_non_hh_weight = 2;

	if (opt) {
		int err = hhf_change(sch, opt, extack);

		if (err)
			return err;
	}

	if (!q->hh_flows) {
		/* Initialize heavy-hitter flow table. */
		q->hh_flows = kvcalloc(HH_FLOWS_CNT, sizeof(struct list_head),
				       GFP_KERNEL);
		if (!q->hh_flows)
			return -ENOMEM;
		for (i = 0; i < HH_FLOWS_CNT; i++)
			INIT_LIST_HEAD(&q->hh_flows[i]);

		/* Cap max active HHs at twice len of hh_flows table. */
		q->hh_flows_limit = 2 * HH_FLOWS_CNT;
		q->hh_flows_overlimit = 0;
		q->hh_flows_total_cnt = 0;
		q->hh_flows_current_cnt = 0;

		/* Initialize heavy-hitter filter arrays. */
		for (i = 0; i < HHF_ARRAYS_CNT; i++) {
			q->hhf_arrays[i] = kvcalloc(HHF_ARRAYS_LEN,
						    sizeof(u32),
						    GFP_KERNEL);
			if (!q->hhf_arrays[i]) {
				/* Note: hhf_destroy() will be called
				 * by our caller.
				 */
				return -ENOMEM;
			}
		}
		q->hhf_arrays_reset_timestamp = hhf_time_stamp();

		/* Initialize valid bits of heavy-hitter filter arrays. */
		for (i = 0; i < HHF_ARRAYS_CNT; i++) {
			q->hhf_valid_bits[i] = kvzalloc(HHF_ARRAYS_LEN /
							  BITS_PER_BYTE, GFP_KERNEL);
			if (!q->hhf_valid_bits[i]) {
				/* Note: hhf_destroy() will be called
				 * by our caller.
				 */
				return -ENOMEM;
			}
		}

		/* Initialize Weighted DRR buckets. */
		for (i = 0; i < WDRR_BUCKET_CNT; i++) {
			struct wdrr_bucket *bucket = q->buckets + i;

			INIT_LIST_HEAD(&bucket->bucketchain);
		}
	}

	return 0;
}

static int hhf_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct hhf_sched_data *q = qdisc_priv(sch);
	struct nlattr *opts;

	opts = nla_nest_start_noflag(skb, TCA_OPTIONS);
	if (opts == NULL)
		goto nla_put_failure;

	if (nla_put_u32(skb, TCA_HHF_BACKLOG_LIMIT, READ_ONCE(sch->limit)) ||
	    nla_put_u32(skb, TCA_HHF_QUANTUM, READ_ONCE(q->quantum)) ||
	    nla_put_u32(skb, TCA_HHF_HH_FLOWS_LIMIT,
			READ_ONCE(q->hh_flows_limit)) ||
	    nla_put_u32(skb, TCA_HHF_RESET_TIMEOUT,
			jiffies_to_usecs(READ_ONCE(q->hhf_reset_timeout))) ||
	    nla_put_u32(skb, TCA_HHF_ADMIT_BYTES,
			READ_ONCE(q->hhf_admit_bytes)) ||
	    nla_put_u32(skb, TCA_HHF_EVICT_TIMEOUT,
			jiffies_to_usecs(READ_ONCE(q->hhf_evict_timeout))) ||
	    nla_put_u32(skb, TCA_HHF_NON_HH_WEIGHT,
			READ_ONCE(q->hhf_non_hh_weight)))
		goto nla_put_failure;

	return nla_nest_end(skb, opts);

nla_put_failure:
	return -1;
}

static int hhf_dump_stats(struct Qdisc *sch, struct gnet_dump *d)
{
	struct hhf_sched_data *q = qdisc_priv(sch);
	struct tc_hhf_xstats st = {
		.drop_overlimit = q->drop_overlimit,
		.hh_overlimit	= q->hh_flows_overlimit,
		.hh_tot_count	= q->hh_flows_total_cnt,
		.hh_cur_count	= q->hh_flows_current_cnt,
	};

	return gnet_stats_copy_app(d, &st, sizeof(st));
}

static struct Qdisc_ops hhf_qdisc_ops __read_mostly = {
	.id		=	"hhf",
	.priv_size	=	sizeof(struct hhf_sched_data),

	.enqueue	=	hhf_enqueue,
	.dequeue	=	hhf_dequeue,
	.peek		=	qdisc_peek_dequeued,
	.init		=	hhf_init,
	.reset		=	hhf_reset,
	.destroy	=	hhf_destroy,
	.change		=	hhf_change,
	.dump		=	hhf_dump,
	.dump_stats	=	hhf_dump_stats,
	.owner		=	THIS_MODULE,
};
MODULE_ALIAS_NET_SCH("hhf");

static int __init hhf_module_init(void)
{
	return register_qdisc(&hhf_qdisc_ops);
}

static void __exit hhf_module_exit(void)
{
	unregister_qdisc(&hhf_qdisc_ops);
}

module_init(hhf_module_init)
module_exit(hhf_module_exit)
MODULE_AUTHOR("Terry Lam");
MODULE_AUTHOR("Nandita Dukkipati");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Heavy-Hitter Filter (HHF)");
