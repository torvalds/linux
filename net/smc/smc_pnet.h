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

struct smc_ib_device;

int smc_pnet_init(void) __init;
void smc_pnet_exit(void);
int smc_pnet_remove_by_ibdev(struct smc_ib_device *ibdev);
void smc_pnet_find_roce_resource(struct sock *sk,
				 struct smc_ib_device **smcibdev, u8 *ibport);

#endif
