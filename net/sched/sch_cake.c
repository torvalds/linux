// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

/* COMMON Applications Kept Enhanced (CAKE) discipline
 *
 * Copyright (C) 2014-2018 Jonathan Morton <chromatix99@gmail.com>
 * Copyright (C) 2015-2018 Toke Høiland-Jørgensen <toke@toke.dk>
 * Copyright (C) 2014-2018 Dave Täht <dave.taht@gmail.com>
 * Copyright (C) 2015-2018 Sebastian Moeller <moeller0@gmx.de>
 * (C) 2015-2018 Kevin Darbyshire-Bryant <kevin@darbyshire-bryant.me.uk>
 * Copyright (C) 2017-2018 Ryan Mounce <ryan@mounce.com.au>
 *
 * The CAKE Principles:
 *		   (or, how to have your cake and eat it too)
 *
 * This is a combination of several shaping, AQM and FQ techniques into one
 * easy-to-use package:
 *
 * - An overall bandwidth shaper, to move the bottleneck away from dumb CPE
 *   equipment and bloated MACs.  This operates in deficit mode (as in sch_fq),
 *   eliminating the need for any sort of burst parameter (eg. token bucket
 *   depth).  Burst support is limited to that necessary to overcome scheduling
 *   latency.
 *
 * - A Diffserv-aware priority queue, giving more priority to certain classes,
 *   up to a specified fraction of bandwidth.  Above that bandwidth threshold,
 *   the priority is reduced to avoid starving other tins.
 *
 * - Each priority tin has a separate Flow Queue system, to isolate traffic
 *   flows from each other.  This prevents a burst on one flow from increasing
 *   the delay to another.  Flows are distributed to queues using a
 *   set-associative hash function.
 *
 * - Each queue is actively managed by Cobalt, which is a combination of the
 *   Codel and Blue AQM algorithms.  This serves flows fairly, and signals
 *   congestion early via ECN (if available) and/or packet drops, to keep
 *   latency low.  The codel parameters are auto-tuned based on the bandwidth
 *   setting, as is necessary at low bandwidths.
 *
 * The configuration parameters are kept deliberately simple for ease of use.
 * Everything has sane defaults.  Complete generality of configuration is *not*
 * a goal.
 *
 * The priority queue operates according to a weighted DRR scheme, combined with
 * a bandwidth tracker which reuses the shaper logic to detect which side of the
 * bandwidth sharing threshold the tin is operating.  This determines whether a
 * priority-based weight (high) or a bandwidth-based weight (low) is used for
 * that tin in the current pass.
 *
 * This qdisc was inspired by Eric Dumazet's fq_codel code, which he kindly
 * granted us permission to leverage.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/string.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/jhash.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/reciprocal_div.h>
#include <net/netlink.h>
#include <linux/if_vlan.h>
#include <net/gso.h>
#include <net/pkt_sched.h>
#include <net/pkt_cls.h>
#include <net/tcp.h>
#include <net/flow_dissector.h>

#if IS_ENABLED(CONFIG_NF_CONNTRACK)
#include <net/netfilter/nf_conntrack_core.h>
#endif

#define CAKE_SET_WAYS (8)
#define CAKE_MAX_TINS (8)
#define CAKE_QUEUES (1024)
#define CAKE_FLOW_MASK 63
#define CAKE_FLOW_NAT_FLAG 64

/* struct cobalt_params - contains codel and blue parameters
 * @interval:	codel initial drop rate
 * @target:     maximum persistent sojourn time & blue update rate
 * @mtu_time:   serialisation delay of maximum-size packet
 * @p_inc:      increment of blue drop probability (0.32 fxp)
 * @p_dec:      decrement of blue drop probability (0.32 fxp)
 */
struct cobalt_params {
	u64	interval;
	u64	target;
	u64	mtu_time;
	u32	p_inc;
	u32	p_dec;
};

/* struct cobalt_vars - contains codel and blue variables
 * @count:		codel dropping frequency
 * @rec_inv_sqrt:	reciprocal value of sqrt(count) >> 1
 * @drop_next:		time to drop next packet, or when we dropped last
 * @blue_timer:		Blue time to next drop
 * @p_drop:		BLUE drop probability (0.32 fxp)
 * @dropping:		set if in dropping state
 * @ecn_marked:		set if marked
 */
struct cobalt_vars {
	u32	count;
	u32	rec_inv_sqrt;
	ktime_t	drop_next;
	ktime_t	blue_timer;
	u32     p_drop;
	bool	dropping;
	bool    ecn_marked;
};

enum {
	CAKE_SET_NONE = 0,
	CAKE_SET_SPARSE,
	CAKE_SET_SPARSE_WAIT, /* counted in SPARSE, actually in BULK */
	CAKE_SET_BULK,
	CAKE_SET_DECAYING
};

struct cake_flow {
	/* this stuff is all needed per-flow at dequeue time */
	struct sk_buff	  *head;
	struct sk_buff	  *tail;
	struct list_head  flowchain;
	s32		  deficit;
	u32		  dropped;
	struct cobalt_vars cvars;
	u16		  srchost; /* index into cake_host table */
	u16		  dsthost;
	u8		  set;
}; /* please try to keep this structure <= 64 bytes */

struct cake_host {
	u32 srchost_tag;
	u32 dsthost_tag;
	u16 srchost_bulk_flow_count;
	u16 dsthost_bulk_flow_count;
};

struct cake_heap_entry {
	u16 t:3, b:10;
};

struct cake_tin_data {
	struct cake_flow flows[CAKE_QUEUES];
	u32	backlogs[CAKE_QUEUES];
	u32	tags[CAKE_QUEUES]; /* for set association */
	u16	overflow_idx[CAKE_QUEUES];
	struct cake_host hosts[CAKE_QUEUES]; /* for triple isolation */
	u16	flow_quantum;

	struct cobalt_params cparams;
	u32	drop_overlimit;
	u16	bulk_flow_count;
	u16	sparse_flow_count;
	u16	decaying_flow_count;
	u16	unresponsive_flow_count;

	u32	max_skblen;

	struct list_head new_flows;
	struct list_head old_flows;
	struct list_head decaying_flows;

	/* time_next = time_this + ((len * rate_ns) >> rate_shft) */
	ktime_t	time_next_packet;
	u64	tin_rate_ns;
	u64	tin_rate_bps;
	u16	tin_rate_shft;

	u16	tin_quantum;
	s32	tin_deficit;
	u32	tin_backlog;
	u32	tin_dropped;
	u32	tin_ecn_mark;

	u32	packets;
	u64	bytes;

	u32	ack_drops;

	/* moving averages */
	u64 avge_delay;
	u64 peak_delay;
	u64 base_delay;

	/* hash function stats */
	u32	way_directs;
	u32	way_hits;
	u32	way_misses;
	u32	way_collisions;
}; /* number of tins is small, so size of this struct doesn't matter much */

struct cake_sched_data {
	struct tcf_proto __rcu *filter_list; /* optional external classifier */
	struct tcf_block *block;
	struct cake_tin_data *tins;

	struct cake_heap_entry overflow_heap[CAKE_QUEUES * CAKE_MAX_TINS];
	u16		overflow_timeout;

	u16		tin_cnt;
	u8		tin_mode;
	u8		flow_mode;
	u8		ack_filter;
	u8		atm_mode;

	u32		fwmark_mask;
	u16		fwmark_shft;

	/* time_next = time_this + ((len * rate_ns) >> rate_shft) */
	u16		rate_shft;
	ktime_t		time_next_packet;
	ktime_t		failsafe_next_packet;
	u64		rate_ns;
	u64		rate_bps;
	u16		rate_flags;
	s16		rate_overhead;
	u16		rate_mpu;
	u64		interval;
	u64		target;

	/* resource tracking */
	u32		buffer_used;
	u32		buffer_max_used;
	u32		buffer_limit;
	u32		buffer_config_limit;

	/* indices for dequeue */
	u16		cur_tin;
	u16		cur_flow;

	struct qdisc_watchdog watchdog;
	const u8	*tin_index;
	const u8	*tin_order;

	/* bandwidth capacity estimate */
	ktime_t		last_packet_time;
	ktime_t		avg_window_begin;
	u64		avg_packet_interval;
	u64		avg_window_bytes;
	u64		avg_peak_bandwidth;
	ktime_t		last_reconfig_time;

	/* packet length stats */
	u32		avg_netoff;
	u16		max_netlen;
	u16		max_adjlen;
	u16		min_netlen;
	u16		min_adjlen;
};

enum {
	CAKE_FLAG_OVERHEAD	   = BIT(0),
	CAKE_FLAG_AUTORATE_INGRESS = BIT(1),
	CAKE_FLAG_INGRESS	   = BIT(2),
	CAKE_FLAG_WASH		   = BIT(3),
	CAKE_FLAG_SPLIT_GSO	   = BIT(4)
};

/* COBALT operates the Codel and BLUE algorithms in parallel, in order to
 * obtain the best features of each.  Codel is excellent on flows which
 * respond to congestion signals in a TCP-like way.  BLUE is more effective on
 * unresponsive flows.
 */

struct cobalt_skb_cb {
	ktime_t enqueue_time;
	u32     adjusted_len;
};

static u64 us_to_ns(u64 us)
{
	return us * NSEC_PER_USEC;
}

static struct cobalt_skb_cb *get_cobalt_cb(const struct sk_buff *skb)
{
	qdisc_cb_private_validate(skb, sizeof(struct cobalt_skb_cb));
	return (struct cobalt_skb_cb *)qdisc_skb_cb(skb)->data;
}

static ktime_t cobalt_get_enqueue_time(const struct sk_buff *skb)
{
	return get_cobalt_cb(skb)->enqueue_time;
}

static void cobalt_set_enqueue_time(struct sk_buff *skb,
				    ktime_t now)
{
	get_cobalt_cb(skb)->enqueue_time = now;
}

static u16 quantum_div[CAKE_QUEUES + 1] = {0};

/* Diffserv lookup tables */

static const u8 precedence[] = {
	0, 0, 0, 0, 0, 0, 0, 0,
	1, 1, 1, 1, 1, 1, 1, 1,
	2, 2, 2, 2, 2, 2, 2, 2,
	3, 3, 3, 3, 3, 3, 3, 3,
	4, 4, 4, 4, 4, 4, 4, 4,
	5, 5, 5, 5, 5, 5, 5, 5,
	6, 6, 6, 6, 6, 6, 6, 6,
	7, 7, 7, 7, 7, 7, 7, 7,
};

static const u8 diffserv8[] = {
	2, 0, 1, 2, 4, 2, 2, 2,
	1, 2, 1, 2, 1, 2, 1, 2,
	5, 2, 4, 2, 4, 2, 4, 2,
	3, 2, 3, 2, 3, 2, 3, 2,
	6, 2, 3, 2, 3, 2, 3, 2,
	6, 2, 2, 2, 6, 2, 6, 2,
	7, 2, 2, 2, 2, 2, 2, 2,
	7, 2, 2, 2, 2, 2, 2, 2,
};

static const u8 diffserv4[] = {
	0, 1, 0, 0, 2, 0, 0, 0,
	1, 0, 0, 0, 0, 0, 0, 0,
	2, 0, 2, 0, 2, 0, 2, 0,
	2, 0, 2, 0, 2, 0, 2, 0,
	3, 0, 2, 0, 2, 0, 2, 0,
	3, 0, 0, 0, 3, 0, 3, 0,
	3, 0, 0, 0, 0, 0, 0, 0,
	3, 0, 0, 0, 0, 0, 0, 0,
};

static const u8 diffserv3[] = {
	0, 1, 0, 0, 2, 0, 0, 0,
	1, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 2, 0, 2, 0,
	2, 0, 0, 0, 0, 0, 0, 0,
	2, 0, 0, 0, 0, 0, 0, 0,
};

static const u8 besteffort[] = {
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
};

/* tin priority order for stats dumping */

static const u8 normal_order[] = {0, 1, 2, 3, 4, 5, 6, 7};
static const u8 bulk_order[] = {1, 0, 2, 3};

/* There is a big difference in timing between the accurate values placed in the
 * cache and the approximations given by a single Newton step for small count
 * values, particularly when stepping from count 1 to 2 or vice versa. Hence,
 * these values are calculated using eight Newton steps, using the
 * implementation below. Above 16, a single Newton step gives sufficient
 * accuracy in either direction, given the precision stored.
 *
 * The magnitude of the error when stepping up to count 2 is such as to give the
 * value that *should* have been produced at count 4.
 */

#define REC_INV_SQRT_CACHE (16)
static const u32 inv_sqrt_cache[REC_INV_SQRT_CACHE] = {
		~0,         ~0, 3037000500, 2479700525,
	2147483647, 1920767767, 1753413056, 1623345051,
	1518500250, 1431655765, 1358187914, 1294981364,
	1239850263, 1191209601, 1147878294, 1108955788
};

/* http://en.wikipedia.org/wiki/Methods_of_computing_square_roots
 * new_invsqrt = (invsqrt / 2) * (3 - count * invsqrt^2)
 *
 * Here, invsqrt is a fixed point number (< 1.0), 32bit mantissa, aka Q0.32
 */

static void cobalt_newton_step(struct cobalt_vars *vars)
{
	u32 invsqrt, invsqrt2;
	u64 val;

	invsqrt = vars->rec_inv_sqrt;
	invsqrt2 = ((u64)invsqrt * invsqrt) >> 32;
	val = (3LL << 32) - ((u64)vars->count * invsqrt2);

	val >>= 2; /* avoid overflow in following multiply */
	val = (val * invsqrt) >> (32 - 2 + 1);

	vars->rec_inv_sqrt = val;
}

