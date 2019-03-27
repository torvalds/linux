#ifndef _SDP_H_
#define _SDP_H_

#define	LINUXKPI_PARAM_PREFIX ib_sdp_

#include "opt_ddb.h"
#include "opt_inet.h"
#include "opt_ofed.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/mbuf.h>
#include <sys/lock.h>
#include <sys/rwlock.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/protosw.h>
#include <sys/proc.h>
#include <sys/jail.h>
#include <sys/domain.h>

#ifdef DDB
#include <ddb/ddb.h>
#endif

#include <net/if.h>
#include <net/if_var.h>
#include <net/route.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/in_pcb.h>
#include <netinet/tcp.h>
#include <netinet/tcp_fsm.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>

#include <linux/device.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/wait.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>

#include <rdma/ib_verbs.h>
#include <rdma/rdma_cm.h>
#include <rdma/ib_cm.h>
#include <rdma/ib_fmr_pool.h>

#ifdef SDP_DEBUG
#define	CONFIG_INFINIBAND_SDP_DEBUG
#endif

#include "sdp_dbg.h"

#undef LIST_HEAD
/* From sys/queue.h */
#define LIST_HEAD(name, type)                                           \
struct name {                                                           \
        struct type *lh_first;  /* first element */                     \
}

/* Interval between successive polls in the Tx routine when polling is used
   instead of interrupts (in per-core Tx rings) - should be power of 2 */
#define SDP_TX_POLL_MODER	16
#define SDP_TX_POLL_TIMEOUT	(HZ / 20)
#define SDP_NAGLE_TIMEOUT (HZ / 10)

#define SDP_SRCAVAIL_CANCEL_TIMEOUT (HZ * 5)
#define SDP_SRCAVAIL_ADV_TIMEOUT (1 * HZ)
#define SDP_SRCAVAIL_PAYLOAD_LEN 1

#define SDP_RESOLVE_TIMEOUT 1000
#define SDP_ROUTE_TIMEOUT 1000
#define SDP_RETRY_COUNT 5
#define SDP_KEEPALIVE_TIME (120 * 60 * HZ)
#define SDP_FIN_WAIT_TIMEOUT (60 * HZ) /* like TCP_FIN_TIMEOUT */

#define SDP_TX_SIZE 0x40
#define SDP_RX_SIZE 0x40

#define SDP_FMR_SIZE (MIN(0x1000, PAGE_SIZE) / sizeof(u64))
#define SDP_FMR_POOL_SIZE	1024
#define SDP_FMR_DIRTY_SIZE	( SDP_FMR_POOL_SIZE / 4 )

#define SDP_MAX_RDMA_READ_LEN (PAGE_SIZE * (SDP_FMR_SIZE - 2))

/* mb inlined data len - rest will be rx'ed into frags */
#define SDP_HEAD_SIZE (sizeof(struct sdp_bsdh))

/* limit tx payload len, if the sink supports bigger buffers than the source
 * can handle.
 * or rx fragment size (limited by sge->length size) */
#define	SDP_MAX_PACKET	(1 << 16)
#define SDP_MAX_PAYLOAD (SDP_MAX_PACKET - SDP_HEAD_SIZE)

#define SDP_MAX_RECV_SGES (SDP_MAX_PACKET / MCLBYTES)
#define SDP_MAX_SEND_SGES (SDP_MAX_PACKET / MCLBYTES) + 2

#define SDP_NUM_WC 4

#define SDP_DEF_ZCOPY_THRESH 64*1024
#define SDP_MIN_ZCOPY_THRESH PAGE_SIZE
#define SDP_MAX_ZCOPY_THRESH 1048576

#define SDP_OP_RECV 0x800000000LL
#define SDP_OP_SEND 0x400000000LL
#define SDP_OP_RDMA 0x200000000LL
#define SDP_OP_NOP  0x100000000LL

/* how long (in jiffies) to block sender till tx completion*/
#define SDP_BZCOPY_POLL_TIMEOUT (HZ / 10)

#define SDP_AUTO_CONF	0xffff
#define AUTO_MOD_DELAY (HZ / 4)

struct sdp_mb_cb {
	__u32		seq;		/* Starting sequence number	*/
	struct bzcopy_state      *bz;
	struct rx_srcavail_state *rx_sa;
	struct tx_srcavail_state *tx_sa;
};

#define	M_PUSH	M_PROTO1	/* Do a 'push'. */
#define	M_URG	M_PROTO2	/* Mark as urgent (oob). */

