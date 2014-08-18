#undef TRACE_SYSTEM
#define TRACE_SYSTEM asoc

#if !defined(_TRACE_ASOC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_ASOC_H

#include <linux/ktime.h>
#include <linux/tracepoint.h>

#define DAPM_DIRECT "(direct)"

struct snd_soc_jack;
struct snd_soc_codec;
struct snd_soc_card;
struct snd_soc_dapm_widget;
struct snd_soc_dapm_path;

DECLARE_EVENT_CLASS(snd_soc_card,

	TP_PROTO(struct snd_soc_card *card, int val),

	TP_ARGS(card, val),

	TP_STRUCT__entry(
		__string(	name,		card->name	)
		__field(	int,		val		)
	),

	TP_fast_assign(
		__assign_str(name, card->name);
		__entry->val = val;
	),

	TP_printk("card=%s val=%d", __get_str(name), (int)__entry->val)
);

DEFINE_EVENT(snd_soc_card, snd_soc_bias_level_start,

	TP_PROTO(struct snd_soc_card *card, int val),

	TP_ARGS(card, val)

);

DEFINE_EVENT(snd_soc_card, snd_soc_bias_level_done,

	TP_PROTO(struct snd_soc_card *card, int val),

	TP_ARGS(card, val)

);

DECLARE_EVENT_CLASS(snd_soc_dapm_basic,

	TP_PROTO(struct snd_soc_card *card),

	TP_ARGS(card),

	TP_STRUCT__entry(
		__string(	name,	card->name	)
	),

	TP_fast_assign(
		__assign_str(name, card->name);
	),

	TP_printk("card=%s", __get_str(name))
);

DEFINE_EVENT(snd_soc_dapm_basic, snd_soc_dapm_start,

	TP_PROTO(struct snd_soc_card *card),

	TP_ARGS(card)

);

DEFINE_EVENT(snd_soc_dapm_basic, snd_soc_dapm_done,

	TP_PROTO(struct snd_soc_card *card),

	TP_ARGS(card)

);

DECLARE_EVENT_CLASS(snd_soc_dapm_widget,

	TP_PROTO(struct snd_soc_dapm_widget *w, int val),

	TP_ARGS(w, val),

	TP_STRUCT__entry(
		__string(	name,	w->name		)
		__field(	int,	val		)
	),

	TP_fast_assign(
		__assign_str(name, w->name);
		__entry->val = val;
	),

	TP_printk("widget=%s val=%d", __get_str(name),
		  (int)__entry->val)
);

DEFINE_EVENT(snd_soc_dapm_widget, snd_soc_dapm_widget_power,

	TP_PROTO(struct snd_soc_dapm_widget *w, int val),

	TP_ARGS(w, val)

);

DEFINE_EVENT(snd_soc_dapm_widget, snd_soc_dapm_widget_event_start,

	TP_PROTO(struct snd_soc_dapm_widget *w, int val),

	TP_ARGS(w, val)

);

DEFINE_EVENT(snd_soc_dapm_widget, snd_soc_dapm_widget_event_done,

	TP_PROTO(struct snd_soc_dapm_widget *w, int val),

	TP_ARGS(w, val)

);

TRACE_EVENT(snd_soc_dapm_walk_done,

	TP_PROTO(struct snd_soc_card *card),

	TP_ARGS(card),

	TP_STRUCT__entry(
		__string(	name,	card->name		)
		__field(	int,	power_checks		)
		__field(	int,	path_checks		)
		__field(	int,	neighbour_checks	)
	),

	TP_fast_assign(
		__assign_str(name, card->name);
		__entry->power_checks = card->dapm_stats.power_checks;
		__entry->path_checks = card->dapm_stats.path_checks;
		__entry->neighbour_checks = card->dapm_stats.neighbour_checks;
	),

	TP_printk("%s: checks %d power, %d path, %d neighbour",
		  __get_str(name), (int)__entry->power_checks,
		  (int)__entry->path_checks, (int)__entry->neighbour_checks)
);

