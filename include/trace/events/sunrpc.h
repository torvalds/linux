#undef TRACE_SYSTEM
#define TRACE_SYSTEM sunrpc

#if !defined(_TRACE_SUNRPC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SUNRPC_H

#include <linux/sunrpc/sched.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/svc.h>
#include <linux/sunrpc/xprtsock.h>
#include <net/tcp_states.h>
#include <linux/net.h>
#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(rpc_task_status,

	TP_PROTO(struct rpc_task *task),

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

	TP_printk("task:%u@%u, status %d",
		__entry->task_id, __entry->client_id,
		__entry->status)
);

DEFINE_EVENT(rpc_task_status, rpc_call_status,
	TP_PROTO(struct rpc_task *task),

	TP_ARGS(task)
);

DEFINE_EVENT(rpc_task_status, rpc_bind_status,
	TP_PROTO(struct rpc_task *task),

	TP_ARGS(task)
);

TRACE_EVENT(rpc_connect_status,
	TP_PROTO(struct rpc_task *task, int status),

	TP_ARGS(task, status),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(int, status)
	),

	TP_fast_assign(
		__entry->task_id = task->tk_pid;
		__entry->client_id = task->tk_client->cl_clid;
		__entry->status = status;
	),

	TP_printk("task:%u@%u, status %d",
		__entry->task_id, __entry->client_id,
		__entry->status)
);

DECLARE_EVENT_CLASS(rpc_task_running,

	TP_PROTO(const struct rpc_clnt *clnt, const struct rpc_task *task, const void *action),

	TP_ARGS(clnt, task, action),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(const void *, action)
		__field(unsigned long, runstate)
		__field(int, status)
		__field(unsigned short, flags)
		),

	TP_fast_assign(
		__entry->client_id = clnt ? clnt->cl_clid : -1;
		__entry->task_id = task->tk_pid;
		__entry->action = action;
		__entry->runstate = task->tk_runstate;
		__entry->status = task->tk_status;
		__entry->flags = task->tk_flags;
		),

	TP_printk("task:%u@%d flags=%4.4x state=%4.4lx status=%d action=%pf",
		__entry->task_id, __entry->client_id,
		__entry->flags,
		__entry->runstate,
		__entry->status,
		__entry->action
		)
);

DEFINE_EVENT(rpc_task_running, rpc_task_begin,

	TP_PROTO(const struct rpc_clnt *clnt, const struct rpc_task *task, const void *action),

	TP_ARGS(clnt, task, action)

);

DEFINE_EVENT(rpc_task_running, rpc_task_run_action,

	TP_PROTO(const struct rpc_clnt *clnt, const struct rpc_task *task, const void *action),

	TP_ARGS(clnt, task, action)

);

DEFINE_EVENT(rpc_task_running, rpc_task_complete,

	TP_PROTO(const struct rpc_clnt *clnt, const struct rpc_task *task, const void *action),

	TP_ARGS(clnt, task, action)

);

DECLARE_EVENT_CLASS(rpc_task_queued,

	TP_PROTO(const struct rpc_clnt *clnt, const struct rpc_task *task, const struct rpc_wait_queue *q),

	TP_ARGS(clnt, task, q),

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
		__entry->client_id = clnt->cl_clid;
		__entry->task_id = task->tk_pid;
		__entry->timeout = task->tk_timeout;
		__entry->runstate = task->tk_runstate;
		__entry->status = task->tk_status;
		__entry->flags = task->tk_flags;
		__assign_str(q_name, rpc_qname(q));
		),

	TP_printk("task:%u@%u flags=%4.4x state=%4.4lx status=%d timeout=%lu queue=%s",
		__entry->task_id, __entry->client_id,
		__entry->flags,
		__entry->runstate,
		__entry->status,
		__entry->timeout,
		__get_str(q_name)
		)
);

DEFINE_EVENT(rpc_task_queued, rpc_task_sleep,

	TP_PROTO(const struct rpc_clnt *clnt, const struct rpc_task *task, const struct rpc_wait_queue *q),

	TP_ARGS(clnt, task, q)

);

