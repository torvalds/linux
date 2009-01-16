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
#include <linux/kref.h>
#include <linux/sunrpc/sched.h>
#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/msg_prot.h>

#ifdef __KERNEL__

#define RPC_MIN_SLOT_TABLE	(2U)
#define RPC_DEF_SLOT_TABLE	(16U)
#define RPC_MAX_SLOT_TABLE	(128U)

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
	RPC_DISPLAY_ALL,
	RPC_DISPLAY_HEX_ADDR,
	RPC_DISPLAY_HEX_PORT,
	RPC_DISPLAY_UNIVERSAL_ADDR,
	RPC_DISPLAY_NETID,
	RPC_DISPLAY_MAX,
};

struct rpc_task;
struct rpc_xprt;
struct seq_file;

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
	__be32			rq_xid;		/* request XID */
	int			rq_cong;	/* has incremented xprt->cong */
	int			rq_received;	/* receive completed */
	u32			rq_seqno;	/* gss seq no. used on req. */
	int			rq_enc_pages_num;
	struct page		**rq_enc_pages;	/* scratch pages for use by
						   gss privacy code */
	void (*rq_release_snd_buf)(struct rpc_rqst *); /* release rq_enc_pages */
	struct list_head	rq_list;

	__u32 *			rq_buffer;	/* XDR encode buffer */
	size_t			rq_callsize,
				rq_rcvsize;

	struct xdr_buf		rq_private_buf;		/* The receive buffer
							 * used in the softirq.
							 */
	unsigned long		rq_majortimeo;	/* major timeout alarm */
	unsigned long		rq_timeout;	/* Current timeout value */
	unsigned int		rq_retries;	/* # of retries */
	unsigned int		rq_connect_cookie;
						/* A cookie used to track the
						   state of the transport
						   connection */
	
	/*
	 * Partial send handling
	 */
	u32			rq_bytes_sent;	/* Bytes we have sent */

	unsigned long		rq_xtime;	/* when transmitted */
	int			rq_ntrans;
};
#define rq_svec			rq_snd_buf.head
#define rq_slen			rq_snd_buf.len

struct rpc_xprt_ops {
	void		(*set_buffer_size)(struct rpc_xprt *xprt, size_t sndsize, size_t rcvsize);
	int		(*reserve_xprt)(struct rpc_task *task);
	void		(*release_xprt)(struct rpc_xprt *xprt, struct rpc_task *task);
	void		(*rpcbind)(struct rpc_task *task);
	void		(*set_port)(struct rpc_xprt *xprt, unsigned short port);
	void		(*connect)(struct rpc_task *task);
	void *		(*buf_alloc)(struct rpc_task *task, size_t size);
	void		(*buf_free)(void *buffer);
	int		(*send_request)(struct rpc_task *task);
	void		(*set_retrans_timeout)(struct rpc_task *task);
	void		(*timer)(struct rpc_task *task);
	void		(*release_request)(struct rpc_task *task);
	void		(*close)(struct rpc_xprt *xprt);
	void		(*destroy)(struct rpc_xprt *xprt);
	void		(*print_stats)(struct rpc_xprt *xprt, struct seq_file *seq);
};

struct rpc_xprt {
	struct kref		kref;		/* Reference count */
	struct rpc_xprt_ops *	ops;		/* transport methods */

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
	struct rpc_wait_queue	resend;		/* requests waiting to resend */
	struct rpc_wait_queue	pending;	/* requests in flight */
	struct rpc_wait_queue	backlog;	/* waiting for slot */
	struct list_head	free;		/* free slots */
	struct rpc_rqst *	slot;		/* slot table storage */
	unsigned int		max_reqs;	/* total slots */
	unsigned long		state;		/* transport state */
	unsigned char		shutdown   : 1,	/* being shut down */
				resvport   : 1; /* use a reserved port */
	unsigned int		bind_index;	/* bind function index */

	/*
	 * Connection of transports
	 */
	unsigned long		connect_timeout,
				bind_timeout,
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
				idle_timeout;

	/*
	 * Send stuff
	 */
	spinlock_t		transport_lock;	/* lock transport info */
	spinlock_t		reserve_lock;	/* lock slot table */
	u32			xid;		/* Next XID value to use */
	struct rpc_task *	snd_task;	/* Task blocked in send */
	struct list_head	recv;

