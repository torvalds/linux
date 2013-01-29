#ifndef TESTS_H
#define TESTS_H

/* Tests */
int test__vmlinux_matches_kallsyms(void);
int test__open_syscall_event(void);
int test__open_syscall_event_on_all_cpus(void);
int test__basic_mmap(void);
int test__PERF_RECORD(void);
int test__rdpmc(void);
int test__perf_evsel__roundtrip_name_test(void);
int test__perf_evsel__tp_sched_test(void);
int test__syscall_open_tp_fields(void);
int test__pmu(void);
int test__attr(void);
int test__dso_data(void);
int test__parse_events(void);

/* Util */
int trace_event__id(const char *evname);

#endif /* TESTS_H */
