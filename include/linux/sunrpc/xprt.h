/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  linux/include/linux/sunrpc/xprt.h
 *
 *  Declarations for the RPC transport interface.
 *
 *  Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 */

#ifndef _LINUX_SUNRPC_XPRT_H
#define _LINUX_SUNRPC_XPRT_H

#include <linux/uio.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/ktime.h>
#include <linux/kref.h>
#include <linux/sunrpc/sched.h>
#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/msg_prot.h>

#ifdef __KERNEL__

#define RPC_MIN_SLOT_TABLE	(2U)
#define RPC_DEF_SLOT_TABLE	(16U)
#define RPC_MAX_SLOT_TABLE_LIMIT	(65536U)
#define RPC_MAX_SLOT_TABLE	RPC_MAX_SLOT_TABLE_LIMIT

#define RPC_CWNDSHIFT		(8U)
#define RPC_CWNDSCALE		(1U << RPC_CWNDSHIFT)
#define RPC_INITCWND		RPC_CWNDSCALE
#define RPC_MAXCWND(xprt)	((xprt)->max_reqs << RPC_CWNDSHIFT)
#define RPCXPRT_CONGESTED(xprt) ((xprt)->cong >= (xprt)->cwnd)

/*
 * This describes a timeout strategy
 */
struct rpc_timeout {
	unsigned long		to_initval,		/* initial timeout */
				to_maxval,		/* max timeout */
				to_increment;		/* if !exponential */
	unsigned int		to_retries;		/* max # of retries */
	unsigned char		to_exponential;
};

enum rpc_display_format_t {
	RPC_DISPLAY_ADDR = 0,
	RPC_DISPLAY_PORT,
	RPC_DISPLAY_PROTO,
	RPC_DISPLAY_HEX_ADDR,
	RPC_DISPLAY_HEX_PORT,
	RPC_DISPLAY_NETID,
	RPC_DISPLAY_MAX,
};

struct rpc_task;
struct rpc_xprt;
struct seq_file;
struct svc_serv;
struct net;

/*
 * This describes a complete RPC request
 */
struct rpc_rqst {
	/*
	 * This is the user-visible part
	 */
	struct rpc_xprt *	rq_xprt;		/* RPC client */
	struct xdr_buf		rq_snd_buf;		/* send buffer */
	struct xdr_buf		rq_rcv_buf;		/* recv buffer */

	/*
	 * This is the private part
	 */
	struct rpc_task *	rq_task;	/* RPC task data */
	struct rpc_cred *	rq_cred;	/* Bound cred */
	__be32			rq_xid;		/* request XID */
	int			rq_cong;	/* has incremented xprt->cong */
	u32			rq_seqno;	/* gss seq no. used on req. */
	int			rq_enc_pages_num;
	struct page		**rq_enc_pages;	/* scratch pages for use by
						   gss privacy code */
	void (*rq_release_snd_buf)(struct rpc_rqst *); /* release rq_enc_pages */
	struct list_head	rq_list;

	void			*rq_buffer;	/* Call XDR encode buffer */
	size_t			rq_callsize;
	void			*rq_rbuffer;	/* Reply XDR decode buffer */
	size_t			rq_rcvsize;
	size_t			rq_xmit_bytes_sent;	/* total bytes sent */
	size_t			rq_reply_bytes_recvd;	/* total reply bytes */
							/* received */

	struct xdr_buf		rq_private_buf;		/* The receive buffer
							 * used in the softirq.
							 */
	unsigned long		rq_majortimeo;	/* major timeout alarm */
	unsigned long		rq_timeout;	/* Current timeout value */
	ktime_t			rq_rtt;		/* round-trip time */
	unsigned int		rq_retries;	/* # of retries */
	unsigned int		rq_connect_cookie;
						/* A cookie used to track the
						   state of the transport
						   connection */
	
	/*
	 * Partial send handling
	 */
	u32			rq_bytes_sent;	/* Bytes we have sent */