#define SDP_SKB_CB(__mb)      ((struct sdp_mb_cb *)&((__mb)->cb[0]))
#define BZCOPY_STATE(mb)      (SDP_SKB_CB(mb)->bz)
#define RX_SRCAVAIL_STATE(mb) (SDP_SKB_CB(mb)->rx_sa)
#define TX_SRCAVAIL_STATE(mb) (SDP_SKB_CB(mb)->tx_sa)

#ifndef MIN
#define MIN(a, b) (a < b ? a : b)
#endif

#define ring_head(ring)   (atomic_read(&(ring).head))
#define ring_tail(ring)   (atomic_read(&(ring).tail))
#define ring_posted(ring) (ring_head(ring) - ring_tail(ring))

#define rx_ring_posted(ssk) ring_posted(ssk->rx_ring)
#ifdef SDP_ZCOPY
#define tx_ring_posted(ssk) (ring_posted(ssk->tx_ring) + \
	(ssk->tx_ring.rdma_inflight ? ssk->tx_ring.rdma_inflight->busy : 0))
#else
#define tx_ring_posted(ssk) ring_posted(ssk->tx_ring)
#endif

extern int sdp_zcopy_thresh;
extern int rcvbuf_initial_size;
extern struct workqueue_struct *rx_comp_wq;
extern struct ib_client sdp_client;

enum sdp_mid {
	SDP_MID_HELLO = 0x0,
	SDP_MID_HELLO_ACK = 0x1,
	SDP_MID_DISCONN = 0x2,
	SDP_MID_ABORT = 0x3,
	SDP_MID_SENDSM = 0x4,
	SDP_MID_RDMARDCOMPL = 0x6,
	SDP_MID_SRCAVAIL_CANCEL = 0x8,
	SDP_MID_CHRCVBUF = 0xB,
	SDP_MID_CHRCVBUF_ACK = 0xC,
	SDP_MID_SINKAVAIL = 0xFD,
	SDP_MID_SRCAVAIL = 0xFE,
	SDP_MID_DATA = 0xFF,
};

enum sdp_flags {
        SDP_OOB_PRES = 1 << 0,
        SDP_OOB_PEND = 1 << 1,
};

enum {
	SDP_MIN_TX_CREDITS = 2
};

enum {
	SDP_ERR_ERROR   = -4,
	SDP_ERR_FAULT   = -3,
	SDP_NEW_SEG     = -2,
	SDP_DO_WAIT_MEM = -1
};

struct sdp_bsdh {
	u8 mid;
	u8 flags;
	__u16 bufs;
	__u32 len;
	__u32 mseq;
	__u32 mseq_ack;
} __attribute__((__packed__));

union cma_ip_addr {
	struct in6_addr ip6;
	struct {
		__u32 pad[3];
		__u32 addr;
	} ip4;
} __attribute__((__packed__));

/* TODO: too much? Can I avoid having the src/dst and port here? */
struct sdp_hh {
	struct sdp_bsdh bsdh;
	u8 majv_minv;
	u8 ipv_cap;
	u8 rsvd1;
	u8 max_adverts;
	__u32 desremrcvsz;
	__u32 localrcvsz;
	__u16 port;
	__u16 rsvd2;
	union cma_ip_addr src_addr;
	union cma_ip_addr dst_addr;
	u8 rsvd3[IB_CM_REQ_PRIVATE_DATA_SIZE - sizeof(struct sdp_bsdh) - 48];
} __attribute__((__packed__));

struct sdp_hah {
	struct sdp_bsdh bsdh;
	u8 majv_minv;
	u8 ipv_cap;
	u8 rsvd1;
	u8 ext_max_adverts;
	__u32 actrcvsz;
	u8 rsvd2[IB_CM_REP_PRIVATE_DATA_SIZE - sizeof(struct sdp_bsdh) - 8];
} __attribute__((__packed__));

struct sdp_rrch {
	__u32 len;
} __attribute__((__packed__));

struct sdp_srcah {
	__u32 len;
	__u32 rkey;
	__u64 vaddr;
} __attribute__((__packed__));

struct sdp_buf {
        struct mbuf *mb;
        u64             mapping[SDP_MAX_SEND_SGES];
} __attribute__((__packed__));

struct sdp_chrecvbuf {
	u32 size;
} __attribute__((__packed__));

