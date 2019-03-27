/*
 * Copyright (c) 2018-2019 Cavium, Inc.
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "qlnxr_def.h"
#include "rdma_common.h"
#include "qlnxr_cm.h"

void
qlnxr_inc_sw_gsi_cons(struct qlnxr_qp_hwq_info *info)
{
	info->gsi_cons = (info->gsi_cons + 1) % info->max_wr;
}

void
qlnxr_store_gsi_qp_cq(struct qlnxr_dev *dev,
		struct qlnxr_qp *qp,
		struct ib_qp_init_attr *attrs)
{
	QL_DPRINT12(dev->ha, "enter\n");
		
	dev->gsi_qp_created = 1;
	dev->gsi_sqcq = get_qlnxr_cq((attrs->send_cq));
	dev->gsi_rqcq = get_qlnxr_cq((attrs->recv_cq));
	dev->gsi_qp = qp;

	QL_DPRINT12(dev->ha, "exit\n");

	return;
}

void
qlnxr_ll2_complete_tx_packet(void *cxt,
		uint8_t connection_handle,
		void *cookie,
		dma_addr_t first_frag_addr,
		bool b_last_fragment,
		bool b_last_packet)
{
	struct qlnxr_dev *dev = (struct qlnxr_dev *)cxt;
	struct ecore_roce_ll2_packet *pkt = cookie;
	struct qlnxr_cq *cq = dev->gsi_sqcq;
	struct qlnxr_qp *qp = dev->gsi_qp;
	unsigned long flags;

	QL_DPRINT12(dev->ha, "enter\n");

	qlnx_dma_free_coherent(&dev->ha->cdev, pkt->header.vaddr,
			pkt->header.baddr, pkt->header.len);
	kfree(pkt);

	spin_lock_irqsave(&qp->q_lock, flags);

	qlnxr_inc_sw_gsi_cons(&qp->sq);

	spin_unlock_irqrestore(&qp->q_lock, flags);

	if (cq->ibcq.comp_handler)
		(*cq->ibcq.comp_handler) (&cq->ibcq, cq->ibcq.cq_context);

	QL_DPRINT12(dev->ha, "exit\n");

	return;
}

void
qlnxr_ll2_complete_rx_packet(void *cxt,
		struct ecore_ll2_comp_rx_data *data)
{
	struct qlnxr_dev *dev = (struct qlnxr_dev *)cxt;
	struct qlnxr_cq *cq = dev->gsi_rqcq;
	// struct qlnxr_qp *qp = dev->gsi_qp;
	struct qlnxr_qp *qp = NULL;
	unsigned long flags;
	uint32_t qp_num = 0;
	// uint32_t delay_count = 0, gsi_cons = 0;
	//void * dest_va;

	QL_DPRINT12(dev->ha, "enter\n");

	if (data->u.data_length_error) {
		/* TODO: add statistic */
	}

	if (data->cookie == NULL) {
		QL_DPRINT12(dev->ha, "cookie is NULL, bad sign\n");
	}

	qp_num = (0xFF << 16) | data->qp_id;

	if (data->qp_id == 1) {
		qp = dev->gsi_qp;
	} else {
		/* TODO: This will be needed for UD QP support */
		/* For RoCEv1 this is invalid */
		QL_DPRINT12(dev->ha, "invalid QP\n");
		return;
	}
	/* note: currently only one recv sg is supported */
	QL_DPRINT12(dev->ha, "MAD received on QP : %x\n", data->rx_buf_addr);

	spin_lock_irqsave(&qp->q_lock, flags);

	qp->rqe_wr_id[qp->rq.gsi_cons].rc =
		data->u.data_length_error ? -EINVAL : 0;
	qp->rqe_wr_id[qp->rq.gsi_cons].vlan_id = data->vlan;
	/* note: length stands for data length i.e. GRH is excluded */
	qp->rqe_wr_id[qp->rq.gsi_cons].sg_list[0].length =
		data->length.data_length;
	*((u32 *)&qp->rqe_wr_id[qp->rq.gsi_cons].smac[0]) =
		ntohl(data->opaque_data_0);
	*((u16 *)&qp->rqe_wr_id[qp->rq.gsi_cons].smac[4]) =
		ntohs((u16)data->opaque_data_1);

	qlnxr_inc_sw_gsi_cons(&qp->rq);

	spin_unlock_irqrestore(&qp->q_lock, flags);

	if (cq->ibcq.comp_handler)
		(*cq->ibcq.comp_handler) (&cq->ibcq, cq->ibcq.cq_context);

	QL_DPRINT12(dev->ha, "exit\n");

	return;
}

