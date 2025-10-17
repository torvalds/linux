// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2017, Microsoft Corporation.
 *   Copyright (C) 2018, LG Electronics.
 *
 *   Author(s): Long Li <longli@microsoft.com>,
 *		Hyunchul Lee <hyc.lee@gmail.com>
 */

#define SUBMOD_NAME	"smb_direct"

#define SMBDIRECT_USE_INLINE_C_FILES 1

#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/mempool.h>
#include <linux/highmem.h>
#include <linux/scatterlist.h>
#include <linux/string_choices.h>
#include <rdma/ib_verbs.h>
#include <rdma/rdma_cm.h>
#include <rdma/rw.h>

#include "glob.h"
#include "connection.h"
#include "smb_common.h"
#include "../common/smb2status.h"
#include "../common/smbdirect/smbdirect.h"
#include "../common/smbdirect/smbdirect_pdu.h"
#include "../common/smbdirect/smbdirect_socket.h"
#include "transport_rdma.h"

/*
 * This is a temporary solution until all code
 * is moved to smbdirect_all_c_files.c and we
 * have an smbdirect.ko that exports the required
 * functions.
 */
#include "../common/smbdirect/smbdirect_all_c_files.c"

#define SMB_DIRECT_PORT_IWARP		5445
#define SMB_DIRECT_PORT_INFINIBAND	445

#define SMB_DIRECT_VERSION_LE		cpu_to_le16(SMBDIRECT_V1)

/* SMB_DIRECT negotiation timeout (for the server) in seconds */
#define SMB_DIRECT_NEGOTIATE_TIMEOUT		5

/* The timeout to wait for a keepalive message from peer in seconds */
#define SMB_DIRECT_KEEPALIVE_SEND_INTERVAL	120

/* The timeout to wait for a keepalive message from peer in seconds */
#define SMB_DIRECT_KEEPALIVE_RECV_TIMEOUT	5

/*
 * Default maximum number of RDMA read/write outstanding on this connection
 * This value is possibly decreased during QP creation on hardware limit
 */
#define SMB_DIRECT_CM_INITIATOR_DEPTH		8

/* Maximum number of retries on data transfer operations */
#define SMB_DIRECT_CM_RETRY			6
/* No need to retry on Receiver Not Ready since SMB_DIRECT manages credits */
#define SMB_DIRECT_CM_RNR_RETRY		0

/*
 * User configurable initial values per SMB_DIRECT transport connection
 * as defined in [MS-SMBD] 3.1.1.1
 * Those may change after a SMB_DIRECT negotiation
 */

/* The local peer's maximum number of credits to grant to the peer */
static int smb_direct_receive_credit_max = 255;

/* The remote peer's credit request of local peer */
static int smb_direct_send_credit_target = 255;

/* The maximum single message size can be sent to remote peer */
static int smb_direct_max_send_size = 1364;

/*
 * The maximum fragmented upper-layer payload receive size supported
 *
 * Assume max_payload_per_credit is
 * smb_direct_receive_credit_max - 24 = 1340
 *
 * The maximum number would be
 * smb_direct_receive_credit_max * max_payload_per_credit
 *
 *                       1340 * 255 = 341700 (0x536C4)
 *
 * The minimum value from the spec is 131072 (0x20000)
 *
 * For now we use the logic we used before:
 *                 (1364 * 255) / 2 = 173910 (0x2A756)
 */
static int smb_direct_max_fragmented_recv_size = (1364 * 255) / 2;

/*  The maximum single-message size which can be received */
static int smb_direct_max_receive_size = 1364;

static int smb_direct_max_read_write_size = SMBD_DEFAULT_IOSIZE;

static LIST_HEAD(smb_direct_device_list);
static DEFINE_RWLOCK(smb_direct_device_lock);

struct smb_direct_device {
	struct ib_device	*ib_dev;
	struct list_head	list;
};

static struct smb_direct_listener {
	int			port;
	struct rdma_cm_id	*cm_id;
} smb_direct_ib_listener, smb_direct_iw_listener;

static struct workqueue_struct *smb_direct_wq;

struct smb_direct_transport {
	struct ksmbd_transport	transport;

	struct smbdirect_socket socket;
};

static bool smb_direct_logging_needed(struct smbdirect_socket *sc,
				      void *private_ptr,
				      unsigned int lvl,
				      unsigned int cls)
{
	if (lvl <= SMBDIRECT_LOG_ERR)
		return true;

	if (lvl > SMBDIRECT_LOG_INFO)
		return false;

	switch (cls) {
	/*
	 * These were more or less also logged before
	 * the move to common code.
	 *
	 * SMBDIRECT_LOG_RDMA_MR was not used, but
	 * that's client only code and we should
	 * notice if it's used on the server...
	 */
	case SMBDIRECT_LOG_RDMA_EVENT:
	case SMBDIRECT_LOG_RDMA_SEND:
	case SMBDIRECT_LOG_RDMA_RECV:
	case SMBDIRECT_LOG_WRITE:
	case SMBDIRECT_LOG_READ:
	case SMBDIRECT_LOG_NEGOTIATE:
	case SMBDIRECT_LOG_OUTGOING:
	case SMBDIRECT_LOG_RDMA_RW:
	case SMBDIRECT_LOG_RDMA_MR:
		return true;
	/*
	 * These were not logged before the move
	 * to common code.
	 */
	case SMBDIRECT_LOG_KEEP_ALIVE:
	case SMBDIRECT_LOG_INCOMING:
		return false;
	}

	/*
	 * Log all unknown messages
	 */
	return true;
}

static void smb_direct_logging_vaprintf(struct smbdirect_socket *sc,
					const char *func,
					unsigned int line,
					void *private_ptr,
					unsigned int lvl,
					unsigned int cls,
					struct va_format *vaf)
{
	if (lvl <= SMBDIRECT_LOG_ERR)
		pr_err("%pV", vaf);
	else
		ksmbd_debug(RDMA, "%pV", vaf);
}

#define KSMBD_TRANS(t) (&(t)->transport)
#define SMBD_TRANS(t)	(container_of(t, \
				struct smb_direct_transport, transport))

static const struct ksmbd_transport_ops ksmbd_smb_direct_transport_ops;

void init_smbd_max_io_size(unsigned int sz)
{
	sz = clamp_val(sz, SMBD_MIN_IOSIZE, SMBD_MAX_IOSIZE);
	smb_direct_max_read_write_size = sz;
}

