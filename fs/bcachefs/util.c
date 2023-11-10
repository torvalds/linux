// SPDX-License-Identifier: GPL-2.0
/*
 * random utiility code, for bcache but in theory not specific to bcache
 *
 * Copyright 2010, 2011 Kent Overstreet <kent.overstreet@gmail.com>
 * Copyright 2012 Google, Inc.
 */

#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/console.h>
#include <linux/ctype.h>
#include <linux/debugfs.h>
#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/log2.h>
#include <linux/math64.h>
#include <linux/percpu.h>
#include <linux/preempt.h>
#include <linux/random.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/sched/clock.h>

#include "eytzinger.h"
#include "mean_and_variance.h"
#include "util.h"

static const char si_units[] = "?kMGTPEZY";

/* string_get_size units: */
static const char *const units_2[] = {
	"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB", "ZiB", "YiB"
};
static const char *const units_10[] = {
	"B", "kB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB"
};

static int parse_u64(const char *cp, u64 *res)
{
	const char *start = cp;
	u64 v = 0;

	if (!isdigit(*cp))
		return -EINVAL;

	do {
		if (v > U64_MAX / 10)
			return -ERANGE;
		v *= 10;
		if (v > U64_MAX - (*cp - '0'))
			return -ERANGE;
		v += *cp - '0';
		cp++;
	} while (isdigit(*cp));

	*res = v;
	return cp - start;
}

static int bch2_pow(u64 n, u64 p, u64 *res)
{
	*res = 1;

	while (p--) {
		if (*res > div_u64(U64_MAX, n))
			return -ERANGE;
		*res *= n;
	}
	return 0;
}

static int parse_unit_suffix(const char *cp, u64 *res)
{
	const char *start = cp;
	u64 base = 1024;
	unsigned u;
	int ret;

	if (*cp == ' ')
		cp++;

	for (u = 1; u < strlen(si_units); u++)
		if (*cp == si_units[u]) {
			cp++;
			goto got_unit;
		}

	for (u = 0; u < ARRAY_SIZE(units_2); u++)
		if (!strncmp(cp, units_2[u], strlen(units_2[u]))) {
			cp += strlen(units_2[u]);
			goto got_unit;
		}

	for (u = 0; u < ARRAY_SIZE(units_10); u++)
		if (!strncmp(cp, units_10[u], strlen(units_10[u]))) {
			cp += strlen(units_10[u]);
			base = 1000;
			goto got_unit;
		}

	*res = 1;
	return 0;
got_unit:
	ret = bch2_pow(base, u, res);
	if (ret)
		return ret;

	return cp - start;
}

#define parse_or_ret(cp, _f)			\
do {						\
	int _ret = _f;				\
	if (_ret < 0)				\
		return _ret;			\
	cp += _ret;				\
} while (0)

static int __bch2_strtou64_h(const char *cp, u64 *res)
{
	const char *start = cp;
	u64 v = 0, b, f_n = 0, f_d = 1;
	int ret;

	parse_or_ret(cp, parse_u64(cp, &v));

	if (*cp == '.') {
		cp++;
		ret = parse_u64(cp, &f_n);
		if (ret < 0)
			return ret;
		cp += ret;

		ret = bch2_pow(10, ret, &f_d);
		if (ret)
			return ret;
	}

	parse_or_ret(cp, parse_unit_suffix(cp, &b));

	if (v > div_u64(U64_MAX, b))
		return -ERANGE;
	v *= b;

	if (f_n > div_u64(U64_MAX, b))
		return -ERANGE;

	f_n = div_u64(f_n * b, f_d);
	if (v + f_n < v)
		return -ERANGE;
	v += f_n;

	*res = v;
	return cp - start;
}

static int __bch2_strtoh(const char *cp, u64 *res,
			 u64 t_max, bool t_signed)
{
	bool positive = *cp != '-';
	u64 v = 0;

	if (*cp == '+' || *cp == '-')
		cp++;

	parse_or_ret(cp, __bch2_strtou64_h(cp, &v));

	if (*cp == '\n')
		cp++;
	if (*cp)
		return -EINVAL;

	if (positive) {
		if (v > t_max)
			return -ERANGE;
	} else {
		if (v && !t_signed)
			return -ERANGE;

		if (v > t_max + 1)
			return -ERANGE;
		v = -v;
	}

	*res = v;
	return 0;
}

