// SPDX-License-Identifier: GPL-2.0-only

#include <test_progs.h>

#include "cap_helpers.h"
#include "verifier_and.skel.h"
#include "verifier_array_access.skel.h"
#include "verifier_basic_stack.skel.h"
#include "verifier_bounds.skel.h"
#include "verifier_bounds_deduction.skel.h"
#include "verifier_bounds_deduction_non_const.skel.h"
#include "verifier_bounds_mix_sign_unsign.skel.h"
#include "verifier_bpf_get_stack.skel.h"
#include "verifier_bswap.skel.h"
#include "verifier_btf_ctx_access.skel.h"
#include "verifier_cfg.skel.h"
#include "verifier_cgroup_inv_retcode.skel.h"
#include "verifier_cgroup_skb.skel.h"
#include "verifier_cgroup_storage.skel.h"
#include "verifier_const_or.skel.h"
#include "verifier_ctx.skel.h"
#include "verifier_ctx_sk_msg.skel.h"
#include "verifier_d_path.skel.h"
#include "verifier_direct_packet_access.skel.h"
#include "verifier_direct_stack_access_wraparound.skel.h"
#include "verifier_div0.skel.h"
#include "verifier_div_overflow.skel.h"
#include "verifier_gotol.skel.h"
#include "verifier_helper_access_var_len.skel.h"
#include "verifier_helper_packet_access.skel.h"
#include "verifier_helper_restricted.skel.h"
#include "verifier_helper_value_access.skel.h"
#include "verifier_int_ptr.skel.h"
#include "verifier_iterating_callbacks.skel.h"
#include "verifier_jeq_infer_not_null.skel.h"
#include "verifier_ld_ind.skel.h"
#include "verifier_ldsx.skel.h"
#include "verifier_leak_ptr.skel.h"
#include "verifier_loops1.skel.h"
#include "verifier_lwt.skel.h"
#include "verifier_map_in_map.skel.h"
#include "verifier_map_ptr.skel.h"
#include "verifier_map_ptr_mixing.skel.h"
#include "verifier_map_ret_val.skel.h"
#include "verifier_masking.skel.h"
#include "verifier_meta_access.skel.h"
#include "verifier_movsx.skel.h"
#include "verifier_netfilter_ctx.skel.h"
#include "verifier_netfilter_retcode.skel.h"
#include "verifier_precision.skel.h"
#include "verifier_prevent_map_lookup.skel.h"
#include "verifier_raw_stack.skel.h"
#include "verifier_raw_tp_writable.skel.h"
#include "verifier_reg_equal.skel.h"
#include "verifier_ref_tracking.skel.h"
#include "verifier_regalloc.skel.h"
#include "verifier_ringbuf.skel.h"
#include "verifier_runtime_jit.skel.h"
#include "verifier_scalar_ids.skel.h"
#include "verifier_sdiv.skel.h"
#include "verifier_search_pruning.skel.h"
#include "verifier_sock.skel.h"
#include "verifier_spill_fill.skel.h"
#include "verifier_spin_lock.skel.h"
#include "verifier_stack_ptr.skel.h"
#include "verifier_subprog_precision.skel.h"
#include "verifier_subreg.skel.h"
#include "verifier_typedef.skel.h"
#include "verifier_uninit.skel.h"
#include "verifier_unpriv.skel.h"
#include "verifier_unpriv_perf.skel.h"
#include "verifier_value_adj_spill.skel.h"
#include "verifier_value.skel.h"
#include "verifier_value_illegal_alu.skel.h"
#include "verifier_value_or_null.skel.h"
#include "verifier_value_ptr_arith.skel.h"
#include "verifier_var_off.skel.h"
#include "verifier_xadd.skel.h"
#include "verifier_xdp.skel.h"
#include "verifier_xdp_direct_packet_access.skel.h"

#define MAX_ENTRIES 11

struct test_val {
	unsigned int index;
	int foo[MAX_ENTRIES];
};

