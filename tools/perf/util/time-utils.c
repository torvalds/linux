// SPDX-License-Identifier: GPL-2.0
#include <stdlib.h>
#include <string.h>
#include <linux/string.h>
#include <sys/time.h>
#include <linux/time64.h>
#include <time.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <linux/ctype.h>

#include "perf.h"
#include "debug.h"
#include "time-utils.h"
#include "session.h"
#include "evlist.h"

int parse_nsec_time(const char *str, u64 *ptime)
{
	u64 time_sec, time_nsec;
	char *end;

	time_sec = strtoul(str, &end, 10);
	if (*end != '.' && *end != '\0')
		return -1;

	if (*end == '.') {
		int i;
		char nsec_buf[10];

		if (strlen(++end) > 9)
			return -1;

		strncpy(nsec_buf, end, 9);
		nsec_buf[9] = '\0';

		/* make it nsec precision */
		for (i = strlen(nsec_buf); i < 9; i++)
			nsec_buf[i] = '0';

		time_nsec = strtoul(nsec_buf, &end, 10);
		if (*end != '\0')
			return -1;
	} else
		time_nsec = 0;

	*ptime = time_sec * NSEC_PER_SEC + time_nsec;
	return 0;
}

static int parse_timestr_sec_nsec(struct perf_time_interval *ptime,
				  char *start_str, char *end_str)
{
	if (start_str && (*start_str != '\0') &&
	    (parse_nsec_time(start_str, &ptime->start) != 0)) {
		return -1;
	}

	if (end_str && (*end_str != '\0') &&
	    (parse_nsec_time(end_str, &ptime->end) != 0)) {
		return -1;
	}

	return 0;
}

static int split_start_end(char **start, char **end, const char *ostr, char ch)
{
	char *start_str, *end_str;
	char *d, *str;

	if (ostr == NULL || *ostr == '\0')
		return 0;

	/* copy original string because we need to modify it */
	str = strdup(ostr);
	if (str == NULL)
		return -ENOMEM;

	start_str = str;
	d = strchr(start_str, ch);
	if (d) {
		*d = '\0';
		++d;
	}
	end_str = d;

	*start = start_str;
	*end = end_str;

	return 0;
}

int perf_time__parse_str(struct perf_time_interval *ptime, const char *ostr)
{
	char *start_str = NULL, *end_str;
	int rc;

	rc = split_start_end(&start_str, &end_str, ostr, ',');
	if (rc || !start_str)
		return rc;

	ptime->start = 0;
	ptime->end = 0;

	rc = parse_timestr_sec_nsec(ptime, start_str, end_str);

	free(start_str);

	/* make sure end time is after start time if it was given */
	if (rc == 0 && ptime->end && ptime->end < ptime->start)
		return -EINVAL;

	pr_debug("start time %" PRIu64 ", ", ptime->start);
	pr_debug("end time %" PRIu64 "\n", ptime->end);

	return rc;
}

static int perf_time__parse_strs(struct perf_time_interval *ptime,
				 const char *ostr, int size)
{
	const char *cp;
	char *str, *arg, *p;
	int i, num = 0, rc = 0;

	/* Count the commas */
	for (cp = ostr; *cp; cp++)
		num += !!(*cp == ',');

	if (!num)
		return -EINVAL;

	BUG_ON(num > size);

	str = strdup(ostr);
	if (!str)
		return -ENOMEM;

	/* Split the string and parse each piece, except the last */
	for (i = 0, p = str; i < num - 1; i++) {
		arg = p;
		/* Find next comma, there must be one */
		p = skip_spaces(strchr(p, ',') + 1);
		/* Skip the value, must not contain space or comma */
		while (*p && !isspace(*p)) {
			if (*p++ == ',') {
				rc = -EINVAL;
				goto out;
			}
		}
		/* Split and parse */
		if (*p)
			*p++ = 0;
		rc = perf_time__parse_str(ptime + i, arg);
		if (rc < 0)
			goto out;
	}

	/* Parse the last piece */
	rc = perf_time__parse_str(ptime + i, p);
	if (rc < 0)
		goto out;

	/* Check there is no overlap */
	for (i = 0; i < num - 1; i++) {
		if (ptime[i].end >= ptime[i + 1].start) {
			rc = -EINVAL;
			goto out;
		}
	}

	rc = num;
out:
	free(str);

	return rc;
}