#define STRTO_H(name, type)					\
int bch2_ ## name ## _h(const char *cp, type *res)		\
{								\
	u64 v = 0;						\
	int ret = __bch2_strtoh(cp, &v, ANYSINT_MAX(type),	\
			ANYSINT_MAX(type) != ((type) ~0ULL));	\
	*res = v;						\
	return ret;						\
}

STRTO_H(strtoint, int)
STRTO_H(strtouint, unsigned int)
STRTO_H(strtoll, long long)
STRTO_H(strtoull, unsigned long long)
STRTO_H(strtou64, u64)

u64 bch2_read_flag_list(char *opt, const char * const list[])
{
	u64 ret = 0;
	char *p, *s, *d = kstrdup(opt, GFP_KERNEL);

	if (!d)
		return -ENOMEM;

	s = strim(d);

	while ((p = strsep(&s, ","))) {
		int flag = match_string(list, -1, p);

		if (flag < 0) {
			ret = -1;
			break;
		}

		ret |= 1 << flag;
	}

	kfree(d);

	return ret;
}

bool bch2_is_zero(const void *_p, size_t n)
{
	const char *p = _p;
	size_t i;

	for (i = 0; i < n; i++)
		if (p[i])
			return false;
	return true;
}

void bch2_prt_u64_binary(struct printbuf *out, u64 v, unsigned nr_bits)
{
	while (nr_bits)
		prt_char(out, '0' + ((v >> --nr_bits) & 1));
}

void bch2_print_string_as_lines(const char *prefix, const char *lines)
{
	const char *p;

	if (!lines) {
		printk("%s (null)\n", prefix);
		return;
	}

	console_lock();
	while (1) {
		p = strchrnul(lines, '\n');
		printk("%s%.*s\n", prefix, (int) (p - lines), lines);
		if (!*p)
			break;
		lines = p + 1;
	}
	console_unlock();
}

int bch2_save_backtrace(bch_stacktrace *stack, struct task_struct *task)
{
#ifdef CONFIG_STACKTRACE
	unsigned nr_entries = 0;
	int ret = 0;

	stack->nr = 0;
	ret = darray_make_room(stack, 32);
	if (ret)
		return ret;

	if (!down_read_trylock(&task->signal->exec_update_lock))
		return -1;

	do {
		nr_entries = stack_trace_save_tsk(task, stack->data, stack->size, 0);
	} while (nr_entries == stack->size &&
		 !(ret = darray_make_room(stack, stack->size * 2)));

	stack->nr = nr_entries;
	up_read(&task->signal->exec_update_lock);

	return ret;
#else
	return 0;
#endif
}

void bch2_prt_backtrace(struct printbuf *out, bch_stacktrace *stack)
{
	unsigned long *i;

	darray_for_each(*stack, i) {
		prt_printf(out, "[<0>] %pB", (void *) *i);
		prt_newline(out);
	}
}

int bch2_prt_task_backtrace(struct printbuf *out, struct task_struct *task)
{
	bch_stacktrace stack = { 0 };
	int ret = bch2_save_backtrace(&stack, task);

	bch2_prt_backtrace(out, &stack);
	darray_exit(&stack);
	return ret;
}

/* time stats: */

#ifndef CONFIG_BCACHEFS_NO_LATENCY_ACCT
static void bch2_quantiles_update(struct bch2_quantiles *q, u64 v)
{
	unsigned i = 0;

	while (i < ARRAY_SIZE(q->entries)) {
		struct bch2_quantile_entry *e = q->entries + i;

		if (unlikely(!e->step)) {
			e->m = v;
			e->step = max_t(unsigned, v / 2, 1024);
		} else if (e->m > v) {
			e->m = e->m >= e->step
				? e->m - e->step
				: 0;
		} else if (e->m < v) {
			e->m = e->m + e->step > e->m
				? e->m + e->step
				: U32_MAX;
		}

		if ((e->m > v ? e->m - v : v - e->m) < e->step)
			e->step = max_t(unsigned, e->step / 2, 1);

		if (v >= e->m)
			break;

		i = eytzinger0_child(i, v > e->m);
	}
}