DEFINE_EVENT(rpc_task_queued, rpc_task_wakeup,

	TP_PROTO(const struct rpc_clnt *clnt, const struct rpc_task *task, const struct rpc_wait_queue *q),

	TP_ARGS(clnt, task, q)

);

#define rpc_show_socket_state(state) \
	__print_symbolic(state, \
		{ SS_FREE, "FREE" }, \
		{ SS_UNCONNECTED, "UNCONNECTED" }, \
		{ SS_CONNECTING, "CONNECTING," }, \
		{ SS_CONNECTED, "CONNECTED," }, \
		{ SS_DISCONNECTING, "DISCONNECTING" })

#define rpc_show_sock_state(state) \
	__print_symbolic(state, \
		{ TCP_ESTABLISHED, "ESTABLISHED" }, \
		{ TCP_SYN_SENT, "SYN_SENT" }, \
		{ TCP_SYN_RECV, "SYN_RECV" }, \
		{ TCP_FIN_WAIT1, "FIN_WAIT1" }, \
		{ TCP_FIN_WAIT2, "FIN_WAIT2" }, \
		{ TCP_TIME_WAIT, "TIME_WAIT" }, \
		{ TCP_CLOSE, "CLOSE" }, \
		{ TCP_CLOSE_WAIT, "CLOSE_WAIT" }, \
		{ TCP_LAST_ACK, "LAST_ACK" }, \
		{ TCP_LISTEN, "LISTEN" }, \
		{ TCP_CLOSING, "CLOSING" })

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
	TP_PROTO(struct rpc_xprt *xprt, __be32 xid, int status),

	TP_ARGS(xprt, xid, status),

	TP_STRUCT__entry(
		__field(__be32, xid)
		__field(int, status)
		__string(addr, xprt->address_strings[RPC_DISPLAY_ADDR])
		__string(port, xprt->address_strings[RPC_DISPLAY_PORT])
	),

	TP_fast_assign(
		__entry->xid = xid;
		__entry->status = status;
		__assign_str(addr, xprt->address_strings[RPC_DISPLAY_ADDR]);
		__assign_str(port, xprt->address_strings[RPC_DISPLAY_PORT]);
	),

	TP_printk("peer=[%s]:%s xid=0x%x status=%d", __get_str(addr),
			__get_str(port), be32_to_cpu(__entry->xid),
			__entry->status)
);

DEFINE_EVENT(rpc_xprt_event, xprt_lookup_rqst,
	TP_PROTO(struct rpc_xprt *xprt, __be32 xid, int status),
	TP_ARGS(xprt, xid, status));

DEFINE_EVENT(rpc_xprt_event, xprt_transmit,
	TP_PROTO(struct rpc_xprt *xprt, __be32 xid, int status),
	TP_ARGS(xprt, xid, status));

DEFINE_EVENT(rpc_xprt_event, xprt_complete_rqst,
	TP_PROTO(struct rpc_xprt *xprt, __be32 xid, int status),
	TP_ARGS(xprt, xid, status));

