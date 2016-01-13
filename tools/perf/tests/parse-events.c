
#include "parse-events.h"
#include "evsel.h"
#include "evlist.h"
#include <api/fs/fs.h>
#include "tests.h"
#include "debug.h"
#include "util.h"
#include <linux/hw_breakpoint.h>
#include <api/fs/fs.h>

#define PERF_TP_SAMPLE_TYPE (PERF_SAMPLE_RAW | PERF_SAMPLE_TIME | \
			     PERF_SAMPLE_CPU | PERF_SAMPLE_PERIOD)

static int test__checkevent_tracepoint(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->nr_entries);
	TEST_ASSERT_VAL("wrong number of groups", 0 == evlist->nr_groups);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_TRACEPOINT == evsel->attr.type);
	TEST_ASSERT_VAL("wrong sample_type",
		PERF_TP_SAMPLE_TYPE == evsel->attr.sample_type);
	TEST_ASSERT_VAL("wrong sample_period", 1 == evsel->attr.sample_period);
	return 0;
}

static int test__checkevent_tracepoint_multi(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel;

	TEST_ASSERT_VAL("wrong number of entries", evlist->nr_entries > 1);
	TEST_ASSERT_VAL("wrong number of groups", 0 == evlist->nr_groups);

	evlist__for_each(evlist, evsel) {
		TEST_ASSERT_VAL("wrong type",
			PERF_TYPE_TRACEPOINT == evsel->attr.type);
		TEST_ASSERT_VAL("wrong sample_type",
			PERF_TP_SAMPLE_TYPE == evsel->attr.sample_type);
		TEST_ASSERT_VAL("wrong sample_period",
			1 == evsel->attr.sample_period);
	}
	return 0;
}

static int test__checkevent_raw(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_RAW == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config", 0x1a == evsel->attr.config);
	return 0;
}

static int test__checkevent_numeric(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->nr_entries);
	TEST_ASSERT_VAL("wrong type", 1 == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config", 1 == evsel->attr.config);
	return 0;
}

static int test__checkevent_symbolic_name(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_INSTRUCTIONS == evsel->attr.config);
	return 0;
}

static int test__checkevent_symbolic_name_config(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CPU_CYCLES == evsel->attr.config);
	/*
	 * The period value gets configured within perf_evlist__config,
	 * while this test executes only parse events method.
	 */
	TEST_ASSERT_VAL("wrong period",
			0 == evsel->attr.sample_period);
	TEST_ASSERT_VAL("wrong config1",
			0 == evsel->attr.config1);
	TEST_ASSERT_VAL("wrong config2",
			1 == evsel->attr.config2);
	return 0;
}

static int test__checkevent_symbolic_alias(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_SOFTWARE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_SW_PAGE_FAULTS == evsel->attr.config);
	return 0;
}

static int test__checkevent_genhw(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HW_CACHE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config", (1 << 16) == evsel->attr.config);
	return 0;
}

static int test__checkevent_breakpoint(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_BREAKPOINT == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config", 0 == evsel->attr.config);
	TEST_ASSERT_VAL("wrong bp_type", (HW_BREAKPOINT_R | HW_BREAKPOINT_W) ==
					 evsel->attr.bp_type);
	TEST_ASSERT_VAL("wrong bp_len", HW_BREAKPOINT_LEN_4 ==
					evsel->attr.bp_len);
	return 0;
}

static int test__checkevent_breakpoint_x(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_BREAKPOINT == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config", 0 == evsel->attr.config);
	TEST_ASSERT_VAL("wrong bp_type",
			HW_BREAKPOINT_X == evsel->attr.bp_type);
	TEST_ASSERT_VAL("wrong bp_len", sizeof(long) == evsel->attr.bp_len);
	return 0;
}

static int test__checkevent_breakpoint_r(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->nr_entries);
	TEST_ASSERT_VAL("wrong type",
			PERF_TYPE_BREAKPOINT == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config", 0 == evsel->attr.config);
	TEST_ASSERT_VAL("wrong bp_type",
			HW_BREAKPOINT_R == evsel->attr.bp_type);
	TEST_ASSERT_VAL("wrong bp_len",
			HW_BREAKPOINT_LEN_4 == evsel->attr.bp_len);
	return 0;
}

static int test__checkevent_breakpoint_w(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->nr_entries);
	TEST_ASSERT_VAL("wrong type",
			PERF_TYPE_BREAKPOINT == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config", 0 == evsel->attr.config);
	TEST_ASSERT_VAL("wrong bp_type",
			HW_BREAKPOINT_W == evsel->attr.bp_type);
	TEST_ASSERT_VAL("wrong bp_len",
			HW_BREAKPOINT_LEN_4 == evsel->attr.bp_len);
	return 0;
}

static int test__checkevent_breakpoint_rw(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->nr_entries);
	TEST_ASSERT_VAL("wrong type",
			PERF_TYPE_BREAKPOINT == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config", 0 == evsel->attr.config);
	TEST_ASSERT_VAL("wrong bp_type",
		(HW_BREAKPOINT_R|HW_BREAKPOINT_W) == evsel->attr.bp_type);
	TEST_ASSERT_VAL("wrong bp_len",
			HW_BREAKPOINT_LEN_4 == evsel->attr.bp_len);
	return 0;
}

static int test__checkevent_tracepoint_modifier(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evlist__first(evlist);

	TEST_ASSERT_VAL("wrong exclude_user", evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);

	return test__checkevent_tracepoint(evlist);
}

static int
test__checkevent_tracepoint_multi_modifier(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel;

	TEST_ASSERT_VAL("wrong number of entries", evlist->nr_entries > 1);

	evlist__for_each(evlist, evsel) {
		TEST_ASSERT_VAL("wrong exclude_user",
				!evsel->attr.exclude_user);
		TEST_ASSERT_VAL("wrong exclude_kernel",
				evsel->attr.exclude_kernel);
		TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
		TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);
	}

	return test__checkevent_tracepoint_multi(evlist);
}

