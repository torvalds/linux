/*
 *  Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 *  IB infrastructure:
 *  Establish SMC-R as an Infiniband Client to be notified about added and
 *  removed IB devices of type RDMA.
 *  Determine device and port characteristics for these IB devices.
 *
 *  Copyright IBM Corp. 2016
 *
 *  Author(s):  Ursula Braun <ubraun@linux.vnet.ibm.com>
 */

#include <linux/random.h>
#include <linux/workqueue.h>
#include <rdma/ib_verbs.h>

#include "smc_pnet.h"
#include "smc_ib.h"
#include "smc_core.h"
#include "smc_wr.h"
#include "smc.h"

#define SMC_QP_MIN_RNR_TIMER		5
#define SMC_QP_TIMEOUT			15 /* 4096 * 2 ** timeout usec */
#define SMC_QP_RETRY_CNT			7 /* 7: infinite */
#define SMC_QP_RNR_RETRY			7 /* 7: infinite */

struct smc_ib_devices smc_ib_devices = {	/* smc-registered ib devices */
	.lock = __SPIN_LOCK_UNLOCKED(smc_ib_devices.lock),
	.list = LIST_HEAD_INIT(smc_ib_devices.list),
};

#define SMC_LOCAL_SYSTEMID_RESET	"%%%%%%%"

u8 local_systemid[SMC_SYSTEMID_LEN] = SMC_LOCAL_SYSTEMID_RESET;	/* unique system
								 * identifier
								 */

static int smc_ib_modify_qp_init(struct smc_link *lnk)
{
	struct ib_qp_attr qp_attr;

	memset(&qp_attr, 0, sizeof(qp_attr));
	qp_attr.qp_state = IB_QPS_INIT;
	qp_attr.pkey_index = 0;
	qp_attr.port_num = lnk->ibport;
	qp_attr.qp_access_flags = IB_ACCESS_LOCAL_WRITE
				| IB_ACCESS_REMOTE_WRITE;
	return ib_modify_qp(lnk->roce_qp, &qp_attr,
			    IB_QP_STATE | IB_QP_PKEY_INDEX |
			    IB_QP_ACCESS_FLAGS | IB_QP_PORT);
}

static int smc_ib_modify_qp_rtr(struct smc_link *lnk)
{
	enum ib_qp_attr_mask qp_attr_mask =
		IB_QP_STATE | IB_QP_AV | IB_QP_PATH_MTU | IB_QP_DEST_QPN |
		IB_QP_RQ_PSN | IB_QP_MAX_DEST_RD_ATOMIC | IB_QP_MIN_RNR_TIMER;
	struct ib_qp_attr qp_attr;

	memset(&qp_attr, 0, sizeof(qp_attr));
	qp_attr.qp_state = IB_QPS_RTR;
	qp_attr.path_mtu = min(lnk->path_mtu, lnk->peer_mtu);
	qp_attr.ah_attr.type = RDMA_AH_ATTR_TYPE_ROCE;
	rdma_ah_set_port_num(&qp_attr.ah_attr, lnk->ibport);
	rdma_ah_set_grh(&qp_attr.ah_attr, NULL, 0, 0, 1, 0);
	rdma_ah_set_dgid_raw(&qp_attr.ah_attr, lnk->peer_gid);
	memcpy(&qp_attr.ah_attr.roce.dmac, lnk->peer_mac,
	       sizeof(lnk->peer_mac));
	qp_attr.dest_qp_num = lnk->peer_qpn;
	qp_attr.rq_psn = lnk->peer_psn; /* starting receive packet seq # */
	qp_attr.max_dest_rd_atomic = 1; /* max # of resources for incoming
					 * requests
					 */
	qp_attr.min_rnr_timer = SMC_QP_MIN_RNR_TIMER;

	return ib_modify_qp(lnk->roce_qp, &qp_attr, qp_attr_mask);
}

int smc_ib_modify_qp_rts(struct smc_link *lnk)
{
	struct ib_qp_attr qp_attr;

	memset(&qp_attr, 0, sizeof(qp_attr));
	qp_attr.qp_state = IB_QPS_RTS;
	qp_attr.timeout = SMC_QP_TIMEOUT;	/* local ack timeout */
	qp_attr.retry_cnt = SMC_QP_RETRY_CNT;	/* retry count */
	qp_attr.rnr_retry = SMC_QP_RNR_RETRY;	/* RNR retries, 7=infinite */
	qp_attr.sq_psn = lnk->psn_initial;	/* starting send packet seq # */
	qp_attr.max_rd_atomic = 1;	/* # of outstanding RDMA reads and
					 * atomic ops allowed
					 */
	return ib_modify_qp(lnk->roce_qp, &qp_attr,
			    IB_QP_STATE | IB_QP_TIMEOUT | IB_QP_RETRY_CNT |
			    IB_QP_SQ_PSN | IB_QP_RNR_RETRY |
			    IB_QP_MAX_QP_RD_ATOMIC);
}

