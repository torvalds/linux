/* SPDX-License-Identifier: GPL-2.0 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM smc

#if !defined(_TRACE_SMC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SMC_H

#include <linux/ipv6.h>
#include <linux/tcp.h>
#include <linux/tracepoint.h>
#include <net/ipv6.h>
#include "smc.h"
#include "smc_core.h"

TRACE_EVENT(smc_switch_to_fallback,

	    TP_PROTO(const struct smc_sock *smc, int fallback_rsn),

	    TP_ARGS(smc, fallback_rsn),

	    TP_STRUCT__entry(
			     __field(const void *, sk)
			     __field(const void *, clcsk)
			     __field(int, fallback_rsn)
	    ),

	    TP_fast_assign(
			   const struct sock *sk = &smc->sk;
			   const struct sock *clcsk = smc->clcsock->sk;

			   __entry->sk = sk;
			   __entry->clcsk = clcsk;
			   __entry->fallback_rsn = fallback_rsn;
	    ),

	    TP_printk("sk=%p clcsk=%p fallback_rsn=%d",
		      __entry->sk, __entry->clcsk, __entry->fallback_rsn)
);

DECLARE_EVENT_CLASS(smc_msg_event,

		    TP_PROTO(const struct smc_sock *smc, size_t len),

		    TP_ARGS(smc, len),

		    TP_STRUCT__entry(
				     __field(const void *, smc)
				     __field(size_t, len)
				     __string(name, smc->conn.lnk->ibname)
		    ),

		    TP_fast_assign(
				   __entry->smc = smc;
				   __entry->len = len;
				   __assign_str(name, smc->conn.lnk->ibname);
		    ),

		    TP_printk("smc=%p len=%zu dev=%s",
			      __entry->smc, __entry->len,
			      __get_str(name))
);

DEFINE_EVENT(smc_msg_event, smc_tx_sendmsg,

	     TP_PROTO(const struct smc_sock *smc, size_t len),

	     TP_ARGS(smc, len)
);

DEFINE_EVENT(smc_msg_event, smc_rx_recvmsg,

	     TP_PROTO(const struct smc_sock *smc, size_t len),

	     TP_ARGS(smc, len)
);

#endif /* _TRACE_SMC_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE smc_tracepoint

#include <trace/define_trace.h>
