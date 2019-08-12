/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2016-17 Synopsys, Inc. (www.synopsys.com)
 */

#ifndef __SOC_ARC_TIMERS_H
#define __SOC_ARC_TIMERS_H

#include <soc/arc/aux.h>

/* Timer related Aux registers */
#define ARC_REG_TIMER0_LIMIT	0x23	/* timer 0 limit */
#define ARC_REG_TIMER0_CTRL	0x22	/* timer 0 control */
#define ARC_REG_TIMER0_CNT	0x21	/* timer 0 count */
#define ARC_REG_TIMER1_LIMIT	0x102	/* timer 1 limit */
#define ARC_REG_TIMER1_CTRL	0x101	/* timer 1 control */
#define ARC_REG_TIMER1_CNT	0x100	/* timer 1 count */

/* CTRL reg bits */
#define TIMER_CTRL_IE	        (1 << 0) /* Interrupt when Count reaches limit */
#define TIMER_CTRL_NH	        (1 << 1) /* Count only when CPU NOT halted */

#define ARC_TIMERN_MAX		0xFFFFFFFF

#define ARC_REG_TIMERS_BCR	0x75

struct bcr_timer {
#ifdef CONFIG_CPU_BIG_ENDIAN
	unsigned int pad2:15, rtsc:1, pad1:5, rtc:1, t1:1, t0:1, ver:8;
#else
	unsigned int ver:8, t0:1, t1:1, rtc:1, pad1:5, rtsc:1, pad2:15;
#endif
};

#endif
