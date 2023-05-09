// SPDX-License-Identifier: GPL-2.0
#include "parse-events.h"
#include "evsel.h"
#include "evlist.h"
#include <api/fs/fs.h>
#include "tests.h"
#include "debug.h"
#include "pmu.h"
#include "pmu-hybrid.h"
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

#ifdef HAVE_LIBTRACEEVENT

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

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong number of groups", 0 == evlist__nr_groups(evlist));
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_TRACEPOINT == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong sample_type",
		PERF_TP_SAMPLE_TYPE == evsel->core.attr.sample_type);
	TEST_ASSERT_VAL("wrong sample_period", 1 == evsel->core.attr.sample_period);
	return TEST_OK;
}

static int test__checkevent_tracepoint_multi(struct evlist *evlist)
{
	struct evsel *evsel;

	TEST_ASSERT_VAL("wrong number of entries", evlist->core.nr_entries > 1);
	TEST_ASSERT_VAL("wrong number of groups", 0 == evlist__nr_groups(evlist));

	evlist__for_each_entry(evlist, evsel) {
		TEST_ASSERT_VAL("wrong type",
			PERF_TYPE_TRACEPOINT == evsel->core.attr.type);
		TEST_ASSERT_VAL("wrong sample_type",
			PERF_TP_SAMPLE_TYPE == evsel->core.attr.sample_type);
		TEST_ASSERT_VAL("wrong sample_period",
			1 == evsel->core.attr.sample_period);
	}
	return TEST_OK;
}
#endif /* HAVE_LIBTRACEEVENT */

static int test__checkevent_raw(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_RAW == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config", 0x1a == evsel->core.attr.config);
	return TEST_OK;
}

static int test__checkevent_numeric(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong type", 1 == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config", 1 == evsel->core.attr.config);
	return TEST_OK;
}

static int test__checkevent_symbolic_name(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_INSTRUCTIONS == evsel->core.attr.config);
	return TEST_OK;
}

static int test__checkevent_symbolic_name_config(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CPU_CYCLES == evsel->core.attr.config);
	/*
	 * The period value gets configured within evlist__config,
	 * while this test executes only parse events method.
	 */
	TEST_ASSERT_VAL("wrong period",
			0 == evsel->core.attr.sample_period);
	TEST_ASSERT_VAL("wrong config1",
			0 == evsel->core.attr.config1);
	TEST_ASSERT_VAL("wrong config2",
			1 == evsel->core.attr.config2);
	return TEST_OK;
}

static int test__checkevent_symbolic_alias(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_SOFTWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_SW_PAGE_FAULTS == evsel->core.attr.config);
	return TEST_OK;
}

static int test__checkevent_genhw(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HW_CACHE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config", (1 << 16) == evsel->core.attr.config);
	return TEST_OK;
}

static int test__checkevent_breakpoint(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_BREAKPOINT == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config", 0 == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong bp_type", (HW_BREAKPOINT_R | HW_BREAKPOINT_W) ==
					 evsel->core.attr.bp_type);
	TEST_ASSERT_VAL("wrong bp_len", HW_BREAKPOINT_LEN_4 ==
					evsel->core.attr.bp_len);
	return TEST_OK;
}

static int test__checkevent_breakpoint_x(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_BREAKPOINT == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config", 0 == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong bp_type",
			HW_BREAKPOINT_X == evsel->core.attr.bp_type);
	TEST_ASSERT_VAL("wrong bp_len", sizeof(long) == evsel->core.attr.bp_len);
	return TEST_OK;
}

static int test__checkevent_breakpoint_r(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong type",
			PERF_TYPE_BREAKPOINT == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config", 0 == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong bp_type",
			HW_BREAKPOINT_R == evsel->core.attr.bp_type);
	TEST_ASSERT_VAL("wrong bp_len",
			HW_BREAKPOINT_LEN_4 == evsel->core.attr.bp_len);
	return TEST_OK;
}

static int test__checkevent_breakpoint_w(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong type",
			PERF_TYPE_BREAKPOINT == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config", 0 == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong bp_type",
			HW_BREAKPOINT_W == evsel->core.attr.bp_type);
	TEST_ASSERT_VAL("wrong bp_len",
			HW_BREAKPOINT_LEN_4 == evsel->core.attr.bp_len);
	return TEST_OK;
}

static int test__checkevent_breakpoint_rw(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong type",
			PERF_TYPE_BREAKPOINT == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config", 0 == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong bp_type",
		(HW_BREAKPOINT_R|HW_BREAKPOINT_W) == evsel->core.attr.bp_type);
	TEST_ASSERT_VAL("wrong bp_len",
			HW_BREAKPOINT_LEN_4 == evsel->core.attr.bp_len);
	return TEST_OK;
}

#ifdef HAVE_LIBTRACEEVENT
static int test__checkevent_tracepoint_modifier(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong exclude_user", evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);

	return test__checkevent_tracepoint(evlist);
}

static int
test__checkevent_tracepoint_multi_modifier(struct evlist *evlist)
{
	struct evsel *evsel;

	TEST_ASSERT_VAL("wrong number of entries", evlist->core.nr_entries > 1);

	evlist__for_each_entry(evlist, evsel) {
		TEST_ASSERT_VAL("wrong exclude_user",
				!evsel->core.attr.exclude_user);
		TEST_ASSERT_VAL("wrong exclude_kernel",
				evsel->core.attr.exclude_kernel);
		TEST_ASSERT_VAL("wrong exclude_hv", evsel->core.attr.exclude_hv);
		TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);
	}

	return test__checkevent_tracepoint_multi(evlist);
}
#endif /* HAVE_LIBTRACEEVENT */

static int test__checkevent_raw_modifier(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong exclude_user", evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", evsel->core.attr.precise_ip);

	return test__checkevent_raw(evlist);
}

static int test__checkevent_numeric_modifier(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong exclude_user", evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", evsel->core.attr.precise_ip);

	return test__checkevent_numeric(evlist);
}

static int test__checkevent_symbolic_name_modifier(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong exclude_user", evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);

	return test__checkevent_symbolic_name(evlist);
}

static int test__checkevent_exclude_host_modifier(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong exclude guest", !evsel->core.attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", evsel->core.attr.exclude_host);

	return test__checkevent_symbolic_name(evlist);
}

