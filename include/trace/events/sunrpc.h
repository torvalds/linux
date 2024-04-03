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

#include <trace/misc/sunrpc.h>

TRACE_DEFINE_ENUM(SOCK_STREAM);
TRACE_DEFINE_ENUM(SOCK_DGRAM);
TRACE_DEFINE_ENUM(SOCK_RAW);
TRACE_DEFINE_ENUM(SOCK_RDM);
TRACE_DEFINE_ENUM(SOCK_SEQPACKET);
TRACE_DEFINE_ENUM(SOCK_DCCP);
TRACE_DEFINE_ENUM(SOCK_PACKET);

#define show_socket_type(type)					\
	__print_symbolic(type,					\
		{ SOCK_STREAM,		"STREAM" },		\
		{ SOCK_DGRAM,		"DGRAM" },		\
		{ SOCK_RAW,		"RAW" },		\
		{ SOCK_RDM,		"RDM" },		\
		{ SOCK_SEQPACKET,	"SEQPACKET" },		\
		{ SOCK_DCCP,		"DCCP" },		\
		{ SOCK_PACKET,		"PACKET" })

/* This list is known to be incomplete, add new enums as needed. */
TRACE_DEFINE_ENUM(AF_UNSPEC);
TRACE_DEFINE_ENUM(AF_UNIX);
TRACE_DEFINE_ENUM(AF_LOCAL);
TRACE_DEFINE_ENUM(AF_INET);
TRACE_DEFINE_ENUM(AF_INET6);

#define rpc_show_address_family(family)				\
	__print_symbolic(family,				\
		{ AF_UNSPEC,		"AF_UNSPEC" },		\
		{ AF_UNIX,		"AF_UNIX" },		\
		{ AF_LOCAL,		"AF_LOCAL" },		\
		{ AF_INET,		"AF_INET" },		\
		{ AF_INET6,		"AF_INET6" })

DECLARE_EVENT_CLASS(rpc_xdr_buf_class,
	TP_PROTO(
		const struct rpc_task *task,
		const struct xdr_buf *xdr
	),

	TP_ARGS(task, xdr),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(const void *, head_base)
		__field(size_t, head_len)
		__field(const void *, tail_base)
		__field(size_t, tail_len)
		__field(unsigned int, page_base)
		__field(unsigned int, page_len)
		__field(unsigned int, msg_len)
	),

	TP_fast_assign(
		__entry->task_id = task->tk_pid;
		__entry->client_id = task->tk_client ?
				     task->tk_client->cl_clid : -1;
		__entry->head_base = xdr->head[0].iov_base;
		__entry->head_len = xdr->head[0].iov_len;
		__entry->tail_base = xdr->tail[0].iov_base;
		__entry->tail_len = xdr->tail[0].iov_len;
		__entry->page_base = xdr->page_base;
		__entry->page_len = xdr->page_len;
		__entry->msg_len = xdr->len;
	),

	TP_printk(SUNRPC_TRACE_TASK_SPECIFIER
		  " head=[%p,%zu] page=%u(%u) tail=[%p,%zu] len=%u",
		__entry->task_id, __entry->client_id,
		__entry->head_base, __entry->head_len,
		__entry->page_len, __entry->page_base,
		__entry->tail_base, __entry->tail_len,
		__entry->msg_len
	)
);

#define DEFINE_RPCXDRBUF_EVENT(name)					\
		DEFINE_EVENT(rpc_xdr_buf_class,				\
				rpc_xdr_##name,				\
				TP_PROTO(				\
					const struct rpc_task *task,	\
					const struct xdr_buf *xdr	\
				),					\
				TP_ARGS(task, xdr))

DEFINE_RPCXDRBUF_EVENT(sendto);
DEFINE_RPCXDRBUF_EVENT(recvfrom);
DEFINE_RPCXDRBUF_EVENT(reply_pages);


DECLARE_EVENT_CLASS(rpc_clnt_class,
	TP_PROTO(
		const struct rpc_clnt *clnt
	),

	TP_ARGS(clnt),

	TP_STRUCT__entry(
		__field(unsigned int, client_id)
	),

	TP_fast_assign(
		__entry->client_id = clnt->cl_clid;
	),

	TP_printk("client=" SUNRPC_TRACE_CLID_SPECIFIER, __entry->client_id)
);

#define DEFINE_RPC_CLNT_EVENT(name)					\
		DEFINE_EVENT(rpc_clnt_class,				\
				rpc_clnt_##name,			\
				TP_PROTO(				\
					const struct rpc_clnt *clnt	\
				),					\
				TP_ARGS(clnt))

DEFINE_RPC_CLNT_EVENT(free);
DEFINE_RPC_CLNT_EVENT(killall);
DEFINE_RPC_CLNT_EVENT(shutdown);
DEFINE_RPC_CLNT_EVENT(release);
DEFINE_RPC_CLNT_EVENT(replace_xprt);
DEFINE_RPC_CLNT_EVENT(replace_xprt_err);

TRACE_DEFINE_ENUM(RPC_XPRTSEC_NONE);
TRACE_DEFINE_ENUM(RPC_XPRTSEC_TLS_X509);

#define rpc_show_xprtsec_policy(policy)					\
	__print_symbolic(policy,					\
		{ RPC_XPRTSEC_NONE,		"none" },		\
		{ RPC_XPRTSEC_TLS_ANON,		"tls-anon" },		\
		{ RPC_XPRTSEC_TLS_X509,		"tls-x509" })

#define rpc_show_create_flags(flags)					\
	__print_flags(flags, "|",					\
		{ RPC_CLNT_CREATE_HARDRTRY,	"HARDRTRY" },		\
		{ RPC_CLNT_CREATE_AUTOBIND,	"AUTOBIND" },		\
		{ RPC_CLNT_CREATE_NONPRIVPORT,	"NONPRIVPORT" },	\
		{ RPC_CLNT_CREATE_NOPING,	"NOPING" },		\
		{ RPC_CLNT_CREATE_DISCRTRY,	"DISCRTRY" },		\
		{ RPC_CLNT_CREATE_QUIET,	"QUIET" },		\
		{ RPC_CLNT_CREATE_INFINITE_SLOTS,			\
						"INFINITE_SLOTS" },	\
		{ RPC_CLNT_CREATE_NO_IDLE_TIMEOUT,			\
						"NO_IDLE_TIMEOUT" },	\
		{ RPC_CLNT_CREATE_NO_RETRANS_TIMEOUT,			\
						"NO_RETRANS_TIMEOUT" },	\
		{ RPC_CLNT_CREATE_SOFTERR,	"SOFTERR" },		\
		{ RPC_CLNT_CREATE_REUSEPORT,	"REUSEPORT" })

TRACE_EVENT(rpc_clnt_new,
	TP_PROTO(
		const struct rpc_clnt *clnt,
		const struct rpc_xprt *xprt,
		const struct rpc_create_args *args
	),

	TP_ARGS(clnt, xprt, args),

	TP_STRUCT__entry(
		__field(unsigned int, client_id)
		__field(unsigned long, xprtsec)
		__field(unsigned long, flags)
		__string(program, clnt->cl_program->name)
		__string(server, xprt->servername)
		__string(addr, xprt->address_strings[RPC_DISPLAY_ADDR])
		__string(port, xprt->address_strings[RPC_DISPLAY_PORT])
	),

	TP_fast_assign(
		__entry->client_id = clnt->cl_clid;
		__entry->xprtsec = args->xprtsec.policy;
		__entry->flags = args->flags;
		__assign_str(program, clnt->cl_program->name);
		__assign_str(server, xprt->servername);
		__assign_str(addr, xprt->address_strings[RPC_DISPLAY_ADDR]);
		__assign_str(port, xprt->address_strings[RPC_DISPLAY_PORT]);
	),

	TP_printk("client=" SUNRPC_TRACE_CLID_SPECIFIER " peer=[%s]:%s"
		" program=%s server=%s xprtsec=%s flags=%s",
		__entry->client_id, __get_str(addr), __get_str(port),
		__get_str(program), __get_str(server),
		rpc_show_xprtsec_policy(__entry->xprtsec),
		rpc_show_create_flags(__entry->flags)
	)
);

