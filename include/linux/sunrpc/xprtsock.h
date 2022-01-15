/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  linux/include/linux/sunrpc/xprtsock.h
 *
 *  Declarations for the RPC transport socket provider.
 */

#ifndef _LINUX_SUNRPC_XPRTSOCK_H
#define _LINUX_SUNRPC_XPRTSOCK_H

int		init_socket_xprt(void);
void		cleanup_socket_xprt(void);
unsigned short	get_srcport(struct rpc_xprt *);

#define RPC_MIN_RESVPORT	(1U)
#define RPC_MAX_RESVPORT	(65535U)
#define RPC_DEF_MIN_RESVPORT	(665U)
#define RPC_DEF_MAX_RESVPORT	(1023U)

struct sock_xprt {
	struct rpc_xprt		xprt;

	/*
	 * Network layer
	 */
	struct socket *		sock;
	struct sock *		inet;
	struct file *		file;

	/*
	 * State of TCP reply receive
	 */
	struct {
		struct {
			__be32	fraghdr,
				xid,
				calldir;
		} __attribute__((packed));

		u32		offset,
				len;

		unsigned long	copied;
	} recv;

	/*
	 * State of TCP transmit queue
	 */
	struct {
		u32		offset;
	} xmit;

	/*
	 * Connection of transports
	 */
	unsigned long		sock_state;
	struct delayed_work	connect_worker;
	struct work_struct	error_worker;
	struct work_struct	recv_worker;
	struct mutex		recv_mutex;
	struct sockaddr_storage	srcaddr;
	unsigned short		srcport;
	int			xprt_err;

	/*
	 * UDP socket buffer size parameters
	 */
	size_t			rcvsize,
				sndsize;

	struct rpc_timeout	tcp_timeout;

	/*
	 * Saved socket callback addresses
	 */
	void			(*old_data_ready)(struct sock *);
	void			(*old_state_change)(struct sock *);
	void			(*old_write_space)(struct sock *);
	void			(*old_error_report)(struct sock *);
};

/*
 * TCP RPC flags
 */
#define XPRT_SOCK_CONNECTING	1U
#define XPRT_SOCK_DATA_READY	(2)
#define XPRT_SOCK_UPD_TIMEOUT	(3)
#define XPRT_SOCK_WAKE_ERROR	(4)
#define XPRT_SOCK_WAKE_WRITE	(5)
#define XPRT_SOCK_WAKE_PENDING	(6)
#define XPRT_SOCK_WAKE_DISCONNECT	(7)

#endif /* _LINUX_SUNRPC_XPRTSOCK_H */
