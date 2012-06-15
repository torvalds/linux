#undef TRACE_SYSTEM
#define TRACE_SYSTEM printk

#if !defined(_TRACE_PRINTK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_PRINTK_H

#include <linux/tracepoint.h>

TRACE_EVENT_CONDITION(console,
	TP_PROTO(const char *log_buf, unsigned start, unsigned end,
		 unsigned log_buf_len),

	TP_ARGS(log_buf, start, end, log_buf_len),

	TP_CONDITION(start != end),

	TP_STRUCT__entry(
		__dynamic_array(char, msg, end - start + 1)
	),

	TP_fast_assign(
		if ((start & (log_buf_len - 1)) > (end & (log_buf_len - 1))) {
			memcpy(__get_dynamic_array(msg),
			       log_buf + (start & (log_buf_len - 1)),
			       log_buf_len - (start & (log_buf_len - 1)));
			memcpy((char *)__get_dynamic_array(msg) +
			       log_buf_len - (start & (log_buf_len - 1)),
			       log_buf, end & (log_buf_len - 1));
		} else
			memcpy(__get_dynamic_array(msg),
			       log_buf + (start & (log_buf_len - 1)),
			       end - start);
		((char *)__get_dynamic_array(msg))[end - start] = 0;
	),

	TP_printk("%s", __get_str(msg))
);
#endif /* _TRACE_PRINTK_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
