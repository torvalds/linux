#include <math.h>
#include <linux/compiler.h>

#include "../util/hist.h"
#include "../util/util.h"
#include "../util/sort.h"
#include "../util/evsel.h"

/* hist period print (hpp) functions */

#define hpp__call_print_fn(hpp, fn, fmt, ...)			\
({								\
	int __ret = fn(hpp, fmt, ##__VA_ARGS__);		\
	advance_hpp(hpp, __ret);				\
	__ret;							\
})

int __hpp__fmt(struct perf_hpp *hpp, struct hist_entry *he,
	       hpp_field_fn get_field, const char *fmt,
	       hpp_snprint_fn print_fn, bool fmt_percent)
{
	int ret;
	struct hists *hists = he->hists;
	struct perf_evsel *evsel = hists_to_evsel(hists);
	char *buf = hpp->buf;
	size_t size = hpp->size;

	if (fmt_percent) {
		double percent = 0.0;
		u64 total = hists__total_period(hists);

		if (total)
			percent = 100.0 * get_field(he) / total;

		ret = hpp__call_print_fn(hpp, print_fn, fmt, percent);
	} else
		ret = hpp__call_print_fn(hpp, print_fn, fmt, get_field(he));

	if (perf_evsel__is_group_event(evsel)) {
		int prev_idx, idx_delta;
		struct hist_entry *pair;
		int nr_members = evsel->nr_members;

		prev_idx = perf_evsel__group_idx(evsel);

		list_for_each_entry(pair, &he->pairs.head, pairs.node) {
			u64 period = get_field(pair);
			u64 total = hists__total_period(pair->hists);

			if (!total)
				continue;

			evsel = hists_to_evsel(pair->hists);
			idx_delta = perf_evsel__group_idx(evsel) - prev_idx - 1;

			while (idx_delta--) {
				/*
				 * zero-fill group members in the middle which
				 * have no sample
				 */
				if (fmt_percent) {
					ret += hpp__call_print_fn(hpp, print_fn,
								  fmt, 0.0);
				} else {
					ret += hpp__call_print_fn(hpp, print_fn,
								  fmt, 0ULL);
				}
			}

			if (fmt_percent) {
				ret += hpp__call_print_fn(hpp, print_fn, fmt,
							  100.0 * period / total);
			} else {
				ret += hpp__call_print_fn(hpp, print_fn, fmt,
							  period);
			}

			prev_idx = perf_evsel__group_idx(evsel);
		}

		idx_delta = nr_members - prev_idx - 1;

		while (idx_delta--) {
			/*
			 * zero-fill group members at last which have no sample
			 */
			if (fmt_percent) {
				ret += hpp__call_print_fn(hpp, print_fn,
							  fmt, 0.0);
			} else {
				ret += hpp__call_print_fn(hpp, print_fn,
							  fmt, 0ULL);
			}
		}
	}

	/*
	 * Restore original buf and size as it's where caller expects
	 * the result will be saved.
	 */
	hpp->buf = buf;
	hpp->size = size;

	return ret;
}

static int field_cmp(u64 field_a, u64 field_b)
{
	if (field_a > field_b)
		return 1;
	if (field_a < field_b)
		return -1;
	return 0;
}

static int __hpp__sort(struct hist_entry *a, struct hist_entry *b,
		       hpp_field_fn get_field)
{
	s64 ret;
	int i, nr_members;
	struct perf_evsel *evsel;
	struct hist_entry *pair;
	u64 *fields_a, *fields_b;

	ret = field_cmp(get_field(a), get_field(b));
	if (ret || !symbol_conf.event_group)
		return ret;

	evsel = hists_to_evsel(a->hists);
	if (!perf_evsel__is_group_event(evsel))
		return ret;

	nr_members = evsel->nr_members;
	fields_a = calloc(sizeof(*fields_a), nr_members);
	fields_b = calloc(sizeof(*fields_b), nr_members);

	if (!fields_a || !fields_b)
		goto out;

	list_for_each_entry(pair, &a->pairs.head, pairs.node) {
		evsel = hists_to_evsel(pair->hists);
		fields_a[perf_evsel__group_idx(evsel)] = get_field(pair);
	}

	list_for_each_entry(pair, &b->pairs.head, pairs.node) {
		evsel = hists_to_evsel(pair->hists);
		fields_b[perf_evsel__group_idx(evsel)] = get_field(pair);
	}

	for (i = 1; i < nr_members; i++) {
		ret = field_cmp(fields_a[i], fields_b[i]);
		if (ret)
			break;
	}

out:
	free(fields_a);
	free(fields_b);

	return ret;
}

#define __HPP_HEADER_FN(_type, _str, _min_width, _unit_width) 		\
static int hpp__header_##_type(struct perf_hpp_fmt *fmt __maybe_unused,	\
			       struct perf_hpp *hpp,			\
			       struct perf_evsel *evsel)		\
{									\
	int len = _min_width;						\
									\
	if (symbol_conf.event_group)					\
		len = max(len, evsel->nr_members * _unit_width);	\
									\
	return scnprintf(hpp->buf, hpp->size, "%*s", len, _str);	\
}

