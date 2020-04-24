/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM sunrpc

#if !defined(_TRACE_SUNRPC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SUNRPC_H

#include <linux/sunrpc/sched.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/svc.h>
#include <linux/sunrpc/xprtsock.h>
#include <linux/sunrpc/svc_xprt.h>
#include <net/tcp_states.h>
#include <linux/net.h>
#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(xdr_buf_class,
	TP_PROTO(
		const struct xdr_buf *xdr
	),

	TP_ARGS(xdr),

	TP_STRUCT__entry(
		__field(const void *, head_base)
		__field(size_t, head_len)
		__field(const void *, tail_base)
		__field(size_t, tail_len)
		__field(unsigned int, page_len)
		__field(unsigned int, msg_len)
	),

	TP_fast_assign(
		__entry->head_base = xdr->head[0].iov_base;
		__entry->head_len = xdr->head[0].iov_len;
		__entry->tail_base = xdr->tail[0].iov_base;
		__entry->tail_len = xdr->tail[0].iov_len;
		__entry->page_len = xdr->page_len;
		__entry->msg_len = xdr->len;
	),

	TP_printk("head=[%p,%zu] page=%u tail=[%p,%zu] len=%u",
		__entry->head_base, __entry->head_len, __entry->page_len,
		__entry->tail_base, __entry->tail_len, __entry->msg_len
	)
);

#define DEFINE_XDRBUF_EVENT(name)					\
		DEFINE_EVENT(xdr_buf_class, name,			\
				TP_PROTO(				\
					const struct xdr_buf *xdr	\
				),					\
				TP_ARGS(xdr))

DEFINE_XDRBUF_EVENT(xprt_sendto);
DEFINE_XDRBUF_EVENT(xprt_recvfrom);
DEFINE_XDRBUF_EVENT(svc_recvfrom);
DEFINE_XDRBUF_EVENT(svc_sendto);

TRACE_DEFINE_ENUM(RPC_AUTH_OK);
TRACE_DEFINE_ENUM(RPC_AUTH_BADCRED);
TRACE_DEFINE_ENUM(RPC_AUTH_REJECTEDCRED);
TRACE_DEFINE_ENUM(RPC_AUTH_BADVERF);
TRACE_DEFINE_ENUM(RPC_AUTH_REJECTEDVERF);
TRACE_DEFINE_ENUM(RPC_AUTH_TOOWEAK);
TRACE_DEFINE_ENUM(RPCSEC_GSS_CREDPROBLEM);
TRACE_DEFINE_ENUM(RPCSEC_GSS_CTXPROBLEM);

#define rpc_show_auth_stat(status)					\
	__print_symbolic(status,					\
		{ RPC_AUTH_OK,			"AUTH_OK" },		\
		{ RPC_AUTH_BADCRED,		"BADCRED" },		\
		{ RPC_AUTH_REJECTEDCRED,	"REJECTEDCRED" },	\
		{ RPC_AUTH_BADVERF,		"BADVERF" },		\
		{ RPC_AUTH_REJECTEDVERF,	"REJECTEDVERF" },	\
		{ RPC_AUTH_TOOWEAK,		"TOOWEAK" },		\
		{ RPCSEC_GSS_CREDPROBLEM,	"GSS_CREDPROBLEM" },	\
		{ RPCSEC_GSS_CTXPROBLEM,	"GSS_CTXPROBLEM" })	\

DECLARE_EVENT_CLASS(rpc_task_status,

	TP_PROTO(const struct rpc_task *task),

	TP_ARGS(task),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(int, status)
	),

	TP_fast_assign(
		__entry->task_id = task->tk_pid;
		__entry->client_id = task->tk_client->cl_clid;
		__entry->status = task->tk_status;
	),

	TP_printk("task:%u@%u status=%d",
		__entry->task_id, __entry->client_id,
		__entry->status)
);
#define DEFINE_RPC_STATUS_EVENT(name) \
	DEFINE_EVENT(rpc_task_status, rpc_##name##_status, \
			TP_PROTO( \
				const struct rpc_task *task \
			), \
			TP_ARGS(task))

DEFINE_RPC_STATUS_EVENT(call);
DEFINE_RPC_STATUS_EVENT(bind);
DEFINE_RPC_STATUS_EVENT(connect);

TRACE_EVENT(rpc_request,
	TP_PROTO(const struct rpc_task *task),

	TP_ARGS(task),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(int, version)
		__field(bool, async)
		__string(progname, task->tk_client->cl_program->name)
		__string(procname, rpc_proc_name(task))
	),

	TP_fast_assign(
		__entry->task_id = task->tk_pid;
		__entry->client_id = task->tk_client->cl_clid;
		__entry->version = task->tk_client->cl_vers;
		__entry->async = RPC_IS_ASYNC(task);
		__assign_str(progname, task->tk_client->cl_program->name)
		__assign_str(procname, rpc_proc_name(task))
	),

	TP_printk("task:%u@%u %sv%d %s (%ssync)",
		__entry->task_id, __entry->client_id,
		__get_str(progname), __entry->version,
		__get_str(procname), __entry->async ? "a": ""
		)
);

TRACE_DEFINE_ENUM(RPC_TASK_ASYNC);
TRACE_DEFINE_ENUM(RPC_TASK_SWAPPER);
TRACE_DEFINE_ENUM(RPC_CALL_MAJORSEEN);
TRACE_DEFINE_ENUM(RPC_TASK_ROOTCREDS);
TRACE_DEFINE_ENUM(RPC_TASK_DYNAMIC);
TRACE_DEFINE_ENUM(RPC_TASK_SOFT);
TRACE_DEFINE_ENUM(RPC_TASK_SOFTCONN);
TRACE_DEFINE_ENUM(RPC_TASK_SENT);
TRACE_DEFINE_ENUM(RPC_TASK_TIMEOUT);
TRACE_DEFINE_ENUM(RPC_TASK_NOCONNECT);
TRACE_DEFINE_ENUM(RPC_TASK_NO_RETRANS_TIMEOUT);