	ktime_t			rq_xtime;	/* transmit time stamp */
	int			rq_ntrans;

#if defined(CONFIG_SUNRPC_BACKCHANNEL)
	struct list_head	rq_bc_list;	/* Callback service list */
	unsigned long		rq_bc_pa_state;	/* Backchannel prealloc state */
	struct list_head	rq_bc_pa_list;	/* Backchannel prealloc list */
#endif /* CONFIG_SUNRPC_BACKCHANEL */
};
#define rq_svec			rq_snd_buf.head
#define rq_slen			rq_snd_buf.len

struct rpc_xprt_ops {
	void		(*set_buffer_size)(struct rpc_xprt *xprt, size_t sndsize, size_t rcvsize);
	int		(*reserve_xprt)(struct rpc_xprt *xprt, struct rpc_task *task);
	void		(*release_xprt)(struct rpc_xprt *xprt, struct rpc_task *task);
	void		(*alloc_slot)(struct rpc_xprt *xprt, struct rpc_task *task);
	void		(*free_slot)(struct rpc_xprt *xprt,
				     struct rpc_rqst *req);
	void		(*rpcbind)(struct rpc_task *task);
	void		(*set_port)(struct rpc_xprt *xprt, unsigned short port);
	void		(*connect)(struct rpc_xprt *xprt, struct rpc_task *task);
	int		(*buf_alloc)(struct rpc_task *task);
	void		(*buf_free)(struct rpc_task *task);
	int		(*send_request)(struct rpc_task *task);
	void		(*set_retrans_timeout)(struct rpc_task *task);
	void		(*timer)(struct rpc_xprt *xprt, struct rpc_task *task);
	void		(*release_request)(struct rpc_task *task);
	void		(*close)(struct rpc_xprt *xprt);
	void		(*destroy)(struct rpc_xprt *xprt);
	void		(*set_connect_timeout)(struct rpc_xprt *xprt,
					unsigned long connect_timeout,
					unsigned long reconnect_timeout);
	void		(*print_stats)(struct rpc_xprt *xprt, struct seq_file *seq);
	int		(*enable_swap)(struct rpc_xprt *xprt);
	void		(*disable_swap)(struct rpc_xprt *xprt);
	void		(*inject_disconnect)(struct rpc_xprt *xprt);
	int		(*bc_setup)(struct rpc_xprt *xprt,
				    unsigned int min_reqs);
	int		(*bc_up)(struct svc_serv *serv, struct net *net);
	size_t		(*bc_maxpayload)(struct rpc_xprt *xprt);
	void		(*bc_free_rqst)(struct rpc_rqst *rqst);
	void		(*bc_destroy)(struct rpc_xprt *xprt,
				      unsigned int max_reqs);
};

/*
 * RPC transport identifiers
 *
 * To preserve compatibility with the historical use of raw IP protocol
 * id's for transport selection, UDP and TCP identifiers are specified
 * with the previous values. No such restriction exists for new transports,
 * except that they may not collide with these values (17 and 6,
 * respectively).
 */
#define XPRT_TRANSPORT_BC       (1 << 31)
enum xprt_transports {
	XPRT_TRANSPORT_UDP	= IPPROTO_UDP,
	XPRT_TRANSPORT_TCP	= IPPROTO_TCP,
	XPRT_TRANSPORT_BC_TCP	= IPPROTO_TCP | XPRT_TRANSPORT_BC,
	XPRT_TRANSPORT_RDMA	= 256,
	XPRT_TRANSPORT_BC_RDMA	= XPRT_TRANSPORT_RDMA | XPRT_TRANSPORT_BC,
	XPRT_TRANSPORT_LOCAL	= 257,
};

struct rpc_xprt {
	struct kref		kref;		/* Reference count */
	const struct rpc_xprt_ops *ops;		/* transport methods */

	const struct rpc_timeout *timeout;	/* timeout parms */
	struct sockaddr_storage	addr;		/* server address */
	size_t			addrlen;	/* size of server address */
	int			prot;		/* IP protocol */

