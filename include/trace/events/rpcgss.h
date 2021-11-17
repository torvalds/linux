/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018 Oracle.  All rights reserved.
 *
 * Trace point definitions for the "rpcgss" subsystem.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM rpcgss

#if !defined(_TRACE_RPCRDMA_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_RPCGSS_H

#include <linux/tracepoint.h>

/**
 ** GSS-API related trace events
 **/

TRACE_DEFINE_ENUM(RPC_GSS_SVC_NONE);
TRACE_DEFINE_ENUM(RPC_GSS_SVC_INTEGRITY);
TRACE_DEFINE_ENUM(RPC_GSS_SVC_PRIVACY);

#define show_gss_service(x)						\
	__print_symbolic(x,						\
		{ RPC_GSS_SVC_NONE,		"none" },		\
		{ RPC_GSS_SVC_INTEGRITY,	"integrity" },		\
		{ RPC_GSS_SVC_PRIVACY,		"privacy" })

TRACE_DEFINE_ENUM(GSS_S_BAD_MECH);
TRACE_DEFINE_ENUM(GSS_S_BAD_NAME);
TRACE_DEFINE_ENUM(GSS_S_BAD_NAMETYPE);
TRACE_DEFINE_ENUM(GSS_S_BAD_BINDINGS);
TRACE_DEFINE_ENUM(GSS_S_BAD_STATUS);
TRACE_DEFINE_ENUM(GSS_S_BAD_SIG);
TRACE_DEFINE_ENUM(GSS_S_NO_CRED);
TRACE_DEFINE_ENUM(GSS_S_NO_CONTEXT);
TRACE_DEFINE_ENUM(GSS_S_DEFECTIVE_TOKEN);
TRACE_DEFINE_ENUM(GSS_S_DEFECTIVE_CREDENTIAL);
TRACE_DEFINE_ENUM(GSS_S_CREDENTIALS_EXPIRED);
TRACE_DEFINE_ENUM(GSS_S_CONTEXT_EXPIRED);
TRACE_DEFINE_ENUM(GSS_S_FAILURE);
TRACE_DEFINE_ENUM(GSS_S_BAD_QOP);
TRACE_DEFINE_ENUM(GSS_S_UNAUTHORIZED);
TRACE_DEFINE_ENUM(GSS_S_UNAVAILABLE);
TRACE_DEFINE_ENUM(GSS_S_DUPLICATE_ELEMENT);
TRACE_DEFINE_ENUM(GSS_S_NAME_NOT_MN);
TRACE_DEFINE_ENUM(GSS_S_CONTINUE_NEEDED);
TRACE_DEFINE_ENUM(GSS_S_DUPLICATE_TOKEN);
TRACE_DEFINE_ENUM(GSS_S_OLD_TOKEN);
TRACE_DEFINE_ENUM(GSS_S_UNSEQ_TOKEN);
TRACE_DEFINE_ENUM(GSS_S_GAP_TOKEN);

#define show_gss_status(x)						\
	__print_flags(x, "|",						\
		{ GSS_S_BAD_MECH, "GSS_S_BAD_MECH" },			\
		{ GSS_S_BAD_NAME, "GSS_S_BAD_NAME" },			\
		{ GSS_S_BAD_NAMETYPE, "GSS_S_BAD_NAMETYPE" },		\
		{ GSS_S_BAD_BINDINGS, "GSS_S_BAD_BINDINGS" },		\
		{ GSS_S_BAD_STATUS, "GSS_S_BAD_STATUS" },		\
		{ GSS_S_BAD_SIG, "GSS_S_BAD_SIG" },			\
		{ GSS_S_NO_CRED, "GSS_S_NO_CRED" },			\
		{ GSS_S_NO_CONTEXT, "GSS_S_NO_CONTEXT" },		\
		{ GSS_S_DEFECTIVE_TOKEN, "GSS_S_DEFECTIVE_TOKEN" },	\
		{ GSS_S_DEFECTIVE_CREDENTIAL, "GSS_S_DEFECTIVE_CREDENTIAL" }, \
		{ GSS_S_CREDENTIALS_EXPIRED, "GSS_S_CREDENTIALS_EXPIRED" }, \
		{ GSS_S_CONTEXT_EXPIRED, "GSS_S_CONTEXT_EXPIRED" },	\
		{ GSS_S_FAILURE, "GSS_S_FAILURE" },			\
		{ GSS_S_BAD_QOP, "GSS_S_BAD_QOP" },			\
		{ GSS_S_UNAUTHORIZED, "GSS_S_UNAUTHORIZED" },		\
		{ GSS_S_UNAVAILABLE, "GSS_S_UNAVAILABLE" },		\
		{ GSS_S_DUPLICATE_ELEMENT, "GSS_S_DUPLICATE_ELEMENT" },	\
		{ GSS_S_NAME_NOT_MN, "GSS_S_NAME_NOT_MN" },		\
		{ GSS_S_CONTINUE_NEEDED, "GSS_S_CONTINUE_NEEDED" },	\
		{ GSS_S_DUPLICATE_TOKEN, "GSS_S_DUPLICATE_TOKEN" },	\
		{ GSS_S_OLD_TOKEN, "GSS_S_OLD_TOKEN" },			\
		{ GSS_S_UNSEQ_TOKEN, "GSS_S_UNSEQ_TOKEN" },		\
		{ GSS_S_GAP_TOKEN, "GSS_S_GAP_TOKEN" })


