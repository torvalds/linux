/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (c) 2025 Stefan Metzmacher
 */

#ifndef __FS_SMB_COMMON_SMBDIRECT_SMBDIRECT_SOCKET_H__
#define __FS_SMB_COMMON_SMBDIRECT_SMBDIRECT_SOCKET_H__

enum smbdirect_socket_status {
	SMBDIRECT_SOCKET_CREATED,
	SMBDIRECT_SOCKET_CONNECTING,
	SMBDIRECT_SOCKET_CONNECTED,
	SMBDIRECT_SOCKET_NEGOTIATE_FAILED,
	SMBDIRECT_SOCKET_DISCONNECTING,
	SMBDIRECT_SOCKET_DISCONNECTED,
	SMBDIRECT_SOCKET_DESTROYED
};

static __always_inline
const char *smbdirect_socket_status_string(enum smbdirect_socket_status status)
{
	switch (status) {
	case SMBDIRECT_SOCKET_CREATED:
		return "CREATED";
	case SMBDIRECT_SOCKET_CONNECTING:
		return "CONNECTING";
	case SMBDIRECT_SOCKET_CONNECTED:
		return "CONNECTED";
	case SMBDIRECT_SOCKET_NEGOTIATE_FAILED:
		return "NEGOTIATE_FAILED";
	case SMBDIRECT_SOCKET_DISCONNECTING:
		return "DISCONNECTING";
	case SMBDIRECT_SOCKET_DISCONNECTED:
		return "DISCONNECTED";
	case SMBDIRECT_SOCKET_DESTROYED:
		return "DESTROYED";
	}

	return "<unknown>";
}

struct smbdirect_socket {
	enum smbdirect_socket_status status;
	wait_queue_head_t status_wait;

	/* RDMA related */
	struct {
		struct rdma_cm_id *cm_id;
	} rdma;

	/* IB verbs related */
	struct {
		struct ib_pd *pd;
		struct ib_cq *send_cq;
		struct ib_cq *recv_cq;

		/*
		 * shortcuts for rdma.cm_id->{qp,device};
		 */
		struct ib_qp *qp;
		struct ib_device *dev;
	} ib;

	struct smbdirect_socket_parameters parameters;

	/*
	 * The state for posted send buffers
	 */
	struct {
		/*
		 * Memory pools for preallocating
		 * smbdirect_send_io buffers
		 */
		struct {
			struct kmem_cache	*cache;
			mempool_t		*pool;
		} mem;
	} send_io;

	/*
	 * The state for posted receive buffers
	 */
	struct {
		/*
		 * The type of PDU we are expecting
		 */
		enum {
			SMBDIRECT_EXPECT_NEGOTIATE_REQ = 1,
			SMBDIRECT_EXPECT_NEGOTIATE_REP = 2,
			SMBDIRECT_EXPECT_DATA_TRANSFER = 3,
		} expected;

		/*
		 * Memory pools for preallocating
		 * smbdirect_recv_io buffers
		 */
		struct {
			struct kmem_cache	*cache;
			mempool_t		*pool;
		} mem;

		/*
		 * The list of free smbdirect_recv_io
		 * structures
		 */
		struct {
			struct list_head list;
			spinlock_t lock;
		} free;

		/*
		 * The list of arrived non-empty smbdirect_recv_io
		 * structures
		 *
		 * This represents the reassembly queue.
		 */
		struct {
			struct list_head list;
			spinlock_t lock;
			wait_queue_head_t wait_queue;
			/* total data length of reassembly queue */
			int data_length;
			int queue_length;
			/* the offset to first buffer in reassembly queue */
			int first_entry_offset;
			/*
			 * Indicate if we have received a full packet on the
			 * connection This is used to identify the first SMBD
			 * packet of a assembled payload (SMB packet) in
			 * reassembly queue so we can return a RFC1002 length to
			 * upper layer to indicate the length of the SMB packet
			 * received
			 */
			bool full_packet_received;
		} reassembly;
	} recv_io;
};

static __always_inline void smbdirect_socket_init(struct smbdirect_socket *sc)
{
	/*
	 * This also sets status = SMBDIRECT_SOCKET_CREATED
	 */
	BUILD_BUG_ON(SMBDIRECT_SOCKET_CREATED != 0);
	memset(sc, 0, sizeof(*sc));

	init_waitqueue_head(&sc->status_wait);

	INIT_LIST_HEAD(&sc->recv_io.free.list);
	spin_lock_init(&sc->recv_io.free.lock);

	INIT_LIST_HEAD(&sc->recv_io.reassembly.list);
	spin_lock_init(&sc->recv_io.reassembly.lock);
	init_waitqueue_head(&sc->recv_io.reassembly.wait_queue);
}

struct smbdirect_send_io {
	struct smbdirect_socket *socket;
	struct ib_cqe cqe;

	/*
	 * The SGE entries for this work request
	 *
	 * The first points to the packet header
	 */
#define SMBDIRECT_SEND_IO_MAX_SGE 6
	size_t num_sge;
	struct ib_sge sge[SMBDIRECT_SEND_IO_MAX_SGE];

	/*
	 * Link to the list of sibling smbdirect_send_io
	 * messages.
	 */
	struct list_head sibling_list;
	struct ib_send_wr wr;

	/* SMBD packet header follows this structure */
	u8 packet[];
};

struct smbdirect_recv_io {
	struct smbdirect_socket *socket;
	struct ib_cqe cqe;

	/*
	 * For now we only use a single SGE
	 * as we have just one large buffer
	 * per posted recv.
	 */
#define SMBDIRECT_RECV_IO_MAX_SGE 1
	struct ib_sge sge;

	/* Link to free or reassembly list */
	struct list_head list;

	/* Indicate if this is the 1st packet of a payload */
	bool first_segment;

	/* SMBD packet header and payload follows this structure */
	u8 packet[];
};

#endif /* __FS_SMB_COMMON_SMBDIRECT_SMBDIRECT_SOCKET_H__ */