#define rpc_show_task_flags(flags)					\
	__print_flags(flags, "|",					\
		{ RPC_TASK_ASYNC, "ASYNC" },				\
		{ RPC_TASK_SWAPPER, "SWAPPER" },			\
		{ RPC_CALL_MAJORSEEN, "MAJORSEEN" },			\
		{ RPC_TASK_ROOTCREDS, "ROOTCREDS" },			\
		{ RPC_TASK_DYNAMIC, "DYNAMIC" },			\
		{ RPC_TASK_SOFT, "SOFT" },				\
		{ RPC_TASK_SOFTCONN, "SOFTCONN" },			\
		{ RPC_TASK_SENT, "SENT" },				\
		{ RPC_TASK_TIMEOUT, "TIMEOUT" },			\
		{ RPC_TASK_NOCONNECT, "NOCONNECT" },			\
		{ RPC_TASK_NO_RETRANS_TIMEOUT, "NORTO" })

TRACE_DEFINE_ENUM(RPC_TASK_RUNNING);
TRACE_DEFINE_ENUM(RPC_TASK_QUEUED);
TRACE_DEFINE_ENUM(RPC_TASK_ACTIVE);
TRACE_DEFINE_ENUM(RPC_TASK_NEED_XMIT);
TRACE_DEFINE_ENUM(RPC_TASK_NEED_RECV);
TRACE_DEFINE_ENUM(RPC_TASK_MSG_PIN_WAIT);
TRACE_DEFINE_ENUM(RPC_TASK_SIGNALLED);

#define rpc_show_runstate(flags)					\
	__print_flags(flags, "|",					\
		{ (1UL << RPC_TASK_RUNNING), "RUNNING" },		\
		{ (1UL << RPC_TASK_QUEUED), "QUEUED" },			\
		{ (1UL << RPC_TASK_ACTIVE), "ACTIVE" },			\
		{ (1UL << RPC_TASK_NEED_XMIT), "NEED_XMIT" },		\
		{ (1UL << RPC_TASK_NEED_RECV), "NEED_RECV" },		\
		{ (1UL << RPC_TASK_MSG_PIN_WAIT), "MSG_PIN_WAIT" },	\
		{ (1UL << RPC_TASK_SIGNALLED), "SIGNALLED" })

DECLARE_EVENT_CLASS(rpc_task_running,

	TP_PROTO(const struct rpc_task *task, const void *action),

	TP_ARGS(task, action),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(const void *, action)
		__field(unsigned long, runstate)
		__field(int, status)
		__field(unsigned short, flags)
		),

	TP_fast_assign(
		__entry->client_id = task->tk_client ?
				     task->tk_client->cl_clid : -1;
		__entry->task_id = task->tk_pid;
		__entry->action = action;
		__entry->runstate = task->tk_runstate;
		__entry->status = task->tk_status;
		__entry->flags = task->tk_flags;
		),

	TP_printk("task:%u@%d flags=%s runstate=%s status=%d action=%ps",
		__entry->task_id, __entry->client_id,
		rpc_show_task_flags(__entry->flags),
		rpc_show_runstate(__entry->runstate),
		__entry->status,
		__entry->action
		)
);
#define DEFINE_RPC_RUNNING_EVENT(name) \
	DEFINE_EVENT(rpc_task_running, rpc_task_##name, \
			TP_PROTO( \
				const struct rpc_task *task, \
				const void *action \
			), \
			TP_ARGS(task, action))

DEFINE_RPC_RUNNING_EVENT(begin);
DEFINE_RPC_RUNNING_EVENT(run_action);
DEFINE_RPC_RUNNING_EVENT(complete);
DEFINE_RPC_RUNNING_EVENT(signalled);
DEFINE_RPC_RUNNING_EVENT(end);

DECLARE_EVENT_CLASS(rpc_task_queued,

	TP_PROTO(const struct rpc_task *task, const struct rpc_wait_queue *q),

	TP_ARGS(task, q),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(unsigned long, timeout)
		__field(unsigned long, runstate)
		__field(int, status)
		__field(unsigned short, flags)
		__string(q_name, rpc_qname(q))
		),

	TP_fast_assign(
		__entry->client_id = task->tk_client ?
				     task->tk_client->cl_clid : -1;
		__entry->task_id = task->tk_pid;
		__entry->timeout = rpc_task_timeout(task);
		__entry->runstate = task->tk_runstate;
		__entry->status = task->tk_status;
		__entry->flags = task->tk_flags;
		__assign_str(q_name, rpc_qname(q));
		),

	TP_printk("task:%u@%d flags=%s runstate=%s status=%d timeout=%lu queue=%s",
		__entry->task_id, __entry->client_id,
		rpc_show_task_flags(__entry->flags),
		rpc_show_runstate(__entry->runstate),
		__entry->status,
		__entry->timeout,
		__get_str(q_name)
		)
);
#define DEFINE_RPC_QUEUED_EVENT(name) \
	DEFINE_EVENT(rpc_task_queued, rpc_task_##name, \
			TP_PROTO( \
				const struct rpc_task *task, \
				const struct rpc_wait_queue *q \
			), \
			TP_ARGS(task, q))

DEFINE_RPC_QUEUED_EVENT(sleep);
DEFINE_RPC_QUEUED_EVENT(wakeup);

DECLARE_EVENT_CLASS(rpc_failure,

	TP_PROTO(const struct rpc_task *task),

	TP_ARGS(task),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
	),

	TP_fast_assign(
		__entry->task_id = task->tk_pid;
		__entry->client_id = task->tk_client->cl_clid;
	),

	TP_printk("task:%u@%u",
		__entry->task_id, __entry->client_id)
);

#define DEFINE_RPC_FAILURE(name)					\
	DEFINE_EVENT(rpc_failure, rpc_bad_##name,			\
			TP_PROTO(					\
				const struct rpc_task *task		\
			),						\
			TP_ARGS(task))

DEFINE_RPC_FAILURE(callhdr);
DEFINE_RPC_FAILURE(verifier);

