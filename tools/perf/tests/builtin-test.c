/*
 * builtin-test.c
 *
 * Builtin regression testing command: ever growing number of sanity tests
 */
#include "builtin.h"

#include "util/cache.h"
#include "util/color.h"
#include "util/debug.h"
#include "util/debugfs.h"
#include "util/evlist.h"
#include "util/machine.h"
#include "util/parse-options.h"
#include "util/parse-events.h"
#include "util/symbol.h"
#include "util/thread_map.h"
#include "util/pmu.h"
#include "event-parse.h"
#include "../../include/linux/hw_breakpoint.h"

#include <sys/mman.h>

#include "util/cpumap.h"
#include "util/evsel.h"
#include <sys/types.h>

#include "tests.h"

#include <sched.h>


static int test__perf_pmu(void)
{
	return perf_pmu__test();
}

static int perf_evsel__roundtrip_cache_name_test(void)
{
	char name[128];
	int type, op, err = 0, ret = 0, i, idx;
	struct perf_evsel *evsel;
        struct perf_evlist *evlist = perf_evlist__new(NULL, NULL);

        if (evlist == NULL)
                return -ENOMEM;

	for (type = 0; type < PERF_COUNT_HW_CACHE_MAX; type++) {
		for (op = 0; op < PERF_COUNT_HW_CACHE_OP_MAX; op++) {
			/* skip invalid cache type */
			if (!perf_evsel__is_cache_op_valid(type, op))
				continue;

			for (i = 0; i < PERF_COUNT_HW_CACHE_RESULT_MAX; i++) {
				__perf_evsel__hw_cache_type_op_res_name(type, op, i,
									name, sizeof(name));
				err = parse_events(evlist, name, 0);
				if (err)
					ret = err;
			}
		}
	}

	idx = 0;
	evsel = perf_evlist__first(evlist);

	for (type = 0; type < PERF_COUNT_HW_CACHE_MAX; type++) {
		for (op = 0; op < PERF_COUNT_HW_CACHE_OP_MAX; op++) {
			/* skip invalid cache type */
			if (!perf_evsel__is_cache_op_valid(type, op))
				continue;

			for (i = 0; i < PERF_COUNT_HW_CACHE_RESULT_MAX; i++) {
				__perf_evsel__hw_cache_type_op_res_name(type, op, i,
									name, sizeof(name));
				if (evsel->idx != idx)
					continue;

				++idx;

				if (strcmp(perf_evsel__name(evsel), name)) {
					pr_debug("%s != %s\n", perf_evsel__name(evsel), name);
					ret = -1;
				}

				evsel = perf_evsel__next(evsel);
			}
		}
	}

	perf_evlist__delete(evlist);
	return ret;
}

static int __perf_evsel__name_array_test(const char *names[], int nr_names)
{
	int i, err;
	struct perf_evsel *evsel;
        struct perf_evlist *evlist = perf_evlist__new(NULL, NULL);

        if (evlist == NULL)
                return -ENOMEM;

	for (i = 0; i < nr_names; ++i) {
		err = parse_events(evlist, names[i], 0);
		if (err) {
			pr_debug("failed to parse event '%s', err %d\n",
				 names[i], err);
			goto out_delete_evlist;
		}
	}

	err = 0;
	list_for_each_entry(evsel, &evlist->entries, node) {
		if (strcmp(perf_evsel__name(evsel), names[evsel->idx])) {
			--err;
			pr_debug("%s != %s\n", perf_evsel__name(evsel), names[evsel->idx]);
		}
	}

out_delete_evlist:
	perf_evlist__delete(evlist);
	return err;
}

#define perf_evsel__name_array_test(names) \
	__perf_evsel__name_array_test(names, ARRAY_SIZE(names))

static int perf_evsel__roundtrip_name_test(void)
{
	int err = 0, ret = 0;

	err = perf_evsel__name_array_test(perf_evsel__hw_names);
	if (err)
		ret = err;

	err = perf_evsel__name_array_test(perf_evsel__sw_names);
	if (err)
		ret = err;

	err = perf_evsel__roundtrip_cache_name_test();
	if (err)
		ret = err;

	return ret;
}

