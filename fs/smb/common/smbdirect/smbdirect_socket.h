/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (c) 2025 Stefan Metzmacher
 */

#ifndef __FS_SMB_COMMON_SMBDIRECT_SMBDIRECT_SOCKET_H__
#define __FS_SMB_COMMON_SMBDIRECT_SMBDIRECT_SOCKET_H__

#include <rdma/rw.h>

enum smbdirect_socket_status {
	SMBDIRECT_SOCKET_CREATED,
	SMBDIRECT_SOCKET_RESOLVE_ADDR_NEEDED,
	SMBDIRECT_SOCKET_RESOLVE_ADDR_RUNNING,
	SMBDIRECT_SOCKET_RESOLVE_ADDR_FAILED,
	SMBDIRECT_SOCKET_RESOLVE_ROUTE_NEEDED,
	SMBDIRECT_SOCKET_RESOLVE_ROUTE_RUNNING,
	SMBDIRECT_SOCKET_RESOLVE_ROUTE_FAILED,
	SMBDIRECT_SOCKET_RDMA_CONNECT_NEEDED,
	SMBDIRECT_SOCKET_RDMA_CONNECT_RUNNING,
	SMBDIRECT_SOCKET_RDMA_CONNECT_FAILED,
	SMBDIRECT_SOCKET_NEGOTIATE_NEEDED,
	SMBDIRECT_SOCKET_NEGOTIATE_RUNNING,
	SMBDIRECT_SOCKET_NEGOTIATE_FAILED,
	SMBDIRECT_SOCKET_CONNECTED,
	SMBDIRECT_SOCKET_ERROR,
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
	case SMBDIRECT_SOCKET_RESOLVE_ADDR_NEEDED:
		return "RESOLVE_ADDR_NEEDED";
	case SMBDIRECT_SOCKET_RESOLVE_ADDR_RUNNING:
		return "RESOLVE_ADDR_RUNNING";
	case SMBDIRECT_SOCKET_RESOLVE_ADDR_FAILED:
		return "RESOLVE_ADDR_FAILED";
	case SMBDIRECT_SOCKET_RESOLVE_ROUTE_NEEDED:
		return "RESOLVE_ROUTE_NEEDED";
	case SMBDIRECT_SOCKET_RESOLVE_ROUTE_RUNNING:
		return "RESOLVE_ROUTE_RUNNING";
	case SMBDIRECT_SOCKET_RESOLVE_ROUTE_FAILED:
		return "RESOLVE_ROUTE_FAILED";
	case SMBDIRECT_SOCKET_RDMA_CONNECT_NEEDED:
		return "RDMA_CONNECT_NEEDED";
	case SMBDIRECT_SOCKET_RDMA_CONNECT_RUNNING:
		return "RDMA_CONNECT_RUNNING";
	case SMBDIRECT_SOCKET_RDMA_CONNECT_FAILED:
		return "RDMA_CONNECT_FAILED";
	case SMBDIRECT_SOCKET_NEGOTIATE_NEEDED:
		return "NEGOTIATE_NEEDED";
	case SMBDIRECT_SOCKET_NEGOTIATE_RUNNING:
		return "NEGOTIATE_RUNNING";
	case SMBDIRECT_SOCKET_NEGOTIATE_FAILED:
		return "NEGOTIATE_FAILED";
	case SMBDIRECT_SOCKET_CONNECTED:
		return "CONNECTED";
	case SMBDIRECT_SOCKET_ERROR:
		return "ERROR";
	case SMBDIRECT_SOCKET_DISCONNECTING:
		return "DISCONNECTING";
	case SMBDIRECT_SOCKET_DISCONNECTED:
		return "DISCONNECTED";
	case SMBDIRECT_SOCKET_DESTROYED:
		return "DESTROYED";
	}

	return "<unknown>";
}

