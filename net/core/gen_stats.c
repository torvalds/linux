/*
 * net/core/gen_stats.c
 *
 *             This program is free software; you can redistribute it and/or
 *             modify it under the terms of the GNU General Public License
 *             as published by the Free Software Foundation; either version
 *             2 of the License, or (at your option) any later version.
 *
 * Authors:  Thomas Graf <tgraf@suug.ch>
 *           Jamal Hadi Salim
 *           Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 * See Documentation/networking/gen_stats.txt
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/socket.h>
#include <linux/rtnetlink.h>
#include <linux/gen_stats.h>
#include <net/gen_stats.h>


static inline int
gnet_stats_copy(struct gnet_dump *d, int type, void *buf, int size)
{
	RTA_PUT(d->skb, type, size, buf);
	return 0;

rtattr_failure:
	spin_unlock_bh(d->lock);
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
	int xstats_type, spinlock_t *lock, struct gnet_dump *d)
{
	memset(d, 0, sizeof(*d));

	spin_lock_bh(lock);
	d->lock = lock;
	if (type)
		d->tail = (struct rtattr *) skb->tail;
	d->skb = skb;
	d->compat_tc_stats = tc_stats_type;
	d->compat_xstats = xstats_type;

	if (d->tail)
		return gnet_stats_copy(d, type, NULL, 0);

	return 0;
}

/**
 * gnet_stats_start_copy_compat - start dumping procedure in compatibility mode
 * @skb: socket buffer to put statistics TLVs into
 * @type: TLV type for top level statistic TLV
 * @lock: statistics lock
 * @d: dumping handle
 *
 * Initializes the dumping handle, grabs the statistic lock and appends
 * an empty TLV header to the socket buffer for use a container for all
 * other statistic TLVS.
 *
 * Returns 0 on success or -1 if the room in the socket buffer was not sufficient.
 */
int
gnet_stats_start_copy(struct sk_buff *skb, int type, spinlock_t *lock,
	struct gnet_dump *d)
{
	return gnet_stats_start_copy_compat(skb, type, 0, 0, lock, d);
}

/**
 * gnet_stats_copy_basic - copy basic statistics into statistic TLV
 * @d: dumping handle
 * @b: basic statistics
 *
 * Appends the basic statistics to the top level TLV created by
 * gnet_stats_start_copy().
 *
 * Returns 0 on success or -1 with the statistic lock released
 * if the room in the socket buffer was not sufficient.
 */
int
gnet_stats_copy_basic(struct gnet_dump *d, struct gnet_stats_basic *b)
{
	if (d->compat_tc_stats) {
		d->tc_stats.bytes = b->bytes;
		d->tc_stats.packets = b->packets;
	}

	if (d->tail)
		return gnet_stats_copy(d, TCA_STATS_BASIC, b, sizeof(*b));

	return 0;
}

/**
 * gnet_stats_copy_rate_est - copy rate estimator statistics into statistics TLV
 * @d: dumping handle
 * @r: rate estimator statistics
 *
 * Appends the rate estimator statistics to the top level TLV created by
 * gnet_stats_start_copy().
 *
 * Returns 0 on success or -1 with the statistic lock released
 * if the room in the socket buffer was not sufficient.
 */
int
gnet_stats_copy_rate_est(struct gnet_dump *d, struct gnet_stats_rate_est *r)
{
	if (d->compat_tc_stats) {
		d->tc_stats.bps = r->bps;
		d->tc_stats.pps = r->pps;
	}

	if (d->tail)
		return gnet_stats_copy(d, TCA_STATS_RATE_EST, r, sizeof(*r));

	return 0;
}

/**
 * gnet_stats_copy_queue - copy queue statistics into statistics TLV
 * @d: dumping handle
 * @q: queue statistics
 *
 * Appends the queue statistics to the top level TLV created by
 * gnet_stats_start_copy().
 *
 * Returns 0 on success or -1 with the statistic lock released
 * if the room in the socket buffer was not sufficient.
 */
int
gnet_stats_copy_queue(struct gnet_dump *d, struct gnet_stats_queue *q)
{
	if (d->compat_tc_stats) {
		d->tc_stats.drops = q->drops;
		d->tc_stats.qlen = q->qlen;
		d->tc_stats.backlog = q->backlog;
		d->tc_stats.overlimits = q->overlimits;
	}

	if (d->tail)
		return gnet_stats_copy(d, TCA_STATS_QUEUE, q, sizeof(*q));

	return 0;
}

/**
 * gnet_stats_copy_app - copy application specific statistics into statistics TLV
 * @d: dumping handle
 * @st: application specific statistics data
 * @len: length of data
 *
 * Appends the application sepecific statistics to the top level TLV created by
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
		d->xstats = st;
		d->xstats_len = len;
	}

	if (d->tail)
		return gnet_stats_copy(d, TCA_STATS_APP, st, len);

	return 0;
}

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
		d->tail->rta_len = d->skb->tail - (u8 *) d->tail;

	if (d->compat_tc_stats)
		if (gnet_stats_copy(d, d->compat_tc_stats, &d->tc_stats,
			sizeof(d->tc_stats)) < 0)
			return -1;

	if (d->compat_xstats && d->xstats) {
		if (gnet_stats_copy(d, d->compat_xstats, d->xstats,
			d->xstats_len) < 0)
			return -1;
	}

	spin_unlock_bh(d->lock);
	return 0;
}


EXPORT_SYMBOL(gnet_stats_start_copy);
EXPORT_SYMBOL(gnet_stats_start_copy_compat);
EXPORT_SYMBOL(gnet_stats_copy_basic);
EXPORT_SYMBOL(gnet_stats_copy_rate_est);
EXPORT_SYMBOL(gnet_stats_copy_queue);
EXPORT_SYMBOL(gnet_stats_copy_app);
EXPORT_SYMBOL(gnet_stats_finish_copy);