DECLARE_EVENT_CLASS(rpcgss_gssapi_event,
	TP_PROTO(
		const struct rpc_task *task,
		u32 maj_stat
	),

	TP_ARGS(task, maj_stat),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(u32, maj_stat)

	),

	TP_fast_assign(
		__entry->task_id = task->tk_pid;
		__entry->client_id = task->tk_client->cl_clid;
		__entry->maj_stat = maj_stat;
	),

	TP_printk("task:%u@%u maj_stat=%s",
		__entry->task_id, __entry->client_id,
		__entry->maj_stat == 0 ?
		"GSS_S_COMPLETE" : show_gss_status(__entry->maj_stat))
);

#define DEFINE_GSSAPI_EVENT(name)					\
	DEFINE_EVENT(rpcgss_gssapi_event, rpcgss_##name,		\
			TP_PROTO(					\
				const struct rpc_task *task,		\
				u32 maj_stat				\
			),						\
			TP_ARGS(task, maj_stat))

TRACE_EVENT(rpcgss_import_ctx,
	TP_PROTO(
		int status
	),

	TP_ARGS(status),

	TP_STRUCT__entry(
		__field(int, status)
	),

	TP_fast_assign(
		__entry->status = status;
	),

	TP_printk("status=%d", __entry->status)
);

DEFINE_GSSAPI_EVENT(get_mic);
DEFINE_GSSAPI_EVENT(verify_mic);
DEFINE_GSSAPI_EVENT(wrap);
DEFINE_GSSAPI_EVENT(unwrap);

DECLARE_EVENT_CLASS(rpcgss_ctx_class,
	TP_PROTO(
		const struct gss_cred *gc
	),

	TP_ARGS(gc),

	TP_STRUCT__entry(
		__field(const void *, cred)
		__field(unsigned long, service)
		__string(principal, gc->gc_principal)
	),

	TP_fast_assign(
		__entry->cred = gc;
		__entry->service = gc->gc_service;
		__assign_str(principal, gc->gc_principal);
	),

	TP_printk("cred=%p service=%s principal='%s'",
		__entry->cred, show_gss_service(__entry->service),
		__get_str(principal))
);

#define DEFINE_CTX_EVENT(name)						\
	DEFINE_EVENT(rpcgss_ctx_class, rpcgss_ctx_##name,		\
			TP_PROTO(					\
				const struct gss_cred *gc		\
			),						\
			TP_ARGS(gc))

DEFINE_CTX_EVENT(init);
DEFINE_CTX_EVENT(destroy);

DECLARE_EVENT_CLASS(rpcgss_svc_gssapi_class,
	TP_PROTO(
		const struct svc_rqst *rqstp,
		u32 maj_stat
	),

	TP_ARGS(rqstp, maj_stat),

	TP_STRUCT__entry(
		__field(u32, xid)
		__field(u32, maj_stat)
		__string(addr, rqstp->rq_xprt->xpt_remotebuf)
	),

	TP_fast_assign(
		__entry->xid = __be32_to_cpu(rqstp->rq_xid);
		__entry->maj_stat = maj_stat;
		__assign_str(addr, rqstp->rq_xprt->xpt_remotebuf);
	),

	TP_printk("addr=%s xid=0x%08x maj_stat=%s",
		__get_str(addr), __entry->xid,
		__entry->maj_stat == 0 ?
		"GSS_S_COMPLETE" : show_gss_status(__entry->maj_stat))
);

