/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NETNS_SMC_H__
#define __NETNS_SMC_H__
#include <linux/mutex.h>
#include <linux/percpu.h>

struct smc_stats_rsn;
struct smc_stats;
struct netns_smc {
	/* per cpu counters for SMC */
	struct smc_stats __percpu	*smc_stats;
	/* protect fback_rsn */
	struct mutex			mutex_fback_rsn;
	struct smc_stats_rsn		*fback_rsn;

	bool				limit_smc_hs;	/* constraint on handshake */
};
#endif
