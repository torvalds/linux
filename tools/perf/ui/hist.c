#include <inttypes.h>
#include <math.h>
#include <linux/compiler.h>

#include "../util/hist.h"
#include "../util/util.h"
#include "../util/sort.h"
#include "../util/evsel.h"
#include "../util/evlist.h"

/* hist period print (hpp) functions */

#define hpp__call_print_fn(hpp, fn, fmt, ...)			\
({								\
	int __ret = fn(hpp, fmt, ##__VA_ARGS__);		\
	advance_hpp(hpp, __ret);				\
	__ret;							\
})

static int __hpp__fmt(struct perf_hpp *hpp, struct hist_entry *he,
		      hpp_field_fn get_field, const char *fmt, int len,
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

		ret = hpp__call_print_fn(hpp, print_fn, fmt, len, percent);
	} else
		ret = hpp__call_print_fn(hpp, print_fn, fmt, len, get_field(he));

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
								  fmt, len, 0.0);
				} else {
					ret += hpp__call_print_fn(hpp, print_fn,
								  fmt, len, 0ULL);
				}
			}

			if (fmt_percent) {
				ret += hpp__call_print_fn(hpp, print_fn, fmt, len,
							  100.0 * period / total);
			} else {
				ret += hpp__call_print_fn(hpp, print_fn, fmt,
							  len, period);
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
							  fmt, len, 0.0);
			} else {
				ret += hpp__call_print_fn(hpp, print_fn,
							  fmt, len, 0ULL);
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

int hpp__fmt(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
	     struct hist_entry *he, hpp_field_fn get_field,
	     const char *fmtstr, hpp_snprint_fn print_fn, bool fmt_percent)
{
	int len = fmt->user_len ?: fmt->len;

	if (symbol_conf.field_sep) {
		return __hpp__fmt(hpp, he, get_field, fmtstr, 1,
				  print_fn, fmt_percent);
	}

	if (fmt_percent)
		len -= 2; /* 2 for a space and a % sign */
	else
		len -= 1;

	return  __hpp__fmt(hpp, he, get_field, fmtstr, len, print_fn, fmt_percent);
}

int hpp__fmt_acc(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		 struct hist_entry *he, hpp_field_fn get_field,
		 const char *fmtstr, hpp_snprint_fn print_fn, bool fmt_percent)
{
	if (!symbol_conf.cumulate_callchain) {
		int len = fmt->user_len ?: fmt->len;
		return snprintf(hpp->buf, hpp->size, " %*s", len - 1, "N/A");
	}

	return hpp__fmt(fmt, hpp, he, get_field, fmtstr, print_fn, fmt_percent);
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
	fields_a = calloc(nr_members, sizeof(*fields_a));
	fields_b = calloc(nr_members, sizeof(*fields_b));

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

static int __hpp__sort_acc(struct hist_entry *a, struct hist_entry *b,
			   hpp_field_fn get_field)
{
	s64 ret = 0;

	if (symbol_conf.cumulate_callchain) {
		/*
		 * Put caller above callee when they have equal period.
		 */
		ret = field_cmp(get_field(a), get_field(b));
		if (ret)
			return ret;

		if (a->thread != b->thread || !symbol_conf.use_callchain)
			return 0;

		ret = b->callchain->max_depth - a->callchain->max_depth;
	}
	return ret;
}

static int hpp__width_fn(struct perf_hpp_fmt *fmt,
			 struct perf_hpp *hpp __maybe_unused,
			 struct hists *hists)
{
	int len = fmt->user_len ?: fmt->len;
	struct perf_evsel *evsel = hists_to_evsel(hists);

	if (symbol_conf.event_group)
		len = max(len, evsel->nr_members * fmt->len);

	if (len < (int)strlen(fmt->name))
		len = strlen(fmt->name);

	return len;
}

static int hpp__header_fn(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
			  struct hists *hists, int line __maybe_unused,
			  int *span __maybe_unused)
{
	int len = hpp__width_fn(fmt, hpp, hists);
	return scnprintf(hpp->buf, hpp->size, "%*s", len, fmt->name);
}

int hpp_color_scnprintf(struct perf_hpp *hpp, const char *fmt, ...)
{
	va_list args;
	ssize_t ssize = hpp->size;
	double percent;
	int ret, len;

	va_start(args, fmt);
	len = va_arg(args, int);
	percent = va_arg(args, double);
	ret = percent_color_len_snprintf(hpp->buf, hpp->size, fmt, len, percent);
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
static int hpp__color_##_type(struct perf_hpp_fmt *fmt,				\
			      struct perf_hpp *hpp, struct hist_entry *he) 	\
{										\
	return hpp__fmt(fmt, hpp, he, he_get_##_field, " %*.2f%%",		\
			hpp_color_scnprintf, true);				\
}

#define __HPP_ENTRY_PERCENT_FN(_type, _field)					\
static int hpp__entry_##_type(struct perf_hpp_fmt *fmt,				\
			      struct perf_hpp *hpp, struct hist_entry *he) 	\
{										\
	return hpp__fmt(fmt, hpp, he, he_get_##_field, " %*.2f%%",		\
			hpp_entry_scnprintf, true);				\
}

#define __HPP_SORT_FN(_type, _field)						\
static int64_t hpp__sort_##_type(struct perf_hpp_fmt *fmt __maybe_unused, 	\
				 struct hist_entry *a, struct hist_entry *b) 	\
{										\
	return __hpp__sort(a, b, he_get_##_field);				\
}

#define __HPP_COLOR_ACC_PERCENT_FN(_type, _field)				\
static u64 he_get_acc_##_field(struct hist_entry *he)				\
{										\
	return he->stat_acc->_field;						\
}										\
										\
static int hpp__color_##_type(struct perf_hpp_fmt *fmt,				\
			      struct perf_hpp *hpp, struct hist_entry *he) 	\
{										\
	return hpp__fmt_acc(fmt, hpp, he, he_get_acc_##_field, " %*.2f%%", 	\
			    hpp_color_scnprintf, true);				\
}

