// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2017, Microsoft Corporation.
 *
 *   Author(s): Long Li <longli@microsoft.com>
 */
#include <linux/module.h>
#include <linux/highmem.h>
#include "smbdirect.h"
#include "cifs_debug.h"
#include "cifsproto.h"
#include "smb2proto.h"

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
static int smbd_post_send_data(
		struct smbd_connection *info,
		struct kvec *iov, int n_vec, int remaining_data_length);
static int smbd_post_send_page(struct smbd_connection *info,
		struct page *page, unsigned long offset,
		size_t size, int remaining_data_length);

static void destroy_mr_list(struct smbd_connection *info);
static int allocate_mr_list(struct smbd_connection *info);

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
		wake_up_interruptible(&info->disconn_wait);
		wake_up_interruptible(&info->wait_reassembly_queue);
		wake_up_interruptible_all(&info->wait_send_queue);
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
		break;

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

	log_rdma_send(INFO, "smbd_request 0x%p completed wc->status=%d\n",
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

	if (atomic_dec_and_test(&request->info->send_pending))
		wake_up(&request->info->wait_send_pending);

	wake_up(&request->info->wait_post_send);

	mempool_free(request, request->info->request_mempool);
}

static void dump_smbd_negotiate_resp(struct smbd_negotiate_resp *resp)
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
	info->rdma_readwrite_threshold =
		rdma_readwrite_threshold > info->max_fragmented_send_size ?
		info->max_fragmented_send_size :
		rdma_readwrite_threshold;


	info->max_readwrite_size = min_t(u32,
			le32_to_cpu(packet->max_readwrite_size),
			info->max_frmr_depth * PAGE_SIZE);
	info->max_frmr_depth = info->max_readwrite_size / PAGE_SIZE;

	return true;
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

	/* Promptly send an immediate packet as defined in [MS-SMBD] 3.1.1.1 */
	info->send_immediate = true;
	if (atomic_read(&info->receive_credits) <
		info->receive_credit_target - 1) {
		if (info->keep_alive_requested == KEEP_ALIVE_PENDING ||
		    info->send_immediate) {
			log_keep_alive(INFO, "send an empty message\n");
			smbd_post_send_empty(info);
		}
	}
}