static void cobalt_invsqrt(struct cobalt_vars *vars)
{
	if (vars->count < REC_INV_SQRT_CACHE)
		vars->rec_inv_sqrt = inv_sqrt_cache[vars->count];
	else
		cobalt_newton_step(vars);
}

static void cobalt_vars_init(struct cobalt_vars *vars)
{
	memset(vars, 0, sizeof(*vars));
}

/* CoDel control_law is t + interval/sqrt(count)
 * We maintain in rec_inv_sqrt the reciprocal value of sqrt(count) to avoid
 * both sqrt() and divide operation.
 */
static ktime_t cobalt_control(ktime_t t,
			      u64 interval,
			      u32 rec_inv_sqrt)
{
	return ktime_add_ns(t, reciprocal_scale(interval,
						rec_inv_sqrt));
}

/* Call this when a packet had to be dropped due to queue overflow.  Returns
 * true if the BLUE state was quiescent before but active after this call.
 */
static bool cobalt_queue_full(struct cobalt_vars *vars,
			      struct cobalt_params *p,
			      ktime_t now)
{
	bool up = false;

	if (ktime_to_ns(ktime_sub(now, vars->blue_timer)) > p->target) {
		up = !vars->p_drop;
		vars->p_drop += p->p_inc;
		if (vars->p_drop < p->p_inc)
			vars->p_drop = ~0;
		vars->blue_timer = now;
	}
	vars->dropping = true;
	vars->drop_next = now;
	if (!vars->count)
		vars->count = 1;

	return up;
}

/* Call this when the queue was serviced but turned out to be empty.  Returns
 * true if the BLUE state was active before but quiescent after this call.
 */
static bool cobalt_queue_empty(struct cobalt_vars *vars,
			       struct cobalt_params *p,
			       ktime_t now)
{
	bool down = false;

	if (vars->p_drop &&
	    ktime_to_ns(ktime_sub(now, vars->blue_timer)) > p->target) {
		if (vars->p_drop < p->p_dec)
			vars->p_drop = 0;
		else
			vars->p_drop -= p->p_dec;
		vars->blue_timer = now;
		down = !vars->p_drop;
	}
	vars->dropping = false;

	if (vars->count && ktime_to_ns(ktime_sub(now, vars->drop_next)) >= 0) {
		vars->count--;
		cobalt_invsqrt(vars);
		vars->drop_next = cobalt_control(vars->drop_next,
						 p->interval,
						 vars->rec_inv_sqrt);
	}

	return down;
}

/* Call this with a freshly dequeued packet for possible congestion marking.
 * Returns true as an instruction to drop the packet, false for delivery.
 */
static enum skb_drop_reason cobalt_should_drop(struct cobalt_vars *vars,
					       struct cobalt_params *p,
					       ktime_t now,
					       struct sk_buff *skb,
					       u32 bulk_flows)
{
	enum skb_drop_reason reason = SKB_NOT_DROPPED_YET;
	bool next_due, over_target;
	ktime_t schedule;
	u64 sojourn;

/* The 'schedule' variable records, in its sign, whether 'now' is before or
 * after 'drop_next'.  This allows 'drop_next' to be updated before the next
 * scheduling decision is actually branched, without destroying that
 * information.  Similarly, the first 'schedule' value calculated is preserved
 * in the boolean 'next_due'.
 *
 * As for 'drop_next', we take advantage of the fact that 'interval' is both
 * the delay between first exceeding 'target' and the first signalling event,
 * *and* the scaling factor for the signalling frequency.  It's therefore very
 * natural to use a single mechanism for both purposes, and eliminates a
 * significant amount of reference Codel's spaghetti code.  To help with this,
 * both the '0' and '1' entries in the invsqrt cache are 0xFFFFFFFF, as close
 * as possible to 1.0 in fixed-point.
 */

	sojourn = ktime_to_ns(ktime_sub(now, cobalt_get_enqueue_time(skb)));
	schedule = ktime_sub(now, vars->drop_next);
	over_target = sojourn > p->target &&
		      sojourn > p->mtu_time * bulk_flows * 2 &&
		      sojourn > p->mtu_time * 4;
	next_due = vars->count && ktime_to_ns(schedule) >= 0;

	vars->ecn_marked = false;

	if (over_target) {
		if (!vars->dropping) {
			vars->dropping = true;
			vars->drop_next = cobalt_control(now,
							 p->interval,
							 vars->rec_inv_sqrt);
		}
		if (!vars->count)
			vars->count = 1;
	} else if (vars->dropping) {
		vars->dropping = false;
	}

	if (next_due && vars->dropping) {
		/* Use ECN mark if possible, otherwise drop */
		if (!(vars->ecn_marked = INET_ECN_set_ce(skb)))
			reason = SKB_DROP_REASON_QDISC_CONGESTED;

		vars->count++;
		if (!vars->count)
			vars->count--;
		cobalt_invsqrt(vars);
		vars->drop_next = cobalt_control(vars->drop_next,
						 p->interval,
						 vars->rec_inv_sqrt);
		schedule = ktime_sub(now, vars->drop_next);
	} else {
		while (next_due) {
			vars->count--;
			cobalt_invsqrt(vars);
			vars->drop_next = cobalt_control(vars->drop_next,
							 p->interval,
							 vars->rec_inv_sqrt);
			schedule = ktime_sub(now, vars->drop_next);
			next_due = vars->count && ktime_to_ns(schedule) >= 0;
		}
	}

	/* Simple BLUE implementation.  Lack of ECN is deliberate. */
	if (vars->p_drop && reason == SKB_NOT_DROPPED_YET &&
	    get_random_u32() < vars->p_drop)
		reason = SKB_DROP_REASON_CAKE_FLOOD;

	/* Overload the drop_next field as an activity timeout */
	if (!vars->count)
		vars->drop_next = ktime_add_ns(now, p->interval);
	else if (ktime_to_ns(schedule) > 0 && reason == SKB_NOT_DROPPED_YET)
		vars->drop_next = now;

	return reason;
}

static bool cake_update_flowkeys(struct flow_keys *keys,
				 const struct sk_buff *skb)
{
#if IS_ENABLED(CONFIG_NF_CONNTRACK)
	struct nf_conntrack_tuple tuple = {};
	bool rev = !skb->_nfct, upd = false;
	__be32 ip;

	if (skb_protocol(skb, true) != htons(ETH_P_IP))
		return false;

	if (!nf_ct_get_tuple_skb(&tuple, skb))
		return false;

	ip = rev ? tuple.dst.u3.ip : tuple.src.u3.ip;
	if (ip != keys->addrs.v4addrs.src) {
		keys->addrs.v4addrs.src = ip;
		upd = true;
	}
	ip = rev ? tuple.src.u3.ip : tuple.dst.u3.ip;
	if (ip != keys->addrs.v4addrs.dst) {
		keys->addrs.v4addrs.dst = ip;
		upd = true;
	}

	if (keys->ports.ports) {
		__be16 port;

		port = rev ? tuple.dst.u.all : tuple.src.u.all;
		if (port != keys->ports.src) {
			keys->ports.src = port;
			upd = true;
		}
		port = rev ? tuple.src.u.all : tuple.dst.u.all;
		if (port != keys->ports.dst) {
			port = keys->ports.dst;
			upd = true;
		}
	}
	return upd;
#else
	return false;
#endif
}

/* Cake has several subtle multiple bit settings. In these cases you
 *  would be matching triple isolate mode as well.
 */

static bool cake_dsrc(int flow_mode)
{
	return (flow_mode & CAKE_FLOW_DUAL_SRC) == CAKE_FLOW_DUAL_SRC;
}

static bool cake_ddst(int flow_mode)
{
	return (flow_mode & CAKE_FLOW_DUAL_DST) == CAKE_FLOW_DUAL_DST;
}

static u32 cake_hash(struct cake_tin_data *q, const struct sk_buff *skb,
		     int flow_mode, u16 flow_override, u16 host_override)
{
	bool hash_flows = (!flow_override && !!(flow_mode & CAKE_FLOW_FLOWS));
	bool hash_hosts = (!host_override && !!(flow_mode & CAKE_FLOW_HOSTS));
	bool nat_enabled = !!(flow_mode & CAKE_FLOW_NAT_FLAG);
	u32 flow_hash = 0, srchost_hash = 0, dsthost_hash = 0;
	u16 reduced_hash, srchost_idx, dsthost_idx;
	struct flow_keys keys, host_keys;
	bool use_skbhash = skb->l4_hash;

	if (unlikely(flow_mode == CAKE_FLOW_NONE))
		return 0;

	/* If both overrides are set, or we can use the SKB hash and nat mode is
	 * disabled, we can skip packet dissection entirely. If nat mode is
	 * enabled there's another check below after doing the conntrack lookup.
	 */
	if ((!hash_flows || (use_skbhash && !nat_enabled)) && !hash_hosts)
		goto skip_hash;

	skb_flow_dissect_flow_keys(skb, &keys,
				   FLOW_DISSECTOR_F_STOP_AT_FLOW_LABEL);

	/* Don't use the SKB hash if we change the lookup keys from conntrack */
	if (nat_enabled && cake_update_flowkeys(&keys, skb))
		use_skbhash = false;

	/* If we can still use the SKB hash and don't need the host hash, we can
	 * skip the rest of the hashing procedure
	 */
	if (use_skbhash && !hash_hosts)
		goto skip_hash;

	/* flow_hash_from_keys() sorts the addresses by value, so we have
	 * to preserve their order in a separate data structure to treat
	 * src and dst host addresses as independently selectable.
	 */
	host_keys = keys;
	host_keys.ports.ports     = 0;
	host_keys.basic.ip_proto  = 0;
	host_keys.keyid.keyid     = 0;
	host_keys.tags.flow_label = 0;

	switch (host_keys.control.addr_type) {
	case FLOW_DISSECTOR_KEY_IPV4_ADDRS:
		host_keys.addrs.v4addrs.src = 0;
		dsthost_hash = flow_hash_from_keys(&host_keys);
		host_keys.addrs.v4addrs.src = keys.addrs.v4addrs.src;
		host_keys.addrs.v4addrs.dst = 0;
		srchost_hash = flow_hash_from_keys(&host_keys);
		break;

	case FLOW_DISSECTOR_KEY_IPV6_ADDRS:
		memset(&host_keys.addrs.v6addrs.src, 0,
		       sizeof(host_keys.addrs.v6addrs.src));
		dsthost_hash = flow_hash_from_keys(&host_keys);
		host_keys.addrs.v6addrs.src = keys.addrs.v6addrs.src;
		memset(&host_keys.addrs.v6addrs.dst, 0,
		       sizeof(host_keys.addrs.v6addrs.dst));
		srchost_hash = flow_hash_from_keys(&host_keys);
		break;

	default:
		dsthost_hash = 0;
		srchost_hash = 0;
	}

	/* This *must* be after the above switch, since as a
	 * side-effect it sorts the src and dst addresses.
	 */
	if (hash_flows && !use_skbhash)
		flow_hash = flow_hash_from_keys(&keys);

skip_hash:
	if (flow_override)
		flow_hash = flow_override - 1;
	else if (use_skbhash && (flow_mode & CAKE_FLOW_FLOWS))
		flow_hash = skb->hash;
	if (host_override) {
		dsthost_hash = host_override - 1;
		srchost_hash = host_override - 1;
	}

	if (!(flow_mode & CAKE_FLOW_FLOWS)) {
		if (flow_mode & CAKE_FLOW_SRC_IP)
			flow_hash ^= srchost_hash;

		if (flow_mode & CAKE_FLOW_DST_IP)
			flow_hash ^= dsthost_hash;
	}

	reduced_hash = flow_hash % CAKE_QUEUES;

	/* set-associative hashing */
	/* fast path if no hash collision (direct lookup succeeds) */
	if (likely(q->tags[reduced_hash] == flow_hash &&
		   q->flows[reduced_hash].set)) {
		q->way_directs++;
	} else {
		u32 inner_hash = reduced_hash % CAKE_SET_WAYS;
		u32 outer_hash = reduced_hash - inner_hash;
		bool allocate_src = false;
		bool allocate_dst = false;
		u32 i, k;

		/* check if any active queue in the set is reserved for
		 * this flow.
		 */
		for (i = 0, k = inner_hash; i < CAKE_SET_WAYS;
		     i++, k = (k + 1) % CAKE_SET_WAYS) {
			if (q->tags[outer_hash + k] == flow_hash) {
				if (i)
					q->way_hits++;

				if (!q->flows[outer_hash + k].set) {
					/* need to increment host refcnts */
					allocate_src = cake_dsrc(flow_mode);
					allocate_dst = cake_ddst(flow_mode);
				}

				goto found;
			}
		}

		/* no queue is reserved for this flow, look for an
		 * empty one.
		 */
		for (i = 0; i < CAKE_SET_WAYS;
			 i++, k = (k + 1) % CAKE_SET_WAYS) {
			if (!q->flows[outer_hash + k].set) {
				q->way_misses++;
				allocate_src = cake_dsrc(flow_mode);
				allocate_dst = cake_ddst(flow_mode);
				goto found;
			}
		}

		/* With no empty queues, default to the original
		 * queue, accept the collision, update the host tags.
		 */
		q->way_collisions++;
		allocate_src = cake_dsrc(flow_mode);
		allocate_dst = cake_ddst(flow_mode);

		if (q->flows[outer_hash + k].set == CAKE_SET_BULK) {
			if (allocate_src)
				q->hosts[q->flows[reduced_hash].srchost].srchost_bulk_flow_count--;
			if (allocate_dst)
				q->hosts[q->flows[reduced_hash].dsthost].dsthost_bulk_flow_count--;
		}
found:
		/* reserve queue for future packets in same flow */
		reduced_hash = outer_hash + k;
		q->tags[reduced_hash] = flow_hash;

		if (allocate_src) {
			srchost_idx = srchost_hash % CAKE_QUEUES;
			inner_hash = srchost_idx % CAKE_SET_WAYS;
			outer_hash = srchost_idx - inner_hash;
			for (i = 0, k = inner_hash; i < CAKE_SET_WAYS;
				i++, k = (k + 1) % CAKE_SET_WAYS) {
				if (q->hosts[outer_hash + k].srchost_tag ==
				    srchost_hash)
					goto found_src;
			}
			for (i = 0; i < CAKE_SET_WAYS;
				i++, k = (k + 1) % CAKE_SET_WAYS) {
				if (!q->hosts[outer_hash + k].srchost_bulk_flow_count)
					break;
			}
			q->hosts[outer_hash + k].srchost_tag = srchost_hash;
found_src:
			srchost_idx = outer_hash + k;
			if (q->flows[reduced_hash].set == CAKE_SET_BULK)
				q->hosts[srchost_idx].srchost_bulk_flow_count++;
			q->flows[reduced_hash].srchost = srchost_idx;
		}

		if (allocate_dst) {
			dsthost_idx = dsthost_hash % CAKE_QUEUES;
			inner_hash = dsthost_idx % CAKE_SET_WAYS;
			outer_hash = dsthost_idx - inner_hash;
			for (i = 0, k = inner_hash; i < CAKE_SET_WAYS;
			     i++, k = (k + 1) % CAKE_SET_WAYS) {
				if (q->hosts[outer_hash + k].dsthost_tag ==
				    dsthost_hash)
					goto found_dst;
			}
			for (i = 0; i < CAKE_SET_WAYS;
			     i++, k = (k + 1) % CAKE_SET_WAYS) {
				if (!q->hosts[outer_hash + k].dsthost_bulk_flow_count)
					break;
			}
			q->hosts[outer_hash + k].dsthost_tag = dsthost_hash;
found_dst:
			dsthost_idx = outer_hash + k;
			if (q->flows[reduced_hash].set == CAKE_SET_BULK)
				q->hosts[dsthost_idx].dsthost_bulk_flow_count++;
			q->flows[reduced_hash].dsthost = dsthost_idx;
		}
	}

	return reduced_hash;
}