TRACE_EVENT(rpc_clnt_new_err,
	TP_PROTO(
		const char *program,
		const char *server,
		int error
	),

	TP_ARGS(program, server, error),

	TP_STRUCT__entry(
		__field(int, error)
		__string(program, program)
		__string(server, server)
	),

	TP_fast_assign(
		__entry->error = error;
		__assign_str(program, program);
		__assign_str(server, server);
	),

	TP_printk("program=%s server=%s error=%d",
		__get_str(program), __get_str(server), __entry->error)
);

TRACE_EVENT(rpc_clnt_clone_err,
	TP_PROTO(
		const struct rpc_clnt *clnt,
		int error
	),

	TP_ARGS(clnt, error),

	TP_STRUCT__entry(
		__field(unsigned int, client_id)
		__field(int, error)
	),

	TP_fast_assign(
		__entry->client_id = clnt->cl_clid;
		__entry->error = error;
	),

	TP_printk("client=" SUNRPC_TRACE_CLID_SPECIFIER " error=%d",
		__entry->client_id, __entry->error)
);


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

	TP_printk(SUNRPC_TRACE_TASK_SPECIFIER " status=%d",
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
DEFINE_RPC_STATUS_EVENT(connect);
DEFINE_RPC_STATUS_EVENT(timeout);
DEFINE_RPC_STATUS_EVENT(retry_refresh);
DEFINE_RPC_STATUS_EVENT(refresh);

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
		__assign_str(progname, task->tk_client->cl_program->name);
		__assign_str(procname, rpc_proc_name(task));
	),

	TP_printk(SUNRPC_TRACE_TASK_SPECIFIER " %sv%d %s (%ssync)",
		__entry->task_id, __entry->client_id,
		__get_str(progname), __entry->version,
		__get_str(procname), __entry->async ? "a": ""
		)
);

