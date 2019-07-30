/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 *  PNET table queries
 *
 *  Copyright IBM Corp. 2016
 *
 *  Author(s):  Thomas Richter <tmricht@linux.vnet.ibm.com>
 */

#ifndef _SMC_PNET_H
#define _SMC_PNET_H

#if IS_ENABLED(CONFIG_HAVE_PNETID)
#include <asm/pnet.h>
#endif

struct smc_ib_device;
struct smcd_dev;

/**
 * struct smc_pnettable - SMC PNET table anchor
 * @lock: Lock for list action
 * @pnetlist: List of PNETIDs
 */
struct smc_pnettable {
	rwlock_t lock;
	struct list_head pnetlist;
};

static inline int smc_pnetid_by_dev_port(struct device *dev,
					 unsigned short port, u8 *pnetid)
{
#if IS_ENABLED(CONFIG_HAVE_PNETID)
	return pnet_id_by_dev_port(dev, port, pnetid);
#else
	return -ENOENT;
#endif
}

int smc_pnet_init(void) __init;
int smc_pnet_net_init(struct net *net);
void smc_pnet_exit(void);
void smc_pnet_net_exit(struct net *net);
void smc_pnet_find_roce_resource(struct sock *sk,
				 struct smc_ib_device **smcibdev, u8 *ibport,
				 unsigned short vlan_id, u8 gid[]);
void smc_pnet_find_ism_resource(struct sock *sk, struct smcd_dev **smcismdev);

#endif
