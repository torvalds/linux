/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005, Devicescape Software, Inc.
 * Copyright 2007, Mattias Nissler <mattias.nissler@gmx.de>
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


/* This is an implementation of a TX rate control algorithm that uses a PID
 * controller. Given a target failed frames rate, the controller decides about
 * TX rate changes to meet the target failed frames rate.
 *
 * The controller basically computes the following:
 *
 * adj = CP * err + CI * err_avg + CD * (err - last_err)
 *
 * where
 * 	adj	adjustment value that is used to switch TX rate (see below)
 * 	err	current error: target vs. current failed frames percentage
 * 	last_err	last error
 * 	err_avg	average (i.e. poor man's integral) of recent errors
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
 * Once we have the adj value, we need to map it to a TX rate to be selected.
 * For now, we depend on the rates to be ordered in a way such that more robust
 * rates (i.e. such that exhibit a lower framed failed percentage) come first.
 * E.g. for the 802.11b/g case, we first have the b rates in ascending order,
 * then the g rates. The adj simply decides the index of the TX rate in the list
 * to switch to (relative to the current TX rate entry).
 *
 * Note that for the computations we use a fixed-point representation to avoid
 * floating point arithmetic. Hence, all values are shifted left by
 * RC_PID_ARITH_SHIFT.
 */

/* Sampling period for measuring percentage of failed frames. */
#define RC_PID_INTERVAL (HZ / 8)

/* Exponential averaging smoothness (used for I part of PID controller) */
#define RC_PID_SMOOTHING_SHIFT 3
#define RC_PID_SMOOTHING (1 << RC_PID_SMOOTHING_SHIFT)

/* Fixed point arithmetic shifting amount. */
#define RC_PID_ARITH_SHIFT 8

/* Fixed point arithmetic factor. */
#define RC_PID_ARITH_FACTOR (1 << RC_PID_ARITH_SHIFT)

/* Proportional PID component coefficient. */
#define RC_PID_COEFF_P 15
/* Integral PID component coefficient. */
#define RC_PID_COEFF_I 9
/* Derivative PID component coefficient. */
#define RC_PID_COEFF_D 15

/* Target failed frames rate for the PID controller. NB: This effectively gives
 * maximum failed frames percentage we're willing to accept. If the wireless
 * link quality is good, the controller will fail to adjust failed frames
 * percentage to the target. This is intentional.
 */
#define RC_PID_TARGET_PF (11 << RC_PID_ARITH_SHIFT)

struct rc_pid_sta_info {
	unsigned long last_change;
	unsigned long last_sample;

	u32 tx_num_failed;
	u32 tx_num_xmit;

	/* Average failed frames percentage error (i.e. actual vs. target
	 * percentage), scaled by RC_PID_SMOOTHING. This value is computed
	 * using using an exponential weighted average technique:
	 *
	 *           (RC_PID_SMOOTHING - 1) * err_avg_old + err
	 * err_avg = ------------------------------------------
	 *                       RC_PID_SMOOTHING
	 *
	 * where err_avg is the new approximation, err_avg_old the previous one
	 * and err is the error w.r.t. to the current failed frames percentage
	 * sample. Note that the bigger RC_PID_SMOOTHING the more weight is
	 * given to the previous estimate, resulting in smoother behavior (i.e.
	 * corresponding to a longer integration window).
	 *
	 * For computation, we actually don't use the above formula, but this
	 * one:
	 *
	 * err_avg_scaled = err_avg_old_scaled - err_avg_old + err
	 *
	 * where:
	 * 	err_avg_scaled = err * RC_PID_SMOOTHING
	 * 	err_avg_old_scaled = err_avg_old * RC_PID_SMOOTHING
	 *
	 * This avoids floating point numbers and the per_failed_old value can
	 * easily be obtained by shifting per_failed_old_scaled right by
	 * RC_PID_SMOOTHING_SHIFT.
	 */
	s32 err_avg_sc;

	/* Last framed failes percentage sample */
	u32 last_pf;
};

/* Algorithm parameters. We keep them on a per-algorithm approach, so they can
 * be tuned individually for each interface.
 */
