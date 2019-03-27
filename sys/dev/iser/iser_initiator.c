/* $FreeBSD$ */
/*-
 * Copyright (c) 2015, Mellanox Technologies, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "icl_iser.h"

static MALLOC_DEFINE(M_ISER_INITIATOR, "iser_initiator", "iser initiator backend");

/* Register user buffer memory and initialize passive rdma
 *  dto descriptor. Data size is stored in
 *  task->data[ISER_DIR_IN].data_len, Protection size
 *  os stored in task->prot[ISER_DIR_IN].data_len
 */
static int
iser_prepare_read_cmd(struct icl_iser_pdu *iser_pdu)
{
	struct iser_hdr *hdr = &iser_pdu->desc.iser_header;
	struct iser_data_buf *buf_in = &iser_pdu->data[ISER_DIR_IN];
	struct iser_mem_reg *mem_reg;
	int err;

	err = iser_dma_map_task_data(iser_pdu,
				     buf_in,
				     ISER_DIR_IN,
				     DMA_FROM_DEVICE);
	if (err)
		return (err);

	err = iser_reg_rdma_mem(iser_pdu, ISER_DIR_IN);
	if (err) {
		ISER_ERR("Failed to set up Data-IN RDMA");
		return (err);
	}

	mem_reg = &iser_pdu->rdma_reg[ISER_DIR_IN];

	hdr->flags    |= ISER_RSV;
	hdr->read_stag = cpu_to_be32(mem_reg->rkey);
	hdr->read_va   = cpu_to_be64(mem_reg->sge.addr);

	return (0);
}

/* Register user buffer memory and initialize passive rdma
 *  dto descriptor. Data size is stored in
 *  task->data[ISER_DIR_OUT].data_len, Protection size
 *  is stored at task->prot[ISER_DIR_OUT].data_len
 */
static int
iser_prepare_write_cmd(struct icl_iser_pdu *iser_pdu)
{
	struct iser_hdr *hdr = &iser_pdu->desc.iser_header;
	struct iser_data_buf *buf_out = &iser_pdu->data[ISER_DIR_OUT];
	struct iser_mem_reg *mem_reg;
	int err;

	err = iser_dma_map_task_data(iser_pdu,
				     buf_out,
				     ISER_DIR_OUT,
				     DMA_TO_DEVICE);
	if (err)
		return (err);

	err = iser_reg_rdma_mem(iser_pdu, ISER_DIR_OUT);
	if (err) {
		ISER_ERR("Failed to set up Data-out RDMA");
		return (err);
	}

	mem_reg = &iser_pdu->rdma_reg[ISER_DIR_OUT];

	hdr->flags     |= ISER_WSV;
	hdr->write_stag = cpu_to_be32(mem_reg->rkey);
	hdr->write_va   = cpu_to_be64(mem_reg->sge.addr);

	return (0);
}

/* creates a new tx descriptor and adds header regd buffer */
void
iser_create_send_desc(struct iser_conn *iser_conn,
		      struct iser_tx_desc *tx_desc)
{
	struct iser_device *device = iser_conn->ib_conn.device;

	ib_dma_sync_single_for_cpu(device->ib_device,
		tx_desc->dma_addr, ISER_HEADERS_LEN, DMA_TO_DEVICE);

	memset(&tx_desc->iser_header, 0, sizeof(struct iser_hdr));
	tx_desc->iser_header.flags = ISER_VER;

	tx_desc->num_sge = 1;

	if (tx_desc->tx_sg[0].lkey != device->mr->lkey) {
		tx_desc->tx_sg[0].lkey = device->mr->lkey;
		ISER_DBG("sdesc %p lkey mismatch, fixing", tx_desc);
	}
}

void
iser_free_login_buf(struct iser_conn *iser_conn)
{
	struct iser_device *device = iser_conn->ib_conn.device;

	if (!iser_conn->login_buf)
		return;

	if (iser_conn->login_req_dma)
		ib_dma_unmap_single(device->ib_device,
				    iser_conn->login_req_dma,
				    ISCSI_DEF_MAX_RECV_SEG_LEN, DMA_TO_DEVICE);

	if (iser_conn->login_resp_dma)
		ib_dma_unmap_single(device->ib_device,
				    iser_conn->login_resp_dma,
				    ISER_RX_LOGIN_SIZE, DMA_FROM_DEVICE);

	free(iser_conn->login_buf, M_ISER_INITIATOR);

	/* make sure we never redo any unmapping */
	iser_conn->login_req_dma = 0;
	iser_conn->login_resp_dma = 0;
	iser_conn->login_buf = NULL;
}