#define __HPP_WIDTH_FN(_type, _min_width, _unit_width) 			\
static int hpp__width_##_type(struct perf_hpp_fmt *fmt __maybe_unused,	\
			      struct perf_hpp *hpp __maybe_unused,	\
			      struct perf_evsel *evsel)			\
{									\
	int len = _min_width;						\
									\
	if (symbol_conf.event_group)					\
		len = max(len, evsel->nr_members * _unit_width);	\
									\
	return len;							\
}

static int hpp_color_scnprintf(struct perf_hpp *hpp, const char *fmt, ...)
{
	va_list args;
	ssize_t ssize = hpp->size;
	double percent;
	int ret;

	va_start(args, fmt);
	percent = va_arg(args, double);
	ret = value_color_snprintf(hpp->buf, hpp->size, fmt, percent);
	va_end(args);

	return (ret >= ssize) ? (ssize - 1) : ret;
}

static int hpp_entry_scnprintf(struct perf_hpp *hpp, const char *fmt, ...)
{
	va_list args;
	ssize_t ssize = hpp->size;
	int ret;

	va_start(args, fmt);
	ret = vsnprintf(hpp->buf, hpp->size, fmt, args);
	va_end(args);

	return (ret >= ssize) ? (ssize - 1) : ret;
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
			  hpp_color_scnprintf, true);				\
}

#define __HPP_ENTRY_PERCENT_FN(_type, _field)					\
static int hpp__entry_##_type(struct perf_hpp_fmt *_fmt __maybe_unused,		\
			      struct perf_hpp *hpp, struct hist_entry *he) 	\
{										\
	const char *fmt = symbol_conf.field_sep ? " %.2f" : " %6.2f%%";		\
	return __hpp__fmt(hpp, he, he_get_##_field, fmt,			\
			  hpp_entry_scnprintf, true);				\
}

