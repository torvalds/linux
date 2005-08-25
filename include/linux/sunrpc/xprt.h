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
#include <linux/sunrpc/sched.h>
#include <linux/sunrpc/xdr.h>

/*
 * The transport code maintains an estimate on the maximum number of out-
 * standing RPC requests, using a smoothed version of the congestion
 * avoidance implemented in 44BSD. This is basically the Van Jacobson
 * congestion algorithm: If a retransmit occurs, the congestion window is
 * halved; otherwise, it is incremented by 1/cwnd when
 *
 *	-	a reply is received and
 *	-	a full number of requests are outstanding and
 *	-	the congestion window hasn't been updated recently.
 *
 * Upper procedures may check whether a request would block waiting for
 * a free RPC slot by using the RPC_CONGESTED() macro.
 */
extern unsigned int xprt_udp_slot_table_entries;
extern unsigned int xprt_tcp_slot_table_entries;

#define RPC_MIN_SLOT_TABLE	(2U)
#define RPC_DEF_SLOT_TABLE	(16U)
#define RPC_MAX_SLOT_TABLE	(128U)

#define RPC_CWNDSHIFT		(8U)
#define RPC_CWNDSCALE		(1U << RPC_CWNDSHIFT)
#define RPC_INITCWND		RPC_CWNDSCALE
#define RPC_MAXCWND(xprt)	((xprt)->max_reqs << RPC_CWNDSHIFT)
#define RPCXPRT_CONGESTED(xprt) ((xprt)->cong >= (xprt)->cwnd)

/* Default timeout values */
#define RPC_MAX_UDP_TIMEOUT	(60*HZ)
#define RPC_MAX_TCP_TIMEOUT	(600*HZ)

/*
 * Wait duration for an RPC TCP connection to be established.  Solaris
 * NFS over TCP uses 60 seconds, for example, which is in line with how
 * long a server takes to reboot.
 */
#define RPC_CONNECT_TIMEOUT	(60*HZ)

/*
 * Delay an arbitrary number of seconds before attempting to reconnect
 * after an error.
 */
#define RPC_REESTABLISH_TIMEOUT	(15*HZ)

/*
 * RPC transport idle timeout.
 */
#define RPC_IDLE_DISCONNECT_TIMEOUT	(5*60*HZ)

/*
 * RPC call and reply header size as number of 32bit words (verifier
 * size computed separately)
 */
#define RPC_CALLHDRSIZE		6
#define RPC_REPHDRSIZE		4

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
	__u32			rq_xid;		/* request XID */
	int			rq_cong;	/* has incremented xprt->cong */
	int			rq_received;	/* receive completed */
	u32			rq_seqno;	/* gss seq no. used on req. */

	struct list_head	rq_list;

	struct xdr_buf		rq_private_buf;		/* The receive buffer
							 * used in the softirq.
							 */
	unsigned long		rq_majortimeo;	/* major timeout alarm */
	unsigned long		rq_timeout;	/* Current timeout value */
	unsigned int		rq_retries;	/* # of retries */
	/*
	 * For authentication (e.g. auth_des)
	 */
	u32			rq_creddata[2];
	
	/*
	 * Partial send handling
	 */
	
	u32			rq_bytes_sent;	/* Bytes we have sent */

	unsigned long		rq_xtime;	/* when transmitted */
	int			rq_ntrans;
};
#define rq_svec			rq_snd_buf.head
#define rq_slen			rq_snd_buf.len

struct rpc_task;
struct rpc_xprt;

struct rpc_xprt_ops {
	void		(*set_buffer_size)(struct rpc_xprt *xprt);
	void		(*connect)(struct rpc_task *task);
	int		(*send_request)(struct rpc_task *task);
	void		(*close)(struct rpc_xprt *xprt);
	void		(*destroy)(struct rpc_xprt *xprt);
};

struct rpc_xprt {
	struct rpc_xprt_ops *	ops;		/* transport methods */
	struct socket *		sock;		/* BSD socket layer */
	struct sock *		inet;		/* INET layer */

	struct rpc_timeout	timeout;	/* timeout parms */
	struct sockaddr_in	addr;		/* server address */
	int			prot;		/* IP protocol */

	unsigned long		cong;		/* current congestion */
	unsigned long		cwnd;		/* congestion window */

