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

int smc_sysctl_init(void);
void smc_sysctl_exit(void);

#else

int smc_sysctl_init(void)
{
	return 0;
}

void smc_sysctl_exit(void) { }

#endif /* CONFIG_SYSCTL */

#endif /* _SMC_SYSCTL_H */