struct rc_pid_info {

	/* The failed frames percentage target. */
	u32 target;

	/* P, I and D coefficients. */
	s32 coeff_p;
	s32 coeff_i;
	s32 coeff_d;
};


static void rate_control_pid_adjust_rate(struct ieee80211_local *local,
					 struct sta_info *sta, int adj)
{
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_hw_mode *mode;
	int newidx = sta->txrate + adj;
	int maxrate;
	int back = (adj > 0) ? 1 : -1;

	sdata = IEEE80211_DEV_TO_SUB_IF(sta->dev);
	if (sdata->bss && sdata->bss->force_unicast_rateidx > -1) {
		/* forced unicast rate - do not change STA rate */
		return;
	}

	mode = local->oper_hw_mode;
	maxrate = sdata->bss ? sdata->bss->max_ratectrl_rateidx : -1;

	if (newidx < 0)
		newidx = 0;
	else if (newidx >= mode->num_rates)
		newidx = mode->num_rates - 1;

	while (newidx != sta->txrate) {
		if (rate_supported(sta, mode, newidx) &&
		    (maxrate < 0 || newidx <= maxrate)) {
			sta->txrate = newidx;
			break;
		}

		newidx += back;
	}
}

static void rate_control_pid_sample(struct rc_pid_info *pinfo,
				    struct ieee80211_local *local,
				    struct sta_info *sta)
{
	struct rc_pid_sta_info *spinfo = sta->rate_ctrl_priv;
	u32 pf;
	s32 err_avg;
	s32 err_prop;
	s32 err_int;
	s32 err_der;
	int adj;

	spinfo = sta->rate_ctrl_priv;
	spinfo->last_sample = jiffies;

	/* If no frames were transmitted, we assume the old sample is
	 * still a good measurement and copy it. */
	if (spinfo->tx_num_xmit == 0)
		pf = spinfo->last_pf;
	else {
		pf = spinfo->tx_num_failed * 100 / spinfo->tx_num_xmit;
		pf <<= RC_PID_ARITH_SHIFT;

		spinfo->tx_num_xmit = 0;
		spinfo->tx_num_failed = 0;
	}

	/* Compute the proportional, integral and derivative errors. */
	err_prop = RC_PID_TARGET_PF - pf;

	err_avg = spinfo->err_avg_sc >> RC_PID_SMOOTHING_SHIFT;
	spinfo->err_avg_sc = spinfo->err_avg_sc - err_avg + err_prop;
	err_int = spinfo->err_avg_sc >> RC_PID_SMOOTHING_SHIFT;

	err_der = pf - spinfo->last_pf;
	spinfo->last_pf = pf;

	/* Compute the controller output. */
	adj = (err_prop * pinfo->coeff_p + err_int * pinfo->coeff_i
	      + err_der * pinfo->coeff_d);

	/* We need to do an arithmetic right shift. ISO C says this is
	 * implementation defined for negative left operands. Hence, be
	 * careful to get it right, also for negative values. */
	adj = (adj < 0) ? -((-adj) >> (2 * RC_PID_ARITH_SHIFT)) :
			  adj >> (2 * RC_PID_ARITH_SHIFT);

	/* Change rate. */
	if (adj)
		rate_control_pid_adjust_rate(local, sta, adj);
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

	pinfo = kmalloc(sizeof(*pinfo), GFP_ATOMIC);

	pinfo->target = RC_PID_TARGET_PF;
	pinfo->coeff_p = RC_PID_COEFF_P;
	pinfo->coeff_i = RC_PID_COEFF_I;
	pinfo->coeff_d = RC_PID_COEFF_D;

	return pinfo;
}

static void rate_control_pid_free(void *priv)
{
	struct rc_pid_info *pinfo = priv;
	kfree(pinfo);
}

static void rate_control_pid_clear(void *priv)
{
}

static void *rate_control_pid_alloc_sta(void *priv, gfp_t gfp)
{
	struct rc_pid_sta_info *spinfo;

	spinfo = kzalloc(sizeof(*spinfo), gfp);

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
};