static int test__checkevent_exclude_guest_modifier(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong exclude guest", evsel->core.attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->core.attr.exclude_host);

	return test__checkevent_symbolic_name(evlist);
}

static int test__checkevent_symbolic_alias_modifier(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong exclude_user", !evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);

	return test__checkevent_symbolic_alias(evlist);
}

static int test__checkevent_genhw_modifier(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong exclude_user", evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", evsel->core.attr.precise_ip);

	return test__checkevent_genhw(evlist);
}

static int test__checkevent_exclude_idle_modifier(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong exclude idle", evsel->core.attr.exclude_idle);
	TEST_ASSERT_VAL("wrong exclude guest", !evsel->core.attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->core.attr.exclude_host);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);

	return test__checkevent_symbolic_name(evlist);
}

static int test__checkevent_exclude_idle_modifier_1(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong exclude idle", evsel->core.attr.exclude_idle);
	TEST_ASSERT_VAL("wrong exclude guest", !evsel->core.attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", evsel->core.attr.exclude_host);
	TEST_ASSERT_VAL("wrong exclude_user", evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);

	return test__checkevent_symbolic_name(evlist);
}

static int test__checkevent_breakpoint_modifier(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);


	TEST_ASSERT_VAL("wrong exclude_user", !evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);
	TEST_ASSERT_VAL("wrong name",
			!strcmp(evsel__name(evsel), "mem:0:u"));

	return test__checkevent_breakpoint(evlist);
}

static int test__checkevent_breakpoint_x_modifier(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong exclude_user", evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);
	TEST_ASSERT_VAL("wrong name",
			!strcmp(evsel__name(evsel), "mem:0:x:k"));

	return test__checkevent_breakpoint_x(evlist);
}

static int test__checkevent_breakpoint_r_modifier(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong exclude_user", evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", evsel->core.attr.precise_ip);
	TEST_ASSERT_VAL("wrong name",
			!strcmp(evsel__name(evsel), "mem:0:r:hp"));

	return test__checkevent_breakpoint_r(evlist);
}

static int test__checkevent_breakpoint_w_modifier(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong exclude_user", !evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", evsel->core.attr.precise_ip);
	TEST_ASSERT_VAL("wrong name",
			!strcmp(evsel__name(evsel), "mem:0:w:up"));

	return test__checkevent_breakpoint_w(evlist);
}

static int test__checkevent_breakpoint_rw_modifier(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong exclude_user", evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", evsel->core.attr.precise_ip);
	TEST_ASSERT_VAL("wrong name",
			!strcmp(evsel__name(evsel), "mem:0:rw:kp"));

	return test__checkevent_breakpoint_rw(evlist);
}

static int test__checkevent_pmu(struct evlist *evlist)
{

	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_RAW == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",    10 == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong config1",    1 == evsel->core.attr.config1);
	TEST_ASSERT_VAL("wrong config2",    3 == evsel->core.attr.config2);
	TEST_ASSERT_VAL("wrong config3",    0 == evsel->core.attr.config3);
	/*
	 * The period value gets configured within evlist__config,
	 * while this test executes only parse events method.
	 */
	TEST_ASSERT_VAL("wrong period",     0 == evsel->core.attr.sample_period);

	return TEST_OK;
}

#ifdef HAVE_LIBTRACEEVENT
static int test__checkevent_list(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 3 == evlist->core.nr_entries);

	/* r1 */
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_RAW == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config", 1 == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong config1", 0 == evsel->core.attr.config1);
	TEST_ASSERT_VAL("wrong config2", 0 == evsel->core.attr.config2);
	TEST_ASSERT_VAL("wrong config3", 0 == evsel->core.attr.config3);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);

	/* syscalls:sys_enter_openat:k */
	evsel = evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_TRACEPOINT == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong sample_type",
		PERF_TP_SAMPLE_TYPE == evsel->core.attr.sample_type);
	TEST_ASSERT_VAL("wrong sample_period", 1 == evsel->core.attr.sample_period);
	TEST_ASSERT_VAL("wrong exclude_user", evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);

	/* 1:1:hp */
	evsel = evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", 1 == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config", 1 == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", evsel->core.attr.precise_ip);

	return TEST_OK;
}
#endif

static int test__checkevent_pmu_name(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	/* cpu/config=1,name=krava/u */
	TEST_ASSERT_VAL("wrong number of entries", 2 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_RAW == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",  1 == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong name", !strcmp(evsel__name(evsel), "krava"));

	/* cpu/config=2/u" */
	evsel = evsel__next(evsel);
	TEST_ASSERT_VAL("wrong number of entries", 2 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_RAW == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",  2 == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong name",
			!strcmp(evsel__name(evsel), "cpu/config=2/u"));

	return TEST_OK;
}

static int test__checkevent_pmu_partial_time_callgraph(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	/* cpu/config=1,call-graph=fp,time,period=100000/ */
	TEST_ASSERT_VAL("wrong number of entries", 2 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_RAW == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",  1 == evsel->core.attr.config);
	/*
	 * The period, time and callgraph value gets configured within evlist__config,
	 * while this test executes only parse events method.
	 */
	TEST_ASSERT_VAL("wrong period",     0 == evsel->core.attr.sample_period);
	TEST_ASSERT_VAL("wrong callgraph",  !evsel__has_callchain(evsel));
	TEST_ASSERT_VAL("wrong time",  !(PERF_SAMPLE_TIME & evsel->core.attr.sample_type));

	/* cpu/config=2,call-graph=no,time=0,period=2000/ */
	evsel = evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_RAW == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",  2 == evsel->core.attr.config);
	/*
	 * The period, time and callgraph value gets configured within evlist__config,
	 * while this test executes only parse events method.
	 */
	TEST_ASSERT_VAL("wrong period",     0 == evsel->core.attr.sample_period);
	TEST_ASSERT_VAL("wrong callgraph",  !evsel__has_callchain(evsel));
	TEST_ASSERT_VAL("wrong time",  !(PERF_SAMPLE_TIME & evsel->core.attr.sample_type));

	return TEST_OK;
}

static int test__checkevent_pmu_events(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_RAW == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong exclude_user",
			!evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel",
			evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);
	TEST_ASSERT_VAL("wrong pinned", !evsel->core.attr.pinned);
	TEST_ASSERT_VAL("wrong exclusive", !evsel->core.attr.exclusive);

	return TEST_OK;
}


