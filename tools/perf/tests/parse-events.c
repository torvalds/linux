// SPDX-License-Identifier: GPL-2.0
#include "parse-events.h"
#include "evsel.h"
#include "evsel_fprintf.h"
#include "evlist.h"
#include <api/fs/fs.h>
#include "tests.h"
#include "debug.h"
#include "pmu.h"
#include "pmus.h"
#include "strbuf.h"
#include <dirent.h>
#include <errno.h>
#include "fncache.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/kernel.h>
#include <linux/hw_breakpoint.h>
#include <api/fs/tracing_path.h>

#define PERF_TP_SAMPLE_TYPE (PERF_SAMPLE_RAW | PERF_SAMPLE_TIME | \
			     PERF_SAMPLE_CPU | PERF_SAMPLE_PERIOD)

static bool check_evlist(const char *test, int line, bool cond, struct evlist *evlist)
{
	struct strbuf sb = STRBUF_INIT;

	if (cond)
		return true;

	evlist__format_evsels(evlist, &sb, 2048);
	pr_debug("FAILED %s:%d: %s\nFor evlist: %s\n", __FILE__, line, test, sb.buf);
	strbuf_release(&sb);
	return false;
}
#define TEST_ASSERT_EVLIST(test, cond, evlist) \
	if (!check_evlist(test, __LINE__, cond, evlist)) \
		return TEST_FAIL

static bool check_evsel(const char *test, int line, bool cond, struct evsel *evsel)
{
	struct perf_attr_details details = { .verbose = true, };

	if (cond)
		return true;

	pr_debug("FAILED %s:%d: %s\nFor evsel: ", __FILE__, line, test);
	evsel__fprintf(evsel, &details, debug_file());
	return false;
}
#define TEST_ASSERT_EVSEL(test, cond, evsel) \
	if (!check_evsel(test, __LINE__, cond, evsel)) \
		return TEST_FAIL

static int num_core_entries(struct evlist *evlist)
{
	/*
	 * Returns number of core PMUs if the evlist has >1 core PMU, otherwise
	 * returns 1.  The number of core PMUs is needed as wild carding can
	 * open an event for each core PMU. If the events were opened with a
	 * specified PMU then wild carding won't happen.
	 */
	struct perf_pmu *core_pmu = NULL;
	struct evsel *evsel;

	evlist__for_each_entry(evlist, evsel) {
		if (!evsel->pmu->is_core)
			continue;
		if (core_pmu != evsel->pmu && core_pmu != NULL)
			return perf_pmus__num_core_pmus();
		core_pmu = evsel->pmu;
	}
	return 1;
}

static bool test_hw_config(const struct evsel *evsel, __u64 expected_config)
{
	return (evsel->core.attr.config & PERF_HW_EVENT_MASK) == expected_config;
}

#if defined(__s390x__)
/* Return true if kvm module is available and loaded. Test this
 * and return success when trace point kvm_s390_create_vm
 * exists. Otherwise this test always fails.
 */
static bool kvm_s390_create_vm_valid(void)
{
	char *eventfile;
	bool rc = false;

	eventfile = get_events_file("kvm-s390");

	if (eventfile) {
		DIR *mydir = opendir(eventfile);

		if (mydir) {
			rc = true;
			closedir(mydir);
		}
		put_events_file(eventfile);
	}

	return rc;
}
#endif

static int test__checkevent_tracepoint(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_EVLIST("wrong number of entries", 1 == evlist->core.nr_entries, evlist);
	TEST_ASSERT_EVLIST("wrong number of groups", 0 == evlist__nr_groups(evlist), evlist);
	TEST_ASSERT_EVSEL("wrong type", PERF_TYPE_TRACEPOINT == evsel->core.attr.type, evsel);
	TEST_ASSERT_EVSEL("wrong sample_type",
			  PERF_TP_SAMPLE_TYPE == evsel->core.attr.sample_type, evsel);
	TEST_ASSERT_EVSEL("wrong sample_period", 1 == evsel->core.attr.sample_period, evsel);
	return TEST_OK;
}

static int test__checkevent_tracepoint_multi(struct evlist *evlist)
{
	struct evsel *evsel;

	TEST_ASSERT_EVLIST("wrong number of entries", evlist->core.nr_entries > 1, evlist);
	TEST_ASSERT_EVLIST("wrong number of groups", 0 == evlist__nr_groups(evlist), evlist);

	evlist__for_each_entry(evlist, evsel) {
		TEST_ASSERT_EVSEL("wrong type",
				  PERF_TYPE_TRACEPOINT == evsel->core.attr.type,
				  evsel);
		TEST_ASSERT_EVSEL("wrong sample_type",
				  PERF_TP_SAMPLE_TYPE == evsel->core.attr.sample_type,
				  evsel);
		TEST_ASSERT_EVSEL("wrong sample_period",
				  1 == evsel->core.attr.sample_period,
				  evsel);
	}
	return TEST_OK;
}

static int test__checkevent_raw(struct evlist *evlist)
{
	struct evsel *evsel;
	bool raw_type_match = false;

	TEST_ASSERT_EVLIST("wrong number of entries", 0 != evlist->core.nr_entries, evlist);

	evlist__for_each_entry(evlist, evsel) {
		struct perf_pmu *pmu __maybe_unused = NULL;
		bool type_matched = false;

		TEST_ASSERT_EVSEL("wrong config", test_hw_config(evsel, 0x1a), evsel);
		TEST_ASSERT_EVSEL("event not parsed as raw type",
				  evsel->core.attr.type == PERF_TYPE_RAW,
				  evsel);
#if defined(__aarch64__)
		/*
		 * Arm doesn't have a real raw type PMU in sysfs, so raw events
		 * would never match any PMU. However, RAW events on Arm will
		 * always successfully open on the first available core PMU
		 * so no need to test for a matching type here.
		 */
		type_matched = raw_type_match = true;
#else
		while ((pmu = perf_pmus__scan(pmu)) != NULL) {
			if (pmu->type == evsel->core.attr.type) {
				TEST_ASSERT_EVSEL("PMU type expected once", !type_matched, evsel);
				type_matched = true;
				if (pmu->type == PERF_TYPE_RAW)
					raw_type_match = true;
			}
		}
#endif
		TEST_ASSERT_EVSEL("No PMU found for type", type_matched, evsel);
	}
	TEST_ASSERT_VAL("Raw PMU not matched", raw_type_match);
	return TEST_OK;
}

static int test__checkevent_numeric(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_EVLIST("wrong number of entries", 1 == evlist->core.nr_entries, evlist);
	TEST_ASSERT_EVSEL("wrong type", 1 == evsel->core.attr.type, evsel);
	TEST_ASSERT_EVSEL("wrong config", 1 == evsel->core.attr.config, evsel);
	return TEST_OK;
}


static int test__checkevent_symbolic_name(struct evlist *evlist)
{
	struct evsel *evsel;

	TEST_ASSERT_EVLIST("wrong number of entries", 0 != evlist->core.nr_entries, evlist);

	evlist__for_each_entry(evlist, evsel) {
		TEST_ASSERT_EVSEL("unexpected event",
				  evsel__match(evsel, HARDWARE, HW_INSTRUCTIONS),
				  evsel);
	}
	return TEST_OK;
}

static int test__checkevent_symbolic_name_config(struct evlist *evlist)
{
	struct evsel *evsel;

	TEST_ASSERT_EVLIST("wrong number of entries", 0 != evlist->core.nr_entries, evlist);

	evlist__for_each_entry(evlist, evsel) {
		TEST_ASSERT_EVSEL("unexpected event",
				  evsel__match(evsel, HARDWARE, HW_CPU_CYCLES),
				  evsel);
		/*
		 * The period value gets configured within evlist__config,
		 * while this test executes only parse events method.
		 */
		TEST_ASSERT_EVSEL("wrong period", 0 == evsel->core.attr.sample_period, evsel);
		TEST_ASSERT_EVSEL("wrong config1", 0 == evsel->core.attr.config1, evsel);
		TEST_ASSERT_EVSEL("wrong config2", 1 == evsel->core.attr.config2, evsel);
	}
	return TEST_OK;
}

static int test__checkevent_symbolic_alias(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_EVLIST("wrong number of entries", 1 == evlist->core.nr_entries, evlist);
	TEST_ASSERT_EVSEL("wrong type/config", evsel__match(evsel, SOFTWARE, SW_PAGE_FAULTS),
			  evsel);
	return TEST_OK;
}

static int test__checkevent_genhw(struct evlist *evlist)
{
	struct evsel *evsel;

	TEST_ASSERT_EVLIST("wrong number of entries", 0 != evlist->core.nr_entries, evlist);

	evlist__for_each_entry(evlist, evsel) {
		TEST_ASSERT_EVSEL("wrong type", PERF_TYPE_HW_CACHE == evsel->core.attr.type, evsel);
		TEST_ASSERT_EVSEL("wrong config", test_hw_config(evsel, 1 << 16), evsel);
	}
	return TEST_OK;
}

static int test__checkevent_breakpoint(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_EVLIST("wrong number of entries", 1 == evlist->core.nr_entries, evlist);
	TEST_ASSERT_EVSEL("wrong type", PERF_TYPE_BREAKPOINT == evsel->core.attr.type, evsel);
	TEST_ASSERT_EVSEL("wrong config", 0 == evsel->core.attr.config, evsel);
	TEST_ASSERT_EVSEL("wrong bp_type",
			  (HW_BREAKPOINT_R | HW_BREAKPOINT_W) == evsel->core.attr.bp_type,
			  evsel);
	TEST_ASSERT_EVSEL("wrong bp_len", HW_BREAKPOINT_LEN_4 == evsel->core.attr.bp_len, evsel);
	return TEST_OK;
}

static int test__checkevent_breakpoint_x(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_EVLIST("wrong number of entries", 1 == evlist->core.nr_entries, evlist);
	TEST_ASSERT_EVSEL("wrong type", PERF_TYPE_BREAKPOINT == evsel->core.attr.type, evsel);
	TEST_ASSERT_EVSEL("wrong config", 0 == evsel->core.attr.config, evsel);
	TEST_ASSERT_EVSEL("wrong bp_type", HW_BREAKPOINT_X == evsel->core.attr.bp_type, evsel);
	TEST_ASSERT_EVSEL("wrong bp_len", default_breakpoint_len() == evsel->core.attr.bp_len,
			  evsel);
	return TEST_OK;
}

static int test__checkevent_breakpoint_r(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_EVLIST("wrong number of entries", 1 == evlist->core.nr_entries, evlist);
	TEST_ASSERT_EVSEL("wrong type", PERF_TYPE_BREAKPOINT == evsel->core.attr.type, evsel);
	TEST_ASSERT_EVSEL("wrong config", 0 == evsel->core.attr.config, evsel);
	TEST_ASSERT_EVSEL("wrong bp_type", HW_BREAKPOINT_R == evsel->core.attr.bp_type, evsel);
	TEST_ASSERT_EVSEL("wrong bp_len", HW_BREAKPOINT_LEN_4 == evsel->core.attr.bp_len, evsel);
	return TEST_OK;
}

static int test__checkevent_breakpoint_w(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_EVLIST("wrong number of entries", 1 == evlist->core.nr_entries, evlist);
	TEST_ASSERT_EVSEL("wrong type", PERF_TYPE_BREAKPOINT == evsel->core.attr.type, evsel);
	TEST_ASSERT_EVSEL("wrong config", 0 == evsel->core.attr.config, evsel);
	TEST_ASSERT_EVSEL("wrong bp_type", HW_BREAKPOINT_W == evsel->core.attr.bp_type, evsel);
	TEST_ASSERT_EVSEL("wrong bp_len", HW_BREAKPOINT_LEN_4 == evsel->core.attr.bp_len, evsel);
	return TEST_OK;
}

static int test__checkevent_breakpoint_rw(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_EVLIST("wrong number of entries", 1 == evlist->core.nr_entries, evlist);
	TEST_ASSERT_EVSEL("wrong type", PERF_TYPE_BREAKPOINT == evsel->core.attr.type, evsel);
	TEST_ASSERT_EVSEL("wrong config", 0 == evsel->core.attr.config, evsel);
	TEST_ASSERT_EVSEL("wrong bp_type",
			  (HW_BREAKPOINT_R|HW_BREAKPOINT_W) == evsel->core.attr.bp_type,
			  evsel);
	TEST_ASSERT_EVSEL("wrong bp_len", HW_BREAKPOINT_LEN_4 == evsel->core.attr.bp_len, evsel);
	return TEST_OK;
}

static int test__checkevent_tracepoint_modifier(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_EVLIST("wrong number of entries", 1 == evlist->core.nr_entries, evlist);
	TEST_ASSERT_EVSEL("wrong exclude_user", evsel->core.attr.exclude_user, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_hv", evsel->core.attr.exclude_hv, evsel);
	TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);

	return test__checkevent_tracepoint(evlist);
}

static int
test__checkevent_tracepoint_multi_modifier(struct evlist *evlist)
{
	struct evsel *evsel;

	TEST_ASSERT_EVLIST("wrong number of entries", evlist->core.nr_entries > 1, evlist);

	evlist__for_each_entry(evlist, evsel) {
		TEST_ASSERT_EVSEL("wrong exclude_user", !evsel->core.attr.exclude_user, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_kernel", evsel->core.attr.exclude_kernel, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_hv", evsel->core.attr.exclude_hv, evsel);
		TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);
	}

	return test__checkevent_tracepoint_multi(evlist);
}

static int test__checkevent_raw_modifier(struct evlist *evlist)
{
	struct evsel *evsel;

	TEST_ASSERT_EVLIST("wrong number of entries", 1 == evlist->core.nr_entries, evlist);

	evlist__for_each_entry(evlist, evsel) {
		TEST_ASSERT_EVSEL("wrong exclude_user", evsel->core.attr.exclude_user, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_hv", evsel->core.attr.exclude_hv, evsel);
		TEST_ASSERT_EVSEL("wrong precise_ip", evsel->core.attr.precise_ip, evsel);
	}
	return test__checkevent_raw(evlist);
}

static int test__checkevent_numeric_modifier(struct evlist *evlist)
{
	struct evsel *evsel;

	TEST_ASSERT_EVLIST("wrong number of entries", 1 == evlist->core.nr_entries, evlist);

	evlist__for_each_entry(evlist, evsel) {
		TEST_ASSERT_EVSEL("wrong exclude_user", evsel->core.attr.exclude_user, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_kernel", evsel->core.attr.exclude_kernel, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_hv", !evsel->core.attr.exclude_hv, evsel);
		TEST_ASSERT_EVSEL("wrong precise_ip", evsel->core.attr.precise_ip, evsel);
	}
	return test__checkevent_numeric(evlist);
}

static int test__checkevent_symbolic_name_modifier(struct evlist *evlist)
{
	struct evsel *evsel;

	TEST_ASSERT_EVLIST("wrong number of entries",
			   evlist->core.nr_entries == num_core_entries(evlist),
			   evlist);

	evlist__for_each_entry(evlist, evsel) {
		TEST_ASSERT_EVSEL("wrong exclude_user", evsel->core.attr.exclude_user, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_kernel", evsel->core.attr.exclude_kernel, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_hv", !evsel->core.attr.exclude_hv, evsel);
		TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);
	}
	return test__checkevent_symbolic_name(evlist);
}