void qlnxr_ll2_release_rx_packet(void *cxt,
		u8 connection_handle,
		void *cookie,
		dma_addr_t rx_buf_addr,
		bool b_last_packet)
{
	/* Do nothing... */
}

static void
qlnxr_destroy_gsi_cq(struct qlnxr_dev *dev,
		struct ib_qp_init_attr *attrs)
{
	struct ecore_rdma_destroy_cq_in_params iparams;
	struct ecore_rdma_destroy_cq_out_params oparams;
	struct qlnxr_cq *cq;

	QL_DPRINT12(dev->ha, "enter\n");

	cq = get_qlnxr_cq((attrs->send_cq));
	iparams.icid = cq->icid;
	ecore_rdma_destroy_cq(dev->rdma_ctx, &iparams, &oparams);
	ecore_chain_free(&dev->ha->cdev, &cq->pbl);

	cq = get_qlnxr_cq((attrs->recv_cq));
	/* if a dedicated recv_cq was used, delete it too */
	if (iparams.icid != cq->icid) {
		iparams.icid = cq->icid;
		ecore_rdma_destroy_cq(dev->rdma_ctx, &iparams, &oparams);
		ecore_chain_free(&dev->ha->cdev, &cq->pbl);
	}

	QL_DPRINT12(dev->ha, "exit\n");

	return;
}

static inline int
qlnxr_check_gsi_qp_attrs(struct qlnxr_dev *dev,
		struct ib_qp_init_attr *attrs)
{
	QL_DPRINT12(dev->ha, "enter\n");

	if (attrs->cap.max_recv_sge > QLNXR_GSI_MAX_RECV_SGE) {
		QL_DPRINT11(dev->ha,
			"(attrs->cap.max_recv_sge > QLNXR_GSI_MAX_RECV_SGE)\n");
		return -EINVAL;
	}

	if (attrs->cap.max_recv_wr > QLNXR_GSI_MAX_RECV_WR) {
		QL_DPRINT11(dev->ha,
			"(attrs->cap.max_recv_wr > QLNXR_GSI_MAX_RECV_WR)\n");
		return -EINVAL;
	}

	if (attrs->cap.max_send_wr > QLNXR_GSI_MAX_SEND_WR) {
		QL_DPRINT11(dev->ha,
			"(attrs->cap.max_send_wr > QLNXR_GSI_MAX_SEND_WR)\n");
		return -EINVAL;
	}

	QL_DPRINT12(dev->ha, "exit\n");

	return 0;
}