/* helper functions : might be changed when/if skb use a standard list_head */
/* remove one skb from head of slot queue */

static struct sk_buff *dequeue_head(struct cake_flow *flow)
{
	struct sk_buff *skb = flow->head;

	if (skb) {
		flow->head = skb->next;
		skb_mark_not_on_list(skb);
	}

	return skb;
}

/* add skb to flow queue (tail add) */

static void flow_queue_add(struct cake_flow *flow, struct sk_buff *skb)
{
	if (!flow->head)
		flow->head = skb;
	else
		flow->tail->next = skb;
	flow->tail = skb;
	skb->next = NULL;
}

static struct iphdr *cake_get_iphdr(const struct sk_buff *skb,
				    struct ipv6hdr *buf)
{
	unsigned int offset = skb_network_offset(skb);
	struct iphdr *iph;

	iph = skb_header_pointer(skb, offset, sizeof(struct iphdr), buf);

	if (!iph)
		return NULL;

	if (iph->version == 4 && iph->protocol == IPPROTO_IPV6)
		return skb_header_pointer(skb, offset + iph->ihl * 4,
					  sizeof(struct ipv6hdr), buf);

	else if (iph->version == 4)
		return iph;

	else if (iph->version == 6)
		return skb_header_pointer(skb, offset, sizeof(struct ipv6hdr),
					  buf);

	return NULL;
}

static struct tcphdr *cake_get_tcphdr(const struct sk_buff *skb,
				      void *buf, unsigned int bufsize)
{
	unsigned int offset = skb_network_offset(skb);
	const struct ipv6hdr *ipv6h;
	const struct tcphdr *tcph;
	const struct iphdr *iph;
	struct ipv6hdr _ipv6h;
	struct tcphdr _tcph;

	ipv6h = skb_header_pointer(skb, offset, sizeof(_ipv6h), &_ipv6h);

	if (!ipv6h)
		return NULL;

	if (ipv6h->version == 4) {
		iph = (struct iphdr *)ipv6h;
		offset += iph->ihl * 4;

		/* special-case 6in4 tunnelling, as that is a common way to get
		 * v6 connectivity in the home
		 */
		if (iph->protocol == IPPROTO_IPV6) {
			ipv6h = skb_header_pointer(skb, offset,
						   sizeof(_ipv6h), &_ipv6h);

			if (!ipv6h || ipv6h->nexthdr != IPPROTO_TCP)
				return NULL;

			offset += sizeof(struct ipv6hdr);

		} else if (iph->protocol != IPPROTO_TCP) {
			return NULL;
		}

	} else if (ipv6h->version == 6) {
		if (ipv6h->nexthdr != IPPROTO_TCP)
			return NULL;

		offset += sizeof(struct ipv6hdr);
	} else {
		return NULL;
	}

	tcph = skb_header_pointer(skb, offset, sizeof(_tcph), &_tcph);
	if (!tcph || tcph->doff < 5)
		return NULL;

	return skb_header_pointer(skb, offset,
				  min(__tcp_hdrlen(tcph), bufsize), buf);
}

static const void *cake_get_tcpopt(const struct tcphdr *tcph,
				   int code, int *oplen)
{
	/* inspired by tcp_parse_options in tcp_input.c */
	int length = __tcp_hdrlen(tcph) - sizeof(struct tcphdr);
	const u8 *ptr = (const u8 *)(tcph + 1);

	while (length > 0) {
		int opcode = *ptr++;
		int opsize;

		if (opcode == TCPOPT_EOL)
			break;
		if (opcode == TCPOPT_NOP) {
			length--;
			continue;
		}
		if (length < 2)
			break;
		opsize = *ptr++;
		if (opsize < 2 || opsize > length)
			break;

		if (opcode == code) {
			*oplen = opsize;
			return ptr;
		}

		ptr += opsize - 2;
		length -= opsize;
	}

	return NULL;
}

/* Compare two SACK sequences. A sequence is considered greater if it SACKs more
 * bytes than the other. In the case where both sequences ACKs bytes that the
 * other doesn't, A is considered greater. DSACKs in A also makes A be
 * considered greater.
 *
 * @return -1, 0 or 1 as normal compare functions
 */
static int cake_tcph_sack_compare(const struct tcphdr *tcph_a,
				  const struct tcphdr *tcph_b)
{
	const struct tcp_sack_block_wire *sack_a, *sack_b;
	u32 ack_seq_a = ntohl(tcph_a->ack_seq);
	u32 bytes_a = 0, bytes_b = 0;
	int oplen_a, oplen_b;
	bool first = true;

	sack_a = cake_get_tcpopt(tcph_a, TCPOPT_SACK, &oplen_a);
	sack_b = cake_get_tcpopt(tcph_b, TCPOPT_SACK, &oplen_b);

	/* pointers point to option contents */
	oplen_a -= TCPOLEN_SACK_BASE;
	oplen_b -= TCPOLEN_SACK_BASE;

	if (sack_a && oplen_a >= sizeof(*sack_a) &&
	    (!sack_b || oplen_b < sizeof(*sack_b)))
		return -1;
	else if (sack_b && oplen_b >= sizeof(*sack_b) &&
		 (!sack_a || oplen_a < sizeof(*sack_a)))
		return 1;
	else if ((!sack_a || oplen_a < sizeof(*sack_a)) &&
		 (!sack_b || oplen_b < sizeof(*sack_b)))
		return 0;

	while (oplen_a >= sizeof(*sack_a)) {
		const struct tcp_sack_block_wire *sack_tmp = sack_b;
		u32 start_a = get_unaligned_be32(&sack_a->start_seq);
		u32 end_a = get_unaligned_be32(&sack_a->end_seq);
		int oplen_tmp = oplen_b;
		bool found = false;

		/* DSACK; always considered greater to prevent dropping */
		if (before(start_a, ack_seq_a))
			return -1;

		bytes_a += end_a - start_a;

		while (oplen_tmp >= sizeof(*sack_tmp)) {
			u32 start_b = get_unaligned_be32(&sack_tmp->start_seq);
			u32 end_b = get_unaligned_be32(&sack_tmp->end_seq);

			/* first time through we count the total size */
			if (first)
				bytes_b += end_b - start_b;

			if (!after(start_b, start_a) && !before(end_b, end_a)) {
				found = true;
				if (!first)
					break;
			}
			oplen_tmp -= sizeof(*sack_tmp);
			sack_tmp++;
		}

		if (!found)
			return -1;

		oplen_a -= sizeof(*sack_a);
		sack_a++;
		first = false;
	}

	/* If we made it this far, all ranges SACKed by A are covered by B, so
	 * either the SACKs are equal, or B SACKs more bytes.
	 */
	return bytes_b > bytes_a ? 1 : 0;
}

static void cake_tcph_get_tstamp(const struct tcphdr *tcph,
				 u32 *tsval, u32 *tsecr)
{
	const u8 *ptr;
	int opsize;

	ptr = cake_get_tcpopt(tcph, TCPOPT_TIMESTAMP, &opsize);

	if (ptr && opsize == TCPOLEN_TIMESTAMP) {
		*tsval = get_unaligned_be32(ptr);
		*tsecr = get_unaligned_be32(ptr + 4);
	}
}

static bool cake_tcph_may_drop(const struct tcphdr *tcph,
			       u32 tstamp_new, u32 tsecr_new)
{
	/* inspired by tcp_parse_options in tcp_input.c */
	int length = __tcp_hdrlen(tcph) - sizeof(struct tcphdr);
	const u8 *ptr = (const u8 *)(tcph + 1);
	u32 tstamp, tsecr;

	/* 3 reserved flags must be unset to avoid future breakage
	 * ACK must be set
	 * ECE/CWR are handled separately
	 * All other flags URG/PSH/RST/SYN/FIN must be unset
	 * 0x0FFF0000 = all TCP flags (confirm ACK=1, others zero)
	 * 0x00C00000 = CWR/ECE (handled separately)
	 * 0x0F3F0000 = 0x0FFF0000 & ~0x00C00000
	 */
	if (((tcp_flag_word(tcph) &
	      cpu_to_be32(0x0F3F0000)) != TCP_FLAG_ACK))
		return false;

	while (length > 0) {
		int opcode = *ptr++;
		int opsize;

		if (opcode == TCPOPT_EOL)
			break;
		if (opcode == TCPOPT_NOP) {
			length--;
			continue;
		}
		if (length < 2)
			break;
		opsize = *ptr++;
		if (opsize < 2 || opsize > length)
			break;

		switch (opcode) {
		case TCPOPT_MD5SIG: /* doesn't influence state */
			break;

		case TCPOPT_SACK: /* stricter checking performed later */
			if (opsize % 8 != 2)
				return false;
			break;

		case TCPOPT_TIMESTAMP:
			/* only drop timestamps lower than new */
			if (opsize != TCPOLEN_TIMESTAMP)
				return false;
			tstamp = get_unaligned_be32(ptr);
			tsecr = get_unaligned_be32(ptr + 4);
			if (after(tstamp, tstamp_new) ||
			    after(tsecr, tsecr_new))
				return false;
			break;

		case TCPOPT_MSS:  /* these should only be set on SYN */
		case TCPOPT_WINDOW:
		case TCPOPT_SACK_PERM:
		case TCPOPT_FASTOPEN:
		case TCPOPT_EXP:
		default: /* don't drop if any unknown options are present */
			return false;
		}

		ptr += opsize - 2;
		length -= opsize;
	}

	return true;
}

static struct sk_buff *cake_ack_filter(struct cake_sched_data *q,
				       struct cake_flow *flow)
{
	bool aggressive = q->ack_filter == CAKE_ACK_AGGRESSIVE;
	struct sk_buff *elig_ack = NULL, *elig_ack_prev = NULL;
	struct sk_buff *skb_check, *skb_prev = NULL;
	const struct ipv6hdr *ipv6h, *ipv6h_check;
	unsigned char _tcph[64], _tcph_check[64];
	const struct tcphdr *tcph, *tcph_check;
	const struct iphdr *iph, *iph_check;
	struct ipv6hdr _iph, _iph_check;
	const struct sk_buff *skb;
	int seglen, num_found = 0;
	u32 tstamp = 0, tsecr = 0;
	__be32 elig_flags = 0;
	int sack_comp;

	/* no other possible ACKs to filter */
	if (flow->head == flow->tail)
		return NULL;

	skb = flow->tail;
	tcph = cake_get_tcphdr(skb, _tcph, sizeof(_tcph));
	iph = cake_get_iphdr(skb, &_iph);
	if (!tcph)
		return NULL;

	cake_tcph_get_tstamp(tcph, &tstamp, &tsecr);

	/* the 'triggering' packet need only have the ACK flag set.
	 * also check that SYN is not set, as there won't be any previous ACKs.
	 */
	if ((tcp_flag_word(tcph) &
	     (TCP_FLAG_ACK | TCP_FLAG_SYN)) != TCP_FLAG_ACK)
		return NULL;