DECLARE_EVENT_CLASS(rpc_reply_event,

	TP_PROTO(
		const struct rpc_task *task
	),

	TP_ARGS(task),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(u32, xid)
		__string(progname, task->tk_client->cl_program->name)
		__field(u32, version)
		__string(procname, rpc_proc_name(task))
		__string(servername, task->tk_xprt->servername)
	),

	TP_fast_assign(
		__entry->task_id = task->tk_pid;
		__entry->client_id = task->tk_client->cl_clid;
		__entry->xid = be32_to_cpu(task->tk_rqstp->rq_xid);
		__assign_str(progname, task->tk_client->cl_program->name)
		__entry->version = task->tk_client->cl_vers;
		__assign_str(procname, rpc_proc_name(task))
		__assign_str(servername, task->tk_xprt->servername)
	),

	TP_printk("task:%u@%d server=%s xid=0x%08x %sv%d %s",
		__entry->task_id, __entry->client_id, __get_str(servername),
		__entry->xid, __get_str(progname), __entry->version,
		__get_str(procname))
)

#define DEFINE_RPC_REPLY_EVENT(name)					\
	DEFINE_EVENT(rpc_reply_event, rpc__##name,			\
			TP_PROTO(					\
				const struct rpc_task *task		\
			),						\
			TP_ARGS(task))

DEFINE_RPC_REPLY_EVENT(prog_unavail);
DEFINE_RPC_REPLY_EVENT(prog_mismatch);
DEFINE_RPC_REPLY_EVENT(proc_unavail);
DEFINE_RPC_REPLY_EVENT(garbage_args);
DEFINE_RPC_REPLY_EVENT(unparsable);
DEFINE_RPC_REPLY_EVENT(mismatch);
DEFINE_RPC_REPLY_EVENT(stale_creds);
DEFINE_RPC_REPLY_EVENT(bad_creds);
DEFINE_RPC_REPLY_EVENT(auth_tooweak);

TRACE_EVENT(rpc_stats_latency,

	TP_PROTO(
		const struct rpc_task *task,
		ktime_t backlog,
		ktime_t rtt,
		ktime_t execute
	),

	TP_ARGS(task, backlog, rtt, execute),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(u32, xid)
		__field(int, version)
		__string(progname, task->tk_client->cl_program->name)
		__string(procname, rpc_proc_name(task))
		__field(unsigned long, backlog)
		__field(unsigned long, rtt)
		__field(unsigned long, execute)
	),

	TP_fast_assign(
		__entry->client_id = task->tk_client->cl_clid;
		__entry->task_id = task->tk_pid;
		__entry->xid = be32_to_cpu(task->tk_rqstp->rq_xid);
		__entry->version = task->tk_client->cl_vers;
		__assign_str(progname, task->tk_client->cl_program->name)
		__assign_str(procname, rpc_proc_name(task))
		__entry->backlog = ktime_to_us(backlog);
		__entry->rtt = ktime_to_us(rtt);
		__entry->execute = ktime_to_us(execute);
	),

	TP_printk("task:%u@%d xid=0x%08x %sv%d %s backlog=%lu rtt=%lu execute=%lu",
		__entry->task_id, __entry->client_id, __entry->xid,
		__get_str(progname), __entry->version, __get_str(procname),
		__entry->backlog, __entry->rtt, __entry->execute)
);

TRACE_EVENT(rpc_xdr_overflow,
	TP_PROTO(
		const struct xdr_stream *xdr,
		size_t requested
	),

	TP_ARGS(xdr, requested),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(int, version)
		__field(size_t, requested)
		__field(const void *, end)
		__field(const void *, p)
		__field(const void *, head_base)
		__field(size_t, head_len)
		__field(const void *, tail_base)
		__field(size_t, tail_len)
		__field(unsigned int, page_len)
		__field(unsigned int, len)
		__string(progname,
			 xdr->rqst->rq_task->tk_client->cl_program->name)
		__string(procedure,
			 xdr->rqst->rq_task->tk_msg.rpc_proc->p_name)
	),

	TP_fast_assign(
		if (xdr->rqst) {
			const struct rpc_task *task = xdr->rqst->rq_task;

			__entry->task_id = task->tk_pid;
			__entry->client_id = task->tk_client->cl_clid;
			__assign_str(progname,
				     task->tk_client->cl_program->name)
			__entry->version = task->tk_client->cl_vers;
			__assign_str(procedure, task->tk_msg.rpc_proc->p_name)
		} else {
			__entry->task_id = 0;
			__entry->client_id = 0;
			__assign_str(progname, "unknown")
			__entry->version = 0;
			__assign_str(procedure, "unknown")
		}
		__entry->requested = requested;
		__entry->end = xdr->end;
		__entry->p = xdr->p;
		__entry->head_base = xdr->buf->head[0].iov_base,
		__entry->head_len = xdr->buf->head[0].iov_len,
		__entry->page_len = xdr->buf->page_len,
		__entry->tail_base = xdr->buf->tail[0].iov_base,
		__entry->tail_len = xdr->buf->tail[0].iov_len,
		__entry->len = xdr->buf->len;
	),

	TP_printk(
		"task:%u@%u %sv%d %s requested=%zu p=%p end=%p xdr=[%p,%zu]/%u/[%p,%zu]/%u\n",
		__entry->task_id, __entry->client_id,
		__get_str(progname), __entry->version, __get_str(procedure),
		__entry->requested, __entry->p, __entry->end,
		__entry->head_base, __entry->head_len,
		__entry->page_len,
		__entry->tail_base, __entry->tail_len,
		__entry->len
	)
);

TRACE_EVENT(rpc_xdr_alignment,
	TP_PROTO(
		const struct xdr_stream *xdr,
		size_t offset,
		unsigned int copied
	),

	TP_ARGS(xdr, offset, copied),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(int, version)
		__field(size_t, offset)
		__field(unsigned int, copied)
		__field(const void *, head_base)
		__field(size_t, head_len)
		__field(const void *, tail_base)
		__field(size_t, tail_len)
		__field(unsigned int, page_len)
		__field(unsigned int, len)
		__string(progname,
			 xdr->rqst->rq_task->tk_client->cl_program->name)
		__string(procedure,
			 xdr->rqst->rq_task->tk_msg.rpc_proc->p_name)
	),

	TP_fast_assign(
		const struct rpc_task *task = xdr->rqst->rq_task;

		__entry->task_id = task->tk_pid;
		__entry->client_id = task->tk_client->cl_clid;
		__assign_str(progname,
			     task->tk_client->cl_program->name)
		__entry->version = task->tk_client->cl_vers;
		__assign_str(procedure, task->tk_msg.rpc_proc->p_name)

		__entry->offset = offset;
		__entry->copied = copied;
		__entry->head_base = xdr->buf->head[0].iov_base,
		__entry->head_len = xdr->buf->head[0].iov_len,
		__entry->page_len = xdr->buf->page_len,
		__entry->tail_base = xdr->buf->tail[0].iov_base,
		__entry->tail_len = xdr->buf->tail[0].iov_len,
		__entry->len = xdr->buf->len;
	),

	TP_printk(
		"task:%u@%u %sv%d %s offset=%zu copied=%u xdr=[%p,%zu]/%u/[%p,%zu]/%u\n",
		__entry->task_id, __entry->client_id,
		__get_str(progname), __entry->version, __get_str(procedure),
		__entry->offset, __entry->copied,
		__entry->head_base, __entry->head_len,
		__entry->page_len,
		__entry->tail_base, __entry->tail_len,
		__entry->len
	)
);

