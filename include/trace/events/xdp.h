#undef TRACE_SYSTEM
#define TRACE_SYSTEM xdp

#if !defined(_TRACE_XDP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_XDP_H

#include <linux/netdevice.h>
#include <linux/filter.h>
#include <linux/tracepoint.h>

#define __XDP_ACT_MAP(FN)	\
	FN(ABORTED)		\
	FN(DROP)		\
	FN(PASS)		\
	FN(TX)

#define __XDP_ACT_TP_FN(x)	\
	TRACE_DEFINE_ENUM(XDP_##x);
#define __XDP_ACT_SYM_FN(x)	\
	{ XDP_##x, #x },
#define __XDP_ACT_SYM_TAB	\
	__XDP_ACT_MAP(__XDP_ACT_SYM_FN) { -1, 0 }
__XDP_ACT_MAP(__XDP_ACT_TP_FN)

TRACE_EVENT(xdp_exception,

	TP_PROTO(const struct net_device *dev,
		 const struct bpf_prog *xdp, u32 act),

	TP_ARGS(dev, xdp, act),

	TP_STRUCT__entry(
		__string(name, dev->name)
		__array(u8, prog_tag, 8)
		__field(u32, act)
	),

	TP_fast_assign(
		BUILD_BUG_ON(sizeof(__entry->prog_tag) != sizeof(xdp->tag));
		memcpy(__entry->prog_tag, xdp->tag, sizeof(xdp->tag));
		__assign_str(name, dev->name);
		__entry->act = act;
	),

	TP_printk("prog=%s device=%s action=%s",
		  __print_hex_str(__entry->prog_tag, 8),
		  __get_str(name),
		  __print_symbolic(__entry->act, __XDP_ACT_SYM_TAB))
);

#endif /* _TRACE_XDP_H */

#include <trace/define_trace.h>