TRACE_EVENT(snd_soc_dapm_output_path,

	TP_PROTO(struct snd_soc_dapm_widget *widget,
		struct snd_soc_dapm_path *path),

	TP_ARGS(widget, path),

	TP_STRUCT__entry(
		__string(	wname,	widget->name		)
		__string(	pname,	path->name ? path->name : DAPM_DIRECT)
		__string(	psname,	path->sink->name	)
		__field(	int,	path_sink		)
		__field(	int,	path_connect		)
	),

	TP_fast_assign(
		__assign_str(wname, widget->name);
		__assign_str(pname, path->name ? path->name : DAPM_DIRECT);
		__assign_str(psname, path->sink->name);
		__entry->path_connect = path->connect;
		__entry->path_sink = (long)path->sink;
	),

	TP_printk("%c%s -> %s -> %s\n",
		(int) __entry->path_sink &&
		(int) __entry->path_connect ? '*' : ' ',
		__get_str(wname), __get_str(pname), __get_str(psname))
);

TRACE_EVENT(snd_soc_dapm_input_path,

	TP_PROTO(struct snd_soc_dapm_widget *widget,
		struct snd_soc_dapm_path *path),

	TP_ARGS(widget, path),

	TP_STRUCT__entry(
		__string(	wname,	widget->name		)
		__string(	pname,	path->name ? path->name : DAPM_DIRECT)
		__string(	psname,	path->source->name	)
		__field(	int,	path_source		)
		__field(	int,	path_connect		)
	),

	TP_fast_assign(
		__assign_str(wname, widget->name);
		__assign_str(pname, path->name ? path->name : DAPM_DIRECT);
		__assign_str(psname, path->source->name);
		__entry->path_connect = path->connect;
		__entry->path_source = (long)path->source;
	),

	TP_printk("%c%s <- %s <- %s\n",
		(int) __entry->path_source &&
		(int) __entry->path_connect ? '*' : ' ',
		__get_str(wname), __get_str(pname), __get_str(psname))
);

TRACE_EVENT(snd_soc_dapm_connected,

	TP_PROTO(int paths, int stream),

	TP_ARGS(paths, stream),

	TP_STRUCT__entry(
		__field(	int,	paths		)
		__field(	int,	stream		)
	),

	TP_fast_assign(
		__entry->paths = paths;
		__entry->stream = stream;
	),

	TP_printk("%s: found %d paths\n",
		__entry->stream ? "capture" : "playback", __entry->paths)
);

TRACE_EVENT(snd_soc_jack_irq,

	TP_PROTO(const char *name),

	TP_ARGS(name),

	TP_STRUCT__entry(
		__string(	name,	name		)
	),

	TP_fast_assign(
		__assign_str(name, name);
	),

	TP_printk("%s", __get_str(name))
);

TRACE_EVENT(snd_soc_jack_report,

	TP_PROTO(struct snd_soc_jack *jack, int mask, int val),

	TP_ARGS(jack, mask, val),

	TP_STRUCT__entry(
		__string(	name,		jack->jack->name	)
		__field(	int,		mask			)
		__field(	int,		val			)
	),

	TP_fast_assign(
		__assign_str(name, jack->jack->name);
		__entry->mask = mask;
		__entry->val = val;
	),

	TP_printk("jack=%s %x/%x", __get_str(name), (int)__entry->val,
		  (int)__entry->mask)
);

TRACE_EVENT(snd_soc_jack_notify,

	TP_PROTO(struct snd_soc_jack *jack, int val),

	TP_ARGS(jack, val),

	TP_STRUCT__entry(
		__string(	name,		jack->jack->name	)
		__field(	int,		val			)
	),

	TP_fast_assign(
		__assign_str(name, jack->jack->name);
		__entry->val = val;
	),

	TP_printk("jack=%s %x", __get_str(name), (int)__entry->val)
);

TRACE_EVENT(snd_soc_cache_sync,

	TP_PROTO(struct snd_soc_codec *codec, const char *type,
		 const char *status),

	TP_ARGS(codec, type, status),

	TP_STRUCT__entry(
		__string(	name,		codec->component.name)
		__string(	status,		status		)
		__string(	type,		type		)
		__field(	int,		id		)
	),

	TP_fast_assign(
		__assign_str(name, codec->component.name);
		__assign_str(status, status);
		__assign_str(type, type);
		__entry->id = codec->component.id;
	),

	TP_printk("codec=%s.%d type=%s status=%s", __get_str(name),
		  (int)__entry->id, __get_str(type), __get_str(status))
);

#endif /* _TRACE_ASOC_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
