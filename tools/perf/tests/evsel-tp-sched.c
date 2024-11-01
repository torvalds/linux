// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <traceevent/event-parse.h>
#include "evsel.h"
#include "tests.h"
#include "debug.h"

static int evsel__test_field(struct evsel *evsel, const char *name, int size, bool should_be_signed)
{
	struct tep_format_field *field = evsel__field(evsel, name);
	int is_signed;
	int ret = 0;

	if (field == NULL) {
		pr_debug("%s: \"%s\" field not found!\n", evsel->name, name);
		return -1;
	}

	is_signed = !!(field->flags & TEP_FIELD_IS_SIGNED);
	if (should_be_signed && !is_signed) {
		pr_debug("%s: \"%s\" signedness(%d) is wrong, should be %d\n",
			 evsel->name, name, is_signed, should_be_signed);
		ret = -1;
	}

	if (field->size != size) {
		pr_debug("%s: \"%s\" size (%d) should be %d!\n",
			 evsel->name, name, field->size, size);
		ret = -1;
	}

	return ret;
}

static int test__perf_evsel__tp_sched_test(struct test_suite *test __maybe_unused,
					   int subtest __maybe_unused)
{
	struct evsel *evsel = evsel__newtp("sched", "sched_switch");
	int ret = 0;

	if (IS_ERR(evsel)) {
		pr_debug("evsel__newtp failed with %ld\n", PTR_ERR(evsel));
		return -1;
	}

	if (evsel__test_field(evsel, "prev_comm", 16, false))
		ret = -1;

	if (evsel__test_field(evsel, "prev_pid", 4, true))
		ret = -1;

	if (evsel__test_field(evsel, "prev_prio", 4, true))
		ret = -1;

	if (evsel__test_field(evsel, "prev_state", sizeof(long), true))
		ret = -1;

	if (evsel__test_field(evsel, "next_comm", 16, false))
		ret = -1;

	if (evsel__test_field(evsel, "next_pid", 4, true))
		ret = -1;

	if (evsel__test_field(evsel, "next_prio", 4, true))
		ret = -1;

	evsel__delete(evsel);

	evsel = evsel__newtp("sched", "sched_wakeup");

	if (IS_ERR(evsel)) {
		pr_debug("evsel__newtp failed with %ld\n", PTR_ERR(evsel));
		return -1;
	}

	if (evsel__test_field(evsel, "comm", 16, false))
		ret = -1;

	if (evsel__test_field(evsel, "pid", 4, true))
		ret = -1;

	if (evsel__test_field(evsel, "prio", 4, true))
		ret = -1;

	if (evsel__test_field(evsel, "target_cpu", 4, true))
		ret = -1;

	evsel__delete(evsel);
	return ret;
}

DEFINE_SUITE("Parse sched tracepoints fields", perf_evsel__tp_sched_test);
