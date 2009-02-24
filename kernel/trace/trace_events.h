#ifndef _LINUX_KERNEL_TRACE_EVENTS_H
#define _LINUX_KERNEL_TRACE_EVENTS_H

#include <linux/ftrace.h>
#include "trace.h"

struct ftrace_event_call {
	char		*name;
	int		enabled;
	int		(*regfunc)(void);
	void		(*unregfunc)(void);
};


#undef TPFMT
#define TPFMT(fmt, args...)	fmt "\n", ##args

#undef DEFINE_TRACE_FMT
#define DEFINE_TRACE_FMT(call, proto, args, fmt)			\
static void ftrace_event_##call(proto)					\
{									\
	event_trace_printk(_RET_IP_, "(" #call ") " fmt);		\
}									\
									\
static int ftrace_reg_event_##call(void)				\
{									\
	int ret;							\
									\
	ret = register_trace_##call(ftrace_event_##call);		\
	if (!ret)							\
		pr_info("event trace: Could not activate trace point "	\
			"probe to " #call);				\
	return ret;							\
}									\
									\
static void ftrace_unreg_event_##call(void)				\
{									\
	unregister_trace_##call(ftrace_event_##call);			\
}									\
									\
static struct ftrace_event_call __used					\
__attribute__((section("_ftrace_events"))) event_##call = {		\
	.name 			= #call,				\
	.regfunc		= ftrace_reg_event_##call,		\
	.unregfunc		= ftrace_unreg_event_##call,		\
}

void event_trace_printk(unsigned long ip, const char *fmt, ...);
extern unsigned long __start_ftrace_events[];
extern unsigned long __stop_ftrace_events[];

#endif /* _LINUX_KERNEL_TRACE_EVENTS_H */
