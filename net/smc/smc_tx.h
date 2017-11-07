/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 * Manage send buffer
 *
 * Copyright IBM Corp. 2016
 *
 * Author(s):  Ursula Braun <ubraun@linux.vnet.ibm.com>
 */

#ifndef SMC_TX_H
#define SMC_TX_H

#include <linux/socket.h>
#include <linux/types.h>

#include "smc.h"
#include "smc_cdc.h"

static inline int smc_tx_prepared_sends(struct smc_connection *conn)
{
	union smc_host_cursor sent, prep;

	smc_curs_write(&sent, smc_curs_read(&conn->tx_curs_sent, conn), conn);
	smc_curs_write(&prep, smc_curs_read(&conn->tx_curs_prep, conn), conn);
	return smc_curs_diff(conn->sndbuf_size, &sent, &prep);
}

void smc_tx_init(struct smc_sock *smc);
int smc_tx_sendmsg(struct smc_sock *smc, struct msghdr *msg, size_t len);
int smc_tx_sndbuf_nonempty(struct smc_connection *conn);
void smc_tx_sndbuf_nonfull(struct smc_sock *smc);
void smc_tx_consumer_update(struct smc_connection *conn);

#endif /* SMC_TX_H */
