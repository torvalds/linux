// SPDX-License-Identifier: GPL-2.0-only
#include <stdbool.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/types.h>

#include "event.h"
#include "evsel.h"
#include "debug.h"
#include "util/synthetic-events.h"

#include "tests/tests.h"
#include "arch-tests.h"

#define COMP(m) do {					\
	if (s1->m != s2->m) {				\
		pr_debug("Samples differ at '"#m"'\n");	\
		return false;				\
	}						\
} while (0)

static bool samples_same(const struct perf_sample *s1,
			 const struct perf_sample *s2,
			 u64 type)
{
	if (type & PERF_SAMPLE_WEIGHT_STRUCT)
		COMP(ins_lat);

	return true;
}

static int do_test(u64 sample_type)
{
	struct evsel evsel = {
		.needs_swap = false,
		.core = {
			. attr = {
				.sample_type = sample_type,
				.read_format = 0,
			},
		},
	};
	union perf_event *event;
	struct perf_sample sample = {
		.weight		= 101,
		.ins_lat        = 102,
	};
	struct perf_sample sample_out;
	size_t i, sz, bufsz;
	int err, ret = -1;

	sz = perf_event__sample_event_size(&sample, sample_type, 0);
	bufsz = sz + 4096; /* Add a bit for overrun checking */
	event = malloc(bufsz);
	if (!event) {
		pr_debug("malloc failed\n");
		return -1;
	}

	memset(event, 0xff, bufsz);
	event->header.type = PERF_RECORD_SAMPLE;
	event->header.misc = 0;
	event->header.size = sz;

	err = perf_event__synthesize_sample(event, sample_type, 0, &sample);
	if (err) {
		pr_debug("%s failed for sample_type %#"PRIx64", error %d\n",
			 "perf_event__synthesize_sample", sample_type, err);
		goto out_free;
	}

	/* The data does not contain 0xff so we use that to check the size */
	for (i = bufsz; i > 0; i--) {
		if (*(i - 1 + (u8 *)event) != 0xff)
			break;
	}
	if (i != sz) {
		pr_debug("Event size mismatch: actual %zu vs expected %zu\n",
			 i, sz);
		goto out_free;
	}

	evsel.sample_size = __evsel__sample_size(sample_type);

	err = evsel__parse_sample(&evsel, event, &sample_out);
	if (err) {
		pr_debug("%s failed for sample_type %#"PRIx64", error %d\n",
			 "evsel__parse_sample", sample_type, err);
		goto out_free;
	}

	if (!samples_same(&sample, &sample_out, sample_type)) {
		pr_debug("parsing failed for sample_type %#"PRIx64"\n",
			 sample_type);
		goto out_free;
	}

	ret = 0;
out_free:
	free(event);

	return ret;
}

/**
 * test__x86_sample_parsing - test X86 specific sample parsing
 *
 * This function implements a test that synthesizes a sample event, parses it
 * and then checks that the parsed sample matches the original sample. If the
 * test passes %0 is returned, otherwise %-1 is returned.
 *
 * For now, the PERF_SAMPLE_WEIGHT_STRUCT is the only X86 specific sample type.
 * The test only checks the PERF_SAMPLE_WEIGHT_STRUCT type.
 */
int test__x86_sample_parsing(struct test *test __maybe_unused, int subtest __maybe_unused)
{
	return do_test(PERF_SAMPLE_WEIGHT_STRUCT);
}
