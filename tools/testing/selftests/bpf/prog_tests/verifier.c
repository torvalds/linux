// SPDX-License-Identifier: GPL-2.0-only

#include <test_progs.h>

#include "cap_helpers.h"
#include "verifier_and.skel.h"
#include "verifier_array_access.skel.h"
#include "verifier_basic_stack.skel.h"
#include "verifier_bounds_deduction.skel.h"
#include "verifier_bounds_mix_sign_unsign.skel.h"
#include "verifier_cfg.skel.h"
#include "verifier_cgroup_inv_retcode.skel.h"
#include "verifier_cgroup_skb.skel.h"
#include "verifier_cgroup_storage.skel.h"
#include "verifier_const_or.skel.h"
#include "verifier_ctx_sk_msg.skel.h"
#include "verifier_direct_stack_access_wraparound.skel.h"
#include "verifier_div0.skel.h"
#include "verifier_div_overflow.skel.h"
#include "verifier_helper_access_var_len.skel.h"
#include "verifier_helper_packet_access.skel.h"
#include "verifier_helper_restricted.skel.h"

__maybe_unused
static void run_tests_aux(const char *skel_name, skel_elf_bytes_fn elf_bytes_factory)
{
	struct test_loader tester = {};
	__u64 old_caps;
	int err;

	/* test_verifier tests are executed w/o CAP_SYS_ADMIN, do the same here */
	err = cap_disable_effective(1ULL << CAP_SYS_ADMIN, &old_caps);
	if (err) {
		PRINT_FAIL("failed to drop CAP_SYS_ADMIN: %i, %s\n", err, strerror(err));
		return;
	}

	test_loader__run_subtests(&tester, skel_name, elf_bytes_factory);
	test_loader_fini(&tester);

	err = cap_enable_effective(old_caps, NULL);
	if (err)
		PRINT_FAIL("failed to restore CAP_SYS_ADMIN: %i, %s\n", err, strerror(err));
}

#define RUN(skel) run_tests_aux(#skel, skel##__elf_bytes)

void test_verifier_and(void)                  { RUN(verifier_and); }
void test_verifier_array_access(void)         { RUN(verifier_array_access); }
void test_verifier_basic_stack(void)          { RUN(verifier_basic_stack); }
void test_verifier_bounds_deduction(void)     { RUN(verifier_bounds_deduction); }
void test_verifier_bounds_mix_sign_unsign(void) { RUN(verifier_bounds_mix_sign_unsign); }
void test_verifier_cfg(void)                  { RUN(verifier_cfg); }
void test_verifier_cgroup_inv_retcode(void)   { RUN(verifier_cgroup_inv_retcode); }
void test_verifier_cgroup_skb(void)           { RUN(verifier_cgroup_skb); }
void test_verifier_cgroup_storage(void)       { RUN(verifier_cgroup_storage); }
void test_verifier_const_or(void)             { RUN(verifier_const_or); }
void test_verifier_ctx_sk_msg(void)           { RUN(verifier_ctx_sk_msg); }
void test_verifier_direct_stack_access_wraparound(void) { RUN(verifier_direct_stack_access_wraparound); }
void test_verifier_div0(void)                 { RUN(verifier_div0); }
void test_verifier_div_overflow(void)         { RUN(verifier_div_overflow); }
void test_verifier_helper_access_var_len(void) { RUN(verifier_helper_access_var_len); }
void test_verifier_helper_packet_access(void) { RUN(verifier_helper_packet_access); }
void test_verifier_helper_restricted(void)    { RUN(verifier_helper_restricted); }
