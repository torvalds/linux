/*
 *	xt_time
 *	Copyright © CC Computer Consultants GmbH, 2007
 *	Contact: <jengelh@computergmbh.de>
 *
 *	based on ipt_time by Fabrice MARIE <fabrice@netfilter.org>
 *	This is a module which is used for time matching
 *	It is using some modified code from dietlibc (localtime() function)
 *	that you can find at http://www.fefe.de/dietlibc/
 *	This file is distributed under the terms of the GNU General Public
 *	License (GPL). Copies of the GPL can be obtained from gnu.org/gpl.
 */
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_time.h>

struct xtm {
	u_int8_t month;    /* (1-12) */
	u_int8_t monthday; /* (1-31) */
	u_int8_t weekday;  /* (1-7) */
	u_int8_t hour;     /* (0-23) */
	u_int8_t minute;   /* (0-59) */
	u_int8_t second;   /* (0-59) */
	unsigned int dse;
};

extern struct timezone sys_tz; /* ouch */

static const u_int16_t days_since_year[] = {
	0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334,
};

static const u_int16_t days_since_leapyear[] = {
	0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335,
};

/*
 * Since time progresses forward, it is best to organize this array in reverse,
 * to minimize lookup time.
 */
enum {
	DSE_FIRST = 2039,
};
static const u_int16_t days_since_epoch[] = {
	/* 2039 - 2030 */
	25202, 24837, 24472, 24106, 23741, 23376, 23011, 22645, 22280, 21915,
	/* 2029 - 2020 */
	21550, 21184, 20819, 20454, 20089, 19723, 19358, 18993, 18628, 18262,
	/* 2019 - 2010 */
	17897, 17532, 17167, 16801, 16436, 16071, 15706, 15340, 14975, 14610,
	/* 2009 - 2000 */
	14245, 13879, 13514, 13149, 12784, 12418, 12053, 11688, 11323, 10957,
	/* 1999 - 1990 */
	10592, 10227, 9862, 9496, 9131, 8766, 8401, 8035, 7670, 7305,
	/* 1989 - 1980 */
	6940, 6574, 6209, 5844, 5479, 5113, 4748, 4383, 4018, 3652,
	/* 1979 - 1970 */
	3287, 2922, 2557, 2191, 1826, 1461, 1096, 730, 365, 0,
};

static inline bool is_leap(unsigned int y)
{
	return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0);
}

/*
 * Each network packet has a (nano)seconds-since-the-epoch (SSTE) timestamp.
 * Since we match against days and daytime, the SSTE value needs to be
 * computed back into human-readable dates.
 *
 * This is done in three separate functions so that the most expensive
 * calculations are done last, in case a "simple match" can be found earlier.
 */
static inline unsigned int localtime_1(struct xtm *r, time_t time)
{
	unsigned int v, w;

	/* Each day has 86400s, so finding the hour/minute is actually easy. */
	v         = time % 86400;
	r->second = v % 60;
	w         = v / 60;
	r->minute = w % 60;
	r->hour   = w / 60;
	return v;
}

static inline void localtime_2(struct xtm *r, time_t time)
{
	/*
	 * Here comes the rest (weekday, monthday). First, divide the SSTE
	 * by seconds-per-day to get the number of _days_ since the epoch.
	 */
	r->dse = time / 86400;

	/*
	 * 1970-01-01 (w=0) was a Thursday (4).
	 * -1 and +1 map Sunday properly onto 7.
	 */
	r->weekday = (4 + r->dse - 1) % 7 + 1;
}

static void localtime_3(struct xtm *r, time_t time)
{
	unsigned int year, i, w = r->dse;

	/*
	 * In each year, a certain number of days-since-the-epoch have passed.
	 * Find the year that is closest to said days.
	 *
	 * Consider, for example, w=21612 (2029-03-04). Loop will abort on
	 * dse[i] <= w, which happens when dse[i] == 21550. This implies
	 * year == 2009. w will then be 62.
	 */
	for (i = 0, year = DSE_FIRST; days_since_epoch[i] > w;
	    ++i, --year)
		/* just loop */;

	w -= days_since_epoch[i];

	/*
	 * By now we have the current year, and the day of the year.
	 * r->yearday = w;
	 *
	 * On to finding the month (like above). In each month, a certain
	 * number of days-since-New Year have passed, and find the closest
	 * one.
	 *
	 * Consider w=62 (in a non-leap year). Loop will abort on
	 * dsy[i] < w, which happens when dsy[i] == 31+28 (i == 2).
	 * Concludes i == 2, i.e. 3rd month => March.
	 *
	 * (A different approach to use would be to subtract a monthlength
	 * from w repeatedly while counting.)
	 */
	if (is_leap(year)) {
		for (i = ARRAY_SIZE(days_since_leapyear) - 1;
		    i > 0 && days_since_year[i] > w; --i)
			/* just loop */;
	} else {
		for (i = ARRAY_SIZE(days_since_year) - 1;
		    i > 0 && days_since_year[i] > w; --i)
			/* just loop */;
	}

	r->month    = i + 1;
	r->monthday = w - days_since_year[i] + 1;
	return;
}

