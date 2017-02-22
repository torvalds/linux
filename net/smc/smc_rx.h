/*
 * Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 * Manage RMBE
 *
 * Copyright IBM Corp. 2016
 *
 * Author(s):  Ursula Braun <ubraun@linux.vnet.ibm.com>
 */

#ifndef SMC_RX_H
#define SMC_RX_H

#include <linux/socket.h>
#include <linux/types.h>

#include "smc.h"

void smc_rx_init(struct smc_sock *smc);
int smc_rx_recvmsg(struct smc_sock *smc, struct msghdr *msg, size_t len,
		   int flags);

#endif /* SMC_RX_H */
