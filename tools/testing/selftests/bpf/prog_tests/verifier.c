// SPDX-License-Identifier: GPL-2.0-only

#include <test_progs.h>

#include "cap_helpers.h"
#include "verifier_and.skel.h"
#include "verifier_array_access.skel.h"
#include "verifier_basic_stack.skel.h"
#include "verifier_bounds_deduction.skel.h"
#include "verifier_bounds_deduction_non_const.skel.h"
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
#include "verifier_helper_value_access.skel.h"
#include "verifier_int_ptr.skel.h"
#include "verifier_ld_ind.skel.h"
#include "verifier_leak_ptr.skel.h"
#include "verifier_map_ptr.skel.h"
#include "verifier_map_ret_val.skel.h"
#include "verifier_masking.skel.h"
#include "verifier_meta_access.skel.h"
#include "verifier_raw_stack.skel.h"
#include "verifier_raw_tp_writable.skel.h"
#include "verifier_reg_equal.skel.h"
#include "verifier_ringbuf.skel.h"
#include "verifier_spill_fill.skel.h"
#include "verifier_stack_ptr.skel.h"
#include "verifier_uninit.skel.h"
#include "verifier_value_adj_spill.skel.h"
#include "verifier_value.skel.h"
#include "verifier_value_or_null.skel.h"
#include "verifier_var_off.skel.h"
#include "verifier_xadd.skel.h"
#include "verifier_xdp.skel.h"
#include "verifier_xdp_direct_packet_access.skel.h"

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
void test_verifier_bounds_deduction_non_const(void)     { RUN(verifier_bounds_deduction_non_const); }
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
void test_verifier_helper_value_access(void)  { RUN(verifier_helper_value_access); }
void test_verifier_int_ptr(void)              { RUN(verifier_int_ptr); }
void test_verifier_ld_ind(void)               { RUN(verifier_ld_ind); }
void test_verifier_leak_ptr(void)             { RUN(verifier_leak_ptr); }
void test_verifier_map_ptr(void)              { RUN(verifier_map_ptr); }
void test_verifier_map_ret_val(void)          { RUN(verifier_map_ret_val); }
void test_verifier_masking(void)              { RUN(verifier_masking); }
void test_verifier_meta_access(void)          { RUN(verifier_meta_access); }
void test_verifier_raw_stack(void)            { RUN(verifier_raw_stack); }
void test_verifier_raw_tp_writable(void)      { RUN(verifier_raw_tp_writable); }
void test_verifier_reg_equal(void)            { RUN(verifier_reg_equal); }
void test_verifier_ringbuf(void)              { RUN(verifier_ringbuf); }
void test_verifier_spill_fill(void)           { RUN(verifier_spill_fill); }
void test_verifier_stack_ptr(void)            { RUN(verifier_stack_ptr); }
void test_verifier_uninit(void)               { RUN(verifier_uninit); }
void test_verifier_value_adj_spill(void)      { RUN(verifier_value_adj_spill); }
void test_verifier_value(void)                { RUN(verifier_value); }
void test_verifier_value_or_null(void)        { RUN(verifier_value_or_null); }
void test_verifier_var_off(void)              { RUN(verifier_var_off); }
void test_verifier_xadd(void)                 { RUN(verifier_xadd); }
void test_verifier_xdp(void)                  { RUN(verifier_xdp); }
void test_verifier_xdp_direct_packet_access(void) { RUN(verifier_xdp_direct_packet_access); }