static int test__checkevent_raw_modifier(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evlist__first(evlist);

	TEST_ASSERT_VAL("wrong exclude_user", evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", evsel->attr.precise_ip);

	return test__checkevent_raw(evlist);
}

static int test__checkevent_numeric_modifier(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evlist__first(evlist);

	TEST_ASSERT_VAL("wrong exclude_user", evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", evsel->attr.precise_ip);

	return test__checkevent_numeric(evlist);
}

static int test__checkevent_symbolic_name_modifier(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evlist__first(evlist);

	TEST_ASSERT_VAL("wrong exclude_user", evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);

	return test__checkevent_symbolic_name(evlist);
}

static int test__checkevent_exclude_host_modifier(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evlist__first(evlist);

	TEST_ASSERT_VAL("wrong exclude guest", !evsel->attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", evsel->attr.exclude_host);

	return test__checkevent_symbolic_name(evlist);
}

static int test__checkevent_exclude_guest_modifier(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evlist__first(evlist);

	TEST_ASSERT_VAL("wrong exclude guest", evsel->attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->attr.exclude_host);

	return test__checkevent_symbolic_name(evlist);
}

static int test__checkevent_symbolic_alias_modifier(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evlist__first(evlist);

	TEST_ASSERT_VAL("wrong exclude_user", !evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);

	return test__checkevent_symbolic_alias(evlist);
}

static int test__checkevent_genhw_modifier(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evlist__first(evlist);

	TEST_ASSERT_VAL("wrong exclude_user", evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", evsel->attr.precise_ip);

	return test__checkevent_genhw(evlist);
}

static int test__checkevent_exclude_idle_modifier(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evlist__first(evlist);

	TEST_ASSERT_VAL("wrong exclude idle", evsel->attr.exclude_idle);
	TEST_ASSERT_VAL("wrong exclude guest", !evsel->attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->attr.exclude_host);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);

	return test__checkevent_symbolic_name(evlist);
}

static int test__checkevent_exclude_idle_modifier_1(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evlist__first(evlist);

	TEST_ASSERT_VAL("wrong exclude idle", evsel->attr.exclude_idle);
	TEST_ASSERT_VAL("wrong exclude guest", !evsel->attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", evsel->attr.exclude_host);
	TEST_ASSERT_VAL("wrong exclude_user", evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);

	return test__checkevent_symbolic_name(evlist);
}

static int test__checkevent_breakpoint_modifier(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evlist__first(evlist);


	TEST_ASSERT_VAL("wrong exclude_user", !evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);
	TEST_ASSERT_VAL("wrong name",
			!strcmp(perf_evsel__name(evsel), "mem:0:u"));

	return test__checkevent_breakpoint(evlist);
}

static int test__checkevent_breakpoint_x_modifier(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evlist__first(evlist);

	TEST_ASSERT_VAL("wrong exclude_user", evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);
	TEST_ASSERT_VAL("wrong name",
			!strcmp(perf_evsel__name(evsel), "mem:0:x:k"));

	return test__checkevent_breakpoint_x(evlist);
}

static int test__checkevent_breakpoint_r_modifier(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evlist__first(evlist);

	TEST_ASSERT_VAL("wrong exclude_user", evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", evsel->attr.precise_ip);
	TEST_ASSERT_VAL("wrong name",
			!strcmp(perf_evsel__name(evsel), "mem:0:r:hp"));

	return test__checkevent_breakpoint_r(evlist);
}

static int test__checkevent_breakpoint_w_modifier(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evlist__first(evlist);

	TEST_ASSERT_VAL("wrong exclude_user", !evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", evsel->attr.precise_ip);
	TEST_ASSERT_VAL("wrong name",
			!strcmp(perf_evsel__name(evsel), "mem:0:w:up"));

	return test__checkevent_breakpoint_w(evlist);
}

static int test__checkevent_breakpoint_rw_modifier(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evlist__first(evlist);

	TEST_ASSERT_VAL("wrong exclude_user", evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", evsel->attr.precise_ip);
	TEST_ASSERT_VAL("wrong name",
			!strcmp(perf_evsel__name(evsel), "mem:0:rw:kp"));

	return test__checkevent_breakpoint_rw(evlist);
}

static int test__checkevent_pmu(struct perf_evlist *evlist)
{

	struct perf_evsel *evsel = perf_evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_RAW == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",    10 == evsel->attr.config);
	TEST_ASSERT_VAL("wrong config1",    1 == evsel->attr.config1);
	TEST_ASSERT_VAL("wrong config2",    3 == evsel->attr.config2);
	/*
	 * The period value gets configured within perf_evlist__config,
	 * while this test executes only parse events method.
	 */
	TEST_ASSERT_VAL("wrong period",     0 == evsel->attr.sample_period);

	return 0;
}

static int test__checkevent_list(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 3 == evlist->nr_entries);

	/* r1 */
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_RAW == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config", 1 == evsel->attr.config);
	TEST_ASSERT_VAL("wrong config1", 0 == evsel->attr.config1);
	TEST_ASSERT_VAL("wrong config2", 0 == evsel->attr.config2);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);

	/* syscalls:sys_enter_openat:k */
	evsel = perf_evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_TRACEPOINT == evsel->attr.type);
	TEST_ASSERT_VAL("wrong sample_type",
		PERF_TP_SAMPLE_TYPE == evsel->attr.sample_type);
	TEST_ASSERT_VAL("wrong sample_period", 1 == evsel->attr.sample_period);
	TEST_ASSERT_VAL("wrong exclude_user", evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);

	/* 1:1:hp */
	evsel = perf_evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", 1 == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config", 1 == evsel->attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", evsel->attr.precise_ip);

	return 0;
}

static int test__checkevent_pmu_name(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evlist__first(evlist);

	/* cpu/config=1,name=krava/u */
	TEST_ASSERT_VAL("wrong number of entries", 2 == evlist->nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_RAW == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",  1 == evsel->attr.config);
	TEST_ASSERT_VAL("wrong name", !strcmp(perf_evsel__name(evsel), "krava"));

	/* cpu/config=2/u" */
	evsel = perf_evsel__next(evsel);
	TEST_ASSERT_VAL("wrong number of entries", 2 == evlist->nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_RAW == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",  2 == evsel->attr.config);
	TEST_ASSERT_VAL("wrong name",
			!strcmp(perf_evsel__name(evsel), "cpu/config=2/u"));

	return 0;
}

static int test__checkevent_pmu_partial_time_callgraph(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evlist__first(evlist);

	/* cpu/config=1,call-graph=fp,time,period=100000/ */
	TEST_ASSERT_VAL("wrong number of entries", 2 == evlist->nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_RAW == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",  1 == evsel->attr.config);
	/*
	 * The period, time and callgraph value gets configured
	 * within perf_evlist__config,
	 * while this test executes only parse events method.
	 */
	TEST_ASSERT_VAL("wrong period",     0 == evsel->attr.sample_period);
	TEST_ASSERT_VAL("wrong callgraph",  !(PERF_SAMPLE_CALLCHAIN & evsel->attr.sample_type));
	TEST_ASSERT_VAL("wrong time",  !(PERF_SAMPLE_TIME & evsel->attr.sample_type));

	/* cpu/config=2,call-graph=no,time=0,period=2000/ */
	evsel = perf_evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_RAW == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",  2 == evsel->attr.config);
	/*
	 * The period, time and callgraph value gets configured
	 * within perf_evlist__config,
	 * while this test executes only parse events method.
	 */
	TEST_ASSERT_VAL("wrong period",     0 == evsel->attr.sample_period);
	TEST_ASSERT_VAL("wrong callgraph",  !(PERF_SAMPLE_CALLCHAIN & evsel->attr.sample_type));
	TEST_ASSERT_VAL("wrong time",  !(PERF_SAMPLE_TIME & evsel->attr.sample_type));

	return 0;
}

static int test__checkevent_pmu_events(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_RAW == evsel->attr.type);
	TEST_ASSERT_VAL("wrong exclude_user",
			!evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel",
			evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);
	TEST_ASSERT_VAL("wrong pinned", !evsel->attr.pinned);

	return 0;
}


static int test__checkevent_pmu_events_mix(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evlist__first(evlist);

	/* pmu-event:u */
	TEST_ASSERT_VAL("wrong number of entries", 2 == evlist->nr_entries);
	TEST_ASSERT_VAL("wrong exclude_user",
			!evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel",
			evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);
	TEST_ASSERT_VAL("wrong pinned", !evsel->attr.pinned);

	/* cpu/pmu-event/u*/
	evsel = perf_evsel__next(evsel);
	TEST_ASSERT_VAL("wrong number of entries", 2 == evlist->nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_RAW == evsel->attr.type);
	TEST_ASSERT_VAL("wrong exclude_user",
			!evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel",
			evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);
	TEST_ASSERT_VAL("wrong pinned", !evsel->attr.pinned);

	return 0;
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
	TEST_ASSERT_VAL("wrong config", !term->config);

	/* config1 */
	term = list_entry(term->list.next, struct parse_events_term, list);
	TEST_ASSERT_VAL("wrong type term",
			term->type_term == PARSE_EVENTS__TERM_TYPE_CONFIG1);
	TEST_ASSERT_VAL("wrong type val",
			term->type_val == PARSE_EVENTS__TERM_TYPE_NUM);
	TEST_ASSERT_VAL("wrong val", term->val.num == 1);
	TEST_ASSERT_VAL("wrong config", !term->config);

	/* config2=3 */
	term = list_entry(term->list.next, struct parse_events_term, list);
	TEST_ASSERT_VAL("wrong type term",
			term->type_term == PARSE_EVENTS__TERM_TYPE_CONFIG2);
	TEST_ASSERT_VAL("wrong type val",
			term->type_val == PARSE_EVENTS__TERM_TYPE_NUM);
	TEST_ASSERT_VAL("wrong val", term->val.num == 3);
	TEST_ASSERT_VAL("wrong config", !term->config);

	/* umask=1*/
	term = list_entry(term->list.next, struct parse_events_term, list);
	TEST_ASSERT_VAL("wrong type term",
			term->type_term == PARSE_EVENTS__TERM_TYPE_USER);
	TEST_ASSERT_VAL("wrong type val",
			term->type_val == PARSE_EVENTS__TERM_TYPE_NUM);
	TEST_ASSERT_VAL("wrong val", term->val.num == 1);
	TEST_ASSERT_VAL("wrong config", !strcmp(term->config, "umask"));

	return 0;
}

static int test__group1(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel, *leader;

	TEST_ASSERT_VAL("wrong number of entries", 2 == evlist->nr_entries);
	TEST_ASSERT_VAL("wrong number of groups", 1 == evlist->nr_groups);

	/* instructions:k */
	evsel = leader = perf_evlist__first(evlist);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_INSTRUCTIONS == evsel->attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", !evsel->attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);
	TEST_ASSERT_VAL("wrong leader", perf_evsel__is_group_leader(evsel));
	TEST_ASSERT_VAL("wrong nr_members", evsel->nr_members == 2);
	TEST_ASSERT_VAL("wrong group_idx", perf_evsel__group_idx(evsel) == 0);
	TEST_ASSERT_VAL("wrong sample_read", !evsel->sample_read);

	/* cycles:upp */
	evsel = perf_evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CPU_CYCLES == evsel->attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
	/* use of precise requires exclude_guest */
	TEST_ASSERT_VAL("wrong exclude guest", evsel->attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", evsel->attr.precise_ip == 2);
	TEST_ASSERT_VAL("wrong leader", evsel->leader == leader);
	TEST_ASSERT_VAL("wrong group_idx", perf_evsel__group_idx(evsel) == 1);
	TEST_ASSERT_VAL("wrong sample_read", !evsel->sample_read);

	return 0;
}

static int test__group2(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel, *leader;

	TEST_ASSERT_VAL("wrong number of entries", 3 == evlist->nr_entries);
	TEST_ASSERT_VAL("wrong number of groups", 1 == evlist->nr_groups);

	/* faults + :ku modifier */
	evsel = leader = perf_evlist__first(evlist);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_SOFTWARE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_SW_PAGE_FAULTS == evsel->attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", !evsel->attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);
	TEST_ASSERT_VAL("wrong leader", perf_evsel__is_group_leader(evsel));
	TEST_ASSERT_VAL("wrong nr_members", evsel->nr_members == 2);
	TEST_ASSERT_VAL("wrong group_idx", perf_evsel__group_idx(evsel) == 0);
	TEST_ASSERT_VAL("wrong sample_read", !evsel->sample_read);

	/* cache-references + :u modifier */
	evsel = perf_evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CACHE_REFERENCES == evsel->attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", evsel->attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);
	TEST_ASSERT_VAL("wrong leader", evsel->leader == leader);
	TEST_ASSERT_VAL("wrong group_idx", perf_evsel__group_idx(evsel) == 1);
	TEST_ASSERT_VAL("wrong sample_read", !evsel->sample_read);

	/* cycles:k */
	evsel = perf_evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CPU_CYCLES == evsel->attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", !evsel->attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);
	TEST_ASSERT_VAL("wrong leader", perf_evsel__is_group_leader(evsel));
	TEST_ASSERT_VAL("wrong sample_read", !evsel->sample_read);

	return 0;
}

static int test__group3(struct perf_evlist *evlist __maybe_unused)
{
	struct perf_evsel *evsel, *leader;

	TEST_ASSERT_VAL("wrong number of entries", 5 == evlist->nr_entries);
	TEST_ASSERT_VAL("wrong number of groups", 2 == evlist->nr_groups);

	/* group1 syscalls:sys_enter_openat:H */
	evsel = leader = perf_evlist__first(evlist);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_TRACEPOINT == evsel->attr.type);
	TEST_ASSERT_VAL("wrong sample_type",
		PERF_TP_SAMPLE_TYPE == evsel->attr.sample_type);
	TEST_ASSERT_VAL("wrong sample_period", 1 == evsel->attr.sample_period);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", evsel->attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);
	TEST_ASSERT_VAL("wrong leader", perf_evsel__is_group_leader(evsel));
	TEST_ASSERT_VAL("wrong group name",
		!strcmp(leader->group_name, "group1"));
	TEST_ASSERT_VAL("wrong nr_members", evsel->nr_members == 2);
	TEST_ASSERT_VAL("wrong group_idx", perf_evsel__group_idx(evsel) == 0);
	TEST_ASSERT_VAL("wrong sample_read", !evsel->sample_read);

	/* group1 cycles:kppp */
	evsel = perf_evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CPU_CYCLES == evsel->attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
	/* use of precise requires exclude_guest */
	TEST_ASSERT_VAL("wrong exclude guest", evsel->attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", evsel->attr.precise_ip == 3);
	TEST_ASSERT_VAL("wrong leader", evsel->leader == leader);
	TEST_ASSERT_VAL("wrong group name", !evsel->group_name);
	TEST_ASSERT_VAL("wrong group_idx", perf_evsel__group_idx(evsel) == 1);
	TEST_ASSERT_VAL("wrong sample_read", !evsel->sample_read);

	/* group2 cycles + G modifier */
	evsel = leader = perf_evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CPU_CYCLES == evsel->attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", !evsel->attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", evsel->attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);
	TEST_ASSERT_VAL("wrong leader", perf_evsel__is_group_leader(evsel));
	TEST_ASSERT_VAL("wrong group name",
		!strcmp(leader->group_name, "group2"));
	TEST_ASSERT_VAL("wrong nr_members", evsel->nr_members == 2);
	TEST_ASSERT_VAL("wrong group_idx", perf_evsel__group_idx(evsel) == 0);
	TEST_ASSERT_VAL("wrong sample_read", !evsel->sample_read);

	/* group2 1:3 + G modifier */
	evsel = perf_evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", 1 == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config", 3 == evsel->attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", !evsel->attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", evsel->attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);
	TEST_ASSERT_VAL("wrong leader", evsel->leader == leader);
	TEST_ASSERT_VAL("wrong group_idx", perf_evsel__group_idx(evsel) == 1);
	TEST_ASSERT_VAL("wrong sample_read", !evsel->sample_read);

	/* instructions:u */
	evsel = perf_evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_INSTRUCTIONS == evsel->attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", !evsel->attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);
	TEST_ASSERT_VAL("wrong leader", perf_evsel__is_group_leader(evsel));
	TEST_ASSERT_VAL("wrong sample_read", !evsel->sample_read);

	return 0;
}

static int test__group4(struct perf_evlist *evlist __maybe_unused)
{
	struct perf_evsel *evsel, *leader;

	TEST_ASSERT_VAL("wrong number of entries", 2 == evlist->nr_entries);
	TEST_ASSERT_VAL("wrong number of groups", 1 == evlist->nr_groups);

	/* cycles:u + p */
	evsel = leader = perf_evlist__first(evlist);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CPU_CYCLES == evsel->attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
	/* use of precise requires exclude_guest */
	TEST_ASSERT_VAL("wrong exclude guest", evsel->attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", evsel->attr.precise_ip == 1);
	TEST_ASSERT_VAL("wrong group name", !evsel->group_name);
	TEST_ASSERT_VAL("wrong leader", perf_evsel__is_group_leader(evsel));
	TEST_ASSERT_VAL("wrong nr_members", evsel->nr_members == 2);
	TEST_ASSERT_VAL("wrong group_idx", perf_evsel__group_idx(evsel) == 0);
	TEST_ASSERT_VAL("wrong sample_read", !evsel->sample_read);

	/* instructions:kp + p */
	evsel = perf_evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_INSTRUCTIONS == evsel->attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
	/* use of precise requires exclude_guest */
	TEST_ASSERT_VAL("wrong exclude guest", evsel->attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", evsel->attr.precise_ip == 2);
	TEST_ASSERT_VAL("wrong leader", evsel->leader == leader);
	TEST_ASSERT_VAL("wrong group_idx", perf_evsel__group_idx(evsel) == 1);
	TEST_ASSERT_VAL("wrong sample_read", !evsel->sample_read);

	return 0;
}

static int test__group5(struct perf_evlist *evlist __maybe_unused)
{
	struct perf_evsel *evsel, *leader;

	TEST_ASSERT_VAL("wrong number of entries", 5 == evlist->nr_entries);
	TEST_ASSERT_VAL("wrong number of groups", 2 == evlist->nr_groups);

	/* cycles + G */
	evsel = leader = perf_evlist__first(evlist);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CPU_CYCLES == evsel->attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", !evsel->attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", evsel->attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);
	TEST_ASSERT_VAL("wrong group name", !evsel->group_name);
	TEST_ASSERT_VAL("wrong leader", perf_evsel__is_group_leader(evsel));
	TEST_ASSERT_VAL("wrong nr_members", evsel->nr_members == 2);
	TEST_ASSERT_VAL("wrong group_idx", perf_evsel__group_idx(evsel) == 0);
	TEST_ASSERT_VAL("wrong sample_read", !evsel->sample_read);

	/* instructions + G */
	evsel = perf_evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_INSTRUCTIONS == evsel->attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", !evsel->attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", evsel->attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);
	TEST_ASSERT_VAL("wrong leader", evsel->leader == leader);
	TEST_ASSERT_VAL("wrong group_idx", perf_evsel__group_idx(evsel) == 1);
	TEST_ASSERT_VAL("wrong sample_read", !evsel->sample_read);

	/* cycles:G */
	evsel = leader = perf_evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CPU_CYCLES == evsel->attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", !evsel->attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", evsel->attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);
	TEST_ASSERT_VAL("wrong group name", !evsel->group_name);
	TEST_ASSERT_VAL("wrong leader", perf_evsel__is_group_leader(evsel));
	TEST_ASSERT_VAL("wrong nr_members", evsel->nr_members == 2);
	TEST_ASSERT_VAL("wrong group_idx", perf_evsel__group_idx(evsel) == 0);
	TEST_ASSERT_VAL("wrong sample_read", !evsel->sample_read);

	/* instructions:G */
	evsel = perf_evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_INSTRUCTIONS == evsel->attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", !evsel->attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", evsel->attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);
	TEST_ASSERT_VAL("wrong leader", evsel->leader == leader);
	TEST_ASSERT_VAL("wrong group_idx", perf_evsel__group_idx(evsel) == 1);

	/* cycles */
	evsel = perf_evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CPU_CYCLES == evsel->attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", evsel->attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);
	TEST_ASSERT_VAL("wrong leader", perf_evsel__is_group_leader(evsel));

	return 0;
}

static int test__group_gh1(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel, *leader;

	TEST_ASSERT_VAL("wrong number of entries", 2 == evlist->nr_entries);
	TEST_ASSERT_VAL("wrong number of groups", 1 == evlist->nr_groups);

	/* cycles + :H group modifier */
	evsel = leader = perf_evlist__first(evlist);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CPU_CYCLES == evsel->attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", evsel->attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);
	TEST_ASSERT_VAL("wrong group name", !evsel->group_name);
	TEST_ASSERT_VAL("wrong leader", perf_evsel__is_group_leader(evsel));
	TEST_ASSERT_VAL("wrong nr_members", evsel->nr_members == 2);
	TEST_ASSERT_VAL("wrong group_idx", perf_evsel__group_idx(evsel) == 0);

	/* cache-misses:G + :H group modifier */
	evsel = perf_evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CACHE_MISSES == evsel->attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", !evsel->attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);
	TEST_ASSERT_VAL("wrong leader", evsel->leader == leader);
	TEST_ASSERT_VAL("wrong group_idx", perf_evsel__group_idx(evsel) == 1);

	return 0;
}

static int test__group_gh2(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel, *leader;

	TEST_ASSERT_VAL("wrong number of entries", 2 == evlist->nr_entries);
	TEST_ASSERT_VAL("wrong number of groups", 1 == evlist->nr_groups);

	/* cycles + :G group modifier */
	evsel = leader = perf_evlist__first(evlist);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CPU_CYCLES == evsel->attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", !evsel->attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", evsel->attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);
	TEST_ASSERT_VAL("wrong group name", !evsel->group_name);
	TEST_ASSERT_VAL("wrong leader", perf_evsel__is_group_leader(evsel));
	TEST_ASSERT_VAL("wrong nr_members", evsel->nr_members == 2);
	TEST_ASSERT_VAL("wrong group_idx", perf_evsel__group_idx(evsel) == 0);

	/* cache-misses:H + :G group modifier */
	evsel = perf_evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CACHE_MISSES == evsel->attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", !evsel->attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);
	TEST_ASSERT_VAL("wrong leader", evsel->leader == leader);
	TEST_ASSERT_VAL("wrong group_idx", perf_evsel__group_idx(evsel) == 1);

	return 0;
}

static int test__group_gh3(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel, *leader;

	TEST_ASSERT_VAL("wrong number of entries", 2 == evlist->nr_entries);
	TEST_ASSERT_VAL("wrong number of groups", 1 == evlist->nr_groups);

	/* cycles:G + :u group modifier */
	evsel = leader = perf_evlist__first(evlist);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CPU_CYCLES == evsel->attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", !evsel->attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", evsel->attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);
	TEST_ASSERT_VAL("wrong group name", !evsel->group_name);
	TEST_ASSERT_VAL("wrong leader", perf_evsel__is_group_leader(evsel));
	TEST_ASSERT_VAL("wrong nr_members", evsel->nr_members == 2);
	TEST_ASSERT_VAL("wrong group_idx", perf_evsel__group_idx(evsel) == 0);

	/* cache-misses:H + :u group modifier */
	evsel = perf_evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CACHE_MISSES == evsel->attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", evsel->attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);
	TEST_ASSERT_VAL("wrong leader", evsel->leader == leader);
	TEST_ASSERT_VAL("wrong group_idx", perf_evsel__group_idx(evsel) == 1);

	return 0;
}

static int test__group_gh4(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel, *leader;

	TEST_ASSERT_VAL("wrong number of entries", 2 == evlist->nr_entries);
	TEST_ASSERT_VAL("wrong number of groups", 1 == evlist->nr_groups);

	/* cycles:G + :uG group modifier */
	evsel = leader = perf_evlist__first(evlist);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CPU_CYCLES == evsel->attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", !evsel->attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", evsel->attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);
	TEST_ASSERT_VAL("wrong group name", !evsel->group_name);
	TEST_ASSERT_VAL("wrong leader", perf_evsel__is_group_leader(evsel));
	TEST_ASSERT_VAL("wrong nr_members", evsel->nr_members == 2);
	TEST_ASSERT_VAL("wrong group_idx", perf_evsel__group_idx(evsel) == 0);

	/* cache-misses:H + :uG group modifier */
	evsel = perf_evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CACHE_MISSES == evsel->attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", !evsel->attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);
	TEST_ASSERT_VAL("wrong leader", evsel->leader == leader);
	TEST_ASSERT_VAL("wrong group_idx", perf_evsel__group_idx(evsel) == 1);

	return 0;
}

