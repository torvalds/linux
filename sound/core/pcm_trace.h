#undef TRACE_SYSTEM
#define TRACE_SYSTEM snd_pcm
#define TRACE_INCLUDE_FILE pcm_trace

#if !defined(_PCM_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _PCM_TRACE_H

#include <linux/tracepoint.h>

TRACE_EVENT(hwptr,
	TP_PROTO(struct snd_pcm_substream *substream, snd_pcm_uframes_t pos, bool irq),
	TP_ARGS(substream, pos, irq),
	TP_STRUCT__entry(
		__field( bool, in_interrupt )
		__field( unsigned int, card )
		__field( unsigned int, device )
		__field( unsigned int, number )
		__field( unsigned int, stream )
		__field( snd_pcm_uframes_t, pos )
		__field( snd_pcm_uframes_t, period_size )
		__field( snd_pcm_uframes_t, buffer_size )
		__field( snd_pcm_uframes_t, old_hw_ptr )
		__field( snd_pcm_uframes_t, hw_ptr_base )
	),
	TP_fast_assign(
		__entry->in_interrupt = (irq);
		__entry->card = (substream)->pcm->card->number;
		__entry->device = (substream)->pcm->device;
		__entry->number = (substream)->number;
		__entry->stream = (substream)->stream;
		__entry->pos = (pos);
		__entry->period_size = (substream)->runtime->period_size;
		__entry->buffer_size = (substream)->runtime->buffer_size;
		__entry->old_hw_ptr = (substream)->runtime->status->hw_ptr;
		__entry->hw_ptr_base = (substream)->runtime->hw_ptr_base;
	),
	TP_printk("pcmC%dD%d%c/sub%d: %s: pos=%lu, old=%lu, base=%lu, period=%lu, buf=%lu",
		  __entry->card, __entry->device,
		  __entry->stream == SNDRV_PCM_STREAM_PLAYBACK ? 'p' : 'c',
		  __entry->number,
		  __entry->in_interrupt ? "IRQ" : "POS",
		  (unsigned long)__entry->pos,
		  (unsigned long)__entry->old_hw_ptr,
		  (unsigned long)__entry->hw_ptr_base,
		  (unsigned long)__entry->period_size,
		  (unsigned long)__entry->buffer_size)
);

TRACE_EVENT(xrun,
	TP_PROTO(struct snd_pcm_substream *substream),
	TP_ARGS(substream),
	TP_STRUCT__entry(
		__field( unsigned int, card )
		__field( unsigned int, device )
		__field( unsigned int, number )
		__field( unsigned int, stream )
		__field( snd_pcm_uframes_t, period_size )
		__field( snd_pcm_uframes_t, buffer_size )
		__field( snd_pcm_uframes_t, old_hw_ptr )
		__field( snd_pcm_uframes_t, hw_ptr_base )
	),
	TP_fast_assign(
		__entry->card = (substream)->pcm->card->number;
		__entry->device = (substream)->pcm->device;
		__entry->number = (substream)->number;
		__entry->stream = (substream)->stream;
		__entry->period_size = (substream)->runtime->period_size;
		__entry->buffer_size = (substream)->runtime->buffer_size;
		__entry->old_hw_ptr = (substream)->runtime->status->hw_ptr;
		__entry->hw_ptr_base = (substream)->runtime->hw_ptr_base;
	),
	TP_printk("pcmC%dD%d%c/sub%d: XRUN: old=%lu, base=%lu, period=%lu, buf=%lu",
		  __entry->card, __entry->device,
		  __entry->stream == SNDRV_PCM_STREAM_PLAYBACK ? 'p' : 'c',
		  __entry->number,
		  (unsigned long)__entry->old_hw_ptr,
		  (unsigned long)__entry->hw_ptr_base,
		  (unsigned long)__entry->period_size,
		  (unsigned long)__entry->buffer_size)
);

TRACE_EVENT(hw_ptr_error,
	TP_PROTO(struct snd_pcm_substream *substream, const char *why),
	TP_ARGS(substream, why),
	TP_STRUCT__entry(
		__field( unsigned int, card )
		__field( unsigned int, device )
		__field( unsigned int, number )
		__field( unsigned int, stream )
		__field( const char *, reason )
	),
	TP_fast_assign(
		__entry->card = (substream)->pcm->card->number;
		__entry->device = (substream)->pcm->device;
		__entry->number = (substream)->number;
		__entry->stream = (substream)->stream;
		__entry->reason = (why);
	),
	TP_printk("pcmC%dD%d%c/sub%d: ERROR: %s",
		  __entry->card, __entry->device,
		  __entry->stream == SNDRV_PCM_STREAM_PLAYBACK ? 'p' : 'c',
		  __entry->number, __entry->reason)
);

#endif /* _PCM_TRACE_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#include <trace/define_trace.h>
