#undef TRACE_SYSTEM
#define TRACE_SYSTEM hda

#if !defined(__HDAC_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define __HDAC_TRACE_H

#include <linux/tracepoint.h>
#include <linux/device.h>
#include <sound/hdaudio.h>

#ifndef HDAC_MSG_MAX
#define HDAC_MSG_MAX	500
#endif

struct hdac_bus;
struct hdac_codec;

TRACE_EVENT(hda_send_cmd,
	TP_PROTO(struct hdac_bus *bus, unsigned int cmd),
	TP_ARGS(bus, cmd),
	TP_STRUCT__entry(__dynamic_array(char, msg, HDAC_MSG_MAX)),
	TP_fast_assign(
		snprintf(__get_str(msg), HDAC_MSG_MAX,
			 "[%s:%d] val=0x%08x",
			 dev_name((bus)->dev), (cmd) >> 28, cmd);
	),
	TP_printk("%s", __get_str(msg))
);

TRACE_EVENT(hda_get_response,
	TP_PROTO(struct hdac_bus *bus, unsigned int addr, unsigned int res),
	TP_ARGS(bus, addr, res),
	TP_STRUCT__entry(__dynamic_array(char, msg, HDAC_MSG_MAX)),
	TP_fast_assign(
		snprintf(__get_str(msg), HDAC_MSG_MAX,
			 "[%s:%d] val=0x%08x",
			 dev_name((bus)->dev), addr, res);
	),
	TP_printk("%s", __get_str(msg))
);

TRACE_EVENT(hda_unsol_event,
	TP_PROTO(struct hdac_bus *bus, u32 res, u32 res_ex),
	TP_ARGS(bus, res, res_ex),
	TP_STRUCT__entry(__dynamic_array(char, msg, HDAC_MSG_MAX)),
	TP_fast_assign(
		snprintf(__get_str(msg), HDAC_MSG_MAX,
			 "[%s:%d] res=0x%08x, res_ex=0x%08x",
			 dev_name((bus)->dev), res_ex & 0x0f, res, res_ex);
	),
	TP_printk("%s", __get_str(msg))
);
#endif /* __HDAC_TRACE_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

#include <trace/define_trace.h>
