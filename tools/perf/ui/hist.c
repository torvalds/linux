#include <math.h>

#include "../util/hist.h"
#include "../util/util.h"
#include "../util/sort.h"


/* hist period print (hpp) functions */
static int hpp__header_overhead(struct perf_hpp *hpp)
{
	return scnprintf(hpp->buf, hpp->size, "Overhead");
}

static int hpp__width_overhead(struct perf_hpp *hpp __maybe_unused)
{
	return 8;
}

static int hpp__color_overhead(struct perf_hpp *hpp, struct hist_entry *he)
{
	struct hists *hists = he->hists;
	double percent = 100.0 * he->stat.period / hists->stats.total_period;

	return percent_color_snprintf(hpp->buf, hpp->size, " %6.2f%%", percent);
}

static int hpp__entry_overhead(struct perf_hpp *hpp, struct hist_entry *he)
{
	struct hists *hists = he->hists;
	double percent = 100.0 * he->stat.period / hists->stats.total_period;
	const char *fmt = symbol_conf.field_sep ? "%.2f" : " %6.2f%%";

	return scnprintf(hpp->buf, hpp->size, fmt, percent);
}

static int hpp__header_overhead_sys(struct perf_hpp *hpp)
{
	const char *fmt = symbol_conf.field_sep ? "%s" : "%7s";

	return scnprintf(hpp->buf, hpp->size, fmt, "sys");
}

static int hpp__width_overhead_sys(struct perf_hpp *hpp __maybe_unused)
{
	return 7;
}

static int hpp__color_overhead_sys(struct perf_hpp *hpp, struct hist_entry *he)
{
	struct hists *hists = he->hists;
	double percent = 100.0 * he->stat.period_sys / hists->stats.total_period;

	return percent_color_snprintf(hpp->buf, hpp->size, "%6.2f%%", percent);
}

static int hpp__entry_overhead_sys(struct perf_hpp *hpp, struct hist_entry *he)
{
	struct hists *hists = he->hists;
	double percent = 100.0 * he->stat.period_sys / hists->stats.total_period;
	const char *fmt = symbol_conf.field_sep ? "%.2f" : "%6.2f%%";

	return scnprintf(hpp->buf, hpp->size, fmt, percent);
}

static int hpp__header_overhead_us(struct perf_hpp *hpp)
{
	const char *fmt = symbol_conf.field_sep ? "%s" : "%7s";

	return scnprintf(hpp->buf, hpp->size, fmt, "user");
}

static int hpp__width_overhead_us(struct perf_hpp *hpp __maybe_unused)
{
	return 7;
}

static int hpp__color_overhead_us(struct perf_hpp *hpp, struct hist_entry *he)
{
	struct hists *hists = he->hists;
	double percent = 100.0 * he->stat.period_us / hists->stats.total_period;

	return percent_color_snprintf(hpp->buf, hpp->size, "%6.2f%%", percent);
}

static int hpp__entry_overhead_us(struct perf_hpp *hpp, struct hist_entry *he)
{
	struct hists *hists = he->hists;
	double percent = 100.0 * he->stat.period_us / hists->stats.total_period;
	const char *fmt = symbol_conf.field_sep ? "%.2f" : "%6.2f%%";

	return scnprintf(hpp->buf, hpp->size, fmt, percent);
}

static int hpp__header_overhead_guest_sys(struct perf_hpp *hpp)
{
	return scnprintf(hpp->buf, hpp->size, "guest sys");
}

static int hpp__width_overhead_guest_sys(struct perf_hpp *hpp __maybe_unused)
{
	return 9;
}

static int hpp__color_overhead_guest_sys(struct perf_hpp *hpp,
					 struct hist_entry *he)
{
	struct hists *hists = he->hists;
	double percent = 100.0 * he->stat.period_guest_sys / hists->stats.total_period;

	return percent_color_snprintf(hpp->buf, hpp->size, " %6.2f%% ", percent);
}

static int hpp__entry_overhead_guest_sys(struct perf_hpp *hpp,
					 struct hist_entry *he)
{
	struct hists *hists = he->hists;
	double percent = 100.0 * he->stat.period_guest_sys / hists->stats.total_period;
	const char *fmt = symbol_conf.field_sep ? "%.2f" : " %6.2f%% ";

