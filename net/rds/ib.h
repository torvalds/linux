#ifndef _RDS_IB_H
#define _RDS_IB_H

#include <rdma/ib_verbs.h>
#include <rdma/rdma_cm.h>
#include "rds.h"
#include "rdma_transport.h"

#define RDS_FMR_SIZE			256
#define RDS_FMR_POOL_SIZE		4096

#define RDS_IB_MAX_SGE			8
#define RDS_IB_RECV_SGE 		2

#define RDS_IB_DEFAULT_RECV_WR		1024
#define RDS_IB_DEFAULT_SEND_WR		256

#define RDS_IB_DEFAULT_RETRY_COUNT	2

#define RDS_IB_SUPPORTED_PROTOCOLS	0x00000003	/* minor versions supported */

extern struct list_head rds_ib_devices;

/*
 * IB posts RDS_FRAG_SIZE fragments of pages to the receive queues to
 * try and minimize the amount of memory tied up both the device and
 * socket receive queues.
 */
/* page offset of the final full frag that fits in the page */
#define RDS_PAGE_LAST_OFF (((PAGE_SIZE  / RDS_FRAG_SIZE) - 1) * RDS_FRAG_SIZE)
struct rds_page_frag {
	struct list_head	f_item;
	struct page		*f_page;
	unsigned long		f_offset;
	dma_addr_t 		f_mapped;
};

struct rds_ib_incoming {
	struct list_head	ii_frags;
	struct rds_incoming	ii_inc;
};

struct rds_ib_connect_private {
	/* Add new fields at the end, and don't permute existing fields. */
	__be32			dp_saddr;
	__be32			dp_daddr;
	u8			dp_protocol_major;
	u8			dp_protocol_minor;
	__be16			dp_protocol_minor_mask; /* bitmask */
	__be32			dp_reserved1;
	__be64			dp_ack_seq;
	__be32			dp_credit;		/* non-zero enables flow ctl */
};

struct rds_ib_send_work {
	struct rds_message	*s_rm;
	struct rds_rdma_op	*s_op;
	struct ib_send_wr	s_wr;
	struct ib_sge		s_sge[RDS_IB_MAX_SGE];
	unsigned long		s_queued;
};

struct rds_ib_recv_work {
	struct rds_ib_incoming 	*r_ibinc;
	struct rds_page_frag	*r_frag;
	struct ib_recv_wr	r_wr;
	struct ib_sge		r_sge[2];
};

struct rds_ib_work_ring {
	u32		w_nr;
	u32		w_alloc_ptr;
	u32		w_alloc_ctr;
	u32		w_free_ptr;
	atomic_t	w_free_ctr;
};

struct rds_ib_device;

struct rds_ib_connection {

	struct list_head	ib_node;
	struct rds_ib_device	*rds_ibdev;
	struct rds_connection	*conn;

	/* alphabet soup, IBTA style */
	struct rdma_cm_id	*i_cm_id;
	struct ib_pd		*i_pd;
	struct ib_mr		*i_mr;
	struct ib_cq		*i_send_cq;
	struct ib_cq		*i_recv_cq;

	/* tx */
	struct rds_ib_work_ring	i_send_ring;
	struct rds_message	*i_rm;
	struct rds_header	*i_send_hdrs;
	u64			i_send_hdrs_dma;
	struct rds_ib_send_work *i_sends;

	/* rx */
	struct mutex		i_recv_mutex;
	struct rds_ib_work_ring	i_recv_ring;
	struct rds_ib_incoming	*i_ibinc;
	u32			i_recv_data_rem;
	struct rds_header	*i_recv_hdrs;
	u64			i_recv_hdrs_dma;
	struct rds_ib_recv_work *i_recvs;
	struct rds_page_frag	i_frag;
	u64			i_ack_recv;	/* last ACK received */

	/* sending acks */
	unsigned long		i_ack_flags;
#ifdef KERNEL_HAS_ATOMIC64
	atomic64_t		i_ack_next;	/* next ACK to send */
#else
	spinlock_t		i_ack_lock;	/* protect i_ack_next */
	u64			i_ack_next;	/* next ACK to send */
#endif
	struct rds_header	*i_ack;
	struct ib_send_wr	i_ack_wr;
	struct ib_sge		i_ack_sge;
	u64			i_ack_dma;
	unsigned long		i_ack_queued;