static int test__leader_sample1(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel, *leader;

	TEST_ASSERT_VAL("wrong number of entries", 3 == evlist->nr_entries);

	/* cycles - sampling group leader */
	evsel = leader = perf_evlist__first(evlist);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CPU_CYCLES == evsel->attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", evsel->attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);
	TEST_ASSERT_VAL("wrong group name", !evsel->group_name);
	TEST_ASSERT_VAL("wrong leader", evsel->leader == leader);
	TEST_ASSERT_VAL("wrong sample_read", evsel->sample_read);

	/* cache-misses - not sampling */
	evsel = perf_evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CACHE_MISSES == evsel->attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", evsel->attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);
	TEST_ASSERT_VAL("wrong leader", evsel->leader == leader);
	TEST_ASSERT_VAL("wrong sample_read", evsel->sample_read);

	/* branch-misses - not sampling */
	evsel = perf_evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_BRANCH_MISSES == evsel->attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", evsel->attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);
	TEST_ASSERT_VAL("wrong group name", !evsel->group_name);
	TEST_ASSERT_VAL("wrong leader", evsel->leader == leader);
	TEST_ASSERT_VAL("wrong sample_read", evsel->sample_read);

	return 0;
}

static int test__leader_sample2(struct perf_evlist *evlist __maybe_unused)
{
	struct perf_evsel *evsel, *leader;

	TEST_ASSERT_VAL("wrong number of entries", 2 == evlist->nr_entries);

	/* instructions - sampling group leader */
	evsel = leader = perf_evlist__first(evlist);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_INSTRUCTIONS == evsel->attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", evsel->attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);
	TEST_ASSERT_VAL("wrong group name", !evsel->group_name);
	TEST_ASSERT_VAL("wrong leader", evsel->leader == leader);
	TEST_ASSERT_VAL("wrong sample_read", evsel->sample_read);

	/* branch-misses - not sampling */
	evsel = perf_evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_BRANCH_MISSES == evsel->attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong exclude guest", evsel->attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->attr.exclude_host);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);
	TEST_ASSERT_VAL("wrong group name", !evsel->group_name);
	TEST_ASSERT_VAL("wrong leader", evsel->leader == leader);
	TEST_ASSERT_VAL("wrong sample_read", evsel->sample_read);

	return 0;
}