static int parse_percent(double *pcnt, char *str)
{
	char *c, *endptr;
	double d;

	c = strchr(str, '%');
	if (c)
		*c = '\0';
	else
		return -1;

	d = strtod(str, &endptr);
	if (endptr != str + strlen(str))
		return -1;

	*pcnt = d / 100.0;
	return 0;
}

static int set_percent_time(struct perf_time_interval *ptime, double start_pcnt,
			    double end_pcnt, u64 start, u64 end)
{
	u64 total = end - start;

	if (start_pcnt < 0.0 || start_pcnt > 1.0 ||
	    end_pcnt < 0.0 || end_pcnt > 1.0) {
		return -1;
	}

	ptime->start = start + round(start_pcnt * total);
	ptime->end = start + round(end_pcnt * total);

	if (ptime->end > ptime->start && ptime->end != end)
		ptime->end -= 1;

	return 0;
}

static int percent_slash_split(char *str, struct perf_time_interval *ptime,
			       u64 start, u64 end)
{
	char *p, *end_str;
	double pcnt, start_pcnt, end_pcnt;
	int i;

	/*
	 * Example:
	 * 10%/2: select the second 10% slice and the third 10% slice
	 */

	/* We can modify this string since the original one is copied */
	p = strchr(str, '/');
	if (!p)
		return -1;

	*p = '\0';
	if (parse_percent(&pcnt, str) < 0)
		return -1;

	p++;
	i = (int)strtol(p, &end_str, 10);
	if (*end_str)
		return -1;

	if (pcnt <= 0.0)
		return -1;

	start_pcnt = pcnt * (i - 1);
	end_pcnt = pcnt * i;

	return set_percent_time(ptime, start_pcnt, end_pcnt, start, end);
}

static int percent_dash_split(char *str, struct perf_time_interval *ptime,
			      u64 start, u64 end)
{
	char *start_str = NULL, *end_str;
	double start_pcnt, end_pcnt;
	int ret;

	/*
	 * Example: 0%-10%
	 */

	ret = split_start_end(&start_str, &end_str, str, '-');
	if (ret || !start_str)
		return ret;

	if ((parse_percent(&start_pcnt, start_str) != 0) ||
	    (parse_percent(&end_pcnt, end_str) != 0)) {
		free(start_str);
		return -1;
	}

	free(start_str);

	return set_percent_time(ptime, start_pcnt, end_pcnt, start, end);
}

typedef int (*time_pecent_split)(char *, struct perf_time_interval *,
				 u64 start, u64 end);

static int percent_comma_split(struct perf_time_interval *ptime_buf, int num,
			       const char *ostr, u64 start, u64 end,
			       time_pecent_split func)
{
	char *str, *p1, *p2;
	int len, ret, i = 0;

	str = strdup(ostr);
	if (str == NULL)
		return -ENOMEM;

	len = strlen(str);
	p1 = str;

	while (p1 < str + len) {
		if (i >= num) {
			free(str);
			return -1;
		}

		p2 = strchr(p1, ',');
		if (p2)
			*p2 = '\0';

		ret = (func)(p1, &ptime_buf[i], start, end);
		if (ret < 0) {
			free(str);
			return -1;
		}

		pr_debug("start time %d: %" PRIu64 ", ", i, ptime_buf[i].start);
		pr_debug("end time %d: %" PRIu64 "\n", i, ptime_buf[i].end);

		i++;

		if (p2)
			p1 = p2 + 1;
		else
			break;
	}

	free(str);
	return i;
}

static int one_percent_convert(struct perf_time_interval *ptime_buf,
			       const char *ostr, u64 start, u64 end, char *c)
{
	char *str;
	int len = strlen(ostr), ret;

	/*
	 * c points to '%'.
	 * '%' should be the last character
	 */
	if (ostr + len - 1 != c)
		return -1;

	/*
	 * Construct a string like "xx%/1"
	 */
	str = malloc(len + 3);
	if (str == NULL)
		return -ENOMEM;

	memcpy(str, ostr, len);
	strcpy(str + len, "/1");

	ret = percent_slash_split(str, ptime_buf, start, end);
	if (ret == 0)
		ret = 1;

	free(str);
	return ret;
}

int perf_time__percent_parse_str(struct perf_time_interval *ptime_buf, int num,
				 const char *ostr, u64 start, u64 end)
{
	char *c;

	/*
	 * ostr example:
	 * 10%/2,10%/3: select the second 10% slice and the third 10% slice
	 * 0%-10%,30%-40%: multiple time range
	 * 50%: just one percent
	 */

	memset(ptime_buf, 0, sizeof(*ptime_buf) * num);

	c = strchr(ostr, '/');
	if (c) {
		return percent_comma_split(ptime_buf, num, ostr, start,
					   end, percent_slash_split);
	}

	c = strchr(ostr, '-');
	if (c) {
		return percent_comma_split(ptime_buf, num, ostr, start,
					   end, percent_dash_split);
	}

	c = strchr(ostr, '%');
	if (c)
		return one_percent_convert(ptime_buf, ostr, start, end, c);

	return -1;
}

