/* Public domain. */

#ifndef _LINUX_TRACEPOINT_H
#define _LINUX_TRACEPOINT_H

#include <linux/types.h>

#define TP_PROTO(x...) x

#define DEFINE_EVENT(template, name, proto, args) \
static inline void trace_##name(proto) {} \
static inline bool trace_##name##_enabled(void) { return false; }

#define DEFINE_EVENT_PRINT(template, name, proto, args, print) \
static inline void trace_##name(proto) {}

#define TRACE_EVENT(name, proto, args, tstruct, assign, print) \
static inline void trace_##name(proto) {} \
static inline bool trace_##name##_enabled(void) { return false; }

#define TRACE_EVENT_CONDITION(name, proto, args, cond, tstruct, assign, print) \
static inline void trace_##name(proto) {}

#define DECLARE_EVENT_CLASS(name, proto, args, tstruct, assign, print)

#endif
