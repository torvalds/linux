// SPDX-License-Identifier: GPL-2.0
#include <linux/compiler.h>
#include <linux/time64.h>
#include <inttypes.h>
#include <string.h>
#include "time-utils.h"
#include "evlist.h"
#include "session.h"
#include "debug.h"
#include "tests.h"

static bool test__parse_nsec_time(const char *str, u64 expected)
{
	u64 ptime;
	int err;

	pr_debug("\nparse_nsec_time(\"%s\")\n", str);

	err = parse_nsec_time(str, &ptime);
	if (err) {
		pr_debug("error %d\n", err);
		return false;
	}

	if (ptime != expected) {
		pr_debug("Failed. ptime %" PRIu64 " expected %" PRIu64 "\n",
			 ptime, expected);
		return false;
	}

	pr_debug("%" PRIu64 "\n", ptime);

	return true;
}

static bool test__perf_time__parse_str(const char *ostr, u64 start, u64 end)
{
	struct perf_time_interval ptime;
	int err;

	pr_debug("\nperf_time__parse_str(\"%s\")\n", ostr);

	err = perf_time__parse_str(&ptime, ostr);
	if (err) {
		pr_debug("Error %d\n", err);
		return false;
	}

	if (ptime.start != start || ptime.end != end) {
		pr_debug("Failed. Expected %" PRIu64 " to %" PRIu64 "\n",
			 start, end);
		return false;
	}

	return true;
}

#define TEST_MAX 64

struct test_data {
	const char *str;
	u64 first;
	u64 last;
	struct perf_time_interval ptime[TEST_MAX];
	int num;
	u64 skip[TEST_MAX];
	u64 noskip[TEST_MAX];
};

static bool test__perf_time__parse_for_ranges(struct test_data *d)
{
	struct perf_evlist evlist = {
		.first_sample_time = d->first,
		.last_sample_time = d->last,
	};
	struct perf_session session = { .evlist = &evlist };
	struct perf_time_interval *ptime = NULL;
	int range_size, range_num;
	bool pass = false;
	int i, err;

	pr_debug("\nperf_time__parse_for_ranges(\"%s\")\n", d->str);

	if (strchr(d->str, '%'))
		pr_debug("first_sample_time %" PRIu64 " last_sample_time %" PRIu64 "\n",
			 d->first, d->last);

	err = perf_time__parse_for_ranges(d->str, &session, &ptime, &range_size,
					  &range_num);
	if (err) {
		pr_debug("error %d\n", err);
		goto out;
	}

	if (range_size < d->num || range_num != d->num) {
		pr_debug("bad size: range_size %d range_num %d expected num %d\n",
			 range_size, range_num, d->num);
		goto out;
	}

	for (i = 0; i < d->num; i++) {
		if (ptime[i].start != d->ptime[i].start ||
		    ptime[i].end != d->ptime[i].end) {
			pr_debug("bad range %d expected %" PRIu64 " to %" PRIu64 "\n",
				 i, d->ptime[i].start, d->ptime[i].end);
			goto out;
		}
	}

	if (perf_time__ranges_skip_sample(ptime, d->num, 0)) {
		pr_debug("failed to keep 0\n");
		goto out;
	}

	for (i = 0; i < TEST_MAX; i++) {
		if (d->skip[i] &&
		    !perf_time__ranges_skip_sample(ptime, d->num, d->skip[i])) {
			pr_debug("failed to skip %" PRIu64 "\n", d->skip[i]);
			goto out;
		}
		if (d->noskip[i] &&
		    perf_time__ranges_skip_sample(ptime, d->num, d->noskip[i])) {
			pr_debug("failed to keep %" PRIu64 "\n", d->noskip[i]);
			goto out;
		}
	}

	pass = true;
out:
	free(ptime);
	return pass;
}

int test__time_utils(struct test *t __maybe_unused, int subtest __maybe_unused)
{
	bool pass = true;

	pass &= test__parse_nsec_time("0", 0);
	pass &= test__parse_nsec_time("1", 1000000000ULL);
	pass &= test__parse_nsec_time("0.000000001", 1);
	pass &= test__parse_nsec_time("1.000000001", 1000000001ULL);
	pass &= test__parse_nsec_time("123456.123456", 123456123456000ULL);
	pass &= test__parse_nsec_time("1234567.123456789", 1234567123456789ULL);
	pass &= test__parse_nsec_time("18446744073.709551615",
				      0xFFFFFFFFFFFFFFFFULL);

	pass &= test__perf_time__parse_str("1234567.123456789,1234567.123456789",
					   1234567123456789ULL, 1234567123456789ULL);
	pass &= test__perf_time__parse_str("1234567.123456789,1234567.123456790",
					   1234567123456789ULL, 1234567123456790ULL);
	pass &= test__perf_time__parse_str("1234567.123456789,",
					   1234567123456789ULL, 0);
	pass &= test__perf_time__parse_str(",1234567.123456789",
					   0, 1234567123456789ULL);
	pass &= test__perf_time__parse_str("0,1234567.123456789",
					   0, 1234567123456789ULL);

	{
		u64 b = 1234567123456789ULL;
		struct test_data d = {
			.str   = "1234567.123456789,1234567.123456790",
			.ptime = { {b, b + 1}, },
			.num = 1,
			.skip = { b - 1, b + 2, },
			.noskip = { b, b + 1, },
		};

		pass &= test__perf_time__parse_for_ranges(&d);
	}

	{
		u64 b = 7654321ULL * NSEC_PER_SEC;
		struct test_data d = {
			.str    = "10%/1",
			.first  = b,
			.last   = b + 100,
			.ptime  = { {b, b + 9}, },
			.num    = 1,
			.skip   = { b - 1, b + 10, },
			.noskip = { b, b + 9, },
		};

		pass &= test__perf_time__parse_for_ranges(&d);
	}

	{
		u64 b = 7654321ULL * NSEC_PER_SEC;
		struct test_data d = {
			.str    = "10%/2",
			.first  = b,
			.last   = b + 100,
			.ptime  = { {b + 10, b + 19}, },
			.num    = 1,
			.skip   = { b + 9, b + 20, },
			.noskip = { b + 10, b + 19, },
		};

		pass &= test__perf_time__parse_for_ranges(&d);
	}

	{
		u64 b = 11223344ULL * NSEC_PER_SEC;
		struct test_data d = {
			.str    = "10%/1,10%/2",
			.first  = b,
			.last   = b + 100,
			.ptime  = { {b, b + 9}, {b + 10, b + 19}, },
			.num    = 2,
			.skip   = { b - 1, b + 20, },
			.noskip = { b, b + 8, b + 9, b + 10, b + 11, b + 12, b + 19, },
		};

		pass &= test__perf_time__parse_for_ranges(&d);
	}

	{
		u64 b = 11223344ULL * NSEC_PER_SEC;
		struct test_data d = {
			.str    = "10%/1,10%/3,10%/10",
			.first  = b,
			.last   = b + 100,
			.ptime  = { {b, b + 9}, {b + 20, b + 29}, { b + 90, b + 100}, },
			.num    = 3,
			.skip   = { b - 1, b + 10, b + 19, b + 30, b + 89, b + 101 },
			.noskip = { b, b + 9, b + 20, b + 29, b + 90, b + 100},
		};

		pass &= test__perf_time__parse_for_ranges(&d);
	}

	pr_debug("\n");

	return pass ? 0 : TEST_FAIL;
}
