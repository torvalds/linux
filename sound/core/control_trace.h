/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM snd_ctl

#if !defined(_TRACE_SND_CTL_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SND_CTL_H

#include <linux/tracepoint.h>
#include <uapi/sound/asound.h>

TRACE_EVENT(snd_ctl_put,

	TP_PROTO(struct snd_ctl_elem_id *id, const char *iname, unsigned int card,
		 int expected, int actual),

	TP_ARGS(id, iname, card, expected, actual),

	TP_STRUCT__entry(
		__field(unsigned int,	numid)
		__string(iname,		iname)
		__string(kname,		id->name)
		__field(unsigned int,	index)
		__field(unsigned int,	device)
		__field(unsigned int,	subdevice)
		__field(unsigned int,	card)
		__field(int,		expected)
		__field(int,		actual)
	),

	TP_fast_assign(
		__entry->numid = id->numid;
		__assign_str(iname);
		__assign_str(kname);
		__entry->index = id->index;
		__entry->device = id->device;
		__entry->subdevice = id->subdevice;
		__entry->card = card;
		__entry->expected = expected;
		__entry->actual = actual;
	),

	TP_printk("%s: expected=%d, actual=%d for ctl numid=%d, iface=%s, name='%s', index=%d, device=%d, subdevice=%d, card=%d\n",
		  __entry->expected == __entry->actual ? "success" : "fail",
		  __entry->expected, __entry->actual, __entry->numid,
		  __get_str(iname), __get_str(kname), __entry->index,
		  __entry->device, __entry->subdevice, __entry->card)
);

#endif /* _TRACE_SND_CTL_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE control_trace
#include <trace/define_trace.h>
