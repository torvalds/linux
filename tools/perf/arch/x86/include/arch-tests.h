/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ARCH_TESTS_H
#define ARCH_TESTS_H

struct test;

/* Tests */
int test__rdpmc(struct test *test, int subtest);
int test__insn_x86(struct test *test, int subtest);
int test__intel_pt_pkt_decoder(struct test *test, int subtest);
int test__bp_modify(struct test *test, int subtest);
int test__x86_sample_parsing(struct test *test, int subtest);

extern struct test arch_tests[];

#endif
