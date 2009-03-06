#ifndef _TRACE_POWER_H
#define _TRACE_POWER_H

#include <linux/ktime.h>
#include <linux/tracepoint.h>

enum {
	POWER_NONE = 0,
	POWER_CSTATE = 1,
	POWER_PSTATE = 2,
};

struct power_trace {
	ktime_t			stamp;
	ktime_t			end;
	int			type;
	int			state;
};

DECLARE_TRACE(power_start,
	TPPROTO(struct power_trace *it, unsigned int type, unsigned int state),
		TPARGS(it, type, state));

DECLARE_TRACE(power_mark,
	TPPROTO(struct power_trace *it, unsigned int type, unsigned int state),
		TPARGS(it, type, state));

DECLARE_TRACE(power_end,
	TPPROTO(struct power_trace *it),
		TPARGS(it));

#endif /* _TRACE_POWER_H */