static int test__checkevent_pmu_events_mix(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	/* pmu-event:u */
	TEST_ASSERT_VAL("wrong number of entries", 2 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong exclude_user",
			!evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel",
			evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);
	TEST_ASSERT_VAL("wrong pinned", !evsel->core.attr.pinned);
	TEST_ASSERT_VAL("wrong exclusive", !evsel->core.attr.exclusive);

	/* cpu/pmu-event/u*/
	evsel = evsel__next(evsel);
	TEST_ASSERT_VAL("wrong number of entries", 2 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_RAW == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong exclude_user",
			!evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel",
			evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);
	TEST_ASSERT_VAL("wrong pinned", !evsel->core.attr.pinned);
	TEST_ASSERT_VAL("wrong exclusive", !evsel->core.attr.pinned);

	return TEST_OK;
}

static int test__checkterms_simple(struct list_head *terms)
{
	struct parse_events_term *term;

	/* config=10 */
	term = list_entry(terms->next, struct parse_events_term, list);
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
			term->type_term == PARSE_EVENTS__TERM_TYPE_USER);
	TEST_ASSERT_VAL("wrong type val",
			term->type_val == PARSE_EVENTS__TERM_TYPE_NUM);
	TEST_ASSERT_VAL("wrong val", term->val.num == 1);
	TEST_ASSERT_VAL("wrong config", !strcmp(term->config, "read"));

	/*
	 * r0xead
	 *
	 * To be still able to pass 'ead' value with 'r' syntax,
	 * we added support to parse 'r0xHEX' event.
	 */
	term = list_entry(term->list.next, struct parse_events_term, list);
	TEST_ASSERT_VAL("wrong type term",
			term->type_term == PARSE_EVENTS__TERM_TYPE_CONFIG);
	TEST_ASSERT_VAL("wrong type val",
			term->type_val == PARSE_EVENTS__TERM_TYPE_NUM);
	TEST_ASSERT_VAL("wrong val", term->val.num == 0xead);
	TEST_ASSERT_VAL("wrong config", !strcmp(term->config, "config"));
	return TEST_OK;
}

static int test__group1(struct evlist *evlist)
{
	struct evsel *evsel, *leader;

	TEST_ASSERT_VAL("wrong number of entries", 2 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong number of groups", 1 == evlist__nr_groups(evlist));

	/* instructions:k */
	evsel = leader = evlist__first(evlist);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_INSTRUCTIONS == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", !evsel->core.attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->core.attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);
	TEST_ASSERT_VAL("wrong leader", evsel__is_group_leader(evsel));
	TEST_ASSERT_VAL("wrong core.nr_members", evsel->core.nr_members == 2);
	TEST_ASSERT_VAL("wrong group_idx", evsel__group_idx(evsel) == 0);
	TEST_ASSERT_VAL("wrong sample_read", !evsel->sample_read);

	/* cycles:upp */
	evsel = evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CPU_CYCLES == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->core.attr.exclude_hv);
	/* use of precise requires exclude_guest */
	TEST_ASSERT_VAL("wrong exclude guest", evsel->core.attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->core.attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", evsel->core.attr.precise_ip == 2);
	TEST_ASSERT_VAL("wrong leader", evsel__has_leader(evsel, leader));
	TEST_ASSERT_VAL("wrong group_idx", evsel__group_idx(evsel) == 1);
	TEST_ASSERT_VAL("wrong sample_read", !evsel->sample_read);

	return TEST_OK;
}

static int test__group2(struct evlist *evlist)
{
	struct evsel *evsel, *leader;

	TEST_ASSERT_VAL("wrong number of entries", 3 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong number of groups", 1 == evlist__nr_groups(evlist));

	/* faults + :ku modifier */
	evsel = leader = evlist__first(evlist);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_SOFTWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_SW_PAGE_FAULTS == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", evsel->core.attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->core.attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);
	TEST_ASSERT_VAL("wrong leader", evsel__is_group_leader(evsel));
	TEST_ASSERT_VAL("wrong core.nr_members", evsel->core.nr_members == 2);
	TEST_ASSERT_VAL("wrong group_idx", evsel__group_idx(evsel) == 0);
	TEST_ASSERT_VAL("wrong sample_read", !evsel->sample_read);

	/* cache-references + :u modifier */
	evsel = evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CACHE_REFERENCES == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", evsel->core.attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->core.attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);
	TEST_ASSERT_VAL("wrong leader", evsel__has_leader(evsel, leader));
	TEST_ASSERT_VAL("wrong group_idx", evsel__group_idx(evsel) == 1);
	TEST_ASSERT_VAL("wrong sample_read", !evsel->sample_read);

	/* cycles:k */
	evsel = evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CPU_CYCLES == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", !evsel->core.attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->core.attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);
	TEST_ASSERT_VAL("wrong leader", evsel__is_group_leader(evsel));
	TEST_ASSERT_VAL("wrong sample_read", !evsel->sample_read);

	return TEST_OK;
}

#ifdef HAVE_LIBTRACEEVENT
static int test__group3(struct evlist *evlist __maybe_unused)
{
	struct evsel *evsel, *leader;

	TEST_ASSERT_VAL("wrong number of entries", 5 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong number of groups", 2 == evlist__nr_groups(evlist));

	/* group1 syscalls:sys_enter_openat:H */
	evsel = leader = evlist__first(evlist);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_TRACEPOINT == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong sample_type",
		PERF_TP_SAMPLE_TYPE == evsel->core.attr.sample_type);
	TEST_ASSERT_VAL("wrong sample_period", 1 == evsel->core.attr.sample_period);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", evsel->core.attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->core.attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);
	TEST_ASSERT_VAL("wrong leader", evsel__is_group_leader(evsel));
	TEST_ASSERT_VAL("wrong group name",
		!strcmp(leader->group_name, "group1"));
	TEST_ASSERT_VAL("wrong core.nr_members", evsel->core.nr_members == 2);
	TEST_ASSERT_VAL("wrong group_idx", evsel__group_idx(evsel) == 0);
	TEST_ASSERT_VAL("wrong sample_read", !evsel->sample_read);

	/* group1 cycles:kppp */
	evsel = evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CPU_CYCLES == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->core.attr.exclude_hv);
	/* use of precise requires exclude_guest */
	TEST_ASSERT_VAL("wrong exclude guest", evsel->core.attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->core.attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", evsel->core.attr.precise_ip == 3);
	TEST_ASSERT_VAL("wrong leader", evsel__has_leader(evsel, leader));
	TEST_ASSERT_VAL("wrong group name", !evsel->group_name);
	TEST_ASSERT_VAL("wrong group_idx", evsel__group_idx(evsel) == 1);
	TEST_ASSERT_VAL("wrong sample_read", !evsel->sample_read);

	/* group2 cycles + G modifier */
	evsel = leader = evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CPU_CYCLES == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", !evsel->core.attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", evsel->core.attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);
	TEST_ASSERT_VAL("wrong leader", evsel__is_group_leader(evsel));
	TEST_ASSERT_VAL("wrong group name",
		!strcmp(leader->group_name, "group2"));
	TEST_ASSERT_VAL("wrong core.nr_members", evsel->core.nr_members == 2);
	TEST_ASSERT_VAL("wrong group_idx", evsel__group_idx(evsel) == 0);
	TEST_ASSERT_VAL("wrong sample_read", !evsel->sample_read);

	/* group2 1:3 + G modifier */
	evsel = evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", 1 == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config", 3 == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", !evsel->core.attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", evsel->core.attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);
	TEST_ASSERT_VAL("wrong leader", evsel__has_leader(evsel, leader));
	TEST_ASSERT_VAL("wrong group_idx", evsel__group_idx(evsel) == 1);
	TEST_ASSERT_VAL("wrong sample_read", !evsel->sample_read);

	/* instructions:u */
	evsel = evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_INSTRUCTIONS == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", evsel->core.attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->core.attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);
	TEST_ASSERT_VAL("wrong leader", evsel__is_group_leader(evsel));
	TEST_ASSERT_VAL("wrong sample_read", !evsel->sample_read);

	return TEST_OK;
}
#endif

