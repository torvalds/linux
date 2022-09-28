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
#include "smc_pnet.h"

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
#if defined(__BIG_ENDIAN_BITFIELD)
	u8 reserved3 : 4,
	   qp_mtu   : 4;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8 qp_mtu   : 4,
	   reserved3 : 4;
#endif
	u8 initial_psn[3];
	u8 reserved[8];
};

struct smc_llc_msg_add_link_cont_rt {
	__be32 rmb_key;
	__be32 rmb_key_new;
	__be64 rmb_vaddr_new;
};

#define SMC_LLC_RKEYS_PER_CONT_MSG	2

struct smc_llc_msg_add_link_cont {	/* type 0x03 */
	struct smc_llc_hdr hd;
	u8 link_num;
	u8 num_rkeys;
	u8 reserved2[2];
	struct smc_llc_msg_add_link_cont_rt rt[SMC_LLC_RKEYS_PER_CONT_MSG];
	u8 reserved[4];
} __packed;			/* format defined in RFC7609 */

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

#define SMC_LLC_DEL_RKEY_MAX	8
#define SMC_LLC_FLAG_RKEY_RETRY	0x10
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
	struct smc_llc_msg_add_link_cont add_link_cont;
	struct smc_llc_msg_del_link delete_link;

	struct smc_llc_msg_confirm_rkey confirm_rkey;
	struct smc_llc_msg_delete_rkey delete_rkey;

	struct smc_llc_msg_test_link test_link;
	struct {
		struct smc_llc_hdr hdr;
		u8 data[SMC_LLC_DATA_LEN];
	} raw;
};

#define SMC_LLC_FLAG_RESP		0x80

struct smc_llc_qentry {
	struct list_head list;
	struct smc_link *link;
	union smc_llc_msg msg;
};

static void smc_llc_enqueue(struct smc_link *link, union smc_llc_msg *llc);

struct smc_llc_qentry *smc_llc_flow_qentry_clr(struct smc_llc_flow *flow)
{
	struct smc_llc_qentry *qentry = flow->qentry;

	flow->qentry = NULL;
	return qentry;
}

void smc_llc_flow_qentry_del(struct smc_llc_flow *flow)
{
	struct smc_llc_qentry *qentry;

	if (flow->qentry) {
		qentry = flow->qentry;
		flow->qentry = NULL;
		kfree(qentry);
	}
}

static inline void smc_llc_flow_qentry_set(struct smc_llc_flow *flow,
					   struct smc_llc_qentry *qentry)
{
	flow->qentry = qentry;
}

static void smc_llc_flow_parallel(struct smc_link_group *lgr, u8 flow_type,
				  struct smc_llc_qentry *qentry)
{
	u8 msg_type = qentry->msg.raw.hdr.common.type;

	if ((msg_type == SMC_LLC_ADD_LINK || msg_type == SMC_LLC_DELETE_LINK) &&
	    flow_type != msg_type && !lgr->delayed_event) {
		lgr->delayed_event = qentry;
		return;
	}
	/* drop parallel or already-in-progress llc requests */
	if (flow_type != msg_type)
		pr_warn_once("smc: SMC-R lg %*phN dropped parallel "
			     "LLC msg: msg %d flow %d role %d\n",
			     SMC_LGR_ID_SIZE, &lgr->id,
			     qentry->msg.raw.hdr.common.type,
			     flow_type, lgr->role);
	kfree(qentry);
}

/* try to start a new llc flow, initiated by an incoming llc msg */
static bool smc_llc_flow_start(struct smc_llc_flow *flow,
			       struct smc_llc_qentry *qentry)
{
	struct smc_link_group *lgr = qentry->link->lgr;

	spin_lock_bh(&lgr->llc_flow_lock);
	if (flow->type) {
		/* a flow is already active */
		smc_llc_flow_parallel(lgr, flow->type, qentry);
		spin_unlock_bh(&lgr->llc_flow_lock);
		return false;
	}
	switch (qentry->msg.raw.hdr.common.type) {
	case SMC_LLC_ADD_LINK:
		flow->type = SMC_LLC_FLOW_ADD_LINK;
		break;
	case SMC_LLC_DELETE_LINK:
		flow->type = SMC_LLC_FLOW_DEL_LINK;
		break;
	case SMC_LLC_CONFIRM_RKEY:
	case SMC_LLC_DELETE_RKEY:
		flow->type = SMC_LLC_FLOW_RKEY;
		break;
	default:
		flow->type = SMC_LLC_FLOW_NONE;
	}
	smc_llc_flow_qentry_set(flow, qentry);
	spin_unlock_bh(&lgr->llc_flow_lock);
	return true;
}

/* start a new local llc flow, wait till current flow finished */
int smc_llc_flow_initiate(struct smc_link_group *lgr,
			  enum smc_llc_flowtype type)
{
	enum smc_llc_flowtype allowed_remote = SMC_LLC_FLOW_NONE;
	int rc;

	/* all flows except confirm_rkey and delete_rkey are exclusive,
	 * confirm/delete rkey flows can run concurrently (local and remote)
	 */
	if (type == SMC_LLC_FLOW_RKEY)
		allowed_remote = SMC_LLC_FLOW_RKEY;
again:
	if (list_empty(&lgr->list))
		return -ENODEV;
	spin_lock_bh(&lgr->llc_flow_lock);
	if (lgr->llc_flow_lcl.type == SMC_LLC_FLOW_NONE &&
	    (lgr->llc_flow_rmt.type == SMC_LLC_FLOW_NONE ||
	     lgr->llc_flow_rmt.type == allowed_remote)) {
		lgr->llc_flow_lcl.type = type;
		spin_unlock_bh(&lgr->llc_flow_lock);
		return 0;
	}
	spin_unlock_bh(&lgr->llc_flow_lock);
	rc = wait_event_timeout(lgr->llc_flow_waiter, (list_empty(&lgr->list) ||
				(lgr->llc_flow_lcl.type == SMC_LLC_FLOW_NONE &&
				 (lgr->llc_flow_rmt.type == SMC_LLC_FLOW_NONE ||
				  lgr->llc_flow_rmt.type == allowed_remote))),
				SMC_LLC_WAIT_TIME * 10);
	if (!rc)
		return -ETIMEDOUT;
	goto again;
}

/* finish the current llc flow */
void smc_llc_flow_stop(struct smc_link_group *lgr, struct smc_llc_flow *flow)
{
	spin_lock_bh(&lgr->llc_flow_lock);
	memset(flow, 0, sizeof(*flow));
	flow->type = SMC_LLC_FLOW_NONE;
	spin_unlock_bh(&lgr->llc_flow_lock);
	if (!list_empty(&lgr->list) && lgr->delayed_event &&
	    flow == &lgr->llc_flow_lcl)
		schedule_work(&lgr->llc_event_work);
	else
		wake_up(&lgr->llc_flow_waiter);
}

/* lnk is optional and used for early wakeup when link goes down, useful in
 * cases where we wait for a response on the link after we sent a request
 */
struct smc_llc_qentry *smc_llc_wait(struct smc_link_group *lgr,
				    struct smc_link *lnk,
				    int time_out, u8 exp_msg)
{
	struct smc_llc_flow *flow = &lgr->llc_flow_lcl;
	u8 rcv_msg;

	wait_event_timeout(lgr->llc_msg_waiter,
			   (flow->qentry ||
			    (lnk && !smc_link_usable(lnk)) ||
			    list_empty(&lgr->list)),
			   time_out);
	if (!flow->qentry ||
	    (lnk && !smc_link_usable(lnk)) || list_empty(&lgr->list)) {
		smc_llc_flow_qentry_del(flow);
		goto out;
	}
	rcv_msg = flow->qentry->msg.raw.hdr.common.type;
	if (exp_msg && rcv_msg != exp_msg) {
		if (exp_msg == SMC_LLC_ADD_LINK &&
		    rcv_msg == SMC_LLC_DELETE_LINK) {
			/* flow_start will delay the unexpected msg */
			smc_llc_flow_start(&lgr->llc_flow_lcl,
					   smc_llc_flow_qentry_clr(flow));
			return NULL;
		}
		pr_warn_once("smc: SMC-R lg %*phN dropped unexpected LLC msg: "
			     "msg %d exp %d flow %d role %d flags %x\n",
			     SMC_LGR_ID_SIZE, &lgr->id, rcv_msg, exp_msg,
			     flow->type, lgr->role,
			     flow->qentry->msg.raw.hdr.flags);
		smc_llc_flow_qentry_del(flow);
	}
out:
	return flow->qentry;
}

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

	rc = smc_wr_tx_get_free_slot(link, smc_llc_tx_handler, wr_buf, NULL,
				     pend);
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
int smc_llc_send_confirm_link(struct smc_link *link,
			      enum smc_llc_reqresp reqresp)
{
	struct smc_llc_msg_confirm_link *confllc;
	struct smc_wr_tx_pend_priv *pend;
	struct smc_wr_buf *wr_buf;
	int rc;

