#include <math.h>
#include <linux/compiler.h>

#include "../util/hist.h"
#include "../util/util.h"
#include "../util/sort.h"
#include "../util/evsel.h"

/* hist period print (hpp) functions */

typedef int (*hpp_snprint_fn)(char *buf, size_t size, const char *fmt, ...);

static int __hpp__fmt(struct perf_hpp *hpp, struct hist_entry *he,
		      u64 (*get_field)(struct hist_entry *),
		      const char *fmt, hpp_snprint_fn print_fn,
		      bool fmt_percent)
{
	int ret;
	struct hists *hists = he->hists;
	struct perf_evsel *evsel = hists_to_evsel(hists);

	if (fmt_percent) {
		double percent = 0.0;

		if (hists->stats.total_period)
			percent = 100.0 * get_field(he) /
				  hists->stats.total_period;

		ret = print_fn(hpp->buf, hpp->size, fmt, percent);
	} else
		ret = print_fn(hpp->buf, hpp->size, fmt, get_field(he));

	if (perf_evsel__is_group_event(evsel)) {
		int prev_idx, idx_delta;
		struct hist_entry *pair;
		int nr_members = evsel->nr_members;

		prev_idx = perf_evsel__group_idx(evsel);

		list_for_each_entry(pair, &he->pairs.head, pairs.node) {
			u64 period = get_field(pair);
			u64 total = pair->hists->stats.total_period;

			if (!total)
				continue;

			evsel = hists_to_evsel(pair->hists);
			idx_delta = perf_evsel__group_idx(evsel) - prev_idx - 1;

			while (idx_delta--) {
				/*
				 * zero-fill group members in the middle which
				 * have no sample
				 */
				ret += print_fn(hpp->buf + ret, hpp->size - ret,
						fmt, 0);
			}

			if (fmt_percent)
				ret += print_fn(hpp->buf + ret, hpp->size - ret,
						fmt, 100.0 * period / total);
			else
				ret += print_fn(hpp->buf + ret, hpp->size - ret,
						fmt, period);

			prev_idx = perf_evsel__group_idx(evsel);
		}

		idx_delta = nr_members - prev_idx - 1;

		while (idx_delta--) {
			/*
			 * zero-fill group members at last which have no sample
			 */
			ret += print_fn(hpp->buf + ret, hpp->size - ret,
					fmt, 0);
		}
	}
	return ret;
}

#define __HPP_HEADER_FN(_type, _str, _min_width, _unit_width) 		\
static int hpp__header_##_type(struct perf_hpp_fmt *fmt __maybe_unused,	\
			       struct perf_hpp *hpp)			\
{									\
	int len = _min_width;						\
									\
	if (symbol_conf.event_group) {					\
		struct perf_evsel *evsel = hpp->ptr;			\
									\
		len = max(len, evsel->nr_members * _unit_width);	\
	}								\
	return scnprintf(hpp->buf, hpp->size, "%*s", len, _str);	\
}

#define __HPP_WIDTH_FN(_type, _min_width, _unit_width) 			\
static int hpp__width_##_type(struct perf_hpp_fmt *fmt __maybe_unused,	\
			      struct perf_hpp *hpp __maybe_unused)	\
{									\
	int len = _min_width;						\
									\
	if (symbol_conf.event_group) {					\
		struct perf_evsel *evsel = hpp->ptr;			\
									\
		len = max(len, evsel->nr_members * _unit_width);	\
	}								\
	return len;							\
}

#define __HPP_COLOR_PERCENT_FN(_type, _field)					\
static u64 he_get_##_field(struct hist_entry *he)				\
{										\
	return he->stat._field;							\
}										\
										\
static int hpp__color_##_type(struct perf_hpp_fmt *fmt __maybe_unused,		\
			      struct perf_hpp *hpp, struct hist_entry *he) 	\
{										\
	return __hpp__fmt(hpp, he, he_get_##_field, " %6.2f%%",			\
			  (hpp_snprint_fn)percent_color_snprintf, true);	\
}

#define __HPP_ENTRY_PERCENT_FN(_type, _field)					\
static int hpp__entry_##_type(struct perf_hpp_fmt *_fmt __maybe_unused,		\
			      struct perf_hpp *hpp, struct hist_entry *he) 	\
{										\
	const char *fmt = symbol_conf.field_sep ? " %.2f" : " %6.2f%%";		\
	return __hpp__fmt(hpp, he, he_get_##_field, fmt,			\
			  scnprintf, true);					\
}

#define __HPP_ENTRY_RAW_FN(_type, _field)					\
static u64 he_get_raw_##_field(struct hist_entry *he)				\
{										\
	return he->stat._field;							\
}										\
										\