	/* Flow control related information
	 *
	 * Our algorithm uses a pair variables that we need to access
	 * atomically - one for the send credits, and one posted
	 * recv credits we need to transfer to remote.
	 * Rather than protect them using a slow spinlock, we put both into
	 * a single atomic_t and update it using cmpxchg
	 */
	atomic_t		i_credits;

	/* Protocol version specific information */
	unsigned int		i_flowctl:1;	/* enable/disable flow ctl */

	/* Batched completions */
	unsigned int		i_unsignaled_wrs;
	long			i_unsignaled_bytes;
};

/* This assumes that atomic_t is at least 32 bits */
#define IB_GET_SEND_CREDITS(v)	((v) & 0xffff)
#define IB_GET_POST_CREDITS(v)	((v) >> 16)
#define IB_SET_SEND_CREDITS(v)	((v) & 0xffff)
#define IB_SET_POST_CREDITS(v)	((v) << 16)

struct rds_ib_ipaddr {
	struct list_head	list;
	__be32			ipaddr;
};

struct rds_ib_device {
	struct list_head	list;
	struct list_head	ipaddr_list;
	struct list_head	conn_list;
	struct ib_device	*dev;
	struct ib_pd		*pd;
	struct ib_mr		*mr;
	struct rds_ib_mr_pool	*mr_pool;
	unsigned int		fmr_max_remaps;
	unsigned int		max_fmrs;
	int			max_sge;
	unsigned int		max_wrs;
	spinlock_t		spinlock;	/* protect the above */
};

/* bits for i_ack_flags */
#define IB_ACK_IN_FLIGHT	0
#define IB_ACK_REQUESTED	1

/* Magic WR_ID for ACKs */
#define RDS_IB_ACK_WR_ID	(~(u64) 0)

struct rds_ib_statistics {
	uint64_t	s_ib_connect_raced;
	uint64_t	s_ib_listen_closed_stale;
	uint64_t	s_ib_tx_cq_call;
	uint64_t	s_ib_tx_cq_event;
	uint64_t	s_ib_tx_ring_full;
	uint64_t	s_ib_tx_throttle;
	uint64_t	s_ib_tx_sg_mapping_failure;
	uint64_t	s_ib_tx_stalled;
	uint64_t	s_ib_tx_credit_updates;
	uint64_t	s_ib_rx_cq_call;
	uint64_t	s_ib_rx_cq_event;
	uint64_t	s_ib_rx_ring_empty;
	uint64_t	s_ib_rx_refill_from_cq;
	uint64_t	s_ib_rx_refill_from_thread;
	uint64_t	s_ib_rx_alloc_limit;
	uint64_t	s_ib_rx_credit_updates;
	uint64_t	s_ib_ack_sent;
	uint64_t	s_ib_ack_send_failure;
	uint64_t	s_ib_ack_send_delayed;
	uint64_t	s_ib_ack_send_piggybacked;
	uint64_t	s_ib_ack_received;
	uint64_t	s_ib_rdma_mr_alloc;
	uint64_t	s_ib_rdma_mr_free;
	uint64_t	s_ib_rdma_mr_used;
	uint64_t	s_ib_rdma_mr_pool_flush;
	uint64_t	s_ib_rdma_mr_pool_wait;
	uint64_t	s_ib_rdma_mr_pool_depleted;
};

extern struct workqueue_struct *rds_ib_wq;

/*
 * Fake ib_dma_sync_sg_for_{cpu,device} as long as ib_verbs.h
 * doesn't define it.
 */
static inline void rds_ib_dma_sync_sg_for_cpu(struct ib_device *dev,
		struct scatterlist *sg, unsigned int sg_dma_len, int direction)
{
	unsigned int i;