static int test__group4(struct evlist *evlist __maybe_unused)
{
	struct evsel *evsel, *leader;

	TEST_ASSERT_VAL("wrong number of entries", 2 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong number of groups", 1 == evlist__nr_groups(evlist));

	/* cycles:u + p */
	evsel = leader = evlist__first(evlist);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CPU_CYCLES == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->core.attr.exclude_hv);
	/* use of precise requires exclude_guest */
	TEST_ASSERT_VAL("wrong exclude guest", evsel->core.attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->core.attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", evsel->core.attr.precise_ip == 1);
	TEST_ASSERT_VAL("wrong group name", !evsel->group_name);
	TEST_ASSERT_VAL("wrong leader", evsel__is_group_leader(evsel));
	TEST_ASSERT_VAL("wrong core.nr_members", evsel->core.nr_members == 2);
	TEST_ASSERT_VAL("wrong group_idx", evsel__group_idx(evsel) == 0);
	TEST_ASSERT_VAL("wrong sample_read", !evsel->sample_read);

	/* instructions:kp + p */
	evsel = evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_INSTRUCTIONS == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->core.attr.exclude_hv);
	/* use of precise requires exclude_guest */
	TEST_ASSERT_VAL("wrong exclude guest", evsel->core.attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->core.attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", evsel->core.attr.precise_ip == 2);
	TEST_ASSERT_VAL("wrong leader", evsel__has_leader(evsel, leader));
	TEST_ASSERT_VAL("wrong group_idx", evsel__group_idx(evsel) == 1);
	TEST_ASSERT_VAL("wrong sample_read", !evsel->sample_read);

	return TEST_OK;
}

static int test__group5(struct evlist *evlist __maybe_unused)
{
	struct evsel *evsel, *leader;

	TEST_ASSERT_VAL("wrong number of entries", 5 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong number of groups", 2 == evlist__nr_groups(evlist));

	/* cycles + G */
	evsel = leader = evlist__first(evlist);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CPU_CYCLES == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", !evsel->core.attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", evsel->core.attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);
	TEST_ASSERT_VAL("wrong group name", !evsel->group_name);
	TEST_ASSERT_VAL("wrong leader", evsel__is_group_leader(evsel));
	TEST_ASSERT_VAL("wrong core.nr_members", evsel->core.nr_members == 2);
	TEST_ASSERT_VAL("wrong group_idx", evsel__group_idx(evsel) == 0);
	TEST_ASSERT_VAL("wrong sample_read", !evsel->sample_read);

	/* instructions + G */
	evsel = evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_INSTRUCTIONS == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", !evsel->core.attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", evsel->core.attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);
	TEST_ASSERT_VAL("wrong leader", evsel__has_leader(evsel, leader));
	TEST_ASSERT_VAL("wrong group_idx", evsel__group_idx(evsel) == 1);
	TEST_ASSERT_VAL("wrong sample_read", !evsel->sample_read);

	/* cycles:G */
	evsel = leader = evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CPU_CYCLES == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", !evsel->core.attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", evsel->core.attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);
	TEST_ASSERT_VAL("wrong group name", !evsel->group_name);
	TEST_ASSERT_VAL("wrong leader", evsel__is_group_leader(evsel));
	TEST_ASSERT_VAL("wrong core.nr_members", evsel->core.nr_members == 2);
	TEST_ASSERT_VAL("wrong group_idx", evsel__group_idx(evsel) == 0);
	TEST_ASSERT_VAL("wrong sample_read", !evsel->sample_read);

	/* instructions:G */
	evsel = evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_INSTRUCTIONS == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", !evsel->core.attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", evsel->core.attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);
	TEST_ASSERT_VAL("wrong leader", evsel__has_leader(evsel, leader));
	TEST_ASSERT_VAL("wrong group_idx", evsel__group_idx(evsel) == 1);

	/* cycles */
	evsel = evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CPU_CYCLES == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", evsel->core.attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->core.attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);
	TEST_ASSERT_VAL("wrong leader", evsel__is_group_leader(evsel));

	return TEST_OK;
}

