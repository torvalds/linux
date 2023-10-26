/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) 2017, Microsoft Corporation.
 *
 *   Author(s): Long Li <longli@microsoft.com>
 */
#ifndef _SMBDIRECT_H
#define _SMBDIRECT_H

#ifdef CONFIG_CIFS_SMB_DIRECT
#define cifs_rdma_enabled(server)	((server)->rdma)

#include "cifsglob.h"
#include <rdma/ib_verbs.h>
#include <rdma/rdma_cm.h>
#include <linux/mempool.h>

extern int rdma_readwrite_threshold;
extern int smbd_max_frmr_depth;
extern int smbd_keep_alive_interval;
extern int smbd_max_receive_size;
extern int smbd_max_fragmented_recv_size;
extern int smbd_max_send_size;
extern int smbd_send_credit_target;
extern int smbd_receive_credit_max;

enum keep_alive_status {
	KEEP_ALIVE_NONE,
	KEEP_ALIVE_PENDING,
	KEEP_ALIVE_SENT,
};

enum smbd_connection_status {
	SMBD_CREATED,
	SMBD_CONNECTING,
	SMBD_CONNECTED,
	SMBD_NEGOTIATE_FAILED,
	SMBD_DISCONNECTING,
	SMBD_DISCONNECTED,
	SMBD_DESTROYED
};

/*
 * The context for the SMBDirect transport
 * Everything related to the transport is here. It has several logical parts
 * 1. RDMA related structures
 * 2. SMBDirect connection parameters
 * 3. Memory registrations
 * 4. Receive and reassembly queues for data receive path
 * 5. mempools for allocating packets
 */
struct smbd_connection {
	enum smbd_connection_status transport_status;

	/* RDMA related */
	struct rdma_cm_id *id;
	struct ib_qp_init_attr qp_attr;
	struct ib_pd *pd;
	struct ib_cq *send_cq, *recv_cq;
	struct ib_device_attr dev_attr;
	int ri_rc;
	struct completion ri_done;
	wait_queue_head_t conn_wait;
	wait_queue_head_t disconn_wait;

	struct completion negotiate_completion;
	bool negotiate_done;

	struct work_struct disconnect_work;
	struct work_struct post_send_credits_work;

	spinlock_t lock_new_credits_offered;
	int new_credits_offered;

	/* Connection parameters defined in [MS-SMBD] 3.1.1.1 */
	int receive_credit_max;
	int send_credit_target;
	int max_send_size;
	int max_fragmented_recv_size;
	int max_fragmented_send_size;
	int max_receive_size;
	int keep_alive_interval;
	int max_readwrite_size;
	enum keep_alive_status keep_alive_requested;
	int protocol;
	atomic_t send_credits;
	atomic_t receive_credits;
	int receive_credit_target;
	int fragment_reassembly_remaining;

	/* Memory registrations */
	/* Maximum number of RDMA read/write outstanding on this connection */
	int responder_resources;
	/* Maximum number of pages in a single RDMA write/read on this connection */
	int max_frmr_depth;
	/*
	 * If payload is less than or equal to the threshold,
	 * use RDMA send/recv to send upper layer I/O.
	 * If payload is more than the threshold,
	 * use RDMA read/write through memory registration for I/O.
	 */
	int rdma_readwrite_threshold;
	enum ib_mr_type mr_type;
	struct list_head mr_list;
	spinlock_t mr_list_lock;
	/* The number of available MRs ready for memory registration */
	atomic_t mr_ready_count;
	atomic_t mr_used_count;
	wait_queue_head_t wait_mr;
	struct work_struct mr_recovery_work;
	/* Used by transport to wait until all MRs are returned */
	wait_queue_head_t wait_for_mr_cleanup;

	/* Activity accoutning */
	atomic_t send_pending;
	wait_queue_head_t wait_send_pending;
	wait_queue_head_t wait_post_send;

	/* Receive queue */
	struct list_head receive_queue;
	int count_receive_queue;
	spinlock_t receive_queue_lock;

	struct list_head empty_packet_queue;
	int count_empty_packet_queue;
	spinlock_t empty_packet_queue_lock;

	wait_queue_head_t wait_receive_queues;

	/* Reassembly queue */
	struct list_head reassembly_queue;
	spinlock_t reassembly_queue_lock;
	wait_queue_head_t wait_reassembly_queue;

	/* total data length of reassembly queue */
	int reassembly_data_length;
	int reassembly_queue_length;
	/* the offset to first buffer in reassembly queue */
	int first_entry_offset;

	bool send_immediate;

	wait_queue_head_t wait_send_queue;

	/*
	 * Indicate if we have received a full packet on the connection
	 * This is used to identify the first SMBD packet of a assembled
	 * payload (SMB packet) in reassembly queue so we can return a
	 * RFC1002 length to upper layer to indicate the length of the SMB
	 * packet received
	 */
	bool full_packet_received;

	struct workqueue_struct *workqueue;
	struct delayed_work idle_timer_work;

	/* Memory pool for preallocating buffers */
	/* request pool for RDMA send */
	struct kmem_cache *request_cache;
	mempool_t *request_mempool;