	unsigned long		cong;		/* current congestion */
	unsigned long		cwnd;		/* congestion window */

	size_t			max_payload;	/* largest RPC payload size,
						   in bytes */
	unsigned int		tsh_size;	/* size of transport specific
						   header */

	struct rpc_wait_queue	binding;	/* requests waiting on rpcbind */
	struct rpc_wait_queue	sending;	/* requests waiting to send */
	struct rpc_wait_queue	pending;	/* requests in flight */
	struct rpc_wait_queue	backlog;	/* waiting for slot */
	struct list_head	free;		/* free slots */
	unsigned int		max_reqs;	/* max number of slots */
	unsigned int		min_reqs;	/* min number of slots */
	unsigned int		num_reqs;	/* total slots */
	unsigned long		state;		/* transport state */
	unsigned char		resvport   : 1; /* use a reserved port */
	atomic_t		swapper;	/* we're swapping over this
						   transport */
	unsigned int		bind_index;	/* bind function index */

	/*
	 * Multipath
	 */
	struct list_head	xprt_switch;

	/*
	 * Connection of transports
	 */
	unsigned long		bind_timeout,
				reestablish_timeout;
	unsigned int		connect_cookie;	/* A cookie that gets bumped
						   every time the transport
						   is reconnected */

	/*
	 * Disconnection of idle transports
	 */
	struct work_struct	task_cleanup;
	struct timer_list	timer;
	unsigned long		last_used,
				idle_timeout,
				connect_timeout,
				max_reconnect_timeout;

	/*
	 * Send stuff
	 */
	spinlock_t		transport_lock;	/* lock transport info */
	spinlock_t		reserve_lock;	/* lock slot table */
	spinlock_t		recv_lock;	/* lock receive list */
	u32			xid;		/* Next XID value to use */
	struct rpc_task *	snd_task;	/* Task blocked in send */
	struct svc_xprt		*bc_xprt;	/* NFSv4.1 backchannel */
#if defined(CONFIG_SUNRPC_BACKCHANNEL)
	struct svc_serv		*bc_serv;       /* The RPC service which will */
						/* process the callback */
	int			bc_alloc_count;	/* Total number of preallocs */
	atomic_t		bc_free_slots;
	spinlock_t		bc_pa_lock;	/* Protects the preallocated
						 * items */
	struct list_head	bc_pa_list;	/* List of preallocated
						 * backchannel rpc_rqst's */
#endif /* CONFIG_SUNRPC_BACKCHANNEL */
	struct list_head	recv;

	struct {
		unsigned long		bind_count,	/* total number of binds */
					connect_count,	/* total number of connects */
					connect_start,	/* connect start timestamp */
					connect_time,	/* jiffies waiting for connect */
					sends,		/* how many complete requests */
					recvs,		/* how many complete requests */
					bad_xids,	/* lookup_rqst didn't find XID */
					max_slots;	/* max rpc_slots used */

		unsigned long long	req_u,		/* average requests on the wire */
					bklog_u,	/* backlog queue utilization */
					sending_u,	/* send q utilization */
					pending_u;	/* pend q utilization */
	} stat;

	struct net		*xprt_net;
	const char		*servername;
	const char		*address_strings[RPC_DISPLAY_MAX];
#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
	struct dentry		*debugfs;		/* debugfs directory */
	atomic_t		inject_disconnect;
#endif
	struct rcu_head		rcu;
};

#if defined(CONFIG_SUNRPC_BACKCHANNEL)
/*
 * Backchannel flags
 */
#define	RPC_BC_PA_IN_USE	0x0001		/* Preallocated backchannel */
						/* buffer in use */
#endif /* CONFIG_SUNRPC_BACKCHANNEL */

#if defined(CONFIG_SUNRPC_BACKCHANNEL)
static inline int bc_prealloc(struct rpc_rqst *req)
{
	return test_bit(RPC_BC_PA_IN_USE, &req->rq_bc_pa_state);
}
#else
static inline int bc_prealloc(struct rpc_rqst *req)
{
	return 0;
}
#endif /* CONFIG_SUNRPC_BACKCHANNEL */