static int test__checkevent_pinned_modifier(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evlist__first(evlist);

	TEST_ASSERT_VAL("wrong exclude_user", !evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", evsel->attr.precise_ip);
	TEST_ASSERT_VAL("wrong pinned", evsel->attr.pinned);

	return test__checkevent_symbolic_name(evlist);
}

static int test__pinned_group(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel, *leader;

	TEST_ASSERT_VAL("wrong number of entries", 3 == evlist->nr_entries);

	/* cycles - group leader */
	evsel = leader = perf_evlist__first(evlist);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CPU_CYCLES == evsel->attr.config);
	TEST_ASSERT_VAL("wrong group name", !evsel->group_name);
	TEST_ASSERT_VAL("wrong leader", evsel->leader == leader);
	TEST_ASSERT_VAL("wrong pinned", evsel->attr.pinned);

	/* cache-misses - can not be pinned, but will go on with the leader */
	evsel = perf_evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CACHE_MISSES == evsel->attr.config);
	TEST_ASSERT_VAL("wrong pinned", !evsel->attr.pinned);

	/* branch-misses - ditto */
	evsel = perf_evsel__next(evsel);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_BRANCH_MISSES == evsel->attr.config);
	TEST_ASSERT_VAL("wrong pinned", !evsel->attr.pinned);

	return 0;
}

