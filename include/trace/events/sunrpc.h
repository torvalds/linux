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
		__field(u32, xid)
		__field(unsigned long, flags)
		__field(unsigned long, copied)
		__field(unsigned int, reclen)
		__field(unsigned long, offset)
	),

	TP_fast_assign(
		__assign_str(addr, xs->xprt.address_strings[RPC_DISPLAY_ADDR]);
		__assign_str(port, xs->xprt.address_strings[RPC_DISPLAY_PORT]);
		__entry->xid = be32_to_cpu(xs->tcp_xid);
		__entry->flags = xs->tcp_flags;
		__entry->copied = xs->tcp_copied;
		__entry->reclen = xs->tcp_reclen;
		__entry->offset = xs->tcp_offset;
	),

	TP_printk("peer=[%s]:%s xid=0x%08x flags=%s copied=%lu reclen=%u offset=%lu",
			__get_str(addr), __get_str(port), __entry->xid,
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
		{ (1UL << RQ_VICTIM),		"RQ_VICTIM"},		\
		{ (1UL << RQ_BUSY),		"RQ_BUSY"})

TRACE_EVENT(svc_recv,
	TP_PROTO(struct svc_rqst *rqst, int status),

	TP_ARGS(rqst, status),

	TP_STRUCT__entry(
		__field(struct sockaddr *, addr)
		__field(u32, xid)
		__field(int, status)
		__field(unsigned long, flags)
	),

	TP_fast_assign(
		__entry->addr = (struct sockaddr *)&rqst->rq_addr;
		__entry->xid = status > 0 ? be32_to_cpu(rqst->rq_xid) : 0;
		__entry->status = status;
		__entry->flags = rqst->rq_flags;
	),

	TP_printk("addr=%pIScp xid=0x%08x status=%d flags=%s", __entry->addr,
			__entry->xid, __entry->status,
			show_rqstp_flags(__entry->flags))
);

DECLARE_EVENT_CLASS(svc_rqst_event,

	TP_PROTO(struct svc_rqst *rqst),

	TP_ARGS(rqst),

	TP_STRUCT__entry(
		__field(u32, xid)
		__field(unsigned long, flags)
		__dynamic_array(unsigned char, addr, rqst->rq_addrlen)
	),

	TP_fast_assign(
		__entry->xid = be32_to_cpu(rqst->rq_xid);
		__entry->flags = rqst->rq_flags;
		memcpy(__get_dynamic_array(addr),
			&rqst->rq_addr, rqst->rq_addrlen);
	),

	TP_printk("addr=%pIScp rq_xid=0x%08x flags=%s",
		(struct sockaddr *)__get_dynamic_array(addr),
		__entry->xid,
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
		__field(struct sockaddr *, addr)
		__field(u32, xid)
		__field(int, dropme)
		__field(int, status)
		__field(unsigned long, flags)
	),

	TP_fast_assign(
		__entry->addr = (struct sockaddr *)&rqst->rq_addr;
		__entry->xid = be32_to_cpu(rqst->rq_xid);
		__entry->status = status;
		__entry->flags = rqst->rq_flags;
	),

	TP_printk("addr=%pIScp rq_xid=0x%08x status=%d flags=%s",
		__entry->addr, __entry->xid,
		__entry->status, show_rqstp_flags(__entry->flags))
);

DEFINE_EVENT(svc_rqst_status, svc_process,
	TP_PROTO(struct svc_rqst *rqst, int status),
	TP_ARGS(rqst, status));

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
		{ (1UL << XPT_LOCAL),		"XPT_LOCAL"})

