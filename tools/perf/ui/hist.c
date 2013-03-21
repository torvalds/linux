#include <math.h>

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
static int hpp__header_##_type(struct perf_hpp *hpp)			\
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
static int hpp__width_##_type(struct perf_hpp *hpp __maybe_unused)	\
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
static int hpp__color_##_type(struct perf_hpp *hpp, struct hist_entry *he) 	\
{										\
	return __hpp__fmt(hpp, he, he_get_##_field, " %6.2f%%",			\
			  (hpp_snprint_fn)percent_color_snprintf, true);	\
}

#define __HPP_ENTRY_PERCENT_FN(_type, _field)					\
static int hpp__entry_##_type(struct perf_hpp *hpp, struct hist_entry *he) 	\
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
static int hpp__entry_##_type(struct perf_hpp *hpp, struct hist_entry *he) 	\
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


static int hpp__header_baseline(struct perf_hpp *hpp)
{
	return scnprintf(hpp->buf, hpp->size, "Baseline");
}

static int hpp__width_baseline(struct perf_hpp *hpp __maybe_unused)
{
	return 8;
}

static double baseline_percent(struct hist_entry *he)
{
	struct hist_entry *pair = hist_entry__next_pair(he);
	struct hists *pair_hists = pair ? pair->hists : NULL;
	double percent = 0.0;

	if (pair) {
		u64 total_period = pair_hists->stats.total_period;
		u64 base_period  = pair->stat.period;

		percent = 100.0 * base_period / total_period;
	}

	return percent;
}

static int hpp__color_baseline(struct perf_hpp *hpp, struct hist_entry *he)
{
	double percent = baseline_percent(he);

	if (hist_entry__has_pairs(he) || symbol_conf.field_sep)
		return percent_color_snprintf(hpp->buf, hpp->size, " %6.2f%%", percent);
	else
		return scnprintf(hpp->buf, hpp->size, "        ");
}

static int hpp__entry_baseline(struct perf_hpp *hpp, struct hist_entry *he)
{
	double percent = baseline_percent(he);
	const char *fmt = symbol_conf.field_sep ? "%.2f" : " %6.2f%%";

	if (hist_entry__has_pairs(he) || symbol_conf.field_sep)
		return scnprintf(hpp->buf, hpp->size, fmt, percent);
	else
		return scnprintf(hpp->buf, hpp->size, "            ");
}

static int hpp__header_period_baseline(struct perf_hpp *hpp)
{
	const char *fmt = symbol_conf.field_sep ? "%s" : "%12s";

	return scnprintf(hpp->buf, hpp->size, fmt, "Period Base");
}

static int hpp__width_period_baseline(struct perf_hpp *hpp __maybe_unused)
{
	return 12;
}

static int hpp__entry_period_baseline(struct perf_hpp *hpp, struct hist_entry *he)
{
	struct hist_entry *pair = hist_entry__next_pair(he);
	u64 period = pair ? pair->stat.period : 0;
	const char *fmt = symbol_conf.field_sep ? "%" PRIu64 : "%12" PRIu64;

	return scnprintf(hpp->buf, hpp->size, fmt, period);
}

static int hpp__header_delta(struct perf_hpp *hpp)
{
	const char *fmt = symbol_conf.field_sep ? "%s" : "%7s";

	return scnprintf(hpp->buf, hpp->size, fmt, "Delta");
}

static int hpp__width_delta(struct perf_hpp *hpp __maybe_unused)
{
	return 7;
}

static int hpp__entry_delta(struct perf_hpp *hpp, struct hist_entry *he)
{
	struct hist_entry *pair = hist_entry__next_pair(he);
	const char *fmt = symbol_conf.field_sep ? "%s" : "%7.7s";
	char buf[32] = " ";
	double diff = 0.0;

	if (pair) {
		if (he->diff.computed)
			diff = he->diff.period_ratio_delta;
		else
			diff = perf_diff__compute_delta(he, pair);
	} else
		diff = perf_diff__period_percent(he, he->stat.period);

	if (fabs(diff) >= 0.01)
		scnprintf(buf, sizeof(buf), "%+4.2F%%", diff);

	return scnprintf(hpp->buf, hpp->size, fmt, buf);
}

static int hpp__header_ratio(struct perf_hpp *hpp)
{
	const char *fmt = symbol_conf.field_sep ? "%s" : "%14s";

	return scnprintf(hpp->buf, hpp->size, fmt, "Ratio");
}

static int hpp__width_ratio(struct perf_hpp *hpp __maybe_unused)
{
	return 14;
}