TRACE_EVENT(rpc_reply_pages,
	TP_PROTO(
		const struct rpc_rqst *req
	),

	TP_ARGS(req),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(const void *, head_base)
		__field(size_t, head_len)
		__field(const void *, tail_base)
		__field(size_t, tail_len)
		__field(unsigned int, page_len)
	),

	TP_fast_assign(
		__entry->task_id = req->rq_task->tk_pid;
		__entry->client_id = req->rq_task->tk_client->cl_clid;

		__entry->head_base = req->rq_rcv_buf.head[0].iov_base;
		__entry->head_len = req->rq_rcv_buf.head[0].iov_len;
		__entry->page_len = req->rq_rcv_buf.page_len;
		__entry->tail_base = req->rq_rcv_buf.tail[0].iov_base;
		__entry->tail_len = req->rq_rcv_buf.tail[0].iov_len;
	),

	TP_printk(
		"task:%u@%u xdr=[%p,%zu]/%u/[%p,%zu]\n",
		__entry->task_id, __entry->client_id,
		__entry->head_base, __entry->head_len,
		__entry->page_len,
		__entry->tail_base, __entry->tail_len
	)
);

/*
 * First define the enums in the below macros to be exported to userspace
 * via TRACE_DEFINE_ENUM().
 */
#undef EM
#undef EMe
#define EM(a, b)	TRACE_DEFINE_ENUM(a);
#define EMe(a, b)	TRACE_DEFINE_ENUM(a);

#define RPC_SHOW_SOCKET				\
	EM( SS_FREE, "FREE" )			\
	EM( SS_UNCONNECTED, "UNCONNECTED" )	\
	EM( SS_CONNECTING, "CONNECTING," )	\
	EM( SS_CONNECTED, "CONNECTED," )	\
	EMe(SS_DISCONNECTING, "DISCONNECTING" )

#define rpc_show_socket_state(state) \
	__print_symbolic(state, RPC_SHOW_SOCKET)

RPC_SHOW_SOCKET

#define RPC_SHOW_SOCK				\
	EM( TCP_ESTABLISHED, "ESTABLISHED" )	\
	EM( TCP_SYN_SENT, "SYN_SENT" )		\
	EM( TCP_SYN_RECV, "SYN_RECV" )		\
	EM( TCP_FIN_WAIT1, "FIN_WAIT1" )	\
	EM( TCP_FIN_WAIT2, "FIN_WAIT2" )	\
	EM( TCP_TIME_WAIT, "TIME_WAIT" )	\
	EM( TCP_CLOSE, "CLOSE" )		\
	EM( TCP_CLOSE_WAIT, "CLOSE_WAIT" )	\
	EM( TCP_LAST_ACK, "LAST_ACK" )		\
	EM( TCP_LISTEN, "LISTEN" )		\
	EMe( TCP_CLOSING, "CLOSING" )

#define rpc_show_sock_state(state) \
	__print_symbolic(state, RPC_SHOW_SOCK)

RPC_SHOW_SOCK

/*
 * Now redefine the EM() and EMe() macros to map the enums to the strings
 * that will be printed in the output.
 */
#undef EM
#undef EMe
#define EM(a, b)	{a, b},
#define EMe(a, b)	{a, b}

DECLARE_EVENT_CLASS(xs_socket_event,

		TP_PROTO(
			struct rpc_xprt *xprt,
			struct socket *socket
		),

		TP_ARGS(xprt, socket),

		TP_STRUCT__entry(
			__field(unsigned int, socket_state)
			__field(unsigned int, sock_state)
			__field(unsigned long long, ino)
			__string(dstaddr,
				xprt->address_strings[RPC_DISPLAY_ADDR])
			__string(dstport,
				xprt->address_strings[RPC_DISPLAY_PORT])
		),

		TP_fast_assign(
			struct inode *inode = SOCK_INODE(socket);
			__entry->socket_state = socket->state;
			__entry->sock_state = socket->sk->sk_state;
			__entry->ino = (unsigned long long)inode->i_ino;
			__assign_str(dstaddr,
				xprt->address_strings[RPC_DISPLAY_ADDR]);
			__assign_str(dstport,
				xprt->address_strings[RPC_DISPLAY_PORT]);
		),

		TP_printk(
			"socket:[%llu] dstaddr=%s/%s "
			"state=%u (%s) sk_state=%u (%s)",
			__entry->ino, __get_str(dstaddr), __get_str(dstport),
			__entry->socket_state,
			rpc_show_socket_state(__entry->socket_state),
			__entry->sock_state,
			rpc_show_sock_state(__entry->sock_state)
		)
);
#define DEFINE_RPC_SOCKET_EVENT(name) \
	DEFINE_EVENT(xs_socket_event, name, \
			TP_PROTO( \
				struct rpc_xprt *xprt, \
				struct socket *socket \
			), \
			TP_ARGS(xprt, socket))