static int
qlnxr_ll2_post_tx(struct qlnxr_dev *dev, struct ecore_roce_ll2_packet *pkt)
{
	enum ecore_ll2_roce_flavor_type roce_flavor;
	struct ecore_ll2_tx_pkt_info ll2_tx_pkt;
	int rc;
	int i;

	QL_DPRINT12(dev->ha, "enter\n");

	memset(&ll2_tx_pkt, 0, sizeof(ll2_tx_pkt));

	if (pkt->roce_mode != ROCE_V1) {
		QL_DPRINT11(dev->ha, "roce_mode != ROCE_V1\n");
		return (-1);
	}

	roce_flavor = (pkt->roce_mode == ROCE_V1) ?
		ECORE_LL2_ROCE : ECORE_LL2_RROCE;

	ll2_tx_pkt.num_of_bds = 1 /* hdr */ +  pkt->n_seg;
	ll2_tx_pkt.vlan = 0; /* ??? */
	ll2_tx_pkt.tx_dest = ECORE_LL2_TX_DEST_NW;
	ll2_tx_pkt.ecore_roce_flavor = roce_flavor;
	ll2_tx_pkt.first_frag = pkt->header.baddr;
	ll2_tx_pkt.first_frag_len = pkt->header.len;
	ll2_tx_pkt.cookie = pkt;
	ll2_tx_pkt.enable_ip_cksum = 1; // Only for RoCEv2:IPv4

	/* tx header */
	rc = ecore_ll2_prepare_tx_packet(dev->rdma_ctx,
			dev->gsi_ll2_handle,
			&ll2_tx_pkt,
			1);
	if (rc) {

		QL_DPRINT11(dev->ha, "ecore_ll2_prepare_tx_packet failed\n");

		/* TX failed while posting header - release resources*/
                qlnx_dma_free_coherent(&dev->ha->cdev,
			pkt->header.vaddr,
			pkt->header.baddr,
                        pkt->header.len);

		kfree(pkt);

		return rc;
	}

	/* tx payload */
	for (i = 0; i < pkt->n_seg; i++) {
		rc = ecore_ll2_set_fragment_of_tx_packet(dev->rdma_ctx,
						       dev->gsi_ll2_handle,
						       pkt->payload[i].baddr,
						       pkt->payload[i].len);
		if (rc) {
			/* if failed not much to do here, partial packet has
			 * been posted we can't free memory, will need to wait
			 * for completion
			 */
			QL_DPRINT11(dev->ha,
				"ecore_ll2_set_fragment_of_tx_packet failed\n");
			return rc;
		}
	}
	struct ecore_ll2_stats stats = {0};
	rc = ecore_ll2_get_stats(dev->rdma_ctx, dev->gsi_ll2_handle, &stats);
	if (rc) {
		QL_DPRINT11(dev->ha, "failed to obtain ll2 stats\n");
	}
	QL_DPRINT12(dev->ha, "exit\n");

	return 0;
}

int
qlnxr_ll2_stop(struct qlnxr_dev *dev)
{
	int rc;

	QL_DPRINT12(dev->ha, "enter\n");

	if (dev->gsi_ll2_handle == 0xFF)
		return 0;

	/* remove LL2 MAC address filter */
	rc = qlnx_rdma_ll2_set_mac_filter(dev->rdma_ctx,
			  dev->gsi_ll2_mac_address, NULL);

	rc = ecore_ll2_terminate_connection(dev->rdma_ctx,
			dev->gsi_ll2_handle);

	ecore_ll2_release_connection(dev->rdma_ctx, dev->gsi_ll2_handle);

	dev->gsi_ll2_handle = 0xFF;

	QL_DPRINT12(dev->ha, "exit rc = %d\n", rc);
	return rc;
}

int qlnxr_ll2_start(struct qlnxr_dev *dev,
		   struct ib_qp_init_attr *attrs,
		   struct qlnxr_qp *qp)
{
	struct ecore_ll2_acquire_data data;
	struct ecore_ll2_cbs cbs;
	int rc;

	QL_DPRINT12(dev->ha, "enter\n");

	/* configure and start LL2 */
	cbs.rx_comp_cb = qlnxr_ll2_complete_rx_packet;
	cbs.tx_comp_cb = qlnxr_ll2_complete_tx_packet;
	cbs.rx_release_cb = qlnxr_ll2_release_rx_packet;
	cbs.tx_release_cb = qlnxr_ll2_complete_tx_packet;
	cbs.cookie = dev;
	dev->gsi_ll2_handle = 0xFF;

	memset(&data, 0, sizeof(data));
	data.input.conn_type = ECORE_LL2_TYPE_ROCE;
	data.input.mtu = dev->ha->ifp->if_mtu;
	data.input.rx_num_desc = 8 * 1024;
	data.input.rx_drop_ttl0_flg = 1;
	data.input.rx_vlan_removal_en = 0;
	data.input.tx_num_desc = 8 * 1024;
	data.input.tx_tc = 0;
	data.input.tx_dest = ECORE_LL2_TX_DEST_NW;
	data.input.ai_err_packet_too_big = ECORE_LL2_DROP_PACKET;
	data.input.ai_err_no_buf = ECORE_LL2_DROP_PACKET;
	data.input.gsi_enable = 1;
	data.p_connection_handle = &dev->gsi_ll2_handle;
	data.cbs = &cbs;

	rc = ecore_ll2_acquire_connection(dev->rdma_ctx, &data);

	if (rc) {
		QL_DPRINT11(dev->ha,
			"ecore_ll2_acquire_connection failed: %d\n",
			rc);
		return rc;
	}

