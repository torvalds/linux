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

SYSCTL_NODE(_kern, OID_AUTO, iser, CTLFLAG_RW, 0, "iSER module");
int iser_debug = 0;
SYSCTL_INT(_kern_iser, OID_AUTO, debug, CTLFLAG_RWTUN,
    &iser_debug, 0, "Enable iser debug messages");

static MALLOC_DEFINE(M_ICL_ISER, "icl_iser", "iSCSI iser backend");
static uma_zone_t icl_pdu_zone;

static volatile u_int	icl_iser_ncons;
struct iser_global ig;

static void iser_conn_release(struct icl_conn *ic);

static icl_conn_new_pdu_t	iser_conn_new_pdu;
static icl_conn_pdu_free_t	iser_conn_pdu_free;
static icl_conn_pdu_data_segment_length_t iser_conn_pdu_data_segment_length;
static icl_conn_pdu_append_data_t	iser_conn_pdu_append_data;
static icl_conn_pdu_queue_t	iser_conn_pdu_queue;
static icl_conn_handoff_t	iser_conn_handoff;
static icl_conn_free_t		iser_conn_free;
static icl_conn_close_t		iser_conn_close;
static icl_conn_connect_t	iser_conn_connect;
static icl_conn_task_setup_t	iser_conn_task_setup;
static icl_conn_task_done_t	iser_conn_task_done;
static icl_conn_pdu_get_data_t	iser_conn_pdu_get_data;

static kobj_method_t icl_iser_methods[] = {
	KOBJMETHOD(icl_conn_new_pdu, iser_conn_new_pdu),
	KOBJMETHOD(icl_conn_pdu_free, iser_conn_pdu_free),
	KOBJMETHOD(icl_conn_pdu_data_segment_length, iser_conn_pdu_data_segment_length),
	KOBJMETHOD(icl_conn_pdu_append_data, iser_conn_pdu_append_data),
	KOBJMETHOD(icl_conn_pdu_queue, iser_conn_pdu_queue),
	KOBJMETHOD(icl_conn_handoff, iser_conn_handoff),
	KOBJMETHOD(icl_conn_free, iser_conn_free),
	KOBJMETHOD(icl_conn_close, iser_conn_close),
	KOBJMETHOD(icl_conn_connect, iser_conn_connect),
	KOBJMETHOD(icl_conn_task_setup, iser_conn_task_setup),
	KOBJMETHOD(icl_conn_task_done, iser_conn_task_done),
	KOBJMETHOD(icl_conn_pdu_get_data, iser_conn_pdu_get_data),
	{ 0, 0 }
};

DEFINE_CLASS(icl_iser, icl_iser_methods, sizeof(struct iser_conn));

/**
 * iser_initialize_headers() - Initialize task headers
 * @pdu:       iser pdu
 * @iser_conn:    iser connection
 *
 * Notes:
 * This routine may race with iser teardown flow for scsi
 * error handling TMFs. So for TMF we should acquire the
 * state mutex to avoid dereferencing the IB device which
 * may have already been terminated (racing teardown sequence).
 */
int
iser_initialize_headers(struct icl_iser_pdu *pdu, struct iser_conn *iser_conn)
{
	struct iser_tx_desc *tx_desc = &pdu->desc;
	struct iser_device *device = iser_conn->ib_conn.device;
	u64 dma_addr;
	int ret = 0;

	dma_addr = ib_dma_map_single(device->ib_device, (void *)tx_desc,
				ISER_HEADERS_LEN, DMA_TO_DEVICE);
	if (ib_dma_mapping_error(device->ib_device, dma_addr)) {
		ret = -ENOMEM;
		goto out;
	}

	tx_desc->mapped = true;
	tx_desc->dma_addr = dma_addr;
	tx_desc->tx_sg[0].addr   = tx_desc->dma_addr;
	tx_desc->tx_sg[0].length = ISER_HEADERS_LEN;
	tx_desc->tx_sg[0].lkey   = device->mr->lkey;

out:

	return (ret);
}