enum smbdirect_keepalive_status {
	SMBDIRECT_KEEPALIVE_NONE,
	SMBDIRECT_KEEPALIVE_PENDING,
	SMBDIRECT_KEEPALIVE_SENT
};

struct smbdirect_socket {
	enum smbdirect_socket_status status;
	wait_queue_head_t status_wait;
	int first_error;

	/*
	 * This points to the workqueue to
	 * be used for this socket.
	 * It can be per socket (on the client)
	 * or point to a global workqueue (on the server)
	 */
	struct workqueue_struct *workqueue;

	struct work_struct disconnect_work;

	/* RDMA related */
	struct {
		struct rdma_cm_id *cm_id;
		/*
		 * This is for iWarp MPA v1
		 */
		bool legacy_iwarp;
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
	 * The state for keepalive and timeout handling
	 */
	struct {
		enum smbdirect_keepalive_status keepalive;
		struct work_struct immediate_work;
		struct delayed_work timer_work;
	} idle;

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

		/*
		 * The local credit state for ib_post_send()
		 */
		struct {
			atomic_t count;
			wait_queue_head_t wait_queue;
		} lcredits;

		/*
		 * The remote credit state for the send side
		 */
		struct {
			atomic_t count;
			wait_queue_head_t wait_queue;
		} credits;

		/*
		 * The state about posted/pending sends
		 */
		struct {
			atomic_t count;
			/*
			 * woken when count is decremented
			 */
			wait_queue_head_t dec_wait_queue;
			/*
			 * woken when count reached zero
			 */
			wait_queue_head_t zero_wait_queue;
		} pending;
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
		 * The state for posted recv_io messages
		 * and the refill work struct.
		 */
		struct {
			atomic_t count;
			struct work_struct refill_work;
		} posted;

		/*
		 * The credit state for the recv side
		 */
		struct {
			u16 target;
			atomic_t count;
		} credits;

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

	/*
	 * The state for Memory registrations on the client
	 */
	struct {
		enum ib_mr_type type;

		/*
		 * The list of free smbdirect_mr_io
		 * structures
		 */
		struct {
			struct list_head list;
			spinlock_t lock;
		} all;

		/*
		 * The number of available MRs ready for memory registration
		 */
		struct {
			atomic_t count;
			wait_queue_head_t wait_queue;
		} ready;

		/*
		 * The number of used MRs
		 */
		struct {
			atomic_t count;
		} used;

		struct work_struct recovery_work;

		/* Used by transport to wait until all MRs are returned */
		struct {
			wait_queue_head_t wait_queue;
		} cleanup;
	} mr_io;

	/*
	 * The state for RDMA read/write requests on the server
	 */
	struct {
		/*
		 * The credit state for the send side
		 */
		struct {
			/*
			 * The maximum number of rw credits
			 */
			size_t max;
			/*
			 * The number of pages per credit
			 */
			size_t num_pages;
			atomic_t count;
			wait_queue_head_t wait_queue;
		} credits;
	} rw_io;

