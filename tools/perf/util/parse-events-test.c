
#include "parse-events.h"
#include "evsel.h"
#include "evlist.h"
#include "sysfs.h"
#include "../../../include/linux/hw_breakpoint.h"

#define TEST_ASSERT_VAL(text, cond) \
do { \
	if (!(cond)) { \
		pr_debug("FAILED %s:%d %s\n", __FILE__, __LINE__, text); \
		return -1; \
	} \
} while (0)

static int test__checkevent_tracepoint(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = list_entry(evlist->entries.next,
					      struct perf_evsel, node);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_TRACEPOINT == evsel->attr.type);
	TEST_ASSERT_VAL("wrong sample_type",
		(PERF_SAMPLE_RAW | PERF_SAMPLE_TIME | PERF_SAMPLE_CPU) ==
		evsel->attr.sample_type);
	TEST_ASSERT_VAL("wrong sample_period", 1 == evsel->attr.sample_period);
	return 0;
}

static int test__checkevent_tracepoint_multi(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel;

	TEST_ASSERT_VAL("wrong number of entries", evlist->nr_entries > 1);

	list_for_each_entry(evsel, &evlist->entries, node) {
		TEST_ASSERT_VAL("wrong type",
			PERF_TYPE_TRACEPOINT == evsel->attr.type);
		TEST_ASSERT_VAL("wrong sample_type",
			(PERF_SAMPLE_RAW | PERF_SAMPLE_TIME | PERF_SAMPLE_CPU)
			== evsel->attr.sample_type);
		TEST_ASSERT_VAL("wrong sample_period",
			1 == evsel->attr.sample_period);
	}
	return 0;
}

static int test__checkevent_raw(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = list_entry(evlist->entries.next,
					      struct perf_evsel, node);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_RAW == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config", 0x1a == evsel->attr.config);
	return 0;
}

static int test__checkevent_numeric(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = list_entry(evlist->entries.next,
					      struct perf_evsel, node);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->nr_entries);
	TEST_ASSERT_VAL("wrong type", 1 == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config", 1 == evsel->attr.config);
	return 0;
}

static int test__checkevent_symbolic_name(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = list_entry(evlist->entries.next,
					      struct perf_evsel, node);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_INSTRUCTIONS == evsel->attr.config);
	return 0;
}

static int test__checkevent_symbolic_name_config(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = list_entry(evlist->entries.next,
					      struct perf_evsel, node);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_HW_CPU_CYCLES == evsel->attr.config);
	TEST_ASSERT_VAL("wrong period",
			100000 == evsel->attr.sample_period);
	TEST_ASSERT_VAL("wrong config1",
			0 == evsel->attr.config1);
	TEST_ASSERT_VAL("wrong config2",
			1 == evsel->attr.config2);
	return 0;
}

static int test__checkevent_symbolic_alias(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = list_entry(evlist->entries.next,
					      struct perf_evsel, node);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_SOFTWARE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",
			PERF_COUNT_SW_PAGE_FAULTS == evsel->attr.config);
	return 0;
}

static int test__checkevent_genhw(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = list_entry(evlist->entries.next,
					      struct perf_evsel, node);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HW_CACHE == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config", (1 << 16) == evsel->attr.config);
	return 0;
}