int
iser_conn_pdu_append_data(struct icl_conn *ic, struct icl_pdu *request,
			  const void *addr, size_t len, int flags)
{
	struct iser_conn *iser_conn = icl_to_iser_conn(ic);

	if (request->ip_bhs->bhs_opcode & ISCSI_BHS_OPCODE_LOGIN_REQUEST ||
	    request->ip_bhs->bhs_opcode & ISCSI_BHS_OPCODE_TEXT_REQUEST) {
		ISER_DBG("copy to login buff");
		memcpy(iser_conn->login_req_buf, addr, len);
		request->ip_data_len = len;
	}

	return (0);
}

void
iser_conn_pdu_get_data(struct icl_conn *ic, struct icl_pdu *ip,
		       size_t off, void *addr, size_t len)
{
	/* If we have a receive data, copy it to upper layer buffer */
	if (ip->ip_data_mbuf)
		memcpy(addr, ip->ip_data_mbuf + off, len);
}

/*
 * Allocate icl_pdu with empty BHS to fill up by the caller.
 */
struct icl_pdu *
iser_new_pdu(struct icl_conn *ic, int flags)
{
	struct icl_iser_pdu *iser_pdu;
	struct icl_pdu *ip;
	struct iser_conn *iser_conn = icl_to_iser_conn(ic);

	iser_pdu = uma_zalloc(icl_pdu_zone, flags | M_ZERO);
	if (iser_pdu == NULL) {
		ISER_WARN("failed to allocate %zd bytes", sizeof(*iser_pdu));
		return (NULL);
	}

	iser_pdu->iser_conn = iser_conn;
	ip = &iser_pdu->icl_pdu;
	ip->ip_conn = ic;
	ip->ip_bhs = &iser_pdu->desc.iscsi_header;

	return (ip);
}

struct icl_pdu *
iser_conn_new_pdu(struct icl_conn *ic, int flags)
{
	return (iser_new_pdu(ic, flags));
}

void
iser_pdu_free(struct icl_conn *ic, struct icl_pdu *ip)
{
	struct icl_iser_pdu *iser_pdu = icl_to_iser_pdu(ip);

	uma_zfree(icl_pdu_zone, iser_pdu);
}

size_t
iser_conn_pdu_data_segment_length(struct icl_conn *ic,
				  const struct icl_pdu *request)
{
	uint32_t len = 0;

	len += request->ip_bhs->bhs_data_segment_len[0];
	len <<= 8;
	len += request->ip_bhs->bhs_data_segment_len[1];
	len <<= 8;
	len += request->ip_bhs->bhs_data_segment_len[2];

	return (len);
}

void
iser_conn_pdu_free(struct icl_conn *ic, struct icl_pdu *ip)
{
	iser_pdu_free(ic, ip);
}

static bool
is_control_opcode(uint8_t opcode)
{
	bool is_control = false;

	switch (opcode & ISCSI_OPCODE_MASK) {
		case ISCSI_BHS_OPCODE_NOP_OUT:
		case ISCSI_BHS_OPCODE_LOGIN_REQUEST:
		case ISCSI_BHS_OPCODE_LOGOUT_REQUEST:
		case ISCSI_BHS_OPCODE_TEXT_REQUEST:
			is_control = true;
			break;
		case ISCSI_BHS_OPCODE_SCSI_COMMAND:
			is_control = false;
			break;
		default:
			ISER_ERR("unknown opcode %d", opcode);
	}

	return (is_control);
}