	/* response pool for RDMA receive */
	struct kmem_cache *response_cache;
	mempool_t *response_mempool;

	/* for debug purposes */
	unsigned int count_get_receive_buffer;
	unsigned int count_put_receive_buffer;
	unsigned int count_reassembly_queue;
	unsigned int count_enqueue_reassembly_queue;
	unsigned int count_dequeue_reassembly_queue;
	unsigned int count_send_empty;
};

enum smbd_message_type {
	SMBD_NEGOTIATE_RESP,
	SMBD_TRANSFER_DATA,
};

#define SMB_DIRECT_RESPONSE_REQUESTED 0x0001

/* SMBD negotiation request packet [MS-SMBD] 2.2.1 */
struct smbd_negotiate_req {
	__le16 min_version;
	__le16 max_version;
	__le16 reserved;
	__le16 credits_requested;
	__le32 preferred_send_size;
	__le32 max_receive_size;
	__le32 max_fragmented_size;
} __packed;

/* SMBD negotiation response packet [MS-SMBD] 2.2.2 */
struct smbd_negotiate_resp {
	__le16 min_version;
	__le16 max_version;
	__le16 negotiated_version;
	__le16 reserved;
	__le16 credits_requested;
	__le16 credits_granted;
	__le32 status;
	__le32 max_readwrite_size;
	__le32 preferred_send_size;
	__le32 max_receive_size;
	__le32 max_fragmented_size;
} __packed;

/* SMBD data transfer packet with payload [MS-SMBD] 2.2.3 */
struct smbd_data_transfer {
	__le16 credits_requested;
	__le16 credits_granted;
	__le16 flags;
	__le16 reserved;
	__le32 remaining_data_length;
	__le32 data_offset;
	__le32 data_length;
	__le32 padding;
	__u8 buffer[];
} __packed;

/* The packet fields for a registered RDMA buffer */
struct smbd_buffer_descriptor_v1 {
	__le64 offset;
	__le32 token;
	__le32 length;
} __packed;

/* Maximum number of SGEs used by smbdirect.c in any send work request */
#define SMBDIRECT_MAX_SEND_SGE	6

/* The context for a SMBD request */
struct smbd_request {
	struct smbd_connection *info;
	struct ib_cqe cqe;

	/* the SGE entries for this work request */
	struct ib_sge sge[SMBDIRECT_MAX_SEND_SGE];
	int num_sge;

	/* SMBD packet header follows this structure */
	u8 packet[];
};

/* Maximum number of SGEs used by smbdirect.c in any receive work request */
#define SMBDIRECT_MAX_RECV_SGE	1

/* The context for a SMBD response */
struct smbd_response {
	struct smbd_connection *info;
	struct ib_cqe cqe;
	struct ib_sge sge;

	enum smbd_message_type type;

	/* Link to receive queue or reassembly queue */
	struct list_head list;

	/* Indicate if this is the 1st packet of a payload */
	bool first_segment;

	/* SMBD packet header and payload follows this structure */
	u8 packet[];
};

/* Create a SMBDirect session */
struct smbd_connection *smbd_get_connection(
	struct TCP_Server_Info *server, struct sockaddr *dstaddr);

/* Reconnect SMBDirect session */
int smbd_reconnect(struct TCP_Server_Info *server);
/* Destroy SMBDirect session */
void smbd_destroy(struct TCP_Server_Info *server);

/* Interface for carrying upper layer I/O through send/recv */
int smbd_recv(struct smbd_connection *info, struct msghdr *msg);
int smbd_send(struct TCP_Server_Info *server,
	int num_rqst, struct smb_rqst *rqst);

enum mr_state {
	MR_READY,
	MR_REGISTERED,
	MR_INVALIDATED,
	MR_ERROR
};

struct smbd_mr {
	struct smbd_connection	*conn;
	struct list_head	list;
	enum mr_state		state;
	struct ib_mr		*mr;
	struct scatterlist	*sgl;
	int			sgl_count;
	enum dma_data_direction	dir;
	union {
		struct ib_reg_wr	wr;
		struct ib_send_wr	inv_wr;
	};
	struct ib_cqe		cqe;
	bool			need_invalidate;
	struct completion	invalidate_done;
};

/* Interfaces to register and deregister MR for RDMA read/write */
struct smbd_mr *smbd_register_mr(
	struct smbd_connection *info, struct page *pages[], int num_pages,
	int offset, int tailsz, bool writing, bool need_invalidate);
int smbd_deregister_mr(struct smbd_mr *mr);

#else
#define cifs_rdma_enabled(server)	0
struct smbd_connection {};
static inline void *smbd_get_connection(
	struct TCP_Server_Info *server, struct sockaddr *dstaddr) {return NULL;}
static inline int smbd_reconnect(struct TCP_Server_Info *server) {return -1; }
static inline void smbd_destroy(struct TCP_Server_Info *server) {}
static inline int smbd_recv(struct smbd_connection *info, struct msghdr *msg) {return -1; }
static inline int smbd_send(struct TCP_Server_Info *server, int num_rqst, struct smb_rqst *rqst) {return -1; }
#endif

#endif