	QL_DPRINT11(dev->ha,
		"ll2 connection acquired successfully\n");
	rc = ecore_ll2_establish_connection(dev->rdma_ctx,
		dev->gsi_ll2_handle);

	if (rc) {
		QL_DPRINT11(dev->ha,
			"ecore_ll2_establish_connection failed\n", rc);
		goto err1;
	}

	QL_DPRINT11(dev->ha,
		"ll2 connection established successfully\n");
	rc = qlnx_rdma_ll2_set_mac_filter(dev->rdma_ctx, NULL,
			dev->ha->primary_mac);
	if (rc) {
		QL_DPRINT11(dev->ha, "qlnx_rdma_ll2_set_mac_filter failed\n", rc);
		goto err2;
	}

	QL_DPRINT12(dev->ha, "exit rc = %d\n", rc);
	return 0;

err2:
	ecore_ll2_terminate_connection(dev->rdma_ctx, dev->gsi_ll2_handle);
err1:
	ecore_ll2_release_connection(dev->rdma_ctx, dev->gsi_ll2_handle);

	QL_DPRINT12(dev->ha, "exit rc = %d\n", rc);
	return rc;
}

struct ib_qp*
qlnxr_create_gsi_qp(struct qlnxr_dev *dev,
		 struct ib_qp_init_attr *attrs,
		 struct qlnxr_qp *qp)
{
	int rc;

	QL_DPRINT12(dev->ha, "enter\n");

	rc = qlnxr_check_gsi_qp_attrs(dev, attrs);

	if (rc) {
		QL_DPRINT11(dev->ha, "qlnxr_check_gsi_qp_attrs failed\n");
		return ERR_PTR(rc);
	}

	rc = qlnxr_ll2_start(dev, attrs, qp);
	if (rc) {
		QL_DPRINT11(dev->ha, "qlnxr_ll2_start failed\n");
		return ERR_PTR(rc);
	}

	/* create QP */
	qp->ibqp.qp_num = 1;
	qp->rq.max_wr = attrs->cap.max_recv_wr;
	qp->sq.max_wr = attrs->cap.max_send_wr;

	qp->rqe_wr_id = kzalloc(qp->rq.max_wr * sizeof(*qp->rqe_wr_id),
				GFP_KERNEL);
	if (!qp->rqe_wr_id) {
		QL_DPRINT11(dev->ha, "(!qp->rqe_wr_id)\n");
		goto err;
	}

	qp->wqe_wr_id = kzalloc(qp->sq.max_wr * sizeof(*qp->wqe_wr_id),
				GFP_KERNEL);
	if (!qp->wqe_wr_id) {
		QL_DPRINT11(dev->ha, "(!qp->wqe_wr_id)\n");
		goto err;
	}

	qlnxr_store_gsi_qp_cq(dev, qp, attrs);
	memcpy(dev->gsi_ll2_mac_address, dev->ha->primary_mac, ETH_ALEN);

	/* the GSI CQ is handled by the driver so remove it from the FW */
	qlnxr_destroy_gsi_cq(dev, attrs);
	dev->gsi_rqcq->cq_type = QLNXR_CQ_TYPE_GSI;
	dev->gsi_rqcq->cq_type = QLNXR_CQ_TYPE_GSI;

	QL_DPRINT12(dev->ha, "exit &qp->ibqp = %p\n", &qp->ibqp);

	return &qp->ibqp;
err:
	kfree(qp->rqe_wr_id);

	rc = qlnxr_ll2_stop(dev);

	QL_DPRINT12(dev->ha, "exit with error\n");

	return ERR_PTR(-ENOMEM);
}

int
qlnxr_destroy_gsi_qp(struct qlnxr_dev *dev)
{
	int rc = 0;

	QL_DPRINT12(dev->ha, "enter\n");

	rc = qlnxr_ll2_stop(dev);

	QL_DPRINT12(dev->ha, "exit rc = %d\n", rc);
	return (rc);
}


static inline bool
qlnxr_get_vlan_id_gsi(struct ib_ah_attr *ah_attr, u16 *vlan_id)
{
	u16 tmp_vlan_id;
	union ib_gid *dgid = &ah_attr->grh.dgid;

	tmp_vlan_id = (dgid->raw[11] << 8) | dgid->raw[12];
	if (tmp_vlan_id < 0x1000) {
		*vlan_id = tmp_vlan_id;
		return true;
	} else {
		*vlan_id = 0;
		return false;
	}
}