DECLARE_EVENT_CLASS(xs_socket_event_done,

		TP_PROTO(
			struct rpc_xprt *xprt,
			struct socket *socket,
			int error
		),

		TP_ARGS(xprt, socket, error),

		TP_STRUCT__entry(
			__field(int, error)
			__field(unsigned int, socket_state)
			__field(unsigned int, sock_state)
			__field(unsigned long long, ino)
			__string(dstaddr,
				xprt->address_strings[RPC_DISPLAY_ADDR])
			__string(dstport,
				xprt->address_strings[RPC_DISPLAY_PORT])
		),

		TP_fast_assign(
			struct inode *inode = SOCK_INODE(socket);
			__entry->socket_state = socket->state;
			__entry->sock_state = socket->sk->sk_state;
			__entry->ino = (unsigned long long)inode->i_ino;
			__entry->error = error;
			__assign_str(dstaddr,
				xprt->address_strings[RPC_DISPLAY_ADDR]);
			__assign_str(dstport,
				xprt->address_strings[RPC_DISPLAY_PORT]);
		),

		TP_printk(
			"error=%d socket:[%llu] dstaddr=%s/%s "
			"state=%u (%s) sk_state=%u (%s)",
			__entry->error,
			__entry->ino, __get_str(dstaddr), __get_str(dstport),
			__entry->socket_state,
			rpc_show_socket_state(__entry->socket_state),
			__entry->sock_state,
			rpc_show_sock_state(__entry->sock_state)
		)
);
#define DEFINE_RPC_SOCKET_EVENT_DONE(name) \
	DEFINE_EVENT(xs_socket_event_done, name, \
			TP_PROTO( \
				struct rpc_xprt *xprt, \
				struct socket *socket, \
				int error \
			), \
			TP_ARGS(xprt, socket, error))

DEFINE_RPC_SOCKET_EVENT(rpc_socket_state_change);
DEFINE_RPC_SOCKET_EVENT_DONE(rpc_socket_connect);
DEFINE_RPC_SOCKET_EVENT_DONE(rpc_socket_error);
DEFINE_RPC_SOCKET_EVENT_DONE(rpc_socket_reset_connection);
DEFINE_RPC_SOCKET_EVENT(rpc_socket_close);
DEFINE_RPC_SOCKET_EVENT(rpc_socket_shutdown);

DECLARE_EVENT_CLASS(rpc_xprt_event,
	TP_PROTO(
		const struct rpc_xprt *xprt,
		__be32 xid,
		int status
	),

	TP_ARGS(xprt, xid, status),

	TP_STRUCT__entry(
		__field(u32, xid)
		__field(int, status)
		__string(addr, xprt->address_strings[RPC_DISPLAY_ADDR])
		__string(port, xprt->address_strings[RPC_DISPLAY_PORT])
	),

	TP_fast_assign(
		__entry->xid = be32_to_cpu(xid);
		__entry->status = status;
		__assign_str(addr, xprt->address_strings[RPC_DISPLAY_ADDR]);
		__assign_str(port, xprt->address_strings[RPC_DISPLAY_PORT]);
	),

	TP_printk("peer=[%s]:%s xid=0x%08x status=%d", __get_str(addr),
			__get_str(port), __entry->xid,
			__entry->status)
);
#define DEFINE_RPC_XPRT_EVENT(name) \
	DEFINE_EVENT(rpc_xprt_event, xprt_##name, \
			TP_PROTO( \
				const struct rpc_xprt *xprt, \
				__be32 xid, \
				int status \
			), \
			TP_ARGS(xprt, xid, status))

DEFINE_RPC_XPRT_EVENT(timer);
DEFINE_RPC_XPRT_EVENT(lookup_rqst);
DEFINE_RPC_XPRT_EVENT(complete_rqst);

TRACE_EVENT(xprt_transmit,
	TP_PROTO(
		const struct rpc_rqst *rqst,
		int status
	),

	TP_ARGS(rqst, status),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(u32, xid)
		__field(u32, seqno)
		__field(int, status)
	),

	TP_fast_assign(
		__entry->task_id = rqst->rq_task->tk_pid;
		__entry->client_id = rqst->rq_task->tk_client ?
			rqst->rq_task->tk_client->cl_clid : -1;
		__entry->xid = be32_to_cpu(rqst->rq_xid);
		__entry->seqno = rqst->rq_seqno;
		__entry->status = status;
	),

	TP_printk(
		"task:%u@%u xid=0x%08x seqno=%u status=%d",
		__entry->task_id, __entry->client_id, __entry->xid,
		__entry->seqno, __entry->status)
);

TRACE_EVENT(xprt_enq_xmit,
	TP_PROTO(
		const struct rpc_task *task,
		int stage
	),

	TP_ARGS(task, stage),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(u32, xid)
		__field(u32, seqno)
		__field(int, stage)
	),

	TP_fast_assign(
		__entry->task_id = task->tk_pid;
		__entry->client_id = task->tk_client ?
			task->tk_client->cl_clid : -1;
		__entry->xid = be32_to_cpu(task->tk_rqstp->rq_xid);
		__entry->seqno = task->tk_rqstp->rq_seqno;
		__entry->stage = stage;
	),

	TP_printk(
		"task:%u@%u xid=0x%08x seqno=%u stage=%d",
		__entry->task_id, __entry->client_id, __entry->xid,
		__entry->seqno, __entry->stage)
);

TRACE_EVENT(xprt_ping,
	TP_PROTO(const struct rpc_xprt *xprt, int status),

	TP_ARGS(xprt, status),

	TP_STRUCT__entry(
		__field(int, status)
		__string(addr, xprt->address_strings[RPC_DISPLAY_ADDR])
		__string(port, xprt->address_strings[RPC_DISPLAY_PORT])
	),

	TP_fast_assign(
		__entry->status = status;
		__assign_str(addr, xprt->address_strings[RPC_DISPLAY_ADDR]);
		__assign_str(port, xprt->address_strings[RPC_DISPLAY_PORT]);
	),

	TP_printk("peer=[%s]:%s status=%d",
			__get_str(addr), __get_str(port), __entry->status)
);

DECLARE_EVENT_CLASS(xprt_writelock_event,
	TP_PROTO(
		const struct rpc_xprt *xprt, const struct rpc_task *task
	),

	TP_ARGS(xprt, task),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(unsigned int, snd_task_id)
	),

	TP_fast_assign(
		if (task) {
			__entry->task_id = task->tk_pid;
			__entry->client_id = task->tk_client ?
					     task->tk_client->cl_clid : -1;
		} else {
			__entry->task_id = -1;
			__entry->client_id = -1;
		}
		__entry->snd_task_id = xprt->snd_task ?
					xprt->snd_task->tk_pid : -1;
	),

	TP_printk("task:%u@%u snd_task:%u",
			__entry->task_id, __entry->client_id,
			__entry->snd_task_id)
);

