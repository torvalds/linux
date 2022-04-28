/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Reset driver for the StarFive JH7110 SoC
 *
 * Copyright (C) 2022 samin <samin.guo@starfivetech.com>
 */

#ifndef __SOC_STARFIVE_JH7110_PMU_H__
#define __SOC_STARFIVE_JH7110_PMU_H__

#include <linux/types.h>

/* SW/HW Power domain id  */
#define POWER_DOMAIN_SYSTOP		(1 << 0)
#define POWER_DOMAIN_CPU		(1 << 1)
#define POWER_DOMAIN_GPUA		(1 << 2)
#define POWER_DOMAIN_VDEC		(1 << 3)
#define POWER_DOMAIN_JPU		POWER_DOMAIN_VDEC
#define POWER_DOMAIN_VOUT		(1 << 4)
#define POWER_DOMAIN_ISP		(1 << 5)
#define POWER_DOMAIN_VENC		(1 << 6)
#define POWER_DOMAIN_GPUB		(1 << 7)

enum PMU_HARD_EVENT {
	RTC_EVENT = 0,
	GMAC_EVENT,
	RFU,
	RGPIO0_EVENT,
	RGPIO1_EVENT,
	RGPIO2_EVENT,
	RGPIO3_EVENT,
	GPU_EVENT,
};

/*
 * @func: starfive_power_domain_set
 * @dec: power domain turn-on/off by software
 * @domain: power domain id
 *	POWER_DOMAIN_SYSTOP:
 *	POWER_DOMAIN_CPU
 *	POWER_DOMAIN_GPUA
 *	POWER_DOMAIN_VDEC
 *	POWER_DOMAIN_VOUT
 *	POWER_DOMAIN_ISP
 *	POWER_DOMAIN_VENC
 *	POWER_DOMAIN_GPUB
 * @enable: 1:enable 0:disable
 */
void starfive_power_domain_set(u32 domain, bool enable);

/*
 * @func: starfive_pmu_hw_encourage
 * @dec: power domain turn-on/off by HW envent(interrupt)
 * @domain: power domain id
 * @event: Hardware trigger event. PMU_HARD_EVENT:
	RTC_EVENT = 0,
	GMAC_EVENT,
	RFU,
	RGPIO0_EVENT,
	RGPIO1_EVENT,
	RGPIO2_EVENT,
	RGPIO3_EVENT,
	GPU_EVENT,
 * @enable: 1:enable 0:disable
 *
 * @for example:
 *	starfive_power_domain_set_by_hwevent(POWER_DOMAIN_VDEC, RTC_EVENT, 0);
 *
 *	Means that when the RTC alarm is interrupted, the hardware
 *	is triggered to close the power domain of VDEC.
 */
void starfive_power_domain_set_by_hwevent(u32 domain, u32 event, bool enable);

/*
 * @func: starfive_power_domain_order_on_get
 * @dec: PMU power domian power on order get.
 * @domian: powerff domain id
 */
int starfive_power_domain_order_on_get(u32 domain);

/*
 * @func: starfive_power_domain_order_off_get
 * @dec: PMU power domian power off order get.
 * @domian: power domain id
 */
int starfive_power_domain_order_off_get(u32 domain);

/*
 * @func: starfive_power_domain_order_on_set
 * @dec: PMU power domian power on order set.
 * @domian: powerff domain id
 * @order: the poweron order of domain
 */
void starfive_power_domain_order_on_set(u32 domain, u32 order);

/*
 * @func: starfive_power_domain_order_off_set
 * @dec: PMU power domian power off order set.
 * @domian: power domain id
 * @order: the poweroff order of domain
 */
void starfive_power_domain_order_off_set(u32 domain, u32 order);

void starfive_pmu_hw_event_turn_off_mask(u32 mask);

#endif /* __SOC_STARFIVE_JH7110_PMU_H__ */