unsigned int get_smbd_max_read_write_size(struct ksmbd_transport *kt)
{
	struct smb_direct_transport *t;
	struct smbdirect_socket *sc;
	struct smbdirect_socket_parameters *sp;

	if (kt->ops != &ksmbd_smb_direct_transport_ops)
		return 0;

	t = SMBD_TRANS(kt);
	sc = &t->socket;
	sp = &sc->parameters;

	return sp->max_read_write_size;
}

static struct smb_direct_transport *alloc_transport(struct rdma_cm_id *cm_id)
{
	struct smb_direct_transport *t;
	struct smbdirect_socket *sc;
	struct smbdirect_socket_parameters init_params = {};
	struct smbdirect_socket_parameters *sp;
	struct ksmbd_conn *conn;

	/*
	 * Create the initial parameters
	 */
	sp = &init_params;
	sp->negotiate_timeout_msec = SMB_DIRECT_NEGOTIATE_TIMEOUT * 1000;
	sp->initiator_depth = SMB_DIRECT_CM_INITIATOR_DEPTH;
	sp->responder_resources = 1;
	sp->recv_credit_max = smb_direct_receive_credit_max;
	sp->send_credit_target = smb_direct_send_credit_target;
	sp->max_send_size = smb_direct_max_send_size;
	sp->max_fragmented_recv_size = smb_direct_max_fragmented_recv_size;
	sp->max_recv_size = smb_direct_max_receive_size;
	sp->max_read_write_size = smb_direct_max_read_write_size;
	sp->keepalive_interval_msec = SMB_DIRECT_KEEPALIVE_SEND_INTERVAL * 1000;
	sp->keepalive_timeout_msec = SMB_DIRECT_KEEPALIVE_RECV_TIMEOUT * 1000;

	t = kzalloc_obj(*t, KSMBD_DEFAULT_GFP);
	if (!t)
		return NULL;
	sc = &t->socket;
	smbdirect_socket_prepare_create(sc, sp, smb_direct_wq);
	smbdirect_socket_set_logging(sc, NULL,
				     smb_direct_logging_needed,
				     smb_direct_logging_vaprintf);
	sc->ib.poll_ctx = IB_POLL_WORKQUEUE;
	sc->send_io.mem.gfp_mask = KSMBD_DEFAULT_GFP;
	sc->recv_io.mem.gfp_mask = KSMBD_DEFAULT_GFP;
	sc->rw_io.mem.gfp_mask = KSMBD_DEFAULT_GFP;
	/*
	 * from here we operate on the copy.
	 */
	sp = &sc->parameters;

	sc->rdma.cm_id = cm_id;
	cm_id->context = sc;

	sc->ib.dev = sc->rdma.cm_id->device;

	conn = ksmbd_conn_alloc();
	if (!conn)
		goto err;

	down_write(&conn_list_lock);
	hash_add(conn_list, &conn->hlist, 0);
	up_write(&conn_list_lock);

	conn->transport = KSMBD_TRANS(t);
	KSMBD_TRANS(t)->conn = conn;
	KSMBD_TRANS(t)->ops = &ksmbd_smb_direct_transport_ops;
	return t;
err:
	kfree(t);
	return NULL;
}

static void smb_direct_free_transport(struct ksmbd_transport *kt)
{
	kfree(SMBD_TRANS(kt));
}

static void free_transport(struct smb_direct_transport *t)
{
	struct smbdirect_socket *sc = &t->socket;

	smbdirect_socket_destroy_sync(sc);

	ksmbd_conn_free(KSMBD_TRANS(t)->conn);
}

static int smb_direct_check_recvmsg(struct smbdirect_recv_io *recvmsg)
{
	struct smbdirect_socket *sc = recvmsg->socket;

	switch (sc->recv_io.expected) {
	case SMBDIRECT_EXPECT_DATA_TRANSFER: {
		struct smbdirect_data_transfer *req =
			(struct smbdirect_data_transfer *)recvmsg->packet;
		struct smb2_hdr *hdr = (struct smb2_hdr *)(recvmsg->packet
				+ le32_to_cpu(req->data_offset));
		ksmbd_debug(RDMA,
			    "CreditGranted: %u, CreditRequested: %u, DataLength: %u, RemainingDataLength: %u, SMB: %x, Command: %u\n",
			    le16_to_cpu(req->credits_granted),
			    le16_to_cpu(req->credits_requested),
			    req->data_length, req->remaining_data_length,
			    hdr->ProtocolId, hdr->Command);
		return 0;
	}
	case SMBDIRECT_EXPECT_NEGOTIATE_REQ: {
		struct smbdirect_negotiate_req *req =
			(struct smbdirect_negotiate_req *)recvmsg->packet;
		ksmbd_debug(RDMA,
			    "MinVersion: %u, MaxVersion: %u, CreditRequested: %u, MaxSendSize: %u, MaxRecvSize: %u, MaxFragmentedSize: %u\n",
			    le16_to_cpu(req->min_version),
			    le16_to_cpu(req->max_version),
			    le16_to_cpu(req->credits_requested),
			    le32_to_cpu(req->preferred_send_size),
			    le32_to_cpu(req->max_receive_size),
			    le32_to_cpu(req->max_fragmented_size));
		if (le16_to_cpu(req->min_version) > 0x0100 ||
		    le16_to_cpu(req->max_version) < 0x0100)
			return -EOPNOTSUPP;
		if (le16_to_cpu(req->credits_requested) <= 0 ||
		    le32_to_cpu(req->max_receive_size) <= 128 ||
		    le32_to_cpu(req->max_fragmented_size) <=
					128 * 1024)
			return -ECONNABORTED;

		return 0;
	}
	case SMBDIRECT_EXPECT_NEGOTIATE_REP:
		/* client only */
		break;
	}

	/* This is an internal error */
	return -EINVAL;
}

