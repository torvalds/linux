#undef TRACE_SYSTEM
#define TRACE_SYSTEM hda
#define TRACE_INCLUDE_FILE hda_trace

#if !defined(_TRACE_HDA_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HDA_H

#include <linux/tracepoint.h>

struct hda_bus;
struct hda_codec;

DECLARE_EVENT_CLASS(hda_cmd,

	TP_PROTO(struct hda_codec *codec, unsigned int val),

	TP_ARGS(codec, val),

	TP_STRUCT__entry(
		__field( unsigned int, card )
		__field( unsigned int, addr )
		__field( unsigned int, val )
	),

	TP_fast_assign(
		__entry->card = (codec)->card->number;
		__entry->addr = (codec)->addr;
		__entry->val = (val);
	),

	TP_printk("[%d:%d] val=%x", __entry->card, __entry->addr, __entry->val)
);

DEFINE_EVENT(hda_cmd, hda_send_cmd,
	TP_PROTO(struct hda_codec *codec, unsigned int val),
	TP_ARGS(codec, val)
);

DEFINE_EVENT(hda_cmd, hda_get_response,
	TP_PROTO(struct hda_codec *codec, unsigned int val),
	TP_ARGS(codec, val)
);

TRACE_EVENT(hda_bus_reset,

	TP_PROTO(struct hda_bus *bus),

	TP_ARGS(bus),

	TP_STRUCT__entry(
		__field( unsigned int, card )
	),

	TP_fast_assign(
		__entry->card = (bus)->card->number;
	),

	TP_printk("[%d]", __entry->card)
);

#ifdef CONFIG_PM
DECLARE_EVENT_CLASS(hda_power,

	TP_PROTO(struct hda_codec *codec),

	TP_ARGS(codec),

	TP_STRUCT__entry(
		__field( unsigned int, card )
		__field( unsigned int, addr )
	),

	TP_fast_assign(
		__entry->card = (codec)->card->number;
		__entry->addr = (codec)->addr;
	),

	TP_printk("[%d:%d]", __entry->card, __entry->addr)
);

DEFINE_EVENT(hda_power, hda_power_down,
	TP_PROTO(struct hda_codec *codec),
	TP_ARGS(codec)
);

DEFINE_EVENT(hda_power, hda_power_up,
	TP_PROTO(struct hda_codec *codec),
	TP_ARGS(codec)
);
#endif /* CONFIG_PM */

TRACE_EVENT(hda_unsol_event,

	TP_PROTO(struct hda_bus *bus, u32 res, u32 res_ex),

	TP_ARGS(bus, res, res_ex),

	TP_STRUCT__entry(
		__field( unsigned int, card )
		__field( u32, res )
		__field( u32, res_ex )
	),

	TP_fast_assign(
		__entry->card = (bus)->card->number;
		__entry->res = res;
		__entry->res_ex = res_ex;
	),

	TP_printk("[%d] res=%x, res_ex=%x", __entry->card,
		  __entry->res, __entry->res_ex)
);

#endif /* _TRACE_HDA_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#include <trace/define_trace.h>