#define DEFINE_WRITELOCK_EVENT(name) \
	DEFINE_EVENT(xprt_writelock_event, xprt_##name, \
			TP_PROTO( \
				const struct rpc_xprt *xprt, \
				const struct rpc_task *task \
			), \
			TP_ARGS(xprt, task))

DEFINE_WRITELOCK_EVENT(reserve_xprt);
DEFINE_WRITELOCK_EVENT(release_xprt);

DECLARE_EVENT_CLASS(xprt_cong_event,
	TP_PROTO(
		const struct rpc_xprt *xprt, const struct rpc_task *task
	),

	TP_ARGS(xprt, task),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(unsigned int, snd_task_id)
		__field(unsigned long, cong)
		__field(unsigned long, cwnd)
		__field(bool, wait)
	),

	TP_fast_assign(
		if (task) {
			__entry->task_id = task->tk_pid;
			__entry->client_id = task->tk_client ?
					     task->tk_client->cl_clid : -1;
		} else {
			__entry->task_id = -1;
			__entry->client_id = -1;
		}
		__entry->snd_task_id = xprt->snd_task ?
					xprt->snd_task->tk_pid : -1;
		__entry->cong = xprt->cong;
		__entry->cwnd = xprt->cwnd;
		__entry->wait = test_bit(XPRT_CWND_WAIT, &xprt->state);
	),

	TP_printk("task:%u@%u snd_task:%u cong=%lu cwnd=%lu%s",
			__entry->task_id, __entry->client_id,
			__entry->snd_task_id, __entry->cong, __entry->cwnd,
			__entry->wait ? " (wait)" : "")
);

#define DEFINE_CONG_EVENT(name) \
	DEFINE_EVENT(xprt_cong_event, xprt_##name, \
			TP_PROTO( \
				const struct rpc_xprt *xprt, \
				const struct rpc_task *task \
			), \
			TP_ARGS(xprt, task))

DEFINE_CONG_EVENT(reserve_cong);
DEFINE_CONG_EVENT(release_cong);
DEFINE_CONG_EVENT(get_cong);
DEFINE_CONG_EVENT(put_cong);

TRACE_EVENT(xs_stream_read_data,
	TP_PROTO(struct rpc_xprt *xprt, ssize_t err, size_t total),

	TP_ARGS(xprt, err, total),

	TP_STRUCT__entry(
		__field(ssize_t, err)
		__field(size_t, total)
		__string(addr, xprt ? xprt->address_strings[RPC_DISPLAY_ADDR] :
				"(null)")
		__string(port, xprt ? xprt->address_strings[RPC_DISPLAY_PORT] :
				"(null)")
	),

	TP_fast_assign(
		__entry->err = err;
		__entry->total = total;
		__assign_str(addr, xprt ?
			xprt->address_strings[RPC_DISPLAY_ADDR] : "(null)");
		__assign_str(port, xprt ?
			xprt->address_strings[RPC_DISPLAY_PORT] : "(null)");
	),

	TP_printk("peer=[%s]:%s err=%zd total=%zu", __get_str(addr),
			__get_str(port), __entry->err, __entry->total)
);

TRACE_EVENT(xs_stream_read_request,
	TP_PROTO(struct sock_xprt *xs),

	TP_ARGS(xs),

	TP_STRUCT__entry(
		__string(addr, xs->xprt.address_strings[RPC_DISPLAY_ADDR])
		__string(port, xs->xprt.address_strings[RPC_DISPLAY_PORT])
		__field(u32, xid)
		__field(unsigned long, copied)
		__field(unsigned int, reclen)
		__field(unsigned int, offset)
	),

	TP_fast_assign(
		__assign_str(addr, xs->xprt.address_strings[RPC_DISPLAY_ADDR]);
		__assign_str(port, xs->xprt.address_strings[RPC_DISPLAY_PORT]);
		__entry->xid = be32_to_cpu(xs->recv.xid);
		__entry->copied = xs->recv.copied;
		__entry->reclen = xs->recv.len;
		__entry->offset = xs->recv.offset;
	),

	TP_printk("peer=[%s]:%s xid=0x%08x copied=%lu reclen=%u offset=%u",
			__get_str(addr), __get_str(port), __entry->xid,
			__entry->copied, __entry->reclen, __entry->offset)
);

#define show_rqstp_flags(flags)						\
	__print_flags(flags, "|",					\
		{ (1UL << RQ_SECURE),		"RQ_SECURE"},		\
		{ (1UL << RQ_LOCAL),		"RQ_LOCAL"},		\
		{ (1UL << RQ_USEDEFERRAL),	"RQ_USEDEFERRAL"},	\
		{ (1UL << RQ_DROPME),		"RQ_DROPME"},		\
		{ (1UL << RQ_SPLICE_OK),	"RQ_SPLICE_OK"},	\
		{ (1UL << RQ_VICTIM),		"RQ_VICTIM"},		\
		{ (1UL << RQ_BUSY),		"RQ_BUSY"})

TRACE_EVENT(svc_recv,
	TP_PROTO(struct svc_rqst *rqst, int len),

	TP_ARGS(rqst, len),

	TP_STRUCT__entry(
		__field(u32, xid)
		__field(int, len)
		__field(unsigned long, flags)
		__string(addr, rqst->rq_xprt->xpt_remotebuf)
	),

	TP_fast_assign(
		__entry->xid = be32_to_cpu(rqst->rq_xid);
		__entry->len = len;
		__entry->flags = rqst->rq_flags;
		__assign_str(addr, rqst->rq_xprt->xpt_remotebuf);
	),

	TP_printk("addr=%s xid=0x%08x len=%d flags=%s",
			__get_str(addr), __entry->xid, __entry->len,
			show_rqstp_flags(__entry->flags))
);