static inline void bch2_time_stats_update_one(struct bch2_time_stats *stats,
					      u64 start, u64 end)
{
	u64 duration, freq;

	if (time_after64(end, start)) {
		duration = end - start;
		mean_and_variance_update(&stats->duration_stats, duration);
		mean_and_variance_weighted_update(&stats->duration_stats_weighted, duration);
		stats->max_duration = max(stats->max_duration, duration);
		stats->min_duration = min(stats->min_duration, duration);
		bch2_quantiles_update(&stats->quantiles, duration);
	}

	if (time_after64(end, stats->last_event)) {
		freq = end - stats->last_event;
		mean_and_variance_update(&stats->freq_stats, freq);
		mean_and_variance_weighted_update(&stats->freq_stats_weighted, freq);
		stats->max_freq = max(stats->max_freq, freq);
		stats->min_freq = min(stats->min_freq, freq);
		stats->last_event = end;
	}
}

static noinline void bch2_time_stats_clear_buffer(struct bch2_time_stats *stats,
						  struct bch2_time_stat_buffer *b)
{
	struct bch2_time_stat_buffer_entry *i;
	unsigned long flags;

	spin_lock_irqsave(&stats->lock, flags);
	for (i = b->entries;
	     i < b->entries + ARRAY_SIZE(b->entries);
	     i++)
		bch2_time_stats_update_one(stats, i->start, i->end);
	spin_unlock_irqrestore(&stats->lock, flags);

	b->nr = 0;
}

void __bch2_time_stats_update(struct bch2_time_stats *stats, u64 start, u64 end)
{
	unsigned long flags;

	WARN_RATELIMIT(!stats->min_duration || !stats->min_freq,
		       "time_stats: min_duration = %llu, min_freq = %llu",
		       stats->min_duration, stats->min_freq);

	if (!stats->buffer) {
		spin_lock_irqsave(&stats->lock, flags);
		bch2_time_stats_update_one(stats, start, end);

		if (mean_and_variance_weighted_get_mean(stats->freq_stats_weighted) < 32 &&
		    stats->duration_stats.n > 1024)
			stats->buffer =
				alloc_percpu_gfp(struct bch2_time_stat_buffer,
						 GFP_ATOMIC);
		spin_unlock_irqrestore(&stats->lock, flags);
	} else {
		struct bch2_time_stat_buffer *b;

		preempt_disable();
		b = this_cpu_ptr(stats->buffer);

		BUG_ON(b->nr >= ARRAY_SIZE(b->entries));
		b->entries[b->nr++] = (struct bch2_time_stat_buffer_entry) {
			.start = start,
			.end = end
		};

		if (unlikely(b->nr == ARRAY_SIZE(b->entries)))
			bch2_time_stats_clear_buffer(stats, b);
		preempt_enable();
	}
}
#endif

static const struct time_unit {
	const char	*name;
	u64		nsecs;
} time_units[] = {
	{ "ns",		1		 },
	{ "us",		NSEC_PER_USEC	 },
	{ "ms",		NSEC_PER_MSEC	 },
	{ "s",		NSEC_PER_SEC	 },
	{ "m",          (u64) NSEC_PER_SEC * 60},
	{ "h",          (u64) NSEC_PER_SEC * 3600},
	{ "eon",        U64_MAX          },
};

static const struct time_unit *pick_time_units(u64 ns)
{
	const struct time_unit *u;

	for (u = time_units;
	     u + 1 < time_units + ARRAY_SIZE(time_units) &&
	     ns >= u[1].nsecs << 1;
	     u++)
		;

	return u;
}

void bch2_pr_time_units(struct printbuf *out, u64 ns)
{
	const struct time_unit *u = pick_time_units(ns);

	prt_printf(out, "%llu %s", div_u64(ns, u->nsecs), u->name);
}

static void bch2_pr_time_units_aligned(struct printbuf *out, u64 ns)
{
	const struct time_unit *u = pick_time_units(ns);

	prt_printf(out, "%llu ", div64_u64(ns, u->nsecs));
	prt_tab_rjust(out);
	prt_printf(out, "%s", u->name);
}

#ifndef __KERNEL__
#include <time.h>
void bch2_prt_datetime(struct printbuf *out, time64_t sec)
{
	time_t t = sec;
	char buf[64];
	ctime_r(&t, buf);
	prt_str(out, buf);
}
#else
void bch2_prt_datetime(struct printbuf *out, time64_t sec)
{
	char buf[64];
	snprintf(buf, sizeof(buf), "%ptT", &sec);
	prt_u64(out, sec);
}
#endif

