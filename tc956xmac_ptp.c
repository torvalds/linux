/*
 * TC956X ethernet driver.
 *
 * tc956xmac_ptp.c
 *
 * Copyright (C) 2013  Vayavya Labs Pvt Ltd
 * Copyright (C) 2021 Toshiba Electronic Devices & Storage Corporation
 *
 * This file has been derived from the STMicro and Vayavya Linux driver,
 * and developed or modified for TC956X.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/*! History:
 *  20 Jan 2021 : Initial Version
 *  VERSION     : 00-01
 *
 *  15 Mar 2021 : Base lined
 *  VERSION     : 01-00
 */

#include "tc956xmac.h"
#include "tc956xmac_ptp.h"

#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
/**
 * tc956xmac_adjust_freq
 *
 * @ptp: pointer to ptp_clock_info structure
 * @ppb: desired period change in parts ber billion
 *
 * Description: this function will adjust the frequency of hardware clock.
 */
static int tc956xmac_adjust_freq(struct ptp_clock_info *ptp, s32 ppb)
{
	struct tc956xmac_priv *priv =
	    container_of(ptp, struct tc956xmac_priv, ptp_clock_ops);
	unsigned long flags;
	u32 diff, addend;
	int neg_adj = 0;
	u64 adj;

	if (ppb < 0) {
		neg_adj = 1;
		ppb = -ppb;
	}

	addend = priv->default_addend;
	adj = addend;
	adj *= ppb;
	diff = div_u64(adj, 1000000000ULL);
	addend = neg_adj ? (addend - diff) : (addend + diff);

	spin_lock_irqsave(&priv->ptp_lock, flags);
	tc956xmac_config_addend(priv, priv->ptpaddr, addend);
	spin_unlock_irqrestore(&priv->ptp_lock, flags);

	return 0;
}

/**
 * tc956xmac_adjust_time
 *
 * @ptp: pointer to ptp_clock_info structure
 * @delta: desired change in nanoseconds
 *
 * Description: this function will shift/adjust the hardware clock time.
 */
static int tc956xmac_adjust_time(struct ptp_clock_info *ptp, s64 delta)
{
	struct tc956xmac_priv *priv =
	    container_of(ptp, struct tc956xmac_priv, ptp_clock_ops);
	unsigned long flags;
	u32 sec, nsec;
	u32 quotient, reminder;
	int neg_adj = 0;
	bool xmac;

	xmac = priv->plat->has_gmac4 || priv->plat->has_xgmac;

	if (delta < 0) {
		neg_adj = 1;
		delta = -delta;
	}

	quotient = div_u64_rem(delta, 1000000000ULL, &reminder);
	sec = quotient;
	nsec = reminder;

	spin_lock_irqsave(&priv->ptp_lock, flags);
	tc956xmac_adjust_systime(priv, priv->ptpaddr, sec, nsec, neg_adj, xmac);
	spin_unlock_irqrestore(&priv->ptp_lock, flags);

	return 0;
}

/**
 * tc956xmac_get_time
 *
 * @ptp: pointer to ptp_clock_info structure
 * @ts: pointer to hold time/result
 *
 * Description: this function will read the current time from the
 * hardware clock and store it in @ts.
 */
static int tc956xmac_get_time(struct ptp_clock_info *ptp, struct timespec64 *ts)
{
	struct tc956xmac_priv *priv =
	    container_of(ptp, struct tc956xmac_priv, ptp_clock_ops);
	unsigned long flags;
	u64 ns = 0;

	spin_lock_irqsave(&priv->ptp_lock, flags);
	tc956xmac_get_systime(priv, priv->ptpaddr, &ns);
	spin_unlock_irqrestore(&priv->ptp_lock, flags);

	*ts = ns_to_timespec64(ns);

	return 0;
}

/**
 * tc956xmac_set_time
 *
 * @ptp: pointer to ptp_clock_info structure
 * @ts: time value to set
 *
 * Description: this function will set the current time on the
 * hardware clock.
 */