#define DEFINE_SVC_GSSAPI_EVENT(name)					\
	DEFINE_EVENT(rpcgss_svc_gssapi_class, rpcgss_svc_##name,	\
			TP_PROTO(					\
				const struct svc_rqst *rqstp,		\
				u32 maj_stat				\
			),						\
			TP_ARGS(rqstp, maj_stat))

DEFINE_SVC_GSSAPI_EVENT(unwrap);
DEFINE_SVC_GSSAPI_EVENT(mic);

TRACE_EVENT(rpcgss_svc_unwrap_failed,
	TP_PROTO(
		const struct svc_rqst *rqstp
	),

	TP_ARGS(rqstp),

	TP_STRUCT__entry(
		__field(u32, xid)
		__string(addr, rqstp->rq_xprt->xpt_remotebuf)
	),

	TP_fast_assign(
		__entry->xid = be32_to_cpu(rqstp->rq_xid);
		__assign_str(addr, rqstp->rq_xprt->xpt_remotebuf);
	),

	TP_printk("addr=%s xid=0x%08x", __get_str(addr), __entry->xid)
);

TRACE_EVENT(rpcgss_svc_seqno_bad,
	TP_PROTO(
		const struct svc_rqst *rqstp,
		u32 expected,
		u32 received
	),

	TP_ARGS(rqstp, expected, received),

	TP_STRUCT__entry(
		__field(u32, expected)
		__field(u32, received)
		__field(u32, xid)
		__string(addr, rqstp->rq_xprt->xpt_remotebuf)
	),

	TP_fast_assign(
		__entry->expected = expected;
		__entry->received = received;
		__entry->xid = __be32_to_cpu(rqstp->rq_xid);
		__assign_str(addr, rqstp->rq_xprt->xpt_remotebuf);
	),

	TP_printk("addr=%s xid=0x%08x expected seqno %u, received seqno %u",
		__get_str(addr), __entry->xid,
		__entry->expected, __entry->received)
);

TRACE_EVENT(rpcgss_svc_accept_upcall,
	TP_PROTO(
		const struct svc_rqst *rqstp,
		u32 major_status,
		u32 minor_status
	),

	TP_ARGS(rqstp, major_status, minor_status),

	TP_STRUCT__entry(
		__field(u32, minor_status)
		__field(unsigned long, major_status)
		__field(u32, xid)
		__string(addr, rqstp->rq_xprt->xpt_remotebuf)
	),

	TP_fast_assign(
		__entry->minor_status = minor_status;
		__entry->major_status = major_status;
		__entry->xid = be32_to_cpu(rqstp->rq_xid);
		__assign_str(addr, rqstp->rq_xprt->xpt_remotebuf);
	),

	TP_printk("addr=%s xid=0x%08x major_status=%s (0x%08lx) minor_status=%u",
		__get_str(addr), __entry->xid,
		(__entry->major_status == 0) ? "GSS_S_COMPLETE" :
			show_gss_status(__entry->major_status),
		__entry->major_status, __entry->minor_status
	)
);

TRACE_EVENT(rpcgss_svc_authenticate,
	TP_PROTO(
		const struct svc_rqst *rqstp,
		const struct rpc_gss_wire_cred *gc
	),

	TP_ARGS(rqstp, gc),

	TP_STRUCT__entry(
		__field(u32, seqno)
		__field(u32, xid)
		__string(addr, rqstp->rq_xprt->xpt_remotebuf)
	),

	TP_fast_assign(
		__entry->xid = be32_to_cpu(rqstp->rq_xid);
		__entry->seqno = gc->gc_seq;
		__assign_str(addr, rqstp->rq_xprt->xpt_remotebuf);
	),

	TP_printk("addr=%s xid=0x%08x seqno=%u", __get_str(addr),
		__entry->xid, __entry->seqno)
);


/**
 ** GSS auth unwrap failures
 **/

TRACE_EVENT(rpcgss_unwrap_failed,
	TP_PROTO(
		const struct rpc_task *task
	),

	TP_ARGS(task),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
	),

	TP_fast_assign(
		__entry->task_id = task->tk_pid;
		__entry->client_id = task->tk_client->cl_clid;
	),

	TP_printk("task:%u@%u", __entry->task_id, __entry->client_id)
);