static int test__checkevent_exclude_host_modifier(struct evlist *evlist)
{
	struct evsel *evsel;

	TEST_ASSERT_EVLIST("wrong number of entries",
			   evlist->core.nr_entries == num_core_entries(evlist),
			   evlist);

	evlist__for_each_entry(evlist, evsel) {
		TEST_ASSERT_EVSEL("wrong exclude guest", !evsel->core.attr.exclude_guest, evsel);
		TEST_ASSERT_EVSEL("wrong exclude host", evsel->core.attr.exclude_host, evsel);
	}
	return test__checkevent_symbolic_name(evlist);
}

static int test__checkevent_exclude_guest_modifier(struct evlist *evlist)
{
	struct evsel *evsel;

	TEST_ASSERT_EVLIST("wrong number of entries",
			   evlist->core.nr_entries == num_core_entries(evlist),
			   evlist);

	evlist__for_each_entry(evlist, evsel) {
		TEST_ASSERT_EVSEL("wrong exclude guest", evsel->core.attr.exclude_guest, evsel);
		TEST_ASSERT_EVSEL("wrong exclude host", !evsel->core.attr.exclude_host, evsel);
	}
	return test__checkevent_symbolic_name(evlist);
}

static int test__checkevent_symbolic_alias_modifier(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_EVLIST("wrong number of entries", 1 == evlist->core.nr_entries, evlist);
	TEST_ASSERT_EVSEL("wrong exclude_user", !evsel->core.attr.exclude_user, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_kernel", evsel->core.attr.exclude_kernel, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_hv", evsel->core.attr.exclude_hv, evsel);
	TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);

	return test__checkevent_symbolic_alias(evlist);
}

static int test__checkevent_genhw_modifier(struct evlist *evlist)
{
	struct evsel *evsel;

	TEST_ASSERT_EVLIST("wrong number of entries",
			   evlist->core.nr_entries == num_core_entries(evlist),
			   evlist);

	evlist__for_each_entry(evlist, evsel) {
		TEST_ASSERT_EVSEL("wrong exclude_user", evsel->core.attr.exclude_user, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_hv", evsel->core.attr.exclude_hv, evsel);
		TEST_ASSERT_EVSEL("wrong precise_ip", evsel->core.attr.precise_ip, evsel);
	}
	return test__checkevent_genhw(evlist);
}

static int test__checkevent_exclude_idle_modifier(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_EVLIST("wrong number of entries",
			   evlist->core.nr_entries == num_core_entries(evlist),
			   evlist);

	TEST_ASSERT_EVSEL("wrong exclude idle", evsel->core.attr.exclude_idle, evsel);
	TEST_ASSERT_EVSEL("wrong exclude guest", !evsel->core.attr.exclude_guest, evsel);
	TEST_ASSERT_EVSEL("wrong exclude host", !evsel->core.attr.exclude_host, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_user", !evsel->core.attr.exclude_user, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_hv", !evsel->core.attr.exclude_hv, evsel);
	TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);

	return test__checkevent_symbolic_name(evlist);
}

static int test__checkevent_exclude_idle_modifier_1(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_EVLIST("wrong number of entries",
			   evlist->core.nr_entries == num_core_entries(evlist),
			   evlist);

	TEST_ASSERT_EVSEL("wrong exclude idle", evsel->core.attr.exclude_idle, evsel);
	TEST_ASSERT_EVSEL("wrong exclude guest", !evsel->core.attr.exclude_guest, evsel);
	TEST_ASSERT_EVSEL("wrong exclude host", evsel->core.attr.exclude_host, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_user", evsel->core.attr.exclude_user, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_hv", evsel->core.attr.exclude_hv, evsel);
	TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);

	return test__checkevent_symbolic_name(evlist);
}

static int test__checkevent_breakpoint_modifier(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);


	TEST_ASSERT_EVSEL("wrong exclude_user", !evsel->core.attr.exclude_user, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_kernel", evsel->core.attr.exclude_kernel, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_hv", evsel->core.attr.exclude_hv, evsel);
	TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);
	TEST_ASSERT_EVSEL("wrong name", evsel__name_is(evsel, "mem:0:u"), evsel);

	return test__checkevent_breakpoint(evlist);
}

static int test__checkevent_breakpoint_x_modifier(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_EVSEL("wrong exclude_user", evsel->core.attr.exclude_user, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_hv", evsel->core.attr.exclude_hv, evsel);
	TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);
	TEST_ASSERT_EVSEL("wrong name", evsel__name_is(evsel, "mem:0:x:k"), evsel);

	return test__checkevent_breakpoint_x(evlist);
}

static int test__checkevent_breakpoint_r_modifier(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_EVSEL("wrong exclude_user", evsel->core.attr.exclude_user, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_kernel", evsel->core.attr.exclude_kernel, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_hv", !evsel->core.attr.exclude_hv, evsel);
	TEST_ASSERT_EVSEL("wrong precise_ip", evsel->core.attr.precise_ip, evsel);
	TEST_ASSERT_EVSEL("wrong name", evsel__name_is(evsel, "mem:0:r:hp"), evsel);

	return test__checkevent_breakpoint_r(evlist);
}

static int test__checkevent_breakpoint_w_modifier(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_EVSEL("wrong exclude_user", !evsel->core.attr.exclude_user, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_kernel", evsel->core.attr.exclude_kernel, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_hv", evsel->core.attr.exclude_hv, evsel);
	TEST_ASSERT_EVSEL("wrong precise_ip", evsel->core.attr.precise_ip, evsel);
	TEST_ASSERT_EVSEL("wrong name", evsel__name_is(evsel, "mem:0:w:up"), evsel);

	return test__checkevent_breakpoint_w(evlist);
}

static int test__checkevent_breakpoint_rw_modifier(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_EVSEL("wrong exclude_user", evsel->core.attr.exclude_user, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_hv", evsel->core.attr.exclude_hv, evsel);
	TEST_ASSERT_EVSEL("wrong precise_ip", evsel->core.attr.precise_ip, evsel);
	TEST_ASSERT_EVSEL("wrong name", evsel__name_is(evsel, "mem:0:rw:kp"), evsel);

	return test__checkevent_breakpoint_rw(evlist);
}

static int test__checkevent_breakpoint_modifier_name(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_EVSEL("wrong exclude_user", !evsel->core.attr.exclude_user, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_kernel", evsel->core.attr.exclude_kernel, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_hv", evsel->core.attr.exclude_hv, evsel);
	TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);
	TEST_ASSERT_EVSEL("wrong name", evsel__name_is(evsel, "breakpoint"), evsel);

	return test__checkevent_breakpoint(evlist);
}

static int test__checkevent_breakpoint_x_modifier_name(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_EVSEL("wrong exclude_user", evsel->core.attr.exclude_user, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_hv", evsel->core.attr.exclude_hv, evsel);
	TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);
	TEST_ASSERT_EVSEL("wrong name", evsel__name_is(evsel, "breakpoint"), evsel);

	return test__checkevent_breakpoint_x(evlist);
}

static int test__checkevent_breakpoint_r_modifier_name(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_EVSEL("wrong exclude_user", evsel->core.attr.exclude_user, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_kernel", evsel->core.attr.exclude_kernel, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_hv", !evsel->core.attr.exclude_hv, evsel);
	TEST_ASSERT_EVSEL("wrong precise_ip", evsel->core.attr.precise_ip, evsel);
	TEST_ASSERT_EVSEL("wrong name", evsel__name_is(evsel, "breakpoint"), evsel);

	return test__checkevent_breakpoint_r(evlist);
}

static int test__checkevent_breakpoint_w_modifier_name(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_EVSEL("wrong exclude_user", !evsel->core.attr.exclude_user, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_kernel", evsel->core.attr.exclude_kernel, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_hv", evsel->core.attr.exclude_hv, evsel);
	TEST_ASSERT_EVSEL("wrong precise_ip", evsel->core.attr.precise_ip, evsel);
	TEST_ASSERT_EVSEL("wrong name", evsel__name_is(evsel, "breakpoint"), evsel);

	return test__checkevent_breakpoint_w(evlist);
}

static int test__checkevent_breakpoint_rw_modifier_name(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_EVSEL("wrong exclude_user", evsel->core.attr.exclude_user, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_hv", evsel->core.attr.exclude_hv, evsel);
	TEST_ASSERT_EVSEL("wrong precise_ip", evsel->core.attr.precise_ip, evsel);
	TEST_ASSERT_EVSEL("wrong name", evsel__name_is(evsel, "breakpoint"), evsel);

	return test__checkevent_breakpoint_rw(evlist);
}

static int test__checkevent_breakpoint_2_events(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_EVSEL("wrong number of entries", 2 == evlist->core.nr_entries, evsel);

	TEST_ASSERT_EVSEL("wrong type", PERF_TYPE_BREAKPOINT == evsel->core.attr.type, evsel);
	TEST_ASSERT_EVSEL("wrong name", evsel__name_is(evsel, "breakpoint1"), evsel);

	evsel = evsel__next(evsel);

	TEST_ASSERT_EVSEL("wrong type", PERF_TYPE_BREAKPOINT == evsel->core.attr.type, evsel);
	TEST_ASSERT_EVSEL("wrong name", evsel__name_is(evsel, "breakpoint2"), evsel);

	return TEST_OK;
}

static int test__checkevent_pmu(struct evlist *evlist)
{

	struct evsel *evsel = evlist__first(evlist);
	struct perf_pmu *core_pmu = perf_pmus__find_core_pmu();

	TEST_ASSERT_EVSEL("wrong number of entries", 1 == evlist->core.nr_entries, evsel);
	TEST_ASSERT_EVSEL("wrong type", core_pmu->type == evsel->core.attr.type, evsel);
	TEST_ASSERT_EVSEL("wrong config",    test_hw_config(evsel, 10), evsel);
	TEST_ASSERT_EVSEL("wrong config1",    1 == evsel->core.attr.config1, evsel);
	TEST_ASSERT_EVSEL("wrong config2",    3 == evsel->core.attr.config2, evsel);
	TEST_ASSERT_EVSEL("wrong config3",    0 == evsel->core.attr.config3, evsel);
	/*
	 * The period value gets configured within evlist__config,
	 * while this test executes only parse events method.
	 */
	TEST_ASSERT_EVSEL("wrong period",     0 == evsel->core.attr.sample_period, evsel);

	return TEST_OK;
}

static int test__checkevent_list(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_EVSEL("wrong number of entries", 3 <= evlist->core.nr_entries, evsel);

	/* r1 */
	TEST_ASSERT_EVSEL("wrong type", PERF_TYPE_TRACEPOINT != evsel->core.attr.type, evsel);
	while (evsel->core.attr.type != PERF_TYPE_TRACEPOINT) {
		TEST_ASSERT_EVSEL("wrong config", 1 == evsel->core.attr.config, evsel);
		TEST_ASSERT_EVSEL("wrong config1", 0 == evsel->core.attr.config1, evsel);
		TEST_ASSERT_EVSEL("wrong config2", 0 == evsel->core.attr.config2, evsel);
		TEST_ASSERT_EVSEL("wrong config3", 0 == evsel->core.attr.config3, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_user", !evsel->core.attr.exclude_user, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_hv", !evsel->core.attr.exclude_hv, evsel);
		TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);
		evsel = evsel__next(evsel);
	}

	/* syscalls:sys_enter_openat:k */
	TEST_ASSERT_EVSEL("wrong type", PERF_TYPE_TRACEPOINT == evsel->core.attr.type, evsel);
	TEST_ASSERT_EVSEL("wrong sample_type", PERF_TP_SAMPLE_TYPE == evsel->core.attr.sample_type,
			  evsel);
	TEST_ASSERT_EVSEL("wrong sample_period", 1 == evsel->core.attr.sample_period, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_user", evsel->core.attr.exclude_user, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_hv", evsel->core.attr.exclude_hv, evsel);
	TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);

	/* 1:1:hp */
	evsel = evsel__next(evsel);
	TEST_ASSERT_EVSEL("wrong type", 1 == evsel->core.attr.type, evsel);
	TEST_ASSERT_EVSEL("wrong config", 1 == evsel->core.attr.config, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_user", evsel->core.attr.exclude_user, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_kernel", evsel->core.attr.exclude_kernel, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_hv", !evsel->core.attr.exclude_hv, evsel);
	TEST_ASSERT_EVSEL("wrong precise_ip", evsel->core.attr.precise_ip, evsel);

	return TEST_OK;
}

static int test__checkevent_pmu_name(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);
	struct perf_pmu *core_pmu = perf_pmus__find_core_pmu();
	char buf[256];

	/* default_core/config=1,name=krava/u */
	TEST_ASSERT_EVLIST("wrong number of entries", 2 == evlist->core.nr_entries, evlist);
	TEST_ASSERT_EVSEL("wrong type", core_pmu->type == evsel->core.attr.type, evsel);
	TEST_ASSERT_EVSEL("wrong config", 1 == evsel->core.attr.config, evsel);
	TEST_ASSERT_EVSEL("wrong name", evsel__name_is(evsel, "krava"), evsel);

	/* default_core/config=2/u" */
	evsel = evsel__next(evsel);
	TEST_ASSERT_EVSEL("wrong number of entries", 2 == evlist->core.nr_entries, evsel);
	TEST_ASSERT_EVSEL("wrong type", core_pmu->type == evsel->core.attr.type, evsel);
	TEST_ASSERT_EVSEL("wrong config", 2 == evsel->core.attr.config, evsel);
	snprintf(buf, sizeof(buf), "%s/config=2/u", core_pmu->name);
	TEST_ASSERT_EVSEL("wrong name", evsel__name_is(evsel, buf), evsel);

	return TEST_OK;
}

