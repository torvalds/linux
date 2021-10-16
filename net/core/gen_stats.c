// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * net/core/gen_stats.c
 *
 * Authors:  Thomas Graf <tgraf@suug.ch>
 *           Jamal Hadi Salim
 *           Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 * See Documentation/networking/gen_stats.rst
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/socket.h>
#include <linux/rtnetlink.h>
#include <linux/gen_stats.h>
#include <net/netlink.h>
#include <net/gen_stats.h>


static inline int
gnet_stats_copy(struct gnet_dump *d, int type, void *buf, int size, int padattr)
{
	if (nla_put_64bit(d->skb, type, size, buf, padattr))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	if (d->lock)
		spin_unlock_bh(d->lock);
	kfree(d->xstats);
	d->xstats = NULL;
	d->xstats_len = 0;
	return -1;
}

/**
 * gnet_stats_start_copy_compat - start dumping procedure in compatibility mode
 * @skb: socket buffer to put statistics TLVs into
 * @type: TLV type for top level statistic TLV
 * @tc_stats_type: TLV type for backward compatibility struct tc_stats TLV
 * @xstats_type: TLV type for backward compatibility xstats TLV
 * @lock: statistics lock
 * @d: dumping handle
 * @padattr: padding attribute
 *
 * Initializes the dumping handle, grabs the statistic lock and appends
 * an empty TLV header to the socket buffer for use a container for all
 * other statistic TLVS.
 *
 * The dumping handle is marked to be in backward compatibility mode telling
 * all gnet_stats_copy_XXX() functions to fill a local copy of struct tc_stats.
 *
 * Returns 0 on success or -1 if the room in the socket buffer was not sufficient.
 */