static int hpp__entry_##_type(struct perf_hpp_fmt *_fmt __maybe_unused,		\
			      struct perf_hpp *hpp, struct hist_entry *he) 	\
{										\
	const char *fmt = symbol_conf.field_sep ? " %"PRIu64 : " %11"PRIu64;	\
	return __hpp__fmt(hpp, he, he_get_raw_##_field, fmt, scnprintf, false);	\
}

#define HPP_PERCENT_FNS(_type, _str, _field, _min_width, _unit_width)	\
__HPP_HEADER_FN(_type, _str, _min_width, _unit_width)			\
__HPP_WIDTH_FN(_type, _min_width, _unit_width)				\
__HPP_COLOR_PERCENT_FN(_type, _field)					\
__HPP_ENTRY_PERCENT_FN(_type, _field)

#define HPP_RAW_FNS(_type, _str, _field, _min_width, _unit_width)	\
__HPP_HEADER_FN(_type, _str, _min_width, _unit_width)			\
__HPP_WIDTH_FN(_type, _min_width, _unit_width)				\
__HPP_ENTRY_RAW_FN(_type, _field)


HPP_PERCENT_FNS(overhead, "Overhead", period, 8, 8)
HPP_PERCENT_FNS(overhead_sys, "sys", period_sys, 8, 8)
HPP_PERCENT_FNS(overhead_us, "usr", period_us, 8, 8)
HPP_PERCENT_FNS(overhead_guest_sys, "guest sys", period_guest_sys, 9, 8)
HPP_PERCENT_FNS(overhead_guest_us, "guest usr", period_guest_us, 9, 8)

HPP_RAW_FNS(samples, "Samples", nr_events, 12, 12)
HPP_RAW_FNS(period, "Period", period, 12, 12)

#define HPP__COLOR_PRINT_FNS(_name)			\
	{						\
		.header	= hpp__header_ ## _name,	\
		.width	= hpp__width_ ## _name,		\
		.color	= hpp__color_ ## _name,		\
		.entry	= hpp__entry_ ## _name		\
	}

#define HPP__PRINT_FNS(_name)				\
	{						\
		.header	= hpp__header_ ## _name,	\
		.width	= hpp__width_ ## _name,		\
		.entry	= hpp__entry_ ## _name		\
	}

struct perf_hpp_fmt perf_hpp__format[] = {
	HPP__COLOR_PRINT_FNS(overhead),
	HPP__COLOR_PRINT_FNS(overhead_sys),
	HPP__COLOR_PRINT_FNS(overhead_us),
	HPP__COLOR_PRINT_FNS(overhead_guest_sys),
	HPP__COLOR_PRINT_FNS(overhead_guest_us),
	HPP__PRINT_FNS(samples),
	HPP__PRINT_FNS(period)
};

LIST_HEAD(perf_hpp__list);


#undef HPP__COLOR_PRINT_FNS
#undef HPP__PRINT_FNS

#undef HPP_PERCENT_FNS
#undef HPP_RAW_FNS

#undef __HPP_HEADER_FN
#undef __HPP_WIDTH_FN
#undef __HPP_COLOR_PERCENT_FN
#undef __HPP_ENTRY_PERCENT_FN
#undef __HPP_ENTRY_RAW_FN


void perf_hpp__init(void)
{
	perf_hpp__column_enable(PERF_HPP__OVERHEAD);

	if (symbol_conf.show_cpu_utilization) {
		perf_hpp__column_enable(PERF_HPP__OVERHEAD_SYS);
		perf_hpp__column_enable(PERF_HPP__OVERHEAD_US);

		if (perf_guest) {
			perf_hpp__column_enable(PERF_HPP__OVERHEAD_GUEST_SYS);
			perf_hpp__column_enable(PERF_HPP__OVERHEAD_GUEST_US);
		}
	}

	if (symbol_conf.show_nr_samples)
		perf_hpp__column_enable(PERF_HPP__SAMPLES);

	if (symbol_conf.show_total_period)
		perf_hpp__column_enable(PERF_HPP__PERIOD);
}

void perf_hpp__column_register(struct perf_hpp_fmt *format)
{
	list_add_tail(&format->list, &perf_hpp__list);
}

void perf_hpp__column_enable(unsigned col)
{
	BUG_ON(col >= PERF_HPP__MAX_INDEX);
	perf_hpp__column_register(&perf_hpp__format[col]);
}

int hist_entry__sort_snprintf(struct hist_entry *he, char *s, size_t size,
			      struct hists *hists)
{
	const char *sep = symbol_conf.field_sep;
	struct sort_entry *se;
	int ret = 0;

	list_for_each_entry(se, &hist_entry__sort_list, list) {
		if (se->elide)
			continue;

		ret += scnprintf(s + ret, size - ret, "%s", sep ?: "  ");
		ret += se->se_snprintf(he, s + ret, size - ret,
				       hists__col_len(hists, se->se_width_idx));
	}

	return ret;
}

/*
 * See hists__fprintf to match the column widths
 */
unsigned int hists__sort_list_width(struct hists *hists)
{
	struct perf_hpp_fmt *fmt;
	struct sort_entry *se;
	int i = 0, ret = 0;
	struct perf_hpp dummy_hpp = {
		.ptr	= hists_to_evsel(hists),
	};

	perf_hpp__for_each_format(fmt) {
		if (i)
			ret += 2;

		ret += fmt->width(fmt, &dummy_hpp);
	}

	list_for_each_entry(se, &hist_entry__sort_list, list)
		if (!se->elide)
			ret += 2 + hists__col_len(hists, se->se_width_idx);

	if (verbose) /* Addr + origin */
		ret += 3 + BITS_PER_LONG / 4;

	return ret;
}