static void recv_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct smbdirect_recv_io *recvmsg;
	struct smbdirect_socket *sc;
	struct smbdirect_socket_parameters *sp;

	recvmsg = container_of(wc->wr_cqe, struct smbdirect_recv_io, cqe);
	sc = recvmsg->socket;
	sp = &sc->parameters;

	if (wc->status != IB_WC_SUCCESS || wc->opcode != IB_WC_RECV) {
		smbdirect_connection_put_recv_io(recvmsg);
		if (wc->status != IB_WC_WR_FLUSH_ERR) {
			pr_err("Recv error. status='%s (%d)' opcode=%d\n",
			       ib_wc_status_msg(wc->status), wc->status,
			       wc->opcode);
			smbdirect_socket_schedule_cleanup(sc, -ECONNABORTED);
		}
		return;
	}

	ksmbd_debug(RDMA, "Recv completed. status='%s (%d)', opcode=%d\n",
		    ib_wc_status_msg(wc->status), wc->status,
		    wc->opcode);

	ib_dma_sync_single_for_cpu(wc->qp->device, recvmsg->sge.addr,
				   recvmsg->sge.length, DMA_FROM_DEVICE);

	/*
	 * Reset timer to the keepalive interval in
	 * order to trigger our next keepalive message.
	 */
	sc->idle.keepalive = SMBDIRECT_KEEPALIVE_NONE;
	mod_delayed_work(sc->workqueue, &sc->idle.timer_work,
			 msecs_to_jiffies(sp->keepalive_interval_msec));

	switch (sc->recv_io.expected) {
	case SMBDIRECT_EXPECT_NEGOTIATE_REQ:
		/* see smb_direct_negotiate_recv_done */
		break;
	case SMBDIRECT_EXPECT_DATA_TRANSFER: {
		struct smbdirect_data_transfer *data_transfer =
			(struct smbdirect_data_transfer *)recvmsg->packet;
		u32 remaining_data_length, data_offset, data_length;
		int current_recv_credits;
		u16 old_recv_credit_target;

		if (wc->byte_len <
		    offsetof(struct smbdirect_data_transfer, padding)) {
			smbdirect_connection_put_recv_io(recvmsg);
			smbdirect_socket_schedule_cleanup(sc, -ECONNABORTED);
			return;
		}

		remaining_data_length = le32_to_cpu(data_transfer->remaining_data_length);
		data_length = le32_to_cpu(data_transfer->data_length);
		data_offset = le32_to_cpu(data_transfer->data_offset);
		if (wc->byte_len < data_offset ||
		    wc->byte_len < (u64)data_offset + data_length) {
			smbdirect_connection_put_recv_io(recvmsg);
			smbdirect_socket_schedule_cleanup(sc, -ECONNABORTED);
			return;
		}
		if (remaining_data_length > sp->max_fragmented_recv_size ||
		    data_length > sp->max_fragmented_recv_size ||
		    (u64)remaining_data_length + (u64)data_length >
		    (u64)sp->max_fragmented_recv_size) {
			smbdirect_connection_put_recv_io(recvmsg);
			smbdirect_socket_schedule_cleanup(sc, -ECONNABORTED);
			return;
		}

		if (data_length) {
			if (sc->recv_io.reassembly.full_packet_received)
				recvmsg->first_segment = true;

			if (le32_to_cpu(data_transfer->remaining_data_length))
				sc->recv_io.reassembly.full_packet_received = false;
			else
				sc->recv_io.reassembly.full_packet_received = true;
		}

		atomic_dec(&sc->recv_io.posted.count);
		current_recv_credits = atomic_dec_return(&sc->recv_io.credits.count);

		old_recv_credit_target = sc->recv_io.credits.target;
		sc->recv_io.credits.target =
				le16_to_cpu(data_transfer->credits_requested);
		sc->recv_io.credits.target =
			min_t(u16, sc->recv_io.credits.target, sp->recv_credit_max);
		sc->recv_io.credits.target =
			max_t(u16, sc->recv_io.credits.target, 1);
		atomic_add(le16_to_cpu(data_transfer->credits_granted),
			   &sc->send_io.credits.count);

		if (le16_to_cpu(data_transfer->flags) &
		    SMBDIRECT_FLAG_RESPONSE_REQUESTED)
			queue_work(sc->workqueue, &sc->idle.immediate_work);

		if (atomic_read(&sc->send_io.credits.count) > 0)
			wake_up(&sc->send_io.credits.wait_queue);

		if (data_length) {
			if (current_recv_credits <= (sc->recv_io.credits.target / 4) ||
			    sc->recv_io.credits.target > old_recv_credit_target)
				queue_work(sc->workqueue, &sc->recv_io.posted.refill_work);

			smbdirect_connection_reassembly_append_recv_io(sc, recvmsg, data_length);
			wake_up(&sc->recv_io.reassembly.wait_queue);
		} else
			smbdirect_connection_put_recv_io(recvmsg);

		return;
	}
	case SMBDIRECT_EXPECT_NEGOTIATE_REP:
		/* client only */
		break;
	}

	/*
	 * This is an internal error!
	 */
	WARN_ON_ONCE(sc->recv_io.expected != SMBDIRECT_EXPECT_DATA_TRANSFER);
	smbdirect_connection_put_recv_io(recvmsg);
	smbdirect_socket_schedule_cleanup(sc, -ECONNABORTED);
}

static void smb_direct_negotiate_recv_work(struct work_struct *work);

static void smb_direct_negotiate_recv_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct smbdirect_recv_io *recv_io =
		container_of(wc->wr_cqe, struct smbdirect_recv_io, cqe);
	struct smbdirect_socket *sc = recv_io->socket;
	unsigned long flags;

	/*
	 * reset the common recv_done for later reuse.
	 */
	recv_io->cqe.done = recv_done;

	if (wc->status != IB_WC_SUCCESS || wc->opcode != IB_WC_RECV) {
		smbdirect_connection_put_recv_io(recv_io);
		if (wc->status != IB_WC_WR_FLUSH_ERR) {
			pr_err("Negotiate Recv error. status='%s (%d)' opcode=%d\n",
			       ib_wc_status_msg(wc->status), wc->status,
			       wc->opcode);
			smbdirect_socket_schedule_cleanup(sc, -ECONNABORTED);
		}
		return;
	}

	ksmbd_debug(RDMA, "Negotiate Recv completed. status='%s (%d)', opcode=%d\n",
		    ib_wc_status_msg(wc->status), wc->status,
		    wc->opcode);

	ib_dma_sync_single_for_cpu(sc->ib.dev,
				   recv_io->sge.addr,
				   recv_io->sge.length,
				   DMA_FROM_DEVICE);

	/*
	 * This is an internal error!
	 */
	if (WARN_ON_ONCE(sc->recv_io.expected != SMBDIRECT_EXPECT_NEGOTIATE_REQ)) {
		smbdirect_connection_put_recv_io(recv_io);
		smbdirect_socket_schedule_cleanup(sc, -ECONNABORTED);
		return;
	}

	/*
	 * Don't reset timer to the keepalive interval in
	 * this will be done in smb_direct_negotiate_recv_work.
	 */

	/*
	 * Only remember the recv_io if it has enough bytes,
	 * this gives smb_direct_negotiate_recv_work enough
	 * information in order to disconnect if it was not
	 * valid.
	 */
	sc->recv_io.reassembly.full_packet_received = true;
	if (wc->byte_len >= sizeof(struct smbdirect_negotiate_req))
		smbdirect_connection_reassembly_append_recv_io(sc, recv_io, 0);
	else
		smbdirect_connection_put_recv_io(recv_io);

	/*
	 * Some drivers (at least mlx5_ib and irdma in roce mode)
	 * might post a recv completion before RDMA_CM_EVENT_ESTABLISHED,
	 * we need to adjust our expectation in that case.
	 *
	 * So we defer further processing of the negotiation
	 * to smb_direct_negotiate_recv_work().
	 *
	 * If we are already in SMBDIRECT_SOCKET_NEGOTIATE_NEEDED
	 * we queue the work directly otherwise
	 * smb_direct_cm_handler() will do it, when
	 * RDMA_CM_EVENT_ESTABLISHED arrived.
	 */
	spin_lock_irqsave(&sc->connect.lock, flags);
	if (!sc->first_error) {
		INIT_WORK(&sc->connect.work, smb_direct_negotiate_recv_work);
		if (sc->status == SMBDIRECT_SOCKET_NEGOTIATE_NEEDED)
			queue_work(sc->workqueue, &sc->connect.work);
	}
	spin_unlock_irqrestore(&sc->connect.lock, flags);
}

