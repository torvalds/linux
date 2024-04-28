/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Shared Memory Communications Direct over loopback-ism device.
 *
 *  SMC-D loopback-ism device structure definitions.
 *
 *  Copyright (c) 2024, Alibaba Inc.
 *
 *  Author: Wen Gu <guwen@linux.alibaba.com>
 *          Tony Lu <tonylu@linux.alibaba.com>
 *
 */

#ifndef _SMC_LOOPBACK_H
#define _SMC_LOOPBACK_H

#include <linux/device.h>
#include <linux/err.h>
#include <net/smc.h>

#if IS_ENABLED(CONFIG_SMC_LO)
#define SMC_LO_MAX_DMBS		5000
#define SMC_LO_RESERVED_CHID	0xFFFF

struct smc_lo_dev {
	struct smcd_dev *smcd;
	struct device dev;
	u16 chid;
	struct smcd_gid local_gid;
};

int smc_loopback_init(void);
void smc_loopback_exit(void);
#else
static inline int smc_loopback_init(void)
{
	return 0;
}

static inline void smc_loopback_exit(void)
{
}
#endif

#endif /* _SMC_LOOPBACK_H */