	if (!smc_wr_tx_link_hold(link))
		return -ENOLINK;
	rc = smc_llc_add_pending_send(link, &wr_buf, &pend);
	if (rc)
		goto put_out;
	confllc = (struct smc_llc_msg_confirm_link *)wr_buf;
	memset(confllc, 0, sizeof(*confllc));
	confllc->hd.common.type = SMC_LLC_CONFIRM_LINK;
	confllc->hd.length = sizeof(struct smc_llc_msg_confirm_link);
	confllc->hd.flags |= SMC_LLC_FLAG_NO_RMBE_EYEC;
	if (reqresp == SMC_LLC_RESP)
		confllc->hd.flags |= SMC_LLC_FLAG_RESP;
	memcpy(confllc->sender_mac, link->smcibdev->mac[link->ibport - 1],
	       ETH_ALEN);
	memcpy(confllc->sender_gid, link->gid, SMC_GID_SIZE);
	hton24(confllc->sender_qp_num, link->roce_qp->qp_num);
	confllc->link_num = link->link_id;
	memcpy(confllc->link_uid, link->link_uid, SMC_LGR_ID_SIZE);
	confllc->max_links = SMC_LLC_ADD_LNK_MAX_LINKS;
	/* send llc message */
	rc = smc_wr_tx_send(link, pend);
put_out:
	smc_wr_tx_link_put(link);
	return rc;
}

/* send LLC confirm rkey request */
static int smc_llc_send_confirm_rkey(struct smc_link *send_link,
				     struct smc_buf_desc *rmb_desc)
{
	struct smc_llc_msg_confirm_rkey *rkeyllc;
	struct smc_wr_tx_pend_priv *pend;
	struct smc_wr_buf *wr_buf;
	struct smc_link *link;
	int i, rc, rtok_ix;

	if (!smc_wr_tx_link_hold(send_link))
		return -ENOLINK;
	rc = smc_llc_add_pending_send(send_link, &wr_buf, &pend);
	if (rc)
		goto put_out;
	rkeyllc = (struct smc_llc_msg_confirm_rkey *)wr_buf;
	memset(rkeyllc, 0, sizeof(*rkeyllc));
	rkeyllc->hd.common.type = SMC_LLC_CONFIRM_RKEY;
	rkeyllc->hd.length = sizeof(struct smc_llc_msg_confirm_rkey);

	rtok_ix = 1;
	for (i = 0; i < SMC_LINKS_PER_LGR_MAX; i++) {
		link = &send_link->lgr->lnk[i];
		if (smc_link_active(link) && link != send_link) {
			rkeyllc->rtoken[rtok_ix].link_id = link->link_id;
			rkeyllc->rtoken[rtok_ix].rmb_key =
				htonl(rmb_desc->mr_rx[link->link_idx]->rkey);
			rkeyllc->rtoken[rtok_ix].rmb_vaddr = cpu_to_be64(
				(u64)sg_dma_address(
					rmb_desc->sgt[link->link_idx].sgl));
			rtok_ix++;
		}
	}
	/* rkey of send_link is in rtoken[0] */
	rkeyllc->rtoken[0].num_rkeys = rtok_ix - 1;
	rkeyllc->rtoken[0].rmb_key =
		htonl(rmb_desc->mr_rx[send_link->link_idx]->rkey);
	rkeyllc->rtoken[0].rmb_vaddr = cpu_to_be64(
		(u64)sg_dma_address(rmb_desc->sgt[send_link->link_idx].sgl));
	/* send llc message */
	rc = smc_wr_tx_send(send_link, pend);
put_out:
	smc_wr_tx_link_put(send_link);
	return rc;
}

/* send LLC delete rkey request */
static int smc_llc_send_delete_rkey(struct smc_link *link,
				    struct smc_buf_desc *rmb_desc)
{
	struct smc_llc_msg_delete_rkey *rkeyllc;
	struct smc_wr_tx_pend_priv *pend;
	struct smc_wr_buf *wr_buf;
	int rc;

	if (!smc_wr_tx_link_hold(link))
		return -ENOLINK;
	rc = smc_llc_add_pending_send(link, &wr_buf, &pend);
	if (rc)
		goto put_out;
	rkeyllc = (struct smc_llc_msg_delete_rkey *)wr_buf;
	memset(rkeyllc, 0, sizeof(*rkeyllc));
	rkeyllc->hd.common.type = SMC_LLC_DELETE_RKEY;
	rkeyllc->hd.length = sizeof(struct smc_llc_msg_delete_rkey);
	rkeyllc->num_rkeys = 1;
	rkeyllc->rkey[0] = htonl(rmb_desc->mr_rx[link->link_idx]->rkey);
	/* send llc message */
	rc = smc_wr_tx_send(link, pend);
put_out:
	smc_wr_tx_link_put(link);
	return rc;
}

/* send ADD LINK request or response */
int smc_llc_send_add_link(struct smc_link *link, u8 mac[], u8 gid[],
			  struct smc_link *link_new,
			  enum smc_llc_reqresp reqresp)
{
	struct smc_llc_msg_add_link *addllc;
	struct smc_wr_tx_pend_priv *pend;
	struct smc_wr_buf *wr_buf;
	int rc;

	if (!smc_wr_tx_link_hold(link))
		return -ENOLINK;
	rc = smc_llc_add_pending_send(link, &wr_buf, &pend);
	if (rc)
		goto put_out;
	addllc = (struct smc_llc_msg_add_link *)wr_buf;

	memset(addllc, 0, sizeof(*addllc));
	addllc->hd.common.type = SMC_LLC_ADD_LINK;
	addllc->hd.length = sizeof(struct smc_llc_msg_add_link);
	if (reqresp == SMC_LLC_RESP)
		addllc->hd.flags |= SMC_LLC_FLAG_RESP;
	memcpy(addllc->sender_mac, mac, ETH_ALEN);
	memcpy(addllc->sender_gid, gid, SMC_GID_SIZE);
	if (link_new) {
		addllc->link_num = link_new->link_id;
		hton24(addllc->sender_qp_num, link_new->roce_qp->qp_num);
		hton24(addllc->initial_psn, link_new->psn_initial);
		if (reqresp == SMC_LLC_REQ)
			addllc->qp_mtu = link_new->path_mtu;
		else
			addllc->qp_mtu = min(link_new->path_mtu,
					     link_new->peer_mtu);
	}
	/* send llc message */
	rc = smc_wr_tx_send(link, pend);
put_out:
	smc_wr_tx_link_put(link);
	return rc;
}

/* send DELETE LINK request or response */
int smc_llc_send_delete_link(struct smc_link *link, u8 link_del_id,
			     enum smc_llc_reqresp reqresp, bool orderly,
			     u32 reason)
{
	struct smc_llc_msg_del_link *delllc;
	struct smc_wr_tx_pend_priv *pend;
	struct smc_wr_buf *wr_buf;
	int rc;

	if (!smc_wr_tx_link_hold(link))
		return -ENOLINK;
	rc = smc_llc_add_pending_send(link, &wr_buf, &pend);
	if (rc)
		goto put_out;
	delllc = (struct smc_llc_msg_del_link *)wr_buf;

	memset(delllc, 0, sizeof(*delllc));
	delllc->hd.common.type = SMC_LLC_DELETE_LINK;
	delllc->hd.length = sizeof(struct smc_llc_msg_del_link);
	if (reqresp == SMC_LLC_RESP)
		delllc->hd.flags |= SMC_LLC_FLAG_RESP;
	if (orderly)
		delllc->hd.flags |= SMC_LLC_FLAG_DEL_LINK_ORDERLY;
	if (link_del_id)
		delllc->link_num = link_del_id;
	else
		delllc->hd.flags |= SMC_LLC_FLAG_DEL_LINK_ALL;
	delllc->reason = htonl(reason);
	/* send llc message */
	rc = smc_wr_tx_send(link, pend);
put_out:
	smc_wr_tx_link_put(link);
	return rc;
}

/* send LLC test link request */
static int smc_llc_send_test_link(struct smc_link *link, u8 user_data[16])
{
	struct smc_llc_msg_test_link *testllc;
	struct smc_wr_tx_pend_priv *pend;
	struct smc_wr_buf *wr_buf;
	int rc;

	if (!smc_wr_tx_link_hold(link))
		return -ENOLINK;
	rc = smc_llc_add_pending_send(link, &wr_buf, &pend);
	if (rc)
		goto put_out;
	testllc = (struct smc_llc_msg_test_link *)wr_buf;
	memset(testllc, 0, sizeof(*testllc));
	testllc->hd.common.type = SMC_LLC_TEST_LINK;
	testllc->hd.length = sizeof(struct smc_llc_msg_test_link);
	memcpy(testllc->user_data, user_data, sizeof(testllc->user_data));
	/* send llc message */
	rc = smc_wr_tx_send(link, pend);
put_out:
	smc_wr_tx_link_put(link);
	return rc;
}

