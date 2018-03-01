// SPDX-License-Identifier: GPL-2.0
/*
 *  Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 *  Link Layer Control (LLC)
 *
 *  For now, we only support the necessary "confirm link" functionality
 *  which happens for the first RoCE link after successful CLC handshake.
 *
 *  Copyright IBM Corp. 2016
 *
 *  Author(s):  Klaus Wacker <Klaus.Wacker@de.ibm.com>
 *              Ursula Braun <ubraun@linux.vnet.ibm.com>
 */

#include <net/tcp.h>
#include <rdma/ib_verbs.h>

#include "smc.h"
#include "smc_core.h"
#include "smc_clc.h"
#include "smc_llc.h"

#define SMC_LLC_DATA_LEN		40

struct smc_llc_hdr {
	struct smc_wr_rx_hdr common;
	u8 length;	/* 44 */
	u8 reserved;
	u8 flags;
};

struct smc_llc_msg_confirm_link {	/* type 0x01 */
	struct smc_llc_hdr hd;
	u8 sender_mac[ETH_ALEN];
	u8 sender_gid[SMC_GID_SIZE];
	u8 sender_qp_num[3];
	u8 link_num;
	u8 link_uid[SMC_LGR_ID_SIZE];
	u8 max_links;
	u8 reserved[9];
};

struct smc_llc_msg_test_link {		/* type 0x07 */
	struct smc_llc_hdr hd;
	u8 user_data[16];
	u8 reserved[24];
};

union smc_llc_msg {
	struct smc_llc_msg_confirm_link confirm_link;
	struct smc_llc_msg_test_link test_link;
	struct {
		struct smc_llc_hdr hdr;
		u8 data[SMC_LLC_DATA_LEN];
	} raw;
};

#define SMC_LLC_FLAG_RESP		0x80

/********************************** send *************************************/

struct smc_llc_tx_pend {
};

/* handler for send/transmission completion of an LLC msg */
static void smc_llc_tx_handler(struct smc_wr_tx_pend_priv *pend,
			       struct smc_link *link,
			       enum ib_wc_status wc_status)
{
	/* future work: handle wc_status error for recovery and failover */
}

/**
 * smc_llc_add_pending_send() - add LLC control message to pending WQE transmits
 * @link: Pointer to SMC link used for sending LLC control message.
 * @wr_buf: Out variable returning pointer to work request payload buffer.
 * @pend: Out variable returning pointer to private pending WR tracking.
 *	  It's the context the transmit complete handler will get.
 *
 * Reserves and pre-fills an entry for a pending work request send/tx.
 * Used by mid-level smc_llc_send_msg() to prepare for later actual send/tx.
 * Can sleep due to smc_get_ctrl_buf (if not in softirq context).
 *
 * Return: 0 on success, otherwise an error value.
 */
static int smc_llc_add_pending_send(struct smc_link *link,
				    struct smc_wr_buf **wr_buf,
				    struct smc_wr_tx_pend_priv **pend)
{
	int rc;

	rc = smc_wr_tx_get_free_slot(link, smc_llc_tx_handler, wr_buf, pend);
	if (rc < 0)
		return rc;
	BUILD_BUG_ON_MSG(
		sizeof(union smc_llc_msg) > SMC_WR_BUF_SIZE,
		"must increase SMC_WR_BUF_SIZE to at least sizeof(struct smc_llc_msg)");
	BUILD_BUG_ON_MSG(
		sizeof(union smc_llc_msg) != SMC_WR_TX_SIZE,
		"must adapt SMC_WR_TX_SIZE to sizeof(struct smc_llc_msg); if not all smc_wr upper layer protocols use the same message size any more, must start to set link->wr_tx_sges[i].length on each individual smc_wr_tx_send()");
	BUILD_BUG_ON_MSG(
		sizeof(struct smc_llc_tx_pend) > SMC_WR_TX_PEND_PRIV_SIZE,
		"must increase SMC_WR_TX_PEND_PRIV_SIZE to at least sizeof(struct smc_llc_tx_pend)");
	return 0;
}

/* high-level API to send LLC confirm link */
int smc_llc_send_confirm_link(struct smc_link *link, u8 mac[],
			      union ib_gid *gid,
			      enum smc_llc_reqresp reqresp)
{
	struct smc_link_group *lgr = container_of(link, struct smc_link_group,
						  lnk[SMC_SINGLE_LINK]);
	struct smc_llc_msg_confirm_link *confllc;
	struct smc_wr_tx_pend_priv *pend;
	struct smc_wr_buf *wr_buf;
	int rc;