#define svc_show_status(status)				\
	__print_symbolic(status,			\
		{ SVC_GARBAGE,	"SVC_GARBAGE" },	\
		{ SVC_SYSERR,	"SVC_SYSERR" },		\
		{ SVC_VALID,	"SVC_VALID" },		\
		{ SVC_NEGATIVE,	"SVC_NEGATIVE" },	\
		{ SVC_OK,	"SVC_OK" },		\
		{ SVC_DROP,	"SVC_DROP" },		\
		{ SVC_CLOSE,	"SVC_CLOSE" },		\
		{ SVC_DENIED,	"SVC_DENIED" },		\
		{ SVC_PENDING,	"SVC_PENDING" },	\
		{ SVC_COMPLETE,	"SVC_COMPLETE" })

TRACE_EVENT(svc_authenticate,
	TP_PROTO(const struct svc_rqst *rqst, int auth_res, __be32 auth_stat),

	TP_ARGS(rqst, auth_res, auth_stat),

	TP_STRUCT__entry(
		__field(u32, xid)
		__field(unsigned long, svc_status)
		__field(unsigned long, auth_stat)
	),

	TP_fast_assign(
		__entry->xid = be32_to_cpu(rqst->rq_xid);
		__entry->svc_status = auth_res;
		__entry->auth_stat = be32_to_cpu(auth_stat);
	),

	TP_printk("xid=0x%08x auth_res=%s auth_stat=%s",
			__entry->xid, svc_show_status(__entry->svc_status),
			rpc_show_auth_stat(__entry->auth_stat))
);

TRACE_EVENT(svc_process,
	TP_PROTO(const struct svc_rqst *rqst, const char *name),

	TP_ARGS(rqst, name),

	TP_STRUCT__entry(
		__field(u32, xid)
		__field(u32, vers)
		__field(u32, proc)
		__string(service, name)
		__string(addr, rqst->rq_xprt ?
			 rqst->rq_xprt->xpt_remotebuf : "(null)")
	),

	TP_fast_assign(
		__entry->xid = be32_to_cpu(rqst->rq_xid);
		__entry->vers = rqst->rq_vers;
		__entry->proc = rqst->rq_proc;
		__assign_str(service, name);
		__assign_str(addr, rqst->rq_xprt ?
			     rqst->rq_xprt->xpt_remotebuf : "(null)");
	),

	TP_printk("addr=%s xid=0x%08x service=%s vers=%u proc=%u",
			__get_str(addr), __entry->xid,
			__get_str(service), __entry->vers, __entry->proc)
);

DECLARE_EVENT_CLASS(svc_rqst_event,

	TP_PROTO(
		const struct svc_rqst *rqst
	),

	TP_ARGS(rqst),

	TP_STRUCT__entry(
		__field(u32, xid)
		__field(unsigned long, flags)
		__string(addr, rqst->rq_xprt->xpt_remotebuf)
	),

	TP_fast_assign(
		__entry->xid = be32_to_cpu(rqst->rq_xid);
		__entry->flags = rqst->rq_flags;
		__assign_str(addr, rqst->rq_xprt->xpt_remotebuf);
	),

	TP_printk("addr=%s xid=0x%08x flags=%s",
			__get_str(addr), __entry->xid,
			show_rqstp_flags(__entry->flags))
);
#define DEFINE_SVC_RQST_EVENT(name) \
	DEFINE_EVENT(svc_rqst_event, svc_##name, \
			TP_PROTO( \
				const struct svc_rqst *rqst \
			), \
			TP_ARGS(rqst))

DEFINE_SVC_RQST_EVENT(defer);
DEFINE_SVC_RQST_EVENT(drop);

DECLARE_EVENT_CLASS(svc_rqst_status,

	TP_PROTO(struct svc_rqst *rqst, int status),

	TP_ARGS(rqst, status),

	TP_STRUCT__entry(
		__field(u32, xid)
		__field(int, status)
		__field(unsigned long, flags)
		__string(addr, rqst->rq_xprt->xpt_remotebuf)
	),

	TP_fast_assign(
		__entry->xid = be32_to_cpu(rqst->rq_xid);
		__entry->status = status;
		__entry->flags = rqst->rq_flags;
		__assign_str(addr, rqst->rq_xprt->xpt_remotebuf);
	),

	TP_printk("addr=%s xid=0x%08x status=%d flags=%s",
		  __get_str(addr), __entry->xid,
		  __entry->status, show_rqstp_flags(__entry->flags))
);

DEFINE_EVENT(svc_rqst_status, svc_send,
	TP_PROTO(struct svc_rqst *rqst, int status),
	TP_ARGS(rqst, status));

#define show_svc_xprt_flags(flags)					\
	__print_flags(flags, "|",					\
		{ (1UL << XPT_BUSY),		"XPT_BUSY"},		\
		{ (1UL << XPT_CONN),		"XPT_CONN"},		\
		{ (1UL << XPT_CLOSE),		"XPT_CLOSE"},		\
		{ (1UL << XPT_DATA),		"XPT_DATA"},		\
		{ (1UL << XPT_TEMP),		"XPT_TEMP"},		\
		{ (1UL << XPT_DEAD),		"XPT_DEAD"},		\
		{ (1UL << XPT_CHNGBUF),		"XPT_CHNGBUF"},		\
		{ (1UL << XPT_DEFERRED),	"XPT_DEFERRED"},	\
		{ (1UL << XPT_OLD),		"XPT_OLD"},		\
		{ (1UL << XPT_LISTENER),	"XPT_LISTENER"},	\
		{ (1UL << XPT_CACHE_AUTH),	"XPT_CACHE_AUTH"},	\
		{ (1UL << XPT_LOCAL),		"XPT_LOCAL"},		\
		{ (1UL << XPT_KILL_TEMP),	"XPT_KILL_TEMP"},	\
		{ (1UL << XPT_CONG_CTRL),	"XPT_CONG_CTRL"})

TRACE_EVENT(svc_xprt_do_enqueue,
	TP_PROTO(struct svc_xprt *xprt, struct svc_rqst *rqst),

	TP_ARGS(xprt, rqst),

	TP_STRUCT__entry(
		__field(struct svc_xprt *, xprt)
		__field(int, pid)
		__field(unsigned long, flags)
		__string(addr, xprt->xpt_remotebuf)
	),

	TP_fast_assign(
		__entry->xprt = xprt;
		__entry->pid = rqst? rqst->rq_task->pid : 0;
		__entry->flags = xprt->xpt_flags;
		__assign_str(addr, xprt->xpt_remotebuf);
	),

	TP_printk("xprt=%p addr=%s pid=%d flags=%s",
			__entry->xprt, __get_str(addr),
			__entry->pid, show_svc_xprt_flags(__entry->flags))
);

