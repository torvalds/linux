/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ARCH_TESTS_H
#define ARCH_TESTS_H

struct test_suite;

/* Tests */
int test__rdpmc(struct test_suite *test, int subtest);
int test__insn_x86(struct test_suite *test, int subtest);
int test__intel_pt_pkt_decoder(struct test_suite *test, int subtest);
int test__intel_pt_hybrid_compat(struct test_suite *test, int subtest);
int test__bp_modify(struct test_suite *test, int subtest);
int test__x86_sample_parsing(struct test_suite *test, int subtest);
int test__amd_ibs_via_core_pmu(struct test_suite *test, int subtest);

extern struct test_suite *arch_tests[];

#endif
