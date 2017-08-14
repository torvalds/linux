#include <stdbool.h>
#include <inttypes.h>
#include <linux/kernel.h>
#include <linux/types.h>

#include "util.h"
#include "event.h"
#include "evsel.h"
#include "debug.h"

#include "tests.h"

#define COMP(m) do {					\
	if (s1->m != s2->m) {				\
		pr_debug("Samples differ at '"#m"'\n");	\
		return false;				\
	}						\
} while (0)

#define MCOMP(m) do {					\
	if (memcmp(&s1->m, &s2->m, sizeof(s1->m))) {	\
		pr_debug("Samples differ at '"#m"'\n");	\
		return false;				\
	}						\
} while (0)

static bool samples_same(const struct perf_sample *s1,
			 const struct perf_sample *s2,
			 u64 type, u64 read_format)
{
	size_t i;

	if (type & PERF_SAMPLE_IDENTIFIER)
		COMP(id);

	if (type & PERF_SAMPLE_IP)
		COMP(ip);

	if (type & PERF_SAMPLE_TID) {
		COMP(pid);
		COMP(tid);
	}

	if (type & PERF_SAMPLE_TIME)
		COMP(time);

	if (type & PERF_SAMPLE_ADDR)
		COMP(addr);

	if (type & PERF_SAMPLE_ID)
		COMP(id);

	if (type & PERF_SAMPLE_STREAM_ID)
		COMP(stream_id);

	if (type & PERF_SAMPLE_CPU)
		COMP(cpu);

	if (type & PERF_SAMPLE_PERIOD)
		COMP(period);

	if (type & PERF_SAMPLE_READ) {
		if (read_format & PERF_FORMAT_GROUP)
			COMP(read.group.nr);
		else
			COMP(read.one.value);
		if (read_format & PERF_FORMAT_TOTAL_TIME_ENABLED)
			COMP(read.time_enabled);
		if (read_format & PERF_FORMAT_TOTAL_TIME_RUNNING)
			COMP(read.time_running);
		/* PERF_FORMAT_ID is forced for PERF_SAMPLE_READ */
		if (read_format & PERF_FORMAT_GROUP) {
			for (i = 0; i < s1->read.group.nr; i++)
				MCOMP(read.group.values[i]);
		} else {
			COMP(read.one.id);
		}
	}

	if (type & PERF_SAMPLE_CALLCHAIN) {
		COMP(callchain->nr);
		for (i = 0; i < s1->callchain->nr; i++)
			COMP(callchain->ips[i]);
	}

	if (type & PERF_SAMPLE_RAW) {
		COMP(raw_size);
		if (memcmp(s1->raw_data, s2->raw_data, s1->raw_size)) {
			pr_debug("Samples differ at 'raw_data'\n");
			return false;
		}
	}

	if (type & PERF_SAMPLE_BRANCH_STACK) {
		COMP(branch_stack->nr);
		for (i = 0; i < s1->branch_stack->nr; i++)
			MCOMP(branch_stack->entries[i]);
	}

	if (type & PERF_SAMPLE_REGS_USER) {
		size_t sz = hweight_long(s1->user_regs.mask) * sizeof(u64);

		COMP(user_regs.mask);
		COMP(user_regs.abi);
		if (s1->user_regs.abi &&
		    (!s1->user_regs.regs || !s2->user_regs.regs ||
		     memcmp(s1->user_regs.regs, s2->user_regs.regs, sz))) {
			pr_debug("Samples differ at 'user_regs'\n");
			return false;
		}
	}

	if (type & PERF_SAMPLE_STACK_USER) {
		COMP(user_stack.size);
		if (memcmp(s1->user_stack.data, s2->user_stack.data,
			   s1->user_stack.size)) {
			pr_debug("Samples differ at 'user_stack'\n");
			return false;
		}
	}

	if (type & PERF_SAMPLE_WEIGHT)
		COMP(weight);

	if (type & PERF_SAMPLE_DATA_SRC)
		COMP(data_src);

	if (type & PERF_SAMPLE_TRANSACTION)
		COMP(transaction);

	if (type & PERF_SAMPLE_REGS_INTR) {
		size_t sz = hweight_long(s1->intr_regs.mask) * sizeof(u64);

		COMP(intr_regs.mask);
		COMP(intr_regs.abi);
		if (s1->intr_regs.abi &&
		    (!s1->intr_regs.regs || !s2->intr_regs.regs ||
		     memcmp(s1->intr_regs.regs, s2->intr_regs.regs, sz))) {
			pr_debug("Samples differ at 'intr_regs'\n");
			return false;
		}
	}

	return true;
}