DECLARE_EVENT_CLASS(svc_xprt_event,
	TP_PROTO(struct svc_xprt *xprt),

	TP_ARGS(xprt),

	TP_STRUCT__entry(
		__field(struct svc_xprt *, xprt)
		__field(unsigned long, flags)
		__string(addr, xprt->xpt_remotebuf)
	),

	TP_fast_assign(
		__entry->xprt = xprt;
		__entry->flags = xprt->xpt_flags;
		__assign_str(addr, xprt->xpt_remotebuf);
	),

	TP_printk("xprt=%p addr=%s flags=%s",
			__entry->xprt, __get_str(addr),
			show_svc_xprt_flags(__entry->flags))
);

DEFINE_EVENT(svc_xprt_event, svc_xprt_no_write_space,
	TP_PROTO(struct svc_xprt *xprt),
	TP_ARGS(xprt));

TRACE_EVENT(svc_xprt_dequeue,
	TP_PROTO(struct svc_rqst *rqst),

	TP_ARGS(rqst),

	TP_STRUCT__entry(
		__field(struct svc_xprt *, xprt)
		__field(unsigned long, flags)
		__field(unsigned long, wakeup)
		__string(addr, rqst->rq_xprt->xpt_remotebuf)
	),

	TP_fast_assign(
		__entry->xprt = rqst->rq_xprt;
		__entry->flags = rqst->rq_xprt->xpt_flags;
		__entry->wakeup = ktime_to_us(ktime_sub(ktime_get(),
							rqst->rq_qtime));
		__assign_str(addr, rqst->rq_xprt->xpt_remotebuf);
	),

	TP_printk("xprt=%p addr=%s flags=%s wakeup-us=%lu",
			__entry->xprt, __get_str(addr),
			show_svc_xprt_flags(__entry->flags),
			__entry->wakeup)
);

TRACE_EVENT(svc_wake_up,
	TP_PROTO(int pid),

	TP_ARGS(pid),

	TP_STRUCT__entry(
		__field(int, pid)
	),

	TP_fast_assign(
		__entry->pid = pid;
	),

	TP_printk("pid=%d", __entry->pid)
);

TRACE_EVENT(svc_handle_xprt,
	TP_PROTO(struct svc_xprt *xprt, int len),

	TP_ARGS(xprt, len),

	TP_STRUCT__entry(
		__field(struct svc_xprt *, xprt)
		__field(int, len)
		__field(unsigned long, flags)
		__string(addr, xprt->xpt_remotebuf)
	),

	TP_fast_assign(
		__entry->xprt = xprt;
		__entry->len = len;
		__entry->flags = xprt->xpt_flags;
		__assign_str(addr, xprt->xpt_remotebuf);
	),

	TP_printk("xprt=%p addr=%s len=%d flags=%s",
		__entry->xprt, __get_str(addr),
		__entry->len, show_svc_xprt_flags(__entry->flags))
);

TRACE_EVENT(svc_stats_latency,
	TP_PROTO(const struct svc_rqst *rqst),

	TP_ARGS(rqst),

	TP_STRUCT__entry(
		__field(u32, xid)
		__field(unsigned long, execute)
		__string(addr, rqst->rq_xprt->xpt_remotebuf)
	),

	TP_fast_assign(
		__entry->xid = be32_to_cpu(rqst->rq_xid);
		__entry->execute = ktime_to_us(ktime_sub(ktime_get(),
							 rqst->rq_stime));
		__assign_str(addr, rqst->rq_xprt->xpt_remotebuf);
	),

	TP_printk("addr=%s xid=0x%08x execute-us=%lu",
		__get_str(addr), __entry->xid, __entry->execute)
);

DECLARE_EVENT_CLASS(svc_deferred_event,
	TP_PROTO(
		const struct svc_deferred_req *dr
	),

	TP_ARGS(dr),

	TP_STRUCT__entry(
		__field(u32, xid)
		__string(addr, dr->xprt->xpt_remotebuf)
	),

	TP_fast_assign(
		__entry->xid = be32_to_cpu(*(__be32 *)(dr->args +
						       (dr->xprt_hlen>>2)));
		__assign_str(addr, dr->xprt->xpt_remotebuf);
	),

	TP_printk("addr=%s xid=0x%08x", __get_str(addr), __entry->xid)
);
#define DEFINE_SVC_DEFERRED_EVENT(name) \
	DEFINE_EVENT(svc_deferred_event, svc_##name##_deferred, \
			TP_PROTO( \
				const struct svc_deferred_req *dr \
			), \
			TP_ARGS(dr))

DEFINE_SVC_DEFERRED_EVENT(drop);
DEFINE_SVC_DEFERRED_EVENT(revisit);

DECLARE_EVENT_CLASS(cache_event,
	TP_PROTO(
		const struct cache_detail *cd,
		const struct cache_head *h
	),

	TP_ARGS(cd, h),

	TP_STRUCT__entry(
		__field(const struct cache_head *, h)
		__string(name, cd->name)
	),

	TP_fast_assign(
		__entry->h = h;
		__assign_str(name, cd->name);
	),

	TP_printk("cache=%s entry=%p", __get_str(name), __entry->h)
);
#define DEFINE_CACHE_EVENT(name) \
	DEFINE_EVENT(cache_event, name, \
			TP_PROTO( \
				const struct cache_detail *cd, \
				const struct cache_head *h \
			), \
			TP_ARGS(cd, h))
DEFINE_CACHE_EVENT(cache_entry_expired);
DEFINE_CACHE_EVENT(cache_entry_upcall);
DEFINE_CACHE_EVENT(cache_entry_update);
DEFINE_CACHE_EVENT(cache_entry_make_negative);
DEFINE_CACHE_EVENT(cache_entry_no_listener);

#endif /* _TRACE_SUNRPC_H */

#include <trace/define_trace.h>
