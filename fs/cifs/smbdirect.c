/*
 *   Copyright (C) 2017, Microsoft Corporation.
 *
 *   Author(s): Long Li <longli@microsoft.com>
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 */
#include <linux/module.h>
#include "smbdirect.h"
#include "cifs_debug.h"

static struct smbd_response *get_empty_queue_buffer(
		struct smbd_connection *info);
static struct smbd_response *get_receive_buffer(
		struct smbd_connection *info);
static void put_receive_buffer(
		struct smbd_connection *info,
		struct smbd_response *response);
static int allocate_receive_buffers(struct smbd_connection *info, int num_buf);
static void destroy_receive_buffers(struct smbd_connection *info);

static void put_empty_packet(
		struct smbd_connection *info, struct smbd_response *response);
static void enqueue_reassembly(
		struct smbd_connection *info,
		struct smbd_response *response, int data_length);
static struct smbd_response *_get_first_reassembly(
		struct smbd_connection *info);

static int smbd_post_recv(
		struct smbd_connection *info,
		struct smbd_response *response);

static int smbd_post_send_empty(struct smbd_connection *info);

/* SMBD version number */
#define SMBD_V1	0x0100

/* Port numbers for SMBD transport */
#define SMB_PORT	445
#define SMBD_PORT	5445

/* Address lookup and resolve timeout in ms */
#define RDMA_RESOLVE_TIMEOUT	5000

/* SMBD negotiation timeout in seconds */
#define SMBD_NEGOTIATE_TIMEOUT	120

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

/*  The maximum fragmented upper-layer payload receive size supported */
int smbd_max_fragmented_recv_size = 1024 * 1024;

/*  The maximum single-message size which can be received */
int smbd_max_receive_size = 8192;

/* The timeout to initiate send of a keepalive message on idle */
int smbd_keep_alive_interval = 120;

/*
 * User configurable initial values for RDMA transport
 * The actual values used may be lower and are limited to hardware capabilities
 */
/* Default maximum number of SGEs in a RDMA write/read */
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

/*
 * Destroy the transport and related RDMA and memory resources
 * Need to go through all the pending counters and make sure on one is using
 * the transport while it is destroyed
 */
static void smbd_destroy_rdma_work(struct work_struct *work)
{
	struct smbd_response *response;
	struct smbd_connection *info =
		container_of(work, struct smbd_connection, destroy_work);
	unsigned long flags;

	log_rdma_event(INFO, "destroying qp\n");
	ib_drain_qp(info->id->qp);
	rdma_destroy_qp(info->id);

	/* Unblock all I/O waiting on the send queue */
	wake_up_interruptible_all(&info->wait_send_queue);

	log_rdma_event(INFO, "cancelling idle timer\n");
	cancel_delayed_work_sync(&info->idle_timer_work);
	log_rdma_event(INFO, "cancelling send immediate work\n");
	cancel_delayed_work_sync(&info->send_immediate_work);

	log_rdma_event(INFO, "wait for all recv to finish\n");
	wake_up_interruptible(&info->wait_reassembly_queue);

	log_rdma_event(INFO, "wait for all send posted to IB to finish\n");
	wait_event(info->wait_send_pending,
		atomic_read(&info->send_pending) == 0);
	wait_event(info->wait_send_payload_pending,
		atomic_read(&info->send_payload_pending) == 0);

	/* It's not posssible for upper layer to get to reassembly */
	log_rdma_event(INFO, "drain the reassembly queue\n");
	do {
		spin_lock_irqsave(&info->reassembly_queue_lock, flags);
		response = _get_first_reassembly(info);
		if (response) {
			list_del(&response->list);
			spin_unlock_irqrestore(
				&info->reassembly_queue_lock, flags);
			put_receive_buffer(info, response);
		}
	} while (response);
	spin_unlock_irqrestore(&info->reassembly_queue_lock, flags);
	info->reassembly_data_length = 0;

	log_rdma_event(INFO, "free receive buffers\n");
	wait_event(info->wait_receive_queues,
		info->count_receive_queue + info->count_empty_packet_queue
			== info->receive_credit_max);
	destroy_receive_buffers(info);

	ib_free_cq(info->send_cq);
	ib_free_cq(info->recv_cq);
	ib_dealloc_pd(info->pd);
	rdma_destroy_id(info->id);

	/* free mempools */
	mempool_destroy(info->request_mempool);
	kmem_cache_destroy(info->request_cache);

	mempool_destroy(info->response_mempool);
	kmem_cache_destroy(info->response_cache);

	info->transport_status = SMBD_DESTROYED;
	wake_up_all(&info->wait_destroy);
}

static int smbd_process_disconnected(struct smbd_connection *info)
{
	schedule_work(&info->destroy_work);
	return 0;
}

static void smbd_disconnect_rdma_work(struct work_struct *work)
{
	struct smbd_connection *info =
		container_of(work, struct smbd_connection, disconnect_work);

	if (info->transport_status == SMBD_CONNECTED) {
		info->transport_status = SMBD_DISCONNECTING;
		rdma_disconnect(info->id);
	}
}

static void smbd_disconnect_rdma_connection(struct smbd_connection *info)
{
	queue_work(info->workqueue, &info->disconnect_work);
}

/* Upcall from RDMA CM */
static int smbd_conn_upcall(
		struct rdma_cm_id *id, struct rdma_cm_event *event)
{
	struct smbd_connection *info = id->context;

	log_rdma_event(INFO, "event=%d status=%d\n",
		event->event, event->status);

	switch (event->event) {
	case RDMA_CM_EVENT_ADDR_RESOLVED:
	case RDMA_CM_EVENT_ROUTE_RESOLVED:
		info->ri_rc = 0;
		complete(&info->ri_done);
		break;

	case RDMA_CM_EVENT_ADDR_ERROR:
		info->ri_rc = -EHOSTUNREACH;
		complete(&info->ri_done);
		break;

	case RDMA_CM_EVENT_ROUTE_ERROR:
		info->ri_rc = -ENETUNREACH;
		complete(&info->ri_done);
		break;

	case RDMA_CM_EVENT_ESTABLISHED:
		log_rdma_event(INFO, "connected event=%d\n", event->event);
		info->transport_status = SMBD_CONNECTED;
		wake_up_interruptible(&info->conn_wait);
		break;

	case RDMA_CM_EVENT_CONNECT_ERROR:
	case RDMA_CM_EVENT_UNREACHABLE:
	case RDMA_CM_EVENT_REJECTED:
		log_rdma_event(INFO, "connecting failed event=%d\n", event->event);
		info->transport_status = SMBD_DISCONNECTED;
		wake_up_interruptible(&info->conn_wait);
		break;

	case RDMA_CM_EVENT_DEVICE_REMOVAL:
	case RDMA_CM_EVENT_DISCONNECTED:
		/* This happenes when we fail the negotiation */
		if (info->transport_status == SMBD_NEGOTIATE_FAILED) {
			info->transport_status = SMBD_DISCONNECTED;
			wake_up(&info->conn_wait);
			break;
		}

		info->transport_status = SMBD_DISCONNECTED;
		smbd_process_disconnected(info);
		break;

	default:
		break;
	}