#define TABSTOP_SIZE 12

static inline void pr_name_and_units(struct printbuf *out, const char *name, u64 ns)
{
	prt_str(out, name);
	prt_tab(out);
	bch2_pr_time_units_aligned(out, ns);
	prt_newline(out);
}

void bch2_time_stats_to_text(struct printbuf *out, struct bch2_time_stats *stats)
{
	const struct time_unit *u;
	s64 f_mean = 0, d_mean = 0;
	u64 q, last_q = 0, f_stddev = 0, d_stddev = 0;
	int i;
	/*
	 * avoid divide by zero
	 */
	if (stats->freq_stats.n) {
		f_mean = mean_and_variance_get_mean(stats->freq_stats);
		f_stddev = mean_and_variance_get_stddev(stats->freq_stats);
		d_mean = mean_and_variance_get_mean(stats->duration_stats);
		d_stddev = mean_and_variance_get_stddev(stats->duration_stats);
	}

	printbuf_tabstop_push(out, out->indent + TABSTOP_SIZE);
	prt_printf(out, "count:");
	prt_tab(out);
	prt_printf(out, "%llu ",
			 stats->duration_stats.n);
	printbuf_tabstop_pop(out);
	prt_newline(out);

	printbuf_tabstops_reset(out);

	printbuf_tabstop_push(out, out->indent + 20);
	printbuf_tabstop_push(out, TABSTOP_SIZE + 2);
	printbuf_tabstop_push(out, 0);
	printbuf_tabstop_push(out, TABSTOP_SIZE + 2);

	prt_tab(out);
	prt_printf(out, "since mount");
	prt_tab_rjust(out);
	prt_tab(out);
	prt_printf(out, "recent");
	prt_tab_rjust(out);
	prt_newline(out);

	printbuf_tabstops_reset(out);
	printbuf_tabstop_push(out, out->indent + 20);
	printbuf_tabstop_push(out, TABSTOP_SIZE);
	printbuf_tabstop_push(out, 2);
	printbuf_tabstop_push(out, TABSTOP_SIZE);

	prt_printf(out, "duration of events");
	prt_newline(out);
	printbuf_indent_add(out, 2);

	pr_name_and_units(out, "min:", stats->min_duration);
	pr_name_and_units(out, "max:", stats->max_duration);

	prt_printf(out, "mean:");
	prt_tab(out);
	bch2_pr_time_units_aligned(out, d_mean);
	prt_tab(out);
	bch2_pr_time_units_aligned(out, mean_and_variance_weighted_get_mean(stats->duration_stats_weighted));
	prt_newline(out);

	prt_printf(out, "stddev:");
	prt_tab(out);
	bch2_pr_time_units_aligned(out, d_stddev);
	prt_tab(out);
	bch2_pr_time_units_aligned(out, mean_and_variance_weighted_get_stddev(stats->duration_stats_weighted));

	printbuf_indent_sub(out, 2);
	prt_newline(out);

	prt_printf(out, "time between events");
	prt_newline(out);
	printbuf_indent_add(out, 2);

	pr_name_and_units(out, "min:", stats->min_freq);
	pr_name_and_units(out, "max:", stats->max_freq);

	prt_printf(out, "mean:");
	prt_tab(out);
	bch2_pr_time_units_aligned(out, f_mean);
	prt_tab(out);
	bch2_pr_time_units_aligned(out, mean_and_variance_weighted_get_mean(stats->freq_stats_weighted));
	prt_newline(out);

	prt_printf(out, "stddev:");
	prt_tab(out);
	bch2_pr_time_units_aligned(out, f_stddev);
	prt_tab(out);
	bch2_pr_time_units_aligned(out, mean_and_variance_weighted_get_stddev(stats->freq_stats_weighted));

	printbuf_indent_sub(out, 2);
	prt_newline(out);

	printbuf_tabstops_reset(out);

	i = eytzinger0_first(NR_QUANTILES);
	u = pick_time_units(stats->quantiles.entries[i].m);

	prt_printf(out, "quantiles (%s):\t", u->name);
	eytzinger0_for_each(i, NR_QUANTILES) {
		bool is_last = eytzinger0_next(i, NR_QUANTILES) == -1;

		q = max(stats->quantiles.entries[i].m, last_q);
		prt_printf(out, "%llu ",
		       div_u64(q, u->nsecs));
		if (is_last)
			prt_newline(out);
		last_q = q;
	}
}