/* schedule an llc send on link, may wait for buffers */
static int smc_llc_send_message(struct smc_link *link, void *llcbuf)
{
	struct smc_wr_tx_pend_priv *pend;
	struct smc_wr_buf *wr_buf;
	int rc;

	if (!smc_wr_tx_link_hold(link))
		return -ENOLINK;
	rc = smc_llc_add_pending_send(link, &wr_buf, &pend);
	if (rc)
		goto put_out;
	memcpy(wr_buf, llcbuf, sizeof(union smc_llc_msg));
	rc = smc_wr_tx_send(link, pend);
put_out:
	smc_wr_tx_link_put(link);
	return rc;
}

/* schedule an llc send on link, may wait for buffers,
 * and wait for send completion notification.
 * @return 0 on success
 */
static int smc_llc_send_message_wait(struct smc_link *link, void *llcbuf)
{
	struct smc_wr_tx_pend_priv *pend;
	struct smc_wr_buf *wr_buf;
	int rc;

	if (!smc_wr_tx_link_hold(link))
		return -ENOLINK;
	rc = smc_llc_add_pending_send(link, &wr_buf, &pend);
	if (rc)
		goto put_out;
	memcpy(wr_buf, llcbuf, sizeof(union smc_llc_msg));
	rc = smc_wr_tx_send_wait(link, pend, SMC_LLC_WAIT_TIME);
put_out:
	smc_wr_tx_link_put(link);
	return rc;
}

/********************************* receive ***********************************/

static int smc_llc_alloc_alt_link(struct smc_link_group *lgr,
				  enum smc_lgr_type lgr_new_t)
{
	int i;

	if (lgr->type == SMC_LGR_SYMMETRIC ||
	    (lgr->type != SMC_LGR_SINGLE &&
	     (lgr_new_t == SMC_LGR_ASYMMETRIC_LOCAL ||
	      lgr_new_t == SMC_LGR_ASYMMETRIC_PEER)))
		return -EMLINK;

	if (lgr_new_t == SMC_LGR_ASYMMETRIC_LOCAL ||
	    lgr_new_t == SMC_LGR_ASYMMETRIC_PEER) {
		for (i = SMC_LINKS_PER_LGR_MAX - 1; i >= 0; i--)
			if (lgr->lnk[i].state == SMC_LNK_UNUSED)
				return i;
	} else {
		for (i = 0; i < SMC_LINKS_PER_LGR_MAX; i++)
			if (lgr->lnk[i].state == SMC_LNK_UNUSED)
				return i;
	}
	return -EMLINK;
}

/* return first buffer from any of the next buf lists */
static struct smc_buf_desc *_smc_llc_get_next_rmb(struct smc_link_group *lgr,
						  int *buf_lst)
{
	struct smc_buf_desc *buf_pos;

	while (*buf_lst < SMC_RMBE_SIZES) {
		buf_pos = list_first_entry_or_null(&lgr->rmbs[*buf_lst],
						   struct smc_buf_desc, list);
		if (buf_pos)
			return buf_pos;
		(*buf_lst)++;
	}
	return NULL;
}

/* return next rmb from buffer lists */
static struct smc_buf_desc *smc_llc_get_next_rmb(struct smc_link_group *lgr,
						 int *buf_lst,
						 struct smc_buf_desc *buf_pos)
{
	struct smc_buf_desc *buf_next;

	if (!buf_pos || list_is_last(&buf_pos->list, &lgr->rmbs[*buf_lst])) {
		(*buf_lst)++;
		return _smc_llc_get_next_rmb(lgr, buf_lst);
	}
	buf_next = list_next_entry(buf_pos, list);
	return buf_next;
}

static struct smc_buf_desc *smc_llc_get_first_rmb(struct smc_link_group *lgr,
						  int *buf_lst)
{
	*buf_lst = 0;
	return smc_llc_get_next_rmb(lgr, buf_lst, NULL);
}

/* send one add_link_continue msg */
static int smc_llc_add_link_cont(struct smc_link *link,
				 struct smc_link *link_new, u8 *num_rkeys_todo,
				 int *buf_lst, struct smc_buf_desc **buf_pos)
{
	struct smc_llc_msg_add_link_cont *addc_llc;
	struct smc_link_group *lgr = link->lgr;
	int prim_lnk_idx, lnk_idx, i, rc;
	struct smc_wr_tx_pend_priv *pend;
	struct smc_wr_buf *wr_buf;
	struct smc_buf_desc *rmb;
	u8 n;

	if (!smc_wr_tx_link_hold(link))
		return -ENOLINK;
	rc = smc_llc_add_pending_send(link, &wr_buf, &pend);
	if (rc)
		goto put_out;
	addc_llc = (struct smc_llc_msg_add_link_cont *)wr_buf;
	memset(addc_llc, 0, sizeof(*addc_llc));

	prim_lnk_idx = link->link_idx;
	lnk_idx = link_new->link_idx;
	addc_llc->link_num = link_new->link_id;
	addc_llc->num_rkeys = *num_rkeys_todo;
	n = *num_rkeys_todo;
	for (i = 0; i < min_t(u8, n, SMC_LLC_RKEYS_PER_CONT_MSG); i++) {
		if (!*buf_pos) {
			addc_llc->num_rkeys = addc_llc->num_rkeys -
					      *num_rkeys_todo;
			*num_rkeys_todo = 0;
			break;
		}
		rmb = *buf_pos;

		addc_llc->rt[i].rmb_key = htonl(rmb->mr_rx[prim_lnk_idx]->rkey);
		addc_llc->rt[i].rmb_key_new = htonl(rmb->mr_rx[lnk_idx]->rkey);
		addc_llc->rt[i].rmb_vaddr_new =
			cpu_to_be64((u64)sg_dma_address(rmb->sgt[lnk_idx].sgl));

		(*num_rkeys_todo)--;
		*buf_pos = smc_llc_get_next_rmb(lgr, buf_lst, *buf_pos);
		while (*buf_pos && !(*buf_pos)->used)
			*buf_pos = smc_llc_get_next_rmb(lgr, buf_lst, *buf_pos);
	}
	addc_llc->hd.common.type = SMC_LLC_ADD_LINK_CONT;
	addc_llc->hd.length = sizeof(struct smc_llc_msg_add_link_cont);
	if (lgr->role == SMC_CLNT)
		addc_llc->hd.flags |= SMC_LLC_FLAG_RESP;
	rc = smc_wr_tx_send(link, pend);
put_out:
	smc_wr_tx_link_put(link);
	return rc;
}

static int smc_llc_cli_rkey_exchange(struct smc_link *link,
				     struct smc_link *link_new)
{
	struct smc_llc_msg_add_link_cont *addc_llc;
	struct smc_link_group *lgr = link->lgr;
	u8 max, num_rkeys_send, num_rkeys_recv;
	struct smc_llc_qentry *qentry;
	struct smc_buf_desc *buf_pos;
	int buf_lst;
	int rc = 0;
	int i;

	mutex_lock(&lgr->rmbs_lock);
	num_rkeys_send = lgr->conns_num;
	buf_pos = smc_llc_get_first_rmb(lgr, &buf_lst);
	do {
		qentry = smc_llc_wait(lgr, NULL, SMC_LLC_WAIT_TIME,
				      SMC_LLC_ADD_LINK_CONT);
		if (!qentry) {
			rc = -ETIMEDOUT;
			break;
		}
		addc_llc = &qentry->msg.add_link_cont;
		num_rkeys_recv = addc_llc->num_rkeys;
		max = min_t(u8, num_rkeys_recv, SMC_LLC_RKEYS_PER_CONT_MSG);
		for (i = 0; i < max; i++) {
			smc_rtoken_set(lgr, link->link_idx, link_new->link_idx,
				       addc_llc->rt[i].rmb_key,
				       addc_llc->rt[i].rmb_vaddr_new,
				       addc_llc->rt[i].rmb_key_new);
			num_rkeys_recv--;
		}
		smc_llc_flow_qentry_del(&lgr->llc_flow_lcl);
		rc = smc_llc_add_link_cont(link, link_new, &num_rkeys_send,
					   &buf_lst, &buf_pos);
		if (rc)
			break;
	} while (num_rkeys_send || num_rkeys_recv);

	mutex_unlock(&lgr->rmbs_lock);
	return rc;
}

/* prepare and send an add link reject response */
static int smc_llc_cli_add_link_reject(struct smc_llc_qentry *qentry)
{
	qentry->msg.raw.hdr.flags |= SMC_LLC_FLAG_RESP;
	qentry->msg.raw.hdr.flags |= SMC_LLC_FLAG_ADD_LNK_REJ;
	qentry->msg.raw.hdr.add_link_rej_rsn = SMC_LLC_REJ_RSN_NO_ALT_PATH;
	return smc_llc_send_message(qentry->link, &qentry->msg);
}

static int smc_llc_cli_conf_link(struct smc_link *link,
				 struct smc_init_info *ini,
				 struct smc_link *link_new,
				 enum smc_lgr_type lgr_new_t)
{
	struct smc_link_group *lgr = link->lgr;
	struct smc_llc_qentry *qentry = NULL;
	int rc = 0;