__maybe_unused
static void run_tests_aux(const char *skel_name,
			  skel_elf_bytes_fn elf_bytes_factory,
			  pre_execution_cb pre_execution_cb)
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

	test_loader__set_pre_execution_cb(&tester, pre_execution_cb);
	test_loader__run_subtests(&tester, skel_name, elf_bytes_factory);
	test_loader_fini(&tester);

	err = cap_enable_effective(old_caps, NULL);
	if (err)
		PRINT_FAIL("failed to restore CAP_SYS_ADMIN: %i, %s\n", err, strerror(err));
}

#define RUN(skel) run_tests_aux(#skel, skel##__elf_bytes, NULL)

void test_verifier_and(void)                  { RUN(verifier_and); }
void test_verifier_basic_stack(void)          { RUN(verifier_basic_stack); }
void test_verifier_bounds(void)               { RUN(verifier_bounds); }
void test_verifier_bounds_deduction(void)     { RUN(verifier_bounds_deduction); }
void test_verifier_bounds_deduction_non_const(void)     { RUN(verifier_bounds_deduction_non_const); }
void test_verifier_bounds_mix_sign_unsign(void) { RUN(verifier_bounds_mix_sign_unsign); }
void test_verifier_bpf_get_stack(void)        { RUN(verifier_bpf_get_stack); }
void test_verifier_bswap(void)                { RUN(verifier_bswap); }
void test_verifier_btf_ctx_access(void)       { RUN(verifier_btf_ctx_access); }
void test_verifier_cfg(void)                  { RUN(verifier_cfg); }
void test_verifier_cgroup_inv_retcode(void)   { RUN(verifier_cgroup_inv_retcode); }
void test_verifier_cgroup_skb(void)           { RUN(verifier_cgroup_skb); }
void test_verifier_cgroup_storage(void)       { RUN(verifier_cgroup_storage); }
void test_verifier_const_or(void)             { RUN(verifier_const_or); }
void test_verifier_ctx(void)                  { RUN(verifier_ctx); }
void test_verifier_ctx_sk_msg(void)           { RUN(verifier_ctx_sk_msg); }
void test_verifier_d_path(void)               { RUN(verifier_d_path); }
void test_verifier_direct_packet_access(void) { RUN(verifier_direct_packet_access); }
void test_verifier_direct_stack_access_wraparound(void) { RUN(verifier_direct_stack_access_wraparound); }
void test_verifier_div0(void)                 { RUN(verifier_div0); }
void test_verifier_div_overflow(void)         { RUN(verifier_div_overflow); }
void test_verifier_gotol(void)                { RUN(verifier_gotol); }
void test_verifier_helper_access_var_len(void) { RUN(verifier_helper_access_var_len); }
void test_verifier_helper_packet_access(void) { RUN(verifier_helper_packet_access); }
void test_verifier_helper_restricted(void)    { RUN(verifier_helper_restricted); }
void test_verifier_helper_value_access(void)  { RUN(verifier_helper_value_access); }
void test_verifier_int_ptr(void)              { RUN(verifier_int_ptr); }
void test_verifier_iterating_callbacks(void)  { RUN(verifier_iterating_callbacks); }
void test_verifier_jeq_infer_not_null(void)   { RUN(verifier_jeq_infer_not_null); }
void test_verifier_ld_ind(void)               { RUN(verifier_ld_ind); }
void test_verifier_ldsx(void)                  { RUN(verifier_ldsx); }
void test_verifier_leak_ptr(void)             { RUN(verifier_leak_ptr); }
void test_verifier_loops1(void)               { RUN(verifier_loops1); }
void test_verifier_lwt(void)                  { RUN(verifier_lwt); }
void test_verifier_map_in_map(void)           { RUN(verifier_map_in_map); }
void test_verifier_map_ptr(void)              { RUN(verifier_map_ptr); }
void test_verifier_map_ptr_mixing(void)       { RUN(verifier_map_ptr_mixing); }
void test_verifier_map_ret_val(void)          { RUN(verifier_map_ret_val); }
void test_verifier_masking(void)              { RUN(verifier_masking); }
void test_verifier_meta_access(void)          { RUN(verifier_meta_access); }
void test_verifier_movsx(void)                 { RUN(verifier_movsx); }
void test_verifier_netfilter_ctx(void)        { RUN(verifier_netfilter_ctx); }
void test_verifier_netfilter_retcode(void)    { RUN(verifier_netfilter_retcode); }
void test_verifier_precision(void)            { RUN(verifier_precision); }
void test_verifier_prevent_map_lookup(void)   { RUN(verifier_prevent_map_lookup); }
void test_verifier_raw_stack(void)            { RUN(verifier_raw_stack); }
void test_verifier_raw_tp_writable(void)      { RUN(verifier_raw_tp_writable); }
void test_verifier_reg_equal(void)            { RUN(verifier_reg_equal); }
void test_verifier_ref_tracking(void)         { RUN(verifier_ref_tracking); }
void test_verifier_regalloc(void)             { RUN(verifier_regalloc); }
void test_verifier_ringbuf(void)              { RUN(verifier_ringbuf); }
void test_verifier_runtime_jit(void)          { RUN(verifier_runtime_jit); }
void test_verifier_scalar_ids(void)           { RUN(verifier_scalar_ids); }
void test_verifier_sdiv(void)                 { RUN(verifier_sdiv); }
void test_verifier_search_pruning(void)       { RUN(verifier_search_pruning); }
void test_verifier_sock(void)                 { RUN(verifier_sock); }
void test_verifier_spill_fill(void)           { RUN(verifier_spill_fill); }
void test_verifier_spin_lock(void)            { RUN(verifier_spin_lock); }
void test_verifier_stack_ptr(void)            { RUN(verifier_stack_ptr); }
void test_verifier_subprog_precision(void)    { RUN(verifier_subprog_precision); }
void test_verifier_subreg(void)               { RUN(verifier_subreg); }
void test_verifier_typedef(void)              { RUN(verifier_typedef); }
void test_verifier_uninit(void)               { RUN(verifier_uninit); }
void test_verifier_unpriv(void)               { RUN(verifier_unpriv); }
void test_verifier_unpriv_perf(void)          { RUN(verifier_unpriv_perf); }
void test_verifier_value_adj_spill(void)      { RUN(verifier_value_adj_spill); }
void test_verifier_value(void)                { RUN(verifier_value); }
void test_verifier_value_illegal_alu(void)    { RUN(verifier_value_illegal_alu); }
void test_verifier_value_or_null(void)        { RUN(verifier_value_or_null); }
void test_verifier_var_off(void)              { RUN(verifier_var_off); }
void test_verifier_xadd(void)                 { RUN(verifier_xadd); }
void test_verifier_xdp(void)                  { RUN(verifier_xdp); }
void test_verifier_xdp_direct_packet_access(void) { RUN(verifier_xdp_direct_packet_access); }

