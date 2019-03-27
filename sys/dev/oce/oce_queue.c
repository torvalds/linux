/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2013 Emulex
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Emulex Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Contact Information:
 * freebsd-drivers@emulex.com
 *
 * Emulex
 * 3333 Susan Street
 * Costa Mesa, CA 92626
 */

/* $FreeBSD$ */

#include "oce_if.h"

/*****************************************************
 * local queue functions
 *****************************************************/

static struct oce_wq *oce_wq_init(POCE_SOFTC sc,
				  uint32_t q_len, uint32_t wq_type);
static int oce_wq_create(struct oce_wq *wq, struct oce_eq *eq);
static void oce_wq_free(struct oce_wq *wq);
static void oce_wq_del(struct oce_wq *wq);
static struct oce_rq *oce_rq_init(POCE_SOFTC sc,
				  uint32_t q_len,
				  uint32_t frag_size,
				  uint32_t mtu, uint32_t rss);
static int oce_rq_create(struct oce_rq *rq, uint32_t if_id, struct oce_eq *eq);
static void oce_rq_free(struct oce_rq *rq);
static void oce_rq_del(struct oce_rq *rq);
static struct oce_eq *oce_eq_create(POCE_SOFTC sc,
				    uint32_t q_len,
				    uint32_t item_size,
				    uint32_t eq_delay,
				    uint32_t vector);
static void oce_eq_del(struct oce_eq *eq);
static struct oce_mq *oce_mq_create(POCE_SOFTC sc,
				    struct oce_eq *eq, uint32_t q_len);
static void oce_mq_free(struct oce_mq *mq);
static int oce_destroy_q(POCE_SOFTC sc, struct oce_mbx
			 *mbx, size_t req_size, enum qtype qtype, int version);
struct oce_cq *oce_cq_create(POCE_SOFTC sc,
			     struct oce_eq *eq,
			     uint32_t q_len,
			     uint32_t item_size,
			     uint32_t sol_event,
			     uint32_t is_eventable,
			     uint32_t nodelay, uint32_t ncoalesce);
static void oce_cq_del(POCE_SOFTC sc, struct oce_cq *cq);



/**
 * @brief	Create and initialize all the queues on the board
 * @param sc	software handle to the device
 * @returns 0	if successful, or error
 **/
int
oce_queue_init_all(POCE_SOFTC sc)
{
	int rc = 0, i, vector;
	struct oce_wq *wq;
	struct oce_rq *rq;
	struct oce_aic_obj *aic;

	/* alloc TX/RX queues */
	for_all_wq_queues(sc, wq, i) {
		sc->wq[i] = oce_wq_init(sc, sc->tx_ring_size,
					 NIC_WQ_TYPE_STANDARD);
		if (!sc->wq[i]) 
			goto error;
		
	}

	for_all_rq_queues(sc, rq, i) {
		sc->rq[i] = oce_rq_init(sc, sc->rx_ring_size, sc->rq_frag_size,
					OCE_MAX_JUMBO_FRAME_SIZE,
					(i == 0) ? 0 : is_rss_enabled(sc));
		if (!sc->rq[i]) 
			goto error;
	}

	/* Create network interface on card */
	if (oce_create_nw_interface(sc))
		goto error;

	/* create all of the event queues */
	for (vector = 0; vector < sc->intr_count; vector++) {
		/* setup aic defaults for each event queue */
		aic = &sc->aic_obj[vector];
		aic->max_eqd = OCE_MAX_EQD;
		aic->min_eqd = OCE_MIN_EQD;
		aic->et_eqd = OCE_MIN_EQD;
		aic->enable = TRUE;
	
		sc->eq[vector] = oce_eq_create(sc, sc->enable_hwlro ? EQ_LEN_2048 : EQ_LEN_1024,
						EQE_SIZE_4,0, vector);	

		if (!sc->eq[vector])
			goto error;
	}

	/* create Tx, Rx and mcc queues */
	for_all_wq_queues(sc, wq, i) {
		rc = oce_wq_create(wq, sc->eq[i]);
		if (rc)
			goto error;
		wq->queue_index = i;
		TASK_INIT(&wq->txtask, 1, oce_tx_task, wq);
	}

	for_all_rq_queues(sc, rq, i) {
		rc = oce_rq_create(rq, sc->if_id,
					sc->eq[(i == 0) ? 0:(i-1)]);
		if (rc)
			goto error;
		rq->queue_index = i;
	}

	sc->mq = oce_mq_create(sc, sc->eq[0], 64);
	if (!sc->mq)
		goto error;

	return rc;

error:
	oce_queue_release_all(sc);
	return 1;
}



/**
 * @brief Releases all mailbox queues created
 * @param sc		software handle to the device
 */
void
oce_queue_release_all(POCE_SOFTC sc)
{
	int i = 0;
	struct oce_wq *wq;
	struct oce_rq *rq;
	struct oce_eq *eq;

	/* before deleting lro queues, we have to disable hwlro	*/
	if(sc->enable_hwlro)
		oce_mbox_nic_set_iface_lro_config(sc, 0);

	for_all_rq_queues(sc, rq, i) {
		if (rq) {
			oce_rq_del(sc->rq[i]);
			oce_rq_free(sc->rq[i]);
		}
	}

	for_all_wq_queues(sc, wq, i) {
		if (wq) {
			oce_wq_del(sc->wq[i]);
			oce_wq_free(sc->wq[i]);
		}
	}

	if (sc->mq)
		oce_mq_free(sc->mq);

	for_all_evnt_queues(sc, eq, i) {
		if (eq)
			oce_eq_del(sc->eq[i]);
	}
}



