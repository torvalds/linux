/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005, Devicescape Software, Inc.
 * Copyright 2007, Mattias Nissler <mattias.nissler@gmx.de>
 * Copyright 2007, Stefano Brivio <stefano.brivio@polimi.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/skbuff.h>

#include <net/mac80211.h>
#include "ieee80211_rate.h"

#include "rc80211_pid.h"


/* This is an implementation of a TX rate control algorithm that uses a PID
 * controller. Given a target failed frames rate, the controller decides about
 * TX rate changes to meet the target failed frames rate.
 *
 * The controller basically computes the following:
 *
 * adj = CP * err + CI * err_avg + CD * (err - last_err) * (1 + sharpening)
 *
 * where
 * 	adj	adjustment value that is used to switch TX rate (see below)
 * 	err	current error: target vs. current failed frames percentage
 * 	last_err	last error
 * 	err_avg	average (i.e. poor man's integral) of recent errors
 *	sharpening	non-zero when fast response is needed (i.e. right after
 *			association or no frames sent for a long time), heading
 * 			to zero over time
 * 	CP	Proportional coefficient
 * 	CI	Integral coefficient
 * 	CD	Derivative coefficient
 *
 * CP, CI, CD are subject to careful tuning.
 *
 * The integral component uses a exponential moving average approach instead of
 * an actual sliding window. The advantage is that we don't need to keep an
 * array of the last N error values and computation is easier.
 *
 * Once we have the adj value, we map it to a rate by means of a learning
 * algorithm. This algorithm keeps the state of the percentual failed frames
 * difference between rates. The behaviour of the lowest available rate is kept
 * as a reference value, and every time we switch between two rates, we compute
 * the difference between the failed frames each rate exhibited. By doing so,
 * we compare behaviours which different rates exhibited in adjacent timeslices,
 * thus the comparison is minimally affected by external conditions. This
 * difference gets propagated to the whole set of measurements, so that the
 * reference is always the same. Periodically, we normalize this set so that
 * recent events weigh the most. By comparing the adj value with this set, we
 * avoid pejorative switches to lower rates and allow for switches to higher
 * rates if they behaved well.
 *
 * Note that for the computations we use a fixed-point representation to avoid
 * floating point arithmetic. Hence, all values are shifted left by
 * RC_PID_ARITH_SHIFT.
 */


/* Shift the adjustment so that we won't switch to a lower rate if it exhibited
 * a worse failed frames behaviour and we'll choose the highest rate whose
 * failed frames behaviour is not worse than the one of the original rate
 * target. While at it, check that the adjustment is within the ranges. Then,
 * provide the new rate index. */
static int rate_control_pid_shift_adjust(struct rc_pid_rateinfo *r,
					 int adj, int cur, int l)
{
	int i, j, k, tmp;

	if (cur + adj < 0)
		return 0;
	if (cur + adj >= l)
		return l - 1;

	i = r[cur + adj].rev_index;

	j = r[cur].rev_index;

	if (adj < 0) {
			tmp = i;
			for (k = j; k >= i; k--)
				if (r[k].diff <= r[j].diff)
					tmp = k;
			return r[tmp].index;
	} else if (adj > 0) {
			tmp = i;
			for (k = i + 1; k + i < l; k++)
				if (r[k].diff <= r[i].diff)
					tmp = k;
			return r[tmp].index;
	}
	return cur + adj;
}

static void rate_control_pid_adjust_rate(struct ieee80211_local *local,
					 struct sta_info *sta, int adj,
					 struct rc_pid_rateinfo *rinfo)
{
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_hw_mode *mode;
	int newidx;
	int maxrate;
	int back = (adj > 0) ? 1 : -1;

	sdata = IEEE80211_DEV_TO_SUB_IF(sta->dev);
	if (sdata->bss && sdata->bss->force_unicast_rateidx > -1) {
		/* forced unicast rate - do not change STA rate */
		return;
	}