static int test__checkevent_breakpoint(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = list_entry(evlist->entries.next,
					      struct perf_evsel, node);

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
	struct perf_evsel *evsel = list_entry(evlist->entries.next,
					      struct perf_evsel, node);

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
	struct perf_evsel *evsel = list_entry(evlist->entries.next,
					      struct perf_evsel, node);

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
	struct perf_evsel *evsel = list_entry(evlist->entries.next,
					      struct perf_evsel, node);

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

static int test__checkevent_tracepoint_modifier(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = list_entry(evlist->entries.next,
					      struct perf_evsel, node);

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

	list_for_each_entry(evsel, &evlist->entries, node) {
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
	struct perf_evsel *evsel = list_entry(evlist->entries.next,
					      struct perf_evsel, node);

	TEST_ASSERT_VAL("wrong exclude_user", evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", evsel->attr.precise_ip);

	return test__checkevent_raw(evlist);
}

static int test__checkevent_numeric_modifier(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = list_entry(evlist->entries.next,
					      struct perf_evsel, node);

	TEST_ASSERT_VAL("wrong exclude_user", evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", evsel->attr.precise_ip);

	return test__checkevent_numeric(evlist);
}

static int test__checkevent_symbolic_name_modifier(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = list_entry(evlist->entries.next,
					      struct perf_evsel, node);

	TEST_ASSERT_VAL("wrong exclude_user", evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);

	return test__checkevent_symbolic_name(evlist);
}

static int test__checkevent_exclude_host_modifier(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = list_entry(evlist->entries.next,
					      struct perf_evsel, node);

	TEST_ASSERT_VAL("wrong exclude guest", !evsel->attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", evsel->attr.exclude_host);

	return test__checkevent_symbolic_name(evlist);
}

static int test__checkevent_exclude_guest_modifier(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = list_entry(evlist->entries.next,
					      struct perf_evsel, node);

	TEST_ASSERT_VAL("wrong exclude guest", evsel->attr.exclude_guest);
	TEST_ASSERT_VAL("wrong exclude host", !evsel->attr.exclude_host);

	return test__checkevent_symbolic_name(evlist);
}

static int test__checkevent_symbolic_alias_modifier(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = list_entry(evlist->entries.next,
					      struct perf_evsel, node);

	TEST_ASSERT_VAL("wrong exclude_user", !evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);

	return test__checkevent_symbolic_alias(evlist);
}

static int test__checkevent_genhw_modifier(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = list_entry(evlist->entries.next,
					      struct perf_evsel, node);

	TEST_ASSERT_VAL("wrong exclude_user", evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", evsel->attr.precise_ip);

	return test__checkevent_genhw(evlist);
}

static int test__checkevent_breakpoint_modifier(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = list_entry(evlist->entries.next,
					      struct perf_evsel, node);

	TEST_ASSERT_VAL("wrong exclude_user", !evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);

	return test__checkevent_breakpoint(evlist);
}

static int test__checkevent_breakpoint_x_modifier(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = list_entry(evlist->entries.next,
					      struct perf_evsel, node);

	TEST_ASSERT_VAL("wrong exclude_user", evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);

	return test__checkevent_breakpoint_x(evlist);
}

static int test__checkevent_breakpoint_r_modifier(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = list_entry(evlist->entries.next,
					      struct perf_evsel, node);

	TEST_ASSERT_VAL("wrong exclude_user", evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", evsel->attr.precise_ip);

	return test__checkevent_breakpoint_r(evlist);
}

static int test__checkevent_breakpoint_w_modifier(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel = list_entry(evlist->entries.next,
					      struct perf_evsel, node);

	TEST_ASSERT_VAL("wrong exclude_user", !evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", evsel->attr.precise_ip);

	return test__checkevent_breakpoint_w(evlist);
}

static int test__checkevent_pmu(struct perf_evlist *evlist)
{

	struct perf_evsel *evsel = list_entry(evlist->entries.next,
					      struct perf_evsel, node);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_RAW == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config",    10 == evsel->attr.config);
	TEST_ASSERT_VAL("wrong config1",    1 == evsel->attr.config1);
	TEST_ASSERT_VAL("wrong config2",    3 == evsel->attr.config2);
	TEST_ASSERT_VAL("wrong period",  1000 == evsel->attr.sample_period);

	return 0;
}

static int test__checkevent_list(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel;

	TEST_ASSERT_VAL("wrong number of entries", 3 == evlist->nr_entries);

	/* r1 */
	evsel = list_entry(evlist->entries.next, struct perf_evsel, node);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_RAW == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config", 1 == evsel->attr.config);
	TEST_ASSERT_VAL("wrong config1", 0 == evsel->attr.config1);
	TEST_ASSERT_VAL("wrong config2", 0 == evsel->attr.config2);
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);

	/* syscalls:sys_enter_open:k */
	evsel = list_entry(evsel->node.next, struct perf_evsel, node);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_TRACEPOINT == evsel->attr.type);
	TEST_ASSERT_VAL("wrong sample_type",
		(PERF_SAMPLE_RAW | PERF_SAMPLE_TIME | PERF_SAMPLE_CPU) ==
		evsel->attr.sample_type);
	TEST_ASSERT_VAL("wrong sample_period", 1 == evsel->attr.sample_period);
	TEST_ASSERT_VAL("wrong exclude_user", evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", !evsel->attr.precise_ip);

	/* 1:1:hp */
	evsel = list_entry(evsel->node.next, struct perf_evsel, node);
	TEST_ASSERT_VAL("wrong type", 1 == evsel->attr.type);
	TEST_ASSERT_VAL("wrong config", 1 == evsel->attr.config);
	TEST_ASSERT_VAL("wrong exclude_user", evsel->attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->attr.exclude_kernel);
	TEST_ASSERT_VAL("wrong exclude_hv", !evsel->attr.exclude_hv);
	TEST_ASSERT_VAL("wrong precise_ip", evsel->attr.precise_ip);

	return 0;
}

struct test__event_st {
	const char *name;
	__u32 type;
	int (*check)(struct perf_evlist *evlist);
};

static struct test__event_st test__events[] = {
	[0] = {
		.name  = "syscalls:sys_enter_open",
		.check = test__checkevent_tracepoint,
	},
	[1] = {
		.name  = "syscalls:*",
		.check = test__checkevent_tracepoint_multi,
	},
	[2] = {
		.name  = "r1a",
		.check = test__checkevent_raw,
	},
	[3] = {
		.name  = "1:1",
		.check = test__checkevent_numeric,
	},
	[4] = {
		.name  = "instructions",
		.check = test__checkevent_symbolic_name,
	},
	[5] = {
		.name  = "cycles/period=100000,config2/",
		.check = test__checkevent_symbolic_name_config,
	},
	[6] = {
		.name  = "faults",
		.check = test__checkevent_symbolic_alias,
	},
	[7] = {
		.name  = "L1-dcache-load-miss",
		.check = test__checkevent_genhw,
	},
	[8] = {
		.name  = "mem:0",
		.check = test__checkevent_breakpoint,
	},
	[9] = {
		.name  = "mem:0:x",
		.check = test__checkevent_breakpoint_x,
	},
	[10] = {
		.name  = "mem:0:r",
		.check = test__checkevent_breakpoint_r,
	},
	[11] = {
		.name  = "mem:0:w",
		.check = test__checkevent_breakpoint_w,
	},
	[12] = {
		.name  = "syscalls:sys_enter_open:k",
		.check = test__checkevent_tracepoint_modifier,
	},
	[13] = {
		.name  = "syscalls:*:u",
		.check = test__checkevent_tracepoint_multi_modifier,
	},
	[14] = {
		.name  = "r1a:kp",
		.check = test__checkevent_raw_modifier,
	},
	[15] = {
		.name  = "1:1:hp",
		.check = test__checkevent_numeric_modifier,
	},
	[16] = {
		.name  = "instructions:h",
		.check = test__checkevent_symbolic_name_modifier,
	},
	[17] = {
		.name  = "faults:u",
		.check = test__checkevent_symbolic_alias_modifier,
	},
	[18] = {
		.name  = "L1-dcache-load-miss:kp",
		.check = test__checkevent_genhw_modifier,
	},
	[19] = {
		.name  = "mem:0:u",
		.check = test__checkevent_breakpoint_modifier,
	},
	[20] = {
		.name  = "mem:0:x:k",
		.check = test__checkevent_breakpoint_x_modifier,
	},
	[21] = {
		.name  = "mem:0:r:hp",
		.check = test__checkevent_breakpoint_r_modifier,
	},
	[22] = {
		.name  = "mem:0:w:up",
		.check = test__checkevent_breakpoint_w_modifier,
	},
	[23] = {
		.name  = "r1,syscalls:sys_enter_open:k,1:1:hp",
		.check = test__checkevent_list,
	},
	[24] = {
		.name  = "instructions:G",
		.check = test__checkevent_exclude_host_modifier,
	},
	[25] = {
		.name  = "instructions:H",
		.check = test__checkevent_exclude_guest_modifier,
	},
};

#define TEST__EVENTS_CNT (sizeof(test__events) / sizeof(struct test__event_st))

static struct test__event_st test__events_pmu[] = {
	[0] = {
		.name  = "cpu/config=10,config1,config2=3,period=1000/u",
		.check = test__checkevent_pmu,
	},
};

#define TEST__EVENTS_PMU_CNT (sizeof(test__events_pmu) / \
			      sizeof(struct test__event_st))

static int test(struct test__event_st *e)
{
	struct perf_evlist *evlist;
	int ret;

	evlist = perf_evlist__new(NULL, NULL);
	if (evlist == NULL)
		return -ENOMEM;

	ret = parse_events(evlist, e->name, 0);
	if (ret) {
		pr_debug("failed to parse event '%s', err %d\n",
			 e->name, ret);
		return ret;
	}

	ret = e->check(evlist);
	perf_evlist__delete(evlist);

	return ret;
}

static int test_events(struct test__event_st *events, unsigned cnt)
{
	int ret = 0;
	unsigned i;

	for (i = 0; i < cnt; i++) {
		struct test__event_st *e = &events[i];

		pr_debug("running test %d '%s'\n", i, e->name);
		ret = test(e);
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
		 sysfs_find_mountpoint());

	ret = stat(path, &st);
	if (ret)
		pr_debug("ommiting PMU cpu tests\n");
	return !ret;
}

int parse_events__test(void)
{
	int ret;

	ret = test_events(test__events, TEST__EVENTS_CNT);
	if (!ret && test_pmu())
		ret = test_events(test__events_pmu, TEST__EVENTS_PMU_CNT);

	return ret;
}