int smc_ib_modify_qp_reset(struct smc_link *lnk)
{
	struct ib_qp_attr qp_attr;

	memset(&qp_attr, 0, sizeof(qp_attr));
	qp_attr.qp_state = IB_QPS_RESET;
	return ib_modify_qp(lnk->roce_qp, &qp_attr, IB_QP_STATE);
}

int smc_ib_ready_link(struct smc_link *lnk)
{
	struct smc_link_group *lgr =
		container_of(lnk, struct smc_link_group, lnk[0]);
	int rc = 0;

	rc = smc_ib_modify_qp_init(lnk);
	if (rc)
		goto out;

	rc = smc_ib_modify_qp_rtr(lnk);
	if (rc)
		goto out;
	smc_wr_remember_qp_attr(lnk);
	rc = ib_req_notify_cq(lnk->smcibdev->roce_cq_recv,
			      IB_CQ_SOLICITED_MASK);
	if (rc)
		goto out;
	rc = smc_wr_rx_post_init(lnk);
	if (rc)
		goto out;
	smc_wr_remember_qp_attr(lnk);

	if (lgr->role == SMC_SERV) {
		rc = smc_ib_modify_qp_rts(lnk);
		if (rc)
			goto out;
		smc_wr_remember_qp_attr(lnk);
	}
out:
	return rc;
}

/* process context wrapper for might_sleep smc_ib_remember_port_attr */
static void smc_ib_port_event_work(struct work_struct *work)
{
	struct smc_ib_device *smcibdev = container_of(
		work, struct smc_ib_device, port_event_work);
	u8 port_idx;

	for_each_set_bit(port_idx, &smcibdev->port_event_mask, SMC_MAX_PORTS) {
		smc_ib_remember_port_attr(smcibdev, port_idx + 1);
		clear_bit(port_idx, &smcibdev->port_event_mask);
	}
}

/* can be called in IRQ context */
static void smc_ib_global_event_handler(struct ib_event_handler *handler,
					struct ib_event *ibevent)
{
	struct smc_ib_device *smcibdev;
	u8 port_idx;

	smcibdev = container_of(handler, struct smc_ib_device, event_handler);

	switch (ibevent->event) {
	case IB_EVENT_PORT_ERR:
		port_idx = ibevent->element.port_num - 1;
		set_bit(port_idx, &smcibdev->port_event_mask);
		schedule_work(&smcibdev->port_event_work);
		/* fall through */
	case IB_EVENT_DEVICE_FATAL:
		/* tbd in follow-on patch:
		 * abnormal close of corresponding connections
		 */
		break;
	case IB_EVENT_PORT_ACTIVE:
		port_idx = ibevent->element.port_num - 1;
		set_bit(port_idx, &smcibdev->port_event_mask);
		schedule_work(&smcibdev->port_event_work);
		break;
	default:
		break;
	}
}

void smc_ib_dealloc_protection_domain(struct smc_link *lnk)
{
	ib_dealloc_pd(lnk->roce_pd);
	lnk->roce_pd = NULL;
}

int smc_ib_create_protection_domain(struct smc_link *lnk)
{
	int rc;

	lnk->roce_pd = ib_alloc_pd(lnk->smcibdev->ibdev,
				   IB_PD_UNSAFE_GLOBAL_RKEY);
	rc = PTR_ERR_OR_ZERO(lnk->roce_pd);
	if (IS_ERR(lnk->roce_pd))
		lnk->roce_pd = NULL;
	return rc;
}

static void smc_ib_qp_event_handler(struct ib_event *ibevent, void *priv)
{
	switch (ibevent->event) {
	case IB_EVENT_DEVICE_FATAL:
	case IB_EVENT_GID_CHANGE:
	case IB_EVENT_PORT_ERR:
	case IB_EVENT_QP_ACCESS_ERR:
		/* tbd in follow-on patch:
		 * abnormal close of corresponding connections
		 */
		break;
	default:
		break;
	}
}