static int init_test_val_map(struct bpf_object *obj, char *map_name)
{
	struct test_val value = {
		.index = (6 + 1) * sizeof(int),
		.foo[6] = 0xabcdef12,
	};
	struct bpf_map *map;
	int err, key = 0;

	map = bpf_object__find_map_by_name(obj, map_name);
	if (!map) {
		PRINT_FAIL("Can't find map '%s'\n", map_name);
		return -EINVAL;
	}

	err = bpf_map_update_elem(bpf_map__fd(map), &key, &value, 0);
	if (err) {
		PRINT_FAIL("Error while updating map '%s': %d\n", map_name, err);
		return err;
	}

	return 0;
}

static int init_array_access_maps(struct bpf_object *obj)
{
	return init_test_val_map(obj, "map_array_ro");
}

void test_verifier_array_access(void)
{
	run_tests_aux("verifier_array_access",
		      verifier_array_access__elf_bytes,
		      init_array_access_maps);
}

static int init_value_ptr_arith_maps(struct bpf_object *obj)
{
	return init_test_val_map(obj, "map_array_48b");
}

void test_verifier_value_ptr_arith(void)
{
	run_tests_aux("verifier_value_ptr_arith",
		      verifier_value_ptr_arith__elf_bytes,
		      init_value_ptr_arith_maps);
}