/* Context used for synchronous zero copy bcopy (BZCOPY) */
struct bzcopy_state {
	unsigned char __user  *u_base;
	int                    u_len;
	int                    left;
	int                    page_cnt;
	int                    cur_page;
	int                    cur_offset;
	int                    busy;
	struct sdp_sock      *ssk;
	struct page         **pages;
};

enum rx_sa_flag {
	RX_SA_ABORTED    = 2,
};

enum tx_sa_flag {
	TX_SA_SENDSM     = 0x01,
	TX_SA_CROSS_SEND = 0x02,
	TX_SA_INTRRUPTED = 0x04,
	TX_SA_TIMEDOUT   = 0x08,
	TX_SA_ERROR      = 0x10,
};

struct rx_srcavail_state {
	/* Advertised buffer stuff */
	u32 mseq;
	u32 used;
	u32 reported;
	u32 len;
	u32 rkey;
	u64 vaddr;

	/* Dest buff info */
	struct ib_umem *umem;
	struct ib_pool_fmr *fmr;

	/* Utility */
	u8  busy;
	enum rx_sa_flag  flags;
};

struct tx_srcavail_state {
	/* Data below 'busy' will be reset */
	u8		busy;

	struct ib_umem *umem;
	struct ib_pool_fmr *fmr;

	u32		bytes_sent;
	u32		bytes_acked;

	enum tx_sa_flag	abort_flags;
	u8		posted;

	u32		mseq;
};

struct sdp_tx_ring {
#ifdef SDP_ZCOPY
	struct rx_srcavail_state *rdma_inflight;
#endif
	struct sdp_buf   	*buffer;
	atomic_t          	head;
	atomic_t          	tail;
	struct ib_cq 	 	*cq;

	atomic_t 	  	credits;
#define tx_credits(ssk) (atomic_read(&ssk->tx_ring.credits))

	struct callout		timer;
	u16 		  	poll_cnt;
};

struct sdp_rx_ring {
	struct sdp_buf   *buffer;
	atomic_t          head;
	atomic_t          tail;
	struct ib_cq 	 *cq;

	int		 destroyed;
	struct rwlock	 destroyed_lock;
};

struct sdp_device {
	struct ib_pd 		*pd;
	struct ib_fmr_pool 	*fmr_pool;
};

struct sdp_moderation {
	unsigned long last_moder_packets;
	unsigned long last_moder_tx_packets;
	unsigned long last_moder_bytes;
	unsigned long last_moder_jiffies;
	int last_moder_time;
	u16 rx_usecs;
	u16 rx_frames;
	u16 tx_usecs;
	u32 pkt_rate_low;
	u16 rx_usecs_low;
	u32 pkt_rate_high;
	u16 rx_usecs_high;
	u16 sample_interval;
	u16 adaptive_rx_coal;
	u32 msg_enable;

	int moder_cnt;
	int moder_time;
};

/* These are flags fields. */
#define	SDP_TIMEWAIT	0x0001		/* In ssk timewait state. */
#define	SDP_DROPPED	0x0002		/* Socket has been dropped. */
#define	SDP_SOCKREF	0x0004		/* Holding a sockref for close. */
#define	SDP_NODELAY	0x0008		/* Disble nagle. */
#define	SDP_NEEDFIN	0x0010		/* Send a fin on the next tx. */
#define	SDP_DREQWAIT	0x0020		/* Waiting on DREQ. */
#define	SDP_DESTROY	0x0040		/* Being destroyed. */
#define	SDP_DISCON	0x0080		/* rdma_disconnect is owed. */

/* These are oobflags */
#define	SDP_HADOOB	0x0001		/* Had OOB data. */
#define	SDP_HAVEOOB	0x0002		/* Have OOB data. */

struct sdp_sock {
	LIST_ENTRY(sdp_sock) list;
	struct socket *socket;
	struct rdma_cm_id *id;
	struct ib_device *ib_device;
	struct sdp_device *sdp_dev;
	struct ib_qp *qp;
	struct ucred *cred;
	struct callout keep2msl;	/* 2msl and keepalive timer. */
	struct callout nagle_timer;	/* timeout waiting for ack */
	struct ib_ucontext context;
	in_port_t lport;
	in_addr_t laddr;
	in_port_t fport;
	in_addr_t faddr;
	int flags;
	int oobflags;		/* protected by rx lock. */
	int state;
	int softerror;
	int recv_bytes;		/* Bytes per recv. buf including header */
	int xmit_size_goal;
	char iobc;