	/* the 'triggering' ACK is at the tail of the queue, we have already
	 * returned if it is the only packet in the flow. loop through the rest
	 * of the queue looking for pure ACKs with the same 5-tuple as the
	 * triggering one.
	 */
	for (skb_check = flow->head;
	     skb_check && skb_check != skb;
	     skb_prev = skb_check, skb_check = skb_check->next) {
		iph_check = cake_get_iphdr(skb_check, &_iph_check);
		tcph_check = cake_get_tcphdr(skb_check, &_tcph_check,
					     sizeof(_tcph_check));

		/* only TCP packets with matching 5-tuple are eligible, and only
		 * drop safe headers
		 */
		if (!tcph_check || iph->version != iph_check->version ||
		    tcph_check->source != tcph->source ||
		    tcph_check->dest != tcph->dest)
			continue;

		if (iph_check->version == 4) {
			if (iph_check->saddr != iph->saddr ||
			    iph_check->daddr != iph->daddr)
				continue;

			seglen = iph_totlen(skb, iph_check) -
				       (4 * iph_check->ihl);
		} else if (iph_check->version == 6) {
			ipv6h = (struct ipv6hdr *)iph;
			ipv6h_check = (struct ipv6hdr *)iph_check;

			if (ipv6_addr_cmp(&ipv6h_check->saddr, &ipv6h->saddr) ||
			    ipv6_addr_cmp(&ipv6h_check->daddr, &ipv6h->daddr))
				continue;

			seglen = ntohs(ipv6h_check->payload_len);
		} else {
			WARN_ON(1);  /* shouldn't happen */
			continue;
		}

		/* If the ECE/CWR flags changed from the previous eligible
		 * packet in the same flow, we should no longer be dropping that
		 * previous packet as this would lose information.
		 */
		if (elig_ack && (tcp_flag_word(tcph_check) &
				 (TCP_FLAG_ECE | TCP_FLAG_CWR)) != elig_flags) {
			elig_ack = NULL;
			elig_ack_prev = NULL;
			num_found--;
		}

		/* Check TCP options and flags, don't drop ACKs with segment
		 * data, and don't drop ACKs with a higher cumulative ACK
		 * counter than the triggering packet. Check ACK seqno here to
		 * avoid parsing SACK options of packets we are going to exclude
		 * anyway.
		 */
		if (!cake_tcph_may_drop(tcph_check, tstamp, tsecr) ||
		    (seglen - __tcp_hdrlen(tcph_check)) != 0 ||
		    after(ntohl(tcph_check->ack_seq), ntohl(tcph->ack_seq)))
			continue;

		/* Check SACK options. The triggering packet must SACK more data
		 * than the ACK under consideration, or SACK the same range but
		 * have a larger cumulative ACK counter. The latter is a
		 * pathological case, but is contained in the following check
		 * anyway, just to be safe.
		 */
		sack_comp = cake_tcph_sack_compare(tcph_check, tcph);

		if (sack_comp < 0 ||
		    (ntohl(tcph_check->ack_seq) == ntohl(tcph->ack_seq) &&
		     sack_comp == 0))
			continue;

		/* At this point we have found an eligible pure ACK to drop; if
		 * we are in aggressive mode, we are done. Otherwise, keep
		 * searching unless this is the second eligible ACK we
		 * found.
		 *
		 * Since we want to drop ACK closest to the head of the queue,
		 * save the first eligible ACK we find, even if we need to loop
		 * again.
		 */
		if (!elig_ack) {
			elig_ack = skb_check;
			elig_ack_prev = skb_prev;
			elig_flags = (tcp_flag_word(tcph_check)
				      & (TCP_FLAG_ECE | TCP_FLAG_CWR));
		}

		if (num_found++ > 0)
			goto found;
	}

	/* We made it through the queue without finding two eligible ACKs . If
	 * we found a single eligible ACK we can drop it in aggressive mode if
	 * we can guarantee that this does not interfere with ECN flag
	 * information. We ensure this by dropping it only if the enqueued
	 * packet is consecutive with the eligible ACK, and their flags match.
	 */
	if (elig_ack && aggressive && elig_ack->next == skb &&
	    (elig_flags == (tcp_flag_word(tcph) &
			    (TCP_FLAG_ECE | TCP_FLAG_CWR))))
		goto found;

	return NULL;

found:
	if (elig_ack_prev)
		elig_ack_prev->next = elig_ack->next;
	else
		flow->head = elig_ack->next;

	skb_mark_not_on_list(elig_ack);

	return elig_ack;
}

static u64 cake_ewma(u64 avg, u64 sample, u32 shift)
{
	avg -= avg >> shift;
	avg += sample >> shift;
	return avg;
}

static u32 cake_calc_overhead(struct cake_sched_data *q, u32 len, u32 off)
{
	if (q->rate_flags & CAKE_FLAG_OVERHEAD)
		len -= off;

	if (q->max_netlen < len)
		q->max_netlen = len;
	if (q->min_netlen > len)
		q->min_netlen = len;

	len += q->rate_overhead;

	if (len < q->rate_mpu)
		len = q->rate_mpu;

	if (q->atm_mode == CAKE_ATM_ATM) {
		len += 47;
		len /= 48;
		len *= 53;
	} else if (q->atm_mode == CAKE_ATM_PTM) {
		/* Add one byte per 64 bytes or part thereof.
		 * This is conservative and easier to calculate than the
		 * precise value.
		 */
		len += (len + 63) / 64;
	}

	if (q->max_adjlen < len)
		q->max_adjlen = len;
	if (q->min_adjlen > len)
		q->min_adjlen = len;

	return len;
}

static u32 cake_overhead(struct cake_sched_data *q, const struct sk_buff *skb)
{
	const struct skb_shared_info *shinfo = skb_shinfo(skb);
	unsigned int hdr_len, last_len = 0;
	u32 off = skb_network_offset(skb);
	u32 len = qdisc_pkt_len(skb);
	u16 segs = 1;

	q->avg_netoff = cake_ewma(q->avg_netoff, off << 16, 8);

	if (!shinfo->gso_size)
		return cake_calc_overhead(q, len, off);

	/* borrowed from qdisc_pkt_len_init() */
	hdr_len = skb_transport_offset(skb);

	/* + transport layer */
	if (likely(shinfo->gso_type & (SKB_GSO_TCPV4 |
						SKB_GSO_TCPV6))) {
		const struct tcphdr *th;
		struct tcphdr _tcphdr;

		th = skb_header_pointer(skb, hdr_len,
					sizeof(_tcphdr), &_tcphdr);
		if (likely(th))
			hdr_len += __tcp_hdrlen(th);
	} else {
		struct udphdr _udphdr;

		if (skb_header_pointer(skb, hdr_len,
				       sizeof(_udphdr), &_udphdr))
			hdr_len += sizeof(struct udphdr);
	}

	if (unlikely(shinfo->gso_type & SKB_GSO_DODGY))
		segs = DIV_ROUND_UP(skb->len - hdr_len,
				    shinfo->gso_size);
	else
		segs = shinfo->gso_segs;

	len = shinfo->gso_size + hdr_len;
	last_len = skb->len - shinfo->gso_size * (segs - 1);

	return (cake_calc_overhead(q, len, off) * (segs - 1) +
		cake_calc_overhead(q, last_len, off));
}

static void cake_heap_swap(struct cake_sched_data *q, u16 i, u16 j)
{
	struct cake_heap_entry ii = q->overflow_heap[i];
	struct cake_heap_entry jj = q->overflow_heap[j];

	q->overflow_heap[i] = jj;
	q->overflow_heap[j] = ii;

	q->tins[ii.t].overflow_idx[ii.b] = j;
	q->tins[jj.t].overflow_idx[jj.b] = i;
}

static u32 cake_heap_get_backlog(const struct cake_sched_data *q, u16 i)
{
	struct cake_heap_entry ii = q->overflow_heap[i];

	return q->tins[ii.t].backlogs[ii.b];
}

static void cake_heapify(struct cake_sched_data *q, u16 i)
{
	static const u32 a = CAKE_MAX_TINS * CAKE_QUEUES;
	u32 mb = cake_heap_get_backlog(q, i);
	u32 m = i;

	while (m < a) {
		u32 l = m + m + 1;
		u32 r = l + 1;

		if (l < a) {
			u32 lb = cake_heap_get_backlog(q, l);

			if (lb > mb) {
				m  = l;
				mb = lb;
			}
		}

		if (r < a) {
			u32 rb = cake_heap_get_backlog(q, r);

			if (rb > mb) {
				m  = r;
				mb = rb;
			}
		}

		if (m != i) {
			cake_heap_swap(q, i, m);
			i = m;
		} else {
			break;
		}
	}
}

static void cake_heapify_up(struct cake_sched_data *q, u16 i)
{
	while (i > 0 && i < CAKE_MAX_TINS * CAKE_QUEUES) {
		u16 p = (i - 1) >> 1;
		u32 ib = cake_heap_get_backlog(q, i);
		u32 pb = cake_heap_get_backlog(q, p);

		if (ib > pb) {
			cake_heap_swap(q, i, p);
			i = p;
		} else {
			break;
		}
	}
}

static int cake_advance_shaper(struct cake_sched_data *q,
			       struct cake_tin_data *b,
			       struct sk_buff *skb,
			       ktime_t now, bool drop)
{
	u32 len = get_cobalt_cb(skb)->adjusted_len;

	/* charge packet bandwidth to this tin
	 * and to the global shaper.
	 */
	if (q->rate_ns) {
		u64 tin_dur = (len * b->tin_rate_ns) >> b->tin_rate_shft;
		u64 global_dur = (len * q->rate_ns) >> q->rate_shft;
		u64 failsafe_dur = global_dur + (global_dur >> 1);

		if (ktime_before(b->time_next_packet, now))
			b->time_next_packet = ktime_add_ns(b->time_next_packet,
							   tin_dur);

		else if (ktime_before(b->time_next_packet,
				      ktime_add_ns(now, tin_dur)))
			b->time_next_packet = ktime_add_ns(now, tin_dur);

		q->time_next_packet = ktime_add_ns(q->time_next_packet,
						   global_dur);
		if (!drop)
			q->failsafe_next_packet = \
				ktime_add_ns(q->failsafe_next_packet,
					     failsafe_dur);
	}
	return len;
}

static unsigned int cake_drop(struct Qdisc *sch, struct sk_buff **to_free)
{
	struct cake_sched_data *q = qdisc_priv(sch);
	ktime_t now = ktime_get();
	u32 idx = 0, tin = 0, len;
	struct cake_heap_entry qq;
	struct cake_tin_data *b;
	struct cake_flow *flow;
	struct sk_buff *skb;

	if (!q->overflow_timeout) {
		int i;
		/* Build fresh max-heap */
		for (i = CAKE_MAX_TINS * CAKE_QUEUES / 2 - 1; i >= 0; i--)
			cake_heapify(q, i);
	}
	q->overflow_timeout = 65535;

	/* select longest queue for pruning */
	qq  = q->overflow_heap[0];
	tin = qq.t;
	idx = qq.b;

	b = &q->tins[tin];
	flow = &b->flows[idx];
	skb = dequeue_head(flow);
	if (unlikely(!skb)) {
		/* heap has gone wrong, rebuild it next time */
		q->overflow_timeout = 0;
		return idx + (tin << 16);
	}

	if (cobalt_queue_full(&flow->cvars, &b->cparams, now))
		b->unresponsive_flow_count++;

	len = qdisc_pkt_len(skb);
	q->buffer_used      -= skb->truesize;
	b->backlogs[idx]    -= len;
	b->tin_backlog      -= len;
	sch->qstats.backlog -= len;

	flow->dropped++;
	b->tin_dropped++;

	if (q->rate_flags & CAKE_FLAG_INGRESS)
		cake_advance_shaper(q, b, skb, now, true);

	qdisc_drop_reason(skb, sch, to_free, SKB_DROP_REASON_QDISC_OVERLIMIT);
	sch->q.qlen--;
	qdisc_tree_reduce_backlog(sch, 1, len);

	cake_heapify(q, 0);

	return idx + (tin << 16);
}

static u8 cake_handle_diffserv(struct sk_buff *skb, bool wash)
{
	const int offset = skb_network_offset(skb);
	u16 *buf, buf_;
	u8 dscp;

	switch (skb_protocol(skb, true)) {
	case htons(ETH_P_IP):
		buf = skb_header_pointer(skb, offset, sizeof(buf_), &buf_);
		if (unlikely(!buf))
			return 0;

		/* ToS is in the second byte of iphdr */
		dscp = ipv4_get_dsfield((struct iphdr *)buf) >> 2;

		if (wash && dscp) {
			const int wlen = offset + sizeof(struct iphdr);

			if (!pskb_may_pull(skb, wlen) ||
			    skb_try_make_writable(skb, wlen))
				return 0;

			ipv4_change_dsfield(ip_hdr(skb), INET_ECN_MASK, 0);
		}

		return dscp;

	case htons(ETH_P_IPV6):
		buf = skb_header_pointer(skb, offset, sizeof(buf_), &buf_);
		if (unlikely(!buf))
			return 0;

		/* Traffic class is in the first and second bytes of ipv6hdr */
		dscp = ipv6_get_dsfield((struct ipv6hdr *)buf) >> 2;

		if (wash && dscp) {
			const int wlen = offset + sizeof(struct ipv6hdr);

			if (!pskb_may_pull(skb, wlen) ||
			    skb_try_make_writable(skb, wlen))
				return 0;

			ipv6_change_dsfield(ipv6_hdr(skb), INET_ECN_MASK, 0);
		}

		return dscp;

	case htons(ETH_P_ARP):
		return 0x38;  /* CS7 - Net Control */

	default:
		/* If there is no Diffserv field, treat as best-effort */
		return 0;
	}
}

static struct cake_tin_data *cake_select_tin(struct Qdisc *sch,
					     struct sk_buff *skb)
{
	struct cake_sched_data *q = qdisc_priv(sch);
	u32 tin, mark;
	bool wash;
	u8 dscp;

