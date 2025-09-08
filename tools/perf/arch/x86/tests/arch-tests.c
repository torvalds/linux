// SPDX-License-Identifier: GPL-2.0
#include <string.h>
#include "tests/tests.h"
#include "arch-tests.h"

#ifdef HAVE_AUXTRACE_SUPPORT
#ifdef HAVE_EXTRA_TESTS
DEFINE_SUITE("x86 instruction decoder - new instructions", insn_x86);
#endif

static struct test_case intel_pt_tests[] = {
	TEST_CASE("Intel PT packet decoder", intel_pt_pkt_decoder),
	TEST_CASE("Intel PT hybrid CPU compatibility", intel_pt_hybrid_compat),
	{ .name = NULL, }
};

struct test_suite suite__intel_pt = {
	.desc = "Intel PT",
	.test_cases = intel_pt_tests,
};

#endif
#if defined(__x86_64__)
DEFINE_SUITE("x86 bp modify", bp_modify);
#endif
DEFINE_SUITE("AMD IBS via core pmu", amd_ibs_via_core_pmu);
DEFINE_SUITE_EXCLUSIVE("AMD IBS sample period", amd_ibs_period);
static struct test_case hybrid_tests[] = {
	TEST_CASE_REASON("x86 hybrid event parsing", hybrid, "not hybrid"),
	{ .name = NULL, }
};

struct test_suite suite__hybrid = {
	.desc = "x86 hybrid",
	.test_cases = hybrid_tests,
};

struct test_suite *arch_tests[] = {
#ifdef HAVE_DWARF_UNWIND_SUPPORT
	&suite__dwarf_unwind,
#endif
#ifdef HAVE_AUXTRACE_SUPPORT
#ifdef HAVE_EXTRA_TESTS
	&suite__insn_x86,
#endif
	&suite__intel_pt,
#endif
#if defined(__x86_64__)
	&suite__bp_modify,
#endif
	&suite__amd_ibs_via_core_pmu,
	&suite__amd_ibs_period,
	&suite__hybrid,
	&suite__x86_topdown,
	NULL,
};
