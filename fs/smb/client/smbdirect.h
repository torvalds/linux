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

#include "../common/smbdirect/smbdirect.h"
#include "../common/smbdirect/smbdirect_socket.h"

extern int rdma_readwrite_threshold;
extern int smbd_max_frmr_depth;
extern int smbd_keep_alive_interval;
extern int smbd_max_receive_size;
extern int smbd_max_fragmented_recv_size;
extern int smbd_max_send_size;
extern int smbd_send_credit_target;
extern int smbd_receive_credit_max;

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
	struct smbdirect_socket socket;


	/* Memory registrations */
	/* Maximum number of pages in a single RDMA write/read on this connection */
	int max_frmr_depth;
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

	struct workqueue_struct *workqueue;
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
	struct sg_table		sgt;
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
	struct smbd_connection *info, struct iov_iter *iter,
	bool writing, bool need_invalidate);
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