	/* receive CONFIRM LINK request over RoCE fabric */
	qentry = smc_llc_wait(lgr, NULL, SMC_LLC_WAIT_FIRST_TIME, 0);
	if (!qentry) {
		rc = smc_llc_send_delete_link(link, link_new->link_id,
					      SMC_LLC_REQ, false,
					      SMC_LLC_DEL_LOST_PATH);
		return -ENOLINK;
	}
	if (qentry->msg.raw.hdr.common.type != SMC_LLC_CONFIRM_LINK) {
		/* received DELETE_LINK instead */
		qentry->msg.raw.hdr.flags |= SMC_LLC_FLAG_RESP;
		smc_llc_send_message(link, &qentry->msg);
		smc_llc_flow_qentry_del(&lgr->llc_flow_lcl);
		return -ENOLINK;
	}
	smc_llc_save_peer_uid(qentry);
	smc_llc_flow_qentry_del(&lgr->llc_flow_lcl);

	rc = smc_ib_modify_qp_rts(link_new);
	if (rc) {
		smc_llc_send_delete_link(link, link_new->link_id, SMC_LLC_REQ,
					 false, SMC_LLC_DEL_LOST_PATH);
		return -ENOLINK;
	}
	smc_wr_remember_qp_attr(link_new);

	rc = smcr_buf_reg_lgr(link_new);
	if (rc) {
		smc_llc_send_delete_link(link, link_new->link_id, SMC_LLC_REQ,
					 false, SMC_LLC_DEL_LOST_PATH);
		return -ENOLINK;
	}

	/* send CONFIRM LINK response over RoCE fabric */
	rc = smc_llc_send_confirm_link(link_new, SMC_LLC_RESP);
	if (rc) {
		smc_llc_send_delete_link(link, link_new->link_id, SMC_LLC_REQ,
					 false, SMC_LLC_DEL_LOST_PATH);
		return -ENOLINK;
	}
	smc_llc_link_active(link_new);
	if (lgr_new_t == SMC_LGR_ASYMMETRIC_LOCAL ||
	    lgr_new_t == SMC_LGR_ASYMMETRIC_PEER)
		smcr_lgr_set_type_asym(lgr, lgr_new_t, link_new->link_idx);
	else
		smcr_lgr_set_type(lgr, lgr_new_t);
	return 0;
}

static void smc_llc_save_add_link_info(struct smc_link *link,
				       struct smc_llc_msg_add_link *add_llc)
{
	link->peer_qpn = ntoh24(add_llc->sender_qp_num);
	memcpy(link->peer_gid, add_llc->sender_gid, SMC_GID_SIZE);
	memcpy(link->peer_mac, add_llc->sender_mac, ETH_ALEN);
	link->peer_psn = ntoh24(add_llc->initial_psn);
	link->peer_mtu = add_llc->qp_mtu;
}

/* as an SMC client, process an add link request */
int smc_llc_cli_add_link(struct smc_link *link, struct smc_llc_qentry *qentry)
{
	struct smc_llc_msg_add_link *llc = &qentry->msg.add_link;
	enum smc_lgr_type lgr_new_t = SMC_LGR_SYMMETRIC;
	struct smc_link_group *lgr = smc_get_lgr(link);
	struct smc_link *lnk_new = NULL;
	struct smc_init_info ini;
	int lnk_idx, rc = 0;

	if (!llc->qp_mtu)
		goto out_reject;

	ini.vlan_id = lgr->vlan_id;
	smc_pnet_find_alt_roce(lgr, &ini, link->smcibdev);
	if (!memcmp(llc->sender_gid, link->peer_gid, SMC_GID_SIZE) &&
	    !memcmp(llc->sender_mac, link->peer_mac, ETH_ALEN)) {
		if (!ini.ib_dev)
			goto out_reject;
		lgr_new_t = SMC_LGR_ASYMMETRIC_PEER;
	}
	if (!ini.ib_dev) {
		lgr_new_t = SMC_LGR_ASYMMETRIC_LOCAL;
		ini.ib_dev = link->smcibdev;
		ini.ib_port = link->ibport;
	}
	lnk_idx = smc_llc_alloc_alt_link(lgr, lgr_new_t);
	if (lnk_idx < 0)
		goto out_reject;
	lnk_new = &lgr->lnk[lnk_idx];
	rc = smcr_link_init(lgr, lnk_new, lnk_idx, &ini);
	if (rc)
		goto out_reject;
	smc_llc_save_add_link_info(lnk_new, llc);
	lnk_new->link_id = llc->link_num;	/* SMC server assigns link id */
	smc_llc_link_set_uid(lnk_new);

	rc = smc_ib_ready_link(lnk_new);
	if (rc)
		goto out_clear_lnk;

	rc = smcr_buf_map_lgr(lnk_new);
	if (rc)
		goto out_clear_lnk;

	rc = smc_llc_send_add_link(link,
				   lnk_new->smcibdev->mac[ini.ib_port - 1],
				   lnk_new->gid, lnk_new, SMC_LLC_RESP);
	if (rc)
		goto out_clear_lnk;
	rc = smc_llc_cli_rkey_exchange(link, lnk_new);
	if (rc) {
		rc = 0;
		goto out_clear_lnk;
	}
	rc = smc_llc_cli_conf_link(link, &ini, lnk_new, lgr_new_t);
	if (!rc)
		goto out;
out_clear_lnk:
	lnk_new->state = SMC_LNK_INACTIVE;
	smcr_link_clear(lnk_new, false);
out_reject:
	smc_llc_cli_add_link_reject(qentry);
out:
	kfree(qentry);
	return rc;
}

/* as an SMC client, invite server to start the add_link processing */
static void smc_llc_cli_add_link_invite(struct smc_link *link,
					struct smc_llc_qentry *qentry)
{
	struct smc_link_group *lgr = smc_get_lgr(link);
	struct smc_init_info ini;

	if (lgr->type == SMC_LGR_SYMMETRIC ||
	    lgr->type == SMC_LGR_ASYMMETRIC_PEER)
		goto out;

	ini.vlan_id = lgr->vlan_id;
	smc_pnet_find_alt_roce(lgr, &ini, link->smcibdev);
	if (!ini.ib_dev)
		goto out;

	smc_llc_send_add_link(link, ini.ib_dev->mac[ini.ib_port - 1],
			      ini.ib_gid, NULL, SMC_LLC_REQ);
out:
	kfree(qentry);
}

static bool smc_llc_is_empty_llc_message(union smc_llc_msg *llc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(llc->raw.data); i++)
		if (llc->raw.data[i])
			return false;
	return true;
}

static bool smc_llc_is_local_add_link(union smc_llc_msg *llc)
{
	if (llc->raw.hdr.common.type == SMC_LLC_ADD_LINK &&
	    smc_llc_is_empty_llc_message(llc))
		return true;
	return false;
}

static void smc_llc_process_cli_add_link(struct smc_link_group *lgr)
{
	struct smc_llc_qentry *qentry;

	qentry = smc_llc_flow_qentry_clr(&lgr->llc_flow_lcl);

	mutex_lock(&lgr->llc_conf_mutex);
	if (smc_llc_is_local_add_link(&qentry->msg))
		smc_llc_cli_add_link_invite(qentry->link, qentry);
	else
		smc_llc_cli_add_link(qentry->link, qentry);
	mutex_unlock(&lgr->llc_conf_mutex);
}

static int smc_llc_active_link_count(struct smc_link_group *lgr)
{
	int i, link_count = 0;

	for (i = 0; i < SMC_LINKS_PER_LGR_MAX; i++) {
		if (!smc_link_active(&lgr->lnk[i]))
			continue;
		link_count++;
	}
	return link_count;
}

/* find the asymmetric link when 3 links are established  */
static struct smc_link *smc_llc_find_asym_link(struct smc_link_group *lgr)
{
	int asym_idx = -ENOENT;
	int i, j, k;
	bool found;

	/* determine asymmetric link */
	found = false;
	for (i = 0; i < SMC_LINKS_PER_LGR_MAX; i++) {
		for (j = i + 1; j < SMC_LINKS_PER_LGR_MAX; j++) {
			if (!smc_link_usable(&lgr->lnk[i]) ||
			    !smc_link_usable(&lgr->lnk[j]))
				continue;
			if (!memcmp(lgr->lnk[i].gid, lgr->lnk[j].gid,
				    SMC_GID_SIZE)) {
				found = true;	/* asym_lnk is i or j */
				break;
			}
		}
		if (found)
			break;
	}
	if (!found)
		goto out; /* no asymmetric link */
	for (k = 0; k < SMC_LINKS_PER_LGR_MAX; k++) {
		if (!smc_link_usable(&lgr->lnk[k]))
			continue;
		if (k != i &&
		    !memcmp(lgr->lnk[i].peer_gid, lgr->lnk[k].peer_gid,
			    SMC_GID_SIZE)) {
			asym_idx = i;
			break;
		}
		if (k != j &&
		    !memcmp(lgr->lnk[j].peer_gid, lgr->lnk[k].peer_gid,
			    SMC_GID_SIZE)) {
			asym_idx = j;
			break;
		}
	}
out:
	return (asym_idx < 0) ? NULL : &lgr->lnk[asym_idx];
}

