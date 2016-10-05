#undef TRACE_SYSTEM
#define TRACE_SYSTEM wbt

#if !defined(_TRACE_WBT_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_WBT_H

#include <linux/tracepoint.h>
#include <linux/wbt.h>

/**
 * wbt_stat - trace stats for blk_wb
 * @stat: array of read/write stats
 */
TRACE_EVENT(wbt_stat,

	TP_PROTO(struct backing_dev_info *bdi, struct blk_rq_stat *stat),

	TP_ARGS(bdi, stat),

	TP_STRUCT__entry(
		__array(char, name, 32)
		__field(s64, rmean)
		__field(u64, rmin)
		__field(u64, rmax)
		__field(s64, rnr_samples)
		__field(s64, rtime)
		__field(s64, wmean)
		__field(u64, wmin)
		__field(u64, wmax)
		__field(s64, wnr_samples)
		__field(s64, wtime)
	),

	TP_fast_assign(
		strncpy(__entry->name, dev_name(bdi->dev), 32);
		__entry->rmean		= stat[0].mean;
		__entry->rmin		= stat[0].min;
		__entry->rmax		= stat[0].max;
		__entry->rnr_samples	= stat[0].nr_samples;
		__entry->wmean		= stat[1].mean;
		__entry->wmin		= stat[1].min;
		__entry->wmax		= stat[1].max;
		__entry->wnr_samples	= stat[1].nr_samples;
	),

	TP_printk("%s: rmean=%llu, rmin=%llu, rmax=%llu, rsamples=%llu, "
		  "wmean=%llu, wmin=%llu, wmax=%llu, wsamples=%llu\n",
		  __entry->name, __entry->rmean, __entry->rmin, __entry->rmax,
		  __entry->rnr_samples, __entry->wmean, __entry->wmin,
		  __entry->wmax, __entry->wnr_samples)
);

/**
 * wbt_lat - trace latency event
 * @lat: latency trigger
 */
TRACE_EVENT(wbt_lat,

	TP_PROTO(struct backing_dev_info *bdi, unsigned long lat),

	TP_ARGS(bdi, lat),

	TP_STRUCT__entry(
		__array(char, name, 32)
		__field(unsigned long, lat)
	),

	TP_fast_assign(
		strncpy(__entry->name, dev_name(bdi->dev), 32);
		__entry->lat = div_u64(lat, 1000);
	),

	TP_printk("%s: latency %lluus\n", __entry->name,
			(unsigned long long) __entry->lat)
);

/**
 * wbt_step - trace wb event step
 * @msg: context message
 * @step: the current scale step count
 * @window: the current monitoring window
 * @bg: the current background queue limit
 * @normal: the current normal writeback limit
 * @max: the current max throughput writeback limit
 */
TRACE_EVENT(wbt_step,

	TP_PROTO(struct backing_dev_info *bdi, const char *msg,
		 int step, unsigned long window, unsigned int bg,
		 unsigned int normal, unsigned int max),

	TP_ARGS(bdi, msg, step, window, bg, normal, max),

	TP_STRUCT__entry(
		__array(char, name, 32)
		__field(const char *, msg)
		__field(int, step)
		__field(unsigned long, window)
		__field(unsigned int, bg)
		__field(unsigned int, normal)
		__field(unsigned int, max)
	),

	TP_fast_assign(
		strncpy(__entry->name, dev_name(bdi->dev), 32);
		__entry->msg	= msg;
		__entry->step	= step;
		__entry->window	= div_u64(window, 1000);
		__entry->bg	= bg;
		__entry->normal	= normal;
		__entry->max	= max;
	),

	TP_printk("%s: %s: step=%d, window=%luus, background=%u, normal=%u, max=%u\n",
		  __entry->name, __entry->msg, __entry->step, __entry->window,
		  __entry->bg, __entry->normal, __entry->max)
);

/**
 * wbt_timer - trace wb timer event
 * @status: timer state status
 * @step: the current scale step count
 * @inflight: tracked writes inflight
 */
TRACE_EVENT(wbt_timer,

	TP_PROTO(struct backing_dev_info *bdi, unsigned int status,
		 int step, unsigned int inflight),

	TP_ARGS(bdi, status, step, inflight),

	TP_STRUCT__entry(
		__array(char, name, 32)
		__field(unsigned int, status)
		__field(int, step)
		__field(unsigned int, inflight)
	),

	TP_fast_assign(
		strncpy(__entry->name, dev_name(bdi->dev), 32);
		__entry->status		= status;
		__entry->step		= step;
		__entry->inflight	= inflight;
	),

	TP_printk("%s: status=%u, step=%d, inflight=%u\n", __entry->name,
		  __entry->status, __entry->step, __entry->inflight)
);

#endif /* _TRACE_WBT_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