static int test__checkevent_pmu_partial_time_callgraph(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);
	struct perf_pmu *core_pmu = perf_pmus__find_core_pmu();

	/* default_core/config=1,call-graph=fp,time,period=100000/ */
	TEST_ASSERT_EVLIST("wrong number of entries", 2 == evlist->core.nr_entries, evlist);
	TEST_ASSERT_EVSEL("wrong type", core_pmu->type == evsel->core.attr.type, evsel);
	TEST_ASSERT_EVSEL("wrong config", 1 == evsel->core.attr.config, evsel);
	/*
	 * The period, time and callgraph value gets configured within evlist__config,
	 * while this test executes only parse events method.
	 */
	TEST_ASSERT_EVSEL("wrong period",     0 == evsel->core.attr.sample_period, evsel);
	TEST_ASSERT_EVSEL("wrong callgraph",  !evsel__has_callchain(evsel), evsel);
	TEST_ASSERT_EVSEL("wrong time",  !(PERF_SAMPLE_TIME & evsel->core.attr.sample_type), evsel);

	/* default_core/config=2,call-graph=no,time=0,period=2000/ */
	evsel = evsel__next(evsel);
	TEST_ASSERT_EVSEL("wrong type", core_pmu->type == evsel->core.attr.type, evsel);
	TEST_ASSERT_EVSEL("wrong config", 2 == evsel->core.attr.config, evsel);
	/*
	 * The period, time and callgraph value gets configured within evlist__config,
	 * while this test executes only parse events method.
	 */
	TEST_ASSERT_EVSEL("wrong period",     0 == evsel->core.attr.sample_period, evsel);
	TEST_ASSERT_EVSEL("wrong callgraph",  !evsel__has_callchain(evsel), evsel);
	TEST_ASSERT_EVSEL("wrong time",  !(PERF_SAMPLE_TIME & evsel->core.attr.sample_type), evsel);

	return TEST_OK;
}

static int test__checkevent_pmu_events(struct evlist *evlist)
{
	struct evsel *evsel;
	struct perf_pmu *core_pmu = perf_pmus__find_core_pmu();

	TEST_ASSERT_EVLIST("wrong number of entries", 1 <= evlist->core.nr_entries, evlist);

	evlist__for_each_entry(evlist, evsel) {
		TEST_ASSERT_EVSEL("wrong type",
				  core_pmu->type == evsel->core.attr.type ||
				  !strncmp(evsel__name(evsel), evsel->pmu->name,
					  strlen(evsel->pmu->name)),
				  evsel);
		TEST_ASSERT_EVSEL("wrong exclude_user", !evsel->core.attr.exclude_user, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_kernel", evsel->core.attr.exclude_kernel, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_hv", evsel->core.attr.exclude_hv, evsel);
		TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);
		TEST_ASSERT_EVSEL("wrong pinned", !evsel->core.attr.pinned, evsel);
		TEST_ASSERT_EVSEL("wrong exclusive", !evsel->core.attr.exclusive, evsel);
	}
	return TEST_OK;
}


static int test__checkevent_pmu_events_mix(struct evlist *evlist)
{
	struct evsel *evsel = NULL;

	/*
	 * The wild card event will be opened at least once, but it may be
	 * opened on each core PMU.
	 */
	TEST_ASSERT_EVLIST("wrong number of entries", evlist->core.nr_entries >= 2, evlist);
	for (int i = 0; i < evlist->core.nr_entries - 1; i++) {
		evsel = (i == 0 ? evlist__first(evlist) : evsel__next(evsel));
		/* pmu-event:u */
		TEST_ASSERT_EVSEL("wrong exclude_user", !evsel->core.attr.exclude_user, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_kernel", evsel->core.attr.exclude_kernel, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_hv", evsel->core.attr.exclude_hv, evsel);
		TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);
		TEST_ASSERT_EVSEL("wrong pinned", !evsel->core.attr.pinned, evsel);
		TEST_ASSERT_EVSEL("wrong exclusive", !evsel->core.attr.exclusive, evsel);
	}
	/* default_core/pmu-event/u*/
	evsel = evsel__next(evsel);
	TEST_ASSERT_EVSEL("wrong type", evsel__find_pmu(evsel)->is_core, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_user", !evsel->core.attr.exclude_user, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_kernel", evsel->core.attr.exclude_kernel, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_hv", evsel->core.attr.exclude_hv, evsel);
	TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);
	TEST_ASSERT_EVSEL("wrong pinned", !evsel->core.attr.pinned, evsel);
	TEST_ASSERT_EVSEL("wrong exclusive", !evsel->core.attr.pinned, evsel);

	return TEST_OK;
}

static int test__checkterms_simple(struct parse_events_terms *terms)
{
	struct parse_events_term *term;

	/* config=10 */
	term = list_entry(terms->terms.next, struct parse_events_term, list);
	TEST_ASSERT_VAL("wrong type term",
			term->type_term == PARSE_EVENTS__TERM_TYPE_CONFIG);
	TEST_ASSERT_VAL("wrong type val",
			term->type_val == PARSE_EVENTS__TERM_TYPE_NUM);
	TEST_ASSERT_VAL("wrong val", term->val.num == 10);
	TEST_ASSERT_VAL("wrong config", !strcmp(term->config, "config"));

	/* config1 */
	term = list_entry(term->list.next, struct parse_events_term, list);
	TEST_ASSERT_VAL("wrong type term",
			term->type_term == PARSE_EVENTS__TERM_TYPE_CONFIG1);
	TEST_ASSERT_VAL("wrong type val",
			term->type_val == PARSE_EVENTS__TERM_TYPE_NUM);
	TEST_ASSERT_VAL("wrong val", term->val.num == 1);
	TEST_ASSERT_VAL("wrong config", !strcmp(term->config, "config1"));

	/* config2=3 */
	term = list_entry(term->list.next, struct parse_events_term, list);
	TEST_ASSERT_VAL("wrong type term",
			term->type_term == PARSE_EVENTS__TERM_TYPE_CONFIG2);
	TEST_ASSERT_VAL("wrong type val",
			term->type_val == PARSE_EVENTS__TERM_TYPE_NUM);
	TEST_ASSERT_VAL("wrong val", term->val.num == 3);
	TEST_ASSERT_VAL("wrong config", !strcmp(term->config, "config2"));

	/* config3=4 */
	term = list_entry(term->list.next, struct parse_events_term, list);
	TEST_ASSERT_VAL("wrong type term",
			term->type_term == PARSE_EVENTS__TERM_TYPE_CONFIG3);
	TEST_ASSERT_VAL("wrong type val",
			term->type_val == PARSE_EVENTS__TERM_TYPE_NUM);
	TEST_ASSERT_VAL("wrong val", term->val.num == 4);
	TEST_ASSERT_VAL("wrong config", !strcmp(term->config, "config3"));

	/* umask=1*/
	term = list_entry(term->list.next, struct parse_events_term, list);
	TEST_ASSERT_VAL("wrong type term",
			term->type_term == PARSE_EVENTS__TERM_TYPE_USER);
	TEST_ASSERT_VAL("wrong type val",
			term->type_val == PARSE_EVENTS__TERM_TYPE_NUM);
	TEST_ASSERT_VAL("wrong val", term->val.num == 1);
	TEST_ASSERT_VAL("wrong config", !strcmp(term->config, "umask"));

	/*
	 * read
	 *
	 * The perf_pmu__test_parse_init injects 'read' term into
	 * perf_pmu_events_list, so 'read' is evaluated as read term
	 * and not as raw event with 'ead' hex value.
	 */
	term = list_entry(term->list.next, struct parse_events_term, list);
	TEST_ASSERT_VAL("wrong type term",
			term->type_term == PARSE_EVENTS__TERM_TYPE_RAW);
	TEST_ASSERT_VAL("wrong type val",
			term->type_val == PARSE_EVENTS__TERM_TYPE_STR);
	TEST_ASSERT_VAL("wrong val", !strcmp(term->val.str, "read"));
	TEST_ASSERT_VAL("wrong config", !strcmp(term->config, "raw"));

	/*
	 * r0xead
	 *
	 * To be still able to pass 'ead' value with 'r' syntax,
	 * we added support to parse 'r0xHEX' event.
	 */
	term = list_entry(term->list.next, struct parse_events_term, list);
	TEST_ASSERT_VAL("wrong type term",
			term->type_term == PARSE_EVENTS__TERM_TYPE_RAW);
	TEST_ASSERT_VAL("wrong type val",
			term->type_val == PARSE_EVENTS__TERM_TYPE_STR);
	TEST_ASSERT_VAL("wrong val", !strcmp(term->val.str, "r0xead"));
	TEST_ASSERT_VAL("wrong config", !strcmp(term->config, "raw"));
	return TEST_OK;
}

static int test__group1(struct evlist *evlist)
{
	struct evsel *evsel = NULL, *leader;

	TEST_ASSERT_EVLIST("wrong number of entries",
			   evlist->core.nr_entries == (num_core_entries(evlist) * 2),
			   evlist);
	TEST_ASSERT_EVLIST("wrong number of groups",
			   evlist__nr_groups(evlist) == num_core_entries(evlist),
			   evlist);

	for (int i = 0; i < num_core_entries(evlist); i++) {
		/* instructions:k */
		evsel = leader = (i == 0 ? evlist__first(evlist) : evsel__next(evsel));
		TEST_ASSERT_EVSEL("unexpected event",
				  evsel__match(evsel, HARDWARE, HW_INSTRUCTIONS),
				  evsel);
		TEST_ASSERT_EVSEL("wrong exclude_user", evsel->core.attr.exclude_user, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_hv", evsel->core.attr.exclude_hv, evsel);
		TEST_ASSERT_EVSEL("wrong exclude guest", !evsel->core.attr.exclude_guest, evsel);
		TEST_ASSERT_EVSEL("wrong exclude host", !evsel->core.attr.exclude_host, evsel);
		TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);
		TEST_ASSERT_EVSEL("wrong leader", evsel__is_group_leader(evsel), evsel);
		TEST_ASSERT_EVSEL("wrong core.nr_members", evsel->core.nr_members == 2, evsel);
		TEST_ASSERT_EVSEL("wrong group_idx", evsel__group_idx(evsel) == 0, evsel);
		TEST_ASSERT_EVSEL("wrong sample_read", !evsel->sample_read, evsel);

		/* cycles:upp */
		evsel = evsel__next(evsel);
		TEST_ASSERT_EVSEL("unexpected event", evsel__match(evsel, HARDWARE, HW_CPU_CYCLES),
				  evsel);
		TEST_ASSERT_EVSEL("wrong exclude_user", !evsel->core.attr.exclude_user, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_kernel", evsel->core.attr.exclude_kernel, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_hv", evsel->core.attr.exclude_hv, evsel);
		TEST_ASSERT_EVSEL("wrong exclude guest", !evsel->core.attr.exclude_guest, evsel);
		TEST_ASSERT_EVSEL("wrong exclude host", !evsel->core.attr.exclude_host, evsel);
		TEST_ASSERT_EVSEL("wrong precise_ip", evsel->core.attr.precise_ip == 2, evsel);
		TEST_ASSERT_EVSEL("wrong leader", evsel__has_leader(evsel, leader), evsel);
		TEST_ASSERT_EVSEL("wrong group_idx", evsel__group_idx(evsel) == 1, evsel);
		TEST_ASSERT_EVSEL("wrong sample_read", !evsel->sample_read, evsel);
	}
	return TEST_OK;
}

static int test__group2(struct evlist *evlist)
{
	struct evsel *evsel, *leader = NULL;

	TEST_ASSERT_EVLIST("wrong number of entries",
			   evlist->core.nr_entries == (2 * num_core_entries(evlist) + 1),
			   evlist);
	/*
	 * TODO: Currently the software event won't be grouped with the hardware
	 * event except for 1 PMU.
	 */
	TEST_ASSERT_EVLIST("wrong number of groups", 1 == evlist__nr_groups(evlist), evlist);

	evlist__for_each_entry(evlist, evsel) {
		if (evsel__match(evsel, SOFTWARE, SW_PAGE_FAULTS)) {
			/* faults + :ku modifier */
			leader = evsel;
			TEST_ASSERT_EVSEL("wrong exclude_user", !evsel->core.attr.exclude_user,
					  evsel);
			TEST_ASSERT_EVSEL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel,
					  evsel);
			TEST_ASSERT_EVSEL("wrong exclude_hv", evsel->core.attr.exclude_hv, evsel);
			TEST_ASSERT_EVSEL("wrong exclude guest", !evsel->core.attr.exclude_guest,
					  evsel);
			TEST_ASSERT_EVSEL("wrong exclude host", !evsel->core.attr.exclude_host,
					  evsel);
			TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);
			TEST_ASSERT_EVSEL("wrong leader", evsel__is_group_leader(evsel), evsel);
			TEST_ASSERT_EVSEL("wrong core.nr_members", evsel->core.nr_members == 2,
					  evsel);
			TEST_ASSERT_EVSEL("wrong group_idx", evsel__group_idx(evsel) == 0, evsel);
			TEST_ASSERT_EVSEL("wrong sample_read", !evsel->sample_read, evsel);
			continue;
		}
		if (evsel__match(evsel, HARDWARE, HW_BRANCH_INSTRUCTIONS)) {
			/* branches + :u modifier */
			TEST_ASSERT_EVSEL("wrong exclude_user", !evsel->core.attr.exclude_user,
					  evsel);
			TEST_ASSERT_EVSEL("wrong exclude_kernel", evsel->core.attr.exclude_kernel,
					  evsel);
			TEST_ASSERT_EVSEL("wrong exclude_hv", evsel->core.attr.exclude_hv, evsel);
			TEST_ASSERT_EVSEL("wrong exclude guest", !evsel->core.attr.exclude_guest,
					  evsel);
			TEST_ASSERT_EVSEL("wrong exclude host", !evsel->core.attr.exclude_host,
					  evsel);
			TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);
			if (evsel__has_leader(evsel, leader)) {
				TEST_ASSERT_EVSEL("wrong group_idx", evsel__group_idx(evsel) == 1,
						  evsel);
			}
			TEST_ASSERT_EVSEL("wrong sample_read", !evsel->sample_read, evsel);
			continue;
		}
		/* cycles:k */
		TEST_ASSERT_EVSEL("unexpected event", evsel__match(evsel, HARDWARE, HW_CPU_CYCLES),
				  evsel);
		TEST_ASSERT_EVSEL("wrong exclude_user", evsel->core.attr.exclude_user, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_hv", evsel->core.attr.exclude_hv, evsel);
		TEST_ASSERT_EVSEL("wrong exclude guest", !evsel->core.attr.exclude_guest, evsel);
		TEST_ASSERT_EVSEL("wrong exclude host", !evsel->core.attr.exclude_host, evsel);
		TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);
		TEST_ASSERT_EVSEL("wrong leader", evsel__is_group_leader(evsel), evsel);
		TEST_ASSERT_EVSEL("wrong sample_read", !evsel->sample_read, evsel);
	}
	return TEST_OK;
}

