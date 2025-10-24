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
#include "smbdirect.h"
#include "cifs_debug.h"
#include "cifsproto.h"
#include "smb2proto.h"

/* Port numbers for SMBD transport */
#define SMB_PORT	445
#define SMBD_PORT	5445

/* Address lookup and resolve timeout in ms */
#define RDMA_RESOLVE_TIMEOUT	5000

/* SMBD negotiation timeout in seconds */
#define SMBD_NEGOTIATE_TIMEOUT	120

/* The timeout to wait for a keepalive message from peer in seconds */
#define KEEPALIVE_RECV_TIMEOUT 5

/*
 * Default maximum number of RDMA read/write outstanding on this connection
 * This value is possibly decreased during QP creation on hardware limit
 */
#define SMBD_CM_RESPONDER_RESOURCES	32

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

static int smbd_post_send_full_iter(struct smbdirect_socket *sc,
				    struct smbdirect_send_batch *batch,
				    struct iov_iter *iter,
				    u32 remaining_data_length)
{
	int bytes = 0;

	/*
	 * smbdirect_connection_send_single_iter() respects the
	 * negotiated max_send_size, so we need to
	 * loop until the full iter is posted
	 */

	while (iov_iter_count(iter) > 0) {
		int rc;

		rc = smbdirect_connection_send_single_iter(sc,
							   batch,
							   iter,
							   0, /* flags */
							   remaining_data_length);
		if (rc < 0)
			return rc;
		remaining_data_length -= rc;
		bytes += rc;
	}

	return bytes;
}

/*
 * Destroy the transport and related RDMA and memory resources
 * Need to go through all the pending counters and make sure on one is using
 * the transport while it is destroyed
 */
void smbd_destroy(struct TCP_Server_Info *server)
{
	struct smbd_connection *info = server->smbd_conn;

	if (!info) {
		log_rdma_event(INFO, "rdma session already destroyed\n");
		return;
	}

	smbdirect_socket_release(info->socket);

	destroy_workqueue(info->workqueue);
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
	log_rdma_event(INFO, "disconnecting transport\n");
	smbd_destroy(server);

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
	struct net *net = cifs_net_ns(server);
	struct smbd_connection *info;
	struct smbdirect_socket *sc;
	struct smbdirect_socket_parameters init_params = {};
	struct smbdirect_socket_parameters *sp;
	__be16 *sport;
	u64 port_flags = 0;
	char wq_name[80];
	int ret;

	switch (port) {
	case SMBD_PORT:
		/*
		 * only allow iWarp devices
		 * for port 5445.
		 */
		port_flags |= SMBDIRECT_FLAG_PORT_RANGE_ONLY_IW;
		break;
	case SMB_PORT:
		/*
		 * only allow InfiniBand, RoCEv1 or RoCEv2
		 * devices for port 445.
		 *
		 * (Basically don't allow iWarp devices)
		 */
		port_flags |= SMBDIRECT_FLAG_PORT_RANGE_ONLY_IB;
		break;
	}

	/*
	 * Create the initial parameters
	 */
	sp = &init_params;
	sp->flags = port_flags;
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
	scnprintf(wq_name, ARRAY_SIZE(wq_name), "smbd_%p", info);
	info->workqueue = create_workqueue(wq_name);
	if (!info->workqueue)
		goto create_wq_failed;
	ret = smbdirect_socket_create_kern(net, &sc);
	if (ret)
		goto socket_init_failed;
	smbdirect_socket_set_logging(sc, NULL, smbd_logging_needed, smbd_logging_vaprintf);
	ret = smbdirect_socket_set_initial_parameters(sc, sp);
	if (ret)
		goto set_params_failed;
	ret = smbdirect_socket_set_kernel_settings(sc, IB_POLL_SOFTIRQ, GFP_KERNEL);
	if (ret)
		goto set_settings_failed;
	ret = smbdirect_socket_set_custom_workqueue(sc, info->workqueue);
	if (ret)
		goto set_workqueue_failed;

	if (dstaddr->sa_family == AF_INET6)
		sport = &((struct sockaddr_in6 *)dstaddr)->sin6_port;
	else
		sport = &((struct sockaddr_in *)dstaddr)->sin_port;

	*sport = htons(port);

	ret = smbdirect_connect_sync(sc, dstaddr);
	if (ret) {
		log_rdma_event(ERR, "connect to %pISpsfc failed: %1pe\n",
			       dstaddr, ERR_PTR(ret));
		goto connect_failed;
	}

	info->socket = sc;
	return info;

connect_failed:
set_workqueue_failed:
set_settings_failed:
set_params_failed:
	smbdirect_socket_release(sc);
socket_init_failed:
	destroy_workqueue(info->workqueue);
create_wq_failed:
	kfree(info);
	return NULL;
}