int
iser_alloc_login_buf(struct iser_conn *iser_conn)
{
	struct iser_device *device = iser_conn->ib_conn.device;
	int req_err, resp_err;

	BUG_ON(device == NULL);

	iser_conn->login_buf = malloc(ISCSI_DEF_MAX_RECV_SEG_LEN + ISER_RX_LOGIN_SIZE,
				      M_ISER_INITIATOR, M_WAITOK | M_ZERO);

	if (!iser_conn->login_buf)
		goto out_err;

	iser_conn->login_req_buf  = iser_conn->login_buf;
	iser_conn->login_resp_buf = iser_conn->login_buf +
				    ISCSI_DEF_MAX_RECV_SEG_LEN;

	iser_conn->login_req_dma = ib_dma_map_single(device->ib_device,
						     iser_conn->login_req_buf,
						     ISCSI_DEF_MAX_RECV_SEG_LEN,
						     DMA_TO_DEVICE);

	iser_conn->login_resp_dma = ib_dma_map_single(device->ib_device,
						      iser_conn->login_resp_buf,
						      ISER_RX_LOGIN_SIZE,
						      DMA_FROM_DEVICE);

	req_err  = ib_dma_mapping_error(device->ib_device,
					iser_conn->login_req_dma);
	resp_err = ib_dma_mapping_error(device->ib_device,
					iser_conn->login_resp_dma);

	if (req_err || resp_err) {
		if (req_err)
			iser_conn->login_req_dma = 0;
		if (resp_err)
			iser_conn->login_resp_dma = 0;
		goto free_login_buf;
	}

	return (0);

free_login_buf:
	iser_free_login_buf(iser_conn);

out_err:
	ISER_DBG("unable to alloc or map login buf");
	return (ENOMEM);
}

int iser_alloc_rx_descriptors(struct iser_conn *iser_conn, int cmds_max)
{
	int i, j;
	u64 dma_addr;
	struct iser_rx_desc *rx_desc;
	struct ib_sge       *rx_sg;
	struct ib_conn *ib_conn = &iser_conn->ib_conn;
	struct iser_device *device = ib_conn->device;

	iser_conn->qp_max_recv_dtos = cmds_max;
	iser_conn->min_posted_rx = iser_conn->qp_max_recv_dtos >> 2;

	if (iser_create_fastreg_pool(ib_conn, cmds_max))
		goto create_rdma_reg_res_failed;


	iser_conn->num_rx_descs = cmds_max;
	iser_conn->rx_descs = malloc(iser_conn->num_rx_descs *
				sizeof(struct iser_rx_desc), M_ISER_INITIATOR,
				M_WAITOK | M_ZERO);
	if (!iser_conn->rx_descs)
		goto rx_desc_alloc_fail;

	rx_desc = iser_conn->rx_descs;

	for (i = 0; i < iser_conn->qp_max_recv_dtos; i++, rx_desc++)  {
		dma_addr = ib_dma_map_single(device->ib_device, (void *)rx_desc,
					ISER_RX_PAYLOAD_SIZE, DMA_FROM_DEVICE);
		if (ib_dma_mapping_error(device->ib_device, dma_addr))
			goto rx_desc_dma_map_failed;

		rx_desc->dma_addr = dma_addr;

		rx_sg = &rx_desc->rx_sg;
		rx_sg->addr   = rx_desc->dma_addr;
		rx_sg->length = ISER_RX_PAYLOAD_SIZE;
		rx_sg->lkey   = device->mr->lkey;
	}

	iser_conn->rx_desc_head = 0;

	return (0);

rx_desc_dma_map_failed:
	rx_desc = iser_conn->rx_descs;
	for (j = 0; j < i; j++, rx_desc++)
		ib_dma_unmap_single(device->ib_device, rx_desc->dma_addr,
				    ISER_RX_PAYLOAD_SIZE, DMA_FROM_DEVICE);
	free(iser_conn->rx_descs, M_ISER_INITIATOR);
	iser_conn->rx_descs = NULL;
rx_desc_alloc_fail:
	iser_free_fastreg_pool(ib_conn);
create_rdma_reg_res_failed:
	ISER_ERR("failed allocating rx descriptors / data buffers");

	return (ENOMEM);
}

void
iser_free_rx_descriptors(struct iser_conn *iser_conn)
{
	int i;
	struct iser_rx_desc *rx_desc;
	struct ib_conn *ib_conn = &iser_conn->ib_conn;
	struct iser_device *device = ib_conn->device;

	iser_free_fastreg_pool(ib_conn);

	rx_desc = iser_conn->rx_descs;
	for (i = 0; i < iser_conn->qp_max_recv_dtos; i++, rx_desc++)
		ib_dma_unmap_single(device->ib_device, rx_desc->dma_addr,
				    ISER_RX_PAYLOAD_SIZE, DMA_FROM_DEVICE);

	free(iser_conn->rx_descs, M_ISER_INITIATOR);

	/* make sure we never redo any unmapping */
	iser_conn->rx_descs = NULL;
}