void
iser_conn_pdu_queue(struct icl_conn *ic, struct icl_pdu *ip)
{
	struct iser_conn *iser_conn = icl_to_iser_conn(ic);
	struct icl_iser_pdu *iser_pdu = icl_to_iser_pdu(ip);
	int ret;

	if (iser_conn->state != ISER_CONN_UP)
		return;

	ret = iser_initialize_headers(iser_pdu, iser_conn);
	if (ret) {
		ISER_ERR("Failed to map TX descriptor pdu %p", iser_pdu);
		return;
	}

	if (is_control_opcode(ip->ip_bhs->bhs_opcode)) {
		ret = iser_send_control(iser_conn, iser_pdu);
		if (unlikely(ret))
			ISER_ERR("Failed to send control pdu %p", iser_pdu);
	} else {
		ret = iser_send_command(iser_conn, iser_pdu);
		if (unlikely(ret))
			ISER_ERR("Failed to send command pdu %p", iser_pdu);
	}
}

static struct icl_conn *
iser_new_conn(const char *name, struct mtx *lock)
{
	struct iser_conn *iser_conn;
	struct icl_conn *ic;

	refcount_acquire(&icl_iser_ncons);

	iser_conn = (struct iser_conn *)kobj_create(&icl_iser_class, M_ICL_ISER, M_WAITOK | M_ZERO);
	if (!iser_conn) {
		ISER_ERR("failed to allocate iser conn");
		refcount_release(&icl_iser_ncons);
		return (NULL);
	}

	cv_init(&iser_conn->up_cv, "iser_cv");
	sx_init(&iser_conn->state_mutex, "iser_conn_state_mutex");
	mtx_init(&iser_conn->ib_conn.beacon.flush_lock, "iser_flush_lock", NULL, MTX_DEF);
	cv_init(&iser_conn->ib_conn.beacon.flush_cv, "flush_cv");
	mtx_init(&iser_conn->ib_conn.lock, "iser_lock", NULL, MTX_DEF);

	ic = &iser_conn->icl_conn;
	ic->ic_lock = lock;
	ic->ic_name = name;
	ic->ic_offload = strdup("iser", M_TEMP);
	ic->ic_iser = true;
	ic->ic_unmapped = true;

	return (ic);
}

void
iser_conn_free(struct icl_conn *ic)
{
	struct iser_conn *iser_conn = icl_to_iser_conn(ic);

	iser_conn_release(ic);
	mtx_destroy(&iser_conn->ib_conn.lock);
	cv_destroy(&iser_conn->ib_conn.beacon.flush_cv);
	mtx_destroy(&iser_conn->ib_conn.beacon.flush_lock);
	sx_destroy(&iser_conn->state_mutex);
	cv_destroy(&iser_conn->up_cv);
	kobj_delete((struct kobj *)iser_conn, M_ICL_ISER);
	refcount_release(&icl_iser_ncons);
}

int
iser_conn_handoff(struct icl_conn *ic, int fd)
{
	struct iser_conn *iser_conn = icl_to_iser_conn(ic);
	int error = 0;

	sx_xlock(&iser_conn->state_mutex);
	if (iser_conn->state != ISER_CONN_UP) {
		error = EINVAL;
		ISER_ERR("iser_conn %p state is %d, teardown started\n",
			 iser_conn, iser_conn->state);
		goto out;
	}

	error = iser_alloc_rx_descriptors(iser_conn, ic->ic_maxtags);
	if (error)
		goto out;

	error = iser_post_recvm(iser_conn, iser_conn->min_posted_rx);
	if (error)
		goto post_error;

	iser_conn->handoff_done = true;

	sx_xunlock(&iser_conn->state_mutex);
	return (error);

post_error:
	iser_free_rx_descriptors(iser_conn);
out:
	sx_xunlock(&iser_conn->state_mutex);
	return (error);

}

/**
 * Frees all conn objects
 */