	mode = local->oper_hw_mode;
	maxrate = sdata->bss ? sdata->bss->max_ratectrl_rateidx : -1;

	newidx = rate_control_pid_shift_adjust(rinfo, adj, sta->txrate,
					       mode->num_rates);

	while (newidx != sta->txrate) {
		if (rate_supported(sta, mode, newidx) &&
		    (maxrate < 0 || newidx <= maxrate)) {
			sta->txrate = newidx;
			break;
		}

		newidx += back;
	}

#ifdef CONFIG_MAC80211_DEBUGFS
	rate_control_pid_event_rate_change(
		&((struct rc_pid_sta_info *)sta->rate_ctrl_priv)->events,
		newidx, mode->rates[newidx].rate);
#endif
}

/* Normalize the failed frames per-rate differences. */
static void rate_control_pid_normalize(struct rc_pid_rateinfo *r, int l)
{
	int i;

	if (r[0].diff > RC_PID_NORM_OFFSET)
		r[0].diff -= RC_PID_NORM_OFFSET;
	else if (r[0].diff < -RC_PID_NORM_OFFSET)
		r[0].diff += RC_PID_NORM_OFFSET;
	for (i = 0; i < l - 1; i++)
		if (r[i + 1].diff > r[i].diff + RC_PID_NORM_OFFSET)
			r[i + 1].diff -= RC_PID_NORM_OFFSET;
		else if (r[i + 1].diff <= r[i].diff)
			r[i + 1].diff += RC_PID_NORM_OFFSET;
}

static void rate_control_pid_sample(struct rc_pid_info *pinfo,
				    struct ieee80211_local *local,
				    struct sta_info *sta)
{
	struct rc_pid_sta_info *spinfo = sta->rate_ctrl_priv;
	struct rc_pid_rateinfo *rinfo = pinfo->rinfo;
	struct ieee80211_hw_mode *mode;
	u32 pf;
	s32 err_avg;
	s32 err_prop;
	s32 err_int;
	s32 err_der;
	int adj, i, j, tmp;

	mode = local->oper_hw_mode;
	spinfo = sta->rate_ctrl_priv;

	/* In case nothing happened during the previous control interval, turn
	 * the sharpening factor on. */
	if (jiffies - spinfo->last_sample > 2 * RC_PID_INTERVAL)
		spinfo->sharp_cnt = RC_PID_SHARPENING_DURATION;

	spinfo->last_sample = jiffies;

	/* This should never happen, but in case, we assume the old sample is
	 * still a good measurement and copy it. */
	if (unlikely(spinfo->tx_num_xmit == 0))
		pf = spinfo->last_pf;
	else {
		pf = spinfo->tx_num_failed * 100 / spinfo->tx_num_xmit;
		pf <<= RC_PID_ARITH_SHIFT;
	}

	spinfo->tx_num_xmit = 0;
	spinfo->tx_num_failed = 0;

	/* If we just switched rate, update the rate behaviour info. */
	if (pinfo->oldrate != sta->txrate) {

		i = rinfo[pinfo->oldrate].rev_index;
		j = rinfo[sta->txrate].rev_index;

		tmp = (pf - spinfo->last_pf);
		tmp = RC_PID_DO_ARITH_RIGHT_SHIFT(tmp, RC_PID_ARITH_SHIFT);

		rinfo[j].diff = rinfo[i].diff + tmp;
		pinfo->oldrate = sta->txrate;
	}
	rate_control_pid_normalize(rinfo, mode->num_rates);

	/* Compute the proportional, integral and derivative errors. */
	err_prop = RC_PID_TARGET_PF - pf;

	err_avg = spinfo->err_avg_sc >> RC_PID_SMOOTHING_SHIFT;
	spinfo->err_avg_sc = spinfo->err_avg_sc - err_avg + err_prop;
	err_int = spinfo->err_avg_sc >> RC_PID_SMOOTHING_SHIFT;

