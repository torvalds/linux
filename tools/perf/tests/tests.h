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

struct test {
	const char *desc;
	int (*func)(int subtest);
	struct {
		bool skip_if_fail;
		int (*get_nr)(void);
		const char *(*get_desc)(int subtest);
	} subtest;
};

/* Tests */
int test__vmlinux_matches_kallsyms(int subtest);
int test__openat_syscall_event(int subtest);
int test__openat_syscall_event_on_all_cpus(int subtest);
int test__basic_mmap(int subtest);
int test__PERF_RECORD(int subtest);
int test__perf_evsel__roundtrip_name_test(int subtest);
int test__perf_evsel__tp_sched_test(int subtest);
int test__syscall_openat_tp_fields(int subtest);
int test__pmu(int subtest);
int test__attr(int subtest);
int test__dso_data(int subtest);
int test__dso_data_cache(int subtest);
int test__dso_data_reopen(int subtest);
int test__parse_events(int subtest);
int test__hists_link(int subtest);
int test__python_use(int subtest);
int test__bp_signal(int subtest);
int test__bp_signal_overflow(int subtest);
int test__task_exit(int subtest);
int test__sw_clock_freq(int subtest);
int test__code_reading(int subtest);
int test__sample_parsing(int subtest);
int test__keep_tracking(int subtest);
int test__parse_no_sample_id_all(int subtest);
int test__dwarf_unwind(int subtest);
int test__hists_filter(int subtest);
int test__mmap_thread_lookup(int subtest);
int test__thread_mg_share(int subtest);
int test__hists_output(int subtest);
int test__hists_cumulate(int subtest);
int test__switch_tracking(int subtest);
int test__fdarray__filter(int subtest);
int test__fdarray__add(int subtest);
int test__kmod_path__parse(int subtest);
int test__thread_map(int subtest);
int test__llvm(int subtest);
const char *test__llvm_subtest_get_desc(int subtest);
int test__llvm_subtest_get_nr(void);
int test__bpf(int subtest);
const char *test__bpf_subtest_get_desc(int subtest);
int test__bpf_subtest_get_nr(void);
int test_session_topology(int subtest);
int test__thread_map_synthesize(int subtest);
int test__cpu_map_synthesize(int subtest);
int test__synthesize_stat_config(int subtest);
int test__synthesize_stat(int subtest);
int test__synthesize_stat_round(int subtest);
int test__event_update(int subtest);
int test__event_times(int subtest);
int test__backward_ring_buffer(int subtest);

#if defined(__arm__) || defined(__aarch64__)
#ifdef HAVE_DWARF_UNWIND_SUPPORT
struct thread;
struct perf_sample;
int test__arch_unwind_sample(struct perf_sample *sample,
			     struct thread *thread);
#endif
#endif
#endif /* TESTS_H */