#define QLNXR_MAX_UD_HEADER_SIZE	(100)
#define QLNXR_GSI_QPN		(1)
static inline int
qlnxr_gsi_build_header(struct qlnxr_dev *dev,
		struct qlnxr_qp *qp,
		struct ib_send_wr *swr,
		struct ib_ud_header *udh,
		int *roce_mode)
{
	bool has_vlan = false, has_grh_ipv6 = true;
	struct ib_ah_attr *ah_attr = &get_qlnxr_ah((ud_wr(swr)->ah))->attr;
	struct ib_global_route *grh = &ah_attr->grh;
	union ib_gid sgid;
	int send_size = 0;
	u16 vlan_id = 0;
	u16 ether_type;

#if __FreeBSD_version >= 1102000
	int rc = 0;
	int ip_ver = 0;
	bool has_udp = false;
#endif /* #if __FreeBSD_version >= 1102000 */


#if !DEFINE_IB_AH_ATTR_WITH_DMAC
	u8 mac[ETH_ALEN];
#endif
	int i;

	send_size = 0;
	for (i = 0; i < swr->num_sge; ++i)
		send_size += swr->sg_list[i].length;

	has_vlan = qlnxr_get_vlan_id_gsi(ah_attr, &vlan_id);
	ether_type = ETH_P_ROCE;
	*roce_mode = ROCE_V1;
	if (grh->sgid_index < QLNXR_MAX_SGID)
		sgid = dev->sgid_tbl[grh->sgid_index];
	else
		sgid = dev->sgid_tbl[0];

#if __FreeBSD_version >= 1102000

	rc = ib_ud_header_init(send_size, false /* LRH */, true /* ETH */,
			has_vlan, has_grh_ipv6, ip_ver, has_udp,
			0 /* immediate */, udh);

	if (rc) {
		QL_DPRINT11(dev->ha, "gsi post send: failed to init header\n");
		return rc;
	}

#else
	ib_ud_header_init(send_size, false /* LRH */, true /* ETH */,
			  has_vlan, has_grh_ipv6, 0 /* immediate */, udh);

#endif /* #if __FreeBSD_version >= 1102000 */

	/* ENET + VLAN headers*/
#if DEFINE_IB_AH_ATTR_WITH_DMAC
	memcpy(udh->eth.dmac_h, ah_attr->dmac, ETH_ALEN);
#else
	qlnxr_get_dmac(dev, ah_attr, mac);
	memcpy(udh->eth.dmac_h, mac, ETH_ALEN);
#endif
	memcpy(udh->eth.smac_h, dev->ha->primary_mac, ETH_ALEN);
	if (has_vlan) {
		udh->eth.type = htons(ETH_P_8021Q);
		udh->vlan.tag = htons(vlan_id);
		udh->vlan.type = htons(ether_type);
	} else {
		udh->eth.type = htons(ether_type);
	}

	for (int j = 0; j < 4; j++) {
		QL_DPRINT12(dev->ha, "destination mac: %x\n",
				udh->eth.dmac_h[j]);
	}
	for (int j = 0; j < 4; j++) {
		QL_DPRINT12(dev->ha, "source mac: %x\n",
				udh->eth.smac_h[j]);
	}
	
	QL_DPRINT12(dev->ha, "QP: %p, opcode: %d, wq: %lx, roce: %x, hops:%d,"
			"imm : %d, vlan :%d, AH: %p\n",
			qp, swr->opcode, swr->wr_id, *roce_mode, grh->hop_limit,
			0, has_vlan, get_qlnxr_ah((ud_wr(swr)->ah)));

	if (has_grh_ipv6) {
		/* GRH / IPv6 header */
		udh->grh.traffic_class = grh->traffic_class;
		udh->grh.flow_label = grh->flow_label;
		udh->grh.hop_limit = grh->hop_limit;
		udh->grh.destination_gid = grh->dgid;
		memcpy(&udh->grh.source_gid.raw, &sgid.raw,
		       sizeof(udh->grh.source_gid.raw));
		QL_DPRINT12(dev->ha, "header: tc: %x, flow_label : %x, "
			"hop_limit: %x \n", udh->grh.traffic_class,
			udh->grh.flow_label, udh->grh.hop_limit);
		for (i = 0; i < 16; i++) {
			QL_DPRINT12(dev->ha, "udh dgid = %x\n", udh->grh.destination_gid.raw[i]);
		}
		for (i = 0; i < 16; i++) {
			QL_DPRINT12(dev->ha, "udh sgid = %x\n", udh->grh.source_gid.raw[i]);
		}
		udh->grh.next_header = 0x1b;
	}
#ifdef DEFINE_IB_UD_HEADER_INIT_UDP_PRESENT 
        /* This is for RoCEv2 */
	else {
                /* IPv4 header */
                u32 ipv4_addr;

                udh->ip4.protocol = IPPROTO_UDP;
                udh->ip4.tos = htonl(grh->flow_label);
                udh->ip4.frag_off = htons(IP_DF);
                udh->ip4.ttl = grh->hop_limit;

                ipv4_addr = qedr_get_ipv4_from_gid(sgid.raw);
                udh->ip4.saddr = ipv4_addr;
                ipv4_addr = qedr_get_ipv4_from_gid(grh->dgid.raw);
                udh->ip4.daddr = ipv4_addr;
                /* note: checksum is calculated by the device */
        }
#endif

	/* BTH */
	udh->bth.solicited_event = !!(swr->send_flags & IB_SEND_SOLICITED);
	udh->bth.pkey = QLNXR_ROCE_PKEY_DEFAULT;/* TODO: ib_get_cahced_pkey?! */
	//udh->bth.destination_qpn = htonl(ud_wr(swr)->remote_qpn);
	udh->bth.destination_qpn = OSAL_CPU_TO_BE32(ud_wr(swr)->remote_qpn);
	//udh->bth.psn = htonl((qp->sq_psn++) & ((1 << 24) - 1));
	udh->bth.psn = OSAL_CPU_TO_BE32((qp->sq_psn++) & ((1 << 24) - 1));
	udh->bth.opcode = IB_OPCODE_UD_SEND_ONLY;

	/* DETH */
	//udh->deth.qkey = htonl(0x80010000); /* qp->qkey */ /* TODO: what is?! */
	//udh->deth.source_qpn = htonl(QLNXR_GSI_QPN);
	udh->deth.qkey = OSAL_CPU_TO_BE32(0x80010000); /* qp->qkey */ /* TODO: what is?! */
	udh->deth.source_qpn = OSAL_CPU_TO_BE32(QLNXR_GSI_QPN);
	QL_DPRINT12(dev->ha, "exit\n");
	return 0;
}