#define rpc_show_task_flags(flags)					\
	__print_flags(flags, "|",					\
		{ RPC_TASK_ASYNC, "ASYNC" },				\
		{ RPC_TASK_SWAPPER, "SWAPPER" },			\
		{ RPC_TASK_MOVEABLE, "MOVEABLE" },			\
		{ RPC_TASK_NULLCREDS, "NULLCREDS" },			\
		{ RPC_CALL_MAJORSEEN, "MAJORSEEN" },			\
		{ RPC_TASK_DYNAMIC, "DYNAMIC" },			\
		{ RPC_TASK_NO_ROUND_ROBIN, "NO_ROUND_ROBIN" },		\
		{ RPC_TASK_SOFT, "SOFT" },				\
		{ RPC_TASK_SOFTCONN, "SOFTCONN" },			\
		{ RPC_TASK_SENT, "SENT" },				\
		{ RPC_TASK_TIMEOUT, "TIMEOUT" },			\
		{ RPC_TASK_NOCONNECT, "NOCONNECT" },			\
		{ RPC_TASK_NO_RETRANS_TIMEOUT, "NORTO" },		\
		{ RPC_TASK_CRED_NOREF, "CRED_NOREF" })

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

	TP_printk(SUNRPC_TRACE_TASK_SPECIFIER
		  " flags=%s runstate=%s status=%d action=%ps",
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
DEFINE_RPC_RUNNING_EVENT(sync_sleep);
DEFINE_RPC_RUNNING_EVENT(sync_wake);
DEFINE_RPC_RUNNING_EVENT(complete);
DEFINE_RPC_RUNNING_EVENT(timeout);
DEFINE_RPC_RUNNING_EVENT(signalled);
DEFINE_RPC_RUNNING_EVENT(end);
DEFINE_RPC_RUNNING_EVENT(call_done);

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

	TP_printk(SUNRPC_TRACE_TASK_SPECIFIER
		  " flags=%s runstate=%s status=%d timeout=%lu queue=%s",
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

	TP_printk(SUNRPC_TRACE_TASK_SPECIFIER,
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
		__assign_str(progname, task->tk_client->cl_program->name);
		__entry->version = task->tk_client->cl_vers;
		__assign_str(procname, rpc_proc_name(task));
		__assign_str(servername, task->tk_xprt->servername);
	),

	TP_printk(SUNRPC_TRACE_TASK_SPECIFIER
		  " server=%s xid=0x%08x %sv%d %s",
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

#define DEFINE_RPCB_ERROR_EVENT(name)					\
	DEFINE_EVENT(rpc_reply_event, rpcb_##name##_err,		\
			TP_PROTO(					\
				const struct rpc_task *task		\
			),						\
			TP_ARGS(task))

DEFINE_RPCB_ERROR_EVENT(prog_unavail);
DEFINE_RPCB_ERROR_EVENT(timeout);
DEFINE_RPCB_ERROR_EVENT(bind_version);
DEFINE_RPCB_ERROR_EVENT(unreachable);
DEFINE_RPCB_ERROR_EVENT(unrecognized);

TRACE_EVENT(rpc_buf_alloc,
	TP_PROTO(
		const struct rpc_task *task,
		int status
	),

	TP_ARGS(task, status),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(size_t, callsize)
		__field(size_t, recvsize)
		__field(int, status)
	),

	TP_fast_assign(
		__entry->task_id = task->tk_pid;
		__entry->client_id = task->tk_client->cl_clid;
		__entry->callsize = task->tk_rqstp->rq_callsize;
		__entry->recvsize = task->tk_rqstp->rq_rcvsize;
		__entry->status = status;
	),

	TP_printk(SUNRPC_TRACE_TASK_SPECIFIER
		  " callsize=%zu recvsize=%zu status=%d",
		__entry->task_id, __entry->client_id,
		__entry->callsize, __entry->recvsize, __entry->status
	)
);

TRACE_EVENT(rpc_call_rpcerror,
	TP_PROTO(
		const struct rpc_task *task,
		int tk_status,
		int rpc_status
	),

	TP_ARGS(task, tk_status, rpc_status),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(int, tk_status)
		__field(int, rpc_status)
	),

	TP_fast_assign(
		__entry->client_id = task->tk_client->cl_clid;
		__entry->task_id = task->tk_pid;
		__entry->tk_status = tk_status;
		__entry->rpc_status = rpc_status;
	),

	TP_printk(SUNRPC_TRACE_TASK_SPECIFIER
		  " tk_status=%d rpc_status=%d",
		__entry->task_id, __entry->client_id,
		__entry->tk_status, __entry->rpc_status)
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
		__field(u32, xprt_id)
	),

	TP_fast_assign(
		__entry->client_id = task->tk_client->cl_clid;
		__entry->task_id = task->tk_pid;
		__entry->xid = be32_to_cpu(task->tk_rqstp->rq_xid);
		__entry->version = task->tk_client->cl_vers;
		__assign_str(progname, task->tk_client->cl_program->name);
		__assign_str(procname, rpc_proc_name(task));
		__entry->backlog = ktime_to_us(backlog);
		__entry->rtt = ktime_to_us(rtt);
		__entry->execute = ktime_to_us(execute);
		__entry->xprt_id = task->tk_xprt->id;
	),

	TP_printk(SUNRPC_TRACE_TASK_SPECIFIER
		  " xid=0x%08x %sv%d %s backlog=%lu rtt=%lu execute=%lu"
		  " xprt_id=%d",
		__entry->task_id, __entry->client_id, __entry->xid,
		__get_str(progname), __entry->version, __get_str(procname),
		__entry->backlog, __entry->rtt, __entry->execute,
		__entry->xprt_id)
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
		__string(progname, xdr->rqst ?
			 xdr->rqst->rq_task->tk_client->cl_program->name : "unknown")
		__string(procedure, xdr->rqst ?
			 xdr->rqst->rq_task->tk_msg.rpc_proc->p_name : "unknown")
	),

	TP_fast_assign(
		if (xdr->rqst) {
			const struct rpc_task *task = xdr->rqst->rq_task;

			__entry->task_id = task->tk_pid;
			__entry->client_id = task->tk_client->cl_clid;
			__assign_str(progname,
				     task->tk_client->cl_program->name);
			__entry->version = task->tk_client->cl_vers;
			__assign_str(procedure, task->tk_msg.rpc_proc->p_name);
		} else {
			__entry->task_id = -1;
			__entry->client_id = -1;
			__assign_str(progname, "unknown");
			__entry->version = 0;
			__assign_str(procedure, "unknown");
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

	TP_printk(SUNRPC_TRACE_TASK_SPECIFIER
		  " %sv%d %s requested=%zu p=%p end=%p xdr=[%p,%zu]/%u/[%p,%zu]/%u\n",
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
			     task->tk_client->cl_program->name);
		__entry->version = task->tk_client->cl_vers;
		__assign_str(procedure, task->tk_msg.rpc_proc->p_name);

		__entry->offset = offset;
		__entry->copied = copied;
		__entry->head_base = xdr->buf->head[0].iov_base,
		__entry->head_len = xdr->buf->head[0].iov_len,
		__entry->page_len = xdr->buf->page_len,
		__entry->tail_base = xdr->buf->tail[0].iov_base,
		__entry->tail_len = xdr->buf->tail[0].iov_len,
		__entry->len = xdr->buf->len;
	),

	TP_printk(SUNRPC_TRACE_TASK_SPECIFIER
		  " %sv%d %s offset=%zu copied=%u xdr=[%p,%zu]/%u/[%p,%zu]/%u\n",
		__entry->task_id, __entry->client_id,
		__get_str(progname), __entry->version, __get_str(procedure),
		__entry->offset, __entry->copied,
		__entry->head_base, __entry->head_len,
		__entry->page_len,
		__entry->tail_base, __entry->tail_len,
		__entry->len
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
	EM( SS_CONNECTING, "CONNECTING" )	\
	EM( SS_CONNECTED, "CONNECTED" )		\
	EMe( SS_DISCONNECTING, "DISCONNECTING" )

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


#include <trace/events/net_probe_common.h>

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
			__array(__u8, saddr, sizeof(struct sockaddr_in6))
			__array(__u8, daddr, sizeof(struct sockaddr_in6))
		),

		TP_fast_assign(
			struct inode *inode = SOCK_INODE(socket);
			const struct sock *sk = socket->sk;
			const struct inet_sock *inet = inet_sk(sk);

			memset(__entry->saddr, 0, sizeof(struct sockaddr_in6));
			memset(__entry->daddr, 0, sizeof(struct sockaddr_in6));

			TP_STORE_ADDR_PORTS(__entry, inet, sk);

			__entry->socket_state = socket->state;
			__entry->sock_state = socket->sk->sk_state;
			__entry->ino = (unsigned long long)inode->i_ino;

		),

		TP_printk(
			"socket:[%llu] srcaddr=%pISpc dstaddr=%pISpc "
			"state=%u (%s) sk_state=%u (%s)",
			__entry->ino,
			__entry->saddr,
			__entry->daddr,
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
			__array(__u8, saddr, sizeof(struct sockaddr_in6))
			__array(__u8, daddr, sizeof(struct sockaddr_in6))
		),

		TP_fast_assign(
			struct inode *inode = SOCK_INODE(socket);
			const struct sock *sk = socket->sk;
			const struct inet_sock *inet = inet_sk(sk);

			memset(__entry->saddr, 0, sizeof(struct sockaddr_in6));
			memset(__entry->daddr, 0, sizeof(struct sockaddr_in6));

			TP_STORE_ADDR_PORTS(__entry, inet, sk);

			__entry->socket_state = socket->state;
			__entry->sock_state = socket->sk->sk_state;
			__entry->ino = (unsigned long long)inode->i_ino;
			__entry->error = error;
		),

		TP_printk(
			"error=%d socket:[%llu] srcaddr=%pISpc dstaddr=%pISpc "
			"state=%u (%s) sk_state=%u (%s)",
			__entry->error,
			__entry->ino,
			__entry->saddr,
			__entry->daddr,
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

TRACE_EVENT(rpc_socket_nospace,
	TP_PROTO(
		const struct rpc_rqst *rqst,
		const struct sock_xprt *transport
	),

	TP_ARGS(rqst, transport),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(unsigned int, total)
		__field(unsigned int, remaining)
	),

	TP_fast_assign(
		__entry->task_id = rqst->rq_task->tk_pid;
		__entry->client_id = rqst->rq_task->tk_client->cl_clid;
		__entry->total = rqst->rq_slen;
		__entry->remaining = rqst->rq_slen - transport->xmit.offset;
	),

	TP_printk(SUNRPC_TRACE_TASK_SPECIFIER
		  " total=%u remaining=%u",
		__entry->task_id, __entry->client_id,
		__entry->total, __entry->remaining
	)
);

#define rpc_show_xprt_state(x)						\
	__print_flags(x, "|",						\
		{ BIT(XPRT_LOCKED),		"LOCKED" },		\
		{ BIT(XPRT_CONNECTED),		"CONNECTED" },		\
		{ BIT(XPRT_CONNECTING),		"CONNECTING" },		\
		{ BIT(XPRT_CLOSE_WAIT),		"CLOSE_WAIT" },		\
		{ BIT(XPRT_BOUND),		"BOUND" },		\
		{ BIT(XPRT_BINDING),		"BINDING" },		\
		{ BIT(XPRT_CLOSING),		"CLOSING" },		\
		{ BIT(XPRT_OFFLINE),		"OFFLINE" },		\
		{ BIT(XPRT_REMOVE),		"REMOVE" },		\
		{ BIT(XPRT_CONGESTED),		"CONGESTED" },		\
		{ BIT(XPRT_CWND_WAIT),		"CWND_WAIT" },		\
		{ BIT(XPRT_WRITE_SPACE),	"WRITE_SPACE" },	\
		{ BIT(XPRT_SND_IS_COOKIE),	"SND_IS_COOKIE" })

DECLARE_EVENT_CLASS(rpc_xprt_lifetime_class,
	TP_PROTO(
		const struct rpc_xprt *xprt
	),

	TP_ARGS(xprt),

	TP_STRUCT__entry(
		__field(unsigned long, state)
		__string(addr, xprt->address_strings[RPC_DISPLAY_ADDR])
		__string(port, xprt->address_strings[RPC_DISPLAY_PORT])
	),

	TP_fast_assign(
		__entry->state = xprt->state;
		__assign_str(addr, xprt->address_strings[RPC_DISPLAY_ADDR]);
		__assign_str(port, xprt->address_strings[RPC_DISPLAY_PORT]);
	),

	TP_printk("peer=[%s]:%s state=%s",
		__get_str(addr), __get_str(port),
		rpc_show_xprt_state(__entry->state))
);

#define DEFINE_RPC_XPRT_LIFETIME_EVENT(name) \
	DEFINE_EVENT(rpc_xprt_lifetime_class, \
			xprt_##name, \
			TP_PROTO( \
				const struct rpc_xprt *xprt \
			), \
			TP_ARGS(xprt))

DEFINE_RPC_XPRT_LIFETIME_EVENT(create);
DEFINE_RPC_XPRT_LIFETIME_EVENT(connect);
DEFINE_RPC_XPRT_LIFETIME_EVENT(disconnect_auto);
DEFINE_RPC_XPRT_LIFETIME_EVENT(disconnect_done);
DEFINE_RPC_XPRT_LIFETIME_EVENT(disconnect_force);
DEFINE_RPC_XPRT_LIFETIME_EVENT(destroy);

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

	TP_printk(SUNRPC_TRACE_TASK_SPECIFIER
		  " xid=0x%08x seqno=%u status=%d",
		__entry->task_id, __entry->client_id, __entry->xid,
		__entry->seqno, __entry->status)
);

