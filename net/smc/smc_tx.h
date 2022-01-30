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

	smc_curs_copy(&sent, &conn->tx_curs_sent, conn);
	smc_curs_copy(&prep, &conn->tx_curs_prep, conn);
	return smc_curs_diff(conn->sndbuf_desc->len, &sent, &prep);
}

void smc_tx_pending(struct smc_connection *conn);
void smc_tx_work(struct work_struct *work);
void smc_tx_init(struct smc_sock *smc);
int smc_tx_sendmsg(struct smc_sock *smc, struct msghdr *msg, size_t len);
int smc_tx_sendpage(struct smc_sock *smc, struct page *page, int offset,
		    size_t size, int flags);
int smc_tx_sndbuf_nonempty(struct smc_connection *conn);
void smc_tx_sndbuf_nonfull(struct smc_sock *smc);
void smc_tx_consumer_update(struct smc_connection *conn, bool force);
int smcd_tx_ism_write(struct smc_connection *conn, void *data, size_t len,
		      u32 offset, int signal);

#endif /* SMC_TX_H */