static int test__checkevent_breakpoint_len(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_BREAKPOINT == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config", 0 == evsel->attr.config);
	TEST_ASSERT_VAL("wrong bp_type", (HW_BREAKPOINT_R | HW_BREAKPOINT_W) ==
					 evsel->attr.bp_type);
	TEST_ASSERT_VAL("wrong bp_len", HW_BREAKPOINT_LEN_1 ==
					evsel->attr.bp_len);

	return 0;
}

static int test__checkevent_breakpoint_len_w(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_BREAKPOINT == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config", 0 == evsel->attr.config);
	TEST_ASSERT_VAL("wrong bp_type", HW_BREAKPOINT_W ==
					 evsel->attr.bp_type);
	TEST_ASSERT_VAL("wrong bp_len", HW_BREAKPOINT_LEN_2 ==
					evsel->attr.bp_len);

	return 0;
}

static int
test__checkevent_breakpoint_len_rw_modifier(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evlist__first(evlist);

	TEST_ASSERT_VAL("wrong exclude_user", !evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);

	return test__checkevent_breakpoint_rw(evlist);
}

static int test__checkevent_precise_max_modifier(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = perf_evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 2 == evlist->nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_SOFTWARE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_SW_TASK_CLOCK == evsel->attr.config);
	return 0;
}