#define XPRT_CREATE_INFINITE_SLOTS	(1U)
#define XPRT_CREATE_NO_IDLE_TIMEOUT	(1U << 1)

struct xprt_create {
	int			ident;		/* XPRT_TRANSPORT identifier */
	struct net *		net;
	struct sockaddr *	srcaddr;	/* optional local address */
	struct sockaddr *	dstaddr;	/* remote peer address */
	size_t			addrlen;
	const char		*servername;
	struct svc_xprt		*bc_xprt;	/* NFSv4.1 backchannel */
	struct rpc_xprt_switch	*bc_xps;
	unsigned int		flags;
};

struct xprt_class {
	struct list_head	list;
	int			ident;		/* XPRT_TRANSPORT identifier */
	struct rpc_xprt *	(*setup)(struct xprt_create *);
	struct module		*owner;
	char			name[32];
	const char *		netid[];
};

/*
 * Generic internal transport functions
 */
struct rpc_xprt		*xprt_create_transport(struct xprt_create *args);
void			xprt_connect(struct rpc_task *task);
void			xprt_reserve(struct rpc_task *task);
void			xprt_retry_reserve(struct rpc_task *task);
int			xprt_reserve_xprt(struct rpc_xprt *xprt, struct rpc_task *task);
int			xprt_reserve_xprt_cong(struct rpc_xprt *xprt, struct rpc_task *task);
void			xprt_alloc_slot(struct rpc_xprt *xprt, struct rpc_task *task);
void			xprt_free_slot(struct rpc_xprt *xprt,
				       struct rpc_rqst *req);
void			xprt_lock_and_alloc_slot(struct rpc_xprt *xprt, struct rpc_task *task);
bool			xprt_prepare_transmit(struct rpc_task *task);
void			xprt_transmit(struct rpc_task *task);
void			xprt_end_transmit(struct rpc_task *task);
int			xprt_adjust_timeout(struct rpc_rqst *req);
void			xprt_release_xprt(struct rpc_xprt *xprt, struct rpc_task *task);
void			xprt_release_xprt_cong(struct rpc_xprt *xprt, struct rpc_task *task);
void			xprt_release(struct rpc_task *task);
struct rpc_xprt *	xprt_get(struct rpc_xprt *xprt);
void			xprt_put(struct rpc_xprt *xprt);
struct rpc_xprt *	xprt_alloc(struct net *net, size_t size,
				unsigned int num_prealloc,
				unsigned int max_req);
void			xprt_free(struct rpc_xprt *);

static inline __be32 *xprt_skip_transport_header(struct rpc_xprt *xprt, __be32 *p)
{
	return p + xprt->tsh_size;
}

static inline int
xprt_enable_swap(struct rpc_xprt *xprt)
{
	return xprt->ops->enable_swap(xprt);
}

static inline void
xprt_disable_swap(struct rpc_xprt *xprt)
{
	xprt->ops->disable_swap(xprt);
}

/*
 * Transport switch helper functions
 */
int			xprt_register_transport(struct xprt_class *type);
int			xprt_unregister_transport(struct xprt_class *type);
int			xprt_load_transport(const char *);
void			xprt_set_retrans_timeout_def(struct rpc_task *task);
void			xprt_set_retrans_timeout_rtt(struct rpc_task *task);
void			xprt_wake_pending_tasks(struct rpc_xprt *xprt, int status);
void			xprt_wait_for_buffer_space(struct rpc_task *task, rpc_action action);
void			xprt_write_space(struct rpc_xprt *xprt);
void			xprt_adjust_cwnd(struct rpc_xprt *xprt, struct rpc_task *task, int result);
struct rpc_rqst *	xprt_lookup_rqst(struct rpc_xprt *xprt, __be32 xid);
void			xprt_update_rtt(struct rpc_task *task);
void			xprt_complete_rqst(struct rpc_task *task, int copied);
void			xprt_pin_rqst(struct rpc_rqst *req);
void			xprt_unpin_rqst(struct rpc_rqst *req);
void			xprt_release_rqst_cong(struct rpc_task *task);
void			xprt_disconnect_done(struct rpc_xprt *xprt);
void			xprt_force_disconnect(struct rpc_xprt *xprt);
void			xprt_conditional_disconnect(struct rpc_xprt *xprt, unsigned int cookie);