	for (i = 0; i < sg_dma_len; ++i) {
		ib_dma_sync_single_for_cpu(dev,
				ib_sg_dma_address(dev, &sg[i]),
				ib_sg_dma_len(dev, &sg[i]),
				direction);
	}
}
#define ib_dma_sync_sg_for_cpu	rds_ib_dma_sync_sg_for_cpu

static inline void rds_ib_dma_sync_sg_for_device(struct ib_device *dev,
		struct scatterlist *sg, unsigned int sg_dma_len, int direction)
{
	unsigned int i;

	for (i = 0; i < sg_dma_len; ++i) {
		ib_dma_sync_single_for_device(dev,
				ib_sg_dma_address(dev, &sg[i]),
				ib_sg_dma_len(dev, &sg[i]),
				direction);
	}
}
#define ib_dma_sync_sg_for_device	rds_ib_dma_sync_sg_for_device


/* ib.c */
extern struct rds_transport rds_ib_transport;
extern void rds_ib_add_one(struct ib_device *device);
extern void rds_ib_remove_one(struct ib_device *device);
extern struct ib_client rds_ib_client;

extern unsigned int fmr_pool_size;
extern unsigned int fmr_message_size;
extern unsigned int rds_ib_retry_count;

extern spinlock_t ib_nodev_conns_lock;
extern struct list_head ib_nodev_conns;

/* ib_cm.c */
int rds_ib_conn_alloc(struct rds_connection *conn, gfp_t gfp);
void rds_ib_conn_free(void *arg);
int rds_ib_conn_connect(struct rds_connection *conn);
void rds_ib_conn_shutdown(struct rds_connection *conn);
void rds_ib_state_change(struct sock *sk);
int __init rds_ib_listen_init(void);
void rds_ib_listen_stop(void);
void __rds_ib_conn_error(struct rds_connection *conn, const char *, ...);
int rds_ib_cm_handle_connect(struct rdma_cm_id *cm_id,
			     struct rdma_cm_event *event);
int rds_ib_cm_initiate_connect(struct rdma_cm_id *cm_id);
void rds_ib_cm_connect_complete(struct rds_connection *conn,
				struct rdma_cm_event *event);


#define rds_ib_conn_error(conn, fmt...) \
	__rds_ib_conn_error(conn, KERN_WARNING "RDS/IB: " fmt)

/* ib_rdma.c */
int rds_ib_update_ipaddr(struct rds_ib_device *rds_ibdev, __be32 ipaddr);
void rds_ib_add_conn(struct rds_ib_device *rds_ibdev, struct rds_connection *conn);
void rds_ib_remove_conn(struct rds_ib_device *rds_ibdev, struct rds_connection *conn);
void __rds_ib_destroy_conns(struct list_head *list, spinlock_t *list_lock);
static inline void rds_ib_destroy_nodev_conns(void)
{
	__rds_ib_destroy_conns(&ib_nodev_conns, &ib_nodev_conns_lock);
}
static inline void rds_ib_destroy_conns(struct rds_ib_device *rds_ibdev)
{
	__rds_ib_destroy_conns(&rds_ibdev->conn_list, &rds_ibdev->spinlock);
}
struct rds_ib_mr_pool *rds_ib_create_mr_pool(struct rds_ib_device *);
void rds_ib_get_mr_info(struct rds_ib_device *rds_ibdev, struct rds_info_rdma_connection *iinfo);
void rds_ib_destroy_mr_pool(struct rds_ib_mr_pool *);
void *rds_ib_get_mr(struct scatterlist *sg, unsigned long nents,
		    struct rds_sock *rs, u32 *key_ret);
void rds_ib_sync_mr(void *trans_private, int dir);
void rds_ib_free_mr(void *trans_private, int invalidate);
void rds_ib_flush_mrs(void);

/* ib_recv.c */
int __init rds_ib_recv_init(void);
void rds_ib_recv_exit(void);
int rds_ib_recv(struct rds_connection *conn);
int rds_ib_recv_refill(struct rds_connection *conn, gfp_t kptr_gfp,
		       gfp_t page_gfp, int prefill);
void rds_ib_inc_purge(struct rds_incoming *inc);
void rds_ib_inc_free(struct rds_incoming *inc);
int rds_ib_inc_copy_to_user(struct rds_incoming *inc, struct iovec *iov,
			     size_t size);