static int count_tracepoints(void)
{
	struct dirent *events_ent;
	DIR *events_dir;
	int cnt = 0;

	events_dir = opendir(tracing_events_path);

	TEST_ASSERT_VAL("Can't open events dir", events_dir);

	while ((events_ent = readdir(events_dir))) {
		char sys_path[PATH_MAX];
		struct dirent *sys_ent;
		DIR *sys_dir;

		if (!strcmp(events_ent->d_name, ".")
		    || !strcmp(events_ent->d_name, "..")
		    || !strcmp(events_ent->d_name, "enable")
		    || !strcmp(events_ent->d_name, "header_event")
		    || !strcmp(events_ent->d_name, "header_page"))
			continue;

		scnprintf(sys_path, PATH_MAX, "%s/%s",
			  tracing_events_path, events_ent->d_name);

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
	}

	closedir(events_dir);
	return cnt;
}

static int test__all_tracepoints(struct perf_evlist *evlist)
{
	TEST_ASSERT_VAL("wrong events count",
			count_tracepoints() == evlist->nr_entries);

	return test__checkevent_tracepoint_multi(evlist);
}

struct evlist_test {
	const char *name;
	__u32 type;
	const int id;
	int (*check)(struct perf_evlist *evlist);
};

static struct evlist_test test__events[] = {
	{
		.name  = "syscalls:sys_enter_openat",
		.check = test__checkevent_tracepoint,
		.id    = 0,
	},
	{
		.name  = "syscalls:*",
		.check = test__checkevent_tracepoint_multi,
		.id    = 1,
	},
	{
		.name  = "r1a",
		.check = test__checkevent_raw,
		.id    = 2,
	},
	{
		.name  = "1:1",
		.check = test__checkevent_numeric,
		.id    = 3,
	},
	{
		.name  = "instructions",
		.check = test__checkevent_symbolic_name,
		.id    = 4,
	},
	{
		.name  = "cycles/period=100000,config2/",
		.check = test__checkevent_symbolic_name_config,
		.id    = 5,
	},
	{
		.name  = "faults",
		.check = test__checkevent_symbolic_alias,
		.id    = 6,
	},
	{
		.name  = "L1-dcache-load-miss",
		.check = test__checkevent_genhw,
		.id    = 7,
	},
	{
		.name  = "mem:0",
		.check = test__checkevent_breakpoint,
		.id    = 8,
	},
	{
		.name  = "mem:0:x",
		.check = test__checkevent_breakpoint_x,
		.id    = 9,
	},
	{
		.name  = "mem:0:r",
		.check = test__checkevent_breakpoint_r,
		.id    = 10,
	},
	{
		.name  = "mem:0:w",
		.check = test__checkevent_breakpoint_w,
		.id    = 11,
	},
	{
		.name  = "syscalls:sys_enter_openat:k",
		.check = test__checkevent_tracepoint_modifier,
		.id    = 12,
	},
	{
		.name  = "syscalls:*:u",
		.check = test__checkevent_tracepoint_multi_modifier,
		.id    = 13,
	},
	{
		.name  = "r1a:kp",
		.check = test__checkevent_raw_modifier,
		.id    = 14,
	},
	{
		.name  = "1:1:hp",
		.check = test__checkevent_numeric_modifier,
		.id    = 15,
	},
	{
		.name  = "instructions:h",
		.check = test__checkevent_symbolic_name_modifier,
		.id    = 16,
	},
	{
		.name  = "faults:u",
		.check = test__checkevent_symbolic_alias_modifier,
		.id    = 17,
	},
	{
		.name  = "L1-dcache-load-miss:kp",
		.check = test__checkevent_genhw_modifier,
		.id    = 18,
	},
	{
		.name  = "mem:0:u",
		.check = test__checkevent_breakpoint_modifier,
		.id    = 19,
	},
	{
		.name  = "mem:0:x:k",
		.check = test__checkevent_breakpoint_x_modifier,
		.id    = 20,
	},
	{
		.name  = "mem:0:r:hp",
		.check = test__checkevent_breakpoint_r_modifier,
		.id    = 21,
	},
	{
		.name  = "mem:0:w:up",
		.check = test__checkevent_breakpoint_w_modifier,
		.id    = 22,
	},
	{
		.name  = "r1,syscalls:sys_enter_openat:k,1:1:hp",
		.check = test__checkevent_list,
		.id    = 23,
	},
	{
		.name  = "instructions:G",
		.check = test__checkevent_exclude_host_modifier,
		.id    = 24,
	},
	{
		.name  = "instructions:H",
		.check = test__checkevent_exclude_guest_modifier,
		.id    = 25,
	},
	{
		.name  = "mem:0:rw",
		.check = test__checkevent_breakpoint_rw,
		.id    = 26,
	},
	{
		.name  = "mem:0:rw:kp",
		.check = test__checkevent_breakpoint_rw_modifier,
		.id    = 27,
	},
	{
		.name  = "{instructions:k,cycles:upp}",
		.check = test__group1,
		.id    = 28,
	},
	{
		.name  = "{faults:k,cache-references}:u,cycles:k",
		.check = test__group2,
		.id    = 29,
	},
	{
		.name  = "group1{syscalls:sys_enter_openat:H,cycles:kppp},group2{cycles,1:3}:G,instructions:u",
		.check = test__group3,
		.id    = 30,
	},
	{
		.name  = "{cycles:u,instructions:kp}:p",
		.check = test__group4,
		.id    = 31,
	},
	{
		.name  = "{cycles,instructions}:G,{cycles:G,instructions:G},cycles",
		.check = test__group5,
		.id    = 32,
	},
	{
		.name  = "*:*",
		.check = test__all_tracepoints,
		.id    = 33,
	},
	{
		.name  = "{cycles,cache-misses:G}:H",
		.check = test__group_gh1,
		.id    = 34,
	},
	{
		.name  = "{cycles,cache-misses:H}:G",
		.check = test__group_gh2,
		.id    = 35,
	},
	{
		.name  = "{cycles:G,cache-misses:H}:u",
		.check = test__group_gh3,
		.id    = 36,
	},
	{
		.name  = "{cycles:G,cache-misses:H}:uG",
		.check = test__group_gh4,
		.id    = 37,
	},
	{
		.name  = "{cycles,cache-misses,branch-misses}:S",
		.check = test__leader_sample1,
		.id    = 38,
	},
	{
		.name  = "{instructions,branch-misses}:Su",
		.check = test__leader_sample2,
		.id    = 39,
	},
	{
		.name  = "instructions:uDp",
		.check = test__checkevent_pinned_modifier,
		.id    = 40,
	},
	{
		.name  = "{cycles,cache-misses,branch-misses}:D",
		.check = test__pinned_group,
		.id    = 41,
	},
	{
		.name  = "mem:0/1",
		.check = test__checkevent_breakpoint_len,
		.id    = 42,
	},
	{
		.name  = "mem:0/2:w",
		.check = test__checkevent_breakpoint_len_w,
		.id    = 43,
	},
	{
		.name  = "mem:0/4:rw:u",
		.check = test__checkevent_breakpoint_len_rw_modifier,
		.id    = 44
	},
#if defined(__s390x__)
	{
		.name  = "kvm-s390:kvm_s390_create_vm",
		.check = test__checkevent_tracepoint,
		.id    = 100,
	},
#endif
	{
		.name  = "instructions:I",
		.check = test__checkevent_exclude_idle_modifier,
		.id    = 45,
	},
	{
		.name  = "instructions:kIG",
		.check = test__checkevent_exclude_idle_modifier_1,
		.id    = 46,
	},
	{
		.name  = "task-clock:P,cycles",
		.check = test__checkevent_precise_max_modifier,
		.id    = 47,
	},
};