static void
iser_conn_release(struct icl_conn *ic)
{
	struct iser_conn *iser_conn = icl_to_iser_conn(ic);
	struct ib_conn *ib_conn = &iser_conn->ib_conn;
	struct iser_conn *curr, *tmp;

	mtx_lock(&ig.connlist_mutex);
	/*
	 * Search for iser connection in global list.
	 * It may not be there in case of failure in connection establishment
	 * stage.
	 */
	list_for_each_entry_safe(curr, tmp, &ig.connlist, conn_list) {
		if (iser_conn == curr) {
			ISER_WARN("found iser_conn %p", iser_conn);
			list_del(&iser_conn->conn_list);
		}
	}
	mtx_unlock(&ig.connlist_mutex);

	/*
	 * In case we reconnecting or removing session, we need to
	 * release IB resources (which is safe to call more than once).
	 */
	sx_xlock(&iser_conn->state_mutex);
	iser_free_ib_conn_res(iser_conn, true);
	sx_xunlock(&iser_conn->state_mutex);

	if (ib_conn->cma_id != NULL) {
		rdma_destroy_id(ib_conn->cma_id);
		ib_conn->cma_id = NULL;
	}

}

void
iser_conn_close(struct icl_conn *ic)
{
	struct iser_conn *iser_conn = icl_to_iser_conn(ic);

	ISER_INFO("closing conn %p", iser_conn);

	sx_xlock(&iser_conn->state_mutex);
	/*
	 * In case iser connection is waiting on conditional variable
	 * (state PENDING) and we try to close it before connection establishment,
	 * we need to signal it to continue releasing connection properly.
	 */
	if (!iser_conn_terminate(iser_conn) && iser_conn->state == ISER_CONN_PENDING)
		cv_signal(&iser_conn->up_cv);
	sx_xunlock(&iser_conn->state_mutex);

}

int
iser_conn_connect(struct icl_conn *ic, int domain, int socktype,
		int protocol, struct sockaddr *from_sa, struct sockaddr *to_sa)
{
	struct iser_conn *iser_conn = icl_to_iser_conn(ic);
	struct ib_conn *ib_conn = &iser_conn->ib_conn;
	int err = 0;

	iser_conn_release(ic);

	sx_xlock(&iser_conn->state_mutex);
	 /* the device is known only --after-- address resolution */
	ib_conn->device = NULL;
	iser_conn->handoff_done = false;

	iser_conn->state = ISER_CONN_PENDING;

	ib_conn->cma_id = rdma_create_id(&init_net, iser_cma_handler, (void *)iser_conn,
			RDMA_PS_TCP, IB_QPT_RC);
	if (IS_ERR(ib_conn->cma_id)) {
		err = -PTR_ERR(ib_conn->cma_id);
		ISER_ERR("rdma_create_id failed: %d", err);
		goto id_failure;
	}

	err = rdma_resolve_addr(ib_conn->cma_id, from_sa, to_sa, 1000);
	if (err) {
		ISER_ERR("rdma_resolve_addr failed: %d", err);
		if (err < 0)
			err = -err;
		goto addr_failure;
	}

	ISER_DBG("before cv_wait: %p", iser_conn);
	cv_wait(&iser_conn->up_cv, &iser_conn->state_mutex);
	ISER_DBG("after cv_wait: %p", iser_conn);

	if (iser_conn->state != ISER_CONN_UP) {
		err = EIO;
		goto addr_failure;
	}

	err = iser_alloc_login_buf(iser_conn);
	if (err)
		goto addr_failure;
	sx_xunlock(&iser_conn->state_mutex);

	mtx_lock(&ig.connlist_mutex);
	list_add(&iser_conn->conn_list, &ig.connlist);
	mtx_unlock(&ig.connlist_mutex);

	return (0);

id_failure:
	ib_conn->cma_id = NULL;
addr_failure:
	sx_xunlock(&iser_conn->state_mutex);
	return (err);
}

int
iser_conn_task_setup(struct icl_conn *ic, struct icl_pdu *ip,
		     struct ccb_scsiio *csio,
		     uint32_t *task_tagp, void **prvp)
{
	struct icl_iser_pdu *iser_pdu = icl_to_iser_pdu(ip);

	*prvp = ip;
	iser_pdu->csio = csio;

	return (0);
}