static int test__group_gh1(struct evlist *evlist)
{
	struct evsel *evsel, *leader;

	TEST_ASSERT_VAL("wrong number of entries", 2 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong number of groups", 1 == evlist__nr_groups(evlist));

	/* cycles + :H group modifier */
	evsel = leader = evlist__first(evlist);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CPU_CYCLES == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", evsel->core.attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->core.attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);
	TEST_ASSERT_VAL("wrong group name", !evsel->group_name);
	TEST_ASSERT_VAL("wrong leader", evsel__is_group_leader(evsel));
	TEST_ASSERT_VAL("wrong core.nr_members", evsel->core.nr_members == 2);
	TEST_ASSERT_VAL("wrong group_idx", evsel__group_idx(evsel) == 0);

	/* cache-misses:G + :H group modifier */
	evsel = evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CACHE_MISSES == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", !evsel->core.attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->core.attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);
	TEST_ASSERT_VAL("wrong leader", evsel__has_leader(evsel, leader));
	TEST_ASSERT_VAL("wrong group_idx", evsel__group_idx(evsel) == 1);

	return TEST_OK;
}

static int test__group_gh2(struct evlist *evlist)
{
	struct evsel *evsel, *leader;

	TEST_ASSERT_VAL("wrong number of entries", 2 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong number of groups", 1 == evlist__nr_groups(evlist));

	/* cycles + :G group modifier */
	evsel = leader = evlist__first(evlist);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CPU_CYCLES == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", !evsel->core.attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", evsel->core.attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);
	TEST_ASSERT_VAL("wrong group name", !evsel->group_name);
	TEST_ASSERT_VAL("wrong leader", evsel__is_group_leader(evsel));
	TEST_ASSERT_VAL("wrong core.nr_members", evsel->core.nr_members == 2);
	TEST_ASSERT_VAL("wrong group_idx", evsel__group_idx(evsel) == 0);

	/* cache-misses:H + :G group modifier */
	evsel = evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CACHE_MISSES == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", !evsel->core.attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->core.attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);
	TEST_ASSERT_VAL("wrong leader", evsel__has_leader(evsel, leader));
	TEST_ASSERT_VAL("wrong group_idx", evsel__group_idx(evsel) == 1);

	return TEST_OK;
}

static int test__group_gh3(struct evlist *evlist)
{
	struct evsel *evsel, *leader;

	TEST_ASSERT_VAL("wrong number of entries", 2 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong number of groups", 1 == evlist__nr_groups(evlist));

	/* cycles:G + :u group modifier */
	evsel = leader = evlist__first(evlist);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CPU_CYCLES == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", !evsel->core.attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", evsel->core.attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);
	TEST_ASSERT_VAL("wrong group name", !evsel->group_name);
	TEST_ASSERT_VAL("wrong leader", evsel__is_group_leader(evsel));
	TEST_ASSERT_VAL("wrong core.nr_members", evsel->core.nr_members == 2);
	TEST_ASSERT_VAL("wrong group_idx", evsel__group_idx(evsel) == 0);

	/* cache-misses:H + :u group modifier */
	evsel = evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CACHE_MISSES == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", evsel->core.attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->core.attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);
	TEST_ASSERT_VAL("wrong leader", evsel__has_leader(evsel, leader));
	TEST_ASSERT_VAL("wrong group_idx", evsel__group_idx(evsel) == 1);

	return TEST_OK;
}

static int test__group_gh4(struct evlist *evlist)
{
	struct evsel *evsel, *leader;

	TEST_ASSERT_VAL("wrong number of entries", 2 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong number of groups", 1 == evlist__nr_groups(evlist));

	/* cycles:G + :uG group modifier */
	evsel = leader = evlist__first(evlist);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CPU_CYCLES == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", !evsel->core.attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", evsel->core.attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);
	TEST_ASSERT_VAL("wrong group name", !evsel->group_name);
	TEST_ASSERT_VAL("wrong leader", evsel__is_group_leader(evsel));
	TEST_ASSERT_VAL("wrong core.nr_members", evsel->core.nr_members == 2);
	TEST_ASSERT_VAL("wrong group_idx", evsel__group_idx(evsel) == 0);

	/* cache-misses:H + :uG group modifier */
	evsel = evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CACHE_MISSES == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", !evsel->core.attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->core.attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);
	TEST_ASSERT_VAL("wrong leader", evsel__has_leader(evsel, leader));
	TEST_ASSERT_VAL("wrong group_idx", evsel__group_idx(evsel) == 1);

	return TEST_OK;
}

static int test__leader_sample1(struct evlist *evlist)
{
	struct evsel *evsel, *leader;

	TEST_ASSERT_VAL("wrong number of entries", 3 == evlist->core.nr_entries);

	/* cycles - sampling group leader */
	evsel = leader = evlist__first(evlist);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CPU_CYCLES == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", evsel->core.attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->core.attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);
	TEST_ASSERT_VAL("wrong group name", !evsel->group_name);
	TEST_ASSERT_VAL("wrong leader", evsel__has_leader(evsel, leader));
	TEST_ASSERT_VAL("wrong sample_read", evsel->sample_read);

	/* cache-misses - not sampling */
	evsel = evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CACHE_MISSES == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", evsel->core.attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->core.attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);
	TEST_ASSERT_VAL("wrong leader", evsel__has_leader(evsel, leader));
	TEST_ASSERT_VAL("wrong sample_read", evsel->sample_read);

	/* branch-misses - not sampling */
	evsel = evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_BRANCH_MISSES == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", evsel->core.attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->core.attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);
	TEST_ASSERT_VAL("wrong group name", !evsel->group_name);
	TEST_ASSERT_VAL("wrong leader", evsel__has_leader(evsel, leader));
	TEST_ASSERT_VAL("wrong sample_read", evsel->sample_read);

	return TEST_OK;
}

static int test__leader_sample2(struct evlist *evlist __maybe_unused)
{
	struct evsel *evsel, *leader;

	TEST_ASSERT_VAL("wrong number of entries", 2 == evlist->core.nr_entries);

	/* instructions - sampling group leader */
	evsel = leader = evlist__first(evlist);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_INSTRUCTIONS == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", evsel->core.attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->core.attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);
	TEST_ASSERT_VAL("wrong group name", !evsel->group_name);
	TEST_ASSERT_VAL("wrong leader", evsel__has_leader(evsel, leader));
	TEST_ASSERT_VAL("wrong sample_read", evsel->sample_read);

	/* branch-misses - not sampling */
	evsel = evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_BRANCH_MISSES == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", evsel->core.attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->core.attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);
	TEST_ASSERT_VAL("wrong group name", !evsel->group_name);
	TEST_ASSERT_VAL("wrong leader", evsel__has_leader(evsel, leader));
	TEST_ASSERT_VAL("wrong sample_read", evsel->sample_read);

	return TEST_OK;
}

static int test__checkevent_pinned_modifier(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong exclude_user", !evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", evsel->core.attr.precise_ip);
	TEST_ASSERT_VAL("wrong pinned", evsel->core.attr.pinned);

	return test__checkevent_symbolic_name(evlist);
}