	/* Tin selection: Default to diffserv-based selection, allow overriding
	 * using firewall marks or skb->priority. Call DSCP parsing early if
	 * wash is enabled, otherwise defer to below to skip unneeded parsing.
	 */
	mark = (skb->mark & q->fwmark_mask) >> q->fwmark_shft;
	wash = !!(q->rate_flags & CAKE_FLAG_WASH);
	if (wash)
		dscp = cake_handle_diffserv(skb, wash);

	if (q->tin_mode == CAKE_DIFFSERV_BESTEFFORT)
		tin = 0;

	else if (mark && mark <= q->tin_cnt)
		tin = q->tin_order[mark - 1];

	else if (TC_H_MAJ(skb->priority) == sch->handle &&
		 TC_H_MIN(skb->priority) > 0 &&
		 TC_H_MIN(skb->priority) <= q->tin_cnt)
		tin = q->tin_order[TC_H_MIN(skb->priority) - 1];

	else {
		if (!wash)
			dscp = cake_handle_diffserv(skb, wash);
		tin = q->tin_index[dscp];

		if (unlikely(tin >= q->tin_cnt))
			tin = 0;
	}

	return &q->tins[tin];
}

static u32 cake_classify(struct Qdisc *sch, struct cake_tin_data **t,
			 struct sk_buff *skb, int flow_mode, int *qerr)
{
	struct cake_sched_data *q = qdisc_priv(sch);
	struct tcf_proto *filter;
	struct tcf_result res;
	u16 flow = 0, host = 0;
	int result;

	filter = rcu_dereference_bh(q->filter_list);
	if (!filter)
		goto hash;

	*qerr = NET_XMIT_SUCCESS | __NET_XMIT_BYPASS;
	result = tcf_classify(skb, NULL, filter, &res, false);

	if (result >= 0) {
#ifdef CONFIG_NET_CLS_ACT
		switch (result) {
		case TC_ACT_STOLEN:
		case TC_ACT_QUEUED:
		case TC_ACT_TRAP:
			*qerr = NET_XMIT_SUCCESS | __NET_XMIT_STOLEN;
			fallthrough;
		case TC_ACT_SHOT:
			return 0;
		}
#endif
		if (TC_H_MIN(res.classid) <= CAKE_QUEUES)
			flow = TC_H_MIN(res.classid);
		if (TC_H_MAJ(res.classid) <= (CAKE_QUEUES << 16))
			host = TC_H_MAJ(res.classid) >> 16;
	}
hash:
	*t = cake_select_tin(sch, skb);
	return cake_hash(*t, skb, flow_mode, flow, host) + 1;
}

static void cake_reconfigure(struct Qdisc *sch);

static s32 cake_enqueue(struct sk_buff *skb, struct Qdisc *sch,
			struct sk_buff **to_free)
{
	struct cake_sched_data *q = qdisc_priv(sch);
	int len = qdisc_pkt_len(skb);
	int ret;
	struct sk_buff *ack = NULL;
	ktime_t now = ktime_get();
	struct cake_tin_data *b;
	struct cake_flow *flow;
	u32 idx;

	/* choose flow to insert into */
	idx = cake_classify(sch, &b, skb, q->flow_mode, &ret);
	if (idx == 0) {
		if (ret & __NET_XMIT_BYPASS)
			qdisc_qstats_drop(sch);
		__qdisc_drop(skb, to_free);
		return ret;
	}
	idx--;
	flow = &b->flows[idx];

	/* ensure shaper state isn't stale */
	if (!b->tin_backlog) {
		if (ktime_before(b->time_next_packet, now))
			b->time_next_packet = now;

		if (!sch->q.qlen) {
			if (ktime_before(q->time_next_packet, now)) {
				q->failsafe_next_packet = now;
				q->time_next_packet = now;
			} else if (ktime_after(q->time_next_packet, now) &&
				   ktime_after(q->failsafe_next_packet, now)) {
				u64 next = \
					min(ktime_to_ns(q->time_next_packet),
					    ktime_to_ns(
						   q->failsafe_next_packet));
				sch->qstats.overlimits++;
				qdisc_watchdog_schedule_ns(&q->watchdog, next);
			}
		}
	}

	if (unlikely(len > b->max_skblen))
		b->max_skblen = len;

	if (skb_is_gso(skb) && q->rate_flags & CAKE_FLAG_SPLIT_GSO) {
		struct sk_buff *segs, *nskb;
		netdev_features_t features = netif_skb_features(skb);
		unsigned int slen = 0, numsegs = 0;

		segs = skb_gso_segment(skb, features & ~NETIF_F_GSO_MASK);
		if (IS_ERR_OR_NULL(segs))
			return qdisc_drop(skb, sch, to_free);

		skb_list_walk_safe(segs, segs, nskb) {
			skb_mark_not_on_list(segs);
			qdisc_skb_cb(segs)->pkt_len = segs->len;
			cobalt_set_enqueue_time(segs, now);
			get_cobalt_cb(segs)->adjusted_len = cake_overhead(q,
									  segs);
			flow_queue_add(flow, segs);

			sch->q.qlen++;
			numsegs++;
			slen += segs->len;
			q->buffer_used += segs->truesize;
			b->packets++;
		}

		/* stats */
		b->bytes	    += slen;
		b->backlogs[idx]    += slen;
		b->tin_backlog      += slen;
		sch->qstats.backlog += slen;
		q->avg_window_bytes += slen;

		qdisc_tree_reduce_backlog(sch, 1-numsegs, len-slen);
		consume_skb(skb);
	} else {
		/* not splitting */
		cobalt_set_enqueue_time(skb, now);
		get_cobalt_cb(skb)->adjusted_len = cake_overhead(q, skb);
		flow_queue_add(flow, skb);

		if (q->ack_filter)
			ack = cake_ack_filter(q, flow);

		if (ack) {
			b->ack_drops++;
			sch->qstats.drops++;
			b->bytes += qdisc_pkt_len(ack);
			len -= qdisc_pkt_len(ack);
			q->buffer_used += skb->truesize - ack->truesize;
			if (q->rate_flags & CAKE_FLAG_INGRESS)
				cake_advance_shaper(q, b, ack, now, true);

			qdisc_tree_reduce_backlog(sch, 1, qdisc_pkt_len(ack));
			consume_skb(ack);
		} else {
			sch->q.qlen++;
			q->buffer_used      += skb->truesize;
		}

		/* stats */
		b->packets++;
		b->bytes	    += len;
		b->backlogs[idx]    += len;
		b->tin_backlog      += len;
		sch->qstats.backlog += len;
		q->avg_window_bytes += len;
	}

	if (q->overflow_timeout)
		cake_heapify_up(q, b->overflow_idx[idx]);

	/* incoming bandwidth capacity estimate */
	if (q->rate_flags & CAKE_FLAG_AUTORATE_INGRESS) {
		u64 packet_interval = \
			ktime_to_ns(ktime_sub(now, q->last_packet_time));

		if (packet_interval > NSEC_PER_SEC)
			packet_interval = NSEC_PER_SEC;

		/* filter out short-term bursts, eg. wifi aggregation */
		q->avg_packet_interval = \
			cake_ewma(q->avg_packet_interval,
				  packet_interval,
				  (packet_interval > q->avg_packet_interval ?
					  2 : 8));

		q->last_packet_time = now;

		if (packet_interval > q->avg_packet_interval) {
			u64 window_interval = \
				ktime_to_ns(ktime_sub(now,
						      q->avg_window_begin));
			u64 b = q->avg_window_bytes * (u64)NSEC_PER_SEC;

			b = div64_u64(b, window_interval);
			q->avg_peak_bandwidth =
				cake_ewma(q->avg_peak_bandwidth, b,
					  b > q->avg_peak_bandwidth ? 2 : 8);
			q->avg_window_bytes = 0;
			q->avg_window_begin = now;

			if (ktime_after(now,
					ktime_add_ms(q->last_reconfig_time,
						     250))) {
				q->rate_bps = (q->avg_peak_bandwidth * 15) >> 4;
				cake_reconfigure(sch);
			}
		}
	} else {
		q->avg_window_bytes = 0;
		q->last_packet_time = now;
	}

	/* flowchain */
	if (!flow->set || flow->set == CAKE_SET_DECAYING) {
		struct cake_host *srchost = &b->hosts[flow->srchost];
		struct cake_host *dsthost = &b->hosts[flow->dsthost];
		u16 host_load = 1;

		if (!flow->set) {
			list_add_tail(&flow->flowchain, &b->new_flows);
		} else {
			b->decaying_flow_count--;
			list_move_tail(&flow->flowchain, &b->new_flows);
		}
		flow->set = CAKE_SET_SPARSE;
		b->sparse_flow_count++;

		if (cake_dsrc(q->flow_mode))
			host_load = max(host_load, srchost->srchost_bulk_flow_count);

		if (cake_ddst(q->flow_mode))
			host_load = max(host_load, dsthost->dsthost_bulk_flow_count);

		flow->deficit = (b->flow_quantum *
				 quantum_div[host_load]) >> 16;
	} else if (flow->set == CAKE_SET_SPARSE_WAIT) {
		struct cake_host *srchost = &b->hosts[flow->srchost];
		struct cake_host *dsthost = &b->hosts[flow->dsthost];

		/* this flow was empty, accounted as a sparse flow, but actually
		 * in the bulk rotation.
		 */
		flow->set = CAKE_SET_BULK;
		b->sparse_flow_count--;
		b->bulk_flow_count++;

		if (cake_dsrc(q->flow_mode))
			srchost->srchost_bulk_flow_count++;

		if (cake_ddst(q->flow_mode))
			dsthost->dsthost_bulk_flow_count++;

	}

	if (q->buffer_used > q->buffer_max_used)
		q->buffer_max_used = q->buffer_used;

	if (q->buffer_used > q->buffer_limit) {
		u32 dropped = 0;

		while (q->buffer_used > q->buffer_limit) {
			dropped++;
			cake_drop(sch, to_free);
		}
		b->drop_overlimit += dropped;
	}
	return NET_XMIT_SUCCESS;
}

static struct sk_buff *cake_dequeue_one(struct Qdisc *sch)
{
	struct cake_sched_data *q = qdisc_priv(sch);
	struct cake_tin_data *b = &q->tins[q->cur_tin];
	struct cake_flow *flow = &b->flows[q->cur_flow];
	struct sk_buff *skb = NULL;
	u32 len;

	if (flow->head) {
		skb = dequeue_head(flow);
		len = qdisc_pkt_len(skb);
		b->backlogs[q->cur_flow] -= len;
		b->tin_backlog		 -= len;
		sch->qstats.backlog      -= len;
		q->buffer_used		 -= skb->truesize;
		sch->q.qlen--;

		if (q->overflow_timeout)
			cake_heapify(q, b->overflow_idx[q->cur_flow]);
	}
	return skb;
}

/* Discard leftover packets from a tin no longer in use. */
static void cake_clear_tin(struct Qdisc *sch, u16 tin)
{
	struct cake_sched_data *q = qdisc_priv(sch);
	struct sk_buff *skb;

	q->cur_tin = tin;
	for (q->cur_flow = 0; q->cur_flow < CAKE_QUEUES; q->cur_flow++)
		while (!!(skb = cake_dequeue_one(sch)))
			kfree_skb_reason(skb, SKB_DROP_REASON_QUEUE_PURGE);
}

static struct sk_buff *cake_dequeue(struct Qdisc *sch)
{
	struct cake_sched_data *q = qdisc_priv(sch);
	struct cake_tin_data *b = &q->tins[q->cur_tin];
	struct cake_host *srchost, *dsthost;
	enum skb_drop_reason reason;
	ktime_t now = ktime_get();
	struct cake_flow *flow;
	struct list_head *head;
	bool first_flow = true;
	struct sk_buff *skb;
	u16 host_load;
	u64 delay;
	u32 len;

begin:
	if (!sch->q.qlen)
		return NULL;

	/* global hard shaper */
	if (ktime_after(q->time_next_packet, now) &&
	    ktime_after(q->failsafe_next_packet, now)) {
		u64 next = min(ktime_to_ns(q->time_next_packet),
			       ktime_to_ns(q->failsafe_next_packet));

		sch->qstats.overlimits++;
		qdisc_watchdog_schedule_ns(&q->watchdog, next);
		return NULL;
	}

	/* Choose a class to work on. */
	if (!q->rate_ns) {
		/* In unlimited mode, can't rely on shaper timings, just balance
		 * with DRR
		 */
		bool wrapped = false, empty = true;

		while (b->tin_deficit < 0 ||
		       !(b->sparse_flow_count + b->bulk_flow_count)) {
			if (b->tin_deficit <= 0)
				b->tin_deficit += b->tin_quantum;
			if (b->sparse_flow_count + b->bulk_flow_count)
				empty = false;

			q->cur_tin++;
			b++;
			if (q->cur_tin >= q->tin_cnt) {
				q->cur_tin = 0;
				b = q->tins;

				if (wrapped) {
					/* It's possible for q->qlen to be
					 * nonzero when we actually have no
					 * packets anywhere.
					 */
					if (empty)
						return NULL;
				} else {
					wrapped = true;
				}
			}
		}
	} else {
		/* In shaped mode, choose:
		 * - Highest-priority tin with queue and meeting schedule, or
		 * - The earliest-scheduled tin with queue.
		 */
		ktime_t best_time = KTIME_MAX;
		int tin, best_tin = 0;

		for (tin = 0; tin < q->tin_cnt; tin++) {
			b = q->tins + tin;
			if ((b->sparse_flow_count + b->bulk_flow_count) > 0) {
				ktime_t time_to_pkt = \
					ktime_sub(b->time_next_packet, now);

				if (ktime_to_ns(time_to_pkt) <= 0 ||
				    ktime_compare(time_to_pkt,
						  best_time) <= 0) {
					best_time = time_to_pkt;
					best_tin = tin;
				}
			}
		}

		q->cur_tin = best_tin;
		b = q->tins + best_tin;

		/* No point in going further if no packets to deliver. */
		if (unlikely(!(b->sparse_flow_count + b->bulk_flow_count)))
			return NULL;
	}

retry:
	/* service this class */
	head = &b->decaying_flows;
	if (!first_flow || list_empty(head)) {
		head = &b->new_flows;
		if (list_empty(head)) {
			head = &b->old_flows;
			if (unlikely(list_empty(head))) {
				head = &b->decaying_flows;
				if (unlikely(list_empty(head)))
					goto begin;
			}
		}
	}
	flow = list_first_entry(head, struct cake_flow, flowchain);
	q->cur_flow = flow - b->flows;
	first_flow = false;

