/* SPDX-License-Identifier: GPL-2.0 */
#ifndef TESTS_H
#define TESTS_H

#include <stdbool.h>

#define TEST_ASSERT_VAL(text, cond)					 \
do {									 \
	if (!(cond)) {							 \
		pr_debug("FAILED %s:%d %s\n", __FILE__, __LINE__, text); \
		return -1;						 \
	}								 \
} while (0)

#define TEST_ASSERT_EQUAL(text, val, expected)				 \
do {									 \
	if (val != expected) {						 \
		pr_debug("FAILED %s:%d %s (%d != %d)\n",		 \
			 __FILE__, __LINE__, text, val, expected);	 \
		return -1;						 \
	}								 \
} while (0)

enum {
	TEST_OK   =  0,
	TEST_FAIL = -1,
	TEST_SKIP = -2,
};

struct test_suite;

typedef int (*test_fnptr)(struct test_suite *, int);

struct test_suite {
	const char *desc;
	test_fnptr func;
	struct {
		bool skip_if_fail;
		int (*get_nr)(void);
		const char *(*get_desc)(int subtest);
		const char *(*skip_reason)(int subtest);
	} subtest;
	bool (*is_supported)(void);
	void *priv;
};

#define DECLARE_SUITE(name) \
	extern struct test_suite suite__##name;

#define DEFINE_SUITE(description, name)		\
	struct test_suite suite__##name = {		\
		.desc = description,		\
		.func = test__##name,		\
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
DECLARE_SUITE(llvm);
DECLARE_SUITE(bpf);
DECLARE_SUITE(session_topology);
DECLARE_SUITE(thread_map_synthesize);
DECLARE_SUITE(thread_map_remove);
DECLARE_SUITE(cpu_map_synthesize);
DECLARE_SUITE(synthesize_stat_config);
DECLARE_SUITE(synthesize_stat);
DECLARE_SUITE(synthesize_stat_round);
DECLARE_SUITE(event_update);
DECLARE_SUITE(event_times);
DECLARE_SUITE(backward_ring_buffer);
DECLARE_SUITE(cpu_map_print);
DECLARE_SUITE(cpu_map_merge);
DECLARE_SUITE(sdt_event);
DECLARE_SUITE(is_printable_array);
DECLARE_SUITE(bitmap_print);
DECLARE_SUITE(perf_hooks);
DECLARE_SUITE(clang);
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

bool test__bp_signal_is_supported(void);

#ifdef HAVE_DWARF_UNWIND_SUPPORT
struct thread;
struct perf_sample;
int test__arch_unwind_sample(struct perf_sample *sample,
			     struct thread *thread);
#endif

#if defined(__arm__)
DECLARE_SUITE(vectors_page);
#endif

#endif /* TESTS_H */