static void
iser_buf_to_sg(void *buf, struct iser_data_buf *data_buf)
{
	struct scatterlist *sg;
	int i;
	size_t len, tlen;
	int offset;

	tlen = data_buf->data_len;

	for (i = 0; 0 < tlen; i++, tlen -= len)  {
		sg = &data_buf->sgl[i];
		offset = ((uintptr_t)buf) & ~PAGE_MASK;
		len = min(PAGE_SIZE - offset, tlen);
		sg_set_buf(sg, buf, len);
		buf = (void *)(((u64)buf) + (u64)len);
	}

	data_buf->size = i;
	sg_mark_end(sg);
}


static void
iser_bio_to_sg(struct bio *bp, struct iser_data_buf *data_buf)
{
	struct scatterlist *sg;
	int i;
	size_t len, tlen;
	int offset;

	tlen = bp->bio_bcount;
	offset = bp->bio_ma_offset;

	for (i = 0; 0 < tlen; i++, tlen -= len) {
		sg = &data_buf->sgl[i];
		len = min(PAGE_SIZE - offset, tlen);
		sg_set_page(sg, bp->bio_ma[i], len, offset);
		offset = 0;
	}

	data_buf->size = i;
	sg_mark_end(sg);
}

static int
iser_csio_to_sg(struct ccb_scsiio *csio, struct iser_data_buf *data_buf)
{
	struct ccb_hdr *ccbh;
	int err = 0;

	ccbh = &csio->ccb_h;
	switch ((ccbh->flags & CAM_DATA_MASK)) {
		case CAM_DATA_BIO:
			iser_bio_to_sg((struct bio *) csio->data_ptr, data_buf);
			break;
		case CAM_DATA_VADDR:
			/*
			 * Support KVA buffers for various scsi commands such as:
			 *  - REPORT_LUNS
			 *  - MODE_SENSE_6
			 *  - INQUIRY
			 *  - SERVICE_ACTION_IN.
			 * The data of these commands always mapped into KVA.
			 */
			iser_buf_to_sg(csio->data_ptr, data_buf);
			break;
		default:
			ISER_ERR("flags 0x%X unimplemented", ccbh->flags);
			err = EINVAL;
	}
	return (err);
}

static inline bool
iser_signal_comp(u8 sig_count)
{
	return ((sig_count % ISER_SIGNAL_CMD_COUNT) == 0);
}

int
iser_send_command(struct iser_conn *iser_conn,
		  struct icl_iser_pdu *iser_pdu)
{
	struct iser_data_buf *data_buf;
	struct iser_tx_desc *tx_desc = &iser_pdu->desc;
	struct iscsi_bhs_scsi_command *hdr = (struct iscsi_bhs_scsi_command *) &(iser_pdu->desc.iscsi_header);
	struct ccb_scsiio *csio = iser_pdu->csio;
	int err = 0;
	u8 sig_count = ++iser_conn->ib_conn.sig_count;

	/* build the tx desc regd header and add it to the tx desc dto */
	tx_desc->type = ISCSI_TX_SCSI_COMMAND;
	iser_create_send_desc(iser_conn, tx_desc);

	if (hdr->bhssc_flags & BHSSC_FLAGS_R) {
		data_buf = &iser_pdu->data[ISER_DIR_IN];
	} else {
		data_buf = &iser_pdu->data[ISER_DIR_OUT];
	}

	data_buf->sg = csio->data_ptr;
	data_buf->data_len = csio->dxfer_len;

	if (likely(csio->dxfer_len)) {
		err = iser_csio_to_sg(csio, data_buf);
		if (unlikely(err))
			goto send_command_error;
	}

	if (hdr->bhssc_flags & BHSSC_FLAGS_R) {
		err = iser_prepare_read_cmd(iser_pdu);
		if (err)
			goto send_command_error;
	} else if (hdr->bhssc_flags & BHSSC_FLAGS_W) {
		err = iser_prepare_write_cmd(iser_pdu);
		if (err)
			goto send_command_error;
	}

	err = iser_post_send(&iser_conn->ib_conn, tx_desc,
			     iser_signal_comp(sig_count));
	if (!err)
		return (0);

send_command_error:
	ISER_ERR("iser_conn %p itt %u len %u err %d", iser_conn,
			hdr->bhssc_initiator_task_tag,
			hdr->bhssc_expected_data_transfer_length,
			err);
	return (err);
}