static void smc_llc_delete_asym_link(struct smc_link_group *lgr)
{
	struct smc_link *lnk_new = NULL, *lnk_asym;
	struct smc_llc_qentry *qentry;
	int rc;

	lnk_asym = smc_llc_find_asym_link(lgr);
	if (!lnk_asym)
		return; /* no asymmetric link */
	if (!smc_link_downing(&lnk_asym->state))
		return;
	lnk_new = smc_switch_conns(lgr, lnk_asym, false);
	smc_wr_tx_wait_no_pending_sends(lnk_asym);
	if (!lnk_new)
		goto out_free;
	/* change flow type from ADD_LINK into DEL_LINK */
	lgr->llc_flow_lcl.type = SMC_LLC_FLOW_DEL_LINK;
	rc = smc_llc_send_delete_link(lnk_new, lnk_asym->link_id, SMC_LLC_REQ,
				      true, SMC_LLC_DEL_NO_ASYM_NEEDED);
	if (rc) {
		smcr_link_down_cond(lnk_new);
		goto out_free;
	}
	qentry = smc_llc_wait(lgr, lnk_new, SMC_LLC_WAIT_TIME,
			      SMC_LLC_DELETE_LINK);
	if (!qentry) {
		smcr_link_down_cond(lnk_new);
		goto out_free;
	}
	smc_llc_flow_qentry_del(&lgr->llc_flow_lcl);
out_free:
	smcr_link_clear(lnk_asym, true);
}

static int smc_llc_srv_rkey_exchange(struct smc_link *link,
				     struct smc_link *link_new)
{
	struct smc_llc_msg_add_link_cont *addc_llc;
	struct smc_link_group *lgr = link->lgr;
	u8 max, num_rkeys_send, num_rkeys_recv;
	struct smc_llc_qentry *qentry = NULL;
	struct smc_buf_desc *buf_pos;
	int buf_lst;
	int rc = 0;
	int i;

	mutex_lock(&lgr->rmbs_lock);
	num_rkeys_send = lgr->conns_num;
	buf_pos = smc_llc_get_first_rmb(lgr, &buf_lst);
	do {
		smc_llc_add_link_cont(link, link_new, &num_rkeys_send,
				      &buf_lst, &buf_pos);
		qentry = smc_llc_wait(lgr, link, SMC_LLC_WAIT_TIME,
				      SMC_LLC_ADD_LINK_CONT);
		if (!qentry) {
			rc = -ETIMEDOUT;
			goto out;
		}
		addc_llc = &qentry->msg.add_link_cont;
		num_rkeys_recv = addc_llc->num_rkeys;
		max = min_t(u8, num_rkeys_recv, SMC_LLC_RKEYS_PER_CONT_MSG);
		for (i = 0; i < max; i++) {
			smc_rtoken_set(lgr, link->link_idx, link_new->link_idx,
				       addc_llc->rt[i].rmb_key,
				       addc_llc->rt[i].rmb_vaddr_new,
				       addc_llc->rt[i].rmb_key_new);
			num_rkeys_recv--;
		}
		smc_llc_flow_qentry_del(&lgr->llc_flow_lcl);
	} while (num_rkeys_send || num_rkeys_recv);
out:
	mutex_unlock(&lgr->rmbs_lock);
	return rc;
}

static int smc_llc_srv_conf_link(struct smc_link *link,
				 struct smc_link *link_new,
				 enum smc_lgr_type lgr_new_t)
{
	struct smc_link_group *lgr = link->lgr;
	struct smc_llc_qentry *qentry = NULL;
	int rc;

	/* send CONFIRM LINK request over the RoCE fabric */
	rc = smc_llc_send_confirm_link(link_new, SMC_LLC_REQ);
	if (rc)
		return -ENOLINK;
	/* receive CONFIRM LINK response over the RoCE fabric */
	qentry = smc_llc_wait(lgr, link, SMC_LLC_WAIT_FIRST_TIME, 0);
	if (!qentry ||
	    qentry->msg.raw.hdr.common.type != SMC_LLC_CONFIRM_LINK) {
		/* send DELETE LINK */
		smc_llc_send_delete_link(link, link_new->link_id, SMC_LLC_REQ,
					 false, SMC_LLC_DEL_LOST_PATH);
		if (qentry)
			smc_llc_flow_qentry_del(&lgr->llc_flow_lcl);
		return -ENOLINK;
	}
	smc_llc_save_peer_uid(qentry);
	smc_llc_link_active(link_new);
	if (lgr_new_t == SMC_LGR_ASYMMETRIC_LOCAL ||
	    lgr_new_t == SMC_LGR_ASYMMETRIC_PEER)
		smcr_lgr_set_type_asym(lgr, lgr_new_t, link_new->link_idx);
	else
		smcr_lgr_set_type(lgr, lgr_new_t);
	smc_llc_flow_qentry_del(&lgr->llc_flow_lcl);
	return 0;
}

int smc_llc_srv_add_link(struct smc_link *link)
{
	enum smc_lgr_type lgr_new_t = SMC_LGR_SYMMETRIC;
	struct smc_link_group *lgr = link->lgr;
	struct smc_llc_msg_add_link *add_llc;
	struct smc_llc_qentry *qentry = NULL;
	struct smc_link *link_new;
	struct smc_init_info ini;
	int lnk_idx, rc = 0;

	/* ignore client add link recommendation, start new flow */
	ini.vlan_id = lgr->vlan_id;
	smc_pnet_find_alt_roce(lgr, &ini, link->smcibdev);
	if (!ini.ib_dev) {
		lgr_new_t = SMC_LGR_ASYMMETRIC_LOCAL;
		ini.ib_dev = link->smcibdev;
		ini.ib_port = link->ibport;
	}
	lnk_idx = smc_llc_alloc_alt_link(lgr, lgr_new_t);
	if (lnk_idx < 0)
		return 0;

	rc = smcr_link_init(lgr, &lgr->lnk[lnk_idx], lnk_idx, &ini);
	if (rc)
		return rc;
	link_new = &lgr->lnk[lnk_idx];
	rc = smc_llc_send_add_link(link,
				   link_new->smcibdev->mac[ini.ib_port - 1],
				   link_new->gid, link_new, SMC_LLC_REQ);
	if (rc)
		goto out_err;
	/* receive ADD LINK response over the RoCE fabric */
	qentry = smc_llc_wait(lgr, link, SMC_LLC_WAIT_TIME, SMC_LLC_ADD_LINK);
	if (!qentry) {
		rc = -ETIMEDOUT;
		goto out_err;
	}
	add_llc = &qentry->msg.add_link;
	if (add_llc->hd.flags & SMC_LLC_FLAG_ADD_LNK_REJ) {
		smc_llc_flow_qentry_del(&lgr->llc_flow_lcl);
		rc = -ENOLINK;
		goto out_err;
	}
	if (lgr->type == SMC_LGR_SINGLE &&
	    (!memcmp(add_llc->sender_gid, link->peer_gid, SMC_GID_SIZE) &&
	     !memcmp(add_llc->sender_mac, link->peer_mac, ETH_ALEN))) {
		lgr_new_t = SMC_LGR_ASYMMETRIC_PEER;
	}
	smc_llc_save_add_link_info(link_new, add_llc);
	smc_llc_flow_qentry_del(&lgr->llc_flow_lcl);

	rc = smc_ib_ready_link(link_new);
	if (rc)
		goto out_err;
	rc = smcr_buf_map_lgr(link_new);
	if (rc)
		goto out_err;
	rc = smcr_buf_reg_lgr(link_new);
	if (rc)
		goto out_err;
	rc = smc_llc_srv_rkey_exchange(link, link_new);
	if (rc)
		goto out_err;
	rc = smc_llc_srv_conf_link(link, link_new, lgr_new_t);
	if (rc)
		goto out_err;
	return 0;
out_err:
	link_new->state = SMC_LNK_INACTIVE;
	smcr_link_clear(link_new, false);
	return rc;
}

static void smc_llc_process_srv_add_link(struct smc_link_group *lgr)
{
	struct smc_link *link = lgr->llc_flow_lcl.qentry->link;
	int rc;

	smc_llc_flow_qentry_del(&lgr->llc_flow_lcl);

	mutex_lock(&lgr->llc_conf_mutex);
	rc = smc_llc_srv_add_link(link);
	if (!rc && lgr->type == SMC_LGR_SYMMETRIC) {
		/* delete any asymmetric link */
		smc_llc_delete_asym_link(lgr);
	}
	mutex_unlock(&lgr->llc_conf_mutex);
}

/* enqueue a local add_link req to trigger a new add_link flow */
void smc_llc_add_link_local(struct smc_link *link)
{
	struct smc_llc_msg_add_link add_llc = {};

	add_llc.hd.length = sizeof(add_llc);
	add_llc.hd.common.type = SMC_LLC_ADD_LINK;
	/* no dev and port needed */
	smc_llc_enqueue(link, (union smc_llc_msg *)&add_llc);
}