struct perf_time_interval *perf_time__range_alloc(const char *ostr, int *size)
{
	const char *p1, *p2;
	int i = 1;
	struct perf_time_interval *ptime;

	/*
	 * At least allocate one time range.
	 */
	if (!ostr)
		goto alloc;

	p1 = ostr;
	while (p1 < ostr + strlen(ostr)) {
		p2 = strchr(p1, ',');
		if (!p2)
			break;

		p1 = p2 + 1;
		i++;
	}

alloc:
	*size = i;
	ptime = calloc(i, sizeof(*ptime));
	return ptime;
}

bool perf_time__skip_sample(struct perf_time_interval *ptime, u64 timestamp)
{
	/* if time is not set don't drop sample */
	if (timestamp == 0)
		return false;

	/* otherwise compare sample time to time window */
	if ((ptime->start && timestamp < ptime->start) ||
	    (ptime->end && timestamp > ptime->end)) {
		return true;
	}

	return false;
}

bool perf_time__ranges_skip_sample(struct perf_time_interval *ptime_buf,
				   int num, u64 timestamp)
{
	struct perf_time_interval *ptime;
	int i;

	if ((!ptime_buf) || (timestamp == 0) || (num == 0))
		return false;

	if (num == 1)
		return perf_time__skip_sample(&ptime_buf[0], timestamp);

	/*
	 * start/end of multiple time ranges must be valid.
	 */
	for (i = 0; i < num; i++) {
		ptime = &ptime_buf[i];

		if (timestamp >= ptime->start &&
		    (timestamp <= ptime->end || !ptime->end)) {
			return false;
		}
	}

	return true;
}

int perf_time__parse_for_ranges(const char *time_str,
				struct perf_session *session,
				struct perf_time_interval **ranges,
				int *range_size, int *range_num)
{
	bool has_percent = strchr(time_str, '%');
	struct perf_time_interval *ptime_range;
	int size, num, ret = -EINVAL;

	ptime_range = perf_time__range_alloc(time_str, &size);
	if (!ptime_range)
		return -ENOMEM;

	if (has_percent) {
		if (session->evlist->first_sample_time == 0 &&
		    session->evlist->last_sample_time == 0) {
			pr_err("HINT: no first/last sample time found in perf data.\n"
			       "Please use latest perf binary to execute 'perf record'\n"
			       "(if '--buildid-all' is enabled, please set '--timestamp-boundary').\n");
			goto error;
		}

		num = perf_time__percent_parse_str(
				ptime_range, size,
				time_str,
				session->evlist->first_sample_time,
				session->evlist->last_sample_time);
	} else {
		num = perf_time__parse_strs(ptime_range, time_str, size);
	}

	if (num < 0)
		goto error_invalid;

	*range_size = size;
	*range_num = num;
	*ranges = ptime_range;
	return 0;

error_invalid:
	pr_err("Invalid time string\n");
error:
	free(ptime_range);
	return ret;
}

int timestamp__scnprintf_usec(u64 timestamp, char *buf, size_t sz)
{
	u64  sec = timestamp / NSEC_PER_SEC;
	u64 usec = (timestamp % NSEC_PER_SEC) / NSEC_PER_USEC;

	return scnprintf(buf, sz, "%"PRIu64".%06"PRIu64, sec, usec);
}

int timestamp__scnprintf_nsec(u64 timestamp, char *buf, size_t sz)
{
	u64 sec  = timestamp / NSEC_PER_SEC,
	    nsec = timestamp % NSEC_PER_SEC;

	return scnprintf(buf, sz, "%" PRIu64 ".%09" PRIu64, sec, nsec);
}

int fetch_current_timestamp(char *buf, size_t sz)
{
	struct timeval tv;
	struct tm tm;
	char dt[32];

	if (gettimeofday(&tv, NULL) || !localtime_r(&tv.tv_sec, &tm))
		return -1;

	if (!strftime(dt, sizeof(dt), "%Y%m%d%H%M%S", &tm))
		return -1;

	scnprintf(buf, sz, "%s%02u", dt, (unsigned)tv.tv_usec / 10000);

	return 0;
}