TRACE_EVENT(xprt_retransmit,
	TP_PROTO(
		const struct rpc_rqst *rqst
	),

	TP_ARGS(rqst),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(u32, xid)
		__field(int, ntrans)
		__field(int, version)
		__field(unsigned long, timeout)
		__string(progname,
			 rqst->rq_task->tk_client->cl_program->name)
		__string(procname, rpc_proc_name(rqst->rq_task))
	),

	TP_fast_assign(
		struct rpc_task *task = rqst->rq_task;

		__entry->task_id = task->tk_pid;
		__entry->client_id = task->tk_client ?
			task->tk_client->cl_clid : -1;
		__entry->xid = be32_to_cpu(rqst->rq_xid);
		__entry->ntrans = rqst->rq_ntrans;
		__entry->timeout = task->tk_timeout;
		__assign_str(progname,
			     task->tk_client->cl_program->name);
		__entry->version = task->tk_client->cl_vers;
		__assign_str(procname, rpc_proc_name(task));
	),

	TP_printk(SUNRPC_TRACE_TASK_SPECIFIER
		  " xid=0x%08x %sv%d %s ntrans=%d timeout=%lu",
		__entry->task_id, __entry->client_id, __entry->xid,
		__get_str(progname), __entry->version, __get_str(procname),
		__entry->ntrans, __entry->timeout
	)
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
		if (xprt->snd_task &&
		    !test_bit(XPRT_SND_IS_COOKIE, &xprt->state))
			__entry->snd_task_id = xprt->snd_task->tk_pid;
		else
			__entry->snd_task_id = -1;
	),

	TP_printk(SUNRPC_TRACE_TASK_SPECIFIER
		  " snd_task:" SUNRPC_TRACE_PID_SPECIFIER,
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
		if (xprt->snd_task &&
		    !test_bit(XPRT_SND_IS_COOKIE, &xprt->state))
			__entry->snd_task_id = xprt->snd_task->tk_pid;
		else
			__entry->snd_task_id = -1;

		__entry->cong = xprt->cong;
		__entry->cwnd = xprt->cwnd;
		__entry->wait = test_bit(XPRT_CWND_WAIT, &xprt->state);
	),

	TP_printk(SUNRPC_TRACE_TASK_SPECIFIER
		  " snd_task:" SUNRPC_TRACE_PID_SPECIFIER
		  " cong=%lu cwnd=%lu%s",
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

TRACE_EVENT(xprt_reserve,
	TP_PROTO(
		const struct rpc_rqst *rqst
	),

	TP_ARGS(rqst),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(u32, xid)
	),

	TP_fast_assign(
		__entry->task_id = rqst->rq_task->tk_pid;
		__entry->client_id = rqst->rq_task->tk_client->cl_clid;
		__entry->xid = be32_to_cpu(rqst->rq_xid);
	),

	TP_printk(SUNRPC_TRACE_TASK_SPECIFIER " xid=0x%08x",
		__entry->task_id, __entry->client_id, __entry->xid
	)
);

TRACE_EVENT(xs_data_ready,
	TP_PROTO(
		const struct rpc_xprt *xprt
	),

	TP_ARGS(xprt),

	TP_STRUCT__entry(
		__string(addr, xprt->address_strings[RPC_DISPLAY_ADDR])
		__string(port, xprt->address_strings[RPC_DISPLAY_PORT])
	),

	TP_fast_assign(
		__assign_str(addr, xprt->address_strings[RPC_DISPLAY_ADDR]);
		__assign_str(port, xprt->address_strings[RPC_DISPLAY_PORT]);
	),

	TP_printk("peer=[%s]:%s", __get_str(addr), __get_str(port))
);

TRACE_EVENT(xs_stream_read_data,
	TP_PROTO(struct rpc_xprt *xprt, ssize_t err, size_t total),

	TP_ARGS(xprt, err, total),

	TP_STRUCT__entry(
		__field(ssize_t, err)
		__field(size_t, total)
		__string(addr, xprt ? xprt->address_strings[RPC_DISPLAY_ADDR] :
				EVENT_NULL_STR)
		__string(port, xprt ? xprt->address_strings[RPC_DISPLAY_PORT] :
				EVENT_NULL_STR)
	),

	TP_fast_assign(
		__entry->err = err;
		__entry->total = total;
		__assign_str(addr, xprt ?
			xprt->address_strings[RPC_DISPLAY_ADDR] : EVENT_NULL_STR);
		__assign_str(port, xprt ?
			xprt->address_strings[RPC_DISPLAY_PORT] : EVENT_NULL_STR);
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

TRACE_EVENT(rpcb_getport,
	TP_PROTO(
		const struct rpc_clnt *clnt,
		const struct rpc_task *task,
		unsigned int bind_version
	),

	TP_ARGS(clnt, task, bind_version),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(unsigned int, program)
		__field(unsigned int, version)
		__field(int, protocol)
		__field(unsigned int, bind_version)
		__string(servername, task->tk_xprt->servername)
	),

	TP_fast_assign(
		__entry->task_id = task->tk_pid;
		__entry->client_id = clnt->cl_clid;
		__entry->program = clnt->cl_prog;
		__entry->version = clnt->cl_vers;
		__entry->protocol = task->tk_xprt->prot;
		__entry->bind_version = bind_version;
		__assign_str(servername, task->tk_xprt->servername);
	),

	TP_printk(SUNRPC_TRACE_TASK_SPECIFIER
		  " server=%s program=%u version=%u protocol=%d bind_version=%u",
		__entry->task_id, __entry->client_id, __get_str(servername),
		__entry->program, __entry->version, __entry->protocol,
		__entry->bind_version
	)
);

TRACE_EVENT(rpcb_setport,
	TP_PROTO(
		const struct rpc_task *task,
		int status,
		unsigned short port
	),

	TP_ARGS(task, status, port),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(int, status)
		__field(unsigned short, port)
	),

	TP_fast_assign(
		__entry->task_id = task->tk_pid;
		__entry->client_id = task->tk_client->cl_clid;
		__entry->status = status;
		__entry->port = port;
	),

	TP_printk(SUNRPC_TRACE_TASK_SPECIFIER " status=%d port=%u",
		__entry->task_id, __entry->client_id,
		__entry->status, __entry->port
	)
);

TRACE_EVENT(pmap_register,
	TP_PROTO(
		u32 program,
		u32 version,
		int protocol,
		unsigned short port
	),

	TP_ARGS(program, version, protocol, port),

	TP_STRUCT__entry(
		__field(unsigned int, program)
		__field(unsigned int, version)
		__field(int, protocol)
		__field(unsigned int, port)
	),

	TP_fast_assign(
		__entry->program = program;
		__entry->version = version;
		__entry->protocol = protocol;
		__entry->port = port;
	),

	TP_printk("program=%u version=%u protocol=%d port=%u",
		__entry->program, __entry->version,
		__entry->protocol, __entry->port
	)
);

TRACE_EVENT(rpcb_register,
	TP_PROTO(
		u32 program,
		u32 version,
		const char *addr,
		const char *netid
	),

	TP_ARGS(program, version, addr, netid),

	TP_STRUCT__entry(
		__field(unsigned int, program)
		__field(unsigned int, version)
		__string(addr, addr)
		__string(netid, netid)
	),

	TP_fast_assign(
		__entry->program = program;
		__entry->version = version;
		__assign_str(addr, addr);
		__assign_str(netid, netid);
	),

	TP_printk("program=%u version=%u addr=%s netid=%s",
		__entry->program, __entry->version,
		__get_str(addr), __get_str(netid)
	)
);