static void smb_direct_negotiate_recv_work(struct work_struct *work)
{
	struct smbdirect_socket *sc =
		container_of(work, struct smbdirect_socket, connect.work);
	const struct smbdirect_socket_parameters *sp = &sc->parameters;
	struct smbdirect_recv_io *recv_io;

	if (sc->first_error)
		return;

	ksmbd_debug(RDMA, "Negotiate Recv Work running\n");

	/*
	 * Reset timer to the keepalive interval in
	 * order to trigger our next keepalive message.
	 */
	sc->idle.keepalive = SMBDIRECT_KEEPALIVE_NONE;
	mod_delayed_work(sc->workqueue, &sc->idle.timer_work,
			 msecs_to_jiffies(sp->keepalive_interval_msec));

	/*
	 * If smb_direct_negotiate_recv_done() detected an
	 * invalid request we want to disconnect.
	 */
	recv_io = smbdirect_connection_reassembly_first_recv_io(sc);
	if (!recv_io) {
		smbdirect_socket_schedule_cleanup(sc, -ECONNABORTED);
		return;
	}

	if (SMBDIRECT_CHECK_STATUS_WARN(sc, SMBDIRECT_SOCKET_NEGOTIATE_NEEDED)) {
		smbdirect_socket_schedule_cleanup(sc, -ECONNABORTED);
		return;
	}
	sc->status = SMBDIRECT_SOCKET_NEGOTIATE_RUNNING;
	wake_up(&sc->status_wait);
}

static int smb_direct_read(struct ksmbd_transport *t, char *buf,
			   unsigned int size, int unused)
{
	struct smb_direct_transport *st = SMBD_TRANS(t);
	struct smbdirect_socket *sc = &st->socket;
	struct msghdr msg = { .msg_flags = 0, };
	struct kvec iov = {
		.iov_base = buf,
		.iov_len = size,
	};
	int ret;

	iov_iter_kvec(&msg.msg_iter, ITER_DEST, &iov, 1, size);

	ret = smbdirect_connection_recvmsg(sc, &msg, 0);
	if (ret == -ERESTARTSYS)
		ret = -EINTR;
	return ret;
}

static int smb_direct_writev(struct ksmbd_transport *t,
			     struct kvec *iov, int niovs, int buflen,
			     bool need_invalidate, unsigned int remote_key)
{
	struct smb_direct_transport *st = SMBD_TRANS(t);
	struct smbdirect_socket *sc = &st->socket;
	struct iov_iter iter;

	iov_iter_kvec(&iter, ITER_SOURCE, iov, niovs, buflen);

	return smbdirect_connection_send_iter(sc, &iter, 0,
					      need_invalidate, remote_key);
}

static int smb_direct_rdma_write(struct ksmbd_transport *t,
				 void *buf, unsigned int buflen,
				 struct smbdirect_buffer_descriptor_v1 *desc,
				 unsigned int desc_len)
{
	struct smb_direct_transport *st = SMBD_TRANS(t);
	struct smbdirect_socket *sc = &st->socket;

	return smbdirect_connection_rdma_xmit(sc, buf, buflen,
					      desc, desc_len, false);
}

static int smb_direct_rdma_read(struct ksmbd_transport *t,
				void *buf, unsigned int buflen,
				struct smbdirect_buffer_descriptor_v1 *desc,
				unsigned int desc_len)
{
	struct smb_direct_transport *st = SMBD_TRANS(t);
	struct smbdirect_socket *sc = &st->socket;

	return smbdirect_connection_rdma_xmit(sc, buf, buflen,
					      desc, desc_len, true);
}

static void smb_direct_disconnect(struct ksmbd_transport *t)
{
	struct smb_direct_transport *st = SMBD_TRANS(t);
	struct smbdirect_socket *sc = &st->socket;

	ksmbd_debug(RDMA, "Disconnecting cm_id=%p\n", sc->rdma.cm_id);

	free_transport(st);
}

static void smb_direct_shutdown(struct ksmbd_transport *t)
{
	struct smb_direct_transport *st = SMBD_TRANS(t);
	struct smbdirect_socket *sc = &st->socket;

	ksmbd_debug(RDMA, "smb-direct shutdown cm_id=%p\n", sc->rdma.cm_id);

	smbdirect_socket_cleanup_work(&sc->disconnect_work);
}

static int smb_direct_cm_handler(struct rdma_cm_id *cm_id,
				 struct rdma_cm_event *event)
{
	struct smbdirect_socket *sc = cm_id->context;
	unsigned long flags;

	ksmbd_debug(RDMA, "RDMA CM event. cm_id=%p event=%s (%d)\n",
		    cm_id, rdma_event_msg(event->event), event->event);

