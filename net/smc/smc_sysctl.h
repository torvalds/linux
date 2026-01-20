/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 *  smc_sysctl.c: sysctl interface to SMC subsystem.
 *
 *  Copyright (c) 2022, Alibaba Inc.
 *
 *  Author: Tony Lu <tonylu@linux.alibaba.com>
 *
 */

#ifndef _SMC_SYSCTL_H
#define _SMC_SYSCTL_H

#ifdef CONFIG_SYSCTL

int __net_init smc_sysctl_net_init(struct net *net);
void __net_exit smc_sysctl_net_exit(struct net *net);

#else

static inline int smc_sysctl_net_init(struct net *net)
{
	net->smc.sysctl_autocorking_size = SMC_AUTOCORKING_DEFAULT_SIZE;
	net->smc.sysctl_max_links_per_lgr = SMC_LINKS_PER_LGR_MAX_PREFER;
	net->smc.sysctl_max_conns_per_lgr = SMC_CONN_PER_LGR_PREFER;
	net->smc.sysctl_smcr_max_send_wr = SMCR_MAX_SEND_WR_DEF;
	net->smc.sysctl_smcr_max_recv_wr = SMCR_MAX_RECV_WR_DEF;
	return 0;
}

static inline void smc_sysctl_net_exit(struct net *net) { }

#endif /* CONFIG_SYSCTL */

#endif /* _SMC_SYSCTL_H */