static int test__group3(struct evlist *evlist __maybe_unused)
{
	struct evsel *evsel, *group1_leader = NULL, *group2_leader = NULL;

	TEST_ASSERT_EVLIST("wrong number of entries",
			   evlist->core.nr_entries == (3 * perf_pmus__num_core_pmus() + 2),
			   evlist);
	/*
	 * Currently the software event won't be grouped with the hardware event
	 * except for 1 PMU. This means there are always just 2 groups
	 * regardless of the number of core PMUs.
	 */
	TEST_ASSERT_EVLIST("wrong number of groups", 2 == evlist__nr_groups(evlist), evlist);

	evlist__for_each_entry(evlist, evsel) {
		if (evsel->core.attr.type == PERF_TYPE_TRACEPOINT) {
			/* group1 syscalls:sys_enter_openat:H */
			group1_leader = evsel;
			TEST_ASSERT_EVSEL("wrong sample_type",
					  evsel->core.attr.sample_type == PERF_TP_SAMPLE_TYPE,
					  evsel);
			TEST_ASSERT_EVSEL("wrong sample_period",
					  1 == evsel->core.attr.sample_period,
					  evsel);
			TEST_ASSERT_EVSEL("wrong exclude_user", !evsel->core.attr.exclude_user,
					  evsel);
			TEST_ASSERT_EVSEL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel,
					  evsel);
			TEST_ASSERT_EVSEL("wrong exclude_hv", !evsel->core.attr.exclude_hv, evsel);
			TEST_ASSERT_EVSEL("wrong exclude guest", evsel->core.attr.exclude_guest,
					  evsel);
			TEST_ASSERT_EVSEL("wrong exclude host", !evsel->core.attr.exclude_host,
					  evsel);
			TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);
			TEST_ASSERT_EVSEL("wrong leader", evsel__is_group_leader(evsel), evsel);
			TEST_ASSERT_EVSEL("wrong group name", !strcmp(evsel->group_name, "group1"),
					  evsel);
			TEST_ASSERT_EVSEL("wrong core.nr_members", evsel->core.nr_members == 2,
					  evsel);
			TEST_ASSERT_EVSEL("wrong group_idx", evsel__group_idx(evsel) == 0, evsel);
			TEST_ASSERT_EVSEL("wrong sample_read", !evsel->sample_read, evsel);
			continue;
		}
		if (evsel__match(evsel, HARDWARE, HW_CPU_CYCLES)) {
			if (evsel->core.attr.exclude_user) {
				/* group1 cycles:kppp */
				TEST_ASSERT_EVSEL("wrong exclude_user",
						  evsel->core.attr.exclude_user, evsel);
				TEST_ASSERT_EVSEL("wrong exclude_kernel",
						  !evsel->core.attr.exclude_kernel, evsel);
				TEST_ASSERT_EVSEL("wrong exclude_hv", evsel->core.attr.exclude_hv,
						  evsel);
				TEST_ASSERT_EVSEL("wrong exclude guest",
						  !evsel->core.attr.exclude_guest, evsel);
				TEST_ASSERT_EVSEL("wrong exclude host",
						  !evsel->core.attr.exclude_host, evsel);
				TEST_ASSERT_EVSEL("wrong precise_ip",
						  evsel->core.attr.precise_ip == 3, evsel);
				if (evsel__has_leader(evsel, group1_leader)) {
					TEST_ASSERT_EVSEL("wrong group name", !evsel->group_name,
							  evsel);
					TEST_ASSERT_EVSEL("wrong group_idx",
							  evsel__group_idx(evsel) == 1,
							  evsel);
				}
				TEST_ASSERT_EVSEL("wrong sample_read", !evsel->sample_read, evsel);
			} else {
				/* group2 cycles + G modifier */
				group2_leader = evsel;
				TEST_ASSERT_EVSEL("wrong exclude_kernel",
						  !evsel->core.attr.exclude_kernel, evsel);
				TEST_ASSERT_EVSEL("wrong exclude_hv",
						  !evsel->core.attr.exclude_hv, evsel);
				TEST_ASSERT_EVSEL("wrong exclude guest",
						  !evsel->core.attr.exclude_guest, evsel);
				TEST_ASSERT_EVSEL("wrong exclude host",
						  evsel->core.attr.exclude_host, evsel);
				TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip,
						  evsel);
				TEST_ASSERT_EVSEL("wrong leader", evsel__is_group_leader(evsel),
						  evsel);
				if (evsel->core.nr_members == 2) {
					TEST_ASSERT_EVSEL("wrong group_idx",
							  evsel__group_idx(evsel) == 0,
							  evsel);
				}
				TEST_ASSERT_EVSEL("wrong sample_read", !evsel->sample_read, evsel);
			}
			continue;
		}
		if (evsel->core.attr.type == 1) {
			/* group2 1:3 + G modifier */
			TEST_ASSERT_EVSEL("wrong config", 3 == evsel->core.attr.config, evsel);
			TEST_ASSERT_EVSEL("wrong exclude_user", !evsel->core.attr.exclude_user,
					  evsel);
			TEST_ASSERT_EVSEL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel,
					  evsel);
			TEST_ASSERT_EVSEL("wrong exclude_hv", !evsel->core.attr.exclude_hv, evsel);
			TEST_ASSERT_EVSEL("wrong exclude guest", !evsel->core.attr.exclude_guest,
					  evsel);
			TEST_ASSERT_EVSEL("wrong exclude host", evsel->core.attr.exclude_host,
					  evsel);
			TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);
			if (evsel__has_leader(evsel, group2_leader)) {
				TEST_ASSERT_EVSEL("wrong group_idx", evsel__group_idx(evsel) == 1,
						  evsel);
			}
			TEST_ASSERT_EVSEL("wrong sample_read", !evsel->sample_read, evsel);
			continue;
		}
		/* instructions:u */
		TEST_ASSERT_EVSEL("unexpected event",
				  evsel__match(evsel, HARDWARE, HW_INSTRUCTIONS),
				  evsel);
		TEST_ASSERT_EVSEL("wrong exclude_user", !evsel->core.attr.exclude_user, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_kernel", evsel->core.attr.exclude_kernel, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_hv", evsel->core.attr.exclude_hv, evsel);
		TEST_ASSERT_EVSEL("wrong exclude guest", !evsel->core.attr.exclude_guest, evsel);
		TEST_ASSERT_EVSEL("wrong exclude host", !evsel->core.attr.exclude_host, evsel);
		TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);
		TEST_ASSERT_EVSEL("wrong leader", evsel__is_group_leader(evsel), evsel);
		TEST_ASSERT_EVSEL("wrong sample_read", !evsel->sample_read, evsel);
	}
	return TEST_OK;
}

static int test__group4(struct evlist *evlist __maybe_unused)
{
	struct evsel *evsel = NULL, *leader;

	TEST_ASSERT_EVLIST("wrong number of entries",
			   evlist->core.nr_entries == (num_core_entries(evlist) * 2),
			   evlist);
	TEST_ASSERT_EVLIST("wrong number of groups",
			   num_core_entries(evlist) == evlist__nr_groups(evlist),
			   evlist);

	for (int i = 0; i < num_core_entries(evlist); i++) {
		/* cycles:u + p */
		evsel = leader = (i == 0 ? evlist__first(evlist) : evsel__next(evsel));
		TEST_ASSERT_EVSEL("unexpected event",
				  evsel__match(evsel, HARDWARE, HW_CPU_CYCLES),
				  evsel);
		TEST_ASSERT_EVSEL("wrong exclude_user", !evsel->core.attr.exclude_user, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_kernel", evsel->core.attr.exclude_kernel, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_hv", evsel->core.attr.exclude_hv, evsel);
		TEST_ASSERT_EVSEL("wrong exclude guest", !evsel->core.attr.exclude_guest, evsel);
		TEST_ASSERT_EVSEL("wrong exclude host", !evsel->core.attr.exclude_host, evsel);
		TEST_ASSERT_EVSEL("wrong precise_ip", evsel->core.attr.precise_ip == 1, evsel);
		TEST_ASSERT_EVSEL("wrong group name", !evsel->group_name, evsel);
		TEST_ASSERT_EVSEL("wrong leader", evsel__is_group_leader(evsel), evsel);
		TEST_ASSERT_EVSEL("wrong core.nr_members", evsel->core.nr_members == 2, evsel);
		TEST_ASSERT_EVSEL("wrong group_idx", evsel__group_idx(evsel) == 0, evsel);
		TEST_ASSERT_EVSEL("wrong sample_read", !evsel->sample_read, evsel);

		/* instructions:kp + p */
		evsel = evsel__next(evsel);
		TEST_ASSERT_EVSEL("unexpected event",
				  evsel__match(evsel, HARDWARE, HW_INSTRUCTIONS),
				  evsel);
		TEST_ASSERT_EVSEL("wrong exclude_user", evsel->core.attr.exclude_user, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_hv", evsel->core.attr.exclude_hv, evsel);
		TEST_ASSERT_EVSEL("wrong exclude guest", !evsel->core.attr.exclude_guest, evsel);
		TEST_ASSERT_EVSEL("wrong exclude host", !evsel->core.attr.exclude_host, evsel);
		TEST_ASSERT_EVSEL("wrong precise_ip", evsel->core.attr.precise_ip == 2, evsel);
		TEST_ASSERT_EVSEL("wrong leader", evsel__has_leader(evsel, leader), evsel);
		TEST_ASSERT_EVSEL("wrong group_idx", evsel__group_idx(evsel) == 1, evsel);
		TEST_ASSERT_EVSEL("wrong sample_read", !evsel->sample_read, evsel);
	}
	return TEST_OK;
}

static int test__group5(struct evlist *evlist __maybe_unused)
{
	struct evsel *evsel = NULL, *leader;

	TEST_ASSERT_EVLIST("wrong number of entries",
			   evlist->core.nr_entries == (5 * num_core_entries(evlist)),
			   evlist);
	TEST_ASSERT_EVLIST("wrong number of groups",
			   evlist__nr_groups(evlist) == (2 * num_core_entries(evlist)),
			   evlist);

	for (int i = 0; i < num_core_entries(evlist); i++) {
		/* cycles + G */
		evsel = leader = (i == 0 ? evlist__first(evlist) : evsel__next(evsel));
		TEST_ASSERT_EVSEL("unexpected event",
				  evsel__match(evsel, HARDWARE, HW_CPU_CYCLES),
				  evsel);
		TEST_ASSERT_EVSEL("wrong exclude_user", !evsel->core.attr.exclude_user, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_hv", !evsel->core.attr.exclude_hv, evsel);
		TEST_ASSERT_EVSEL("wrong exclude guest", !evsel->core.attr.exclude_guest, evsel);
		TEST_ASSERT_EVSEL("wrong exclude host", evsel->core.attr.exclude_host, evsel);
		TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);
		TEST_ASSERT_EVSEL("wrong group name", !evsel->group_name, evsel);
		TEST_ASSERT_EVSEL("wrong leader", evsel__is_group_leader(evsel), evsel);
		TEST_ASSERT_EVSEL("wrong core.nr_members", evsel->core.nr_members == 2, evsel);
		TEST_ASSERT_EVSEL("wrong group_idx", evsel__group_idx(evsel) == 0, evsel);
		TEST_ASSERT_EVSEL("wrong sample_read", !evsel->sample_read, evsel);

		/* instructions + G */
		evsel = evsel__next(evsel);
		TEST_ASSERT_EVSEL("unexpected event",
				  evsel__match(evsel, HARDWARE, HW_INSTRUCTIONS),
				  evsel);
		TEST_ASSERT_EVSEL("wrong exclude_user", !evsel->core.attr.exclude_user, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_hv", !evsel->core.attr.exclude_hv, evsel);
		TEST_ASSERT_EVSEL("wrong exclude guest", !evsel->core.attr.exclude_guest, evsel);
		TEST_ASSERT_EVSEL("wrong exclude host", evsel->core.attr.exclude_host, evsel);
		TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);
		TEST_ASSERT_EVSEL("wrong leader", evsel__has_leader(evsel, leader), evsel);
		TEST_ASSERT_EVSEL("wrong group_idx", evsel__group_idx(evsel) == 1, evsel);
		TEST_ASSERT_EVSEL("wrong sample_read", !evsel->sample_read, evsel);
	}
	for (int i = 0; i < num_core_entries(evlist); i++) {
		/* cycles:G */
		evsel = leader = evsel__next(evsel);
		TEST_ASSERT_EVSEL("unexpected event",
				  evsel__match(evsel, HARDWARE, HW_CPU_CYCLES),
				  evsel);
		TEST_ASSERT_EVSEL("wrong exclude_user", !evsel->core.attr.exclude_user, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_hv", !evsel->core.attr.exclude_hv, evsel);
		TEST_ASSERT_EVSEL("wrong exclude guest", !evsel->core.attr.exclude_guest, evsel);
		TEST_ASSERT_EVSEL("wrong exclude host", evsel->core.attr.exclude_host, evsel);
		TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);
		TEST_ASSERT_EVSEL("wrong group name", !evsel->group_name, evsel);
		TEST_ASSERT_EVSEL("wrong leader", evsel__is_group_leader(evsel), evsel);
		TEST_ASSERT_EVSEL("wrong core.nr_members", evsel->core.nr_members == 2, evsel);
		TEST_ASSERT_EVSEL("wrong group_idx", evsel__group_idx(evsel) == 0, evsel);
		TEST_ASSERT_EVSEL("wrong sample_read", !evsel->sample_read, evsel);

		/* instructions:G */
		evsel = evsel__next(evsel);
		TEST_ASSERT_EVSEL("unexpected event",
				  evsel__match(evsel, HARDWARE, HW_INSTRUCTIONS),
				evsel);
		TEST_ASSERT_EVSEL("wrong exclude_user", !evsel->core.attr.exclude_user, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_hv", !evsel->core.attr.exclude_hv, evsel);
		TEST_ASSERT_EVSEL("wrong exclude guest", !evsel->core.attr.exclude_guest, evsel);
		TEST_ASSERT_EVSEL("wrong exclude host", evsel->core.attr.exclude_host, evsel);
		TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);
		TEST_ASSERT_EVSEL("wrong leader", evsel__has_leader(evsel, leader), evsel);
		TEST_ASSERT_EVSEL("wrong group_idx", evsel__group_idx(evsel) == 1, evsel);
	}
	for (int i = 0; i < num_core_entries(evlist); i++) {
		/* cycles */
		evsel = evsel__next(evsel);
		TEST_ASSERT_EVSEL("unexpected event",
				  evsel__match(evsel, HARDWARE, HW_CPU_CYCLES),
				  evsel);
		TEST_ASSERT_EVSEL("wrong exclude_user", !evsel->core.attr.exclude_user, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_hv", !evsel->core.attr.exclude_hv, evsel);
		TEST_ASSERT_EVSEL("wrong exclude guest", !evsel->core.attr.exclude_guest, evsel);
		TEST_ASSERT_EVSEL("wrong exclude host", !evsel->core.attr.exclude_host, evsel);
		TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);
		TEST_ASSERT_EVSEL("wrong leader", evsel__is_group_leader(evsel), evsel);
	}
	return TEST_OK;
}

static int test__group_gh1(struct evlist *evlist)
{
	struct evsel *evsel = NULL, *leader;

	TEST_ASSERT_EVLIST("wrong number of entries",
			   evlist->core.nr_entries == (2 * num_core_entries(evlist)),
			   evlist);
	TEST_ASSERT_EVLIST("wrong number of groups",
			   evlist__nr_groups(evlist) == num_core_entries(evlist),
			   evlist);

	for (int i = 0; i < num_core_entries(evlist); i++) {
		/* cycles + :H group modifier */
		evsel = leader = (i == 0 ? evlist__first(evlist) : evsel__next(evsel));
		TEST_ASSERT_EVSEL("unexpected event",
				  evsel__match(evsel, HARDWARE, HW_CPU_CYCLES),
				  evsel);
		TEST_ASSERT_EVSEL("wrong exclude_user", !evsel->core.attr.exclude_user, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_hv", !evsel->core.attr.exclude_hv, evsel);
		TEST_ASSERT_EVSEL("wrong exclude guest", evsel->core.attr.exclude_guest, evsel);
		TEST_ASSERT_EVSEL("wrong exclude host", !evsel->core.attr.exclude_host, evsel);
		TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);
		TEST_ASSERT_EVSEL("wrong group name", !evsel->group_name, evsel);
		TEST_ASSERT_EVSEL("wrong leader", evsel__is_group_leader(evsel), evsel);
		TEST_ASSERT_EVSEL("wrong core.nr_members", evsel->core.nr_members == 2, evsel);
		TEST_ASSERT_EVSEL("wrong group_idx", evsel__group_idx(evsel) == 0, evsel);

		/* cache-misses:G + :H group modifier */
		evsel = evsel__next(evsel);
		TEST_ASSERT_EVSEL("unexpected event",
				  evsel__match(evsel, HARDWARE, HW_CACHE_MISSES),
				  evsel);
		TEST_ASSERT_EVSEL("wrong exclude_user", !evsel->core.attr.exclude_user, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_hv", !evsel->core.attr.exclude_hv, evsel);
		TEST_ASSERT_EVSEL("wrong exclude guest", !evsel->core.attr.exclude_guest, evsel);
		TEST_ASSERT_EVSEL("wrong exclude host", !evsel->core.attr.exclude_host, evsel);
		TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);
		TEST_ASSERT_EVSEL("wrong leader", evsel__has_leader(evsel, leader), evsel);
		TEST_ASSERT_EVSEL("wrong group_idx", evsel__group_idx(evsel) == 1, evsel);
	}
	return TEST_OK;
}

static int test__group_gh2(struct evlist *evlist)
{
	struct evsel *evsel = NULL, *leader;

	TEST_ASSERT_EVLIST("wrong number of entries",
			   evlist->core.nr_entries == (2 * num_core_entries(evlist)),
			   evlist);
	TEST_ASSERT_EVLIST("wrong number of groups",
			   evlist__nr_groups(evlist) == num_core_entries(evlist),
			   evlist);

	for (int i = 0; i < num_core_entries(evlist); i++) {
		/* cycles + :G group modifier */
		evsel = leader = (i == 0 ? evlist__first(evlist) : evsel__next(evsel));
		TEST_ASSERT_EVSEL("unexpected event",
				  evsel__match(evsel, HARDWARE, HW_CPU_CYCLES),
				  evsel);
		TEST_ASSERT_EVSEL("wrong exclude_user", !evsel->core.attr.exclude_user, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_hv", !evsel->core.attr.exclude_hv, evsel);
		TEST_ASSERT_EVSEL("wrong exclude guest", !evsel->core.attr.exclude_guest, evsel);
		TEST_ASSERT_EVSEL("wrong exclude host", evsel->core.attr.exclude_host, evsel);
		TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);
		TEST_ASSERT_EVSEL("wrong group name", !evsel->group_name, evsel);
		TEST_ASSERT_EVSEL("wrong leader", evsel__is_group_leader(evsel), evsel);
		TEST_ASSERT_EVSEL("wrong core.nr_members", evsel->core.nr_members == 2, evsel);
		TEST_ASSERT_EVSEL("wrong group_idx", evsel__group_idx(evsel) == 0, evsel);

		/* cache-misses:H + :G group modifier */
		evsel = evsel__next(evsel);
		TEST_ASSERT_EVSEL("unexpected event",
				  evsel__match(evsel, HARDWARE, HW_CACHE_MISSES),
				  evsel);
		TEST_ASSERT_EVSEL("wrong exclude_user", !evsel->core.attr.exclude_user, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_hv", !evsel->core.attr.exclude_hv, evsel);
		TEST_ASSERT_EVSEL("wrong exclude guest", !evsel->core.attr.exclude_guest, evsel);
		TEST_ASSERT_EVSEL("wrong exclude host", !evsel->core.attr.exclude_host, evsel);
		TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);
		TEST_ASSERT_EVSEL("wrong leader", evsel__has_leader(evsel, leader), evsel);
		TEST_ASSERT_EVSEL("wrong group_idx", evsel__group_idx(evsel) == 1, evsel);
	}
	return TEST_OK;
}

static int test__group_gh3(struct evlist *evlist)
{
	struct evsel *evsel = NULL, *leader;

	TEST_ASSERT_EVLIST("wrong number of entries",
			   evlist->core.nr_entries == (2 * num_core_entries(evlist)),
			   evlist);
	TEST_ASSERT_EVLIST("wrong number of groups",
			   evlist__nr_groups(evlist) == num_core_entries(evlist),
			   evlist);

	for (int i = 0; i < num_core_entries(evlist); i++) {
		/* cycles:G + :u group modifier */
		evsel = leader = (i == 0 ? evlist__first(evlist) : evsel__next(evsel));
		TEST_ASSERT_EVSEL("unexpected event",
				  evsel__match(evsel, HARDWARE, HW_CPU_CYCLES),
				  evsel);
		TEST_ASSERT_EVSEL("wrong exclude_user", !evsel->core.attr.exclude_user, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_kernel", evsel->core.attr.exclude_kernel, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_hv", evsel->core.attr.exclude_hv, evsel);
		TEST_ASSERT_EVSEL("wrong exclude guest", !evsel->core.attr.exclude_guest, evsel);
		TEST_ASSERT_EVSEL("wrong exclude host", evsel->core.attr.exclude_host, evsel);
		TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);
		TEST_ASSERT_EVSEL("wrong group name", !evsel->group_name, evsel);
		TEST_ASSERT_EVSEL("wrong leader", evsel__is_group_leader(evsel), evsel);
		TEST_ASSERT_EVSEL("wrong core.nr_members", evsel->core.nr_members == 2, evsel);
		TEST_ASSERT_EVSEL("wrong group_idx", evsel__group_idx(evsel) == 0, evsel);

		/* cache-misses:H + :u group modifier */
		evsel = evsel__next(evsel);
		TEST_ASSERT_EVSEL("unexpected event",
				  evsel__match(evsel, HARDWARE, HW_CACHE_MISSES),
				  evsel);
		TEST_ASSERT_EVSEL("wrong exclude_user", !evsel->core.attr.exclude_user, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_kernel", evsel->core.attr.exclude_kernel, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_hv", evsel->core.attr.exclude_hv, evsel);
		TEST_ASSERT_EVSEL("wrong exclude guest", evsel->core.attr.exclude_guest, evsel);
		TEST_ASSERT_EVSEL("wrong exclude host", !evsel->core.attr.exclude_host, evsel);
		TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);
		TEST_ASSERT_EVSEL("wrong leader", evsel__has_leader(evsel, leader), evsel);
		TEST_ASSERT_EVSEL("wrong group_idx", evsel__group_idx(evsel) == 1, evsel);
	}
	return TEST_OK;
}

