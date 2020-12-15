/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/*
 * Microsemi Ocelot Switch driver
 *
 * License: Dual MIT/GPL
 * Copyright (c) 2017 Microsemi Corporation
 * Copyright 2020 NXP
 */

#ifndef _MSCC_OCELOT_PTP_H_
#define _MSCC_OCELOT_PTP_H_

#include <linux/ptp_clock_kernel.h>
#include <soc/mscc/ocelot.h>

#define PTP_PIN_CFG_RSZ			0x20
#define PTP_PIN_TOD_SEC_MSB_RSZ		PTP_PIN_CFG_RSZ
#define PTP_PIN_TOD_SEC_LSB_RSZ		PTP_PIN_CFG_RSZ
#define PTP_PIN_TOD_NSEC_RSZ		PTP_PIN_CFG_RSZ
#define PTP_PIN_WF_HIGH_PERIOD_RSZ	PTP_PIN_CFG_RSZ
#define PTP_PIN_WF_LOW_PERIOD_RSZ	PTP_PIN_CFG_RSZ

#define PTP_PIN_CFG_DOM			BIT(0)
#define PTP_PIN_CFG_SYNC		BIT(2)
#define PTP_PIN_CFG_ACTION(x)		((x) << 3)
#define PTP_PIN_CFG_ACTION_MASK		PTP_PIN_CFG_ACTION(0x7)

enum {
	PTP_PIN_ACTION_IDLE = 0,
	PTP_PIN_ACTION_LOAD,
	PTP_PIN_ACTION_SAVE,
	PTP_PIN_ACTION_CLOCK,
	PTP_PIN_ACTION_DELTA,
	PTP_PIN_ACTION_NOSYNC,
	PTP_PIN_ACTION_SYNC,
};

#define PTP_CFG_MISC_PTP_EN		BIT(2)

#define PSEC_PER_SEC			1000000000000LL

#define PTP_CFG_CLK_ADJ_CFG_ENA		BIT(0)
#define PTP_CFG_CLK_ADJ_CFG_DIR		BIT(1)

#define PTP_CFG_CLK_ADJ_FREQ_NS		BIT(30)

int ocelot_ptp_gettime64(struct ptp_clock_info *ptp, struct timespec64 *ts);
int ocelot_ptp_settime64(struct ptp_clock_info *ptp,
			 const struct timespec64 *ts);
int ocelot_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta);
int ocelot_ptp_adjfine(struct ptp_clock_info *ptp, long scaled_ppm);
int ocelot_ptp_verify(struct ptp_clock_info *ptp, unsigned int pin,
		      enum ptp_pin_function func, unsigned int chan);
int ocelot_ptp_enable(struct ptp_clock_info *ptp,
		      struct ptp_clock_request *rq, int on);
int ocelot_init_timestamp(struct ocelot *ocelot, struct ptp_clock_info *info);
int ocelot_deinit_timestamp(struct ocelot *ocelot);
#endif