	struct {
		unsigned long		bind_count,	/* total number of binds */
					connect_count,	/* total number of connects */
					connect_start,	/* connect start timestamp */
					connect_time,	/* jiffies waiting for connect */
					sends,		/* how many complete requests */
					recvs,		/* how many complete requests */
					bad_xids;	/* lookup_rqst didn't find XID */

		unsigned long long	req_u,		/* average requests on the wire */
					bklog_u;	/* backlog queue utilization */
	} stat;

	const char		*address_strings[RPC_DISPLAY_MAX];
};

struct xprt_create {
	int			ident;		/* XPRT_TRANSPORT identifier */
	struct sockaddr *	srcaddr;	/* optional local address */
	struct sockaddr *	dstaddr;	/* remote peer address */
	size_t			addrlen;
};

struct xprt_class {
	struct list_head	list;
	int			ident;		/* XPRT_TRANSPORT identifier */
	struct rpc_xprt *	(*setup)(struct xprt_create *);
	struct module		*owner;
	char			name[32];
};

/*
 * Generic internal transport functions
 */
struct rpc_xprt		*xprt_create_transport(struct xprt_create *args);
void			xprt_connect(struct rpc_task *task);
void			xprt_reserve(struct rpc_task *task);
int			xprt_reserve_xprt(struct rpc_task *task);
int			xprt_reserve_xprt_cong(struct rpc_task *task);
int			xprt_prepare_transmit(struct rpc_task *task);
void			xprt_transmit(struct rpc_task *task);
void			xprt_end_transmit(struct rpc_task *task);
int			xprt_adjust_timeout(struct rpc_rqst *req);
void			xprt_release_xprt(struct rpc_xprt *xprt, struct rpc_task *task);
void			xprt_release_xprt_cong(struct rpc_xprt *xprt, struct rpc_task *task);
void			xprt_release(struct rpc_task *task);
struct rpc_xprt *	xprt_get(struct rpc_xprt *xprt);
void			xprt_put(struct rpc_xprt *xprt);

static inline __be32 *xprt_skip_transport_header(struct rpc_xprt *xprt, __be32 *p)
{
	return p + xprt->tsh_size;
}

/*
 * Transport switch helper functions
 */
int			xprt_register_transport(struct xprt_class *type);
int			xprt_unregister_transport(struct xprt_class *type);
void			xprt_set_retrans_timeout_def(struct rpc_task *task);
void			xprt_set_retrans_timeout_rtt(struct rpc_task *task);
void			xprt_wake_pending_tasks(struct rpc_xprt *xprt, int status);
void			xprt_wait_for_buffer_space(struct rpc_task *task, rpc_action action);
void			xprt_write_space(struct rpc_xprt *xprt);
void			xprt_update_rtt(struct rpc_task *task);
void			xprt_adjust_cwnd(struct rpc_task *task, int result);
struct rpc_rqst *	xprt_lookup_rqst(struct rpc_xprt *xprt, __be32 xid);
void			xprt_complete_rqst(struct rpc_task *task, int copied);
void			xprt_release_rqst_cong(struct rpc_task *task);
void			xprt_disconnect_done(struct rpc_xprt *xprt);
void			xprt_force_disconnect(struct rpc_xprt *xprt);
void			xprt_conditional_disconnect(struct rpc_xprt *xprt, unsigned int cookie);

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
	smp_mb__before_clear_bit();
	clear_bit(XPRT_CONNECTING, &xprt->state);
	smp_mb__after_clear_bit();
}

static inline int xprt_connecting(struct rpc_xprt *xprt)
{
	return test_bit(XPRT_CONNECTING, &xprt->state);
}

static inline int xprt_test_and_set_connecting(struct rpc_xprt *xprt)
{
	return test_and_set_bit(XPRT_CONNECTING, &xprt->state);
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
	smp_mb__before_clear_bit();
	clear_bit(XPRT_BINDING, &xprt->state);
	smp_mb__after_clear_bit();
}

static inline int xprt_test_and_set_binding(struct rpc_xprt *xprt)
{
	return test_and_set_bit(XPRT_BINDING, &xprt->state);
}

#endif /* __KERNEL__*/

#endif /* _LINUX_SUNRPC_XPRT_H */