TRACE_EVENT(rpcb_unregister,
	TP_PROTO(
		u32 program,
		u32 version,
		const char *netid
	),

	TP_ARGS(program, version, netid),

	TP_STRUCT__entry(
		__field(unsigned int, program)
		__field(unsigned int, version)
		__string(netid, netid)
	),

	TP_fast_assign(
		__entry->program = program;
		__entry->version = version;
		__assign_str(netid, netid);
	),

	TP_printk("program=%u version=%u netid=%s",
		__entry->program, __entry->version, __get_str(netid)
	)
);

/**
 ** RPC-over-TLS tracepoints
 **/

DECLARE_EVENT_CLASS(rpc_tls_class,
	TP_PROTO(
		const struct rpc_clnt *clnt,
		const struct rpc_xprt *xprt
	),

	TP_ARGS(clnt, xprt),

	TP_STRUCT__entry(
		__field(unsigned long, requested_policy)
		__field(u32, version)
		__string(servername, xprt->servername)
		__string(progname, clnt->cl_program->name)
	),

	TP_fast_assign(
		__entry->requested_policy = clnt->cl_xprtsec.policy;
		__entry->version = clnt->cl_vers;
		__assign_str(servername, xprt->servername);
		__assign_str(progname, clnt->cl_program->name)
	),

	TP_printk("server=%s %sv%u requested_policy=%s",
		__get_str(servername), __get_str(progname), __entry->version,
		rpc_show_xprtsec_policy(__entry->requested_policy)
	)
);

#define DEFINE_RPC_TLS_EVENT(name) \
	DEFINE_EVENT(rpc_tls_class, rpc_tls_##name, \
			TP_PROTO( \
				const struct rpc_clnt *clnt, \
				const struct rpc_xprt *xprt \
			), \
			TP_ARGS(clnt, xprt))

DEFINE_RPC_TLS_EVENT(unavailable);
DEFINE_RPC_TLS_EVENT(not_started);


/* Record an xdr_buf containing a fully-formed RPC message */
DECLARE_EVENT_CLASS(svc_xdr_msg_class,
	TP_PROTO(
		const struct xdr_buf *xdr
	),

	TP_ARGS(xdr),

	TP_STRUCT__entry(
		__field(u32, xid)
		__field(const void *, head_base)
		__field(size_t, head_len)
		__field(const void *, tail_base)
		__field(size_t, tail_len)
		__field(unsigned int, page_len)
		__field(unsigned int, msg_len)
	),

	TP_fast_assign(
		__be32 *p = (__be32 *)xdr->head[0].iov_base;

		__entry->xid = be32_to_cpu(*p);
		__entry->head_base = p;
		__entry->head_len = xdr->head[0].iov_len;
		__entry->tail_base = xdr->tail[0].iov_base;
		__entry->tail_len = xdr->tail[0].iov_len;
		__entry->page_len = xdr->page_len;
		__entry->msg_len = xdr->len;
	),

	TP_printk("xid=0x%08x head=[%p,%zu] page=%u tail=[%p,%zu] len=%u",
		__entry->xid,
		__entry->head_base, __entry->head_len, __entry->page_len,
		__entry->tail_base, __entry->tail_len, __entry->msg_len
	)
);

#define DEFINE_SVCXDRMSG_EVENT(name)					\
		DEFINE_EVENT(svc_xdr_msg_class,				\
				svc_xdr_##name,				\
				TP_PROTO(				\
					const struct xdr_buf *xdr	\
				),					\
				TP_ARGS(xdr))

DEFINE_SVCXDRMSG_EVENT(recvfrom);

/* Record an xdr_buf containing arbitrary data, tagged with an XID */
DECLARE_EVENT_CLASS(svc_xdr_buf_class,
	TP_PROTO(
		__be32 xid,
		const struct xdr_buf *xdr
	),

	TP_ARGS(xid, xdr),

	TP_STRUCT__entry(
		__field(u32, xid)
		__field(const void *, head_base)
		__field(size_t, head_len)
		__field(const void *, tail_base)
		__field(size_t, tail_len)
		__field(unsigned int, page_base)
		__field(unsigned int, page_len)
		__field(unsigned int, msg_len)
	),

	TP_fast_assign(
		__entry->xid = be32_to_cpu(xid);
		__entry->head_base = xdr->head[0].iov_base;
		__entry->head_len = xdr->head[0].iov_len;
		__entry->tail_base = xdr->tail[0].iov_base;
		__entry->tail_len = xdr->tail[0].iov_len;
		__entry->page_base = xdr->page_base;
		__entry->page_len = xdr->page_len;
		__entry->msg_len = xdr->len;
	),

	TP_printk("xid=0x%08x head=[%p,%zu] page=%u(%u) tail=[%p,%zu] len=%u",
		__entry->xid,
		__entry->head_base, __entry->head_len,
		__entry->page_len, __entry->page_base,
		__entry->tail_base, __entry->tail_len,
		__entry->msg_len
	)
);

#define DEFINE_SVCXDRBUF_EVENT(name)					\
		DEFINE_EVENT(svc_xdr_buf_class,				\
				svc_xdr_##name,				\
				TP_PROTO(				\
					__be32 xid,			\
					const struct xdr_buf *xdr	\
				),					\
				TP_ARGS(xid, xdr))

DEFINE_SVCXDRBUF_EVENT(sendto);

/*
 * from include/linux/sunrpc/svc.h
 */
#define SVC_RQST_FLAG_LIST						\
	svc_rqst_flag(SECURE)						\
	svc_rqst_flag(LOCAL)						\
	svc_rqst_flag(USEDEFERRAL)					\
	svc_rqst_flag(DROPME)						\
	svc_rqst_flag(VICTIM)						\
	svc_rqst_flag_end(DATA)

#undef svc_rqst_flag
#undef svc_rqst_flag_end
#define svc_rqst_flag(x)	TRACE_DEFINE_ENUM(RQ_##x);
#define svc_rqst_flag_end(x)	TRACE_DEFINE_ENUM(RQ_##x);

SVC_RQST_FLAG_LIST

#undef svc_rqst_flag
#undef svc_rqst_flag_end
#define svc_rqst_flag(x)	{ BIT(RQ_##x), #x },
#define svc_rqst_flag_end(x)	{ BIT(RQ_##x), #x }

#define show_rqstp_flags(flags)						\
		__print_flags(flags, "|", SVC_RQST_FLAG_LIST)

TRACE_DEFINE_ENUM(SVC_GARBAGE);
TRACE_DEFINE_ENUM(SVC_SYSERR);
TRACE_DEFINE_ENUM(SVC_VALID);
TRACE_DEFINE_ENUM(SVC_NEGATIVE);
TRACE_DEFINE_ENUM(SVC_OK);
TRACE_DEFINE_ENUM(SVC_DROP);
TRACE_DEFINE_ENUM(SVC_CLOSE);
TRACE_DEFINE_ENUM(SVC_DENIED);
TRACE_DEFINE_ENUM(SVC_PENDING);
TRACE_DEFINE_ENUM(SVC_COMPLETE);

#define show_svc_auth_status(status)			\
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

#define SVC_RQST_ENDPOINT_FIELDS(r) \
		__sockaddr(server, (r)->rq_xprt->xpt_locallen) \
		__sockaddr(client, (r)->rq_xprt->xpt_remotelen) \
		__field(unsigned int, netns_ino) \
		__field(u32, xid)

