// SPDX-License-Identifier: GPL-2.0
/*
 *  Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 *  Link Layer Control (LLC)
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
#if defined(__BIG_ENDIAN_BITFIELD)
	u8 reserved:4,
	   add_link_rej_rsn:4;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8 add_link_rej_rsn:4,
	   reserved:4;
#endif
	u8 flags;
};

#define SMC_LLC_FLAG_NO_RMBE_EYEC	0x03

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

#define SMC_LLC_FLAG_ADD_LNK_REJ	0x40
#define SMC_LLC_REJ_RSN_NO_ALT_PATH	1

#define SMC_LLC_ADD_LNK_MAX_LINKS	2

struct smc_llc_msg_add_link {		/* type 0x02 */
	struct smc_llc_hdr hd;
	u8 sender_mac[ETH_ALEN];
	u8 reserved2[2];
	u8 sender_gid[SMC_GID_SIZE];
	u8 sender_qp_num[3];
	u8 link_num;
	u8 flags2;	/* QP mtu */
	u8 initial_psn[3];
	u8 reserved[8];
};

#define SMC_LLC_FLAG_DEL_LINK_ALL	0x40
#define SMC_LLC_FLAG_DEL_LINK_ORDERLY	0x20

struct smc_llc_msg_del_link {		/* type 0x04 */
	struct smc_llc_hdr hd;
	u8 link_num;
	__be32 reason;
	u8 reserved[35];
} __packed;			/* format defined in RFC7609 */

struct smc_llc_msg_test_link {		/* type 0x07 */
	struct smc_llc_hdr hd;
	u8 user_data[16];
	u8 reserved[24];
};

struct smc_rmb_rtoken {
	union {
		u8 num_rkeys;	/* first rtoken byte of CONFIRM LINK msg */
				/* is actually the num of rtokens, first */
				/* rtoken is always for the current link */
		u8 link_id;	/* link id of the rtoken */
	};
	__be32 rmb_key;
	__be64 rmb_vaddr;
} __packed;			/* format defined in RFC7609 */

#define SMC_LLC_RKEYS_PER_MSG	3

struct smc_llc_msg_confirm_rkey {	/* type 0x06 */
	struct smc_llc_hdr hd;
	struct smc_rmb_rtoken rtoken[SMC_LLC_RKEYS_PER_MSG];
	u8 reserved;
};

struct smc_llc_msg_confirm_rkey_cont {	/* type 0x08 */
	struct smc_llc_hdr hd;
	u8 num_rkeys;
	struct smc_rmb_rtoken rtoken[SMC_LLC_RKEYS_PER_MSG];
};

#define SMC_LLC_DEL_RKEY_MAX	8
#define SMC_LLC_FLAG_RKEY_NEG	0x20

struct smc_llc_msg_delete_rkey {	/* type 0x09 */
	struct smc_llc_hdr hd;
	u8 num_rkeys;
	u8 err_mask;
	u8 reserved[2];
	__be32 rkey[8];
	u8 reserved2[4];
};

union smc_llc_msg {
	struct smc_llc_msg_confirm_link confirm_link;
	struct smc_llc_msg_add_link add_link;
	struct smc_llc_msg_del_link delete_link;

	struct smc_llc_msg_confirm_rkey confirm_rkey;
	struct smc_llc_msg_confirm_rkey_cont confirm_rkey_cont;
	struct smc_llc_msg_delete_rkey delete_rkey;

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
	confllc->hd.flags |= SMC_LLC_FLAG_NO_RMBE_EYEC;
	if (reqresp == SMC_LLC_RESP)
		confllc->hd.flags |= SMC_LLC_FLAG_RESP;
	memcpy(confllc->sender_mac, mac, ETH_ALEN);
	memcpy(confllc->sender_gid, gid, SMC_GID_SIZE);
	hton24(confllc->sender_qp_num, link->roce_qp->qp_num);
	confllc->link_num = link->link_id;
	memcpy(confllc->link_uid, lgr->id, SMC_LGR_ID_SIZE);
	confllc->max_links = SMC_LLC_ADD_LNK_MAX_LINKS; /* enforce peer resp. */
	/* send llc message */
	rc = smc_wr_tx_send(link, pend);
	return rc;
}

