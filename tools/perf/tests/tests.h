#ifndef TESTS_H
#define TESTS_H

/* Tests */
int test__vmlinux_matches_kallsyms(void);
int test__open_syscall_event(void);
int test__open_syscall_event_on_all_cpus(void);
int test__basic_mmap(void);
int test__PERF_RECORD(void);

/* Util */
int trace_event__id(const char *evname);

#endif /* TESTS_H */