static int perf_evsel__test_field(struct perf_evsel *evsel, const char *name,
				  int size, bool should_be_signed)
{
	struct format_field *field = perf_evsel__field(evsel, name);
	int is_signed;
	int ret = 0;

	if (field == NULL) {
		pr_debug("%s: \"%s\" field not found!\n", evsel->name, name);
		return -1;
	}

	is_signed = !!(field->flags | FIELD_IS_SIGNED);
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

static int perf_evsel__tp_sched_test(void)
{
	struct perf_evsel *evsel = perf_evsel__newtp("sched", "sched_switch", 0);
	int ret = 0;

	if (evsel == NULL) {
		pr_debug("perf_evsel__new\n");
		return -1;
	}

	if (perf_evsel__test_field(evsel, "prev_comm", 16, true))
		ret = -1;

	if (perf_evsel__test_field(evsel, "prev_pid", 4, true))
		ret = -1;

	if (perf_evsel__test_field(evsel, "prev_prio", 4, true))
		ret = -1;

	if (perf_evsel__test_field(evsel, "prev_state", 8, true))
		ret = -1;

	if (perf_evsel__test_field(evsel, "next_comm", 16, true))
		ret = -1;

	if (perf_evsel__test_field(evsel, "next_pid", 4, true))
		ret = -1;

	if (perf_evsel__test_field(evsel, "next_prio", 4, true))
		ret = -1;

	perf_evsel__delete(evsel);

	evsel = perf_evsel__newtp("sched", "sched_wakeup", 0);

	if (perf_evsel__test_field(evsel, "comm", 16, true))
		ret = -1;

	if (perf_evsel__test_field(evsel, "pid", 4, true))
		ret = -1;

	if (perf_evsel__test_field(evsel, "prio", 4, true))
		ret = -1;

	if (perf_evsel__test_field(evsel, "success", 4, true))
		ret = -1;

	if (perf_evsel__test_field(evsel, "target_cpu", 4, true))
		ret = -1;

	return ret;
}

static int test__syscall_open_tp_fields(void)
{
	struct perf_record_opts opts = {
		.target = {
			.uid = UINT_MAX,
			.uses_mmap = true,
		},
		.no_delay   = true,
		.freq	    = 1,
		.mmap_pages = 256,
		.raw_samples = true,
	};
	const char *filename = "/etc/passwd";
	int flags = O_RDONLY | O_DIRECTORY;
	struct perf_evlist *evlist = perf_evlist__new(NULL, NULL);
	struct perf_evsel *evsel;
	int err = -1, i, nr_events = 0, nr_polls = 0;

	if (evlist == NULL) {
		pr_debug("%s: perf_evlist__new\n", __func__);
		goto out;
	}

	evsel = perf_evsel__newtp("syscalls", "sys_enter_open", 0);
	if (evsel == NULL) {
		pr_debug("%s: perf_evsel__newtp\n", __func__);
		goto out_delete_evlist;
	}

	perf_evlist__add(evlist, evsel);

	err = perf_evlist__create_maps(evlist, &opts.target);
	if (err < 0) {
		pr_debug("%s: perf_evlist__create_maps\n", __func__);
		goto out_delete_evlist;
	}

	perf_evsel__config(evsel, &opts, evsel);

	evlist->threads->map[0] = getpid();

	err = perf_evlist__open(evlist);
	if (err < 0) {
		pr_debug("perf_evlist__open: %s\n", strerror(errno));
		goto out_delete_evlist;
	}

	err = perf_evlist__mmap(evlist, UINT_MAX, false);
	if (err < 0) {
		pr_debug("perf_evlist__mmap: %s\n", strerror(errno));
		goto out_delete_evlist;
	}

	perf_evlist__enable(evlist);

	/*
	 * Generate the event:
	 */
	open(filename, flags);

	while (1) {
		int before = nr_events;

		for (i = 0; i < evlist->nr_mmaps; i++) {
			union perf_event *event;

			while ((event = perf_evlist__mmap_read(evlist, i)) != NULL) {
				const u32 type = event->header.type;
				int tp_flags;
				struct perf_sample sample;

				++nr_events;

				if (type != PERF_RECORD_SAMPLE)
					continue;

				err = perf_evsel__parse_sample(evsel, event, &sample);
				if (err) {
					pr_err("Can't parse sample, err = %d\n", err);
					goto out_munmap;
				}

				tp_flags = perf_evsel__intval(evsel, &sample, "flags");

				if (flags != tp_flags) {
					pr_debug("%s: Expected flags=%#x, got %#x\n",
						 __func__, flags, tp_flags);
					goto out_munmap;
				}

				goto out_ok;
			}
		}

		if (nr_events == before)
			poll(evlist->pollfd, evlist->nr_fds, 10);

		if (++nr_polls > 5) {
			pr_debug("%s: no events!\n", __func__);
			goto out_munmap;
		}
	}
out_ok:
	err = 0;
out_munmap:
	perf_evlist__munmap(evlist);
out_delete_evlist:
	perf_evlist__delete(evlist);
out:
	return err;
}

static struct test {
	const char *desc;
	int (*func)(void);
} tests[] = {
	{
		.desc = "vmlinux symtab matches kallsyms",
		.func = test__vmlinux_matches_kallsyms,
	},
	{
		.desc = "detect open syscall event",
		.func = test__open_syscall_event,
	},
	{
		.desc = "detect open syscall event on all cpus",
		.func = test__open_syscall_event_on_all_cpus,
	},
	{
		.desc = "read samples using the mmap interface",
		.func = test__basic_mmap,
	},
	{
		.desc = "parse events tests",
		.func = parse_events__test,
	},
#if defined(__x86_64__) || defined(__i386__)
	{
		.desc = "x86 rdpmc test",
		.func = test__rdpmc,
	},
#endif
	{
		.desc = "Validate PERF_RECORD_* events & perf_sample fields",
		.func = test__PERF_RECORD,
	},
	{
		.desc = "Test perf pmu format parsing",
		.func = test__perf_pmu,
	},
	{
		.desc = "Test dso data interface",
		.func = dso__test_data,
	},
	{
		.desc = "roundtrip evsel->name check",
		.func = perf_evsel__roundtrip_name_test,
	},
	{
		.desc = "Check parsing of sched tracepoints fields",
		.func = perf_evsel__tp_sched_test,
	},
	{
		.desc = "Generate and check syscalls:sys_enter_open event fields",
		.func = test__syscall_open_tp_fields,
	},
	{
		.desc = "struct perf_event_attr setup",
		.func = test_attr__run,
	},
	{
		.func = NULL,
	},
};

static bool perf_test__matches(int curr, int argc, const char *argv[])
{
	int i;

	if (argc == 0)
		return true;

	for (i = 0; i < argc; ++i) {
		char *end;
		long nr = strtoul(argv[i], &end, 10);

		if (*end == '\0') {
			if (nr == curr + 1)
				return true;
			continue;
		}

		if (strstr(tests[curr].desc, argv[i]))
			return true;
	}

	return false;
}

static int __cmd_test(int argc, const char *argv[])
{
	int i = 0;
	int width = 0;

	while (tests[i].func) {
		int len = strlen(tests[i].desc);

		if (width < len)
			width = len;
		++i;
	}

	i = 0;
	while (tests[i].func) {
		int curr = i++, err;

		if (!perf_test__matches(curr, argc, argv))
			continue;

		pr_info("%2d: %-*s:", i, width, tests[curr].desc);
		pr_debug("\n--- start ---\n");
		err = tests[curr].func();
		pr_debug("---- end ----\n%s:", tests[curr].desc);
		if (err)
			color_fprintf(stderr, PERF_COLOR_RED, " FAILED!\n");
		else
			pr_info(" Ok\n");
	}

	return 0;
}

static int perf_test__list(int argc, const char **argv)
{
	int i = 0;

	while (tests[i].func) {
		int curr = i++;

		if (argc > 1 && !strstr(tests[curr].desc, argv[1]))
			continue;

		pr_info("%2d: %s\n", i, tests[curr].desc);
	}

	return 0;
}

int cmd_test(int argc, const char **argv, const char *prefix __maybe_unused)
{
	const char * const test_usage[] = {
	"perf test [<options>] [{list <test-name-fragment>|[<test-name-fragments>|<test-numbers>]}]",
	NULL,
	};
	const struct option test_options[] = {
	OPT_INCR('v', "verbose", &verbose,
		    "be more verbose (show symbol address, etc)"),
	OPT_END()
	};

	argc = parse_options(argc, argv, test_options, test_usage, 0);
	if (argc >= 1 && !strcmp(argv[0], "list"))
		return perf_test__list(argc, argv);

	symbol_conf.priv_size = sizeof(int);
	symbol_conf.sort_by_name = true;
	symbol_conf.try_vmlinux_path = true;

	if (symbol__init() < 0)
		return -1;

	return __cmd_test(argc, argv);
}