/* worker to process an add link message */
static void smc_llc_add_link_work(struct work_struct *work)
{
	struct smc_link_group *lgr = container_of(work, struct smc_link_group,
						  llc_add_link_work);

	if (list_empty(&lgr->list)) {
		/* link group is terminating */
		smc_llc_flow_qentry_del(&lgr->llc_flow_lcl);
		goto out;
	}

	if (lgr->role == SMC_CLNT)
		smc_llc_process_cli_add_link(lgr);
	else
		smc_llc_process_srv_add_link(lgr);
out:
	smc_llc_flow_stop(lgr, &lgr->llc_flow_lcl);
}

/* enqueue a local del_link msg to trigger a new del_link flow,
 * called only for role SMC_SERV
 */
void smc_llc_srv_delete_link_local(struct smc_link *link, u8 del_link_id)
{
	struct smc_llc_msg_del_link del_llc = {};

	del_llc.hd.length = sizeof(del_llc);
	del_llc.hd.common.type = SMC_LLC_DELETE_LINK;
	del_llc.link_num = del_link_id;
	del_llc.reason = htonl(SMC_LLC_DEL_LOST_PATH);
	del_llc.hd.flags |= SMC_LLC_FLAG_DEL_LINK_ORDERLY;
	smc_llc_enqueue(link, (union smc_llc_msg *)&del_llc);
}

static void smc_llc_process_cli_delete_link(struct smc_link_group *lgr)
{
	struct smc_link *lnk_del = NULL, *lnk_asym, *lnk;
	struct smc_llc_msg_del_link *del_llc;
	struct smc_llc_qentry *qentry;
	int active_links;
	int lnk_idx;

	qentry = smc_llc_flow_qentry_clr(&lgr->llc_flow_lcl);
	lnk = qentry->link;
	del_llc = &qentry->msg.delete_link;

	if (del_llc->hd.flags & SMC_LLC_FLAG_DEL_LINK_ALL) {
		smc_lgr_terminate_sched(lgr);
		goto out;
	}
	mutex_lock(&lgr->llc_conf_mutex);
	/* delete single link */
	for (lnk_idx = 0; lnk_idx < SMC_LINKS_PER_LGR_MAX; lnk_idx++) {
		if (lgr->lnk[lnk_idx].link_id != del_llc->link_num)
			continue;
		lnk_del = &lgr->lnk[lnk_idx];
		break;
	}
	del_llc->hd.flags |= SMC_LLC_FLAG_RESP;
	if (!lnk_del) {
		/* link was not found */
		del_llc->reason = htonl(SMC_LLC_DEL_NOLNK);
		smc_llc_send_message(lnk, &qentry->msg);
		goto out_unlock;
	}
	lnk_asym = smc_llc_find_asym_link(lgr);

	del_llc->reason = 0;
	smc_llc_send_message(lnk, &qentry->msg); /* response */

	if (smc_link_downing(&lnk_del->state))
		smc_switch_conns(lgr, lnk_del, false);
	smcr_link_clear(lnk_del, true);

	active_links = smc_llc_active_link_count(lgr);
	if (lnk_del == lnk_asym) {
		/* expected deletion of asym link, don't change lgr state */
	} else if (active_links == 1) {
		smcr_lgr_set_type(lgr, SMC_LGR_SINGLE);
	} else if (!active_links) {
		smcr_lgr_set_type(lgr, SMC_LGR_NONE);
		smc_lgr_terminate_sched(lgr);
	}
out_unlock:
	mutex_unlock(&lgr->llc_conf_mutex);
out:
	kfree(qentry);
}

/* try to send a DELETE LINK ALL request on any active link,
 * waiting for send completion
 */
void smc_llc_send_link_delete_all(struct smc_link_group *lgr, bool ord, u32 rsn)
{
	struct smc_llc_msg_del_link delllc = {};
	int i;

	delllc.hd.common.type = SMC_LLC_DELETE_LINK;
	delllc.hd.length = sizeof(delllc);
	if (ord)
		delllc.hd.flags |= SMC_LLC_FLAG_DEL_LINK_ORDERLY;
	delllc.hd.flags |= SMC_LLC_FLAG_DEL_LINK_ALL;
	delllc.reason = htonl(rsn);

	for (i = 0; i < SMC_LINKS_PER_LGR_MAX; i++) {
		if (!smc_link_sendable(&lgr->lnk[i]))
			continue;
		if (!smc_llc_send_message_wait(&lgr->lnk[i], &delllc))
			break;
	}
}

static void smc_llc_process_srv_delete_link(struct smc_link_group *lgr)
{
	struct smc_llc_msg_del_link *del_llc;
	struct smc_link *lnk, *lnk_del;
	struct smc_llc_qentry *qentry;
	int active_links;
	int i;

	mutex_lock(&lgr->llc_conf_mutex);
	qentry = smc_llc_flow_qentry_clr(&lgr->llc_flow_lcl);
	lnk = qentry->link;
	del_llc = &qentry->msg.delete_link;

	if (qentry->msg.delete_link.hd.flags & SMC_LLC_FLAG_DEL_LINK_ALL) {
		/* delete entire lgr */
		smc_llc_send_link_delete_all(lgr, true, ntohl(
					      qentry->msg.delete_link.reason));
		smc_lgr_terminate_sched(lgr);
		goto out;
	}
	/* delete single link */
	lnk_del = NULL;
	for (i = 0; i < SMC_LINKS_PER_LGR_MAX; i++) {
		if (lgr->lnk[i].link_id == del_llc->link_num) {
			lnk_del = &lgr->lnk[i];
			break;
		}
	}
	if (!lnk_del)
		goto out; /* asymmetric link already deleted */

	if (smc_link_downing(&lnk_del->state)) {
		if (smc_switch_conns(lgr, lnk_del, false))
			smc_wr_tx_wait_no_pending_sends(lnk_del);
	}
	if (!list_empty(&lgr->list)) {
		/* qentry is either a request from peer (send it back to
		 * initiate the DELETE_LINK processing), or a locally
		 * enqueued DELETE_LINK request (forward it)
		 */
		if (!smc_llc_send_message(lnk, &qentry->msg)) {
			struct smc_llc_qentry *qentry2;

			qentry2 = smc_llc_wait(lgr, lnk, SMC_LLC_WAIT_TIME,
					       SMC_LLC_DELETE_LINK);
			if (qentry2)
				smc_llc_flow_qentry_del(&lgr->llc_flow_lcl);
		}
	}
	smcr_link_clear(lnk_del, true);

	active_links = smc_llc_active_link_count(lgr);
	if (active_links == 1) {
		smcr_lgr_set_type(lgr, SMC_LGR_SINGLE);
	} else if (!active_links) {
		smcr_lgr_set_type(lgr, SMC_LGR_NONE);
		smc_lgr_terminate_sched(lgr);
	}

	if (lgr->type == SMC_LGR_SINGLE && !list_empty(&lgr->list)) {
		/* trigger setup of asymm alt link */
		smc_llc_add_link_local(lnk);
	}
out:
	mutex_unlock(&lgr->llc_conf_mutex);
	kfree(qentry);
}

static void smc_llc_delete_link_work(struct work_struct *work)
{
	struct smc_link_group *lgr = container_of(work, struct smc_link_group,
						  llc_del_link_work);

	if (list_empty(&lgr->list)) {
		/* link group is terminating */
		smc_llc_flow_qentry_del(&lgr->llc_flow_lcl);
		goto out;
	}

	if (lgr->role == SMC_CLNT)
		smc_llc_process_cli_delete_link(lgr);
	else
		smc_llc_process_srv_delete_link(lgr);
out:
	smc_llc_flow_stop(lgr, &lgr->llc_flow_lcl);
}

/* process a confirm_rkey request from peer, remote flow */
static void smc_llc_rmt_conf_rkey(struct smc_link_group *lgr)
{
	struct smc_llc_msg_confirm_rkey *llc;
	struct smc_llc_qentry *qentry;
	struct smc_link *link;
	int num_entries;
	int rk_idx;
	int i;

	qentry = lgr->llc_flow_rmt.qentry;
	llc = &qentry->msg.confirm_rkey;
	link = qentry->link;

	num_entries = llc->rtoken[0].num_rkeys;
	/* first rkey entry is for receiving link */
	rk_idx = smc_rtoken_add(link,
				llc->rtoken[0].rmb_vaddr,
				llc->rtoken[0].rmb_key);
	if (rk_idx < 0)
		goto out_err;

	for (i = 1; i <= min_t(u8, num_entries, SMC_LLC_RKEYS_PER_MSG - 1); i++)
		smc_rtoken_set2(lgr, rk_idx, llc->rtoken[i].link_id,
				llc->rtoken[i].rmb_vaddr,
				llc->rtoken[i].rmb_key);
	/* max links is 3 so there is no need to support conf_rkey_cont msgs */
	goto out;
out_err:
	llc->hd.flags |= SMC_LLC_FLAG_RKEY_NEG;
	llc->hd.flags |= SMC_LLC_FLAG_RKEY_RETRY;
out:
	llc->hd.flags |= SMC_LLC_FLAG_RESP;
	smc_llc_send_message(link, &qentry->msg);
	smc_llc_flow_qentry_del(&lgr->llc_flow_rmt);
}