TRACE_EVENT(rpcgss_bad_seqno,
	TP_PROTO(
		const struct rpc_task *task,
		u32 expected,
		u32 received
	),

	TP_ARGS(task, expected, received),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(u32, expected)
		__field(u32, received)
	),

	TP_fast_assign(
		__entry->task_id = task->tk_pid;
		__entry->client_id = task->tk_client->cl_clid;
		__entry->expected = expected;
		__entry->received = received;
	),

	TP_printk("task:%u@%u expected seqno %u, received seqno %u",
		__entry->task_id, __entry->client_id,
		__entry->expected, __entry->received)
);

TRACE_EVENT(rpcgss_seqno,
	TP_PROTO(
		const struct rpc_task *task
	),

	TP_ARGS(task),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(u32, xid)
		__field(u32, seqno)
	),

	TP_fast_assign(
		const struct rpc_rqst *rqst = task->tk_rqstp;

		__entry->task_id = task->tk_pid;
		__entry->client_id = task->tk_client->cl_clid;
		__entry->xid = be32_to_cpu(rqst->rq_xid);
		__entry->seqno = rqst->rq_seqno;
	),

	TP_printk("task:%u@%u xid=0x%08x seqno=%u",
		__entry->task_id, __entry->client_id,
		__entry->xid, __entry->seqno)
);

TRACE_EVENT(rpcgss_need_reencode,
	TP_PROTO(
		const struct rpc_task *task,
		u32 seq_xmit,
		bool ret
	),

	TP_ARGS(task, seq_xmit, ret),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(u32, xid)
		__field(u32, seq_xmit)
		__field(u32, seqno)
		__field(bool, ret)
	),

	TP_fast_assign(
		__entry->task_id = task->tk_pid;
		__entry->client_id = task->tk_client->cl_clid;
		__entry->xid = be32_to_cpu(task->tk_rqstp->rq_xid);
		__entry->seq_xmit = seq_xmit;
		__entry->seqno = task->tk_rqstp->rq_seqno;
		__entry->ret = ret;
	),

	TP_printk("task:%u@%u xid=0x%08x rq_seqno=%u seq_xmit=%u reencode %sneeded",
		__entry->task_id, __entry->client_id,
		__entry->xid, __entry->seqno, __entry->seq_xmit,
		__entry->ret ? "" : "un")
);

TRACE_EVENT(rpcgss_update_slack,
	TP_PROTO(
		const struct rpc_task *task,
		const struct rpc_auth *auth
	),

	TP_ARGS(task, auth),

	TP_STRUCT__entry(
		__field(unsigned int, task_id)
		__field(unsigned int, client_id)
		__field(u32, xid)
		__field(const void *, auth)
		__field(unsigned int, rslack)
		__field(unsigned int, ralign)
		__field(unsigned int, verfsize)
	),

	TP_fast_assign(
		__entry->task_id = task->tk_pid;
		__entry->client_id = task->tk_client->cl_clid;
		__entry->xid = be32_to_cpu(task->tk_rqstp->rq_xid);
		__entry->auth = auth;
		__entry->rslack = auth->au_rslack;
		__entry->ralign = auth->au_ralign;
		__entry->verfsize = auth->au_verfsize;
	),

	TP_printk("task:%u@%u xid=0x%08x auth=%p rslack=%u ralign=%u verfsize=%u\n",
		__entry->task_id, __entry->client_id, __entry->xid,
		__entry->auth, __entry->rslack, __entry->ralign,
		__entry->verfsize)
);

DECLARE_EVENT_CLASS(rpcgss_svc_seqno_class,
	TP_PROTO(
		const struct svc_rqst *rqstp,
		u32 seqno
	),

	TP_ARGS(rqstp, seqno),

	TP_STRUCT__entry(
		__field(u32, xid)
		__field(u32, seqno)
	),

	TP_fast_assign(
		__entry->xid = be32_to_cpu(rqstp->rq_xid);
		__entry->seqno = seqno;
	),

	TP_printk("xid=0x%08x seqno=%u",
		__entry->xid, __entry->seqno)
);

#define DEFINE_SVC_SEQNO_EVENT(name)					\
	DEFINE_EVENT(rpcgss_svc_seqno_class, rpcgss_svc_seqno_##name,	\
			TP_PROTO(					\
				const struct svc_rqst *rqstp,		\
				u32 seqno				\
			),						\
			TP_ARGS(rqstp, seqno))