static inline int
qlnxr_gsi_build_packet(struct qlnxr_dev *dev,
	struct qlnxr_qp *qp, struct ib_send_wr *swr,
	struct ecore_roce_ll2_packet **p_packet)
{
	u8 ud_header_buffer[QLNXR_MAX_UD_HEADER_SIZE];
	struct ecore_roce_ll2_packet *packet;
	int roce_mode, header_size;
	struct ib_ud_header udh;
	int i, rc;

	QL_DPRINT12(dev->ha, "enter\n");

	*p_packet = NULL;

	rc = qlnxr_gsi_build_header(dev, qp, swr, &udh, &roce_mode);
	if (rc) {
		QL_DPRINT11(dev->ha,
			"qlnxr_gsi_build_header failed rc = %d\n", rc);
		return rc;
	}

	header_size = ib_ud_header_pack(&udh, &ud_header_buffer);

	packet = kzalloc(sizeof(*packet), GFP_ATOMIC);
	if (!packet) {
		QL_DPRINT11(dev->ha, "packet == NULL\n");
		return -ENOMEM;
	}

	packet->header.vaddr = qlnx_dma_alloc_coherent(&dev->ha->cdev,
					&packet->header.baddr,
					header_size);
	if (!packet->header.vaddr) {
		QL_DPRINT11(dev->ha, "packet->header.vaddr == NULL\n");
		kfree(packet);
		return -ENOMEM;
	}

	if (memcmp(udh.eth.smac_h, udh.eth.dmac_h, ETH_ALEN))
		packet->tx_dest = ECORE_ROCE_LL2_TX_DEST_NW;
	else
		packet->tx_dest = ECORE_ROCE_LL2_TX_DEST_LB;