static struct evlist_test test__events_pmu[] = {
	{
		.name  = "cpu/config=10,config1,config2=3,period=1000/u",
		.check = test__checkevent_pmu,
		.id    = 0,
	},
	{
		.name  = "cpu/config=1,name=krava/u,cpu/config=2/u",
		.check = test__checkevent_pmu_name,
		.id    = 1,
	},
	{
		.name  = "cpu/config=1,call-graph=fp,time,period=100000/,cpu/config=2,call-graph=no,time=0,period=2000/",
		.check = test__checkevent_pmu_partial_time_callgraph,
		.id    = 2,
	},
};

struct terms_test {
	const char *str;
	__u32 type;
	int (*check)(struct list_head *terms);
};

static struct terms_test test__terms[] = {
	[0] = {
		.str   = "config=10,config1,config2=3,umask=1",
		.check = test__checkterms_simple,
	},
};

static int test_event(struct evlist_test *e)
{
	struct perf_evlist *evlist;
	int ret;

	evlist = perf_evlist__new();
	if (evlist == NULL)
		return -ENOMEM;

	ret = parse_events(evlist, e->name, NULL);
	if (ret) {
		pr_debug("failed to parse event '%s', err %d\n",
			 e->name, ret);
	} else {
		ret = e->check(evlist);
	}

	perf_evlist__delete(evlist);

	return ret;
}