	return 0;
}

/* Upcall from RDMA QP */
static void
smbd_qp_async_error_upcall(struct ib_event *event, void *context)
{
	struct smbd_connection *info = context;

	log_rdma_event(ERR, "%s on device %s info %p\n",
		ib_event_msg(event->event), event->device->name, info);

	switch (event->event) {
	case IB_EVENT_CQ_ERR:
	case IB_EVENT_QP_FATAL:
		smbd_disconnect_rdma_connection(info);

	default:
		break;
	}
}

static inline void *smbd_request_payload(struct smbd_request *request)
{
	return (void *)request->packet;
}

static inline void *smbd_response_payload(struct smbd_response *response)
{
	return (void *)response->packet;
}

/* Called when a RDMA send is done */
static void send_done(struct ib_cq *cq, struct ib_wc *wc)
{
	int i;
	struct smbd_request *request =
		container_of(wc->wr_cqe, struct smbd_request, cqe);

	log_rdma_send(INFO, "smbd_request %p completed wc->status=%d\n",
		request, wc->status);

	if (wc->status != IB_WC_SUCCESS || wc->opcode != IB_WC_SEND) {
		log_rdma_send(ERR, "wc->status=%d wc->opcode=%d\n",
			wc->status, wc->opcode);
		smbd_disconnect_rdma_connection(request->info);
	}

	for (i = 0; i < request->num_sge; i++)
		ib_dma_unmap_single(request->info->id->device,
			request->sge[i].addr,
			request->sge[i].length,
			DMA_TO_DEVICE);

	if (request->has_payload) {
		if (atomic_dec_and_test(&request->info->send_payload_pending))
			wake_up(&request->info->wait_send_payload_pending);
	} else {
		if (atomic_dec_and_test(&request->info->send_pending))
			wake_up(&request->info->wait_send_pending);
	}

	mempool_free(request, request->info->request_mempool);
}

static void dump_smbd_negotiate_resp(struct smbd_negotiate_resp *resp)
{
	log_rdma_event(INFO, "resp message min_version %u max_version %u "
		"negotiated_version %u credits_requested %u "
		"credits_granted %u status %u max_readwrite_size %u "
		"preferred_send_size %u max_receive_size %u "
		"max_fragmented_size %u\n",
		resp->min_version, resp->max_version, resp->negotiated_version,
		resp->credits_requested, resp->credits_granted, resp->status,
		resp->max_readwrite_size, resp->preferred_send_size,
		resp->max_receive_size, resp->max_fragmented_size);
}

/*
 * Process a negotiation response message, according to [MS-SMBD]3.1.5.7
 * response, packet_length: the negotiation response message
 * return value: true if negotiation is a success, false if failed
 */
static bool process_negotiation_response(
		struct smbd_response *response, int packet_length)
{
	struct smbd_connection *info = response->info;
	struct smbd_negotiate_resp *packet = smbd_response_payload(response);

	if (packet_length < sizeof(struct smbd_negotiate_resp)) {
		log_rdma_event(ERR,
			"error: packet_length=%d\n", packet_length);
		return false;
	}

	if (le16_to_cpu(packet->negotiated_version) != SMBD_V1) {
		log_rdma_event(ERR, "error: negotiated_version=%x\n",
			le16_to_cpu(packet->negotiated_version));
		return false;
	}
	info->protocol = le16_to_cpu(packet->negotiated_version);

	if (packet->credits_requested == 0) {
		log_rdma_event(ERR, "error: credits_requested==0\n");
		return false;
	}
	info->receive_credit_target = le16_to_cpu(packet->credits_requested);

	if (packet->credits_granted == 0) {
		log_rdma_event(ERR, "error: credits_granted==0\n");
		return false;
	}
	atomic_set(&info->send_credits, le16_to_cpu(packet->credits_granted));

	atomic_set(&info->receive_credits, 0);

	if (le32_to_cpu(packet->preferred_send_size) > info->max_receive_size) {
		log_rdma_event(ERR, "error: preferred_send_size=%d\n",
			le32_to_cpu(packet->preferred_send_size));
		return false;
	}
	info->max_receive_size = le32_to_cpu(packet->preferred_send_size);

	if (le32_to_cpu(packet->max_receive_size) < SMBD_MIN_RECEIVE_SIZE) {
		log_rdma_event(ERR, "error: max_receive_size=%d\n",
			le32_to_cpu(packet->max_receive_size));
		return false;
	}
	info->max_send_size = min_t(int, info->max_send_size,
					le32_to_cpu(packet->max_receive_size));

	if (le32_to_cpu(packet->max_fragmented_size) <
			SMBD_MIN_FRAGMENTED_SIZE) {
		log_rdma_event(ERR, "error: max_fragmented_size=%d\n",
			le32_to_cpu(packet->max_fragmented_size));
		return false;
	}
	info->max_fragmented_send_size =
		le32_to_cpu(packet->max_fragmented_size);

	return true;
}

/*
 * Check and schedule to send an immediate packet
 * This is used to extend credtis to remote peer to keep the transport busy
 */
static void check_and_send_immediate(struct smbd_connection *info)
{
	if (info->transport_status != SMBD_CONNECTED)
		return;

	info->send_immediate = true;

	/*
	 * Promptly send a packet if our peer is running low on receive
	 * credits
	 */
	if (atomic_read(&info->receive_credits) <
		info->receive_credit_target - 1)
		queue_delayed_work(
			info->workqueue, &info->send_immediate_work, 0);
}