	struct sdp_rx_ring rx_ring;
	struct sdp_tx_ring tx_ring;
	struct rwlock	lock;
	struct mbufq	rxctlq;		/* received control packets */

	int qp_active;	/* XXX Flag. */
	int max_sge;
	struct work_struct rx_comp_work;
#define rcv_nxt(ssk) atomic_read(&(ssk->rcv_nxt))
	atomic_t rcv_nxt;

	/* SDP specific */
	atomic_t mseq_ack;
#define mseq_ack(ssk) (atomic_read(&ssk->mseq_ack))
	unsigned max_bufs;	/* Initial buffers offered by other side */
	unsigned min_bufs;	/* Low water mark to wake senders */

	unsigned long nagle_last_unacked; /* mseq of lastest unacked packet */

	atomic_t               remote_credits;
#define remote_credits(ssk) (atomic_read(&ssk->remote_credits))
	int 		  poll_cq;

	/* SDP slow start */
	int recv_request_head; 	/* mark the rx_head when the resize request
				   was received */
	int recv_request; 	/* XXX flag if request to resize was received */

	unsigned long tx_packets;
	unsigned long rx_packets;
	unsigned long tx_bytes;
	unsigned long rx_bytes;
	struct sdp_moderation auto_mod;
	struct task shutdown_task;
#ifdef SDP_ZCOPY
	struct tx_srcavail_state *tx_sa;
	struct rx_srcavail_state *rx_sa;
	spinlock_t tx_sa_lock;
	struct delayed_work srcavail_cancel_work;
	int srcavail_cancel_mseq;
	/* ZCOPY data: -1:use global; 0:disable zcopy; >0: zcopy threshold */
	int zcopy_thresh;
#endif
};

#define	sdp_sk(so)	((struct sdp_sock *)(so->so_pcb))

#define	SDP_RLOCK(ssk)		rw_rlock(&(ssk)->lock)
#define	SDP_WLOCK(ssk)		rw_wlock(&(ssk)->lock)
#define	SDP_RUNLOCK(ssk)	rw_runlock(&(ssk)->lock)
#define	SDP_WUNLOCK(ssk)	rw_wunlock(&(ssk)->lock)
#define	SDP_WLOCK_ASSERT(ssk)	rw_assert(&(ssk)->lock, RA_WLOCKED)
#define	SDP_RLOCK_ASSERT(ssk)	rw_assert(&(ssk)->lock, RA_RLOCKED)
#define	SDP_LOCK_ASSERT(ssk)	rw_assert(&(ssk)->lock, RA_LOCKED)

MALLOC_DECLARE(M_SDP);

static inline void tx_sa_reset(struct tx_srcavail_state *tx_sa)
{
	memset((void *)&tx_sa->busy, 0,
			sizeof(*tx_sa) - offsetof(typeof(*tx_sa), busy));
}

static inline void rx_ring_unlock(struct sdp_rx_ring *rx_ring)
{
	rw_runlock(&rx_ring->destroyed_lock);
}

static inline int rx_ring_trylock(struct sdp_rx_ring *rx_ring)
{
	rw_rlock(&rx_ring->destroyed_lock);
	if (rx_ring->destroyed) {
		rx_ring_unlock(rx_ring);
		return 0;
	}
	return 1;
}

static inline void rx_ring_destroy_lock(struct sdp_rx_ring *rx_ring)
{
	rw_wlock(&rx_ring->destroyed_lock);
	rx_ring->destroyed = 1;
	rw_wunlock(&rx_ring->destroyed_lock);
}

static inline void sdp_arm_rx_cq(struct sdp_sock *ssk)
{
	sdp_prf(ssk->socket, NULL, "Arming RX cq");
	sdp_dbg_data(ssk->socket, "Arming RX cq\n");

	ib_req_notify_cq(ssk->rx_ring.cq, IB_CQ_NEXT_COMP);
}

static inline void sdp_arm_tx_cq(struct sdp_sock *ssk)
{
	sdp_prf(ssk->socket, NULL, "Arming TX cq");
	sdp_dbg_data(ssk->socket, "Arming TX cq. credits: %d, posted: %d\n",
		tx_credits(ssk), tx_ring_posted(ssk));

	ib_req_notify_cq(ssk->tx_ring.cq, IB_CQ_NEXT_COMP);
}

