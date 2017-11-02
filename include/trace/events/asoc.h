/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM asoc

#if !defined(_TRACE_ASOC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_ASOC_H

#include <linux/ktime.h>
#include <linux/tracepoint.h>

#define DAPM_DIRECT "(direct)"
#define DAPM_ARROW(dir) (((dir) == SND_SOC_DAPM_DIR_OUT) ? "->" : "<-")

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

TRACE_EVENT(snd_soc_dapm_path,

	TP_PROTO(struct snd_soc_dapm_widget *widget,
		enum snd_soc_dapm_direction dir,
		struct snd_soc_dapm_path *path),

	TP_ARGS(widget, dir, path),

	TP_STRUCT__entry(
		__string(	wname,	widget->name		)
		__string(	pname,	path->name ? path->name : DAPM_DIRECT)
		__string(	pnname,	path->node[dir]->name	)
		__field(	int,	path_node		)
		__field(	int,	path_connect		)
		__field(	int,	path_dir		)
	),

	TP_fast_assign(
		__assign_str(wname, widget->name);
		__assign_str(pname, path->name ? path->name : DAPM_DIRECT);
		__assign_str(pnname, path->node[dir]->name);
		__entry->path_connect = path->connect;
		__entry->path_node = (long)path->node[dir];
		__entry->path_dir = dir;
	),

	TP_printk("%c%s %s %s %s %s",
		(int) __entry->path_node &&
		(int) __entry->path_connect ? '*' : ' ',
		__get_str(wname), DAPM_ARROW(__entry->path_dir),
		__get_str(pname), DAPM_ARROW(__entry->path_dir),
		__get_str(pnname))
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

	TP_printk("%s: found %d paths",
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
		__string(	name,		jack->jack->id		)
		__field(	int,		mask			)
		__field(	int,		val			)
	),

	TP_fast_assign(
		__assign_str(name, jack->jack->id);
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
		__string(	name,		jack->jack->id		)
		__field(	int,		val			)
	),

	TP_fast_assign(
		__assign_str(name, jack->jack->id);
		__entry->val = val;
	),

	TP_printk("jack=%s %x", __get_str(name), (int)__entry->val)
);

#endif /* _TRACE_ASOC_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
