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

struct test {
	const char *desc;
	int (*func)(struct test *test, int subtest);
	struct {
		bool skip_if_fail;
		int (*get_nr)(void);
		const char *(*get_desc)(int subtest);
		const char *(*skip_reason)(int subtest);
	} subtest;
	bool (*is_supported)(void);
	void *priv;
};

/* Tests */
int test__vmlinux_matches_kallsyms(struct test *test, int subtest);
int test__openat_syscall_event(struct test *test, int subtest);
int test__openat_syscall_event_on_all_cpus(struct test *test, int subtest);
int test__basic_mmap(struct test *test, int subtest);
int test__PERF_RECORD(struct test *test, int subtest);
int test__perf_evsel__roundtrip_name_test(struct test *test, int subtest);
int test__perf_evsel__tp_sched_test(struct test *test, int subtest);
int test__syscall_openat_tp_fields(struct test *test, int subtest);
int test__pmu(struct test *test, int subtest);
int test__pmu_events(struct test *test, int subtest);
const char *test__pmu_events_subtest_get_desc(int subtest);
const char *test__pmu_events_subtest_skip_reason(int subtest);
int test__pmu_events_subtest_get_nr(void);
int test__attr(struct test *test, int subtest);
int test__dso_data(struct test *test, int subtest);
int test__dso_data_cache(struct test *test, int subtest);
int test__dso_data_reopen(struct test *test, int subtest);
int test__parse_events(struct test *test, int subtest);
int test__hists_link(struct test *test, int subtest);
int test__python_use(struct test *test, int subtest);
int test__bp_signal(struct test *test, int subtest);
int test__bp_signal_overflow(struct test *test, int subtest);
int test__bp_accounting(struct test *test, int subtest);
int test__wp(struct test *test, int subtest);
const char *test__wp_subtest_get_desc(int subtest);
const char *test__wp_subtest_skip_reason(int subtest);
int test__wp_subtest_get_nr(void);
int test__task_exit(struct test *test, int subtest);
int test__mem(struct test *test, int subtest);
int test__sw_clock_freq(struct test *test, int subtest);
int test__code_reading(struct test *test, int subtest);
int test__sample_parsing(struct test *test, int subtest);
int test__keep_tracking(struct test *test, int subtest);
int test__parse_no_sample_id_all(struct test *test, int subtest);
int test__dwarf_unwind(struct test *test, int subtest);
int test__expr(struct test *test, int subtest);
int test__hists_filter(struct test *test, int subtest);
int test__mmap_thread_lookup(struct test *test, int subtest);
int test__thread_maps_share(struct test *test, int subtest);
int test__hists_output(struct test *test, int subtest);
int test__hists_cumulate(struct test *test, int subtest);
int test__switch_tracking(struct test *test, int subtest);
int test__fdarray__filter(struct test *test, int subtest);
int test__fdarray__add(struct test *test, int subtest);
int test__kmod_path__parse(struct test *test, int subtest);
int test__thread_map(struct test *test, int subtest);
int test__llvm(struct test *test, int subtest);
const char *test__llvm_subtest_get_desc(int subtest);
int test__llvm_subtest_get_nr(void);
int test__bpf(struct test *test, int subtest);
const char *test__bpf_subtest_get_desc(int subtest);
int test__bpf_subtest_get_nr(void);
int test__session_topology(struct test *test, int subtest);
int test__thread_map_synthesize(struct test *test, int subtest);
int test__thread_map_remove(struct test *test, int subtest);
int test__cpu_map_synthesize(struct test *test, int subtest);
int test__synthesize_stat_config(struct test *test, int subtest);
int test__synthesize_stat(struct test *test, int subtest);
int test__synthesize_stat_round(struct test *test, int subtest);
int test__event_update(struct test *test, int subtest);
int test__event_times(struct test *test, int subtest);
int test__backward_ring_buffer(struct test *test, int subtest);
int test__cpu_map_print(struct test *test, int subtest);
int test__cpu_map_merge(struct test *test, int subtest);
int test__sdt_event(struct test *test, int subtest);
int test__is_printable_array(struct test *test, int subtest);
int test__bitmap_print(struct test *test, int subtest);
int test__perf_hooks(struct test *test, int subtest);
int test__clang(struct test *test, int subtest);
const char *test__clang_subtest_get_desc(int subtest);
int test__clang_subtest_get_nr(void);
int test__unit_number__scnprint(struct test *test, int subtest);
int test__mem2node(struct test *t, int subtest);
int test__maps__merge_in(struct test *t, int subtest);
int test__time_utils(struct test *t, int subtest);
int test__jit_write_elf(struct test *test, int subtest);
int test__api_io(struct test *test, int subtest);
int test__demangle_java(struct test *test, int subtest);
int test__demangle_ocaml(struct test *test, int subtest);
int test__pfm(struct test *test, int subtest);
const char *test__pfm_subtest_get_desc(int subtest);
int test__pfm_subtest_get_nr(void);
int test__parse_metric(struct test *test, int subtest);
int test__pe_file_parsing(struct test *test, int subtest);
int test__expand_cgroup_events(struct test *test, int subtest);
int test__perf_time_to_tsc(struct test *test, int subtest);
int test__dlfilter(struct test *test, int subtest);

bool test__bp_signal_is_supported(void);
bool test__bp_account_is_supported(void);
bool test__wp_is_supported(void);
bool test__tsc_is_supported(void);

#ifdef HAVE_DWARF_UNWIND_SUPPORT
struct thread;
struct perf_sample;
int test__arch_unwind_sample(struct perf_sample *sample,
			     struct thread *thread);
#endif

#if defined(__arm__)
int test__vectors_page(struct test *test, int subtest);
#endif

#endif /* TESTS_H */