/* send ADD LINK request or response */
int smc_llc_send_add_link(struct smc_link *link, u8 mac[],
			  union ib_gid *gid,
			  enum smc_llc_reqresp reqresp)
{
	struct smc_llc_msg_add_link *addllc;
	struct smc_wr_tx_pend_priv *pend;
	struct smc_wr_buf *wr_buf;
	int rc;

	rc = smc_llc_add_pending_send(link, &wr_buf, &pend);
	if (rc)
		return rc;
	addllc = (struct smc_llc_msg_add_link *)wr_buf;
	memset(addllc, 0, sizeof(*addllc));
	addllc->hd.common.type = SMC_LLC_ADD_LINK;
	addllc->hd.length = sizeof(struct smc_llc_msg_add_link);
	if (reqresp == SMC_LLC_RESP) {
		addllc->hd.flags |= SMC_LLC_FLAG_RESP;
		/* always reject more links for now */
		addllc->hd.flags |= SMC_LLC_FLAG_ADD_LNK_REJ;
		addllc->hd.add_link_rej_rsn = SMC_LLC_REJ_RSN_NO_ALT_PATH;
	}
	memcpy(addllc->sender_mac, mac, ETH_ALEN);
	memcpy(addllc->sender_gid, gid, SMC_GID_SIZE);
	/* send llc message */
	rc = smc_wr_tx_send(link, pend);
	return rc;
}

/* send DELETE LINK request or response */
int smc_llc_send_delete_link(struct smc_link *link,
			     enum smc_llc_reqresp reqresp)
{
	struct smc_llc_msg_del_link *delllc;
	struct smc_wr_tx_pend_priv *pend;
	struct smc_wr_buf *wr_buf;
	int rc;

	rc = smc_llc_add_pending_send(link, &wr_buf, &pend);
	if (rc)
		return rc;
	delllc = (struct smc_llc_msg_del_link *)wr_buf;
	memset(delllc, 0, sizeof(*delllc));
	delllc->hd.common.type = SMC_LLC_DELETE_LINK;
	delllc->hd.length = sizeof(struct smc_llc_msg_add_link);
	if (reqresp == SMC_LLC_RESP)
		delllc->hd.flags |= SMC_LLC_FLAG_RESP;
	/* DEL_LINK_ALL because only 1 link supported */
	delllc->hd.flags |= SMC_LLC_FLAG_DEL_LINK_ALL;
	delllc->hd.flags |= SMC_LLC_FLAG_DEL_LINK_ORDERLY;
	delllc->link_num = link->link_id;
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

/* send a prepared message */
static int smc_llc_send_message(struct smc_link *link, void *llcbuf, int llclen)
{
	struct smc_wr_tx_pend_priv *pend;
	struct smc_wr_buf *wr_buf;
	int rc;

	rc = smc_llc_add_pending_send(link, &wr_buf, &pend);
	if (rc)
		return rc;
	memcpy(wr_buf, llcbuf, llclen);
	/* send llc message */
	rc = smc_wr_tx_send(link, pend);
	return rc;
}

/********************************* receive ***********************************/

static void smc_llc_rx_confirm_link(struct smc_link *link,
				    struct smc_llc_msg_confirm_link *llc)
{
	struct smc_link_group *lgr;
	int conf_rc;

	lgr = container_of(link, struct smc_link_group, lnk[SMC_SINGLE_LINK]);

	/* RMBE eyecatchers are not supported */
	if (llc->hd.flags & SMC_LLC_FLAG_NO_RMBE_EYEC)
		conf_rc = 0;
	else
		conf_rc = ENOTSUPP;

	if (llc->hd.flags & SMC_LLC_FLAG_RESP) {
		if (lgr->role == SMC_SERV &&
		    link->state == SMC_LNK_ACTIVATING) {
			link->llc_confirm_resp_rc = conf_rc;
			complete(&link->llc_confirm_resp);
		}
	} else {
		if (lgr->role == SMC_CLNT &&
		    link->state == SMC_LNK_ACTIVATING) {
			link->llc_confirm_rc = conf_rc;
			link->link_id = llc->link_num;
			complete(&link->llc_confirm);
		}
	}
}

static void smc_llc_rx_add_link(struct smc_link *link,
				struct smc_llc_msg_add_link *llc)
{
	struct smc_link_group *lgr = container_of(link, struct smc_link_group,
						  lnk[SMC_SINGLE_LINK]);

