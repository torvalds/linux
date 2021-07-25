/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 *  SMC Generic netlink operations
 *
 *  Copyright IBM Corp. 2020
 *
 *  Author(s):	Guvenc Gulce <guvenc@linux.ibm.com>
 */

#ifndef _SMC_NETLINK_H
#define _SMC_NETLINK_H

#include <net/netlink.h>
#include <net/genetlink.h>

extern struct genl_family smc_gen_nl_family;

struct smc_nl_dmp_ctx {
	int pos[3];
};

static inline struct smc_nl_dmp_ctx *smc_nl_dmp_ctx(struct netlink_callback *c)
{
	return (struct smc_nl_dmp_ctx *)c->ctx;
}

int smc_nl_init(void) __init;
void smc_nl_exit(void);

#endif
