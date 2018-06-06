/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright(C) 2015 Linaro Limited. All rights reserved.
 * Author: Mathieu Poirier <mathieu.poirier@linaro.org>
 */

#ifndef _LINUX_CORESIGHT_PMU_H
#define _LINUX_CORESIGHT_PMU_H

#define CORESIGHT_ETM_PMU_NAME "cs_etm"
#define CORESIGHT_ETM_PMU_SEED  0x10

/* ETMv3.5/PTM's ETMCR config bit */
#define ETM_OPT_CYCACC  12
#define ETM_OPT_TS      28
#define ETM_OPT_RETSTK	29

/* ETMv4 CONFIGR programming bits for the ETM OPTs */
#define ETM4_CFG_BIT_CYCACC	4
#define ETM4_CFG_BIT_TS		11
#define ETM4_CFG_BIT_RETSTK	12

static inline int coresight_get_trace_id(int cpu)
{
	/*
	 * A trace ID of value 0 is invalid, so let's start at some
	 * random value that fits in 7 bits and go from there.  Since
	 * the common convention is to have data trace IDs be I(N) + 1,
	 * set instruction trace IDs as a function of the CPU number.
	 */
	return (CORESIGHT_ETM_PMU_SEED + (cpu * 2));
}

#endif