	packet->roce_mode = roce_mode;
	memcpy(packet->header.vaddr, ud_header_buffer, header_size);
	packet->header.len = header_size;
	packet->n_seg = swr->num_sge;
	qp->wqe_wr_id[qp->sq.prod].bytes_len = IB_GRH_BYTES; //RDMA_GRH_BYTES
	for (i = 0; i < packet->n_seg; i++) {
		packet->payload[i].baddr = swr->sg_list[i].addr;
		packet->payload[i].len = swr->sg_list[i].length;
		qp->wqe_wr_id[qp->sq.prod].bytes_len +=
			packet->payload[i].len;
		QL_DPRINT11(dev->ha, "baddr: %p, len: %d\n",
				packet->payload[i].baddr,
				packet->payload[i].len);
	}

	*p_packet = packet;

	QL_DPRINT12(dev->ha, "exit, packet->n_seg: %d\n", packet->n_seg);
	return 0;
}

int
qlnxr_gsi_post_send(struct ib_qp *ibqp,
		struct ib_send_wr *wr,
		struct ib_send_wr **bad_wr)
{
	struct ecore_roce_ll2_packet *pkt = NULL;
	struct qlnxr_qp *qp = get_qlnxr_qp(ibqp);
	struct qlnxr_dev *dev = qp->dev;
	unsigned long flags;
	int rc;

	QL_DPRINT12(dev->ha, "exit\n");

	if (qp->state != ECORE_ROCE_QP_STATE_RTS) {
		QL_DPRINT11(dev->ha,
			"(qp->state != ECORE_ROCE_QP_STATE_RTS)\n");
		*bad_wr = wr;
		return -EINVAL;
	}

	if (wr->num_sge > RDMA_MAX_SGE_PER_SQ_WQE) {
		QL_DPRINT11(dev->ha,
			"(wr->num_sge > RDMA_MAX_SGE_PER_SQ_WQE)\n");
		rc = -EINVAL;
		goto err;
	}

	if (wr->opcode != IB_WR_SEND) {
		QL_DPRINT11(dev->ha, "(wr->opcode > IB_WR_SEND)\n");
		rc = -EINVAL;
		goto err;
	}

	spin_lock_irqsave(&qp->q_lock, flags);

	rc = qlnxr_gsi_build_packet(dev, qp, wr, &pkt);
	if(rc) {
		spin_unlock_irqrestore(&qp->q_lock, flags);
		QL_DPRINT11(dev->ha, "qlnxr_gsi_build_packet failed\n");
		goto err;
	}

	rc = qlnxr_ll2_post_tx(dev, pkt);

	if (!rc) {
		qp->wqe_wr_id[qp->sq.prod].wr_id = wr->wr_id;
		qp->wqe_wr_id[qp->sq.prod].signaled = 
			!!(wr->send_flags & IB_SEND_SIGNALED);
		qp->wqe_wr_id[qp->sq.prod].opcode = IB_WC_SEND;
		qlnxr_inc_sw_prod(&qp->sq);
		QL_DPRINT11(dev->ha, "packet sent over gsi qp\n");
	} else {
		QL_DPRINT11(dev->ha, "qlnxr_ll2_post_tx failed\n");
		rc = -EAGAIN;
		*bad_wr = wr;
	}

	spin_unlock_irqrestore(&qp->q_lock, flags);

	if (wr->next != NULL) {
		*bad_wr = wr->next;
		rc=-EINVAL;
	}

	QL_DPRINT12(dev->ha, "exit\n");
	return rc;

err:
	*bad_wr = wr;
	QL_DPRINT12(dev->ha, "exit error\n");
	return rc;
}

#define	QLNXR_LL2_RX_BUFFER_SIZE	(4 * 1024)
int
qlnxr_gsi_post_recv(struct ib_qp *ibqp,
		struct ib_recv_wr *wr,
		struct ib_recv_wr **bad_wr)
{
	struct qlnxr_dev *dev = get_qlnxr_dev((ibqp->device));
	struct qlnxr_qp *qp = get_qlnxr_qp(ibqp);
	unsigned long flags;
	int rc = 0;

	QL_DPRINT12(dev->ha, "enter, wr: %p\n", wr);

