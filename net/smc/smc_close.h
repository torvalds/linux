/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 * Socket Closing
 *
 * Copyright IBM Corp. 2016
 *
 * Author(s):  Ursula Braun <ubraun@linux.vnet.ibm.com>
 */

#ifndef SMC_CLOSE_H
#define SMC_CLOSE_H

#include <linux/workqueue.h>

#include "smc.h"

#define SMC_MAX_STREAM_WAIT_TIMEOUT		(2 * HZ)
#define SMC_CLOSE_SOCK_PUT_DELAY		HZ

void smc_close_wake_tx_prepared(struct smc_sock *smc);
int smc_close_active(struct smc_sock *smc);
int smc_close_shutdown_write(struct smc_sock *smc);
void smc_close_init(struct smc_sock *smc);
void smc_clcsock_release(struct smc_sock *smc);
int smc_close_abort(struct smc_connection *conn);
void smc_close_active_abort(struct smc_sock *smc);

#endif /* SMC_CLOSE_H */