/* process a delete_rkey request from peer, remote flow */
static void smc_llc_rmt_delete_rkey(struct smc_link_group *lgr)
{
	struct smc_llc_msg_delete_rkey *llc;
	struct smc_llc_qentry *qentry;
	struct smc_link *link;
	u8 err_mask = 0;
	int i, max;

	qentry = lgr->llc_flow_rmt.qentry;
	llc = &qentry->msg.delete_rkey;
	link = qentry->link;

	max = min_t(u8, llc->num_rkeys, SMC_LLC_DEL_RKEY_MAX);
	for (i = 0; i < max; i++) {
		if (smc_rtoken_delete(link, llc->rkey[i]))
			err_mask |= 1 << (SMC_LLC_DEL_RKEY_MAX - 1 - i);
	}
	if (err_mask) {
		llc->hd.flags |= SMC_LLC_FLAG_RKEY_NEG;
		llc->err_mask = err_mask;
	}
	llc->hd.flags |= SMC_LLC_FLAG_RESP;
	smc_llc_send_message(link, &qentry->msg);
	smc_llc_flow_qentry_del(&lgr->llc_flow_rmt);
}

static void smc_llc_protocol_violation(struct smc_link_group *lgr, u8 type)
{
	pr_warn_ratelimited("smc: SMC-R lg %*phN LLC protocol violation: "
			    "llc_type %d\n", SMC_LGR_ID_SIZE, &lgr->id, type);
	smc_llc_set_termination_rsn(lgr, SMC_LLC_DEL_PROT_VIOL);
	smc_lgr_terminate_sched(lgr);
}

/* flush the llc event queue */
static void smc_llc_event_flush(struct smc_link_group *lgr)
{
	struct smc_llc_qentry *qentry, *q;

	spin_lock_bh(&lgr->llc_event_q_lock);
	list_for_each_entry_safe(qentry, q, &lgr->llc_event_q, list) {
		list_del_init(&qentry->list);
		kfree(qentry);
	}
	spin_unlock_bh(&lgr->llc_event_q_lock);
}

static void smc_llc_event_handler(struct smc_llc_qentry *qentry)
{
	union smc_llc_msg *llc = &qentry->msg;
	struct smc_link *link = qentry->link;
	struct smc_link_group *lgr = link->lgr;

	if (!smc_link_usable(link))
		goto out;

	switch (llc->raw.hdr.common.type) {
	case SMC_LLC_TEST_LINK:
		llc->test_link.hd.flags |= SMC_LLC_FLAG_RESP;
		smc_llc_send_message(link, llc);
		break;
	case SMC_LLC_ADD_LINK:
		if (list_empty(&lgr->list))
			goto out;	/* lgr is terminating */
		if (lgr->role == SMC_CLNT) {
			if (smc_llc_is_local_add_link(llc)) {
				if (lgr->llc_flow_lcl.type ==
				    SMC_LLC_FLOW_ADD_LINK)
					break;	/* add_link in progress */
				if (smc_llc_flow_start(&lgr->llc_flow_lcl,
						       qentry)) {
					schedule_work(&lgr->llc_add_link_work);
				}
				return;
			}
			if (lgr->llc_flow_lcl.type == SMC_LLC_FLOW_ADD_LINK &&
			    !lgr->llc_flow_lcl.qentry) {
				/* a flow is waiting for this message */
				smc_llc_flow_qentry_set(&lgr->llc_flow_lcl,
							qentry);
				wake_up(&lgr->llc_msg_waiter);
			} else if (smc_llc_flow_start(&lgr->llc_flow_lcl,
						      qentry)) {
				schedule_work(&lgr->llc_add_link_work);
			}
		} else if (smc_llc_flow_start(&lgr->llc_flow_lcl, qentry)) {
			/* as smc server, handle client suggestion */
			schedule_work(&lgr->llc_add_link_work);
		}
		return;
	case SMC_LLC_CONFIRM_LINK:
	case SMC_LLC_ADD_LINK_CONT:
		if (lgr->llc_flow_lcl.type != SMC_LLC_FLOW_NONE) {
			/* a flow is waiting for this message */
			smc_llc_flow_qentry_set(&lgr->llc_flow_lcl, qentry);
			wake_up(&lgr->llc_msg_waiter);
			return;
		}
		break;
	case SMC_LLC_DELETE_LINK:
		if (lgr->llc_flow_lcl.type == SMC_LLC_FLOW_ADD_LINK &&
		    !lgr->llc_flow_lcl.qentry) {
			/* DEL LINK REQ during ADD LINK SEQ */
			smc_llc_flow_qentry_set(&lgr->llc_flow_lcl, qentry);
			wake_up(&lgr->llc_msg_waiter);
		} else if (smc_llc_flow_start(&lgr->llc_flow_lcl, qentry)) {
			schedule_work(&lgr->llc_del_link_work);
		}
		return;
	case SMC_LLC_CONFIRM_RKEY:
		/* new request from remote, assign to remote flow */
		if (smc_llc_flow_start(&lgr->llc_flow_rmt, qentry)) {
			/* process here, does not wait for more llc msgs */
			smc_llc_rmt_conf_rkey(lgr);
			smc_llc_flow_stop(lgr, &lgr->llc_flow_rmt);
		}
		return;
	case SMC_LLC_CONFIRM_RKEY_CONT:
		/* not used because max links is 3, and 3 rkeys fit into
		 * one CONFIRM_RKEY message
		 */
		break;
	case SMC_LLC_DELETE_RKEY:
		/* new request from remote, assign to remote flow */
		if (smc_llc_flow_start(&lgr->llc_flow_rmt, qentry)) {
			/* process here, does not wait for more llc msgs */
			smc_llc_rmt_delete_rkey(lgr);
			smc_llc_flow_stop(lgr, &lgr->llc_flow_rmt);
		}
		return;
	default:
		smc_llc_protocol_violation(lgr, llc->raw.hdr.common.type);
		break;
	}
out:
	kfree(qentry);
}

/* worker to process llc messages on the event queue */
static void smc_llc_event_work(struct work_struct *work)
{
	struct smc_link_group *lgr = container_of(work, struct smc_link_group,
						  llc_event_work);
	struct smc_llc_qentry *qentry;

	if (!lgr->llc_flow_lcl.type && lgr->delayed_event) {
		qentry = lgr->delayed_event;
		lgr->delayed_event = NULL;
		if (smc_link_usable(qentry->link))
			smc_llc_event_handler(qentry);
		else
			kfree(qentry);
	}

again:
	spin_lock_bh(&lgr->llc_event_q_lock);
	if (!list_empty(&lgr->llc_event_q)) {
		qentry = list_first_entry(&lgr->llc_event_q,
					  struct smc_llc_qentry, list);
		list_del_init(&qentry->list);
		spin_unlock_bh(&lgr->llc_event_q_lock);
		smc_llc_event_handler(qentry);
		goto again;
	}
	spin_unlock_bh(&lgr->llc_event_q_lock);
}

/* process llc responses in tasklet context */
static void smc_llc_rx_response(struct smc_link *link,
				struct smc_llc_qentry *qentry)
{
	enum smc_llc_flowtype flowtype = link->lgr->llc_flow_lcl.type;
	struct smc_llc_flow *flow = &link->lgr->llc_flow_lcl;
	u8 llc_type = qentry->msg.raw.hdr.common.type;

	switch (llc_type) {
	case SMC_LLC_TEST_LINK:
		if (smc_link_active(link))
			complete(&link->llc_testlink_resp);
		break;
	case SMC_LLC_ADD_LINK:
	case SMC_LLC_ADD_LINK_CONT:
	case SMC_LLC_CONFIRM_LINK:
		if (flowtype != SMC_LLC_FLOW_ADD_LINK || flow->qentry)
			break;	/* drop out-of-flow response */
		goto assign;
	case SMC_LLC_DELETE_LINK:
		if (flowtype != SMC_LLC_FLOW_DEL_LINK || flow->qentry)
			break;	/* drop out-of-flow response */
		goto assign;
	case SMC_LLC_CONFIRM_RKEY:
	case SMC_LLC_DELETE_RKEY:
		if (flowtype != SMC_LLC_FLOW_RKEY || flow->qentry)
			break;	/* drop out-of-flow response */
		goto assign;
	case SMC_LLC_CONFIRM_RKEY_CONT:
		/* not used because max links is 3 */
		break;
	default:
		smc_llc_protocol_violation(link->lgr, llc_type);
		break;
	}
	kfree(qentry);
	return;
assign:
	/* assign responses to the local flow, we requested them */
	smc_llc_flow_qentry_set(&link->lgr->llc_flow_lcl, qentry);
	wake_up(&link->lgr->llc_msg_waiter);
}