int
iser_send_control(struct iser_conn *iser_conn,
		  struct icl_iser_pdu *iser_pdu)
{
	struct iser_tx_desc *mdesc;
	struct iser_device *device;
	size_t datalen = iser_pdu->icl_pdu.ip_data_len;
	int err;

	mdesc = &iser_pdu->desc;

	/* build the tx desc regd header and add it to the tx desc dto */
	mdesc->type = ISCSI_TX_CONTROL;
	iser_create_send_desc(iser_conn, mdesc);

	device = iser_conn->ib_conn.device;

	if (datalen > 0) {
		struct ib_sge *tx_dsg = &mdesc->tx_sg[1];
		ib_dma_sync_single_for_cpu(device->ib_device,
				iser_conn->login_req_dma, datalen,
				DMA_TO_DEVICE);

		ib_dma_sync_single_for_device(device->ib_device,
			iser_conn->login_req_dma, datalen,
			DMA_TO_DEVICE);

		tx_dsg->addr    = iser_conn->login_req_dma;
		tx_dsg->length  = datalen;
		tx_dsg->lkey    = device->mr->lkey;
		mdesc->num_sge = 2;
	}

	/* For login phase and discovery session we re-use the login buffer */
	if (!iser_conn->handoff_done) {
		err = iser_post_recvl(iser_conn);
		if (err)
			goto send_control_error;
	}

	err = iser_post_send(&iser_conn->ib_conn, mdesc, true);
	if (!err)
		return (0);

send_control_error:
	ISER_ERR("conn %p failed err %d", iser_conn, err);

	return (err);

}

/**
 * iser_rcv_dto_completion - recv DTO completion
 */
void
iser_rcv_completion(struct iser_rx_desc *rx_desc,
		    unsigned long rx_xfer_len,
		    struct ib_conn *ib_conn)
{
	struct iser_conn *iser_conn = container_of(ib_conn, struct iser_conn,
						   ib_conn);
	struct icl_conn *ic = &iser_conn->icl_conn;
	struct icl_pdu *response;
	struct iscsi_bhs *hdr;
	u64 rx_dma;
	int rx_buflen;
	int outstanding, count, err;

	/* differentiate between login to all other PDUs */
	if ((char *)rx_desc == iser_conn->login_resp_buf) {
		rx_dma = iser_conn->login_resp_dma;
		rx_buflen = ISER_RX_LOGIN_SIZE;
	} else {
		rx_dma = rx_desc->dma_addr;
		rx_buflen = ISER_RX_PAYLOAD_SIZE;
	}

	ib_dma_sync_single_for_cpu(ib_conn->device->ib_device, rx_dma,
				   rx_buflen, DMA_FROM_DEVICE);

	hdr = &rx_desc->iscsi_header;

	response = iser_new_pdu(ic, M_NOWAIT);
	response->ip_bhs = hdr;
	response->ip_data_len = rx_xfer_len - ISER_HEADERS_LEN;

	/*
	 * In case we got data in the receive buffer, assign the ip_data_mbuf
	 * to the rx_buffer - later we'll copy it to upper layer buffers
	 */
	if (response->ip_data_len)
		response->ip_data_mbuf = (struct mbuf *)(rx_desc->data);

	ib_dma_sync_single_for_device(ib_conn->device->ib_device, rx_dma,
				      rx_buflen, DMA_FROM_DEVICE);

	/* decrementing conn->post_recv_buf_count only --after-- freeing the   *
	 * task eliminates the need to worry on tasks which are completed in   *
	 * parallel to the execution of iser_conn_term. So the code that waits *
	 * for the posted rx bufs refcount to become zero handles everything   */
	ib_conn->post_recv_buf_count--;

	if (rx_dma == iser_conn->login_resp_dma)
		goto receive;

	outstanding = ib_conn->post_recv_buf_count;
	if (outstanding + iser_conn->min_posted_rx <= iser_conn->qp_max_recv_dtos) {
		count = min(iser_conn->qp_max_recv_dtos - outstanding,
			    iser_conn->min_posted_rx);
		err = iser_post_recvm(iser_conn, count);
		if (err)
			ISER_ERR("posting %d rx bufs err %d", count, err);
	}

receive:
	(ic->ic_receive)(response);
}

void
iser_snd_completion(struct iser_tx_desc *tx_desc,
		    struct ib_conn *ib_conn)
{
	struct icl_iser_pdu *iser_pdu = container_of(tx_desc, struct icl_iser_pdu, desc);
	struct iser_conn *iser_conn = iser_pdu->iser_conn;

	if (tx_desc && tx_desc->type == ISCSI_TX_CONTROL)
		iser_pdu_free(&iser_conn->icl_conn, &iser_pdu->icl_pdu);
}