static int test__pinned_group(struct evlist *evlist)
{
	struct evsel *evsel, *leader;

	TEST_ASSERT_VAL("wrong number of entries", 3 == evlist->core.nr_entries);

	/* cycles - group leader */
	evsel = leader = evlist__first(evlist);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CPU_CYCLES == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong group name", !evsel->group_name);
	TEST_ASSERT_VAL("wrong leader", evsel__has_leader(evsel, leader));
	TEST_ASSERT_VAL("wrong pinned", evsel->core.attr.pinned);

	/* cache-misses - can not be pinned, but will go on with the leader */
	evsel = evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CACHE_MISSES == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong pinned", !evsel->core.attr.pinned);

	/* branch-misses - ditto */
	evsel = evsel__next(evsel);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_BRANCH_MISSES == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong pinned", !evsel->core.attr.pinned);

	return TEST_OK;
}

static int test__checkevent_exclusive_modifier(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong exclude_user", !evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", evsel->core.attr.precise_ip);
	TEST_ASSERT_VAL("wrong exclusive", evsel->core.attr.exclusive);

	return test__checkevent_symbolic_name(evlist);
}

static int test__exclusive_group(struct evlist *evlist)
{
	struct evsel *evsel, *leader;

	TEST_ASSERT_VAL("wrong number of entries", 3 == evlist->core.nr_entries);

	/* cycles - group leader */
	evsel = leader = evlist__first(evlist);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CPU_CYCLES == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong group name", !evsel->group_name);
	TEST_ASSERT_VAL("wrong leader", evsel__has_leader(evsel, leader));
	TEST_ASSERT_VAL("wrong exclusive", evsel->core.attr.exclusive);

	/* cache-misses - can not be pinned, but will go on with the leader */
	evsel = evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CACHE_MISSES == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong exclusive", !evsel->core.attr.exclusive);

	/* branch-misses - ditto */
	evsel = evsel__next(evsel);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_BRANCH_MISSES == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong exclusive", !evsel->core.attr.exclusive);

	return TEST_OK;
}
static int test__checkevent_breakpoint_len(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_BREAKPOINT == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config", 0 == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong bp_type", (HW_BREAKPOINT_R | HW_BREAKPOINT_W) ==
					 evsel->core.attr.bp_type);
	TEST_ASSERT_VAL("wrong bp_len", HW_BREAKPOINT_LEN_1 ==
					evsel->core.attr.bp_len);

	return TEST_OK;
}

static int test__checkevent_breakpoint_len_w(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_BREAKPOINT == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config", 0 == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong bp_type", HW_BREAKPOINT_W ==
					 evsel->core.attr.bp_type);
	TEST_ASSERT_VAL("wrong bp_len", HW_BREAKPOINT_LEN_2 ==
					evsel->core.attr.bp_len);

	return TEST_OK;
}

static int
test__checkevent_breakpoint_len_rw_modifier(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong exclude_user", !evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->core.attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->core.attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->core.attr.precise_ip);

	return test__checkevent_breakpoint_rw(evlist);
}

static int test__checkevent_precise_max_modifier(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 2 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_SOFTWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_SW_TASK_CLOCK == evsel->core.attr.config);
	return TEST_OK;
}

static int test__checkevent_config_symbol(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong name setting", evsel__name_is(evsel, "insn"));
	return TEST_OK;
}

static int test__checkevent_config_raw(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong name setting", evsel__name_is(evsel, "rawpmu"));
	return TEST_OK;
}

static int test__checkevent_config_num(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong name setting", evsel__name_is(evsel, "numpmu"));
	return TEST_OK;
}

static int test__checkevent_config_cache(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong name setting", evsel__name_is(evsel, "cachepmu"));
	return TEST_OK;
}

static bool test__intel_pt_valid(void)
{
	return !!perf_pmu__find("intel_pt");
}

static int test__intel_pt(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong name setting", evsel__name_is(evsel, "intel_pt//u"));
	return TEST_OK;
}

static int test__checkevent_complex_name(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong complex name parsing", evsel__name_is(evsel, "COMPLEX_CYCLES_NAME:orig=cycles,desc=chip-clock-ticks"));
	return TEST_OK;
}

static int test__checkevent_raw_pmu(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_SOFTWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config", 0x1a == evsel->core.attr.config);
	return TEST_OK;
}

static int test__sym_event_slash(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong type", evsel->core.attr.type == PERF_TYPE_HARDWARE);
	TEST_ASSERT_VAL("wrong config", evsel->core.attr.config == PERF_COUNT_HW_CPU_CYCLES);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->core.attr.exclude_kernel);
	return TEST_OK;
}

static int test__sym_event_dc(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong type", evsel->core.attr.type == PERF_TYPE_HARDWARE);
	TEST_ASSERT_VAL("wrong config", evsel->core.attr.config == PERF_COUNT_HW_CPU_CYCLES);
	TEST_ASSERT_VAL("wrong exclude_user", evsel->core.attr.exclude_user);
	return TEST_OK;
}

#ifdef HAVE_LIBTRACEEVENT
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
#endif /* HAVE_LIBTRACEVENT */

static int test__hybrid_hw_event_with_pmu(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_RAW == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config", 0x3c == evsel->core.attr.config);
	return TEST_OK;
}

static int test__hybrid_hw_group_event(struct evlist *evlist)
{
	struct evsel *evsel, *leader;

	evsel = leader = evlist__first(evlist);
	TEST_ASSERT_VAL("wrong number of entries", 2 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_RAW == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config", 0x3c == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong leader", evsel__has_leader(evsel, leader));

	evsel = evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_RAW == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config", 0xc0 == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong leader", evsel__has_leader(evsel, leader));
	return TEST_OK;
}