void smc_ib_destroy_queue_pair(struct smc_link *lnk)
{
	ib_destroy_qp(lnk->roce_qp);
	lnk->roce_qp = NULL;
}

/* create a queue pair within the protection domain for a link */
int smc_ib_create_queue_pair(struct smc_link *lnk)
{
	struct ib_qp_init_attr qp_attr = {
		.event_handler = smc_ib_qp_event_handler,
		.qp_context = lnk,
		.send_cq = lnk->smcibdev->roce_cq_send,
		.recv_cq = lnk->smcibdev->roce_cq_recv,
		.srq = NULL,
		.cap = {
			.max_send_wr = SMC_WR_BUF_CNT,
				/* include unsolicited rdma_writes as well,
				 * there are max. 2 RDMA_WRITE per 1 WR_SEND
				 */
			.max_recv_wr = SMC_WR_BUF_CNT * 3,
			.max_send_sge = SMC_IB_MAX_SEND_SGE,
			.max_recv_sge = 1,
		},
		.sq_sig_type = IB_SIGNAL_REQ_WR,
		.qp_type = IB_QPT_RC,
	};
	int rc;

	lnk->roce_qp = ib_create_qp(lnk->roce_pd, &qp_attr);
	rc = PTR_ERR_OR_ZERO(lnk->roce_qp);
	if (IS_ERR(lnk->roce_qp))
		lnk->roce_qp = NULL;
	else
		smc_wr_remember_qp_attr(lnk);
	return rc;
}

/* map a new TX or RX buffer to DMA */
int smc_ib_buf_map(struct smc_ib_device *smcibdev, int buf_size,
		   struct smc_buf_desc *buf_slot,
		   enum dma_data_direction data_direction)
{
	int rc = 0;

	if (buf_slot->dma_addr[SMC_SINGLE_LINK])
		return rc; /* already mapped */
	buf_slot->dma_addr[SMC_SINGLE_LINK] =
		ib_dma_map_single(smcibdev->ibdev, buf_slot->cpu_addr,
				  buf_size, data_direction);
	if (ib_dma_mapping_error(smcibdev->ibdev,
				 buf_slot->dma_addr[SMC_SINGLE_LINK]))
		rc = -EIO;
	return rc;
}

void smc_ib_buf_unmap(struct smc_ib_device *smcibdev, int buf_size,
		      struct smc_buf_desc *buf_slot,
		      enum dma_data_direction data_direction)
{
	if (!buf_slot->dma_addr[SMC_SINGLE_LINK])
		return; /* already unmapped */
	ib_dma_unmap_single(smcibdev->ibdev, *buf_slot->dma_addr, buf_size,
			    data_direction);
	buf_slot->dma_addr[SMC_SINGLE_LINK] = 0;
}

static int smc_ib_fill_gid_and_mac(struct smc_ib_device *smcibdev, u8 ibport)
{
	struct net_device *ndev;
	int rc;

	rc = ib_query_gid(smcibdev->ibdev, ibport, 0,
			  &smcibdev->gid[ibport - 1], NULL);
	/* the SMC protocol requires specification of the roce MAC address;
	 * if net_device cannot be determined, it can be derived from gid 0
	 */
	ndev = smcibdev->ibdev->get_netdev(smcibdev->ibdev, ibport);
	if (ndev) {
		memcpy(&smcibdev->mac, ndev->dev_addr, ETH_ALEN);
	} else if (!rc) {
		memcpy(&smcibdev->mac[ibport - 1][0],
		       &smcibdev->gid[ibport - 1].raw[8], 3);
		memcpy(&smcibdev->mac[ibport - 1][3],
		       &smcibdev->gid[ibport - 1].raw[13], 3);
		smcibdev->mac[ibport - 1][0] &= ~0x02;
	}
	return rc;
}

/* Create an identifier unique for this instance of SMC-R.
 * The MAC-address of the first active registered IB device
 * plus a random 2-byte number is used to create this identifier.
 * This name is delivered to the peer during connection initialization.
 */
static inline void smc_ib_define_local_systemid(struct smc_ib_device *smcibdev,
						u8 ibport)
{
	memcpy(&local_systemid[2], &smcibdev->mac[ibport - 1],
	       sizeof(smcibdev->mac[ibport - 1]));
	get_random_bytes(&local_systemid[0], 2);
}

bool smc_ib_port_active(struct smc_ib_device *smcibdev, u8 ibport)
{
	return smcibdev->pattr[ibport - 1].state == IB_PORT_ACTIVE;
}