/* Called from softirq, when recv is done */
static void recv_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct smbd_data_transfer *data_transfer;
	struct smbd_response *response =
		container_of(wc->wr_cqe, struct smbd_response, cqe);
	struct smbd_connection *info = response->info;
	int data_length = 0;

	log_rdma_recv(INFO, "response=0x%p type=%d wc status=%d wc opcode %d byte_len=%d pkey_index=%u\n",
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
		if (le16_to_cpu(data_transfer->credits_granted)) {
			atomic_add(le16_to_cpu(data_transfer->credits_granted),
				&info->send_credits);
			/*
			 * We have new send credits granted from remote peer
			 * If any sender is waiting for credits, unblock it
			 */
			wake_up_interruptible(&info->wait_send_queue);
		}

		log_incoming(INFO, "data flags %d data_offset %d data_length %d remaining_data_length %d\n",
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
	rc = wait_for_completion_interruptible_timeout(
		&info->ri_done, msecs_to_jiffies(RDMA_RESOLVE_TIMEOUT));
	/* e.g. if interrupted returns -ERESTARTSYS */
	if (rc < 0) {
		log_rdma_event(ERR, "rdma_resolve_addr timeout rc: %i\n", rc);
		goto out;
	}
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
	rc = wait_for_completion_interruptible_timeout(
		&info->ri_done, msecs_to_jiffies(RDMA_RESOLVE_TIMEOUT));
	/* e.g. if interrupted returns -ERESTARTSYS */
	if (rc < 0)  {
		log_rdma_event(ERR, "rdma_resolve_addr timeout rc: %i\n", rc);
		goto out;
	}
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
		log_rdma_event(ERR, "Fast Registration Work Requests (FRWR) is not supported\n");
		log_rdma_event(ERR, "Device capability flags = %llx max_fast_reg_page_list_len = %u\n",
			       info->id->device->attrs.device_cap_flags,
			       info->id->device->attrs.max_fast_reg_page_list_len);
		rc = -EPROTONOSUPPORT;
		goto out2;
	}
	info->max_frmr_depth = min_t(int,
		smbd_max_frmr_depth,
		info->id->device->attrs.max_fast_reg_page_list_len);
	info->mr_type = IB_MR_TYPE_MEM_REG;
	if (info->id->device->attrs.kernel_cap_flags & IBK_SG_GAPS_REG)
		info->mr_type = IB_MR_TYPE_SG_GAPS;

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
	struct ib_send_wr send_wr;
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

	log_rdma_send(INFO, "sge addr=0x%llx length=%u lkey=0x%x\n",
		request->sge[0].addr,
		request->sge[0].length, request->sge[0].lkey);

	atomic_inc(&info->send_pending);
	rc = ib_post_send(info->id->qp, &send_wr, NULL);
	if (!rc)
		return 0;

	/* if we reach here, post send failed */
	log_rdma_send(ERR, "ib_post_send failed rc=%d\n", rc);
	atomic_dec(&info->send_pending);
	ib_dma_unmap_single(info->id->device, request->sge[0].addr,
		request->sge[0].length, DMA_TO_DEVICE);

	smbd_disconnect_rdma_connection(info);

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

/* Post the send request */
static int smbd_post_send(struct smbd_connection *info,
		struct smbd_request *request)
{
	struct ib_send_wr send_wr;
	int rc, i;

	for (i = 0; i < request->num_sge; i++) {
		log_rdma_send(INFO,
			"rdma_request sge[%d] addr=0x%llx length=%u\n",
			i, request->sge[i].addr, request->sge[i].length);
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

	rc = ib_post_send(info->id->qp, &send_wr, NULL);
	if (rc) {
		log_rdma_send(ERR, "ib_post_send failed rc=%d\n", rc);
		smbd_disconnect_rdma_connection(info);
		rc = -EAGAIN;
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
	int header_length;
	struct smbd_request *request;
	struct smbd_data_transfer *packet;
	int new_credits;
	struct scatterlist *sg;

wait_credit:
	/* Wait for send credits. A SMBD packet needs one credit */
	rc = wait_event_interruptible(info->wait_send_queue,
		atomic_read(&info->send_credits) > 0 ||
		info->transport_status != SMBD_CONNECTED);
	if (rc)
		goto err_wait_credit;

	if (info->transport_status != SMBD_CONNECTED) {
		log_outgoing(ERR, "disconnected not sending on wait_credit\n");
		rc = -EAGAIN;
		goto err_wait_credit;
	}
	if (unlikely(atomic_dec_return(&info->send_credits) < 0)) {
		atomic_inc(&info->send_credits);
		goto wait_credit;
	}

wait_send_queue:
	wait_event(info->wait_post_send,
		atomic_read(&info->send_pending) < info->send_credit_target ||
		info->transport_status != SMBD_CONNECTED);

	if (info->transport_status != SMBD_CONNECTED) {
		log_outgoing(ERR, "disconnected not sending on wait_send_queue\n");
		rc = -EAGAIN;
		goto err_wait_send_queue;
	}

	if (unlikely(atomic_inc_return(&info->send_pending) >
				info->send_credit_target)) {
		atomic_dec(&info->send_pending);
		goto wait_send_queue;
	}

	request = mempool_alloc(info->request_mempool, GFP_KERNEL);
	if (!request) {
		rc = -ENOMEM;
		goto err_alloc;
	}

	request->info = info;

	/* Fill in the packet header */
	packet = smbd_request_payload(request);
	packet->credits_requested = cpu_to_le16(info->send_credit_target);

	new_credits = manage_credits_prior_sending(info);
	atomic_add(new_credits, &info->receive_credits);
	packet->credits_granted = cpu_to_le16(new_credits);

	info->send_immediate = false;

	packet->flags = 0;
	if (manage_keep_alive_before_sending(info))
		packet->flags |= cpu_to_le16(SMB_DIRECT_RESPONSE_REQUESTED);

	packet->reserved = 0;
	if (!data_length)
		packet->data_offset = 0;
	else
		packet->data_offset = cpu_to_le32(24);
	packet->data_length = cpu_to_le32(data_length);
	packet->remaining_data_length = cpu_to_le32(remaining_data_length);
	packet->padding = 0;

	log_outgoing(INFO, "credits_requested=%d credits_granted=%d data_offset=%d data_length=%d remaining_data_length=%d\n",
		     le16_to_cpu(packet->credits_requested),
		     le16_to_cpu(packet->credits_granted),
		     le32_to_cpu(packet->data_offset),
		     le32_to_cpu(packet->data_length),
		     le32_to_cpu(packet->remaining_data_length));

	/* Map the packet to DMA */
	header_length = sizeof(struct smbd_data_transfer);
	/* If this is a packet without payload, don't send padding */
	if (!data_length)
		header_length = offsetof(struct smbd_data_transfer, padding);

	request->num_sge = 1;
	request->sge[0].addr = ib_dma_map_single(info->id->device,
						 (void *)packet,
						 header_length,
						 DMA_TO_DEVICE);
	if (ib_dma_mapping_error(info->id->device, request->sge[0].addr)) {
		rc = -EIO;
		request->sge[0].addr = 0;
		goto err_dma;
	}

	request->sge[0].length = header_length;
	request->sge[0].lkey = info->pd->local_dma_lkey;

	/* Fill in the packet data payload */
	num_sgs = sgl ? sg_nents(sgl) : 0;
	for_each_sg(sgl, sg, num_sgs, i) {
		request->sge[i+1].addr =
			ib_dma_map_page(info->id->device, sg_page(sg),
			       sg->offset, sg->length, DMA_TO_DEVICE);
		if (ib_dma_mapping_error(
				info->id->device, request->sge[i+1].addr)) {
			rc = -EIO;
			request->sge[i+1].addr = 0;
			goto err_dma;
		}
		request->sge[i+1].length = sg->length;
		request->sge[i+1].lkey = info->pd->local_dma_lkey;
		request->num_sge++;
	}

	rc = smbd_post_send(info, request);
	if (!rc)
		return 0;

err_dma:
	for (i = 0; i < request->num_sge; i++)
		if (request->sge[i].addr)
			ib_dma_unmap_single(info->id->device,
					    request->sge[i].addr,
					    request->sge[i].length,
					    DMA_TO_DEVICE);
	mempool_free(request, info->request_mempool);

	/* roll back receive credits and credits to be offered */
	spin_lock(&info->lock_new_credits_offered);
	info->new_credits_offered += new_credits;
	spin_unlock(&info->lock_new_credits_offered);
	atomic_sub(new_credits, &info->receive_credits);

err_alloc:
	if (atomic_dec_and_test(&info->send_pending))
		wake_up(&info->wait_send_pending);

err_wait_send_queue:
	/* roll back send credits and pending */
	atomic_inc(&info->send_credits);

err_wait_credit:
	return rc;
}

/*
 * Send a page
 * page: the page to send
 * offset: offset in the page to send
 * size: length in the page to send
 * remaining_data_length: remaining data to send in this payload
 */
static int smbd_post_send_page(struct smbd_connection *info, struct page *page,
		unsigned long offset, size_t size, int remaining_data_length)
{
	struct scatterlist sgl;

	sg_init_table(&sgl, 1);
	sg_set_page(&sgl, page, size, offset);

	return smbd_post_send_sgl(info, &sgl, size, remaining_data_length);
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
 * Send a data buffer
 * iov: the iov array describing the data buffers
 * n_vec: number of iov array
 * remaining_data_length: remaining data to send following this packet
 * in segmented SMBD packet
 */
static int smbd_post_send_data(
	struct smbd_connection *info, struct kvec *iov, int n_vec,
	int remaining_data_length)
{
	int i;
	u32 data_length = 0;
	struct scatterlist sgl[SMBDIRECT_MAX_SEND_SGE - 1];

	if (n_vec > SMBDIRECT_MAX_SEND_SGE - 1) {
		cifs_dbg(VFS, "Can't fit data to SGL, n_vec=%d\n", n_vec);
		return -EINVAL;
	}

	sg_init_table(sgl, n_vec);
	for (i = 0; i < n_vec; i++) {
		data_length += iov[i].iov_len;
		sg_set_buf(&sgl[i], iov[i].iov_base, iov[i].iov_len);
	}

	return smbd_post_send_sgl(info, sgl, data_length, remaining_data_length);
}

/*
 * Post a receive request to the transport
 * The remote peer can only send data when a receive request is posted
 * The interaction is controlled by send/receive credit system
 */
static int smbd_post_recv(
		struct smbd_connection *info, struct smbd_response *response)
{
	struct ib_recv_wr recv_wr;
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

	rc = ib_post_recv(info->id->qp, &recv_wr, NULL);
	if (rc) {
		ib_dma_unmap_single(info->id->device, response->sge.addr,
				    response->sge.length, DMA_FROM_DEVICE);
		smbd_disconnect_rdma_connection(info);
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
	log_rdma_event(INFO, "smbd_post_recv rc=%d iov.addr=0x%llx iov.length=%u iov.lkey=0x%x\n",
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

/*
 * Destroy the transport and related RDMA and memory resources
 * Need to go through all the pending counters and make sure on one is using
 * the transport while it is destroyed
 */
void smbd_destroy(struct TCP_Server_Info *server)
{
	struct smbd_connection *info = server->smbd_conn;
	struct smbd_response *response;
	unsigned long flags;

	if (!info) {
		log_rdma_event(INFO, "rdma session already destroyed\n");
		return;
	}

	log_rdma_event(INFO, "destroying rdma session\n");
	if (info->transport_status != SMBD_DISCONNECTED) {
		rdma_disconnect(server->smbd_conn->id);
		log_rdma_event(INFO, "wait for transport being disconnected\n");
		wait_event_interruptible(
			info->disconn_wait,
			info->transport_status == SMBD_DISCONNECTED);
	}

	log_rdma_event(INFO, "destroying qp\n");
	ib_drain_qp(info->id->qp);
	rdma_destroy_qp(info->id);

	log_rdma_event(INFO, "cancelling idle timer\n");
	cancel_delayed_work_sync(&info->idle_timer_work);

	log_rdma_event(INFO, "wait for all send posted to IB to finish\n");
	wait_event(info->wait_send_pending,
		atomic_read(&info->send_pending) == 0);

	/* It's not possible for upper layer to get to reassembly */
	log_rdma_event(INFO, "drain the reassembly queue\n");
	do {
		spin_lock_irqsave(&info->reassembly_queue_lock, flags);
		response = _get_first_reassembly(info);
		if (response) {
			list_del(&response->list);
			spin_unlock_irqrestore(
				&info->reassembly_queue_lock, flags);
			put_receive_buffer(info, response);
		} else
			spin_unlock_irqrestore(
				&info->reassembly_queue_lock, flags);
	} while (response);
	info->reassembly_data_length = 0;

	log_rdma_event(INFO, "free receive buffers\n");
	wait_event(info->wait_receive_queues,
		info->count_receive_queue + info->count_empty_packet_queue
			== info->receive_credit_max);
	destroy_receive_buffers(info);

	/*
	 * For performance reasons, memory registration and deregistration
	 * are not locked by srv_mutex. It is possible some processes are
	 * blocked on transport srv_mutex while holding memory registration.
	 * Release the transport srv_mutex to allow them to hit the failure
	 * path when sending data, and then release memory registartions.
	 */
	log_rdma_event(INFO, "freeing mr list\n");
	wake_up_interruptible_all(&info->wait_mr);
	while (atomic_read(&info->mr_used_count)) {
		cifs_server_unlock(server);
		msleep(1000);
		cifs_server_lock(server);
	}
	destroy_mr_list(info);

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

	destroy_workqueue(info->workqueue);
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
	if (server->smbd_conn->transport_status == SMBD_CONNECTED) {
		log_rdma_event(INFO, "disconnecting transport\n");
		smbd_destroy(server);
	}

create_conn:
	log_rdma_event(INFO, "creating rdma session\n");
	server->smbd_conn = smbd_get_connection(
		server, (struct sockaddr *) &server->dstaddr);

	if (server->smbd_conn)
		cifs_dbg(VFS, "RDMA transport re-established\n");

	return server->smbd_conn ? 0 : -ENOENT;
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

	scnprintf(name, MAX_NAME_LEN, "smbd_request_%p", info);
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

	scnprintf(name, MAX_NAME_LEN, "smbd_response_%p", info);
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

	scnprintf(name, MAX_NAME_LEN, "smbd_%p", info);
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
static struct smbd_connection *_smbd_get_connection(
	struct TCP_Server_Info *server, struct sockaddr *dstaddr, int port)
{
	int rc;
	struct smbd_connection *info;
	struct rdma_conn_param conn_param;
	struct ib_qp_init_attr qp_attr;
	struct sockaddr_in *addr_in = (struct sockaddr_in *) dstaddr;
	struct ib_port_immutable port_immutable;
	u32 ird_ord_hdr[2];

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
		log_rdma_event(ERR, "consider lowering send_credit_target = %d. Possible CQE overrun, device reporting max_cqe %d max_qp_wr %d\n",
			       smbd_send_credit_target,
			       info->id->device->attrs.max_cqe,
			       info->id->device->attrs.max_qp_wr);
		goto config_failed;
	}

	if (smbd_receive_credit_max > info->id->device->attrs.max_cqe ||
	    smbd_receive_credit_max > info->id->device->attrs.max_qp_wr) {
		log_rdma_event(ERR, "consider lowering receive_credit_max = %d. Possible CQE overrun, device reporting max_cqe %d max_qp_wr %d\n",
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

	if (info->id->device->attrs.max_send_sge < SMBDIRECT_MAX_SEND_SGE ||
	    info->id->device->attrs.max_recv_sge < SMBDIRECT_MAX_RECV_SGE) {
		log_rdma_event(ERR,
			"device %.*s max_send_sge/max_recv_sge = %d/%d too small\n",
			IB_DEVICE_NAME_MAX,
			info->id->device->name,
			info->id->device->attrs.max_send_sge,
			info->id->device->attrs.max_recv_sge);
		goto config_failed;
	}

	info->send_cq = NULL;
	info->recv_cq = NULL;
	info->send_cq =
		ib_alloc_cq_any(info->id->device, info,
				info->send_credit_target, IB_POLL_SOFTIRQ);
	if (IS_ERR(info->send_cq)) {
		info->send_cq = NULL;
		goto alloc_cq_failed;
	}

	info->recv_cq =
		ib_alloc_cq_any(info->id->device, info,
				info->receive_credit_max, IB_POLL_SOFTIRQ);
	if (IS_ERR(info->recv_cq)) {
		info->recv_cq = NULL;
		goto alloc_cq_failed;
	}

	memset(&qp_attr, 0, sizeof(qp_attr));
	qp_attr.event_handler = smbd_qp_async_error_upcall;
	qp_attr.qp_context = info;
	qp_attr.cap.max_send_wr = info->send_credit_target;
	qp_attr.cap.max_recv_wr = info->receive_credit_max;
	qp_attr.cap.max_send_sge = SMBDIRECT_MAX_SEND_SGE;
	qp_attr.cap.max_recv_sge = SMBDIRECT_MAX_RECV_SGE;
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

	conn_param.responder_resources =
		info->id->device->attrs.max_qp_rd_atom
			< SMBD_CM_RESPONDER_RESOURCES ?
		info->id->device->attrs.max_qp_rd_atom :
		SMBD_CM_RESPONDER_RESOURCES;
	info->responder_resources = conn_param.responder_resources;
	log_rdma_mr(INFO, "responder_resources=%d\n",
		info->responder_resources);

	/* Need to send IRD/ORD in private data for iWARP */
	info->id->device->ops.get_port_immutable(
		info->id->device, info->id->port_num, &port_immutable);
	if (port_immutable.core_cap_flags & RDMA_CORE_PORT_IWARP) {
		ird_ord_hdr[0] = info->responder_resources;
		ird_ord_hdr[1] = 1;
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

	init_waitqueue_head(&info->conn_wait);
	init_waitqueue_head(&info->disconn_wait);
	init_waitqueue_head(&info->wait_reassembly_queue);
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
	INIT_DELAYED_WORK(&info->idle_timer_work, idle_connection_timer);
	queue_delayed_work(info->workqueue, &info->idle_timer_work,
		info->keep_alive_interval*HZ);

	init_waitqueue_head(&info->wait_send_pending);
	atomic_set(&info->send_pending, 0);

	init_waitqueue_head(&info->wait_post_send);

	INIT_WORK(&info->disconnect_work, smbd_disconnect_rdma_work);
	INIT_WORK(&info->post_send_credits_work, smbd_post_send_credits);
	info->new_credits_offered = 0;
	spin_lock_init(&info->lock_new_credits_offered);

	rc = smbd_negotiate(info);
	if (rc) {
		log_rdma_event(ERR, "smbd_negotiate rc=%d\n", rc);
		goto negotiation_failed;
	}

	rc = allocate_mr_list(info);
	if (rc) {
		log_rdma_mr(ERR, "memory registration allocation failed\n");
		goto allocate_mr_failed;
	}

	return info;

allocate_mr_failed:
	/* At this point, need to a full transport shutdown */
	smbd_destroy(server);
	return NULL;

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

/*
 * Receive data from receive reassembly queue
 * All the incoming data packets are placed in reassembly queue
 * buf: the buffer to read data into
 * size: the length of data to read
 * return value: actual data read
 * Note: this implementation copies the data from reassebmly queue to receive
 * buffers used by upper layer. This is not the optimal code path. A better way
 * to do it is to not have upper layer allocate its receive buffers but rather
 * borrow the buffer from reassembly queue, and return it after data is
 * consumed. But this will require more changes to upper layer code, and also
 * need to consider packet boundaries while they still being reassembled.
 */
static int smbd_recv_buf(struct smbd_connection *info, char *buf,
		unsigned int size)
{
	struct smbd_response *response;
	struct smbd_data_transfer *data_transfer;
	int to_copy, to_read, data_read, offset;
	u32 data_length, remaining_data_length, data_offset;
	int rc;

again:
	/*
	 * No need to hold the reassembly queue lock all the time as we are
	 * the only one reading from the front of the queue. The transport
	 * may add more entries to the back of the queue at the same time
	 */
	log_read(INFO, "size=%d info->reassembly_data_length=%d\n", size,
		info->reassembly_data_length);
	if (info->reassembly_data_length >= size) {
		int queue_length;
		int queue_removed = 0;

		/*
		 * Need to make sure reassembly_data_length is read before
		 * reading reassembly_queue_length and calling
		 * _get_first_reassembly. This call is lock free
		 * as we never read at the end of the queue which are being
		 * updated in SOFTIRQ as more data is received
		 */
		virt_rmb();
		queue_length = info->reassembly_queue_length;
		data_read = 0;
		to_read = size;
		offset = info->first_entry_offset;
		while (data_read < size) {
			response = _get_first_reassembly(info);
			data_transfer = smbd_response_payload(response);
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
				*((__be32 *)buf) = cpu_to_be32(rfc1002_len);
				data_read = 4;
				response->first_segment = false;
				log_read(INFO, "returning rfc1002 length %d\n",
					rfc1002_len);
				goto read_rfc1002_done;
			}

			to_copy = min_t(int, data_length - offset, to_read);
			memcpy(
				buf + data_read,
				(char *)data_transfer + data_offset + offset,
				to_copy);

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
					spin_lock_irq(
						&info->reassembly_queue_lock);
					list_del(&response->list);
					spin_unlock_irq(
						&info->reassembly_queue_lock);
				}
				queue_removed++;
				info->count_reassembly_queue--;
				info->count_dequeue_reassembly_queue++;
				put_receive_buffer(info, response);
				offset = 0;
				log_read(INFO, "put_receive_buffer offset=0\n");
			} else
				offset += to_copy;

			to_read -= to_copy;
			data_read += to_copy;

			log_read(INFO, "_get_first_reassembly memcpy %d bytes data_transfer_length-offset=%d after that to_read=%d data_read=%d offset=%d\n",
				 to_copy, data_length - offset,
				 to_read, data_read, offset);
		}

		spin_lock_irq(&info->reassembly_queue_lock);
		info->reassembly_data_length -= data_read;
		info->reassembly_queue_length -= queue_removed;
		spin_unlock_irq(&info->reassembly_queue_lock);

		info->first_entry_offset = offset;
		log_read(INFO, "returning to thread data_read=%d reassembly_data_length=%d first_entry_offset=%d\n",
			 data_read, info->reassembly_data_length,
			 info->first_entry_offset);
read_rfc1002_done:
		return data_read;
	}

	log_read(INFO, "wait_event on more data\n");
	rc = wait_event_interruptible(
		info->wait_reassembly_queue,
		info->reassembly_data_length >= size ||
			info->transport_status != SMBD_CONNECTED);
	/* Don't return any data if interrupted */
	if (rc)
		return rc;

	if (info->transport_status != SMBD_CONNECTED) {
		log_read(ERR, "disconnected\n");
		return -ECONNABORTED;
	}

	goto again;
}

/*
 * Receive a page from receive reassembly queue
 * page: the page to read data into
 * to_read: the length of data to read
 * return value: actual data read
 */
static int smbd_recv_page(struct smbd_connection *info,
		struct page *page, unsigned int page_offset,
		unsigned int to_read)
{
	int ret;
	char *to_address;
	void *page_address;

	/* make sure we have the page ready for read */
	ret = wait_event_interruptible(
		info->wait_reassembly_queue,
		info->reassembly_data_length >= to_read ||
			info->transport_status != SMBD_CONNECTED);
	if (ret)
		return ret;

	/* now we can read from reassembly queue and not sleep */
	page_address = kmap_atomic(page);
	to_address = (char *) page_address + page_offset;

	log_read(INFO, "reading from page=%p address=%p to_read=%d\n",
		page, to_address, to_read);

	ret = smbd_recv_buf(info, to_address, to_read);
	kunmap_atomic(page_address);

	return ret;
}

/*
 * Receive data from transport
 * msg: a msghdr point to the buffer, can be ITER_KVEC or ITER_BVEC
 * return: total bytes read, or 0. SMB Direct will not do partial read.
 */
int smbd_recv(struct smbd_connection *info, struct msghdr *msg)
{
	char *buf;
	struct page *page;
	unsigned int to_read, page_offset;
	int rc;

	if (iov_iter_rw(&msg->msg_iter) == WRITE) {
		/* It's a bug in upper layer to get there */
		cifs_dbg(VFS, "Invalid msg iter dir %u\n",
			 iov_iter_rw(&msg->msg_iter));
		rc = -EINVAL;
		goto out;
	}

	switch (iov_iter_type(&msg->msg_iter)) {
	case ITER_KVEC:
		buf = msg->msg_iter.kvec->iov_base;
		to_read = msg->msg_iter.kvec->iov_len;
		rc = smbd_recv_buf(info, buf, to_read);
		break;

	case ITER_BVEC:
		page = msg->msg_iter.bvec->bv_page;
		page_offset = msg->msg_iter.bvec->bv_offset;
		to_read = msg->msg_iter.bvec->bv_len;
		rc = smbd_recv_page(info, page, page_offset, to_read);
		break;

	default:
		/* It's a bug in upper layer to get there */
		cifs_dbg(VFS, "Invalid msg type %d\n",
			 iov_iter_type(&msg->msg_iter));
		rc = -EINVAL;
	}

out:
	/* SMBDirect will read it all or nothing */
	if (rc > 0)
		msg->msg_iter.count = 0;
	return rc;
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
	struct kvec vecs[SMBDIRECT_MAX_SEND_SGE - 1];
	int nvecs;
	int size;
	unsigned int buflen, remaining_data_length;
	unsigned int offset, remaining_vec_data_length;
	int start, i, j;
	int max_iov_size =
		info->max_send_size - sizeof(struct smbd_data_transfer);
	struct kvec *iov;
	int rc;
	struct smb_rqst *rqst;
	int rqst_idx;

	if (info->transport_status != SMBD_CONNECTED)
		return -EAGAIN;

	/*
	 * Add in the page array if there is one. The caller needs to set
	 * rq_tailsz to PAGE_SIZE when the buffer has multiple pages and
	 * ends at page boundary
	 */
	remaining_data_length = 0;
	for (i = 0; i < num_rqst; i++)
		remaining_data_length += smb_rqst_len(server, &rqst_array[i]);

	if (unlikely(remaining_data_length > info->max_fragmented_send_size)) {
		/* assertion: payload never exceeds negotiated maximum */
		log_write(ERR, "payload size %d > max size %d\n",
			remaining_data_length, info->max_fragmented_send_size);
		return -EINVAL;
	}

	log_write(INFO, "num_rqst=%d total length=%u\n",
			num_rqst, remaining_data_length);

	rqst_idx = 0;
	do {
		rqst = &rqst_array[rqst_idx];
		iov = rqst->rq_iov;

		cifs_dbg(FYI, "Sending smb (RDMA): idx=%d smb_len=%lu\n",
			rqst_idx, smb_rqst_len(server, rqst));
		remaining_vec_data_length = 0;
		for (i = 0; i < rqst->rq_nvec; i++) {
			remaining_vec_data_length += iov[i].iov_len;
			dump_smb(iov[i].iov_base, iov[i].iov_len);
		}

		log_write(INFO, "rqst_idx=%d nvec=%d rqst->rq_npages=%d rq_pagesz=%d rq_tailsz=%d buflen=%lu\n",
			  rqst_idx, rqst->rq_nvec,
			  rqst->rq_npages, rqst->rq_pagesz,
			  rqst->rq_tailsz, smb_rqst_len(server, rqst));

		start = 0;
		offset = 0;
		do {
			buflen = 0;
			i = start;
			j = 0;
			while (i < rqst->rq_nvec &&
				j < SMBDIRECT_MAX_SEND_SGE - 1 &&
				buflen < max_iov_size) {

				vecs[j].iov_base = iov[i].iov_base + offset;
				if (buflen + iov[i].iov_len > max_iov_size) {
					vecs[j].iov_len =
						max_iov_size - iov[i].iov_len;
					buflen = max_iov_size;
					offset = vecs[j].iov_len;
				} else {
					vecs[j].iov_len =
						iov[i].iov_len - offset;
					buflen += vecs[j].iov_len;
					offset = 0;
					++i;
				}
				++j;
			}

			remaining_vec_data_length -= buflen;
			remaining_data_length -= buflen;
			log_write(INFO, "sending %s iov[%d] from start=%d nvecs=%d remaining_data_length=%d\n",
					remaining_vec_data_length > 0 ?
						"partial" : "complete",
					rqst->rq_nvec, start, j,
					remaining_data_length);

			start = i;
			rc = smbd_post_send_data(info, vecs, j, remaining_data_length);
			if (rc)
				goto done;
		} while (remaining_vec_data_length > 0);

		/* now sending pages if there are any */
		for (i = 0; i < rqst->rq_npages; i++) {
			rqst_page_get_length(rqst, i, &buflen, &offset);
			nvecs = (buflen + max_iov_size - 1) / max_iov_size;
			log_write(INFO, "sending pages buflen=%d nvecs=%d\n",
				buflen, nvecs);
			for (j = 0; j < nvecs; j++) {
				size = min_t(unsigned int, max_iov_size, remaining_data_length);
				remaining_data_length -= size;
				log_write(INFO, "sending pages i=%d offset=%d size=%d remaining_data_length=%d\n",
					  i, j * max_iov_size + offset, size,
					  remaining_data_length);
				rc = smbd_post_send_page(
					info, rqst->rq_pages[i],
					j*max_iov_size + offset,
					size, remaining_data_length);
				if (rc)
					goto done;
			}
		}
	} while (++rqst_idx < num_rqst);

done:
	/*
	 * As an optimization, we don't wait for individual I/O to finish
	 * before sending the next one.
	 * Send them all and wait for pending send count to get to 0
	 * that means all the I/Os have been out and we are good to return
	 */

	wait_event(info->wait_send_pending,
		atomic_read(&info->send_pending) == 0);

	return rc;
}

static void register_mr_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct smbd_mr *mr;
	struct ib_cqe *cqe;

	if (wc->status) {
		log_rdma_mr(ERR, "status=%d\n", wc->status);
		cqe = wc->wr_cqe;
		mr = container_of(cqe, struct smbd_mr, cqe);
		smbd_disconnect_rdma_connection(mr->conn);
	}
}

/*
 * The work queue function that recovers MRs
 * We need to call ib_dereg_mr() and ib_alloc_mr() before this MR can be used
 * again. Both calls are slow, so finish them in a workqueue. This will not
 * block I/O path.
 * There is one workqueue that recovers MRs, there is no need to lock as the
 * I/O requests calling smbd_register_mr will never update the links in the
 * mr_list.
 */
static void smbd_mr_recovery_work(struct work_struct *work)
{
	struct smbd_connection *info =
		container_of(work, struct smbd_connection, mr_recovery_work);
	struct smbd_mr *smbdirect_mr;
	int rc;

	list_for_each_entry(smbdirect_mr, &info->mr_list, list) {
		if (smbdirect_mr->state == MR_ERROR) {

			/* recover this MR entry */
			rc = ib_dereg_mr(smbdirect_mr->mr);
			if (rc) {
				log_rdma_mr(ERR,
					"ib_dereg_mr failed rc=%x\n",
					rc);
				smbd_disconnect_rdma_connection(info);
				continue;
			}

			smbdirect_mr->mr = ib_alloc_mr(
				info->pd, info->mr_type,
				info->max_frmr_depth);
			if (IS_ERR(smbdirect_mr->mr)) {
				log_rdma_mr(ERR, "ib_alloc_mr failed mr_type=%x max_frmr_depth=%x\n",
					    info->mr_type,
					    info->max_frmr_depth);
				smbd_disconnect_rdma_connection(info);
				continue;
			}
		} else
			/* This MR is being used, don't recover it */
			continue;

		smbdirect_mr->state = MR_READY;

		/* smbdirect_mr->state is updated by this function
		 * and is read and updated by I/O issuing CPUs trying
		 * to get a MR, the call to atomic_inc_return
		 * implicates a memory barrier and guarantees this
		 * value is updated before waking up any calls to
		 * get_mr() from the I/O issuing CPUs
		 */
		if (atomic_inc_return(&info->mr_ready_count) == 1)
			wake_up_interruptible(&info->wait_mr);
	}
}

static void destroy_mr_list(struct smbd_connection *info)
{
	struct smbd_mr *mr, *tmp;

	cancel_work_sync(&info->mr_recovery_work);
	list_for_each_entry_safe(mr, tmp, &info->mr_list, list) {
		if (mr->state == MR_INVALIDATED)
			ib_dma_unmap_sg(info->id->device, mr->sgl,
				mr->sgl_count, mr->dir);
		ib_dereg_mr(mr->mr);
		kfree(mr->sgl);
		kfree(mr);
	}
}

/*
 * Allocate MRs used for RDMA read/write
 * The number of MRs will not exceed hardware capability in responder_resources
 * All MRs are kept in mr_list. The MR can be recovered after it's used
 * Recovery is done in smbd_mr_recovery_work. The content of list entry changes
 * as MRs are used and recovered for I/O, but the list links will not change
 */
static int allocate_mr_list(struct smbd_connection *info)
{
	int i;
	struct smbd_mr *smbdirect_mr, *tmp;

	INIT_LIST_HEAD(&info->mr_list);
	init_waitqueue_head(&info->wait_mr);
	spin_lock_init(&info->mr_list_lock);
	atomic_set(&info->mr_ready_count, 0);
	atomic_set(&info->mr_used_count, 0);
	init_waitqueue_head(&info->wait_for_mr_cleanup);
	/* Allocate more MRs (2x) than hardware responder_resources */
	for (i = 0; i < info->responder_resources * 2; i++) {
		smbdirect_mr = kzalloc(sizeof(*smbdirect_mr), GFP_KERNEL);
		if (!smbdirect_mr)
			goto out;
		smbdirect_mr->mr = ib_alloc_mr(info->pd, info->mr_type,
					info->max_frmr_depth);
		if (IS_ERR(smbdirect_mr->mr)) {
			log_rdma_mr(ERR, "ib_alloc_mr failed mr_type=%x max_frmr_depth=%x\n",
				    info->mr_type, info->max_frmr_depth);
			goto out;
		}
		smbdirect_mr->sgl = kcalloc(
					info->max_frmr_depth,
					sizeof(struct scatterlist),
					GFP_KERNEL);
		if (!smbdirect_mr->sgl) {
			log_rdma_mr(ERR, "failed to allocate sgl\n");
			ib_dereg_mr(smbdirect_mr->mr);
			goto out;
		}
		smbdirect_mr->state = MR_READY;
		smbdirect_mr->conn = info;

		list_add_tail(&smbdirect_mr->list, &info->mr_list);
		atomic_inc(&info->mr_ready_count);
	}
	INIT_WORK(&info->mr_recovery_work, smbd_mr_recovery_work);
	return 0;

out:
	kfree(smbdirect_mr);

	list_for_each_entry_safe(smbdirect_mr, tmp, &info->mr_list, list) {
		ib_dereg_mr(smbdirect_mr->mr);
		kfree(smbdirect_mr->sgl);
		kfree(smbdirect_mr);
	}
	return -ENOMEM;
}

/*
 * Get a MR from mr_list. This function waits until there is at least one
 * MR available in the list. It may access the list while the
 * smbd_mr_recovery_work is recovering the MR list. This doesn't need a lock
 * as they never modify the same places. However, there may be several CPUs
 * issueing I/O trying to get MR at the same time, mr_list_lock is used to
 * protect this situation.
 */
static struct smbd_mr *get_mr(struct smbd_connection *info)
{
	struct smbd_mr *ret;
	int rc;
again:
	rc = wait_event_interruptible(info->wait_mr,
		atomic_read(&info->mr_ready_count) ||
		info->transport_status != SMBD_CONNECTED);
	if (rc) {
		log_rdma_mr(ERR, "wait_event_interruptible rc=%x\n", rc);
		return NULL;
	}

	if (info->transport_status != SMBD_CONNECTED) {
		log_rdma_mr(ERR, "info->transport_status=%x\n",
			info->transport_status);
		return NULL;
	}

	spin_lock(&info->mr_list_lock);
	list_for_each_entry(ret, &info->mr_list, list) {
		if (ret->state == MR_READY) {
			ret->state = MR_REGISTERED;
			spin_unlock(&info->mr_list_lock);
			atomic_dec(&info->mr_ready_count);
			atomic_inc(&info->mr_used_count);
			return ret;
		}
	}

	spin_unlock(&info->mr_list_lock);
	/*
	 * It is possible that we could fail to get MR because other processes may
	 * try to acquire a MR at the same time. If this is the case, retry it.
	 */
	goto again;
}

/*
 * Register memory for RDMA read/write
 * pages[]: the list of pages to register memory with
 * num_pages: the number of pages to register
 * tailsz: if non-zero, the bytes to register in the last page
 * writing: true if this is a RDMA write (SMB read), false for RDMA read
 * need_invalidate: true if this MR needs to be locally invalidated after I/O
 * return value: the MR registered, NULL if failed.
 */
struct smbd_mr *smbd_register_mr(
	struct smbd_connection *info, struct page *pages[], int num_pages,
	int offset, int tailsz, bool writing, bool need_invalidate)
{
	struct smbd_mr *smbdirect_mr;
	int rc, i;
	enum dma_data_direction dir;
	struct ib_reg_wr *reg_wr;

	if (num_pages > info->max_frmr_depth) {
		log_rdma_mr(ERR, "num_pages=%d max_frmr_depth=%d\n",
			num_pages, info->max_frmr_depth);
		return NULL;
	}

	smbdirect_mr = get_mr(info);
	if (!smbdirect_mr) {
		log_rdma_mr(ERR, "get_mr returning NULL\n");
		return NULL;
	}
	smbdirect_mr->need_invalidate = need_invalidate;
	smbdirect_mr->sgl_count = num_pages;
	sg_init_table(smbdirect_mr->sgl, num_pages);

	log_rdma_mr(INFO, "num_pages=0x%x offset=0x%x tailsz=0x%x\n",
			num_pages, offset, tailsz);

	if (num_pages == 1) {
		sg_set_page(&smbdirect_mr->sgl[0], pages[0], tailsz, offset);
		goto skip_multiple_pages;
	}

	/* We have at least two pages to register */
	sg_set_page(
		&smbdirect_mr->sgl[0], pages[0], PAGE_SIZE - offset, offset);
	i = 1;
	while (i < num_pages - 1) {
		sg_set_page(&smbdirect_mr->sgl[i], pages[i], PAGE_SIZE, 0);
		i++;
	}
	sg_set_page(&smbdirect_mr->sgl[i], pages[i],
		tailsz ? tailsz : PAGE_SIZE, 0);

skip_multiple_pages:
	dir = writing ? DMA_FROM_DEVICE : DMA_TO_DEVICE;
	smbdirect_mr->dir = dir;
	rc = ib_dma_map_sg(info->id->device, smbdirect_mr->sgl, num_pages, dir);
	if (!rc) {
		log_rdma_mr(ERR, "ib_dma_map_sg num_pages=%x dir=%x rc=%x\n",
			num_pages, dir, rc);
		goto dma_map_error;
	}

	rc = ib_map_mr_sg(smbdirect_mr->mr, smbdirect_mr->sgl, num_pages,
		NULL, PAGE_SIZE);
	if (rc != num_pages) {
		log_rdma_mr(ERR,
			"ib_map_mr_sg failed rc = %d num_pages = %x\n",
			rc, num_pages);
		goto map_mr_error;
	}

	ib_update_fast_reg_key(smbdirect_mr->mr,
		ib_inc_rkey(smbdirect_mr->mr->rkey));
	reg_wr = &smbdirect_mr->wr;
	reg_wr->wr.opcode = IB_WR_REG_MR;
	smbdirect_mr->cqe.done = register_mr_done;
	reg_wr->wr.wr_cqe = &smbdirect_mr->cqe;
	reg_wr->wr.num_sge = 0;
	reg_wr->wr.send_flags = IB_SEND_SIGNALED;
	reg_wr->mr = smbdirect_mr->mr;
	reg_wr->key = smbdirect_mr->mr->rkey;
	reg_wr->access = writing ?
			IB_ACCESS_REMOTE_WRITE | IB_ACCESS_LOCAL_WRITE :
			IB_ACCESS_REMOTE_READ;

	/*
	 * There is no need for waiting for complemtion on ib_post_send
	 * on IB_WR_REG_MR. Hardware enforces a barrier and order of execution
	 * on the next ib_post_send when we actaully send I/O to remote peer
	 */
	rc = ib_post_send(info->id->qp, &reg_wr->wr, NULL);
	if (!rc)
		return smbdirect_mr;

	log_rdma_mr(ERR, "ib_post_send failed rc=%x reg_wr->key=%x\n",
		rc, reg_wr->key);

	/* If all failed, attempt to recover this MR by setting it MR_ERROR*/
map_mr_error:
	ib_dma_unmap_sg(info->id->device, smbdirect_mr->sgl,
		smbdirect_mr->sgl_count, smbdirect_mr->dir);

dma_map_error:
	smbdirect_mr->state = MR_ERROR;
	if (atomic_dec_and_test(&info->mr_used_count))
		wake_up(&info->wait_for_mr_cleanup);

	smbd_disconnect_rdma_connection(info);

	return NULL;
}

static void local_inv_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct smbd_mr *smbdirect_mr;
	struct ib_cqe *cqe;

	cqe = wc->wr_cqe;
	smbdirect_mr = container_of(cqe, struct smbd_mr, cqe);
	smbdirect_mr->state = MR_INVALIDATED;
	if (wc->status != IB_WC_SUCCESS) {
		log_rdma_mr(ERR, "invalidate failed status=%x\n", wc->status);
		smbdirect_mr->state = MR_ERROR;
	}
	complete(&smbdirect_mr->invalidate_done);
}

/*
 * Deregister a MR after I/O is done
 * This function may wait if remote invalidation is not used
 * and we have to locally invalidate the buffer to prevent data is being
 * modified by remote peer after upper layer consumes it
 */
int smbd_deregister_mr(struct smbd_mr *smbdirect_mr)
{
	struct ib_send_wr *wr;
	struct smbd_connection *info = smbdirect_mr->conn;
	int rc = 0;

	if (smbdirect_mr->need_invalidate) {
		/* Need to finish local invalidation before returning */
		wr = &smbdirect_mr->inv_wr;
		wr->opcode = IB_WR_LOCAL_INV;
		smbdirect_mr->cqe.done = local_inv_done;
		wr->wr_cqe = &smbdirect_mr->cqe;
		wr->num_sge = 0;
		wr->ex.invalidate_rkey = smbdirect_mr->mr->rkey;
		wr->send_flags = IB_SEND_SIGNALED;

		init_completion(&smbdirect_mr->invalidate_done);
		rc = ib_post_send(info->id->qp, wr, NULL);
		if (rc) {
			log_rdma_mr(ERR, "ib_post_send failed rc=%x\n", rc);
			smbd_disconnect_rdma_connection(info);
			goto done;
		}
		wait_for_completion(&smbdirect_mr->invalidate_done);
		smbdirect_mr->need_invalidate = false;
	} else
		/*
		 * For remote invalidation, just set it to MR_INVALIDATED
		 * and defer to mr_recovery_work to recover the MR for next use
		 */
		smbdirect_mr->state = MR_INVALIDATED;

	if (smbdirect_mr->state == MR_INVALIDATED) {
		ib_dma_unmap_sg(
			info->id->device, smbdirect_mr->sgl,
			smbdirect_mr->sgl_count,
			smbdirect_mr->dir);
		smbdirect_mr->state = MR_READY;
		if (atomic_inc_return(&info->mr_ready_count) == 1)
			wake_up_interruptible(&info->wait_mr);
	} else
		/*
		 * Schedule the work to do MR recovery for future I/Os MR
		 * recovery is slow and don't want it to block current I/O
		 */
		queue_work(info->workqueue, &info->mr_recovery_work);

done:
	if (atomic_dec_and_test(&info->mr_used_count))
		wake_up(&info->wait_for_mr_cleanup);

	return rc;
}
