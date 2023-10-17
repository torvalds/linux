/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM handshake

#if !defined(_TRACE_HANDSHAKE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HANDSHAKE_H

#include <linux/net.h>
#include <net/tls_prot.h>
#include <linux/tracepoint.h>
#include <trace/events/net_probe_common.h>

#define TLS_RECORD_TYPE_LIST \
	record_type(CHANGE_CIPHER_SPEC) \
	record_type(ALERT) \
	record_type(HANDSHAKE) \
	record_type(DATA) \
	record_type(HEARTBEAT) \
	record_type(TLS12_CID) \
	record_type_end(ACK)

#undef record_type
#undef record_type_end
#define record_type(x)		TRACE_DEFINE_ENUM(TLS_RECORD_TYPE_##x);
#define record_type_end(x)	TRACE_DEFINE_ENUM(TLS_RECORD_TYPE_##x);

TLS_RECORD_TYPE_LIST

#undef record_type
#undef record_type_end
#define record_type(x)		{ TLS_RECORD_TYPE_##x, #x },
#define record_type_end(x)	{ TLS_RECORD_TYPE_##x, #x }

#define show_tls_content_type(type) \
	__print_symbolic(type, TLS_RECORD_TYPE_LIST)

TRACE_DEFINE_ENUM(TLS_ALERT_LEVEL_WARNING);
TRACE_DEFINE_ENUM(TLS_ALERT_LEVEL_FATAL);

#define show_tls_alert_level(level) \
	__print_symbolic(level, \
		{ TLS_ALERT_LEVEL_WARNING,	"Warning" }, \
		{ TLS_ALERT_LEVEL_FATAL,	"Fatal" })

#define TLS_ALERT_DESCRIPTION_LIST \
	alert_description(CLOSE_NOTIFY) \
	alert_description(UNEXPECTED_MESSAGE) \
	alert_description(BAD_RECORD_MAC) \
	alert_description(RECORD_OVERFLOW) \
	alert_description(HANDSHAKE_FAILURE) \
	alert_description(BAD_CERTIFICATE) \
	alert_description(UNSUPPORTED_CERTIFICATE) \
	alert_description(CERTIFICATE_REVOKED) \
	alert_description(CERTIFICATE_EXPIRED) \
	alert_description(CERTIFICATE_UNKNOWN) \
	alert_description(ILLEGAL_PARAMETER) \
	alert_description(UNKNOWN_CA) \
	alert_description(ACCESS_DENIED) \
	alert_description(DECODE_ERROR) \
	alert_description(DECRYPT_ERROR) \
	alert_description(TOO_MANY_CIDS_REQUESTED) \
	alert_description(PROTOCOL_VERSION) \
	alert_description(INSUFFICIENT_SECURITY) \
	alert_description(INTERNAL_ERROR) \
	alert_description(INAPPROPRIATE_FALLBACK) \
	alert_description(USER_CANCELED) \
	alert_description(MISSING_EXTENSION) \
	alert_description(UNSUPPORTED_EXTENSION) \
	alert_description(UNRECOGNIZED_NAME) \
	alert_description(BAD_CERTIFICATE_STATUS_RESPONSE) \
	alert_description(UNKNOWN_PSK_IDENTITY) \
	alert_description(CERTIFICATE_REQUIRED) \
	alert_description_end(NO_APPLICATION_PROTOCOL)

#undef alert_description
#undef alert_description_end
#define alert_description(x)		TRACE_DEFINE_ENUM(TLS_ALERT_DESC_##x);
#define alert_description_end(x)	TRACE_DEFINE_ENUM(TLS_ALERT_DESC_##x);

TLS_ALERT_DESCRIPTION_LIST

#undef alert_description
#undef alert_description_end
#define alert_description(x)		{ TLS_ALERT_DESC_##x, #x },
#define alert_description_end(x)	{ TLS_ALERT_DESC_##x, #x }

#define show_tls_alert_description(desc) \
	__print_symbolic(desc, TLS_ALERT_DESCRIPTION_LIST)

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

DECLARE_EVENT_CLASS(handshake_alert_class,
	TP_PROTO(
		const struct sock *sk,
		unsigned char level,
		unsigned char description
	),
	TP_ARGS(sk, level, description),
	TP_STRUCT__entry(
		/* sockaddr_in6 is always bigger than sockaddr_in */
		__array(__u8, saddr, sizeof(struct sockaddr_in6))
		__array(__u8, daddr, sizeof(struct sockaddr_in6))
		__field(unsigned int, netns_ino)
		__field(unsigned long, level)
		__field(unsigned long, description)
	),
	TP_fast_assign(
		const struct inet_sock *inet = inet_sk(sk);

		memset(__entry->saddr, 0, sizeof(struct sockaddr_in6));
		memset(__entry->daddr, 0, sizeof(struct sockaddr_in6));
		TP_STORE_ADDR_PORTS(__entry, inet, sk);

		__entry->netns_ino = sock_net(sk)->ns.inum;
		__entry->level = level;
		__entry->description = description;
	),
	TP_printk("src=%pISpc dest=%pISpc %s: %s",
		__entry->saddr, __entry->daddr,
		show_tls_alert_level(__entry->level),
		show_tls_alert_description(__entry->description)
	)
);
#define DEFINE_HANDSHAKE_ALERT(name)				\
	DEFINE_EVENT(handshake_alert_class, name,		\
		TP_PROTO(					\
			const struct sock *sk,			\
			unsigned char level,			\
			unsigned char description		\
		),						\
		TP_ARGS(sk, level, description))


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

/*
 * TLS Record events
 */

TRACE_EVENT(tls_contenttype,
	TP_PROTO(
		const struct sock *sk,
		unsigned char type
	),
	TP_ARGS(sk, type),
	TP_STRUCT__entry(
		/* sockaddr_in6 is always bigger than sockaddr_in */
		__array(__u8, saddr, sizeof(struct sockaddr_in6))
		__array(__u8, daddr, sizeof(struct sockaddr_in6))
		__field(unsigned int, netns_ino)
		__field(unsigned long, type)
	),
	TP_fast_assign(
		const struct inet_sock *inet = inet_sk(sk);

		memset(__entry->saddr, 0, sizeof(struct sockaddr_in6));
		memset(__entry->daddr, 0, sizeof(struct sockaddr_in6));
		TP_STORE_ADDR_PORTS(__entry, inet, sk);

		__entry->netns_ino = sock_net(sk)->ns.inum;
		__entry->type = type;
	),
	TP_printk("src=%pISpc dest=%pISpc %s",
		__entry->saddr, __entry->daddr,
		show_tls_content_type(__entry->type)
	)
);

/*
 * TLS Alert events
 */

DEFINE_HANDSHAKE_ALERT(tls_alert_send);
DEFINE_HANDSHAKE_ALERT(tls_alert_recv);

#endif /* _TRACE_HANDSHAKE_H */

#include <trace/define_trace.h>