	switch (event->event) {
	case RDMA_CM_EVENT_ESTABLISHED: {
		/*
		 * Some drivers (at least mlx5_ib and irdma in roce mode)
		 * might post a recv completion before RDMA_CM_EVENT_ESTABLISHED,
		 * we need to adjust our expectation in that case.
		 *
		 * If smb_direct_negotiate_recv_done was called first
		 * it initialized sc->connect.work only for us to
		 * start, so that we turned into
		 * SMBDIRECT_SOCKET_NEGOTIATE_NEEDED, before
		 * smb_direct_negotiate_recv_work() runs.
		 *
		 * If smb_direct_negotiate_recv_done didn't happen
		 * yet. sc->connect.work is still be disabled and
		 * queue_work() is a no-op.
		 */
		if (SMBDIRECT_CHECK_STATUS_DISCONNECT(sc, SMBDIRECT_SOCKET_RDMA_CONNECT_RUNNING))
			break;
		sc->status = SMBDIRECT_SOCKET_NEGOTIATE_NEEDED;
		spin_lock_irqsave(&sc->connect.lock, flags);
		if (!sc->first_error)
			queue_work(sc->workqueue, &sc->connect.work);
		spin_unlock_irqrestore(&sc->connect.lock, flags);
		wake_up(&sc->status_wait);
		break;
	}
	case RDMA_CM_EVENT_DEVICE_REMOVAL:
	case RDMA_CM_EVENT_DISCONNECTED: {
		sc->status = SMBDIRECT_SOCKET_DISCONNECTED;
		smbdirect_socket_cleanup_work(&sc->disconnect_work);
		if (sc->ib.qp)
			ib_drain_qp(sc->ib.qp);
		break;
	}
	case RDMA_CM_EVENT_CONNECT_ERROR: {
		sc->status = SMBDIRECT_SOCKET_DISCONNECTED;
		smbdirect_socket_cleanup_work(&sc->disconnect_work);
		break;
	}
	default:
		pr_err("Unexpected RDMA CM event. cm_id=%p, event=%s (%d)\n",
		       cm_id, rdma_event_msg(event->event),
		       event->event);
		break;
	}
	return 0;
}

static int smb_direct_send_negotiate_response(struct smbdirect_socket *sc,
					      int failed)
{
	struct smbdirect_socket_parameters *sp = &sc->parameters;
	struct smbdirect_send_io *sendmsg;
	struct smbdirect_negotiate_resp *resp;
	int ret;

	sendmsg = smbdirect_connection_alloc_send_io(sc);
	if (IS_ERR(sendmsg))
		return -ENOMEM;

	resp = (struct smbdirect_negotiate_resp *)sendmsg->packet;
	if (failed) {
		memset(resp, 0, sizeof(*resp));
		resp->min_version = SMB_DIRECT_VERSION_LE;
		resp->max_version = SMB_DIRECT_VERSION_LE;
		resp->status = STATUS_NOT_SUPPORTED;

		sc->status = SMBDIRECT_SOCKET_NEGOTIATE_FAILED;
	} else {
		resp->status = STATUS_SUCCESS;
		resp->min_version = SMB_DIRECT_VERSION_LE;
		resp->max_version = SMB_DIRECT_VERSION_LE;
		resp->negotiated_version = SMB_DIRECT_VERSION_LE;
		resp->reserved = 0;
		resp->credits_requested =
				cpu_to_le16(sp->send_credit_target);
		resp->credits_granted = cpu_to_le16(smbdirect_connection_grant_recv_credits(sc));
		resp->max_readwrite_size = cpu_to_le32(sp->max_read_write_size);
		resp->preferred_send_size = cpu_to_le32(sp->max_send_size);
		resp->max_receive_size = cpu_to_le32(sp->max_recv_size);
		resp->max_fragmented_size =
				cpu_to_le32(sp->max_fragmented_recv_size);

		atomic_set(&sc->send_io.bcredits.count, 1);
		sc->recv_io.expected = SMBDIRECT_EXPECT_DATA_TRANSFER;
		sc->status = SMBDIRECT_SOCKET_CONNECTED;
	}

	sendmsg->sge[0].addr = ib_dma_map_single(sc->ib.dev,
						 (void *)resp, sizeof(*resp),
						 DMA_TO_DEVICE);
	ret = ib_dma_mapping_error(sc->ib.dev, sendmsg->sge[0].addr);
	if (ret) {
		smbdirect_connection_free_send_io(sendmsg);
		return ret;
	}

	sendmsg->num_sge = 1;
	sendmsg->sge[0].length = sizeof(*resp);
	sendmsg->sge[0].lkey = sc->ib.pd->local_dma_lkey;

	ret = smbdirect_connection_post_send_io(sc, NULL, sendmsg);
	if (ret) {
		smbdirect_connection_free_send_io(sendmsg);
		return ret;
	}

	wait_event(sc->send_io.pending.zero_wait_queue,
		   atomic_read(&sc->send_io.pending.count) == 0 ||
		   sc->status != SMBDIRECT_SOCKET_CONNECTED);
	if (sc->status != SMBDIRECT_SOCKET_CONNECTED)
		return -ENOTCONN;

	return 0;
}

static int smb_direct_accept_client(struct smbdirect_socket *sc)
{
	struct smbdirect_socket_parameters *sp = &sc->parameters;
	struct rdma_conn_param conn_param;
	__be32 ird_ord_hdr[2];
	int ret;

	/*
	 * smb_direct_handle_connect_request()
	 * already negotiated sp->initiator_depth
	 * and sp->responder_resources
	 */
	memset(&conn_param, 0, sizeof(conn_param));
	conn_param.initiator_depth = sp->initiator_depth;
	conn_param.responder_resources = sp->responder_resources;

	if (sc->rdma.legacy_iwarp) {
		ird_ord_hdr[0] = cpu_to_be32(conn_param.responder_resources);
		ird_ord_hdr[1] = cpu_to_be32(conn_param.initiator_depth);
		conn_param.private_data = ird_ord_hdr;
		conn_param.private_data_len = sizeof(ird_ord_hdr);
	} else {
		conn_param.private_data = NULL;
		conn_param.private_data_len = 0;
	}
	conn_param.retry_count = SMB_DIRECT_CM_RETRY;
	conn_param.rnr_retry_count = SMB_DIRECT_CM_RNR_RETRY;
	conn_param.flow_control = 0;

	/*
	 * start with the negotiate timeout and SMBDIRECT_KEEPALIVE_PENDING
	 * so that the timer will cause a disconnect.
	 */
	sc->idle.keepalive = SMBDIRECT_KEEPALIVE_PENDING;
	mod_delayed_work(sc->workqueue, &sc->idle.timer_work,
			 msecs_to_jiffies(sp->negotiate_timeout_msec));

	WARN_ON_ONCE(sc->status != SMBDIRECT_SOCKET_RDMA_CONNECT_NEEDED);
	sc->status = SMBDIRECT_SOCKET_RDMA_CONNECT_RUNNING;
	ret = rdma_accept(sc->rdma.cm_id, &conn_param);
	if (ret) {
		pr_err("error at rdma_accept: %d\n", ret);
		return ret;
	}
	return 0;
}