#define SVC_RQST_ENDPOINT_ASSIGNMENTS(r) \
		do { \
			struct svc_xprt *xprt = (r)->rq_xprt; \
			__assign_sockaddr(server, &xprt->xpt_local, \
					  xprt->xpt_locallen); \
			__assign_sockaddr(client, &xprt->xpt_remote, \
					  xprt->xpt_remotelen); \
			__entry->netns_ino = xprt->xpt_net->ns.inum; \
			__entry->xid = be32_to_cpu((r)->rq_xid); \
		} while (0)

#define SVC_RQST_ENDPOINT_FORMAT \
		"xid=0x%08x server=%pISpc client=%pISpc"

#define SVC_RQST_ENDPOINT_VARARGS \
		__entry->xid, __get_sockaddr(server), __get_sockaddr(client)

TRACE_EVENT_CONDITION(svc_authenticate,
	TP_PROTO(
		const struct svc_rqst *rqst,
		enum svc_auth_status auth_res
	),

	TP_ARGS(rqst, auth_res),

	TP_CONDITION(auth_res != SVC_OK && auth_res != SVC_COMPLETE),

	TP_STRUCT__entry(
		SVC_RQST_ENDPOINT_FIELDS(rqst)

		__field(unsigned long, svc_status)
		__field(unsigned long, auth_stat)
	),

	TP_fast_assign(
		SVC_RQST_ENDPOINT_ASSIGNMENTS(rqst);

		__entry->svc_status = auth_res;
		__entry->auth_stat = be32_to_cpu(rqst->rq_auth_stat);
	),

	TP_printk(SVC_RQST_ENDPOINT_FORMAT
		" auth_res=%s auth_stat=%s",
		SVC_RQST_ENDPOINT_VARARGS,
		show_svc_auth_status(__entry->svc_status),
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
		__string(procedure, svc_proc_name(rqst))
		__string(addr, rqst->rq_xprt ?
			 rqst->rq_xprt->xpt_remotebuf : EVENT_NULL_STR)
	),

	TP_fast_assign(
		__entry->xid = be32_to_cpu(rqst->rq_xid);
		__entry->vers = rqst->rq_vers;
		__entry->proc = rqst->rq_proc;
		__assign_str(service, name);
		__assign_str(procedure, svc_proc_name(rqst));
		__assign_str(addr, rqst->rq_xprt ?
			     rqst->rq_xprt->xpt_remotebuf : EVENT_NULL_STR);
	),

	TP_printk("addr=%s xid=0x%08x service=%s vers=%u proc=%s",
			__get_str(addr), __entry->xid,
			__get_str(service), __entry->vers,
			__get_str(procedure)
	)
);

