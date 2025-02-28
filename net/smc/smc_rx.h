/* SPDX-License-Identifier: GPL-2.0 */
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

int smc_rx_recvmsg(struct smc_sock *smc, struct msghdr *msg,
		   struct pipe_inode_info *pipe, size_t len, int flags);
int smc_rx_wait(struct smc_sock *smc, long *timeo, size_t peeked,
		int (*fcrit)(struct smc_connection *conn, size_t baseline));
static inline int smc_rx_data_available(struct smc_connection *conn, size_t peeked)
{
	return atomic_read(&conn->bytes_to_rcv) - peeked;
}

#endif /* SMC_RX_H */
