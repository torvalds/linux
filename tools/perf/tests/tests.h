/* SPDX-License-Identifier: GPL-2.0 */
#ifndef TESTS_H
#define TESTS_H

#include <stdbool.h>

enum {
	TEST_OK   =  0,
	TEST_FAIL = -1,
	TEST_SKIP = -2,
};

#define TEST_ASSERT_VAL(text, cond)					 \
do {									 \
	if (!(cond)) {							 \
		pr_debug("FAILED %s:%d %s\n", __FILE__, __LINE__, text); \
		return TEST_FAIL;					 \
	}								 \
} while (0)

#define TEST_ASSERT_EQUAL(text, val, expected)				 \
do {									 \
	if (val != expected) {						 \
		pr_debug("FAILED %s:%d %s (%d != %d)\n",		 \
			 __FILE__, __LINE__, text, val, expected);	 \
		return TEST_FAIL;						 \
	}								 \
} while (0)

struct test_suite;

typedef int (*test_fnptr)(struct test_suite *, int);

struct test_case {
	const char *name;
	const char *desc;
	const char *skip_reason;
	test_fnptr run_case;
};

struct test_suite {
	const char *desc;
	struct test_case *test_cases;
	void *priv;
};

#define DECLARE_SUITE(name) \
	extern struct test_suite suite__##name;

#define TEST_CASE(description, _name)			\
	{						\
		.name = #_name,				\
		.desc = description,			\
		.run_case = test__##_name,		\
	}

#define TEST_CASE_REASON(description, _name, _reason)	\
	{						\
		.name = #_name,				\
		.desc = description,			\
		.run_case = test__##_name,		\
		.skip_reason = _reason,			\
	}

#define DEFINE_SUITE(description, _name)		\
	struct test_case tests__##_name[] = {           \
		TEST_CASE(description, _name),		\
		{	.name = NULL, }			\
	};						\
	struct test_suite suite__##_name = {		\
		.desc = description,			\
		.test_cases = tests__##_name,		\
	}

/* Tests */
DECLARE_SUITE(vmlinux_matches_kallsyms);
DECLARE_SUITE(openat_syscall_event);
DECLARE_SUITE(openat_syscall_event_on_all_cpus);
DECLARE_SUITE(basic_mmap);
DECLARE_SUITE(PERF_RECORD);
DECLARE_SUITE(perf_evsel__roundtrip_name_test);
DECLARE_SUITE(perf_evsel__tp_sched_test);
DECLARE_SUITE(syscall_openat_tp_fields);
DECLARE_SUITE(pmu);
DECLARE_SUITE(pmu_events);
DECLARE_SUITE(attr);
DECLARE_SUITE(dso_data);
DECLARE_SUITE(dso_data_cache);
DECLARE_SUITE(dso_data_reopen);
DECLARE_SUITE(parse_events);
DECLARE_SUITE(hists_link);
DECLARE_SUITE(python_use);
DECLARE_SUITE(bp_signal);
DECLARE_SUITE(bp_signal_overflow);
DECLARE_SUITE(bp_accounting);
DECLARE_SUITE(wp);
DECLARE_SUITE(task_exit);
DECLARE_SUITE(mem);
DECLARE_SUITE(sw_clock_freq);
DECLARE_SUITE(code_reading);
DECLARE_SUITE(sample_parsing);
DECLARE_SUITE(keep_tracking);
DECLARE_SUITE(parse_no_sample_id_all);
DECLARE_SUITE(dwarf_unwind);
DECLARE_SUITE(expr);
DECLARE_SUITE(hists_filter);
DECLARE_SUITE(mmap_thread_lookup);
DECLARE_SUITE(thread_maps_share);
DECLARE_SUITE(hists_output);
DECLARE_SUITE(hists_cumulate);
DECLARE_SUITE(switch_tracking);
DECLARE_SUITE(fdarray__filter);
DECLARE_SUITE(fdarray__add);
DECLARE_SUITE(kmod_path__parse);
DECLARE_SUITE(thread_map);
DECLARE_SUITE(bpf);
DECLARE_SUITE(session_topology);
DECLARE_SUITE(thread_map_synthesize);
DECLARE_SUITE(thread_map_remove);
DECLARE_SUITE(cpu_map);
DECLARE_SUITE(synthesize_stat_config);
DECLARE_SUITE(synthesize_stat);
DECLARE_SUITE(synthesize_stat_round);
DECLARE_SUITE(event_update);
DECLARE_SUITE(event_times);
DECLARE_SUITE(backward_ring_buffer);
DECLARE_SUITE(sdt_event);
DECLARE_SUITE(is_printable_array);
DECLARE_SUITE(bitmap_print);
DECLARE_SUITE(perf_hooks);
DECLARE_SUITE(unit_number__scnprint);
DECLARE_SUITE(mem2node);
DECLARE_SUITE(maps__merge_in);
DECLARE_SUITE(time_utils);
DECLARE_SUITE(jit_write_elf);
DECLARE_SUITE(api_io);
DECLARE_SUITE(demangle_java);
DECLARE_SUITE(demangle_ocaml);
DECLARE_SUITE(pfm);
DECLARE_SUITE(parse_metric);
DECLARE_SUITE(pe_file_parsing);
DECLARE_SUITE(expand_cgroup_events);
DECLARE_SUITE(perf_time_to_tsc);
DECLARE_SUITE(dlfilter);
DECLARE_SUITE(sigtrap);
DECLARE_SUITE(event_groups);
DECLARE_SUITE(symbols);
DECLARE_SUITE(util);

/*
 * PowerPC and S390 do not support creation of instruction breakpoints using the
 * perf_event interface.
 *
 * ARM requires explicit rounding down of the instruction pointer in Thumb mode,
 * and then requires the single-step to be handled explicitly in the overflow
 * handler to avoid stepping into the SIGIO handler and getting stuck on the
 * breakpointed instruction.
 *
 * Since arm64 has the same issue with arm for the single-step handling, this
 * case also gets stuck on the breakpointed instruction.
 *
 * Just disable the test for these architectures until these issues are
 * resolved.
 */
#if defined(__powerpc__) || defined(__s390x__) || defined(__arm__) || defined(__aarch64__)
#define BP_SIGNAL_IS_SUPPORTED 0
#else
#define BP_SIGNAL_IS_SUPPORTED 1
#endif

#ifdef HAVE_DWARF_UNWIND_SUPPORT
struct thread;
struct perf_sample;
int test__arch_unwind_sample(struct perf_sample *sample,
			     struct thread *thread);
#endif

#if defined(__arm__)
DECLARE_SUITE(vectors_page);
#endif

/*
 * Define test workloads to be used in test suites.
 */
typedef int (*workload_fnptr)(int argc, const char **argv);

struct test_workload {
	const char	*name;
	workload_fnptr	func;
};

#define DECLARE_WORKLOAD(work) \
	extern struct test_workload workload__##work

#define DEFINE_WORKLOAD(work) \
struct test_workload workload__##work = {	\
	.name = #work,				\
	.func = work,				\
}

/* The list of test workloads */
DECLARE_WORKLOAD(noploop);
DECLARE_WORKLOAD(thloop);
DECLARE_WORKLOAD(leafloop);
DECLARE_WORKLOAD(sqrtloop);
DECLARE_WORKLOAD(brstack);
DECLARE_WORKLOAD(datasym);

extern const char *dso_to_test;
extern const char *test_objdump_path;

#endif /* TESTS_H */