	/* triple isolation (modified DRR++) */
	srchost = &b->hosts[flow->srchost];
	dsthost = &b->hosts[flow->dsthost];
	host_load = 1;

	/* flow isolation (DRR++) */
	if (flow->deficit <= 0) {
		/* Keep all flows with deficits out of the sparse and decaying
		 * rotations.  No non-empty flow can go into the decaying
		 * rotation, so they can't get deficits
		 */
		if (flow->set == CAKE_SET_SPARSE) {
			if (flow->head) {
				b->sparse_flow_count--;
				b->bulk_flow_count++;

				if (cake_dsrc(q->flow_mode))
					srchost->srchost_bulk_flow_count++;

				if (cake_ddst(q->flow_mode))
					dsthost->dsthost_bulk_flow_count++;

				flow->set = CAKE_SET_BULK;
			} else {
				/* we've moved it to the bulk rotation for
				 * correct deficit accounting but we still want
				 * to count it as a sparse flow, not a bulk one.
				 */
				flow->set = CAKE_SET_SPARSE_WAIT;
			}
		}

		if (cake_dsrc(q->flow_mode))
			host_load = max(host_load, srchost->srchost_bulk_flow_count);

		if (cake_ddst(q->flow_mode))
			host_load = max(host_load, dsthost->dsthost_bulk_flow_count);

		WARN_ON(host_load > CAKE_QUEUES);

		/* The get_random_u16() is a way to apply dithering to avoid
		 * accumulating roundoff errors
		 */
		flow->deficit += (b->flow_quantum * quantum_div[host_load] +
				  get_random_u16()) >> 16;
		list_move_tail(&flow->flowchain, &b->old_flows);

		goto retry;
	}

	/* Retrieve a packet via the AQM */
	while (1) {
		skb = cake_dequeue_one(sch);
		if (!skb) {
			/* this queue was actually empty */
			if (cobalt_queue_empty(&flow->cvars, &b->cparams, now))
				b->unresponsive_flow_count--;

			if (flow->cvars.p_drop || flow->cvars.count ||
			    ktime_before(now, flow->cvars.drop_next)) {
				/* keep in the flowchain until the state has
				 * decayed to rest
				 */
				list_move_tail(&flow->flowchain,
					       &b->decaying_flows);
				if (flow->set == CAKE_SET_BULK) {
					b->bulk_flow_count--;

					if (cake_dsrc(q->flow_mode))
						srchost->srchost_bulk_flow_count--;

					if (cake_ddst(q->flow_mode))
						dsthost->dsthost_bulk_flow_count--;

					b->decaying_flow_count++;
				} else if (flow->set == CAKE_SET_SPARSE ||
					   flow->set == CAKE_SET_SPARSE_WAIT) {
					b->sparse_flow_count--;
					b->decaying_flow_count++;
				}
				flow->set = CAKE_SET_DECAYING;
			} else {
				/* remove empty queue from the flowchain */
				list_del_init(&flow->flowchain);
				if (flow->set == CAKE_SET_SPARSE ||
				    flow->set == CAKE_SET_SPARSE_WAIT)
					b->sparse_flow_count--;
				else if (flow->set == CAKE_SET_BULK) {
					b->bulk_flow_count--;

					if (cake_dsrc(q->flow_mode))
						srchost->srchost_bulk_flow_count--;

					if (cake_ddst(q->flow_mode))
						dsthost->dsthost_bulk_flow_count--;

				} else
					b->decaying_flow_count--;

				flow->set = CAKE_SET_NONE;
			}
			goto begin;
		}

		reason = cobalt_should_drop(&flow->cvars, &b->cparams, now, skb,
					    (b->bulk_flow_count *
					     !!(q->rate_flags &
						CAKE_FLAG_INGRESS)));
		/* Last packet in queue may be marked, shouldn't be dropped */
		if (reason == SKB_NOT_DROPPED_YET || !flow->head)
			break;

		/* drop this packet, get another one */
		if (q->rate_flags & CAKE_FLAG_INGRESS) {
			len = cake_advance_shaper(q, b, skb,
						  now, true);
			flow->deficit -= len;
			b->tin_deficit -= len;
		}
		flow->dropped++;
		b->tin_dropped++;
		qdisc_tree_reduce_backlog(sch, 1, qdisc_pkt_len(skb));
		qdisc_qstats_drop(sch);
		kfree_skb_reason(skb, reason);
		if (q->rate_flags & CAKE_FLAG_INGRESS)
			goto retry;
	}

	b->tin_ecn_mark += !!flow->cvars.ecn_marked;
	qdisc_bstats_update(sch, skb);

	/* collect delay stats */
	delay = ktime_to_ns(ktime_sub(now, cobalt_get_enqueue_time(skb)));
	b->avge_delay = cake_ewma(b->avge_delay, delay, 8);
	b->peak_delay = cake_ewma(b->peak_delay, delay,
				  delay > b->peak_delay ? 2 : 8);
	b->base_delay = cake_ewma(b->base_delay, delay,
				  delay < b->base_delay ? 2 : 8);

	len = cake_advance_shaper(q, b, skb, now, false);
	flow->deficit -= len;
	b->tin_deficit -= len;

	if (ktime_after(q->time_next_packet, now) && sch->q.qlen) {
		u64 next = min(ktime_to_ns(q->time_next_packet),
			       ktime_to_ns(q->failsafe_next_packet));

		qdisc_watchdog_schedule_ns(&q->watchdog, next);
	} else if (!sch->q.qlen) {
		int i;

		for (i = 0; i < q->tin_cnt; i++) {
			if (q->tins[i].decaying_flow_count) {
				ktime_t next = \
					ktime_add_ns(now,
						     q->tins[i].cparams.target);

				qdisc_watchdog_schedule_ns(&q->watchdog,
							   ktime_to_ns(next));
				break;
			}
		}
	}

	if (q->overflow_timeout)
		q->overflow_timeout--;

	return skb;
}

static void cake_reset(struct Qdisc *sch)
{
	struct cake_sched_data *q = qdisc_priv(sch);
	u32 c;

	if (!q->tins)
		return;

	for (c = 0; c < CAKE_MAX_TINS; c++)
		cake_clear_tin(sch, c);
}

static const struct nla_policy cake_policy[TCA_CAKE_MAX + 1] = {
	[TCA_CAKE_BASE_RATE64]   = { .type = NLA_U64 },
	[TCA_CAKE_DIFFSERV_MODE] = { .type = NLA_U32 },
	[TCA_CAKE_ATM]		 = { .type = NLA_U32 },
	[TCA_CAKE_FLOW_MODE]     = { .type = NLA_U32 },
	[TCA_CAKE_OVERHEAD]      = { .type = NLA_S32 },
	[TCA_CAKE_RTT]		 = { .type = NLA_U32 },
	[TCA_CAKE_TARGET]	 = { .type = NLA_U32 },
	[TCA_CAKE_AUTORATE]      = { .type = NLA_U32 },
	[TCA_CAKE_MEMORY]	 = { .type = NLA_U32 },
	[TCA_CAKE_NAT]		 = { .type = NLA_U32 },
	[TCA_CAKE_RAW]		 = { .type = NLA_U32 },
	[TCA_CAKE_WASH]		 = { .type = NLA_U32 },
	[TCA_CAKE_MPU]		 = { .type = NLA_U32 },
	[TCA_CAKE_INGRESS]	 = { .type = NLA_U32 },
	[TCA_CAKE_ACK_FILTER]	 = { .type = NLA_U32 },
	[TCA_CAKE_SPLIT_GSO]	 = { .type = NLA_U32 },
	[TCA_CAKE_FWMARK]	 = { .type = NLA_U32 },
};

static void cake_set_rate(struct cake_tin_data *b, u64 rate, u32 mtu,
			  u64 target_ns, u64 rtt_est_ns)
{
	/* convert byte-rate into time-per-byte
	 * so it will always unwedge in reasonable time.
	 */
	static const u64 MIN_RATE = 64;
	u32 byte_target = mtu;
	u64 byte_target_ns;
	u8  rate_shft = 0;
	u64 rate_ns = 0;

	b->flow_quantum = 1514;
	if (rate) {
		b->flow_quantum = max(min(rate >> 12, 1514ULL), 300ULL);
		rate_shft = 34;
		rate_ns = ((u64)NSEC_PER_SEC) << rate_shft;
		rate_ns = div64_u64(rate_ns, max(MIN_RATE, rate));
		while (!!(rate_ns >> 34)) {
			rate_ns >>= 1;
			rate_shft--;
		}
	} /* else unlimited, ie. zero delay */

	b->tin_rate_bps  = rate;
	b->tin_rate_ns   = rate_ns;
	b->tin_rate_shft = rate_shft;

	byte_target_ns = (byte_target * rate_ns) >> rate_shft;

	b->cparams.target = max((byte_target_ns * 3) / 2, target_ns);
	b->cparams.interval = max(rtt_est_ns +
				     b->cparams.target - target_ns,
				     b->cparams.target * 2);
	b->cparams.mtu_time = byte_target_ns;
	b->cparams.p_inc = 1 << 24; /* 1/256 */
	b->cparams.p_dec = 1 << 20; /* 1/4096 */
}

static int cake_config_besteffort(struct Qdisc *sch)
{
	struct cake_sched_data *q = qdisc_priv(sch);
	struct cake_tin_data *b = &q->tins[0];
	u32 mtu = psched_mtu(qdisc_dev(sch));
	u64 rate = q->rate_bps;

	q->tin_cnt = 1;

	q->tin_index = besteffort;
	q->tin_order = normal_order;

	cake_set_rate(b, rate, mtu,
		      us_to_ns(q->target), us_to_ns(q->interval));
	b->tin_quantum = 65535;

	return 0;
}

static int cake_config_precedence(struct Qdisc *sch)
{
	/* convert high-level (user visible) parameters into internal format */
	struct cake_sched_data *q = qdisc_priv(sch);
	u32 mtu = psched_mtu(qdisc_dev(sch));
	u64 rate = q->rate_bps;
	u32 quantum = 256;
	u32 i;

	q->tin_cnt = 8;
	q->tin_index = precedence;
	q->tin_order = normal_order;

	for (i = 0; i < q->tin_cnt; i++) {
		struct cake_tin_data *b = &q->tins[i];

		cake_set_rate(b, rate, mtu, us_to_ns(q->target),
			      us_to_ns(q->interval));

		b->tin_quantum = max_t(u16, 1U, quantum);

		/* calculate next class's parameters */
		rate  *= 7;
		rate >>= 3;

		quantum  *= 7;
		quantum >>= 3;
	}

	return 0;
}

/*	List of known Diffserv codepoints:
 *
 *	Default Forwarding (DF/CS0) - Best Effort
 *	Max Throughput (TOS2)
 *	Min Delay (TOS4)
 *	LLT "La" (TOS5)
 *	Assured Forwarding 1 (AF1x) - x3
 *	Assured Forwarding 2 (AF2x) - x3
 *	Assured Forwarding 3 (AF3x) - x3
 *	Assured Forwarding 4 (AF4x) - x3
 *	Precedence Class 1 (CS1)
 *	Precedence Class 2 (CS2)
 *	Precedence Class 3 (CS3)
 *	Precedence Class 4 (CS4)
 *	Precedence Class 5 (CS5)
 *	Precedence Class 6 (CS6)
 *	Precedence Class 7 (CS7)
 *	Voice Admit (VA)
 *	Expedited Forwarding (EF)
 *	Lower Effort (LE)
 *
 *	Total 26 codepoints.
 */

/*	List of traffic classes in RFC 4594, updated by RFC 8622:
 *		(roughly descending order of contended priority)
 *		(roughly ascending order of uncontended throughput)
 *
 *	Network Control (CS6,CS7)      - routing traffic
 *	Telephony (EF,VA)         - aka. VoIP streams
 *	Signalling (CS5)               - VoIP setup
 *	Multimedia Conferencing (AF4x) - aka. video calls
 *	Realtime Interactive (CS4)     - eg. games
 *	Multimedia Streaming (AF3x)    - eg. YouTube, NetFlix, Twitch
 *	Broadcast Video (CS3)
 *	Low-Latency Data (AF2x,TOS4)      - eg. database
 *	Ops, Admin, Management (CS2)      - eg. ssh
 *	Standard Service (DF & unrecognised codepoints)
 *	High-Throughput Data (AF1x,TOS2)  - eg. web traffic
 *	Low-Priority Data (LE,CS1)        - eg. BitTorrent
 *
 *	Total 12 traffic classes.
 */