static int smb_direct_prepare_negotiation(struct smbdirect_socket *sc)
{
	struct smbdirect_recv_io *recvmsg;
	bool recv_posted = false;
	int ret;

	WARN_ON_ONCE(sc->status != SMBDIRECT_SOCKET_CREATED);
	sc->status = SMBDIRECT_SOCKET_RDMA_CONNECT_NEEDED;

	sc->recv_io.expected = SMBDIRECT_EXPECT_NEGOTIATE_REQ;

	recvmsg = smbdirect_connection_get_recv_io(sc);
	if (!recvmsg)
		return -ENOMEM;
	recvmsg->cqe.done = smb_direct_negotiate_recv_done;

	ret = smbdirect_connection_post_recv_io(recvmsg);
	if (ret) {
		pr_err("Can't post recv: %d\n", ret);
		goto out_err;
	}
	recv_posted = true;

	ret = smb_direct_accept_client(sc);
	if (ret) {
		pr_err("Can't accept client\n");
		goto out_err;
	}

	return 0;
out_err:
	/*
	 * If the recv was never posted, return it to the free list.
	 * If it was posted, leave it alone so disconnect teardown can
	 * drain the QP and complete it (flush) and the completion path
	 * will unmap it exactly once.
	 */
	if (!recv_posted)
		smbdirect_connection_put_recv_io(recvmsg);
	return ret;
}

static int smb_direct_init_params(struct smbdirect_socket *sc)
{
	struct smbdirect_socket_parameters *sp = &sc->parameters;
	int max_send_sges;
	unsigned int maxpages;

	/* need 3 more sge. because a SMB_DIRECT header, SMB2 header,
	 * SMB2 response could be mapped.
	 */
	max_send_sges = DIV_ROUND_UP(sp->max_send_size, PAGE_SIZE) + 3;
	if (max_send_sges > SMBDIRECT_SEND_IO_MAX_SGE) {
		pr_err("max_send_size %d is too large\n", sp->max_send_size);
		return -EINVAL;
	}

	atomic_set(&sc->send_io.lcredits.count, sp->send_credit_target);

	maxpages = DIV_ROUND_UP(sp->max_read_write_size, PAGE_SIZE);
	sc->rw_io.credits.max = rdma_rw_mr_factor(sc->ib.dev,
						  sc->rdma.cm_id->port_num,
						  maxpages);
	sc->rw_io.credits.num_pages = DIV_ROUND_UP(maxpages, sc->rw_io.credits.max);
	/* add one extra in order to handle unaligned pages */
	sc->rw_io.credits.max += 1;

	sc->recv_io.credits.target = 1;

	atomic_set(&sc->rw_io.credits.count, sc->rw_io.credits.max);

	return 0;
}

static int smb_direct_prepare(struct ksmbd_transport *t)
{
	struct smb_direct_transport *st = SMBD_TRANS(t);
	struct smbdirect_socket *sc = &st->socket;
	struct smbdirect_socket_parameters *sp = &sc->parameters;
	struct smbdirect_recv_io *recvmsg;
	struct smbdirect_negotiate_req *req;
	unsigned long flags;
	int ret;

	/*
	 * We are waiting to pass the following states:
	 *
	 * SMBDIRECT_SOCKET_RDMA_CONNECT_NEEDED
	 * SMBDIRECT_SOCKET_RDMA_CONNECT_RUNNING
	 * SMBDIRECT_SOCKET_NEGOTIATE_NEEDED
	 *
	 * To finally get to SMBDIRECT_SOCKET_NEGOTIATE_RUNNING
	 * in order to continue below.
	 *
	 * Everything else is unexpected and an error.
	 */
	ksmbd_debug(RDMA, "Waiting for SMB_DIRECT negotiate request\n");
	ret = wait_event_interruptible_timeout(sc->status_wait,
					sc->status != SMBDIRECT_SOCKET_RDMA_CONNECT_NEEDED &&
					sc->status != SMBDIRECT_SOCKET_RDMA_CONNECT_RUNNING &&
					sc->status != SMBDIRECT_SOCKET_NEGOTIATE_NEEDED,
					msecs_to_jiffies(sp->negotiate_timeout_msec));
	if (ret <= 0 || sc->status != SMBDIRECT_SOCKET_NEGOTIATE_RUNNING)
		return ret < 0 ? ret : -ETIMEDOUT;

	recvmsg = smbdirect_connection_reassembly_first_recv_io(sc);
	if (!recvmsg)
		return -ECONNABORTED;

	ret = smb_direct_check_recvmsg(recvmsg);
	if (ret)
		goto put;

	req = (struct smbdirect_negotiate_req *)recvmsg->packet;
	sp->max_recv_size = min_t(u32, sp->max_recv_size,
				  le32_to_cpu(req->preferred_send_size));
	sp->max_send_size = min_t(u32, sp->max_send_size,
				  le32_to_cpu(req->max_receive_size));
	sp->max_fragmented_send_size =
		le32_to_cpu(req->max_fragmented_size);
	/*
	 * The maximum fragmented upper-layer payload receive size supported
	 *
	 * Assume max_payload_per_credit is
	 * smb_direct_receive_credit_max - 24 = 1340
	 *
	 * The maximum number would be
	 * smb_direct_receive_credit_max * max_payload_per_credit
	 *
	 *                       1340 * 255 = 341700 (0x536C4)
	 *
	 * The minimum value from the spec is 131072 (0x20000)
	 *
	 * For now we use the logic we used before:
	 *                 (1364 * 255) / 2 = 173910 (0x2A756)
	 *
	 * We need to adjust this here in case the peer
	 * lowered sp->max_recv_size.
	 *
	 * TODO: instead of adjusting max_fragmented_recv_size
	 * we should adjust the number of available buffers,
	 * but for now we keep the current logic.
	 */
	sp->max_fragmented_recv_size =
		(sp->recv_credit_max * sp->max_recv_size) / 2;
	sc->recv_io.credits.target = le16_to_cpu(req->credits_requested);
	sc->recv_io.credits.target = min_t(u16, sc->recv_io.credits.target, sp->recv_credit_max);
	sc->recv_io.credits.target = max_t(u16, sc->recv_io.credits.target, 1);

put:
	spin_lock_irqsave(&sc->recv_io.reassembly.lock, flags);
	sc->recv_io.reassembly.queue_length--;
	list_del(&recvmsg->list);
	spin_unlock_irqrestore(&sc->recv_io.reassembly.lock, flags);
	smbdirect_connection_put_recv_io(recvmsg);

	if (ret == -ECONNABORTED)
		return ret;

	if (ret)
		goto respond;

	/*
	 * We negotiated with success, so we need to refill the recv queue.
	 *
	 * The message that grants the credits to the client is
	 * the negotiate response.
	 */
	ret = smbdirect_connection_recv_io_refill(sc);
	if (ret < 0)
		return ret;
	ret = 0;

respond:
	ret = smb_direct_send_negotiate_response(sc, ret);
	if (ret)
		return ret;

	INIT_WORK(&sc->recv_io.posted.refill_work, smbdirect_connection_recv_io_refill_work);
	INIT_WORK(&sc->idle.immediate_work, smbdirect_connection_send_immediate_work);

	return 0;
}