static int test__hybrid_sw_hw_group_event(struct evlist *evlist)
{
	struct evsel *evsel, *leader;

	evsel = leader = evlist__first(evlist);
	TEST_ASSERT_VAL("wrong number of entries", 2 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_SOFTWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong leader", evsel__has_leader(evsel, leader));

	evsel = evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_RAW == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config", 0x3c == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong leader", evsel__has_leader(evsel, leader));
	return TEST_OK;
}

static int test__hybrid_hw_sw_group_event(struct evlist *evlist)
{
	struct evsel *evsel, *leader;

	evsel = leader = evlist__first(evlist);
	TEST_ASSERT_VAL("wrong number of entries", 2 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_RAW == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config", 0x3c == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong leader", evsel__has_leader(evsel, leader));

	evsel = evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_SOFTWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong leader", evsel__has_leader(evsel, leader));
	return TEST_OK;
}

static int test__hybrid_group_modifier1(struct evlist *evlist)
{
	struct evsel *evsel, *leader;

	evsel = leader = evlist__first(evlist);
	TEST_ASSERT_VAL("wrong number of entries", 2 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_RAW == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config", 0x3c == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong leader", evsel__has_leader(evsel, leader));
	TEST_ASSERT_VAL("wrong exclude_user", evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel);

	evsel = evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_RAW == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config", 0xc0 == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong leader", evsel__has_leader(evsel, leader));
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->core.attr.exclude_kernel);
	return TEST_OK;
}

static int test__hybrid_raw1(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	if (!perf_pmu__hybrid_mounted("cpu_atom")) {
		TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->core.nr_entries);
		TEST_ASSERT_VAL("wrong type", PERF_TYPE_RAW == evsel->core.attr.type);
		TEST_ASSERT_VAL("wrong config", 0x1a == evsel->core.attr.config);
		return TEST_OK;
	}

	TEST_ASSERT_VAL("wrong number of entries", 2 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_RAW == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config", 0x1a == evsel->core.attr.config);

	/* The type of second event is randome value */
	evsel = evsel__next(evsel);
	TEST_ASSERT_VAL("wrong config", 0x1a == evsel->core.attr.config);
	return TEST_OK;
}

static int test__hybrid_raw2(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_RAW == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config", 0x1a == evsel->core.attr.config);
	return TEST_OK;
}

static int test__hybrid_cache_event(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HW_CACHE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config", 0x2 == (evsel->core.attr.config & 0xffffffff));
	return TEST_OK;
}

struct evlist_test {
	const char *name;
	bool (*valid)(void);
	int (*check)(struct evlist *evlist);
};

static const struct evlist_test test__events[] = {
#ifdef HAVE_LIBTRACEEVENT
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
#endif
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
		.name  = "cycles/period=100000,config2/",
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
#ifdef HAVE_LIBTRACEEVENT
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
#endif
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
#ifdef HAVE_LIBTRACEEVENT
	{
		.name  = "r1,syscalls:sys_enter_openat:k,1:1:hp",
		.check = test__checkevent_list,
		/* 3 */
	},
#endif
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
		.name  = "{instructions:k,cycles:upp}",
		.check = test__group1,
		/* 8 */
	},
	{
		.name  = "{faults:k,cache-references}:u,cycles:k",
		.check = test__group2,
		/* 9 */
	},
#ifdef HAVE_LIBTRACEEVENT
	{
		.name  = "group1{syscalls:sys_enter_openat:H,cycles:kppp},group2{cycles,1:3}:G,instructions:u",
		.check = test__group3,
		/* 0 */
	},
#endif
	{
		.name  = "{cycles:u,instructions:kp}:p",
		.check = test__group4,
		/* 1 */
	},
	{
		.name  = "{cycles,instructions}:G,{cycles:G,instructions:G},cycles",
		.check = test__group5,
		/* 2 */
	},
#ifdef HAVE_LIBTRACEEVENT
	{
		.name  = "*:*",
		.check = test__all_tracepoints,
		/* 3 */
	},
#endif
	{
		.name  = "{cycles,cache-misses:G}:H",
		.check = test__group_gh1,
		/* 4 */
	},
	{
		.name  = "{cycles,cache-misses:H}:G",
		.check = test__group_gh2,
		/* 5 */
	},
	{
		.name  = "{cycles:G,cache-misses:H}:u",
		.check = test__group_gh3,
		/* 6 */
	},
	{
		.name  = "{cycles:G,cache-misses:H}:uG",
		.check = test__group_gh4,
		/* 7 */
	},
	{
		.name  = "{cycles,cache-misses,branch-misses}:S",
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
		.name  = "{cycles,cache-misses,branch-misses}:D",
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
#if defined(__s390x__) && defined(HAVE_LIBTRACEEVENT)
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
		.name  = "task-clock:P,cycles",
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
		.name  = "cycles/name='COMPLEX_CYCLES_NAME:orig=cycles,desc=chip-clock-ticks'/Duk",
		.check = test__checkevent_complex_name,
		/* 3 */
	},
	{
		.name  = "cycles//u",
		.check = test__sym_event_slash,
		/* 4 */
	},
	{
		.name  = "cycles:k",
		.check = test__sym_event_dc,
		/* 5 */
	},
	{
		.name  = "instructions:uep",
		.check = test__checkevent_exclusive_modifier,
		/* 6 */
	},
	{
		.name  = "{cycles,cache-misses,branch-misses}:e",
		.check = test__exclusive_group,
		/* 7 */
	},
};