DEFINE_SVC_SEQNO_EVENT(large);
DEFINE_SVC_SEQNO_EVENT(seen);

TRACE_EVENT(rpcgss_svc_seqno_low,
	TP_PROTO(
		const struct svc_rqst *rqstp,
		u32 seqno,
		u32 min,
		u32 max
	),

	TP_ARGS(rqstp, seqno, min, max),

	TP_STRUCT__entry(
		__field(u32, xid)
		__field(u32, seqno)
		__field(u32, min)
		__field(u32, max)
	),

	TP_fast_assign(
		__entry->xid = be32_to_cpu(rqstp->rq_xid);
		__entry->seqno = seqno;
		__entry->min = min;
		__entry->max = max;
	),

	TP_printk("xid=0x%08x seqno=%u window=[%u..%u]",
		__entry->xid, __entry->seqno, __entry->min, __entry->max)
);

/**
 ** gssd upcall related trace events
 **/

TRACE_EVENT(rpcgss_upcall_msg,
	TP_PROTO(
		const char *buf
	),

	TP_ARGS(buf),

	TP_STRUCT__entry(
		__string(msg, buf)
	),

	TP_fast_assign(
		__assign_str(msg, buf);
	),

	TP_printk("msg='%s'", __get_str(msg))
);

TRACE_EVENT(rpcgss_upcall_result,
	TP_PROTO(
		u32 uid,
		int result
	),

	TP_ARGS(uid, result),

	TP_STRUCT__entry(
		__field(u32, uid)
		__field(int, result)

	),

	TP_fast_assign(
		__entry->uid = uid;
		__entry->result = result;
	),

	TP_printk("for uid %u, result=%d", __entry->uid, __entry->result)
);

TRACE_EVENT(rpcgss_context,
	TP_PROTO(
		u32 window_size,
		unsigned long expiry,
		unsigned long now,
		unsigned int timeout,
		unsigned int len,
		const u8 *data
	),

	TP_ARGS(window_size, expiry, now, timeout, len, data),

	TP_STRUCT__entry(
		__field(unsigned long, expiry)
		__field(unsigned long, now)
		__field(unsigned int, timeout)
		__field(u32, window_size)
		__field(int, len)
		__string(acceptor, data)
	),

	TP_fast_assign(
		__entry->expiry = expiry;
		__entry->now = now;
		__entry->timeout = timeout;
		__entry->window_size = window_size;
		__entry->len = len;
		strncpy(__get_str(acceptor), data, len);
	),

	TP_printk("win_size=%u expiry=%lu now=%lu timeout=%u acceptor=%.*s",
		__entry->window_size, __entry->expiry, __entry->now,
		__entry->timeout, __entry->len, __get_str(acceptor))
);


/**
 ** Miscellaneous events
 */

TRACE_DEFINE_ENUM(RPC_AUTH_GSS_KRB5);
TRACE_DEFINE_ENUM(RPC_AUTH_GSS_KRB5I);
TRACE_DEFINE_ENUM(RPC_AUTH_GSS_KRB5P);

#define show_pseudoflavor(x)						\
	__print_symbolic(x,						\
		{ RPC_AUTH_GSS_KRB5, "RPC_AUTH_GSS_KRB5" },		\
		{ RPC_AUTH_GSS_KRB5I, "RPC_AUTH_GSS_KRB5I" },		\
		{ RPC_AUTH_GSS_KRB5P, "RPC_AUTH_GSS_KRB5P" })


TRACE_EVENT(rpcgss_createauth,
	TP_PROTO(
		unsigned int flavor,
		int error
	),

	TP_ARGS(flavor, error),

	TP_STRUCT__entry(
		__field(unsigned int, flavor)
		__field(int, error)

	),

	TP_fast_assign(
		__entry->flavor = flavor;
		__entry->error = error;
	),

	TP_printk("flavor=%s error=%d",
		show_pseudoflavor(__entry->flavor), __entry->error)
);

TRACE_EVENT(rpcgss_oid_to_mech,
	TP_PROTO(
		const char *oid
	),

	TP_ARGS(oid),

	TP_STRUCT__entry(
		__string(oid, oid)
	),

	TP_fast_assign(
		__assign_str(oid, oid);
	),

	TP_printk("mech for oid %s was not found", __get_str(oid))
);

#endif	/* _TRACE_RPCGSS_H */

#include <trace/define_trace.h>