static int tc956xmac_set_time(struct ptp_clock_info *ptp,
			   const struct timespec64 *ts)
{
	struct tc956xmac_priv *priv =
	    container_of(ptp, struct tc956xmac_priv, ptp_clock_ops);
	unsigned long flags;

	spin_lock_irqsave(&priv->ptp_lock, flags);
	tc956xmac_init_systime(priv, priv->ptpaddr, ts->tv_sec, ts->tv_nsec);
	spin_unlock_irqrestore(&priv->ptp_lock, flags);

	return 0;
}

static int tc956xmac_enable(struct ptp_clock_info *ptp,
			 struct ptp_clock_request *rq, int on)
{
	struct tc956xmac_priv *priv =
	    container_of(ptp, struct tc956xmac_priv, ptp_clock_ops);
	struct tc956xmac_pps_cfg *cfg;
	int ret = -EOPNOTSUPP;
	unsigned long flags;

	switch (rq->type) {
	case PTP_CLK_REQ_PEROUT:
		/* Reject requests with unsupported flags */
		if (rq->perout.flags)
			return -EOPNOTSUPP;

		cfg = &priv->pps[rq->perout.index];

		cfg->start.tv_sec = rq->perout.start.sec;
		cfg->start.tv_nsec = rq->perout.start.nsec;
		cfg->period.tv_sec = rq->perout.period.sec;
		cfg->period.tv_nsec = rq->perout.period.nsec;

		spin_lock_irqsave(&priv->ptp_lock, flags);
		ret = tc956xmac_flex_pps_config(priv, priv->ioaddr,
					     rq->perout.index, cfg, on,
					     priv->sub_second_inc,
					     priv->systime_flags);
		spin_unlock_irqrestore(&priv->ptp_lock, flags);
		break;
	default:
		break;
	}

	return ret;
}
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */

/* structure describing a PTP hardware clock */
static struct ptp_clock_info tc956xmac_ptp_clock_ops = {
	.owner = THIS_MODULE,
	.name = "tc956xmac ptp",
	.max_adj = 62500000,
	.n_alarm = 0,
	.n_ext_ts = 0,
	.n_per_out = 0, /* will be overwritten in tc956xmac_ptp_register */
	.n_pins = 0,
	.pps = 0,
#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
	.adjfreq = tc956xmac_adjust_freq,
	.adjtime = tc956xmac_adjust_time,
	.gettime64 = tc956xmac_get_time,
	.settime64 = tc956xmac_set_time,
	.enable = tc956xmac_enable,
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */
};

/**
 * tc956xmac_ptp_register
 * @priv: driver private structure
 * Description: this function will register the ptp clock driver
 * to kernel. It also does some house keeping work.
 */
void tc956xmac_ptp_register(struct tc956xmac_priv *priv)
{
	int i;

	for (i = 0; i < priv->dma_cap.pps_out_num; i++) {
		if (i >= TC956XMAC_PPS_MAX)
			break;
		priv->pps[i].available = true;
	}

	if (priv->plat->ptp_max_adj)
		tc956xmac_ptp_clock_ops.max_adj = priv->plat->ptp_max_adj;

	tc956xmac_ptp_clock_ops.n_per_out = priv->dma_cap.pps_out_num;

	spin_lock_init(&priv->ptp_lock);
	priv->ptp_clock_ops = tc956xmac_ptp_clock_ops;

	priv->ptp_clock = ptp_clock_register(&priv->ptp_clock_ops,
					     priv->device);
	if (IS_ERR(priv->ptp_clock)) {
		netdev_err(priv->dev, "ptp_clock_register failed\n");
		priv->ptp_clock = NULL;
	} else if (priv->ptp_clock)
		netdev_info(priv->dev, "registered PTP clock\n");
}

/**
 * tc956xmac_ptp_unregister
 * @priv: driver private structure
 * Description: this function will remove/unregister the ptp clock driver
 * from the kernel.
 */
void tc956xmac_ptp_unregister(struct tc956xmac_priv *priv)
{
	if (priv->ptp_clock) {
		ptp_clock_unregister(priv->ptp_clock);
		priv->ptp_clock = NULL;
		pr_debug("Removed PTP HW clock successfully on %s\n",
			 priv->dev->name);
	}
}
