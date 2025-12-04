/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 *  Generic hook for SMC handshake flow.
 *
 *  Copyright IBM Corp. 2016
 *  Copyright (c) 2025, Alibaba Inc.
 *
 *  Author: D. Wythe <alibuda@linux.alibaba.com>
 */

#ifndef __SMC_HS_CTRL
#define __SMC_HS_CTRL

#include <net/smc.h>

/* Find hs_ctrl by the target name, which required to be a c-string.
 * Return NULL if no such ctrl was found,otherwise, return a valid ctrl.
 *
 * Note: Caller MUST ensure it's was invoked under rcu_read_lock.
 */
struct smc_hs_ctrl *smc_hs_ctrl_find_by_name(const char *name);

#if IS_ENABLED(CONFIG_SMC_HS_CTRL_BPF)
int bpf_smc_hs_ctrl_init(void);
#else
static inline int bpf_smc_hs_ctrl_init(void) { return 0; }
#endif /* CONFIG_SMC_HS_CTRL_BPF */

#endif /* __SMC_HS_CTRL */
