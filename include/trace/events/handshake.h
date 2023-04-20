/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM handshake

#if !defined(_TRACE_HANDSHAKE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HANDSHAKE_H

#include <linux/net.h>
#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(handshake_event_class,
	TP_PROTO(
		const struct net *net,
		const struct handshake_req *req,
		const struct sock *sk
	),
	TP_ARGS(net, req, sk),
	TP_STRUCT__entry(
		__field(const void *, req)
		__field(const void *, sk)
		__field(unsigned int, netns_ino)
	),
	TP_fast_assign(
		__entry->req = req;
		__entry->sk = sk;
		__entry->netns_ino = net->ns.inum;
	),
	TP_printk("req=%p sk=%p",
		__entry->req, __entry->sk
	)
);
#define DEFINE_HANDSHAKE_EVENT(name)				\
	DEFINE_EVENT(handshake_event_class, name,		\
		TP_PROTO(					\
			const struct net *net,			\
			const struct handshake_req *req,	\
			const struct sock *sk			\
		),						\
		TP_ARGS(net, req, sk))

DECLARE_EVENT_CLASS(handshake_fd_class,
	TP_PROTO(
		const struct net *net,
		const struct handshake_req *req,
		const struct sock *sk,
		int fd
	),
	TP_ARGS(net, req, sk, fd),
	TP_STRUCT__entry(
		__field(const void *, req)
		__field(const void *, sk)
		__field(int, fd)
		__field(unsigned int, netns_ino)
	),
	TP_fast_assign(
		__entry->req = req;
		__entry->sk = req->hr_sk;
		__entry->fd = fd;
		__entry->netns_ino = net->ns.inum;
	),
	TP_printk("req=%p sk=%p fd=%d",
		__entry->req, __entry->sk, __entry->fd
	)
);
#define DEFINE_HANDSHAKE_FD_EVENT(name)				\
	DEFINE_EVENT(handshake_fd_class, name,			\
		TP_PROTO(					\
			const struct net *net,			\
			const struct handshake_req *req,	\
			const struct sock *sk,			\
			int fd					\
		),						\
		TP_ARGS(net, req, sk, fd))

DECLARE_EVENT_CLASS(handshake_error_class,
	TP_PROTO(
		const struct net *net,
		const struct handshake_req *req,
		const struct sock *sk,
		int err
	),
	TP_ARGS(net, req, sk, err),
	TP_STRUCT__entry(
		__field(const void *, req)
		__field(const void *, sk)
		__field(int, err)
		__field(unsigned int, netns_ino)
	),
	TP_fast_assign(
		__entry->req = req;
		__entry->sk = sk;
		__entry->err = err;
		__entry->netns_ino = net->ns.inum;
	),
	TP_printk("req=%p sk=%p err=%d",
		__entry->req, __entry->sk, __entry->err
	)
);
#define DEFINE_HANDSHAKE_ERROR(name)				\
	DEFINE_EVENT(handshake_error_class, name,		\
		TP_PROTO(					\
			const struct net *net,			\
			const struct handshake_req *req,	\
			const struct sock *sk,			\
			int err					\
		),						\
		TP_ARGS(net, req, sk, err))


/*
 * Request lifetime events
 */

DEFINE_HANDSHAKE_EVENT(handshake_submit);
DEFINE_HANDSHAKE_ERROR(handshake_submit_err);
DEFINE_HANDSHAKE_EVENT(handshake_cancel);
DEFINE_HANDSHAKE_EVENT(handshake_cancel_none);
DEFINE_HANDSHAKE_EVENT(handshake_cancel_busy);
DEFINE_HANDSHAKE_EVENT(handshake_destruct);


TRACE_EVENT(handshake_complete,
	TP_PROTO(
		const struct net *net,
		const struct handshake_req *req,
		const struct sock *sk,
		int status
	),
	TP_ARGS(net, req, sk, status),
	TP_STRUCT__entry(
		__field(const void *, req)
		__field(const void *, sk)
		__field(int, status)
		__field(unsigned int, netns_ino)
	),
	TP_fast_assign(
		__entry->req = req;
		__entry->sk = sk;
		__entry->status = status;
		__entry->netns_ino = net->ns.inum;
	),
	TP_printk("req=%p sk=%p status=%d",
		__entry->req, __entry->sk, __entry->status
	)
);

/*
 * Netlink events
 */

DEFINE_HANDSHAKE_ERROR(handshake_notify_err);
DEFINE_HANDSHAKE_FD_EVENT(handshake_cmd_accept);
DEFINE_HANDSHAKE_ERROR(handshake_cmd_accept_err);
DEFINE_HANDSHAKE_FD_EVENT(handshake_cmd_done);
DEFINE_HANDSHAKE_ERROR(handshake_cmd_done_err);

#endif /* _TRACE_HANDSHAKE_H */

#include <trace/define_trace.h>
