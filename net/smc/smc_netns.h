/* SPDX-License-Identifier: GPL-2.0 */
/* Shared Memory Communications
 *
 * Network namespace definitions.
 *
 * Copyright IBM Corp. 2018
 */

#ifndef SMC_NETNS_H
#define SMC_NETNS_H

#include "smc_pnet.h"

extern unsigned int smc_net_id;

/* per-network namespace private data */
struct smc_net {
	struct smc_pnettable pnettable;
	struct smc_pnetids_ndev pnetids_ndev;
};
#endif