static void smbd_post_send_credits(struct work_struct *work)
{
	int ret = 0;
	int use_receive_queue = 1;
	int rc;
	struct smbd_response *response;
	struct smbd_connection *info =
		container_of(work, struct smbd_connection,
			post_send_credits_work);

	if (info->transport_status != SMBD_CONNECTED) {
		wake_up(&info->wait_receive_queues);
		return;
	}

	if (info->receive_credit_target >
		atomic_read(&info->receive_credits)) {
		while (true) {
			if (use_receive_queue)
				response = get_receive_buffer(info);
			else
				response = get_empty_queue_buffer(info);
			if (!response) {
				/* now switch to emtpy packet queue */
				if (use_receive_queue) {
					use_receive_queue = 0;
					continue;
				} else
					break;
			}

			response->type = SMBD_TRANSFER_DATA;
			response->first_segment = false;
			rc = smbd_post_recv(info, response);
			if (rc) {
				log_rdma_recv(ERR,
					"post_recv failed rc=%d\n", rc);
				put_receive_buffer(info, response);
				break;
			}

			ret++;
		}
	}

	spin_lock(&info->lock_new_credits_offered);
	info->new_credits_offered += ret;
	spin_unlock(&info->lock_new_credits_offered);

	atomic_add(ret, &info->receive_credits);

	/* Check if we can post new receive and grant credits to peer */
	check_and_send_immediate(info);
}

static void smbd_recv_done_work(struct work_struct *work)
{
	struct smbd_connection *info =
		container_of(work, struct smbd_connection, recv_done_work);

	/*
	 * We may have new send credits granted from remote peer
	 * If any sender is blcoked on lack of credets, unblock it
	 */
	if (atomic_read(&info->send_credits))
		wake_up_interruptible(&info->wait_send_queue);

	/*
	 * Check if we need to send something to remote peer to
	 * grant more credits or respond to KEEP_ALIVE packet
	 */
	check_and_send_immediate(info);
}

/* Called from softirq, when recv is done */
static void recv_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct smbd_data_transfer *data_transfer;
	struct smbd_response *response =
		container_of(wc->wr_cqe, struct smbd_response, cqe);
	struct smbd_connection *info = response->info;
	int data_length = 0;

	log_rdma_recv(INFO, "response=%p type=%d wc status=%d wc opcode %d "
		      "byte_len=%d pkey_index=%x\n",
		response, response->type, wc->status, wc->opcode,
		wc->byte_len, wc->pkey_index);

	if (wc->status != IB_WC_SUCCESS || wc->opcode != IB_WC_RECV) {
		log_rdma_recv(INFO, "wc->status=%d opcode=%d\n",
			wc->status, wc->opcode);
		smbd_disconnect_rdma_connection(info);
		goto error;
	}

	ib_dma_sync_single_for_cpu(
		wc->qp->device,
		response->sge.addr,
		response->sge.length,
		DMA_FROM_DEVICE);

	switch (response->type) {
	/* SMBD negotiation response */
	case SMBD_NEGOTIATE_RESP:
		dump_smbd_negotiate_resp(smbd_response_payload(response));
		info->full_packet_received = true;
		info->negotiate_done =
			process_negotiation_response(response, wc->byte_len);
		complete(&info->negotiate_completion);
		break;

	/* SMBD data transfer packet */
	case SMBD_TRANSFER_DATA:
		data_transfer = smbd_response_payload(response);
		data_length = le32_to_cpu(data_transfer->data_length);

		/*
		 * If this is a packet with data playload place the data in
		 * reassembly queue and wake up the reading thread
		 */
		if (data_length) {
			if (info->full_packet_received)
				response->first_segment = true;

			if (le32_to_cpu(data_transfer->remaining_data_length))
				info->full_packet_received = false;
			else
				info->full_packet_received = true;

			enqueue_reassembly(
				info,
				response,
				data_length);
		} else
			put_empty_packet(info, response);

		if (data_length)
			wake_up_interruptible(&info->wait_reassembly_queue);

		atomic_dec(&info->receive_credits);
		info->receive_credit_target =
			le16_to_cpu(data_transfer->credits_requested);
		atomic_add(le16_to_cpu(data_transfer->credits_granted),
			&info->send_credits);

		log_incoming(INFO, "data flags %d data_offset %d "
			"data_length %d remaining_data_length %d\n",
			le16_to_cpu(data_transfer->flags),
			le32_to_cpu(data_transfer->data_offset),
			le32_to_cpu(data_transfer->data_length),
			le32_to_cpu(data_transfer->remaining_data_length));

		/* Send a KEEP_ALIVE response right away if requested */
		info->keep_alive_requested = KEEP_ALIVE_NONE;
		if (le16_to_cpu(data_transfer->flags) &
				SMB_DIRECT_RESPONSE_REQUESTED) {
			info->keep_alive_requested = KEEP_ALIVE_PENDING;
		}

		queue_work(info->workqueue, &info->recv_done_work);
		return;

	default:
		log_rdma_recv(ERR,
			"unexpected response type=%d\n", response->type);
	}

error:
	put_receive_buffer(info, response);
}

static struct rdma_cm_id *smbd_create_id(
		struct smbd_connection *info,
		struct sockaddr *dstaddr, int port)
{
	struct rdma_cm_id *id;
	int rc;
	__be16 *sport;

	id = rdma_create_id(&init_net, smbd_conn_upcall, info,
		RDMA_PS_TCP, IB_QPT_RC);
	if (IS_ERR(id)) {
		rc = PTR_ERR(id);
		log_rdma_event(ERR, "rdma_create_id() failed %i\n", rc);
		return id;
	}

	if (dstaddr->sa_family == AF_INET6)
		sport = &((struct sockaddr_in6 *)dstaddr)->sin6_port;
	else
		sport = &((struct sockaddr_in *)dstaddr)->sin_port;

	*sport = htons(port);

	init_completion(&info->ri_done);
	info->ri_rc = -ETIMEDOUT;

	rc = rdma_resolve_addr(id, NULL, (struct sockaddr *)dstaddr,
		RDMA_RESOLVE_TIMEOUT);
	if (rc) {
		log_rdma_event(ERR, "rdma_resolve_addr() failed %i\n", rc);
		goto out;
	}
	wait_for_completion_interruptible_timeout(
		&info->ri_done, msecs_to_jiffies(RDMA_RESOLVE_TIMEOUT));
	rc = info->ri_rc;
	if (rc) {
		log_rdma_event(ERR, "rdma_resolve_addr() completed %i\n", rc);
		goto out;
	}

	info->ri_rc = -ETIMEDOUT;
	rc = rdma_resolve_route(id, RDMA_RESOLVE_TIMEOUT);
	if (rc) {
		log_rdma_event(ERR, "rdma_resolve_route() failed %i\n", rc);
		goto out;
	}
	wait_for_completion_interruptible_timeout(
		&info->ri_done, msecs_to_jiffies(RDMA_RESOLVE_TIMEOUT));
	rc = info->ri_rc;
	if (rc) {
		log_rdma_event(ERR, "rdma_resolve_route() completed %i\n", rc);
		goto out;
	}