static int test__group_gh4(struct evlist *evlist)
{
	struct evsel *evsel = NULL, *leader;

	TEST_ASSERT_EVLIST("wrong number of entries",
			   evlist->core.nr_entries == (2 * num_core_entries(evlist)),
			   evlist);
	TEST_ASSERT_EVLIST("wrong number of groups",
			   evlist__nr_groups(evlist) == num_core_entries(evlist),
			   evlist);

	for (int i = 0; i < num_core_entries(evlist); i++) {
		/* cycles:G + :uG group modifier */
		evsel = leader = (i == 0 ? evlist__first(evlist) : evsel__next(evsel));
		TEST_ASSERT_EVSEL("unexpected event",
				  evsel__match(evsel, HARDWARE, HW_CPU_CYCLES),
				  evsel);
		TEST_ASSERT_EVSEL("wrong exclude_user", !evsel->core.attr.exclude_user, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_kernel", evsel->core.attr.exclude_kernel, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_hv", evsel->core.attr.exclude_hv, evsel);
		TEST_ASSERT_EVSEL("wrong exclude guest", !evsel->core.attr.exclude_guest, evsel);
		TEST_ASSERT_EVSEL("wrong exclude host", evsel->core.attr.exclude_host, evsel);
		TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);
		TEST_ASSERT_EVSEL("wrong group name", !evsel->group_name, evsel);
		TEST_ASSERT_EVSEL("wrong leader", evsel__is_group_leader(evsel), evsel);
		TEST_ASSERT_EVSEL("wrong core.nr_members", evsel->core.nr_members == 2, evsel);
		TEST_ASSERT_EVSEL("wrong group_idx", evsel__group_idx(evsel) == 0, evsel);

		/* cache-misses:H + :uG group modifier */
		evsel = evsel__next(evsel);
		TEST_ASSERT_EVSEL("unexpected event",
				  evsel__match(evsel, HARDWARE, HW_CACHE_MISSES),
				  evsel);
		TEST_ASSERT_EVSEL("wrong exclude_user", !evsel->core.attr.exclude_user, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_kernel", evsel->core.attr.exclude_kernel, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_hv", evsel->core.attr.exclude_hv, evsel);
		TEST_ASSERT_EVSEL("wrong exclude guest", !evsel->core.attr.exclude_guest, evsel);
		TEST_ASSERT_EVSEL("wrong exclude host", !evsel->core.attr.exclude_host, evsel);
		TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);
		TEST_ASSERT_EVSEL("wrong leader", evsel__has_leader(evsel, leader), evsel);
		TEST_ASSERT_EVSEL("wrong group_idx", evsel__group_idx(evsel) == 1, evsel);
	}
	return TEST_OK;
}

static int test__leader_sample1(struct evlist *evlist)
{
	struct evsel *evsel = NULL, *leader;

	TEST_ASSERT_EVLIST("wrong number of entries",
			   evlist->core.nr_entries == (3 * num_core_entries(evlist)),
			   evlist);

	for (int i = 0; i < num_core_entries(evlist); i++) {
		/* cycles - sampling group leader */
		evsel = leader = (i == 0 ? evlist__first(evlist) : evsel__next(evsel));
		TEST_ASSERT_EVSEL("unexpected event",
				  evsel__match(evsel, HARDWARE, HW_CPU_CYCLES),
				  evsel);
		TEST_ASSERT_EVSEL("wrong exclude_user", !evsel->core.attr.exclude_user, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_hv", !evsel->core.attr.exclude_hv, evsel);
		TEST_ASSERT_EVSEL("wrong exclude guest", !evsel->core.attr.exclude_guest, evsel);
		TEST_ASSERT_EVSEL("wrong exclude host", !evsel->core.attr.exclude_host, evsel);
		TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);
		TEST_ASSERT_EVSEL("wrong group name", !evsel->group_name, evsel);
		TEST_ASSERT_EVSEL("wrong leader", evsel__has_leader(evsel, leader), evsel);
		TEST_ASSERT_EVSEL("wrong sample_read", evsel->sample_read, evsel);

		/* cache-misses - not sampling */
		evsel = evsel__next(evsel);
		TEST_ASSERT_EVSEL("unexpected event",
				  evsel__match(evsel, HARDWARE, HW_CACHE_MISSES),
				  evsel);
		TEST_ASSERT_EVSEL("wrong exclude_user", !evsel->core.attr.exclude_user, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_hv", !evsel->core.attr.exclude_hv, evsel);
		TEST_ASSERT_EVSEL("wrong exclude guest", !evsel->core.attr.exclude_guest, evsel);
		TEST_ASSERT_EVSEL("wrong exclude host", !evsel->core.attr.exclude_host, evsel);
		TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);
		TEST_ASSERT_EVSEL("wrong leader", evsel__has_leader(evsel, leader), evsel);
		TEST_ASSERT_EVSEL("wrong sample_read", evsel->sample_read, evsel);

		/* branch-misses - not sampling */
		evsel = evsel__next(evsel);
		TEST_ASSERT_EVSEL("unexpected event",
				  evsel__match(evsel, HARDWARE, HW_BRANCH_MISSES),
				  evsel);
		TEST_ASSERT_EVSEL("wrong exclude_user", !evsel->core.attr.exclude_user, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_hv", !evsel->core.attr.exclude_hv, evsel);
		TEST_ASSERT_EVSEL("wrong exclude guest", !evsel->core.attr.exclude_guest, evsel);
		TEST_ASSERT_EVSEL("wrong exclude host", !evsel->core.attr.exclude_host, evsel);
		TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);
		TEST_ASSERT_EVSEL("wrong group name", !evsel->group_name, evsel);
		TEST_ASSERT_EVSEL("wrong leader", evsel__has_leader(evsel, leader), evsel);
		TEST_ASSERT_EVSEL("wrong sample_read", evsel->sample_read, evsel);
	}
	return TEST_OK;
}

