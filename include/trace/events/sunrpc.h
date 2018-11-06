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

	TP_printk("task:%u@%u status=%d",
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

	TP_printk("task:%u@%d flags=%4.4x state=%4.4lx status=%d action=%pf",
		__entry->task_id, __entry->client_id,
		__entry->flags,
		__entry->runstate,
		__entry->status,
		__entry->action
		)
);

DEFINE_EVENT(rpc_task_running, rpc_task_begin,

	TP_PROTO(const struct rpc_task *task, const void *action),

	TP_ARGS(task, action)

);

DEFINE_EVENT(rpc_task_running, rpc_task_run_action,

	TP_PROTO(const struct rpc_task *task, const void *action),

	TP_ARGS(task, action)

);

DEFINE_EVENT(rpc_task_running, rpc_task_complete,

	TP_PROTO(const struct rpc_task *task, const void *action),

	TP_ARGS(task, action)

);

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
		__entry->timeout = task->tk_timeout;
		__entry->runstate = task->tk_runstate;
		__entry->status = task->tk_status;
		__entry->flags = task->tk_flags;
		__assign_str(q_name, rpc_qname(q));
		),

	TP_printk("task:%u@%d flags=%4.4x state=%4.4lx status=%d timeout=%lu queue=%s",
		__entry->task_id, __entry->client_id,
		__entry->flags,
		__entry->runstate,
		__entry->status,
		__entry->timeout,
		__get_str(q_name)
		)
);

DEFINE_EVENT(rpc_task_queued, rpc_task_sleep,

	TP_PROTO(const struct rpc_task *task, const struct rpc_wait_queue *q),

	TP_ARGS(task, q)

);

DEFINE_EVENT(rpc_task_queued, rpc_task_wakeup,

	TP_PROTO(const struct rpc_task *task, const struct rpc_wait_queue *q),

	TP_ARGS(task, q)

);

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
	TP_PROTO(struct rpc_xprt *xprt, __be32 xid, int status),

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

DEFINE_EVENT(rpc_xprt_event, xprt_timer,
	TP_PROTO(struct rpc_xprt *xprt, __be32 xid, int status),
	TP_ARGS(xprt, xid, status));

DEFINE_EVENT(rpc_xprt_event, xprt_lookup_rqst,
	TP_PROTO(struct rpc_xprt *xprt, __be32 xid, int status),
	TP_ARGS(xprt, xid, status));

DEFINE_EVENT(rpc_xprt_event, xprt_transmit,
	TP_PROTO(struct rpc_xprt *xprt, __be32 xid, int status),
	TP_ARGS(xprt, xid, status));

DEFINE_EVENT(rpc_xprt_event, xprt_complete_rqst,
	TP_PROTO(struct rpc_xprt *xprt, __be32 xid, int status),
	TP_ARGS(xprt, xid, status));

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

TRACE_EVENT(svc_process,
	TP_PROTO(const struct svc_rqst *rqst, const char *name),

	TP_ARGS(rqst, name),

	TP_STRUCT__entry(
		__field(u32, xid)
		__field(u32, vers)
		__field(u32, proc)
		__string(service, name)
		__string(addr, rqst->rq_xprt->xpt_remotebuf)
	),

	TP_fast_assign(
		__entry->xid = be32_to_cpu(rqst->rq_xid);
		__entry->vers = rqst->rq_vers;
		__entry->proc = rqst->rq_proc;
		__assign_str(service, name);
		__assign_str(addr, rqst->rq_xprt->xpt_remotebuf);
	),

	TP_printk("addr=%s xid=0x%08x service=%s vers=%u proc=%u",
			__get_str(addr), __entry->xid,
			__get_str(service), __entry->vers, __entry->proc)
);

DECLARE_EVENT_CLASS(svc_rqst_event,

	TP_PROTO(struct svc_rqst *rqst),

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

DEFINE_EVENT(svc_rqst_event, svc_defer,
	TP_PROTO(struct svc_rqst *rqst),
	TP_ARGS(rqst));

DEFINE_EVENT(svc_rqst_event, svc_drop,
	TP_PROTO(struct svc_rqst *rqst),
	TP_ARGS(rqst));

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
	TP_PROTO(struct svc_deferred_req *dr),

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

DEFINE_EVENT(svc_deferred_event, svc_drop_deferred,
	TP_PROTO(struct svc_deferred_req *dr),
	TP_ARGS(dr));
DEFINE_EVENT(svc_deferred_event, svc_revisit_deferred,
	TP_PROTO(struct svc_deferred_req *dr),
	TP_ARGS(dr));
#endif /* _TRACE_SUNRPC_H */

#include <trace/define_trace.h>