	if (llc->hd.flags & SMC_LLC_FLAG_RESP) {
		if (link->state == SMC_LNK_ACTIVATING)
			complete(&link->llc_add_resp);
	} else {
		if (link->state == SMC_LNK_ACTIVATING) {
			complete(&link->llc_add);
			return;
		}

		if (lgr->role == SMC_SERV) {
			smc_llc_send_add_link(link,
					link->smcibdev->mac[link->ibport - 1],
					&link->smcibdev->gid[link->ibport - 1],
					SMC_LLC_REQ);

		} else {
			smc_llc_send_add_link(link,
					link->smcibdev->mac[link->ibport - 1],
					&link->smcibdev->gid[link->ibport - 1],
					SMC_LLC_RESP);
		}
	}
}

static void smc_llc_rx_delete_link(struct smc_link *link,
				   struct smc_llc_msg_del_link *llc)
{
	struct smc_link_group *lgr = container_of(link, struct smc_link_group,
						  lnk[SMC_SINGLE_LINK]);

	if (llc->hd.flags & SMC_LLC_FLAG_RESP) {
		if (lgr->role == SMC_SERV)
			smc_lgr_terminate(lgr);
	} else {
		if (lgr->role == SMC_SERV) {
			smc_lgr_forget(lgr);
			smc_llc_send_delete_link(link, SMC_LLC_REQ);
		} else {
			smc_llc_send_delete_link(link, SMC_LLC_RESP);
			smc_lgr_terminate(lgr);
		}
	}
}

static void smc_llc_rx_test_link(struct smc_link *link,
				 struct smc_llc_msg_test_link *llc)
{
	if (llc->hd.flags & SMC_LLC_FLAG_RESP) {
		if (link->state == SMC_LNK_ACTIVE)
			complete(&link->llc_testlink_resp);
	} else {
		smc_llc_send_test_link(link, llc->user_data, SMC_LLC_RESP);
	}
}

static void smc_llc_rx_confirm_rkey(struct smc_link *link,
				    struct smc_llc_msg_confirm_rkey *llc)
{
	struct smc_link_group *lgr;
	int rc;

	lgr = container_of(link, struct smc_link_group, lnk[SMC_SINGLE_LINK]);

	if (llc->hd.flags & SMC_LLC_FLAG_RESP) {
		/* unused as long as we don't send this type of msg */
	} else {
		rc = smc_rtoken_add(lgr,
				    llc->rtoken[0].rmb_vaddr,
				    llc->rtoken[0].rmb_key);

		/* ignore rtokens for other links, we have only one link */

		llc->hd.flags |= SMC_LLC_FLAG_RESP;
		if (rc < 0)
			llc->hd.flags |= SMC_LLC_FLAG_RKEY_NEG;
		smc_llc_send_message(link, (void *)llc, sizeof(*llc));
	}
}

static void smc_llc_rx_confirm_rkey_cont(struct smc_link *link,
				      struct smc_llc_msg_confirm_rkey_cont *llc)
{
	if (llc->hd.flags & SMC_LLC_FLAG_RESP) {
		/* unused as long as we don't send this type of msg */
	} else {
		/* ignore rtokens for other links, we have only one link */
		llc->hd.flags |= SMC_LLC_FLAG_RESP;
		smc_llc_send_message(link, (void *)llc, sizeof(*llc));
	}
}

static void smc_llc_rx_delete_rkey(struct smc_link *link,
				   struct smc_llc_msg_delete_rkey *llc)
{
	struct smc_link_group *lgr;
	u8 err_mask = 0;
	int i, max;

	lgr = container_of(link, struct smc_link_group, lnk[SMC_SINGLE_LINK]);