static int test__leader_sample2(struct evlist *evlist __maybe_unused)
{
	struct evsel *evsel = NULL, *leader;

	TEST_ASSERT_EVLIST("wrong number of entries",
			   evlist->core.nr_entries == (2 * num_core_entries(evlist)),
			   evlist);

	for (int i = 0; i < num_core_entries(evlist); i++) {
		/* instructions - sampling group leader */
		evsel = leader = (i == 0 ? evlist__first(evlist) : evsel__next(evsel));
		TEST_ASSERT_EVSEL("unexpected event",
				  evsel__match(evsel, HARDWARE, HW_INSTRUCTIONS),
				  evsel);
		TEST_ASSERT_EVSEL("wrong exclude_user", !evsel->core.attr.exclude_user, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_kernel", evsel->core.attr.exclude_kernel, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_hv", evsel->core.attr.exclude_hv, evsel);
		TEST_ASSERT_EVSEL("wrong exclude guest", !evsel->core.attr.exclude_guest, evsel);
		TEST_ASSERT_EVSEL("wrong exclude host", !evsel->core.attr.exclude_host, evsel);
		TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);
		TEST_ASSERT_EVSEL("wrong group name", !evsel->group_name, evsel);
		TEST_ASSERT_EVSEL("wrong leader", evsel__has_leader(evsel, leader), evsel);
		TEST_ASSERT_EVSEL("wrong sample_read", evsel->sample_read, evsel);

		/* branch-misses - not sampling */
		evsel = evsel__next(evsel);
		TEST_ASSERT_EVSEL("unexpected event",
				  evsel__match(evsel, HARDWARE, HW_BRANCH_MISSES),
				  evsel);
		TEST_ASSERT_EVSEL("wrong exclude_user", !evsel->core.attr.exclude_user, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_kernel", evsel->core.attr.exclude_kernel, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_hv", evsel->core.attr.exclude_hv, evsel);
		TEST_ASSERT_EVSEL("wrong exclude guest", !evsel->core.attr.exclude_guest, evsel);
		TEST_ASSERT_EVSEL("wrong exclude host", !evsel->core.attr.exclude_host, evsel);
		TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);
		TEST_ASSERT_EVSEL("wrong group name", !evsel->group_name, evsel);
		TEST_ASSERT_EVSEL("wrong leader", evsel__has_leader(evsel, leader), evsel);
		TEST_ASSERT_EVSEL("wrong sample_read", evsel->sample_read, evsel);
	}
	return TEST_OK;
}

