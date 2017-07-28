#undef TRACE_SYSTEM
#define TRACE_SYSTEM snd_pcm

#if !defined(_PCM_PARAMS_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _PCM_PARAMS_TRACE_H

#include <linux/tracepoint.h>

#define HW_PARAM_ENTRY(param) {SNDRV_PCM_HW_PARAM_##param, #param}
#define hw_param_labels			\
	HW_PARAM_ENTRY(ACCESS),		\
	HW_PARAM_ENTRY(FORMAT),		\
	HW_PARAM_ENTRY(SUBFORMAT),	\
	HW_PARAM_ENTRY(SAMPLE_BITS),	\
	HW_PARAM_ENTRY(FRAME_BITS),	\
	HW_PARAM_ENTRY(CHANNELS),	\
	HW_PARAM_ENTRY(RATE),		\
	HW_PARAM_ENTRY(PERIOD_TIME),	\
	HW_PARAM_ENTRY(PERIOD_SIZE),	\
	HW_PARAM_ENTRY(PERIOD_BYTES),	\
	HW_PARAM_ENTRY(PERIODS),	\
	HW_PARAM_ENTRY(BUFFER_TIME),	\
	HW_PARAM_ENTRY(BUFFER_SIZE),	\
	HW_PARAM_ENTRY(BUFFER_BYTES),	\
	HW_PARAM_ENTRY(TICK_TIME)

TRACE_EVENT(hw_mask_param,
	TP_PROTO(struct snd_pcm_substream *substream, snd_pcm_hw_param_t type, int index, const struct snd_mask *prev, const struct snd_mask *curr),
	TP_ARGS(substream, type, index, prev, curr),
	TP_STRUCT__entry(
		__field(int, card)
		__field(int, device)
		__field(int, subdevice)
		__field(int, direction)
		__field(snd_pcm_hw_param_t, type)
		__field(int, index)
		__field(int, total)
		__array(__u32, prev_bits, 8)
		__array(__u32, curr_bits, 8)
	),
	TP_fast_assign(
		__entry->card = substream->pcm->card->number;
		__entry->device = substream->pcm->device;
		__entry->subdevice = substream->number;
		__entry->direction = substream->stream;
		__entry->type = type;
		__entry->index = index;
		__entry->total = substream->runtime->hw_constraints.rules_num;
		memcpy(__entry->prev_bits, prev->bits, sizeof(__u32) * 8);
		memcpy(__entry->curr_bits, curr->bits, sizeof(__u32) * 8);
	),
	TP_printk("pcmC%dD%d%s:%d %03d/%03d %s %08x%08x%08x%08x %08x%08x%08x%08x",
		  __entry->card,
		  __entry->device,
		  __entry->direction ? "c" : "p",
		  __entry->subdevice,
		  __entry->index,
		  __entry->total,
		  __print_symbolic(__entry->type, hw_param_labels),
		  __entry->prev_bits[3], __entry->prev_bits[2],
		  __entry->prev_bits[1], __entry->prev_bits[0],
		  __entry->curr_bits[3], __entry->curr_bits[2],
		  __entry->curr_bits[1], __entry->curr_bits[0]
	)
);

TRACE_EVENT(hw_interval_param,
	TP_PROTO(struct snd_pcm_substream *substream, snd_pcm_hw_param_t type, int index, const struct snd_interval *prev, const struct snd_interval *curr),
	TP_ARGS(substream, type, index, prev, curr),
	TP_STRUCT__entry(
		__field(int, card)
		__field(int, device)
		__field(int, subdevice)
		__field(int, direction)
		__field(snd_pcm_hw_param_t, type)
		__field(int, index)
		__field(int, total)
		__field(unsigned int, prev_min)
		__field(unsigned int, prev_max)
		__field(unsigned int, prev_openmin)
		__field(unsigned int, prev_openmax)
		__field(unsigned int, prev_integer)
		__field(unsigned int, prev_empty)
		__field(unsigned int, curr_min)
		__field(unsigned int, curr_max)
		__field(unsigned int, curr_openmin)
		__field(unsigned int, curr_openmax)
		__field(unsigned int, curr_integer)
		__field(unsigned int, curr_empty)
	),
	TP_fast_assign(
		__entry->card = substream->pcm->card->number;
		__entry->device = substream->pcm->device;
		__entry->subdevice = substream->number;
		__entry->direction = substream->stream;
		__entry->type = type;
		__entry->index = index;
		__entry->total = substream->runtime->hw_constraints.rules_num;
		__entry->prev_min = prev->min;
		__entry->prev_max = prev->max;
		__entry->prev_openmin = prev->openmin;
		__entry->prev_openmax = prev->openmax;
		__entry->prev_integer = prev->integer;
		__entry->prev_empty = prev->empty;
		__entry->curr_min = curr->min;
		__entry->curr_max = curr->max;
		__entry->curr_openmin = curr->openmin;
		__entry->curr_openmax = curr->openmax;
		__entry->curr_integer = curr->integer;
		__entry->curr_empty = curr->empty;
	),
	TP_printk("pcmC%dD%d%s:%d %03d/%03d %s %d %d %s%u %u%s %d %d %s%u %u%s",
		  __entry->card,
		  __entry->device,
		  __entry->direction ? "c" : "p",
		  __entry->subdevice,
		  __entry->index,
		  __entry->total,
		  __print_symbolic(__entry->type, hw_param_labels),
		  __entry->prev_empty,
		  __entry->prev_integer,
		  __entry->prev_openmin ? "(" : "[",
		  __entry->prev_min,
		  __entry->prev_max,
		  __entry->prev_openmax ? ")" : "]",
		  __entry->curr_empty,
		  __entry->curr_integer,
		  __entry->curr_openmin ? "(" : "[",
		  __entry->curr_min,
		  __entry->curr_max,
		  __entry->curr_openmax ? ")" : "]"
	)
);

#endif /* _PCM_PARAMS_TRACE_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE pcm_param_trace
#include <trace/define_trace.h>