TRACE_EVENT(svc_xprt_do_enqueue,
	TP_PROTO(struct svc_xprt *xprt, struct svc_rqst *rqst),

	TP_ARGS(xprt, rqst),

	TP_STRUCT__entry(
		__field(struct svc_xprt *, xprt)
		__field(int, pid)
		__field(unsigned long, flags)
		__dynamic_array(unsigned char, addr, xprt != NULL ?
			xprt->xpt_remotelen : 0)
	),

	TP_fast_assign(
		__entry->xprt = xprt;
		__entry->pid = rqst? rqst->rq_task->pid : 0;
		if (xprt) {
			memcpy(__get_dynamic_array(addr),
				&xprt->xpt_remote,
				xprt->xpt_remotelen);
			__entry->flags = xprt->xpt_flags;
		} else
			__entry->flags = 0;
	),

	TP_printk("xprt=0x%p addr=%pIScp pid=%d flags=%s", __entry->xprt,
		__get_dynamic_array_len(addr) != 0 ?
			(struct sockaddr *)__get_dynamic_array(addr) : NULL,
		__entry->pid, show_svc_xprt_flags(__entry->flags))
);

DECLARE_EVENT_CLASS(svc_xprt_event,
	TP_PROTO(struct svc_xprt *xprt),

	TP_ARGS(xprt),

	TP_STRUCT__entry(
		__field(struct svc_xprt *, xprt)
		__field(unsigned long, flags)
		__dynamic_array(unsigned char, addr, xprt != NULL ?
			xprt->xpt_remotelen : 0)
	),

	TP_fast_assign(
		__entry->xprt = xprt;
		if (xprt) {
			memcpy(__get_dynamic_array(addr),
					&xprt->xpt_remote,
					xprt->xpt_remotelen);
			__entry->flags = xprt->xpt_flags;
		} else
			__entry->flags = 0;
	),

	TP_printk("xprt=0x%p addr=%pIScp flags=%s", __entry->xprt,
		__get_dynamic_array_len(addr) != 0 ?
			(struct sockaddr *)__get_dynamic_array(addr) : NULL,
		show_svc_xprt_flags(__entry->flags))
);

DEFINE_EVENT(svc_xprt_event, svc_xprt_dequeue,
	TP_PROTO(struct svc_xprt *xprt),
	TP_ARGS(xprt));

DEFINE_EVENT(svc_xprt_event, svc_xprt_no_write_space,
	TP_PROTO(struct svc_xprt *xprt),
	TP_ARGS(xprt));

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
		__dynamic_array(unsigned char, addr, xprt != NULL ?
			xprt->xpt_remotelen : 0)
	),

	TP_fast_assign(
		__entry->xprt = xprt;
		__entry->len = len;
		if (xprt) {
			memcpy(__get_dynamic_array(addr),
					&xprt->xpt_remote,
					xprt->xpt_remotelen);
			__entry->flags = xprt->xpt_flags;
		} else
			__entry->flags = 0;
	),

	TP_printk("xprt=0x%p addr=%pIScp len=%d flags=%s", __entry->xprt,
		__get_dynamic_array_len(addr) != 0 ?
			(struct sockaddr *)__get_dynamic_array(addr) : NULL,
		__entry->len, show_svc_xprt_flags(__entry->flags))
);


DECLARE_EVENT_CLASS(svc_deferred_event,
	TP_PROTO(struct svc_deferred_req *dr),

	TP_ARGS(dr),

	TP_STRUCT__entry(
		__field(u32, xid)
		__dynamic_array(unsigned char, addr, dr->addrlen)
	),

	TP_fast_assign(
		__entry->xid = be32_to_cpu(*(__be32 *)(dr->args +
						       (dr->xprt_hlen>>2)));
		memcpy(__get_dynamic_array(addr), &dr->addr, dr->addrlen);
	),

	TP_printk("addr=%pIScp xid=0x%08x",
		(struct sockaddr *)__get_dynamic_array(addr),
		__entry->xid)
);

DEFINE_EVENT(svc_deferred_event, svc_drop_deferred,
	TP_PROTO(struct svc_deferred_req *dr),
	TP_ARGS(dr));
DEFINE_EVENT(svc_deferred_event, svc_revisit_deferred,
	TP_PROTO(struct svc_deferred_req *dr),
	TP_ARGS(dr));
#endif /* _TRACE_SUNRPC_H */

#include <trace/define_trace.h>