/* return the min of:
 * - tx credits
 * - free slots in tx_ring (not including SDP_MIN_TX_CREDITS
 */
static inline int tx_slots_free(struct sdp_sock *ssk)
{
	int min_free;

	min_free = MIN(tx_credits(ssk),
			SDP_TX_SIZE - tx_ring_posted(ssk));
	if (min_free < SDP_MIN_TX_CREDITS)
		return 0;

	return min_free - SDP_MIN_TX_CREDITS;
};

/* utilities */
static inline char *mid2str(int mid)
{
#define ENUM2STR(e) [e] = #e
	static char *mid2str[] = {
		ENUM2STR(SDP_MID_HELLO),
		ENUM2STR(SDP_MID_HELLO_ACK),
		ENUM2STR(SDP_MID_ABORT),
		ENUM2STR(SDP_MID_DISCONN),
		ENUM2STR(SDP_MID_SENDSM),
		ENUM2STR(SDP_MID_RDMARDCOMPL),
		ENUM2STR(SDP_MID_SRCAVAIL_CANCEL),
		ENUM2STR(SDP_MID_CHRCVBUF),
		ENUM2STR(SDP_MID_CHRCVBUF_ACK),
		ENUM2STR(SDP_MID_DATA),
		ENUM2STR(SDP_MID_SRCAVAIL),
		ENUM2STR(SDP_MID_SINKAVAIL),
	};

	if (mid >= ARRAY_SIZE(mid2str))
		return NULL;

	return mid2str[mid];
}

static inline struct mbuf *
sdp_alloc_mb(struct socket *sk, u8 mid, int size, int wait)
{
	struct sdp_bsdh *h;
	struct mbuf *mb;

	MGETHDR(mb, wait, MT_DATA);
	if (mb == NULL)
		return (NULL);
	mb->m_pkthdr.len = mb->m_len = sizeof(struct sdp_bsdh);
	h = mtod(mb, struct sdp_bsdh *);
	h->mid = mid;

	return mb;
}
static inline struct mbuf *
sdp_alloc_mb_data(struct socket *sk, int wait)
{
	return sdp_alloc_mb(sk, SDP_MID_DATA, 0, wait);
}

static inline struct mbuf *
sdp_alloc_mb_disconnect(struct socket *sk, int wait)
{
	return sdp_alloc_mb(sk, SDP_MID_DISCONN, 0, wait);
}

static inline void *
mb_put(struct mbuf *mb, int len)
{
	uint8_t *data;

	data = mb->m_data;
	data += mb->m_len;
	mb->m_len += len;
	return (void *)data;
}

static inline struct mbuf *
sdp_alloc_mb_chrcvbuf_ack(struct socket *sk, int size, int wait)
{
	struct mbuf *mb;
	struct sdp_chrecvbuf *resp_size;

	mb = sdp_alloc_mb(sk, SDP_MID_CHRCVBUF_ACK, sizeof(*resp_size), wait);
	if (mb == NULL)
		return (NULL);
	resp_size = (struct sdp_chrecvbuf *)mb_put(mb, sizeof *resp_size);
	resp_size->size = htonl(size);

	return mb;
}

static inline struct mbuf *
sdp_alloc_mb_srcavail(struct socket *sk, u32 len, u32 rkey, u64 vaddr, int wait)
{
	struct mbuf *mb;
	struct sdp_srcah *srcah;

	mb = sdp_alloc_mb(sk, SDP_MID_SRCAVAIL, sizeof(*srcah), wait);
	if (mb == NULL)
		return (NULL);
	srcah = (struct sdp_srcah *)mb_put(mb, sizeof(*srcah));
	srcah->len = htonl(len);
	srcah->rkey = htonl(rkey);
	srcah->vaddr = cpu_to_be64(vaddr);

	return mb;
}

static inline struct mbuf *
sdp_alloc_mb_srcavail_cancel(struct socket *sk, int wait)
{
	return sdp_alloc_mb(sk, SDP_MID_SRCAVAIL_CANCEL, 0, wait);
}

static inline struct mbuf *
sdp_alloc_mb_rdmardcompl(struct socket *sk, u32 len, int wait)
{
	struct mbuf *mb;
	struct sdp_rrch *rrch;

	mb = sdp_alloc_mb(sk, SDP_MID_RDMARDCOMPL, sizeof(*rrch), wait);
	if (mb == NULL)
		return (NULL);
	rrch = (struct sdp_rrch *)mb_put(mb, sizeof(*rrch));
	rrch->len = htonl(len);

	return mb;
}

