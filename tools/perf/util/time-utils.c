#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <linux/time64.h>
#include <time.h>
#include <errno.h>
#include <inttypes.h>

#include "perf.h"
#include "debug.h"
#include "time-utils.h"

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

int perf_time__parse_str(struct perf_time_interval *ptime, const char *ostr)
{
	char *start_str, *end_str;
	char *d, *str;
	int rc = 0;

	if (ostr == NULL || *ostr == '\0')
		return 0;

	/* copy original string because we need to modify it */
	str = strdup(ostr);
	if (str == NULL)
		return -ENOMEM;

	ptime->start = 0;
	ptime->end = 0;

	/* str has the format: <start>,<stop>
	 * variations: <start>,
	 *             ,<stop>
	 *             ,
	 */
	start_str = str;
	d = strchr(start_str, ',');
	if (d) {
		*d = '\0';
		++d;
	}
	end_str = d;

	rc = parse_timestr_sec_nsec(ptime, start_str, end_str);

	free(str);

	/* make sure end time is after start time if it was given */
	if (rc == 0 && ptime->end && ptime->end < ptime->start)
		return -EINVAL;

	pr_debug("start time %" PRIu64 ", ", ptime->start);
	pr_debug("end time %" PRIu64 "\n", ptime->end);

	return rc;
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

int timestamp__scnprintf_usec(u64 timestamp, char *buf, size_t sz)
{
	u64  sec = timestamp / NSEC_PER_SEC;
	u64 usec = (timestamp % NSEC_PER_SEC) / NSEC_PER_USEC;

	return scnprintf(buf, sz, "%"PRIu64".%06"PRIu64, sec, usec);
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