static int smb_direct_connect(struct smbdirect_socket *sc)
{
	struct smbdirect_recv_io *recv_io;
	int ret;

	sc->rdma.cm_id->event_handler = smb_direct_cm_handler;

	ret = smb_direct_init_params(sc);
	if (ret) {
		pr_err("Can't configure RDMA parameters\n");
		return ret;
	}

	ret = smbdirect_connection_create_mem_pools(sc);
	if (ret) {
		pr_err("Can't init RDMA pool: %d\n", ret);
		return ret;
	}

	list_for_each_entry(recv_io, &sc->recv_io.free.list, list)
		recv_io->cqe.done = recv_done;

	ret = smbdirect_connection_create_qp(sc);
	if (ret) {
		pr_err("Can't accept RDMA client: %d\n", ret);
		return ret;
	}

	ret = smb_direct_prepare_negotiation(sc);
	if (ret) {
		pr_err("Can't negotiate: %d\n", ret);
		return ret;
	}
	return 0;
}

static int smb_direct_handle_connect_request(struct rdma_cm_id *new_cm_id,
					     struct rdma_cm_event *event)
{
	struct smb_direct_listener *listener = new_cm_id->context;
	struct smb_direct_transport *t;
	struct smbdirect_socket *sc;
	struct smbdirect_socket_parameters *sp;
	struct task_struct *handler;
	u8 peer_initiator_depth;
	u8 peer_responder_resources;
	int ret;

	if (!smbdirect_frwr_is_supported(&new_cm_id->device->attrs)) {
		ksmbd_debug(RDMA,
			    "Fast Registration Work Requests is not supported. device capabilities=%llx\n",
			    new_cm_id->device->attrs.device_cap_flags);
		return -EPROTONOSUPPORT;
	}

	t = alloc_transport(new_cm_id);
	if (!t)
		return -ENOMEM;
	sc = &t->socket;
	sp = &sc->parameters;

	/*
	 * First set what the we as server are able to support
	 */
	sp->initiator_depth = min_t(u8, sp->initiator_depth,
				    sc->ib.dev->attrs.max_qp_rd_atom);

	peer_initiator_depth = event->param.conn.initiator_depth;
	peer_responder_resources = event->param.conn.responder_resources;
	smbdirect_connection_negotiate_rdma_resources(sc,
						      peer_initiator_depth,
						      peer_responder_resources,
						      &event->param.conn);

	ret = smb_direct_connect(sc);
	if (ret)
		goto out_err;

	handler = kthread_run(ksmbd_conn_handler_loop,
			      KSMBD_TRANS(t)->conn, "ksmbd:r%u",
			      listener->port);
	if (IS_ERR(handler)) {
		ret = PTR_ERR(handler);
		pr_err("Can't start thread\n");
		goto out_err;
	}

	return 0;
out_err:
	free_transport(t);
	return ret;
}

static int smb_direct_listen_handler(struct rdma_cm_id *cm_id,
				     struct rdma_cm_event *event)
{
	switch (event->event) {
	case RDMA_CM_EVENT_CONNECT_REQUEST: {
		int ret = smb_direct_handle_connect_request(cm_id, event);

		if (ret) {
			pr_err("Can't create transport: %d\n", ret);
			return ret;
		}

		ksmbd_debug(RDMA, "Received connection request. cm_id=%p\n",
			    cm_id);
		break;
	}
	default:
		pr_err("Unexpected listen event. cm_id=%p, event=%s (%d)\n",
		       cm_id, rdma_event_msg(event->event), event->event);
		break;
	}
	return 0;
}

static int smb_direct_listen(struct smb_direct_listener *listener,
			     int port)
{
	int ret;
	struct rdma_cm_id *cm_id;
	u8 node_type = RDMA_NODE_UNSPECIFIED;
	struct sockaddr_in sin = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= htonl(INADDR_ANY),
		.sin_port		= htons(port),
	};

	switch (port) {
	case SMB_DIRECT_PORT_IWARP:
		/*
		 * only allow iWarp devices
		 * for port 5445.
		 */
		node_type = RDMA_NODE_RNIC;
		break;
	case SMB_DIRECT_PORT_INFINIBAND:
		/*
		 * only allow InfiniBand, RoCEv1 or RoCEv2
		 * devices for port 445.
		 *
		 * (Basically don't allow iWarp devices)
		 */
		node_type = RDMA_NODE_IB_CA;
		break;
	default:
		pr_err("unsupported smbdirect port=%d!\n", port);
		return -ENODEV;
	}

	cm_id = rdma_create_id(&init_net, smb_direct_listen_handler,
			       listener, RDMA_PS_TCP, IB_QPT_RC);
	if (IS_ERR(cm_id)) {
		pr_err("Can't create cm id: %ld\n", PTR_ERR(cm_id));
		return PTR_ERR(cm_id);
	}

	ret = rdma_restrict_node_type(cm_id, node_type);
	if (ret) {
		pr_err("rdma_restrict_node_type(%u) failed %d\n",
		       node_type, ret);
		goto err;
	}

	ret = rdma_bind_addr(cm_id, (struct sockaddr *)&sin);
	if (ret) {
		pr_err("Can't bind: %d\n", ret);
		goto err;
	}

	ret = rdma_listen(cm_id, 10);
	if (ret) {
		pr_err("Can't listen: %d\n", ret);
		goto err;
	}

	listener->port = port;
	listener->cm_id = cm_id;

	return 0;