	unsigned int		rcvsize,	/* transport rcv buffer size */
				sndsize;	/* transport send buffer size */

	size_t			max_payload;	/* largest RPC payload size,
						   in bytes */
	unsigned int		tsh_size;	/* size of transport specific
						   header */

	struct rpc_wait_queue	sending;	/* requests waiting to send */
	struct rpc_wait_queue	resend;		/* requests waiting to resend */
	struct rpc_wait_queue	pending;	/* requests in flight */
	struct rpc_wait_queue	backlog;	/* waiting for slot */
	struct list_head	free;		/* free slots */
	struct rpc_rqst *	slot;		/* slot table storage */
	unsigned int		max_reqs;	/* total slots */
	unsigned long		state;		/* transport state */
	unsigned char		shutdown   : 1,	/* being shut down */
				nocong	   : 1,	/* no congestion control */
				resvport   : 1, /* use a reserved port */
				stream     : 1;	/* TCP */

	/*
	 * XID
	 */
	__u32			xid;		/* Next XID value to use */

	/*
	 * State of TCP reply receive stuff
	 */
	u32			tcp_recm,	/* Fragment header */
				tcp_xid,	/* Current XID */
				tcp_reclen,	/* fragment length */
				tcp_offset;	/* fragment offset */
	unsigned long		tcp_copied,	/* copied to request */
				tcp_flags;
	/*
	 * Connection of transports
	 */
	struct work_struct	connect_worker;
	unsigned short		port;
	/*
	 * Disconnection of idle transports
	 */
	struct work_struct	task_cleanup;
	struct timer_list	timer;
	unsigned long		last_used;

	/*
	 * Send stuff
	 */
	spinlock_t		transport_lock;	/* lock transport info */
	spinlock_t		reserve_lock;	/* lock slot table */
	struct rpc_task *	snd_task;	/* Task blocked in send */

	struct list_head	recv;


	void			(*old_data_ready)(struct sock *, int);
	void			(*old_state_change)(struct sock *);
	void			(*old_write_space)(struct sock *);

	wait_queue_head_t	cong_wait;
};

#define XPRT_LAST_FRAG		(1 << 0)
#define XPRT_COPY_RECM		(1 << 1)
#define XPRT_COPY_XID		(1 << 2)
#define XPRT_COPY_DATA		(1 << 3)

#ifdef __KERNEL__

/*
 * Transport operations used by ULPs
 */
struct rpc_xprt *	xprt_create_proto(int proto, struct sockaddr_in *addr, struct rpc_timeout *to);
void			xprt_set_timeout(struct rpc_timeout *to, unsigned int retr, unsigned long incr);

/*
 * Generic internal transport functions
 */
void			xprt_connect(struct rpc_task *task);
void			xprt_reserve(struct rpc_task *task);
int			xprt_prepare_transmit(struct rpc_task *task);
void			xprt_transmit(struct rpc_task *task);
int			xprt_adjust_timeout(struct rpc_rqst *req);
void			xprt_release(struct rpc_task *task);
int			xprt_destroy(struct rpc_xprt *xprt);

static inline u32 *xprt_skip_transport_header(struct rpc_xprt *xprt, u32 *p)
{
	return p + xprt->tsh_size;
}

/*
 * Transport switch helper functions
 */
void			xprt_wake_pending_tasks(struct rpc_xprt *xprt, int status);
void			xprt_wait_for_buffer_space(struct rpc_task *task);
void			xprt_write_space(struct rpc_xprt *xprt);
struct rpc_rqst *	xprt_lookup_rqst(struct rpc_xprt *xprt, u32 xid);
void			xprt_complete_rqst(struct rpc_xprt *xprt, struct rpc_rqst *req, int copied);
void			xprt_disconnect(struct rpc_xprt *xprt);

/*
 * Socket transport setup operations
 */
int			xs_setup_udp(struct rpc_xprt *xprt, struct rpc_timeout *to);
int			xs_setup_tcp(struct rpc_xprt *xprt, struct rpc_timeout *to);

/*
 * Reserved bit positions in xprt->state
 */
#define XPRT_LOCKED		(0)
#define XPRT_CONNECTED		(1)
#define XPRT_CONNECTING		(2)

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

#endif /* __KERNEL__*/

#endif /* _LINUX_SUNRPC_XPRT_H */
