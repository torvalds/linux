#undef TRACE_SYSTEM
#define TRACE_SYSTEM hda_intel
#define TRACE_INCLUDE_FILE hda_intel_trace

#if !defined(_TRACE_HDA_INTEL_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HDA_INTEL_H

#include <linux/tracepoint.h>

struct azx;
struct azx_dev;

TRACE_EVENT(azx_pcm_trigger,

	TP_PROTO(struct azx *chip, struct azx_dev *dev, int cmd),

	TP_ARGS(chip, dev, cmd),

	TP_STRUCT__entry(
		__field( int, card )
		__field( int, idx )
		__field( int, cmd )
	),

	TP_fast_assign(
		__entry->card = (chip)->card->number;
		__entry->idx = (dev)->index;
		__entry->cmd = cmd;
	),

	TP_printk("[%d:%d] cmd=%d", __entry->card, __entry->idx, __entry->cmd)
);

TRACE_EVENT(azx_get_position,

    TP_PROTO(struct azx *chip, struct azx_dev *dev, unsigned int pos, unsigned int delay),

	    TP_ARGS(chip, dev, pos, delay),

	TP_STRUCT__entry(
		__field( int, card )
		__field( int, idx )
		__field( unsigned int, pos )
		__field( unsigned int, delay )
	),

	TP_fast_assign(
		__entry->card = (chip)->card->number;
		__entry->idx = (dev)->index;
		__entry->pos = pos;
		__entry->delay = delay;
	),

	TP_printk("[%d:%d] pos=%u, delay=%u", __entry->card, __entry->idx, __entry->pos, __entry->delay)
);

#endif /* _TRACE_HDA_INTEL_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#include <trace/define_trace.h>