	err_der = pf - spinfo->last_pf
		  * (1 + RC_PID_SHARPENING_FACTOR * spinfo->sharp_cnt);
	spinfo->last_pf = pf;
	if (spinfo->sharp_cnt)
			spinfo->sharp_cnt--;

#ifdef CONFIG_MAC80211_DEBUGFS
	rate_control_pid_event_pf_sample(&spinfo->events, pf, err_prop, err_int,
					 err_der);
#endif

	/* Compute the controller output. */
	adj = (err_prop * pinfo->coeff_p + err_int * pinfo->coeff_i
	      + err_der * pinfo->coeff_d);
	adj = RC_PID_DO_ARITH_RIGHT_SHIFT(adj, 2 * RC_PID_ARITH_SHIFT);

	/* Change rate. */
	if (adj)
		rate_control_pid_adjust_rate(local, sta, adj, rinfo);
}

static void rate_control_pid_tx_status(void *priv, struct net_device *dev,
				       struct sk_buff *skb,
				       struct ieee80211_tx_status *status)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct rc_pid_info *pinfo = priv;
	struct sta_info *sta;
	struct rc_pid_sta_info *spinfo;

	sta = sta_info_get(local, hdr->addr1);

	if (!sta)
		return;

	/* Ignore all frames that were sent with a different rate than the rate
	 * we currently advise mac80211 to use. */
	if (status->control.rate != &local->oper_hw_mode->rates[sta->txrate])
		return;

	spinfo = sta->rate_ctrl_priv;
	spinfo->tx_num_xmit++;

#ifdef CONFIG_MAC80211_DEBUGFS
	rate_control_pid_event_tx_status(&spinfo->events, status);
#endif

	/* We count frames that totally failed to be transmitted as two bad
	 * frames, those that made it out but had some retries as one good and
	 * one bad frame. */
	if (status->excessive_retries) {
		spinfo->tx_num_failed += 2;
		spinfo->tx_num_xmit++;
	} else if (status->retry_count) {
		spinfo->tx_num_failed++;
		spinfo->tx_num_xmit++;
	}

	if (status->excessive_retries) {
		sta->tx_retry_failed++;
		sta->tx_num_consecutive_failures++;
		sta->tx_num_mpdu_fail++;
	} else {
		sta->last_ack_rssi[0] = sta->last_ack_rssi[1];
		sta->last_ack_rssi[1] = sta->last_ack_rssi[2];
		sta->last_ack_rssi[2] = status->ack_signal;
		sta->tx_num_consecutive_failures = 0;
		sta->tx_num_mpdu_ok++;
	}
	sta->tx_retry_count += status->retry_count;
	sta->tx_num_mpdu_fail += status->retry_count;

	/* Update PID controller state. */
	if (time_after(jiffies, spinfo->last_sample + RC_PID_INTERVAL))
		rate_control_pid_sample(pinfo, local, sta);

	sta_info_put(sta);
}

static void rate_control_pid_get_rate(void *priv, struct net_device *dev,
				      struct ieee80211_hw_mode *mode,
				      struct sk_buff *skb,
				      struct rate_selection *sel)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct sta_info *sta;
	int rateidx;

	sta = sta_info_get(local, hdr->addr1);

	if (!sta) {
		sel->rate = rate_lowest(local, mode, NULL);
		sta_info_put(sta);
		return;
	}

	rateidx = sta->txrate;

	if (rateidx >= mode->num_rates)
		rateidx = mode->num_rates - 1;

	sta_info_put(sta);

	sel->rate = &mode->rates[rateidx];

#ifdef CONFIG_MAC80211_DEBUGFS
	rate_control_pid_event_tx_rate(
		&((struct rc_pid_sta_info *) sta->rate_ctrl_priv)->events,
		rateidx, mode->rates[rateidx].rate);
#endif
}

static void rate_control_pid_rate_init(void *priv, void *priv_sta,
					  struct ieee80211_local *local,
					  struct sta_info *sta)
{
	/* TODO: This routine should consider using RSSI from previous packets
	 * as we need to have IEEE 802.1X auth succeed immediately after assoc..
	 * Until that method is implemented, we will use the lowest supported
	 * rate as a workaround. */
	sta->txrate = rate_lowest_index(local, local->oper_hw_mode, sta);
}

