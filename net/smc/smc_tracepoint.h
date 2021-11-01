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

#endif /* _TRACE_SMC_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE smc_tracepoint

#include <trace/define_trace.h>