/**
 * @brief 		Function to create a WQ for NIC Tx
 * @param sc 		software handle to the device
 * @param qlen		number of entries in the queue
 * @param wq_type	work queue type
 * @returns		the pointer to the WQ created or NULL on failure
 */
static struct
oce_wq *oce_wq_init(POCE_SOFTC sc, uint32_t q_len, uint32_t wq_type)
{
	struct oce_wq *wq;
	int rc = 0, i;

	/* q_len must be min 256 and max 2k */
	if (q_len < 256 || q_len > 2048) {
		device_printf(sc->dev,
			  "Invalid q length. Must be "
			  "[256, 2000]: 0x%x\n", q_len);
		return NULL;
	}

	/* allocate wq */
	wq = malloc(sizeof(struct oce_wq), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (!wq)
		return NULL;

	/* Set the wq config */
	wq->cfg.q_len = q_len;
	wq->cfg.wq_type = (uint8_t) wq_type;
	wq->cfg.eqd = OCE_DEFAULT_WQ_EQD;
	wq->cfg.nbufs = 2 * wq->cfg.q_len;
	wq->cfg.nhdl = 2 * wq->cfg.q_len;

	wq->parent = (void *)sc;

	rc = bus_dma_tag_create(bus_get_dma_tag(sc->dev),
				1, 0,
				BUS_SPACE_MAXADDR,
				BUS_SPACE_MAXADDR,
				NULL, NULL,
				OCE_MAX_TX_SIZE,
				OCE_MAX_TX_ELEMENTS,
				PAGE_SIZE, 0, NULL, NULL, &wq->tag);

	if (rc)
		goto free_wq;


	for (i = 0; i < OCE_WQ_PACKET_ARRAY_SIZE; i++) {
		rc = bus_dmamap_create(wq->tag, 0, &wq->pckts[i].map);
		if (rc) 
			goto free_wq;
	}

	wq->ring = oce_create_ring_buffer(sc, q_len, NIC_WQE_SIZE);
	if (!wq->ring)
		goto free_wq;


	LOCK_CREATE(&wq->tx_lock, "TX_lock");
	LOCK_CREATE(&wq->tx_compl_lock, "WQ_HANDLER_LOCK");
	
#if __FreeBSD_version >= 800000
	/* Allocate buf ring for multiqueue*/
	wq->br = buf_ring_alloc(4096, M_DEVBUF,
			M_WAITOK, &wq->tx_lock.mutex);
	if (!wq->br)
		goto free_wq;
#endif
	return wq;


free_wq:
	device_printf(sc->dev, "Create WQ failed\n");
	oce_wq_free(wq);
	return NULL;
}



/**
 * @brief 		Frees the work queue
 * @param wq		pointer to work queue to free
 */
static void
oce_wq_free(struct oce_wq *wq)
{
	POCE_SOFTC sc = (POCE_SOFTC) wq->parent;
	int i;

	taskqueue_drain(taskqueue_swi, &wq->txtask);

	if (wq->ring != NULL) {
		oce_destroy_ring_buffer(sc, wq->ring);
		wq->ring = NULL;
	}

	for (i = 0; i < OCE_WQ_PACKET_ARRAY_SIZE; i++) {
		if (wq->pckts[i].map != NULL) {
			bus_dmamap_unload(wq->tag, wq->pckts[i].map);
			bus_dmamap_destroy(wq->tag, wq->pckts[i].map);
			wq->pckts[i].map = NULL;
		}
	}

	if (wq->tag != NULL)
		bus_dma_tag_destroy(wq->tag);
	if (wq->br != NULL)
		buf_ring_free(wq->br, M_DEVBUF);

	LOCK_DESTROY(&wq->tx_lock);
	LOCK_DESTROY(&wq->tx_compl_lock);
	free(wq, M_DEVBUF);
}



/**
 * @brief 		Create a work queue
 * @param wq		pointer to work queue
 * @param eq		pointer to associated event queue
 */
static int
oce_wq_create(struct oce_wq *wq, struct oce_eq *eq)
{
	POCE_SOFTC sc = wq->parent;
	struct oce_cq *cq;
	int rc = 0;

	/* create the CQ */
	cq = oce_cq_create(sc,
			   eq,
			   CQ_LEN_1024,
			   sizeof(struct oce_nic_tx_cqe), 0, 1, 0, 3);
	if (!cq)
		return ENXIO;


	wq->cq = cq;

	rc = oce_mbox_create_wq(wq);
	if (rc)
		goto error;

	wq->qstate = QCREATED;
	wq->wq_free = wq->cfg.q_len;
	wq->ring->cidx = 0;
	wq->ring->pidx = 0;

	eq->cq[eq->cq_valid] = cq;
	eq->cq_valid++;
	cq->cb_arg = wq;
	cq->cq_handler = oce_wq_handler;

	return 0;

error:
	device_printf(sc->dev, "WQ create failed\n");
	oce_wq_del(wq);
	return rc;
}




/**
 * @brief 		Delete a work queue
 * @param wq		pointer to work queue
 */
static void
oce_wq_del(struct oce_wq *wq)
{
	struct oce_mbx mbx;
	struct mbx_delete_nic_wq *fwcmd;
	POCE_SOFTC sc = (POCE_SOFTC) wq->parent;

	if (wq->qstate == QCREATED) {
		bzero(&mbx, sizeof(struct oce_mbx));
		/* now fill the command */
		fwcmd = (struct mbx_delete_nic_wq *)&mbx.payload;
		fwcmd->params.req.wq_id = wq->wq_id;
		(void)oce_destroy_q(sc, &mbx,
				sizeof(struct mbx_delete_nic_wq), QTYPE_WQ, 0);
		wq->qstate = QDELETED;
	}

	if (wq->cq != NULL) {
		oce_cq_del(sc, wq->cq);
		wq->cq = NULL;
	}
}



/**
 * @brief 		function to allocate receive queue resources
 * @param sc		software handle to the device
 * @param q_len		length of receive queue
 * @param frag_size	size of an receive queue fragment
 * @param mtu		maximum transmission unit
 * @param rss		is-rss-queue flag
 * @returns		the pointer to the RQ created or NULL on failure
 */
static struct
oce_rq *oce_rq_init(POCE_SOFTC sc,
				  uint32_t q_len,
				  uint32_t frag_size,
				  uint32_t mtu, uint32_t rss)
{
	struct oce_rq *rq;
	int rc = 0, i;

	if (OCE_LOG2(frag_size) <= 0)
		return NULL;
	
	if ((q_len == 0) || (q_len > 1024))
		return NULL;

	/* allocate the rq */
	rq = malloc(sizeof(struct oce_rq), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (!rq) 
		return NULL;

	
	rq->cfg.q_len = q_len;
	rq->cfg.frag_size = frag_size;
	rq->cfg.mtu = mtu;
	rq->cfg.eqd = 0;
	rq->lro_pkts_queued = 0;
	rq->cfg.is_rss_queue = rss;
        rq->pending = 0;

	rq->parent = (void *)sc;

	rc = bus_dma_tag_create(bus_get_dma_tag(sc->dev),
			1, 0,
			BUS_SPACE_MAXADDR,
			BUS_SPACE_MAXADDR,
			NULL, NULL,
			oce_rq_buf_size,
			1, oce_rq_buf_size, 0, NULL, NULL, &rq->tag);
	if (rc)
		goto free_rq;

	for (i = 0; i < OCE_RQ_PACKET_ARRAY_SIZE; i++) {
		rc = bus_dmamap_create(rq->tag, 0, &rq->pckts[i].map);
		if (rc)
			goto free_rq;
	}

	/* create the ring buffer */
	rq->ring = oce_create_ring_buffer(sc, q_len,
				 sizeof(struct oce_nic_rqe));
	if (!rq->ring)
		goto free_rq;

	LOCK_CREATE(&rq->rx_lock, "RX_lock");

	return rq;

free_rq:
	device_printf(sc->dev, "Create RQ failed\n");
	oce_rq_free(rq);
	return NULL;
}




/**
 * @brief 		Free a receive queue
 * @param rq		pointer to receive queue
 */
static void
oce_rq_free(struct oce_rq *rq)
{
	POCE_SOFTC sc = (POCE_SOFTC) rq->parent;
	int i = 0 ;

	if (rq->ring != NULL) {
		oce_destroy_ring_buffer(sc, rq->ring);
		rq->ring = NULL;
	}
	for (i = 0; i < OCE_RQ_PACKET_ARRAY_SIZE; i++) {
		if (rq->pckts[i].map != NULL) {
			bus_dmamap_unload(rq->tag, rq->pckts[i].map);
			bus_dmamap_destroy(rq->tag, rq->pckts[i].map);
			rq->pckts[i].map = NULL;
		}
		if (rq->pckts[i].mbuf) {
			m_free(rq->pckts[i].mbuf);
			rq->pckts[i].mbuf = NULL;
		}
	}

	if (rq->tag != NULL)
		bus_dma_tag_destroy(rq->tag);

	LOCK_DESTROY(&rq->rx_lock);
	free(rq, M_DEVBUF);
}




/**
 * @brief 		Create a receive queue
 * @param rq 		receive queue
 * @param if_id		interface identifier index`
 * @param eq		pointer to event queue
 */
static int
oce_rq_create(struct oce_rq *rq, uint32_t if_id, struct oce_eq *eq)
{
	POCE_SOFTC sc = rq->parent;
	struct oce_cq *cq;

	cq = oce_cq_create(sc, eq,
		       	sc->enable_hwlro ? CQ_LEN_2048 : CQ_LEN_1024,
			sizeof(struct oce_nic_rx_cqe), 0, 1, 0, 3);		
			
	if (!cq)
		return ENXIO;

	rq->cq = cq;
	rq->cfg.if_id = if_id;

	/* Dont create RQ here. Create in if_activate */
	rq->qstate     = 0;
	rq->ring->cidx = 0;
	rq->ring->pidx = 0;
	eq->cq[eq->cq_valid] = cq;
	eq->cq_valid++;
	cq->cb_arg = rq;
	cq->cq_handler = oce_rq_handler;

	return 0;

}




/**
 * @brief 		Delete a receive queue
 * @param rq		receive queue
 */
static void
oce_rq_del(struct oce_rq *rq)
{
	POCE_SOFTC sc = (POCE_SOFTC) rq->parent;
	struct oce_mbx mbx;
	struct mbx_delete_nic_rq *fwcmd;
	struct mbx_delete_nic_rq_v1 *fwcmd1;

	if (rq->qstate == QCREATED) {
		bzero(&mbx, sizeof(mbx));
		if(!rq->islro) {
			fwcmd = (struct mbx_delete_nic_rq *)&mbx.payload;
			fwcmd->params.req.rq_id = rq->rq_id;
			(void)oce_destroy_q(sc, &mbx, sizeof(struct mbx_delete_nic_rq), QTYPE_RQ, 0);
		}else {
			fwcmd1 = (struct mbx_delete_nic_rq_v1 *)&mbx.payload;
			fwcmd1->params.req.rq_id = rq->rq_id;
			fwcmd1->params.req.rq_flags = (NIC_RQ_FLAGS_RSS | NIC_RQ_FLAGS_LRO);
			(void)oce_destroy_q(sc, &mbx, sizeof(struct mbx_delete_nic_rq_v1), QTYPE_RQ, 1);
		}
		rq->qstate = QDELETED;
	}

	if (rq->cq != NULL) {
		oce_cq_del(sc, rq->cq);
		rq->cq = NULL;
	}
}



/**
 * @brief		function to create an event queue
 * @param sc		software handle to the device
 * @param q_len		length of event queue
 * @param item_size	size of an event queue item
 * @param eq_delay	event queue delay
 * @retval eq      	success, pointer to event queue
 * @retval NULL		failure
 */
static struct
oce_eq *oce_eq_create(POCE_SOFTC sc, uint32_t q_len,
				    uint32_t item_size,
				    uint32_t eq_delay,
				    uint32_t vector)
{
	struct oce_eq *eq;
	int rc = 0;

	/* allocate an eq */
	eq = malloc(sizeof(struct oce_eq), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (eq == NULL)
		return NULL;

	eq->parent = (void *)sc;
	eq->eq_id = 0xffff;
	eq->ring = oce_create_ring_buffer(sc, q_len, item_size);
	if (!eq->ring)
		goto free_eq;
	
	eq->eq_cfg.q_len = q_len;
	eq->eq_cfg.item_size = item_size;
	eq->eq_cfg.cur_eqd = (uint8_t) eq_delay;

	rc = oce_mbox_create_eq(eq);
	if (rc)
		goto free_eq;

	sc->intrs[sc->neqs++].eq = eq;

	return eq;

free_eq:
	oce_eq_del(eq);
	return NULL;
}




/**
 * @brief 		Function to delete an event queue
 * @param eq		pointer to an event queue
 */
static void
oce_eq_del(struct oce_eq *eq)
{
	struct oce_mbx mbx;
	struct mbx_destroy_common_eq *fwcmd;
	POCE_SOFTC sc = (POCE_SOFTC) eq->parent;

	if (eq->eq_id != 0xffff) {
		bzero(&mbx, sizeof(mbx));
		fwcmd = (struct mbx_destroy_common_eq *)&mbx.payload;
		fwcmd->params.req.id = eq->eq_id;
		(void)oce_destroy_q(sc, &mbx,
			sizeof(struct mbx_destroy_common_eq), QTYPE_EQ, 0);
	}

	if (eq->ring != NULL) {
		oce_destroy_ring_buffer(sc, eq->ring);
		eq->ring = NULL;
	}

	free(eq, M_DEVBUF);

}




/**
 * @brief		Function to create an MQ
 * @param sc		software handle to the device
 * @param eq		the EQ to associate with the MQ for event notification
 * @param q_len		the number of entries to create in the MQ
 * @returns		pointer to the created MQ, failure otherwise
 */
static struct oce_mq *
oce_mq_create(POCE_SOFTC sc, struct oce_eq *eq, uint32_t q_len)
{
	struct oce_mbx mbx;
	struct mbx_create_common_mq_ex *fwcmd = NULL;
	struct oce_mq *mq = NULL;
	int rc = 0;
	struct oce_cq *cq;
	oce_mq_ext_ctx_t *ctx;
	uint32_t num_pages;
	uint32_t page_size;
	int version;

	cq = oce_cq_create(sc, eq, CQ_LEN_256,
			sizeof(struct oce_mq_cqe), 1, 1, 0, 0);
	if (!cq)
		return NULL;

	/* allocate the mq */
	mq = malloc(sizeof(struct oce_mq), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (!mq) {
		oce_cq_del(sc, cq);
		goto error;
	}

	mq->parent = sc;

	mq->ring = oce_create_ring_buffer(sc, q_len, sizeof(struct oce_mbx));
	if (!mq->ring)
		goto error;

	bzero(&mbx, sizeof(struct oce_mbx));

	IS_XE201(sc) ? (version = OCE_MBX_VER_V1) : (version = OCE_MBX_VER_V0);
	fwcmd = (struct mbx_create_common_mq_ex *)&mbx.payload;
	mbx_common_req_hdr_init(&fwcmd->hdr, 0, 0,
				MBX_SUBSYSTEM_COMMON,
				OPCODE_COMMON_CREATE_MQ_EXT,
				MBX_TIMEOUT_SEC,
				sizeof(struct mbx_create_common_mq_ex),
				version);

	num_pages = oce_page_list(mq->ring, &fwcmd->params.req.pages[0]);
	page_size = mq->ring->num_items * mq->ring->item_size;

	ctx = &fwcmd->params.req.context;

	if (IS_XE201(sc)) {
		ctx->v1.num_pages = num_pages;
		ctx->v1.ring_size = OCE_LOG2(q_len) + 1;
		ctx->v1.cq_id = cq->cq_id;
		ctx->v1.valid = 1;
		ctx->v1.async_cq_id = cq->cq_id;
		ctx->v1.async_cq_valid = 1;
		/* Subscribe to Link State and Group 5 Events(bits 1 & 5 set) */
		ctx->v1.async_evt_bitmap |= LE_32(0x00000022);
		ctx->v1.async_evt_bitmap |= LE_32(1 << ASYNC_EVENT_CODE_DEBUG);
		ctx->v1.async_evt_bitmap |=
					LE_32(1 << ASYNC_EVENT_CODE_SLIPORT);
	}
	else {
		ctx->v0.num_pages = num_pages;
		ctx->v0.cq_id = cq->cq_id;
		ctx->v0.ring_size = OCE_LOG2(q_len) + 1;
		ctx->v0.valid = 1;
		/* Subscribe to Link State and Group5 Events(bits 1 & 5 set) */
		ctx->v0.async_evt_bitmap = 0xffffffff;
	}

	mbx.u0.s.embedded = 1;
	mbx.payload_length = sizeof(struct mbx_create_common_mq_ex);
	DW_SWAP(u32ptr(&mbx), mbx.payload_length + OCE_BMBX_RHDR_SZ);

	rc = oce_mbox_post(sc, &mbx, NULL);
	if (!rc)
                rc = fwcmd->hdr.u0.rsp.status;
	if (rc) {
		device_printf(sc->dev,"%s failed - cmd status: %d\n",
			      __FUNCTION__, rc);
		goto error;
	}
	mq->mq_id = LE_16(fwcmd->params.rsp.mq_id);
	mq->cq = cq;
	eq->cq[eq->cq_valid] = cq;
	eq->cq_valid++;
	mq->cq->eq = eq;
	mq->cfg.q_len = (uint8_t) q_len;
	mq->cfg.eqd = 0;
	mq->qstate = QCREATED;

	mq->cq->cb_arg = mq;
	mq->cq->cq_handler = oce_mq_handler;

	return mq;

error:
	device_printf(sc->dev, "MQ create failed\n");
	oce_mq_free(mq);
	mq = NULL;
	return mq;
}





/**
 * @brief		Function to free a mailbox queue
 * @param mq		pointer to a mailbox queue
 */
static void
oce_mq_free(struct oce_mq *mq)
{
	POCE_SOFTC sc = (POCE_SOFTC) mq->parent;
	struct oce_mbx mbx;
	struct mbx_destroy_common_mq *fwcmd;

	if (!mq)
		return;

	if (mq->ring != NULL) {
		oce_destroy_ring_buffer(sc, mq->ring);
		mq->ring = NULL;
		if (mq->qstate == QCREATED) {
			bzero(&mbx, sizeof (struct oce_mbx));
			fwcmd = (struct mbx_destroy_common_mq *)&mbx.payload;
			fwcmd->params.req.id = mq->mq_id;
			(void) oce_destroy_q(sc, &mbx,
				sizeof (struct mbx_destroy_common_mq),
				QTYPE_MQ, 0);
		}
		mq->qstate = QDELETED;
	}

	if (mq->cq != NULL) {
		oce_cq_del(sc, mq->cq);
		mq->cq = NULL;
	}

	free(mq, M_DEVBUF);
	mq = NULL;
}



/**
 * @brief		Function to delete a EQ, CQ, MQ, WQ or RQ
 * @param sc		sofware handle to the device
 * @param mbx		mailbox command to send to the fw to delete the queue
 *			(mbx contains the queue information to delete)
 * @param req_size	the size of the mbx payload dependent on the qtype
 * @param qtype		the type of queue i.e. EQ, CQ, MQ, WQ or RQ
 * @returns 		0 on success, failure otherwise
 */
static int
oce_destroy_q(POCE_SOFTC sc, struct oce_mbx *mbx, size_t req_size,
		enum qtype qtype, int version)
{
	struct mbx_hdr *hdr = (struct mbx_hdr *)&mbx->payload;
	int opcode;
	int subsys;
	int rc = 0;

	switch (qtype) {
	case QTYPE_EQ:
		opcode = OPCODE_COMMON_DESTROY_EQ;
		subsys = MBX_SUBSYSTEM_COMMON;
		break;
	case QTYPE_CQ:
		opcode = OPCODE_COMMON_DESTROY_CQ;
		subsys = MBX_SUBSYSTEM_COMMON;
		break;
	case QTYPE_MQ:
		opcode = OPCODE_COMMON_DESTROY_MQ;
		subsys = MBX_SUBSYSTEM_COMMON;
		break;
	case QTYPE_WQ:
		opcode = NIC_DELETE_WQ;
		subsys = MBX_SUBSYSTEM_NIC;
		break;
	case QTYPE_RQ:
		opcode = NIC_DELETE_RQ;
		subsys = MBX_SUBSYSTEM_NIC;
		break;
	default:
		return EINVAL;
	}

	mbx_common_req_hdr_init(hdr, 0, 0, subsys,
				opcode, MBX_TIMEOUT_SEC, req_size,
				version);

	mbx->u0.s.embedded = 1;
	mbx->payload_length = (uint32_t) req_size;
	DW_SWAP(u32ptr(mbx), mbx->payload_length + OCE_BMBX_RHDR_SZ);

	rc = oce_mbox_post(sc, mbx, NULL);
	if (!rc)
                rc = hdr->u0.rsp.status;
	if (rc)
		device_printf(sc->dev,"%s failed - cmd status: %d\n",
			      __FUNCTION__, rc);
	return rc;
}



/**
 * @brief		Function to create a completion queue
 * @param sc		software handle to the device
 * @param eq		optional eq to be associated with to the cq
 * @param q_len		length of completion queue
 * @param item_size	size of completion queue items
 * @param sol_event	command context event
 * @param is_eventable	event table
 * @param nodelay	no delay flag
 * @param ncoalesce	no coalescence flag
 * @returns 		pointer to the cq created, NULL on failure
 */
struct oce_cq *
oce_cq_create(POCE_SOFTC sc, struct oce_eq *eq,
			     uint32_t q_len,
			     uint32_t item_size,
			     uint32_t sol_event,
			     uint32_t is_eventable,
			     uint32_t nodelay, uint32_t ncoalesce)
{
	struct oce_cq *cq = NULL;
	int rc = 0;

	cq = malloc(sizeof(struct oce_cq), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (!cq)
		return NULL;

	cq->ring = oce_create_ring_buffer(sc, q_len, item_size);
	if (!cq->ring)
		goto error;
	
	cq->parent = sc;
	cq->eq = eq;
	cq->cq_cfg.q_len = q_len;
	cq->cq_cfg.item_size = item_size;
	cq->cq_cfg.nodelay = (uint8_t) nodelay;

	rc = oce_mbox_cq_create(cq, ncoalesce, is_eventable);
	if (rc)
		goto error;

	sc->cq[sc->ncqs++] = cq;

	return cq;

error:
	device_printf(sc->dev, "CQ create failed\n");
	oce_cq_del(sc, cq);
	return NULL;
}



/**
 * @brief		Deletes the completion queue
 * @param sc		software handle to the device
 * @param cq		pointer to a completion queue
 */
static void 
oce_cq_del(POCE_SOFTC sc, struct oce_cq *cq)
{
	struct oce_mbx mbx;
	struct mbx_destroy_common_cq *fwcmd;

	if (cq->ring != NULL) {

		bzero(&mbx, sizeof(struct oce_mbx));
		/* now fill the command */
		fwcmd = (struct mbx_destroy_common_cq *)&mbx.payload;
		fwcmd->params.req.id = cq->cq_id;
		(void)oce_destroy_q(sc, &mbx,
			sizeof(struct mbx_destroy_common_cq), QTYPE_CQ, 0);
		/*NOW destroy the ring */
		oce_destroy_ring_buffer(sc, cq->ring);
		cq->ring = NULL;
	}

	free(cq, M_DEVBUF);
	cq = NULL;
}



/**
 * @brief		Start a receive queue
 * @param rq		pointer to a receive queue
 */
int
oce_start_rq(struct oce_rq *rq)
{
	POCE_SOFTC sc = (POCE_SOFTC) rq->parent;
	int rc;

	if(sc->enable_hwlro)
		rc = oce_alloc_rx_bufs(rq, 960);
	else
		rc = oce_alloc_rx_bufs(rq, rq->cfg.q_len - 1);

	if (rc == 0)
		oce_arm_cq(rq->parent, rq->cq->cq_id, 0, TRUE);

	return rc;
}



/**
 * @brief		Start a work queue
 * @param wq		pointer to a work queue
 */
int
oce_start_wq(struct oce_wq *wq)
{
	oce_arm_cq(wq->parent, wq->cq->cq_id, 0, TRUE);
	return 0;
}



/**
 * @brief		Start a mailbox queue
 * @param mq		pointer to a mailbox queue
 */
int
oce_start_mq(struct oce_mq *mq)
{
	oce_arm_cq(mq->parent, mq->cq->cq_id, 0, TRUE);
	return 0;
}



/**
 * @brief		Function to arm an EQ so that it can generate events
 * @param sc		software handle to the device
 * @param qid		id of the EQ returned by the fw at the time of creation
 * @param npopped	number of EQEs to arm
 * @param rearm		rearm bit enable/disable
 * @param clearint	bit to clear the interrupt condition because of which
 *			EQEs are generated
 */
void
oce_arm_eq(POCE_SOFTC sc,
	   int16_t qid, int npopped, uint32_t rearm, uint32_t clearint)
{
	eq_db_t eq_db = { 0 };

	eq_db.bits.rearm = rearm;
	eq_db.bits.event = 1;
	eq_db.bits.num_popped = npopped;
	eq_db.bits.clrint = clearint;
	eq_db.bits.qid = qid;
	OCE_WRITE_REG32(sc, db, PD_EQ_DB, eq_db.dw0);

}




/**
 * @brief		Function to arm a CQ with CQEs
 * @param sc		software handle to the device
 * @param qid		id of the CQ returned by the fw at the time of creation
 * @param npopped	number of CQEs to arm
 * @param rearm		rearm bit enable/disable
 */
void oce_arm_cq(POCE_SOFTC sc, int16_t qid, int npopped, uint32_t rearm)
{
	cq_db_t cq_db = { 0 };

	cq_db.bits.rearm = rearm;
	cq_db.bits.num_popped = npopped;
	cq_db.bits.event = 0;
	cq_db.bits.qid = qid;
	OCE_WRITE_REG32(sc, db, PD_CQ_DB, cq_db.dw0);

}




/*
 * @brief		function to cleanup the eqs used during stop
 * @param eq		pointer to event queue structure
 * @returns		the number of EQs processed
 */
void
oce_drain_eq(struct oce_eq *eq)
{

	struct oce_eqe *eqe;
	uint16_t num_eqe = 0;
	POCE_SOFTC sc = eq->parent;

	do {
		eqe = RING_GET_CONSUMER_ITEM_VA(eq->ring, struct oce_eqe);
		if (eqe->evnt == 0)
			break;
		eqe->evnt = 0;
		bus_dmamap_sync(eq->ring->dma.tag, eq->ring->dma.map,
					BUS_DMASYNC_POSTWRITE);
		num_eqe++;
		RING_GET(eq->ring, 1);

	} while (TRUE);

	oce_arm_eq(sc, eq->eq_id, num_eqe, FALSE, TRUE);
	
}



void
oce_drain_wq_cq(struct oce_wq *wq)
{
        POCE_SOFTC sc = wq->parent;
        struct oce_cq *cq = wq->cq;
        struct oce_nic_tx_cqe *cqe;
        int num_cqes = 0;

	bus_dmamap_sync(cq->ring->dma.tag, cq->ring->dma.map,
				 BUS_DMASYNC_POSTWRITE);

	do {
		cqe = RING_GET_CONSUMER_ITEM_VA(cq->ring, struct oce_nic_tx_cqe);
		if (cqe->u0.dw[3] == 0)
			break;			
		cqe->u0.dw[3] = 0;
		bus_dmamap_sync(cq->ring->dma.tag, cq->ring->dma.map,
				 BUS_DMASYNC_POSTWRITE);
		RING_GET(cq->ring, 1);
		num_cqes++;

	} while (TRUE);

	oce_arm_cq(sc, cq->cq_id, num_cqes, FALSE);

}


/*
 * @brief		function to drain a MCQ and process its CQEs
 * @param dev		software handle to the device
 * @param cq		pointer to the cq to drain
 * @returns		the number of CQEs processed
 */
void
oce_drain_mq_cq(void *arg)
{
	/* TODO: additional code. */
	return;
}



/**
 * @brief		function to process a Recieve queue
 * @param arg		pointer to the RQ to charge
 * @return		number of cqes processed
 */
void
oce_drain_rq_cq(struct oce_rq *rq)
{
	struct oce_nic_rx_cqe *cqe;
	uint16_t num_cqe = 0;
	struct oce_cq  *cq;
	POCE_SOFTC sc;

	sc = rq->parent;
	cq = rq->cq;
	cqe = RING_GET_CONSUMER_ITEM_VA(cq->ring, struct oce_nic_rx_cqe);
	/* dequeue till you reach an invalid cqe */
	while (RQ_CQE_VALID(cqe)) {
		RQ_CQE_INVALIDATE(cqe);
		RING_GET(cq->ring, 1);
		cqe = RING_GET_CONSUMER_ITEM_VA(cq->ring,
		    struct oce_nic_rx_cqe);
		num_cqe++;
	}
	oce_arm_cq(sc, cq->cq_id, num_cqe, FALSE);

	return;
}


void
oce_free_posted_rxbuf(struct oce_rq *rq)
{
	struct oce_packet_desc *pd;
	
	while (rq->pending) {

		pd = &rq->pckts[rq->ring->cidx];
		bus_dmamap_sync(rq->tag, pd->map, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(rq->tag, pd->map);
		if (pd->mbuf != NULL) {
			m_freem(pd->mbuf);
			pd->mbuf = NULL;
		}

		RING_GET(rq->ring,1);
                rq->pending--;
	}

}

void
oce_rx_cq_clean_hwlro(struct oce_rq *rq)
{
        struct oce_cq *cq = rq->cq;
        POCE_SOFTC sc = rq->parent;
        struct nic_hwlro_singleton_cqe *cqe;
        struct nic_hwlro_cqe_part2 *cqe2;
        int flush_wait = 0;
        int flush_compl = 0;
	int num_frags = 0;

        for (;;) {
                bus_dmamap_sync(cq->ring->dma.tag,cq->ring->dma.map, BUS_DMASYNC_POSTWRITE);
                cqe = RING_GET_CONSUMER_ITEM_VA(cq->ring, struct nic_hwlro_singleton_cqe);
                if(cqe->valid) {
                        if(cqe->cqe_type == 0) { /* singleton cqe */
                                /* we should not get singleton cqe after cqe1 on same rq */
                                if(rq->cqe_firstpart != NULL) {
                                        device_printf(sc->dev, "Got singleton cqe after cqe1 \n");
                                        goto exit_rx_cq_clean_hwlro;
                                }
				num_frags = cqe->pkt_size / rq->cfg.frag_size;
				if(cqe->pkt_size % rq->cfg.frag_size)
					num_frags++;
                                oce_discard_rx_comp(rq, num_frags);
                                /* Check if CQE is flush completion */
                                if(!cqe->pkt_size) 
                                        flush_compl = 1;
                                cqe->valid = 0;
                                RING_GET(cq->ring, 1);
                        }else if(cqe->cqe_type == 0x1) { /* first part */
                                /* we should not get cqe1 after cqe1 on same rq */
                                if(rq->cqe_firstpart != NULL) {
                                        device_printf(sc->dev, "Got cqe1 after cqe1 \n");
                                        goto exit_rx_cq_clean_hwlro;
                                }
                                rq->cqe_firstpart = (struct nic_hwlro_cqe_part1 *)cqe;
                                RING_GET(cq->ring, 1);
                        }else if(cqe->cqe_type == 0x2) { /* second part */
                                cqe2 = (struct nic_hwlro_cqe_part2 *)cqe;
                                /* We should not get cqe2 without cqe1 */
                                if(rq->cqe_firstpart == NULL) {
                                        device_printf(sc->dev, "Got cqe2 without cqe1 \n");
                                        goto exit_rx_cq_clean_hwlro;
                                }
				num_frags = cqe2->coalesced_size / rq->cfg.frag_size;
				if(cqe2->coalesced_size % rq->cfg.frag_size)
					num_frags++;
				
				/* Flush completion will always come in singleton CQE */
                                oce_discard_rx_comp(rq, num_frags);

                                rq->cqe_firstpart->valid = 0;
                                cqe2->valid = 0;
                                rq->cqe_firstpart = NULL;
                                RING_GET(cq->ring, 1);
                        }
                        oce_arm_cq(sc, cq->cq_id, 1, FALSE);
                        if(flush_compl)
                                break;
                }else {
                        if (flush_wait++ > 100) {
                                device_printf(sc->dev, "did not receive hwlro flush compl\n");
                                break;
                        }
                        oce_arm_cq(sc, cq->cq_id, 0, TRUE);
                        DELAY(1000);
                }
        }

        /* After cleanup, leave the CQ in unarmed state */
        oce_arm_cq(sc, cq->cq_id, 0, FALSE);

exit_rx_cq_clean_hwlro:
	return;
}


void
oce_rx_cq_clean(struct oce_rq *rq)
{
	struct oce_nic_rx_cqe *cqe;
        struct oce_cq  *cq;
        POCE_SOFTC sc;
	int flush_wait = 0;
	int flush_compl = 0;
        sc = rq->parent;
        cq = rq->cq;
	
	for (;;) {
		bus_dmamap_sync(cq->ring->dma.tag,
			cq->ring->dma.map, BUS_DMASYNC_POSTWRITE);
        	cqe = RING_GET_CONSUMER_ITEM_VA(cq->ring, struct oce_nic_rx_cqe);
		if(RQ_CQE_VALID(cqe)) {
			DW_SWAP((uint32_t *) cqe, sizeof(oce_rq_cqe));
                        oce_discard_rx_comp(rq, cqe->u0.s.num_fragments);
                        /* Check if CQE is flush completion */
                        if((cqe->u0.s.num_fragments==0)&&(cqe->u0.s.pkt_size == 0)&&(cqe->u0.s.error == 0)) 
				flush_compl = 1;
                        
                        RQ_CQE_INVALIDATE(cqe);
                        RING_GET(cq->ring, 1);
#if defined(INET6) || defined(INET)
		        if (IF_LRO_ENABLED(sc))
                		oce_rx_flush_lro(rq);
#endif
                        oce_arm_cq(sc, cq->cq_id, 1, FALSE);
			if(flush_compl)
				break;
		}else {
			if (flush_wait++ > 100) {
				device_printf(sc->dev, "did not receive flush compl\n");
				break;
			}
			oce_arm_cq(sc, cq->cq_id, 0, TRUE);
			DELAY(1000);
                } 
        }

	/* After cleanup, leave the CQ in unarmed state */
	oce_arm_cq(sc, cq->cq_id, 0, FALSE);
}

void
oce_stop_rx(POCE_SOFTC sc)
{
        struct oce_mbx mbx;
        struct mbx_delete_nic_rq *fwcmd;
        struct mbx_delete_nic_rq_v1 *fwcmd1;
        struct oce_rq *rq;
        int i = 0;
 
       /* before deleting disable hwlro */
	if(sc->enable_hwlro)
        	oce_mbox_nic_set_iface_lro_config(sc, 0);

        for_all_rq_queues(sc, rq, i) {
                if (rq->qstate == QCREATED) {
                        /* Delete rxq in firmware */
			LOCK(&rq->rx_lock);

                        bzero(&mbx, sizeof(mbx));
                	if(!rq->islro) {
                        	fwcmd = (struct mbx_delete_nic_rq *)&mbx.payload;
                        	fwcmd->params.req.rq_id = rq->rq_id;
                        	(void)oce_destroy_q(sc, &mbx, sizeof(struct mbx_delete_nic_rq), QTYPE_RQ, 0);
                	}else {
                        	fwcmd1 = (struct mbx_delete_nic_rq_v1 *)&mbx.payload;
                        	fwcmd1->params.req.rq_id = rq->rq_id;
                               	fwcmd1->params.req.rq_flags = (NIC_RQ_FLAGS_RSS | NIC_RQ_FLAGS_LRO);

                        	(void)oce_destroy_q(sc,&mbx,sizeof(struct mbx_delete_nic_rq_v1),QTYPE_RQ,1);
                	}
                        rq->qstate = QDELETED;

                        DELAY(1000);
			
			if(!rq->islro)
				oce_rx_cq_clean(rq);
			else
				oce_rx_cq_clean_hwlro(rq);

                        /* Free posted RX buffers that are not used */
                        oce_free_posted_rxbuf(rq);
			UNLOCK(&rq->rx_lock);
                }
        }
}



int
oce_start_rx(POCE_SOFTC sc)
{
	struct oce_rq *rq;
	int rc = 0, i;
	
	for_all_rq_queues(sc, rq, i) {
		if (rq->qstate == QCREATED)
			continue;
		if((i == 0) || (!sc->enable_hwlro)) {
        	        rc = oce_mbox_create_rq(rq);
                        if (rc)
                                goto error;
			rq->islro = 0;
		}else {
			rc = oce_mbox_create_rq_v2(rq);
                        if (rc)
                                goto error;
                        rq->islro = 1;
		}
                /* reset queue pointers */
                rq->qstate       = QCREATED;
                rq->pending      = 0;
                rq->ring->cidx   = 0;
                rq->ring->pidx   = 0;
	}
	
	if(sc->enable_hwlro) {
		rc = oce_mbox_nic_set_iface_lro_config(sc, 1);
		if (rc)
			goto error;
	}

	DELAY(1);
	
	/* RSS config */
	if (is_rss_enabled(sc)) {
		rc = oce_config_nic_rss(sc, (uint8_t) sc->if_id, RSS_ENABLE);
		if (rc)
			goto error;

	}

	DELAY(1);
	return rc;
error:
	device_printf(sc->dev, "Start RX failed\n");
	return rc;

}



