/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 *  Definitions for LLC (link layer control) message handling
 *
 *  Copyright IBM Corp. 2016
 *
 *  Author(s):  Klaus Wacker <Klaus.Wacker@de.ibm.com>
 *              Ursula Braun <ubraun@linux.vnet.ibm.com>
 */

#ifndef SMC_LLC_H
#define SMC_LLC_H

#include "smc_wr.h"

#define SMC_LLC_FLAG_RESP		0x80

#define SMC_LLC_WAIT_FIRST_TIME		(5 * HZ)

enum smc_llc_reqresp {
	SMC_LLC_REQ,
	SMC_LLC_RESP
};

enum smc_llc_msg_type {
	SMC_LLC_CONFIRM_LINK		= 0x01,
};

/* transmit */
int smc_llc_send_confirm_link(struct smc_link *lnk, u8 mac[], union ib_gid *gid,
			      enum smc_llc_reqresp reqresp);
int smc_llc_init(void) __init;

#endif /* SMC_LLC_H */