	if (llc->hd.flags & SMC_LLC_FLAG_RESP) {
		/* unused as long as we don't send this type of msg */
	} else {
		max = min_t(u8, llc->num_rkeys, SMC_LLC_DEL_RKEY_MAX);
		for (i = 0; i < max; i++) {
			if (smc_rtoken_delete(lgr, llc->rkey[i]))
				err_mask |= 1 << (SMC_LLC_DEL_RKEY_MAX - 1 - i);
		}

		if (err_mask) {
			llc->hd.flags |= SMC_LLC_FLAG_RKEY_NEG;
			llc->err_mask = err_mask;
		}

		llc->hd.flags |= SMC_LLC_FLAG_RESP;
		smc_llc_send_message(link, (void *)llc, sizeof(*llc));
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
	case SMC_LLC_ADD_LINK:
		smc_llc_rx_add_link(link, &llc->add_link);
		break;
	case SMC_LLC_DELETE_LINK:
		smc_llc_rx_delete_link(link, &llc->delete_link);
		break;
	case SMC_LLC_CONFIRM_RKEY:
		smc_llc_rx_confirm_rkey(link, &llc->confirm_rkey);
		break;
	case SMC_LLC_CONFIRM_RKEY_CONT:
		smc_llc_rx_confirm_rkey_cont(link, &llc->confirm_rkey_cont);
		break;
	case SMC_LLC_DELETE_RKEY:
		smc_llc_rx_delete_rkey(link, &llc->delete_rkey);
		break;
	}
}

/***************************** worker ****************************************/

static void smc_llc_testlink_work(struct work_struct *work)
{
	struct smc_link *link = container_of(to_delayed_work(work),
					     struct smc_link, llc_testlink_wrk);
	unsigned long next_interval;
	struct smc_link_group *lgr;
	unsigned long expire_time;
	u8 user_data[16] = { 0 };
	int rc;

	lgr = container_of(link, struct smc_link_group, lnk[SMC_SINGLE_LINK]);
	if (link->state != SMC_LNK_ACTIVE)
		return;		/* don't reschedule worker */
	expire_time = link->wr_rx_tstamp + link->llc_testlink_time;
	if (time_is_after_jiffies(expire_time)) {
		next_interval = expire_time - jiffies;
		goto out;
	}
	reinit_completion(&link->llc_testlink_resp);
	smc_llc_send_test_link(link, user_data, SMC_LLC_REQ);
	/* receive TEST LINK response over RoCE fabric */
	rc = wait_for_completion_interruptible_timeout(&link->llc_testlink_resp,
						       SMC_LLC_WAIT_TIME);
	if (rc <= 0) {
		smc_lgr_terminate(lgr);
		return;
	}
	next_interval = link->llc_testlink_time;
out:
	schedule_delayed_work(&link->llc_testlink_wrk, next_interval);
}

void smc_llc_link_active(struct smc_link *link, int testlink_time)
{
	init_completion(&link->llc_testlink_resp);
	INIT_DELAYED_WORK(&link->llc_testlink_wrk, smc_llc_testlink_work);
	link->state = SMC_LNK_ACTIVE;
	if (testlink_time) {
		link->llc_testlink_time = testlink_time * HZ;
		schedule_delayed_work(&link->llc_testlink_wrk,
				      link->llc_testlink_time);
	}
}

/* called in tasklet context */
void smc_llc_link_inactive(struct smc_link *link)
{
	link->state = SMC_LNK_INACTIVE;
	cancel_delayed_work(&link->llc_testlink_wrk);
}

/* called in worker context */
void smc_llc_link_flush(struct smc_link *link)
{
	cancel_delayed_work_sync(&link->llc_testlink_wrk);
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
		.handler	= smc_llc_rx_handler,
		.type		= SMC_LLC_ADD_LINK
	},
	{
		.handler	= smc_llc_rx_handler,
		.type		= SMC_LLC_DELETE_LINK
	},
	{
		.handler	= smc_llc_rx_handler,
		.type		= SMC_LLC_CONFIRM_RKEY
	},
	{
		.handler	= smc_llc_rx_handler,
		.type		= SMC_LLC_CONFIRM_RKEY_CONT
	},
	{
		.handler	= smc_llc_rx_handler,
		.type		= SMC_LLC_DELETE_RKEY
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