static int do_test(u64 sample_type, u64 sample_regs, u64 read_format)
{
	struct perf_evsel evsel = {
		.needs_swap = false,
		.attr = {
			.sample_type = sample_type,
			.read_format = read_format,
		},
	};
	union perf_event *event;
	union {
		struct ip_callchain callchain;
		u64 data[64];
	} callchain = {
		/* 3 ips */
		.data = {3, 201, 202, 203},
	};
	union {
		struct branch_stack branch_stack;
		u64 data[64];
	} branch_stack = {
		/* 1 branch_entry */
		.data = {1, 211, 212, 213},
	};
	u64 regs[64];
	const u64 raw_data[] = {0x123456780a0b0c0dULL, 0x1102030405060708ULL};
	const u64 data[] = {0x2211443366558877ULL, 0, 0xaabbccddeeff4321ULL};
	struct perf_sample sample = {
		.ip		= 101,
		.pid		= 102,
		.tid		= 103,
		.time		= 104,
		.addr		= 105,
		.id		= 106,
		.stream_id	= 107,
		.period		= 108,
		.weight		= 109,
		.cpu		= 110,
		.raw_size	= sizeof(raw_data),
		.data_src	= 111,
		.transaction	= 112,
		.raw_data	= (void *)raw_data,
		.callchain	= &callchain.callchain,
		.branch_stack	= &branch_stack.branch_stack,
		.user_regs	= {
			.abi	= PERF_SAMPLE_REGS_ABI_64,
			.mask	= sample_regs,
			.regs	= regs,
		},
		.user_stack	= {
			.size	= sizeof(data),
			.data	= (void *)data,
		},
		.read		= {
			.time_enabled = 0x030a59d664fca7deULL,
			.time_running = 0x011b6ae553eb98edULL,
		},
		.intr_regs	= {
			.abi	= PERF_SAMPLE_REGS_ABI_64,
			.mask	= sample_regs,
			.regs	= regs,
		},
	};
	struct sample_read_value values[] = {{1, 5}, {9, 3}, {2, 7}, {6, 4},};
	struct perf_sample sample_out;
	size_t i, sz, bufsz;
	int err, ret = -1;

	if (sample_type & PERF_SAMPLE_REGS_USER)
		evsel.attr.sample_regs_user = sample_regs;

	if (sample_type & PERF_SAMPLE_REGS_INTR)
		evsel.attr.sample_regs_intr = sample_regs;

	for (i = 0; i < sizeof(regs); i++)
		*(i + (u8 *)regs) = i & 0xfe;

	if (read_format & PERF_FORMAT_GROUP) {
		sample.read.group.nr     = 4;
		sample.read.group.values = values;
	} else {
		sample.read.one.value = 0x08789faeb786aa87ULL;
		sample.read.one.id    = 99;
	}

	sz = perf_event__sample_event_size(&sample, sample_type, read_format);
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

	err = perf_event__synthesize_sample(event, sample_type, read_format,
					    &sample, false);
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

	evsel.sample_size = __perf_evsel__sample_size(sample_type);

	err = perf_evsel__parse_sample(&evsel, event, &sample_out);
	if (err) {
		pr_debug("%s failed for sample_type %#"PRIx64", error %d\n",
			 "perf_evsel__parse_sample", sample_type, err);
		goto out_free;
	}

	if (!samples_same(&sample, &sample_out, sample_type, read_format)) {
		pr_debug("parsing failed for sample_type %#"PRIx64"\n",
			 sample_type);
		goto out_free;
	}

	ret = 0;
out_free:
	free(event);
	if (ret && read_format)
		pr_debug("read_format %#"PRIx64"\n", read_format);
	return ret;
}

/**
 * test__sample_parsing - test sample parsing.
 *
 * This function implements a test that synthesizes a sample event, parses it
 * and then checks that the parsed sample matches the original sample.  The test
 * checks sample format bits separately and together.  If the test passes %0 is
 * returned, otherwise %-1 is returned.
 */
int test__sample_parsing(struct test *test __maybe_unused, int subtest __maybe_unused)
{
	const u64 rf[] = {4, 5, 6, 7, 12, 13, 14, 15};
	u64 sample_type;
	u64 sample_regs;
	size_t i;
	int err;

	/*
	 * Fail the test if it has not been updated when new sample format bits
	 * were added.  Please actually update the test rather than just change
	 * the condition below.
	 */
	if (PERF_SAMPLE_MAX > PERF_SAMPLE_REGS_INTR << 1) {
		pr_debug("sample format has changed, some new PERF_SAMPLE_ bit was introduced - test needs updating\n");
		return -1;
	}

	/* Test each sample format bit separately */
	for (sample_type = 1; sample_type != PERF_SAMPLE_MAX;
	     sample_type <<= 1) {
		/* Test read_format variations */
		if (sample_type == PERF_SAMPLE_READ) {
			for (i = 0; i < ARRAY_SIZE(rf); i++) {
				err = do_test(sample_type, 0, rf[i]);
				if (err)
					return err;
			}
			continue;
		}
		sample_regs = 0;

		if (sample_type == PERF_SAMPLE_REGS_USER)
			sample_regs = 0x3fff;

		if (sample_type == PERF_SAMPLE_REGS_INTR)
			sample_regs = 0xff0fff;

		err = do_test(sample_type, sample_regs, 0);
		if (err)
			return err;
	}

	/* Test all sample format bits together */
	sample_type = PERF_SAMPLE_MAX - 1;
	sample_regs = 0x3fff; /* shared yb intr and user regs */
	for (i = 0; i < ARRAY_SIZE(rf); i++) {
		err = do_test(sample_type, sample_regs, rf[i]);
		if (err)
			return err;
	}

	return 0;
}