void rds_ib_recv_cq_comp_handler(struct ib_cq *cq, void *context);
void rds_ib_recv_init_ring(struct rds_ib_connection *ic);
void rds_ib_recv_clear_ring(struct rds_ib_connection *ic);
void rds_ib_recv_init_ack(struct rds_ib_connection *ic);
void rds_ib_attempt_ack(struct rds_ib_connection *ic);
void rds_ib_ack_send_complete(struct rds_ib_connection *ic);
u64 rds_ib_piggyb_ack(struct rds_ib_connection *ic);

/* ib_ring.c */
void rds_ib_ring_init(struct rds_ib_work_ring *ring, u32 nr);
void rds_ib_ring_resize(struct rds_ib_work_ring *ring, u32 nr);
u32 rds_ib_ring_alloc(struct rds_ib_work_ring *ring, u32 val, u32 *pos);
void rds_ib_ring_free(struct rds_ib_work_ring *ring, u32 val);
void rds_ib_ring_unalloc(struct rds_ib_work_ring *ring, u32 val);
int rds_ib_ring_empty(struct rds_ib_work_ring *ring);
int rds_ib_ring_low(struct rds_ib_work_ring *ring);
u32 rds_ib_ring_oldest(struct rds_ib_work_ring *ring);
u32 rds_ib_ring_completed(struct rds_ib_work_ring *ring, u32 wr_id, u32 oldest);
extern wait_queue_head_t rds_ib_ring_empty_wait;

/* ib_send.c */
void rds_ib_xmit_complete(struct rds_connection *conn);
int rds_ib_xmit(struct rds_connection *conn, struct rds_message *rm,
		unsigned int hdr_off, unsigned int sg, unsigned int off);
void rds_ib_send_cq_comp_handler(struct ib_cq *cq, void *context);
void rds_ib_send_init_ring(struct rds_ib_connection *ic);
void rds_ib_send_clear_ring(struct rds_ib_connection *ic);
int rds_ib_xmit_rdma(struct rds_connection *conn, struct rds_rdma_op *op);
void rds_ib_send_add_credits(struct rds_connection *conn, unsigned int credits);
void rds_ib_advertise_credits(struct rds_connection *conn, unsigned int posted);
int rds_ib_send_grab_credits(struct rds_ib_connection *ic, u32 wanted,
			     u32 *adv_credits, int need_posted, int max_posted);

/* ib_stats.c */
DECLARE_PER_CPU(struct rds_ib_statistics, rds_ib_stats);
#define rds_ib_stats_inc(member) rds_stats_inc_which(rds_ib_stats, member)
unsigned int rds_ib_stats_info_copy(struct rds_info_iterator *iter,
				    unsigned int avail);

/* ib_sysctl.c */
int __init rds_ib_sysctl_init(void);
void rds_ib_sysctl_exit(void);
extern unsigned long rds_ib_sysctl_max_send_wr;
extern unsigned long rds_ib_sysctl_max_recv_wr;
extern unsigned long rds_ib_sysctl_max_unsig_wrs;
extern unsigned long rds_ib_sysctl_max_unsig_bytes;
extern unsigned long rds_ib_sysctl_max_recv_allocation;
extern unsigned int rds_ib_sysctl_flow_control;
extern ctl_table rds_ib_sysctl_table[];

/*
 * Helper functions for getting/setting the header and data SGEs in
 * RDS packets (not RDMA)
 *
 * From version 3.1 onwards, header is in front of data in the sge.
 */
static inline struct ib_sge *
rds_ib_header_sge(struct rds_ib_connection *ic, struct ib_sge *sge)
{
	if (ic->conn->c_version > RDS_PROTOCOL_3_0)
		return &sge[0];
	else
		return &sge[1];
}

static inline struct ib_sge *
rds_ib_data_sge(struct rds_ib_connection *ic, struct ib_sge *sge)
{
	if (ic->conn->c_version > RDS_PROTOCOL_3_0)
		return &sge[1];
	else
		return &sge[0];
}

#endif