static int test__checkevent_pinned_modifier(struct evlist *evlist)
{
	struct evsel *evsel = NULL;

	TEST_ASSERT_EVLIST("wrong number of entries",
			   evlist->core.nr_entries == num_core_entries(evlist),
			   evlist);

	for (int i = 0; i < num_core_entries(evlist); i++) {
		evsel = (i == 0 ? evlist__first(evlist) : evsel__next(evsel));
		TEST_ASSERT_EVSEL("wrong exclude_user", !evsel->core.attr.exclude_user, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_kernel", evsel->core.attr.exclude_kernel, evsel);
		TEST_ASSERT_EVSEL("wrong exclude_hv", evsel->core.attr.exclude_hv, evsel);
		TEST_ASSERT_EVSEL("wrong precise_ip", evsel->core.attr.precise_ip, evsel);
		TEST_ASSERT_EVSEL("wrong pinned", evsel->core.attr.pinned, evsel);
	}
	return test__checkevent_symbolic_name(evlist);
}

static int test__pinned_group(struct evlist *evlist)
{
	struct evsel *evsel = NULL, *leader;

	TEST_ASSERT_EVLIST("wrong number of entries",
			   evlist->core.nr_entries == (3 * num_core_entries(evlist)),
			   evlist);

	for (int i = 0; i < num_core_entries(evlist); i++) {
		/* cycles - group leader */
		evsel = leader = (i == 0 ? evlist__first(evlist) : evsel__next(evsel));
		TEST_ASSERT_EVSEL("unexpected event",
				  evsel__match(evsel, HARDWARE, HW_CPU_CYCLES),
				evsel);
		TEST_ASSERT_EVSEL("wrong group name", !evsel->group_name, evsel);
		TEST_ASSERT_EVSEL("wrong leader", evsel__has_leader(evsel, leader), evsel);
		/* TODO: The group modifier is not copied to the split group leader. */
		if (perf_pmus__num_core_pmus() == 1)
			TEST_ASSERT_EVSEL("wrong pinned", evsel->core.attr.pinned, evsel);

		/* cache-misses - can not be pinned, but will go on with the leader */
		evsel = evsel__next(evsel);
		TEST_ASSERT_EVSEL("unexpected event",
				  evsel__match(evsel, HARDWARE, HW_CACHE_MISSES),
				  evsel);
		TEST_ASSERT_EVSEL("wrong pinned", !evsel->core.attr.pinned, evsel);

		/* branch-misses - ditto */
		evsel = evsel__next(evsel);
		TEST_ASSERT_EVSEL("unexpected event",
				  evsel__match(evsel, HARDWARE, HW_BRANCH_MISSES),
				  evsel);
		TEST_ASSERT_EVSEL("wrong pinned", !evsel->core.attr.pinned, evsel);
	}
	return TEST_OK;
}

static int test__checkevent_exclusive_modifier(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_EVLIST("wrong number of entries",
			   evlist->core.nr_entries == num_core_entries(evlist),
			   evlist);
	TEST_ASSERT_EVSEL("wrong exclude_user", !evsel->core.attr.exclude_user, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_kernel", evsel->core.attr.exclude_kernel, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_hv", evsel->core.attr.exclude_hv, evsel);
	TEST_ASSERT_EVSEL("wrong precise_ip", evsel->core.attr.precise_ip, evsel);
	TEST_ASSERT_EVSEL("wrong exclusive", evsel->core.attr.exclusive, evsel);

	return test__checkevent_symbolic_name(evlist);
}

static int test__exclusive_group(struct evlist *evlist)
{
	struct evsel *evsel = NULL, *leader;

	TEST_ASSERT_EVLIST("wrong number of entries",
			   evlist->core.nr_entries == 3 * num_core_entries(evlist),
			   evlist);

	for (int i = 0; i < num_core_entries(evlist); i++) {
		/* cycles - group leader */
		evsel = leader = (i == 0 ? evlist__first(evlist) : evsel__next(evsel));
		TEST_ASSERT_EVSEL("unexpected event",
				  evsel__match(evsel, HARDWARE, HW_CPU_CYCLES),
				  evsel);
		TEST_ASSERT_EVSEL("wrong group name", !evsel->group_name, evsel);
		TEST_ASSERT_EVSEL("wrong leader", evsel__has_leader(evsel, leader), evsel);
		/* TODO: The group modifier is not copied to the split group leader. */
		if (perf_pmus__num_core_pmus() == 1)
			TEST_ASSERT_EVSEL("wrong exclusive", evsel->core.attr.exclusive, evsel);

		/* cache-misses - can not be pinned, but will go on with the leader */
		evsel = evsel__next(evsel);
		TEST_ASSERT_EVSEL("unexpected event",
				  evsel__match(evsel, HARDWARE, HW_CACHE_MISSES),
				  evsel);
		TEST_ASSERT_EVSEL("wrong exclusive", !evsel->core.attr.exclusive, evsel);

		/* branch-misses - ditto */
		evsel = evsel__next(evsel);
		TEST_ASSERT_EVSEL("unexpected event",
				  evsel__match(evsel, HARDWARE, HW_BRANCH_MISSES),
				  evsel);
		TEST_ASSERT_EVSEL("wrong exclusive", !evsel->core.attr.exclusive, evsel);
	}
	return TEST_OK;
}
static int test__checkevent_breakpoint_len(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_EVLIST("wrong number of entries", 1 == evlist->core.nr_entries, evlist);
	TEST_ASSERT_EVSEL("wrong type", PERF_TYPE_BREAKPOINT == evsel->core.attr.type, evsel);
	TEST_ASSERT_EVSEL("wrong config", 0 == evsel->core.attr.config, evsel);
	TEST_ASSERT_EVSEL("wrong bp_type",
			  (HW_BREAKPOINT_R | HW_BREAKPOINT_W) == evsel->core.attr.bp_type,
			  evsel);
	TEST_ASSERT_EVSEL("wrong bp_len", HW_BREAKPOINT_LEN_1 == evsel->core.attr.bp_len, evsel);

	return TEST_OK;
}

static int test__checkevent_breakpoint_len_w(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_EVLIST("wrong number of entries", 1 == evlist->core.nr_entries, evlist);
	TEST_ASSERT_EVSEL("wrong type", PERF_TYPE_BREAKPOINT == evsel->core.attr.type, evsel);
	TEST_ASSERT_EVSEL("wrong config", 0 == evsel->core.attr.config, evsel);
	TEST_ASSERT_EVSEL("wrong bp_type", HW_BREAKPOINT_W == evsel->core.attr.bp_type, evsel);
	TEST_ASSERT_EVSEL("wrong bp_len", HW_BREAKPOINT_LEN_2 == evsel->core.attr.bp_len, evsel);

	return TEST_OK;
}

static int
test__checkevent_breakpoint_len_rw_modifier(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_EVLIST("wrong number of entries", 1 == evlist->core.nr_entries, evlist);
	TEST_ASSERT_EVSEL("wrong exclude_user", !evsel->core.attr.exclude_user, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_kernel", evsel->core.attr.exclude_kernel, evsel);
	TEST_ASSERT_EVSEL("wrong exclude_hv", evsel->core.attr.exclude_hv, evsel);
	TEST_ASSERT_EVSEL("wrong precise_ip", !evsel->core.attr.precise_ip, evsel);

	return test__checkevent_breakpoint_rw(evlist);
}

static int test__checkevent_precise_max_modifier(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_EVLIST("wrong number of entries",
			   evlist->core.nr_entries == 1 + num_core_entries(evlist),
			   evlist);
	TEST_ASSERT_EVSEL("wrong type/config", evsel__match(evsel, SOFTWARE, SW_TASK_CLOCK), evsel);
	return TEST_OK;
}

static int test__checkevent_config_symbol(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_EVLIST("wrong number of entries",
			   evlist->core.nr_entries == num_core_entries(evlist),
			   evlist);
	TEST_ASSERT_EVSEL("wrong name setting", evsel__name_is(evsel, "insn"), evsel);
	return TEST_OK;
}

static int test__checkevent_config_raw(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_EVLIST("wrong number of entries", 1 == evlist->core.nr_entries, evlist);
	TEST_ASSERT_EVSEL("wrong name setting", evsel__name_is(evsel, "rawpmu"), evsel);
	return TEST_OK;
}

static int test__checkevent_config_num(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_EVLIST("wrong number of entries", 1 == evlist->core.nr_entries, evlist);
	TEST_ASSERT_EVSEL("wrong name setting", evsel__name_is(evsel, "numpmu"), evsel);
	return TEST_OK;
}

static int test__checkevent_config_cache(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_EVLIST("wrong number of entries",
			   evlist->core.nr_entries == num_core_entries(evlist),
			   evlist);
	TEST_ASSERT_EVSEL("wrong name setting", evsel__name_is(evsel, "cachepmu"), evsel);
	return test__checkevent_genhw(evlist);
}

static bool test__pmu_default_core_event_valid(void)
{
	struct perf_pmu *pmu = perf_pmus__find_core_pmu();

	if (!pmu)
		return false;

	return perf_pmu__has_format(pmu, "event");
}

static bool test__intel_pt_valid(void)
{
	return !!perf_pmus__find("intel_pt");
}

static int test__intel_pt(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_EVLIST("wrong number of entries", 1 == evlist->core.nr_entries, evlist);
	TEST_ASSERT_EVSEL("wrong name setting", evsel__name_is(evsel, "intel_pt//u"), evsel);
	return TEST_OK;
}

static bool test__acr_valid(void)
{
	struct perf_pmu *pmu = NULL;

	while ((pmu = perf_pmus__scan_core(pmu)) != NULL) {
		if (perf_pmu__has_format(pmu, "acr_mask"))
			return true;
	}

	return false;
}

static int test__ratio_to_prev(struct evlist *evlist)
{
	struct evsel *evsel;

	TEST_ASSERT_VAL("wrong number of entries", 2 * perf_pmus__num_core_pmus() == evlist->core.nr_entries);

	 evlist__for_each_entry(evlist, evsel) {
		if (!perf_pmu__has_format(evsel->pmu, "acr_mask"))
			return TEST_OK;

		if (evsel == evlist__first(evlist)) {
			TEST_ASSERT_VAL("wrong config2", 0 == evsel->core.attr.config2);
			TEST_ASSERT_VAL("wrong leader", evsel__is_group_leader(evsel));
			TEST_ASSERT_VAL("wrong core.nr_members", evsel->core.nr_members == 2);
			TEST_ASSERT_VAL("wrong group_idx", evsel__group_idx(evsel) == 0);
			TEST_ASSERT_EVSEL("unexpected event",
					evsel__match(evsel, HARDWARE, HW_CPU_CYCLES),
					evsel);
		} else {
			TEST_ASSERT_VAL("wrong config2", 0 == evsel->core.attr.config2);
			TEST_ASSERT_VAL("wrong leader", !evsel__is_group_leader(evsel));
			TEST_ASSERT_VAL("wrong core.nr_members", evsel->core.nr_members == 0);
			TEST_ASSERT_VAL("wrong group_idx", evsel__group_idx(evsel) == 1);
			TEST_ASSERT_EVSEL("unexpected event",
					evsel__match(evsel, HARDWARE, HW_INSTRUCTIONS),
					evsel);
		}
		/*
		 * The period value gets configured within evlist__config,
		 * while this test executes only parse events method.
		 */
		TEST_ASSERT_VAL("wrong period", 0 == evsel->core.attr.sample_period);
	}
	return TEST_OK;
}

static int test__checkevent_complex_name(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_EVLIST("wrong number of entries",
			   evlist->core.nr_entries == num_core_entries(evlist),
			   evlist);
	TEST_ASSERT_EVSEL("wrong complex name parsing",
			  evsel__name_is(evsel,
				  "COMPLEX_CYCLES_NAME:orig=cpu-cycles,desc=chip-clock-ticks"),
			  evsel);
	return TEST_OK;
}

static int test__checkevent_raw_pmu(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_EVLIST("wrong number of entries", 1 == evlist->core.nr_entries, evlist);
	TEST_ASSERT_EVSEL("wrong type", PERF_TYPE_SOFTWARE == evsel->core.attr.type, evsel);
	TEST_ASSERT_EVSEL("wrong config", 0x1a == evsel->core.attr.config, evsel);
	return TEST_OK;
}

static int test__sym_event_slash(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_EVLIST("wrong number of entries",
			   evlist->core.nr_entries == num_core_entries(evlist),
			   evlist);
	TEST_ASSERT_EVSEL("unexpected event", evsel__match(evsel, HARDWARE, HW_CPU_CYCLES), evsel);
	TEST_ASSERT_EVSEL("wrong exclude_kernel", evsel->core.attr.exclude_kernel, evsel);
	return TEST_OK;
}

static int test__sym_event_dc(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_EVLIST("wrong number of entries",
			   evlist->core.nr_entries == num_core_entries(evlist),
			   evlist);
	TEST_ASSERT_EVSEL("unexpected event", evsel__match(evsel, HARDWARE, HW_CPU_CYCLES), evsel);
	TEST_ASSERT_EVSEL("wrong exclude_user", evsel->core.attr.exclude_user, evsel);
	return TEST_OK;
}

static int test__term_equal_term(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_EVLIST("wrong number of entries",
			   evlist->core.nr_entries == num_core_entries(evlist),
			   evlist);
	TEST_ASSERT_EVSEL("unexpected event", evsel__match(evsel, HARDWARE, HW_CPU_CYCLES), evsel);
	TEST_ASSERT_EVSEL("wrong name setting", strcmp(evsel->name, "name") == 0, evsel);
	return TEST_OK;
}

static int test__term_equal_legacy(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_EVLIST("wrong number of entries",
			   evlist->core.nr_entries == num_core_entries(evlist),
			   evlist);
	TEST_ASSERT_EVSEL("unexpected event", evsel__match(evsel, HARDWARE, HW_CPU_CYCLES), evsel);
	TEST_ASSERT_EVSEL("wrong name setting", strcmp(evsel->name, "l1d") == 0, evsel);
	return TEST_OK;
}

static int count_tracepoints(void)
{
	struct dirent *events_ent;
	DIR *events_dir;
	int cnt = 0;

	events_dir = tracing_events__opendir();

	TEST_ASSERT_VAL("Can't open events dir", events_dir);

	while ((events_ent = readdir(events_dir))) {
		char *sys_path;
		struct dirent *sys_ent;
		DIR *sys_dir;

		if (!strcmp(events_ent->d_name, ".")
		    || !strcmp(events_ent->d_name, "..")
		    || !strcmp(events_ent->d_name, "enable")
		    || !strcmp(events_ent->d_name, "header_event")
		    || !strcmp(events_ent->d_name, "header_page"))
			continue;

		sys_path = get_events_file(events_ent->d_name);
		TEST_ASSERT_VAL("Can't get sys path", sys_path);

		sys_dir = opendir(sys_path);
		TEST_ASSERT_VAL("Can't open sys dir", sys_dir);

		while ((sys_ent = readdir(sys_dir))) {
			if (!strcmp(sys_ent->d_name, ".")
			    || !strcmp(sys_ent->d_name, "..")
			    || !strcmp(sys_ent->d_name, "enable")
			    || !strcmp(sys_ent->d_name, "filter"))
				continue;

			cnt++;
		}

		closedir(sys_dir);
		put_events_file(sys_path);
	}

	closedir(events_dir);
	return cnt;
}

static int test__all_tracepoints(struct evlist *evlist)
{
	TEST_ASSERT_VAL("wrong events count",
			count_tracepoints() == evlist->core.nr_entries);

	return test__checkevent_tracepoint_multi(evlist);
}

struct evlist_test {
	const char *name;
	bool (*valid)(void);
	int (*check)(struct evlist *evlist);
};

static const struct evlist_test test__events[] = {
	{
		.name  = "syscalls:sys_enter_openat",
		.check = test__checkevent_tracepoint,
		/* 0 */
	},
	{
		.name  = "syscalls:*",
		.check = test__checkevent_tracepoint_multi,
		/* 1 */
	},
	{
		.name  = "r1a",
		.check = test__checkevent_raw,
		/* 2 */
	},
	{
		.name  = "1:1",
		.check = test__checkevent_numeric,
		/* 3 */
	},
	{
		.name  = "instructions",
		.check = test__checkevent_symbolic_name,
		/* 4 */
	},
	{
		.name  = "cpu-cycles/period=100000,config2/",
		.check = test__checkevent_symbolic_name_config,
		/* 5 */
	},
	{
		.name  = "faults",
		.check = test__checkevent_symbolic_alias,
		/* 6 */
	},
	{
		.name  = "L1-dcache-load-miss",
		.check = test__checkevent_genhw,
		/* 7 */
	},
	{
		.name  = "mem:0",
		.check = test__checkevent_breakpoint,
		/* 8 */
	},
	{
		.name  = "mem:0:x",
		.check = test__checkevent_breakpoint_x,
		/* 9 */
	},
	{
		.name  = "mem:0:r",
		.check = test__checkevent_breakpoint_r,
		/* 0 */
	},
	{
		.name  = "mem:0:w",
		.check = test__checkevent_breakpoint_w,
		/* 1 */
	},
	{
		.name  = "syscalls:sys_enter_openat:k",
		.check = test__checkevent_tracepoint_modifier,
		/* 2 */
	},
	{
		.name  = "syscalls:*:u",
		.check = test__checkevent_tracepoint_multi_modifier,
		/* 3 */
	},
	{
		.name  = "r1a:kp",
		.check = test__checkevent_raw_modifier,
		/* 4 */
	},
	{
		.name  = "1:1:hp",
		.check = test__checkevent_numeric_modifier,
		/* 5 */
	},
	{
		.name  = "instructions:h",
		.check = test__checkevent_symbolic_name_modifier,
		/* 6 */
	},
	{
		.name  = "faults:u",
		.check = test__checkevent_symbolic_alias_modifier,
		/* 7 */
	},
	{
		.name  = "L1-dcache-load-miss:kp",
		.check = test__checkevent_genhw_modifier,
		/* 8 */
	},
	{
		.name  = "mem:0:u",
		.check = test__checkevent_breakpoint_modifier,
		/* 9 */
	},
	{
		.name  = "mem:0:x:k",
		.check = test__checkevent_breakpoint_x_modifier,
		/* 0 */
	},
	{
		.name  = "mem:0:r:hp",
		.check = test__checkevent_breakpoint_r_modifier,
		/* 1 */
	},
	{
		.name  = "mem:0:w:up",
		.check = test__checkevent_breakpoint_w_modifier,
		/* 2 */
	},
	{
		.name  = "r1,syscalls:sys_enter_openat:k,1:1:hp",
		.check = test__checkevent_list,
		/* 3 */
	},
	{
		.name  = "instructions:G",
		.check = test__checkevent_exclude_host_modifier,
		/* 4 */
	},
	{
		.name  = "instructions:H",
		.check = test__checkevent_exclude_guest_modifier,
		/* 5 */
	},
	{
		.name  = "mem:0:rw",
		.check = test__checkevent_breakpoint_rw,
		/* 6 */
	},
	{
		.name  = "mem:0:rw:kp",
		.check = test__checkevent_breakpoint_rw_modifier,
		/* 7 */
	},
	{
		.name  = "{instructions:k,cpu-cycles:upp}",
		.check = test__group1,
		/* 8 */
	},
	{
		.name  = "{faults:k,branches}:u,cpu-cycles:k",
		.check = test__group2,
		/* 9 */
	},
	{
		.name  = "group1{syscalls:sys_enter_openat:H,cpu-cycles:kppp},group2{cpu-cycles,1:3}:G,instructions:u",
		.check = test__group3,
		/* 0 */
	},
	{
		.name  = "{cpu-cycles:u,instructions:kp}:p",
		.check = test__group4,
		/* 1 */
	},
	{
		.name  = "{cpu-cycles,instructions}:G,{cpu-cycles:G,instructions:G},cpu-cycles",
		.check = test__group5,
		/* 2 */
	},
	{
		.name  = "*:*",
		.check = test__all_tracepoints,
		/* 3 */
	},
	{
		.name  = "{cpu-cycles,cache-misses:G}:H",
		.check = test__group_gh1,
		/* 4 */
	},
	{
		.name  = "{cpu-cycles,cache-misses:H}:G",
		.check = test__group_gh2,
		/* 5 */
	},
	{
		.name  = "{cpu-cycles:G,cache-misses:H}:u",
		.check = test__group_gh3,
		/* 6 */
	},
	{
		.name  = "{cpu-cycles:G,cache-misses:H}:uG",
		.check = test__group_gh4,
		/* 7 */
	},
	{
		.name  = "{cpu-cycles,cache-misses,branch-misses}:S",
		.check = test__leader_sample1,
		/* 8 */
	},
	{
		.name  = "{instructions,branch-misses}:Su",
		.check = test__leader_sample2,
		/* 9 */
	},
	{
		.name  = "instructions:uDp",
		.check = test__checkevent_pinned_modifier,
		/* 0 */
	},
	{
		.name  = "{cpu-cycles,cache-misses,branch-misses}:D",
		.check = test__pinned_group,
		/* 1 */
	},
	{
		.name  = "mem:0/1",
		.check = test__checkevent_breakpoint_len,
		/* 2 */
	},
	{
		.name  = "mem:0/2:w",
		.check = test__checkevent_breakpoint_len_w,
		/* 3 */
	},
	{
		.name  = "mem:0/4:rw:u",
		.check = test__checkevent_breakpoint_len_rw_modifier,
		/* 4 */
	},
#if defined(__s390x__)
	{
		.name  = "kvm-s390:kvm_s390_create_vm",
		.check = test__checkevent_tracepoint,
		.valid = kvm_s390_create_vm_valid,
		/* 0 */
	},
#endif
	{
		.name  = "instructions:I",
		.check = test__checkevent_exclude_idle_modifier,
		/* 5 */
	},
	{
		.name  = "instructions:kIG",
		.check = test__checkevent_exclude_idle_modifier_1,
		/* 6 */
	},
	{
		.name  = "task-clock:P,cpu-cycles",
		.check = test__checkevent_precise_max_modifier,
		/* 7 */
	},
	{
		.name  = "instructions/name=insn/",
		.check = test__checkevent_config_symbol,
		/* 8 */
	},
	{
		.name  = "r1234/name=rawpmu/",
		.check = test__checkevent_config_raw,
		/* 9 */
	},
	{
		.name  = "4:0x6530160/name=numpmu/",
		.check = test__checkevent_config_num,
		/* 0 */
	},
	{
		.name  = "L1-dcache-misses/name=cachepmu/",
		.check = test__checkevent_config_cache,
		/* 1 */
	},
	{
		.name  = "intel_pt//u",
		.valid = test__intel_pt_valid,
		.check = test__intel_pt,
		/* 2 */
	},
	{
		.name  = "cpu-cycles/name='COMPLEX_CYCLES_NAME:orig=cpu-cycles,desc=chip-clock-ticks'/Duk",
		.check = test__checkevent_complex_name,
		/* 3 */
	},
	{
		.name  = "cpu-cycles//u",
		.check = test__sym_event_slash,
		/* 4 */
	},
	{
		.name  = "cpu-cycles:k",
		.check = test__sym_event_dc,
		/* 5 */
	},
	{
		.name  = "instructions:uep",
		.check = test__checkevent_exclusive_modifier,
		/* 6 */
	},
	{
		.name  = "{cpu-cycles,cache-misses,branch-misses}:e",
		.check = test__exclusive_group,
		/* 7 */
	},
	{
		.name  = "cpu-cycles/name=name/",
		.check = test__term_equal_term,
		/* 8 */
	},
	{
		.name  = "cpu-cycles/name=l1d/",
		.check = test__term_equal_legacy,
		/* 9 */
	},
	{
		.name  = "mem:0/name=breakpoint/",
		.check = test__checkevent_breakpoint,
		/* 0 */
	},
	{
		.name  = "mem:0:x/name=breakpoint/",
		.check = test__checkevent_breakpoint_x,
		/* 1 */
	},
	{
		.name  = "mem:0:r/name=breakpoint/",
		.check = test__checkevent_breakpoint_r,
		/* 2 */
	},
	{
		.name  = "mem:0:w/name=breakpoint/",
		.check = test__checkevent_breakpoint_w,
		/* 3 */
	},
	{
		.name  = "mem:0/name=breakpoint/u",
		.check = test__checkevent_breakpoint_modifier_name,
		/* 4 */
	},
	{
		.name  = "mem:0:x/name=breakpoint/k",
		.check = test__checkevent_breakpoint_x_modifier_name,
		/* 5 */
	},
	{
		.name  = "mem:0:r/name=breakpoint/hp",
		.check = test__checkevent_breakpoint_r_modifier_name,
		/* 6 */
	},
	{
		.name  = "mem:0:w/name=breakpoint/up",
		.check = test__checkevent_breakpoint_w_modifier_name,
		/* 7 */
	},
	{
		.name  = "mem:0:rw/name=breakpoint/",
		.check = test__checkevent_breakpoint_rw,
		/* 8 */
	},
	{
		.name  = "mem:0:rw/name=breakpoint/kp",
		.check = test__checkevent_breakpoint_rw_modifier_name,
		/* 9 */
	},
	{
		.name  = "mem:0/1/name=breakpoint/",
		.check = test__checkevent_breakpoint_len,
		/* 0 */
	},
	{
		.name  = "mem:0/2:w/name=breakpoint/",
		.check = test__checkevent_breakpoint_len_w,
		/* 1 */
	},
	{
		.name  = "mem:0/4:rw/name=breakpoint/u",
		.check = test__checkevent_breakpoint_len_rw_modifier,
		/* 2 */
	},
	{
		.name  = "mem:0/1/name=breakpoint1/,mem:0/4:rw/name=breakpoint2/",
		.check = test__checkevent_breakpoint_2_events,
		/* 3 */
	},
	{
		.name = "9p:9p_client_req",
		.check = test__checkevent_tracepoint,
		/* 4 */
	},
	{
		.name  = "{cycles,instructions/period=200000,ratio-to-prev=2.0/}",
		.valid = test__acr_valid,
		.check = test__ratio_to_prev,
		/* 5 */
	},

};

static const struct evlist_test test__events_pmu[] = {
	{
		.name  = "default_core/config=10,config1=1,config2=3,period=1000/u",
		.check = test__checkevent_pmu,
		/* 0 */
	},
	{
		.name  = "default_core/config=1,name=krava/u,default_core/config=2/u",
		.check = test__checkevent_pmu_name,
		/* 1 */
	},
	{
		.name  = "default_core/config=1,call-graph=fp,time,period=100000/,default_core/config=2,call-graph=no,time=0,period=2000/",
		.check = test__checkevent_pmu_partial_time_callgraph,
		/* 2 */
	},
	{
		.name  = "default_core/name='COMPLEX_CYCLES_NAME:orig=cpu-cycles,desc=chip-clock-ticks',period=0x1,event=0x2/ukp",
		.valid = test__pmu_default_core_event_valid,
		.check = test__checkevent_complex_name,
		/* 3 */
	},
	{
		.name  = "software/r1a/",
		.check = test__checkevent_raw_pmu,
		/* 4 */
	},
	{
		.name  = "software/r0x1a/",
		.check = test__checkevent_raw_pmu,
		/* 5 */
	},
	{
		.name  = "default_core/L1-dcache-load-miss/",
		.check = test__checkevent_genhw,
		/* 6 */
	},
	{
		.name  = "default_core/L1-dcache-load-miss/kp",
		.check = test__checkevent_genhw_modifier,
		/* 7 */
	},
	{
		.name  = "default_core/L1-dcache-misses,name=cachepmu/",
		.check = test__checkevent_config_cache,
		/* 8 */
	},
	{
		.name  = "default_core/instructions/",
		.check = test__checkevent_symbolic_name,
		/* 9 */
	},
	{
		.name  = "default_core/cycles,period=100000,config2/",
		.check = test__checkevent_symbolic_name_config,
		/* 0 */
	},
	{
		.name  = "default_core/instructions/h",
		.check = test__checkevent_symbolic_name_modifier,
		/* 1 */
	},
	{
		.name  = "default_core/instructions/G",
		.check = test__checkevent_exclude_host_modifier,
		/* 2 */
	},
	{
		.name  = "default_core/instructions/H",
		.check = test__checkevent_exclude_guest_modifier,
		/* 3 */
	},
	{
		.name  = "{default_core/instructions/k,default_core/cycles/upp}",
		.check = test__group1,
		/* 4 */
	},
	{
		.name  = "{default_core/cycles/u,default_core/instructions/kp}:p",
		.check = test__group4,
		/* 5 */
	},
	{
		.name  = "{default_core/cycles/,default_core/cache-misses/G}:H",
		.check = test__group_gh1,
		/* 6 */
	},
	{
		.name  = "{default_core/cycles/,default_core/cache-misses/H}:G",
		.check = test__group_gh2,
		/* 7 */
	},
	{
		.name  = "{default_core/cycles/G,default_core/cache-misses/H}:u",
		.check = test__group_gh3,
		/* 8 */
	},
	{
		.name  = "{default_core/cycles/G,default_core/cache-misses/H}:uG",
		.check = test__group_gh4,
		/* 9 */
	},
	{
		.name  = "{default_core/cycles/,default_core/cache-misses/,default_core/branch-misses/}:S",
		.check = test__leader_sample1,
		/* 0 */
	},
	{
		.name  = "{default_core/instructions/,default_core/branch-misses/}:Su",
		.check = test__leader_sample2,
		/* 1 */
	},
	{
		.name  = "default_core/instructions/uDp",
		.check = test__checkevent_pinned_modifier,
		/* 2 */
	},
	{
		.name  = "{default_core/cycles/,default_core/cache-misses/,default_core/branch-misses/}:D",
		.check = test__pinned_group,
		/* 3 */
	},
	{
		.name  = "default_core/instructions/I",
		.check = test__checkevent_exclude_idle_modifier,
		/* 4 */
	},
	{
		.name  = "default_core/instructions/kIG",
		.check = test__checkevent_exclude_idle_modifier_1,
		/* 5 */
	},
	{
		.name  = "default_core/cycles/u",
		.check = test__sym_event_slash,
		/* 6 */
	},
	{
		.name  = "default_core/cycles/k",
		.check = test__sym_event_dc,
		/* 7 */
	},
	{
		.name  = "default_core/instructions/uep",
		.check = test__checkevent_exclusive_modifier,
		/* 8 */
	},
	{
		.name  = "{default_core/cycles/,default_core/cache-misses/,default_core/branch-misses/}:e",
		.check = test__exclusive_group,
		/* 9 */
	},
	{
		.name  = "default_core/cycles,name=name/",
		.check = test__term_equal_term,
		/* 0 */
	},
	{
		.name  = "default_core/cycles,name=l1d/",
		.check = test__term_equal_legacy,
		/* 1 */
	},
};

struct terms_test {
	const char *str;
	int (*check)(struct parse_events_terms *terms);
};

static const struct terms_test test__terms[] = {
	[0] = {
		.str   = "config=10,config1,config2=3,config3=4,umask=1,read,r0xead",
		.check = test__checkterms_simple,
	},
};

static int test_event(const struct evlist_test *e)
{
	struct parse_events_error err;
	struct evlist *evlist;
	int ret;

	if (e->valid && !e->valid()) {
		pr_debug("... SKIP\n");
		return TEST_OK;
	}

	evlist = evlist__new();
	if (evlist == NULL) {
		pr_err("Failed allocation");
		return TEST_FAIL;
	}
	parse_events_error__init(&err);
	ret = __parse_events(evlist, e->name, /*pmu_filter=*/NULL, &err, /*fake_pmu=*/false,
			     /*warn_if_reordered=*/true, /*fake_tp=*/true);
	if (ret) {
		pr_debug("failed to parse event '%s', err %d\n", e->name, ret);
		parse_events_error__print(&err, e->name);
		ret = TEST_FAIL;
		if (parse_events_error__contains(&err, "can't access trace events"))
			ret = TEST_SKIP;
	} else {
		ret = e->check(evlist);
	}
	parse_events_error__exit(&err);
	evlist__delete(evlist);

	return ret;
}

static int test_event_fake_pmu(const char *str)
{
	struct parse_events_error err;
	struct evlist *evlist;
	int ret;

	evlist = evlist__new();
	if (!evlist)
		return -ENOMEM;

	parse_events_error__init(&err);
	ret = __parse_events(evlist, str, /*pmu_filter=*/NULL, &err,
			     /*fake_pmu=*/true, /*warn_if_reordered=*/true,
			     /*fake_tp=*/true);
	if (ret) {
		pr_debug("failed to parse event '%s', err %d\n",
			 str, ret);
		parse_events_error__print(&err, str);
	}

	parse_events_error__exit(&err);
	evlist__delete(evlist);

	return ret;
}

static int combine_test_results(int existing, int latest)
{
	if (existing == TEST_FAIL)
		return TEST_FAIL;
	if (existing == TEST_SKIP)
		return latest == TEST_OK ? TEST_SKIP : latest;
	return latest;
}

static int test_events(const struct evlist_test *events, int cnt)
{
	int ret = TEST_OK;
	struct perf_pmu *core_pmu = perf_pmus__find_core_pmu();

	for (int i = 0; i < cnt; i++) {
		struct evlist_test e = events[i];
		int test_ret;
		const char *pos = e.name;
		char buf[1024], *buf_pos = buf, *end;

		while ((end = strstr(pos, "default_core"))) {
			size_t len = end - pos;

			strncpy(buf_pos, pos, len);
			pos = end + 12;
			buf_pos += len;
			strcpy(buf_pos, core_pmu->name);
			buf_pos += strlen(core_pmu->name);
		}
		strcpy(buf_pos, pos);

		e.name = buf;
		pr_debug("running test %d '%s'\n", i, e.name);
		test_ret = test_event(&e);
		if (test_ret != TEST_OK) {
			pr_debug("Event test failure: test %d '%s'", i, e.name);
			ret = combine_test_results(ret, test_ret);
		}
	}

	return ret;
}

static int test__events2(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	return test_events(test__events, ARRAY_SIZE(test__events));
}

static int test_term(const struct terms_test *t)
{
	struct parse_events_terms terms;
	int ret;


	parse_events_terms__init(&terms);
	ret = parse_events_terms(&terms, t->str);
	if (ret) {
		pr_debug("failed to parse terms '%s', err %d\n",
			 t->str , ret);
		return ret;
	}

	ret = t->check(&terms);
	parse_events_terms__exit(&terms);

	return ret;
}

static int test_terms(const struct terms_test *terms, int cnt)
{
	int ret = 0;

	for (int i = 0; i < cnt; i++) {
		const struct terms_test *t = &terms[i];

		pr_debug("running test %d '%s'\n", i, t->str);
		ret = test_term(t);
		if (ret)
			break;
	}

	return ret;
}

static int test__terms2(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	return test_terms(test__terms, ARRAY_SIZE(test__terms));
}

static int test__pmu_events(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	struct perf_pmu *pmu = NULL;
	int ret = TEST_OK;

	while ((pmu = perf_pmus__scan(pmu)) != NULL) {
		struct stat st;
		char path[PATH_MAX];
		char pmu_event[PATH_MAX];
		char *buf = NULL;
		FILE *file;
		struct dirent *ent;
		size_t len = 0;
		DIR *dir;
		int err;
		int n;

		snprintf(path, PATH_MAX, "%s/bus/event_source/devices/%s/events/",
			sysfs__mountpoint(), pmu->name);

		err = stat(path, &st);
		if (err) {
			pr_debug("skipping PMU %s events tests: %s\n", pmu->name, path);
			continue;
		}

		dir = opendir(path);
		if (!dir) {
			pr_debug("can't open pmu event dir: %s\n", path);
			ret = combine_test_results(ret, TEST_SKIP);
			continue;
		}

		while ((ent = readdir(dir))) {
			struct evlist_test e = { .name = NULL, };
			char name[2 * NAME_MAX + 1 + 12 + 3];
			int test_ret;
			bool is_event_parameterized = 0;

			/* Names containing . are special and cannot be used directly */
			if (strchr(ent->d_name, '.'))
				continue;

			/* exclude parameterized ones (name contains '?') */
			n = snprintf(pmu_event, sizeof(pmu_event), "%s%s", path, ent->d_name);
			if (n >= PATH_MAX) {
				pr_err("pmu event name crossed PATH_MAX(%d) size\n", PATH_MAX);
				continue;
			}

			file = fopen(pmu_event, "r");
			if (!file) {
				pr_debug("can't open pmu event file for '%s'\n", ent->d_name);
				ret = combine_test_results(ret, TEST_FAIL);
				continue;
			}

			if (getline(&buf, &len, file) < 0) {
				pr_debug(" pmu event: %s is a null event\n", ent->d_name);
				ret = combine_test_results(ret, TEST_FAIL);
				fclose(file);
				continue;
			}

			if (strchr(buf, '?'))
				is_event_parameterized = 1;

			free(buf);
			buf = NULL;
			fclose(file);

			if (is_event_parameterized == 1) {
				pr_debug("skipping parameterized PMU event: %s which contains ?\n", pmu_event);
				continue;
			}

			snprintf(name, sizeof(name), "%s/event=%s/u", pmu->name, ent->d_name);

			e.name  = name;
			e.check = test__checkevent_pmu_events;

			test_ret = test_event(&e);
			if (test_ret != TEST_OK) {
				pr_debug("Test PMU event failed for '%s'", name);
				ret = combine_test_results(ret, test_ret);
			}

			if (!is_pmu_core(pmu->name))
				continue;

			/*
			 * Names containing '-' are recognized as prefixes and suffixes
			 * due to '-' being a legacy PMU separator. This fails when the
			 * prefix or suffix collides with an existing legacy token. For
			 * example, branch-brs has a prefix (branch) that collides with
			 * a PE_NAME_CACHE_TYPE token causing a parse error as a suffix
			 * isn't expected after this. As event names in the config
			 * slashes are allowed a '-' in the name we check this works
			 * above.
			 */
			if (strchr(ent->d_name, '-'))
				continue;

			snprintf(name, sizeof(name), "%s:u,%s/event=%s/u",
				 ent->d_name, pmu->name, ent->d_name);
			e.name  = name;
			e.check = test__checkevent_pmu_events_mix;
			test_ret = test_event(&e);
			if (test_ret != TEST_OK) {
				pr_debug("Test PMU event failed for '%s'", name);
				ret = combine_test_results(ret, test_ret);
			}
		}

		closedir(dir);
	}
	return ret;
}

static int test__pmu_events2(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	return test_events(test__events_pmu, ARRAY_SIZE(test__events_pmu));
}

static bool test_alias(char **event, char **alias)
{
	char path[PATH_MAX];
	DIR *dir;
	struct dirent *dent;
	const char *sysfs = sysfs__mountpoint();
	char buf[128];
	FILE *file;

	if (!sysfs)
		return false;

	snprintf(path, PATH_MAX, "%s/bus/event_source/devices/", sysfs);
	dir = opendir(path);
	if (!dir)
		return false;

	while ((dent = readdir(dir))) {
		if (!strcmp(dent->d_name, ".") ||
		    !strcmp(dent->d_name, ".."))
			continue;

		snprintf(path, PATH_MAX, "%s/bus/event_source/devices/%s/alias",
			 sysfs, dent->d_name);

		if (!file_available(path))
			continue;

		file = fopen(path, "r");
		if (!file)
			continue;

		if (!fgets(buf, sizeof(buf), file)) {
			fclose(file);
			continue;
		}

		/* Remove the last '\n' */
		buf[strlen(buf) - 1] = 0;

		fclose(file);
		*event = strdup(dent->d_name);
		*alias = strdup(buf);
		closedir(dir);

		if (*event == NULL || *alias == NULL) {
			free(*event);
			free(*alias);
			return false;
		}

		return true;
	}

	closedir(dir);
	return false;
}

static int test__checkevent_pmu_events_alias(struct evlist *evlist)
{
	struct evsel *evsel1 = evlist__first(evlist);
	struct evsel *evsel2 = evlist__last(evlist);

	TEST_ASSERT_EVSEL("wrong type", evsel1->core.attr.type == evsel2->core.attr.type, evsel1);
	TEST_ASSERT_EVSEL("wrong config", evsel1->core.attr.config == evsel2->core.attr.config,
			  evsel1);
	return TEST_OK;
}

static int test__pmu_events_alias(char *event, char *alias)
{
	struct evlist_test e = { .name = NULL, };
	char name[2 * NAME_MAX + 20];

	snprintf(name, sizeof(name), "%s/event=1/,%s/event=1/",
		 event, alias);

	e.name  = name;
	e.check = test__checkevent_pmu_events_alias;
	return test_event(&e);
}

static int test__alias(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	char *event, *alias;
	int ret;

	if (!test_alias(&event, &alias))
		return TEST_SKIP;

	ret = test__pmu_events_alias(event, alias);

	free(event);
	free(alias);
	return ret;
}

static int test__pmu_events_alias2(struct test_suite *test __maybe_unused,
				   int subtest __maybe_unused)
{
	static const char events[][30] = {
			"event-hyphen",
			"event-two-hyph",
	};
	int ret = TEST_OK;

	for (unsigned int i = 0; i < ARRAY_SIZE(events); i++) {
		int test_ret = test_event_fake_pmu(&events[i][0]);

		if (test_ret != TEST_OK) {
			pr_debug("check_parse_fake %s failed\n", &events[i][0]);
			ret = combine_test_results(ret, test_ret);
		}
	}

	return ret;
}

static struct test_case tests__parse_events[] = {
	TEST_CASE_REASON("Test event parsing",
			 events2,
			 "permissions"),
	TEST_CASE_REASON("Parsing of all PMU events from sysfs",
			 pmu_events,
			 "permissions"),
	TEST_CASE_REASON("Parsing of given PMU events from sysfs",
			 pmu_events2,
			 "permissions"),
	TEST_CASE_REASON("Parsing of aliased events from sysfs", alias,
			 "no aliases in sysfs"),
	TEST_CASE("Parsing of aliased events", pmu_events_alias2),
	TEST_CASE("Parsing of terms (event modifiers)", terms2),
	{	.name = NULL, }
};

struct test_suite suite__parse_events = {
	.desc = "Parse event definition strings",
	.test_cases = tests__parse_events,
};
