#undef TRACE_SYSTEM
#define TRACE_SYSTEM xen

#if !defined(_TRACE_XEN_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_XEN_H

#include <linux/tracepoint.h>
#include <asm/paravirt_types.h>
#include <asm/xen/trace_types.h>

/* Multicalls */

TRACE_EVENT(xen_mc_batch,
	    TP_PROTO(enum paravirt_lazy_mode mode),
	    TP_ARGS(mode),
	    TP_STRUCT__entry(
		    __field(enum paravirt_lazy_mode, mode)
		    ),
	    TP_fast_assign(__entry->mode = mode),
	    TP_printk("start batch LAZY_%s",
		      (__entry->mode == PARAVIRT_LAZY_MMU) ? "MMU" :
		      (__entry->mode == PARAVIRT_LAZY_CPU) ? "CPU" : "NONE")
	);

TRACE_EVENT(xen_mc_issue,
	    TP_PROTO(enum paravirt_lazy_mode mode),
	    TP_ARGS(mode),
	    TP_STRUCT__entry(
		    __field(enum paravirt_lazy_mode, mode)
		    ),
	    TP_fast_assign(__entry->mode = mode),
	    TP_printk("issue mode LAZY_%s",
		      (__entry->mode == PARAVIRT_LAZY_MMU) ? "MMU" :
		      (__entry->mode == PARAVIRT_LAZY_CPU) ? "CPU" : "NONE")
	);

TRACE_EVENT(xen_mc_entry,
	    TP_PROTO(struct multicall_entry *mc, unsigned nargs),
	    TP_ARGS(mc, nargs),
	    TP_STRUCT__entry(
		    __field(unsigned int, op)
		    __field(unsigned int, nargs)
		    __array(unsigned long, args, 6)
		    ),
	    TP_fast_assign(__entry->op = mc->op;
			   __entry->nargs = nargs;
			   memcpy(__entry->args, mc->args, sizeof(unsigned long) * nargs);
			   memset(__entry->args + nargs, 0, sizeof(unsigned long) * (6 - nargs));
		    ),
	    TP_printk("op %u%s args [%lx, %lx, %lx, %lx, %lx, %lx]",
		      __entry->op, xen_hypercall_name(__entry->op),
		      __entry->args[0], __entry->args[1], __entry->args[2],
		      __entry->args[3], __entry->args[4], __entry->args[5])
	);

TRACE_EVENT(xen_mc_entry_alloc,
	    TP_PROTO(size_t args),
	    TP_ARGS(args),
	    TP_STRUCT__entry(
		    __field(size_t, args)
		    ),
	    TP_fast_assign(__entry->args = args),
	    TP_printk("alloc entry %zu arg bytes", __entry->args)
	);

TRACE_EVENT(xen_mc_callback,
	    TP_PROTO(xen_mc_callback_fn_t fn, void *data),
	    TP_ARGS(fn, data),
	    TP_STRUCT__entry(
		    __field(xen_mc_callback_fn_t, fn)
		    __field(void *, data)
		    ),
	    TP_fast_assign(
		    __entry->fn = fn;
		    __entry->data = data;
		    ),
	    TP_printk("callback %pf, data %p",
		      __entry->fn, __entry->data)
	);

TRACE_EVENT(xen_mc_flush_reason,
	    TP_PROTO(enum xen_mc_flush_reason reason),
	    TP_ARGS(reason),
	    TP_STRUCT__entry(
		    __field(enum xen_mc_flush_reason, reason)
		    ),
	    TP_fast_assign(__entry->reason = reason),
	    TP_printk("flush reason %s",
		      (__entry->reason == XEN_MC_FL_NONE) ? "NONE" :
		      (__entry->reason == XEN_MC_FL_BATCH) ? "BATCH" :
		      (__entry->reason == XEN_MC_FL_ARGS) ? "ARGS" :
		      (__entry->reason == XEN_MC_FL_CALLBACK) ? "CALLBACK" : "??")
	);

TRACE_EVENT(xen_mc_flush,
	    TP_PROTO(unsigned mcidx, unsigned argidx, unsigned cbidx),
	    TP_ARGS(mcidx, argidx, cbidx),
	    TP_STRUCT__entry(
		    __field(unsigned, mcidx)
		    __field(unsigned, argidx)
		    __field(unsigned, cbidx)
		    ),
	    TP_fast_assign(__entry->mcidx = mcidx;
			   __entry->argidx = argidx;
			   __entry->cbidx = cbidx),
	    TP_printk("flushing %u hypercalls, %u arg bytes, %u callbacks",
		      __entry->mcidx, __entry->argidx, __entry->cbidx)
	);

TRACE_EVENT(xen_mc_extend_args,
	    TP_PROTO(unsigned long op, size_t args, enum xen_mc_extend_args res),
	    TP_ARGS(op, args, res),
	    TP_STRUCT__entry(
		    __field(unsigned int, op)
		    __field(size_t, args)
		    __field(enum xen_mc_extend_args, res)
		    ),
	    TP_fast_assign(__entry->op = op;
			   __entry->args = args;
			   __entry->res = res),
	    TP_printk("extending op %u%s by %zu bytes res %s",
		      __entry->op, xen_hypercall_name(__entry->op),
		      __entry->args,
		      __entry->res == XEN_MC_XE_OK ? "OK" :
		      __entry->res == XEN_MC_XE_BAD_OP ? "BAD_OP" :
		      __entry->res == XEN_MC_XE_NO_SPACE ? "NO_SPACE" : "???")
	);
#endif /*  _TRACE_XEN_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