DECLARE_EVENT_CLASS(svc_rqst_event,
	TP_PROTO(
		const struct svc_rqst *rqst
	),

	TP_ARGS(rqst),

	TP_STRUCT__entry(
		SVC_RQST_ENDPOINT_FIELDS(rqst)

		__field(unsigned long, flags)
	),

	TP_fast_assign(
		SVC_RQST_ENDPOINT_ASSIGNMENTS(rqst);

		__entry->flags = rqst->rq_flags;
	),

	TP_printk(SVC_RQST_ENDPOINT_FORMAT " flags=%s",
		SVC_RQST_ENDPOINT_VARARGS,
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
	TP_PROTO(
		const struct svc_rqst *rqst,
		int status
	),

	TP_ARGS(rqst, status),

	TP_STRUCT__entry(
		SVC_RQST_ENDPOINT_FIELDS(rqst)

		__field(int, status)
		__field(unsigned long, flags)
	),

	TP_fast_assign(
		SVC_RQST_ENDPOINT_ASSIGNMENTS(rqst);

		__entry->status = status;
		__entry->flags = rqst->rq_flags;
	),

	TP_printk(SVC_RQST_ENDPOINT_FORMAT " status=%d flags=%s",
		SVC_RQST_ENDPOINT_VARARGS,
		__entry->status, show_rqstp_flags(__entry->flags))
);

DEFINE_EVENT(svc_rqst_status, svc_send,
	TP_PROTO(const struct svc_rqst *rqst, int status),
	TP_ARGS(rqst, status));

TRACE_EVENT(svc_replace_page_err,
	TP_PROTO(const struct svc_rqst *rqst),

	TP_ARGS(rqst),
	TP_STRUCT__entry(
		SVC_RQST_ENDPOINT_FIELDS(rqst)

		__field(const void *, begin)
		__field(const void *, respages)
		__field(const void *, nextpage)
	),

	TP_fast_assign(
		SVC_RQST_ENDPOINT_ASSIGNMENTS(rqst);

		__entry->begin = rqst->rq_pages;
		__entry->respages = rqst->rq_respages;
		__entry->nextpage = rqst->rq_next_page;
	),

	TP_printk(SVC_RQST_ENDPOINT_FORMAT " begin=%p respages=%p nextpage=%p",
		SVC_RQST_ENDPOINT_VARARGS,
		__entry->begin, __entry->respages, __entry->nextpage)
);

TRACE_EVENT(svc_stats_latency,
	TP_PROTO(
		const struct svc_rqst *rqst
	),

	TP_ARGS(rqst),

	TP_STRUCT__entry(
		SVC_RQST_ENDPOINT_FIELDS(rqst)

		__field(unsigned long, execute)
		__string(procedure, svc_proc_name(rqst))
	),

	TP_fast_assign(
		SVC_RQST_ENDPOINT_ASSIGNMENTS(rqst);

		__entry->execute = ktime_to_us(ktime_sub(ktime_get(),
							 rqst->rq_stime));
		__assign_str(procedure, svc_proc_name(rqst));
	),

	TP_printk(SVC_RQST_ENDPOINT_FORMAT " proc=%s execute-us=%lu",
		SVC_RQST_ENDPOINT_VARARGS,
		__get_str(procedure), __entry->execute)
);

/*
 * from include/linux/sunrpc/svc_xprt.h
 */
#define SVC_XPRT_FLAG_LIST						\
	svc_xprt_flag(BUSY)						\
	svc_xprt_flag(CONN)						\
	svc_xprt_flag(CLOSE)						\
	svc_xprt_flag(DATA)						\
	svc_xprt_flag(TEMP)						\
	svc_xprt_flag(DEAD)						\
	svc_xprt_flag(CHNGBUF)						\
	svc_xprt_flag(DEFERRED)						\
	svc_xprt_flag(OLD)						\
	svc_xprt_flag(LISTENER)						\
	svc_xprt_flag(CACHE_AUTH)					\
	svc_xprt_flag(LOCAL)						\
	svc_xprt_flag(KILL_TEMP)					\
	svc_xprt_flag(CONG_CTRL)					\
	svc_xprt_flag(HANDSHAKE)					\
	svc_xprt_flag(TLS_SESSION)					\
	svc_xprt_flag_end(PEER_AUTH)

#undef svc_xprt_flag
#undef svc_xprt_flag_end
#define svc_xprt_flag(x)	TRACE_DEFINE_ENUM(XPT_##x);
#define svc_xprt_flag_end(x)	TRACE_DEFINE_ENUM(XPT_##x);

SVC_XPRT_FLAG_LIST

#undef svc_xprt_flag
#undef svc_xprt_flag_end
#define svc_xprt_flag(x)	{ BIT(XPT_##x), #x },
#define svc_xprt_flag_end(x)	{ BIT(XPT_##x), #x }

#define show_svc_xprt_flags(flags)					\
	__print_flags(flags, "|", SVC_XPRT_FLAG_LIST)

TRACE_EVENT(svc_xprt_create_err,
	TP_PROTO(
		const char *program,
		const char *protocol,
		struct sockaddr *sap,
		size_t salen,
		const struct svc_xprt *xprt
	),

	TP_ARGS(program, protocol, sap, salen, xprt),

	TP_STRUCT__entry(
		__field(long, error)
		__string(program, program)
		__string(protocol, protocol)
		__sockaddr(addr, salen)
	),

	TP_fast_assign(
		__entry->error = PTR_ERR(xprt);
		__assign_str(program, program);
		__assign_str(protocol, protocol);
		__assign_sockaddr(addr, sap, salen);
	),

	TP_printk("addr=%pISpc program=%s protocol=%s error=%ld",
		__get_sockaddr(addr), __get_str(program), __get_str(protocol),
		__entry->error)
);

#define SVC_XPRT_ENDPOINT_FIELDS(x) \
		__sockaddr(server, (x)->xpt_locallen) \
		__sockaddr(client, (x)->xpt_remotelen) \
		__field(unsigned long, flags) \
		__field(unsigned int, netns_ino)

#define SVC_XPRT_ENDPOINT_ASSIGNMENTS(x) \
		do { \
			__assign_sockaddr(server, &(x)->xpt_local, \
					  (x)->xpt_locallen); \
			__assign_sockaddr(client, &(x)->xpt_remote, \
					  (x)->xpt_remotelen); \
			__entry->flags = (x)->xpt_flags; \
			__entry->netns_ino = (x)->xpt_net->ns.inum; \
		} while (0)

#define SVC_XPRT_ENDPOINT_FORMAT \
		"server=%pISpc client=%pISpc flags=%s"

#define SVC_XPRT_ENDPOINT_VARARGS \
		__get_sockaddr(server), __get_sockaddr(client), \
		show_svc_xprt_flags(__entry->flags)

TRACE_EVENT(svc_xprt_enqueue,
	TP_PROTO(
		const struct svc_xprt *xprt,
		unsigned long flags
	),

	TP_ARGS(xprt, flags),

	TP_STRUCT__entry(
		SVC_XPRT_ENDPOINT_FIELDS(xprt)
	),

	TP_fast_assign(
		__assign_sockaddr(server, &xprt->xpt_local,
				  xprt->xpt_locallen);
		__assign_sockaddr(client, &xprt->xpt_remote,
				  xprt->xpt_remotelen);
		__entry->flags = flags;
		__entry->netns_ino = xprt->xpt_net->ns.inum;
	),

	TP_printk(SVC_XPRT_ENDPOINT_FORMAT, SVC_XPRT_ENDPOINT_VARARGS)
);

TRACE_EVENT(svc_xprt_dequeue,
	TP_PROTO(
		const struct svc_rqst *rqst
	),

	TP_ARGS(rqst),

	TP_STRUCT__entry(
		SVC_XPRT_ENDPOINT_FIELDS(rqst->rq_xprt)

		__field(unsigned long, wakeup)
	),

	TP_fast_assign(
		SVC_XPRT_ENDPOINT_ASSIGNMENTS(rqst->rq_xprt);

		__entry->wakeup = ktime_to_us(ktime_sub(ktime_get(),
							rqst->rq_qtime));
	),

	TP_printk(SVC_XPRT_ENDPOINT_FORMAT " wakeup-us=%lu",
		SVC_XPRT_ENDPOINT_VARARGS, __entry->wakeup)
);

DECLARE_EVENT_CLASS(svc_xprt_event,
	TP_PROTO(
		const struct svc_xprt *xprt
	),

	TP_ARGS(xprt),

	TP_STRUCT__entry(
		SVC_XPRT_ENDPOINT_FIELDS(xprt)
	),

	TP_fast_assign(
		SVC_XPRT_ENDPOINT_ASSIGNMENTS(xprt);
	),

	TP_printk(SVC_XPRT_ENDPOINT_FORMAT, SVC_XPRT_ENDPOINT_VARARGS)
);

#define DEFINE_SVC_XPRT_EVENT(name) \
	DEFINE_EVENT(svc_xprt_event, svc_xprt_##name, \
			TP_PROTO( \
				const struct svc_xprt *xprt \
			), \
			TP_ARGS(xprt))

DEFINE_SVC_XPRT_EVENT(no_write_space);
DEFINE_SVC_XPRT_EVENT(close);
DEFINE_SVC_XPRT_EVENT(detach);
DEFINE_SVC_XPRT_EVENT(free);

#define DEFINE_SVC_TLS_EVENT(name) \
	DEFINE_EVENT(svc_xprt_event, svc_tls_##name, \
		TP_PROTO(const struct svc_xprt *xprt), \
		TP_ARGS(xprt))

DEFINE_SVC_TLS_EVENT(start);
DEFINE_SVC_TLS_EVENT(upcall);
DEFINE_SVC_TLS_EVENT(unavailable);
DEFINE_SVC_TLS_EVENT(not_started);
DEFINE_SVC_TLS_EVENT(timed_out);

TRACE_EVENT(svc_xprt_accept,
	TP_PROTO(
		const struct svc_xprt *xprt,
		const char *service
	),

	TP_ARGS(xprt, service),

	TP_STRUCT__entry(
		SVC_XPRT_ENDPOINT_FIELDS(xprt)

		__string(protocol, xprt->xpt_class->xcl_name)
		__string(service, service)
	),

	TP_fast_assign(
		SVC_XPRT_ENDPOINT_ASSIGNMENTS(xprt);

		__assign_str(protocol, xprt->xpt_class->xcl_name);
		__assign_str(service, service);
	),

	TP_printk(SVC_XPRT_ENDPOINT_FORMAT " protocol=%s service=%s",
		SVC_XPRT_ENDPOINT_VARARGS,
		__get_str(protocol), __get_str(service)
	)
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

TRACE_EVENT(svc_alloc_arg_err,
	TP_PROTO(
		unsigned int requested,
		unsigned int allocated
	),

	TP_ARGS(requested, allocated),

	TP_STRUCT__entry(
		__field(unsigned int, requested)
		__field(unsigned int, allocated)
	),

	TP_fast_assign(
		__entry->requested = requested;
		__entry->allocated = allocated;
	),

	TP_printk("requested=%u allocated=%u",
		__entry->requested, __entry->allocated)
);

DECLARE_EVENT_CLASS(svc_deferred_event,
	TP_PROTO(
		const struct svc_deferred_req *dr
	),

	TP_ARGS(dr),

	TP_STRUCT__entry(
		__field(const void *, dr)
		__field(u32, xid)
		__sockaddr(addr, dr->addrlen)
	),

	TP_fast_assign(
		__entry->dr = dr;
		__entry->xid = be32_to_cpu(*(__be32 *)dr->args);
		__assign_sockaddr(addr, &dr->addr, dr->addrlen);
	),

	TP_printk("addr=%pISpc dr=%p xid=0x%08x", __get_sockaddr(addr),
		__entry->dr, __entry->xid)
);

#define DEFINE_SVC_DEFERRED_EVENT(name) \
	DEFINE_EVENT(svc_deferred_event, svc_defer_##name, \
			TP_PROTO( \
				const struct svc_deferred_req *dr \
			), \
			TP_ARGS(dr))

DEFINE_SVC_DEFERRED_EVENT(drop);
DEFINE_SVC_DEFERRED_EVENT(queue);
DEFINE_SVC_DEFERRED_EVENT(recv);

DECLARE_EVENT_CLASS(svcsock_lifetime_class,
	TP_PROTO(
		const void *svsk,
		const struct socket *socket
	),
	TP_ARGS(svsk, socket),
	TP_STRUCT__entry(
		__field(unsigned int, netns_ino)
		__field(const void *, svsk)
		__field(const void *, sk)
		__field(unsigned long, type)
		__field(unsigned long, family)
		__field(unsigned long, state)
	),
	TP_fast_assign(
		struct sock *sk = socket->sk;

		__entry->netns_ino = sock_net(sk)->ns.inum;
		__entry->svsk = svsk;
		__entry->sk = sk;
		__entry->type = socket->type;
		__entry->family = sk->sk_family;
		__entry->state = sk->sk_state;
	),
	TP_printk("svsk=%p type=%s family=%s%s",
		__entry->svsk, show_socket_type(__entry->type),
		rpc_show_address_family(__entry->family),
		__entry->state == TCP_LISTEN ? " (listener)" : ""
	)
);
#define DEFINE_SVCSOCK_LIFETIME_EVENT(name) \
	DEFINE_EVENT(svcsock_lifetime_class, name, \
		TP_PROTO( \
			const void *svsk, \
			const struct socket *socket \
		), \
		TP_ARGS(svsk, socket))

DEFINE_SVCSOCK_LIFETIME_EVENT(svcsock_new);
DEFINE_SVCSOCK_LIFETIME_EVENT(svcsock_free);

TRACE_EVENT(svcsock_marker,
	TP_PROTO(
		const struct svc_xprt *xprt,
		__be32 marker
	),

	TP_ARGS(xprt, marker),

	TP_STRUCT__entry(
		__field(unsigned int, length)
		__field(bool, last)
		__string(addr, xprt->xpt_remotebuf)
	),

	TP_fast_assign(
		__entry->length = be32_to_cpu(marker) & RPC_FRAGMENT_SIZE_MASK;
		__entry->last = be32_to_cpu(marker) & RPC_LAST_STREAM_FRAGMENT;
		__assign_str(addr, xprt->xpt_remotebuf);
	),

	TP_printk("addr=%s length=%u%s", __get_str(addr),
		__entry->length, __entry->last ? " (last)" : "")
);

DECLARE_EVENT_CLASS(svcsock_class,
	TP_PROTO(
		const struct svc_xprt *xprt,
		ssize_t result
	),

	TP_ARGS(xprt, result),

	TP_STRUCT__entry(
		__field(ssize_t, result)
		__field(unsigned long, flags)
		__string(addr, xprt->xpt_remotebuf)
	),

	TP_fast_assign(
		__entry->result = result;
		__entry->flags = xprt->xpt_flags;
		__assign_str(addr, xprt->xpt_remotebuf);
	),

	TP_printk("addr=%s result=%zd flags=%s", __get_str(addr),
		__entry->result, show_svc_xprt_flags(__entry->flags)
	)
);

#define DEFINE_SVCSOCK_EVENT(name) \
	DEFINE_EVENT(svcsock_class, svcsock_##name, \
			TP_PROTO( \
				const struct svc_xprt *xprt, \
				ssize_t result \
			), \
			TP_ARGS(xprt, result))

DEFINE_SVCSOCK_EVENT(udp_send);
DEFINE_SVCSOCK_EVENT(udp_recv);
DEFINE_SVCSOCK_EVENT(udp_recv_err);
DEFINE_SVCSOCK_EVENT(tcp_send);
DEFINE_SVCSOCK_EVENT(tcp_recv);
DEFINE_SVCSOCK_EVENT(tcp_recv_eagain);
DEFINE_SVCSOCK_EVENT(tcp_recv_err);
DEFINE_SVCSOCK_EVENT(data_ready);
DEFINE_SVCSOCK_EVENT(write_space);

TRACE_EVENT(svcsock_tcp_recv_short,
	TP_PROTO(
		const struct svc_xprt *xprt,
		u32 expected,
		u32 received
	),

	TP_ARGS(xprt, expected, received),

	TP_STRUCT__entry(
		__field(u32, expected)
		__field(u32, received)
		__field(unsigned long, flags)
		__string(addr, xprt->xpt_remotebuf)
	),

	TP_fast_assign(
		__entry->expected = expected;
		__entry->received = received;
		__entry->flags = xprt->xpt_flags;
		__assign_str(addr, xprt->xpt_remotebuf);
	),

	TP_printk("addr=%s flags=%s expected=%u received=%u",
		__get_str(addr), show_svc_xprt_flags(__entry->flags),
		__entry->expected, __entry->received
	)
);

TRACE_EVENT(svcsock_tcp_state,
	TP_PROTO(
		const struct svc_xprt *xprt,
		const struct socket *socket
	),

	TP_ARGS(xprt, socket),

	TP_STRUCT__entry(
		__field(unsigned long, socket_state)
		__field(unsigned long, sock_state)
		__field(unsigned long, flags)
		__string(addr, xprt->xpt_remotebuf)
	),

	TP_fast_assign(
		__entry->socket_state = socket->state;
		__entry->sock_state = socket->sk->sk_state;
		__entry->flags = xprt->xpt_flags;
		__assign_str(addr, xprt->xpt_remotebuf);
	),

	TP_printk("addr=%s state=%s sk_state=%s flags=%s", __get_str(addr),
		rpc_show_socket_state(__entry->socket_state),
		rpc_show_sock_state(__entry->sock_state),
		show_svc_xprt_flags(__entry->flags)
	)
);

DECLARE_EVENT_CLASS(svcsock_accept_class,
	TP_PROTO(
		const struct svc_xprt *xprt,
		const char *service,
		long status
	),

	TP_ARGS(xprt, service, status),

	TP_STRUCT__entry(
		__field(long, status)
		__string(service, service)
		__field(unsigned int, netns_ino)
	),

	TP_fast_assign(
		__entry->status = status;
		__assign_str(service, service);
		__entry->netns_ino = xprt->xpt_net->ns.inum;
	),

	TP_printk("addr=listener service=%s status=%ld",
		__get_str(service), __entry->status
	)
);

#define DEFINE_ACCEPT_EVENT(name) \
	DEFINE_EVENT(svcsock_accept_class, svcsock_##name##_err, \
			TP_PROTO( \
				const struct svc_xprt *xprt, \
				const char *service, \
				long status \
			), \
			TP_ARGS(xprt, service, status))

DEFINE_ACCEPT_EVENT(accept);
DEFINE_ACCEPT_EVENT(getpeername);

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

DECLARE_EVENT_CLASS(register_class,
	TP_PROTO(
		const char *program,
		const u32 version,
		const int family,
		const unsigned short protocol,
		const unsigned short port,
		int error
	),

	TP_ARGS(program, version, family, protocol, port, error),

	TP_STRUCT__entry(
		__field(u32, version)
		__field(unsigned long, family)
		__field(unsigned short, protocol)
		__field(unsigned short, port)
		__field(int, error)
		__string(program, program)
	),

	TP_fast_assign(
		__entry->version = version;
		__entry->family = family;
		__entry->protocol = protocol;
		__entry->port = port;
		__entry->error = error;
		__assign_str(program, program);
	),

	TP_printk("program=%sv%u proto=%s port=%u family=%s error=%d",
		__get_str(program), __entry->version,
		__entry->protocol == IPPROTO_UDP ? "udp" : "tcp",
		__entry->port, rpc_show_address_family(__entry->family),
		__entry->error
	)
);

#define DEFINE_REGISTER_EVENT(name) \
	DEFINE_EVENT(register_class, svc_##name, \
			TP_PROTO( \
				const char *program, \
				const u32 version, \
				const int family, \
				const unsigned short protocol, \
				const unsigned short port, \
				int error \
			), \
			TP_ARGS(program, version, family, protocol, \
				port, error))

DEFINE_REGISTER_EVENT(register);
DEFINE_REGISTER_EVENT(noregister);

TRACE_EVENT(svc_unregister,
	TP_PROTO(
		const char *program,
		const u32 version,
		int error
	),

	TP_ARGS(program, version, error),

	TP_STRUCT__entry(
		__field(u32, version)
		__field(int, error)
		__string(program, program)
	),

	TP_fast_assign(
		__entry->version = version;
		__entry->error = error;
		__assign_str(program, program);
	),

	TP_printk("program=%sv%u error=%d",
		__get_str(program), __entry->version, __entry->error
	)
);

#endif /* _TRACE_SUNRPC_H */

#include <trace/define_trace.h>