	if ((qp->state != ECORE_ROCE_QP_STATE_RTR) &&
	    (qp->state != ECORE_ROCE_QP_STATE_RTS)) {
		*bad_wr = wr;
		QL_DPRINT11(dev->ha, "exit 0\n");
		return -EINVAL;
	}

	spin_lock_irqsave(&qp->q_lock, flags);

	while (wr) {
		if (wr->num_sge > QLNXR_GSI_MAX_RECV_SGE) {
			QL_DPRINT11(dev->ha, "exit 1\n");
			goto err;
		}

		rc = ecore_ll2_post_rx_buffer(dev->rdma_ctx,
				dev->gsi_ll2_handle,
				wr->sg_list[0].addr,
				wr->sg_list[0].length,
				0 /* cookie */,
				1 /* notify_fw */);
		if (rc) {
			QL_DPRINT11(dev->ha, "exit 2\n");
			goto err;
		}

		memset(&qp->rqe_wr_id[qp->rq.prod], 0,
			sizeof(qp->rqe_wr_id[qp->rq.prod]));
		qp->rqe_wr_id[qp->rq.prod].sg_list[0] = wr->sg_list[0];
		qp->rqe_wr_id[qp->rq.prod].wr_id = wr->wr_id;

		qlnxr_inc_sw_prod(&qp->rq);

		wr = wr->next;
	}

	spin_unlock_irqrestore(&qp->q_lock, flags);

	QL_DPRINT12(dev->ha, "exit rc = %d\n", rc);
	return rc;
err:

	spin_unlock_irqrestore(&qp->q_lock, flags);
	*bad_wr = wr;

	QL_DPRINT12(dev->ha, "exit with -ENOMEM\n");
	return -ENOMEM;
}

int
qlnxr_gsi_poll_cq(struct ib_cq *ibcq, int num_entries, struct ib_wc *wc)
{
	struct qlnxr_dev *dev = get_qlnxr_dev((ibcq->device));
	struct qlnxr_cq *cq = get_qlnxr_cq(ibcq);
	struct qlnxr_qp *qp = dev->gsi_qp;
	unsigned long flags;
	int i = 0;

	QL_DPRINT12(dev->ha, "enter\n");

	spin_lock_irqsave(&cq->cq_lock, flags);

	while (i < num_entries && qp->rq.cons != qp->rq.gsi_cons) {
		memset(&wc[i], 0, sizeof(*wc));

		wc[i].qp = &qp->ibqp;
		wc[i].wr_id = qp->rqe_wr_id[qp->rq.cons].wr_id;
		wc[i].opcode = IB_WC_RECV;
		wc[i].pkey_index = 0;
		wc[i].status = (qp->rqe_wr_id[qp->rq.cons].rc)?
			       IB_WC_GENERAL_ERR:IB_WC_SUCCESS;
		/* 0 - currently only one recv sg is supported */
		wc[i].byte_len = qp->rqe_wr_id[qp->rq.cons].sg_list[0].length;
		wc[i].wc_flags |= IB_WC_GRH | IB_WC_IP_CSUM_OK;

#if __FreeBSD_version >= 1100000
		memcpy(&wc[i].smac, qp->rqe_wr_id[qp->rq.cons].smac, ETH_ALEN);
		wc[i].wc_flags |= IB_WC_WITH_SMAC;

		if (qp->rqe_wr_id[qp->rq.cons].vlan_id) {
			wc[i].wc_flags |= IB_WC_WITH_VLAN;
			wc[i].vlan_id = qp->rqe_wr_id[qp->rq.cons].vlan_id;
		}

#endif
		qlnxr_inc_sw_cons(&qp->rq);
		i++;
	}

	while (i < num_entries && qp->sq.cons != qp->sq.gsi_cons) {
		memset(&wc[i], 0, sizeof(*wc));

		wc[i].qp = &qp->ibqp;
		wc[i].wr_id = qp->wqe_wr_id[qp->sq.cons].wr_id;
		wc[i].opcode = IB_WC_SEND;
		wc[i].status = IB_WC_SUCCESS;

		qlnxr_inc_sw_cons(&qp->sq);
		i++;
	}

	spin_unlock_irqrestore(&cq->cq_lock, flags);

	QL_DPRINT12(dev->ha, "exit i = %d\n", i);
	return i;
}