	/*
	 * For debug purposes
	 */
	struct {
		u64 get_receive_buffer;
		u64 put_receive_buffer;
		u64 enqueue_reassembly_queue;
		u64 dequeue_reassembly_queue;
		u64 send_empty;
	} statistics;
};

static void __smbdirect_socket_disabled_work(struct work_struct *work)
{
	/*
	 * Should never be called as disable_[delayed_]work_sync() was used.
	 */
	WARN_ON_ONCE(1);
}

static __always_inline void smbdirect_socket_init(struct smbdirect_socket *sc)
{
	/*
	 * This also sets status = SMBDIRECT_SOCKET_CREATED
	 */
	BUILD_BUG_ON(SMBDIRECT_SOCKET_CREATED != 0);
	memset(sc, 0, sizeof(*sc));

	init_waitqueue_head(&sc->status_wait);

	INIT_WORK(&sc->disconnect_work, __smbdirect_socket_disabled_work);
	disable_work_sync(&sc->disconnect_work);

	INIT_WORK(&sc->idle.immediate_work, __smbdirect_socket_disabled_work);
	disable_work_sync(&sc->idle.immediate_work);
	INIT_DELAYED_WORK(&sc->idle.timer_work, __smbdirect_socket_disabled_work);
	disable_delayed_work_sync(&sc->idle.timer_work);

	atomic_set(&sc->send_io.lcredits.count, 0);
	init_waitqueue_head(&sc->send_io.lcredits.wait_queue);

	atomic_set(&sc->send_io.credits.count, 0);
	init_waitqueue_head(&sc->send_io.credits.wait_queue);

	atomic_set(&sc->send_io.pending.count, 0);
	init_waitqueue_head(&sc->send_io.pending.dec_wait_queue);
	init_waitqueue_head(&sc->send_io.pending.zero_wait_queue);

	INIT_LIST_HEAD(&sc->recv_io.free.list);
	spin_lock_init(&sc->recv_io.free.lock);

	atomic_set(&sc->recv_io.posted.count, 0);
	INIT_WORK(&sc->recv_io.posted.refill_work, __smbdirect_socket_disabled_work);
	disable_work_sync(&sc->recv_io.posted.refill_work);

	atomic_set(&sc->recv_io.credits.count, 0);

	INIT_LIST_HEAD(&sc->recv_io.reassembly.list);
	spin_lock_init(&sc->recv_io.reassembly.lock);
	init_waitqueue_head(&sc->recv_io.reassembly.wait_queue);

	atomic_set(&sc->rw_io.credits.count, 0);
	init_waitqueue_head(&sc->rw_io.credits.wait_queue);

	spin_lock_init(&sc->mr_io.all.lock);
	INIT_LIST_HEAD(&sc->mr_io.all.list);
	atomic_set(&sc->mr_io.ready.count, 0);
	init_waitqueue_head(&sc->mr_io.ready.wait_queue);
	atomic_set(&sc->mr_io.used.count, 0);
	INIT_WORK(&sc->mr_io.recovery_work, __smbdirect_socket_disabled_work);
	disable_work_sync(&sc->mr_io.recovery_work);
	init_waitqueue_head(&sc->mr_io.cleanup.wait_queue);
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

struct smbdirect_send_batch {
	/*
	 * List of smbdirect_send_io messages
	 */
	struct list_head msg_list;
	/*
	 * Number of list entries
	 */
	size_t wr_cnt;

	/*
	 * Possible remote key invalidation state
	 */
	bool need_invalidate_rkey;
	u32 remote_key;
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

enum smbdirect_mr_state {
	SMBDIRECT_MR_READY,
	SMBDIRECT_MR_REGISTERED,
	SMBDIRECT_MR_INVALIDATED,
	SMBDIRECT_MR_ERROR,
	SMBDIRECT_MR_DISABLED
};

struct smbdirect_mr_io {
	struct smbdirect_socket *socket;
	struct ib_cqe cqe;

	/*
	 * We can have up to two references:
	 * 1. by the connection
	 * 2. by the registration
	 */
	struct kref kref;
	struct mutex mutex;

	struct list_head list;

	enum smbdirect_mr_state state;
	struct ib_mr *mr;
	struct sg_table sgt;
	enum dma_data_direction dir;
	union {
		struct ib_reg_wr wr;
		struct ib_send_wr inv_wr;
	};

	bool need_invalidate;
	struct completion invalidate_done;
};

struct smbdirect_rw_io {
	struct smbdirect_socket *socket;
	struct ib_cqe cqe;

	struct list_head list;

	int error;
	struct completion *completion;

	struct rdma_rw_ctx rdma_ctx;
	struct sg_table sgt;
	struct scatterlist sg_list[];
};

#endif /* __FS_SMB_COMMON_SMBDIRECT_SMBDIRECT_SOCKET_H__ */