void bch2_time_stats_exit(struct bch2_time_stats *stats)
{
	free_percpu(stats->buffer);
}

void bch2_time_stats_init(struct bch2_time_stats *stats)
{
	memset(stats, 0, sizeof(*stats));
	stats->duration_stats_weighted.weight = 8;
	stats->freq_stats_weighted.weight = 8;
	stats->min_duration = U64_MAX;
	stats->min_freq = U64_MAX;
	spin_lock_init(&stats->lock);
}

/* ratelimit: */

/**
 * bch2_ratelimit_delay() - return how long to delay until the next time to do
 *		some work
 * @d:		the struct bch_ratelimit to update
 * Returns:	the amount of time to delay by, in jiffies
 */
u64 bch2_ratelimit_delay(struct bch_ratelimit *d)
{
	u64 now = local_clock();

	return time_after64(d->next, now)
		? nsecs_to_jiffies(d->next - now)
		: 0;
}

/**
 * bch2_ratelimit_increment() - increment @d by the amount of work done
 * @d:		the struct bch_ratelimit to update
 * @done:	the amount of work done, in arbitrary units
 */
void bch2_ratelimit_increment(struct bch_ratelimit *d, u64 done)
{
	u64 now = local_clock();

	d->next += div_u64(done * NSEC_PER_SEC, d->rate);

	if (time_before64(now + NSEC_PER_SEC, d->next))
		d->next = now + NSEC_PER_SEC;

	if (time_after64(now - NSEC_PER_SEC * 2, d->next))
		d->next = now - NSEC_PER_SEC * 2;
}

/* pd controller: */

/*
 * Updates pd_controller. Attempts to scale inputed values to units per second.
 * @target: desired value
 * @actual: current value
 *
 * @sign: 1 or -1; 1 if increasing the rate makes actual go up, -1 if increasing
 * it makes actual go down.
 */
void bch2_pd_controller_update(struct bch_pd_controller *pd,
			      s64 target, s64 actual, int sign)
{
	s64 proportional, derivative, change;

	unsigned long seconds_since_update = (jiffies - pd->last_update) / HZ;

	if (seconds_since_update == 0)
		return;

	pd->last_update = jiffies;

	proportional = actual - target;
	proportional *= seconds_since_update;
	proportional = div_s64(proportional, pd->p_term_inverse);

	derivative = actual - pd->last_actual;
	derivative = div_s64(derivative, seconds_since_update);
	derivative = ewma_add(pd->smoothed_derivative, derivative,
			      (pd->d_term / seconds_since_update) ?: 1);
	derivative = derivative * pd->d_term;
	derivative = div_s64(derivative, pd->p_term_inverse);

	change = proportional + derivative;

	/* Don't increase rate if not keeping up */
	if (change > 0 &&
	    pd->backpressure &&
	    time_after64(local_clock(),
			 pd->rate.next + NSEC_PER_MSEC))
		change = 0;

	change *= (sign * -1);

	pd->rate.rate = clamp_t(s64, (s64) pd->rate.rate + change,
				1, UINT_MAX);

	pd->last_actual		= actual;
	pd->last_derivative	= derivative;
	pd->last_proportional	= proportional;
	pd->last_change		= change;
	pd->last_target		= target;
}

void bch2_pd_controller_init(struct bch_pd_controller *pd)
{
	pd->rate.rate		= 1024;
	pd->last_update		= jiffies;
	pd->p_term_inverse	= 6000;
	pd->d_term		= 30;
	pd->d_smooth		= pd->d_term;
	pd->backpressure	= 1;
}

