#ifndef ARCH_TESTS_H
#define ARCH_TESTS_H

/* Tests */
int test__rdpmc(int subtest);
int test__perf_time_to_tsc(int subtest);
int test__insn_x86(int subtest);
int test__intel_cqm_count_nmi_context(int subtest);

#ifdef HAVE_DWARF_UNWIND_SUPPORT
struct thread;
struct perf_sample;
int test__arch_unwind_sample(struct perf_sample *sample,
			     struct thread *thread);
#endif

extern struct test arch_tests[];

#endif