	return scnprintf(hpp->buf, hpp->size, fmt, percent);
}

static int hpp__header_overhead_guest_us(struct perf_hpp *hpp)
{
	return scnprintf(hpp->buf, hpp->size, "guest usr");
}

static int hpp__width_overhead_guest_us(struct perf_hpp *hpp __maybe_unused)
{
	return 9;
}

static int hpp__color_overhead_guest_us(struct perf_hpp *hpp,
					struct hist_entry *he)
{
	struct hists *hists = he->hists;
	double percent = 100.0 * he->stat.period_guest_us / hists->stats.total_period;

	return percent_color_snprintf(hpp->buf, hpp->size, " %6.2f%% ", percent);
}

static int hpp__entry_overhead_guest_us(struct perf_hpp *hpp,
					struct hist_entry *he)
{
	struct hists *hists = he->hists;
	double percent = 100.0 * he->stat.period_guest_us / hists->stats.total_period;
	const char *fmt = symbol_conf.field_sep ? "%.2f" : " %6.2f%% ";

	return scnprintf(hpp->buf, hpp->size, fmt, percent);
}

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
	struct hist_entry *pair = he->pair;
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

	return percent_color_snprintf(hpp->buf, hpp->size, " %6.2f%%", percent);
}

static int hpp__entry_baseline(struct perf_hpp *hpp, struct hist_entry *he)
{
	double percent = baseline_percent(he);
	const char *fmt = symbol_conf.field_sep ? "%.2f" : " %6.2f%%";

	return scnprintf(hpp->buf, hpp->size, fmt, percent);
}

static int hpp__header_samples(struct perf_hpp *hpp)
{
	const char *fmt = symbol_conf.field_sep ? "%s" : "%11s";

	return scnprintf(hpp->buf, hpp->size, fmt, "Samples");
}

static int hpp__width_samples(struct perf_hpp *hpp __maybe_unused)
{
	return 11;
}

static int hpp__entry_samples(struct perf_hpp *hpp, struct hist_entry *he)
{
	const char *fmt = symbol_conf.field_sep ? "%" PRIu64 : "%11" PRIu64;

	return scnprintf(hpp->buf, hpp->size, fmt, he->stat.nr_events);
}

static int hpp__header_period(struct perf_hpp *hpp)
{
	const char *fmt = symbol_conf.field_sep ? "%s" : "%12s";

	return scnprintf(hpp->buf, hpp->size, fmt, "Period");
}

static int hpp__width_period(struct perf_hpp *hpp __maybe_unused)
{
	return 12;
}

static int hpp__entry_period(struct perf_hpp *hpp, struct hist_entry *he)
{
	const char *fmt = symbol_conf.field_sep ? "%" PRIu64 : "%12" PRIu64;

	return scnprintf(hpp->buf, hpp->size, fmt, he->stat.period);
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
	struct hist_entry *pair = he->pair;
	struct hists *pair_hists = pair ? pair->hists : NULL;
	struct hists *hists = he->hists;
	u64 old_total, new_total;
	double old_percent = 0, new_percent = 0;
	double diff;
	const char *fmt = symbol_conf.field_sep ? "%s" : "%7.7s";
	char buf[32] = " ";

	old_total = pair_hists ? pair_hists->stats.total_period : 0;
	if (old_total > 0 && pair)
		old_percent = 100.0 * pair->stat.period / old_total;

	new_total = hists->stats.total_period;
	if (new_total > 0)
		new_percent = 100.0 * he->stat.period / new_total;

	diff = new_percent - old_percent;
	if (fabs(diff) >= 0.01)
		scnprintf(buf, sizeof(buf), "%+4.2F%%", diff);

	return scnprintf(hpp->buf, hpp->size, fmt, buf);
}

static int hpp__header_displ(struct perf_hpp *hpp)
{
	return scnprintf(hpp->buf, hpp->size, "Displ.");
}

static int hpp__width_displ(struct perf_hpp *hpp __maybe_unused)
{
	return 6;
}