static int cake_config_diffserv8(struct Qdisc *sch)
{
/*	Pruned list of traffic classes for typical applications:
 *
 *		Network Control          (CS6, CS7)
 *		Minimum Latency          (EF, VA, CS5, CS4)
 *		Interactive Shell        (CS2)
 *		Low Latency Transactions (AF2x, TOS4)
 *		Video Streaming          (AF4x, AF3x, CS3)
 *		Bog Standard             (DF etc.)
 *		High Throughput          (AF1x, TOS2, CS1)
 *		Background Traffic       (LE)
 *
 *		Total 8 traffic classes.
 */

	struct cake_sched_data *q = qdisc_priv(sch);
	u32 mtu = psched_mtu(qdisc_dev(sch));
	u64 rate = q->rate_bps;
	u32 quantum = 256;
	u32 i;

	q->tin_cnt = 8;

	/* codepoint to class mapping */
	q->tin_index = diffserv8;
	q->tin_order = normal_order;

	/* class characteristics */
	for (i = 0; i < q->tin_cnt; i++) {
		struct cake_tin_data *b = &q->tins[i];

		cake_set_rate(b, rate, mtu, us_to_ns(q->target),
			      us_to_ns(q->interval));

		b->tin_quantum = max_t(u16, 1U, quantum);

		/* calculate next class's parameters */
		rate  *= 7;
		rate >>= 3;

		quantum  *= 7;
		quantum >>= 3;
	}

	return 0;
}

static int cake_config_diffserv4(struct Qdisc *sch)
{
/*  Further pruned list of traffic classes for four-class system:
 *
 *	    Latency Sensitive  (CS7, CS6, EF, VA, CS5, CS4)
 *	    Streaming Media    (AF4x, AF3x, CS3, AF2x, TOS4, CS2)
 *	    Best Effort        (DF, AF1x, TOS2, and those not specified)
 *	    Background Traffic (LE, CS1)
 *
 *		Total 4 traffic classes.
 */

	struct cake_sched_data *q = qdisc_priv(sch);
	u32 mtu = psched_mtu(qdisc_dev(sch));
	u64 rate = q->rate_bps;
	u32 quantum = 1024;

	q->tin_cnt = 4;

	/* codepoint to class mapping */
	q->tin_index = diffserv4;
	q->tin_order = bulk_order;

	/* class characteristics */
	cake_set_rate(&q->tins[0], rate, mtu,
		      us_to_ns(q->target), us_to_ns(q->interval));
	cake_set_rate(&q->tins[1], rate >> 4, mtu,
		      us_to_ns(q->target), us_to_ns(q->interval));
	cake_set_rate(&q->tins[2], rate >> 1, mtu,
		      us_to_ns(q->target), us_to_ns(q->interval));
	cake_set_rate(&q->tins[3], rate >> 2, mtu,
		      us_to_ns(q->target), us_to_ns(q->interval));

	/* bandwidth-sharing weights */
	q->tins[0].tin_quantum = quantum;
	q->tins[1].tin_quantum = quantum >> 4;
	q->tins[2].tin_quantum = quantum >> 1;
	q->tins[3].tin_quantum = quantum >> 2;

	return 0;
}

static int cake_config_diffserv3(struct Qdisc *sch)
{
/*  Simplified Diffserv structure with 3 tins.
 *		Latency Sensitive	(CS7, CS6, EF, VA, TOS4)
 *		Best Effort
 *		Low Priority		(LE, CS1)
 */
	struct cake_sched_data *q = qdisc_priv(sch);
	u32 mtu = psched_mtu(qdisc_dev(sch));
	u64 rate = q->rate_bps;
	u32 quantum = 1024;

	q->tin_cnt = 3;

	/* codepoint to class mapping */
	q->tin_index = diffserv3;
	q->tin_order = bulk_order;

	/* class characteristics */
	cake_set_rate(&q->tins[0], rate, mtu,
		      us_to_ns(q->target), us_to_ns(q->interval));
	cake_set_rate(&q->tins[1], rate >> 4, mtu,
		      us_to_ns(q->target), us_to_ns(q->interval));
	cake_set_rate(&q->tins[2], rate >> 2, mtu,
		      us_to_ns(q->target), us_to_ns(q->interval));

	/* bandwidth-sharing weights */
	q->tins[0].tin_quantum = quantum;
	q->tins[1].tin_quantum = quantum >> 4;
	q->tins[2].tin_quantum = quantum >> 2;

	return 0;
}

static void cake_reconfigure(struct Qdisc *sch)
{
	struct cake_sched_data *q = qdisc_priv(sch);
	int c, ft;

	switch (q->tin_mode) {
	case CAKE_DIFFSERV_BESTEFFORT:
		ft = cake_config_besteffort(sch);
		break;

	case CAKE_DIFFSERV_PRECEDENCE:
		ft = cake_config_precedence(sch);
		break;

	case CAKE_DIFFSERV_DIFFSERV8:
		ft = cake_config_diffserv8(sch);
		break;

	case CAKE_DIFFSERV_DIFFSERV4:
		ft = cake_config_diffserv4(sch);
		break;

	case CAKE_DIFFSERV_DIFFSERV3:
	default:
		ft = cake_config_diffserv3(sch);
		break;
	}

	for (c = q->tin_cnt; c < CAKE_MAX_TINS; c++) {
		cake_clear_tin(sch, c);
		q->tins[c].cparams.mtu_time = q->tins[ft].cparams.mtu_time;
	}

	q->rate_ns   = q->tins[ft].tin_rate_ns;
	q->rate_shft = q->tins[ft].tin_rate_shft;

	if (q->buffer_config_limit) {
		q->buffer_limit = q->buffer_config_limit;
	} else if (q->rate_bps) {
		u64 t = q->rate_bps * q->interval;

		do_div(t, USEC_PER_SEC / 4);
		q->buffer_limit = max_t(u32, t, 4U << 20);
	} else {
		q->buffer_limit = ~0;
	}

	sch->flags &= ~TCQ_F_CAN_BYPASS;

	q->buffer_limit = min(q->buffer_limit,
			      max(sch->limit * psched_mtu(qdisc_dev(sch)),
				  q->buffer_config_limit));
}

static int cake_change(struct Qdisc *sch, struct nlattr *opt,
		       struct netlink_ext_ack *extack)
{
	struct cake_sched_data *q = qdisc_priv(sch);
	struct nlattr *tb[TCA_CAKE_MAX + 1];
	u16 rate_flags;
	u8 flow_mode;
	int err;

	err = nla_parse_nested_deprecated(tb, TCA_CAKE_MAX, opt, cake_policy,
					  extack);
	if (err < 0)
		return err;

	flow_mode = q->flow_mode;
	if (tb[TCA_CAKE_NAT]) {
#if IS_ENABLED(CONFIG_NF_CONNTRACK)
		flow_mode &= ~CAKE_FLOW_NAT_FLAG;
		flow_mode |= CAKE_FLOW_NAT_FLAG *
			!!nla_get_u32(tb[TCA_CAKE_NAT]);
#else
		NL_SET_ERR_MSG_ATTR(extack, tb[TCA_CAKE_NAT],
				    "No conntrack support in kernel");
		return -EOPNOTSUPP;
#endif
	}

	if (tb[TCA_CAKE_BASE_RATE64])
		WRITE_ONCE(q->rate_bps,
			   nla_get_u64(tb[TCA_CAKE_BASE_RATE64]));

	if (tb[TCA_CAKE_DIFFSERV_MODE])
		WRITE_ONCE(q->tin_mode,
			   nla_get_u32(tb[TCA_CAKE_DIFFSERV_MODE]));

	rate_flags = q->rate_flags;
	if (tb[TCA_CAKE_WASH]) {
		if (!!nla_get_u32(tb[TCA_CAKE_WASH]))
			rate_flags |= CAKE_FLAG_WASH;
		else
			rate_flags &= ~CAKE_FLAG_WASH;
	}

	if (tb[TCA_CAKE_FLOW_MODE])
		flow_mode = ((flow_mode & CAKE_FLOW_NAT_FLAG) |
				(nla_get_u32(tb[TCA_CAKE_FLOW_MODE]) &
					CAKE_FLOW_MASK));

	if (tb[TCA_CAKE_ATM])
		WRITE_ONCE(q->atm_mode,
			   nla_get_u32(tb[TCA_CAKE_ATM]));

	if (tb[TCA_CAKE_OVERHEAD]) {
		WRITE_ONCE(q->rate_overhead,
			   nla_get_s32(tb[TCA_CAKE_OVERHEAD]));
		rate_flags |= CAKE_FLAG_OVERHEAD;

		q->max_netlen = 0;
		q->max_adjlen = 0;
		q->min_netlen = ~0;
		q->min_adjlen = ~0;
	}

	if (tb[TCA_CAKE_RAW]) {
		rate_flags &= ~CAKE_FLAG_OVERHEAD;

		q->max_netlen = 0;
		q->max_adjlen = 0;
		q->min_netlen = ~0;
		q->min_adjlen = ~0;
	}

	if (tb[TCA_CAKE_MPU])
		WRITE_ONCE(q->rate_mpu,
			   nla_get_u32(tb[TCA_CAKE_MPU]));

	if (tb[TCA_CAKE_RTT]) {
		u32 interval = nla_get_u32(tb[TCA_CAKE_RTT]);

		WRITE_ONCE(q->interval, max(interval, 1U));
	}

	if (tb[TCA_CAKE_TARGET]) {
		u32 target = nla_get_u32(tb[TCA_CAKE_TARGET]);

		WRITE_ONCE(q->target, max(target, 1U));
	}

	if (tb[TCA_CAKE_AUTORATE]) {
		if (!!nla_get_u32(tb[TCA_CAKE_AUTORATE]))
			rate_flags |= CAKE_FLAG_AUTORATE_INGRESS;
		else
			rate_flags &= ~CAKE_FLAG_AUTORATE_INGRESS;
	}

	if (tb[TCA_CAKE_INGRESS]) {
		if (!!nla_get_u32(tb[TCA_CAKE_INGRESS]))
			rate_flags |= CAKE_FLAG_INGRESS;
		else
			rate_flags &= ~CAKE_FLAG_INGRESS;
	}

	if (tb[TCA_CAKE_ACK_FILTER])
		WRITE_ONCE(q->ack_filter,
			   nla_get_u32(tb[TCA_CAKE_ACK_FILTER]));

	if (tb[TCA_CAKE_MEMORY])
		WRITE_ONCE(q->buffer_config_limit,
			   nla_get_u32(tb[TCA_CAKE_MEMORY]));

	if (tb[TCA_CAKE_SPLIT_GSO]) {
		if (!!nla_get_u32(tb[TCA_CAKE_SPLIT_GSO]))
			rate_flags |= CAKE_FLAG_SPLIT_GSO;
		else
			rate_flags &= ~CAKE_FLAG_SPLIT_GSO;
	}

	if (tb[TCA_CAKE_FWMARK]) {
		WRITE_ONCE(q->fwmark_mask, nla_get_u32(tb[TCA_CAKE_FWMARK]));
		WRITE_ONCE(q->fwmark_shft,
			   q->fwmark_mask ? __ffs(q->fwmark_mask) : 0);
	}

	WRITE_ONCE(q->rate_flags, rate_flags);
	WRITE_ONCE(q->flow_mode, flow_mode);
	if (q->tins) {
		sch_tree_lock(sch);
		cake_reconfigure(sch);
		sch_tree_unlock(sch);
	}

	return 0;
}

static void cake_destroy(struct Qdisc *sch)
{
	struct cake_sched_data *q = qdisc_priv(sch);

	qdisc_watchdog_cancel(&q->watchdog);
	tcf_block_put(q->block);
	kvfree(q->tins);
}

static int cake_init(struct Qdisc *sch, struct nlattr *opt,
		     struct netlink_ext_ack *extack)
{
	struct cake_sched_data *q = qdisc_priv(sch);
	int i, j, err;

	sch->limit = 10240;
	q->tin_mode = CAKE_DIFFSERV_DIFFSERV3;
	q->flow_mode  = CAKE_FLOW_TRIPLE;

	q->rate_bps = 0; /* unlimited by default */

	q->interval = 100000; /* 100ms default */
	q->target   =   5000; /* 5ms: codel RFC argues
			       * for 5 to 10% of interval
			       */
	q->rate_flags |= CAKE_FLAG_SPLIT_GSO;
	q->cur_tin = 0;
	q->cur_flow  = 0;

	qdisc_watchdog_init(&q->watchdog, sch);

	if (opt) {
		err = cake_change(sch, opt, extack);

		if (err)
			return err;
	}

	err = tcf_block_get(&q->block, &q->filter_list, sch, extack);
	if (err)
		return err;

	quantum_div[0] = ~0;
	for (i = 1; i <= CAKE_QUEUES; i++)
		quantum_div[i] = 65535 / i;

	q->tins = kvcalloc(CAKE_MAX_TINS, sizeof(struct cake_tin_data),
			   GFP_KERNEL);
	if (!q->tins)
		return -ENOMEM;

	for (i = 0; i < CAKE_MAX_TINS; i++) {
		struct cake_tin_data *b = q->tins + i;

		INIT_LIST_HEAD(&b->new_flows);
		INIT_LIST_HEAD(&b->old_flows);
		INIT_LIST_HEAD(&b->decaying_flows);
		b->sparse_flow_count = 0;
		b->bulk_flow_count = 0;
		b->decaying_flow_count = 0;

		for (j = 0; j < CAKE_QUEUES; j++) {
			struct cake_flow *flow = b->flows + j;
			u32 k = j * CAKE_MAX_TINS + i;

			INIT_LIST_HEAD(&flow->flowchain);
			cobalt_vars_init(&flow->cvars);

			q->overflow_heap[k].t = i;
			q->overflow_heap[k].b = j;
			b->overflow_idx[j] = k;
		}
	}

	cake_reconfigure(sch);
	q->avg_peak_bandwidth = q->rate_bps;
	q->min_netlen = ~0;
	q->min_adjlen = ~0;
	return 0;
}

