/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM dccp

#if !defined(_TRACE_DCCP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_DCCP_H

#include <net/sock.h>
#include "dccp.h"
#include "ccids/ccid3.h"
#include <linux/tracepoint.h>
#include <trace/events/net_probe_common.h>

TRACE_EVENT(dccp_probe,

	TP_PROTO(struct sock *sk, size_t size),

	TP_ARGS(sk, size),

	TP_STRUCT__entry(
		/* sockaddr_in6 is always bigger than sockaddr_in */
		__array(__u8, saddr, sizeof(struct sockaddr_in6))
		__array(__u8, daddr, sizeof(struct sockaddr_in6))
		__field(__u16, sport)
		__field(__u16, dport)
		__field(__u16, size)
		__field(__u16, tx_s)
		__field(__u32, tx_rtt)
		__field(__u32, tx_p)
		__field(__u32, tx_x_calc)
		__field(__u64, tx_x_recv)
		__field(__u64, tx_x)
		__field(__u32, tx_t_ipi)
	),

	TP_fast_assign(
		const struct inet_sock *inet = inet_sk(sk);
		struct ccid3_hc_tx_sock *hc = NULL;

		if (ccid_get_current_tx_ccid(dccp_sk(sk)) == DCCPC_CCID3)
			hc = ccid3_hc_tx_sk(sk);

		memset(__entry->saddr, 0, sizeof(struct sockaddr_in6));
		memset(__entry->daddr, 0, sizeof(struct sockaddr_in6));

		TP_STORE_ADDR_PORTS(__entry, inet, sk);

		/* For filtering use */
		__entry->sport = ntohs(inet->inet_sport);
		__entry->dport = ntohs(inet->inet_dport);

		__entry->size = size;
		if (hc) {
			__entry->tx_s = hc->tx_s;
			__entry->tx_rtt = hc->tx_rtt;
			__entry->tx_p = hc->tx_p;
			__entry->tx_x_calc = hc->tx_x_calc;
			__entry->tx_x_recv = hc->tx_x_recv >> 6;
			__entry->tx_x = hc->tx_x >> 6;
			__entry->tx_t_ipi = hc->tx_t_ipi;
		} else {
			__entry->tx_s = 0;
			memset(&__entry->tx_rtt, 0, (void *)&__entry->tx_t_ipi -
			       (void *)&__entry->tx_rtt +
			       sizeof(__entry->tx_t_ipi));
		}
	),

	TP_printk("src=%pISpc dest=%pISpc size=%d tx_s=%d tx_rtt=%d "
		  "tx_p=%d tx_x_calc=%u tx_x_recv=%llu tx_x=%llu tx_t_ipi=%d",
		  __entry->saddr, __entry->daddr, __entry->size,
		  __entry->tx_s, __entry->tx_rtt, __entry->tx_p,
		  __entry->tx_x_calc, __entry->tx_x_recv, __entry->tx_x,
		  __entry->tx_t_ipi)
);

#endif /* _TRACE_TCP_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>
