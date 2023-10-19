/* SPDX-License-Identifier: GPL-2.0 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM mctp

#if !defined(_TRACE_MCTP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_MCTP_H

#include <linux/tracepoint.h>

#ifndef __TRACE_MCTP_ENUMS
#define __TRACE_MCTP_ENUMS
enum {
	MCTP_TRACE_KEY_TIMEOUT,
	MCTP_TRACE_KEY_REPLIED,
	MCTP_TRACE_KEY_INVALIDATED,
	MCTP_TRACE_KEY_CLOSED,
	MCTP_TRACE_KEY_DROPPED,
};
#endif /* __TRACE_MCTP_ENUMS */

TRACE_DEFINE_ENUM(MCTP_TRACE_KEY_TIMEOUT);
TRACE_DEFINE_ENUM(MCTP_TRACE_KEY_REPLIED);
TRACE_DEFINE_ENUM(MCTP_TRACE_KEY_INVALIDATED);
TRACE_DEFINE_ENUM(MCTP_TRACE_KEY_CLOSED);
TRACE_DEFINE_ENUM(MCTP_TRACE_KEY_DROPPED);

TRACE_EVENT(mctp_key_acquire,
	TP_PROTO(const struct mctp_sk_key *key),
	TP_ARGS(key),
	TP_STRUCT__entry(
		__field(__u8,	paddr)
		__field(__u8,	laddr)
		__field(__u8,	tag)
	),
	TP_fast_assign(
		__entry->paddr = key->peer_addr;
		__entry->laddr = key->local_addr;
		__entry->tag = key->tag;
	),
	TP_printk("local %d, peer %d, tag %1x",
		__entry->laddr,
		__entry->paddr,
		__entry->tag
	)
);

TRACE_EVENT(mctp_key_release,
	TP_PROTO(const struct mctp_sk_key *key, int reason),
	TP_ARGS(key, reason),
	TP_STRUCT__entry(
		__field(__u8,	paddr)
		__field(__u8,	laddr)
		__field(__u8,	tag)
		__field(int,	reason)
	),
	TP_fast_assign(
		__entry->paddr = key->peer_addr;
		__entry->laddr = key->local_addr;
		__entry->tag = key->tag;
		__entry->reason = reason;
	),
	TP_printk("local %d, peer %d, tag %1x %s",
		__entry->laddr,
		__entry->paddr,
		__entry->tag,
		__print_symbolic(__entry->reason,
				 { MCTP_TRACE_KEY_TIMEOUT, "timeout" },
				 { MCTP_TRACE_KEY_REPLIED, "replied" },
				 { MCTP_TRACE_KEY_INVALIDATED, "invalidated" },
				 { MCTP_TRACE_KEY_CLOSED, "closed" },
				 { MCTP_TRACE_KEY_DROPPED, "dropped" })
	)
);

#endif

#include <trace/define_trace.h>