#define __HPP_ENTRY_ACC_PERCENT_FN(_type, _field)				\
static int hpp__entry_##_type(struct perf_hpp_fmt *fmt,				\
			      struct perf_hpp *hpp, struct hist_entry *he) 	\
{										\
	return hpp__fmt_acc(fmt, hpp, he, he_get_acc_##_field, " %*.2f%%",	\
			    hpp_entry_scnprintf, true);				\
}

#define __HPP_SORT_ACC_FN(_type, _field)					\
static int64_t hpp__sort_##_type(struct perf_hpp_fmt *fmt __maybe_unused, 	\
				 struct hist_entry *a, struct hist_entry *b) 	\
{										\
	return __hpp__sort_acc(a, b, he_get_acc_##_field);			\
}

#define __HPP_ENTRY_RAW_FN(_type, _field)					\
static u64 he_get_raw_##_field(struct hist_entry *he)				\
{										\
	return he->stat._field;							\
}										\
										\
static int hpp__entry_##_type(struct perf_hpp_fmt *fmt,				\
			      struct perf_hpp *hpp, struct hist_entry *he) 	\
{										\
	return hpp__fmt(fmt, hpp, he, he_get_raw_##_field, " %*"PRIu64, 	\
			hpp_entry_scnprintf, false);				\
}

#define __HPP_SORT_RAW_FN(_type, _field)					\
static int64_t hpp__sort_##_type(struct perf_hpp_fmt *fmt __maybe_unused, 	\
				 struct hist_entry *a, struct hist_entry *b) 	\
{										\
	return __hpp__sort(a, b, he_get_raw_##_field);				\
}


#define HPP_PERCENT_FNS(_type, _field)					\
__HPP_COLOR_PERCENT_FN(_type, _field)					\
__HPP_ENTRY_PERCENT_FN(_type, _field)					\
__HPP_SORT_FN(_type, _field)

#define HPP_PERCENT_ACC_FNS(_type, _field)				\
__HPP_COLOR_ACC_PERCENT_FN(_type, _field)				\
__HPP_ENTRY_ACC_PERCENT_FN(_type, _field)				\
__HPP_SORT_ACC_FN(_type, _field)

#define HPP_RAW_FNS(_type, _field)					\
__HPP_ENTRY_RAW_FN(_type, _field)					\
__HPP_SORT_RAW_FN(_type, _field)

HPP_PERCENT_FNS(overhead, period)
HPP_PERCENT_FNS(overhead_sys, period_sys)
HPP_PERCENT_FNS(overhead_us, period_us)
HPP_PERCENT_FNS(overhead_guest_sys, period_guest_sys)
HPP_PERCENT_FNS(overhead_guest_us, period_guest_us)
HPP_PERCENT_ACC_FNS(overhead_acc, period)

HPP_RAW_FNS(samples, nr_events)
HPP_RAW_FNS(period, period)

static int64_t hpp__nop_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
			    struct hist_entry *a __maybe_unused,
			    struct hist_entry *b __maybe_unused)
{
	return 0;
}