	rc = smc_llc_add_pending_send(link, &wr_buf, &pend);
	if (rc)
		return rc;
	confllc = (struct smc_llc_msg_confirm_link *)wr_buf;
	memset(confllc, 0, sizeof(*confllc));
	confllc->hd.common.type = SMC_LLC_CONFIRM_LINK;
	confllc->hd.length = sizeof(struct smc_llc_msg_confirm_link);
	if (reqresp == SMC_LLC_RESP)
		confllc->hd.flags |= SMC_LLC_FLAG_RESP;
	memcpy(confllc->sender_mac, mac, ETH_ALEN);
	memcpy(confllc->sender_gid, gid, SMC_GID_SIZE);
	hton24(confllc->sender_qp_num, link->roce_qp->qp_num);
	/* confllc->link_num = SMC_SINGLE_LINK; already done by memset above */
	memcpy(confllc->link_uid, lgr->id, SMC_LGR_ID_SIZE);
	confllc->max_links = SMC_LINKS_PER_LGR_MAX;
	/* send llc message */
	rc = smc_wr_tx_send(link, pend);
	return rc;
}

/* send LLC test link request or response */
int smc_llc_send_test_link(struct smc_link *link, u8 user_data[16],
			   enum smc_llc_reqresp reqresp)
{
	struct smc_llc_msg_test_link *testllc;
	struct smc_wr_tx_pend_priv *pend;
	struct smc_wr_buf *wr_buf;
	int rc;

	rc = smc_llc_add_pending_send(link, &wr_buf, &pend);
	if (rc)
		return rc;
	testllc = (struct smc_llc_msg_test_link *)wr_buf;
	memset(testllc, 0, sizeof(*testllc));
	testllc->hd.common.type = SMC_LLC_TEST_LINK;
	testllc->hd.length = sizeof(struct smc_llc_msg_test_link);
	if (reqresp == SMC_LLC_RESP)
		testllc->hd.flags |= SMC_LLC_FLAG_RESP;
	memcpy(testllc->user_data, user_data, sizeof(testllc->user_data));
	/* send llc message */
	rc = smc_wr_tx_send(link, pend);
	return rc;
}

/********************************* receive ***********************************/

static void smc_llc_rx_confirm_link(struct smc_link *link,
				    struct smc_llc_msg_confirm_link *llc)
{
	struct smc_link_group *lgr;

	lgr = container_of(link, struct smc_link_group, lnk[SMC_SINGLE_LINK]);
	if (llc->hd.flags & SMC_LLC_FLAG_RESP) {
		if (lgr->role == SMC_SERV)
			complete(&link->llc_confirm_resp);
	} else {
		if (lgr->role == SMC_CLNT) {
			link->link_id = llc->link_num;
			complete(&link->llc_confirm);
		}
	}
}

static void smc_llc_rx_test_link(struct smc_link *link,
				 struct smc_llc_msg_test_link *llc)
{
	if (llc->hd.flags & SMC_LLC_FLAG_RESP) {
		/* unused as long as we don't send this type of msg */
	} else {
		smc_llc_send_test_link(link, llc->user_data, SMC_LLC_RESP);
	}
}

static void smc_llc_rx_handler(struct ib_wc *wc, void *buf)
{
	struct smc_link *link = (struct smc_link *)wc->qp->qp_context;
	union smc_llc_msg *llc = buf;

	if (wc->byte_len < sizeof(*llc))
		return; /* short message */
	if (llc->raw.hdr.length != sizeof(*llc))
		return; /* invalid message */

	switch (llc->raw.hdr.common.type) {
	case SMC_LLC_TEST_LINK:
		smc_llc_rx_test_link(link, &llc->test_link);
		break;
	case SMC_LLC_CONFIRM_LINK:
		smc_llc_rx_confirm_link(link, &llc->confirm_link);
		break;
	}
}

/***************************** init, exit, misc ******************************/

static struct smc_wr_rx_handler smc_llc_rx_handlers[] = {
	{
		.handler	= smc_llc_rx_handler,
		.type		= SMC_LLC_CONFIRM_LINK
	},
	{
		.handler	= smc_llc_rx_handler,
		.type		= SMC_LLC_TEST_LINK
	},
	{
		.handler	= NULL,
	}
};

int __init smc_llc_init(void)
{
	struct smc_wr_rx_handler *handler;
	int rc = 0;

	for (handler = smc_llc_rx_handlers; handler->handler; handler++) {
		INIT_HLIST_NODE(&handler->list);
		rc = smc_wr_rx_register_handler(handler);
		if (rc)
			break;
	}
	return rc;
}