TRACE_EVENT(xs_tcp_data_ready,
	TP_PROTO(struct rpc_xprt *xprt, int err, unsigned int total),

	TP_ARGS(xprt, err, total),

	TP_STRUCT__entry(
		__field(int, err)
		__field(unsigned int, total)
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

	TP_printk("peer=[%s]:%s err=%d total=%u", __get_str(addr),
			__get_str(port), __entry->err, __entry->total)
);

#define rpc_show_sock_xprt_flags(flags) \
	__print_flags(flags, "|", \
		{ TCP_RCV_LAST_FRAG, "TCP_RCV_LAST_FRAG" }, \
		{ TCP_RCV_COPY_FRAGHDR, "TCP_RCV_COPY_FRAGHDR" }, \
		{ TCP_RCV_COPY_XID, "TCP_RCV_COPY_XID" }, \
		{ TCP_RCV_COPY_DATA, "TCP_RCV_COPY_DATA" }, \
		{ TCP_RCV_READ_CALLDIR, "TCP_RCV_READ_CALLDIR" }, \
		{ TCP_RCV_COPY_CALLDIR, "TCP_RCV_COPY_CALLDIR" }, \
		{ TCP_RPC_REPLY, "TCP_RPC_REPLY" })

TRACE_EVENT(xs_tcp_data_recv,
	TP_PROTO(struct sock_xprt *xs),

	TP_ARGS(xs),

	TP_STRUCT__entry(
		__string(addr, xs->xprt.address_strings[RPC_DISPLAY_ADDR])
		__string(port, xs->xprt.address_strings[RPC_DISPLAY_PORT])
		__field(__be32, xid)
		__field(unsigned long, flags)
		__field(unsigned long, copied)
		__field(unsigned int, reclen)
		__field(unsigned long, offset)
	),

	TP_fast_assign(
		__assign_str(addr, xs->xprt.address_strings[RPC_DISPLAY_ADDR]);
		__assign_str(port, xs->xprt.address_strings[RPC_DISPLAY_PORT]);
		__entry->xid = xs->tcp_xid;
		__entry->flags = xs->tcp_flags;
		__entry->copied = xs->tcp_copied;
		__entry->reclen = xs->tcp_reclen;
		__entry->offset = xs->tcp_offset;
	),

	TP_printk("peer=[%s]:%s xid=0x%x flags=%s copied=%lu reclen=%u offset=%lu",
			__get_str(addr), __get_str(port), be32_to_cpu(__entry->xid),
			rpc_show_sock_xprt_flags(__entry->flags),
			__entry->copied, __entry->reclen, __entry->offset)
);

#define show_rqstp_flags(flags)						\
	__print_flags(flags, "|",					\
		{ (1UL << RQ_SECURE),		"RQ_SECURE"},		\
		{ (1UL << RQ_LOCAL),		"RQ_LOCAL"},		\
		{ (1UL << RQ_USEDEFERRAL),	"RQ_USEDEFERRAL"},	\
		{ (1UL << RQ_DROPME),		"RQ_DROPME"},		\
		{ (1UL << RQ_SPLICE_OK),	"RQ_SPLICE_OK"},	\
		{ (1UL << RQ_VICTIM),		"RQ_VICTIM"})

TRACE_EVENT(svc_recv,
	TP_PROTO(struct svc_rqst *rqst, int status),

	TP_ARGS(rqst, status),

	TP_STRUCT__entry(
		__field(struct sockaddr *, addr)
		__field(__be32, xid)
		__field(int, status)
		__field(unsigned long, flags)
	),

	TP_fast_assign(
		__entry->addr = (struct sockaddr *)&rqst->rq_addr;
		__entry->xid = status > 0 ? rqst->rq_xid : 0;
		__entry->status = status;
		__entry->flags = rqst->rq_flags;
	),

	TP_printk("addr=%pIScp xid=0x%x status=%d flags=%s", __entry->addr,
			be32_to_cpu(__entry->xid), __entry->status,
			show_rqstp_flags(__entry->flags))
);

DECLARE_EVENT_CLASS(svc_rqst_status,

	TP_PROTO(struct svc_rqst *rqst, int status),

	TP_ARGS(rqst, status),

	TP_STRUCT__entry(
		__field(struct sockaddr *, addr)
		__field(__be32, xid)
		__field(int, dropme)
		__field(int, status)
		__field(unsigned long, flags)
	),

	TP_fast_assign(
		__entry->addr = (struct sockaddr *)&rqst->rq_addr;
		__entry->xid = rqst->rq_xid;
		__entry->status = status;
		__entry->flags = rqst->rq_flags;
	),

	TP_printk("addr=%pIScp rq_xid=0x%x status=%d flags=%s",
		__entry->addr, be32_to_cpu(__entry->xid),
		__entry->status, show_rqstp_flags(__entry->flags))
);

DEFINE_EVENT(svc_rqst_status, svc_process,
	TP_PROTO(struct svc_rqst *rqst, int status),
	TP_ARGS(rqst, status));

DEFINE_EVENT(svc_rqst_status, svc_send,
	TP_PROTO(struct svc_rqst *rqst, int status),
	TP_ARGS(rqst, status));

#endif /* _TRACE_SUNRPC_H */

#include <trace/define_trace.h>