static bool perf_hpp__is_hpp_entry(struct perf_hpp_fmt *a)
{
	return a->header == hpp__header_fn;
}

static bool hpp__equal(struct perf_hpp_fmt *a, struct perf_hpp_fmt *b)
{
	if (!perf_hpp__is_hpp_entry(a) || !perf_hpp__is_hpp_entry(b))
		return false;

	return a->idx == b->idx;
}

#define HPP__COLOR_PRINT_FNS(_name, _fn, _idx)		\
	{						\
		.name   = _name,			\
		.header	= hpp__header_fn,		\
		.width	= hpp__width_fn,		\
		.color	= hpp__color_ ## _fn,		\
		.entry	= hpp__entry_ ## _fn,		\
		.cmp	= hpp__nop_cmp,			\
		.collapse = hpp__nop_cmp,		\
		.sort	= hpp__sort_ ## _fn,		\
		.idx	= PERF_HPP__ ## _idx,		\
		.equal	= hpp__equal,			\
	}

#define HPP__COLOR_ACC_PRINT_FNS(_name, _fn, _idx)	\
	{						\
		.name   = _name,			\
		.header	= hpp__header_fn,		\
		.width	= hpp__width_fn,		\
		.color	= hpp__color_ ## _fn,		\
		.entry	= hpp__entry_ ## _fn,		\
		.cmp	= hpp__nop_cmp,			\
		.collapse = hpp__nop_cmp,		\
		.sort	= hpp__sort_ ## _fn,		\
		.idx	= PERF_HPP__ ## _idx,		\
		.equal	= hpp__equal,			\
	}

#define HPP__PRINT_FNS(_name, _fn, _idx)		\
	{						\
		.name   = _name,			\
		.header	= hpp__header_fn,		\
		.width	= hpp__width_fn,		\
		.entry	= hpp__entry_ ## _fn,		\
		.cmp	= hpp__nop_cmp,			\
		.collapse = hpp__nop_cmp,		\
		.sort	= hpp__sort_ ## _fn,		\
		.idx	= PERF_HPP__ ## _idx,		\
		.equal	= hpp__equal,			\
	}

struct perf_hpp_fmt perf_hpp__format[] = {
	HPP__COLOR_PRINT_FNS("Overhead", overhead, OVERHEAD),
	HPP__COLOR_PRINT_FNS("sys", overhead_sys, OVERHEAD_SYS),
	HPP__COLOR_PRINT_FNS("usr", overhead_us, OVERHEAD_US),
	HPP__COLOR_PRINT_FNS("guest sys", overhead_guest_sys, OVERHEAD_GUEST_SYS),
	HPP__COLOR_PRINT_FNS("guest usr", overhead_guest_us, OVERHEAD_GUEST_US),
	HPP__COLOR_ACC_PRINT_FNS("Children", overhead_acc, OVERHEAD_ACC),
	HPP__PRINT_FNS("Samples", samples, SAMPLES),
	HPP__PRINT_FNS("Period", period, PERIOD)
};

struct perf_hpp_list perf_hpp_list = {
	.fields	= LIST_HEAD_INIT(perf_hpp_list.fields),
	.sorts	= LIST_HEAD_INIT(perf_hpp_list.sorts),
	.nr_header_lines = 1,
};

#undef HPP__COLOR_PRINT_FNS
#undef HPP__COLOR_ACC_PRINT_FNS
#undef HPP__PRINT_FNS

#undef HPP_PERCENT_FNS
#undef HPP_PERCENT_ACC_FNS
#undef HPP_RAW_FNS

#undef __HPP_HEADER_FN
#undef __HPP_WIDTH_FN
#undef __HPP_COLOR_PERCENT_FN
#undef __HPP_ENTRY_PERCENT_FN
#undef __HPP_COLOR_ACC_PERCENT_FN
#undef __HPP_ENTRY_ACC_PERCENT_FN
#undef __HPP_ENTRY_RAW_FN
#undef __HPP_SORT_FN
#undef __HPP_SORT_ACC_FN
#undef __HPP_SORT_RAW_FN