void bch2_pd_controller_debug_to_text(struct printbuf *out, struct bch_pd_controller *pd)
{
	if (!out->nr_tabstops)
		printbuf_tabstop_push(out, 20);

	prt_printf(out, "rate:");
	prt_tab(out);
	prt_human_readable_s64(out, pd->rate.rate);
	prt_newline(out);

	prt_printf(out, "target:");
	prt_tab(out);
	prt_human_readable_u64(out, pd->last_target);
	prt_newline(out);

	prt_printf(out, "actual:");
	prt_tab(out);
	prt_human_readable_u64(out, pd->last_actual);
	prt_newline(out);

	prt_printf(out, "proportional:");
	prt_tab(out);
	prt_human_readable_s64(out, pd->last_proportional);
	prt_newline(out);

	prt_printf(out, "derivative:");
	prt_tab(out);
	prt_human_readable_s64(out, pd->last_derivative);
	prt_newline(out);

	prt_printf(out, "change:");
	prt_tab(out);
	prt_human_readable_s64(out, pd->last_change);
	prt_newline(out);

	prt_printf(out, "next io:");
	prt_tab(out);
	prt_printf(out, "%llims", div64_s64(pd->rate.next - local_clock(), NSEC_PER_MSEC));
	prt_newline(out);
}

/* misc: */

void bch2_bio_map(struct bio *bio, void *base, size_t size)
{
	while (size) {
		struct page *page = is_vmalloc_addr(base)
				? vmalloc_to_page(base)
				: virt_to_page(base);
		unsigned offset = offset_in_page(base);
		unsigned len = min_t(size_t, PAGE_SIZE - offset, size);

		BUG_ON(!bio_add_page(bio, page, len, offset));
		size -= len;
		base += len;
	}
}

int bch2_bio_alloc_pages(struct bio *bio, size_t size, gfp_t gfp_mask)
{
	while (size) {
		struct page *page = alloc_pages(gfp_mask, 0);
		unsigned len = min_t(size_t, PAGE_SIZE, size);

		if (!page)
			return -ENOMEM;

		if (unlikely(!bio_add_page(bio, page, len, 0))) {
			__free_page(page);
			break;
		}

		size -= len;
	}

	return 0;
}

size_t bch2_rand_range(size_t max)
{
	size_t rand;

	if (!max)
		return 0;

	do {
		rand = get_random_long();
		rand &= roundup_pow_of_two(max) - 1;
	} while (rand >= max);

	return rand;
}

void memcpy_to_bio(struct bio *dst, struct bvec_iter dst_iter, const void *src)
{
	struct bio_vec bv;
	struct bvec_iter iter;

	__bio_for_each_segment(bv, dst, iter, dst_iter) {
		void *dstp = kmap_local_page(bv.bv_page);

		memcpy(dstp + bv.bv_offset, src, bv.bv_len);
		kunmap_local(dstp);

		src += bv.bv_len;
	}
}

void memcpy_from_bio(void *dst, struct bio *src, struct bvec_iter src_iter)
{
	struct bio_vec bv;
	struct bvec_iter iter;

	__bio_for_each_segment(bv, src, iter, src_iter) {
		void *srcp = kmap_local_page(bv.bv_page);

		memcpy(dst, srcp + bv.bv_offset, bv.bv_len);
		kunmap_local(srcp);

		dst += bv.bv_len;
	}
}

static int alignment_ok(const void *base, size_t align)
{
	return IS_ENABLED(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) ||
		((unsigned long)base & (align - 1)) == 0;
}

static void u32_swap(void *a, void *b, size_t size)
{
	u32 t = *(u32 *)a;
	*(u32 *)a = *(u32 *)b;
	*(u32 *)b = t;
}

static void u64_swap(void *a, void *b, size_t size)
{
	u64 t = *(u64 *)a;
	*(u64 *)a = *(u64 *)b;
	*(u64 *)b = t;
}

static void generic_swap(void *a, void *b, size_t size)
{
	char t;

	do {
		t = *(char *)a;
		*(char *)a++ = *(char *)b;
		*(char *)b++ = t;
	} while (--size > 0);
}

static inline int do_cmp(void *base, size_t n, size_t size,
			 int (*cmp_func)(const void *, const void *, size_t),
			 size_t l, size_t r)
{
	return cmp_func(base + inorder_to_eytzinger0(l, n) * size,
			base + inorder_to_eytzinger0(r, n) * size,
			size);
}

static inline void do_swap(void *base, size_t n, size_t size,
			   void (*swap_func)(void *, void *, size_t),
			   size_t l, size_t r)
{
	swap_func(base + inorder_to_eytzinger0(l, n) * size,
		  base + inorder_to_eytzinger0(r, n) * size,
		  size);
}

