// SPDX-License-Identifier: GPL-2.0-only
/*
 * Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 * SMC statistics netlink routines
 *
 * Copyright IBM Corp. 2021
 *
 * Author(s):  Guvenc Gulce
 */
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/percpu.h>
#include <linux/ctype.h>
#include "smc_stats.h"

/* serialize fallback reason statistic gathering */
DEFINE_MUTEX(smc_stat_fback_rsn);
struct smc_stats __percpu *smc_stats;	/* per cpu counters for SMC */
struct smc_stats_reason fback_rsn;

int __init smc_stats_init(void)
{
	memset(&fback_rsn, 0, sizeof(fback_rsn));
	smc_stats = alloc_percpu(struct smc_stats);
	if (!smc_stats)
		return -ENOMEM;

	return 0;
}

void smc_stats_exit(void)
{
	free_percpu(smc_stats);
}