	return id;

out:
	rdma_destroy_id(id);
	return ERR_PTR(rc);
}

/*
 * Test if FRWR (Fast Registration Work Requests) is supported on the device
 * This implementation requries FRWR on RDMA read/write
 * return value: true if it is supported
 */
static bool frwr_is_supported(struct ib_device_attr *attrs)
{
	if (!(attrs->device_cap_flags & IB_DEVICE_MEM_MGT_EXTENSIONS))
		return false;
	if (attrs->max_fast_reg_page_list_len == 0)
		return false;
	return true;
}

static int smbd_ia_open(
		struct smbd_connection *info,
		struct sockaddr *dstaddr, int port)
{
	int rc;

	info->id = smbd_create_id(info, dstaddr, port);
	if (IS_ERR(info->id)) {
		rc = PTR_ERR(info->id);
		goto out1;
	}

	if (!frwr_is_supported(&info->id->device->attrs)) {
		log_rdma_event(ERR,
			"Fast Registration Work Requests "
			"(FRWR) is not supported\n");
		log_rdma_event(ERR,
			"Device capability flags = %llx "
			"max_fast_reg_page_list_len = %u\n",
			info->id->device->attrs.device_cap_flags,
			info->id->device->attrs.max_fast_reg_page_list_len);
		rc = -EPROTONOSUPPORT;
		goto out2;
	}

	info->pd = ib_alloc_pd(info->id->device, 0);
	if (IS_ERR(info->pd)) {
		rc = PTR_ERR(info->pd);
		log_rdma_event(ERR, "ib_alloc_pd() returned %d\n", rc);
		goto out2;
	}

	return 0;

out2:
	rdma_destroy_id(info->id);
	info->id = NULL;

out1:
	return rc;
}

/*
 * Send a negotiation request message to the peer
 * The negotiation procedure is in [MS-SMBD] 3.1.5.2 and 3.1.5.3
 * After negotiation, the transport is connected and ready for
 * carrying upper layer SMB payload
 */
