#ifndef _TRACE_POWER_H
#define _TRACE_POWER_H

#include <linux/ktime.h>

enum {
	POWER_NONE = 0,
	POWER_CSTATE = 1,
	POWER_PSTATE = 2,
};

struct power_trace {
#ifdef CONFIG_POWER_TRACER
	ktime_t			stamp;
	ktime_t			end;
	int			type;
	int			state;
#endif
};

#ifdef CONFIG_POWER_TRACER
extern void trace_power_start(struct power_trace *it, unsigned int type,
					unsigned int state);
extern void trace_power_mark(struct power_trace *it, unsigned int type,
					unsigned int state);
extern void trace_power_end(struct power_trace *it);
#else
static inline void trace_power_start(struct power_trace *it, unsigned int type,
					unsigned int state) { }
static inline void trace_power_mark(struct power_trace *it, unsigned int type,
					unsigned int state) { }
static inline void trace_power_end(struct power_trace *it) { }
#endif

#endif /* _TRACE_POWER_H */
