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

#include <net/smc.h>

#if IS_ENABLED(CONFIG_HAVE_PNETID)
#include <asm/pnet.h>
#endif

struct smc_ib_device;
struct smcd_dev;
struct smc_init_info;
struct smc_link_group;

/**
 * struct smc_pnettable - SMC PNET table anchor
 * @lock: Lock for list action
 * @pnetlist: List of PNETIDs
 */
struct smc_pnettable {
	rwlock_t lock;
	struct list_head pnetlist;
};

struct smc_pnetids_ndev {	/* list of pnetids for net devices in UP state*/
	struct list_head	list;
	rwlock_t		lock;
};

struct smc_pnetids_ndev_entry {
	struct list_head	list;
	u8			pnetid[SMC_MAX_PNETID_LEN];
	refcount_t		refcnt;
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
void smc_pnet_find_roce_resource(struct sock *sk, struct smc_init_info *ini);
void smc_pnet_find_ism_resource(struct sock *sk, struct smc_init_info *ini);
int smc_pnetid_by_table_ib(struct smc_ib_device *smcibdev, u8 ib_port);
int smc_pnetid_by_table_smcd(struct smcd_dev *smcd);
void smc_pnet_find_alt_roce(struct smc_link_group *lgr,
			    struct smc_init_info *ini,
			    struct smc_ib_device *known_dev);
bool smc_pnet_is_ndev_pnetid(struct net *net, u8 *pnetid);
#endif