err:
	listener->port = 0;
	listener->cm_id = NULL;
	rdma_destroy_id(cm_id);
	return ret;
}

static int smb_direct_ib_client_add(struct ib_device *ib_dev)
{
	struct smb_direct_device *smb_dev;

	if (!smbdirect_frwr_is_supported(&ib_dev->attrs))
		return 0;

	smb_dev = kzalloc_obj(*smb_dev, KSMBD_DEFAULT_GFP);
	if (!smb_dev)
		return -ENOMEM;
	smb_dev->ib_dev = ib_dev;

	write_lock(&smb_direct_device_lock);
	list_add(&smb_dev->list, &smb_direct_device_list);
	write_unlock(&smb_direct_device_lock);

	ksmbd_debug(RDMA, "ib device added: name %s\n", ib_dev->name);
	return 0;
}

static void smb_direct_ib_client_remove(struct ib_device *ib_dev,
					void *client_data)
{
	struct smb_direct_device *smb_dev, *tmp;

	write_lock(&smb_direct_device_lock);
	list_for_each_entry_safe(smb_dev, tmp, &smb_direct_device_list, list) {
		if (smb_dev->ib_dev == ib_dev) {
			list_del(&smb_dev->list);
			kfree(smb_dev);
			break;
		}
	}
	write_unlock(&smb_direct_device_lock);
}

static struct ib_client smb_direct_ib_client = {
	.name	= "ksmbd_smb_direct_ib",
	.add	= smb_direct_ib_client_add,
	.remove	= smb_direct_ib_client_remove,
};

int ksmbd_rdma_init(void)
{
	int ret;

	smb_direct_ib_listener = smb_direct_iw_listener = (struct smb_direct_listener) {
		.cm_id = NULL,
	};

	ret = ib_register_client(&smb_direct_ib_client);
	if (ret) {
		pr_err("failed to ib_register_client\n");
		return ret;
	}

	/* When a client is running out of send credits, the credits are
	 * granted by the server's sending a packet using this queue.
	 * This avoids the situation that a clients cannot send packets
	 * for lack of credits
	 */
	smb_direct_wq = alloc_workqueue("ksmbd-smb_direct-wq",
					WQ_HIGHPRI | WQ_MEM_RECLAIM | WQ_PERCPU,
					0);
	if (!smb_direct_wq) {
		ret = -ENOMEM;
		goto err;
	}

	ret = smb_direct_listen(&smb_direct_ib_listener,
				SMB_DIRECT_PORT_INFINIBAND);
	if (ret) {
		pr_err("Can't listen on InfiniBand/RoCEv1/RoCEv2: %d\n", ret);
		goto err;
	}

	ksmbd_debug(RDMA, "InfiniBand/RoCEv1/RoCEv2 RDMA listener. cm_id=%p\n",
		    smb_direct_ib_listener.cm_id);

	ret = smb_direct_listen(&smb_direct_iw_listener,
				SMB_DIRECT_PORT_IWARP);
	if (ret) {
		pr_err("Can't listen on iWarp: %d\n", ret);
		goto err;
	}

	ksmbd_debug(RDMA, "iWarp RDMA listener. cm_id=%p\n",
		    smb_direct_iw_listener.cm_id);

	return 0;
err:
	ksmbd_rdma_stop_listening();
	ksmbd_rdma_destroy();
	return ret;
}

void ksmbd_rdma_stop_listening(void)
{
	if (!smb_direct_ib_listener.cm_id && !smb_direct_iw_listener.cm_id)
		return;

	ib_unregister_client(&smb_direct_ib_client);

	if (smb_direct_ib_listener.cm_id)
		rdma_destroy_id(smb_direct_ib_listener.cm_id);
	if (smb_direct_iw_listener.cm_id)
		rdma_destroy_id(smb_direct_iw_listener.cm_id);

	smb_direct_ib_listener = smb_direct_iw_listener = (struct smb_direct_listener) {
		.cm_id = NULL,
	};
}

void ksmbd_rdma_destroy(void)
{
	if (smb_direct_wq) {
		destroy_workqueue(smb_direct_wq);
		smb_direct_wq = NULL;
	}
}

static bool ksmbd_find_rdma_capable_netdev(struct net_device *netdev)
{
	struct smb_direct_device *smb_dev;
	int i;
	bool rdma_capable = false;

	read_lock(&smb_direct_device_lock);
	list_for_each_entry(smb_dev, &smb_direct_device_list, list) {
		for (i = 0; i < smb_dev->ib_dev->phys_port_cnt; i++) {
			struct net_device *ndev;

			ndev = ib_device_get_netdev(smb_dev->ib_dev, i + 1);
			if (!ndev)
				continue;

			if (ndev == netdev) {
				dev_put(ndev);
				rdma_capable = true;
				goto out;
			}
			dev_put(ndev);
		}
	}
out:
	read_unlock(&smb_direct_device_lock);

	if (rdma_capable == false) {
		struct ib_device *ibdev;

		ibdev = ib_device_get_by_netdev(netdev, RDMA_DRIVER_UNKNOWN);
		if (ibdev) {
			rdma_capable = smbdirect_frwr_is_supported(&ibdev->attrs);
			ib_device_put(ibdev);
		}
	}

	ksmbd_debug(RDMA, "netdev(%s) rdma capable : %s\n",
		    netdev->name, str_true_false(rdma_capable));

	return rdma_capable;
}

bool ksmbd_rdma_capable_netdev(struct net_device *netdev)
{
	struct net_device *lower_dev;
	struct list_head *iter;

	if (ksmbd_find_rdma_capable_netdev(netdev))
		return true;

	/* check if netdev is bridge or VLAN */
	if (netif_is_bridge_master(netdev) ||
	    netdev->priv_flags & IFF_802_1Q_VLAN)
		netdev_for_each_lower_dev(netdev, lower_dev, iter)
			if (ksmbd_find_rdma_capable_netdev(lower_dev))
				return true;

	/* check if netdev is IPoIB safely without layer violation */
	if (netdev->type == ARPHRD_INFINIBAND)
		return true;

	return false;
}

static const struct ksmbd_transport_ops ksmbd_smb_direct_transport_ops = {
	.prepare	= smb_direct_prepare,
	.disconnect	= smb_direct_disconnect,
	.shutdown	= smb_direct_shutdown,
	.writev		= smb_direct_writev,
	.read		= smb_direct_read,
	.rdma_read	= smb_direct_rdma_read,
	.rdma_write	= smb_direct_rdma_write,
	.free_transport = smb_direct_free_transport,
};