static void smc_llc_enqueue(struct smc_link *link, union smc_llc_msg *llc)
{
	struct smc_link_group *lgr = link->lgr;
	struct smc_llc_qentry *qentry;
	unsigned long flags;

	qentry = kmalloc(sizeof(*qentry), GFP_ATOMIC);
	if (!qentry)
		return;
	qentry->link = link;
	INIT_LIST_HEAD(&qentry->list);
	memcpy(&qentry->msg, llc, sizeof(union smc_llc_msg));

	/* process responses immediately */
	if (llc->raw.hdr.flags & SMC_LLC_FLAG_RESP) {
		smc_llc_rx_response(link, qentry);
		return;
	}

	/* add requests to event queue */
	spin_lock_irqsave(&lgr->llc_event_q_lock, flags);
	list_add_tail(&qentry->list, &lgr->llc_event_q);
	spin_unlock_irqrestore(&lgr->llc_event_q_lock, flags);
	queue_work(system_highpri_wq, &lgr->llc_event_work);
}

/* copy received msg and add it to the event queue */
static void smc_llc_rx_handler(struct ib_wc *wc, void *buf)
{
	struct smc_link *link = (struct smc_link *)wc->qp->qp_context;
	union smc_llc_msg *llc = buf;

	if (wc->byte_len < sizeof(*llc))
		return; /* short message */
	if (llc->raw.hdr.length != sizeof(*llc))
		return; /* invalid message */

	smc_llc_enqueue(link, llc);
}

/***************************** worker, utils *********************************/

static void smc_llc_testlink_work(struct work_struct *work)
{
	struct smc_link *link = container_of(to_delayed_work(work),
					     struct smc_link, llc_testlink_wrk);
	unsigned long next_interval;
	unsigned long expire_time;
	u8 user_data[16] = { 0 };
	int rc;

	if (!smc_link_active(link))
		return;		/* don't reschedule worker */
	expire_time = link->wr_rx_tstamp + link->llc_testlink_time;
	if (time_is_after_jiffies(expire_time)) {
		next_interval = expire_time - jiffies;
		goto out;
	}
	reinit_completion(&link->llc_testlink_resp);
	smc_llc_send_test_link(link, user_data);
	/* receive TEST LINK response over RoCE fabric */
	rc = wait_for_completion_interruptible_timeout(&link->llc_testlink_resp,
						       SMC_LLC_WAIT_TIME);
	if (!smc_link_active(link))
		return;		/* link state changed */
	if (rc <= 0) {
		smcr_link_down_cond_sched(link);
		return;
	}
	next_interval = link->llc_testlink_time;
out:
	schedule_delayed_work(&link->llc_testlink_wrk, next_interval);
}

void smc_llc_lgr_init(struct smc_link_group *lgr, struct smc_sock *smc)
{
	struct net *net = sock_net(smc->clcsock->sk);

	INIT_WORK(&lgr->llc_event_work, smc_llc_event_work);
	INIT_WORK(&lgr->llc_add_link_work, smc_llc_add_link_work);
	INIT_WORK(&lgr->llc_del_link_work, smc_llc_delete_link_work);
	INIT_LIST_HEAD(&lgr->llc_event_q);
	spin_lock_init(&lgr->llc_event_q_lock);
	spin_lock_init(&lgr->llc_flow_lock);
	init_waitqueue_head(&lgr->llc_flow_waiter);
	init_waitqueue_head(&lgr->llc_msg_waiter);
	mutex_init(&lgr->llc_conf_mutex);
	lgr->llc_testlink_time = READ_ONCE(net->ipv4.sysctl_tcp_keepalive_time);
}

/* called after lgr was removed from lgr_list */
void smc_llc_lgr_clear(struct smc_link_group *lgr)
{
	smc_llc_event_flush(lgr);
	wake_up_all(&lgr->llc_flow_waiter);
	wake_up_all(&lgr->llc_msg_waiter);
	cancel_work_sync(&lgr->llc_event_work);
	cancel_work_sync(&lgr->llc_add_link_work);
	cancel_work_sync(&lgr->llc_del_link_work);
	if (lgr->delayed_event) {
		kfree(lgr->delayed_event);
		lgr->delayed_event = NULL;
	}
}

int smc_llc_link_init(struct smc_link *link)
{
	init_completion(&link->llc_testlink_resp);
	INIT_DELAYED_WORK(&link->llc_testlink_wrk, smc_llc_testlink_work);
	return 0;
}

void smc_llc_link_active(struct smc_link *link)
{
	pr_warn_ratelimited("smc: SMC-R lg %*phN link added: id %*phN, "
			    "peerid %*phN, ibdev %s, ibport %d\n",
			    SMC_LGR_ID_SIZE, &link->lgr->id,
			    SMC_LGR_ID_SIZE, &link->link_uid,
			    SMC_LGR_ID_SIZE, &link->peer_link_uid,
			    link->smcibdev->ibdev->name, link->ibport);
	link->state = SMC_LNK_ACTIVE;
	if (link->lgr->llc_testlink_time) {
		link->llc_testlink_time = link->lgr->llc_testlink_time;
		schedule_delayed_work(&link->llc_testlink_wrk,
				      link->llc_testlink_time);
	}
}

/* called in worker context */
void smc_llc_link_clear(struct smc_link *link, bool log)
{
	if (log)
		pr_warn_ratelimited("smc: SMC-R lg %*phN link removed: id %*phN"
				    ", peerid %*phN, ibdev %s, ibport %d\n",
				    SMC_LGR_ID_SIZE, &link->lgr->id,
				    SMC_LGR_ID_SIZE, &link->link_uid,
				    SMC_LGR_ID_SIZE, &link->peer_link_uid,
				    link->smcibdev->ibdev->name, link->ibport);
	complete(&link->llc_testlink_resp);
	cancel_delayed_work_sync(&link->llc_testlink_wrk);
}

/* register a new rtoken at the remote peer (for all links) */
int smc_llc_do_confirm_rkey(struct smc_link *send_link,
			    struct smc_buf_desc *rmb_desc)
{
	struct smc_link_group *lgr = send_link->lgr;
	struct smc_llc_qentry *qentry = NULL;
	int rc = 0;

	rc = smc_llc_send_confirm_rkey(send_link, rmb_desc);
	if (rc)
		goto out;
	/* receive CONFIRM RKEY response from server over RoCE fabric */
	qentry = smc_llc_wait(lgr, send_link, SMC_LLC_WAIT_TIME,
			      SMC_LLC_CONFIRM_RKEY);
	if (!qentry || (qentry->msg.raw.hdr.flags & SMC_LLC_FLAG_RKEY_NEG))
		rc = -EFAULT;
out:
	if (qentry)
		smc_llc_flow_qentry_del(&lgr->llc_flow_lcl);
	return rc;
}

/* unregister an rtoken at the remote peer */
int smc_llc_do_delete_rkey(struct smc_link_group *lgr,
			   struct smc_buf_desc *rmb_desc)
{
	struct smc_llc_qentry *qentry = NULL;
	struct smc_link *send_link;
	int rc = 0;

	send_link = smc_llc_usable_link(lgr);
	if (!send_link)
		return -ENOLINK;

	/* protected by llc_flow control */
	rc = smc_llc_send_delete_rkey(send_link, rmb_desc);
	if (rc)
		goto out;
	/* receive DELETE RKEY response from server over RoCE fabric */
	qentry = smc_llc_wait(lgr, send_link, SMC_LLC_WAIT_TIME,
			      SMC_LLC_DELETE_RKEY);
	if (!qentry || (qentry->msg.raw.hdr.flags & SMC_LLC_FLAG_RKEY_NEG))
		rc = -EFAULT;
out:
	if (qentry)
		smc_llc_flow_qentry_del(&lgr->llc_flow_lcl);
	return rc;
}

void smc_llc_link_set_uid(struct smc_link *link)
{
	__be32 link_uid;

	link_uid = htonl(*((u32 *)link->lgr->id) + link->link_id);
	memcpy(link->link_uid, &link_uid, SMC_LGR_ID_SIZE);
}

/* save peers link user id, used for debug purposes */
void smc_llc_save_peer_uid(struct smc_llc_qentry *qentry)
{
	memcpy(qentry->link->peer_link_uid, qentry->msg.confirm_link.link_uid,
	       SMC_LGR_ID_SIZE);
}

/* evaluate confirm link request or response */
int smc_llc_eval_conf_link(struct smc_llc_qentry *qentry,
			   enum smc_llc_reqresp type)
{
	if (type == SMC_LLC_REQ) {	/* SMC server assigns link_id */
		qentry->link->link_id = qentry->msg.confirm_link.link_num;
		smc_llc_link_set_uid(qentry->link);
	}
	if (!(qentry->msg.raw.hdr.flags & SMC_LLC_FLAG_NO_RMBE_EYEC))
		return -ENOTSUPP;
	return 0;
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
		.type		= SMC_LLC_ADD_LINK_CONT
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