static bool
time_mt(const struct sk_buff *skb, const struct net_device *in,
        const struct net_device *out, const struct xt_match *match,
        const void *matchinfo, int offset, unsigned int protoff, bool *hotdrop)
{
	const struct xt_time_info *info = matchinfo;
	unsigned int packet_time;
	struct xtm current_time;
	s64 stamp;

	/*
	 * We cannot use get_seconds() instead of __net_timestamp() here.
	 * Suppose you have two rules:
	 * 	1. match before 13:00
	 * 	2. match after 13:00
	 * If you match against processing time (get_seconds) it
	 * may happen that the same packet matches both rules if
	 * it arrived at the right moment before 13:00.
	 */
	if (skb->tstamp.tv64 == 0)
		__net_timestamp((struct sk_buff *)skb);

	stamp = ktime_to_ns(skb->tstamp);
	stamp = div_s64(stamp, NSEC_PER_SEC);

	if (info->flags & XT_TIME_LOCAL_TZ)
		/* Adjust for local timezone */
		stamp -= 60 * sys_tz.tz_minuteswest;

	/*
	 * xt_time will match when _all_ of the following hold:
	 *   - 'now' is in the global time range date_start..date_end
	 *   - 'now' is in the monthday mask
	 *   - 'now' is in the weekday mask
	 *   - 'now' is in the daytime range time_start..time_end
	 * (and by default, libxt_time will set these so as to match)
	 */

	if (stamp < info->date_start || stamp > info->date_stop)
		return false;

	packet_time = localtime_1(&current_time, stamp);

	if (info->daytime_start < info->daytime_stop) {
		if (packet_time < info->daytime_start ||
		    packet_time > info->daytime_stop)
			return false;
	} else {
		if (packet_time < info->daytime_start &&
		    packet_time > info->daytime_stop)
			return false;
	}

	localtime_2(&current_time, stamp);

	if (!(info->weekdays_match & (1 << current_time.weekday)))
		return false;

	/* Do not spend time computing monthday if all days match anyway */
	if (info->monthdays_match != XT_TIME_ALL_MONTHDAYS) {
		localtime_3(&current_time, stamp);
		if (!(info->monthdays_match & (1 << current_time.monthday)))
			return false;
	}

	return true;
}

static bool
time_mt_check(const char *tablename, const void *ip,
              const struct xt_match *match, void *matchinfo,
              unsigned int hook_mask)
{
	const struct xt_time_info *info = matchinfo;

	if (info->daytime_start > XT_TIME_MAX_DAYTIME ||
	    info->daytime_stop > XT_TIME_MAX_DAYTIME) {
		printk(KERN_WARNING "xt_time: invalid argument - start or "
		       "stop time greater than 23:59:59\n");
		return false;
	}

	return true;
}

static struct xt_match time_mt_reg[] __read_mostly = {
	{
		.name       = "time",
		.family     = AF_INET,
		.match      = time_mt,
		.matchsize  = sizeof(struct xt_time_info),
		.checkentry = time_mt_check,
		.me         = THIS_MODULE,
	},
	{
		.name       = "time",
		.family     = AF_INET6,
		.match      = time_mt,
		.matchsize  = sizeof(struct xt_time_info),
		.checkentry = time_mt_check,
		.me         = THIS_MODULE,
	},
};

static int __init time_mt_init(void)
{
	return xt_register_matches(time_mt_reg, ARRAY_SIZE(time_mt_reg));
}

static void __exit time_mt_exit(void)
{
	xt_unregister_matches(time_mt_reg, ARRAY_SIZE(time_mt_reg));
}

module_init(time_mt_init);
module_exit(time_mt_exit);
MODULE_AUTHOR("Jan Engelhardt <jengelh@computergmbh.de>");
MODULE_DESCRIPTION("Xtables: time-based matching");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ipt_time");
MODULE_ALIAS("ip6t_time");
