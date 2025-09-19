// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2017, Microsoft Corporation.
 *
 *   Author(s): Long Li <longli@microsoft.com>
 */

#define SMBDIRECT_USE_INLINE_C_FILES 1

#include <linux/module.h>
#include <linux/highmem.h>
#include <linux/folio_queue.h>
#include "../common/smbdirect/smbdirect_pdu.h"
#include "smbdirect.h"
#include "cifs_debug.h"
#include "cifsproto.h"
#include "smb2proto.h"

const struct smbdirect_socket_parameters *smbd_get_parameters(struct smbd_connection *conn)
{
	struct smbdirect_socket *sc = &conn->socket;

	return &sc->parameters;
}

static int smbd_post_send(struct smbdirect_socket *sc,
			  struct smbdirect_send_batch *batch,
			  struct smbdirect_send_io *request);

static int smbd_post_send_empty(struct smbdirect_socket *sc);

/* Port numbers for SMBD transport */
#define SMB_PORT	445
#define SMBD_PORT	5445

/* Address lookup and resolve timeout in ms */
#define RDMA_RESOLVE_TIMEOUT	5000

/* SMBD negotiation timeout in seconds */
#define SMBD_NEGOTIATE_TIMEOUT	120

/* The timeout to wait for a keepalive message from peer in seconds */
#define KEEPALIVE_RECV_TIMEOUT 5

/* SMBD minimum receive size and fragmented sized defined in [MS-SMBD] */
#define SMBD_MIN_RECEIVE_SIZE		128
#define SMBD_MIN_FRAGMENTED_SIZE	131072

/*
 * Default maximum number of RDMA read/write outstanding on this connection
 * This value is possibly decreased during QP creation on hardware limit
 */
#define SMBD_CM_RESPONDER_RESOURCES	32

/* Maximum number of retries on data transfer operations */
#define SMBD_CM_RETRY			6
/* No need to retry on Receiver Not Ready since SMBD manages credits */
#define SMBD_CM_RNR_RETRY		0

/*
 * User configurable initial values per SMBD transport connection
 * as defined in [MS-SMBD] 3.1.1.1
 * Those may change after a SMBD negotiation
 */
/* The local peer's maximum number of credits to grant to the peer */
int smbd_receive_credit_max = 255;

/* The remote peer's credit request of local peer */
int smbd_send_credit_target = 255;

/* The maximum single message size can be sent to remote peer */
int smbd_max_send_size = 1364;

/*
 * The maximum fragmented upper-layer payload receive size supported
 *
 * Assume max_payload_per_credit is
 * smbd_max_receive_size - 24 = 1340
 *
 * The maximum number would be
 * smbd_receive_credit_max * max_payload_per_credit
 *
 *                       1340 * 255 = 341700 (0x536C4)
 *
 * The minimum value from the spec is 131072 (0x20000)
 *
 * For now we use the logic we used in ksmbd before:
 *                 (1364 * 255) / 2 = 173910 (0x2A756)
 */
int smbd_max_fragmented_recv_size = (1364 * 255) / 2;

/*  The maximum single-message size which can be received */
int smbd_max_receive_size = 1364;

/* The timeout to initiate send of a keepalive message on idle */
int smbd_keep_alive_interval = 120;

/*
 * User configurable initial values for RDMA transport
 * The actual values used may be lower and are limited to hardware capabilities
 */
/* Default maximum number of pages in a single RDMA write/read */
int smbd_max_frmr_depth = 2048;

/* If payload is less than this byte, use RDMA send/recv not read/write */
int rdma_readwrite_threshold = 4096;

/* Transport logging functions
 * Logging are defined as classes. They can be OR'ed to define the actual
 * logging level via module parameter smbd_logging_class
 * e.g. cifs.smbd_logging_class=0xa0 will log all log_rdma_recv() and
 * log_rdma_event()
 */
#define LOG_OUTGOING			0x1
#define LOG_INCOMING			0x2
#define LOG_READ			0x4
#define LOG_WRITE			0x8
#define LOG_RDMA_SEND			0x10
#define LOG_RDMA_RECV			0x20
#define LOG_KEEP_ALIVE			0x40
#define LOG_RDMA_EVENT			0x80
#define LOG_RDMA_MR			0x100
static unsigned int smbd_logging_class;
module_param(smbd_logging_class, uint, 0644);
MODULE_PARM_DESC(smbd_logging_class,
	"Logging class for SMBD transport 0x0 to 0x100");

#define ERR		0x0
#define INFO		0x1
static unsigned int smbd_logging_level = ERR;
module_param(smbd_logging_level, uint, 0644);
MODULE_PARM_DESC(smbd_logging_level,
	"Logging level for SMBD transport, 0 (default): error, 1: info");

/*
 * This is a temporary solution until all code
 * is moved to smbdirect_all_c_files.c and we
 * have an smbdirect.ko that exports the required
 * functions.
 */
#include "../common/smbdirect/smbdirect_all_c_files.c"

static bool smbd_logging_needed(struct smbdirect_socket *sc,
				void *private_ptr,
				unsigned int lvl,
				unsigned int cls)
{
#define BUILD_BUG_SAME(x) BUILD_BUG_ON(x != SMBDIRECT_LOG_ ##x)
	BUILD_BUG_SAME(ERR);
	BUILD_BUG_SAME(INFO);
#undef BUILD_BUG_SAME
#define BUILD_BUG_SAME(x) BUILD_BUG_ON(x != SMBDIRECT_ ##x)
	BUILD_BUG_SAME(LOG_OUTGOING);
	BUILD_BUG_SAME(LOG_INCOMING);
	BUILD_BUG_SAME(LOG_READ);
	BUILD_BUG_SAME(LOG_WRITE);
	BUILD_BUG_SAME(LOG_RDMA_SEND);
	BUILD_BUG_SAME(LOG_RDMA_RECV);
	BUILD_BUG_SAME(LOG_KEEP_ALIVE);
	BUILD_BUG_SAME(LOG_RDMA_EVENT);
	BUILD_BUG_SAME(LOG_RDMA_MR);
#undef BUILD_BUG_SAME

	if (lvl <= smbd_logging_level || cls & smbd_logging_class)
		return true;
	return false;
}

static void smbd_logging_vaprintf(struct smbdirect_socket *sc,
				  const char *func,
				  unsigned int line,
				  void *private_ptr,
				  unsigned int lvl,
				  unsigned int cls,
				  struct va_format *vaf)
{
	cifs_dbg(VFS, "%s:%u %pV", func, line, vaf);
}