void
iser_conn_task_done(struct icl_conn *ic, void *prv)
{
	struct icl_pdu *ip = prv;
	struct icl_iser_pdu *iser_pdu = icl_to_iser_pdu(ip);
	struct iser_device *device = iser_pdu->iser_conn->ib_conn.device;
	struct iser_tx_desc *tx_desc = &iser_pdu->desc;

	if (iser_pdu->dir[ISER_DIR_IN]) {
		iser_unreg_rdma_mem(iser_pdu, ISER_DIR_IN);
		iser_dma_unmap_task_data(iser_pdu,
					 &iser_pdu->data[ISER_DIR_IN],
					 DMA_FROM_DEVICE);
	}

	if (iser_pdu->dir[ISER_DIR_OUT]) {
		iser_unreg_rdma_mem(iser_pdu, ISER_DIR_OUT);
		iser_dma_unmap_task_data(iser_pdu,
					 &iser_pdu->data[ISER_DIR_OUT],
					 DMA_TO_DEVICE);
	}

	if (likely(tx_desc->mapped)) {
		ib_dma_unmap_single(device->ib_device, tx_desc->dma_addr,
				    ISER_HEADERS_LEN, DMA_TO_DEVICE);
		tx_desc->mapped = false;
	}

	iser_pdu_free(ic, ip);
}

static int
iser_limits(struct icl_drv_limits *idl)
{

	idl->idl_max_recv_data_segment_length = 128 * 1024;
	idl->idl_max_send_data_segment_length = 128 * 1024;
	idl->idl_max_burst_length = 262144;
	idl->idl_first_burst_length = 65536;

	return (0);
}

static int
icl_iser_load(void)
{
	int error;

	ISER_DBG("Starting iSER datamover...");

	icl_pdu_zone = uma_zcreate("icl_iser_pdu", sizeof(struct icl_iser_pdu),
				   NULL, NULL, NULL, NULL,
				   UMA_ALIGN_PTR, 0);
	/* FIXME: Check rc */

	refcount_init(&icl_iser_ncons, 0);

	error = icl_register("iser", true, 0, iser_limits, iser_new_conn);
	KASSERT(error == 0, ("failed to register iser"));

	memset(&ig, 0, sizeof(struct iser_global));

	/* device init is called only after the first addr resolution */
	sx_init(&ig.device_list_mutex,  "global_device_lock");
	INIT_LIST_HEAD(&ig.device_list);
	mtx_init(&ig.connlist_mutex, "iser_global_conn_lock", NULL, MTX_DEF);
	INIT_LIST_HEAD(&ig.connlist);
	sx_init(&ig.close_conns_mutex,  "global_close_conns_lock");

	return (error);
}

static int
icl_iser_unload(void)
{
	ISER_DBG("Removing iSER datamover...");

	if (icl_iser_ncons != 0)
		return (EBUSY);

	sx_destroy(&ig.close_conns_mutex);
	mtx_destroy(&ig.connlist_mutex);
	sx_destroy(&ig.device_list_mutex);

	icl_unregister("iser", true);

	uma_zdestroy(icl_pdu_zone);

	return (0);
}

static int
icl_iser_modevent(module_t mod, int what, void *arg)
{
	switch (what) {
	case MOD_LOAD:
		return (icl_iser_load());
	case MOD_UNLOAD:
		return (icl_iser_unload());
	default:
		return (EINVAL);
	}
}

moduledata_t icl_iser_data = {
	.name = "icl_iser",
	.evhand = icl_iser_modevent,
	.priv = 0
};

DECLARE_MODULE(icl_iser, icl_iser_data, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_DEPEND(icl_iser, icl, 1, 1, 1);
MODULE_DEPEND(icl_iser, ibcore, 1, 1, 1);
MODULE_DEPEND(icl_iser, linuxkpi, 1, 1, 1);
MODULE_VERSION(icl_iser, 1);