#define __HPP_SORT_FN(_type, _field)						\
static int64_t hpp__sort_##_type(struct hist_entry *a, struct hist_entry *b)	\
{										\
	return __hpp__sort(a, b, he_get_##_field);				\
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
	return __hpp__fmt(hpp, he, he_get_raw_##_field, fmt,			\
			  hpp_entry_scnprintf, false);				\
}

#define __HPP_SORT_RAW_FN(_type, _field)					\
static int64_t hpp__sort_##_type(struct hist_entry *a, struct hist_entry *b)	\
{										\
	return __hpp__sort(a, b, he_get_raw_##_field);				\
}


#define HPP_PERCENT_FNS(_type, _str, _field, _min_width, _unit_width)	\
__HPP_HEADER_FN(_type, _str, _min_width, _unit_width)			\
__HPP_WIDTH_FN(_type, _min_width, _unit_width)				\
__HPP_COLOR_PERCENT_FN(_type, _field)					\
__HPP_ENTRY_PERCENT_FN(_type, _field)					\
__HPP_SORT_FN(_type, _field)

#define HPP_RAW_FNS(_type, _str, _field, _min_width, _unit_width)	\
__HPP_HEADER_FN(_type, _str, _min_width, _unit_width)			\
__HPP_WIDTH_FN(_type, _min_width, _unit_width)				\
__HPP_ENTRY_RAW_FN(_type, _field)					\
__HPP_SORT_RAW_FN(_type, _field)


HPP_PERCENT_FNS(overhead, "Overhead", period, 8, 8)
HPP_PERCENT_FNS(overhead_sys, "sys", period_sys, 8, 8)
HPP_PERCENT_FNS(overhead_us, "usr", period_us, 8, 8)
HPP_PERCENT_FNS(overhead_guest_sys, "guest sys", period_guest_sys, 9, 8)
HPP_PERCENT_FNS(overhead_guest_us, "guest usr", period_guest_us, 9, 8)

HPP_RAW_FNS(samples, "Samples", nr_events, 12, 12)
HPP_RAW_FNS(period, "Period", period, 12, 12)

static int64_t hpp__nop_cmp(struct hist_entry *a __maybe_unused,
			    struct hist_entry *b __maybe_unused)
{
	return 0;
}

#define HPP__COLOR_PRINT_FNS(_name)			\
	{						\
		.header	= hpp__header_ ## _name,	\
		.width	= hpp__width_ ## _name,		\
		.color	= hpp__color_ ## _name,		\
		.entry	= hpp__entry_ ## _name,		\
		.cmp	= hpp__nop_cmp,			\
		.collapse = hpp__nop_cmp,		\
		.sort	= hpp__sort_ ## _name,		\
	}

#define HPP__PRINT_FNS(_name)				\
	{						\
		.header	= hpp__header_ ## _name,	\
		.width	= hpp__width_ ## _name,		\
		.entry	= hpp__entry_ ## _name,		\
		.cmp	= hpp__nop_cmp,			\
		.collapse = hpp__nop_cmp,		\
		.sort	= hpp__sort_ ## _name,		\
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
LIST_HEAD(perf_hpp__sort_list);


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
	struct list_head *list;
	int i;

	for (i = 0; i < PERF_HPP__MAX_INDEX; i++) {
		struct perf_hpp_fmt *fmt = &perf_hpp__format[i];

		INIT_LIST_HEAD(&fmt->list);

		/* sort_list may be linked by setup_sorting() */
		if (fmt->sort_list.next == NULL)
			INIT_LIST_HEAD(&fmt->sort_list);
	}

	/*
	 * If user specified field order, no need to setup default fields.
	 */
	if (field_order)
		return;

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

	/* prepend overhead field for backward compatiblity.  */
	list = &perf_hpp__format[PERF_HPP__OVERHEAD].sort_list;
	if (list_empty(list))
		list_add(list, &perf_hpp__sort_list);
}

void perf_hpp__column_register(struct perf_hpp_fmt *format)
{
	list_add_tail(&format->list, &perf_hpp__list);
}

void perf_hpp__register_sort_field(struct perf_hpp_fmt *format)
{
	list_add_tail(&format->sort_list, &perf_hpp__sort_list);
}

void perf_hpp__column_enable(unsigned col)
{
	BUG_ON(col >= PERF_HPP__MAX_INDEX);
	perf_hpp__column_register(&perf_hpp__format[col]);
}

void perf_hpp__setup_output_field(void)
{
	struct perf_hpp_fmt *fmt;

	/* append sort keys to output field */
	perf_hpp__for_each_sort_list(fmt) {
		if (!list_empty(&fmt->list))
			continue;

		/*
		 * sort entry fields are dynamically created,
		 * so they can share a same sort key even though
		 * the list is empty.
		 */
		if (perf_hpp__is_sort_entry(fmt)) {
			struct perf_hpp_fmt *pos;

			perf_hpp__for_each_format(pos) {
				if (perf_hpp__same_sort_entry(pos, fmt))
					goto next;
			}
		}

		perf_hpp__column_register(fmt);
next:
		continue;
	}
}

void perf_hpp__append_sort_keys(void)
{
	struct perf_hpp_fmt *fmt;

	/* append output fields to sort keys */
	perf_hpp__for_each_format(fmt) {
		if (!list_empty(&fmt->sort_list))
			continue;

		/*
		 * sort entry fields are dynamically created,
		 * so they can share a same sort key even though
		 * the list is empty.
		 */
		if (perf_hpp__is_sort_entry(fmt)) {
			struct perf_hpp_fmt *pos;

			perf_hpp__for_each_sort_list(pos) {
				if (perf_hpp__same_sort_entry(pos, fmt))
					goto next;
			}
		}

		perf_hpp__register_sort_field(fmt);
next:
		continue;
	}
}

void perf_hpp__reset_output_field(void)
{
	struct perf_hpp_fmt *fmt, *tmp;

	/* reset output fields */
	perf_hpp__for_each_format_safe(fmt, tmp) {
		list_del_init(&fmt->list);
		list_del_init(&fmt->sort_list);
	}

	/* reset sort keys */
	perf_hpp__for_each_sort_list_safe(fmt, tmp) {
		list_del_init(&fmt->list);
		list_del_init(&fmt->sort_list);
	}
}

/*
 * See hists__fprintf to match the column widths
 */
unsigned int hists__sort_list_width(struct hists *hists)
{
	struct perf_hpp_fmt *fmt;
	int ret = 0;
	bool first = true;
	struct perf_hpp dummy_hpp;

	perf_hpp__for_each_format(fmt) {
		if (perf_hpp__should_skip(fmt))
			continue;

		if (first)
			first = false;
		else
			ret += 2;

		ret += fmt->width(fmt, &dummy_hpp, hists_to_evsel(hists));
	}

	if (verbose && sort__has_sym) /* Addr + origin */
		ret += 3 + BITS_PER_LONG / 4;

	return ret;
}