void eytzinger0_sort(void *base, size_t n, size_t size,
		     int (*cmp_func)(const void *, const void *, size_t),
		     void (*swap_func)(void *, void *, size_t))
{
	int i, c, r;

	if (!swap_func) {
		if (size == 4 && alignment_ok(base, 4))
			swap_func = u32_swap;
		else if (size == 8 && alignment_ok(base, 8))
			swap_func = u64_swap;
		else
			swap_func = generic_swap;
	}

	/* heapify */
	for (i = n / 2 - 1; i >= 0; --i) {
		for (r = i; r * 2 + 1 < n; r = c) {
			c = r * 2 + 1;

			if (c + 1 < n &&
			    do_cmp(base, n, size, cmp_func, c, c + 1) < 0)
				c++;

			if (do_cmp(base, n, size, cmp_func, r, c) >= 0)
				break;

			do_swap(base, n, size, swap_func, r, c);
		}
	}

	/* sort */
	for (i = n - 1; i > 0; --i) {
		do_swap(base, n, size, swap_func, 0, i);

		for (r = 0; r * 2 + 1 < i; r = c) {
			c = r * 2 + 1;

			if (c + 1 < i &&
			    do_cmp(base, n, size, cmp_func, c, c + 1) < 0)
				c++;

			if (do_cmp(base, n, size, cmp_func, r, c) >= 0)
				break;

			do_swap(base, n, size, swap_func, r, c);
		}
	}
}

void sort_cmp_size(void *base, size_t num, size_t size,
	  int (*cmp_func)(const void *, const void *, size_t),
	  void (*swap_func)(void *, void *, size_t size))
{
	/* pre-scale counters for performance */
	int i = (num/2 - 1) * size, n = num * size, c, r;

	if (!swap_func) {
		if (size == 4 && alignment_ok(base, 4))
			swap_func = u32_swap;
		else if (size == 8 && alignment_ok(base, 8))
			swap_func = u64_swap;
		else
			swap_func = generic_swap;
	}

	/* heapify */
	for ( ; i >= 0; i -= size) {
		for (r = i; r * 2 + size < n; r  = c) {
			c = r * 2 + size;
			if (c < n - size &&
			    cmp_func(base + c, base + c + size, size) < 0)
				c += size;
			if (cmp_func(base + r, base + c, size) >= 0)
				break;
			swap_func(base + r, base + c, size);
		}
	}

	/* sort */
	for (i = n - size; i > 0; i -= size) {
		swap_func(base, base + i, size);
		for (r = 0; r * 2 + size < i; r = c) {
			c = r * 2 + size;
			if (c < i - size &&
			    cmp_func(base + c, base + c + size, size) < 0)
				c += size;
			if (cmp_func(base + r, base + c, size) >= 0)
				break;
			swap_func(base + r, base + c, size);
		}
	}
}

static void mempool_free_vp(void *element, void *pool_data)
{
	size_t size = (size_t) pool_data;

	vpfree(element, size);
}

static void *mempool_alloc_vp(gfp_t gfp_mask, void *pool_data)
{
	size_t size = (size_t) pool_data;

	return vpmalloc(size, gfp_mask);
}

int mempool_init_kvpmalloc_pool(mempool_t *pool, int min_nr, size_t size)
{
	return size < PAGE_SIZE
		? mempool_init_kmalloc_pool(pool, min_nr, size)
		: mempool_init(pool, min_nr, mempool_alloc_vp,
			       mempool_free_vp, (void *) size);
}

#if 0
void eytzinger1_test(void)
{
	unsigned inorder, eytz, size;

	pr_info("1 based eytzinger test:");

	for (size = 2;
	     size < 65536;
	     size++) {
		unsigned extra = eytzinger1_extra(size);

		if (!(size % 4096))
			pr_info("tree size %u", size);

		BUG_ON(eytzinger1_prev(0, size) != eytzinger1_last(size));
		BUG_ON(eytzinger1_next(0, size) != eytzinger1_first(size));

		BUG_ON(eytzinger1_prev(eytzinger1_first(size), size)	!= 0);
		BUG_ON(eytzinger1_next(eytzinger1_last(size), size)	!= 0);

		inorder = 1;
		eytzinger1_for_each(eytz, size) {
			BUG_ON(__inorder_to_eytzinger1(inorder, size, extra) != eytz);
			BUG_ON(__eytzinger1_to_inorder(eytz, size, extra) != inorder);
			BUG_ON(eytz != eytzinger1_last(size) &&
			       eytzinger1_prev(eytzinger1_next(eytz, size), size) != eytz);

			inorder++;
		}
	}
}