static int hpp__entry_ratio(struct perf_hpp *hpp, struct hist_entry *he)
{
	struct hist_entry *pair = hist_entry__next_pair(he);
	const char *fmt = symbol_conf.field_sep ? "%s" : "%14s";
	char buf[32] = " ";
	double ratio = 0.0;

	if (pair) {
		if (he->diff.computed)
			ratio = he->diff.period_ratio;
		else
			ratio = perf_diff__compute_ratio(he, pair);
	}

	if (ratio > 0.0)
		scnprintf(buf, sizeof(buf), "%+14.6F", ratio);

	return scnprintf(hpp->buf, hpp->size, fmt, buf);
}

static int hpp__header_wdiff(struct perf_hpp *hpp)
{
	const char *fmt = symbol_conf.field_sep ? "%s" : "%14s";

	return scnprintf(hpp->buf, hpp->size, fmt, "Weighted diff");
}

static int hpp__width_wdiff(struct perf_hpp *hpp __maybe_unused)
{
	return 14;
}

static int hpp__entry_wdiff(struct perf_hpp *hpp, struct hist_entry *he)
{
	struct hist_entry *pair = hist_entry__next_pair(he);
	const char *fmt = symbol_conf.field_sep ? "%s" : "%14s";
	char buf[32] = " ";
	s64 wdiff = 0;

	if (pair) {
		if (he->diff.computed)
			wdiff = he->diff.wdiff;
		else
			wdiff = perf_diff__compute_wdiff(he, pair);
	}

	if (wdiff != 0)
		scnprintf(buf, sizeof(buf), "%14ld", wdiff);

	return scnprintf(hpp->buf, hpp->size, fmt, buf);
}

static int hpp__header_formula(struct perf_hpp *hpp)
{
	const char *fmt = symbol_conf.field_sep ? "%s" : "%70s";

	return scnprintf(hpp->buf, hpp->size, fmt, "Formula");
}

static int hpp__width_formula(struct perf_hpp *hpp __maybe_unused)
{
	return 70;
}

static int hpp__entry_formula(struct perf_hpp *hpp, struct hist_entry *he)
{
	struct hist_entry *pair = hist_entry__next_pair(he);
	const char *fmt = symbol_conf.field_sep ? "%s" : "%-70s";
	char buf[96] = " ";

	if (pair)
		perf_diff__formula(he, pair, buf, sizeof(buf));

	return scnprintf(hpp->buf, hpp->size, fmt, buf);
}

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
	HPP__COLOR_PRINT_FNS(baseline),
	HPP__COLOR_PRINT_FNS(overhead),
	HPP__COLOR_PRINT_FNS(overhead_sys),
	HPP__COLOR_PRINT_FNS(overhead_us),
	HPP__COLOR_PRINT_FNS(overhead_guest_sys),
	HPP__COLOR_PRINT_FNS(overhead_guest_us),
	HPP__PRINT_FNS(samples),
	HPP__PRINT_FNS(period),
	HPP__PRINT_FNS(period_baseline),
	HPP__PRINT_FNS(delta),
	HPP__PRINT_FNS(ratio),
	HPP__PRINT_FNS(wdiff),
	HPP__PRINT_FNS(formula)
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

static inline void advance_hpp(struct perf_hpp *hpp, int inc)
{
	hpp->buf  += inc;
	hpp->size -= inc;
}

int hist_entry__period_snprintf(struct perf_hpp *hpp, struct hist_entry *he,
				bool color)
{
	const char *sep = symbol_conf.field_sep;
	struct perf_hpp_fmt *fmt;
	char *start = hpp->buf;
	int ret;
	bool first = true;

	if (symbol_conf.exclude_other && !he->parent)
		return 0;

	perf_hpp__for_each_format(fmt) {
		/*
		 * If there's no field_sep, we still need
		 * to display initial '  '.
		 */
		if (!sep || !first) {
			ret = scnprintf(hpp->buf, hpp->size, "%s", sep ?: "  ");
			advance_hpp(hpp, ret);
		} else
			first = false;

		if (color && fmt->color)
			ret = fmt->color(hpp, he);
		else
			ret = fmt->entry(hpp, he);

		advance_hpp(hpp, ret);
	}

	return hpp->buf - start;
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

		ret += fmt->width(&dummy_hpp);
	}

	list_for_each_entry(se, &hist_entry__sort_list, list)
		if (!se->elide)
			ret += 2 + hists__col_len(hists, se->se_width_idx);

	if (verbose) /* Addr + origin */
		ret += 3 + BITS_PER_LONG / 4;

	return ret;
}