static int cake_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct cake_sched_data *q = qdisc_priv(sch);
	struct nlattr *opts;
	u16 rate_flags;
	u8 flow_mode;

	opts = nla_nest_start_noflag(skb, TCA_OPTIONS);
	if (!opts)
		goto nla_put_failure;

	if (nla_put_u64_64bit(skb, TCA_CAKE_BASE_RATE64,
			      READ_ONCE(q->rate_bps), TCA_CAKE_PAD))
		goto nla_put_failure;

	flow_mode = READ_ONCE(q->flow_mode);
	if (nla_put_u32(skb, TCA_CAKE_FLOW_MODE, flow_mode & CAKE_FLOW_MASK))
		goto nla_put_failure;

	if (nla_put_u32(skb, TCA_CAKE_RTT, READ_ONCE(q->interval)))
		goto nla_put_failure;

	if (nla_put_u32(skb, TCA_CAKE_TARGET, READ_ONCE(q->target)))
		goto nla_put_failure;

	if (nla_put_u32(skb, TCA_CAKE_MEMORY,
			READ_ONCE(q->buffer_config_limit)))
		goto nla_put_failure;

	rate_flags = READ_ONCE(q->rate_flags);
	if (nla_put_u32(skb, TCA_CAKE_AUTORATE,
			!!(rate_flags & CAKE_FLAG_AUTORATE_INGRESS)))
		goto nla_put_failure;

	if (nla_put_u32(skb, TCA_CAKE_INGRESS,
			!!(rate_flags & CAKE_FLAG_INGRESS)))
		goto nla_put_failure;

	if (nla_put_u32(skb, TCA_CAKE_ACK_FILTER, READ_ONCE(q->ack_filter)))
		goto nla_put_failure;

	if (nla_put_u32(skb, TCA_CAKE_NAT,
			!!(flow_mode & CAKE_FLOW_NAT_FLAG)))
		goto nla_put_failure;

	if (nla_put_u32(skb, TCA_CAKE_DIFFSERV_MODE, READ_ONCE(q->tin_mode)))
		goto nla_put_failure;

	if (nla_put_u32(skb, TCA_CAKE_WASH,
			!!(rate_flags & CAKE_FLAG_WASH)))
		goto nla_put_failure;

	if (nla_put_u32(skb, TCA_CAKE_OVERHEAD, READ_ONCE(q->rate_overhead)))
		goto nla_put_failure;

	if (!(rate_flags & CAKE_FLAG_OVERHEAD))
		if (nla_put_u32(skb, TCA_CAKE_RAW, 0))
			goto nla_put_failure;

	if (nla_put_u32(skb, TCA_CAKE_ATM, READ_ONCE(q->atm_mode)))
		goto nla_put_failure;

	if (nla_put_u32(skb, TCA_CAKE_MPU, READ_ONCE(q->rate_mpu)))
		goto nla_put_failure;

	if (nla_put_u32(skb, TCA_CAKE_SPLIT_GSO,
			!!(rate_flags & CAKE_FLAG_SPLIT_GSO)))
		goto nla_put_failure;

	if (nla_put_u32(skb, TCA_CAKE_FWMARK, READ_ONCE(q->fwmark_mask)))
		goto nla_put_failure;

	return nla_nest_end(skb, opts);

nla_put_failure:
	return -1;
}

static int cake_dump_stats(struct Qdisc *sch, struct gnet_dump *d)
{
	struct nlattr *stats = nla_nest_start_noflag(d->skb, TCA_STATS_APP);
	struct cake_sched_data *q = qdisc_priv(sch);
	struct nlattr *tstats, *ts;
	int i;

	if (!stats)
		return -1;

#define PUT_STAT_U32(attr, data) do {				       \
		if (nla_put_u32(d->skb, TCA_CAKE_STATS_ ## attr, data)) \
			goto nla_put_failure;			       \
	} while (0)
#define PUT_STAT_U64(attr, data) do {				       \
		if (nla_put_u64_64bit(d->skb, TCA_CAKE_STATS_ ## attr, \
					data, TCA_CAKE_STATS_PAD)) \
			goto nla_put_failure;			       \
	} while (0)

	PUT_STAT_U64(CAPACITY_ESTIMATE64, q->avg_peak_bandwidth);
	PUT_STAT_U32(MEMORY_LIMIT, q->buffer_limit);
	PUT_STAT_U32(MEMORY_USED, q->buffer_max_used);
	PUT_STAT_U32(AVG_NETOFF, ((q->avg_netoff + 0x8000) >> 16));
	PUT_STAT_U32(MAX_NETLEN, q->max_netlen);
	PUT_STAT_U32(MAX_ADJLEN, q->max_adjlen);
	PUT_STAT_U32(MIN_NETLEN, q->min_netlen);
	PUT_STAT_U32(MIN_ADJLEN, q->min_adjlen);

#undef PUT_STAT_U32
#undef PUT_STAT_U64

	tstats = nla_nest_start_noflag(d->skb, TCA_CAKE_STATS_TIN_STATS);
	if (!tstats)
		goto nla_put_failure;

#define PUT_TSTAT_U32(attr, data) do {					\
		if (nla_put_u32(d->skb, TCA_CAKE_TIN_STATS_ ## attr, data)) \
			goto nla_put_failure;				\
	} while (0)
#define PUT_TSTAT_U64(attr, data) do {					\
		if (nla_put_u64_64bit(d->skb, TCA_CAKE_TIN_STATS_ ## attr, \
					data, TCA_CAKE_TIN_STATS_PAD))	\
			goto nla_put_failure;				\
	} while (0)

	for (i = 0; i < q->tin_cnt; i++) {
		struct cake_tin_data *b = &q->tins[q->tin_order[i]];

		ts = nla_nest_start_noflag(d->skb, i + 1);
		if (!ts)
			goto nla_put_failure;

		PUT_TSTAT_U64(THRESHOLD_RATE64, b->tin_rate_bps);
		PUT_TSTAT_U64(SENT_BYTES64, b->bytes);
		PUT_TSTAT_U32(BACKLOG_BYTES, b->tin_backlog);

		PUT_TSTAT_U32(TARGET_US,
			      ktime_to_us(ns_to_ktime(b->cparams.target)));
		PUT_TSTAT_U32(INTERVAL_US,
			      ktime_to_us(ns_to_ktime(b->cparams.interval)));

		PUT_TSTAT_U32(SENT_PACKETS, b->packets);
		PUT_TSTAT_U32(DROPPED_PACKETS, b->tin_dropped);
		PUT_TSTAT_U32(ECN_MARKED_PACKETS, b->tin_ecn_mark);
		PUT_TSTAT_U32(ACKS_DROPPED_PACKETS, b->ack_drops);

		PUT_TSTAT_U32(PEAK_DELAY_US,
			      ktime_to_us(ns_to_ktime(b->peak_delay)));
		PUT_TSTAT_U32(AVG_DELAY_US,
			      ktime_to_us(ns_to_ktime(b->avge_delay)));
		PUT_TSTAT_U32(BASE_DELAY_US,
			      ktime_to_us(ns_to_ktime(b->base_delay)));

		PUT_TSTAT_U32(WAY_INDIRECT_HITS, b->way_hits);
		PUT_TSTAT_U32(WAY_MISSES, b->way_misses);
		PUT_TSTAT_U32(WAY_COLLISIONS, b->way_collisions);

		PUT_TSTAT_U32(SPARSE_FLOWS, b->sparse_flow_count +
					    b->decaying_flow_count);
		PUT_TSTAT_U32(BULK_FLOWS, b->bulk_flow_count);
		PUT_TSTAT_U32(UNRESPONSIVE_FLOWS, b->unresponsive_flow_count);
		PUT_TSTAT_U32(MAX_SKBLEN, b->max_skblen);

		PUT_TSTAT_U32(FLOW_QUANTUM, b->flow_quantum);
		nla_nest_end(d->skb, ts);
	}

#undef PUT_TSTAT_U32
#undef PUT_TSTAT_U64

	nla_nest_end(d->skb, tstats);
	return nla_nest_end(d->skb, stats);

nla_put_failure:
	nla_nest_cancel(d->skb, stats);
	return -1;
}

static struct Qdisc *cake_leaf(struct Qdisc *sch, unsigned long arg)
{
	return NULL;
}

static unsigned long cake_find(struct Qdisc *sch, u32 classid)
{
	return 0;
}

static unsigned long cake_bind(struct Qdisc *sch, unsigned long parent,
			       u32 classid)
{
	return 0;
}

static void cake_unbind(struct Qdisc *q, unsigned long cl)
{
}

static struct tcf_block *cake_tcf_block(struct Qdisc *sch, unsigned long cl,
					struct netlink_ext_ack *extack)
{
	struct cake_sched_data *q = qdisc_priv(sch);

	if (cl)
		return NULL;
	return q->block;
}

static int cake_dump_class(struct Qdisc *sch, unsigned long cl,
			   struct sk_buff *skb, struct tcmsg *tcm)
{
	tcm->tcm_handle |= TC_H_MIN(cl);
	return 0;
}

static int cake_dump_class_stats(struct Qdisc *sch, unsigned long cl,
				 struct gnet_dump *d)
{
	struct cake_sched_data *q = qdisc_priv(sch);
	const struct cake_flow *flow = NULL;
	struct gnet_stats_queue qs = { 0 };
	struct nlattr *stats;
	u32 idx = cl - 1;

	if (idx < CAKE_QUEUES * q->tin_cnt) {
		const struct cake_tin_data *b = \
			&q->tins[q->tin_order[idx / CAKE_QUEUES]];
		const struct sk_buff *skb;

		flow = &b->flows[idx % CAKE_QUEUES];

		if (flow->head) {
			sch_tree_lock(sch);
			skb = flow->head;
			while (skb) {
				qs.qlen++;
				skb = skb->next;
			}
			sch_tree_unlock(sch);
		}
		qs.backlog = b->backlogs[idx % CAKE_QUEUES];
		qs.drops = flow->dropped;
	}
	if (gnet_stats_copy_queue(d, NULL, &qs, qs.qlen) < 0)
		return -1;
	if (flow) {
		ktime_t now = ktime_get();

		stats = nla_nest_start_noflag(d->skb, TCA_STATS_APP);
		if (!stats)
			return -1;

#define PUT_STAT_U32(attr, data) do {				       \
		if (nla_put_u32(d->skb, TCA_CAKE_STATS_ ## attr, data)) \
			goto nla_put_failure;			       \
	} while (0)
#define PUT_STAT_S32(attr, data) do {				       \
		if (nla_put_s32(d->skb, TCA_CAKE_STATS_ ## attr, data)) \
			goto nla_put_failure;			       \
	} while (0)

		PUT_STAT_S32(DEFICIT, flow->deficit);
		PUT_STAT_U32(DROPPING, flow->cvars.dropping);
		PUT_STAT_U32(COBALT_COUNT, flow->cvars.count);
		PUT_STAT_U32(P_DROP, flow->cvars.p_drop);
		if (flow->cvars.p_drop) {
			PUT_STAT_S32(BLUE_TIMER_US,
				     ktime_to_us(
					     ktime_sub(now,
						       flow->cvars.blue_timer)));
		}
		if (flow->cvars.dropping) {
			PUT_STAT_S32(DROP_NEXT_US,
				     ktime_to_us(
					     ktime_sub(now,
						       flow->cvars.drop_next)));
		}

		if (nla_nest_end(d->skb, stats) < 0)
			return -1;
	}

	return 0;

nla_put_failure:
	nla_nest_cancel(d->skb, stats);
	return -1;
}

static void cake_walk(struct Qdisc *sch, struct qdisc_walker *arg)
{
	struct cake_sched_data *q = qdisc_priv(sch);
	unsigned int i, j;

	if (arg->stop)
		return;

	for (i = 0; i < q->tin_cnt; i++) {
		struct cake_tin_data *b = &q->tins[q->tin_order[i]];

		for (j = 0; j < CAKE_QUEUES; j++) {
			if (list_empty(&b->flows[j].flowchain)) {
				arg->count++;
				continue;
			}
			if (!tc_qdisc_stats_dump(sch, i * CAKE_QUEUES + j + 1,
						 arg))
				break;
		}
	}
}

static const struct Qdisc_class_ops cake_class_ops = {
	.leaf		=	cake_leaf,
	.find		=	cake_find,
	.tcf_block	=	cake_tcf_block,
	.bind_tcf	=	cake_bind,
	.unbind_tcf	=	cake_unbind,
	.dump		=	cake_dump_class,
	.dump_stats	=	cake_dump_class_stats,
	.walk		=	cake_walk,
};

static struct Qdisc_ops cake_qdisc_ops __read_mostly = {
	.cl_ops		=	&cake_class_ops,
	.id		=	"cake",
	.priv_size	=	sizeof(struct cake_sched_data),
	.enqueue	=	cake_enqueue,
	.dequeue	=	cake_dequeue,
	.peek		=	qdisc_peek_dequeued,
	.init		=	cake_init,
	.reset		=	cake_reset,
	.destroy	=	cake_destroy,
	.change		=	cake_change,
	.dump		=	cake_dump,
	.dump_stats	=	cake_dump_stats,
	.owner		=	THIS_MODULE,
};
MODULE_ALIAS_NET_SCH("cake");

static int __init cake_module_init(void)
{
	return register_qdisc(&cake_qdisc_ops);
}

static void __exit cake_module_exit(void)
{
	unregister_qdisc(&cake_qdisc_ops);
}

module_init(cake_module_init)
module_exit(cake_module_exit)
MODULE_AUTHOR("Jonathan Morton");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("The CAKE shaper.");
