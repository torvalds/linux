// SPDX-License-Identifier: GPL-2.0
#include <string.h>
#include "tests/tests.h"
#include "arch-tests.h"

DEFINE_SUITE("x86 rdpmc", rdpmc);
#ifdef HAVE_AUXTRACE_SUPPORT
DEFINE_SUITE("x86 instruction decoder - new instructions", insn_x86);
DEFINE_SUITE("Intel PT packet decoder", intel_pt_pkt_decoder);
#endif
#if defined(__x86_64__)
DEFINE_SUITE("x86 bp modify", bp_modify);
#endif
DEFINE_SUITE("x86 Sample parsing", x86_sample_parsing);

struct test *arch_tests[] = {
	&rdpmc,
#ifdef HAVE_DWARF_UNWIND_SUPPORT
	&dwarf_unwind,
#endif
#ifdef HAVE_AUXTRACE_SUPPORT
	&insn_x86,
	&intel_pt_pkt_decoder,
#endif
#if defined(__x86_64__)
	&bp_modify,
#endif
	&x86_sample_parsing,
	NULL,
};