int
gnet_stats_start_copy_compat(struct sk_buff *skb, int type, int tc_stats_type,
			     int xstats_type, spinlock_t *lock,
			     struct gnet_dump *d, int padattr)
	__acquires(lock)
{
	memset(d, 0, sizeof(*d));

	if (type)
		d->tail = (struct nlattr *)skb_tail_pointer(skb);
	d->skb = skb;
	d->compat_tc_stats = tc_stats_type;
	d->compat_xstats = xstats_type;
	d->padattr = padattr;
	if (lock) {
		d->lock = lock;
		spin_lock_bh(lock);
	}
	if (d->tail) {
		int ret = gnet_stats_copy(d, type, NULL, 0, padattr);

		/* The initial attribute added in gnet_stats_copy() may be
		 * preceded by a padding attribute, in which case d->tail will
		 * end up pointing at the padding instead of the real attribute.
		 * Fix this so gnet_stats_finish_copy() adjusts the length of
		 * the right attribute.
		 */
		if (ret == 0 && d->tail->nla_type == padattr)
			d->tail = (struct nlattr *)((char *)d->tail +
						    NLA_ALIGN(d->tail->nla_len));
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(gnet_stats_start_copy_compat);

/**
 * gnet_stats_start_copy - start dumping procedure in compatibility mode
 * @skb: socket buffer to put statistics TLVs into
 * @type: TLV type for top level statistic TLV
 * @lock: statistics lock
 * @d: dumping handle
 * @padattr: padding attribute
 *
 * Initializes the dumping handle, grabs the statistic lock and appends
 * an empty TLV header to the socket buffer for use a container for all
 * other statistic TLVS.
 *
 * Returns 0 on success or -1 if the room in the socket buffer was not sufficient.
 */
int
gnet_stats_start_copy(struct sk_buff *skb, int type, spinlock_t *lock,
		      struct gnet_dump *d, int padattr)
{
	return gnet_stats_start_copy_compat(skb, type, 0, 0, lock, d, padattr);
}
EXPORT_SYMBOL(gnet_stats_start_copy);

static void gnet_stats_add_basic_cpu(struct gnet_stats_basic_packed *bstats,
				     struct gnet_stats_basic_cpu __percpu *cpu)
{
	int i;

	for_each_possible_cpu(i) {
		struct gnet_stats_basic_cpu *bcpu = per_cpu_ptr(cpu, i);
		unsigned int start;
		u64 bytes, packets;

		do {
			start = u64_stats_fetch_begin_irq(&bcpu->syncp);
			bytes = bcpu->bstats.bytes;
			packets = bcpu->bstats.packets;
		} while (u64_stats_fetch_retry_irq(&bcpu->syncp, start));

		bstats->bytes += bytes;
		bstats->packets += packets;
	}
}

void gnet_stats_add_basic(const seqcount_t *running,
			  struct gnet_stats_basic_packed *bstats,
			  struct gnet_stats_basic_cpu __percpu *cpu,
			  struct gnet_stats_basic_packed *b)
{
	unsigned int seq;
	u64 bytes = 0;
	u64 packets = 0;

	if (cpu) {
		gnet_stats_add_basic_cpu(bstats, cpu);
		return;
	}
	do {
		if (running)
			seq = read_seqcount_begin(running);
		bytes = b->bytes;
		packets = b->packets;
	} while (running && read_seqcount_retry(running, seq));

	bstats->bytes += bytes;
	bstats->packets += packets;
}
EXPORT_SYMBOL(gnet_stats_add_basic);

static int
___gnet_stats_copy_basic(const seqcount_t *running,
			 struct gnet_dump *d,
			 struct gnet_stats_basic_cpu __percpu *cpu,
			 struct gnet_stats_basic_packed *b,
			 int type)
{
	struct gnet_stats_basic_packed bstats = {0};

	gnet_stats_add_basic(running, &bstats, cpu, b);

	if (d->compat_tc_stats && type == TCA_STATS_BASIC) {
		d->tc_stats.bytes = bstats.bytes;
		d->tc_stats.packets = bstats.packets;
	}

	if (d->tail) {
		struct gnet_stats_basic sb;
		int res;

		memset(&sb, 0, sizeof(sb));
		sb.bytes = bstats.bytes;
		sb.packets = bstats.packets;
		res = gnet_stats_copy(d, type, &sb, sizeof(sb), TCA_STATS_PAD);
		if (res < 0 || sb.packets == bstats.packets)
			return res;
		/* emit 64bit stats only if needed */
		return gnet_stats_copy(d, TCA_STATS_PKT64, &bstats.packets,
				       sizeof(bstats.packets), TCA_STATS_PAD);
	}
	return 0;
}

/**
 * gnet_stats_copy_basic - copy basic statistics into statistic TLV
 * @running: seqcount_t pointer
 * @d: dumping handle
 * @cpu: copy statistic per cpu
 * @b: basic statistics
 *
 * Appends the basic statistics to the top level TLV created by
 * gnet_stats_start_copy().
 *
 * Returns 0 on success or -1 with the statistic lock released
 * if the room in the socket buffer was not sufficient.
 */
int
gnet_stats_copy_basic(const seqcount_t *running,
		      struct gnet_dump *d,
		      struct gnet_stats_basic_cpu __percpu *cpu,
		      struct gnet_stats_basic_packed *b)
{
	return ___gnet_stats_copy_basic(running, d, cpu, b,
					TCA_STATS_BASIC);
}
EXPORT_SYMBOL(gnet_stats_copy_basic);

/**
 * gnet_stats_copy_basic_hw - copy basic hw statistics into statistic TLV
 * @running: seqcount_t pointer
 * @d: dumping handle
 * @cpu: copy statistic per cpu
 * @b: basic statistics
 *
 * Appends the basic statistics to the top level TLV created by
 * gnet_stats_start_copy().
 *
 * Returns 0 on success or -1 with the statistic lock released
 * if the room in the socket buffer was not sufficient.
 */
int
gnet_stats_copy_basic_hw(const seqcount_t *running,
			 struct gnet_dump *d,
			 struct gnet_stats_basic_cpu __percpu *cpu,
			 struct gnet_stats_basic_packed *b)
{
	return ___gnet_stats_copy_basic(running, d, cpu, b,
					TCA_STATS_BASIC_HW);
}
EXPORT_SYMBOL(gnet_stats_copy_basic_hw);

/**
 * gnet_stats_copy_rate_est - copy rate estimator statistics into statistics TLV
 * @d: dumping handle
 * @rate_est: rate estimator
 *
 * Appends the rate estimator statistics to the top level TLV created by
 * gnet_stats_start_copy().
 *
 * Returns 0 on success or -1 with the statistic lock released
 * if the room in the socket buffer was not sufficient.
 */
int
gnet_stats_copy_rate_est(struct gnet_dump *d,
			 struct net_rate_estimator __rcu **rate_est)
{
	struct gnet_stats_rate_est64 sample;
	struct gnet_stats_rate_est est;
	int res;

	if (!gen_estimator_read(rate_est, &sample))
		return 0;
	est.bps = min_t(u64, UINT_MAX, sample.bps);
	/* we have some time before reaching 2^32 packets per second */
	est.pps = sample.pps;

	if (d->compat_tc_stats) {
		d->tc_stats.bps = est.bps;
		d->tc_stats.pps = est.pps;
	}

	if (d->tail) {
		res = gnet_stats_copy(d, TCA_STATS_RATE_EST, &est, sizeof(est),
				      TCA_STATS_PAD);
		if (res < 0 || est.bps == sample.bps)
			return res;
		/* emit 64bit stats only if needed */
		return gnet_stats_copy(d, TCA_STATS_RATE_EST64, &sample,
				       sizeof(sample), TCA_STATS_PAD);
	}

	return 0;
}
EXPORT_SYMBOL(gnet_stats_copy_rate_est);

static void
__gnet_stats_copy_queue_cpu(struct gnet_stats_queue *qstats,
			    const struct gnet_stats_queue __percpu *q)
{
	int i;

	for_each_possible_cpu(i) {
		const struct gnet_stats_queue *qcpu = per_cpu_ptr(q, i);

		qstats->qlen = 0;
		qstats->backlog += qcpu->backlog;
		qstats->drops += qcpu->drops;
		qstats->requeues += qcpu->requeues;
		qstats->overlimits += qcpu->overlimits;
	}
}

void __gnet_stats_copy_queue(struct gnet_stats_queue *qstats,
			     const struct gnet_stats_queue __percpu *cpu,
			     const struct gnet_stats_queue *q,
			     __u32 qlen)
{
	if (cpu) {
		__gnet_stats_copy_queue_cpu(qstats, cpu);
	} else {
		qstats->qlen = q->qlen;
		qstats->backlog = q->backlog;
		qstats->drops = q->drops;
		qstats->requeues = q->requeues;
		qstats->overlimits = q->overlimits;
	}

	qstats->qlen = qlen;
}
EXPORT_SYMBOL(__gnet_stats_copy_queue);

/**
 * gnet_stats_copy_queue - copy queue statistics into statistics TLV
 * @d: dumping handle
 * @cpu_q: per cpu queue statistics
 * @q: queue statistics
 * @qlen: queue length statistics
 *
 * Appends the queue statistics to the top level TLV created by
 * gnet_stats_start_copy(). Using per cpu queue statistics if
 * they are available.
 *
 * Returns 0 on success or -1 with the statistic lock released
 * if the room in the socket buffer was not sufficient.
 */
int
gnet_stats_copy_queue(struct gnet_dump *d,
		      struct gnet_stats_queue __percpu *cpu_q,
		      struct gnet_stats_queue *q, __u32 qlen)
{
	struct gnet_stats_queue qstats = {0};

	__gnet_stats_copy_queue(&qstats, cpu_q, q, qlen);

	if (d->compat_tc_stats) {
		d->tc_stats.drops = qstats.drops;
		d->tc_stats.qlen = qstats.qlen;
		d->tc_stats.backlog = qstats.backlog;
		d->tc_stats.overlimits = qstats.overlimits;
	}

	if (d->tail)
		return gnet_stats_copy(d, TCA_STATS_QUEUE,
				       &qstats, sizeof(qstats),
				       TCA_STATS_PAD);

	return 0;
}
EXPORT_SYMBOL(gnet_stats_copy_queue);

/**
 * gnet_stats_copy_app - copy application specific statistics into statistics TLV
 * @d: dumping handle
 * @st: application specific statistics data
 * @len: length of data
 *
 * Appends the application specific statistics to the top level TLV created by
 * gnet_stats_start_copy() and remembers the data for XSTATS if the dumping
 * handle is in backward compatibility mode.
 *
 * Returns 0 on success or -1 with the statistic lock released
 * if the room in the socket buffer was not sufficient.
 */
int
gnet_stats_copy_app(struct gnet_dump *d, void *st, int len)
{
	if (d->compat_xstats) {
		d->xstats = kmemdup(st, len, GFP_ATOMIC);
		if (!d->xstats)
			goto err_out;
		d->xstats_len = len;
	}

	if (d->tail)
		return gnet_stats_copy(d, TCA_STATS_APP, st, len,
				       TCA_STATS_PAD);

	return 0;

err_out:
	if (d->lock)
		spin_unlock_bh(d->lock);
	d->xstats_len = 0;
	return -1;
}
EXPORT_SYMBOL(gnet_stats_copy_app);

/**
 * gnet_stats_finish_copy - finish dumping procedure
 * @d: dumping handle
 *
 * Corrects the length of the top level TLV to include all TLVs added
 * by gnet_stats_copy_XXX() calls. Adds the backward compatibility TLVs
 * if gnet_stats_start_copy_compat() was used and releases the statistics
 * lock.
 *
 * Returns 0 on success or -1 with the statistic lock released
 * if the room in the socket buffer was not sufficient.
 */
int
gnet_stats_finish_copy(struct gnet_dump *d)
{
	if (d->tail)
		d->tail->nla_len = skb_tail_pointer(d->skb) - (u8 *)d->tail;

	if (d->compat_tc_stats)
		if (gnet_stats_copy(d, d->compat_tc_stats, &d->tc_stats,
				    sizeof(d->tc_stats), d->padattr) < 0)
			return -1;

	if (d->compat_xstats && d->xstats) {
		if (gnet_stats_copy(d, d->compat_xstats, d->xstats,
				    d->xstats_len, d->padattr) < 0)
			return -1;
	}

	if (d->lock)
		spin_unlock_bh(d->lock);
	kfree(d->xstats);
	d->xstats = NULL;
	d->xstats_len = 0;
	return 0;
}
EXPORT_SYMBOL(gnet_stats_finish_copy);
