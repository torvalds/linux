// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <network_helpers.h>

#include "map_kptr.skel.h"
#include "map_kptr_fail.skel.h"

static char log_buf[1024 * 1024];

struct {
	const char *prog_name;
	const char *err_msg;
} map_kptr_fail_tests[] = {
	{ "size_not_bpf_dw", "kptr access size must be BPF_DW" },
	{ "non_const_var_off", "kptr access cannot have variable offset" },
	{ "non_const_var_off_kptr_xchg", "R1 doesn't have constant offset. kptr has to be" },
	{ "misaligned_access_write", "kptr access misaligned expected=8 off=7" },
	{ "misaligned_access_read", "kptr access misaligned expected=8 off=1" },
	{ "reject_var_off_store", "variable untrusted_ptr_ access var_off=(0x0; 0x1e0)" },
	{ "reject_bad_type_match", "invalid kptr access, R1 type=untrusted_ptr_prog_test_ref_kfunc" },
	{ "marked_as_untrusted_or_null", "R1 type=untrusted_ptr_or_null_ expected=percpu_ptr_" },
	{ "correct_btf_id_check_size", "access beyond struct prog_test_ref_kfunc at off 32 size 4" },
	{ "inherit_untrusted_on_walk", "R1 type=untrusted_ptr_ expected=percpu_ptr_" },
	{ "reject_kptr_xchg_on_unref", "off=8 kptr isn't referenced kptr" },
	{ "reject_kptr_get_no_map_val", "arg#0 expected pointer to map value" },
	{ "reject_kptr_get_no_null_map_val", "arg#0 expected pointer to map value" },
	{ "reject_kptr_get_no_kptr", "arg#0 no referenced kptr at map value offset=0" },
	{ "reject_kptr_get_on_unref", "arg#0 no referenced kptr at map value offset=8" },
	{ "reject_kptr_get_bad_type_match", "kernel function bpf_kfunc_call_test_kptr_get args#0" },
	{ "mark_ref_as_untrusted_or_null", "R1 type=untrusted_ptr_or_null_ expected=percpu_ptr_" },
	{ "reject_untrusted_store_to_ref", "store to referenced kptr disallowed" },
	{ "reject_bad_type_xchg", "invalid kptr access, R2 type=ptr_prog_test_ref_kfunc expected=ptr_prog_test_member" },
	{ "reject_untrusted_xchg", "R2 type=untrusted_ptr_ expected=ptr_" },
	{ "reject_member_of_ref_xchg", "invalid kptr access, R2 type=ptr_prog_test_ref_kfunc" },
	{ "reject_indirect_helper_access", "kptr cannot be accessed indirectly by helper" },
	{ "reject_indirect_global_func_access", "kptr cannot be accessed indirectly by helper" },
	{ "kptr_xchg_ref_state", "Unreleased reference id=5 alloc_insn=" },
	{ "kptr_get_ref_state", "Unreleased reference id=3 alloc_insn=" },
};

static void test_map_kptr_fail_prog(const char *prog_name, const char *err_msg)
{
	LIBBPF_OPTS(bpf_object_open_opts, opts, .kernel_log_buf = log_buf,
						.kernel_log_size = sizeof(log_buf),
						.kernel_log_level = 1);
	struct map_kptr_fail *skel;
	struct bpf_program *prog;
	int ret;

	skel = map_kptr_fail__open_opts(&opts);
	if (!ASSERT_OK_PTR(skel, "map_kptr_fail__open_opts"))
		return;

	prog = bpf_object__find_program_by_name(skel->obj, prog_name);
	if (!ASSERT_OK_PTR(prog, "bpf_object__find_program_by_name"))
		goto end;

	bpf_program__set_autoload(prog, true);

	ret = map_kptr_fail__load(skel);
	if (!ASSERT_ERR(ret, "map_kptr__load must fail"))
		goto end;

	if (!ASSERT_OK_PTR(strstr(log_buf, err_msg), "expected error message")) {
		fprintf(stderr, "Expected: %s\n", err_msg);
		fprintf(stderr, "Verifier: %s\n", log_buf);
	}

end:
	map_kptr_fail__destroy(skel);
}

static void test_map_kptr_fail(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(map_kptr_fail_tests); i++) {
		if (!test__start_subtest(map_kptr_fail_tests[i].prog_name))
			continue;
		test_map_kptr_fail_prog(map_kptr_fail_tests[i].prog_name,
					map_kptr_fail_tests[i].err_msg);
	}
}

static void test_map_kptr_success(bool test_run)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.repeat = 1,
	);
	struct map_kptr *skel;
	int key = 0, ret;
	char buf[16];

	skel = map_kptr__open_and_load();
	if (!ASSERT_OK_PTR(skel, "map_kptr__open_and_load"))
		return;

	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.test_map_kptr_ref), &opts);
	ASSERT_OK(ret, "test_map_kptr_ref refcount");
	ASSERT_OK(opts.retval, "test_map_kptr_ref retval");
	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.test_map_kptr_ref2), &opts);
	ASSERT_OK(ret, "test_map_kptr_ref2 refcount");
	ASSERT_OK(opts.retval, "test_map_kptr_ref2 retval");

	if (test_run)
		goto exit;

	ret = bpf_map__update_elem(skel->maps.array_map,
				   &key, sizeof(key), buf, sizeof(buf), 0);
	ASSERT_OK(ret, "array_map update");
	ret = bpf_map__update_elem(skel->maps.array_map,
				   &key, sizeof(key), buf, sizeof(buf), 0);
	ASSERT_OK(ret, "array_map update2");

	ret = bpf_map__update_elem(skel->maps.hash_map,
				   &key, sizeof(key), buf, sizeof(buf), 0);
	ASSERT_OK(ret, "hash_map update");
	ret = bpf_map__delete_elem(skel->maps.hash_map, &key, sizeof(key), 0);
	ASSERT_OK(ret, "hash_map delete");

	ret = bpf_map__update_elem(skel->maps.hash_malloc_map,
				   &key, sizeof(key), buf, sizeof(buf), 0);
	ASSERT_OK(ret, "hash_malloc_map update");
	ret = bpf_map__delete_elem(skel->maps.hash_malloc_map, &key, sizeof(key), 0);
	ASSERT_OK(ret, "hash_malloc_map delete");

	ret = bpf_map__update_elem(skel->maps.lru_hash_map,
				   &key, sizeof(key), buf, sizeof(buf), 0);
	ASSERT_OK(ret, "lru_hash_map update");
	ret = bpf_map__delete_elem(skel->maps.lru_hash_map, &key, sizeof(key), 0);
	ASSERT_OK(ret, "lru_hash_map delete");

exit:
	map_kptr__destroy(skel);
}

void test_map_kptr(void)
{
	if (test__start_subtest("success")) {
		test_map_kptr_success(false);
		/* Do test_run twice, so that we see refcount going back to 1
		 * after we leave it in map from first iteration.
		 */
		test_map_kptr_success(true);
	}
	test_map_kptr_fail();
}