#define log_rdma(level, class, fmt, args...)				\
do {									\
	if (level <= smbd_logging_level || class & smbd_logging_class)	\
		cifs_dbg(VFS, "%s:%d " fmt, __func__, __LINE__, ##args);\
} while (0)

#define log_outgoing(level, fmt, args...) \
		log_rdma(level, LOG_OUTGOING, fmt, ##args)
#define log_incoming(level, fmt, args...) \
		log_rdma(level, LOG_INCOMING, fmt, ##args)
#define log_read(level, fmt, args...)	log_rdma(level, LOG_READ, fmt, ##args)
#define log_write(level, fmt, args...)	log_rdma(level, LOG_WRITE, fmt, ##args)
#define log_rdma_send(level, fmt, args...) \
		log_rdma(level, LOG_RDMA_SEND, fmt, ##args)
#define log_rdma_recv(level, fmt, args...) \
		log_rdma(level, LOG_RDMA_RECV, fmt, ##args)
#define log_keep_alive(level, fmt, args...) \
		log_rdma(level, LOG_KEEP_ALIVE, fmt, ##args)
#define log_rdma_event(level, fmt, args...) \
		log_rdma(level, LOG_RDMA_EVENT, fmt, ##args)
#define log_rdma_mr(level, fmt, args...) \
		log_rdma(level, LOG_RDMA_MR, fmt, ##args)

/* Upcall from RDMA CM */
static int smbd_conn_upcall(
		struct rdma_cm_id *id, struct rdma_cm_event *event)
{
	struct smbdirect_socket *sc = id->context;
	const char *event_name = rdma_event_msg(event->event);
	u8 peer_initiator_depth;
	u8 peer_responder_resources;

	log_rdma_event(INFO, "event=%s status=%d\n",
		event_name, event->status);

	switch (event->event) {
	case RDMA_CM_EVENT_ADDR_RESOLVED:
		if (SMBDIRECT_CHECK_STATUS_DISCONNECT(sc, SMBDIRECT_SOCKET_RESOLVE_ADDR_RUNNING))
			break;
		sc->status = SMBDIRECT_SOCKET_RESOLVE_ROUTE_NEEDED;
		wake_up(&sc->status_wait);
		break;

	case RDMA_CM_EVENT_ROUTE_RESOLVED:
		if (SMBDIRECT_CHECK_STATUS_DISCONNECT(sc, SMBDIRECT_SOCKET_RESOLVE_ROUTE_RUNNING))
			break;
		sc->status = SMBDIRECT_SOCKET_RDMA_CONNECT_NEEDED;
		wake_up(&sc->status_wait);
		break;

	case RDMA_CM_EVENT_ADDR_ERROR:
		log_rdma_event(ERR, "connecting failed event=%s\n", event_name);
		sc->status = SMBDIRECT_SOCKET_RESOLVE_ADDR_FAILED;
		smbdirect_socket_cleanup_work(&sc->disconnect_work);
		break;

	case RDMA_CM_EVENT_ROUTE_ERROR:
		log_rdma_event(ERR, "connecting failed event=%s\n", event_name);
		sc->status = SMBDIRECT_SOCKET_RESOLVE_ROUTE_FAILED;
		smbdirect_socket_cleanup_work(&sc->disconnect_work);
		break;

	case RDMA_CM_EVENT_ESTABLISHED:
		log_rdma_event(INFO, "connected event=%s\n", event_name);

		/*
		 * Here we work around an inconsistency between
		 * iWarp and other devices (at least rxe and irdma using RoCEv2)
		 */
		if (rdma_protocol_iwarp(id->device, id->port_num)) {
			/*
			 * iWarp devices report the peer's values
			 * with the perspective of the peer here.
			 * Tested with siw and irdma (in iwarp mode)
			 * We need to change to our perspective here,
			 * so we need to switch the values.
			 */
			peer_initiator_depth = event->param.conn.responder_resources;
			peer_responder_resources = event->param.conn.initiator_depth;
		} else {
			/*
			 * Non iWarp devices report the peer's values
			 * already changed to our perspective here.
			 * Tested with rxe and irdma (in roce mode).
			 */
			peer_initiator_depth = event->param.conn.initiator_depth;
			peer_responder_resources = event->param.conn.responder_resources;
		}
		smbdirect_connection_negotiate_rdma_resources(sc,
							      peer_initiator_depth,
							      peer_responder_resources,
							      &event->param.conn);

		if (SMBDIRECT_CHECK_STATUS_DISCONNECT(sc, SMBDIRECT_SOCKET_RDMA_CONNECT_RUNNING))
			break;
		sc->status = SMBDIRECT_SOCKET_NEGOTIATE_NEEDED;
		wake_up(&sc->status_wait);
		break;

	case RDMA_CM_EVENT_CONNECT_ERROR:
	case RDMA_CM_EVENT_UNREACHABLE:
	case RDMA_CM_EVENT_REJECTED:
		log_rdma_event(ERR, "connecting failed event=%s\n", event_name);
		sc->status = SMBDIRECT_SOCKET_RDMA_CONNECT_FAILED;
		smbdirect_socket_cleanup_work(&sc->disconnect_work);
		break;

	case RDMA_CM_EVENT_DEVICE_REMOVAL:
	case RDMA_CM_EVENT_DISCONNECTED:
		/* This happens when we fail the negotiation */
		if (sc->status == SMBDIRECT_SOCKET_NEGOTIATE_FAILED) {
			log_rdma_event(ERR, "event=%s during negotiation\n", event_name);
		}

		sc->status = SMBDIRECT_SOCKET_DISCONNECTED;
		smbdirect_socket_cleanup_work(&sc->disconnect_work);
		break;

	default:
		log_rdma_event(ERR, "unexpected event=%s status=%d\n",
			       event_name, event->status);
		break;
	}

	return 0;
}

static inline void *smbdirect_send_io_payload(struct smbdirect_send_io *request)
{
	return (void *)request->packet;
}

static inline void *smbdirect_recv_io_payload(struct smbdirect_recv_io *response)
{
	return (void *)response->packet;
}

static void dump_smbdirect_negotiate_resp(struct smbdirect_negotiate_resp *resp)
{
	log_rdma_event(INFO, "resp message min_version %u max_version %u negotiated_version %u credits_requested %u credits_granted %u status %u max_readwrite_size %u preferred_send_size %u max_receive_size %u max_fragmented_size %u\n",
		       resp->min_version, resp->max_version,
		       resp->negotiated_version, resp->credits_requested,
		       resp->credits_granted, resp->status,
		       resp->max_readwrite_size, resp->preferred_send_size,
		       resp->max_receive_size, resp->max_fragmented_size);
}

/*
 * Process a negotiation response message, according to [MS-SMBD]3.1.5.7
 * response, packet_length: the negotiation response message
 * return value: true if negotiation is a success, false if failed
 */
static bool process_negotiation_response(
		struct smbdirect_recv_io *response, int packet_length)
{
	struct smbdirect_socket *sc = response->socket;
	struct smbdirect_socket_parameters *sp = &sc->parameters;
	struct smbdirect_negotiate_resp *packet = smbdirect_recv_io_payload(response);

	if (packet_length < sizeof(struct smbdirect_negotiate_resp)) {
		log_rdma_event(ERR,
			"error: packet_length=%d\n", packet_length);
		return false;
	}

	if (le16_to_cpu(packet->negotiated_version) != SMBDIRECT_V1) {
		log_rdma_event(ERR, "error: negotiated_version=%x\n",
			le16_to_cpu(packet->negotiated_version));
		return false;
	}

	if (packet->credits_requested == 0) {
		log_rdma_event(ERR, "error: credits_requested==0\n");
		return false;
	}
	sc->recv_io.credits.target = le16_to_cpu(packet->credits_requested);
	sc->recv_io.credits.target = min_t(u16, sc->recv_io.credits.target, sp->recv_credit_max);

	if (packet->credits_granted == 0) {
		log_rdma_event(ERR, "error: credits_granted==0\n");
		return false;
	}
	atomic_set(&sc->send_io.lcredits.count, sp->send_credit_target);
	atomic_set(&sc->send_io.credits.count, le16_to_cpu(packet->credits_granted));

	if (le32_to_cpu(packet->preferred_send_size) > sp->max_recv_size) {
		log_rdma_event(ERR, "error: preferred_send_size=%d\n",
			le32_to_cpu(packet->preferred_send_size));
		return false;
	}
	sp->max_recv_size = le32_to_cpu(packet->preferred_send_size);

	if (le32_to_cpu(packet->max_receive_size) < SMBD_MIN_RECEIVE_SIZE) {
		log_rdma_event(ERR, "error: max_receive_size=%d\n",
			le32_to_cpu(packet->max_receive_size));
		return false;
	}
	sp->max_send_size = min_t(u32, sp->max_send_size,
				  le32_to_cpu(packet->max_receive_size));

	if (le32_to_cpu(packet->max_fragmented_size) <
			SMBD_MIN_FRAGMENTED_SIZE) {
		log_rdma_event(ERR, "error: max_fragmented_size=%d\n",
			le32_to_cpu(packet->max_fragmented_size));
		return false;
	}
	sp->max_fragmented_send_size =
		le32_to_cpu(packet->max_fragmented_size);


	sp->max_read_write_size = min_t(u32,
			le32_to_cpu(packet->max_readwrite_size),
			sp->max_frmr_depth * PAGE_SIZE);
	sp->max_frmr_depth = sp->max_read_write_size / PAGE_SIZE;

	atomic_set(&sc->send_io.bcredits.count, 1);
	sc->recv_io.expected = SMBDIRECT_EXPECT_DATA_TRANSFER;
	return true;
}

/* Called from softirq, when recv is done */
static void recv_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct smbdirect_data_transfer *data_transfer;
	struct smbdirect_recv_io *response =
		container_of(wc->wr_cqe, struct smbdirect_recv_io, cqe);
	struct smbdirect_socket *sc = response->socket;
	struct smbdirect_socket_parameters *sp = &sc->parameters;
	int current_recv_credits;
	u16 old_recv_credit_target;
	u32 data_offset = 0;
	u32 data_length = 0;
	u32 remaining_data_length = 0;
	bool negotiate_done = false;

	log_rdma_recv(INFO,
		      "response=0x%p type=%d wc status=%s wc opcode %d byte_len=%d pkey_index=%u\n",
		      response, sc->recv_io.expected,
		      ib_wc_status_msg(wc->status), wc->opcode,
		      wc->byte_len, wc->pkey_index);

	if (wc->status != IB_WC_SUCCESS || wc->opcode != IB_WC_RECV) {
		if (wc->status != IB_WC_WR_FLUSH_ERR)
			log_rdma_recv(ERR, "wc->status=%s opcode=%d\n",
				ib_wc_status_msg(wc->status), wc->opcode);
		goto error;
	}

	ib_dma_sync_single_for_cpu(
		wc->qp->device,
		response->sge.addr,
		response->sge.length,
		DMA_FROM_DEVICE);

	/*
	 * Reset timer to the keepalive interval in
	 * order to trigger our next keepalive message.
	 */
	sc->idle.keepalive = SMBDIRECT_KEEPALIVE_NONE;
	mod_delayed_work(sc->workqueue, &sc->idle.timer_work,
			 msecs_to_jiffies(sp->keepalive_interval_msec));

	switch (sc->recv_io.expected) {
	/* SMBD negotiation response */
	case SMBDIRECT_EXPECT_NEGOTIATE_REP:
		dump_smbdirect_negotiate_resp(smbdirect_recv_io_payload(response));
		sc->recv_io.reassembly.full_packet_received = true;
		negotiate_done =
			process_negotiation_response(response, wc->byte_len);
		smbdirect_connection_put_recv_io(response);
		if (SMBDIRECT_CHECK_STATUS_WARN(sc, SMBDIRECT_SOCKET_NEGOTIATE_RUNNING))
			negotiate_done = false;
		if (!negotiate_done) {
			sc->status = SMBDIRECT_SOCKET_NEGOTIATE_FAILED;
			smbdirect_socket_schedule_cleanup(sc, -ECONNABORTED);
		} else {
			sc->status = SMBDIRECT_SOCKET_CONNECTED;
			wake_up(&sc->status_wait);
		}

		return;

	/* SMBD data transfer packet */
	case SMBDIRECT_EXPECT_DATA_TRANSFER:
		data_transfer = smbdirect_recv_io_payload(response);

		if (wc->byte_len <
		    offsetof(struct smbdirect_data_transfer, padding))
			goto error;

		remaining_data_length = le32_to_cpu(data_transfer->remaining_data_length);
		data_offset = le32_to_cpu(data_transfer->data_offset);
		data_length = le32_to_cpu(data_transfer->data_length);
		if (wc->byte_len < data_offset ||
		    (u64)wc->byte_len < (u64)data_offset + data_length)
			goto error;

		if (remaining_data_length > sp->max_fragmented_recv_size ||
		    data_length > sp->max_fragmented_recv_size ||
		    (u64)remaining_data_length + (u64)data_length > (u64)sp->max_fragmented_recv_size)
			goto error;

		if (data_length) {
			if (sc->recv_io.reassembly.full_packet_received)
				response->first_segment = true;

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
		if (le16_to_cpu(data_transfer->credits_granted)) {
			atomic_add(le16_to_cpu(data_transfer->credits_granted),
				&sc->send_io.credits.count);
			/*
			 * We have new send credits granted from remote peer
			 * If any sender is waiting for credits, unblock it
			 */
			wake_up(&sc->send_io.credits.wait_queue);
		}

		log_incoming(INFO, "data flags %d data_offset %d data_length %d remaining_data_length %d\n",
			     le16_to_cpu(data_transfer->flags),
			     le32_to_cpu(data_transfer->data_offset),
			     le32_to_cpu(data_transfer->data_length),
			     le32_to_cpu(data_transfer->remaining_data_length));

		/* Send an immediate response right away if requested */
		if (le16_to_cpu(data_transfer->flags) &
				SMBDIRECT_FLAG_RESPONSE_REQUESTED) {
			log_keep_alive(INFO, "schedule send of immediate response\n");
			queue_work(sc->workqueue, &sc->idle.immediate_work);
		}

		/*
		 * If this is a packet with data playload place the data in
		 * reassembly queue and wake up the reading thread
		 */
		if (data_length) {
			if (current_recv_credits <= (sc->recv_io.credits.target / 4) ||
			    sc->recv_io.credits.target > old_recv_credit_target)
				queue_work(sc->workqueue, &sc->recv_io.posted.refill_work);

			smbdirect_connection_reassembly_append_recv_io(sc, response, data_length);
			wake_up(&sc->recv_io.reassembly.wait_queue);
		} else
			smbdirect_connection_put_recv_io(response);

		return;

	case SMBDIRECT_EXPECT_NEGOTIATE_REQ:
		/* Only server... */
		break;
	}

	/*
	 * This is an internal error!
	 */
	log_rdma_recv(ERR, "unexpected response type=%d\n", sc->recv_io.expected);
	WARN_ON_ONCE(sc->recv_io.expected != SMBDIRECT_EXPECT_DATA_TRANSFER);
error:
	smbdirect_connection_put_recv_io(response);
	smbdirect_socket_schedule_cleanup(sc, -ECONNABORTED);
}

static struct rdma_cm_id *smbd_create_id(
		struct smbdirect_socket *sc,
		struct sockaddr *dstaddr, int port)
{
	struct smbdirect_socket_parameters *sp = &sc->parameters;
	struct rdma_cm_id *id;
	u8 node_type = RDMA_NODE_UNSPECIFIED;
	int rc;
	__be16 *sport;

	id = rdma_create_id(&init_net, smbd_conn_upcall, sc,
		RDMA_PS_TCP, IB_QPT_RC);
	if (IS_ERR(id)) {
		rc = PTR_ERR(id);
		log_rdma_event(ERR, "rdma_create_id() failed %i\n", rc);
		return id;
	}

	switch (port) {
	case SMBD_PORT:
		/*
		 * only allow iWarp devices
		 * for port 5445.
		 */
		node_type = RDMA_NODE_RNIC;
		break;
	case SMB_PORT:
		/*
		 * only allow InfiniBand, RoCEv1 or RoCEv2
		 * devices for port 445.
		 *
		 * (Basically don't allow iWarp devices)
		 */
		node_type = RDMA_NODE_IB_CA;
		break;
	}
	rc = rdma_restrict_node_type(id, node_type);
	if (rc) {
		log_rdma_event(ERR, "rdma_restrict_node_type(%u) failed %i\n",
			       node_type, rc);
		goto out;
	}

	if (dstaddr->sa_family == AF_INET6)
		sport = &((struct sockaddr_in6 *)dstaddr)->sin6_port;
	else
		sport = &((struct sockaddr_in *)dstaddr)->sin_port;

	*sport = htons(port);

	WARN_ON_ONCE(sc->status != SMBDIRECT_SOCKET_RESOLVE_ADDR_NEEDED);
	sc->status = SMBDIRECT_SOCKET_RESOLVE_ADDR_RUNNING;
	rc = rdma_resolve_addr(id, NULL, (struct sockaddr *)dstaddr,
		sp->resolve_addr_timeout_msec);
	if (rc) {
		log_rdma_event(ERR, "rdma_resolve_addr() failed %i\n", rc);
		goto out;
	}
	rc = wait_event_interruptible_timeout(
		sc->status_wait,
		sc->status != SMBDIRECT_SOCKET_RESOLVE_ADDR_RUNNING,
		msecs_to_jiffies(sp->resolve_addr_timeout_msec));
	/* e.g. if interrupted returns -ERESTARTSYS */
	if (rc < 0) {
		log_rdma_event(ERR, "rdma_resolve_addr timeout rc: %i\n", rc);
		goto out;
	}
	if (sc->status == SMBDIRECT_SOCKET_RESOLVE_ADDR_RUNNING) {
		rc = -ETIMEDOUT;
		log_rdma_event(ERR, "rdma_resolve_addr() completed %i\n", rc);
		goto out;
	}
	if (sc->status != SMBDIRECT_SOCKET_RESOLVE_ROUTE_NEEDED) {
		rc = -EHOSTUNREACH;
		log_rdma_event(ERR, "rdma_resolve_addr() completed %i\n", rc);
		goto out;
	}

	WARN_ON_ONCE(sc->status != SMBDIRECT_SOCKET_RESOLVE_ROUTE_NEEDED);
	sc->status = SMBDIRECT_SOCKET_RESOLVE_ROUTE_RUNNING;
	rc = rdma_resolve_route(id, sp->resolve_route_timeout_msec);
	if (rc) {
		log_rdma_event(ERR, "rdma_resolve_route() failed %i\n", rc);
		goto out;
	}
	rc = wait_event_interruptible_timeout(
		sc->status_wait,
		sc->status != SMBDIRECT_SOCKET_RESOLVE_ROUTE_RUNNING,
		msecs_to_jiffies(sp->resolve_route_timeout_msec));
	/* e.g. if interrupted returns -ERESTARTSYS */
	if (rc < 0)  {
		log_rdma_event(ERR, "rdma_resolve_addr timeout rc: %i\n", rc);
		goto out;
	}
	if (sc->status == SMBDIRECT_SOCKET_RESOLVE_ROUTE_RUNNING) {
		rc = -ETIMEDOUT;
		log_rdma_event(ERR, "rdma_resolve_route() completed %i\n", rc);
		goto out;
	}
	if (sc->status != SMBDIRECT_SOCKET_RDMA_CONNECT_NEEDED) {
		rc = -ENETUNREACH;
		log_rdma_event(ERR, "rdma_resolve_route() completed %i\n", rc);
		goto out;
	}

	return id;

out:
	rdma_destroy_id(id);
	return ERR_PTR(rc);
}

static int smbd_ia_open(
		struct smbdirect_socket *sc,
		struct sockaddr *dstaddr, int port)
{
	struct smbdirect_socket_parameters *sp = &sc->parameters;
	int rc;

	WARN_ON_ONCE(sc->status != SMBDIRECT_SOCKET_CREATED);
	sc->status = SMBDIRECT_SOCKET_RESOLVE_ADDR_NEEDED;

	sc->rdma.cm_id = smbd_create_id(sc, dstaddr, port);
	if (IS_ERR(sc->rdma.cm_id)) {
		rc = PTR_ERR(sc->rdma.cm_id);
		goto out1;
	}
	sc->ib.dev = sc->rdma.cm_id->device;

	if (!smbdirect_frwr_is_supported(&sc->ib.dev->attrs)) {
		log_rdma_event(ERR, "Fast Registration Work Requests (FRWR) is not supported\n");
		log_rdma_event(ERR, "Device capability flags = %llx max_fast_reg_page_list_len = %u\n",
			       sc->ib.dev->attrs.device_cap_flags,
			       sc->ib.dev->attrs.max_fast_reg_page_list_len);
		rc = -EPROTONOSUPPORT;
		goto out2;
	}
	sp->max_frmr_depth = min_t(u32,
		sp->max_frmr_depth,
		sc->ib.dev->attrs.max_fast_reg_page_list_len);
	sc->mr_io.type = IB_MR_TYPE_MEM_REG;
	if (sc->ib.dev->attrs.kernel_cap_flags & IBK_SG_GAPS_REG)
		sc->mr_io.type = IB_MR_TYPE_SG_GAPS;

	return 0;

out2:
	rdma_destroy_id(sc->rdma.cm_id);
	sc->rdma.cm_id = NULL;

out1:
	return rc;
}

/*
 * Send a negotiation request message to the peer
 * The negotiation procedure is in [MS-SMBD] 3.1.5.2 and 3.1.5.3
 * After negotiation, the transport is connected and ready for
 * carrying upper layer SMB payload
 */
static int smbd_post_send_negotiate_req(struct smbdirect_socket *sc)
{
	struct smbdirect_socket_parameters *sp = &sc->parameters;
	int rc;
	struct smbdirect_send_io *request;
	struct smbdirect_negotiate_req *packet;

	request = smbdirect_connection_alloc_send_io(sc);
	if (IS_ERR(request))
		return PTR_ERR(request);

	packet = smbdirect_send_io_payload(request);
	packet->min_version = cpu_to_le16(SMBDIRECT_V1);
	packet->max_version = cpu_to_le16(SMBDIRECT_V1);
	packet->reserved = 0;
	packet->credits_requested = cpu_to_le16(sp->send_credit_target);
	packet->preferred_send_size = cpu_to_le32(sp->max_send_size);
	packet->max_receive_size = cpu_to_le32(sp->max_recv_size);
	packet->max_fragmented_size =
		cpu_to_le32(sp->max_fragmented_recv_size);

	request->sge[0].addr = ib_dma_map_single(
				sc->ib.dev, (void *)packet,
				sizeof(*packet), DMA_TO_DEVICE);
	if (ib_dma_mapping_error(sc->ib.dev, request->sge[0].addr)) {
		rc = -EIO;
		goto dma_mapping_failed;
	}
	request->num_sge = 1;

	request->sge[0].length = sizeof(*packet);
	request->sge[0].lkey = sc->ib.pd->local_dma_lkey;
	request->num_sge = 1;

	rc = smbd_post_send(sc, NULL, request);
	if (!rc)
		return 0;

	if (rc == -EAGAIN)
		rc = -EIO;

dma_mapping_failed:
	smbdirect_connection_free_send_io(request);
	return rc;
}

/*
 * Extend the credits to remote peer
 * This implements [MS-SMBD] 3.1.5.9
 * The idea is that we should extend credits to remote peer as quickly as
 * it's allowed, to maintain data flow. We allocate as much receive
 * buffer as possible, and extend the receive credits to remote peer
 * return value: the new credtis being granted.
 */
static int manage_credits_prior_sending(struct smbdirect_socket *sc)
{
	int missing;
	int available;
	int new_credits;

	if (atomic_read(&sc->recv_io.credits.count) >= sc->recv_io.credits.target)
		return 0;

	missing = (int)sc->recv_io.credits.target - atomic_read(&sc->recv_io.credits.count);
	available = atomic_xchg(&sc->recv_io.credits.available, 0);
	new_credits = (u16)min3(U16_MAX, missing, available);
	if (new_credits <= 0) {
		/*
		 * If credits are available, but not granted
		 * we need to re-add them again.
		 */
		if (available)
			atomic_add(available, &sc->recv_io.credits.available);
		return 0;
	}

	if (new_credits < available) {
		/*
		 * Readd the remaining available again.
		 */
		available -= new_credits;
		atomic_add(available, &sc->recv_io.credits.available);
	}

	/*
	 * Remember we granted the credits
	 */
	atomic_add(new_credits, &sc->recv_io.credits.count);
	return new_credits;
}

/*
 * Check if we need to send a KEEP_ALIVE message
 * The idle connection timer triggers a KEEP_ALIVE message when expires
 * SMBDIRECT_FLAG_RESPONSE_REQUESTED is set in the message flag to have peer send
 * back a response.
 * return value:
 * 1 if SMBDIRECT_FLAG_RESPONSE_REQUESTED needs to be set
 * 0: otherwise
 */
static int manage_keep_alive_before_sending(struct smbdirect_socket *sc)
{
	struct smbdirect_socket_parameters *sp = &sc->parameters;

	if (sc->idle.keepalive == SMBDIRECT_KEEPALIVE_PENDING) {
		sc->idle.keepalive = SMBDIRECT_KEEPALIVE_SENT;
		/*
		 * Now use the keepalive timeout (instead of keepalive interval)
		 * in order to wait for a response
		 */
		mod_delayed_work(sc->workqueue, &sc->idle.timer_work,
				 msecs_to_jiffies(sp->keepalive_timeout_msec));
		return 1;
	}
	return 0;
}

static int smbd_ib_post_send(struct smbdirect_socket *sc,
			     struct ib_send_wr *wr)
{
	int ret;

	atomic_inc(&sc->send_io.pending.count);
	ret = ib_post_send(sc->ib.qp, wr, NULL);
	if (ret) {
		pr_err("failed to post send: %d\n", ret);
		smbdirect_socket_schedule_cleanup(sc, ret);
		ret = -EAGAIN;
	}
	return ret;
}

/* Post the send request */
static int smbd_post_send(struct smbdirect_socket *sc,
			  struct smbdirect_send_batch *batch,
			  struct smbdirect_send_io *request)
{
	int i;

	for (i = 0; i < request->num_sge; i++) {
		log_rdma_send(INFO,
			"rdma_request sge[%d] addr=0x%llx length=%u\n",
			i, request->sge[i].addr, request->sge[i].length);
		ib_dma_sync_single_for_device(
			sc->ib.dev,
			request->sge[i].addr,
			request->sge[i].length,
			DMA_TO_DEVICE);
	}

	request->cqe.done = smbdirect_connection_send_io_done;
	request->wr.next = NULL;
	request->wr.sg_list = request->sge;
	request->wr.num_sge = request->num_sge;
	request->wr.opcode = IB_WR_SEND;

	if (batch) {
		request->wr.wr_cqe = NULL;
		request->wr.send_flags = 0;
		if (!list_empty(&batch->msg_list)) {
			struct smbdirect_send_io *last;

			last = list_last_entry(&batch->msg_list,
					       struct smbdirect_send_io,
					       sibling_list);
			last->wr.next = &request->wr;
		}
		list_add_tail(&request->sibling_list, &batch->msg_list);
		batch->wr_cnt++;
		return 0;
	}

	request->wr.wr_cqe = &request->cqe;
	request->wr.send_flags = IB_SEND_SIGNALED;
	return smbd_ib_post_send(sc, &request->wr);
}

static void smbd_send_batch_init(struct smbdirect_send_batch *batch,
				 bool need_invalidate_rkey,
				 unsigned int remote_key)
{
	INIT_LIST_HEAD(&batch->msg_list);
	batch->wr_cnt = 0;
	batch->need_invalidate_rkey = need_invalidate_rkey;
	batch->remote_key = remote_key;
	batch->credit = 0;
}

static int smbd_send_batch_flush(struct smbdirect_socket *sc,
				 struct smbdirect_send_batch *batch,
				 bool is_last)
{
	struct smbdirect_send_io *first, *last;
	int ret = 0;

	if (list_empty(&batch->msg_list))
		goto release_credit;

	first = list_first_entry(&batch->msg_list,
				 struct smbdirect_send_io,
				 sibling_list);
	last = list_last_entry(&batch->msg_list,
			       struct smbdirect_send_io,
			       sibling_list);

	if (batch->need_invalidate_rkey) {
		first->wr.opcode = IB_WR_SEND_WITH_INV;
		first->wr.ex.invalidate_rkey = batch->remote_key;
		batch->need_invalidate_rkey = false;
		batch->remote_key = 0;
	}

	last->wr.send_flags = IB_SEND_SIGNALED;
	last->wr.wr_cqe = &last->cqe;

	/*
	 * Remove last from batch->msg_list
	 * and splice the rest of batch->msg_list
	 * to last->sibling_list.
	 *
	 * batch->msg_list is a valid empty list
	 * at the end.
	 */
	list_del_init(&last->sibling_list);
	list_splice_tail_init(&batch->msg_list, &last->sibling_list);
	batch->wr_cnt = 0;

	ret = smbd_ib_post_send(sc, &first->wr);
	if (ret) {
		struct smbdirect_send_io *sibling, *next;

		list_for_each_entry_safe(sibling, next, &last->sibling_list, sibling_list) {
			list_del_init(&sibling->sibling_list);
			smbdirect_connection_free_send_io(sibling);
		}
		smbdirect_connection_free_send_io(last);
	}

release_credit:
	if (is_last && !ret && batch->credit) {
		atomic_add(batch->credit, &sc->send_io.bcredits.count);
		batch->credit = 0;
		wake_up(&sc->send_io.bcredits.wait_queue);
	}

	return ret;
}

static int wait_for_credits(struct smbdirect_socket *sc,
			    wait_queue_head_t *waitq, atomic_t *total_credits,
			    int needed)
{
	int ret;

	do {
		if (atomic_sub_return(needed, total_credits) >= 0)
			return 0;

		atomic_add(needed, total_credits);
		ret = wait_event_interruptible(*waitq,
					       atomic_read(total_credits) >= needed ||
					       sc->status != SMBDIRECT_SOCKET_CONNECTED);

		if (sc->status != SMBDIRECT_SOCKET_CONNECTED)
			return -ENOTCONN;
		else if (ret < 0)
			return ret;
	} while (true);
}

static int wait_for_send_bcredit(struct smbdirect_socket *sc,
				 struct smbdirect_send_batch *batch)
{
	int ret;

	if (batch->credit)
		return 0;

	ret = wait_for_credits(sc,
			       &sc->send_io.bcredits.wait_queue,
			       &sc->send_io.bcredits.count,
			       1);
	if (ret)
		return ret;

	batch->credit = 1;
	return 0;
}

static int wait_for_send_lcredit(struct smbdirect_socket *sc,
				 struct smbdirect_send_batch *batch)
{
	if (batch && (atomic_read(&sc->send_io.lcredits.count) <= 1)) {
		int ret;

		ret = smbd_send_batch_flush(sc, batch, false);
		if (ret)
			return ret;
	}

	return wait_for_credits(sc,
				&sc->send_io.lcredits.wait_queue,
				&sc->send_io.lcredits.count,
				1);
}

static int wait_for_send_credits(struct smbdirect_socket *sc,
				 struct smbdirect_send_batch *batch)
{
	if (batch &&
	    (batch->wr_cnt >= 16 || atomic_read(&sc->send_io.credits.count) <= 1)) {
		int ret;

		ret = smbd_send_batch_flush(sc, batch, false);
		if (ret)
			return ret;
	}

	return wait_for_credits(sc,
				&sc->send_io.credits.wait_queue,
				&sc->send_io.credits.count,
				1);
}

static int smbd_post_send_iter(struct smbdirect_socket *sc,
			       struct smbdirect_send_batch *batch,
			       struct iov_iter *iter,
			       int *_remaining_data_length)
{
	struct smbdirect_socket_parameters *sp = &sc->parameters;
	int rc;
	int header_length;
	int data_length;
	struct smbdirect_send_io *request;
	struct smbdirect_data_transfer *packet;
	int new_credits = 0;
	struct smbdirect_send_batch _batch;

	if (!batch) {
		smbd_send_batch_init(&_batch, false, 0);
		batch = &_batch;
	}

	rc = wait_for_send_bcredit(sc, batch);
	if (rc) {
		log_outgoing(ERR, "disconnected not sending on wait_bcredit\n");
		rc = -EAGAIN;
		goto err_wait_bcredit;
	}

	rc = wait_for_send_lcredit(sc, batch);
	if (rc) {
		log_outgoing(ERR, "disconnected not sending on wait_lcredit\n");
		rc = -EAGAIN;
		goto err_wait_lcredit;
	}

	rc = wait_for_send_credits(sc, batch);
	if (rc) {
		log_outgoing(ERR, "disconnected not sending on wait_credit\n");
		rc = -EAGAIN;
		goto err_wait_credit;
	}

	new_credits = manage_credits_prior_sending(sc);
	if (new_credits == 0 &&
	    atomic_read(&sc->send_io.credits.count) == 0 &&
	    atomic_read(&sc->recv_io.credits.count) == 0) {
		queue_work(sc->workqueue, &sc->recv_io.posted.refill_work);
		rc = wait_event_interruptible(sc->send_io.credits.wait_queue,
					      atomic_read(&sc->send_io.credits.count) >= 1 ||
					      atomic_read(&sc->recv_io.credits.available) >= 1 ||
					      sc->status != SMBDIRECT_SOCKET_CONNECTED);
		if (sc->status != SMBDIRECT_SOCKET_CONNECTED)
			rc = -ENOTCONN;
		if (rc < 0) {
			log_outgoing(ERR, "disconnected not sending on last credit\n");
			rc = -EAGAIN;
			goto err_wait_credit;
		}

		new_credits = manage_credits_prior_sending(sc);
	}

	request = smbdirect_connection_alloc_send_io(sc);
	if (IS_ERR(request)) {
		rc = PTR_ERR(request);
		goto err_alloc;
	}

	memset(request->sge, 0, sizeof(request->sge));

	/* Map the packet to DMA */
	header_length = sizeof(struct smbdirect_data_transfer);
	/* If this is a packet without payload, don't send padding */
	if (!iter)
		header_length = offsetof(struct smbdirect_data_transfer, padding);

	packet = smbdirect_send_io_payload(request);
	request->sge[0].addr = ib_dma_map_single(sc->ib.dev,
						 (void *)packet,
						 header_length,
						 DMA_TO_DEVICE);
	if (ib_dma_mapping_error(sc->ib.dev, request->sge[0].addr)) {
		rc = -EIO;
		goto err_dma;
	}

	request->sge[0].length = header_length;
	request->sge[0].lkey = sc->ib.pd->local_dma_lkey;
	request->num_sge = 1;

	/* Fill in the data payload to find out how much data we can add */
	if (iter) {
		struct smbdirect_map_sges extract = {
			.num_sge	= request->num_sge,
			.max_sge	= ARRAY_SIZE(request->sge),
			.sge		= request->sge,
			.device		= sc->ib.dev,
			.local_dma_lkey	= sc->ib.pd->local_dma_lkey,
			.direction	= DMA_TO_DEVICE,
		};
		size_t payload_len = umin(*_remaining_data_length,
					  sp->max_send_size - sizeof(*packet));

		rc = smbdirect_map_sges_from_iter(iter, payload_len, &extract);
		if (rc < 0)
			goto err_dma;
		data_length = rc;
		request->num_sge = extract.num_sge;
		*_remaining_data_length -= data_length;
	} else {
		data_length = 0;
	}

	/* Fill in the packet header */
	packet->credits_requested = cpu_to_le16(sp->send_credit_target);
	packet->credits_granted = cpu_to_le16(new_credits);

	packet->flags = 0;
	if (manage_keep_alive_before_sending(sc))
		packet->flags |= cpu_to_le16(SMBDIRECT_FLAG_RESPONSE_REQUESTED);

	packet->reserved = 0;
	if (!data_length)
		packet->data_offset = 0;
	else
		packet->data_offset = cpu_to_le32(24);
	packet->data_length = cpu_to_le32(data_length);
	packet->remaining_data_length = cpu_to_le32(*_remaining_data_length);
	packet->padding = 0;

	log_outgoing(INFO, "credits_requested=%d credits_granted=%d data_offset=%d data_length=%d remaining_data_length=%d\n",
		     le16_to_cpu(packet->credits_requested),
		     le16_to_cpu(packet->credits_granted),
		     le32_to_cpu(packet->data_offset),
		     le32_to_cpu(packet->data_length),
		     le32_to_cpu(packet->remaining_data_length));

	rc = smbd_post_send(sc, batch, request);
	if (!rc) {
		/*
		 * From here request is moved to batch
		 * and we should not free it explicitly.
		 */

		if (batch != &_batch)
			return 0;

		rc = smbd_send_batch_flush(sc, batch, true);
		if (!rc)
			return 0;

		goto err_flush;
	}

err_dma:
	smbdirect_connection_free_send_io(request);

err_flush:
err_alloc:
	atomic_inc(&sc->send_io.credits.count);
	wake_up(&sc->send_io.credits.wait_queue);

err_wait_credit:
	atomic_inc(&sc->send_io.lcredits.count);
	wake_up(&sc->send_io.lcredits.wait_queue);

err_wait_lcredit:
	atomic_add(batch->credit, &sc->send_io.bcredits.count);
	batch->credit = 0;
	wake_up(&sc->send_io.bcredits.wait_queue);

err_wait_bcredit:
	return rc;
}

/*
 * Send an empty message
 * Empty message is used to extend credits to peer to for keep live
 * while there is no upper layer payload to send at the time
 */
static int smbd_post_send_empty(struct smbdirect_socket *sc)
{
	int remaining_data_length = 0;

	sc->statistics.send_empty++;
	return smbd_post_send_iter(sc, NULL, NULL, &remaining_data_length);
}

static int smbd_post_send_full_iter(struct smbdirect_socket *sc,
				    struct smbdirect_send_batch *batch,
				    struct iov_iter *iter,
				    int *_remaining_data_length)
{
	int rc = 0;

	/*
	 * smbd_post_send_iter() respects the
	 * negotiated max_send_size, so we need to
	 * loop until the full iter is posted
	 */

	while (iov_iter_count(iter) > 0) {
		rc = smbd_post_send_iter(sc, batch, iter, _remaining_data_length);
		if (rc < 0)
			break;
	}

	return rc;
}

/* Perform SMBD negotiate according to [MS-SMBD] 3.1.5.2 */
static int smbd_negotiate(struct smbdirect_socket *sc)
{
	struct smbdirect_socket_parameters *sp = &sc->parameters;
	int rc;
	struct smbdirect_recv_io *response = smbdirect_connection_get_recv_io(sc);

	WARN_ON_ONCE(sc->status != SMBDIRECT_SOCKET_NEGOTIATE_NEEDED);
	sc->status = SMBDIRECT_SOCKET_NEGOTIATE_RUNNING;

	sc->recv_io.expected = SMBDIRECT_EXPECT_NEGOTIATE_REP;
	rc = smbdirect_connection_post_recv_io(response);
	log_rdma_event(INFO, "smbd_post_recv rc=%d iov.addr=0x%llx iov.length=%u iov.lkey=0x%x\n",
		       rc, response->sge.addr,
		       response->sge.length, response->sge.lkey);
	if (rc) {
		smbdirect_connection_put_recv_io(response);
		return rc;
	}

	rc = smbd_post_send_negotiate_req(sc);
	if (rc)
		return rc;

	rc = wait_event_interruptible_timeout(
		sc->status_wait,
		sc->status != SMBDIRECT_SOCKET_NEGOTIATE_RUNNING,
		msecs_to_jiffies(sp->negotiate_timeout_msec));
	log_rdma_event(INFO, "wait_event_interruptible_timeout rc=%d\n", rc);

	if (sc->status == SMBDIRECT_SOCKET_CONNECTED)
		return 0;

	if (rc == 0)
		rc = -ETIMEDOUT;
	else if (rc == -ERESTARTSYS)
		rc = -EINTR;
	else
		rc = -ENOTCONN;

	return rc;
}

static void send_immediate_empty_message(struct work_struct *work)
{
	struct smbdirect_socket *sc =
		container_of(work, struct smbdirect_socket, idle.immediate_work);

	if (sc->status != SMBDIRECT_SOCKET_CONNECTED)
		return;

	log_keep_alive(INFO, "send an empty message\n");
	smbd_post_send_empty(sc);
}

/*
 * Destroy the transport and related RDMA and memory resources
 * Need to go through all the pending counters and make sure on one is using
 * the transport while it is destroyed
 */
void smbd_destroy(struct TCP_Server_Info *server)
{
	struct smbd_connection *info = server->smbd_conn;
	struct smbdirect_socket *sc;
	struct smbdirect_recv_io *response;
	unsigned long flags;

	if (!info) {
		log_rdma_event(INFO, "rdma session already destroyed\n");
		return;
	}
	sc = &info->socket;

	log_rdma_event(INFO, "cancelling and disable disconnect_work\n");
	disable_work_sync(&sc->disconnect_work);

	log_rdma_event(INFO, "destroying rdma session\n");
	if (sc->status < SMBDIRECT_SOCKET_DISCONNECTING)
		smbdirect_socket_cleanup_work(&sc->disconnect_work);
	if (sc->status < SMBDIRECT_SOCKET_DISCONNECTED) {
		log_rdma_event(INFO, "wait for transport being disconnected\n");
		wait_event(sc->status_wait, sc->status == SMBDIRECT_SOCKET_DISCONNECTED);
		log_rdma_event(INFO, "waited for transport being disconnected\n");
	}

	/*
	 * Wake up all waiters in all wait queues
	 * in order to notice the broken connection.
	 *
	 * Most likely this was already called via
	 * smbdirect_socket_cleanup_work(), but call it again...
	 */
	smbdirect_socket_wake_up_all(sc);

	log_rdma_event(INFO, "cancelling recv_io.posted.refill_work\n");
	disable_work_sync(&sc->recv_io.posted.refill_work);

	log_rdma_event(INFO, "drain qp\n");
	ib_drain_qp(sc->ib.qp);

	log_rdma_event(INFO, "cancelling idle timer\n");
	disable_delayed_work_sync(&sc->idle.timer_work);
	log_rdma_event(INFO, "cancelling send immediate work\n");
	disable_work_sync(&sc->idle.immediate_work);

	/* It's not possible for upper layer to get to reassembly */
	log_rdma_event(INFO, "drain the reassembly queue\n");
	do {
		spin_lock_irqsave(&sc->recv_io.reassembly.lock, flags);
		response = smbdirect_connection_reassembly_first_recv_io(sc);
		if (response) {
			list_del(&response->list);
			spin_unlock_irqrestore(
				&sc->recv_io.reassembly.lock, flags);
			smbdirect_connection_put_recv_io(response);
		} else
			spin_unlock_irqrestore(
				&sc->recv_io.reassembly.lock, flags);
	} while (response);
	sc->recv_io.reassembly.data_length = 0;

	log_rdma_event(INFO, "freeing mr list\n");
	smbdirect_connection_destroy_mr_list(sc);

	log_rdma_event(INFO, "destroying qp\n");
	smbdirect_connection_destroy_qp(sc);
	rdma_destroy_id(sc->rdma.cm_id);

	/* free mempools */
	smbdirect_connection_destroy_mem_pools(sc);

	sc->status = SMBDIRECT_SOCKET_DESTROYED;

	destroy_workqueue(sc->workqueue);
	log_rdma_event(INFO,  "rdma session destroyed\n");
	kfree(info);
	server->smbd_conn = NULL;
}

/*
 * Reconnect this SMBD connection, called from upper layer
 * return value: 0 on success, or actual error code
 */
int smbd_reconnect(struct TCP_Server_Info *server)
{
	log_rdma_event(INFO, "reconnecting rdma session\n");

	if (!server->smbd_conn) {
		log_rdma_event(INFO, "rdma session already destroyed\n");
		goto create_conn;
	}

	/*
	 * This is possible if transport is disconnected and we haven't received
	 * notification from RDMA, but upper layer has detected timeout
	 */
	if (server->smbd_conn->socket.status == SMBDIRECT_SOCKET_CONNECTED) {
		log_rdma_event(INFO, "disconnecting transport\n");
		smbd_destroy(server);
	}

create_conn:
	log_rdma_event(INFO, "creating rdma session\n");
	server->smbd_conn = smbd_get_connection(
		server, (struct sockaddr *) &server->dstaddr);

	if (server->smbd_conn) {
		cifs_dbg(VFS, "RDMA transport re-established\n");
		trace_smb3_smbd_connect_done(server->hostname, server->conn_id, &server->dstaddr);
		return 0;
	}
	trace_smb3_smbd_connect_err(server->hostname, server->conn_id, &server->dstaddr);
	return -ENOENT;
}

/* Create a SMBD connection, called by upper layer */
static struct smbd_connection *_smbd_get_connection(
	struct TCP_Server_Info *server, struct sockaddr *dstaddr, int port)
{
	int rc;
	struct smbd_connection *info;
	struct smbdirect_socket *sc;
	struct smbdirect_socket_parameters init_params = {};
	struct smbdirect_socket_parameters *sp;
	struct rdma_conn_param conn_param;
	struct sockaddr_in *addr_in = (struct sockaddr_in *) dstaddr;
	struct ib_port_immutable port_immutable;
	__be32 ird_ord_hdr[2];
	char wq_name[80];
	struct workqueue_struct *workqueue;
	struct smbdirect_recv_io *recv_io;

	/*
	 * Create the initial parameters
	 */
	sp = &init_params;
	sp->resolve_addr_timeout_msec = RDMA_RESOLVE_TIMEOUT;
	sp->resolve_route_timeout_msec = RDMA_RESOLVE_TIMEOUT;
	sp->rdma_connect_timeout_msec = RDMA_RESOLVE_TIMEOUT;
	sp->negotiate_timeout_msec = SMBD_NEGOTIATE_TIMEOUT * 1000;
	sp->initiator_depth = 1;
	sp->responder_resources = SMBD_CM_RESPONDER_RESOURCES;
	sp->recv_credit_max = smbd_receive_credit_max;
	sp->send_credit_target = smbd_send_credit_target;
	sp->max_send_size = smbd_max_send_size;
	sp->max_fragmented_recv_size = smbd_max_fragmented_recv_size;
	sp->max_recv_size = smbd_max_receive_size;
	sp->max_frmr_depth = smbd_max_frmr_depth;
	sp->keepalive_interval_msec = smbd_keep_alive_interval * 1000;
	sp->keepalive_timeout_msec = KEEPALIVE_RECV_TIMEOUT * 1000;

	info = kzalloc_obj(*info);
	if (!info)
		return NULL;
	sc = &info->socket;
	scnprintf(wq_name, ARRAY_SIZE(wq_name), "smbd_%p", sc);
	workqueue = create_workqueue(wq_name);
	if (!workqueue)
		goto create_wq_failed;
	smbdirect_socket_prepare_create(sc, sp, workqueue);
	smbdirect_socket_set_logging(sc, NULL, smbd_logging_needed, smbd_logging_vaprintf);
	sc->ib.poll_ctx = IB_POLL_SOFTIRQ;
	/*
	 * from here we operate on the copy.
	 */
	sp = &sc->parameters;

	rc = smbd_ia_open(sc, dstaddr, port);
	if (rc) {
		log_rdma_event(INFO, "smbd_ia_open rc=%d\n", rc);
		goto create_id_failed;
	}

	sp->responder_resources =
		min_t(u8, sp->responder_resources,
		      sc->ib.dev->attrs.max_qp_rd_atom);
	log_rdma_mr(INFO, "responder_resources=%d\n",
		sp->responder_resources);

	rc = smbdirect_connection_create_qp(sc);
	if (rc) {
		log_rdma_event(ERR, "smbdirect_connection_create_qp failed %i\n", rc);
		goto create_qp_failed;
	}

	memset(&conn_param, 0, sizeof(conn_param));
	conn_param.initiator_depth = sp->initiator_depth;
	conn_param.responder_resources = sp->responder_resources;

	/* Need to send IRD/ORD in private data for iWARP */
	sc->ib.dev->ops.get_port_immutable(
		sc->ib.dev, sc->rdma.cm_id->port_num, &port_immutable);
	if (port_immutable.core_cap_flags & RDMA_CORE_PORT_IWARP) {
		ird_ord_hdr[0] = cpu_to_be32(conn_param.responder_resources);
		ird_ord_hdr[1] = cpu_to_be32(conn_param.initiator_depth);
		conn_param.private_data = ird_ord_hdr;
		conn_param.private_data_len = sizeof(ird_ord_hdr);
	} else {
		conn_param.private_data = NULL;
		conn_param.private_data_len = 0;
	}

	conn_param.retry_count = SMBD_CM_RETRY;
	conn_param.rnr_retry_count = SMBD_CM_RNR_RETRY;
	conn_param.flow_control = 0;

	log_rdma_event(INFO, "connecting to IP %pI4 port %d\n",
		&addr_in->sin_addr, port);

	WARN_ON_ONCE(sc->status != SMBDIRECT_SOCKET_RDMA_CONNECT_NEEDED);
	sc->status = SMBDIRECT_SOCKET_RDMA_CONNECT_RUNNING;
	rc = rdma_connect(sc->rdma.cm_id, &conn_param);
	if (rc) {
		log_rdma_event(ERR, "rdma_connect() failed with %i\n", rc);
		goto rdma_connect_failed;
	}

	wait_event_interruptible_timeout(
		sc->status_wait,
		sc->status != SMBDIRECT_SOCKET_RDMA_CONNECT_RUNNING,
		msecs_to_jiffies(sp->rdma_connect_timeout_msec));

	if (sc->status != SMBDIRECT_SOCKET_NEGOTIATE_NEEDED) {
		log_rdma_event(ERR, "rdma_connect failed port=%d\n", port);
		goto rdma_connect_failed;
	}

	log_rdma_event(INFO, "rdma_connect connected\n");

	rc = smbdirect_connection_create_mem_pools(sc);
	if (rc) {
		log_rdma_event(ERR, "cache allocation failed\n");
		goto allocate_cache_failed;
	}

	list_for_each_entry(recv_io, &sc->recv_io.free.list, list)
		recv_io->cqe.done = recv_done;

	INIT_WORK(&sc->idle.immediate_work, send_immediate_empty_message);
	/*
	 * start with the negotiate timeout and SMBDIRECT_KEEPALIVE_PENDING
	 * so that the timer will cause a disconnect.
	 */
	sc->idle.keepalive = SMBDIRECT_KEEPALIVE_PENDING;
	mod_delayed_work(sc->workqueue, &sc->idle.timer_work,
			 msecs_to_jiffies(sp->negotiate_timeout_msec));

	INIT_WORK(&sc->recv_io.posted.refill_work, smbdirect_connection_recv_io_refill_work);

	rc = smbd_negotiate(sc);
	if (rc) {
		log_rdma_event(ERR, "smbd_negotiate rc=%d\n", rc);
		goto negotiation_failed;
	}

	rc = smbdirect_connection_create_mr_list(sc);
	if (rc) {
		log_rdma_mr(ERR, "memory registration allocation failed\n");
		goto allocate_mr_failed;
	}

	return info;

allocate_mr_failed:
	/* At this point, need to a full transport shutdown */
	server->smbd_conn = info;
	smbd_destroy(server);
	return NULL;

negotiation_failed:
	disable_delayed_work_sync(&sc->idle.timer_work);
	smbdirect_connection_destroy_mem_pools(sc);
	sc->status = SMBDIRECT_SOCKET_NEGOTIATE_FAILED;
	rdma_disconnect(sc->rdma.cm_id);
	wait_event(sc->status_wait,
		sc->status == SMBDIRECT_SOCKET_DISCONNECTED);

allocate_cache_failed:
rdma_connect_failed:
	smbdirect_connection_destroy_qp(sc);

create_qp_failed:
	rdma_destroy_id(sc->rdma.cm_id);

create_id_failed:
	destroy_workqueue(sc->workqueue);
create_wq_failed:
	kfree(info);
	return NULL;
}

struct smbd_connection *smbd_get_connection(
	struct TCP_Server_Info *server, struct sockaddr *dstaddr)
{
	struct smbd_connection *ret;
	const struct smbdirect_socket_parameters *sp;
	int port = SMBD_PORT;

try_again:
	ret = _smbd_get_connection(server, dstaddr, port);

	/* Try SMB_PORT if SMBD_PORT doesn't work */
	if (!ret && port == SMBD_PORT) {
		port = SMB_PORT;
		goto try_again;
	}
	if (!ret)
		return NULL;

	sp = &ret->socket.parameters;

	server->rdma_readwrite_threshold =
		rdma_readwrite_threshold > sp->max_fragmented_send_size ?
		sp->max_fragmented_send_size :
		rdma_readwrite_threshold;

	return ret;
}

/*
 * Receive data from the transport's receive reassembly queue
 * All the incoming data packets are placed in reassembly queue
 * iter: the buffer to read data into
 * size: the length of data to read
 * return value: actual data read
 *
 * Note: this implementation copies the data from reassembly queue to receive
 * buffers used by upper layer. This is not the optimal code path. A better way
 * to do it is to not have upper layer allocate its receive buffers but rather
 * borrow the buffer from reassembly queue, and return it after data is
 * consumed. But this will require more changes to upper layer code, and also
 * need to consider packet boundaries while they still being reassembled.
 */
int smbd_recv(struct smbd_connection *info, struct msghdr *msg)
{
	struct smbdirect_socket *sc = &info->socket;
	struct smbdirect_recv_io *response;
	struct smbdirect_data_transfer *data_transfer;
	size_t size = iov_iter_count(&msg->msg_iter);
	int to_copy, to_read, data_read, offset;
	u32 data_length, remaining_data_length, data_offset;
	int rc;

	if (WARN_ON_ONCE(iov_iter_rw(&msg->msg_iter) == WRITE))
		return -EINVAL; /* It's a bug in upper layer to get there */

again:
	/*
	 * No need to hold the reassembly queue lock all the time as we are
	 * the only one reading from the front of the queue. The transport
	 * may add more entries to the back of the queue at the same time
	 */
	log_read(INFO, "size=%zd sc->recv_io.reassembly.data_length=%d\n", size,
		sc->recv_io.reassembly.data_length);
	if (sc->recv_io.reassembly.data_length >= size) {
		int queue_length;
		int queue_removed = 0;
		unsigned long flags;

		/*
		 * Need to make sure reassembly_data_length is read before
		 * reading reassembly_queue_length and calling
		 * smbdirect_connection_reassembly_first_recv_io. This call is lock free
		 * as we never read at the end of the queue which are being
		 * updated in SOFTIRQ as more data is received
		 */
		virt_rmb();
		queue_length = sc->recv_io.reassembly.queue_length;
		data_read = 0;
		to_read = size;
		offset = sc->recv_io.reassembly.first_entry_offset;
		while (data_read < size) {
			response = smbdirect_connection_reassembly_first_recv_io(sc);
			data_transfer = smbdirect_recv_io_payload(response);
			data_length = le32_to_cpu(data_transfer->data_length);
			remaining_data_length =
				le32_to_cpu(
					data_transfer->remaining_data_length);
			data_offset = le32_to_cpu(data_transfer->data_offset);

			/*
			 * The upper layer expects RFC1002 length at the
			 * beginning of the payload. Return it to indicate
			 * the total length of the packet. This minimize the
			 * change to upper layer packet processing logic. This
			 * will be eventually remove when an intermediate
			 * transport layer is added
			 */
			if (response->first_segment && size == 4) {
				unsigned int rfc1002_len =
					data_length + remaining_data_length;
				__be32 rfc1002_hdr = cpu_to_be32(rfc1002_len);
				if (copy_to_iter(&rfc1002_hdr, sizeof(rfc1002_hdr),
						 &msg->msg_iter) != sizeof(rfc1002_hdr))
					return -EFAULT;
				data_read = 4;
				response->first_segment = false;
				log_read(INFO, "returning rfc1002 length %d\n",
					rfc1002_len);
				goto read_rfc1002_done;
			}

			to_copy = min_t(int, data_length - offset, to_read);
			if (copy_to_iter((char *)data_transfer + data_offset + offset,
					 to_copy, &msg->msg_iter) != to_copy)
				return -EFAULT;

			/* move on to the next buffer? */
			if (to_copy == data_length - offset) {
				queue_length--;
				/*
				 * No need to lock if we are not at the
				 * end of the queue
				 */
				if (queue_length)
					list_del(&response->list);
				else {
					spin_lock_irqsave(
						&sc->recv_io.reassembly.lock, flags);
					list_del(&response->list);
					spin_unlock_irqrestore(
						&sc->recv_io.reassembly.lock, flags);
				}
				queue_removed++;
				sc->statistics.dequeue_reassembly_queue++;
				smbdirect_connection_put_recv_io(response);
				offset = 0;
				log_read(INFO, "smbdirect_connection_put_recv_io offset=0\n");
			} else
				offset += to_copy;

			to_read -= to_copy;
			data_read += to_copy;

			log_read(INFO, "_get_first_reassembly memcpy %d bytes data_transfer_length-offset=%d after that to_read=%d data_read=%d offset=%d\n",
				 to_copy, data_length - offset,
				 to_read, data_read, offset);
		}

		spin_lock_irqsave(&sc->recv_io.reassembly.lock, flags);
		sc->recv_io.reassembly.data_length -= data_read;
		sc->recv_io.reassembly.queue_length -= queue_removed;
		spin_unlock_irqrestore(&sc->recv_io.reassembly.lock, flags);

		sc->recv_io.reassembly.first_entry_offset = offset;
		log_read(INFO, "returning to thread data_read=%d reassembly_data_length=%d first_entry_offset=%d\n",
			 data_read, sc->recv_io.reassembly.data_length,
			 sc->recv_io.reassembly.first_entry_offset);
read_rfc1002_done:
		return data_read;
	}

	log_read(INFO, "wait_event on more data\n");
	rc = wait_event_interruptible(
		sc->recv_io.reassembly.wait_queue,
		sc->recv_io.reassembly.data_length >= size ||
			sc->status != SMBDIRECT_SOCKET_CONNECTED);
	/* Don't return any data if interrupted */
	if (rc)
		return rc;

	if (sc->status != SMBDIRECT_SOCKET_CONNECTED) {
		log_read(ERR, "disconnected\n");
		return -ECONNABORTED;
	}

	goto again;
}

/*
 * Send data to transport
 * Each rqst is transported as a SMBDirect payload
 * rqst: the data to write
 * return value: 0 if successfully write, otherwise error code
 */
int smbd_send(struct TCP_Server_Info *server,
	int num_rqst, struct smb_rqst *rqst_array)
{
	struct smbd_connection *info = server->smbd_conn;
	struct smbdirect_socket *sc = &info->socket;
	struct smbdirect_socket_parameters *sp = &sc->parameters;
	struct smb_rqst *rqst;
	struct iov_iter iter;
	struct smbdirect_send_batch batch;
	unsigned int remaining_data_length, klen;
	int rc, i, rqst_idx;
	int error = 0;

	if (sc->status != SMBDIRECT_SOCKET_CONNECTED)
		return -EAGAIN;

	/*
	 * Add in the page array if there is one. The caller needs to set
	 * rq_tailsz to PAGE_SIZE when the buffer has multiple pages and
	 * ends at page boundary
	 */
	remaining_data_length = 0;
	for (i = 0; i < num_rqst; i++)
		remaining_data_length += smb_rqst_len(server, &rqst_array[i]);

	if (unlikely(remaining_data_length > sp->max_fragmented_send_size)) {
		/* assertion: payload never exceeds negotiated maximum */
		log_write(ERR, "payload size %d > max size %d\n",
			remaining_data_length, sp->max_fragmented_send_size);
		return -EINVAL;
	}

	log_write(INFO, "num_rqst=%d total length=%u\n",
			num_rqst, remaining_data_length);

	rqst_idx = 0;
	smbd_send_batch_init(&batch, false, 0);
	do {
		rqst = &rqst_array[rqst_idx];

		cifs_dbg(FYI, "Sending smb (RDMA): idx=%d smb_len=%lu\n",
			 rqst_idx, smb_rqst_len(server, rqst));
		for (i = 0; i < rqst->rq_nvec; i++)
			dump_smb(rqst->rq_iov[i].iov_base, rqst->rq_iov[i].iov_len);

		log_write(INFO, "RDMA-WR[%u] nvec=%d len=%u iter=%zu rqlen=%lu\n",
			  rqst_idx, rqst->rq_nvec, remaining_data_length,
			  iov_iter_count(&rqst->rq_iter), smb_rqst_len(server, rqst));

		/* Send the metadata pages. */
		klen = 0;
		for (i = 0; i < rqst->rq_nvec; i++)
			klen += rqst->rq_iov[i].iov_len;
		iov_iter_kvec(&iter, ITER_SOURCE, rqst->rq_iov, rqst->rq_nvec, klen);

		rc = smbd_post_send_full_iter(sc, &batch, &iter, &remaining_data_length);
		if (rc < 0) {
			error = rc;
			break;
		}

		if (iov_iter_count(&rqst->rq_iter) > 0) {
			/* And then the data pages if there are any */
			rc = smbd_post_send_full_iter(sc, &batch, &rqst->rq_iter,
						      &remaining_data_length);
			if (rc < 0) {
				error = rc;
				break;
			}
		}

	} while (++rqst_idx < num_rqst);

	rc = smbd_send_batch_flush(sc, &batch, true);
	if (unlikely(!rc && error))
		rc = error;

	/*
	 * As an optimization, we don't wait for individual I/O to finish
	 * before sending the next one.
	 * Send them all and wait for pending send count to get to 0
	 * that means all the I/Os have been out and we are good to return
	 */

	wait_event(sc->send_io.pending.zero_wait_queue,
		atomic_read(&sc->send_io.pending.count) == 0 ||
		sc->status != SMBDIRECT_SOCKET_CONNECTED);

	if (sc->status != SMBDIRECT_SOCKET_CONNECTED && rc == 0)
		rc = -EAGAIN;

	return rc;
}

/*
 * Register memory for RDMA read/write
 * iter: the buffer to register memory with
 * writing: true if this is a RDMA write (SMB read), false for RDMA read
 * need_invalidate: true if this MR needs to be locally invalidated after I/O
 * return value: the MR registered, NULL if failed.
 */
struct smbdirect_mr_io *smbd_register_mr(struct smbd_connection *info,
				 struct iov_iter *iter,
				 bool writing, bool need_invalidate)
{
	struct smbdirect_socket *sc = &info->socket;

	return smbdirect_connection_register_mr_io(sc, iter, writing, need_invalidate);
}

/*
 * Deregister a MR after I/O is done
 * This function may wait if remote invalidation is not used
 * and we have to locally invalidate the buffer to prevent data is being
 * modified by remote peer after upper layer consumes it
 */
void smbd_deregister_mr(struct smbdirect_mr_io *mr)
{
	smbdirect_connection_deregister_mr_io(mr);
}
