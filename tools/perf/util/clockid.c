// SPDX-License-Identifier: GPL-2.0

#include <subcmd/parse-options.h>
#include <stdio.h>
#include <time.h>
#include <strings.h>
#include <linux/time64.h>
#include "debug.h"
#include "clockid.h"
#include "record.h"

struct clockid_map {
	const char *name;
	int clockid;
};

#define CLOCKID_MAP(n, c)	\
	{ .name = n, .clockid = (c), }

#define CLOCKID_END	{ .name = NULL, }


/*
 * Add the missing ones, we need to build on many distros...
 */
#ifndef CLOCK_MONOTONIC_RAW
#define CLOCK_MONOTONIC_RAW 4
#endif
#ifndef CLOCK_BOOTTIME
#define CLOCK_BOOTTIME 7
#endif
#ifndef CLOCK_TAI
#define CLOCK_TAI 11
#endif

static const struct clockid_map clockids[] = {
	/* available for all events, NMI safe */
	CLOCKID_MAP("monotonic", CLOCK_MONOTONIC),
	CLOCKID_MAP("monotonic_raw", CLOCK_MONOTONIC_RAW),

	/* available for some events */
	CLOCKID_MAP("realtime", CLOCK_REALTIME),
	CLOCKID_MAP("boottime", CLOCK_BOOTTIME),
	CLOCKID_MAP("tai", CLOCK_TAI),

	/* available for the lazy */
	CLOCKID_MAP("mono", CLOCK_MONOTONIC),
	CLOCKID_MAP("raw", CLOCK_MONOTONIC_RAW),
	CLOCKID_MAP("real", CLOCK_REALTIME),
	CLOCKID_MAP("boot", CLOCK_BOOTTIME),

	CLOCKID_END,
};

static int get_clockid_res(clockid_t clk_id, u64 *res_ns)
{
	struct timespec res;

	*res_ns = 0;
	if (!clock_getres(clk_id, &res))
		*res_ns = res.tv_nsec + res.tv_sec * NSEC_PER_SEC;
	else
		pr_warning("WARNING: Failed to determine specified clock resolution.\n");

	return 0;
}

int parse_clockid(const struct option *opt, const char *str, int unset)
{
	struct record_opts *opts = (struct record_opts *)opt->value;
	const struct clockid_map *cm;
	const char *ostr = str;

	if (unset) {
		opts->use_clockid = 0;
		return 0;
	}

	/* no arg passed */
	if (!str)
		return 0;

	/* no setting it twice */
	if (opts->use_clockid)
		return -1;

	opts->use_clockid = true;

	/* if its a number, we're done */
	if (sscanf(str, "%d", &opts->clockid) == 1)
		return get_clockid_res(opts->clockid, &opts->clockid_res_ns);

	/* allow a "CLOCK_" prefix to the name */
	if (!strncasecmp(str, "CLOCK_", 6))
		str += 6;

	for (cm = clockids; cm->name; cm++) {
		if (!strcasecmp(str, cm->name)) {
			opts->clockid = cm->clockid;
			return get_clockid_res(opts->clockid,
					       &opts->clockid_res_ns);
		}
	}

	opts->use_clockid = false;
	ui__warning("unknown clockid %s, check man page\n", ostr);
	return -1;
}

const char *clockid_name(clockid_t clk_id)
{
	const struct clockid_map *cm;

	for (cm = clockids; cm->name; cm++) {
		if (cm->clockid == clk_id)
			return cm->name;
	}
	return "(not found)";
}