static int test_events(struct evlist_test *events, unsigned cnt)
{
	int ret1, ret2 = 0;
	unsigned i;

	for (i = 0; i < cnt; i++) {
		struct evlist_test *e = &events[i];

		pr_debug("running test %d '%s'\n", e->id, e->name);
		ret1 = test_event(e);
		if (ret1)
			ret2 = ret1;
	}

	return ret2;
}

static int test_term(struct terms_test *t)
{
	struct list_head terms;
	int ret;

	INIT_LIST_HEAD(&terms);

	ret = parse_events_terms(&terms, t->str);
	if (ret) {
		pr_debug("failed to parse terms '%s', err %d\n",
			 t->str , ret);
		return ret;
	}

	ret = t->check(&terms);
	parse_events__free_terms(&terms);

	return ret;
}

static int test_terms(struct terms_test *terms, unsigned cnt)
{
	int ret = 0;
	unsigned i;

	for (i = 0; i < cnt; i++) {
		struct terms_test *t = &terms[i];

		pr_debug("running test %d '%s'\n", i, t->str);
		ret = test_term(t);
		if (ret)
			break;
	}

	return ret;
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

static int test_pmu_events(void)
{
	struct stat st;
	char path[PATH_MAX];
	struct dirent *ent;
	DIR *dir;
	int ret;

	snprintf(path, PATH_MAX, "%s/bus/event_source/devices/cpu/events/",
		 sysfs__mountpoint());

	ret = stat(path, &st);
	if (ret) {
		pr_debug("omitting PMU cpu events tests\n");
		return 0;
	}

	dir = opendir(path);
	if (!dir) {
		pr_debug("can't open pmu event dir");
		return -1;
	}

	while (!ret && (ent = readdir(dir))) {
#define MAX_NAME 100
		struct evlist_test e;
		char name[MAX_NAME];

		if (!strcmp(ent->d_name, ".") ||
		    !strcmp(ent->d_name, ".."))
			continue;

		snprintf(name, MAX_NAME, "cpu/event=%s/u", ent->d_name);

		e.name  = name;
		e.check = test__checkevent_pmu_events;

		ret = test_event(&e);
		if (ret)
			break;
		snprintf(name, MAX_NAME, "%s:u,cpu/event=%s/u", ent->d_name, ent->d_name);
		e.name  = name;
		e.check = test__checkevent_pmu_events_mix;
		ret = test_event(&e);
#undef MAX_NAME
	}

	closedir(dir);
	return ret;
}

static void debug_warn(const char *warn, va_list params)
{
	char msg[1024];

	if (!verbose)
		return;

	vsnprintf(msg, sizeof(msg), warn, params);
	fprintf(stderr, " Warning: %s\n", msg);
}

int test__parse_events(int subtest __maybe_unused)
{
	int ret1, ret2 = 0;

#define TEST_EVENTS(tests)				\
do {							\
	ret1 = test_events(tests, ARRAY_SIZE(tests));	\
	if (!ret2)					\
		ret2 = ret1;				\
} while (0)

	set_warning_routine(debug_warn);

	TEST_EVENTS(test__events);

	if (test_pmu())
		TEST_EVENTS(test__events_pmu);

	if (test_pmu()) {
		int ret = test_pmu_events();
		if (ret)
			return ret;
	}

	ret1 = test_terms(test__terms, ARRAY_SIZE(test__terms));
	if (!ret2)
		ret2 = ret1;

	return ret2;
}
