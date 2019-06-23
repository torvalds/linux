/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2015 Linaro Ltd.
 */

#ifndef __SOC_IMX_TIMER_H__
#define __SOC_IMX_TIMER_H__

enum imx_gpt_type {
	GPT_TYPE_IMX1,		/* i.MX1 */
	GPT_TYPE_IMX21,		/* i.MX21/27 */
	GPT_TYPE_IMX31,		/* i.MX31/35/25/37/51/6Q */
	GPT_TYPE_IMX6DL,	/* i.MX6DL/SX/SL */
};

/*
 * This is a stop-gap solution for clock drivers like imx1/imx21 which call
 * mxc_timer_init() to initialize timer for non-DT boot.  It can be removed
 * when these legacy non-DT support is converted or dropped.
 */
void mxc_timer_init(unsigned long pbase, int irq, enum imx_gpt_type type);

#endif  /* __SOC_IMX_TIMER_H__ */