bool			xprt_lock_connect(struct rpc_xprt *, struct rpc_task *, void *);
void			xprt_unlock_connect(struct rpc_xprt *, void *);

/*
 * Reserved bit positions in xprt->state
 */
#define XPRT_LOCKED		(0)
#define XPRT_CONNECTED		(1)
#define XPRT_CONNECTING		(2)
#define XPRT_CLOSE_WAIT		(3)
#define XPRT_BOUND		(4)
#define XPRT_BINDING		(5)
#define XPRT_CLOSING		(6)
#define XPRT_CONGESTED		(9)

static inline void xprt_set_connected(struct rpc_xprt *xprt)
{
	set_bit(XPRT_CONNECTED, &xprt->state);
}

static inline void xprt_clear_connected(struct rpc_xprt *xprt)
{
	clear_bit(XPRT_CONNECTED, &xprt->state);
}

static inline int xprt_connected(struct rpc_xprt *xprt)
{
	return test_bit(XPRT_CONNECTED, &xprt->state);
}

static inline int xprt_test_and_set_connected(struct rpc_xprt *xprt)
{
	return test_and_set_bit(XPRT_CONNECTED, &xprt->state);
}

static inline int xprt_test_and_clear_connected(struct rpc_xprt *xprt)
{
	return test_and_clear_bit(XPRT_CONNECTED, &xprt->state);
}

static inline void xprt_clear_connecting(struct rpc_xprt *xprt)
{
	smp_mb__before_atomic();
	clear_bit(XPRT_CONNECTING, &xprt->state);
	smp_mb__after_atomic();
}

static inline int xprt_connecting(struct rpc_xprt *xprt)
{
	return test_bit(XPRT_CONNECTING, &xprt->state);
}

static inline int xprt_test_and_set_connecting(struct rpc_xprt *xprt)
{
	return test_and_set_bit(XPRT_CONNECTING, &xprt->state);
}

static inline int xprt_close_wait(struct rpc_xprt *xprt)
{
	return test_bit(XPRT_CLOSE_WAIT, &xprt->state);
}

static inline void xprt_set_bound(struct rpc_xprt *xprt)
{
	test_and_set_bit(XPRT_BOUND, &xprt->state);
}

static inline int xprt_bound(struct rpc_xprt *xprt)
{
	return test_bit(XPRT_BOUND, &xprt->state);
}

static inline void xprt_clear_bound(struct rpc_xprt *xprt)
{
	clear_bit(XPRT_BOUND, &xprt->state);
}

static inline void xprt_clear_binding(struct rpc_xprt *xprt)
{
	smp_mb__before_atomic();
	clear_bit(XPRT_BINDING, &xprt->state);
	smp_mb__after_atomic();
}

static inline int xprt_test_and_set_binding(struct rpc_xprt *xprt)
{
	return test_and_set_bit(XPRT_BINDING, &xprt->state);
}

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
extern unsigned int rpc_inject_disconnect;
static inline void xprt_inject_disconnect(struct rpc_xprt *xprt)
{
	if (!rpc_inject_disconnect)
		return;
	if (atomic_dec_return(&xprt->inject_disconnect))
		return;
	atomic_set(&xprt->inject_disconnect, rpc_inject_disconnect);
	xprt->ops->inject_disconnect(xprt);
}
#else
static inline void xprt_inject_disconnect(struct rpc_xprt *xprt)
{
}
#endif

#endif /* __KERNEL__*/

#endif /* _LINUX_SUNRPC_XPRT_H */
