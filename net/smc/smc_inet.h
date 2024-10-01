/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 *  Definitions for the IPPROTO_SMC (socket related)

 *  Copyright IBM Corp. 2016
 *  Copyright (c) 2024, Alibaba Inc.
 *
 *  Author: D. Wythe <alibuda@linux.alibaba.com>
 */
#ifndef __INET_SMC
#define __INET_SMC

/* Initialize protocol registration on IPPROTO_SMC,
 * @return 0 on success
 */
int smc_inet_init(void);

void smc_inet_exit(void);

#endif /* __INET_SMC */