void perf_hpp__init(void)
{
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
	if (is_strict_order(field_order))
		return;

	if (symbol_conf.cumulate_callchain) {
		hpp_dimension__add_output(PERF_HPP__OVERHEAD_ACC);
		perf_hpp__format[PERF_HPP__OVERHEAD].name = "Self";
	}

	hpp_dimension__add_output(PERF_HPP__OVERHEAD);

	if (symbol_conf.show_cpu_utilization) {
		hpp_dimension__add_output(PERF_HPP__OVERHEAD_SYS);
		hpp_dimension__add_output(PERF_HPP__OVERHEAD_US);

		if (perf_guest) {
			hpp_dimension__add_output(PERF_HPP__OVERHEAD_GUEST_SYS);
			hpp_dimension__add_output(PERF_HPP__OVERHEAD_GUEST_US);
		}
	}

	if (symbol_conf.show_nr_samples)
		hpp_dimension__add_output(PERF_HPP__SAMPLES);

	if (symbol_conf.show_total_period)
		hpp_dimension__add_output(PERF_HPP__PERIOD);
}

void perf_hpp_list__column_register(struct perf_hpp_list *list,
				    struct perf_hpp_fmt *format)
{
	list_add_tail(&format->list, &list->fields);
}

void perf_hpp_list__register_sort_field(struct perf_hpp_list *list,
					struct perf_hpp_fmt *format)
{
	list_add_tail(&format->sort_list, &list->sorts);
}

void perf_hpp_list__prepend_sort_field(struct perf_hpp_list *list,
				       struct perf_hpp_fmt *format)
{
	list_add(&format->sort_list, &list->sorts);
}

void perf_hpp__column_unregister(struct perf_hpp_fmt *format)
{
	list_del(&format->list);
}

void perf_hpp__cancel_cumulate(void)
{
	struct perf_hpp_fmt *fmt, *acc, *ovh, *tmp;

	if (is_strict_order(field_order))
		return;

	ovh = &perf_hpp__format[PERF_HPP__OVERHEAD];
	acc = &perf_hpp__format[PERF_HPP__OVERHEAD_ACC];

	perf_hpp_list__for_each_format_safe(&perf_hpp_list, fmt, tmp) {
		if (acc->equal(acc, fmt)) {
			perf_hpp__column_unregister(fmt);
			continue;
		}

		if (ovh->equal(ovh, fmt))
			fmt->name = "Overhead";
	}
}

static bool fmt_equal(struct perf_hpp_fmt *a, struct perf_hpp_fmt *b)
{
	return a->equal && a->equal(a, b);
}

void perf_hpp__setup_output_field(struct perf_hpp_list *list)
{
	struct perf_hpp_fmt *fmt;

	/* append sort keys to output field */
	perf_hpp_list__for_each_sort_list(list, fmt) {
		struct perf_hpp_fmt *pos;

		/* skip sort-only fields ("sort_compute" in perf diff) */
		if (!fmt->entry && !fmt->color)
			continue;

		perf_hpp_list__for_each_format(list, pos) {
			if (fmt_equal(fmt, pos))
				goto next;
		}

		perf_hpp__column_register(fmt);
next:
		continue;
	}
}

void perf_hpp__append_sort_keys(struct perf_hpp_list *list)
{
	struct perf_hpp_fmt *fmt;

	/* append output fields to sort keys */
	perf_hpp_list__for_each_format(list, fmt) {
		struct perf_hpp_fmt *pos;

		perf_hpp_list__for_each_sort_list(list, pos) {
			if (fmt_equal(fmt, pos))
				goto next;
		}

		perf_hpp__register_sort_field(fmt);
next:
		continue;
	}
}


static void fmt_free(struct perf_hpp_fmt *fmt)
{
	if (fmt->free)
		fmt->free(fmt);
}