void eytzinger0_test(void)
{

	unsigned inorder, eytz, size;

	pr_info("0 based eytzinger test:");

	for (size = 1;
	     size < 65536;
	     size++) {
		unsigned extra = eytzinger0_extra(size);

		if (!(size % 4096))
			pr_info("tree size %u", size);

		BUG_ON(eytzinger0_prev(-1, size) != eytzinger0_last(size));
		BUG_ON(eytzinger0_next(-1, size) != eytzinger0_first(size));

		BUG_ON(eytzinger0_prev(eytzinger0_first(size), size)	!= -1);
		BUG_ON(eytzinger0_next(eytzinger0_last(size), size)	!= -1);

		inorder = 0;
		eytzinger0_for_each(eytz, size) {
			BUG_ON(__inorder_to_eytzinger0(inorder, size, extra) != eytz);
			BUG_ON(__eytzinger0_to_inorder(eytz, size, extra) != inorder);
			BUG_ON(eytz != eytzinger0_last(size) &&
			       eytzinger0_prev(eytzinger0_next(eytz, size), size) != eytz);

			inorder++;
		}
	}
}

static inline int cmp_u16(const void *_l, const void *_r, size_t size)
{
	const u16 *l = _l, *r = _r;

	return (*l > *r) - (*r - *l);
}

static void eytzinger0_find_test_val(u16 *test_array, unsigned nr, u16 search)
{
	int i, c1 = -1, c2 = -1;
	ssize_t r;

	r = eytzinger0_find_le(test_array, nr,
			       sizeof(test_array[0]),
			       cmp_u16, &search);
	if (r >= 0)
		c1 = test_array[r];

	for (i = 0; i < nr; i++)
		if (test_array[i] <= search && test_array[i] > c2)
			c2 = test_array[i];

	if (c1 != c2) {
		eytzinger0_for_each(i, nr)
			pr_info("[%3u] = %12u", i, test_array[i]);
		pr_info("find_le(%2u) -> [%2zi] = %2i should be %2i",
			i, r, c1, c2);
	}
}

void eytzinger0_find_test(void)
{
	unsigned i, nr, allocated = 1 << 12;
	u16 *test_array = kmalloc_array(allocated, sizeof(test_array[0]), GFP_KERNEL);

	for (nr = 1; nr < allocated; nr++) {
		pr_info("testing %u elems", nr);

		get_random_bytes(test_array, nr * sizeof(test_array[0]));
		eytzinger0_sort(test_array, nr, sizeof(test_array[0]), cmp_u16, NULL);

		/* verify array is sorted correctly: */
		eytzinger0_for_each(i, nr)
			BUG_ON(i != eytzinger0_last(nr) &&
			       test_array[i] > test_array[eytzinger0_next(i, nr)]);

		for (i = 0; i < U16_MAX; i += 1 << 12)
			eytzinger0_find_test_val(test_array, nr, i);

		for (i = 0; i < nr; i++) {
			eytzinger0_find_test_val(test_array, nr, test_array[i] - 1);
			eytzinger0_find_test_val(test_array, nr, test_array[i]);
			eytzinger0_find_test_val(test_array, nr, test_array[i] + 1);
		}
	}

	kfree(test_array);
}
#endif

/*
 * Accumulate percpu counters onto one cpu's copy - only valid when access
 * against any percpu counter is guarded against
 */
u64 *bch2_acc_percpu_u64s(u64 __percpu *p, unsigned nr)
{
	u64 *ret;
	int cpu;

	/* access to pcpu vars has to be blocked by other locking */
	preempt_disable();
	ret = this_cpu_ptr(p);
	preempt_enable();

	for_each_possible_cpu(cpu) {
		u64 *i = per_cpu_ptr(p, cpu);

		if (i != ret) {
			acc_u64s(ret, i, nr);
			memset(i, 0, nr * sizeof(u64));
		}
	}

	return ret;
}