static int smbd_post_send_negotiate_req(struct smbd_connection *info)
{
	struct ib_send_wr send_wr, *send_wr_fail;
	int rc = -ENOMEM;
	struct smbd_request *request;
	struct smbd_negotiate_req *packet;

	request = mempool_alloc(info->request_mempool, GFP_KERNEL);
	if (!request)
		return rc;

	request->info = info;

	packet = smbd_request_payload(request);
	packet->min_version = cpu_to_le16(SMBD_V1);
	packet->max_version = cpu_to_le16(SMBD_V1);
	packet->reserved = 0;
	packet->credits_requested = cpu_to_le16(info->send_credit_target);
	packet->preferred_send_size = cpu_to_le32(info->max_send_size);
	packet->max_receive_size = cpu_to_le32(info->max_receive_size);
	packet->max_fragmented_size =
		cpu_to_le32(info->max_fragmented_recv_size);

	request->num_sge = 1;
	request->sge[0].addr = ib_dma_map_single(
				info->id->device, (void *)packet,
				sizeof(*packet), DMA_TO_DEVICE);
	if (ib_dma_mapping_error(info->id->device, request->sge[0].addr)) {
		rc = -EIO;
		goto dma_mapping_failed;
	}

	request->sge[0].length = sizeof(*packet);
	request->sge[0].lkey = info->pd->local_dma_lkey;

	ib_dma_sync_single_for_device(
		info->id->device, request->sge[0].addr,
		request->sge[0].length, DMA_TO_DEVICE);

	request->cqe.done = send_done;

	send_wr.next = NULL;
	send_wr.wr_cqe = &request->cqe;
	send_wr.sg_list = request->sge;
	send_wr.num_sge = request->num_sge;
	send_wr.opcode = IB_WR_SEND;
	send_wr.send_flags = IB_SEND_SIGNALED;

	log_rdma_send(INFO, "sge addr=%llx length=%x lkey=%x\n",
		request->sge[0].addr,
		request->sge[0].length, request->sge[0].lkey);

	request->has_payload = false;
	atomic_inc(&info->send_pending);
	rc = ib_post_send(info->id->qp, &send_wr, &send_wr_fail);
	if (!rc)
		return 0;

	/* if we reach here, post send failed */
	log_rdma_send(ERR, "ib_post_send failed rc=%d\n", rc);
	atomic_dec(&info->send_pending);
	ib_dma_unmap_single(info->id->device, request->sge[0].addr,
		request->sge[0].length, DMA_TO_DEVICE);

dma_mapping_failed:
	mempool_free(request, info->request_mempool);
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
static int manage_credits_prior_sending(struct smbd_connection *info)
{
	int new_credits;

	spin_lock(&info->lock_new_credits_offered);
	new_credits = info->new_credits_offered;
	info->new_credits_offered = 0;
	spin_unlock(&info->lock_new_credits_offered);

	return new_credits;
}

/*
 * Check if we need to send a KEEP_ALIVE message
 * The idle connection timer triggers a KEEP_ALIVE message when expires
 * SMB_DIRECT_RESPONSE_REQUESTED is set in the message flag to have peer send
 * back a response.
 * return value:
 * 1 if SMB_DIRECT_RESPONSE_REQUESTED needs to be set
 * 0: otherwise
 */
static int manage_keep_alive_before_sending(struct smbd_connection *info)
{
	if (info->keep_alive_requested == KEEP_ALIVE_PENDING) {
		info->keep_alive_requested = KEEP_ALIVE_SENT;
		return 1;
	}
	return 0;
}

/*
 * Build and prepare the SMBD packet header
 * This function waits for avaialbe send credits and build a SMBD packet
 * header. The caller then optional append payload to the packet after
 * the header
 * intput values
 * size: the size of the payload
 * remaining_data_length: remaining data to send if this is part of a
 * fragmented packet
 * output values
 * request_out: the request allocated from this function
 * return values: 0 on success, otherwise actual error code returned
 */
static int smbd_create_header(struct smbd_connection *info,
		int size, int remaining_data_length,
		struct smbd_request **request_out)
{
	struct smbd_request *request;
	struct smbd_data_transfer *packet;
	int header_length;
	int rc;

	/* Wait for send credits. A SMBD packet needs one credit */
	rc = wait_event_interruptible(info->wait_send_queue,
		atomic_read(&info->send_credits) > 0 ||
		info->transport_status != SMBD_CONNECTED);
	if (rc)
		return rc;

	if (info->transport_status != SMBD_CONNECTED) {
		log_outgoing(ERR, "disconnected not sending\n");
		return -ENOENT;
	}
	atomic_dec(&info->send_credits);

	request = mempool_alloc(info->request_mempool, GFP_KERNEL);
	if (!request) {
		rc = -ENOMEM;
		goto err;
	}

	request->info = info;

	/* Fill in the packet header */
	packet = smbd_request_payload(request);
	packet->credits_requested = cpu_to_le16(info->send_credit_target);
	packet->credits_granted =
		cpu_to_le16(manage_credits_prior_sending(info));
	info->send_immediate = false;

	packet->flags = 0;
	if (manage_keep_alive_before_sending(info))
		packet->flags |= cpu_to_le16(SMB_DIRECT_RESPONSE_REQUESTED);

	packet->reserved = 0;
	if (!size)
		packet->data_offset = 0;
	else
		packet->data_offset = cpu_to_le32(24);
	packet->data_length = cpu_to_le32(size);
	packet->remaining_data_length = cpu_to_le32(remaining_data_length);
	packet->padding = 0;

	log_outgoing(INFO, "credits_requested=%d credits_granted=%d "
		"data_offset=%d data_length=%d remaining_data_length=%d\n",
		le16_to_cpu(packet->credits_requested),
		le16_to_cpu(packet->credits_granted),
		le32_to_cpu(packet->data_offset),
		le32_to_cpu(packet->data_length),
		le32_to_cpu(packet->remaining_data_length));

	/* Map the packet to DMA */
	header_length = sizeof(struct smbd_data_transfer);
	/* If this is a packet without payload, don't send padding */
	if (!size)
		header_length = offsetof(struct smbd_data_transfer, padding);

	request->num_sge = 1;
	request->sge[0].addr = ib_dma_map_single(info->id->device,
						 (void *)packet,
						 header_length,
						 DMA_BIDIRECTIONAL);
	if (ib_dma_mapping_error(info->id->device, request->sge[0].addr)) {
		mempool_free(request, info->request_mempool);
		rc = -EIO;
		goto err;
	}

	request->sge[0].length = header_length;
	request->sge[0].lkey = info->pd->local_dma_lkey;

	*request_out = request;
	return 0;

err:
	atomic_inc(&info->send_credits);
	return rc;
}

static void smbd_destroy_header(struct smbd_connection *info,
		struct smbd_request *request)
{

	ib_dma_unmap_single(info->id->device,
			    request->sge[0].addr,
			    request->sge[0].length,
			    DMA_TO_DEVICE);
	mempool_free(request, info->request_mempool);
	atomic_inc(&info->send_credits);
}

/* Post the send request */
static int smbd_post_send(struct smbd_connection *info,
		struct smbd_request *request, bool has_payload)
{
	struct ib_send_wr send_wr, *send_wr_fail;
	int rc, i;

	for (i = 0; i < request->num_sge; i++) {
		log_rdma_send(INFO,
			"rdma_request sge[%d] addr=%llu legnth=%u\n",
			i, request->sge[0].addr, request->sge[0].length);
		ib_dma_sync_single_for_device(
			info->id->device,
			request->sge[i].addr,
			request->sge[i].length,
			DMA_TO_DEVICE);
	}

	request->cqe.done = send_done;

	send_wr.next = NULL;
	send_wr.wr_cqe = &request->cqe;
	send_wr.sg_list = request->sge;
	send_wr.num_sge = request->num_sge;
	send_wr.opcode = IB_WR_SEND;
	send_wr.send_flags = IB_SEND_SIGNALED;

	if (has_payload) {
		request->has_payload = true;
		atomic_inc(&info->send_payload_pending);
	} else {
		request->has_payload = false;
		atomic_inc(&info->send_pending);
	}

	rc = ib_post_send(info->id->qp, &send_wr, &send_wr_fail);
	if (rc) {
		log_rdma_send(ERR, "ib_post_send failed rc=%d\n", rc);
		if (has_payload) {
			if (atomic_dec_and_test(&info->send_payload_pending))
				wake_up(&info->wait_send_payload_pending);
		} else {
			if (atomic_dec_and_test(&info->send_pending))
				wake_up(&info->wait_send_pending);
		}
	} else
		/* Reset timer for idle connection after packet is sent */
		mod_delayed_work(info->workqueue, &info->idle_timer_work,
			info->keep_alive_interval*HZ);

	return rc;
}

static int smbd_post_send_sgl(struct smbd_connection *info,
	struct scatterlist *sgl, int data_length, int remaining_data_length)
{
	int num_sgs;
	int i, rc;
	struct smbd_request *request;
	struct scatterlist *sg;

	rc = smbd_create_header(
		info, data_length, remaining_data_length, &request);
	if (rc)
		return rc;

	num_sgs = sgl ? sg_nents(sgl) : 0;
	for_each_sg(sgl, sg, num_sgs, i) {
		request->sge[i+1].addr =
			ib_dma_map_page(info->id->device, sg_page(sg),
			       sg->offset, sg->length, DMA_BIDIRECTIONAL);
		if (ib_dma_mapping_error(
				info->id->device, request->sge[i+1].addr)) {
			rc = -EIO;
			request->sge[i+1].addr = 0;
			goto dma_mapping_failure;
		}
		request->sge[i+1].length = sg->length;
		request->sge[i+1].lkey = info->pd->local_dma_lkey;
		request->num_sge++;
	}

	rc = smbd_post_send(info, request, data_length);
	if (!rc)
		return 0;

dma_mapping_failure:
	for (i = 1; i < request->num_sge; i++)
		if (request->sge[i].addr)
			ib_dma_unmap_single(info->id->device,
					    request->sge[i].addr,
					    request->sge[i].length,
					    DMA_TO_DEVICE);
	smbd_destroy_header(info, request);
	return rc;
}

/*
 * Send an empty message
 * Empty message is used to extend credits to peer to for keep live
 * while there is no upper layer payload to send at the time
 */
static int smbd_post_send_empty(struct smbd_connection *info)
{
	info->count_send_empty++;
	return smbd_post_send_sgl(info, NULL, 0, 0);
}

/*
 * Post a receive request to the transport
 * The remote peer can only send data when a receive request is posted
 * The interaction is controlled by send/receive credit system
 */
static int smbd_post_recv(
		struct smbd_connection *info, struct smbd_response *response)
{
	struct ib_recv_wr recv_wr, *recv_wr_fail = NULL;
	int rc = -EIO;

	response->sge.addr = ib_dma_map_single(
				info->id->device, response->packet,
				info->max_receive_size, DMA_FROM_DEVICE);
	if (ib_dma_mapping_error(info->id->device, response->sge.addr))
		return rc;

	response->sge.length = info->max_receive_size;
	response->sge.lkey = info->pd->local_dma_lkey;

	response->cqe.done = recv_done;

	recv_wr.wr_cqe = &response->cqe;
	recv_wr.next = NULL;
	recv_wr.sg_list = &response->sge;
	recv_wr.num_sge = 1;

	rc = ib_post_recv(info->id->qp, &recv_wr, &recv_wr_fail);
	if (rc) {
		ib_dma_unmap_single(info->id->device, response->sge.addr,
				    response->sge.length, DMA_FROM_DEVICE);

		log_rdma_recv(ERR, "ib_post_recv failed rc=%d\n", rc);
	}

	return rc;
}

/* Perform SMBD negotiate according to [MS-SMBD] 3.1.5.2 */
static int smbd_negotiate(struct smbd_connection *info)
{
	int rc;
	struct smbd_response *response = get_receive_buffer(info);

	response->type = SMBD_NEGOTIATE_RESP;
	rc = smbd_post_recv(info, response);
	log_rdma_event(INFO,
		"smbd_post_recv rc=%d iov.addr=%llx iov.length=%x "
		"iov.lkey=%x\n",
		rc, response->sge.addr,
		response->sge.length, response->sge.lkey);
	if (rc)
		return rc;

	init_completion(&info->negotiate_completion);
	info->negotiate_done = false;
	rc = smbd_post_send_negotiate_req(info);
	if (rc)
		return rc;

	rc = wait_for_completion_interruptible_timeout(
		&info->negotiate_completion, SMBD_NEGOTIATE_TIMEOUT * HZ);
	log_rdma_event(INFO, "wait_for_completion_timeout rc=%d\n", rc);

	if (info->negotiate_done)
		return 0;

	if (rc == 0)
		rc = -ETIMEDOUT;
	else if (rc == -ERESTARTSYS)
		rc = -EINTR;
	else
		rc = -ENOTCONN;

	return rc;
}

static void put_empty_packet(
		struct smbd_connection *info, struct smbd_response *response)
{
	spin_lock(&info->empty_packet_queue_lock);
	list_add_tail(&response->list, &info->empty_packet_queue);
	info->count_empty_packet_queue++;
	spin_unlock(&info->empty_packet_queue_lock);

	queue_work(info->workqueue, &info->post_send_credits_work);
}

/*
 * Implement Connection.FragmentReassemblyBuffer defined in [MS-SMBD] 3.1.1.1
 * This is a queue for reassembling upper layer payload and present to upper
 * layer. All the inncoming payload go to the reassembly queue, regardless of
 * if reassembly is required. The uuper layer code reads from the queue for all
 * incoming payloads.
 * Put a received packet to the reassembly queue
 * response: the packet received
 * data_length: the size of payload in this packet
 */
static void enqueue_reassembly(
	struct smbd_connection *info,
	struct smbd_response *response,
	int data_length)
{
	spin_lock(&info->reassembly_queue_lock);
	list_add_tail(&response->list, &info->reassembly_queue);
	info->reassembly_queue_length++;
	/*
	 * Make sure reassembly_data_length is updated after list and
	 * reassembly_queue_length are updated. On the dequeue side
	 * reassembly_data_length is checked without a lock to determine
	 * if reassembly_queue_length and list is up to date
	 */
	virt_wmb();
	info->reassembly_data_length += data_length;
	spin_unlock(&info->reassembly_queue_lock);
	info->count_reassembly_queue++;
	info->count_enqueue_reassembly_queue++;
}

/*
 * Get the first entry at the front of reassembly queue
 * Caller is responsible for locking
 * return value: the first entry if any, NULL if queue is empty
 */
static struct smbd_response *_get_first_reassembly(struct smbd_connection *info)
{
	struct smbd_response *ret = NULL;

	if (!list_empty(&info->reassembly_queue)) {
		ret = list_first_entry(
			&info->reassembly_queue,
			struct smbd_response, list);
	}
	return ret;
}

static struct smbd_response *get_empty_queue_buffer(
		struct smbd_connection *info)
{
	struct smbd_response *ret = NULL;
	unsigned long flags;

	spin_lock_irqsave(&info->empty_packet_queue_lock, flags);
	if (!list_empty(&info->empty_packet_queue)) {
		ret = list_first_entry(
			&info->empty_packet_queue,
			struct smbd_response, list);
		list_del(&ret->list);
		info->count_empty_packet_queue--;
	}
	spin_unlock_irqrestore(&info->empty_packet_queue_lock, flags);

	return ret;
}

/*
 * Get a receive buffer
 * For each remote send, we need to post a receive. The receive buffers are
 * pre-allocated in advance.
 * return value: the receive buffer, NULL if none is available
 */
static struct smbd_response *get_receive_buffer(struct smbd_connection *info)
{
	struct smbd_response *ret = NULL;
	unsigned long flags;

	spin_lock_irqsave(&info->receive_queue_lock, flags);
	if (!list_empty(&info->receive_queue)) {
		ret = list_first_entry(
			&info->receive_queue,
			struct smbd_response, list);
		list_del(&ret->list);
		info->count_receive_queue--;
		info->count_get_receive_buffer++;
	}
	spin_unlock_irqrestore(&info->receive_queue_lock, flags);

	return ret;
}

/*
 * Return a receive buffer
 * Upon returning of a receive buffer, we can post new receive and extend
 * more receive credits to remote peer. This is done immediately after a
 * receive buffer is returned.
 */
static void put_receive_buffer(
	struct smbd_connection *info, struct smbd_response *response)
{
	unsigned long flags;

	ib_dma_unmap_single(info->id->device, response->sge.addr,
		response->sge.length, DMA_FROM_DEVICE);

	spin_lock_irqsave(&info->receive_queue_lock, flags);
	list_add_tail(&response->list, &info->receive_queue);
	info->count_receive_queue++;
	info->count_put_receive_buffer++;
	spin_unlock_irqrestore(&info->receive_queue_lock, flags);

	queue_work(info->workqueue, &info->post_send_credits_work);
}

/* Preallocate all receive buffer on transport establishment */
static int allocate_receive_buffers(struct smbd_connection *info, int num_buf)
{
	int i;
	struct smbd_response *response;

	INIT_LIST_HEAD(&info->reassembly_queue);
	spin_lock_init(&info->reassembly_queue_lock);
	info->reassembly_data_length = 0;
	info->reassembly_queue_length = 0;

	INIT_LIST_HEAD(&info->receive_queue);
	spin_lock_init(&info->receive_queue_lock);
	info->count_receive_queue = 0;

	INIT_LIST_HEAD(&info->empty_packet_queue);
	spin_lock_init(&info->empty_packet_queue_lock);
	info->count_empty_packet_queue = 0;

	init_waitqueue_head(&info->wait_receive_queues);

	for (i = 0; i < num_buf; i++) {
		response = mempool_alloc(info->response_mempool, GFP_KERNEL);
		if (!response)
			goto allocate_failed;

		response->info = info;
		list_add_tail(&response->list, &info->receive_queue);
		info->count_receive_queue++;
	}

	return 0;

allocate_failed:
	while (!list_empty(&info->receive_queue)) {
		response = list_first_entry(
				&info->receive_queue,
				struct smbd_response, list);
		list_del(&response->list);
		info->count_receive_queue--;

		mempool_free(response, info->response_mempool);
	}
	return -ENOMEM;
}

static void destroy_receive_buffers(struct smbd_connection *info)
{
	struct smbd_response *response;

	while ((response = get_receive_buffer(info)))
		mempool_free(response, info->response_mempool);

	while ((response = get_empty_queue_buffer(info)))
		mempool_free(response, info->response_mempool);
}

/*
 * Check and send an immediate or keep alive packet
 * The condition to send those packets are defined in [MS-SMBD] 3.1.1.1
 * Connection.KeepaliveRequested and Connection.SendImmediate
 * The idea is to extend credits to server as soon as it becomes available
 */
static void send_immediate_work(struct work_struct *work)
{
	struct smbd_connection *info = container_of(
					work, struct smbd_connection,
					send_immediate_work.work);

	if (info->keep_alive_requested == KEEP_ALIVE_PENDING ||
	    info->send_immediate) {
		log_keep_alive(INFO, "send an empty message\n");
		smbd_post_send_empty(info);
	}
}

/* Implement idle connection timer [MS-SMBD] 3.1.6.2 */
static void idle_connection_timer(struct work_struct *work)
{
	struct smbd_connection *info = container_of(
					work, struct smbd_connection,
					idle_timer_work.work);

	if (info->keep_alive_requested != KEEP_ALIVE_NONE) {
		log_keep_alive(ERR,
			"error status info->keep_alive_requested=%d\n",
			info->keep_alive_requested);
		smbd_disconnect_rdma_connection(info);
		return;
	}

	log_keep_alive(INFO, "about to send an empty idle message\n");
	smbd_post_send_empty(info);

	/* Setup the next idle timeout work */
	queue_delayed_work(info->workqueue, &info->idle_timer_work,
			info->keep_alive_interval*HZ);
}

static void destroy_caches_and_workqueue(struct smbd_connection *info)
{
	destroy_receive_buffers(info);
	destroy_workqueue(info->workqueue);
	mempool_destroy(info->response_mempool);
	kmem_cache_destroy(info->response_cache);
	mempool_destroy(info->request_mempool);
	kmem_cache_destroy(info->request_cache);
}

#define MAX_NAME_LEN	80
static int allocate_caches_and_workqueue(struct smbd_connection *info)
{
	char name[MAX_NAME_LEN];
	int rc;

	snprintf(name, MAX_NAME_LEN, "smbd_request_%p", info);
	info->request_cache =
		kmem_cache_create(
			name,
			sizeof(struct smbd_request) +
				sizeof(struct smbd_data_transfer),
			0, SLAB_HWCACHE_ALIGN, NULL);
	if (!info->request_cache)
		return -ENOMEM;

	info->request_mempool =
		mempool_create(info->send_credit_target, mempool_alloc_slab,
			mempool_free_slab, info->request_cache);
	if (!info->request_mempool)
		goto out1;

	snprintf(name, MAX_NAME_LEN, "smbd_response_%p", info);
	info->response_cache =
		kmem_cache_create(
			name,
			sizeof(struct smbd_response) +
				info->max_receive_size,
			0, SLAB_HWCACHE_ALIGN, NULL);
	if (!info->response_cache)
		goto out2;

	info->response_mempool =
		mempool_create(info->receive_credit_max, mempool_alloc_slab,
		       mempool_free_slab, info->response_cache);
	if (!info->response_mempool)
		goto out3;

	snprintf(name, MAX_NAME_LEN, "smbd_%p", info);
	info->workqueue = create_workqueue(name);
	if (!info->workqueue)
		goto out4;

	rc = allocate_receive_buffers(info, info->receive_credit_max);
	if (rc) {
		log_rdma_event(ERR, "failed to allocate receive buffers\n");
		goto out5;
	}

	return 0;

out5:
	destroy_workqueue(info->workqueue);
out4:
	mempool_destroy(info->response_mempool);
out3:
	kmem_cache_destroy(info->response_cache);
out2:
	mempool_destroy(info->request_mempool);
out1:
	kmem_cache_destroy(info->request_cache);
	return -ENOMEM;
}

/* Create a SMBD connection, called by upper layer */
struct smbd_connection *_smbd_get_connection(
	struct TCP_Server_Info *server, struct sockaddr *dstaddr, int port)
{
	int rc;
	struct smbd_connection *info;
	struct rdma_conn_param conn_param;
	struct ib_qp_init_attr qp_attr;
	struct sockaddr_in *addr_in = (struct sockaddr_in *) dstaddr;

	info = kzalloc(sizeof(struct smbd_connection), GFP_KERNEL);
	if (!info)
		return NULL;

	info->transport_status = SMBD_CONNECTING;
	rc = smbd_ia_open(info, dstaddr, port);
	if (rc) {
		log_rdma_event(INFO, "smbd_ia_open rc=%d\n", rc);
		goto create_id_failed;
	}

	if (smbd_send_credit_target > info->id->device->attrs.max_cqe ||
	    smbd_send_credit_target > info->id->device->attrs.max_qp_wr) {
		log_rdma_event(ERR,
			"consider lowering send_credit_target = %d. "
			"Possible CQE overrun, device "
			"reporting max_cpe %d max_qp_wr %d\n",
			smbd_send_credit_target,
			info->id->device->attrs.max_cqe,
			info->id->device->attrs.max_qp_wr);
		goto config_failed;
	}

	if (smbd_receive_credit_max > info->id->device->attrs.max_cqe ||
	    smbd_receive_credit_max > info->id->device->attrs.max_qp_wr) {
		log_rdma_event(ERR,
			"consider lowering receive_credit_max = %d. "
			"Possible CQE overrun, device "
			"reporting max_cpe %d max_qp_wr %d\n",
			smbd_receive_credit_max,
			info->id->device->attrs.max_cqe,
			info->id->device->attrs.max_qp_wr);
		goto config_failed;
	}

	info->receive_credit_max = smbd_receive_credit_max;
	info->send_credit_target = smbd_send_credit_target;
	info->max_send_size = smbd_max_send_size;
	info->max_fragmented_recv_size = smbd_max_fragmented_recv_size;
	info->max_receive_size = smbd_max_receive_size;
	info->keep_alive_interval = smbd_keep_alive_interval;

	if (info->id->device->attrs.max_sge < SMBDIRECT_MAX_SGE) {
		log_rdma_event(ERR, "warning: device max_sge = %d too small\n",
			info->id->device->attrs.max_sge);
		log_rdma_event(ERR, "Queue Pair creation may fail\n");
	}

	info->send_cq = NULL;
	info->recv_cq = NULL;
	info->send_cq = ib_alloc_cq(info->id->device, info,
			info->send_credit_target, 0, IB_POLL_SOFTIRQ);
	if (IS_ERR(info->send_cq)) {
		info->send_cq = NULL;
		goto alloc_cq_failed;
	}

	info->recv_cq = ib_alloc_cq(info->id->device, info,
			info->receive_credit_max, 0, IB_POLL_SOFTIRQ);
	if (IS_ERR(info->recv_cq)) {
		info->recv_cq = NULL;
		goto alloc_cq_failed;
	}

	memset(&qp_attr, 0, sizeof(qp_attr));
	qp_attr.event_handler = smbd_qp_async_error_upcall;
	qp_attr.qp_context = info;
	qp_attr.cap.max_send_wr = info->send_credit_target;
	qp_attr.cap.max_recv_wr = info->receive_credit_max;
	qp_attr.cap.max_send_sge = SMBDIRECT_MAX_SGE;
	qp_attr.cap.max_recv_sge = SMBDIRECT_MAX_SGE;
	qp_attr.cap.max_inline_data = 0;
	qp_attr.sq_sig_type = IB_SIGNAL_REQ_WR;
	qp_attr.qp_type = IB_QPT_RC;
	qp_attr.send_cq = info->send_cq;
	qp_attr.recv_cq = info->recv_cq;
	qp_attr.port_num = ~0;

	rc = rdma_create_qp(info->id, info->pd, &qp_attr);
	if (rc) {
		log_rdma_event(ERR, "rdma_create_qp failed %i\n", rc);
		goto create_qp_failed;
	}

	memset(&conn_param, 0, sizeof(conn_param));
	conn_param.initiator_depth = 0;

	conn_param.retry_count = SMBD_CM_RETRY;
	conn_param.rnr_retry_count = SMBD_CM_RNR_RETRY;
	conn_param.flow_control = 0;
	init_waitqueue_head(&info->wait_destroy);

	log_rdma_event(INFO, "connecting to IP %pI4 port %d\n",
		&addr_in->sin_addr, port);

	init_waitqueue_head(&info->conn_wait);
	rc = rdma_connect(info->id, &conn_param);
	if (rc) {
		log_rdma_event(ERR, "rdma_connect() failed with %i\n", rc);
		goto rdma_connect_failed;
	}

	wait_event_interruptible(
		info->conn_wait, info->transport_status != SMBD_CONNECTING);

	if (info->transport_status != SMBD_CONNECTED) {
		log_rdma_event(ERR, "rdma_connect failed port=%d\n", port);
		goto rdma_connect_failed;
	}

	log_rdma_event(INFO, "rdma_connect connected\n");

	rc = allocate_caches_and_workqueue(info);
	if (rc) {
		log_rdma_event(ERR, "cache allocation failed\n");
		goto allocate_cache_failed;
	}

	init_waitqueue_head(&info->wait_send_queue);
	init_waitqueue_head(&info->wait_reassembly_queue);

	INIT_DELAYED_WORK(&info->idle_timer_work, idle_connection_timer);
	INIT_DELAYED_WORK(&info->send_immediate_work, send_immediate_work);
	queue_delayed_work(info->workqueue, &info->idle_timer_work,
		info->keep_alive_interval*HZ);

	init_waitqueue_head(&info->wait_send_pending);
	atomic_set(&info->send_pending, 0);

	init_waitqueue_head(&info->wait_send_payload_pending);
	atomic_set(&info->send_payload_pending, 0);

	INIT_WORK(&info->disconnect_work, smbd_disconnect_rdma_work);
	INIT_WORK(&info->destroy_work, smbd_destroy_rdma_work);
	INIT_WORK(&info->recv_done_work, smbd_recv_done_work);
	INIT_WORK(&info->post_send_credits_work, smbd_post_send_credits);
	info->new_credits_offered = 0;
	spin_lock_init(&info->lock_new_credits_offered);

	rc = smbd_negotiate(info);
	if (rc) {
		log_rdma_event(ERR, "smbd_negotiate rc=%d\n", rc);
		goto negotiation_failed;
	}

	return info;

negotiation_failed:
	cancel_delayed_work_sync(&info->idle_timer_work);
	destroy_caches_and_workqueue(info);
	info->transport_status = SMBD_NEGOTIATE_FAILED;
	init_waitqueue_head(&info->conn_wait);
	rdma_disconnect(info->id);
	wait_event(info->conn_wait,
		info->transport_status == SMBD_DISCONNECTED);

allocate_cache_failed:
rdma_connect_failed:
	rdma_destroy_qp(info->id);

create_qp_failed:
alloc_cq_failed:
	if (info->send_cq)
		ib_free_cq(info->send_cq);
	if (info->recv_cq)
		ib_free_cq(info->recv_cq);

config_failed:
	ib_dealloc_pd(info->pd);
	rdma_destroy_id(info->id);

create_id_failed:
	kfree(info);
	return NULL;
}

struct smbd_connection *smbd_get_connection(
	struct TCP_Server_Info *server, struct sockaddr *dstaddr)
{
	struct smbd_connection *ret;
	int port = SMBD_PORT;

try_again:
	ret = _smbd_get_connection(server, dstaddr, port);

	/* Try SMB_PORT if SMBD_PORT doesn't work */
	if (!ret && port == SMBD_PORT) {
		port = SMB_PORT;
		goto try_again;
	}
	return ret;
}