static int hpp__entry_displ(struct perf_hpp *hpp,
			    struct hist_entry *he)
{
	struct hist_entry *pair = he->pair;
	long displacement = pair ? pair->position - he->position : 0;
	const char *fmt = symbol_conf.field_sep ? "%s" : "%6.6s";
	char buf[32] = " ";

	if (displacement)
		scnprintf(buf, sizeof(buf), "%+4ld", displacement);

	return scnprintf(hpp->buf, hpp->size, fmt, buf);
}

#define HPP__COLOR_PRINT_FNS(_name)		\
	.header	= hpp__header_ ## _name,		\
	.width	= hpp__width_ ## _name,		\
	.color	= hpp__color_ ## _name,		\
	.entry	= hpp__entry_ ## _name

#define HPP__PRINT_FNS(_name)			\
	.header	= hpp__header_ ## _name,		\
	.width	= hpp__width_ ## _name,		\
	.entry	= hpp__entry_ ## _name

struct perf_hpp_fmt perf_hpp__format[] = {
	{ .cond = false, HPP__COLOR_PRINT_FNS(baseline) },
	{ .cond = true,  HPP__COLOR_PRINT_FNS(overhead) },
	{ .cond = false, HPP__COLOR_PRINT_FNS(overhead_sys) },
	{ .cond = false, HPP__COLOR_PRINT_FNS(overhead_us) },
	{ .cond = false, HPP__COLOR_PRINT_FNS(overhead_guest_sys) },
	{ .cond = false, HPP__COLOR_PRINT_FNS(overhead_guest_us) },
	{ .cond = false, HPP__PRINT_FNS(samples) },
	{ .cond = false, HPP__PRINT_FNS(period) },
	{ .cond = false, HPP__PRINT_FNS(delta) },
	{ .cond = false, HPP__PRINT_FNS(displ) }
};

#undef HPP__COLOR_PRINT_FNS
#undef HPP__PRINT_FNS

void perf_hpp__init(void)
{
	if (symbol_conf.show_cpu_utilization) {
		perf_hpp__format[PERF_HPP__OVERHEAD_SYS].cond = true;
		perf_hpp__format[PERF_HPP__OVERHEAD_US].cond = true;

		if (perf_guest) {
			perf_hpp__format[PERF_HPP__OVERHEAD_GUEST_SYS].cond = true;
			perf_hpp__format[PERF_HPP__OVERHEAD_GUEST_US].cond = true;
		}
	}

	if (symbol_conf.show_nr_samples)
		perf_hpp__format[PERF_HPP__SAMPLES].cond = true;

	if (symbol_conf.show_total_period)
		perf_hpp__format[PERF_HPP__PERIOD].cond = true;
}

void perf_hpp__column_enable(unsigned col, bool enable)
{
	BUG_ON(col >= PERF_HPP__MAX_INDEX);
	perf_hpp__format[col].cond = enable;
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
	char *start = hpp->buf;
	int i, ret;
	bool first = true;

	if (symbol_conf.exclude_other && !he->parent)
		return 0;

	for (i = 0; i < PERF_HPP__MAX_INDEX; i++) {
		if (!perf_hpp__format[i].cond)
			continue;

		if (!sep || !first) {
			ret = scnprintf(hpp->buf, hpp->size, "%s", sep ?: "  ");
			advance_hpp(hpp, ret);
			first = false;
		}

		if (color && perf_hpp__format[i].color)
			ret = perf_hpp__format[i].color(hpp, he);
		else
			ret = perf_hpp__format[i].entry(hpp, he);

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
	struct sort_entry *se;
	int i, ret = 0;

	for (i = 0; i < PERF_HPP__MAX_INDEX; i++) {
		if (!perf_hpp__format[i].cond)
			continue;
		if (i)
			ret += 2;

		ret += perf_hpp__format[i].width(NULL);
	}

	list_for_each_entry(se, &hist_entry__sort_list, list)
		if (!se->elide)
			ret += 2 + hists__col_len(hists, se->se_width_idx);

	if (verbose) /* Addr + origin */
		ret += 3 + BITS_PER_LONG / 4;

	return ret;
}