void perf_hpp__reset_output_field(struct perf_hpp_list *list)
{
	struct perf_hpp_fmt *fmt, *tmp;

	/* reset output fields */
	perf_hpp_list__for_each_format_safe(list, fmt, tmp) {
		list_del_init(&fmt->list);
		list_del_init(&fmt->sort_list);
		fmt_free(fmt);
	}

	/* reset sort keys */
	perf_hpp_list__for_each_sort_list_safe(list, fmt, tmp) {
		list_del_init(&fmt->list);
		list_del_init(&fmt->sort_list);
		fmt_free(fmt);
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

	hists__for_each_format(hists, fmt) {
		if (perf_hpp__should_skip(fmt, hists))
			continue;

		if (first)
			first = false;
		else
			ret += 2;

		ret += fmt->width(fmt, &dummy_hpp, hists);
	}

	if (verbose > 0 && hists__has(hists, sym)) /* Addr + origin */
		ret += 3 + BITS_PER_LONG / 4;

	return ret;
}

unsigned int hists__overhead_width(struct hists *hists)
{
	struct perf_hpp_fmt *fmt;
	int ret = 0;
	bool first = true;
	struct perf_hpp dummy_hpp;

	hists__for_each_format(hists, fmt) {
		if (perf_hpp__is_sort_entry(fmt) || perf_hpp__is_dynamic_entry(fmt))
			break;

		if (first)
			first = false;
		else
			ret += 2;

		ret += fmt->width(fmt, &dummy_hpp, hists);
	}

	return ret;
}

void perf_hpp__reset_width(struct perf_hpp_fmt *fmt, struct hists *hists)
{
	if (perf_hpp__is_sort_entry(fmt))
		return perf_hpp__reset_sort_width(fmt, hists);

	if (perf_hpp__is_dynamic_entry(fmt))
		return;

	BUG_ON(fmt->idx >= PERF_HPP__MAX_INDEX);

	switch (fmt->idx) {
	case PERF_HPP__OVERHEAD:
	case PERF_HPP__OVERHEAD_SYS:
	case PERF_HPP__OVERHEAD_US:
	case PERF_HPP__OVERHEAD_ACC:
		fmt->len = 8;
		break;

	case PERF_HPP__OVERHEAD_GUEST_SYS:
	case PERF_HPP__OVERHEAD_GUEST_US:
		fmt->len = 9;
		break;

	case PERF_HPP__SAMPLES:
	case PERF_HPP__PERIOD:
		fmt->len = 12;
		break;

	default:
		break;
	}
}

void hists__reset_column_width(struct hists *hists)
{
	struct perf_hpp_fmt *fmt;
	struct perf_hpp_list_node *node;

	hists__for_each_format(hists, fmt)
		perf_hpp__reset_width(fmt, hists);

	/* hierarchy entries have their own hpp list */
	list_for_each_entry(node, &hists->hpp_formats, list) {
		perf_hpp_list__for_each_format(&node->hpp, fmt)
			perf_hpp__reset_width(fmt, hists);
	}
}

void perf_hpp__set_user_width(const char *width_list_str)
{
	struct perf_hpp_fmt *fmt;
	const char *ptr = width_list_str;

	perf_hpp_list__for_each_format(&perf_hpp_list, fmt) {
		char *p;

		int len = strtol(ptr, &p, 10);
		fmt->user_len = len;

		if (*p == ',')
			ptr = p + 1;
		else
			break;
	}
}

static int add_hierarchy_fmt(struct hists *hists, struct perf_hpp_fmt *fmt)
{
	struct perf_hpp_list_node *node = NULL;
	struct perf_hpp_fmt *fmt_copy;
	bool found = false;
	bool skip = perf_hpp__should_skip(fmt, hists);

	list_for_each_entry(node, &hists->hpp_formats, list) {
		if (node->level == fmt->level) {
			found = true;
			break;
		}
	}

	if (!found) {
		node = malloc(sizeof(*node));
		if (node == NULL)
			return -1;

		node->skip = skip;
		node->level = fmt->level;
		perf_hpp_list__init(&node->hpp);

		hists->nr_hpp_node++;
		list_add_tail(&node->list, &hists->hpp_formats);
	}

	fmt_copy = perf_hpp_fmt__dup(fmt);
	if (fmt_copy == NULL)
		return -1;

	if (!skip)
		node->skip = false;

	list_add_tail(&fmt_copy->list, &node->hpp.fields);
	list_add_tail(&fmt_copy->sort_list, &node->hpp.sorts);

	return 0;
}

int perf_hpp__setup_hists_formats(struct perf_hpp_list *list,
				  struct perf_evlist *evlist)
{
	struct perf_evsel *evsel;
	struct perf_hpp_fmt *fmt;
	struct hists *hists;
	int ret;

	if (!symbol_conf.report_hierarchy)
		return 0;

	evlist__for_each_entry(evlist, evsel) {
		hists = evsel__hists(evsel);

		perf_hpp_list__for_each_sort_list(list, fmt) {
			if (perf_hpp__is_dynamic_entry(fmt) &&
			    !perf_hpp__defined_dynamic_entry(fmt, hists))
				continue;

			ret = add_hierarchy_fmt(hists, fmt);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}
