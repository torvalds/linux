#undef TRACE_SYSTEM
#define TRACE_SYSTEM sunrpc

#if !defined(_TRACE_SUNRPC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SUNRPC_H

#include <linux/sunrpc/sched.h>
#include <linux/sunrpc/clnt.h>
#include <net/tcp_states.h>
#include <linux/net.h>
#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(rpc_task_status,

	TP_PROTO(struct rpc_task *task),

	TP_ARGS(task),

	TP_STRUCT__entry(
		__field(const struct rpc_task *, task)
		__field(const struct rpc_clnt *, clnt)
		__field(int, status)
	),

	TP_fast_assign(
		__entry->task = task;
		__entry->clnt = task->tk_client;
		__entry->status = task->tk_status;
	),

	TP_printk("task:%p@%p, status %d",__entry->task, __entry->clnt, __entry->status)
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
		__field(const struct rpc_task *, task)
		__field(const struct rpc_clnt *, clnt)
		__field(int, status)
	),

	TP_fast_assign(
		__entry->task = task;
		__entry->clnt = task->tk_client;
		__entry->status = status;
	),

	TP_printk("task:%p@%p, status %d",__entry->task, __entry->clnt, __entry->status)
);

DECLARE_EVENT_CLASS(rpc_task_running,

	TP_PROTO(const struct rpc_clnt *clnt, const struct rpc_task *task, const void *action),

	TP_ARGS(clnt, task, action),

	TP_STRUCT__entry(
		__field(const struct rpc_clnt *, clnt)
		__field(const struct rpc_task *, task)
		__field(const void *, action)
		__field(unsigned long, runstate)
		__field(int, status)
		__field(unsigned short, flags)
		),

	TP_fast_assign(
		__entry->clnt = clnt;
		__entry->task = task;
		__entry->action = action;
		__entry->runstate = task->tk_runstate;
		__entry->status = task->tk_status;
		__entry->flags = task->tk_flags;
		),

	TP_printk("task:%p@%p flags=%4.4x state=%4.4lx status=%d action=%pf",
		__entry->task,
		__entry->clnt,
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
		__field(const struct rpc_clnt *, clnt)
		__field(const struct rpc_task *, task)
		__field(unsigned long, timeout)
		__field(unsigned long, runstate)
		__field(int, status)
		__field(unsigned short, flags)
		__string(q_name, rpc_qname(q))
		),

	TP_fast_assign(
		__entry->clnt = clnt;
		__entry->task = task;
		__entry->timeout = task->tk_timeout;
		__entry->runstate = task->tk_runstate;
		__entry->status = task->tk_status;
		__entry->flags = task->tk_flags;
		__assign_str(q_name, rpc_qname(q));
		),

	TP_printk("task:%p@%p flags=%4.4x state=%4.4lx status=%d timeout=%lu queue=%s",
		__entry->task,
		__entry->clnt,
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
DEFINE_RPC_SOCKET_EVENT_DONE(rpc_socket_reset_connection);
DEFINE_RPC_SOCKET_EVENT(rpc_socket_close);
DEFINE_RPC_SOCKET_EVENT(rpc_socket_shutdown);

#endif /* _TRACE_SUNRPC_H */

#include <trace/define_trace.h>