static inline struct mbuf *
sdp_alloc_mb_sendsm(struct socket *sk, int wait)
{
	return sdp_alloc_mb(sk, SDP_MID_SENDSM, 0, wait);
}
static inline int sdp_tx_ring_slots_left(struct sdp_sock *ssk)
{
	return SDP_TX_SIZE - tx_ring_posted(ssk);
}

static inline int credit_update_needed(struct sdp_sock *ssk)
{
	int c;

	c = remote_credits(ssk);
	if (likely(c > SDP_MIN_TX_CREDITS))
		c += c/2;
	return unlikely(c < rx_ring_posted(ssk)) &&
	    likely(tx_credits(ssk) > 0) &&
	    likely(sdp_tx_ring_slots_left(ssk));
}


#define SDPSTATS_COUNTER_INC(stat)
#define SDPSTATS_COUNTER_ADD(stat, val)
#define SDPSTATS_COUNTER_MID_INC(stat, mid)
#define SDPSTATS_HIST_LINEAR(stat, size)
#define SDPSTATS_HIST(stat, size)

static inline void
sdp_cleanup_sdp_buf(struct sdp_sock *ssk, struct sdp_buf *sbuf,
    enum dma_data_direction dir)
{
	struct ib_device *dev;
	struct mbuf *mb;
	int i;

	dev = ssk->ib_device;
	for (i = 0, mb = sbuf->mb; mb != NULL; mb = mb->m_next, i++)
		ib_dma_unmap_single(dev, sbuf->mapping[i], mb->m_len, dir);
}

/* sdp_main.c */
void sdp_set_default_moderation(struct sdp_sock *ssk);
void sdp_start_keepalive_timer(struct socket *sk);
void sdp_urg(struct sdp_sock *ssk, struct mbuf *mb);
void sdp_cancel_dreq_wait_timeout(struct sdp_sock *ssk);
void sdp_abort(struct socket *sk);
struct sdp_sock *sdp_notify(struct sdp_sock *ssk, int error);


/* sdp_cma.c */
int sdp_cma_handler(struct rdma_cm_id *, struct rdma_cm_event *);

/* sdp_tx.c */
int sdp_tx_ring_create(struct sdp_sock *ssk, struct ib_device *device);
void sdp_tx_ring_destroy(struct sdp_sock *ssk);
int sdp_xmit_poll(struct sdp_sock *ssk, int force);
void sdp_post_send(struct sdp_sock *ssk, struct mbuf *mb);
void sdp_post_sends(struct sdp_sock *ssk, int wait);
void sdp_post_keepalive(struct sdp_sock *ssk);

/* sdp_rx.c */
void sdp_rx_ring_init(struct sdp_sock *ssk);
int sdp_rx_ring_create(struct sdp_sock *ssk, struct ib_device *device);
void sdp_rx_ring_destroy(struct sdp_sock *ssk);
int sdp_resize_buffers(struct sdp_sock *ssk, u32 new_size);
int sdp_init_buffers(struct sdp_sock *ssk, u32 new_size);
void sdp_do_posts(struct sdp_sock *ssk);
void sdp_rx_comp_full(struct sdp_sock *ssk);

/* sdp_zcopy.c */
struct kiocb;
int sdp_sendmsg_zcopy(struct kiocb *iocb, struct socket *sk, struct iovec *iov);
int sdp_handle_srcavail(struct sdp_sock *ssk, struct sdp_srcah *srcah);
void sdp_handle_sendsm(struct sdp_sock *ssk, u32 mseq_ack);
void sdp_handle_rdma_read_compl(struct sdp_sock *ssk, u32 mseq_ack,
		u32 bytes_completed);
int sdp_handle_rdma_read_cqe(struct sdp_sock *ssk);
int sdp_rdma_to_iovec(struct socket *sk, struct iovec *iov, struct mbuf *mb,
		unsigned long *used);
int sdp_post_rdma_rd_compl(struct sdp_sock *ssk,
		struct rx_srcavail_state *rx_sa);
int sdp_post_sendsm(struct socket *sk);
void srcavail_cancel_timeout(struct work_struct *work);
void sdp_abort_srcavail(struct socket *sk);
void sdp_abort_rdma_read(struct socket *sk);
int sdp_process_rx(struct sdp_sock *ssk);

#endif