static const struct evlist_test test__events_pmu[] = {
	{
		.name  = "cpu/config=10,config1,config2=3,period=1000/u",
		.check = test__checkevent_pmu,
		/* 0 */
	},
	{
		.name  = "cpu/config=1,name=krava/u,cpu/config=2/u",
		.check = test__checkevent_pmu_name,
		/* 1 */
	},
	{
		.name  = "cpu/config=1,call-graph=fp,time,period=100000/,cpu/config=2,call-graph=no,time=0,period=2000/",
		.check = test__checkevent_pmu_partial_time_callgraph,
		/* 2 */
	},
	{
		.name  = "cpu/name='COMPLEX_CYCLES_NAME:orig=cycles,desc=chip-clock-ticks',period=0x1,event=0x2/ukp",
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
};

struct terms_test {
	const char *str;
	int (*check)(struct list_head *terms);
};

static const struct terms_test test__terms[] = {
	[0] = {
		.str   = "config=10,config1,config2=3,config3=4,umask=1,read,r0xead",
		.check = test__checkterms_simple,
	},
};

static const struct evlist_test test__hybrid_events[] = {
	{
		.name  = "cpu_core/cpu-cycles/",
		.check = test__hybrid_hw_event_with_pmu,
		/* 0 */
	},
	{
		.name  = "{cpu_core/cpu-cycles/,cpu_core/instructions/}",
		.check = test__hybrid_hw_group_event,
		/* 1 */
	},
	{
		.name  = "{cpu-clock,cpu_core/cpu-cycles/}",
		.check = test__hybrid_sw_hw_group_event,
		/* 2 */
	},
	{
		.name  = "{cpu_core/cpu-cycles/,cpu-clock}",
		.check = test__hybrid_hw_sw_group_event,
		/* 3 */
	},
	{
		.name  = "{cpu_core/cpu-cycles/k,cpu_core/instructions/u}",
		.check = test__hybrid_group_modifier1,
		/* 4 */
	},
	{
		.name  = "r1a",
		.check = test__hybrid_raw1,
		/* 5 */
	},
	{
		.name  = "cpu_core/r1a/",
		.check = test__hybrid_raw2,
		/* 6 */
	},
	{
		.name  = "cpu_core/config=10,config1,config2=3,period=1000/u",
		.check = test__checkevent_pmu,
		/* 7 */
	},
	{
		.name  = "cpu_core/LLC-loads/",
		.check = test__hybrid_cache_event,
		/* 8 */
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
	ret = parse_events(evlist, e->name, &err);
	if (ret) {
		pr_debug("failed to parse event '%s', err %d, str '%s'\n",
			 e->name, ret, err.str);
		parse_events_error__print(&err, e->name);
		ret = TEST_FAIL;
		if (strstr(err.str, "can't access trace events"))
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
	perf_pmu__test_parse_init();
	ret = __parse_events(evlist, str, &err, &perf_pmu__fake, /*warn_if_reordered=*/true);
	if (ret) {
		pr_debug("failed to parse event '%s', err %d, str '%s'\n",
			 str, ret, err.str);
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

	for (int i = 0; i < cnt; i++) {
		const struct evlist_test *e = &events[i];
		int test_ret;

		pr_debug("running test %d '%s'\n", i, e->name);
		test_ret = test_event(e);
		if (test_ret != TEST_OK) {
			pr_debug("Event test failure: test %d '%s'", i, e->name);
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
	struct list_head terms;
	int ret;

	INIT_LIST_HEAD(&terms);

	/*
	 * The perf_pmu__test_parse_init prepares perf_pmu_events_list
	 * which gets freed in parse_events_terms.
	 */
	if (perf_pmu__test_parse_init())
		return -1;

	ret = parse_events_terms(&terms, t->str);
	if (ret) {
		pr_debug("failed to parse terms '%s', err %d\n",
			 t->str , ret);
		return ret;
	}

	ret = t->check(&terms);
	parse_events_terms__purge(&terms);

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

static int test_pmu(void)
{
	struct stat st;
	char path[PATH_MAX];
	int ret;

	snprintf(path, PATH_MAX, "%s/bus/event_source/devices/cpu/format/",
		 sysfs__mountpoint());

	ret = stat(path, &st);
	if (ret)
		pr_debug("omitting PMU cpu tests\n");
	return !ret;
}

static int test__pmu_events(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	struct stat st;
	char path[PATH_MAX];
	struct dirent *ent;
	DIR *dir;
	int ret;

	if (!test_pmu())
		return TEST_SKIP;

	snprintf(path, PATH_MAX, "%s/bus/event_source/devices/cpu/events/",
		 sysfs__mountpoint());

	ret = stat(path, &st);
	if (ret) {
		pr_debug("omitting PMU cpu events tests: %s\n", path);
		return TEST_OK;
	}

	dir = opendir(path);
	if (!dir) {
		pr_debug("can't open pmu event dir: %s\n", path);
		return TEST_FAIL;
	}

	ret = TEST_OK;
	while ((ent = readdir(dir))) {
		struct evlist_test e = { .name = NULL, };
		char name[2 * NAME_MAX + 1 + 12 + 3];
		int test_ret;

		/* Names containing . are special and cannot be used directly */
		if (strchr(ent->d_name, '.'))
			continue;

		snprintf(name, sizeof(name), "cpu/event=%s/u", ent->d_name);

		e.name  = name;
		e.check = test__checkevent_pmu_events;

		test_ret = test_event(&e);
		if (test_ret != TEST_OK) {
			pr_debug("Test PMU event failed for '%s'", name);
			ret = combine_test_results(ret, test_ret);
		}
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

		snprintf(name, sizeof(name), "%s:u,cpu/event=%s/u", ent->d_name, ent->d_name);
		e.name  = name;
		e.check = test__checkevent_pmu_events_mix;
		test_ret = test_event(&e);
		if (test_ret != TEST_OK) {
			pr_debug("Test PMU event failed for '%s'", name);
			ret = combine_test_results(ret, test_ret);
		}
	}

	closedir(dir);
	return ret;
}

static int test__pmu_events2(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	if (!test_pmu())
		return TEST_SKIP;

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

static int test__hybrid(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	if (!perf_pmu__has_hybrid())
		return TEST_SKIP;

	return test_events(test__hybrid_events, ARRAY_SIZE(test__hybrid_events));
}

static int test__checkevent_pmu_events_alias(struct evlist *evlist)
{
	struct evsel *evsel1 = evlist__first(evlist);
	struct evsel *evsel2 = evlist__last(evlist);

	TEST_ASSERT_VAL("wrong type", evsel1->core.attr.type == evsel2->core.attr.type);
	TEST_ASSERT_VAL("wrong config", evsel1->core.attr.config == evsel2->core.attr.config);
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
	TEST_CASE_REASON("Test parsing of \"hybrid\" CPU events",
			 hybrid,
			"not hybrid"),
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