int smc_ib_remember_port_attr(struct smc_ib_device *smcibdev, u8 ibport)
{
	int rc;

	memset(&smcibdev->pattr[ibport - 1], 0,
	       sizeof(smcibdev->pattr[ibport - 1]));
	rc = ib_query_port(smcibdev->ibdev, ibport,
			   &smcibdev->pattr[ibport - 1]);
	if (rc)
		goto out;
	rc = smc_ib_fill_gid_and_mac(smcibdev, ibport);
	if (rc)
		goto out;
	if (!strncmp(local_systemid, SMC_LOCAL_SYSTEMID_RESET,
		     sizeof(local_systemid)) &&
	    smc_ib_port_active(smcibdev, ibport))
		/* create unique system identifier */
		smc_ib_define_local_systemid(smcibdev, ibport);
out:
	return rc;
}

long smc_ib_setup_per_ibdev(struct smc_ib_device *smcibdev)
{
	struct ib_cq_init_attr cqattr =	{
		.cqe = SMC_WR_MAX_CQE, .comp_vector = 0 };
	long rc;

	smcibdev->roce_cq_send = ib_create_cq(smcibdev->ibdev,
					      smc_wr_tx_cq_handler, NULL,
					      smcibdev, &cqattr);
	rc = PTR_ERR_OR_ZERO(smcibdev->roce_cq_send);
	if (IS_ERR(smcibdev->roce_cq_send)) {
		smcibdev->roce_cq_send = NULL;
		return rc;
	}
	smcibdev->roce_cq_recv = ib_create_cq(smcibdev->ibdev,
					      smc_wr_rx_cq_handler, NULL,
					      smcibdev, &cqattr);
	rc = PTR_ERR_OR_ZERO(smcibdev->roce_cq_recv);
	if (IS_ERR(smcibdev->roce_cq_recv)) {
		smcibdev->roce_cq_recv = NULL;
		goto err;
	}
	INIT_IB_EVENT_HANDLER(&smcibdev->event_handler, smcibdev->ibdev,
			      smc_ib_global_event_handler);
	ib_register_event_handler(&smcibdev->event_handler);
	smc_wr_add_dev(smcibdev);
	smcibdev->initialized = 1;
	return rc;

err:
	ib_destroy_cq(smcibdev->roce_cq_send);
	return rc;
}

static void smc_ib_cleanup_per_ibdev(struct smc_ib_device *smcibdev)
{
	if (!smcibdev->initialized)
		return;
	smc_wr_remove_dev(smcibdev);
	ib_unregister_event_handler(&smcibdev->event_handler);
	ib_destroy_cq(smcibdev->roce_cq_recv);
	ib_destroy_cq(smcibdev->roce_cq_send);
}

static struct ib_client smc_ib_client;

/* callback function for ib_register_client() */
static void smc_ib_add_dev(struct ib_device *ibdev)
{
	struct smc_ib_device *smcibdev;

	if (ibdev->node_type != RDMA_NODE_IB_CA)
		return;

	smcibdev = kzalloc(sizeof(*smcibdev), GFP_KERNEL);
	if (!smcibdev)
		return;

	smcibdev->ibdev = ibdev;
	INIT_WORK(&smcibdev->port_event_work, smc_ib_port_event_work);

	spin_lock(&smc_ib_devices.lock);
	list_add_tail(&smcibdev->list, &smc_ib_devices.list);
	spin_unlock(&smc_ib_devices.lock);
	ib_set_client_data(ibdev, &smc_ib_client, smcibdev);
}

/* callback function for ib_register_client() */
static void smc_ib_remove_dev(struct ib_device *ibdev, void *client_data)
{
	struct smc_ib_device *smcibdev;

	smcibdev = ib_get_client_data(ibdev, &smc_ib_client);
	ib_set_client_data(ibdev, &smc_ib_client, NULL);
	spin_lock(&smc_ib_devices.lock);
	list_del_init(&smcibdev->list); /* remove from smc_ib_devices */
	spin_unlock(&smc_ib_devices.lock);
	smc_pnet_remove_by_ibdev(smcibdev);
	smc_ib_cleanup_per_ibdev(smcibdev);
	kfree(smcibdev);
}

static struct ib_client smc_ib_client = {
	.name	= "smc_ib",
	.add	= smc_ib_add_dev,
	.remove = smc_ib_remove_dev,
};

int __init smc_ib_register_client(void)
{
	return ib_register_client(&smc_ib_client);
}

void smc_ib_unregister_client(void)
{
	ib_unregister_client(&smc_ib_client);
}