const struct smbdirect_socket_parameters *smbd_get_parameters(struct smbd_connection *conn)
{
	if (unlikely(!conn->socket)) {
		static const struct smbdirect_socket_parameters zero_params;

		return &zero_params;
	}

	return smbdirect_socket_get_current_parameters(conn->socket);
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

	sp = smbd_get_parameters(ret);

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
	struct smbdirect_socket *sc = info->socket;

	if (!smbdirect_connection_is_connected(sc))
		return -ENOTCONN;

	return smbdirect_connection_recvmsg(sc, msg, 0);
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
	struct smbdirect_socket *sc = info->socket;
	const struct smbdirect_socket_parameters *sp = smbd_get_parameters(info);
	struct smb_rqst *rqst;
	struct iov_iter iter;
	struct smbdirect_send_batch_storage bstorage;
	struct smbdirect_send_batch *batch;
	unsigned int remaining_data_length, klen;
	int rc, i, rqst_idx;
	int error = 0;

	if (!smbdirect_connection_is_connected(sc))
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
	batch = smbdirect_init_send_batch_storage(&bstorage, false, 0);
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

		rc = smbd_post_send_full_iter(sc, batch, &iter, remaining_data_length);
		if (rc < 0) {
			error = rc;
			break;
		}
		remaining_data_length -= rc;

		if (iov_iter_count(&rqst->rq_iter) > 0) {
			/* And then the data pages if there are any */
			rc = smbd_post_send_full_iter(sc, batch, &rqst->rq_iter,
						      remaining_data_length);
			if (rc < 0) {
				error = rc;
				break;
			}
			remaining_data_length -= rc;
		}

	} while (++rqst_idx < num_rqst);

	rc = smbdirect_connection_send_batch_flush(sc, batch, true);
	if (unlikely(!rc && error))
		rc = error;

	/*
	 * As an optimization, we don't wait for individual I/O to finish
	 * before sending the next one.
	 * Send them all and wait for pending send count to get to 0
	 * that means all the I/Os have been out and we are good to return
	 */

	error = rc;
	rc = smbdirect_connection_send_wait_zero_pending(sc);
	if (unlikely(rc && !error))
		error = -EAGAIN;

	if (unlikely(error))
		return error;

	return 0;
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
	struct smbdirect_socket *sc = info->socket;

	if (!smbdirect_connection_is_connected(sc))
		return NULL;

	return smbdirect_connection_register_mr_io(sc, iter, writing, need_invalidate);
}

void smbd_mr_fill_buffer_descriptor(struct smbdirect_mr_io *mr,
				    struct smbdirect_buffer_descriptor_v1 *v1)
{
	smbdirect_mr_io_fill_buffer_descriptor(mr, v1);
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

void smbd_debug_proc_show(struct TCP_Server_Info *server, struct seq_file *m)
{
	if (!server->rdma)
		return;

	if (!server->smbd_conn) {
		seq_puts(m, "\nSMBDirect transport not available");
		return;
	}

	smbdirect_connection_legacy_debug_proc_show(server->smbd_conn->socket,
						    server->rdma_readwrite_threshold,
						    m);
}