static void *rate_control_pid_alloc(struct ieee80211_local *local)
{
	struct rc_pid_info *pinfo;
	struct rc_pid_rateinfo *rinfo;
	struct ieee80211_hw_mode *mode;
	int i, j, tmp;
	bool s;

	pinfo = kmalloc(sizeof(*pinfo), GFP_ATOMIC);
	if (!pinfo)
		return NULL;

	/* We can safely assume that oper_hw_mode won't change unless we get
	 * reinitialized. */
	mode = local->oper_hw_mode;
	rinfo = kmalloc(sizeof(*rinfo) * mode->num_rates, GFP_ATOMIC);
	if (!rinfo) {
		kfree(pinfo);
		return NULL;
	}

	/* Sort the rates. This is optimized for the most common case (i.e.
	 * almost-sorted CCK+OFDM rates). Kind of bubble-sort with reversed
	 * mapping too. */
	for (i = 0; i < mode->num_rates; i++) {
		rinfo[i].index = i;
		rinfo[i].rev_index = i;
		if (RC_PID_FAST_START)
			rinfo[i].diff = 0;
		else
			rinfo[i].diff = i * RC_PID_NORM_OFFSET;
	}
	for (i = 1; i < mode->num_rates; i++) {
		s = 0;
		for (j = 0; j < mode->num_rates - i; j++)
			if (unlikely(mode->rates[rinfo[j].index].rate >
				     mode->rates[rinfo[j + 1].index].rate)) {
				tmp = rinfo[j].index;
				rinfo[j].index = rinfo[j + 1].index;
				rinfo[j + 1].index = tmp;
				rinfo[rinfo[j].index].rev_index = j;
				rinfo[rinfo[j + 1].index].rev_index = j + 1;
				s = 1;
			}
		if (!s)
			break;
	}

	pinfo->target = RC_PID_TARGET_PF;
	pinfo->coeff_p = RC_PID_COEFF_P;
	pinfo->coeff_i = RC_PID_COEFF_I;
	pinfo->coeff_d = RC_PID_COEFF_D;
	pinfo->rinfo = rinfo;
	pinfo->oldrate = 0;

	return pinfo;
}

static void rate_control_pid_free(void *priv)
{
	struct rc_pid_info *pinfo = priv;
	kfree(pinfo->rinfo);
	kfree(pinfo);
}

static void rate_control_pid_clear(void *priv)
{
}

static void *rate_control_pid_alloc_sta(void *priv, gfp_t gfp)
{
	struct rc_pid_sta_info *spinfo;

	spinfo = kzalloc(sizeof(*spinfo), gfp);
	if (spinfo == NULL)
		return NULL;

#ifdef CONFIG_MAC80211_DEBUGFS
	spin_lock_init(&spinfo->events.lock);
	init_waitqueue_head(&spinfo->events.waitqueue);
#endif

	return spinfo;
}

static void rate_control_pid_free_sta(void *priv, void *priv_sta)
{
	struct rc_pid_sta_info *spinfo = priv_sta;
	kfree(spinfo);
}

struct rate_control_ops mac80211_rcpid = {
	.name = "pid",
	.tx_status = rate_control_pid_tx_status,
	.get_rate = rate_control_pid_get_rate,
	.rate_init = rate_control_pid_rate_init,
	.clear = rate_control_pid_clear,
	.alloc = rate_control_pid_alloc,
	.free = rate_control_pid_free,
	.alloc_sta = rate_control_pid_alloc_sta,
	.free_sta = rate_control_pid_free_sta,
#ifdef CONFIG_MAC80211_DEBUGFS
	.add_sta_debugfs = rate_control_pid_add_sta_debugfs,
	.remove_sta_debugfs = rate_control_pid_remove_sta_debugfs,
#endif
};
